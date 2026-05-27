/*
 * Copyright © 2015 Intel Corporation
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
#ifndef WSI_COMMON_H
#define WSI_COMMON_H

#include <stdint.h>
#include <stdbool.h>

#include "util/log.h"
#include "vk_alloc.h"
#include "vk_dispatch_table.h"
#include "vk_internal_exts.h"
#include "vk_util.h"
#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WSI_ENTRYPOINTS_H
extern const struct vk_instance_entrypoint_table wsi_instance_entrypoints;
extern const struct vk_physical_device_entrypoint_table wsi_physical_device_entrypoints;
extern const struct vk_device_entrypoint_table wsi_device_entrypoints;
#endif

#include <util/list.h>

struct wsi_interface;
struct vk_instance;
struct vk_queue;

struct driOptionCache;

#define VK_ICD_WSI_PLATFORM_MAX (VK_ICD_WSI_PLATFORM_METAL + 1)

struct wsi_device {
   /* Allocator for the instance */
   VkAllocationCallbacks instance_alloc;

   VkPhysicalDevice pdevice;
   VkPhysicalDeviceMemoryProperties memory_props;
   uint32_t queue_family_count;
   uint64_t queue_supports_blit;
   uint64_t queue_supports_timestamps;
   float timestamp_period;
   uint32_t timestamp_bits;

   VkPhysicalDeviceDrmPropertiesEXT drm_info;
   VkPhysicalDevicePCIBusInfoPropertiesEXT pci_bus_info;

   VkExternalSemaphoreHandleTypeFlags semaphore_export_handle_types;
   VkExternalSemaphoreHandleTypeFlags timeline_semaphore_export_handle_types;

   bool has_import_memory_host;
   bool has_timeline_semaphore;

   /** Whether the device uses 32bpp formats for 24bpp
    *
    * If true, VkImages created with R8G8B8/B8G8R8 formats will be
    * exported as 32bpp to the window system, as if they were B8G8R8A8
    * or R8G8B8A8
    */
   bool emulate_24as32;

   /** Indicates if wsi_image_create_info::scanout is supported
    *
    * If false, WSI will always use either modifiers or the prime blit path.
    */
   bool supports_scanout;

   bool supports_protected[VK_ICD_WSI_PLATFORM_MAX];

   bool supports_modifiers;
   uint32_t maxImageDimension2D;
   uint32_t optimalBufferCopyRowPitchAlignment;
   VkPresentModeKHR override_present_mode;
   bool force_bgra8_unorm_first;

   /* Cached result for wsi_drm_check_dma_buf_sync_file_import_export(). */
   uint32_t cached_sync_file_import_export_result;

   /* Whether to enable adaptive sync for a swapchain if implemented and
    * available. Not all window systems might support this. */
   bool enable_adaptive_sync;

   /* List of fences to signal when hotplug event happens. */
   struct list_head hotplug_fences;

   /* Create headless swapchains. */
   bool force_headless_swapchain;

   bool force_swapchain_to_currentExtent;

   bool disable_unordered_submits;

   struct {
      /* Override the minimum number of images on the swapchain.
       * 0 = no override */
      uint32_t override_minImageCount;

      /* Forces strict number of image on the swapchain using application
       * provided VkSwapchainCreateInfoKH::RminImageCount.
       */
      bool strict_imageCount;

      /* Ensures to create at least the number of image specified by the
       * driver in VkSurfaceCapabilitiesKHR::minImageCount.
       */
      bool ensure_minImageCount;

      /* Wait for fences before submitting buffers to Xwayland. Defaults to
       * true.
       */
      bool xwaylandWaitReady;

      /* adds an extra minImageCount when running under xwayland */
      bool extra_xwayland_image;

      /* Never report VK_SUBOPTIMAL_KHR. Used to workaround
       * games that cannot handle SUBOPTIMAL correctly. */
      bool ignore_suboptimal;
   } x11;

   struct {
      void *(*get_d3d12_command_queue)(VkDevice device);
      /* Needs to be per VkDevice, not VkPhysicalDevice, depends on queue config */
      bool (*requires_blits)(VkDevice device);
      VkResult (*create_image_memory)(VkDevice device, void *resource,
                                      const VkAllocationCallbacks *alloc,
                                      VkDeviceMemory *out);
   } win32;

   struct {
      VkResult (*bind_drawable_to_vkimage)(VkImage image, void *drawable);
      /* Does not transfer the ownership to the command buffer. The command
       * buffer must retain the drawable to ensure lifespan. */
      void (*encode_drawable_present)(VkCommandBuffer cmd, void *drawable);
   } metal;


   bool sw;

   /* Set to true if the implementation is ok with linear WSI images. */
   bool wants_linear;

   /* Whether present_wait functionality is enabled on the device.
    * In this case, we have to create an extra timeline semaphore
    * to be able to synchronize with the WSI present semaphore being unsignalled.
    * This requires VK_KHR_timeline_semaphore. */
   bool has_present_wait;

   struct {
      /* Don't use the commit-timing protocol for pacing */
      bool disable_timestamps;
   } wayland;

   /*
    * This sets the ownership for a WSI memory object:
    *
    * The ownership is true if and only if the application is allowed to submit
    * command buffers that reference the buffer.
    *
    * This can be used to prune BO lists without too many adverse affects on
    * implicit sync.
    *
    * Side note: care needs to be taken for internally delayed submissions wrt
    * timeline semaphores.
    */
   void (*set_memory_ownership)(VkDevice device,
                                VkDeviceMemory memory,
                                VkBool32 ownership);

   /*
    * If this is set, the WSI device will call it to let the driver backend
    * decide if it can present images directly on the given device fd.
    */
   bool (*can_present_on_device)(VkPhysicalDevice pdevice, int fd);

   /*
    * A driver can implement this callback to return a special queue to execute
    * buffer blits.
    */
   struct vk_queue *(*get_blit_queue)(VkDevice device);

#define WSI_CB(cb) PFN_vk##cb cb
   WSI_CB(AllocateMemory);
   WSI_CB(AllocateCommandBuffers);
   WSI_CB(BindBufferMemory);
   WSI_CB(BindImageMemory);
   WSI_CB(BeginCommandBuffer);
   WSI_CB(CmdPipelineBarrier);
   WSI_CB(CmdCopyImage);
   WSI_CB(CmdCopyImageToBuffer);
   WSI_CB(CmdResetQueryPool);
   WSI_CB(CmdWriteTimestamp);
   WSI_CB(CreateBuffer);
   WSI_CB(CreateCommandPool);
   WSI_CB(CreateFence);
   WSI_CB(CreateImage);
   WSI_CB(CreateQueryPool);
   WSI_CB(CreateSemaphore);
   WSI_CB(DestroyBuffer);
   WSI_CB(DestroyCommandPool);
   WSI_CB(DestroyFence);
   WSI_CB(DestroyImage);
   WSI_CB(DestroyQueryPool);
   WSI_CB(DestroySemaphore);
   WSI_CB(EndCommandBuffer);
   WSI_CB(FreeMemory);
   WSI_CB(FreeCommandBuffers);
   WSI_CB(GetBufferMemoryRequirements);
   WSI_CB(GetCalibratedTimestampsKHR);
   WSI_CB(GetFenceStatus);
   WSI_CB(GetImageDrmFormatModifierPropertiesEXT);
   WSI_CB(GetImageMemoryRequirements);
   WSI_CB(GetImageSubresourceLayout);
   WSI_CB(GetMemoryFdKHR);
   WSI_CB(GetPhysicalDeviceCalibrateableTimeDomainsKHR);
   WSI_CB(GetPhysicalDeviceProperties);
   WSI_CB(GetPhysicalDeviceFormatProperties);
   WSI_CB(GetPhysicalDeviceFormatProperties2);
   WSI_CB(GetPhysicalDeviceImageFormatProperties2);
   WSI_CB(GetPhysicalDeviceQueueFamilyProperties);
   WSI_CB(GetQueryPoolResults);
   WSI_CB(GetSemaphoreFdKHR);
   WSI_CB(ResetFences);
   WSI_CB(QueueSubmit2);
   WSI_CB(SetDebugUtilsObjectNameEXT);
   WSI_CB(WaitForFences);
   WSI_CB(MapMemory);
   WSI_CB(UnmapMemory);
   WSI_CB(WaitSemaphores);
#undef WSI_CB

    struct wsi_interface *                  wsi[VK_ICD_WSI_PLATFORM_MAX];
};

typedef PFN_vkVoidFunction (VKAPI_PTR *WSI_FN_GetPhysicalDeviceProcAddr)(VkPhysicalDevice physicalDevice, const char* pName);

struct wsi_device_options {
   bool sw_device;
   bool extra_xwayland_image;
   bool emulate_24as32;
};

VkResult
wsi_device_init(struct wsi_device *wsi,
                VkPhysicalDevice pdevice,
                WSI_FN_GetPhysicalDeviceProcAddr proc_addr,
                const VkAllocationCallbacks *alloc,
                int display_fd,
                const struct driOptionCache *dri_options,
                const struct wsi_device_options *device_options);

void
wsi_device_finish(struct wsi_device *wsi,
                  const VkAllocationCallbacks *alloc);

/* Setup file descriptor to be used with imported sync_fd's in wsi fences. */
void
wsi_device_setup_syncobj_fd(struct wsi_device *wsi_device,
                            int fd);

#define ICD_DEFINE_NONDISP_HANDLE_CASTS(__VkIcdType, __VkType)             \
                                                                           \
   static inline __VkIcdType *                                             \
   __VkIcdType ## _from_handle(__VkType _handle)                           \
   {                                                                       \
      return (__VkIcdType *)(uintptr_t) _handle;                           \
   }                                                                       \
                                                                           \
   static inline __VkType                                                  \
   __VkIcdType ## _to_handle(__VkIcdType *_obj)                            \
   {                                                                       \
      return (__VkType)(uintptr_t) _obj;                                   \
   }

#define ICD_FROM_HANDLE(__VkIcdType, __name, __handle) \
   __VkIcdType *__name = __VkIcdType ## _from_handle(__handle)

ICD_DEFINE_NONDISP_HANDLE_CASTS(VkIcdSurfaceBase, VkSurfaceKHR)

VkDeviceMemory
wsi_common_get_memory(VkSwapchainKHR _swapchain, uint32_t index);

VkResult
wsi_common_acquire_next_image2(const struct wsi_device *wsi,
                               VkDevice device,
                               const VkAcquireNextImageInfoKHR *pAcquireInfo,
                               uint32_t *pImageIndex);

VkResult
wsi_common_queue_present(const struct wsi_device *wsi,
                         struct vk_queue *queue,
                         const VkPresentInfoKHR *pPresentInfo);

static inline bool
wsi_common_is_swapchain_image(const VkImageCreateInfo *pCreateInfo)
{
#ifdef VK_USE_PLATFORM_ANDROID_KHR
   return false;
# else
   const VkImageSwapchainCreateInfoKHR *swapchain_info =
      vk_find_struct_const(pCreateInfo->pNext, IMAGE_SWAPCHAIN_CREATE_INFO_KHR);
   return swapchain_info && swapchain_info->swapchain != VK_NULL_HANDLE;
#endif
}

VkResult
wsi_common_create_swapchain_image(const struct wsi_device *wsi,
                                  const VkImageCreateInfo *pCreateInfo,
                                  VkImage *pImage);

VkImageUsageFlags
wsi_caps_get_image_usage(void);

bool
wsi_device_supports_explicit_sync(struct wsi_device *device);

#define wsi_common_vk_warn_once(warning) \
   do { \
      static int warned = false; \
      if (!warned) { \
         mesa_loge(warning); \
         warned = true; \
      } \
   } while (0)

#ifdef __cplusplus
}
#endif

#endif
