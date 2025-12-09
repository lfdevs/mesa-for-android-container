/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_device.h"

#include "nvk_cmd_buffer.h"
#include "nvk_entrypoints.h"
#include "nvk_instance.h"
#include "nvk_physical_device.h"
#include "nvk_sampler.h"
#include "nvk_shader.h"
#include "nvkmd/nvkmd.h"

#include "vk_drm_syncobj.h"
#include "vk_pipeline_cache.h"
#include "vulkan/wsi/wsi_common.h"

#include "cl9097.h"
#include "clb097.h"
#include "clb197.h"
#include "clc397.h"

static void
nvk_slm_area_init(struct nvk_slm_area *area)
{
   memset(area, 0, sizeof(*area));
   simple_mtx_init(&area->mutex, mtx_plain);
}

static void
nvk_slm_area_finish(struct nvk_slm_area *area)
{
   simple_mtx_destroy(&area->mutex);
   if (area->mem)
      nvkmd_mem_unref(area->mem);
}

struct nvkmd_mem *
nvk_slm_area_get_mem_ref(struct nvk_slm_area *area,
                         uint32_t *bytes_per_warp_out,
                         uint32_t *bytes_per_tpc_out)
{
   simple_mtx_lock(&area->mutex);
   struct nvkmd_mem *mem = area->mem;
   if (mem)
      nvkmd_mem_ref(mem);
   *bytes_per_warp_out = area->bytes_per_warp;
   *bytes_per_tpc_out = area->bytes_per_tpc;
   simple_mtx_unlock(&area->mutex);

   return mem;
}

static VkResult
nvk_slm_area_ensure(struct nvk_device *dev,
                    struct nvk_slm_area *area,
                    uint32_t slm_bytes_per_lane,
                    uint32_t crs_bytes_per_warp)
{
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);
   VkResult result;

   assert(slm_bytes_per_lane < (1 << 24));
   assert(crs_bytes_per_warp <= (1 << 20));
   uint64_t bytes_per_warp = slm_bytes_per_lane * 32 + crs_bytes_per_warp;

   /* The hardware seems to require this alignment for
    * NV9097_SET_SHADER_LOCAL_MEMORY_E_DEFAULT_SIZE_PER_WARP
    */
   bytes_per_warp = align64(bytes_per_warp, 0x200);

   uint64_t bytes_per_mp = bytes_per_warp * pdev->info.max_warps_per_mp;
   uint64_t bytes_per_tpc = bytes_per_mp * pdev->info.mp_per_tpc;

   /* The hardware seems to require this alignment for
    * NVA0C0_SET_SHADER_LOCAL_MEMORY_NON_THROTTLED_A_SIZE_LOWER.
    */
   bytes_per_tpc = align64(bytes_per_tpc, 0x8000);

   /* nvk_slm_area::bytes_per_mp only ever increases so we can check this
    * outside the lock and exit early in the common case.  We only need to
    * take the lock if we're actually going to resize.
    *
    * Also, we only care about bytes_per_mp and not bytes_per_warp because
    * they are integer multiples of each other.
    */
   if (likely(bytes_per_tpc <= area->bytes_per_tpc))
      return VK_SUCCESS;

   uint64_t size = bytes_per_tpc * pdev->info.tpc_count;

   /* The hardware seems to require this alignment for
    * NV9097_SET_SHADER_LOCAL_MEMORY_D_SIZE_LOWER.
    */
   size = align64(size, 0x20000);

   struct nvkmd_mem *mem;
   result = nvkmd_dev_alloc_mem(dev->nvkmd, &dev->vk.base, size, 0,
                                NVKMD_MEM_LOCAL, &mem);
   if (result != VK_SUCCESS)
      return result;

   struct nvkmd_mem *unref_mem;
   simple_mtx_lock(&area->mutex);
   if (bytes_per_tpc <= area->bytes_per_tpc) {
      /* We lost the race, throw away our BO */
      assert(area->bytes_per_warp >= bytes_per_warp);
      unref_mem = mem;
   } else {
      unref_mem = area->mem;
      area->mem = mem;
      area->bytes_per_warp = bytes_per_warp;
      area->bytes_per_tpc = bytes_per_tpc;
   }
   simple_mtx_unlock(&area->mutex);

   if (unref_mem)
      nvkmd_mem_unref(unref_mem);

   return VK_SUCCESS;
}

static VkResult
nvk_device_get_timestamp(struct vk_device *vk_dev, uint64_t *timestamp)
{
   struct nvk_device *dev = container_of(vk_dev, struct nvk_device, vk);
   *timestamp = nvkmd_dev_get_gpu_timestamp(dev->nvkmd);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_CreateDevice(VkPhysicalDevice physicalDevice,
                 const VkDeviceCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator,
                 VkDevice *pDevice)
{
   VK_FROM_HANDLE(nvk_physical_device, pdev, physicalDevice);
   VkResult result = VK_ERROR_OUT_OF_HOST_MEMORY;
   struct nvk_device *dev;

   dev = vk_zalloc2(&pdev->vk.instance->alloc, pAllocator,
                    sizeof(*dev), 8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!dev)
      return vk_error(pdev, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_device_dispatch_table dispatch_table;
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                             &nvk_device_entrypoints, true);
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                             &wsi_device_entrypoints, false);

   result = vk_device_init(&dev->vk, &pdev->vk, &dispatch_table,
                           pCreateInfo, pAllocator);
   if (result != VK_SUCCESS)
      goto fail_alloc;

   dev->vk.shader_ops = &nvk_device_shader_ops;

   uint32_t queue_count = 0;
   for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++)
      queue_count += pCreateInfo->pQueueCreateInfos[i].queueCount;

   if (queue_count > 0) {
      result = nvkmd_pdev_create_dev(pdev->nvkmd, &pdev->vk.base, &dev->nvkmd);
      if (result != VK_SUCCESS)
         goto fail_init;

      vk_device_set_drm_fd(&dev->vk, nvkmd_dev_get_drm_fd(dev->nvkmd));
      dev->vk.command_buffer_ops = &nvk_cmd_buffer_ops;

      dev->vk.get_timestamp = nvk_device_get_timestamp;
      dev->vk.copy_sync_payloads = vk_drm_syncobj_copy_payloads;

      result = nvk_upload_queue_init(dev, &dev->upload);
      if (result != VK_SUCCESS)
         goto fail_nvkmd;

      result = nvkmd_dev_alloc_mapped_mem(dev->nvkmd, &pdev->vk.base,
                                          0x1000, 0, NVKMD_MEM_LOCAL,
                                          NVKMD_MEM_MAP_WR, &dev->zero_page);
      if (result != VK_SUCCESS)
         goto fail_upload;

      memset(dev->zero_page->map, 0, 0x1000);
      nvkmd_mem_sync_map_to_gpu(dev->zero_page, 0, 0x1000);
      nvkmd_mem_unmap(dev->zero_page, 0);

      result = nvk_descriptor_table_init(dev, &dev->images,
                                         sizeof(struct nil_descriptor),
                                         1024, 1024 * 1024);
      if (result != VK_SUCCESS)
         goto fail_zero_page;

      /* Reserve the descriptor at offset 0 to be the null descriptor */
      const struct nil_descriptor null_desc =
         nil_null_descriptor(&pdev->info, dev->zero_page->va->addr);

      ASSERTED uint32_t null_image_index;
      result = nvk_descriptor_table_add(dev, &dev->images,
                                        &null_desc, sizeof(null_desc),
                                        &null_image_index);
      assert(result == VK_SUCCESS);
      assert(null_image_index == 0);

      result = nvk_descriptor_table_init(dev, &dev->samplers,
                                         8 * 4 /* tsc entry size */,
                                         4096, 4096);
      if (result != VK_SUCCESS)
         goto fail_images;

      /* On Kepler and earlier, TXF takes a sampler but SPIR-V defines it as
       * not taking one so we need to reserve one at device create time.  If
       * we do so now then it will always have sampler index 0 so we can rely
       * on that in the compiler lowering code (similar to null descriptors).
       */
      if (pdev->info.cls_eng3d < MAXWELL_A) {
         const struct nvk_sampler_header txf_sampler =
            nvk_txf_sampler_header(pdev);

         ASSERTED uint32_t txf_sampler_index;
         result = nvk_descriptor_table_add(dev, &dev->samplers,
                                           &txf_sampler, sizeof(txf_sampler),
                                           &txf_sampler_index);
         assert(result == VK_SUCCESS);
         assert(txf_sampler_index == 0);
      }

      if (dev->vk.enabled_features.descriptorBuffer ||
          nvk_use_edb_buffer_views(pdev)) {
         result = nvk_edb_bview_cache_init(dev, &dev->edb_bview_cache);
         if (result != VK_SUCCESS)
            goto fail_samplers;
      }

      /* If we have a full BAR, go ahead and do shader uploads on the CPU.
       * Otherwise, we fall back to doing shader uploads via the upload queue.
       *
       * Also, the I-cache pre-fetches and NVIDIA has informed us
       * overallocating shaders BOs by 2K is sufficient.
       */
      enum nvkmd_mem_map_flags shader_map_flags = 0;
      if (pdev->info.bar_size_B >= pdev->info.vram_size_B)
         shader_map_flags = NVKMD_MEM_MAP_WR;
      result = nvk_heap_init(dev, &dev->shader_heap,
                             NVKMD_MEM_LOCAL, shader_map_flags,
                             2048 /* overalloc */,
                             pdev->info.cls_eng3d < VOLTA_A);
      if (result != VK_SUCCESS)
         goto fail_edb_bview_cache;

      result = nvk_heap_init(dev, &dev->event_heap,
                             NVKMD_MEM_LOCAL | NVKMD_MEM_COHERENT,
                             NVKMD_MEM_MAP_WR,
                             0 /* overalloc */, false /* contiguous */);
      if (result != VK_SUCCESS)
         goto fail_shader_heap;

      if (pdev->info.cls_eng3d < MAXWELL_B) {
         result = nvk_heap_init(dev, &dev->qmd_heap,
                                NVKMD_MEM_LOCAL, NVKMD_MEM_MAP_WR,
                                0 /* overalloc */, false /* contiguous */);
         if (result != VK_SUCCESS)
            goto fail_event_heap;
      }

      nvk_slm_area_init(&dev->slm);

      if (pdev->info.cls_eng3d >= FERMI_A &&
          pdev->info.cls_eng3d < MAXWELL_A) {
         /* max size is 256k */
         result = nvkmd_dev_alloc_mem(dev->nvkmd, &pdev->vk.base,
                                      256 * 1024, 0, NVKMD_MEM_LOCAL,
                                      &dev->vab_memory);
         if (result != VK_SUCCESS)
            goto fail_slm;
      }

      for (unsigned i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
         for (unsigned q = 0; q < pCreateInfo->pQueueCreateInfos[i].queueCount; q++) {
            result = nvk_queue_create(dev, &pCreateInfo->pQueueCreateInfos[i], q);
            if (result != VK_SUCCESS)
               goto fail_queues;
         }
      }
   }

   struct vk_pipeline_cache_create_info cache_info = {
      .weak_ref = true,
   };
   dev->vk.mem_cache = vk_pipeline_cache_create(&dev->vk, &cache_info, NULL);
   if (dev->vk.mem_cache == NULL) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail_queues;
   }

   if (queue_count > 0) {
      result = nvk_device_init_meta(dev);
      if (result != VK_SUCCESS)
         goto fail_mem_cache;
   }

   *pDevice = nvk_device_to_handle(dev);

   return VK_SUCCESS;

fail_mem_cache:
   vk_pipeline_cache_destroy(dev->vk.mem_cache, NULL);
fail_queues:
   vk_foreach_queue_safe(iter, &dev->vk) {
      struct nvk_queue *queue = container_of(iter, struct nvk_queue, vk);
      nvk_queue_destroy(dev, queue);
   }
   if (dev->vab_memory)
      nvkmd_mem_unref(dev->vab_memory);
fail_slm:
   nvk_slm_area_finish(&dev->slm);
   if (pdev->info.cls_eng3d < MAXWELL_B)
      nvk_heap_finish(dev, &dev->qmd_heap);
fail_event_heap:
   nvk_heap_finish(dev, &dev->event_heap);
fail_shader_heap:
   nvk_heap_finish(dev, &dev->shader_heap);
fail_edb_bview_cache:
   nvk_edb_bview_cache_finish(dev, &dev->edb_bview_cache);
fail_samplers:
   nvk_descriptor_table_finish(dev, &dev->samplers);
fail_images:
   nvk_descriptor_table_finish(dev, &dev->images);
fail_zero_page:
   nvkmd_mem_unref(dev->zero_page);
fail_upload:
   nvk_upload_queue_finish(dev, &dev->upload);
fail_nvkmd:
   nvkmd_dev_destroy(dev->nvkmd);
fail_init:
   vk_device_finish(&dev->vk);
fail_alloc:
   vk_free(&dev->vk.alloc, dev);
   return result;
}

VKAPI_ATTR void VKAPI_CALL
nvk_DestroyDevice(VkDevice _device, const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);

   if (!dev)
      return;

   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   if (dev->copy_queries)
      vk_shader_destroy(&dev->vk, &dev->copy_queries->vk, &dev->vk.alloc);

   if (dev->nvkmd)
      nvk_device_finish_meta(dev);

   vk_pipeline_cache_destroy(dev->vk.mem_cache, NULL);

   vk_foreach_queue_safe(iter, &dev->vk) {
      struct nvk_queue *queue = container_of(iter, struct nvk_queue, vk);
      nvk_queue_destroy(dev, queue);
   }

   if (dev->vab_memory)
      nvkmd_mem_unref(dev->vab_memory);

   if (dev->nvkmd) {
      /* Idle the upload queue before we tear down heaps */
      nvk_upload_queue_sync(dev, &dev->upload);

      nvk_slm_area_finish(&dev->slm);
      if (pdev->info.cls_eng3d < MAXWELL_B)
         nvk_heap_finish(dev, &dev->qmd_heap);
      nvk_heap_finish(dev, &dev->event_heap);
      nvk_heap_finish(dev, &dev->shader_heap);
      nvk_edb_bview_cache_finish(dev, &dev->edb_bview_cache);
      nvk_descriptor_table_finish(dev, &dev->samplers);
      nvk_descriptor_table_finish(dev, &dev->images);
      nvkmd_mem_unref(dev->zero_page);
      nvk_upload_queue_finish(dev, &dev->upload);
      nvkmd_dev_destroy(dev->nvkmd);
   }

   vk_device_finish(&dev->vk);
   vk_free(&dev->vk.alloc, dev);
}

VkResult
nvk_device_ensure_slm(struct nvk_device *dev,
                      uint32_t slm_bytes_per_lane,
                      uint32_t crs_bytes_per_warp)
{
   return nvk_slm_area_ensure(dev, &dev->slm,
                              slm_bytes_per_lane,
                              crs_bytes_per_warp);
}
