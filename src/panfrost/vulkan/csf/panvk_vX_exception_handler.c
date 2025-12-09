/*
 * Copyright © 2024 Collabora Ltd.
 * Copyright © 2024 Arm Ltd.
 *
 * SPDX-License-Identifier: MIT
 */
#include "drm-uapi/panthor_drm.h"

#include "panvk_cmd_buffer.h"
#include "panvk_device.h"

static enum cs_reg_perm
tiler_oom_reg_perm_cb(struct cs_builder *b, unsigned reg)
{
   switch (reg) {
   /* The bbox is set up by the fragment subqueue, we should not modify it. */
   case 42:
   case 43:
   /* We should only load from the subqueue context. */
   case PANVK_CS_REG_SUBQUEUE_CTX_START:
   case PANVK_CS_REG_SUBQUEUE_CTX_END:
      return CS_REG_RD;
   }
   return CS_REG_RW;
}

void
panvk_per_arch(cs_patch_ir_state)(
   struct cs_builder *b, const struct cs_tracing_ctx *tracing_ctx,
   bool has_zs_ext, uint32_t rt_count, struct cs_index remaining_layers_in_td,
   struct cs_index current_fbd_ptr_reg, struct cs_index ir_desc_info_ptr,
   struct cs_index ir_fbd_word_0, struct cs_index scratch_fbd_ptr_reg,
   struct cs_index scratch_registers_5)
{
   assert(scratch_registers_5.type == CS_INDEX_REGISTER &&
          "invalid register type");
   assert(scratch_registers_5.size == 5 && "scratch register size must be 5");

   const uint32_t fbd_size = get_fbd_size(has_zs_ext, rt_count);

   /* Calculate the *used* ir_desc_infos size */
   const uint32_t used_ir_desc_info_size = sizeof(struct panvk_ir_fbd_info) +
                                           (has_zs_ext ? sizeof(uint32_t) : 0) +
                                           rt_count * sizeof(uint32_t);

   struct cs_index copy_fbd_staging_regs =
      cs_extract_tuple(b, scratch_registers_5, 0, 4);
   struct cs_index copy_fbd_dword_6_reg =
      cs_extract64(b, scratch_registers_5, 0);
   struct cs_index copy_fbd_word_reg = cs_extract32(b, scratch_registers_5, 2);
   struct cs_index fbd_offset_reg = cs_extract32(b, scratch_registers_5, 4);

   /* Copy fbd+dbd+rtds to scratch */
   {
      /* Our loop is copying 16 bytes at a time, so make sure the
       * fbd_size is aligned to 16 bytes. */
      const int32_t copy_stride = 16;
      assert(fbd_size == ALIGN_POT(fbd_size, copy_stride));

      /* Copy the current FBD in full to the FBD scratch */
      cs_move32_to(b, fbd_offset_reg, fbd_size);
      cs_while(b, MALI_CS_CONDITION_GREATER, fbd_offset_reg) {
         cs_add32(b, fbd_offset_reg, fbd_offset_reg, -copy_stride);

         cs_load_to(b, copy_fbd_staging_regs, current_fbd_ptr_reg,
                    BITFIELD_MASK(4), 0);
         cs_store(b, copy_fbd_staging_regs, scratch_fbd_ptr_reg,
                  BITFIELD_MASK(4), 0);

         cs_add64(b, current_fbd_ptr_reg, current_fbd_ptr_reg, copy_stride);
         cs_add64(b, scratch_fbd_ptr_reg, scratch_fbd_ptr_reg, copy_stride);
      }

      /* Move scratch FBD ptr back to FBD base */
      cs_add64(b, scratch_fbd_ptr_reg, scratch_fbd_ptr_reg, -fbd_size);

      /* Patch FBD for IR */
      {
         /* Load word 12 and dword 6 */
         cs_load64_to(b, copy_fbd_dword_6_reg, ir_desc_info_ptr,
                      offsetof(struct panvk_ir_desc_info, fbd.word6));
         cs_load32_to(b, copy_fbd_word_reg, ir_desc_info_ptr,
                      offsetof(struct panvk_ir_desc_info, fbd.word12));
         cs_store32(b, ir_fbd_word_0, scratch_fbd_ptr_reg, 0 * 4);
         cs_store64(b, copy_fbd_dword_6_reg, scratch_fbd_ptr_reg, 6 * 4);
         cs_store32(b, copy_fbd_word_reg, scratch_fbd_ptr_reg, 12 * 4);
      }

      /* Move fbd and info ptr past base fbd */
      cs_add64(b, ir_desc_info_ptr, ir_desc_info_ptr,
               sizeof(struct panvk_ir_fbd_info));
      cs_add64(b, scratch_fbd_ptr_reg, scratch_fbd_ptr_reg,
               pan_size(FRAMEBUFFER));

      /* If the IR FBD has crc zs ext descriptor, then copy word0 from it
       * to scratch */
      struct cs_index has_zs_ext_reg = copy_fbd_word_reg;
      cs_move32_to(b, has_zs_ext_reg, has_zs_ext);
      /* Use cs_if for this as the exception handler excepts each instance of
       * tiler_oom_handler to be of the same size */
      cs_if(b, MALI_CS_CONDITION_GREATER, has_zs_ext_reg) {
         cs_load32_to(b, copy_fbd_word_reg, ir_desc_info_ptr, 0 * 4);
         cs_store32(b, copy_fbd_word_reg, scratch_fbd_ptr_reg, 0 * 4);

         /* Move fbd ptr past crc zs ext */
         cs_add64(b, scratch_fbd_ptr_reg, scratch_fbd_ptr_reg,
                  pan_size(ZS_CRC_EXTENSION));
      }

      /* Always move ir info ptr past crc zs ext  */
      cs_add64(b, ir_desc_info_ptr, ir_desc_info_ptr, sizeof(uint32_t));

      /* Loop to copy IR RTD's word1 */
      struct cs_index rt_count_reg = fbd_offset_reg;
      cs_move32_to(b, rt_count_reg, rt_count);
      cs_while(b, MALI_CS_CONDITION_GREATER, rt_count_reg) {
         cs_add32(b, rt_count_reg, rt_count_reg, -1);

         cs_load32_to(b, copy_fbd_word_reg, ir_desc_info_ptr, 0 * 4);
         cs_store32(b, copy_fbd_word_reg, scratch_fbd_ptr_reg, 1 * 4);

         /* Move fbd and info ptr past current RT  */
         cs_add64(b, ir_desc_info_ptr, ir_desc_info_ptr, sizeof(uint32_t));
         cs_add64(b, scratch_fbd_ptr_reg, scratch_fbd_ptr_reg,
                  pan_size(RENDER_TARGET));
      }

      cs_add64(b, ir_desc_info_ptr, ir_desc_info_ptr, -used_ir_desc_info_size);
      cs_add64(b, scratch_fbd_ptr_reg, scratch_fbd_ptr_reg, -fbd_size);
      cs_add64(b, current_fbd_ptr_reg, current_fbd_ptr_reg, -fbd_size);
      cs_flush_stores(b);
   }
}

void
panvk_per_arch(cs_ir_update_registers_to_next_layer)(
   struct cs_builder *b, bool has_zs_ext, uint32_t rt_count,
   struct cs_index current_fbd_ptr_reg, struct cs_index ir_fbd_word_0,
   struct cs_index remaining_layers_in_td)
{
   const uint32_t fbd_size = get_fbd_size(has_zs_ext, rt_count);
   cs_add64(b, current_fbd_ptr_reg, current_fbd_ptr_reg, fbd_size);

   cs_add32(b, ir_fbd_word_0, ir_fbd_word_0, 1 << 24);

   /* Set remaining_layers_in_td to zero if reached td limit */
   cs_add32(b, remaining_layers_in_td, remaining_layers_in_td, -1);
   cs_if(b, MALI_CS_CONDITION_LEQUAL, remaining_layers_in_td) {
      cs_add32(b, ir_fbd_word_0, ir_fbd_word_0,
               -(1 << 24) * MAX_LAYERS_PER_TILER_DESC);
      cs_move32_to(b, remaining_layers_in_td, MAX_LAYERS_PER_TILER_DESC);
   }
}

static size_t
generate_tiler_oom_handler(struct panvk_device *dev,
                           struct cs_buffer handler_mem, bool has_zs_ext,
                           uint32_t rt_count, bool tracing_enabled,
                           uint32_t *dump_region_size)
{
   assert(rt_count >= 1 && rt_count <= MAX_RTS);

   uint32_t ir_desc_info_size = sizeof(struct panvk_ir_desc_info);

   const struct drm_panthor_csif_info *csif_info =
      panthor_kmod_get_csif_props(dev->kmod.dev);

   struct cs_builder b;
   struct cs_builder_conf conf = {
      .nr_registers = csif_info->cs_reg_count,
      .nr_kernel_registers = MAX2(csif_info->unpreserved_cs_reg_count, 4),
      .reg_perm = tiler_oom_reg_perm_cb,
      .ls_sb_slot = SB_ID(LS),
   };
   cs_builder_init(&b, &conf, handler_mem);

   struct cs_function handler;
   struct cs_function_ctx handler_ctx = {
      .ctx_reg = cs_subqueue_ctx_reg(&b),
      .dump_addr_offset =
         offsetof(struct panvk_cs_subqueue_context, reg_dump_addr),
   };
   struct cs_tracing_ctx tracing_ctx = {
      .enabled = tracing_enabled,
      .ctx_reg = cs_subqueue_ctx_reg(&b),
      .tracebuf_addr_offset =
         offsetof(struct panvk_cs_subqueue_context, debug.tracebuf.cs),
   };

   cs_function_def(&b, &handler, handler_ctx) {
      struct cs_index subqueue_ctx = cs_subqueue_ctx_reg(&b);

      struct cs_index zero = cs_scratch_reg64(&b, 0);
      /* Have flush_id read part of the double zero register */
      struct cs_index flush_id = cs_scratch_reg32(&b, 0);

      struct cs_index completed_chunks = cs_scratch_reg_tuple(&b, 2, 4);
      struct cs_index completed_top = cs_scratch_reg64(&b, 2);
      struct cs_index completed_bottom = cs_scratch_reg64(&b, 4);
      struct cs_index td_count = cs_scratch_reg32(&b, 6);

      /* Counter is used early before any over lap registers are used */
      struct cs_index counter = cs_scratch_reg32(&b, 0);

      /* FBD patching registers */
      struct cs_index scratch_regs = cs_scratch_reg_tuple(&b, 2, 5);
      struct cs_index layer_count = cs_scratch_reg32(&b, 7);
      struct cs_index ir_fbd_word_0 = cs_scratch_reg32(&b, 8);
      struct cs_index remaining_layers_in_td = cs_scratch_reg32(&b, 9);
      struct cs_index scratch_fbd_ptr_reg = cs_scratch_reg64(&b, 10);
      struct cs_index current_fbd_ptr_reg = cs_scratch_reg64(&b, 12);
      struct cs_index ir_desc_info_ptr = cs_scratch_reg64(&b, 14);

      /* Run fragment registers will only be used after FBD patching */
      struct cs_index run_fragment_regs = cs_scratch_reg_tuple(&b, 0, 4);

      /* The tiler pointer is pre-filled. */
      struct cs_index tiler_ptr = cs_reg64(&b, 38);

      cs_load64_to(&b, scratch_fbd_ptr_reg, subqueue_ctx,
                   TILER_OOM_CTX_FIELD_OFFSET(ir_scratch_fbd_ptr));
      cs_load32_to(&b, counter, subqueue_ctx,
                   TILER_OOM_CTX_FIELD_OFFSET(counter));
      cs_load32_to(&b, layer_count, subqueue_ctx,
                   TILER_OOM_CTX_FIELD_OFFSET(layer_count));
      cs_load64_to(&b, current_fbd_ptr_reg, subqueue_ctx,
                   TILER_OOM_CTX_FIELD_OFFSET(layer_fbd_ptr));

      cs_add64(&b, ir_desc_info_ptr, subqueue_ctx,
               TILER_OOM_CTX_FIELD_OFFSET(ir_desc_infos));
      cs_move32_to(&b, remaining_layers_in_td, MAX_LAYERS_PER_TILER_DESC);

      /* Move FBD pointer to the scratch fbd */
      cs_add64(&b, cs_sr_reg64(&b, FRAGMENT, FBD_POINTER), scratch_fbd_ptr_reg,
               0);

      /* Use different framebuffer descriptor depending on whether incremental
       * rendering has already been triggered */
      cs_if(&b, MALI_CS_CONDITION_GREATER, counter) {
         cs_add64(&b, ir_desc_info_ptr, ir_desc_info_ptr,
                  ir_desc_info_size * PANVK_IR_MIDDLE_PASS);
      }

      cs_load32_to(&b, ir_fbd_word_0, ir_desc_info_ptr,
                   offsetof(struct panvk_ir_desc_info, fbd.word0));

      /* Increment counter */
      cs_add32(&b, counter, counter, 1);
      cs_store32(&b, counter, subqueue_ctx,
                 TILER_OOM_CTX_FIELD_OFFSET(counter));

      cs_wait_slot(&b, SB_ID(LS));

      cs_while(&b, MALI_CS_CONDITION_GREATER, layer_count) {
         cs_add32(&b, layer_count, layer_count, -1);

         panvk_per_arch(cs_patch_ir_state)(
            &b, &tracing_ctx, has_zs_ext, rt_count, remaining_layers_in_td,
            current_fbd_ptr_reg, ir_desc_info_ptr, ir_fbd_word_0,
            scratch_fbd_ptr_reg, scratch_regs);

         cs_trace_run_fragment(&b, &tracing_ctx, run_fragment_regs, false,
                               MALI_TILE_RENDER_ORDER_Z_ORDER);

         panvk_per_arch(cs_ir_update_registers_to_next_layer)(
            &b, has_zs_ext, rt_count, current_fbd_ptr_reg, ir_fbd_word_0,
            remaining_layers_in_td);

         /* Serialize run fragments since we reuse FBD for the runs */
         cs_wait_slots(&b, dev->csf.sb.all_iters_mask);
      }

      cs_load32_to(&b, td_count, subqueue_ctx,
                   TILER_OOM_CTX_FIELD_OFFSET(td_count));
      cs_move64_to(&b, zero, 0);

      cs_while(&b, MALI_CS_CONDITION_GREATER, td_count) {
         /* Load completed chunks */
         cs_load_to(&b, completed_chunks, tiler_ptr, BITFIELD_MASK(4), 10 * 4);

         cs_finish_fragment(&b, false, completed_top, completed_bottom,
                            cs_now());

         /* Zero out polygon list, completed_top and completed_bottom */
         cs_store64(&b, zero, tiler_ptr, 0);
         cs_store64(&b, zero, tiler_ptr, 10 * 4);
         cs_store64(&b, zero, tiler_ptr, 12 * 4);

         cs_add64(&b, tiler_ptr, tiler_ptr, pan_size(TILER_CONTEXT));
         cs_add32(&b, td_count, td_count, -1);
      }

      /* We need to flush the texture caches so future preloads see the new
       * content. */
      cs_flush_caches(&b, MALI_CS_FLUSH_MODE_NONE, MALI_CS_FLUSH_MODE_NONE,
                      MALI_CS_OTHER_FLUSH_MODE_INVALIDATE, flush_id,
                      cs_defer(SB_IMM_MASK, SB_ID(IMM_FLUSH)));

      cs_wait_slot(&b, SB_ID(IMM_FLUSH));
   }

   assert(cs_is_valid(&b));
   cs_finish(&b);
   *dump_region_size = handler.dump_size;

   return handler.length * sizeof(uint64_t);
}

#define TILER_OOM_HANDLER_MAX_SIZE 1024
VkResult
panvk_per_arch(init_tiler_oom)(struct panvk_device *device)
{
   const bool tracing_enabled = PANVK_DEBUG(TRACE);
   VkResult result = panvk_priv_bo_create(
      device, TILER_OOM_HANDLER_MAX_SIZE * 2 * MAX_RTS, 0,
      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE, &device->tiler_oom.handlers_bo);
   if (result != VK_SUCCESS)
      return result;

   for (uint32_t zs_ext = 0; zs_ext <= 1; zs_ext++) {
      for (uint32_t rt_count = 1; rt_count <= MAX_RTS; rt_count++) {
         uint32_t idx = get_tiler_oom_handler_idx(zs_ext, rt_count);
         /* Check that we have calculated a handler_stride if we need it to
          * offset addresses. */
         assert(idx == 0 || device->tiler_oom.handler_stride != 0);
         size_t offset = idx * device->tiler_oom.handler_stride;

         struct cs_buffer handler_mem = {
            .cpu = device->tiler_oom.handlers_bo->addr.host + offset,
            .gpu = device->tiler_oom.handlers_bo->addr.dev + offset,
            .capacity = TILER_OOM_HANDLER_MAX_SIZE / sizeof(uint64_t),
         };

         uint32_t dump_region_size;
         size_t handler_length =
            generate_tiler_oom_handler(device, handler_mem, zs_ext, rt_count,
                                       tracing_enabled, &dump_region_size);

         /* All handlers must have the same length */
         assert(idx == 0 || handler_length == device->tiler_oom.handler_stride);
         device->tiler_oom.handler_stride = handler_length;
         device->dump_region_size[PANVK_SUBQUEUE_FRAGMENT] =
            MAX2(device->dump_region_size[PANVK_SUBQUEUE_FRAGMENT],
                 dump_region_size);
      }
   }

   return result;
}
