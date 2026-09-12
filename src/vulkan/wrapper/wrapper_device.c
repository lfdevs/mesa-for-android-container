#include <sys/stat.h>

#include "wrapper_private.h"
#include "wrapper_log.h"
#include "wrapper_bcdec.h"
#include "spirv_patcher.hpp"
#include "wrapper_entrypoints.h"
#include "wrapper_trampolines.h"
#include "vk_alloc.h"
#include "vk_common_entrypoints.h"
#include "vk_device.h"
#include "vk_dispatch_table.h"
#include "vk_extensions.h"
#include "vk_queue.h"
#include "vk_util.h"
#include "util/list.h"
#include "util/simple_mtx.h"

const struct vk_device_extension_table wrapper_device_extensions =
{
   .KHR_swapchain = true,
   .EXT_swapchain_maintenance1 = true,
   .KHR_swapchain_mutable_format = true,
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   .EXT_display_control = true,
#endif
   .KHR_present_id = true,
   .KHR_present_wait = true,
   .KHR_incremental_present = true,
};

const struct vk_device_extension_table wrapper_filter_extensions =
{
   .EXT_hdr_metadata = true,
   .GOOGLE_display_timing = true,
   .KHR_shared_presentable_image = true,
   .EXT_image_compression_control_swapchain = true,
};

static struct wrapper_buffer *
get_wrapper_buffer_from_handle(struct wrapper_device *device, VkBuffer buffer) {
   struct wrapper_buffer *wb = NULL;

   simple_mtx_lock(&device->resource_mutex);
   wb = _mesa_hash_table_u64_search(device->buffer_table, (uint64_t) buffer);
   simple_mtx_unlock(&device->resource_mutex);

   return wb;
}

static struct wrapper_image *
get_wrapper_image_from_handle(struct wrapper_device *device, VkImage image) {
   struct wrapper_image *wi = NULL;

   simple_mtx_lock(&device->resource_mutex);
   wi = _mesa_hash_table_u64_search(device->image_table, (uint64_t) image);
   simple_mtx_unlock(&device->resource_mutex);

   return wi;
}

static struct wrapper_fence *
get_wrapper_fence_from_handle(struct wrapper_device *device, VkFence fence) {
   struct wrapper_fence *wf = NULL;

   simple_mtx_lock(&device->resource_mutex);
   wf = _mesa_hash_table_u64_search(device->fence_table, (uint64_t) fence);
   simple_mtx_unlock(&device->resource_mutex);

   return wf;
}

static void
wrapper_filter_enabled_extensions(const struct wrapper_device *device,
                                  uint32_t *enable_extension_count,
                                  const char **enable_extensions)
{
   for (int idx = 0; idx < VK_DEVICE_EXTENSION_COUNT; idx++) {
      if (!device->vk.enabled_extensions.extensions[idx])
         continue;

      if (!device->physical->base_supported_extensions.extensions[idx])
         continue;

      if (wrapper_device_extensions.extensions[idx])
         continue;

      if (wrapper_filter_extensions.extensions[idx])
         continue;

      enable_extensions[(*enable_extension_count)++] =
         vk_device_extensions[idx].extensionName;
   }
}

static inline void
wrapper_append_required_extensions(const struct vk_device *device,
                                  uint32_t *count,
                                  const char **exts) {
#define REQUIRED_EXTENSION(name) \
   if (device->physical->supported_extensions.name) { \
      exts[(*count)++] = "VK_" #name; \
   }

   REQUIRED_EXTENSION(KHR_external_fence);
   REQUIRED_EXTENSION(KHR_external_semaphore);
   REQUIRED_EXTENSION(KHR_external_memory);
   REQUIRED_EXTENSION(KHR_external_fence_fd);
   REQUIRED_EXTENSION(KHR_external_semaphore_fd);
   REQUIRED_EXTENSION(KHR_external_memory_fd);
   REQUIRED_EXTENSION(KHR_dedicated_allocation);
   REQUIRED_EXTENSION(EXT_queue_family_foreign);
   REQUIRED_EXTENSION(KHR_maintenance1)
   REQUIRED_EXTENSION(KHR_maintenance2)
   REQUIRED_EXTENSION(KHR_synchronization2)
   REQUIRED_EXTENSION(KHR_image_format_list)
   REQUIRED_EXTENSION(KHR_swapchain);
   REQUIRED_EXTENSION(KHR_timeline_semaphore);
   REQUIRED_EXTENSION(EXT_external_memory_host);
   REQUIRED_EXTENSION(EXT_external_memory_dma_buf);
   REQUIRED_EXTENSION(EXT_image_drm_format_modifier);
   REQUIRED_EXTENSION(ANDROID_external_memory_android_hardware_buffer);
#undef REQUIRED_EXTENSION
}

static void unlink_vk_struct(VkBaseInStructure *create_info, const VkBaseInStructure **current, VkBaseInStructure **prev) {
   if (!*prev)
      create_info->pNext = (*current)->pNext;
   else
      (*prev)->pNext = (*current)->pNext;

   *current = (*current)->pNext;
}

static void patch_image_create_info(VkBaseInStructure *create_info, VkFormat *format) {
   const VkBaseInStructure *current = (VkBaseInStructure *)create_info->pNext;

   while (current != NULL) {
      switch(current->sType) {
	     case VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO: {
		    VkImageFormatListCreateInfo *formatCreateInfo =
				  (VkImageFormatListCreateInfo *)current;
			formatCreateInfo->viewFormatCount = 1;
			formatCreateInfo->pViewFormats = format;
			break;
         }
         default:
	break;
      }
	  current = current->pNext;
   }
}

static void process_device_pnext_chain(VkBaseInStructure *create_info, struct wrapper_physical_device *pdevice) {
   const uint32_t api_version = pdevice->properties2.properties.apiVersion;
   const VkBaseInStructure *current = (VkBaseInStructure *)create_info->pNext;
   VkBaseInStructure *prev = NULL;

   while (current != NULL) {
      switch(current->sType) {
          case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT: {
             VkPhysicalDeviceTransformFeedbackFeaturesEXT *transform_features =
                (VkPhysicalDeviceTransformFeedbackFeaturesEXT *)current;
             transform_features->geometryStreams &= pdevice->base_supported_features.geometryStreams;
             break;
          }
          case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT:
             if (pdevice->base_supported_extensions.EXT_robustness2)
                break;
             WRAPPER_LOG(info, "Unlinking VkPhysicalDeviceRobustness2FeaturesEXT from pNext chain");
             unlink_vk_struct(create_info, &current, &prev);
             continue;
          case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES:
             if (api_version >= VK_MAKE_VERSION(1, 1, 0))
                break;
             WRAPPER_LOG(info, "Unlinking VkPhysicalDeviceVulkan11Features from pNext chain");
             unlink_vk_struct(create_info, &current, &prev);
             continue;
          case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES:
             if (api_version >= VK_MAKE_VERSION(1, 2, 0))
                break;
             WRAPPER_LOG(info, "Unlinking VkPhysicalDeviceVulkan12Features from pNext chain");
             unlink_vk_struct(create_info, &current, &prev);
             continue;
          case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES:
             if (api_version >= VK_MAKE_VERSION(1, 3, 0))
                break;
             WRAPPER_LOG(info, "Unlinking VkPhysicalDeviceVulkan13Features from pNext chain");
             unlink_vk_struct(create_info, &current, &prev);
             continue;
          default:
             break;
      }
      prev = (VkBaseInStructure *)current;
      current = current->pNext;
   }
}

static VkResult
wrapper_create_device_queue(struct wrapper_device *device,
                            const VkDeviceCreateInfo* pCreateInfo)
{
   const VkDeviceQueueCreateInfo *create_info;
   struct wrapper_queue *queue;
   VkResult result;

   for (int i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
      create_info = &pCreateInfo->pQueueCreateInfos[i];
      for (int j = 0; j < create_info->queueCount; j++) {
         queue = vk_zalloc(&device->vk.alloc, sizeof(*queue), 8,
                           VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
         if (!queue)
            return VK_ERROR_OUT_OF_HOST_MEMORY;

         if (create_info->flags) {
            device->dispatch_table.GetDeviceQueue2(
               device->dispatch_handle,
               &(VkDeviceQueueInfo2) {
                  .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
                  .flags = create_info->flags,
                  .queueFamilyIndex = create_info->queueFamilyIndex,
                  .queueIndex = j,
               },
               &queue->dispatch_handle);;
         } else {
            device->dispatch_table.GetDeviceQueue(
               device->dispatch_handle, create_info->queueFamilyIndex,
               j, &queue->dispatch_handle);
         }
         queue->device = device;

         result = vk_queue_init(&queue->vk, &device->vk, create_info, j);
         if (result != VK_SUCCESS) {
            vk_free(&device->vk.alloc, queue);
            return result;
         }
      }
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_CreateDevice(VkPhysicalDevice physicalDevice,
                     const VkDeviceCreateInfo* pCreateInfo,
                     const VkAllocationCallbacks* pAllocator,
                     VkDevice* pDevice)
{
   VK_FROM_HANDLE(wrapper_physical_device, physical_device, physicalDevice);
   const char *wrapper_enable_extensions[VK_DEVICE_EXTENSION_COUNT];
   uint32_t wrapper_enable_extension_count = 0;
   VkDeviceCreateInfo wrapper_create_info = *pCreateInfo;
   struct vk_device_dispatch_table dispatch_table;
   struct wrapper_device *device;
   VkPhysicalDeviceFeatures2 *pdf2;
   VkPhysicalDeviceFeatures *pdf;
   VkResult result;
   static int wrapper_safe_create_device = -1;

   device = vk_zalloc2(&physical_device->instance->vk.alloc, pAllocator,
                       sizeof(*device), 8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device)
      return vk_error(physical_device, VK_ERROR_OUT_OF_HOST_MEMORY);

   list_inithead(&device->command_buffer_list);
   list_inithead(&device->device_memory_list);
   list_inithead(&device->image_list);
   list_inithead(&device->buffer_list);
   list_inithead(&device->fence_list);
   device->image_table = _mesa_hash_table_u64_create(NULL);
   device->buffer_table = _mesa_hash_table_u64_create(NULL);
   device->fence_table = _mesa_hash_table_u64_create(NULL);

   simple_mtx_init(&device->resource_mutex, mtx_plain);
   device->physical = physical_device;

   vk_device_dispatch_table_from_entrypoints(
      &dispatch_table, &wrapper_device_entrypoints, true);
   vk_device_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_device_entrypoints, false);
   vk_device_dispatch_table_from_entrypoints(
      &dispatch_table, &wrapper_device_trampolines, false);

   result = vk_device_init(&device->vk, &physical_device->vk,
                           &dispatch_table, pCreateInfo, pAllocator);

   if (result != VK_SUCCESS) {
      WRAPPER_LOG(error, "Failed to init Vulkan device, res %d", result);
      vk_free2(&physical_device->instance->vk.alloc, pAllocator,
               device);
      return vk_error(physical_device, result);
   }

   wrapper_filter_enabled_extensions(device,
      &wrapper_enable_extension_count, wrapper_enable_extensions);
   wrapper_append_required_extensions(&device->vk,
      &wrapper_enable_extension_count, wrapper_enable_extensions);

   wrapper_create_info.enabledExtensionCount = wrapper_enable_extension_count;
   wrapper_create_info.ppEnabledExtensionNames = wrapper_enable_extensions;

   pdf = (void *)pCreateInfo->pEnabledFeatures;
   pdf2 = __vk_find_struct((void *)pCreateInfo->pNext,
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);

#define DISABLE_FEATURE(f) \
if (pdf && pdf->f) { \
   pdf->f &= physical_device->base_supported_features.f; \
} \
\
if (pdf2 && pdf2->features.f) { \
   pdf2->features.f &= physical_device->base_supported_features.f; \
}

   DISABLE_FEATURE(textureCompressionBC);
   DISABLE_FEATURE(multiViewport);
   DISABLE_FEATURE(depthClamp);
   DISABLE_FEATURE(depthBiasClamp);
   DISABLE_FEATURE(fillModeNonSolid);
   DISABLE_FEATURE(shaderClipDistance);
   DISABLE_FEATURE(shaderCullDistance);
   DISABLE_FEATURE(dualSrcBlend);
   DISABLE_FEATURE(multiDrawIndirect);

#undef DISABLE_FEATURE

   process_device_pnext_chain((VkBaseInStructure *)&wrapper_create_info, device->physical);

   if (WRAPPER_LOG_LEVEL(info)) {
      for (int i = 0; i < wrapper_enable_extension_count; i++) {
         WRAPPER_LOG(info, "Enabling device extension %s", wrapper_enable_extensions[i]);
      }
   }

   if (wrapper_safe_create_device == -1) {
      wrapper_safe_create_device = getenv("WRAPPER_SAFE_CREATE_DEVICE") ? atoi(getenv("WRAPPER_SAFE_CREATE_DEVICE")) : 1;
   }

   result = physical_device->dispatch_table.CreateDevice(
      physical_device->dispatch_handle, &wrapper_create_info,
         pAllocator, &device->dispatch_handle);

   if (result != VK_SUCCESS) {
      if (wrapper_safe_create_device) {
         WRAPPER_LOG(info, "Forcing device creation with a NULL pNext chain");
         wrapper_create_info.pNext = NULL;
         result = physical_device->dispatch_table.CreateDevice(
            physical_device->dispatch_handle, &wrapper_create_info,
               pAllocator, &device->dispatch_handle);
      }

      if (result != VK_SUCCESS) {
         WRAPPER_LOG(error, "Failed driver createDevice, res %d", result);
         wrapper_DestroyDevice(wrapper_device_to_handle(device),
                               &device->vk.alloc);
         return vk_error(physical_device, result);
      }
   }

   void *gdpa = physical_device->instance->dispatch_table.GetInstanceProcAddr(
      physical_device->instance->dispatch_handle, "vkGetDeviceProcAddr");
   vk_device_dispatch_table_load(&device->dispatch_table, gdpa,
                                 device->dispatch_handle);

   result = wrapper_create_device_queue(device, pCreateInfo);
   if (result != VK_SUCCESS) {
      wrapper_DestroyDevice(wrapper_device_to_handle(device),
                            &device->vk.alloc);
      return vk_error(physical_device, result);
   }

   if (!physical_device->vk.supported_features.memoryMapPlaced) {
      device->vk.dispatch_table.AllocateMemory =
         wrapper_device_trampolines.AllocateMemory;
      device->vk.dispatch_table.MapMemory2 =
         wrapper_device_trampolines.MapMemory2;
      device->vk.dispatch_table.UnmapMemory =
         wrapper_device_trampolines.UnmapMemory;
      device->vk.dispatch_table.UnmapMemory2 =
         wrapper_device_trampolines.UnmapMemory2;
      device->vk.dispatch_table.FreeMemory =
         wrapper_device_trampolines.FreeMemory;
   }

   *pDevice = wrapper_device_to_handle(device);

   return VK_SUCCESS;
}

static void
wrapper_buffer_destroy(struct wrapper_device *device,
					   struct wrapper_buffer *wb,
					   const VkAllocationCallbacks *pAllocator)
{
   if (wb == NULL)
      return;

   simple_mtx_lock(&device->resource_mutex);

   device->dispatch_table.DestroyBuffer(device->dispatch_handle,
      wb->dispatch_handle, pAllocator);

   _mesa_hash_table_u64_remove(device->buffer_table, (uint64_t)wb->dispatch_handle);
   list_del(&wb->link);

   simple_mtx_unlock(&device->resource_mutex);

   vk_object_free(&device->vk, &device->vk.alloc, wb);
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_CreateBuffer(VkDevice _device,
					 const VkBufferCreateInfo *pCreateInfo,
					 const VkAllocationCallbacks *pAllocator,
					 VkBuffer *pBuffer)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);
   VkResult res;

   res = device->dispatch_table.CreateBuffer(device->dispatch_handle,
      pCreateInfo, pAllocator, pBuffer);

   if (res != VK_SUCCESS) {
      WRAPPER_LOG(error, "Failed to create buffer, res %d", res);
      return res;
   }

   simple_mtx_lock(&device->resource_mutex);

   struct wrapper_buffer *wb = vk_object_zalloc(&device->vk,
      &device->vk.alloc, sizeof(struct wrapper_buffer), VK_OBJECT_TYPE_BUFFER);

   if (!wb) {
      WRAPPER_LOG(error, "Failed to allocate wrapper_buffer");
      simple_mtx_unlock(&device->resource_mutex);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   wb->device = device;
   wb->size = pCreateInfo->size;
   wb->dispatch_handle = *pBuffer;

   list_add(&wb->link, &device->buffer_list);
   _mesa_hash_table_u64_insert(device->buffer_table, (uint64_t)wb->dispatch_handle, wb);

   simple_mtx_unlock(&device->resource_mutex);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_BindBufferMemory(VkDevice _device,
						 VkBuffer buffer,
						 VkDeviceMemory memory,
						 VkDeviceSize memoryOffset)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);
   VkResult res;

   res = device->dispatch_table.BindBufferMemory(device->dispatch_handle,
      buffer, memory, memoryOffset);

   if (res != VK_SUCCESS) {
      WRAPPER_LOG(error, "Failed to bind buffer memory, res %d", res);
      return res;
   }

   struct wrapper_buffer *wb = get_wrapper_buffer_from_handle(device, buffer);
   if (wb == NULL) {
      WRAPPER_LOG(error, "Failed to query wrapper_buffer");
      simple_mtx_unlock(&device->resource_mutex);
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   wb->memory = memory;
   wb->offset = memoryOffset;

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
wrapper_DestroyBuffer(VkDevice _device,
					  VkBuffer buffer,
					  const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);

   struct wrapper_buffer *wb = get_wrapper_buffer_from_handle(device, buffer);
   wrapper_buffer_destroy(device, wb, pAllocator);
}

static void
wrapper_image_destroy(struct wrapper_device *device,
					  struct wrapper_image *wi,
					  const VkAllocationCallbacks *pAllocator)
{
   if (wi == NULL)
      return;

   simple_mtx_lock(&device->resource_mutex);

   device->dispatch_table.DestroyImage(device->dispatch_handle,
      wi->dispatch_handle, pAllocator);

   _mesa_hash_table_u64_remove(device->image_table, (uint64_t)wi->dispatch_handle);
   list_del(&wi->link);

   simple_mtx_unlock(&device->resource_mutex);

   vk_object_free(&device->vk, &device->vk.alloc, wi);
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_CreateImage(VkDevice _device,
					const VkImageCreateInfo *pCreateInfo,
					const VkAllocationCallbacks *pAllocator,
					VkImage *pImage)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);
   VkResult res;
   VkImageCreateInfo create_info = *pCreateInfo;

   if (is_emulated_bcn(device->physical, pCreateInfo->format)) {
      create_info.format = get_format_for_bcn(pCreateInfo->format);
      if (create_info.flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT)
         create_info.flags &= ~VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
      patch_image_create_info((VkBaseInStructure *)&create_info, &create_info.format);
   }

   res = device->dispatch_table.CreateImage(device->dispatch_handle,
      &create_info, pAllocator, pImage);

   if (res != VK_SUCCESS) {
      WRAPPER_LOG(error, "Failed to create image, res %d", res);
      return res;
   }

   simple_mtx_lock(&device->resource_mutex);

   struct wrapper_image *wi = vk_object_zalloc(&device->vk,
      &device->vk.alloc, sizeof(struct wrapper_image), VK_OBJECT_TYPE_IMAGE);

   if (!wi) {
      WRAPPER_LOG(error, "Failed to allocate wrapper_image");
      simple_mtx_unlock(&device->resource_mutex);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   wi->device = device;
   wi->info = *pCreateInfo;
   wi->dispatch_handle = *pImage;

   list_add(&wi->link, &device->image_list);
   _mesa_hash_table_u64_insert(device->image_table, (uint64_t)wi->dispatch_handle, wi);

   simple_mtx_unlock(&device->resource_mutex);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_CreateImageView(VkDevice _device,
						const VkImageViewCreateInfo *pCreateInfo,
						const VkAllocationCallbacks *pAllocator,
						VkImageView *pView)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);
   VkImageViewCreateInfo create_info = *pCreateInfo;
   VkResult result;

   if (is_emulated_bcn(device->physical, pCreateInfo->format)) {
      create_info.format = get_format_for_bcn(pCreateInfo->format);
      patch_image_create_info((VkBaseInStructure *)&create_info, &create_info.format);
   }

   result = device->dispatch_table.CreateImageView(device->dispatch_handle,
     &create_info, pAllocator, pView);

   if (result != VK_SUCCESS)
	  WRAPPER_LOG(error, "Failed to create image view, res %d", result);

   return result;
}

VKAPI_ATTR void VKAPI_CALL
wrapper_DestroyImage(VkDevice _device,
					 VkImage image,
					 const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);

   struct wrapper_image *wi = get_wrapper_image_from_handle(device, image);
   wrapper_image_destroy(device, wi, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL
wrapper_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex,
                       uint32_t queueIndex, VkQueue* pQueue) {
   vk_common_GetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
}

VKAPI_ATTR void VKAPI_CALL
wrapper_GetDeviceQueue2(VkDevice _device, const VkDeviceQueueInfo2* pQueueInfo,
                        VkQueue* pQueue) {
   VK_FROM_HANDLE(vk_device, device, _device);

   struct vk_queue *queue = NULL;
   vk_foreach_queue(iter, device) {
      if (iter->queue_family_index == pQueueInfo->queueFamilyIndex &&
          iter->index_in_family == pQueueInfo->queueIndex &&
          iter->flags == pQueueInfo->flags) {
         queue = iter;
         break;
      }
   }

   *pQueue = queue ? vk_queue_to_handle(queue) : VK_NULL_HANDLE;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
wrapper_GetDeviceProcAddr(VkDevice _device, const char* pName) {
   VK_FROM_HANDLE(wrapper_device, device, _device);
   return vk_device_get_proc_addr(&device->vk, pName);
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_QueueSubmit(VkQueue _queue, uint32_t submitCount,
                    const VkSubmitInfo* pSubmits, VkFence fence)
{
   VK_FROM_HANDLE(wrapper_queue, queue, _queue);
   VkSubmitInfo wrapper_submits[submitCount];
   VkCommandBuffer *command_buffers;
   VkResult result;

   struct wrapper_fence *wf = get_wrapper_fence_from_handle(queue->device, fence);

   for (int i = 0; i < submitCount; i++) {
      const VkSubmitInfo *submit_info = &pSubmits[i];
      command_buffers = malloc(sizeof(VkCommandBuffer) *
         submit_info->commandBufferCount);
      for (int j = 0; j < submit_info->commandBufferCount; j++) {
         VK_FROM_HANDLE(wrapper_command_buffer, wcb,
                        submit_info->pCommandBuffers[j]);
         wcb->fence = wf;
         command_buffers[j] = wcb->dispatch_handle;

      }
      wrapper_submits[i] = pSubmits[i];
      wrapper_submits[i].pCommandBuffers = command_buffers;
   }

   result = queue->device->dispatch_table.QueueSubmit(
      queue->dispatch_handle, submitCount, wrapper_submits, fence);

   for (int i = 0; i < submitCount; i++)
      free((void *)wrapper_submits[i].pCommandBuffers);

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_QueueSubmit2(VkQueue _queue, uint32_t submitCount,
                     const VkSubmitInfo2* pSubmits, VkFence fence)
{
   VK_FROM_HANDLE(wrapper_queue, queue, _queue);
   VkSubmitInfo2 wrapper_submits[submitCount];
   VkCommandBufferSubmitInfo *command_buffers;
   VkResult result;

   struct wrapper_fence *wf = get_wrapper_fence_from_handle(queue->device, fence);

   for (int i = 0; i < submitCount; i++) {
      const VkSubmitInfo2 *submit_info = &pSubmits[i];
      command_buffers = malloc(sizeof(VkCommandBufferSubmitInfo) *
         submit_info->commandBufferInfoCount);
      for (int j = 0; j < submit_info->commandBufferInfoCount; j++) {
         VK_FROM_HANDLE(wrapper_command_buffer, wcb,
                        submit_info->pCommandBufferInfos[j].commandBuffer);
         wcb->fence = wf;
         command_buffers[j] = pSubmits[i].pCommandBufferInfos[j];
         command_buffers[j].commandBuffer = wcb->dispatch_handle;
      }
      wrapper_submits[i] = pSubmits[i];
      wrapper_submits[i].pCommandBufferInfos = command_buffers;
   }

   result = queue->device->dispatch_table.QueueSubmit2(
      queue->dispatch_handle, submitCount, wrapper_submits, fence);

   for (int i = 0; i < submitCount; i++)
      free((void *)wrapper_submits[i].pCommandBufferInfos);

   return result;
}

static void
wrapper_fence_destroy(struct wrapper_device *device,
					  struct wrapper_fence *wf,
					  const VkAllocationCallbacks *pAllocator)
{
   if (wf == NULL)
      return;

   simple_mtx_lock(&device->resource_mutex);

   device->dispatch_table.DestroyFence(device->dispatch_handle,
      wf->dispatch_handle, pAllocator);

   _mesa_hash_table_u64_remove(device->fence_table, (uint64_t)wf->dispatch_handle);
   list_del(&wf->link);

   simple_mtx_unlock(&device->resource_mutex);

   vk_object_free(&device->vk, &device->vk.alloc, wf);
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_CreateFence(VkDevice _device,
					const VkFenceCreateInfo *pCreateInfo,
					const VkAllocationCallbacks *pAllocator,
					VkFence *pFence)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);

   VkResult res = device->dispatch_table.CreateFence(device->dispatch_handle,
      pCreateInfo, pAllocator, pFence);

   if (res != VK_SUCCESS) {
      WRAPPER_LOG(error, "Failed to create fence, res %d", res);
      return res;
   }

   simple_mtx_lock(&device->resource_mutex);

   struct wrapper_fence *wf = vk_object_zalloc(&device->vk,
      &device->vk.alloc, sizeof(struct wrapper_fence), VK_OBJECT_TYPE_FENCE);
   if (!wf) {
      WRAPPER_LOG(error, "Failed to allocate wrapper_fence");
      simple_mtx_unlock(&device->resource_mutex);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   wf->device = device;
   wf->dispatch_handle = *pFence;

   list_inithead(&wf->staging_buffers_list);

   list_add(&wf->link, &device->fence_list);
   _mesa_hash_table_u64_insert(device->fence_table, (uint64_t)wf->dispatch_handle, wf);

   simple_mtx_unlock(&device->resource_mutex);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_WaitForFences(VkDevice _device,
					  uint32_t fenceCount,
					  const VkFence *pFences,
					  VkBool32 waitAll,
					  uint64_t timeout)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);
   VkResult res;

   res = device->dispatch_table.WaitForFences(device->dispatch_handle,
     fenceCount, pFences, waitAll, timeout);

   if (res != VK_SUCCESS || device->physical->emulate_bcn < 2)
      return res;

   for (uint32_t i = 0; i < fenceCount; i++) {
      struct wrapper_fence *wf = get_wrapper_fence_from_handle(device, pFences[i]);
      list_for_each_entry_safe(struct wrapper_buffer, wb,
                               &wf->staging_buffers_list, link)
      {
         VkDeviceMemory memory = wb->memory;
         wrapper_buffer_destroy(device, wb, NULL);
         device->dispatch_table.FreeMemory(device->dispatch_handle,
            memory, NULL);
      }
   }

   return res;
}

VKAPI_ATTR void VKAPI_CALL
wrapper_DestroyFence(VkDevice _device,
					 VkFence fence,
					 const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);

   struct wrapper_fence *wf = get_wrapper_fence_from_handle(device, fence);
   wrapper_fence_destroy(device, wf, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL
wrapper_CmdExecuteCommands(VkCommandBuffer commandBuffer,
                           uint32_t commandBufferCount,
                           const VkCommandBuffer* pCommandBuffers)
{
   VK_FROM_HANDLE(wrapper_command_buffer, wcb, commandBuffer);
   VkCommandBuffer command_buffers[commandBufferCount];

   for (int i = 0; i < commandBufferCount; i++) {
      command_buffers[i] =
         wrapper_command_buffer_from_handle(pCommandBuffers[i])->dispatch_handle;
   }
   wcb->device->dispatch_table.CmdExecuteCommands(
      wcb->dispatch_handle, commandBufferCount, command_buffers);
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_CreateShaderModule(VkDevice _device,
						   const VkShaderModuleCreateInfo *pCreateInfo,
						   const VkAllocationCallbacks *pAllocator,
						   VkShaderModule *pShaderModule)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);
   static int wrapper_no_remove_clip_distance = -1;
   static int wrapper_no_patch_OpConstComp = -1;

   if (wrapper_no_remove_clip_distance == -1)
      wrapper_no_remove_clip_distance = getenv("WRAPPER_NO_REMOVE_CLIP_DISTANCE") && atoi(getenv("WRAPPER_NO_REMOVE_CLIP_DISTANCE"));

   if (wrapper_no_patch_OpConstComp == -1)
      wrapper_no_patch_OpConstComp = getenv("WRAPPER_NO_PATCH_OPCONSTCOMP") && atoi(getenv("WRAPPER_NO_PATCH_OPCONSTCOMP"));

   VkShaderModuleCreateInfo create_info = *pCreateInfo;

   simple_mtx_lock(&device->resource_mutex);

   if (device->physical->driver_properties.driverID == VK_DRIVER_ID_ARM_PROPRIETARY) {
      uint32_t *code = malloc(create_info.codeSize);
      memcpy(code, create_info.pCode, create_info.codeSize);
      if (!wrapper_no_patch_OpConstComp) patch_OpConstantComposite_to_OpSpecConstantComposite(code, create_info.codeSize);
      if (!wrapper_no_remove_clip_distance) remove_ClipDistance_CullDistance(code, &create_info.codeSize);
      create_info.pCode = code;
   }

   simple_mtx_unlock(&device->resource_mutex);

   if (WRAPPER_LOG_LEVEL(shader))
      dump_shader_code(create_info.pCode, create_info.codeSize);

   return device->dispatch_table.CreateShaderModule(
      device->dispatch_handle, &create_info, pAllocator, pShaderModule);
}

static VkResult
wrapper_command_buffer_create(struct wrapper_device *device,
                              VkCommandPool pool,
                              VkCommandBuffer dispatch_handle,
                              VkCommandBuffer *pCommandBuffers) {
   struct wrapper_command_buffer *wcb;
   wcb = vk_object_zalloc(&device->vk, &device->vk.alloc,
                          sizeof(struct wrapper_command_buffer),
                          VK_OBJECT_TYPE_COMMAND_BUFFER);
   if (!wcb)
      return vk_error(&device->vk, VK_ERROR_OUT_OF_HOST_MEMORY);

   wcb->device = device;
   wcb->pool = pool;
   wcb->dispatch_handle = dispatch_handle;
   list_add(&wcb->link, &device->command_buffer_list);

   *pCommandBuffers = wrapper_command_buffer_to_handle(wcb);

   return VK_SUCCESS;
}

static void
wrapper_command_buffer_destroy(struct wrapper_device *device,
                               struct wrapper_command_buffer *wcb) {
   if (wcb == NULL)
      return;

   device->dispatch_table.FreeCommandBuffers(
      device->dispatch_handle, wcb->pool, 1, &wcb->dispatch_handle);

   list_del(&wcb->link);
   vk_object_free(&device->vk, &device->vk.alloc, wcb);
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_AllocateCommandBuffers(VkDevice _device,
                               const VkCommandBufferAllocateInfo* pAllocateInfo,
                               VkCommandBuffer* pCommandBuffers)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);
   VkResult result;
   uint32_t i;

   result = device->dispatch_table.AllocateCommandBuffers(
      device->dispatch_handle, pAllocateInfo, pCommandBuffers);
   if (result != VK_SUCCESS)
      return result;

   simple_mtx_lock(&device->resource_mutex);

   for (i = 0; i < pAllocateInfo->commandBufferCount; i++) {
      result = wrapper_command_buffer_create(
         device, pAllocateInfo->commandPool, pCommandBuffers[i],
         pCommandBuffers + i);
      if (result != VK_SUCCESS)
         break;
   }

   if (result != VK_SUCCESS) {
      for (int q = 0; q < i; q++) {
         VK_FROM_HANDLE(wrapper_command_buffer, wcb, pCommandBuffers[q]);
         wrapper_command_buffer_destroy(device, wcb);
      }

      device->dispatch_table.FreeCommandBuffers(
         device->dispatch_handle, pAllocateInfo->commandPool,
         pAllocateInfo->commandBufferCount - i, pCommandBuffers + i);

      for (i = 0; i < pAllocateInfo->commandBufferCount; i++) {
         pCommandBuffers[i] = VK_NULL_HANDLE;
      }
   }

   simple_mtx_unlock(&device->resource_mutex);

   return result;
}

VKAPI_ATTR void VKAPI_CALL
wrapper_CmdCopyBufferToImage(VkCommandBuffer commandBuffer,
							 VkBuffer srcBuffer,
							 VkImage dstImage,
							 VkImageLayout dstLayout,
							 uint32_t regionCount,
							 const VkBufferImageCopy *pRegions)
{
   VK_FROM_HANDLE(wrapper_command_buffer, wcb, commandBuffer);
   VkResult res;

   struct wrapper_device *device = wcb->device;
   struct wrapper_image *wi = get_wrapper_image_from_handle(device, dstImage);
   struct wrapper_buffer *wb = get_wrapper_buffer_from_handle(device, srcBuffer);
   VkFormat format = wi->info.format;
   int texel_size = get_texel_size_for_format(get_format_for_bcn(format));

   if (!wi || !wb || !is_emulated_bcn(device->physical, format)) {
      device->dispatch_table.CmdCopyBufferToImage(wcb->dispatch_handle,
         srcBuffer, dstImage, dstLayout, regionCount, pRegions);
      return;
   }

   simple_mtx_lock(&device->resource_mutex);

   if (!wb->is_mapped) {
      res = device->dispatch_table.MapMemory(device->dispatch_handle,
         wb->memory, wb->offset, wb->size, 0, &wb->mapped_address);

      if (res != VK_SUCCESS) {
         WRAPPER_LOG(error, "Failed to map source buffer memory, res %d", res);
         simple_mtx_unlock(&device->resource_mutex);
         return;
      }

      wb->is_mapped = 1;
   }

   for (int i = 0; i < regionCount; i++) {
      VkBufferImageCopy copy_region = pRegions[i];
      int w = copy_region.imageExtent.width;
      int h = copy_region.imageExtent.height;
      int offset = copy_region.bufferOffset;

      struct wrapper_buffer *staging_wb = vk_object_zalloc(&device->vk,
         &device->vk.alloc, sizeof(struct wrapper_buffer), VK_OBJECT_TYPE_BUFFER);

      VkBufferCreateInfo buffer_create_info = {
         .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
         .size = w * h * texel_size,
         .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
         .flags = 0,
         .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      };

      res = device->dispatch_table.CreateBuffer(device->dispatch_handle,
         &buffer_create_info, NULL, &staging_wb->dispatch_handle);

      if (res != VK_SUCCESS) {
         WRAPPER_LOG(error, "Failed to create staging buffer, res %d", res);
         simple_mtx_unlock(&device->resource_mutex);
         return;
      }

      VkMemoryAllocateInfo allocate_info = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
         .allocationSize = w * h * texel_size,
         .memoryTypeIndex = wrapper_select_device_memory_type(device,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT),
      };

      res = device->dispatch_table.AllocateMemory(device->dispatch_handle,
         &allocate_info, NULL, &staging_wb->memory);

      if (res != VK_SUCCESS) {
         WRAPPER_LOG(error, "Failed to allocate staging buffer memory, res %d", res);
         simple_mtx_unlock(&device->resource_mutex);
         return;
      }

      res = device->dispatch_table.BindBufferMemory(device->dispatch_handle,
         staging_wb->dispatch_handle, staging_wb->memory, 0);

      if (res != VK_SUCCESS) {
         WRAPPER_LOG(error, "Failed to bind staging buffer memory, res %d", res);
         simple_mtx_unlock(&device->resource_mutex);
         return;
      }

      res = device->dispatch_table.MapMemory(device->dispatch_handle,
         staging_wb->memory, 0, w * h * texel_size, 0, &staging_wb->mapped_address);

      if (res != VK_SUCCESS) {
         WRAPPER_LOG(error, "Failed to map staging buffer memory, res %d", res);
         simple_mtx_unlock(&device->resource_mutex);
         return;
      }

      decompress_bcn_format(wb->mapped_address, staging_wb->mapped_address, w, h, format, offset);

      copy_region.bufferOffset = 0;
      copy_region.bufferRowLength = 0;
      copy_region.bufferImageHeight = 0;

      device->dispatch_table.CmdCopyBufferToImage(wcb->dispatch_handle,
         staging_wb->dispatch_handle, dstImage, dstLayout, 1, &copy_region);

      staging_wb->wcb = wcb;
      staging_wb->device = device;

      if (wcb->fence)
         list_add(&staging_wb->link, &wcb->fence->staging_buffers_list);
   }

   if (wb->is_mapped) {
      device->dispatch_table.UnmapMemory(device->dispatch_handle,
         wb->memory);

      wb->is_mapped = 0;
   }

   simple_mtx_unlock(&device->resource_mutex);
}

VKAPI_ATTR void VKAPI_CALL
wrapper_FreeCommandBuffers(VkDevice _device,
                           VkCommandPool commandPool,
                           uint32_t commandBufferCount,
                           const VkCommandBuffer* pCommandBuffers)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);

   simple_mtx_lock(&device->resource_mutex);

   for (int i = 0; i < commandBufferCount; i++) {
      VK_FROM_HANDLE(wrapper_command_buffer, wcb, pCommandBuffers[i]);
      wrapper_command_buffer_destroy(device, wcb);
   }

   simple_mtx_unlock(&device->resource_mutex);
}

VKAPI_ATTR void VKAPI_CALL
wrapper_DestroyCommandPool(VkDevice _device, VkCommandPool commandPool,
                           const VkAllocationCallbacks* pAllocator)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);

   simple_mtx_lock(&device->resource_mutex);

   list_for_each_entry_safe(struct wrapper_command_buffer, wcb,
                            &device->command_buffer_list, link) {
      if (wcb->pool == commandPool) {
         wrapper_command_buffer_destroy(device, wcb);
      }
   }

   simple_mtx_unlock(&device->resource_mutex);

   device->dispatch_table.DestroyCommandPool(device->dispatch_handle,
                                             commandPool, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL
wrapper_DestroyDevice(VkDevice _device, const VkAllocationCallbacks* pAllocator)
{
   VK_FROM_HANDLE(wrapper_device, device, _device);

   simple_mtx_lock(&device->resource_mutex);

   list_for_each_entry_safe(struct wrapper_command_buffer, wcb,
                            &device->command_buffer_list, link) {
      wrapper_command_buffer_destroy(device, wcb);
   }
   list_for_each_entry_safe(struct wrapper_device_memory, mem,
                            &device->device_memory_list, link) {
      wrapper_device_memory_destroy(mem);
   }

   simple_mtx_unlock(&device->resource_mutex);

   list_for_each_entry_safe(struct wrapper_buffer, wb,
                            &device->buffer_list, link) {
      wrapper_buffer_destroy(device, wb, pAllocator);
   }
   list_for_each_entry_safe(struct wrapper_image, wi,
                            &device->image_list, link) {
      wrapper_image_destroy(device, wi, pAllocator);
   }
   list_for_each_entry_safe(struct wrapper_fence, wf,
                            &device->fence_list, link) {
      wrapper_fence_destroy(device, wf, pAllocator);
   }

   list_for_each_entry_safe(struct vk_queue, queue, &device->vk.queues, link) {
      vk_queue_finish(queue);
      vk_free2(&device->vk.alloc, pAllocator, queue);
   }
   if (device->dispatch_handle != VK_NULL_HANDLE) {
      device->dispatch_table.DestroyDevice(device->
         dispatch_handle, pAllocator);
   }
   simple_mtx_destroy(&device->resource_mutex);
   vk_device_finish(&device->vk);
   vk_free2(&device->vk.alloc, pAllocator, device);
}

static uint64_t
unwrap_device_object(VkObjectType objectType,
                     uint64_t objectHandle)
{
   switch(objectType) {
   case VK_OBJECT_TYPE_DEVICE:
      return (uint64_t)(uintptr_t)wrapper_device_from_handle((VkDevice)(uintptr_t)objectHandle)->dispatch_handle;
   case VK_OBJECT_TYPE_QUEUE:
      return (uint64_t)(uintptr_t)wrapper_queue_from_handle((VkQueue)(uintptr_t)objectHandle)->dispatch_handle;
   case VK_OBJECT_TYPE_COMMAND_BUFFER:
      return (uint64_t)(uintptr_t)wrapper_command_buffer_from_handle((VkCommandBuffer)(uintptr_t)objectHandle)->dispatch_handle;
   default:
      return objectHandle;
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
wrapper_SetPrivateData(VkDevice _device, VkObjectType objectType,
                       uint64_t objectHandle,
                       VkPrivateDataSlot privateDataSlot,
                       uint64_t data) {
   VK_FROM_HANDLE(wrapper_device, device, _device);

   uint64_t object_handle = unwrap_device_object(objectType, objectHandle);
   return device->dispatch_table.SetPrivateData(device->dispatch_handle,
      objectType, object_handle, privateDataSlot, data);
}

VKAPI_ATTR void VKAPI_CALL
wrapper_GetPrivateData(VkDevice _device, VkObjectType objectType,
                       uint64_t objectHandle,
                       VkPrivateDataSlot privateDataSlot,
                       uint64_t* pData) {
   VK_FROM_HANDLE(wrapper_device, device, _device);

   uint64_t object_handle = unwrap_device_object(objectType, objectHandle);
   return device->dispatch_table.GetPrivateData(device->dispatch_handle,
      objectType, object_handle, privateDataSlot, pData);
}
