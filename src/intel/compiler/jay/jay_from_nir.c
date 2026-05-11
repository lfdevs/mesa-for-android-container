/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/brw/brw_compiler.h"
#include "compiler/brw/brw_eu.h"
#include "compiler/brw/brw_eu_defines.h"
#include "compiler/brw/brw_nir.h"
#include "compiler/brw/brw_sampler.h"
#include "compiler/intel_nir.h"
#include "compiler/intel_shader_enums.h"
#include "compiler/list.h"
#include "intel/dev/intel_debug.h"
#include "util/bitpack_helpers.h"
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
   DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(jay_debug, "JAY_DEBUG", jay_debug_options, 0)
int jay_debug = 0;

typedef struct jay_vs_payload {
   /* "the maximum limit is 30 elements per vertex" (bspec 56124) */
   jay_def attributes[30 * 4];
} jay_vs_payload;

typedef struct jay_cs_payload {
   jay_def local_invocation_ids;
} jay_cs_payload;

typedef struct jay_fs_payload {
   jay_def bary[INTEL_BARYCENTRIC_MODE_COUNT];

   struct {
      jay_def xy, z, w;
   } coord;

   jay_def pixel_sample_mask;
   jay_def deltas[64];
} jay_fs_payload;

struct nir_to_jay_state {
   jay_shader *s;
   jay_function *f;
   const nir_shader *nir;
   const struct intel_device_info *devinfo;

   jay_builder bld;

   jay_block *current_block;
   jay_block *after_block;
   jay_block *break_block;

   unsigned indent;

   /* We cache ballot(true), ctz(ballot(true)), and 4*ctz(ballot(true)) within a
    * block. If we had competent backend CSE - or emitted uniformize in NIR and
    * taught NIR's CSE about ballots - we could remove this kludge.
    */
   jay_def active_lane_mask, active_lane, active_lane_x4;

   /* These defs contain the extracted payload. They are only valid while
    * translating NIR->Jay since they aren't maintained by Jay passes.
    */
   struct {
      jay_def u0, u1;
      jay_def sampler_state_pointer, scratch_surface;
      jay_def inline_data;
      jay_def push_data[512];
      jay_def lane_id;
      jay_def urb_handle;

      union {
         jay_vs_payload vs;
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
#define CMP(op, jay)                                                           \
   case nir_op_##op:                                                           \
      jay_CMP(b, jay_alu_source_type(alu, 0), JAY_CONDITIONAL_##jay, dst,      \
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
   case nir_op_f2i8:
   case nir_op_f2u8: {
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
      jay_BFN(b, bfn, 1, src[0], src[1], UTIL_LUT3(a & (b ^ c)));
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
      /* TODO: Optimize */
      jay_BFI2(b, dst, 0xffff0000, src[1], src[0]);
      break;

   case nir_op_pack_64_2x32_split:
      jay_MOV(b, jay_extract(dst, 0), src[0]);
      jay_MOV(b, jay_extract(dst, 1), src[1]);
      break;

   case nir_op_bitfield_select:
      assert(jay_type_size_bits(type) <= 32);
      jay_BFN(b, dst, src[0], src[1], src[2], UTIL_LUT3((a & b) | (~a & c)));
      break;

   case nir_op_ubfe:
   case nir_op_ibfe:
      jay_BFE(b, type, dst, src[0], src[1], src[2]);
      break;
   case nir_op_bfi:
      jay_BFI2(b, dst, src[0], src[1], src[2]);
      break;

   case nir_op_ffma:
      jay_MAD(b, type, dst, src[0], src[1], src[2]);
      break;

   case nir_op_fcsel:
      jay_CSEL(b, type, dst, src[1], src[2], src[0])->conditional_mod =
         JAY_CONDITIONAL_NE;
      break;

   case nir_op_fcsel_gt:
   case nir_op_i32csel_gt:
      jay_CSEL(b, type, dst, src[1], src[2], src[0])->conditional_mod =
         JAY_CONDITIONAL_GT;
      break;

   case nir_op_fcsel_ge:
   case nir_op_i32csel_ge:
      jay_CSEL(b, type, dst, src[1], src[2], src[0])->conditional_mod =
         JAY_CONDITIONAL_GE;
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
               enum brw_sfid sfid)
{
   bool device = nir_intrinsic_memory_scope(intr) >= SCOPE_QUEUE_FAMILY;
   enum lsc_fence_scope scope = device ? LSC_FENCE_TILE : LSC_FENCE_THREADGROUP;
   enum lsc_flush_type type =
      sfid == BRW_SFID_SLM ? LSC_FLUSH_TYPE_NONE : translate_flush_type(intr);

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
      emit_lsc_fence(nj, intr, BRW_SFID_TGM);
      assert(!nj->nir->info.use_lowered_image_to_global && "fix common code");
   }

   if (modes & (nir_var_mem_ssbo | nir_var_mem_global)) {
      emit_lsc_fence(nj, intr, BRW_SFID_UGM);
   }

   if (modes & (nir_var_shader_out | nir_var_mem_task_payload)) {
      emit_lsc_fence(nj, intr, BRW_SFID_URB);
   }

   if ((modes & nir_var_mem_shared) &&
       !jay_workgroup_is_one_subgroup(&nj->bld, nj->nir)) {
      emit_lsc_fence(nj, intr, BRW_SFID_SLM);
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

   jay_SEND(b, .sfid = BRW_SFID_MESSAGE_GATEWAY,
            .msg_desc = BRW_MESSAGE_GATEWAY_SFID_BARRIER_MSG, .srcs = &zipped,
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

static void
jay_emit_fb_write(jay_builder *b, nir_intrinsic_instr *intr)
{
   jay_def data = nj_src(intr->src[0]);
   jay_def srcs[8];

   /* Optimize unconditional discards. Should probably do this in NIR. */
   bool trivial =
      nir_src_is_const(intr->src[2]) && nir_src_as_bool(intr->src[2]);

   for (unsigned i = 0; i < nir_src_num_components(intr->src[0]); ++i) {
      srcs[i] = trivial ? jay_INDETERMINATE_u32(b) :
                          jay_as_gpr(b, jay_extract(data, i));
   }

   jay_inst *send =
      jay_SEND(b, .sfid = BRW_SFID_RENDER_CACHE, .check_tdr = true,
               .msg_desc = nir_scalar_as_uint(nir_scalar_chase_movs(
                              nir_get_scalar(intr->src[1].ssa, 0))) |
                           (nir_scalar_as_uint(nir_scalar_chase_movs(
                               nir_get_scalar(intr->src[1].ssa, 1)))
                            << 32),
               .srcs = srcs, .nr_srcs = nir_src_num_components(intr->src[0]),
               .type = JAY_TYPE_U32, .eot = nir_intrinsic_eot(intr));

   /* Handle the disable predicate. It is logically inverted. */
   if (!nir_src_is_const(intr->src[2]) || nir_src_as_bool(intr->src[2])) {
      jay_add_predicate(b, send, jay_negate(nj_src(intr->src[2])));
   }
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
   if (dst_file == UGPR) {
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
   bool urb = intr->intrinsic == nir_intrinsic_store_urb_lsc_intel ||
              intr->intrinsic == nir_intrinsic_store_urb_vec4_intel;
   enum brw_sfid sfid = slm ? BRW_SFID_SLM :
                        tgm ? BRW_SFID_TGM :
                        urb ? BRW_SFID_URB :
                              BRW_SFID_UGM;

   nir_src *data_src = nir_get_io_data_src(intr);
   bool scratch = intr->intrinsic == nir_intrinsic_load_scratch_intel ||
                  intr->intrinsic == nir_intrinsic_store_scratch_intel;

   enum lsc_opcode op;
   if (nir_intrinsic_has_atomic_op(intr))
      op = lsc_op_for_atomic(nir_intrinsic_atomic_op(intr));
   else if (sfid == BRW_SFID_TGM)
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

   bool a64 = surf_type == LSC_ADDR_SURFTYPE_FLAT && sfid == BRW_SFID_UGM;
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
      uniform &= !(cmask || urb);
   }

   /* Per bspec 57330, 8-bit/16-bit are not supported for transpose */
   bool transpose = uniform && !cmask && ndata->bit_size >= 32;
   bool scalar_uniform = uniform && !cmask && ndata->bit_size < 32;

   if (!uniform) {
      offset = jay_as_gpr(b, offset);
   } else if (!transpose) {
      offset = jay_src_as_strided(b, offset, a64 ? 2 : 1, UGPR);
   }

   if (!jay_is_null(data) && !transpose && !scalar_uniform)
      data = jay_as_gpr(b, data);

   unsigned access =
      nir_intrinsic_has_access(intr) ? nir_intrinsic_access(intr) : 0;

   bool volatile_access = access & ACCESS_VOLATILE;
   bool coherent_access = access & ACCESS_COHERENT;

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
   unsigned cache =
      urb ? LSC_CACHE(devinfo, STORE, L1UC_L3UC) :
      lsc_opcode_is_atomic(op) ?
            LSC_CACHE(devinfo, STORE, L1UC_L3WB) :
      volatile_access ?
            (devinfo->ver >= 20 ?
                /* Xe2 has a better L3 that can deal with null tiles.*/
                (!has_dest ? LSC_CACHE(devinfo, STORE, L1UC_L3WB) :
                             LSC_CACHE(devinfo, LOAD, L1UC_L3C)) :
                /* On older platforms, all caches have to be bypassed. */
                (!has_dest ? LSC_CACHE(devinfo, STORE, L1UC_L3UC) :
                             LSC_CACHE(devinfo, LOAD, L1UC_L3UC))) :
            /* Skip L1 for coherent accesses */
         coherent_access ? (!has_dest ? LSC_CACHE(devinfo, STORE, L1UC_L3WB) :
                                        LSC_CACHE(devinfo, LOAD, L1UC_L3C)) :
      !has_dest          ? LSC_CACHE(devinfo, STORE, L1STATE_L3MOCS) :
                           LSC_CACHE(devinfo, LOAD, L1STATE_L3MOCS);

   unsigned max_imm_bits = brw_max_immediate_offset_bits(surf_type);
   assert(base_offset >= u_intN_min(max_imm_bits));
   assert(base_offset <= u_intN_max(max_imm_bits));
   assert(base_offset == 0 || sfid != BRW_SFID_TGM);

   const unsigned base_offs_bits =
      util_bitpack_sint(base_offset, 0, max_imm_bits - 1);

   unsigned nr = ndata->num_components;
   uint64_t desc =
      lsc_msg_desc(devinfo, op, surf_type, addr_size,
                   lsc_bits_to_data_size(ndata->bit_size),
                   cmask ? BITFIELD_MASK(nr) : nr, transpose, cache);

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
      desc |= ((uint64_t) lsc_flat_ex_desc(devinfo, base_offs_bits) << 32);
   } else if (jay_is_null(bti_indirect)) {
      desc |=
         ((uint64_t) lsc_bti_ex_desc(devinfo, bti_const, base_offs_bits) << 32);
   } else if (!jay_is_null(bti_indirect)) {
      ex_desc = bti_indirect;

      if (surf_type == LSC_ADDR_SURFTYPE_SS ||
          surf_type == LSC_ADDR_SURFTYPE_BSS) {
         ex_desc_imm = SET_BITS(GET_BITS(base_offs_bits, 16, 4), 31, 19) |
                       SET_BITS(GET_BITS(base_offs_bits, 3, 0), 15, 12);
      } else {
         /* TODO: Move the SHL to NIR for CSE? */
         assert(surf_type == LSC_ADDR_SURFTYPE_BTI);
         assert(base_offs_bits == 0);
         ex_desc = jay_SHL_u32(b, bti_indirect, 24);
      }
   }

   enum jay_type data_type = jay_type(JAY_TYPE_U, MAX2(ndata->bit_size, 32));
   jay_SEND(b, .sfid = sfid, .msg_desc = desc, .srcs = srcs,
            .nr_srcs = jay_is_null(data) ? 1 : 2, .dst = tmp, .type = data_type,
            .src_type = { offset_type, data_type }, .uniform = uniform,
            .bindless = surf_type == LSC_ADDR_SURFTYPE_BSS, .ex_desc = ex_desc,
            .ex_desc_imm = ex_desc_imm);

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
jay_emit_intrinsic(struct nir_to_jay_state *nj, nir_intrinsic_instr *intr)
{
   jay_shader *s = nj->s;
   jay_function *f = nj->f;
   jay_builder *b = &nj->bld;
   jay_cs_payload *cs =
      mesa_shader_stage_is_compute(s->stage) ? &nj->payload.cs : NULL;

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
   case nir_intrinsic_load_scratch_intel:
   case nir_intrinsic_load_shared:
   case nir_intrinsic_load_shared_uniform_block_intel:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_load_ssbo_intel:
   case nir_intrinsic_load_ssbo_uniform_block_intel:
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ubo_uniform_block_intel:
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap:
   case nir_intrinsic_ssbo_atomic:
   case nir_intrinsic_ssbo_atomic_swap:
   case nir_intrinsic_store_global:
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

   case nir_intrinsic_load_push_data_intel: {
      unsigned sz = intr->def.bit_size / 8;
      unsigned base_offset = nir_intrinsic_base(intr);
      assert(util_is_aligned(base_offset, sz));

      if (nir_src_is_const(intr->src[0])) {
         unsigned load_offset = nir_src_as_uint(intr->src[0]);
         unsigned offs = base_offset + load_offset;
         assert(util_is_aligned(load_offset, sz));

         if (sz >= 4) {
            jay_foreach_comp(dst, c) {
               jay_MOV(b, jay_extract(dst, c),
                       nj->payload.push_data[(offs / 4) + c]);
            }
         } else {
            jay_foreach_comp(dst, c) {
               unsigned comp_offs = offs + c * sz;
               if (util_is_aligned(comp_offs, 4)) {
                  jay_MOV(b, jay_extract(dst, c),
                          nj->payload.push_data[comp_offs / 4]);
               } else {
                  jay_CVT(b, JAY_TYPE_U32, jay_extract(dst, c),
                          nj->payload.push_data[comp_offs / 4],
                          JAY_TYPE_U | intr->def.bit_size, JAY_ROUND,
                          (comp_offs % 4) / sz);
               }
            }
         }
      } else {
         UNREACHABLE("todo: indirect push data");
      }
      break;
   }

   case nir_intrinsic_barrier: {
      jay_SCHEDULE_BARRIER(b);

      if (nir_intrinsic_memory_scope(intr) != SCOPE_NONE) {
         jay_emit_memory_barrier(nj, intr);
      }

      if ((cs && nir_intrinsic_execution_scope(intr) == SCOPE_WORKGROUP) &&
          !jay_workgroup_is_one_subgroup(b, nj->nir)) {

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
      jay_emit_fb_write(b, intr);
      break;

   case nir_intrinsic_shader_clock:
      /* We must access the timestamp register atomically, but 64-bit
       * instructions cannot read ARF. Instead use a 2x32-bit vectorized move.
       */
      assert(dst.file == UGPR && "required for vectorization");
      jay_MOV(b, dst, jay_contiguous_def(J_ARF, JAY_ARF_TIMESTAMP, 2))->type =
         JAY_TYPE_U32;
      break;

   case nir_intrinsic_load_sample_mask_in: {
      jay_def mask = jay_extract(nj->payload.u0, 15);

      if (nj->s->dispatch_width == 32) {
         /* TODO: Optimize */
         jay_def hi = jay_extract(nj->payload.u1, 15);
         mask = jay_BFI2_u32(b, 0xffff0000, hi, mask);
      }

      jay_MOV(b, dst, mask);
      break;
   }

   case nir_intrinsic_load_subgroup_invocation:
      /* TODO: Lower this in NIR? */
      jay_CVT(b, JAY_TYPE_U32, dst, nj->payload.lane_id, JAY_TYPE_U16,
              JAY_ROUND, 0);
      break;

   case nir_intrinsic_demote:
   case nir_intrinsic_demote_if:
      /* TODO: Already lowered, but need to implement for performance. */
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
    * GPR input, we could uniformize (as behaviour is undefined for
    * non-uniform inputs) but a lowered bit extract is cheaper than uniformize.
    */
   case nir_intrinsic_inverse_ballot: {
      assert(dst.file == FLAG);
      jay_def x = nj_src(intr->src[0]);
      if (x.file == GPR) {
         jay_def shr = jay_SHR_u32(b, x, nj->payload.lane_id);
         jay_inst *and = jay_AND(b, JAY_TYPE_U32, jay_null(), shr, 1);
         jay_set_conditional_mod(b, and, dst, JAY_CONDITIONAL_NE);
      } else {
         jay_MOV(b, dst, x)->type = JAY_TYPE_U | b->shader->dispatch_width;
      }

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

   case nir_intrinsic_load_pixel_coord_intel:
      jay_MOV(b, dst, nj->payload.fs.coord.xy);
      break;

   case nir_intrinsic_load_frag_coord_z:
      jay_MOV(b, dst, nj->payload.fs.coord.z);
      break;

   case nir_intrinsic_load_frag_coord_w_rcp:
      jay_MOV(b, dst, nj->payload.fs.coord.w);
      break;

   case nir_intrinsic_load_urb_output_handle_intel:
      jay_MOV(b, dst, nj->payload.urb_handle);
      break;

   case nir_intrinsic_load_layer_id:
      jay_EXTRACT_LAYER(b, dst, jay_extract(nj->payload.u0, 9),
                        payload_u1(nj, 9, 1));
      break;

   case nir_intrinsic_load_front_face: {
      /* Bit 11 is facingness for the first polygon. TODO: Multipolygon. */
      jay_inst *and = jay_AND(b, JAY_TYPE_U32, jay_null(),
                              jay_extract(nj->payload.u0, 9), BITFIELD_BIT(11));

      /* The bit is actually backfacingness so check for equality with 0 */
      jay_set_conditional_mod(b, and, dst, JAY_CONDITIONAL_EQ);
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

      FALLTHROUGH;
   case nir_intrinsic_load_fs_input_interp_deltas: {
      assert(s->stage == MESA_SHADER_FRAGMENT);
      unsigned location = nir_intrinsic_io_semantics(intr).location +
                          nir_src_as_uint(intr->src[0]);
      unsigned i = (s->prog_data->fs.urb_setup[location] * 4) +
                   nir_intrinsic_component(intr);

      if (intr->intrinsic == nir_intrinsic_load_input) {
         assert(intr->def.num_components == 1 && "should be scalarized");
      }

      /* Zeroth delta is the flat value */
      jay_copy(b, dst, nj->payload.fs.deltas[i]);
      break;
   }

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

   case nir_intrinsic_load_inline_data_intel: {
      assert(cs && f->is_entrypoint && "todo: this needs ABI");

      unsigned offset = nir_intrinsic_base(intr) / 4;
      unsigned nr = jay_num_values(dst);
      jay_copy(b, dst, jay_extract_range(nj->payload.inline_data, offset, nr));
      break;
   }

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

      /* Vectorized zeroing of the header. TODO: This can be optimized more. */
      jay_def zeroes = jay_alloc_def(b, UGPR, jay_ugpr_per_grf(b->shader));
      jay_MOV(b, zeroes, 0);

      jay_def ugprs[JAY_MAX_DEF_LENGTH];
      jay_foreach_comp(zeroes, i) {
         ugprs[i] = jay_extract(zeroes, i);
      }

      /* Set the main immediate part of the header */
      if (header2 != 0) {
         ugprs[2] = jay_MOV_u32(b, header2);
      }

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
         ugprs[3] = sampler;
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

         ugprs[3] = sampler_ptr;
      }
      /* Zip it all up into a vector of UGPRs which will RA to a single GRF */
      header = jay_collect_vectors(b, ugprs, jay_num_values(zeroes));
   }

   assert(payload_type_bit_size == 16 || payload_type_bit_size == 32);
   unsigned simd_mode = 0;
   unsigned simd_width = payload_uniform ? 1 : nj->s->dispatch_width;
   if (nj->devinfo->ver < 20) {
      if (payload_type_bit_size == 16) {
         assert(nj->devinfo->ver >= 11);
         simd_mode = simd_width <= 8 ? GFX10_SAMPLER_SIMD_MODE_SIMD8H :
                                       GFX10_SAMPLER_SIMD_MODE_SIMD16H;
      } else {
         simd_mode = simd_width <= 8 ? BRW_SAMPLER_SIMD_MODE_SIMD8 :
                                       BRW_SAMPLER_SIMD_MODE_SIMD16;
      }
   } else {
      if (payload_type_bit_size == 16) {
         simd_mode = simd_width <= 16 ? XE2_SAMPLER_SIMD_MODE_SIMD16H :
                                        XE2_SAMPLER_SIMD_MODE_SIMD32H;
      } else {
         simd_mode = simd_width <= 16 ? XE2_SAMPLER_SIMD_MODE_SIMD16 :
                                        XE2_SAMPLER_SIMD_MODE_SIMD32;
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
      desc = brw_sampler_desc(nj->devinfo, GFX9_BTI_BINDLESS, sampler_imm,
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

   if (n_sources > 2 || !jay_is_null(header)) {
      for (unsigned i = 0; i < n_sources; ++i) {
         payload[i] =
            jay_src_as_strided(b, payload[i], 1, payload_uniform ? UGPR : GPR);
      }
   }

   enum jay_type src_type = jay_type(JAY_TYPE_U, payload_type_bit_size);
   jay_SEND(b, .sfid = BRW_SFID_SAMPLER, .msg_desc = desc, .desc = desc_src,
            .ex_desc = desc_ex_src, .header = header, .srcs = payload,
            .nr_srcs = n_sources, .type = JAY_TYPE_U32,
            .src_type = { src_type }, .dst = tmp, .uniform = payload_uniform,
            .bindless = surface_bindless);

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
      // TODO: Do we want a predicated EOT here, or a jump to the end?
      assert(!"TODO: implement HALT");
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
            jay_INDETERMINATE(&nj->bld, jay_extract(def, c));
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
   loop_header->loop_header = true;

   /* The current block falls through to the start of the loop */
   jay_block_add_successor(nj->current_block, loop_header, GPR);

   /* Emit the loop body */
   nj->after_block = loop_header;
   jay_emit_cf_list(nj, &nloop->body);

   /* Emit the backedge */
   jay_inst *jump = jay_block_ending_jump(nj->current_block);
   if (jump && jump->op == JAY_OPCODE_BREAK) {
      jump->op = JAY_OPCODE_LOOP_ONCE;
   } else {
      jay_block_add_successor(nj->current_block, loop_header, GPR);
      jay_WHILE(b);
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

   if (mesa_shader_stage_is_compute(nj->nir->info.stage)) {
      /* Vectorized copy into the EOT register. Not necessary for correctness
       * but keeps RA from inserting 16 scalar copies instead.
       */
      jay_def copy = jay_alloc_def(b, UGPR, jay_ugpr_per_grf(b->shader));
      jay_MOV(b, copy, nj->payload.u0);

      jay_SEND(b, .sfid = BRW_SFID_MESSAGE_GATEWAY, .eot = true, .msg_desc = 0,
               .srcs = &copy, .nr_srcs = 1, .type = JAY_TYPE_U32,
               .uniform = true);
   } else if (nj->nir->info.stage == MESA_SHADER_VERTEX) {
      jay_block *block = jay_last_block(nj->f);
      jay_inst *I = jay_last_inst(block);

      /* TODO: What if this isn't the case? Do we need a no-op store...? */
      assert(I && I->op == JAY_OPCODE_SEND && jay_send_sfid(I) == BRW_SFID_URB);
      jay_set_send_eot(I, true);
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

   /* XXX: This is a hack to line up with the partition chosen in RA. This whole
    * thing needs an overhaul. Need to think harder about partitioning.
    */
   p->offsets[GPR] += 7;

   for (unsigned i = 0; i < (8 * nj->s->prog_data->vue.urb_read_length); ++i) {
      assert(i < ARRAY_SIZE(nj->payload.vs.attributes));
      nj->payload.vs.attributes[i] = read_payload(p, GPR);
   }

   setup_payload_push(nj, p);
}

static void
setup_compute_payload(struct nir_to_jay_state *nj, struct payload_builder *p)
{
   assert(!nj->s->prog_data->cs.generate_local_id);
   assert(!nj->s->prog_data->cs.uses_btd_stack_ids);

   nj->payload.inline_data =
      read_vector_payload(p, UGPR, jay_ugpr_per_grf(nj->s));
}

static void
setup_fragment_payload(struct nir_to_jay_state *nj, struct payload_builder *p)
{
   jay_fs_payload *fs = &nj->payload.fs;

   if (nj->s->dispatch_width == 32) {
      nj->payload.u1 = read_vector_payload(p, UGPR, jay_ugpr_per_grf(nj->s));
   }

   setup_payload_push(nj, p);

   u_foreach_bit(i, nj->s->prog_data->fs.barycentric_interp_modes) {
      fs->bary[i] = read_vector_payload(p, GPR, 2);
   }

   if (nj->s->prog_data->fs.uses_src_depth) {
      fs->coord.z = read_payload(p, GPR);
   }

   if (nj->s->prog_data->fs.uses_src_w) {
      fs->coord.w = read_payload(p, GPR);
   }

   unsigned nr_attribs = 16 * 4; /* TODO */
   for (unsigned i = 0; i < nr_attribs; ++i) {
      jay_def comps[] = { read_payload(p, UGPR), read_payload(p, UGPR),
                          read_payload(p, UGPR) };

      /* The .yz components are swizzled in the hardware compared to NIR. */
      SWAP(comps[1], comps[2]);
      fs->deltas[i] = jay_collect_vectors(&nj->bld, comps, ARRAY_SIZE(comps));

      /* Padding */
      if ((i % 5) == 4) {
         read_payload(p, UGPR);
      }
   }

   if (BITSET_TEST(nj->nir->info.system_values_read, SYSTEM_VALUE_FRAG_COORD)) {
      jay_def t = jay_alloc_def(&nj->bld, GPR, 1);
      jay_def lo = jay_extract_range(nj->payload.u0, 10, 4);
      jay_EXPAND_QUAD(&nj->bld, t, lo, payload_u1(nj, 10, 4));
      fs->coord.xy = jay_OFFSET_PACKED_PIXEL_COORDS_u32(&nj->bld, t);
   }

   /* Due to complexities of the physical payload, the logical payload is split
    * into even/odd halves. Fix up the offsets and insert copies.
    */
   if (nj->s->dispatch_width == 32) {
      jay_builder *b = &nj->bld;
      jay_foreach_inst_in_block(nj->after_block, I) {
         if (I->op == JAY_OPCODE_PRELOAD && I->dst.file == GPR) {
            unsigned base = (jay_preload_reg(I) % 2) ? p->offsets[GPR] : 0;
            jay_set_preload_reg(I, base + (jay_preload_reg(I) / 2));
         }
      }

      b->cursor = jay_before_block(nj->after_block);
      jay_DESWIZZLE(b, p->offsets[GPR]);
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
   case MESA_SHADER_FRAGMENT:
      setup_fragment_payload(nj, &p);
      break;
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
      setup_compute_payload(nj, &p);
      break;
   default:
      UNREACHABLE("unimplemented shader stages");
   }

   /* Lane ID calculations require &W and therefore are calculated in
    * uniform control flow to sidestep RA problems. The easy solution is
    * calculating the lane ID in the first block.
    *
    * XXX: This doesn't work for multi-function. Reconsider.
    */
   nj->payload.lane_id = jay_LANE_ID_8_u16(b);

   for (unsigned i = 8; i < s->dispatch_width; i *= 2) {
      nj->payload.lane_id = jay_LANE_ID_EXPAND_u16(b, nj->payload.lane_id, i);
   }
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

   jay_emit_cf_list(&nj, &impl->body);
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

      /* XXX: Write a real cycle model */
      stats->cycles++;
   }

   stats->spills = s->spills;
   stats->fills = s->fills;
   stats->sends -= (s->spills + s->fills);
}

struct jay_shader_bin *
jay_compile(const struct intel_device_info *devinfo,
            void *mem_ctx,
            nir_shader *nir,
            union brw_any_prog_data *prog_data,
            union brw_any_prog_key *key)
{
   jay_debug = debug_get_option_jay_debug();
   bool debug =
      INTEL_DEBUG(intel_debug_flag_for_shader_stage(nir->info.stage)) &&
      !(nir->info.internal || NIR_DEBUG(PRINT_INTERNAL));

   unsigned simd_width = jay_process_nir(devinfo, nir, prog_data, key);

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

   JAY_PASS(s, jay_assign_flags);
   if (!(jay_debug & JAY_DBG_NOOPT)) {
      JAY_PASS(s, jay_opt_dead_code);
   }

   JAY_PASS(s, jay_lower_pre_ra);
   JAY_PASS(s, jay_partition_grf);
   JAY_PASS(s, jay_register_allocate);
   JAY_PASS(s, jay_lower_post_ra);
   JAY_PASS(s, jay_insert_fp_mode, nir->info.float_controls_execution_mode,
            nir->info.bit_sizes_float);

   if (!(jay_debug & JAY_DBG_NOOPT)) {
      JAY_PASS(s, jay_opt_predicate);
      JAY_PASS(s, jay_assign_accumulators);
   }

   JAY_PASS(s, jay_lower_scoreboard);

   if (debug) {
      fprintf(stdout, "Jay shader (post-RA):\n\n");
      jay_print(stdout, s);
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
