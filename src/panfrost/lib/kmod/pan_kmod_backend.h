/*
 * Copyright © 2023 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "util/log.h"

#include "pan_kmod.h"

static inline void
pan_kmod_dev_init(struct pan_kmod_dev *dev, int fd, uint32_t flags,
                  const struct pan_kmod_driver *drv_info,
                  const struct pan_kmod_ops *ops,
                  const struct pan_kmod_allocator *allocator)
{
   simple_mtx_init(&dev->handle_to_bo.lock, mtx_plain);
   util_sparse_array_init(&dev->handle_to_bo.array,
                          sizeof(struct pan_kmod_bo *), 512);
   simple_mtx_init(&dev->pending_bo_syncs.lock, mtx_plain);
   util_dynarray_init(&dev->pending_bo_syncs.array, NULL);
   dev->driver = *drv_info;
   dev->fd = fd;
   dev->flags = flags;
   dev->ops = ops;
   dev->allocator = allocator;
}

static inline void
pan_kmod_dev_cleanup(struct pan_kmod_dev *dev)
{
   if (dev->flags & PAN_KMOD_DEV_FLAG_OWNS_FD)
      close(dev->fd);

   util_dynarray_fini(&dev->pending_bo_syncs.array);
   util_sparse_array_finish(&dev->handle_to_bo.array);
   simple_mtx_destroy(&dev->handle_to_bo.lock);
   simple_mtx_destroy(&dev->pending_bo_syncs.lock);
}

static inline void *
pan_kmod_alloc(const struct pan_kmod_allocator *allocator, size_t size)
{
   return allocator->zalloc(allocator, size, false);
}

static inline void *
pan_kmod_alloc_transient(const struct pan_kmod_allocator *allocator,
                         size_t size)
{
   return allocator->zalloc(allocator, size, true);
}

static inline void
pan_kmod_free(const struct pan_kmod_allocator *allocator, void *data)
{
   return allocator->free(allocator, data);
}

static inline void *
pan_kmod_dev_alloc(struct pan_kmod_dev *dev, size_t size)
{
   return pan_kmod_alloc(dev->allocator, size);
}

static inline void *
pan_kmod_dev_alloc_transient(struct pan_kmod_dev *dev, size_t size)
{
   return pan_kmod_alloc_transient(dev->allocator, size);
}

static inline void
pan_kmod_dev_free(const struct pan_kmod_dev *dev, void *data)
{
   return pan_kmod_free(dev->allocator, data);
}

static inline void
pan_kmod_bo_init(struct pan_kmod_bo *bo, struct pan_kmod_dev *dev,
                 struct pan_kmod_vm *exclusive_vm, uint64_t size, uint32_t flags,
                 uint32_t handle)
{
   /* Set by default when the device is IO coherent. We might want to
    * make it optional at some point and pass a NON_COHERENT flag to
    * the KMD to force non-coherent mappings on IO coherent setup.
    */
   if (dev->props.is_io_coherent)
      flags |= PAN_KMOD_BO_FLAG_IO_COHERENT;

   bo->dev = dev;
   bo->exclusive_vm = exclusive_vm;
   bo->size = size;
   bo->flags = flags;
   bo->handle = handle;
   p_atomic_set(&bo->refcnt, 1);
}

void pan_kmod_flush_bo_map_syncs_locked(struct pan_kmod_dev *dev);

static inline void
pan_kmod_bo_cleanup(struct pan_kmod_bo *bo)
{
   if (bo->has_pending_deferred_syncs) {
      struct pan_kmod_dev *dev = bo->dev;

      simple_mtx_lock(&dev->pending_bo_syncs.lock);
      pan_kmod_flush_bo_map_syncs_locked(dev);
      simple_mtx_unlock(&dev->pending_bo_syncs.lock);
   }
}

static inline void
pan_kmod_vm_init(struct pan_kmod_vm *vm, struct pan_kmod_dev *dev,
                 uint32_t handle, uint32_t flags)
{
   vm->dev = dev;
   vm->handle = handle;
   vm->flags = flags;
   vm->pgsize_bitmap = dev->props.pgsize_bitmap;

   simple_mtx_init(&vm->sparse_dummy.lock, mtx_plain);
}

static inline void
pan_kmod_vm_cleanup(struct pan_kmod_vm *vm)
{
   pan_kmod_bo_put(vm->sparse_dummy.bo);
   simple_mtx_destroy(&vm->sparse_dummy.lock);
}

static inline int
pan_kmod_vm_op_check(struct pan_kmod_vm *vm, enum pan_kmod_vm_op_mode mode,
                     struct pan_kmod_vm_op *op)
{
   /* We should only have sync operations on an async VM bind request. */
   if (mode != PAN_KMOD_VM_OP_MODE_ASYNC &&
       (op->signal.count || op->wait.count)) {
      mesa_loge("only PAN_KMOD_VM_OP_MODE_ASYNC can be passed sync operations");
      return -1;
   }

   /* Make sure the PAN_KMOD_VM_FLAG_AUTO_VA and VA passed to the op match. */
   if (op->type == PAN_KMOD_VM_OP_TYPE_MAP &&
       !!(vm->flags & PAN_KMOD_VM_FLAG_AUTO_VA) !=
          (op->va.start == PAN_KMOD_VM_MAP_AUTO_VA)) {
      mesa_loge("op->va.start and vm->flags don't match");
      return -1;
   }

   if ((op->flags & ~vm->dev->props.supported_vm_op_flags) != 0) {
      mesa_loge("incompatible VM operation flags");
      return -1;
   }

   if ((op->flags & PAN_KMOD_VM_OP_OP_MAP_SPARSE) && op->map.bo) {
      mesa_loge("sparse operations cannot have attached BOs");
      return -1;
   }

   if ((op->flags & PAN_KMOD_VM_OP_OP_MAP_SPARSE) &&
       op->va.start == PAN_KMOD_VM_MAP_AUTO_VA) {
      mesa_loge("sparse operations cannot have an automatic VA assigned");
      return -1;
   }

   return 0;
}

static inline void
pan_kmod_perf_session_init(struct pan_kmod_perf_session *session,
                           struct pan_kmod_dev *dev,
                           struct pan_kmod_perf_caps caps,
                           struct mali_perf_backend backend)
{
   session->dev = dev;
   session->caps = caps;
   session->mali_perf_backend = backend;
}

struct pan_kmod_perf_sample_section {
   struct {
      enum mali_perf_block_type type;
      uint32_t index;
   } block;
   uint32_t hw_sample_offset;
   uint32_t consolidated_sample_offset;
};

struct pan_kmod_perf_sample_layout {
   uint32_t block_type_offset[MALI_PERF_BLOCK_TYPE_COUNT];
   uint32_t consolidated_sample_size;
   uint32_t section_count;
   const struct pan_kmod_perf_sample_section *sections;
};

struct pan_kmod_perf_consolidated_sample {
   struct {
      uint64_t start_gpu_ts;
      uint64_t end_gpu_ts;
   } time_span;
   void *data;
};

struct pan_kmod_perf_dumped_sample {
   void *data;
};

static inline int
pan_kmod_perf_sample_layout_init(struct pan_kmod_dev *dev,
                                 struct pan_kmod_perf_sample_layout *layout,
                                 uint32_t hw_blk_cnt)
{
   *layout = (struct pan_kmod_perf_sample_layout){
      .sections = pan_kmod_dev_alloc(dev, hw_blk_cnt * sizeof(*layout->sections)),
   };

   if (!layout->sections)
      return -1;

   return 0;
}

static inline void
pan_kmod_perf_sample_layout_cleanup(struct pan_kmod_dev *dev,
                                    struct pan_kmod_perf_sample_layout *layout)
{
   if (layout->sections)
      pan_kmod_dev_free(dev, (void *)layout->sections);
}

static inline void
pan_kmod_perf_sample_layout_add_section(
   struct pan_kmod_dev *dev, struct pan_kmod_perf_sample_layout *layout,
   enum mali_perf_block_type blk_type, uint32_t blk_idx,
   uint32_t *hw_sample_offset)
{
   uint32_t counters_per_block = pan_query_perf_counter_per_block(&dev->props);
   uint32_t hw_blk_sz = counters_per_block * sizeof(uint32_t);
   uint32_t consolidated_blk_sz = counters_per_block * sizeof(uint64_t);
   struct pan_kmod_perf_sample_section *sections =
      (struct pan_kmod_perf_sample_section *)layout->sections;

   if (!blk_idx)
      layout->block_type_offset[blk_type] = layout->consolidated_sample_size;

   sections[layout->section_count++] = (struct pan_kmod_perf_sample_section){
      .block.type = blk_type,
      .block.index = blk_idx,
      .hw_sample_offset = *hw_sample_offset,
      .consolidated_sample_offset = layout->consolidated_sample_size,
   };

   layout->consolidated_sample_size += consolidated_blk_sz;
   *hw_sample_offset += hw_blk_sz;
}

static inline void
pan_kmod_perf_sample_layout_add_shader_core_sections(
   struct pan_kmod_dev *dev, struct pan_kmod_perf_sample_layout *layout,
   uint32_t *hw_sample_offset)
{
   uint32_t arch = pan_arch(dev->props.gpu_id);
   bool supports_virtual_sc = arch >= 14;
   uint64_t shader_present = dev->props.shader_present;
   uint32_t counters_per_block = pan_query_perf_counter_per_block(&dev->props);
   uint32_t hw_blk_sz = counters_per_block * sizeof(uint32_t);

   for (uint32_t phys_sc = 0, virt_sc = 0; phys_sc < 64; phys_sc++) {
      if (!(shader_present & BITFIELD64_BIT(phys_sc))) {
         if (!supports_virtual_sc)
            *hw_sample_offset += hw_blk_sz;

         continue;
      }

      pan_kmod_perf_sample_layout_add_section(
         dev, layout, MALI_PERF_BLOCK_SHADER_CORE, virt_sc++, hw_sample_offset);
   }
}

static inline void
pan_kmod_perf_sample_layout_add_memsys_sections(
   struct pan_kmod_dev *dev, struct pan_kmod_perf_sample_layout *layout,
   uint32_t *hw_sample_offset)
{
   uint32_t l2_slice_count = pan_query_l2_slices(&dev->props);

   for (uint32_t l2_slice = 0; l2_slice < l2_slice_count; l2_slice++) {
      pan_kmod_perf_sample_layout_add_section(
         dev, layout, MALI_PERF_BLOCK_MEMSYS, l2_slice, hw_sample_offset);
   }
}

static inline int
pan_kmod_perf_consolidated_sample_init(
   struct pan_kmod_dev *dev, const struct pan_kmod_perf_sample_layout *layout,
   struct pan_kmod_perf_consolidated_sample *consolidated_sample)
{
   *consolidated_sample = (struct pan_kmod_perf_consolidated_sample){
      .data = pan_kmod_dev_alloc(dev, layout->consolidated_sample_size),
   };

   if (!consolidated_sample->data)
      return -1;

   return 0;
}

static inline void
pan_kmod_perf_consolidated_sample_cleanup(
   struct pan_kmod_dev *dev,
   struct pan_kmod_perf_consolidated_sample *consolidated_sample)
{
   if (consolidated_sample->data)
      pan_kmod_dev_free(dev, consolidated_sample->data);
}

static inline int
pan_kmod_perf_dumped_sample_init(
   struct pan_kmod_dev *dev, const struct pan_kmod_perf_sample_layout *layout,
   struct pan_kmod_perf_dumped_sample *dumped_sample)
{
   *dumped_sample = (struct pan_kmod_perf_dumped_sample){
      .data = pan_kmod_dev_alloc(dev, layout->consolidated_sample_size),
   };

   if (!dumped_sample->data)
      return -1;

   return 0;
}

static inline void
pan_kmod_perf_dumped_sample_cleanup(
   struct pan_kmod_dev *dev, struct pan_kmod_perf_dumped_sample *dumped_sample)
{
   if (dumped_sample->data)
      pan_kmod_dev_free(dev, dumped_sample->data);
}

static inline void
pan_kmod_perf_consolidate_sample(
   struct pan_kmod_dev *dev, const struct pan_kmod_perf_sample_layout *layout,
   void *hw_sample,
   struct pan_kmod_perf_consolidated_sample *consolidated_sample)
{
   uint32_t counters_per_block = pan_query_perf_counter_per_block(&dev->props);

   /* First we update the consolidated sample. */
   for (uint32_t s = 0; s < layout->section_count; s++) {
      const struct pan_kmod_perf_sample_section *section = &layout->sections[s];
      uint64_t *out =
         consolidated_sample->data + section->consolidated_sample_offset;
      uint32_t *in = hw_sample + section->hw_sample_offset;
      uint64_t gpu_ts = in[0] | ((uint64_t)in[1]) << 32;
      uint32_t en_mask = in[2];

      /* Update sample time span. */
      if (en_mask && gpu_ts) {
         if (!consolidated_sample->time_span.start_gpu_ts ||
             consolidated_sample->time_span.start_gpu_ts > gpu_ts)
            consolidated_sample->time_span.start_gpu_ts = gpu_ts;

         if (consolidated_sample->time_span.end_gpu_ts < gpu_ts)
            consolidated_sample->time_span.end_gpu_ts = gpu_ts;
      }

      /* Reset the TIMESTAMP to make sure we don't read stale data next time. */
      in[0] = 0;
      in[1] = 0;

      /* Reset PERFCNT_EN to acknowledge the processining of this sample. */
      in[2] = 0;

      /* Accumulate all the counters starting at offset 4. */
      for (uint32_t c = 4; c < counters_per_block; c += 4) {
         if ((en_mask & BITFIELD_BIT(c / 4))) {
            out[c] += in[c];
            out[c + 1] += in[c + 1];
            out[c + 2] += in[c + 2];
            out[c + 3] += in[c + 3];
         }
      }
   }
}

static inline struct mali_perf_dump_info
pan_kmod_perf_dump_sample(
   struct pan_kmod_dev *dev,
   const struct pan_kmod_perf_sample_layout *layout,
   struct pan_kmod_perf_consolidated_sample *consolidated_sample,
   struct pan_kmod_perf_dumped_sample *dumped_sample)
{
   uint64_t start_gpu_ts, end_gpu_ts;

   memcpy(dumped_sample->data, consolidated_sample->data,
          layout->consolidated_sample_size);
   start_gpu_ts = consolidated_sample->time_span.start_gpu_ts;
   end_gpu_ts = consolidated_sample->time_span.end_gpu_ts;
   memset(consolidated_sample->data, 0, layout->consolidated_sample_size);

   /* Counters start accumulating again after the last sample. */
   consolidated_sample->time_span.start_gpu_ts = end_gpu_ts;

   return (struct mali_perf_dump_info){
      /* time_span is in cycles, turn it into nanoseconds. */
      .time_span = {
         .start_ns = pan_kmod_timestamp_cycles_to_ns(dev, start_gpu_ts),
         .end_ns = pan_kmod_timestamp_cycles_to_ns(dev, end_gpu_ts),
      },
   };
}
