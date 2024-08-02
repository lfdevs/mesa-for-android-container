/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_device_memory.h"

#include "nvk_device.h"
#include "nvk_entrypoints.h"
#include "nvk_image.h"
#include "nvk_physical_device.h"
#include "nvkmd/nvkmd.h"

#include "util/u_atomic.h"

#include <inttypes.h>
#include <sys/mman.h>

/* Supports opaque fd only */
const VkExternalMemoryProperties nvk_opaque_fd_mem_props = {
   .externalMemoryFeatures =
      VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
      VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT,
   .exportFromImportedHandleTypes =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
   .compatibleHandleTypes =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
};

/* Supports opaque fd and dma_buf. */
const VkExternalMemoryProperties nvk_dma_buf_mem_props = {
   .externalMemoryFeatures =
      VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
      VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT,
   .exportFromImportedHandleTypes =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   .compatibleHandleTypes =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
};

static enum nvkmd_mem_flags
nvk_memory_type_flags(const VkMemoryType *type,
                      VkExternalMemoryHandleTypeFlagBits handle_types)
{
   enum nvkmd_mem_flags flags = 0;
   if (type->propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
      flags = NVKMD_MEM_LOCAL;
   else
      flags = NVKMD_MEM_GART;

   if (type->propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
      flags |= NVKMD_MEM_CAN_MAP;

   if (handle_types != 0)
      flags |= NVKMD_MEM_SHARED;

   return flags;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_GetMemoryFdPropertiesKHR(VkDevice device,
                             VkExternalMemoryHandleTypeFlagBits handleType,
                             int fd,
                             VkMemoryFdPropertiesKHR *pMemoryFdProperties)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   struct nvk_physical_device *pdev = nvk_device_physical(dev);
   struct nvkmd_mem *mem;
   VkResult result;

   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      result = nvkmd_dev_import_dma_buf(dev->nvkmd, &dev->vk.base, fd, &mem);
      if (result != VK_SUCCESS)
         return result;
      break;
   default:
      return vk_error(dev, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   }

   uint32_t type_bits = 0;
   if (handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT) {
      /* We allow a dma-buf to be imported anywhere because there's no way
       * for us to actually know where it came from.
       */
      type_bits = BITFIELD_MASK(pdev->mem_type_count);
   } else {
      for (unsigned t = 0; t < ARRAY_SIZE(pdev->mem_types); t++) {
         const enum nvkmd_mem_flags flags =
            nvk_memory_type_flags(&pdev->mem_types[t], handleType);
         if (!(flags & ~mem->flags))
            type_bits |= (1 << t);
      }
   }

   pMemoryFdProperties->memoryTypeBits = type_bits;

   nvkmd_mem_unref(mem);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_AllocateMemory(VkDevice device,
                   const VkMemoryAllocateInfo *pAllocateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkDeviceMemory *pMem)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   struct nvk_physical_device *pdev = nvk_device_physical(dev);
   struct nvk_device_memory *mem;
   VkResult result = VK_SUCCESS;

   const VkImportMemoryFdInfoKHR *fd_info =
      vk_find_struct_const(pAllocateInfo->pNext, IMPORT_MEMORY_FD_INFO_KHR);
   const VkExportMemoryAllocateInfo *export_info =
      vk_find_struct_const(pAllocateInfo->pNext, EXPORT_MEMORY_ALLOCATE_INFO);
   const VkMemoryDedicatedAllocateInfo *dedicated_info =
      vk_find_struct_const(pAllocateInfo->pNext, MEMORY_DEDICATED_ALLOCATE_INFO);
   const VkMemoryType *type =
      &pdev->mem_types[pAllocateInfo->memoryTypeIndex];

   VkExternalMemoryHandleTypeFlagBits handle_types = 0;
   if (export_info != NULL)
      handle_types |= export_info->handleTypes;
   if (fd_info != NULL)
      handle_types |= fd_info->handleType;

   const enum nvkmd_mem_flags flags = nvk_memory_type_flags(type, handle_types);

   uint32_t alignment = (1ULL << 12);
   if (flags & NVKMD_MEM_LOCAL)
      alignment = (1ULL << 16);

   uint8_t pte_kind = 0, tile_mode = 0;
   if (dedicated_info != NULL) {
      VK_FROM_HANDLE(nvk_image, image, dedicated_info->image);
      if (image != NULL &&
          image->vk.tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
         /* This image might be shared with GL so we need to set the BO flags
          * such that GL can bind and use it.
          */
         assert(image->plane_count == 1);
         alignment = MAX2(alignment, image->planes[0].nil.align_B);
         pte_kind = image->planes[0].nil.pte_kind;
         tile_mode = image->planes[0].nil.tile_mode;
      }
   }

   const uint64_t aligned_size =
      align64(pAllocateInfo->allocationSize, alignment);

   mem = vk_device_memory_create(&dev->vk, pAllocateInfo,
                                 pAllocator, sizeof(*mem));
   if (!mem)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (fd_info && fd_info->handleType) {
      assert(fd_info->handleType ==
               VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
             fd_info->handleType ==
               VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

      result = nvkmd_dev_import_dma_buf(dev->nvkmd, &dev->vk.base,
                                        fd_info->fd, &mem->mem);
      if (result != VK_SUCCESS)
         goto fail_alloc;

      /* We can't really assert anything for dma-bufs because they could come
       * in from some other device.
       */
      if (fd_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT)
         assert(!(flags & ~mem->mem->flags));
   } else if (pte_kind != 0 || tile_mode != 0) {
      result = nvkmd_dev_alloc_tiled_mem(dev->nvkmd, &dev->vk.base,
                                         aligned_size, alignment,
                                         pte_kind, tile_mode, flags,
                                         &mem->mem);
      if (result != VK_SUCCESS)
         goto fail_alloc;
   } else {
      result = nvkmd_dev_alloc_mem(dev->nvkmd, &dev->vk.base,
                                   aligned_size, alignment, flags,
                                   &mem->mem);
      if (result != VK_SUCCESS)
         goto fail_alloc;
   }

   if (pdev->debug_flags & NVK_DEBUG_ZERO_MEMORY) {
      if (type->propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
         void *map;
         result = nvkmd_mem_map(mem->mem, &dev->vk.base,
                             NVKMD_MEM_MAP_RDWR, NULL, &map);
         if (result != VK_SUCCESS)
            goto fail_mem;

         memset(map, 0, mem->mem->size_B);
         nvkmd_mem_unmap(mem->mem);
      } else {
         result = nvk_upload_queue_fill(dev, &dev->upload,
                                        mem->mem->va->addr,
                                        0, mem->mem->size_B);
         if (result != VK_SUCCESS)
            goto fail_mem;

         /* Since we don't know when the memory will be freed, sync now */
         result = nvk_upload_queue_sync(dev, &dev->upload);
         if (result != VK_SUCCESS)
            goto fail_mem;
      }
   }

   if (fd_info && fd_info->handleType) {
      /* From the Vulkan spec:
       *
       *    "Importing memory from a file descriptor transfers ownership of
       *    the file descriptor from the application to the Vulkan
       *    implementation. The application must not perform any operations on
       *    the file descriptor after a successful import."
       *
       * If the import fails, we leave the file descriptor open.
       */
      close(fd_info->fd);
   }

   struct nvk_memory_heap *heap = &pdev->mem_heaps[type->heapIndex];
   p_atomic_add(&heap->used, mem->mem->size_B);

   *pMem = nvk_device_memory_to_handle(mem);

   return VK_SUCCESS;

fail_mem:
   nvkmd_mem_unref(mem->mem);
fail_alloc:
   vk_device_memory_destroy(&dev->vk, pAllocator, &mem->vk);
   return result;
}

VKAPI_ATTR void VKAPI_CALL
nvk_FreeMemory(VkDevice device,
               VkDeviceMemory _mem,
               const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   VK_FROM_HANDLE(nvk_device_memory, mem, _mem);
   struct nvk_physical_device *pdev = nvk_device_physical(dev);

   if (!mem)
      return;

   const VkMemoryType *type = &pdev->mem_types[mem->vk.memory_type_index];
   struct nvk_memory_heap *heap = &pdev->mem_heaps[type->heapIndex];
   p_atomic_add(&heap->used, -((int64_t)mem->mem->size_B));

   nvkmd_mem_unref(mem->mem);

   vk_device_memory_destroy(&dev->vk, pAllocator, &mem->vk);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_MapMemory2KHR(VkDevice device,
                  const VkMemoryMapInfoKHR *pMemoryMapInfo,
                  void **ppData)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   VK_FROM_HANDLE(nvk_device_memory, mem, pMemoryMapInfo->memory);
   VkResult result;

   if (mem == NULL) {
      *ppData = NULL;
      return VK_SUCCESS;
   }

   const VkDeviceSize offset = pMemoryMapInfo->offset;
   const VkDeviceSize size =
      vk_device_memory_range(&mem->vk, pMemoryMapInfo->offset,
                                       pMemoryMapInfo->size);

   enum nvkmd_mem_map_flags map_flags = NVKMD_MEM_MAP_RDWR;

   void *fixed_addr = NULL;
   if (pMemoryMapInfo->flags & VK_MEMORY_MAP_PLACED_BIT_EXT) {
      const VkMemoryMapPlacedInfoEXT *placed_info =
         vk_find_struct_const(pMemoryMapInfo->pNext, MEMORY_MAP_PLACED_INFO_EXT);
      map_flags |= NVKMD_MEM_MAP_FIXED;
      fixed_addr = placed_info->pPlacedAddress;
   }

   /* From the Vulkan spec version 1.0.32 docs for MapMemory:
    *
    *  * If size is not equal to VK_WHOLE_SIZE, size must be greater than 0
    *    assert(size != 0);
    *  * If size is not equal to VK_WHOLE_SIZE, size must be less than or
    *    equal to the size of the memory minus offset
    */
   assert(size > 0);
   assert(offset + size <= mem->mem->size_B);

   if (size != (size_t)size) {
      return vk_errorf(dev, VK_ERROR_MEMORY_MAP_FAILED,
                       "requested size 0x%"PRIx64" does not fit in %u bits",
                       size, (unsigned)(sizeof(size_t) * 8));
   }

   /* From the Vulkan 1.2.194 spec:
    *
    *    "memory must not be currently host mapped"
    */
   if (mem->mem->map != NULL) {
      return vk_errorf(dev, VK_ERROR_MEMORY_MAP_FAILED,
                       "Memory object already mapped.");
   }

   void *mem_map;
   result = nvkmd_mem_map(mem->mem, &mem->vk.base, map_flags, fixed_addr, &mem_map);
   if (result != VK_SUCCESS)
      return result;

   *ppData = mem_map + offset;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_UnmapMemory2KHR(VkDevice device,
                    const VkMemoryUnmapInfoKHR *pMemoryUnmapInfo)
{
   VK_FROM_HANDLE(nvk_device_memory, mem, pMemoryUnmapInfo->memory);

   if (mem == NULL)
      return VK_SUCCESS;

   if (pMemoryUnmapInfo->flags & VK_MEMORY_UNMAP_RESERVE_BIT_EXT) {
      return nvkmd_mem_overmap(mem->mem, &mem->vk.base);
   } else {
      nvkmd_mem_unmap(mem->mem);
      return VK_SUCCESS;
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_FlushMappedMemoryRanges(VkDevice device,
                            uint32_t memoryRangeCount,
                            const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_InvalidateMappedMemoryRanges(VkDevice device,
                                 uint32_t memoryRangeCount,
                                 const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvk_GetDeviceMemoryCommitment(VkDevice device,
                              VkDeviceMemory _mem,
                              VkDeviceSize* pCommittedMemoryInBytes)
{
   VK_FROM_HANDLE(nvk_device_memory, mem, _mem);

   *pCommittedMemoryInBytes = mem->mem->size_B;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_GetMemoryFdKHR(VkDevice device,
                   const VkMemoryGetFdInfoKHR *pGetFdInfo,
                   int *pFD)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   VK_FROM_HANDLE(nvk_device_memory, mem, pGetFdInfo->memory);

   switch (pGetFdInfo->handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      return nvkmd_mem_export_dma_buf(mem->mem, &mem->vk.base, pFD);
   default:
      assert(!"unsupported handle type");
      return vk_error(dev, VK_ERROR_FEATURE_NOT_PRESENT);
   }
}

VKAPI_ATTR uint64_t VKAPI_CALL
nvk_GetDeviceMemoryOpaqueCaptureAddress(
   UNUSED VkDevice device,
   const VkDeviceMemoryOpaqueCaptureAddressInfo* pInfo)
{
   VK_FROM_HANDLE(nvk_device_memory, mem, pInfo->memory);

   return mem->mem->va->addr;
}
