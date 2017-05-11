/*
 * Copyright © 2013 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * \file brw_performance_query.c
 *
 * Implementation of the GL_INTEL_performance_query extension.
 *
 * Currently there are two possible counter sources exposed here:
 *
 * On Gen6+ hardware we have numerous 64bit Pipeline Statistics Registers
 * that we can snapshot at the beginning and end of a query.
 *
 * On Gen7.5+ we have Observability Architecture counters which are
 * covered in separate document from the rest of the PRMs.  It is available at:
 * https://01.org/linuxgraphics/documentation/driver-documentation-prms
 * => 2013 Intel Core Processor Family => Observability Performance Counters
 * (This one volume covers Sandybridge, Ivybridge, Baytrail, and Haswell,
 * though notably we currently only support OA counters for Haswell+)
 */

#include <limits.h>
#include <dirent.h>

/* put before sys/types.h to silence glibc warnings */
#ifdef MAJOR_IN_MKDEV
#include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMACROS
#include <sys/sysmacros.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <xf86drm.h>
#include <i915_drm.h>

#include "main/hash.h"
#include "main/macros.h"
#include "main/mtypes.h"
#include "main/performance_query.h"

#include "util/bitset.h"
#include "util/ralloc.h"
#include "util/hash_table.h"
#include "util/list.h"

#include "brw_context.h"
#include "brw_defines.h"
#include "brw_performance_query.h"
#include "brw_oa_hsw.h"
#include "intel_batchbuffer.h"

#define FILE_DEBUG_FLAG DEBUG_PERFMON

/*
 * The largest OA format we can use on Haswell includes:
 * 1 timestamp, 45 A counters, 8 B counters and 8 C counters.
 */
#define MAX_OA_REPORT_COUNTERS 62

#define I915_PERF_OA_SAMPLE_SIZE (8 +   /* drm_i915_perf_record_header */ \
                                  256)  /* OA counter report */

/**
 * Periodic OA samples are read() into these buffer structures via the
 * i915 perf kernel interface and appended to the
 * brw->perfquery.sample_buffers linked list. When we process the
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
struct brw_oa_sample_buf {
   struct exec_node link;
   int refcount;
   int len;
   uint8_t buf[I915_PERF_OA_SAMPLE_SIZE * 10];
};

/**
 * i965 representation of a performance query object.
 *
 * NB: We want to keep this structure relatively lean considering that
 * applications may expect to allocate enough objects to be able to
 * query around all draw calls in a frame.
 */
struct brw_perf_query_object
{
   struct gl_perf_query_object base;

   const struct brw_perf_query_info *query;

   /* See query->kind to know which state below is in use... */
   union {
      struct {

         /**
          * BO containing OA counter snapshots at query Begin/End time.
          */
         struct brw_bo *bo;

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
          * Storage for the final accumulated OA counters.
          */
         uint64_t accumulator[MAX_OA_REPORT_COUNTERS];

         /**
          * false while in the unaccumulated_elements list, and set to
          * true when the final, end MI_RPC snapshot has been
          * accumulated.
          */
         bool results_accumulated;

      } oa;

      struct {
         /**
          * BO containing starting and ending snapshots for the
          * statistics counters.
          */
         struct brw_bo *bo;
      } pipeline_stats;
   };
};

/** Downcasting convenience macro. */
static inline struct brw_perf_query_object *
brw_perf_query(struct gl_perf_query_object *o)
{
   return (struct brw_perf_query_object *) o;
}

#define STATS_BO_SIZE               4096
#define STATS_BO_END_OFFSET_BYTES   (STATS_BO_SIZE / 2)
#define MAX_STAT_COUNTERS           (STATS_BO_END_OFFSET_BYTES / 8)

#define MI_RPC_BO_SIZE              4096
#define MI_RPC_BO_END_OFFSET_BYTES  (MI_RPC_BO_SIZE / 2)

/******************************************************************************/

static bool
brw_is_perf_query_ready(struct gl_context *ctx,
                        struct gl_perf_query_object *o);

static void
dump_perf_query_callback(GLuint id, void *query_void, void *brw_void)
{
   struct gl_context *ctx = brw_void;
   struct gl_perf_query_object *o = query_void;
   struct brw_perf_query_object *obj = query_void;

   switch (obj->query->kind) {
   case OA_COUNTERS:
      DBG("%4d: %-6s %-8s BO: %-4s OA data: %-10s %-15s\n",
          id,
          o->Used ? "Dirty," : "New,",
          o->Active ? "Active," : (o->Ready ? "Ready," : "Pending,"),
          obj->oa.bo ? "yes," : "no,",
          brw_is_perf_query_ready(ctx, o) ? "ready," : "not ready,",
          obj->oa.results_accumulated ? "accumulated" : "not accumulated");
      break;
   case PIPELINE_STATS:
      DBG("%4d: %-6s %-8s BO: %-4s\n",
          id,
          o->Used ? "Dirty," : "New,",
          o->Active ? "Active," : (o->Ready ? "Ready," : "Pending,"),
          obj->pipeline_stats.bo ? "yes" : "no");
      break;
   }
}

static void
dump_perf_queries(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->ctx;
   DBG("Queries: (Open queries = %d, OA users = %d)\n",
       brw->perfquery.n_active_oa_queries, brw->perfquery.n_oa_users);
   _mesa_HashWalk(ctx->PerfQuery.Objects, dump_perf_query_callback, brw);
}

/******************************************************************************/

static struct brw_oa_sample_buf *
get_free_sample_buf(struct brw_context *brw)
{
   struct exec_node *node = exec_list_pop_head(&brw->perfquery.free_sample_buffers);
   struct brw_oa_sample_buf *buf;

   if (node)
      buf = exec_node_data(struct brw_oa_sample_buf, node, link);
   else {
      buf = ralloc_size(brw, sizeof(*buf));

      exec_node_init(&buf->link);
      buf->refcount = 0;
      buf->len = 0;
   }

   return buf;
}

static void
reap_old_sample_buffers(struct brw_context *brw)
{
   struct exec_node *tail_node =
      exec_list_get_tail(&brw->perfquery.sample_buffers);
   struct brw_oa_sample_buf *tail_buf =
      exec_node_data(struct brw_oa_sample_buf, tail_node, link);

   /* Remove all old, unreferenced sample buffers walking forward from
    * the head of the list, except always leave at least one node in
    * the list so we always have a node to reference when we Begin
    * a new query.
    */
   foreach_list_typed_safe(struct brw_oa_sample_buf, buf, link,
                           &brw->perfquery.sample_buffers)
   {
      if (buf->refcount == 0 && buf != tail_buf) {
         exec_node_remove(&buf->link);
         exec_list_push_head(&brw->perfquery.free_sample_buffers, &buf->link);
      } else
         return;
   }
}

static void
free_sample_bufs(struct brw_context *brw)
{
   foreach_list_typed_safe(struct brw_oa_sample_buf, buf, link,
                           &brw->perfquery.free_sample_buffers)
      ralloc_free(buf);

   exec_list_make_empty(&brw->perfquery.free_sample_buffers);
}

/******************************************************************************/

/**
 * Driver hook for glGetPerfQueryInfoINTEL().
 */
static void
brw_get_perf_query_info(struct gl_context *ctx,
                        unsigned query_index,
                        const char **name,
                        GLuint *data_size,
                        GLuint *n_counters,
                        GLuint *n_active)
{
   struct brw_context *brw = brw_context(ctx);
   const struct brw_perf_query_info *query =
      &brw->perfquery.queries[query_index];

   *name = query->name;
   *data_size = query->data_size;
   *n_counters = query->n_counters;

   switch (query->kind) {
   case OA_COUNTERS:
      *n_active = brw->perfquery.n_active_oa_queries;
      break;

   case PIPELINE_STATS:
      *n_active = brw->perfquery.n_active_pipeline_stats_queries;
      break;
   }
}

/**
 * Driver hook for glGetPerfCounterInfoINTEL().
 */
static void
brw_get_perf_counter_info(struct gl_context *ctx,
                          unsigned query_index,
                          unsigned counter_index,
                          const char **name,
                          const char **desc,
                          GLuint *offset,
                          GLuint *data_size,
                          GLuint *type_enum,
                          GLuint *data_type_enum,
                          GLuint64 *raw_max)
{
   struct brw_context *brw = brw_context(ctx);
   const struct brw_perf_query_info *query =
      &brw->perfquery.queries[query_index];
   const struct brw_perf_query_counter *counter =
      &query->counters[counter_index];

   *name = counter->name;
   *desc = counter->desc;
   *offset = counter->offset;
   *data_size = counter->size;
   *type_enum = counter->type;
   *data_type_enum = counter->data_type;
   *raw_max = counter->raw_max;
}

/******************************************************************************/

/**
 * Emit MI_STORE_REGISTER_MEM commands to capture all of the
 * pipeline statistics for the performance query object.
 */
static void
snapshot_statistics_registers(struct brw_context *brw,
                              struct brw_perf_query_object *obj,
                              uint32_t offset_in_bytes)
{
   const struct brw_perf_query_info *query = obj->query;
   const int n_counters = query->n_counters;

   for (int i = 0; i < n_counters; i++) {
      const struct brw_perf_query_counter *counter = &query->counters[i];

      assert(counter->data_type == GL_PERFQUERY_COUNTER_DATA_UINT64_INTEL);

      brw_store_register_mem64(brw, obj->pipeline_stats.bo,
                               counter->pipeline_stat.reg,
                               offset_in_bytes + i * sizeof(uint64_t));
   }
}

/**
 * Emit an MI_REPORT_PERF_COUNT command packet.
 *
 * This asks the GPU to write a report of the current OA counter
 * values into @bo at the given offset and containing the given
 * @report_id which we can cross-reference when parsing the report.
 */
static void
emit_mi_report_perf_count(struct brw_context *brw,
                          struct brw_bo *bo,
                          uint32_t offset_in_bytes,
                          uint32_t report_id)
{
   assert(offset_in_bytes % 64 == 0);

   BEGIN_BATCH(3);
   OUT_BATCH(GEN6_MI_REPORT_PERF_COUNT);
   OUT_RELOC(bo, I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
             offset_in_bytes);
   OUT_BATCH(report_id);
   ADVANCE_BATCH();
}

/**
 * Add a query to the global list of "unaccumulated queries."
 *
 * Queries are tracked here until all the associated OA reports have
 * been accumulated via accumulate_oa_reports() after the end
 * MI_REPORT_PERF_COUNT has landed in query->oa.bo.
 */
static void
add_to_unaccumulated_query_list(struct brw_context *brw,
                                struct brw_perf_query_object *obj)
{
   if (brw->perfquery.unaccumulated_elements >=
       brw->perfquery.unaccumulated_array_size)
   {
      brw->perfquery.unaccumulated_array_size *= 1.5;
      brw->perfquery.unaccumulated =
         reralloc(brw, brw->perfquery.unaccumulated,
                  struct brw_perf_query_object *,
                  brw->perfquery.unaccumulated_array_size);
   }

   brw->perfquery.unaccumulated[brw->perfquery.unaccumulated_elements++] = obj;
}

/**
 * Remove a query from the global list of unaccumulated queries once
 * after successfully accumulating the OA reports associated with the
 * query in accumulate_oa_reports() or when discarding unwanted query
 * results.
 */
static void
drop_from_unaccumulated_query_list(struct brw_context *brw,
                                   struct brw_perf_query_object *obj)
{
   for (int i = 0; i < brw->perfquery.unaccumulated_elements; i++) {
      if (brw->perfquery.unaccumulated[i] == obj) {
         int last_elt = --brw->perfquery.unaccumulated_elements;

         if (i == last_elt)
            brw->perfquery.unaccumulated[i] = NULL;
         else {
            brw->perfquery.unaccumulated[i] =
               brw->perfquery.unaccumulated[last_elt];
         }

         break;
      }
   }

   /* Drop our samples_head reference so that associated periodic
    * sample data buffers can potentially be reaped if they aren't
    * referenced by any other queries...
    */

   struct brw_oa_sample_buf *buf =
      exec_node_data(struct brw_oa_sample_buf, obj->oa.samples_head, link);

   assert(buf->refcount > 0);
   buf->refcount--;

   obj->oa.samples_head = NULL;

   reap_old_sample_buffers(brw);
}

static uint64_t
timebase_scale(struct brw_context *brw, uint32_t u32_time_delta)
{
   uint64_t tmp = ((uint64_t)u32_time_delta) * 1000000000ull;

   return tmp ? tmp / brw->perfquery.sys_vars.timestamp_frequency : 0;
}

static void
accumulate_uint32(const uint32_t *report0,
                  const uint32_t *report1,
                  uint64_t *accumulator)
{
   *accumulator += (uint32_t)(*report1 - *report0);
}

/**
 * Given pointers to starting and ending OA snapshots, add the deltas for each
 * counter to the results.
 */
static void
add_deltas(struct brw_context *brw,
           struct brw_perf_query_object *obj,
           const uint32_t *start,
           const uint32_t *end)
{
   const struct brw_perf_query_info *query = obj->query;
   uint64_t *accumulator = obj->oa.accumulator;
   int i;

   switch (query->oa_format) {
   case I915_OA_FORMAT_A45_B8_C8:
      accumulate_uint32(start + 1, end + 1, accumulator); /* timestamp */

      for (i = 0; i < 61; i++)
         accumulate_uint32(start + 3 + i, end + 3 + i, accumulator + 1 + i);

      break;
   default:
      unreachable("Can't accumulate OA counters in unknown format");
   }
}

static bool
inc_n_oa_users(struct brw_context *brw)
{
   if (brw->perfquery.n_oa_users == 0 &&
       drmIoctl(brw->perfquery.oa_stream_fd,
                I915_PERF_IOCTL_ENABLE, 0) < 0)
   {
      return false;
   }
   ++brw->perfquery.n_oa_users;

   return true;
}

static void
dec_n_oa_users(struct brw_context *brw)
{
   /* Disabling the i915 perf stream will effectively disable the OA
    * counters.  Note it's important to be sure there are no outstanding
    * MI_RPC commands at this point since they could stall the CS
    * indefinitely once OACONTROL is disabled.
    */
   --brw->perfquery.n_oa_users;
   if (brw->perfquery.n_oa_users == 0 &&
       drmIoctl(brw->perfquery.oa_stream_fd, I915_PERF_IOCTL_DISABLE, 0) < 0)
   {
      DBG("WARNING: Error disabling i915 perf stream: %m\n");
   }
}

/* In general if we see anything spurious while accumulating results,
 * we don't try and continue accumulating the current query, hoping
 * for the best, we scrap anything outstanding, and then hope for the
 * best with new queries.
 */
static void
discard_all_queries(struct brw_context *brw)
{
   while (brw->perfquery.unaccumulated_elements) {
      struct brw_perf_query_object *obj = brw->perfquery.unaccumulated[0];

      obj->oa.results_accumulated = true;
      drop_from_unaccumulated_query_list(brw, brw->perfquery.unaccumulated[0]);

      dec_n_oa_users(brw);
   }
}

static bool
read_oa_samples(struct brw_context *brw)
{
   while (1) {
      struct brw_oa_sample_buf *buf = get_free_sample_buf(brw);
      int len;

      while ((len = read(brw->perfquery.oa_stream_fd, buf->buf,
                         sizeof(buf->buf))) < 0 && errno == EINTR)
         ;

      if (len <= 0) {
         exec_list_push_tail(&brw->perfquery.free_sample_buffers, &buf->link);

         if (len < 0) {
            if (errno == EAGAIN)
               return true;
            else {
               DBG("Error reading i915 perf samples: %m\n");
               return false;
            }
         } else {
            DBG("Spurious EOF reading i915 perf samples\n");
            return false;
         }
      }

      buf->len = len;
      exec_list_push_tail(&brw->perfquery.sample_buffers, &buf->link);
   }

   unreachable("not reached");
   return false;
}

/**
 * Accumulate raw OA counter values based on deltas between pairs
 * of OA reports.
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
 * overflows of a counter between snapshots.
 */
static void
accumulate_oa_reports(struct brw_context *brw,
                      struct brw_perf_query_object *obj)
{
   struct gl_perf_query_object *o = &obj->base;
   uint32_t *query_buffer;
   uint32_t *start;
   uint32_t *last;
   uint32_t *end;
   struct exec_node *first_samples_node;

   assert(o->Ready);

   /* Collect the latest periodic OA reports from i915 perf */
   if (!read_oa_samples(brw))
      goto error;

   brw_bo_map(brw, obj->oa.bo, false);
   query_buffer = obj->oa.bo->virtual;

   start = last = query_buffer;
   end = query_buffer + (MI_RPC_BO_END_OFFSET_BYTES / sizeof(uint32_t));

   if (start[0] != obj->oa.begin_report_id) {
      DBG("Spurious start report id=%"PRIu32"\n", start[0]);
      goto error;
   }
   if (end[0] != (obj->oa.begin_report_id + 1)) {
      DBG("Spurious end report id=%"PRIu32"\n", end[0]);
      goto error;
   }

   /* See if we have any periodic reports to accumulate too... */

   /* N.B. The oa.samples_head was set when the query began and
    * pointed to the tail of the brw->perfquery.sample_buffers list at
    * the time the query started. Since the buffer existed before the
    * first MI_REPORT_PERF_COUNT command was emitted we therefore know
    * that no data in this particular node's buffer can possibly be
    * associated with the query - so skip ahead one...
    */
   first_samples_node = obj->oa.samples_head->next;

   foreach_list_typed_from(struct brw_oa_sample_buf, buf, link,
                           &brw->perfquery.sample_buffers,
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

            /* Ignore reports that come before the start marker.
             * (Note: takes care to allow overflow of 32bit timestamps)
             */
            if (timebase_scale(brw, report[1] - start[1]) > 5000000000)
               continue;

            /* Ignore reports that come after the end marker.
             * (Note: takes care to allow overflow of 32bit timestamps)
             */
            if (timebase_scale(brw, report[1] - end[1]) <= 5000000000)
               goto end;

            add_deltas(brw, obj, last, report);

            last = report;

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

   add_deltas(brw, obj, last, end);

   DBG("Marking %d accumulated - results gathered\n", o->Id);

   brw_bo_unmap(obj->oa.bo);
   obj->oa.results_accumulated = true;
   drop_from_unaccumulated_query_list(brw, obj);
   dec_n_oa_users(brw);

   return;

error:

   brw_bo_unmap(obj->oa.bo);
   discard_all_queries(brw);
}

/******************************************************************************/

static bool
open_i915_perf_oa_stream(struct brw_context *brw,
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
      .properties_ptr = (uint64_t)properties
   };
   int fd = drmIoctl(drm_fd, DRM_IOCTL_I915_PERF_OPEN, &param);
   if (fd == -1) {
      DBG("Error opening i915 perf OA stream: %m\n");
      return false;
   }

   brw->perfquery.oa_stream_fd = fd;

   brw->perfquery.current_oa_metrics_set_id = metrics_set_id;
   brw->perfquery.current_oa_format = report_format;

   return true;
}

static void
close_perf(struct brw_context *brw)
{
   if (brw->perfquery.oa_stream_fd != -1) {
      close(brw->perfquery.oa_stream_fd);
      brw->perfquery.oa_stream_fd = -1;
   }
}

/**
 * Driver hook for glBeginPerfQueryINTEL().
 */
static bool
brw_begin_perf_query(struct gl_context *ctx,
                     struct gl_perf_query_object *o)
{
   struct brw_context *brw = brw_context(ctx);
   struct brw_perf_query_object *obj = brw_perf_query(o);
   const struct brw_perf_query_info *query = obj->query;

   /* We can assume the frontend hides mistaken attempts to Begin a
    * query object multiple times before its End. Similarly if an
    * application reuses a query object before results have arrived
    * the frontend will wait for prior results so we don't need
    * to support abandoning in-flight results.
    */
   assert(!o->Active);
   assert(!o->Used || o->Ready); /* no in-flight query to worry about */

   DBG("Begin(%d)\n", o->Id);

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
   brw_emit_mi_flush(brw);

   switch (query->kind) {
   case OA_COUNTERS:

      /* Opening an i915 perf stream implies exclusive access to the OA unit
       * which will generate counter reports for a specific counter set with a
       * specific layout/format so we can't begin any OA based queries that
       * require a different counter set or format unless we get an opportunity
       * to close the stream and open a new one...
       */
      if (brw->perfquery.oa_stream_fd != -1 &&
          brw->perfquery.current_oa_metrics_set_id !=
          query->oa_metrics_set_id) {

         if (brw->perfquery.n_oa_users != 0)
            return false;
         else
            close_perf(brw);
      }

      /* If the OA counters aren't already on, enable them. */
      if (brw->perfquery.oa_stream_fd == -1) {
         __DRIscreen *screen = brw->screen->driScrnPriv;
         int period_exponent;

         /* The timestamp for HSW+ increments every 80ns
          *
          * The period_exponent gives a sampling period as follows:
          *   sample_period = 80ns * 2^(period_exponent + 1)
          *
          * The overflow period for Haswell can be calculated as:
          *
          * 2^32 / (n_eus * max_gen_freq * 2)
          * (E.g. 40 EUs @ 1GHz = ~53ms)
          *
          * We currently sample every 42 milliseconds...
          */
         period_exponent = 18;

         if (!open_i915_perf_oa_stream(brw,
                                       query->oa_metrics_set_id,
                                       query->oa_format,
                                       period_exponent,
                                       screen->fd, /* drm fd */
                                       brw->hw_ctx))
            return false;
      } else {
         assert(brw->perfquery.current_oa_metrics_set_id ==
                query->oa_metrics_set_id &&
                brw->perfquery.current_oa_format ==
                query->oa_format);
      }

      if (!inc_n_oa_users(brw)) {
         DBG("WARNING: Error enabling i915 perf stream: %m\n");
         return false;
      }

      if (obj->oa.bo) {
         brw_bo_unreference(obj->oa.bo);
         obj->oa.bo = NULL;
      }

      obj->oa.bo =
         brw_bo_alloc(brw->bufmgr, "perf. query OA MI_RPC bo",
                      MI_RPC_BO_SIZE, 64);
#ifdef DEBUG
      /* Pre-filling the BO helps debug whether writes landed. */
      brw_bo_map(brw, obj->oa.bo, true);
      memset((char *) obj->oa.bo->virtual, 0x80, MI_RPC_BO_SIZE);
      brw_bo_unmap(obj->oa.bo);
#endif

      obj->oa.begin_report_id = brw->perfquery.next_query_start_report_id;
      brw->perfquery.next_query_start_report_id += 2;

      /* Take a starting OA counter snapshot. */
      emit_mi_report_perf_count(brw, obj->oa.bo, 0,
                                obj->oa.begin_report_id);
      ++brw->perfquery.n_active_oa_queries;

      /* No already-buffered samples can possibly be associated with this query
       * so create a marker within the list of sample buffers enabling us to
       * easily ignore earlier samples when processing this query after
       * completion.
       */
      assert(!exec_list_is_empty(&brw->perfquery.sample_buffers));
      obj->oa.samples_head = exec_list_get_tail(&brw->perfquery.sample_buffers);

      struct brw_oa_sample_buf *buf =
         exec_node_data(struct brw_oa_sample_buf, obj->oa.samples_head, link);

      /* This reference will ensure that future/following sample
       * buffers (that may relate to this query) can't be freed until
       * this drops to zero.
       */
      buf->refcount++;

      memset(obj->oa.accumulator, 0, sizeof(obj->oa.accumulator));
      obj->oa.results_accumulated = false;

      add_to_unaccumulated_query_list(brw, obj);
      break;

   case PIPELINE_STATS:
      if (obj->pipeline_stats.bo) {
         brw_bo_unreference(obj->pipeline_stats.bo);
         obj->pipeline_stats.bo = NULL;
      }

      obj->pipeline_stats.bo =
         brw_bo_alloc(brw->bufmgr, "perf. query pipeline stats bo",
                            STATS_BO_SIZE, 64);

      /* Take starting snapshots. */
      snapshot_statistics_registers(brw, obj, 0);

      ++brw->perfquery.n_active_pipeline_stats_queries;
      break;
   }

   if (INTEL_DEBUG & DEBUG_PERFMON)
      dump_perf_queries(brw);

   return true;
}

/**
 * Driver hook for glEndPerfQueryINTEL().
 */
static void
brw_end_perf_query(struct gl_context *ctx,
                     struct gl_perf_query_object *o)
{
   struct brw_context *brw = brw_context(ctx);
   struct brw_perf_query_object *obj = brw_perf_query(o);

   DBG("End(%d)\n", o->Id);

   /* Ensure that the work associated with the queried commands will have
    * finished before taking our query end counter readings.
    *
    * For more details see comment in brw_begin_perf_query for
    * corresponding flush.
    */
   brw_emit_mi_flush(brw);

   switch (obj->query->kind) {
   case OA_COUNTERS:

      /* NB: It's possible that the query will have already been marked
       * as 'accumulated' if an error was seen while reading samples
       * from perf. In this case we mustn't try and emit a closing
       * MI_RPC command in case the OA unit has already been disabled
       */
      if (!obj->oa.results_accumulated) {
         /* Take an ending OA counter snapshot. */
         emit_mi_report_perf_count(brw, obj->oa.bo,
                                   MI_RPC_BO_END_OFFSET_BYTES,
                                   obj->oa.begin_report_id + 1);
      }

      --brw->perfquery.n_active_oa_queries;

      /* NB: even though the query has now ended, it can't be accumulated
       * until the end MI_REPORT_PERF_COUNT snapshot has been written
       * to query->oa.bo
       */
      break;

   case PIPELINE_STATS:
      snapshot_statistics_registers(brw, obj,
                                    STATS_BO_END_OFFSET_BYTES);
      --brw->perfquery.n_active_pipeline_stats_queries;
      break;
   }
}

static void
brw_wait_perf_query(struct gl_context *ctx, struct gl_perf_query_object *o)
{
   struct brw_context *brw = brw_context(ctx);
   struct brw_perf_query_object *obj = brw_perf_query(o);
   struct brw_bo *bo = NULL;

   assert(!o->Ready);

   switch (obj->query->kind) {
   case OA_COUNTERS:
      bo = obj->oa.bo;
      break;

   case PIPELINE_STATS:
      bo = obj->pipeline_stats.bo;
      break;
   }

   if (bo == NULL)
      return;

   /* If the current batch references our results bo then we need to
    * flush first...
    */
   if (brw_batch_references(&brw->batch, bo))
      intel_batchbuffer_flush(brw);

   brw_bo_wait_rendering(brw, bo);
}

static bool
brw_is_perf_query_ready(struct gl_context *ctx,
                        struct gl_perf_query_object *o)
{
   struct brw_context *brw = brw_context(ctx);
   struct brw_perf_query_object *obj = brw_perf_query(o);

   if (o->Ready)
      return true;

   switch (obj->query->kind) {
   case OA_COUNTERS:
      return (obj->oa.results_accumulated ||
              (obj->oa.bo &&
               !brw_batch_references(&brw->batch, obj->oa.bo) &&
               !brw_bo_busy(obj->oa.bo)));

   case PIPELINE_STATS:
      return (obj->pipeline_stats.bo &&
              !brw_batch_references(&brw->batch, obj->pipeline_stats.bo) &&
              !brw_bo_busy(obj->pipeline_stats.bo));
   }

   unreachable("missing ready check for unknown query kind");
   return false;
}

static int
get_oa_counter_data(struct brw_context *brw,
                    struct brw_perf_query_object *obj,
                    size_t data_size,
                    uint8_t *data)
{
   const struct brw_perf_query_info *query = obj->query;
   int n_counters = query->n_counters;
   int written = 0;

   if (!obj->oa.results_accumulated) {
      accumulate_oa_reports(brw, obj);
      assert(obj->oa.results_accumulated);
   }

   for (int i = 0; i < n_counters; i++) {
      const struct brw_perf_query_counter *counter = &query->counters[i];
      uint64_t *out_uint64;
      float *out_float;

      if (counter->size) {
         switch (counter->data_type) {
         case GL_PERFQUERY_COUNTER_DATA_UINT64_INTEL:
            out_uint64 = (uint64_t *)(data + counter->offset);
            *out_uint64 = counter->oa_counter_read_uint64(brw, query,
                                                          obj->oa.accumulator);
            break;
         case GL_PERFQUERY_COUNTER_DATA_FLOAT_INTEL:
            out_float = (float *)(data + counter->offset);
            *out_float = counter->oa_counter_read_float(brw, query,
                                                        obj->oa.accumulator);
            break;
         default:
            /* So far we aren't using uint32, double or bool32... */
            unreachable("unexpected counter data type");
         }
         written = counter->offset + counter->size;
      }
   }

   return written;
}

static int
get_pipeline_stats_data(struct brw_context *brw,
                        struct brw_perf_query_object *obj,
                        size_t data_size,
                        uint8_t *data)

{
   const struct brw_perf_query_info *query = obj->query;
   int n_counters = obj->query->n_counters;
   uint8_t *p = data;

   brw_bo_map(brw, obj->pipeline_stats.bo, false);
   uint64_t *start = obj->pipeline_stats.bo->virtual;
   uint64_t *end = start + (STATS_BO_END_OFFSET_BYTES / sizeof(uint64_t));

   for (int i = 0; i < n_counters; i++) {
      const struct brw_perf_query_counter *counter = &query->counters[i];
      uint64_t value = end[i] - start[i];

      if (counter->pipeline_stat.numerator !=
          counter->pipeline_stat.denominator) {
         value *= counter->pipeline_stat.numerator;
         value /= counter->pipeline_stat.denominator;
      }

      *((uint64_t *)p) = value;
      p += 8;
   }

   brw_bo_unmap(obj->pipeline_stats.bo);

   return p - data;
}

/**
 * Driver hook for glGetPerfQueryDataINTEL().
 */
static void
brw_get_perf_query_data(struct gl_context *ctx,
                        struct gl_perf_query_object *o,
                        GLsizei data_size,
                        GLuint *data,
                        GLuint *bytes_written)
{
   struct brw_context *brw = brw_context(ctx);
   struct brw_perf_query_object *obj = brw_perf_query(o);
   int written = 0;

   assert(brw_is_perf_query_ready(ctx, o));

   DBG("GetData(%d)\n", o->Id);

   if (INTEL_DEBUG & DEBUG_PERFMON)
      dump_perf_queries(brw);

   /* We expect that the frontend only calls this hook when it knows
    * that results are available.
    */
   assert(o->Ready);

   switch (obj->query->kind) {
   case OA_COUNTERS:
      written = get_oa_counter_data(brw, obj, data_size, (uint8_t *)data);
      break;

   case PIPELINE_STATS:
      written = get_pipeline_stats_data(brw, obj, data_size, (uint8_t *)data);
      break;
   }

   if (bytes_written)
      *bytes_written = written;
}

static struct gl_perf_query_object *
brw_new_perf_query_object(struct gl_context *ctx, unsigned query_index)
{
   struct brw_context *brw = brw_context(ctx);
   const struct brw_perf_query_info *query =
      &brw->perfquery.queries[query_index];
   struct brw_perf_query_object *obj =
      calloc(1, sizeof(struct brw_perf_query_object));

   if (!obj)
      return NULL;

   obj->query = query;

   brw->perfquery.n_query_instances++;

   return &obj->base;
}

/**
 * Driver hook for glDeletePerfQueryINTEL().
 */
static void
brw_delete_perf_query(struct gl_context *ctx,
                      struct gl_perf_query_object *o)
{
   struct brw_context *brw = brw_context(ctx);
   struct brw_perf_query_object *obj = brw_perf_query(o);

   /* We can assume that the frontend waits for a query to complete
    * before ever calling into here, so we don't have to worry about
    * deleting an in-flight query object.
    */
   assert(!o->Active);
   assert(!o->Used || o->Ready);

   DBG("Delete(%d)\n", o->Id);

   switch (obj->query->kind) {
   case OA_COUNTERS:
      if (obj->oa.bo) {
         if (!obj->oa.results_accumulated) {
            drop_from_unaccumulated_query_list(brw, obj);
            dec_n_oa_users(brw);
         }

         brw_bo_unreference(obj->oa.bo);
         obj->oa.bo = NULL;
      }

      obj->oa.results_accumulated = false;
      break;

   case PIPELINE_STATS:
      if (obj->pipeline_stats.bo) {
         brw_bo_unreference(obj->pipeline_stats.bo);
         obj->pipeline_stats.bo = NULL;
      }
      break;
   }

   free(obj);

   /* As an indication that the INTEL_performance_query extension is no
    * longer in use, it's a good time to free our cache of sample
    * buffers and close any current i915-perf stream.
    */
   if (--brw->perfquery.n_query_instances == 0) {
      free_sample_bufs(brw);
      close_perf(brw);
   }
}

/******************************************************************************/

static struct brw_perf_query_info *
append_query_info(struct brw_context *brw)
{
   brw->perfquery.queries =
      reralloc(brw, brw->perfquery.queries,
               struct brw_perf_query_info, ++brw->perfquery.n_queries);

   return &brw->perfquery.queries[brw->perfquery.n_queries - 1];
}

static void
add_stat_reg(struct brw_perf_query_info *query,
             uint32_t reg,
             uint32_t numerator,
             uint32_t denominator,
             const char *name,
             const char *description)
{
   struct brw_perf_query_counter *counter;

   assert(query->n_counters < MAX_STAT_COUNTERS);

   counter = &query->counters[query->n_counters];
   counter->name = name;
   counter->desc = description;
   counter->type = GL_PERFQUERY_COUNTER_RAW_INTEL;
   counter->data_type = GL_PERFQUERY_COUNTER_DATA_UINT64_INTEL;
   counter->size = sizeof(uint64_t);
   counter->offset = sizeof(uint64_t) * query->n_counters;
   counter->pipeline_stat.reg = reg;
   counter->pipeline_stat.numerator = numerator;
   counter->pipeline_stat.denominator = denominator;

   query->n_counters++;
}

static void
add_basic_stat_reg(struct brw_perf_query_info *query,
                   uint32_t reg, const char *name)
{
   add_stat_reg(query, reg, 1, 1, name, name);
}

static void
init_pipeline_statistic_query_registers(struct brw_context *brw)
{
   struct brw_perf_query_info *query = append_query_info(brw);

   query->kind = PIPELINE_STATS;
   query->name = "Pipeline Statistics Registers";
   query->n_counters = 0;
   query->counters =
      rzalloc_array(brw, struct brw_perf_query_counter, MAX_STAT_COUNTERS);

   add_basic_stat_reg(query, IA_VERTICES_COUNT,
                      "N vertices submitted");
   add_basic_stat_reg(query, IA_PRIMITIVES_COUNT,
                      "N primitives submitted");
   add_basic_stat_reg(query, VS_INVOCATION_COUNT,
                      "N vertex shader invocations");

   if (brw->gen == 6) {
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

   if (brw->is_haswell || brw->gen == 8)
      add_stat_reg(query, PS_INVOCATION_COUNT, 1, 4,
                   "N fragment shader invocations",
                   "N fragment shader invocations");
   else
      add_basic_stat_reg(query, PS_INVOCATION_COUNT,
                         "N fragment shader invocations");

   add_basic_stat_reg(query, PS_DEPTH_COUNT, "N z-pass fragments");

   if (brw->gen >= 7)
      add_basic_stat_reg(query, CS_INVOCATION_COUNT,
                         "N compute shader invocations");

   query->data_size = sizeof(uint64_t) * query->n_counters;
}

static bool
read_file_uint64(const char *file, uint64_t *val)
{
    char buf[32];
    int fd, n;

    fd = open(file, 0);
    if (fd < 0)
	return false;
    n = read(fd, buf, sizeof (buf) - 1);
    close(fd);
    if (n < 0)
	return false;

    buf[n] = '\0';
    *val = strtoull(buf, NULL, 0);

    return true;
}

static void
enumerate_sysfs_metrics(struct brw_context *brw, const char *sysfs_dev_dir)
{
   char buf[256];
   DIR *metricsdir = NULL;
   struct dirent *metric_entry;
   int len;

   len = snprintf(buf, sizeof(buf), "%s/metrics", sysfs_dev_dir);
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
      entry = _mesa_hash_table_search(brw->perfquery.oa_metrics_table,
                                      metric_entry->d_name);
      if (entry) {
         struct brw_perf_query_info *query;
         uint64_t id;

         len = snprintf(buf, sizeof(buf), "%s/metrics/%s/id",
                        sysfs_dev_dir, metric_entry->d_name);
         if (len < 0 || len >= sizeof(buf)) {
            DBG("Failed to concatenate path to sysfs metric id file\n");
            continue;
         }

         if (!read_file_uint64(buf, &id)) {
            DBG("Failed to read metric set id from %s: %m", buf);
            continue;
         }

         query = append_query_info(brw);
         *query = *(struct brw_perf_query_info *)entry->data;
         query->oa_metrics_set_id = id;

         DBG("metric set known by mesa: id = %" PRIu64"\n",
             query->oa_metrics_set_id);
      } else
         DBG("metric set not known by mesa (skipping)\n");
   }

   closedir(metricsdir);
}

static bool
read_sysfs_drm_device_file_uint64(struct brw_context *brw,
                                  const char *sysfs_dev_dir,
                                  const char *file,
                                  uint64_t *value)
{
   char buf[512];
   int len;

   len = snprintf(buf, sizeof(buf), "%s/%s", sysfs_dev_dir, file);
   if (len < 0 || len >= sizeof(buf)) {
      DBG("Failed to concatenate sys filename to read u64 from\n");
      return false;
   }

   return read_file_uint64(buf, value);
}

static bool
init_oa_sys_vars(struct brw_context *brw, const char *sysfs_dev_dir)
{
   uint64_t min_freq_mhz = 0, max_freq_mhz = 0;

   if (!read_sysfs_drm_device_file_uint64(brw, sysfs_dev_dir,
                                          "gt_min_freq_mhz",
                                          &min_freq_mhz))
      return false;

   if (!read_sysfs_drm_device_file_uint64(brw, sysfs_dev_dir,
                                          "gt_max_freq_mhz",
                                          &max_freq_mhz))
      return false;

   brw->perfquery.sys_vars.gt_min_freq = min_freq_mhz * 1000000;
   brw->perfquery.sys_vars.gt_max_freq = max_freq_mhz * 1000000;

   if (brw->is_haswell) {
      const struct gen_device_info *info = &brw->screen->devinfo;

      brw->perfquery.sys_vars.timestamp_frequency = 12500000;

      if (info->gt == 1) {
         brw->perfquery.sys_vars.n_eus = 10;
         brw->perfquery.sys_vars.n_eu_slices = 1;
         brw->perfquery.sys_vars.subslice_mask = 0x1;
      } else if (info->gt == 2) {
         brw->perfquery.sys_vars.n_eus = 20;
         brw->perfquery.sys_vars.n_eu_slices = 1;
         brw->perfquery.sys_vars.subslice_mask = 0x3;
      } else if (info->gt == 3) {
         brw->perfquery.sys_vars.n_eus = 40;
         brw->perfquery.sys_vars.n_eu_slices = 2;
         brw->perfquery.sys_vars.subslice_mask = 0xf;
      } else
         unreachable("not reached");

      return true;
   } else
      return false;
}

static bool
get_sysfs_dev_dir(struct brw_context *brw,
                  char *path_buf,
                  int path_buf_len)
{
   __DRIscreen *screen = brw->screen->driScrnPriv;
   struct stat sb;
   int min, maj;
   DIR *drmdir;
   struct dirent *drm_entry;
   int len;

   assert(path_buf);
   assert(path_buf_len);
   path_buf[0] = '\0';

   if (fstat(screen->fd, &sb)) {
      DBG("Failed to stat DRM fd\n");
      return false;
   }

   maj = major(sb.st_rdev);
   min = minor(sb.st_rdev);

   if (!S_ISCHR(sb.st_mode)) {
      DBG("DRM fd is not a character device as expected\n");
      return false;
   }

   len = snprintf(path_buf, path_buf_len,
                  "/sys/dev/char/%d:%d/device/drm", maj, min);
   if (len < 0 || len >= path_buf_len) {
      DBG("Failed to concatenate sysfs path to drm device\n");
      return false;
   }

   drmdir = opendir(path_buf);
   if (!drmdir) {
      DBG("Failed to open %s: %m\n", path_buf);
      return false;
   }

   while ((drm_entry = readdir(drmdir))) {
      if ((drm_entry->d_type == DT_DIR ||
           drm_entry->d_type == DT_LNK) &&
          strncmp(drm_entry->d_name, "card", 4) == 0)
      {
         len = snprintf(path_buf, path_buf_len,
                        "/sys/dev/char/%d:%d/device/drm/%s",
                        maj, min, drm_entry->d_name);
         closedir(drmdir);
         if (len < 0 || len >= path_buf_len)
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

static unsigned
brw_init_perf_query_info(struct gl_context *ctx)
{
   struct brw_context *brw = brw_context(ctx);
   struct stat sb;
   char sysfs_dev_dir[128];

   if (brw->perfquery.n_queries)
      return brw->perfquery.n_queries;

   init_pipeline_statistic_query_registers(brw);

   /* The existence of this sysctl parameter implies the kernel supports
    * the i915 perf interface.
    */
   if (brw->is_haswell &&
       stat("/proc/sys/dev/i915/perf_stream_paranoid", &sb) == 0 &&
       get_sysfs_dev_dir(brw, sysfs_dev_dir, sizeof(sysfs_dev_dir)) &&
       init_oa_sys_vars(brw, sysfs_dev_dir))
   {
      brw->perfquery.oa_metrics_table =
         _mesa_hash_table_create(NULL, _mesa_key_hash_string,
                                 _mesa_key_string_equal);

      /* Index all the metric sets mesa knows about before looking to
       * see what the kernel is advertising.
       */
      brw_oa_register_queries_hsw(brw);

      enumerate_sysfs_metrics(brw, sysfs_dev_dir);
   }

   brw->perfquery.unaccumulated =
      ralloc_array(brw, struct brw_perf_query_object *, 2);
   brw->perfquery.unaccumulated_elements = 0;
   brw->perfquery.unaccumulated_array_size = 2;

   exec_list_make_empty(&brw->perfquery.sample_buffers);
   exec_list_make_empty(&brw->perfquery.free_sample_buffers);

   /* It's convenient to guarantee that this linked list of sample
    * buffers is never empty so we add an empty head so when we
    * Begin an OA query we can always take a reference on a buffer
    * in this list.
    */
   struct brw_oa_sample_buf *buf = get_free_sample_buf(brw);
   exec_list_push_head(&brw->perfquery.sample_buffers, &buf->link);

   brw->perfquery.oa_stream_fd = -1;

   brw->perfquery.next_query_start_report_id = 1000;

   return brw->perfquery.n_queries;
}

void
brw_init_performance_queries(struct brw_context *brw)
{
   struct gl_context *ctx = &brw->ctx;

   ctx->Driver.InitPerfQueryInfo = brw_init_perf_query_info;
   ctx->Driver.GetPerfQueryInfo = brw_get_perf_query_info;
   ctx->Driver.GetPerfCounterInfo = brw_get_perf_counter_info;
   ctx->Driver.NewPerfQueryObject = brw_new_perf_query_object;
   ctx->Driver.DeletePerfQuery = brw_delete_perf_query;
   ctx->Driver.BeginPerfQuery = brw_begin_perf_query;
   ctx->Driver.EndPerfQuery = brw_end_perf_query;
   ctx->Driver.WaitPerfQuery = brw_wait_perf_query;
   ctx->Driver.IsPerfQueryReady = brw_is_perf_query_ready;
   ctx->Driver.GetPerfQueryData = brw_get_perf_query_data;
}
