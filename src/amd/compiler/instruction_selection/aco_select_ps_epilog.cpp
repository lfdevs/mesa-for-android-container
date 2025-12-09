/*
 * Copyright © 2022 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_builder.h"
#include "aco_instruction_selection.h"
#include "aco_ir.h"

#include "amdgfxregs.h"

namespace aco {
namespace {

void
emit_clamp_alpha_test(isel_context* ctx, const struct aco_ps_epilog_info* info, Temp colors[4],
                      unsigned color_index)
{
   Builder bld(ctx->program, ctx->block);

   if (info->clamp_color) {
      for (unsigned i = 0; i < 4; i++) {
         if (colors[i].regClass() == v2b) {
            colors[i] = bld.vop3(aco_opcode::v_med3_f16, bld.def(v2b), Operand::c16(0u),
                                 Operand::c16(0x3c00), colors[i]);
         } else {
            assert(colors[i].regClass() == v1);
            colors[i] = bld.vop3(aco_opcode::v_med3_f32, bld.def(v1), Operand::zero(),
                                 Operand::c32(0x3f800000u), colors[i]);
         }
      }
   }

   if (info->alpha_to_one) {
      if (colors[3].regClass() == v2b)
         colors[3] = bld.copy(bld.def(v2b), Operand::c16(0x3c00));
      else
         colors[3] = bld.copy(bld.def(v1), Operand::c32(0x3f800000u));
   }

   if (color_index == 0 && info->alpha_func != COMPARE_FUNC_ALWAYS) {
      Operand cond = Operand::c32(-1u);
      if (info->alpha_func != COMPARE_FUNC_NEVER) {
         aco_opcode opcode = aco_opcode::num_opcodes;

         switch (info->alpha_func) {
         case COMPARE_FUNC_LESS: opcode = aco_opcode::v_cmp_ngt_f32; break;
         case COMPARE_FUNC_EQUAL: opcode = aco_opcode::v_cmp_neq_f32; break;
         case COMPARE_FUNC_LEQUAL: opcode = aco_opcode::v_cmp_nge_f32; break;
         case COMPARE_FUNC_GREATER: opcode = aco_opcode::v_cmp_nlt_f32; break;
         case COMPARE_FUNC_NOTEQUAL: opcode = aco_opcode::v_cmp_nlg_f32; break;
         case COMPARE_FUNC_GEQUAL: opcode = aco_opcode::v_cmp_nle_f32; break;
         default: UNREACHABLE("invalid alpha func");
         }

         Temp ref = get_arg(ctx, info->alpha_reference);

         Temp alpha = colors[3].regClass() == v2b
                         ? bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), colors[3])
                         : colors[3];

         /* true if not pass */
         cond = bld.vopc(opcode, bld.def(bld.lm), ref, alpha);
      }

      bld.pseudo(aco_opcode::p_discard_if, cond);
      ctx->block->kind |= block_kind_uses_discard;
      ctx->program->needs_exact = true;
   }
}

void
export_mrt(isel_context* ctx, const struct aco_export_mrt* mrt)
{
   Builder bld(ctx->program, ctx->block);

   bld.exp(aco_opcode::exp, mrt->out[0], mrt->out[1], mrt->out[2], mrt->out[3],
           mrt->enabled_channels, mrt->target, mrt->compr);

   ctx->program->has_color_exports = true;
}

bool
export_fs_mrt_color(isel_context* ctx, const struct aco_ps_epilog_info* info, Temp colors[4],
                    unsigned slot, unsigned color_type, struct aco_export_mrt* mrt)
{
   unsigned col_format = (info->spi_shader_col_format >> (slot * 4)) & 0xf;

   if (col_format == V_028714_SPI_SHADER_ZERO)
      return false;

   Builder bld(ctx->program, ctx->block);
   Operand values[4];

   for (unsigned i = 0; i < 4; ++i) {
      values[i] = Operand(colors[i]);
   }

   unsigned enabled_channels = 0;
   aco_opcode compr_op = aco_opcode::num_opcodes;
   bool compr = false;
   bool is_16bit = colors[0].regClass() == v2b;
   assert(is_16bit == (color_type != ACO_TYPE_ANY32));
   bool is_int8 = (info->color_is_int8 >> slot) & 1;
   bool is_int10 = (info->color_is_int10 >> slot) & 1;
   bool enable_mrt_output_nan_fixup = (ctx->options->enable_mrt_output_nan_fixup >> slot) & 1;

   /* Replace NaN by zero (only 32-bit) to fix game bugs if requested. */
   if (enable_mrt_output_nan_fixup && !is_16bit &&
       (col_format == V_028714_SPI_SHADER_32_R || col_format == V_028714_SPI_SHADER_32_GR ||
        col_format == V_028714_SPI_SHADER_32_AR || col_format == V_028714_SPI_SHADER_32_ABGR ||
        col_format == V_028714_SPI_SHADER_FP16_ABGR)) {
      for (unsigned i = 0; i < 4; i++) {
         Temp is_not_nan =
            bld.vopc(aco_opcode::v_cmp_eq_f32, bld.def(bld.lm), values[i], values[i]);
         values[i] = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), Operand::zero(), values[i],
                              is_not_nan);
      }
   }

   switch (col_format) {
   case V_028714_SPI_SHADER_32_R:
      if (color_type == ACO_TYPE_FLOAT16)
         values[0] = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), values[0]);
      else if (color_type == ACO_TYPE_INT16 || color_type == ACO_TYPE_UINT16)
         values[0] = Operand(
            convert_int(ctx, bld, values[0].getTemp(), 16, 32, color_type == ACO_TYPE_INT16));
      enabled_channels = 1;
      break;

   case V_028714_SPI_SHADER_32_GR:
      if (color_type == ACO_TYPE_FLOAT16) {
         for (unsigned i = 0; i < 2; i++)
            values[i] = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), values[i]);
      } else if (color_type == ACO_TYPE_INT16 || color_type == ACO_TYPE_UINT16) {
         for (unsigned i = 0; i < 2; i++)
            values[i] = Operand(
               convert_int(ctx, bld, values[i].getTemp(), 16, 32, color_type == ACO_TYPE_INT16));
      }
      enabled_channels = 0x3;
      break;

   case V_028714_SPI_SHADER_32_AR:
      if (color_type == ACO_TYPE_FLOAT16) {
         for (unsigned i = 0; i < 2; i++) {
            unsigned idx = i ? 3 : 0;
            values[idx] = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), values[idx]);
         }
      } else if (color_type == ACO_TYPE_INT16 || color_type == ACO_TYPE_UINT16) {
         for (unsigned i = 0; i < 2; i++) {
            unsigned idx = i ? 3 : 0;
            values[idx] = Operand(
               convert_int(ctx, bld, values[idx].getTemp(), 16, 32, color_type == ACO_TYPE_INT16));
         }
      }

      if (ctx->options->gfx_level >= GFX10) {
         /* Special case: on GFX10, the outputs are different for 32_AR */
         enabled_channels = 0x3;
         values[1] = values[3];
         values[3] = Operand(v1);
      } else {
         enabled_channels = 0x9;
      }
      break;

   case V_028714_SPI_SHADER_FP16_ABGR:
      for (int i = 0; i < 2; i++) {
         if (is_16bit) {
            values[i] = bld.pseudo(aco_opcode::p_create_vector, bld.def(v1), values[i * 2],
                                   values[i * 2 + 1]);
         } else if (ctx->options->gfx_level == GFX8 || ctx->options->gfx_level == GFX9) {
            values[i] = bld.vop3(aco_opcode::v_cvt_pkrtz_f16_f32_e64, bld.def(v1), values[i * 2],
                                 values[i * 2 + 1]);
         } else {
            values[i] = bld.vop2(aco_opcode::v_cvt_pkrtz_f16_f32, bld.def(v1), values[i * 2],
                                 values[i * 2 + 1]);
         }
      }
      values[2] = Operand(v1);
      values[3] = Operand(v1);
      enabled_channels = 0xf;
      compr = true;
      break;

   case V_028714_SPI_SHADER_UNORM16_ABGR:
      if (is_16bit && ctx->options->gfx_level >= GFX9) {
         compr_op = aco_opcode::v_cvt_pknorm_u16_f16;
      } else {
         compr_op = aco_opcode::v_cvt_pknorm_u16_f32;
      }
      break;

   case V_028714_SPI_SHADER_SNORM16_ABGR:
      if (is_16bit && ctx->options->gfx_level >= GFX9) {
         compr_op = aco_opcode::v_cvt_pknorm_i16_f16;
      } else {
         compr_op = aco_opcode::v_cvt_pknorm_i16_f32;
      }
      break;

   case V_028714_SPI_SHADER_UINT16_ABGR:
      compr_op = aco_opcode::v_cvt_pk_u16_u32;
      if (is_int8 || is_int10) {
         /* clamp */
         uint32_t max_rgb = is_int8 ? 255 : is_int10 ? 1023 : 0;

         for (unsigned i = 0; i < 4; i++) {
            uint32_t max = i == 3 && is_int10 ? 3 : max_rgb;

            values[i] = bld.vop2(aco_opcode::v_min_u32, bld.def(v1), Operand::c32(max), values[i]);
         }
      } else if (is_16bit) {
         for (unsigned i = 0; i < 4; i++) {
            Temp tmp = convert_int(ctx, bld, values[i].getTemp(), 16, 32, false);
            values[i] = Operand(tmp);
         }
      }
      break;

   case V_028714_SPI_SHADER_SINT16_ABGR:
      compr_op = aco_opcode::v_cvt_pk_i16_i32;
      if (is_int8 || is_int10) {
         /* clamp */
         uint32_t max_rgb = is_int8 ? 127 : is_int10 ? 511 : 0;
         uint32_t min_rgb = is_int8 ? -128 : is_int10 ? -512 : 0;

         for (unsigned i = 0; i < 4; i++) {
            uint32_t max = i == 3 && is_int10 ? 1 : max_rgb;
            uint32_t min = i == 3 && is_int10 ? -2u : min_rgb;

            values[i] = bld.vop2(aco_opcode::v_min_i32, bld.def(v1), Operand::c32(max), values[i]);
            values[i] = bld.vop2(aco_opcode::v_max_i32, bld.def(v1), Operand::c32(min), values[i]);
         }
      } else if (is_16bit) {
         for (unsigned i = 0; i < 4; i++) {
            Temp tmp = convert_int(ctx, bld, values[i].getTemp(), 16, 32, true);
            values[i] = Operand(tmp);
         }
      }
      break;

   case V_028714_SPI_SHADER_32_ABGR:
      enabled_channels = 0xF;
      if (color_type == ACO_TYPE_FLOAT16) {
         for (unsigned i = 0; i < 4; i++)
            values[i] = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), values[i]);
      } else if (color_type == ACO_TYPE_INT16 || color_type == ACO_TYPE_UINT16) {
         for (unsigned i = 0; i < 4; i++)
            values[i] = Operand(
               convert_int(ctx, bld, values[i].getTemp(), 16, 32, color_type == ACO_TYPE_INT16));
      }
      break;

   case V_028714_SPI_SHADER_ZERO:
   default: return false;
   }

   if (compr_op != aco_opcode::num_opcodes) {
      values[0] = bld.vop3(compr_op, bld.def(v1), values[0], values[1]);
      values[1] = bld.vop3(compr_op, bld.def(v1), values[2], values[3]);
      values[2] = Operand(v1);
      values[3] = Operand(v1);
      enabled_channels = 0xf;
      compr = true;
   } else if (!compr) {
      for (int i = 0; i < 4; i++)
         values[i] = enabled_channels & (1 << i) ? values[i] : Operand(v1);
   }

   if (ctx->program->gfx_level >= GFX11) {
      /* GFX11 doesn't use COMPR for exports, but the channel mask should be
       * 0x3 instead.
       */
      enabled_channels = compr ? 0x3 : enabled_channels;
      compr = false;
   }

   for (unsigned i = 0; i < 4; i++)
      mrt->out[i] = values[i];
   mrt->target = V_008DFC_SQ_EXP_MRT;
   mrt->enabled_channels = enabled_channels;
   mrt->compr = compr;

   return true;
}

void
export_fs_mrtz(isel_context* ctx, const struct aco_ps_epilog_info* info, Temp depth, Temp stencil,
               Temp samplemask, Temp alpha)
{
   Builder bld(ctx->program, ctx->block);
   unsigned enabled_channels = 0;
   bool compr = false;
   Operand values[4];

   for (unsigned i = 0; i < 4; ++i) {
      values[i] = Operand(v1);
   }

   const unsigned format =
      ac_get_spi_shader_z_format(depth.id(), stencil.id(), samplemask.id(), alpha.id());
   assert(format != V_028710_SPI_SHADER_ZERO);

   /* Both stencil and sample mask only need 16-bits. */
   if (format == V_028710_SPI_SHADER_UINT16_ABGR) {
      compr = ctx->program->gfx_level < GFX11; /* COMPR flag */

      if (stencil.id()) {
         /* Stencil should be in X[23:16]. */
         values[0] = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(16u), stencil);
         enabled_channels |= ctx->program->gfx_level >= GFX11 ? 0x1 : 0x3;
      }

      if (samplemask.id()) {
         /* SampleMask should be in Y[15:0]. */
         values[1] = Operand(samplemask);
         enabled_channels |= ctx->program->gfx_level >= GFX11 ? 0x2 : 0xc;
      }
   } else {
      if (depth.id()) {
         values[0] = Operand(depth);
         enabled_channels |= 0x1;
      }

      if (stencil.id()) {
         assert(format == V_028710_SPI_SHADER_32_GR || format == V_028710_SPI_SHADER_32_ABGR);
         values[1] = Operand(stencil);
         enabled_channels |= 0x2;
      }

      if (samplemask.id()) {
         assert(format == V_028710_SPI_SHADER_32_ABGR);
         values[2] = Operand(samplemask);
         enabled_channels |= 0x4;
      }

      if (alpha.id()) {
         assert(format == V_028710_SPI_SHADER_32_AR || format == V_028710_SPI_SHADER_32_ABGR);
         assert(ctx->program->gfx_level >= GFX11 || info->alpha_to_one);
         values[3] = Operand(alpha);
         enabled_channels |= 0x8;
      }
   }

   /* GFX6 (except OLAND and HAINAN) has a bug that it only looks at the X
    * writemask component.
    */
   if (ctx->options->gfx_level == GFX6 && ctx->options->family != CHIP_OLAND &&
       ctx->options->family != CHIP_HAINAN) {
      enabled_channels |= 0x1;
   }

   bld.exp(aco_opcode::exp, values[0], values[1], values[2], values[3], enabled_channels,
           V_008DFC_SQ_EXP_MRTZ, compr);
}

void
create_fs_null_export(isel_context* ctx)
{
   /* FS must always have exports.
    * So when there are none, we need to add a null export.
    */

   Builder bld(ctx->program, ctx->block);
   /* GFX11 doesn't support NULL exports, and MRT0 should be exported instead. */
   unsigned dest = ctx->options->gfx_level >= GFX11 ? V_008DFC_SQ_EXP_MRT : V_008DFC_SQ_EXP_NULL;
   bld.exp(aco_opcode::exp, Operand(v1), Operand(v1), Operand(v1), Operand(v1),
           /* enabled_mask */ 0, dest, /* compr */ false, /* done */ true, /* vm */ true);

   ctx->program->has_color_exports = true;
}

} // namespace

void
select_ps_epilog(Program* program, void* pinfo, ac_shader_config* config,
                 const struct aco_compiler_options* options, const struct aco_shader_info* info,
                 const struct ac_shader_args* args)
{
   const struct aco_ps_epilog_info* einfo = (const struct aco_ps_epilog_info*)pinfo;
   isel_context ctx =
      setup_isel_context(program, 0, NULL, config, options, info, args, SWStage::FS);

   ctx.block->fp_mode = program->next_fp_mode;

   add_startpgm(&ctx);
   append_logical_start(ctx.block);

   Builder bld(ctx.program, ctx.block);

   bool has_mrtz_alpha = einfo->alpha_to_coverage_via_mrtz && einfo->colors[0].used;
   Temp mrtz_alpha;

   Temp colors[MAX_DRAW_BUFFERS][4];
   for (unsigned i = 0; i < MAX_DRAW_BUFFERS; i++) {
      if (!einfo->colors[i].used)
         continue;

      Temp color = get_arg(&ctx, einfo->colors[i]);
      unsigned col_type = (einfo->color_types >> (i * 2)) & 0x3;

      emit_split_vector(&ctx, color, col_type == ACO_TYPE_ANY32 ? 4 : 8);
      for (unsigned c = 0; c < 4; ++c) {
         colors[i][c] = emit_extract_vector(&ctx, color, c, col_type == ACO_TYPE_ANY32 ? v1 : v2b);
      }

      /* Store MRTZ.a before applying alpha-to-one if enabled. */
      if (has_mrtz_alpha && i == 0)
         mrtz_alpha = colors[0][3];

      emit_clamp_alpha_test(&ctx, einfo, colors[i], i);
   }

   bool has_mrtz_depth = einfo->depth.used && !einfo->kill_depth;
   bool has_mrtz_stencil = einfo->stencil.used && !einfo->kill_stencil;
   bool has_mrtz_samplemask = einfo->samplemask.used && !einfo->kill_samplemask;
   bool has_mrtz_export =
      has_mrtz_depth || has_mrtz_stencil || has_mrtz_samplemask || has_mrtz_alpha;
   if (has_mrtz_export) {
      Temp depth = has_mrtz_depth ? get_arg(&ctx, einfo->depth) : Temp();
      Temp stencil = has_mrtz_stencil ? get_arg(&ctx, einfo->stencil) : Temp();
      Temp samplemask = has_mrtz_samplemask ? get_arg(&ctx, einfo->samplemask) : Temp();

      export_fs_mrtz(&ctx, einfo, depth, stencil, samplemask, mrtz_alpha);
   }

   /* Export all color render targets */
   struct aco_export_mrt mrts[MAX_DRAW_BUFFERS];
   unsigned mrt_num = 0;

   if (einfo->writes_all_cbufs) {
      /* This will do nothing for color buffers with SPI_SHADER_COL_FORMAT=ZERO, so always
       * iterate over all 8.
       */
      for (unsigned i = 0; i < 8; i++) {
         struct aco_export_mrt* mrt = &mrts[mrt_num];
         unsigned col_type = einfo->color_types & 0x3;

         if (export_fs_mrt_color(&ctx, einfo, colors[0], i, col_type, mrt))
            mrt->target += mrt_num++;
      }
   } else {
      for (unsigned i = 0; i < MAX_DRAW_BUFFERS; i++) {
         struct aco_export_mrt* mrt = &mrts[mrt_num];
         const uint8_t cb_idx = einfo->color_map[i];
         unsigned col_type = (einfo->color_types >> (cb_idx * 2)) & 0x3;

         if (cb_idx == 0xff || !einfo->colors[cb_idx].used)
            continue;

         if (export_fs_mrt_color(&ctx, einfo, colors[cb_idx], i, col_type, mrt)) {
            mrt->target += mrt_num++;
         }
      }
   }

   if (mrt_num) {
      if (ctx.options->gfx_level >= GFX11 && einfo->mrt0_is_dual_src) {
         assert(mrt_num == 2);
         create_fs_dual_src_export_gfx11(&ctx, &mrts[0], &mrts[1]);
      } else {
         for (unsigned i = 0; i < mrt_num; i++)
            export_mrt(&ctx, &mrts[i]);
      }
   } else if (!has_mrtz_export && !einfo->skip_null_export) {
      create_fs_null_export(&ctx);
   }

   program->config->float_mode = program->blocks[0].fp_mode.val;

   append_logical_end(&ctx);
   ctx.block->kind |= block_kind_export_end;
   bld.reset(ctx.block);
   bld.sopp(aco_opcode::s_endpgm);

   finish_program(&ctx);
}

} // namespace aco
