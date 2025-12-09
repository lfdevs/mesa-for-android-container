/*
 * Copyright © 2018 Valve Corporation
 * Copyright © 2018 Google
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_builder.h"
#include "aco_instruction_selection.h"
#include "aco_ir.h"

namespace aco {
namespace {

static Builder
create_alu_builder(isel_context* ctx, nir_alu_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   bld.is_precise = instr->exact;
   bld.is_sz_preserve = nir_alu_instr_is_signed_zero_preserve(instr);
   bld.is_inf_preserve = nir_alu_instr_is_inf_preserve(instr);
   bld.is_nan_preserve = nir_alu_instr_is_nan_preserve(instr);
   return bld;
}

enum sgpr_extract_mode {
   sgpr_extract_sext,
   sgpr_extract_zext,
   sgpr_extract_undef,
};

Temp
extract_8_16_bit_sgpr_element(isel_context* ctx, Temp dst, nir_alu_src* src, sgpr_extract_mode mode)
{
   Temp vec = get_ssa_temp(ctx, src->src.ssa);
   unsigned src_size = src->src.ssa->bit_size;
   unsigned swizzle = src->swizzle[0];

   if (vec.size() > 1) {
      unsigned factor = 32 / src_size;
      vec = emit_extract_vector(ctx, vec, swizzle / factor, s1);
      swizzle = swizzle & (factor - 1);
   }

   Builder bld(ctx->program, ctx->block);
   Temp tmp = dst.regClass() == s2 ? bld.tmp(s1) : dst;

   if (mode == sgpr_extract_undef && swizzle == 0)
      bld.copy(Definition(tmp), vec);
   else
      bld.pseudo(aco_opcode::p_extract, Definition(tmp), bld.def(s1, scc), Operand(vec),
                 Operand::c32(swizzle), Operand::c32(src_size),
                 Operand::c32((mode == sgpr_extract_sext)));

   if (dst.regClass() == s2)
      convert_int(ctx, bld, tmp, 32, 64, mode == sgpr_extract_sext, dst);

   return dst;
}

Temp
get_alu_src(struct isel_context* ctx, nir_alu_src src, unsigned size = 1)
{
   if (src.src.ssa->num_components == 1 && size == 1)
      return get_ssa_temp(ctx, src.src.ssa);

   if (nir_src_is_const(src.src) && src.src.ssa->num_components == 1 &&
       (size * src.src.ssa->bit_size) <= 32) {
      uint32_t val = 0;
      for (unsigned i = 0; i < size; i++) {
         val |= nir_src_as_uint(src.src) << (i * src.src.ssa->bit_size);
      }
      Builder bld(ctx->program, ctx->block);
      return bld.copy(bld.def(s1), Operand::c32(val));
   }

   Temp vec = get_ssa_temp(ctx, src.src.ssa);
   unsigned elem_size = src.src.ssa->bit_size / 8u;
   bool identity_swizzle = true;

   for (unsigned i = 0; identity_swizzle && i < size; i++) {
      if (src.swizzle[i] != i)
         identity_swizzle = false;
   }
   if (identity_swizzle)
      return emit_extract_vector(ctx, vec, 0, RegClass::get(vec.type(), elem_size * size));

   assert(elem_size > 0);
   assert(vec.bytes() % elem_size == 0);

   if (elem_size < 4 && vec.type() == RegType::sgpr && size == 1) {
      assert(src.src.ssa->bit_size == 8 || src.src.ssa->bit_size == 16);
      return extract_8_16_bit_sgpr_element(ctx, ctx->program->allocateTmp(s1), &src,
                                           sgpr_extract_undef);
   }

   bool as_uniform = elem_size < 4 && vec.type() == RegType::sgpr;
   if (as_uniform)
      vec = as_vgpr(ctx, vec);

   RegClass elem_rc = RegClass::get(vec.type(), elem_size);
   if (size == 1) {
      return emit_extract_vector(ctx, vec, src.swizzle[0], elem_rc);
   } else {
      assert(size <= 4);
      std::array<Temp, NIR_MAX_VEC_COMPONENTS> elems;
      aco_ptr<Instruction> vec_instr{
         create_instruction(aco_opcode::p_create_vector, Format::PSEUDO, size, 1)};
      for (unsigned i = 0; i < size; ++i) {
         elems[i] = emit_extract_vector(ctx, vec, src.swizzle[i], elem_rc);
         vec_instr->operands[i] = Operand{elems[i]};
      }
      Temp dst = ctx->program->allocateTmp(RegClass::get(vec.type(), elem_size * size));
      vec_instr->definitions[0] = Definition(dst);
      ctx->block->instructions.emplace_back(std::move(vec_instr));
      ctx->allocated_vec.emplace(dst.id(), elems);
      return as_uniform ? Builder(ctx->program, ctx->block).as_uniform(dst) : dst;
   }
}

Temp
get_alu_src_vop3p(struct isel_context* ctx, nir_alu_src src)
{
   /* returns v2b or v1 for vop3p usage.
    * The source expects exactly 2 16bit components
    * which are within the same dword
    */
   assert(src.src.ssa->bit_size == 16);
   assert(src.swizzle[0] >> 1 == src.swizzle[1] >> 1);

   Temp tmp = get_ssa_temp(ctx, src.src.ssa);
   if (tmp.size() == 1)
      return tmp;

   /* the size is larger than 1 dword: check the swizzle */
   unsigned dword = src.swizzle[0] >> 1;

   /* extract a full dword if possible */
   if (tmp.bytes() >= (dword + 1) * 4) {
      /* if the source is split into components, use p_create_vector */
      auto it = ctx->allocated_vec.find(tmp.id());
      if (it != ctx->allocated_vec.end()) {
         unsigned index = dword << 1;
         Builder bld(ctx->program, ctx->block);
         if (it->second[index].regClass() == v2b)
            return bld.pseudo(aco_opcode::p_create_vector, bld.def(v1), it->second[index],
                              it->second[index + 1]);
      }
      return emit_extract_vector(ctx, tmp, dword, v1);
   } else {
      /* This must be a swizzled access to %a.zz where %a is v6b */
      assert(((src.swizzle[0] | src.swizzle[1]) & 1) == 0);
      assert(tmp.regClass() == v6b && dword == 1);
      return emit_extract_vector(ctx, tmp, dword * 2, v2b);
   }
}

uint32_t
get_alu_src_ub(isel_context* ctx, nir_alu_instr* instr, int src_idx)
{
   nir_scalar scalar = nir_scalar{instr->src[src_idx].src.ssa, instr->src[src_idx].swizzle[0]};
   return nir_unsigned_upper_bound(ctx->shader, ctx->range_ht, scalar);
}

void
emit_sop2_instruction(isel_context* ctx, nir_alu_instr* instr, aco_opcode op, Temp dst,
                      bool writes_scc, uint8_t uses_ub = 0)
{
   Builder bld = create_alu_builder(ctx, instr);
   bld.is_nuw = instr->no_unsigned_wrap;

   Operand operands[2] = {Operand(get_alu_src(ctx, instr->src[0])),
                          Operand(get_alu_src(ctx, instr->src[1]))};
   u_foreach_bit (i, uses_ub) {
      uint32_t src_ub = get_alu_src_ub(ctx, instr, i);
      if (src_ub <= 0xffff)
         operands[i].set16bit(true);
      else if (src_ub <= 0xffffff)
         operands[i].set24bit(true);
   }

   if (writes_scc)
      bld.sop2(op, Definition(dst), bld.def(s1, scc), operands[0], operands[1]);
   else
      bld.sop2(op, Definition(dst), operands[0], operands[1]);
}

void
emit_vop2_instruction(isel_context* ctx, nir_alu_instr* instr, aco_opcode opc, Temp dst,
                      bool commutative, bool swap_srcs = false, bool flush_denorms = false,
                      bool nuw = false, uint8_t uses_ub = 0)
{
   Builder bld = create_alu_builder(ctx, instr);
   bld.is_nuw = nuw;

   Operand operands[2] = {Operand(get_alu_src(ctx, instr->src[0])),
                          Operand(get_alu_src(ctx, instr->src[1]))};
   u_foreach_bit (i, uses_ub) {
      uint32_t src_ub = get_alu_src_ub(ctx, instr, i);
      if (src_ub <= 0xffff)
         operands[i].set16bit(true);
      else if (src_ub <= 0xffffff)
         operands[i].set24bit(true);
   }

   if (swap_srcs)
      std::swap(operands[0], operands[1]);

   if (operands[1].isOfType(RegType::sgpr)) {
      if (commutative && operands[0].isOfType(RegType::vgpr)) {
         std::swap(operands[0], operands[1]);
      } else {
         operands[1] = bld.copy(bld.def(RegType::vgpr, operands[1].size()), operands[1]);
      }
   }

   if (flush_denorms && ctx->program->gfx_level < GFX9) {
      assert(dst.size() == 1);
      Temp tmp = bld.vop2(opc, bld.def(dst.regClass()), operands[0], operands[1]);
      if (dst.bytes() == 2)
         bld.vop2(aco_opcode::v_mul_f16, Definition(dst), Operand::c16(0x3c00), tmp);
      else
         bld.vop2(aco_opcode::v_mul_f32, Definition(dst), Operand::c32(0x3f800000u), tmp);
   } else {
      bld.vop2(opc, Definition(dst), operands[0], operands[1]);
   }
}

void
emit_vop3a_instruction(isel_context* ctx, nir_alu_instr* instr, aco_opcode op, Temp dst,
                       bool flush_denorms = false, unsigned num_sources = 2, bool swap_srcs = false)
{
   assert(num_sources == 2 || num_sources == 3);
   Temp src[3] = {Temp(0, v1), Temp(0, v1), Temp(0, v1)};
   bool has_sgpr = false;
   for (unsigned i = 0; i < num_sources; i++) {
      src[i] = get_alu_src(ctx, instr->src[(swap_srcs && i < 2) ? 1 - i : i]);
      if (has_sgpr)
         src[i] = as_vgpr(ctx, src[i]);
      else
         has_sgpr = src[i].type() == RegType::sgpr;
   }

   Builder bld = create_alu_builder(ctx, instr);
   if (flush_denorms && ctx->program->gfx_level < GFX9) {
      Temp tmp;
      if (num_sources == 3)
         tmp = bld.vop3(op, bld.def(dst.regClass()), src[0], src[1], src[2]);
      else
         tmp = bld.vop3(op, bld.def(dst.regClass()), src[0], src[1]);
      if (dst.size() == 1)
         bld.vop2(aco_opcode::v_mul_f32, Definition(dst), Operand::c32(0x3f800000u), tmp);
      else
         bld.vop3(aco_opcode::v_mul_f64_e64, Definition(dst), Operand::c64(0x3FF0000000000000),
                  tmp);
   } else if (num_sources == 3) {
      bld.vop3(op, Definition(dst), src[0], src[1], src[2]);
   } else {
      bld.vop3(op, Definition(dst), src[0], src[1]);
   }
}

Builder::Result
emit_vop3p_instruction(isel_context* ctx, nir_alu_instr* instr, aco_opcode op, Temp dst)
{
   Temp src0 = get_alu_src_vop3p(ctx, instr->src[0]);
   Temp src1 = get_alu_src_vop3p(ctx, instr->src[1]);
   if (src0.type() == RegType::sgpr && src1.type() == RegType::sgpr)
      src1 = as_vgpr(ctx, src1);
   assert(instr->def.num_components == 2);

   /* swizzle to opsel: all swizzles are either 0 (x) or 1 (y) */
   unsigned opsel_lo = (instr->src[1].swizzle[0] & 1) << 1 | (instr->src[0].swizzle[0] & 1);
   unsigned opsel_hi = (instr->src[1].swizzle[1] & 1) << 1 | (instr->src[0].swizzle[1] & 1);

   Builder bld = create_alu_builder(ctx, instr);
   Builder::Result res = bld.vop3p(op, Definition(dst), src0, src1, opsel_lo, opsel_hi);
   emit_split_vector(ctx, dst, 2);
   return res;
}

void
emit_idot_instruction(isel_context* ctx, nir_alu_instr* instr, aco_opcode op, Temp dst, bool clamp,
                      unsigned neg_lo = 0)
{
   Temp src[3] = {Temp(0, v1), Temp(0, v1), Temp(0, v1)};
   bool has_sgpr = false;
   for (unsigned i = 0; i < 3; i++) {
      src[i] = get_alu_src(ctx, instr->src[i]);
      if (has_sgpr)
         src[i] = as_vgpr(ctx, src[i]);
      else
         has_sgpr = src[i].type() == RegType::sgpr;
   }

   Builder bld = create_alu_builder(ctx, instr);
   VALU_instruction& vop3p =
      bld.vop3p(op, Definition(dst), src[0], src[1], src[2], 0x0, 0x7)->valu();
   vop3p.clamp = clamp;
   vop3p.neg_lo = neg_lo;
}

void
emit_pk_shift(isel_context* ctx, nir_alu_instr* instr, aco_opcode op, Temp dst)
{
   Builder bld = create_alu_builder(ctx, instr);
   Temp src1 = get_alu_src_vop3p(ctx, instr->src[0]);
   Temp src0;

   bitarray8 opsel_lo = (instr->src[0].swizzle[0] & 1) << 1;
   bitarray8 opsel_hi = (instr->src[0].swizzle[1] & 1) << 1;

   /* NIR's shift operand is always 32bit, but we want 16bit here. */
   if (instr->src[1].swizzle[0] == instr->src[1].swizzle[1]) {
      src0 = get_alu_src(ctx, instr->src[1], 1);
   } else {
      Operand comps[2];
      for (unsigned i = 0; i < 2; i++) {
         nir_scalar s = nir_scalar_resolved(instr->src[1].src.ssa, instr->src[1].swizzle[i]);
         if (nir_scalar_is_const(s)) {
            comps[i] = Operand::c16(nir_scalar_as_uint(s));
         } else if (nir_scalar_is_alu(s) &&
                    (nir_scalar_alu_op(s) == nir_op_u2u32 ||
                     nir_scalar_alu_op(s) == nir_op_i2i32) &&
                    nir_def_as_alu(s.def)->src[0].src.ssa->bit_size == 16) {
            assert(s.def->num_components == 1);
            Temp comp = get_alu_src(ctx, nir_def_as_alu(s.def)->src[0]);
            comps[i] = Operand(emit_extract_vector(ctx, comp, 0, v2b));
         } else {
            Temp vec = get_ssa_temp(ctx, instr->src[1].src.ssa);
            RegClass rc = RegClass::get(vec.type(), 4);
            Temp comp = emit_extract_vector(ctx, vec, instr->src[1].swizzle[i], rc);
            comps[i] = Operand(emit_extract_vector(ctx, comp, 0, v2b));
         }
      }

      opsel_hi[0] = 1;

      if (comps[0].isConstant() && comps[1].isConstant()) {
         uint32_t packed = (comps[1].constantValue() << 16) | comps[0].constantValue();
         src0 = bld.copy(bld.def(s1), Operand::c32(packed));
      } else {
         src0 = bld.pseudo(aco_opcode::p_create_vector, bld.def(v1), comps[0], comps[1]);
      }
   }

   if (src0.type() == RegType::sgpr && src1.type() == RegType::sgpr)
      src1 = as_vgpr(ctx, src1);

   bld.vop3p(op, Definition(dst), src0, src1, opsel_lo, opsel_hi);
   emit_split_vector(ctx, dst, 2);
}

void
emit_pk_int16_from_8bit(isel_context* ctx, Temp dst, Temp src, unsigned byte0, unsigned byte2,
                        bool sext)
{
   Builder bld(ctx->program, ctx->block);
   assert(src.size() == 1);
   assert(dst.regClass() == v1);

   src = as_vgpr(ctx, src);

   if (byte0 == 0 && byte2 == 2 && !sext) {
      Temp mask = bld.copy(bld.def(s1), Operand::c32(0x00ff00ffu));
      bld.vop2(aco_opcode::v_and_b32, Definition(dst), mask, src);
   } else if ((byte0 & 0x1) != 0 && (byte2 & 0x1) != 0) {
      aco_opcode shift = sext ? aco_opcode::v_pk_ashrrev_i16 : aco_opcode::v_pk_lshrrev_b16;
      bld.vop3p(shift, Definition(dst), Operand::c32(8), src, byte0 & 0x2, byte2 & 0x2);
   } else {
      unsigned swizzle[2] = {byte0, byte2};
      uint32_t pk_select = 0;

      Operand msb = Operand::c32(0);

      for (unsigned i = 0; i < 2; i++) {
         pk_select |= swizzle[i] << (i * 16);
         if (!sext) {
            pk_select |= bperm_0 << (i * 16 + 8);
         } else if (swizzle[i] & 0x1) {
            pk_select |= (swizzle[i] & 0x2 ? bperm_b3_sign : bperm_b1_sign) << (i * 16 + 8);
         } else {
            if (msb.isConstant())
               msb = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(8), src);

            pk_select |= (swizzle[i] & 0x2 ? bperm_b7_sign : bperm_b5_sign) << (i * 16 + 8);
         }
      }

      bld.vop3(aco_opcode::v_perm_b32, Definition(dst), msb, src,
               bld.copy(bld.def(s1), Operand::c32(pk_select)));
   }

   emit_split_vector(ctx, dst, 2);
}

void
emit_vop1_instruction(isel_context* ctx, nir_alu_instr* instr, aco_opcode op, Temp dst)
{
   Builder bld = create_alu_builder(ctx, instr);
   if (dst.type() == RegType::sgpr)
      bld.pseudo(aco_opcode::p_as_uniform, Definition(dst),
                 bld.vop1(op, bld.def(RegType::vgpr, dst.size()), get_alu_src(ctx, instr->src[0])));
   else
      bld.vop1(op, Definition(dst), get_alu_src(ctx, instr->src[0]));
}

void
emit_vopc_instruction(isel_context* ctx, nir_alu_instr* instr, aco_opcode op, Temp dst)
{
   Temp src0 = get_alu_src(ctx, instr->src[0]);
   Temp src1 = get_alu_src(ctx, instr->src[1]);
   assert(src0.size() == src1.size());

   aco_ptr<Instruction> vopc;
   if (src1.type() == RegType::sgpr) {
      if (src0.type() == RegType::vgpr) {
         /* to swap the operands, we might also have to change the opcode */
         op = get_vcmp_swapped(op);
         Temp t = src0;
         src0 = src1;
         src1 = t;
      } else {
         src1 = as_vgpr(ctx, src1);
      }
   }

   Builder bld = create_alu_builder(ctx, instr);
   bld.vopc(op, Definition(dst), src0, src1);
}

void
emit_sopc_instruction(isel_context* ctx, nir_alu_instr* instr, aco_opcode op, Temp dst)
{
   Temp src0 = get_alu_src(ctx, instr->src[0]);
   Temp src1 = get_alu_src(ctx, instr->src[1]);
   Builder bld = create_alu_builder(ctx, instr);

   assert(dst.regClass() == bld.lm);
   assert(src0.type() == RegType::sgpr);
   assert(src1.type() == RegType::sgpr);

   /* Emit the SALU comparison instruction */
   Temp cmp = bld.sopc(op, bld.scc(bld.def(s1)), src0, src1);
   /* Turn the result into a per-lane bool */
   bool_to_vector_condition(ctx, cmp, dst);
}

void
emit_comparison(isel_context* ctx, nir_alu_instr* instr, Temp dst, aco_opcode v16_op,
                aco_opcode v32_op, aco_opcode v64_op, aco_opcode s16_op = aco_opcode::num_opcodes,
                aco_opcode s32_op = aco_opcode::num_opcodes,
                aco_opcode s64_op = aco_opcode::num_opcodes)
{
   aco_opcode s_op = instr->src[0].src.ssa->bit_size == 64   ? s64_op
                     : instr->src[0].src.ssa->bit_size == 32 ? s32_op
                                                             : s16_op;
   aco_opcode v_op = instr->src[0].src.ssa->bit_size == 64   ? v64_op
                     : instr->src[0].src.ssa->bit_size == 32 ? v32_op
                                                             : v16_op;
   bool use_valu = s_op == aco_opcode::num_opcodes || instr->def.divergent ||
                   get_ssa_temp(ctx, instr->src[0].src.ssa).type() == RegType::vgpr ||
                   get_ssa_temp(ctx, instr->src[1].src.ssa).type() == RegType::vgpr;
   aco_opcode op = use_valu ? v_op : s_op;
   assert(op != aco_opcode::num_opcodes);
   assert(dst.regClass() == ctx->program->lane_mask);

   if (use_valu)
      emit_vopc_instruction(ctx, instr, op, dst);
   else
      emit_sopc_instruction(ctx, instr, op, dst);
}

void
emit_bitwise_logic(isel_context* ctx, nir_alu_instr* instr, Temp dst,
                   Builder::WaveSpecificOpcode op, aco_opcode v32_op)
{
   Builder bld(ctx->program, ctx->block);
   Temp src0 = get_alu_src(ctx, instr->src[0], instr->def.num_components);
   Temp src1 = get_alu_src(ctx, instr->src[1], instr->def.num_components);

   if (instr->def.bit_size == 1) {
      bld.sop2(op, Definition(dst), bld.def(s1, scc), src0, src1);
   } else if (dst.regClass() == s1) {
      bld.sop2(bld.w32(op), Definition(dst), bld.def(s1, scc), src0, src1);
   } else if (dst.regClass() == s2) {
      bld.sop2(bld.w64(op), Definition(dst), bld.def(s1, scc), src0, src1);
   } else {
      assert(dst.regClass().type() == RegType::vgpr && dst.size() <= 2);

      if (src1.type() == RegType::sgpr) {
         assert(src0.type() == RegType::vgpr);
         std::swap(src0, src1);
      }

      if (dst.size() == 1) {
         bld.vop2(v32_op, Definition(dst), src0, src1);
         emit_split_vector(ctx, dst, instr->def.num_components);
      } else {
         Temp src00 = bld.tmp(src0.type(), 1), src01 = bld.tmp(src0.type(), 1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(src00), Definition(src01), src0);
         Temp src10 = bld.tmp(v1), src11 = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(src10), Definition(src11), src1);
         Temp lo = bld.vop2(v32_op, bld.def(v1), src00, src10);
         Temp hi = bld.vop2(v32_op, bld.def(v1), src01, src11);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
      }
   }
}

void
emit_bcsel(isel_context* ctx, nir_alu_instr* instr, Temp dst)
{
   Builder bld(ctx->program, ctx->block);
   Temp cond = get_alu_src(ctx, instr->src[0]);
   Temp then = get_alu_src(ctx, instr->src[1], instr->def.num_components);
   Temp els = get_alu_src(ctx, instr->src[2], instr->def.num_components);

   assert(cond.regClass() == bld.lm);

   if (dst.type() == RegType::vgpr) {
      aco_ptr<Instruction> bcsel;
      if (dst.size() == 1) {
         then = as_vgpr(ctx, then);
         els = as_vgpr(ctx, els);

         bld.vop2(aco_opcode::v_cndmask_b32, Definition(dst), els, then, cond);
      } else if (dst.size() == 2) {
         select_vec2(ctx, dst, cond, then, els);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }

      emit_split_vector(ctx, dst, instr->def.num_components);
      return;
   }

   if (instr->def.bit_size == 1) {
      assert(dst.regClass() == bld.lm);
      assert(then.regClass() == bld.lm);
      assert(els.regClass() == bld.lm);
   }

   if (!nir_src_is_divergent(&instr->src[0].src)) { /* uniform condition and values in sgpr */
      cond = bool_to_scalar_condition(ctx, cond);

      bool els_zero =
         nir_src_is_const(instr->src[2].src) && nir_src_as_uint(instr->src[2].src) == 0;

      if (dst.regClass() == s1 && els_zero) {
         /* Use s_mul_i32 because it doesn't require scc. */
         bld.sop2(aco_opcode::s_mul_i32, Definition(dst), then, cond);
      } else if (dst.regClass() == s1 || dst.regClass() == s2) {
         assert((then.regClass() == s1 || then.regClass() == s2) &&
                els.regClass() == then.regClass());
         assert(dst.size() == then.size());
         aco_opcode op =
            dst.regClass() == s1 ? aco_opcode::s_cselect_b32 : aco_opcode::s_cselect_b64;
         bld.sop2(op, Definition(dst), then, els, bld.scc(cond));
      } else {
         isel_err(&instr->instr, "Unimplemented uniform bcsel bit size");
      }
      return;
   }

   /* divergent boolean bcsel
    * this implements bcsel on bools: dst = s0 ? s1 : s2
    * are going to be: dst = (s0 & s1) | (~s0 & s2) */
   assert(instr->def.bit_size == 1);

   if (cond.id() != then.id())
      then = bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), cond, then);

   if (cond.id() == els.id())
      bld.copy(Definition(dst), then);
   else
      bld.sop2(Builder::s_or, Definition(dst), bld.def(s1, scc), then,
               bld.sop2(Builder::s_andn2, bld.def(bld.lm), bld.def(s1, scc), els, cond));
}

void
emit_vec2_f2f16(isel_context* ctx, nir_alu_instr* instr, Temp dst)
{
   Builder bld = create_alu_builder(ctx, instr);
   Temp src = get_ssa_temp(ctx, instr->src[0].src.ssa);
   RegClass rc = RegClass(src.regClass().type(), instr->src[0].src.ssa->bit_size / 32);
   Temp src0 = emit_extract_vector(ctx, src, instr->src[0].swizzle[0], rc);
   Temp src1 = emit_extract_vector(ctx, src, instr->src[0].swizzle[1], rc);

   if (dst.regClass() == s1) {
      bld.sop2(aco_opcode::s_cvt_pk_rtz_f16_f32, Definition(dst), src0, src1);
   } else {
      src1 = as_vgpr(ctx, src1);
      if (ctx->program->gfx_level == GFX8 || ctx->program->gfx_level == GFX9)
         bld.vop3(aco_opcode::v_cvt_pkrtz_f16_f32_e64, Definition(dst), src0, src1);
      else
         bld.vop2(aco_opcode::v_cvt_pkrtz_f16_f32, Definition(dst), src0, src1);
      emit_split_vector(ctx, dst, 2);
   }
}

void
emit_scaled_op(isel_context* ctx, Builder& bld, Definition dst, Temp val, aco_opcode vop,
               aco_opcode sop, uint32_t undo)
{
   if (ctx->block->fp_mode.denorm32 == 0) {
      if (dst.regClass() == v1)
         bld.vop1(vop, dst, val);
      else if (ctx->options->gfx_level >= GFX12)
         bld.vop3(sop, dst, val);
      else
         bld.pseudo(aco_opcode::p_as_uniform, dst, bld.vop1(vop, bld.def(v1), val));
      return;
   }

   /* multiply by 16777216 to handle denormals */
   Temp scale, unscale;
   if (val.regClass() == v1) {
      val = as_vgpr(ctx, val);
      Temp is_denormal = bld.tmp(bld.lm);
      VALU_instruction& valu = bld.vopc_e64(aco_opcode::v_cmp_class_f32, Definition(is_denormal),
                                            val, Operand::c32(1u << 4))
                                  ->valu();
      valu.neg[0] = true;
      valu.abs[0] = true;
      scale = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), Operand::c32(0x3f800000),
                           bld.copy(bld.def(s1), Operand::c32(0x4b800000u)), is_denormal);
      unscale = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), Operand::c32(0x3f800000),
                             bld.copy(bld.def(s1), Operand::c32(undo)), is_denormal);
   } else {
      Temp abs = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), val,
                          bld.copy(bld.def(s1), Operand::c32(0x7fffffff)));
      Temp denorm_cmp = bld.copy(bld.def(s1), Operand::c32(0x00800000));
      Temp is_denormal = bld.sopc(aco_opcode::s_cmp_lt_u32, bld.def(s1, scc), abs, denorm_cmp);
      scale = bld.sop2(aco_opcode::s_cselect_b32, bld.def(s1),
                       bld.copy(bld.def(s1), Operand::c32(0x4b800000u)), Operand::c32(0x3f800000),
                       bld.scc(is_denormal));
      unscale =
         bld.sop2(aco_opcode::s_cselect_b32, bld.def(s1), bld.copy(bld.def(s1), Operand::c32(undo)),
                  Operand::c32(0x3f800000), bld.scc(is_denormal));
   }

   if (dst.regClass() == v1) {
      Temp scaled = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), scale, as_vgpr(ctx, val));
      scaled = bld.vop1(vop, bld.def(v1), scaled);
      bld.vop2(aco_opcode::v_mul_f32, dst, unscale, scaled);
   } else {
      assert(ctx->options->gfx_level >= GFX11_5);
      Temp scaled = bld.sop2(aco_opcode::s_mul_f32, bld.def(s1), scale, val);
      if (ctx->options->gfx_level >= GFX12)
         scaled = bld.vop3(sop, bld.def(s1), scaled);
      else
         scaled = bld.as_uniform(bld.vop1(vop, bld.def(v1), scaled));
      bld.sop2(aco_opcode::s_mul_f32, dst, unscale, scaled);
   }
}

void
emit_rcp(isel_context* ctx, Builder& bld, Definition dst, Temp val)
{
   emit_scaled_op(ctx, bld, dst, val, aco_opcode::v_rcp_f32, aco_opcode::v_s_rcp_f32, 0x4b800000u);
}

void
emit_rsq(isel_context* ctx, Builder& bld, Definition dst, Temp val)
{
   emit_scaled_op(ctx, bld, dst, val, aco_opcode::v_rsq_f32, aco_opcode::v_s_rsq_f32, 0x45800000u);
}

void
emit_sqrt(isel_context* ctx, Builder& bld, Definition dst, Temp val)
{
   emit_scaled_op(ctx, bld, dst, val, aco_opcode::v_sqrt_f32, aco_opcode::v_s_sqrt_f32,
                  0x39800000u);
}

void
emit_log2(isel_context* ctx, Builder& bld, Definition dst, Temp val)
{
   emit_scaled_op(ctx, bld, dst, val, aco_opcode::v_log_f32, aco_opcode::v_s_log_f32, 0xc1c00000u);
}

Temp
emit_trunc_f64(isel_context* ctx, Builder& bld, Definition dst, Temp val)
{
   if (ctx->options->gfx_level >= GFX7)
      return bld.vop1(aco_opcode::v_trunc_f64, Definition(dst), val);

   /* GFX6 doesn't support V_TRUNC_F64, lower it. */
   /* TODO: create more efficient code! */
   if (val.type() == RegType::sgpr)
      val = as_vgpr(ctx, val);

   /* Split the input value. */
   Temp val_lo = bld.tmp(v1), val_hi = bld.tmp(v1);
   bld.pseudo(aco_opcode::p_split_vector, Definition(val_lo), Definition(val_hi), val);

   /* Extract the exponent and compute the unbiased value. */
   Temp exponent =
      bld.vop3(aco_opcode::v_bfe_u32, bld.def(v1), val_hi, Operand::c32(20u), Operand::c32(11u));
   exponent = bld.vsub32(bld.def(v1), exponent, Operand::c32(1023u));

   /* Extract the fractional part. */
   Temp fract_mask = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), Operand::c32(-1u),
                                Operand::c32(0x000fffffu));
   fract_mask = bld.vop3(aco_opcode::v_lshr_b64, bld.def(v2), fract_mask, exponent);

   Temp fract_mask_lo = bld.tmp(v1), fract_mask_hi = bld.tmp(v1);
   bld.pseudo(aco_opcode::p_split_vector, Definition(fract_mask_lo), Definition(fract_mask_hi),
              fract_mask);

   Temp fract_lo = bld.tmp(v1), fract_hi = bld.tmp(v1);
   Temp tmp = bld.vop1(aco_opcode::v_not_b32, bld.def(v1), fract_mask_lo);
   fract_lo = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), val_lo, tmp);
   tmp = bld.vop1(aco_opcode::v_not_b32, bld.def(v1), fract_mask_hi);
   fract_hi = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), val_hi, tmp);

   /* Get the sign bit. */
   Temp sign = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand::c32(0x80000000u), val_hi);

   /* Decide the operation to apply depending on the unbiased exponent. */
   Temp exp_lt0 =
      bld.vopc_e64(aco_opcode::v_cmp_lt_i32, bld.def(bld.lm), exponent, Operand::zero());
   Temp dst_lo = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), fract_lo,
                          bld.copy(bld.def(v1), Operand::zero()), exp_lt0);
   Temp dst_hi = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), fract_hi, sign, exp_lt0);
   Temp exp_gt51 = bld.vopc_e64(aco_opcode::v_cmp_gt_i32, bld.def(s2), exponent, Operand::c32(51u));
   dst_lo = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), dst_lo, val_lo, exp_gt51);
   dst_hi = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), dst_hi, val_hi, exp_gt51);

   return bld.pseudo(aco_opcode::p_create_vector, Definition(dst), dst_lo, dst_hi);
}

Temp
emit_floor_f64(isel_context* ctx, Builder& bld, Definition dst, Temp val)
{
   if (ctx->options->gfx_level >= GFX7)
      return bld.vop1(aco_opcode::v_floor_f64, Definition(dst), val);

   /* GFX6 doesn't support V_FLOOR_F64, lower it (note that it's actually
    * lowered at NIR level for precision reasons). */
   Temp src0 = as_vgpr(ctx, val);

   Temp min_val = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), Operand::c32(-1u),
                             Operand::c32(0x3fefffffu));

   Temp isnan = bld.vopc(aco_opcode::v_cmp_neq_f64, bld.def(bld.lm), src0, src0);
   Temp fract = bld.vop1(aco_opcode::v_fract_f64, bld.def(v2), src0);
   Temp min = bld.vop3(aco_opcode::v_min_f64_e64, bld.def(v2), fract, min_val);

   Temp then_lo = bld.tmp(v1), then_hi = bld.tmp(v1);
   bld.pseudo(aco_opcode::p_split_vector, Definition(then_lo), Definition(then_hi), src0);
   Temp else_lo = bld.tmp(v1), else_hi = bld.tmp(v1);
   bld.pseudo(aco_opcode::p_split_vector, Definition(else_lo), Definition(else_hi), min);

   Temp dst0 = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), else_lo, then_lo, isnan);
   Temp dst1 = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), else_hi, then_hi, isnan);

   Temp v = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), dst0, dst1);

   Instruction* add = bld.vop3(aco_opcode::v_add_f64_e64, Definition(dst), src0, v);
   add->valu().neg[1] = true;

   return add->definitions[0].getTemp();
}

Temp
uadd32_sat(Builder& bld, Definition dst, Temp src0, Temp src1)
{
   if (bld.program->gfx_level < GFX8) {
      Builder::Result add = bld.vadd32(bld.def(v1), src0, src1, true);
      return bld.vop2_e64(aco_opcode::v_cndmask_b32, dst, add.def(0).getTemp(), Operand::c32(-1),
                          add.def(1).getTemp());
   }

   Builder::Result add(NULL);
   if (bld.program->gfx_level >= GFX9) {
      add = bld.vop2_e64(aco_opcode::v_add_u32, dst, src0, src1);
   } else {
      add = bld.vop2_e64(aco_opcode::v_add_co_u32, dst, bld.def(bld.lm), src0, src1);
   }
   add->valu().clamp = 1;
   return dst.getTemp();
}

Temp
usub32_sat(Builder& bld, Definition dst, Temp src0, Temp src1)
{
   if (bld.program->gfx_level < GFX8) {
      Builder::Result sub = bld.vsub32(bld.def(v1), src0, src1, true);
      return bld.vop2_e64(aco_opcode::v_cndmask_b32, dst, sub.def(0).getTemp(), Operand::c32(0u),
                          sub.def(1).getTemp());
   }

   Builder::Result sub(NULL);
   if (bld.program->gfx_level >= GFX9) {
      sub = bld.vop2_e64(aco_opcode::v_sub_u32, dst, src0, src1);
   } else {
      sub = bld.vop2_e64(aco_opcode::v_sub_co_u32, dst, bld.def(bld.lm), src0, src1);
   }
   sub->valu().clamp = 1;
   return dst.getTemp();
}

} // namespace

void
visit_alu_instr(isel_context* ctx, nir_alu_instr* instr)
{
   Builder bld = create_alu_builder(ctx, instr);
   Temp dst = get_ssa_temp(ctx, &instr->def);
   switch (instr->op) {
   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4:
   case nir_op_vec5:
   case nir_op_vec8:
   case nir_op_vec16: {
      std::array<Temp, NIR_MAX_VEC_COMPONENTS> elems;
      unsigned num = instr->def.num_components;
      for (unsigned i = 0; i < num; ++i)
         elems[i] = get_alu_src(ctx, instr->src[i]);

      if (instr->def.bit_size >= 32 || dst.type() == RegType::vgpr) {
         aco_ptr<Instruction> vec{create_instruction(aco_opcode::p_create_vector, Format::PSEUDO,
                                                     instr->def.num_components, 1)};
         RegClass elem_rc = RegClass::get(dst.type(), instr->def.bit_size / 8u);
         for (unsigned i = 0; i < num; ++i) {
            if (elems[i].type() == RegType::sgpr && elem_rc.is_subdword())
               elems[i] = emit_extract_vector(ctx, elems[i], 0, elem_rc);

            if (nir_src_is_undef(instr->src[i].src))
               vec->operands[i] = Operand{elem_rc};
            else
               vec->operands[i] = Operand{elems[i]};
         }
         vec->definitions[0] = Definition(dst);
         ctx->block->instructions.emplace_back(std::move(vec));
         ctx->allocated_vec.emplace(dst.id(), elems);
      } else {
         bool use_s_pack = ctx->program->gfx_level >= GFX9;
         Temp mask = bld.copy(bld.def(s1), Operand::c32((1u << instr->def.bit_size) - 1));

         std::array<Temp, NIR_MAX_VEC_COMPONENTS> packed;
         uint32_t const_vals[NIR_MAX_VEC_COMPONENTS] = {};
         bitarray32 undef_mask = UINT32_MAX;
         for (unsigned i = 0; i < num; i++) {
            unsigned packed_size = use_s_pack ? 16 : 32;
            unsigned idx = i * instr->def.bit_size / packed_size;
            unsigned offset = i * instr->def.bit_size % packed_size;
            if (nir_src_is_undef(instr->src[i].src))
               continue;
            else
               undef_mask[idx] = false;

            if (nir_src_is_const(instr->src[i].src)) {
               const_vals[idx] |= nir_src_as_uint(instr->src[i].src) << offset;
               continue;
            }

            if (offset != packed_size - instr->def.bit_size)
               elems[i] =
                  bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), elems[i], mask);

            if (offset)
               elems[i] = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), elems[i],
                                   Operand::c32(offset));

            if (packed[idx].id())
               packed[idx] = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc), elems[i],
                                      packed[idx]);
            else
               packed[idx] = elems[i];
         }

         if (use_s_pack) {
            for (unsigned i = 0; i < dst.size(); i++) {
               if (packed[i * 2].id() && packed[i * 2 + 1].id()) {
                  packed[i] = bld.sop2(aco_opcode::s_pack_ll_b32_b16, bld.def(s1), packed[i * 2],
                                       packed[i * 2 + 1]);
                  const_vals[i] = const_vals[i * 2] | (const_vals[i * 2 + 1] << 16);
               } else if (packed[i * 2 + 1].id()) {
                  packed[i] = bld.sop2(aco_opcode::s_pack_ll_b32_b16, bld.def(s1),
                                       Operand::c32(const_vals[i * 2]), packed[i * 2 + 1]);
                  const_vals[i] = const_vals[i * 2 + 1] << 16;
               } else if (packed[i * 2].id()) {
                  packed[i] = bld.sop2(aco_opcode::s_pack_ll_b32_b16, bld.def(s1), packed[i * 2],
                                       Operand::c32(const_vals[i * 2 + 1]));
                  const_vals[i] = const_vals[i * 2];
               } else {
                  packed[i] = Temp(0, s1); /* Both constants, so reset the entry */
                  const_vals[i] = const_vals[i * 2] | (const_vals[i * 2 + 1] << 16);
               }

               undef_mask[i] = undef_mask[i * 2] && undef_mask[i * 2 + 1];
            }
         }

         for (unsigned i = 0; i < dst.size(); i++) {
            if (const_vals[i] && packed[i].id())
               packed[i] = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc),
                                    Operand::c32(const_vals[i]), packed[i]);
            else if (!packed[i].id() && !undef_mask[i])
               packed[i] = bld.copy(bld.def(s1), Operand::c32(const_vals[i]));
         }

         if (dst.size() == 1 && packed[0].id())
            bld.copy(Definition(dst), packed[0]);
         else {
            aco_ptr<Instruction> vec{
               create_instruction(aco_opcode::p_create_vector, Format::PSEUDO, dst.size(), 1)};
            vec->definitions[0] = Definition(dst);
            for (unsigned i = 0; i < dst.size(); ++i)
               vec->operands[i] = Operand(packed[i]);
            bld.insert(std::move(vec));
         }
      }
      break;
   }
   case nir_op_mov: {
      Temp src = get_alu_src(ctx, instr->src[0], instr->def.num_components);
      if (src.type() == RegType::vgpr && dst.type() == RegType::sgpr) {
         /* use size() instead of bytes() for 8/16-bit */
         assert(src.size() == dst.size() && "wrong src or dst register class for nir_op_mov");
         bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), src);
      } else {
         assert(src.bytes() == dst.bytes() && "wrong src or dst register class for nir_op_mov");
         bld.copy(Definition(dst), src);
      }
      break;
   }
   case nir_op_inot: {
      Temp src = get_alu_src(ctx, instr->src[0], instr->def.num_components);
      if (dst.regClass().type() == RegType::vgpr && dst.size() == 1) {
         bld.vop1(aco_opcode::v_not_b32, Definition(dst), src);
      } else if (dst.regClass() == v2) {
         Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), src);
         lo = bld.vop1(aco_opcode::v_not_b32, bld.def(v1), lo);
         hi = bld.vop1(aco_opcode::v_not_b32, bld.def(v1), hi);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
      } else if (dst.type() == RegType::sgpr) {
         aco_opcode opcode = dst.size() == 1 ? aco_opcode::s_not_b32 : aco_opcode::s_not_b64;
         bld.sop1(opcode, Definition(dst), bld.def(s1, scc), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      emit_split_vector(ctx, dst, instr->def.num_components);
      break;
   }
   case nir_op_iabs: {
      if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         Temp src = get_alu_src_vop3p(ctx, instr->src[0]);

         unsigned opsel_lo = (instr->src[0].swizzle[0] & 1) << 1;
         unsigned opsel_hi = ((instr->src[0].swizzle[1] & 1) << 1) | 1;

         Temp sub = bld.vop3p(aco_opcode::v_pk_sub_u16, Definition(bld.tmp(v1)), Operand::zero(),
                              src, opsel_lo, opsel_hi);
         bld.vop3p(aco_opcode::v_pk_max_i16, Definition(dst), sub, src, opsel_lo, opsel_hi);
         emit_split_vector(ctx, dst, 2);
         break;
      }
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == s1) {
         bld.sop1(aco_opcode::s_abs_i32, Definition(dst), bld.def(s1, scc), src);
      } else if (dst.regClass() == v1) {
         bld.vop2(aco_opcode::v_max_i32, Definition(dst), src,
                  bld.vsub32(bld.def(v1), Operand::zero(), src));
      } else if (dst.regClass() == v2b && ctx->program->gfx_level >= GFX10) {
         bld.vop3(
            aco_opcode::v_max_i16_e64, Definition(dst), src,
            bld.vop3(aco_opcode::v_sub_u16_e64, Definition(bld.tmp(v2b)), Operand::zero(2), src));
      } else if (dst.regClass() == v2b) {
         src = as_vgpr(ctx, src);
         bld.vop2(aco_opcode::v_max_i16, Definition(dst), src,
                  bld.vop2(aco_opcode::v_sub_u16, Definition(bld.tmp(v2b)), Operand::zero(2), src));
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_isign: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == s1) {
         Temp tmp =
            bld.sop2(aco_opcode::s_max_i32, bld.def(s1), bld.def(s1, scc), src, Operand::c32(-1));
         bld.sop2(aco_opcode::s_min_i32, Definition(dst), bld.def(s1, scc), tmp, Operand::c32(1u));
      } else if (dst.regClass() == s2) {
         Temp neg =
            bld.sop2(aco_opcode::s_ashr_i64, bld.def(s2), bld.def(s1, scc), src, Operand::c32(63u));
         Temp neqz;
         if (ctx->program->gfx_level >= GFX8)
            neqz = bld.sopc(aco_opcode::s_cmp_lg_u64, bld.def(s1, scc), src, Operand::zero());
         else
            neqz =
               bld.sop2(aco_opcode::s_or_b64, bld.def(s2), bld.def(s1, scc), src, Operand::zero())
                  .def(1)
                  .getTemp();
         /* SCC gets zero-extended to 64 bit */
         bld.sop2(aco_opcode::s_or_b64, Definition(dst), bld.def(s1, scc), neg, bld.scc(neqz));
      } else if (dst.regClass() == v1) {
         bld.vop3(aco_opcode::v_med3_i32, Definition(dst), Operand::c32(-1), src, Operand::c32(1u));
      } else if (dst.regClass() == v2b && ctx->program->gfx_level >= GFX9) {
         bld.vop3(aco_opcode::v_med3_i16, Definition(dst), Operand::c16(-1), src, Operand::c16(1u));
      } else if (dst.regClass() == v2b) {
         src = as_vgpr(ctx, src);
         bld.vop2(aco_opcode::v_max_i16, Definition(dst), Operand::c16(-1),
                  bld.vop2(aco_opcode::v_min_i16, Definition(bld.tmp(v1)), Operand::c16(1u), src));
      } else if (dst.regClass() == v2) {
         Temp upper = emit_extract_vector(ctx, src, 1, v1);
         Temp neg = bld.vop2(aco_opcode::v_ashrrev_i32, bld.def(v1), Operand::c32(31u), upper);
         Temp gtz = bld.vopc(aco_opcode::v_cmp_ge_i64, bld.def(bld.lm), Operand::zero(), src);
         Temp lower = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), Operand::c32(1u), neg, gtz);
         upper = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), Operand::zero(), neg, gtz);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lower, upper);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_imax: {
      if (dst.regClass() == v2b && ctx->program->gfx_level >= GFX10) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_max_i16_e64, dst);
      } else if (dst.regClass() == v2b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_max_i16, dst, true);
      } else if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_max_i16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_max_i32, dst, true);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_max_i32, dst, true);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_umax: {
      if (dst.regClass() == v2b && ctx->program->gfx_level >= GFX10) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_max_u16_e64, dst);
      } else if (dst.regClass() == v2b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_max_u16, dst, true);
      } else if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_max_u16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_max_u32, dst, true);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_max_u32, dst, true);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_imin: {
      if (dst.regClass() == v2b && ctx->program->gfx_level >= GFX10) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_min_i16_e64, dst);
      } else if (dst.regClass() == v2b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_min_i16, dst, true);
      } else if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_min_i16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_min_i32, dst, true);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_min_i32, dst, true);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_umin: {
      if (dst.regClass() == v2b && ctx->program->gfx_level >= GFX10) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_min_u16_e64, dst);
      } else if (dst.regClass() == v2b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_min_u16, dst, true);
      } else if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_min_u16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_min_u32, dst, true);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_min_u32, dst, true);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ior: {
      emit_bitwise_logic(ctx, instr, dst, Builder::s_or, aco_opcode::v_or_b32);
      break;
   }
   case nir_op_iand: {
      emit_bitwise_logic(ctx, instr, dst, Builder::s_and, aco_opcode::v_and_b32);
      break;
   }
   case nir_op_ixor: {
      emit_bitwise_logic(ctx, instr, dst, Builder::s_xor, aco_opcode::v_xor_b32);
      break;
   }
   case nir_op_ushr: {
      if (dst.regClass() == v2b && ctx->program->gfx_level >= GFX10) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_lshrrev_b16_e64, dst, false, 2, true);
      } else if (dst.regClass() == v2b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_lshrrev_b16, dst, false, true);
      } else if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         emit_pk_shift(ctx, instr, aco_opcode::v_pk_lshrrev_b16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_lshrrev_b32, dst, false, true);
      } else if (dst.regClass() == v2 && ctx->program->gfx_level >= GFX8) {
         bld.vop3(aco_opcode::v_lshrrev_b64, Definition(dst), get_alu_src(ctx, instr->src[1]),
                  get_alu_src(ctx, instr->src[0]));
      } else if (dst.regClass() == v2) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_lshr_b64, dst);
      } else if (dst.regClass() == s2) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_lshr_b64, dst, true);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_lshr_b32, dst, true);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ishl: {
      if (dst.regClass() == v2b && ctx->program->gfx_level >= GFX10) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_lshlrev_b16_e64, dst, false, 2, true);
      } else if (dst.regClass() == v2b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_lshlrev_b16, dst, false, true);
      } else if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         emit_pk_shift(ctx, instr, aco_opcode::v_pk_lshlrev_b16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_lshlrev_b32, dst, false, true, false,
                               false, 1);
      } else if (dst.regClass() == v2 && ctx->program->gfx_level >= GFX12) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_lshlrev_b64, dst, false, true);
      } else if (dst.regClass() == v2 && ctx->program->gfx_level >= GFX8) {
         bld.vop3(aco_opcode::v_lshlrev_b64_e64, Definition(dst), get_alu_src(ctx, instr->src[1]),
                  get_alu_src(ctx, instr->src[0]));
      } else if (dst.regClass() == v2) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_lshl_b64, dst);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_lshl_b32, dst, true, 1);
      } else if (dst.regClass() == s2) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_lshl_b64, dst, true);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ishr: {
      if (dst.regClass() == v2b && ctx->program->gfx_level >= GFX10) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_ashrrev_i16_e64, dst, false, 2, true);
      } else if (dst.regClass() == v2b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_ashrrev_i16, dst, false, true);
      } else if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         emit_pk_shift(ctx, instr, aco_opcode::v_pk_ashrrev_i16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_ashrrev_i32, dst, false, true);
      } else if (dst.regClass() == v2 && ctx->program->gfx_level >= GFX8) {
         bld.vop3(aco_opcode::v_ashrrev_i64, Definition(dst), get_alu_src(ctx, instr->src[1]),
                  get_alu_src(ctx, instr->src[0]));
      } else if (dst.regClass() == v2) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_ashr_i64, dst);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_ashr_i32, dst, true);
      } else if (dst.regClass() == s2) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_ashr_i64, dst, true);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_find_lsb: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (src.regClass() == s1) {
         bld.sop1(aco_opcode::s_ff1_i32_b32, Definition(dst), src);
      } else if (src.regClass() == v1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_ffbl_b32, dst);
      } else if (src.regClass() == s2) {
         bld.sop1(aco_opcode::s_ff1_i32_b64, Definition(dst), src);
      } else if (src.regClass() == v2) {
         Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), src);
         lo = bld.vop1(aco_opcode::v_ffbl_b32, bld.def(v1), lo);
         hi = bld.vop1(aco_opcode::v_ffbl_b32, bld.def(v1), hi);
         hi = bld.vop2(aco_opcode::v_or_b32, bld.def(v1), Operand::c32(32u), hi);
         bld.vop2(aco_opcode::v_min_u32, Definition(dst), lo, hi);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ufind_msb:
   case nir_op_ifind_msb: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (src.regClass() == s1 || src.regClass() == s2) {
         aco_opcode op = src.regClass() == s2
                            ? (instr->op == nir_op_ufind_msb ? aco_opcode::s_flbit_i32_b64
                                                             : aco_opcode::s_flbit_i32_i64)
                            : (instr->op == nir_op_ufind_msb ? aco_opcode::s_flbit_i32_b32
                                                             : aco_opcode::s_flbit_i32);
         Temp msb_rev = bld.sop1(op, bld.def(s1), src);

         Builder::Result sub = bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.def(s1, scc),
                                        Operand::c32(src.size() * 32u - 1u), msb_rev);
         Temp msb = sub.def(0).getTemp();
         Temp carry = sub.def(1).getTemp();

         bld.sop2(aco_opcode::s_cselect_b32, Definition(dst), Operand::c32(-1), msb,
                  bld.scc(carry));
      } else if (src.regClass() == v1) {
         aco_opcode op =
            instr->op == nir_op_ufind_msb ? aco_opcode::v_ffbh_u32 : aco_opcode::v_ffbh_i32;
         Temp msb_rev = bld.tmp(v1);
         emit_vop1_instruction(ctx, instr, op, msb_rev);
         Temp msb = bld.tmp(v1);
         Temp carry =
            bld.vsub32(Definition(msb), Operand::c32(31u), Operand(msb_rev), true).def(1).getTemp();
         bld.vop2(aco_opcode::v_cndmask_b32, Definition(dst), msb, msb_rev, carry);
      } else if (src.regClass() == v2) {
         aco_opcode op =
            instr->op == nir_op_ufind_msb ? aco_opcode::v_ffbh_u32 : aco_opcode::v_ffbh_i32;

         Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), src);

         lo = bld.vop1(op, bld.def(v1), lo);
         lo = bld.vop2(aco_opcode::v_or_b32, bld.def(v1), Operand::c32(32), lo);
         hi = bld.vop1(op, bld.def(v1), hi);
         Temp msb_rev = bld.vop2(aco_opcode::v_min_u32, bld.def(v1), lo, hi);

         Temp msb = bld.tmp(v1);
         Temp carry =
            bld.vsub32(Definition(msb), Operand::c32(63u), Operand(msb_rev), true).def(1).getTemp();
         bld.vop2(aco_opcode::v_cndmask_b32, Definition(dst), msb, msb_rev, carry);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ufind_msb_rev:
   case nir_op_ifind_msb_rev: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (src.regClass() == s1) {
         aco_opcode op = instr->op == nir_op_ufind_msb_rev ? aco_opcode::s_flbit_i32_b32
                                                           : aco_opcode::s_flbit_i32;
         bld.sop1(op, Definition(dst), src);
      } else if (src.regClass() == v1) {
         aco_opcode op =
            instr->op == nir_op_ufind_msb_rev ? aco_opcode::v_ffbh_u32 : aco_opcode::v_ffbh_i32;
         emit_vop1_instruction(ctx, instr, op, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_bitfield_reverse: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == s1) {
         Temp rev = bld.sop1(aco_opcode::s_brev_b32, bld.def(s1), src);

         if (instr->def.bit_size != 32) {
            bld.pseudo(aco_opcode::p_extract, Definition(dst), bld.def(s1, scc), rev,
                       Operand::c32(instr->def.bit_size == 8 ? 3 : 1),
                       Operand::c32(instr->def.bit_size), Operand::zero());
         } else {
            bld.copy(Definition(dst), rev);
         }
      } else if (dst.regClass() == s2) {
         bld.sop1(aco_opcode::s_brev_b64, Definition(dst), src);
      } else if (dst.regClass() == v1 || dst.regClass() == v1b || dst.regClass() == v2b) {
         Temp rev = bld.vop1(aco_opcode::v_bfrev_b32, bld.def(v1), src);

         if (instr->def.bit_size != 32) {
            bld.pseudo(aco_opcode::p_extract_vector, Definition(dst), rev,
                       Operand::c32(instr->def.bit_size == 8 ? 3 : 1));
         } else {
            bld.copy(Definition(dst), rev);
         }
      } else if (dst.regClass() == v2) {
         Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(hi), Definition(lo), src);
         lo = bld.vop1(aco_opcode::v_bfrev_b32, bld.def(v1), lo);
         hi = bld.vop1(aco_opcode::v_bfrev_b32, bld.def(v1), hi);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ubitfield_extract:
   case nir_op_ibitfield_extract: {
      assert(instr->def.bit_size <= 16);
      if (dst.type() == RegType::sgpr) {
         Temp base = get_alu_src(ctx, instr->src[0]);
         Temp offset = get_alu_src(ctx, instr->src[1]);
         Temp bits = get_alu_src(ctx, instr->src[2]);
         Temp extract;

         if (nir_src_is_const(instr->src[1].src) && nir_src_is_const(instr->src[2].src)) {
            uint32_t c_offset = nir_src_as_uint(instr->src[1].src);
            uint32_t c_bits = nir_src_as_uint(instr->src[2].src);
            extract = bld.copy(bld.def(s1), Operand::c32(c_offset | (c_bits << 16)));
         } else if (ctx->program->gfx_level >= GFX9) {
            extract = bld.sop2(aco_opcode::s_pack_ll_b32_b16, bld.def(s1), offset, bits);
         } else {
            if (nir_src_is_const(instr->src[2].src)) {
               bits = bld.copy(bld.def(s1), Operand::c32(nir_src_as_uint(instr->src[2].src) << 16));
            } else {
               bits = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), bits,
                               Operand::c32(16u));
            }

            if (nir_src_is_const(instr->src[1].src) && !nir_src_as_uint(instr->src[1].src)) {
               extract = bits;
            } else {
               extract =
                  bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc), bits, offset);
            }
         }

         aco_opcode opcode =
            instr->op == nir_op_ubitfield_extract ? aco_opcode::s_bfe_u32 : aco_opcode::s_bfe_i32;
         bld.sop2(opcode, Definition(dst), bld.def(s1, scc), base, extract);
      } else {
         aco_opcode opcode =
            instr->op == nir_op_ubitfield_extract ? aco_opcode::v_bfe_u32 : aco_opcode::v_bfe_i32;
         emit_vop3a_instruction(ctx, instr, opcode, dst, false, 3);
      }
      break;
   }
   case nir_op_iadd: {
      if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_add_u32, dst, true);
         break;
      } else if (dst.bytes() <= 2 && ctx->program->gfx_level >= GFX10) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_add_u16_e64, dst);
         break;
      } else if (dst.bytes() <= 2 && ctx->program->gfx_level >= GFX8) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_add_u16, dst, true);
         break;
      } else if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_add_u16, dst);
         break;
      } else if (dst.regClass() == s2 && ctx->program->gfx_level >= GFX12) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_add_u64, dst, false);
         break;
      }

      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      if (dst.type() == RegType::vgpr && dst.bytes() <= 4) {
         if (instr->no_unsigned_wrap)
            bld.nuw().vadd32(Definition(dst), Operand(src0), Operand(src1));
         else
            bld.vadd32(Definition(dst), Operand(src0), Operand(src1));
         break;
      }

      assert(src0.size() == 2 && src1.size() == 2);
      Temp src00 = bld.tmp(src0.type(), 1);
      Temp src01 = bld.tmp(dst.type(), 1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src00), Definition(src01), src0);
      Temp src10 = bld.tmp(src1.type(), 1);
      Temp src11 = bld.tmp(dst.type(), 1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src10), Definition(src11), src1);

      if (dst.regClass() == s2) {
         Temp carry = bld.tmp(s1);
         Temp dst0 =
            bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.scc(Definition(carry)), src00, src10);
         Temp dst1 = bld.sop2(aco_opcode::s_addc_u32, bld.def(s1), bld.def(s1, scc), src01, src11,
                              bld.scc(carry));
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), dst0, dst1);
      } else if (dst.regClass() == v2) {
         Temp dst0 = bld.tmp(v1);
         Temp carry = bld.vadd32(Definition(dst0), src00, src10, true).def(1).getTemp();
         Temp dst1 = bld.vadd32(bld.def(v1), src01, src11, false, carry);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), dst0, dst1);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_uadd_sat: {
      if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         Instruction* add_instr = emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_add_u16, dst);
         add_instr->valu().clamp = 1;
         break;
      }
      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      if (dst.regClass() == s1) {
         Temp tmp = bld.tmp(s1), carry = bld.tmp(s1);
         bld.sop2(aco_opcode::s_add_u32, Definition(tmp), bld.scc(Definition(carry)), src0, src1);
         bld.sop2(aco_opcode::s_cselect_b32, Definition(dst), Operand::c32(-1), tmp,
                  bld.scc(carry));
         break;
      } else if (dst.regClass() == v2b) {
         Instruction* add_instr;
         if (ctx->program->gfx_level >= GFX10) {
            add_instr = bld.vop3(aco_opcode::v_add_u16_e64, Definition(dst), src0, src1).instr;
         } else {
            if (src1.type() == RegType::sgpr)
               std::swap(src0, src1);
            add_instr =
               bld.vop2_e64(aco_opcode::v_add_u16, Definition(dst), src0, as_vgpr(ctx, src1)).instr;
         }
         add_instr->valu().clamp = 1;
         break;
      } else if (dst.regClass() == v1) {
         uadd32_sat(bld, Definition(dst), src0, src1);
         break;
      }

      assert(src0.size() == 2 && src1.size() == 2);

      Temp src00 = bld.tmp(src0.type(), 1);
      Temp src01 = bld.tmp(src0.type(), 1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src00), Definition(src01), src0);
      Temp src10 = bld.tmp(src1.type(), 1);
      Temp src11 = bld.tmp(src1.type(), 1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src10), Definition(src11), src1);

      if (dst.regClass() == s2) {
         Temp carry0 = bld.tmp(s1);
         Temp carry1 = bld.tmp(s1);

         Temp no_sat0 =
            bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.scc(Definition(carry0)), src00, src10);
         Temp no_sat1 = bld.sop2(aco_opcode::s_addc_u32, bld.def(s1), bld.scc(Definition(carry1)),
                                 src01, src11, bld.scc(carry0));

         Temp no_sat = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), no_sat0, no_sat1);

         bld.sop2(aco_opcode::s_cselect_b64, Definition(dst), Operand::c64(-1), no_sat,
                  bld.scc(carry1));
      } else if (dst.regClass() == v2) {
         Temp no_sat0 = bld.tmp(v1);
         Temp dst0 = bld.tmp(v1);
         Temp dst1 = bld.tmp(v1);

         Temp carry0 = bld.vadd32(Definition(no_sat0), src00, src10, true).def(1).getTemp();
         Temp carry1;

         if (ctx->program->gfx_level >= GFX8) {
            carry1 = bld.tmp(bld.lm);
            bld.vop2_e64(aco_opcode::v_addc_co_u32, Definition(dst1), Definition(carry1),
                         as_vgpr(ctx, src01), as_vgpr(ctx, src11), carry0)
               ->valu()
               .clamp = 1;
         } else {
            Temp no_sat1 = bld.tmp(v1);
            carry1 = bld.vadd32(Definition(no_sat1), src01, src11, true, carry0).def(1).getTemp();
            bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(dst1), no_sat1, Operand::c32(-1),
                         carry1);
         }

         bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(dst0), no_sat0, Operand::c32(-1),
                      carry1);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), dst0, dst1);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_iadd_sat: {
      if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         Instruction* add_instr = emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_add_i16, dst);
         add_instr->valu().clamp = 1;
         break;
      }
      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      if (dst.regClass() == s1) {
         Temp cond = bld.sopc(aco_opcode::s_cmp_lt_i32, bld.def(s1, scc), src1, Operand::zero());
         Temp bound = bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.scc(bld.def(s1, scc)),
                               Operand::c32(INT32_MAX), cond);
         Temp overflow = bld.tmp(s1);
         Temp add =
            bld.sop2(aco_opcode::s_add_i32, bld.def(s1), bld.scc(Definition(overflow)), src0, src1);
         bld.sop2(aco_opcode::s_cselect_b32, Definition(dst), bound, add, bld.scc(overflow));
         break;
      }

      src1 = as_vgpr(ctx, src1);

      if (dst.regClass() == v2b) {
         Instruction* add_instr =
            bld.vop3(aco_opcode::v_add_i16, Definition(dst), src0, src1).instr;
         add_instr->valu().clamp = 1;
      } else if (dst.regClass() == v1) {
         Instruction* add_instr =
            bld.vop3(aco_opcode::v_add_i32, Definition(dst), src0, src1).instr;
         add_instr->valu().clamp = 1;
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_uadd_carry: {
      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      if (dst.regClass() == s1) {
         bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.scc(Definition(dst)), src0, src1);
         break;
      }
      if (dst.regClass() == v1) {
         Temp carry = bld.vadd32(bld.def(v1), src0, src1, true).def(1).getTemp();
         bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(dst), Operand::zero(), Operand::c32(1u),
                      carry);
         break;
      }

      Temp src00 = bld.tmp(src0.type(), 1);
      Temp src01 = bld.tmp(dst.type(), 1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src00), Definition(src01), src0);
      Temp src10 = bld.tmp(src1.type(), 1);
      Temp src11 = bld.tmp(dst.type(), 1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src10), Definition(src11), src1);
      if (dst.regClass() == s2) {
         Temp carry = bld.tmp(s1);
         bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.scc(Definition(carry)), src00, src10);
         carry = bld.sop2(aco_opcode::s_addc_u32, bld.def(s1), bld.scc(bld.def(s1)), src01, src11,
                          bld.scc(carry))
                    .def(1)
                    .getTemp();
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), carry, Operand::zero());
      } else if (dst.regClass() == v2) {
         Temp carry = bld.vadd32(bld.def(v1), src00, src10, true).def(1).getTemp();
         carry = bld.vadd32(bld.def(v1), src01, src11, true, carry).def(1).getTemp();
         carry = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), Operand::zero(),
                              Operand::c32(1u), carry);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), carry, Operand::zero());
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_isub: {
      if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_sub_i32, dst, true);
         break;
      } else if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_sub_u16, dst);
         break;
      } else if (dst.regClass() == s2 && ctx->program->gfx_level >= GFX12) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_sub_u64, dst, false);
         break;
      }

      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      if (dst.regClass() == v1) {
         bld.vsub32(Definition(dst), src0, src1);
         break;
      } else if (dst.bytes() <= 2) {
         if (ctx->program->gfx_level >= GFX10)
            bld.vop3(aco_opcode::v_sub_u16_e64, Definition(dst), src0, src1);
         else if (src1.type() == RegType::sgpr)
            bld.vop2(aco_opcode::v_subrev_u16, Definition(dst), src1, as_vgpr(ctx, src0));
         else if (ctx->program->gfx_level >= GFX8)
            bld.vop2(aco_opcode::v_sub_u16, Definition(dst), src0, as_vgpr(ctx, src1));
         else
            bld.vsub32(Definition(dst), src0, src1);
         break;
      }

      Temp src00 = bld.tmp(src0.type(), 1);
      Temp src01 = bld.tmp(dst.type(), 1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src00), Definition(src01), src0);
      Temp src10 = bld.tmp(src1.type(), 1);
      Temp src11 = bld.tmp(dst.type(), 1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src10), Definition(src11), src1);
      if (dst.regClass() == s2) {
         Temp borrow = bld.tmp(s1);
         Temp dst0 =
            bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.scc(Definition(borrow)), src00, src10);
         Temp dst1 = bld.sop2(aco_opcode::s_subb_u32, bld.def(s1), bld.def(s1, scc), src01, src11,
                              bld.scc(borrow));
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), dst0, dst1);
      } else if (dst.regClass() == v2) {
         Temp lower = bld.tmp(v1);
         Temp borrow = bld.vsub32(Definition(lower), src00, src10, true).def(1).getTemp();
         Temp upper = bld.vsub32(bld.def(v1), src01, src11, false, borrow);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lower, upper);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_usub_borrow: {
      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      if (dst.regClass() == s1) {
         bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.scc(Definition(dst)), src0, src1);
         break;
      } else if (dst.regClass() == v1) {
         Temp borrow = bld.vsub32(bld.def(v1), src0, src1, true).def(1).getTemp();
         bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(dst), Operand::zero(), Operand::c32(1u),
                      borrow);
         break;
      }

      Temp src00 = bld.tmp(src0.type(), 1);
      Temp src01 = bld.tmp(dst.type(), 1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src00), Definition(src01), src0);
      Temp src10 = bld.tmp(src1.type(), 1);
      Temp src11 = bld.tmp(dst.type(), 1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src10), Definition(src11), src1);
      if (dst.regClass() == s2) {
         Temp borrow = bld.tmp(s1);
         bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.scc(Definition(borrow)), src00, src10);
         borrow = bld.sop2(aco_opcode::s_subb_u32, bld.def(s1), bld.scc(bld.def(s1)), src01, src11,
                           bld.scc(borrow))
                     .def(1)
                     .getTemp();
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), borrow, Operand::zero());
      } else if (dst.regClass() == v2) {
         Temp borrow = bld.vsub32(bld.def(v1), src00, src10, true).def(1).getTemp();
         borrow = bld.vsub32(bld.def(v1), src01, src11, true, Operand(borrow)).def(1).getTemp();
         borrow = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), Operand::zero(),
                               Operand::c32(1u), borrow);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), borrow, Operand::zero());
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_usub_sat: {
      if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         Instruction* sub_instr = emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_sub_u16, dst);
         sub_instr->valu().clamp = 1;
         break;
      }
      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      if (dst.regClass() == s1) {
         Temp tmp = bld.tmp(s1), carry = bld.tmp(s1);
         bld.sop2(aco_opcode::s_sub_u32, Definition(tmp), bld.scc(Definition(carry)), src0, src1);
         bld.sop2(aco_opcode::s_cselect_b32, Definition(dst), Operand::c32(0), tmp, bld.scc(carry));
         break;
      } else if (dst.regClass() == v2b) {
         Instruction* sub_instr;
         if (ctx->program->gfx_level >= GFX10) {
            sub_instr = bld.vop3(aco_opcode::v_sub_u16_e64, Definition(dst), src0, src1).instr;
         } else {
            aco_opcode op = aco_opcode::v_sub_u16;
            if (src1.type() == RegType::sgpr) {
               std::swap(src0, src1);
               op = aco_opcode::v_subrev_u16;
            }
            sub_instr = bld.vop2_e64(op, Definition(dst), src0, as_vgpr(ctx, src1)).instr;
         }
         sub_instr->valu().clamp = 1;
         break;
      } else if (dst.regClass() == v1) {
         usub32_sat(bld, Definition(dst), src0, as_vgpr(ctx, src1));
         break;
      }

      assert(src0.size() == 2 && src1.size() == 2);
      Temp src00 = bld.tmp(src0.type(), 1);
      Temp src01 = bld.tmp(src0.type(), 1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src00), Definition(src01), src0);
      Temp src10 = bld.tmp(src1.type(), 1);
      Temp src11 = bld.tmp(src1.type(), 1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src10), Definition(src11), src1);

      if (dst.regClass() == s2) {
         Temp carry0 = bld.tmp(s1);
         Temp carry1 = bld.tmp(s1);

         Temp no_sat0 =
            bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.scc(Definition(carry0)), src00, src10);
         Temp no_sat1 = bld.sop2(aco_opcode::s_subb_u32, bld.def(s1), bld.scc(Definition(carry1)),
                                 src01, src11, bld.scc(carry0));

         Temp no_sat = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), no_sat0, no_sat1);

         bld.sop2(aco_opcode::s_cselect_b64, Definition(dst), Operand::c64(0ull), no_sat,
                  bld.scc(carry1));
      } else if (dst.regClass() == v2) {
         Temp no_sat0 = bld.tmp(v1);
         Temp dst0 = bld.tmp(v1);
         Temp dst1 = bld.tmp(v1);

         Temp carry0 = bld.vsub32(Definition(no_sat0), src00, src10, true).def(1).getTemp();
         Temp carry1;

         if (ctx->program->gfx_level >= GFX8) {
            carry1 = bld.tmp(bld.lm);
            bld.vop2_e64(aco_opcode::v_subb_co_u32, Definition(dst1), Definition(carry1),
                         as_vgpr(ctx, src01), as_vgpr(ctx, src11), carry0)
               ->valu()
               .clamp = 1;
         } else {
            Temp no_sat1 = bld.tmp(v1);
            carry1 = bld.vsub32(Definition(no_sat1), src01, src11, true, carry0).def(1).getTemp();
            bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(dst1), no_sat1, Operand::c32(0u),
                         carry1);
         }

         bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(dst0), no_sat0, Operand::c32(0u),
                      carry1);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), dst0, dst1);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_isub_sat: {
      if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         Instruction* sub_instr = emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_sub_i16, dst);
         sub_instr->valu().clamp = 1;
         break;
      }
      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      if (dst.regClass() == s1) {
         Temp cond = bld.sopc(aco_opcode::s_cmp_gt_i32, bld.def(s1, scc), src1, Operand::zero());
         Temp bound = bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.scc(bld.def(s1, scc)),
                               Operand::c32(INT32_MAX), cond);
         Temp overflow = bld.tmp(s1);
         Temp sub =
            bld.sop2(aco_opcode::s_sub_i32, bld.def(s1), bld.scc(Definition(overflow)), src0, src1);
         bld.sop2(aco_opcode::s_cselect_b32, Definition(dst), bound, sub, bld.scc(overflow));
         break;
      }

      src1 = as_vgpr(ctx, src1);

      if (dst.regClass() == v2b) {
         Instruction* sub_instr =
            bld.vop3(aco_opcode::v_sub_i16, Definition(dst), src0, src1).instr;
         sub_instr->valu().clamp = 1;
      } else if (dst.regClass() == v1) {
         Instruction* sub_instr =
            bld.vop3(aco_opcode::v_sub_i32, Definition(dst), src0, src1).instr;
         sub_instr->valu().clamp = 1;
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_imul: {
      if (dst.bytes() <= 2 && ctx->program->gfx_level >= GFX10) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_mul_lo_u16_e64, dst);
      } else if (dst.bytes() <= 2 && ctx->program->gfx_level >= GFX8) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_mul_lo_u16, dst, true);
      } else if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_mul_lo_u16, dst);
      } else if (dst.type() == RegType::vgpr) {
         uint32_t src0_ub = get_alu_src_ub(ctx, instr, 0);
         uint32_t src1_ub = get_alu_src_ub(ctx, instr, 1);

         if (src0_ub <= 0xffffff && src1_ub <= 0xffffff) {
            bool nuw_16bit = src0_ub <= 0xffff && src1_ub <= 0xffff && src0_ub * src1_ub <= 0xffff;
            emit_vop2_instruction(ctx, instr, aco_opcode::v_mul_u32_u24, dst,
                                  true /* commutative */, false, false, nuw_16bit, 0x3);
         } else if (nir_src_is_const(instr->src[0].src)) {
            bld.v_mul_imm(Definition(dst), get_alu_src(ctx, instr->src[1]),
                          nir_src_as_uint(instr->src[0].src), false);
         } else if (nir_src_is_const(instr->src[1].src)) {
            bld.v_mul_imm(Definition(dst), get_alu_src(ctx, instr->src[0]),
                          nir_src_as_uint(instr->src[1].src), false);
         } else {
            emit_vop3a_instruction(ctx, instr, aco_opcode::v_mul_lo_u32, dst);
         }
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_mul_i32, dst, false, 0x3);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_imul24_relaxed: {
      if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_mul_i32, dst, false, 0x3);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_mul_i32_i24, dst, true);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_umul24_relaxed: {
      if (dst.regClass() == s1) {
         Operand op1(get_alu_src(ctx, instr->src[0]));
         Operand op2(get_alu_src(ctx, instr->src[1]));
         op1.set24bit(true);
         op2.set24bit(true);
         bld.sop2(aco_opcode::s_mul_i32, Definition(dst), op1, op2);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_mul_u32_u24, dst, true /* commutative */,
                               false, false, false, 0x3);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_umul_high: {
      if (dst.regClass() == s1 && ctx->options->gfx_level >= GFX9) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_mul_hi_u32, dst, false);
      } else if (dst.bytes() == 4) {
         uint32_t src0_ub = get_alu_src_ub(ctx, instr, 0);
         uint32_t src1_ub = get_alu_src_ub(ctx, instr, 1);

         Temp tmp = dst.regClass() == s1 ? bld.tmp(v1) : dst;
         if (src0_ub <= 0xffffff && src1_ub <= 0xffffff) {
            emit_vop2_instruction(ctx, instr, aco_opcode::v_mul_hi_u32_u24, tmp, true);
         } else {
            emit_vop3a_instruction(ctx, instr, aco_opcode::v_mul_hi_u32, tmp);
         }

         if (dst.regClass() == s1)
            bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), tmp);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_imul_high: {
      if (dst.regClass() == v1) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_mul_hi_i32, dst);
      } else if (dst.regClass() == s1 && ctx->options->gfx_level >= GFX9) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_mul_hi_i32, dst, false);
      } else if (dst.regClass() == s1) {
         Temp tmp = bld.vop3(aco_opcode::v_mul_hi_i32, bld.def(v1), get_alu_src(ctx, instr->src[0]),
                             as_vgpr(ctx, get_alu_src(ctx, instr->src[1])));
         bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), tmp);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fmul: {
      if (dst.regClass() == v2b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_mul_f16, dst, true);
      } else if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_mul_f16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_mul_f32, dst, true);
      } else if (dst.regClass() == v2 && ctx->options->gfx_level >= GFX12) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_mul_f64, dst, true);
      } else if (dst.regClass() == v2) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_mul_f64_e64, dst);
      } else if (dst.regClass() == s1 && instr->def.bit_size == 16) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_mul_f16, dst, false);
      } else if (dst.regClass() == s1 && instr->def.bit_size == 32) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_mul_f32, dst, false);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fmulz: {
      if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_mul_legacy_f32, dst, true);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fadd: {
      if (dst.regClass() == v2b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_add_f16, dst, true);
      } else if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_add_f16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_add_f32, dst, true);
      } else if (dst.regClass() == v2 && ctx->options->gfx_level >= GFX12) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_add_f64, dst, true);
      } else if (dst.regClass() == v2) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_add_f64_e64, dst);
      } else if (dst.regClass() == s1 && instr->def.bit_size == 16) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_add_f16, dst, false);
      } else if (dst.regClass() == s1 && instr->def.bit_size == 32) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_add_f32, dst, false);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fsub: {
      if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         Instruction* add = emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_add_f16, dst);
         VALU_instruction& sub = add->valu();
         sub.neg_lo[1] = true;
         sub.neg_hi[1] = true;
         break;
      }

      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      if (dst.regClass() == v2b) {
         if (src1.type() == RegType::vgpr || src0.type() != RegType::vgpr)
            emit_vop2_instruction(ctx, instr, aco_opcode::v_sub_f16, dst, false);
         else
            emit_vop2_instruction(ctx, instr, aco_opcode::v_subrev_f16, dst, true);
      } else if (dst.regClass() == v1) {
         if (src1.type() == RegType::vgpr || src0.type() != RegType::vgpr)
            emit_vop2_instruction(ctx, instr, aco_opcode::v_sub_f32, dst, false);
         else
            emit_vop2_instruction(ctx, instr, aco_opcode::v_subrev_f32, dst, true);
      } else if (dst.regClass() == v2 && ctx->options->gfx_level >= GFX12) {
         Instruction* add =
            bld.vop2_e64(aco_opcode::v_add_f64, Definition(dst), src0, as_vgpr(ctx, src1));
         add->valu().neg[1] = true;
      } else if (dst.regClass() == v2) {
         Instruction* add = bld.vop3(aco_opcode::v_add_f64_e64, Definition(dst), as_vgpr(ctx, src0),
                                     as_vgpr(ctx, src1));
         add->valu().neg[1] = true;
      } else if (dst.regClass() == s1 && instr->def.bit_size == 16) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_sub_f16, dst, false);
      } else if (dst.regClass() == s1 && instr->def.bit_size == 32) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_sub_f32, dst, false);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ffma: {
      if (dst.regClass() == v2b) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_fma_f16, dst, false, 3);
      } else if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         assert(instr->def.num_components == 2);

         Temp src0 = as_vgpr(ctx, get_alu_src_vop3p(ctx, instr->src[0]));
         Temp src1 = as_vgpr(ctx, get_alu_src_vop3p(ctx, instr->src[1]));
         Temp src2 = as_vgpr(ctx, get_alu_src_vop3p(ctx, instr->src[2]));

         /* swizzle to opsel: all swizzles are either 0 (x) or 1 (y) */
         unsigned opsel_lo = 0, opsel_hi = 0;
         for (unsigned i = 0; i < 3; i++) {
            opsel_lo |= (instr->src[i].swizzle[0] & 1) << i;
            opsel_hi |= (instr->src[i].swizzle[1] & 1) << i;
         }

         bld.vop3p(aco_opcode::v_pk_fma_f16, Definition(dst), src0, src1, src2, opsel_lo, opsel_hi);
         emit_split_vector(ctx, dst, 2);
      } else if (dst.regClass() == v1) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_fma_f32, dst,
                                ctx->block->fp_mode.must_flush_denorms32, 3);
      } else if (dst.regClass() == v2) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_fma_f64, dst, false, 3);
      } else if (dst.regClass() == s1) {
         Temp src0 = get_alu_src(ctx, instr->src[0]);
         Temp src1 = get_alu_src(ctx, instr->src[1]);
         Temp src2 = get_alu_src(ctx, instr->src[2]);
         aco_opcode op =
            instr->def.bit_size == 16 ? aco_opcode::s_fmac_f16 : aco_opcode::s_fmac_f32;
         bld.sop2(op, Definition(dst), src0, src1, src2);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ffmaz: {
      if (dst.regClass() == v1) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_fma_legacy_f32, dst,
                                ctx->block->fp_mode.must_flush_denorms32, 3);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fmax: {
      if (dst.regClass() == v2b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_max_f16, dst, true, false,
                               ctx->block->fp_mode.must_flush_denorms16_64);
      } else if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_max_f16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_max_f32, dst, true, false,
                               ctx->block->fp_mode.must_flush_denorms32);
      } else if (dst.regClass() == v2 && ctx->options->gfx_level >= GFX12) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_max_f64, dst, true);
      } else if (dst.regClass() == v2) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_max_f64_e64, dst,
                                ctx->block->fp_mode.must_flush_denorms16_64);
      } else if (dst.regClass() == s1 && instr->def.bit_size == 16) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_max_f16, dst, false);
      } else if (dst.regClass() == s1 && instr->def.bit_size == 32) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_max_f32, dst, false);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fmin: {
      if (dst.regClass() == v2b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_min_f16, dst, true, false,
                               ctx->block->fp_mode.must_flush_denorms16_64);
      } else if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_min_f16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_min_f32, dst, true, false,
                               ctx->block->fp_mode.must_flush_denorms32);
      } else if (dst.regClass() == v2 && ctx->options->gfx_level >= GFX12) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_min_f64, dst, true);
      } else if (dst.regClass() == v2) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_min_f64_e64, dst,
                                ctx->block->fp_mode.must_flush_denorms16_64);
      } else if (dst.regClass() == s1 && instr->def.bit_size == 16) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_min_f16, dst, false);
      } else if (dst.regClass() == s1 && instr->def.bit_size == 32) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_min_f32, dst, false);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_sdot_4x8_iadd: {
      if (ctx->options->gfx_level >= GFX11)
         emit_idot_instruction(ctx, instr, aco_opcode::v_dot4_i32_iu8, dst, false, 0x3);
      else
         emit_idot_instruction(ctx, instr, aco_opcode::v_dot4_i32_i8, dst, false);
      break;
   }
   case nir_op_sdot_4x8_iadd_sat: {
      if (ctx->options->gfx_level >= GFX11)
         emit_idot_instruction(ctx, instr, aco_opcode::v_dot4_i32_iu8, dst, true, 0x3);
      else
         emit_idot_instruction(ctx, instr, aco_opcode::v_dot4_i32_i8, dst, true);
      break;
   }
   case nir_op_sudot_4x8_iadd: {
      emit_idot_instruction(ctx, instr, aco_opcode::v_dot4_i32_iu8, dst, false, 0x1);
      break;
   }
   case nir_op_sudot_4x8_iadd_sat: {
      emit_idot_instruction(ctx, instr, aco_opcode::v_dot4_i32_iu8, dst, true, 0x1);
      break;
   }
   case nir_op_udot_4x8_uadd: {
      emit_idot_instruction(ctx, instr, aco_opcode::v_dot4_u32_u8, dst, false);
      break;
   }
   case nir_op_udot_4x8_uadd_sat: {
      emit_idot_instruction(ctx, instr, aco_opcode::v_dot4_u32_u8, dst, true);
      break;
   }
   case nir_op_sdot_2x16_iadd: {
      emit_idot_instruction(ctx, instr, aco_opcode::v_dot2_i32_i16, dst, false);
      break;
   }
   case nir_op_sdot_2x16_iadd_sat: {
      emit_idot_instruction(ctx, instr, aco_opcode::v_dot2_i32_i16, dst, true);
      break;
   }
   case nir_op_udot_2x16_uadd: {
      emit_idot_instruction(ctx, instr, aco_opcode::v_dot2_u32_u16, dst, false);
      break;
   }
   case nir_op_udot_2x16_uadd_sat: {
      emit_idot_instruction(ctx, instr, aco_opcode::v_dot2_u32_u16, dst, true);
      break;
   }
   case nir_op_bfdot2_bfadd: {
      Temp src0 = as_vgpr(ctx, get_alu_src(ctx, instr->src[0], 2));
      Temp src1 = as_vgpr(ctx, get_alu_src(ctx, instr->src[1], 2));
      Temp src2 = get_alu_src(ctx, instr->src[2], 1);

      bld.vop3(aco_opcode::v_dot2_bf16_bf16, Definition(dst), src0, src1, src2);
      break;
   }
   case nir_op_cube_amd: {
      Temp in = get_ssa_temp(ctx, instr->src[0].src.ssa);
      Temp src[3];
      for (unsigned i = 0; i < 3; i++)
         src[i] = emit_extract_vector(ctx, in, instr->src[0].swizzle[i], v1);

      Temp ma = bld.vop3(aco_opcode::v_cubema_f32, bld.def(v1), src[0], src[1], src[2]);
      Temp sc = bld.vop3(aco_opcode::v_cubesc_f32, bld.def(v1), src[0], src[1], src[2]);
      Temp tc = bld.vop3(aco_opcode::v_cubetc_f32, bld.def(v1), src[0], src[1], src[2]);
      Temp id = bld.vop3(aco_opcode::v_cubeid_f32, bld.def(v1), src[0], src[1], src[2]);
      bld.pseudo(aco_opcode::p_create_vector, Definition(dst), tc, sc, ma, id);
      break;
   }
   case nir_op_bcsel: {
      emit_bcsel(ctx, instr, dst);
      break;
   }
   case nir_op_frsq: {
      if (instr->def.bit_size == 16) {
         if (dst.regClass() == s1 && ctx->program->gfx_level >= GFX12)
            bld.vop3(aco_opcode::v_s_rsq_f16, Definition(dst), get_alu_src(ctx, instr->src[0]));
         else
            emit_vop1_instruction(ctx, instr, aco_opcode::v_rsq_f16, dst);
      } else if (instr->def.bit_size == 32) {
         emit_rsq(ctx, bld, Definition(dst), get_alu_src(ctx, instr->src[0]));
      } else if (instr->def.bit_size == 64) {
         /* Lowered at NIR level for precision reasons. */
         emit_vop1_instruction(ctx, instr, aco_opcode::v_rsq_f64, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fneg: {
      if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         Temp src = as_vgpr(ctx, get_alu_src_vop3p(ctx, instr->src[0]));
         Instruction* vop3p =
            bld.vop3p(aco_opcode::v_pk_mul_f16, Definition(dst), src, Operand::c16(0x3C00),
                      instr->src[0].swizzle[0] & 1, instr->src[0].swizzle[1] & 1);
         vop3p->valu().neg_lo[0] = true;
         vop3p->valu().neg_hi[0] = true;
         emit_split_vector(ctx, dst, 2);
         break;
      }
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == v2b) {
         bld.vop2(aco_opcode::v_mul_f16, Definition(dst), Operand::c16(0xbc00u), as_vgpr(ctx, src));
      } else if (dst.regClass() == v1) {
         bld.vop2(aco_opcode::v_mul_f32, Definition(dst), Operand::c32(0xbf800000u),
                  as_vgpr(ctx, src));
      } else if (dst.regClass() == v2 && ctx->program->gfx_level >= GFX12) {
         bld.vop2(aco_opcode::v_mul_f64, Definition(dst), Operand::c64(0xBFF0000000000000),
                  as_vgpr(ctx, src));
      } else if (dst.regClass() == v2) {
         bld.vop3(aco_opcode::v_mul_f64_e64, Definition(dst), Operand::c64(0xBFF0000000000000),
                  as_vgpr(ctx, src));
      } else if (dst.regClass() == s1 && instr->def.bit_size == 16) {
         bld.sop2(aco_opcode::s_mul_f16, Definition(dst), Operand::c16(0xbc00u), src);
      } else if (dst.regClass() == s1 && instr->def.bit_size == 32) {
         bld.sop2(aco_opcode::s_mul_f32, Definition(dst), Operand::c32(0xbf800000u), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fabs: {
      if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         Temp src = get_alu_src_vop3p(ctx, instr->src[0]);
         Instruction* vop3p =
            bld.vop3p(aco_opcode::v_pk_max_f16, Definition(dst), src, src,
                      instr->src[0].swizzle[0] & 1 ? 3 : 0, instr->src[0].swizzle[1] & 1 ? 3 : 0)
               .instr;
         vop3p->valu().neg_lo[1] = true;
         vop3p->valu().neg_hi[1] = true;
         emit_split_vector(ctx, dst, 2);
         break;
      }
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == v2b) {
         Instruction* mul = bld.vop2_e64(aco_opcode::v_mul_f16, Definition(dst),
                                         Operand::c16(0x3c00), as_vgpr(ctx, src))
                               .instr;
         mul->valu().abs[1] = true;
      } else if (dst.regClass() == v1) {
         Instruction* mul = bld.vop2_e64(aco_opcode::v_mul_f32, Definition(dst),
                                         Operand::c32(0x3f800000u), as_vgpr(ctx, src))
                               .instr;
         mul->valu().abs[1] = true;
      } else if (dst.regClass() == v2 && ctx->program->gfx_level >= GFX12) {
         Instruction* mul = bld.vop2_e64(aco_opcode::v_mul_f64, Definition(dst),
                                         Operand::c64(0x3FF0000000000000), as_vgpr(ctx, src));
         mul->valu().abs[1] = true;
      } else if (dst.regClass() == v2) {
         Instruction* mul = bld.vop3(aco_opcode::v_mul_f64_e64, Definition(dst),
                                     Operand::c64(0x3FF0000000000000), as_vgpr(ctx, src));
         mul->valu().abs[1] = true;
      } else if (dst.regClass() == s1 && instr->def.bit_size == 16) {
         Temp mask = bld.copy(bld.def(s1), Operand::c32(0x7fff));
         if (ctx->block->fp_mode.denorm16_64 == fp_denorm_keep) {
            bld.sop2(aco_opcode::s_and_b32, Definition(dst), bld.def(s1, scc), mask, src);
         } else {
            Temp tmp = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), mask, src);
            bld.sop2(aco_opcode::s_mul_f16, Definition(dst), Operand::c16(0x3c00), tmp);
         }
      } else if (dst.regClass() == s1 && instr->def.bit_size == 32) {
         Temp mask = bld.copy(bld.def(s1), Operand::c32(0x7fffffff));
         if (ctx->block->fp_mode.denorm32 == fp_denorm_keep) {
            bld.sop2(aco_opcode::s_and_b32, Definition(dst), bld.def(s1, scc), mask, src);
         } else {
            Temp tmp = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), mask, src);
            bld.sop2(aco_opcode::s_mul_f32, Definition(dst), Operand::c32(0x3f800000), tmp);
         }
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fsat: {
      if (dst.regClass() == v1 && instr->def.bit_size == 16) {
         Temp src = get_alu_src_vop3p(ctx, instr->src[0]);
         Instruction* vop3p =
            bld.vop3p(aco_opcode::v_pk_mul_f16, Definition(dst), src, Operand::c16(0x3C00),
                      instr->src[0].swizzle[0] & 1, instr->src[0].swizzle[1] & 1);
         vop3p->valu().clamp = true;
         emit_split_vector(ctx, dst, 2);
         break;
      }
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == v2b && ctx->program->gfx_level >= GFX9) {
         bld.vop3(aco_opcode::v_med3_f16, Definition(dst), Operand::c16(0u), Operand::c16(0x3c00),
                  src);
      } else if (dst.regClass() == v2b) {
         bld.vop2_e64(aco_opcode::v_mul_f16, Definition(dst), Operand::c16(0x3c00), src)
            ->valu()
            .clamp = true;
      } else if (dst.regClass() == v1) {
         bld.vop3(aco_opcode::v_med3_f32, Definition(dst), Operand::zero(),
                  Operand::c32(0x3f800000u), src);
         /* apparently, it is not necessary to flush denorms if this instruction is used with these
          * operands */
         // TODO: confirm that this holds under any circumstances
      } else if (dst.regClass() == v2 && ctx->program->gfx_level >= GFX12) {
         Instruction* mul = bld.vop2_e64(aco_opcode::v_mul_f64, Definition(dst), src,
                                         Operand::c64(0x3FF0000000000000));
         mul->valu().clamp = true;
      } else if (dst.regClass() == v2) {
         Instruction* mul = bld.vop3(aco_opcode::v_mul_f64_e64, Definition(dst), src,
                                     Operand::c64(0x3FF0000000000000));
         mul->valu().clamp = true;
      } else if (dst.regClass() == s1 && instr->def.bit_size == 16) {
         Temp low = bld.sop2(aco_opcode::s_max_f16, bld.def(s1), src, Operand::c16(0));
         bld.sop2(aco_opcode::s_min_f16, Definition(dst), low, Operand::c16(0x3C00));
      } else if (dst.regClass() == s1 && instr->def.bit_size == 32) {
         Temp low = bld.sop2(aco_opcode::s_max_f32, bld.def(s1), src, Operand::c32(0));
         bld.sop2(aco_opcode::s_min_f32, Definition(dst), low, Operand::c32(0x3f800000));
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_flog2: {
      if (instr->def.bit_size == 16) {
         if (dst.regClass() == s1 && ctx->program->gfx_level >= GFX12)
            bld.vop3(aco_opcode::v_s_log_f16, Definition(dst), get_alu_src(ctx, instr->src[0]));
         else
            emit_vop1_instruction(ctx, instr, aco_opcode::v_log_f16, dst);
      } else if (instr->def.bit_size == 32) {
         emit_log2(ctx, bld, Definition(dst), get_alu_src(ctx, instr->src[0]));
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_frcp: {
      if (instr->def.bit_size == 16) {
         if (dst.regClass() == s1 && ctx->program->gfx_level >= GFX12)
            bld.vop3(aco_opcode::v_s_rcp_f16, Definition(dst), get_alu_src(ctx, instr->src[0]));
         else
            emit_vop1_instruction(ctx, instr, aco_opcode::v_rcp_f16, dst);
      } else if (instr->def.bit_size == 32) {
         emit_rcp(ctx, bld, Definition(dst), get_alu_src(ctx, instr->src[0]));
      } else if (instr->def.bit_size == 64) {
         /* Lowered at NIR level for precision reasons. */
         emit_vop1_instruction(ctx, instr, aco_opcode::v_rcp_f64, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fexp2: {
      if (dst.regClass() == s1 && ctx->options->gfx_level >= GFX12) {
         aco_opcode opcode =
            instr->def.bit_size == 16 ? aco_opcode::v_s_exp_f16 : aco_opcode::v_s_exp_f32;
         bld.vop3(opcode, Definition(dst), get_alu_src(ctx, instr->src[0]));
      } else if (instr->def.bit_size == 16) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_exp_f16, dst);
      } else if (instr->def.bit_size == 32) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_exp_f32, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fsqrt: {
      if (instr->def.bit_size == 16) {
         if (dst.regClass() == s1 && ctx->program->gfx_level >= GFX12)
            bld.vop3(aco_opcode::v_s_sqrt_f16, Definition(dst), get_alu_src(ctx, instr->src[0]));
         else
            emit_vop1_instruction(ctx, instr, aco_opcode::v_sqrt_f16, dst);
      } else if (instr->def.bit_size == 32) {
         emit_sqrt(ctx, bld, Definition(dst), get_alu_src(ctx, instr->src[0]));
      } else if (instr->def.bit_size == 64) {
         /* Lowered at NIR level for precision reasons. */
         emit_vop1_instruction(ctx, instr, aco_opcode::v_sqrt_f64, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ffract: {
      if (dst.regClass() == v2b) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_fract_f16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_fract_f32, dst);
      } else if (dst.regClass() == v2) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_fract_f64, dst);
      } else if (dst.regClass() == s1) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         aco_opcode op =
            instr->def.bit_size == 16 ? aco_opcode::s_floor_f16 : aco_opcode::s_floor_f32;
         Temp floor = bld.sop1(op, bld.def(s1), src);
         op = instr->def.bit_size == 16 ? aco_opcode::s_sub_f16 : aco_opcode::s_sub_f32;
         bld.sop2(op, Definition(dst), src, floor);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ffloor: {
      if (dst.regClass() == v2b) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_floor_f16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_floor_f32, dst);
      } else if (dst.regClass() == v2) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         emit_floor_f64(ctx, bld, Definition(dst), src);
      } else if (dst.regClass() == s1) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         aco_opcode op =
            instr->def.bit_size == 16 ? aco_opcode::s_floor_f16 : aco_opcode::s_floor_f32;
         bld.sop1(op, Definition(dst), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fceil: {
      if (dst.regClass() == v2b) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_ceil_f16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_ceil_f32, dst);
      } else if (dst.regClass() == v2) {
         if (ctx->options->gfx_level >= GFX7) {
            emit_vop1_instruction(ctx, instr, aco_opcode::v_ceil_f64, dst);
         } else {
            /* GFX6 doesn't support V_CEIL_F64, lower it. */
            /* trunc = trunc(src0)
             * if (src0 > 0.0 && src0 != trunc)
             *    trunc += 1.0
             */
            Temp src0 = get_alu_src(ctx, instr->src[0]);
            Temp trunc = emit_trunc_f64(ctx, bld, bld.def(v2), src0);
            Temp tmp0 =
               bld.vopc_e64(aco_opcode::v_cmp_gt_f64, bld.def(bld.lm), src0, Operand::zero());
            Temp tmp1 = bld.vopc(aco_opcode::v_cmp_lg_f64, bld.def(bld.lm), src0, trunc);
            Temp cond = bld.sop2(aco_opcode::s_and_b64, bld.def(s2), bld.def(s1, scc), tmp0, tmp1);
            Temp add = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1),
                                bld.copy(bld.def(v1), Operand::zero()),
                                bld.copy(bld.def(v1), Operand::c32(0x3ff00000u)), cond);
            add = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2),
                             bld.copy(bld.def(v1), Operand::zero()), add);
            bld.vop3(aco_opcode::v_add_f64_e64, Definition(dst), trunc, add);
         }
      } else if (dst.regClass() == s1) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         aco_opcode op =
            instr->def.bit_size == 16 ? aco_opcode::s_ceil_f16 : aco_opcode::s_ceil_f32;
         bld.sop1(op, Definition(dst), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ftrunc: {
      if (dst.regClass() == v2b) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_trunc_f16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_trunc_f32, dst);
      } else if (dst.regClass() == v2) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         emit_trunc_f64(ctx, bld, Definition(dst), src);
      } else if (dst.regClass() == s1) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         aco_opcode op =
            instr->def.bit_size == 16 ? aco_opcode::s_trunc_f16 : aco_opcode::s_trunc_f32;
         bld.sop1(op, Definition(dst), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fround_even: {
      if (dst.regClass() == v2b) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_rndne_f16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_rndne_f32, dst);
      } else if (dst.regClass() == v2) {
         if (ctx->options->gfx_level >= GFX7) {
            emit_vop1_instruction(ctx, instr, aco_opcode::v_rndne_f64, dst);
         } else {
            /* GFX6 doesn't support V_RNDNE_F64, lower it. */
            Temp src0_lo = bld.tmp(v1), src0_hi = bld.tmp(v1);
            Temp src0 = get_alu_src(ctx, instr->src[0]);
            bld.pseudo(aco_opcode::p_split_vector, Definition(src0_lo), Definition(src0_hi), src0);

            Temp bitmask = bld.sop1(aco_opcode::s_brev_b32, bld.def(s1),
                                    bld.copy(bld.def(s1), Operand::c32(-2u)));
            Temp bfi =
               bld.vop3(aco_opcode::v_bfi_b32, bld.def(v1), bitmask,
                        bld.copy(bld.def(v1), Operand::c32(0x43300000u)), as_vgpr(ctx, src0_hi));
            Temp tmp =
               bld.vop3(aco_opcode::v_add_f64_e64, bld.def(v2), src0,
                        bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), Operand::zero(), bfi));
            Instruction* sub =
               bld.vop3(aco_opcode::v_add_f64_e64, bld.def(v2), tmp,
                        bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), Operand::zero(), bfi));
            sub->valu().neg[1] = true;
            tmp = sub->definitions[0].getTemp();

            Temp v = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), Operand::c32(-1u),
                                Operand::c32(0x432fffffu));
            Instruction* vop3 = bld.vopc_e64(aco_opcode::v_cmp_gt_f64, bld.def(bld.lm), src0, v);
            vop3->valu().abs[0] = true;
            Temp cond = vop3->definitions[0].getTemp();

            Temp tmp_lo = bld.tmp(v1), tmp_hi = bld.tmp(v1);
            bld.pseudo(aco_opcode::p_split_vector, Definition(tmp_lo), Definition(tmp_hi), tmp);
            Temp dst0 = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), tmp_lo,
                                     as_vgpr(ctx, src0_lo), cond);
            Temp dst1 = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), tmp_hi,
                                     as_vgpr(ctx, src0_hi), cond);

            bld.pseudo(aco_opcode::p_create_vector, Definition(dst), dst0, dst1);
         }
      } else if (dst.regClass() == s1) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         aco_opcode op =
            instr->def.bit_size == 16 ? aco_opcode::s_rndne_f16 : aco_opcode::s_rndne_f32;
         bld.sop1(op, Definition(dst), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fsin_amd:
   case nir_op_fcos_amd: {
      if (instr->def.bit_size == 16 || instr->def.bit_size == 32) {
         bool is_sin = instr->op == nir_op_fsin_amd;
         aco_opcode opcode, fract;
         RegClass rc;
         if (instr->def.bit_size == 16) {
            opcode = is_sin ? aco_opcode::v_sin_f16 : aco_opcode::v_cos_f16;
            fract = aco_opcode::v_fract_f16;
            rc = v2b;
         } else {
            opcode = is_sin ? aco_opcode::v_sin_f32 : aco_opcode::v_cos_f32;
            fract = aco_opcode::v_fract_f32;
            rc = v1;
         }

         Temp src = get_alu_src(ctx, instr->src[0]);
         /* before GFX9, v_sin and v_cos had a valid input domain of [-256, +256] */
         if (ctx->options->gfx_level < GFX9)
            src = bld.vop1(fract, bld.def(rc), src);

         if (dst.regClass() == rc) {
            bld.vop1(opcode, Definition(dst), src);
         } else {
            Temp tmp = bld.vop1(opcode, bld.def(rc), src);
            bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), tmp);
         }
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ldexp: {
      if (dst.regClass() == v2b) {
         nir_scalar scalar = nir_get_scalar(&instr->def, 0);
         scalar = nir_scalar_chase_alu_src(scalar, 1);

         Temp exp;

         /* Convert the exponent to 16bit int with saturation. */
         if (nir_scalar_is_const(scalar)) {
            int16_t clamped = MIN2(MAX2(nir_scalar_as_int(scalar), INT16_MIN), INT16_MAX);
            exp = bld.copy(bld.def(v2b), Operand::c16(clamped));
         } else {
            exp = get_alu_src(ctx, instr->src[1]);
            exp = bld.vop3(aco_opcode::v_cvt_pk_i16_i32, bld.def(v2b), exp, Operand::c32(0));
         }

         bld.vop2(aco_opcode::v_ldexp_f16, Definition(dst), get_alu_src(ctx, instr->src[0]), exp);
      } else if (dst.regClass() == v1) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_ldexp_f32, dst);
      } else if (dst.regClass() == v2) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_ldexp_f64, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_frexp_sig: {
      if (dst.regClass() == v2b) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_frexp_mant_f16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_frexp_mant_f32, dst);
      } else if (dst.regClass() == v2) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_frexp_mant_f64, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_frexp_exp: {
      if (instr->src[0].src.ssa->bit_size == 16) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         Temp tmp = bld.vop1(aco_opcode::v_frexp_exp_i16_f16, bld.def(v1), src);
         tmp = bld.pseudo(aco_opcode::p_extract_vector, bld.def(v1b), tmp, Operand::zero());
         convert_int(ctx, bld, tmp, 8, 32, true, dst);
      } else if (instr->src[0].src.ssa->bit_size == 32) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_frexp_exp_i32_f32, dst);
      } else if (instr->src[0].src.ssa->bit_size == 64) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_frexp_exp_i32_f64, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fsign: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == v2b) {
         /* replace negative zero with positive zero */
         src = bld.vop2(aco_opcode::v_add_f16, bld.def(v2b), Operand::zero(), as_vgpr(ctx, src));
         if (ctx->program->gfx_level >= GFX9) {
            src = bld.vop3(aco_opcode::v_med3_i16, bld.def(v2b), Operand::c16(-1), src,
                           Operand::c16(1u));
            bld.vop1(aco_opcode::v_cvt_f16_i16, Definition(dst), src);
         } else {
            src = convert_int(ctx, bld, src, 16, 32, true);
            src = bld.vop3(aco_opcode::v_med3_i32, bld.def(v1), Operand::c32(-1), src,
                           Operand::c32(1u));
            bld.vop1(aco_opcode::v_cvt_f16_i16, Definition(dst), src);
         }
      } else if (dst.regClass() == v1) {
         /* Legacy multiply with +Inf means +-0.0 becomes +0.0 and all other numbers
          * the correctly signed Inf. After that, we only need to clamp between -1.0 and +1.0.
          */
         Temp inf = bld.copy(bld.def(s1), Operand::c32(0x7f800000));
         src = bld.vop2(aco_opcode::v_mul_legacy_f32, bld.def(v1), inf, as_vgpr(ctx, src));
         bld.vop3(aco_opcode::v_med3_f32, Definition(dst), Operand::c32(0x3f800000), src,
                  Operand::c32(0xbf800000));
      } else if (dst.regClass() == v2) {
         src = as_vgpr(ctx, src);
         Temp cond = bld.vopc(aco_opcode::v_cmp_nlt_f64, bld.def(bld.lm), Operand::zero(), src);
         Temp tmp = bld.copy(bld.def(v1), Operand::c32(0x3FF00000u));
         Temp upper = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), tmp,
                                   emit_extract_vector(ctx, src, 1, v1), cond);

         cond = bld.vopc(aco_opcode::v_cmp_le_f64, bld.def(bld.lm), Operand::zero(), src);
         tmp = bld.copy(bld.def(v1), Operand::c32(0xBFF00000u));
         upper = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), tmp, upper, cond);

         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), Operand::zero(), upper);
      } else if (dst.regClass() == s1 && instr->def.bit_size == 16) {
         Temp cond = bld.sopc(aco_opcode::s_cmp_lt_f16, bld.def(s1, scc), Operand::c16(0), src);
         src = bld.sop2(aco_opcode::s_cselect_b32, bld.def(s1), Operand::c32(0x3c00), src,
                        bld.scc(cond));
         cond = bld.sopc(aco_opcode::s_cmp_ge_f16, bld.def(s1, scc), src, Operand::c16(0));
         bld.sop2(aco_opcode::s_cselect_b32, Definition(dst), src, Operand::c32(0xbc00),
                  bld.scc(cond));
      } else if (dst.regClass() == s1 && instr->def.bit_size == 32) {
         Temp cond = bld.sopc(aco_opcode::s_cmp_lt_f32, bld.def(s1, scc), Operand::c32(0), src);
         src = bld.sop2(aco_opcode::s_cselect_b32, bld.def(s1), Operand::c32(0x3f800000), src,
                        bld.scc(cond));
         cond = bld.sopc(aco_opcode::s_cmp_ge_f32, bld.def(s1, scc), src, Operand::c32(0));
         bld.sop2(aco_opcode::s_cselect_b32, Definition(dst), src, Operand::c32(0xbf800000),
                  bld.scc(cond));
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_f2f16:
   case nir_op_f2f16_rtne: {
      assert(instr->src[0].src.ssa->bit_size == 32);
      if (instr->def.num_components == 2) {
         /* Vectorizing f2f16 is only possible with rtz. */
         assert(instr->op != nir_op_f2f16_rtne);
         assert(ctx->block->fp_mode.round16_64 == fp_round_tz ||
                !ctx->block->fp_mode.care_about_round16_64);
         emit_vec2_f2f16(ctx, instr, dst);
         break;
      }
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (instr->op == nir_op_f2f16_rtne && ctx->block->fp_mode.round16_64 != fp_round_ne) {
         /* We emit s_round_mode/s_setreg_imm32 in insert_fp_mode to
          * keep value numbering and scheduling simpler.
          */
         ctx->program->needs_fp_mode_insertion = true;
         if (dst.regClass() == v2b)
            bld.vop1(aco_opcode::p_v_cvt_f16_f32_rtne, Definition(dst), src);
         else
            bld.sop1(aco_opcode::p_s_cvt_f16_f32_rtne, Definition(dst), src);
      } else {
         if (dst.regClass() == v2b)
            bld.vop1(aco_opcode::v_cvt_f16_f32, Definition(dst), src);
         else
            bld.sop1(aco_opcode::s_cvt_f16_f32, Definition(dst), src);
      }
      break;
   }
   case nir_op_f2f16_rtz: {
      assert(instr->src[0].src.ssa->bit_size == 32);
      if (instr->def.num_components == 2) {
         emit_vec2_f2f16(ctx, instr, dst);
         break;
      }
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (ctx->block->fp_mode.round16_64 == fp_round_tz) {
         if (dst.regClass() == v2b)
            bld.vop1(aco_opcode::v_cvt_f16_f32, Definition(dst), src);
         else
            bld.sop1(aco_opcode::s_cvt_f16_f32, Definition(dst), src);
      } else if (dst.regClass() == s1) {
         bld.sop2(aco_opcode::s_cvt_pk_rtz_f16_f32, Definition(dst), src, Operand::zero());
      } else if (ctx->program->gfx_level == GFX8 || ctx->program->gfx_level == GFX9) {
         bld.vop3(aco_opcode::v_cvt_pkrtz_f16_f32_e64, Definition(dst), src, Operand::zero());
      } else {
         bld.vop2(aco_opcode::v_cvt_pkrtz_f16_f32, Definition(dst), src, as_vgpr(ctx, src));
      }
      break;
   }
   case nir_op_f2f32: {
      if (dst.regClass() == s1) {
         assert(instr->src[0].src.ssa->bit_size == 16);
         Temp src = get_alu_src(ctx, instr->src[0]);
         bld.sop1(aco_opcode::s_cvt_f32_f16, Definition(dst), src);
      } else if (instr->src[0].src.ssa->bit_size == 16) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_f32_f16, dst);
      } else if (instr->src[0].src.ssa->bit_size == 64) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_f32_f64, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_f2f64: {
      assert(instr->src[0].src.ssa->bit_size == 32);
      Temp src = get_alu_src(ctx, instr->src[0]);
      bld.vop1(aco_opcode::v_cvt_f64_f32, Definition(dst), src);
      break;
   }
   case nir_op_f2e4m3fn:
   case nir_op_f2e4m3fn_sat:
   case nir_op_f2e4m3fn_satfn:
   case nir_op_f2e5m2:
   case nir_op_f2e5m2_sat: {
      Operand src[2];
      if (instr->def.num_components == 2) {
         Temp pk_src = get_ssa_temp(ctx, instr->src[0].src.ssa);
         RegClass rc = RegClass(pk_src.regClass().type(), 1);
         for (unsigned i = 0; i < 2; i++)
            src[i] = Operand(emit_extract_vector(ctx, pk_src, instr->src[0].swizzle[i], rc));
      } else {
         assert(instr->def.num_components == 1);
         src[0] = Operand(get_alu_src(ctx, instr->src[0]));
         src[1] = Operand::c32(0);
      }

      /* Ideally we would want to use FP16_OVFL for the sat variants,
       * but the ISA doc is wrong and Inf isn't clamped to max_float.
       */
      bool clamp = instr->op == nir_op_f2e4m3fn_sat || instr->op == nir_op_f2e5m2_sat;
      if (clamp) {
         Temp max_float = bld.copy(
            bld.def(s1), Operand::c32(fui(instr->op == nir_op_f2e4m3fn_sat ? 448.0f : 57344.0f)));

         for (unsigned i = 0; i < instr->def.num_components; i++) {
            /* use minimum variant because it preserves NaN. */
            Instruction* clamped = bld.vop3(aco_opcode::v_minimummaximum_f32, bld.def(v1), src[i],
                                            max_float, max_float);
            clamped->valu().neg[2] = true;
            src[i] = Operand(clamped->definitions[0].getTemp());
         }
      }

      ctx->program->needs_fp_mode_insertion |= instr->op == nir_op_f2e4m3fn_satfn;

      aco_opcode opcode = instr->op == nir_op_f2e4m3fn || instr->op == nir_op_f2e4m3fn_sat
                             ? aco_opcode::v_cvt_pk_fp8_f32
                          : instr->op == nir_op_f2e4m3fn_satfn ? aco_opcode::p_v_cvt_pk_fp8_f32_ovfl
                                                               : aco_opcode::v_cvt_pk_bf8_f32;
      bld.vop3(opcode, Definition(dst), src[0], src[1]);
      if (instr->def.num_components == 2)
         emit_split_vector(ctx, dst, 2);
      break;
   }
   case nir_op_e4m3fn2f: {
      if (instr->def.num_components == 2) {
         Temp src = get_alu_src(ctx, instr->src[0], 2);
         bld.vop1(aco_opcode::v_cvt_pk_f32_fp8, Definition(dst), src);
         emit_split_vector(ctx, dst, 2);
      } else {
         Temp src = get_alu_src(ctx, instr->src[0]);
         assert(instr->def.num_components == 1);
         bld.vop1(aco_opcode::v_cvt_f32_fp8, Definition(dst), src);
      }
      break;
   }
   case nir_op_e5m22f: {
      if (instr->def.num_components == 2) {
         Temp src = get_alu_src(ctx, instr->src[0], 2);
         bld.vop1(aco_opcode::v_cvt_pk_f32_bf8, Definition(dst), src);
         emit_split_vector(ctx, dst, 2);
      } else {
         Temp src = get_alu_src(ctx, instr->src[0]);
         assert(instr->def.num_components == 1);
         bld.vop1(aco_opcode::v_cvt_f32_bf8, Definition(dst), src);
      }
      break;
   }
   case nir_op_i2f16: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      const unsigned input_size = instr->src[0].src.ssa->bit_size;
      if (dst.regClass() == v2b) {
         if (input_size <= 16) {
            /* Expand integer to the size expected by the uint→float converter used below */
            unsigned target_size = (ctx->program->gfx_level >= GFX8 ? 16 : 32);
            if (input_size != target_size) {
               src = convert_int(ctx, bld, src, input_size, target_size, true);
            }
         }

         if (ctx->program->gfx_level >= GFX8 && input_size <= 16) {
            bld.vop1(aco_opcode::v_cvt_f16_i16, Definition(dst), src);
         } else {
            /* Large 32bit inputs need to return +-inf/FLOAT_MAX.
             *
             * This is also the fallback-path taken on GFX7 and earlier, which
             * do not support direct f16⟷i16 conversions.
             */
            src = bld.vop1(aco_opcode::v_cvt_f32_i32, bld.def(v1), src);
            bld.vop1(aco_opcode::v_cvt_f16_f32, Definition(dst), src);
         }
      } else if (dst.regClass() == s1) {
         if (input_size <= 16) {
            src = convert_int(ctx, bld, src, input_size, 32, true);
         }
         src = bld.sop1(aco_opcode::s_cvt_f32_i32, bld.def(s1), src);
         bld.sop1(aco_opcode::s_cvt_f16_f32, Definition(dst), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_i2f32: {
      assert(dst.size() == 1);
      Temp src = get_alu_src(ctx, instr->src[0]);
      const unsigned input_size = instr->src[0].src.ssa->bit_size;
      if (input_size <= 32) {
         if (input_size <= 16) {
            /* Sign-extend to 32-bits */
            src = convert_int(ctx, bld, src, input_size, 32, true);
         }
         if (dst.regClass() == v1)
            bld.vop1(aco_opcode::v_cvt_f32_i32, Definition(dst), src);
         else
            bld.sop1(aco_opcode::s_cvt_f32_i32, Definition(dst), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_i2f64: {
      if (instr->src[0].src.ssa->bit_size <= 32) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         if (instr->src[0].src.ssa->bit_size <= 16)
            src = convert_int(ctx, bld, src, instr->src[0].src.ssa->bit_size, 32, true);
         bld.vop1(aco_opcode::v_cvt_f64_i32, Definition(dst), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_u2f16: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      const unsigned input_size = instr->src[0].src.ssa->bit_size;
      if (dst.regClass() == v2b) {
         if (input_size <= 16) {
            /* Expand integer to the size expected by the uint→float converter used below */
            unsigned target_size = (ctx->program->gfx_level >= GFX8 ? 16 : 32);
            if (input_size != target_size) {
               src = convert_int(ctx, bld, src, input_size, target_size, false);
            }
         }

         if (ctx->program->gfx_level >= GFX8 && input_size <= 16) {
            bld.vop1(aco_opcode::v_cvt_f16_u16, Definition(dst), src);
         } else {
            /* Large 32bit inputs need to return inf/FLOAT_MAX.
             *
             * This is also the fallback-path taken on GFX7 and earlier, which
             * do not support direct f16⟷u16 conversions.
             */
            src = bld.vop1(aco_opcode::v_cvt_f32_u32, bld.def(v1), src);
            bld.vop1(aco_opcode::v_cvt_f16_f32, Definition(dst), src);
         }
      } else if (dst.regClass() == s1) {
         if (input_size <= 16) {
            src = convert_int(ctx, bld, src, input_size, 32, false);
         }
         src = bld.sop1(aco_opcode::s_cvt_f32_u32, bld.def(s1), src);
         bld.sop1(aco_opcode::s_cvt_f16_f32, Definition(dst), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_u2f32: {
      assert(dst.size() == 1);
      Temp src = get_alu_src(ctx, instr->src[0]);
      const unsigned input_size = instr->src[0].src.ssa->bit_size;
      if (input_size == 8 && dst.regClass() == v1) {
         bld.vop1(aco_opcode::v_cvt_f32_ubyte0, Definition(dst), src);
      } else if (input_size <= 32) {
         if (input_size <= 16)
            src = convert_int(ctx, bld, src, instr->src[0].src.ssa->bit_size, 32, false);
         if (dst.regClass() == v1)
            bld.vop1(aco_opcode::v_cvt_f32_u32, Definition(dst), src);
         else
            bld.sop1(aco_opcode::s_cvt_f32_u32, Definition(dst), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_u2f64: {
      if (instr->src[0].src.ssa->bit_size <= 32) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         if (instr->src[0].src.ssa->bit_size <= 16)
            src = convert_int(ctx, bld, src, instr->src[0].src.ssa->bit_size, 32, false);
         bld.vop1(aco_opcode::v_cvt_f64_u32, Definition(dst), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_f2i8:
   case nir_op_f2i16: {
      if (instr->src[0].src.ssa->bit_size <= 32 && dst.regClass() == s1 &&
          ctx->program->gfx_level >= GFX11_5) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         Temp tmp = bld.as_uniform(src);
         if (instr->src[0].src.ssa->bit_size == 16)
            tmp = bld.sop1(aco_opcode::s_cvt_f32_f16, bld.def(s1), tmp);
         bld.sop1(aco_opcode::s_cvt_i32_f32, Definition(dst), tmp);
      } else if (instr->src[0].src.ssa->bit_size == 16) {
         if (ctx->program->gfx_level >= GFX8) {
            emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_i16_f16, dst);
         } else {
            /* GFX7 and earlier do not support direct f16⟷i16 conversions */
            Temp tmp = bld.tmp(v1);
            emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_f32_f16, tmp);
            tmp = bld.vop1(aco_opcode::v_cvt_i32_f32, bld.def(v1), tmp);
            tmp = convert_int(ctx, bld, tmp, 32, instr->def.bit_size, false,
                              (dst.type() == RegType::sgpr) ? Temp() : dst);
            if (dst.type() == RegType::sgpr) {
               bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), tmp);
            }
         }
      } else if (instr->src[0].src.ssa->bit_size == 32) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_i32_f32, dst);
      } else {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_i32_f64, dst);
      }
      break;
   }
   case nir_op_f2u8:
   case nir_op_f2u16: {
      if (instr->src[0].src.ssa->bit_size <= 32 && dst.regClass() == s1 &&
          ctx->program->gfx_level >= GFX11_5) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         Temp tmp = bld.as_uniform(src);
         if (instr->src[0].src.ssa->bit_size == 16)
            tmp = bld.sop1(aco_opcode::s_cvt_f32_f16, bld.def(s1), tmp);
         bld.sop1(aco_opcode::s_cvt_u32_f32, Definition(dst), tmp);
      } else if (instr->src[0].src.ssa->bit_size == 16) {
         if (ctx->program->gfx_level >= GFX8) {
            emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_u16_f16, dst);
         } else {
            /* GFX7 and earlier do not support direct f16⟷u16 conversions */
            Temp tmp = bld.tmp(v1);
            emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_f32_f16, tmp);
            tmp = bld.vop1(aco_opcode::v_cvt_u32_f32, bld.def(v1), tmp);
            tmp = convert_int(ctx, bld, tmp, 32, instr->def.bit_size, false,
                              (dst.type() == RegType::sgpr) ? Temp() : dst);
            if (dst.type() == RegType::sgpr) {
               bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), tmp);
            }
         }
      } else if (instr->src[0].src.ssa->bit_size == 32) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_u32_f32, dst);
      } else {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_u32_f64, dst);
      }
      break;
   }
   case nir_op_f2i32: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (instr->src[0].src.ssa->bit_size <= 32 && dst.regClass() == s1 &&
          ctx->program->gfx_level >= GFX11_5) {
         Temp tmp = bld.as_uniform(src);
         if (instr->src[0].src.ssa->bit_size == 16)
            tmp = bld.sop1(aco_opcode::s_cvt_f32_f16, bld.def(s1), tmp);
         bld.sop1(aco_opcode::s_cvt_i32_f32, Definition(dst), tmp);
      } else if (instr->src[0].src.ssa->bit_size == 16) {
         Temp tmp = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), src);
         if (dst.type() == RegType::vgpr) {
            bld.vop1(aco_opcode::v_cvt_i32_f32, Definition(dst), tmp);
         } else {
            bld.pseudo(aco_opcode::p_as_uniform, Definition(dst),
                       bld.vop1(aco_opcode::v_cvt_i32_f32, bld.def(v1), tmp));
         }
      } else if (instr->src[0].src.ssa->bit_size == 32) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_i32_f32, dst);
      } else if (instr->src[0].src.ssa->bit_size == 64) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_i32_f64, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_f2u32: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (instr->src[0].src.ssa->bit_size <= 32 && dst.regClass() == s1 &&
          ctx->program->gfx_level >= GFX11_5) {
         Temp tmp = bld.as_uniform(src);
         if (instr->src[0].src.ssa->bit_size == 16)
            tmp = bld.sop1(aco_opcode::s_cvt_f32_f16, bld.def(s1), tmp);
         bld.sop1(aco_opcode::s_cvt_u32_f32, Definition(dst), tmp);
      } else if (instr->src[0].src.ssa->bit_size == 16) {
         Temp tmp = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), src);
         if (dst.type() == RegType::vgpr) {
            bld.vop1(aco_opcode::v_cvt_u32_f32, Definition(dst), tmp);
         } else {
            bld.pseudo(aco_opcode::p_as_uniform, Definition(dst),
                       bld.vop1(aco_opcode::v_cvt_u32_f32, bld.def(v1), tmp));
         }
      } else if (instr->src[0].src.ssa->bit_size == 32) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_u32_f32, dst);
      } else if (instr->src[0].src.ssa->bit_size == 64) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_u32_f64, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_b2f16: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      assert(src.regClass() == bld.lm);

      if (dst.regClass() == s1) {
         src = bool_to_scalar_condition(ctx, src);
         bld.sop2(aco_opcode::s_mul_i32, Definition(dst), Operand::c32(0x3c00u), src);
      } else if (dst.regClass() == v2b) {
         Temp one = bld.copy(bld.def(v1), Operand::c32(0x3c00u));
         bld.vop2(aco_opcode::v_cndmask_b32, Definition(dst), Operand::zero(), one, src);
      } else {
         UNREACHABLE("Wrong destination register class for nir_op_b2f16.");
      }
      break;
   }
   case nir_op_b2f32: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      assert(src.regClass() == bld.lm);

      if (dst.regClass() == s1) {
         src = bool_to_scalar_condition(ctx, src);
         bld.sop2(aco_opcode::s_mul_i32, Definition(dst), Operand::c32(0x3f800000u), src);
      } else if (dst.regClass() == v1) {
         bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(dst), Operand::zero(),
                      Operand::c32(0x3f800000u), src);
      } else {
         UNREACHABLE("Wrong destination register class for nir_op_b2f32.");
      }
      break;
   }
   case nir_op_b2f64: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      assert(src.regClass() == bld.lm);

      if (dst.regClass() == s2) {
         src = bool_to_scalar_condition(ctx, src);
         bld.sop2(aco_opcode::s_cselect_b64, Definition(dst), Operand::c32(0x3f800000u),
                  Operand::zero(), bld.scc(src));
      } else if (dst.regClass() == v2) {
         Temp one = bld.copy(bld.def(v1), Operand::c32(0x3FF00000u));
         Temp upper =
            bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), Operand::zero(), one, src);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), Operand::zero(), upper);
      } else {
         UNREACHABLE("Wrong destination register class for nir_op_b2f64.");
      }
      break;
   }
   case nir_op_i2i8:
   case nir_op_i2i16:
   case nir_op_i2i32:
   case nir_op_u2u8:
   case nir_op_u2u16:
   case nir_op_u2u32: {
      const unsigned input_bitsize = instr->src[0].src.ssa->bit_size;
      const unsigned output_bitsize = instr->def.bit_size;
      bool sext =
         instr->op == nir_op_i2i8 || instr->op == nir_op_i2i16 || instr->op == nir_op_i2i32;
      bool trunc = output_bitsize <= input_bitsize;

      if (instr->def.num_components == 2) {
         assert(output_bitsize == 16 && input_bitsize == 8);
         assert((instr->src[0].swizzle[0] & ~0x3) == (instr->src[0].swizzle[1] & ~0x3));

         Temp src = get_ssa_temp(ctx, instr->src[0].src.ssa);
         if (src.bytes() >= 4)
            src = emit_extract_vector(ctx, src, instr->src[0].swizzle[0] >> 2, v1);

         emit_pk_int16_from_8bit(ctx, dst, src, instr->src[0].swizzle[0] & 0x3,
                                 instr->src[0].swizzle[1] & 0x3, sext);
         break;
      }

      if (dst.type() == RegType::sgpr && instr->src[0].src.ssa->bit_size < 32) {
         /* no need to do the extract in get_alu_src() */
         sgpr_extract_mode mode = trunc  ? sgpr_extract_undef
                                  : sext ? sgpr_extract_sext
                                         : sgpr_extract_zext;
         extract_8_16_bit_sgpr_element(ctx, dst, &instr->src[0], mode);
      } else {
         convert_int(ctx, bld, get_alu_src(ctx, instr->src[0]), input_bitsize, output_bitsize,
                     sext && !trunc, dst);
      }
      break;
   }
   case nir_op_b2b32:
   case nir_op_b2i8:
   case nir_op_b2i16:
   case nir_op_b2i32: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      assert(src.regClass() == bld.lm);

      if (dst.regClass() == s1) {
         bool_to_scalar_condition(ctx, src, dst);
      } else if (dst.type() == RegType::vgpr) {
         bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(dst), Operand::zero(), Operand::c32(1u),
                      src);
      } else {
         UNREACHABLE("Invalid register class for b2i32");
      }
      break;
   }
   case nir_op_b2b1: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      assert(dst.regClass() == bld.lm);

      if (src.type() == RegType::vgpr) {
         assert(src.regClass() == v1 || src.regClass() == v2);
         assert(dst.regClass() == bld.lm);
         bld.vopc(src.size() == 2 ? aco_opcode::v_cmp_lg_u64 : aco_opcode::v_cmp_lg_u32,
                  Definition(dst), Operand::zero(), src);
      } else {
         assert(src.regClass() == s1 || src.regClass() == s2);
         Temp tmp;
         if (src.regClass() == s2 && ctx->program->gfx_level <= GFX7) {
            tmp =
               bld.sop2(aco_opcode::s_or_b64, bld.def(s2), bld.def(s1, scc), Operand::zero(), src)
                  .def(1)
                  .getTemp();
         } else {
            tmp = bld.sopc(src.size() == 2 ? aco_opcode::s_cmp_lg_u64 : aco_opcode::s_cmp_lg_u32,
                           bld.scc(bld.def(s1)), Operand::zero(), src);
         }
         bool_to_vector_condition(ctx, tmp, dst);
      }
      break;
   }
   case nir_op_unpack_64_2x32:
   case nir_op_unpack_32_2x16:
   case nir_op_unpack_64_4x16:
   case nir_op_unpack_32_4x8:
      bld.copy(Definition(dst), get_alu_src(ctx, instr->src[0]));
      emit_split_vector(
         ctx, dst, instr->op == nir_op_unpack_32_4x8 || instr->op == nir_op_unpack_64_4x16 ? 4 : 2);
      break;
   case nir_op_pack_64_2x32_split: {
      Operand src[2];
      RegClass elem_rc = dst.regClass() == s2 ? s1 : v1;
      for (unsigned i = 0; i < 2; i++) {
         if (nir_src_is_undef(instr->src[i].src))
            src[i] = Operand(elem_rc);
         else
            src[i] = Operand(get_alu_src(ctx, instr->src[i]));
      }

      bld.pseudo(aco_opcode::p_create_vector, Definition(dst), src[0], src[1]);
      break;
   }
   case nir_op_unpack_64_2x32_split_x:
      bld.pseudo(aco_opcode::p_split_vector, Definition(dst), bld.def(dst.regClass()),
                 get_alu_src(ctx, instr->src[0]));
      break;
   case nir_op_unpack_64_2x32_split_y:
      bld.pseudo(aco_opcode::p_split_vector, bld.def(dst.regClass()), Definition(dst),
                 get_alu_src(ctx, instr->src[0]));
      break;
   case nir_op_unpack_32_2x16_split_x:
      if (dst.type() == RegType::vgpr) {
         bld.pseudo(aco_opcode::p_split_vector, Definition(dst), bld.def(dst.regClass()),
                    get_alu_src(ctx, instr->src[0]));
      } else {
         bld.copy(Definition(dst), get_alu_src(ctx, instr->src[0]));
      }
      break;
   case nir_op_unpack_32_2x16_split_y:
      if (dst.type() == RegType::vgpr) {
         bld.pseudo(aco_opcode::p_split_vector, bld.def(dst.regClass()), Definition(dst),
                    get_alu_src(ctx, instr->src[0]));
      } else {
         bld.pseudo(aco_opcode::p_extract, Definition(dst), bld.def(s1, scc),
                    get_alu_src(ctx, instr->src[0]), Operand::c32(1u), Operand::c32(16u),
                    Operand::zero());
      }
      break;
   case nir_op_pack_32_2x16_split: {
      Operand src0 = Operand(get_alu_src(ctx, instr->src[0]));
      Operand src1 = Operand(get_alu_src(ctx, instr->src[1]));
      if (dst.regClass() == v1) {
         if (nir_src_is_undef(instr->src[0].src))
            src0 = Operand(v2b);
         else
            src0 = Operand(emit_extract_vector(ctx, src0.getTemp(), 0, v2b));

         if (nir_src_is_undef(instr->src[1].src))
            src1 = Operand(v2b);
         else
            src1 = Operand(emit_extract_vector(ctx, src1.getTemp(), 0, v2b));

         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), src0, src1);
      } else if (nir_src_is_undef(instr->src[1].src)) {
         bld.copy(Definition(dst), src0);
      } else if (nir_src_is_undef(instr->src[0].src)) {
         bld.pseudo(aco_opcode::p_insert, Definition(dst), bld.def(s1, scc), src1, Operand::c32(1),
                    Operand::c32(16));
      } else if (ctx->program->gfx_level >= GFX9) {
         bld.sop2(aco_opcode::s_pack_ll_b32_b16, Definition(dst), src0, src1);
      } else {
         src0 = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), src0,
                         Operand::c32(0xFFFFu));
         src1 = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), src1,
                         Operand::c32(16u));
         bld.sop2(aco_opcode::s_or_b32, Definition(dst), bld.def(s1, scc), src0, src1);
      }
      break;
   }
   case nir_op_pack_32_4x8: bld.copy(Definition(dst), get_alu_src(ctx, instr->src[0], 4)); break;
   case nir_op_pack_half_2x16_rtz_split:
   case nir_op_pack_half_2x16_split: {
      if (dst.regClass() == v1) {
         if (ctx->program->gfx_level == GFX8 || ctx->program->gfx_level == GFX9)
            emit_vop3a_instruction(ctx, instr, aco_opcode::v_cvt_pkrtz_f16_f32_e64, dst);
         else
            emit_vop2_instruction(ctx, instr, aco_opcode::v_cvt_pkrtz_f16_f32, dst, false);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_cvt_pk_rtz_f16_f32, dst, false);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_pack_unorm_2x16:
   case nir_op_pack_snorm_2x16: {
      unsigned bit_size = instr->src[0].src.ssa->bit_size;
      /* Only support 16 and 32bit. */
      assert(bit_size == 32 || bit_size == 16);

      Temp src = get_ssa_temp(ctx, instr->src[0].src.ssa);

      RegClass src_rc = bit_size == 32 ? RegClass::get(src.type(), 4) : v2b;
      Temp src0 = emit_extract_vector(ctx, src, instr->src[0].swizzle[0], src_rc);
      Temp src1 = emit_extract_vector(ctx, src, instr->src[0].swizzle[1], src_rc);

      /* Work around for pre-GFX9 GPU which don't have fp16 pknorm instruction. */
      if (bit_size == 16 && ctx->program->gfx_level < GFX9) {
         src0 = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), src0);
         src1 = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), src1);
         bit_size = 32;
      }

      aco_opcode opcode;
      if (bit_size == 32) {
         opcode = instr->op == nir_op_pack_unorm_2x16 ? aco_opcode::v_cvt_pknorm_u16_f32
                                                      : aco_opcode::v_cvt_pknorm_i16_f32;
      } else {
         opcode = instr->op == nir_op_pack_unorm_2x16 ? aco_opcode::v_cvt_pknorm_u16_f16
                                                      : aco_opcode::v_cvt_pknorm_i16_f16;
      }

      bld.vop3(opcode, Definition(dst), src0, as_vgpr(ctx, src1));
      break;
   }
   case nir_op_pack_uint_2x16:
   case nir_op_pack_sint_2x16: {
      Temp src = get_ssa_temp(ctx, instr->src[0].src.ssa);
      RegClass src_rc = RegClass::get(src.type(), 4);
      Temp src0 = emit_extract_vector(ctx, src, instr->src[0].swizzle[0], src_rc);
      Temp src1 = emit_extract_vector(ctx, src, instr->src[0].swizzle[1], src_rc);
      aco_opcode opcode = instr->op == nir_op_pack_uint_2x16 ? aco_opcode::v_cvt_pk_u16_u32
                                                             : aco_opcode::v_cvt_pk_i16_i32;
      bld.vop3(opcode, Definition(dst), src0, as_vgpr(ctx, src1));
      break;
   }
   case nir_op_unpack_half_2x16_split_x: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == s1) {
         bld.sop1(aco_opcode::s_cvt_f32_f16, Definition(dst), src);
         break;
      }
      if (src.regClass() == v1)
         src = bld.pseudo(aco_opcode::p_split_vector, bld.def(v2b), bld.def(v2b), src);
      if (dst.regClass() == v1) {
         bld.vop1(aco_opcode::v_cvt_f32_f16, Definition(dst), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_unpack_half_2x16_split_y: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == s1) {
         bld.sop1(aco_opcode::s_cvt_hi_f32_f16, Definition(dst), src);
         break;
      }
      if (src.regClass() == s1)
         src = bld.pseudo(aco_opcode::p_extract, bld.def(s1), bld.def(s1, scc), src,
                          Operand::c32(1u), Operand::c32(16u), Operand::zero());
      else
         src =
            bld.pseudo(aco_opcode::p_split_vector, bld.def(v2b), bld.def(v2b), src).def(1).getTemp();
      if (dst.regClass() == v1) {
         bld.vop1(aco_opcode::v_cvt_f32_f16, Definition(dst), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_msad_4x8: {
      assert(dst.regClass() == v1);
      emit_vop3a_instruction(ctx, instr, aco_opcode::v_msad_u8, dst, false, 3u, true);
      break;
   }
   case nir_op_mqsad_4x8: {
      assert(dst.regClass() == v4);
      Temp ref = get_alu_src(ctx, instr->src[0]);
      Temp src = get_alu_src(ctx, instr->src[1], 2);
      Temp accum = get_alu_src(ctx, instr->src[2], 4);
      bld.vop3(aco_opcode::v_mqsad_u32_u8, Definition(dst), as_vgpr(ctx, src), as_vgpr(ctx, ref),
               as_vgpr(ctx, accum));
      emit_split_vector(ctx, dst, 4);
      break;
   }
   case nir_op_shfr: {
      if (dst.regClass() == s1) {
         Temp src0 = get_alu_src(ctx, instr->src[0]);
         Temp src1 = get_alu_src(ctx, instr->src[1]);

         Temp amount;
         if (nir_src_is_const(instr->src[2].src)) {
            unsigned camount = nir_src_as_uint(instr->src[2].src) & 0x1f;
            if (camount == 16 && ctx->program->gfx_level >= GFX11) {
               bld.sop2(aco_opcode::s_pack_hl_b32_b16, Definition(dst), src1, src0);
               break;
            }
            amount = bld.copy(bld.def(s1), Operand::c32(camount));
         } else if (get_alu_src_ub(ctx, instr, 2) >= 32) {
            amount = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc),
                              get_alu_src(ctx, instr->src[2]), Operand::c32(0x1f));
         } else {
            amount = get_alu_src(ctx, instr->src[2]);
         }

         Temp src = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), src1, src0);

         Temp res = bld.sop2(aco_opcode::s_lshr_b64, bld.def(s2), bld.def(s1, scc), src, amount);
         bld.pseudo(aco_opcode::p_extract_vector, Definition(dst), res, Operand::zero());
      } else if (dst.regClass() == v1) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_alignbit_b32, dst, false, 3u);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_alignbyte_amd: {
      if (dst.regClass() == v1) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_alignbyte_b32, dst, false, 3u);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_byte_perm_amd: {
      if (dst.regClass() == v1) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_perm_b32, dst, false, 3u);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fquantize2f16: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == v1) {
         Temp f16;
         if (ctx->block->fp_mode.round16_64 != fp_round_ne) {
            ctx->program->needs_fp_mode_insertion = true;
            f16 = bld.vop1(aco_opcode::p_v_cvt_f16_f32_rtne, bld.def(v2b), src);
         } else {
            f16 = bld.vop1(aco_opcode::v_cvt_f16_f32, bld.def(v2b), src);
         }

         if (ctx->block->fp_mode.denorm16_64 != fp_denorm_keep) {
            bld.vop1(aco_opcode::v_cvt_f32_f16, Definition(dst), f16);
            break;
         }

         Temp denorm_zero;
         Temp f32 = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), f16);
         if (ctx->program->gfx_level >= GFX8) {
            /* value is negative/positive denormal value/zero */
            Instruction* tmp0 =
               bld.vopc_e64(aco_opcode::v_cmp_class_f16, bld.def(bld.lm), f16, Operand::c32(0x30));
            tmp0->valu().abs[0] = true;
            tmp0->valu().neg[0] = true;
            denorm_zero = tmp0->definitions[0].getTemp();
         } else {
            /* 0x38800000 is smallest half float value (2^-14) in 32-bit float,
             * so compare the result and flush to 0 if it's smaller.
             */
            Temp smallest = bld.copy(bld.def(s1), Operand::c32(0x38800000u));
            Instruction* tmp0 =
               bld.vopc_e64(aco_opcode::v_cmp_lt_f32, bld.def(bld.lm), f32, smallest);
            tmp0->valu().abs[0] = true;
            denorm_zero = tmp0->definitions[0].getTemp();
         }
         if (nir_alu_instr_is_signed_zero_preserve(instr)) {
            Temp copysign_0 =
               bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), Operand::zero(), as_vgpr(ctx, src));
            bld.vop2(aco_opcode::v_cndmask_b32, Definition(dst), f32, copysign_0, denorm_zero);
         } else {
            bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(dst), f32, Operand::zero(),
                         denorm_zero);
         }
      } else if (dst.regClass() == s1) {
         Temp f16;
         if (ctx->block->fp_mode.round16_64 != fp_round_ne) {
            ctx->program->needs_fp_mode_insertion = true;
            f16 = bld.sop1(aco_opcode::p_s_cvt_f16_f32_rtne, bld.def(s1), src);
         } else {
            f16 = bld.sop1(aco_opcode::s_cvt_f16_f32, bld.def(s1), src);
         }

         if (ctx->block->fp_mode.denorm16_64 != fp_denorm_keep) {
            bld.sop1(aco_opcode::s_cvt_f32_f16, Definition(dst), f16);
         } else {
            Temp f32 = bld.sop1(aco_opcode::s_cvt_f32_f16, bld.def(s1), f16);
            Temp abs_mask = bld.copy(bld.def(s1), Operand::c32(0x7fffffff));
            Temp abs =
               bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), f32, abs_mask);
            Operand sign;
            if (nir_alu_instr_is_signed_zero_preserve(instr)) {
               sign =
                  bld.sop2(aco_opcode::s_andn2_b32, bld.def(s1), bld.def(s1, scc), f32, abs_mask);
            } else {
               sign = Operand::c32(0);
            }
            Temp smallest = bld.copy(bld.def(s1), Operand::c32(0x38800000u));
            Temp denorm_zero = bld.sopc(aco_opcode::s_cmp_lt_u32, bld.def(s1, scc), abs, smallest);
            bld.sop2(aco_opcode::s_cselect_b32, Definition(dst), sign, f32, bld.scc(denorm_zero));
         }
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_bfm: {
      Temp bits = get_alu_src(ctx, instr->src[0]);
      Temp offset = get_alu_src(ctx, instr->src[1]);

      if (dst.regClass() == s1) {
         bld.sop2(aco_opcode::s_bfm_b32, Definition(dst), bits, offset);
      } else if (dst.regClass() == v1) {
         bld.vop3(aco_opcode::v_bfm_b32, Definition(dst), bits, offset);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_bitfield_select: {
      Temp bitmask = get_alu_src(ctx, instr->src[0], instr->def.num_components);
      Temp insert = get_alu_src(ctx, instr->src[1], instr->def.num_components);
      Temp base = get_alu_src(ctx, instr->src[2], instr->def.num_components);

      /* dst = (insert & bitmask) | (base & ~bitmask) */
      if (dst.type() == RegType::sgpr) {
         RegClass rc = dst.regClass();
         assert(rc == s1 || rc == s2);

         bool src_const[3] = {true, true, true};
         uint64_t const_value[3] = {0, 0, 0};
         for (unsigned i = 0; i < 3; i++) {
            for (unsigned j = 0; j < instr->def.num_components; j++) {
               nir_scalar s = nir_scalar_resolved(instr->src[i].src.ssa, instr->src[i].swizzle[j]);
               if (!nir_scalar_is_const(s)) {
                  src_const[i] = false;
                  break;
               }

               const_value[i] |= nir_scalar_as_uint(s) << (instr->def.bit_size * j);
            }
         }

         if (rc == s1 && src_const[0] && ctx->program->gfx_level >= GFX9 &&
             (const_value[0] == 0xffff || const_value[0] == 0xffff0000)) {
            if (const_value[0] == 0xffff) {
               bld.sop2(aco_opcode::s_pack_lh_b32_b16, Definition(dst), insert, base);
            } else {
               bld.sop2(aco_opcode::s_pack_lh_b32_b16, Definition(dst), base, insert);
            }
            break;
         }

         Temp lhs;
         if (src_const[0] && src_const[1]) {
            uint64_t const_lhs = const_value[1] & const_value[0];
            if (rc == s1) {
               lhs = bld.copy(bld.def(s1), Operand::c32(const_lhs));
            } else {
               lhs = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), Operand::c32(const_lhs),
                                Operand::c32(const_lhs >> 32));
            }
         } else {
            aco_opcode s_and = rc == s1 ? aco_opcode::s_and_b32 : aco_opcode::s_and_b64;
            lhs = bld.sop2(s_and, bld.def(rc), bld.def(s1, scc), insert, bitmask);
         }

         Temp rhs;
         if (src_const[0] && src_const[2]) {
            uint64_t const_rhs = const_value[2] & ~const_value[0];
            if (rc == s1) {
               rhs = bld.copy(bld.def(s1), Operand::c32(const_rhs));
            } else {
               rhs = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), Operand::c32(const_rhs),
                                Operand::c32(const_rhs >> 32));
            }
         } else {
            aco_opcode s_andn2 = rc == s1 ? aco_opcode::s_andn2_b32 : aco_opcode::s_andn2_b64;
            rhs = bld.sop2(s_andn2, bld.def(rc), bld.def(s1, scc), base, bitmask);
         }

         aco_opcode s_or = rc == s1 ? aco_opcode::s_or_b32 : aco_opcode::s_or_b64;
         bld.sop2(s_or, Definition(dst), bld.def(s1, scc), rhs, lhs);
         break;
      }

      if (bitmask.type() == RegType::sgpr) {
         insert = as_vgpr(ctx, insert);
         base = as_vgpr(ctx, base);
      } else if (insert.type() == RegType::sgpr) {
         base = as_vgpr(ctx, base);
      }

      if (dst.size() == 1) {
         bld.vop3(aco_opcode::v_bfi_b32, Definition(dst), bitmask, insert, base);
         emit_split_vector(ctx, dst, instr->def.num_components);
      } else if (dst.size() == 2) {
         Temp bitmask_lo = bld.tmp(v1), bitmask_hi = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(bitmask_lo), Definition(bitmask_hi),
                    bitmask);
         Temp insert_lo = bld.tmp(v1), insert_hi = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(insert_lo), Definition(insert_hi),
                    insert);
         Temp base_lo = bld.tmp(v1), base_hi = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(base_lo), Definition(base_hi), base);

         Temp res_lo = bld.vop3(aco_opcode::v_bfi_b32, bld.def(v1), bitmask_lo, insert_lo, base_lo);
         Temp res_hi = bld.vop3(aco_opcode::v_bfi_b32, bld.def(v1), bitmask_hi, insert_hi, base_hi);

         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), res_lo, res_hi);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ubfe:
   case nir_op_ibfe: {
      if (dst.bytes() != 4)
         UNREACHABLE("Unsupported BFE bit size");

      if (dst.type() == RegType::sgpr) {
         Temp base = get_alu_src(ctx, instr->src[0]);

         nir_const_value* const_offset = nir_src_as_const_value(instr->src[1].src);
         nir_const_value* const_bits = nir_src_as_const_value(instr->src[2].src);
         aco_opcode opcode =
            instr->op == nir_op_ubfe ? aco_opcode::s_bfe_u32 : aco_opcode::s_bfe_i32;
         if (const_offset && const_bits) {
            uint32_t extract = ((const_bits->u32 & 0x1f) << 16) | (const_offset->u32 & 0x1f);
            bld.sop2(opcode, Definition(dst), bld.def(s1, scc), base, Operand::c32(extract));
            break;
         }

         Temp offset = get_alu_src(ctx, instr->src[1]);
         Temp bits = get_alu_src(ctx, instr->src[2]);

         if (ctx->program->gfx_level >= GFX9) {
            Operand bits_op = const_bits ? Operand::c32(const_bits->u32 & 0x1f)
                                         : bld.sop2(aco_opcode::s_and_b32, bld.def(s1),
                                                    bld.def(s1, scc), bits, Operand::c32(0x1fu));
            Temp extract = bld.sop2(aco_opcode::s_pack_ll_b32_b16, bld.def(s1), offset, bits_op);
            bld.sop2(opcode, Definition(dst), bld.def(s1, scc), base, extract);
         } else if (instr->op == nir_op_ubfe) {
            Temp mask = bld.sop2(aco_opcode::s_bfm_b32, bld.def(s1), bits, offset);
            Temp masked =
               bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), base, mask);
            bld.sop2(aco_opcode::s_lshr_b32, Definition(dst), bld.def(s1, scc), masked, offset);
         } else {
            Operand bits_op = const_bits
                                 ? Operand::c32((const_bits->u32 & 0x1f) << 16)
                                 : bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc),
                                            bld.sop2(aco_opcode::s_and_b32, bld.def(s1),
                                                     bld.def(s1, scc), bits, Operand::c32(0x1fu)),
                                            Operand::c32(16u));
            Operand offset_op = const_offset
                                   ? Operand::c32(const_offset->u32 & 0x1fu)
                                   : bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc),
                                              offset, Operand::c32(0x1fu));

            Temp extract =
               bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc), bits_op, offset_op);
            bld.sop2(aco_opcode::s_bfe_i32, Definition(dst), bld.def(s1, scc), base, extract);
         }

      } else {
         aco_opcode opcode =
            instr->op == nir_op_ubfe ? aco_opcode::v_bfe_u32 : aco_opcode::v_bfe_i32;
         emit_vop3a_instruction(ctx, instr, opcode, dst, false, 3);
      }
      break;
   }
   case nir_op_extract_u8:
   case nir_op_extract_i8:
   case nir_op_extract_u16:
   case nir_op_extract_i16: {
      bool is_signed = instr->op == nir_op_extract_i16 || instr->op == nir_op_extract_i8;
      unsigned comp = instr->op == nir_op_extract_u8 || instr->op == nir_op_extract_i8 ? 4 : 2;
      uint32_t bits = comp == 4 ? 8 : 16;

      if (instr->def.num_components == 2) {
         assert(instr->def.bit_size == 16 && bits == 8);

         Temp src = get_alu_src_vop3p(ctx, instr->src[0]);

         unsigned swizzle[2];
         for (unsigned i = 0; i < 2; i++) {
            nir_scalar index = nir_scalar_resolved(instr->src[1].src.ssa, instr->src[1].swizzle[i]);
            swizzle[i] = (instr->src[0].swizzle[i] & 0x1) * 2 + nir_scalar_as_uint(index);
         }

         emit_pk_int16_from_8bit(ctx, dst, src, swizzle[0], swizzle[1], is_signed);
         break;
      }

      unsigned index = nir_src_as_uint(instr->src[1].src);
      if (bits >= instr->def.bit_size || index * bits >= instr->def.bit_size) {
         assert(index == 0);
         bld.copy(Definition(dst), get_alu_src(ctx, instr->src[0]));
      } else if (dst.regClass() == s1 && instr->def.bit_size == 16) {
         Temp vec = get_ssa_temp(ctx, instr->src[0].src.ssa);
         unsigned swizzle = instr->src[0].swizzle[0];
         if (vec.size() > 1) {
            vec = emit_extract_vector(ctx, vec, swizzle / 2, s1);
            swizzle = swizzle & 1;
         }
         index += swizzle * instr->def.bit_size / bits;
         bld.pseudo(aco_opcode::p_extract, Definition(dst), bld.def(s1, scc), Operand(vec),
                    Operand::c32(index), Operand::c32(bits), Operand::c32(is_signed));
      } else if (dst.regClass() == s1) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         bld.pseudo(aco_opcode::p_extract, Definition(dst), bld.def(s1, scc), Operand(src),
                    Operand::c32(index), Operand::c32(bits), Operand::c32(is_signed));
      } else if (dst.regClass() == s2) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         aco_opcode op = is_signed ? aco_opcode::s_bfe_i64 : aco_opcode::s_bfe_u64;
         Temp extract = bld.copy(bld.def(s1), Operand::c32((bits << 16) | (index * bits)));
         bld.sop2(op, Definition(dst), bld.def(s1, scc), src, extract);
      } else {
         assert(dst.regClass().type() == RegType::vgpr);
         Temp src = get_alu_src(ctx, instr->src[0]);
         Definition def(dst);

         if (dst.bytes() == 8) {
            src = emit_extract_vector(ctx, src, index / comp, v1);
            index %= comp;
            def = bld.def(v1);
         }

         assert(def.bytes() <= 4);
         src = emit_extract_vector(ctx, src, 0, def.regClass());
         bld.pseudo(aco_opcode::p_extract, def, Operand(src), Operand::c32(index),
                    Operand::c32(bits), Operand::c32(is_signed));

         if (dst.size() == 2) {
            Temp lo = def.getTemp();
            Operand hi = Operand::zero();
            if (is_signed)
               hi = bld.vop2(aco_opcode::v_ashrrev_i32, bld.def(v1), Operand::c32(31), lo);
            bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
         }
      }
      break;
   }
   case nir_op_insert_u8:
   case nir_op_insert_u16: {
      unsigned comp = instr->op == nir_op_insert_u8 ? 4 : 2;
      uint32_t bits = comp == 4 ? 8 : 16;
      unsigned index = nir_src_as_uint(instr->src[1].src);
      if (bits >= instr->def.bit_size || index * bits >= instr->def.bit_size) {
         assert(index == 0);
         bld.copy(Definition(dst), get_alu_src(ctx, instr->src[0]));
      } else {
         Temp src = get_alu_src(ctx, instr->src[0]);
         Definition def(dst);
         bool swap = false;
         if (dst.bytes() == 8) {
            src = emit_extract_vector(ctx, src, 0u, RegClass(src.type(), 1));
            swap = index >= comp;
            index %= comp;
            def = bld.def(src.type(), 1);
         }
         if (def.regClass() == s1) {
            bld.pseudo(aco_opcode::p_insert, def, bld.def(s1, scc), Operand(src),
                       Operand::c32(index), Operand::c32(bits));
         } else {
            src = emit_extract_vector(ctx, src, 0, def.regClass());
            bld.pseudo(aco_opcode::p_insert, def, Operand(src), Operand::c32(index),
                       Operand::c32(bits));
         }
         if (dst.size() == 2 && swap)
            bld.pseudo(aco_opcode::p_create_vector, Definition(dst), Operand::zero(),
                       def.getTemp());
         else if (dst.size() == 2)
            bld.pseudo(aco_opcode::p_create_vector, Definition(dst), def.getTemp(),
                       Operand::zero());
      }
      break;
   }
   case nir_op_bit_count: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (src.regClass() == s1) {
         bld.sop1(aco_opcode::s_bcnt1_i32_b32, Definition(dst), bld.def(s1, scc), src);
      } else if (src.regClass() == v1) {
         bld.vop3(aco_opcode::v_bcnt_u32_b32, Definition(dst), src, Operand::zero());
      } else if (src.regClass() == v2) {
         bld.vop3(aco_opcode::v_bcnt_u32_b32, Definition(dst), emit_extract_vector(ctx, src, 1, v1),
                  bld.vop3(aco_opcode::v_bcnt_u32_b32, bld.def(v1),
                           emit_extract_vector(ctx, src, 0, v1), Operand::zero()));
      } else if (src.regClass() == s2) {
         bld.sop1(aco_opcode::s_bcnt1_i32_b64, Definition(dst), bld.def(s1, scc), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_flt: {
      emit_comparison(
         ctx, instr, dst, aco_opcode::v_cmp_lt_f16, aco_opcode::v_cmp_lt_f32,
         aco_opcode::v_cmp_lt_f64,
         ctx->program->gfx_level >= GFX11_5 ? aco_opcode::s_cmp_lt_f16 : aco_opcode::num_opcodes,
         ctx->program->gfx_level >= GFX11_5 ? aco_opcode::s_cmp_lt_f32 : aco_opcode::num_opcodes);
      break;
   }
   case nir_op_fge: {
      emit_comparison(
         ctx, instr, dst, aco_opcode::v_cmp_ge_f16, aco_opcode::v_cmp_ge_f32,
         aco_opcode::v_cmp_ge_f64,
         ctx->program->gfx_level >= GFX11_5 ? aco_opcode::s_cmp_ge_f16 : aco_opcode::num_opcodes,
         ctx->program->gfx_level >= GFX11_5 ? aco_opcode::s_cmp_ge_f32 : aco_opcode::num_opcodes);
      break;
   }
   case nir_op_fltu: {
      emit_comparison(
         ctx, instr, dst, aco_opcode::v_cmp_nge_f16, aco_opcode::v_cmp_nge_f32,
         aco_opcode::v_cmp_nge_f64,
         ctx->program->gfx_level >= GFX11_5 ? aco_opcode::s_cmp_nge_f16 : aco_opcode::num_opcodes,
         ctx->program->gfx_level >= GFX11_5 ? aco_opcode::s_cmp_nge_f32 : aco_opcode::num_opcodes);
      break;
   }
   case nir_op_fgeu: {
      emit_comparison(
         ctx, instr, dst, aco_opcode::v_cmp_nlt_f16, aco_opcode::v_cmp_nlt_f32,
         aco_opcode::v_cmp_nlt_f64,
         ctx->program->gfx_level >= GFX11_5 ? aco_opcode::s_cmp_nlt_f16 : aco_opcode::num_opcodes,
         ctx->program->gfx_level >= GFX11_5 ? aco_opcode::s_cmp_nlt_f32 : aco_opcode::num_opcodes);
      break;
   }
   case nir_op_feq: {
      emit_comparison(
         ctx, instr, dst, aco_opcode::v_cmp_eq_f16, aco_opcode::v_cmp_eq_f32,
         aco_opcode::v_cmp_eq_f64,
         ctx->program->gfx_level >= GFX11_5 ? aco_opcode::s_cmp_eq_f16 : aco_opcode::num_opcodes,
         ctx->program->gfx_level >= GFX11_5 ? aco_opcode::s_cmp_eq_f32 : aco_opcode::num_opcodes);
      break;
   }
   case nir_op_fneu: {
      emit_comparison(
         ctx, instr, dst, aco_opcode::v_cmp_neq_f16, aco_opcode::v_cmp_neq_f32,
         aco_opcode::v_cmp_neq_f64,
         ctx->program->gfx_level >= GFX11_5 ? aco_opcode::s_cmp_neq_f16 : aco_opcode::num_opcodes,
         ctx->program->gfx_level >= GFX11_5 ? aco_opcode::s_cmp_neq_f32 : aco_opcode::num_opcodes);
      break;
   }
   case nir_op_fequ: {
      emit_comparison(
         ctx, instr, dst, aco_opcode::v_cmp_nlg_f16, aco_opcode::v_cmp_nlg_f32,
         aco_opcode::v_cmp_nlg_f64,
         ctx->program->gfx_level >= GFX11_5 ? aco_opcode::s_cmp_nlg_f16 : aco_opcode::num_opcodes,
         ctx->program->gfx_level >= GFX11_5 ? aco_opcode::s_cmp_nlg_f32 : aco_opcode::num_opcodes);
      break;
   }
   case nir_op_fneo: {
      emit_comparison(
         ctx, instr, dst, aco_opcode::v_cmp_lg_f16, aco_opcode::v_cmp_lg_f32,
         aco_opcode::v_cmp_lg_f64,
         ctx->program->gfx_level >= GFX11_5 ? aco_opcode::s_cmp_lg_f16 : aco_opcode::num_opcodes,
         ctx->program->gfx_level >= GFX11_5 ? aco_opcode::s_cmp_lg_f32 : aco_opcode::num_opcodes);
      break;
   }
   case nir_op_funord: {
      emit_comparison(
         ctx, instr, dst, aco_opcode::v_cmp_u_f16, aco_opcode::v_cmp_u_f32, aco_opcode::v_cmp_u_f64,
         ctx->program->gfx_level >= GFX11_5 ? aco_opcode::s_cmp_u_f16 : aco_opcode::num_opcodes,
         ctx->program->gfx_level >= GFX11_5 ? aco_opcode::s_cmp_u_f32 : aco_opcode::num_opcodes);
      break;
   }
   case nir_op_ford: {
      emit_comparison(
         ctx, instr, dst, aco_opcode::v_cmp_o_f16, aco_opcode::v_cmp_o_f32, aco_opcode::v_cmp_o_f64,
         ctx->program->gfx_level >= GFX11_5 ? aco_opcode::s_cmp_o_f16 : aco_opcode::num_opcodes,
         ctx->program->gfx_level >= GFX11_5 ? aco_opcode::s_cmp_o_f32 : aco_opcode::num_opcodes);
      break;
   }
   case nir_op_ilt: {
      emit_comparison(ctx, instr, dst, aco_opcode::v_cmp_lt_i16, aco_opcode::v_cmp_lt_i32,
                      aco_opcode::v_cmp_lt_i64, aco_opcode::num_opcodes, aco_opcode::s_cmp_lt_i32);
      break;
   }
   case nir_op_ige: {
      emit_comparison(ctx, instr, dst, aco_opcode::v_cmp_ge_i16, aco_opcode::v_cmp_ge_i32,
                      aco_opcode::v_cmp_ge_i64, aco_opcode::num_opcodes, aco_opcode::s_cmp_ge_i32);
      break;
   }
   case nir_op_ieq: {
      if (instr->src[0].src.ssa->bit_size == 1)
         emit_bitwise_logic(ctx, instr, dst, Builder::s_xnor, aco_opcode::num_opcodes);
      else
         emit_comparison(
            ctx, instr, dst, aco_opcode::v_cmp_eq_i16, aco_opcode::v_cmp_eq_i32,
            aco_opcode::v_cmp_eq_i64, aco_opcode::num_opcodes, aco_opcode::s_cmp_eq_i32,
            ctx->program->gfx_level >= GFX8 ? aco_opcode::s_cmp_eq_u64 : aco_opcode::num_opcodes);
      break;
   }
   case nir_op_ine: {
      if (instr->src[0].src.ssa->bit_size == 1)
         emit_bitwise_logic(ctx, instr, dst, Builder::s_xor, aco_opcode::num_opcodes);
      else
         emit_comparison(
            ctx, instr, dst, aco_opcode::v_cmp_lg_i16, aco_opcode::v_cmp_lg_i32,
            aco_opcode::v_cmp_lg_i64, aco_opcode::num_opcodes, aco_opcode::s_cmp_lg_i32,
            ctx->program->gfx_level >= GFX8 ? aco_opcode::s_cmp_lg_u64 : aco_opcode::num_opcodes);
      break;
   }
   case nir_op_ult: {
      emit_comparison(ctx, instr, dst, aco_opcode::v_cmp_lt_u16, aco_opcode::v_cmp_lt_u32,
                      aco_opcode::v_cmp_lt_u64, aco_opcode::num_opcodes, aco_opcode::s_cmp_lt_u32);
      break;
   }
   case nir_op_uge: {
      emit_comparison(ctx, instr, dst, aco_opcode::v_cmp_ge_u16, aco_opcode::v_cmp_ge_u32,
                      aco_opcode::v_cmp_ge_u64, aco_opcode::num_opcodes, aco_opcode::s_cmp_ge_u32);
      break;
   }
   case nir_op_bitz:
   case nir_op_bitnz: {
      assert(instr->src[0].src.ssa->bit_size != 1);
      bool test0 = instr->op == nir_op_bitz;
      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      bool use_valu = src0.type() == RegType::vgpr || src1.type() == RegType::vgpr;
      if (!use_valu) {
         aco_opcode op = instr->src[0].src.ssa->bit_size == 64 ? aco_opcode::s_bitcmp1_b64
                                                               : aco_opcode::s_bitcmp1_b32;
         if (test0)
            op = instr->src[0].src.ssa->bit_size == 64 ? aco_opcode::s_bitcmp0_b64
                                                       : aco_opcode::s_bitcmp0_b32;
         emit_sopc_instruction(ctx, instr, op, dst);
         break;
      }

      /* We do not have a VALU version of s_bitcmp.
       * But if the second source is constant, we can use
       * v_cmp_class_f32's LUT to check the bit.
       * The LUT only has 10 entries, so extract a higher byte if we have to.
       * For sign bits comparision with 0 is better because v_cmp_class
       * can't be inverted.
       */
      if (nir_src_is_const(instr->src[1].src)) {
         uint32_t bit = nir_alu_src_as_uint(instr->src[1]);
         bit &= instr->src[0].src.ssa->bit_size - 1;
         src0 = as_vgpr(ctx, src0);

         if (src0.regClass() == v2) {
            src0 = emit_extract_vector(ctx, src0, (bit & 32) != 0, v1);
            bit &= 31;
         }

         if (bit == 31) {
            bld.vopc(test0 ? aco_opcode::v_cmp_le_i32 : aco_opcode::v_cmp_gt_i32, Definition(dst),
                     Operand::c32(0), src0);
            break;
         }

         if (bit == 15 && ctx->program->gfx_level >= GFX8) {
            bld.vopc(test0 ? aco_opcode::v_cmp_le_i16 : aco_opcode::v_cmp_gt_i16, Definition(dst),
                     Operand::c32(0), src0);
            break;
         }

         /* Set max_bit lower to avoid +inf if we can use sdwa+qnan instead. */
         const bool can_sdwa = ctx->program->gfx_level >= GFX8 && ctx->program->gfx_level < GFX11;
         const unsigned max_bit = can_sdwa ? 0x8 : 0x9;
         const bool use_opsel = bit > 0xf && (bit & 0xf) <= max_bit;
         if (use_opsel) {
            src0 = bld.pseudo(aco_opcode::p_extract, bld.def(v1), src0, Operand::c32(1),
                              Operand::c32(16), Operand::c32(0));
            bit &= 0xf;
         }

         /* If we can use sdwa the extract is free, while test0's s_not is not. */
         if (bit == 7 && test0 && can_sdwa) {
            src0 = bld.pseudo(aco_opcode::p_extract, bld.def(v1), src0, Operand::c32(bit / 8),
                              Operand::c32(8), Operand::c32(1));
            bld.vopc(test0 ? aco_opcode::v_cmp_le_i32 : aco_opcode::v_cmp_gt_i32, Definition(dst),
                     Operand::c32(0), src0);
            break;
         }

         if (bit > max_bit) {
            src0 = bld.pseudo(aco_opcode::p_extract, bld.def(v1), src0, Operand::c32(bit / 8),
                              Operand::c32(8), Operand::c32(0));
            bit &= 0x7;
         }

         /* denorm and snan/qnan inputs are preserved using all float control modes. */
         static const struct {
            uint32_t fp32;
            uint32_t fp16;
            bool negate;
         } float_lut[10] = {
            {0x7f800001, 0x7c01, false}, /* snan */
            {~0u, ~0u, false},           /* qnan */
            {0xff800000, 0xfc00, false}, /* -inf */
            {0xbf800000, 0xbc00, false}, /* -normal (-1.0) */
            {1, 1, true},                /* -denormal */
            {0, 0, true},                /* -0.0 */
            {0, 0, false},               /* +0.0 */
            {1, 1, false},               /* +denormal */
            {0x3f800000, 0x3c00, false}, /* +normal (+1.0) */
            {0x7f800000, 0x7c00, false}, /* +inf */
         };

         Temp tmp = test0 ? bld.tmp(bld.lm) : dst;
         /* fp16 can use s_movk for bit 0. It also supports opsel on gfx11. */
         const bool use_fp16 = (ctx->program->gfx_level >= GFX8 && bit == 0) ||
                               (ctx->program->gfx_level >= GFX11 && use_opsel);
         const aco_opcode op = use_fp16 ? aco_opcode::v_cmp_class_f16 : aco_opcode::v_cmp_class_f32;
         const uint32_t c = use_fp16 ? float_lut[bit].fp16 : float_lut[bit].fp32;

         VALU_instruction& res =
            bld.vopc(op, Definition(tmp), bld.copy(bld.def(s1), Operand::c32(c)), src0)->valu();
         if (float_lut[bit].negate) {
            res.format = asVOP3(res.format);
            res.neg[0] = true;
         }

         if (test0)
            bld.sop1(Builder::s_not, Definition(dst), bld.def(s1, scc), tmp);

         break;
      }

      Temp res;
      aco_opcode op = test0 ? aco_opcode::v_cmp_eq_i32 : aco_opcode::v_cmp_lg_i32;
      if (instr->src[0].src.ssa->bit_size == 16) {
         op = test0 ? aco_opcode::v_cmp_eq_i16 : aco_opcode::v_cmp_lg_i16;
         if (ctx->program->gfx_level < GFX10)
            res = bld.vop2_e64(aco_opcode::v_lshlrev_b16, bld.def(v2b), src1, Operand::c32(1));
         else
            res = bld.vop3(aco_opcode::v_lshlrev_b16_e64, bld.def(v2b), src1, Operand::c32(1));

         res = bld.vop2(aco_opcode::v_and_b32, bld.def(v2b), src0, res);
      } else if (instr->src[0].src.ssa->bit_size == 32) {
         res = bld.vop3(aco_opcode::v_bfe_u32, bld.def(v1), src0, src1, Operand::c32(1));
      } else if (instr->src[0].src.ssa->bit_size == 64) {
         if (ctx->program->gfx_level < GFX8)
            res = bld.vop3(aco_opcode::v_lshr_b64, bld.def(v2), src0, src1);
         else
            res = bld.vop3(aco_opcode::v_lshrrev_b64, bld.def(v2), src1, src0);

         res = emit_extract_vector(ctx, res, 0, v1);
         res = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand::c32(0x1), res);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      bld.vopc(op, Definition(dst), Operand::c32(0), res);
      break;
   }
   default: isel_err(&instr->instr, "Unknown NIR ALU instr");
   }
}

} // namespace aco
