/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_common.h"

#include <stdarg.h>

#include "util/log.h"
#include "util/os_misc.h"
#include "util/u_debug.h"
#include "venus-protocol/vn_protocol_driver_info.h"
#include "vk_enum_to_str.h"

#include "vn_ring.h"

#define VN_RELAX_MIN_BASE_SLEEP_US (160)

static const struct debug_control vn_debug_options[] = {
   /* clang-format off */
   { "init", VN_DEBUG_INIT },
   { "result", VN_DEBUG_RESULT },
   { "vtest", VN_DEBUG_VTEST },
   { "wsi", VN_DEBUG_WSI },
   { "no_abort", VN_DEBUG_NO_ABORT },
   { "log_ctx_info", VN_DEBUG_LOG_CTX_INFO },
   { "cache", VN_DEBUG_CACHE },
   { NULL, 0 },
   /* clang-format on */
};

static const struct debug_control vn_perf_options[] = {
   /* clang-format off */
   { "no_async_set_alloc", VN_PERF_NO_ASYNC_SET_ALLOC },
   { "no_async_buffer_create", VN_PERF_NO_ASYNC_BUFFER_CREATE },
   { "no_async_queue_submit", VN_PERF_NO_ASYNC_QUEUE_SUBMIT },
   { "no_event_feedback", VN_PERF_NO_EVENT_FEEDBACK },
   { "no_fence_feedback", VN_PERF_NO_FENCE_FEEDBACK },
   { "no_memory_suballoc", VN_PERF_NO_MEMORY_SUBALLOC },
   { "no_cmd_batching", VN_PERF_NO_CMD_BATCHING },
   { "no_timeline_sem_feedback", VN_PERF_NO_TIMELINE_SEM_FEEDBACK },
   { NULL, 0 },
   /* clang-format on */
};

struct vn_env vn_env;

static void
vn_env_init_once(void)
{
   vn_env.debug =
      parse_debug_string(os_get_option("VN_DEBUG"), vn_debug_options);
   vn_env.perf =
      parse_debug_string(os_get_option("VN_PERF"), vn_perf_options);
   vn_env.draw_cmd_batch_limit =
      debug_get_num_option("VN_DRAW_CMD_BATCH_LIMIT", UINT32_MAX);
   if (!vn_env.draw_cmd_batch_limit)
      vn_env.draw_cmd_batch_limit = UINT32_MAX;
   vn_env.relax_base_sleep_us = debug_get_num_option(
      "VN_RELAX_BASE_SLEEP_US", VN_RELAX_MIN_BASE_SLEEP_US);
}

void
vn_env_init(void)
{
   static once_flag once = ONCE_FLAG_INIT;
   call_once(&once, vn_env_init_once);

   /* log per VkInstance creation */
   if (VN_DEBUG(INIT)) {
      vn_log(NULL,
             "vn_env is as below:"
             "\n\tdebug = 0x%" PRIx64 ""
             "\n\tperf = 0x%" PRIx64 ""
             "\n\tdraw_cmd_batch_limit = %u"
             "\n\trelax_base_sleep_us = %u",
             vn_env.debug, vn_env.perf, vn_env.draw_cmd_batch_limit,
             vn_env.relax_base_sleep_us);
   }
}

void
vn_trace_init(void)
{
#ifdef ANDROID
   atrace_init();
#else
   util_perfetto_init();
#endif
}

void
vn_log(struct vn_instance *instance, const char *format, ...)
{
   va_list ap;

   va_start(ap, format);
   mesa_log_v(MESA_LOG_DEBUG, "MESA-VIRTIO", format, ap);
   va_end(ap);

   /* instance may be NULL or partially initialized */
}

VkResult
vn_log_result(struct vn_instance *instance,
              VkResult result,
              const char *where)
{
   vn_log(instance, "%s: %s", where, vk_Result_to_str(result));
   return result;
}

uint32_t
vn_extension_get_spec_version(const char *name)
{
   const int32_t index = vn_info_extension_index(name);
   return index >= 0 ? vn_info_extension_get(index)->spec_version : 0;
}

static bool
vn_ring_monitor_acquire(struct vn_ring *ring)
{
   pid_t tid = gettid();
   if (!ring->monitor.threadid && tid != ring->monitor.threadid &&
       mtx_trylock(&ring->monitor.mutex) == thrd_success) {
      /* register as the only waiting thread that monitors the ring. */
      ring->monitor.threadid = tid;
   }
   return tid == ring->monitor.threadid;
}

void
vn_ring_monitor_release(struct vn_ring *ring)
{
   if (gettid() != ring->monitor.threadid)
      return;

   ring->monitor.threadid = 0;
   mtx_unlock(&ring->monitor.mutex);
}

struct vn_relax_state
vn_relax_init(struct vn_ring *ring, const char *reason)
{
   if (ring->monitor.report_period_us) {
#ifndef NDEBUG
      /* ensure minimum check period is greater than maximum renderer
       * reporting period (with margin of safety to ensure no false
       * positives).
       *
       * first_warn_time is pre-calculated based on parameters in vn_relax
       * and must update together.
       */
      const uint32_t first_warn_time = 3481600;
      const uint32_t safety_margin = 250000;
      assert(first_warn_time - safety_margin >=
             ring->monitor.report_period_us);
#endif

      if (vn_ring_monitor_acquire(ring)) {
         ring->monitor.alive = true;
         vn_ring_unset_status_bits(ring, VK_RING_STATUS_ALIVE_BIT_MESA);
      }
   }

   return (struct vn_relax_state){
      .ring = ring,
      .iter = 0,
      .reason = reason,
   };
}

void
vn_relax(struct vn_relax_state *state)
{
   struct vn_ring *ring = state->ring;
   uint32_t *iter = &state->iter;
   const char *reason = state->reason;

   /* Yield for the first 2^busy_wait_order times and then sleep for
    * base_sleep_us microseconds for the same number of times.  After that,
    * keep doubling both sleep length and count.
    * Must also update pre-calculated "first_warn_time" in vn_relax_init().
    */
   const uint32_t busy_wait_order = 8;
   const uint32_t base_sleep_us = vn_env.relax_base_sleep_us;
   const uint32_t warn_order = 12;
   const uint32_t abort_order = 16;

   (*iter)++;
   if (*iter < (1 << busy_wait_order)) {
      thrd_yield();
      return;
   }

   /* warn occasionally if we have slept at least 1.28ms for 2048 times (plus
    * another 2047 shorter sleeps)
    */
   if (unlikely(*iter % (1 << warn_order) == 0)) {
      vn_log(NULL, "stuck in %s wait with iter at %d", reason, *iter);

      const uint32_t status = vn_ring_load_status(ring);
      if (status & VK_RING_STATUS_FATAL_BIT_MESA) {
         vn_log(NULL, "aborting on ring fatal error at iter %d", *iter);
         abort();
      }

      if (ring->monitor.report_period_us) {
         if (vn_ring_monitor_acquire(ring)) {
            ring->monitor.alive = status & VK_RING_STATUS_ALIVE_BIT_MESA;
            vn_ring_unset_status_bits(ring, VK_RING_STATUS_ALIVE_BIT_MESA);
         }

         if (!ring->monitor.alive && !VN_DEBUG(NO_ABORT)) {
            vn_log(NULL, "aborting on expired ring alive status at iter %d",
                   *iter);
            abort();
         }
      }

      if (*iter >= (1 << abort_order) && !VN_DEBUG(NO_ABORT)) {
         vn_log(NULL, "aborting");
         abort();
      }
   }

   const uint32_t shift = util_last_bit(*iter) - busy_wait_order - 1;
   os_time_sleep(base_sleep_us << shift);
}
