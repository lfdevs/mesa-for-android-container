/*
 * Copyright © 2024 Collabora Ltd.
 * Copyright © 2024 Arm Ltd.
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

static void
copy_fbd(struct cs_builder *b, bool has_zs_ext, uint32_t rt_count,
         struct cs_index src_tiler, struct cs_index src_other,
         struct cs_index dst)
{
   /* Copy the FBD from src to dst. Most words come from
    * src_other, but the tiler desc pointer is taken from src_tiler.
    */
   cs_load_to(b, cs_scratch_reg_tuple(b, 0, 8), src_other,
              BITFIELD_MASK(8), 0);
   cs_store(b, cs_scratch_reg_tuple(b, 0, 8), dst,
            BITFIELD_MASK(8), 0);
   cs_load_to(b, cs_scratch_reg_tuple(b, 0, 6), src_other,
              BITFIELD_MASK(6), 8 * sizeof(uint32_t));
   cs_load64_to(b, cs_scratch_reg64(b, 6), src_tiler,
                14 * sizeof(uint32_t));
   cs_store(b, cs_scratch_reg_tuple(b, 0, 8), dst, BITFIELD_MASK(8),
            8 * sizeof(uint32_t));

   if (has_zs_ext) {
      const uint16_t dbd_offset = sizeof(struct mali_framebuffer_packed);

      /* Copy the whole DBD. */
      cs_load_to(b, cs_scratch_reg_tuple(b, 0, 8), src_other,
                 BITFIELD_MASK(8), dbd_offset);
      cs_store(b, cs_scratch_reg_tuple(b, 0, 8), dst,
               BITFIELD_MASK(8), dbd_offset);
      cs_load_to(b, cs_scratch_reg_tuple(b, 0, 8), src_other,
                 BITFIELD_MASK(8), dbd_offset + (8 * sizeof(uint32_t)));
      cs_store(b, cs_scratch_reg_tuple(b, 0, 8), dst,
               BITFIELD_MASK(8), dbd_offset + (8 * sizeof(uint32_t)));
   }

   const uint16_t rts_offset =
      sizeof(struct mali_framebuffer_packed) +
      (has_zs_ext ? sizeof(struct mali_zs_crc_extension_packed) : 0);

   for (uint32_t rt = 0; rt < rt_count; rt++) {
      const uint16_t rt_offset =
         rts_offset + (rt * sizeof(struct mali_render_target_packed));

      /* Copy the whole RTD. */
      cs_load_to(b, cs_scratch_reg_tuple(b, 0, 8), src_other,
                 BITFIELD_MASK(8), rt_offset);
      cs_store(b, cs_scratch_reg_tuple(b, 0, 8), dst,
               BITFIELD_MASK(8), rt_offset);
      cs_load_to(b, cs_scratch_reg_tuple(b, 0, 8), src_other,
                 BITFIELD_MASK(8), rt_offset + (8 * sizeof(uint32_t)));
      cs_store(b, cs_scratch_reg_tuple(b, 0, 8), dst,
               BITFIELD_MASK(8), rt_offset + (8 * sizeof(uint32_t)));
   }
}

static size_t
generate_tiler_oom_handler(struct panvk_device *dev,
                           struct cs_buffer handler_mem, bool has_zs_ext,
                           uint32_t rt_count, bool tracing_enabled,
                           uint32_t *dump_region_size)
{
   assert(rt_count >= 1 && rt_count <= MAX_RTS);

   const uint32_t fbd_size = get_fbd_size(has_zs_ext, rt_count);

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
   struct mali_framebuffer_pointer_packed fb_tag;

   pan_pack(&fb_tag, FRAMEBUFFER_POINTER, cfg) {
      cfg.zs_crc_extension_present = has_zs_ext;
      cfg.render_target_count = rt_count;
   }

   cs_function_def(&b, &handler, handler_ctx) {
      struct cs_index subqueue_ctx = cs_subqueue_ctx_reg(&b);

      struct cs_index zero = cs_scratch_reg64(&b, 0);
      /* Have flush_id read part of the double zero register */
      struct cs_index flush_id = cs_scratch_reg32(&b, 0);

      struct cs_index completed_chunks = cs_scratch_reg_tuple(&b, 2, 4);
      struct cs_index completed_top = cs_scratch_reg64(&b, 2);
      struct cs_index completed_bottom = cs_scratch_reg64(&b, 4);
      struct cs_index td_count = cs_scratch_reg32(&b, 6);

      /* FBD patching registers */
      struct cs_index layer_count = cs_scratch_reg32(&b, 8);
      struct cs_index ir_count = cs_scratch_reg32(&b, 9);
      struct cs_index scratch_fbd_ptr_reg = cs_scratch_reg64(&b, 10);
      struct cs_index current_fbd_ptr_reg = cs_scratch_reg64(&b, 12);
      struct cs_index ir_descs_ptr = cs_scratch_reg64(&b, 14);

      /* Run fragment registers will only be used after FBD patching */
      struct cs_index run_fragment_regs = cs_scratch_reg_tuple(&b, 0, 4);

      /* The tiler pointer is pre-filled. */
      struct cs_index tiler_ptr = cs_reg64(&b, 38);

      cs_load64_to(&b, scratch_fbd_ptr_reg, subqueue_ctx,
                   TILER_OOM_CTX_FIELD_OFFSET(ir_scratch_fbd_ptr));
      cs_load32_to(&b, ir_count, subqueue_ctx,
                   TILER_OOM_CTX_FIELD_OFFSET(counter));
      cs_load32_to(&b, layer_count, subqueue_ctx,
                   TILER_OOM_CTX_FIELD_OFFSET(layer_count));
      cs_load64_to(&b, current_fbd_ptr_reg, subqueue_ctx,
                   TILER_OOM_CTX_FIELD_OFFSET(layer_fbd_ptr));

      /* Use different framebuffer descriptor depending on whether incremental
       * rendering has already been triggered */
      cs_if(&b, MALI_CS_CONDITION_GREATER, ir_count) {
         cs_load64_to(
            &b, ir_descs_ptr, subqueue_ctx,
            TILER_OOM_CTX_FIELD_OFFSET(ir_descs[PANVK_IR_MIDDLE_PASS]));
      }
      cs_else(&b) {
         cs_load64_to(
            &b, ir_descs_ptr, subqueue_ctx,
            TILER_OOM_CTX_FIELD_OFFSET(ir_descs[PANVK_IR_FIRST_PASS]));
      }

      cs_wait_slot(&b, SB_ID(LS));

      cs_while(&b, MALI_CS_CONDITION_GREATER, layer_count) {
         cs_add32(&b, layer_count, layer_count, -1);

         copy_fbd(&b, has_zs_ext, rt_count, current_fbd_ptr_reg, ir_descs_ptr,
                  scratch_fbd_ptr_reg);

         /* Flush copies before the RUN_FRAGMENT. */
         cs_wait_slot(&b, SB_ID(LS));

         /* Set FBD pointer to the scratch fbd */
         cs_add64(&b, cs_sr_reg64(&b, FRAGMENT, FBD_POINTER),
                  scratch_fbd_ptr_reg, fb_tag.opaque[0]);

         cs_trace_run_fragment(&b, &tracing_ctx, run_fragment_regs, false,
                               MALI_TILE_RENDER_ORDER_Z_ORDER);

         /* Serialize run fragments since we reuse FBD for the runs */
         cs_wait_slots(&b, dev->csf.sb.all_iters_mask);

         cs_add64(&b, current_fbd_ptr_reg, current_fbd_ptr_reg, fbd_size);
         cs_add64(&b, ir_descs_ptr, ir_descs_ptr, fbd_size);
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

      /* If this is the first IR call, we need to patch the regular FBD
       * to use the last IR config.
       */
      cs_if(&b, MALI_CS_CONDITION_EQUAL, ir_count) {
         cs_load64_to(&b, current_fbd_ptr_reg, subqueue_ctx,
                      TILER_OOM_CTX_FIELD_OFFSET(layer_fbd_ptr));
         cs_load64_to(&b, ir_descs_ptr, subqueue_ctx,
                      TILER_OOM_CTX_FIELD_OFFSET(ir_descs[PANVK_IR_LAST_PASS]));
         cs_load32_to(&b, layer_count, subqueue_ctx,
                      TILER_OOM_CTX_FIELD_OFFSET(layer_count));

         cs_while(&b, MALI_CS_CONDITION_GREATER, layer_count) {
            cs_add32(&b, layer_count, layer_count, -1);

            /* Preserve the tiler pointer, take the rest from the
             * last IR config.
             */
            copy_fbd(&b, has_zs_ext, rt_count, current_fbd_ptr_reg,
                     ir_descs_ptr, current_fbd_ptr_reg);

            cs_add64(&b, current_fbd_ptr_reg, current_fbd_ptr_reg, fbd_size);
            cs_add64(&b, ir_descs_ptr, ir_descs_ptr, fbd_size);
         }
      }

      /* Increment IR counter */
      cs_add32(&b, ir_count, ir_count, 1);
      cs_store32(&b, ir_count, subqueue_ctx,
                 TILER_OOM_CTX_FIELD_OFFSET(counter));
      cs_wait_slot(&b, SB_ID(LS));

      /* We need to flush the texture caches so future preloads see the new
       * content. */
      cs_flush_caches(&b, MALI_CS_FLUSH_MODE_NONE, MALI_CS_FLUSH_MODE_NONE,
                      MALI_CS_OTHER_FLUSH_MODE_INVALIDATE, flush_id,
                      cs_defer(SB_IMM_MASK, SB_ID(IMM_FLUSH)));

      cs_wait_slot(&b, SB_ID(IMM_FLUSH));
   }

   assert(cs_is_valid(&b));
   cs_end(&b);
   cs_builder_fini(&b);
   *dump_region_size = handler.dump_size;

   return handler.length * sizeof(uint64_t);
}

#define TILER_OOM_HANDLER_MAX_SIZE 2048
VkResult
panvk_per_arch(init_tiler_oom)(struct panvk_device *device)
{
   const bool tracing_enabled = PANVK_DEBUG(TRACE);
   VkResult result = panvk_priv_bo_create(
      device, TILER_OOM_HANDLER_MAX_SIZE * 2 * MAX_RTS,
      panvk_device_adjust_bo_flags(device, PAN_KMOD_BO_FLAG_WB_MMAP),
      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE, &device->tiler_oom.handlers_bo);
   if (result != VK_SUCCESS)
      return result;

   for (uint32_t zs_ext = 0; zs_ext <= 1; zs_ext++) {
      for (uint32_t rt_count = 1; rt_count <= MAX_RTS; rt_count++) {
         uint32_t idx = get_tiler_oom_handler_idx(zs_ext, rt_count);
         size_t offset = idx * TILER_OOM_HANDLER_MAX_SIZE;

         struct cs_buffer handler_mem = {
            .cpu = device->tiler_oom.handlers_bo->addr.host + offset,
            .gpu = device->tiler_oom.handlers_bo->addr.dev + offset,
            .capacity = TILER_OOM_HANDLER_MAX_SIZE / sizeof(uint64_t),
         };

         uint32_t dump_region_size;
         size_t handler_length =
            generate_tiler_oom_handler(device, handler_mem, zs_ext, rt_count,
                                       tracing_enabled, &dump_region_size);

         /* Use memset(0) to make sure the remaining space is filled with NOP
          * instructions. */
         assert(handler_length <= TILER_OOM_HANDLER_MAX_SIZE);
         memset((uint8_t *)handler_mem.cpu + handler_length, 0,
                TILER_OOM_HANDLER_MAX_SIZE - handler_length);
         device->dump_region_size[PANVK_SUBQUEUE_FRAGMENT] =
            MAX2(device->dump_region_size[PANVK_SUBQUEUE_FRAGMENT],
                 dump_region_size);
      }
      device->tiler_oom.handler_stride = TILER_OOM_HANDLER_MAX_SIZE;
   }

   panvk_priv_bo_flush(device->tiler_oom.handlers_bo, 0,
                       pan_kmod_bo_size(device->tiler_oom.handlers_bo->bo));

   return result;
}
