/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "gen.h"
#include "gen_names.h"

#include "dev/intel_device_info.h"

enum {
   GEN_INVALID_HW_REG_TYPE = 0b1111,
};

/* This is almost always called with a numeric constant argument, so
 * make things easy to evaluate at compile time:
 */
static inline unsigned cvt(unsigned val)
{
   switch (val) {
   case 0: return 0;
   case 1: return 1;
   case 2: return 2;
   case 4: return 3;
   case 8: return 4;
   case 16: return 5;
   case 32: return 6;
   }
   return 0;
}

static inline unsigned
gen_implied_width_for_3src_a1(unsigned v, unsigned h)
{
   /* "Regioning Rules for Align1 Ternary Operations" */

   /* TODO: Add remaining rules and de-duplicate with brw_disasm.c */

   if (v == 0) return 1;
   if (h == 0) return v;
   return v/h;
}

static inline bool
is_null(const gen_operand &o)
{
   return !o.indirect &&
          o.file == GEN_ARF &&
          o.nr == GEN_ARF_NULL;
}

static inline bool
is_accumulator(const gen_operand &o)
{
   return !o.indirect &&
          o.file == GEN_ARF &&
          (o.nr & 0xF0) == GEN_ARF_ACCUMULATOR;
}

enum ENUM_PACKED gen_format {
   GEN_FORMAT_BASIC_ONE_SRC,
   GEN_FORMAT_BASIC_TWO_SRC,
   GEN_FORMAT_BASIC_THREE_SRC,
   GEN_FORMAT_DPAS_THREE_SRC,
   GEN_FORMAT_SEND,
   GEN_FORMAT_BRANCH_ONE_SRC,
   GEN_FORMAT_BRANCH_TWO_SRC,
   GEN_FORMAT_ILLEGAL,
   GEN_FORMAT_NOP,
};

#include "gen_opcodes_private.h"

inline gen_format
gen_inst_format(const gen_opcode op)
{
   return gen_opcode_format(op);
}

/* This needs to be applied to a gen_range to be used.
 * Avoids making the mistake of using fields directly.
 */
struct gen_sub_range {
   unsigned hi;
   unsigned lo;
};

template <int N>
struct gen_sub_ranges {
   gen_sub_range ranges[N];

   constexpr
   const gen_sub_range&
   operator[](std::size_t i) const
   {
      assert(i < N);
      return ranges[i];
   }

   constexpr
   operator const gen_sub_range&() const
   {
      static_assert(N == 1, "split sub ranges cannot be cast to "
                    "gen_sub_range");
      return ranges[0];
   }
};

struct gen_range {
   unsigned hi;
   unsigned lo;

   gen_range
   operator()(unsigned rel) const
   {
      assert(lo <= hi);
      assert(rel <= hi - lo);
      return { lo + rel, lo + rel };
   }

   gen_range
   operator()(unsigned rel_hi, unsigned rel_lo) const
   {
      /* TODO: Move some of these assertions to user code (set/get). */
      assert(lo <= hi);
      assert(rel_lo <= rel_hi);
      assert(rel_lo <= hi - lo);
      assert(rel_hi <= hi - lo);
      return { lo + rel_hi, lo + rel_lo };
   }

   constexpr
   gen_range
   operator()(gen_sub_range rel) const
   {
      assert(lo <= hi);
      assert(rel.lo <= rel.hi);
      assert(rel.lo <= hi - lo);
      assert(rel.hi <= hi - lo);
      return { lo + rel.hi, lo + rel.lo };
   }

   constexpr
   gen_range
   operator()(gen_sub_ranges<1> rel) const
   {
      return operator()(rel.ranges[0]);
   }

   constexpr bool
   operator==(const gen_range &other) const
   {
      return hi == other.hi && lo == other.lo;
   }
};

template <int N>
struct gen_ranges {
   const gen_range ranges[N];

   constexpr
   const gen_range&
   operator[](std::size_t i) const
   {
      assert(i < N);
      return ranges[i];
   }

   constexpr
   gen_range
   operator()(unsigned rel_hi, unsigned rel_lo) const
   {
      return ((const gen_range&)*this)(rel_hi, rel_lo);
   }

   constexpr
   gen_range
   operator()(gen_sub_ranges<1> rel) const
   {
      return ((const gen_range&)*this)(rel);
   }

   constexpr
   operator const gen_range&() const
   {
      static_assert(N == 1, "split ranges cannot be cast to gen_range");
      return ranges[0];
   }
};

inline bool
gen_inst_is_send(const gen_inst *inst)
{
   return gen_inst_format(inst->opcode) == GEN_FORMAT_SEND;
}

inline bool
gen_inst_is_split_send(const intel_device_info *devinfo, const gen_inst *inst)
{
   switch (inst->opcode) {
   case GEN_OP_SEND:
   case GEN_OP_SENDC:
      return devinfo->ver >= 12;

   case GEN_OP_SENDS:
   case GEN_OP_SENDSC:
      return true;

   default:
      return false;
   }
}

inline bool
gen_inst_has_dst(const gen_opcode op)
{
   return gen_opcode_has_dst(op);
}

inline bool
gen_inst_has_saturate(const gen_format format, const gen_inst *inst)
{
   return format == GEN_FORMAT_BASIC_ONE_SRC ||
          format == GEN_FORMAT_BASIC_TWO_SRC ||
          (format == GEN_FORMAT_BASIC_THREE_SRC && inst->opcode != GEN_OP_BFN) ||
          format == GEN_FORMAT_DPAS_THREE_SRC;
}

inline bool
gen_inst_has_cond_modifier(const intel_device_info *devinfo,
                           const gen_format format, const gen_inst *inst)
{
   switch (format) {
   case GEN_FORMAT_BASIC_ONE_SRC:
      if (inst->opcode == GEN_OP_SYNC)
         return false;
      /* On Gfx12+, a one-src instruction with a 64-bit immediate overlays
       * the cmod bits with the high half of the immediate, so the cmod
       * field is not actually encoded.
       */
      if (devinfo->ver >= 12 &&
          inst->src[0].file == GEN_IMM &&
          gen_type_size_bytes(inst->src[0].type) >= 8)
         return false;
      return true;

   case GEN_FORMAT_BASIC_TWO_SRC:
      return inst->opcode != GEN_OP_MATH;

   case GEN_FORMAT_BASIC_THREE_SRC:
   case GEN_FORMAT_DPAS_THREE_SRC:
      return true;

   default:
      return false;
   }
}

#define STRIDE(stride) (stride != 0 ? 1 << ((stride) - 1) : 0)

enum {
   GEN_3SRC_DST_HORIZONTAL_STRIDE_1 = 0,
   GEN_3SRC_DST_HORIZONTAL_STRIDE_2 = 1,
};

inline unsigned
DST_STRIDE_3SRC(unsigned hstride)
{
   switch (hstride) {
   case GEN_3SRC_DST_HORIZONTAL_STRIDE_1: return 1;
   case GEN_3SRC_DST_HORIZONTAL_STRIDE_2: return 2;
   }
   UNREACHABLE("invalid hstride");
}

enum {
   GEN_3SRC_VERTICAL_STRIDE_0 = 0,
   GEN_3SRC_VERTICAL_STRIDE_1 = 1,
   GEN_3SRC_VERTICAL_STRIDE_4 = 2,
   GEN_3SRC_VERTICAL_STRIDE_8 = 3,
};

inline unsigned
DECODE_VSTRIDE_3SRC(unsigned vstride)
{
   switch (vstride) {
   case GEN_3SRC_VERTICAL_STRIDE_0: return 0;
   case GEN_3SRC_VERTICAL_STRIDE_1: return 1;
   case GEN_3SRC_VERTICAL_STRIDE_4: return 4;
   case GEN_3SRC_VERTICAL_STRIDE_8: return 8;
   }
   UNREACHABLE("invalid vstride");
}

inline unsigned
ENCODE_VSTRIDE_3SRC(unsigned vstride)
{
   switch (vstride) {
   case 0:  return GEN_3SRC_VERTICAL_STRIDE_0;
   case 1:  return GEN_3SRC_VERTICAL_STRIDE_1;
   case 2:  return GEN_3SRC_VERTICAL_STRIDE_1;
   case 4:  return GEN_3SRC_VERTICAL_STRIDE_4;
   case 8:  return GEN_3SRC_VERTICAL_STRIDE_8;
   case 16: return GEN_3SRC_VERTICAL_STRIDE_8;
   }
   UNREACHABLE("invalid vstride");
}

inline bool
gen_region_is_scalar(const gen_region rgn)
{
   return rgn.vstride == 0 &&
          rgn.width == 1 &&
          rgn.hstride == 0;
}

/**
 * Returns whether a region is packed
 *
 * A region is packed if its elements are adjacent in memory, with no
 * intervening space, no overlap, and no replicated values.
 */
inline bool
gen_region_is_packed(const gen_region rgn)
{
   if (rgn.vstride == rgn.width) {
      if (rgn.vstride == 1)
         return rgn.hstride == 0;
      else
         return rgn.hstride == 1;
   }

   return false;
}

/**
 * Returns whether a region is linear
 *
 * A region is linear if its elements do not overlap and are not replicated.
 * Unlike a packed region, intervening space (i.e. strided values) is allowed.
 */
inline bool
gen_region_is_linear(const gen_region rgn)
{
   return (rgn.vstride == rgn.width * rgn.hstride) ||
          (rgn.hstride == 0 && rgn.width == 1);
}

static inline bool
gen_raw_is_compact(const void *raw_bytes)
{
   /* Bit 29 always contains whether instruction is compacted or not. */
   const uint32_t *raw = (const uint32_t *)raw_bytes;
   return raw[0] & BITFIELD_BIT(29);
}

inline bool
gen_region_is_scalar_or_linear(const gen_region rgn)
{
   return gen_region_is_scalar(rgn) || gen_region_is_linear(rgn);
}

static inline unsigned
gen_raw_get_opcode(const void *raw_bytes)
{
   /* Bits 6:0 always contain the hardware opcode. */
   const uint64_t *raw = (uint64_t *)raw_bytes;
   return *raw & 0x7fu;
}

bool gen_compact(gen_encode_params *params);
bool gen_decode_compact(gen_raw_compact_inst inst,
                        gen_inst *decoded);
void gen_uncompact(gen_decode_params *params, void *&uncompacted);
bool gen_uncompact_inst(const struct intel_device_info *devinfo,
                        const gen_raw_compact_inst &src,
                        gen_raw_inst &dst);

bool gen_encode_pre_xe(gen_encode_params *params);
bool gen_decode_pre_xe(gen_decode_params *params);
int gen_find_shader_size_pre_xe(const struct intel_device_info *devinfo,
                                const uint64_t *raw,
                                const uint64_t *raw_start,
                                const uint64_t *raw_end);

void gen_decode_inst_pre_xe(const intel_device_info *devinfo,
                            gen_inst *inst,
                            const gen_raw_inst *raw, char **error);

gen_reg_type
xe_decode_type(const intel_device_info *devinfo, gen_file file,
               unsigned hw_type);
gen_reg_type
pre_xe_decode_type(const intel_device_info *devinfo, gen_file file,
                   unsigned hw_type);
