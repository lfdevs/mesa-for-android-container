/*
 * Copyright © 2023 Intel Corporation
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

#include "xe/anv_batch_chain.h"

#include "anv_private.h"
#include "anv_measure.h"
#include "common/intel_bind_timeline.h"
#include "perf/intel_perf.h"

#include "drm-uapi/xe_drm.h"

#define TYPE_SIGNAL true
#define TYPE_WAIT false

struct drm_xe_sync
vk_sync_to_drm_xe_sync(struct vk_sync *vk_sync, uint64_t value, bool signal)
{
   const struct vk_drm_syncobj *syncobj = vk_sync_as_drm_syncobj(vk_sync);
   assert(syncobj);

   struct drm_xe_sync drm_sync = {
      .type = value ? DRM_XE_SYNC_TYPE_TIMELINE_SYNCOBJ :
                      DRM_XE_SYNC_TYPE_SYNCOBJ,
      .flags = signal ? DRM_XE_SYNC_FLAG_SIGNAL : 0,
      .handle = syncobj->syncobj,
      .timeline_value = value,
   };

   return drm_sync;
}

static VkResult
xe_exec_process_syncs(struct anv_queue *queue,
                      uint32_t wait_count, const struct vk_sync_wait *waits,
                      uint32_t signal_count, const struct vk_sync_signal *signals,
                      uint32_t extra_sync_count, const struct drm_xe_sync *extra_syncs,
                      struct anv_utrace_submit *utrace_submit,
                      bool is_companion_rcs_queue,
                      struct drm_xe_sync **ret, uint32_t *ret_count)
{
   struct anv_device *device = queue->device;
   /* Signal the utrace sync only if it doesn't have a batch. Otherwise the
    * it's the utrace batch that should signal its own sync.
    */
   const bool has_utrace_sync =
      utrace_submit &&
      util_dynarray_num_elements(&utrace_submit->base.batch_bos, struct anv_bo *) == 0;
   const uint32_t num_syncs = wait_count + signal_count + extra_sync_count +
                              (has_utrace_sync ? 1 : 0) +
                              ((queue->sync && !is_companion_rcs_queue) ? 1 : 0) +
                              1 /* vm bind sync */;
   struct drm_xe_sync *xe_syncs = vk_zalloc(&device->vk.alloc,
                                            sizeof(*xe_syncs) * num_syncs, 8,
                                            VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!xe_syncs)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   uint32_t count = 0;

   if (has_utrace_sync) {
      xe_syncs[count++] = vk_sync_to_drm_xe_sync(utrace_submit->base.signal.sync,
                                                 utrace_submit->base.signal.signal_value,
                                                 TYPE_SIGNAL);
   }

   for (uint32_t i = 0; i < wait_count; i++) {
      xe_syncs[count++] = vk_sync_to_drm_xe_sync(waits[i].sync,
                                                 waits[i].wait_value,
                                                 TYPE_WAIT);
   }

   for (uint32_t i = 0; i < signal_count; i++) {
      xe_syncs[count++] = vk_sync_to_drm_xe_sync(signals[i].sync,
                                                 signals[i].signal_value,
                                                 TYPE_SIGNAL);
   }

   for (uint32_t i = 0; i < extra_sync_count; i++)
      xe_syncs[count++] = extra_syncs[i];

   if (queue->sync && !is_companion_rcs_queue)
      xe_syncs[count++] = vk_sync_to_drm_xe_sync(queue->sync, 0, TYPE_SIGNAL);

   /* vm bind sync */
   xe_syncs[count++] = (struct drm_xe_sync) {
      .type = DRM_XE_SYNC_TYPE_TIMELINE_SYNCOBJ,
      .flags = 0 /* TYPE_WAIT */,
      .handle = intel_bind_timeline_get_syncobj(&device->bind_timeline),
      .timeline_value = intel_bind_timeline_get_last_point(&device->bind_timeline),
   };

   assert(count == num_syncs);
   *ret = xe_syncs;
   *ret_count = num_syncs;
   return VK_SUCCESS;
}

static void
xe_exec_print_debug(struct anv_queue *queue, uint32_t cmd_buffer_count,
                    struct anv_cmd_buffer **cmd_buffers, struct anv_query_pool *perf_query_pool,
                    uint32_t perf_query_pass, struct drm_xe_exec *exec)
{
   if (INTEL_DEBUG(DEBUG_SUBMIT))
      fprintf(stderr, "Batch offset=0x%016"PRIx64" on queue %u\n",
              (uint64_t)exec->address, queue->vk.index_in_family);

   anv_cmd_buffer_exec_batch_debug(queue, cmd_buffer_count, cmd_buffers,
                                   perf_query_pool, perf_query_pass);
}

VkResult
xe_queue_exec_async(struct anv_async_submit *submit,
                    uint32_t wait_count,
                    const struct vk_sync_wait *waits,
                    uint32_t signal_count,
                    const struct vk_sync_signal *signals)
{
   struct anv_queue *queue = submit->queue;
   struct anv_device *device = queue->device;
   STACK_ARRAY(struct drm_xe_sync, xe_syncs,
               wait_count + signal_count +
               ((submit->signal.sync != NULL) ? 1 : 0) +
               (queue->sync != NULL ? 1 : 0) +
               + 1);
   uint32_t n_syncs = 0;

   for (uint32_t i = 0; i < wait_count; i++) {
      xe_syncs[n_syncs++] = vk_sync_to_drm_xe_sync(waits[i].sync,
                                                   waits[i].wait_value,
                                                   TYPE_WAIT);
   }
   for (uint32_t i = 0; i < signal_count; i++) {
      xe_syncs[n_syncs++] = vk_sync_to_drm_xe_sync(signals[i].sync,
                                                   signals[i].signal_value,
                                                   TYPE_SIGNAL);
   }
   if (submit->signal.sync) {
      xe_syncs[n_syncs++] = vk_sync_to_drm_xe_sync(submit->signal.sync,
                                                   submit->signal.signal_value,
                                                   TYPE_SIGNAL);
   }
   if (queue->sync)
      xe_syncs[n_syncs++] = vk_sync_to_drm_xe_sync(queue->sync, 0, TYPE_SIGNAL);

   xe_syncs[n_syncs++] = (struct drm_xe_sync) {
      .type = DRM_XE_SYNC_TYPE_TIMELINE_SYNCOBJ,
      .flags = 0 /* TYPE_WAIT */,
      .handle = intel_bind_timeline_get_syncobj(&device->bind_timeline),
      .timeline_value = intel_bind_timeline_get_last_point(&device->bind_timeline),
   };

#ifdef SUPPORT_INTEL_INTEGRATED_GPUS
   if (device->physical->memory.need_flush &&
       anv_bo_needs_host_cache_flush(device->utrace_bo_pool.bo_alloc_flags)) {
      util_dynarray_foreach(&submit->batch_bos, struct anv_bo *, bo)
         intel_flush_range((*bo)->map, (*bo)->size);
   }
#endif

   struct anv_bo *batch_bo =
      *util_dynarray_element(&submit->batch_bos, struct anv_bo *, 0);
   struct drm_xe_exec exec = {
      .exec_queue_id = submit->use_companion_rcs ?
                       queue->companion_rcs_id : queue->exec_queue_id,
      .num_batch_buffer = 1,
      .syncs = (uintptr_t)xe_syncs,
      .num_syncs = n_syncs,
      .address = batch_bo->offset,
   };

   xe_exec_print_debug(queue, 0, NULL, NULL, 0, &exec);

   if (likely(!device->info->no_hw)) {
      if (intel_ioctl(device->fd, DRM_IOCTL_XE_EXEC, &exec))
         return vk_device_set_lost(&device->vk, "anv_xe_queue_exec_locked failed: %m");
   }

   return anv_queue_post_submit(queue, VK_SUCCESS);
}

static VkResult
xe_companion_rcs_queue_exec_locked(struct anv_queue *queue,
                                   struct anv_cmd_buffer *companion_rcs_cmd_buffer,
                                   uint32_t wait_count,
                                   const struct vk_sync_wait *waits)
{
   struct anv_device *device = queue->device;
   VkResult result;

   struct vk_sync_signal companion_sync = {
      .sync = queue->companion_sync,
   };
   struct drm_xe_sync *xe_syncs = NULL;
   uint32_t xe_syncs_count = 0;
   result = xe_exec_process_syncs(queue,
                                  wait_count, waits,
                                  1, &companion_sync,
                                  0, NULL, /* extra_syncs */
                                  NULL /* utrace_submit */,
                                  true /* is_companion_rcs_queue */,
                                  &xe_syncs,
                                  &xe_syncs_count);
   if (result != VK_SUCCESS)
      return result;

   struct drm_xe_exec exec = {
      .exec_queue_id = queue->companion_rcs_id,
      .num_batch_buffer = 1,
      .syncs = (uintptr_t)xe_syncs,
      .num_syncs = xe_syncs_count,
   };

   struct anv_batch_bo *batch_bo =
      list_first_entry(&companion_rcs_cmd_buffer->batch_bos,
                       struct anv_batch_bo, link);
   exec.address = batch_bo->bo->offset;

   anv_measure_submit(companion_rcs_cmd_buffer);
   xe_exec_print_debug(queue, 1, &companion_rcs_cmd_buffer, NULL, 0, &exec);

   if (!device->info->no_hw) {
      if (intel_ioctl(device->fd, DRM_IOCTL_XE_EXEC, &exec))
         result = vk_device_set_lost(&device->vk, "anv_xe_queue_exec_locked failed: %m");
   }
   vk_free(&device->vk.alloc, xe_syncs);

   return result;
}

VkResult
xe_queue_exec_locked(struct anv_queue *queue,
                     uint32_t wait_count,
                     const struct vk_sync_wait *waits,
                     uint32_t cmd_buffer_count,
                     struct anv_cmd_buffer **cmd_buffers,
                     uint32_t signal_count,
                     const struct vk_sync_signal *signals,
                     struct anv_query_pool *perf_query_pool,
                     uint32_t perf_query_pass,
                     struct anv_utrace_submit *utrace_submit)
{
   struct anv_device *device = queue->device;
   VkResult result;

   struct drm_xe_sync *xe_syncs = NULL;
   uint32_t xe_syncs_count = 0;
   result = xe_exec_process_syncs(queue, wait_count, waits,
                                  signal_count, signals,
                                  0, NULL, /* extra_syncs */
                                  utrace_submit,
                                  false, /* is_companion_rcs_queue */
                                  &xe_syncs, &xe_syncs_count);
   if (result != VK_SUCCESS)
      return result;

   /* If there is a utrace submission but no batch, it means there is no
    * commands to run for utrace so ignore the submission.
    */
   if (utrace_submit &&
       util_dynarray_num_elements(&utrace_submit->base.batch_bos,
                                  struct anv_bo *) == 0)
      utrace_submit = NULL;

   struct drm_xe_exec exec = {
      .exec_queue_id = queue->exec_queue_id,
      .num_batch_buffer = 1,
      .syncs = (uintptr_t)xe_syncs,
      .num_syncs = xe_syncs_count,
   };

   if (cmd_buffer_count) {
      if (unlikely(device->physical->measure_device.config)) {
         for (uint32_t i = 0; i < cmd_buffer_count; i++)
            anv_measure_submit(cmd_buffers[i]);
      }

      anv_cmd_buffer_chain_command_buffers(cmd_buffers, cmd_buffer_count);

#ifdef SUPPORT_INTEL_INTEGRATED_GPUS
      if (device->physical->memory.need_flush &&
          anv_bo_needs_host_cache_flush(device->batch_bo_pool.bo_alloc_flags))
         anv_cmd_buffer_clflush(cmd_buffers, cmd_buffer_count);
#endif

      struct anv_cmd_buffer *first_cmd_buffer = cmd_buffers[0];
      struct anv_batch_bo *first_batch_bo = list_first_entry(&first_cmd_buffer->batch_bos,
                                                             struct anv_batch_bo, link);
      exec.address = first_batch_bo->bo->offset;
   } else {
      exec.address = device->trivial_batch_bo->offset;
   }

   xe_exec_print_debug(queue, cmd_buffer_count, cmd_buffers, perf_query_pool,
                       perf_query_pass, &exec);

   if (perf_query_pool && cmd_buffer_count) {
      struct drm_xe_sync xe_syncs[2] = {};
      struct drm_xe_exec perf_query_exec = {
            .exec_queue_id = queue->exec_queue_id,
            .num_batch_buffer = 1,
            .address = perf_query_pool->bo->offset +
                       khr_perf_query_preamble_offset(perf_query_pool, perf_query_pass),
            .num_syncs = 1,
            .syncs = (uintptr_t)xe_syncs,
      };
      assert(perf_query_pass < perf_query_pool->n_passes);
      struct intel_perf_query_info *query_info = perf_query_pool->pass_query[perf_query_pass];

      /* Some performance queries just the pipeline statistic HW, no need for
       * OA in that case, so no need to reconfigure.
       */
      if (!INTEL_DEBUG(DEBUG_NO_OACONFIG) &&
          (query_info->kind == INTEL_PERF_QUERY_TYPE_OA ||
           query_info->kind == INTEL_PERF_QUERY_TYPE_RAW)) {
         int ret = intel_perf_stream_set_metrics_id(device->physical->perf,
                                                    device->fd,
                                                    device->perf_fd,
                                                    queue->exec_queue_id,
                                                    query_info->oa_metrics_set_id,
                                                    &device->perf_timeline);
         if (ret < 0) {
            result = vk_device_set_lost(&device->vk,
                                        "intel_perf_stream_set_metrics_id failed: %s",
                                        strerror(errno));
         }
      }

      /* wait on completion of all vm binds */
      xe_syncs[0].type = DRM_XE_SYNC_TYPE_TIMELINE_SYNCOBJ;
      xe_syncs[0].flags = 0;/* wait */
      xe_syncs[0].handle = intel_bind_timeline_get_syncobj(&device->bind_timeline);
      xe_syncs[0].timeline_value = intel_bind_timeline_get_last_point(&device->bind_timeline);

      /* wait for metric change if supported */
      if (intel_bind_timeline_get_syncobj(&device->perf_timeline)) {
         perf_query_exec.num_syncs++;
         xe_syncs[1].type = DRM_XE_SYNC_TYPE_TIMELINE_SYNCOBJ;
         xe_syncs[1].flags = 0;/* wait */
         xe_syncs[1].handle = intel_bind_timeline_get_syncobj(&device->perf_timeline);
         xe_syncs[1].timeline_value = intel_bind_timeline_get_last_point(&device->perf_timeline);
      }

      if (!device->info->no_hw && result == VK_SUCCESS) {
         if (intel_ioctl(device->fd, DRM_IOCTL_XE_EXEC, &perf_query_exec))
            result = vk_device_set_lost(&device->vk, "perf_query_exec failed: %m");
      }
   }

   if (!device->info->no_hw && result == VK_SUCCESS) {
      if (intel_ioctl(device->fd, DRM_IOCTL_XE_EXEC, &exec))
         result = vk_device_set_lost(&device->vk, "anv_xe_queue_exec_locked failed: %m");
   }
   vk_free(&device->vk.alloc, xe_syncs);

   if (cmd_buffer_count != 0 && cmd_buffers[0]->companion_rcs_cmd_buffer) {
      /* not allowed to chain cmd_buffers with companion_rcs_cmd_buffer  */
      assert(cmd_buffer_count == 1);
      result = xe_companion_rcs_queue_exec_locked(queue,
                                                  cmd_buffers[0]->companion_rcs_cmd_buffer,
                                                  wait_count, waits);
   }

   result = anv_queue_post_submit(queue, result);

   if (result == VK_SUCCESS && utrace_submit) {
      struct vk_sync_signal signal = {
         .sync = utrace_submit->base.signal.sync,
         .signal_value = utrace_submit->base.signal.signal_value,
      };
      result = xe_queue_exec_async(&utrace_submit->base,
                                   0, NULL, 1, &signal);
   }

   return result;
}
