/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_queue.h"

#include "nvk_buffer.h"
#include "nvk_cmd_buffer.h"
#include "nvk_device.h"
#include "nvk_image.h"
#include "nvk_physical_device.h"
#include "nv_push.h"

#include "nv_push_cl9039.h"
#include "nv_push_cl9097.h"
#include "nv_push_cl90b5.h"
#include "nv_push_cla0c0.h"
#include "cla1c0.h"
#include "nv_push_clc3c0.h"
#include "nv_push_clc397.h"

static VkResult
nvk_queue_push(struct nvk_queue *queue, const struct nv_push *push);

static void
nvk_queue_state_init(struct nvk_queue_state *qs)
{
   memset(qs, 0, sizeof(*qs));
}

static void
nvk_queue_state_finish(struct nvk_device *dev,
                       struct nvk_queue_state *qs)
{
   if (qs->slm.mem)
      nvkmd_mem_unref(qs->slm.mem);
}

static VkResult
nvk_queue_state_update(struct nvk_queue *queue,
                       struct nvk_queue_state *qs)
{
   struct nvk_device *dev = nvk_queue_device(queue);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);
   struct nvkmd_mem *mem;
   uint32_t alloc_count, bytes_per_warp, bytes_per_tpc;
   bool dirty = false;

   alloc_count = nvk_descriptor_table_alloc_count(&dev->images);
   if (qs->images.alloc_count != alloc_count) {
      qs->images.alloc_count = alloc_count;
      dirty = true;
   }

   alloc_count = nvk_descriptor_table_alloc_count(&dev->samplers);
   if (qs->samplers.alloc_count != alloc_count) {
      qs->samplers.alloc_count = alloc_count;
      dirty = true;
   }

   mem = nvk_slm_area_get_mem_ref(&dev->slm, &bytes_per_warp, &bytes_per_tpc);
   if (qs->slm.mem != mem || qs->slm.bytes_per_warp != bytes_per_warp ||
       qs->slm.bytes_per_tpc != bytes_per_tpc) {
      if (qs->slm.mem)
         nvkmd_mem_unref(qs->slm.mem);
      qs->slm.mem = mem;
      qs->slm.bytes_per_warp = bytes_per_warp;
      qs->slm.bytes_per_tpc = bytes_per_tpc;
      dirty = true;
   } else {
      /* No change */
      if (mem)
         nvkmd_mem_unref(mem);
   }

   if (!dirty)
      return VK_SUCCESS;

   uint32_t push_data[64];
   struct nv_push push;
   nv_push_init(&push, push_data, 64,
                nvk_queue_subchannels_from_engines(queue->engines));
   struct nv_push *p = &push;

   if (qs->images.alloc_count > 0) {
      const uint64_t tex_pool_addr =
         nvk_descriptor_table_base_address(&dev->images);
      if (queue->engines & NVKMD_ENGINE_COMPUTE) {
         P_MTHD(p, NVA0C0, SET_TEX_HEADER_POOL_A);
         P_NVA0C0_SET_TEX_HEADER_POOL_A(p, tex_pool_addr >> 32);
         P_NVA0C0_SET_TEX_HEADER_POOL_B(p, tex_pool_addr);
         P_NVA0C0_SET_TEX_HEADER_POOL_C(p, qs->images.alloc_count - 1);
         P_IMMD(p, NVA0C0, INVALIDATE_TEXTURE_HEADER_CACHE_NO_WFI, {
            .lines = LINES_ALL
         });
      }

      if (queue->engines & NVKMD_ENGINE_3D) {
         P_MTHD(p, NV9097, SET_TEX_HEADER_POOL_A);
         P_NV9097_SET_TEX_HEADER_POOL_A(p, tex_pool_addr >> 32);
         P_NV9097_SET_TEX_HEADER_POOL_B(p, tex_pool_addr);
         P_NV9097_SET_TEX_HEADER_POOL_C(p, qs->images.alloc_count - 1);
         P_IMMD(p, NV9097, INVALIDATE_TEXTURE_HEADER_CACHE_NO_WFI, {
            .lines = LINES_ALL
         });
      }
   }

   if (qs->samplers.alloc_count > 0) {
      const uint64_t sampler_pool_addr =
         nvk_descriptor_table_base_address(&dev->samplers);
      if (queue->engines & NVKMD_ENGINE_COMPUTE) {
         P_MTHD(p, NVA0C0, SET_TEX_SAMPLER_POOL_A);
         P_NVA0C0_SET_TEX_SAMPLER_POOL_A(p, sampler_pool_addr >> 32);
         P_NVA0C0_SET_TEX_SAMPLER_POOL_B(p, sampler_pool_addr);
         P_NVA0C0_SET_TEX_SAMPLER_POOL_C(p, qs->samplers.alloc_count - 1);
         P_IMMD(p, NVA0C0, INVALIDATE_SAMPLER_CACHE_NO_WFI, {
            .lines = LINES_ALL
         });
      }

      if (queue->engines & NVKMD_ENGINE_3D) {
         P_MTHD(p, NV9097, SET_TEX_SAMPLER_POOL_A);
         P_NV9097_SET_TEX_SAMPLER_POOL_A(p, sampler_pool_addr >> 32);
         P_NV9097_SET_TEX_SAMPLER_POOL_B(p, sampler_pool_addr);
         P_NV9097_SET_TEX_SAMPLER_POOL_C(p, qs->samplers.alloc_count - 1);
         P_IMMD(p, NV9097, INVALIDATE_SAMPLER_CACHE_NO_WFI, {
            .lines = LINES_ALL
         });
      }
   }

   if (qs->slm.mem) {
      const uint64_t slm_addr = qs->slm.mem->va->addr;
      const uint64_t slm_size = qs->slm.mem->size_B;
      const uint64_t slm_per_warp = qs->slm.bytes_per_warp;
      const uint64_t slm_per_tpc = qs->slm.bytes_per_tpc;
      assert(!(slm_per_tpc & 0x7fff));

      if (queue->engines & NVKMD_ENGINE_COMPUTE) {
         P_MTHD(p, NVA0C0, SET_SHADER_LOCAL_MEMORY_A);
         P_NVA0C0_SET_SHADER_LOCAL_MEMORY_A(p, slm_addr >> 32);
         P_NVA0C0_SET_SHADER_LOCAL_MEMORY_B(p, slm_addr);

         P_MTHD(p, NVA0C0, SET_SHADER_LOCAL_MEMORY_NON_THROTTLED_A);
         P_NVA0C0_SET_SHADER_LOCAL_MEMORY_NON_THROTTLED_A(p, slm_per_tpc >> 32);
         P_NVA0C0_SET_SHADER_LOCAL_MEMORY_NON_THROTTLED_B(p, slm_per_tpc);
         P_NVA0C0_SET_SHADER_LOCAL_MEMORY_NON_THROTTLED_C(p, 0xff);

         if (pdev->info.cls_compute < VOLTA_COMPUTE_A) {
            P_MTHD(p, NVA0C0, SET_SHADER_LOCAL_MEMORY_THROTTLED_A);
            P_NVA0C0_SET_SHADER_LOCAL_MEMORY_THROTTLED_A(p, slm_per_tpc >> 32);
            P_NVA0C0_SET_SHADER_LOCAL_MEMORY_THROTTLED_B(p, slm_per_tpc);
            P_NVA0C0_SET_SHADER_LOCAL_MEMORY_THROTTLED_C(p, 0xff);
         }
      }

      if (queue->engines & NVKMD_ENGINE_3D) {
         P_MTHD(p, NV9097, SET_SHADER_LOCAL_MEMORY_A);
         P_NV9097_SET_SHADER_LOCAL_MEMORY_A(p, slm_addr >> 32);
         P_NV9097_SET_SHADER_LOCAL_MEMORY_B(p, slm_addr);
         P_NV9097_SET_SHADER_LOCAL_MEMORY_C(p, slm_size >> 32);
         P_NV9097_SET_SHADER_LOCAL_MEMORY_D(p, slm_size);
         P_NV9097_SET_SHADER_LOCAL_MEMORY_E(p, slm_per_warp);
      }
   }

   return nvk_queue_push(queue, p);
}

static VkResult
nvk_queue_submit_bind(struct nvk_queue *queue,
                      struct vk_queue_submit *submit)
{
   VkResult result;

   result = nvkmd_ctx_wait(queue->bind_ctx, &queue->vk.base,
                           submit->wait_count, submit->waits);
   if (result != VK_SUCCESS)
      return result;

   for (uint32_t i = 0; i < submit->buffer_bind_count; i++) {
      result = nvk_queue_buffer_bind(queue, &submit->buffer_binds[i]);
      if (result != VK_SUCCESS)
         return result;
   }

   for (uint32_t i = 0; i < submit->image_bind_count; i++) {
      result = nvk_queue_image_bind(queue, &submit->image_binds[i]);
      if (result != VK_SUCCESS)
         return result;
   }

   for (uint32_t i = 0; i < submit->image_opaque_bind_count; i++) {
      result = nvk_queue_image_opaque_bind(queue, &submit->image_opaque_binds[i]);
      if (result != VK_SUCCESS)
         return result;
   }

   result = nvkmd_ctx_signal(queue->bind_ctx, &queue->vk.base,
                             submit->signal_count, submit->signals);
   if (result != VK_SUCCESS)
      return result;

   return VK_SUCCESS;
}

static VkResult
nvk_queue_submit_exec(struct nvk_queue *queue,
                      struct vk_queue_submit *submit)
{
   struct nvk_device *dev = nvk_queue_device(queue);
   VkResult result;

   if (submit->command_buffer_count > 0) {
      nvk_descriptor_table_flush_map(dev, &dev->images);
      nvk_descriptor_table_flush_map(dev, &dev->samplers);
      nvk_heap_flush_maps(dev, &dev->shader_heap);
      assert(dev->event_heap.arena.mem_flags & NVKMD_MEM_COHERENT);

      result = nvk_queue_state_update(queue, &queue->state);
      if (result != VK_SUCCESS)
         return result;

      uint64_t upload_time_point;
      result = nvk_upload_queue_flush(dev, &dev->upload, &upload_time_point);
      if (result != VK_SUCCESS)
         return result;

      if (upload_time_point > 0) {
         struct vk_sync_wait wait = {
            .sync = dev->upload.stream.sync,
            .stage_mask = ~0,
            .wait_value = upload_time_point,
         };
         result = nvkmd_ctx_wait(queue->exec_ctx, &queue->vk.base, 1, &wait);
         if (result != VK_SUCCESS)
            goto fail;
      }
   }

   result = nvkmd_ctx_wait(queue->exec_ctx, &queue->vk.base,
                           submit->wait_count, submit->waits);
   if (result != VK_SUCCESS)
      goto fail;

   for (unsigned i = 0; i < submit->command_buffer_count; i++) {
      struct nvk_cmd_buffer *cmd =
         container_of(submit->command_buffers[i], struct nvk_cmd_buffer, vk);

      const uint32_t max_execs =
         util_dynarray_num_elements(&cmd->pushes, struct nvk_cmd_push);
      STACK_ARRAY(struct nvkmd_ctx_exec, execs, max_execs);
      uint32_t exec_count = 0;

      util_dynarray_foreach(&cmd->pushes, struct nvk_cmd_push, push) {
         if (push->range == 0)
            continue;

         execs[exec_count++] = (struct nvkmd_ctx_exec) {
            .addr = push->addr,
            .size_B = push->range,
            .incomplete = push->incomplete,
            .no_prefetch = push->no_prefetch,
         };
      }

      result = nvkmd_ctx_exec(queue->exec_ctx, &queue->vk.base,
                              exec_count, execs);

      STACK_ARRAY_FINISH(execs);

      if (result != VK_SUCCESS)
         goto fail;
   }

   result = nvkmd_ctx_signal(queue->exec_ctx, &queue->vk.base,
                             submit->signal_count, submit->signals);
   if (result != VK_SUCCESS)
      goto fail;

fail:
   return result;
}

static VkResult
nvk_queue_submit(struct vk_queue *vk_queue,
                 struct vk_queue_submit *submit)
{
   struct nvk_queue *queue = container_of(vk_queue, struct nvk_queue, vk);
   VkResult result;

   if (vk_queue_is_lost(&queue->vk))
      return VK_ERROR_DEVICE_LOST;

   if (submit->buffer_bind_count > 0 ||
       submit->image_bind_count > 0  ||
       submit->image_opaque_bind_count > 0) {
      assert(submit->command_buffer_count == 0);
      result = nvk_queue_submit_bind(queue, submit);
      if (result != VK_SUCCESS)
         return vk_queue_set_lost(&queue->vk, "Bind operation failed");
   } else {
      result = nvk_queue_submit_exec(queue, submit);
      if (result != VK_SUCCESS)
         return vk_queue_set_lost(&queue->vk, "Submit failed");
   }

   return VK_SUCCESS;
}

static VkResult
nvk_queue_push(struct nvk_queue *queue, const struct nv_push *push)
{
   struct nvk_device *dev = nvk_queue_device(queue);

   if (vk_queue_is_lost(&queue->vk))
      return VK_ERROR_DEVICE_LOST;

   if (nv_push_dw_count(push) == 0)
      return VK_SUCCESS;

   return nvk_mem_stream_push(dev, &queue->push_stream, queue->exec_ctx,
                              push->start, nv_push_dw_count(push), NULL);
}

static VkResult
nvk_queue_init_context_state(struct nvk_queue *queue)
{
   struct nvk_device *dev = nvk_queue_device(queue);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);
   VkResult result;

   uint32_t push_data[4096];
   struct nv_push push;
   nv_push_init(&push, push_data, ARRAY_SIZE(push_data),
                nvk_queue_subchannels_from_engines(queue->engines));
   struct nv_push *p = &push;

   /* M2MF state */
   if (pdev->info.cls_m2mf <= FERMI_MEMORY_TO_MEMORY_FORMAT_A) {
      /* we absolutely do not support Fermi, but if somebody wants to toy
       * around with it, this is a must
       */
      P_MTHD(p, NV9039, SET_OBJECT);
      P_NV9039_SET_OBJECT(p, {
         .class_id = pdev->info.cls_m2mf,
         .engine_id = 0,
      });
   }

   if (queue->engines & NVKMD_ENGINE_3D) {
      result = nvk_push_draw_state_init(queue, p);
      if (result != VK_SUCCESS)
         return result;
   }

   if (queue->engines & NVKMD_ENGINE_COMPUTE) {
      result = nvk_push_dispatch_state_init(queue, p);
      if (result != VK_SUCCESS)
         return result;
   }

   return nvk_queue_push(queue, &push);
}

static VkQueueGlobalPriority
get_queue_global_priority(const VkDeviceQueueCreateInfo *pCreateInfo)
{
   const VkDeviceQueueGlobalPriorityCreateInfo *priority_info =
      vk_find_struct_const(pCreateInfo->pNext,
                           DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO);
   if (priority_info == NULL)
      return VK_QUEUE_GLOBAL_PRIORITY_MEDIUM;

   return priority_info->globalPriority;
}

VkResult
nvk_queue_create(struct nvk_device *dev,
                 const VkDeviceQueueCreateInfo *pCreateInfo,
                 uint32_t index_in_family)
{
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);
   VkResult result;

   assert(pCreateInfo->queueFamilyIndex < pdev->queue_family_count);
   const struct nvk_queue_family *queue_family =
      &pdev->queue_families[pCreateInfo->queueFamilyIndex];

   const VkQueueGlobalPriority global_priority =
      get_queue_global_priority(pCreateInfo);

   /* From the Vulkan 1.3.295 spec:
    *
    *    "If the globalPriorityQuery feature is enabled and the requested
    *    global priority is not reported via
    *    VkQueueFamilyGlobalPriorityPropertiesKHR, the driver implementation
    *    must fail the queue creation. In this scenario,
    *    VK_ERROR_INITIALIZATION_FAILED is returned."
    */
   if (dev->vk.enabled_features.globalPriorityQuery &&
       global_priority != VK_QUEUE_GLOBAL_PRIORITY_MEDIUM)
      return VK_ERROR_INITIALIZATION_FAILED;

   if (global_priority > VK_QUEUE_GLOBAL_PRIORITY_MEDIUM)
      return VK_ERROR_NOT_PERMITTED;

   struct nvk_queue *queue = vk_zalloc(&dev->vk.alloc, sizeof(struct nvk_queue),
                                       8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!queue)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   result = vk_queue_init(&queue->vk, &dev->vk, pCreateInfo, index_in_family);
   if (result != VK_SUCCESS)
      goto fail_alloc;

   nvk_queue_state_init(&queue->state);

   queue->engines =
      nvk_queue_engines_from_queue_flags(queue_family->queue_flags);

   if (queue->engines) {
      result = nvkmd_dev_create_ctx(dev->nvkmd, &dev->vk.base,
                                    queue->engines, &queue->exec_ctx);
      if (result != VK_SUCCESS)
         goto fail_init;

      result = nvkmd_dev_alloc_mem(dev->nvkmd, &dev->vk.base,
                                   4096, 0, NVKMD_MEM_LOCAL,
                                   &queue->draw_cb0);
      if (result != VK_SUCCESS)
         goto fail_exec_ctx;

      result = nvk_upload_queue_fill(dev, &dev->upload,
                                     queue->draw_cb0->va->addr, 0,
                                     queue->draw_cb0->size_B);
      if (result != VK_SUCCESS)
         goto fail_draw_cb0;
   }

   if (queue_family->queue_flags & VK_QUEUE_SPARSE_BINDING_BIT) {
      result = nvkmd_dev_create_ctx(dev->nvkmd, &dev->vk.base,
                                    NVKMD_ENGINE_BIND, &queue->bind_ctx);
      if (result != VK_SUCCESS)
         goto fail_draw_cb0;
   }

   result = nvk_mem_stream_init(dev, &queue->push_stream);
   if (result != VK_SUCCESS)
      goto fail_bind_ctx;

   result = nvk_queue_init_context_state(queue);
   if (result != VK_SUCCESS)
      goto fail_push_stream;

   queue->vk.driver_submit = nvk_queue_submit;

   return VK_SUCCESS;

fail_push_stream:
   nvk_mem_stream_sync(dev, &queue->push_stream, queue->exec_ctx);
   nvk_mem_stream_finish(dev, &queue->push_stream);
fail_bind_ctx:
   if (queue->bind_ctx != NULL)
      nvkmd_ctx_destroy(queue->bind_ctx);
fail_draw_cb0:
   if (queue->draw_cb0 != NULL)
      nvkmd_mem_unref(queue->draw_cb0);
fail_exec_ctx:
   if (queue->exec_ctx != NULL)
      nvkmd_ctx_destroy(queue->exec_ctx);
fail_init:
   nvk_queue_state_finish(dev, &queue->state);
   vk_queue_finish(&queue->vk);
fail_alloc:
   vk_free(&dev->vk.alloc, queue);

   return result;
}

void
nvk_queue_destroy(struct nvk_device *dev, struct nvk_queue *queue)
{
   nvk_mem_stream_sync(dev, &queue->push_stream, queue->exec_ctx);
   nvk_mem_stream_finish(dev, &queue->push_stream);
   if (queue->draw_cb0 != NULL) {
      nvk_upload_queue_sync(dev, &dev->upload);
      nvkmd_mem_unref(queue->draw_cb0);
   }
   nvk_queue_state_finish(dev, &queue->state);
   if (queue->bind_ctx != NULL)
      nvkmd_ctx_destroy(queue->bind_ctx);
   if (queue->exec_ctx != NULL)
      nvkmd_ctx_destroy(queue->exec_ctx);
   vk_queue_finish(&queue->vk);
   vk_free(&dev->vk.alloc, queue);
}
