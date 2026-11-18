/*
 * Copyright © 2026 NXP
 * Copyright © 2022 Intel Corporation
 * Copyright © 2026 Google LLC
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

#include "vk_android.h"

#include "vk_alloc.h"
#include "vk_common_entrypoints.h"
#include "vk_device.h"
#include "vk_enum_defines.h"
#include "vk_format.h"
#include "vk_image.h"
#include "vk_log.h"
#include "vk_physical_device.h"
#include "vk_queue.h"
#include "vk_util.h"

#include "c11/threads.h"
#include "drm-uapi/drm_fourcc.h"
#include "util/libsync.h"
#include "util/log.h"
#include "util/os_file.h"
#include "util/u_gralloc/u_gralloc.h"

#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <hardware/hwvulkan.h>

#if ANDROID_API_LEVEL >= 26
#include <hardware/gralloc1.h>
#include <vndk/hardware_buffer.h>
#endif

#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/dma-buf.h>

static struct u_gralloc *_gralloc;

static void
vk_android_init_ugralloc_once(void)
{
   _gralloc = u_gralloc_create(U_GRALLOC_TYPE_AUTO);
}

struct u_gralloc *
vk_android_get_ugralloc(void)
{
   static once_flag once = ONCE_FLAG_INIT;
   call_once(&once, vk_android_init_ugralloc_once);
   return _gralloc;
}

static int vk_android_hal_open(const struct hw_module_t *mod, const char *id,
                               struct hw_device_t **dev);

static_assert(HWVULKAN_DISPATCH_MAGIC == ICD_LOADER_MAGIC, "");

PUBLIC struct hwvulkan_module_t HAL_MODULE_INFO_SYM = {
   .common =
      {
         .tag = HARDWARE_MODULE_TAG,
         .module_api_version = HWVULKAN_MODULE_API_VERSION_0_1,
         .hal_api_version = HARDWARE_MAKE_API_VERSION(1, 0),
         .id = HWVULKAN_HARDWARE_MODULE_ID,
         .name = "Mesa 3D Vulkan HAL",
         .author = "Mesa 3D",
         .methods =
            &(hw_module_methods_t){
               .open = vk_android_hal_open,
            },
      },
};

static int
vk_android_hal_close(struct hw_device_t *dev)
{
   /* the hw_device_t::close() function is called upon driver unloading */
   assert(dev->version == HWVULKAN_DEVICE_API_VERSION_0_1);
   assert(dev->module == &HAL_MODULE_INFO_SYM.common);

   hwvulkan_device_t *hal_dev = container_of(dev, hwvulkan_device_t, common);
   free(hal_dev);
   return 0;
}

static int
vk_android_hal_open(const struct hw_module_t *mod, const char *id,
                    struct hw_device_t **dev)
{
   assert(mod == &HAL_MODULE_INFO_SYM.common);
   assert(strcmp(id, HWVULKAN_DEVICE_0) == 0);

   hwvulkan_device_t *hal_dev = malloc(sizeof(*hal_dev));
   if (!hal_dev)
      return -1;

   *hal_dev = (hwvulkan_device_t){
      .common =
         {
            .tag = HARDWARE_DEVICE_TAG,
            .version = HWVULKAN_DEVICE_API_VERSION_0_1,
            .module = &HAL_MODULE_INFO_SYM.common,
            .close = vk_android_hal_close,
         },
      .EnumerateInstanceExtensionProperties =
         (PFN_vkEnumerateInstanceExtensionProperties)vk_icdGetInstanceProcAddr(
            NULL, "vkEnumerateInstanceExtensionProperties"),
      .CreateInstance =
         (PFN_vkCreateInstance)vk_icdGetInstanceProcAddr(
            NULL, "vkCreateInstance"),
      .GetInstanceProcAddr =
         (PFN_vkGetInstanceProcAddr)vk_icdGetInstanceProcAddr(
            NULL, "vkGetInstanceProcAddr"),
   };

   *dev = &hal_dev->common;
   return 0;
}

static VkResult
vk_gralloc_to_drm_explicit_layout(
   struct u_gralloc_buffer_handle *in_hnd,
   VkImageDrmFormatModifierExplicitCreateInfoEXT *out,
   VkSubresourceLayout *out_layouts, int max_planes)
{
   struct u_gralloc_buffer_basic_info info;
   struct u_gralloc *u_gralloc = vk_android_get_ugralloc();
   assert(u_gralloc);

   if (u_gralloc_get_buffer_basic_info(u_gralloc, in_hnd, &info) != 0) {
      mesa_loge("u_gralloc_get_buffer_basic_info failed");
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }

   if (info.num_planes > max_planes) {
      mesa_loge("info.num_planes(%u) > max_planes(%u) unsupported",
                info.num_planes, max_planes);
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }

   bool is_disjoint = false;
   for (size_t i = 1; i < info.num_planes; i++) {
      if (info.offsets[i] == 0) {
         is_disjoint = true;
         mesa_loge("disjoint planes (info.offsets[%zu] == 0) unsupported", i);
         break;
      }
   }

   if (is_disjoint) {
      /* We don't support disjoint planes yet */
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }

   memset(out, 0, sizeof(*out));
   memset(out_layouts, 0, sizeof(*out_layouts) * max_planes);

   out->sType =
      VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
   out->pPlaneLayouts = out_layouts;

   out->drmFormatModifier = info.modifier;
   out->drmFormatModifierPlaneCount = info.num_planes;
   for (size_t i = 0; i < info.num_planes; i++) {
      out_layouts[i].offset = info.offsets[i];
      out_layouts[i].rowPitch = info.strides[i];
   }

   /* Compute arrayPitch for multi-layer buffers. The gralloc HAL does not
    * expose a per-layer stride directly, but we can derive it from the
    * total allocation size and layer count. Disjoint multi-plane buffers
    * are rejected above, so alloc_size / layer_count is valid here.
    */
   if (info.layer_count > 1 && info.alloc_size > 0) {
      uint64_t array_pitch = info.alloc_size / info.layer_count;
      for (size_t i = 0; i < info.num_planes; i++)
         out_layouts[i].arrayPitch = array_pitch;
   }

   if (info.drm_fourcc == DRM_FORMAT_YVU420) {
      /* Swap the U and V planes to match the
       * VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM */
      VkSubresourceLayout tmp = out_layouts[1];
      out_layouts[1] = out_layouts[2];
      out_layouts[2] = tmp;
   }

   return VK_SUCCESS;
}

VkResult
vk_android_import_anb_memory(struct vk_device *device,
                             struct vk_image *image,
                             const VkNativeBufferANDROID *anb,
                             const VkAllocationCallbacks *alloc)
{
   assert(anb && anb->handle && anb->handle->numFds > 0);

   int dma_buf_fd = anb->handle->data[0];

   /* Query image memory requirements for size and supported memory types */
   VkMemoryRequirements mem_reqs;
   device->dispatch_table.GetImageMemoryRequirements(
      (VkDevice)device, (VkImage)image, &mem_reqs);

   VkMemoryFdPropertiesKHR fd_props = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR,
   };
   VkResult result = device->dispatch_table.GetMemoryFdPropertiesKHR(
      (VkDevice)device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
      dma_buf_fd, &fd_props);
   if (result != VK_SUCCESS) {
      mesa_loge("GetMemoryFdPropertiesKHR failed");
      return result;
   }

   uint32_t compatible_types = mem_reqs.memoryTypeBits & fd_props.memoryTypeBits;
   if (!compatible_types) {
      mesa_loge("No compatible AHB mem types (img_reqs=%#x,fd_reqs=%#x)",
                 mem_reqs.memoryTypeBits, fd_props.memoryTypeBits);
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }

   int dup_fd = os_dupfd_cloexec(dma_buf_fd);
   if (dup_fd < 0) {
      mesa_loge("os_dupfd_cloexec(%d) failed: %s", dma_buf_fd, strerror(errno));
      return (errno == EMFILE) ? VK_ERROR_TOO_MANY_OBJECTS
                               : VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   const VkMemoryDedicatedAllocateInfo ded_alloc = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = NULL,
      .buffer = VK_NULL_HANDLE,
      .image = (VkImage)image};

   const VkImportMemoryFdInfoKHR import_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
      .pNext = &ded_alloc,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
      .fd = dup_fd,
   };
   const VkMemoryAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &import_info,
      .allocationSize = mem_reqs.size,
      .memoryTypeIndex = ffs(compatible_types) - 1,
   };
   result = device->dispatch_table.AllocateMemory(
      (VkDevice)device, &alloc_info, alloc, &image->anb_memory);
   if (result != VK_SUCCESS) {
      close(dup_fd);
      return result;
   }

   return VK_SUCCESS;
}

VkResult
vk_android_import_anb(struct vk_device *device,
                      const VkImageCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *alloc,
                      struct vk_image *image)
{
   const VkNativeBufferANDROID *native_buffer =
      vk_find_struct_const(pCreateInfo->pNext, NATIVE_BUFFER_ANDROID);

   assert(native_buffer);

   VkResult result = vk_android_import_anb_memory(device, image,
                                                  native_buffer, alloc);
   if (result != VK_SUCCESS)
      return result;

   VkBindImageMemoryInfo bind_info = {
      .sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
      .image = (VkImage)image,
      .memory = image->anb_memory,
      .memoryOffset = 0,
   };

   return device->dispatch_table.BindImageMemory2((VkDevice)device, 1, &bind_info);
}

VkResult
vk_android_get_anb_layout(
   const VkImageCreateInfo *pCreateInfo,
   VkImageDrmFormatModifierExplicitCreateInfoEXT *out,
   VkSubresourceLayout *out_layouts, int max_planes)
{
   const VkNativeBufferANDROID *native_buffer =
      vk_find_struct_const(pCreateInfo->pNext, NATIVE_BUFFER_ANDROID);

   struct u_gralloc_buffer_handle gr_handle = {
      .handle = native_buffer->handle,
      .hal_format = native_buffer->format,
      .pixel_stride = native_buffer->stride,
   };

   return vk_gralloc_to_drm_explicit_layout(&gr_handle, out,
                                            out_layouts, max_planes);
}

VkResult
vk_android_init_deferred_image(struct vk_device *device,
                               struct vk_image *image,
                               const VkImageCreateInfo *pCreateInfo,
                               const VkAllocationCallbacks *pAllocator)
{
   /* collect all dynamic array infos */
   uint32_t queue_family_count = 0;
   uint32_t view_format_count = 0;
   uint32_t fixed_rate_count = 0;

   if (pCreateInfo->sharingMode == VK_SHARING_MODE_CONCURRENT)
      queue_family_count = pCreateInfo->queueFamilyIndexCount;

   const VkImageFormatListCreateInfo *raw_list =
      vk_find_struct_const(pCreateInfo->pNext, IMAGE_FORMAT_LIST_CREATE_INFO);
   if (raw_list)
      view_format_count = raw_list->viewFormatCount;

   const VkImageCompressionControlEXT *raw_compress =
      vk_find_struct_const(pCreateInfo->pNext, IMAGE_COMPRESSION_CONTROL_EXT);
   if (raw_compress &&
       (raw_compress->flags & VK_IMAGE_COMPRESSION_FIXED_RATE_EXPLICIT_EXT))
      fixed_rate_count = raw_compress->compressionControlPlaneCount;

   /* Extend below when drivers support more extensions that interact with ANB
    * or AHB.
    */
   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, VkImageCreateInfo, create_info, 1);
   VK_MULTIALLOC_DECL(&ma, VkImageFormatListCreateInfo, list_info, 1);
   VK_MULTIALLOC_DECL(&ma, VkImageStencilUsageCreateInfo, stencil_info, 1);
   VK_MULTIALLOC_DECL(&ma, VkImageCompressionControlEXT, compress_info, 1);
   VK_MULTIALLOC_DECL(&ma, uint32_t, queue_families, queue_family_count);
   VK_MULTIALLOC_DECL(&ma, VkFormat, view_formats, view_format_count);
   VK_MULTIALLOC_DECL(&ma, VkImageCompressionFixedRateFlagsEXT, fixed_rates,
                      fixed_rate_count);

   if (!vk_multialloc_zalloc2(&ma, &device->alloc, pAllocator,
                              VK_SYSTEM_ALLOCATION_SCOPE_OBJECT))
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   /* prepare the deferred VkImageCreateInfo chain */
   *create_info = *pCreateInfo;
   create_info->pNext = NULL;
   /* Assign resolved AHB external format */
   create_info->format = image->format;
   create_info->tiling = image->tiling =
      VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
   if (pCreateInfo->sharingMode == VK_SHARING_MODE_CONCURRENT) {
      typed_memcpy(queue_families, pCreateInfo->pQueueFamilyIndices,
                   pCreateInfo->queueFamilyIndexCount);
      create_info->pQueueFamilyIndices = queue_families;
   }

   /* Per spec section 12.3. Images
    *
    * - If tiling is VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT and flags contains
    *   VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT, then the pNext chain must include a
    *   VkImageFormatListCreateInfo structure with non-zero viewFormatCount.
    *
    * ANB and aliased ANB always chain proper format list for mutable swapchain
    * image support, but AHB is allowed to mutate without an explicit format
    * list due to legacy spec issue. So we chain a view format of the create
    * format itself to satisfy VK_EXT_image_drm_format_modifier VUs.
    */
   if (view_format_count ||
       image->create_flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) {
      if (view_format_count) {
         typed_memcpy(view_formats, raw_list->pViewFormats, view_format_count);
      } else {
         view_format_count = 1;
         view_formats = &create_info->format;
      }
      *list_info = (VkImageFormatListCreateInfo){
         .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
         .viewFormatCount = view_format_count,
         .pViewFormats = view_formats,
      };
      __vk_append_struct(create_info, list_info);
   }

   if (image->stencil_usage) {
      *stencil_info = (VkImageStencilUsageCreateInfo){
         .sType = VK_STRUCTURE_TYPE_IMAGE_STENCIL_USAGE_CREATE_INFO,
         .stencilUsage = image->stencil_usage,
      };
      __vk_append_struct(create_info, stencil_info);
   }

   /* VK_EXT_image_compression_control */
   if (raw_compress) {
      if (fixed_rate_count) {
         typed_memcpy(fixed_rates, raw_compress->pFixedRateFlags,
                      fixed_rate_count);
      }
      *compress_info = (VkImageCompressionControlEXT){
         .sType = VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_CONTROL_EXT,
         .flags = raw_compress->flags,
         .compressionControlPlaneCount = fixed_rate_count,
         .pFixedRateFlags = fixed_rate_count ? fixed_rates : NULL,
      };
      __vk_append_struct(create_info, compress_info);
   }

   image->android_deferred_create_info = create_info;

   return VK_SUCCESS;
}

static VkResult
setup_gralloc0_usage(VkFormat format, VkImageUsageFlags image_usage,
                     int *out_gralloc_usage)
{
   const VkImageUsageFlags render_usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
   const VkImageUsageFlags texture_usage =
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
   int gralloc_usage = 0;

   if (image_usage & ~(render_usage | texture_usage))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   if (image_usage & render_usage)
      gralloc_usage |= GRALLOC_USAGE_HW_RENDER;
   if (image_usage & texture_usage)
      gralloc_usage |= GRALLOC_USAGE_HW_TEXTURE;

   if (!gralloc_usage)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   *out_gralloc_usage = gralloc_usage;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_GetSwapchainGrallocUsageANDROID(VkDevice device, VkFormat format,
                                          VkImageUsageFlags imageUsage,
                                          int *grallocUsage)
{
   return setup_gralloc0_usage(format, imageUsage, grallocUsage);
}


VKAPI_ATTR VkResult VKAPI_CALL
vk_common_AcquireImageANDROID(VkDevice _device,
                              VkImage image,
                              int nativeFenceFd,
                              VkSemaphore semaphore,
                              VkFence fence)
{
   VK_FROM_HANDLE(vk_device, device, _device);
   VkResult result = VK_SUCCESS;

   /* From https://source.android.com/devices/graphics/implement-vulkan :
    *
    *    "The driver takes ownership of the fence file descriptor and closes
    *    the fence file descriptor when no longer needed. The driver must do
    *    so even if neither a semaphore or fence object is provided, or even
    *    if vkAcquireImageANDROID fails and returns an error."
    *
    * The Vulkan spec for VkImportFence/SemaphoreFdKHR(), however, requires
    * the file descriptor to be left alone on failure.
    */
   int semaphore_fd = -1, fence_fd = -1;
   if (nativeFenceFd >= 0) {
      if (semaphore != VK_NULL_HANDLE && fence != VK_NULL_HANDLE) {
         /* We have both so we have to import the sync file twice. One of
          * them needs to be a dup.
          */
         semaphore_fd = nativeFenceFd;
         fence_fd = dup(nativeFenceFd);
         if (fence_fd < 0) {
            VkResult err = (errno == EMFILE) ? VK_ERROR_TOO_MANY_OBJECTS :
                                               VK_ERROR_OUT_OF_HOST_MEMORY;
            close(nativeFenceFd);
            return vk_error(device, err);
         }
      } else if (semaphore != VK_NULL_HANDLE) {
         semaphore_fd = nativeFenceFd;
      } else if (fence != VK_NULL_HANDLE) {
         fence_fd = nativeFenceFd;
      } else {
         /* Nothing to import into so we have to close the file */
         close(nativeFenceFd);
      }
   }

   if (semaphore != VK_NULL_HANDLE) {
      const VkImportSemaphoreFdInfoKHR info = {
         .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
         .semaphore = semaphore,
         .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
         .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
         .fd = semaphore_fd,
      };
      result = device->dispatch_table.ImportSemaphoreFdKHR(_device, &info);
      if (result == VK_SUCCESS)
         semaphore_fd = -1; /* The driver took ownership */
   }

   if (result == VK_SUCCESS && fence != VK_NULL_HANDLE) {
      const VkImportFenceFdInfoKHR info = {
         .sType = VK_STRUCTURE_TYPE_IMPORT_FENCE_FD_INFO_KHR,
         .fence = fence,
         .flags = VK_FENCE_IMPORT_TEMPORARY_BIT,
         .handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT,
         .fd = fence_fd,
      };
      result = device->dispatch_table.ImportFenceFdKHR(_device, &info);
      if (result == VK_SUCCESS)
         fence_fd = -1; /* The driver took ownership */
   }

   if (semaphore_fd >= 0)
      close(semaphore_fd);
   if (fence_fd >= 0)
      close(fence_fd);

   return result;
}

static VkResult
vk_anb_semaphore_init_once(struct vk_queue *queue, struct vk_device *device)
{
   if (queue->anb_semaphore != VK_NULL_HANDLE)
      return VK_SUCCESS;

   const VkExportSemaphoreCreateInfo export_info = {
      .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
      .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
   };
   const VkSemaphoreCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = &export_info,
   };
   return device->dispatch_table.CreateSemaphore(vk_device_to_handle(device),
                                                 &create_info, NULL,
                                                 &queue->anb_semaphore);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_QueueSignalReleaseImageANDROID(VkQueue _queue,
                                         uint32_t waitSemaphoreCount,
                                         const VkSemaphore *pWaitSemaphores,
                                         VkImage image,
                                         int *pNativeFenceFd)
{
   VK_FROM_HANDLE(vk_queue, queue, _queue);
   struct vk_device *device = queue->base.device;
   VkResult result;

   if (waitSemaphoreCount == 0) {
      *pNativeFenceFd = -1;
      return VK_SUCCESS;
   }

   result = vk_anb_semaphore_init_once(queue, device);
   if (result != VK_SUCCESS)
      return result;

   STACK_ARRAY(VkSemaphoreSubmitInfo, wait_infos, waitSemaphoreCount);
   for (uint32_t i = 0; i < waitSemaphoreCount; i++) {
      wait_infos[i] = (VkSemaphoreSubmitInfo){
         .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
         .semaphore = pWaitSemaphores[i],
         /* see wsi_common_queue_present for the rationale */
         .stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
      };
   }
   const VkSemaphoreSubmitInfo signal_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = queue->anb_semaphore,
      /* see wsi_common_queue_present for the rationale */
      .stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
   };
   if (device->copy_sync_payloads != NULL) {
      result = vk_device_copy_semaphore_payloads(
         device, waitSemaphoreCount, wait_infos, 1, &signal_info, 0, NULL);
   } else {
      const VkSubmitInfo2 submit_info = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
         .waitSemaphoreInfoCount = waitSemaphoreCount,
         .pWaitSemaphoreInfos = wait_infos,
         .signalSemaphoreInfoCount = 1,
         .pSignalSemaphoreInfos = &signal_info,
      };
      result = device->dispatch_table.QueueSubmit2(_queue, 1, &submit_info,
                                                   VK_NULL_HANDLE);
   }
   STACK_ARRAY_FINISH(wait_infos);
   if (result != VK_SUCCESS)
      return result;

   const VkSemaphoreGetFdInfoKHR get_fd = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
      .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
      .semaphore = queue->anb_semaphore,
   };
   return device->dispatch_table.GetSemaphoreFdKHR(vk_device_to_handle(device),
                                                   &get_fd, pNativeFenceFd);
}

#if ANDROID_API_LEVEL >= 26

uint64_t
vk_android_get_front_buffer_usage(void)
{
   struct u_gralloc *gralloc = vk_android_get_ugralloc();
   if (gralloc) {
      uint64_t usage = 0;
      int ret = u_gralloc_get_front_rendering_usage(gralloc, &usage);
      if (!ret)
         return usage;
   }
   return 0;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_GetSwapchainGrallocUsage2ANDROID(
   VkDevice device, VkFormat format, VkImageUsageFlags imageUsage,
   VkSwapchainImageUsageFlagsANDROID swapchainImageUsage,
   uint64_t *grallocConsumerUsage, uint64_t *grallocProducerUsage)
{
   int gralloc_usage;
   VkResult result = setup_gralloc0_usage(format, imageUsage, &gralloc_usage);
   if (result != VK_SUCCESS)
      return result;

   /* Setup gralloc1 usage flags from gralloc0 flags. */
   *grallocConsumerUsage = *grallocProducerUsage = 0;
   if (gralloc_usage & GRALLOC_USAGE_HW_RENDER)
      *grallocProducerUsage |= GRALLOC1_PRODUCER_USAGE_GPU_RENDER_TARGET;
   if (gralloc_usage & GRALLOC_USAGE_HW_TEXTURE)
      *grallocConsumerUsage |= GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE;

   /* for front buffer rendering */
   if (swapchainImageUsage & VK_SWAPCHAIN_IMAGE_USAGE_SHARED_BIT_ANDROID)
      *grallocProducerUsage |= vk_android_get_front_buffer_usage();

   return VK_SUCCESS;
}

/* AHB the driver itself just allocated (Android WSI swapchain image, contents
 * still undefined), set by the caller around its import on the same thread.
 * Only this buffer may be written by the linear probe below. */
static __thread const struct AHardwareBuffer *vk_android_owned_ahb;

void
vk_android_set_owned_ahb(const struct AHardwareBuffer *ahb)
{
   vk_android_owned_ahb = ahb;
}

/* No gralloc mapper is reachable: app linker namespaces (clns-N) cannot load
 * libvndksupport or the vendor mapper on vendor API < 34. For a driver-owned
 * single-plane 32bpp AHB with CPU read usage, prove the layout instead of
 * guessing: write cookies at the corners and center through our own mmap of
 * the dma-buf at y * stride * 4 + x * 4, then read them back through
 * AHardwareBuffer_lock, whose CPU view is linear with desc.stride by API
 * contract. A full match means the dma-buf holds linear pixels at offset 0
 * with that pitch; anything else is refused. */
static VkResult
vk_android_probe_linear_ahb(struct AHardwareBuffer *ahb,
                            const AHardwareBuffer_Desc *d,
                            const native_handle_t *h,
                            VkImageDrmFormatModifierExplicitCreateInfoEXT *out,
                            VkSubresourceLayout *out_layouts, int max_planes)
{
   if (!ahb || ahb != vk_android_owned_ahb)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   const char *why = NULL;
   const uint64_t pitch = (uint64_t)d->stride * 4;
   off_t size = 0;
   uint8_t *map = MAP_FAILED;
   int lr = -1;
   void *cpu = NULL;
   unsigned match = 0;
   const uint32_t xs[3] = { 0, d->width / 2, d->width - 1 };
   const uint32_t ys[3] = { 0, d->height / 2, d->height - 1 };
#define VK_AHB_COOKIE(i) (0x9a5c0000u ^ ((i) << 12) ^ (xs[(i) % 3] << 1) ^ ys[(i) / 3])

   if (!h || h->numFds < 1 || max_planes < 1 || d->layers != 1 ||
       !d->width || !d->height || d->stride < d->width ||
       (d->format != AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM &&
        d->format != AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM) ||
       !(d->usage & AHARDWAREBUFFER_USAGE_CPU_READ_MASK)) {
      why = "unsupported desc";
      goto done;
   }
   /* First fd with a size: the one panvk_android_find_dma_buf_fd imports
    * (Arm gralloc4 handles do not always put the dma-buf at data[0]). */
   int fd = -1;
   for (int i = 0; i < h->numFds && size <= 0; i++) {
      fd = h->data[i];
      size = fd >= 0 ? lseek(fd, 0, SEEK_END) : -1;
   }
   if (size <= 0 || pitch * d->height > (uint64_t)size) {
      why = "dma-buf size";
      goto done;
   }
   map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   if (map == MAP_FAILED) {
      why = "mmap";
      goto done;
   }
   struct dma_buf_sync sync = { DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE };
   ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
   for (unsigned i = 0; i < 9; i++) {
      uint32_t cookie = VK_AHB_COOKIE(i);
      memcpy(map + ys[i / 3] * pitch + xs[i % 3] * 4, &cookie, 4);
   }
   sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
   ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
   munmap(map, size);

   lr = AHardwareBuffer_lock(ahb, d->usage & AHARDWAREBUFFER_USAGE_CPU_READ_MASK,
                             -1, NULL, &cpu);
   for (unsigned i = 0; !lr && cpu && i < 9; i++) {
      uint32_t got;
      memcpy(&got, (uint8_t *)cpu + ys[i / 3] * pitch + xs[i % 3] * 4, 4);
      match += got == VK_AHB_COOKIE(i);
   }
   if (!lr)
      AHardwareBuffer_unlock(ahb, NULL);
   why = match == 9 ? NULL : "cookie mismatch";
#undef VK_AHB_COOKIE

done:
   mesa_logi("[P0A-V19-FULLPLANE] backend=cpu-linear-probe %ux%u fmt=%u stride=%u size=%lld lock_rc=%d match=%u/9 -> %s",
             d->width, d->height, d->format, d->stride, (long long)size, lr, match,
             why ? why : "LINEAR");
   if (why)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   memset(out, 0, sizeof(*out));
   memset(out_layouts, 0, sizeof(*out_layouts) * max_planes);
   out->sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
   out->pPlaneLayouts = out_layouts;
   out->drmFormatModifier = DRM_FORMAT_MOD_LINEAR;
   out->drmFormatModifierPlaneCount = 1;
   out_layouts[0].rowPitch = pitch;
   return VK_SUCCESS;
}

VkResult
vk_android_get_ahb_layout(
   struct AHardwareBuffer *ahardware_buffer,
   VkImageDrmFormatModifierExplicitCreateInfoEXT *out,
   VkSubresourceLayout *out_layouts, int max_planes)
{
   AHardwareBuffer_Desc description;
   const native_handle_t *handle =
      AHardwareBuffer_getNativeHandle(ahardware_buffer);

   AHardwareBuffer_describe(ahardware_buffer, &description);

   struct u_gralloc_buffer_handle gr_handle = {
      .handle = handle,
      .pixel_stride = description.stride,
      .hal_format = description.format,
   };

   VkResult result = vk_gralloc_to_drm_explicit_layout(&gr_handle, out,
                                                       out_layouts, max_planes);
   if (result == VK_ERROR_INVALID_EXTERNAL_HANDLE)
      result = vk_android_probe_linear_ahb(ahardware_buffer, &description,
                                           handle, out, out_layouts, max_planes);
   return result;
}

/* From the Android hardware_buffer.h header:
 *
 *    "The buffer will be written to by the GPU as a framebuffer attachment.
 *
 *    Note that the name of this flag is somewhat misleading: it does not
 *    imply that the buffer contains a color format. A buffer with depth or
 *    stencil format that will be used as a framebuffer attachment should
 *    also have this flag. Use the equivalent flag
 *    AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER to avoid this confusion."
 *
 * The flag was renamed from COLOR_OUTPUT to FRAMEBUFFER at Android API
 * version 29.
 */
#if ANDROID_API_LEVEL < 29
#define AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT
#endif

/* Convert an AHB format to a VkFormat, based on the "AHardwareBuffer Format
 * Equivalence" table in Vulkan spec.
 *
 * Note that this only covers a subset of AHB formats defined in NDK.  Drivers
 * can support more AHB formats, including private ones.
 */
VkFormat
vk_ahb_format_to_image_format(uint32_t ahb_format)
{
   switch (ahb_format) {
   case AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM:
   case AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM:
      return VK_FORMAT_R8G8B8A8_UNORM;
   case AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM:
      return VK_FORMAT_R8G8B8_UNORM;
   case AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM:
      return VK_FORMAT_R5G6B5_UNORM_PACK16;
   case AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT:
      return VK_FORMAT_R16G16B16A16_SFLOAT;
   case AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM:
      return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
   case AHARDWAREBUFFER_FORMAT_D16_UNORM:
      return VK_FORMAT_D16_UNORM;
   case AHARDWAREBUFFER_FORMAT_D24_UNORM:
      return VK_FORMAT_X8_D24_UNORM_PACK32;
   case AHARDWAREBUFFER_FORMAT_D24_UNORM_S8_UINT:
      return VK_FORMAT_D24_UNORM_S8_UINT;
   case AHARDWAREBUFFER_FORMAT_D32_FLOAT:
      return VK_FORMAT_D32_SFLOAT;
   case AHARDWAREBUFFER_FORMAT_D32_FLOAT_S8_UINT:
      return VK_FORMAT_D32_SFLOAT_S8_UINT;
   case AHARDWAREBUFFER_FORMAT_S8_UINT:
      return VK_FORMAT_S8_UINT;
#if ANDROID_API_LEVEL >= 33
   case AHARDWAREBUFFER_FORMAT_R8_UNORM:
      return VK_FORMAT_R8_UNORM;
#endif
   case AHARDWAREBUFFER_FORMAT_Y8:
      return VK_FORMAT_R8_UNORM;
#if ANDROID_API_LEVEL >= 34
   case AHARDWAREBUFFER_FORMAT_R16_UINT:
      return VK_FORMAT_R16_UINT;
   case AHARDWAREBUFFER_FORMAT_R16G16_UINT:
      return VK_FORMAT_R16G16_UINT;
#endif
   case AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM:
      return VK_FORMAT_B8G8R8A8_UNORM;
   default:
      return VK_FORMAT_UNDEFINED;
   }
}

/* Convert a VkFormat to an AHB format, based on the "AHardwareBuffer Format
 * Equivalence" table in Vulkan spec.
 *
 * Note that this only covers a subset of AHB formats defined in NDK.  Drivers
 * can support more AHB formats, including private ones.
 */
uint32_t
vk_image_format_to_ahb_format(VkFormat vk_format)
{
   switch (vk_format) {
   case VK_FORMAT_R8G8B8A8_UNORM:
      return AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
   case VK_FORMAT_R8G8B8_UNORM:
      return AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM;
   case VK_FORMAT_R5G6B5_UNORM_PACK16:
      return AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM;
   case VK_FORMAT_R16G16B16A16_SFLOAT:
      return AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT;
   case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
      return AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM;
   case VK_FORMAT_D16_UNORM:
      return AHARDWAREBUFFER_FORMAT_D16_UNORM;
   case VK_FORMAT_X8_D24_UNORM_PACK32:
      return AHARDWAREBUFFER_FORMAT_D24_UNORM;
   case VK_FORMAT_D24_UNORM_S8_UINT:
      return AHARDWAREBUFFER_FORMAT_D24_UNORM_S8_UINT;
   case VK_FORMAT_D32_SFLOAT:
      return AHARDWAREBUFFER_FORMAT_D32_FLOAT;
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return AHARDWAREBUFFER_FORMAT_D32_FLOAT_S8_UINT;
   case VK_FORMAT_S8_UINT:
      return AHARDWAREBUFFER_FORMAT_S8_UINT;
#if ANDROID_API_LEVEL >= 33
   case VK_FORMAT_R8_UNORM:
      return AHARDWAREBUFFER_FORMAT_R8_UNORM;
#endif
#if ANDROID_API_LEVEL >= 34
   case VK_FORMAT_R16_UINT:
      return AHARDWAREBUFFER_FORMAT_R16_UINT;
   case VK_FORMAT_R16G16_UINT:
      return AHARDWAREBUFFER_FORMAT_R16G16_UINT;
#endif
   case VK_FORMAT_B8G8R8A8_UNORM:
      return AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM;
   default:
      return 0;
   }
}

VkFormat
vk_external_format_to_efr_format(VkFormat external_format)
{
   /* passthrough for RGB formats */
   if (!vk_format_get_ycbcr_info(external_format))
      return external_format;

   switch (external_format) {
   case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
   case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
      return VK_FORMAT_R8G8B8A8_UNORM;
   case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
      return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
   default:
      break;
   }

   /* no support beyond the potentially required YUV formats */
   return VK_FORMAT_UNDEFINED;
}

/* Construct ahw usage mask from image usage bits, see
 * 'AHardwareBuffer Usage Equivalence' in Vulkan spec.
 */
uint64_t
vk_image_usage_to_ahb_usage(const VkImageCreateFlags2KHR vk_create,
                            const VkImageUsageFlags2KHR vk_usage)
{
   uint64_t ahb_usage = 0;
   if (vk_usage & (VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT))
      ahb_usage |= AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;

   if (vk_usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
      ahb_usage |= AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER;

   if (vk_usage & VK_IMAGE_USAGE_STORAGE_BIT)
      ahb_usage |= AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER;

   if (vk_create & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
      ahb_usage |= AHARDWAREBUFFER_USAGE_GPU_CUBE_MAP;

   if (vk_create & VK_IMAGE_CREATE_PROTECTED_BIT)
      ahb_usage |= AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT;

   /* No usage bits set - set at least one GPU usage. */
   if (ahb_usage == 0)
      ahb_usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;

   return ahb_usage;
}

static uint64_t
vk_image_info_to_ahb_usage(VkFormat vk_format,
                           VkImageCreateFlags2KHR vk_create,
                           VkImageUsageFlags2KHR vk_usage,
                           const void *image_info_pnext)
{
   uint64_t ahb_usage = vk_image_usage_to_ahb_usage(vk_create, vk_usage);
   bool force_linear = false;

   /* Optimal tiling can be assumed for mutability only between unorm and srgb
    * variants. For anything beyond that, force AHB alloc with linear tiling.
    * This should cover most practical usages without perf impact. Ideally, a
    * new gralloc query is needed to precisely instruct the allocation, but the
    * shape could vary a lot across different vendors.
    *
    * To be noted: Android 16 and 17 have missed to forward the app provided
    * image format list to AHB usage query for VK_KHR_swapchain_mutable_format
    * support in the platform loader. The lucky part is the exact list does get
    * forwarded to the actual ANB image creation. So we additionally workaround
    * to assume optimal tiling when there's no format list provided.
    */
   if (vk_create & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) {
      const VkImageFormatListCreateInfo *format_list =
         vk_find_struct_const(image_info_pnext, IMAGE_FORMAT_LIST_CREATE_INFO);
      if (format_list && format_list->viewFormatCount > 1) {
         /* vk_format_srgb_to_linear returns the original format if not srgb */
         const VkFormat src_fmt =
            vk_format_srgb_to_linear(format_list->pViewFormats[0]);

         for (uint32_t i = 1; i < format_list->viewFormatCount; i++) {
            const VkFormat dst_fmt =
               vk_format_srgb_to_linear(format_list->pViewFormats[i]);
            if (src_fmt != dst_fmt) {
               force_linear = true;
               break;
            }
         }
      }
   }

   /* VK_IMAGE_COMPRESSION_DISABLED_EXT means the app doesn't want an
    * implicit compressed/tiled layout for this image. Use
    * CPU_WRITE_RARELY to implicitly force LINEAR, preventing gralloc from
    * allocating a compressed buffer and silently violating
    * VK_IMAGE_COMPRESSION_DISABLED_EXT.
    */
   const VkImageCompressionControlEXT *compression_control =
      vk_find_struct_const(image_info_pnext, IMAGE_COMPRESSION_CONTROL_EXT);
   if (compression_control &&
       (compression_control->flags & VK_IMAGE_COMPRESSION_DISABLED_EXT) &&
       !vk_format_is_depth_or_stencil(vk_format))
      force_linear = true;

   if (force_linear)
      ahb_usage |= AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY;

   return ahb_usage;
}

static inline uint64_t
vk_image_format_info_to_ahb_usage(const VkPhysicalDeviceImageFormatInfo2 *info)
{

   const VkImageCreateFlags2KHR flags = vk_image_format_info_2_flags(info);
   const VkImageUsageFlags2KHR usage = vk_image_format_info_2_usage(info);
   return vk_image_info_to_ahb_usage(info->format, flags, usage, info->pNext);
}

static inline uint64_t
vk_image_create_info_to_ahb_usage(const VkImageCreateInfo *info)
{
   const VkImageCreateFlags2KHR flags = vk_image_create_flags(info);
   const VkImageUsageFlags2KHR usage = vk_image_usage_flags(info);
   return vk_image_info_to_ahb_usage(info->format, flags, usage, info->pNext);
}

static bool
vk_ahb_probe_format(uint32_t ahb_format, uint64_t ahb_usage)
{
   AHardwareBuffer_Desc desc = {
      .width = 16,
      .height = 16,
      .layers = 1,
      .format = ahb_format,
      .usage = ahb_usage,
   };
#if ANDROID_API_LEVEL >= 29
   return AHardwareBuffer_isSupported(&desc);
#else
   AHardwareBuffer *ahb = NULL;
   int ret = 0;

   ret = AHardwareBuffer_allocate(&desc, &ahb);
   if (ret)
      return false;

   AHardwareBuffer_release(ahb);

   return true;
#endif
}

struct AHardwareBuffer *
vk_alloc_ahardware_buffer(const VkMemoryAllocateInfo *pAllocateInfo)
{
   const VkMemoryDedicatedAllocateInfo *dedicated_info =
      vk_find_struct_const(pAllocateInfo->pNext,
                           MEMORY_DEDICATED_ALLOCATE_INFO);

   uint32_t w = 0;
   uint32_t h = 1;
   uint32_t layers = 1;
   uint32_t format = 0;
   uint64_t usage = 0;

   /* If caller passed dedicated information. */
   if (dedicated_info && dedicated_info->image) {
      VK_FROM_HANDLE(vk_image, image, dedicated_info->image);

      if (!image->ahb_format) {
         mesa_loge("image->ahb_format must not be UNDEFINED");
         return NULL;
      }

      w = image->extent.width;
      h = image->extent.height;
      layers = image->array_layers;
      format = image->ahb_format;

      /* Populate more accurate AHB usage via vk_image_create_info_to_ahb_usage
       * if vk_android_init_deferred_image has been adopted. Otherwise, fallback
       * to vk_image_usage_to_ahb_usage.
       */
      if (image->android_deferred_create_info) {
         usage = vk_image_create_info_to_ahb_usage(
            image->android_deferred_create_info);
      } else {
         usage = vk_image_usage_to_ahb_usage(image->create_flags, image->usage);
         if ((image->compr_flags & VK_IMAGE_COMPRESSION_DISABLED_EXT) &&
             !vk_format_is_depth_or_stencil(image->format))
            usage |= AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY;
      }
   } else {
      /* AHB export allocation for VkBuffer requires a valid allocationSize */
      assert(pAllocateInfo->allocationSize);
      w = pAllocateInfo->allocationSize;
      format = AHARDWAREBUFFER_FORMAT_BLOB;
      usage = AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER |
              AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
              AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
   }

   struct AHardwareBuffer_Desc desc = {
      .width = w,
      .height = h,
      .layers = layers,
      .format = format,
      .usage = usage,
    };

   struct AHardwareBuffer *ahb;
   if (AHardwareBuffer_allocate(&desc, &ahb) != 0) {
      mesa_loge("Failed to allocate AHB (w=%u,h=%u,l=%u,f=%u,u=0x%" PRIx64 ")",
                w, h, layers, format, usage);
      return NULL;
   }

   return ahb;
}

static VkResult
get_ahb_buffer_format_properties2(
   struct vk_device *device, const struct AHardwareBuffer *buffer,
   VkAndroidHardwareBufferFormatProperties2ANDROID *pProperties)
{
   /* Get a description of buffer contents . */
   AHardwareBuffer_Desc desc;
   AHardwareBuffer_describe(buffer, &desc);

   /* Verify description. */
   bool gpu_usage = desc.usage & (AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                                  AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
                                  AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER);

   /* "Buffer must be a valid Android hardware buffer object with at least
    * one of the AHARDWAREBUFFER_USAGE_GPU_* usage flags."
    */
   if (!gpu_usage) {
      mesa_loge("AHB (format=%u,usage=0x%" PRIx64 ") has no GPU usage",
                desc.format, desc.usage);
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }

   /* No known gralloc implementations currently allocate with a
    * layers > 1. So return an error if we happen to get one since
    * the rest of mesa won't handle it properly.
    */
   if (desc.layers > 1) {
      mesa_loge("Unsupported AHB desc.layers(%u) > 1", desc.layers);
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }

   /* Fill properties fields based on description. */
   VkAndroidHardwareBufferFormatProperties2ANDROID *p = pProperties;

   p->samplerYcbcrConversionComponents.r = VK_COMPONENT_SWIZZLE_IDENTITY;
   p->samplerYcbcrConversionComponents.g = VK_COMPONENT_SWIZZLE_IDENTITY;
   p->samplerYcbcrConversionComponents.b = VK_COMPONENT_SWIZZLE_IDENTITY;
   p->samplerYcbcrConversionComponents.a = VK_COMPONENT_SWIZZLE_IDENTITY;

   p->suggestedYcbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY;
   p->suggestedYcbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;

   p->suggestedXChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;
   p->suggestedYChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;

   VkFormatProperties2 format_properties = {.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};

   p->format = vk_ahb_format_to_image_format(desc.format);

   VkFormat external_format = p->format;

   if (p->format != VK_FORMAT_UNDEFINED)
      goto finish;

   /* External format only case
    *
    * From vkGetAndroidHardwareBufferPropertiesANDROID spec:
    * "If the Android hardware buffer has one of the formats listed in the Format
    * Equivalence table (see spec.), then format must have the equivalent Vulkan
    * format listed in the table. Otherwise, format may be VK_FORMAT_UNDEFINED,
    * indicating the Android hardware buffer can only be used with an external format."
    *
    * From SKIA source code analysis: p->format MUST be VK_FORMAT_UNDEFINED, if the
    * format is not in the Equivalence table.
    */

   struct u_gralloc_buffer_handle gr_handle = {
      .handle = AHardwareBuffer_getNativeHandle(buffer),
      .pixel_stride = desc.stride,
      .hal_format = desc.format,
   };

   struct u_gralloc_buffer_basic_info info;
   if (u_gralloc_get_buffer_basic_info(vk_android_get_ugralloc(), &gr_handle,
                                       &info) != 0) {
      mesa_loge("Failed to get u_gralloc_buffer_basic_info");
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }

   switch (info.drm_fourcc) {
   case DRM_FORMAT_YVU420:
      /* Assuming that U and V planes are swapped earlier */
      external_format = VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
      break;
   case DRM_FORMAT_NV12:
      external_format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
      break;
   case DRM_FORMAT_P010:
      external_format = VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
      break;
   case DRM_FORMAT_P210:
      external_format = VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16;
      break;
   case DRM_FORMAT_YUYV:
      external_format = VK_FORMAT_G8B8G8R8_422_UNORM;
      break;
   case DRM_FORMAT_UYVY:
      external_format = VK_FORMAT_B8G8R8G8_422_UNORM;
      break;
   case DRM_FORMAT_XBGR8888:
      /* This can be resolved from IMPLEMENTATION_DEFINED AHB format */
      external_format = VK_FORMAT_R8G8B8A8_UNORM;
      break;
   default:
      mesa_loge("Unsupported external DRM format: %d", info.drm_fourcc);
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }

   struct u_gralloc_buffer_color_info color_info;
   if (u_gralloc_get_buffer_color_info(vk_android_get_ugralloc(), &gr_handle,
                                       &color_info) != 0) {
      mesa_loge("Failed to get u_gralloc_buffer_color_info");
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }

   switch (color_info.yuv_color_space) {
   case __DRI_YUV_COLOR_SPACE_ITU_REC601:
      p->suggestedYcbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601;
      break;
   case __DRI_YUV_COLOR_SPACE_ITU_REC709:
      p->suggestedYcbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;
      break;
   case __DRI_YUV_COLOR_SPACE_ITU_REC2020:
      p->suggestedYcbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_2020;
      break;
   default:
      break;
   }

   p->suggestedYcbcrRange = (color_info.sample_range == __DRI_YUV_NARROW_RANGE)
                               ? VK_SAMPLER_YCBCR_RANGE_ITU_NARROW
                               : VK_SAMPLER_YCBCR_RANGE_ITU_FULL;
   p->suggestedXChromaOffset =
      (color_info.horizontal_siting == __DRI_YUV_CHROMA_SITING_0_5)
         ? VK_CHROMA_LOCATION_MIDPOINT
         : VK_CHROMA_LOCATION_COSITED_EVEN;
   p->suggestedYChromaOffset =
      (color_info.vertical_siting == __DRI_YUV_CHROMA_SITING_0_5)
         ? VK_CHROMA_LOCATION_MIDPOINT
         : VK_CHROMA_LOCATION_COSITED_EVEN;

finish:

   device->physical->dispatch_table.GetPhysicalDeviceFormatProperties2(
      (VkPhysicalDevice)device->physical, external_format, &format_properties);

   p->formatFeatures = format_properties.formatProperties.optimalTilingFeatures;
   p->externalFormat = external_format;

   /* From vkGetAndroidHardwareBufferPropertiesANDROID spec:
    * "The formatFeatures member *must* include
    *  VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT and at least one of
    *  VK_FORMAT_FEATURE_2_MIDPOINT_CHROMA_SAMPLES_BIT or
    *  VK_FORMAT_FEATURE_2_COSITED_CHROMA_SAMPLES_BIT"
    */
   p->formatFeatures |= VK_FORMAT_FEATURE_2_MIDPOINT_CHROMA_SAMPLES_BIT_KHR;

   return VK_SUCCESS;
}

VkResult
vk_common_GetAndroidHardwareBufferPropertiesANDROID(
   VkDevice device_h, const struct AHardwareBuffer *buffer,
   VkAndroidHardwareBufferPropertiesANDROID *pProperties)
{
   VK_FROM_HANDLE(vk_device, device, device_h);
   VkResult result;

   VkAndroidHardwareBufferFormatPropertiesANDROID *format_prop = NULL;
   VkAndroidHardwareBufferFormatProperties2ANDROID *format_prop2 = NULL;
   VkAndroidHardwareBufferFormatResolvePropertiesANDROID *format_resolve = NULL;

   vk_foreach_struct(sType, ext, pProperties->pNext) {
      switch (sType) {
      case VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID:
         format_prop = ext;
         break;
      case VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_2_ANDROID:
         format_prop2 = ext;
         break;
      case VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_RESOLVE_PROPERTIES_ANDROID:
         format_resolve = ext;
         break;
      default:
         break;
      }
   }

   /* Fill format properties of an Android hardware buffer. */
   VkAndroidHardwareBufferFormatProperties2ANDROID local_prop2 = {
      .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_2_ANDROID,
   };
   if (!format_prop2 && (format_prop || format_resolve))
      format_prop2 = &local_prop2;

   if (format_prop2) {
      result = get_ahb_buffer_format_properties2(device, buffer, format_prop2);
      if (result != VK_SUCCESS)
         return result;
   }

   if (format_prop) {
      format_prop->format                 = format_prop2->format;
      format_prop->externalFormat         = format_prop2->externalFormat;
      format_prop->formatFeatures         =
         vk_format_features2_to_features(format_prop2->formatFeatures);
      format_prop->samplerYcbcrConversionComponents =
         format_prop2->samplerYcbcrConversionComponents;
      format_prop->suggestedYcbcrModel    = format_prop2->suggestedYcbcrModel;
      format_prop->suggestedYcbcrRange    = format_prop2->suggestedYcbcrRange;
      format_prop->suggestedXChromaOffset = format_prop2->suggestedXChromaOffset;
      format_prop->suggestedYChromaOffset = format_prop2->suggestedYChromaOffset;
   }

   if (format_resolve) {
      assert(format_prop2->externalFormat != VK_FORMAT_UNDEFINED);
      format_resolve->colorAttachmentFormat =
         vk_external_format_to_efr_format((VkFormat)format_prop2->externalFormat);

      if (format_resolve->colorAttachmentFormat != VK_FORMAT_UNDEFINED) {
         format_prop2->formatFeatures |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT;
         if (format_prop) {
            format_prop->formatFeatures |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
         }
      }
   }

   const native_handle_t *handle = AHardwareBuffer_getNativeHandle(buffer);
   if (!handle || handle->numFds <= 0)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   int dma_buf = -1;
   off_t dma_buf_size = -1;
   VkMemoryFdPropertiesKHR fd_props = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR,
   };

   for (int i = 0; i < handle->numFds; i++) {
      int fd = handle->data[i];
      if (fd < 0)
         continue;

      struct stat st;
      if (fstat(fd, &st) < 0)
         continue;

      off_t size = lseek(fd, 0, SEEK_END);
      if (size <= 0)
         continue;

      if (device->dispatch_table.GetMemoryFdPropertiesKHR(
             device_h, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd,
             &fd_props) == VK_SUCCESS) {
         dma_buf = fd;
         dma_buf_size = size;
         break;
      }
   }

   if (dma_buf < 0 || dma_buf_size <= 0)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   pProperties->allocationSize = (VkDeviceSize)dma_buf_size;
   pProperties->memoryTypeBits = fd_props.memoryTypeBits;

   return VK_SUCCESS;
}

/* AHB image support per spec:
 *
 * - Any Android hardware buffer successfully allocated outside Vulkan with
 *   usage that includes AHARDWAREBUFFER_USAGE_GPU_* must be supported when
 *   using equivalent Vulkan image parameters.
 *
 * - If a given choice of image parameters are supported for import, they can
 *   also be used to create an image and memory that will be exported to an
 *   Android hardware buffer.
 *
 * An additional constraint derived from above is:
 *
 * - If that AHB cannot get allocated out, then the Vulkan driver must not
 *   advertise support for the AHB backed image.
 *
 * Based on all above, this helper implements the AHB validation as well as
 * the AHB external and usage props filling.
 */
VkResult
vk_android_get_ahb_image_properties(
   VkPhysicalDevice pdev_handle,
   const VkPhysicalDeviceImageFormatInfo2 *info,
   VkImageFormatProperties2 *props)
{
   VK_FROM_HANDLE(vk_physical_device, pdevice, pdev_handle);
   VkExternalImageFormatProperties *external_props;
   VkAndroidHardwareBufferUsageANDROID *ahb_usage_props;

   ASSERTED const VkPhysicalDeviceExternalImageFormatInfo *external_info =
      vk_find_struct_const(info->pNext,
                           PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO);
   assert(
      external_info &&
      external_info->handleType ==
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID);

   if (info->type != VK_IMAGE_TYPE_2D) {
      return vk_errorf(pdevice, VK_ERROR_FORMAT_NOT_SUPPORTED,
                       "type (%u) unsupported for AHB", info->type);
   }

   const uint32_t ahb_format = vk_image_format_to_ahb_format(info->format);
   if (!ahb_format) {
      return vk_errorf(pdevice, VK_ERROR_FORMAT_NOT_SUPPORTED,
                       "format (%u) unsupported for AHB", info->format);
   }

   const uint64_t ahb_usage = vk_image_format_info_to_ahb_usage(info);
   if (!vk_ahb_probe_format(ahb_format, ahb_usage)) {
      return vk_errorf(pdevice, VK_ERROR_FORMAT_NOT_SUPPORTED,
                       "ahb_format (%u) ahb_usage (0x%" PRIx64 ") unsupported",
                       ahb_format, ahb_usage);
   }

   external_props =
      vk_find_struct(props->pNext, EXTERNAL_IMAGE_FORMAT_PROPERTIES);
   if (external_props) {
      external_props->externalMemoryProperties = (VkExternalMemoryProperties){
         .externalMemoryFeatures =
            VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT |
            VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
            VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT,
         .exportFromImportedHandleTypes =
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
         .compatibleHandleTypes =
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
      };
   }

   ahb_usage_props =
      vk_find_struct(props->pNext, ANDROID_HARDWARE_BUFFER_USAGE_ANDROID);
   if (ahb_usage_props)
      ahb_usage_props->androidHardwareBufferUsage = ahb_usage;

   return VK_SUCCESS;
}

void
vk_android_get_ahb_buffer_properties(
   VkPhysicalDevice pdev_handle,
   const VkPhysicalDeviceExternalBufferInfo *info,
   VkExternalBufferProperties *props)
{
   assert(info->handleType ==
          VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID);
   props->externalMemoryProperties = (VkExternalMemoryProperties){
      .externalMemoryFeatures =
         VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
         VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT,
      .exportFromImportedHandleTypes =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
      .compatibleHandleTypes =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
   };
}

bool
vk_android_rp_attachment_has_external_format(
   const VkAttachmentDescription2 *desc)
{
   return desc->format == VK_FORMAT_UNDEFINED &&
          vk_android_get_external_format(desc->pNext) != VK_FORMAT_UNDEFINED;
}

VkFormat
vk_android_get_external_format(const void *pnext)
{
   const VkExternalFormatANDROID *info =
      vk_find_struct_const(pnext, EXTERNAL_FORMAT_ANDROID);
   return info ? (VkFormat)info->externalFormat : VK_FORMAT_UNDEFINED;
}

static bool
subpass_uses_efr(const VkRenderPassCreateInfo2 *info, uint32_t s)
{
   const VkSubpassDescription2 *subpass = &info->pSubpasses[s];
   if (!subpass->pResolveAttachments || subpass->colorAttachmentCount != 1)
      return false;

   const VkAttachmentReference2 *resolve_ref =
      &subpass->pResolveAttachments[0];
   return resolve_ref->attachment != VK_ATTACHMENT_UNUSED &&
          vk_android_rp_attachment_has_external_format(
             &info->pAttachments[resolve_ref->attachment]);
}

bool
vk_android_is_efr_rp(struct vk_device *device,
                     const VkRenderPassCreateInfo2 *info)
{
   const struct vk_properties *props = &device->physical->properties;
   if (props->nullColorAttachmentWithExternalFormatResolve != VK_TRUE)
      return false;

   for (uint32_t s = 0; s < info->subpassCount; s++) {
      if (subpass_uses_efr(info, s))
         return true;
   }

   return false;
}

VkResult
vk_android_create_efr_rp(struct vk_device *device,
                         const VkRenderPassCreateInfo2 *info,
                         const VkAllocationCallbacks *alloc,
                         VkRenderPass *out_rp_handle)
{
   uint32_t efr_subpass_count = 0;
   for (uint32_t s = 0; s < info->subpassCount; s++) {
      if (subpass_uses_efr(info, s))
         efr_subpass_count++;
   }

   STACK_ARRAY(VkAttachmentDescription2, attachments, info->attachmentCount);
   STACK_ARRAY(VkSubpassDescription2, subpasses, info->subpassCount);
   STACK_ARRAY(VkAttachmentReference2, color_refs, efr_subpass_count);

   VkRenderPassCreateInfo2 local_info = *info;
   local_info.pAttachments = attachments;
   local_info.pSubpasses = subpasses;

   typed_memcpy(attachments, info->pAttachments, info->attachmentCount);
   for (uint32_t a = 0; a < info->attachmentCount; a++) {
      VkAttachmentDescription2 *att = &attachments[a];
      if (vk_android_rp_attachment_has_external_format(att)) {
         VkFormat external_format = vk_android_get_external_format(att->pNext);
         att->format = vk_external_format_to_efr_format(external_format);
      }
   }

   VkAttachmentReference2 *color_ref_ptr = color_refs;

   typed_memcpy(subpasses, info->pSubpasses, info->subpassCount);
   for (uint32_t s = 0; s < info->subpassCount; s++) {
      if (!subpass_uses_efr(info, s))
         continue;

      VkSubpassDescription2 *subpass = &subpasses[s];
      assert(subpass->colorAttachmentCount == 1);
      assert(subpass->pResolveAttachments);

      *color_ref_ptr = subpass->pResolveAttachments[0];
      subpass->pColorAttachments = color_ref_ptr++;
      subpass->pResolveAttachments = NULL;
   }

   VkResult result = device->dispatch_table.CreateRenderPass2(
      vk_device_to_handle(device), &local_info, alloc, out_rp_handle);

   STACK_ARRAY_FINISH(attachments);
   STACK_ARRAY_FINISH(subpasses);
   STACK_ARRAY_FINISH(color_refs);

   return result;
}

bool
vk_android_is_efr_rendering_info(const VkRenderingInfo *info)
{
   /* implies nullColorAttachmentWithExternalFormatResolve == VK_TRUE */
   assert(info);
   return info->colorAttachmentCount == 1 &&
          info->pColorAttachments[0].imageView == VK_NULL_HANDLE &&
          info->pColorAttachments[0].resolveMode ==
             VK_RESOLVE_MODE_EXTERNAL_FORMAT_DOWNSAMPLE_BIT_ANDROID;
}

const VkRenderingInfo *
vk_android_get_efr_rendering_info(const VkRenderingInfo *info,
                                  VkRenderingInfo *local_info,
                                  VkRenderingAttachmentInfo *local_color_att)
{
   *local_color_att = info->pColorAttachments[0];
   local_color_att->imageView = local_color_att->resolveImageView;
   local_color_att->imageLayout = local_color_att->resolveImageLayout;
   local_color_att->resolveImageView = VK_NULL_HANDLE;
   local_color_att->resolveMode = VK_RESOLVE_MODE_NONE;

   *local_info = *info;
   local_info->pColorAttachments = local_color_att;
   return local_info;
}

bool
vk_android_is_efr_inheritance_rendering_info(
   const VkCommandBufferInheritanceInfo *info,
   const VkCommandBufferInheritanceRenderingInfo *r_info)
{
   /* implies nullColorAttachmentWithExternalFormatResolve == VK_TRUE */
   assert(info && r_info);
   return r_info->colorAttachmentCount == 1 &&
          r_info->pColorAttachmentFormats[0] == VK_FORMAT_UNDEFINED &&
          vk_android_get_external_format(info->pNext) != VK_FORMAT_UNDEFINED;
}

const VkCommandBufferInheritanceRenderingInfo *
vk_android_get_efr_inheritance_rendering_info(
   const VkCommandBufferInheritanceInfo *info,
   const VkCommandBufferInheritanceRenderingInfo *r_info,
   VkCommandBufferInheritanceRenderingInfo *local_info,
   VkFormat *local_color_format)
{
   VkFormat external_format = vk_android_get_external_format(info->pNext);
   *local_color_format = vk_external_format_to_efr_format(external_format);
   *local_info = *r_info;
   local_info->pColorAttachmentFormats = local_color_format;
   return local_info;
}

bool
vk_android_is_efr_pipeline_rendering_info(
   const VkGraphicsPipelineCreateInfo *info,
   const VkPipelineRenderingCreateInfo *r_info)
{
   /* implies nullColorAttachmentWithExternalFormatResolve == VK_TRUE */
   assert(info && r_info);
   return r_info->colorAttachmentCount == 1 &&
          r_info->pColorAttachmentFormats[0] == VK_FORMAT_UNDEFINED &&
          vk_android_get_external_format(info->pNext) != VK_FORMAT_UNDEFINED;
}

const VkPipelineRenderingCreateInfo *
vk_android_get_efr_pipeline_rendering_info(
   const VkGraphicsPipelineCreateInfo *info,
   const VkPipelineRenderingCreateInfo *r_info,
   VkPipelineRenderingCreateInfo *local_info,
   VkFormat *local_color_format)
{
   VkFormat external_format = vk_android_get_external_format(info->pNext);
   *local_color_format = vk_external_format_to_efr_format(external_format);

   *local_info = *r_info;
   local_info->pColorAttachmentFormats = local_color_format;
   return local_info;
}

#endif /* ANDROID_API_LEVEL >= 26 */
