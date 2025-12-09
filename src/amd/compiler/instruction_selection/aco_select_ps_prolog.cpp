/*
 * Copyright © 2018 Valve Corporation
 * Copyright © 2023 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_builder.h"
#include "aco_instruction_selection.h"
#include "aco_ir.h"

namespace aco {
namespace {

void
emit_polygon_stipple(isel_context* ctx, const struct aco_ps_prolog_info* finfo)
{
   Builder bld(ctx->program, ctx->block);

   /* Use the fixed-point gl_FragCoord input.
    * Since the stipple pattern is 32x32 and it repeats, just get 5 bits
    * per coordinate to get the repeating effect.
    */
   Temp pos_fixed_pt = get_arg(ctx, ctx->args->pos_fixed_pt);
   Temp addr0 = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand::c32(0x1f), pos_fixed_pt);
   Temp addr1 = bld.vop3(aco_opcode::v_bfe_u32, bld.def(v1), pos_fixed_pt, Operand::c32(16u),
                         Operand::c32(5u));

   /* Load the buffer descriptor. */
   Temp list = get_arg(ctx, finfo->internal_bindings);
   list = convert_pointer_to_64_bit(ctx, list);
   Temp desc = bld.smem(aco_opcode::s_load_dwordx4, bld.def(s4), list,
                        Operand::c32(finfo->poly_stipple_buf_offset));

   /* The stipple pattern is 32x32, each row has 32 bits. */
   Temp offset = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(2), addr1);
   Temp row = bld.mubuf(aco_opcode::buffer_load_dword, bld.def(v1), desc, offset, Operand::c32(0u),
                        0, true);
   Temp bit = bld.vop3(aco_opcode::v_bfe_u32, bld.def(v1), row, addr0, Operand::c32(1u));
   Temp cond = bld.vopc(aco_opcode::v_cmp_eq_u32, bld.def(bld.lm), Operand::zero(), bit);
   bld.pseudo(aco_opcode::p_demote_to_helper, cond);

   ctx->block->kind |= block_kind_uses_discard;
   ctx->program->needs_exact = true;
}

void
overwrite_interp_args(isel_context* ctx, const struct aco_ps_prolog_info* finfo)
{
   Builder bld(ctx->program, ctx->block);

   if (finfo->bc_optimize_for_persp || finfo->bc_optimize_for_linear) {
      /* The shader should do: if (PRIM_MASK[31]) CENTROID = CENTER;
       * The hw doesn't compute CENTROID if the whole wave only
       * contains fully-covered quads.
       */
      Temp bc_optimize = get_arg(ctx, ctx->args->prim_mask);

      /* enabled when bit 31 is set */
      Temp cond =
         bld.sopc(aco_opcode::s_bitcmp1_b32, bld.def(s1, scc), bc_optimize, Operand::c32(31u));

      /* scale 1bit scc to wave size bits used by v_cndmask */
      cond = bool_to_vector_condition(ctx, cond);

      if (finfo->bc_optimize_for_persp) {
         Temp center = get_arg(ctx, ctx->args->persp_center);
         Temp centroid = get_arg(ctx, ctx->args->persp_centroid);

         Temp dst = bld.tmp(v2);
         select_vec2(ctx, dst, cond, center, centroid);
         ctx->arg_temps[ctx->args->persp_centroid.arg_index] = dst;
      }

      if (finfo->bc_optimize_for_linear) {
         Temp center = get_arg(ctx, ctx->args->linear_center);
         Temp centroid = get_arg(ctx, ctx->args->linear_centroid);

         Temp dst = bld.tmp(v2);
         select_vec2(ctx, dst, cond, center, centroid);
         ctx->arg_temps[ctx->args->linear_centroid.arg_index] = dst;
      }
   }

   if (finfo->force_persp_sample_interp) {
      Temp persp_sample = get_arg(ctx, ctx->args->persp_sample);
      ctx->arg_temps[ctx->args->persp_center.arg_index] = persp_sample;
      ctx->arg_temps[ctx->args->persp_centroid.arg_index] = persp_sample;
   }

   if (finfo->force_linear_sample_interp) {
      Temp linear_sample = get_arg(ctx, ctx->args->linear_sample);
      ctx->arg_temps[ctx->args->linear_center.arg_index] = linear_sample;
      ctx->arg_temps[ctx->args->linear_centroid.arg_index] = linear_sample;
   }

   if (finfo->force_persp_center_interp) {
      Temp persp_center = get_arg(ctx, ctx->args->persp_center);
      ctx->arg_temps[ctx->args->persp_sample.arg_index] = persp_center;
      ctx->arg_temps[ctx->args->persp_centroid.arg_index] = persp_center;
   }

   if (finfo->force_linear_center_interp) {
      Temp linear_center = get_arg(ctx, ctx->args->linear_center);
      ctx->arg_temps[ctx->args->linear_sample.arg_index] = linear_center;
      ctx->arg_temps[ctx->args->linear_centroid.arg_index] = linear_center;
   }
}

void
overwrite_samplemask_arg(isel_context* ctx, const struct aco_ps_prolog_info* finfo)
{
   Builder bld(ctx->program, ctx->block);

   /* Section 15.2.2 (Shader Inputs) of the OpenGL 4.5 (Core Profile) spec
    * says:
    *
    *    "When per-sample shading is active due to the use of a fragment
    *     input qualified by sample or due to the use of the gl_SampleID
    *     or gl_SamplePosition variables, only the bit for the current
    *     sample is set in gl_SampleMaskIn. When state specifies multiple
    *     fragment shader invocations for a given fragment, the sample
    *     mask for any single fragment shader invocation may specify a
    *     subset of the covered samples for the fragment. In this case,
    *     the bit corresponding to each covered sample will be set in
    *     exactly one fragment shader invocation."
    *
    * The samplemask loaded by hardware is always the coverage of the
    * entire pixel/fragment, so mask bits out based on the sample ID.
    */
   if (finfo->samplemask_log_ps_iter) {
      Temp ancillary = get_arg(ctx, ctx->args->ancillary);
      Temp sampleid = bld.vop3(aco_opcode::v_bfe_u32, bld.def(v1), ancillary, Operand::c32(8u),
                               Operand::c32(4u));
      Temp samplemask;

      if (finfo->samplemask_log_ps_iter == 3) {
         Temp is_helper_invoc =
            bld.pseudo(aco_opcode::p_is_helper, bld.def(bld.lm), Operand(exec, bld.lm));
         ctx->program->needs_exact = true;

         /* samplemask = is_helper ? 0 : (1 << sample_id); */
         samplemask =
            bld.vop2_e64(aco_opcode::v_lshlrev_b32, bld.def(v1), sampleid, Operand::c32(1u));
         samplemask = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), samplemask,
                                   Operand::c32(0u), is_helper_invoc);
      } else {
         /* samplemask &= ps_iter_mask << sample_id; */
         uint32_t ps_iter_mask = ac_get_ps_iter_mask(1 << finfo->samplemask_log_ps_iter);
         Builder::Op mask = ctx->options->gfx_level >= GFX11
                               ? Operand::c32(ps_iter_mask)
                               : bld.copy(bld.def(v1), Operand::c32(ps_iter_mask));

         samplemask = bld.vop2_e64(aco_opcode::v_lshlrev_b32, bld.def(v1), sampleid, mask);
         samplemask = bld.vop2(aco_opcode::v_and_b32, bld.def(v1),
                               get_arg(ctx, ctx->args->sample_coverage), samplemask);
      }

      ctx->arg_temps[ctx->args->sample_coverage.arg_index] = samplemask;
   } else if (finfo->force_samplemask_to_helper_invocation) {
      Temp is_helper_invoc =
         bld.pseudo(aco_opcode::p_is_helper, bld.def(bld.lm), Operand(exec, bld.lm));
      ctx->program->needs_exact = true;

      ctx->arg_temps[ctx->args->sample_coverage.arg_index] =
         bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), Operand::c32(1u), Operand::c32(0u),
                      is_helper_invoc);
   }
}
void
overwrite_pos_xy_args(isel_context* ctx, const struct aco_ps_prolog_info* finfo)
{
   if (!finfo->get_frag_coord_from_pixel_coord)
      return;

   Builder bld(ctx->program, ctx->block);
   Temp pos_fixed_pt = get_arg(ctx, ctx->args->pos_fixed_pt);

   for (unsigned i = 0; i < 2; i++) {
      if (!ctx->args->frag_pos[i].used)
         continue;

      Temp t;
      if (i)
         t = bld.vop2(aco_opcode::v_lshrrev_b32, bld.def(v1), Operand::c32(16), pos_fixed_pt);
      else
         t = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand::c32(0xffff), pos_fixed_pt);

      t = bld.vop1(aco_opcode::v_cvt_f32_u32, bld.def(v1), t);
      if (!finfo->pixel_center_integer)
         t = bld.vop2(aco_opcode::v_add_f32, bld.def(v1), Operand::c32(0x3f000000 /*0.5*/), t);

      ctx->arg_temps[ctx->args->frag_pos[i].arg_index] = t;
   }
}

void
passthrough_all_args(isel_context* ctx, std::vector<Operand>& regs)
{
   struct ac_arg arg;
   arg.used = true;

   for (arg.arg_index = 0; arg.arg_index < ctx->args->arg_count; arg.arg_index++)
      regs.emplace_back(Operand(get_arg(ctx, arg), get_arg_reg(ctx->args, arg)));
}

Temp
get_interp_color(isel_context* ctx, int interp_vgpr, unsigned attr_index, unsigned comp)
{
   Builder bld(ctx->program, ctx->block);

   Temp dst = bld.tmp(v1);

   Temp prim_mask = get_arg(ctx, ctx->args->prim_mask);

   if (interp_vgpr != -1) {
      /* interp args are all 2 vgprs */
      int arg_index = ctx->args->persp_sample.arg_index + interp_vgpr / 2;
      Temp interp_ij = ctx->arg_temps[arg_index];

      emit_interp_instr(ctx, attr_index, comp, interp_ij, dst, prim_mask, false);
   } else {
      emit_interp_mov_instr(ctx, attr_index, comp, 0, dst, prim_mask, false);
   }

   return dst;
}

void
interpolate_color_args(isel_context* ctx, const struct aco_ps_prolog_info* finfo,
                       std::vector<Operand>& regs)
{
   if (!finfo->colors_read)
      return;

   Builder bld(ctx->program, ctx->block);

   unsigned vgpr = 256 + ctx->args->num_vgprs_used;

   if (finfo->color_two_side) {
      Temp face = get_arg(ctx, ctx->args->front_face);
      Temp is_face_positive =
         bld.vopc(aco_opcode::v_cmp_lt_f32, bld.def(bld.lm), Operand::zero(), face);

      u_foreach_bit (i, finfo->colors_read) {
         unsigned color_index = i / 4;
         unsigned front_index = finfo->color_attr_index[color_index];
         int interp_vgpr = finfo->color_interp_vgpr_index[color_index];

         /* If BCOLOR0 is used, BCOLOR1 is at offset "num_inputs + 1",
          * otherwise it's at offset "num_inputs".
          */
         unsigned back_index = finfo->num_interp_inputs;
         if (color_index == 1 && finfo->colors_read & 0xf)
            back_index++;

         Temp front = get_interp_color(ctx, interp_vgpr, front_index, i % 4);
         Temp back = get_interp_color(ctx, interp_vgpr, back_index, i % 4);

         Temp color =
            bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), back, front, is_face_positive);

         regs.emplace_back(Operand(color, PhysReg{vgpr++}));
      }
   } else {
      u_foreach_bit (i, finfo->colors_read) {
         unsigned color_index = i / 4;
         unsigned attr_index = finfo->color_attr_index[color_index];
         int interp_vgpr = finfo->color_interp_vgpr_index[color_index];
         Temp color = get_interp_color(ctx, interp_vgpr, attr_index, i % 4);

         regs.emplace_back(Operand(color, PhysReg{vgpr++}));
      }
   }
}

} // namespace

void
select_ps_prolog(Program* program, void* pinfo, ac_shader_config* config,
                 const struct aco_compiler_options* options, const struct aco_shader_info* info,
                 const struct ac_shader_args* args)
{
   const struct aco_ps_prolog_info* finfo = (const struct aco_ps_prolog_info*)pinfo;
   isel_context ctx =
      setup_isel_context(program, 0, NULL, config, options, info, args, SWStage::FS);

   ctx.block->fp_mode = program->next_fp_mode;

   add_startpgm(&ctx);
   append_logical_start(ctx.block);

   if (finfo->poly_stipple)
      emit_polygon_stipple(&ctx, finfo);

   overwrite_interp_args(&ctx, finfo);
   overwrite_samplemask_arg(&ctx, finfo);
   overwrite_pos_xy_args(&ctx, finfo);

   std::vector<Operand> regs;
   passthrough_all_args(&ctx, regs);

   interpolate_color_args(&ctx, finfo, regs);

   program->config->float_mode = program->blocks[0].fp_mode.val;

   append_logical_end(&ctx);

   build_end_with_regs(&ctx, regs);

   /* To compute all end args in WQM mode if required by main part. */
   if (finfo->needs_wqm)
      set_wqm(&ctx, true);

   /* Exit WQM mode finally. */
   program->needs_exact = true;

   finish_program(&ctx);
}

} // namespace aco
