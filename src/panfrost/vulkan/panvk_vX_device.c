/*
 * Copyright © 2021 Collabora Ltd.
 * Copyright © 2024 Arm Ltd.
 *
 * Derived from tu_image.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "drm-uapi/panthor_drm.h"

#include "vk_cmd_enqueue_entrypoints.h"
#include "vk_common_entrypoints.h"
#include "vk_drm_syncobj.h"

#include "panvk_buffer.h"
#include "panvk_cmd_alloc.h"
#include "panvk_cmd_buffer.h"
#include "panvk_device.h"
#include "panvk_cmd_draw.h"
#include "panvk_entrypoints.h"
#include "panvk_instance.h"
#include "panvk_macros.h"
#include "panvk_physical_device.h"
#include "panvk_precomp_cache.h"
#include "panvk_priv_bo.h"
#include "panvk_queue.h"
#include "panvk_utrace.h"
#include "panvk_utrace_perfetto.h"

#include "genxml/decode.h"
#include "genxml/gen_macros.h"

#include "kmod/pan_kmod.h"
#include "util/os_file.h"
#include "util/u_printf.h"
#include "pan_props.h"
#include "pan_samples.h"

static void *
panvk_kmod_zalloc(const struct pan_kmod_allocator *allocator, size_t size,
                  bool transient)
{
   const VkAllocationCallbacks *vkalloc = allocator->priv;

   void *obj = vk_zalloc(vkalloc, size, 8,
                         transient ? VK_SYSTEM_ALLOCATION_SCOPE_COMMAND
                                   : VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   /* We force errno to -ENOMEM on host allocation failures so we can properly
    * report it back as VK_ERROR_OUT_OF_HOST_MEMORY. */
   if (!obj)
      errno = -ENOMEM;

   return obj;
}

static void
panvk_kmod_free(const struct pan_kmod_allocator *allocator, void *data)
{
   const VkAllocationCallbacks *vkalloc = allocator->priv;

   return vk_free(vkalloc, data);
}

static void
panvk_device_init_mempools(struct panvk_device *dev)
{
   struct panvk_pool_properties rw_pool_props = {
      .create_flags = 0,
      .slab_size = 16 * 1024,
      .label = "Device RW cached memory pool",
      .owns_bos = false,
      .needs_locking = true,
      .prealloc = false,
   };

   panvk_pool_init(&dev->mempools.rw, dev, NULL, NULL, &rw_pool_props);

   struct panvk_pool_properties rw_nc_pool_props = {
      .create_flags = PAN_ARCH <= 9 ? 0 : PAN_KMOD_BO_FLAG_GPU_UNCACHED,
      .slab_size = 16 * 1024,
      .label = "Device RW uncached memory pool",
      .owns_bos = false,
      .needs_locking = true,
      .prealloc = false,
   };

   panvk_pool_init(&dev->mempools.rw_nc, dev, NULL, NULL, &rw_nc_pool_props);

   struct panvk_pool_properties exec_pool_props = {
      .create_flags = PAN_KMOD_BO_FLAG_EXECUTABLE,
      .slab_size = 16 * 1024,
      .label = "Device executable memory pool (shaders)",
      .owns_bos = false,
      .needs_locking = true,
      .prealloc = false,
   };

   panvk_pool_init(&dev->mempools.exec, dev, NULL, NULL, &exec_pool_props);
}

static void
panvk_device_cleanup_mempools(struct panvk_device *dev)
{
   panvk_pool_cleanup(&dev->mempools.rw);
   panvk_pool_cleanup(&dev->mempools.rw_nc);
   panvk_pool_cleanup(&dev->mempools.exec);
}

static VkResult
panvk_meta_cmd_bind_map_buffer(struct vk_command_buffer *cmd,
                               struct vk_meta_device *meta, VkBuffer buf,
                               void **map_out)
{
   VK_FROM_HANDLE(panvk_buffer, buffer, buf);
   struct panvk_cmd_buffer *cmdbuf =
      container_of(cmd, struct panvk_cmd_buffer, vk);
   struct pan_ptr mem =
      panvk_cmd_alloc_dev_mem(cmdbuf, desc, buffer->vk.size, 64);

   if (!mem.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   assert(buffer->vk.device_address == 0);
   buffer->vk.device_address = mem.gpu;
   *map_out = mem.cpu;
   return VK_SUCCESS;
}

static VkResult
panvk_meta_init(struct panvk_device *device)
{
   const struct vk_physical_device *pdev = device->vk.physical;

   VkResult result = vk_meta_device_init(&device->vk, &device->meta);
   if (result != VK_SUCCESS)
      return result;

   device->meta.use_stencil_export = true;
   device->meta.use_rect_list_pipeline = true;
   device->meta.max_bind_map_buffer_size_B = 64 * 1024;
   device->meta.cmd_bind_map_buffer = panvk_meta_cmd_bind_map_buffer;

   /* Assume a maximum of 1024 bytes per worgroup and choose the workgroup size
    * accordingly. */
   for (uint32_t i = 0;
        i < ARRAY_SIZE(device->meta.buffer_access.optimal_wg_size); i++) {
      device->meta.buffer_access.optimal_wg_size[i] =
         MIN2(1024 >> i, pdev->properties.maxComputeWorkGroupSize[0]);
   }

   return VK_SUCCESS;
}

static void
panvk_meta_cleanup(struct panvk_device *device)
{
   vk_meta_device_finish(&device->vk, &device->meta);
}

static VkResult
panvk_precomp_init(struct panvk_device *device)
{
   device->precomp_cache = panvk_per_arch(precomp_cache_init)(device);

   if (device->precomp_cache == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   return VK_SUCCESS;
}

static void
panvk_precomp_cleanup(struct panvk_device *device)
{
   panvk_per_arch(precomp_cache_cleanup)(device->precomp_cache);
}

/* Always reserve the lower 32MB. */
#define PANVK_VA_RESERVE_BOTTOM 0x2000000ull

static enum pan_kmod_group_allow_priority_flags
global_priority_to_group_allow_priority_flag(
   VkQueueGlobalPriorityKHR priority)
{
   switch (priority) {
   case VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR:
      return PAN_KMOD_GROUP_ALLOW_PRIORITY_LOW;
   case VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR:
      return PAN_KMOD_GROUP_ALLOW_PRIORITY_MEDIUM;
   case VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR:
      return PAN_KMOD_GROUP_ALLOW_PRIORITY_HIGH;
   case VK_QUEUE_GLOBAL_PRIORITY_REALTIME_KHR:
      return PAN_KMOD_GROUP_ALLOW_PRIORITY_REALTIME;
   default:
      UNREACHABLE("Invalid global priority");
   }
}

static VkResult
check_global_priority(const struct panvk_physical_device *phys_dev,
                      const VkDeviceQueueCreateInfo *create_info)
{
   const unsigned arch = pan_arch(phys_dev->kmod.props.gpu_id);
   const VkDeviceQueueGlobalPriorityCreateInfoKHR *priority_info =
      vk_find_struct_const(create_info->pNext,
                           DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR);
   const VkQueueGlobalPriorityKHR priority =
      priority_info ? priority_info->globalPriority
                    : VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR;

   switch (create_info->queueFamilyIndex) {
   case PANVK_QUEUE_FAMILY_GPU: {
      enum pan_kmod_group_allow_priority_flags requested_prio =
         global_priority_to_group_allow_priority_flag(priority);
      enum pan_kmod_group_allow_priority_flags allowed_prio_mask =
         phys_dev->kmod.props.allowed_group_priorities_mask;

      /* Non-medium priority context is not hooked-up in the JM backend, even
       * though the panfrost kmod advertize it. Manually filter non-medium
       * priority for now.
       */
      if (arch < 10)
         allowed_prio_mask &= PAN_KMOD_GROUP_ALLOW_PRIORITY_MEDIUM;

      if (requested_prio & allowed_prio_mask)
         return VK_SUCCESS;

      return VK_ERROR_NOT_PERMITTED_KHR;
   }

   case PANVK_QUEUE_FAMILY_BIND: {
      if (priority == VK_QUEUE_GLOBAL_PRIORITY_MEDIUM)
         return VK_SUCCESS;

      return VK_ERROR_NOT_PERMITTED_KHR;
   }

   default:
      UNREACHABLE("Unknown queue family");
   }
}

static VkResult
panvk_queue_check_status(struct vk_queue *queue)
{
   switch (queue->queue_family_index) {
   case PANVK_QUEUE_FAMILY_GPU:
      return panvk_per_arch(gpu_queue_check_status)(queue);
   case PANVK_QUEUE_FAMILY_BIND:
      return panvk_per_arch(bind_queue_check_status)(queue);
   default:
      UNREACHABLE("Unknown queue family");
   }
}

static VkResult
panvk_device_check_status(struct vk_device *vk_dev)
{
   struct panvk_device *dev = to_panvk_device(vk_dev);
   VkResult result = vk_check_printf_status(&dev->vk, &dev->printf.ctx);

   for (uint32_t qfi = 0; qfi < PANVK_QUEUE_FAMILY_COUNT; qfi++) {
      struct panvk_device_queue_family *qf = &dev->queue_families[qfi];

      for (uint32_t q = 0; q < qf->queue_count; q++) {
         struct vk_queue *queue = qf->queues[q];

         if (panvk_queue_check_status(queue) != VK_SUCCESS)
            result = VK_ERROR_DEVICE_LOST;
      }
   }

   if (pan_kmod_vm_query_state(dev->kmod.vm) != PAN_KMOD_VM_USABLE) {
      vk_device_set_lost(&dev->vk, "vm state: not usable");
      result = VK_ERROR_DEVICE_LOST;
   }

   return result;
}

static VkResult
panvk_device_get_timestamp(struct vk_device *vk_dev, uint64_t *timestamp)
{
   struct panvk_physical_device *pdev =
      to_panvk_physical_device(vk_dev->physical);

   *timestamp = pan_kmod_query_timestamp(pdev->kmod.dev);

   return VK_SUCCESS;
}

static VkResult
panvk_queue_create(struct panvk_device *dev,
                   const VkDeviceQueueCreateInfo *create_info,
                   uint32_t queue_idx,
                   struct vk_queue **out_queue)
{
   switch (create_info->queueFamilyIndex) {
   case PANVK_QUEUE_FAMILY_GPU:
      return panvk_per_arch(create_gpu_queue)(
         dev, create_info, queue_idx, out_queue);
   case PANVK_QUEUE_FAMILY_BIND:
      return panvk_per_arch(create_bind_queue)(
         dev, create_info, queue_idx, out_queue);
   default:
      return panvk_error(dev, VK_ERROR_INITIALIZATION_FAILED);
   }
}

static void
panvk_queue_destroy(struct vk_queue *queue)
{
   switch (queue->queue_family_index) {
   case PANVK_QUEUE_FAMILY_GPU:
      panvk_per_arch(destroy_gpu_queue)(queue);
      break;
   case PANVK_QUEUE_FAMILY_BIND:
      panvk_per_arch(destroy_bind_queue)(queue);
      break;
   default:
      UNREACHABLE("Unknown queue family");
   }
}

VkResult
panvk_per_arch(create_device)(struct panvk_physical_device *physical_device,
                              const VkDeviceCreateInfo *pCreateInfo,
                              const VkAllocationCallbacks *pAllocator,
                              VkDevice *pDevice)
{
   struct panvk_instance *instance =
      to_panvk_instance(physical_device->vk.instance);
   VkResult result;
   struct panvk_device *device;

   device = vk_zalloc2(&instance->vk.alloc, pAllocator, sizeof(*device), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device)
      return panvk_error(physical_device, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_device_dispatch_table dispatch_table;



   if (PAN_ARCH <= 9) {
      /* For secondary command buffer support, overwrite any command entrypoints
       * in the main device-level dispatch table with
       * vk_cmd_enqueue_unless_primary_Cmd*.
       */
      vk_device_dispatch_table_from_entrypoints(
         &dispatch_table, &vk_cmd_enqueue_unless_primary_device_entrypoints, true);

      /* Populate our primary cmd_dispatch table. */
      vk_device_dispatch_table_from_entrypoints(
         &device->cmd_dispatch, &panvk_per_arch(device_entrypoints), true);
      vk_device_dispatch_table_from_entrypoints(&device->cmd_dispatch,
                                                &panvk_device_entrypoints,
                                                false);
      vk_device_dispatch_table_from_entrypoints(&device->cmd_dispatch,
		                                &vk_common_device_entrypoints,
                                                false);
   }

   vk_device_dispatch_table_from_entrypoints(
      &dispatch_table, &panvk_per_arch(device_entrypoints), PAN_ARCH > 9);
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                             &panvk_device_entrypoints, false);
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                             &wsi_device_entrypoints, false);

   result = vk_device_init(&device->vk, &physical_device->vk, &dispatch_table,
                           pCreateInfo, pAllocator);
   if (result != VK_SUCCESS)
      goto err_free_dev;

   /* Must be done after vk_device_init() because this function memset(0) the
    * whole struct.
    */
   device->vk.command_dispatch_table = &device->cmd_dispatch;
   device->vk.command_buffer_ops = &panvk_per_arch(cmd_buffer_ops);
   device->vk.shader_ops = &panvk_per_arch(device_shader_ops);
   device->vk.check_status = panvk_device_check_status;
   device->vk.get_timestamp = panvk_device_get_timestamp;
   device->vk.copy_sync_payloads = vk_drm_syncobj_copy_payloads;

   device->kmod.allocator = (struct pan_kmod_allocator){
      .zalloc = panvk_kmod_zalloc,
      .free = panvk_kmod_free,
      .priv = &device->vk.alloc,
   };
   device->kmod.dev =
      pan_kmod_dev_create(os_dupfd_cloexec(physical_device->kmod.dev->fd),
                          PAN_KMOD_DEV_FLAG_OWNS_FD, &device->kmod.allocator);

   if (!device->kmod.dev) {
      result = panvk_errorf(instance, VK_ERROR_OUT_OF_HOST_MEMORY,
                            "cannot create device");
      goto err_finish_dev;
   }

   if (PANVK_DEBUG(TRACE) || PANVK_DEBUG(SYNC) || PANVK_DEBUG(DUMP))
      device->debug.decode_ctx = pandecode_create_context(false);

   /* 32bit address space, with the lower 32MB reserved. We clamp
    * things so it matches kmod VA range limitations.
    */
   uint64_t user_va_start = pan_clamp_to_usable_va_range(
      device->kmod.dev, PANVK_VA_RESERVE_BOTTOM);
   uint64_t user_va_end =
      pan_clamp_to_usable_va_range(device->kmod.dev, 1ull << 32);
   uint32_t vm_flags = PAN_ARCH < 9 ? PAN_KMOD_VM_FLAG_AUTO_VA : 0;

   device->kmod.vm =
      pan_kmod_vm_create(device->kmod.dev, vm_flags,
                         user_va_start, user_va_end - user_va_start);

   if (!device->kmod.vm) {
      result = panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_destroy_kdev;
   }

#if PAN_ARCH >= 10
   const struct drm_panthor_csif_info *csif_info =
      panthor_kmod_get_csif_props(device->kmod.dev);

   assert(csif_info->scoreboard_slot_count <= 16);
   device->csf.sb.count = csif_info->scoreboard_slot_count;
   device->csf.sb.all_mask = (uint16_t)BITFIELD_MASK(csif_info->scoreboard_slot_count);

   assert(device->csf.sb.count > PANVK_SB_ITER_START);
   device->csf.sb.iter_count = device->csf.sb.count - PANVK_SB_ITER_START;

#if PAN_ARCH == 10
   device->csf.sb.iter_count =
      MIN2(device->csf.sb.iter_count, PANVK_SB_ITER_COUNT);
#endif

   device->csf.sb.all_iters_mask =
      BITFIELD_RANGE(PANVK_SB_ITER_START, device->csf.sb.iter_count);
#endif

   simple_mtx_init(&device->as.lock, mtx_plain);
   util_vma_heap_init(&device->as.heap, user_va_start,
                      user_va_end - user_va_start);

   panvk_device_init_mempools(device);

#if PAN_ARCH >= 10
   /* The only reason this is a heap allocation is that PANVK_SUBQUEUE_COUNT
    * isn't available in the header */
   device->dump_region_size =
      vk_zalloc(&device->vk.alloc, PANVK_SUBQUEUE_COUNT * sizeof(uint32_t),
                alignof(uint32_t), VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device->dump_region_size) {
      result = panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_free_priv_bos;
   }
#endif

#if PAN_ARCH <= 9
   result = panvk_priv_bo_create(
      device, 128 * 1024 * 1024,
      PAN_KMOD_BO_FLAG_NO_MMAP | PAN_KMOD_BO_FLAG_ALLOC_ON_FAULT,
      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE, &device->tiler_heap);
   if (result != VK_SUCCESS)
      goto err_free_priv_bos;

   result = panvk_priv_bo_create(
      device,
      PANVK_JM_MAX_VERTICES_INDIRECT * PANVK_JM_MAX_PER_VTX_ATTRIBUTES_INDIRECT_SIZE,
      PAN_KMOD_BO_FLAG_NO_MMAP | PAN_KMOD_BO_FLAG_ALLOC_ON_FAULT,
      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE, &device->indirect_varying_buffer);
   if (result != VK_SUCCESS)
      goto err_free_priv_bos;
#endif

   result = panvk_priv_bo_create(
      device, pan_sample_positions_buffer_size(), 0,
      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE, &device->sample_positions);
   if (result != VK_SUCCESS)
      goto err_free_priv_bos;

   pan_upload_sample_positions(device->sample_positions->addr.host);

#if PAN_ARCH >= 10
   result = panvk_per_arch(init_tiler_oom)(device);
   if (result != VK_SUCCESS)
      goto err_free_priv_bos;
#endif

   result = panvk_priv_bo_create(device, PAN_PRINTF_BUFFER_SIZE, 0,
                                 VK_SYSTEM_ALLOCATION_SCOPE_DEVICE,
                                 &device->printf.bo);
   if (result != VK_SUCCESS)
      goto err_free_priv_bos;

   u_printf_init(&device->printf.ctx, device->printf.bo,
                 device->printf.bo->addr.host);

   device->drm_fd = device->kmod.dev->fd;
   vk_device_set_drm_fd(&device->vk, device->kmod.dev->fd);


   result = panvk_precomp_init(device);
   if (result != VK_SUCCESS)
      goto err_free_priv_bos;

   struct vk_pipeline_cache_create_info cache_info = {
      .weak_ref = true,
   };
   device->vk.mem_cache = vk_pipeline_cache_create(&device->vk, &cache_info, NULL);
   if (device->vk.mem_cache == NULL) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto err_free_precomp;
   }

#if PAN_ARCH >= 10
   result = panvk_per_arch(device_draw_context_init)(device);
   if (result != VK_SUCCESS)
      goto err_free_mem_cache;
#endif

   result = panvk_meta_init(device);
   if (result != VK_SUCCESS)
      goto err_free_draw_ctx;

   for (unsigned i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
      const VkDeviceQueueCreateInfo *queue_create =
         &pCreateInfo->pQueueCreateInfos[i];

      result = check_global_priority(physical_device, queue_create);
      if (result != VK_SUCCESS)
         goto err_finish_queues;

      uint32_t qfi = queue_create->queueFamilyIndex;
      struct panvk_device_queue_family *qf = &device->queue_families[qfi];

      qf->queues =
         vk_zalloc(&device->vk.alloc,
                   queue_create->queueCount * sizeof(qf->queues[0]), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
      if (!qf->queues) {
         result = panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
         goto err_finish_queues;
      }

      for (unsigned q = 0; q < queue_create->queueCount; q++) {
         result = panvk_queue_create(device, queue_create, q, &qf->queues[q]);
         if (result != VK_SUCCESS)
            goto err_finish_queues;

         qf->queue_count++;
      }
   }

   result = panvk_per_arch(utrace_context_init)(device);
   if (result != VK_SUCCESS)
      goto err_finish_queues;

#if PAN_ARCH >= 10
   panvk_utrace_perfetto_init(device, PANVK_SUBQUEUE_COUNT);
#else
   panvk_utrace_perfetto_init(device, 2);
#endif

   *pDevice = panvk_device_to_handle(device);
   return VK_SUCCESS;

err_finish_queues:
   for (unsigned i = 0; i < PANVK_QUEUE_FAMILY_COUNT; i++) {
      struct panvk_device_queue_family *qf = &device->queue_families[i];

      for (unsigned q = 0; q < qf->queue_count; q++)
         panvk_queue_destroy(qf->queues[q]);

      if (qf->queues)
         vk_free(&device->vk.alloc, qf->queues);
   }

   panvk_meta_cleanup(device);

err_free_draw_ctx:
#if PAN_ARCH >= 10
   panvk_per_arch(device_draw_context_cleanup)(device);
err_free_mem_cache:
#endif
   vk_pipeline_cache_destroy(device->vk.mem_cache, NULL);
err_free_precomp:
   panvk_precomp_cleanup(device);
err_free_priv_bos:
   if (device->printf.bo)
      u_printf_destroy(&device->printf.ctx);
   panvk_priv_bo_unref(device->printf.bo);
   panvk_priv_bo_unref(device->tiler_oom.handlers_bo);
   panvk_priv_bo_unref(device->sample_positions);
   panvk_priv_bo_unref(device->indirect_varying_buffer);
   panvk_priv_bo_unref(device->tiler_heap);
   panvk_device_cleanup_mempools(device);
   vk_free(&device->vk.alloc, device->dump_region_size);
   pan_kmod_vm_destroy(device->kmod.vm);
   util_vma_heap_finish(&device->as.heap);
   simple_mtx_destroy(&device->as.lock);

err_destroy_kdev:
   pan_kmod_dev_destroy(device->kmod.dev);

err_finish_dev:
   vk_device_finish(&device->vk);

err_free_dev:
   vk_free(&device->vk.alloc, device);
   return result;
}

void
panvk_per_arch(destroy_device)(struct panvk_device *device,
                               const VkAllocationCallbacks *pAllocator)
{
   if (!device)
      return;

   panvk_per_arch(utrace_context_fini)(device);

   for (unsigned i = 0; i < PANVK_QUEUE_FAMILY_COUNT; i++) {
      struct panvk_device_queue_family *qf = &device->queue_families[i];

      for (unsigned q = 0; q < qf->queue_count; q++)
         panvk_queue_destroy(qf->queues[q]);

      if (qf->queues)
         vk_free(&device->vk.alloc, qf->queues);
   }

   panvk_precomp_cleanup(device);
#if PAN_ARCH >= 10
   panvk_per_arch(device_draw_context_cleanup)(device);
#endif
   panvk_meta_cleanup(device);
   vk_pipeline_cache_destroy(device->vk.mem_cache, NULL);
   pan_kmod_bo_put(device->sparse_mem.blackhole);
   u_printf_destroy(&device->printf.ctx);
   panvk_priv_bo_unref(device->printf.bo);
   panvk_priv_bo_unref(device->tiler_oom.handlers_bo);
   panvk_priv_bo_unref(device->indirect_varying_buffer);
   panvk_priv_bo_unref(device->tiler_heap);
   panvk_priv_bo_unref(device->sample_positions);
   panvk_device_cleanup_mempools(device);
   vk_free(&device->vk.alloc, device->dump_region_size);
   pan_kmod_vm_destroy(device->kmod.vm);
   util_vma_heap_finish(&device->as.heap);
   simple_mtx_destroy(&device->as.lock);

   if (device->debug.decode_ctx)
      pandecode_destroy_context(device->debug.decode_ctx);

   pan_kmod_dev_destroy(device->kmod.dev);
   vk_device_finish(&device->vk);
   vk_free(&device->vk.alloc, device);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(GetRenderAreaGranularity)(VkDevice device,
                                         VkRenderPass renderPass,
                                         VkExtent2D *pGranularity)
{
   *pGranularity = (VkExtent2D){32, 32};
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(GetRenderingAreaGranularityKHR)(
   VkDevice _device, const VkRenderingAreaInfoKHR *pRenderingAreaInfo,
   VkExtent2D *pGranularity)
{
   *pGranularity = (VkExtent2D){32, 32};
}
