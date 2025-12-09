/*
 * Copyright © 2025 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_device.h"
#include "panvk_sparse.h"

#include "drm-uapi/panthor_drm.h"

static uint64_t
panvk_choose_blackhole_size(const struct pan_kmod_vm *vm, uint64_t max_size)
{
   uint64_t blackhole_size = 0;
   u_foreach_bit64(pgsize_bit, vm->pgsize_bitmap) {
      uint64_t pgsize = (uint64_t)1 << pgsize_bit;
      if (blackhole_size > 0 && pgsize > max_size)
         break;
      blackhole_size = pgsize;
   }

   return blackhole_size;
}

static void
panvk_blackhole_init(const void *_dev)
{
   struct panvk_device *dev = *(void *const *)_dev;

   uint64_t blackhole_size = panvk_choose_blackhole_size(dev->kmod.vm, 0x200000);

   dev->sparse_mem.blackhole =
      pan_kmod_bo_alloc(dev->kmod.dev, dev->kmod.vm, blackhole_size,
                        PAN_KMOD_BO_FLAG_NO_MMAP);
}

struct pan_kmod_bo *
panvk_get_blackhole(struct panvk_device *dev)
{
   util_call_once_data(&dev->sparse_mem.blackhole_once, panvk_blackhole_init, &dev);
   return dev->sparse_mem.blackhole;
}

struct panvk_vm_binder {
   uint32_t drm_fd;
   uint32_t vm_id;

   struct drm_panthor_vm_bind_op *ops;
   size_t op_count;
   size_t op_cap;
};

static int
panvk_vm_binder_flush(struct panvk_vm_binder *b)
{
   if (b->op_count == 0)
      return 0;

   struct drm_panthor_vm_bind req = {
      .vm_id = b->vm_id,
      .ops = DRM_PANTHOR_OBJ_ARRAY(b->op_count, b->ops),
   };
   int ret = pan_kmod_ioctl(b->drm_fd, DRM_IOCTL_PANTHOR_VM_BIND, &req);
   b->op_count = 0;
   return ret;
}

static int
panvk_vm_binder_bind(struct panvk_vm_binder *b, const struct drm_panthor_vm_bind_op *op)
{
   if (b->op_count == b->op_cap) {
      int ret = panvk_vm_binder_flush(b);
      if (ret)
         return ret;
   }
   assert(b->op_count < b->op_cap);
   b->ops[b->op_count++] = *op;
   return 0;
}

VkResult
panvk_map_to_blackhole(struct panvk_device *device, uint64_t address, uint64_t size)
{
   struct pan_kmod_bo *blackhole = panvk_get_blackhole(device);
   uint64_t blackhole_size = blackhole->size;

   struct drm_panthor_vm_bind_op ops[16] = {};
   struct panvk_vm_binder binder = {
      .drm_fd = device->drm_fd,
      .vm_id = device->kmod.vm->handle,

      .ops = ops,
      .op_cap = ARRAY_SIZE(ops),
   };

   uint64_t off = 0;
   while (off < size) {
      uint64_t va = address + off;
      uint64_t bo_offset = va & (blackhole_size - 1);
      uint64_t range = MIN2(blackhole_size - bo_offset, size - off);
      struct drm_panthor_vm_bind_op op = {
         .flags = DRM_PANTHOR_VM_BIND_OP_TYPE_MAP,
         .bo_handle = blackhole->handle,
         .bo_offset = bo_offset,
         .va = va,
         .size = range,
      };
      int ret = panvk_vm_binder_bind(&binder, &op);
      if (ret)
         goto err_unmap;

      off += range;
   }
   assert(off == size);

   int ret = panvk_vm_binder_flush(&binder);
   if (ret)
      goto err_unmap;

   return VK_SUCCESS;

err_unmap:
   {
      struct pan_kmod_vm_op unmap = {
         .type = PAN_KMOD_VM_OP_TYPE_UNMAP,
         .va = {
            .start = address,
            .size = size,
         },
      };
      ASSERTED int ret = pan_kmod_vm_bind(
         device->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE, &unmap, 1);
      assert(!ret);
   }

   return panvk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
}
