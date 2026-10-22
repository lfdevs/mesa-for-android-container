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
#if PAN_ARCH >= 10
#include "panvk_gpu_prerast.h"
#endif
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

#ifdef HAVE_PAN_KMOD_KBASE
#include <fcntl.h>
#endif

#include "kmod/pan_kmod.h"
#include "util/os_file.h"
#include "util/log.h"
#include "util/os_misc.h"
#include "util/u_printf.h"
#include "pan_props.h"
#include "pan_samples.h"
#include "poly/geometry.h"

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
      .create_flags =
         panvk_device_adjust_bo_flags(dev, PAN_KMOD_BO_FLAG_WB_MMAP),
      .slab_size = 16 * 1024,
      .label = "Device RW cached memory pool",
      .owns_bos = false,
      .needs_locking = true,
      .prealloc = false,
   };

   panvk_pool_init(&dev->mempools.rw, dev, NULL, NULL, &rw_pool_props);

   struct panvk_pool_properties rw_nc_pool_props = {
      .create_flags =
         panvk_device_adjust_bo_flags(dev, PAN_KMOD_BO_FLAG_GPU_UNCACHED),
      .slab_size = 16 * 1024,
      .label = "Device RW uncached memory pool",
      .owns_bos = false,
      .needs_locking = true,
      .prealloc = false,
   };

   panvk_pool_init(&dev->mempools.rw_nc, dev, NULL, NULL, &rw_nc_pool_props);

   struct panvk_pool_properties exec_pool_props = {
      .create_flags = panvk_device_adjust_bo_flags(
         dev, PAN_KMOD_BO_FLAG_EXECUTABLE | PAN_KMOD_BO_FLAG_WB_MMAP),
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

/* On JM, this is called during device initialization. On CSF, panthor doesn't
 * support alloc-on-fault, so we initialize the heap the first time it is used
 * in a command buffer to avoid wasting memory on the heap buffer on
 * applications that don't use it */
static VkResult
panvk_device_init_poly_heap_inner(struct panvk_device *dev)
{
#if PAN_ARCH >= 10
   uint32_t flags = PAN_KMOD_BO_FLAG_NO_MMAP;
#else
   uint32_t flags = PAN_KMOD_BO_FLAG_NO_MMAP | PAN_KMOD_BO_FLAG_ALLOC_ON_FAULT;
#endif

   VkResult result = panvk_priv_bo_create(
      dev, PANVK_POLY_HEAP_SIZE, flags, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE,
      &dev->poly_heap.buffer);
   if (result != VK_SUCCESS) {
      return result;
   }

   struct panvk_pool_alloc_info alloc_info = {
      .size = sizeof(struct poly_heap),
      .alignment = 8,
   };
   dev->poly_heap.state = panvk_pool_alloc_mem(&dev->mempools.rw, alloc_info);
   if (!panvk_priv_mem_check_alloc(dev->poly_heap.state)) {
      result = panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      panvk_priv_bo_unref(dev->poly_heap.buffer);
      dev->poly_heap.buffer = NULL;
      return result;
   }

   struct poly_heap *state_cpu = panvk_priv_mem_host_addr(dev->poly_heap.state);
   state_cpu->base = dev->poly_heap.buffer->addr.dev;
   state_cpu->bottom = 0;
   state_cpu->size = PANVK_POLY_HEAP_SIZE;

   return VK_SUCCESS;
}

VkResult
panvk_per_arch(device_init_poly_heap)(struct panvk_device *dev)
{
#if PAN_ARCH >= 10
   /* On CSF, we only try to initialize poly_heap the first time it is used
    * during command buffer recording. We can't use util_once_flag for this,
    * because it has no mechanism to handle failed initialization. So instead
    * we have to hand-roll something similar with a mutex and an atomic
    * flag. */
   if (unlikely(!p_atomic_read(&dev->poly_heap.initialized))) {
      simple_mtx_lock(&dev->poly_heap.init_lock);

      /* poly_heap was initialized successfully by another thread while we
       * waited to acquire the lock */
      if (p_atomic_read(&dev->poly_heap.initialized)) {
         simple_mtx_unlock(&dev->poly_heap.init_lock);
         return VK_SUCCESS;
      }

      VkResult result = panvk_device_init_poly_heap_inner(dev);
      /* Only set the flag if initialization was successful. If not, we want
       * to be able to try again */
      if (result == VK_SUCCESS)
         p_atomic_set(&dev->poly_heap.initialized, true);

      simple_mtx_unlock(&dev->poly_heap.init_lock);

      return result;
   }
   return VK_SUCCESS;
#else
   /* On JM, we only expect to call this function once, at device creation, so
    * there is no need for synchronization */
   assert(!dev->poly_heap.initialized);
   VkResult result = panvk_device_init_poly_heap_inner(dev);
   dev->poly_heap.initialized = true;
   return result;
#endif
}
static void
panvk_device_cleanup_tls(struct panvk_device *dev)
{
   util_dynarray_foreach(&dev->tls.bos, struct panvk_priv_bo *, bo)
      panvk_priv_bo_unref(*bo);
   util_dynarray_fini(&dev->tls.bos);
   simple_mtx_destroy(&dev->tls.lock);
}

#if PAN_ARCH >= 10
/* Commit the gpu_prerast arenas at creation.  On MediaTek kbase (CSF 1.21),
 * GPU-fault growth loses the first gpu_prerast draw's output: PanProbe
 * gs_viewport_depth case A drew nothing on every run; without alloc-on-fault,
 * 6/6 runs pass.
 *
 * csf-v11/205: "creation" is no longer the VkDevice's, which committed and
 * CPU-cleared 64 + 96 MiB in every VkDevice that enables GS, XFB,
 * tessellation, non-solid fill, viewport index or vertex stores (every
 * DXVK device), used or not. On CSF (v10+) each arena is created at the
 * first draw that records a gpu_prerast or tessellation pass, still fully
 * committed before any GPU access (the record happens before the submit),
 * so the fault growth problem above does not come back. The v9 JM
 * gpu_prerast paths read the arenas directly and keep the creation-time
 * commit. PANVK_GPU_PRERAST_ARENA=eager restores it on CSF too. */
static bool
gpu_prerast_arena_eager(void)
{
   const char *v = os_get_option("PANVK_GPU_PRERAST_ARENA");

   return PAN_ARCH < 10 || (v && !strcmp(v, "eager"));
}

static struct panvk_priv_mem
alloc_gpu_prerast_arena(struct panvk_device *dev, uint64_t size)
{
   uint32_t flags =
      panvk_device_adjust_bo_flags(dev, PAN_KMOD_BO_FLAG_WB_MMAP);
   struct panvk_priv_bo *bo;

   if (panvk_priv_bo_create(dev, size, flags, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE,
                            &bo) != VK_SUCCESS)
      return (struct panvk_priv_mem){0};

   struct panvk_priv_mem mem = {.bo = (uintptr_t)bo};
   panvk_priv_mem_write(mem, 0, struct panvk_gpu_prerast_arena, arena) {
      memset(arena, 0, sizeof(*arena));
   }
   return mem;
}

struct panvk_priv_mem
panvk_per_arch(device_gpu_prerast_arena)(struct panvk_device *dev, bool tess)
{
   struct panvk_priv_mem *mem =
      tess ? &dev->gpu_prerast_tess_arena : &dev->gpu_prerast_arena;

   if (!(tess ? dev->gpu_prerast_tess_wanted : dev->gpu_prerast_wanted))
      return (struct panvk_priv_mem){0};

   simple_mtx_lock(&dev->gpu_prerast_lock);
   if (!panvk_priv_mem_check_alloc(*mem)) {
      *mem = alloc_gpu_prerast_arena(
         dev, tess ? PANVK_GPU_PRERAST_TESS_ARENA_SIZE
                   : PANVK_GPU_PRERAST_ARENA_SIZE);
      if (panvk_priv_mem_check_alloc(*mem))
         mesa_logi("panvk: gpu_prerast %s arena committed (%llu MiB)",
                   tess ? "tessellation" : "draw",
                   (unsigned long long)((tess ? PANVK_GPU_PRERAST_TESS_ARENA_SIZE
                                              : PANVK_GPU_PRERAST_ARENA_SIZE) >> 20));
   }
   struct panvk_priv_mem ret = *mem;
   simple_mtx_unlock(&dev->gpu_prerast_lock);
   return ret;
}
#endif

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
#if PAN_ARCH >= 10
   device->meta.cmd_draw_rects = panvk_per_arch(cmd_draw_rects);
   device->meta.cmd_draw_volume = panvk_per_arch(cmd_draw_volume);
#endif

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
   const unsigned arch = pan_arch(phys_dev->kmod.dev->props.gpu_id);
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
         phys_dev->kmod.dev->props.allowed_group_priorities_mask;

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
      return panvk_bind_queue_check_status(queue);
   default:
      UNREACHABLE("Unknown queue family");
   }
}

static VkResult
panvk_device_check_status(struct vk_device *vk_dev)
{
   struct panvk_device *dev = to_panvk_device(vk_dev);
   VkResult result = vk_check_printf_status(&dev->vk, &dev->printf.ctx);

   vk_foreach_queue(queue, vk_dev) {
      if (panvk_queue_check_status(queue) != VK_SUCCESS)
         result = VK_ERROR_DEVICE_LOST;
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

#ifdef HAVE_PAN_KMOD_KBASE
/* Sparse binding over kbase (which has no DRM syncobjs and no explicit-VA
 * vm_bind) is not implemented yet: the BIND queue family is created as a
 * stub that fails at submit time with a clear message.  The GPU queue
 * family uses the real kbase CSF submission path. */
static bool
panvk_kbase_stub_queues(const struct panvk_device *dev)
{
   const struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);

   return phys_dev->kbase_node_path[0] != '\0';
}

static VkResult
panvk_kbase_stub_queue_submit(struct vk_queue *queue,
                              struct vk_queue_submit *submit)
{
   return vk_queue_set_lost(
      queue, "kbase: sparse binding queues are not implemented yet");
}

static VkResult
panvk_kbase_stub_queue_create(struct panvk_device *dev,
                              const VkDeviceQueueCreateInfo *create_info,
                              uint32_t queue_idx,
                              struct vk_queue **out_queue)
{
   struct vk_queue *queue = vk_zalloc(&dev->vk.alloc, sizeof(*queue), 8,
                                      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!queue)
      return panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = vk_queue_init(queue, &dev->vk, create_info, queue_idx);
   if (result != VK_SUCCESS) {
      vk_free(&dev->vk.alloc, queue);
      return result;
   }

   queue->driver_submit = panvk_kbase_stub_queue_submit;
   *out_queue = queue;
   return VK_SUCCESS;
}

static void
panvk_kbase_stub_queue_destroy(struct vk_queue *queue)
{
   struct vk_device *vk_dev = queue->base.device;

   vk_queue_finish(queue);
   vk_free(&vk_dev->alloc, queue);
}
#endif /* HAVE_PAN_KMOD_KBASE */

static VkResult
panvk_queue_create(struct panvk_device *dev,
                   const VkDeviceQueueCreateInfo *create_info,
                   uint32_t queue_idx,
                   struct vk_queue **out_queue)
{
#ifdef HAVE_PAN_KMOD_KBASE
   if (panvk_kbase_stub_queues(dev) &&
       create_info->queueFamilyIndex == PANVK_QUEUE_FAMILY_BIND)
      return panvk_kbase_stub_queue_create(dev, create_info, queue_idx,
                                           out_queue);
#endif

   switch (create_info->queueFamilyIndex) {
   case PANVK_QUEUE_FAMILY_GPU:
      return panvk_per_arch(create_gpu_queue)(
         dev, create_info, queue_idx, out_queue);
   case PANVK_QUEUE_FAMILY_BIND:
      return panvk_create_bind_queue(dev, create_info, queue_idx, out_queue);
   default:
      return panvk_error(dev, VK_ERROR_INITIALIZATION_FAILED);
   }
}

static void
panvk_queue_destroy(struct vk_queue *queue)
{
#ifdef HAVE_PAN_KMOD_KBASE
   struct panvk_device *dev = to_panvk_device(queue->base.device);

   if (panvk_kbase_stub_queues(dev) &&
       queue->queue_family_index == PANVK_QUEUE_FAMILY_BIND) {
      panvk_kbase_stub_queue_destroy(queue);
      return;
   }
#endif

   switch (queue->queue_family_index) {
   case PANVK_QUEUE_FAMILY_GPU:
      panvk_per_arch(destroy_gpu_queue)(queue);
      break;
   case PANVK_QUEUE_FAMILY_BIND:
      panvk_destroy_bind_queue(queue);
      break;
   default:
      UNREACHABLE("Unknown queue family");
   }
}

static void
init_va_heap(struct panvk_va_heap *heap, uint64_t start, uint64_t end)
{
   heap->start = start;
   heap->end = MAX2(end, start);
   util_vma_heap_init(&heap->heap, start, heap->end - heap->start);
}

static void
init_va_heaps(struct panvk_device *device, uint64_t user_va_start, uint64_t user_va_end)
{
   /* capture/replay requires a separate AS for fixed allocations. */
   init_va_heap(&device->as.heaps[PANVK_FIXED_VA_HEAP],
                device->vk.enabled_features.bufferDeviceAddressCaptureReplay
                   ? user_va_end / 2
                   : user_va_end,
                user_va_end);

   /* Non-executable heap starts at a 4G offset, and ends where the fixed heap
    * starts. If the fixed heap stars below the 4G boundary, then this heap
    * is empty. */
   init_va_heap(&device->as.heaps[PANVK_NO_EXEC_VA_HEAP], 1ull << 32,
                device->as.heaps[PANVK_FIXED_VA_HEAP].start);

   /* And Finally, we have the executable heap, which covers at most the
    * first 4G of the VA space. This is where we'll map all our executable
    * buffers, but non-executable buffers can also live here if there's no
    * space in the no-exec heap. */
   init_va_heap(&device->as.heaps[PANVK_EXEC_VA_HEAP], user_va_start,
                MIN3(user_va_end, device->as.heaps[PANVK_NO_EXEC_VA_HEAP].start,
                     device->as.heaps[PANVK_FIXED_VA_HEAP].start));
}

static void
cleanup_va_heaps(struct panvk_device *device)
{
   for (uint32_t i = 0; i < ARRAY_SIZE(device->as.heaps); i++)
      util_vma_heap_finish(&device->as.heaps[i].heap);
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

#ifdef HAVE_PAN_KMOD_KBASE
   if (physical_device->kbase_node_path[0]) {
      /* dup()ing a kbase fd would share the physical device's kbase
       * context, whose version handshake can only be done once; open a
       * fresh context instead.  A separate context also means a separate
       * GPU address space, which matches the per-logical-device VM
       * semantics of the DRM backends. */
      int kbase_fd =
         open(physical_device->kbase_node_path, O_RDWR | O_CLOEXEC);
      if (kbase_fd >= 0) {
         device->kmod.dev = pan_kmod_dev_create_with_driver(
            kbase_fd, physical_device->kmod.dev->flags, "kbase", NULL,
            &device->kmod.allocator);
         if (!device->kmod.dev)
            close(kbase_fd);
      }
   } else
#endif
   {
      device->kmod.dev = pan_kmod_dev_create(
         os_dupfd_cloexec(physical_device->kmod.dev->fd),
         physical_device->kmod.dev->flags, &device->kmod.allocator);
   }

   if (!device->kmod.dev) {
      result = panvk_errorf(instance, VK_ERROR_OUT_OF_HOST_MEMORY,
                            "cannot create device");
      goto err_finish_dev;
   }

   if (PANVK_DEBUG(TRACE) || PANVK_DEBUG(SYNC) || PANVK_DEBUG(DUMP) || PANVK_DEBUG(KBASE_DIAG)) {
      device->debug.decode_ctx = pandecode_create_context(false);
      pandecode_set_disassemble(device->debug.decode_ctx, pan_disassemble);
   }

   /* 48bit address space clamped by the physical device limits, with the lower
    * 32MB reserved. */
   uint64_t user_va_start = pan_clamp_to_usable_va_range(
      device->kmod.dev, PANVK_VA_RESERVE_BOTTOM);
   uint64_t user_va_end = physical_device->memory.max_supported_va;
   uint32_t vm_flags = PAN_ARCH < 10 ? PAN_KMOD_VM_FLAG_AUTO_VA : 0;

#ifdef HAVE_PAN_KMOD_KBASE
   /* kbase assigns all GPU VAs itself (SAME_VA memory model); mapping at a
    * caller-chosen address is not possible, so use AUTO_VA regardless of
    * the architecture. */
   if (physical_device->kbase_node_path[0])
      vm_flags = PAN_KMOD_VM_FLAG_AUTO_VA;
#endif

   device->kmod.vm =
      pan_kmod_vm_create(device->kmod.dev, vm_flags,
                         user_va_start, user_va_end - user_va_start);

   if (!device->kmod.vm) {
      result = panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_destroy_kdev;
   }

   init_va_heaps(device, user_va_start, user_va_end);

#if PAN_ARCH >= 10
   const struct drm_panthor_csif_info *csif_info =
      panvk_get_csif_props(device);

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

   panvk_device_init_mempools(device);
   simple_mtx_init(&device->tls.lock, mtx_plain);
   util_dynarray_init(&device->tls.bos, NULL);

#if PAN_ARCH >= 10
   if (PANVK_DEBUG(GPU_PRERAST) || device->vk.enabled_features.fillModeNonSolid ||
       device->vk.enabled_features.geometryShader ||
       device->vk.enabled_features.transformFeedback ||
       device->vk.enabled_features.shaderOutputViewportIndex ||
       device->vk.enabled_features.vertexPipelineStoresAndAtomics)
      device->gpu_prerast_wanted = true;
   device->gpu_prerast_tess_wanted =
      device->vk.enabled_features.tessellationShader;
   simple_mtx_init(&device->gpu_prerast_lock, mtx_plain);
   device->tess_opt.fuse = debug_get_num_option("PANVK_TESS_FUSE", 1) != 0;
   device->tess_opt.fuse_gen_max =
      debug_get_num_option("PANVK_TESS_FUSE_GEN_MAX", 64);
   device->tess_opt.setup_in_step =
      debug_get_num_option("PANVK_TESS_SETUP_STEP", 1) != 0;

   /* v9, or PANVK_GPU_PRERAST_ARENA=eager: commit both arenas here, as
    * before csf-v11/205 (64 + 96 MiB per VkDevice with the features). */
   if (gpu_prerast_arena_eager()) {
      if ((device->gpu_prerast_wanted &&
           !panvk_priv_mem_check_alloc(
              panvk_per_arch(device_gpu_prerast_arena)(device, false))) ||
          (device->gpu_prerast_tess_wanted &&
           !panvk_priv_mem_check_alloc(
              panvk_per_arch(device_gpu_prerast_arena)(device, true)))) {
         result = panvk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         goto err_free_priv_bos;
      }
   }
   if (device->gpu_prerast_wanted || device->gpu_prerast_tess_wanted) {
      result = panvk_priv_bo_create(
         device, sizeof(struct panvk_gpu_prerast_syncs),
         device->kmod.dev->props.supported_bo_flags &
            PAN_KMOD_BO_FLAG_CSF_EVENT,
         VK_SYSTEM_ALLOCATION_SCOPE_DEVICE, &device->gpu_prerast_syncs);
      if (result != VK_SUCCESS)
         goto err_free_priv_bos;

      struct panvk_gpu_prerast_syncs *syncs =
         device->gpu_prerast_syncs->addr.host;
      *syncs = (struct panvk_gpu_prerast_syncs){
         .arena = {1},
         .tess_arena = {1},
      };
   }
#endif

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
#endif

   result = panvk_priv_bo_create(
      device, pan_sample_positions_buffer_size(),
      panvk_device_adjust_bo_flags(device, PAN_KMOD_BO_FLAG_WB_MMAP),
      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE, &device->sample_positions);
   if (result != VK_SUCCESS)
      goto err_free_priv_bos;

   pan_upload_sample_positions(device->sample_positions->addr.host);
   panvk_priv_bo_flush(device->sample_positions, 0,
                       pan_sample_positions_buffer_size());

#if PAN_ARCH >= 10
   simple_mtx_init(&device->poly_heap.init_lock, mtx_plain);
#else
   result = panvk_per_arch(device_init_poly_heap)(device);
   if (result != VK_SUCCESS)
      goto err_free_priv_bos;
#endif

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
   device->vk.sync =  pan_kmod_sync_clone(device->kmod.dev);

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

#if PAN_ARCH >= 10 && PAN_ARCH < 14
   result = panvk_per_arch(device_draw_context_init)(device);
   if (result != VK_SUCCESS)
      goto err_free_mem_cache;
#endif

   result = panvk_meta_init(device);
   if (result != VK_SUCCESS)
      goto err_free_draw_ctx;

   result = panvk_per_arch(create_depth_bounds_fs)(device);
   if (result != VK_SUCCESS)
      goto err_finish_queues;

   for (unsigned i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
      const VkDeviceQueueCreateInfo *queue_create =
         &pCreateInfo->pQueueCreateInfos[i];

      result = check_global_priority(physical_device, queue_create);
      if (result != VK_SUCCESS)
         goto err_finish_queues;

      for (unsigned q = 0; q < queue_create->queueCount; q++) {
         struct vk_queue *queue;
         result = panvk_queue_create(device, queue_create, q, &queue);
         if (result != VK_SUCCESS)
            goto err_finish_queues;
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
   vk_foreach_queue_safe(queue, &device->vk)
      panvk_queue_destroy(queue);

   if (device->depth_bounds_fs)
      vk_shader_destroy(&device->vk, &device->depth_bounds_fs->vk, NULL);
   panvk_meta_cleanup(device);

err_free_draw_ctx:
#if PAN_ARCH >= 10 && PAN_ARCH < 14
   panvk_per_arch(device_draw_context_cleanup)(device);
err_free_mem_cache:
#endif
   vk_pipeline_cache_destroy(device->vk.mem_cache, NULL);
err_free_precomp:
   panvk_precomp_cleanup(device);
err_free_priv_bos:
   panvk_device_cleanup_tls(device);
   panvk_priv_bo_unref(device->gpu_prerast_syncs);
   panvk_pool_free_mem(&device->gpu_prerast_tess_arena);
   panvk_pool_free_mem(&device->gpu_prerast_arena);
   if (device->printf.bo)
      u_printf_destroy(&device->printf.ctx);
   panvk_priv_bo_unref(device->printf.bo);
   panvk_priv_bo_unref(device->tiler_oom.handlers_bo);
   panvk_priv_bo_unref(device->sample_positions);
   panvk_pool_free_mem(&device->poly_heap.state);
   panvk_priv_bo_unref(device->poly_heap.buffer);
#if PAN_ARCH >= 10
   simple_mtx_destroy(&device->poly_heap.init_lock);
#endif
   panvk_priv_bo_unref(device->tiler_heap);
   panvk_device_cleanup_mempools(device);
   vk_free(&device->vk.alloc, device->dump_region_size);
   pan_kmod_vm_destroy(device->kmod.vm);
   cleanup_va_heaps(device);
   simple_mtx_destroy(&device->as.lock);

err_destroy_kdev:
   if (device->debug.decode_ctx)
      pandecode_destroy_context(device->debug.decode_ctx);

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

   vk_foreach_queue_safe(queue, &device->vk)
      panvk_queue_destroy(queue);

   panvk_precomp_cleanup(device);
#if PAN_ARCH >= 10 && PAN_ARCH < 14
   panvk_per_arch(device_draw_context_cleanup)(device);
#endif
   if (device->depth_bounds_fs)
      vk_shader_destroy(&device->vk, &device->depth_bounds_fs->vk, NULL);
   panvk_per_arch(device_bc_finish)(device);
   vk_pipeline_cache_destroy(device->vk.mem_cache, NULL);
   pan_kmod_bo_put(device->sparse_mem.blackhole);
   u_printf_destroy(&device->printf.ctx);
   panvk_priv_bo_unref(device->printf.bo);
   panvk_priv_bo_unref(device->tiler_oom.handlers_bo);
   panvk_pool_free_mem(&device->poly_heap.state);
   panvk_priv_bo_unref(device->poly_heap.buffer);
#if PAN_ARCH >= 10
   simple_mtx_destroy(&device->poly_heap.init_lock);
#endif
   panvk_priv_bo_unref(device->tiler_heap);
   panvk_priv_bo_unref(device->sample_positions);
   panvk_priv_bo_unref(device->indirect_varying_buffer);
   panvk_device_cleanup_tls(device);
   panvk_priv_bo_unref(device->gpu_prerast_syncs);
   panvk_pool_free_mem(&device->gpu_prerast_tess_arena);
   panvk_pool_free_mem(&device->gpu_prerast_arena);
#if PAN_ARCH >= 10
   simple_mtx_destroy(&device->gpu_prerast_lock);
#endif
   panvk_device_cleanup_mempools(device);
   vk_free(&device->vk.alloc, device->dump_region_size);
   pan_kmod_vm_destroy(device->kmod.vm);
   cleanup_va_heaps(device);
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
