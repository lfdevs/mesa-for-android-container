/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_buffer.h"
#include "panvk_device.h"
#include "panvk_device_memory.h"
#include "panvk_entrypoints.h"
#include "panvk_sparse.h"

#include "pan_props.h"

#include "vk_log.h"

VKAPI_ATTR uint64_t VKAPI_CALL
panvk_GetBufferOpaqueCaptureAddress(VkDevice _device,
                                    const VkBufferDeviceAddressInfo *pInfo)
{
   VK_FROM_HANDLE(panvk_buffer, buffer, pInfo->buffer);

   return buffer->vk.device_address;
}

static uint64_t
panvk_buffer_get_sparse_size(const struct panvk_buffer *buffer)
{
   struct panvk_device *device = to_panvk_device(buffer->vk.base.device);
   uint64_t buffer_size = buffer->vk.size;
   uint64_t page_size = panvk_get_gpu_page_size(device);
   return ALIGN_POT(buffer_size, page_size);
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetDeviceBufferMemoryRequirements(VkDevice _device,
                                        const VkDeviceBufferMemoryRequirements *pInfo,
                                        VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(panvk_device, device, _device);

   /* For sparse resources alignment specifies binding granularity, rather than
    * the alignment requirement. It's up to us to satisfy the alignment
    * requirement when allocating the VA range.
    */
   const uint64_t align =
      pInfo->pCreateInfo->flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT
         ? panvk_get_gpu_page_size(device)
         : 64;
   const uint64_t size = align64(pInfo->pCreateInfo->size, align);

   pMemoryRequirements->memoryRequirements.memoryTypeBits = 1;
   pMemoryRequirements->memoryRequirements.alignment = align;
   pMemoryRequirements->memoryRequirements.size = size;

   vk_foreach_struct_const(ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *dedicated = (void *)ext;
         dedicated->requiresDedicatedAllocation = false;
         dedicated->prefersDedicatedAllocation = dedicated->requiresDedicatedAllocation;
         break;
      }
      default:
         vk_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_BindBufferMemory2(VkDevice _device, uint32_t bindInfoCount,
                        const VkBindBufferMemoryInfo *pBindInfos)
{
   for (uint32_t i = 0; i < bindInfoCount; i++) {
      VK_FROM_HANDLE(panvk_device_memory, mem, pBindInfos[i].memory);
      VK_FROM_HANDLE(panvk_buffer, buffer, pBindInfos[i].buffer);
      const VkBindMemoryStatus *bind_status =
         vk_find_struct_const(&pBindInfos[i], BIND_MEMORY_STATUS);

      assert(!(buffer->vk.create_flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT));
      assert(buffer->vk.device_address == 0);
      assert(mem != NULL);

      if (bind_status)
         *bind_status->pResult = VK_SUCCESS;

      buffer->vk.device_address = mem->addr.dev + pBindInfos[i].memoryOffset;
   }
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_CreateBuffer(VkDevice _device, const VkBufferCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   struct panvk_buffer *buffer;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);

   buffer =
      vk_buffer_create(&device->vk, pCreateInfo, pAllocator, sizeof(*buffer));
   if (buffer == NULL)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (buffer->vk.size > PANVK_MAX_BUFFER_SIZE) {
      result = panvk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      goto err_destroy_buffer;
   }

   if (buffer->vk.create_flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT) {
      uint64_t va_range = panvk_buffer_get_sparse_size(buffer);

      buffer->vk.device_address = panvk_as_alloc(device, va_range,
         pan_choose_gpu_va_alignment(device->kmod.vm, va_range));
      if (!buffer->vk.device_address) {
         result = panvk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         goto err_destroy_buffer;
      }

      if ((buffer->vk.create_flags & VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT) ||
          PANVK_DEBUG(FORCE_BLACKHOLE)) {
         /* Map last so that we don't have a possibility of getting any more
          * errors, in which case we'd have to unmap.
          */
         result = panvk_map_to_blackhole(device, buffer->vk.device_address,
                                         va_range);
         if (result != VK_SUCCESS) {
            result = panvk_error(device, result);
            goto err_free_va;
         }
      }
   }

   *pBuffer = panvk_buffer_to_handle(buffer);

   return VK_SUCCESS;

err_free_va:
   if (buffer->vk.create_flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT)
      panvk_as_free(device, buffer->vk.device_address, panvk_buffer_get_sparse_size(buffer));

err_destroy_buffer:
   vk_buffer_destroy(&device->vk, pAllocator, &buffer->vk);
   return result;
}

VKAPI_ATTR void VKAPI_CALL
panvk_DestroyBuffer(VkDevice _device, VkBuffer _buffer,
                    const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_buffer, buffer, _buffer);

   if (!buffer)
      return;

   if (buffer->vk.create_flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT) {
      uint64_t va_range = panvk_buffer_get_sparse_size(buffer);

      struct pan_kmod_vm_op unmap = {
         .type = PAN_KMOD_VM_OP_TYPE_UNMAP,
         .va = {
            .start = buffer->vk.device_address,
            .size = va_range,
         },
      };
      ASSERTED int ret = pan_kmod_vm_bind(
         device->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE, &unmap, 1);
      assert(!ret);

      panvk_as_free(device, buffer->vk.device_address, va_range);
   }

   vk_buffer_destroy(&device->vk, pAllocator, &buffer->vk);
}
