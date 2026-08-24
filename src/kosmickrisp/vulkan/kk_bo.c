/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_bo.h"

#include "kk_device.h"
#include "kk_physical_device.h"

#include "kosmickrisp/bridge/mtl_bridge.h"

#include "util/u_memory.h"

#if DETECT_OS_APPLE
#include <mach/mach_init.h>
#include <mach/mach_vm.h>
#endif /* DETECT_OS_APPLE */

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

#if DETECT_ARCH_X86_64
   /* KK_WORKAROUND_8 */
   struct kk_physical_device *pdev = kk_device_physical(dev);
   if (!(pdev->settings.disabled_workarounds & BITFIELD64_BIT(8))) {
      minimum_alignment = MAX2(minimum_alignment, 16384);
   }
#endif

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
   /* We may only have a mapped buffer, for example if the memory was imported
    * from a host pointer */
   if (bo->mtl_handle)
      kk_device_remove_heap_from_residency_set(dev, bo->mtl_handle);
   else
      kk_device_remove_buffer_from_residency_set(dev, bo->map);

   mtl_release(bo->map);

   if (bo->mtl_handle)
      mtl_release(bo->mtl_handle);

   FREE(bo);
}

VkResult
kk_bo_map_placed(struct kk_device *dev, struct kk_bo *bo, void **addr)
{
#if DETECT_OS_APPLE
   mach_vm_address_t src_addr = (mach_vm_address_t)bo->cpu;
   mach_vm_address_t dst_addr = (mach_vm_address_t)*addr;

   int flags = VM_FLAGS_ANYWHERE;
   if (dst_addr)
      flags = VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE;

   vm_prot_t cur_prot;
   vm_prot_t max_prot;

   /* Metal provides us with its own CPU mapping of the memory. mach_vm_remap
    * allows us to create a second mapping pointing to that same memory, placed
    * wherever the caller requests. Note that this is used even without a fixed
    * address, since we need a unique mapping to support reserved unmap. */
   kern_return_t ret = mach_vm_remap(mach_task_self(), &dst_addr, bo->size_B, 0,
                                     flags, mach_task_self(), src_addr, false,
                                     &cur_prot, &max_prot, VM_INHERIT_DEFAULT);
   if (ret != KERN_SUCCESS)
      return vk_errorf(dev, VK_ERROR_MEMORY_MAP_FAILED,
                       "mach_vm_remap failed: %d", ret);

   *addr = (void *)dst_addr;
#endif /* DETECT_OS_APPLE */

   return VK_SUCCESS;
}

VkResult
kk_bo_unmap(struct kk_device *dev, struct kk_bo *bo, void *addr, bool reserved)
{
#if DETECT_OS_APPLE
   mach_vm_address_t unmap_addr = (mach_vm_address_t)addr;
   if (reserved) {
      /* Replace with a reserved mapping using mach_vm_map */
      kern_return_t ret = mach_vm_map(
         mach_task_self(), &unmap_addr, bo->size_B, 0,
         VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, MEMORY_OBJECT_NULL, 0, false,
         VM_PROT_NONE, VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_DEFAULT);
      if (ret != KERN_SUCCESS)
         return vk_errorf(dev, VK_ERROR_MEMORY_MAP_FAILED,
                          "mach_vm_map failed: %d", ret);
   } else {
      /* According to spec, we can only return a fail code here if the reserve
       * flag is set, so ignore the result */
      mach_vm_deallocate(mach_task_self(), unmap_addr, bo->size_B);
   }
#endif /* DETECT_OS_APPLE */

   return VK_SUCCESS;
}
