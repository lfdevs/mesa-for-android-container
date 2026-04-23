/*
 * Copyright © 2018 Valve Corporation
 * Copyright © 2018 Google
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_builder.h"
#include "aco_instruction_selection.h"
#include "aco_ir.h"
#include "aco_nir_call_attribs.h"

#include "amdgfxregs.h"
#include <array>
#include <utility>
#include <vector>

namespace aco {
namespace {

void
visit_load_const(isel_context* ctx, nir_load_const_instr* instr)
{
   Temp dst = get_ssa_temp(ctx, &instr->def);

   // TODO: we really want to have the resulting type as this would allow for 64bit literals
   // which get truncated the lsb if double and msb if int
   // for now, we only use s_mov_b64 with 64bit inline constants
   assert(instr->def.num_components == 1 && "Vector load_const should be lowered to scalar.");
   assert(dst.type() == RegType::sgpr);

   Builder bld(ctx->program, ctx->block);

   if (instr->def.bit_size == 1) {
      assert(dst.regClass() == bld.lm);
      int val = instr->value[0].b ? -1 : 0;
      Operand op = bld.lm.size() == 1 ? Operand::c32(val) : Operand::c64(val);
      bld.copy(Definition(dst), op);
   } else if (instr->def.bit_size == 8) {
      bld.copy(Definition(dst), Operand::c32(instr->value[0].u8));
   } else if (instr->def.bit_size == 16) {
      /* sign-extend to use s_movk_i32 instead of a literal */
      bld.copy(Definition(dst), Operand::c32(instr->value[0].i16));
   } else if (dst.size() == 1) {
      bld.copy(Definition(dst), Operand::c32(instr->value[0].u32));
   } else {
      assert(dst.size() != 1);
      aco_ptr<Instruction> vec{
         create_instruction(aco_opcode::p_create_vector, Format::PSEUDO, dst.size(), 1)};
      if (instr->def.bit_size == 64)
         for (unsigned i = 0; i < dst.size(); i++)
            vec->operands[i] = Operand::c32(instr->value[0].u64 >> i * 32);
      else {
         for (unsigned i = 0; i < dst.size(); i++)
            vec->operands[i] = Operand::c32(instr->value[i].u32);
      }
      vec->definitions[0] = Definition(dst);
      ctx->block->instructions.emplace_back(std::move(vec));
   }
}

Temp merged_wave_info_to_mask(isel_context* ctx, unsigned i);

void
get_const_vec(nir_def* vec, nir_const_value* cv[4])
{
   nir_alu_instr* vec_instr = nir_def_as_alu_or_null(vec);
   if (!vec_instr || vec_instr->op != nir_op_vec(vec->num_components))
      return;

   for (unsigned i = 0; i < vec->num_components; i++) {
      cv[i] =
         vec_instr->src[i].swizzle[0] == 0 ? nir_src_as_const_value(vec_instr->src[i].src) : NULL;
   }
}

void
visit_tex(isel_context* ctx, nir_tex_instr* instr)
{
   assert(instr->op != nir_texop_samples_identical);

   Builder bld(ctx->program, ctx->block);
   bool disable_wqm = instr->skip_helpers;
   bool has_bias = false, has_lod = false, level_zero = false, has_compare = false,
        has_offset = false, has_ddx = false, has_ddy = false, has_derivs = false,
        has_sample_index = false, has_clamped_lod = false, has_wqm_coord = false;
   Temp resource, sampler, bias = Temp(), compare = Temp(), sample_index = Temp(), lod = Temp(),
                           offset = Temp(), ddx = Temp(), ddy = Temp(), clamped_lod = Temp(),
                           coord = Temp(), wqm_coord = Temp();
   std::vector<Temp> coords;
   std::vector<Temp> derivs;
   nir_const_value* const_offset[4] = {NULL, NULL, NULL, NULL};

   for (unsigned i = 0; i < instr->num_srcs; i++) {
      switch (instr->src[i].src_type) {
      case nir_tex_src_texture_handle:
         resource = bld.as_uniform(get_ssa_temp(ctx, instr->src[i].src.ssa));
         break;
      case nir_tex_src_sampler_handle:
         sampler = bld.as_uniform(get_ssa_temp(ctx, instr->src[i].src.ssa));
         break;
      default: break;
      }
   }

   bool tg4_integer_workarounds = ctx->options->gfx_level <= GFX8 && instr->op == nir_texop_tg4 &&
                                  (instr->dest_type & (nir_type_int | nir_type_uint));
   bool tg4_integer_cube_workaround =
      tg4_integer_workarounds && instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE;

   bool a16 = false, g16 = false;

   int coord_idx = nir_tex_instr_src_index(instr, nir_tex_src_coord);
   if (coord_idx >= 0)
      a16 = instr->src[coord_idx].src.ssa->bit_size == 16;

   int ddx_idx = nir_tex_instr_src_index(instr, nir_tex_src_ddx);
   if (ddx_idx >= 0)
      g16 = instr->src[ddx_idx].src.ssa->bit_size == 16;

   for (unsigned i = 0; i < instr->num_srcs; i++) {
      switch (instr->src[i].src_type) {
      case nir_tex_src_coord: {
         assert(instr->src[i].src.ssa->bit_size == (a16 ? 16 : 32));
         coord = get_ssa_temp_tex(ctx, instr->src[i].src.ssa, a16);
         break;
      }
      case nir_tex_src_backend1: {
         assert(instr->src[i].src.ssa->bit_size == 32);
         wqm_coord = get_ssa_temp(ctx, instr->src[i].src.ssa);
         has_wqm_coord = true;
         break;
      }
      case nir_tex_src_bias:
         assert(instr->src[i].src.ssa->bit_size == (a16 ? 16 : 32));
         /* Doesn't need get_ssa_temp_tex because we pack it into its own dword anyway. */
         bias = get_ssa_temp(ctx, instr->src[i].src.ssa);
         has_bias = true;
         break;
      case nir_tex_src_lod: {
         if (nir_src_is_const(instr->src[i].src) && nir_src_as_uint(instr->src[i].src) == 0) {
            level_zero = true;
         } else {
            assert(instr->src[i].src.ssa->bit_size == (a16 ? 16 : 32));
            lod = get_ssa_temp_tex(ctx, instr->src[i].src.ssa, a16);
            has_lod = true;
         }
         break;
      }
      case nir_tex_src_min_lod:
         assert(instr->src[i].src.ssa->bit_size == (a16 ? 16 : 32));
         clamped_lod = get_ssa_temp_tex(ctx, instr->src[i].src.ssa, a16);
         has_clamped_lod = true;
         break;
      case nir_tex_src_comparator:
         if (instr->is_shadow) {
            assert(instr->src[i].src.ssa->bit_size == 32);
            compare = get_ssa_temp(ctx, instr->src[i].src.ssa);
            has_compare = true;
         }
         break;
      case nir_tex_src_offset:
      case nir_tex_src_backend2:
         assert(instr->src[i].src.ssa->bit_size == 32);
         offset = get_ssa_temp(ctx, instr->src[i].src.ssa);
         get_const_vec(instr->src[i].src.ssa, const_offset);
         has_offset = true;
         break;
      case nir_tex_src_ddx:
         assert(instr->src[i].src.ssa->bit_size == (g16 ? 16 : 32));
         ddx = get_ssa_temp_tex(ctx, instr->src[i].src.ssa, g16);
         has_ddx = true;
         break;
      case nir_tex_src_ddy:
         assert(instr->src[i].src.ssa->bit_size == (g16 ? 16 : 32));
         ddy = get_ssa_temp_tex(ctx, instr->src[i].src.ssa, g16);
         has_ddy = true;
         break;
      case nir_tex_src_ms_index:
         assert(instr->src[i].src.ssa->bit_size == (a16 ? 16 : 32));
         sample_index = get_ssa_temp_tex(ctx, instr->src[i].src.ssa, a16);
         has_sample_index = true;
         break;
      case nir_tex_src_texture_offset:
      case nir_tex_src_sampler_offset:
      default: break;
      }
   }

   if (has_wqm_coord) {
      assert(instr->op == nir_texop_tex || instr->op == nir_texop_txb ||
             instr->op == nir_texop_lod);
      assert(wqm_coord.regClass().is_linear_vgpr());
      assert(!a16 && !g16);
   }

   if (instr->op == nir_texop_tg4 && !has_lod && !instr->is_gather_implicit_lod)
      level_zero = true;

   if (has_offset) {
      assert(instr->op != nir_texop_txf);

      aco_ptr<Instruction> tmp_instr;
      Temp acc, pack = Temp();

      uint32_t pack_const = 0;
      for (unsigned i = 0; i < offset.size(); i++) {
         if (!const_offset[i])
            continue;
         pack_const |= (const_offset[i]->u32 & 0x3Fu) << (8u * i);
      }

      if (offset.type() == RegType::sgpr) {
         for (unsigned i = 0; i < offset.size(); i++) {
            if (const_offset[i])
               continue;

            acc = emit_extract_vector(ctx, offset, i, s1);
            acc = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), acc,
                           Operand::c32(0x3Fu));

            if (i) {
               acc = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), acc,
                              Operand::c32(8u * i));
            }

            if (pack == Temp()) {
               pack = acc;
            } else {
               pack = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc), pack, acc);
            }
         }

         if (pack_const && pack != Temp())
            pack = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc),
                            Operand::c32(pack_const), pack);
      } else {
         for (unsigned i = 0; i < offset.size(); i++) {
            if (const_offset[i])
               continue;

            acc = emit_extract_vector(ctx, offset, i, v1);
            acc = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand::c32(0x3Fu), acc);

            if (i) {
               acc = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(8u * i), acc);
            }

            if (pack == Temp()) {
               pack = acc;
            } else {
               pack = bld.vop2(aco_opcode::v_or_b32, bld.def(v1), pack, acc);
            }
         }

         if (pack_const && pack != Temp())
            pack = bld.vop2(aco_opcode::v_or_b32, bld.def(v1), Operand::c32(pack_const), pack);
      }
      if (pack == Temp())
         offset = bld.copy(bld.def(v1), Operand::c32(pack_const));
      else
         offset = pack;
   }

   std::vector<Temp> unpacked_coord;
   if (coord != Temp())
      unpacked_coord.push_back(coord);
   if (has_sample_index)
      unpacked_coord.push_back(sample_index);
   if (has_lod)
      unpacked_coord.push_back(lod);
   if (has_clamped_lod)
      unpacked_coord.push_back(clamped_lod);

   coords = emit_pack_v1(ctx, unpacked_coord);

   /* pack derivatives */
   if (has_ddx || has_ddy) {
      assert(a16 == g16 || ctx->options->gfx_level >= GFX10);
      std::array<Temp, 2> ddxddy = {ddx, ddy};
      for (Temp tmp : ddxddy) {
         if (tmp == Temp())
            continue;
         std::vector<Temp> unpacked = {tmp};
         for (Temp derv : emit_pack_v1(ctx, unpacked))
            derivs.push_back(derv);
      }
      has_derivs = true;
   }

   assert(instr->sampler_dim != GLSL_SAMPLER_DIM_BUF);

   unsigned dim = ac_get_sampler_dim(ctx->options->gfx_level, instr->sampler_dim, instr->is_array);
   bool da = should_declare_array((ac_image_dim)dim);

   /* Build tex instruction */
   unsigned dmask = nir_def_components_read(&instr->def);
   /* Mask out the bit set for the sparse info. */
   if (instr->is_sparse)
      dmask &= ~(1u << (instr->def.num_components - 1));
   /* Set the 5th bit for the sparse code. */
   if (instr->is_sparse)
      dmask = MAX2(dmask, 1) | 0x10;

   bool d16 = instr->def.bit_size == 16;
   Temp dst = get_ssa_temp(ctx, &instr->def);
   Temp tmp_dst = dst;

   /* gather4 selects the component by dmask and always returns vec4 (vec5 if sparse) */
   if (instr->op == nir_texop_tg4) {
      assert(instr->def.num_components == (4 + instr->is_sparse));
      if (instr->is_shadow)
         dmask = 1;
      else
         dmask = 1 << instr->component;
      if (tg4_integer_cube_workaround || dst.type() == RegType::sgpr)
         tmp_dst = bld.tmp(instr->is_sparse ? v5 : (d16 ? v2 : v4));
   } else if (instr->op == nir_texop_fragment_mask_fetch_amd) {
      tmp_dst = bld.tmp(v1);
   } else if (util_bitcount(dmask) != instr->def.num_components || dst.type() == RegType::sgpr) {
      unsigned bytes = util_bitcount(dmask) * instr->def.bit_size / 8;
      tmp_dst = bld.tmp(RegClass::get(RegType::vgpr, bytes));
   }

   Temp tg4_compare_cube_wa64 = Temp();

   if (tg4_integer_workarounds) {
      Temp half_texel[2];
      if (instr->sampler_dim == GLSL_SAMPLER_DIM_RECT) {
         half_texel[0] = half_texel[1] = bld.copy(bld.def(v1), Operand::c32(0xbf000000 /*-0.5*/));
      } else {
         Temp tg4_lod = bld.copy(bld.def(v1), Operand::zero());
         Temp size = bld.tmp(v2);
         MIMG_instruction* tex = emit_mimg(bld, aco_opcode::image_get_resinfo, {size}, resource,
                                           Operand(s4), std::vector<Temp>{tg4_lod}, disable_wqm);
         tex->dim = dim;
         tex->dmask = 0x3;
         tex->da = da;
         emit_split_vector(ctx, size, size.size());

         for (unsigned i = 0; i < 2; i++) {
            half_texel[i] = emit_extract_vector(ctx, size, i, v1);
            half_texel[i] = bld.vop1(aco_opcode::v_cvt_f32_i32, bld.def(v1), half_texel[i]);
            half_texel[i] = bld.vop1(aco_opcode::v_rcp_iflag_f32, bld.def(v1), half_texel[i]);
            half_texel[i] = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1),
                                     Operand::c32(0xbf000000 /*-0.5*/), half_texel[i]);
         }

         if (instr->sampler_dim == GLSL_SAMPLER_DIM_2D && !instr->is_array) {
            /* In vulkan, whether the sampler uses unnormalized
             * coordinates or not is a dynamic property of the
             * sampler. Hence, to figure out whether or not we
             * need to divide by the texture size, we need to test
             * the sampler at runtime. This tests the bit set by
             * radv_init_sampler().
             */
            unsigned bit_idx = ffs(S_008F30_FORCE_UNNORMALIZED(1)) - 1;
            Temp dword0 = emit_extract_vector(ctx, sampler, 0, s1);
            Temp not_needed =
               bld.sopc(aco_opcode::s_bitcmp0_b32, bld.def(s1, scc), dword0, Operand::c32(bit_idx));

            not_needed = bool_to_vector_condition(ctx, not_needed);
            half_texel[0] = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1),
                                     Operand::c32(0xbf000000 /*-0.5*/), half_texel[0], not_needed);
            half_texel[1] = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1),
                                     Operand::c32(0xbf000000 /*-0.5*/), half_texel[1], not_needed);
         }
      }

      Temp new_coords[2] = {bld.vop2(aco_opcode::v_add_f32, bld.def(v1), coords[0], half_texel[0]),
                            bld.vop2(aco_opcode::v_add_f32, bld.def(v1), coords[1], half_texel[1])};

      if (tg4_integer_cube_workaround) {
         /* see comment in ac_nir_to_llvm.c's lower_gather4_integer() */
         Temp* const desc = (Temp*)alloca(resource.size() * sizeof(Temp));
         aco_ptr<Instruction> split{
            create_instruction(aco_opcode::p_split_vector, Format::PSEUDO, 1, resource.size())};
         split->operands[0] = Operand(resource);
         for (unsigned i = 0; i < resource.size(); i++) {
            desc[i] = bld.tmp(s1);
            split->definitions[i] = Definition(desc[i]);
         }
         ctx->block->instructions.emplace_back(std::move(split));

         Temp dfmt = bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc), desc[1],
                              Operand::c32(20u | (6u << 16)));
         Temp compare_cube_wa = bld.sopc(aco_opcode::s_cmp_eq_u32, bld.def(s1, scc), dfmt,
                                         Operand::c32(V_008F14_IMG_DATA_FORMAT_8_8_8_8));

         Temp nfmt;
         if (instr->dest_type & nir_type_uint) {
            nfmt = bld.sop2(aco_opcode::s_cselect_b32, bld.def(s1),
                            Operand::c32(V_008F14_IMG_NUM_FORMAT_USCALED),
                            Operand::c32(V_008F14_IMG_NUM_FORMAT_UINT), bld.scc(compare_cube_wa));
         } else {
            nfmt = bld.sop2(aco_opcode::s_cselect_b32, bld.def(s1),
                            Operand::c32(V_008F14_IMG_NUM_FORMAT_SSCALED),
                            Operand::c32(V_008F14_IMG_NUM_FORMAT_SINT), bld.scc(compare_cube_wa));
         }
         tg4_compare_cube_wa64 = bld.tmp(bld.lm);
         bool_to_vector_condition(ctx, compare_cube_wa, tg4_compare_cube_wa64);

         nfmt = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), nfmt,
                         Operand::c32(26u));

         desc[1] = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), desc[1],
                            Operand::c32(C_008F14_NUM_FORMAT));
         desc[1] = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc), desc[1], nfmt);

         aco_ptr<Instruction> vec{
            create_instruction(aco_opcode::p_create_vector, Format::PSEUDO, resource.size(), 1)};
         for (unsigned i = 0; i < resource.size(); i++)
            vec->operands[i] = Operand(desc[i]);
         resource = bld.tmp(resource.regClass());
         vec->definitions[0] = Definition(resource);
         ctx->block->instructions.emplace_back(std::move(vec));

         new_coords[0] = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), new_coords[0], coords[0],
                                  tg4_compare_cube_wa64);
         new_coords[1] = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), new_coords[1], coords[1],
                                  tg4_compare_cube_wa64);
      }
      coords[0] = new_coords[0];
      coords[1] = new_coords[1];
   }

   /* gather MIMG address components */
   std::vector<Temp> args;
   if (has_wqm_coord) {
      args.emplace_back(wqm_coord);
      if (!(ctx->block->kind & block_kind_top_level))
         ctx->unended_linear_vgprs.push_back(wqm_coord);
   }
   if (has_offset)
      args.emplace_back(offset);
   if (has_bias)
      args.emplace_back(emit_pack_v1(ctx, {bias})[0]);
   if (has_compare)
      args.emplace_back(compare);
   if (has_derivs)
      args.insert(args.end(), derivs.begin(), derivs.end());

   args.insert(args.end(), coords.begin(), coords.end());

   if (instr->op == nir_texop_txf || instr->op == nir_texop_fragment_fetch_amd ||
       instr->op == nir_texop_fragment_mask_fetch_amd || instr->op == nir_texop_txf_ms) {
      aco_opcode op = level_zero || instr->sampler_dim == GLSL_SAMPLER_DIM_MS ||
                            instr->sampler_dim == GLSL_SAMPLER_DIM_SUBPASS_MS
                         ? aco_opcode::image_load
                         : aco_opcode::image_load_mip;
      Operand vdata = instr->is_sparse ? emit_tfe_init(bld, tmp_dst) : Operand(v1);
      MIMG_instruction* tex =
         emit_mimg(bld, op, {tmp_dst}, resource, Operand(s4), args, disable_wqm, vdata);
      if (instr->op == nir_texop_fragment_mask_fetch_amd)
         tex->dim = da ? ac_image_2darray : ac_image_2d;
      else
         tex->dim = dim;
      tex->dmask = dmask & 0xf;
      tex->unrm = true;
      tex->da = da;
      tex->tfe = instr->is_sparse;
      tex->d16 = d16;
      tex->a16 = a16;

      if (instr->op == nir_texop_fragment_mask_fetch_amd) {
         /* Use 0x76543210 if the image doesn't have FMASK. */
         assert(dmask == 1 && dst.bytes() == 4);
         assert(dst.id() != tmp_dst.id());

         if (dst.regClass() == s1) {
            Temp is_not_null = bld.sopc(aco_opcode::s_cmp_lg_u32, bld.def(s1, scc), Operand::zero(),
                                        emit_extract_vector(ctx, resource, 1, s1));
            bld.sop2(aco_opcode::s_cselect_b32, Definition(dst), bld.as_uniform(tmp_dst),
                     Operand::c32(0x76543210), bld.scc(is_not_null));
         } else {
            Temp is_not_null = bld.tmp(bld.lm);
            bld.vopc_e64(aco_opcode::v_cmp_lg_u32, Definition(is_not_null), Operand::zero(),
                         emit_extract_vector(ctx, resource, 1, s1));
            bld.vop2(aco_opcode::v_cndmask_b32, Definition(dst),
                     bld.copy(bld.def(v1), Operand::c32(0x76543210)), tmp_dst, is_not_null);
         }
      } else {
         expand_vector(ctx, tmp_dst, dst, instr->def.num_components, dmask);
      }
      return;
   }

   bool separate_g16 = ctx->options->gfx_level >= GFX10 && g16;

   // TODO: would be better to do this by adding offsets, but needs the opcodes ordered.
   aco_opcode opcode = aco_opcode::image_sample;
   if (has_offset) { /* image_sample_*_o */
      if (has_clamped_lod) {
         if (has_compare) {
            opcode = aco_opcode::image_sample_c_cl_o;
            if (separate_g16)
               opcode = aco_opcode::image_sample_c_d_cl_o_g16;
            else if (has_derivs)
               opcode = aco_opcode::image_sample_c_d_cl_o;
            if (has_bias)
               opcode = aco_opcode::image_sample_c_b_cl_o;
         } else {
            opcode = aco_opcode::image_sample_cl_o;
            if (separate_g16)
               opcode = aco_opcode::image_sample_d_cl_o_g16;
            else if (has_derivs)
               opcode = aco_opcode::image_sample_d_cl_o;
            if (has_bias)
               opcode = aco_opcode::image_sample_b_cl_o;
         }
      } else if (has_compare) {
         opcode = aco_opcode::image_sample_c_o;
         if (separate_g16)
            opcode = aco_opcode::image_sample_c_d_o_g16;
         else if (has_derivs)
            opcode = aco_opcode::image_sample_c_d_o;
         if (has_bias)
            opcode = aco_opcode::image_sample_c_b_o;
         if (level_zero)
            opcode = aco_opcode::image_sample_c_lz_o;
         if (has_lod)
            opcode = aco_opcode::image_sample_c_l_o;
      } else {
         opcode = aco_opcode::image_sample_o;
         if (separate_g16)
            opcode = aco_opcode::image_sample_d_o_g16;
         else if (has_derivs)
            opcode = aco_opcode::image_sample_d_o;
         if (has_bias)
            opcode = aco_opcode::image_sample_b_o;
         if (level_zero)
            opcode = aco_opcode::image_sample_lz_o;
         if (has_lod)
            opcode = aco_opcode::image_sample_l_o;
      }
   } else if (has_clamped_lod) { /* image_sample_*_cl */
      if (has_compare) {
         opcode = aco_opcode::image_sample_c_cl;
         if (separate_g16)
            opcode = aco_opcode::image_sample_c_d_cl_g16;
         else if (has_derivs)
            opcode = aco_opcode::image_sample_c_d_cl;
         if (has_bias)
            opcode = aco_opcode::image_sample_c_b_cl;
      } else {
         opcode = aco_opcode::image_sample_cl;
         if (separate_g16)
            opcode = aco_opcode::image_sample_d_cl_g16;
         else if (has_derivs)
            opcode = aco_opcode::image_sample_d_cl;
         if (has_bias)
            opcode = aco_opcode::image_sample_b_cl;
      }
   } else { /* no offset */
      if (has_compare) {
         opcode = aco_opcode::image_sample_c;
         if (separate_g16)
            opcode = aco_opcode::image_sample_c_d_g16;
         else if (has_derivs)
            opcode = aco_opcode::image_sample_c_d;
         if (has_bias)
            opcode = aco_opcode::image_sample_c_b;
         if (level_zero)
            opcode = aco_opcode::image_sample_c_lz;
         if (has_lod)
            opcode = aco_opcode::image_sample_c_l;
      } else {
         opcode = aco_opcode::image_sample;
         if (separate_g16)
            opcode = aco_opcode::image_sample_d_g16;
         else if (has_derivs)
            opcode = aco_opcode::image_sample_d;
         if (has_bias)
            opcode = aco_opcode::image_sample_b;
         if (level_zero)
            opcode = aco_opcode::image_sample_lz;
         if (has_lod)
            opcode = aco_opcode::image_sample_l;
      }
   }

   if (instr->op == nir_texop_tg4) {
      /* GFX11 supports implicit LOD, but the extension is unsupported. */
      assert(level_zero || ctx->options->gfx_level < GFX11);

      if (has_offset) { /* image_gather4_*_o */
         if (has_compare) {
            opcode = aco_opcode::image_gather4_c_o;
            if (level_zero)
               opcode = aco_opcode::image_gather4_c_lz_o;
            if (has_lod)
               opcode = aco_opcode::image_gather4_c_l_o;
            if (has_bias)
               opcode = aco_opcode::image_gather4_c_b_o;
         } else {
            opcode = aco_opcode::image_gather4_o;
            if (level_zero)
               opcode = aco_opcode::image_gather4_lz_o;
            if (has_lod)
               opcode = aco_opcode::image_gather4_l_o;
            if (has_bias)
               opcode = aco_opcode::image_gather4_b_o;
         }
      } else {
         if (has_compare) {
            opcode = aco_opcode::image_gather4_c;
            if (level_zero)
               opcode = aco_opcode::image_gather4_c_lz;
            if (has_lod)
               opcode = aco_opcode::image_gather4_c_l;
            if (has_bias)
               opcode = aco_opcode::image_gather4_c_b;
         } else {
            opcode = aco_opcode::image_gather4;
            if (level_zero)
               opcode = aco_opcode::image_gather4_lz;
            if (has_lod)
               opcode = aco_opcode::image_gather4_l;
            if (has_bias)
               opcode = aco_opcode::image_gather4_b;
         }
      }
   } else if (instr->op == nir_texop_lod) {
      opcode = aco_opcode::image_get_lod;
   }

   bool implicit_derivs = bld.program->stage == fragment_fs && !has_derivs && !has_lod &&
                          !level_zero && instr->sampler_dim != GLSL_SAMPLER_DIM_MS &&
                          instr->sampler_dim != GLSL_SAMPLER_DIM_SUBPASS_MS;

   Operand vdata = instr->is_sparse ? emit_tfe_init(bld, tmp_dst) : Operand(v1);
   MIMG_instruction* tex =
      emit_mimg(bld, opcode, {tmp_dst}, resource, Operand(sampler), args, disable_wqm, vdata);
   tex->dim = dim;
   tex->dmask = dmask & 0xf;
   tex->da = da;
   tex->unrm = instr->sampler_dim == GLSL_SAMPLER_DIM_RECT;
   tex->tfe = instr->is_sparse;
   tex->d16 = d16;
   tex->a16 = a16;
   if (implicit_derivs && !has_wqm_coord)
      set_wqm(ctx, true);

   if (tg4_integer_cube_workaround) {
      assert(tmp_dst.id() != dst.id());
      assert(tmp_dst.size() == dst.size());

      emit_split_vector(ctx, tmp_dst, tmp_dst.size());
      Temp val[4];
      for (unsigned i = 0; i < 4; i++) {
         val[i] = emit_extract_vector(ctx, tmp_dst, i, v1);
         Temp cvt_val;
         if (instr->dest_type & nir_type_uint)
            cvt_val = bld.vop1(aco_opcode::v_cvt_u32_f32, bld.def(v1), val[i]);
         else
            cvt_val = bld.vop1(aco_opcode::v_cvt_i32_f32, bld.def(v1), val[i]);
         val[i] = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), val[i], cvt_val,
                           tg4_compare_cube_wa64);
      }

      Temp tmp = dst.regClass() == tmp_dst.regClass() ? dst : bld.tmp(tmp_dst.regClass());
      if (instr->is_sparse)
         tmp_dst = bld.pseudo(aco_opcode::p_create_vector, Definition(tmp), val[0], val[1], val[2],
                              val[3], emit_extract_vector(ctx, tmp_dst, 4, v1));
      else
         tmp_dst = bld.pseudo(aco_opcode::p_create_vector, Definition(tmp), val[0], val[1], val[2],
                              val[3]);
   }
   unsigned mask = instr->op == nir_texop_tg4 ? (instr->is_sparse ? 0x1F : 0xF) : dmask;

   /* Move the bit for the sparse residency code from the 5th bit to the last component. */
   if (mask & 0x10) {
      mask &= ~0x10;
      mask |= 1u << (instr->def.num_components - 1);
   }

   expand_vector(ctx, tmp_dst, dst, instr->def.num_components, mask);
}

Operand
get_phi_operand(isel_context* ctx, nir_def* ssa, RegClass rc)
{
   Temp tmp = get_ssa_temp(ctx, ssa);
   if (nir_def_is_undef(ssa)) {
      return Operand(rc);
   } else if (ssa->bit_size == 1 && nir_def_is_const(ssa)) {
      bool val = nir_def_as_load_const(ssa)->value[0].b;
      return Operand::c32_or_c64(val ? -1 : 0, ctx->program->lane_mask == s2);
   } else {
      return Operand(tmp);
   }
}

void
visit_phi(isel_context* ctx, nir_phi_instr* instr)
{
   Temp dst = get_ssa_temp(ctx, &instr->def);
   assert(instr->def.bit_size != 1 || dst.regClass() == ctx->program->lane_mask);
   aco_opcode opcode = instr->def.bit_size == 1 ? aco_opcode::p_boolean_phi : aco_opcode::p_phi;

   /* we want a sorted list of sources, since the predecessor list is also sorted */
   std::map<unsigned, nir_def*> phi_src;
   nir_foreach_phi_src (src, instr)
      phi_src[src->pred->index] = src->src.ssa;

   Instruction* phi = create_instruction(opcode, Format::PSEUDO, phi_src.size(), 1);
   unsigned i = 0;
   for (std::pair<unsigned, nir_def*> src : phi_src)
      phi->operands[i++] = get_phi_operand(ctx, src.second, dst.regClass());
   phi->definitions[0] = Definition(dst);
   ctx->block->instructions.emplace(ctx->block->instructions.begin(), std::move(phi));
}

void
visit_undef(isel_context* ctx, nir_undef_instr* instr)
{
   Temp dst = get_ssa_temp(ctx, &instr->def);

   assert(dst.type() == RegType::sgpr);

   if (dst.size() == 1) {
      Builder(ctx->program, ctx->block).copy(Definition(dst), Operand::zero());
   } else {
      aco_ptr<Instruction> vec{
         create_instruction(aco_opcode::p_create_vector, Format::PSEUDO, dst.size(), 1)};
      for (unsigned i = 0; i < dst.size(); i++)
         vec->operands[i] = Operand::zero();
      vec->definitions[0] = Definition(dst);
      ctx->block->instructions.emplace_back(std::move(vec));
   }
}

void
visit_jump(isel_context* ctx, nir_jump_instr* instr)
{
   end_empty_exec_skip(ctx);

   if (instr->type != nir_jump_break) {
      isel_err(&instr->instr, "Unknown NIR jump instr");
      abort();
   }

   emit_loop_break(ctx);
}

void
visit_call(isel_context* ctx, nir_call_instr* instr)
{
   assert(!ctx->program->preserve_s2);

   Builder bld(ctx->program, ctx->block);

   unsigned nir_abi = instr->callee->driver_attributes & ACO_NIR_FUNCTION_ATTRIB_ABI_MASK;
   param_assignment_hints hints;

   if (nir_abi == ACO_NIR_CALL_ABI_AHIT_ISEC)
      hints = get_ahit_isec_param_hints(ctx->callee_info, ctx->program->info.descriptor_heap);

   ABI abi = nir_abi_to_aco(instr->callee->driver_attributes);

   RegisterDemand limit = get_addr_regs_from_waves(ctx->program, ctx->program->min_waves);

   struct callee_info info =
      get_callee_info(ctx->program->gfx_level, ctx->program->wave_size, abi,
                      instr->callee->num_params, instr->callee->params, nullptr, limit, hints);
   std::vector<parameter_info> return_infos;

   /* Before setting up the call itself, set up parameters stored in scratch memory.
    * The stack layout during a call looks something like this:
    * -------------------------------------------------------------------
    * | caller stack area | callee's scratch params | callee stack area
    * -------------------------------------------------------------------
    * ^ caller's stack ptr                          ^ callee's stack ptr
    *
    * Since we don't know how big our own stack area is yet (spilling and register preservation may
    * add to the stack size), we query the callee's stack pointer using p_callee_stack_ptr and use
    * negative offsets to index into the scratch parameter area (similar to how the callee will load
    * the parameters as well).
    */

   Temp stack_ptr, param_stack_ptr;
   if (ctx->program->is_callee && ctx->program->gfx_level >= GFX9) {
      param_stack_ptr = bld.pseudo(aco_opcode::p_callee_stack_ptr, bld.def(s1), bld.def(s1, scc),
                                   Operand::c32(info.scratch_param_size),
                                   Operand(ctx->callee_info.stack_ptr.def.getTemp()));
      stack_ptr = ctx->callee_info.stack_ptr.def.getTemp();
   } else {
      param_stack_ptr = bld.pseudo(aco_opcode::p_callee_stack_ptr, bld.def(s1),
                                   Operand::c32(info.scratch_param_size));
      stack_ptr = bld.pseudo(aco_opcode::p_parallelcopy, bld.def(s1), Operand::c32(0));
   }

   for (unsigned i = 0; i < info.param_infos.size(); ++i) {
      if (info.param_infos[i].is_reg)
         continue;

      store_scratch_param(ctx, bld, info.param_infos[i], param_stack_ptr, info.scratch_param_size,
                          get_ssa_temp(ctx, instr->params[i].ssa));
   }

   unsigned extra_def_count = 1;
   unsigned extra_param_count = 2;

   unsigned param_size = info.scratch_param_size;
   if (ctx->program->gfx_level < GFX9)
      param_size *= ctx->program->wave_size;

   assert(info.param_infos[0].is_reg);
   Instruction* call_instr = create_instruction(aco_opcode::p_call, Format::PSEUDO_CALL,
                                                info.reg_param_count + extra_param_count,
                                                info.reg_discardable_param_count + extra_def_count);
   call_instr->call().abi = abi;
   if (ctx->program->gfx_level >= GFX9) {
      call_instr->operands[0] = Operand(stack_ptr, info.stack_ptr.def.physReg());
   } else {
      call_instr->operands[0] = Operand(load_scratch_resource(ctx->program, bld, ctx->program->private_segment_buffers.size() - 1, true));
      call_instr->operands[0].setPrecolored(info.stack_ptr.def.physReg());
   }

   call_instr->operands[1] = Operand::c32(param_size);
   call_instr->definitions[0] = Definition(bld.tmp(s2), info.return_address.def.physReg());

   /* Set up parameters stored in registers. Every parameter corresponds to an operand,
    * and parameters that may have their value clobbered (i.e. discardable and return params)
    * also have a definition.
    */
   unsigned reg_param_idx = 0;
   unsigned reg_discardable_param_idx = 0;
   for (unsigned i = 0; i < info.param_infos.size(); ++i) {
      if (!info.param_infos[i].is_reg) {
         /* While setting up parameters, also capture information about where return parameters
          * are stored, in order to reload them later.
          * Since return_infos stores return parameters contiguously, and return parameters in
          * scratch may be at any position in the parameter list, we need to add information about
          * returned scratch parameters in the same loop as returned parameters stored in registers.
          */
         if (instr->callee->params[i].is_return) {
            parameter_info return_info = {};
            return_info.is_reg = false;
            return_info.scratch_offset = info.param_infos[i].scratch_offset;
            return_infos.emplace_back(return_info);
         }
         continue;
      }

      Operand& op = call_instr->operands[reg_param_idx + extra_param_count];
      op.setPrecolored(info.param_infos[i].def.physReg());

      if (instr->callee->params[i].is_uniform || instr->callee->params[i].bit_size == 1)
         op.setTemp(bld.as_uniform(get_ssa_temp(ctx, instr->params[i].ssa)));
      else
         op.setTemp(as_vgpr(ctx, get_ssa_temp(ctx, instr->params[i].ssa)));

      if ((instr->callee->params[i].driver_attributes & ACO_NIR_PARAM_ATTRIB_DISCARDABLE) ||
          instr->callee->params[i].is_return) {
         Definition def = bld.def(op.regClass(), op.physReg());
         call_instr->definitions[extra_def_count + reg_discardable_param_idx++] = def;
         if (instr->callee->params[i].is_return) {
            assert(!instr->callee->params[i].is_uniform);
            parameter_info return_info = {};
            return_info.is_reg = true;
            return_info.def = def;
            return_infos.emplace_back(return_info);
         }
      }

      ++reg_param_idx;
   }

   ctx->block->instructions.emplace_back(static_cast<Instruction*>(call_instr));

   ctx->call_infos.emplace_back(call_info{
      instr,
      call_instr,
      std::move(return_infos),
      info.scratch_param_size,
   });
   ctx->block->kind |= block_kind_contains_call;
   ctx->program->has_call = true;
}

void
visit_debug_info(isel_context* ctx, nir_instr_debug_info* instr_info)
{
   ac_shader_debug_info info;
   memset(&info, 0, sizeof(info));

   info.type = ac_shader_debug_info_src_loc;
   if (instr_info->filename)
      info.src_loc.file = strdup(instr_info->filename);
   info.src_loc.line = instr_info->line;
   info.src_loc.column = instr_info->column;
   info.src_loc.spirv_offset = instr_info->spirv_offset;

   Builder bld(ctx->program, ctx->block);
   bld.pseudo(aco_opcode::p_debug_info, Operand::c32(ctx->program->debug_info.size()));

   ctx->program->debug_info.push_back(info);
}

void
push_cf_list(isel_context* ctx, struct exec_list* l)
{
   if (nir_cf_list_is_empty_block(l))
      return;

   nir_cf_node* node = exec_node_data_head(nir_cf_node, l, node);

   cf_traversal_state state;
   state.phase = CF_TRAVERSAL_PHASE_ENTER;
   state.node = node;
   state.saved_skipping_empty_exec = ctx->skipping_empty_exec;
   ctx->skipping_empty_exec = false;

   ctx->traversal_stack.push_back(std::move(state));
}

void
pop_cf_list(isel_context* ctx)
{
   end_empty_exec_skip(ctx);
   cf_traversal_state& head = ctx->traversal_stack.back();
   ctx->skipping_empty_exec = head.saved_skipping_empty_exec;
   ctx->traversal_stack.pop_back();
}

void
advance_cf_list(isel_context* ctx, nir_cf_node* node)
{
   if (!nir_cf_node_is_last(node)) {
      cf_traversal_state& head = ctx->traversal_stack.back();
      head.node = nir_cf_node_next(node);
      head.phase = CF_TRAVERSAL_PHASE_ENTER;
   } else {
      pop_cf_list(ctx);
   }
}

void
visit_block(isel_context* ctx, nir_block* block)
{
   if (ctx->block->kind & block_kind_top_level) {
      Builder bld(ctx->program, ctx->block);
      for (Temp tmp : ctx->unended_linear_vgprs) {
         bld.pseudo(aco_opcode::p_end_linear_vgpr, tmp);
      }
      ctx->unended_linear_vgprs.clear();
   }

   nir_foreach_phi (instr, block)
      visit_phi(ctx, instr);

   nir_phi_instr* last_phi = nir_block_last_phi_instr(block);
   begin_empty_exec_skip(ctx, last_phi ? &last_phi->instr : NULL, block);

   ctx->block->instructions.reserve(ctx->block->instructions.size() +
                                    exec_list_length(&block->instr_list) * 2);
   nir_foreach_instr (instr, block) {
      if (ctx->shader->has_debug_info)
         visit_debug_info(ctx, nir_instr_get_debug_info(instr));

      switch (instr->type) {
      case nir_instr_type_alu: visit_alu_instr(ctx, nir_instr_as_alu(instr)); break;
      case nir_instr_type_load_const: visit_load_const(ctx, nir_instr_as_load_const(instr)); break;
      case nir_instr_type_intrinsic: visit_intrinsic(ctx, nir_instr_as_intrinsic(instr)); break;
      case nir_instr_type_tex: visit_tex(ctx, nir_instr_as_tex(instr)); break;
      case nir_instr_type_phi: break;
      case nir_instr_type_undef: visit_undef(ctx, nir_instr_as_undef(instr)); break;
      case nir_instr_type_deref: break;
      case nir_instr_type_jump: visit_jump(ctx, nir_instr_as_jump(instr)); break;
      case nir_instr_type_call: visit_call(ctx, nir_instr_as_call(instr)); break;
      default: isel_err(instr, "Unknown NIR instr type");
      }
   }

   advance_cf_list(ctx, (nir_cf_node*)block);
}

void
visit_loop(isel_context* ctx, nir_loop* loop, cf_traversal_phase phase)
{
   if (phase == CF_TRAVERSAL_PHASE_ENTER) {
      assert(!nir_loop_has_continue_construct(loop));

      begin_loop(ctx);

      ctx->cf_info.parent_loop.has_divergent_break =
         loop->divergent_break && nir_block_num_preds(nir_loop_first_block(loop)) > 1;
      ctx->cf_info.in_divergent_cf |= ctx->cf_info.parent_loop.has_divergent_break;

      ctx->traversal_stack.back().phase = CF_TRAVERSAL_PHASE_LEAVE;
      push_cf_list(ctx, &loop->body);
      return; /* Return to driver to process body */
   } else if (phase == CF_TRAVERSAL_PHASE_LEAVE) {
      end_loop(ctx);

      advance_cf_list(ctx, (nir_cf_node*)loop);
   }
}

/**
 * Uniform conditionals are represented in the following way*) :
 *
 * The linear and logical CFG:
 *                        BB_IF
 *                        /    \
 *       BB_THEN (logical)      BB_ELSE (logical)
 *                        \    /
 *                        BB_ENDIF
 *
 * *) Exceptions may be due to break and continue statements within loops
 *    If a break/continue happens within uniform control flow, it branches
 *    to the loop exit/entry block. Otherwise, it branches to the next
 *    merge block.
 *
 * To maintain a logical and linear CFG without critical edges,
 * non-uniform conditionals are represented in the following way*) :
 *
 * The linear CFG:
 *                        BB_IF
 *                        /    \
 *       BB_THEN (logical)      BB_THEN (linear)
 *                        \    /
 *                        BB_INVERT (linear)
 *                        /    \
 *       BB_ELSE (logical)      BB_ELSE (linear)
 *                        \    /
 *                        BB_ENDIF
 *
 * The logical CFG:
 *                        BB_IF
 *                        /    \
 *       BB_THEN (logical)      BB_ELSE (logical)
 *                        \    /
 *                        BB_ENDIF
 *
 *
 * Exceptions may be due to break and continue statements within loops:
 *
 * The linear CFG:
 *                        BB_IF
 *                        /    \
 *       BB_THEN (logical)      \
 *           /    \              \
 *    BB_JUMP    BB_CONTINUE    BB_ELSE     (all linear)
 *                        \      /
 *                        BB_ENDIF
 **/
void
visit_if(isel_context* ctx, nir_if* if_stmt, cf_traversal_phase phase)
{
   if (phase == CF_TRAVERSAL_PHASE_ENTER) {
      Temp cond = get_ssa_temp(ctx, if_stmt->condition.ssa);

      if (!nir_src_is_divergent(&if_stmt->condition)) {
         assert(cond.regClass() == ctx->program->lane_mask);
         cond = bool_to_scalar_condition(ctx, cond);
         begin_uniform_if_then(ctx, cond);
      } else {
         begin_divergent_if_then(ctx, cond, if_stmt->control);
      }
      ctx->traversal_stack.back().phase = CF_TRAVERSAL_PHASE_IN_ELSE;
      push_cf_list(ctx, &if_stmt->then_list);
      return; /* Return to driver to process then_list */
   }

   if (phase == CF_TRAVERSAL_PHASE_IN_ELSE) {
      if (!nir_src_is_divergent(&if_stmt->condition)) {
         begin_uniform_if_else(ctx);
      } else {
         begin_divergent_if_else(ctx, if_stmt->control);
      }

      ctx->traversal_stack.back().phase = CF_TRAVERSAL_PHASE_LEAVE;
      push_cf_list(ctx, &if_stmt->else_list);
      return; /* Return to driver to process else_list */
   }

   if (phase == CF_TRAVERSAL_PHASE_LEAVE) {
      if (!nir_src_is_divergent(&if_stmt->condition)) {
         end_uniform_if(ctx);
      } else {
         end_divergent_if(ctx);
      }

      advance_cf_list(ctx, (nir_cf_node*)if_stmt);
   }
}

void
visit_function_impl(isel_context* ctx, struct nir_function_impl* impl)
{
   push_cf_list(ctx, &impl->body);

   while (!ctx->traversal_stack.empty()) {
      nir_cf_node* node = ctx->traversal_stack.back().node;
      cf_traversal_phase phase = ctx->traversal_stack.back().phase;

      switch (node->type) {
      case nir_cf_node_block: visit_block(ctx, nir_cf_node_as_block(node)); break;
      case nir_cf_node_if: visit_if(ctx, nir_cf_node_as_if(node), phase); break;
      case nir_cf_node_loop: visit_loop(ctx, nir_cf_node_as_loop(node), phase); break;
      default: UNREACHABLE("unimplemented cf list type");
      }
   }
}

void
create_fs_jump_to_epilog(isel_context* ctx)
{
   Builder bld(ctx->program, ctx->block);
   std::vector<Operand> exports;
   unsigned vgpr = 256; /* VGPR 0 */

   if (ctx->outputs.mask[FRAG_RESULT_DEPTH])
      exports.emplace_back(Operand(ctx->outputs.temps[FRAG_RESULT_DEPTH * 4u], PhysReg{vgpr++}));

   if (ctx->outputs.mask[FRAG_RESULT_STENCIL])
      exports.emplace_back(Operand(ctx->outputs.temps[FRAG_RESULT_STENCIL * 4u], PhysReg{vgpr++}));

   if (ctx->outputs.mask[FRAG_RESULT_SAMPLE_MASK])
      exports.emplace_back(
         Operand(ctx->outputs.temps[FRAG_RESULT_SAMPLE_MASK * 4u], PhysReg{vgpr++}));

   PhysReg exports_start(vgpr);

   for (unsigned slot = FRAG_RESULT_DATA0; slot < FRAG_RESULT_DATA7 + 1; ++slot) {
      unsigned color_index = slot - FRAG_RESULT_DATA0;
      unsigned color_type = (ctx->output_color_types >> (color_index * 2)) & 0x3;
      unsigned write_mask = ctx->outputs.mask[slot];

      if (!write_mask)
         continue;

      PhysReg color_start(exports_start.reg() + color_index * 4);

      for (unsigned i = 0; i < 4; i++) {
         if (!(write_mask & BITFIELD_BIT(i))) {
            exports.emplace_back(Operand(v1));
            continue;
         }

         PhysReg chan_reg = color_start.advance(i * 4u);
         Operand chan(ctx->outputs.temps[slot * 4u + i]);

         if (color_type == ACO_TYPE_FLOAT16) {
            chan = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), chan);
         } else if (color_type == ACO_TYPE_INT16 || color_type == ACO_TYPE_UINT16) {
            bool sign_ext = color_type == ACO_TYPE_INT16;
            Temp tmp = convert_int(ctx, bld, chan.getTemp(), 16, 32, sign_ext);
            chan = Operand(tmp);
         }

         chan.setPrecolored(chan_reg);
         exports.emplace_back(chan);
      }
   }

   Temp continue_pc = convert_pointer_to_64_bit(ctx, get_arg(ctx, ctx->program->info.epilog_pc));

   aco_ptr<Instruction> jump{
      create_instruction(aco_opcode::p_jump_to_epilog, Format::PSEUDO, 1 + exports.size(), 0)};
   jump->operands[0] = Operand(continue_pc);
   for (unsigned i = 0; i < exports.size(); i++) {
      jump->operands[i + 1] = exports[i];
   }
   ctx->block->instructions.emplace_back(std::move(jump));
}

Operand
get_arg_for_end(isel_context* ctx, struct ac_arg arg)
{
   return Operand(get_arg(ctx, arg), get_arg_reg(ctx->args, arg));
}

void
create_fs_end_for_epilog(isel_context* ctx)
{
   Builder bld(ctx->program, ctx->block);

   std::vector<Operand> regs;

   regs.emplace_back(get_arg_for_end(ctx, ctx->program->info.ps.alpha_reference));

   unsigned vgpr = 256;

   for (unsigned slot = FRAG_RESULT_DATA0; slot <= FRAG_RESULT_DATA7; slot++) {
      unsigned index = slot - FRAG_RESULT_DATA0;
      unsigned type = (ctx->output_color_types >> (index * 2)) & 0x3;
      unsigned write_mask = ctx->outputs.mask[slot];

      if (!write_mask)
         continue;

      if (type == ACO_TYPE_ANY32) {
         u_foreach_bit (i, write_mask) {
            regs.emplace_back(Operand(ctx->outputs.temps[slot * 4 + i], PhysReg{vgpr + i}));
         }
      } else {
         for (unsigned i = 0; i < 2; i++) {
            unsigned mask = (write_mask >> (i * 2)) & 0x3;
            if (!mask)
               continue;

            unsigned chan = slot * 4 + i * 2;
            Operand lo = mask & 0x1 ? Operand(ctx->outputs.temps[chan]) : Operand(v2b);
            Operand hi = mask & 0x2 ? Operand(ctx->outputs.temps[chan + 1]) : Operand(v2b);

            Temp dst = bld.pseudo(aco_opcode::p_create_vector, bld.def(v1), lo, hi);
            regs.emplace_back(Operand(dst, PhysReg{vgpr + i}));
         }
      }
      vgpr += 4;
   }

   if (ctx->outputs.mask[FRAG_RESULT_DEPTH])
      regs.emplace_back(Operand(ctx->outputs.temps[FRAG_RESULT_DEPTH * 4], PhysReg{vgpr++}));

   if (ctx->outputs.mask[FRAG_RESULT_STENCIL])
      regs.emplace_back(Operand(ctx->outputs.temps[FRAG_RESULT_STENCIL * 4], PhysReg{vgpr++}));

   if (ctx->outputs.mask[FRAG_RESULT_SAMPLE_MASK])
      regs.emplace_back(Operand(ctx->outputs.temps[FRAG_RESULT_SAMPLE_MASK * 4], PhysReg{vgpr++}));

   build_end_with_regs(ctx, regs);

   /* Exit WQM mode finally. */
   ctx->program->needs_exact = true;
}

void
split_arguments(isel_context* ctx, Instruction* startpgm)
{
   /* Split all arguments except for the first (ring_offsets) and the last
    * (exec) so that the dead channels don't stay live throughout the program.
    */
   for (int i = 1; i < startpgm->definitions.size(); i++) {
      if (startpgm->definitions[i].regClass().size() > 1) {
         emit_split_vector(ctx, startpgm->definitions[i].getTemp(),
                           startpgm->definitions[i].regClass().size());
      }
   }
}

void
setup_fp_mode(isel_context* ctx, nir_shader* shader)
{
   Program* program = ctx->program;

   unsigned float_controls = shader->info.float_controls_execution_mode;

   program->next_fp_mode.must_flush_denorms32 =
      float_controls & FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP32;
   program->next_fp_mode.must_flush_denorms16_64 =
      float_controls &
      (FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP16 | FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP64);

   program->next_fp_mode.care_about_round32 =
      float_controls &
      (FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP32 | FLOAT_CONTROLS_ROUNDING_MODE_RTE_FP32);

   program->next_fp_mode.care_about_round16_64 =
      float_controls &
      (FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP16 | FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP64 |
       FLOAT_CONTROLS_ROUNDING_MODE_RTE_FP16 | FLOAT_CONTROLS_ROUNDING_MODE_RTE_FP64);

   /* default to preserving fp16 and fp64 denorms, since it's free for fp64 and
    * the precision seems needed for Wolfenstein: Youngblood to render correctly */
   if (program->next_fp_mode.must_flush_denorms16_64)
      program->next_fp_mode.denorm16_64 = 0;
   else
      program->next_fp_mode.denorm16_64 = fp_denorm_keep;

   /* preserving fp32 denorms is expensive, so only do it if asked */
   if (float_controls & FLOAT_CONTROLS_DENORM_PRESERVE_FP32)
      program->next_fp_mode.denorm32 = fp_denorm_keep;
   else
      program->next_fp_mode.denorm32 = 0;

   if (float_controls & FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP32)
      program->next_fp_mode.round32 = fp_round_tz;
   else
      program->next_fp_mode.round32 = fp_round_ne;

   if (float_controls &
       (FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP16 | FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP64))
      program->next_fp_mode.round16_64 = fp_round_tz;
   else
      program->next_fp_mode.round16_64 = fp_round_ne;

   ctx->block->fp_mode = program->next_fp_mode;
}

Temp
merged_wave_info_to_mask(isel_context* ctx, unsigned i)
{
   /* lanecount_to_mask() only cares about s0.byte[i].[6:0]
    * so we don't need either s_bfe nor s_and here.
    */
   Temp count = get_arg(ctx, ctx->args->merged_wave_info);

   return lanecount_to_mask(ctx, count, i * 8u);
}

void
insert_return(isel_context& ctx)
{
   assert(ctx.callee_info.stack_ptr.needs_explicit_preservation);
   assert(
      ctx.callee_info.param_infos[ACO_NIR_CALL_SYSTEM_ARG_UNIFORM_PC].needs_explicit_preservation);
   assert(
      ctx.callee_info.param_infos[ACO_NIR_CALL_SYSTEM_ARG_DIVERGENT_PC].needs_explicit_preservation);

   /* stack_ptr always needs to be explicitly preserved */
   unsigned preserved_param_count = 1;
   if (ctx.callee_info.return_address.needs_explicit_preservation)
      ++preserved_param_count;
   for (auto param_info : ctx.callee_info.param_infos) {
      if (!param_info.is_reg || !param_info.needs_explicit_preservation)
         continue;
      ++preserved_param_count;
   }
   unsigned src_count = preserved_param_count + 1;
   Instruction* ret = create_instruction(aco_opcode::p_return, Format::PSEUDO, src_count, 0);
   ctx.block->instructions.emplace_back(ret);

   unsigned def_idx = 0;
   ret->operands[def_idx++] = Operand();

   Operand stack_op = Operand(ctx.callee_info.stack_ptr.def.getTemp());
   stack_op.setPrecolored(ctx.callee_info.stack_ptr.def.physReg());
   ret->operands[def_idx++] = stack_op;

   for (unsigned i = 0; i < ctx.callee_info.param_infos.size(); ++i) {
      const auto& param_info = ctx.callee_info.param_infos[i];
      if (!param_info.is_reg || !param_info.needs_explicit_preservation)
         continue;
      Temp param_temp = param_info.def.getTemp();
      if (i == ACO_NIR_CALL_SYSTEM_ARG_DIVERGENT_PC)
         param_temp = ctx.next_divergent_pc;
      else if (i == ACO_NIR_CALL_SYSTEM_ARG_UNIFORM_PC)
         param_temp = ctx.next_pc;
      Operand op = Operand(param_temp);
      op.setPrecolored(param_info.def.physReg());
      ret->operands[def_idx++] = op;
   }
   if (ctx.callee_info.return_address.needs_explicit_preservation) {
      Operand op = Operand(ctx.callee_info.return_address.def.getTemp());
      op.setPrecolored(ctx.callee_info.return_address.def.physReg());
      ret->operands[def_idx++] = op;
   }
}

void
select_program_rt(isel_context& ctx, unsigned shader_count, struct nir_shader* const* shaders,
                  const struct ac_shader_args* args)
{
   for (unsigned i = 0; i < shader_count; i++) {
      if (i) {
         ctx.block = ctx.program->create_and_insert_block();
         ctx.block->kind = block_kind_top_level | block_kind_resume;
      }

      nir_shader* nir = shaders[i];
      init_context(&ctx, nir);
      setup_fp_mode(&ctx, nir);

      RegisterDemand limit = get_addr_regs_from_waves(ctx.program, ctx.program->min_waves);

      nir_function_impl* impl = NULL;
      nir_function* traversal_function = NULL;
      nir_function* ahit_isec_function = NULL;
      nir_foreach_function (func, nir) {
         unsigned func_nir_abi = (func->driver_attributes & ACO_NIR_FUNCTION_ATTRIB_ABI_MASK);

         if (func->impl)
            impl = func->impl;
         if (func_nir_abi == ACO_NIR_CALL_ABI_TRAVERSAL)
            traversal_function = func;
         if (func_nir_abi == ACO_NIR_CALL_ABI_AHIT_ISEC)
            ahit_isec_function = func;
         if (impl && traversal_function && ahit_isec_function)
            break;
      }

      unsigned nir_abi = (impl->function->driver_attributes & ACO_NIR_FUNCTION_ATTRIB_ABI_MASK);
      param_assignment_hints callee_hints;
      if (nir_abi == ACO_NIR_CALL_ABI_AHIT_ISEC) {
         assert(traversal_function);
         callee_info traversal_info = get_callee_info(
            ctx.program->gfx_level, ctx.program->wave_size, rtTraversalABI,
            traversal_function->num_params, traversal_function->params, NULL, limit);
         callee_hints =
            get_ahit_isec_param_hints(traversal_info, ctx.program->info.descriptor_heap);
      }

      /* TODO: callable abi? */
      ctx.callee_abi = nir_abi_to_aco(impl->function->driver_attributes);
      ctx.program->callee_abi = ctx.callee_abi;
      ctx.callee_info = get_callee_info(ctx.program->gfx_level, ctx.program->wave_size,
                                        ctx.callee_abi, impl->function->num_params,
                                        impl->function->params, ctx.program, limit, callee_hints);
      ctx.program->is_callee = true;

      Instruction* startpgm = add_startpgm(&ctx, true);

      append_logical_start(ctx.block);
      split_arguments(&ctx, startpgm);
      visit_function_impl(&ctx, impl);
      /* This block doesn't need a p_reload_preserved, we add it manually after p_return */
      append_logical_end(&ctx, false);
      ctx.block->kind |= block_kind_uniform;

      if (ctx.next_pc != Temp()) {
         insert_return(ctx);
         Builder(ctx.program, ctx.block).sop1(aco_opcode::s_setpc_b64, Operand(ctx.next_pc));
      } else {
         Builder(ctx.program, ctx.block).sopp(aco_opcode::s_endpgm);
      }

      cleanup_context(&ctx);
   }

   ctx.program->config->float_mode = ctx.program->blocks[0].fp_mode.val;
   finish_program(&ctx);
}

static void
create_merged_jump_to_epilog(isel_context* ctx)
{
   Builder bld(ctx->program, ctx->block);
   std::vector<Operand> regs;

   for (unsigned i = 0; i < ctx->args->arg_count; i++) {
      if (!ctx->args->args[i].preserved)
         continue;

      const enum ac_arg_regfile file = ctx->args->args[i].file;
      const unsigned reg = ctx->args->args[i].offset;

      Operand op(ctx->arg_temps[i]);
      op.setPrecolored(PhysReg{file == AC_ARG_SGPR ? reg : reg + 256});
      regs.emplace_back(op);
   }

   Temp continue_pc =
      convert_pointer_to_64_bit(ctx, get_arg(ctx, ctx->program->info.next_stage_pc));

   aco_ptr<Instruction> jump{
      create_instruction(aco_opcode::p_jump_to_epilog, Format::PSEUDO, 1 + regs.size(), 0)};
   jump->operands[0] = Operand(continue_pc);
   for (unsigned i = 0; i < regs.size(); i++) {
      jump->operands[i + 1] = regs[i];
   }
   ctx->block->instructions.emplace_back(std::move(jump));
}

void
create_end_for_merged_shader(isel_context* ctx)
{
   std::vector<Operand> regs;

   unsigned max_args;
   if (ctx->stage.sw == SWStage::VS) {
      assert(ctx->args->vertex_id.used);
      max_args = ctx->args->vertex_id.arg_index;
   } else {
      assert(ctx->stage.sw == SWStage::TES);
      assert(ctx->args->tes_u.used);
      max_args = ctx->args->tes_u.arg_index;
   }

   struct ac_arg arg;
   arg.used = true;

   for (arg.arg_index = 0; arg.arg_index < max_args; arg.arg_index++)
      regs.emplace_back(get_arg_for_end(ctx, arg));

   build_end_with_regs(ctx, regs);
}

void
select_shader(isel_context& ctx, nir_shader* nir, const bool need_startpgm, const bool need_endpgm,
              const bool need_barrier, const bool check_merged_wave_info,
              const bool endif_merged_wave_info)
{
   init_context(&ctx, nir);
   setup_fp_mode(&ctx, nir);

   Program* program = ctx.program;

   if (need_startpgm) {
      /* Needs to be after init_context() for FS. */
      Instruction* startpgm = add_startpgm(&ctx);

      if (!program->info.vs.has_prolog &&
          (program->stage.has(SWStage::VS) || program->stage.has(SWStage::TES))) {
         Builder(ctx.program, ctx.block).sopp(aco_opcode::s_setprio, 0x3u);
      }

      append_logical_start(ctx.block);
      split_arguments(&ctx, startpgm);
   }

   if (program->gfx_level == GFX10 && program->stage.hw == AC_HW_NEXT_GEN_GEOMETRY_SHADER &&
       !program->stage.has(SWStage::GS)) {
      /* Workaround for Navi1x HW bug to ensure that all NGG waves launch before
       * s_sendmsg(GS_ALLOC_REQ).
       */
      Builder(ctx.program, ctx.block).sopp(aco_opcode::s_barrier, 0u);
   }

   if (check_merged_wave_info) {
      const unsigned i =
         nir->info.stage == MESA_SHADER_VERTEX || nir->info.stage == MESA_SHADER_TESS_EVAL ? 0 : 1;
      const Temp cond = merged_wave_info_to_mask(&ctx, i);
      begin_divergent_if_then(&ctx, cond);
   }

   if (need_barrier) {
      const sync_scope scope = ctx.stage == vertex_tess_control_hs && ctx.tcs_in_out_eq &&
                                     program->wave_size % nir->info.tess.tcs_vertices_out == 0
                                  ? scope_subgroup
                                  : scope_workgroup;

      Builder(ctx.program, ctx.block)
         .barrier(aco_opcode::p_barrier, memory_sync_info(storage_shared, semantic_acqrel, scope),
                  scope);
   }

   nir_function_impl* func = nir_shader_get_entrypoint(nir);
   visit_function_impl(&ctx, func);

   if (ctx.program->info.ps.has_epilog) {
      if (ctx.stage == fragment_fs) {
         if (ctx.options->is_opengl)
            create_fs_end_for_epilog(&ctx);
         else
            create_fs_jump_to_epilog(&ctx);

         /* FS epilogs always have at least one color/null export. */
         ctx.program->has_color_exports = true;
      }
   }

   if (endif_merged_wave_info) {
      begin_divergent_if_else(&ctx);
      end_divergent_if(&ctx);
   }

   bool is_first_stage_of_merged_shader = false;

   if (ctx.program->info.merged_shader_compiled_separately &&
       (ctx.stage.sw == SWStage::VS || ctx.stage.sw == SWStage::TES)) {
      assert(program->gfx_level >= GFX9);
      if (ctx.options->is_opengl)
         create_end_for_merged_shader(&ctx);
      else
         create_merged_jump_to_epilog(&ctx);

      is_first_stage_of_merged_shader = true;
   }

   cleanup_context(&ctx);

   if (need_endpgm) {
      program->config->float_mode = program->blocks[0].fp_mode.val;

      append_logical_end(&ctx);
      ctx.block->kind |= block_kind_uniform;

      if ((!program->info.ps.has_epilog && !is_first_stage_of_merged_shader) ||
          (nir->info.stage == MESA_SHADER_TESS_CTRL && program->gfx_level >= GFX9)) {
         Builder(program, ctx.block).sopp(aco_opcode::s_endpgm);
      }

      finish_program(&ctx);
   }
}

void
select_program_merged(isel_context& ctx, const unsigned shader_count, nir_shader* const* shaders)
{
   const bool ngg_gs = ctx.stage.hw == AC_HW_NEXT_GEN_GEOMETRY_SHADER && ctx.stage.has(SWStage::GS);
   const bool hs = ctx.stage.hw == AC_HW_HULL_SHADER;

   for (unsigned i = 0; i < shader_count; i++) {
      nir_shader* nir = shaders[i];

      /* We always need to insert p_startpgm at the beginning of the first shader.  */
      const bool need_startpgm = i == 0;

      /* Need to handle program end for last shader stage. */
      const bool need_endpgm = i == shader_count - 1;

      /* In a merged VS+TCS HS, the VS implementation can be completely empty. */
      nir_function_impl* func = nir_shader_get_entrypoint(nir);
      const bool empty_shader =
         nir_cf_list_is_empty_block(&func->body) &&
         ((nir->info.stage == MESA_SHADER_VERTEX &&
           (ctx.stage == vertex_tess_control_hs || ctx.stage == vertex_geometry_gs)) ||
          (nir->info.stage == MESA_SHADER_TESS_EVAL && ctx.stage == tess_eval_geometry_gs));

      /* See if we need to emit a check of the merged wave info SGPR. */
      const bool check_merged_wave_info =
         ctx.tcs_in_out_eq ? i == 0
                           : (shader_count >= 2 && !empty_shader && ((!ngg_gs && !hs) || i != 1));
      const bool endif_merged_wave_info = ctx.tcs_in_out_eq ? i == 1 : check_merged_wave_info;

      /* Skip s_barrier from TCS when VS outputs are not stored in the LDS. */
      const bool tcs_skip_barrier =
         ctx.stage == vertex_tess_control_hs && !ctx.any_tcs_inputs_via_lds;

      /* A barrier is usually needed at the beginning of the second shader, with exceptions. */
      const bool need_barrier = i != 0 && !ngg_gs && !tcs_skip_barrier;

      select_shader(ctx, nir, need_startpgm, need_endpgm, need_barrier, check_merged_wave_info,
                    endif_merged_wave_info);

      if (i == 0 && ctx.stage == vertex_tess_control_hs && ctx.tcs_in_out_eq) {
         /* Special handling when TCS input and output patch size is the same.
          * Outputs of the previous stage are inputs to the next stage.
          */
         ctx.inputs = ctx.outputs;
         ctx.outputs = shader_io_state();
      }
   }
}

} /* end namespace */

void
select_program(Program* program, unsigned shader_count, struct nir_shader* const* shaders,
               ac_shader_config* config, const struct aco_compiler_options* options,
               const struct aco_shader_info* info, const struct ac_shader_args* args)
{
   isel_context ctx =
      setup_isel_context(program, shader_count, shaders, config, options, info, args);

   if (ctx.stage == raytracing_cs)
      return select_program_rt(ctx, shader_count, shaders, args);

   if (shader_count >= 2) {
      program->needs_fp_mode_insertion = true;
      select_program_merged(ctx, shader_count, shaders);
   } else {
      bool need_barrier = false, check_merged_wave_info = false, endif_merged_wave_info = false;

      /* Handle separate compilation of VS+TCS and {VS,TES}+GS on GFX9+. */
      if (ctx.program->info.merged_shader_compiled_separately) {
         assert(ctx.program->gfx_level >= GFX9);
         program->needs_fp_mode_insertion = true;
         if (ctx.stage.sw == SWStage::VS || ctx.stage.sw == SWStage::TES) {
            check_merged_wave_info = endif_merged_wave_info = true;
         } else {
            const bool ngg_gs =
               ctx.stage.hw == AC_HW_NEXT_GEN_GEOMETRY_SHADER && ctx.stage.sw == SWStage::GS;
            assert(ctx.stage == tess_control_hs || ctx.stage == geometry_gs || ngg_gs);
            check_merged_wave_info = endif_merged_wave_info = !ngg_gs;
            need_barrier = !ngg_gs;
         }
      }

      select_shader(ctx, shaders[0], true, true, need_barrier, check_merged_wave_info,
                    endif_merged_wave_info);
   }
}

} // namespace aco
