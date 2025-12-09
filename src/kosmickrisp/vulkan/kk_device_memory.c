/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_device_memory.h"

#include "kk_device.h"
#include "kk_entrypoints.h"
#include "kk_physical_device.h"

#include "kosmickrisp/bridge/mtl_bridge.h"

#include "vulkan/vulkan_metal.h"

#include "util/u_atomic.h"
#include "util/u_memory.h"

#include <inttypes.h>
#include <sys/mman.h>

/* Supports mtlheap only */
const VkExternalMemoryProperties kk_mtlheap_mem_props = {
   .externalMemoryFeatures = VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
                             VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT,
   .exportFromImportedHandleTypes =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT,
   .compatibleHandleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT,
};

#ifdef VK_USE_PLATFORM_METAL_EXT
VKAPI_ATTR VkResult VKAPI_CALL
kk_GetMemoryMetalHandlePropertiesEXT(
   VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType,
   const void *pHandle,
   VkMemoryMetalHandlePropertiesEXT *pMemoryMetalHandleProperties)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   struct kk_physical_device *pdev = kk_device_physical(dev);

   /* We only support heaps since that's the backing for all our memory and
    * simplifies implementation */
   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT:
      break;
   default:
      return vk_error(dev, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   }
   pMemoryMetalHandleProperties->memoryTypeBits =
      BITFIELD_MASK(pdev->mem_type_count);

   return VK_SUCCESS;
}
#endif /* VK_USE_PLATFORM_METAL_EXT */

VKAPI_ATTR VkResult VKAPI_CALL
kk_AllocateMemory(VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo,
                  const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMem)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   struct kk_physical_device *pdev = kk_device_physical(dev);
   struct kk_device_memory *mem;
   VkResult result = VK_SUCCESS;
   const VkImportMemoryMetalHandleInfoEXT *metal_info = vk_find_struct_const(
      pAllocateInfo->pNext, IMPORT_MEMORY_METAL_HANDLE_INFO_EXT);
   const VkMemoryType *type = &pdev->mem_types[pAllocateInfo->memoryTypeIndex];

   // TODO_KOSMICKRISP Do the actual memory allocation with alignment requirements
   uint32_t alignment = (1ULL << 12);

   const uint64_t aligned_size =
      align64(pAllocateInfo->allocationSize, alignment);

   mem = vk_device_memory_create(&dev->vk, pAllocateInfo, pAllocator,
                                 sizeof(*mem));
   if (!mem)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (metal_info && metal_info->handleType) {
      /* We only support heaps since that's the backing for all our memory and
       * simplifies implementation */
      assert(metal_info->handleType ==
             VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT);
      mem->bo = CALLOC_STRUCT(kk_bo);
      if (!mem->bo) {
         result = vk_errorf(&dev->vk.base, VK_ERROR_OUT_OF_DEVICE_MEMORY, "%m");
         goto fail_alloc;
      }
      mem->bo->mtl_handle = mtl_retain(metal_info->handle);
      mem->bo->map =
         mtl_new_buffer_with_length(mem->bo->mtl_handle, mem->vk.size, 0u);
      mem->bo->gpu = mtl_buffer_get_gpu_address(mem->bo->map);
      mem->bo->cpu = mtl_get_contents(mem->bo->map);
      mem->bo->size_B = mtl_heap_get_size(mem->bo->mtl_handle);
      kk_device_add_heap_to_residency_set(dev, mem->bo->mtl_handle);
   } else {
      result =
         kk_alloc_bo(dev, &dev->vk.base, aligned_size, alignment, &mem->bo);
      if (result != VK_SUCCESS)
         goto fail_alloc;
   }

   struct kk_memory_heap *heap = &pdev->mem_heaps[type->heapIndex];
   p_atomic_add(&heap->used, mem->bo->size_B);

   *pMem = kk_device_memory_to_handle(mem);

   return VK_SUCCESS;

fail_alloc:
   vk_device_memory_destroy(&dev->vk, pAllocator, &mem->vk);
   return result;
}

VKAPI_ATTR void VKAPI_CALL
kk_FreeMemory(VkDevice device, VkDeviceMemory _mem,
              const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   VK_FROM_HANDLE(kk_device_memory, mem, _mem);
   struct kk_physical_device *pdev = kk_device_physical(dev);

   if (!mem)
      return;

   const VkMemoryType *type = &pdev->mem_types[mem->vk.memory_type_index];
   struct kk_memory_heap *heap = &pdev->mem_heaps[type->heapIndex];
   p_atomic_add(&heap->used, -((int64_t)mem->bo->size_B));

   kk_destroy_bo(dev, mem->bo);

   vk_device_memory_destroy(&dev->vk, pAllocator, &mem->vk);
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_MapMemory2KHR(VkDevice device, const VkMemoryMapInfoKHR *pMemoryMapInfo,
                 void **ppData)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   VK_FROM_HANDLE(kk_device_memory, mem, pMemoryMapInfo->memory);
   VkResult result = VK_SUCCESS;

   if (mem == NULL) {
      *ppData = NULL;
      return VK_SUCCESS;
   }

   const VkDeviceSize offset = pMemoryMapInfo->offset;
   const VkDeviceSize size = vk_device_memory_range(
      &mem->vk, pMemoryMapInfo->offset, pMemoryMapInfo->size);

   /* From the Vulkan spec version 1.0.32 docs for MapMemory:
    *
    *  * If size is not equal to VK_WHOLE_SIZE, size must be greater than 0
    *    assert(size != 0);
    *  * If size is not equal to VK_WHOLE_SIZE, size must be less than or
    *    equal to the size of the memory minus offset
    */
   assert(size > 0);
   assert(offset + size <= mem->bo->size_B);

   if (size != (size_t)size) {
      return vk_errorf(dev, VK_ERROR_MEMORY_MAP_FAILED,
                       "requested size 0x%" PRIx64 " does not fit in %u bits",
                       size, (unsigned)(sizeof(size_t) * 8));
   }

   /* From the Vulkan 1.2.194 spec:
    *
    *    "memory must not be currently host mapped"
    */
   if (mem->map != NULL) {
      return vk_errorf(dev, VK_ERROR_MEMORY_MAP_FAILED,
                       "Memory object already mapped.");
   }

   // TODO_KOSMICKRISP Use mmap here to so we can support VK_EXT_map_memory_placed
   mem->map = mem->bo->cpu;

   *ppData = mem->map + offset;

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_UnmapMemory2KHR(VkDevice device,
                   const VkMemoryUnmapInfoKHR *pMemoryUnmapInfo)
{
   VK_FROM_HANDLE(kk_device_memory, mem, pMemoryUnmapInfo->memory);

   if (mem == NULL)
      return VK_SUCCESS;

   // TODO_KOSMICKRISP Use unmap here to so we can support
   // VK_EXT_map_memory_placed
   mem->map = NULL;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_FlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                           const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_InvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                                const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
kk_GetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory _mem,
                             VkDeviceSize *pCommittedMemoryInBytes)
{
   VK_FROM_HANDLE(kk_device_memory, mem, _mem);

   *pCommittedMemoryInBytes = mem->bo->size_B;
}

#ifdef VK_USE_PLATFORM_METAL_EXT
VKAPI_ATTR VkResult VKAPI_CALL
kk_GetMemoryMetalHandleEXT(
   VkDevice device, const VkMemoryGetMetalHandleInfoEXT *pGetMetalHandleInfo,
   void **pHandle)
{
   /* We only support heaps since that's the backing for all our memory and
    * simplifies implementation */
   assert(pGetMetalHandleInfo->handleType ==
          VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT);
   VK_FROM_HANDLE(kk_device_memory, mem, pGetMetalHandleInfo->memory);

   /* From the Vulkan spec of vkGetMemoryMetalHandleEXT:
    *
    *    "Unless the app retains the handle object returned by the call,
    *     the lifespan will be the same as the associated VkDeviceMemory"
    */
   *pHandle = mem->bo->mtl_handle;
   return VK_SUCCESS;
}
#endif /* VK_USE_PLATFORM_METAL_EXT */

VKAPI_ATTR uint64_t VKAPI_CALL
kk_GetDeviceMemoryOpaqueCaptureAddress(
   UNUSED VkDevice device, const VkDeviceMemoryOpaqueCaptureAddressInfo *pInfo)
{
   VK_FROM_HANDLE(kk_device_memory, mem, pInfo->memory);

   return mem->bo->gpu;
}
