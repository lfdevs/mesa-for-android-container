/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#include "tu_queue.h"

#include "tu_buffer.h"
#include "tu_cmd_buffer.h"
#include "tu_dynamic_rendering.h"
#include "tu_image.h"
#include "tu_knl.h"
#include "tu_device.h"

#include "vk_util.h"

static int
tu_get_submitqueue_priority(const struct tu_physical_device *pdevice,
                            VkQueueGlobalPriorityKHR global_priority,
                            enum tu_queue_type type,
                            bool global_priority_query)
{
   if (global_priority_query) {
      VkQueueFamilyGlobalPriorityPropertiesKHR props;
      tu_physical_device_get_global_priority_properties(pdevice, type, &props);

      bool valid = false;
      for (uint32_t i = 0; i < props.priorityCount; i++) {
         if (props.priorities[i] == global_priority) {
            valid = true;
            break;
         }
      }

      if (!valid)
         return -1;
   }

   /* drm/msm requires a priority of 0 */
   if (type == TU_QUEUE_SPARSE)
      return 0;

   /* Valid values are from 0 to (pdevice->submitqueue_priority_count - 1),
    * with 0 being the highest priority.
    *
    * Map vulkan's REALTIME to LOW priority to that range.
    */
   int priority;
   switch (global_priority) {
   case VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR:
      priority = 3;
      break;
   case VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR:
      priority = 2;
      break;
   case VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR:
      priority = 1;
      break;
   case VK_QUEUE_GLOBAL_PRIORITY_REALTIME_KHR:
      priority = 0;
      break;
   default:
      UNREACHABLE("");
      break;
   }
   priority =
      DIV_ROUND_UP((pdevice->submitqueue_priority_count - 1) * priority, 3);

   return priority;
}

static void
submit_add_entries(struct tu_device *dev, void *submit,
                   struct util_dynarray *dump_cmds,
                   struct tu_cs_entry *entries, unsigned num_entries)
{
   tu_submit_add_entries(dev, submit, entries, num_entries);
   if (FD_RD_DUMP(ENABLE)) {
      util_dynarray_append_array(dump_cmds, struct tu_cs_entry, entries,
                                 num_entries);
   }
}

/* Normally, we can just resolve visibility stream patchpoints on the CPU by
 * writing directly to the command stream with the final iova of the allocated
 * BO. However this doesn't work with SIMULTANEOUS_USE command buffers, where
 * the same buffer may be in flight more than once, including within a submit.
 * To handle this we have to update the patchpoints on the GPU. The lifetime
 * of the CS used to write the patchpoints on the GPU is tricky, since if we
 * always allocate a new one for each submit the size could grow infinitely if
 * the command buffer is never freed or reset. Instead this implements a pool
 * of patchpoint CS's per command buffer that reuses finiehed CS's.
 */
static VkResult
get_vis_stream_patchpoint_cs(struct tu_cmd_buffer *cmd,
                             struct tu_cs *cs,
                             struct tu_cs *sub_cs,
                             uint64_t *fence_iova)
{
   /* See below for the commands emitted to the CS. */
   uint32_t cs_size = 5 *
      util_dynarray_num_elements(&cmd->vis_stream_patchpoints,
                                 struct tu_vis_stream_patchpoint) + 4 + 6;

   util_dynarray_foreach (&cmd->vis_stream_cs_bos,
                          struct tu_vis_stream_patchpoint_cs,
                          patchpoint_cs) {
      uint32_t *fence = (uint32_t *)patchpoint_cs->fence_bo.bo->map;
      if (*fence == 1) {
         *fence = 0;
         tu_cs_init_suballoc(cs, cmd->device, &patchpoint_cs->cs_bo);
         tu_cs_begin_sub_stream(cs, cs_size, sub_cs);
         *fence_iova = patchpoint_cs->fence_bo.iova;
         return VK_SUCCESS;
      }
   }

   struct tu_vis_stream_patchpoint_cs patchpoint_cs;

   mtx_lock(&cmd->device->vis_stream_suballocator_mtx);
   VkResult result =
      tu_suballoc_bo_alloc(&patchpoint_cs.cs_bo,
                           &cmd->device->vis_stream_suballocator,
                           cs_size * 4, 4);

   if (result != VK_SUCCESS) {
      mtx_unlock(&cmd->device->vis_stream_suballocator_mtx);
      return result;
   }

   result =
      tu_suballoc_bo_alloc(&patchpoint_cs.fence_bo,
                           &cmd->device->vis_stream_suballocator,
                           4, 4);

   if (result != VK_SUCCESS) {
      tu_suballoc_bo_free(&cmd->device->vis_stream_suballocator,
                          &patchpoint_cs.cs_bo);
      mtx_unlock(&cmd->device->vis_stream_suballocator_mtx);
      return result;
   }

   mtx_unlock(&cmd->device->vis_stream_suballocator_mtx);

   util_dynarray_append(&cmd->vis_stream_cs_bos, patchpoint_cs);

   tu_cs_init_suballoc(cs, cmd->device, &patchpoint_cs.cs_bo);
   tu_cs_begin_sub_stream(cs, cs_size, sub_cs);
   *fence_iova = patchpoint_cs.fence_bo.iova;

   return VK_SUCCESS;
}

static VkResult
resolve_vis_stream_patchpoints(struct tu_queue *queue,
                               void *submit,
                               struct util_dynarray *dump_cmds,
                               struct tu_cmd_buffer **cmd_buffers,
                               uint32_t cmdbuf_count)
{
   struct tu_device *dev = queue->device;

   uint32_t max_size = 0;
   uint32_t rp_count = 0;
   for (unsigned i = 0; i < cmdbuf_count; i++) {
      max_size = MAX2(max_size, cmd_buffers[i]->vsc_size);
      rp_count += cmd_buffers[i]->state.tile_render_pass_count;
   }

   if (max_size == 0)
      return VK_SUCCESS;

   struct tu_bo *bo = NULL;
   VkResult result = VK_SUCCESS;

   /* Note, we want to make the vis stream count at least 1 because an
    * BV_BR_OFFSET of 0 can lead to hangs even if not using visibility
    * streams and therefore should be avoided.
    */
   uint32_t min_vis_stream_count =
      (TU_DEBUG(NO_CONCURRENT_BINNING) || dev->physical_device->info->chip < 7) ?
      1 : MIN2(MAX2(rp_count, 1), TU_MAX_VIS_STREAMS);
   uint32_t vis_stream_count;

   mtx_lock(&dev->vis_stream_mtx);

   if (!dev->vis_stream_bo || max_size > dev->vis_stream_size ||
       min_vis_stream_count > dev->vis_stream_count) {
      dev->vis_stream_count = MAX2(dev->vis_stream_count,
                                   min_vis_stream_count);
      dev->vis_stream_size = MAX2(dev->vis_stream_size, max_size);
      if (dev->vis_stream_bo)
         tu_bo_finish(dev, dev->vis_stream_bo);
      result = tu_bo_init_new(dev, &dev->vk.base, &dev->vis_stream_bo,
                              dev->vis_stream_size * dev->vis_stream_count, 
                              TU_BO_ALLOC_INTERNAL_RESOURCE,
                              "visibility stream");
   }

   bo = dev->vis_stream_bo;
   vis_stream_count = dev->vis_stream_count;

   mtx_unlock(&dev->vis_stream_mtx);

   if (!bo)
      return result;

   /* Attach a reference to the BO to each command buffer involved in the
    * submit.
    */
   for (unsigned i = 0; i < cmdbuf_count; i++) {
      bool has_bo = false;
      util_dynarray_foreach (&cmd_buffers[i]->vis_stream_bos,
                             struct tu_bo *, cmd_bo) {
         if (*cmd_bo == bo) {
            has_bo = true;
            break;
         }
      }

      if (!has_bo) {
         util_dynarray_append(&cmd_buffers[i]->vis_stream_bos,
                              tu_bo_get_ref(bo));
      }
   }

   unsigned render_pass_idx = queue->render_pass_idx;

   for (unsigned i = 0; i < cmdbuf_count; i++) {
      struct tu_cs cs, sub_cs;
      uint64_t fence_iova = 0;
      if (cmd_buffers[i]->usage_flags &
          VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT) {
         result = get_vis_stream_patchpoint_cs(cmd_buffers[i],
                                               &cs, &sub_cs, &fence_iova);
         if (result != VK_SUCCESS)
            return result;
      }

      util_dynarray_foreach (&cmd_buffers[i]->vis_stream_patchpoints,
                             struct tu_vis_stream_patchpoint,
                             patchpoint) {
         unsigned vis_stream_idx =
            (render_pass_idx + patchpoint->render_pass_idx) %
            vis_stream_count;
         uint64_t final_iova =
            bo->iova + vis_stream_idx * max_size + patchpoint->offset;

         if (cmd_buffers[i]->usage_flags &
             VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT) {
            tu_cs_emit_pkt7(&sub_cs, CP_MEM_WRITE, 4);
            tu_cs_emit_qw(&sub_cs, patchpoint->iova);
            tu_cs_emit_qw(&sub_cs, final_iova);
         } else {
            patchpoint->data[0] = final_iova;
            patchpoint->data[1] = final_iova >> 32;
         }
      }

      struct tu_vis_stream_patchpoint *count_patchpoint =
         &cmd_buffers[i]->vis_stream_count_patchpoint;
      if (count_patchpoint->data) {
         if (cmd_buffers[i]->usage_flags &
             VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT) {
            tu_cs_emit_pkt7(&sub_cs, CP_MEM_WRITE, 3);
            tu_cs_emit_qw(&sub_cs, count_patchpoint->iova);
            tu_cs_emit(&sub_cs, vis_stream_count);
         } else {
            count_patchpoint->data[0] = vis_stream_count;
         }
      }

      if (cmd_buffers[i]->usage_flags &
          VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT) {
         tu_cs_emit_pkt7(&sub_cs, CP_WAIT_MEM_WRITES, 0);
         tu_cs_emit_pkt7(&sub_cs, CP_WAIT_FOR_ME, 0);

         /* Signal that this CS is done and can be reused. */
         tu_cs_emit_pkt7(&sub_cs, CP_MEM_WRITE, 3);
         tu_cs_emit_qw(&sub_cs, fence_iova);
         tu_cs_emit(&sub_cs, 1);

         struct tu_cs_entry entry = tu_cs_end_sub_stream(&cs, &sub_cs);
         submit_add_entries(queue->device, submit, dump_cmds, &entry, 1);
      }

      render_pass_idx += cmd_buffers[i]->state.tile_render_pass_count;
   }

   queue->render_pass_idx = render_pass_idx;

   return VK_SUCCESS;
}

static VkResult
resolve_cb_control_patchpoints(struct tu_queue *queue,
                               void *submit,
                               struct util_dynarray *dump_cmds,
                               struct tu_cmd_buffer **cmd_buffers,
                               uint32_t cmdbuf_count)
{
   bool enable_cb = false;
   for (int32_t i = cmdbuf_count - 1; i >= 0; i--) {
      struct tu_cmd_buffer *cmd = cmd_buffers[i];

      /* Simultaneous cmdbufs are not expected to be used for workloads that
       * benefit from CB, so instead of on-GPU patching, just treat them as CB
       * barriers.
       */
      if (cmd_buffers[i]->usage_flags &
          VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT) {
         enable_cb = false;
         continue;
      }

      bool one_time_submit = !!(cmd_buffers[i]->usage_flags &
                                VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
      util_dynarray_foreach_reverse (&cmd->cb_control_points,
                                     struct tu_cb_control_point, info) {
         if (info->type == TU_CB_CONTROL_TYPE_CB_ENABLED) {
            enable_cb = true;
         } else if (info->type == TU_CB_CONTROL_TYPE_BARRIER) {
            enable_cb = false;
         } else if (enable_cb) {
            *info->patchpoint = info->patch_value;
         } else if (!one_time_submit) {
            *info->patchpoint = info->original_value;
         }
      }
   }

   return VK_SUCCESS;
}

static VkResult
queue_submit_sparse(struct vk_queue *_queue, struct vk_queue_submit *vk_submit)
{
   struct tu_queue *queue = list_entry(_queue, struct tu_queue, vk);
   struct tu_device *device = queue->device;

   pthread_mutex_lock(&device->submit_mutex);

   void *submit = tu_submit_create(device);
   if (!submit)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   for (uint32_t i = 0; i < vk_submit->buffer_bind_count; i++) {
      const VkSparseBufferMemoryBindInfo *bind = &vk_submit->buffer_binds[i];
      VK_FROM_HANDLE(tu_buffer, buffer, bind->buffer);

      for (uint32_t j = 0; j < bind->bindCount; j++) {
         const VkSparseMemoryBind *range = &bind->pBinds[j];
         VK_FROM_HANDLE(tu_device_memory, mem, range->memory);

         tu_submit_add_bind(queue->device, submit,
                            &buffer->vma, range->resourceOffset,
                            mem ? mem->bo : NULL,
                            mem ? range->memoryOffset : 0,
                            range->size);
      }
   }

   for (uint32_t i = 0; i < vk_submit->image_bind_count; i++) {
      const VkSparseImageMemoryBindInfo *bind = &vk_submit->image_binds[i];
      VK_FROM_HANDLE(tu_image, image, bind->image);

      for (uint32_t j = 0; j < bind->bindCount; j++)
         tu_bind_sparse_image(device, submit, image, &bind->pBinds[j]);
   }

   for (uint32_t i = 0; i < vk_submit->image_opaque_bind_count; i++) {
      const VkSparseImageOpaqueMemoryBindInfo *bind =
         &vk_submit->image_opaque_binds[i];
      VK_FROM_HANDLE(tu_image, image, bind->image);

      for (uint32_t j = 0; j < bind->bindCount; j++) {
         const VkSparseMemoryBind *range = &bind->pBinds[j];
         VK_FROM_HANDLE(tu_device_memory, mem, range->memory);

         tu_submit_add_bind(queue->device, submit,
                            &image->vma, range->resourceOffset,
                            mem ? mem->bo : NULL,
                            mem ? range->memoryOffset : 0,
                            range->size);
      }
   }

   VkResult result =
      tu_queue_submit(queue, submit, vk_submit->waits, vk_submit->wait_count,
                      vk_submit->signals, vk_submit->signal_count,
                      NULL);

   if (result != VK_SUCCESS) {
      pthread_mutex_unlock(&device->submit_mutex);
      goto out;
   }

   device->submit_count++;

   pthread_mutex_unlock(&device->submit_mutex);
   pthread_cond_broadcast(&queue->device->timeline_cond);

out:
   tu_submit_finish(device, submit);

   return result;
}

static VkResult
queue_submit(struct vk_queue *_queue, struct vk_queue_submit *vk_submit)
{
   MESA_TRACE_FUNC();
   struct tu_queue *queue = list_entry(_queue, struct tu_queue, vk);
   struct tu_device *device = queue->device;
   bool u_trace_enabled = u_trace_should_process(&queue->device->trace_context);
   struct util_dynarray dump_cmds;

   if (vk_submit->buffer_bind_count ||
       vk_submit->image_bind_count ||
       vk_submit->image_opaque_bind_count)
      return queue_submit_sparse(_queue, vk_submit);

   dump_cmds = UTIL_DYNARRAY_INIT;

   uint32_t perf_pass_index =
      device->perfcntrs_pass_cs_entries ? vk_submit->perf_pass_index : ~0;

   if (TU_DEBUG(LOG_SKIP_GMEM_OPS))
      tu_dbg_log_gmem_load_store_skips(device);

   pthread_mutex_lock(&device->submit_mutex);

   struct tu_cmd_buffer **cmd_buffers =
      (struct tu_cmd_buffer **) vk_submit->command_buffers;
   uint32_t cmdbuf_count = vk_submit->command_buffer_count;

   VkResult result =
      tu_insert_dynamic_cmdbufs(device, &cmd_buffers, &cmdbuf_count);
   if (result != VK_SUCCESS)
      return result;

   bool has_trace_points = false;
   static_assert(offsetof(struct tu_cmd_buffer, vk) == 0,
                 "vk must be first member of tu_cmd_buffer");
   for (unsigned i = 0; i < vk_submit->command_buffer_count; i++) {
      if (u_trace_enabled && u_trace_has_points(&cmd_buffers[i]->trace))
         has_trace_points = true;
   }

   struct tu_u_trace_submission_data *u_trace_submission_data = NULL;

   void *submit = tu_submit_create(device);
   if (!submit)
      goto fail_create_submit;

   result = resolve_vis_stream_patchpoints(queue, submit, &dump_cmds,
                                           cmd_buffers, cmdbuf_count);
   if (result != VK_SUCCESS)
      goto out;

   result = resolve_cb_control_patchpoints(queue, submit, &dump_cmds,
                                           cmd_buffers, cmdbuf_count);

   if (result != VK_SUCCESS)
      goto out;

   if (has_trace_points) {
      tu_u_trace_submission_data_create(
         device, cmd_buffers, cmdbuf_count, &u_trace_submission_data);
   }

   for (uint32_t i = 0; i < cmdbuf_count; i++) {
      struct tu_cmd_buffer *cmd_buffer = cmd_buffers[i];
      struct tu_cs *cs = &cmd_buffer->cs;

      if (perf_pass_index != ~0) {
         struct tu_cs_entry *perf_cs_entry =
            &cmd_buffer->device->perfcntrs_pass_cs_entries[perf_pass_index];

         submit_add_entries(device, submit, &dump_cmds, perf_cs_entry, 1);
      }

      submit_add_entries(device, submit, &dump_cmds, cs->entries,
                         cs->entry_count);

      if (u_trace_submission_data &&
          u_trace_submission_data->timestamp_copy_data) {
         struct tu_cs *cs = &u_trace_submission_data->timestamp_copy_data->cs;
         submit_add_entries(device, submit, &dump_cmds, cs->entries,
                            cs->entry_count);
      }
   }

   if (tu_autotune_submit_requires_fence(cmd_buffers, cmdbuf_count)) {
      struct tu_cs *autotune_cs = tu_autotune_on_submit(
         device, &device->autotune, cmd_buffers, cmdbuf_count);
      submit_add_entries(device, submit, &dump_cmds, autotune_cs->entries,
                         autotune_cs->entry_count);
   }

   if (cmdbuf_count && FD_RD_DUMP(ENABLE) &&
       fd_rd_output_begin(&queue->device->rd_output,
                          queue->device->vk.current_frame, queue->device->submit_count)) {
      struct tu_device *device = queue->device;
      struct fd_rd_output *rd_output = &device->rd_output;

      if (FD_RD_DUMP(FULL)) {
         VkResult result = tu_queue_wait_fence(queue, queue->fence, ~0);
         if (result != VK_SUCCESS) {
            mesa_loge("FD_RD_DUMP_FULL: wait on previous submission for device %u and queue %d failed: %u",
                      device->device_idx, queue->msm_queue_id, 0);
         }
      }

      fd_rd_output_write_section(rd_output, RD_CHIP_ID, &device->physical_device->dev_id.chip_id, 8);
      fd_rd_output_write_section(rd_output, RD_CMD, "tu-dump", 8);

      mtx_lock(&device->bo_mutex);
      util_dynarray_foreach (&device->dump_bo_list, struct tu_bo *, bo_ptr) {
         struct tu_bo *bo = *bo_ptr;
         uint64_t iova = bo->iova;

         uint32_t buf[3] = { iova, bo->size, iova >> 32 };
         fd_rd_output_write_section(rd_output, RD_GPUADDR, buf, 12);
         if (bo->dump || FD_RD_DUMP(FULL)) {
            tu_bo_map(device, bo, NULL); /* note: this would need locking to be safe */
            fd_rd_output_write_section(rd_output, RD_BUFFER_CONTENTS, bo->map, bo->size);
         }
      }
      mtx_unlock(&device->bo_mutex);

      util_dynarray_foreach (&dump_cmds, struct tu_cs_entry, cmd) {
         uint64_t iova = cmd->bo->iova + cmd->offset;
         uint32_t size = cmd->size >> 2;
         uint32_t buf[3] = { iova, size, iova >> 32 };
         fd_rd_output_write_section(rd_output, RD_CMDSTREAM_ADDR, buf, 12);
      }

      fd_rd_output_end(rd_output);
   }

   util_dynarray_fini(&dump_cmds);

#ifdef HAVE_PERFETTO
   if (u_trace_should_process(&device->trace_context)) {
      for (int i = 0; i < vk_submit->command_buffer_count; i++)
         tu_perfetto_refresh_debug_utils_object_name(
            &vk_submit->command_buffers[i]->base);
   }
#endif

   result =
      tu_queue_submit(queue, submit, vk_submit->waits, vk_submit->wait_count,
                      vk_submit->signals, vk_submit->signal_count,
                      u_trace_submission_data);

   if (result != VK_SUCCESS) {
      pthread_mutex_unlock(&device->submit_mutex);
      goto out;
   }

   tu_debug_bos_print_stats(device);

   if (u_trace_submission_data) {
      u_trace_submission_data->submission_id = device->submit_count;
      u_trace_submission_data->queue = queue;
      u_trace_submission_data->fence = queue->fence;

      for (uint32_t i = 0; i < u_trace_submission_data->cmd_buffer_count; i++) {
         bool free_data = i == u_trace_submission_data->last_buffer_with_tracepoints;
         if (u_trace_submission_data->trace_per_cmd_buffer[i])
            u_trace_flush(u_trace_submission_data->trace_per_cmd_buffer[i],
                          u_trace_submission_data, queue->device->vk.current_frame,
                          free_data);
      }
      if (u_trace_submission_data->timestamp_copy_data) {
         u_trace_flush(&u_trace_submission_data->timestamp_copy_data->trace,
                       u_trace_submission_data, queue->device->vk.current_frame,
                       true);
      }
   }

   device->submit_count++;

   pthread_mutex_unlock(&device->submit_mutex);
   pthread_cond_broadcast(&queue->device->timeline_cond);

   u_trace_context_process(&device->trace_context, false);

out:
   tu_submit_finish(device, submit);

fail_create_submit:
   if (cmd_buffers != (struct tu_cmd_buffer **) vk_submit->command_buffers)
      vk_free(&queue->device->vk.alloc, cmd_buffers);

   return result;
}

VkResult
tu_queue_init(struct tu_device *device,
              struct tu_queue *queue,
              enum tu_queue_type type,
              int idx,
              const VkDeviceQueueCreateInfo *create_info)
{
   const VkDeviceQueueGlobalPriorityCreateInfoKHR *priority_info =
      vk_find_struct_const(create_info->pNext,
            DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR);
   const VkQueueGlobalPriorityKHR global_priority = priority_info ?
      priority_info->globalPriority :
      (TU_DEBUG(HIPRIO) ? VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR :
       VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR);

   const int priority = tu_get_submitqueue_priority(
         device->physical_device, global_priority, type,
         device->vk.enabled_features.globalPriorityQuery);
   if (priority < 0) {
      return vk_startup_errorf(device->instance, VK_ERROR_INITIALIZATION_FAILED,
                               "invalid global priority");
   }

   VkResult result = vk_queue_init(&queue->vk, &device->vk, create_info, idx);
   if (result != VK_SUCCESS)
      return result;

   queue->device = device;
   queue->priority = priority;
   queue->vk.driver_submit =
      (type == TU_QUEUE_SPARSE) ? queue_submit_sparse : queue_submit;
   queue->type = type;

   int ret = tu_drm_submitqueue_new(device, queue);
   if (ret)
      return vk_startup_errorf(device->instance, VK_ERROR_INITIALIZATION_FAILED,
                               "submitqueue create failed");

   queue->fence = -1;

   return VK_SUCCESS;
}

void
tu_queue_finish(struct tu_queue *queue)
{
   vk_queue_finish(&queue->vk);
   tu_drm_submitqueue_close(queue->device, queue);
}

