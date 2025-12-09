/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_bo.h"

#include "kk_device.h"

#include "kosmickrisp/bridge/mtl_bridge.h"

#include "util/u_memory.h"

VkResult
kk_alloc_bo(struct kk_device *dev, struct vk_object_base *log_obj,
            uint64_t size_B, uint64_t align_B, struct kk_bo **bo_out)
{
   VkResult result = VK_SUCCESS;

   // TODO_KOSMICKRISP: Probably requires handling the buffer maximum 256MB
   uint64_t minimum_alignment = 0u;
   mtl_heap_buffer_size_and_align_with_length(dev->mtl_handle, &size_B,
                                              &minimum_alignment);
   minimum_alignment = MAX2(minimum_alignment, align_B);
   size_B = align64(size_B, minimum_alignment);
   mtl_heap *handle =
      mtl_new_heap(dev->mtl_handle, size_B, KK_MTL_RESOURCE_OPTIONS);
   if (handle == NULL) {
      result = vk_errorf(log_obj, VK_ERROR_OUT_OF_DEVICE_MEMORY, "%m");
      goto fail_heap;
   }

   mtl_buffer *map = mtl_new_buffer_with_length(handle, size_B, 0u);
   if (map == NULL) {
      result = vk_errorf(log_obj, VK_ERROR_OUT_OF_DEVICE_MEMORY, "%m");
      goto fail_map;
   }

   struct kk_bo *bo = CALLOC_STRUCT(kk_bo);

   if (bo == NULL) {
      result = vk_errorf(log_obj, VK_ERROR_OUT_OF_HOST_MEMORY, "%m");
      goto fail_alloc;
   }

   bo->mtl_handle = handle;
   bo->size_B = size_B;
   bo->map = map;
   bo->gpu = mtl_buffer_get_gpu_address(map);
   bo->cpu = mtl_get_contents(map);

   kk_device_add_heap_to_residency_set(dev, handle);

   *bo_out = bo;
   return result;

fail_alloc:
   mtl_release(map);
fail_map:
   mtl_release(handle);
fail_heap:
   return result;
}

void
kk_destroy_bo(struct kk_device *dev, struct kk_bo *bo)
{
   kk_device_remove_heap_from_residency_set(dev, bo->mtl_handle);
   mtl_release(bo->map);
   mtl_release(bo->mtl_handle);
   FREE(bo);
}
