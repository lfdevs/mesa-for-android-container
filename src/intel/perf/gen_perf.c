/*
 * Copyright © 2018 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <dirent.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <drm-uapi/i915_drm.h>

#include "common/gen_gem.h"
#include "gen_perf.h"
#include "gen_perf_regs.h"
#include "perf/gen_perf_mdapi.h"
#include "perf/gen_perf_metrics.h"

#include "dev/gen_debug.h"
#include "dev/gen_device_info.h"
#include "util/bitscan.h"
#include "util/mesa-sha1.h"
#include "util/u_math.h"

#define FILE_DEBUG_FLAG DEBUG_PERFMON
#define MI_RPC_BO_SIZE              4096
#define MI_FREQ_START_OFFSET_BYTES  (3072)
#define MI_RPC_BO_END_OFFSET_BYTES  (MI_RPC_BO_SIZE / 2)
#define MI_FREQ_END_OFFSET_BYTES    (3076)

#define INTEL_MASK(high, low) (((1u<<((high)-(low)+1))-1)<<(low))

#define GEN7_RPSTAT1                       0xA01C
#define  GEN7_RPSTAT1_CURR_GT_FREQ_SHIFT   7
#define  GEN7_RPSTAT1_CURR_GT_FREQ_MASK    INTEL_MASK(13, 7)
#define  GEN7_RPSTAT1_PREV_GT_FREQ_SHIFT   0
#define  GEN7_RPSTAT1_PREV_GT_FREQ_MASK    INTEL_MASK(6, 0)

#define GEN9_RPSTAT0                       0xA01C
#define  GEN9_RPSTAT0_CURR_GT_FREQ_SHIFT   23
#define  GEN9_RPSTAT0_CURR_GT_FREQ_MASK    INTEL_MASK(31, 23)
#define  GEN9_RPSTAT0_PREV_GT_FREQ_SHIFT   0
#define  GEN9_RPSTAT0_PREV_GT_FREQ_MASK    INTEL_MASK(8, 0)

#define GEN6_SO_PRIM_STORAGE_NEEDED     0x2280
#define GEN7_SO_PRIM_STORAGE_NEEDED(n)  (0x5240 + (n) * 8)
#define GEN6_SO_NUM_PRIMS_WRITTEN       0x2288
#define GEN7_SO_NUM_PRIMS_WRITTEN(n)    (0x5200 + (n) * 8)

#define MAP_READ  (1 << 0)
#define MAP_WRITE (1 << 1)

#define OA_REPORT_INVALID_CTX_ID (0xffffffff)

/**
 * Periodic OA samples are read() into these buffer structures via the
 * i915 perf kernel interface and appended to the
 * perf_ctx->sample_buffers linked list. When we process the
 * results of an OA metrics query we need to consider all the periodic
 * samples between the Begin and End MI_REPORT_PERF_COUNT command
 * markers.
 *
 * 'Periodic' is a simplification as there are other automatic reports
 * written by the hardware also buffered here.
 *
 * Considering three queries, A, B and C:
 *
 *  Time ---->
 *                ________________A_________________
 *                |                                |
 *                | ________B_________ _____C___________
 *                | |                | |           |   |
 *
 * And an illustration of sample buffers read over this time frame:
 * [HEAD ][     ][     ][     ][     ][     ][     ][     ][TAIL ]
 *
 * These nodes may hold samples for query A:
 * [     ][     ][  A  ][  A  ][  A  ][  A  ][  A  ][     ][     ]
 *
 * These nodes may hold samples for query B:
 * [     ][     ][  B  ][  B  ][  B  ][     ][     ][     ][     ]
 *
 * These nodes may hold samples for query C:
 * [     ][     ][     ][     ][     ][  C  ][  C  ][  C  ][     ]
 *
 * The illustration assumes we have an even distribution of periodic
 * samples so all nodes have the same size plotted against time:
 *
 * Note, to simplify code, the list is never empty.
 *
 * With overlapping queries we can see that periodic OA reports may
 * relate to multiple queries and care needs to be take to keep
 * track of sample buffers until there are no queries that might
 * depend on their contents.
 *
 * We use a node ref counting system where a reference ensures that a
 * node and all following nodes can't be freed/recycled until the
 * reference drops to zero.
 *
 * E.g. with a ref of one here:
 * [  0  ][  0  ][  1  ][  0  ][  0  ][  0  ][  0  ][  0  ][  0  ]
 *
 * These nodes could be freed or recycled ("reaped"):
 * [  0  ][  0  ]
 *
 * These must be preserved until the leading ref drops to zero:
 *               [  1  ][  0  ][  0  ][  0  ][  0  ][  0  ][  0  ]
 *
 * When a query starts we take a reference on the current tail of
 * the list, knowing that no already-buffered samples can possibly
 * relate to the newly-started query. A pointer to this node is
 * also saved in the query object's ->oa.samples_head.
 *
 * E.g. starting query A while there are two nodes in .sample_buffers:
 *                ________________A________
 *                |
 *
 * [  0  ][  1  ]
 *           ^_______ Add a reference and store pointer to node in
 *                    A->oa.samples_head
 *
 * Moving forward to when the B query starts with no new buffer nodes:
 * (for reference, i915 perf reads() are only done when queries finish)
 *                ________________A_______
 *                | ________B___
 *                | |
 *
 * [  0  ][  2  ]
 *           ^_______ Add a reference and store pointer to
 *                    node in B->oa.samples_head
 *
 * Once a query is finished, after an OA query has become 'Ready',
 * once the End OA report has landed and after we we have processed
 * all the intermediate periodic samples then we drop the
 * ->oa.samples_head reference we took at the start.
 *
 * So when the B query has finished we have:
 *                ________________A________
 *                | ______B___________
 *                | |                |
 * [  0  ][  1  ][  0  ][  0  ][  0  ]
 *           ^_______ Drop B->oa.samples_head reference
 *
 * We still can't free these due to the A->oa.samples_head ref:
 *        [  1  ][  0  ][  0  ][  0  ]
 *
 * When the A query finishes: (note there's a new ref for C's samples_head)
 *                ________________A_________________
 *                |                                |
 *                |                    _____C_________
 *                |                    |           |
 * [  0  ][  0  ][  0  ][  0  ][  1  ][  0  ][  0  ]
 *           ^_______ Drop A->oa.samples_head reference
 *
 * And we can now reap these nodes up to the C->oa.samples_head:
 * [  X  ][  X  ][  X  ][  X  ]
 *                  keeping -> [  1  ][  0  ][  0  ]
 *
 * We reap old sample buffers each time we finish processing an OA
 * query by iterating the sample_buffers list from the head until we
 * find a referenced node and stop.
 *
 * Reaped buffers move to a perfquery.free_sample_buffers list and
 * when we come to read() we first look to recycle a buffer from the
 * free_sample_buffers list before allocating a new buffer.
 */
struct oa_sample_buf {
   struct exec_node link;
   int refcount;
   int len;
   uint8_t buf[I915_PERF_OA_SAMPLE_SIZE * 10];
   uint32_t last_timestamp;
};

/**
 * gen representation of a performance query object.
 *
 * NB: We want to keep this structure relatively lean considering that
 * applications may expect to allocate enough objects to be able to
 * query around all draw calls in a frame.
 */
struct gen_perf_query_object
{
   const struct gen_perf_query_info *queryinfo;

   /* See query->kind to know which state below is in use... */
   union {
      struct {

         /**
          * BO containing OA counter snapshots at query Begin/End time.
          */
         void *bo;

         /**
          * Address of mapped of @bo
          */
         void *map;

         /**
          * The MI_REPORT_PERF_COUNT command lets us specify a unique
          * ID that will be reflected in the resulting OA report
          * that's written by the GPU. This is the ID we're expecting
          * in the begin report and the the end report should be
          * @begin_report_id + 1.
          */
         int begin_report_id;

         /**
          * Reference the head of the brw->perfquery.sample_buffers
          * list at the time that the query started (so we only need
          * to look at nodes after this point when looking for samples
          * related to this query)
          *
          * (See struct brw_oa_sample_buf description for more details)
          */
         struct exec_node *samples_head;

         /**
          * false while in the unaccumulated_elements list, and set to
          * true when the final, end MI_RPC snapshot has been
          * accumulated.
          */
         bool results_accumulated;

         /**
          * Frequency of the GT at begin and end of the query.
          */
         uint64_t gt_frequency[2];

         /**
          * Accumulated OA results between begin and end of the query.
          */
         struct gen_perf_query_result result;
      } oa;

      struct {
         /**
          * BO containing starting and ending snapshots for the
          * statistics counters.
          */
         void *bo;
      } pipeline_stats;
   };
};

struct gen_perf_context {
   struct gen_perf_config *perf;

   void * ctx;  /* driver context (eg, brw_context) */
   void * bufmgr;
   const struct gen_device_info *devinfo;

   uint32_t hw_ctx;
   int drm_fd;

   /* The i915 perf stream we open to setup + enable the OA counters */
   int oa_stream_fd;

   /* An i915 perf stream fd gives exclusive access to the OA unit that will
    * report counter snapshots for a specific counter set/profile in a
    * specific layout/format so we can only start OA queries that are
    * compatible with the currently open fd...
    */
   int current_oa_metrics_set_id;
   int current_oa_format;

   /* List of buffers containing OA reports */
   struct exec_list sample_buffers;

   /* Cached list of empty sample buffers */
   struct exec_list free_sample_buffers;

   int n_active_oa_queries;
   int n_active_pipeline_stats_queries;

   /* The number of queries depending on running OA counters which
    * extends beyond brw_end_perf_query() since we need to wait until
    * the last MI_RPC command has parsed by the GPU.
    *
    * Accurate accounting is important here as emitting an
    * MI_REPORT_PERF_COUNT command while the OA unit is disabled will
    * effectively hang the gpu.
    */
   int n_oa_users;

   /* To help catch an spurious problem with the hardware or perf
    * forwarding samples, we emit each MI_REPORT_PERF_COUNT command
    * with a unique ID that we can explicitly check for...
    */
   int next_query_start_report_id;

   /**
    * An array of queries whose results haven't yet been assembled
    * based on the data in buffer objects.
    *
    * These may be active, or have already ended.  However, the
    * results have not been requested.
    */
   struct gen_perf_query_object **unaccumulated;
   int unaccumulated_elements;
   int unaccumulated_array_size;

   /* The total number of query objects so we can relinquish
    * our exclusive access to perf if the application deletes
    * all of its objects. (NB: We only disable perf while
    * there are no active queries)
    */
   int n_query_instances;
};

const struct gen_perf_query_info*
gen_perf_query_info(const struct gen_perf_query_object *query)
{
   return query->queryinfo;
}

struct gen_perf_context *
gen_perf_new_context(void *parent)
{
   struct gen_perf_context *ctx = rzalloc(parent, struct gen_perf_context);
   if (! ctx)
      fprintf(stderr, "%s: failed to alloc context\n", __func__);
   return ctx;
}

struct gen_perf_config *
gen_perf_config(struct gen_perf_context *ctx)
{
   return ctx->perf;
}

struct gen_perf_query_object *
gen_perf_new_query(struct gen_perf_context *perf_ctx, unsigned query_index)
{
   const struct gen_perf_query_info *query =
      &perf_ctx->perf->queries[query_index];
   struct gen_perf_query_object *obj =
      calloc(1, sizeof(struct gen_perf_query_object));

   if (!obj)
      return NULL;

   obj->queryinfo = query;

   perf_ctx->n_query_instances++;
   return obj;
}

int
gen_perf_active_queries(struct gen_perf_context *perf_ctx,
                        const struct gen_perf_query_info *query)
{
   assert(perf_ctx->n_active_oa_queries == 0 || perf_ctx->n_active_pipeline_stats_queries == 0);

   switch (query->kind) {
   case GEN_PERF_QUERY_TYPE_OA:
   case GEN_PERF_QUERY_TYPE_RAW:
      return perf_ctx->n_active_oa_queries;
      break;

   case GEN_PERF_QUERY_TYPE_PIPELINE:
      return perf_ctx->n_active_pipeline_stats_queries;
      break;

   default:
      unreachable("Unknown query type");
      break;
   }
}

static inline uint64_t to_user_pointer(void *ptr)
{
   return (uintptr_t) ptr;
}

static bool
get_sysfs_dev_dir(struct gen_perf_config *perf, int fd)
{
   struct stat sb;
   int min, maj;
   DIR *drmdir;
   struct dirent *drm_entry;
   int len;

   perf->sysfs_dev_dir[0] = '\0';

   if (fstat(fd, &sb)) {
      DBG("Failed to stat DRM fd\n");
      return false;
   }

   maj = major(sb.st_rdev);
   min = minor(sb.st_rdev);

   if (!S_ISCHR(sb.st_mode)) {
      DBG("DRM fd is not a character device as expected\n");
      return false;
   }

   len = snprintf(perf->sysfs_dev_dir,
                  sizeof(perf->sysfs_dev_dir),
                  "/sys/dev/char/%d:%d/device/drm", maj, min);
   if (len < 0 || len >= sizeof(perf->sysfs_dev_dir)) {
      DBG("Failed to concatenate sysfs path to drm device\n");
      return false;
   }

   drmdir = opendir(perf->sysfs_dev_dir);
   if (!drmdir) {
      DBG("Failed to open %s: %m\n", perf->sysfs_dev_dir);
      return false;
   }

   while ((drm_entry = readdir(drmdir))) {
      if ((drm_entry->d_type == DT_DIR ||
           drm_entry->d_type == DT_LNK) &&
          strncmp(drm_entry->d_name, "card", 4) == 0)
      {
         len = snprintf(perf->sysfs_dev_dir,
                        sizeof(perf->sysfs_dev_dir),
                        "/sys/dev/char/%d:%d/device/drm/%s",
                        maj, min, drm_entry->d_name);
         closedir(drmdir);
         if (len < 0 || len >= sizeof(perf->sysfs_dev_dir))
            return false;
         else
            return true;
      }
   }

   closedir(drmdir);

   DBG("Failed to find cardX directory under /sys/dev/char/%d:%d/device/drm\n",
       maj, min);

   return false;
}

static bool
read_file_uint64(const char *file, uint64_t *val)
{
    char buf[32];
    int fd, n;

    fd = open(file, 0);
    if (fd < 0)
       return false;
    while ((n = read(fd, buf, sizeof (buf) - 1)) < 0 &&
           errno == EINTR);
    close(fd);
    if (n < 0)
       return false;

    buf[n] = '\0';
    *val = strtoull(buf, NULL, 0);

    return true;
}

static bool
read_sysfs_drm_device_file_uint64(struct gen_perf_config *perf,
                                  const char *file,
                                  uint64_t *value)
{
   char buf[512];
   int len;

   len = snprintf(buf, sizeof(buf), "%s/%s", perf->sysfs_dev_dir, file);
   if (len < 0 || len >= sizeof(buf)) {
      DBG("Failed to concatenate sys filename to read u64 from\n");
      return false;
   }

   return read_file_uint64(buf, value);
}

static inline struct gen_perf_query_info *
append_query_info(struct gen_perf_config *perf, int max_counters)
{
   struct gen_perf_query_info *query;

   perf->queries = reralloc(perf, perf->queries,
                            struct gen_perf_query_info,
                            ++perf->n_queries);
   query = &perf->queries[perf->n_queries - 1];
   memset(query, 0, sizeof(*query));

   if (max_counters > 0) {
      query->max_counters = max_counters;
      query->counters =
         rzalloc_array(perf, struct gen_perf_query_counter, max_counters);
   }

   return query;
}

static void
register_oa_config(struct gen_perf_config *perf,
                   const struct gen_perf_query_info *query,
                   uint64_t config_id)
{
   struct gen_perf_query_info *registered_query = append_query_info(perf, 0);

   *registered_query = *query;
   registered_query->oa_metrics_set_id = config_id;
   DBG("metric set registered: id = %" PRIu64", guid = %s\n",
       registered_query->oa_metrics_set_id, query->guid);
}

static void
enumerate_sysfs_metrics(struct gen_perf_config *perf)
{
   DIR *metricsdir = NULL;
   struct dirent *metric_entry;
   char buf[256];
   int len;

   len = snprintf(buf, sizeof(buf), "%s/metrics", perf->sysfs_dev_dir);
   if (len < 0 || len >= sizeof(buf)) {
      DBG("Failed to concatenate path to sysfs metrics/ directory\n");
      return;
   }

   metricsdir = opendir(buf);
   if (!metricsdir) {
      DBG("Failed to open %s: %m\n", buf);
      return;
   }

   while ((metric_entry = readdir(metricsdir))) {
      struct hash_entry *entry;

      if ((metric_entry->d_type != DT_DIR &&
           metric_entry->d_type != DT_LNK) ||
          metric_entry->d_name[0] == '.')
         continue;

      DBG("metric set: %s\n", metric_entry->d_name);
      entry = _mesa_hash_table_search(perf->oa_metrics_table,
                                      metric_entry->d_name);
      if (entry) {
         uint64_t id;
         if (!gen_perf_load_metric_id(perf, metric_entry->d_name, &id)) {
            DBG("Failed to read metric set id from %s: %m", buf);
            continue;
         }

         register_oa_config(perf, (const struct gen_perf_query_info *)entry->data, id);
      } else
         DBG("metric set not known by mesa (skipping)\n");
   }

   closedir(metricsdir);
}

static bool
kernel_has_dynamic_config_support(struct gen_perf_config *perf, int fd)
{
   uint64_t invalid_config_id = UINT64_MAX;

   return gen_ioctl(fd, DRM_IOCTL_I915_PERF_REMOVE_CONFIG,
                    &invalid_config_id) < 0 && errno == ENOENT;
}

static int
i915_query_items(struct gen_perf_config *perf, int fd,
                 struct drm_i915_query_item *items, uint32_t n_items)
{
   struct drm_i915_query q = {
      .num_items = n_items,
      .items_ptr = to_user_pointer(items),
   };
   return gen_ioctl(fd, DRM_IOCTL_I915_QUERY, &q);
}

static bool
i915_query_perf_config_supported(struct gen_perf_config *perf, int fd)
{
   struct drm_i915_query_item item = {
      .query_id = DRM_I915_QUERY_PERF_CONFIG,
      .flags = DRM_I915_QUERY_PERF_CONFIG_LIST,
   };

   return i915_query_items(perf, fd, &item, 1) == 0 && item.length > 0;
}

static bool
i915_query_perf_config_data(struct gen_perf_config *perf,
                            int fd, const char *guid,
                            struct drm_i915_perf_oa_config *config)
{
   struct {
      struct drm_i915_query_perf_config query;
      struct drm_i915_perf_oa_config config;
   } item_data;
   struct drm_i915_query_item item = {
      .query_id = DRM_I915_QUERY_PERF_CONFIG,
      .flags = DRM_I915_QUERY_PERF_CONFIG_DATA_FOR_UUID,
      .data_ptr = to_user_pointer(&item_data),
      .length = sizeof(item_data),
   };

   memset(&item_data, 0, sizeof(item_data));
   memcpy(item_data.query.uuid, guid, sizeof(item_data.query.uuid));
   memcpy(&item_data.config, config, sizeof(item_data.config));

   if (!(i915_query_items(perf, fd, &item, 1) == 0 && item.length > 0))
      return false;

   memcpy(config, &item_data.config, sizeof(item_data.config));

   return true;
}

bool
gen_perf_load_metric_id(struct gen_perf_config *perf_cfg,
                        const char *guid,
                        uint64_t *metric_id)
{
   char config_path[280];

   snprintf(config_path, sizeof(config_path), "%s/metrics/%s/id",
            perf_cfg->sysfs_dev_dir, guid);

   /* Don't recreate already loaded configs. */
   return read_file_uint64(config_path, metric_id);
}

static uint64_t
i915_add_config(struct gen_perf_config *perf, int fd,
                const struct gen_perf_registers *config,
                const char *guid)
{
   struct drm_i915_perf_oa_config i915_config = { 0, };

   memcpy(i915_config.uuid, guid, sizeof(i915_config.uuid));

   i915_config.n_mux_regs = config->n_mux_regs;
   i915_config.mux_regs_ptr = to_user_pointer(config->mux_regs);

   i915_config.n_boolean_regs = config->n_b_counter_regs;
   i915_config.boolean_regs_ptr = to_user_pointer(config->b_counter_regs);

   i915_config.n_flex_regs = config->n_flex_regs;
   i915_config.flex_regs_ptr = to_user_pointer(config->flex_regs);

   int ret = gen_ioctl(fd, DRM_IOCTL_I915_PERF_ADD_CONFIG, &i915_config);
   return ret > 0 ? ret : 0;
}

static void
init_oa_configs(struct gen_perf_config *perf, int fd)
{
   hash_table_foreach(perf->oa_metrics_table, entry) {
      const struct gen_perf_query_info *query = entry->data;
      uint64_t config_id;

      if (gen_perf_load_metric_id(perf, query->guid, &config_id)) {
         DBG("metric set: %s (already loaded)\n", query->guid);
         register_oa_config(perf, query, config_id);
         continue;
      }

      int ret = i915_add_config(perf, fd, &query->config, query->guid);
      if (ret < 0) {
         DBG("Failed to load \"%s\" (%s) metrics set in kernel: %s\n",
             query->name, query->guid, strerror(errno));
         continue;
      }

      register_oa_config(perf, query, ret);
      DBG("metric set: %s (added)\n", query->guid);
   }
}

static void
compute_topology_builtins(struct gen_perf_config *perf,
                          const struct gen_device_info *devinfo)
{
   perf->sys_vars.slice_mask = devinfo->slice_masks;
   perf->sys_vars.n_eu_slices = devinfo->num_slices;

   for (int i = 0; i < sizeof(devinfo->subslice_masks[i]); i++) {
      perf->sys_vars.n_eu_sub_slices +=
         __builtin_popcount(devinfo->subslice_masks[i]);
   }

   for (int i = 0; i < sizeof(devinfo->eu_masks); i++)
      perf->sys_vars.n_eus += __builtin_popcount(devinfo->eu_masks[i]);

   perf->sys_vars.eu_threads_count = devinfo->num_thread_per_eu;

   /* The subslice mask builtin contains bits for all slices. Prior to Gen11
    * it had groups of 3bits for each slice, on Gen11 it's 8bits for each
    * slice.
    *
    * Ideally equations would be updated to have a slice/subslice query
    * function/operator.
    */
   perf->sys_vars.subslice_mask = 0;

   int bits_per_subslice = devinfo->gen == 11 ? 8 : 3;

   for (int s = 0; s < util_last_bit(devinfo->slice_masks); s++) {
      for (int ss = 0; ss < (devinfo->subslice_slice_stride * 8); ss++) {
         if (gen_device_info_subslice_available(devinfo, s, ss))
            perf->sys_vars.subslice_mask |= 1ULL << (s * bits_per_subslice + ss);
      }
   }
}

static bool
init_oa_sys_vars(struct gen_perf_config *perf, const struct gen_device_info *devinfo)
{
   uint64_t min_freq_mhz = 0, max_freq_mhz = 0;

   if (!read_sysfs_drm_device_file_uint64(perf, "gt_min_freq_mhz", &min_freq_mhz))
      return false;

   if (!read_sysfs_drm_device_file_uint64(perf,  "gt_max_freq_mhz", &max_freq_mhz))
      return false;

   memset(&perf->sys_vars, 0, sizeof(perf->sys_vars));
   perf->sys_vars.gt_min_freq = min_freq_mhz * 1000000;
   perf->sys_vars.gt_max_freq = max_freq_mhz * 1000000;
   perf->sys_vars.timestamp_frequency = devinfo->timestamp_frequency;
   perf->sys_vars.revision = devinfo->revision;
   compute_topology_builtins(perf, devinfo);

   return true;
}

typedef void (*perf_register_oa_queries_t)(struct gen_perf_config *);

static perf_register_oa_queries_t
get_register_queries_function(const struct gen_device_info *devinfo)
{
   if (devinfo->is_haswell)
      return gen_oa_register_queries_hsw;
   if (devinfo->is_cherryview)
      return gen_oa_register_queries_chv;
   if (devinfo->is_broadwell)
      return gen_oa_register_queries_bdw;
   if (devinfo->is_broxton)
      return gen_oa_register_queries_bxt;
   if (devinfo->is_skylake) {
      if (devinfo->gt == 2)
         return gen_oa_register_queries_sklgt2;
      if (devinfo->gt == 3)
         return gen_oa_register_queries_sklgt3;
      if (devinfo->gt == 4)
         return gen_oa_register_queries_sklgt4;
   }
   if (devinfo->is_kabylake) {
      if (devinfo->gt == 2)
         return gen_oa_register_queries_kblgt2;
      if (devinfo->gt == 3)
         return gen_oa_register_queries_kblgt3;
   }
   if (devinfo->is_geminilake)
      return gen_oa_register_queries_glk;
   if (devinfo->is_coffeelake) {
      if (devinfo->gt == 2)
         return gen_oa_register_queries_cflgt2;
      if (devinfo->gt == 3)
         return gen_oa_register_queries_cflgt3;
   }
   if (devinfo->is_cannonlake)
      return gen_oa_register_queries_cnl;
   if (devinfo->gen == 11)
      return gen_oa_register_queries_icl;

   return NULL;
}

static inline void
add_stat_reg(struct gen_perf_query_info *query, uint32_t reg,
             uint32_t numerator, uint32_t denominator,
             const char *name, const char *description)
{
   struct gen_perf_query_counter *counter;

   assert(query->n_counters < query->max_counters);

   counter = &query->counters[query->n_counters];
   counter->name = name;
   counter->desc = description;
   counter->type = GEN_PERF_COUNTER_TYPE_RAW;
   counter->data_type = GEN_PERF_COUNTER_DATA_TYPE_UINT64;
   counter->offset = sizeof(uint64_t) * query->n_counters;
   counter->pipeline_stat.reg = reg;
   counter->pipeline_stat.numerator = numerator;
   counter->pipeline_stat.denominator = denominator;

   query->n_counters++;
}

static inline void
add_basic_stat_reg(struct gen_perf_query_info *query,
                                       uint32_t reg, const char *name)
{
   add_stat_reg(query, reg, 1, 1, name, name);
}

static void
load_pipeline_statistic_metrics(struct gen_perf_config *perf_cfg,
                                         const struct gen_device_info *devinfo)
{
   struct gen_perf_query_info *query =
      append_query_info(perf_cfg, MAX_STAT_COUNTERS);

   query->kind = GEN_PERF_QUERY_TYPE_PIPELINE;
   query->name = "Pipeline Statistics Registers";

   add_basic_stat_reg(query, IA_VERTICES_COUNT,
                                          "N vertices submitted");
   add_basic_stat_reg(query, IA_PRIMITIVES_COUNT,
                                          "N primitives submitted");
   add_basic_stat_reg(query, VS_INVOCATION_COUNT,
                                          "N vertex shader invocations");

   if (devinfo->gen == 6) {
      add_stat_reg(query, GEN6_SO_PRIM_STORAGE_NEEDED, 1, 1,
                   "SO_PRIM_STORAGE_NEEDED",
                   "N geometry shader stream-out primitives (total)");
      add_stat_reg(query, GEN6_SO_NUM_PRIMS_WRITTEN, 1, 1,
                   "SO_NUM_PRIMS_WRITTEN",
                   "N geometry shader stream-out primitives (written)");
   } else {
      add_stat_reg(query, GEN7_SO_PRIM_STORAGE_NEEDED(0), 1, 1,
                   "SO_PRIM_STORAGE_NEEDED (Stream 0)",
                   "N stream-out (stream 0) primitives (total)");
      add_stat_reg(query, GEN7_SO_PRIM_STORAGE_NEEDED(1), 1, 1,
                   "SO_PRIM_STORAGE_NEEDED (Stream 1)",
                   "N stream-out (stream 1) primitives (total)");
      add_stat_reg(query, GEN7_SO_PRIM_STORAGE_NEEDED(2), 1, 1,
                   "SO_PRIM_STORAGE_NEEDED (Stream 2)",
                   "N stream-out (stream 2) primitives (total)");
      add_stat_reg(query, GEN7_SO_PRIM_STORAGE_NEEDED(3), 1, 1,
                   "SO_PRIM_STORAGE_NEEDED (Stream 3)",
                   "N stream-out (stream 3) primitives (total)");
      add_stat_reg(query, GEN7_SO_NUM_PRIMS_WRITTEN(0), 1, 1,
                   "SO_NUM_PRIMS_WRITTEN (Stream 0)",
                   "N stream-out (stream 0) primitives (written)");
      add_stat_reg(query, GEN7_SO_NUM_PRIMS_WRITTEN(1), 1, 1,
                   "SO_NUM_PRIMS_WRITTEN (Stream 1)",
                   "N stream-out (stream 1) primitives (written)");
      add_stat_reg(query, GEN7_SO_NUM_PRIMS_WRITTEN(2), 1, 1,
                   "SO_NUM_PRIMS_WRITTEN (Stream 2)",
                   "N stream-out (stream 2) primitives (written)");
      add_stat_reg(query, GEN7_SO_NUM_PRIMS_WRITTEN(3), 1, 1,
                   "SO_NUM_PRIMS_WRITTEN (Stream 3)",
                   "N stream-out (stream 3) primitives (written)");
   }

   add_basic_stat_reg(query, HS_INVOCATION_COUNT,
                                          "N TCS shader invocations");
   add_basic_stat_reg(query, DS_INVOCATION_COUNT,
                                          "N TES shader invocations");

   add_basic_stat_reg(query, GS_INVOCATION_COUNT,
                                          "N geometry shader invocations");
   add_basic_stat_reg(query, GS_PRIMITIVES_COUNT,
                                          "N geometry shader primitives emitted");

   add_basic_stat_reg(query, CL_INVOCATION_COUNT,
                                          "N primitives entering clipping");
   add_basic_stat_reg(query, CL_PRIMITIVES_COUNT,
                                          "N primitives leaving clipping");

   if (devinfo->is_haswell || devinfo->gen == 8) {
      add_stat_reg(query, PS_INVOCATION_COUNT, 1, 4,
                   "N fragment shader invocations",
                   "N fragment shader invocations");
   } else {
      add_basic_stat_reg(query, PS_INVOCATION_COUNT,
                                             "N fragment shader invocations");
   }

   add_basic_stat_reg(query, PS_DEPTH_COUNT,
                                          "N z-pass fragments");

   if (devinfo->gen >= 7) {
      add_basic_stat_reg(query, CS_INVOCATION_COUNT,
                                             "N compute shader invocations");
   }

   query->data_size = sizeof(uint64_t) * query->n_counters;
}

static bool
load_oa_metrics(struct gen_perf_config *perf, int fd,
                         const struct gen_device_info *devinfo)
{
   perf_register_oa_queries_t oa_register = get_register_queries_function(devinfo);
   bool i915_perf_oa_available = false;
   struct stat sb;

   perf->i915_query_supported = i915_query_perf_config_supported(perf, fd);

   /* The existence of this sysctl parameter implies the kernel supports
    * the i915 perf interface.
    */
   if (stat("/proc/sys/dev/i915/perf_stream_paranoid", &sb) == 0) {

      /* If _paranoid == 1 then on Gen8+ we won't be able to access OA
       * metrics unless running as root.
       */
      if (devinfo->is_haswell)
         i915_perf_oa_available = true;
      else {
         uint64_t paranoid = 1;

         read_file_uint64("/proc/sys/dev/i915/perf_stream_paranoid", &paranoid);

         if (paranoid == 0 || geteuid() == 0)
            i915_perf_oa_available = true;
      }
   }

   if (!i915_perf_oa_available ||
       !oa_register ||
       !get_sysfs_dev_dir(perf, fd) ||
       !init_oa_sys_vars(perf, devinfo))
      return false;

   perf->oa_metrics_table =
      _mesa_hash_table_create(perf, _mesa_key_hash_string,
                              _mesa_key_string_equal);

   /* Index all the metric sets mesa knows about before looking to see what
    * the kernel is advertising.
    */
   oa_register(perf);

   if (likely((INTEL_DEBUG & DEBUG_NO_OACONFIG) == 0) &&
       kernel_has_dynamic_config_support(perf, fd))
      init_oa_configs(perf, fd);
   else
      enumerate_sysfs_metrics(perf);

   return true;
}

struct gen_perf_registers *
gen_perf_load_configuration(struct gen_perf_config *perf_cfg, int fd, const char *guid)
{
   if (!perf_cfg->i915_query_supported)
      return NULL;

   struct drm_i915_perf_oa_config i915_config = { 0, };
   if (!i915_query_perf_config_data(perf_cfg, fd, guid, &i915_config))
      return NULL;

   struct gen_perf_registers *config = rzalloc(NULL, struct gen_perf_registers);
   config->n_flex_regs = i915_config.n_flex_regs;
   config->flex_regs = rzalloc_array(config, struct gen_perf_query_register_prog, config->n_flex_regs);
   config->n_mux_regs = i915_config.n_mux_regs;
   config->mux_regs = rzalloc_array(config, struct gen_perf_query_register_prog, config->n_mux_regs);
   config->n_b_counter_regs = i915_config.n_boolean_regs;
   config->b_counter_regs = rzalloc_array(config, struct gen_perf_query_register_prog, config->n_b_counter_regs);

   /*
    * struct gen_perf_query_register_prog maps exactly to the tuple of
    * (register offset, register value) returned by the i915.
    */
   i915_config.flex_regs_ptr = to_user_pointer(config->flex_regs);
   i915_config.mux_regs_ptr = to_user_pointer(config->mux_regs);
   i915_config.boolean_regs_ptr = to_user_pointer(config->b_counter_regs);
   if (!i915_query_perf_config_data(perf_cfg, fd, guid, &i915_config)) {
      ralloc_free(config);
      return NULL;
   }

   return config;
}

uint64_t
gen_perf_store_configuration(struct gen_perf_config *perf_cfg, int fd,
                             const struct gen_perf_registers *config,
                             const char *guid)
{
   if (guid)
      return i915_add_config(perf_cfg, fd, config, guid);

   struct mesa_sha1 sha1_ctx;
   _mesa_sha1_init(&sha1_ctx);

   if (config->flex_regs) {
      _mesa_sha1_update(&sha1_ctx, config->flex_regs,
                        sizeof(config->flex_regs[0]) *
                        config->n_flex_regs);
   }
   if (config->mux_regs) {
      _mesa_sha1_update(&sha1_ctx, config->mux_regs,
                        sizeof(config->mux_regs[0]) *
                        config->n_mux_regs);
   }
   if (config->b_counter_regs) {
      _mesa_sha1_update(&sha1_ctx, config->b_counter_regs,
                        sizeof(config->b_counter_regs[0]) *
                        config->n_b_counter_regs);
   }

   uint8_t hash[20];
   _mesa_sha1_final(&sha1_ctx, hash);

   char formatted_hash[41];
   _mesa_sha1_format(formatted_hash, hash);

   char generated_guid[37];
   snprintf(generated_guid, sizeof(generated_guid),
            "%.8s-%.4s-%.4s-%.4s-%.12s",
            &formatted_hash[0], &formatted_hash[8],
            &formatted_hash[8 + 4], &formatted_hash[8 + 4 + 4],
            &formatted_hash[8 + 4 + 4 + 4]);

   /* Check if already present. */
   uint64_t id;
   if (gen_perf_load_metric_id(perf_cfg, generated_guid, &id))
      return id;

   return i915_add_config(perf_cfg, fd, config, generated_guid);
}

/* Accumulate 32bits OA counters */
static inline void
accumulate_uint32(const uint32_t *report0,
                  const uint32_t *report1,
                  uint64_t *accumulator)
{
   *accumulator += (uint32_t)(*report1 - *report0);
}

/* Accumulate 40bits OA counters */
static inline void
accumulate_uint40(int a_index,
                  const uint32_t *report0,
                  const uint32_t *report1,
                  uint64_t *accumulator)
{
   const uint8_t *high_bytes0 = (uint8_t *)(report0 + 40);
   const uint8_t *high_bytes1 = (uint8_t *)(report1 + 40);
   uint64_t high0 = (uint64_t)(high_bytes0[a_index]) << 32;
   uint64_t high1 = (uint64_t)(high_bytes1[a_index]) << 32;
   uint64_t value0 = report0[a_index + 4] | high0;
   uint64_t value1 = report1[a_index + 4] | high1;
   uint64_t delta;

   if (value0 > value1)
      delta = (1ULL << 40) + value1 - value0;
   else
      delta = value1 - value0;

   *accumulator += delta;
}

static void
gen8_read_report_clock_ratios(const uint32_t *report,
                              uint64_t *slice_freq_hz,
                              uint64_t *unslice_freq_hz)
{
   /* The lower 16bits of the RPT_ID field of the OA reports contains a
    * snapshot of the bits coming from the RP_FREQ_NORMAL register and is
    * divided this way :
    *
    * RPT_ID[31:25]: RP_FREQ_NORMAL[20:14] (low squashed_slice_clock_frequency)
    * RPT_ID[10:9]:  RP_FREQ_NORMAL[22:21] (high squashed_slice_clock_frequency)
    * RPT_ID[8:0]:   RP_FREQ_NORMAL[31:23] (squashed_unslice_clock_frequency)
    *
    * RP_FREQ_NORMAL[31:23]: Software Unslice Ratio Request
    *                        Multiple of 33.33MHz 2xclk (16 MHz 1xclk)
    *
    * RP_FREQ_NORMAL[22:14]: Software Slice Ratio Request
    *                        Multiple of 33.33MHz 2xclk (16 MHz 1xclk)
    */

   uint32_t unslice_freq = report[0] & 0x1ff;
   uint32_t slice_freq_low = (report[0] >> 25) & 0x7f;
   uint32_t slice_freq_high = (report[0] >> 9) & 0x3;
   uint32_t slice_freq = slice_freq_low | (slice_freq_high << 7);

   *slice_freq_hz = slice_freq * 16666667ULL;
   *unslice_freq_hz = unslice_freq * 16666667ULL;
}

void
gen_perf_query_result_read_frequencies(struct gen_perf_query_result *result,
                                       const struct gen_device_info *devinfo,
                                       const uint32_t *start,
                                       const uint32_t *end)
{
   /* Slice/Unslice frequency is only available in the OA reports when the
    * "Disable OA reports due to clock ratio change" field in
    * OA_DEBUG_REGISTER is set to 1. This is how the kernel programs this
    * global register (see drivers/gpu/drm/i915/i915_perf.c)
    *
    * Documentation says this should be available on Gen9+ but experimentation
    * shows that Gen8 reports similar values, so we enable it there too.
    */
   if (devinfo->gen < 8)
      return;

   gen8_read_report_clock_ratios(start,
                                 &result->slice_frequency[0],
                                 &result->unslice_frequency[0]);
   gen8_read_report_clock_ratios(end,
                                 &result->slice_frequency[1],
                                 &result->unslice_frequency[1]);
}

void
gen_perf_query_result_accumulate(struct gen_perf_query_result *result,
                                 const struct gen_perf_query_info *query,
                                 const uint32_t *start,
                                 const uint32_t *end)
{
   int i, idx = 0;

   if (result->hw_id == OA_REPORT_INVALID_CTX_ID &&
       start[2] != OA_REPORT_INVALID_CTX_ID)
      result->hw_id = start[2];
   if (result->reports_accumulated == 0)
      result->begin_timestamp = start[1];
   result->reports_accumulated++;

   switch (query->oa_format) {
   case I915_OA_FORMAT_A32u40_A4u32_B8_C8:
      accumulate_uint32(start + 1, end + 1, result->accumulator + idx++); /* timestamp */
      accumulate_uint32(start + 3, end + 3, result->accumulator + idx++); /* clock */

      /* 32x 40bit A counters... */
      for (i = 0; i < 32; i++)
         accumulate_uint40(i, start, end, result->accumulator + idx++);

      /* 4x 32bit A counters... */
      for (i = 0; i < 4; i++)
         accumulate_uint32(start + 36 + i, end + 36 + i, result->accumulator + idx++);

      /* 8x 32bit B counters + 8x 32bit C counters... */
      for (i = 0; i < 16; i++)
         accumulate_uint32(start + 48 + i, end + 48 + i, result->accumulator + idx++);
      break;

   case I915_OA_FORMAT_A45_B8_C8:
      accumulate_uint32(start + 1, end + 1, result->accumulator); /* timestamp */

      for (i = 0; i < 61; i++)
         accumulate_uint32(start + 3 + i, end + 3 + i, result->accumulator + 1 + i);
      break;

   default:
      unreachable("Can't accumulate OA counters in unknown format");
   }

}

void
gen_perf_query_result_clear(struct gen_perf_query_result *result)
{
   memset(result, 0, sizeof(*result));
   result->hw_id = OA_REPORT_INVALID_CTX_ID; /* invalid */
}

static void
register_mdapi_statistic_query(struct gen_perf_config *perf_cfg,
                               const struct gen_device_info *devinfo)
{
   if (!(devinfo->gen >= 7 && devinfo->gen <= 11))
      return;

   struct gen_perf_query_info *query =
      append_query_info(perf_cfg, MAX_STAT_COUNTERS);

   query->kind = GEN_PERF_QUERY_TYPE_PIPELINE;
   query->name = "Intel_Raw_Pipeline_Statistics_Query";

   /* The order has to match mdapi_pipeline_metrics. */
   add_basic_stat_reg(query, IA_VERTICES_COUNT,
                      "N vertices submitted");
   add_basic_stat_reg(query, IA_PRIMITIVES_COUNT,
                      "N primitives submitted");
   add_basic_stat_reg(query, VS_INVOCATION_COUNT,
                      "N vertex shader invocations");
   add_basic_stat_reg(query, GS_INVOCATION_COUNT,
                      "N geometry shader invocations");
   add_basic_stat_reg(query, GS_PRIMITIVES_COUNT,
                      "N geometry shader primitives emitted");
   add_basic_stat_reg(query, CL_INVOCATION_COUNT,
                      "N primitives entering clipping");
   add_basic_stat_reg(query, CL_PRIMITIVES_COUNT,
                      "N primitives leaving clipping");
   if (devinfo->is_haswell || devinfo->gen == 8) {
      add_stat_reg(query, PS_INVOCATION_COUNT, 1, 4,
                   "N fragment shader invocations",
                   "N fragment shader invocations");
   } else {
      add_basic_stat_reg(query, PS_INVOCATION_COUNT,
                         "N fragment shader invocations");
   }
   add_basic_stat_reg(query, HS_INVOCATION_COUNT,
                      "N TCS shader invocations");
   add_basic_stat_reg(query, DS_INVOCATION_COUNT,
                      "N TES shader invocations");
   if (devinfo->gen >= 7) {
      add_basic_stat_reg(query, CS_INVOCATION_COUNT,
                         "N compute shader invocations");
   }

   if (devinfo->gen >= 10) {
      /* Reuse existing CS invocation register until we can expose this new
       * one.
       */
      add_basic_stat_reg(query, CS_INVOCATION_COUNT,
                         "Reserved1");
   }

   query->data_size = sizeof(uint64_t) * query->n_counters;
}

static void
fill_mdapi_perf_query_counter(struct gen_perf_query_info *query,
                              const char *name,
                              uint32_t data_offset,
                              uint32_t data_size,
                              enum gen_perf_counter_data_type data_type)
{
   struct gen_perf_query_counter *counter = &query->counters[query->n_counters];

   assert(query->n_counters <= query->max_counters);

   counter->name = name;
   counter->desc = "Raw counter value";
   counter->type = GEN_PERF_COUNTER_TYPE_RAW;
   counter->data_type = data_type;
   counter->offset = data_offset;

   query->n_counters++;

   assert(counter->offset + gen_perf_query_counter_get_size(counter) <= query->data_size);
}

#define MDAPI_QUERY_ADD_COUNTER(query, struct_name, field_name, type_name) \
   fill_mdapi_perf_query_counter(query, #field_name,                    \
                                 (uint8_t *) &struct_name.field_name -  \
                                 (uint8_t *) &struct_name,              \
                                 sizeof(struct_name.field_name),        \
                                 GEN_PERF_COUNTER_DATA_TYPE_##type_name)
#define MDAPI_QUERY_ADD_ARRAY_COUNTER(ctx, query, struct_name, field_name, idx, type_name) \
   fill_mdapi_perf_query_counter(query,                                 \
                                 ralloc_asprintf(ctx, "%s%i", #field_name, idx), \
                                 (uint8_t *) &struct_name.field_name[idx] - \
                                 (uint8_t *) &struct_name,              \
                                 sizeof(struct_name.field_name[0]),     \
                                 GEN_PERF_COUNTER_DATA_TYPE_##type_name)

static void
register_mdapi_oa_query(const struct gen_device_info *devinfo,
                        struct gen_perf_config *perf)
{
   struct gen_perf_query_info *query = NULL;

   /* MDAPI requires different structures for pretty much every generation
    * (right now we have definitions for gen 7 to 11).
    */
   if (!(devinfo->gen >= 7 && devinfo->gen <= 11))
      return;

   switch (devinfo->gen) {
   case 7: {
      query = append_query_info(perf, 1 + 45 + 16 + 7);
      query->oa_format = I915_OA_FORMAT_A45_B8_C8;

      struct gen7_mdapi_metrics metric_data;
      query->data_size = sizeof(metric_data);

      MDAPI_QUERY_ADD_COUNTER(query, metric_data, TotalTime, UINT64);
      for (int i = 0; i < ARRAY_SIZE(metric_data.ACounters); i++) {
         MDAPI_QUERY_ADD_ARRAY_COUNTER(perf->queries, query,
                                       metric_data, ACounters, i, UINT64);
      }
      for (int i = 0; i < ARRAY_SIZE(metric_data.NOACounters); i++) {
         MDAPI_QUERY_ADD_ARRAY_COUNTER(perf->queries, query,
                                       metric_data, NOACounters, i, UINT64);
      }
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, PerfCounter1, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, PerfCounter2, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, SplitOccured, BOOL32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, CoreFrequencyChanged, BOOL32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, CoreFrequency, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, ReportId, UINT32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, ReportsCount, UINT32);
      break;
   }
   case 8: {
      query = append_query_info(perf, 2 + 36 + 16 + 16);
      query->oa_format = I915_OA_FORMAT_A32u40_A4u32_B8_C8;

      struct gen8_mdapi_metrics metric_data;
      query->data_size = sizeof(metric_data);

      MDAPI_QUERY_ADD_COUNTER(query, metric_data, TotalTime, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, GPUTicks, UINT64);
      for (int i = 0; i < ARRAY_SIZE(metric_data.OaCntr); i++) {
         MDAPI_QUERY_ADD_ARRAY_COUNTER(perf->queries, query,
                                       metric_data, OaCntr, i, UINT64);
      }
      for (int i = 0; i < ARRAY_SIZE(metric_data.NoaCntr); i++) {
         MDAPI_QUERY_ADD_ARRAY_COUNTER(perf->queries, query,
                                       metric_data, NoaCntr, i, UINT64);
      }
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, BeginTimestamp, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, Reserved1, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, Reserved2, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, Reserved3, UINT32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, OverrunOccured, BOOL32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, MarkerUser, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, MarkerDriver, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, SliceFrequency, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, UnsliceFrequency, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, PerfCounter1, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, PerfCounter2, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, SplitOccured, BOOL32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, CoreFrequencyChanged, BOOL32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, CoreFrequency, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, ReportId, UINT32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, ReportsCount, UINT32);
      break;
   }
   case 9:
   case 10:
   case 11: {
      query = append_query_info(perf, 2 + 36 + 16 + 16 + 16 + 2);
      query->oa_format = I915_OA_FORMAT_A32u40_A4u32_B8_C8;

      struct gen9_mdapi_metrics metric_data;
      query->data_size = sizeof(metric_data);

      MDAPI_QUERY_ADD_COUNTER(query, metric_data, TotalTime, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, GPUTicks, UINT64);
      for (int i = 0; i < ARRAY_SIZE(metric_data.OaCntr); i++) {
         MDAPI_QUERY_ADD_ARRAY_COUNTER(perf->queries, query,
                                       metric_data, OaCntr, i, UINT64);
      }
      for (int i = 0; i < ARRAY_SIZE(metric_data.NoaCntr); i++) {
         MDAPI_QUERY_ADD_ARRAY_COUNTER(perf->queries, query,
                                       metric_data, NoaCntr, i, UINT64);
      }
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, BeginTimestamp, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, Reserved1, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, Reserved2, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, Reserved3, UINT32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, OverrunOccured, BOOL32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, MarkerUser, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, MarkerDriver, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, SliceFrequency, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, UnsliceFrequency, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, PerfCounter1, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, PerfCounter2, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, SplitOccured, BOOL32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, CoreFrequencyChanged, BOOL32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, CoreFrequency, UINT64);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, ReportId, UINT32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, ReportsCount, UINT32);
      for (int i = 0; i < ARRAY_SIZE(metric_data.UserCntr); i++) {
         MDAPI_QUERY_ADD_ARRAY_COUNTER(perf->queries, query,
                                       metric_data, UserCntr, i, UINT64);
      }
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, UserCntrCfgId, UINT32);
      MDAPI_QUERY_ADD_COUNTER(query, metric_data, Reserved4, UINT32);
      break;
   }
   default:
      unreachable("Unsupported gen");
      break;
   }

   query->kind = GEN_PERF_QUERY_TYPE_RAW;
   query->name = "Intel_Raw_Hardware_Counters_Set_0_Query";
   query->guid = GEN_PERF_QUERY_GUID_MDAPI;

   {
      /* Accumulation buffer offsets copied from an actual query... */
      const struct gen_perf_query_info *copy_query =
         &perf->queries[0];

      query->gpu_time_offset = copy_query->gpu_time_offset;
      query->gpu_clock_offset = copy_query->gpu_clock_offset;
      query->a_offset = copy_query->a_offset;
      query->b_offset = copy_query->b_offset;
      query->c_offset = copy_query->c_offset;
   }
}

static uint64_t
get_metric_id(struct gen_perf_config *perf,
              const struct gen_perf_query_info *query)
{
   /* These queries are know not to ever change, their config ID has been
    * loaded upon the first query creation. No need to look them up again.
    */
   if (query->kind == GEN_PERF_QUERY_TYPE_OA)
      return query->oa_metrics_set_id;

   assert(query->kind == GEN_PERF_QUERY_TYPE_RAW);

   /* Raw queries can be reprogrammed up by an external application/library.
    * When a raw query is used for the first time it's id is set to a value !=
    * 0. When it stops being used the id returns to 0. No need to reload the
    * ID when it's already loaded.
    */
   if (query->oa_metrics_set_id != 0) {
      DBG("Raw query '%s' guid=%s using cached ID: %"PRIu64"\n",
          query->name, query->guid, query->oa_metrics_set_id);
      return query->oa_metrics_set_id;
   }

   struct gen_perf_query_info *raw_query = (struct gen_perf_query_info *)query;
   if (!gen_perf_load_metric_id(perf, query->guid,
                                &raw_query->oa_metrics_set_id)) {
      DBG("Unable to read query guid=%s ID, falling back to test config\n", query->guid);
      raw_query->oa_metrics_set_id = 1ULL;
   } else {
      DBG("Raw query '%s'guid=%s loaded ID: %"PRIu64"\n",
          query->name, query->guid, query->oa_metrics_set_id);
   }
   return query->oa_metrics_set_id;
}

static struct oa_sample_buf *
get_free_sample_buf(struct gen_perf_context *perf_ctx)
{
   struct exec_node *node = exec_list_pop_head(&perf_ctx->free_sample_buffers);
   struct oa_sample_buf *buf;

   if (node)
      buf = exec_node_data(struct oa_sample_buf, node, link);
   else {
      buf = ralloc_size(perf_ctx->perf, sizeof(*buf));

      exec_node_init(&buf->link);
      buf->refcount = 0;
   }
   buf->len = 0;

   return buf;
}

static void
reap_old_sample_buffers(struct gen_perf_context *perf_ctx)
{
   struct exec_node *tail_node =
      exec_list_get_tail(&perf_ctx->sample_buffers);
   struct oa_sample_buf *tail_buf =
      exec_node_data(struct oa_sample_buf, tail_node, link);

   /* Remove all old, unreferenced sample buffers walking forward from
    * the head of the list, except always leave at least one node in
    * the list so we always have a node to reference when we Begin
    * a new query.
    */
   foreach_list_typed_safe(struct oa_sample_buf, buf, link,
                           &perf_ctx->sample_buffers)
   {
      if (buf->refcount == 0 && buf != tail_buf) {
         exec_node_remove(&buf->link);
         exec_list_push_head(&perf_ctx->free_sample_buffers, &buf->link);
      } else
         return;
   }
}

static void
free_sample_bufs(struct gen_perf_context *perf_ctx)
{
   foreach_list_typed_safe(struct oa_sample_buf, buf, link,
                           &perf_ctx->free_sample_buffers)
      ralloc_free(buf);

   exec_list_make_empty(&perf_ctx->free_sample_buffers);
}

/******************************************************************************/

/**
 * Emit MI_STORE_REGISTER_MEM commands to capture all of the
 * pipeline statistics for the performance query object.
 */
static void
snapshot_statistics_registers(void *context,
                              struct gen_perf_config *perf,
                              struct gen_perf_query_object *obj,
                              uint32_t offset_in_bytes)
{
   const struct gen_perf_query_info *query = obj->queryinfo;
   const int n_counters = query->n_counters;

   for (int i = 0; i < n_counters; i++) {
      const struct gen_perf_query_counter *counter = &query->counters[i];

      assert(counter->data_type == GEN_PERF_COUNTER_DATA_TYPE_UINT64);

      perf->vtbl.store_register_mem64(context, obj->pipeline_stats.bo,
                                      counter->pipeline_stat.reg,
                                      offset_in_bytes + i * sizeof(uint64_t));
   }
}

static void
gen_perf_close(struct gen_perf_context *perfquery,
               const struct gen_perf_query_info *query)
{
   if (perfquery->oa_stream_fd != -1) {
      close(perfquery->oa_stream_fd);
      perfquery->oa_stream_fd = -1;
   }
   if (query->kind == GEN_PERF_QUERY_TYPE_RAW) {
      struct gen_perf_query_info *raw_query =
         (struct gen_perf_query_info *) query;
      raw_query->oa_metrics_set_id = 0;
   }
}

static bool
gen_perf_open(struct gen_perf_context *perf_ctx,
              int metrics_set_id,
              int report_format,
              int period_exponent,
              int drm_fd,
              uint32_t ctx_id)
{
   uint64_t properties[] = {
      /* Single context sampling */
      DRM_I915_PERF_PROP_CTX_HANDLE, ctx_id,

      /* Include OA reports in samples */
      DRM_I915_PERF_PROP_SAMPLE_OA, true,

      /* OA unit configuration */
      DRM_I915_PERF_PROP_OA_METRICS_SET, metrics_set_id,
      DRM_I915_PERF_PROP_OA_FORMAT, report_format,
      DRM_I915_PERF_PROP_OA_EXPONENT, period_exponent,
   };
   struct drm_i915_perf_open_param param = {
      .flags = I915_PERF_FLAG_FD_CLOEXEC |
               I915_PERF_FLAG_FD_NONBLOCK |
               I915_PERF_FLAG_DISABLED,
      .num_properties = ARRAY_SIZE(properties) / 2,
      .properties_ptr = (uintptr_t) properties,
   };
   int fd = gen_ioctl(drm_fd, DRM_IOCTL_I915_PERF_OPEN, &param);
   if (fd == -1) {
      DBG("Error opening gen perf OA stream: %m\n");
      return false;
   }

   perf_ctx->oa_stream_fd = fd;

   perf_ctx->current_oa_metrics_set_id = metrics_set_id;
   perf_ctx->current_oa_format = report_format;

   return true;
}

static bool
inc_n_users(struct gen_perf_context *perf_ctx)
{
   if (perf_ctx->n_oa_users == 0 &&
       gen_ioctl(perf_ctx->oa_stream_fd, I915_PERF_IOCTL_ENABLE, 0) < 0)
   {
      return false;
   }
   ++perf_ctx->n_oa_users;

   return true;
}

static void
dec_n_users(struct gen_perf_context *perf_ctx)
{
   /* Disabling the i915 perf stream will effectively disable the OA
    * counters.  Note it's important to be sure there are no outstanding
    * MI_RPC commands at this point since they could stall the CS
    * indefinitely once OACONTROL is disabled.
    */
   --perf_ctx->n_oa_users;
   if (perf_ctx->n_oa_users == 0 &&
       gen_ioctl(perf_ctx->oa_stream_fd, I915_PERF_IOCTL_DISABLE, 0) < 0)
   {
      DBG("WARNING: Error disabling gen perf stream: %m\n");
   }
}

void
gen_perf_init_metrics(struct gen_perf_config *perf_cfg,
                      const struct gen_device_info *devinfo,
                      int drm_fd)
{
   load_pipeline_statistic_metrics(perf_cfg, devinfo);
   register_mdapi_statistic_query(perf_cfg, devinfo);
   if (load_oa_metrics(perf_cfg, drm_fd, devinfo))
      register_mdapi_oa_query(devinfo, perf_cfg);
}

void
gen_perf_init_context(struct gen_perf_context *perf_ctx,
                      struct gen_perf_config *perf_cfg,
                      void * ctx,  /* driver context (eg, brw_context) */
                      void * bufmgr,  /* eg brw_bufmgr */
                      const struct gen_device_info *devinfo,
                      uint32_t hw_ctx,
                      int drm_fd)
{
   perf_ctx->perf = perf_cfg;
   perf_ctx->ctx = ctx;
   perf_ctx->bufmgr = bufmgr;
   perf_ctx->drm_fd = drm_fd;
   perf_ctx->hw_ctx = hw_ctx;
   perf_ctx->devinfo = devinfo;

   perf_ctx->unaccumulated =
      ralloc_array(ctx, struct gen_perf_query_object *, 2);
   perf_ctx->unaccumulated_elements = 0;
   perf_ctx->unaccumulated_array_size = 2;

   exec_list_make_empty(&perf_ctx->sample_buffers);
   exec_list_make_empty(&perf_ctx->free_sample_buffers);

   /* It's convenient to guarantee that this linked list of sample
    * buffers is never empty so we add an empty head so when we
    * Begin an OA query we can always take a reference on a buffer
    * in this list.
    */
   struct oa_sample_buf *buf = get_free_sample_buf(perf_ctx);
   exec_list_push_head(&perf_ctx->sample_buffers, &buf->link);

   perf_ctx->oa_stream_fd = -1;
   perf_ctx->next_query_start_report_id = 1000;
}

/**
 * Add a query to the global list of "unaccumulated queries."
 *
 * Queries are tracked here until all the associated OA reports have
 * been accumulated via accumulate_oa_reports() after the end
 * MI_REPORT_PERF_COUNT has landed in query->oa.bo.
 */
static void
add_to_unaccumulated_query_list(struct gen_perf_context *perf_ctx,
                                struct gen_perf_query_object *obj)
{
   if (perf_ctx->unaccumulated_elements >=
       perf_ctx->unaccumulated_array_size)
   {
      perf_ctx->unaccumulated_array_size *= 1.5;
      perf_ctx->unaccumulated =
         reralloc(perf_ctx->ctx, perf_ctx->unaccumulated,
                  struct gen_perf_query_object *,
                  perf_ctx->unaccumulated_array_size);
   }

   perf_ctx->unaccumulated[perf_ctx->unaccumulated_elements++] = obj;
}

bool
gen_perf_begin_query(struct gen_perf_context *perf_ctx,
                     struct gen_perf_query_object *query)
{
   struct gen_perf_config *perf_cfg = perf_ctx->perf;
   const struct gen_perf_query_info *queryinfo = query->queryinfo;

   /* XXX: We have to consider that the command parser unit that parses batch
    * buffer commands and is used to capture begin/end counter snapshots isn't
    * implicitly synchronized with what's currently running across other GPU
    * units (such as the EUs running shaders) that the performance counters are
    * associated with.
    *
    * The intention of performance queries is to measure the work associated
    * with commands between the begin/end delimiters and so for that to be the
    * case we need to explicitly synchronize the parsing of commands to capture
    * Begin/End counter snapshots with what's running across other parts of the
    * GPU.
    *
    * When the command parser reaches a Begin marker it effectively needs to
    * drain everything currently running on the GPU until the hardware is idle
    * before capturing the first snapshot of counters - otherwise the results
    * would also be measuring the effects of earlier commands.
    *
    * When the command parser reaches an End marker it needs to stall until
    * everything currently running on the GPU has finished before capturing the
    * end snapshot - otherwise the results won't be a complete representation
    * of the work.
    *
    * Theoretically there could be opportunities to minimize how much of the
    * GPU pipeline is drained, or that we stall for, when we know what specific
    * units the performance counters being queried relate to but we don't
    * currently attempt to be clever here.
    *
    * Note: with our current simple approach here then for back-to-back queries
    * we will redundantly emit duplicate commands to synchronize the command
    * streamer with the rest of the GPU pipeline, but we assume that in HW the
    * second synchronization is effectively a NOOP.
    *
    * N.B. The final results are based on deltas of counters between (inside)
    * Begin/End markers so even though the total wall clock time of the
    * workload is stretched by larger pipeline bubbles the bubbles themselves
    * are generally invisible to the query results. Whether that's a good or a
    * bad thing depends on the use case. For a lower real-time impact while
    * capturing metrics then periodic sampling may be a better choice than
    * INTEL_performance_query.
    *
    *
    * This is our Begin synchronization point to drain current work on the
    * GPU before we capture our first counter snapshot...
    */
   perf_cfg->vtbl.emit_mi_flush(perf_ctx->ctx);

   switch (queryinfo->kind) {
   case GEN_PERF_QUERY_TYPE_OA:
   case GEN_PERF_QUERY_TYPE_RAW: {

      /* Opening an i915 perf stream implies exclusive access to the OA unit
       * which will generate counter reports for a specific counter set with a
       * specific layout/format so we can't begin any OA based queries that
       * require a different counter set or format unless we get an opportunity
       * to close the stream and open a new one...
       */
      uint64_t metric_id = get_metric_id(perf_ctx->perf, queryinfo);

      if (perf_ctx->oa_stream_fd != -1 &&
          perf_ctx->current_oa_metrics_set_id != metric_id) {

         if (perf_ctx->n_oa_users != 0) {
            DBG("WARNING: Begin failed already using perf config=%i/%"PRIu64"\n",
                perf_ctx->current_oa_metrics_set_id, metric_id);
            return false;
         } else
            gen_perf_close(perf_ctx, queryinfo);
      }

      /* If the OA counters aren't already on, enable them. */
      if (perf_ctx->oa_stream_fd == -1) {
         const struct gen_device_info *devinfo = perf_ctx->devinfo;

         /* The period_exponent gives a sampling period as follows:
          *   sample_period = timestamp_period * 2^(period_exponent + 1)
          *
          * The timestamps increments every 80ns (HSW), ~52ns (GEN9LP) or
          * ~83ns (GEN8/9).
          *
          * The counter overflow period is derived from the EuActive counter
          * which reads a counter that increments by the number of clock
          * cycles multiplied by the number of EUs. It can be calculated as:
          *
          * 2^(number of bits in A counter) / (n_eus * max_gen_freq * 2)
          *
          * (E.g. 40 EUs @ 1GHz = ~53ms)
          *
          * We select a sampling period inferior to that overflow period to
          * ensure we cannot see more than 1 counter overflow, otherwise we
          * could loose information.
          */

         int a_counter_in_bits = 32;
         if (devinfo->gen >= 8)
            a_counter_in_bits = 40;

         uint64_t overflow_period = pow(2, a_counter_in_bits) / (perf_cfg->sys_vars.n_eus *
             /* drop 1GHz freq to have units in nanoseconds */
             2);

         DBG("A counter overflow period: %"PRIu64"ns, %"PRIu64"ms (n_eus=%"PRIu64")\n",
             overflow_period, overflow_period / 1000000ul, perf_cfg->sys_vars.n_eus);

         int period_exponent = 0;
         uint64_t prev_sample_period, next_sample_period;
         for (int e = 0; e < 30; e++) {
            prev_sample_period = 1000000000ull * pow(2, e + 1) / devinfo->timestamp_frequency;
            next_sample_period = 1000000000ull * pow(2, e + 2) / devinfo->timestamp_frequency;

            /* Take the previous sampling period, lower than the overflow
             * period.
             */
            if (prev_sample_period < overflow_period &&
                next_sample_period > overflow_period)
               period_exponent = e + 1;
         }

         if (period_exponent == 0) {
            DBG("WARNING: enable to find a sampling exponent\n");
            return false;
         }

         DBG("OA sampling exponent: %i ~= %"PRIu64"ms\n", period_exponent,
             prev_sample_period / 1000000ul);

         if (!gen_perf_open(perf_ctx, metric_id, queryinfo->oa_format,
                            period_exponent, perf_ctx->drm_fd,
                            perf_ctx->hw_ctx))
            return false;
      } else {
         assert(perf_ctx->current_oa_metrics_set_id == metric_id &&
                perf_ctx->current_oa_format == queryinfo->oa_format);
      }

      if (!inc_n_users(perf_ctx)) {
         DBG("WARNING: Error enabling i915 perf stream: %m\n");
         return false;
      }

      if (query->oa.bo) {
         perf_cfg->vtbl.bo_unreference(query->oa.bo);
         query->oa.bo = NULL;
      }

      query->oa.bo = perf_cfg->vtbl.bo_alloc(perf_ctx->bufmgr,
                                             "perf. query OA MI_RPC bo",
                                             MI_RPC_BO_SIZE);
#ifdef DEBUG
      /* Pre-filling the BO helps debug whether writes landed. */
      void *map = perf_cfg->vtbl.bo_map(perf_ctx->ctx, query->oa.bo, MAP_WRITE);
      memset(map, 0x80, MI_RPC_BO_SIZE);
      perf_cfg->vtbl.bo_unmap(query->oa.bo);
#endif

      query->oa.begin_report_id = perf_ctx->next_query_start_report_id;
      perf_ctx->next_query_start_report_id += 2;

      /* We flush the batchbuffer here to minimize the chances that MI_RPC
       * delimiting commands end up in different batchbuffers. If that's the
       * case, the measurement will include the time it takes for the kernel
       * scheduler to load a new request into the hardware. This is manifested in
       * tools like frameretrace by spikes in the "GPU Core Clocks" counter.
       */
      perf_cfg->vtbl.batchbuffer_flush(perf_ctx->ctx, __FILE__, __LINE__);

      /* Take a starting OA counter snapshot. */
      perf_cfg->vtbl.emit_mi_report_perf_count(perf_ctx->ctx, query->oa.bo, 0,
                                               query->oa.begin_report_id);
      perf_cfg->vtbl.capture_frequency_stat_register(perf_ctx->ctx, query->oa.bo,
                                                     MI_FREQ_START_OFFSET_BYTES);

      ++perf_ctx->n_active_oa_queries;

      /* No already-buffered samples can possibly be associated with this query
       * so create a marker within the list of sample buffers enabling us to
       * easily ignore earlier samples when processing this query after
       * completion.
       */
      assert(!exec_list_is_empty(&perf_ctx->sample_buffers));
      query->oa.samples_head = exec_list_get_tail(&perf_ctx->sample_buffers);

      struct oa_sample_buf *buf =
         exec_node_data(struct oa_sample_buf, query->oa.samples_head, link);

      /* This reference will ensure that future/following sample
       * buffers (that may relate to this query) can't be freed until
       * this drops to zero.
       */
      buf->refcount++;

      gen_perf_query_result_clear(&query->oa.result);
      query->oa.results_accumulated = false;

      add_to_unaccumulated_query_list(perf_ctx, query);
      break;
   }

   case GEN_PERF_QUERY_TYPE_PIPELINE:
      if (query->pipeline_stats.bo) {
         perf_cfg->vtbl.bo_unreference(query->pipeline_stats.bo);
         query->pipeline_stats.bo = NULL;
      }

      query->pipeline_stats.bo =
         perf_cfg->vtbl.bo_alloc(perf_ctx->bufmgr,
                                 "perf. query pipeline stats bo",
                                 STATS_BO_SIZE);

      /* Take starting snapshots. */
      snapshot_statistics_registers(perf_ctx->ctx , perf_cfg, query, 0);

      ++perf_ctx->n_active_pipeline_stats_queries;
      break;

   default:
      unreachable("Unknown query type");
      break;
   }

   return true;
}

void
gen_perf_end_query(struct gen_perf_context *perf_ctx,
                   struct gen_perf_query_object *query)
{
   struct gen_perf_config *perf_cfg = perf_ctx->perf;

   /* Ensure that the work associated with the queried commands will have
    * finished before taking our query end counter readings.
    *
    * For more details see comment in brw_begin_perf_query for
    * corresponding flush.
    */
  perf_cfg->vtbl.emit_mi_flush(perf_ctx->ctx);

   switch (query->queryinfo->kind) {
   case GEN_PERF_QUERY_TYPE_OA:
   case GEN_PERF_QUERY_TYPE_RAW:

      /* NB: It's possible that the query will have already been marked
       * as 'accumulated' if an error was seen while reading samples
       * from perf. In this case we mustn't try and emit a closing
       * MI_RPC command in case the OA unit has already been disabled
       */
      if (!query->oa.results_accumulated) {
         /* Take an ending OA counter snapshot. */
         perf_cfg->vtbl.capture_frequency_stat_register(perf_ctx->ctx, query->oa.bo,
                                                     MI_FREQ_END_OFFSET_BYTES);
         perf_cfg->vtbl.emit_mi_report_perf_count(perf_ctx->ctx, query->oa.bo,
                                             MI_RPC_BO_END_OFFSET_BYTES,
                                             query->oa.begin_report_id + 1);
      }

      --perf_ctx->n_active_oa_queries;

      /* NB: even though the query has now ended, it can't be accumulated
       * until the end MI_REPORT_PERF_COUNT snapshot has been written
       * to query->oa.bo
       */
      break;

   case GEN_PERF_QUERY_TYPE_PIPELINE:
      snapshot_statistics_registers(perf_ctx->ctx, perf_cfg, query,
                                    STATS_BO_END_OFFSET_BYTES);
      --perf_ctx->n_active_pipeline_stats_queries;
      break;

   default:
      unreachable("Unknown query type");
      break;
   }
}

enum OaReadStatus {
   OA_READ_STATUS_ERROR,
   OA_READ_STATUS_UNFINISHED,
   OA_READ_STATUS_FINISHED,
};

static enum OaReadStatus
read_oa_samples_until(struct gen_perf_context *perf_ctx,
                      uint32_t start_timestamp,
                      uint32_t end_timestamp)
{
   struct exec_node *tail_node =
      exec_list_get_tail(&perf_ctx->sample_buffers);
   struct oa_sample_buf *tail_buf =
      exec_node_data(struct oa_sample_buf, tail_node, link);
   uint32_t last_timestamp =
      tail_buf->len == 0 ? start_timestamp : tail_buf->last_timestamp;

   while (1) {
      struct oa_sample_buf *buf = get_free_sample_buf(perf_ctx);
      uint32_t offset;
      int len;

      while ((len = read(perf_ctx->oa_stream_fd, buf->buf,
                         sizeof(buf->buf))) < 0 && errno == EINTR)
         ;

      if (len <= 0) {
         exec_list_push_tail(&perf_ctx->free_sample_buffers, &buf->link);

         if (len < 0) {
            if (errno == EAGAIN) {
               return ((last_timestamp - start_timestamp) < INT32_MAX &&
                       (last_timestamp - start_timestamp) >=
                       (end_timestamp - start_timestamp)) ?
                      OA_READ_STATUS_FINISHED :
                      OA_READ_STATUS_UNFINISHED;
            } else {
               DBG("Error reading i915 perf samples: %m\n");
            }
         } else
            DBG("Spurious EOF reading i915 perf samples\n");

         return OA_READ_STATUS_ERROR;
      }

      buf->len = len;
      exec_list_push_tail(&perf_ctx->sample_buffers, &buf->link);

      /* Go through the reports and update the last timestamp. */
      offset = 0;
      while (offset < buf->len) {
         const struct drm_i915_perf_record_header *header =
            (const struct drm_i915_perf_record_header *) &buf->buf[offset];
         uint32_t *report = (uint32_t *) (header + 1);

         if (header->type == DRM_I915_PERF_RECORD_SAMPLE)
            last_timestamp = report[1];

         offset += header->size;
      }

      buf->last_timestamp = last_timestamp;
   }

   unreachable("not reached");
   return OA_READ_STATUS_ERROR;
}

/**
 * Try to read all the reports until either the delimiting timestamp
 * or an error arises.
 */
static bool
read_oa_samples_for_query(struct gen_perf_context *perf_ctx,
                          struct gen_perf_query_object *query,
                          void *current_batch)
{
   uint32_t *start;
   uint32_t *last;
   uint32_t *end;
   struct gen_perf_config *perf_cfg = perf_ctx->perf;

   /* We need the MI_REPORT_PERF_COUNT to land before we can start
    * accumulate. */
   assert(!perf_cfg->vtbl.batch_references(current_batch, query->oa.bo) &&
          !perf_cfg->vtbl.bo_busy(query->oa.bo));

   /* Map the BO once here and let accumulate_oa_reports() unmap
    * it. */
   if (query->oa.map == NULL)
      query->oa.map = perf_cfg->vtbl.bo_map(perf_ctx->ctx, query->oa.bo, MAP_READ);

   start = last = query->oa.map;
   end = query->oa.map + MI_RPC_BO_END_OFFSET_BYTES;

   if (start[0] != query->oa.begin_report_id) {
      DBG("Spurious start report id=%"PRIu32"\n", start[0]);
      return true;
   }
   if (end[0] != (query->oa.begin_report_id + 1)) {
      DBG("Spurious end report id=%"PRIu32"\n", end[0]);
      return true;
   }

   /* Read the reports until the end timestamp. */
   switch (read_oa_samples_until(perf_ctx, start[1], end[1])) {
   case OA_READ_STATUS_ERROR:
      /* Fallthrough and let accumulate_oa_reports() deal with the
       * error. */
   case OA_READ_STATUS_FINISHED:
      return true;
   case OA_READ_STATUS_UNFINISHED:
      return false;
   }

   unreachable("invalid read status");
   return false;
}

void
gen_perf_wait_query(struct gen_perf_context *perf_ctx,
                    struct gen_perf_query_object *query,
                    void *current_batch)
{
   struct gen_perf_config *perf_cfg = perf_ctx->perf;
   struct brw_bo *bo = NULL;

   switch (query->queryinfo->kind) {
   case GEN_PERF_QUERY_TYPE_OA:
   case GEN_PERF_QUERY_TYPE_RAW:
      bo = query->oa.bo;
      break;

   case GEN_PERF_QUERY_TYPE_PIPELINE:
      bo = query->pipeline_stats.bo;
      break;

   default:
      unreachable("Unknown query type");
      break;
   }

   if (bo == NULL)
      return;

   /* If the current batch references our results bo then we need to
    * flush first...
    */
   if (perf_cfg->vtbl.batch_references(current_batch, bo))
      perf_cfg->vtbl.batchbuffer_flush(perf_ctx->ctx, __FILE__, __LINE__);

   perf_cfg->vtbl.bo_wait_rendering(bo);

   /* Due to a race condition between the OA unit signaling report
    * availability and the report actually being written into memory,
    * we need to wait for all the reports to come in before we can
    * read them.
    */
   if (query->queryinfo->kind == GEN_PERF_QUERY_TYPE_OA ||
       query->queryinfo->kind == GEN_PERF_QUERY_TYPE_RAW) {
      while (!read_oa_samples_for_query(perf_ctx, query, current_batch))
         ;
   }
}

bool
gen_perf_is_query_ready(struct gen_perf_context *perf_ctx,
                        struct gen_perf_query_object *query,
                        void *current_batch)
{
   struct gen_perf_config *perf_cfg = perf_ctx->perf;

   switch (query->queryinfo->kind) {
   case GEN_PERF_QUERY_TYPE_OA:
   case GEN_PERF_QUERY_TYPE_RAW:
      return (query->oa.results_accumulated ||
              (query->oa.bo &&
               !perf_cfg->vtbl.batch_references(current_batch, query->oa.bo) &&
               !perf_cfg->vtbl.bo_busy(query->oa.bo) &&
               read_oa_samples_for_query(perf_ctx, query, current_batch)));
   case GEN_PERF_QUERY_TYPE_PIPELINE:
      return (query->pipeline_stats.bo &&
              !perf_cfg->vtbl.batch_references(current_batch, query->pipeline_stats.bo) &&
              !perf_cfg->vtbl.bo_busy(query->pipeline_stats.bo));

   default:
      unreachable("Unknown query type");
      break;
   }

   return false;
}

/**
 * Remove a query from the global list of unaccumulated queries once
 * after successfully accumulating the OA reports associated with the
 * query in accumulate_oa_reports() or when discarding unwanted query
 * results.
 */
static void
drop_from_unaccumulated_query_list(struct gen_perf_context *perf_ctx,
                                   struct gen_perf_query_object *query)
{
   for (int i = 0; i < perf_ctx->unaccumulated_elements; i++) {
      if (perf_ctx->unaccumulated[i] == query) {
         int last_elt = --perf_ctx->unaccumulated_elements;

         if (i == last_elt)
            perf_ctx->unaccumulated[i] = NULL;
         else {
            perf_ctx->unaccumulated[i] =
               perf_ctx->unaccumulated[last_elt];
         }

         break;
      }
   }

   /* Drop our samples_head reference so that associated periodic
    * sample data buffers can potentially be reaped if they aren't
    * referenced by any other queries...
    */

   struct oa_sample_buf *buf =
      exec_node_data(struct oa_sample_buf, query->oa.samples_head, link);

   assert(buf->refcount > 0);
   buf->refcount--;

   query->oa.samples_head = NULL;

   reap_old_sample_buffers(perf_ctx);
}

/* In general if we see anything spurious while accumulating results,
 * we don't try and continue accumulating the current query, hoping
 * for the best, we scrap anything outstanding, and then hope for the
 * best with new queries.
 */
static void
discard_all_queries(struct gen_perf_context *perf_ctx)
{
   while (perf_ctx->unaccumulated_elements) {
      struct gen_perf_query_object *query = perf_ctx->unaccumulated[0];

      query->oa.results_accumulated = true;
      drop_from_unaccumulated_query_list(perf_ctx, query);

      dec_n_users(perf_ctx);
   }
}

/* Looks for the validity bit of context ID (dword 2) of an OA report. */
static bool
oa_report_ctx_id_valid(const struct gen_device_info *devinfo,
                       const uint32_t *report)
{
   assert(devinfo->gen >= 8);
   if (devinfo->gen == 8)
      return (report[0] & (1 << 25)) != 0;
   return (report[0] & (1 << 16)) != 0;
}

/**
 * Accumulate raw OA counter values based on deltas between pairs of
 * OA reports.
 *
 * Accumulation starts from the first report captured via
 * MI_REPORT_PERF_COUNT (MI_RPC) by brw_begin_perf_query() until the
 * last MI_RPC report requested by brw_end_perf_query(). Between these
 * two reports there may also some number of periodically sampled OA
 * reports collected via the i915 perf interface - depending on the
 * duration of the query.
 *
 * These periodic snapshots help to ensure we handle counter overflow
 * correctly by being frequent enough to ensure we don't miss multiple
 * overflows of a counter between snapshots. For Gen8+ the i915 perf
 * snapshots provide the extra context-switch reports that let us
 * subtract out the progress of counters associated with other
 * contexts running on the system.
 */
static void
accumulate_oa_reports(struct gen_perf_context *perf_ctx,
                      struct gen_perf_query_object *query)
{
   const struct gen_device_info *devinfo = perf_ctx->devinfo;
   uint32_t *start;
   uint32_t *last;
   uint32_t *end;
   struct exec_node *first_samples_node;
   bool last_report_ctx_match = true;
   int out_duration = 0;

   assert(query->oa.map != NULL);

   start = last = query->oa.map;
   end = query->oa.map + MI_RPC_BO_END_OFFSET_BYTES;

   if (start[0] != query->oa.begin_report_id) {
      DBG("Spurious start report id=%"PRIu32"\n", start[0]);
      goto error;
   }
   if (end[0] != (query->oa.begin_report_id + 1)) {
      DBG("Spurious end report id=%"PRIu32"\n", end[0]);
      goto error;
   }

   /* See if we have any periodic reports to accumulate too... */

   /* N.B. The oa.samples_head was set when the query began and
    * pointed to the tail of the perf_ctx->sample_buffers list at
    * the time the query started. Since the buffer existed before the
    * first MI_REPORT_PERF_COUNT command was emitted we therefore know
    * that no data in this particular node's buffer can possibly be
    * associated with the query - so skip ahead one...
    */
   first_samples_node = query->oa.samples_head->next;

   foreach_list_typed_from(struct oa_sample_buf, buf, link,
                           &perf_ctx->sample_buffers,
                           first_samples_node)
   {
      int offset = 0;

      while (offset < buf->len) {
         const struct drm_i915_perf_record_header *header =
            (const struct drm_i915_perf_record_header *)(buf->buf + offset);

         assert(header->size != 0);
         assert(header->size <= buf->len);

         offset += header->size;

         switch (header->type) {
         case DRM_I915_PERF_RECORD_SAMPLE: {
            uint32_t *report = (uint32_t *)(header + 1);
            bool report_ctx_match = true;
            bool add = true;

            /* Ignore reports that come before the start marker.
             * (Note: takes care to allow overflow of 32bit timestamps)
             */
            if (gen_device_info_timebase_scale(devinfo,
                                               report[1] - start[1]) > 5000000000) {
               continue;
            }

            /* Ignore reports that come after the end marker.
             * (Note: takes care to allow overflow of 32bit timestamps)
             */
            if (gen_device_info_timebase_scale(devinfo,
                                               report[1] - end[1]) <= 5000000000) {
               goto end;
            }

            /* For Gen8+ since the counters continue while other
             * contexts are running we need to discount any unrelated
             * deltas. The hardware automatically generates a report
             * on context switch which gives us a new reference point
             * to continuing adding deltas from.
             *
             * For Haswell we can rely on the HW to stop the progress
             * of OA counters while any other context is acctive.
             */
            if (devinfo->gen >= 8) {
               /* Consider that the current report matches our context only if
                * the report says the report ID is valid.
                */
               report_ctx_match = oa_report_ctx_id_valid(devinfo, report) &&
                  report[2] == start[2];
               if (report_ctx_match)
                  out_duration = 0;
               else
                  out_duration++;

               /* Only add the delta between <last, report> if the last report
                * was clearly identified as our context, or if we have at most
                * 1 report without a matching ID.
                *
                * The OA unit will sometimes label reports with an invalid
                * context ID when i915 rewrites the execlist submit register
                * with the same context as the one currently running. This
                * happens when i915 wants to notify the HW of ringbuffer tail
                * register update. We have to consider this report as part of
                * our context as the 3d pipeline behind the OACS unit is still
                * processing the operations started at the previous execlist
                * submission.
                */
               add = last_report_ctx_match && out_duration < 2;
            }

            if (add) {
               gen_perf_query_result_accumulate(&query->oa.result,
                                                query->queryinfo,
                                                last, report);
            } else {
               /* We're not adding the delta because we've identified it's not
                * for the context we filter for. We can consider that the
                * query was split.
                */
               query->oa.result.query_disjoint = true;
            }

            last = report;
            last_report_ctx_match = report_ctx_match;

            break;
         }

         case DRM_I915_PERF_RECORD_OA_BUFFER_LOST:
             DBG("i915 perf: OA error: all reports lost\n");
             goto error;
         case DRM_I915_PERF_RECORD_OA_REPORT_LOST:
             DBG("i915 perf: OA report lost\n");
             break;
         }
      }
   }

end:

   gen_perf_query_result_accumulate(&query->oa.result, query->queryinfo,
                                    last, end);

   query->oa.results_accumulated = true;
   drop_from_unaccumulated_query_list(perf_ctx, query);
   dec_n_users(perf_ctx);

   return;

error:

   discard_all_queries(perf_ctx);
}

void
gen_perf_delete_query(struct gen_perf_context *perf_ctx,
                      struct gen_perf_query_object *query)
{
   struct gen_perf_config *perf_cfg = perf_ctx->perf;

   /* We can assume that the frontend waits for a query to complete
    * before ever calling into here, so we don't have to worry about
    * deleting an in-flight query object.
    */
   switch (query->queryinfo->kind) {
   case GEN_PERF_QUERY_TYPE_OA:
   case GEN_PERF_QUERY_TYPE_RAW:
      if (query->oa.bo) {
         if (!query->oa.results_accumulated) {
            drop_from_unaccumulated_query_list(perf_ctx, query);
            dec_n_users(perf_ctx);
         }

         perf_cfg->vtbl.bo_unreference(query->oa.bo);
         query->oa.bo = NULL;
      }

      query->oa.results_accumulated = false;
      break;

   case GEN_PERF_QUERY_TYPE_PIPELINE:
      if (query->pipeline_stats.bo) {
         perf_cfg->vtbl.bo_unreference(query->pipeline_stats.bo);
         query->pipeline_stats.bo = NULL;
      }
      break;

   default:
      unreachable("Unknown query type");
      break;
   }

   /* As an indication that the INTEL_performance_query extension is no
    * longer in use, it's a good time to free our cache of sample
    * buffers and close any current i915-perf stream.
    */
   if (--perf_ctx->n_query_instances == 0) {
      free_sample_bufs(perf_ctx);
      gen_perf_close(perf_ctx, query->queryinfo);
   }

   free(query);
}

#define GET_FIELD(word, field) (((word)  & field ## _MASK) >> field ## _SHIFT)

static void
read_gt_frequency(struct gen_perf_context *perf_ctx,
                  struct gen_perf_query_object *obj)
{
   const struct gen_device_info *devinfo = perf_ctx->devinfo;
   uint32_t start = *((uint32_t *)(obj->oa.map + MI_FREQ_START_OFFSET_BYTES)),
      end = *((uint32_t *)(obj->oa.map + MI_FREQ_END_OFFSET_BYTES));

   switch (devinfo->gen) {
   case 7:
   case 8:
      obj->oa.gt_frequency[0] = GET_FIELD(start, GEN7_RPSTAT1_CURR_GT_FREQ) * 50ULL;
      obj->oa.gt_frequency[1] = GET_FIELD(end, GEN7_RPSTAT1_CURR_GT_FREQ) * 50ULL;
      break;
   case 9:
   case 10:
   case 11:
      obj->oa.gt_frequency[0] = GET_FIELD(start, GEN9_RPSTAT0_CURR_GT_FREQ) * 50ULL / 3ULL;
      obj->oa.gt_frequency[1] = GET_FIELD(end, GEN9_RPSTAT0_CURR_GT_FREQ) * 50ULL / 3ULL;
      break;
   default:
      unreachable("unexpected gen");
   }

   /* Put the numbers into Hz. */
   obj->oa.gt_frequency[0] *= 1000000ULL;
   obj->oa.gt_frequency[1] *= 1000000ULL;
}

static int
get_oa_counter_data(struct gen_perf_context *perf_ctx,
                    struct gen_perf_query_object *query,
                    size_t data_size,
                    uint8_t *data)
{
   struct gen_perf_config *perf_cfg = perf_ctx->perf;
   const struct gen_perf_query_info *queryinfo = query->queryinfo;
   int n_counters = queryinfo->n_counters;
   int written = 0;

   for (int i = 0; i < n_counters; i++) {
      const struct gen_perf_query_counter *counter = &queryinfo->counters[i];
      uint64_t *out_uint64;
      float *out_float;
      size_t counter_size = gen_perf_query_counter_get_size(counter);

      if (counter_size) {
         switch (counter->data_type) {
         case GEN_PERF_COUNTER_DATA_TYPE_UINT64:
            out_uint64 = (uint64_t *)(data + counter->offset);
            *out_uint64 =
               counter->oa_counter_read_uint64(perf_cfg, queryinfo,
                                               query->oa.result.accumulator);
            break;
         case GEN_PERF_COUNTER_DATA_TYPE_FLOAT:
            out_float = (float *)(data + counter->offset);
            *out_float =
               counter->oa_counter_read_float(perf_cfg, queryinfo,
                                              query->oa.result.accumulator);
            break;
         default:
            /* So far we aren't using uint32, double or bool32... */
            unreachable("unexpected counter data type");
         }
         written = counter->offset + counter_size;
      }
   }

   return written;
}

static int
get_pipeline_stats_data(struct gen_perf_context *perf_ctx,
                        struct gen_perf_query_object *query,
                        size_t data_size,
                        uint8_t *data)

{
   struct gen_perf_config *perf_cfg = perf_ctx->perf;
   const struct gen_perf_query_info *queryinfo = query->queryinfo;
   int n_counters = queryinfo->n_counters;
   uint8_t *p = data;

   uint64_t *start = perf_cfg->vtbl.bo_map(perf_ctx->ctx, query->pipeline_stats.bo, MAP_READ);
   uint64_t *end = start + (STATS_BO_END_OFFSET_BYTES / sizeof(uint64_t));

   for (int i = 0; i < n_counters; i++) {
      const struct gen_perf_query_counter *counter = &queryinfo->counters[i];
      uint64_t value = end[i] - start[i];

      if (counter->pipeline_stat.numerator !=
          counter->pipeline_stat.denominator) {
         value *= counter->pipeline_stat.numerator;
         value /= counter->pipeline_stat.denominator;
      }

      *((uint64_t *)p) = value;
      p += 8;
   }

   perf_cfg->vtbl.bo_unmap(query->pipeline_stats.bo);

   return p - data;
}

void
gen_perf_get_query_data(struct gen_perf_context *perf_ctx,
                        struct gen_perf_query_object *query,
                        int data_size,
                        unsigned *data,
                        unsigned *bytes_written)
{
   struct gen_perf_config *perf_cfg = perf_ctx->perf;
   int written = 0;

   switch (query->queryinfo->kind) {
   case GEN_PERF_QUERY_TYPE_OA:
   case GEN_PERF_QUERY_TYPE_RAW:
      if (!query->oa.results_accumulated) {
         read_gt_frequency(perf_ctx, query);
         uint32_t *begin_report = query->oa.map;
         uint32_t *end_report = query->oa.map + MI_RPC_BO_END_OFFSET_BYTES;
         gen_perf_query_result_read_frequencies(&query->oa.result,
                                                perf_ctx->devinfo,
                                                begin_report,
                                                end_report);
         accumulate_oa_reports(perf_ctx, query);
         assert(query->oa.results_accumulated);

         perf_cfg->vtbl.bo_unmap(query->oa.bo);
         query->oa.map = NULL;
      }
      if (query->queryinfo->kind == GEN_PERF_QUERY_TYPE_OA) {
         written = get_oa_counter_data(perf_ctx, query, data_size, (uint8_t *)data);
      } else {
         const struct gen_device_info *devinfo = perf_ctx->devinfo;

         written = gen_perf_query_result_write_mdapi((uint8_t *)data, data_size,
                                                     devinfo, &query->oa.result,
                                                     query->oa.gt_frequency[0],
                                                     query->oa.gt_frequency[1]);
      }
      break;

   case GEN_PERF_QUERY_TYPE_PIPELINE:
      written = get_pipeline_stats_data(perf_ctx, query, data_size, (uint8_t *)data);
      break;

   default:
      unreachable("Unknown query type");
      break;
   }

   if (bytes_written)
      *bytes_written = written;
}

void
gen_perf_dump_query_count(struct gen_perf_context *perf_ctx)
{
   DBG("Queries: (Open queries = %d, OA users = %d)\n",
       perf_ctx->n_active_oa_queries, perf_ctx->n_oa_users);
}

void
gen_perf_dump_query(struct gen_perf_context *ctx,
                    struct gen_perf_query_object *obj,
                    void *current_batch)
{
   switch (obj->queryinfo->kind) {
   case GEN_PERF_QUERY_TYPE_OA:
   case GEN_PERF_QUERY_TYPE_RAW:
      DBG("BO: %-4s OA data: %-10s %-15s\n",
          obj->oa.bo ? "yes," : "no,",
          gen_perf_is_query_ready(ctx, obj, current_batch) ? "ready," : "not ready,",
          obj->oa.results_accumulated ? "accumulated" : "not accumulated");
      break;
   case GEN_PERF_QUERY_TYPE_PIPELINE:
      DBG("BO: %-4s\n",
          obj->pipeline_stats.bo ? "yes" : "no");
      break;
   default:
      unreachable("Unknown query type");
      break;
   }
}
