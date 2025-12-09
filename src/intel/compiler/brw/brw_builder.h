/* -*- c++ -*- */
/*
 * Copyright © 2010-2015 Intel Corporation
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

#pragma once

#include "brw_eu.h"
#include "brw_shader.h"
#include "brw_inst.h"

static inline brw_reg offset(const brw_reg &, const brw_builder &,
                             unsigned);

/**
 * Toolbox to assemble an BRW IR program out of individual instructions.
 */
class brw_builder {
public:
   /**
    * Construct an brw_builder that inserts instructions
    * at the end of \p shader. The optional \p dispatch_width
    * gives the execution width to be used instead of the
    * shader original dispatch_width.
    */
   brw_builder(brw_shader *shader,
               unsigned dispatch_width = 0) :
      shader(shader), block(NULL), cursor(NULL),
      _dispatch_width(dispatch_width ? dispatch_width : shader->dispatch_width),
      _group(0),
      force_writemask_all(false),
      annotation()
   {
      if (shader->cfg && shader->cfg->num_blocks > 0) {
         block = shader->cfg->last_block();
         cursor = &block->instructions.tail_sentinel;
      } else {
         cursor = (brw_exec_node *)&shader->instructions.tail_sentinel;
      }
   }

   /**
    * Construct an brw_builder that inserts instructions before
    * instruction \p inst in the same basic block.  The default
    * execution controls and debug annotation are initialized from the
    * instruction passed as argument.
    */
   explicit brw_builder(brw_inst *inst) :
      shader(inst->block->cfg->s), block(inst->block), cursor(inst),
      _dispatch_width(inst->exec_size),
      _group(inst->group),
      force_writemask_all(inst->force_writemask_all)
   {
#ifndef NDEBUG
      annotation.str = inst->annotation;
#else
      annotation.str = NULL;
#endif
   }

   brw_builder
   at_start(bblock_t *block) const
   {
      brw_builder bld = *this;
      bld.block = block;
      bld.cursor = block->instructions.head_sentinel.next;
      return bld;
   }

   brw_builder
   at_end(bblock_t *block) const
   {
      brw_builder bld = *this;
      bld.block = block;
      bld.cursor = &block->instructions.tail_sentinel;
      return bld;
   }

   brw_builder
   before(brw_inst *ref) const
   {
      brw_builder bld = *this;
      bld.block = ref->block;
      bld.cursor = ref;
      return bld;
   }

   brw_builder
   after(brw_inst *ref) const
   {
      brw_builder bld = *this;
      bld.block = ref->block;
      bld.cursor = ref->next;
      return bld;
   }

   brw_builder
   after_block_before_control_flow(bblock_t *block) const
   {
      brw_builder bld = *this;
      bld.block = block;
      bld.cursor = block->last_non_control_flow_inst()->next;
      return bld;
   }

   /**
    * Construct a builder specifying the default SIMD width and group of
    * channel enable signals, inheriting other code generation parameters
    * from this.
    *
    * \p n gives the default SIMD width, \p i gives the slot group used for
    * predication and control flow masking in multiples of \p n channels.
    */
   brw_builder
   group(unsigned n, unsigned i) const
   {
      brw_builder bld = *this;

      if (n <= dispatch_width() && i < dispatch_width() / n) {
         bld._group += i * n;
      } else {
         /* The requested channel group isn't a subset of the channel group
          * of this builder, which means that the resulting instructions
          * would use (potentially undefined) channel enable signals not
          * specified by the parent builder.  That's only valid if the
          * instruction doesn't have per-channel semantics, in which case
          * we should clear off the default group index in order to prevent
          * emitting instructions with channel group not aligned to their
          * own execution size.
          */
         assert(force_writemask_all);
         bld._group = 0;
      }

      bld._dispatch_width = n;
      return bld;
   }

   /**
    * Alias for group() with width equal to eight.
    */
   brw_builder
   quarter(unsigned i) const
   {
      return group(8, i);
   }

   /**
    * Construct a builder with per-channel control flow execution masking
    * disabled if \p b is true.  If control flow execution masking is
    * already disabled this has no effect.
    */
   brw_builder
   exec_all(bool b = true) const
   {
      brw_builder bld = *this;
      if (b)
         bld.force_writemask_all = true;
      return bld;
   }

   /**
    * Construct a builder for SIMD1 operations.
    */
   brw_builder
   uniform() const
   {
      return exec_all().group(1, 0);
   }

   /**
    * Construct a builder for SIMD8-as-scalar
    */
   brw_builder
   scalar_group() const
   {
      return exec_all().group(8 * reg_unit(shader->devinfo), 0);
   }

   /**
    * Construct a builder with the given debug annotation info.
    */
   brw_builder
   annotate(const char *str) const
   {
      brw_builder bld = *this;
      bld.annotation.str = str;
      return bld;
   }

   /**
    * Get the SIMD width in use.
    */
   unsigned
   dispatch_width() const
   {
      return _dispatch_width;
   }

   /**
    * Get the channel group in use.
    */
   unsigned
   group() const
   {
      return _group;
   }

   /**
    * Allocate a virtual register of natural vector size (one for this IR)
    * and SIMD width.  \p n gives the amount of space to allocate in
    * dispatch_width units (which is just enough space for one logical
    * component in this IR).
    */
   brw_reg
   vgrf(enum brw_reg_type type, unsigned n = 1) const
   {
      assert(dispatch_width() <= 32);

      if (n > 0)
         return brw_allocate_vgrf(*shader, type, n * dispatch_width());
      else
         return retype(null_reg_ud(), type);
   }

   brw_reg
   vaddr(enum brw_reg_type type, unsigned subnr) const
   {
      brw_reg addr = brw_address_reg(subnr);
      addr.nr = shader->next_address_register_nr++;
      return retype(addr, type);
   }

   /**
    * Create a null register of floating type.
    */
   brw_reg
   null_reg_f() const
   {
      return brw_reg(retype(brw_null_reg(), BRW_TYPE_F));
   }

   brw_reg
   null_reg_df() const
   {
      return brw_reg(retype(brw_null_reg(), BRW_TYPE_DF));
   }

   /**
    * Create a null register of signed integer type.
    */
   brw_reg
   null_reg_d() const
   {
      return brw_reg(retype(brw_null_reg(), BRW_TYPE_D));
   }

   /**
    * Create a null register of unsigned integer type.
    */
   brw_reg
   null_reg_ud() const
   {
      return brw_reg(retype(brw_null_reg(), BRW_TYPE_UD));
   }

   /**
    * Create and insert a nullary control instruction into the program.
    */
   brw_inst *
   emit(enum opcode opcode) const
   {
      return emit(opcode, brw_reg(), NULL, 0);
   }

   brw_inst *
   emit(enum opcode opcode, unsigned num_srcs) const
   {
      return emit(brw_new_inst(*shader, opcode, dispatch_width(), brw_reg(), num_srcs));
   }

   /**
    * Create and insert a nullary instruction into the program.
    */
   brw_inst *
   emit(enum opcode opcode, const brw_reg &dst) const
   {
      return emit(opcode, dst, NULL, 0);
   }

   /**
    * Create and insert a unary instruction into the program.
    */
   brw_inst *
   emit(enum opcode opcode, const brw_reg &dst, const brw_reg &src0) const
   {
      return emit(opcode, dst, &src0, 1);
   }

   /**
    * Create and insert a binary instruction into the program.
    */
   brw_inst *
   emit(enum opcode opcode, const brw_reg &dst, const brw_reg &src0,
        const brw_reg &src1) const
   {
      const brw_reg srcs[] = { src0, src1 };
      return emit(opcode, dst, srcs, 2);
   }

   /**
    * Create and insert a ternary instruction into the program.
    */
   brw_inst *
   emit(enum opcode opcode, const brw_reg &dst, const brw_reg &src0,
        const brw_reg &src1, const brw_reg &src2) const
   {
      brw_inst *inst = brw_new_inst(*shader, opcode, dispatch_width(), dst, 3);
      inst->src[0] = src0;
      inst->src[1] = src1;
      inst->src[2] = src2;

      switch (opcode) {
      case BRW_OPCODE_BFE:
      case BRW_OPCODE_BFI2:
      case BRW_OPCODE_MAD:
      case BRW_OPCODE_LRP:
         for (unsigned i = 0; i < 3; i++)
            inst->src[i] = fix_3src_operand(inst->src[i]);
         break;

      default:
         /* Nothing to do. */
         break;
      }

      return emit(inst);
   }

   /**
    * Create and insert an instruction with a variable number of sources
    * into the program.
    */
   brw_inst *
   emit(enum opcode opcode, const brw_reg &dst, const brw_reg srcs[],
        unsigned num_srcs) const
   {
      /* Use the emit() methods for specific operand counts to ensure that
       * opcode-specific operand fixups occur.
       */
      if (num_srcs == 3) {
         return emit(opcode, dst, srcs[0], srcs[1], srcs[2]);
      } else {
         brw_inst *inst = brw_new_inst(*shader, opcode, dispatch_width(), dst, num_srcs);
         for (unsigned i = 0; i < num_srcs; i++)
            inst->src[i] = srcs[i];
         return emit(inst);
      }
   }

   /**
    * Insert a preallocated instruction into the program.
    */
   brw_inst *
   emit(brw_inst *inst) const
   {
      assert(inst->exec_size <= 32);
      assert(inst->exec_size == dispatch_width() ||
             force_writemask_all);

      inst->group = _group;
      inst->force_writemask_all = force_writemask_all;
#ifndef NDEBUG
      inst->annotation = annotation.str;
#endif

      if (block)
         block->insert_before(inst, cursor);
      else
         cursor->insert_before(inst);

      return inst;
   }

   /**
    * Select \p src0 if the comparison of both sources with the given
    * conditional mod evaluates to true, otherwise select \p src1.
    *
    * Generally useful to get the minimum or maximum of two values.
    */
   brw_inst *
   emit_minmax(const brw_reg &dst, const brw_reg &src0,
               const brw_reg &src1, brw_conditional_mod mod) const
   {
      assert(mod == BRW_CONDITIONAL_GE || mod == BRW_CONDITIONAL_L);

      /* In some cases we can't have bytes as operand for src1, so use the
       * same type for both operand.
       */
      return set_condmod(mod, SEL(dst, fix_unsigned_negate(src0),
                                  fix_unsigned_negate(src1)));
   }

   /**
    * Copy any live channel from \p src to the first channel of the result.
    */
   brw_reg
   emit_uniformize(const brw_reg &src) const
   {
      /* Trivial: skip unnecessary work and retain IMM */
      if (src.file == IMM)
         return src;

      /* FIXME: We use a vector chan_index and dst to allow constant and
       * copy propagration to move result all the way into the consuming
       * instruction (typically a surface index or sampler index for a
       * send). Once we teach const/copy propagation about scalars we
       * should go back to scalar destinations here.
       */
      const brw_builder xbld = scalar_group();
      const brw_reg chan_index = xbld.vgrf(BRW_TYPE_UD);

      /* FIND_LIVE_CHANNEL will only write a single component after
       * lowering. Munge size_written here to match the allocated size of
       * chan_index.
       */
      exec_all().emit(SHADER_OPCODE_FIND_LIVE_CHANNEL, chan_index)
         ->size_written = chan_index.component_size(xbld.dispatch_width());

      return BROADCAST(src, component(chan_index, 0));
   }

   brw_reg
   move_to_vgrf(const brw_reg &src, unsigned num_components) const
   {
      brw_reg *const src_comps = new brw_reg[num_components];

      for (unsigned i = 0; i < num_components; i++)
         src_comps[i] = offset(src, *this, i);

      const brw_reg dst = vgrf(src.type, num_components);
      LOAD_PAYLOAD(dst, src_comps, num_components, 0);

      delete[] src_comps;

      return brw_reg(dst);
   }

   brw_inst *
   emit_undef_for_dst(const brw_inst *old_inst) const
   {
      assert(old_inst->dst.file == VGRF);
      brw_inst *inst = emit(SHADER_OPCODE_UNDEF,
                               retype(old_inst->dst, BRW_TYPE_UD));
      inst->size_written = old_inst->size_written;

      return inst;
   }

   /**
    * Emit UNDEF for the given register if its data doesn't fully occupy
    * the space we allocated.
    */
   void
   emit_undef_for_partial_reg(const brw_reg &reg) const
   {
      if (brw_type_size_bytes(reg.type) * dispatch_width() < REG_SIZE)
         UNDEF(reg);
   }

   /**
    * Assorted arithmetic ops.
    * @{
    */
#define _ALU1(prefix, op)                                \
   brw_inst *                                          \
   op(const brw_reg &dst, const brw_reg &src0) const    \
   {                                                  \
      assert(_dispatch_width == 1 ||                  \
             (dst.file >= VGRF && dst.stride != 0) || \
             (dst.file < VGRF && dst.hstride != 0));  \
      return emit(prefix##op, dst, src0);             \
   }                                                  \
   brw_reg                                             \
   op(const brw_reg &src0, brw_inst **out = NULL) const \
   {                                                  \
      brw_reg dst = vgrf(src0.type);                  \
      emit_undef_for_partial_reg(dst);                \
      brw_inst *inst = op(dst, src0);                 \
      if (out) *out = inst;                           \
      return inst->dst;                               \
   }
#define ALU1(op) _ALU1(BRW_OPCODE_, op)
#define VIRT1(op) _ALU1(SHADER_OPCODE_, op)

   brw_inst *
   alu2(opcode op, const brw_reg &dst, const brw_reg &src0, const brw_reg &src1) const
   {
      return emit(op, dst, src0, src1);
   }
   brw_reg
   alu2(opcode op, const brw_reg &src0, const brw_reg &src1, brw_inst **out = NULL) const
   {
      enum brw_reg_type inferred_dst_type =
         brw_type_larger_of(src0.type, src1.type);
      brw_reg dst = vgrf(inferred_dst_type);
      emit_undef_for_partial_reg(dst);
      brw_inst *inst = alu2(op, dst, src0, src1);
      if (out) *out = inst;
      return inst->dst;
   }

#define _ALU2(prefix, op)                                                    \
   brw_inst *                                                              \
   op(const brw_reg &dst, const brw_reg &src0, const brw_reg &src1) const    \
   {                                                                      \
      return alu2(prefix##op, dst, src0, src1);                           \
   }                                                                      \
   brw_reg                                                                 \
   op(const brw_reg &src0, const brw_reg &src1, brw_inst **out = NULL) const \
   {                                                                      \
      return alu2(prefix##op, src0, src1, out);                           \
   }
#define ALU2(op) _ALU2(BRW_OPCODE_, op)
#define VIRT2(op) _ALU2(SHADER_OPCODE_, op)

#define ALU2_ACC(op)                                                    \
   brw_inst *                                                     \
   op(const brw_reg &dst, const brw_reg &src0, const brw_reg &src1) const \
   {                                                                 \
      brw_inst *inst = emit(BRW_OPCODE_##op, dst, src0, src1);    \
      inst->writes_accumulator = true;                               \
      return inst;                                                   \
   }

#define ALU3(op)                                                        \
   brw_inst *                                                     \
   op(const brw_reg &dst, const brw_reg &src0, const brw_reg &src1,  \
      const brw_reg &src2) const                                     \
   {                                                                 \
      return emit(BRW_OPCODE_##op, dst, src0, src1, src2);           \
   }                                                                 \
   brw_reg                                                           \
   op(const brw_reg &src0, const brw_reg &src1, const brw_reg &src2, \
      brw_inst **out = NULL) const                                    \
   {                                                                 \
      enum brw_reg_type inferred_dst_type =                          \
         brw_type_larger_of(brw_type_larger_of(src0.type, src1.type),\
                            src2.type);                              \
      brw_inst *inst = op(vgrf(inferred_dst_type), src0, src1, src2); \
      if (out) *out = inst;                                          \
      return inst->dst;                                              \
   }

   ALU3(ADD3)
   ALU2_ACC(ADDC)
   ALU2(AND)
   ALU2(ASR)
   ALU2(AVG)
   ALU3(BFE)
   ALU2(BFI1)
   ALU3(BFI2)
   ALU1(BFREV)
   ALU1(CBIT)
   ALU2(DP2)
   ALU2(DP3)
   ALU2(DP4)
   ALU2(DPH)
   ALU1(FBH)
   ALU1(FBL)
   ALU1(FRC)
   ALU3(DP4A)
   ALU2(LINE)
   ALU1(LZD)
   ALU2(MAC)
   ALU2_ACC(MACH)
   ALU3(MAD)
   ALU1(MOV)
   ALU2(MUL)
   ALU1(NOT)
   ALU2(OR)
   ALU2(PLN)
   ALU1(RNDD)
   ALU1(RNDE)
   ALU1(RNDU)
   ALU1(RNDZ)
   ALU2(ROL)
   ALU2(ROR)
   ALU2(SEL)
   ALU2(SHL)
   ALU2(SHR)
   ALU2_ACC(SUBB)
   ALU2(XOR)

   VIRT1(RCP)
   VIRT1(RSQ)
   VIRT1(SQRT)
   VIRT1(EXP2)
   VIRT1(LOG2)
   VIRT2(POW)
   VIRT2(INT_QUOTIENT)
   VIRT2(INT_REMAINDER)
   VIRT1(SIN)
   VIRT1(COS)

#undef ALU3
#undef ALU2_ACC
#undef ALU2
#undef VIRT2
#undef _ALU2
#undef ALU1
#undef VIRT1
#undef _ALU1
   /** @} */

   brw_send_inst *
   SEND() const
   {
      return emit(SHADER_OPCODE_SEND, SEND_NUM_SRCS)->as_send();
   }

   brw_scratch_inst *
   FILL() const
   {
      return emit(SHADER_OPCODE_LSC_FILL, FILL_NUM_SRCS)->as_scratch();
   }

   brw_scratch_inst *
   SPILL() const
   {
      return emit(SHADER_OPCODE_LSC_SPILL, SPILL_NUM_SRCS)->as_scratch();
   }

   brw_urb_inst *
   URB_WRITE(const brw_reg srcs[], unsigned num_srcs) const
   {
      assert(num_srcs == URB_LOGICAL_NUM_SRCS);
      return emit(SHADER_OPCODE_URB_WRITE_LOGICAL, reg_undef, srcs, num_srcs)->as_urb();
   }

   brw_urb_inst *
   URB_READ(const brw_reg &dst, const brw_reg srcs[], unsigned num_srcs) const
   {
      assert(num_srcs == URB_LOGICAL_NUM_SRCS);
      return emit(SHADER_OPCODE_URB_READ_LOGICAL, dst, srcs, num_srcs)->as_urb();
   }

   brw_inst *
   ADD(const brw_reg &dst, const brw_reg &src0, const brw_reg &src1) const
   {
      return alu2(BRW_OPCODE_ADD, dst, src0, src1);
   }

   brw_reg
   ADD(const brw_reg &src0, const brw_reg &src1, brw_inst **out = NULL) const
   {
      if (src1.file == IMM && src1.ud == 0 && !out)
         return src0;

      return alu2(BRW_OPCODE_ADD, src0, src1, out);
   }

   brw_inst *
   BFN(const brw_reg &dst, const brw_reg &src0, const brw_reg &src1,
       const brw_reg &src2, unsigned table_byte) const
   {
      brw_inst *inst = brw_new_inst(*shader, BRW_OPCODE_BFN, dispatch_width(),
                                    dst, 4);

      inst->src[0] = fix_3src_operand(src0);
      inst->src[1] = fix_3src_operand(src1);
      inst->src[2] = fix_3src_operand(src2);
      inst->src[3] = brw_imm_ud(table_byte);

      return emit(inst);
   }

   brw_reg
   BFN(const brw_reg &src0, const brw_reg &src1, const brw_reg &src2,
       unsigned table_byte, brw_inst **out = NULL) const
   {
      brw_reg dst = vgrf(src0.type);
      emit_undef_for_partial_reg(dst);
      brw_inst *inst = BFN(dst, src0, src1, src2, table_byte);
      if (out) *out = inst;
      return inst->dst;
   }

   /**
    * CMP: Sets the low bit of the destination channels with the result
    * of the comparison, while the upper bits are undefined, and updates
    * the flag register with the packed 16 bits of the result.
    */
   brw_inst *
   CMP(const brw_reg &dst, const brw_reg &src0, const brw_reg &src1,
       brw_conditional_mod condition) const
   {
      /* Take the instruction:
       *
       * CMP null<d> src0<f> src1<f>
       *
       * Original gfx4 does type conversion to the destination type
       * before comparison, producing garbage results for floating
       * point comparisons.
       */
      const enum brw_reg_type type =
         dst.is_null() ?
         src0.type :
         brw_type_with_size(src0.type, brw_type_size_bits(dst.type));

      return set_condmod(condition,
                         emit(BRW_OPCODE_CMP, retype(dst, type),
                              fix_unsigned_negate(src0),
                              fix_unsigned_negate(src1)));
   }

   /**
    * CMPN: Behaves like CMP, but produces true if src1 is NaN.
    */
   brw_inst *
   CMPN(const brw_reg &dst, const brw_reg &src0, const brw_reg &src1,
        brw_conditional_mod condition) const
   {
      /* Take the instruction:
       *
       * CMP null<d> src0<f> src1<f>
       *
       * Original gfx4 does type conversion to the destination type
       * before comparison, producing garbage results for floating
       * point comparisons.
       */
      const enum brw_reg_type type =
         dst.is_null() ?
         src0.type :
         brw_type_with_size(src0.type, brw_type_size_bits(dst.type));

      return set_condmod(condition,
                         emit(BRW_OPCODE_CMPN, retype(dst, type),
                              fix_unsigned_negate(src0),
                              fix_unsigned_negate(src1)));
   }

   /**
    * CSEL: dst = src2 <op> 0.0f ? src0 : src1
    */
   brw_inst *
   CSEL(const brw_reg &dst, const brw_reg &src0, const brw_reg &src1,
        const brw_reg &src2, brw_conditional_mod condition) const
   {
      return set_condmod(condition,
                         emit(BRW_OPCODE_CSEL,
                              retype(dst, src2.type),
                              retype(src0, src2.type),
                              retype(src1, src2.type),
                              src2));
   }

   /**
    * Emit a linear interpolation instruction.
    */
   brw_inst *
   LRP(const brw_reg &dst, const brw_reg &x, const brw_reg &y,
       const brw_reg &a) const
   {
      if (shader->devinfo->ver <= 10) {
         /* The LRP instruction actually does op1 * op0 + op2 * (1 - op0), so
          * we need to reorder the operands.
          */
         return emit(BRW_OPCODE_LRP, dst, a, y, x);

      } else {
         /* We can't use the LRP instruction.  Emit x*(1-a) + y*a. */
         const brw_reg y_times_a = vgrf(dst.type);
         const brw_reg one_minus_a = vgrf(dst.type);
         const brw_reg x_times_one_minus_a = vgrf(dst.type);

         MUL(y_times_a, y, a);
         ADD(one_minus_a, negate(a), brw_imm_f(1.0f));
         MUL(x_times_one_minus_a, x, brw_reg(one_minus_a));
         return ADD(dst, brw_reg(x_times_one_minus_a), brw_reg(y_times_a));
      }
   }

   /**
    * Collect a number of registers in a contiguous range of registers.
    */
   brw_load_payload_inst *
   LOAD_PAYLOAD(const brw_reg &dst, const brw_reg *src,
                unsigned sources, unsigned header_size) const
   {
      brw_load_payload_inst *lp =
         emit(SHADER_OPCODE_LOAD_PAYLOAD, dst, src, sources)->as_load_payload();
      lp->header_size = header_size;
      lp->size_written = header_size * REG_SIZE;
      for (unsigned i = header_size; i < sources; i++) {
         lp->size_written += dispatch_width() * brw_type_size_bytes(src[i].type) *
                             dst.stride;
      }

      return lp;
   }

   brw_inst *
   VEC(const brw_reg &dst, const brw_reg *src, unsigned sources) const
   {
      return sources == 1 ? MOV(dst, src[0])
                          : LOAD_PAYLOAD(dst, src, sources, 0);
   }

   brw_inst *
   SYNC(enum tgl_sync_function sync) const
   {
      return emit(BRW_OPCODE_SYNC, null_reg_ud(), brw_imm_ud(sync));
   }

   brw_inst *
   UNDEF(const brw_reg &dst) const
   {
      assert(dst.file == VGRF);
      assert(dst.offset % REG_SIZE == 0);
      brw_inst *inst = emit(SHADER_OPCODE_UNDEF,
                               retype(dst, BRW_TYPE_UD));
      inst->size_written = shader->alloc.sizes[dst.nr] * REG_SIZE - dst.offset;

      return inst;
   }

   brw_dpas_inst *
   DPAS(const brw_reg &dst, const brw_reg &src0, const brw_reg &src1, const brw_reg &src2,
        unsigned sdepth, unsigned rcount) const
   {
      assert(_dispatch_width == 8 * reg_unit(shader->devinfo));
      assert(sdepth == 8);
      assert(rcount == 1 || rcount == 2 || rcount == 4 || rcount == 8);

      brw_dpas_inst *dpas = emit(BRW_OPCODE_DPAS, dst, src0, src1, src2)->as_dpas();
      dpas->sdepth = sdepth;
      dpas->rcount = rcount;

      unsigned type_size = brw_type_size_bytes(dst.type);
      assert(type_size == 4 || type_size == 2);
      dpas->size_written = rcount * reg_unit(shader->devinfo) * 8 * type_size;

      return dpas;
   }

   void
   VARYING_PULL_CONSTANT_LOAD(const brw_reg &dst,
                              const brw_reg &surface,
                              const brw_reg &surface_handle,
                              const brw_reg &varying_offset,
                              uint32_t const_offset,
                              uint8_t alignment,
                              unsigned components) const
   {
      assert(components <= 4);

      /* We have our constant surface use a pitch of 4 bytes, so our index can
       * be any component of a vector, and then we load 4 contiguous
       * components starting from that.  TODO: Support loading fewer than 4.
       */
      brw_reg total_offset = ADD(varying_offset, brw_imm_ud(const_offset));

      /* The pull load message will load a vec4 (16 bytes). If we are loading
       * a double this means we are only loading 2 elements worth of data.
       * We also want to use a 32-bit data type for the dst of the load operation
       * so other parts of the driver don't get confused about the size of the
       * result.
       */
      brw_reg vec4_result = vgrf(BRW_TYPE_F, 4);

      brw_reg srcs[PULL_VARYING_CONSTANT_SRCS];
      srcs[PULL_VARYING_CONSTANT_SRC_SURFACE]        = surface;
      srcs[PULL_VARYING_CONSTANT_SRC_SURFACE_HANDLE] = surface_handle;
      srcs[PULL_VARYING_CONSTANT_SRC_OFFSET]         = total_offset;
      srcs[PULL_VARYING_CONSTANT_SRC_ALIGNMENT]      = brw_imm_ud(alignment);

      brw_inst *inst = emit(FS_OPCODE_VARYING_PULL_CONSTANT_LOAD_LOGICAL,
                           vec4_result, srcs, PULL_VARYING_CONSTANT_SRCS);
      inst->size_written = 4 * vec4_result.component_size(inst->exec_size);

      shuffle_from_32bit_read(dst, vec4_result, 0, components);
   }

   brw_reg
   LOAD_SUBGROUP_INVOCATION() const
   {
      brw_reg reg = vgrf(shader->dispatch_width < 16 ? BRW_TYPE_UD : BRW_TYPE_UW);
      exec_all().emit(SHADER_OPCODE_LOAD_SUBGROUP_INVOCATION, reg);
      return reg;
   }

   brw_reg
   BROADCAST(brw_reg value, brw_reg index) const
   {
      const brw_builder xbld = scalar_group();
      const brw_reg dst = xbld.vgrf(value.type);

      assert(is_uniform(index));

      /* A broadcast will always be at the full dispatch width even if the
       * use of the broadcast result is smaller. If the source is_scalar,
       * it may be allocated at less than the full dispatch width (e.g.,
       * allocated at SIMD8 with SIMD32 dispatch). The input may or may
       * not be stride=0. If it is not, the generated broadcast
       *
       *    broadcast(32) dst, value<1>, index<0>
       *
       * is invalid because it may read out of bounds from value.
       *
       * To account for this, modify the stride of an is_scalar input to be
       * zero.
       */
      if (value.is_scalar)
         value = component(value, 0);

      /* Ensure that the source of a broadcast is always register aligned.
       * See brw_broadcast() non-scalar case for more details.
       */
      if (reg_offset(value) % (REG_SIZE * reg_unit(shader->devinfo)) != 0)
         value = MOV(value);

      /* BROADCAST will only write a single component after lowering. Munge
       * size_written here to match the allocated size of dst.
       */
      xbld.emit(SHADER_OPCODE_BROADCAST, dst, value, index,
                brw_imm_ud(value.component_size(_dispatch_width)));

      return component(dst, 0);
   }

   brw_reg
   LOAD_REG(const brw_reg &src0, brw_inst **out = NULL) const
   {
      /* LOAD_REG is a raw, bulk copy of one VGRF to another. The type is
       * irrelevant. The pass that inserts LOAD_REG to encourage results to be
       * defs will force all types to be integer types.  Forcing the type to
       * always be integer here helps with uniformity, and it will also help
       * implement unit tests that want to compare two shaders for equality.
       */
      brw_reg_type t = brw_type_with_size(BRW_TYPE_UD,
                                          brw_type_size_bits(src0.type));
      brw_reg dst = retype(brw_allocate_vgrf_units(*shader,
                                                   shader->alloc.sizes[src0.nr]),
                           t);

      assert(src0.file == VGRF);
      assert(shader->alloc.sizes[dst.nr] == shader->alloc.sizes[src0.nr]);

      brw_inst *inst = emit(SHADER_OPCODE_LOAD_REG, dst, retype(src0, t));

      inst->size_written = REG_SIZE * shader->alloc.sizes[src0.nr];

      assert(shader->alloc.sizes[inst->dst.nr] * REG_SIZE == inst->size_written);
      assert(!inst->is_partial_write());

      if (out) *out = inst;
      return retype(inst->dst, src0.type);
   }

   brw_shader *shader;

   brw_inst *BREAK()    const { return emit(BRW_OPCODE_BREAK); }
   brw_inst *ELSE()     const { return emit(BRW_OPCODE_ELSE); }
   brw_inst *ENDIF()    const { return emit(BRW_OPCODE_ENDIF); }
   brw_inst *NOP()      const { return emit(BRW_OPCODE_NOP); }
   brw_inst *CONTINUE() const { return emit(BRW_OPCODE_CONTINUE); }

   brw_inst *
   IF(brw_predicate predicate = BRW_PREDICATE_NORMAL) const
   {
      return set_predicate(predicate, emit(BRW_OPCODE_IF));
   }

   brw_inst *
   WHILE(brw_predicate predicate = BRW_PREDICATE_NONE) const
   {
      return set_predicate(predicate, emit(BRW_OPCODE_WHILE));
   }

   void
   DO() const
   {
      emit(BRW_OPCODE_DO);
      /* Ensure that there'll always be a block after DO to add
       * instructions and serve as sucessor for predicated WHILE
       * and CONTINUE.
       *
       * See more details in brw_cfg::validate().
       */
      emit(SHADER_OPCODE_FLOW);
   }

   bool has_writemask_all() const {
      return force_writemask_all;
   }

private:
   /**
    * Workaround for negation of UD registers.  See comment in
    * brw_generator::generate_code() for more details.
    */
   brw_reg
   fix_unsigned_negate(const brw_reg &src) const
   {
      if (src.type == BRW_TYPE_UD &&
          src.negate) {
         brw_reg temp = vgrf(BRW_TYPE_UD);
         MOV(temp, src);
         return brw_reg(temp);
      } else {
         return src;
      }
   }

   /**
    * Workaround for source register modes not supported by the ternary
    * instruction encoding.
    */
   brw_reg
   fix_3src_operand(const brw_reg &src) const
   {
      switch (src.file) {
      case FIXED_GRF:
         /* FINISHME: Could handle scalar region, other stride=1 regions */
         if (src.vstride != BRW_VERTICAL_STRIDE_8 ||
             src.width != BRW_WIDTH_8 ||
             src.hstride != BRW_HORIZONTAL_STRIDE_1)
            break;
         FALLTHROUGH;
      case ATTR:
      case VGRF:
      case UNIFORM:
      case IMM:
         return src;
      default:
         break;
      }

      brw_reg expanded = vgrf(src.type);
      MOV(expanded, src);
      return expanded;
   }

   void shuffle_from_32bit_read(const brw_reg &dst,
                                const brw_reg &src,
                                uint32_t first_component,
                                uint32_t components) const;

   bblock_t *block;
   brw_exec_node *cursor;

   unsigned _dispatch_width;
   unsigned _group;
   bool force_writemask_all;

   /** Debug annotation info. */
   struct {
      const char *str;
   } annotation;
};

/**
 * Offset by a number of components into a VGRF
 *
 * It is assumed that the VGRF represents a vector (e.g., returned by
 * load_uniform or a texture operation). Convergent and divergent values are
 * stored differently, so care must be taken to offset properly.
 */
static inline brw_reg
offset(const brw_reg &reg, const brw_builder &bld, unsigned delta)
{
   /* If the value is convergent (stored as one or more SIMD8), offset using
    * SIMD8 and select component 0.
    */
   if (reg.is_scalar) {
      const unsigned allocation_width = 8 * reg_unit(bld.shader->devinfo);

      brw_reg offset_reg = offset(reg, allocation_width, delta);

      /* If the dispatch width is larger than the allocation width, that
       * implies that the register can only be used as a source. Otherwise the
       * instruction would write past the allocation size of the register.
       */
      if (bld.dispatch_width() > allocation_width)
         return component(offset_reg, 0);
      else
         return offset_reg;
   }

   /* Offset to the component assuming the value was allocated in
    * dispatch_width units.
    */
   return offset(reg, bld.dispatch_width(), delta);
}

brw_reg brw_sample_mask_reg(const brw_builder &bld);
void brw_emit_predicate_on_sample_mask(const brw_builder &bld, brw_inst *inst);

brw_reg
brw_fetch_payload_reg(const brw_builder &bld, uint8_t regs[2],
                      brw_reg_type type = BRW_TYPE_F,
                      unsigned n = 1);

brw_reg
brw_fetch_barycentric_reg(const brw_builder &bld, uint8_t regs[2]);

void
brw_check_dynamic_msaa_flag(const brw_builder &bld,
                            const struct brw_wm_prog_data *wm_prog_data,
                            enum intel_msaa_flags flag);

inline brw_inst *
brw_transform_inst(const brw_builder &bld, brw_inst *inst,
                   enum opcode new_opcode,
                   unsigned new_num_srcs = UINT_MAX)
{
   return brw_transform_inst(*bld.shader, inst, new_opcode, new_num_srcs);
}
