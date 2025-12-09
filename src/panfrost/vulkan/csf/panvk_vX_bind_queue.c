/*
 * Copyright © 2024 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "drm-uapi/panthor_drm.h"

#include "genxml/cs_builder.h"
#include "genxml/decode.h"

#include "panvk_buffer.h"
#include "panvk_cmd_buffer.h"
#include "panvk_device_memory.h"
#include "panvk_macros.h"
#include "panvk_queue.h"
#include "panvk_utrace.h"
#include "panvk_sparse.h"

#include "util/bitscan.h"
#include "vk_drm_syncobj.h"
#include "vk_log.h"

struct panvk_bind_queue_submit_sync_ops {
   struct drm_panthor_sync_op *all;
   size_t all_count;
   struct drm_panthor_sync_op *waits;
   size_t wait_count;
   struct drm_panthor_sync_op *signals;
   size_t signal_count;

   struct drm_panthor_sync_op small_storage[4];
};

static void
panvk_bind_queue_submit_sync_ops_init(
   struct panvk_bind_queue_submit_sync_ops *sync_ops,
   const struct vk_queue_submit *vk_submit,
   const struct drm_panthor_sync_op *extra_signal)
{
   size_t signal_count = vk_submit->signal_count;
   if (extra_signal)
      signal_count++;

   size_t all_count = vk_submit->wait_count + signal_count;

   sync_ops->all = all_count <= ARRAY_SIZE(sync_ops->small_storage)
                         ? sync_ops->small_storage
                         : malloc(sizeof(*sync_ops->all) * all_count);
   sync_ops->all_count = all_count;

   sync_ops->waits = sync_ops->all;
   sync_ops->wait_count = vk_submit->wait_count;

   sync_ops->signals = sync_ops->all + vk_submit->wait_count;
   sync_ops->signal_count = signal_count;

   for (uint32_t i = 0; i < vk_submit->wait_count; i++) {
      const struct vk_sync_wait *wait = &vk_submit->waits[i];
      const struct vk_drm_syncobj *syncobj = vk_sync_as_drm_syncobj(wait->sync);
      assert(syncobj);

      sync_ops->waits[i] = (struct drm_panthor_sync_op){
         .flags = (syncobj->base.flags & VK_SYNC_IS_TIMELINE
                      ? DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ
                      : DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_SYNCOBJ) |
                  DRM_PANTHOR_SYNC_OP_WAIT,
         .handle = syncobj->syncobj,
         .timeline_value = wait->wait_value,
      };
   }

   uint32_t signal_idx = 0;
   for (uint32_t i = 0; i < vk_submit->signal_count; i++) {
      const struct vk_sync_signal *signal = &vk_submit->signals[i];
      const struct vk_drm_syncobj *syncobj =
         vk_sync_as_drm_syncobj(signal->sync);
      assert(syncobj);

      sync_ops->signals[signal_idx++] = (struct drm_panthor_sync_op){
         .flags = (syncobj->base.flags & VK_SYNC_IS_TIMELINE
                      ? DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ
                      : DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_SYNCOBJ) |
                  DRM_PANTHOR_SYNC_OP_SIGNAL,
         .handle = syncobj->syncobj,
         .timeline_value = signal->signal_value,
      };
   }
   if (extra_signal)
      sync_ops->signals[signal_idx++] = *extra_signal;
   assert(signal_idx == signal_count);
}

static void
panvk_bind_queue_submit_sync_ops_cleanup(
   struct panvk_bind_queue_submit_sync_ops *sync_ops)
{
   if (sync_ops->all != sync_ops->small_storage)
      free(sync_ops->all);
}

struct panvk_bind_queue_submit {
   struct panvk_bind_queue *queue;

   bool force_sync;

   struct panvk_bind_queue_submit_sync_ops sync_ops;

   struct drm_panthor_vm_bind_op *bind_ops;
   size_t bind_op_count; /* number of bind ops buffered */
   size_t bind_op_cap;   /* size of the bind op buffer */

   struct drm_panthor_vm_bind_op bind_op_small_storage[16];
};

static void
panvk_bind_queue_submit_init(struct panvk_bind_queue_submit *submit,
                             struct vk_queue *vk_queue,
                             struct vk_queue_submit *vk_submit)
{
   struct panvk_bind_queue *queue =
      container_of(vk_queue, struct panvk_bind_queue, vk);

   const bool force_sync = PANVK_DEBUG(SYNC);

   *submit = (struct panvk_bind_queue_submit){
      .queue = queue,
      .force_sync = force_sync,
   };

   struct drm_panthor_sync_op syncobj_signal = {
      .flags =
         DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_SYNCOBJ | DRM_PANTHOR_SYNC_OP_SIGNAL,
      .handle = queue->syncobj_handle,
   };

   panvk_bind_queue_submit_sync_ops_init(&submit->sync_ops, vk_submit,
                                         force_sync ? &syncobj_signal : NULL);

   /* TODO: guess how many bind ops we'll need and allocate that much, unless
    * it's too much
    */
   uint32_t bind_op_cap = ARRAY_SIZE(submit->bind_op_small_storage);

   submit->bind_ops = bind_op_cap <= ARRAY_SIZE(submit->bind_op_small_storage)
                         ? submit->bind_op_small_storage
                         : malloc(sizeof(*submit->bind_ops) * bind_op_cap);
   submit->bind_op_cap = bind_op_cap;
}

static void
panvk_bind_queue_submit_cleanup(struct panvk_bind_queue_submit *submit)
{
   panvk_bind_queue_submit_sync_ops_cleanup(&submit->sync_ops);

   if (submit->bind_ops != submit->bind_op_small_storage)
      free(submit->bind_ops);
}

static int
panvk_bind_queue_submit_flush(struct panvk_bind_queue_submit *submit)
{
   struct panvk_device *dev = to_panvk_device(submit->queue->vk.base.device);

   if (submit->bind_op_count == 0)
      return 0;

   struct drm_panthor_vm_bind req = {
      .vm_id = dev->kmod.vm->handle,
      .flags = DRM_PANTHOR_VM_BIND_ASYNC,
      .ops = DRM_PANTHOR_OBJ_ARRAY(submit->bind_op_count, submit->bind_ops),
   };
   int ret = pan_kmod_ioctl(dev->drm_fd, DRM_IOCTL_PANTHOR_VM_BIND, &req);
   submit->bind_op_count = 0;
   return ret;
}

static int
panvk_bind_queue_submit_vm_bind(
   struct panvk_bind_queue_submit *submit,
   const struct drm_panthor_vm_bind_op *op)
{
   if (submit->bind_op_count == submit->bind_op_cap) {
      int ret = panvk_bind_queue_submit_flush(submit);
      if (ret)
         return ret;
   }

   struct drm_panthor_vm_bind_op tmp = *op;
   assert(!tmp.syncs.array);
   if (submit->sync_ops.wait_count > 0) {
      tmp.syncs = (struct drm_panthor_obj_array)DRM_PANTHOR_OBJ_ARRAY(
         submit->sync_ops.wait_count, submit->sync_ops.waits);
      submit->sync_ops.wait_count = 0;
   }

   assert(submit->bind_op_count < submit->bind_op_cap);
   submit->bind_ops[submit->bind_op_count++] = tmp;

   return 0;
}

static int
panvk_bind_queue_submit_process_signals(struct panvk_bind_queue_submit *submit)
{
   struct panvk_bind_queue *queue = submit->queue;
   struct panvk_device *device =
      to_panvk_device(queue->vk.base.device);

   struct drm_panthor_vm_bind_op tmp = {
      .flags = DRM_PANTHOR_VM_BIND_OP_TYPE_SYNC_ONLY,
   };

   struct drm_panthor_vm_bind_op *op = submit->bind_op_count > 0
      ? &submit->bind_ops[submit->bind_op_count - 1]
      : &tmp;

   assert(!op->syncs.array ||
          (op->syncs.array == (__u64)(uintptr_t)submit->sync_ops.waits &&
           op->syncs.count > 0));

   /* If we
    * - have not processed waits (i.e. have not done any bind ops at all), or
    * - the op we're about to make do signals already waits
    *
    * the op will be made to do all the sync ops.
    */
   op->syncs = submit->sync_ops.wait_count > 0 || op->syncs.array
         ? (struct drm_panthor_obj_array)DRM_PANTHOR_OBJ_ARRAY(
            submit->sync_ops.all_count, submit->sync_ops.all)
         : (struct drm_panthor_obj_array)DRM_PANTHOR_OBJ_ARRAY(
            submit->sync_ops.signal_count, submit->sync_ops.signals);

   if (op == &tmp && op->syncs.count > 0) {
      /* If we were filling out tmp, it wasn't in the bind op buffer. Now is a
       * good time to plop it into there.
       */
      assert(submit->bind_op_count < submit->bind_op_cap);
      submit->bind_ops[submit->bind_op_count++] = tmp;
   }

   int ret = panvk_bind_queue_submit_flush(submit);
   if (ret)
      return ret;

   if (submit->force_sync) {
      int ret = drmSyncobjWait(device->drm_fd, &queue->syncobj_handle, 1,
                               INT64_MAX, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL,
                               NULL);
      assert(!ret);

      drmSyncobjReset(device->drm_fd, &queue->syncobj_handle, 1);
   }

   return 0;
}

static int
panvk_bind_queue_submit_map_to_blackhole(
   struct panvk_bind_queue_submit *submit,
   uint64_t base_va, uint64_t size)
{
   struct panvk_device *dev = to_panvk_device(submit->queue->vk.base.device);

   struct pan_kmod_bo *blackhole = panvk_get_blackhole(dev);
   uint64_t blackhole_size = blackhole->size;

   uint64_t off = 0;
   while (off < size) {
      uint64_t bo_offset = base_va & (blackhole_size - 1);
      uint64_t va = base_va + off;
      uint64_t va_range = MIN2(blackhole_size - bo_offset, size - off);

      struct drm_panthor_vm_bind_op op = {
         .flags = DRM_PANTHOR_VM_BIND_OP_TYPE_MAP,
         .bo_handle = blackhole->handle,
         .bo_offset = bo_offset,
         .va = va,
         .size = va_range,
      };
      int ret = panvk_bind_queue_submit_vm_bind(submit, &op);
      if (!ret)
         return ret;

      off += va_range;
   }

   return 0;
}

static int
panvk_bind_queue_submit_sparse_memory_bind(
   struct panvk_bind_queue_submit *submit,
   VkDeviceAddress resource_va, const VkSparseMemoryBind *in)
{
   VK_FROM_HANDLE(panvk_device_memory, mem, in->memory);

   if (mem) {
      struct drm_panthor_vm_bind_op op = {
         .flags = DRM_PANTHOR_VM_BIND_OP_TYPE_MAP,
         .bo_handle = mem->bo->handle,
         .bo_offset = in->memoryOffset,
         .va = resource_va + in->resourceOffset,
         .size = in->size,
      };
      return panvk_bind_queue_submit_vm_bind(submit, &op);
   } else {
      return panvk_bind_queue_submit_map_to_blackhole(submit, resource_va, in->size);
   }
}

struct panvk_sparse_block_memory_bind {
   uint32_t plane_index;
   uint32_t layer;
   uint32_t level;
   VkOffset3D offset; /* must be a multiple of extent */
   VkExtent3D extent;
   VkDeviceMemory mem;
   VkDeviceSize mem_offset;
};

static int
panvk_bind_queue_submit_sparse_block_memory_bind(
   struct panvk_bind_queue_submit *submit,
   const struct panvk_image *image,
   const struct panvk_sparse_block_memory_bind *in)
{
   uint64_t resource_va = image->sparse.device_address;
   const struct panvk_image_plane *plane = &image->planes[in->plane_index];
   int ret;

   /* 3D images are not yet supported. See
    * https://gitlab.freedesktop.org/panfrost/mesa/-/issues/242 */
   assert(image->vk.image_type == VK_IMAGE_TYPE_2D);

   /*
    * Right now we only handle U interleaved, but it would be beneficial to use
    * 64K interleaved for sparse partially-resident images. See
    * https://gitlab.freedesktop.org/panfrost/mesa/-/issues/244 for details.
    */
   assert(image->vk.drm_format_mod == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED);
   VkExtent3D oblock_clumps =
      vk_format_is_block_compressed(image->vk.format)
         ? (VkExtent3D){4, 4, 1}
         : (VkExtent3D){16, 16, 1};
   VkExtent3D oblock_extent = {
      oblock_clumps.width * vk_format_get_blockwidth(image->vk.format),
      oblock_clumps.height * vk_format_get_blockheight(image->vk.format),
      oblock_clumps.depth * 1,
   };
   uint32_t oblock_size_B =
      oblock_clumps.width * oblock_clumps.height * oblock_clumps.depth *
         vk_format_get_blocksize(image->vk.format);

   VkOffset3D offset_oblocks = {
      in->offset.x / oblock_extent.width,
      in->offset.y / oblock_extent.height,
      in->offset.z / oblock_extent.depth,
   };
   VkExtent3D extent_oblocks = {
      DIV_ROUND_UP(in->extent.width, oblock_extent.width),
      DIV_ROUND_UP(in->extent.height, oblock_extent.height),
      DIV_ROUND_UP(in->extent.depth, oblock_extent.depth),
   };

   assert(offset_oblocks.z == 0 && extent_oblocks.depth == 1);

   uint32_t max_y = offset_oblocks.y + extent_oblocks.height;

   uint64_t mem_offset = in->mem_offset;
   for (uint32_t y = offset_oblocks.y; y < max_y; y++) {
      const struct pan_image_slice_layout *slayout = &plane->plane.layout.slices[in->level];

      VkSparseMemoryBind bind = {
         .resourceOffset = resource_va +
            in->layer * plane->plane.layout.array_stride_B +
            slayout->offset_B +
            y * slayout->tiled_or_linear.row_stride_B +
            offset_oblocks.x * oblock_size_B,
         .size = extent_oblocks.width * oblock_size_B,
         .memory = in->mem,
         .memoryOffset = mem_offset,
      };
      ret = panvk_bind_queue_submit_sparse_memory_bind(submit, resource_va, &bind);
      if (ret)
         return ret;
      mem_offset += bind.size;
   }

   return 0;
}

static int
panvk_bind_queue_submit_do(struct panvk_bind_queue_submit *submit,
                           const struct vk_queue_submit *vk_submit)
{
   int ret;

   for (uint32_t i = 0; i < vk_submit->buffer_bind_count; i++) {
      VK_FROM_HANDLE(panvk_buffer, buf, vk_submit->buffer_binds[i].buffer);
      assert(buf->vk.create_flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT);

      uint64_t resource_va = buf->vk.device_address;

      for (uint32_t j = 0; j < vk_submit->buffer_binds[i].bindCount; j++) {
         const VkSparseMemoryBind *in = &vk_submit->buffer_binds[i].pBinds[j];

         ret = panvk_bind_queue_submit_sparse_memory_bind(submit, resource_va, in);
         if (ret)
            return ret;
      }
   }
   for (uint32_t i = 0; i < vk_submit->image_opaque_bind_count; i++) {
      VK_FROM_HANDLE(panvk_image, image,
                     vk_submit->image_opaque_binds[i].image);
      assert(image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT);

      uint64_t resource_va = image->sparse.device_address;

      for (uint32_t j = 0; j < vk_submit->image_opaque_binds[i].bindCount;
           j++) {
         const VkSparseMemoryBind *in =
            &vk_submit->image_opaque_binds[i].pBinds[j];

         ret = panvk_bind_queue_submit_sparse_memory_bind(submit, resource_va, in);
         if (ret)
            return ret;
      }
   }
   for (uint32_t i = 0; i < vk_submit->image_bind_count; i++) {
      VK_FROM_HANDLE(panvk_image, image, vk_submit->image_binds[i].image);
      assert(image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT);
      assert(image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT);

      struct panvk_sparse_block_desc sblock = panvk_get_sparse_block_desc(image->vk.image_type, image->vk.format);

      for (uint32_t j = 0; j < vk_submit->image_binds[i].bindCount; j++) {
         const VkSparseImageMemoryBind *in =
            &vk_submit->image_binds[i].pBinds[j];

         VkOffset3D max = {
            in->offset.x + in->extent.width,
            in->offset.y + in->extent.height,
            in->offset.z + in->extent.depth,
         };
         struct panvk_sparse_block_memory_bind bind = {
            /* We only support single-plane images right now. See
             * https://gitlab.freedesktop.org/panfrost/mesa/-/issues/243 details. */
            .plane_index = 0,
            .level = in->subresource.mipLevel,
            .layer = in->subresource.arrayLayer,
            .offset = {},
            .extent = sblock.extent,
            .mem = in->memory,
            .mem_offset = in->memoryOffset,
         };
         for (bind.offset.z = in->offset.z;
              bind.offset.z < max.z; bind.offset.z += sblock.extent.depth) {
            for (bind.offset.y = in->offset.y;
                 bind.offset.y < max.y; bind.offset.y += sblock.extent.height) {
               for (bind.offset.x = in->offset.x;
                    bind.offset.x < max.x; bind.offset.x += sblock.extent.width) {
                  ret = panvk_bind_queue_submit_sparse_block_memory_bind(submit, image, &bind);
                  if (ret)
                     return ret;
                  bind.mem_offset += sblock.size_B;
               }
            }
         }
      }
   }

   return panvk_bind_queue_submit_process_signals(submit);
}

VkResult
panvk_per_arch(bind_queue_submit)(struct vk_queue *vk_queue,
                                  struct vk_queue_submit *vk_submit)
{
   struct panvk_bind_queue_submit submit;
   VkResult result = VK_SUCCESS;

   if (vk_queue_is_lost(vk_queue))
      return VK_ERROR_DEVICE_LOST;

   panvk_bind_queue_submit_init(&submit, vk_queue, vk_submit);

   int ret = panvk_bind_queue_submit_do(&submit, vk_submit);
   if (ret)
      result = vk_queue_set_lost(vk_queue, "GROUP_SUBMIT: %m");

   panvk_bind_queue_submit_cleanup(&submit);

   return result;
}

VkResult
panvk_per_arch(create_bind_queue)(struct panvk_device *dev,
                                  const VkDeviceQueueCreateInfo *create_info,
                                  uint32_t queue_idx,
                                  struct vk_queue **out_queue)
{
   struct panvk_bind_queue *queue = vk_zalloc(
      &dev->vk.alloc, sizeof(*queue), 8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!queue)
      return panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result =
      vk_queue_init(&queue->vk, &dev->vk, create_info, queue_idx);
   if (result != VK_SUCCESS)
      goto err_free_queue;

   int ret = drmSyncobjCreate(dev->drm_fd, 0, &queue->syncobj_handle);
   if (ret) {
      result = panvk_errorf(dev, VK_ERROR_INITIALIZATION_FAILED,
                            "Failed to create our internal sync object");
      goto err_finish_queue;
   }

   queue->vk.driver_submit = panvk_per_arch(bind_queue_submit);
   *out_queue = &queue->vk;
   return VK_SUCCESS;

err_finish_queue:
   vk_queue_finish(&queue->vk);

err_free_queue:
   vk_free(&dev->vk.alloc, queue);
   return result;
}

void
panvk_per_arch(destroy_bind_queue)(struct vk_queue *vk_queue)
{
   struct panvk_bind_queue *queue =
      container_of(vk_queue, struct panvk_bind_queue, vk);
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);

   drmSyncobjDestroy(dev->drm_fd, queue->syncobj_handle);
   vk_queue_finish(&queue->vk);
   vk_free(&dev->vk.alloc, queue);
}

VkResult
panvk_per_arch(bind_queue_check_status)(struct vk_queue *vk_queue)
{
   return VK_SUCCESS;
}
