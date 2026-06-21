/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "compiler/gen/gen.h"
#include "compiler/gen/gen_enums.h"
#include "compiler/gen/gen_helpers.h"
#include "dev/intel_debug.h"
#include "util/macros.h"
#include "util/u_dynarray.h"
#include "util/u_math.h"
#include "jay.h"
#include "jay_builder.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

static inline gen_reg_type
to_gen_reg_type(enum jay_type type)
{
   /* clang-format off */
   switch (type) {
   case JAY_TYPE_UNTYPED:
   case JAY_TYPE_U8:   return GEN_TYPE_UB;
   case JAY_TYPE_U16:  return GEN_TYPE_UW;
   case JAY_TYPE_U32:  return GEN_TYPE_UD;
   case JAY_TYPE_U64:  return GEN_TYPE_UQ;
   case JAY_TYPE_S8:   return GEN_TYPE_B;
   case JAY_TYPE_S16:  return GEN_TYPE_W;
   case JAY_TYPE_S32:  return GEN_TYPE_D;
   case JAY_TYPE_S64:  return GEN_TYPE_Q;
   case JAY_TYPE_F16:  return GEN_TYPE_HF;
   case JAY_TYPE_F32:  return GEN_TYPE_F;
   case JAY_TYPE_F64:  return GEN_TYPE_DF;
   case JAY_TYPE_BF16: return GEN_TYPE_BF;
   default: UNREACHABLE("invalid type");
   }
   /* clang-format on */
}

#define GEN_INST_BYTES 16

struct jay_codegen {
   const struct intel_device_info *devinfo;

   gen_inst *insts;
   int num_insts;
   int insts_cap;

   /* Indices of the active loop headers. */
   struct util_dynarray loop_stack;

   uint8_t *output;
   int output_size;

   /* struct intel_shader_reloc */
   struct util_dynarray relocs;

   /* Index of the final HALT instruction, or -1 if none has been emitted yet. */
   int final_halt_offset;
};

static inline gen_operand
to_gen_operand(
   jay_function *f, const jay_inst *I, signed idx, unsigned simd_offs, bool hi)
{
   const struct intel_device_info *devinfo = f->shader->devinfo;
   bool is_dest = idx < 0;
   enum jay_type type = is_dest ? I->type : jay_src_type(I, idx);
   jay_def d = is_dest ? I->dst : I->src[idx];
   hi |= d.hi;

   gen_operand R;
   unsigned reg = d.reg, count = jay_num_values(d);
   unsigned offset_B = 0, grf = 0;
   assert(!hi || d.file == GPR || d.file == FLAG);

   if (count && (d.file == GPR || d.file == UGPR)) {
      struct jay_register_block block =
         jay_lookup_block(&f->shader->partition, d.reg, d.file);

      grf = block.start_grf;
      reg -= block.start_gpr;

      assert(reg + count <= block.len_gpr && "must not cross partitions");
   }

   if (jay_is_imm(d)) {
      /* Immediates have size restrictions but can zero extend */
      if (jay_type_size_bits(type) == 64) {
         type = jay_type_resize(type, 32);
      } else if (jay_num_isa_srcs(I) == 3) {
         assert(jay_as_uint(d) <= UINT16_MAX);
         type = JAY_TYPE_U16;
      }

      R = gen_imm_ud(jay_as_uint(d));
   } else if (jay_is_null(d)) {
      R = gen_null();
   } else if (d.file == UGPR || d.file == UACCUM) {
      grf += (reg / jay_ugpr_per_grf(f->shader));
      offset_B = (reg % jay_ugpr_per_grf(f->shader)) * 4;

      if (d.file == UGPR) {
         R = gen_grf(grf, 0);
      } else {
         R = gen_accumulator(grf);
      }
      R = gen_retype(gen_restride(R, 0, 1, 0), GEN_TYPE_UD);

      /* Handle 3-src restrictions and vectorized uniform code. */
      if (is_dest || jay_num_values(d) >= 4) {
         R = gen_restride(R, 8, 8, 1);
      }

      /* Some operations have special restrictions on the destination stride,
       * but if we write a single UGPR the stride is ignored..  Specify
       * whatever stride is needed to satisfy the rules.
       */
      if (is_dest && I->num_srcs > 0) {
         /* BSpec 56640 "Special Restrictions" says:
          *
          *    "Conversion between HF and Integer must be DWord-aligned
          *     and strided by a DWord on the destination."
          */
         enum jay_type src0_type = jay_src_type(I, 0);
         if ((I->type == JAY_TYPE_F16 && !jay_type_is_any_float(src0_type)) ||
             (src0_type == JAY_TYPE_F16 && !jay_type_is_any_float(I->type))) {
            assert(jay_num_values(d) == 1 && "must not vectorize HF<->Int");
            R = gen_restride(R, 8, 2, 4);
         }

         /* Packed floats have restrictions on mixed sizes.  Use <2>. */
         if (I->type == JAY_TYPE_F16 && jay_type_size_bits(src0_type) != 16) {
            assert(jay_num_values(d) == 1 && "must not vectorize mixed float");
            R = gen_restride(R, 4, 2, 2);
         }
      }
   } else if (d.file == GPR || d.file == ACCUM) {
      enum jay_stride def_stride =
         d.file == GPR ? jay_def_stride(f->shader, d) : JAY_STRIDE_4;
      uint32_t type_bits = jay_type_size_bits(type);
      unsigned stride_bits = jay_stride_to_bits(def_stride);
      unsigned simd_width = jay_simd_width_physical(f->shader, I);

      if (def_stride == JAY_STRIDE_2) {
         /* Select between lo/hi halves of the GPR */
         grf += reg * jay_grf_per_gpr(f->shader);
         offset_B = hi ? 2 * f->shader->dispatch_width : 0;
      } else {
         /* Treat low bits as an offset in 2-byte words into the GRF */
         unsigned r = (reg * 2) + hi;
         unsigned mask = BITFIELD_MASK(stride_bits / 32);
         grf += ((r & ~mask) / 2) * jay_grf_per_gpr(f->shader);
         offset_B = (r & mask) * 2;
      }

      if (d.file == GPR) {
         R = gen_restride(gen_grf(grf, 0), 8, 8, 1);
      } else {
         R = gen_restride(gen_accumulator(grf / 2), 8, 8, 1);
      }

      R = gen_byte_offset(devinfo, R, simd_offs * simd_width * stride_bits / 8);

      if (stride_bits == (type_bits * 4)) {
         R = gen_restride(R, 8, 2, 4);
      } else if (stride_bits == (type_bits * 2)) {
         R = gen_restride(R, 4, 2, 2);
      } else {
         assert(stride_bits == type_bits);
      }

      /* Broadcast is equivalent to <8, 8, 1> for SIMD1 instructions. Use that
       * instead due to regioning restrictions.
       */
      if (simd_width == 1) {
         R = gen_restride(R, 0, 1, 0);
      }
   } else if (jay_is_flag(d)) {
      /* Explicit flags act like UGPRs. As sources they broadcast to all lanes,
       * so we may ignore the SIMD offset. As destinations, they are written by
       * SIMD1 instructions and are never SIMD split.
       */
      assert(simd_offs == 0 || idx >= 0);
      unsigned offs_B =
         (d.reg * (f->shader->dispatch_width / 8)) + (hi ? 2 : 0);
      R = gen_flag(offs_B / 2);
   } else if (d.file == J_ADDRESS) {
      R = gen_address(d.reg);
   } else if (d.file == J_ARF) {
      R = gen_arf(jay_base_index(d), 0);
   } else {
      UNREACHABLE("unexpected file");
   }

   R.negate = d.negate;
   R.abs = d.abs;
   R.type = to_gen_reg_type(type);
   return gen_byte_offset(devinfo, R, offset_B);
}

static gen_operand
quad_swizzle(const struct intel_device_info *devinfo,
             gen_operand r,
             const jay_inst *I)
{
   static const struct {
      gen_region region;
      unsigned element;
   } map[] = {
      [JAY_QUAD_SWIZZLE_XXXX] = { { 4, 4, 0 }, 0 },
      [JAY_QUAD_SWIZZLE_YYYY] = { { 4, 4, 0 }, 1 },
      [JAY_QUAD_SWIZZLE_ZZZZ] = { { 4, 4, 0 }, 2 },
      [JAY_QUAD_SWIZZLE_WWWW] = { { 4, 4, 0 }, 3 },
      [JAY_QUAD_SWIZZLE_XXZZ] = { { 2, 2, 0 }, 0 },
      [JAY_QUAD_SWIZZLE_YYWW] = { { 2, 2, 0 }, 1 },
      [JAY_QUAD_SWIZZLE_XYXY] = { { 0, 2, 1 }, 0 },
      [JAY_QUAD_SWIZZLE_ZWZW] = { { 0, 2, 1 }, 2 },
   };

   enum jay_quad_swizzle swizzle = jay_quad_swizzle_swizzle(I);
   assert(swizzle < ARRAY_SIZE(map));
   r.region = map[swizzle].region;
   return gen_element_offset(devinfo, r, map[swizzle].element);
}

#define OP(jay, hw, num) [JAY_OPCODE_##jay] = { GEN_OP_##hw, num }

static const struct {
   enum gen_opcode op;
   unsigned num_srcs;
} jay_to_gen_opcodes[] = {
   /* clang-format off */
   OP(ADD3, ADD3, 3),
   OP(ADD, ADD, 2),
   OP(AND, AND, 2),
   OP(AND_U32_U16, AND, 2),
   OP(ASR, ASR, 2),
   OP(AVG, AVG, 2),
   OP(BFE, BFE, 3),
   OP(BFI1, BFI1, 2),
   OP(BFI2, BFI2, 3),
   OP(BFN, BFN, 3),
   OP(BFREV, BFREV, 1),
   OP(BREAK, BREAK, 0),
   OP(BROADCAST_IMM, MOV, 1),
   OP(CBIT, CBIT, 1),
   OP(CMP, CMP, 2),
   OP(CSEL, CSEL, 3),
   OP(CVT, MOV, 1),
   OP(DESWIZZLE_EVEN, MOV, 1),
   OP(DESWIZZLE_ODD, MOV, 1),
   OP(DP4A_SS, DP4A, 3),
   OP(DP4A_SU, DP4A, 3),
   OP(DP4A_UU, DP4A, 3),
   OP(DPAS, DPAS, 3),
   OP(ELSE, ELSE, 0),
   OP(ENDIF, ENDIF, 0),
   OP(EXPAND_QUAD, MOV, 2),
   OP(EXTRACT_BYTE_PER_8LANES, MOV, 2),
   OP(EXTRACT_SUBSPAN_INFO, AND, 2),
   OP(FBH, FBH, 1),
   OP(FBL, FBL, 1),
   OP(FRC, FRC, 1),
   OP(HALT, HALT, 0),
   OP(IF, IF, 0),
   OP(LANE_ID_8, MOV, 0),
   OP(LZD, LZD, 1),
   OP(MAC, MAC, 2),
   OP(MAD, MAD, 3),
   OP(MATH, MATH, 1),
   OP(MAX, SEL, 2),
   OP(MIN, SEL, 2),
   OP(MODIFIER, MOV, 1),
   OP(MOV_IMM64, MOV, 0),
   OP(MOV, MOV, 1),
   OP(MUL_32, MUL, 2),
   OP(MUL_32X16, MUL, 2),
   OP(MUL, MUL, 2),
   OP(NOT, NOT, 1),
   OP(NOP, NOP, 0),
   OP(OFFSET_PACKED_PIXEL_COORDS, ADD, 1),
   OP(OR, OR, 2),
   OP(QUAD_SWIZZLE, MOV, 1),
   OP(RELOC, MOV, 0),
   OP(RNDD, RNDD, 1),
   OP(RNDE, RNDE, 1),
   OP(RNDZ, RNDZ, 1),
   OP(ROL, ROL, 2),
   OP(ROR, ROR, 2),
   OP(SEL, SEL, 2),
   OP(SEND, SEND, 0),
   OP(SHL, SHL, 2),
   OP(SHR_ODD_SUBSPANS_BY_4, SHR, 1),
   OP(SHR, SHR, 2),
   OP(SHUFFLE, MOV, 2),
   OP(SYNC, SYNC, 1),
   OP(WHILE, WHILE, 0),
   OP(XOR, XOR, 2),
   OP(ZIP_UGPR16, MOV, 0),
   OP(SLICE_REPACK, MOV, 1),
   /* clang-format on */
};

/*
 * Emit a single hardware instruction. This runs multiple times per IR
 * instruction in the case of SIMD splits and macros, so this must not modify
 * the instruction!
 */
static void
emit(struct jay_codegen *jc,
     struct gen_inst *gen,
     jay_function *f,
     const jay_inst *I,
     unsigned simd_offs,
     unsigned idx_in_macro)
{
   /* Replicate the SWSB regdist for SIMD split instructions if needed */
   gen_swsb dep = simd_offs && !I->replicate_dep ? gen_swsb_null() : I->dep;

   /* We do not allow SBID dependencies on SIMD split instructions since
    * individual groups could get shot down. This would require more tracking
    * and is unclear whether it's beneficial.
    */
   assert(simd_offs == 0 || I->dep.mode == GEN_SBID_NULL);

   if (I->decrement_dep) {
      unsigned delta = simd_offs * jay_macro_length(I);
      assert(dep.regdist > delta);
      dep.regdist -= delta;
   }

   gen->exec_size = jay_simd_width_physical(f->shader, I);
   gen->no_mask = jay_is_no_mask(I);
   gen->chan_offset = simd_offs * gen->exec_size;
   gen->swsb = dep;
   gen->saturate = I->saturate;
   gen->cmod = I->op == JAY_OPCODE_MIN ? GEN_CONDITION_LT :
               I->op == JAY_OPCODE_MAX ? GEN_CONDITION_GE :
                                         I->conditional_mod;

   /* Grab the hardware predicate, corresponding either to a logical predicate
    * or SEL's selector.
    */
   const jay_def *pred = I->predication ? jay_inst_get_predicate((void *) I) :
                         I->op == JAY_OPCODE_SEL ? &I->src[2] :
                                                   NULL;

   gen->pred_control = pred ? GEN_PREDICATE_NORMAL : GEN_PREDICATE_NONE;
   gen->pred_inv = pred && pred->negate;

   if (!jay_is_null(I->cond_flag)) {
      assert(!(pred && pred->reg != I->cond_flag.reg) && "must be tied");
      pred = &I->cond_flag;
   }

   if (pred) {
      unsigned reg = pred->reg * jay_phys_flag_per_virt(f->shader);
      gen->flag_nr = reg / 2;
      gen->flag_subnr = reg % 2;
   }

   gen->opcode = jay_to_gen_opcodes[I->op].op;
   gen->dst = to_gen_operand(f, I, -1, simd_offs, false);

   for (unsigned i = 0; i < jay_to_gen_opcodes[I->op].num_srcs; ++i) {
      gen->src[i] = to_gen_operand(f, I, i, simd_offs, false);
   }

   switch (I->op) {
   case JAY_OPCODE_WHILE: {
      assert(util_dynarray_num_elements(&jc->loop_stack, int) > 0);
      int target = util_dynarray_pop(&jc->loop_stack, int);
      gen->src[0] = gen_imm_d(GEN_INST_BYTES * (target - (jc->num_insts - 1)));
      break;
   }

   case JAY_OPCODE_MATH:
      gen->src[1] = gen_null();
      gen->math.func = (gen_math) jay_math_op(I);
      break;

   case JAY_OPCODE_BFN:
      gen->boolean_func_ctrl = jay_bfn_ctrl(I);
      break;

   case JAY_OPCODE_DESWIZZLE_ODD: {
      bool hi = simd_offs == 0 ? true : jay_deswizzle_odd_src2_hi(I);
      gen->chan_offset = 0;
      gen->src[0] =
         gen_byte_offset(jc->devinfo, to_gen_operand(f, I, simd_offs, 0, false),
                         hi ? 64 : 0);
      break;
   }

   case JAY_OPCODE_DESWIZZLE_EVEN:
      gen->exec_size = 16;
      gen->dst = gen_byte_offset(jc->devinfo, gen->dst, 64);
      gen->src[0] = gen_byte_offset(jc->devinfo, gen->src[0],
                                    jay_deswizzle_even_src_hi(I) * 64);
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

      gen_operand src = to_gen_operand(f, I, 0, simd_offs, force_hi);
      gen->src[0] = gen_element_offset(jc->devinfo, src, index);
      break;
   }

   case JAY_OPCODE_SYNC:
      gen->src[0] = gen_restride(gen->src[0], 0, 1, 0);
      gen->sync.func = (gen_sync_func) jay_sync_op(I);
      break;

   case JAY_OPCODE_MOV_IMM64:
      gen->src[0] = gen_imm_uq(jay_mov_imm64_imm(I));
      break;

   case JAY_OPCODE_RELOC: {
      util_dynarray_append(&jc->relocs,
                           ((struct intel_shader_reloc) {
                              .id = jay_reloc_param(I),
                              .type = INTEL_SHADER_RELOC_TYPE_MOV_IMM,
                              .offset = GEN_INST_BYTES * (jc->num_insts - 1),
                              .delta = jay_reloc_base(I),
                           }));

      gen->src[0] = gen_imm_ud(GEN_UNCOMPACTABLE_PATCH_IMM);
      break;
   }

   case JAY_OPCODE_QUAD_SWIZZLE:
      /* Quad swizzle can get split down to SIMD4 even on Xe2 where we don't
       * have NibCtrl.  Fortunately, it's NoMask so it doesn't matter.
       */
      gen->chan_offset = 0;
      gen->src[0] = quad_swizzle(jc->devinfo, gen->src[0], I);
      break;

   case JAY_OPCODE_BROADCAST_IMM: {
      if (gen->src[0].file != GEN_IMM) {
         gen->src[0] = gen_element_offset(jc->devinfo, gen->src[0],
                                          jay_broadcast_imm_lane(I));
         gen->src[0] = gen_restride(gen->src[0], 0, 1, 0);
      }
      break;
   }

   case JAY_OPCODE_SEND: {
      gen_operand ex_desc = to_gen_operand(f, I, 1, simd_offs, false);
      gen->src[0] =
         gen_retype(to_gen_operand(f, I, 2, simd_offs, false), GEN_TYPE_UD);
      gen->src[1] =
         gen_retype(to_gen_operand(f, I, 3, simd_offs, false), GEN_TYPE_UD);

      gen->opcode = jay_send_check_tdr(I) ? GEN_OP_SENDC : GEN_OP_SEND;
      gen->send.eot = jay_send_eot(I);
      gen->send.sfid = (gen_sfid) jay_send_sfid(I);
      gen->send.ex_bso = jay_send_bindless(I);
      gen->dst.type = GEN_TYPE_UD;

      if (jay_is_imm(I->src[0]))
         gen->send.desc_imm = jay_as_uint(I->src[0]);
      else
         gen->send.desc_is_reg = true;

      if (ex_desc.file == GEN_IMM) {
         assert(jay_send_ex_desc_imm(I) == 0);
         gen->send.ex_desc_imm = ex_desc.imm;
      } else {
         gen->send.ex_desc_is_reg = true;
         gen->send.ex_desc_subnr = ex_desc.subnr;
         gen->send.ex_desc_imm_extra = jay_send_ex_desc_imm(I);
      }

      if (jay_send_bindless(I) || gen->send.sfid == GEN_SFID_UGM)
         gen->send.src1_len = jay_send_ex_mlen(I) / reg_unit(jc->devinfo);

      break;
   }

   /* On Gfx20+, the viewport index, render target array index, and facing
    * fields come together in consecutive words for each pair of subspans.
    * We use a <1;8,0>W region so that each pair of 4-lane subspans reads
    * the right value, and split to SIMD16 since the high subspans come
    * in a separate register.
    */
   case JAY_OPCODE_EXTRACT_SUBSPAN_INFO:
      gen->src[0] =
         gen_restride(gen_retype(gen->src[simd_offs], GEN_TYPE_UW), 1, 8, 0);
      gen->src[1] = gen_imm_uw(jay_extract_subspan_info_mask(I));
      break;

   case JAY_OPCODE_EXPAND_QUAD:
      gen->src[0] = gen_restride(gen->src[simd_offs], 1, 4, 0);
      break;

   case JAY_OPCODE_OFFSET_PACKED_PIXEL_COORDS:
      gen->exec_size = 32;
      gen->chan_offset = 0;
      gen->dst = gen_retype(gen->dst, GEN_TYPE_UW);
      gen->src[0] = gen_retype(gen->src[0], GEN_TYPE_UW);
      gen->src[1] = gen_imm_uv(0x11100100);
      break;

   case JAY_OPCODE_LANE_ID_8:
      gen->src[0] = gen_imm_uv(0x76543210);
      break;

   case JAY_OPCODE_ZIP_UGPR16:
      gen->src[0] = to_gen_operand(f, I, simd_offs, 0, false);
      break;

   case JAY_OPCODE_EXTRACT_BYTE_PER_8LANES: {
      gen->src[0] =
         gen_restride(gen_retype(gen->src[simd_offs], GEN_TYPE_UB), 1, 8, 0);
      break;
   }

   case JAY_OPCODE_SHR_ODD_SUBSPANS_BY_4:
      gen->src[1] = gen_imm_uv(0x44440000);
      break;

   case JAY_OPCODE_MUL_32:
      if (idx_in_macro == 0) {
         gen->dst = gen_accumulator(0);
         gen->dst.type = to_gen_reg_type(I->type);
         gen->src[1] = gen_subscript(jc->devinfo, gen->src[1], GEN_TYPE_UW, 0);
      } else {
         gen->swsb = gen_swsb_null();
         gen->opcode = jay_mul_32_high(I) ? GEN_OP_MACH : GEN_OP_MACL;
      }
      break;

   case JAY_OPCODE_SHUFFLE:
      if (idx_in_macro == 0) {
         assert(I->src[0].file == GPR && jay_num_values(I->src[0]) == 1);
         struct jay_register_block block =
            jay_lookup_block(&f->shader->partition, I->src[0].reg, GPR);

         unsigned offset_B =
            (block.start_grf * jc->devinfo->grf_size) +
            ((I->src[0].reg - block.start_gpr) * 4 * f->shader->dispatch_width);

         gen->opcode = GEN_OP_ADD;
         gen->dst = gen_address(0);
         gen->src[0] = gen_subscript(jc->devinfo, gen->src[1], GEN_TYPE_UW, 0);
         gen->src[1] = gen_imm_uw(offset_B);
      } else {
         gen->src[0] = gen_grf(0, 0);
         gen->src[0].type = GEN_TYPE_UD;
         gen->src[0].indirect = true;
         gen->src[0].region.vstride = GEN_VSTRIDE_ONE_DIMENSIONAL;
         gen->src[0].addr_imm = 0;
      }
      break;

   case JAY_OPCODE_HALT:
      if (jay_halt_predicate_all(I)) {
         assert(I->predication);
         gen->pred_control =
            jc->devinfo->ver >= 20 ? GEN_PREDICATE_XE2_ALL : GEN_PREDICATE_ALLV;
      }
      break;

   case JAY_OPCODE_HALT_TARGET:
      /* HALT temporarily disables channels, and the same instruction is used
       * to re-enable them: once all channels are disabled, then they are
       * re-enabled again immediately.
       *
       * So put a HALT right before the "epilogue" of the shader to make sure
       * all channels get HALTed, so that this last HALT will re-enable them
       * again.
       */
      jc->final_halt_offset = jc->num_insts - 1;
      gen->opcode = GEN_OP_HALT;
      break;

   case JAY_OPCODE_DPAS: {
      gen_reg_type acc_type = to_gen_reg_type(jay_dpas_acc_type(I));
      gen_reg_type src_type = to_gen_reg_type(jay_dpas_src_type(I));

      gen->dst = gen_retype(gen->dst, acc_type);
      gen->src[0] = gen_retype(gen->src[0], acc_type);
      gen->src[1] = gen_retype(gen->src[1], src_type);
      gen->src[2] = gen_retype(gen->src[2], src_type);

      gen->dpas.sdepth = jay_dpas_sdepth(I);
      gen->dpas.rcount = jay_dpas_rcount(I);
      gen->exec_size = jc->devinfo->ver >= 20 ? 16 : 8;
      break;
   }

   case JAY_OPCODE_SLICE_REPACK: {
      const unsigned elem_bits = 32 >> jay_slice_repack_factor_log2(I);
      const unsigned unpacked_B = idx_in_macro * gen->exec_size * 4;
      const unsigned packed_B = idx_in_macro * gen->exec_size * (elem_bits / 8);
      gen_reg_type t = to_gen_reg_type(jay_type(JAY_TYPE_U, elem_bits));

      gen_operand *unpacked = &gen->src[0];
      gen_operand *packed   = &gen->dst;

      if (jay_slice_repack_unpack(I))
         SWAP(unpacked, packed);

      *packed   = gen_retype(gen_byte_offset(jc->devinfo, *packed,   packed_B), t);
      *unpacked = gen_retype(gen_byte_offset(jc->devinfo, *unpacked, unpacked_B), t);

      if (elem_bits == 16)
         *unpacked = gen_restride(*unpacked, 4, 2, 2);
      else if (elem_bits == 8)
         *unpacked = gen_restride(*unpacked, 8, 2, 4);

      break;
   }

   default:
      break;
   }

   static_assert(GEN_OP_ILLEGAL == 0);
   if (!gen->opcode) {
      jay_print_inst(stderr, (jay_inst *) I);
      UNREACHABLE("Unhandled opcode");
   }
}

struct jay_shader_bin *
jay_to_binary(jay_shader *s,
              void *const_data,
              size_t const_data_size,
              bool debug)
{
   struct jay_shader_bin *bin = rzalloc(s, struct jay_shader_bin);
   void *mem_ctx = ralloc_context(NULL);

   int total_gen_insts = 0;
   jay_foreach_function(s, f) {
      jay_inst *last = NULL;

      jay_foreach_block(f, block) {
         jay_foreach_inst_in_block_safe(block, I) {
            total_gen_insts +=
               (1 << jay_simd_split(s, I)) * jay_macro_length(I);

            /* Workaround for an issue with branch prediction for WHILE
             * instructions that may lead to misrendering or GPU hangs.
             * See HSDs 22020521218 and 16026360541.
             */
            if (I->op == JAY_OPCODE_WHILE &&
                (last && jay_op_is_control_flow(last->op)) &&
                s->devinfo->ver >= 20) {

               jay_builder b = jay_init_builder(f, jay_before_inst(I));
               jay_NOP(&b);
               total_gen_insts++;
            }

            last = I;
         }
      }
   }

   const unsigned max_code_size = total_gen_insts * GEN_INST_BYTES;
   const unsigned output_capacity =
      const_data_size > 0 ? align(max_code_size, 32) + const_data_size :
                            max_code_size;

   struct jay_codegen jc = {
      .devinfo = s->devinfo,
      .insts = rzalloc_array(mem_ctx, gen_inst, total_gen_insts),
      .insts_cap = total_gen_insts,
      .output = rzalloc_size(bin, output_capacity),
      .final_halt_offset = -1,
   };
   util_dynarray_init(&jc.loop_stack, mem_ctx);
   util_dynarray_init(&jc.relocs, bin);

   struct brw_stage_prog_data *prog_data = &s->prog_data->base;
   int code_size;

   /* TODO: Multifunction properly */
   jay_foreach_function(s, f) {
      jay_foreach_block(f, block) {
         if (block->loop_header) {
            util_dynarray_append(&jc.loop_stack, jc.num_insts);
         }

         jay_foreach_inst_in_block(block, I) {
            // jay_print_inst(stdout, (jay_inst *) I);

            for (unsigned i = 0; i < (1 << jay_simd_split(s, I)); ++i) {
               for (unsigned j = 0; j < jay_macro_length(I); ++j) {
                  assert(jc.num_insts < jc.insts_cap);
                  gen_inst *gen = &jc.insts[jc.num_insts++];

                  emit(&jc, gen, f, I, i, j);
               }
            }
         }
      }
   }

   assert(util_dynarray_num_elements(&jc.loop_stack, int) == 0);

   /* TODO: Check if jay still needs this normalization. */
   for (int i = 0; i < jc.num_insts; i++) {
      gen_inst *gen = &jc.insts[i];

      if ((gen->dst.file == GEN_GRF || gen->dst.file == GEN_ARF) &&
          gen->dst.region.hstride == 0)
         gen->dst.region.hstride = 1;
   }

   gen_finish_structured_cf(jc.insts, jc.num_insts, jc.final_halt_offset);

   const unsigned num_relocs =
      util_dynarray_num_elements(&jc.relocs, struct intel_shader_reloc);

   int *inst_offsets =
      num_relocs > 0 ? rzalloc_array(mem_ctx, int, jc.num_insts) : NULL;

   gen_encode_params enc_params = {
      .devinfo = jc.devinfo,
      .compact_all = true,
#ifdef NDEBUG
      .skip_validation = true,
#endif
      .insts = jc.insts,
      .num_insts = jc.num_insts,
      .mem_ctx = bin,
      .raw_bytes = jc.output,
      .raw_bytes_size = max_code_size,
      .encoded_offsets = inst_offsets,
   };

   bool encoded = gen_encode(&enc_params);
   if (!encoded) {
      gen_print_params print_params = {
         .devinfo = jc.devinfo,
         .fp = stderr,
         .insts = jc.insts,
         .num_insts = jc.num_insts,
         .errors = enc_params.errors,
         .num_errors = enc_params.num_errors,
      };
      gen_print(&print_params);
      ralloc_free(mem_ctx);
      UNREACHABLE("invalid assembly");
   }

   code_size = enc_params.raw_bytes_size;
   jc.output_size = code_size;

   /* Update reloc offsets to use the actual encoded offsets, which
    * will account for instruction compaction.
    */
   for (unsigned i = 0; i < num_relocs; i++) {
      struct intel_shader_reloc *reloc =
         util_dynarray_element(&jc.relocs, struct intel_shader_reloc, i);
      assert(reloc->type == INTEL_SHADER_RELOC_TYPE_MOV_IMM);
      assert(reloc->offset % GEN_INST_BYTES == 0);

      unsigned inst_idx = reloc->offset / GEN_INST_BYTES;
      assert(inst_idx < (unsigned) jc.num_insts);

      reloc->offset = inst_offsets[inst_idx];
   }

   if (debug || s->archiver) {
      gen_print_params print_params = {
         .devinfo = jc.devinfo,
         .insts = jc.insts,
         .num_insts = jc.num_insts,
         .raw_bytes = jc.output,
         .raw_bytes_size = code_size,
      };

      if (debug) {
         print_params.fp = stdout;
         gen_print(&print_params);
      }

      if (s->archiver) {
         const char *filename =
            ralloc_asprintf(s, "GEN%u/0", s->dispatch_width);
         print_params.fp = debug_archiver_start_file(s->archiver, filename);
         gen_print(&print_params);
         debug_archiver_finish_file(s->archiver);
      }
   }

   assert(prog_data->const_data_size == 0);
   if (const_data_size > 0) {
      unsigned offset = align(jc.output_size, 32);
      assert(offset + const_data_size <= output_capacity);
      memcpy(jc.output + offset, const_data, const_data_size);
      jc.output_size = offset + const_data_size;
      prog_data->const_data_size = const_data_size;
      prog_data->const_data_offset = offset;
   }

   prog_data->relocs = jc.relocs.data;
   prog_data->num_relocs = num_relocs;

   bin->kernel = (const uint32_t *) jc.output;
   bin->size = jc.output_size;

   ralloc_free(mem_ctx);
   return bin;
}
