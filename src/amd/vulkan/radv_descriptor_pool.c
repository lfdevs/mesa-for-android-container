/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#include "radv_descriptor_pool.h"
#include "radv_buffer.h"
#include "radv_descriptor_set.h"
#include "radv_descriptors.h"
#include "radv_device.h"
#include "radv_entrypoints.h"
#include "radv_physical_device.h"
#include "radv_rmv.h"

#include "vk_log.h"

static void
radv_destroy_descriptor_pool_entries(struct radv_device *device, struct radv_descriptor_pool *pool)
{
   if (!pool->host_memory_base) {
      list_for_each_entry_safe (struct radv_descriptor_set, set, &pool->sets, link) {
         radv_descriptor_set_destroy(device, pool, set);
      }
   } else {
      list_for_each_entry_safe (struct radv_descriptor_set, set, &pool->sets, link) {
         list_del(&set->link);
         vk_descriptor_set_layout_unref(&device->vk, &set->header.layout->vk);
         vk_object_base_finish(&set->header.base);
      }
   }
}

static void
radv_destroy_descriptor_pool(struct radv_device *device, const VkAllocationCallbacks *pAllocator,
                             struct radv_descriptor_pool *pool)
{
   radv_destroy_descriptor_pool_entries(device, pool);

   if (!pool->host_memory_base && pool->size)
      util_vma_heap_finish(&pool->bo_heap);
   if (pool->bo)
      radv_bo_destroy(device, &pool->base, pool->bo);
   if (pool->host_bo)
      vk_free2(&device->vk.alloc, pAllocator, pool->host_bo);

   radv_rmv_log_resource_destroy(device, (uint64_t)radv_descriptor_pool_to_handle(pool));
   vk_object_base_finish(&pool->base);
   vk_free2(&device->vk.alloc, pAllocator, pool);
}

static VkResult
radv_create_descriptor_pool(struct radv_device *device, const VkDescriptorPoolCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator, VkDescriptorPool *pDescriptorPool)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_descriptor_pool *pool;
   uint64_t size = sizeof(struct radv_descriptor_pool);
   uint64_t bo_size = 0, bo_count = 0, range_count = 0;
   VkResult result = VK_SUCCESS;

   const VkMutableDescriptorTypeCreateInfoEXT *mutable_info =
      vk_find_struct_const(pCreateInfo->pNext, MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT);

   vk_foreach_struct_const (ext, pCreateInfo->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_INLINE_UNIFORM_BLOCK_CREATE_INFO: {
         const VkDescriptorPoolInlineUniformBlockCreateInfo *info =
            (const VkDescriptorPoolInlineUniformBlockCreateInfo *)ext;
         /* the sizes are 4 aligned, and we need to align to at
          * most 32, which needs at most 28 bytes extra per
          * binding. */
         bo_size += 28llu * info->maxInlineUniformBlockBindings;
         break;
      }
      default:
         break;
      }
   }

   uint64_t num_16byte_descriptors = 0;
   for (unsigned i = 0; i < pCreateInfo->poolSizeCount; ++i) {
      bo_count += radv_descriptor_type_buffer_count(pCreateInfo->pPoolSizes[i].type) *
                  pCreateInfo->pPoolSizes[i].descriptorCount;

      switch (pCreateInfo->pPoolSizes[i].type) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         range_count += pCreateInfo->pPoolSizes[i].descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
         bo_size += RADV_BUFFER_DESC_SIZE * pCreateInfo->pPoolSizes[i].descriptorCount;
         num_16byte_descriptors += pCreateInfo->pPoolSizes[i].descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         bo_size += RADV_STORAGE_IMAGE_DESC_SIZE * pCreateInfo->pPoolSizes[i].descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         bo_size += radv_get_sampled_image_desc_size(pdev) * pCreateInfo->pPoolSizes[i].descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_MUTABLE_EXT:
         /* Per spec, if a mutable descriptor type list is provided for the pool entry, we
          * allocate enough memory to hold any subset of that list.
          * If there is no mutable descriptor type list available,
          * we must allocate enough for any supported mutable descriptor type, i.e. 64 bytes if
          * FMASK is used.
          */
         if (mutable_info && i < mutable_info->mutableDescriptorTypeListCount) {
            uint64_t mutable_size, mutable_alignment;
            if (radv_mutable_descriptor_type_size_alignment(device, &mutable_info->pMutableDescriptorTypeLists[i],
                                                            &mutable_size, &mutable_alignment)) {
               /* 32 as we may need to align for images */
               mutable_size = align(mutable_size, 32);
               bo_size += mutable_size * pCreateInfo->pPoolSizes[i].descriptorCount;
               if (mutable_size < 32)
                  num_16byte_descriptors += pCreateInfo->pPoolSizes[i].descriptorCount;
            }
         } else {
            const uint32_t max_desc_size = pdev->use_fmask ? 64 : 32;
            bo_size += max_desc_size * pCreateInfo->pPoolSizes[i].descriptorCount;
         }
         break;
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         bo_size += pCreateInfo->pPoolSizes[i].descriptorCount * radv_get_combined_image_sampler_desc_size(pdev);
         break;
      case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
         bo_size += pCreateInfo->pPoolSizes[i].descriptorCount;
         break;
      default:
         break;
      }
   }

   if (num_16byte_descriptors) {
      /* Reserve space to align before image descriptors. Our layout code ensures at most one gap
       * per set. */
      bo_size += 16 * MIN2(num_16byte_descriptors, pCreateInfo->maxSets);
   }

   if (!(pCreateInfo->flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)) {
      size += pCreateInfo->maxSets * sizeof(struct radv_descriptor_set);
      size += sizeof(struct radeon_winsys_bo *) * bo_count;
      size += sizeof(struct radv_descriptor_range) * range_count;
   }

   pool = vk_zalloc2(&device->vk.alloc, pAllocator, size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pool)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &pool->base, VK_OBJECT_TYPE_DESCRIPTOR_POOL);

   list_inithead(&pool->sets);

   if (!(pCreateInfo->flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)) {
      pool->host_memory_base = (uint8_t *)pool + sizeof(struct radv_descriptor_pool);
      pool->host_memory_ptr = pool->host_memory_base;
      pool->host_memory_end = (uint8_t *)pool + size;
   } else {
      if (bo_size) {
         util_vma_heap_init(&pool->bo_heap, RADV_POOL_HEAP_OFFSET, bo_size + RADV_POOL_HEAP_OFFSET);
         pool->bo_heap.alloc_high = false;
      }
   }

   if (bo_size) {
      if (!(pCreateInfo->flags & VK_DESCRIPTOR_POOL_CREATE_HOST_ONLY_BIT_EXT)) {
         enum radeon_bo_flag flags = RADEON_FLAG_NO_INTERPROCESS_SHARING | RADEON_FLAG_READ_ONLY | RADEON_FLAG_32BIT;

         if (radv_device_should_clear_vram(device))
            flags |= RADEON_FLAG_ZERO_VRAM;

         result = radv_bo_create(device, &pool->base, bo_size, 32, RADEON_DOMAIN_VRAM, flags,
                                 RADV_BO_PRIORITY_DESCRIPTOR, 0, false, &pool->bo);
         if (result != VK_SUCCESS)
            goto fail;

         pool->mapped_ptr = (uint8_t *)radv_buffer_map(device->ws, pool->bo);
         if (!pool->mapped_ptr) {
            result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
            goto fail;
         }
      } else {
         pool->host_bo = vk_alloc2(&device->vk.alloc, pAllocator, bo_size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
         if (!pool->host_bo) {
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto fail;
         }
         pool->mapped_ptr = pool->host_bo;
      }
   }
   pool->size = bo_size;
   pool->max_entry_count = pCreateInfo->maxSets;

   *pDescriptorPool = radv_descriptor_pool_to_handle(pool);
   radv_rmv_log_descriptor_pool_create(device, pCreateInfo, *pDescriptorPool);

   return result;

fail:
   radv_destroy_descriptor_pool(device, pAllocator, pool);
   return vk_error(device, result);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateDescriptorPool(VkDevice _device, const VkDescriptorPoolCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator, VkDescriptorPool *pDescriptorPool)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   return radv_create_descriptor_pool(device, pCreateInfo, pAllocator, pDescriptorPool);
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyDescriptorPool(VkDevice _device, VkDescriptorPool _pool, const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(radv_descriptor_pool, pool, _pool);

   if (!pool)
      return;

   radv_destroy_descriptor_pool(device, pAllocator, pool);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_ResetDescriptorPool(VkDevice _device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(radv_descriptor_pool, pool, descriptorPool);

   radv_destroy_descriptor_pool_entries(device, pool);

   if (!pool->host_memory_base && pool->size) {
      util_vma_heap_finish(&pool->bo_heap);
      util_vma_heap_init(&pool->bo_heap, RADV_POOL_HEAP_OFFSET, pool->size + RADV_POOL_HEAP_OFFSET);
      pool->bo_heap.alloc_high = false;
   }

   pool->entry_count = 0;
   pool->current_offset = 0;
   pool->host_memory_ptr = pool->host_memory_base;

   return VK_SUCCESS;
}
