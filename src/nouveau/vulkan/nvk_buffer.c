/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_buffer.h"

#include "nvk_entrypoints.h"
#include "nvk_device.h"
#include "nvk_device_memory.h"
#include "nvk_physical_device.h"
#include "nvk_queue.h"
#include "nvkmd/nvkmd.h"

static uint32_t
nvk_get_buffer_alignment(const struct nvk_physical_device *pdev,
                         VkBufferUsageFlags2KHR usage_flags,
                         VkBufferCreateFlags create_flags)
{
   uint32_t alignment = 16;

   if (usage_flags & VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR)
      alignment = MAX2(alignment, nvk_min_cbuf_alignment(&pdev->info));

   if (usage_flags & VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR)
      alignment = MAX2(alignment, NVK_MIN_SSBO_ALIGNMENT);

   if (usage_flags & (VK_BUFFER_USAGE_2_UNIFORM_TEXEL_BUFFER_BIT_KHR |
                      VK_BUFFER_USAGE_2_STORAGE_TEXEL_BUFFER_BIT_KHR))
      alignment = MAX2(alignment, NVK_MIN_TEXEL_BUFFER_ALIGNMENT);

   if (create_flags & (VK_BUFFER_CREATE_SPARSE_BINDING_BIT |
                       VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT))
      alignment = MAX2(alignment, pdev->nvkmd->bind_align_B);

   return alignment;
}

static uint64_t
nvk_get_bda_replay_addr(const VkBufferCreateInfo *pCreateInfo)
{
   uint64_t addr = 0;
   vk_foreach_struct_const(ext, pCreateInfo->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_BUFFER_OPAQUE_CAPTURE_ADDRESS_CREATE_INFO: {
         const VkBufferOpaqueCaptureAddressCreateInfo *bda = (void *)ext;
         if (bda->opaqueCaptureAddress != 0) {
#ifdef NDEBUG
            return bda->opaqueCaptureAddress;
#else
            assert(addr == 0 || bda->opaqueCaptureAddress == addr);
            addr = bda->opaqueCaptureAddress;
#endif
         }
         break;
      }

      case VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_CREATE_INFO_EXT: {
         const VkBufferDeviceAddressCreateInfoEXT *bda = (void *)ext;
         if (bda->deviceAddress != 0) {
#ifdef NDEBUG
            return bda->deviceAddress;
#else
            assert(addr == 0 || bda->deviceAddress == addr);
            addr = bda->deviceAddress;
#endif
         }
         break;
      }

      default:
         break;
      }
   }

   return addr;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_CreateBuffer(VkDevice device,
                 const VkBufferCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator,
                 VkBuffer *pBuffer)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   struct nvk_buffer *buffer;
   VkResult result;

   if (pCreateInfo->size > NVK_MAX_BUFFER_SIZE)
      return vk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   buffer = vk_buffer_create(&dev->vk, pCreateInfo, pAllocator,
                             sizeof(*buffer));
   if (!buffer)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (buffer->vk.size > 0 &&
       (buffer->vk.create_flags & (VK_BUFFER_CREATE_SPARSE_BINDING_BIT |
                                   VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT))) {
      const uint32_t alignment =
         nvk_get_buffer_alignment(nvk_device_physical(dev),
                                  buffer->vk.usage,
                                  buffer->vk.create_flags);
      assert(alignment >= 4096);
      const uint64_t va_size_B = align64(buffer->vk.size, alignment);

      enum nvkmd_va_flags va_flags = 0;
      if (buffer->vk.create_flags & VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT)
         va_flags |= NVKMD_VA_SPARSE;

      uint64_t fixed_addr = 0;
      if (buffer->vk.create_flags & VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT) {
         va_flags |= NVKMD_VA_REPLAY;

         fixed_addr = nvk_get_bda_replay_addr(pCreateInfo);
         if (fixed_addr != 0)
            va_flags |= NVKMD_VA_ALLOC_FIXED;
      }

      result = nvkmd_dev_alloc_va(dev->nvkmd, &dev->vk.base,
                                  va_flags, 0 /* pte_kind */,
                                  va_size_B, alignment, fixed_addr,
                                  &buffer->va);
      if (result != VK_SUCCESS) {
         vk_buffer_destroy(&dev->vk, pAllocator, &buffer->vk);
         return result;
      }

      buffer->addr = buffer->va->addr;
   }

   *pBuffer = nvk_buffer_to_handle(buffer);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvk_DestroyBuffer(VkDevice device,
                  VkBuffer _buffer,
                  const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   VK_FROM_HANDLE(nvk_buffer, buffer, _buffer);

   if (!buffer)
      return;

   if (buffer->va != NULL)
      nvkmd_va_free(buffer->va);

   vk_buffer_destroy(&dev->vk, pAllocator, &buffer->vk);
}

VKAPI_ATTR void VKAPI_CALL
nvk_GetDeviceBufferMemoryRequirements(
   VkDevice device,
   const VkDeviceBufferMemoryRequirements *pInfo,
   VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   struct nvk_physical_device *pdev = nvk_device_physical(dev);

   const uint32_t alignment =
      nvk_get_buffer_alignment(nvk_device_physical(dev),
                               pInfo->pCreateInfo->usage,
                               pInfo->pCreateInfo->flags);

   pMemoryRequirements->memoryRequirements = (VkMemoryRequirements) {
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
nvk_GetPhysicalDeviceExternalBufferProperties(
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
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      pExternalBufferProperties->externalMemoryProperties =
         nvk_dma_buf_mem_props;
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
      (VkExternalMemoryProperties) {
         .compatibleHandleTypes = pExternalBufferInfo->handleType,
      };
}

static VkResult
nvk_bind_buffer_memory(struct nvk_device *dev,
                       const VkBindBufferMemoryInfo *info)
{
   VK_FROM_HANDLE(nvk_device_memory, mem, info->memory);
   VK_FROM_HANDLE(nvk_buffer, buffer, info->buffer);
   struct nvk_physical_device *pdev = nvk_device_physical(dev);
   VkResult result = VK_SUCCESS;

   if ((pdev->debug_flags & NVK_DEBUG_PUSH_DUMP) &&
       (buffer->vk.usage & (VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT_KHR |
                            VK_BUFFER_USAGE_2_PREPROCESS_BUFFER_BIT_EXT)))
      nvkmd_dev_track_mem(dev->nvkmd, mem->mem);

   if (buffer->va != NULL) {
      result = nvkmd_va_bind_mem(buffer->va, &buffer->vk.base,
                                 0 /* va_offset */,
                                 mem->mem, info->memoryOffset,
                                 buffer->va->size_B);
   } else {
      buffer->addr = mem->mem->va->addr + info->memoryOffset;
   }

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_BindBufferMemory2(VkDevice device,
                      uint32_t bindInfoCount,
                      const VkBindBufferMemoryInfo *pBindInfos)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   VkResult first_error_or_success = VK_SUCCESS;

   for (uint32_t i = 0; i < bindInfoCount; ++i) {
      VkResult result = nvk_bind_buffer_memory(dev, &pBindInfos[i]);

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
nvk_GetBufferDeviceAddress(UNUSED VkDevice device,
                           const VkBufferDeviceAddressInfo *pInfo)
{
   VK_FROM_HANDLE(nvk_buffer, buffer, pInfo->buffer);

   return nvk_buffer_address(buffer, 0);
}

VKAPI_ATTR uint64_t VKAPI_CALL
nvk_GetBufferOpaqueCaptureAddress(UNUSED VkDevice device,
                                  const VkBufferDeviceAddressInfo *pInfo)
{
   VK_FROM_HANDLE(nvk_buffer, buffer, pInfo->buffer);

   return nvk_buffer_address(buffer, 0);
}

VkResult
nvk_queue_buffer_bind(struct nvk_queue *queue,
                      const VkSparseBufferMemoryBindInfo *bind_info)
{
   VK_FROM_HANDLE(nvk_buffer, buffer, bind_info->buffer);
   VkResult result;

   const uint32_t bind_count = bind_info->bindCount;
   if (bind_count == 0)
      return VK_SUCCESS;

   STACK_ARRAY(struct nvkmd_ctx_bind, binds, bind_count);

   for (unsigned i = 0; i < bind_count; i++) {
      const VkSparseMemoryBind *bind = &bind_info->pBinds[i];
      VK_FROM_HANDLE(nvk_device_memory, mem, bind->memory);

      binds[i] = (struct nvkmd_ctx_bind) {
         .op = mem ? NVKMD_BIND_OP_BIND : NVKMD_BIND_OP_UNBIND,
         .va = buffer->va,
         .va_offset_B = bind->resourceOffset,
         .mem = mem ? mem->mem : NULL,
         .mem_offset_B = mem ? bind->memoryOffset : 0,
         .range_B = bind->size,
      };
   }

   result = nvkmd_ctx_bind(queue->bind_ctx, &queue->vk.base,
                           bind_count, binds);

   STACK_ARRAY_FINISH(binds);

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_GetBufferOpaqueCaptureDescriptorDataEXT(
    VkDevice device,
    const VkBufferCaptureDescriptorDataInfoEXT *pInfo,
    void *pData)
{
   return VK_SUCCESS;
}
