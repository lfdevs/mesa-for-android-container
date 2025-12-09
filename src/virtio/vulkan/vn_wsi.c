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

static PFN_vkVoidFunction
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
   /* TODO Drop the workaround for NVIDIA_PROPRIETARY once hw prime buffer
    * blit path works there.
    */
   const bool use_sw_device =
      !physical_dev->base.vk.supported_extensions
          .EXT_external_memory_dma_buf ||
      physical_dev->renderer_driver_id == VK_DRIVER_ID_NVIDIA_PROPRIETARY;

   const VkAllocationCallbacks *alloc =
      &physical_dev->instance->base.vk.alloc;
   VkResult result = wsi_device_init(
      &physical_dev->wsi_device, vn_physical_device_to_handle(physical_dev),
      vn_wsi_proc_addr, alloc, -1, &physical_dev->instance->dri_options,
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
       !physical_dev->instance->enable_wsi_multi_plane_modifiers &&
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

/* swapchain commands */

VkResult
vn_AcquireNextImage2KHR(VkDevice device,
                        const VkAcquireNextImageInfoKHR *pAcquireInfo,
                        uint32_t *pImageIndex)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);

   VkResult result = wsi_common_acquire_next_image2(
      &dev->physical_device->wsi_device, device, pAcquireInfo, pImageIndex);
   if (VN_DEBUG(WSI) && result != VK_SUCCESS) {
      const int idx = result >= VK_SUCCESS ? *pImageIndex : -1;
      vn_log(dev->instance, "swapchain %p: acquired image %d: %s",
             VN_WSI_PTR(pAcquireInfo->swapchain), idx,
             vk_Result_to_str(result));
   }

   if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
      return vn_error(dev->instance, result);

   /* XXX this relies on renderer side doing implicit fencing */
   if (pAcquireInfo->semaphore != VK_NULL_HANDLE) {
      const VkImportSemaphoreFdInfoKHR info = {
         .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
         .semaphore = pAcquireInfo->semaphore,
         .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
         .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
         .fd = -1,
      };
      result = vn_ImportSemaphoreFdKHR(device, &info);
   }

   if (result == VK_SUCCESS && pAcquireInfo->fence != VK_NULL_HANDLE) {
      const VkImportFenceFdInfoKHR info = {
         .sType = VK_STRUCTURE_TYPE_IMPORT_FENCE_FD_INFO_KHR,
         .fence = pAcquireInfo->fence,
         .flags = VK_FENCE_IMPORT_TEMPORARY_BIT,
         .handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT,
         .fd = -1,
      };
      result = vn_ImportFenceFdKHR(device, &info);
   }

   return vn_result(dev->instance, result);
}
