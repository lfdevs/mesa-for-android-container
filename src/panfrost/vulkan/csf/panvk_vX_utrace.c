/*
 * Copyright 2024 Google LLC
 * Copyright 2025 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_utrace.h"

#include "util/os_misc.h"

#include "drm-uapi/panthor_drm.h"

#include "genxml/cs_builder.h"
#include "panvk_cmd_buffer.h"
#include "panvk_device.h"

static void
cmd_write_timestamp(const struct panvk_device *dev, struct cs_builder *b,
                    uint64_t addr, struct cs_async_op ts_async_op)
{
   /* Unless we dedicate a register, this will potentially overwrite something
    * during begin/end. */
   const struct cs_index addr_reg =
      cs_scratch_reg64(b, CS_REG_SCRATCH_COUNT - 2);

   /* Overwrite the signal_slot. Note that this has no effect in case of
    * synchronous or indirect syncs. */
   assert(!ts_async_op.wait_mask ||
#if PAN_ARCH >= 11
          ts_async_op.indirect ||
#endif
          ts_async_op.signal_slot == 0);
   /* abuse DEFERRED_SYNC */
   ts_async_op.signal_slot = SB_ID(DEFERRED_SYNC);

   cs_move64_to(b, addr_reg, addr);
   cs_store_state(b, addr_reg, 0, MALI_CS_STATE_TIMESTAMP, ts_async_op);
}

static void
cmd_copy_data(struct cs_builder *b, uint64_t dst_addr, uint64_t src_addr,
              uint32_t size, bool wait_for_timestamp)
{
   assert((dst_addr | src_addr | size) % sizeof(uint32_t) == 0);

   if (wait_for_timestamp)
      cs_wait_slot(b, SB_ID(DEFERRED_SYNC));

   /* Depending on where this is called from, we could potentially use SR
    * registers or copy with a compute job.
    */
   const struct cs_index dst_addr_reg = cs_scratch_reg64(b, 0);
   const struct cs_index src_addr_reg = cs_scratch_reg64(b, 2);
   const uint32_t temp_count =
      MIN2(CS_REG_SCRATCH_COUNT - 4, CS_MAX_REG_TUPLE_SIZE);

   while (size) {
      cs_move64_to(b, dst_addr_reg, dst_addr);
      cs_move64_to(b, src_addr_reg, src_addr);

      const uint32_t max_offset = 1 << 16;
      uint32_t copy_count = MIN2(size, max_offset) / sizeof(uint32_t);
      uint32_t offset = 0;
      while (copy_count) {
         const uint32_t count = MIN2(copy_count, temp_count);
         const struct cs_index reg = cs_scratch_reg_tuple(b, 4, count);

         cs_load_to(b, reg, src_addr_reg, BITFIELD_MASK(count), offset);
         cs_wait_slot(b, SB_ID(LS));
         cs_store(b, reg, dst_addr_reg, BITFIELD_MASK(count), offset);

         copy_count -= count;
         offset += count * sizeof(uint32_t);
      }

      dst_addr += offset;
      src_addr += offset;
      size -= offset;
   }
}

static void
cmd_store_regs(struct cs_builder *b, uint64_t dst_addr, uint64_t src_addr,
               uint32_t size, bool wait_for_timestamp)
{
   assert((dst_addr | size) % sizeof(uint32_t) == 0);
   uint32_t num_regs = size / sizeof(uint32_t);
   assert(num_regs <= CS_MAX_REG_TUPLE_SIZE);
   assert(src_addr + num_regs <= PANVK_CS_REG_SCRATCH_END);

   if (wait_for_timestamp)
      cs_wait_slot(b, SB_ID(DEFERRED_SYNC));

   bool valid_dst_regs = false;
   struct cs_index dst_addr_reg;
   /* Unless we dedicate a register, this will potentially overwrite
    * something during indirect capture. For now, we only ensure we don't
    * corrupt the registers we're capturing. */
   for (uint32_t dst_scratch_base = PANVK_CS_REG_SCRATCH_END - 1;
        dst_scratch_base >= PANVK_CS_REG_SCRATCH_START; dst_scratch_base -= 2) {
      if (src_addr + num_regs <= dst_scratch_base ||
          dst_scratch_base + 2 <= src_addr) {
         dst_addr_reg =
            cs_scratch_reg64(b, dst_scratch_base - PANVK_CS_REG_SCRATCH_START);
         valid_dst_regs = true;
         break;
      }
   }

   if (!valid_dst_regs) {
      assert(!"No unused scratch registers found");
      return;
   }

   const struct cs_index src_addr_reg = cs_reg_tuple(b, src_addr, num_regs);
   cs_move64_to(b, dst_addr_reg, dst_addr);
   cs_store(b, src_addr_reg, dst_addr_reg, BITFIELD_MASK(num_regs), 0);
}

static struct cs_builder *
get_builder(struct panvk_cmd_buffer *cmdbuf, struct u_trace *ut)
{
   const uint32_t subqueue = ut - cmdbuf->utrace.uts;
   assert(subqueue < PANVK_SUBQUEUE_COUNT);

   return panvk_get_cs_builder(cmdbuf, subqueue);
}

static void
panvk_utrace_record_ts(struct u_trace *ut, void *cs, void *timestamps,
                       uint64_t offset_B, uint32_t flags)
{
   /* Here the input type for void *cs is panvk_utrace_cs_info instead of
    * panvk_cmd_buffer so we can pass additional parameters. */
   struct panvk_utrace_cs_info *cs_info = cs;
   struct panvk_cmd_buffer *cmdbuf = cs_info->cmdbuf;
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct cs_builder *b = get_builder(cmdbuf, ut);
   const struct panvk_utrace_buf *buf = timestamps;
   const uint64_t addr = buf->dev + offset_B;

   cmd_write_timestamp(dev, b, addr, *cs_info->ts_async_op);
}

static void
panvk_utrace_capture_data(struct u_trace *ut, void *cs, void *dst_buffer,
                          uint64_t dst_offset_B, void *src_buffer,
                          uint64_t src_offset_B, uint32_t size_B)
{
   /* Here the input type for void *cs is panvk_utrace_cs_info instead of
    * panvk_cmd_buffer so we can pass additional parameters. */
   struct panvk_utrace_cs_info *cs_info = cs;
   struct cs_builder *b = get_builder(cs_info->cmdbuf, ut);
   const struct panvk_utrace_buf *dst_buf = dst_buffer;
   const uint64_t dst_addr = dst_buf->dev + dst_offset_B;
   const uint64_t src_addr = src_offset_B;

   /* src_offset_B is absolute, src_buffer is used to indicate register capture */
   assert(!src_buffer ||
          (uintptr_t)src_buffer == PANVK_UTRACE_CAPTURE_REGISTERS);

   if ((uintptr_t)src_buffer == PANVK_UTRACE_CAPTURE_REGISTERS)
      cmd_store_regs(b, dst_addr, src_addr, size_B,
                     cs_info->capture_data_wait_for_ts);
   else
      cmd_copy_data(b, dst_addr, src_addr, size_B,
                    cs_info->capture_data_wait_for_ts);
}

static uint32_t
get_utrace_clone_mem_size()
{
   return debug_get_num_option("PANVK_UTRACE_CLONE_MEM_SIZE", 0xa00000);
}

VkResult
panvk_per_arch(utrace_context_init)(struct panvk_device *dev)
{
   u_trace_context_init(&dev->utrace.utctx, dev, sizeof(uint64_t),
                        sizeof(VkDispatchIndirectCommand),
                        panvk_utrace_create_buffer, panvk_utrace_delete_buffer,
                        panvk_utrace_record_ts, panvk_utrace_read_ts,
                        panvk_utrace_capture_data, panvk_utrace_get_data,
                        panvk_utrace_delete_flush_data);

   VkResult result = panvk_priv_bo_create(dev, get_utrace_clone_mem_size(), 0,
                                          VK_SYSTEM_ALLOCATION_SCOPE_OBJECT,
                                          &dev->utrace.copy_buf_heap_bo);
   if (result != VK_SUCCESS) {
      u_trace_context_fini(&dev->utrace.utctx);
      return result;
   }

   simple_mtx_init(&dev->utrace.copy_buf_heap_lock, mtx_plain);

   simple_mtx_lock(&dev->utrace.copy_buf_heap_lock);
   util_vma_heap_init(&dev->utrace.copy_buf_heap,
                      dev->utrace.copy_buf_heap_bo->addr.dev,
                      dev->utrace.copy_buf_heap_bo->bo->size);
   simple_mtx_unlock(&dev->utrace.copy_buf_heap_lock);

   return VK_SUCCESS;
}

void
panvk_per_arch(utrace_context_fini)(struct panvk_device *dev)
{
   u_trace_context_fini(&dev->utrace.utctx);

   simple_mtx_lock(&dev->utrace.copy_buf_heap_lock);
   util_vma_heap_finish(&dev->utrace.copy_buf_heap);
   simple_mtx_unlock(&dev->utrace.copy_buf_heap_lock);

   panvk_priv_bo_unref(dev->utrace.copy_buf_heap_bo);

   simple_mtx_destroy(&dev->utrace.copy_buf_heap_lock);
}

void
panvk_per_arch(utrace_copy_buffer)(struct u_trace_context *utctx,
                                   void *cmdstream, void *ts_from,
                                   uint64_t from_offset, void *ts_to,
                                   uint64_t to_offset, uint64_t size_B)
{
   struct cs_builder *b = cmdstream;
   const struct panvk_utrace_buf *src_buf = ts_from;
   const struct panvk_utrace_buf *dst_buf = ts_to;
   const uint64_t src_addr = src_buf->dev + from_offset;
   const uint64_t dst_addr = dst_buf->dev + to_offset;

   cmd_copy_data(b, dst_addr, src_addr, size_B, false);
}

void
panvk_per_arch(utrace_clone_init_builder)(struct cs_builder *b,
                                          struct panvk_device *dev,
                                          const struct cs_buffer *cs_root)
{
   const struct drm_panthor_csif_info *csif_info =
      panthor_kmod_get_csif_props(dev->kmod.dev);
   const struct cs_builder_conf builder_conf = {
      .nr_registers = csif_info->cs_reg_count,
      .nr_kernel_registers = MAX2(csif_info->unpreserved_cs_reg_count, 4),
      .ls_sb_slot = SB_ID(LS),
   };
   cs_builder_init(b, &builder_conf, *cs_root);
}

void
panvk_per_arch(utrace_clone_finish_builder)(struct cs_builder *b)
{
   const struct cs_index flush_id = cs_scratch_reg32(b, 0);

   cs_move32_to(b, flush_id, 0);
   cs_flush_caches(b, MALI_CS_FLUSH_MODE_CLEAN, MALI_CS_FLUSH_MODE_NONE,
                   MALI_CS_OTHER_FLUSH_MODE_NONE, flush_id,
                   cs_defer(SB_IMM_MASK, SB_ID(IMM_FLUSH)));
   cs_wait_slot(b, SB_ID(IMM_FLUSH));

   cs_finish(b);
}
