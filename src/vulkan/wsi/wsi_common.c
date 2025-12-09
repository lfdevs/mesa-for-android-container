/*
 * Copyright © 2017 Intel Corporation
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

#include "wsi_common_private.h"
#include "wsi_common_entrypoints.h"
#include "util/u_debug.h"
#include "util/macros.h"
#include "util/os_file.h"
#include "util/os_time.h"
#include "util/xmlconfig.h"
#include "vk_device.h"
#include "vk_fence.h"
#include "vk_format.h"
#include "vk_instance.h"
#include "vk_physical_device.h"
#include "vk_queue.h"
#include "vk_semaphore.h"
#include "vk_sync.h"
#include "vk_sync_dummy.h"
#include "vk_util.h"

#include <time.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef _WIN32
#include <unistd.h>
#endif

uint64_t WSI_DEBUG;

static const struct debug_control debug_control[] = {
   { "buffer",       WSI_DEBUG_BUFFER },
   { "sw",           WSI_DEBUG_SW },
   { "noshm",        WSI_DEBUG_NOSHM },
   { "linear",       WSI_DEBUG_LINEAR },
   { "dxgi",         WSI_DEBUG_DXGI },
   { "nowlts",       WSI_DEBUG_NOWLTS },
   { NULL, },
};

static bool present_false(VkPhysicalDevice pdevice, int fd) {
   return false;
}

VkResult
wsi_device_init(struct wsi_device *wsi,
                VkPhysicalDevice pdevice,
                WSI_FN_GetPhysicalDeviceProcAddr proc_addr,
                const VkAllocationCallbacks *alloc,
                int display_fd,
                const struct driOptionCache *dri_options,
                const struct wsi_device_options *device_options)
{
   const char *present_mode;
   UNUSED VkResult result;

   WSI_DEBUG = parse_debug_string(os_get_option("MESA_VK_WSI_DEBUG"), debug_control);

   util_cpu_trace_init();

   memset(wsi, 0, sizeof(*wsi));

   wsi->instance_alloc = *alloc;
   wsi->pdevice = pdevice;
   wsi->supports_scanout = true;
   wsi->sw = device_options->sw_device || (WSI_DEBUG & WSI_DEBUG_SW);
   wsi->wants_linear = (WSI_DEBUG & WSI_DEBUG_LINEAR) != 0;
   wsi->x11.extra_xwayland_image = device_options->extra_xwayland_image;
   wsi->wayland.disable_timestamps = (WSI_DEBUG & WSI_DEBUG_NOWLTS) != 0;
#define WSI_GET_CB(func) \
   PFN_vk##func func = (PFN_vk##func)proc_addr(pdevice, "vk" #func)
   WSI_GET_CB(GetPhysicalDeviceExternalSemaphoreProperties);
   WSI_GET_CB(GetPhysicalDeviceProperties2);
   WSI_GET_CB(GetPhysicalDeviceMemoryProperties);
   WSI_GET_CB(GetPhysicalDeviceQueueFamilyProperties);
#undef WSI_GET_CB

   wsi->drm_info.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT;
   wsi->pci_bus_info.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT;
   wsi->pci_bus_info.pNext = &wsi->drm_info;
   VkPhysicalDeviceProperties2 pdp2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      .pNext = &wsi->pci_bus_info,
   };
   GetPhysicalDeviceProperties2(pdevice, &pdp2);

   wsi->maxImageDimension2D = pdp2.properties.limits.maxImageDimension2D;
   assert(pdp2.properties.limits.optimalBufferCopyRowPitchAlignment <= UINT32_MAX);
   wsi->optimalBufferCopyRowPitchAlignment =
      pdp2.properties.limits.optimalBufferCopyRowPitchAlignment;
   wsi->override_present_mode = VK_PRESENT_MODE_MAX_ENUM_KHR;

   GetPhysicalDeviceMemoryProperties(pdevice, &wsi->memory_props);
   GetPhysicalDeviceQueueFamilyProperties(pdevice, &wsi->queue_family_count, NULL);

   assert(wsi->queue_family_count <= 64);
   VkQueueFamilyProperties queue_properties[64];
   GetPhysicalDeviceQueueFamilyProperties(pdevice, &wsi->queue_family_count, queue_properties);

   for (unsigned i = 0; i < wsi->queue_family_count; i++) {
      VkFlags req_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
      if (queue_properties[i].queueFlags & req_flags)
         wsi->queue_supports_blit |= BITFIELD64_BIT(i);
   }

   for (VkExternalSemaphoreHandleTypeFlags handle_type = 1;
        handle_type <= VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
        handle_type <<= 1) {
      VkPhysicalDeviceExternalSemaphoreInfo esi = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO,
         .handleType = handle_type,
      };
      VkExternalSemaphoreProperties esp = {
         .sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES,
      };
      GetPhysicalDeviceExternalSemaphoreProperties(pdevice, &esi, &esp);

      if (esp.externalSemaphoreFeatures &
          VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT)
         wsi->semaphore_export_handle_types |= handle_type;

      VkSemaphoreTypeCreateInfo timeline_tci = {
         .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
         .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR,
      };
      esi.pNext = &timeline_tci;
      GetPhysicalDeviceExternalSemaphoreProperties(pdevice, &esi, &esp);

      if (esp.externalSemaphoreFeatures &
          VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT)
         wsi->timeline_semaphore_export_handle_types |= handle_type;
   }

   const struct vk_device_extension_table *supported_extensions =
      &vk_physical_device_from_handle(pdevice)->supported_extensions;
   wsi->has_import_memory_host =
      supported_extensions->EXT_external_memory_host;
   wsi->khr_present_wait =
      supported_extensions->KHR_present_id &&
      supported_extensions->KHR_present_wait;
   wsi->has_timeline_semaphore =
      supported_extensions->KHR_timeline_semaphore;

   /* We cannot expose KHR_present_wait without timeline semaphores. */
   assert(!wsi->khr_present_wait || supported_extensions->KHR_timeline_semaphore);

   list_inithead(&wsi->hotplug_fences);

#define WSI_GET_CB(func) \
   wsi->func = (PFN_vk##func)proc_addr(pdevice, "vk" #func)
   WSI_GET_CB(AllocateMemory);
   WSI_GET_CB(AllocateCommandBuffers);
   WSI_GET_CB(BindBufferMemory);
   WSI_GET_CB(BindImageMemory);
   WSI_GET_CB(BeginCommandBuffer);
   WSI_GET_CB(CmdPipelineBarrier);
   WSI_GET_CB(CmdCopyImage);
   WSI_GET_CB(CmdCopyImageToBuffer);
   WSI_GET_CB(CreateBuffer);
   WSI_GET_CB(CreateCommandPool);
   WSI_GET_CB(CreateFence);
   WSI_GET_CB(CreateImage);
   WSI_GET_CB(CreateSemaphore);
   WSI_GET_CB(DestroyBuffer);
   WSI_GET_CB(DestroyCommandPool);
   WSI_GET_CB(DestroyFence);
   WSI_GET_CB(DestroyImage);
   WSI_GET_CB(DestroySemaphore);
   WSI_GET_CB(EndCommandBuffer);
   WSI_GET_CB(FreeMemory);
   WSI_GET_CB(FreeCommandBuffers);
   WSI_GET_CB(GetBufferMemoryRequirements);
   WSI_GET_CB(GetFenceStatus);
   WSI_GET_CB(GetImageDrmFormatModifierPropertiesEXT);
   WSI_GET_CB(GetImageMemoryRequirements);
   WSI_GET_CB(GetImageSubresourceLayout);
   if (!wsi->sw)
      WSI_GET_CB(GetMemoryFdKHR);
   WSI_GET_CB(GetPhysicalDeviceFormatProperties);
   WSI_GET_CB(GetPhysicalDeviceFormatProperties2);
   WSI_GET_CB(GetPhysicalDeviceImageFormatProperties2);
   WSI_GET_CB(GetSemaphoreFdKHR);
   WSI_GET_CB(ResetFences);
   WSI_GET_CB(QueueSubmit2);
   WSI_GET_CB(SetDebugUtilsObjectNameEXT);
   WSI_GET_CB(WaitForFences);
   WSI_GET_CB(MapMemory);
   WSI_GET_CB(UnmapMemory);
   if (wsi->khr_present_wait)
      WSI_GET_CB(WaitSemaphores);
#undef WSI_GET_CB

#if defined(VK_USE_PLATFORM_XCB_KHR)
   result = wsi_x11_init_wsi(wsi, alloc, dri_options);
   if (result != VK_SUCCESS)
      goto fail;
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   result = wsi_wl_init_wsi(wsi, alloc, pdevice);
   if (result != VK_SUCCESS)
      goto fail;
#endif

#ifdef VK_USE_PLATFORM_WIN32_KHR
   result = wsi_win32_init_wsi(wsi, alloc, pdevice);
   if (result != VK_SUCCESS)
      goto fail;
#endif

#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   result = wsi_display_init_wsi(wsi, alloc, display_fd);
   if (result != VK_SUCCESS)
      goto fail;
#endif

#ifdef VK_USE_PLATFORM_METAL_EXT
   result = wsi_metal_init_wsi(wsi, alloc, pdevice);
   if (result != VK_SUCCESS)
      goto fail;
#endif

#ifndef VK_USE_PLATFORM_WIN32_KHR
   result = wsi_headless_init_wsi(wsi, alloc, pdevice);
   if (result != VK_SUCCESS)
      goto fail;
#endif

   present_mode = os_get_option("MESA_VK_WSI_PRESENT_MODE");
   if (present_mode) {
      if (!strcmp(present_mode, "fifo")) {
         wsi->override_present_mode = VK_PRESENT_MODE_FIFO_KHR;
      } else if (!strcmp(present_mode, "relaxed")) {
          wsi->override_present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
      } else if (!strcmp(present_mode, "mailbox")) {
         wsi->override_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
      } else if (!strcmp(present_mode, "immediate")) {
         wsi->override_present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
      } else {
         fprintf(stderr, "Invalid MESA_VK_WSI_PRESENT_MODE value!\n");
      }
   }

   wsi->force_headless_swapchain =
      debug_get_bool_option("MESA_VK_WSI_HEADLESS_SWAPCHAIN", false);

   if (dri_options) {
      if (driCheckOption(dri_options, "adaptive_sync", DRI_BOOL))
         wsi->enable_adaptive_sync = driQueryOptionb(dri_options,
                                                     "adaptive_sync");

      if (driCheckOption(dri_options, "vk_wsi_force_bgra8_unorm_first",  DRI_BOOL)) {
         wsi->force_bgra8_unorm_first =
            driQueryOptionb(dri_options, "vk_wsi_force_bgra8_unorm_first");
      }

      if (driCheckOption(dri_options, "vk_wsi_force_swapchain_to_current_extent",  DRI_BOOL)) {
         wsi->force_swapchain_to_currentExtent =
            driQueryOptionb(dri_options, "vk_wsi_force_swapchain_to_current_extent");
      }

      if (driCheckOption(dri_options, "vk_wsi_disable_unordered_submits",  DRI_BOOL)) {
         wsi->disable_unordered_submits =
            driQueryOptionb(dri_options, "vk_wsi_disable_unordered_submits");
      }

   }

   /* can_present_on_device is a function pointer used to determine if images
    * can be presented directly on a given device file descriptor (fd).
    * If HAVE_LIBDRM is defined, it will be initialized to a platform-specific
    * function (wsi_device_matches_drm_fd). Otherwise, it is initialized to
    * present_false to ensure that it always returns false, preventing potential
    * segmentation faults from unchecked calls.
    * Drivers for non-PCI based GPUs are expected to override this after calling
    * wsi_device_init().
    */
#ifdef HAVE_LIBDRM
   wsi->can_present_on_device = wsi_device_matches_drm_fd;
#else
   wsi->can_present_on_device = present_false;
#endif

   return VK_SUCCESS;
fail:
   wsi_device_finish(wsi, alloc);
   return result;
}

void
wsi_device_finish(struct wsi_device *wsi,
                  const VkAllocationCallbacks *alloc)
{
#ifndef VK_USE_PLATFORM_WIN32_KHR
   wsi_headless_finish_wsi(wsi, alloc);
#endif
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   wsi_display_finish_wsi(wsi, alloc);
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   wsi_wl_finish_wsi(wsi, alloc);
#endif
#ifdef VK_USE_PLATFORM_WIN32_KHR
   wsi_win32_finish_wsi(wsi, alloc);
#endif
#if defined(VK_USE_PLATFORM_XCB_KHR)
   wsi_x11_finish_wsi(wsi, alloc);
#endif
#if defined(VK_USE_PLATFORM_METAL_EXT)
   wsi_metal_finish_wsi(wsi, alloc);
#endif
}

VKAPI_ATTR void VKAPI_CALL
wsi_DestroySurfaceKHR(VkInstance _instance,
                      VkSurfaceKHR _surface,
                      const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(vk_instance, instance, _instance);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);

   if (!surface)
      return;

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   if (surface->platform == VK_ICD_WSI_PLATFORM_WAYLAND) {
      wsi_wl_surface_destroy(surface, _instance, pAllocator);
      return;
   }
#endif
#ifdef VK_USE_PLATFORM_WIN32_KHR
   if (surface->platform == VK_ICD_WSI_PLATFORM_WIN32) {
      wsi_win32_surface_destroy(surface, _instance, pAllocator);
      return;
   }
#endif

   vk_free2(&instance->alloc, pAllocator, surface);
}

void
wsi_device_setup_syncobj_fd(struct wsi_device *wsi_device,
                            int fd)
{
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   wsi_display_setup_syncobj_fd(wsi_device, fd);
#endif
}

static enum wsi_swapchain_blit_type
get_blit_type(const struct wsi_device *wsi,
              const struct wsi_base_image_params *params,
              VkDevice device)
{
   switch (params->image_type) {
   case WSI_IMAGE_TYPE_CPU: {
      const struct wsi_cpu_image_params *cpu_params =
         container_of(params, const struct wsi_cpu_image_params, base);
      return wsi_cpu_image_needs_buffer_blit(wsi, cpu_params) ?
         WSI_SWAPCHAIN_BUFFER_BLIT : WSI_SWAPCHAIN_NO_BLIT;
   }
#ifdef HAVE_LIBDRM
   case WSI_IMAGE_TYPE_DRM: {
      const struct wsi_drm_image_params *drm_params =
         container_of(params, const struct wsi_drm_image_params, base);
      return wsi_drm_image_needs_buffer_blit(wsi, drm_params) ?
         WSI_SWAPCHAIN_BUFFER_BLIT : WSI_SWAPCHAIN_NO_BLIT;
   }
#endif
#ifdef _WIN32
   case WSI_IMAGE_TYPE_DXGI: {
      const struct wsi_dxgi_image_params *dxgi_params =
         container_of(params, const struct wsi_dxgi_image_params, base);
      return wsi_dxgi_image_needs_blit(wsi, dxgi_params, device);
   }
#endif
#if defined(VK_USE_PLATFORM_METAL_EXT)
   case WSI_IMAGE_TYPE_METAL: {
      /* Due to mismatches between WSI and Metal, we require rendering into an
       * intermediate texture and later blit that texture to the display
       * texture. There is not much we can do about this since users can get
       * the VkImages before acquiring the display image for command buffer
       * recording as long as they acquire the image before submission. Metal
       * on the other hand will only give us a texture handle after acquiring
       * which leads to us having to provide an intermediate texture just for
       * this. We could move the acquisition to the first usage of the VkImage
       * but that's something we can contemplate if the performace gain is
       * considerable. */
      return WSI_SWAPCHAIN_IMAGE_BLIT;
   }
#endif
   default:
      UNREACHABLE("Invalid image type");
   }
}

static VkResult
configure_image(const struct wsi_swapchain *chain,
                const VkSwapchainCreateInfoKHR *pCreateInfo,
                const struct wsi_base_image_params *params,
                struct wsi_image_info *info)
{
   info->image_type = params->image_type;
   info->color_space = pCreateInfo->imageColorSpace;

   switch (params->image_type) {
   case WSI_IMAGE_TYPE_CPU: {
      const struct wsi_cpu_image_params *cpu_params =
         container_of(params, const struct wsi_cpu_image_params, base);
      return wsi_configure_cpu_image(chain, pCreateInfo, cpu_params, info);
   }
#ifdef HAVE_LIBDRM
   case WSI_IMAGE_TYPE_DRM: {
      const struct wsi_drm_image_params *drm_params =
         container_of(params, const struct wsi_drm_image_params, base);
      return wsi_drm_configure_image(chain, pCreateInfo, drm_params, info);
   }
#endif
#ifdef _WIN32
   case WSI_IMAGE_TYPE_DXGI: {
      const struct wsi_dxgi_image_params *dxgi_params =
         container_of(params, const struct wsi_dxgi_image_params, base);
      return wsi_dxgi_configure_image(chain, pCreateInfo, dxgi_params, info);
   }
#endif
#if defined(VK_USE_PLATFORM_METAL_EXT)
   case WSI_IMAGE_TYPE_METAL: {
      const struct wsi_metal_image_params *metal_params =
         container_of(params, const struct wsi_metal_image_params, base);
      return wsi_metal_configure_image(chain, pCreateInfo, metal_params, info);
   }
#endif
   default:
      UNREACHABLE("Invalid image type");
   }
}

VkResult
wsi_swapchain_init(const struct wsi_device *wsi,
                   struct wsi_swapchain *chain,
                   VkDevice _device,
                   const VkSwapchainCreateInfoKHR *pCreateInfo,
                   const struct wsi_base_image_params *image_params,
                   const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(vk_device, device, _device);
   VkResult result;

   memset(chain, 0, sizeof(*chain));

   vk_object_base_init(device, &chain->base, VK_OBJECT_TYPE_SWAPCHAIN_KHR);

   chain->create_flags = pCreateInfo->flags;
   chain->wsi = wsi;
   chain->device = _device;
   chain->alloc = *pAllocator;
   chain->blit.type = get_blit_type(wsi, image_params, _device);

   chain->blit.queue = NULL;
   if (chain->blit.type != WSI_SWAPCHAIN_NO_BLIT) {
      if (wsi->get_blit_queue) {
         chain->blit.queue = wsi->get_blit_queue(_device);
      }

      int cmd_pools_count = chain->blit.queue != NULL ? 1 : wsi->queue_family_count;

      chain->cmd_pools =
         vk_zalloc(pAllocator, sizeof(VkCommandPool) * cmd_pools_count, 8,
                  VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!chain->cmd_pools)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      const VkCommandPoolCreateFlags cmd_pool_flags =
         (pCreateInfo->flags & VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR) ?
         VK_COMMAND_POOL_CREATE_PROTECTED_BIT : 0;
      for (uint32_t i = 0; i < cmd_pools_count; i++) {
         int queue_family_index = i;

         if (chain->blit.queue != NULL) {
            queue_family_index = chain->blit.queue->queue_family_index;
         } else {
            /* Queues returned by get_blit_queue() might not be listed in
            * GetPhysicalDeviceQueueFamilyProperties, so this check is skipped for those queues.
            */
            if (!(wsi->queue_supports_blit & BITFIELD64_BIT(queue_family_index)))
               continue;
         }

         const VkCommandPoolCreateInfo cmd_pool_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = NULL,
            .flags = cmd_pool_flags,
            .queueFamilyIndex = queue_family_index,
         };
         result = wsi->CreateCommandPool(_device, &cmd_pool_info, &chain->alloc,
                                       &chain->cmd_pools[i]);
         if (result != VK_SUCCESS)
            goto fail;
      }
   }

   result = configure_image(chain, pCreateInfo, image_params,
                            &chain->image_info);
   if (result != VK_SUCCESS)
      goto fail;

#ifdef HAVE_LIBDRM
   result = wsi_drm_init_swapchain_implicit_sync(chain);
   if (result != VK_SUCCESS)
      goto fail;
#endif

   return VK_SUCCESS;

fail:
   wsi_swapchain_finish(chain);
   return result;
}

static bool
wsi_swapchain_is_present_mode_supported(struct wsi_device *wsi,
                                        const VkSwapchainCreateInfoKHR *pCreateInfo,
                                        VkPresentModeKHR mode)
{
      ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, pCreateInfo->surface);
      struct wsi_interface *iface = wsi->wsi[surface->platform];
      VkPresentModeKHR *present_modes;
      uint32_t present_mode_count;
      bool supported = false;
      VkResult result;

      result = iface->get_present_modes(surface, wsi, &present_mode_count, NULL);
      if (result != VK_SUCCESS)
         return supported;

      present_modes = malloc(present_mode_count * sizeof(*present_modes));
      if (!present_modes)
         return supported;

      result = iface->get_present_modes(surface, wsi, &present_mode_count,
                                        present_modes);
      if (result != VK_SUCCESS)
         goto fail;

      for (uint32_t i = 0; i < present_mode_count; i++) {
         if (present_modes[i] == mode) {
            supported = true;
            break;
         }
      }

fail:
      free(present_modes);
      return supported;
}

VkPresentModeKHR
wsi_swapchain_get_present_mode(struct wsi_device *wsi,
                               const VkSwapchainCreateInfoKHR *pCreateInfo)
{
   if (wsi->override_present_mode == VK_PRESENT_MODE_MAX_ENUM_KHR)
      return pCreateInfo->presentMode;

   if (!wsi_swapchain_is_present_mode_supported(wsi, pCreateInfo,
                                                wsi->override_present_mode)) {
      fprintf(stderr, "Unsupported MESA_VK_WSI_PRESENT_MODE value!\n");
      return pCreateInfo->presentMode;
   }

   return wsi->override_present_mode;
}

void
wsi_swapchain_finish(struct wsi_swapchain *chain)
{
   wsi_destroy_image_info(chain, &chain->image_info);

   if (chain->fences) {
      for (unsigned i = 0; i < chain->image_count; i++)
         chain->wsi->DestroyFence(chain->device, chain->fences[i], &chain->alloc);

      vk_free(&chain->alloc, chain->fences);
   }
   if (chain->blit.semaphores) {
      for (unsigned i = 0; i < chain->image_count; i++)
         chain->wsi->DestroySemaphore(chain->device, chain->blit.semaphores[i], &chain->alloc);

      vk_free(&chain->alloc, chain->blit.semaphores);
   }
   chain->wsi->DestroySemaphore(chain->device, chain->dma_buf_semaphore,
                                &chain->alloc);
   chain->wsi->DestroySemaphore(chain->device, chain->present_id_timeline,
                                &chain->alloc);

   if (chain->blit.type != WSI_SWAPCHAIN_NO_BLIT) {
      int cmd_pools_count = chain->blit.queue != NULL ?
         1 : chain->wsi->queue_family_count;
      for (uint32_t i = 0; i < cmd_pools_count; i++) {
         if (!chain->cmd_pools[i])
            continue;
         chain->wsi->DestroyCommandPool(chain->device, chain->cmd_pools[i],
                                       &chain->alloc);
      }
      vk_free(&chain->alloc, chain->cmd_pools);
   }

   vk_object_base_finish(&chain->base);
}

VkResult
wsi_configure_image(const struct wsi_swapchain *chain,
                    const VkSwapchainCreateInfoKHR *pCreateInfo,
                    VkExternalMemoryHandleTypeFlags handle_types,
                    struct wsi_image_info *info)
{
   memset(info, 0, sizeof(*info));
   uint32_t queue_family_count = 1;

   if (pCreateInfo->imageSharingMode == VK_SHARING_MODE_CONCURRENT)
      queue_family_count = pCreateInfo->queueFamilyIndexCount;

   /*
    * TODO: there should be no reason to allocate this, but
    * 15331 shows that games crashed without doing this.
    */
   uint32_t *queue_family_indices =
      vk_alloc(&chain->alloc,
               sizeof(*queue_family_indices) *
               queue_family_count,
               8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!queue_family_indices)
      goto err_oom;

   if (pCreateInfo->imageSharingMode == VK_SHARING_MODE_CONCURRENT)
      for (uint32_t i = 0; i < pCreateInfo->queueFamilyIndexCount; i++)
         queue_family_indices[i] = pCreateInfo->pQueueFamilyIndices[i];

   const VkImageCreateFlags protected_flag =
      (pCreateInfo->flags & VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR) ?
      VK_IMAGE_CREATE_PROTECTED_BIT : 0;

   info->create = (VkImageCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .flags = VK_IMAGE_CREATE_ALIAS_BIT | protected_flag,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = pCreateInfo->imageFormat,
      .extent = {
         .width = pCreateInfo->imageExtent.width,
         .height = pCreateInfo->imageExtent.height,
         .depth = 1,
      },
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = pCreateInfo->imageUsage,
      .sharingMode = pCreateInfo->imageSharingMode,
      .queueFamilyIndexCount = queue_family_count,
      .pQueueFamilyIndices = queue_family_indices,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };

   info->color_space = pCreateInfo->imageColorSpace;

   if (handle_types != 0) {
      info->ext_mem = (VkExternalMemoryImageCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
         .handleTypes = handle_types,
      };
      __vk_append_struct(&info->create, &info->ext_mem);
   }

   info->wsi = (struct wsi_image_create_info) {
      .sType = VK_STRUCTURE_TYPE_WSI_IMAGE_CREATE_INFO_MESA,
   };
   __vk_append_struct(&info->create, &info->wsi);

   if (pCreateInfo->flags & VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR) {
      info->create.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT |
                            VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;

      const VkImageFormatListCreateInfo *format_list_in =
         vk_find_struct_const(pCreateInfo->pNext,
                              IMAGE_FORMAT_LIST_CREATE_INFO);

      assume(format_list_in && format_list_in->viewFormatCount > 0);

      const uint32_t view_format_count = format_list_in->viewFormatCount;
      VkFormat *view_formats =
         vk_alloc(&chain->alloc, sizeof(VkFormat) * view_format_count,
                  8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!view_formats)
         goto err_oom;

      ASSERTED bool format_found = false;
      for (uint32_t i = 0; i < format_list_in->viewFormatCount; i++) {
         if (pCreateInfo->imageFormat == format_list_in->pViewFormats[i])
            format_found = true;
         view_formats[i] = format_list_in->pViewFormats[i];
      }
      assert(format_found);

      info->format_list = (VkImageFormatListCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
         .viewFormatCount = view_format_count,
         .pViewFormats = view_formats,
      };
      __vk_append_struct(&info->create, &info->format_list);
   }

   return VK_SUCCESS;

err_oom:
   wsi_destroy_image_info(chain, info);
   return VK_ERROR_OUT_OF_HOST_MEMORY;
}

void
wsi_destroy_image_info(const struct wsi_swapchain *chain,
                       struct wsi_image_info *info)
{
   if (info->create.pQueueFamilyIndices != NULL) {
      vk_free(&chain->alloc, (void *)info->create.pQueueFamilyIndices);
      info->create.pQueueFamilyIndices = NULL;
   }
   if (info->format_list.pViewFormats != NULL) {
      vk_free(&chain->alloc, (void *)info->format_list.pViewFormats);
      info->format_list.pViewFormats = NULL;
   }
   if (info->drm_mod_list.pDrmFormatModifiers != NULL) {
      vk_free(&chain->alloc, (void *)info->drm_mod_list.pDrmFormatModifiers);
      info->drm_mod_list.pDrmFormatModifiers = NULL;
   }
   if (info->modifier_props != NULL) {
      vk_free(&chain->alloc, info->modifier_props);
      info->modifier_props = NULL;
   }
}

VkResult
wsi_create_image(const struct wsi_swapchain *chain,
                 const struct wsi_image_info *info,
                 struct wsi_image *image)
{
   const struct wsi_device *wsi = chain->wsi;
   VkResult result;

   memset(image, 0, sizeof(*image));

#ifndef _WIN32
   image->dma_buf_fd = -1;
   for (uint32_t i = 0; i < WSI_ES_COUNT; i++)
      image->explicit_sync[i].fd = -1;
#endif

   result = wsi->CreateImage(chain->device, &info->create,
                             &chain->alloc, &image->image);
   if (result != VK_SUCCESS)
      goto fail;

   result = info->create_mem(chain, info, image);
   if (result != VK_SUCCESS)
      goto fail;

   result = wsi->BindImageMemory(chain->device, image->image,
                                 image->memory, 0);
   if (result != VK_SUCCESS)
      goto fail;

   if (info->finish_create) {
      result = info->finish_create(chain, info, image);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (info->explicit_sync) {
#if HAVE_LIBDRM
      result = wsi_create_image_explicit_sync_drm(chain, image);
      if (result != VK_SUCCESS)
         goto fail;
#else
      result = VK_ERROR_FEATURE_NOT_PRESENT;
      goto fail;
#endif
   }

   return VK_SUCCESS;

fail:
   wsi_destroy_image(chain, image);
   return result;
}

void
wsi_destroy_image(const struct wsi_swapchain *chain,
                  struct wsi_image *image)
{
   const struct wsi_device *wsi = chain->wsi;

#ifndef _WIN32
   if (image->dma_buf_fd >= 0)
      close(image->dma_buf_fd);
#endif

   if (image->explicit_sync[WSI_ES_ACQUIRE].semaphore) {
#if HAVE_LIBDRM
      wsi_destroy_image_explicit_sync_drm(chain, image);
#endif
   }

   if (image->cpu_map != NULL) {
      wsi->UnmapMemory(chain->device, image->blit.buffer != VK_NULL_HANDLE ?
                                      image->blit.memory : image->memory);
   }

   if (image->blit.cmd_buffers) {
      int cmd_buffer_count =
         chain->blit.queue != NULL ? 1 : wsi->queue_family_count;

      for (uint32_t i = 0; i < cmd_buffer_count; i++) {
         if (!chain->cmd_pools[i])
            continue;
         wsi->FreeCommandBuffers(chain->device, chain->cmd_pools[i],
                                 1, &image->blit.cmd_buffers[i]);
      }
      vk_free(&chain->alloc, image->blit.cmd_buffers);
   }

   wsi->FreeMemory(chain->device, image->memory, &chain->alloc);
   wsi->DestroyImage(chain->device, image->image, &chain->alloc);
   wsi->DestroyImage(chain->device, image->blit.image, &chain->alloc);
   wsi->FreeMemory(chain->device, image->blit.memory, &chain->alloc);
   wsi->DestroyBuffer(chain->device, image->blit.buffer, &chain->alloc);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice,
                                       uint32_t queueFamilyIndex,
                                       VkSurfaceKHR _surface,
                                       VkBool32 *pSupported)
{
   VK_FROM_HANDLE(vk_physical_device, device, physicalDevice);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);
   struct wsi_device *wsi_device = device->wsi_device;
   struct wsi_interface *iface = wsi_device->wsi[surface->platform];

   VkResult res = iface->get_support(surface, wsi_device,
                                     queueFamilyIndex, pSupported);
   if (res == VK_SUCCESS) {
      bool blit = (wsi_device->queue_supports_blit & BITFIELD64_BIT(queueFamilyIndex)) != 0;
      *pSupported = (bool)*pSupported && blit;
   }

   return res;
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetPhysicalDeviceSurfaceCapabilitiesKHR(
   VkPhysicalDevice physicalDevice,
   VkSurfaceKHR _surface,
   VkSurfaceCapabilitiesKHR *pSurfaceCapabilities)
{
   VK_FROM_HANDLE(vk_physical_device, device, physicalDevice);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);
   struct wsi_device *wsi_device = device->wsi_device;
   struct wsi_interface *iface = wsi_device->wsi[surface->platform];

   VkSurfaceCapabilities2KHR caps2 = {
      .sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR,
   };

   VkResult result = iface->get_capabilities2(surface, wsi_device, NULL, &caps2);

   if (result == VK_SUCCESS)
      *pSurfaceCapabilities = caps2.surfaceCapabilities;

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetPhysicalDeviceSurfaceCapabilities2KHR(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo,
   VkSurfaceCapabilities2KHR *pSurfaceCapabilities)
{
   VK_FROM_HANDLE(vk_physical_device, device, physicalDevice);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, pSurfaceInfo->surface);
   struct wsi_device *wsi_device = device->wsi_device;
   struct wsi_interface *iface = wsi_device->wsi[surface->platform];

   return iface->get_capabilities2(surface, wsi_device, pSurfaceInfo->pNext,
                                   pSurfaceCapabilities);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetPhysicalDeviceSurfaceCapabilities2EXT(
   VkPhysicalDevice physicalDevice,
   VkSurfaceKHR _surface,
   VkSurfaceCapabilities2EXT *pSurfaceCapabilities)
{
   VK_FROM_HANDLE(vk_physical_device, device, physicalDevice);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);
   struct wsi_device *wsi_device = device->wsi_device;
   struct wsi_interface *iface = wsi_device->wsi[surface->platform];

   assert(pSurfaceCapabilities->sType ==
          VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_EXT);

   struct wsi_surface_supported_counters counters = {
      .sType = VK_STRUCTURE_TYPE_WSI_SURFACE_SUPPORTED_COUNTERS_MESA,
      .pNext = pSurfaceCapabilities->pNext,
      .supported_surface_counters = 0,
   };

   VkSurfaceCapabilities2KHR caps2 = {
      .sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR,
      .pNext = &counters,
   };

   VkResult result = iface->get_capabilities2(surface, wsi_device, NULL, &caps2);

   if (result == VK_SUCCESS) {
      VkSurfaceCapabilities2EXT *ext_caps = pSurfaceCapabilities;
      VkSurfaceCapabilitiesKHR khr_caps = caps2.surfaceCapabilities;

      ext_caps->minImageCount = khr_caps.minImageCount;
      ext_caps->maxImageCount = khr_caps.maxImageCount;
      ext_caps->currentExtent = khr_caps.currentExtent;
      ext_caps->minImageExtent = khr_caps.minImageExtent;
      ext_caps->maxImageExtent = khr_caps.maxImageExtent;
      ext_caps->maxImageArrayLayers = khr_caps.maxImageArrayLayers;
      ext_caps->supportedTransforms = khr_caps.supportedTransforms;
      ext_caps->currentTransform = khr_caps.currentTransform;
      ext_caps->supportedCompositeAlpha = khr_caps.supportedCompositeAlpha;
      ext_caps->supportedUsageFlags = khr_caps.supportedUsageFlags;
      ext_caps->supportedSurfaceCounters = counters.supported_surface_counters;
   }

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice,
                                       VkSurfaceKHR _surface,
                                       uint32_t *pSurfaceFormatCount,
                                       VkSurfaceFormatKHR *pSurfaceFormats)
{
   VK_FROM_HANDLE(vk_physical_device, device, physicalDevice);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);
   struct wsi_device *wsi_device = device->wsi_device;
   struct wsi_interface *iface = wsi_device->wsi[surface->platform];

   return iface->get_formats(surface, wsi_device,
                             pSurfaceFormatCount, pSurfaceFormats);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice,
                                        const VkPhysicalDeviceSurfaceInfo2KHR * pSurfaceInfo,
                                        uint32_t *pSurfaceFormatCount,
                                        VkSurfaceFormat2KHR *pSurfaceFormats)
{
   VK_FROM_HANDLE(vk_physical_device, device, physicalDevice);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, pSurfaceInfo->surface);
   struct wsi_device *wsi_device = device->wsi_device;
   struct wsi_interface *iface = wsi_device->wsi[surface->platform];

   return iface->get_formats2(surface, wsi_device, pSurfaceInfo->pNext,
                              pSurfaceFormatCount, pSurfaceFormats);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice,
                                            VkSurfaceKHR _surface,
                                            uint32_t *pPresentModeCount,
                                            VkPresentModeKHR *pPresentModes)
{
   VK_FROM_HANDLE(vk_physical_device, device, physicalDevice);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);
   struct wsi_device *wsi_device = device->wsi_device;
   struct wsi_interface *iface = wsi_device->wsi[surface->platform];

   return iface->get_present_modes(surface, wsi_device, pPresentModeCount,
                                   pPresentModes);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice physicalDevice,
                                          VkSurfaceKHR _surface,
                                          uint32_t *pRectCount,
                                          VkRect2D *pRects)
{
   VK_FROM_HANDLE(vk_physical_device, device, physicalDevice);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);
   struct wsi_device *wsi_device = device->wsi_device;
   struct wsi_interface *iface = wsi_device->wsi[surface->platform];

   return iface->get_present_rectangles(surface, wsi_device,
                                        pRectCount, pRects);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_CreateSwapchainKHR(VkDevice _device,
                       const VkSwapchainCreateInfoKHR *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator,
                       VkSwapchainKHR *pSwapchain)
{
   MESA_TRACE_FUNC();
   VK_FROM_HANDLE(vk_device, device, _device);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, pCreateInfo->surface);
   struct wsi_device *wsi_device = device->physical->wsi_device;
   struct wsi_interface *iface = wsi_device->force_headless_swapchain ?
      wsi_device->wsi[VK_ICD_WSI_PLATFORM_HEADLESS] :
      wsi_device->wsi[surface->platform];
   const VkAllocationCallbacks *alloc;
   struct wsi_swapchain *swapchain;

   if (pAllocator)
     alloc = pAllocator;
   else
     alloc = &device->alloc;

   VkSwapchainCreateInfoKHR info = *pCreateInfo;

   if (wsi_device->force_swapchain_to_currentExtent) {
      VkSurfaceCapabilities2KHR caps2 = {
         .sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR,
      };
      iface->get_capabilities2(surface, wsi_device, NULL, &caps2);

      /* 0xffffffff (UINT32_MAX) indicates that the surface has no intrinsic extent, so the size of the
       * surface will be the size of the swapchain. In this case, overriding the swapchain size makes
       * no sense.
       */
      if (caps2.surfaceCapabilities.currentExtent.width != UINT32_MAX)
         info.imageExtent = caps2.surfaceCapabilities.currentExtent;
   }

   /* Ignore DEFERRED_MEMORY_ALLOCATION_BIT. Would require deep plumbing to be able to take advantage of it.
    * bool deferred_allocation = pCreateInfo->flags & VK_SWAPCHAIN_CREATE_DEFERRED_MEMORY_ALLOCATION_BIT_EXT;
    */

   VkResult result = iface->create_swapchain(surface, _device, wsi_device,
                                             &info, alloc,
                                             &swapchain);
   if (result != VK_SUCCESS)
      return result;

   swapchain->fences = vk_zalloc(alloc,
                                 sizeof (*swapchain->fences) * swapchain->image_count,
                                 sizeof (*swapchain->fences),
                                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!swapchain->fences) {
      swapchain->destroy(swapchain, alloc);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   if (wsi_device->khr_present_wait) {
      const VkSemaphoreTypeCreateInfo type_info = {
         .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
         .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
      };

      const VkSemaphoreCreateInfo sem_info = {
         .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
         .pNext = &type_info,
         .flags = 0,
      };

      /* We assume here that a driver exposing present_wait also exposes VK_KHR_timeline_semaphore. */
      result = wsi_device->CreateSemaphore(_device, &sem_info, alloc, &swapchain->present_id_timeline);
      if (result != VK_SUCCESS) {
         swapchain->destroy(swapchain, alloc);
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }

   if (swapchain->blit.queue != NULL) {
      swapchain->blit.semaphores = vk_zalloc(alloc,
                                         sizeof (*swapchain->blit.semaphores) * swapchain->image_count,
                                         sizeof (*swapchain->blit.semaphores),
                                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!swapchain->blit.semaphores) {
         swapchain->destroy(swapchain, alloc);
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }

   *pSwapchain = wsi_swapchain_to_handle(swapchain);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
wsi_DestroySwapchainKHR(VkDevice _device,
                        VkSwapchainKHR _swapchain,
                        const VkAllocationCallbacks *pAllocator)
{
   MESA_TRACE_FUNC();
   VK_FROM_HANDLE(vk_device, device, _device);
   VK_FROM_HANDLE(wsi_swapchain, swapchain, _swapchain);
   const VkAllocationCallbacks *alloc;

   if (!swapchain)
      return;

   if (pAllocator)
     alloc = pAllocator;
   else
     alloc = &device->alloc;

   swapchain->destroy(swapchain, alloc);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_ReleaseSwapchainImagesKHR(VkDevice _device,
                              const VkReleaseSwapchainImagesInfoKHR *pReleaseInfo)
{
   VK_FROM_HANDLE(wsi_swapchain, swapchain, pReleaseInfo->swapchain);

   for (uint32_t i = 0; i < pReleaseInfo->imageIndexCount; i++) {
      uint32_t index = pReleaseInfo->pImageIndices[i];
      assert(index < swapchain->image_count);
      struct wsi_image *image = swapchain->get_wsi_image(swapchain, index);
      assert(image->acquired);
      image->acquired = false;
   }

   VkResult result = swapchain->release_images(swapchain,
                                               pReleaseInfo->imageIndexCount,
                                               pReleaseInfo->pImageIndices);

   if (result != VK_SUCCESS)
      return result;

   if (swapchain->wsi->set_memory_ownership) {
      for (uint32_t i = 0; i < pReleaseInfo->imageIndexCount; i++) {
         uint32_t image_index = pReleaseInfo->pImageIndices[i];
         VkDeviceMemory mem = swapchain->get_wsi_image(swapchain, image_index)->memory;
         swapchain->wsi->set_memory_ownership(swapchain->device, mem, false);
      }
   }

   return VK_SUCCESS;
}

VkDeviceMemory
wsi_common_get_memory(VkSwapchainKHR _swapchain, uint32_t index)
{
   VK_FROM_HANDLE(wsi_swapchain, swapchain, _swapchain);
   assert(index < swapchain->image_count);
   return swapchain->get_wsi_image(swapchain, index)->memory;
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetSwapchainImagesKHR(VkDevice device,
                          VkSwapchainKHR _swapchain,
                          uint32_t *pSwapchainImageCount,
                          VkImage *pSwapchainImages)
{
   MESA_TRACE_FUNC();
   VK_FROM_HANDLE(wsi_swapchain, swapchain, _swapchain);
   VK_OUTARRAY_MAKE_TYPED(VkImage, images, pSwapchainImages, pSwapchainImageCount);

   for (uint32_t i = 0; i < swapchain->image_count; i++) {
      vk_outarray_append_typed(VkImage, &images, image) {
         *image = swapchain->get_wsi_image(swapchain, i)->image;
      }
   }

   return vk_outarray_status(&images);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_AcquireNextImageKHR(VkDevice _device,
                        VkSwapchainKHR swapchain,
                        uint64_t timeout,
                        VkSemaphore semaphore,
                        VkFence fence,
                        uint32_t *pImageIndex)
{
   MESA_TRACE_FUNC();
   VK_FROM_HANDLE(vk_device, device, _device);

   const VkAcquireNextImageInfoKHR acquire_info = {
      .sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
      .swapchain = swapchain,
      .timeout = timeout,
      .semaphore = semaphore,
      .fence = fence,
      .deviceMask = 0,
   };

   return device->dispatch_table.AcquireNextImage2KHR(_device, &acquire_info,
                                                      pImageIndex);
}

static VkResult
wsi_signal_semaphore_for_image(struct vk_device *device,
                               const struct wsi_swapchain *chain,
                               const struct wsi_image *image,
                               VkSemaphore _semaphore)
{
   if (device->physical->supported_sync_types == NULL)
      return VK_SUCCESS;

   VK_FROM_HANDLE(vk_semaphore, semaphore, _semaphore);

   vk_semaphore_reset_temporary(device, semaphore);

#ifdef HAVE_LIBDRM
   VkResult result = chain->image_info.explicit_sync ?
      wsi_create_sync_for_image_syncobj(chain, image,
                                        VK_SYNC_FEATURE_GPU_WAIT,
                                        &semaphore->temporary) :
      wsi_create_sync_for_dma_buf_wait(chain, image,
                                       VK_SYNC_FEATURE_GPU_WAIT,
                                       &semaphore->temporary);
   if (result != VK_ERROR_FEATURE_NOT_PRESENT)
      return result;
#endif

   return vk_sync_create(device, &vk_sync_dummy_type,
                         0 /* flags */, 0 /* initial_value */,
                         &semaphore->temporary);
}

static VkResult
wsi_signal_fence_for_image(struct vk_device *device,
                           const struct wsi_swapchain *chain,
                           const struct wsi_image *image,
                           VkFence _fence)
{
   if (device->physical->supported_sync_types == NULL)
      return VK_SUCCESS;

   VK_FROM_HANDLE(vk_fence, fence, _fence);

   vk_fence_reset_temporary(device, fence);

#ifdef HAVE_LIBDRM
   VkResult result = chain->image_info.explicit_sync ?
      wsi_create_sync_for_image_syncobj(chain, image,
                                        VK_SYNC_FEATURE_CPU_WAIT,
                                        &fence->temporary) :
      wsi_create_sync_for_dma_buf_wait(chain, image,
                                       VK_SYNC_FEATURE_CPU_WAIT,
                                       &fence->temporary);
   if (result != VK_ERROR_FEATURE_NOT_PRESENT)
      return result;
#endif

   return vk_sync_create(device, &vk_sync_dummy_type,
                         0 /* flags */, 0 /* initial_value */,
                         &fence->temporary);
}

VkResult
wsi_common_acquire_next_image2(const struct wsi_device *wsi,
                               VkDevice _device,
                               const VkAcquireNextImageInfoKHR *pAcquireInfo,
                               uint32_t *pImageIndex)
{
   VK_FROM_HANDLE(wsi_swapchain, swapchain, pAcquireInfo->swapchain);
   VK_FROM_HANDLE(vk_device, device, _device);

   VkResult result = swapchain->acquire_next_image(swapchain, pAcquireInfo,
                                                   pImageIndex);
   if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
      return result;
   struct wsi_image *image =
      swapchain->get_wsi_image(swapchain, *pImageIndex);

   image->acquired = true;

   if (pAcquireInfo->semaphore != VK_NULL_HANDLE) {
      VkResult signal_result =
         wsi_signal_semaphore_for_image(device, swapchain, image,
                                        pAcquireInfo->semaphore);
      if (signal_result != VK_SUCCESS)
         return signal_result;
   }

   if (pAcquireInfo->fence != VK_NULL_HANDLE) {
      VkResult signal_result =
         wsi_signal_fence_for_image(device, swapchain, image,
                                    pAcquireInfo->fence);
      if (signal_result != VK_SUCCESS)
         return signal_result;
   }

   if (wsi->set_memory_ownership)
      wsi->set_memory_ownership(swapchain->device, image->memory, true);

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_AcquireNextImage2KHR(VkDevice _device,
                         const VkAcquireNextImageInfoKHR *pAcquireInfo,
                         uint32_t *pImageIndex)
{
   MESA_TRACE_FUNC();
   VK_FROM_HANDLE(vk_device, device, _device);

   return wsi_common_acquire_next_image2(device->physical->wsi_device,
                                         _device, pAcquireInfo, pImageIndex);
}

static VkResult
handle_trace(struct vk_queue *queue, struct vk_device *device,
             uint32_t current_frame)
{
   struct vk_instance *instance = device->physical->instance;
   if (!instance->trace_mode)
      return VK_SUCCESS;

   simple_mtx_lock(&device->trace_mtx);

   bool frame_trigger = device->current_frame == instance->trace_frame;

   bool file_trigger = false;
#ifndef _WIN32
   if (instance->trace_trigger_file && access(instance->trace_trigger_file, W_OK) == 0) {
      if (unlink(instance->trace_trigger_file) == 0) {
         file_trigger = true;
      } else {
         /* Do not enable tracing if we cannot remove the file,
          * because by then we'll trace every frame ... */
         fprintf(stderr, "Could not remove trace trigger file, ignoring\n");
      }
   }
#endif

   VkResult result = VK_SUCCESS;
   if (frame_trigger || file_trigger || device->trace_hotkey_trigger)
      result = device->capture_trace(vk_queue_to_handle(queue));

   device->trace_hotkey_trigger = false;

   simple_mtx_unlock(&device->trace_mtx);

   return result;
}

/* You can treat this like vkQueueSubmit2() except that it doesn't obey the
 * implicit ordering of submits to queues.  Instead, each submit needs
 * explicit semaphores to ensure ordering.
 */
static VkResult
wsi_queue_submit2_unordered(const struct wsi_device *wsi,
                            struct vk_queue *queue,
                            const VkSubmitInfo2 *info,
                            uint32_t fence_count,
                            const VkFence *fences)
{
   if (!wsi->disable_unordered_submits &&
       info->commandBufferInfoCount == 0 &&
       queue->base.device->copy_sync_payloads != NULL) {
      /* This helper is unordered so if there are no command buffers, we can
       * just signal the signal semaphores and fences with the wait semaphores
       * and skip the queue entirely.
       */
      return vk_device_copy_semaphore_payloads(queue->base.device,
                                               info->waitSemaphoreInfoCount,
                                               info->pWaitSemaphoreInfos,
                                               info->signalSemaphoreInfoCount,
                                               info->pSignalSemaphoreInfos,
                                               fence_count, fences);
   }

   VkResult result = wsi->QueueSubmit2(vk_queue_to_handle(queue), 1, info,
                                       fence_count > 0 ? fences[0]
                                                       : VK_NULL_HANDLE);
   if (result != VK_SUCCESS)
      return result;

   for (uint32_t i = 1; i < fence_count; i++) {
      const VkSubmitInfo2 submit_info = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      };
      result = wsi->QueueSubmit2(vk_queue_to_handle(queue),
                                 1, &submit_info, fences[i]);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

struct wsi_image_signal_info {
   uint64_t present_id;
   uint32_t semaphore_count;
   VkSemaphoreSubmitInfo semaphore_infos[2];
   uint32_t fence_count;
   VkFence fences[2];
};

static void
wsi_image_signal_info_init(struct wsi_image_signal_info *info)
{
   memset(info, 0, sizeof(*info));
}

static void
wsi_image_signal_info_add_semaphore(struct wsi_image_signal_info *info,
                                    VkSemaphoreSubmitInfo sem_info)
{
   assert(info->semaphore_count < ARRAY_SIZE(info->semaphore_infos));
   info->semaphore_infos[info->semaphore_count++] = sem_info;
}

static void
wsi_image_signal_info_add_fence(struct wsi_image_signal_info *info,
                                VkFence fence)
{
   assert(info->fence_count < ARRAY_SIZE(info->fences));
   info->fences[info->fence_count++] = fence;
}

VkResult
wsi_common_queue_present(const struct wsi_device *wsi,
                         struct vk_queue *queue,
                         const VkPresentInfoKHR *pPresentInfo)
{
   struct vk_device *dev = queue->base.device;
   uint32_t current_frame = p_atomic_fetch_add(&dev->current_frame, 1);
   VkResult final_result = handle_trace(queue, dev, current_frame);

   STACK_ARRAY(VkResult, results, pPresentInfo->swapchainCount);
   for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++)
      results[i] = VK_SUCCESS;

   /* First, do the throttle waits, creating the throttle fences if needed */
   for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
      VK_FROM_HANDLE(wsi_swapchain, swapchain, pPresentInfo->pSwapchains[i]);
      uint32_t image_index = pPresentInfo->pImageIndices[i];

      if (swapchain->fences[image_index] == VK_NULL_HANDLE) {
         const VkFenceCreateInfo fence_info = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = NULL,
         };
         results[i] = wsi->CreateFence(vk_device_to_handle(dev), &fence_info,
                                       &swapchain->alloc,
                                       &swapchain->fences[image_index]);
         if (results[i] != VK_SUCCESS)
            continue;
      } else {
         MESA_TRACE_SCOPE("throttle");
         results[i] = wsi->WaitForFences(vk_device_to_handle(dev),
                                         1, &swapchain->fences[image_index],
                                         true, ~0ull);
         if (results[i] != VK_SUCCESS)
            continue;

         results[i] = wsi->ResetFences(vk_device_to_handle(dev),
                                       1, &swapchain->fences[image_index]);
         if (results[i] != VK_SUCCESS)
            continue;
      }
   }

   /* Gather up all the semaphores we need to wait on */
   STACK_ARRAY(VkSemaphoreSubmitInfo, semaphore_wait_infos,
               pPresentInfo->waitSemaphoreCount);
   for (uint32_t i = 0; i < pPresentInfo->waitSemaphoreCount; i++) {
      semaphore_wait_infos[i] = (VkSemaphoreSubmitInfo) {
         .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
         .semaphore = pPresentInfo->pWaitSemaphores[i],
         /* From the Vulkan 1.4.325 spec:
          *
          *    "Semaphore Waiting
          *
          *    In the case of vkQueueSubmit, the second synchronization scope
          *    is limited to operations on the pipeline stages determined by
          *    the destination stage mask specified by the corresponding
          *    element of pWaitDstStageMask. In the case of vkQueueSubmit2,
          *    the second synchronization scope is limited to the pipeline
          *    stage specified by VkSemaphoreSubmitInfo::stageMask."
          *
          * So the stageMask controls not what we're waiting on but who on
          * this queue is waiting.  Since we only ever either submit nothing
          * or submit a blit, the only thing that needs to block on the wait
          * semaphores are blits (which are transfer ops) and other semaphores
          * and fences.  Therefore, it's safe to always use TRANSFER_BIT as
          * long as we use it for all the semaphore ops on queues (to ensure
          * transitivity).
          */
         .stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
      };
   }

   const VkPresentIdKHR *present_ids =
      vk_find_struct_const(pPresentInfo->pNext, PRESENT_ID_KHR);
   const VkPresentId2KHR *present_ids2 =
      vk_find_struct_const(pPresentInfo->pNext, PRESENT_ID_2_KHR);
   const VkSwapchainPresentFenceInfoKHR *present_fence_info =
      vk_find_struct_const(pPresentInfo->pNext, SWAPCHAIN_PRESENT_FENCE_INFO_KHR);

   /* Gather up all the semaphores and fences we need to signal per-image */
   STACK_ARRAY(struct wsi_image_signal_info, image_signal_infos,
               pPresentInfo->swapchainCount);
   for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
      VK_FROM_HANDLE(wsi_swapchain, swapchain, pPresentInfo->pSwapchains[i]);
      uint32_t image_index = pPresentInfo->pImageIndices[i];
      struct wsi_image *image =
         swapchain->get_wsi_image(swapchain, image_index);

      wsi_image_signal_info_init(&image_signal_infos[i]);

      wsi_image_signal_info_add_fence(&image_signal_infos[i],
                                      swapchain->fences[image_index]);

      if (swapchain->image_info.explicit_sync) {
         /* We will signal this acquire value ourselves when GPU work is done. */
         image->explicit_sync[WSI_ES_ACQUIRE].timeline++;
         /* The compositor will signal this value when it is done with the image. */
         image->explicit_sync[WSI_ES_RELEASE].timeline++;

         const VkSemaphoreSubmitInfo sem_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .semaphore = image->explicit_sync[WSI_ES_ACQUIRE].semaphore,
            .value = image->explicit_sync[WSI_ES_ACQUIRE].timeline,
         };
         wsi_image_signal_info_add_semaphore(&image_signal_infos[i], sem_info);
      } else if (swapchain->dma_buf_semaphore != VK_NULL_HANDLE) {
         const VkSemaphoreSubmitInfo sem_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .semaphore = swapchain->dma_buf_semaphore,
         };
         wsi_image_signal_info_add_semaphore(&image_signal_infos[i], sem_info);
      }

      uint64_t present_id = 0;
      if (present_ids && present_ids->pPresentIds)
         present_id = present_ids->pPresentIds[i];
      if (present_ids2 && present_ids2->pPresentIds) {
         assert(present_id == 0);
         present_id = present_ids2->pPresentIds[i];
      }
      if (present_id > 0) {
         image_signal_infos[i].present_id = present_id;
         const VkSemaphoreSubmitInfo sem_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = swapchain->present_id_timeline,
            .value = present_id,
            .stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
         };
         wsi_image_signal_info_add_semaphore(&image_signal_infos[i], sem_info);
      }

      /* The present fence guards all client-allocated resources and GPU
       * execution that may be in use by the swapchain.  Since everything tied
       * to the swapchain itself is managed by us, this really just means the
       * execution of blits on the GPU and the client-provided wait semaphores.
       * Therefore, it's valid to signal the present fence at the end of the
       * per-image GPU work.
       */
      if (present_fence_info && present_fence_info->pFences &&
          present_fence_info->pFences[i] != VK_NULL_HANDLE) {
         wsi_image_signal_info_add_fence(&image_signal_infos[i],
                                         present_fence_info->pFences[i]);
      }
   }

   /* Wait on the semaphores from the client, do any blits on this queue, and
    * signal the per-image semaphores/fences.  If a swapchain uses a separate
    * blit queue, we just signal the blit semaphores here and wait to signal
    * the per-image semaphores and fences with the blit.
    */
   {
      STACK_ARRAY(VkCommandBufferSubmitInfo, blit_command_buffer_infos,
                  pPresentInfo->swapchainCount);
      STACK_ARRAY(VkSemaphoreSubmitInfo, signal_semaphore_infos,
                  pPresentInfo->swapchainCount *
                  ARRAY_SIZE(image_signal_infos[0].semaphore_infos));
      STACK_ARRAY(VkFence, fences,
                  pPresentInfo->swapchainCount *
                  ARRAY_SIZE(image_signal_infos[0].fences));
      uint32_t blit_count = 0, signal_semaphore_count = 0, fence_count = 0;

      for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
         VK_FROM_HANDLE(wsi_swapchain, swapchain, pPresentInfo->pSwapchains[i]);
         uint32_t image_index = pPresentInfo->pImageIndices[i];
         struct wsi_image *image =
            swapchain->get_wsi_image(swapchain, image_index);

         if (results[i] != VK_SUCCESS)
            continue;

         /* If we're blitting on another swapchain, just signal the blit
          * semaphore for now.
          */
         if (swapchain->blit.type != WSI_SWAPCHAIN_NO_BLIT &&
             swapchain->blit.queue != NULL) {
            /* Create the blit semaphore if needed */
            if (swapchain->blit.semaphores[image_index] == VK_NULL_HANDLE) {
               const VkSemaphoreCreateInfo sem_info = {
                  .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                  .pNext = NULL,
                  .flags = 0,
               };
               results[i] = wsi->CreateSemaphore(vk_device_to_handle(dev),
                                                 &sem_info,
                                                 &swapchain->alloc,
                                                 &swapchain->blit.semaphores[image_index]);
               if (results[i] != VK_SUCCESS)
                  continue;
            }

            signal_semaphore_infos[signal_semaphore_count++] = (VkSemaphoreSubmitInfo) {
               .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
               .stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
               .semaphore = swapchain->blit.semaphores[image_index],
            };
            continue;
         }

         if (swapchain->blit.type != WSI_SWAPCHAIN_NO_BLIT) {
            blit_command_buffer_infos[blit_count++] = (VkCommandBufferSubmitInfo) {
               .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
               .commandBuffer =
                  image->blit.cmd_buffers[queue->queue_family_index],
            };
         }

         for (uint32_t j = 0; j < image_signal_infos[i].semaphore_count; j++) {
            signal_semaphore_infos[signal_semaphore_count++] =
               image_signal_infos[i].semaphore_infos[j];
         }
         for (uint32_t j = 0; j < image_signal_infos[i].fence_count; j++)
            fences[fence_count++] = image_signal_infos[i].fences[j];
      }

      const VkSubmitInfo2 submit_info = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
         .waitSemaphoreInfoCount = pPresentInfo->waitSemaphoreCount,
         .pWaitSemaphoreInfos = semaphore_wait_infos,
         .commandBufferInfoCount = blit_count,
         .pCommandBufferInfos = blit_command_buffer_infos,
         .signalSemaphoreInfoCount = signal_semaphore_count,
         .pSignalSemaphoreInfos = signal_semaphore_infos,
      };
      VkResult result = wsi_queue_submit2_unordered(wsi, queue, &submit_info,
                                                    fence_count, fences);
      if (result != VK_SUCCESS) {
         /* If this failed, everything failed */
         for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
            if (results[i] == VK_SUCCESS)
               results[i] = result;
         }
      }

      STACK_ARRAY_FINISH(fences);
      STACK_ARRAY_FINISH(signal_semaphore_infos);
      STACK_ARRAY_FINISH(blit_command_buffer_infos);
   }

   /* Now do blits on any blit queues */
   for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
      VK_FROM_HANDLE(wsi_swapchain, swapchain, pPresentInfo->pSwapchains[i]);
      uint32_t image_index = pPresentInfo->pImageIndices[i];
      struct wsi_image *image =
         swapchain->get_wsi_image(swapchain, image_index);

      if (results[i] != VK_SUCCESS)
         continue;

      if (swapchain->blit.type == WSI_SWAPCHAIN_NO_BLIT ||
          swapchain->blit.queue == NULL)
         continue;

      const VkSemaphoreSubmitInfo blit_semaphore_info = {
         .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
         .stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
         .semaphore = swapchain->blit.semaphores[image_index],
      };

      const VkCommandBufferSubmitInfo blit_command_buffer_info = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
         .commandBuffer = image->blit.cmd_buffers[0],
      };

      const VkSubmitInfo2 submit_info = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
         .waitSemaphoreInfoCount = 1,
         .pWaitSemaphoreInfos = &blit_semaphore_info,
         .commandBufferInfoCount = 1,
         .pCommandBufferInfos = &blit_command_buffer_info,
         .signalSemaphoreInfoCount = image_signal_infos[i].semaphore_count,
         .pSignalSemaphoreInfos = image_signal_infos[i].semaphore_infos,
      };
      results[i] = wsi_queue_submit2_unordered(wsi, swapchain->blit.queue,
                                               &submit_info,
                                               image_signal_infos[i].fence_count,
                                               image_signal_infos[i].fences);
   }

   /* Finally, we can present */
   const VkPresentRegionsKHR *regions =
      vk_find_struct_const(pPresentInfo->pNext, PRESENT_REGIONS_KHR);
   const VkSwapchainPresentModeInfoKHR *present_mode_info =
      vk_find_struct_const(pPresentInfo->pNext, SWAPCHAIN_PRESENT_MODE_INFO_KHR);

   for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
      VK_FROM_HANDLE(wsi_swapchain, swapchain, pPresentInfo->pSwapchains[i]);
      uint32_t image_index = pPresentInfo->pImageIndices[i];
      struct wsi_image *image =
         swapchain->get_wsi_image(swapchain, image_index);

      if (results[i] != VK_SUCCESS)
         continue;

      /* Update the present mode for this present and any subsequent present.
       * Only update the present mode when MESA_VK_WSI_PRESENT_MODE is not used.
       * We should also turn any VkSwapchainPresentModesCreateInfoEXT into a nop,
       * but none of the WSI backends use that currently. */
      if (present_mode_info && present_mode_info->pPresentModes &&
          swapchain->set_present_mode && wsi->override_present_mode == VK_PRESENT_MODE_MAX_ENUM_KHR) {
         swapchain->set_present_mode(swapchain, present_mode_info->pPresentModes[i]);
      }

      /* The app can only submit images they have acquired. */
      assert(image->acquired);
      image->acquired = false;
      image->present_serial = ++swapchain->present_serial;

      /* If we updated the signal_dma_buf semaphore, extract its syncobj and
       * attach it to the dma-buf before we present so that the present
       * implicitly syncs on it.
       */
      if (swapchain->dma_buf_semaphore != VK_NULL_HANDLE) {
#ifdef HAVE_LIBDRM
         assert(!swapchain->image_info.explicit_sync);
         results[i] = wsi_signal_dma_buf_from_semaphore(swapchain, image);
         if (results[i] != VK_SUCCESS)
            continue;
#else
         UNREACHABLE("We shouldn't have a dma-buf semaphore without libdrm");
#endif
      }

      if (wsi->sw) {
         wsi->WaitForFences(vk_device_to_handle(dev),
                            1, &swapchain->fences[image_index], true, ~0ull);
      }

      const VkPresentRegionKHR *region = NULL;
      if (regions && regions->pRegions)
         region = &regions->pRegions[i];

      results[i] = swapchain->queue_present(swapchain, image_index,
                                            image_signal_infos[i].present_id,
                                            region);
      if (results[i] != VK_SUCCESS && results[i] != VK_SUBOPTIMAL_KHR)
         continue;

      if (wsi->set_memory_ownership) {
         VkDeviceMemory mem = swapchain->get_wsi_image(swapchain, image_index)->memory;
         wsi->set_memory_ownership(swapchain->device, mem, false);
      }
   }

   for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
      if (pPresentInfo->pResults != NULL)
         pPresentInfo->pResults[i] = results[i];

      /* Let the final result be our first unsuccessful result */
      if (final_result == VK_SUCCESS)
         final_result = results[i];
   }

   STACK_ARRAY_FINISH(image_signal_infos);
   STACK_ARRAY_FINISH(semaphore_wait_infos);
   STACK_ARRAY_FINISH(results);

   return final_result;
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_QueuePresentKHR(VkQueue _queue, const VkPresentInfoKHR *pPresentInfo)
{
   MESA_TRACE_FUNC();
   VK_FROM_HANDLE(vk_queue, queue, _queue);

   return wsi_common_queue_present(queue->base.device->physical->wsi_device,
                                   queue, pPresentInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetDeviceGroupPresentCapabilitiesKHR(VkDevice device,
                                         VkDeviceGroupPresentCapabilitiesKHR *pCapabilities)
{
   memset(pCapabilities->presentMask, 0,
          sizeof(pCapabilities->presentMask));
   pCapabilities->presentMask[0] = 0x1;
   pCapabilities->modes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_GetDeviceGroupSurfacePresentModesKHR(VkDevice device,
                                         VkSurfaceKHR surface,
                                         VkDeviceGroupPresentModeFlagsKHR *pModes)
{
   *pModes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;

   return VK_SUCCESS;
}

VkResult
wsi_common_create_swapchain_image(const struct wsi_device *wsi,
                                  const VkImageCreateInfo *pCreateInfo,
                                  VkSwapchainKHR _swapchain,
                                  VkImage *pImage)
{
   VK_FROM_HANDLE(wsi_swapchain, chain, _swapchain);

#ifndef NDEBUG
   const VkImageCreateInfo *swcInfo = &chain->image_info.create;
   assert(pCreateInfo->flags == 0);
   assert(pCreateInfo->imageType == swcInfo->imageType);
   assert(pCreateInfo->format == swcInfo->format);
   assert(pCreateInfo->extent.width == swcInfo->extent.width);
   assert(pCreateInfo->extent.height == swcInfo->extent.height);
   assert(pCreateInfo->extent.depth == swcInfo->extent.depth);
   assert(pCreateInfo->mipLevels == swcInfo->mipLevels);
   assert(pCreateInfo->arrayLayers == swcInfo->arrayLayers);
   assert(pCreateInfo->samples == swcInfo->samples);
   assert(pCreateInfo->tiling == VK_IMAGE_TILING_OPTIMAL);
   assert(!(pCreateInfo->usage & ~swcInfo->usage));

   vk_foreach_struct_const(ext, pCreateInfo->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO: {
         const VkImageFormatListCreateInfo *iflci =
            (const VkImageFormatListCreateInfo *)ext;
         const VkImageFormatListCreateInfo *swc_iflci =
            &chain->image_info.format_list;

         for (uint32_t i = 0; i < iflci->viewFormatCount; i++) {
            bool found = false;
            for (uint32_t j = 0; j < swc_iflci->viewFormatCount; j++) {
               if (iflci->pViewFormats[i] == swc_iflci->pViewFormats[j]) {
                  found = true;
                  break;
               }
            }
            assert(found);
         }
         break;
      }

      case VK_STRUCTURE_TYPE_IMAGE_SWAPCHAIN_CREATE_INFO_KHR:
         break;

      default:
         assert(!"Unsupported image create extension");
      }
   }
#endif

   return wsi->CreateImage(chain->device, &chain->image_info.create,
                           &chain->alloc, pImage);
}

VkResult
wsi_swapchain_wait_for_present_semaphore(const struct wsi_swapchain *chain,
                                         uint64_t present_id, uint64_t timeout)
{
   assert(chain->present_id_timeline);
   const VkSemaphoreWaitInfo wait_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .semaphoreCount = 1,
      .pSemaphores = &chain->present_id_timeline,
      .pValues = &present_id,
   };

   return chain->wsi->WaitSemaphores(chain->device, &wait_info, timeout);
}

uint32_t
wsi_select_memory_type(const struct wsi_device *wsi,
                       VkMemoryPropertyFlags req_props,
                       VkMemoryPropertyFlags deny_props,
                       uint32_t type_bits)
{
   assert(type_bits != 0);

   VkMemoryPropertyFlags common_props = ~0;
   u_foreach_bit(t, type_bits) {
      const VkMemoryType type = wsi->memory_props.memoryTypes[t];

      common_props &= type.propertyFlags;

      if (deny_props & type.propertyFlags)
         continue;

      if (!(req_props & ~type.propertyFlags))
         return t;
   }

   if ((deny_props & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
       (common_props & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
      /* If they asked for non-device-local and all the types are device-local
       * (this is commonly true for UMA platforms), try again without denying
       * device-local types
       */
      deny_props &= ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      return wsi_select_memory_type(wsi, req_props, deny_props, type_bits);
   }

   if (req_props & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
      req_props &= ~VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
      // fallback to coherent if cached-coherent is requested but not found
      return wsi_select_memory_type(wsi, req_props, deny_props, type_bits);
   }

   UNREACHABLE("No memory type found");
}

uint32_t
wsi_select_device_memory_type(const struct wsi_device *wsi,
                              uint32_t type_bits)
{
   return wsi_select_memory_type(wsi, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                 0 /* deny_props */, type_bits);
}

static uint32_t
wsi_select_host_memory_type(const struct wsi_device *wsi,
                            uint32_t type_bits)
{
   VkMemoryPropertyFlags req_props = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

   if (wsi->sw)
      req_props |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

   return wsi_select_memory_type(wsi, req_props, 0 /* deny_props */, type_bits);
}

VkResult
wsi_create_buffer_blit_context(const struct wsi_swapchain *chain,
                               const struct wsi_image_info *info,
                               struct wsi_image *image,
                               VkExternalMemoryHandleTypeFlags handle_types)
{
   assert(chain->blit.type == WSI_SWAPCHAIN_BUFFER_BLIT);

   const struct wsi_device *wsi = chain->wsi;
   VkResult result;

   const VkBufferUsageFlags create_flags =
      (chain->create_flags & VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR) ?
      VK_BUFFER_CREATE_PROTECTED_BIT : 0;
   const VkExternalMemoryBufferCreateInfo buffer_external_info = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
      .pNext = NULL,
      .handleTypes = handle_types,
   };
   const VkBufferCreateInfo buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext = &buffer_external_info,
      .flags = create_flags,
      .size = info->linear_size,
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };
   result = wsi->CreateBuffer(chain->device, &buffer_info,
                              &chain->alloc, &image->blit.buffer);
   if (result != VK_SUCCESS)
      return result;

   VkMemoryRequirements reqs;
   wsi->GetBufferMemoryRequirements(chain->device, image->blit.buffer, &reqs);
   assert(reqs.size <= info->linear_size);

   struct wsi_memory_allocate_info memory_wsi_info = {
      .sType = VK_STRUCTURE_TYPE_WSI_MEMORY_ALLOCATE_INFO_MESA,
      .pNext = NULL,
      .implicit_sync = info->image_type == WSI_IMAGE_TYPE_DRM &&
                       !info->explicit_sync && !chain->dma_buf_semaphore,
      .dma_buf_sync_file = chain->dma_buf_semaphore,
   };
   VkMemoryDedicatedAllocateInfo buf_mem_dedicated_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = &memory_wsi_info,
      .image = VK_NULL_HANDLE,
      .buffer = image->blit.buffer,
   };
   VkMemoryAllocateInfo buf_mem_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &buf_mem_dedicated_info,
      .allocationSize = info->linear_size,
      .memoryTypeIndex =
         info->select_blit_dst_memory_type(wsi, reqs.memoryTypeBits),
   };

   void *sw_host_ptr = NULL;
   if (info->alloc_shm)
      sw_host_ptr = info->alloc_shm(image, info->linear_size);

   VkExportMemoryAllocateInfo memory_export_info;
   VkImportMemoryHostPointerInfoEXT host_ptr_info;
   if (sw_host_ptr != NULL) {
      image->blit.to_foreign_queue = true;
      host_ptr_info = (VkImportMemoryHostPointerInfoEXT) {
         .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
         .pHostPointer = sw_host_ptr,
         .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
      };
      __vk_append_struct(&buf_mem_info, &host_ptr_info);
   } else if (handle_types != 0) {
      image->blit.to_foreign_queue = true;
      memory_export_info = (VkExportMemoryAllocateInfo) {
         .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
         .handleTypes = handle_types,
      };
      __vk_append_struct(&buf_mem_info, &memory_export_info);
   }

   result = wsi->AllocateMemory(chain->device, &buf_mem_info,
                                &chain->alloc, &image->blit.memory);
   if (result != VK_SUCCESS)
      return result;

   result = wsi->BindBufferMemory(chain->device, image->blit.buffer,
                                  image->blit.memory, 0);
   if (result != VK_SUCCESS)
      return result;

   wsi->GetImageMemoryRequirements(chain->device, image->image, &reqs);

   const VkMemoryDedicatedAllocateInfo memory_dedicated_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = NULL,
      .image = image->image,
      .buffer = VK_NULL_HANDLE,
   };
   const VkMemoryAllocateInfo memory_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &memory_dedicated_info,
      .allocationSize = reqs.size,
      .memoryTypeIndex =
         info->select_image_memory_type(wsi, reqs.memoryTypeBits),
   };

   result = wsi->AllocateMemory(chain->device, &memory_info,
                                &chain->alloc, &image->memory);
   if (result != VK_SUCCESS)
      return result;

   image->num_planes = 1;
   image->sizes[0] = info->linear_size;
   image->row_pitches[0] = info->linear_stride;
   image->offsets[0] = 0;

   return VK_SUCCESS;
}

static void
wsi_label_cmd_buffer(const struct wsi_device *wsi, VkDevice device, VkCommandBuffer cmd_buffer, const char *name)
{
   VkDebugUtilsObjectNameInfoEXT name_info = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
      .pNext = NULL,
      .objectType = VK_OBJECT_TYPE_COMMAND_BUFFER,
      .objectHandle = (uintptr_t)cmd_buffer,
      .pObjectName = name,
   };

   wsi->SetDebugUtilsObjectNameEXT(device, &name_info);
}

static void
wsi_cmd_blit_image_to_buffer(VkCommandBuffer cmd_buffer,
                             const struct wsi_device *wsi,
                             const struct wsi_image_info *info,
                             struct wsi_image *image,
                             uint32_t qfi)
{
   assert(info->image_type == WSI_IMAGE_TYPE_CPU ||
          info->image_type == WSI_IMAGE_TYPE_DRM);

   VkImageMemoryBarrier img_mem_barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .pNext = NULL,
      .srcAccessMask = 0,
      .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image->image,
      .subresourceRange = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .baseMipLevel = 0,
         .levelCount = 1,
         .baseArrayLayer = 0,
         .layerCount = 1,
      },
   };
   wsi->CmdPipelineBarrier(cmd_buffer,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0,
                           0, NULL,
                           0, NULL,
                           1, &img_mem_barrier);

   struct VkBufferImageCopy buffer_image_copy = {
      .bufferOffset = 0,
      .bufferRowLength = info->linear_stride /
                         vk_format_get_blocksize(info->create.format),
      .bufferImageHeight = 0,
      .imageSubresource = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .mipLevel = 0,
         .baseArrayLayer = 0,
         .layerCount = 1,
      },
      .imageOffset = { .x = 0, .y = 0, .z = 0 },
      .imageExtent = info->create.extent,
   };
   wsi->CmdCopyImageToBuffer(cmd_buffer,
                             image->image,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             image->blit.buffer,
                             1, &buffer_image_copy);

   img_mem_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
   img_mem_barrier.dstAccessMask = 0;
   img_mem_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
   img_mem_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

   const VkBufferMemoryBarrier buf_mem_barrier = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .pNext = NULL,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      .srcQueueFamilyIndex =
         image->blit.to_foreign_queue ? qfi : VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = image->blit.to_foreign_queue
                                ? VK_QUEUE_FAMILY_FOREIGN_EXT
                                : VK_QUEUE_FAMILY_IGNORED,
      .buffer = image->blit.buffer,
      .offset = 0,
      .size = VK_WHOLE_SIZE,
   };
   wsi->CmdPipelineBarrier(cmd_buffer,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_HOST_BIT,
                           0,
                           0, NULL,
                           1, &buf_mem_barrier,
                           1, &img_mem_barrier);
}

static void
wsi_cmd_blit_image_to_image(VkCommandBuffer cmd_buffer,
                            const struct wsi_device *wsi,
                            const struct wsi_image_info *info,
                            struct wsi_image *image)
{
   assert(info->image_type == WSI_IMAGE_TYPE_DXGI);

   VkImageMemoryBarrier img_mem_barriers[2] = {
      {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .pNext = NULL,
         .srcAccessMask = 0,
         .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
         .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image = image->image,
         .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
      },
      {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .pNext = NULL,
         .srcAccessMask = 0,
         .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
         .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image = image->blit.image,
         .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
      },
   };
   wsi->CmdPipelineBarrier(cmd_buffer,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0,
                           0, NULL,
                           0, NULL,
                           2, img_mem_barriers);

   const struct VkImageCopy image_copy = {
      .srcSubresource = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .mipLevel = 0,
         .baseArrayLayer = 0,
         .layerCount = 1,
      },
      .srcOffset = { .x = 0, .y = 0, .z = 0 },
      .dstSubresource = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .mipLevel = 0,
         .baseArrayLayer = 0,
         .layerCount = 1,
      },
      .dstOffset = { .x = 0, .y = 0, .z = 0 },
      .extent = info->create.extent,
   };
   wsi->CmdCopyImage(cmd_buffer,
                     image->image,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     image->blit.image,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     1, &image_copy);

   img_mem_barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
   img_mem_barriers[0].dstAccessMask = 0;
   img_mem_barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
   img_mem_barriers[0].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
   img_mem_barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   img_mem_barriers[1].dstAccessMask = 0;
   img_mem_barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
   img_mem_barriers[1].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
   wsi->CmdPipelineBarrier(cmd_buffer,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                           0,
                           0, NULL,
                           0, NULL,
                           2, img_mem_barriers);
}

VkResult
wsi_finish_create_blit_context(const struct wsi_swapchain *chain,
                               const struct wsi_image_info *info,
                               struct wsi_image *image)
{
   const struct wsi_device *wsi = chain->wsi;
   VkResult result;

   int cmd_buffer_count =
      chain->blit.queue != NULL ? 1 : wsi->queue_family_count;
   image->blit.cmd_buffers =
      vk_zalloc(&chain->alloc,
                sizeof(VkCommandBuffer) * cmd_buffer_count, 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!image->blit.cmd_buffers)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   for (uint32_t i = 0; i < cmd_buffer_count; i++) {
      if (!chain->cmd_pools[i])
         continue;

      const VkCommandBufferAllocateInfo cmd_buffer_info = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
         .pNext = NULL,
         .commandPool = chain->cmd_pools[i],
         .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
         .commandBufferCount = 1,
      };
      VkCommandBuffer cmd_buffer;
      result = wsi->AllocateCommandBuffers(chain->device, &cmd_buffer_info,
                                           &cmd_buffer);
      if (result != VK_SUCCESS)
         return result;

      image->blit.cmd_buffers[i] = cmd_buffer;

      wsi_label_cmd_buffer(wsi, chain->device, cmd_buffer, "wsi blit");

      const VkCommandBufferBeginInfo begin_info = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };
      wsi->BeginCommandBuffer(cmd_buffer, &begin_info);

      switch (chain->blit.type) {
      case WSI_SWAPCHAIN_BUFFER_BLIT: {
         wsi_cmd_blit_image_to_buffer(
            cmd_buffer, wsi, info, image,
            chain->blit.queue ? chain->blit.queue->queue_family_index : i);
         break;
      }
      case WSI_SWAPCHAIN_IMAGE_BLIT:
         wsi_cmd_blit_image_to_image(cmd_buffer, wsi, info, image);
         break;
      default:
         UNREACHABLE("Invalid wsi_swapchain_blit_type");
      }

      result = wsi->EndCommandBuffer(cmd_buffer);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

void
wsi_configure_buffer_image(UNUSED const struct wsi_swapchain *chain,
                           const VkSwapchainCreateInfoKHR *pCreateInfo,
                           uint32_t stride_align, uint32_t size_align,
                           struct wsi_image_info *info)
{
   const struct wsi_device *wsi = chain->wsi;

   assert(util_is_power_of_two_nonzero(stride_align));
   assert(util_is_power_of_two_nonzero(size_align));

   info->create.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
   info->wsi.blit_src = true;

   const uint32_t cpp = vk_format_get_blocksize(pCreateInfo->imageFormat);
   info->linear_stride = pCreateInfo->imageExtent.width * cpp;
   info->linear_stride = align(info->linear_stride, stride_align);

   /* Since we can pick the stride to be whatever we want, also align to the
    * device's optimalBufferCopyRowPitchAlignment so we get efficient copies.
    */
   assert(wsi->optimalBufferCopyRowPitchAlignment > 0);
   info->linear_stride = align(info->linear_stride,
                               wsi->optimalBufferCopyRowPitchAlignment);

   info->linear_size = (uint64_t)info->linear_stride *
                       pCreateInfo->imageExtent.height;
   info->linear_size = align64(info->linear_size, size_align);

   info->finish_create = wsi_finish_create_blit_context;
}

void
wsi_configure_image_blit_image(UNUSED const struct wsi_swapchain *chain,
                               struct wsi_image_info *info)
{
   info->create.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
   info->wsi.blit_src = true;
   info->finish_create = wsi_finish_create_blit_context;
}

static VkResult
wsi_create_cpu_linear_image_mem(const struct wsi_swapchain *chain,
                                const struct wsi_image_info *info,
                                struct wsi_image *image)
{
   const struct wsi_device *wsi = chain->wsi;
   VkResult result;

   VkMemoryRequirements reqs;
   wsi->GetImageMemoryRequirements(chain->device, image->image, &reqs);

   VkSubresourceLayout layout;
   wsi->GetImageSubresourceLayout(chain->device, image->image,
                                  &(VkImageSubresource) {
                                     .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                     .mipLevel = 0,
                                     .arrayLayer = 0,
                                  }, &layout);
   assert(layout.offset == 0);

   const VkMemoryDedicatedAllocateInfo memory_dedicated_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .image = image->image,
      .buffer = VK_NULL_HANDLE,
   };
   VkMemoryAllocateInfo memory_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &memory_dedicated_info,
      .allocationSize = reqs.size,
      .memoryTypeIndex =
         wsi_select_host_memory_type(wsi, reqs.memoryTypeBits),
   };

   void *sw_host_ptr = NULL;
   if (info->alloc_shm)
      sw_host_ptr = info->alloc_shm(image, layout.size);

   VkImportMemoryHostPointerInfoEXT host_ptr_info;
   if (sw_host_ptr != NULL) {
      host_ptr_info = (VkImportMemoryHostPointerInfoEXT) {
         .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
         .pHostPointer = sw_host_ptr,
         .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
      };
      __vk_append_struct(&memory_info, &host_ptr_info);
   }

   result = wsi->AllocateMemory(chain->device, &memory_info,
                                &chain->alloc, &image->memory);
   if (result != VK_SUCCESS)
      return result;

   result = wsi->MapMemory(chain->device, image->memory,
                           0, VK_WHOLE_SIZE, 0, &image->cpu_map);
   if (result != VK_SUCCESS)
      return result;

   image->num_planes = 1;
   image->sizes[0] = reqs.size;
   image->row_pitches[0] = layout.rowPitch;
   image->offsets[0] = 0;

   return VK_SUCCESS;
}

static VkResult
wsi_create_cpu_buffer_image_mem(const struct wsi_swapchain *chain,
                                const struct wsi_image_info *info,
                                struct wsi_image *image)
{
   VkResult result;

   result = wsi_create_buffer_blit_context(chain, info, image, 0);
   if (result != VK_SUCCESS)
      return result;

   result = chain->wsi->MapMemory(chain->device, image->blit.memory,
                                  0, VK_WHOLE_SIZE, 0, &image->cpu_map);
   if (result != VK_SUCCESS)
      return result;

   return VK_SUCCESS;
}

bool
wsi_cpu_image_needs_buffer_blit(const struct wsi_device *wsi,
                                const struct wsi_cpu_image_params *params)
{
   if (WSI_DEBUG & WSI_DEBUG_BUFFER)
      return true;

   if (wsi->wants_linear)
      return false;

   return true;
}

VkResult
wsi_configure_cpu_image(const struct wsi_swapchain *chain,
                        const VkSwapchainCreateInfoKHR *pCreateInfo,
                        const struct wsi_cpu_image_params *params,
                        struct wsi_image_info *info)
{
   assert(params->base.image_type == WSI_IMAGE_TYPE_CPU);
   assert(chain->blit.type == WSI_SWAPCHAIN_NO_BLIT ||
          chain->blit.type == WSI_SWAPCHAIN_BUFFER_BLIT);

   VkExternalMemoryHandleTypeFlags handle_types = 0;
   if (params->alloc_shm && chain->blit.type != WSI_SWAPCHAIN_NO_BLIT)
      handle_types = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;

   VkResult result = wsi_configure_image(chain, pCreateInfo,
                                         handle_types, info);
   if (result != VK_SUCCESS)
      return result;

   if (chain->blit.type != WSI_SWAPCHAIN_NO_BLIT) {
      wsi_configure_buffer_image(chain, pCreateInfo,
                                 1 /* stride_align */,
                                 1 /* size_align */,
                                 info);

      info->select_blit_dst_memory_type = wsi_select_host_memory_type;
      info->select_image_memory_type = wsi_select_device_memory_type;
      info->create_mem = wsi_create_cpu_buffer_image_mem;
   } else {
      /* Force the image to be linear */
      info->create.tiling = VK_IMAGE_TILING_LINEAR;

      info->create_mem = wsi_create_cpu_linear_image_mem;
   }

   info->alloc_shm = params->alloc_shm;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_WaitForPresentKHR(VkDevice device, VkSwapchainKHR _swapchain,
                      uint64_t presentId, uint64_t timeout)
{
   VK_FROM_HANDLE(wsi_swapchain, swapchain, _swapchain);
   assert(swapchain->wait_for_present);
   return swapchain->wait_for_present(swapchain, presentId, timeout);
}

VKAPI_ATTR VkResult VKAPI_CALL
wsi_WaitForPresent2KHR(VkDevice device, VkSwapchainKHR _swapchain,
                       const VkPresentWait2InfoKHR *info)
{
   VK_FROM_HANDLE(wsi_swapchain, swapchain, _swapchain);
   assert(swapchain->wait_for_present2);
   return swapchain->wait_for_present2(swapchain, info->presentId, info->timeout);
}

VkImageUsageFlags
wsi_caps_get_image_usage(void)
{
   return VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
          VK_IMAGE_USAGE_SAMPLED_BIT |
          VK_IMAGE_USAGE_TRANSFER_DST_BIT |
          VK_IMAGE_USAGE_STORAGE_BIT |
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
          VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
}

bool
wsi_device_supports_explicit_sync(struct wsi_device *device)
{
   return !device->sw && device->has_timeline_semaphore &&
      (device->timeline_semaphore_export_handle_types &
       VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT);
}

VKAPI_ATTR void VKAPI_CALL
wsi_SetHdrMetadataEXT(VkDevice device, uint32_t swapchainCount,
                      const VkSwapchainKHR* pSwapchains,
                      const VkHdrMetadataEXT* pMetadata)
{
   for (uint32_t i = 0; i < swapchainCount; i++) {
      VK_FROM_HANDLE(wsi_swapchain, swapchain, pSwapchains[i]);
      if (swapchain->set_hdr_metadata)
         swapchain->set_hdr_metadata(swapchain, pMetadata);
   }
}
