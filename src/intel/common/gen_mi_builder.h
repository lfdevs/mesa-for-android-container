/*
 * Copyright © 2019 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef GEN_MI_BUILDER_H
#define GEN_MI_BUILDER_H

#include "genxml/genX_bits.h"
#include "util/bitscan.h"
#include "util/fast_idiv_by_const.h"
#include "util/u_math.h"

#ifndef GEN_MI_BUILDER_NUM_ALLOC_GPRS
/** The number of GPRs the MI builder is allowed to allocate
 *
 * This may be set by a user of this API so that it can reserve some GPRs at
 * the top end for its own use.
 */
#define GEN_MI_BUILDER_NUM_ALLOC_GPRS 16
#endif

/** These must be defined by the user of the builder
 *
 * void *__gen_get_batch_dwords(__gen_user_data *user_data,
 *                              unsigned num_dwords);
 *
 * __gen_address_type
 * __gen_address_offset(__gen_address_type addr, uint64_t offset);
 *
 *
 * If self-modifying batches are supported, we must be able to pass batch
 * addresses around as void*s so pinning as well as batch chaining or some
 * other mechanism for ensuring batch pointers remain valid during building is
 * required. The following function must also be defined, it returns an
 * address in canonical form:
 *
 * uint64_t
 * __gen_get_batch_address(__gen_user_data *user_data, void *location);
 *
 * Also, __gen_combine_address must accept a location value of NULL and return
 * a fully valid 64-bit address.
 */

/*
 * Start of the actual MI builder
 */

#define __genxml_cmd_length(cmd) cmd ## _length
#define __genxml_cmd_header(cmd) cmd ## _header
#define __genxml_cmd_pack(cmd) cmd ## _pack

#define gen_mi_builder_pack(b, cmd, dst, name)                          \
   for (struct cmd name = { __genxml_cmd_header(cmd) },                 \
        *_dst = (struct cmd *)(dst); __builtin_expect(_dst != NULL, 1); \
        __genxml_cmd_pack(cmd)((b)->user_data, (void *)_dst, &name),    \
        _dst = NULL)

#define gen_mi_builder_emit(b, cmd, name)                               \
   gen_mi_builder_pack((b), cmd, __gen_get_batch_dwords((b)->user_data, __genxml_cmd_length(cmd)), name)


enum gen_mi_value_type {
   GEN_MI_VALUE_TYPE_IMM,
   GEN_MI_VALUE_TYPE_MEM32,
   GEN_MI_VALUE_TYPE_MEM64,
   GEN_MI_VALUE_TYPE_REG32,
   GEN_MI_VALUE_TYPE_REG64,
};

struct gen_mi_value {
   enum gen_mi_value_type type;

   union {
      uint64_t imm;
      __gen_address_type addr;
      uint32_t reg;
   };

#if GEN_GEN >= 7 || GEN_IS_HASWELL
   bool invert;
#endif
};

struct gen_mi_reg_num {
   uint32_t num;
#if GEN_GEN >= 11
   bool cs;
#endif
};

static inline struct gen_mi_reg_num
gen_mi_adjust_reg_num(uint32_t reg)
{
#if GEN_GEN >= 11
   bool cs = reg >= 0x2000 && reg < 0x4000;
   return (struct gen_mi_reg_num) {
      .num = reg - (cs ? 0x2000 : 0),
      .cs = cs,
   };
#else
   return (struct gen_mi_reg_num) { .num = reg, };
#endif
}

#if GEN_GEN >= 9
#define GEN_MI_BUILDER_MAX_MATH_DWORDS 256
#else
#define GEN_MI_BUILDER_MAX_MATH_DWORDS 64
#endif

struct gen_mi_builder {
   __gen_user_data *user_data;

#if GEN_GEN >= 8 || GEN_IS_HASWELL
   uint32_t gprs;
   uint8_t gpr_refs[GEN_MI_BUILDER_NUM_ALLOC_GPRS];

   unsigned num_math_dwords;
   uint32_t math_dwords[GEN_MI_BUILDER_MAX_MATH_DWORDS];
#endif
};

static inline void
gen_mi_builder_init(struct gen_mi_builder *b, __gen_user_data *user_data)
{
   memset(b, 0, sizeof(*b));
   b->user_data = user_data;

#if GEN_GEN >= 8 || GEN_IS_HASWELL
   b->gprs = 0;
   b->num_math_dwords = 0;
#endif
}

static inline void
gen_mi_builder_flush_math(struct gen_mi_builder *b)
{
#if GEN_GEN >= 8 || GEN_IS_HASWELL
   if (b->num_math_dwords == 0)
      return;

   uint32_t *dw = (uint32_t *)__gen_get_batch_dwords(b->user_data,
                                                     1 + b->num_math_dwords);
   gen_mi_builder_pack(b, GENX(MI_MATH), dw, math) {
      math.DWordLength = 1 + b->num_math_dwords - GENX(MI_MATH_length_bias);
   }
   memcpy(dw + 1, b->math_dwords, b->num_math_dwords * sizeof(uint32_t));
   b->num_math_dwords = 0;
#endif
}

#define _GEN_MI_BUILDER_GPR_BASE 0x2600
/* The actual hardware limit on GPRs */
#define _GEN_MI_BUILDER_NUM_HW_GPRS 16

#if GEN_GEN >= 8 || GEN_IS_HASWELL

static inline bool
gen_mi_value_is_gpr(struct gen_mi_value val)
{
   return (val.type == GEN_MI_VALUE_TYPE_REG32 ||
           val.type == GEN_MI_VALUE_TYPE_REG64) &&
          val.reg >= _GEN_MI_BUILDER_GPR_BASE &&
          val.reg < _GEN_MI_BUILDER_GPR_BASE +
                    _GEN_MI_BUILDER_NUM_HW_GPRS * 8;
}

static inline bool
_gen_mi_value_is_allocated_gpr(struct gen_mi_value val)
{
   return (val.type == GEN_MI_VALUE_TYPE_REG32 ||
           val.type == GEN_MI_VALUE_TYPE_REG64) &&
          val.reg >= _GEN_MI_BUILDER_GPR_BASE &&
          val.reg < _GEN_MI_BUILDER_GPR_BASE +
                    GEN_MI_BUILDER_NUM_ALLOC_GPRS * 8;
}

static inline uint32_t
_gen_mi_value_as_gpr(struct gen_mi_value val)
{
   assert(gen_mi_value_is_gpr(val));
   assert(val.reg % 8 == 0);
   return (val.reg - _GEN_MI_BUILDER_GPR_BASE) / 8;
}

static inline struct gen_mi_value
gen_mi_new_gpr(struct gen_mi_builder *b)
{
   unsigned gpr = ffs(~b->gprs) - 1;
   assert(gpr < GEN_MI_BUILDER_NUM_ALLOC_GPRS);
   assert(b->gpr_refs[gpr] == 0);
   b->gprs |= (1u << gpr);
   b->gpr_refs[gpr] = 1;

   return (struct gen_mi_value) {
      .type = GEN_MI_VALUE_TYPE_REG64,
      .reg = _GEN_MI_BUILDER_GPR_BASE + gpr * 8,
   };
}
#endif /* GEN_GEN >= 8 || GEN_IS_HASWELL */

/** Take a reference to a gen_mi_value
 *
 * The MI builder uses reference counting to automatically free ALU GPRs for
 * re-use in calculations.  All gen_mi_* math functions consume the reference
 * they are handed for each source and return a reference to a value which the
 * caller must consume.  In particular, if you pas the same value into a
 * single gen_mi_* math function twice (say to add a number to itself), you
 * are responsible for calling gen_mi_value_ref() to get a second reference
 * because the gen_mi_* math function will consume it twice.
 */
static inline struct gen_mi_value
gen_mi_value_ref(struct gen_mi_builder *b, struct gen_mi_value val)
{
#if GEN_GEN >= 8 || GEN_IS_HASWELL
   if (_gen_mi_value_is_allocated_gpr(val)) {
      unsigned gpr = _gen_mi_value_as_gpr(val);
      assert(gpr < GEN_MI_BUILDER_NUM_ALLOC_GPRS);
      assert(b->gprs & (1u << gpr));
      assert(b->gpr_refs[gpr] < UINT8_MAX);
      b->gpr_refs[gpr]++;
   }
#endif /* GEN_GEN >= 8 || GEN_IS_HASWELL */

   return val;
}

/** Drop a reference to a gen_mi_value
 *
 * See also gen_mi_value_ref.
 */
static inline void
gen_mi_value_unref(struct gen_mi_builder *b, struct gen_mi_value val)
{
#if GEN_GEN >= 8 || GEN_IS_HASWELL
   if (_gen_mi_value_is_allocated_gpr(val)) {
      unsigned gpr = _gen_mi_value_as_gpr(val);
      assert(gpr < GEN_MI_BUILDER_NUM_ALLOC_GPRS);
      assert(b->gprs & (1u << gpr));
      assert(b->gpr_refs[gpr] > 0);
      if (--b->gpr_refs[gpr] == 0)
         b->gprs &= ~(1u << gpr);
   }
#endif /* GEN_GEN >= 8 || GEN_IS_HASWELL */
}

static inline struct gen_mi_value
gen_mi_imm(uint64_t imm)
{
   return (struct gen_mi_value) {
      .type = GEN_MI_VALUE_TYPE_IMM,
      .imm = imm,
   };
}

static inline struct gen_mi_value
gen_mi_reg32(uint32_t reg)
{
   struct gen_mi_value val = {
      .type = GEN_MI_VALUE_TYPE_REG32,
      .reg = reg,
   };
#if GEN_GEN >= 8 || GEN_IS_HASWELL
   assert(!_gen_mi_value_is_allocated_gpr(val));
#endif
   return val;
}

static inline struct gen_mi_value
gen_mi_reg64(uint32_t reg)
{
   struct gen_mi_value val = {
      .type = GEN_MI_VALUE_TYPE_REG64,
      .reg = reg,
   };
#if GEN_GEN >= 8 || GEN_IS_HASWELL
   assert(!_gen_mi_value_is_allocated_gpr(val));
#endif
   return val;
}

static inline struct gen_mi_value
gen_mi_mem32(__gen_address_type addr)
{
   return (struct gen_mi_value) {
      .type = GEN_MI_VALUE_TYPE_MEM32,
      .addr = addr,
   };
}

static inline struct gen_mi_value
gen_mi_mem64(__gen_address_type addr)
{
   return (struct gen_mi_value) {
      .type = GEN_MI_VALUE_TYPE_MEM64,
      .addr = addr,
   };
}

static inline struct gen_mi_value
gen_mi_value_half(struct gen_mi_value value, bool top_32_bits)
{
   switch (value.type) {
   case GEN_MI_VALUE_TYPE_IMM:
      if (top_32_bits)
         value.imm >>= 32;
      else
         value.imm &= 0xffffffffu;
      return value;

   case GEN_MI_VALUE_TYPE_MEM32:
      assert(!top_32_bits);
      return value;

   case GEN_MI_VALUE_TYPE_MEM64:
      if (top_32_bits)
         value.addr = __gen_address_offset(value.addr, 4);
      value.type = GEN_MI_VALUE_TYPE_MEM32;
      return value;

   case GEN_MI_VALUE_TYPE_REG32:
      assert(!top_32_bits);
      return value;

   case GEN_MI_VALUE_TYPE_REG64:
      if (top_32_bits)
         value.reg += 4;
      value.type = GEN_MI_VALUE_TYPE_REG32;
      return value;
   }

   unreachable("Invalid gen_mi_value type");
}

static inline void
_gen_mi_copy_no_unref(struct gen_mi_builder *b,
                      struct gen_mi_value dst, struct gen_mi_value src)
{
#if GEN_GEN >= 7 || GEN_IS_HASWELL
   /* TODO: We could handle src.invert by emitting a bit of math if we really
    * wanted to.
    */
   assert(!dst.invert && !src.invert);
#endif
   gen_mi_builder_flush_math(b);

   switch (dst.type) {
   case GEN_MI_VALUE_TYPE_IMM:
      unreachable("Cannot copy to an immediate");

   case GEN_MI_VALUE_TYPE_MEM64:
   case GEN_MI_VALUE_TYPE_REG64:
      /* If the destination is 64 bits, we have to copy in two halves */
      _gen_mi_copy_no_unref(b, gen_mi_value_half(dst, false),
                               gen_mi_value_half(src, false));
      switch (src.type) {
      case GEN_MI_VALUE_TYPE_IMM:
      case GEN_MI_VALUE_TYPE_MEM64:
      case GEN_MI_VALUE_TYPE_REG64:
         /* TODO: Use MI_STORE_DATA_IMM::StoreQWord when we have it */
         _gen_mi_copy_no_unref(b, gen_mi_value_half(dst, true),
                                  gen_mi_value_half(src, true));
         break;
      default:
         _gen_mi_copy_no_unref(b, gen_mi_value_half(dst, true),
                                  gen_mi_imm(0));
         break;
      }
      break;

   case GEN_MI_VALUE_TYPE_MEM32:
      switch (src.type) {
      case GEN_MI_VALUE_TYPE_IMM:
         gen_mi_builder_emit(b, GENX(MI_STORE_DATA_IMM), sdi) {
            sdi.Address = dst.addr;
#if GEN_GEN >= 12
            sdi.ForceWriteCompletionCheck = true;
#endif
            sdi.ImmediateData = src.imm;
         }
         break;

      case GEN_MI_VALUE_TYPE_MEM32:
      case GEN_MI_VALUE_TYPE_MEM64:
#if GEN_GEN >= 8
         gen_mi_builder_emit(b, GENX(MI_COPY_MEM_MEM), cmm) {
            cmm.DestinationMemoryAddress = dst.addr;
            cmm.SourceMemoryAddress = src.addr;
         }
#elif GEN_IS_HASWELL
         {
            struct gen_mi_value tmp = gen_mi_new_gpr(b);
            _gen_mi_copy_no_unref(b, tmp, src);
            _gen_mi_copy_no_unref(b, dst, tmp);
            gen_mi_value_unref(b, tmp);
         }
#else
         unreachable("Cannot do mem <-> mem copy on IVB and earlier");
#endif
         break;

      case GEN_MI_VALUE_TYPE_REG32:
      case GEN_MI_VALUE_TYPE_REG64:
         gen_mi_builder_emit(b, GENX(MI_STORE_REGISTER_MEM), srm) {
            struct gen_mi_reg_num reg = gen_mi_adjust_reg_num(src.reg);
            srm.RegisterAddress = reg.num;
#if GEN_GEN >= 11
            srm.AddCSMMIOStartOffset = reg.cs;
#endif
            srm.MemoryAddress = dst.addr;
         }
         break;

      default:
         unreachable("Invalid gen_mi_value type");
      }
      break;

   case GEN_MI_VALUE_TYPE_REG32:
      switch (src.type) {
      case GEN_MI_VALUE_TYPE_IMM:
         gen_mi_builder_emit(b, GENX(MI_LOAD_REGISTER_IMM), lri) {
            struct gen_mi_reg_num reg = gen_mi_adjust_reg_num(dst.reg);
            lri.RegisterOffset = reg.num;
#if GEN_GEN >= 11
            lri.AddCSMMIOStartOffset = reg.cs;
#endif
            lri.DataDWord = src.imm;
         }
         break;

      case GEN_MI_VALUE_TYPE_MEM32:
      case GEN_MI_VALUE_TYPE_MEM64:
         gen_mi_builder_emit(b, GENX(MI_LOAD_REGISTER_MEM), lrm) {
            struct gen_mi_reg_num reg = gen_mi_adjust_reg_num(dst.reg);
            lrm.RegisterAddress = reg.num;
#if GEN_GEN >= 11
            lrm.AddCSMMIOStartOffset = reg.cs;
#endif
            lrm.MemoryAddress = src.addr;
         }
         break;

      case GEN_MI_VALUE_TYPE_REG32:
      case GEN_MI_VALUE_TYPE_REG64:
#if GEN_GEN >= 8 || GEN_IS_HASWELL
         if (src.reg != dst.reg) {
            gen_mi_builder_emit(b, GENX(MI_LOAD_REGISTER_REG), lrr) {
               struct gen_mi_reg_num reg = gen_mi_adjust_reg_num(src.reg);
               lrr.SourceRegisterAddress = reg.num;
#if GEN_GEN >= 11
               lrr.AddCSMMIOStartOffsetSource = reg.cs;
#endif
               reg = gen_mi_adjust_reg_num(dst.reg);
               lrr.DestinationRegisterAddress = reg.num;
#if GEN_GEN >= 11
               lrr.AddCSMMIOStartOffsetDestination = reg.cs;
#endif
            }
         }
#else
         unreachable("Cannot do reg <-> reg copy on IVB and earlier");
#endif
         break;

      default:
         unreachable("Invalid gen_mi_value type");
      }
      break;

   default:
      unreachable("Invalid gen_mi_value type");
   }
}

/** Store the value in src to the value represented by dst
 *
 * If the bit size of src and dst mismatch, this function does an unsigned
 * integer cast.  If src has more bits than dst, it takes the bottom bits.  If
 * src has fewer bits then dst, it fills the top bits with zeros.
 *
 * This function consumes one reference for each of src and dst.
 */
static inline void
gen_mi_store(struct gen_mi_builder *b,
             struct gen_mi_value dst, struct gen_mi_value src)
{
   _gen_mi_copy_no_unref(b, dst, src);
   gen_mi_value_unref(b, src);
   gen_mi_value_unref(b, dst);
}

static inline void
gen_mi_memset(struct gen_mi_builder *b, __gen_address_type dst,
              uint32_t value, uint32_t size)
{
#if GEN_GEN >= 8 || GEN_IS_HASWELL
   assert(b->num_math_dwords == 0);
#endif

   /* This memset operates in units of dwords. */
   assert(size % 4 == 0);

   for (uint32_t i = 0; i < size; i += 4) {
      gen_mi_store(b, gen_mi_mem32(__gen_address_offset(dst, i)),
                      gen_mi_imm(value));
   }
}

/* NOTE: On IVB, this function stomps GEN7_3DPRIM_BASE_VERTEX */
static inline void
gen_mi_memcpy(struct gen_mi_builder *b, __gen_address_type dst,
              __gen_address_type src, uint32_t size)
{
#if GEN_GEN >= 8 || GEN_IS_HASWELL
   assert(b->num_math_dwords == 0);
#endif

   /* This memcpy operates in units of dwords. */
   assert(size % 4 == 0);

   for (uint32_t i = 0; i < size; i += 4) {
      struct gen_mi_value dst_val = gen_mi_mem32(__gen_address_offset(dst, i));
      struct gen_mi_value src_val = gen_mi_mem32(__gen_address_offset(src, i));
#if GEN_GEN >= 8 || GEN_IS_HASWELL
      gen_mi_store(b, dst_val, src_val);
#else
      /* IVB does not have a general purpose register for command streamer
       * commands. Therefore, we use an alternate temporary register.
       */
      struct gen_mi_value tmp_reg = gen_mi_reg32(0x2440); /* GEN7_3DPRIM_BASE_VERTEX */
      gen_mi_store(b, tmp_reg, src_val);
      gen_mi_store(b, dst_val, tmp_reg);
#endif
   }
}

/*
 * MI_MATH Section.  Only available on Haswell+
 */

#if GEN_GEN >= 8 || GEN_IS_HASWELL

/**
 * Perform a predicated store (assuming the condition is already loaded
 * in the MI_PREDICATE_RESULT register) of the value in src to the memory
 * location specified by dst.  Non-memory destinations are not supported.
 *
 * This function consumes one reference for each of src and dst.
 */
static inline void
gen_mi_store_if(struct gen_mi_builder *b,
                struct gen_mi_value dst,
                struct gen_mi_value src)
{
   assert(!dst.invert && !src.invert);

   gen_mi_builder_flush_math(b);

   /* We can only predicate MI_STORE_REGISTER_MEM, so restrict the
    * destination to be memory, and resolve the source to a temporary
    * register if it isn't in one already.
    */
   assert(dst.type == GEN_MI_VALUE_TYPE_MEM64 ||
          dst.type == GEN_MI_VALUE_TYPE_MEM32);

   if (src.type != GEN_MI_VALUE_TYPE_REG32 &&
       src.type != GEN_MI_VALUE_TYPE_REG64) {
      struct gen_mi_value tmp = gen_mi_new_gpr(b);
      _gen_mi_copy_no_unref(b, tmp, src);
      src = tmp;
   }

   if (dst.type == GEN_MI_VALUE_TYPE_MEM64) {
      gen_mi_builder_emit(b, GENX(MI_STORE_REGISTER_MEM), srm) {
         struct gen_mi_reg_num reg = gen_mi_adjust_reg_num(src.reg);
         srm.RegisterAddress = reg.num;
#if GEN_GEN >= 11
         srm.AddCSMMIOStartOffset = reg.cs;
#endif
         srm.MemoryAddress = dst.addr;
         srm.PredicateEnable = true;
      }
      gen_mi_builder_emit(b, GENX(MI_STORE_REGISTER_MEM), srm) {
         struct gen_mi_reg_num reg = gen_mi_adjust_reg_num(src.reg + 4);
         srm.RegisterAddress = reg.num;
#if GEN_GEN >= 11
         srm.AddCSMMIOStartOffset = reg.cs;
#endif
         srm.MemoryAddress = __gen_address_offset(dst.addr, 4);
         srm.PredicateEnable = true;
      }
   } else {
      gen_mi_builder_emit(b, GENX(MI_STORE_REGISTER_MEM), srm) {
         struct gen_mi_reg_num reg = gen_mi_adjust_reg_num(src.reg);
         srm.RegisterAddress = reg.num;
#if GEN_GEN >= 11
         srm.AddCSMMIOStartOffset = reg.cs;
#endif
         srm.MemoryAddress = dst.addr;
         srm.PredicateEnable = true;
      }
   }

   gen_mi_value_unref(b, src);
   gen_mi_value_unref(b, dst);
}

static inline void
_gen_mi_builder_push_math(struct gen_mi_builder *b,
                          const uint32_t *dwords,
                          unsigned num_dwords)
{
   assert(num_dwords < GEN_MI_BUILDER_MAX_MATH_DWORDS);
   if (b->num_math_dwords + num_dwords > GEN_MI_BUILDER_MAX_MATH_DWORDS)
      gen_mi_builder_flush_math(b);

   memcpy(&b->math_dwords[b->num_math_dwords],
          dwords, num_dwords * sizeof(*dwords));
   b->num_math_dwords += num_dwords;
}

static inline uint32_t
_gen_mi_pack_alu(uint32_t opcode, uint32_t operand1, uint32_t operand2)
{
   struct GENX(MI_MATH_ALU_INSTRUCTION) instr = {
      .Operand2 = operand2,
      .Operand1 = operand1,
      .ALUOpcode = opcode,
   };

   uint32_t dw;
   GENX(MI_MATH_ALU_INSTRUCTION_pack)(NULL, &dw, &instr);

   return dw;
}

static inline struct gen_mi_value
gen_mi_value_to_gpr(struct gen_mi_builder *b, struct gen_mi_value val)
{
   if (gen_mi_value_is_gpr(val))
      return val;

   /* Save off the invert flag because it makes copy() grumpy */
   bool invert = val.invert;
   val.invert = false;

   struct gen_mi_value tmp = gen_mi_new_gpr(b);
   _gen_mi_copy_no_unref(b, tmp, val);
   tmp.invert = invert;

   return tmp;
}

static inline uint32_t
_gen_mi_math_load_src(struct gen_mi_builder *b,
                      unsigned src, struct gen_mi_value *val)
{
   if (val->type == GEN_MI_VALUE_TYPE_IMM &&
       (val->imm == 0 || val->imm == UINT64_MAX)) {
      uint64_t imm = val->invert ? ~val->imm : val->imm;
      return _gen_mi_pack_alu(imm ? MI_ALU_LOAD1 : MI_ALU_LOAD0, src, 0);
   } else {
      *val = gen_mi_value_to_gpr(b, *val);
      return _gen_mi_pack_alu(val->invert ? MI_ALU_LOADINV : MI_ALU_LOAD,
                              src, _gen_mi_value_as_gpr(*val));
   }
}

static inline struct gen_mi_value
gen_mi_math_binop(struct gen_mi_builder *b, uint32_t opcode,
                  struct gen_mi_value src0, struct gen_mi_value src1,
                  uint32_t store_op, uint32_t store_src)
{
   struct gen_mi_value dst = gen_mi_new_gpr(b);

   uint32_t dw[4];
   dw[0] = _gen_mi_math_load_src(b, MI_ALU_SRCA, &src0);
   dw[1] = _gen_mi_math_load_src(b, MI_ALU_SRCB, &src1);
   dw[2] = _gen_mi_pack_alu(opcode, 0, 0);
   dw[3] = _gen_mi_pack_alu(store_op, _gen_mi_value_as_gpr(dst), store_src);
   _gen_mi_builder_push_math(b, dw, 4);

   gen_mi_value_unref(b, src0);
   gen_mi_value_unref(b, src1);

   return dst;
}

static inline struct gen_mi_value
gen_mi_inot(struct gen_mi_builder *b, struct gen_mi_value val)
{
   /* TODO These currently can't be passed into gen_mi_copy */
   val.invert = !val.invert;
   return val;
}

static inline struct gen_mi_value
gen_mi_iadd(struct gen_mi_builder *b,
            struct gen_mi_value src0, struct gen_mi_value src1)
{
   return gen_mi_math_binop(b, MI_ALU_ADD, src0, src1,
                            MI_ALU_STORE, MI_ALU_ACCU);
}

static inline struct gen_mi_value
gen_mi_iadd_imm(struct gen_mi_builder *b,
                struct gen_mi_value src, uint64_t N)
{
   if (N == 0)
      return src;

   return gen_mi_iadd(b, src, gen_mi_imm(N));
}

static inline struct gen_mi_value
gen_mi_isub(struct gen_mi_builder *b,
            struct gen_mi_value src0, struct gen_mi_value src1)
{
   return gen_mi_math_binop(b, MI_ALU_SUB, src0, src1,
                            MI_ALU_STORE, MI_ALU_ACCU);
}

static inline struct gen_mi_value
gen_mi_ult(struct gen_mi_builder *b,
           struct gen_mi_value src0, struct gen_mi_value src1)
{
   /* Compute "less than" by subtracting and storing the carry bit */
   return gen_mi_math_binop(b, MI_ALU_SUB, src0, src1,
                            MI_ALU_STORE, MI_ALU_CF);
}

static inline struct gen_mi_value
gen_mi_uge(struct gen_mi_builder *b,
           struct gen_mi_value src0, struct gen_mi_value src1)
{
   /* Compute "less than" by subtracting and storing the carry bit */
   return gen_mi_math_binop(b, MI_ALU_SUB, src0, src1,
                            MI_ALU_STOREINV, MI_ALU_CF);
}

static inline struct gen_mi_value
gen_mi_iand(struct gen_mi_builder *b,
            struct gen_mi_value src0, struct gen_mi_value src1)
{
   return gen_mi_math_binop(b, MI_ALU_AND, src0, src1,
                            MI_ALU_STORE, MI_ALU_ACCU);
}

/**
 * Returns (src != 0) ? 1 : 0.
 */
static inline struct gen_mi_value
gen_mi_nz(struct gen_mi_builder *b, struct gen_mi_value src)
{
   return gen_mi_math_binop(b, MI_ALU_ADD, src, gen_mi_imm(0),
                            MI_ALU_STOREINV, MI_ALU_ZF);
}

/**
 * Returns (src == 0) ? 1 : 0.
 */
static inline struct gen_mi_value
gen_mi_z(struct gen_mi_builder *b, struct gen_mi_value src)
{
   return gen_mi_math_binop(b, MI_ALU_ADD, src, gen_mi_imm(0),
                            MI_ALU_STORE, MI_ALU_ZF);
}

static inline struct gen_mi_value
gen_mi_ior(struct gen_mi_builder *b,
           struct gen_mi_value src0, struct gen_mi_value src1)
{
   return gen_mi_math_binop(b, MI_ALU_OR, src0, src1,
                            MI_ALU_STORE, MI_ALU_ACCU);
}

static inline struct gen_mi_value
gen_mi_imul_imm(struct gen_mi_builder *b,
                struct gen_mi_value src, uint32_t N)
{
   if (N == 0) {
      gen_mi_value_unref(b, src);
      return gen_mi_imm(0);
   }

   if (N == 1)
      return src;

   src = gen_mi_value_to_gpr(b, src);

   struct gen_mi_value res = gen_mi_value_ref(b, src);

   unsigned top_bit = 31 - __builtin_clz(N);
   for (int i = top_bit - 1; i >= 0; i--) {
      res = gen_mi_iadd(b, res, gen_mi_value_ref(b, res));
      if (N & (1 << i))
         res = gen_mi_iadd(b, res, gen_mi_value_ref(b, src));
   }

   gen_mi_value_unref(b, src);

   return res;
}

static inline struct gen_mi_value
gen_mi_ishl_imm(struct gen_mi_builder *b,
                struct gen_mi_value src, uint32_t shift)
{
   struct gen_mi_value res = gen_mi_value_to_gpr(b, src);

   for (unsigned i = 0; i < shift; i++)
      res = gen_mi_iadd(b, res, gen_mi_value_ref(b, res));

   return res;
}

static inline struct gen_mi_value
gen_mi_ushr32_imm(struct gen_mi_builder *b,
                  struct gen_mi_value src, uint32_t shift)
{
   /* We right-shift by left-shifting by 32 - shift and taking the top 32 bits
    * of the result.  This assumes the top 32 bits are zero.
    */
   if (shift > 64)
      return gen_mi_imm(0);

   if (shift > 32) {
      struct gen_mi_value tmp = gen_mi_new_gpr(b);
      _gen_mi_copy_no_unref(b, gen_mi_value_half(tmp, false),
                               gen_mi_value_half(src, true));
      _gen_mi_copy_no_unref(b, gen_mi_value_half(tmp, true), gen_mi_imm(0));
      gen_mi_value_unref(b, src);
      src = tmp;
      shift -= 32;
   }
   assert(shift <= 32);
   struct gen_mi_value tmp = gen_mi_ishl_imm(b, src, 32 - shift);
   struct gen_mi_value dst = gen_mi_new_gpr(b);
   _gen_mi_copy_no_unref(b, gen_mi_value_half(dst, false),
                            gen_mi_value_half(tmp, true));
   _gen_mi_copy_no_unref(b, gen_mi_value_half(dst, true), gen_mi_imm(0));
   gen_mi_value_unref(b, tmp);
   return dst;
}

static inline struct gen_mi_value
gen_mi_udiv32_imm(struct gen_mi_builder *b,
                  struct gen_mi_value N, uint32_t D)
{
   /* We implicitly assume that N is only a 32-bit value */
   if (D == 0) {
      /* This is invalid but we should do something */
      return gen_mi_imm(0);
   } else if (util_is_power_of_two_or_zero(D)) {
      return gen_mi_ushr32_imm(b, N, util_logbase2(D));
   } else {
      struct util_fast_udiv_info m = util_compute_fast_udiv_info(D, 32, 32);
      assert(m.multiplier <= UINT32_MAX);

      if (m.pre_shift)
         N = gen_mi_ushr32_imm(b, N, m.pre_shift);

      /* Do the 32x32 multiply  into gpr0 */
      N = gen_mi_imul_imm(b, N, m.multiplier);

      if (m.increment)
         N = gen_mi_iadd(b, N, gen_mi_imm(m.multiplier));

      N = gen_mi_ushr32_imm(b, N, 32);

      if (m.post_shift)
         N = gen_mi_ushr32_imm(b, N, m.post_shift);

      return N;
   }
}

#endif /* MI_MATH section */

/* This assumes addresses of strictly more than 32bits (aka. Gen8+). */
#if GEN_MI_BUILDER_CAN_WRITE_BATCH

struct gen_mi_address_token {
   /* Pointers to address memory fields in the batch. */
   uint64_t *ptrs[2];
};

static inline struct gen_mi_address_token
gen_mi_store_address(struct gen_mi_builder *b,
                     struct gen_mi_value addr_reg)
{
   gen_mi_builder_flush_math(b);

   assert(addr_reg.type == GEN_MI_VALUE_TYPE_REG64);

   struct gen_mi_address_token token = {};

   for (unsigned i = 0; i < 2; i++) {
      gen_mi_builder_emit(b, GENX(MI_STORE_REGISTER_MEM), srm) {
         srm.RegisterAddress = addr_reg.reg + (i * 4);

         const unsigned addr_dw =
            GENX(MI_STORE_REGISTER_MEM_MemoryAddress_start) / 8;
         token.ptrs[i] = (void *)_dst + addr_dw;
      }
   }

   gen_mi_value_unref(b, addr_reg);
   return token;
}

static inline void
gen_mi_self_mod_barrier(struct gen_mi_builder *b)
{
   /* First make sure all the memory writes from previous modifying commands
    * have landed. We want to do this before going through the CS cache,
    * otherwise we could be fetching memory that hasn't been written to yet.
    */
   gen_mi_builder_emit(b, GENX(PIPE_CONTROL), pc) {
      pc.CommandStreamerStallEnable = true;
   }
   /* Documentation says Gen11+ should be able to invalidate the command cache
    * but experiment show it doesn't work properly, so for now just get over
    * the CS prefetch.
    */
   for (uint32_t i = 0; i < 128; i++)
      gen_mi_builder_emit(b, GENX(MI_NOOP), noop);
}

static inline void
_gen_mi_resolve_address_token(struct gen_mi_builder *b,
                              struct gen_mi_address_token token,
                              void *batch_location)
{
   uint64_t addr_addr_u64 = __gen_get_batch_address(b->user_data,
                                                    batch_location);
   *(token.ptrs[0]) = addr_addr_u64;
   *(token.ptrs[1]) = addr_addr_u64 + 4;
}

#endif /* GEN_MI_BUILDER_CAN_WRITE_BATCH */

#endif /* GEN_MI_BUILDER_H */
