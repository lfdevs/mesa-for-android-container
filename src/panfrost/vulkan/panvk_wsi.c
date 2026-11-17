/*
 * Copyright © 2021 Collabora Ltd.
 * Copyright © 2025 Arm Ltd.
 *
 * Derived from tu_wsi.c:
 * Copyright © 2016 Red Hat
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "panvk_wsi.h"
#include "panvk_instance.h"
#include "panvk_physical_device.h"

#include <stdlib.h>
#include <string.h>

#if defined(HAVE_PAN_KMOD_KBASE)
#include "lib/kmod/kbase_kmod.h"
#endif

#include "vk_util.h"
#include "wsi_common.h"

static VKAPI_PTR PFN_vkVoidFunction
panvk_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char *pName)
{
   VK_FROM_HANDLE(panvk_physical_device, pdevice, physicalDevice);
   struct panvk_instance *instance = to_panvk_instance(pdevice->vk.instance);

   return vk_instance_get_proc_addr_unchecked(&instance->vk, pName);
}

static bool
panvk_can_present_on_device(VkPhysicalDevice pdevice, int fd)
{
   drmDevicePtr device;
   if (drmGetDevice2(fd, 0, &device) != 0)
      return false;
   /* Allow on-device presentation for all devices with bus type PLATFORM.
    * Other device types such as PCI or USB should use the PRIME blit path. */
   bool match = device->bustype == DRM_BUS_PLATFORM;

   drmFreeDevice(&device);

   return match;
}

#ifdef VK_USE_PLATFORM_ANDROID_KHR
#include <android/native_window.h>
#include <android/hardware_buffer.h>
#include "wsi_common_private.h"
#include "panvk_android.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_image.h"
#include "vk_object.h"

struct panvk_android_swapchain_image {
   struct wsi_image base;
   AHardwareBuffer *ahb;
};

struct panvk_android_swapchain {
   struct wsi_swapchain base;
   ANativeWindow *window;
   uint32_t next_image;
   uint64_t completed_present_id;
   struct panvk_android_swapchain_image images[4];
};

static VkResult
panvk_wsi_android_get_support(VkIcdSurfaceBase *surface,
                              struct wsi_device *wsi_device,
                              uint32_t queueFamilyIndex,
                              VkBool32 *pSupported)
{
   *pSupported = VK_TRUE;
   return VK_SUCCESS;
}

static VkResult
panvk_wsi_android_get_capabilities2(VkIcdSurfaceBase *surface,
                                    struct wsi_device *wsi_device,
                                    const void *info_next,
                                    VkSurfaceCapabilities2KHR *pSurfaceCapabilities)
{
   VkIcdSurfaceAndroid *s = (VkIcdSurfaceAndroid *)surface;
   int w = s->window ? ANativeWindow_getWidth(s->window) : 64;
   int h = s->window ? ANativeWindow_getHeight(s->window) : 64;
   if (w <= 0) w = 64;
   if (h <= 0) h = 64;

   pSurfaceCapabilities->surfaceCapabilities = (VkSurfaceCapabilitiesKHR){
      .minImageCount = 2,
      .maxImageCount = 4,
      .currentExtent = {(uint32_t)w, (uint32_t)h},
      .minImageExtent = {1, 1},
      .maxImageExtent = {4096, 4096},
      .maxImageArrayLayers = 1,
      .supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
      .currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
      .supportedCompositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR |
                                 VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                             VK_IMAGE_USAGE_SAMPLED_BIT,
   };

   const VkSurfacePresentModeKHR *mode =
      vk_find_struct_const(info_next, SURFACE_PRESENT_MODE_KHR);
   vk_foreach_struct(sType, ext, pSurfaceCapabilities->pNext) {
      if (sType == VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_COMPATIBILITY_KHR) {
         VkSurfacePresentModeCompatibilityKHR *compat = (void *)ext;
         const VkPresentModeKHR present_mode =
            mode ? mode->presentMode : VK_PRESENT_MODE_FIFO_KHR;
         VK_OUTARRAY_MAKE_TYPED(VkPresentModeKHR, out,
                                compat->pPresentModes,
                                &compat->presentModeCount);
         vk_outarray_append_typed(VkPresentModeKHR, &out, compatible_mode) {
            *compatible_mode = present_mode;
         }
      }
   }
   return VK_SUCCESS;
}

static VkResult
panvk_wsi_android_get_formats(VkIcdSurfaceBase *surface,
                              struct wsi_device *wsi_device,
                              uint32_t *pSurfaceFormatCount,
                              VkSurfaceFormatKHR *pSurfaceFormats)
{
   static const VkSurfaceFormatKHR formats[] = {
      { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
      { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
      { VK_FORMAT_R8G8B8A8_SRGB,  VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
   };
   VK_OUTARRAY_MAKE_TYPED(VkSurfaceFormatKHR, out, pSurfaceFormats, pSurfaceFormatCount);
   for (unsigned i = 0; i < ARRAY_SIZE(formats); i++) {
      vk_outarray_append_typed(VkSurfaceFormatKHR, &out, f) {
         *f = formats[i];
      }
   }
   return vk_outarray_status(&out);
}

static VkResult
panvk_wsi_android_get_formats2(VkIcdSurfaceBase *surface,
                               struct wsi_device *wsi_device,
                               const void *info_next,
                               uint32_t *pSurfaceFormatCount,
                               VkSurfaceFormat2KHR *pSurfaceFormats)
{
   static const VkSurfaceFormatKHR formats[] = {
      { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
      { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
      { VK_FORMAT_R8G8B8A8_SRGB,  VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
   };
   VK_OUTARRAY_MAKE_TYPED(VkSurfaceFormat2KHR, out, pSurfaceFormats, pSurfaceFormatCount);
   for (unsigned i = 0; i < ARRAY_SIZE(formats); i++) {
      vk_outarray_append_typed(VkSurfaceFormat2KHR, &out, f) {
         f->surfaceFormat = formats[i];
      }
   }
   return vk_outarray_status(&out);
}

static VkResult
panvk_wsi_android_get_present_modes(VkIcdSurfaceBase *surface,
                                    struct wsi_device *wsi_device,
                                    uint32_t *pPresentModeCount,
                                    VkPresentModeKHR *pPresentModes)
{
   static const VkPresentModeKHR modes[] = {
      VK_PRESENT_MODE_FIFO_KHR,
      VK_PRESENT_MODE_MAILBOX_KHR,
      VK_PRESENT_MODE_IMMEDIATE_KHR,
   };
   VK_OUTARRAY_MAKE_TYPED(VkPresentModeKHR, out, pPresentModes, pPresentModeCount);
   for (unsigned i = 0; i < ARRAY_SIZE(modes); i++) {
      vk_outarray_append_typed(VkPresentModeKHR, &out, m) {
         *m = modes[i];
      }
   }
   return vk_outarray_status(&out);
}

static VkResult
panvk_wsi_android_get_present_rectangles(VkIcdSurfaceBase *surface,
                                         struct wsi_device *wsi_device,
                                         uint32_t *pRectCount,
                                         VkRect2D *pRects)
{
   VkIcdSurfaceAndroid *s = (VkIcdSurfaceAndroid *)surface;
   int w = s->window ? ANativeWindow_getWidth(s->window) : 64;
   int h = s->window ? ANativeWindow_getHeight(s->window) : 64;
   if (w <= 0) w = 64;
   if (h <= 0) h = 64;

   VK_OUTARRAY_MAKE_TYPED(VkRect2D, out, pRects, pRectCount);
   vk_outarray_append_typed(VkRect2D, &out, r) {
      r->offset.x = 0;
      r->offset.y = 0;
      r->extent.width = (uint32_t)w;
      r->extent.height = (uint32_t)h;
   }
   return vk_outarray_status(&out);
}

static struct wsi_image *
panvk_android_swapchain_get_wsi_image(struct wsi_swapchain *swapchain,
                                     uint32_t image_index)
{
   struct panvk_android_swapchain *chain = (struct panvk_android_swapchain *)swapchain;
   return &chain->images[image_index].base;
}

static VkResult
panvk_android_swapchain_acquire_next_image(
   struct wsi_swapchain *swapchain,
   const VkAcquireNextImageInfoKHR *info,
   uint32_t *image_index)
{
   struct panvk_android_swapchain *chain = (struct panvk_android_swapchain *)swapchain;
    uint32_t idx = chain->next_image;
    chain->next_image = (chain->next_image + 1) % chain->base.image_count;
    *image_index = idx;
    return VK_SUCCESS;
}

static VkResult
panvk_android_swapchain_release_images(struct wsi_swapchain *swapchain,
                                       uint32_t count,
                                       const uint32_t *indices)
{
   for (uint32_t i = 0; i < count; i++) {
      if (indices[i] >= swapchain->image_count)
         return VK_ERROR_OUT_OF_DATE_KHR;

      swapchain->get_wsi_image(swapchain, indices[i])->acquired = false;
   }

   return VK_SUCCESS;
}

static VkResult
panvk_android_swapchain_wait_for_present2(struct wsi_swapchain *swapchain,
                                          uint64_t present_id,
                                          uint64_t timeout)
{
   struct panvk_android_swapchain *chain =
      (struct panvk_android_swapchain *)swapchain;

   return present_id <= chain->completed_present_id ? VK_SUCCESS : VK_TIMEOUT;
}

static VkResult
panvk_android_swapchain_queue_present(
   struct wsi_swapchain *swapchain,
   uint32_t image_index,
   uint64_t present_id,
   const VkPresentRegionKHR *damage)
{
   struct panvk_android_swapchain *chain = (struct panvk_android_swapchain *)swapchain;
   if (image_index >= chain->base.image_count)
      return VK_ERROR_OUT_OF_DATE_KHR;

   VK_FROM_HANDLE(vk_device, dev, swapchain->device);
   dev->dispatch_table.DeviceWaitIdle(swapchain->device);

   if (chain->window && chain->images[image_index].ahb) {
      ANativeWindow_Buffer win_buf;
      if (ANativeWindow_lock(chain->window, &win_buf, NULL) == 0) {
         void *ahb_data = NULL;
         AHardwareBuffer_Desc desc;
         AHardwareBuffer_describe(chain->images[image_index].ahb, &desc);
         if (AHardwareBuffer_lock(chain->images[image_index].ahb,
                                  AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, NULL, &ahb_data) == 0) {
            uint32_t copy_h = win_buf.height < (int)desc.height ? (uint32_t)win_buf.height : desc.height;
            uint32_t copy_w = win_buf.width < (int)desc.width ? (uint32_t)win_buf.width : desc.width;
            for (uint32_t y = 0; y < copy_h; y++) {
               memcpy((char *)win_buf.bits + y * win_buf.stride * 4,
                      (char *)ahb_data + y * desc.stride * 4,
                      copy_w * 4);
            }
            AHardwareBuffer_unlock(chain->images[image_index].ahb, NULL);
         }
         ANativeWindow_unlockAndPost(chain->window);
      }
   }

   chain->completed_present_id = MAX2(chain->completed_present_id, present_id);

   return VK_SUCCESS;
}

static VkResult
panvk_android_swapchain_destroy(
   struct wsi_swapchain *swapchain,
   const VkAllocationCallbacks *pAllocator)
{
   struct panvk_android_swapchain *chain = (struct panvk_android_swapchain *)swapchain;
   VK_FROM_HANDLE(vk_device, dev, swapchain->device);

   for (uint32_t i = 0; i < chain->base.image_count; i++) {
      if (chain->images[i].base.image != VK_NULL_HANDLE)
         dev->dispatch_table.DestroyImage(swapchain->device, chain->images[i].base.image, pAllocator);
      if (chain->images[i].base.memory != VK_NULL_HANDLE)
         dev->dispatch_table.FreeMemory(swapchain->device, chain->images[i].base.memory, pAllocator);
      if (chain->images[i].ahb)
         AHardwareBuffer_release(chain->images[i].ahb);
   }

   wsi_swapchain_finish(&chain->base);
   vk_free2(&dev->alloc, pAllocator, chain);
   return VK_SUCCESS;
}

static VkResult
panvk_wsi_android_create_swapchain(
   VkIcdSurfaceBase *surface,
   VkDevice device,
   struct wsi_device *wsi_device,
   const VkSwapchainCreateInfoKHR *pCreateInfo,
   const VkAllocationCallbacks *pAllocator,
   struct wsi_swapchain **swapchain_out)
{
    VK_FROM_HANDLE(vk_device, dev, device);
    VkResult result;
    uint32_t num_images = pCreateInfo->minImageCount < 2 ? 2 : pCreateInfo->minImageCount;
    if (num_images > 4) num_images = 4;

    struct panvk_android_swapchain *chain = vk_zalloc2(
       &dev->alloc, pAllocator, sizeof(*chain), 8,
       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
    if (!chain)
       return VK_ERROR_OUT_OF_HOST_MEMORY;

    vk_object_base_init(dev, &chain->base.base, VK_OBJECT_TYPE_SWAPCHAIN_KHR);
    chain->base.wsi = wsi_device;
    chain->base.device = device;
    chain->base.alloc = *pAllocator;
    chain->base.blit.type = WSI_SWAPCHAIN_NO_BLIT;
    chain->base.destroy = panvk_android_swapchain_destroy;
    chain->base.get_wsi_image = panvk_android_swapchain_get_wsi_image;
   chain->base.acquire_next_image = panvk_android_swapchain_acquire_next_image;
   chain->base.queue_present = panvk_android_swapchain_queue_present;
   chain->base.wait_for_present2 = panvk_android_swapchain_wait_for_present2;
   chain->base.release_images = panvk_android_swapchain_release_images;
   chain->base.image_count = num_images;
   chain->next_image = 0;

   VkIcdSurfaceAndroid *android_surface = (VkIcdSurfaceAndroid *)surface;
   chain->window = android_surface->window;
   if (chain->window) {
      ANativeWindow_setBuffersGeometry(chain->window,
                                       pCreateInfo->imageExtent.width,
                                       pCreateInfo->imageExtent.height,
                                       WINDOW_FORMAT_RGBA_8888);
   }

   for (uint32_t i = 0; i < num_images; i++) {
      AHardwareBuffer_Desc desc = {
         .width = pCreateInfo->imageExtent.width,
         .height = pCreateInfo->imageExtent.height,
         .layers = 1,
         .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
         .usage = AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
                  AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                  AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
      };
      if (AHardwareBuffer_allocate(&desc, &chain->images[i].ahb) != 0 || !chain->images[i].ahb) {
         panvk_android_swapchain_destroy(&chain->base, pAllocator);
         return VK_ERROR_INITIALIZATION_FAILED;
      }

      VkExternalMemoryImageCreateInfo emici = {
         .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
         .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
      };
      VkImageCreateInfo ici = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
         .pNext = &emici,
         .imageType = VK_IMAGE_TYPE_2D,
         .format = pCreateInfo->imageFormat,
         .extent = {pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height, 1},
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .tiling = VK_IMAGE_TILING_OPTIMAL,
         .usage = pCreateInfo->imageUsage,
         .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      };
      result = dev->dispatch_table.CreateImage(device, &ici, pAllocator, &chain->images[i].base.image);
      if (result != VK_SUCCESS) {
         panvk_android_swapchain_destroy(&chain->base, pAllocator);
         return result;
      }

      VkAndroidHardwareBufferPropertiesANDROID ahb_props = {
         .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,
      };
      result = panvk_GetAndroidHardwareBufferPropertiesANDROID(device, chain->images[i].ahb, &ahb_props);
      if (result != VK_SUCCESS) {
         panvk_android_swapchain_destroy(&chain->base, pAllocator);
         return result;
      }

      uint32_t chosen_mi = 0;
      for (uint32_t m = 0; m < 32; m++) {
         if (ahb_props.memoryTypeBits & (1u << m)) {
            chosen_mi = m;
            break;
         }
      }

      VkImportAndroidHardwareBufferInfoANDROID imp = {
         .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
         .buffer = chain->images[i].ahb,
      };
      VkMemoryDedicatedAllocateInfo dai = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
         .pNext = &imp,
         .image = chain->images[i].base.image,
      };
      VkMemoryAllocateInfo mai = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
         .pNext = &dai,
         .allocationSize = ahb_props.allocationSize,
         .memoryTypeIndex = chosen_mi,
      };
      result = dev->dispatch_table.AllocateMemory(device, &mai, pAllocator, &chain->images[i].base.memory);
      if (result != VK_SUCCESS) {
         panvk_android_swapchain_destroy(&chain->base, pAllocator);
         return result;
      }

      VkBindImageMemoryInfo bmi = {
         .sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
         .image = chain->images[i].base.image,
         .memory = chain->images[i].base.memory,
         .memoryOffset = 0,
      };
      result = dev->dispatch_table.BindImageMemory2(device, 1, &bmi);
      if (result != VK_SUCCESS) {
         panvk_android_swapchain_destroy(&chain->base, pAllocator);
         return result;
      }
   }

   *swapchain_out = &chain->base;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_CreateAndroidSurfaceKHR(
   VkInstance _instance,
   const VkAndroidSurfaceCreateInfoKHR *pCreateInfo,
   const VkAllocationCallbacks *pAllocator,
   VkSurfaceKHR *pSurface)
{
   VK_FROM_HANDLE(vk_instance, instance, _instance);
   if (!pCreateInfo || !pCreateInfo->window)
      return VK_ERROR_INITIALIZATION_FAILED;

   VkIcdSurfaceAndroid *surface = vk_alloc2(
      &instance->alloc, pAllocator, sizeof(*surface), 8,
      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!surface)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   surface->base.platform = VK_ICD_WSI_PLATFORM_ANDROID;
   surface->window = pCreateInfo->window;
   ANativeWindow_acquire(surface->window);

   *pSurface = VkIcdSurfaceBase_to_handle(&surface->base);
   return VK_SUCCESS;
}

static struct wsi_interface panvk_android_wsi_interface = {
   .get_support = panvk_wsi_android_get_support,
   .get_capabilities2 = panvk_wsi_android_get_capabilities2,
   .get_formats = panvk_wsi_android_get_formats,
   .get_formats2 = panvk_wsi_android_get_formats2,
   .get_present_modes = panvk_wsi_android_get_present_modes,
   .get_present_rectangles = panvk_wsi_android_get_present_rectangles,
   .create_swapchain = panvk_wsi_android_create_swapchain,
};
#endif /* VK_USE_PLATFORM_ANDROID_KHR */

VkResult
panvk_wsi_init(struct panvk_physical_device *physical_device)
{
   struct panvk_instance *instance =
      to_panvk_instance(physical_device->vk.instance);
   const bool uses_kbase = physical_device->kbase_node_path[0] != '\0';
   const char *dri3_option = getenv("PANVK_KBASE_DRI3");
   /* The proprietary Termux:X11 layer uses WSI_X11_TERMUX=1 as its
    * process-wide switch.  Accept the same switch here when no explicit
    * PanVK override is present, so both ICDs exercise the same raw-FD DRI3
    * transport instead of silently falling back to the slower SHM path. */
   const char *termux_wsi = getenv("WSI_X11_TERMUX");
   const bool termux_raw_dri3 =
      uses_kbase && !dri3_option && termux_wsi && strcmp(termux_wsi, "0") != 0;
   const bool kbase_raw_dri3 =
      termux_raw_dri3 ||
      (uses_kbase && dri3_option &&
       (!strcmp(dri3_option, "raw") || !strcmp(dri3_option, "termux")));
   const bool kbase_dmabuf =
#if defined(HAVE_PAN_KMOD_KBASE)
      uses_kbase &&
      ((!dri3_option && termux_raw_dri3) ||
       (dri3_option && strcmp(dri3_option, "0") != 0)) &&
      kbase_kmod_supports_dmabuf(physical_device->kmod.dev);
#else
      false;
#endif
   VkResult result;

   result = wsi_device_init(&physical_device->wsi_device,
                            panvk_physical_device_to_handle(physical_device),
                            panvk_wsi_proc_addr, &instance->vk.alloc, -1,
                            &instance->drirc.options,
                            &(struct wsi_device_options){
                               .sw_device = uses_kbase && !kbase_dmabuf,
                                  kbase_dmabuf && kbase_raw_dri3,
                            });
   if (result != VK_SUCCESS)
      return result;

   /* kbase syncs carry GPU seqno state in userspace and cannot be copied via
    * DRM syncobj fd payloads.  Keep even empty WSI submits on the real queue
    * so present fences are backed by the kbase submission timeline.
    */
   physical_device->wsi_device.disable_unordered_submits = uses_kbase;

   /* kbase is not a DRM fd.  The default CPU WSI path presents through
    * MIT-SHM.  Android dma-heaps can also provide shareable
    * allocations, but some Android X servers advertise DRI3 while rejecting
    * standard PixmapFromBuffer.  PANVK_KBASE_DRI3=raw (or termux) uses the
    * Termux:X11/Winlator private raw-FD modifier instead of DRM modifiers.
    */
   physical_device->wsi_device.supports_modifiers =
      !uses_kbase || (kbase_dmabuf && !kbase_raw_dri3);
   physical_device->wsi_device.can_present_on_device =
      panvk_can_present_on_device;

   physical_device->vk.wsi_device = &physical_device->wsi_device;

#ifdef VK_USE_PLATFORM_ANDROID_KHR
   physical_device->wsi_device.wsi[VK_ICD_WSI_PLATFORM_ANDROID] = &panvk_android_wsi_interface;
#endif

   return VK_SUCCESS;
}

void
panvk_wsi_finish(struct panvk_physical_device *physical_device)
{
   struct panvk_instance *instance =
      to_panvk_instance(physical_device->vk.instance);

   physical_device->vk.wsi_device = NULL;
   wsi_device_finish(&physical_device->wsi_device, &instance->vk.alloc);
}
