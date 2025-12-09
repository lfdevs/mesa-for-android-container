/*
 * Copyright © 2018 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_builder.h"
#include "aco_ir.h"

#include "util/half_float.h"
#include "util/memstream.h"

#include <algorithm>
#include <array>
#include <vector>

namespace aco {

namespace {
/**
 * The optimizer works in 4 phases:
 * (1) The first pass collects information for each ssa-def,
 *     propagates reg->reg operands of the same type, inline constants
 *     and neg/abs input modifiers.
 * (2) The second pass combines instructions like mad, omod, clamp and
 *     propagates sgpr's on VALU instructions.
 *     This pass depends on information collected in the first pass.
 * (3) The third pass goes backwards, and selects instructions,
 *     i.e. decides if a mad instruction is profitable and eliminates dead code.
 * (4) The fourth pass cleans up the sequence: literals get applied and dead
 *     instructions are removed from the sequence.
 */

enum Label {
   label_constant = 1ull << 0,
   label_temp = 1ull << 1,
   label_combined_instr = 1ull << 2,
   /* This label means that it's either 0 or -1, and the ssa_info::temp is an s1 which is 0 or 1. */
   label_uniform_bool = 1ull << 3,
   /* This label is added to the first definition of s_not/s_or/s_xor/s_and when all operands are
    * uniform_bool or uniform_bitwise. The first definition of ssa_info::instr would be 0 or -1 and
    * the second is SCC.
    */
   label_uniform_bitwise = 1ull << 4,
   /* This label means that it's either 0 or 1 and ssa_info::temp is the inverse. */
   label_scc_invert = 1ull << 5,
   label_scc_needed = 1ull << 6,
   label_extract = 1ull << 7,

   /* These have one label for fp16 and one for fp32/64.
    * 32bit vs 64bit type mismatches are impossible because
    * of the different register class sizes.
    */
   label_abs_fp32_64 = 1ull << 16,
   label_neg_fp32_64 = 1ull << 17,
   label_fcanonicalize_fp32_64 = 1ull << 18,
   label_abs_fp16 = 1ull << 19,
   label_neg_fp16 = 1ull << 20,
   label_fcanonicalize_fp16 = 1ull << 21,
   /* One label for each bit size because there are packed fp32 definitions. */
   label_canonicalized_fp16 = 1ull << 22,
   label_canonicalized_fp32 = 1ull << 23,
   label_canonicalized_fp64 = 1ull << 24,
};

static constexpr uint64_t input_mod_labels =
   label_abs_fp16 | label_abs_fp32_64 | label_neg_fp16 | label_neg_fp32_64;

static constexpr uint64_t temp_labels = label_temp | label_uniform_bool | label_scc_invert |
                                        input_mod_labels | label_fcanonicalize_fp32_64 |
                                        label_fcanonicalize_fp16;

static constexpr uint64_t val_labels = label_constant | label_combined_instr;

static constexpr uint64_t canonicalized_labels =
   label_canonicalized_fp16 | label_canonicalized_fp32 | label_canonicalized_fp64;

static Label
canonicalized_label(unsigned bit_size)
{
   if (bit_size == 16)
      return label_canonicalized_fp16;
   else if (bit_size == 32)
      return label_canonicalized_fp32;
   else if (bit_size == 64)
      return label_canonicalized_fp64;
   else
      UNREACHABLE("unknown canonicalized size");
}

static_assert((temp_labels & val_labels) == 0, "labels cannot intersect");

struct ssa_info {
   uint64_t label;
   union {
      uint64_t val;
      Temp temp;
   };
   Instruction* parent_instr;

   ssa_info() : label(0) {}

   void add_label(Label new_label)
   {
      if (new_label & temp_labels) {
         label &= ~temp_labels;
         label &= ~val_labels; /* temp and val alias */
      }

      if (new_label & val_labels) {
         label &= ~val_labels;
         label &= ~temp_labels; /* temp and val alias */
      }

      label |= new_label;
   }

   void set_constant(uint64_t constant)
   {
      add_label(label_constant);
      val = constant;
   }

   bool is_constant() { return label & label_constant; }

   void set_abs(Temp abs_temp, unsigned bit_size)
   {
      assert(bit_size == 16 || bit_size == 32 || bit_size == 64);
      add_label(bit_size == 16 ? label_abs_fp16 : label_abs_fp32_64);
      temp = abs_temp;
   }

   bool is_abs(unsigned bit_size)
   {
      assert(bit_size == 16 || bit_size == 32 || bit_size == 64);
      return bit_size == 16 ? label & label_abs_fp16 : label & label_abs_fp32_64;
   }

   void set_neg(Temp neg_temp, unsigned bit_size)
   {
      assert(bit_size == 16 || bit_size == 32 || bit_size == 64);
      add_label(bit_size == 16 ? label_neg_fp16 : label_neg_fp32_64);
      temp = neg_temp;
   }

   bool is_neg(unsigned bit_size)
   {
      assert(bit_size == 16 || bit_size == 32 || bit_size == 64);
      return bit_size == 16 ? label & label_neg_fp16 : label & label_neg_fp32_64;
   }

   void set_neg_abs(Temp neg_abs_temp, unsigned bit_size)
   {
      assert(bit_size == 16 || bit_size == 32 || bit_size == 64);
      if (bit_size == 16)
         add_label((Label)((uint32_t)label_abs_fp16 | (uint32_t)label_neg_fp16));
      else
         add_label((Label)((uint32_t)label_abs_fp32_64 | (uint32_t)label_neg_fp32_64));
      temp = neg_abs_temp;
   }

   void set_temp(Temp tmp)
   {
      add_label(label_temp);
      temp = tmp;
   }

   bool is_temp() { return label & label_temp; }

   void set_combined(uint32_t pre_combine_idx)
   {
      add_label(label_combined_instr);
      val = pre_combine_idx;
   }

   bool is_combined() { return label & label_combined_instr; }

   void set_uniform_bitwise() { add_label(label_uniform_bitwise); }

   bool is_uniform_bitwise() { return label & label_uniform_bitwise; }

   void set_scc_needed() { add_label(label_scc_needed); }

   bool is_scc_needed() { return label & label_scc_needed; }

   void set_scc_invert(Temp scc_inv)
   {
      add_label(label_scc_invert);
      temp = scc_inv;
   }

   bool is_scc_invert() { return label & label_scc_invert; }

   void set_uniform_bool(Temp uniform_bool)
   {
      add_label(label_uniform_bool);
      temp = uniform_bool;
   }

   bool is_uniform_bool() { return label & label_uniform_bool; }

   void set_fcanonicalize(Temp tmp, unsigned bit_size)
   {
      assert(bit_size == 16 || bit_size == 32 || bit_size == 64);
      add_label(bit_size == 16 ? label_fcanonicalize_fp16 : label_fcanonicalize_fp32_64);
      temp = tmp;
   }

   bool is_fcanonicalize(unsigned bit_size)
   {
      assert(bit_size == 16 || bit_size == 32 || bit_size == 64);
      return bit_size == 16 ? label & label_fcanonicalize_fp16
                            : label & label_fcanonicalize_fp32_64;
   }

   void set_canonicalized(unsigned bit_size) { add_label(canonicalized_label(bit_size)); }

   bool is_canonicalized(unsigned bit_size) { return label & canonicalized_label(bit_size); }

   void set_extract() { add_label(label_extract); }

   bool is_extract() { return label & label_extract; }
};

struct opt_ctx {
   Program* program;
   float_mode fp_mode;
   std::vector<aco_ptr<Instruction>> instructions;
   std::vector<ssa_info> info;
   std::vector<aco_ptr<Instruction>> pre_combine_instrs;
   std::vector<uint16_t> uses;
   std::unordered_map<Instruction*, aco_ptr<Instruction>> replacement_instr;
};

aco_type
get_canonical_operand_type(aco_opcode opcode, unsigned idx)
{
   aco_type type = instr_info.alu_opcode_infos[(int)opcode].op_types[idx];

   if (type.bit_size == 8 && type.num_components > 1) {
      /* Handling packed fp8/bf8 as non vector is easier. */
      type.bit_size *= type.num_components;
      type.num_components = 1;
      type.base_type = aco_base_type_none;
   }

   return type;
}

bool
dpp16_ctrl_uses_bc(uint16_t dpp_ctrl)
{
   if (dpp_ctrl >= dpp_row_sl(1) && dpp_ctrl <= dpp_row_sl(15))
      return true;
   if (dpp_ctrl >= dpp_row_sr(1) && dpp_ctrl <= dpp_row_sr(15))
      return true;
   if (dpp_ctrl == dpp_wf_sl1 || dpp_ctrl == dpp_wf_sr1)
      return true;
   if (dpp_ctrl == dpp_row_bcast15 || dpp_ctrl == dpp_row_bcast31)
      return true;
   return false;
}

struct alu_opt_op {
   Operand op;
   SubdwordSel extract[2] = {SubdwordSel::dword, SubdwordSel::dword};
   union {
      uint16_t _modifiers = 0;
      bitfield_array8<uint16_t, 0, 2> neg;
      bitfield_array8<uint16_t, 2, 2> abs;
      bitfield_bool<uint16_t, 4> f16_to_f32;
      bitfield_bool<uint16_t, 5> dot_sext;
      bitfield_bool<uint16_t, 6> dpp16;
      bitfield_bool<uint16_t, 7> dpp8;
      bitfield_bool<uint16_t, 8> bc;
      bitfield_bool<uint16_t, 9> fi;
   };
   uint32_t dpp_ctrl = 0;

   alu_opt_op& operator=(const alu_opt_op& other)
   {
      memmove((void*)this, &other, sizeof(*this));

      return *this;
   }

   alu_opt_op() = default;
   alu_opt_op(Operand _op) : op(_op) {};
   alu_opt_op(const alu_opt_op& other) { *this = other; }

   uint64_t constant_after_mods(opt_ctx& ctx, aco_type type) const
   {
      assert(this->op.isConstant());
      uint64_t res = 0;
      for (unsigned comp = 0; comp < type.num_components; comp++) {
         uint64_t part = this->op.constantValue64();
         /* 16bit negative int inline constants are sign extended, constantValue16 handles that. */
         if (this->op.bytes() == 2)
            part = this->op.constantValue16(false) | (this->op.constantValue16(true) << 16);

         if (type.bytes() <= 4) {
            SubdwordSel sel = this->extract[comp];
            part = part >> (sel.offset() * 8);
            if (sel.size() < 4) {
               part &= BITFIELD_MASK(sel.size() * 8);
               part = sel.sign_extend() ? util_sign_extend(part, sel.size() * 8) : part;
            }
         }

         if (this->f16_to_f32) {
            if (!(ctx.fp_mode.denorm16_64 & fp_denorm_keep_in)) {
               uint32_t absv = part & 0x7fff;
               if (absv <= 0x3ff)
                  part &= 0x8000;
            }
            part = fui(_mesa_half_to_float(part));
         }

         part &= BITFIELD64_MASK(type.bit_size - this->abs[comp]);
         part ^= this->neg[comp] ? BITFIELD64_BIT(type.bit_size - 1) : 0;
         res |= part << (type.bit_size * comp);
      }

      return res;
   }
};

struct alu_opt_info {
   aco::small_vec<Definition, 2> defs;
   aco::small_vec<alu_opt_op, 4> operands;
   aco_opcode opcode;
   Format format;
   uint32_t imm;
   uint32_t pass_flags; /* exec id */

   /* defs[0] modifiers */
   uint8_t omod = 0;
   bool clamp = false;
   bool f32_to_f16 = false;
   SubdwordSel insert = SubdwordSel::dword;

   bool try_swap_operands(unsigned idx0, unsigned idx1)
   {
      aco_opcode new_opcode = get_swapped_opcode(opcode, idx0, idx1);
      if (new_opcode != aco_opcode::num_opcodes) {
         opcode = new_opcode;
         std::swap(operands[idx0], operands[idx1]);
         return true;
      }
      return false;
   }

   bool uses_insert() const
   {
      return defs[0].size() == 1 && (insert.offset() != 0 || insert.size() < defs[0].bytes());
   }
};

bool
at_most_6lsb_used(aco_opcode op, unsigned idx)
{
   if (op == aco_opcode::v_writelane_b32 || op == aco_opcode::v_writelane_b32_e64 ||
       op == aco_opcode::v_readlane_b32 || op == aco_opcode::v_readlane_b32_e64)
      return idx == 1;
   return false;
}

unsigned
bytes_used(opt_ctx& ctx, alu_opt_info& info, unsigned idx)
{
   unsigned used = 4;
   aco_type type = get_canonical_operand_type(info.opcode, idx);
   if (type.bytes() == 0)
      return 4;
   used = MIN2(used, type.bytes());
   if (info.opcode == aco_opcode::v_lshlrev_b32 && idx == 1 && info.operands[0].op.isConstant()) {
      unsigned shift = info.operands[0].op.constantValue() & 0x1f;
      if (shift >= 16)
         used = MIN2(used, 2);
      if (shift >= 24)
         used = MIN2(used, 1);
   }
   return used;
}

bool
optimize_constants(opt_ctx& ctx, alu_opt_info& info)
{
   /* inline constants, pack literals */
   uint32_t literal = 0;
   unsigned litbits_used = 0;
   bool force_f2f32 = false;
   for (unsigned i = 0; i < info.operands.size(); i++) {
      auto& op_info = info.operands[i];
      assert(!op_info.op.isUndefined());
      if (!op_info.op.isConstant())
         continue;

      aco_type type = get_canonical_operand_type(info.opcode, i);

      if (type.num_components != 1 && type.num_components != 2)
         return false;
      if (!type.constant_bits())
         return false;

      if (type.bytes() > 4) {
         if (!op_info.op.isLiteral())
            continue;

         int64_t constant = op_info.op.constantValue64();
         if (type.base_type == aco_base_type_float)
            return false; /* Operand doesn't support double literal yet. */
         else if (type.base_type == aco_base_type_int && constant >= 0x7fff'ffff)
            return false;
         else if (type.base_type != aco_base_type_int && constant < 0)
            return false;

         uint32_t constant32 = op_info.op.constantValue();
         if (literal != (constant32 & BITFIELD_MASK(litbits_used)))
            return false;
         literal = constant32;
         litbits_used = 32;
         continue;
      }

      /* remove modifiers on constants: apply extract, f2f32, abs, neg */
      assert(op_info.op.size() == 1);
      uint32_t constant = op_info.constant_after_mods(ctx, type);
      op_info.op = Operand();
      for (unsigned comp = 0; comp < type.num_components; comp++) {
         op_info.extract[comp] = SubdwordSel(type.bit_size / 8, comp * type.bit_size / 8, false);
         op_info.f16_to_f32 = false;
         op_info.neg[comp] = false;
         op_info.abs[comp] = false;
      }

      if (at_most_6lsb_used(info.opcode, i))
         constant &= 0x3f;

      bool can_use_mods = can_use_input_modifiers(ctx.program->gfx_level, info.opcode, i);

      /* inline constants */
      if (type.num_components == 1) {
         Operand new_op =
            Operand::get_const(ctx.program->gfx_level, constant, type.constant_bits() / 8);
         Operand neg_op =
            Operand::get_const(ctx.program->gfx_level, BITFIELD_BIT(type.bit_size - 1) ^ constant,
                               type.constant_bits() / 8);
         Operand sext_op = Operand::get_const(ctx.program->gfx_level, 0xffff0000 | constant,
                                              type.constant_bits() / 8);
         if (!new_op.isLiteral()) {
            op_info.op = new_op;
         } else if (can_use_mods && !neg_op.isLiteral()) {
            op_info.op = neg_op;
            op_info.neg[0] = true;
         } else if (type.bit_size == 16 && !sext_op.isLiteral()) {
            op_info.op = sext_op;
         }
         // TODO opsel?
      } else if (info.format == Format::VOP3P) {
         assert(!can_use_mods || type.constant_bits() == 16);
         unsigned num_methods = (type.constant_bits() == 32 ? 5 : 1);
         for (unsigned hi = 0; op_info.op.isUndefined() && hi < 2; hi++) {
            for (unsigned negate = 0;
                 op_info.op.isUndefined() && (negate <= unsigned(can_use_mods)); negate++) {
               for (unsigned method = 0; op_info.op.isUndefined() && method < num_methods;
                    method++) {
                  uint32_t candidate = ((constant >> (hi * 16)) & 0xffff) ^ (negate ? 0x8000 : 0);
                  switch (method) {
                  case 0: break;                                /* try directly as constant */
                  case 1: candidate |= 0xffff0000; break;       /* sign extend */
                  case 2: candidate |= 0x3e220000; break;       /* 0.5pi */
                  case 3: candidate = (candidate << 16); break; /* high half */
                  case 4: candidate = (candidate << 16) | 0xf983; break; /* high half, 0.5pi. */
                  default: UNREACHABLE("impossible");
                  }
                  Operand new_op = Operand::get_const(ctx.program->gfx_level, candidate,
                                                      type.constant_bits() / 8);
                  if (new_op.isLiteral())
                     continue;

                  for (unsigned opsel = 0; op_info.op.isUndefined() && opsel < 2; opsel++) {
                     uint16_t other = constant >> (!hi * 16);
                     uint16_t abs_mask = 0xffffu >> unsigned(can_use_mods);
                     if ((new_op.constantValue16(opsel) & abs_mask) != (other & abs_mask))
                        continue;
                     op_info.op = new_op;
                     op_info.extract[hi] = method >= 3 ? SubdwordSel::uword1 : SubdwordSel::uword0;
                     op_info.extract[!hi] = opsel ? SubdwordSel::uword1 : SubdwordSel::uword0;
                     op_info.neg[hi] = negate;
                     op_info.neg[!hi] = new_op.constantValue16(opsel) ^ other;
                  }
               }
            }
         }
      }

      /* we found an inline constant */
      if (!op_info.op.isUndefined())
         continue;

      bool use_swizzle = type.num_components == 2 && info.format == Format::VOP3P;
      bool try_neg = can_use_mods && (type.num_components == 1 || use_swizzle);
      unsigned comp_bits = use_swizzle ? type.bit_size : type.bytes() * 8;
      assert(comp_bits == 32 || comp_bits == 16);
      uint32_t abs_mask = BITFIELD_MASK(comp_bits - try_neg);
      for (unsigned comp = 0; comp <= unsigned(use_swizzle); comp++) {
         uint32_t part = constant >> (comp * comp_bits) & BITFIELD_MASK(comp_bits);

         /* Try to re-use another literal, or part of it. */
         bool found_part = false;
         for (unsigned litcomp = 0; litcomp < (litbits_used / comp_bits); litcomp++) {
            uint32_t litpart = literal >> (litcomp * comp_bits) & BITFIELD_MASK(comp_bits);
            if ((litpart & abs_mask) == (part & abs_mask)) {
               op_info.neg[comp] = litpart ^ part;
               op_info.extract[comp] = SubdwordSel(comp_bits / 8, litcomp * (comp_bits / 8), false);
               found_part = true;
            }
         }

         if (found_part)
            continue;

         /* If there isn't enough space for more literal data, try to use fp16 or return false. */
         litbits_used = align(litbits_used, comp_bits);
         if (litbits_used + comp_bits > 32) {
            if (comp_bits == 32 && !force_f2f32) {
               float f32s[] = {uif(literal), uif(constant)};
               literal = 0;
               for (unsigned fltidx = 0; fltidx < 2; fltidx++) {
                  uint32_t fp16_val = _mesa_float_to_half(f32s[fltidx]);
                  bool is_denorm = (fp16_val & 0x7fff) != 0 && (fp16_val & 0x7fff) <= 0x3ff;
                  if (_mesa_half_to_float(fp16_val) != f32s[fltidx] ||
                      (is_denorm && !(ctx.fp_mode.denorm16_64 & fp_denorm_keep_in)))
                     return false;
                  literal |= fp16_val << (fltidx * 16);
               }
               force_f2f32 = true;
               op_info.extract[0] = SubdwordSel::uword1;
               break;
            }
            return false;
         }

         literal |= part << litbits_used;
         op_info.extract[comp] = SubdwordSel(comp_bits / 8, litbits_used / 8, false);
         litbits_used += comp_bits;
      }
   }

   for (auto& op_info : info.operands) {
      if (!op_info.op.isUndefined())
         continue;
      op_info.op = Operand::literal32(literal);
      op_info.f16_to_f32 = force_f2f32;
   }

   return true;
}

Format
format_combine(Format f1, Format f2)
{
   return (Format)((uint32_t)f1 | (uint32_t)f2);
}

bool
format_is(Format f1, Format f2)
{
   return ((Format)((uint32_t)f1 & (uint32_t)f2)) == f2;
}

bool
try_vinterp_inreg(opt_ctx& ctx, alu_opt_info& info)
{
   if (ctx.program->gfx_level < GFX11 || info.opcode != aco_opcode::v_fma_f32 || info.omod)
      return false;

   bool fp16 = info.f32_to_f16;
   for (auto& op_info : info.operands) {
      if (op_info.abs[0] || op_info.dpp8 || (op_info.dpp16 && !op_info.fi))
         return false;
      fp16 |= op_info.f16_to_f32;
      if (!op_info.op.isOfType(RegType::vgpr))
         return false;
   }

   if (info.operands[0].dpp16 == info.operands[1].dpp16)
      return false;

   bool swap = info.operands[1].dpp16;
   bool p2 = !info.operands[2].dpp16;

   if (fp16) {
      if (info.f32_to_f16 != p2 || !info.operands[swap].f16_to_f32 ||
          info.operands[!swap].f16_to_f32 || info.operands[2].f16_to_f32 == p2)
         return false;
   }

   if (p2) {
      if (info.operands[swap].dpp_ctrl != dpp_quad_perm(2, 2, 2, 2))
         return false;
      info.opcode =
         fp16 ? aco_opcode::v_interp_p2_f16_f32_inreg : aco_opcode::v_interp_p2_f32_inreg;
   } else {
      if (info.operands[2].dpp_ctrl != dpp_quad_perm(0, 0, 0, 0))
         return false;
      if (info.operands[swap].dpp_ctrl != dpp_quad_perm(1, 1, 1, 1))
         return false;
      info.opcode =
         fp16 ? aco_opcode::v_interp_p10_f16_f32_inreg : aco_opcode::v_interp_p10_f32_inreg;
   }

   info.f32_to_f16 = false;
   for (auto& op_info : info.operands) {
      op_info.dpp16 = false;
      op_info.f16_to_f32 = false;
   }

   if (swap)
      std::swap(info.operands[0], info.operands[1]);

   info.format = Format::VINTERP_INREG;
   return true;
}

/* Determine if this alu_opt_info can be represented by a valid ACO IR instruction.
 * info is modified to not duplicate work when it's converted to an ACO IR instruction.
 * If false is returned, info must no longer be used.
 */
bool
alu_opt_info_is_valid(opt_ctx& ctx, alu_opt_info& info)
{
   info.format = instr_info.format[(int)info.opcode];

   /* remove dpp if possible, abort in some unsupported cases (bc with sgpr, constant.) */
   for (auto& op_info : info.operands) {
      if (!op_info.dpp16 && !op_info.dpp8)
         continue;
      if (op_info.op.isOfType(RegType::vgpr))
         continue;
      /* bc=0: undefined if inactive read (lane disabled, but that's not expressed in SSA)
       * if fi=1, bc only matters for a few dpp16 options
       */
      if (op_info.bc && (!op_info.fi || (op_info.dpp16 && dpp16_ctrl_uses_bc(op_info.dpp_ctrl))))
         return false;
      op_info.dpp16 = false;
      op_info.dpp8 = false;
   }

   /* if mul, push neg to constant, eliminate double negate */
   switch (info.opcode) {
   case aco_opcode::v_mul_f64_e64:
   case aco_opcode::v_mul_f64:
   case aco_opcode::v_mul_f32:
   case aco_opcode::v_mul_legacy_f32:
   case aco_opcode::v_mul_f16:
   case aco_opcode::v_mad_f32:
   case aco_opcode::v_mad_legacy_f32:
   case aco_opcode::v_mad_f16:
   case aco_opcode::v_mad_legacy_f16:
   case aco_opcode::v_fma_f64:
   case aco_opcode::v_fma_f32:
   case aco_opcode::v_fma_legacy_f32:
   case aco_opcode::v_fma_f16:
   case aco_opcode::v_fma_legacy_f16:
   case aco_opcode::v_fma_mix_f32:
   case aco_opcode::v_fma_mixlo_f16:
   case aco_opcode::v_pk_mul_f16:
   case aco_opcode::v_pk_fma_f16:
   case aco_opcode::s_mul_f32:
   case aco_opcode::s_mul_f16:
   case aco_opcode::s_fmac_f32:
   case aco_opcode::s_fmac_f16:
      for (unsigned comp = 0; comp < 2; comp++) {
         for (unsigned i = 0; i < 2; i++) {
            if (info.operands[!i].op.isConstant() || info.operands[!i].neg[comp]) {
               info.operands[!i].neg[comp] ^= info.operands[i].neg[comp];
               info.operands[i].neg[comp] = false;
            }
         }
      }
      break;
   default: break;
   }

   if (!optimize_constants(ctx, info))
      return false;

   /* check constant bus limit */
   bool is_salu = false;
   switch (info.format) {
   case Format::SOPC:
   case Format::SOPK:
   case Format::SOP1:
   case Format::SOP2:
   case Format::SOPP: is_salu = true; break;
   default: break;
   }
   int constant_limit = is_salu ? INT_MAX : (ctx.program->gfx_level >= GFX10 ? 2 : 1);

   switch (info.opcode) {
   case aco_opcode::v_writelane_b32:
   case aco_opcode::v_writelane_b32_e64: constant_limit = INT_MAX; break;
   case aco_opcode::v_lshlrev_b64:
   case aco_opcode::v_lshlrev_b64_e64:
   case aco_opcode::v_lshrrev_b64:
   case aco_opcode::v_ashrrev_i64: constant_limit = 1; break;
   default: break;
   }

   for (unsigned i = 0; i < info.operands.size(); i++) {
      const Operand& op = info.operands[i].op;
      if (!op.isLiteral() && !op.isOfType(RegType::sgpr))
         continue;

      constant_limit--;
      for (unsigned j = 0; j < i; j++) {
         const Operand& other = info.operands[j].op;
         if (op == other) {
            constant_limit++;
            break;
         }
      }
   }

   if (constant_limit < 0)
      return false;

   /* apply extract. */
   if (info.opcode == aco_opcode::s_pack_ll_b32_b16) {
      if (info.operands[0].extract[0].size() < 2 || info.operands[1].extract[0].size() < 2)
         return false;
      if (info.operands[0].extract[0].offset() == 2 && info.operands[1].extract[0].offset() == 2) {
         info.opcode = aco_opcode::s_pack_hh_b32_b16;
      } else if (info.operands[0].extract[0].offset() == 0 &&
                 info.operands[1].extract[0].offset() == 2) {
         info.opcode = aco_opcode::s_pack_lh_b32_b16;
      } else if (info.operands[0].extract[0].offset() == 2 &&
                 info.operands[1].extract[0].offset() == 0) {
         if (ctx.program->gfx_level < GFX11) /* TODO try shifting constant */
            return false;
         info.opcode = aco_opcode::s_pack_hl_b32_b16;
      }
      info.operands[0].extract[0] = SubdwordSel::dword;
      info.operands[1].extract[0] = SubdwordSel::dword;
   }

   for (unsigned i = 0; i < info.operands.size(); i++) {
      aco_type type = get_canonical_operand_type(info.opcode, i);
      if (type.bit_size == 16 && type.num_components == 2) {
         for (unsigned comp = 0; comp < 2; comp++) {
            SubdwordSel sel = info.operands[i].extract[comp];
            if (sel.size() < 2)
               return false;
            if (info.format != Format::VOP3P && sel.offset() != 2 * comp)
               return false;
         }
         continue;
      }
      SubdwordSel sel = info.operands[i].extract[0];
      if (sel.size() == 4) {
         continue;
      } else if (info.operands[i].f16_to_f32 && sel.size() < 2) {
         return false;
      } else if (info.operands[i].f16_to_f32 && sel.size() == 2) {
         continue;
      } else if (sel.offset() == 0 && sel.size() >= bytes_used(ctx, info, i)) {
         info.operands[i].extract[0] = SubdwordSel::dword;
      } else if ((info.opcode == aco_opcode::v_cvt_f32_u32 ||
                  info.opcode == aco_opcode::v_cvt_f32_i32) &&
                 sel.size() == 1 && !sel.sign_extend()) {
         switch (sel.offset()) {
         case 0: info.opcode = aco_opcode::v_cvt_f32_ubyte0; break;
         case 1: info.opcode = aco_opcode::v_cvt_f32_ubyte1; break;
         case 2: info.opcode = aco_opcode::v_cvt_f32_ubyte2; break;
         case 3: info.opcode = aco_opcode::v_cvt_f32_ubyte3; break;
         default: UNREACHABLE("invalid SubdwordSel");
         }
         info.operands[i].extract[0] = SubdwordSel::dword;
         continue;
      } else if (info.opcode == aco_opcode::v_mul_u32_u24 && ctx.program->gfx_level >= GFX10 &&
                 sel.size() == 2 && !sel.sign_extend() &&
                 !info.operands[!i].extract[0].sign_extend() &&
                 info.operands[!i].extract[0].size() >= 2 &&
                 (info.operands[!i].op.is16bit() || info.operands[!i].extract[0].size() == 2 ||
                  (info.operands[!i].op.isConstant() &&
                   info.operands[!i].op.constantValue() <= UINT16_MAX))) {
         info.opcode = aco_opcode::v_mad_u32_u16;
         info.format = Format::VOP3;
         info.operands.push_back(alu_opt_op{});
         info.operands[2].op = Operand::c32(0);
         continue;
      } else if (i < 2 && ctx.program->gfx_level >= GFX8 && ctx.program->gfx_level < GFX11 &&
                 (format_is(info.format, Format::VOPC) || format_is(info.format, Format::VOP2) ||
                  format_is(info.format, Format::VOP1))) {
         info.format = format_combine(info.format, Format::SDWA);
         continue;
      } else if (sel.size() == 2 && can_use_opsel(ctx.program->gfx_level, info.opcode, i)) {
         continue;
      } else if (info.opcode == aco_opcode::s_cvt_f32_f16 && sel.size() == 2 && sel.offset() == 2) {
         info.opcode = aco_opcode::s_cvt_hi_f32_f16;
         info.operands[i].extract[0] = SubdwordSel::dword;
         continue;
      } else {
         return false;
      }
   }

   /* convert to VINTERP_INREG */
   try_vinterp_inreg(ctx, info);

   /* convert to v_fma_mix */
   bool uses_f2f32 = false;
   for (auto& op_info : info.operands)
      uses_f2f32 |= op_info.f16_to_f32;

   if (uses_f2f32 || info.f32_to_f16) {
      if (ctx.program->gfx_level < GFX9)
         return false;

      /* unfused v_mad_mix* always flushes 16/32-bit denormal inputs/outputs */
      if (!ctx.program->dev.fused_mad_mix && ctx.fp_mode.denorm)
         return false;

      switch (info.opcode) {
      case aco_opcode::v_add_f32:
         info.operands.insert(info.operands.begin(), alu_opt_op{});
         info.operands[0].op = Operand::c32(0x3f800000);
         break;
      case aco_opcode::v_mul_f32:
         info.operands.push_back(alu_opt_op{});
         info.operands[2].op = Operand::c32(0);
         info.operands[2].neg[0] = true;
         break;
      case aco_opcode::v_fma_f32:
         if (!ctx.program->dev.fused_mad_mix)
            return false;
         break;
      case aco_opcode::v_mad_f32:
         if (ctx.program->dev.fused_mad_mix && info.defs[0].isPrecise())
            return false;
         break;
      default: return false;
      }

      info.opcode = info.f32_to_f16 ? aco_opcode::v_fma_mixlo_f16 : aco_opcode::v_fma_mix_f32;
      info.format = Format::VOP3P;
   }

   /* remove negate modifiers by converting to subtract */
   aco_opcode sub = aco_opcode::num_opcodes;
   aco_opcode subrev = aco_opcode::num_opcodes;
   switch (info.opcode) {
   case aco_opcode::v_add_f32:
      sub = aco_opcode::v_sub_f32;
      subrev = aco_opcode::v_subrev_f32;
      break;
   case aco_opcode::v_add_f16:
      sub = aco_opcode::v_sub_f16;
      subrev = aco_opcode::v_subrev_f16;
      break;
   case aco_opcode::s_add_f32: sub = aco_opcode::s_sub_f32; break;
   case aco_opcode::s_add_f16: sub = aco_opcode::s_sub_f16; break;
   default: break;
   }

   if (sub != aco_opcode::num_opcodes && (info.operands[0].neg[0] ^ info.operands[1].neg[0])) {
      if (info.operands[1].neg[0]) {
         info.opcode = sub;
      } else if (subrev != aco_opcode::num_opcodes) {
         info.opcode = subrev;
      } else {
         info.opcode = sub;
         std::swap(info.operands[0], info.operands[1]);
      }

      info.operands[0].neg[0] = false;
      info.operands[1].neg[0] = false;
   }

   /* convert to DPP */
   bool is_dpp = false;
   for (unsigned i = 0; i < info.operands.size(); i++) {
      if (info.operands[i].dpp16 || info.operands[i].dpp8) {
         if (is_dpp || !info.try_swap_operands(0, i))
            return false;

         is_dpp = true;
         if (info.operands[0].dpp16)
            info.format = format_combine(info.format, Format::DPP16);
         else if (info.operands[0].dpp8)
            info.format = format_combine(info.format, Format::DPP8);
      }
   }
   if (is_dpp && info.operands.size() > 2 && !info.operands[1].op.isOfType(RegType::vgpr) &&
       info.operands[2].op.isOfType(RegType::vgpr))
      info.try_swap_operands(1, 2);
   if (is_dpp && info.operands.size() > 1 && !info.operands[1].op.isOfType(RegType::vgpr))
      return false; /* TODO: gfx11.5 */

   /* dst SDWA */
   if (info.insert != SubdwordSel::dword) {
      if (!info.uses_insert()) {
         info.insert = SubdwordSel::dword;
      } else if (info.defs[0].bytes() != 4 ||
                 (!format_is(info.format, Format::VOP1) && !format_is(info.format, Format::VOP2)) ||
                 ctx.program->gfx_level < GFX8 || ctx.program->gfx_level >= GFX11) {
         return false;
      } else {
         info.format = format_combine(info.format, Format::SDWA);
      }
   }

   /* DPP and SDWA can't be used at the same time. */
   if (is_dpp && format_is(info.format, Format::SDWA))
      return false;

   bool is_dpp_or_sdwa = is_dpp || format_is(info.format, Format::SDWA);

   bitarray8 neg = 0;
   bitarray8 abs = 0;
   bitarray8 opsel = 0;
   bitarray8 vmask = 0;
   bitarray8 smask = 0;
   bitarray8 cmask = 0;
   bitarray8 lmask = 0;

   for (unsigned i = 0; i < info.operands.size(); i++) {
      aco_type type = get_canonical_operand_type(info.opcode, i);
      bool can_use_mods = can_use_input_modifiers(ctx.program->gfx_level, info.opcode, i);
      const auto& op_info = info.operands[i];

      if (!format_is(info.format, Format::VOP3P) && type.num_components == 2 &&
          (op_info.neg[0] != op_info.neg[1] || op_info.abs[0] != op_info.abs[1]))
         return false;

      for (unsigned comp = 0; comp < type.num_components; comp++) {
         if (!can_use_mods && (op_info.neg[comp] || op_info.abs[comp]))
            return false;
         abs[i] |= op_info.abs[comp];
         neg[i] |= op_info.neg[comp];
      }
      opsel[i] = op_info.extract[0].offset();
      vmask[i] = op_info.op.isOfType(RegType::vgpr);
      smask[i] = op_info.op.isOfType(RegType::sgpr);
      cmask[i] = op_info.op.isConstant();
      lmask[i] = op_info.op.isLiteral();

      /* lane masks must be sgpr */
      if (type.bit_size == 1 && !smask[i])
         return false;

      /* DPP/SDWA doesn't allow 64bit opcodes. */
      if (is_dpp_or_sdwa && info.operands[i].op.size() != 1 && type.bit_size != 1)
         return false;
   }

   /* DPP/SDWA doesn't allow 64bit opcodes. */
   if (is_dpp_or_sdwa && !format_is(info.format, Format::VOPC) && info.defs[0].size() != 1)
      return false;

   if (format_is(info.format, Format::VOP1) || format_is(info.format, Format::VOP2) ||
       format_is(info.format, Format::VOPC) || format_is(info.format, Format::VOP3)) {
      bool needs_vop3 = false;
      if (info.omod && format_is(info.format, Format::SDWA) && ctx.program->gfx_level < GFX9)
         return false;

      if (info.omod && !format_is(info.format, Format::SDWA))
         needs_vop3 = true;

      if (info.clamp && format_is(info.format, Format::SDWA) &&
          format_is(info.format, Format::VOPC) && ctx.program->gfx_level >= GFX9)
         return false;

      if ((info.clamp || (opsel & ~vmask)) && !format_is(info.format, Format::SDWA))
         needs_vop3 = true;

      if (!format_is(info.format, Format::SDWA) && !format_is(info.format, Format::DPP16) &&
          (abs || neg))
         needs_vop3 = true;

      if (((cmask | smask) & 0x3) && format_is(info.format, Format::SDWA) &&
          ctx.program->gfx_level == GFX8)
         return false;

      aco_opcode mulk = aco_opcode::num_opcodes;
      aco_opcode addk = aco_opcode::num_opcodes;
      switch (info.opcode) {
      case aco_opcode::v_s_exp_f16:
      case aco_opcode::v_s_log_f16:
      case aco_opcode::v_s_rcp_f16:
      case aco_opcode::v_s_rsq_f16:
      case aco_opcode::v_s_sqrt_f16:
         /* These can't use inline constants on GFX12 but can use literals. We don't bother since
          * they should be constant folded anyway. */
         if (cmask)
            return false;
         FALLTHROUGH;
      case aco_opcode::v_s_exp_f32:
      case aco_opcode::v_s_log_f32:
      case aco_opcode::v_s_rcp_f32:
      case aco_opcode::v_s_rsq_f32:
      case aco_opcode::v_s_sqrt_f32:
         if (vmask)
            return false;
         break;
      case aco_opcode::v_writelane_b32:
      case aco_opcode::v_writelane_b32_e64:
         if ((vmask & 0x3) || (~vmask & 0x4))
            return false;
         if (is_dpp || format_is(info.format, Format::SDWA))
            return false;
         break;
      case aco_opcode::v_permlane16_b32:
      case aco_opcode::v_permlanex16_b32:
      case aco_opcode::v_permlane64_b32:
      case aco_opcode::v_readfirstlane_b32:
      case aco_opcode::v_readlane_b32:
      case aco_opcode::v_readlane_b32_e64:
         if ((~vmask & 0x1) || (vmask & 0x6))
            return false;
         if (is_dpp || format_is(info.format, Format::SDWA))
            return false;
         break;
      case aco_opcode::v_mul_lo_u32:
      case aco_opcode::v_mul_lo_i32:
      case aco_opcode::v_mul_hi_u32:
      case aco_opcode::v_mul_hi_i32:
         if (is_dpp)
            return false;
         break;
      case aco_opcode::v_fma_f32:
         if (ctx.program->gfx_level >= GFX10) {
            mulk = aco_opcode::v_fmamk_f32;
            addk = aco_opcode::v_fmaak_f32;
         }
         break;
      case aco_opcode::v_fma_f16:
      case aco_opcode::v_fma_legacy_f16:
         if (ctx.program->gfx_level >= GFX10) {
            mulk = aco_opcode::v_fmamk_f16;
            addk = aco_opcode::v_fmaak_f16;
         }
         break;
      case aco_opcode::v_mad_f32:
         mulk = aco_opcode::v_madmk_f32;
         addk = aco_opcode::v_madak_f32;
         break;
      case aco_opcode::v_mad_f16:
      case aco_opcode::v_mad_legacy_f16:
         mulk = aco_opcode::v_madmk_f16;
         addk = aco_opcode::v_madak_f16;
         break;
      default:
         if ((smask[1] || cmask[1]) && !needs_vop3 && !format_is(info.format, Format::VOP3) &&
             !format_is(info.format, Format::SDWA)) {
            if (is_dpp || !vmask[0] || !info.try_swap_operands(0, 1))
               needs_vop3 = true;
         }
         if (needs_vop3)
            info.format = format_combine(info.format, Format::VOP3);
      }

      if (addk != aco_opcode::num_opcodes && vmask && lmask && !needs_vop3 &&
          (vmask[2] || lmask[2]) && (!opsel || ctx.program->gfx_level >= GFX11)) {
         for (int i = 2; i >= 0; i--) {
            if (lmask[i]) {
               if (i == 0 || (i == 2 && !vmask[1]))
                  std::swap(info.operands[0], info.operands[1]);
               if (i != 2)
                  std::swap(info.operands[1], info.operands[2]);
               info.opcode = i == 2 ? addk : mulk;
               info.format = Format::VOP2;
               break;
            }
         }
      }

      bool nolit = format_is(info.format, Format::SDWA) || is_dpp ||
                   (format_is(info.format, Format::VOP3) && ctx.program->gfx_level < GFX10);
      if (nolit && lmask)
         return false;
      if (is_dpp && format_is(info.format, Format::VOP3) && ctx.program->gfx_level < GFX11)
         return false;

      /* Fix lane mask src/dst to vcc if the format requires it. */
      if (ctx.program->gfx_level < GFX11 && (is_dpp || format_is(info.format, Format::SDWA))) {
         if (format_is(info.format, Format::VOP2)) {
            if (info.operands.size() > 2)
               info.operands[2].op.setPrecolored(vcc);
            if (info.defs.size() > 1)
               info.defs[1].setPrecolored(vcc);
         }
         if (format_is(info.format, Format::VOPC) && (is_dpp || ctx.program->gfx_level < GFX9) &&
             !info.defs[0].isFixed())
            info.defs[0].setPrecolored(vcc);
      }
   } else if (format_is(info.format, Format::VOP3P)) {
      bool fmamix =
         info.opcode == aco_opcode::v_fma_mix_f32 || info.opcode == aco_opcode::v_fma_mixlo_f16;
      bool dot2_f32 =
         info.opcode == aco_opcode::v_dot2_f32_f16 || info.opcode == aco_opcode::v_dot2_f32_bf16;
      bool supports_dpp = (fmamix || dot2_f32) && ctx.program->gfx_level >= GFX11;
      if ((abs && !fmamix) || (is_dpp && !supports_dpp) || info.omod)
         return false;
      if (lmask && (ctx.program->gfx_level < GFX10 || is_dpp))
         return false;
   } else if (is_salu) {
      if (vmask)
         return false;
      if (info.opcode == aco_opcode::s_fmac_f32) {
         for (unsigned i = 0; i < 2; i++) {
            if (lmask[i]) {
               std::swap(info.operands[i], info.operands[1]);
               std::swap(info.operands[1], info.operands[2]);
               info.opcode = aco_opcode::s_fmamk_f32;
               break;
            }
         }
         if (info.opcode == aco_opcode::s_fmac_f32 && cmask[2]) {
            info.operands[2].op = Operand::literal32(info.operands[2].op.constantValue());
            lmask[2] = true;
            info.opcode = aco_opcode::s_fmaak_f32;
         }
      } else if (info.opcode == aco_opcode::s_fmac_f16 && !smask[2]) {
         return false;
      }
   }

   return true;
}

/* Gather semantic information about an alu instruction and its operands from an ACO IR Instruction.
 *
 * Some callers expect that the alu_opt_info created by alu_opt_gather_info() or the instruction
 * created by alu_opt_info_to_instr() does not have more uses of a temporary than the original
 * instruction did.
 */
bool
alu_opt_gather_info(opt_ctx& ctx, Instruction* instr, alu_opt_info& info)
{
   if (instr->opcode == aco_opcode::p_insert &&
       (instr->operands[1].constantValue() + 1) * instr->operands[2].constantValue() == 32) {
      info = {};
      info.pass_flags = instr->pass_flags;
      info.defs.push_back(instr->definitions[0]);
      info.operands.push_back({Operand::c32(32 - instr->operands[2].constantValue())});
      info.operands.push_back({instr->operands[0]});
      if (instr->definitions[0].regClass() == s1) {
         info.defs.push_back(instr->definitions[1]);
         info.opcode = aco_opcode::v_lshl_b32;
         info.format = Format::SOP2;
         std::swap(info.operands[0], info.operands[1]);
      } else {
         info.opcode = aco_opcode::v_lshlrev_b32;
         info.format = Format::VOP2;
      }
      return true;
   } else if ((instr->opcode == aco_opcode::p_insert ||
               (instr->opcode == aco_opcode::p_extract && instr->operands[3].constantEquals(0))) &&
              instr->operands[1].constantEquals(0)) {
      info = {};
      info.pass_flags = instr->pass_flags;
      info.defs.push_back(instr->definitions[0]);
      info.operands.push_back(
         {Operand::c32(instr->operands[2].constantEquals(8) ? 0xffu : 0xffffu)});
      info.operands.push_back({instr->operands[0]});
      if (instr->definitions[0].regClass() == s1) {
         info.defs.push_back(instr->definitions[1]);
         info.opcode = aco_opcode::s_and_b32;
         info.format = Format::SOP2;
      } else {
         info.opcode = aco_opcode::v_and_b32;
         info.format = Format::VOP2;
      }
      return true;
   }

   if (!instr->isVALU() && !instr->isSALU())
      return false;

   /* There is nothing to be gained from handling WMMA/mqsad here. */
   if (instr_info.classes[(int)instr->opcode] == instr_class::wmma ||
       instr->opcode == aco_opcode::v_mqsad_u32_u8)
      return false;

   switch (instr->opcode) {
   case aco_opcode::s_addk_i32:
   case aco_opcode::s_cmovk_i32:
   case aco_opcode::s_mulk_i32:
   case aco_opcode::v_dot2c_f32_f16:
   case aco_opcode::v_dot4c_i32_i8:
   case aco_opcode::v_fmac_f32:
   case aco_opcode::v_fmac_f16:
   case aco_opcode::v_fmac_legacy_f32:
   case aco_opcode::v_mac_f32:
   case aco_opcode::v_mac_f16:
   case aco_opcode::v_mac_legacy_f32:
   case aco_opcode::v_pk_fmac_f16: UNREACHABLE("Only created by RA."); return false;
   default: break;
   }

   info = {};

   info.opcode = instr->opcode;
   info.pass_flags = instr->pass_flags;

   if (instr->isSALU())
      info.imm = instr->salu().imm;

   bitarray8 opsel = 0;
   if (instr->isVALU()) {
      info.omod = instr->valu().omod;
      info.clamp = instr->valu().clamp;
      opsel = instr->valu().opsel;
   }

   if (instr->opcode == aco_opcode::v_permlane16_b32 ||
       instr->opcode == aco_opcode::v_permlanex16_b32) {
      info.imm = opsel;
      opsel = 0;
   }

   if (instr->opcode == aco_opcode::v_fma_mix_f32 || instr->opcode == aco_opcode::v_fma_mixlo_f16) {
      info.opcode = ctx.program->dev.fused_mad_mix ? aco_opcode::v_fma_f32 : aco_opcode::v_mad_f32;
      info.f32_to_f16 = instr->opcode == aco_opcode::v_fma_mixlo_f16;
   }

   if (instr->isSDWA())
      info.insert = instr->sdwa().dst_sel;

   for (Definition& def : instr->definitions)
      info.defs.push_back(def);

   for (unsigned i = 0; i < instr->operands.size(); i++) {
      alu_opt_op op_info = {};
      op_info.op = instr->operands[i];
      if (instr->opcode == aco_opcode::v_fma_mix_f32 ||
          instr->opcode == aco_opcode::v_fma_mixlo_f16) {
         op_info.neg[0] = instr->valu().neg[i];
         op_info.abs[0] = instr->valu().abs[i];
         if (instr->valu().opsel_hi[i]) {
            op_info.f16_to_f32 = true;
            if (instr->valu().opsel_lo[i])
               op_info.extract[0] = SubdwordSel::uword1;
         }
      } else if (instr->isVOP3P()) {
         op_info.neg[0] = instr->valu().neg_lo[i];
         op_info.neg[1] = instr->valu().neg_hi[i];
         if (instr->valu().opsel_lo[i])
            op_info.extract[0] = SubdwordSel::uword1;
         if (instr->valu().opsel_hi[i])
            op_info.extract[1] = SubdwordSel::uword1;
      } else if (instr->isVALU() && i < 3) {
         op_info.neg[0] = instr->valu().neg[i];
         op_info.neg[1] = instr->valu().neg[i];
         op_info.abs[0] = instr->valu().abs[i];
         op_info.abs[1] = instr->valu().abs[i];
         if (opsel[i])
            op_info.extract[0] = SubdwordSel::uword1;
         op_info.extract[1] = SubdwordSel::uword1;

         if (i < 2 && instr->isSDWA())
            op_info.extract[0] = instr->sdwa().sel[i];
      }

      info.operands.push_back(op_info);
   }

   if (instr->isVINTERP_INREG()) {
      switch (instr->opcode) {
      case aco_opcode::v_interp_p10_f16_f32_inreg:
         info.operands[0].f16_to_f32 = true;
         info.operands[2].f16_to_f32 = true;
         FALLTHROUGH;
      case aco_opcode::v_interp_p10_f32_inreg:
         info.operands[0].dpp_ctrl = dpp_quad_perm(1, 1, 1, 1);
         info.operands[2].dpp_ctrl = dpp_quad_perm(0, 0, 0, 0);
         info.operands[2].dpp16 = true;
         info.operands[2].fi = true;
         break;
      case aco_opcode::v_interp_p2_f16_f32_inreg:
         info.operands[0].f16_to_f32 = true;
         info.f32_to_f16 = true;
         FALLTHROUGH;
      case aco_opcode::v_interp_p2_f32_inreg:
         info.operands[0].dpp_ctrl = dpp_quad_perm(2, 2, 2, 2);
         break;
      default: return false;
      }
      info.opcode = aco_opcode::v_fma_f32;
      info.operands[0].dpp16 = true;
      info.operands[0].fi = true;
      /* Anything else doesn't make sense before scheduling. */
      assert(instr->vinterp_inreg().wait_exp == 7);
   } else if (instr->isDPP16()) {
      info.operands[0].dpp16 = true;
      info.operands[0].dpp_ctrl = instr->dpp16().dpp_ctrl;
      info.operands[0].fi = instr->dpp16().fetch_inactive;
      info.operands[0].bc = instr->dpp16().bound_ctrl;
      assert(instr->dpp16().row_mask == 0xf && instr->dpp16().bank_mask == 0xf);
   } else if (instr->isDPP8()) {
      info.operands[0].dpp8 = true;
      info.operands[0].dpp_ctrl = instr->dpp8().lane_sel;
      info.operands[0].fi = instr->dpp8().fetch_inactive;
   }

   switch (info.opcode) {
   case aco_opcode::s_cvt_hi_f32_f16:
      info.operands[0].extract[0] = SubdwordSel::uword1;
      info.opcode = aco_opcode::s_cvt_f32_f16;
      break;
   case aco_opcode::s_pack_lh_b32_b16:
   case aco_opcode::s_pack_hl_b32_b16:
   case aco_opcode::s_pack_hh_b32_b16:
      if (info.opcode != aco_opcode::s_pack_lh_b32_b16)
         info.operands[0].extract[0] = SubdwordSel::uword1;
      if (info.opcode != aco_opcode::s_pack_hl_b32_b16)
         info.operands[1].extract[0] = SubdwordSel::uword1;
      info.opcode = aco_opcode::s_pack_ll_b32_b16;
      break;
   case aco_opcode::v_sub_f32:
   case aco_opcode::v_subrev_f32:
      info.operands[info.opcode == aco_opcode::v_sub_f32].neg[0] ^= true;
      info.opcode = aco_opcode::v_add_f32;
      break;
   case aco_opcode::v_sub_f16:
   case aco_opcode::v_subrev_f16:
      info.operands[info.opcode == aco_opcode::v_sub_f16].neg[0] ^= true;
      info.opcode = aco_opcode::v_add_f16;
      break;
   case aco_opcode::s_sub_f32:
      info.operands[1].neg[0] ^= true;
      info.opcode = aco_opcode::s_add_f32;
      break;
   case aco_opcode::s_sub_f16:
      info.operands[1].neg[0] ^= true;
      info.opcode = aco_opcode::s_add_f16;
      break;
   case aco_opcode::v_dot4_i32_iu8:
   case aco_opcode::v_dot8_i32_iu4:
      for (unsigned i = 0; i < 2; i++) {
         info.operands[i].dot_sext = info.operands[i].neg[0];
         info.operands[i].neg[0] = false;
      }
      break;
   case aco_opcode::v_mad_f32:
      if (ctx.fp_mode.denorm32)
         break;
      FALLTHROUGH;
   case aco_opcode::v_fma_f32:
      if (info.operands[2].op.constantEquals(0) && info.operands[2].neg[0]) {
         info.operands.pop_back();
         info.opcode = aco_opcode::v_mul_f32;
      } else {
         for (unsigned i = 0; i < 2; i++) {
            uint32_t one = info.operands[i].f16_to_f32 ? 0x3c00 : 0x3f800000;
            if (info.operands[i].op.constantEquals(one) && !info.operands[i].neg[0] &&
                info.operands[i].extract[0] == SubdwordSel::dword) {
               info.operands.erase(info.operands.begin() + i);
               info.opcode = aco_opcode::v_add_f32;
               break;
            }
         }
      }
      break;
   case aco_opcode::v_fmaak_f32:
   case aco_opcode::v_fmamk_f32:
      if (info.opcode == aco_opcode::v_fmamk_f32)
         std::swap(info.operands[1], info.operands[2]);
      info.opcode = aco_opcode::v_fma_f32;
      break;
   case aco_opcode::v_fmaak_f16:
   case aco_opcode::v_fmamk_f16:
      if (info.opcode == aco_opcode::v_fmamk_f16)
         std::swap(info.operands[1], info.operands[2]);
      info.opcode = aco_opcode::v_fma_f16;
      break;
   case aco_opcode::v_madak_f32:
   case aco_opcode::v_madmk_f32:
      if (info.opcode == aco_opcode::v_madmk_f32)
         std::swap(info.operands[1], info.operands[2]);
      info.opcode = aco_opcode::v_mad_f32;
      break;
   case aco_opcode::v_madak_f16:
   case aco_opcode::v_madmk_f16:
      if (info.opcode == aco_opcode::v_madmk_f16)
         std::swap(info.operands[1], info.operands[2]);
      info.opcode =
         ctx.program->gfx_level == GFX8 ? aco_opcode::v_mad_legacy_f16 : aco_opcode::v_mad_f16;
      break;
   case aco_opcode::s_fmaak_f32:
   case aco_opcode::s_fmamk_f32:
      if (info.opcode == aco_opcode::s_fmamk_f32)
         std::swap(info.operands[1], info.operands[2]);
      info.opcode = aco_opcode::s_fmac_f32;
      break;
   case aco_opcode::v_subbrev_co_u32:
      std::swap(info.operands[0], info.operands[1]);
      info.opcode = aco_opcode::v_subb_co_u32;
      break;
   case aco_opcode::v_subrev_co_u32:
      std::swap(info.operands[0], info.operands[1]);
      info.opcode = aco_opcode::v_sub_co_u32;
      break;
   case aco_opcode::v_subrev_co_u32_e64:
      std::swap(info.operands[0], info.operands[1]);
      info.opcode = aco_opcode::v_sub_co_u32_e64;
      break;
   case aco_opcode::v_subrev_u32:
      std::swap(info.operands[0], info.operands[1]);
      info.opcode = aco_opcode::v_sub_u32;
      break;
   default: break;
   }

   return true;
}

/* Convert an alu_opt_info to an ACO IR instruction.
 * alu_opt_info_is_valid must have been called and returned true before this.
 * If old_instr is large enough for the new instruction, it's reused.
 * Otherwise a new instruction is allocated.
 */
Instruction*
alu_opt_info_to_instr(opt_ctx& ctx, alu_opt_info& info, Instruction* old_instr)
{
   Instruction* instr;
   if (old_instr && old_instr->definitions.size() >= info.defs.size() &&
       old_instr->operands.size() >= info.operands.size() &&
       get_instr_data_size(old_instr->format) >= get_instr_data_size(info.format)) {
      instr = old_instr;
      while (instr->operands.size() > info.operands.size())
         instr->operands.pop_back();
      while (instr->definitions.size() > info.defs.size())
         instr->definitions.pop_back();
      instr->opcode = info.opcode;
      instr->format = info.format;

      if (instr->isVALU()) {
         instr->valu().abs = 0;
         instr->valu().neg = 0;
         instr->valu().opsel = 0;
         instr->valu().opsel_hi = 0;
         instr->valu().opsel_lo = 0;
      }
   } else {
      instr = create_instruction(info.opcode, info.format, info.operands.size(), info.defs.size());
   }

   instr->pass_flags = info.pass_flags;

   for (unsigned i = 0; i < info.defs.size(); i++) {
      instr->definitions[i] = info.defs[i];
      ctx.info[info.defs[i].tempId()].parent_instr = instr;
   }

   for (unsigned i = 0; i < info.operands.size(); i++) {
      instr->operands[i] = info.operands[i].op;
      if (instr->opcode == aco_opcode::v_fma_mix_f32 ||
          instr->opcode == aco_opcode::v_fma_mixlo_f16) {
         instr->valu().neg[i] = info.operands[i].neg[0];
         instr->valu().abs[i] = info.operands[i].abs[0];
         instr->valu().opsel_hi[i] = info.operands[i].f16_to_f32;
         instr->valu().opsel_lo[i] = info.operands[i].extract[0].offset();
      } else if (instr->isVOP3P()) {
         instr->valu().neg_lo[i] = info.operands[i].neg[0] || info.operands[i].dot_sext;
         instr->valu().neg_hi[i] = info.operands[i].neg[1];
         instr->valu().opsel_lo[i] = info.operands[i].extract[0].offset();
         instr->valu().opsel_hi[i] = info.operands[i].extract[1].offset();
      } else if (instr->isVALU()) {
         instr->valu().neg[i] = info.operands[i].neg[0];
         instr->valu().abs[i] = info.operands[i].abs[0];
         if (instr->isSDWA() && i < 2) {
            SubdwordSel sel = info.operands[i].extract[0];
            unsigned size = MIN2(sel.size(), info.operands[i].op.bytes());
            instr->sdwa().sel[i] = SubdwordSel(size, sel.offset(), sel.sign_extend());
         } else if (info.operands[i].extract[0].offset()) {
            instr->valu().opsel[i] = true;
         }
      }
   }

   if (instr->isVALU()) {
      instr->valu().omod = info.omod;
      instr->valu().clamp = info.clamp;
   }

   if (instr->isVINTERP_INREG()) {
      instr->vinterp_inreg().wait_exp = 7;
   } else if (instr->isDPP16()) {
      instr->dpp16().dpp_ctrl = info.operands[0].dpp_ctrl;
      instr->dpp16().fetch_inactive = info.operands[0].fi;
      instr->dpp16().bound_ctrl = info.operands[0].bc;
      instr->dpp16().row_mask = 0xf;
      instr->dpp16().bank_mask = 0xf;
   } else if (instr->isDPP8()) {
      instr->dpp8().lane_sel = info.operands[0].dpp_ctrl;
      instr->dpp8().fetch_inactive = info.operands[0].fi;
   } else if (instr->isSDWA()) {
      instr->sdwa().dst_sel = info.insert;
      if (!instr->isVOPC() && instr->definitions[0].bytes() != 4) {
         instr->sdwa().dst_sel = SubdwordSel(instr->definitions[0].bytes(), 0, false);
         assert(instr->sdwa().dst_sel == info.insert || info.insert == SubdwordSel::dword);
      }
   } else if (instr->opcode == aco_opcode::v_permlane16_b32 ||
              instr->opcode == aco_opcode::v_permlanex16_b32) {
      instr->valu().opsel = info.imm;
   }

   if (instr->isSALU())
      instr->salu().imm = info.imm;

   return instr;
}

double
extract_float(uint64_t raw, unsigned bits, unsigned idx = 0)
{
   raw >>= bits * idx;
   if (bits == 16)
      return _mesa_half_to_float(raw);
   else if (bits == 32)
      return uif(raw);
   else if (bits == 64)
      return uid(raw);
   else
      UNREACHABLE("unsupported float size");
}

uint64_t
operand_canonicalized_labels(opt_ctx& ctx, Operand op)
{
   if (op.isConstant()) {
      uint64_t val = op.constantValue64();
      uint64_t res = 0;
      if (op.size() == 2) {
         if (((val << 1) >> 1) == 0 || ((val << 1) >> 1) > 0x000f'ffff'ffff'ffffull)
            res |= label_canonicalized_fp64;
      } else if (op.size() == 1) {
         /* Check both fp16 halves for denorms because of packed math and opsel.*/
         if (((val & 0x7fff) == 0 || (val & 0x7fff) > 0x3ff) &&
             ((val & 0x7fff0000) == 0 || (val & 0x7fff0000) > 0x3ff0000))
            res |= label_canonicalized_fp16;
         if ((val & 0x7fffffff) == 0 || (val & 0x7fffffff) > 0x7fffff)
            res |= label_canonicalized_fp32;
      }
      return res;
   } else if (op.isTemp()) {
      return ctx.info[op.tempId()].label & canonicalized_labels;
   }

   return 0;
}

void
gather_canonicalized(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (instr->isSDWA() || instr->definitions.size() == 0)
      return;

   if (is_phi(instr)) {
      /* This is correct even for loop header phis because label is 0 initially. */
      uint64_t label = canonicalized_labels;
      for (Operand& op : instr->operands)
         label &= operand_canonicalized_labels(ctx, op);

      ctx.info[instr->definitions[0].tempId()].label |= label;
   } else if (instr->opcode == aco_opcode::p_parallelcopy ||
              instr->opcode == aco_opcode::p_as_uniform || instr->opcode == aco_opcode::v_mov_b32 ||
              instr->opcode == aco_opcode::v_mov_b16 ||
              instr->opcode == aco_opcode::v_readfirstlane_b32 ||
              instr->opcode == aco_opcode::v_readlane_b32 ||
              instr->opcode == aco_opcode::v_readlane_b32_e64) {
      ctx.info[instr->definitions[0].tempId()].label |=
         operand_canonicalized_labels(ctx, instr->operands[0]);
   } else if (instr->opcode == aco_opcode::v_cndmask_b32 ||
              instr->opcode == aco_opcode::v_cndmask_b16 ||
              instr->opcode == aco_opcode::s_cselect_b32 ||
              instr->opcode == aco_opcode::s_cselect_b64) {
      uint64_t label = canonicalized_labels;
      for (unsigned i = 0; i < 2; i++)
         label &= operand_canonicalized_labels(ctx, instr->operands[i]);

      ctx.info[instr->definitions[0].tempId()].label |= label;
   } else if (instr->opcode == aco_opcode::s_mul_i32) {
      for (unsigned i = 0; i < 2; i++) {
         if (!instr->operands[i].isTemp())
            continue;
         Temp tmp = instr->operands[i].getTemp();
         Definition parent_def = ctx.info[tmp.id()].parent_instr->definitions.back();
         if (parent_def.getTemp() == tmp && parent_def.isFixed() && parent_def.physReg() == scc) {
            /* The operand is either 0 or 1, so this is a select between 0 and the other operand. */
            ctx.info[instr->definitions[0].tempId()].label |=
               operand_canonicalized_labels(ctx, instr->operands[!i]);
            break;
         }
      }
   } else if (ctx.program->gfx_level < GFX9 &&
              (instr->opcode == aco_opcode::v_max_f32 || instr->opcode == aco_opcode::v_min_f32 ||
               instr->opcode == aco_opcode::v_max_f64_e64 ||
               instr->opcode == aco_opcode::v_min_f64_e64 ||
               instr->opcode == aco_opcode::v_max3_f32 || instr->opcode == aco_opcode::v_min3_f32 ||
               instr->opcode == aco_opcode::v_med3_f32 || instr->opcode == aco_opcode::v_max_f16 ||
               instr->opcode == aco_opcode::v_min_f16)) {
      uint64_t label = canonicalized_labels;
      for (Operand& op : instr->operands)
         label &= operand_canonicalized_labels(ctx, op);

      ctx.info[instr->definitions[0].tempId()].label |= label;
   } else if (instr->isVALU() || instr->isSALU() || instr->isVINTRP()) {
      aco_type type = instr_info.alu_opcode_infos[(int)instr->opcode].def_types[0];
      if (type.base_type == aco_base_type_float && type.bit_size >= 16)
         ctx.info[instr->definitions[0].tempId()].set_canonicalized(type.bit_size);
   }
}

bool
pseudo_propagate_temp(opt_ctx& ctx, aco_ptr<Instruction>& instr, Temp temp, unsigned index)
{
   if (instr->definitions.empty())
      return false;

   const bool vgpr =
      instr->opcode == aco_opcode::p_as_uniform ||
      std::all_of(instr->definitions.begin(), instr->definitions.end(),
                  [](const Definition& def) { return def.regClass().type() == RegType::vgpr; });

   /* don't propagate VGPRs into SGPR instructions */
   if (temp.type() == RegType::vgpr && !vgpr)
      return false;

   bool can_accept_sgpr =
      ctx.program->gfx_level >= GFX9 ||
      std::none_of(instr->definitions.begin(), instr->definitions.end(),
                   [](const Definition& def) { return def.regClass().is_subdword(); });

   switch (instr->opcode) {
   case aco_opcode::p_phi:
   case aco_opcode::p_linear_phi:
   case aco_opcode::p_parallelcopy:
   case aco_opcode::p_create_vector:
   case aco_opcode::p_start_linear_vgpr:
      if (temp.bytes() != instr->operands[index].bytes())
         return false;
      break;
   case aco_opcode::p_extract_vector:
   case aco_opcode::p_extract:
      if (temp.type() == RegType::sgpr && !can_accept_sgpr)
         return false;
      break;
   case aco_opcode::p_split_vector: {
      if (temp.type() == RegType::sgpr && !can_accept_sgpr)
         return false;
      /* don't increase the vector size */
      if (temp.bytes() > instr->operands[index].bytes())
         return false;
      /* We can decrease the vector size as smaller temporaries are only
       * propagated by p_as_uniform instructions.
       * If this propagation leads to invalid IR or hits the assertion below,
       * it means that some undefined bytes within a dword are begin accessed
       * and a bug in instruction_selection is likely. */
      int decrease = instr->operands[index].bytes() - temp.bytes();
      while (decrease > 0) {
         decrease -= instr->definitions.back().bytes();
         instr->definitions.pop_back();
      }
      assert(decrease == 0);
      break;
   }
   case aco_opcode::p_as_uniform:
      if (temp.regClass() == instr->definitions[0].regClass())
         instr->opcode = aco_opcode::p_parallelcopy;
      break;
   default: return false;
   }

   instr->operands[index].setTemp(temp);
   return true;
}

/* only covers special cases */
bool
pseudo_can_accept_constant(const aco_ptr<Instruction>& instr, unsigned operand)
{
   /* Fixed operands can't accept constants because we need them
    * to be in their fixed register.
    */
   assert(instr->operands.size() > operand);
   if (instr->operands[operand].isFixed())
      return false;

   switch (instr->opcode) {
   case aco_opcode::p_extract_vector:
   case aco_opcode::p_split_vector:
   case aco_opcode::p_extract:
   case aco_opcode::p_insert: return operand != 0;
   case aco_opcode::p_bpermute_readlane:
   case aco_opcode::p_bpermute_shared_vgpr:
   case aco_opcode::p_bpermute_permlane:
   case aco_opcode::p_permlane64_shared_vgpr:
   case aco_opcode::p_interp_gfx11:
   case aco_opcode::p_dual_src_export_gfx11: return false;
   default: return true;
   }
}

bool
parse_base_offset(opt_ctx& ctx, Instruction* instr, unsigned op_index, Temp* base, uint32_t* offset,
                  bool prevent_overflow)
{
   Operand op = instr->operands[op_index];

   if (!op.isTemp())
      return false;
   Temp tmp = op.getTemp();

   Instruction* add_instr = ctx.info[tmp.id()].parent_instr;

   if (add_instr->definitions[0].getTemp() != tmp)
      return false;

   unsigned mask = 0x3;
   bool is_sub = false;
   switch (add_instr->opcode) {
   case aco_opcode::v_add_u32:
   case aco_opcode::v_add_co_u32:
   case aco_opcode::v_add_co_u32_e64:
   case aco_opcode::s_add_i32:
   case aco_opcode::s_add_u32: break;
   case aco_opcode::v_sub_u32:
   case aco_opcode::v_sub_i32:
   case aco_opcode::v_sub_co_u32:
   case aco_opcode::v_sub_co_u32_e64:
   case aco_opcode::s_sub_u32:
   case aco_opcode::s_sub_i32:
      mask = 0x2;
      is_sub = true;
      break;
   case aco_opcode::v_subrev_u32:
   case aco_opcode::v_subrev_co_u32:
   case aco_opcode::v_subrev_co_u32_e64:
      mask = 0x1;
      is_sub = true;
      break;
   default: return false;
   }
   if (prevent_overflow && !add_instr->definitions[0].isNUW())
      return false;

   if (add_instr->usesModifiers())
      return false;

   u_foreach_bit (i, mask) {
      if (add_instr->operands[i].isConstant()) {
         *offset = add_instr->operands[i].constantValue() * (uint32_t)(is_sub ? -1 : 1);
      } else if (add_instr->operands[i].isTemp() &&
                 ctx.info[add_instr->operands[i].tempId()].is_constant()) {
         *offset = ctx.info[add_instr->operands[i].tempId()].val * (uint32_t)(is_sub ? -1 : 1);
      } else {
         continue;
      }
      if (!add_instr->operands[!i].isTemp())
         continue;

      uint32_t offset2 = 0;
      if (parse_base_offset(ctx, add_instr, !i, base, &offset2, prevent_overflow)) {
         *offset += offset2;
      } else {
         *base = add_instr->operands[!i].getTemp();
      }
      return true;
   }

   return false;
}

void
skip_smem_offset_align(opt_ctx& ctx, SMEM_instruction* smem, uint32_t align)
{
   bool soe = smem->operands.size() >= (!smem->definitions.empty() ? 3 : 4);
   if (soe && !smem->operands[1].isConstant())
      return;
   /* We don't need to check the constant offset because the address seems to be calculated with
    * (offset&-4 + const_offset&-4), not (offset+const_offset)&-4.
    */

   Operand& op = smem->operands[soe ? smem->operands.size() - 1 : 1];
   if (!op.isTemp())
      return;

   Instruction* bitwise_instr = ctx.info[op.tempId()].parent_instr;
   if (bitwise_instr->opcode != aco_opcode::s_and_b32 ||
       bitwise_instr->definitions[0].getTemp() != op.getTemp())
      return;

   uint32_t mask = ~(align - 1u);
   if (bitwise_instr->operands[0].constantEquals(mask) &&
       bitwise_instr->operands[1].isOfType(op.regClass().type()))
      op.setTemp(bitwise_instr->operands[1].getTemp());
   else if (bitwise_instr->operands[1].constantEquals(mask) &&
            bitwise_instr->operands[0].isOfType(op.regClass().type()))
      op.setTemp(bitwise_instr->operands[0].getTemp());
}

void
smem_combine(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   uint32_t align = 4;
   switch (instr->opcode) {
   case aco_opcode::s_load_sbyte:
   case aco_opcode::s_load_ubyte:
   case aco_opcode::s_buffer_load_sbyte:
   case aco_opcode::s_buffer_load_ubyte: align = 1; break;
   case aco_opcode::s_load_sshort:
   case aco_opcode::s_load_ushort:
   case aco_opcode::s_buffer_load_sshort:
   case aco_opcode::s_buffer_load_ushort: align = 2; break;
   default: break;
   }

   /* skip &-4 before offset additions: load((a + 16) & -4, 0) */
   if (!instr->operands.empty() && align > 1)
      skip_smem_offset_align(ctx, &instr->smem(), align);

   /* propagate constants and combine additions */
   if (!instr->operands.empty() && instr->operands[1].isTemp()) {
      SMEM_instruction& smem = instr->smem();
      ssa_info info = ctx.info[instr->operands[1].tempId()];

      Temp base;
      uint32_t offset;
      if (info.is_constant() && info.val <= ctx.program->dev.smem_offset_max) {
         instr->operands[1] = Operand::c32(info.val);
      } else if (parse_base_offset(ctx, instr.get(), 1, &base, &offset, true) &&
                 base.regClass() == s1 && offset <= ctx.program->dev.smem_offset_max &&
                 ctx.program->gfx_level >= GFX9 && offset % align == 0) {
         bool soe = smem.operands.size() >= (!smem.definitions.empty() ? 3 : 4);
         if (soe) {
            if (ctx.info[smem.operands.back().tempId()].is_constant() &&
                ctx.info[smem.operands.back().tempId()].val == 0) {
               smem.operands[1] = Operand::c32(offset);
               smem.operands.back() = Operand(base);
            }
         } else {
            Instruction* new_instr = create_instruction(
               smem.opcode, Format::SMEM, smem.operands.size() + 1, smem.definitions.size());
            new_instr->operands[0] = smem.operands[0];
            new_instr->operands[1] = Operand::c32(offset);
            if (smem.definitions.empty())
               new_instr->operands[2] = smem.operands[2];
            new_instr->operands.back() = Operand(base);
            if (!smem.definitions.empty())
               new_instr->definitions[0] = smem.definitions[0];
            new_instr->smem().sync = smem.sync;
            new_instr->smem().cache = smem.cache;
            instr.reset(new_instr);
         }
      }
   }

   /* skip &-4 after offset additions: load(a & -4, 16) */
   if (!instr->operands.empty() && align > 1)
      skip_smem_offset_align(ctx, &instr->smem(), align);
}

Operand
get_constant_op(opt_ctx& ctx, ssa_info info, uint32_t bits)
{
   if (bits == 64)
      return Operand::c32_or_c64(info.val, true);
   return Operand::get_const(ctx.program->gfx_level, info.val, bits / 8u);
}

bool
fixed_to_exec(Operand op)
{
   return op.isFixed() && op.physReg() == exec;
}

SubdwordSel
parse_extract(Instruction* instr)
{
   if (instr->opcode == aco_opcode::p_extract) {
      unsigned size = instr->operands[2].constantValue() / 8;
      unsigned offset = instr->operands[1].constantValue() * size;
      bool sext = instr->operands[3].constantEquals(1);
      return SubdwordSel(size, offset, sext);
   } else if (instr->opcode == aco_opcode::p_insert && instr->operands[1].constantEquals(0)) {
      return instr->operands[2].constantEquals(8) ? SubdwordSel::ubyte : SubdwordSel::uword;
   } else if (instr->opcode == aco_opcode::p_extract_vector) {
      unsigned size = instr->definitions[0].bytes();
      unsigned offset = instr->operands[1].constantValue() * size;
      if (size <= 2)
         return SubdwordSel(size, offset, false);
   } else if (instr->opcode == aco_opcode::p_split_vector) {
      assert(instr->operands[0].bytes() == 4 && instr->definitions[1].bytes() == 2);
      return SubdwordSel(2, 2, false);
   }

   return SubdwordSel();
}

SubdwordSel
parse_insert(Instruction* instr)
{
   if (instr->opcode == aco_opcode::p_extract && instr->operands[3].constantEquals(0) &&
       instr->operands[1].constantEquals(0)) {
      return instr->operands[2].constantEquals(8) ? SubdwordSel::ubyte : SubdwordSel::uword;
   } else if (instr->opcode == aco_opcode::p_insert) {
      unsigned size = instr->operands[2].constantValue() / 8;
      unsigned offset = instr->operands[1].constantValue() * size;
      return SubdwordSel(size, offset, false);
   } else {
      return SubdwordSel();
   }
}

void
remove_operand_extract(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   /* We checked these earlier in alu_propagate_temp_const */
   if (instr->isSALU() || instr->isVALU())
      return;

   for (unsigned i = 0; i < instr->operands.size(); i++) {
      Operand op = instr->operands[i];
      if (!op.isTemp())
         continue;
      ssa_info& info = ctx.info[op.tempId()];
      info.label &= ~label_extract;
   }
}

bool
can_eliminate_and_exec(opt_ctx& ctx, Temp tmp, unsigned pass_flags, bool allow_cselect = false)
{
   Instruction* instr = ctx.info[tmp.id()].parent_instr;
   /* Remove superfluous s_and when the VOPC instruction uses the same exec and thus
    * already produces the same result */
   if (instr->isVOPC())
      return instr->pass_flags == pass_flags;

   if (allow_cselect && instr->pass_flags == pass_flags &&
       (instr->opcode == aco_opcode::s_cselect_b32 || instr->opcode == aco_opcode::s_cselect_b64)) {
      return (instr->operands[0].constantEquals(0) && instr->operands[1].constantEquals(-1)) ||
             (instr->operands[1].constantEquals(0) && instr->operands[0].constantEquals(-1));
   }

   if (instr->operands.size() != 2 || instr->pass_flags != pass_flags)
      return false;
   if (!(instr->operands[0].isTemp() && instr->operands[1].isTemp()))
      return false;

   switch (instr->opcode) {
   case aco_opcode::s_and_b32:
   case aco_opcode::s_and_b64:
      return can_eliminate_and_exec(ctx, instr->operands[0].getTemp(), pass_flags) ||
             can_eliminate_and_exec(ctx, instr->operands[1].getTemp(), pass_flags);
   case aco_opcode::s_or_b32:
   case aco_opcode::s_or_b64:
   case aco_opcode::s_xor_b32:
   case aco_opcode::s_xor_b64:
      return can_eliminate_and_exec(ctx, instr->operands[0].getTemp(), pass_flags) &&
             can_eliminate_and_exec(ctx, instr->operands[1].getTemp(), pass_flags);
   default: return false;
   }
}

bool
is_scratch_offset_valid(opt_ctx& ctx, Instruction* instr, int64_t offset0, int64_t offset1)
{
   bool negative_unaligned_scratch_offset_bug = ctx.program->gfx_level == GFX10;
   int32_t min = ctx.program->dev.scratch_global_offset_min;
   int32_t max = ctx.program->dev.scratch_global_offset_max;

   int64_t offset = offset0 + offset1;

   bool has_vgpr_offset = instr && !instr->operands[0].isUndefined();
   if (negative_unaligned_scratch_offset_bug && has_vgpr_offset && offset < 0 && offset % 4)
      return false;

   return offset >= min && offset <= max;
}

bool
detect_clamp(Instruction* instr, unsigned* clamped_idx)
{
   VALU_instruction& valu = instr->valu();
   if (valu.omod != 0 || valu.opsel != 0 || instr->isDPP())
      return false;

   unsigned idx = 0;
   bool found_zero = false, found_one = false;
   bool is_fp16 = instr->opcode == aco_opcode::v_med3_f16;
   for (unsigned i = 0; i < 3; i++) {
      if (!valu.neg[i] && instr->operands[i].constantEquals(0))
         found_zero = true;
      else if (!valu.neg[i] &&
               instr->operands[i].constantEquals(is_fp16 ? 0x3c00 : 0x3f800000)) /* 1.0 */
         found_one = true;
      else
         idx = i;
   }
   if (found_zero && found_one && instr->operands[idx].isTemp()) {
      *clamped_idx = idx;
      return true;
   } else {
      return false;
   }
}

bool
parse_operand(opt_ctx& ctx, Temp tmp, alu_opt_op& op_info, aco_type& type)
{
   ssa_info info = ctx.info[tmp.id()];
   op_info = {};
   type = {};

   if (info.parent_instr->opcode == aco_opcode::v_pk_mul_f16 &&
       (info.parent_instr->operands[0].constantEquals(0x3c00) ||
        info.parent_instr->operands[1].constantEquals(0x3c00) ||
        info.parent_instr->operands[0].constantEquals(0xbc00) ||
        info.parent_instr->operands[1].constantEquals(0xbc00))) {

      VALU_instruction* fneg = &info.parent_instr->valu();

      unsigned fneg_src =
         fneg->operands[0].constantEquals(0x3c00) || fneg->operands[0].constantEquals(0xbc00);

      if (fneg->opsel_lo[1 - fneg_src] || fneg->opsel_hi[1 - fneg_src])
         return false;

      if (fneg->clamp || fneg->isDPP())
         return false;

      type.base_type = aco_base_type_float;
      type.num_components = 2;
      type.bit_size = 16;

      op_info.op = fneg->operands[fneg_src];
      if (fneg->opsel_lo[fneg_src])
         op_info.extract[0] = SubdwordSel::uword1;
      if (fneg->opsel_hi[fneg_src])
         op_info.extract[1] = SubdwordSel::uword1;
      op_info.neg[0] =
         fneg->operands[1 - fneg_src].constantEquals(0xbc00) ^ fneg->neg_lo[0] ^ fneg->neg_lo[1];
      op_info.neg[1] =
         fneg->operands[1 - fneg_src].constantEquals(0xbc00) ^ fneg->neg_hi[0] ^ fneg->neg_hi[1];
      return true;
   }

   for (unsigned bit_size = tmp.size() == 2 ? 64 : 16; bit_size <= tmp.bytes() * 8; bit_size *= 2) {
      if (info.is_fcanonicalize(bit_size) || info.is_abs(bit_size) || info.is_neg(bit_size)) {
         type.num_components = 1;
         type.bit_size = bit_size;
         if (ctx.info[info.temp.id()].is_canonicalized(bit_size) ||
             (bit_size == 32 ? ctx.fp_mode.denorm32 : ctx.fp_mode.denorm16_64) == fp_denorm_keep)
            type.base_type = aco_base_type_uint;
         else
            type.base_type = aco_base_type_float;

         op_info.op = Operand(info.temp);
         if (info.is_abs(bit_size))
            op_info.abs[0] = true;
         if (info.is_neg(bit_size))
            op_info.neg[0] = true;
         return true;
      }
   }

   type.base_type = aco_base_type_uint;
   type.num_components = 1;
   type.bit_size = tmp.bytes() * 8;

   if (info.is_temp()) {
      op_info.op = Operand(info.temp);
      return true;
   }

   if (info.is_extract()) {
      op_info.extract[0] = parse_extract(info.parent_instr);
      op_info.op = info.parent_instr->operands[0];
      return true;
   }

   if (info.is_constant()) {
      op_info.op = get_constant_op(ctx, info, type.bit_size);
      return true;
   }

   if (info.parent_instr->opcode == aco_opcode::v_cvt_f32_f16 ||
       info.parent_instr->opcode == aco_opcode::s_cvt_f32_f16 ||
       info.parent_instr->opcode == aco_opcode::s_cvt_hi_f32_f16) {
      Instruction* instr = info.parent_instr;
      if (instr->isVALU() && (instr->valu().clamp || instr->valu().omod))
         return false;
      if (instr->isDPP() || (instr->isSDWA() && instr->sdwa().dst_sel.size() != 4))
         return false;

      if (instr->isVALU() && instr->valu().abs[0])
         op_info.abs[0] = true;
      if (instr->isVALU() && instr->valu().neg[0])
         op_info.neg[0] = true;

      if (instr->isSDWA())
         op_info.extract[0] = instr->sdwa().sel[0];
      else if (instr->isVALU() && instr->valu().opsel[0])
         op_info.extract[0] = SubdwordSel::uword1;
      else if (info.parent_instr->opcode == aco_opcode::s_cvt_hi_f32_f16)
         op_info.extract[0] = SubdwordSel::uword1;

      op_info.f16_to_f32 = true;
      op_info.op = instr->operands[0];
      return true;
   }

   return false;
}

bool
combine_operand(opt_ctx& ctx, alu_opt_op& inner, const aco_type& inner_type,
                const alu_opt_op& outer, const aco_type& outer_type, bool flushes_denorms)
{
   /* Nothing to be gained by bothering with lane masks. */
   if (inner_type.bit_size <= 1)
      return false;

   if (inner.op.size() != outer.op.size())
      return false;

   if (outer_type.base_type != aco_base_type_uint && !flushes_denorms)
      return false;

   bool has_imod = outer.abs[0] || outer.neg[0] || outer.abs[1] || outer.neg[1] ||
                   outer_type.base_type != aco_base_type_uint;
   if (has_imod && outer_type.bit_size != inner_type.bit_size)
      return false;

   if (outer.f16_to_f32) {
      if (inner_type.num_components != 1 || inner.extract[0].size() != 4 || inner.f16_to_f32)
         return false;
      inner.f16_to_f32 = true;
   }

   assert(inner.op.size() == outer.op.size());
   assert(inner.op.size() == 1 || inner_type.num_components == 1);
   for (unsigned i = 0; i < inner_type.num_components; i++) {
      unsigned size = inner_type.bit_size;
      unsigned out_comp = 0;
      if (inner.op.size() == 1) {
         size = MIN2(inner.extract[i].size() * 8, size);
         unsigned offset = inner.extract[i].offset() * 8;
         out_comp = offset / outer_type.bit_size;
         unsigned rem_off = offset % outer_type.bit_size;
         if (rem_off && has_imod)
            return false;
         if (out_comp > outer_type.num_components)
            return false;
         if (size > outer_type.bit_size && (out_comp + 1) != outer_type.num_components)
            return false;
         if (rem_off >= outer.extract[out_comp].size() * 8)
            return false;
         if (size < inner_type.bit_size && size > outer.extract[out_comp].size() * 8 &&
             outer.extract[out_comp].sign_extend() && !inner.extract[i].sign_extend())
            return false;

         bool sign_extend = size <= outer.extract[out_comp].size() * 8
                               ? inner.extract[i].sign_extend()
                               : outer.extract[out_comp].sign_extend();
         unsigned new_off = (rem_off / 8) + outer.extract[out_comp].offset();
         unsigned new_size = MIN2(size / 8, outer.extract[i].size());
         inner.extract[i] = SubdwordSel(new_size, new_off, sign_extend);
      }

      if (size == outer_type.bit_size) {
         inner.neg[i] ^= !inner.abs[i] && outer.neg[out_comp];
         inner.abs[i] |= outer.abs[out_comp];
      } else if (outer_type.base_type != aco_base_type_uint) {
         return false;
      }
   }

   if (outer.op.isTemp())
      inner.op.setTemp(outer.op.getTemp());
   else if (inner.op.isFixed())
      return false;
   else
      inner.op = outer.op;
   return true;
}

void
decrease_and_dce(opt_ctx& ctx, Temp tmp)
{
   assert(ctx.uses[tmp.id()]);
   ctx.uses[tmp.id()]--;
   Instruction* instr = ctx.info[tmp.id()].parent_instr;
   if (is_dead(ctx.uses, instr)) {
      for (const Operand& op : instr->operands) {
         if (op.isTemp())
            decrease_and_dce(ctx, op.getTemp());
      }
   }
}

void
alu_propagate_temp_const(opt_ctx& ctx, aco_ptr<Instruction>& instr, bool uses_valid)
{
   alu_opt_info info;
   if (!alu_opt_gather_info(ctx, instr.get(), info))
      return;

   bool had_lit = std::any_of(info.operands.begin(), info.operands.end(),
                              [](const alu_opt_op& op) { return op.op.isLiteral(); });

   const bool gfx8_min_max =
      ctx.program->gfx_level < GFX9 &&
      (instr->opcode == aco_opcode::v_min_f32 || instr->opcode == aco_opcode::v_max_f32 ||
       instr->opcode == aco_opcode::v_min_f16 || instr->opcode == aco_opcode::v_max_f16 ||
       instr->opcode == aco_opcode::v_min_f64_e64 || instr->opcode == aco_opcode::v_max_f64_e64 ||
       instr->opcode == aco_opcode::v_min3_f32 || instr->opcode == aco_opcode::v_max3_f32 ||
       instr->opcode == aco_opcode::v_med3_f32);

   bool remove_extract = !uses_valid;
   /* GFX8: Don't remove label_extract if we can't apply the extract to
    * neg/abs instructions because we'll likely combine it into another valu. */
   if (instr->opcode == aco_opcode::v_mul_f16) {
      for (Operand op : instr->operands)
         remove_extract &= !op.constantEquals(0x3c00) && !op.constantEquals(0xbc00);
   } else if (instr->opcode == aco_opcode::v_mul_f32) {
      for (Operand op : instr->operands)
         remove_extract &= !op.constantEquals(0x3f800000) && !op.constantEquals(0xbf800000);
   }

   unsigned operand_mask = BITFIELD_MASK(info.operands.size());

   bool progress = false;
   alu_opt_info result_info;
   while (operand_mask) {
      uint32_t i = UINT32_MAX;
      uint32_t op_uses = UINT32_MAX;
      u_foreach_bit (candidate, operand_mask) {
         if (!info.operands[candidate].op.isTemp()) {
            operand_mask &= ~BITFIELD_BIT(candidate);
            continue;
         }

         if (!uses_valid) {
            i = candidate;
            break;
         }

         unsigned new_uses = ctx.uses[info.operands[candidate].op.tempId()];
         if (new_uses >= op_uses)
            continue;
         i = candidate;
         op_uses = new_uses;
      }

      if (i == UINT32_MAX)
         break;

      alu_opt_op outer;
      aco_type outer_type;
      if (!parse_operand(ctx, info.operands[i].op.getTemp(), outer, outer_type) ||
          (!uses_valid && outer.f16_to_f32)) {
         operand_mask &= ~BITFIELD_BIT(i);
         continue;
      }

      /* Applying SGPRs to VOP1 doesn't increase code size and DCE is helped by doing it earlier,
       * otherwise we apply SGPRs later.
       */
      bool valu_new_sgpr = info.operands[i].op.isOfType(RegType::vgpr) &&
                           outer.op.isOfType(RegType::sgpr) && !instr->isVOP1();
      if (valu_new_sgpr && !uses_valid) {
         operand_mask &= ~BITFIELD_BIT(i);
         continue;
      }

      alu_opt_op inner = info.operands[i];
      aco_type inner_type = get_canonical_operand_type(info.opcode, i);
      if (inner.f16_to_f32)
         inner_type.bit_size = 16;
      bool flushes_denorms = inner_type.base_type == aco_base_type_float && !gfx8_min_max;
      if (!combine_operand(ctx, inner, inner_type, outer, outer_type, flushes_denorms)) {
         if (remove_extract)
            ctx.info[info.operands[i].op.tempId()].label &= ~label_extract;
         operand_mask &= ~BITFIELD_BIT(i);
         continue;
      }

      alu_opt_info info_copy = info;
      info_copy.operands[i] = inner;
      if (!alu_opt_info_is_valid(ctx, info_copy)) {
         if (remove_extract)
            ctx.info[info.operands[i].op.tempId()].label &= ~label_extract;
         operand_mask &= ~BITFIELD_BIT(i);
         continue;
      }
      bool has_lit = std::any_of(info_copy.operands.begin(), info_copy.operands.end(),
                                 [](const alu_opt_op& op) { return op.op.isLiteral(); });

      if ((!had_lit && has_lit) ||
          (ctx.info[info.operands[i].op.tempId()].is_extract() && !uses_valid)) {
         operand_mask &= ~BITFIELD_BIT(i);
         continue;
      }

      bool valu_removed_sgpr = info.operands[i].op.isOfType(RegType::sgpr) &&
                               !inner.op.isOfType(RegType::sgpr) && instr->isVALU();
      if (valu_removed_sgpr && uses_valid)
         operand_mask = BITFIELD_MASK(info.operands.size());

      if (uses_valid) {
         if (inner.op.isTemp())
            ctx.uses[inner.op.tempId()]++;
         decrease_and_dce(ctx, info.operands[i].op.getTemp());
      }

      result_info = info_copy;
      info.operands[i] = inner;
      progress = true;
   }

   if (!progress)
      return;

   instr.reset(alu_opt_info_to_instr(ctx, result_info, instr.release()));
   for (const Definition& def : instr->definitions)
      ctx.info[def.tempId()].label &= canonicalized_labels;
}

void
extract_apply_extract(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (!ctx.info[instr->operands[0].tempId()].is_extract())
      return;

   alu_opt_op outer;
   aco_type outer_type;
   if (!parse_operand(ctx, instr->operands[0].getTemp(), outer, outer_type))
      return;

   if (instr->definitions[0].bytes() < 4 && outer.op.isOfType(RegType::sgpr) &&
       ctx.program->gfx_level < GFX9)
      return;

   alu_opt_op inner = {};
   inner.op = instr->operands[0];
   inner.extract[0] = parse_extract(instr.get());
   if (!inner.extract[0])
      return;

   aco_type inner_type = {};
   inner_type.base_type = aco_base_type_uint;
   inner_type.num_components = 1;
   inner_type.bit_size = instr->definitions[0].bytes() * 8;

   if (!combine_operand(ctx, inner, inner_type, outer, outer_type, false))
      return;

   assert(inner.extract[0].size() <= 2);

   aco_opcode new_opcode = inner.extract[0].size() == instr->definitions[0].bytes()
                              ? aco_opcode::p_extract_vector
                              : aco_opcode::p_extract;

   if (new_opcode != instr->opcode) {
      assert(instr->definitions[0].regClass().type() == RegType::vgpr);

      unsigned new_ops = new_opcode == aco_opcode::p_extract_vector ? 2 : 4;
      Instruction* new_instr = create_instruction(new_opcode, Format::PSEUDO, new_ops, 1);
      new_instr->definitions[0] = instr->definitions[0];
      new_instr->pass_flags = instr->pass_flags;
      instr.reset(new_instr);
   }

   instr->operands[0] = inner.op;
   if (instr->opcode == aco_opcode::p_extract_vector) {
      instr->operands[1] = Operand::c32(inner.extract[0].offset() / instr->definitions[0].bytes());
   } else {
      instr->operands[1] = Operand::c32(inner.extract[0].offset() / inner.extract[0].size());
      instr->operands[2] = Operand::c32(inner.extract[0].size() * 8u);
      instr->operands[3] = Operand::c32(inner.extract[0].sign_extend());
   }
}

void
label_instruction(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (instr->isSMEM())
      smem_combine(ctx, instr);

   for (unsigned i = 0; i < instr->operands.size(); i++) {
      if (!instr->operands[i].isTemp())
         continue;

      ssa_info info = ctx.info[instr->operands[i].tempId()];
      /* propagate reg->reg of same type */
      while (info.is_temp() && info.temp.regClass() == instr->operands[i].getTemp().regClass()) {
         instr->operands[i].setTemp(ctx.info[instr->operands[i].tempId()].temp);
         info = ctx.info[info.temp.id()];
      }

      /* PSEUDO: propagate temporaries/constants */
      if (instr->isPseudo()) {
         while (info.is_temp()) {
            pseudo_propagate_temp(ctx, instr, info.temp, i);
            info = ctx.info[info.temp.id()];
         }
         unsigned bits = instr->operands[i].bytes() * 8u;
         if (info.is_constant() && pseudo_can_accept_constant(instr, i)) {
            instr->operands[i] = get_constant_op(ctx, info, bits);
            continue;
         }
      }
      /* MUBUF: propagate constants and combine additions */
      else if (instr->isMUBUF()) {
         MUBUF_instruction& mubuf = instr->mubuf();
         Temp base;
         uint32_t offset;
         while (info.is_temp())
            info = ctx.info[info.temp.id()];

         bool swizzled = ctx.program->gfx_level >= GFX12 ? mubuf.cache.gfx12.swizzled
                                                         : (mubuf.cache.value & ac_swizzled);
         /* According to AMDGPUDAGToDAGISel::SelectMUBUFScratchOffen(), vaddr
          * overflow for scratch accesses works only on GFX9+ and saddr overflow
          * never works. Since swizzling is the only thing that separates
          * scratch accesses and other accesses and swizzling changing how
          * addressing works significantly, this probably applies to swizzled
          * MUBUF accesses. */
         bool vaddr_prevent_overflow = swizzled && ctx.program->gfx_level < GFX9;

         uint32_t const_max = ctx.program->dev.buf_offset_max;

         if (mubuf.offen && mubuf.idxen && i == 1 &&
             info.parent_instr->opcode == aco_opcode::p_create_vector &&
             info.parent_instr->operands.size() == 2 && info.parent_instr->operands[0].isTemp() &&
             info.parent_instr->operands[0].regClass() == v1 &&
             info.parent_instr->operands[1].isConstant() &&
             mubuf.offset + info.parent_instr->operands[1].constantValue() <= const_max) {
            instr->operands[1] = info.parent_instr->operands[0];
            mubuf.offset += info.parent_instr->operands[1].constantValue();
            mubuf.offen = false;
            continue;
         } else if (mubuf.offen && i == 1 && info.is_constant() &&
                    mubuf.offset + info.val <= const_max) {
            assert(!mubuf.idxen);
            instr->operands[1] = Operand(v1);
            mubuf.offset += info.val;
            mubuf.offen = false;
            continue;
         } else if (i == 2 && info.is_constant() && mubuf.offset + info.val <= const_max) {
            instr->operands[2] = Operand::c32(0);
            mubuf.offset += info.val;
            continue;
         } else if (mubuf.offen && i == 1 &&
                    parse_base_offset(ctx, instr.get(), i, &base, &offset,
                                      vaddr_prevent_overflow) &&
                    base.regClass() == v1 && mubuf.offset + offset <= const_max) {
            assert(!mubuf.idxen);
            instr->operands[1].setTemp(base);
            mubuf.offset += offset;
            continue;
         } else if (i == 2 && parse_base_offset(ctx, instr.get(), i, &base, &offset, true) &&
                    base.regClass() == s1 && mubuf.offset + offset <= const_max && !swizzled) {
            instr->operands[i].setTemp(base);
            mubuf.offset += offset;
            continue;
         }
      }

      else if (instr->isMTBUF()) {
         MTBUF_instruction& mtbuf = instr->mtbuf();
         while (info.is_temp())
            info = ctx.info[info.temp.id()];

         if (mtbuf.offen && mtbuf.idxen && i == 1 &&
             info.parent_instr->opcode == aco_opcode::p_create_vector &&
             info.parent_instr->operands.size() == 2 && info.parent_instr->operands[0].isTemp() &&
             info.parent_instr->operands[0].regClass() == v1 &&
             info.parent_instr->operands[1].isConstant() &&
             mtbuf.offset + info.parent_instr->operands[1].constantValue() <=
                ctx.program->dev.buf_offset_max) {
            instr->operands[1] = info.parent_instr->operands[0];
            mtbuf.offset += info.parent_instr->operands[1].constantValue();
            mtbuf.offen = false;
            continue;
         }
      }

      /* SCRATCH: propagate constants and combine additions */
      else if (instr->isScratch()) {
         FLAT_instruction& scratch = instr->scratch();
         Temp base;
         uint32_t offset;
         while (info.is_temp())
            info = ctx.info[info.temp.id()];

         /* The hardware probably does: 'scratch_base + u2u64(saddr) + i2i64(offset)'. This means
          * we can't combine the addition if the unsigned addition overflows and offset is
          * positive. In theory, there is also issues if
          * 'ilt(offset, 0) && ige(saddr, 0) && ilt(saddr + offset, 0)', but that just
          * replaces an already out-of-bounds access with a larger one since 'saddr + offset'
          * would be larger than INT32_MAX.
          */
         if (i <= 1 && parse_base_offset(ctx, instr.get(), i, &base, &offset, true) &&
             base.regClass() == instr->operands[i].regClass() &&
             is_scratch_offset_valid(ctx, instr.get(), scratch.offset, (int32_t)offset)) {
            instr->operands[i].setTemp(base);
            scratch.offset += (int32_t)offset;
            continue;
         } else if (i <= 1 && parse_base_offset(ctx, instr.get(), i, &base, &offset, false) &&
                    base.regClass() == instr->operands[i].regClass() && (int32_t)offset < 0 &&
                    is_scratch_offset_valid(ctx, instr.get(), scratch.offset, (int32_t)offset)) {
            instr->operands[i].setTemp(base);
            scratch.offset += (int32_t)offset;
            continue;
         } else if (i <= 1 && info.is_constant() && ctx.program->gfx_level >= GFX10_3 &&
                    is_scratch_offset_valid(ctx, NULL, scratch.offset, (int32_t)info.val)) {
            /* GFX10.3+ can disable both SADDR and ADDR. */
            instr->operands[i] = Operand(instr->operands[i].regClass());
            scratch.offset += (int32_t)info.val;
            continue;
         }
      }

      else if (instr->isBranch()) {
         if (ctx.info[instr->operands[0].tempId()].is_scc_invert()) {
            /* Flip the branch instruction to get rid of the scc_invert instruction */
            instr->opcode = instr->opcode == aco_opcode::p_cbranch_z ? aco_opcode::p_cbranch_nz
                                                                     : aco_opcode::p_cbranch_z;
            instr->operands[0].setTemp(ctx.info[instr->operands[0].tempId()].temp);
         }
      }
   }

   /* SALU / VALU: propagate inline constants, temps, and imod */
   if (instr->isSALU() || instr->isVALU()) {
      alu_propagate_temp_const(ctx, instr, false);
   }

   /* if this instruction doesn't define anything, return */
   if (instr->definitions.empty()) {
      remove_operand_extract(ctx, instr);
      return;
   }

   if (instr->opcode == aco_opcode::p_extract || instr->opcode == aco_opcode::p_extract_vector)
      extract_apply_extract(ctx, instr);

   gather_canonicalized(ctx, instr);

   switch (instr->opcode) {
   case aco_opcode::p_create_vector: {
      bool copy_prop = instr->operands.size() == 1 && instr->operands[0].isTemp() &&
                       instr->operands[0].regClass() == instr->definitions[0].regClass();
      if (copy_prop) {
         ctx.info[instr->definitions[0].tempId()].set_temp(instr->operands[0].getTemp());
         break;
      }

      /* expand vector operands */
      std::vector<Operand> ops;
      unsigned offset = 0;
      for (const Operand& op : instr->operands) {
         /* ensure that any expanded operands are properly aligned */
         bool aligned = offset % 4 == 0 || op.bytes() < 4;
         offset += op.bytes();
         if (aligned && op.isTemp() &&
             ctx.info[op.tempId()].parent_instr->opcode == aco_opcode::p_create_vector) {
            Instruction* vec = ctx.info[op.tempId()].parent_instr;
            for (const Operand& vec_op : vec->operands)
               ops.emplace_back(vec_op);
         } else {
            ops.emplace_back(op);
         }
      }

      offset = 0;
      for (unsigned i = 0; i < ops.size(); i++) {
         if (ops[i].isTemp()) {
            if (ctx.info[ops[i].tempId()].is_temp() &&
                ops[i].regClass() == ctx.info[ops[i].tempId()].temp.regClass()) {
               ops[i].setTemp(ctx.info[ops[i].tempId()].temp);
            }

            /* If this and the following operands make up all definitions of a `p_split_vector`,
             * replace them with the operand of the `p_split_vector` instruction.
             */
            Instruction* parent = ctx.info[ops[i].tempId()].parent_instr;
            if (parent->opcode == aco_opcode::p_split_vector &&
                (offset % 4 == 0 || parent->operands[0].bytes() < 4) &&
                parent->definitions.size() <= ops.size() - i) {
               copy_prop = true;
               for (unsigned j = 0; copy_prop && j < parent->definitions.size(); j++) {
                  copy_prop &= ops[i + j].isTemp() &&
                               ops[i + j].getTemp() == parent->definitions[j].getTemp();
               }

               if (copy_prop) {
                  ops.erase(ops.begin() + i + 1, ops.begin() + i + parent->definitions.size());
                  ops[i] = parent->operands[0];
               }
            }
         }

         offset += ops[i].bytes();
      }

      /* combine expanded operands to new vector */
      if (ops.size() <= instr->operands.size()) {
         while (instr->operands.size() > ops.size())
            instr->operands.pop_back();

         if (ops.size() == 1 && !ops[0].isUndefined()) {
            instr->opcode = aco_opcode::p_parallelcopy;
            if (ops[0].isTemp())
               ctx.info[instr->definitions[0].tempId()].set_temp(ops[0].getTemp());
         }
      } else {
         Definition def = instr->definitions[0];
         instr.reset(
            create_instruction(aco_opcode::p_create_vector, Format::PSEUDO, ops.size(), 1));
         instr->definitions[0] = def;
      }

      for (unsigned i = 0; i < ops.size(); i++)
         instr->operands[i] = ops[i];
      break;
   }
   case aco_opcode::p_split_vector: {
      ssa_info& info = ctx.info[instr->operands[0].tempId()];

      if (info.is_constant()) {
         uint64_t val = info.val;
         for (Definition def : instr->definitions) {
            uint64_t mask = u_bit_consecutive64(0, def.bytes() * 8u);
            ctx.info[def.tempId()].set_constant(val & mask);
            val >>= def.bytes() * 8u;
         }
         break;
      } else if (info.parent_instr->opcode != aco_opcode::p_create_vector) {
         if (instr->definitions.size() == 2 && instr->operands[0].isTemp() &&
             instr->definitions[0].bytes() == instr->definitions[1].bytes()) {
            if (instr->operands[0].bytes() == 4) {
               /* D16 subdword split */
               ctx.info[instr->definitions[0].tempId()].set_temp(instr->operands[0].getTemp());
               ctx.info[instr->definitions[1].tempId()].set_extract();
            }
         }
         break;
      }

      Instruction* vec = info.parent_instr;
      unsigned split_offset = 0;
      unsigned vec_offset = 0;
      unsigned vec_index = 0;
      for (unsigned i = 0; i < instr->definitions.size();
           split_offset += instr->definitions[i++].bytes()) {
         while (vec_offset < split_offset && vec_index < vec->operands.size())
            vec_offset += vec->operands[vec_index++].bytes();

         if (vec_offset != split_offset ||
             vec->operands[vec_index].bytes() != instr->definitions[i].bytes())
            continue;

         Operand vec_op = vec->operands[vec_index];
         if (vec_op.isConstant()) {
            ctx.info[instr->definitions[i].tempId()].set_constant(vec_op.constantValue64());
         } else if (vec_op.isTemp()) {
            ctx.info[instr->definitions[i].tempId()].set_temp(vec_op.getTemp());
         }
      }
      break;
   }
   case aco_opcode::p_extract_vector: { /* mov */
      const unsigned index = instr->operands[1].constantValue();

      if (instr->operands[0].isTemp()) {
         ssa_info& info = ctx.info[instr->operands[0].tempId()];
         const unsigned dst_offset = index * instr->definitions[0].bytes();

         if (info.parent_instr->opcode == aco_opcode::p_create_vector) {
            /* check if we index directly into a vector element */
            Instruction* vec = info.parent_instr;
            unsigned offset = 0;

            for (const Operand& op : vec->operands) {
               if (offset < dst_offset) {
                  offset += op.bytes();
                  continue;
               } else if (offset != dst_offset || op.bytes() != instr->definitions[0].bytes()) {
                  break;
               }
               instr->operands[0] = op;
               break;
            }
         } else if (info.is_constant()) {
            /* propagate constants */
            uint64_t mask = u_bit_consecutive64(0, instr->definitions[0].bytes() * 8u);
            uint64_t val = (info.val >> (dst_offset * 8u)) & mask;
            instr->operands[0] =
               Operand::get_const(ctx.program->gfx_level, val, instr->definitions[0].bytes());
         }
      }

      if (instr->operands[0].bytes() != instr->definitions[0].bytes()) {
         if (instr->operands[0].size() != 1 || !instr->operands[0].isTemp())
            break;

         if (index == 0)
            ctx.info[instr->definitions[0].tempId()].set_temp(instr->operands[0].getTemp());
         else
            ctx.info[instr->definitions[0].tempId()].set_extract();
         break;
      }

      /* convert this extract into a copy instruction */
      instr->opcode = aco_opcode::p_parallelcopy;
      instr->operands.pop_back();
      FALLTHROUGH;
   }
   case aco_opcode::p_parallelcopy: /* propagate */
      if (instr->operands[0].isTemp() &&
          ctx.info[instr->operands[0].tempId()].parent_instr->opcode ==
             aco_opcode::p_create_vector &&
          instr->operands[0].regClass() != instr->definitions[0].regClass()) {
         /* We might not be able to copy-propagate if it's a SGPR->VGPR copy, so
          * duplicate the vector instead.
          */
         Instruction* vec = ctx.info[instr->operands[0].tempId()].parent_instr;
         aco_ptr<Instruction> old_copy = std::move(instr);

         instr.reset(create_instruction(aco_opcode::p_create_vector, Format::PSEUDO,
                                        vec->operands.size(), 1));
         instr->definitions[0] = old_copy->definitions[0];
         std::copy(vec->operands.begin(), vec->operands.end(), instr->operands.begin());
         for (unsigned i = 0; i < vec->operands.size(); i++) {
            Operand& op = instr->operands[i];
            if (op.isTemp() && ctx.info[op.tempId()].is_temp() &&
                ctx.info[op.tempId()].temp.type() == instr->definitions[0].regClass().type())
               op.setTemp(ctx.info[op.tempId()].temp);
         }
         break;
      }
      FALLTHROUGH;
   case aco_opcode::p_as_uniform:
      if (instr->definitions[0].isFixed()) {
         /* don't copy-propagate copies into fixed registers */
      } else if (instr->operands[0].isConstant()) {
         ctx.info[instr->definitions[0].tempId()].set_constant(
            instr->operands[0].constantValue64());
      } else if (instr->operands[0].isTemp()) {
         ctx.info[instr->definitions[0].tempId()].set_temp(instr->operands[0].getTemp());
      } else {
         assert(instr->operands[0].isFixed());
      }
      break;
   case aco_opcode::p_is_helper:
      if (!ctx.program->needs_wqm)
         ctx.info[instr->definitions[0].tempId()].set_constant(0u);
      break;
   case aco_opcode::v_mul_f16:
   case aco_opcode::v_mul_f32:
   case aco_opcode::v_mul_legacy_f32:
   case aco_opcode::v_mul_f64:
   case aco_opcode::v_mul_f64_e64: {
      bool uses_mods = instr->usesModifiers();
      bool fp16 = instr->opcode == aco_opcode::v_mul_f16;
      bool fp64 =
         instr->opcode == aco_opcode::v_mul_f64 || instr->opcode == aco_opcode::v_mul_f64_e64;
      unsigned bit_size = fp16 ? 16 : (fp64 ? 64 : 32);
      unsigned denorm_mode = fp16 || fp64 ? ctx.fp_mode.denorm16_64 : ctx.fp_mode.denorm32;

      for (unsigned i = 0; i < 2; i++) {
         if (!instr->operands[!i].isConstant() || !instr->operands[i].isTemp())
            continue;

         double constant = extract_float(instr->operands[!i].constantValue64(), bit_size);

         if (!instr->isDPP() && !instr->isSDWA() && !instr->valu().opsel && fabs(constant) == 1.0) {
            bool neg1 = constant == -1.0;

            VALU_instruction* valu = &instr->valu();
            if (valu->abs[!i] || valu->neg[!i] || valu->omod || valu->clamp)
               continue;

            bool abs = valu->abs[i];
            bool neg = neg1 ^ valu->neg[i];
            Temp other = instr->operands[i].getTemp();

            if (abs && neg && other.type() == RegType::vgpr)
               ctx.info[instr->definitions[0].tempId()].set_neg_abs(other, bit_size);
            else if (abs && !neg && other.type() == RegType::vgpr)
               ctx.info[instr->definitions[0].tempId()].set_abs(other, bit_size);
            else if (!abs && neg && other.type() == RegType::vgpr)
               ctx.info[instr->definitions[0].tempId()].set_neg(other, bit_size);
            else if (!abs && !neg) {
               if (denorm_mode == fp_denorm_keep || ctx.info[other.id()].is_canonicalized(bit_size))
                  ctx.info[instr->definitions[0].tempId()].set_temp(other);
               else
                  ctx.info[instr->definitions[0].tempId()].set_fcanonicalize(other, bit_size);
            }
         } else if (!uses_mods && instr->operands[!i].constantValue64() == 0u &&
                    ((!instr->definitions[0].isNaNPreserve() &&
                      !instr->definitions[0].isInfPreserve() &&
                      !instr->definitions[0].isSZPreserve()) ||
                     instr->opcode == aco_opcode::v_mul_legacy_f32)) {
            ctx.info[instr->definitions[0].tempId()].set_constant(0u);
         }
         break;
      }
      break;
   }
   case aco_opcode::s_not_b32:
   case aco_opcode::s_not_b64:
      if (!instr->operands[0].isTemp()) {
      } else if (ctx.info[instr->operands[0].tempId()].is_uniform_bool()) {
         ctx.info[instr->definitions[0].tempId()].set_uniform_bitwise();
         ctx.info[instr->definitions[1].tempId()].set_scc_invert(
            ctx.info[instr->operands[0].tempId()].temp);
      } else if (ctx.info[instr->operands[0].tempId()].is_uniform_bitwise()) {
         ctx.info[instr->definitions[0].tempId()].set_uniform_bitwise();
         ctx.info[instr->definitions[1].tempId()].set_scc_invert(
            ctx.info[instr->operands[0].tempId()].parent_instr->definitions[1].getTemp());
      }
      break;
   case aco_opcode::s_and_b32:
   case aco_opcode::s_and_b64:
      if (fixed_to_exec(instr->operands[1]) && instr->operands[0].isTemp()) {
         if (ctx.info[instr->operands[0].tempId()].is_uniform_bool()) {
            /* Try to get rid of the superfluous s_cselect + s_and_b64 that comes from turning a
             * uniform bool into divergent */
            ctx.info[instr->definitions[1].tempId()].set_temp(
               ctx.info[instr->operands[0].tempId()].temp);
            break;
         } else if (ctx.info[instr->operands[0].tempId()].is_uniform_bitwise()) {
            /* Try to get rid of the superfluous s_and_b64, since the uniform bitwise instruction
             * already produces the same SCC */
            ctx.info[instr->definitions[1].tempId()].set_temp(
               ctx.info[instr->operands[0].tempId()].parent_instr->definitions[1].getTemp());
            break;
         } else if ((ctx.program->stage.num_sw_stages() > 1 ||
                     ctx.program->stage.hw == AC_HW_NEXT_GEN_GEOMETRY_SHADER) &&
                    instr->pass_flags == 1) {
            /* In case of merged shaders, pass_flags=1 means that all lanes are active (exec=-1), so
             * s_and is unnecessary. */
            ctx.info[instr->definitions[0].tempId()].set_temp(instr->operands[0].getTemp());
            break;
         }
      }
      FALLTHROUGH;
   case aco_opcode::s_or_b32:
   case aco_opcode::s_or_b64:
   case aco_opcode::s_xor_b32:
   case aco_opcode::s_xor_b64:
      if (std::all_of(instr->operands.begin(), instr->operands.end(),
                      [&ctx](const Operand& op)
                      {
                         return op.isTemp() && (ctx.info[op.tempId()].is_uniform_bool() ||
                                                ctx.info[op.tempId()].is_uniform_bitwise());
                      })) {
         ctx.info[instr->definitions[0].tempId()].set_uniform_bitwise();
      }
      break;
   case aco_opcode::s_cselect_b64:
   case aco_opcode::s_cselect_b32:
      if (instr->operands[0].constantEquals((unsigned)-1) && instr->operands[1].constantEquals(0)) {
         /* Found a cselect that operates on a uniform bool that comes from eg. s_cmp */
         ctx.info[instr->definitions[0].tempId()].set_uniform_bool(instr->operands[2].getTemp());
      } else if (instr->operands[2].isTemp() && ctx.info[instr->operands[2].tempId()].is_scc_invert()) {
         /* Flip the operands to get rid of the scc_invert instruction */
         std::swap(instr->operands[0], instr->operands[1]);
         instr->operands[2].setTemp(ctx.info[instr->operands[2].tempId()].temp);
      }
      break;
   case aco_opcode::p_extract: {
      if (instr->operands[0].isTemp()) {
         ctx.info[instr->definitions[0].tempId()].set_extract();
      }
      break;
   }
   case aco_opcode::p_insert: {
      if (instr->operands[0].isTemp() && parse_extract(instr.get()))
         ctx.info[instr->definitions[0].tempId()].set_extract();
      break;
   }
   default: break;
   }

   remove_operand_extract(ctx, instr);

   /* Set parent_instr for all SSA definitions. */
   for (const Definition& def : instr->definitions)
      ctx.info[def.tempId()].parent_instr = instr.get();
}

unsigned
original_temp_id(opt_ctx& ctx, Temp tmp)
{
   if (ctx.info[tmp.id()].is_temp())
      return ctx.info[tmp.id()].temp.id();
   else
      return tmp.id();
}

bool
is_operand_constant(opt_ctx& ctx, Operand op, unsigned bit_size, uint64_t* value)
{
   if (op.isConstant()) {
      *value = op.constantValue64();
      return true;
   } else if (op.isTemp()) {
      unsigned id = original_temp_id(ctx, op.getTemp());
      if (!ctx.info[id].is_constant())
         return false;
      *value = get_constant_op(ctx, ctx.info[id], bit_size).constantValue64();
      return true;
   }
   return false;
}

/* This function attempts to propagate (potential) input modifers from the consuming
 * instruction backwards to the producing instruction.
 * Because inbetween swizzles are resolved,
 * it also changes num_components of the producer's operands to match consumer.
 *
 * - info is the instruction info of the producing instruction
 * - op_info is the Operand info of the consuming instruction
 * - type is the aco type of op_info
 */
bool
backpropagate_input_modifiers(opt_ctx& ctx, alu_opt_info& info, const alu_opt_op& op_info,
                              const aco_type& type)
{
   if (op_info.f16_to_f32 || op_info.dpp16 || op_info.dpp8)
      return false;

   aco_type dest_type = instr_info.alu_opcode_infos[(int)info.opcode].def_types[0];

   if (info.f32_to_f16)
      dest_type.bit_size = 16;

   if (info.uses_insert())
      return false;

   assert(type.num_components != 0);

   /* Resolve swizzles first. */
   if (type.bit_size == 1 || op_info.op.size() > 1) {
      /* no swizzle */
      assert(type.num_components == 1);
   } else {
      bitarray8 swizzle = 0;
      for (unsigned comp = 0; comp < type.num_components; comp++) {
         /* Check if this extract is a swizzle or some other subdword access. */
         if (op_info.extract[comp].offset() * 8 % type.bit_size != 0 ||
             op_info.extract[comp].size() * 8 < type.bit_size)
            return false;
         swizzle[comp] = op_info.extract[comp].offset() * 8 / type.bit_size;
      }

      if (swizzle != 0 && dest_type.num_components == 1)
         return false;

      if (swizzle == 0b10) {
         /* noop */
      } else if (info.opcode == aco_opcode::v_cvt_pkrtz_f16_f32 ||
                 info.opcode == aco_opcode::v_cvt_pkrtz_f16_f32_e64 ||
                 info.opcode == aco_opcode::s_cvt_pk_rtz_f16_f32 ||
                 info.opcode == aco_opcode::v_pack_b32_f16) {
         if (swizzle == 0b01) {
            std::swap(info.operands[0], info.operands[1]);
         } else {
            unsigned broadcast = swizzle == 0b00 ? 0 : 1;
            info.operands[!broadcast] = info.operands[broadcast];
         }
      } else {
         for (alu_opt_op& op : info.operands) {
            if (swizzle == 0b01) {
               op.neg[0].swap(op.neg[1]);
               op.abs[0].swap(op.abs[1]);
               std::swap(op.extract[0], op.extract[1]);
            } else {
               unsigned broadcast = swizzle == 0b00 ? 0 : 1;
               op.neg[!broadcast] = op.neg[broadcast];
               op.abs[!broadcast] = op.abs[broadcast];
               op.extract[!broadcast] = op.extract[broadcast];
            }
         }
      }
   }

   if (!op_info.abs && !op_info.neg)
      return true;

   if (info.clamp || type.bit_size != dest_type.bit_size)
      return false;

   /* neg(omod(...)) and omod(neg(...)) are not the same because omod turn -0.0 into +0.0.
    * Adds and dx9 mul have similar limitations.
    */
   bool require_neg_nsz = info.omod;

   /* Apply modifiers for each component. */
   switch (info.opcode) {
   case aco_opcode::v_mul_legacy_f32: require_neg_nsz = true; FALLTHROUGH;
   case aco_opcode::v_mul_f64_e64:
   case aco_opcode::v_mul_f64:
   case aco_opcode::v_mul_f32:
   case aco_opcode::v_mul_f16:
   case aco_opcode::s_mul_f32:
   case aco_opcode::s_mul_f16:
   case aco_opcode::v_pk_mul_f16:
   case aco_opcode::v_rcp_f64:
   case aco_opcode::v_rcp_f32:
   case aco_opcode::v_rcp_f16:
   case aco_opcode::v_s_rcp_f32:
   case aco_opcode::v_s_rcp_f16:
   case aco_opcode::v_cvt_f32_f64:
   case aco_opcode::v_cvt_f64_f32:
   case aco_opcode::v_cvt_f16_f32:
   case aco_opcode::v_cvt_f32_f16:
   case aco_opcode::s_cvt_f16_f32:
   case aco_opcode::s_cvt_f32_f16:
   case aco_opcode::p_v_cvt_f16_f32_rtne:
   case aco_opcode::p_s_cvt_f16_f32_rtne:
      for (alu_opt_op& op : info.operands) {
         op.neg &= ~op_info.abs;
         op.abs |= op_info.abs;
      }
      info.operands[0].neg ^= op_info.neg;
      break;
   case aco_opcode::v_cndmask_b32:
   case aco_opcode::v_cndmask_b16:
   case aco_opcode::s_cselect_b32:
   case aco_opcode::s_cselect_b64:
      for (unsigned i = 0; i < 2; i++) {
         info.operands[i].neg &= ~op_info.abs;
         info.operands[i].abs |= op_info.abs;
         info.operands[i].neg ^= op_info.neg;
      }
      break;
   case aco_opcode::v_add_f64_e64:
   case aco_opcode::v_add_f64:
   case aco_opcode::v_add_f32:
   case aco_opcode::v_add_f16:
   case aco_opcode::s_add_f32:
   case aco_opcode::s_add_f16:
   case aco_opcode::v_pk_add_f16:
   case aco_opcode::v_fma_f64:
   case aco_opcode::v_fma_f32:
   case aco_opcode::v_fma_f16:
   case aco_opcode::s_fmac_f32:
   case aco_opcode::s_fmac_f16:
   case aco_opcode::v_pk_fma_f16:
   case aco_opcode::v_fma_legacy_f32:
   case aco_opcode::v_fma_legacy_f16:
   case aco_opcode::v_mad_f32:
   case aco_opcode::v_mad_f16:
   case aco_opcode::v_mad_legacy_f32:
   case aco_opcode::v_mad_legacy_f16:
      if (op_info.abs)
         return false;
      info.operands[0].neg ^= op_info.neg;
      info.operands.back().neg ^= op_info.neg;
      require_neg_nsz = true;
      break;
   case aco_opcode::v_min_f64_e64:
   case aco_opcode::v_min_f64:
   case aco_opcode::v_min_f32:
   case aco_opcode::v_min_f16:
   case aco_opcode::v_max_f64_e64:
   case aco_opcode::v_max_f64:
   case aco_opcode::v_max_f32:
   case aco_opcode::v_max_f16:
   case aco_opcode::v_min3_f32:
   case aco_opcode::v_min3_f16:
   case aco_opcode::v_max3_f32:
   case aco_opcode::v_max3_f16:
   case aco_opcode::v_minmax_f32:
   case aco_opcode::v_minmax_f16:
   case aco_opcode::v_maxmin_f32:
   case aco_opcode::v_maxmin_f16:
   case aco_opcode::s_min_f32:
   case aco_opcode::s_min_f16:
   case aco_opcode::s_max_f32:
   case aco_opcode::s_max_f16:
   case aco_opcode::v_pk_min_f16:
   case aco_opcode::v_pk_max_f16:
      if (op_info.abs)
         return false;

      if (op_info.neg[0] != op_info.neg[type.num_components - 1])
         return false;
      for (alu_opt_op& op : info.operands)
         op.neg ^= op_info.neg;

      switch (info.opcode) {
      case aco_opcode::v_min_f64_e64: info.opcode = aco_opcode::v_max_f64_e64; break;
      case aco_opcode::v_min_f64: info.opcode = aco_opcode::v_max_f64; break;
      case aco_opcode::v_min_f32: info.opcode = aco_opcode::v_max_f32; break;
      case aco_opcode::v_min_f16: info.opcode = aco_opcode::v_max_f16; break;
      case aco_opcode::v_max_f64_e64: info.opcode = aco_opcode::v_min_f64_e64; break;
      case aco_opcode::v_max_f64: info.opcode = aco_opcode::v_min_f64; break;
      case aco_opcode::v_max_f32: info.opcode = aco_opcode::v_min_f32; break;
      case aco_opcode::v_max_f16: info.opcode = aco_opcode::v_min_f16; break;
      case aco_opcode::v_min3_f32: info.opcode = aco_opcode::v_max3_f32; break;
      case aco_opcode::v_min3_f16: info.opcode = aco_opcode::v_max3_f16; break;
      case aco_opcode::v_max3_f32: info.opcode = aco_opcode::v_min3_f32; break;
      case aco_opcode::v_max3_f16: info.opcode = aco_opcode::v_min3_f16; break;
      case aco_opcode::v_minmax_f32: info.opcode = aco_opcode::v_maxmin_f32; break;
      case aco_opcode::v_minmax_f16: info.opcode = aco_opcode::v_maxmin_f16; break;
      case aco_opcode::v_maxmin_f32: info.opcode = aco_opcode::v_minmax_f32; break;
      case aco_opcode::v_maxmin_f16: info.opcode = aco_opcode::v_minmax_f16; break;
      case aco_opcode::s_min_f32: info.opcode = aco_opcode::s_max_f32; break;
      case aco_opcode::s_min_f16: info.opcode = aco_opcode::s_max_f16; break;
      case aco_opcode::s_max_f32: info.opcode = aco_opcode::s_min_f32; break;
      case aco_opcode::s_max_f16: info.opcode = aco_opcode::s_min_f16; break;
      case aco_opcode::v_pk_min_f16: info.opcode = aco_opcode::v_pk_max_f16; break;
      case aco_opcode::v_pk_max_f16: info.opcode = aco_opcode::v_pk_min_f16; break;
      default: UNREACHABLE("invalid op");
      }
      break;
   case aco_opcode::v_cvt_pkrtz_f16_f32:
   case aco_opcode::v_cvt_pkrtz_f16_f32_e64:
   case aco_opcode::s_cvt_pk_rtz_f16_f32:
   case aco_opcode::v_pack_b32_f16:
      for (unsigned comp = 0; comp < type.num_components; comp++) {
         if (op_info.abs[comp]) {
            info.operands[comp].neg[0] = false;
            info.operands[comp].abs[0] = true;
         }
         info.operands[comp].neg[0] ^= op_info.neg[comp];
      }
      break;
   default: return false;
   }

   if (op_info.neg && require_neg_nsz && info.defs[0].isSZPreserve())
      return false;

   return true;
}

typedef bool (*combine_instr_callback)(opt_ctx& ctx, alu_opt_info& info);

struct combine_instr_pattern {
   aco_opcode src_opcode;
   aco_opcode res_opcode;
   unsigned operand_mask;
   const char* swizzle;
   combine_instr_callback callback;

   /* Limit to pattern matching to avoid unlike combining for instructions
    * that might be used as src_opcode for other patterns.
    */
   bool less_aggressive;
};

bool
can_match_op(opt_ctx& ctx, Operand op, uint32_t exec_id)
{
   if (!op.isTemp())
      return false;

   Instruction* op_instr = ctx.info[op.tempId()].parent_instr;
   if (op_instr->definitions[0].getTemp() != op.getTemp())
      return false;

   if (op_instr->pass_flags == exec_id)
      return true;

   if (op_instr->isDPP() || op_instr->isVINTERP_INREG() || op_instr->reads_exec())
      return false;

   return true;
}

bool
match_and_apply_patterns(opt_ctx& ctx, alu_opt_info& info,
                         const aco::small_vec<combine_instr_pattern, 8>& patterns)
{
   if (patterns.empty())
      return false;

   unsigned total_mask = 0;
   for (const combine_instr_pattern& pattern : patterns)
      total_mask |= pattern.operand_mask;

   for (unsigned i = 0; i < info.operands.size(); i++) {
      if (!can_match_op(ctx, info.operands[i].op, info.pass_flags))
         total_mask &= ~BITFIELD_BIT(i);
   }

   if (!total_mask)
      return false;

   aco::small_vec<int, 4> indices;
   indices.reserve(util_bitcount(total_mask));
   u_foreach_bit (i, total_mask)
      indices.push_back(i);

   std::stable_sort(indices.begin(), indices.end(),
                    [&](int a, int b)
                    {
                       Temp temp_a = info.operands[a].op.getTemp();
                       Temp temp_b = info.operands[b].op.getTemp();

                       /* Less uses make it more likely/profitable to eliminate an instruction. */
                       if (ctx.uses[temp_a.id()] != ctx.uses[temp_b.id()])
                          return ctx.uses[temp_a.id()] < ctx.uses[temp_b.id()];

                       /* Prefer eliminating VALU instructions. */
                       if (temp_a.type() != temp_b.type())
                          return temp_a.type() == RegType::vgpr;

                       /* The id is a good approximation for instruction order,
                        * prefer instructions closer to info to not increase register pressure
                        * as much.
                        */
                       return temp_a.id() > temp_b.id();
                    });

   for (unsigned op_idx : indices) {
      Temp tmp = info.operands[op_idx].op.getTemp();
      alu_opt_info op_instr;
      if (!alu_opt_gather_info(ctx, ctx.info[tmp.id()].parent_instr, op_instr))
         continue;

      if (op_instr.clamp || op_instr.omod || op_instr.f32_to_f16)
         continue;

      aco_type type = instr_info.alu_opcode_infos[(int)info.opcode].op_types[op_idx];
      if (!backpropagate_input_modifiers(ctx, op_instr, info.operands[op_idx], type))
         continue;

      for (const combine_instr_pattern& pattern : patterns) {
         if (!(pattern.operand_mask & BITFIELD_BIT(op_idx)) ||
             op_instr.opcode != pattern.src_opcode)
            continue;

         if (pattern.less_aggressive && ctx.uses[tmp.id()] > ctx.uses[info.defs[0].tempId()])
            continue;

         alu_opt_info new_info = info;

         unsigned rem = info.operands.size() - 1;
         unsigned op_count = rem + op_instr.operands.size();
         new_info.operands.resize(op_count);
         assert(strlen(pattern.swizzle) == op_count);
         for (unsigned i = 0; i < op_count; i++) {
            unsigned src_idx = pattern.swizzle[i] - '0';
            if (src_idx < op_idx)
               new_info.operands[i] = info.operands[src_idx];
            else if (src_idx < rem)
               new_info.operands[i] = info.operands[src_idx + 1];
            else
               new_info.operands[i] = op_instr.operands[src_idx - rem];
         }

         new_info.opcode = pattern.res_opcode;

         if (op_instr.defs[0].isPrecise())
            new_info.defs[0].setPrecise(true);

         if (pattern.callback && !pattern.callback(ctx, new_info))
            continue;

         if (alu_opt_info_is_valid(ctx, new_info)) {
            info = std::move(new_info);
            return true;
         }
      }
   }

   return false;
}

/* v_not(v_xor(a, b)) -> v_xnor(a, b) */
Instruction*
apply_v_not(opt_ctx& ctx, aco_ptr<Instruction>& instr, Instruction* op_instr)
{
   if (ctx.program->gfx_level < GFX10 || instr->usesModifiers() ||
       op_instr->opcode != aco_opcode::v_xor_b32 || op_instr->isSDWA())
      return nullptr;

   op_instr->definitions[0] = instr->definitions[0];
   op_instr->opcode = aco_opcode::v_xnor_b32;
   return op_instr;
}

/* s_not_b32(s_and_b32(a, b)) -> s_nand_b32(a, b)
 * s_not_b32(s_or_b32(a, b)) -> s_nor_b32(a, b)
 * s_not_b32(s_xor_b32(a, b)) -> s_xnor_b32(a, b)
 * s_not_b64(s_and_b64(a, b)) -> s_nand_b64(a, b)
 * s_not_b64(s_or_b64(a, b)) -> s_nor_b64(a, b)
 * s_not_b64(s_xor_b64(a, b)) -> s_xnor_b64(a, b)
 * s_not(cmp(a, b)) -> get_vcmp_inverse(cmp)(a, b) */
Instruction*
apply_s_not(opt_ctx& ctx, aco_ptr<Instruction>& instr, Instruction* op_instr)
{
   if (op_instr->definitions.size() == 1 && ctx.uses[instr->definitions[1].tempId()])
      return nullptr;
   else if (op_instr->definitions.size() == 2 && ctx.uses[op_instr->definitions[1].tempId()])
      return nullptr;

   switch (op_instr->opcode) {
   case aco_opcode::s_and_b32: op_instr->opcode = aco_opcode::s_nand_b32; break;
   case aco_opcode::s_or_b32: op_instr->opcode = aco_opcode::s_nor_b32; break;
   case aco_opcode::s_xor_b32: op_instr->opcode = aco_opcode::s_xnor_b32; break;
   case aco_opcode::s_and_b64: op_instr->opcode = aco_opcode::s_nand_b64; break;
   case aco_opcode::s_or_b64: op_instr->opcode = aco_opcode::s_nor_b64; break;
   case aco_opcode::s_xor_b64: op_instr->opcode = aco_opcode::s_xnor_b64; break;
   default: {
      if (!op_instr->isVOPC())
         return nullptr;
      aco_opcode new_opcode = get_vcmp_inverse(op_instr->opcode);
      if (new_opcode == aco_opcode::num_opcodes)
         return nullptr;
      op_instr->opcode = new_opcode;
   }
   }

   for (unsigned i = 0; i < op_instr->definitions.size(); i++)
      op_instr->definitions[i] = instr->definitions[i];

   return op_instr;
}

/* s_abs_i32(s_sub_[iu]32(a, b)) -> s_absdiff_i32(a, b)
 * s_abs_i32(s_add_[iu]32(a, #b)) -> s_absdiff_i32(a, -b)
 */
Instruction*
apply_s_abs(opt_ctx& ctx, aco_ptr<Instruction>& instr, Instruction* op_instr)
{
   if (op_instr->definitions.size() != 2 || ctx.uses[op_instr->definitions[1].tempId()])
      return nullptr;

   if (op_instr->opcode == aco_opcode::s_add_i32 || op_instr->opcode == aco_opcode::s_add_u32) {
      for (unsigned i = 0; i < 2; i++) {
         uint64_t constant;
         if (op_instr->operands[!i].isLiteral() ||
             !is_operand_constant(ctx, op_instr->operands[i], 32, &constant))
            continue;

         op_instr->operands[0] = op_instr->operands[!i];
         op_instr->operands[1] = Operand::c32(-int32_t(constant));
         goto use_absdiff;
      }
      return nullptr;
   } else if (op_instr->opcode != aco_opcode::s_sub_i32 &&
              op_instr->opcode != aco_opcode::s_sub_u32) {
      return nullptr;
   }

use_absdiff:
   op_instr->opcode = aco_opcode::s_absdiff_i32;
   op_instr->definitions[0] = instr->definitions[0];
   op_instr->definitions[1] = instr->definitions[1];
   return op_instr;
}

Instruction*
apply_clamp(opt_ctx& ctx, aco_ptr<Instruction>& instr, Instruction* parent)
{
   unsigned idx;
   if (!detect_clamp(instr.get(), &idx))
      return nullptr;

   aco_type type = instr_info.alu_opcode_infos[(int)instr->opcode].def_types[0];

   if (!ctx.info[parent->definitions[0].tempId()].is_canonicalized(type.bit_size) &&
       ctx.fp_mode.denorm32 != fp_denorm_keep)
      return nullptr;

   aco_type parent_type = instr_info.alu_opcode_infos[(int)parent->opcode].def_types[0];

   if (!instr_info.alu_opcode_infos[(int)parent->opcode].output_modifiers ||
       type.bit_size != parent_type.bit_size || parent_type.num_components != 1)
      return nullptr;

   alu_opt_info parent_info;
   if (!alu_opt_gather_info(ctx, parent, parent_info))
      return nullptr;

   if (parent_info.uses_insert())
      return nullptr;

   alu_opt_info info;
   if (!alu_opt_gather_info(ctx, instr.get(), info))
      return nullptr;

   if (!backpropagate_input_modifiers(ctx, parent_info, info.operands[idx], type))
      return nullptr;

   parent_info.clamp = true;
   parent_info.defs[0].setTemp(info.defs[0].getTemp());
   if (!alu_opt_info_is_valid(ctx, parent_info))
      return nullptr;
   return alu_opt_info_to_instr(ctx, parent_info, parent);
}

/* Combine an p_insert (or p_extract, in some cases) instruction with instr.
 * p_insert(parent(...)) -> instr_insert().
 */
Instruction*
apply_insert(opt_ctx& ctx, aco_ptr<Instruction>& instr, Instruction* parent)
{
   if (instr->definitions[0].regClass() != v1)
      return nullptr;

   SubdwordSel sel = parse_insert(instr.get());
   if (!sel)
      return nullptr;

   if (ctx.info[instr->operands[0].tempId()].label & temp_labels)
      return nullptr;

   alu_opt_info parent_info;
   if (!alu_opt_gather_info(ctx, parent, parent_info))
      return nullptr;

   if (parent_info.uses_insert())
      return nullptr;

   parent_info.insert = sel;

   parent_info.defs[0].setTemp(instr->definitions[0].getTemp());
   if (!alu_opt_info_is_valid(ctx, parent_info))
      return nullptr;
   return alu_opt_info_to_instr(ctx, parent_info, parent);
}

/* Remove superfluous extract after ds_read like so:
 * p_extract(ds_read_uN(), 0, N, 0) -> ds_read_uN()
 */
Instruction*
apply_load_extract(opt_ctx& ctx, aco_ptr<Instruction>& extract, Instruction* load)
{
   unsigned extract_idx = extract->operands[1].constantValue();
   unsigned bits_extracted = extract->operands[2].constantValue();
   bool sign_ext = extract->operands[3].constantValue();
   unsigned dst_bitsize = extract->definitions[0].bytes() * 8u;

   unsigned bits_loaded = 0;
   bool can_shrink = false;
   switch (load->opcode) {
   case aco_opcode::ds_read_u8:
   case aco_opcode::ds_read_u8_d16:
   case aco_opcode::flat_load_ubyte:
   case aco_opcode::flat_load_ubyte_d16:
   case aco_opcode::global_load_ubyte:
   case aco_opcode::global_load_ubyte_d16:
   case aco_opcode::scratch_load_ubyte:
   case aco_opcode::scratch_load_ubyte_d16: can_shrink = true; FALLTHROUGH;
   case aco_opcode::s_load_ubyte:
   case aco_opcode::s_buffer_load_ubyte:
   case aco_opcode::buffer_load_ubyte:
   case aco_opcode::buffer_load_ubyte_d16: bits_loaded = 8; break;
   case aco_opcode::ds_read_u16:
   case aco_opcode::ds_read_u16_d16:
   case aco_opcode::flat_load_ushort:
   case aco_opcode::flat_load_short_d16:
   case aco_opcode::global_load_ushort:
   case aco_opcode::global_load_short_d16:
   case aco_opcode::scratch_load_ushort:
   case aco_opcode::scratch_load_short_d16: can_shrink = true; FALLTHROUGH;
   case aco_opcode::s_load_ushort:
   case aco_opcode::s_buffer_load_ushort:
   case aco_opcode::buffer_load_ushort:
   case aco_opcode::buffer_load_short_d16: bits_loaded = 16; break;
   default: return nullptr;
   }

   /* TODO: These are doable, but probably don't occur too often. */
   if (extract_idx || bits_extracted > bits_loaded || dst_bitsize > 32 ||
       (load->definitions[0].regClass().type() != extract->definitions[0].regClass().type()))
      return nullptr;

   /* We can't shrink some loads because that would remove zeroing of the offset/address LSBs. */
   if (!can_shrink && bits_extracted < bits_loaded)
      return nullptr;

   /* Shrink the load if the extracted bit size is smaller. */
   bits_loaded = MIN2(bits_loaded, bits_extracted);

   /* Change the opcode so it writes the full register. */
   bool is_s_buffer = load->opcode == aco_opcode::s_buffer_load_ubyte ||
                      load->opcode == aco_opcode::s_buffer_load_ushort;
   if (bits_loaded == 8 && load->isDS())
      load->opcode = sign_ext ? aco_opcode::ds_read_i8 : aco_opcode::ds_read_u8;
   else if (bits_loaded == 16 && load->isDS())
      load->opcode = sign_ext ? aco_opcode::ds_read_i16 : aco_opcode::ds_read_u16;
   else if (bits_loaded == 8 && load->isMUBUF())
      load->opcode = sign_ext ? aco_opcode::buffer_load_sbyte : aco_opcode::buffer_load_ubyte;
   else if (bits_loaded == 16 && load->isMUBUF())
      load->opcode = sign_ext ? aco_opcode::buffer_load_sshort : aco_opcode::buffer_load_ushort;
   else if (bits_loaded == 8 && load->isFlat())
      load->opcode = sign_ext ? aco_opcode::flat_load_sbyte : aco_opcode::flat_load_ubyte;
   else if (bits_loaded == 16 && load->isFlat())
      load->opcode = sign_ext ? aco_opcode::flat_load_sshort : aco_opcode::flat_load_ushort;
   else if (bits_loaded == 8 && load->isGlobal())
      load->opcode = sign_ext ? aco_opcode::global_load_sbyte : aco_opcode::global_load_ubyte;
   else if (bits_loaded == 16 && load->isGlobal())
      load->opcode = sign_ext ? aco_opcode::global_load_sshort : aco_opcode::global_load_ushort;
   else if (bits_loaded == 8 && load->isScratch())
      load->opcode = sign_ext ? aco_opcode::scratch_load_sbyte : aco_opcode::scratch_load_ubyte;
   else if (bits_loaded == 16 && load->isScratch())
      load->opcode = sign_ext ? aco_opcode::scratch_load_sshort : aco_opcode::scratch_load_ushort;
   else if (bits_loaded == 8 && load->isSMEM() && is_s_buffer)
      load->opcode = sign_ext ? aco_opcode::s_buffer_load_sbyte : aco_opcode::s_buffer_load_ubyte;
   else if (bits_loaded == 8 && load->isSMEM() && !is_s_buffer)
      load->opcode = sign_ext ? aco_opcode::s_load_sbyte : aco_opcode::s_load_ubyte;
   else if (bits_loaded == 16 && load->isSMEM() && is_s_buffer)
      load->opcode = sign_ext ? aco_opcode::s_buffer_load_sshort : aco_opcode::s_buffer_load_ushort;
   else if (bits_loaded == 16 && load->isSMEM() && !is_s_buffer)
      load->opcode = sign_ext ? aco_opcode::s_load_sshort : aco_opcode::s_load_ushort;
   else
      UNREACHABLE("Forgot to add opcode above.");

   if (dst_bitsize <= 16 && ctx.program->gfx_level >= GFX9) {
      switch (load->opcode) {
      case aco_opcode::ds_read_i8: load->opcode = aco_opcode::ds_read_i8_d16; break;
      case aco_opcode::ds_read_u8: load->opcode = aco_opcode::ds_read_u8_d16; break;
      case aco_opcode::ds_read_i16: load->opcode = aco_opcode::ds_read_u16_d16; break;
      case aco_opcode::ds_read_u16: load->opcode = aco_opcode::ds_read_u16_d16; break;
      case aco_opcode::buffer_load_sbyte: load->opcode = aco_opcode::buffer_load_sbyte_d16; break;
      case aco_opcode::buffer_load_ubyte: load->opcode = aco_opcode::buffer_load_ubyte_d16; break;
      case aco_opcode::buffer_load_sshort: load->opcode = aco_opcode::buffer_load_short_d16; break;
      case aco_opcode::buffer_load_ushort: load->opcode = aco_opcode::buffer_load_short_d16; break;
      case aco_opcode::flat_load_sbyte: load->opcode = aco_opcode::flat_load_sbyte_d16; break;
      case aco_opcode::flat_load_ubyte: load->opcode = aco_opcode::flat_load_ubyte_d16; break;
      case aco_opcode::flat_load_sshort: load->opcode = aco_opcode::flat_load_short_d16; break;
      case aco_opcode::flat_load_ushort: load->opcode = aco_opcode::flat_load_short_d16; break;
      case aco_opcode::global_load_sbyte: load->opcode = aco_opcode::global_load_sbyte_d16; break;
      case aco_opcode::global_load_ubyte: load->opcode = aco_opcode::global_load_ubyte_d16; break;
      case aco_opcode::global_load_sshort: load->opcode = aco_opcode::global_load_short_d16; break;
      case aco_opcode::global_load_ushort: load->opcode = aco_opcode::global_load_short_d16; break;
      case aco_opcode::scratch_load_sbyte: load->opcode = aco_opcode::scratch_load_sbyte_d16; break;
      case aco_opcode::scratch_load_ubyte: load->opcode = aco_opcode::scratch_load_ubyte_d16; break;
      case aco_opcode::scratch_load_sshort: load->opcode = aco_opcode::scratch_load_short_d16; break;
      case aco_opcode::scratch_load_ushort: load->opcode = aco_opcode::scratch_load_short_d16; break;
      default: break;
      }
   }

   /* The load now produces the exact same thing as the extract, remove the extract. */
   load->definitions[0] = extract->definitions[0];
   return load;
}

Instruction*
apply_f2f16(opt_ctx& ctx, aco_ptr<Instruction>& instr, Instruction* parent)
{
   if (instr->valu().omod)
      return nullptr;

   alu_opt_info info;
   if (!alu_opt_gather_info(ctx, instr.get(), info))
      return nullptr;
   aco_type type = {aco_base_type_float, 1, 32};

   alu_opt_info parent_info;
   if (!alu_opt_gather_info(ctx, parent, parent_info))
      return nullptr;

   if (parent_info.uses_insert() || parent_info.f32_to_f16)
      return nullptr;

   if (!backpropagate_input_modifiers(ctx, parent_info, info.operands[0], type))
      return nullptr;

   parent_info.f32_to_f16 = true;
   parent_info.clamp |= info.clamp;

   parent_info.defs[0].setTemp(info.defs[0].getTemp());
   if (!alu_opt_info_is_valid(ctx, parent_info))
      return nullptr;
   return alu_opt_info_to_instr(ctx, parent_info, parent);
}

bool
op_info_get_constant(opt_ctx& ctx, alu_opt_op op_info, aco_type type, uint64_t* res)
{
   if (op_info.op.isTemp()) {
      unsigned id = original_temp_id(ctx, op_info.op.getTemp());
      if (ctx.info[id].is_constant())
         op_info.op = get_constant_op(ctx, ctx.info[id], type.bytes() * 8);
   }
   if (!op_info.op.isConstant())
      return false;
   *res = op_info.constant_after_mods(ctx, type);
   return true;
}

/* neg(mul(a, b)) -> mul(neg(a), b), abs(mul(a, b)) -> mul(abs(a), abs(b)) */
Instruction*
apply_output_mul(opt_ctx& ctx, aco_ptr<Instruction>& instr, Instruction* parent)
{
   alu_opt_info info;
   if (!alu_opt_gather_info(ctx, instr.get(), info))
      return nullptr;
   aco_type type = instr_info.alu_opcode_infos[(int)instr->opcode].def_types[0];

   unsigned denorm_mode = type.bit_size == 32 ? ctx.fp_mode.denorm32 : ctx.fp_mode.denorm16_64;
   if (!ctx.info[parent->definitions[0].tempId()].is_canonicalized(type.bit_size) &&
       denorm_mode != fp_denorm_keep)
      return nullptr;

   aco_type parent_type = instr_info.alu_opcode_infos[(int)parent->opcode].def_types[0];

   if (type.num_components != parent_type.num_components || type.bit_size != parent_type.bit_size ||
       instr->definitions[0].regClass().type() != parent->definitions[0].regClass().type())
      return nullptr;

   unsigned cidx = !info.operands[0].op.isConstant();

   uint64_t constant = 0;
   if (!op_info_get_constant(ctx, info.operands[cidx], type, &constant))
      return nullptr;

   unsigned omod = 0;

   for (unsigned i = 0; i < type.num_components; i++) {
      double val = extract_float(constant, type.bit_size, i);
      if (val < 0.0) {
         val = fabs(val);
         info.operands[!cidx].neg[i] ^= true;
      }

      if (val == 1.0)
         omod = 0;
      else if (val == 2.0)
         omod = 1;
      else if (val == 4.0)
         omod = 2;
      else if (val == 0.5)
         omod = 3;
      else
         return nullptr;

      if (omod && type.num_components != 1)
         return nullptr;
   }

   if (omod && (info.omod || denorm_mode != fp_denorm_flush ||
                (info.opcode != aco_opcode::v_mul_legacy_f32 && info.defs[0].isSZPreserve())))
      return nullptr;

   omod |= info.omod;

   if ((omod || info.clamp) && !instr_info.alu_opcode_infos[(int)parent->opcode].output_modifiers)
      return nullptr;

   alu_opt_info parent_info;
   if (!alu_opt_gather_info(ctx, parent, parent_info))
      return nullptr;

   if (parent_info.uses_insert() || (omod && (parent_info.omod || parent_info.clamp)))
      return nullptr;

   if (!backpropagate_input_modifiers(ctx, parent_info, info.operands[!cidx], type))
      return nullptr;

   parent_info.clamp |= info.clamp;
   parent_info.omod |= omod;
   parent_info.insert = info.insert;
   parent_info.defs[0].setTemp(info.defs[0].getTemp());
   if (!alu_opt_info_is_valid(ctx, parent_info))
      return nullptr;
   return alu_opt_info_to_instr(ctx, parent_info, parent);
}

Instruction*
apply_output_impl(opt_ctx& ctx, aco_ptr<Instruction>& instr, Instruction* parent)
{
   if (instr->opcode == aco_opcode::p_extract &&
       (parent->isDS() || parent->isSMEM() || parent->isMUBUF() || parent->isFlatLike()))
      return apply_load_extract(ctx, instr, parent);
   else if (instr->opcode == aco_opcode::p_extract || instr->opcode == aco_opcode::p_insert)
      return apply_insert(ctx, instr, parent);
   else if (instr->opcode == aco_opcode::v_not_b32)
      return apply_v_not(ctx, instr, parent);
   else if (instr->opcode == aco_opcode::s_not_b32 || instr->opcode == aco_opcode::s_not_b64)
      return apply_s_not(ctx, instr, parent);
   else if (instr->opcode == aco_opcode::s_abs_i32)
      return apply_s_abs(ctx, instr, parent);
   else if (instr->opcode == aco_opcode::v_mul_f64 || instr->opcode == aco_opcode::v_mul_f64_e64 ||
            instr->opcode == aco_opcode::v_mul_f32 || instr->opcode == aco_opcode::v_mul_f16 ||
            instr->opcode == aco_opcode::v_pk_mul_f16 ||
            instr->opcode == aco_opcode::v_mul_legacy_f32 ||
            instr->opcode == aco_opcode::s_mul_f32 || instr->opcode == aco_opcode::s_mul_f16)
      return apply_output_mul(ctx, instr, parent);
   else if (instr->opcode == aco_opcode::v_cvt_f16_f32)
      return apply_f2f16(ctx, instr, parent);
   else if (instr->opcode == aco_opcode::v_med3_f32 || instr->opcode == aco_opcode::v_med3_f16)
      return apply_clamp(ctx, instr, parent);
   else
      UNREACHABLE("unhandled opcode");

   return nullptr;
}

bool
apply_output(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   switch (instr->opcode) {
   case aco_opcode::p_extract:
   case aco_opcode::p_insert:
   case aco_opcode::v_not_b32:
   case aco_opcode::s_not_b32:
   case aco_opcode::s_not_b64:
   case aco_opcode::s_abs_i32:
   case aco_opcode::v_mul_f64:
   case aco_opcode::v_mul_f64_e64:
   case aco_opcode::v_mul_f32:
   case aco_opcode::v_mul_f16:
   case aco_opcode::v_pk_mul_f16:
   case aco_opcode::v_mul_legacy_f32:
   case aco_opcode::s_mul_f32:
   case aco_opcode::s_mul_f16:
   case aco_opcode::v_cvt_f16_f32:
   case aco_opcode::v_med3_f32:
   case aco_opcode::v_med3_f16: break;
   default: return false;
   }

   int temp_idx = -1;
   for (unsigned i = 0; i < instr->operands.size(); i++) {
      if (temp_idx < 0 && instr->operands[i].isTemp())
         temp_idx = i;
      else if (instr->operands[i].isConstant())
         continue;
      else
         return false;
   }

   if (temp_idx < 0)
      return false;

   unsigned tmpid = instr->operands[temp_idx].tempId();
   Instruction* parent = ctx.info[tmpid].parent_instr;
   if (ctx.uses[tmpid] != 1 || parent->definitions[0].tempId() != tmpid)
      return false;

   int64_t alt_idx = ctx.info[tmpid].is_combined() ? ctx.info[tmpid].val : -1;
   aco::small_vec<Operand, 4> pre_opt_ops;
   for (const Operand& op : parent->operands)
      pre_opt_ops.push_back(op);

   Instruction* new_instr = apply_output_impl(ctx, instr, parent);

   if (new_instr == nullptr)
      return false;

   for (const Operand& op : parent->operands) {
      if (op.isTemp())
         ctx.uses[op.tempId()]++;
   }
   for (const Operand& op : pre_opt_ops) {
      if (op.isTemp())
         decrease_and_dce(ctx, op.getTemp());
   }

   ctx.uses[tmpid] = 0;
   ctx.info[tmpid].parent_instr = nullptr;

   if (new_instr != parent)
      ctx.replacement_instr.emplace(parent, new_instr);

   if (alt_idx >= 0) {
      Instruction* new_pre_combine =
         apply_output_impl(ctx, instr, ctx.pre_combine_instrs[alt_idx].get());

      if (new_pre_combine != ctx.pre_combine_instrs[alt_idx].get())
         ctx.pre_combine_instrs[alt_idx].reset(new_pre_combine);

      if (new_pre_combine)
         ctx.info[new_instr->definitions[0].tempId()].set_combined(alt_idx);
   }

   for (Definition& def : new_instr->definitions) {
      ctx.info[def.tempId()].parent_instr = new_instr;
      ctx.info[def.tempId()].label &= canonicalized_labels | label_combined_instr;
   }

   instr.reset();
   return true;
}

bool
create_fma_cb(opt_ctx& ctx, alu_opt_info& info)
{
   if (!info.defs[0].isPrecise())
      return true;

   aco_type type = instr_info.alu_opcode_infos[(int)info.opcode].def_types[0];

   for (unsigned op_idx = 0; op_idx < 2; op_idx++) {
      uint64_t constant = 0;
      if (!op_info_get_constant(ctx, info.operands[op_idx], type, &constant))
         continue;

      for (unsigned comp = 0; comp < type.num_components; comp++) {
         double val = extract_float(constant, type.bit_size, comp);
         /* Check if the value is a power of two. */
         if (fabs(val) < 1.0)
            return false;
         if (dui(val) & 0xf'ffff'ffff'ffffull)
            return false;
      }

      return true;
   }

   return false;
}

template <bool max_first>
bool
create_med3_cb(opt_ctx& ctx, alu_opt_info& info)
{
   aco_type type = instr_info.alu_opcode_infos[(int)info.opcode].def_types[0];

   /* NaN correctness needs max first, then min. */
   if (!max_first && type.base_type == aco_base_type_float && info.defs[0].isPrecise())
      return false;

   uint64_t upper = 0;
   uint64_t lower = 0;

   if (!op_info_get_constant(ctx, info.operands[0], type, &upper))
      return false;

   if (!op_info_get_constant(ctx, info.operands[1], type, &lower) &&
       !op_info_get_constant(ctx, info.operands[2], type, &lower))
      return false;

   if (!max_first)
      std::swap(upper, lower);

   switch (info.opcode) {
   case aco_opcode::v_med3_f32: return uif(lower) <= uif(upper);
   case aco_opcode::v_med3_f16: return _mesa_half_to_float(lower) <= _mesa_half_to_float(upper);
   case aco_opcode::v_med3_u32: return uint32_t(lower) <= uint32_t(upper);
   case aco_opcode::v_med3_u16: return uint16_t(lower) <= uint16_t(upper);
   case aco_opcode::v_med3_i32: return int32_t(lower) <= int32_t(upper);
   case aco_opcode::v_med3_i16: return int16_t(lower) <= int16_t(upper);
   default: UNREACHABLE("invalid clamp");
   }
   return false;
}

template <unsigned bits>
bool
shift_to_mad_cb(opt_ctx& ctx, alu_opt_info& info)
{
   aco_type type = {aco_base_type_uint, 1, 32};
   uint64_t constant = 0;
   if (!op_info_get_constant(ctx, info.operands[1], type, &constant))
      return false;

   info.operands[1] = {Operand::c32(1u << (constant % bits))};
   return true;
}

bool
check_mul_u24_cb(opt_ctx& ctx, alu_opt_info& info)
{
   aco_type type = {aco_base_type_uint, 1, 32};
   for (unsigned i = 0; i < 2; i++) {
      uint64_t constant = 0;
      if (op_info_get_constant(ctx, info.operands[i], type, &constant)) {
         if (constant > 0xff'ffffu)
            return false;
      } else if (!info.operands[i].op.is24bit() && !info.operands[i].op.is16bit()) {
         return false;
      }
   }

   return true;
}

bool
neg_mul_to_i24_cb(opt_ctx& ctx, alu_opt_info& info)
{
   aco_type type = {aco_base_type_uint, 1, 32};
   for (unsigned i = 0; i < 2; i++) {
      /* v_mad_i32_i24 sign extends, so is16bit is the best thing we have. */
      if (!info.operands[!i].op.is16bit())
         continue;
      uint64_t constant = 0;
      if (!op_info_get_constant(ctx, info.operands[i], type, &constant))
         continue;

      int32_t multiplier = -constant;
      if (multiplier < int32_t(0xff80'0000) || multiplier > 0x007f'ffff)
         return false;
      info.operands[i] = {Operand::c32(multiplier)};
      return true;
   }

   return false;
}

bool
add_lm_def_cb(opt_ctx& ctx, alu_opt_info& info)
{
   info.defs.push_back(Definition(ctx.program->allocateTmp(ctx.program->lane_mask)));
   /* Make sure the uses vector is large enough and the number of
    * uses properly initialized to 0.
    */
   ctx.uses.push_back(0);
   ctx.info.push_back(ssa_info{});
   return true;
}

bool
pop_def_cb(opt_ctx& ctx, alu_opt_info& info)
{
   assert(ctx.uses[info.defs.back().tempId()] == 0);
   assert(info.defs.size() >= 2);
   info.defs.pop_back();
   return true;
}

bool
check_constant(opt_ctx& ctx, alu_opt_info& info, unsigned idx, uint32_t expected)
{
   assert(idx < info.operands.size());
   aco_type type = {aco_base_type_uint, 1, 32}; /* maybe param in the future, if needed. */
   uint64_t constant;
   return op_info_get_constant(ctx, info.operands[idx], type, &constant) && constant == expected;
}

template <unsigned idx, uint32_t expected>
bool
check_const_cb(opt_ctx& ctx, alu_opt_info& info)
{
   return check_constant(ctx, info, idx, expected);
}

template <uint32_t expected>
bool
remove_const_cb(opt_ctx& ctx, alu_opt_info& info)
{
   if (!check_constant(ctx, info, info.operands.size() - 1, expected))
      return false;
   info.operands.pop_back();
   return true;
}

template <unsigned idx, uint32_t constant>
bool
insert_const_cb(opt_ctx& ctx, alu_opt_info& info)
{
   assert(idx <= info.operands.size());
   info.operands.insert(info.operands.begin() + idx, {Operand::c32(constant)});
   return true;
}

template <combine_instr_callback func1, combine_instr_callback func2>
bool
and_cb(opt_ctx& ctx, alu_opt_info& info)
{
   return func1(ctx, info) && func2(ctx, info);
}

void
combine_instruction(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (instr->definitions.empty() || is_dead(ctx.uses, instr.get()))
      return;

   for (const Definition& def : instr->definitions) {
      ssa_info& info = ctx.info[def.tempId()];
      if (info.is_extract() && ctx.uses[def.tempId()] > 4)
         info.label &= ~label_extract;
   }

   if (instr->isVALU() || instr->isSALU()) {
      /* Apply SDWA. Do this after label_instruction() so it can remove
       * label_extract if not all instructions can take SDWA. */
      alu_propagate_temp_const(ctx, instr, true);
   }

   if (instr->isDPP())
      return;

   if (!instr->isVALU() && !instr->isSALU() && !instr->isPseudo())
      return;

   if (apply_output(ctx, instr))
      return;

   /* TODO: There are still some peephole optimizations that could be done:
    * - abs(a - b) -> s_absdiff_i32
    * - various patterns for s_bitcmp{0,1}_b32 and s_bitset{0,1}_b32
    * - patterns for v_alignbit_b32 and v_alignbyte_b32
    * These aren't probably too interesting though.
    * There are also patterns for v_cmp_class_f{16,32,64}. This is difficult but
    * probably more useful than the previously mentioned optimizations.
    * The various comparison optimizations also currently only work with 32-bit
    * floats. */

   alu_opt_info info;
   if (!alu_opt_gather_info(ctx, instr.get(), info))
      return;

   aco::small_vec<combine_instr_pattern, 8> patterns;

/* Variadic macro to make callback optional and to allow templates<a, b>. */
#define add_opt(src_op, res_op, mask, swizzle, ...)                                                \
   patterns.push_back(                                                                             \
      combine_instr_pattern{aco_opcode::src_op, aco_opcode::res_op, mask, swizzle, __VA_ARGS__})

   if (info.opcode == aco_opcode::v_add_f32) {
      if (ctx.program->gfx_level < GFX10_3 && ctx.program->family != CHIP_GFX940 &&
          ctx.fp_mode.denorm32 == 0) {
         add_opt(v_mul_f32, v_mad_f32, 0x3, "120");
         add_opt(v_mul_legacy_f32, v_mad_legacy_f32, 0x3, "120");
      }
      if (ctx.program->dev.has_fast_fma32) {
         add_opt(v_mul_f32, v_fma_f32, 0x3, "120", create_fma_cb);
         add_opt(s_mul_f32, v_fma_f32, 0x3, "120", create_fma_cb);
      }
      if (ctx.program->gfx_level >= GFX10_3)
         add_opt(v_mul_legacy_f32, v_fma_legacy_f32, 0x3, "120", create_fma_cb);
   } else if (info.opcode == aco_opcode::v_add_f16) {
      if (ctx.program->gfx_level < GFX9 && ctx.fp_mode.denorm16_64 == 0) {
         add_opt(v_mul_f16, v_mad_legacy_f16, 0x3, "120");
      } else if (ctx.program->gfx_level < GFX10 && ctx.fp_mode.denorm16_64 == 0) {
         add_opt(v_mul_f16, v_mad_f16, 0x3, "120");
         add_opt(v_pk_mul_f16, v_mad_f16, 0x3, "120");
      }

      if (ctx.program->gfx_level < GFX9) {
         add_opt(v_mul_f16, v_fma_legacy_f16, 0x3, "120", create_fma_cb);
      } else {
         add_opt(v_mul_f16, v_fma_f16, 0x3, "120", create_fma_cb);
         add_opt(s_mul_f16, v_fma_f16, 0x3, "120", create_fma_cb);
         add_opt(v_pk_mul_f16, v_fma_f16, 0x3, "120", create_fma_cb);
      }
   } else if (info.opcode == aco_opcode::v_add_f64) {
      add_opt(v_mul_f64, v_fma_f64, 0x3, "120", create_fma_cb);
   } else if (info.opcode == aco_opcode::v_add_f64_e64) {
      add_opt(v_mul_f64_e64, v_fma_f64, 0x3, "120", create_fma_cb);
   } else if (info.opcode == aco_opcode::s_add_f32) {
      add_opt(s_mul_f32, s_fmac_f32, 0x3, "120", create_fma_cb);
   } else if (info.opcode == aco_opcode::s_add_f16) {
      add_opt(s_mul_f16, s_fmac_f16, 0x3, "120", create_fma_cb);
   } else if (info.opcode == aco_opcode::v_pk_add_f16) {
      add_opt(v_pk_mul_f16, v_pk_fma_f16, 0x3, "120", create_fma_cb);
      add_opt(v_mul_f16, v_pk_fma_f16, 0x3, "120", create_fma_cb);
      add_opt(s_mul_f16, v_pk_fma_f16, 0x3, "120", create_fma_cb);
   } else if (info.opcode == aco_opcode::v_max_f32) {
      add_opt(v_max_f32, v_max3_f32, 0x3, "120", nullptr, true);
      add_opt(s_max_f32, v_max3_f32, 0x3, "120", nullptr, true);
      if (ctx.program->gfx_level >= GFX11) {
         add_opt(v_min_f32, v_minmax_f32, 0x3, "120", nullptr, true);
         add_opt(s_min_f32, v_minmax_f32, 0x3, "120", nullptr, true);
      } else {
         add_opt(v_min_f32, v_med3_f32, 0x3, "012", create_med3_cb<false>, true);
      }
   } else if (info.opcode == aco_opcode::v_min_f32) {
      add_opt(v_min_f32, v_min3_f32, 0x3, "120", nullptr, true);
      add_opt(s_min_f32, v_min3_f32, 0x3, "120", nullptr, true);
      if (ctx.program->gfx_level >= GFX11) {
         add_opt(v_max_f32, v_maxmin_f32, 0x3, "120", nullptr, true);
         add_opt(s_max_f32, v_maxmin_f32, 0x3, "120", nullptr, true);
      } else {
         add_opt(v_max_f32, v_med3_f32, 0x3, "012", create_med3_cb<true>, true);
      }
   } else if (info.opcode == aco_opcode::v_max_u32) {
      add_opt(v_max_u32, v_max3_u32, 0x3, "120", nullptr, true);
      add_opt(s_max_u32, v_max3_u32, 0x3, "120", nullptr, true);
      if (ctx.program->gfx_level >= GFX11) {
         add_opt(v_min_u32, v_minmax_u32, 0x3, "120", nullptr, true);
         add_opt(s_min_u32, v_minmax_u32, 0x3, "120", nullptr, true);
      } else {
         add_opt(v_min_u32, v_med3_u32, 0x3, "012", create_med3_cb<false>, true);
         add_opt(s_min_u32, v_med3_u32, 0x3, "012", create_med3_cb<false>, true);
      }
   } else if (info.opcode == aco_opcode::v_min_u32) {
      add_opt(v_min_u32, v_min3_u32, 0x3, "120", nullptr, true);
      add_opt(s_min_u32, v_min3_u32, 0x3, "120", nullptr, true);
      if (ctx.program->gfx_level >= GFX11) {
         add_opt(v_max_u32, v_maxmin_u32, 0x3, "120", nullptr, true);
         add_opt(s_max_u32, v_maxmin_u32, 0x3, "120", nullptr, true);
      } else {
         add_opt(v_max_u32, v_med3_u32, 0x3, "012", create_med3_cb<true>, true);
         add_opt(s_max_u32, v_med3_u32, 0x3, "012", create_med3_cb<true>, true);
      }
   } else if (info.opcode == aco_opcode::v_max_i32) {
      add_opt(v_max_i32, v_max3_i32, 0x3, "120", nullptr, true);
      add_opt(s_max_i32, v_max3_i32, 0x3, "120", nullptr, true);
      if (ctx.program->gfx_level >= GFX11) {
         add_opt(v_min_i32, v_minmax_i32, 0x3, "120", nullptr, true);
         add_opt(s_min_i32, v_minmax_i32, 0x3, "120", nullptr, true);
      } else {
         add_opt(v_min_i32, v_med3_i32, 0x3, "012", create_med3_cb<false>, true);
         add_opt(s_min_i32, v_med3_i32, 0x3, "012", create_med3_cb<false>, true);
      }
   } else if (info.opcode == aco_opcode::v_min_i32) {
      add_opt(v_min_i32, v_min3_i32, 0x3, "120", nullptr, true);
      add_opt(s_min_i32, v_min3_i32, 0x3, "120", nullptr, true);
      if (ctx.program->gfx_level >= GFX11) {
         add_opt(v_max_i32, v_maxmin_i32, 0x3, "120", nullptr, true);
         add_opt(s_max_i32, v_maxmin_i32, 0x3, "120", nullptr, true);
      } else {
         add_opt(v_max_i32, v_med3_i32, 0x3, "012", create_med3_cb<true>, true);
         add_opt(s_max_i32, v_med3_i32, 0x3, "012", create_med3_cb<true>, true);
      }
   } else if (info.opcode == aco_opcode::v_max_f16 && ctx.program->gfx_level >= GFX9) {
      add_opt(v_max_f16, v_max3_f16, 0x3, "120", nullptr, true);
      add_opt(s_max_f16, v_max3_f16, 0x3, "120", nullptr, true);
      if (ctx.program->gfx_level >= GFX11) {
         add_opt(v_min_f16, v_minmax_f16, 0x3, "120", nullptr, true);
         add_opt(s_min_f16, v_minmax_f16, 0x3, "120", nullptr, true);
      } else {
         add_opt(v_min_f16, v_med3_f16, 0x3, "012", create_med3_cb<false>, true);
      }
   } else if (info.opcode == aco_opcode::v_min_f16 && ctx.program->gfx_level >= GFX9) {
      add_opt(v_min_f16, v_min3_f16, 0x3, "120", nullptr, true);
      add_opt(s_min_f16, v_min3_f16, 0x3, "120", nullptr, true);
      if (ctx.program->gfx_level >= GFX11) {
         add_opt(v_max_f16, v_maxmin_f16, 0x3, "120", nullptr, true);
         add_opt(s_max_f16, v_maxmin_f16, 0x3, "120", nullptr, true);
      } else {
         add_opt(v_max_f16, v_med3_f16, 0x3, "012", create_med3_cb<true>, true);
      }
   } else if (info.opcode == aco_opcode::v_max_u16 && ctx.program->gfx_level >= GFX9) {
      add_opt(v_max_u16, v_max3_u16, 0x3, "120", nullptr, true);
      add_opt(v_min_u16, v_med3_u16, 0x3, "012", create_med3_cb<false>, true);
   } else if (info.opcode == aco_opcode::v_min_u16 && ctx.program->gfx_level >= GFX9) {
      add_opt(v_min_u16, v_min3_u16, 0x3, "120", nullptr, true);
      add_opt(v_max_u16, v_med3_u16, 0x3, "012", create_med3_cb<true>, true);
   } else if (info.opcode == aco_opcode::v_max_i16 && ctx.program->gfx_level >= GFX9) {
      add_opt(v_max_i16, v_max3_i16, 0x3, "120", nullptr, true);
      add_opt(v_min_i16, v_med3_i16, 0x3, "012", create_med3_cb<false>, true);
   } else if (info.opcode == aco_opcode::v_min_i16 && ctx.program->gfx_level >= GFX9) {
      add_opt(v_min_i16, v_min3_i16, 0x3, "120", nullptr, true);
      add_opt(v_max_i16, v_med3_i16, 0x3, "012", create_med3_cb<true>, true);
   } else if (info.opcode == aco_opcode::v_max_u16_e64) {
      add_opt(v_max_u16_e64, v_max3_u16, 0x3, "120", nullptr, true);
      add_opt(v_min_u16_e64, v_med3_u16, 0x3, "012", create_med3_cb<false>, true);
   } else if (info.opcode == aco_opcode::v_min_u16_e64) {
      add_opt(v_min_u16_e64, v_min3_u16, 0x3, "120", nullptr, true);
      add_opt(v_max_u16_e64, v_med3_u16, 0x3, "012", create_med3_cb<true>, true);
   } else if (info.opcode == aco_opcode::v_max_i16_e64) {
      add_opt(v_max_i16_e64, v_max3_i16, 0x3, "120", nullptr, true);
      add_opt(v_min_i16_e64, v_med3_i16, 0x3, "012", create_med3_cb<false>, true);
   } else if (info.opcode == aco_opcode::v_min_i16_e64) {
      add_opt(v_min_i16_e64, v_min3_i16, 0x3, "120", nullptr, true);
      add_opt(v_max_i16_e64, v_med3_i16, 0x3, "012", create_med3_cb<true>, true);
   } else if (((info.opcode == aco_opcode::v_mul_f32 && !info.defs[0].isNaNPreserve() &&
                !info.defs[0].isInfPreserve()) ||
               (info.opcode == aco_opcode::v_mul_legacy_f32 && !info.defs[0].isSZPreserve())) &&
              !info.clamp && !info.omod && !ctx.fp_mode.must_flush_denorms32) {
      /* v_mul_f32(a, v_cndmask_b32(0, 1.0, cond)) -> v_cndmask_b32(0, a, cond) */
      add_opt(v_cndmask_b32, v_cndmask_b32, 0x3, "1032",
              and_cb<check_const_cb<0, 0>, remove_const_cb<0x3f800000>>, true);
      /* v_mul_f32(a, v_cndmask_b32(1.0, 0, cond)) -> v_cndmask_b32(a, 0, cond) */
      add_opt(v_cndmask_b32, v_cndmask_b32, 0x3, "0231",
              and_cb<check_const_cb<1, 0>, remove_const_cb<0x3f800000>>, true);
   } else if (info.opcode == aco_opcode::v_add_u16 && !info.clamp) {
      if (ctx.program->gfx_level < GFX9) {
         add_opt(v_mul_lo_u16, v_mad_legacy_u16, 0x3, "120");
      } else {
         add_opt(v_mul_lo_u16, v_mad_u16, 0x3, "120");
         add_opt(v_pk_mul_lo_u16, v_mad_u16, 0x3, "120");
      }
   } else if (info.opcode == aco_opcode::v_add_u16_e64 && !info.clamp) {
      add_opt(v_mul_lo_u16_e64, v_mad_u16, 0x3, "120");
      add_opt(v_pk_mul_lo_u16, v_mad_u16, 0x3, "120");
   } else if (info.opcode == aco_opcode::v_pk_add_u16 && !info.clamp) {
      add_opt(v_pk_mul_lo_u16, v_pk_mad_u16, 0x3, "120");
      if (ctx.program->gfx_level < GFX10)
         add_opt(v_mul_lo_u16, v_pk_mad_u16, 0x3, "120");
      else
         add_opt(v_mul_lo_u16_e64, v_pk_mad_u16, 0x3, "120");
   } else if (info.opcode == aco_opcode::v_or_b32) {
      add_opt(v_not_b32, v_bfi_b32, 0x3, "10", insert_const_cb<2, UINT32_MAX>, true);
      add_opt(s_not_b32, v_bfi_b32, 0x3, "10", insert_const_cb<2, UINT32_MAX>, true);
      if (ctx.program->gfx_level >= GFX9) {
         add_opt(v_or_b32, v_or3_b32, 0x3, "012", nullptr, true);
         add_opt(s_or_b32, v_or3_b32, 0x3, "012", nullptr, true);
         add_opt(v_lshlrev_b32, v_lshl_or_b32, 0x3, "210", nullptr, true);
         add_opt(s_lshl_b32, v_lshl_or_b32, 0x3, "120", nullptr, true);
         add_opt(v_and_b32, v_and_or_b32, 0x3, "120", nullptr, true);
         add_opt(s_and_b32, v_and_or_b32, 0x3, "120", nullptr, true);
      }
   } else if (info.opcode == aco_opcode::v_xor_b32 && ctx.program->gfx_level >= GFX10) {
      add_opt(v_xor_b32, v_xor3_b32, 0x3, "012", nullptr, true);
      add_opt(s_xor_b32, v_xor3_b32, 0x3, "012", nullptr, true);
      add_opt(v_not_b32, v_xnor_b32, 0x3, "01", nullptr, true);
      add_opt(s_not_b32, v_xnor_b32, 0x3, "01", nullptr, true);
   } else if (info.opcode == aco_opcode::v_add_u32 && !info.clamp) {
      assert(ctx.program->gfx_level >= GFX9);
      add_opt(v_bcnt_u32_b32, v_bcnt_u32_b32, 0x3, "102", remove_const_cb<0>, true);
      add_opt(s_bcnt1_i32_b32, v_bcnt_u32_b32, 0x3, "10", nullptr, true);
      add_opt(v_mbcnt_lo_u32_b32, v_mbcnt_lo_u32_b32, 0x3, "102", remove_const_cb<0>, true);
      add_opt(v_mbcnt_hi_u32_b32_e64, v_mbcnt_hi_u32_b32_e64, 0x3, "102", remove_const_cb<0>, true);
      add_opt(v_mad_u32_u16, v_mad_u32_u16, 0x3, "1203", remove_const_cb<0>, true);
      add_opt(v_mul_u32_u24, v_mad_u32_u24, 0x3, "120", nullptr, true);
      add_opt(v_mul_i32_i24, v_mad_i32_i24, 0x3, "120", nullptr, true);
      add_opt(v_xor_b32, v_xad_u32, 0x3, "120", nullptr, true);
      add_opt(s_xor_b32, v_xad_u32, 0x3, "120", nullptr, true);
      add_opt(v_add_u32, v_add3_u32, 0x3, "012", nullptr, true);
      add_opt(s_add_u32, v_add3_u32, 0x3, "012", nullptr, true);
      add_opt(s_add_i32, v_add3_u32, 0x3, "012", nullptr, true);
      add_opt(v_lshlrev_b32, v_lshl_add_u32, 0x3, "210", nullptr, true);
      add_opt(s_lshl_b32, v_lshl_add_u32, 0x3, "120", nullptr, true);
      add_opt(s_mul_i32, v_mad_u32_u24, 0x3, "120", check_mul_u24_cb, true);
      /* v_add_u32(a, v_cndmask_b32(0, 1, cond)) -> v_addc_co_u32(a, 0, cond) */
      add_opt(v_cndmask_b32, v_addc_co_u32, 0x3, "0132",
              and_cb<and_cb<check_const_cb<1, 0>, remove_const_cb<1>>, add_lm_def_cb>, true);
      /* v_add_u32(a, v_cndmask_b32(1, 0, cond)) -> v_subb_co_u32(a, -1, cond) */
      add_opt(v_cndmask_b32, v_subb_co_u32, 0x3, "0321",
              and_cb<and_cb<remove_const_cb<1>, remove_const_cb<0>>,
                     and_cb<insert_const_cb<1, UINT32_MAX>, add_lm_def_cb>>,
              true);
   } else if ((info.opcode == aco_opcode::v_add_co_u32 ||
               info.opcode == aco_opcode::v_add_co_u32_e64) &&
              !info.clamp) {
      /* v_add_co_u32(a, v_cndmask_b32(0, 1, cond)) -> v_addc_co_u32(a, 0, cond) */
      add_opt(v_cndmask_b32, v_addc_co_u32, 0x3, "0132",
              and_cb<check_const_cb<1, 0>, remove_const_cb<1>>);
      if (ctx.uses[info.defs[1].tempId()] == 0) {
         /* v_add_co_u32(a, v_cndmask_b32(1, 0, cond)) -> v_subb_co_u32(a, -1, cond) */
         add_opt(
            v_cndmask_b32, v_subb_co_u32, 0x3, "0321",
            and_cb<and_cb<remove_const_cb<1>, remove_const_cb<0>>, insert_const_cb<1, UINT32_MAX>>);
         add_opt(v_bcnt_u32_b32, v_bcnt_u32_b32, 0x3, "102",
                 and_cb<remove_const_cb<0>, pop_def_cb>);
         add_opt(s_bcnt1_i32_b32, v_bcnt_u32_b32, 0x3, "10", pop_def_cb);
         add_opt(v_mbcnt_lo_u32_b32, v_mbcnt_lo_u32_b32, 0x3, "102",
                 and_cb<remove_const_cb<0>, pop_def_cb>);
         add_opt(v_mbcnt_hi_u32_b32, v_mbcnt_hi_u32_b32, 0x3, "102",
                 and_cb<remove_const_cb<0>, pop_def_cb>);
         add_opt(v_mbcnt_hi_u32_b32_e64, v_mbcnt_hi_u32_b32_e64, 0x3, "102",
                 and_cb<remove_const_cb<0>, pop_def_cb>);
         add_opt(v_mul_u32_u24, v_mad_u32_u24, 0x3, "120", pop_def_cb);
         add_opt(v_mul_i32_i24, v_mad_i32_i24, 0x3, "120", pop_def_cb);
         add_opt(v_lshlrev_b32, v_mad_u32_u24, 0x3, "210",
                 and_cb<and_cb<shift_to_mad_cb<32>, check_mul_u24_cb>, pop_def_cb>);
         add_opt(s_lshl_b32, v_mad_u32_u24, 0x3, "120",
                 and_cb<and_cb<shift_to_mad_cb<32>, check_mul_u24_cb>, pop_def_cb>);
         add_opt(s_mul_i32, v_mad_u32_u24, 0x3, "120", and_cb<check_mul_u24_cb, pop_def_cb>);
      }
   } else if (info.opcode == aco_opcode::v_sub_u32 && !info.clamp) {
      assert(ctx.program->gfx_level >= GFX9);
      /* v_sub_u32(0, v_cndmask_b32(0, 1, cond)) -> v_cndmask_b32(0, -1, cond) */
      add_opt(v_cndmask_b32, v_cndmask_b32, 0x2, "0312",
              and_cb<and_cb<and_cb<check_const_cb<0, 0>, remove_const_cb<1>>, remove_const_cb<0>>,
                     insert_const_cb<1, UINT32_MAX>>);
      /* v_sub_u32(a, v_cndmask_b32(0, 1, cond)) -> v_subb_co_u32(a, 0, cond) */
      add_opt(v_cndmask_b32, v_subb_co_u32, 0x2, "0132",
              and_cb<and_cb<check_const_cb<1, 0>, remove_const_cb<1>>, add_lm_def_cb>);
      /* v_sub_u32(a, v_cndmask_b32(1, 0, cond)) -> v_addc_co_u32(a, -1, cond) */
      add_opt(v_cndmask_b32, v_addc_co_u32, 0x2, "0321",
              and_cb<and_cb<remove_const_cb<1>, remove_const_cb<0>>,
                     and_cb<insert_const_cb<1, UINT32_MAX>, add_lm_def_cb>>);
      add_opt(v_lshlrev_b32, v_mad_i32_i24, 0x2, "210",
              and_cb<shift_to_mad_cb<32>, neg_mul_to_i24_cb>);
      add_opt(s_lshl_b32, v_mad_i32_i24, 0x2, "120",
              and_cb<shift_to_mad_cb<32>, neg_mul_to_i24_cb>);
      add_opt(v_mul_u32_u24, v_mad_i32_i24, 0x2, "120", neg_mul_to_i24_cb);
      add_opt(s_mul_i32, v_mad_i32_i24, 0x2, "120", neg_mul_to_i24_cb);
   } else if ((info.opcode == aco_opcode::v_sub_co_u32 ||
               info.opcode == aco_opcode::v_sub_co_u32_e64) &&
              !info.clamp) {
      /* v_sub_co_u32(0, v_cndmask_b32(0, 1, cond)) -> v_cndmask_b32(0, -1, cond) */
      if (ctx.uses[info.defs[1].tempId()] == 0) {
         add_opt(
            v_cndmask_b32, v_cndmask_b32, 0x2, "0312",
            and_cb<and_cb<and_cb<check_const_cb<0, 0>, remove_const_cb<1>>, remove_const_cb<0>>,
                   and_cb<insert_const_cb<1, UINT32_MAX>, pop_def_cb>>);
      }
      /* v_sub_co_u32(a, v_cndmask_b32(0, 1, cond)) -> v_subb_co_u32(a, 0, cond) */
      add_opt(v_cndmask_b32, v_subb_co_u32, 0x2, "0132",
              and_cb<check_const_cb<1, 0>, remove_const_cb<1>>);
      if (ctx.uses[info.defs[1].tempId()] == 0) {
         /* v_sub_co_u32(a, v_cndmask_b32(1, 0, cond)) -> v_addc_co_u32(a, -1, cond) */
         add_opt(
            v_cndmask_b32, v_addc_co_u32, 0x2, "0321",
            and_cb<and_cb<remove_const_cb<1>, remove_const_cb<0>>, insert_const_cb<1, UINT32_MAX>>);
         add_opt(v_lshlrev_b32, v_mad_i32_i24, 0x2, "210",
                 and_cb<and_cb<shift_to_mad_cb<32>, neg_mul_to_i24_cb>, pop_def_cb>);
         add_opt(s_lshl_b32, v_mad_i32_i24, 0x2, "120",
                 and_cb<and_cb<shift_to_mad_cb<32>, neg_mul_to_i24_cb>, pop_def_cb>);
         add_opt(v_mul_u32_u24, v_mad_i32_i24, 0x2, "120", and_cb<neg_mul_to_i24_cb, pop_def_cb>);
         add_opt(s_mul_i32, v_mad_i32_i24, 0x2, "120", and_cb<neg_mul_to_i24_cb, pop_def_cb>);
      }
   } else if ((info.opcode == aco_opcode::s_add_u32 ||
               (info.opcode == aco_opcode::s_add_i32 && !ctx.uses[info.defs[1].tempId()])) &&
              ctx.program->gfx_level >= GFX9) {
      add_opt(s_lshl_b32, s_lshl1_add_u32, 0x3, "102", remove_const_cb<1>);
      add_opt(s_lshl_b32, s_lshl2_add_u32, 0x3, "102", remove_const_cb<2>);
      add_opt(s_lshl_b32, s_lshl3_add_u32, 0x3, "102", remove_const_cb<3>);
      add_opt(s_lshl_b32, s_lshl4_add_u32, 0x3, "102", remove_const_cb<4>);
   } else if (info.opcode == aco_opcode::v_lshlrev_b32 && ctx.program->gfx_level >= GFX9) {
      add_opt(v_add_u32, v_add_lshl_u32, 0x2, "120", nullptr, true);
      add_opt(s_add_u32, v_add_lshl_u32, 0x2, "120", nullptr, true);
      add_opt(s_add_i32, v_add_lshl_u32, 0x2, "120", nullptr, true);
   } else if (info.opcode == aco_opcode::v_and_b32) {
      add_opt(v_not_b32, v_bfi_b32, 0x3, "10", insert_const_cb<1, 0>, true);
      add_opt(s_not_b32, v_bfi_b32, 0x3, "10", insert_const_cb<1, 0>, true);
   } else if (info.opcode == aco_opcode::s_and_b32) {
      add_opt(s_not_b32, s_andn2_b32, 0x3, "01");
   } else if (info.opcode == aco_opcode::s_and_b64) {
      add_opt(s_not_b64, s_andn2_b64, 0x3, "01");
   } else if (info.opcode == aco_opcode::s_or_b32) {
      add_opt(s_not_b32, s_orn2_b32, 0x3, "01");
   } else if (info.opcode == aco_opcode::s_or_b64) {
      add_opt(s_not_b64, s_orn2_b64, 0x3, "01");
   } else if (info.opcode == aco_opcode::s_xor_b32) {
      add_opt(s_not_b32, s_xnor_b32, 0x3, "01");
   } else if (info.opcode == aco_opcode::s_xor_b64) {
      add_opt(s_not_b64, s_xnor_b64, 0x3, "01");
   } else if ((info.opcode == aco_opcode::s_sub_u32 || info.opcode == aco_opcode::s_sub_i32) &&
              !ctx.uses[info.defs[1].tempId()]) {
      add_opt(s_bcnt1_i32_b32, s_bcnt0_i32_b32, 0x2, "10", remove_const_cb<32>);
      add_opt(s_bcnt1_i32_b64, s_bcnt0_i32_b64, 0x2, "10", remove_const_cb<64>);
   } else if (info.opcode == aco_opcode::s_bcnt1_i32_b32) {
      add_opt(s_not_b32, s_bcnt0_i32_b32, 0x1, "0");
   } else if (info.opcode == aco_opcode::s_bcnt1_i32_b64) {
      add_opt(s_not_b64, s_bcnt0_i32_b64, 0x1, "0");
   } else if (info.opcode == aco_opcode::s_ff1_i32_b32 && ctx.program->gfx_level < GFX11) {
      add_opt(s_not_b32, s_ff0_i32_b32, 0x1, "0");
   } else if (info.opcode == aco_opcode::s_ff1_i32_b64 && ctx.program->gfx_level < GFX11) {
      add_opt(s_not_b64, s_ff0_i32_b64, 0x1, "0");
   } else if (info.opcode == aco_opcode::v_cndmask_b32) {
      add_opt(s_not_b64, v_cndmask_b32, 0x4, "102");
      add_opt(s_not_b32, v_cndmask_b32, 0x4, "102");
   }

   if (match_and_apply_patterns(ctx, info, patterns)) {
      for (const alu_opt_op& op_info : info.operands) {
         if (op_info.op.isTemp())
            ctx.uses[op_info.op.tempId()]++;
      }
      for (const Operand& op : instr->operands) {
         if (op.isTemp())
            decrease_and_dce(ctx, op.getTemp());
      }
      ctx.pre_combine_instrs.emplace_back(std::move(instr));
      instr.reset(alu_opt_info_to_instr(ctx, info, nullptr));
      ctx.info[instr->definitions[0].tempId()].set_combined(ctx.pre_combine_instrs.size() - 1);
   }
#undef add_opt
}

struct remat_entry {
   Instruction* instr;
   uint32_t block;
};

inline bool
is_constant(Instruction* instr)
{
   if (instr->opcode != aco_opcode::p_parallelcopy || instr->operands.size() != 1)
      return false;

   return instr->operands[0].isConstant() && instr->definitions[0].isTemp();
}

void
remat_constants_instr(opt_ctx& ctx, aco::map<Temp, remat_entry>& constants, Instruction* instr,
                      uint32_t block_idx)
{
   for (Operand& op : instr->operands) {
      if (!op.isTemp())
         continue;

      auto it = constants.find(op.getTemp());
      if (it == constants.end())
         continue;

      /* Check if we already emitted the same constant in this block. */
      if (it->second.block != block_idx) {
         /* Rematerialize the constant. */
         Builder bld(ctx.program, &ctx.instructions);
         Operand const_op = it->second.instr->operands[0];
         it->second.instr = bld.copy(bld.def(op.regClass()), const_op);
         it->second.block = block_idx;
         ctx.uses.push_back(0);
         ctx.info.push_back(ctx.info[op.tempId()]);
         ctx.info[it->second.instr->definitions[0].tempId()].parent_instr = it->second.instr;
      }

      /* Use the rematerialized constant and update information about latest use. */
      if (op.getTemp() != it->second.instr->definitions[0].getTemp()) {
         ctx.uses[op.tempId()]--;
         op.setTemp(it->second.instr->definitions[0].getTemp());
         ctx.uses[op.tempId()]++;
      }
   }
}

/**
 * This pass implements a simple constant rematerialization.
 * As common subexpression elimination (CSE) might increase the live-ranges
 * of loaded constants over large distances, this pass splits the live-ranges
 * again by re-emitting constants in every basic block.
 */
void
rematerialize_constants(opt_ctx& ctx)
{
   aco::monotonic_buffer_resource memory(1024);
   aco::map<Temp, remat_entry> constants(memory);

   for (Block& block : ctx.program->blocks) {
      if (block.logical_idom == -1)
         continue;

      if (block.logical_idom == (int)block.index)
         constants.clear();

      ctx.instructions.reserve(block.instructions.size());

      for (aco_ptr<Instruction>& instr : block.instructions) {
         if (is_dead(ctx.uses, instr.get()))
            continue;

         if (is_constant(instr.get())) {
            Temp tmp = instr->definitions[0].getTemp();
            constants[tmp] = {instr.get(), block.index};
         } else if (!is_phi(instr)) {
            remat_constants_instr(ctx, constants, instr.get(), block.index);
         }

         ctx.instructions.emplace_back(instr.release());
      }

      block.instructions = std::move(ctx.instructions);
   }
}

bool
to_uniform_bool_instr(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   /* Check every operand to make sure they are suitable. */
   for (Operand& op : instr->operands) {
      if (!op.isTemp())
         return false;
      if (!ctx.info[op.tempId()].is_uniform_bool() && !ctx.info[op.tempId()].is_uniform_bitwise())
         return false;
   }

   switch (instr->opcode) {
   case aco_opcode::s_and_b32:
   case aco_opcode::s_and_b64: instr->opcode = aco_opcode::s_and_b32; break;
   case aco_opcode::s_or_b32:
   case aco_opcode::s_or_b64: instr->opcode = aco_opcode::s_or_b32; break;
   case aco_opcode::s_xor_b32:
   case aco_opcode::s_xor_b64: instr->opcode = aco_opcode::s_absdiff_i32; break;
   case aco_opcode::s_not_b32:
   case aco_opcode::s_not_b64: {
      aco_ptr<Instruction> new_instr{
         create_instruction(aco_opcode::s_absdiff_i32, Format::SOP2, 2, 2)};
      new_instr->operands[0] = instr->operands[0];
      new_instr->operands[1] = Operand::c32(1);
      new_instr->definitions[0] = instr->definitions[0];
      new_instr->definitions[1] = instr->definitions[1];
      new_instr->pass_flags = instr->pass_flags;
      instr = std::move(new_instr);
      ctx.info[instr->definitions[0].tempId()].parent_instr = instr.get();
      ctx.info[instr->definitions[1].tempId()].parent_instr = instr.get();
      break;
   }
   default:
      /* Don't transform other instructions. They are very unlikely to appear here. */
      return false;
   }

   for (Operand& op : instr->operands) {
      if (!op.isTemp())
         continue;

      ctx.uses[op.tempId()]--;

      if (ctx.info[op.tempId()].is_uniform_bool()) {
         /* Just use the uniform boolean temp. */
         op.setTemp(ctx.info[op.tempId()].temp);
      } else if (ctx.info[op.tempId()].is_uniform_bitwise()) {
         /* Use the SCC definition of the predecessor instruction.
          * This allows the predecessor to get picked up by the same optimization (if it has no
          * divergent users), and it also makes sure that the current instruction will keep working
          * even if the predecessor won't be transformed.
          */
         Instruction* pred_instr = ctx.info[op.tempId()].parent_instr;
         assert(pred_instr->definitions.size() >= 2);
         assert(pred_instr->definitions[1].isFixed() &&
                pred_instr->definitions[1].physReg() == scc);
         op.setTemp(pred_instr->definitions[1].getTemp());
      } else {
         UNREACHABLE("Invalid operand on uniform bitwise instruction.");
      }

      ctx.uses[op.tempId()]++;
   }

   instr->definitions[0].setTemp(Temp(instr->definitions[0].tempId(), s1));
   ctx.program->temp_rc[instr->definitions[0].tempId()] = s1;
   assert(!instr->operands[0].isTemp() || instr->operands[0].regClass() == s1);
   assert(!instr->operands[1].isTemp() || instr->operands[1].regClass() == s1);
   return true;
}

void
insert_replacement_instr(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (!instr.get() || instr->definitions.empty() ||
       ctx.info[instr->definitions[0].tempId()].parent_instr == instr.get())
      return;

   while (true) {
      auto it = ctx.replacement_instr.find(instr.get());
      if (it == ctx.replacement_instr.end())
         return;

      instr = std::move(it->second);
      ctx.replacement_instr.erase(it);
   }
}

void
select_instruction(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   const uint32_t threshold = 4;

   if (!instr.get() || is_dead(ctx.uses, instr.get())) {
      instr.reset();
      return;
   }

   if (instr->opcode == aco_opcode::v_med3_f32 || instr->opcode == aco_opcode::v_med3_f16) {
      /* Optimize v_med3 to v_add so that it can be dual issued on GFX11. We start with v_med3 in
       * case omod can be applied.
       */
      unsigned idx;
      if (detect_clamp(instr.get(), &idx)) {
         instr->format = asVOP3(Format::VOP2);
         instr->operands[0] = instr->operands[idx];
         instr->operands[1] = Operand::zero();
         instr->opcode =
            instr->opcode == aco_opcode::v_med3_f32 ? aco_opcode::v_add_f32 : aco_opcode::v_add_f16;
         instr->valu().clamp = true;
         instr->valu().abs = (uint8_t)instr->valu().abs[idx];
         instr->valu().neg = (uint8_t)instr->valu().neg[idx];
         instr->operands.pop_back();
      }
   }

   /* convert split_vector into a copy or extract_vector if only one definition is ever used */
   if (instr->opcode == aco_opcode::p_split_vector) {
      unsigned num_used = 0;
      unsigned idx = 0;
      unsigned split_offset = 0;
      for (unsigned i = 0, offset = 0; i < instr->definitions.size();
           offset += instr->definitions[i++].bytes()) {
         if (ctx.uses[instr->definitions[i].tempId()]) {
            num_used++;
            idx = i;
            split_offset = offset;
         }
      }
      bool done = false;
      Instruction* vec = ctx.info[instr->operands[0].tempId()].parent_instr;
      if (num_used == 1 && vec->opcode == aco_opcode::p_create_vector &&
          ctx.uses[instr->operands[0].tempId()] == 1) {

         unsigned off = 0;
         Operand op;
         for (Operand& vec_op : vec->operands) {
            if (off == split_offset) {
               op = vec_op;
               break;
            }
            off += vec_op.bytes();
         }
         if (off != instr->operands[0].bytes() && op.bytes() == instr->definitions[idx].bytes()) {
            ctx.uses[instr->operands[0].tempId()]--;
            for (Operand& vec_op : vec->operands) {
               if (vec_op.isTemp())
                  ctx.uses[vec_op.tempId()]--;
            }
            if (op.isTemp())
               ctx.uses[op.tempId()]++;

            aco_ptr<Instruction> copy{
               create_instruction(aco_opcode::p_parallelcopy, Format::PSEUDO, 1, 1)};
            copy->operands[0] = op;
            copy->definitions[0] = instr->definitions[idx];
            instr = std::move(copy);
            ctx.info[instr->definitions[0].tempId()].parent_instr = instr.get();

            done = true;
         }
      }

      if (!done && num_used == 1 &&
          instr->operands[0].bytes() % instr->definitions[idx].bytes() == 0 &&
          split_offset % instr->definitions[idx].bytes() == 0) {
         aco_ptr<Instruction> extract{
            create_instruction(aco_opcode::p_extract_vector, Format::PSEUDO, 2, 1)};
         extract->operands[0] = instr->operands[0];
         extract->operands[1] =
            Operand::c32((uint32_t)split_offset / instr->definitions[idx].bytes());
         extract->definitions[0] = instr->definitions[idx];
         instr = std::move(extract);
         ctx.info[instr->definitions[0].tempId()].parent_instr = instr.get();
      }
   }

   if (!instr->definitions.empty() && ctx.info[instr->definitions[0].tempId()].is_combined()) {
      aco_ptr<Instruction>& prev_instr =
         ctx.pre_combine_instrs[ctx.info[instr->definitions[0].tempId()].val];
      /* Re-check combined instructions, revert to using pre combine instruction if
       * no operand instruction was eliminated.
       */
      bool use_prev = std::all_of(
         prev_instr->operands.begin(), prev_instr->operands.end(),
         [&](Operand op)
         {
            return !op.isTemp() || (ctx.info[op.tempId()].parent_instr &&
                                    !is_dead(ctx.uses, ctx.info[op.tempId()].parent_instr));
         });

      if (use_prev) {
         for (const Operand& op : prev_instr->operands) {
            if (op.isTemp())
               ctx.uses[op.tempId()]++;
         }
         for (const Operand& op : instr->operands) {
            if (op.isTemp())
               decrease_and_dce(ctx, op.getTemp());
         }

         instr = std::move(prev_instr);
         for (Definition& def : instr->definitions)
            ctx.info[def.tempId()].parent_instr = instr.get();
      }
   }

   /* Mark SCC needed, so the uniform boolean transformation won't swap the definitions
    * when it isn't beneficial */
   if (instr->isBranch() && instr->operands.size() && instr->operands[0].isTemp() &&
       instr->operands[0].isFixed() && instr->operands[0].physReg() == scc) {
      ctx.info[instr->operands[0].tempId()].set_scc_needed();
      return;
   } else if ((instr->opcode == aco_opcode::s_cselect_b64 ||
               instr->opcode == aco_opcode::s_cselect_b32) &&
              instr->operands[2].isTemp()) {
      ctx.info[instr->operands[2].tempId()].set_scc_needed();
   }

   /* check for literals */
   if (!instr->isSALU() && !instr->isVALU())
      return;

   /* Transform uniform bitwise boolean operations to 32-bit when there are no divergent uses. */
   if (instr->definitions.size() && ctx.uses[instr->definitions[0].tempId()] == 0 &&
       ctx.info[instr->definitions[0].tempId()].is_uniform_bitwise()) {
      bool transform_done = to_uniform_bool_instr(ctx, instr);

      if (transform_done && !ctx.info[instr->definitions[1].tempId()].is_scc_needed()) {
         /* Swap the two definition IDs in order to avoid overusing the SCC.
          * This reduces extra moves generated by RA. */
         uint32_t def0_id = instr->definitions[0].getTemp().id();
         uint32_t def1_id = instr->definitions[1].getTemp().id();
         instr->definitions[0].setTemp(Temp(def1_id, s1));
         instr->definitions[1].setTemp(Temp(def0_id, s1));
      }

      return;
   }

   /* This optimization is done late in order to be able to apply otherwise
    * unsafe optimizations such as the inverse comparison optimization.
    */
   if (instr->opcode == aco_opcode::s_and_b32 || instr->opcode == aco_opcode::s_and_b64) {
      if (instr->operands[0].isTemp() && fixed_to_exec(instr->operands[1]) &&
          ctx.uses[instr->operands[0].tempId()] == 1 &&
          ctx.uses[instr->definitions[1].tempId()] == 0 &&
          can_eliminate_and_exec(ctx, instr->operands[0].getTemp(), instr->pass_flags, true)) {
         ctx.uses[instr->operands[0].tempId()]--;
         Instruction* op_instr = ctx.info[instr->operands[0].tempId()].parent_instr;

         if (op_instr->opcode == aco_opcode::s_cselect_b32 ||
             op_instr->opcode == aco_opcode::s_cselect_b64) {
            for (unsigned i = 0; i < 2; i++) {
               if (op_instr->operands[i].constantEquals(-1))
                  op_instr->operands[i] = instr->operands[1];
            }
            ctx.info[op_instr->definitions[0].tempId()].label &= label_uniform_bool;
         }

         op_instr->definitions[0].setTemp(instr->definitions[0].getTemp());
         ctx.info[op_instr->definitions[0].tempId()].parent_instr = op_instr;
         instr.reset();
         return;
      }
   }

   /* Combine DPP copies into VALU. This should be done after creating MAD/FMA. */
   if (instr->isVALU() && !instr->isDPP()) {
      for (unsigned i = 0; i < instr->operands.size(); i++) {
         if (!instr->operands[i].isTemp())
            continue;
         ssa_info info = ctx.info[instr->operands[i].tempId()];

         if (!info.parent_instr->isDPP() || info.parent_instr->opcode != aco_opcode::v_mov_b32 ||
             info.parent_instr->pass_flags != instr->pass_flags)
            continue;

         /* We won't eliminate the DPP mov if the operand is used twice */
         bool op_used_twice = false;
         for (unsigned j = 0; j < instr->operands.size(); j++)
            op_used_twice |= i != j && instr->operands[i] == instr->operands[j];
         if (op_used_twice)
            continue;

         if (i != 0) {
            if (!can_swap_operands(instr, &instr->opcode, 0, i))
               continue;
            instr->valu().swapOperands(0, i);
         }

         bool dpp8 = info.parent_instr->isDPP8();
         if (!can_use_DPP(ctx.program->gfx_level, instr, dpp8))
            continue;

         bool input_mods = can_use_input_modifiers(ctx.program->gfx_level, instr->opcode, 0) &&
                           get_operand_type(instr, 0).bit_size == 32;
         bool mov_uses_mods = info.parent_instr->valu().neg[0] || info.parent_instr->valu().abs[0];
         if (((dpp8 && ctx.program->gfx_level < GFX11) || !input_mods) && mov_uses_mods)
            continue;

         convert_to_DPP(ctx.program->gfx_level, instr, dpp8);

         if (dpp8) {
            DPP8_instruction* dpp = &instr->dpp8();
            dpp->lane_sel = info.parent_instr->dpp8().lane_sel;
            dpp->fetch_inactive = info.parent_instr->dpp8().fetch_inactive;
            if (mov_uses_mods)
               instr->format = asVOP3(instr->format);
         } else {
            DPP16_instruction* dpp = &instr->dpp16();
            /* anything else doesn't make sense in SSA */
            assert(info.parent_instr->dpp16().row_mask == 0xf &&
                   info.parent_instr->dpp16().bank_mask == 0xf);
            dpp->dpp_ctrl = info.parent_instr->dpp16().dpp_ctrl;
            dpp->bound_ctrl = info.parent_instr->dpp16().bound_ctrl;
            dpp->fetch_inactive = info.parent_instr->dpp16().fetch_inactive;
         }

         instr->valu().neg[0] ^= info.parent_instr->valu().neg[0] && !instr->valu().abs[0];
         instr->valu().abs[0] |= info.parent_instr->valu().abs[0];

         if (--ctx.uses[info.parent_instr->definitions[0].tempId()])
            ctx.uses[info.parent_instr->operands[0].tempId()]++;
         instr->operands[0].setTemp(info.parent_instr->operands[0].getTemp());
         for (const Definition& def : instr->definitions)
            ctx.info[def.tempId()].parent_instr = instr.get();
         break;
      }
   }

   /* Use v_fma_mix for f2f32/f2f16 if it has higher throughput.
    * Do this late to not disturb other optimizations.
    */
   if ((instr->opcode == aco_opcode::v_cvt_f32_f16 || instr->opcode == aco_opcode::v_cvt_f16_f32) &&
       ctx.program->gfx_level >= GFX11 && ctx.program->wave_size == 64 && !instr->valu().omod &&
       !instr->isDPP()) {
      bool is_f2f16 = instr->opcode == aco_opcode::v_cvt_f16_f32;
      Instruction* fma = create_instruction(
         is_f2f16 ? aco_opcode::v_fma_mixlo_f16 : aco_opcode::v_fma_mix_f32, Format::VOP3P, 3, 1);
      fma->definitions[0] = instr->definitions[0];
      fma->operands[0] = instr->operands[0];
      fma->valu().opsel_hi[0] = !is_f2f16;
      fma->valu().opsel_lo[0] = instr->valu().opsel[0];
      fma->valu().clamp = instr->valu().clamp;
      fma->valu().abs[0] = instr->valu().abs[0];
      fma->valu().neg[0] = instr->valu().neg[0];
      fma->operands[1] = Operand::c32(fui(1.0f));
      fma->operands[2] = Operand::zero();
      fma->valu().neg[2] = true;
      instr.reset(fma);
      ctx.info[instr->definitions[0].tempId()].label = 0;
      ctx.info[instr->definitions[0].tempId()].parent_instr = instr.get();
   }

   /* Check operands for whether we can apply constants or literals. */
   if (std::none_of(instr->operands.begin(), instr->operands.end(),
                    [&](const Operand& op)
                    {
                       if (!op.isTemp() || op.isFixed())
                          return false;
                       auto& temp_info = ctx.info[op.tempId()];
                       return temp_info.is_constant();
                    }))
      return;

   alu_opt_info input_info;
   if (!alu_opt_gather_info(ctx, instr.get(), input_info))
      return;

   unsigned literal_mask = 0;
   for (unsigned i = 0; i < input_info.operands.size(); i++) {
      Operand op = input_info.operands[i].op;
      if (!op.isTemp() || op.isFixed())
         continue;
      auto& temp_info = ctx.info[op.tempId()];
      if (temp_info.is_constant())
         literal_mask |= BITFIELD_BIT(i);
   }

   alu_opt_info lit_info;
   bool force_create = false;
   unsigned lit_uses = threshold;
   for (unsigned sub_mask = (~literal_mask + 1) & literal_mask; sub_mask;
        sub_mask = ((sub_mask | ~literal_mask) + 1) & literal_mask) {
      alu_opt_info candidate = input_info;
      unsigned candidate_uses = UINT32_MAX;
      u_foreach_bit (i, sub_mask) {
         uint32_t tmpid = candidate.operands[i].op.tempId();
         candidate.operands[i].op = Operand::literal32(ctx.info[tmpid].val);
         candidate_uses = MIN2(candidate_uses, ctx.uses[tmpid]);
      }
      if (!alu_opt_info_is_valid(ctx, candidate))
         continue;

      switch (candidate.opcode) {
      case aco_opcode::v_fmaak_f32:
      case aco_opcode::v_fmaak_f16:
      case aco_opcode::v_madak_f32:
      case aco_opcode::v_madak_f16:
         /* This instruction won't be able to use fmac, so fmaak doesn't regress code size. */
         force_create = true;
         break;
      default: break;
      }

      if (!force_create && util_bitcount(sub_mask) <= 1 && candidate_uses >= lit_uses)
         continue;
      lit_info = candidate;
      lit_uses = candidate_uses;

      if (util_bitcount(sub_mask) > 1) {
         force_create = true;
         break;
      }
   }
   if (!lit_info.operands.size())
      return;

   for (const auto& op_info : lit_info.operands) {
      if (op_info.op.isTemp())
         ctx.uses[op_info.op.tempId()]++;
   }
   for (Operand op : instr->operands) {
      if (op.isTemp())
         decrease_and_dce(ctx, op.getTemp());
   }
   if (force_create || lit_uses == 1)
      instr.reset(alu_opt_info_to_instr(ctx, lit_info, instr.release()));
}

static aco_opcode
sopk_opcode_for_sopc(aco_opcode opcode)
{
#define CTOK(op)                                                                                   \
   case aco_opcode::s_cmp_##op##_i32: return aco_opcode::s_cmpk_##op##_i32;                        \
   case aco_opcode::s_cmp_##op##_u32: return aco_opcode::s_cmpk_##op##_u32;
   switch (opcode) {
      CTOK(eq)
      CTOK(lg)
      CTOK(gt)
      CTOK(ge)
      CTOK(lt)
      CTOK(le)
   default: return aco_opcode::num_opcodes;
   }
#undef CTOK
}

static bool
sopc_is_signed(aco_opcode opcode)
{
#define SOPC(op)                                                                                   \
   case aco_opcode::s_cmp_##op##_i32: return true;                                                 \
   case aco_opcode::s_cmp_##op##_u32: return false;
   switch (opcode) {
      SOPC(eq)
      SOPC(lg)
      SOPC(gt)
      SOPC(ge)
      SOPC(lt)
      SOPC(le)
   default: UNREACHABLE("Not a valid SOPC instruction.");
   }
#undef SOPC
}

static aco_opcode
sopc_32_swapped(aco_opcode opcode)
{
#define SOPC(op1, op2)                                                                             \
   case aco_opcode::s_cmp_##op1##_i32: return aco_opcode::s_cmp_##op2##_i32;                       \
   case aco_opcode::s_cmp_##op1##_u32: return aco_opcode::s_cmp_##op2##_u32;
   switch (opcode) {
      SOPC(eq, eq)
      SOPC(lg, lg)
      SOPC(gt, lt)
      SOPC(ge, le)
      SOPC(lt, gt)
      SOPC(le, ge)
   default: return aco_opcode::num_opcodes;
   }
#undef SOPC
}

static void
try_convert_sopc_to_sopk(aco_ptr<Instruction>& instr)
{
   if (sopk_opcode_for_sopc(instr->opcode) == aco_opcode::num_opcodes)
      return;

   if (instr->operands[0].isLiteral()) {
      std::swap(instr->operands[0], instr->operands[1]);
      instr->opcode = sopc_32_swapped(instr->opcode);
   }

   if (!instr->operands[1].isLiteral())
      return;

   if (instr->operands[0].isFixed() && instr->operands[0].physReg() >= 128)
      return;

   uint32_t value = instr->operands[1].constantValue();

   const uint32_t i16_mask = 0xffff8000u;

   bool value_is_i16 = (value & i16_mask) == 0 || (value & i16_mask) == i16_mask;
   bool value_is_u16 = !(value & 0xffff0000u);

   if (!value_is_i16 && !value_is_u16)
      return;

   if (!value_is_i16 && sopc_is_signed(instr->opcode)) {
      if (instr->opcode == aco_opcode::s_cmp_lg_i32)
         instr->opcode = aco_opcode::s_cmp_lg_u32;
      else if (instr->opcode == aco_opcode::s_cmp_eq_i32)
         instr->opcode = aco_opcode::s_cmp_eq_u32;
      else
         return;
   } else if (!value_is_u16 && !sopc_is_signed(instr->opcode)) {
      if (instr->opcode == aco_opcode::s_cmp_lg_u32)
         instr->opcode = aco_opcode::s_cmp_lg_i32;
      else if (instr->opcode == aco_opcode::s_cmp_eq_u32)
         instr->opcode = aco_opcode::s_cmp_eq_i32;
      else
         return;
   }

   instr->format = Format::SOPK;
   SALU_instruction* instr_sopk = &instr->salu();

   instr_sopk->imm = instr_sopk->operands[1].constantValue() & 0xffff;
   instr_sopk->opcode = sopk_opcode_for_sopc(instr_sopk->opcode);
   instr_sopk->operands.pop_back();
}

static void
opt_fma_mix_acc(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   /* fma_mix is only dual issued on gfx11 if dst and acc type match */
   bool f2f16 = instr->opcode == aco_opcode::v_fma_mixlo_f16;

   if (instr->valu().opsel_hi[2] == f2f16 || instr->isDPP())
      return;

   bool is_add = false;
   for (unsigned i = 0; i < 2; i++) {
      uint32_t one = instr->valu().opsel_hi[i] ? 0x3800 : 0x3f800000;
      is_add = instr->operands[i].constantEquals(one) && !instr->valu().neg[i] &&
               !instr->valu().opsel_lo[i];
      if (is_add) {
         instr->valu().swapOperands(0, i);
         break;
      }
   }

   if (is_add && instr->valu().opsel_hi[1] == f2f16) {
      instr->valu().swapOperands(1, 2);
      return;
   }

   unsigned literal_count = instr->operands[0].isLiteral() + instr->operands[1].isLiteral() +
                            instr->operands[2].isLiteral();

   if (!f2f16 || literal_count > 1)
      return;

   /* try to convert constant operand to fp16 */
   for (unsigned i = 2 - is_add; i < 3; i++) {
      if (!instr->operands[i].isConstant())
         continue;

      float value = uif(instr->operands[i].constantValue());
      uint16_t fp16_val = _mesa_float_to_half(value);
      bool is_denorm = (fp16_val & 0x7fff) != 0 && (fp16_val & 0x7fff) <= 0x3ff;

      if (_mesa_half_to_float(fp16_val) != value ||
          (is_denorm && !(ctx.fp_mode.denorm16_64 & fp_denorm_keep_in)))
         continue;

      instr->valu().swapOperands(i, 2);

      Operand op16 = Operand::c16(fp16_val);
      assert(!op16.isLiteral() || instr->operands[2].isLiteral());

      instr->operands[2] = op16;
      instr->valu().opsel_lo[2] = false;
      instr->valu().opsel_hi[2] = true;
      return;
   }
}

void
opt_neg_abs_fp64(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   if (instr->valu().omod || instr->valu().clamp)
      return;

   /* Lower fp64 neg/abs to bitwise instructions if possible. */
   for (unsigned i = 0; i < 2; i++) {
      if (!instr->operands[i].isConstant() ||
          fabs(uid(instr->operands[i].constantValue64())) != 1.0 || !instr->operands[!i].isTemp() ||
          (!ctx.info[instr->operands[!i].tempId()].is_canonicalized(64) &&
           ctx.fp_mode.denorm16_64 != fp_denorm_keep))
         continue;
      bool neg = uid(instr->operands[i].constantValue64()) == -1.0 && !instr->valu().abs[i];
      neg ^= instr->valu().neg[0] != instr->valu().neg[1];
      bool abs = instr->valu().abs[!i];

      static_assert(sizeof(Pseudo_instruction) <= sizeof(VALU_instruction));
      instr->format = Format::PSEUDO;

      if (!neg && !abs) {
         instr->opcode = aco_opcode::p_parallelcopy;
         instr->operands[0] = instr->operands[!i];
         instr->operands.pop_back();
         return;
      }

      Builder bld(ctx.program, &ctx.instructions);

      RegClass rc = RegClass::get(instr->operands[!i].regClass().type(), 4);

      Instruction* split = bld.pseudo(aco_opcode::p_split_vector, bld.def(rc), bld.def(rc),
                                      instr->operands[!i].getTemp());

      Instruction* bit_instr;
      uint32_t constant = neg ? 0x80000000 : 0x7fffffff;
      if (rc == s1) {
         aco_opcode opcode =
            neg ? (abs ? aco_opcode::s_or_b32 : aco_opcode::s_xor_b32) : aco_opcode::s_and_b32;
         bit_instr = bld.sop2(opcode, bld.def(s1), bld.def(s1, scc), Operand::c32(constant),
                              split->definitions[1].getTemp());
      } else {
         assert(rc == v1);
         aco_opcode opcode =
            neg ? (abs ? aco_opcode::v_or_b32 : aco_opcode::v_xor_b32) : aco_opcode::v_and_b32;
         bit_instr =
            bld.vop2(opcode, bld.def(v1), Operand::c32(constant), split->definitions[1].getTemp());
      }

      instr->opcode = aco_opcode::p_create_vector;
      instr->operands[0] = Operand(split->definitions[0].getTemp());
      instr->operands[1] = Operand(bit_instr->definitions[0].getTemp());

      ctx.uses.resize(ctx.program->peekAllocationId());
      ctx.info.resize(ctx.program->peekAllocationId());
      for (Definition def : split->definitions) {
         ctx.uses[def.tempId()] = 1;
         ctx.info[def.tempId()].parent_instr = split;
      }
      for (unsigned j = 0; j < bit_instr->definitions.size(); j++) {
         Definition def = bit_instr->definitions[j];
         ctx.uses[def.tempId()] = j == 0;
         ctx.info[def.tempId()].parent_instr = bit_instr;
      }
      return;
   }
}

void
apply_literals(opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   /* Cleanup Dead Instructions */
   if (!instr)
      return;

   /* apply literals on SALU/VALU */
   if (instr->isSALU() || instr->isVALU()) {
      for (const Operand& op : instr->operands) {
         if (op.isTemp() && ctx.info[op.tempId()].is_constant() && ctx.uses[op.tempId()] == 0) {
            alu_opt_info info;
            if (!alu_opt_gather_info(ctx, instr.get(), info))
               UNREACHABLE("We already check that we can apply lit");

            for (auto& op_info : info.operands) {
               if (op_info.op == op)
                  op_info.op = Operand::literal32(ctx.info[op.tempId()].val);
            }

            if (!alu_opt_info_is_valid(ctx, info))
               UNREACHABLE("We already check that we can apply lit");
            instr.reset(alu_opt_info_to_instr(ctx, info, instr.release()));
            break;
         }
      }
   }

   if (instr->isSOPC() && ctx.program->gfx_level < GFX12)
      try_convert_sopc_to_sopk(instr);

   if (instr->opcode == aco_opcode::v_fma_mixlo_f16 || instr->opcode == aco_opcode::v_fma_mix_f32)
      opt_fma_mix_acc(ctx, instr);

   if (instr->opcode == aco_opcode::v_mul_f64 || instr->opcode == aco_opcode::v_mul_f64_e64)
      opt_neg_abs_fp64(ctx, instr);

   ctx.instructions.emplace_back(std::move(instr));
}

void
validate_opt_ctx(opt_ctx& ctx)
{
   if (!(debug_flags & DEBUG_VALIDATE_OPT))
      return;

   Program* program = ctx.program;

   bool is_valid = true;
   auto check = [&program, &is_valid](bool success, const char* msg,
                                      aco::Instruction* instr) -> void
   {
      if (!success) {
         char* out;
         size_t outsize;
         struct u_memstream mem;
         u_memstream_open(&mem, &out, &outsize);
         FILE* const memf = u_memstream_get(&mem);

         fprintf(memf, "Optimizer: %s: ", msg);
         aco_print_instr(program->gfx_level, instr, memf);
         u_memstream_close(&mem);

         aco_err(program, "%s", out);
         free(out);

         is_valid = false;
      }
   };

   for (Block& block : program->blocks) {
      for (aco_ptr<Instruction>& instr : block.instructions) {
         if (!instr)
            continue;
         for (const Definition& def : instr->definitions) {
            check(ctx.info[def.tempId()].parent_instr == instr.get(), "parent_instr incorrect",
                  instr.get());
         }
      }
   }
   if (!is_valid) {
      abort();
   }
}

void rename_loop_header_phis(opt_ctx& ctx) {
   for (Block& block : ctx.program->blocks) {
      if (!(block.kind & block_kind_loop_header))
         continue;

      for (auto& instr : block.instructions) {
         if (!is_phi(instr))
            break;

         for (unsigned i = 0; i < instr->operands.size(); i++) {
            if (!instr->operands[i].isTemp())
               continue;

            ssa_info info = ctx.info[instr->operands[i].tempId()];
            while (info.is_temp()) {
               pseudo_propagate_temp(ctx, instr, info.temp, i);
               info = ctx.info[info.temp.id()];
            }
         }
      }
   }
}

} /* end namespace */

void
optimize(Program* program)
{
   opt_ctx ctx;
   ctx.program = program;
   ctx.info = std::vector<ssa_info>(program->peekAllocationId());

   /* 1. Bottom-Up DAG pass (forward) to label all ssa-defs */
   for (Block& block : program->blocks) {
      ctx.fp_mode = block.fp_mode;
      for (aco_ptr<Instruction>& instr : block.instructions)
         label_instruction(ctx, instr);
   }

   validate_opt_ctx(ctx);

   rename_loop_header_phis(ctx);

   validate_opt_ctx(ctx);

   ctx.uses = dead_code_analysis(program);

   /* 2. Rematerialize constants in every block. */
   rematerialize_constants(ctx);

   validate_opt_ctx(ctx);

   /* 3. Combine v_mad, omod, clamp and propagate sgpr on VALU instructions */
   for (Block& block : program->blocks) {
      ctx.fp_mode = block.fp_mode;
      for (aco_ptr<Instruction>& instr : block.instructions)
         combine_instruction(ctx, instr);
   }

   if (!ctx.replacement_instr.empty()) {
      for (Block& block : program->blocks) {
         ctx.fp_mode = block.fp_mode;
         for (aco_ptr<Instruction>& instr : block.instructions)
            insert_replacement_instr(ctx, instr);
      }
   }

   validate_opt_ctx(ctx);

   /* 4. Top-Down DAG pass (backward) to select instructions (includes DCE) */
   for (auto block_rit = program->blocks.rbegin(); block_rit != program->blocks.rend();
        ++block_rit) {
      Block* block = &(*block_rit);
      ctx.fp_mode = block->fp_mode;
      for (auto instr_rit = block->instructions.rbegin(); instr_rit != block->instructions.rend();
           ++instr_rit)
         select_instruction(ctx, *instr_rit);
   }

   validate_opt_ctx(ctx);

   /* 5. Add literals to instructions */
   for (Block& block : program->blocks) {
      ctx.instructions.reserve(block.instructions.size());
      ctx.fp_mode = block.fp_mode;
      for (aco_ptr<Instruction>& instr : block.instructions)
         apply_literals(ctx, instr);
      block.instructions = std::move(ctx.instructions);
   }

   validate_opt_ctx(ctx);
}

} // namespace aco
