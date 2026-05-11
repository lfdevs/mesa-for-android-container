/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_pipe.h"
#include "si_shader_internal.h"
#include "si_shader_llvm.h"
#include "nir.h"

static LLVMValueRef si_nir_load_tcs_varyings(struct ac_shader_abi *abi, unsigned num_components,
                                             unsigned bit_size, gl_varying_slot slot,
                                             unsigned component, bool high_16bits)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);

   assert(ctx->shader->key.ge.opt.same_patch_vertices);
   assert(bit_size == 16 || bit_size == 32);

   /* Load the TCS input from a VGPR. */
   unsigned func_param = ctx->args->ac.tcs_rel_ids.arg_index + 1 +
                         si_shader_io_get_unique_index(slot) * 4;
   LLVMValueRef *value = alloca(sizeof(LLVMValueRef) * num_components);

   for (unsigned i = 0; i < num_components; i++) {
      value[i] = LLVMGetParam(ctx->main_fn.value, func_param + component + i);

      if (bit_size == 16) {
         /* Extract low or high 16 bits from the value. */
         value[i] = LLVMBuildBitCast(ctx->ac.builder, value[i], ctx->ac.v2i16, "");
         value[i] = LLVMBuildExtractElement(ctx->ac.builder, value[i],
                                            LLVMConstInt(ctx->ac.i32, high_16bits, 0), "");
      }
   }

   return ac_build_gather_values(&ctx->ac, value, num_components);
}

void si_llvm_ls_build_end(struct si_shader_context *ctx, const nir_shader *nir)
{
   struct si_shader *shader = ctx->shader;
   bool same_thread_count = shader->key.ge.opt.same_patch_vertices;

   /* Only need return value when merged shader on part mode or mono mode with same thread count. */
   if (ctx->screen->info.gfx_level < GFX9 || (shader->is_monolithic && !same_thread_count))
      return;

   if (!ctx->shader->is_monolithic)
      ac_build_endif(&ctx->ac, SI_MERGED_WRAP_IF_LABEL);

   LLVMValueRef ret = ctx->return_value;

   ret = si_insert_input_ptr(ctx, ret, ctx->args->other_const_and_shader_buffers, 0);
   ret = si_insert_input_ptr(ctx, ret, ctx->args->other_samplers_and_images, 1);
   ret = si_insert_input_ret(ctx, ret, ctx->args->ac.tess_offchip_offset, 2);
   ret = si_insert_input_ret(ctx, ret, ctx->args->ac.merged_wave_info, 3);
   ret = si_insert_input_ret(ctx, ret, ctx->args->ac.tcs_factor_offset, 4);
   if (ctx->screen->info.gfx_level >= GFX11)
      ret = si_insert_input_ret(ctx, ret, ctx->args->ac.tcs_wave_id, 5);
   else
      ret = si_insert_input_ret(ctx, ret, ctx->args->ac.scratch_offset, 5);

   ret = si_insert_input_ptr(ctx, ret, ctx->args->internal_bindings, 8 + SI_SGPR_INTERNAL_BINDINGS);
   ret = si_insert_input_ptr(ctx, ret, ctx->args->bindless_samplers_and_images,
                             8 + SI_SGPR_BINDLESS_SAMPLERS_AND_IMAGES);

   ret = si_insert_input_ret(ctx, ret, ctx->args->vs_state_bits, 8 + SI_SGPR_VS_STATE_BITS);

   ret = si_insert_input_ret(ctx, ret, ctx->args->ac.tcs_offchip_layout, 8 + GFX9_SGPR_TCS_OFFCHIP_LAYOUT);
   ret = si_insert_input_ret(ctx, ret, ctx->args->tes_offchip_addr, 8 + GFX9_SGPR_TCS_OFFCHIP_ADDR);

   unsigned vgpr = 8 + GFX9_TCS_NUM_USER_SGPR;
   ret = si_insert_input_ret_float(ctx, ret, ctx->args->ac.tcs_patch_id, vgpr++);
   ret = si_insert_input_ret_float(ctx, ret, ctx->args->ac.tcs_rel_ids, vgpr++);

   if (same_thread_count) {
      /* Same thread count is set only when mono mode. */
      assert(shader->is_monolithic);

      struct si_shader_info *info = &shader->selector->info;

      u_foreach_bit64_two_masks(semantic, nir->info.outputs_written,
                                VARYING_SLOT_VAR0_16BIT, nir->info.outputs_written_16bit) {
         int param = si_shader_io_get_unique_index(semantic);

         if (!(info->ls_es_outputs_written & BITFIELD64_BIT(param)))
            continue;

         unsigned i = ac_nir_get_io_driver_location(nir, semantic, false);

         for (unsigned chan = 0; chan < 4; chan++) {
            if (!ctx->abi.outputs[4 * i + chan])
               continue;

            LLVMValueRef value = LLVMBuildLoad2(ctx->ac.builder, ctx->ac.f32,
                                                ctx->abi.outputs[4 * i + chan], "");
            ret = LLVMBuildInsertValue(ctx->ac.builder, ret, value, vgpr + param * 4 + chan, "");
         }
      }
   }

   ctx->return_value = ret;
}

void si_llvm_init_tcs_callbacks(struct si_shader_context *ctx)
{
   ctx->abi.load_tess_varyings = si_nir_load_tcs_varyings;
}
