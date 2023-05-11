/*
 * Copyright 2023 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "ac_nir.h"
#include "sid.h"
#include "nir_builder.h"
#include "nir_builtin_builder.h"

typedef struct {
   const ac_nir_lower_ps_options *options;

   /* Add one for dual source blend second output. */
   nir_ssa_def *outputs[FRAG_RESULT_MAX + 1][4];
   nir_alu_type output_types[FRAG_RESULT_MAX + 1];

   /* MAX_DRAW_BUFFERS for MRT export, 1 for MRTZ export */
   nir_intrinsic_instr *exp[MAX_DRAW_BUFFERS + 1];
   unsigned exp_num;

   unsigned compacted_mrt_index;
} lower_ps_state;

#define DUAL_SRC_BLEND_SLOT FRAG_RESULT_MAX

static bool
gather_ps_store_output(nir_builder *b, nir_intrinsic_instr *intrin, lower_ps_state *s)
{
   nir_io_semantics sem = nir_intrinsic_io_semantics(intrin);
   unsigned write_mask = nir_intrinsic_write_mask(intrin);
   unsigned component = nir_intrinsic_component(intrin);
   nir_alu_type type = nir_intrinsic_src_type(intrin);
   nir_ssa_def *store_val = intrin->src[0].ssa;

   b->cursor = nir_before_instr(&intrin->instr);

   unsigned slot = sem.dual_source_blend_index ?
      DUAL_SRC_BLEND_SLOT : sem.location;

   u_foreach_bit (i, write_mask) {
      unsigned comp = component + i;
      s->outputs[slot][comp] = nir_channel(b, store_val, i);
   }

   /* Same slot should have same type for all components. */
   assert(s->output_types[slot] == nir_type_invalid || s->output_types[slot] == type);

   s->output_types[slot] = type;

   nir_instr_remove(&intrin->instr);
   return true;
}

static bool
lower_ps_intrinsic(nir_builder *b, nir_instr *instr, void *state)
{
   lower_ps_state *s = (lower_ps_state *)state;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   if (intrin->intrinsic == nir_intrinsic_store_output)
      return gather_ps_store_output(b, intrin, s);

   return false;
}

static void
emit_ps_color_clamp_and_alpha_test(nir_builder *b, lower_ps_state *s)
{
   uint32_t color_mask =
      BITFIELD_BIT(FRAG_RESULT_COLOR) |
      BITFIELD_RANGE(FRAG_RESULT_DATA0, MAX_DRAW_BUFFERS);
   uint32_t color_outputs =
      (b->shader->info.outputs_written & color_mask) |
      /* both dual source blend outputs use FRAG_RESULT_DATA0 slot in nir,
       * but we use an extra slot number in lower_ps_state for the second
       * output
       */
      BITFIELD_BIT(DUAL_SRC_BLEND_SLOT);

   u_foreach_bit (slot, color_outputs) {
      if (s->options->clamp_color) {
         for (int i = 0; i < 4; i++) {
            if (s->outputs[slot][i])
               s->outputs[slot][i] = nir_fsat(b, s->outputs[slot][i]);
         }
      }

      if (s->options->alpha_to_one) {
         /* any one has written to this slot */
         if (s->output_types[slot] != nir_type_invalid) {
            unsigned bit_size = nir_alu_type_get_type_size(s->output_types[slot]);
            s->outputs[slot][3] = nir_imm_floatN_t(b, 1, bit_size);
         }
      }

      if (slot == FRAG_RESULT_COLOR || slot == FRAG_RESULT_DATA0) {
         if (s->options->alpha_func == PIPE_FUNC_ALWAYS) {
            /* always pass, do nothing */
         } else if (s->options->alpha_func == PIPE_FUNC_NEVER) {
            nir_discard(b);
         } else if (s->outputs[slot][3]) {
            nir_ssa_def *ref = nir_load_alpha_reference_amd(b);
            nir_ssa_def *cond =
               nir_compare_func(b, s->options->alpha_func, s->outputs[slot][3], ref);
            nir_discard_if(b, nir_inot(b, cond));
         }
      }
   }
}

static void
emit_ps_mrtz_export(nir_builder *b, lower_ps_state *s)
{
   nir_ssa_def *mrtz_alpha = NULL;
   if (s->options->alpha_to_coverage_via_mrtz) {
      mrtz_alpha = s->outputs[FRAG_RESULT_COLOR][3] ?
         s->outputs[FRAG_RESULT_COLOR][3] :
         s->outputs[FRAG_RESULT_DATA0][3];
   }

   nir_ssa_def *depth = s->outputs[FRAG_RESULT_DEPTH][0];
   nir_ssa_def *stencil = s->outputs[FRAG_RESULT_STENCIL][0];
   nir_ssa_def *sample_mask = s->outputs[FRAG_RESULT_SAMPLE_MASK][0];

   /* skip mrtz export if no one has written to any of them */
   if (!depth && !stencil && !sample_mask && !mrtz_alpha)
      return;

   uint64_t outputs_written = b->shader->info.outputs_written;
   /* use outputs_written to determine export format as we use it to set
    * R_028710_SPI_SHADER_Z_FORMAT instead of relying on the real store ouput,
    * because store ouput may be optimized out.
    */
   unsigned format =
      ac_get_spi_shader_z_format(outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH),
                                 outputs_written & BITFIELD64_BIT(FRAG_RESULT_STENCIL),
                                 outputs_written & BITFIELD64_BIT(FRAG_RESULT_SAMPLE_MASK),
                                 s->options->alpha_to_coverage_via_mrtz);

   nir_ssa_def *undef = nir_ssa_undef(b, 1, 32);
   nir_ssa_def *outputs[4] = {undef, undef, undef, undef};
   unsigned write_mask = 0;
   unsigned flags = 0;

   if (format == V_028710_SPI_SHADER_UINT16_ABGR) {
      assert(!depth && !mrtz_alpha);

      if (s->options->gfx_level < GFX11)
         flags |= AC_EXP_FLAG_COMPRESSED;

      if (stencil) {
         outputs[0] = nir_ishl_imm(b, stencil, 16);
         write_mask |= s->options->gfx_level >= GFX11 ? 0x1 : 0x3;
      }

      if (sample_mask) {
         outputs[1] = sample_mask;
         write_mask |= s->options->gfx_level >= GFX11 ? 0x2 : 0xc;
      }
   } else {
      if (depth) {
         outputs[0] = depth;
         write_mask |= 0x1;
      }

      if (stencil) {
         outputs[1] = stencil;
         write_mask |= 0x2;
      }

      if (sample_mask) {
         outputs[2] = sample_mask;
         write_mask |= 0x4;
      }

      if (mrtz_alpha) {
         outputs[3] = mrtz_alpha;
         write_mask |= 0x8;
      }
   }

   /* GFX6 (except OLAND and HAINAN) has a bug that it only looks at the
    * X writemask component.
    */
   if (s->options->gfx_level == GFX6 &&
       s->options->family != CHIP_OLAND &&
       s->options->family != CHIP_HAINAN) {
      write_mask |= 0x1;
   }

   s->exp[s->exp_num++] = nir_export_amd(b, nir_vec(b, outputs, 4),
                                         .base = V_008DFC_SQ_EXP_MRTZ,
                                         .write_mask = write_mask,
                                         .flags = flags);
}

static unsigned
get_ps_color_export_target(lower_ps_state *s)
{
   unsigned target = V_008DFC_SQ_EXP_MRT + s->compacted_mrt_index;

   if (s->options->dual_src_blend_swizzle && s->compacted_mrt_index < 2)
      target += 21;

   s->compacted_mrt_index++;

   return target;
}

static bool
emit_ps_color_export(nir_builder *b, lower_ps_state *s, gl_frag_result slot, unsigned cbuf)
{
   assert(cbuf < 8);

   unsigned spi_shader_col_format = (s->options->spi_shader_col_format >> (cbuf * 4)) & 0xf;
   if (spi_shader_col_format == V_028714_SPI_SHADER_ZERO)
      return false;

   /* get target after checking spi_shader_col_format as we need to increase
    * compacted_mrt_index anyway regardless of whether the export is built
    */
   unsigned target = get_ps_color_export_target(s);

   nir_alu_type type = s->output_types[slot];
   /* no one has written to this slot */
   if (type == nir_type_invalid)
      return false;

   bool is_int8 = s->options->color_is_int8 & BITFIELD_BIT(cbuf);
   bool is_int10 = s->options->color_is_int10 & BITFIELD_BIT(cbuf);
   bool enable_mrt_output_nan_fixup =
      s->options->enable_mrt_output_nan_fixup & BITFIELD_BIT(cbuf);

   nir_ssa_def *undef = nir_ssa_undef(b, 1, 32);
   nir_ssa_def *outputs[4] = {undef, undef, undef, undef};
   unsigned write_mask = 0;
   unsigned flags = 0;

   nir_alu_type base_type = nir_alu_type_get_base_type(type);
   unsigned type_size = nir_alu_type_get_type_size(type);

   nir_ssa_def *data[4];
   memcpy(data, s->outputs[slot], sizeof(data));

   /* Replace NaN by zero (for 32-bit float formats) to fix game bugs if requested. */
   if (enable_mrt_output_nan_fixup && type == nir_type_float32) {
      for (int i = 0; i < 4; i++) {
         if (data[i]) {
            nir_ssa_def *isnan = nir_fisnan(b, data[i]);
            data[i] = nir_bcsel(b, isnan, nir_imm_float(b, 0), data[i]);
         }
      }
   }

   switch (spi_shader_col_format) {
   case V_028714_SPI_SHADER_32_R:
      if (!data[0])
         return false;

      outputs[0] = nir_convert_to_bit_size(b, data[0], base_type, 32);
      write_mask = 0x1;
      break;

   case V_028714_SPI_SHADER_32_GR:
      if (!data[0] && !data[1])
         return false;

      if (data[0]) {
         outputs[0] = nir_convert_to_bit_size(b, data[0], base_type, 32);
         write_mask |= 0x1;
      }

      if (data[1]) {
         outputs[1] = nir_convert_to_bit_size(b, data[1], base_type, 32);
         write_mask |= 0x2;
      }
      break;

   case V_028714_SPI_SHADER_32_AR:
      if (!data[0] && !data[3])
         return false;

      if (data[0]) {
         outputs[0] = nir_convert_to_bit_size(b, data[0], base_type, 32);
         write_mask |= 0x1;
      }

      if (data[3]) {
         unsigned index = s->options->gfx_level >= GFX10 ? 1 : 3;
         outputs[index] = nir_convert_to_bit_size(b, data[3], base_type, 32);
         write_mask |= BITFIELD_BIT(index);
      }
      break;

   case V_028714_SPI_SHADER_32_ABGR:
      for (int i = 0; i < 4; i++) {
         if (data[i]) {
            outputs[i] = nir_convert_to_bit_size(b, data[i], base_type, 32);
            write_mask |= BITFIELD_BIT(i);
         }
      }
      break;

   default: {
      nir_op pack_op = nir_op_pack_32_2x16;

      switch (spi_shader_col_format) {
      case V_028714_SPI_SHADER_FP16_ABGR:
         if (type_size == 32)
            pack_op = nir_op_pack_half_2x16;
         break;
      case V_028714_SPI_SHADER_UINT16_ABGR:
         if (type_size == 32) {
            pack_op = nir_op_pack_uint_2x16;
            if (is_int8 || is_int10) {
               /* clamp 32bit output for 8/10 bit color component */
               uint32_t max_rgb = is_int8 ? 255 : 1023;

               for (int i = 0; i < 4; i++) {
                  if (!data[i])
                     continue;

                  uint32_t max_value = i == 3 && is_int10 ? 3 : max_rgb;
                  data[i] = nir_umin(b, data[i], nir_imm_int(b, max_value));
               }
            }
         }
         break;
      case V_028714_SPI_SHADER_SINT16_ABGR:
         if (type_size == 32) {
            pack_op = nir_op_pack_sint_2x16;
            if (is_int8 || is_int10) {
               /* clamp 32bit output for 8/10 bit color component */
               uint32_t max_rgb = is_int8 ? 127 : 511;
               uint32_t min_rgb = is_int8 ? -128 : -512;

               for (int i = 0; i < 4; i++) {
                  if (!data[i])
                     continue;

                  uint32_t max_value = i == 3 && is_int10 ? 1 : max_rgb;
                  uint32_t min_value = i == 3 && is_int10 ? -2u : min_rgb;

                  data[i] = nir_imin(b, data[i], nir_imm_int(b, max_value));
                  data[i] = nir_imax(b, data[i], nir_imm_int(b, min_value));
               }
            }
         }
         break;
      case V_028714_SPI_SHADER_UNORM16_ABGR:
         pack_op = nir_op_pack_unorm_2x16;
         break;
      case V_028714_SPI_SHADER_SNORM16_ABGR:
         pack_op = nir_op_pack_snorm_2x16;
         break;
      default:
         unreachable("unsupport color export format");
         break;
      }

      for (int i = 0; i < 2; i++) {
         nir_ssa_def *lo = data[i * 2];
         nir_ssa_def *hi = data[i * 2 + 1];
         if (!lo && !hi)
            continue;

         lo = lo ? lo : nir_ssa_undef(b, 1, type_size);
         hi = hi ? hi : nir_ssa_undef(b, 1, type_size);
         nir_ssa_def *vec = nir_vec2(b, lo, hi);

         outputs[i] = nir_build_alu1(b, pack_op, vec);

         if (s->options->gfx_level >= GFX11)
            write_mask |= BITFIELD_BIT(i);
         else
            write_mask |= 0x3 << (i * 2);
      }

      if (s->options->gfx_level < GFX11)
         flags |= AC_EXP_FLAG_COMPRESSED;
   }
   }

   s->exp[s->exp_num++] = nir_export_amd(b, nir_vec(b, outputs, 4),
                                         .base = target,
                                         .write_mask = write_mask,
                                         .flags = flags);
   return true;
}

static void
emit_ps_dual_src_blend_swizzle(nir_builder *b, lower_ps_state *s, unsigned first_color_export)
{
   assert(s->exp_num > first_color_export + 1);

   nir_intrinsic_instr *mrt0_exp = s->exp[first_color_export];
   nir_intrinsic_instr *mrt1_exp = s->exp[first_color_export + 1];

   /* There are some instructions which operate mrt1_exp's argument
    * between mrt0_exp and mrt1_exp. Move mrt0_exp next to mrt1_exp,
    * so that we can swizzle their arguments.
    */
   unsigned target0 = nir_intrinsic_base(mrt0_exp);
   unsigned target1 = nir_intrinsic_base(mrt1_exp);
   if (target0 > target1) {
      /* mrt0 export is after mrt1 export, this happens when src0 is missing,
       * so we emit mrt1 first then emit an empty mrt0.
       *
       * swap the pointer
       */
      nir_intrinsic_instr *tmp = mrt0_exp;
      mrt0_exp = mrt1_exp;
      mrt1_exp = tmp;

      /* move mrt1_exp down to after mrt0_exp */
      nir_instr_move(nir_after_instr(&mrt0_exp->instr), &mrt1_exp->instr);
   } else {
      /* move mrt0_exp down to before mrt1_exp */
      nir_instr_move(nir_before_instr(&mrt1_exp->instr), &mrt0_exp->instr);
   }

   uint32_t mrt0_write_mask = nir_intrinsic_write_mask(mrt0_exp);
   uint32_t mrt1_write_mask = nir_intrinsic_write_mask(mrt1_exp);
   uint32_t write_mask = mrt0_write_mask | mrt1_write_mask;

   nir_ssa_def *mrt0_arg = mrt0_exp->src[0].ssa;
   nir_ssa_def *mrt1_arg = mrt1_exp->src[0].ssa;

   /* Swizzle code is right before mrt0_exp. */
   b->cursor = nir_before_instr(&mrt0_exp->instr);

   nir_ssa_def *undef = nir_ssa_undef(b, 1, 32);
   nir_ssa_def *arg0_vec[4] = {undef, undef, undef, undef};
   nir_ssa_def *arg1_vec[4] = {undef, undef, undef, undef};

   /* For illustration, originally
    *   lane0 export arg00 and arg01
    *   lane1 export arg10 and arg11.
    *
    * After the following operation
    *   lane0 export arg00 and arg10
    *   lane1 export arg01 and arg11.
    */
   u_foreach_bit (i, write_mask) {
      nir_ssa_def *arg0 = nir_channel(b, mrt0_arg, i);
      nir_ssa_def *arg1 = nir_channel(b, mrt1_arg, i);

      /* swap odd,even lanes of arg0 */
      arg0 = nir_quad_swizzle_amd(b, arg0, .swizzle_mask = 0b10110001);

      /* swap even lanes between arg0 and arg1 */
      nir_ssa_def *tid = nir_load_subgroup_invocation(b);
      nir_ssa_def *is_even = nir_ieq_imm(b, nir_iand_imm(b, tid, 1), 0);

      nir_ssa_def *tmp = arg0;
      arg0 = nir_bcsel(b, is_even, arg1, arg0);
      arg1 = nir_bcsel(b, is_even, tmp, arg1);

      /* swap odd,even lanes again for arg0 */
      arg0 = nir_quad_swizzle_amd(b, arg0, .swizzle_mask = 0b10110001);

      arg0_vec[i] = arg0;
      arg1_vec[i] = arg1;
   }

   nir_instr_rewrite_src_ssa(&mrt0_exp->instr, &mrt0_exp->src[0], nir_vec(b, arg0_vec, 4));
   nir_instr_rewrite_src_ssa(&mrt1_exp->instr, &mrt1_exp->src[0], nir_vec(b, arg1_vec, 4));

   nir_intrinsic_set_write_mask(mrt0_exp, write_mask);
   nir_intrinsic_set_write_mask(mrt1_exp, write_mask);
}

static void
emit_ps_null_export(nir_builder *b, lower_ps_state *s)
{
   /* Gfx10+ doesn't need to export anything if we don't need to export the EXEC mask
    * for discard.
    */
   if (s->options->gfx_level >= GFX10 && !s->options->uses_discard)
      return;

   /* Gfx11 doesn't support null exports, and mrt0 should be exported instead. */
   unsigned target = s->options->gfx_level >= GFX11 ?
      V_008DFC_SQ_EXP_MRT : V_008DFC_SQ_EXP_NULL;

   nir_intrinsic_instr *intrin =
      nir_export_amd(b, nir_ssa_undef(b, 4, 32),
                     .base = target,
                     .flags = AC_EXP_FLAG_VALID_MASK | AC_EXP_FLAG_DONE);
   /* To avoid builder set write mask to 0xf. */
   nir_intrinsic_set_write_mask(intrin, 0);
}

static void
export_ps_outputs(nir_shader *nir, lower_ps_state *s)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);

   nir_builder builder;
   nir_builder *b = &builder;
   nir_builder_init(b, impl);

   b->cursor = nir_after_cf_list(&impl->body);

   emit_ps_color_clamp_and_alpha_test(b, s);

   emit_ps_mrtz_export(b, s);

   unsigned first_color_export = s->exp_num;

   /* When dual src blend is enabled and we need both src0 and src1
    * export present, try to export both src, and add an empty export
    * for either src missing.
    */
   if (s->output_types[DUAL_SRC_BLEND_SLOT] != nir_type_invalid ||
       s->options->dual_src_blend_swizzle) {
      unsigned slot;
      if (s->output_types[FRAG_RESULT_COLOR] != nir_type_invalid) {
         /* when dual source blending, there must be only one color buffer */
         assert(s->options->broadcast_last_cbuf == 0);
         slot = FRAG_RESULT_COLOR;
      } else {
         slot = FRAG_RESULT_DATA0;
      }

      bool src0_exported = emit_ps_color_export(b, s, slot, 0);
      /* src1 use cubf1 info, when dual src blend is enabled it's
       * same as cbuf0, but when dual src blend is disabled it's used
       * to disable src1 export.
       */
      bool src1_exported = emit_ps_color_export(b, s, DUAL_SRC_BLEND_SLOT, 1);

      bool need_empty_export =
         /* miss src1, need to add src1 only when swizzle case */
         (src0_exported && !src1_exported && s->options->dual_src_blend_swizzle) ||
         /* miss src0, always need to add src0 */
         (!src0_exported && src1_exported);

      if (need_empty_export) {
         /* set to expected value */
         s->compacted_mrt_index = src0_exported ? 1 : 0;

         unsigned target = get_ps_color_export_target(s);

         s->exp[s->exp_num++] =
            nir_export_amd(b, nir_ssa_undef(b, 4, 32), .base = target);
      }
   } else {
      if (s->output_types[FRAG_RESULT_COLOR] != nir_type_invalid) {
         /* write to all color buffers */
         for (int cbuf = 0; cbuf <= s->options->broadcast_last_cbuf; cbuf++)
            emit_ps_color_export(b, s, FRAG_RESULT_COLOR, cbuf);
      } else {
         for (int cbuf = 0; cbuf < MAX_DRAW_BUFFERS; cbuf++) {
            unsigned slot = FRAG_RESULT_DATA0 + cbuf;
            emit_ps_color_export(b, s, slot, cbuf);
         }
      }
   }

   if (s->exp_num) {
      if (s->options->dual_src_blend_swizzle)
         emit_ps_dual_src_blend_swizzle(b, s, first_color_export);

      /* Specify that this is the last export */
      nir_intrinsic_instr *final_exp = s->exp[s->exp_num - 1];
      unsigned final_exp_flags = nir_intrinsic_flags(final_exp);
      final_exp_flags |= AC_EXP_FLAG_DONE | AC_EXP_FLAG_VALID_MASK;
      nir_intrinsic_set_flags(final_exp, final_exp_flags);
   } else {
      emit_ps_null_export(b, s);
   }
}

void
ac_nir_lower_ps(nir_shader *nir, const ac_nir_lower_ps_options *options)
{
   lower_ps_state state = {
      .options = options,
   };

   nir_shader_instructions_pass(nir, lower_ps_intrinsic,
                                nir_metadata_block_index | nir_metadata_dominance,
                                &state);

   export_ps_outputs(nir, &state);
}
