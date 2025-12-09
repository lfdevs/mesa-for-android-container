/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#include "tu_buffer.h"

#include "vk_android.h"
#include "vk_debug_utils.h"

#include "tu_device.h"
#include "tu_rmv.h"

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateBuffer(VkDevice _device,
                const VkBufferCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator,
                VkBuffer *pBuffer)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   struct tu_buffer *buffer;

   buffer = (struct tu_buffer *) vk_buffer_create(
      &device->vk, pCreateInfo, pAllocator, sizeof(*buffer));
   if (buffer == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (pCreateInfo->flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT) {
      struct tu_instance *instance = device->physical_device->instance;
      BITMASK_ENUM(tu_sparse_vma_flags) flags = 0;
      uint64_t client_address = 0;

      if (pCreateInfo->flags & VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT)
         flags |= TU_SPARSE_VMA_MAP_ZERO;
      if (pCreateInfo->flags & VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT)
         flags |= TU_SPARSE_VMA_REPLAYABLE;

      const VkBufferOpaqueCaptureAddressCreateInfo *replay_info =
         vk_find_struct_const(pCreateInfo->pNext,
                              BUFFER_OPAQUE_CAPTURE_ADDRESS_CREATE_INFO);
      if (replay_info && replay_info->opaqueCaptureAddress) {
         client_address = replay_info->opaqueCaptureAddress;
         flags |= TU_SPARSE_VMA_REPLAYABLE;
      }

      VkResult result =
         tu_sparse_vma_init(device, &buffer->vk.base, &buffer->vma,
                            &buffer->vk.device_address, flags,
                            pCreateInfo->size, client_address);

      if (result != VK_SUCCESS) {
         vk_buffer_destroy(&device->vk, pAllocator, &buffer->vk);
         return result;
      }

      vk_address_binding_report(&instance->vk, &buffer->vk.base,
                                buffer->vk.device_address, buffer->vk.size,
                                VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT);
   }

   TU_RMV(buffer_create, device, buffer);

#ifdef HAVE_PERFETTO
   tu_perfetto_log_create_buffer(device, buffer);
#endif

   *pBuffer = tu_buffer_to_handle(buffer);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
tu_DestroyBuffer(VkDevice _device,
                 VkBuffer _buffer,
                 const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VK_FROM_HANDLE(tu_buffer, buffer, _buffer);
   struct tu_instance *instance = device->physical_device->instance;

   if (!buffer)
      return;

   TU_RMV(buffer_destroy, device, buffer);

#ifdef HAVE_PERFETTO
   tu_perfetto_log_destroy_buffer(device, buffer);
#endif

   if (buffer->vk.create_flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT) {
      vk_address_binding_report(&instance->vk, &buffer->vk.base,
                                buffer->vk.device_address, buffer->vk.size,
                                VK_DEVICE_ADDRESS_BINDING_TYPE_UNBIND_EXT);
      tu_sparse_vma_finish(device, &buffer->vma);
   } else if (buffer->vk.device_address) {
      vk_address_binding_report(&instance->vk, &buffer->vk.base,
                                buffer->vk.device_address, buffer->bo_size,
                                VK_DEVICE_ADDRESS_BINDING_TYPE_UNBIND_EXT);
   }


   vk_buffer_destroy(&device->vk, pAllocator, &buffer->vk);
}

VKAPI_ATTR void VKAPI_CALL
tu_GetDeviceBufferMemoryRequirements(
   VkDevice _device,
   const VkDeviceBufferMemoryRequirements *pInfo,
   VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(tu_device, device, _device);

   uint64_t size = pInfo->pCreateInfo->size;
   uint32_t alignment =
      (pInfo->pCreateInfo->flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT) ?
      os_page_size : 64;
   pMemoryRequirements->memoryRequirements = (VkMemoryRequirements) {
      .size = MAX2(align64(size, alignment), size),
      .alignment = alignment,
      .memoryTypeBits = (1 << device->physical_device->memory.non_lazy_type_count) - 1,
   };

   vk_foreach_struct(ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *req =
            (VkMemoryDedicatedRequirements *) ext;
         req->requiresDedicatedAllocation = false;
         req->prefersDedicatedAllocation = req->requiresDedicatedAllocation;
         break;
      }
      default:
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
tu_GetPhysicalDeviceExternalBufferProperties(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
   VkExternalBufferProperties *pExternalBufferProperties)
{
   BITMASK_ENUM(VkExternalMemoryFeatureFlagBits) flags = 0;
   VkExternalMemoryHandleTypeFlags export_flags = 0;
   VkExternalMemoryHandleTypeFlags compat_flags = 0;
   switch (pExternalBufferInfo->handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      flags = VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
              VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
      compat_flags = export_flags =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
      break;
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT:
      flags = VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
      compat_flags = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
      break;
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID:
      vk_android_get_ahb_buffer_properties(
         physicalDevice, pExternalBufferInfo, pExternalBufferProperties);
      return;
   default:
      break;
   }
   pExternalBufferProperties->externalMemoryProperties =
      (VkExternalMemoryProperties) {
         .externalMemoryFeatures = flags,
         .exportFromImportedHandleTypes = export_flags,
         .compatibleHandleTypes = compat_flags,
      };
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_BindBufferMemory2(VkDevice device,
                     uint32_t bindInfoCount,
                     const VkBindBufferMemoryInfo *pBindInfos)
{
   VK_FROM_HANDLE(tu_device, dev, device);
   struct tu_instance *instance = dev->physical_device->instance;

   for (uint32_t i = 0; i < bindInfoCount; ++i) {
      VK_FROM_HANDLE(tu_device_memory, mem, pBindInfos[i].memory);
      VK_FROM_HANDLE(tu_buffer, buffer, pBindInfos[i].buffer);

      const VkBindMemoryStatusKHR *status =
         vk_find_struct_const(pBindInfos[i].pNext, BIND_MEMORY_STATUS_KHR);
      if (status)
         *status->pResult = VK_SUCCESS;

      if (mem) {
         buffer->bo = mem->bo;
         buffer->vk.device_address = mem->bo->iova + pBindInfos[i].memoryOffset;
         if (buffer->vk.usage &
             (VK_BUFFER_USAGE_2_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT |
              VK_BUFFER_USAGE_2_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT))
            tu_bo_allow_dump(dev, mem->bo);
#ifdef HAVE_PERFETTO
         tu_perfetto_log_bind_buffer(dev, buffer);
#endif
         buffer->bo_size = mem->bo->size;
      } else {
         buffer->bo = NULL;
      }

      TU_RMV(buffer_bind, dev, buffer);

      vk_address_binding_report(&instance->vk, &buffer->vk.base,
                                buffer->bo->iova, buffer->bo->size,
                                VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT);
   }
   return VK_SUCCESS;
}

uint64_t tu_GetBufferOpaqueCaptureAddress(
   VkDevice _device,
   const VkBufferDeviceAddressInfo* pInfo)
{
   VK_FROM_HANDLE(tu_buffer, buffer, pInfo->buffer);

   /* Sparse buffers have their own iova allocation, but all others do not so
    * we only care about sparse buffers.
    */
   if (buffer->vk.create_flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT)
      return buffer->vk.device_address;

   return 0;
}
