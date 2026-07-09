/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/brw/brw_compiler.h"
#include "compiler/brw/brw_eu.h"
#include "compiler/brw/brw_eu_defines.h"
#include "compiler/brw/brw_nir.h"
#include "compiler/brw/brw_sampler.h"
#include "compiler/gen/gen_enums.h"
#include "compiler/intel_nir.h"
#include "compiler/intel_shader_enums.h"
#include "compiler/list.h"
#include "intel/dev/intel_debug.h"
#include "mda/debug_archiver.h"
#include "util/bitscan.h"
#include "util/bitset.h"
#include "util/lut.h"
#include "util/macros.h"
#include "util/u_math.h"
#include "intel_device_info_gen.h"
#include "jay.h"
#include "jay_builder.h"
#include "jay_builder_opcodes.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_defines.h"
#include "nir_intrinsics.h"
#include "nir_intrinsics_indices.h"
#include "nir_opcodes.h"
#include "nir_search_helpers.h"
#include "shader_enums.h"
#include "shader_stats.h"

static const struct debug_named_value jay_debug_options[] = {
   { "noopt",       JAY_DBG_NOOPT,       "Disable backend optimizer"             },
   { "printdemand", JAY_DBG_PRINTDEMAND, "Print demand per instruction"          },
   { "spill",       JAY_DBG_SPILL,       "Shrink register file to test spilling" },
   { "sync",        JAY_DBG_SYNC,        "Sync after every instruction"          },
   { "noacc",       JAY_DBG_NOACC,       "Disable accumulator substitution"      },
   { "nosched",     JAY_DBG_NOSCHED,     "Disable scheduling"                    },
   { "strict",      JAY_DBG_STRICT,      "Strictly conform to bspec/fulsim"      },
   DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(jay_debug, "JAY_DEBUG", jay_debug_options, 0)
int jay_debug = 0;

typedef struct jay_vs_payload {
   /* "the maximum limit is 30 elements per vertex" (bspec 56124) */
   jay_def attributes[30 * 4];
} jay_vs_payload;

typedef struct jay_tcs_payload {
   jay_def primitive_id;
   jay_def icp_handles;
} jay_tcs_payload;

typedef struct jay_tes_payload {
   jay_def tess_coord;
   jay_def patch_inputs[32 * 4];
} jay_tes_payload;

typedef struct jay_cs_payload {
   jay_def local_invocation_ids;
} jay_cs_payload;

typedef struct jay_fs_payload {
   jay_def bary[INTEL_BARYCENTRIC_MODE_COUNT];

   struct {
      jay_def xy, z, w;
   } coord;

   jay_def config;
   jay_def coverage_mask;
   jay_def sample_pos;
   jay_def coefficients;
   jay_def *deltas;
} jay_fs_payload;

struct nir_to_jay_state {
   jay_shader *s;
   jay_function *f;
   const nir_shader *nir;
   const struct intel_device_info *devinfo;

   jay_builder bld;
   jay_block *current_block, *after_block, *break_block, *exit_block;

   unsigned indent;
   bool needs_final_halt;

   /* We cache ballot(true), ctz(ballot(true)), and 4*ctz(ballot(true)) within a
    * block. If we had competent backend CSE - or emitted uniformize in NIR and
    * taught NIR's CSE about ballots - we could remove this kludge.
    */
   jay_def active_lane_mask, active_lane, active_lane_x4;

   /* Likewise we cache a message header */
   jay_def msg_header[16];
   jay_def msg_header_unmoved[16];

   /* These defs contain the extracted payload. They are only valid while
    * translating NIR->Jay since they aren't maintained by Jay passes.
    */
   struct {
      jay_def u0, u1;
      jay_def sampler_state_pointer, scratch_surface;
      jay_def inline_data[16];
      jay_def push_data[512];
      jay_def urb_handle;

      union {
         jay_vs_payload vs;
         jay_tcs_payload tcs;
         jay_tes_payload tes;
         jay_cs_payload cs;
         jay_fs_payload fs;
      };
   } payload;
};

static jay_def
payload_u1(struct nir_to_jay_state *nj, unsigned idx, unsigned len)
{
   if (jay_is_null(nj->payload.u1))
      return jay_null();
   else
      return jay_extract_range(nj->payload.u1, idx, len);
}

static jay_def
emit_active_lane_mask(struct nir_to_jay_state *nj)
{
   /* Note that we don't use mask0 since it needs fixups. Just ballot(true). */
   if (jay_is_null(nj->active_lane_mask)) {
      nj->active_lane_mask = jay_alloc_def(&nj->bld, FLAG, 1);
      jay_MOV(&nj->bld, nj->active_lane_mask, 1);
   }

   return nj->active_lane_mask;
}

static jay_def
build_msg_header(struct nir_to_jay_state *nj, jay_def *desired)
{
   jay_builder *b = &nj->bld;

   /* Vectorized zeroing of the header when we first construct it */
   if (jay_is_null(nj->msg_header[0])) {
      jay_def zeroes = jay_alloc_def(b, UGPR, jay_ugpr_per_grf(b->shader));
      jay_MOV(b, zeroes, 0);

      jay_foreach_comp(zeroes, i) {
         nj->msg_header[i] = jay_extract(zeroes, i);
         nj->msg_header_unmoved[i] = jay_imm(0);
      }
   }

   /* Set all fields to what they should be */
   for (unsigned i = 0; i < jay_ugpr_per_grf(b->shader); ++i) {
      jay_def d = jay_is_null(desired[i]) ? jay_imm(0) : desired[i];

      if (!jay_defs_equivalent(nj->msg_header_unmoved[i], d)) {
         nj->msg_header_unmoved[i] = desired[i];
         nj->msg_header[i] = jay_MOV_u32(b, desired[i]);
      }
   }

   /* Zip it all up into a vector of UGPRs which will RA to a single GRF */
   return jay_collect_vectors(b, nj->msg_header, jay_ugpr_per_grf(b->shader));
}

static jay_def
emit_active_lane(struct nir_to_jay_state *nj)
{
   /* For this instruction to execute, some lane must be active. Therefore there
    * is a 1 in the lower [dispatch width] bits of the lane mask, so we may
    * equivalently use fbl.u32 instead of fbl.u[dispatch width].
    */
   if (jay_is_null(nj->active_lane)) {
      nj->active_lane = jay_alloc_def(&nj->bld, UGPR, 1);
      jay_FBL(&nj->bld, nj->active_lane, emit_active_lane_mask(nj));
   }

   return nj->active_lane;
}

static jay_def
emit_uniformize(struct nir_to_jay_state *nj, jay_def x)
{
   jay_builder *b = &nj->bld;
   if (x.file != GPR && x.file != FLAG) {
      return x;
   }

   if (jay_is_null(nj->active_lane_x4)) {
      nj->active_lane_x4 = jay_SHL_u32(b, emit_active_lane(nj), 2);
   }

   jay_def u = jay_alloc_def(b, x.file == FLAG ? UFLAG : UGPR, 1);
   jay_SHUFFLE(b, u, x, nj->active_lane_x4);
   return u;
}

static jay_block *jay_emit_cf_list(struct nir_to_jay_state *nj,
                                   struct exec_list *list);

/** Returns true if the entire compute workgroup fits in a single subgroup. */
static bool
jay_workgroup_is_one_subgroup(jay_builder *b, const nir_shader *nir)
{
   return mesa_shader_stage_uses_workgroup(nir->info.stage) &&
          !nir->info.workgroup_size_variable &&
          nir_static_workgroup_size(nir) <= b->shader->dispatch_width;
}

static enum jay_type
jay_base_type_for_nir(nir_alu_type nir_type)
{
   /* clang-format off */
   switch (nir_alu_type_get_base_type(nir_type)) {
   case nir_type_int:   return JAY_TYPE_S;
   case nir_type_uint:  return JAY_TYPE_U;
   case nir_type_bool:  return JAY_TYPE_S;
   case nir_type_float: return JAY_TYPE_F;
   default:             UNREACHABLE("invalid NIR type");
   }
   /* clang-format on */
}

static enum jay_file
jay_file_for_def(const nir_def *def)
{
   return def->bit_size == 1 ? (def->divergent ? FLAG : UFLAG) :
                               (def->divergent ? GPR : UGPR);
}

/**
 * Returns an jay_type for the ALU op's i-th source.
 * (Useful for conversions and comparisons.)
 */
static enum jay_type
jay_alu_source_type(nir_alu_instr *alu, unsigned i)
{
   return jay_type(jay_base_type_for_nir(nir_op_infos[alu->op].input_types[i]),
                   nir_src_bit_size(alu->src[i].src));
}

static enum jay_type
jay_type_for_glsl_base_type(enum glsl_base_type t)
{
   /* clang-format off */
   switch (t) {
   case GLSL_TYPE_UINT:         return JAY_TYPE_U32;
   case GLSL_TYPE_INT:          return JAY_TYPE_S32;
   case GLSL_TYPE_FLOAT:        return JAY_TYPE_F32;
   case GLSL_TYPE_FLOAT16:      return JAY_TYPE_F16;
   case GLSL_TYPE_BFLOAT16:     return JAY_TYPE_BF16;
   case GLSL_TYPE_DOUBLE:       return JAY_TYPE_F64;
   case GLSL_TYPE_UINT16:       return JAY_TYPE_U16;
   case GLSL_TYPE_INT16:        return JAY_TYPE_S16;
   case GLSL_TYPE_UINT8:        return JAY_TYPE_U8;
   case GLSL_TYPE_INT8:         return JAY_TYPE_S8;
   case GLSL_TYPE_UINT64:       return JAY_TYPE_U64;
   case GLSL_TYPE_INT64:        return JAY_TYPE_S64;
   default:                     UNREACHABLE("invalid base type");
   }
   /* clang-format on */
}

static inline jay_def
nj_def(nir_def *def)
{
   unsigned bits = def->num_components * MAX2(def->bit_size, 32);
   unsigned words = DIV_ROUND_UP(bits, 32);

   return jay_contiguous_def(jay_file_for_def(def), def->index, words);
}

static inline jay_def
nj_src(nir_src src)
{
   return nj_def(src.ssa);
}

static void
lower_bf(jay_builder *b, jay_inst *I)
{
   /* Needed b/c no region exists on Intel HW that allows for
    * SIMD1 bfloat ops. See BSpec 74213. 
    */
   if (I->dst.file == UGPR) {
      assert(jay_num_values(I->dst) && "we do not vectorize bf");
      unsigned factor = jay_type_size_bits(I->type) / 16;
      jay_def tmp = jay_alloc_def(b, UGPR, 4 * factor);
      jay_MOV(b, I->dst, jay_extract(tmp, 0));
      I->dst = tmp;

      jay_foreach_src(I, s) {
         if (I->src[s].file == UGPR && jay_src_type(I, s) == JAY_TYPE_BF16) {
            uint32_t indices[4] = { jay_channel(I->src[s], 0), 0 };
            jay_replace_src(&I->src[s], jay_collect(b, UGPR, indices, 4));
         }
      }
   }
}

static void
jay_emit_alu(struct nir_to_jay_state *nj, nir_alu_instr *alu)
{
   jay_builder *b = &nj->bld;
   jay_def dst = nj_def(&alu->def);

   nir_alu_type nir_type = nir_op_infos[alu->op].output_type;
   enum jay_type base_type = jay_base_type_for_nir(nir_type);
   enum jay_type type = jay_type(base_type, alu->def.bit_size);

   jay_def src[NIR_ALU_MAX_INPUTS];
   for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
      unsigned len = nir_src_bit_size(alu->src[i].src) == 64 ? 2 : 1;
      src[i] = jay_extract_range(nj_src(alu->src[i].src),
                                 len * alu->src[i].swizzle[0], len);
   }

   switch (alu->op) {
#define CMP(op, cmod)                                                          \
   case nir_op_##op:                                                           \
      jay_CMP(b, jay_alu_source_type(alu, 0), GEN_CONDITION_##cmod, dst,       \
              src[0], src[1]);                                                 \
      break;

#define UNOP(nir, jay_op)                                                      \
   case nir_op_##nir:                                                          \
      jay_##jay_op(b, type, dst, src[0]);                                      \
      break;

#define MATH(nir, jay_op)                                                      \
   case nir_op_##nir:                                                          \
      jay_MATH(b, type, dst, src[0], JAY_MATH_##jay_op);                       \
      break;

#define UNOP_UNTYPED(nir, jay_op)                                              \
   case nir_op_##nir:                                                          \
      jay_##jay_op(b, dst, src[0]);                                            \
      break;

#define BINOP(nir, jay_op)                                                     \
   case nir_op_##nir:                                                          \
      jay_##jay_op(b, type, dst, src[0], src[1]);                              \
      break;

#define DP4A(nir, jay_op, sat_)                                                \
   case nir_op_##nir:                                                          \
      jay_DP4A_##jay_op(b, dst, src[2], src[0], src[1])->saturate = sat_;      \
      break;

      CMP(flt, LT)
      CMP(ilt, LT)
      CMP(ult, LT)
      CMP(fge, GE)
      CMP(ige, GE)
      CMP(uge, GE)
      CMP(feq, EQ)
      CMP(ieq, EQ)
      CMP(fneu, NE)
      CMP(ine, NE)

      MATH(frcp, INV)
      MATH(fexp2, EXP)
      MATH(flog2, LOG)
      MATH(fsin, SIN)
      MATH(fcos, COS)
      MATH(fsqrt, SQRT)
      MATH(frsq, RSQ)
      UNOP(ffract, FRC)
      UNOP(ftrunc, RNDZ)
      UNOP(ffloor, RNDD)
      UNOP(fround_even, RNDE)

      UNOP_UNTYPED(mov, copy)
      UNOP_UNTYPED(unpack_32_2x16_split_x, MOV)
      UNOP_UNTYPED(b2b1, CAST_CANONICAL_TO_FLAG)
      UNOP_UNTYPED(inot, NOT)
      UNOP_UNTYPED(bitfield_reverse, BFREV)
      UNOP_UNTYPED(bit_count, CBIT)
      UNOP_UNTYPED(uclz, LZD)
      UNOP_UNTYPED(find_lsb, FBL)

      BINOP(imin, MIN)
      BINOP(umin, MIN)
      BINOP(fmin, MIN)
      BINOP(imax, MAX)
      BINOP(umax, MAX)
      BINOP(fmax, MAX)
      BINOP(fadd, ADD)
      BINOP(iadd, ADD)
      BINOP(fmul, MUL)
      BINOP(imul_32x16, MUL_32X16)
      BINOP(umul_32x16, MUL_32X16)
      BINOP(ishl, SHL)
      BINOP(ishr, ASR)
      BINOP(ushr, SHR)
      BINOP(urol, ROL)
      BINOP(uror, ROR)
      BINOP(urhadd, AVG)
      BINOP(irhadd, AVG)
      BINOP(iand, AND)
      BINOP(ior, OR)
      BINOP(ixor, XOR)

      DP4A(sdot_4x8_iadd, SS, false)
      DP4A(sdot_4x8_iadd_sat, SS, true)
      DP4A(udot_4x8_uadd, UU, false)
      DP4A(udot_4x8_uadd_sat, UU, true)
      DP4A(sudot_4x8_iadd, SU, false)
      DP4A(sudot_4x8_iadd_sat, SU, true)

#undef CMP
#undef UNOP
#undef UNOP_UNTYPED
#undef BINOP
#undef DP4A

   case nir_op_imul:
      if (jay_type_size_bits(type) == 32) {
         jay_MUL_32(b, type, dst, src[0], src[1], false);
      } else {
         jay_MUL(b, type, dst, src[0], src[1]);
      }

      break;

   case nir_op_imul_high:
   case nir_op_umul_high:
      jay_MUL_32(b, type, dst, src[0], src[1], true);
      break;

   case nir_op_bfm:
      jay_BFI1(b, dst, src[0], src[1]);
      break;

   case nir_op_b2f64:
      jay_SEL(b, JAY_TYPE_U32, jay_extract(dst, 1), 0x3ff00000, 0, src[0]);
      jay_MOV(b, jay_extract(dst, 0), 0);
      break;

   case nir_op_ufind_msb_rev:
   case nir_op_ifind_msb_rev:
      jay_FBH(b, jay_alu_source_type(alu, 0), dst, src[0]);
      break;

   case nir_op_u2u8:
   case nir_op_u2u16:
   case nir_op_u2u32:
   case nir_op_i2i8:
   case nir_op_i2i16:
   case nir_op_i2i32:
      assert(nir_src_bit_size(alu->src[0].src) > 1 &&
             "predicate conversions are lowered");

      if (alu->def.bit_size <= nir_src_bit_size(alu->src[0].src)) {
         /* Downconversion. Upper bits garbage convention makes this a no-op.
          * The extract handles 64->32 narrowing conversions.
          */
         jay_MOV(b, dst, jay_extract(src[0], 0));
         break;
      }

      FALLTHROUGH;
   case nir_op_i2f64:
   case nir_op_i2i64:
   case nir_op_u2u64:
   case nir_op_u2f64:
   case nir_op_f2f64:
   case nir_op_f2i64:
   case nir_op_f2u64:
   case nir_op_f2i32:
   case nir_op_f2u32:
   case nir_op_f2i32_sat:
   case nir_op_f2u32_sat:
   case nir_op_i2f32:
   case nir_op_u2f32:
   case nir_op_f2f32:
   case nir_op_i2f16:
   case nir_op_u2f16:
   case nir_op_f2f16:
   case nir_op_f2i16:
   case nir_op_f2u16:
   case nir_op_f2i16_sat:
   case nir_op_f2u16_sat:
   case nir_op_f2i8:
   case nir_op_f2u8:
   case nir_op_f2i8_sat:
   case nir_op_f2u8_sat: {
      enum jay_type src_type = jay_alu_source_type(alu, 0);

      /* UGPR byte to float is not supported. Do it in 2 steps. */
      if (jay_type_size_bits(src_type) == 8 &&
          jay_base_type(type) == JAY_TYPE_F &&
          dst.file == UGPR) {

         enum jay_type integer = jay_type_rebase(type, jay_base_type(src_type));
         jay_def tmp = jay_alloc_def(b, UGPR, 1);
         jay_CVT(b, integer, tmp, src[0], src_type, JAY_ROUND, 0);
         jay_CVT(b, type, dst, tmp, integer, JAY_ROUND, 0);
      } else {
         jay_CVT(b, type, dst, src[0], src_type, JAY_ROUND, 0);
      }

      break;
   }

   case nir_op_f2f16_rtne:
   case nir_op_f2f16_rtz:
      jay_CVT(b, JAY_TYPE_F16, dst, src[0], jay_alu_source_type(alu, 0),
              alu->op == nir_op_f2f16_rtz ? JAY_RTZ : JAY_RNE, 0);
      break;

   case nir_op_f2bf:
      jay_CVT(b, JAY_TYPE_BF16, dst, src[0], JAY_TYPE_F32, JAY_RNE, 0);
      break;

   case nir_op_bf2f:
      lower_bf(b, jay_CVT(b, JAY_TYPE_F32, dst, src[0], JAY_TYPE_BF16, JAY_RNE,
                          0));
      break;

   /* See jay_src_type for type information.
    * This is a weird case with mixed types. 
    */
   case nir_op_bfmul_mixed_intel:
      lower_bf(b, jay_MUL(b, JAY_TYPE_BF16, dst, src[0], src[1]));
      break;
   case nir_op_bffma_mixed_intel:
      lower_bf(b, jay_MAD(b, JAY_TYPE_BF16, dst, src[2], src[1], src[0]));
      break;

   case nir_op_fsat:
      jay_MODIFIER(b, type, dst, src[0])->saturate = true;
      break;

   case nir_op_fneg:
   case nir_op_ineg:
      jay_MODIFIER(b, type, dst, jay_negate(src[0]));
      break;

   case nir_op_fabs:
   case nir_op_iabs:
      jay_MODIFIER(b, type, dst, jay_abs(src[0]));
      break;

   case nir_op_iadd3:
      jay_ADD3(b, type, dst, src[0], src[1], src[2]);
      break;

   case nir_op_uadd_sat:
   case nir_op_iadd_sat:
      jay_ADD(b, type, dst, src[0], src[1])->saturate = true;
      break;

   case nir_op_usub_sat:
   case nir_op_isub_sat:
      jay_ADD(b, type, dst, src[0], jay_negate(src[1]))->saturate = true;
      break;

   case nir_op_ihadd:
   case nir_op_uhadd: {
      /* AVG(x, y) - ((x ^ y) & 1) */
      jay_def avg = jay_alloc_def(b, dst.file, 1);
      jay_def bfn = jay_alloc_def(b, dst.file, 1);
      jay_AVG(b, type, avg, src[0], src[1]);
      jay_BFN(b, JAY_TYPE_U32, bfn, 1, src[0], src[1], UTIL_LUT3(a & (b ^ c)));
      jay_ADD(b, type, dst, avg, jay_negate(bfn));
      break;
   }

   case nir_op_unpack_64_2x32_split_x:
      jay_MOV(b, dst, jay_extract(src[0], 0));
      break;
   case nir_op_unpack_64_2x32_split_y:
      jay_MOV(b, dst, jay_extract(src[0], 1));
      break;
   case nir_op_unpack_32_2x16_split_y:
      jay_CVT(b, JAY_TYPE_U32, dst, src[0], JAY_TYPE_U16, JAY_ROUND, 1);
      break;

   case nir_op_pack_32_4x8_split: {
      /* TODO: Optimize */
      jay_def r = jay_BFI2_u32(b, 0x0000ff00, src[1], src[0]);
      r = jay_BFI2_u32(b, 0x00ff0000, src[2], r);
      jay_BFI2(b, dst, 0xff000000, src[3], r);
      break;
   }

   case nir_op_pack_32_2x16_split:
      if (nir_src_is_const(alu->src[0].src) &&
          nir_alu_src_as_uint(alu->src[0]) == 0) {

         /* pack_32_2x16_split(0, x) is just a shift. This saves a constant. */
         jay_SHL(b, JAY_TYPE_U32, dst, src[1], 16);
      } else {
         /* TODO: Optimize */
         jay_BFI2(b, dst, 0xffff0000, src[1], src[0]);
      }
      break;

   case nir_op_pack_64_2x32_split:
      jay_MOV(b, jay_extract(dst, 0), src[0]);
      jay_MOV(b, jay_extract(dst, 1), src[1]);
      break;

   case nir_op_bitfield_select:
      assert(jay_type_size_bits(type) <= 32);
      jay_BFN(b, JAY_TYPE_U32, dst, src[0], src[1], src[2],
              UTIL_LUT3((a & b) | (~a & c)));
      break;

   case nir_op_ubfe:
   case nir_op_ibfe:
      jay_BFE(b, type, dst, src[2], src[1], src[0]);
      break;
   case nir_op_bfi:
      jay_BFI2(b, dst, src[0], src[1], src[2]);
      break;

   case nir_op_ffma:
      jay_MAD(b, type, dst, src[2], src[1], src[0]);
      break;

   case nir_op_fcsel:
      jay_CSEL(b, type, dst, src[1], src[2], src[0])->conditional_mod =
         GEN_CONDITION_NE;
      break;

   case nir_op_fcsel_gt:
   case nir_op_i32csel_gt:
      jay_CSEL(b, type, dst, src[1], src[2], src[0])->conditional_mod =
         GEN_CONDITION_GT;
      break;

   case nir_op_fcsel_ge:
   case nir_op_i32csel_ge:
      jay_CSEL(b, type, dst, src[1], src[2], src[0])->conditional_mod =
         GEN_CONDITION_GE;
      break;

   case nir_op_bcsel:
      assert(alu->def.bit_size < 64);
      assert(jay_is_flag(src[0]));

      /* sel.s32 can propagate more modifiers than sel.u32 with no drawback */
      type = jay_type_rebase(type, JAY_TYPE_S);

      /* b2i8 gets lowered into 8-bit csel. Just use the upper bits garbage
       * convention to implement with SEL.u16 instead.
       */
      if (type == JAY_TYPE_S8) {
         type = JAY_TYPE_S16;
      }

      /* SEL.f32 flushes denorms but SEL.u32 does not, so we can only use the
       * float types when we are used only as a float. We care about the uses
       * and not the sources here, to ensure we pick u32 instead of f32 for:
       *
       *    ieq(1, bcsel(a, fneg(b), c))
       *
       * Picking sel.f32 would incorrectly "flush" the integer c. However, when
       * we can use sel.f32, we prefer it since it usually gives more
       * flexibility for modifiers and saturation.
       */
      if (is_only_used_as_float(alu)) {
         type = jay_type_rebase(type, JAY_TYPE_F);
      }

      jay_SEL(b, type, dst, src[1], src[2], src[0]);
      break;

   case nir_op_extract_u8:
      jay_CVT(b, JAY_TYPE_U32, dst, src[0], JAY_TYPE_U8, JAY_ROUND,
              nir_alu_src_as_uint(alu->src[1]));
      break;

   case nir_op_extract_i8:
      jay_CVT(b, JAY_TYPE_S32, dst, src[0], JAY_TYPE_S8, JAY_ROUND,
              nir_alu_src_as_uint(alu->src[1]));
      break;

   case nir_op_extract_u16:
      jay_CVT(b, JAY_TYPE_U32, dst, src[0], JAY_TYPE_U16, JAY_ROUND,
              nir_alu_src_as_uint(alu->src[1]));
      break;

   case nir_op_extract_i16:
      jay_CVT(b, JAY_TYPE_S32, dst, src[0], JAY_TYPE_S16, JAY_ROUND,
              nir_alu_src_as_uint(alu->src[1]));
      break;

   default:
      if (nir_op_is_vec(alu->op)) {
         for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
            unsigned len = jay_type_vector_length(type);
            jay_copy(b, jay_extract_range(dst, len * i, len), src[i]);
         }

         break;
      }

      nir_print_instr(&alu->instr, stderr);
      fprintf(stderr, "\n");
      UNREACHABLE("unhandled instruction");
   }
}

static void
jay_emit_load_const(struct nir_to_jay_state *nj, nir_load_const_instr *lc)
{
   jay_builder *b = &nj->bld;
   jay_def dst = nj_def(&lc->def);
   assert(lc->def.num_components == 1 && "must be scalarized");

   if (lc->def.bit_size == 64 && lc->value[0].u64 >> 32) {
      jay_MOV_IMM64(b, dst, lc->value[0].u64);
   } else {
      jay_MOV(b, dst, lc->value[0].u32);
   }
}

static jay_def
jay_resource_handle(jay_builder *b,
                    nir_src *nsrc,
                    unsigned *bti_const,
                    bool *internal,
                    bool *bindless)
{
   if (!nsrc) {
      return jay_null();
   }

   nir_intrinsic_instr *rin = nir_src_as_intrinsic(*nsrc);

   if (nir_src_is_const(*nsrc)) {
      *bti_const = nir_src_as_uint(*nsrc);
      return jay_null();
   } else if (!rin || rin->intrinsic != nir_intrinsic_resource_intel) {
      return nj_src(*nsrc);
   }

   uint32_t flags = nir_intrinsic_resource_access_intel(rin);
   if (internal) {
      *internal = !!(flags & nir_resource_intel_internal);
   }
   if (bindless) {
      *bindless = !!(flags & nir_resource_intel_bindless);
   }

   if (nir_src_is_const(rin->src[1])) {
      *bti_const = nir_src_as_uint(rin->src[1]);
      return jay_null();
   } else {
      return nj_src(rin->src[1]);
   }
}

static inline enum lsc_flush_type
translate_flush_type(nir_intrinsic_instr *intr)
{
   switch (nir_intrinsic_memory_semantics(intr)) {
   case NIR_MEMORY_ACQUIRE:
      return LSC_FLUSH_TYPE_INVALIDATE;
   case NIR_MEMORY_RELEASE:
      return LSC_FLUSH_TYPE_CLEAN;
   case NIR_MEMORY_ACQ_REL:
      return LSC_FLUSH_TYPE_EVICT;
   case NIR_MEMORY_MAKE_AVAILABLE:
   case NIR_MEMORY_MAKE_VISIBLE:
   default:
      UNREACHABLE("unexpected memory semantic");
   }
}

static void
emit_lsc_fence(struct nir_to_jay_state *nj,
               nir_intrinsic_instr *intr,
               enum gen_sfid sfid)
{
   bool device = nir_intrinsic_memory_scope(intr) >= SCOPE_QUEUE_FAMILY;
   enum lsc_fence_scope scope = device ? LSC_FENCE_TILE : LSC_FENCE_THREADGROUP;
   enum lsc_flush_type type =
      sfid == GEN_SFID_SLM ? LSC_FLUSH_TYPE_NONE : translate_flush_type(intr);

   jay_def notif = jay_alloc_def(&nj->bld, UGPR, jay_ugpr_per_grf(nj->s));
   uint32_t desc = lsc_fence_msg_desc(nj->s->devinfo, scope, type, false);

   jay_SEND(&nj->bld, .sfid = sfid, .msg_desc = desc, .srcs = &nj->payload.u0,
            .nr_srcs = 1, .type = JAY_TYPE_U32, .uniform = true, .dst = notif);
}

static void
jay_emit_memory_barrier(struct nir_to_jay_state *nj, nir_intrinsic_instr *intr)
{
   nir_variable_mode modes = nir_intrinsic_memory_modes(intr);

   if (modes & nir_var_image) {
      emit_lsc_fence(nj, intr, GEN_SFID_TGM);
      assert(!nj->nir->info.use_lowered_image_to_global && "fix common code");
   }

   if (modes & (nir_var_mem_ssbo | nir_var_mem_global)) {
      emit_lsc_fence(nj, intr, GEN_SFID_UGM);
   }

   if (modes & (nir_var_shader_out | nir_var_mem_task_payload)) {
      emit_lsc_fence(nj, intr, GEN_SFID_URB);
   }

   if ((modes & nir_var_mem_shared) &&
       !jay_workgroup_is_one_subgroup(&nj->bld, nj->nir)) {
      emit_lsc_fence(nj, intr, GEN_SFID_SLM);
   }
}

static void
jay_emit_signal_barrier(jay_builder *b, struct nir_to_jay_state *nj)
{
   /* Signal barrier / Active threads only (BSpec 72052).
    *
    * Source 0 is the number of subgroups in [31:24], which comes from the u0.2
    * payload in [31:24]. Mask out the other bits, then replicate to [23:15].
    *
    * TODO: This can be done faster with a SIMD2 8-bit move.
    */
   jay_def a = jay_AND_u32(b, jay_extract(nj->payload.u0, 2), 0xff000000);
   jay_def m2 = jay_OR_u32(b, a, jay_SHR_u32(b, a, 8));

   /* Use an active threads only barrier. TODO: I think we can optimize. */
   if (b->shader->devinfo->ver >= 20) {
      m2 = jay_OR_u32(b, m2, BITFIELD_BIT(8));
   }

   uint32_t indices[JAY_MAX_DEF_LENGTH] = { 0 };
   indices[2] = jay_index(m2);
   jay_def zipped = jay_collect(b, UGPR, indices, 3);

   jay_SEND(b, .sfid = GEN_SFID_MESSAGE_GATEWAY,
            .msg_desc = GEN_MESSAGE_GATEWAY_SFID_BARRIER_MSG, .srcs = &zipped,
            .nr_srcs = 1, .type = JAY_TYPE_U32, .uniform = true);
}

static void
jay_emit_derivative(jay_builder *b,
                    jay_def dst,
                    nir_intrinsic_instr *intr,
                    enum jay_quad_swizzle swz0,
                    enum jay_quad_swizzle swz1)
{
   assert(intr->def.bit_size == 32 && "todo");
   jay_def val = nj_src(intr->src[0]);

   jay_ADD(b, JAY_TYPE_F32, dst, jay_QUAD_SWIZZLE_u32(b, val, swz1),
           jay_negate(jay_QUAD_SWIZZLE_u32(b, val, swz0)));
}

static inline jay_def
optional_src(nir_src nsrc)
{
   return nir_src_is_undef(nsrc) ? jay_null() : nj_src(nsrc);
}

static bool
scalars_equal(nir_scalar a, nir_scalar b)
{
   return nir_scalar_equal(a, b) ||
          (nir_scalar_is_const(a) &&
           nir_scalar_is_const(b) &&
           nir_scalar_as_uint(a) == nir_scalar_as_uint(b));
}

static void
jay_emit_fb_write(struct nir_to_jay_state *nj, nir_intrinsic_instr *intr)
{
   jay_builder *b = &nj->bld;
   const struct intel_device_info *devinfo = b->shader->devinfo;
   jay_def colour = nj_src(intr->src[0]);
   jay_def dual_colour = jay_null();
   jay_def src0_alpha = optional_src(intr->src[2]);
   jay_def omask = optional_src(intr->src[3]);
   jay_def depth = optional_src(intr->src[4]);
   jay_def stencil = optional_src(intr->src[5]);
   const bool null_rt = ((signed) nir_intrinsic_target(intr)) < 0;
   const int target = MAX2(((signed) nir_intrinsic_target(intr)), 0);
   const bool last = !nir_instr_next(&intr->instr);
   const bool coarse = nj->s->prog_data->fs.coarse_pixel_dispatch;

   /* The hardware freaks out if we give it an omask without multisampling. */
   if (!b->shader->prog_data->fs.uses_omask) {
      omask = jay_null();
   }

   /* If our alpha happens to match src0_alpha, we can skip sending it,
    * as the hardware will use our alpha in that case.
    */
   if (scalars_equal(nir_scalar_resolved(intr->src[2].ssa, 0),
                     nir_scalar_resolved(intr->src[0].ssa, 3)))
      src0_alpha = jay_null();

   if (b->shader->prog_data->fs.dual_src_blend) {
      assert(b->shader->dispatch_width == 16);
      dual_colour = nj_src(intr->src[1]);
      src0_alpha = jay_null();
   }

   unsigned op = !jay_is_null(dual_colour) ?
                    XE2_DATAPORT_RENDER_TARGET_WRITE_SIMD16_DUAL_SOURCE :
                 b->shader->dispatch_width == 32 ?
                    XE2_DATAPORT_RENDER_TARGET_WRITE_SIMD32_SINGLE_SOURCE :
                    BRW_DATAPORT_RENDER_TARGET_WRITE_SIMD16_SINGLE_SOURCE;

   uint64_t desc = brw_fb_write_desc(devinfo, target, op, last, coarse);

   uint64_t ex_desc = (target << 21) |
                      (null_rt ? (1 << 20) : 0) |
                      (jay_is_null(src0_alpha) ? 0 : (1 << 15)) |
                      (jay_is_null(stencil) ? 0 : (1 << 14)) |
                      (jay_is_null(depth) ? 0 : (1 << 13)) |
                      (jay_is_null(omask) ? 0 : (1 << 12));

   assert((jay_is_null(src0_alpha) || jay_is_null(omask)) &&
          "TODO: lower alpha test to discards when samplemask is written");

   jay_def srcs[4 + 16 + 4 + 1 + 16];

   unsigned len = 0;
   int split = -1;

   if (!jay_is_null(src0_alpha))
      srcs[len++] = jay_as_gpr(b, src0_alpha);

   if (!jay_is_null(omask)) {
      jay_def packed = jay_alloc_def(b, UGPR, b->shader->dispatch_width / 2);
      jay_MOV(b, packed, omask)->type = JAY_TYPE_U16;

      for (unsigned i = 0; i < jay_num_values(packed); i++)
         srcs[len++] = jay_extract(packed, i);

      /* Split send after omask due to file difference */
      split = len;
   }

   for (unsigned i = 0; i < 4; i++)
      srcs[len++] = jay_as_gpr(b, jay_extract(colour, i));

   if (!jay_is_null(dual_colour)) {
      for (unsigned i = 0; i < 4; i++)
         srcs[len++] = jay_as_gpr(b, jay_extract(dual_colour, i));
   }

   if (!jay_is_null(depth))
      srcs[len++] = jay_as_gpr(b, depth);

   if (!jay_is_null(stencil)) {
      jay_def packed = jay_alloc_def(b, UGPR, b->shader->dispatch_width / 4);
      jay_MOV(b, packed, stencil)->type = JAY_TYPE_U8;

      /* Split send before stencil due to file difference */
      assert(split == -1 && "TODO: samplemask and stencil outputs together");
      split = len;

      for (unsigned i = 0; i < jay_num_values(packed); i++)
         srcs[len++] = jay_extract(packed, i);
   }

   jay_SEND(b, .sfid = GEN_SFID_RENDER_CACHE, .check_tdr = true,
            .msg_desc = desc | (ex_desc << 32), .srcs = srcs, .nr_srcs = len,
            .type = JAY_TYPE_U32, .eot = last, .split = split,
            .skip_helpers = true);
}

static enum lsc_data_size
lsc_bits_to_data_size(unsigned bit_size)
{
   /* clang-format off */
   switch (bit_size / 8) {
   case 1:  return LSC_DATA_SIZE_D8U32;
   case 2:  return LSC_DATA_SIZE_D16U32;
   case 4:  return LSC_DATA_SIZE_D32;
   case 8:  return LSC_DATA_SIZE_D64;
   default: UNREACHABLE("Unsupported data size.");
   }
   /* clang-format on */
}

static enum lsc_opcode
lsc_op_for_atomic(nir_atomic_op op)
{
   /* clang-format off */
   switch (op) {
   case nir_atomic_op_iadd:     return LSC_OP_ATOMIC_ADD;
   case nir_atomic_op_imin:     return LSC_OP_ATOMIC_MIN;
   case nir_atomic_op_umin:     return LSC_OP_ATOMIC_UMIN;
   case nir_atomic_op_imax:     return LSC_OP_ATOMIC_MAX;
   case nir_atomic_op_umax:     return LSC_OP_ATOMIC_UMAX;
   case nir_atomic_op_iand:     return LSC_OP_ATOMIC_AND;
   case nir_atomic_op_ior:      return LSC_OP_ATOMIC_OR;
   case nir_atomic_op_ixor:     return LSC_OP_ATOMIC_XOR;
   case nir_atomic_op_xchg:     return LSC_OP_ATOMIC_STORE;
   case nir_atomic_op_cmpxchg:  return LSC_OP_ATOMIC_CMPXCHG;
   case nir_atomic_op_fmin:     return LSC_OP_ATOMIC_FMIN;
   case nir_atomic_op_fmax:     return LSC_OP_ATOMIC_FMAX;
   case nir_atomic_op_fcmpxchg: return LSC_OP_ATOMIC_FCMPXCHG;
   case nir_atomic_op_fadd:     return LSC_OP_ATOMIC_FADD;
   default:                     UNREACHABLE("Unsupported NIR atomic");
   }
   /* clang-format on */
}

static jay_def
jay_src_as_strided(jay_builder *b,
                   jay_def x,
                   unsigned element_sz,
                   enum jay_file dst_file)
{
   if (jay_is_null(x)) {
      return x;
   } else if (dst_file == UGPR) {
      assert(jay_is_uniform(x) && "Uniform dests require uniform sources");

      if (x.file != UGPR) {
         jay_def tmp = jay_alloc_def(b, UGPR, jay_num_values(x));
         jay_copy(b, tmp, x);
         x = tmp;
      }

      uint32_t indices[JAY_MAX_DEF_LENGTH] = { 0 };
      unsigned nr = jay_num_values(x) * jay_ugpr_per_grf(b->shader);
      assert(nr < ARRAY_SIZE(indices));

      for (unsigned i = 0; i < jay_num_values(x) / element_sz; ++i) {
         for (unsigned j = 0; j < element_sz; ++j) {
            indices[(i * jay_ugpr_per_grf(b->shader)) + j] =
               jay_channel(x, (i * element_sz) + j);
         }
      }

      return jay_collect(b, UGPR, indices, nr);
   } else {
      /* Could be a GPR or UGPR source */
      assert(dst_file == GPR);
      return jay_as_gpr(b, x);
   }
}

static jay_def
jay_scratch_surface(struct nir_to_jay_state *nj)
{
   if (jay_is_null(nj->payload.scratch_surface)) {
      jay_function *func = nj->f;
      assert(func->is_entrypoint && "todo: this needs ABI");

      jay_builder b = jay_init_builder(func, jay_before_function(func));
      jay_def u0_5 = jay_extract(nj->payload.u0, 5);
      nj->payload.scratch_surface = jay_AND_u32(&b, u0_5, ~BITFIELD_MASK(10));
   }

   return nj->payload.scratch_surface;
}

static void
jay_emit_mem_access(struct nir_to_jay_state *nj, nir_intrinsic_instr *intr)
{
   jay_builder *b = &nj->bld;
   bool slm = nir_is_shared_access(intr);
   bool tgm = nir_intrinsic_has_image_dim(intr);
   bool urb = intr->intrinsic == nir_intrinsic_load_urb_lsc_intel ||
              intr->intrinsic == nir_intrinsic_load_urb_vec4_intel ||
              intr->intrinsic == nir_intrinsic_store_urb_lsc_intel ||
              intr->intrinsic == nir_intrinsic_store_urb_vec4_intel;
   enum gen_sfid sfid = slm ? GEN_SFID_SLM :
                        tgm ? GEN_SFID_TGM :
                        urb ? GEN_SFID_URB :
                              GEN_SFID_UGM;

   nir_src *data_src = nir_get_io_data_src(intr);
   bool scratch = intr->intrinsic == nir_intrinsic_load_scratch_intel ||
                  intr->intrinsic == nir_intrinsic_store_scratch_intel;

   enum lsc_opcode op;
   if (nir_intrinsic_has_atomic_op(intr))
      op = lsc_op_for_atomic(nir_intrinsic_atomic_op(intr));
   else if (sfid == GEN_SFID_TGM)
      op = data_src ? LSC_OP_STORE_CMASK : LSC_OP_LOAD_CMASK;
   else
      op = data_src ? LSC_OP_STORE : LSC_OP_LOAD;

   nir_src *bti = nir_get_io_index_src(intr), *ubo = NULL;
   nir_src *offset_src = tgm ? &intr->src[1] : nir_get_io_offset_src(intr);

   if (intr->intrinsic == nir_intrinsic_load_ubo ||
       intr->intrinsic == nir_intrinsic_load_ubo_uniform_block_intel) {
      ubo = bti;
      bti = NULL;
      b->shader->prog_data->base.has_ubo_pull = true;
   }

   const struct intel_device_info *devinfo = b->shader->devinfo;
   bool has_dest = nir_intrinsic_infos[intr->intrinsic].has_dest;
   jay_def data = data_src ? nj_src(*data_src) : jay_null();
   unsigned bti_const = 0;
   bool internal = false;
   bool bindless = false;
   jay_def bti_indirect =
      jay_resource_handle(b, bti ?: ubo, &bti_const, &internal, &bindless);
   jay_def offset = nj_src(*offset_src);
   nir_def *ndata = data_src ? data_src->ssa : &intr->def;
   jay_def dst = has_dest ? nj_def(&intr->def) : jay_null();
   int32_t base_offset =
      nir_intrinsic_has_base(intr) ? nir_intrinsic_base(intr) : 0;

   /* Optimize increment/decrement */
   if (op == LSC_OP_ATOMIC_ADD && nir_src_is_const(*data_src)) {
      int64_t add_val = nir_src_as_int(*data_src);
      if (add_val == 1 || add_val == -1) {
         op = add_val == 1 ? LSC_OP_ATOMIC_INC : LSC_OP_ATOMIC_DEC;
         data = jay_null();
      }
   }

   /* Pack the coordinates. TODO: MSAA */
   if (tgm) {
      unsigned nr = nir_image_intrinsic_coord_components(intr);
      offset = jay_extract_range(offset, 0, nr);
   }

   internal |= scratch;
   enum lsc_addr_surface_type surf_type = internal     ? LSC_ADDR_SURFTYPE_SS :
                                          bindless     ? LSC_ADDR_SURFTYPE_BSS :
                                          (bti || ubo) ? LSC_ADDR_SURFTYPE_BTI :
                                                         LSC_ADDR_SURFTYPE_FLAT;

   bool a64 = surf_type == LSC_ADDR_SURFTYPE_FLAT && sfid == GEN_SFID_UGM;
   enum lsc_addr_size addr_size = a64 ? LSC_ADDR_SIZE_A64 : LSC_ADDR_SIZE_A32;
   enum jay_type offset_type = a64 ? JAY_TYPE_U64 : JAY_TYPE_U32;

   bool cmask = op == LSC_OP_LOAD_CMASK || op == LSC_OP_STORE_CMASK;
   bool uniform = !(has_dest && dst.file != UGPR);

   if (nir_intrinsic_has_align(intr)) {
      assert(nir_intrinsic_align(intr) >= (ndata->bit_size / 8));
   }

   if (!has_dest) {
      uniform &= jay_is_null(data) || data.file == UGPR;
      uniform &= jay_is_null(offset) || offset.file == UGPR;
      uniform &= !urb;
   }

   /* Per bspec 57330, 8-bit/16-bit are not supported for transpose */
   bool transpose = uniform && !cmask && ndata->bit_size >= 32;

   if (!uniform) {
      offset = jay_as_gpr(b, offset);
      data = jay_as_gpr(b, data);
   } else if (!transpose) {
      offset = jay_src_as_strided(b, offset, a64 ? 2 : 1, UGPR);
      data = jay_src_as_strided(b, data, 1, UGPR);
   }

   unsigned access =
      nir_intrinsic_has_access(intr) ? nir_intrinsic_access(intr) : 0;

   bool volatile_access = access & ACCESS_VOLATILE;
   bool coherent_access = access & ACCESS_COHERENT;

   bool skip_helpers = data_src || (access & ACCESS_SKIP_HELPERS);
   skip_helpers &= !(access & ACCESS_INCLUDE_HELPERS);

   /* Bspec: Atomic instruction -> Cache section:
    *
    *    Atomic messages are always forced to "un-cacheable" in the L1
    *    cache.
    *
    * Bspec: Overview of memory Access:
    *
    *   If a read from a Null tile gets a cache-hit in a virtually-addressed
    *   GPU cache, then the read may not return zeroes.
    *
    * If a shader writes to a null tile and wants to be able to read it back
    * as zero, it will use the 'volatile' decoration for the access, otherwise
    * the compiler may choose to optimize things out, breaking the
    * residencyNonResidentStrict guarantees. Due to the above, we need to make
    * these operations uncached.
    */

   /* Skip L1 for coherent/volatile and URB access. */
   bool bypass_l1 = volatile_access || coherent_access || urb;

   /* Xe2 has a better L3 that can deal with null tiles. On older platforms,
    * all caches have to be bypassed for volatile.
    */
   bool bypass_l3 = volatile_access && (devinfo->ver < 20);

   /* Skip L3 for URB */
   bypass_l3 |= urb;

   /* Disable LSC data port L1 cache scheme for the TGM load/store for RT
    * shaders (see HSD 18038444588).
    */
   bypass_l1 |= devinfo->ver >= 20 && sfid == GEN_SFID_TGM &&
                mesa_shader_stage_is_rt(b->shader->stage);

   unsigned atomic_cache_mode = LSC_CACHE(devinfo, STORE, L1UC_L3WB);
   unsigned store_cache_mode = LSC_CACHE(devinfo, STORE, L1STATE_L3MOCS);
   unsigned load_cache_mode = LSC_CACHE(devinfo, LOAD, L1STATE_L3MOCS);

   if (bypass_l3) {
      store_cache_mode = LSC_CACHE(devinfo, STORE, L1UC_L3UC);
      load_cache_mode = LSC_CACHE(devinfo, LOAD, L1UC_L3UC);
   } else if (bypass_l1) {
      store_cache_mode = LSC_CACHE(devinfo, STORE, L1UC_L3WB);
      load_cache_mode = LSC_CACHE(devinfo, LOAD, L1UC_L3C);
   }

   unsigned cache =
      lsc_opcode_is_atomic(op) ? atomic_cache_mode :
      lsc_opcode_is_store(op) ? store_cache_mode :
      load_cache_mode;

   ASSERTED const unsigned max_imm_bits =
      brw_max_immediate_offset_bits(surf_type);
   assert(base_offset >= u_intN_min(max_imm_bits));
   assert(base_offset <= u_intN_max(max_imm_bits));
   assert(base_offset == 0 || sfid != GEN_SFID_TGM);

   unsigned nr = ndata->num_components;
   uint64_t desc =
      lsc_msg_desc(devinfo, op, surf_type, addr_size,
                   lsc_bits_to_data_size(ndata->bit_size),
                   cmask ? BITFIELD_MASK(nr) : nr, transpose, cache);

   /* Unlike most SENDs, we may skip the destination of atomics. We do this here
    * instead of DCE so we don't need to fix up message descriptors later.
    */
   if (nir_intrinsic_has_atomic_op(intr) && nir_def_is_unused(&intr->def)) {
      dst = jay_null();
   }

   jay_def tmp = dst;

   if (dst.file == UGPR) {
      if (transpose) {
         /* Transpose writes whole GRFs, so round up */
         tmp = jay_alloc_def(b, UGPR,
                             ALIGN_POT(jay_num_values(dst),
                                       jay_ugpr_per_grf(b->shader)));
      } else {
         /* Without transpose we write at GRF granularity. Pad out. */
         tmp = jay_alloc_def(b, UGPR,
                             jay_ugpr_per_grf(b->shader) * jay_num_values(dst));
      }
   }

   jay_def srcs[] = { offset, data };

   /* Second data source immediately follows the first */
   if (op == LSC_OP_ATOMIC_CMPXCHG || op == LSC_OP_ATOMIC_FCMPXCHG) {
      jay_def data2 = nj_src(*(data_src + 1));

      if (!transpose) {
         data2 = jay_as_gpr(b, data2);
      }

      srcs[1] = jay_collect_two(b, data, data2);
   }

   jay_def ex_desc = jay_null();
   uint32_t ex_desc_imm = 0;
   if (scratch) {
      /* TODO: Once we have an address register RA, we should CSE these */
      ex_desc = jay_alloc_def(b, J_ADDRESS, 1);
      jay_SHR(b, JAY_TYPE_U32, ex_desc, jay_scratch_surface(nj), 4);

      if (has_dest) {
         b->shader->fills++;
      } else {
         b->shader->spills++;
      }
   } else if (surf_type == LSC_ADDR_SURFTYPE_FLAT) {
      const gen_lsc_ex_desc gen_ex_desc = {
         .addr_type = surf_type,
         .flat.base_offset = base_offset,
      };
      desc |=
         ((uint64_t) gen_lsc_ex_desc_encode(devinfo, &gen_ex_desc, NULL) << 32);
   } else if (jay_is_null(bti_indirect)) {
      const gen_lsc_ex_desc gen_ex_desc = {
         .addr_type = LSC_ADDR_SURFTYPE_BTI,
         .bti = {
            .index = bti_const,
            .base_offset = base_offset,
         },
      };
      desc |=
         ((uint64_t) gen_lsc_ex_desc_encode(devinfo, &gen_ex_desc, NULL) << 32);
   } else if (!jay_is_null(bti_indirect)) {
      ex_desc = bti_indirect;

      if (surf_type == LSC_ADDR_SURFTYPE_SS ||
          surf_type == LSC_ADDR_SURFTYPE_BSS) {
         const gen_lsc_ex_desc gen_ex_desc = {
            .addr_type = surf_type,
            .surface_state = {
               .base_offset = base_offset,
            },
         };
         gen_lsc_ex_desc_encode(devinfo, &gen_ex_desc, &ex_desc_imm);
      } else {
         /* TODO: Move the SHL to NIR for CSE? */
         assert(surf_type == LSC_ADDR_SURFTYPE_BTI);
         assert(base_offset == 0);
         ex_desc = jay_SHL_u32(b, emit_uniformize(nj, bti_indirect), 24);
      }
   }

   enum jay_type data_type = jay_type(JAY_TYPE_U, MAX2(ndata->bit_size, 32));
   jay_SEND(b, .sfid = sfid, .msg_desc = desc, .srcs = srcs,
            .nr_srcs = jay_is_null(data) ? 1 : 2, .dst = tmp, .type = data_type,
            .src_type = { offset_type, data_type }, .uniform = uniform,
            .pure = nir_intrinsic_can_reorder(intr),
            .bindless = surf_type == LSC_ADDR_SURFTYPE_BSS, .ex_desc = ex_desc,
            .ex_desc_imm = ex_desc_imm, .skip_helpers = skip_helpers);

   if (has_dest && !jay_defs_equivalent(tmp, dst)) {
      jay_copy_strided(b, dst, tmp, !transpose);
   }
}

static void
jay_emit_barycentric(struct nir_to_jay_state *nj,
                     nir_intrinsic_instr *intr,
                     enum intel_barycentric_mode mode)
{
   assert(nj->s->stage == MESA_SHADER_FRAGMENT);
   enum glsl_interp_mode glsl_mode = nir_intrinsic_interp_mode(intr);

   if (glsl_mode == INTERP_MODE_NOPERSPECTIVE) {
      mode += INTEL_BARYCENTRIC_NONPERSPECTIVE_PIXEL;
   } else {
      assert(glsl_mode == INTERP_MODE_SMOOTH);
   }

   jay_copy(&nj->bld, nj_def(&intr->def), nj->payload.fs.bary[mode]);
}

static void
jay_emit_rt_lsc_fence(struct nir_to_jay_state *nj,
                      enum lsc_fence_scope scope,
                      enum lsc_flush_type type)
{
   jay_def notif = jay_alloc_def(&nj->bld, UGPR, jay_ugpr_per_grf(nj->s));
   uint32_t desc = lsc_fence_msg_desc(nj->s->devinfo, scope, type, true);

   jay_SEND(&nj->bld, .sfid = GEN_SFID_UGM, .msg_desc = desc,
            .srcs = &nj->payload.u0, .nr_srcs = 1, .type = JAY_TYPE_U32,
            .uniform = true, .dst = notif);

   /* There is no implicit ordering between messages to the dataport, the
    * thread sorting unit, and the raytracing accelerator. We need to manually
    * wait on the SBIDs of these fence messages to ensure all pending writes
    * have landed before sending messages to the BTD/RTA units.
    */
   jay_SCHEDULE_BARRIER(&nj->bld);
}

static uint32_t
build_rt_header_and_srcs(struct nir_to_jay_state *nj, nir_intrinsic_instr *instr,
                         jay_def *srcs, uint32_t *split_len)
{
   jay_shader *s = nj->s;
   jay_builder *b = &nj->bld;
   uint32_t len = 0;
   bool synchronous = false;

   /* Make sure all the previous RT structure writes are visible to the RT
    * fixed function within the DSS, as well as stack pointers to resume
    * shaders.
    */
   jay_emit_rt_lsc_fence(nj, LSC_FENCE_LOCAL, LSC_FLUSH_TYPE_NONE);

   /*
    * TODO: Look into efficient RA implications for moving all zeros
    */
   unsigned length = jay_ugpr_per_grf(nj->s);
   jay_def ugprs[JAY_MAX_DEF_LENGTH] = {};
   for (unsigned i = 0; i < length; ++i) {
      ugprs[i] = jay_MOV_u32(b, 0);
   }

   if (instr->intrinsic == nir_intrinsic_trace_ray_intel ||
       instr->intrinsic == nir_intrinsic_btd_spawn_intel) {
      synchronous = instr->intrinsic == nir_intrinsic_trace_ray_intel ?
                    nir_intrinsic_synchronous(instr) : false;
      jay_def globals = nj_src(instr->src[0]);
      assert(globals.file == UGPR);
      ugprs[0] = jay_extract(globals, 0);
      ugprs[1] = jay_extract(globals, 1);

      if (synchronous) {
         ugprs[4] = jay_MOV_u32(b, (unsigned) synchronous);
      }
   } else if (instr->intrinsic == nir_intrinsic_btd_retire_intel) {
      /* The bottom bit is the Stack ID release bit */
      ugprs[0] = jay_MOV_u32(b, 1);
      ugprs[1] = jay_MOV_u32(b, 0);
   }

   /* Header */
   srcs[len++] = jay_collect_vectors(b, ugprs, length);

   if (instr->intrinsic == nir_intrinsic_trace_ray_intel) {
      jay_def payload = jay_as_gpr(b, nj_src(instr->src[1]));

      if (!synchronous) {
         jay_def packed_stack_ids = jay_extract_range(nj->payload.u1, 0,
                                                      s->dispatch_width / 2);
         jay_def stack_id = jay_alloc_def(b, GPR, 1);
         jay_CVT(b, JAY_TYPE_U32, stack_id, packed_stack_ids,
                 JAY_TYPE_U16, JAY_ROUND, 0);

         payload = jay_BFI2_u32(b, s->devinfo->ver >= 20 ? 0x0fff0000 : 0x07ff0000,
                                stack_id, payload);
      }

      srcs[len++] = payload;
   } else if (instr->intrinsic == nir_intrinsic_btd_retire_intel ||
              instr->intrinsic == nir_intrinsic_btd_spawn_intel) {
      /* Bitgroup 1 - stackIDs, for SIMD16, we need 8 Dwords, each will contain
       * 16-bit stackID.
       */
      jay_def packed_stacks = jay_alloc_def(b, UGPR, s->dispatch_width/2);
      jay_def stack_id_packed = jay_extract_range(nj->payload.u1, 0, s->dispatch_width/2);
      jay_MOV(b, packed_stacks, stack_id_packed)->type = JAY_TYPE_U16;

      jay_def stacks[JAY_MAX_DEF_LENGTH] = {};
      for (unsigned i = 0; i < jay_num_values(packed_stacks); i++) {
         stacks[i] = jay_extract(packed_stacks, i);
      }

      jay_def stack_ids = jay_collect_vectors(b, stacks, jay_ugpr_per_grf(nj->s));
      srcs[len++] = stack_ids;

      if (instr->intrinsic == nir_intrinsic_btd_retire_intel) {
         jay_def btd_record_ugprs[JAY_MAX_DEF_LENGTH] = {};
         unsigned length = jay_ugpr_per_grf(nj->s);
         /* Things complain if we don't provide one for RETIRE. However, it shouldn't
          * ever actually get used so fill it with zero.
          */
         btd_record_ugprs[0] = jay_MOV_u32(b, 0);
         jay_def btd_record = jay_collect_vectors(b, btd_record_ugprs, length);

         srcs[len++] = btd_record;
         srcs[len++] = btd_record;
      } else if (instr->intrinsic == nir_intrinsic_btd_spawn_intel) {
         assert(split_len != NULL);
         *split_len = len;

         /* Bitgroup 2-3: shader record identifier, 64-bit per lane. */
         srcs[len++] = jay_as_gpr(b, nj_src(instr->src[1]));
      }
   }

   return len;
}

static bool
jay_shader_stage_uses_btd(jay_shader *s)
{
   return s->stage == MESA_SHADER_COMPUTE ? s->prog_data->cs.uses_btd_stack_ids :
                                            brw_shader_stage_is_bindless(s->stage);
}

static void
jay_emit_btd_ops(struct nir_to_jay_state *nj, nir_intrinsic_instr *instr)
{
   jay_shader *s = nj->s;
   const bool synchronous =
      instr->intrinsic == nir_intrinsic_trace_ray_intel ?
      nir_intrinsic_synchronous(instr) : false;

   assert(nj->s->dispatch_width <= 16 || synchronous);
   assert(nj->s->dispatch_width <= 16 && "TODO: Ray query SIMD splitting");

   uint32_t desc = 0;
   uint32_t split_len = 0;
   jay_def srcs[JAY_MAX_SRCS] = {};
   uint32_t nr_srcs = build_rt_header_and_srcs(nj, instr, srcs, &split_len);

   /* Bspec 57508, 47937: Structure_SIMD16TraceRayMessage:: RayQuery Enable
    *
    *    "When this bit is set in the header, Trace Ray Message behaves like
    *    a Ray Query. This message requires a write-back message indicating
    *    RayQuery for all valid Rays (SIMD lanes) have completed."
    */
   jay_def notif = synchronous ?
                      jay_alloc_def(&nj->bld, UGPR, jay_ugpr_per_grf(nj->s)) :
                      jay_null();

   switch (instr->intrinsic) {
   case nir_intrinsic_trace_ray_intel:
      desc = brw_rt_trace_ray_desc(s->devinfo, nj->s->dispatch_width);

      jay_SEND(&nj->bld, .sfid = GEN_SFID_RAY_TRACE_ACCELERATOR, .msg_desc = desc,
               .type = JAY_TYPE_U32, .srcs = srcs, .nr_srcs = nr_srcs, .dst = notif);
      break;

   case nir_intrinsic_btd_retire_intel:
   case nir_intrinsic_btd_spawn_intel:
      assert(jay_shader_stage_uses_btd(s));
      desc = brw_btd_spawn_desc(s->devinfo, nj->s->dispatch_width,
                                GEN_RT_BTD_MESSAGE_SPAWN);

      /* Set TYPE_U64 so that last two bitgroups (2-3) are interpreted as
       * expected, which is lower 8 channels of U64 of shader record identifier
       * and then next register with upper 8-channels.
       */
      jay_SEND(&nj->bld, .sfid = GEN_SFID_BINDLESS_THREAD_DISPATCH, .msg_desc = desc,
               .type = JAY_TYPE_U64, .srcs = srcs, .nr_srcs = nr_srcs, .dst = notif,
               .split = split_len);
      break;
   default:
      UNREACHABLE("Unknown intrinsic");
   }

   /* There is no implicit ordering between messages to the dataport and the
    * raytracing accelerator. We need to manually wait on the SBIDs of ray
    * queries first before the shader can load the result using the dataport.
    */
   if (synchronous) {
      assert(instr->intrinsic == nir_intrinsic_trace_ray_intel);
      jay_SCHEDULE_BARRIER(&nj->bld);
   }
}

static void
jay_emit_dpas(struct nir_to_jay_state *nj, nir_intrinsic_instr *intr)
{
   assert(mesa_shader_stage_uses_workgroup(nj->nir->info.stage));

   /* For Accumulator source we can use null register. */
   bool src0_use_null = true;
   for (unsigned c = 0; c < nir_src_num_components(intr->src[0]); c++) {
      nir_scalar val = nir_scalar_resolved(intr->src[0].ssa, c);
      src0_use_null &= nir_scalar_is_zero(val);
   }

   jay_builder *b = &nj->bld;
   jay_def dst = nj_def(&intr->def);
   jay_def src[3] = {
      src0_use_null ? jay_null() : jay_as_gpr(b, nj_src(intr->src[0])),
      jay_as_gpr(b, nj_src(intr->src[1])),
      jay_as_gpr(b, nj_src(intr->src[2])),
   };

   jay_DPAS(b, dst, src[0], src[1], src[2], nir_intrinsic_systolic_depth(intr),
            nir_intrinsic_repeat_count(intr),
            jay_type_for_glsl_base_type(nir_intrinsic_dest_base_type(intr)),
            jay_type_for_glsl_base_type(nir_intrinsic_src_base_type(intr)),
            /* sbid */ 0)
      ->saturate = nir_intrinsic_saturate(intr);

   nj->s->prog_data->cs.uses_systolic = true;
}

static void
jay_emit_convert_cmat(struct nir_to_jay_state *nj, nir_intrinsic_instr *intr)
{
   struct glsl_cmat_description dst_cmat_desc =
      nir_intrinsic_dst_cmat_desc(intr);
   struct glsl_cmat_description src_cmat_desc =
      nir_intrinsic_src_cmat_desc(intr);

   enum jay_type dst_type = jay_type_for_glsl_base_type(
      (enum glsl_base_type) dst_cmat_desc.element_type);
   enum jay_type src_type = jay_type_for_glsl_base_type(
      (enum glsl_base_type) src_cmat_desc.element_type);

   const unsigned dst_element_bits = jay_type_size_bits(dst_type);
   const unsigned src_element_bits = jay_type_size_bits(src_type);

   assert(dst_cmat_desc.use == src_cmat_desc.use);
   if (src_cmat_desc.use == GLSL_CMAT_USE_B)
      assert(dst_element_bits == src_element_bits);

   const unsigned dst_pf = 32 / dst_element_bits;
   const unsigned src_pf = 32 / src_element_bits;
   const unsigned elems = nir_src_num_components(intr->src[0]) * src_pf;

   jay_def dst = nj_def(&intr->def);
   jay_def src = nj_src(intr->src[0]);

   jay_builder *b = &nj->bld;

   jay_def src_tmp = src_pf > 1 ? jay_alloc_def(b, GPR, elems) : src;
   jay_def dst_tmp = dst_pf > 1 ? jay_alloc_def(b, GPR, elems) : dst;

   if (src_pf > 1) {
      for (unsigned i = 0; i < elems; i += src_pf) {
         jay_SLICE_REPACK(b, jay_extract_range(src_tmp, i, src_pf),
                          jay_extract(src, i / src_pf), util_logbase2(src_pf),
                          /* unpack */ true);
      }
   }

   if ((src_type == JAY_TYPE_BF16 && dst_type != JAY_TYPE_F32) ||
       (dst_type == JAY_TYPE_BF16 && src_type != JAY_TYPE_F32)) {
      jay_def tmp = jay_alloc_def(b, GPR, elems);
      for (unsigned i = 0; i < elems; ++i) {
         jay_CVT(b, JAY_TYPE_F32, jay_extract(tmp, i), jay_extract(src_tmp, i),
                 src_type, JAY_ROUND, 0);
      }
      src_tmp = tmp;
      src_type = JAY_TYPE_F32;
   }

   for (unsigned i = 0; i < elems; ++i) {
      jay_CVT(b, dst_type, jay_extract(dst_tmp, i), jay_extract(src_tmp, i),
              src_type, JAY_ROUND, 0);
   }

   if (dst_pf > 1) {
      for (unsigned i = 0; i < elems; i += dst_pf) {
         jay_SLICE_REPACK(b, jay_extract(dst, i / dst_pf),
                          jay_extract_range(dst_tmp, i, dst_pf),
                          util_logbase2(dst_pf), /* unpack */ false);
      }
   }
}

static void
load_push_data(struct nir_to_jay_state *nj,
               nir_intrinsic_instr *intr,
               jay_def *push_data)
{
   unsigned sz = intr->def.bit_size / 8;
   unsigned base_offset = nir_intrinsic_base(intr);
   jay_def dst = nj_def(&intr->def);
   jay_builder *b = &nj->bld;

   if (nir_src_is_const(intr->src[0])) {
      unsigned load_offset = nir_src_as_uint(intr->src[0]);
      unsigned offs = base_offset + load_offset;
      assert(util_is_aligned(load_offset, sz));

      if (sz >= 4) {
         jay_foreach_comp(dst, c) {
            jay_MOV(b, jay_extract(dst, c), push_data[(offs / 4) + c]);
         }
      } else {
         jay_foreach_comp(dst, c) {
            unsigned comp_offs = offs + c * sz;
            if (util_is_aligned(comp_offs, 4)) {
               jay_MOV(b, jay_extract(dst, c), push_data[comp_offs / 4]);
            } else {
               jay_CVT(b, JAY_TYPE_U32, jay_extract(dst, c),
                       push_data[comp_offs / 4],
                       JAY_TYPE_U | intr->def.bit_size, JAY_ROUND,
                       (comp_offs % 4) / sz);
            }
         }
      }
   } else {
      assert(sz < 8);
      unsigned range = DIV_ROUND_UP(nir_intrinsic_range(intr), 4);
      unsigned range_base = jay_base_index(push_data[base_offset / 4]);
      jay_def push_data = jay_contiguous_def(UGPR, range_base, range);
      jay_def offset = nj_src(intr->src[0]);
      if (!intr->def.divergent)
         offset = emit_uniformize(nj, offset);
      if (base_offset % 4)
         offset = jay_ADD_u32(b, offset, base_offset % 4);

      jay_VECTOR_EXTRACT(b, JAY_TYPE_U | intr->def.bit_size, dst, push_data,
                         offset);
   }
}

static void
jay_emit_intrinsic(struct nir_to_jay_state *nj, nir_intrinsic_instr *intr)
{
   jay_shader *s = nj->s;
   jay_function *f = nj->f;
   jay_builder *b = &nj->bld;
   jay_cs_payload *cs =
      mesa_shader_stage_is_compute(s->stage) ? &nj->payload.cs : NULL;
   jay_fs_payload *fs =
      s->stage == MESA_SHADER_FRAGMENT ? &nj->payload.fs : NULL;
   jay_tcs_payload *tcs =
      s->stage == MESA_SHADER_TESS_CTRL ? &nj->payload.tcs : NULL;
   jay_tes_payload *tes =
      s->stage == MESA_SHADER_TESS_EVAL ? &nj->payload.tes : NULL;

   const bool has_dest = nir_intrinsic_infos[intr->intrinsic].has_dest;
   jay_def dst = has_dest ? nj_def(&intr->def) : jay_null();

   switch (intr->intrinsic) {
   case nir_intrinsic_resource_intel:
      /* No code to generate here */
      break;

   case nir_intrinsic_global_atomic:
   case nir_intrinsic_global_atomic_swap:
   case nir_intrinsic_image_atomic:
   case nir_intrinsic_image_atomic_swap:
   case nir_intrinsic_image_load:
   case nir_intrinsic_image_store:
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_global_constant:
   case nir_intrinsic_load_global_constant_uniform_block_intel:
   case nir_intrinsic_load_global_intel:
   case nir_intrinsic_load_scratch_intel:
   case nir_intrinsic_load_shared:
   case nir_intrinsic_load_shared_uniform_block_intel:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_load_ssbo_intel:
   case nir_intrinsic_load_ssbo_uniform_block_intel:
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ubo_uniform_block_intel:
   case nir_intrinsic_load_urb_lsc_intel:
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap:
   case nir_intrinsic_ssbo_atomic:
   case nir_intrinsic_ssbo_atomic_swap:
   case nir_intrinsic_store_global:
   case nir_intrinsic_store_global_intel:
   case nir_intrinsic_store_urb_lsc_intel:
   case nir_intrinsic_store_scratch_intel:
   case nir_intrinsic_store_shared:
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_store_ssbo_intel:
   case nir_intrinsic_bindless_image_load:
   case nir_intrinsic_bindless_image_store:
   case nir_intrinsic_bindless_image_atomic:
   case nir_intrinsic_bindless_image_atomic_swap:
      jay_emit_mem_access(nj, intr);
      break;

   case nir_intrinsic_load_push_data_intel:
      load_push_data(nj, intr, nj->payload.push_data);
      break;

   case nir_intrinsic_load_inline_data_intel:
      assert(cs && f->is_entrypoint && "todo: this needs ABI");
      load_push_data(nj, intr, nj->payload.inline_data);
      break;

   case nir_intrinsic_load_fs_config_intel:
      s->prog_data->fs.uses_fs_config = true;
      jay_MOV(b, dst, fs->config);
      break;

   case nir_intrinsic_load_tess_config_intel: {
      const unsigned offs = tes ? s->prog_data->tes.tess_config_param :
                                  s->prog_data->tcs.tess_config_param;
      jay_MOV(b, dst, nj->payload.push_data[offs / 4]);
      break;
   }

   case nir_intrinsic_barrier: {
      jay_SCHEDULE_BARRIER(b);

      if (nir_intrinsic_memory_scope(intr) != SCOPE_NONE) {
         jay_emit_memory_barrier(nj, intr);
      }

      if (nir_intrinsic_execution_scope(intr) == SCOPE_WORKGROUP &&
          ((cs && !jay_workgroup_is_one_subgroup(b, nj->nir)) ||
           (tcs && s->prog_data->tcs.instances != 1))) {
         jay_emit_signal_barrier(b, nj);
         s->prog_data->cs.uses_barrier = true;
      }

      break;
   }

   case nir_intrinsic_begin_invocation_interlock:
   case nir_intrinsic_end_invocation_interlock:
      UNREACHABLE("TODO");

   case nir_intrinsic_load_reloc_const_intel:
      jay_RELOC(b, dst, nir_intrinsic_param_idx(intr),
                nir_intrinsic_base(intr));
      break;

   case nir_intrinsic_store_render_target_intel:
      assert(nj->nir->info.stage == MESA_SHADER_FRAGMENT);
      jay_emit_fb_write(nj, intr);
      break;

   case nir_intrinsic_shader_clock:
      /* We must access the timestamp register atomically, but 64-bit
       * instructions cannot read ARF. Instead use a 2x32-bit vectorized move.
       */
      assert(dst.file == UGPR && "required for vectorization");
      jay_MOV(b, dst, jay_contiguous_def(J_ARF, GEN_ARF_TIMESTAMP, 2))->type =
         JAY_TYPE_U32;
      break;

   case nir_intrinsic_load_coverage_mask_intel:
      jay_MOV(b, dst, fs->coverage_mask);
      break;

   case nir_intrinsic_load_subgroup_invocation: {
      jay_def lid = jay_alloc_def(b, UGPR, s->dispatch_width / 2);
      jay_LANE_ID_8(b, jay_extract_range(lid, 0, 4));

      for (unsigned i = 8; i < s->dispatch_width; i *= 2) {
         jay_ADD(b, JAY_TYPE_U16, jay_extract_range(lid, i / 2, i / 2),
                 jay_extract_range(lid, 0, i / 2), i);
      }

      /* TODO: Lower this in NIR? */
      jay_CVT(b, JAY_TYPE_U32, dst, lid, JAY_TYPE_U16, JAY_ROUND, 0);
      break;
   }

   case nir_intrinsic_demote:
      jay_DEMOTE_u32(b, jay_null(), jay_null());
      break;
   case nir_intrinsic_demote_if:
      jay_DEMOTE(b, JAY_TYPE_U1, nj_src(intr->src[0]), 0)->conditional_mod =
         GEN_CONDITION_NE;
      break;

   case nir_intrinsic_load_helper_invocation:
   case nir_intrinsic_is_helper_invocation:
      jay_HELPER_SEL(b, dst, 1, 0);
      break;

   case nir_intrinsic_ddx:
   case nir_intrinsic_ddx_coarse:
      jay_emit_derivative(b, dst, intr, JAY_QUAD_SWIZZLE_XXXX,
                          JAY_QUAD_SWIZZLE_YYYY);
      break;
   case nir_intrinsic_ddx_fine:
      jay_emit_derivative(b, dst, intr, JAY_QUAD_SWIZZLE_XXZZ,
                          JAY_QUAD_SWIZZLE_YYWW);
      break;

   case nir_intrinsic_ddy:
   case nir_intrinsic_ddy_coarse:
      jay_emit_derivative(b, dst, intr, JAY_QUAD_SWIZZLE_XXXX,
                          JAY_QUAD_SWIZZLE_ZZZZ);
      break;
   case nir_intrinsic_ddy_fine:
      jay_emit_derivative(b, dst, intr, JAY_QUAD_SWIZZLE_XYXY,
                          JAY_QUAD_SWIZZLE_ZWZW);
      break;

   case nir_intrinsic_first_invocation:
      jay_MOV(b, dst, emit_active_lane(nj));
      break;

   case nir_intrinsic_read_first_invocation:
      jay_MOV(b, dst, emit_uniformize(nj, nj_src(intr->src[0])));
      break;

   case nir_intrinsic_ballot:
   case nir_intrinsic_ballot_relaxed: {
      jay_def val = nj_src(intr->src[0]);
      if (nir_src_is_const(intr->src[0]) && nir_src_as_bool(intr->src[0])) {
         val = emit_active_lane_mask(nj);
      } else if (val.file == UFLAG) {
         /* Move to a FLAG temporary so we can ballot it. */
         val = jay_MOV(b, jay_alloc_def(b, FLAG, 1), val)->dst;
      } else {
         assert(val.file == FLAG);
      }

      assert(intr->def.bit_size == b->shader->dispatch_width);
      jay_MOV(b, dst, val);
      break;
   }

   /* We prefer to inverse_ballot by copying a UGPR to the flag. If we have a
    * GPR input, behaviour is undefined for non-uniform inputs. TODO: a lowered
    * bit extract is cheaper than uniformize, but maybe lower in NIR..?
    */
   case nir_intrinsic_inverse_ballot: {
      assert(dst.file == FLAG);
      jay_def x = nj_src(intr->src[0]);
      if (x.file == GPR) {
         x = emit_uniformize(nj, x);
      }

      jay_MOV(b, dst, x)->type = JAY_TYPE_U | b->shader->dispatch_width;
      break;
   }

   case nir_intrinsic_load_local_invocation_id:
      assert(cs);
      UNREACHABLE("todo: implement me from payload");
      jay_copy(b, dst, cs->local_invocation_ids);
      break;

   case nir_intrinsic_load_barycentric_pixel:
      jay_emit_barycentric(nj, intr, INTEL_BARYCENTRIC_PERSPECTIVE_PIXEL);
      break;

   case nir_intrinsic_load_barycentric_sample:
      jay_emit_barycentric(nj, intr, INTEL_BARYCENTRIC_PERSPECTIVE_SAMPLE);
      break;

   case nir_intrinsic_load_barycentric_centroid:
      jay_emit_barycentric(nj, intr, INTEL_BARYCENTRIC_PERSPECTIVE_CENTROID);
      break;

   case nir_intrinsic_load_frag_shading_rate_intel:
      jay_copy(b, dst,
               jay_collect_two(b,
                               jay_CVT_u32(b, jay_extract(nj->payload.u0, 8),
                                           JAY_TYPE_U8, JAY_ROUND, 0),
                               jay_CVT_u32(b, jay_extract(nj->payload.u0, 8),
                                           JAY_TYPE_U8, JAY_ROUND, 1)));
      break;

   case nir_intrinsic_load_pixel_coord_intel:
      jay_MOV(b, dst, fs->coord.xy);
      break;

   case nir_intrinsic_load_frag_coord_z:
      jay_MOV(b, dst, fs->coord.z);
      break;

   case nir_intrinsic_load_frag_coord_w_rcp:
      jay_MOV(b, dst, fs->coord.w);
      break;

   case nir_intrinsic_load_fs_start_intel:
      jay_copy(b, dst, jay_extract_range(fs->coefficients, 6, 2));
      break;

   case nir_intrinsic_load_fs_z_c_intel:
      jay_copy(b, dst,
               jay_collect_two(b, jay_extract(fs->coefficients, 9),
                               jay_extract(fs->coefficients, 8)));
      break;

   case nir_intrinsic_load_fs_z_c0_intel:
      jay_copy(b, dst, jay_extract(fs->coefficients, 10));
      break;

   case nir_intrinsic_load_sample_pos:
   case nir_intrinsic_load_sample_pos_or_center:
      assert(fs);
      jay_def gpr = jay_alloc_def(b, GPR, 1);
      jay_CVT(b, JAY_TYPE_U32, gpr, fs->sample_pos, JAY_TYPE_U16, JAY_ROUND, 0);

      jay_foreach_comp(dst, c) {
         /* We do this in two steps because regioning restrictions forbid
          * g14.1<4>:u8 as an operand to a float instruction.
          */
         if (c) {
            gpr = jay_CVT_u32(b, gpr, JAY_TYPE_U8, JAY_ROUND, 1);
         }

         jay_MUL(b, JAY_TYPE_F32, jay_extract(dst, c),
                 jay_CVT_f32(b, gpr, JAY_TYPE_U8, JAY_ROUND, 0),
                 jay_imm_f(1 / 16.0f));
      }
      break;

   case nir_intrinsic_load_tess_coord:
      assert(tes);
      jay_copy(b, dst, tes->tess_coord);
      break;

   case nir_intrinsic_load_primitive_id:
      if (tcs) {
         assert(nj->s->prog_data->tcs.include_primitive_id);
         jay_copy(b, dst, tcs->primitive_id);
      } else {
         assert(tes);
         assert(nj->s->prog_data->tes.include_primitive_id);
         jay_copy(b, dst, jay_extract(nj->payload.u0, 1));
      }
      break;

   case nir_intrinsic_load_invocation_id:
      assert(tcs);
      /* "Instance ID" from the thread payload */
      jay_CVT(b, JAY_TYPE_U32, dst, jay_extract(nj->payload.u0, 2), JAY_TYPE_U8,
              JAY_ROUND, 0);
      break;

   case nir_intrinsic_load_urb_input_handle_indexed_intel:
      assert(tcs);
      if (nir_src_is_const(intr->src[0])) {
         jay_copy(b, dst,
                  jay_extract(tcs->icp_handles, nir_src_as_uint(intr->src[0])));
      } else {
         jay_VECTOR_EXTRACT(b, JAY_TYPE_U32, dst, tcs->icp_handles,
                            jay_SHL_u32(b, nj_src(intr->src[0]), 6));
      }
      break;

   case nir_intrinsic_load_urb_input_handle_intel:
      assert(tes);
      jay_MOV(b, dst, jay_extract(nj->payload.u0, 0));
      break;

   case nir_intrinsic_load_urb_output_handle_intel:
      jay_MOV(b, dst, nj->payload.urb_handle);
      break;

   case nir_intrinsic_load_layer_id:
      jay_EXTRACT_SUBSPAN_INFO(b, dst, jay_extract(nj->payload.u0, 9),
                               payload_u1(nj, 9, 1), 0x7ff);
      break;

   case nir_intrinsic_load_front_face: {
      /* Bit 11 is facingness for subspans 1-2 and 5-6. */
      jay_inst *and =
         jay_EXTRACT_SUBSPAN_INFO(b, jay_null(), jay_extract(nj->payload.u0, 9),
                                  payload_u1(nj, 9, 1), BITFIELD_BIT(11));

      /* The bit is actually backfacingness so check for equality with 0 */
      jay_set_conditional_mod(b, and, dst, GEN_CONDITION_EQ);
      break;
   }

   /* Sample ID comes in as 4-bit numbers in g1.0:
    *
    *    15:12 Slot 3 SampleID
    *     11:8 Slot 2 SampleID
    *      7:4 Slot 1 SampleID
    *      3:0 Slot 0 SampleID
    *
    * Each slot corresponds to four channels, so we want to replicate each
    * half-byte value to 4 channels in a row:
    *
    *    dst+0:    .7    .6    .5    .4    .3    .2    .1    .0
    *             7:4   7:4   7:4   7:4   3:0   3:0   3:0   3:0
    *
    *    dst+1:    .7    .6    .5    .4    .3    .2    .1    .0
    *           15:12 15:12 15:12 15:12  11:8  11:8  11:8  11:8
    *
    * First, we read g1.0 with a <1,8,0>UB region, causing the first 8
    * channels to read the first byte (7:0), and the second group of 8
    * channels to read the second byte (15:8).  Then, we shift right by
    * a vector immediate of <4, 4, 4, 4, 0, 0, 0, 0>, moving the slot 1 / 3
    * values into place.  Finally, we AND with 0xf to keep the low nibble.
    *
    * According to the "PS Thread Payload for Normal Dispatch"
    * pages on the BSpec, the sample ids are stored in R0.8/R1.8
    * on gfx20+ and in R1.0/R2.0 on gfx8+.
    */
   case nir_intrinsic_load_sample_id: {
      jay_def x = jay_alloc_def(b, GPR, 1);
      jay_EXTRACT_BYTE_PER_8LANES(b, x, jay_extract(nj->payload.u0, 8),
                                  payload_u1(nj, 8, 1));
      jay_AND_U32_U16(b, dst, jay_SHR_ODD_SUBSPANS_BY_4_u16(b, x), 0xF);
      break;
   }

   case nir_intrinsic_load_attribute_payload_intel:
      assert(intr->def.bit_size == 32);

      if (s->stage == MESA_SHADER_TESS_EVAL) {
         assert(nir_src_is_const(intr->src[0]));
         unsigned offs = nir_src_as_uint(intr->src[0]) / 4;
         jay_copy(b, dst,
                  jay_collect_vectors(b, nj->payload.tes.patch_inputs + offs,
                                      intr->def.num_components));
      } else {
         UNREACHABLE("TODO: attribute payload data");
      }
      break;

   case nir_intrinsic_load_input:
      if (s->stage == MESA_SHADER_VERTEX) {
         unsigned offs = nir_intrinsic_base(intr) * 4;
         offs += nir_intrinsic_component(intr);
         assert(intr->def.bit_size == 32 && "todo");

         jay_copy(b, dst,
                  jay_collect_vectors(b, nj->payload.vs.attributes + offs,
                                      intr->def.num_components));
         break;
      }

      if (fs &&
          nir_intrinsic_io_semantics(intr).location == VARYING_SLOT_VIEWPORT) {
         jay_def x = jay_alloc_def(b, GPR, 1);
         jay_EXTRACT_SUBSPAN_INFO(b, x, jay_extract(nj->payload.u0, 9),
                                  payload_u1(nj, 9, 1), BITFIELD_RANGE(12, 4));
         jay_SHR(b, JAY_TYPE_U32, dst, x, 12);
         break;
      }

      FALLTHROUGH;
   case nir_intrinsic_load_fs_input_interp_deltas: {
      assert(s->stage == MESA_SHADER_FRAGMENT);
      unsigned location = nir_intrinsic_io_semantics(intr).location +
                          nir_src_as_uint(intr->src[0]);
      unsigned i = (s->prog_data->fs.urb_setup[location] * 4) +
                   nir_intrinsic_component(intr);
      assert(!jay_is_null(fs->deltas[i]));

      if (intr->intrinsic == nir_intrinsic_load_input) {
         assert(intr->def.num_components == 1 && "should be scalarized");
      }

      /* Zeroth delta is the flat value; .yz deltas are reversed from NIR */
      jay_MOV(b, jay_extract(dst, 0), jay_extract(fs->deltas[i], 0));
      if (intr->def.num_components > 1) {
         jay_MOV(b, jay_extract(dst, 1), jay_extract(fs->deltas[i], 2));
         jay_MOV(b, jay_extract(dst, 2), jay_extract(fs->deltas[i], 1));
      }
      break;
   }

   case nir_intrinsic_load_input_vertex: {
      assert(fs);
      unsigned location = nir_intrinsic_io_semantics(intr).location;
      unsigned i = (s->prog_data->fs.urb_setup[location] * 4) +
                   nir_intrinsic_component(intr);
      unsigned vtx = nir_src_as_uint(intr->src[0]);

      for (unsigned j = 0; j < intr->num_components; j++) {
         jay_copy(b, jay_extract(dst, j), jay_extract(fs->deltas[i + j], vtx));
      }
      break;
   }

   case nir_intrinsic_load_subgroup_size:
      jay_MOV(b, dst, s->dispatch_width);
      break;

   case nir_intrinsic_load_subgroup_id:
      assert(cs && f->is_entrypoint && "todo: this needs ABI");
      /* Subgroup ID in Thread Group is u0.2 bits 7:0 */
      jay_AND(b, JAY_TYPE_U32, dst, jay_extract(nj->payload.u0, 2), 0xFF);
      break;

   case nir_intrinsic_load_num_subgroups:
      assert(cs && f->is_entrypoint && "todo: this needs ABI");
      /* Number of subgroups in Thread Group is u0.2 bits 31:24 */
      jay_SHR(b, JAY_TYPE_U32, dst, jay_extract(nj->payload.u0, 2), 24);
      break;

   case nir_intrinsic_load_workgroup_id:
      assert(cs && f->is_entrypoint && "todo: this needs ABI");
      jay_MOV(b, jay_extract(dst, 0), jay_extract(nj->payload.u0, 1));
      jay_MOV(b, jay_extract(dst, 1), jay_extract(nj->payload.u0, 6));
      jay_MOV(b, jay_extract(dst, 2), jay_extract(nj->payload.u0, 7));
      break;

   case nir_intrinsic_shuffle_intel: {
      jay_def data = nj_src(intr->src[0]);

      if (nir_src_is_const(intr->src[1])) {
         /* Broadcast takes a lane index, with only 32-bit registers */
         jay_BROADCAST_IMM(b, dst, data, nir_src_as_uint(intr->src[1]) / 4);
      } else {
         /* Shuffle takes a byte index */
         jay_SHUFFLE(b, dst, data, nj_src(intr->src[1]));
      }

      break;
   }

   case nir_intrinsic_quad_broadcast:
      jay_QUAD_SWIZZLE(b, dst, nj_src(intr->src[0]),
                       JAY_QUAD_SWIZZLE_XXXX + nir_src_as_uint(intr->src[1]));
      break;

   case nir_intrinsic_load_topology_id_intel:
      jay_MOV(b, dst, jay_scalar(J_ARF, GEN_ARF_STATE));
      break;

   case nir_intrinsic_trace_ray_intel:
   case nir_intrinsic_btd_retire_intel:
   case nir_intrinsic_btd_spawn_intel:
      jay_emit_btd_ops(nj, intr);
      break;

   case nir_intrinsic_load_btd_stack_id_intel: {
      assert(jay_shader_stage_uses_btd(s));
      /* Stack IDs are always in R1 regardless of whether we're coming from a
       * bindless shader or a regular compute shader.
       */
      jay_def packed_stack_ids = jay_extract_range(nj->payload.u1, 0,
                                                   s->dispatch_width / 2);
      jay_CVT(b, JAY_TYPE_U32, dst, packed_stack_ids, JAY_TYPE_U16, JAY_ROUND, 0);
      break;
   }

   case nir_intrinsic_load_btd_global_arg_addr_intel:
      jay_MOV(b, dst, jay_collect_vectors(b, &nj->payload.inline_data[0], 2));
      break;

   case nir_intrinsic_load_btd_local_arg_addr_intel:
      jay_MOV(b, dst, jay_collect_vectors(b, &nj->payload.inline_data[2], 2));
      break;

   case nir_intrinsic_dpas_intel:
      jay_emit_dpas(nj, intr);
      break;

   case nir_intrinsic_convert_cmat_intel:
      jay_emit_convert_cmat(nj, intr);
      break;

   default:
#ifndef NDEBUG
      assert(intr->intrinsic < nir_num_intrinsics);
      fprintf(stdout, "intrinsic: %s\n",
              nir_intrinsic_infos[intr->intrinsic].name);
#endif
      UNREACHABLE("unknown intrinsic");
   }
}

static bool
sampler_needs_header(enum brw_sampler_opcode op,
                     nir_texop nir_op,
                     const struct intel_device_info *devinfo)
{
   switch (op) {
   case BRW_SAMPLER_OPCODE_SAMPLEINFO:
      return true;
   case BRW_SAMPLER_OPCODE_LD:
   case BRW_SAMPLER_OPCODE_LD_LZ:
      /* Xe3 HW does not seem to work unless we force a header. */
      return devinfo->ver >= 30;
   default:
      return nir_op == nir_texop_tg4;
   }
}

static void
jay_emit_texture(struct nir_to_jay_state *nj, nir_tex_instr *tex)
{
   /* SKL PRMs: Volume 7: 3D-Media-GPGPU:
    *
    *    "The Pixel Null Mask field, when enabled via the Pixel Null Mask
    *     Enable will be incorect for sample_c when applied to a surface with
    *     64-bit per texel format such as R16G16BA16_UNORM. Pixel Null mask
    *     Enable may incorrectly report pixels as referencing a Null surface."
    *
    * We'll take care of this in NIR.
    */
   assert(!tex->is_sparse ||
          nir_tex_instr_src_index(tex, nir_tex_src_comparator) == -1);

   jay_builder *b = &nj->bld;
   jay_def dst = nj_def(&tex->def);
   jay_def tmp = dst;

   const enum brw_sampler_opcode op = (enum brw_sampler_opcode)(
      tex->backend_flags & ~BRW_TEX_INSTR_FUSED_EU_DISABLE);
   const struct brw_sampler_payload_desc *payload_desc =
      brw_get_sampler_payload_desc(op);

   /* First deal with surface & sampler */
   unsigned payload_type_bit_size = 0;
   bool surface_bindless = false;
   bool sampler_bindless = false;
   jay_def surface, sampler, packed_offsets = jay_null();
   jay_def payload[JAY_MAX_SAMPLER_MESSAGE_SIZE];
   int i;
   if ((i = nir_tex_instr_src_index(tex, nir_tex_src_texture_handle)) >= 0) {
      unsigned x;
      surface =
         jay_resource_handle(b, &tex->src[i].src, &x, NULL, &surface_bindless);
      if (jay_is_null(surface))
         surface = jay_imm(x);
      assert(tex->texture_index == 0);
   } else if ((i = nir_tex_instr_src_index(tex, nir_tex_src_texture_offset)) >=
              0) {
      unsigned x;
      surface =
         jay_resource_handle(b, &tex->src[i].src, &x, NULL, &surface_bindless);
      if (jay_is_null(surface))
         surface = jay_imm(x + tex->texture_index);
      else if (tex->texture_index)
         surface = jay_ADD_u32(b, surface, tex->texture_index);
   } else {
      surface = jay_imm(tex->texture_index);
   }

   if ((i = nir_tex_instr_src_index(tex, nir_tex_src_sampler_handle)) >= 0) {
      unsigned x;
      sampler =
         jay_resource_handle(b, &tex->src[i].src, &x, NULL, &sampler_bindless);
      if (jay_is_null(sampler))
         surface = jay_imm(x);
      assert(tex->sampler_index == 0);
   } else if ((i = nir_tex_instr_src_index(tex, nir_tex_src_sampler_offset)) >=
              0) {
      unsigned x;
      sampler =
         jay_resource_handle(b, &tex->src[i].src, &x, NULL, &sampler_bindless);
      if (jay_is_null(sampler))
         sampler = jay_imm(x + tex->sampler_index);
      else
         sampler = jay_ADD_u32(b, sampler, tex->sampler_index);
   } else {
      sampler = jay_imm(tex->sampler_index);
   }

   surface = emit_uniformize(nj, surface);
   sampler = emit_uniformize(nj, sampler);

   /* Now the sampler payload */
   bool has_offset_in_payload = false;
   bool payload_uniform = true;
   uint32_t n_sources = TEX_LOGICAL_SRC_PAYLOAD0;
   for (uint32_t i = 0;
        payload_desc->sources[i].param != BRW_SAMPLER_PAYLOAD_PARAM_INVALID;
        i++) {
      nir_tex_src_type nir_source;
      unsigned nir_comp;

#define P(name) BRW_SAMPLER_PAYLOAD_PARAM_##name
#define S(name, component)                                                     \
   do {                                                                        \
      nir_source = nir_tex_src_##name;                                         \
      nir_comp = component;                                                    \
   } while (0)

      struct brw_sampler_payload_src sampler_src = payload_desc->sources[i];

      switch (sampler_src.param) {
      case P(U):
         S(coord, 0);
         break;
      case P(V):
         S(coord, 1);
         break;
      case P(R):
         S(coord, 2);
         break;
      case P(AI):
         S(coord, 3);
         break;
      case P(BIAS):
         S(bias, 0);
         break;
      case P(LOD):
         S(lod, 0);
         break;
      case P(MLOD):
         S(min_lod, 0);
         break;
      case P(REF):
         S(comparator, 0);
         break;
      case P(DUDX):
         S(ddx, 0);
         break;
      case P(DUDY):
         S(ddy, 0);
         break;
      case P(DVDX):
         S(ddx, 1);
         break;
      case P(DVDY):
         S(ddy, 1);
         break;
      case P(DRDX):
         S(ddx, 2);
         break;
      case P(DRDY):
         S(ddy, 2);
         break;
      case P(SI):
         S(ms_index, 0);
         break;
      case P(MCSL):
         S(ms_mcs_intel, 0);
         break;
      case P(MCSH):
         S(ms_mcs_intel, 1);
         break;
      case P(MCS0):
         S(ms_mcs_intel, 0);
         break;
      case P(MCS1):
         S(ms_mcs_intel, 1);
         break;
      case P(MCS2):
         S(ms_mcs_intel, 2);
         break;
      case P(MCS3):
         S(ms_mcs_intel, 3);
         break;

      case P(OFFU):
         S(offset, 0);
         has_offset_in_payload = true;
         break;
      case P(OFFV):
         S(offset, 1);
         has_offset_in_payload = true;
         break;
      case P(OFFUV4):
      case P(OFFUVR4):
      case P(OFFUV6):
      case P(OFFUVR6):
      case P(BIAS_OFFUV6):
      case P(BIAS_OFFUVR4):
      case P(LOD_OFFUV6):
      case P(LOD_OFFUVR4):
      case P(OFFUV4_R):
      case P(OFFUV6_R):
      case P(OFFUVR4_R):
         /* There is no payload with 2 packed entries, so backend1 is always
          * the one payload parameter packed. */
         S(backend1, 0);
         has_offset_in_payload = true;
         break;

      case P(BIAS_AI):
      case P(LOD_AI):
      case P(MLOD_R):
         /* There is no payload with 2 packed entries, so backend1 is always
          * the one payload parameter packed. */
         S(backend1, 0);
         break;

      default:
         UNREACHABLE("unhandled sampler param");
      }

#undef P
#undef S

      jay_def param_val = jay_null();

      int j = nir_tex_instr_src_index(tex, nir_source);
      if (j >= 0 && nir_comp < tex->src[j].src.ssa->num_components) {
         param_val = jay_extract(nj_src(tex->src[j].src), nir_comp);

         unsigned bitsize = nir_src_bit_size(tex->src[j].src);
         assert(payload_type_bit_size == 0 || payload_type_bit_size == bitsize);
         payload_type_bit_size = bitsize;
      }

      /* The hardware requires a LOD for buffer textures */
      if (tex->sampler_dim == GLSL_SAMPLER_DIM_BUF &&
          sampler_src.param == BRW_SAMPLER_PAYLOAD_PARAM_LOD) {
         sampler_src.optional = false;
      }

      /* Wa_14012688258:
       *
       * Don't trim zeros at the end of payload for sample operations
       * in cube and cube arrays.
       *
       * Compiler should send U,V,R parameters even if V,R are 0.
       */
      if (tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE &&
          intel_needs_workaround(nj->devinfo, 14012688258) &&
          (sampler_src.param == BRW_SAMPLER_PAYLOAD_PARAM_U ||
           sampler_src.param == BRW_SAMPLER_PAYLOAD_PARAM_V ||
           sampler_src.param == BRW_SAMPLER_PAYLOAD_PARAM_R)) {
         sampler_src.optional = false;
      }

      /* The last source present in the payload dictates the number of
       * sources, unless it's required.
       *
       * We can skip the last source if it's zero.
       */
      if (!sampler_src.optional || !jay_is_null(param_val))
         n_sources = i + 1;

      if (jay_is_null(param_val)) {
         param_val = jay_alloc_def(b, dst.file, 1);
         jay_MOV(b, param_val, 0);
      }

      payload[i] = param_val;
      payload_uniform &= jay_is_uniform(payload[i]);
   }

   i = nir_tex_instr_src_index(tex, nir_tex_src_backend2);
   if (i >= 0) {
      packed_offsets = nj_src(tex->src[i].src);
   }

   /* Xe2+ should never used packed offsets since it has enough opcodes to
    * handle any programmable offset.
    */
   assert(jay_is_null(packed_offsets) || nj->devinfo->ver < 20);

   /* If the NIR instruction has an offset param but the sampler payload
    * doesn't, we can put the offset into the header of the message.
    *
    * The restriction though is that it should be a constant value.
    */
   int offs_idx = nir_tex_instr_src_index(tex, nir_tex_src_offset);
   bool has_const_offsets = offs_idx != -1 && !has_offset_in_payload;

   bool is_high_sampler = !jay_is_imm(sampler) || jay_as_uint(sampler) >= 16;
   bool residency = tex->is_sparse;
   unsigned null_mask_component = 0;

   const bool needs_header = sampler_needs_header(op, tex->op, nj->devinfo) ||
                             has_const_offsets ||
                             !jay_is_null(packed_offsets) ||
                             sampler_bindless ||
                             is_high_sampler ||
                             residency;

   uint8_t component_mask;
   if (tex->op == nir_texop_tg4) {
      component_mask = WRITEMASK_XYZW;
   } else if (residency) {
      /* intel_nir_lower_sparse guarantees that texturing operations only
       * read the data, or the sparse residency code, but not both at once.
       *
       * We need to use UGPRs for the residency result because the sampler
       * returns the null pixel mask in lane 0, regardless of lanemasking.
       *
       * Unfortunately, the sampler doesn't allow us to writemask out all
       * four colour channels, so we have to needlessly return red.  This
       * isn't uniform data, but we store it in an array of UGPRs anyway
       * in order to have a consistent def file.  The colour data will be
       * immediately dead anyway.
       */
      assert(tex->op == nir_texop_sparse_residency_intel ||
             tex->op == nir_texop_sparse_residency_txf_intel);
      assert(nir_def_components_read(&tex->def) == WRITEMASK_Y);
      component_mask = WRITEMASK_X;
      unsigned red_grfs = payload_uniform ? 1 : jay_grf_per_gpr(b->shader);
      unsigned grfs = red_grfs + 1;
      tmp = jay_alloc_def(b, UGPR, grfs * jay_ugpr_per_grf(b->shader));
      null_mask_component = red_grfs * jay_ugpr_per_grf(b->shader);
   } else {
      component_mask = nir_def_components_read(&tex->def);

      /* We can reduce the return length of the message to drop unused
       * trailing components, but shrinking with a discontiguous mask
       * requires a message header.  We only do that if we need a header
       * for other reasons, as it's more expensive than writing extra data.
       */
      if (!needs_header) {
         component_mask =
            (uint8_t) BITFIELD_MASK(util_last_bit(component_mask));
      }

      /* TODO: Shrink 16-bit textures too. Shrinking is problematic for some
       * component masks due to 32-bit granularity of ISA registers.
       */
      if (tex->def.bit_size != 32 || (jay_debug & JAY_DBG_NOOPT))
         component_mask = nir_component_mask(tex->def.num_components);

      /* If we shrunk the destination, we need a temporary */
      if (component_mask != BITFIELD_MASK(tex->def.num_components)) {
         tmp = jay_alloc_def(b, GPR, util_bitcount(component_mask));
      }
   }

   /* SENDs always write entire GRFs so we need to pad out for uniform dests */
   if (dst.file == UGPR && !residency) {
      unsigned nr = jay_ugpr_per_grf(b->shader) * jay_num_values(tmp);
      tmp = jay_alloc_def(b, UGPR, nr);
   }

   if (tex->op == nir_texop_texture_samples) {
      assert(needs_header);
      payload_type_bit_size = 32;
      n_sources = 0;
   }

   jay_def header = jay_null();
   if (needs_header) {
      uint32_t header2;
      if (tex->op == nir_texop_tg4) {
         /* Gathers have a component but no write mask */
         header2 = (tex->component << 16);
      } else {
         /* If present, the header write mask are inverted compared to NIR */
         header2 = (~component_mask & 0xf) << 12;
      }

      if (residency)
         header2 |= 1 << 23; /* g0.2 bit 23: Pixel Null Mask Enable */

      if (has_const_offsets) {
         const unsigned num_components = nir_tex_instr_src_size(tex, offs_idx);
         for (unsigned i = 0; i < num_components; i++) {
            nir_scalar s = nir_get_scalar(tex->src[offs_idx].src.ssa, i);
            s = nir_scalar_chase_movs(s);
            assert(nir_scalar_is_const(s));
            int offset = nir_scalar_as_int(s);

            /* Offsets are 4-bits, reversed order */
            header2 |= (offset & 0xf) << ((2 - i) * 4);
         }
      }

      jay_def header_builder[16] = { [2] = jay_imm(header2) };

      if (sampler_bindless) {
         /* Bindless sampler handles aren't relative to the sampler state
          * pointer passed into the shader through SAMPLER_STATE_POINTERS_*.
          * Instead, it's an absolute pointer relative to dynamic state base
          * address.
          *
          * Sampler states are 16 bytes each and the pointer we give here has
          * to be 32-byte aligned.  In order to avoid more indirect messages
          * than required, we assume that all bindless sampler states are
          * 32-byte aligned.  This sacrifices a bit of general state base
          * address space but means we can do something more efficient in the
          * shader.
          */
         header_builder[3] = sampler;
      } else {
         /* Select the default dynamic state base address + offset */
         jay_def sampler_ptr = nj->payload.sampler_state_pointer;

         /* Gfx11+ sampler message headers include bits in 4:0 which conflict
          * with the ones included in g0.3 bits 4:0.  Mask them out.
          */
         if (b->shader->devinfo->ver >= 11) {
            sampler_ptr = jay_AND_u32(b, sampler_ptr, INTEL_MASK(31, 5));
         }

         /* TODO: We should probably lower this in NIR. */
         if (is_high_sampler) {
            if (jay_is_imm(sampler)) {
               unsigned s = jay_as_uint(sampler);
               const int sampler_state_size_B = 16;
               unsigned offs_B = ROUND_DOWN_TO(s, 16) * sampler_state_size_B;
               assert(offs_B > 0 && "since s > 0");
               sampler_ptr = jay_ADD_u32(b, sampler_ptr, offs_B);
            } else {
               jay_def offs_B =
                  jay_SHL_u32(b, jay_AND_u32(b, sampler, 0xf0), 4);
               sampler_ptr = jay_ADD_u32(b, sampler_ptr, offs_B);
            }
         }

         header_builder[3] = sampler_ptr;
      }

      header = build_msg_header(nj, header_builder);
   }

   assert(payload_type_bit_size == 16 || payload_type_bit_size == 32);
   unsigned simd_mode = 0;
   unsigned simd_width = payload_uniform ? 1 : nj->s->dispatch_width;
   if (nj->devinfo->ver < 20) {
      if (payload_type_bit_size == 16) {
         assert(nj->devinfo->ver >= 11);
         simd_mode = simd_width <= 8 ? GEN_GFX11_SAMPLER_SIMD_MODE_SIMD8H :
                                       GEN_GFX11_SAMPLER_SIMD_MODE_SIMD16H;
      } else {
         simd_mode = simd_width <= 8 ? GEN_SAMPLER_SIMD_MODE_SIMD8 :
                                       GEN_SAMPLER_SIMD_MODE_SIMD16;
      }
   } else {
      if (payload_type_bit_size == 16) {
         simd_mode = simd_width <= 16 ? GEN_XE2_SAMPLER_SIMD_MODE_SIMD16H :
                                        GEN_XE2_SAMPLER_SIMD_MODE_SIMD32H;
      } else {
         simd_mode = simd_width <= 16 ? GEN_XE2_SAMPLER_SIMD_MODE_SIMD16 :
                                        GEN_XE2_SAMPLER_SIMD_MODE_SIMD32;
      }
   }

   uint64_t desc = 0;
   jay_def desc_src = jay_null(), desc_ex_src = jay_null();

   unsigned sampler_imm = 0;
   if (jay_is_imm(sampler) && !sampler_bindless) {
      sampler_imm = jay_as_uint(sampler) % 16;
   }

   const unsigned msg_type = brw_get_sampler_hw_opcode(op);
   bool is_16 = false; /* TODO */
   unsigned ret_type = is_16 ? GFX8_SAMPLER_RETURN_FORMAT_16BITS :
                               GFX8_SAMPLER_RETURN_FORMAT_32BITS;

   if (!surface_bindless &&
       jay_is_imm(surface) &&
       (jay_is_imm(sampler) || sampler_bindless)) {
      desc = brw_sampler_desc(nj->devinfo, jay_as_uint(surface), sampler_imm,
                              msg_type, simd_mode, ret_type);
   } else if (surface_bindless) {
      /* Bindless surface */
      desc = brw_sampler_desc(nj->devinfo, GEN_BTI_BINDLESS, sampler_imm,
                              msg_type, simd_mode, ret_type);

      /* For bindless samplers, the entire address is included in the message
       * header so we can leave the portion in the message descriptor 0.
       */
      if (!sampler_bindless && !jay_is_imm(sampler)) {
         desc_src = jay_SHL_u32(b, sampler, 8);
      }

      /* We assume that the driver provided the handle in the top 20 bits so
       * we can use the surface handle directly as the extended descriptor.
       */
      desc_ex_src = jay_alloc_def(b, J_ADDRESS, 1);
      jay_MOV(b, desc_ex_src, surface);
   } else {
      /* Immediate portion of the descriptor */
      desc = brw_sampler_desc(nj->devinfo, 0, 0, msg_type, simd_mode, ret_type);

      if (sampler_bindless) {
         desc_src = surface;
      } else if (!sampler_bindless && jay_is_imm(sampler)) {
         desc_src = jay_OR_u32(b, surface, jay_as_uint(sampler) << 8);
      } else {
         desc_src = jay_OR_u32(b, jay_SHL_u32(b, sampler, 8), surface);
      }

      desc_src = jay_AND_u32(b, desc_src, 0xfff);
   }

   for (unsigned i = 0; i < n_sources; ++i) {
      payload[i] =
         jay_src_as_strided(b, payload[i], 1, payload_uniform ? UGPR : GPR);
   }

   enum jay_type src_type = jay_type(JAY_TYPE_U, payload_type_bit_size);
   jay_SEND(b, .sfid = GEN_SFID_SAMPLER, .msg_desc = desc, .desc = desc_src,
            .ex_desc = desc_ex_src, .header = header, .srcs = payload,
            .nr_srcs = n_sources, .type = JAY_TYPE_U32,
            .src_type = { src_type }, .dst = tmp, .uniform = payload_uniform,
            .bindless = surface_bindless, .pure = true,
            .skip_helpers = tex->skip_helpers);

   /* If we sampled into a temporary, copy out to the final */
   if (residency) {
      jay_MOV(b, jay_extract(dst, 1), jay_extract(tmp, null_mask_component));
   } else if (!jay_defs_equivalent(dst, tmp)) {
      unsigned i = 0;
      unsigned tmp_stride = dst.file == UGPR ? jay_ugpr_per_grf(b->shader) : 1;

      u_foreach_bit(c, component_mask) {
         jay_MOV(b, jay_extract(dst, c), jay_extract(tmp, (i++) * tmp_stride));
      }
   }

   if (mesa_shader_stage_is_compute(b->shader->stage)) {
      b->shader->prog_data->cs.uses_sampler |= !nir_tex_instr_is_query(tex);
   }
}

static void
jay_emit_jump(struct nir_to_jay_state *nj, nir_jump_instr *instr)
{
   switch (instr->type) {
   case nir_jump_break:
      jay_block_add_successor(nj->current_block, nj->break_block, GPR);
      jay_BREAK(&nj->bld);
      break;
   case nir_jump_halt:
      nj->needs_final_halt = true;
      jay_block_add_successor(nj->current_block, nj->exit_block, GPR);
      jay_HALT(&nj->bld, false);
      break;
   case nir_jump_return:
      /* Should be lowered */
   default:
      UNREACHABLE("unknown jump");
   }
}

static void
jay_emit_instr(struct nir_to_jay_state *nj, jay_block *block, nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_alu:
      jay_emit_alu(nj, nir_instr_as_alu(instr));
      break;

   case nir_instr_type_intrinsic:
      jay_emit_intrinsic(nj, nir_instr_as_intrinsic(instr));
      break;

   case nir_instr_type_tex:
      jay_emit_texture(nj, nir_instr_as_tex(instr));
      break;

   case nir_instr_type_load_const:
      jay_emit_load_const(nj, nir_instr_as_load_const(instr));
      break;

   case nir_instr_type_phi:
   case nir_instr_type_undef: {
      jay_def def = nj_def(nir_instr_def(instr));

      jay_foreach_comp(def, c) {
         if (instr->type == nir_instr_type_phi) {
            jay_PHI_DST(&nj->bld, jay_extract(def, c));
         } else {
            jay_UNDEF(&nj->bld, jay_extract(def, c));
         }
      }

      break;
   }

   case nir_instr_type_jump:
      jay_emit_jump(nj, nir_instr_as_jump(instr));
      break;

   case nir_instr_type_deref:
      UNREACHABLE("All derefs should've been lowered");

   default:
      UNREACHABLE("unknown instruction type");
   }
}

static jay_block *
jay_create_block(struct nir_to_jay_state *nj)
{
   jay_block *block = jay_new_block(nj->f);
   block->indent = nj->indent;
   return block;
}

static jay_inst *
jay_block_ending_unconditional_jump(jay_block *block)
{
   jay_inst *jump = jay_block_ending_jump(block);
   return jump && !jump->predication ? jump : NULL;
}

static void
jay_emit_if(struct nir_to_jay_state *nj, nir_if *nif)
{
   jay_builder *b = &nj->bld;
   jay_def condition = nj_src(nif->condition);

   jay_block *before_block = nj->current_block;
   jay_block *after_block = jay_create_block(nj);

   /* Push */
   ++nj->indent;

   jay_block *else_first = jay_create_block(nj);

   jay_block *then_first = jay_emit_cf_list(nj, &nif->then_list);
   jay_block *then_last = nj->current_block;

   nj->after_block = else_first;

   jay_block *else_first_2 = jay_emit_cf_list(nj, &nif->else_list);
   jay_block *else_last = nj->current_block;
   assert(else_first == else_first_2);

   /* Pop */
   --nj->indent;

   bool uniform = jay_is_uniform(condition);

   /* Logical CFG edges */
   jay_block_add_successor(before_block, then_first, GPR);
   jay_block_add_successor(before_block, else_first, GPR);

   if (!jay_block_ending_unconditional_jump(then_last))
      jay_block_add_successor(then_last, after_block, GPR);

   if (!jay_block_ending_unconditional_jump(else_last))
      jay_block_add_successor(else_last, after_block, GPR);

   /* For a non-uniform IF, we fall through both sides in the physical CFG */
   if (!uniform) {
      jay_block_add_successor(then_last, else_first, UGPR);
   }

   nj->after_block = after_block;

   /* Emit the if-else-endif sequence */
   b->cursor = jay_after_block(before_block);
   jay_add_predicate(b, jay_IF(b), condition);

   b->cursor = jay_before_block(else_first);
   jay_ELSE(b);

   b->cursor = jay_after_block(else_last);
   jay_ENDIF(b);
}

static void
jay_emit_loop(struct nir_to_jay_state *nj, nir_loop *nloop)
{
   assert(!nir_loop_has_continue_construct(nloop));

   jay_builder *b = &nj->bld;
   jay_block *saved_break = nj->break_block;

   /* Make the block that will be after the loop exit */
   nj->break_block = jay_create_block(nj);
   ++nj->indent;

   /* Make a block for the loop body, which is also the loop header */
   jay_block *loop_header = jay_create_block(nj);
   loop_header->physical_loop_header = true;

   /* The current block falls through to the start of the loop */
   jay_block_add_successor(nj->current_block, loop_header, GPR);

   /* Emit the loop body */
   nj->after_block = loop_header;
   jay_emit_cf_list(nj, &nloop->body);

   /* Emit the backedge */
   jay_inst *jump = jay_block_ending_jump(nj->current_block);
   if (jump && (jump->op == JAY_OPCODE_BREAK || jump->op == JAY_OPCODE_HALT)) {
      jump->op = JAY_OPCODE_LOOP_ONCE;
   } else {
      jay_block_add_successor(nj->current_block, loop_header, GPR);
      jay_WHILE(b);
      loop_header->loop_header = true;
   }

   /* Pop */
   --nj->indent;
   nj->after_block = nj->break_block;
   nj->break_block = saved_break;

   b->cursor = jay_after_block(nj->after_block);
}

static jay_block *
jay_emit_block(struct nir_to_jay_state *nj, nir_block *nb)
{
   jay_builder *b = &nj->bld;

   /* Reset per block state */
   memset(nj->msg_header, 0, sizeof(nj->msg_header));
   memset(nj->msg_header_unmoved, 0, sizeof(nj->msg_header_unmoved));

   if (nj->after_block) {
      nj->current_block = nj->after_block;
      nj->after_block = NULL;
   } else {
      nj->current_block = jay_create_block(nj);
   }

   jay_block *block = nj->current_block;
   block->uniform = !nb->divergent;
   list_addtail(&block->link, &nj->f->blocks);

   b->cursor = jay_after_block(block);

   /* Emit the contents of the block */
   nir_foreach_instr(instr, nb) {
      jay_emit_instr(nj, block, instr);
   }

   /* Look in the current NIR block's successors for any phis. Each of them
    * should have a source corresponding to a value coming from our current
    * block. Create PHI_SRC opcodes in the current block for those values.
    * The corresponding PHI_DST may not have been emitted yet, but that's ok.
    */
   for (unsigned bs = 0; bs < ARRAY_SIZE(nb->successors); ++bs) {
      nir_block *nb_successor = nb->successors[bs];
      if (!nb_successor)
         continue;

      nir_foreach_phi(nphi, nb_successor) {
         jay_def val = nj_src(nir_phi_get_src_from_block(nphi, nb)->src);

         /* The phi def might be nonuniform but have uniform source (like a
          * constant). Move to the correct file in the the source block and
          * reference that in PHI_SRC.
          */
         if (jay_file_for_def(&nphi->def) != val.file) {
            b->cursor = jay_after_block_logical(block);
            jay_def tmp = val;
            val = jay_alloc_def(b, jay_file_for_def(&nphi->def),
                                jay_num_values(val));
            jay_copy(b, val, tmp);
         }

         jay_foreach_comp(val, c) {
            b->cursor = jay_before_jump(block);
            jay_PHI_SRC(b, JAY_TYPE_U32, jay_extract(val, c),
                        nphi->def.index + c);
         }
      }
   }

   b->cursor = jay_after_block(block);
   nj->active_lane_mask = jay_null();
   nj->active_lane = jay_null();
   nj->active_lane_x4 = jay_null();

   return block;
}

static jay_block *
jay_emit_cf_list(struct nir_to_jay_state *nj, struct exec_list *list)
{
   jay_block *start_block = NULL;

   foreach_list_typed(nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_block: {
         jay_block *block = jay_emit_block(nj, nir_cf_node_as_block(node));

         if (!start_block)
            start_block = block;
         break;
      }

      case nir_cf_node_if:
         jay_emit_if(nj, nir_cf_node_as_if(node));
         break;

      case nir_cf_node_loop:
         jay_emit_loop(nj, nir_cf_node_as_loop(node));
         break;

      default:
         UNREACHABLE("Unknown NIR control flow node");
      }
   }

   return start_block;
}

static void
jay_emit_eot(struct nir_to_jay_state *nj)
{
   jay_builder *b = &nj->bld;
   b->cursor = jay_after_block(nj->exit_block);

   /* Jump target for HALT */
   if (nj->needs_final_halt) {
      if (nj->s->stage == MESA_SHADER_FRAGMENT) {
         assert(nj->s->helpers_tracked);
      } else {
         jay_HALT_TARGET(&nj->bld);
      }
   }

   if (mesa_shader_stage_is_compute(nj->nir->info.stage) ||
       mesa_shader_stage_is_rt(nj->nir->info.stage)) {
      jay_def u0 = nj->payload.u0;

      /* Vectorized copy into the EOT register. Not necessary for correctness
       * but keeps RA from inserting 16 scalar copies instead.
       */
      if (jay_has_early_eot(nj->s)) {
         u0 = jay_alloc_def(b, UGPR, jay_ugpr_per_grf(b->shader));
         jay_MOV(b, u0, nj->payload.u0);
      }

      jay_SEND(b, .sfid = GEN_SFID_MESSAGE_GATEWAY, .eot = true, .msg_desc = 0,
               .srcs = &u0, .nr_srcs = 1, .type = JAY_TYPE_U32,
               .uniform = true);
   } else if (nj->nir->info.stage >= MESA_SHADER_VERTEX &&
              nj->nir->info.stage <= MESA_SHADER_GEOMETRY) {
      jay_block *block = jay_last_source_block(nj->f);
      jay_inst *I = jay_last_inst(block);

      if ((I && I->op == JAY_OPCODE_SEND) &&
          jay_send_sfid(I) == GEN_SFID_URB &&
          !nj->needs_final_halt) {
         /* Pluck out the existing final SEND and put it in the exit block */
         jay_set_send_eot(I, true);
         jay_remove_instruction(I);
         jay_builder_insert(b, I);
      } else {
         /* There's no SEND to reuse, make a noop write for EOT */
         const gen_lsc_ex_desc gen_ex_desc = {
            .addr_type = LSC_ADDR_SURFTYPE_FLAT,
         };
         uint64_t ex_desc =
            gen_lsc_ex_desc_encode(nj->devinfo, &gen_ex_desc, NULL);

         uint64_t desc =
            (ex_desc << 32) |
            lsc_msg_desc(nj->devinfo, LSC_OP_STORE, LSC_ADDR_SURFTYPE_FLAT,
                         LSC_ADDR_SIZE_A32, LSC_DATA_SIZE_D32, 1, false,
                         LSC_CACHE(nj->devinfo, STORE, L1UC_L3UC));

         jay_def never = jay_alloc_def(b, FLAG, 1);
         jay_MOV(b, never, jay_imm(0));

         jay_def srcs[2];
         for (unsigned i = 0; i < 2; i++) {
            srcs[i] = jay_alloc_def(b, UGPR, 1);
            jay_UNDEF(b, srcs[i]);
         }

         I = jay_SEND(b, .sfid = GEN_SFID_URB, .msg_desc = desc, .srcs = srcs,
                      .nr_srcs = 2, .type = JAY_TYPE_U32, .uniform = true,
                      .eot = true);
         I = jay_add_predicate(b, I, never);
      }
   }
}

struct payload_builder {
   jay_builder *b;
   unsigned offsets[JAY_NUM_SSA_FILES];
   jay_def vecs[JAY_NUM_SSA_FILES];
};

static jay_def
read_payload(struct payload_builder *b, enum jay_file file)
{
   unsigned granularity = file == UGPR ? 16 : 1;
   unsigned channel = b->offsets[file] % granularity;

   if (channel == 0) {
      b->vecs[file] = jay_alloc_def(b->b, file, granularity);
      jay_PRELOAD(b->b, b->vecs[file], b->offsets[file]);
   }

   b->offsets[file]++;
   return jay_extract(b->vecs[file], channel);
}

static jay_def
read_vector_payload(struct payload_builder *b, enum jay_file file, unsigned len)
{
   jay_def defs[JAY_MAX_DEF_LENGTH];
   assert(len < ARRAY_SIZE(defs));

   for (unsigned i = 0; i < len; ++i) {
      defs[i] = read_payload(b, file);
   }

   return jay_collect_vectors(b->b, defs, len);
}

static void
setup_payload_dispatch_start(struct nir_to_jay_state *nj,
                             struct payload_builder *p)
{
   jay_shader *s = nj->s;

   const unsigned start_grf = p->offsets[GPR] * jay_grf_per_gpr(nj->s) +
                              p->offsets[UGPR] / jay_ugpr_per_grf(nj->s);

   if (s->stage == MESA_SHADER_FRAGMENT && s->dispatch_width == 32) {
      s->prog_data->fs.dispatch_grf_start_reg_32 = start_grf;
   } else if (s->stage == MESA_SHADER_FRAGMENT && s->dispatch_width == 16) {
      s->prog_data->fs.dispatch_grf_start_reg_16 = start_grf;
   } else {
      s->prog_data->base.dispatch_grf_start_reg = start_grf;
   }
}

static void
setup_payload_push(struct nir_to_jay_state *nj, struct payload_builder *p)
{
   unsigned push_size_B = 0;
   for (int i = 0; i < ARRAY_SIZE(nj->s->prog_data->base.push_sizes); i++) {
      push_size_B += nj->s->prog_data->base.push_sizes[i];
   }

   assert(util_is_aligned(push_size_B, 32));
   for (unsigned i = 0; i < (push_size_B / 4); ++i) {
      nj->payload.push_data[i] = read_payload(p, UGPR);
   }

   nj->s->push_grfs = push_size_B / (4 * jay_ugpr_per_grf(nj->s));
}

static void
setup_vertex_payload(struct nir_to_jay_state *nj, struct payload_builder *p)
{
   nj->payload.urb_handle = read_payload(p, GPR);

   setup_payload_dispatch_start(nj, p);
   setup_payload_push(nj, p);

   for (unsigned i = 0; i < (8 * nj->s->prog_data->vue.urb_read_length); ++i) {
      assert(i < ARRAY_SIZE(nj->payload.vs.attributes));
      nj->payload.vs.attributes[i] = read_payload(p, GPR);
   }
}

static void
setup_tess_ctrl_payload(struct nir_to_jay_state *nj, struct payload_builder *p)
{
   nj->payload.urb_handle = read_payload(p, GPR);

   if (nj->s->prog_data->tcs.include_primitive_id)
      nj->payload.tcs.primitive_id = read_payload(p, GPR);

   nj->payload.tcs.icp_handles =
      read_vector_payload(p, GPR,
                          nj->s->prog_data->tcs.input_vertices ?:
                             BRW_MAX_TCS_INPUT_VERTICES);

   setup_payload_dispatch_start(nj, p);
   setup_payload_push(nj, p);
}

static void
setup_tess_eval_payload(struct nir_to_jay_state *nj, struct payload_builder *p)
{
   nj->payload.tes.tess_coord = read_vector_payload(p, GPR, 3);
   nj->payload.urb_handle = read_payload(p, GPR);

   setup_payload_dispatch_start(nj, p);
   setup_payload_push(nj, p);

   unsigned input_ugprs = 8 * nj->s->prog_data->vue.urb_read_length;

   for (unsigned i = 0; i < input_ugprs; ++i) {
      assert(i < ARRAY_SIZE(nj->payload.tes.patch_inputs));
      nj->payload.tes.patch_inputs[i] = read_payload(p, UGPR);
   }
}

static void
setup_compute_payload(struct nir_to_jay_state *nj, struct payload_builder *p)
{
   assert(!nj->s->prog_data->cs.generate_local_id);

   if (nj->s->prog_data->cs.uses_btd_stack_ids)
      nj->payload.u1 = read_vector_payload(p, UGPR, jay_ugpr_per_grf(nj->s));

   for (unsigned i = 0; i < jay_ugpr_per_grf(nj->s); ++i) {
      nj->payload.inline_data[i] = read_payload(p, UGPR);
   };

   setup_payload_dispatch_start(nj, p);
}

static void
setup_bindless_payload(struct nir_to_jay_state *nj, struct payload_builder *p)
{
   /* Bspec 56937: BTD R1 has stack IDs each 16-bit wide, so each Dword has 2
    * stack ids.
    */
   nj->payload.u1 = read_vector_payload(p, UGPR, jay_ugpr_per_grf(nj->s));

   /* Bspec 56938: BTD R2:
    *
    *    channel 0-2 (2 Dwords) - Global argument pointer
    *    channel 2-3 (2 Dwords) - Local argument pointer
    */
   for (unsigned i = 0; i < jay_ugpr_per_grf(nj->s); ++i) {
      nj->payload.inline_data[i] = read_payload(p, UGPR);
   }

   setup_payload_dispatch_start(nj, p);
}

static void
setup_fragment_payload(struct nir_to_jay_state *nj, struct payload_builder *p)
{
   /* Summarizing the "PS Thread Payload for Normal Dispatch" docs, the
    * physical thread payload layout is as follows:
    *
    * UGPRs:
    * R0: All modes
    * R1: SIMD32-only (not present for SIMD16)
    *
    * Barycentrics (optional, see "Barycentric Interpolation Mode" bits):
    * (lanes 15:0 in first register, lanes 31:16 in higher register)
    *
    * GPRs:
    * R2+R23:  pixel location[1]
    * R3+R24:  pixel location[2]
    * R4+R25:  centroid[1]
    * R5+R26:  centroid[2]
    * R6+R27:  sample[1]
    * R7+R28:  sample[2]
    * R8+R29:  noperspective pixel[1]
    * R9+R30:  noperspective pixel[2]
    * R10+R31: noperspective centroid[1]
    * R11+R32: noperspective centroid[2]
    * R12+R33: noperspective sample[1]
    * R13+R34: noperspective sample[2]
    *
    * R14+R35: Source Depth (optional)
    * R15+R36: Source W (optional)
    * R16+R37: Input Coverage Mask (optional)
    *
    * R17-R18: (defeatured)
    *
    * UGPRs:
    * R19: Sample Position Offsets (optional, see "XY Offset Select")
    *      32 lanes in a single register, X/Y are 1 byte each.
    *      (i.e. lane 7 is at coordinate (X, Y))
    * R20: Centroid Position Offsets (see "Requires Centroid Offset")
    *
    * R21: Requested Coarse Pixel Shading Rate (optional)
    *
    * R22: Sample Offsets (optional, see "Requires Sample Offsets")
    *      (i.e. sample 4 is at subpixel coordinate (X, Y))
    *
    * TODO: multipolygon, explicit barycentrics, ...
    */

   jay_fs_payload *fs = &nj->payload.fs;
   jay_builder *b = &nj->bld;

   if (nj->s->dispatch_width == 32) {
      nj->payload.u1 = read_vector_payload(p, UGPR, jay_ugpr_per_grf(nj->s));
   }

   u_foreach_bit(i, nj->s->prog_data->fs.barycentric_interp_modes) {
      fs->bary[i] = read_vector_payload(p, GPR, 2);
   }

   struct {
      bool cond;
      jay_def *def;
   } split_gprs[] = {
      { nj->s->prog_data->fs.uses_src_depth,   &fs->coord.z       },
      { nj->s->prog_data->fs.uses_src_w,       &fs->coord.w       },
      { nj->s->prog_data->fs.uses_sample_mask, &fs->coverage_mask },
   };

   unsigned extra_gpr =
      split_gprs[0].cond + split_gprs[1].cond + split_gprs[2].cond;
   bool odd = extra_gpr & 1;

   for (unsigned i = 0; i < ARRAY_SIZE(split_gprs); ++i) {
      if (split_gprs[i].cond) {
         extra_gpr -= 1;

         /* Pad out to GPR alignment by reading the last split GPR as two UGPR
          * halves and zipping them together below. This lets us construct a
          * valid partition with minimal copying.
          */
         if (extra_gpr == 0 && jay_grf_per_gpr(nj->s) == 2 && odd) {
            *split_gprs[i].def =
               read_vector_payload(p, UGPR, jay_ugpr_per_grf(nj->s));
         } else {
            *split_gprs[i].def = read_payload(p, GPR);
         }
      }
   }

   assert(extra_gpr == 0);

   if (nj->s->prog_data->fs.uses_pos_offset) {
      fs->sample_pos = read_vector_payload(p, UGPR, jay_ugpr_per_grf(nj->s));

      /* 2 bytes per lane, divided into 4 byte UGPRs */
      fs->sample_pos =
         jay_extract_range(fs->sample_pos, 0, nj->s->dispatch_width / 2);
   }

   nj->s->payload_ugprs = p->offsets[UGPR];

   jay_def split[3] = { jay_null() };
   for (unsigned i = 0; i < ARRAY_SIZE(split_gprs); ++i) {
      if (!jay_is_null(*split_gprs[i].def) &&
          (*split_gprs[i].def).file == UGPR) {
         split[i] = read_vector_payload(p, UGPR, jay_ugpr_per_grf(nj->s));
      }
   }

   if (nj->s->prog_data->fs.uses_depth_w_coefficients ||
       nj->s->prog_data->fs.uses_pc_bary_coefficients) {
      fs->coefficients = read_vector_payload(p, UGPR, jay_ugpr_per_grf(nj->s));
   }

   setup_payload_dispatch_start(nj, p);
   setup_payload_push(nj, p);

   fs->config = nj->payload.push_data[nj->s->prog_data->fs.fs_config_param / 4];

   if (nj->s->prog_data->fs.num_varying_inputs > 0) {
      fs->deltas =
         linear_alloc_child_array(nj->s->lin_ctx, sizeof(jay_def),
                                  nj->s->prog_data->fs.num_varying_inputs * 4);

      for (unsigned i = 0; i < nj->s->prog_data->fs.num_varying_inputs * 4;
           ++i) {
         fs->deltas[i] = read_vector_payload(p, UGPR, 3);

         /* Padding */
         if ((i % 5) == 4) {
            read_payload(p, UGPR);
         }
      }
   }

   /* INIT_HELPERS reads UGPRs but has no SSA write. Therefore to minimize
    * pressure, we want to hoist it as much as possible.
    */
   if (nj->s->helpers_tracked) {
      jay_INIT_HELPERS(b, jay_extract(nj->payload.u0, 15),
                       payload_u1(nj, 15, 1));
   }

   for (unsigned i = 0; i < ARRAY_SIZE(split_gprs); ++i) {
      if (!jay_is_null(split[i]) && split_gprs[i].def->file == UGPR) {
         *(split_gprs[i].def) =
            jay_ZIP_UGPR16_u32(b, *split_gprs[i].def, split[i]);
      }
   }

   if (nj->s->prog_data->fs.uses_src_xy) {
      jay_def t = jay_alloc_def(b, GPR, 1);
      jay_def lo = jay_extract_range(nj->payload.u0, 10, 4);
      jay_EXPAND_QUAD(b, t, lo, payload_u1(nj, 10, 4));

      if (nj->s->prog_data->fs.coarse_pixel_dispatch) {
         jay_def size_u8v2 = jay_extract(nj->payload.u0, 8);

         /* Expand from u8vec2 to u16vec2 */
         jay_def x = jay_CVT_u32(b, size_u8v2, JAY_TYPE_U8, JAY_ROUND, 0);
         jay_def y = jay_CVT_u32(b, size_u8v2, JAY_TYPE_U8, JAY_ROUND, 1);
         jay_def size_xy = jay_alloc_def(b, UGPR, 8);
         jay_BFI2(b, size_xy, 0xffff0000, y, x);

         /* 'size' in the lanes for the far corners, 0 for the near corners */
         jay_def size_in_far_corners = jay_alloc_def(b, UGPR, 8);
         jay_COARSE_PIXEL_CORNERS(b, size_in_far_corners, size_xy);

         /* Note: the low bit of .y will roll over into .x's high bit */
         jay_def half_size_xy =
            jay_AND_u32(b, jay_SHR_u32(b, jay_extract(size_xy, 0), 1),
                        0x00070007);

         /* The coordinate offsets we want to compute for the near (top/left)
          * and far (bottom/right) corners are 0.5/1.5x the coarse pixel size:
          *
          *     pixel size   | near offset  | far offset
          *     1 = 00000001 | 0 = 00000000 | 1 = 00000001
          *     2 = 00000010 | 1 = 00000001 | 3 = 00000011
          *     4 = 00000100 | 2 = 00000010 | 6 = 00000110
          *
          * From this, we can see that the offsets in the near lanes
          * should be (size >> 1), and the offsets for the far lanes
          * should be (size >> 1) | size.
          */
         jay_def offsets = jay_alloc_def(b, UGPR, 8);
         jay_OR(b, JAY_TYPE_U32, offsets, size_in_far_corners, half_size_xy);

         fs->coord.xy =
            jay_OFFSET_PACKED_PIXEL_COORDS_u32(&nj->bld, t, offsets);
      } else {
         fs->coord.xy = jay_OFFSET_PACKED_PIXEL_COORDS_u32(&nj->bld, t, 1);
      }
   }

   /* Renumber to match what jay_insert_payload_swizzle expects. */
   if (nj->s->dispatch_width == 32) {
      jay_foreach_inst_in_block(nj->after_block, I) {
         if (I->op == JAY_OPCODE_PRELOAD && I->dst.file == GPR) {
            unsigned base = (jay_preload_reg(I) % 2) ? p->offsets[GPR] : 0;
            jay_set_preload_reg(I, base + (jay_preload_reg(I) / 2));
         }
      }
   }
}

/*
 * For SIMD32 dispatch, many fields come as pairs of discontiguous GRFs
 * (i.e. R2+R23), where the first register contains the lanes 15:0, and
 * the higher register contains lanes 31:16.  This doesn't map well to
 * our assumption that GPRs hold 32 lanes of values and are stored in
 * contiguous aligned pairs of GRFs.
 *
 * We insert copies to put both halves together.  Payload fields have
 * both an even-numbered and an odd-numbered register (i.e. R2+R23).
 * We use some tricks to reduce the number of copies.
 */
static void
jay_insert_payload_swizzle(jay_shader *s)
{
   jay_function *func = jay_shader_get_entrypoint(s);
   jay_builder b = jay_init_builder(func, jay_before_function(func));

   unsigned size = s->payload_gprs;

   /* Odd: copy both halves to contiguous pair after payload */
   for (unsigned i = 0; i < (size / 2); ++i) {
      jay_DESWIZZLE_ODD(&b, jay_bare_reg(GPR, size + i), jay_bare_reg(GPR, i),
                        jay_bare_reg(GPR, i + ((size + 1) / 2)), !(size & 1));
   }

   /* Even: leave the bottom half in place, copy top half. If size=1 (rare
    * but possible), this would be a no-op move so skip it.
    */
   if (size > 1) {
      for (unsigned i = 0; i < DIV_ROUND_UP(size, 2); ++i) {
         jay_DESWIZZLE_EVEN(&b, jay_bare_reg(GPR, i),
                            jay_bare_reg(GPR, (size / 2) + i), size & 1);
      }
   }
}

static void
jay_setup_payload(struct nir_to_jay_state *nj)
{
   jay_shader *s = nj->s;
   jay_builder *b = &nj->bld;
   nj->after_block = jay_create_block(nj);
   b->cursor = jay_after_block(nj->after_block);

   struct payload_builder p = { .b = &nj->bld };
   nj->payload.u0 = read_vector_payload(&p, UGPR, jay_ugpr_per_grf(s));
   nj->payload.sampler_state_pointer = jay_extract(nj->payload.u0, 3);

   switch (s->stage) {
   case MESA_SHADER_VERTEX:
      setup_vertex_payload(nj, &p);
      break;
   case MESA_SHADER_TESS_CTRL:
      setup_tess_ctrl_payload(nj, &p);
      break;
   case MESA_SHADER_TESS_EVAL:
      setup_tess_eval_payload(nj, &p);
      break;
   case MESA_SHADER_FRAGMENT:
      setup_fragment_payload(nj, &p);
      break;
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
      setup_compute_payload(nj, &p);
      break;
   case MESA_SHADER_RAYGEN:
   case MESA_SHADER_ANY_HIT:
   case MESA_SHADER_CLOSEST_HIT:
   case MESA_SHADER_MISS:
   case MESA_SHADER_INTERSECTION:
   case MESA_SHADER_CALLABLE:
      setup_bindless_payload(nj, &p);
      break;
   default:
      UNREACHABLE("unimplemented shader stages");
   }

   s->payload_gprs = p.offsets[GPR];
}

/*
 * NIR sometimes contains logically unreachable blocks (e.g. due to infinite
 * loops). These blocks have no predecessors, but do have successors and can
 * contribute to phis. They are dead and violate the IR invariant:
 *
 *    Live-in sources are live-out in all predecessors.
 *
 * ...which RA (validation) depends on. The simplest solution is to simply
 * delete these dead blocks. Fortunately, because they are unreachable, this
 * does not have any ill effects. Notably, this cannot introduce critical edges.
 *
 * Deleting a block may cause a successor to become unreachable, so we use a
 * fixed-point algorithm to converge.
 */
static void
jay_remove_unreachable_blocks(jay_function *func)
{
   bool progress;
   do {
      progress = false;

      jay_foreach_block(func, pred) {
         if (pred != jay_first_block(func) &&
             jay_num_predecessors(pred, GPR) == 0 &&
             jay_num_successors(pred, GPR) > 0) {

            jay_foreach_successor(pred, succ, GPR) {
               util_dynarray_delete_unordered(&succ->logical_preds, jay_block *,
                                              pred);
            }

            jay_foreach_successor(pred, succ, UGPR) {
               util_dynarray_delete_unordered(&succ->physical_preds,
                                              jay_block *, pred);
            }

            pred->logical_succs[0] = NULL;
            pred->logical_succs[1] = NULL;
            pred->physical_succs[0] = NULL;
            pred->physical_succs[1] = NULL;
            progress = true;
         }
      }
   } while (progress);
}

static void
jay_from_nir_function(const struct intel_device_info *devinfo,
                      nir_shader *nir,
                      jay_shader *s,
                      nir_function_impl *impl)
{
   jay_function *f = jay_new_function(s);
   f->is_entrypoint = impl->function->is_entrypoint;

   struct nir_to_jay_state nj = {
      .s = s,
      .f = f,
      .nir = nir,
      .devinfo = devinfo,
      .bld = (jay_builder) { .shader = s, .func = f },
   };

   /* Jay indices match NIR indices. Therefore the first impl->ssa_alloc
    * indices are reserved. Our own temporaries go after.
    */
   f->ssa_alloc = impl->ssa_alloc;

   if (f->is_entrypoint) {
      jay_setup_payload(&nj);
   }

   nj.exit_block = jay_create_block(&nj);
   jay_emit_cf_list(&nj, &impl->body);
   jay_block_add_successor(nj.current_block, nj.exit_block, GPR);

   list_addtail(&nj.exit_block->link, &f->blocks);
   jay_emit_eot(&nj);
   jay_remove_unreachable_blocks(f);
}

static void
jay_gather_stats(const jay_shader *s, struct genisa_stats *stats)
{
   jay_foreach_inst_in_shader(s, f, I) {
      if (I->op != JAY_OPCODE_SYNC) {
         stats->instrs += jay_macro_length(I) << jay_simd_split(s, I);
      }

      stats->loops += I->op == JAY_OPCODE_WHILE;
      stats->sends += I->op == JAY_OPCODE_SEND;
   }

   /* It's unclear how to report cycle counts with multiple functions... */
   stats->cycles = jay_estimate_cycles(jay_shader_get_entrypoint((void *) s));
   stats->spills = s->spills;
   stats->fills = s->fills;
   stats->sends -= (s->spills + s->fills);
}

struct jay_shader_bin *
jay_compile(const struct intel_device_info *devinfo,
            void *mem_ctx,
            nir_shader *nir,
            union brw_any_prog_data *prog_data,
            union brw_any_prog_key *key,
            debug_archiver *archiver)
{
   jay_debug = debug_get_option_jay_debug();
   bool debug =
      INTEL_DEBUG(intel_debug_flag_for_shader_stage(nir->info.stage)) &&
      (!nir->info.internal || NIR_DEBUG(PRINT_INTERNAL));

   bool track_helpers = false;
   unsigned simd_width =
      jay_process_nir(devinfo, nir, prog_data, key, archiver, &track_helpers);

   if (debug) {
      /* We can't use nir_print_shader since it reindexes SSA defs. */
      fprintf(stdout, "NIR right before from_nir:\n\n");
      nir_print_shader_annotated(nir, stdout, NULL);
      fflush(stdout);
   }

   jay_shader *s = jay_new_shader(NULL, nir->info.stage);
   s->dispatch_width = simd_width;
   s->scratch_size = align(nir->scratch_size, 4) * s->dispatch_width;
   s->devinfo = devinfo;
   s->prog_data = prog_data;
   s->archiver = archiver;
   s->helpers_tracked = track_helpers;

   nir_foreach_function_impl(impl, nir) {
      jay_from_nir_function(devinfo, nir, s, impl);
   }

   /* Re-number block indices to be sequential and match the NIR. This ensures
    * block indices are ordered with respect to the control flow graph which is
    * a convenient IR invariant.
    */
   jay_foreach_function(s, f) {
      unsigned index = 0;

      jay_foreach_block(f, b) {
         b->index = index++;
      }
   }

   jay_validate(s, "NIR->Jay translation");

   /* After each propagation pass, eliminate dead code. This ensures use counts
    * are correct in jay_opt_propagate_backwards which allows more progress. We
    * don't do a progress loop - just run DCE an extra time. DCE is cheap.
    */
   if (!(jay_debug & JAY_DBG_NOOPT)) {
      JAY_PASS(s, jay_opt_propagate_forwards);
      JAY_PASS(s, jay_opt_dead_code);

      JAY_PASS(s, jay_opt_propagate_backwards);
      JAY_PASS(s, jay_opt_dead_code);
   }

   if (debug) {
      fprintf(stdout, "Jay shader:\n\n");
      jay_print(stdout, s);
   }

   if (!(jay_debug & JAY_DBG_NOSCHED)) {
      JAY_PASS(s, jay_schedule_pressure);
   }

   JAY_PASS(s, jay_assign_flags);
   if (!(jay_debug & JAY_DBG_NOOPT)) {
      JAY_PASS(s, jay_opt_dead_code);
   }

   JAY_PASS(s, jay_lower_pre_ra);
   JAY_PASS(s, jay_partition_grf);

   if (debug) {
      fprintf(stdout, "Jay shader:\n\n");
      jay_print(stdout, s);
   }

   JAY_PASS(s, jay_register_allocate);
   JAY_PASS(s, jay_lower_post_ra);
   JAY_PASS(s, jay_lower_post_sched, nir->info.float_controls_execution_mode,
            nir->info.bit_sizes_float);

   if (s->dispatch_width == 32 && s->stage == MESA_SHADER_FRAGMENT) {
      JAY_PASS(s, jay_insert_payload_swizzle);
   }

   if (s->stage == MESA_SHADER_FRAGMENT && s->helpers_tracked) {
      JAY_PASS(s, jay_lower_helpers);
   }

   if (!(jay_debug & JAY_DBG_NOOPT)) {
      /* jay_assign_accumulators uses a conservative liveness analysis for
       * predication, so assign accumulators before predicating for better
       * results.
       */
      if (!(jay_debug & JAY_DBG_NOACC)) {
         JAY_PASS(s, jay_assign_accumulators);
      }

      JAY_PASS(s, jay_opt_predicate);
   }

   if (jay_debug & JAY_DBG_SYNC) {
      JAY_PASS(s, jay_lower_scoreboard_trivial);
   } else {
      JAY_PASS(s, jay_lower_scoreboard);
   }

   if (debug) {
      fprintf(stdout, "Jay shader (post-RA):\n\n");
      jay_print(stdout, s);

      jay_print_partition(&s->partition);
   }

   struct jay_shader_bin *bin =
      jay_to_binary(s, nir->constant_data, nir->constant_data_size, debug);
   assert(bin->kernel);
   ralloc_steal(mem_ctx, bin);

   jay_gather_stats(s, &bin->stats);
   bin->stats.code_size = bin->size;

   if (debug) {
      if (nir->info.label) {
         printf("%s - ", nir->info.label);
      }

      const char *shader_name =
         ralloc_asprintf(s, "%s SIMD%u", _mesa_shader_stage_to_abbrev(s->stage),
                         s->dispatch_width);
      genisa_stats_fprintf(stdout, shader_name, &bin->stats);
   }

   bin->stats.workgroup_memory_size = nir->info.shared_size;
   bin->stats.dispatch_width = simd_width;

   if (s->stage == MESA_SHADER_FRAGMENT) {
      if (simd_width == 8) {
         prog_data->fs.dispatch_8 = true;
      } else if (simd_width == 16) {
         prog_data->fs.dispatch_16 = true;
         prog_data->fs.prog_offset_16 = 0;
      } else if (simd_width == 32) {
         prog_data->fs.dispatch_32 = true;
         prog_data->fs.prog_offset_32 = 0;
      }

   } else if (mesa_shader_stage_is_compute(s->stage)) {
      unsigned i = simd_width == 8 ? 0 : simd_width == 16 ? 1 : 2;
      prog_data->cs.prog_offset[i] = 0;
      prog_data->cs.prog_mask = BITFIELD_BIT(i);
      prog_data->cs.prog_spilled = s->scratch_size > 0; /* XXX */
   } else if (brw_shader_stage_is_bindless(s->stage)) {
      prog_data->bs.simd_size = simd_width;
   }

   prog_data->base.program_size = bin->size;

   if (s->scratch_size > 0) {
      /* We currently only support up to 2MB of scratch space.  If we
       * need to support more eventually, the documentation suggests
       * that we could allocate a larger buffer, and partition it out
       * ourselves.  We'd just have to undo the hardware's address
       * calculation by subtracting (FFTID * Per Thread Scratch Space)
       * and then add FFTID * (Larger Per Thread Scratch Space).
       *
       * See 3D-Media-GPGPU Engine > Media GPGPU Pipeline >
       * Thread Group Tracking > Local Memory/Scratch Space.
       */
      assert(s->scratch_size <= devinfo->max_scratch_size_per_thread &&
             "maximum scratch size");

      /* Take the max of any previously compiled variant of the shader. In the
       * case of bindless shaders with return parts, this will also take the
       * max of all parts.
       */
      prog_data->base.total_scratch =
         MAX2(prog_data->base.total_scratch,
              util_next_power_of_two(s->scratch_size));
   }

   /* Scratch is allocated in 1KiB increments. */
   prog_data->base.total_scratch = align(prog_data->base.total_scratch, 1024);

   ralloc_free(s);
   return bin;
}
