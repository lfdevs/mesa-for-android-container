/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "ac_nir.h"
#include "si_pipe.h"
#include "si_shader_internal.h"
#include "si_query.h"
#include "sid.h"
#include "util/u_memory.h"

LLVMValueRef si_is_es_thread(struct si_shader_context *ctx)
{
   /* Return true if the current thread should execute an ES thread. */
   return LLVMBuildICmp(ctx->ac.builder, LLVMIntULT, ac_get_thread_id(&ctx->ac),
                        si_unpack_param(ctx, ctx->args->ac.merged_wave_info, 0, 8), "");
}

LLVMValueRef si_is_gs_thread(struct si_shader_context *ctx)
{
   /* Return true if the current thread should execute a GS thread. */
   return LLVMBuildICmp(ctx->ac.builder, LLVMIntULT, ac_get_thread_id(&ctx->ac),
                        si_unpack_param(ctx, ctx->args->ac.merged_wave_info, 8, 8), "");
}

/* Pass GS inputs from ES to GS on GFX9. */
static void si_set_es_return_value_for_gs(struct si_shader_context *ctx)
{
   if (!ctx->shader->is_monolithic)
      ac_build_endif(&ctx->ac, ctx->merged_wrap_if_label);

   LLVMValueRef ret = ctx->return_value;

   ret = si_insert_input_ptr(ctx, ret, ctx->args->other_const_and_shader_buffers, 0);
   ret = si_insert_input_ptr(ctx, ret, ctx->args->other_samplers_and_images, 1);
   if (ctx->shader->key.ge.as_ngg)
      ret = si_insert_input_ptr(ctx, ret, ctx->args->ac.gs_tg_info, 2);
   else
      ret = si_insert_input_ret(ctx, ret, ctx->args->ac.gs2vs_offset, 2);
   ret = si_insert_input_ret(ctx, ret, ctx->args->ac.merged_wave_info, 3);
   if (ctx->screen->info.gfx_level >= GFX11)
      ret = si_insert_input_ret(ctx, ret, ctx->args->ac.gs_attr_offset, 5);
   else
      ret = si_insert_input_ret(ctx, ret, ctx->args->ac.scratch_offset, 5);
   ret = si_insert_input_ptr(ctx, ret, ctx->args->internal_bindings, 8 + SI_SGPR_INTERNAL_BINDINGS);
   ret = si_insert_input_ptr(ctx, ret, ctx->args->bindless_samplers_and_images,
                             8 + SI_SGPR_BINDLESS_SAMPLERS_AND_IMAGES);
   ret = si_insert_input_ptr(ctx, ret, ctx->args->vs_state_bits, 8 + SI_SGPR_VS_STATE_BITS);
   if (ctx->screen->use_ngg) {
      ret = si_insert_input_ptr(ctx, ret, ctx->args->small_prim_cull_info, 8 + GFX9_SGPR_SMALL_PRIM_CULL_INFO);
      if (ctx->screen->info.gfx_level >= GFX11)
         ret = si_insert_input_ptr(ctx, ret, ctx->args->gs_attr_address, 8 + GFX9_SGPR_ATTRIBUTE_RING_ADDR);
   }

   unsigned vgpr = 8 + GFX9_GS_NUM_USER_SGPR;

   ret = si_insert_input_ret_float(ctx, ret, ctx->args->ac.gs_vtx_offset[0], vgpr++);
   ret = si_insert_input_ret_float(ctx, ret, ctx->args->ac.gs_vtx_offset[1], vgpr++);
   ret = si_insert_input_ret_float(ctx, ret, ctx->args->ac.gs_prim_id, vgpr++);
   ret = si_insert_input_ret_float(ctx, ret, ctx->args->ac.gs_invocation_id, vgpr++);
   ret = si_insert_input_ret_float(ctx, ret, ctx->args->ac.gs_vtx_offset[2], vgpr++);
   ctx->return_value = ret;
}

void si_llvm_es_build_end(struct si_shader_context *ctx)
{
   if (ctx->screen->info.gfx_level >= GFX9)
      si_set_es_return_value_for_gs(ctx);
}

static LLVMValueRef si_get_gs_wave_id(struct si_shader_context *ctx)
{
   if (ctx->screen->info.gfx_level >= GFX9)
      return si_unpack_param(ctx, ctx->args->ac.merged_wave_info, 16, 8);
   else
      return ac_get_arg(&ctx->ac, ctx->args->ac.gs_wave_id);
}

void si_llvm_gs_build_end(struct si_shader_context *ctx)
{
   if (ctx->screen->info.gfx_level >= GFX10)
      ac_build_waitcnt(&ctx->ac, AC_WAIT_VSTORE);

   ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_NOP | AC_SENDMSG_GS_DONE, si_get_gs_wave_id(ctx));

   if (ctx->screen->info.gfx_level >= GFX9)
      ac_build_endif(&ctx->ac, ctx->merged_wrap_if_label);
}

/* Emit one vertex from the geometry shader */
static void si_llvm_emit_vertex(struct ac_shader_abi *abi, unsigned stream,
                                LLVMValueRef vertexidx, LLVMValueRef *addrs)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);

   assert(!ctx->shader->key.ge.as_ngg);

   /* Signal vertex emission */
   ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_EMIT | AC_SENDMSG_GS | (stream << 8),
                    si_get_gs_wave_id(ctx));
}

/* Cut one primitive from the geometry shader */
static void si_llvm_emit_primitive(struct ac_shader_abi *abi, unsigned stream)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);

   assert(!ctx->shader->key.ge.as_ngg);

   /* Signal primitive cut */
   ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_CUT | AC_SENDMSG_GS | (stream << 8),
                    si_get_gs_wave_id(ctx));
}

void si_preload_esgs_ring(struct si_shader_context *ctx)
{
   LLVMBuilderRef builder = ctx->ac.builder;

   if (ctx->screen->info.gfx_level <= GFX8) {
      LLVMValueRef offset = LLVMConstInt(ctx->ac.i32, SI_RING_ESGS, 0);

      ctx->esgs_ring = ac_build_load_to_sgpr(&ctx->ac,
         ac_get_ptr_arg(&ctx->ac, &ctx->args->ac, ctx->args->internal_bindings), offset);

      if (ctx->stage != MESA_SHADER_GEOMETRY) {
         LLVMValueRef desc1 = LLVMBuildExtractElement(builder, ctx->esgs_ring, ctx->ac.i32_1, "");
         LLVMValueRef desc3 = LLVMBuildExtractElement(builder, ctx->esgs_ring,
                                                      LLVMConstInt(ctx->ac.i32, 3, 0), "");
         desc1 = LLVMBuildOr(builder, desc1, LLVMConstInt(ctx->ac.i32,
                                                          S_008F04_SWIZZLE_ENABLE_GFX6(1), 0), "");
         desc3 = LLVMBuildOr(builder, desc3, LLVMConstInt(ctx->ac.i32,
                                                          S_008F0C_ELEMENT_SIZE(1) |
                                                          S_008F0C_INDEX_STRIDE(3) |
                                                          S_008F0C_ADD_TID_ENABLE(1), 0), "");

         /* If MUBUF && ADD_TID_ENABLE, DATA_FORMAT means STRIDE[14:17] on gfx8-9, so set 0. */
         if (ctx->screen->info.gfx_level == GFX8) {
            desc3 = LLVMBuildAnd(builder, desc3,
                                 LLVMConstInt(ctx->ac.i32, C_008F0C_DATA_FORMAT, 0), "");
         }

         ctx->esgs_ring = LLVMBuildInsertElement(builder, ctx->esgs_ring, desc1, ctx->ac.i32_1, "");
         ctx->esgs_ring = LLVMBuildInsertElement(builder, ctx->esgs_ring, desc3,
                                                 LLVMConstInt(ctx->ac.i32, 3, 0), "");
      }
   } else {
      /* Declare the ESGS ring as an explicit LDS symbol. */
      si_llvm_declare_esgs_ring(ctx);
      ctx->ac.lds = (struct ac_llvm_pointer) {
         .value = ctx->esgs_ring,
         .pointee_type = LLVMArrayType(ctx->ac.i32, 0),
      };
   }
}

void si_preload_gs_rings(struct si_shader_context *ctx)
{
   if (ctx->ac.gfx_level >= GFX11)
      return;

   const struct si_shader_selector *sel = ctx->shader->selector;
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef offset = LLVMConstInt(ctx->ac.i32, SI_RING_GSVS, 0);
   LLVMValueRef base_ring = ac_build_load_to_sgpr(&ctx->ac,
      ac_get_ptr_arg(&ctx->ac, &ctx->args->ac, ctx->args->internal_bindings), offset);

   /* The conceptual layout of the GSVS ring is
    *   v0c0 .. vLv0 v0c1 .. vLc1 ..
    * but the real memory layout is swizzled across
    * threads:
    *   t0v0c0 .. t15v0c0 t0v1c0 .. t15v1c0 ... t15vLcL
    *   t16v0c0 ..
    * Override the buffer descriptor accordingly.
    */
   LLVMTypeRef v2i64 = LLVMVectorType(ctx->ac.i64, 2);
   uint64_t stream_offset = 0;

   for (unsigned stream = 0; stream < 4; ++stream) {
      unsigned num_components;
      unsigned stride;
      unsigned num_records;
      LLVMValueRef ring, tmp;

      num_components = sel->info.num_stream_output_components[stream];
      if (!num_components)
         continue;

      stride = 4 * num_components * sel->info.base.gs.vertices_out;

      /* Limit on the stride field for <= GFX7. */
      assert(stride < (1 << 14));

      num_records = ctx->ac.wave_size;

      ring = LLVMBuildBitCast(builder, base_ring, v2i64, "");
      tmp = LLVMBuildExtractElement(builder, ring, ctx->ac.i32_0, "");
      tmp = LLVMBuildAdd(builder, tmp, LLVMConstInt(ctx->ac.i64, stream_offset, 0), "");
      stream_offset += stride * ctx->ac.wave_size;

      ring = LLVMBuildInsertElement(builder, ring, tmp, ctx->ac.i32_0, "");
      ring = LLVMBuildBitCast(builder, ring, ctx->ac.v4i32, "");
      tmp = LLVMBuildExtractElement(builder, ring, ctx->ac.i32_1, "");
      tmp = LLVMBuildOr(
         builder, tmp,
         LLVMConstInt(ctx->ac.i32, S_008F04_STRIDE(stride) | S_008F04_SWIZZLE_ENABLE_GFX6(1), 0), "");
      ring = LLVMBuildInsertElement(builder, ring, tmp, ctx->ac.i32_1, "");
      ring = LLVMBuildInsertElement(builder, ring, LLVMConstInt(ctx->ac.i32, num_records, 0),
                                    LLVMConstInt(ctx->ac.i32, 2, 0), "");

      uint32_t rsrc3 =
         S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
         S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
         S_008F0C_INDEX_STRIDE(1) | /* index_stride = 16 (elements) */
         S_008F0C_ADD_TID_ENABLE(1);

      if (ctx->ac.gfx_level >= GFX10) {
         rsrc3 |= S_008F0C_FORMAT(V_008F0C_GFX10_FORMAT_32_FLOAT) |
                  S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_DISABLED) | S_008F0C_RESOURCE_LEVEL(1);
      } else {
         /* If MUBUF && ADD_TID_ENABLE, DATA_FORMAT means STRIDE[14:17] on gfx8-9, so set 0. */
         unsigned data_format = ctx->ac.gfx_level == GFX8 || ctx->ac.gfx_level == GFX9 ?
                                   0 : V_008F0C_BUF_DATA_FORMAT_32;

         rsrc3 |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
                  S_008F0C_DATA_FORMAT(data_format) |
                  S_008F0C_ELEMENT_SIZE(1); /* element_size = 4 (bytes) */
      }

      ring = LLVMBuildInsertElement(builder, ring, LLVMConstInt(ctx->ac.i32, rsrc3, false),
                                    LLVMConstInt(ctx->ac.i32, 3, 0), "");

      ctx->gsvs_ring[stream] = ring;
   }
}

void si_llvm_init_gs_callbacks(struct si_shader_context *ctx)
{
   ctx->abi.emit_vertex_with_counter = si_llvm_emit_vertex;
   ctx->abi.emit_primitive = si_llvm_emit_primitive;
}
