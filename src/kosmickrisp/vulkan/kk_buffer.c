/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_buffer.h"

#include "kk_device.h"
#include "kk_entrypoints.h"
#include "kk_physical_device.h"

#include "kosmickrisp/bridge/mtl_bridge.h"

static uint64_t
kk_get_buffer_alignment(const struct kk_physical_device *pdev, uint64_t size,
                        VkBufferUsageFlags2KHR usage_flags,
                        VkBufferCreateFlags create_flags)
{
   uint64_t alignment;
   mtl_heap_buffer_size_and_align_with_length(pdev->mtl_dev_handle, &size,
                                              &alignment);

   /** TODO_KOSMICKRISP Metal requires that texel buffers be aligned to the
    * format they'll use. Since we won't be able to know the format until the
    * view is created, we should align to the worst case scenario. For this, we
    * need to request all supported format alignments and take the largest one.
    */
   return alignment;
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_CreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   struct kk_buffer *buffer;

   if (pCreateInfo->size > KK_MAX_BUFFER_SIZE)
      return vk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   buffer =
      vk_buffer_create(&dev->vk, pCreateInfo, pAllocator, sizeof(*buffer));
   if (!buffer)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   *pBuffer = kk_buffer_to_handle(buffer);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
kk_DestroyBuffer(VkDevice device, VkBuffer _buffer,
                 const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   VK_FROM_HANDLE(kk_buffer, buffer, _buffer);

   if (!buffer)
      return;

   if (buffer->mtl_handle)
      mtl_release(buffer->mtl_handle);

   vk_buffer_destroy(&dev->vk, pAllocator, &buffer->vk);
}

VKAPI_ATTR void VKAPI_CALL
kk_GetDeviceBufferMemoryRequirements(
   VkDevice device, const VkDeviceBufferMemoryRequirements *pInfo,
   VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   struct kk_physical_device *pdev = kk_device_physical(dev);

   const uint64_t alignment = kk_get_buffer_alignment(
      pdev, pInfo->pCreateInfo->size, pInfo->pCreateInfo->usage,
      pInfo->pCreateInfo->flags);

   pMemoryRequirements->memoryRequirements = (VkMemoryRequirements){
      .size = align64(pInfo->pCreateInfo->size, alignment),
      .alignment = alignment,
      .memoryTypeBits = BITFIELD_MASK(pdev->mem_type_count),
   };

   vk_foreach_struct_const(ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *dedicated = (void *)ext;
         dedicated->prefersDedicatedAllocation = false;
         dedicated->requiresDedicatedAllocation = false;
         break;
      }
      default:
         vk_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
kk_GetPhysicalDeviceExternalBufferProperties(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
   VkExternalBufferProperties *pExternalBufferProperties)
{
   /* The Vulkan 1.3.256 spec says:
    *
    *    VUID-VkPhysicalDeviceExternalBufferInfo-handleType-parameter
    *
    *    "handleType must be a valid VkExternalMemoryHandleTypeFlagBits value"
    *
    * This differs from VkPhysicalDeviceExternalImageFormatInfo, which
    * surprisingly permits handleType == 0.
    */
   assert(pExternalBufferInfo->handleType != 0);

   /* All of the current flags are for sparse which we don't support yet.
    * Even when we do support it, doing sparse on external memory sounds
    * sketchy.  Also, just disallowing flags is the safe option.
    */
   if (pExternalBufferInfo->flags)
      goto unsupported;

   switch (pExternalBufferInfo->handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT:
      pExternalBufferProperties->externalMemoryProperties =
         kk_mtlheap_mem_props;
      return;
   default:
      goto unsupported;
   }

unsupported:
   /* From the Vulkan 1.3.256 spec:
    *
    *    compatibleHandleTypes must include at least handleType.
    */
   pExternalBufferProperties->externalMemoryProperties =
      (VkExternalMemoryProperties){
         .compatibleHandleTypes = pExternalBufferInfo->handleType,
      };
}

static VkResult
kk_bind_buffer_memory(struct kk_device *dev, const VkBindBufferMemoryInfo *info)
{
   // Do the actual memory binding
   VK_FROM_HANDLE(kk_device_memory, mem, info->memory);
   VK_FROM_HANDLE(kk_buffer, buffer, info->buffer);

   buffer->mtl_handle = mtl_new_buffer_with_length(
      mem->bo->mtl_handle, buffer->vk.size, info->memoryOffset);
   buffer->vk.device_address = mtl_buffer_get_gpu_address(buffer->mtl_handle);
   /* We need Metal to give us a CPU mapping so it correctly captures the
    * data in the GPU debugger... */
   mtl_get_contents(buffer->mtl_handle);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_BindBufferMemory2(VkDevice device, uint32_t bindInfoCount,
                     const VkBindBufferMemoryInfo *pBindInfos)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   VkResult first_error_or_success = VK_SUCCESS;

   for (uint32_t i = 0; i < bindInfoCount; ++i) {
      VkResult result = kk_bind_buffer_memory(dev, &pBindInfos[i]);

      const VkBindMemoryStatusKHR *status =
         vk_find_struct_const(pBindInfos[i].pNext, BIND_MEMORY_STATUS_KHR);
      if (status != NULL && status->pResult != NULL)
         *status->pResult = result;

      if (first_error_or_success == VK_SUCCESS)
         first_error_or_success = result;
   }

   return first_error_or_success;
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL
kk_GetBufferDeviceAddress(UNUSED VkDevice device,
                          const VkBufferDeviceAddressInfo *pInfo)
{
   VK_FROM_HANDLE(kk_buffer, buffer, pInfo->buffer);

   return vk_buffer_address(&buffer->vk, 0);
}

VKAPI_ATTR uint64_t VKAPI_CALL
kk_GetBufferOpaqueCaptureAddress(UNUSED VkDevice device,
                                 const VkBufferDeviceAddressInfo *pInfo)
{
   VK_FROM_HANDLE(kk_buffer, buffer, pInfo->buffer);

   return vk_buffer_address(&buffer->vk, 0);
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_GetBufferOpaqueCaptureDescriptorDataEXT(
   VkDevice device, const VkBufferCaptureDescriptorDataInfoEXT *pInfo,
   void *pData)
{
   return VK_SUCCESS;
}
