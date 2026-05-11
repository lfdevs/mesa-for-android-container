/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include "compiler/brw/brw_disasm_info.h"
#include "compiler/brw/brw_eu.h"
#include "compiler/brw/brw_eu_defines.h"
#include "compiler/brw/brw_eu_inst.h"
#include "compiler/brw/brw_reg.h"
#include "compiler/brw/brw_reg_type.h"
#include "dev/intel_debug.h"
#include "util/macros.h"
#include "util/u_dynarray.h"
#include "util/u_math.h"
#include "jay.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

static inline enum brw_reg_type
to_brw_reg_type(enum jay_type type)
{
   /* clang-format off */
   switch (type) {
   case JAY_TYPE_UNTYPED:
   case JAY_TYPE_U8:   return BRW_TYPE_UB;
   case JAY_TYPE_U16:  return BRW_TYPE_UW;
   case JAY_TYPE_U32:  return BRW_TYPE_UD;
   case JAY_TYPE_U64:  return BRW_TYPE_UQ;
   case JAY_TYPE_S8:   return BRW_TYPE_B;
   case JAY_TYPE_S16:  return BRW_TYPE_W;
   case JAY_TYPE_S32:  return BRW_TYPE_D;
   case JAY_TYPE_S64:  return BRW_TYPE_Q;
   case JAY_TYPE_F16:  return BRW_TYPE_HF;
   case JAY_TYPE_F32:  return BRW_TYPE_F;
   case JAY_TYPE_F64:  return BRW_TYPE_DF;
   case JAY_TYPE_BF16: return BRW_TYPE_BF;
   default: UNREACHABLE("invalid type");
   }
   /* clang-format on */
}

static inline unsigned
to_def_grf_16(struct jay_partition *p, jay_def d)
{
   unsigned count = jay_num_values(d);
   if (count == 0 || !(d.file == GPR || d.file == UGPR)) {
      return d.reg;
   }

   unsigned base = 0;
   for (unsigned i = 0; i < JAY_PARTITION_BLOCKS; ++i) {
      unsigned offset = d.reg - base;

      if (offset < p->blocks[d.file][i].len) {
         assert(offset + count <= p->blocks[d.file][i].len &&
                "vectors must not cross partition boundaries");

         return (p->blocks[d.file][i].start + offset) * 2 + d.hi;
      }

      base += p->blocks[d.file][i].len;
   }

   UNREACHABLE("virtual register must be in a block");
}

static inline brw_reg
to_brw_reg(jay_function *f,
           const jay_inst *I,
           signed idx,
           unsigned simd_offs,
           bool force_hi)
{
   bool is_dest = idx < 0;
   enum jay_type type = is_dest ? I->type : jay_src_type(I, idx);
   jay_def d = is_dest ? I->dst : I->src[idx];
   d.hi |= force_hi;

   struct brw_reg R;
   unsigned reg = to_def_grf_16(&f->shader->partition, d), offset_B = 0;

   if (jay_is_imm(d)) {
      /* Immediates have size restrictions but can zero extend */
      if (jay_type_size_bits(type) == 64) {
         type = jay_type_resize(type, 32);
      } else if (I->op == JAY_OPCODE_BFN) {
         assert(jay_as_uint(d) < UINT16_MAX);
         type = JAY_TYPE_U16;
      }

      R = brw_imm_ud(jay_as_uint(d));
   } else if (jay_is_null(d)) {
      R = brw_null_reg();
   } else if (d.file == UGPR || d.file == UACCUM) {
      unsigned phys_reg = (reg >> 1) / 8;
      offset_B = ((reg >> 1) % 8) * 4;

      if (d.file == UGPR) {
         R = brw_ud1_grf(phys_reg, 0);
      } else {
         R = brw_ud1_reg(ARF, BRW_ARF_ACCUMULATOR + (phys_reg * 2), 0);
      }

      /* Handle 3-src restrictions and vectorized uniform code. */
      if (is_dest || jay_num_values(d) >= 8) {
         R = vec8(R);
      }

      /* Some operations have special restrictions on the destination stride,
       * but if we write a single UGPR the stride is ignored..  Specify
       * whatever stride is needed to satisfy the rules.
       */
      if (is_dest) {
         /* BSpec 56640 "Special Restrictions" says:
          *
          *    "Conversion between HF and Integer must be DWord-aligned
          *     and strided by a DWord on the destination."
          */
         enum jay_type src0_type = jay_src_type(I, 0);
         if ((I->type == JAY_TYPE_F16 && !jay_type_is_any_float(src0_type)) ||
             (src0_type == JAY_TYPE_F16 && !jay_type_is_any_float(I->type))) {
            assert(jay_num_values(d) == 1 && "must not vectorize HF<->Int");
            R = stride(R, 8, 2, 4);
         }

         /* Packed floats have restrictions on mixed sizes.  Use <2>. */
         if (jay_type_size_bits(I->type) == 16 &&
             jay_type_size_bits(jay_src_type(I, 0)) != 16) {
            assert(jay_num_values(d) == 1 && "must not vectorize mixed float");
            R = stride(R, 4, 2, 2);
         }
      }
   } else if (d.file == GPR || d.file == ACCUM) {
      enum jay_stride def_stride =
         d.file == GPR ? jay_def_stride(f->shader, d) : JAY_STRIDE_4;
      uint32_t type_bits = jay_type_size_bits(type);
      unsigned stride_bits = jay_stride_to_bits(def_stride);
      unsigned simd_width = jay_simd_width_physical(f->shader, I);

      unsigned phys_reg;
      if (def_stride == JAY_STRIDE_2) {
         /* Bit 0 selects between lo/hi halves of the GPR */
         phys_reg = (reg / 2) * jay_grf_per_gpr(f->shader);
         offset_B = (reg & 1) * 2 * f->shader->dispatch_width;
      } else {
         /* Low bits are an offset in 2-byte words into the GRF */
         unsigned mask = BITFIELD_MASK(stride_bits / 32);
         phys_reg = ((reg & ~mask) / 2) * jay_grf_per_gpr(f->shader);
         offset_B = (reg & mask) * 2;
      }

      if (d.file == GPR) {
         R = xe2_vec8_grf(phys_reg, 0);
      } else {
         R = brw_vecn_reg(8, ARF, BRW_ARF_ACCUMULATOR + (phys_reg * 2), 0);
      }

      R = byte_offset(R, simd_offs * simd_width * stride_bits / 8);

      if (stride_bits == (type_bits * 4)) {
         R = stride(R, 8, 2, 4);
      } else if (stride_bits == (type_bits * 2)) {
         R = stride(R, 4, 2, 2);
      } else {
         assert(stride_bits == type_bits);
      }

      /* Broadcast is equivalent to <8, 8, 1> for SIMD1 instructions. Use that
       * instead due to regioning restrictions.
       */
      if (simd_width == 1) {
         R = vec1(R);
      }
   } else if (jay_is_flag(d)) {
      /* Explicit flags act like UGPRs. As sources they broadcast to all lanes,
       * so we may ignore the SIMD offset. As destinations, they are written by
       * SIMD1 instructions and are never SIMD split.
       */
      assert(simd_offs == 0 || idx >= 0);
      unsigned offs_B = d.reg * (f->shader->dispatch_width / 8);
      R = brw_flag_subreg(offs_B / 2);
   } else if (d.file == J_ADDRESS) {
      R = brw_address_reg(d.reg);
   } else if (d.file == J_ARF) {
      R = brw_ud1_reg(ARF, jay_base_index(d), 0);
   } else {
      UNREACHABLE("unexpected file");
   }

   R.negate = d.negate;
   R.abs = d.abs;
   return byte_offset(retype(R, to_brw_reg_type(type)), offset_B);
}

#define SRC(i) to_brw_reg(f, I, i, simd_offs, false)

#define OP0(hw)                                                                \
   case JAY_OPCODE_##hw:                                                       \
      brw_##hw(p);                                                             \
      break;

#define OP1(jay, hw)                                                           \
   case JAY_OPCODE_##jay:                                                      \
      brw_alu1(p, BRW_OPCODE_##hw, dst, SRC(0));                               \
      break;

#define OP2(jay, hw)                                                           \
   case JAY_OPCODE_##jay:                                                      \
      brw_alu2(p, BRW_OPCODE_##hw, dst, SRC(0), SRC(1));                       \
      break;

#define OP3(jay, hw)                                                           \
   case JAY_OPCODE_##jay:                                                      \
      brw_alu3(p, BRW_OPCODE_##hw, dst, SRC(0), SRC(1), SRC(2));               \
      break;

#define OP3_SWAP(jay, hw)                                                      \
   case JAY_OPCODE_##jay:                                                      \
      brw_alu3(p, BRW_OPCODE_##hw, dst, SRC(2), SRC(1), SRC(0));               \
      break;

static struct brw_reg
quad_swizzle(struct brw_reg r, const jay_inst *I)
{
   /* clang-format off */
   switch (jay_quad_swizzle_swizzle(I)) {
   case JAY_QUAD_SWIZZLE_XXXX: return suboffset(stride(r, 4, 4, 0), 0);
   case JAY_QUAD_SWIZZLE_YYYY: return suboffset(stride(r, 4, 4, 0), 1);
   case JAY_QUAD_SWIZZLE_ZZZZ: return suboffset(stride(r, 4, 4, 0), 2);
   case JAY_QUAD_SWIZZLE_WWWW: return suboffset(stride(r, 4, 4, 0), 3);
   case JAY_QUAD_SWIZZLE_XXZZ: return suboffset(stride(r, 2, 2, 0), 0);
   case JAY_QUAD_SWIZZLE_YYWW: return suboffset(stride(r, 2, 2, 0), 1);
   case JAY_QUAD_SWIZZLE_XYXY: return suboffset(stride(r, 0, 2, 1), 0);
   case JAY_QUAD_SWIZZLE_ZWZW: return suboffset(stride(r, 0, 2, 1), 2);
   }
   /* clang-format on */

   UNREACHABLE("invalid quad swizzle");
}

/* Runs once per SIMD-split, so must not modify the instruction! */
static void
emit(struct brw_codegen *p,
     jay_function *f,
     const jay_inst *I,
     unsigned simd_offs)
{
   ASSERTED unsigned nr_ins_before = p->nr_insn;
   unsigned exec_size = jay_simd_width_physical(f->shader, I);
   // jay_print_inst(stdout, (jay_inst *) I);

   /* Replicate the SWSB regdist for SIMD split instructions if needed */
   struct tgl_swsb dep =
      simd_offs && !I->replicate_dep ? tgl_swsb_null() : I->dep;

   /* We do not allow SBID dependencies on SIMD split instructions since
    * individual groups could get shot down. This would require more tracking
    * and is unclear whether it's beneficial.
    */
   assert(simd_offs == 0 || I->dep.mode == TGL_SBID_NULL);

   if (I->decrement_dep) {
      unsigned delta = simd_offs * jay_macro_length(I);
      assert(dep.regdist > delta);
      dep.regdist -= delta;
   }

   brw_set_default_exec_size(p, util_logbase2(exec_size));
   brw_set_default_mask_control(p, jay_is_no_mask(I));
   brw_set_default_group(p, simd_offs * exec_size);
   brw_set_default_swsb(p, dep);
   brw_set_default_saturate(p, I->saturate);

   /* Grab the hardware predicate, corresponding either to a logical predicate
    * or SEL's selector.
    */
   const jay_def *pred = I->predication ? jay_inst_get_predicate((void *) I) :
                         I->op == JAY_OPCODE_SEL ? &I->src[2] :
                                                   NULL;

   brw_set_default_predicate_control(p, pred ? BRW_PREDICATE_NORMAL :
                                               BRW_PREDICATE_NONE);
   brw_set_default_predicate_inverse(p, pred && pred->negate);

   /* Jay/brw enums line up by construction */
   enum brw_conditional_mod cmod =
      (enum brw_conditional_mod) I->conditional_mod;

   if (!jay_is_null(I->cond_flag)) {
      assert(!(pred && pred->reg != I->cond_flag.reg) && "must be tied");
      pred = &I->cond_flag;
   }

   if (pred) {
      unsigned reg = pred->reg * jay_phys_flag_per_virt(f->shader);
      brw_set_default_flag_reg(p, reg / 2, reg % 2);
   }

   if (I->op == JAY_OPCODE_MIN) {
      cmod = BRW_CONDITIONAL_L;
   } else if (I->op == JAY_OPCODE_MAX) {
      cmod = BRW_CONDITIONAL_GE;
   }

   struct brw_reg dst = to_brw_reg(f, I, -1, simd_offs, false);

   switch (I->op) {
      OP0(ELSE)
      OP0(ENDIF)
      OP0(WHILE)
      OP0(BREAK)
      OP1(MOV, MOV)
      OP1(MODIFIER, MOV)
      OP1(RNDD, RNDD)
      OP1(RNDZ, RNDZ)
      OP1(RNDE, RNDE)
      OP1(FRC, FRC)
      OP1(BFREV, BFREV)
      OP1(CBIT, CBIT)
      OP1(NOT, NOT)
      OP1(FBL, FBL)
      OP1(FBH, FBH)
      OP1(LZD, LZD)
      OP2(ROL, ROL)
      OP2(ROR, ROR)
      OP2(AVG, AVG)
      OP2(ADD, ADD)
      OP2(MUL, MUL)
      OP2(SEL, SEL)
      OP2(MIN, SEL)
      OP2(MAX, SEL)
      OP2(MUL_32X16, MUL)
      OP2(AND, AND)
      OP2(AND_U32_U16, AND)
      OP2(OR, OR)
      OP2(XOR, XOR)
      OP2(ASR, ASR)
      OP2(SHR, SHR)
      OP2(SHL, SHL)
      OP2(BFI1, BFI1)
      OP2(MAC, MAC)
      OP3(BFI2, BFI2)
      OP3(ADD3, ADD3)
      OP3(CSEL, CSEL)
      OP3(DP4A_UU, DP4A)
      OP3(DP4A_SS, DP4A)
      OP3(DP4A_SU, DP4A)
      OP3_SWAP(MAD, MAD)
      OP3_SWAP(BFE, BFE)

   case JAY_OPCODE_LOOP_ONCE:
      /* TODO: Is there a better way to do this? */
      brw_BREAK(p);
      brw_WHILE(p);
      break;

   case JAY_OPCODE_IF:
      brw_IF(p, util_logbase2(exec_size));
      break;

   case JAY_OPCODE_MATH:
      gfx6_math(p, dst, jay_math_op(I), SRC(0),
                retype(brw_null_reg(), to_brw_reg_type(I->type)));
      break;

   case JAY_OPCODE_BFN:
      brw_BFN(p, dst, SRC(0), SRC(1), SRC(2), brw_imm_ud(jay_bfn_ctrl(I)));
      break;

   case JAY_OPCODE_DESWIZZLE_ODD: {
      bool hi = simd_offs == 0 ? true : jay_deswizzle_odd_src2_hi(I);
      brw_set_default_group(p, 0);
      brw_MOV(p, dst,
              byte_offset(to_brw_reg(f, I, simd_offs, 0, false), hi ? 64 : 0));
      break;
   }

   case JAY_OPCODE_DESWIZZLE_EVEN:
      brw_set_default_exec_size(p, BRW_EXECUTE_16);
      brw_MOV(p, byte_offset(dst, 64),
              byte_offset(SRC(0), jay_deswizzle_even_src_hi(I) * 64));
      break;

   case JAY_OPCODE_CVT: {
      unsigned index = jay_cvt_index(I);
      bool force_hi = false;

      /* We will apply a suboffset for the specific subword being converted. In
       * the case where we have a subword (16-bit) stride, accesses to the upper
       * half will be instead to a discontiguous GRF so we have to fix up. This
       * affects u8->u32 conversions.
       */
      if (I->src[0].file == GPR) {
         unsigned type_size_B = jay_type_size_bits(jay_cvt_src_type(I)) / 8;
         unsigned index_B = index * type_size_B;
         unsigned stride_B =
            jay_stride_to_bits(jay_def_stride(f->shader, I->src[0])) / 8;

         if (index_B >= stride_B) {
            assert(stride_B == 2 && index_B <= 4 && !I->src[0].hi);
            force_hi = true;
            index = (index_B % stride_B) / type_size_B;
         }
      }

      brw_MOV(p, dst,
              suboffset(to_brw_reg(f, I, 0, simd_offs, force_hi), index));
      break;
   }

   case JAY_OPCODE_SYNC:
      brw_SYNC(p, jay_sync_op(I));

      if (!jay_is_null(I->src[0])) {
         brw_set_src0(p, brw_eu_last_inst(p), stride(SRC(0), 0, 1, 0));
      }
      break;

   case JAY_OPCODE_CMP:
      brw_CMP(p, dst, I->conditional_mod, SRC(0), SRC(1));
      break;

   case JAY_OPCODE_MOV_IMM64:
      brw_MOV(p, dst, brw_imm_u64(jay_mov_imm64_imm(I)));
      break;

   case JAY_OPCODE_RELOC:
      brw_MOV_reloc_imm(p, dst, BRW_TYPE_UD, jay_reloc_param(I),
                        jay_reloc_base(I));
      break;

   case JAY_OPCODE_QUAD_SWIZZLE:
      /* Quad swizzle can get split down to SIMD4 even on Xe2 where we don't
       * have NibCtrl.  Fortunately, it's NoMask so it doesn't matter.
       */
      brw_set_default_group(p, 0);
      brw_MOV(p, dst, quad_swizzle(SRC(0), I));
      break;

   case JAY_OPCODE_BROADCAST_IMM:
      brw_MOV(p, dst, get_element(SRC(0), jay_broadcast_imm_lane(I)));
      break;

   case JAY_OPCODE_SEND:
      brw_SEND(p, jay_send_sfid(I), dst, SRC(2), SRC(3), SRC(0), SRC(1),
               jay_send_ex_desc_imm(I), jay_send_ex_mlen(I),
               jay_send_bindless(I), jay_send_eot(I), false /* gather */);
      if (jay_send_check_tdr(I)) {
         brw_eu_inst_set_opcode(p->isa, brw_eu_last_inst(p), BRW_OPCODE_SENDC);
      }
      break;

   /* Gfx20+ has separate Render Target Array indices for each pair of subspans
    * in order to support multiple polygons, so we need to use a <1;8,0> region
    * in order to select the word for each channel.
    */
   case JAY_OPCODE_EXTRACT_LAYER:
      brw_AND(p, dst, stride(retype(SRC(simd_offs), BRW_TYPE_UW), 1, 8, 0),
              brw_imm_uw(0x7ff));
      break;

   case JAY_OPCODE_EXPAND_QUAD:
      brw_MOV(p, dst, stride(SRC(simd_offs), 1, 4, 0));
      break;

   case JAY_OPCODE_OFFSET_PACKED_PIXEL_COORDS:
      brw_set_default_exec_size(p, BRW_EXECUTE_32);
      brw_set_default_group(p, 0);
      brw_ADD(p, retype(dst, BRW_TYPE_UW), retype(SRC(0), BRW_TYPE_UW),
              brw_imm_uv(0x11100100));
      break;

   case JAY_OPCODE_LANE_ID_8:
      brw_set_default_exec_size(p, BRW_EXECUTE_8);
      brw_MOV(p, dst, brw_imm_uv(0x76543210));
      break;

   case JAY_OPCODE_LANE_ID_EXPAND:
      brw_set_default_exec_size(p, util_logbase2(jay_lane_id_expand_width(I)));
      brw_ADD(p, suboffset(dst, jay_lane_id_expand_width(I)), SRC(0),
              brw_imm_uw(jay_lane_id_expand_width(I)));
      break;

   case JAY_OPCODE_EXTRACT_BYTE_PER_8LANES:
      brw_MOV(p, dst, stride(retype(SRC(simd_offs), BRW_TYPE_UB), 1, 8, 0));
      break;

   case JAY_OPCODE_SHR_ODD_SUBSPANS_BY_4:
      brw_SHR(p, dst, SRC(0), brw_imm_uv(0x44440000));
      break;

   case JAY_OPCODE_MUL_32: {
      brw_MUL(p, retype(brw_acc_reg(1), to_brw_reg_type(I->type)), SRC(0),
              subscript(SRC(1), BRW_TYPE_UW, 0));

      brw_set_default_swsb(p, tgl_swsb_null());
      brw_alu2(p, jay_mul_32_high(I) ? BRW_OPCODE_MACH : BRW_OPCODE_MACL, dst,
               SRC(0), SRC(1));
      break;
   }

   case JAY_OPCODE_SHUFFLE: {
      struct brw_reg a0 = brw_address_reg(0);
      unsigned grf_16 = to_def_grf_16(&f->shader->partition, I->src[0]);
      unsigned offset_B = grf_16 * 2 * f->shader->dispatch_width;

      brw_ADD(p, a0, subscript(SRC(1), BRW_TYPE_UW, 0), brw_imm_uw(offset_B));
      brw_MOV(p, dst, retype(brw_VxH_indirect(0, 0), BRW_TYPE_UD));
      break;
   }

   default:
      jay_print_inst(stderr, (jay_inst *) I);
      UNREACHABLE("Unhandled opcode");
   }

   if (cmod != BRW_CONDITIONAL_NONE) {
      if (I->op != JAY_OPCODE_BFN) {
         brw_eu_inst_set_cond_modifier(p->devinfo, brw_eu_last_inst(p), cmod);
      } else {
         unsigned cc = cmod == BRW_CONDITIONAL_L    ? 3 :
                       cmod == BRW_CONDITIONAL_G    ? 2 :
                       cmod == BRW_CONDITIONAL_Z    ? 1 :
                       cmod == BRW_CONDITIONAL_NONE ? 0 :
                                                      -1;
         assert(cc < 4 && "invalid cmod for bfn");
         brw_eu_inst_set_boolean_func_cond_modifier(p->devinfo,
                                                    brw_eu_last_inst(p), cc);
      }
   }

   assert(p->nr_insn == (nr_ins_before + jay_macro_length(I)) &&
          "Jay instructions must map 1:n to GEN instructions");
}

struct jay_shader_bin *
jay_to_binary(jay_shader *s,
              void *const_data,
              size_t const_data_size,
              bool debug)
{
   struct jay_shader_bin *bin = rzalloc(s, struct jay_shader_bin);

   struct util_dynarray prog;
   util_dynarray_init(&prog, bin);

   struct brw_isa_info isa;
   struct brw_codegen p;

   brw_init_isa_info(&isa, s->devinfo);
   brw_init_codegen(&isa, &p, bin);
   int start_offset = p.next_insn_offset;

   /* TODO: Multifunction properly */
   jay_foreach_function(s, f) {
      jay_foreach_block(f, block) {
         if (block->loop_header) {
            brw_DO(&p, 0);
         }

         jay_foreach_inst_in_block(block, I) {
            for (unsigned i = 0; i < (1 << jay_simd_split(s, I)); ++i) {
               emit(&p, f, I, i);
            }
         }
      }
   }

   int final_halt_offset = -1 /* TODO */;
   brw_set_uip_jip(&p, start_offset, final_halt_offset);

   struct disasm_info *disasm = disasm_initialize(p.isa, NULL);

   disasm_new_inst_group(disasm, 0);
   disasm_new_inst_group(disasm, p.next_insn_offset);

   UNUSED bool valid = true;
#ifndef NDEBUG
   valid =
      brw_validate_instructions(p.isa, p.store, 0, p.next_insn_offset, disasm);
#endif

   brw_compact_instructions(&p, start_offset, disasm);

   if (debug || !valid) {
      dump_assembly(p.store, 0, p.next_insn_offset, disasm, NULL, stdout);
   }

   if (!valid) {
      UNREACHABLE("invalid assembly");
   }

   struct brw_stage_prog_data *prog_data = &s->prog_data->base;

   assert(prog_data->const_data_size == 0);
   if (const_data_size > 0) {
      prog_data->const_data_size = const_data_size;
      prog_data->const_data_offset =
         brw_append_data(&p, const_data, const_data_size, 32);
   }

   bin->kernel = brw_get_program(&p, &bin->size);
   s->prog_data->base.relocs =
      brw_get_shader_relocs(&p, &s->prog_data->base.num_relocs);

   return bin;
}
