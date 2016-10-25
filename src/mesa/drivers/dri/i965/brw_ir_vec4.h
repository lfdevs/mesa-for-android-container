/* -*- c++ -*- */
/*
 * Copyright © 2011-2015 Intel Corporation
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

#ifndef BRW_IR_VEC4_H
#define BRW_IR_VEC4_H

#include "brw_shader.h"
#include "brw_context.h"

namespace brw {

class dst_reg;

class src_reg : public backend_reg
{
public:
   DECLARE_RALLOC_CXX_OPERATORS(src_reg)

   void init();

   src_reg(enum brw_reg_file file, int nr, const glsl_type *type);
   src_reg();
   src_reg(struct ::brw_reg reg);

   bool equals(const src_reg &r) const;

   src_reg(class vec4_visitor *v, const struct glsl_type *type);
   src_reg(class vec4_visitor *v, const struct glsl_type *type, int size);

   explicit src_reg(const dst_reg &reg);

   src_reg *reladdr;
};

static inline src_reg
retype(src_reg reg, enum brw_reg_type type)
{
   reg.type = type;
   return reg;
}

static inline src_reg
offset(src_reg reg, unsigned delta)
{
   assert(delta == 0 ||
          (reg.file != ARF && reg.file != FIXED_GRF && reg.file != IMM));
   reg.offset += delta * (reg.file == UNIFORM ? 16 : REG_SIZE);
   return reg;
}

/**
 * Reswizzle a given source register.
 * \sa brw_swizzle().
 */
static inline src_reg
swizzle(src_reg reg, unsigned swizzle)
{
   if (reg.file == IMM)
      reg.ud = brw_swizzle_immediate(reg.type, reg.ud, swizzle);
   else
      reg.swizzle = brw_compose_swizzle(swizzle, reg.swizzle);

   return reg;
}

static inline src_reg
negate(src_reg reg)
{
   assert(reg.file != IMM);
   reg.negate = !reg.negate;
   return reg;
}

static inline bool
is_uniform(const src_reg &reg)
{
   return (reg.file == IMM || reg.file == UNIFORM || reg.is_null()) &&
          (!reg.reladdr || is_uniform(*reg.reladdr));
}

class dst_reg : public backend_reg
{
public:
   DECLARE_RALLOC_CXX_OPERATORS(dst_reg)

   void init();

   dst_reg();
   dst_reg(enum brw_reg_file file, int nr);
   dst_reg(enum brw_reg_file file, int nr, const glsl_type *type,
           unsigned writemask);
   dst_reg(enum brw_reg_file file, int nr, brw_reg_type type,
           unsigned writemask);
   dst_reg(struct ::brw_reg reg);
   dst_reg(class vec4_visitor *v, const struct glsl_type *type);

   explicit dst_reg(const src_reg &reg);

   bool equals(const dst_reg &r) const;

   src_reg *reladdr;
};

static inline dst_reg
retype(dst_reg reg, enum brw_reg_type type)
{
   reg.type = type;
   return reg;
}

static inline dst_reg
offset(dst_reg reg, unsigned delta)
{
   assert(delta == 0 ||
          (reg.file != ARF && reg.file != FIXED_GRF && reg.file != IMM));
   reg.offset += delta * (reg.file == UNIFORM ? 16 : REG_SIZE);
   return reg;
}

static inline dst_reg
writemask(dst_reg reg, unsigned mask)
{
   assert(reg.file != IMM);
   assert((reg.writemask & mask) != 0);
   reg.writemask &= mask;
   return reg;
}

/**
 * Return an integer identifying the discrete address space a register is
 * contained in.  A register is by definition fully contained in the single
 * reg_space it belongs to, so two registers with different reg_space ids are
 * guaranteed not to overlap.  Most register files are a single reg_space of
 * its own, only the VGRF file is composed of multiple discrete address
 * spaces, one for each VGRF allocation.
 */
static inline uint32_t
reg_space(const backend_reg &r)
{
   return r.file << 16 | (r.file == VGRF ? r.nr : 0);
}

/**
 * Return the base offset in bytes of a register relative to the start of its
 * reg_space().
 */
static inline unsigned
reg_offset(const backend_reg &r)
{
   return (r.file == VGRF || r.file == IMM ? 0 : r.nr) *
          (r.file == UNIFORM ? 16 : REG_SIZE) + r.offset +
          (r.file == ARF || r.file == FIXED_GRF ? r.subnr : 0);
}

/**
 * Return whether the register region starting at \p r and spanning \p dr
 * bytes could potentially overlap the register region starting at \p s and
 * spanning \p ds bytes.
 */
static inline bool
regions_overlap(const backend_reg &r, unsigned dr,
                const backend_reg &s, unsigned ds)
{
   if (r.file == MRF && (r.nr & BRW_MRF_COMPR4)) {
      /* COMPR4 regions are translated by the hardware during decompression
       * into two separate half-regions 4 MRFs apart from each other.
       */
      backend_reg t0 = r;
      t0.nr &= ~BRW_MRF_COMPR4;
      backend_reg t1 = t0;
      t1.offset += 4 * REG_SIZE;
      return regions_overlap(t0, dr / 2, s, ds) ||
             regions_overlap(t1, dr / 2, s, ds);

   } else if (s.file == MRF && (s.nr & BRW_MRF_COMPR4)) {
      return regions_overlap(s, ds, r, dr);

   } else {
      return reg_space(r) == reg_space(s) &&
             !(reg_offset(r) + dr <= reg_offset(s) ||
               reg_offset(s) + ds <= reg_offset(r));
   }
}

class vec4_instruction : public backend_instruction {
public:
   DECLARE_RALLOC_CXX_OPERATORS(vec4_instruction)

   vec4_instruction(enum opcode opcode,
                    const dst_reg &dst = dst_reg(),
                    const src_reg &src0 = src_reg(),
                    const src_reg &src1 = src_reg(),
                    const src_reg &src2 = src_reg());

   dst_reg dst;
   src_reg src[3];

   enum brw_urb_write_flags urb_write_flags;

   unsigned sol_binding; /**< gen6: SOL binding table index */
   bool sol_final_write; /**< gen6: send commit message */
   unsigned sol_vertex; /**< gen6: used for setting dst index in SVB header */

   bool is_send_from_grf();
   unsigned size_read(unsigned arg) const;
   bool can_reswizzle(const struct gen_device_info *devinfo, int dst_writemask,
                      int swizzle, int swizzle_mask);
   void reswizzle(int dst_writemask, int swizzle);
   bool can_do_source_mods(const struct gen_device_info *devinfo);
   bool can_do_writemask(const struct gen_device_info *devinfo);
   bool can_change_types() const;
   bool has_source_and_destination_hazard() const;

   bool reads_flag()
   {
      return predicate || opcode == VS_OPCODE_UNPACK_FLAGS_SIMD4X2;
   }

   bool reads_flag(unsigned c)
   {
      if (opcode == VS_OPCODE_UNPACK_FLAGS_SIMD4X2)
         return true;

      switch (predicate) {
      case BRW_PREDICATE_NONE:
         return false;
      case BRW_PREDICATE_ALIGN16_REPLICATE_X:
         return c == 0;
      case BRW_PREDICATE_ALIGN16_REPLICATE_Y:
         return c == 1;
      case BRW_PREDICATE_ALIGN16_REPLICATE_Z:
         return c == 2;
      case BRW_PREDICATE_ALIGN16_REPLICATE_W:
         return c == 3;
      default:
         return true;
      }
   }

   bool writes_flag()
   {
      return (conditional_mod && (opcode != BRW_OPCODE_SEL &&
                                  opcode != BRW_OPCODE_IF &&
                                  opcode != BRW_OPCODE_WHILE));
   }
};

/**
 * Make the execution of \p inst dependent on the evaluation of a possibly
 * inverted predicate.
 */
inline vec4_instruction *
set_predicate_inv(enum brw_predicate pred, bool inverse,
                  vec4_instruction *inst)
{
   inst->predicate = pred;
   inst->predicate_inverse = inverse;
   return inst;
}

/**
 * Make the execution of \p inst dependent on the evaluation of a predicate.
 */
inline vec4_instruction *
set_predicate(enum brw_predicate pred, vec4_instruction *inst)
{
   return set_predicate_inv(pred, false, inst);
}

/**
 * Write the result of evaluating the condition given by \p mod to a flag
 * register.
 */
inline vec4_instruction *
set_condmod(enum brw_conditional_mod mod, vec4_instruction *inst)
{
   inst->conditional_mod = mod;
   return inst;
}

/**
 * Clamp the result of \p inst to the saturation range of its destination
 * datatype.
 */
inline vec4_instruction *
set_saturate(bool saturate, vec4_instruction *inst)
{
   inst->saturate = saturate;
   return inst;
}

/**
 * Return the number of dataflow registers written by the instruction (either
 * fully or partially) counted from 'floor(reg_offset(inst->dst) /
 * register_size)'.  The somewhat arbitrary register size unit is 16B for the
 * UNIFORM and IMM files and 32B for all other files.
 */
inline unsigned
regs_written(const vec4_instruction *inst)
{
   assert(inst->dst.file != UNIFORM && inst->dst.file != IMM);
   return DIV_ROUND_UP(reg_offset(inst->dst) % REG_SIZE + inst->size_written,
                       REG_SIZE);
}

/**
 * Return the number of dataflow registers read by the instruction (either
 * fully or partially) counted from 'floor(reg_offset(inst->src[i]) /
 * register_size)'.  The somewhat arbitrary register size unit is 16B for the
 * UNIFORM and IMM files and 32B for all other files.
 */
inline unsigned
regs_read(const vec4_instruction *inst, unsigned i)
{
   const unsigned reg_size =
      inst->src[i].file == UNIFORM || inst->src[i].file == IMM ? 16 : REG_SIZE;
   return DIV_ROUND_UP(reg_offset(inst->src[i]) % reg_size + inst->size_read(i),
                       reg_size);
}

} /* namespace brw */

#endif
