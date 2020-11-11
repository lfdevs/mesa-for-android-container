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

#ifndef HAVE_DIRENT_D_TYPE
#include <limits.h> // PATH_MAX
#endif

#include <drm-uapi/i915_drm.h>

#include "common/gen_gem.h"

#include "dev/gen_debug.h"
#include "dev/gen_device_info.h"

#include "perf/gen_perf.h"
#include "perf/gen_perf_regs.h"
#include "perf/gen_perf_mdapi.h"
#include "perf/gen_perf_metrics.h"
#include "perf/gen_perf_private.h"

#include "util/bitscan.h"
#include "util/macros.h"
#include "util/mesa-sha1.h"
#include "util/u_math.h"

#define FILE_DEBUG_FLAG DEBUG_PERFMON

#define OA_REPORT_INVALID_CTX_ID (0xffffffff)

static bool
is_dir_or_link(const struct dirent *entry, const char *parent_dir)
{
#ifdef HAVE_DIRENT_D_TYPE
   return entry->d_type == DT_DIR || entry->d_type == DT_LNK;
#else
   struct stat st;
   char path[PATH_MAX + 1];
   snprintf(path, sizeof(path), "%s/%s", parent_dir, entry->d_name);
   lstat(path, &st);
   return S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode);
#endif
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

   if (INTEL_DEBUG & DEBUG_NO_OACONFIG)
      return true;

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
      if (is_dir_or_link(drm_entry, perf->sysfs_dev_dir) &&
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

static void
register_oa_config(struct gen_perf_config *perf,
                   const struct gen_device_info *devinfo,
                   const struct gen_perf_query_info *query,
                   uint64_t config_id)
{
   struct gen_perf_query_info *registered_query =
      gen_perf_append_query_info(perf, 0);

   *registered_query = *query;
   registered_query->oa_format = devinfo->gen >= 8 ?
      I915_OA_FORMAT_A32u40_A4u32_B8_C8 : I915_OA_FORMAT_A45_B8_C8;
   registered_query->oa_metrics_set_id = config_id;
   DBG("metric set registered: id = %" PRIu64", guid = %s\n",
       registered_query->oa_metrics_set_id, query->guid);
}

static void
enumerate_sysfs_metrics(struct gen_perf_config *perf,
                        const struct gen_device_info *devinfo)
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
      if (!is_dir_or_link(metric_entry, buf) ||
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

         register_oa_config(perf, devinfo,
                            (const struct gen_perf_query_info *)entry->data, id);
      } else
         DBG("metric set not known by mesa (skipping)\n");
   }

   closedir(metricsdir);
}

static void
add_all_metrics(struct gen_perf_config *perf,
                const struct gen_device_info *devinfo)
{
   hash_table_foreach(perf->oa_metrics_table, entry) {
      const struct gen_perf_query_info *query = entry->data;
      register_oa_config(perf, devinfo, query, 0);
   }
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
   i915_config.mux_regs_ptr = to_const_user_pointer(config->mux_regs);

   i915_config.n_boolean_regs = config->n_b_counter_regs;
   i915_config.boolean_regs_ptr = to_const_user_pointer(config->b_counter_regs);

   i915_config.n_flex_regs = config->n_flex_regs;
   i915_config.flex_regs_ptr = to_const_user_pointer(config->flex_regs);

   int ret = gen_ioctl(fd, DRM_IOCTL_I915_PERF_ADD_CONFIG, &i915_config);
   return ret > 0 ? ret : 0;
}

static void
init_oa_configs(struct gen_perf_config *perf, int fd,
                const struct gen_device_info *devinfo)
{
   hash_table_foreach(perf->oa_metrics_table, entry) {
      const struct gen_perf_query_info *query = entry->data;
      uint64_t config_id;

      if (gen_perf_load_metric_id(perf, query->guid, &config_id)) {
         DBG("metric set: %s (already loaded)\n", query->guid);
         register_oa_config(perf, devinfo, query, config_id);
         continue;
      }

      int ret = i915_add_config(perf, fd, &query->config, query->guid);
      if (ret < 0) {
         DBG("Failed to load \"%s\" (%s) metrics set in kernel: %s\n",
             query->name, query->guid, strerror(errno));
         continue;
      }

      register_oa_config(perf, devinfo, query, ret);
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

   if (!(INTEL_DEBUG & DEBUG_NO_OACONFIG)) {
      if (!read_sysfs_drm_device_file_uint64(perf, "gt_min_freq_mhz", &min_freq_mhz))
         return false;

      if (!read_sysfs_drm_device_file_uint64(perf,  "gt_max_freq_mhz", &max_freq_mhz))
         return false;
   } else {
      min_freq_mhz = 300;
      max_freq_mhz = 1000;
   }

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
   if (devinfo->gen == 11) {
      if (devinfo->is_elkhartlake)
         return gen_oa_register_queries_lkf;
      return gen_oa_register_queries_icl;
   }
   if (devinfo->gen == 12)
      return gen_oa_register_queries_tgl;

   return NULL;
}

static int
gen_perf_compare_counter_names(const void *v1, const void *v2)
{
   const struct gen_perf_query_counter *c1 = v1;
   const struct gen_perf_query_counter *c2 = v2;

   return strcmp(c1->name, c2->name);
}

static void
sort_query(struct gen_perf_query_info *q)
{
   qsort(q->counters, q->n_counters, sizeof(q->counters[0]),
         gen_perf_compare_counter_names);
}

static void
load_pipeline_statistic_metrics(struct gen_perf_config *perf_cfg,
                                const struct gen_device_info *devinfo)
{
   struct gen_perf_query_info *query =
      gen_perf_append_query_info(perf_cfg, MAX_STAT_COUNTERS);

   query->kind = GEN_PERF_QUERY_TYPE_PIPELINE;
   query->name = "Pipeline Statistics Registers";

   gen_perf_query_add_basic_stat_reg(query, IA_VERTICES_COUNT,
                                     "N vertices submitted");
   gen_perf_query_add_basic_stat_reg(query, IA_PRIMITIVES_COUNT,
                                     "N primitives submitted");
   gen_perf_query_add_basic_stat_reg(query, VS_INVOCATION_COUNT,
                                     "N vertex shader invocations");

   if (devinfo->gen == 6) {
      gen_perf_query_add_stat_reg(query, GEN6_SO_PRIM_STORAGE_NEEDED, 1, 1,
                                  "SO_PRIM_STORAGE_NEEDED",
                                  "N geometry shader stream-out primitives (total)");
      gen_perf_query_add_stat_reg(query, GEN6_SO_NUM_PRIMS_WRITTEN, 1, 1,
                                  "SO_NUM_PRIMS_WRITTEN",
                                  "N geometry shader stream-out primitives (written)");
   } else {
      gen_perf_query_add_stat_reg(query, GEN7_SO_PRIM_STORAGE_NEEDED(0), 1, 1,
                                  "SO_PRIM_STORAGE_NEEDED (Stream 0)",
                                  "N stream-out (stream 0) primitives (total)");
      gen_perf_query_add_stat_reg(query, GEN7_SO_PRIM_STORAGE_NEEDED(1), 1, 1,
                                  "SO_PRIM_STORAGE_NEEDED (Stream 1)",
                                  "N stream-out (stream 1) primitives (total)");
      gen_perf_query_add_stat_reg(query, GEN7_SO_PRIM_STORAGE_NEEDED(2), 1, 1,
                                  "SO_PRIM_STORAGE_NEEDED (Stream 2)",
                                  "N stream-out (stream 2) primitives (total)");
      gen_perf_query_add_stat_reg(query, GEN7_SO_PRIM_STORAGE_NEEDED(3), 1, 1,
                                  "SO_PRIM_STORAGE_NEEDED (Stream 3)",
                                  "N stream-out (stream 3) primitives (total)");
      gen_perf_query_add_stat_reg(query, GEN7_SO_NUM_PRIMS_WRITTEN(0), 1, 1,
                                  "SO_NUM_PRIMS_WRITTEN (Stream 0)",
                                  "N stream-out (stream 0) primitives (written)");
      gen_perf_query_add_stat_reg(query, GEN7_SO_NUM_PRIMS_WRITTEN(1), 1, 1,
                                  "SO_NUM_PRIMS_WRITTEN (Stream 1)",
                                  "N stream-out (stream 1) primitives (written)");
      gen_perf_query_add_stat_reg(query, GEN7_SO_NUM_PRIMS_WRITTEN(2), 1, 1,
                                  "SO_NUM_PRIMS_WRITTEN (Stream 2)",
                                  "N stream-out (stream 2) primitives (written)");
      gen_perf_query_add_stat_reg(query, GEN7_SO_NUM_PRIMS_WRITTEN(3), 1, 1,
                                  "SO_NUM_PRIMS_WRITTEN (Stream 3)",
                                  "N stream-out (stream 3) primitives (written)");
   }

   gen_perf_query_add_basic_stat_reg(query, HS_INVOCATION_COUNT,
                                     "N TCS shader invocations");
   gen_perf_query_add_basic_stat_reg(query, DS_INVOCATION_COUNT,
                                     "N TES shader invocations");

   gen_perf_query_add_basic_stat_reg(query, GS_INVOCATION_COUNT,
                                     "N geometry shader invocations");
   gen_perf_query_add_basic_stat_reg(query, GS_PRIMITIVES_COUNT,
                                     "N geometry shader primitives emitted");

   gen_perf_query_add_basic_stat_reg(query, CL_INVOCATION_COUNT,
                                     "N primitives entering clipping");
   gen_perf_query_add_basic_stat_reg(query, CL_PRIMITIVES_COUNT,
                                     "N primitives leaving clipping");

   if (devinfo->is_haswell || devinfo->gen == 8) {
      gen_perf_query_add_stat_reg(query, PS_INVOCATION_COUNT, 1, 4,
                                  "N fragment shader invocations",
                                  "N fragment shader invocations");
   } else {
      gen_perf_query_add_basic_stat_reg(query, PS_INVOCATION_COUNT,
                                        "N fragment shader invocations");
   }

   gen_perf_query_add_basic_stat_reg(query, PS_DEPTH_COUNT,
                                     "N z-pass fragments");

   if (devinfo->gen >= 7) {
      gen_perf_query_add_basic_stat_reg(query, CS_INVOCATION_COUNT,
                                        "N compute shader invocations");
   }

   query->data_size = sizeof(uint64_t) * query->n_counters;

   sort_query(query);
}

static int
i915_perf_version(int drm_fd)
{
   int tmp;
   drm_i915_getparam_t gp = {
      .param = I915_PARAM_PERF_REVISION,
      .value = &tmp,
   };

   int ret = gen_ioctl(drm_fd, DRM_IOCTL_I915_GETPARAM, &gp);

   /* Return 0 if this getparam is not supported, the first version supported
    * is 1.
    */
   return ret < 0 ? 0 : tmp;
}

static void
i915_get_sseu(int drm_fd, struct drm_i915_gem_context_param_sseu *sseu)
{
   struct drm_i915_gem_context_param arg = {
      .param = I915_CONTEXT_PARAM_SSEU,
      .size = sizeof(*sseu),
      .value = to_user_pointer(sseu)
   };

   gen_ioctl(drm_fd, DRM_IOCTL_I915_GEM_CONTEXT_GETPARAM, &arg);
}

static inline int
compare_str_or_null(const char *s1, const char *s2)
{
   if (s1 == NULL && s2 == NULL)
      return 0;
   if (s1 == NULL)
      return -1;
   if (s2 == NULL)
      return 1;

   return strcmp(s1, s2);
}

static int
compare_counter_categories_and_names(const void *_c1, const void *_c2)
{
   const struct gen_perf_query_counter_info *c1 = (const struct gen_perf_query_counter_info *)_c1;
   const struct gen_perf_query_counter_info *c2 = (const struct gen_perf_query_counter_info *)_c2;

   /* pipeline counters don't have an assigned category */
   int r = compare_str_or_null(c1->counter->category, c2->counter->category);
   if (r)
      return r;

   return strcmp(c1->counter->name, c2->counter->name);
}

static void
build_unique_counter_list(struct gen_perf_config *perf)
{
   assert(perf->n_queries < 64);

   size_t max_counters = 0;

   for (int q = 0; q < perf->n_queries; q++)
      max_counters += perf->queries[q].n_counters;

   /*
    * Allocate big enough array to hold maximum possible number of counters.
    * We can't alloc it small and realloc when needed because the hash table
    * below contains pointers to this array.
    */
   struct gen_perf_query_counter_info *counter_infos =
         ralloc_array_size(perf, sizeof(counter_infos[0]), max_counters);

   perf->n_counters = 0;

   struct hash_table *counters_table =
      _mesa_hash_table_create(perf,
                              _mesa_hash_string,
                              _mesa_key_string_equal);
   struct hash_entry *entry;
   for (int q = 0; q < perf->n_queries ; q++) {
      struct gen_perf_query_info *query = &perf->queries[q];

      for (int c = 0; c < query->n_counters; c++) {
         struct gen_perf_query_counter *counter;
         struct gen_perf_query_counter_info *counter_info;

         counter = &query->counters[c];
         entry = _mesa_hash_table_search(counters_table, counter->symbol_name);

         if (entry) {
            counter_info = entry->data;
            counter_info->query_mask |= BITFIELD64_BIT(q);
            continue;
         }
         assert(perf->n_counters < max_counters);

         counter_info = &counter_infos[perf->n_counters++];
         counter_info->counter = counter;
         counter_info->query_mask = BITFIELD64_BIT(q);

         counter_info->location.group_idx = q;
         counter_info->location.counter_idx = c;

         _mesa_hash_table_insert(counters_table, counter->symbol_name, counter_info);
      }
   }

   _mesa_hash_table_destroy(counters_table, NULL);

   /* Now we can realloc counter_infos array because hash table doesn't exist. */
   perf->counter_infos = reralloc_array_size(perf, counter_infos,
         sizeof(counter_infos[0]), perf->n_counters);

   qsort(perf->counter_infos, perf->n_counters, sizeof(perf->counter_infos[0]),
         compare_counter_categories_and_names);
}

static bool
oa_metrics_available(struct gen_perf_config *perf, int fd,
      const struct gen_device_info *devinfo)
{
   perf_register_oa_queries_t oa_register = get_register_queries_function(devinfo);
   bool i915_perf_oa_available = false;
   struct stat sb;

   perf->i915_query_supported = i915_query_perf_config_supported(perf, fd);
   perf->i915_perf_version = i915_perf_version(fd);

   /* Record the default SSEU configuration. */
   i915_get_sseu(fd, &perf->sseu);

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

      perf->platform_supported = oa_register != NULL;
   }

   return i915_perf_oa_available &&
          oa_register &&
          get_sysfs_dev_dir(perf, fd) &&
          init_oa_sys_vars(perf, devinfo);
}

static void
load_oa_metrics(struct gen_perf_config *perf, int fd,
                const struct gen_device_info *devinfo)
{
   int existing_queries = perf->n_queries;

   perf_register_oa_queries_t oa_register = get_register_queries_function(devinfo);

   perf->oa_metrics_table =
      _mesa_hash_table_create(perf, _mesa_hash_string,
                              _mesa_key_string_equal);

   /* Index all the metric sets mesa knows about before looking to see what
    * the kernel is advertising.
    */
   oa_register(perf);

   if (!(INTEL_DEBUG & DEBUG_NO_OACONFIG)) {
      if (kernel_has_dynamic_config_support(perf, fd))
         init_oa_configs(perf, fd, devinfo);
      else
         enumerate_sysfs_metrics(perf, devinfo);
   } else {
      add_all_metrics(perf, devinfo);
   }

   /* sort counters in each individual group created by this function by name */
   for (int i = existing_queries; i < perf->n_queries; ++i)
      sort_query(&perf->queries[i]);

   /* Select a fallback OA metric. Look for the TestOa metric or use the last
    * one if no present (on HSW).
    */
   for (int i = existing_queries; i < perf->n_queries; i++) {
      if (perf->queries[i].symbol_name &&
          strcmp(perf->queries[i].symbol_name, "TestOa") == 0) {
         perf->fallback_raw_oa_metric = perf->queries[i].oa_metrics_set_id;
         break;
      }
   }
   if (perf->fallback_raw_oa_metric == 0 && perf->n_queries > 0)
      perf->fallback_raw_oa_metric = perf->queries[perf->n_queries - 1].oa_metrics_set_id;
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
   i915_config.flex_regs_ptr = to_const_user_pointer(config->flex_regs);
   i915_config.mux_regs_ptr = to_const_user_pointer(config->mux_regs);
   i915_config.boolean_regs_ptr = to_const_user_pointer(config->b_counter_regs);
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

static uint64_t
get_passes_mask(struct gen_perf_config *perf,
                const uint32_t *counter_indices,
                uint32_t counter_indices_count)
{
   uint64_t queries_mask = 0;

   assert(perf->n_queries < 64);

   /* Compute the number of passes by going through all counters N times (with
    * N the number of queries) to make sure we select the most constraining
    * counters first and look at the more flexible ones (that could be
    * obtained from multiple queries) later. That way we minimize the number
    * of passes required.
    */
   for (uint32_t q = 0; q < perf->n_queries; q++) {
      for (uint32_t i = 0; i < counter_indices_count; i++) {
         assert(counter_indices[i] < perf->n_counters);

         uint32_t idx = counter_indices[i];
         if (__builtin_popcount(perf->counter_infos[idx].query_mask) != (q + 1))
            continue;

         if (queries_mask & perf->counter_infos[idx].query_mask)
            continue;

         queries_mask |= BITFIELD64_BIT(ffsll(perf->counter_infos[idx].query_mask) - 1);
      }
   }

   return queries_mask;
}

uint32_t
gen_perf_get_n_passes(struct gen_perf_config *perf,
                      const uint32_t *counter_indices,
                      uint32_t counter_indices_count,
                      struct gen_perf_query_info **pass_queries)
{
   uint64_t queries_mask = get_passes_mask(perf, counter_indices, counter_indices_count);

   if (pass_queries) {
      uint32_t pass = 0;
      for (uint32_t q = 0; q < perf->n_queries; q++) {
         if ((1ULL << q) & queries_mask)
            pass_queries[pass++] = &perf->queries[q];
      }
   }

   return __builtin_popcount(queries_mask);
}

void
gen_perf_get_counters_passes(struct gen_perf_config *perf,
                             const uint32_t *counter_indices,
                             uint32_t counter_indices_count,
                             struct gen_perf_counter_pass *counter_pass)
{
   uint64_t queries_mask = get_passes_mask(perf, counter_indices, counter_indices_count);
   ASSERTED uint32_t n_passes = __builtin_popcount(queries_mask);

   for (uint32_t i = 0; i < counter_indices_count; i++) {
      assert(counter_indices[i] < perf->n_counters);

      uint32_t idx = counter_indices[i];
      counter_pass[i].counter = perf->counter_infos[idx].counter;

      uint32_t query_idx = ffsll(perf->counter_infos[idx].query_mask & queries_mask) - 1;
      counter_pass[i].query = &perf->queries[query_idx];

      uint32_t clear_bits = 63 - query_idx;
      counter_pass[i].pass = __builtin_popcount((queries_mask << clear_bits) >> clear_bits) - 1;
      assert(counter_pass[i].pass < n_passes);
   }
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
   int i;

   if (result->hw_id == OA_REPORT_INVALID_CTX_ID &&
       start[2] != OA_REPORT_INVALID_CTX_ID)
      result->hw_id = start[2];
   if (result->reports_accumulated == 0)
      result->begin_timestamp = start[1];
   result->reports_accumulated++;

   switch (query->oa_format) {
   case I915_OA_FORMAT_A32u40_A4u32_B8_C8:
      accumulate_uint32(start + 1, end + 1,
                        result->accumulator + query->gpu_time_offset); /* timestamp */
      accumulate_uint32(start + 3, end + 3,
                        result->accumulator + query->gpu_clock_offset); /* clock */

      /* 32x 40bit A counters... */
      for (i = 0; i < 32; i++) {
         accumulate_uint40(i, start, end,
                           result->accumulator + query->a_offset + i);
      }

      /* 4x 32bit A counters... */
      for (i = 0; i < 4; i++) {
         accumulate_uint32(start + 36 + i, end + 36 + i,
                           result->accumulator + query->a_offset + 32 + i);
      }

      /* 8x 32bit B counters */
      for (i = 0; i < 8; i++) {
         accumulate_uint32(start + 48 + i, end + 48 + i,
                           result->accumulator + query->b_offset + i);
      }

      /* 8x 32bit C counters... */
      for (i = 0; i < 8; i++) {
         accumulate_uint32(start + 56 + i, end + 56 + i,
                           result->accumulator + query->c_offset + i);
      }
      break;

   case I915_OA_FORMAT_A45_B8_C8:
      accumulate_uint32(start + 1, end + 1, result->accumulator); /* timestamp */

      for (i = 0; i < 61; i++) {
         accumulate_uint32(start + 3 + i, end + 3 + i,
                           result->accumulator + query->a_offset + i);
      }
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

static int
gen_perf_compare_query_names(const void *v1, const void *v2)
{
   const struct gen_perf_query_info *q1 = v1;
   const struct gen_perf_query_info *q2 = v2;

   return strcmp(q1->name, q2->name);
}

void
gen_perf_init_metrics(struct gen_perf_config *perf_cfg,
                      const struct gen_device_info *devinfo,
                      int drm_fd,
                      bool include_pipeline_statistics)
{
   if (include_pipeline_statistics) {
      load_pipeline_statistic_metrics(perf_cfg, devinfo);
      gen_perf_register_mdapi_statistic_query(perf_cfg, devinfo);
   }

   bool oa_metrics = oa_metrics_available(perf_cfg, drm_fd, devinfo);
   if (oa_metrics)
      load_oa_metrics(perf_cfg, drm_fd, devinfo);

   /* sort query groups by name */
   qsort(perf_cfg->queries, perf_cfg->n_queries,
         sizeof(perf_cfg->queries[0]), gen_perf_compare_query_names);

   build_unique_counter_list(perf_cfg);

   if (oa_metrics)
      gen_perf_register_mdapi_oa_query(perf_cfg, devinfo);
}
