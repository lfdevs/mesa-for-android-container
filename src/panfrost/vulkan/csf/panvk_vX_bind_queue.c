/*
 * Copyright © 2026 NXP
 *
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "genxml/cs_builder.h"
#include "genxml/decode.h"

#include "panvk_buffer.h"
#include "panvk_cmd_buffer.h"
#include "panvk_device_memory.h"
#include "panvk_macros.h"
#include "panvk_queue.h"
#include "panvk_utrace.h"
#include "pan_layout.h"

#include "util/bitscan.h"
#include "vk_drm_syncobj.h"
#include "vk_log.h"

struct panvk_bind_queue_submit_sync_ops {
   struct pan_kmod_sync_op *all;
   size_t all_count;

   struct pan_kmod_sync_op *waits;
   size_t wait_count;

   struct pan_kmod_sync_op *signals;
   size_t signal_count;

   struct pan_kmod_sync_op small_storage[4];
};

static void
panvk_bind_queue_submit_sync_ops_init(
   struct panvk_bind_queue_submit_sync_ops *sync_ops,
   const struct vk_queue_submit *vk_submit,
   const struct pan_kmod_sync_op *extra_signal)
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

      sync_ops->waits[i] = (struct pan_kmod_sync_op){
         .handle = syncobj->syncobj,
         .point = wait->wait_value,
      };
   }

   uint32_t signal_idx = 0;
   for (uint32_t i = 0; i < vk_submit->signal_count; i++) {
      const struct vk_sync_signal *signal = &vk_submit->signals[i];
      const struct vk_drm_syncobj *syncobj =
         vk_sync_as_drm_syncobj(signal->sync);
      assert(syncobj);

      sync_ops->signals[signal_idx++] = (struct pan_kmod_sync_op){
         .handle = syncobj->syncobj,
         .point = signal->signal_value,
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
   struct pan_kmod_vm_op storage[16];
   struct pan_kmod_vm_op pending_op;

   struct pan_kmod_vm_multi_op_ctx ctx;
};

static void
panvk_bind_queue_submit_init(struct panvk_bind_queue_submit *submit,
                             struct vk_queue *vk_queue,
                             struct vk_queue_submit *vk_submit)
{
   struct panvk_bind_queue *queue =
      container_of(vk_queue, struct panvk_bind_queue, vk);
   struct panvk_device *device = to_panvk_device(queue->vk.base.device);

   const bool force_sync = PANVK_DEBUG(SYNC);

   *submit = (struct panvk_bind_queue_submit){
      .queue = queue,
      .force_sync = force_sync,
   };

   struct pan_kmod_sync_op syncobj_signal = {
      .handle = queue->syncobj_handle,
      .point = 0,
   };

   panvk_bind_queue_submit_sync_ops_init(&submit->sync_ops, vk_submit,
                                         force_sync ? &syncobj_signal : NULL);

   pan_kmod_vm_multi_op_init(&submit->ctx, device->kmod.vm,
                             PAN_KMOD_VM_OP_MODE_ASYNC, submit->storage,
                             ARRAY_SIZE(submit->storage), NULL);

   /* The pending op always starts as a SYNC_ONLY op with just the waits
    * and will be upgraded to a MAP when bind operations are processed.
    */
   submit->pending_op = (struct pan_kmod_vm_op){
      .type = PAN_KMOD_VM_OP_TYPE_SYNC_ONLY,
      .wait = {
         .count = submit->sync_ops.wait_count,
         .array = submit->sync_ops.waits,
      },
   };
}

static void
panvk_bind_queue_submit_cleanup(struct panvk_bind_queue_submit *submit)
{
   panvk_bind_queue_submit_sync_ops_cleanup(&submit->sync_ops);
}

static int
panvk_bind_queue_submit_process_signals(struct panvk_bind_queue_submit *submit)
{
   struct panvk_bind_queue *queue = submit->queue;
   struct panvk_device *device =
      to_panvk_device(queue->vk.base.device);
   struct pan_kmod_vm_multi_op_ctx *ctx = &submit->ctx;

   submit->pending_op.signal.array = submit->sync_ops.signals;
   submit->pending_op.signal.count = submit->sync_ops.signal_count;



   /* A SYNC_ONLY op without wait or signal syncs is a no-op. Evict such
    * ops here rather than letting the kernel reject an empty SYNC_ONLY op
    * with -EINVAL in async mode.
    */
   if (submit->pending_op.type == PAN_KMOD_VM_OP_TYPE_SYNC_ONLY &&
       submit->pending_op.wait.count == 0 &&
       submit->pending_op.signal.count == 0)
      return 0;

   int ret = pan_kmod_vm_multi_op_push(ctx, &submit->pending_op);
   if (ret)
      return ret;

   ret = pan_kmod_vm_multi_op_flush(ctx);
   if (ret)
      return ret;

   if (submit->force_sync) {
      ASSERTED int ret = drmSyncobjWait(device->drm_fd,
                               &queue->syncobj_handle, 1, INT64_MAX,
                               DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL, NULL);
      assert(!ret);

      drmSyncobjReset(device->drm_fd, &queue->syncobj_handle, 1);
   }

   return 0;
}

static bool
can_merge_map_ops(struct pan_kmod_vm_op a, struct pan_kmod_vm_op b,
                  struct pan_kmod_vm_op *out)
{
   assert(a.type == PAN_KMOD_VM_OP_TYPE_MAP);
   assert(b.type == PAN_KMOD_VM_OP_TYPE_MAP);

   /* If the flags don't match, we don't merge. */
   if (a.flags != b.flags)
      return false;

   if (b.va.start < a.va.start)
      SWAP(a, b);

   if (a.va.start + a.va.size != b.va.start)
      return false;

   if (!(a.flags & PAN_KMOD_VM_OP_OP_MAP_SPARSE) &&
       (a.map.bo != b.map.bo || a.map.bo_offset + a.va.size != b.map.bo_offset))
      return false;

   *out = (struct pan_kmod_vm_op){
      .type = PAN_KMOD_VM_OP_TYPE_MAP,
      .va = {
         .start = a.va.start,
         .size = b.va.start + b.va.size - a.va.start,
      },
      .map = {
         .bo = a.map.bo,
         .bo_offset = a.map.bo_offset,
      },
      .flags = a.flags,
   };
   return true;
}

static int
panvk_bind_queue_submit_sparse_memory_bind(
   struct panvk_bind_queue_submit *submit,
   VkDeviceAddress resource_va, const VkSparseMemoryBind *in)
{
   VK_FROM_HANDLE(panvk_device_memory, mem, in->memory);
   struct pan_kmod_vm_op op = {
      .type = PAN_KMOD_VM_OP_TYPE_MAP,
      .va = {
         .start = resource_va + in->resourceOffset,
         .size = in->size,
      },
   };

   if (in->memory) {
      op.map.bo = mem->bo;
      op.map.bo_offset = in->memoryOffset;
   } else {
      op.flags = PAN_KMOD_VM_OP_OP_MAP_SPARSE;
   }

   /* We can always merge a MAP op with a SYNC_ONLY one. */
   if (submit->pending_op.type == PAN_KMOD_VM_OP_TYPE_SYNC_ONLY ||
       can_merge_map_ops(op, submit->pending_op, &op)) {
      submit->pending_op.type = op.type;
      submit->pending_op.va = op.va;
      submit->pending_op.map = op.map;
      submit->pending_op.flags = op.flags;
      return 0;
   }

   /* If we can't merge, flush the pending op and replace it by our new MAP. */
   int ret = pan_kmod_vm_multi_op_push(&submit->ctx, &submit->pending_op);

   submit->pending_op = op;

   return ret;
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

static VkSparseMemoryBind
image_to_mem_bind(const struct panvk_image *image,
                  const struct panvk_sparse_block_memory_bind *in)
{
   const struct panvk_image_plane *plane = &image->planes[in->plane_index];

   /* Previously, sparse residency was implemented using block U-interleaved
    * (https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/37483).
    * Interleaved 64k offers better map and unmap performance (at most one bind
    * op per tile, as opposed to 4 to 16 with U-interleaved), and no worse
    * access performance.
    *
    * Note that interleaved 64k is not a thing on pre-v10 hardware. If at any
    * point there's a desire for sparse residency on pre-v10 hardware, resurrect
    * the code linked above.
    */
   assert(image->vk.drm_format_mod == DRM_FORMAT_MOD_ARM_INTERLEAVED_64K);

   struct pan_image_block_size tile_extent_el =
      pan_interleaved_64k_tile_size_el(vk_format_to_pipe_format(image->vk.format));
   struct pan_image_block_size tile_extent_px = {
      tile_extent_el.width * vk_format_get_blockwidth(image->vk.format),
      tile_extent_el.height * vk_format_get_blockheight(image->vk.format),
   };
   VkOffset3D offset_tiles = {
      in->offset.x / tile_extent_px.width,
      in->offset.y / tile_extent_px.height,
      in->offset.z,
   };
   assert(in->extent.width == tile_extent_px.width &&
          in->extent.height == tile_extent_px.height &&
          in->extent.depth == 1);
   uint32_t tile_size_B = 65536;

   assert(image->vk.image_type != VK_IMAGE_TYPE_3D || in->layer == 0);

   const struct pan_image_slice_layout *slayout = &plane->plane.layout.slices[in->level];

   return (VkSparseMemoryBind){
      .resourceOffset =
         in->layer * plane->plane.layout.array_stride_B +
         slayout->offset_B +
         offset_tiles.z * slayout->tiled_or_linear.surface_stride_B +
         offset_tiles.y * slayout->tiled_or_linear.row_stride_B +
         offset_tiles.x * tile_size_B,
      .size = tile_size_B,
      .memory = in->mem,
      .memoryOffset = in->mem_offset,
   };
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
         const VkSparseMemoryBind *mbind = &vk_submit->buffer_binds[i].pBinds[j];

         ret = panvk_bind_queue_submit_sparse_memory_bind(submit, resource_va,
                                                          mbind);
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
         const VkSparseMemoryBind *mbind =
            &vk_submit->image_opaque_binds[i].pBinds[j];

         ret = panvk_bind_queue_submit_sparse_memory_bind(submit, resource_va,
                                                          mbind);
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
         const VkSparseImageMemoryBind *ibind =
            &vk_submit->image_binds[i].pBinds[j];

         VkOffset3D max = {
            ibind->offset.x + ibind->extent.width,
            ibind->offset.y + ibind->extent.height,
            ibind->offset.z + ibind->extent.depth,
         };
         struct panvk_sparse_block_memory_bind bind = {
            /* We only support single-plane images right now. See
             * https://gitlab.freedesktop.org/panfrost/mesa/-/issues/243 details. */
            .plane_index = 0,
            .level = ibind->subresource.mipLevel,
            .layer = ibind->subresource.arrayLayer,
            .offset = {},
            .extent = sblock.extent,
            .mem = ibind->memory,
            .mem_offset = ibind->memoryOffset,
         };

         for (bind.offset.z = ibind->offset.z;
              bind.offset.z < max.z; bind.offset.z += sblock.extent.depth) {
            for (bind.offset.y = ibind->offset.y;
                 bind.offset.y < max.y; bind.offset.y += sblock.extent.height) {
               for (bind.offset.x = ibind->offset.x;
                    bind.offset.x < max.x; bind.offset.x += sblock.extent.width) {
                  VkSparseMemoryBind mbind = image_to_mem_bind(image, &bind);

                  ret = panvk_bind_queue_submit_sparse_memory_bind(
                     submit, image->sparse.device_address, &mbind);
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
