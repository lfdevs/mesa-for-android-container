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

#ifndef BRW_FS_BUILDER_H
#define BRW_FS_BUILDER_H

#include "brw_ir_fs.h"
#include "brw_eu.h"
#include "brw_fs.h"

namespace brw {
   /**
    * Toolbox to assemble an FS IR program out of individual instructions.
    */
   class fs_builder {
   public:
      /**
       * Construct an fs_builder that inserts instructions into \p shader.
       * \p dispatch_width gives the native execution width of the program.
       */
      fs_builder(fs_visitor *shader,
                 unsigned dispatch_width) :
         shader(shader), block(NULL), cursor(NULL),
         _dispatch_width(dispatch_width),
         _group(0),
         force_writemask_all(false),
         annotation()
      {
      }

      explicit fs_builder(fs_visitor *s) : fs_builder(s, s->dispatch_width) {}

      /**
       * Construct an fs_builder that inserts instructions into \p shader
       * before instruction \p inst in basic block \p block.  The default
       * execution controls and debug annotation are initialized from the
       * instruction passed as argument.
       */
      fs_builder(fs_visitor *shader, bblock_t *block, fs_inst *inst) :
         shader(shader), block(block), cursor(inst),
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

      /**
       * Construct an fs_builder that inserts instructions before \p cursor in
       * basic block \p block, inheriting other code generation parameters
       * from this.
       */
      fs_builder
      at(bblock_t *block, exec_node *cursor) const
      {
         fs_builder bld = *this;
         bld.block = block;
         bld.cursor = cursor;
         return bld;
      }

      /**
       * Construct an fs_builder appending instructions at the end of the
       * instruction list of the shader, inheriting other code generation
       * parameters from this.
       */
      fs_builder
      at_end() const
      {
         return at(NULL, (exec_node *)&shader->instructions.tail_sentinel);
      }

      /**
       * Construct a builder specifying the default SIMD width and group of
       * channel enable signals, inheriting other code generation parameters
       * from this.
       *
       * \p n gives the default SIMD width, \p i gives the slot group used for
       * predication and control flow masking in multiples of \p n channels.
       */
      fs_builder
      group(unsigned n, unsigned i) const
      {
         fs_builder bld = *this;

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
      fs_builder
      quarter(unsigned i) const
      {
         return group(8, i);
      }

      /**
       * Construct a builder with per-channel control flow execution masking
       * disabled if \p b is true.  If control flow execution masking is
       * already disabled this has no effect.
       */
      fs_builder
      exec_all(bool b = true) const
      {
         fs_builder bld = *this;
         if (b)
            bld.force_writemask_all = true;
         return bld;
      }

      /**
       * Construct a builder with the given debug annotation info.
       */
      fs_builder
      annotate(const char *str) const
      {
         fs_builder bld = *this;
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
         const unsigned unit = reg_unit(shader->devinfo);
         assert(dispatch_width() <= 32);

         if (n > 0)
            return brw_vgrf(shader->alloc.allocate(
                               DIV_ROUND_UP(n * brw_type_size_bytes(type) * dispatch_width(),
                                            unit * REG_SIZE) * unit),
                            type);
         else
            return retype(null_reg_ud(), type);
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
       * Insert an instruction into the program.
       */
      fs_inst *
      emit(const fs_inst &inst) const
      {
         return emit(new(shader->mem_ctx) fs_inst(inst));
      }

      /**
       * Create and insert a nullary control instruction into the program.
       */
      fs_inst *
      emit(enum opcode opcode) const
      {
         return emit(fs_inst(opcode, dispatch_width()));
      }

      /**
       * Create and insert a nullary instruction into the program.
       */
      fs_inst *
      emit(enum opcode opcode, const brw_reg &dst) const
      {
         return emit(fs_inst(opcode, dispatch_width(), dst));
      }

      /**
       * Create and insert a unary instruction into the program.
       */
      fs_inst *
      emit(enum opcode opcode, const brw_reg &dst, const brw_reg &src0) const
      {
         return emit(fs_inst(opcode, dispatch_width(), dst, src0));
      }

      /**
       * Create and insert a binary instruction into the program.
       */
      fs_inst *
      emit(enum opcode opcode, const brw_reg &dst, const brw_reg &src0,
           const brw_reg &src1) const
      {
         return emit(fs_inst(opcode, dispatch_width(), dst,
                                 src0, src1));
      }

      /**
       * Create and insert a ternary instruction into the program.
       */
      fs_inst *
      emit(enum opcode opcode, const brw_reg &dst, const brw_reg &src0,
           const brw_reg &src1, const brw_reg &src2) const
      {
         switch (opcode) {
         case BRW_OPCODE_BFE:
         case BRW_OPCODE_BFI2:
         case BRW_OPCODE_MAD:
         case BRW_OPCODE_LRP:
            return emit(fs_inst(opcode, dispatch_width(), dst,
                                    fix_3src_operand(src0),
                                    fix_3src_operand(src1),
                                    fix_3src_operand(src2)));

         default:
            return emit(fs_inst(opcode, dispatch_width(), dst,
                                    src0, src1, src2));
         }
      }

      /**
       * Create and insert an instruction with a variable number of sources
       * into the program.
       */
      fs_inst *
      emit(enum opcode opcode, const brw_reg &dst, const brw_reg srcs[],
           unsigned n) const
      {
         /* Use the emit() methods for specific operand counts to ensure that
          * opcode-specific operand fixups occur.
          */
         if (n == 3) {
            return emit(opcode, dst, srcs[0], srcs[1], srcs[2]);
         } else {
            return emit(fs_inst(opcode, dispatch_width(), dst, srcs, n));
         }
      }

      /**
       * Insert a preallocated instruction into the program.
       */
      fs_inst *
      emit(fs_inst *inst) const
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
            static_cast<fs_inst *>(cursor)->insert_before(block, inst);
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
      fs_inst *
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
         /* FIXME: We use a vector chan_index and dst to allow constant and
          * copy propagration to move result all the way into the consuming
          * instruction (typically a surface index or sampler index for a
          * send). This uses 1 or 3 extra hw registers in 16 or 32 wide
          * dispatch. Once we teach const/copy propagation about scalars we
          * should go back to scalar destinations here.
          */
         const brw_reg chan_index = vgrf(BRW_TYPE_UD);

         exec_all().emit(SHADER_OPCODE_FIND_LIVE_CHANNEL, chan_index);
         return BROADCAST(src, component(chan_index, 0));
      }

      brw_reg
      move_to_vgrf(const brw_reg &src, unsigned num_components) const
      {
         brw_reg *const src_comps = new brw_reg[num_components];
         for (unsigned i = 0; i < num_components; i++)
            src_comps[i] = offset(src, dispatch_width(), i);

         const brw_reg dst = vgrf(src.type, num_components);
         LOAD_PAYLOAD(dst, src_comps, num_components, 0);

         delete[] src_comps;

         return brw_reg(dst);
      }

      fs_inst *
      emit_undef_for_dst(const fs_inst *old_inst) const
      {
         assert(old_inst->dst.file == VGRF);
         fs_inst *inst = emit(SHADER_OPCODE_UNDEF,
                                  retype(old_inst->dst, BRW_TYPE_UD));
         inst->size_written = old_inst->size_written;

         return inst;
      }

      /**
       * Assorted arithmetic ops.
       * @{
       */
#define _ALU1(prefix, op)                                \
      fs_inst *                                          \
      op(const brw_reg &dst, const brw_reg &src0) const    \
      {                                                  \
         assert(_dispatch_width == 1 ||                  \
                (dst.file >= VGRF && dst.stride != 0) || \
                (dst.file < VGRF && dst.hstride != 0));  \
         return emit(prefix##op, dst, src0);             \
      }                                                  \
      brw_reg                                             \
      op(const brw_reg &src0, fs_inst **out = NULL) const \
      {                                                  \
         fs_inst *inst = op(vgrf(src0.type), src0);      \
         if (out) *out = inst;                           \
         return inst->dst;                               \
      }
#define ALU1(op) _ALU1(BRW_OPCODE_, op)
#define VIRT1(op) _ALU1(SHADER_OPCODE_, op)

      fs_inst *
      alu2(opcode op, const brw_reg &dst, const brw_reg &src0, const brw_reg &src1) const
      {
         return emit(op, dst, src0, src1);
      }
      brw_reg
      alu2(opcode op, const brw_reg &src0, const brw_reg &src1, fs_inst **out = NULL) const
      {
         enum brw_reg_type inferred_dst_type =
            brw_type_larger_of(src0.type, src1.type);
         fs_inst *inst = alu2(op, vgrf(inferred_dst_type), src0, src1);
         if (out) *out = inst;
         return inst->dst;
      }

#define _ALU2(prefix, op)                                                    \
      fs_inst *                                                              \
      op(const brw_reg &dst, const brw_reg &src0, const brw_reg &src1) const    \
      {                                                                      \
         return alu2(prefix##op, dst, src0, src1);                           \
      }                                                                      \
      brw_reg                                                                 \
      op(const brw_reg &src0, const brw_reg &src1, fs_inst **out = NULL) const \
      {                                                                      \
         return alu2(prefix##op, src0, src1, out);                           \
      }
#define ALU2(op) _ALU2(BRW_OPCODE_, op)
#define VIRT2(op) _ALU2(SHADER_OPCODE_, op)

#define ALU2_ACC(op)                                                    \
      fs_inst *                                                     \
      op(const brw_reg &dst, const brw_reg &src0, const brw_reg &src1) const \
      {                                                                 \
         fs_inst *inst = emit(BRW_OPCODE_##op, dst, src0, src1);    \
         inst->writes_accumulator = true;                               \
         return inst;                                                   \
      }

#define ALU3(op)                                                        \
      fs_inst *                                                     \
      op(const brw_reg &dst, const brw_reg &src0, const brw_reg &src1,  \
         const brw_reg &src2) const                                     \
      {                                                                 \
         return emit(BRW_OPCODE_##op, dst, src0, src1, src2);           \
      }                                                                 \
      brw_reg                                                           \
      op(const brw_reg &src0, const brw_reg &src1, const brw_reg &src2, \
         fs_inst **out = NULL) const                                    \
      {                                                                 \
         enum brw_reg_type inferred_dst_type =                          \
            brw_type_larger_of(brw_type_larger_of(src0.type, src1.type),\
                               src2.type);                              \
         fs_inst *inst = op(vgrf(inferred_dst_type), src0, src1, src2); \
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

      fs_inst *
      ADD(const brw_reg &dst, const brw_reg &src0, const brw_reg &src1) const
      {
         return alu2(BRW_OPCODE_ADD, dst, src0, src1);
      }

      brw_reg
      ADD(const brw_reg &src0, const brw_reg &src1, fs_inst **out = NULL) const
      {
         if (src1.file == IMM && src1.ud == 0 && !out)
            return src0;

         return alu2(BRW_OPCODE_ADD, src0, src1, out);
      }

      /**
       * CMP: Sets the low bit of the destination channels with the result
       * of the comparison, while the upper bits are undefined, and updates
       * the flag register with the packed 16 bits of the result.
       */
      fs_inst *
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
      fs_inst *
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
       * Gfx4 predicated IF.
       */
      fs_inst *
      IF(brw_predicate predicate) const
      {
         return set_predicate(predicate, emit(BRW_OPCODE_IF));
      }

      /**
       * CSEL: dst = src2 <op> 0.0f ? src0 : src1
       */
      fs_inst *
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
      fs_inst *
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
      fs_inst *
      LOAD_PAYLOAD(const brw_reg &dst, const brw_reg *src,
                   unsigned sources, unsigned header_size) const
      {
         fs_inst *inst = emit(SHADER_OPCODE_LOAD_PAYLOAD, dst, src, sources);
         inst->header_size = header_size;
         inst->size_written = header_size * REG_SIZE;
         for (unsigned i = header_size; i < sources; i++) {
            inst->size_written += dispatch_width() * brw_type_size_bytes(src[i].type) *
                                  dst.stride;
         }

         return inst;
      }

      fs_inst *
      VEC(const brw_reg &dst, const brw_reg *src, unsigned sources) const
      {
         return sources == 1 ? MOV(dst, src[0])
                             : LOAD_PAYLOAD(dst, src, sources, 0);
      }

      fs_inst *
      SYNC(enum tgl_sync_function sync) const
      {
         return emit(BRW_OPCODE_SYNC, null_reg_ud(), brw_imm_ud(sync));
      }

      fs_inst *
      UNDEF(const brw_reg &dst) const
      {
         assert(dst.file == VGRF);
         assert(dst.offset % REG_SIZE == 0);
         fs_inst *inst = emit(SHADER_OPCODE_UNDEF,
                                  retype(dst, BRW_TYPE_UD));
         inst->size_written = shader->alloc.sizes[dst.nr] * REG_SIZE - dst.offset;

         return inst;
      }

      fs_inst *
      DPAS(const brw_reg &dst, const brw_reg &src0, const brw_reg &src1, const brw_reg &src2,
           unsigned sdepth, unsigned rcount) const
      {
         assert(_dispatch_width == 8 * reg_unit(shader->devinfo));
         assert(sdepth == 8);
         assert(rcount == 1 || rcount == 2 || rcount == 4 || rcount == 8);

         fs_inst *inst = emit(BRW_OPCODE_DPAS, dst, src0, src1, src2);
         inst->sdepth = sdepth;
         inst->rcount = rcount;

         if (dst.type == BRW_TYPE_HF) {
            inst->size_written = reg_unit(shader->devinfo) * rcount * REG_SIZE / 2;
         } else {
            inst->size_written = reg_unit(shader->devinfo) * rcount * REG_SIZE;
         }

         return inst;
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

         fs_inst *inst = emit(FS_OPCODE_VARYING_PULL_CONSTANT_LOAD_LOGICAL,
                              vec4_result, srcs, PULL_VARYING_CONSTANT_SRCS);
         inst->size_written = 4 * vec4_result.component_size(inst->exec_size);

         shuffle_from_32bit_read(*this, dst, vec4_result, 0, components);
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
         const brw_reg dst = vgrf(value.type);

         /* Ensure that the source of a broadcast is always register aligned.
          * See brw_broadcast() non-scalar case for more details.
          */
         if (reg_offset(value) % (REG_SIZE * reg_unit(shader->devinfo)) != 0)
            value = MOV(value);

         exec_all().emit(SHADER_OPCODE_BROADCAST, dst, value, index);
         return component(dst, 0);
      }

      fs_visitor *shader;

      fs_inst *BREAK()    { return emit(BRW_OPCODE_BREAK); }
      fs_inst *DO()       { return emit(BRW_OPCODE_DO); }
      fs_inst *ENDIF()    { return emit(BRW_OPCODE_ENDIF); }
      fs_inst *NOP()      { return emit(BRW_OPCODE_NOP); }
      fs_inst *WHILE()    { return emit(BRW_OPCODE_WHILE); }
      fs_inst *CONTINUE() { return emit(BRW_OPCODE_CONTINUE); }

   private:
      /**
       * Workaround for negation of UD registers.  See comment in
       * fs_generator::generate_code() for more details.
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

      bblock_t *block;
      exec_node *cursor;

      unsigned _dispatch_width;
      unsigned _group;
      bool force_writemask_all;

      /** Debug annotation info. */
      struct {
         const char *str;
      } annotation;
   };
}

static inline brw_reg
offset(const brw_reg &reg, const brw::fs_builder &bld, unsigned delta)
{
   return offset(reg, bld.dispatch_width(), delta);
}

#endif
