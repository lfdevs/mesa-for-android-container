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
nvk_queue_submit_simple(struct nvk_queue *queue,
                        uint32_t dw_count, const uint32_t *dw);

static void
nvk_queue_state_init(struct nvk_queue_state *qs)
{
   memset(qs, 0, sizeof(*qs));
}

static void
nvk_queue_state_finish(struct nvk_device *dev,
                       struct nvk_queue_state *qs)
{
   if (qs->images.mem)
      nvkmd_mem_unref(qs->images.mem);
   if (qs->samplers.mem)
      nvkmd_mem_unref(qs->samplers.mem);
   if (qs->slm.mem)
      nvkmd_mem_unref(qs->slm.mem);
}

static VkResult
nvk_queue_state_update(struct nvk_queue *queue,
                       struct nvk_queue_state *qs)
{
   struct nvk_device *dev = nvk_queue_device(queue);
   struct nvk_physical_device *pdev = nvk_device_physical(dev);
   struct nvkmd_mem *mem;
   uint32_t alloc_count, bytes_per_warp, bytes_per_tpc;
   bool dirty = false;

   mem = nvk_descriptor_table_get_mem_ref(&dev->images, &alloc_count);
   if (qs->images.mem != mem || qs->images.alloc_count != alloc_count) {
      if (qs->images.mem)
         nvkmd_mem_unref(qs->images.mem);
      qs->images.mem = mem;
      qs->images.alloc_count = alloc_count;
      dirty = true;
   } else {
      /* No change */
      if (mem)
         nvkmd_mem_unref(mem);
   }

   mem = nvk_descriptor_table_get_mem_ref(&dev->samplers, &alloc_count);
   if (qs->samplers.mem != mem || qs->samplers.alloc_count != alloc_count) {
      if (qs->samplers.mem)
         nvkmd_mem_unref(qs->samplers.mem);
      qs->samplers.mem = mem;
      qs->samplers.alloc_count = alloc_count;
      dirty = true;
   } else {
      /* No change */
      if (mem)
         nvkmd_mem_unref(mem);
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
   nv_push_init(&push, push_data, 64);
   struct nv_push *p = &push;

   if (qs->images.mem) {
      if (queue->engines & NVKMD_ENGINE_COMPUTE) {
         P_MTHD(p, NVA0C0, SET_TEX_HEADER_POOL_A);
         P_NVA0C0_SET_TEX_HEADER_POOL_A(p, qs->images.mem->va->addr >> 32);
         P_NVA0C0_SET_TEX_HEADER_POOL_B(p, qs->images.mem->va->addr);
         P_NVA0C0_SET_TEX_HEADER_POOL_C(p, qs->images.alloc_count - 1);
         P_IMMD(p, NVA0C0, INVALIDATE_TEXTURE_HEADER_CACHE_NO_WFI, {
            .lines = LINES_ALL
         });
      }

      if (queue->engines & NVKMD_ENGINE_3D) {
         P_MTHD(p, NV9097, SET_TEX_HEADER_POOL_A);
         P_NV9097_SET_TEX_HEADER_POOL_A(p, qs->images.mem->va->addr >> 32);
         P_NV9097_SET_TEX_HEADER_POOL_B(p, qs->images.mem->va->addr);
         P_NV9097_SET_TEX_HEADER_POOL_C(p, qs->images.alloc_count - 1);
         P_IMMD(p, NV9097, INVALIDATE_TEXTURE_HEADER_CACHE_NO_WFI, {
            .lines = LINES_ALL
         });
      }
   }

   if (qs->samplers.mem) {
      if (queue->engines & NVKMD_ENGINE_COMPUTE) {
         P_MTHD(p, NVA0C0, SET_TEX_SAMPLER_POOL_A);
         P_NVA0C0_SET_TEX_SAMPLER_POOL_A(p, qs->samplers.mem->va->addr >> 32);
         P_NVA0C0_SET_TEX_SAMPLER_POOL_B(p, qs->samplers.mem->va->addr);
         P_NVA0C0_SET_TEX_SAMPLER_POOL_C(p, qs->samplers.alloc_count - 1);
         P_IMMD(p, NVA0C0, INVALIDATE_SAMPLER_CACHE_NO_WFI, {
            .lines = LINES_ALL
         });
      }

      if (queue->engines & NVKMD_ENGINE_3D) {
         P_MTHD(p, NV9097, SET_TEX_SAMPLER_POOL_A);
         P_NV9097_SET_TEX_SAMPLER_POOL_A(p, qs->samplers.mem->va->addr >> 32);
         P_NV9097_SET_TEX_SAMPLER_POOL_B(p, qs->samplers.mem->va->addr);
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

   /* We set memory windows unconditionally.  Otherwise, the memory window
    * might be in a random place and cause us to fault off into nowhere.
    */
   if (queue->engines & NVKMD_ENGINE_COMPUTE) {
      if (pdev->info.cls_compute >= VOLTA_COMPUTE_A) {
         uint64_t temp = 0xfeULL << 24;
         P_MTHD(p, NVC3C0, SET_SHADER_SHARED_MEMORY_WINDOW_A);
         P_NVC3C0_SET_SHADER_SHARED_MEMORY_WINDOW_A(p, temp >> 32);
         P_NVC3C0_SET_SHADER_SHARED_MEMORY_WINDOW_B(p, temp & 0xffffffff);

         temp = 0xffULL << 24;
         P_MTHD(p, NVC3C0, SET_SHADER_LOCAL_MEMORY_WINDOW_A);
         P_NVC3C0_SET_SHADER_LOCAL_MEMORY_WINDOW_A(p, temp >> 32);
         P_NVC3C0_SET_SHADER_LOCAL_MEMORY_WINDOW_B(p, temp & 0xffffffff);
      } else {
         P_MTHD(p, NVA0C0, SET_SHADER_LOCAL_MEMORY_WINDOW);
         P_NVA0C0_SET_SHADER_LOCAL_MEMORY_WINDOW(p, 0xff << 24);

         P_MTHD(p, NVA0C0, SET_SHADER_SHARED_MEMORY_WINDOW);
         P_NVA0C0_SET_SHADER_SHARED_MEMORY_WINDOW(p, 0xfe << 24);
      }

      /* From nvc0_screen.c:
       *
       *    "Reduce likelihood of collision with real buffers by placing the
       *    hole at the top of the 4G area. This will have to be dealt with
       *    for real eventually by blocking off that area from the VM."
       *
       * Really?!?  TODO: Fix this for realz.  Annoyingly, we only have a
       * 32-bit pointer for this in 3D rather than a full 48 like we have for
       * compute.
       */
      P_IMMD(p, NV9097, SET_SHADER_LOCAL_MEMORY_WINDOW, 0xff << 24);
   }

   /* From nvc0_screen.c:
    *
    *    "Reduce likelihood of collision with real buffers by placing the
    *    hole at the top of the 4G area. This will have to be dealt with
    *    for real eventually by blocking off that area from the VM."
    *
    * Really?!?  TODO: Fix this for realz.  Annoyingly, we only have a
    * 32-bit pointer for this in 3D rather than a full 48 like we have for
    * compute.
    */
   P_IMMD(p, NV9097, SET_SHADER_LOCAL_MEMORY_WINDOW, 0xff << 24);

   return nvk_queue_submit_simple(queue, nv_push_dw_count(p), push_data);
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
   struct nvk_physical_device *pdev = nvk_device_physical(dev);
   VkResult result;

   const bool sync = pdev->debug_flags & NVK_DEBUG_PUSH_SYNC;

   if (submit->command_buffer_count > 0) {
      result = nvk_queue_state_update(queue, &queue->state);
      if (result != VK_SUCCESS)
         return result;

      uint64_t upload_time_point;
      result = nvk_upload_queue_flush(dev, &dev->upload, &upload_time_point);
      if (result != VK_SUCCESS)
         return result;

      if (upload_time_point > 0) {
         struct vk_sync_wait wait = {
            .sync = dev->upload.sync,
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

   if (sync) {
      result = nvkmd_ctx_sync(queue->exec_ctx, &queue->vk.base);
      if (result != VK_SUCCESS)
         goto fail;
   }

fail:
   if ((sync && result != VK_SUCCESS) ||
       (pdev->debug_flags & NVK_DEBUG_PUSH_DUMP)) {
      for (unsigned i = 0; i < submit->command_buffer_count; i++) {
         struct nvk_cmd_buffer *cmd =
            container_of(submit->command_buffers[i], struct nvk_cmd_buffer, vk);

         nvk_cmd_buffer_dump(cmd, stderr);
      }
   }

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
nvk_queue_submit_simple(struct nvk_queue *queue,
                        uint32_t dw_count, const uint32_t *dw)
{
   struct nvk_device *dev = nvk_queue_device(queue);
   struct nvk_physical_device *pdev = nvk_device_physical(dev);
   VkResult result;

   if (vk_queue_is_lost(&queue->vk))
      return VK_ERROR_DEVICE_LOST;

   struct nvkmd_mem *push_mem;
   result = nvkmd_dev_alloc_mapped_mem(dev->nvkmd, &dev->vk.base,
                                       dw_count * 4, 0,
                                       NVKMD_MEM_GART,
                                       NVKMD_MEM_MAP_WR, &push_mem);
   if (result != VK_SUCCESS)
      return result;

   memcpy(push_mem->map, dw, dw_count * 4);

   const struct nvkmd_ctx_exec exec = {
      .addr = push_mem->va->addr,
      .size_B = dw_count * 4,
   };
   result = nvkmd_ctx_exec(queue->exec_ctx, &queue->vk.base, 1, &exec);
   if (result == VK_SUCCESS)
      result = nvkmd_ctx_sync(queue->exec_ctx, &queue->vk.base);

   nvkmd_mem_unref(push_mem);

   const bool debug_sync = pdev->debug_flags & NVK_DEBUG_PUSH_SYNC;
   if ((debug_sync && result != VK_SUCCESS) ||
       (pdev->debug_flags & NVK_DEBUG_PUSH_DUMP)) {
      struct nv_push push = {
         .start = (uint32_t *)dw,
         .end = (uint32_t *)dw + dw_count,
      };
      vk_push_print(stderr, &push, &pdev->info);
   }

   if (result != VK_SUCCESS)
      return vk_queue_set_lost(&queue->vk, "Submit failed");

   return VK_SUCCESS;
}

static VkResult
nvk_queue_init_context_state(struct nvk_queue *queue)
{
   struct nvk_device *dev = nvk_queue_device(queue);
   struct nvk_physical_device *pdev = nvk_device_physical(dev);
   VkResult result;

   uint32_t push_data[4096];
   struct nv_push push;
   nv_push_init(&push, push_data, ARRAY_SIZE(push_data));
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

   return nvk_queue_submit_simple(queue, nv_push_dw_count(&push), push_data);
}

VkResult
nvk_queue_init(struct nvk_device *dev, struct nvk_queue *queue,
               const VkDeviceQueueCreateInfo *pCreateInfo,
               uint32_t index_in_family)
{
   struct nvk_physical_device *pdev = nvk_device_physical(dev);
   VkResult result;

   assert(pCreateInfo->queueFamilyIndex < pdev->queue_family_count);
   const struct nvk_queue_family *queue_family =
      &pdev->queue_families[pCreateInfo->queueFamilyIndex];

   result = vk_queue_init(&queue->vk, &dev->vk, pCreateInfo, index_in_family);
   if (result != VK_SUCCESS)
      return result;

   nvk_queue_state_init(&queue->state);

   queue->engines = 0;
   if (queue_family->queue_flags & VK_QUEUE_GRAPHICS_BIT) {
      queue->engines |= NVKMD_ENGINE_3D;
      /* We rely on compute shaders for queries */
      queue->engines |= NVKMD_ENGINE_COMPUTE;
   }
   if (queue_family->queue_flags & VK_QUEUE_COMPUTE_BIT) {
      queue->engines |= NVKMD_ENGINE_COMPUTE;
      /* We currently rely on 3D engine MMEs for indirect dispatch */
      queue->engines |= NVKMD_ENGINE_3D;
   }
   if (queue_family->queue_flags & VK_QUEUE_TRANSFER_BIT)
      queue->engines |= NVKMD_ENGINE_COPY;

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

   result = nvk_queue_init_context_state(queue);
   if (result != VK_SUCCESS)
      goto fail_bind_ctx;

   queue->vk.driver_submit = nvk_queue_submit;

   return VK_SUCCESS;

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

   return result;
}

void
nvk_queue_finish(struct nvk_device *dev, struct nvk_queue *queue)
{
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
}
