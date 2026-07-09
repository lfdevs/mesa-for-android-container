/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_wsi.h"

#include "vk_enum_to_str.h"
#include "wsi_common_entrypoints.h"

#include "vn_device.h"
#include "vn_image.h"
#include "vn_instance.h"
#include "vn_physical_device.h"
#include "vn_queue.h"

#ifndef DRM_FORMAT_MOD_LINEAR
#define DRM_FORMAT_MOD_LINEAR 0
#endif

/* The common WSI support makes some assumptions about the driver.
 *
 * In wsi_device_init, it assumes VK_EXT_pci_bus_info is available.  In
 * wsi_create_native_image and wsi_create_prime_image, it assumes
 * VK_KHR_external_memory_fd and VK_EXT_external_memory_dma_buf are enabled.
 *
 * In wsi_create_native_image, if wsi_device::supports_modifiers is set and
 * the window system supports modifiers, it assumes
 * VK_EXT_image_drm_format_modifier is enabled.  Otherwise, it assumes that
 * wsi_image_create_info can be chained to VkImageCreateInfo and
 * vkGetImageSubresourceLayout can be called even the tiling is
 * VK_IMAGE_TILING_OPTIMAL.
 *
 * Together, it knows how to share dma-bufs, with explicit or implicit
 * modifiers, to the window system.
 *
 * For venus, we use explicit modifiers when the renderer and the window
 * system support them.  Otherwise, we have to fall back to
 * VK_IMAGE_TILING_LINEAR (or trigger the prime blit path).  But the fallback
 * can be problematic when the memory is scanned out directly and special
 * requirements (e.g., alignments) must be met.
 *
 * For venus, implicit fencing is broken (and there is no explicit fencing
 * support yet).  The kernel driver assumes everything is in the same fence
 * context and no synchronization is needed.  It should be fixed for
 * correctness, but it is still not ideal.  venus requires explicit fencing
 * (and renderer-side synchronization) to work well.
 */

/* cast a WSI object to a pointer for logging */
#define VN_WSI_PTR(obj) ((const void *)(uintptr_t)(obj))

struct vn_swapchain {
   VkSwapchainKHR handle;
   /* lock chain access */
   simple_mtx_t mutex;
   /* sub-lock for image acquire */
   simple_mtx_t acquire_mutex;

   struct list_head head;
};

static struct vn_swapchain *
vn_wsi_chain_lookup(struct vn_device *dev, VkSwapchainKHR swapchain)
{
   struct vn_swapchain *chain = NULL;

   simple_mtx_lock(&dev->mutex);
   list_for_each_entry(struct vn_swapchain, entry, &dev->chains, head) {
      if (entry->handle == swapchain) {
         chain = entry;
         break;
      }
   }
   simple_mtx_unlock(&dev->mutex);

   assert(chain);
   return chain;
}

static void
vn_wsi_chains_lock(struct vn_device *dev,
                   const VkPresentInfoKHR *pi,
                   bool all)
{
   for (uint32_t i = 0; i < pi->swapchainCount; i++) {
      struct vn_swapchain *chain =
         vn_wsi_chain_lookup(dev, pi->pSwapchains[i]);

      assert(chain);

      if (all)
         simple_mtx_lock(&chain->mutex);

      simple_mtx_lock(&chain->acquire_mutex);
   }
}

static void
vn_wsi_chains_unlock(struct vn_device *dev,
                     const VkPresentInfoKHR *pi,
                     bool all)
{
   for (uint32_t i = 0; i < pi->swapchainCount; i++) {
      struct vn_swapchain *chain =
         vn_wsi_chain_lookup(dev, pi->pSwapchains[i]);

      assert(chain);

      simple_mtx_unlock(&chain->acquire_mutex);

      if (all)
         simple_mtx_unlock(&chain->mutex);
   }
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vn_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char *pName)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);
   return vk_instance_get_proc_addr_unchecked(
      &physical_dev->instance->base.vk, pName);
}

VkResult
vn_wsi_init(struct vn_physical_device *physical_dev)
{
   const bool use_sw_device =
      !physical_dev->base.vk.supported_extensions
          .EXT_external_memory_dma_buf ||
      (physical_dev->renderer_driver_id == VK_DRIVER_ID_NVIDIA_PROPRIETARY &&
       physical_dev->renderer_driver_version <
          VN_MAKE_NVIDIA_VERSION(590, 48, 1, 0));

   /* Normally Venus on Nvidia GPUs takes the prime blit path. The exception
    * is when KWin or any wlroots based compositors are used:
    * 1. KWin and wlroots based compositors always add LINEAR to dmabuf
    *    feedback tranches assuming LINEAR can be handled by GPU drivers.
    * 2. Venus + Virgl only sees the compositor injected LINEAR mod since
    *    Virgl doesn't support explicit modifiers on the driver side.
    * 3. Nvidia GPUs doesn't support LINEAR color attachment, and it's too
    *    late to reject LINEAR mod when the native image path has already
    *    been taken instead of the prime image path.
    *
    * Gamescope requires VK_EXT_physical_device_drm and its runtime doesn't
    * use standard WSI extensions, so venus can spoof without impacting it.
    */
   struct vk_properties *props = &physical_dev->base.vk.properties;
   const bool is_nvidia = props->vendorID == 0x10de;
   const char *app_name = physical_dev->instance->base.vk.app_info.app_name;
   const bool is_gamescope = app_name && strcmp(app_name, "gamescope") == 0;
   if (is_nvidia && !is_gamescope) {
      /* Fail same_gpu check on x11. See wsi_device_matches_drm_fd. */
      physical_dev->base.vk.supported_extensions.EXT_pci_bus_info = false;
      props->pciDomain = 0;
      props->pciBus = 0;
      props->pciDevice = 0;
      props->pciFunction = 0;

      /* Fail same_gpu check on wayland. See wsi_wl_display_init. */
      physical_dev->base.vk.supported_extensions.EXT_physical_device_drm =
         false;
      props->drmHasPrimary = false;
      props->drmHasRender = true;
      props->drmPrimaryMajor = 0;
      props->drmPrimaryMinor = 0;
      props->drmRenderMajor = 0;
      props->drmRenderMinor = 0;
   }

   const VkAllocationCallbacks *alloc =
      &physical_dev->instance->base.vk.alloc;
   VkResult result = wsi_device_init(
      &physical_dev->wsi_device, vn_physical_device_to_handle(physical_dev),
      vn_wsi_proc_addr, alloc, -1, &physical_dev->instance->drirc.options,
      &(struct wsi_device_options){
         .sw_device = use_sw_device,
         .extra_xwayland_image = true,
      });
   if (result != VK_SUCCESS)
      return result;

   physical_dev->wsi_device.supports_scanout = false;
   physical_dev->wsi_device.supports_modifiers =
      physical_dev->base.vk.supported_extensions.EXT_image_drm_format_modifier;
   physical_dev->base.vk.wsi_device = &physical_dev->wsi_device;

   return VK_SUCCESS;
}

void
vn_wsi_fini(struct vn_physical_device *physical_dev)
{
   const VkAllocationCallbacks *alloc =
      &physical_dev->instance->base.vk.alloc;
   physical_dev->base.vk.wsi_device = NULL;
   wsi_device_finish(&physical_dev->wsi_device, alloc);
}

VkResult
vn_wsi_create_image(struct vn_device *dev,
                    const VkImageCreateInfo *create_info,
                    const struct wsi_image_create_info *wsi_info,
                    const VkAllocationCallbacks *alloc,
                    struct vn_image **out_img)
{
   VkImageCreateInfo local_create_info;
   if (dev->physical_device->renderer_driver_id ==
          VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA &&
       (create_info->flags & VK_IMAGE_CREATE_ALIAS_BIT)) {
      /* See explanation in vn_GetPhysicalDeviceImageFormatProperties2() */
      local_create_info = *create_info;
      local_create_info.flags &= ~VK_IMAGE_CREATE_ALIAS_BIT;
      create_info = &local_create_info;
   }

   /* Gamescope relies on legacy scanout support when explicit modifier isn't
    * available and it chains the mesa wsi hint requesting such. Venus doesn't
    * support legacy scanout with optimal tiling on its own, so venus disables
    * legacy scanout in favor of prime buffer blit for optimal performance. As
    * a workaround here, venus can once again force linear tiling when legacy
    * scanout is requested outside of common wsi.
    */
   if (wsi_info->scanout) {
      if (create_info != &local_create_info) {
         local_create_info = *create_info;
         local_create_info.tiling = VK_IMAGE_TILING_LINEAR;
         create_info = &local_create_info;
      } else {
         local_create_info.tiling = VK_IMAGE_TILING_LINEAR;
      }
   }

   struct vn_image *img;
   VkResult result = vn_image_create(dev, create_info, alloc, &img);
   if (result != VK_SUCCESS)
      return result;

   img->wsi.is_prime_blit_src = wsi_info->blit_src;
   if (VN_DEBUG(WSI)) {
      vn_log(dev->instance, "%s: legacy_scanout=%d, prime_blit=%d", __func__,
             wsi_info->scanout, wsi_info->blit_src);
   }

   *out_img = img;
   return VK_SUCCESS;
}

void
vn_wsi_memory_info_init(struct vn_device_memory *mem,
                        const VkMemoryAllocateInfo *alloc_info)
{
   const VkMemoryDedicatedAllocateInfo *dedicated_info = NULL;
   const struct wsi_memory_allocate_info *wsi_info = NULL;

   vk_foreach_struct_const(pnext, alloc_info->pNext) {
      switch ((uint32_t)pnext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO:
         dedicated_info = (const void *)pnext;
         break;
      case VK_STRUCTURE_TYPE_WSI_MEMORY_ALLOCATE_INFO_MESA:
         wsi_info = (const void *)pnext;
         break;
      default:
         break;
      }
   }

   /* wsi always uses dedicated allocation */
   assert(dedicated_info || !wsi_info);

   if (wsi_info && dedicated_info->buffer != VK_NULL_HANDLE) {
      struct vn_buffer *buf = vn_buffer_from_handle(dedicated_info->buffer);
      buf->wsi.mem = mem;
   }

   /* wsi_memory_allocate_info is not chained for prime blit src */
   if (dedicated_info && dedicated_info->image != VK_NULL_HANDLE) {
      struct vn_image *img = vn_image_from_handle(dedicated_info->image);
      mem->dedicated_img = img;
   }
}

static uint32_t
vn_modifier_plane_count(struct vn_physical_device *physical_dev,
                        VkFormat format,
                        uint64_t modifier)
{
   VkPhysicalDevice physical_dev_handle =
      vn_physical_device_to_handle(physical_dev);

   VkDrmFormatModifierPropertiesListEXT modifier_list = {
      .sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT,
      .pDrmFormatModifierProperties = NULL,
   };
   VkFormatProperties2 format_props = {
      .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
      .pNext = &modifier_list,
   };
   vn_GetPhysicalDeviceFormatProperties2(physical_dev_handle, format,
                                         &format_props);

   STACK_ARRAY(VkDrmFormatModifierPropertiesEXT, modifier_props,
               modifier_list.drmFormatModifierCount);
   if (!modifier_props)
      return 0;
   modifier_list.pDrmFormatModifierProperties = modifier_props;

   vn_GetPhysicalDeviceFormatProperties2(physical_dev_handle, format,
                                         &format_props);

   uint32_t plane_count = 0;
   for (uint32_t i = 0; i < modifier_list.drmFormatModifierCount; i++) {
      const VkDrmFormatModifierPropertiesEXT *props =
         &modifier_list.pDrmFormatModifierProperties[i];
      if (modifier == props->drmFormatModifier) {
         plane_count = props->drmFormatModifierPlaneCount;
         break;
      }
   }

   STACK_ARRAY_FINISH(modifier_props);
   return plane_count;
}

bool
vn_wsi_validate_image_format_info(struct vn_physical_device *physical_dev,
                                  const VkPhysicalDeviceImageFormatInfo2 *info)
{
   const VkPhysicalDeviceImageDrmFormatModifierInfoEXT *modifier_info =
      vk_find_struct_const(
         info->pNext, PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT);

   /* force common wsi into choosing DRM_FORMAT_MOD_LINEAR or else fall back
    * to the legacy path, for which Venus also forces LINEAR for wsi images.
    */
   if (VN_PERF(NO_TILED_WSI_IMAGE)) {
      if (modifier_info &&
          modifier_info->drmFormatModifier != DRM_FORMAT_MOD_LINEAR) {
         if (VN_DEBUG(WSI)) {
            vn_log(physical_dev->instance,
                   "rejecting non-linear wsi image format modifier %" PRIu64,
                   modifier_info->drmFormatModifier);
         }
         return false;
      }
   }

   /* Integration with Xwayland (using virgl-backed gbm) may only use
    * modifiers for which `memory_plane_count == format_plane_count` with the
    * distinction defined in the spec for VkDrmFormatModifierPropertiesEXT.
    *
    * The spec also states that:
    *   If an image is non-linear, then the partition of the image’s memory
    *   into memory planes is implementation-specific and may be unrelated to
    *   the partition of the image’s content into format planes.
    *
    * A modifier like I915_FORMAT_MOD_Y_TILED_CCS with an extra CCS
    * metadata-only _memory_ plane is not supported by virgl. In general,
    * since the partition of format planes into memory planes (even when their
    * counts match) cannot be guarantably known, the safest option is to limit
    * both plane counts to 1 while virgl may be involved.
    */
   if (modifier_info &&
       !physical_dev->instance->drirc.performance.enable_wsi_multi_plane_modifiers &&
       modifier_info->drmFormatModifier != DRM_FORMAT_MOD_LINEAR) {
      const uint32_t plane_count = vn_modifier_plane_count(
         physical_dev, info->format, modifier_info->drmFormatModifier);
      if (plane_count != 1) {
         if (VN_DEBUG(WSI)) {
            vn_log(physical_dev->instance,
                   "rejecting multi-plane (%u) modifier %" PRIu64
                   " for wsi image with format %u",
                   plane_count, modifier_info->drmFormatModifier,
                   info->format);
         }
         return false;
      }
   }

   return true;
}

VkResult
vn_wsi_fence_wait(struct vn_device *dev, struct vn_queue *queue)
{
   /* External sync is supported by virtgpu backend but not vtest backend. For
    * vtest, common wsi will skip the implicit out fence installation due to
    * the lack of external SYNC_FD semaphore support. So we'll detect async
    * present thread and properly wait inside the wsi queue submit.
    */
   if (dev->renderer->info.has_external_sync ||
       dev->renderer->info.has_implicit_fencing)
      return VK_SUCCESS;

   if (!queue->async_present.initialized ||
       queue->async_present.tid != vn_gettid())
      return VK_SUCCESS;

   /* lazily create wsi wait fence for present fence waiting */
   VkDevice dev_handle = vn_device_to_handle(dev);
   VkResult result;
   if (queue->async_present.fence == VK_NULL_HANDLE) {
      const VkFenceCreateInfo create_info = {
         .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      };
      result = vn_CreateFence(dev_handle, &create_info, NULL,
                              &queue->async_present.fence);
      if (result != VK_SUCCESS)
         return result;
   }

   VkQueue queue_handle = vn_queue_to_handle(queue);
   result = vn_QueueSubmit(queue_handle, 0, NULL, queue->async_present.fence);
   if (result != VK_SUCCESS)
      return result;

   /* Common wsi does queue submit for each chain, so here we can only safely
    * unlock the queue mutex if presenting to a single chain.
    */
   const bool can_unlock_queue =
      queue->async_present.info->swapchainCount == 1;
   if (can_unlock_queue)
      simple_mtx_unlock(&queue->async_present.queue_mutex);
   vn_wsi_chains_unlock(dev, queue->async_present.info, /*all=*/false);

   result = vn_WaitForFences(dev_handle, 1, &queue->async_present.fence, true,
                             UINT64_MAX);

   vn_wsi_chains_lock(dev, queue->async_present.info, /*all=*/false);
   if (can_unlock_queue)
      simple_mtx_lock(&queue->async_present.queue_mutex);

   if (result != VK_SUCCESS)
      return result;

   return vn_ResetFences(dev_handle, 1, &queue->async_present.fence);
}

void
vn_wsi_sync_wait(struct vn_device *dev, int fd)
{
   if (dev->renderer->info.has_implicit_fencing)
      return;

   const pid_t tid = vn_gettid();
   struct vn_queue *queue = NULL;
   for (uint32_t i = 0; i < dev->queue_count; i++) {
      if (dev->queues[i].async_present.initialized &&
          dev->queues[i].async_present.tid == tid) {
         queue = &dev->queues[i];
         break;
      }
   }

   if (queue) {
      simple_mtx_unlock(&queue->async_present.queue_mutex);
      vn_wsi_chains_unlock(dev, queue->async_present.info, /*all=*/false);
   }

   sync_wait(fd, -1);

   if (queue) {
      vn_wsi_chains_lock(dev, queue->async_present.info, /*all=*/false);
      simple_mtx_lock(&queue->async_present.queue_mutex);
   }
}

void
vn_wsi_flush(struct vn_queue *queue)
{
   /* No need to flush if there's no present. */
   if (!queue->async_present.initialized)
      return;

   /* Should not flush on the async present thread. */
   if (queue->async_present.tid == vn_gettid())
      return;

   /* Being able to acquire the lock ensures async present queue access
    * has completed.
    */
   simple_mtx_lock(&queue->async_present.queue_mutex);
   simple_mtx_unlock(&queue->async_present.queue_mutex);
}

static VkPresentInfoKHR *
vn_wsi_clone_present_info(struct vn_device *dev, const VkPresentInfoKHR *pi)
{
   const VkAllocationCallbacks *alloc = &dev->base.vk.alloc;

   VkDeviceGroupPresentInfoKHR *dgpi = NULL;
   VkPresentRegionsKHR *pr = NULL;
   VkPresentIdKHR *id = NULL;
   VkPresentId2KHR *id2 = NULL;
   VkSwapchainPresentFenceInfoKHR *spfi = NULL;
   VkSwapchainPresentModeInfoKHR *spmi = NULL;
   VkPresentTimingsInfoEXT *pti = NULL;

   vk_foreach_struct_const(pnext, pi->pNext) {
      switch (pnext->sType) {
      case VK_STRUCTURE_TYPE_DEVICE_GROUP_PRESENT_INFO_KHR:
         dgpi = (void *)pnext;
         break;
      case VK_STRUCTURE_TYPE_PRESENT_REGIONS_KHR:
         pr = (void *)pnext;
         /* drop pr when pr->pRegions is NULL */
         if (!pr->pRegions)
            pr = NULL;
         break;
      case VK_STRUCTURE_TYPE_PRESENT_ID_KHR:
         id = (void *)pnext;
         break;
      case VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR:
         id2 = (void *)pnext;
         break;
      case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR:
         spfi = (void *)pnext;
         break;
      case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_KHR:
         spmi = (void *)pnext;
         break;
      case VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT:
         pti = (void *)pnext;
         break;
      default:
         break;
      }
   }

   /* VK_KHR_swapchain */
   VkPresentInfoKHR *_pi;
   VkSemaphore *_pi_sems;
   VkSwapchainKHR *_pi_chains;
   uint32_t *_pi_indices;
   VkResult *_pi_results;

   /* VK_KHR_device_group */
   VkDeviceGroupPresentInfoKHR *_dgpi;
   uint32_t *_dgpi_masks;

   /* VK_KHR_incremental_present */
   VkPresentRegionsKHR *_pr;
   VkPresentRegionKHR *_pr_regions;
   VkRectLayerKHR *_pr_rects;

   /* VK_KHR_present_id */
   VkPresentIdKHR *_id;
   uint64_t *_id_ids;

   /* VK_KHR_present_id2 */
   VkPresentId2KHR *_id2;
   uint64_t *_id2_ids;

   /* VK_KHR_swapchain_maintenance1 */
   VkSwapchainPresentFenceInfoKHR *_spfi;
   VkFence *_spfi_fences;
   VkSwapchainPresentModeInfoKHR *_spmi;
   VkPresentModeKHR *_spmi_modes;

   /* VK_EXT_present_timing */
   VkPresentTimingsInfoEXT *_pti;
   VkPresentTimingInfoEXT *_pti_timings;

   VK_MULTIALLOC(ma);
   vk_multialloc_add(&ma, &_pi, __typeof__(*_pi), 1);
   vk_multialloc_add(&ma, &_pi_sems, __typeof__(*_pi_sems),
                     pi->waitSemaphoreCount);
   vk_multialloc_add(&ma, &_pi_chains, __typeof__(*_pi_chains),
                     pi->swapchainCount);
   vk_multialloc_add(&ma, &_pi_indices, __typeof__(*_pi_indices),
                     pi->swapchainCount);
   if (pi->pResults) {
      vk_multialloc_add(&ma, &_pi_results, __typeof__(*_pi_results),
                        pi->swapchainCount);
   } else {
      _pi_results = NULL;
   }
   if (dgpi) {
      vk_multialloc_add(&ma, &_dgpi, __typeof__(*_dgpi), 1);
      vk_multialloc_add(&ma, &_dgpi_masks, __typeof__(*_dgpi_masks),
                        dgpi->swapchainCount);
   }
   if (pr) {
      vk_multialloc_add(&ma, &_pr, __typeof__(*_pr), 1);
      vk_multialloc_add(&ma, &_pr_regions, __typeof__(*_pr_regions),
                        pr->swapchainCount);

      uint32_t rect_count = 0;
      for (uint32_t i = 0; i < pr->swapchainCount; i++)
         rect_count += pr->pRegions[i].rectangleCount;

      vk_multialloc_add(&ma, &_pr_rects, __typeof__(*_pr_rects), rect_count);
   }
   if (id) {
      vk_multialloc_add(&ma, &_id, __typeof__(*_id), 1);
      vk_multialloc_add(&ma, &_id_ids, __typeof__(*_id_ids),
                        id->swapchainCount);
   }
   if (id2) {
      vk_multialloc_add(&ma, &_id2, __typeof__(*_id2), 1);
      vk_multialloc_add(&ma, &_id2_ids, __typeof__(*_id2_ids),
                        id2->swapchainCount);
   }
   if (spfi) {
      vk_multialloc_add(&ma, &_spfi, __typeof__(*_spfi), 1);
      vk_multialloc_add(&ma, &_spfi_fences, __typeof__(*_spfi_fences),
                        spfi->swapchainCount);
   }
   if (spmi) {
      vk_multialloc_add(&ma, &_spmi, __typeof__(*_spmi), 1);
      vk_multialloc_add(&ma, &_spmi_modes, __typeof__(*_spmi_modes),
                        spmi->swapchainCount);
   }
   if (pti) {
      vk_multialloc_add(&ma, &_pti, __typeof__(*_pti), 1);
      vk_multialloc_add(&ma, &_pti_timings, __typeof__(*_pti_timings),
                        pti->swapchainCount);
   }

   if (!vk_multialloc_alloc(&ma, alloc, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE))
      return NULL;

   typed_memcpy(_pi_sems, pi->pWaitSemaphores, pi->waitSemaphoreCount);
   typed_memcpy(_pi_chains, pi->pSwapchains, pi->swapchainCount);
   typed_memcpy(_pi_indices, pi->pImageIndices, pi->swapchainCount);

   *_pi = (VkPresentInfoKHR){
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = pi->waitSemaphoreCount,
      .pWaitSemaphores = _pi_sems,
      .swapchainCount = pi->swapchainCount,
      .pSwapchains = _pi_chains,
      .pImageIndices = _pi_indices,
      .pResults = _pi_results,
   };

   if (dgpi) {
      typed_memcpy(_dgpi_masks, dgpi->pDeviceMasks, dgpi->swapchainCount);

      *_dgpi = (VkDeviceGroupPresentInfoKHR){
         .sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_PRESENT_INFO_KHR,
         .swapchainCount = dgpi->swapchainCount,
         .pDeviceMasks = _dgpi_masks,
         .mode = dgpi->mode,
      };
      __vk_append_struct(_pi, _dgpi);
   }

   if (pr) {
      typed_memcpy(_pr_regions, pr->pRegions, pr->swapchainCount);

      for (uint32_t i = 0; i < pr->swapchainCount; i++) {
         VkPresentRegionKHR *_r = &_pr_regions[i];
         if (_r->rectangleCount > 0) {
            typed_memcpy(_pr_rects, _r->pRectangles, _r->rectangleCount);
            _r->pRectangles = _pr_rects;
            _pr_rects += _r->rectangleCount;
         }
      }

      *_pr = (VkPresentRegionsKHR){
         .sType = VK_STRUCTURE_TYPE_PRESENT_REGIONS_KHR,
         .swapchainCount = pr->swapchainCount,
         .pRegions = _pr_regions,
      };
      __vk_append_struct(_pi, _pr);
   }

   if (id) {
      typed_memcpy(_id_ids, id->pPresentIds, id->swapchainCount);

      *_id = (VkPresentIdKHR){
         .sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR,
         .swapchainCount = id->swapchainCount,
         .pPresentIds = _id_ids,
      };
      __vk_append_struct(_pi, _id);
   }

   if (id2) {
      typed_memcpy(_id2_ids, id2->pPresentIds, id2->swapchainCount);

      *_id2 = (VkPresentId2KHR){
         .sType = VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR,
         .swapchainCount = id2->swapchainCount,
         .pPresentIds = _id2_ids,
      };
      __vk_append_struct(_pi, _id2);
   }

   if (spfi) {
      typed_memcpy(_spfi_fences, spfi->pFences, spfi->swapchainCount);

      *_spfi = (VkSwapchainPresentFenceInfoKHR){
         .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR,
         .swapchainCount = spfi->swapchainCount,
         .pFences = _spfi_fences,
      };
      __vk_append_struct(_pi, _spfi);
   }

   if (spmi) {
      typed_memcpy(_spmi_modes, spmi->pPresentModes, spmi->swapchainCount);

      *_spmi = (VkSwapchainPresentModeInfoKHR){
         .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_KHR,
         .swapchainCount = spmi->swapchainCount,
         .pPresentModes = _spmi_modes,
      };
      __vk_append_struct(_pi, _spmi);
   }

   if (pti) {
      typed_memcpy(_pti_timings, pti->pTimingInfos, pti->swapchainCount);

      *_pti = (VkPresentTimingsInfoEXT){
         .sType = VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT,
         .swapchainCount = pti->swapchainCount,
         .pTimingInfos = _pti_timings,
      };
      __vk_append_struct(_pi, _pti);
   }

   return _pi;
}

static int
vn_wsi_present_thread(void *data)
{
   struct vn_queue *queue = data;
   struct vk_queue *queue_vk = &queue->base.vk;
   struct vn_device *dev = vn_device_from_vk(queue_vk->base.device);
   const VkAllocationCallbacks *alloc = &dev->base.vk.alloc;
   char thread_name[16];

   snprintf(thread_name, ARRAY_SIZE(thread_name), "vn_wsi[%u,%u]",
            queue_vk->queue_family_index, queue_vk->index_in_family);
   u_thread_setname(thread_name);

   queue->async_present.tid = vn_gettid();
   queue->async_present.initialized = true;

   mtx_lock(&queue->async_present.mutex);
   while (true) {
      while (!queue->async_present.info && !queue->async_present.join)
         cnd_wait(&queue->async_present.cond, &queue->async_present.mutex);

      if (queue->async_present.join)
         break;

      simple_mtx_lock(&queue->async_present.queue_mutex);
      vn_wsi_chains_lock(dev, queue->async_present.info, /*all=*/true);

      queue->async_present.pending = false;

      queue->async_present.result =
         wsi_common_queue_present(queue_vk->base.device->physical->wsi_device,
                                  queue_vk, queue->async_present.info);

      vn_wsi_chains_unlock(dev, queue->async_present.info, /*all=*/true);
      simple_mtx_unlock(&queue->async_present.queue_mutex);

      vk_free(alloc, queue->async_present.info);
      queue->async_present.info = NULL;
   }
   mtx_unlock(&queue->async_present.mutex);

   return 0;
}

static VkResult
vn_wsi_present_async(struct vn_device *dev,
                     struct vn_queue *queue,
                     const VkPresentInfoKHR *pi)
{
   VkResult result = VK_SUCCESS;

   if (unlikely(!queue->async_present.initialized)) {
      simple_mtx_init(&queue->async_present.queue_mutex, mtx_plain);
      mtx_init(&queue->async_present.mutex, mtx_plain);
      cnd_init(&queue->async_present.cond);

      if (u_thread_create(&queue->async_present.thread, vn_wsi_present_thread,
                          queue) != thrd_success)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   mtx_lock(&queue->async_present.mutex);
   assert(!queue->async_present.info);
   assert(!queue->async_present.pending);
   result = queue->async_present.result;
   if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
      queue->async_present.info = vn_wsi_clone_present_info(dev, pi);
      queue->async_present.pending = true;
      cnd_signal(&queue->async_present.cond);
   }
   queue->async_present.result = VK_SUCCESS;
   mtx_unlock(&queue->async_present.mutex);

   /* Ensure async present thread has acquired the queue and present locks. */
   while (queue->async_present.pending)
      thrd_yield();

   if (pi->pResults) {
      /* TODO: fill present result for the corresponding chain */
      for (uint32_t i = 0; i < pi->swapchainCount; i++)
         pi->pResults[i] = result;
   }

   return result;
}

/* swapchain commands */

VKAPI_ATTR VkResult VKAPI_CALL
vn_CreateSwapchainKHR(VkDevice device,
                      const VkSwapchainCreateInfoKHR *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkSwapchainKHR *pSwapchain)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);

   struct vn_swapchain *chain =
      vk_alloc2(&dev->base.vk.alloc, pAllocator, sizeof(*chain),
                VN_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!chain)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result =
      wsi_CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
   if (result != VK_SUCCESS) {
      vk_free2(&dev->base.vk.alloc, pAllocator, chain);
      return vn_error(dev->instance, result);
   }

   chain->handle = *pSwapchain;
   simple_mtx_init(&chain->mutex, mtx_plain);
   simple_mtx_init(&chain->acquire_mutex, mtx_plain);

   simple_mtx_lock(&dev->mutex);
   list_add(&chain->head, &dev->chains);
   simple_mtx_unlock(&dev->mutex);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vn_DestroySwapchainKHR(VkDevice device,
                       VkSwapchainKHR swapchain,
                       const VkAllocationCallbacks *pAllocator)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);

   if (swapchain == VK_NULL_HANDLE)
      return;

   struct vn_swapchain *chain = vn_wsi_chain_lookup(dev, swapchain);

   /* lock/unlock to wait for async present thread to release chain access */
   simple_mtx_lock(&chain->mutex);
   simple_mtx_unlock(&chain->mutex);

   simple_mtx_lock(&dev->mutex);
   list_del(&chain->head);
   simple_mtx_unlock(&dev->mutex);

   /* now safe to do swapchain tear down */
   simple_mtx_destroy(&chain->acquire_mutex);
   simple_mtx_destroy(&chain->mutex);
   vk_free2(&dev->base.vk.alloc, pAllocator, chain);

   return wsi_DestroySwapchainKHR(device, swapchain, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL
vn_AcquireNextImage2KHR(VkDevice device,
                        const VkAcquireNextImageInfoKHR *pAcquireInfo,
                        uint32_t *pImageIndex)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   VkResult result;

   struct vn_swapchain *chain =
      vn_wsi_chain_lookup(dev, pAcquireInfo->swapchain);

   simple_mtx_lock(&chain->acquire_mutex);
   result = wsi_common_acquire_next_image2(&dev->physical_device->wsi_device,
                                           device, pAcquireInfo, pImageIndex);
   simple_mtx_unlock(&chain->acquire_mutex);

   if (VN_DEBUG(WSI) && result != VK_SUCCESS) {
      const int idx = result >= VK_SUCCESS ? *pImageIndex : -1;
      vn_log(dev->instance, "swapchain %p: acquired image %d: %s",
             VN_WSI_PTR(pAcquireInfo->swapchain), idx,
             vk_Result_to_str(result));
   }

   if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
      return vn_error(dev->instance, result);

   int sync_fd = -1;
   if (!dev->renderer->info.has_implicit_fencing) {
      VkDeviceMemory mem_handle =
         wsi_common_get_memory(pAcquireInfo->swapchain, *pImageIndex);
      struct vn_device_memory *mem = vn_device_memory_from_handle(mem_handle);
      struct vn_image *img = mem->dedicated_img;

      if (img->wsi.is_prime_blit_src)
         mem = img->wsi.blit_mem;

      /* Only image buffer blit is tracked (both cpu and prime), so we use if
       * condition below for potential new blit path supported on non-Linux
       * platforms.
       */
      if (mem) {
         sync_fd =
            vn_renderer_bo_export_sync_file(dev->renderer, mem->base_bo);
      }
   }

   int sem_fd = -1, fence_fd = -1;
   if (sync_fd >= 0) {
      if (pAcquireInfo->semaphore != VK_NULL_HANDLE &&
          pAcquireInfo->fence != VK_NULL_HANDLE) {
         sem_fd = sync_fd;
         fence_fd = dup(sync_fd);
         if (fence_fd < 0) {
            result = errno == EMFILE ? VK_ERROR_TOO_MANY_OBJECTS
                                     : VK_ERROR_OUT_OF_HOST_MEMORY;
            close(sync_fd);
            return vn_error(dev->instance, result);
         }
      } else if (pAcquireInfo->semaphore != VK_NULL_HANDLE) {
         sem_fd = sync_fd;
      } else {
         assert(pAcquireInfo->fence != VK_NULL_HANDLE);
         fence_fd = sync_fd;
      }
   }

   if (pAcquireInfo->semaphore != VK_NULL_HANDLE) {
      const VkImportSemaphoreFdInfoKHR info = {
         .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
         .semaphore = pAcquireInfo->semaphore,
         .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
         .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
         .fd = sem_fd,
      };
      VkResult ret = vn_ImportSemaphoreFdKHR(device, &info);
      if (ret == VK_SUCCESS) {
         sem_fd = -1;
      } else {
         result = ret;
         goto out;
      }
   }

   if (pAcquireInfo->fence != VK_NULL_HANDLE) {
      const VkImportFenceFdInfoKHR info = {
         .sType = VK_STRUCTURE_TYPE_IMPORT_FENCE_FD_INFO_KHR,
         .fence = pAcquireInfo->fence,
         .flags = VK_FENCE_IMPORT_TEMPORARY_BIT,
         .handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT,
         .fd = fence_fd,
      };
      VkResult ret = vn_ImportFenceFdKHR(device, &info);
      if (ret == VK_SUCCESS) {
         fence_fd = -1;
      } else {
         result = ret;
         goto out;
      }
   }

out:
   if (sem_fd >= 0)
      close(sem_fd);
   if (fence_fd >= 0)
      close(fence_fd);

   return vn_result(dev->instance, result);
}

VKAPI_ATTR VkResult VKAPI_CALL
vn_QueuePresentKHR(VkQueue _queue, const VkPresentInfoKHR *pPresentInfo)
{
   VN_TRACE_FUNC();
   VK_FROM_HANDLE(vk_queue, queue_vk, _queue);
   struct vn_device *dev = vn_device_from_vk(queue_vk->base.device);

   if (!dev->renderer->info.has_implicit_fencing &&
       !VN_PERF(NO_ASYNC_PRESENT)) {
      struct vn_queue *queue = vn_queue_from_handle(_queue);
      return vn_wsi_present_async(dev, queue, pPresentInfo);
   }

   return wsi_common_queue_present(
      queue_vk->base.device->physical->wsi_device, queue_vk, pPresentInfo);
}
