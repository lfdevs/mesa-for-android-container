/*
 * Copyright © 2011 Intel Corporation
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

#include "brw_vec4.h"
#include "brw_cfg.h"
#include "glsl/ir_uniform.h"
#include "program/sampler.h"

namespace brw {

vec4_instruction::vec4_instruction(enum opcode opcode, const dst_reg &dst,
                                   const src_reg &src0, const src_reg &src1,
                                   const src_reg &src2)
{
   this->opcode = opcode;
   this->dst = dst;
   this->src[0] = src0;
   this->src[1] = src1;
   this->src[2] = src2;
   this->saturate = false;
   this->force_writemask_all = false;
   this->no_dd_clear = false;
   this->no_dd_check = false;
   this->writes_accumulator = false;
   this->conditional_mod = BRW_CONDITIONAL_NONE;
   this->predicate = BRW_PREDICATE_NONE;
   this->predicate_inverse = false;
   this->target = 0;
   this->regs_written = (dst.file == BAD_FILE ? 0 : 1);
   this->shadow_compare = false;
   this->ir = NULL;
   this->urb_write_flags = BRW_URB_WRITE_NO_FLAGS;
   this->header_size = 0;
   this->flag_subreg = 0;
   this->mlen = 0;
   this->base_mrf = 0;
   this->offset = 0;
   this->annotation = NULL;
}

vec4_instruction *
vec4_visitor::emit(vec4_instruction *inst)
{
   inst->ir = this->base_ir;
   inst->annotation = this->current_annotation;

   this->instructions.push_tail(inst);

   return inst;
}

vec4_instruction *
vec4_visitor::emit_before(bblock_t *block, vec4_instruction *inst,
                          vec4_instruction *new_inst)
{
   new_inst->ir = inst->ir;
   new_inst->annotation = inst->annotation;

   inst->insert_before(block, new_inst);

   return inst;
}

vec4_instruction *
vec4_visitor::emit(enum opcode opcode, const dst_reg &dst, const src_reg &src0,
                   const src_reg &src1, const src_reg &src2)
{
   return emit(new(mem_ctx) vec4_instruction(opcode, dst, src0, src1, src2));
}


vec4_instruction *
vec4_visitor::emit(enum opcode opcode, const dst_reg &dst, const src_reg &src0,
                   const src_reg &src1)
{
   return emit(new(mem_ctx) vec4_instruction(opcode, dst, src0, src1));
}

vec4_instruction *
vec4_visitor::emit(enum opcode opcode, const dst_reg &dst, const src_reg &src0)
{
   return emit(new(mem_ctx) vec4_instruction(opcode, dst, src0));
}

vec4_instruction *
vec4_visitor::emit(enum opcode opcode, const dst_reg &dst)
{
   return emit(new(mem_ctx) vec4_instruction(opcode, dst));
}

vec4_instruction *
vec4_visitor::emit(enum opcode opcode)
{
   return emit(new(mem_ctx) vec4_instruction(opcode, dst_reg()));
}

#define ALU1(op)							\
   vec4_instruction *							\
   vec4_visitor::op(const dst_reg &dst, const src_reg &src0)		\
   {									\
      return new(mem_ctx) vec4_instruction(BRW_OPCODE_##op, dst, src0); \
   }

#define ALU2(op)							\
   vec4_instruction *							\
   vec4_visitor::op(const dst_reg &dst, const src_reg &src0,		\
                    const src_reg &src1)				\
   {									\
      return new(mem_ctx) vec4_instruction(BRW_OPCODE_##op, dst,        \
                                           src0, src1);                 \
   }

#define ALU2_ACC(op)							\
   vec4_instruction *							\
   vec4_visitor::op(const dst_reg &dst, const src_reg &src0,		\
                    const src_reg &src1)				\
   {									\
      vec4_instruction *inst = new(mem_ctx) vec4_instruction(           \
                       BRW_OPCODE_##op, dst, src0, src1);		\
      inst->writes_accumulator = true;                                  \
      return inst;                                                      \
   }

#define ALU3(op)							\
   vec4_instruction *							\
   vec4_visitor::op(const dst_reg &dst, const src_reg &src0,		\
                    const src_reg &src1, const src_reg &src2)		\
   {									\
      assert(devinfo->gen >= 6);						\
      return new(mem_ctx) vec4_instruction(BRW_OPCODE_##op, dst,	\
					   src0, src1, src2);		\
   }

ALU1(NOT)
ALU1(MOV)
ALU1(FRC)
ALU1(RNDD)
ALU1(RNDE)
ALU1(RNDZ)
ALU1(F32TO16)
ALU1(F16TO32)
ALU2(ADD)
ALU2(MUL)
ALU2_ACC(MACH)
ALU2(AND)
ALU2(OR)
ALU2(XOR)
ALU2(DP3)
ALU2(DP4)
ALU2(DPH)
ALU2(SHL)
ALU2(SHR)
ALU2(ASR)
ALU3(LRP)
ALU1(BFREV)
ALU3(BFE)
ALU2(BFI1)
ALU3(BFI2)
ALU1(FBH)
ALU1(FBL)
ALU1(CBIT)
ALU3(MAD)
ALU2_ACC(ADDC)
ALU2_ACC(SUBB)
ALU2(MAC)

/** Gen4 predicated IF. */
vec4_instruction *
vec4_visitor::IF(enum brw_predicate predicate)
{
   vec4_instruction *inst;

   inst = new(mem_ctx) vec4_instruction(BRW_OPCODE_IF);
   inst->predicate = predicate;

   return inst;
}

/** Gen6 IF with embedded comparison. */
vec4_instruction *
vec4_visitor::IF(src_reg src0, src_reg src1,
                 enum brw_conditional_mod condition)
{
   assert(devinfo->gen == 6);

   vec4_instruction *inst;

   resolve_ud_negate(&src0);
   resolve_ud_negate(&src1);

   inst = new(mem_ctx) vec4_instruction(BRW_OPCODE_IF, dst_null_d(),
					src0, src1);
   inst->conditional_mod = condition;

   return inst;
}

/**
 * CMP: Sets the low bit of the destination channels with the result
 * of the comparison, while the upper bits are undefined, and updates
 * the flag register with the packed 16 bits of the result.
 */
vec4_instruction *
vec4_visitor::CMP(dst_reg dst, src_reg src0, src_reg src1,
                  enum brw_conditional_mod condition)
{
   vec4_instruction *inst;

   /* Take the instruction:
    *
    * CMP null<d> src0<f> src1<f>
    *
    * Original gen4 does type conversion to the destination type before
    * comparison, producing garbage results for floating point comparisons.
    *
    * The destination type doesn't matter on newer generations, so we set the
    * type to match src0 so we can compact the instruction.
    */
   dst.type = src0.type;

   resolve_ud_negate(&src0);
   resolve_ud_negate(&src1);

   inst = new(mem_ctx) vec4_instruction(BRW_OPCODE_CMP, dst, src0, src1);
   inst->conditional_mod = condition;

   return inst;
}

vec4_instruction *
vec4_visitor::SCRATCH_READ(const dst_reg &dst, const src_reg &index)
{
   vec4_instruction *inst;

   inst = new(mem_ctx) vec4_instruction(SHADER_OPCODE_GEN4_SCRATCH_READ,
					dst, index);
   inst->base_mrf = FIRST_SPILL_MRF(devinfo->gen) + 1;
   inst->mlen = 2;

   return inst;
}

vec4_instruction *
vec4_visitor::SCRATCH_WRITE(const dst_reg &dst, const src_reg &src,
                            const src_reg &index)
{
   vec4_instruction *inst;

   inst = new(mem_ctx) vec4_instruction(SHADER_OPCODE_GEN4_SCRATCH_WRITE,
					dst, src, index);
   inst->base_mrf = FIRST_SPILL_MRF(devinfo->gen);
   inst->mlen = 3;

   return inst;
}

src_reg
vec4_visitor::fix_3src_operand(const src_reg &src)
{
   /* Using vec4 uniforms in SIMD4x2 programs is difficult. You'd like to be
    * able to use vertical stride of zero to replicate the vec4 uniform, like
    *
    *    g3<0;4,1>:f - [0, 4][1, 5][2, 6][3, 7]
    *
    * But you can't, since vertical stride is always four in three-source
    * instructions. Instead, insert a MOV instruction to do the replication so
    * that the three-source instruction can consume it.
    */

   /* The MOV is only needed if the source is a uniform or immediate. */
   if (src.file != UNIFORM && src.file != IMM)
      return src;

   if (src.file == UNIFORM && brw_is_single_value_swizzle(src.swizzle))
      return src;

   dst_reg expanded = dst_reg(this, glsl_type::vec4_type);
   expanded.type = src.type;
   emit(VEC4_OPCODE_UNPACK_UNIFORM, expanded, src);
   return src_reg(expanded);
}

src_reg
vec4_visitor::resolve_source_modifiers(const src_reg &src)
{
   if (!src.abs && !src.negate)
      return src;

   dst_reg resolved = dst_reg(this, glsl_type::ivec4_type);
   resolved.type = src.type;
   emit(MOV(resolved, src));

   return src_reg(resolved);
}

src_reg
vec4_visitor::fix_math_operand(const src_reg &src)
{
   if (devinfo->gen < 6 || devinfo->gen >= 8 || src.file == BAD_FILE)
      return src;

   /* The gen6 math instruction ignores the source modifiers --
    * swizzle, abs, negate, and at least some parts of the register
    * region description.
    *
    * Rather than trying to enumerate all these cases, *always* expand the
    * operand to a temp GRF for gen6.
    *
    * For gen7, keep the operand as-is, except if immediate, which gen7 still
    * can't use.
    */

   if (devinfo->gen == 7 && src.file != IMM)
      return src;

   dst_reg expanded = dst_reg(this, glsl_type::vec4_type);
   expanded.type = src.type;
   emit(MOV(expanded, src));
   return src_reg(expanded);
}

vec4_instruction *
vec4_visitor::emit_math(enum opcode opcode,
                        const dst_reg &dst,
                        const src_reg &src0, const src_reg &src1)
{
   vec4_instruction *math =
      emit(opcode, dst, fix_math_operand(src0), fix_math_operand(src1));

   if (devinfo->gen == 6 && dst.writemask != WRITEMASK_XYZW) {
      /* MATH on Gen6 must be align1, so we can't do writemasks. */
      math->dst = dst_reg(this, glsl_type::vec4_type);
      math->dst.type = dst.type;
      math = emit(MOV(dst, src_reg(math->dst)));
   } else if (devinfo->gen < 6) {
      math->base_mrf = 1;
      math->mlen = src1.file == BAD_FILE ? 1 : 2;
   }

   return math;
}

void
vec4_visitor::emit_pack_half_2x16(dst_reg dst, src_reg src0)
{
   if (devinfo->gen < 7) {
      unreachable("ir_unop_pack_half_2x16 should be lowered");
   }

   assert(dst.type == BRW_REGISTER_TYPE_UD);
   assert(src0.type == BRW_REGISTER_TYPE_F);

   /* From the Ivybridge PRM, Vol4, Part3, Section 6.27 f32to16:
    *
    *   Because this instruction does not have a 16-bit floating-point type,
    *   the destination data type must be Word (W).
    *
    *   The destination must be DWord-aligned and specify a horizontal stride
    *   (HorzStride) of 2. The 16-bit result is stored in the lower word of
    *   each destination channel and the upper word is not modified.
    *
    * The above restriction implies that the f32to16 instruction must use
    * align1 mode, because only in align1 mode is it possible to specify
    * horizontal stride.  We choose here to defy the hardware docs and emit
    * align16 instructions.
    *
    * (I [chadv] did attempt to emit align1 instructions for VS f32to16
    * instructions. I was partially successful in that the code passed all
    * tests.  However, the code was dubiously correct and fragile, and the
    * tests were not harsh enough to probe that frailty. Not trusting the
    * code, I chose instead to remain in align16 mode in defiance of the hw
    * docs).
    *
    * I've [chadv] experimentally confirmed that, on gen7 hardware and the
    * simulator, emitting a f32to16 in align16 mode with UD as destination
    * data type is safe. The behavior differs from that specified in the PRM
    * in that the upper word of each destination channel is cleared to 0.
    */

   dst_reg tmp_dst(this, glsl_type::uvec2_type);
   src_reg tmp_src(tmp_dst);

#if 0
   /* Verify the undocumented behavior on which the following instructions
    * rely.  If f32to16 fails to clear the upper word of the X and Y channels,
    * then the result of the bit-or instruction below will be incorrect.
    *
    * You should inspect the disasm output in order to verify that the MOV is
    * not optimized away.
    */
   emit(MOV(tmp_dst, brw_imm_ud(0x12345678u)));
#endif

   /* Give tmp the form below, where "." means untouched.
    *
    *     w z          y          x w z          y          x
    *   |.|.|0x0000hhhh|0x0000llll|.|.|0x0000hhhh|0x0000llll|
    *
    * That the upper word of each write-channel be 0 is required for the
    * following bit-shift and bit-or instructions to work. Note that this
    * relies on the undocumented hardware behavior mentioned above.
    */
   tmp_dst.writemask = WRITEMASK_XY;
   emit(F32TO16(tmp_dst, src0));

   /* Give the write-channels of dst the form:
    *   0xhhhh0000
    */
   tmp_src.swizzle = BRW_SWIZZLE_YYYY;
   emit(SHL(dst, tmp_src, brw_imm_ud(16u)));

   /* Finally, give the write-channels of dst the form of packHalf2x16's
    * output:
    *   0xhhhhllll
    */
   tmp_src.swizzle = BRW_SWIZZLE_XXXX;
   emit(OR(dst, src_reg(dst), tmp_src));
}

void
vec4_visitor::emit_unpack_half_2x16(dst_reg dst, src_reg src0)
{
   if (devinfo->gen < 7) {
      unreachable("ir_unop_unpack_half_2x16 should be lowered");
   }

   assert(dst.type == BRW_REGISTER_TYPE_F);
   assert(src0.type == BRW_REGISTER_TYPE_UD);

   /* From the Ivybridge PRM, Vol4, Part3, Section 6.26 f16to32:
    *
    *   Because this instruction does not have a 16-bit floating-point type,
    *   the source data type must be Word (W). The destination type must be
    *   F (Float).
    *
    * To use W as the source data type, we must adjust horizontal strides,
    * which is only possible in align1 mode. All my [chadv] attempts at
    * emitting align1 instructions for unpackHalf2x16 failed to pass the
    * Piglit tests, so I gave up.
    *
    * I've verified that, on gen7 hardware and the simulator, it is safe to
    * emit f16to32 in align16 mode with UD as source data type.
    */

   dst_reg tmp_dst(this, glsl_type::uvec2_type);
   src_reg tmp_src(tmp_dst);

   tmp_dst.writemask = WRITEMASK_X;
   emit(AND(tmp_dst, src0, brw_imm_ud(0xffffu)));

   tmp_dst.writemask = WRITEMASK_Y;
   emit(SHR(tmp_dst, src0, brw_imm_ud(16u)));

   dst.writemask = WRITEMASK_XY;
   emit(F16TO32(dst, tmp_src));
}

void
vec4_visitor::emit_unpack_unorm_4x8(const dst_reg &dst, src_reg src0)
{
   /* Instead of splitting the 32-bit integer, shifting, and ORing it back
    * together, we can shift it by <0, 8, 16, 24>. The packed integer immediate
    * is not suitable to generate the shift values, but we can use the packed
    * vector float and a type-converting MOV.
    */
   dst_reg shift(this, glsl_type::uvec4_type);
   emit(MOV(shift, brw_imm_vf4(0x00, 0x60, 0x70, 0x78)));

   dst_reg shifted(this, glsl_type::uvec4_type);
   src0.swizzle = BRW_SWIZZLE_XXXX;
   emit(SHR(shifted, src0, src_reg(shift)));

   shifted.type = BRW_REGISTER_TYPE_UB;
   dst_reg f(this, glsl_type::vec4_type);
   emit(VEC4_OPCODE_MOV_BYTES, f, src_reg(shifted));

   emit(MUL(dst, src_reg(f), brw_imm_f(1.0f / 255.0f)));
}

void
vec4_visitor::emit_unpack_snorm_4x8(const dst_reg &dst, src_reg src0)
{
   /* Instead of splitting the 32-bit integer, shifting, and ORing it back
    * together, we can shift it by <0, 8, 16, 24>. The packed integer immediate
    * is not suitable to generate the shift values, but we can use the packed
    * vector float and a type-converting MOV.
    */
   dst_reg shift(this, glsl_type::uvec4_type);
   emit(MOV(shift, brw_imm_vf4(0x00, 0x60, 0x70, 0x78)));

   dst_reg shifted(this, glsl_type::uvec4_type);
   src0.swizzle = BRW_SWIZZLE_XXXX;
   emit(SHR(shifted, src0, src_reg(shift)));

   shifted.type = BRW_REGISTER_TYPE_B;
   dst_reg f(this, glsl_type::vec4_type);
   emit(VEC4_OPCODE_MOV_BYTES, f, src_reg(shifted));

   dst_reg scaled(this, glsl_type::vec4_type);
   emit(MUL(scaled, src_reg(f), brw_imm_f(1.0f / 127.0f)));

   dst_reg max(this, glsl_type::vec4_type);
   emit_minmax(BRW_CONDITIONAL_GE, max, src_reg(scaled), brw_imm_f(-1.0f));
   emit_minmax(BRW_CONDITIONAL_L, dst, src_reg(max), brw_imm_f(1.0f));
}

void
vec4_visitor::emit_pack_unorm_4x8(const dst_reg &dst, const src_reg &src0)
{
   dst_reg saturated(this, glsl_type::vec4_type);
   vec4_instruction *inst = emit(MOV(saturated, src0));
   inst->saturate = true;

   dst_reg scaled(this, glsl_type::vec4_type);
   emit(MUL(scaled, src_reg(saturated), brw_imm_f(255.0f)));

   dst_reg rounded(this, glsl_type::vec4_type);
   emit(RNDE(rounded, src_reg(scaled)));

   dst_reg u(this, glsl_type::uvec4_type);
   emit(MOV(u, src_reg(rounded)));

   src_reg bytes(u);
   emit(VEC4_OPCODE_PACK_BYTES, dst, bytes);
}

void
vec4_visitor::emit_pack_snorm_4x8(const dst_reg &dst, const src_reg &src0)
{
   dst_reg max(this, glsl_type::vec4_type);
   emit_minmax(BRW_CONDITIONAL_GE, max, src0, brw_imm_f(-1.0f));

   dst_reg min(this, glsl_type::vec4_type);
   emit_minmax(BRW_CONDITIONAL_L, min, src_reg(max), brw_imm_f(1.0f));

   dst_reg scaled(this, glsl_type::vec4_type);
   emit(MUL(scaled, src_reg(min), brw_imm_f(127.0f)));

   dst_reg rounded(this, glsl_type::vec4_type);
   emit(RNDE(rounded, src_reg(scaled)));

   dst_reg i(this, glsl_type::ivec4_type);
   emit(MOV(i, src_reg(rounded)));

   src_reg bytes(i);
   emit(VEC4_OPCODE_PACK_BYTES, dst, bytes);
}

/**
 * Returns the minimum number of vec4 elements needed to pack a type.
 *
 * For simple types, it will return 1 (a single vec4); for matrices, the
 * number of columns; for array and struct, the sum of the vec4_size of
 * each of its elements; and for sampler and atomic, zero.
 *
 * This method is useful to calculate how much register space is needed to
 * store a particular type.
 */
extern "C" int
type_size_vec4(const struct glsl_type *type)
{
   unsigned int i;
   int size;

   switch (type->base_type) {
   case GLSL_TYPE_UINT:
   case GLSL_TYPE_INT:
   case GLSL_TYPE_FLOAT:
   case GLSL_TYPE_BOOL:
      if (type->is_matrix()) {
	 return type->matrix_columns;
      } else {
	 /* Regardless of size of vector, it gets a vec4. This is bad
	  * packing for things like floats, but otherwise arrays become a
	  * mess.  Hopefully a later pass over the code can pack scalars
	  * down if appropriate.
	  */
	 return 1;
      }
   case GLSL_TYPE_ARRAY:
      assert(type->length > 0);
      return type_size_vec4(type->fields.array) * type->length;
   case GLSL_TYPE_STRUCT:
      size = 0;
      for (i = 0; i < type->length; i++) {
	 size += type_size_vec4(type->fields.structure[i].type);
      }
      return size;
   case GLSL_TYPE_SUBROUTINE:
      return 1;

   case GLSL_TYPE_SAMPLER:
      /* Samplers take up no register space, since they're baked in at
       * link time.
       */
      return 0;
   case GLSL_TYPE_ATOMIC_UINT:
      return 0;
   case GLSL_TYPE_IMAGE:
      return DIV_ROUND_UP(BRW_IMAGE_PARAM_SIZE, 4);
   case GLSL_TYPE_VOID:
   case GLSL_TYPE_DOUBLE:
   case GLSL_TYPE_ERROR:
   case GLSL_TYPE_INTERFACE:
      unreachable("not reached");
   }

   return 0;
}

src_reg::src_reg(class vec4_visitor *v, const struct glsl_type *type)
{
   init();

   this->file = VGRF;
   this->nr = v->alloc.allocate(type_size_vec4(type));

   if (type->is_array() || type->is_record()) {
      this->swizzle = BRW_SWIZZLE_NOOP;
   } else {
      this->swizzle = brw_swizzle_for_size(type->vector_elements);
   }

   this->type = brw_type_for_base_type(type);
}

src_reg::src_reg(class vec4_visitor *v, const struct glsl_type *type, int size)
{
   assert(size > 0);

   init();

   this->file = VGRF;
   this->nr = v->alloc.allocate(type_size_vec4(type) * size);

   this->swizzle = BRW_SWIZZLE_NOOP;

   this->type = brw_type_for_base_type(type);
}

dst_reg::dst_reg(class vec4_visitor *v, const struct glsl_type *type)
{
   init();

   this->file = VGRF;
   this->nr = v->alloc.allocate(type_size_vec4(type));

   if (type->is_array() || type->is_record()) {
      this->writemask = WRITEMASK_XYZW;
   } else {
      this->writemask = (1 << type->vector_elements) - 1;
   }

   this->type = brw_type_for_base_type(type);
}

vec4_instruction *
vec4_visitor::emit_minmax(enum brw_conditional_mod conditionalmod, dst_reg dst,
                          src_reg src0, src_reg src1)
{
   vec4_instruction *inst;

   if (devinfo->gen >= 6) {
      inst = emit(BRW_OPCODE_SEL, dst, src0, src1);
      inst->conditional_mod = conditionalmod;
   } else {
      emit(CMP(dst, src0, src1, conditionalmod));

      inst = emit(BRW_OPCODE_SEL, dst, src0, src1);
      inst->predicate = BRW_PREDICATE_NORMAL;
   }

   return inst;
}

vec4_instruction *
vec4_visitor::emit_lrp(const dst_reg &dst,
                       const src_reg &x, const src_reg &y, const src_reg &a)
{
   if (devinfo->gen >= 6) {
      /* Note that the instruction's argument order is reversed from GLSL
       * and the IR.
       */
     return emit(LRP(dst, fix_3src_operand(a), fix_3src_operand(y),
                     fix_3src_operand(x)));
   } else {
      /* Earlier generations don't support three source operations, so we
       * need to emit x*(1-a) + y*a.
       */
      dst_reg y_times_a           = dst_reg(this, glsl_type::vec4_type);
      dst_reg one_minus_a         = dst_reg(this, glsl_type::vec4_type);
      dst_reg x_times_one_minus_a = dst_reg(this, glsl_type::vec4_type);
      y_times_a.writemask           = dst.writemask;
      one_minus_a.writemask         = dst.writemask;
      x_times_one_minus_a.writemask = dst.writemask;

      emit(MUL(y_times_a, y, a));
      emit(ADD(one_minus_a, negate(a), brw_imm_f(1.0f)));
      emit(MUL(x_times_one_minus_a, x, src_reg(one_minus_a)));
      return emit(ADD(dst, src_reg(x_times_one_minus_a), src_reg(y_times_a)));
   }
}

/**
 * Emits the instructions needed to perform a pull constant load. before_block
 * and before_inst can be NULL in which case the instruction will be appended
 * to the end of the instruction list.
 */
void
vec4_visitor::emit_pull_constant_load_reg(dst_reg dst,
                                          src_reg surf_index,
                                          src_reg offset_reg,
                                          bblock_t *before_block,
                                          vec4_instruction *before_inst)
{
   assert((before_inst == NULL && before_block == NULL) ||
          (before_inst && before_block));

   vec4_instruction *pull;

   if (devinfo->gen >= 9) {
      /* Gen9+ needs a message header in order to use SIMD4x2 mode */
      src_reg header(this, glsl_type::uvec4_type, 2);

      pull = new(mem_ctx)
         vec4_instruction(VS_OPCODE_SET_SIMD4X2_HEADER_GEN9,
                          dst_reg(header));

      if (before_inst)
         emit_before(before_block, before_inst, pull);
      else
         emit(pull);

      dst_reg index_reg = retype(offset(dst_reg(header), 1),
                                 offset_reg.type);
      pull = MOV(writemask(index_reg, WRITEMASK_X), offset_reg);

      if (before_inst)
         emit_before(before_block, before_inst, pull);
      else
         emit(pull);

      pull = new(mem_ctx) vec4_instruction(VS_OPCODE_PULL_CONSTANT_LOAD_GEN7,
                                           dst,
                                           surf_index,
                                           header);
      pull->mlen = 2;
      pull->header_size = 1;
   } else if (devinfo->gen >= 7) {
      dst_reg grf_offset = dst_reg(this, glsl_type::int_type);

      grf_offset.type = offset_reg.type;

      pull = MOV(grf_offset, offset_reg);

      if (before_inst)
         emit_before(before_block, before_inst, pull);
      else
         emit(pull);

      pull = new(mem_ctx) vec4_instruction(VS_OPCODE_PULL_CONSTANT_LOAD_GEN7,
                                           dst,
                                           surf_index,
                                           src_reg(grf_offset));
      pull->mlen = 1;
   } else {
      pull = new(mem_ctx) vec4_instruction(VS_OPCODE_PULL_CONSTANT_LOAD,
                                           dst,
                                           surf_index,
                                           offset_reg);
      pull->base_mrf = FIRST_PULL_LOAD_MRF(devinfo->gen) + 1;
      pull->mlen = 1;
   }

   if (before_inst)
      emit_before(before_block, before_inst, pull);
   else
      emit(pull);
}

src_reg
vec4_visitor::emit_uniformize(const src_reg &src)
{
   const src_reg chan_index(this, glsl_type::uint_type);
   const dst_reg dst = retype(dst_reg(this, glsl_type::uint_type),
                              src.type);

   emit(SHADER_OPCODE_FIND_LIVE_CHANNEL, dst_reg(chan_index))
      ->force_writemask_all = true;
   emit(SHADER_OPCODE_BROADCAST, dst, src, chan_index)
      ->force_writemask_all = true;

   return src_reg(dst);
}

src_reg
vec4_visitor::emit_mcs_fetch(const glsl_type *coordinate_type,
                             src_reg coordinate, src_reg sampler)
{
   vec4_instruction *inst =
      new(mem_ctx) vec4_instruction(SHADER_OPCODE_TXF_MCS,
                                    dst_reg(this, glsl_type::uvec4_type));
   inst->base_mrf = 2;
   inst->src[1] = sampler;

   int param_base;

   if (devinfo->gen >= 9) {
      /* Gen9+ needs a message header in order to use SIMD4x2 mode */
      vec4_instruction *header_inst = new(mem_ctx)
         vec4_instruction(VS_OPCODE_SET_SIMD4X2_HEADER_GEN9,
                          dst_reg(MRF, inst->base_mrf));

      emit(header_inst);

      inst->mlen = 2;
      inst->header_size = 1;
      param_base = inst->base_mrf + 1;
   } else {
      inst->mlen = 1;
      param_base = inst->base_mrf;
   }

   /* parameters are: u, v, r, lod; lod will always be zero due to api restrictions */
   int coord_mask = (1 << coordinate_type->vector_elements) - 1;
   int zero_mask = 0xf & ~coord_mask;

   emit(MOV(dst_reg(MRF, param_base, coordinate_type, coord_mask),
            coordinate));

   emit(MOV(dst_reg(MRF, param_base, coordinate_type, zero_mask),
            brw_imm_d(0)));

   emit(inst);
   return src_reg(inst->dst);
}

bool
vec4_visitor::is_high_sampler(src_reg sampler)
{
   if (devinfo->gen < 8 && !devinfo->is_haswell)
      return false;

   return sampler.file != IMM || sampler.ud >= 16;
}

void
vec4_visitor::emit_texture(ir_texture_opcode op,
                           dst_reg dest,
                           const glsl_type *dest_type,
                           src_reg coordinate,
                           int coord_components,
                           src_reg shadow_comparitor,
                           src_reg lod, src_reg lod2,
                           src_reg sample_index,
                           uint32_t constant_offset,
                           src_reg offset_value,
                           src_reg mcs,
                           bool is_cube_array,
                           uint32_t sampler,
                           src_reg sampler_reg)
{
   /* The sampler can only meaningfully compute LOD for fragment shader
    * messages. For all other stages, we change the opcode to TXL and hardcode
    * the LOD to 0.
    *
    * textureQueryLevels() is implemented in terms of TXS so we need to pass a
    * valid LOD argument.
    */
   if (op == ir_tex || op == ir_query_levels) {
      assert(lod.file == BAD_FILE);
      lod = brw_imm_f(0.0f);
   }

   enum opcode opcode;
   switch (op) {
   case ir_tex: opcode = SHADER_OPCODE_TXL; break;
   case ir_txl: opcode = SHADER_OPCODE_TXL; break;
   case ir_txd: opcode = SHADER_OPCODE_TXD; break;
   case ir_txf: opcode = SHADER_OPCODE_TXF; break;
   case ir_txf_ms: opcode = (devinfo->gen >= 9 ? SHADER_OPCODE_TXF_CMS_W :
                             SHADER_OPCODE_TXF_CMS); break;
   case ir_txs: opcode = SHADER_OPCODE_TXS; break;
   case ir_tg4: opcode = offset_value.file != BAD_FILE
                         ? SHADER_OPCODE_TG4_OFFSET : SHADER_OPCODE_TG4; break;
   case ir_query_levels: opcode = SHADER_OPCODE_TXS; break;
   case ir_texture_samples: opcode = SHADER_OPCODE_SAMPLEINFO; break;
   case ir_txb:
      unreachable("TXB is not valid for vertex shaders.");
   case ir_lod:
      unreachable("LOD is not valid for vertex shaders.");
   case ir_samples_identical: {
      /* There are some challenges implementing this for vec4, and it seems
       * unlikely to be used anyway.  For now, just return false ways.
       */
      emit(MOV(dest, brw_imm_ud(0u)));
      return;
   }
   default:
      unreachable("Unrecognized tex op");
   }

   vec4_instruction *inst = new(mem_ctx) vec4_instruction(
      opcode, dst_reg(this, dest_type));

   inst->offset = constant_offset;

   /* The message header is necessary for:
    * - Gen4 (always)
    * - Gen9+ for selecting SIMD4x2
    * - Texel offsets
    * - Gather channel selection
    * - Sampler indices too large to fit in a 4-bit value.
    * - Sampleinfo message - takes no parameters, but mlen = 0 is illegal
    */
   inst->header_size =
      (devinfo->gen < 5 || devinfo->gen >= 9 ||
       inst->offset != 0 || op == ir_tg4 ||
       op == ir_texture_samples ||
       is_high_sampler(sampler_reg)) ? 1 : 0;
   inst->base_mrf = 2;
   inst->mlen = inst->header_size;
   inst->dst.writemask = WRITEMASK_XYZW;
   inst->shadow_compare = shadow_comparitor.file != BAD_FILE;

   inst->src[1] = sampler_reg;

   /* MRF for the first parameter */
   int param_base = inst->base_mrf + inst->header_size;

   if (op == ir_txs || op == ir_query_levels) {
      int writemask = devinfo->gen == 4 ? WRITEMASK_W : WRITEMASK_X;
      emit(MOV(dst_reg(MRF, param_base, lod.type, writemask), lod));
      inst->mlen++;
   } else if (op == ir_texture_samples) {
      inst->dst.writemask = WRITEMASK_X;
   } else {
      /* Load the coordinate */
      /* FINISHME: gl_clamp_mask and saturate */
      int coord_mask = (1 << coord_components) - 1;
      int zero_mask = 0xf & ~coord_mask;

      emit(MOV(dst_reg(MRF, param_base, coordinate.type, coord_mask),
               coordinate));
      inst->mlen++;

      if (zero_mask != 0) {
         emit(MOV(dst_reg(MRF, param_base, coordinate.type, zero_mask),
                  brw_imm_d(0)));
      }
      /* Load the shadow comparitor */
      if (shadow_comparitor.file != BAD_FILE && op != ir_txd && (op != ir_tg4 || offset_value.file == BAD_FILE)) {
	 emit(MOV(dst_reg(MRF, param_base + 1, shadow_comparitor.type,
			  WRITEMASK_X),
		  shadow_comparitor));
	 inst->mlen++;
      }

      /* Load the LOD info */
      if (op == ir_tex || op == ir_txl) {
	 int mrf, writemask;
	 if (devinfo->gen >= 5) {
	    mrf = param_base + 1;
	    if (shadow_comparitor.file != BAD_FILE) {
	       writemask = WRITEMASK_Y;
	       /* mlen already incremented */
	    } else {
	       writemask = WRITEMASK_X;
	       inst->mlen++;
	    }
	 } else /* devinfo->gen == 4 */ {
	    mrf = param_base;
	    writemask = WRITEMASK_W;
	 }
	 emit(MOV(dst_reg(MRF, mrf, lod.type, writemask), lod));
      } else if (op == ir_txf) {
         emit(MOV(dst_reg(MRF, param_base, lod.type, WRITEMASK_W), lod));
      } else if (op == ir_txf_ms) {
         emit(MOV(dst_reg(MRF, param_base + 1, sample_index.type, WRITEMASK_X),
                  sample_index));
         if (opcode == SHADER_OPCODE_TXF_CMS_W) {
            /* MCS data is stored in the first two channels of ‘mcs’, but we
             * need to get it into the .y and .z channels of the second vec4
             * of params.
             */
            mcs.swizzle = BRW_SWIZZLE4(0, 0, 1, 1);
            emit(MOV(dst_reg(MRF, param_base + 1,
                             glsl_type::uint_type, WRITEMASK_YZ),
                     mcs));
         } else if (devinfo->gen >= 7) {
            /* MCS data is in the first channel of `mcs`, but we need to get it into
             * the .y channel of the second vec4 of params, so replicate .x across
             * the whole vec4 and then mask off everything except .y
             */
            mcs.swizzle = BRW_SWIZZLE_XXXX;
            emit(MOV(dst_reg(MRF, param_base + 1, glsl_type::uint_type, WRITEMASK_Y),
                     mcs));
         }
         inst->mlen++;
      } else if (op == ir_txd) {
         const brw_reg_type type = lod.type;

	 if (devinfo->gen >= 5) {
	    lod.swizzle = BRW_SWIZZLE4(SWIZZLE_X,SWIZZLE_X,SWIZZLE_Y,SWIZZLE_Y);
	    lod2.swizzle = BRW_SWIZZLE4(SWIZZLE_X,SWIZZLE_X,SWIZZLE_Y,SWIZZLE_Y);
	    emit(MOV(dst_reg(MRF, param_base + 1, type, WRITEMASK_XZ), lod));
	    emit(MOV(dst_reg(MRF, param_base + 1, type, WRITEMASK_YW), lod2));
	    inst->mlen++;

	    if (dest_type->vector_elements == 3 || shadow_comparitor.file != BAD_FILE) {
	       lod.swizzle = BRW_SWIZZLE_ZZZZ;
	       lod2.swizzle = BRW_SWIZZLE_ZZZZ;
	       emit(MOV(dst_reg(MRF, param_base + 2, type, WRITEMASK_X), lod));
	       emit(MOV(dst_reg(MRF, param_base + 2, type, WRITEMASK_Y), lod2));
	       inst->mlen++;

               if (shadow_comparitor.file != BAD_FILE) {
                  emit(MOV(dst_reg(MRF, param_base + 2,
                                   shadow_comparitor.type, WRITEMASK_Z),
                           shadow_comparitor));
               }
	    }
	 } else /* devinfo->gen == 4 */ {
	    emit(MOV(dst_reg(MRF, param_base + 1, type, WRITEMASK_XYZ), lod));
	    emit(MOV(dst_reg(MRF, param_base + 2, type, WRITEMASK_XYZ), lod2));
	    inst->mlen += 2;
	 }
      } else if (op == ir_tg4 && offset_value.file != BAD_FILE) {
         if (shadow_comparitor.file != BAD_FILE) {
            emit(MOV(dst_reg(MRF, param_base, shadow_comparitor.type, WRITEMASK_W),
                     shadow_comparitor));
         }

         emit(MOV(dst_reg(MRF, param_base + 1, glsl_type::ivec2_type, WRITEMASK_XY),
                  offset_value));
         inst->mlen++;
      }
   }

   emit(inst);

   /* fixup num layers (z) for cube arrays: hardware returns faces * layers;
    * spec requires layers.
    */
   if (op == ir_txs && is_cube_array) {
      emit_math(SHADER_OPCODE_INT_QUOTIENT,
                writemask(inst->dst, WRITEMASK_Z),
                src_reg(inst->dst), brw_imm_d(6));
   }

   if (devinfo->gen == 6 && op == ir_tg4) {
      emit_gen6_gather_wa(key_tex->gen6_gather_wa[sampler], inst->dst);
   }

   swizzle_result(op, dest,
                  src_reg(inst->dst), sampler, dest_type);
}

/**
 * Apply workarounds for Gen6 gather with UINT/SINT
 */
void
vec4_visitor::emit_gen6_gather_wa(uint8_t wa, dst_reg dst)
{
   if (!wa)
      return;

   int width = (wa & WA_8BIT) ? 8 : 16;
   dst_reg dst_f = dst;
   dst_f.type = BRW_REGISTER_TYPE_F;

   /* Convert from UNORM to UINT */
   emit(MUL(dst_f, src_reg(dst_f), brw_imm_f((float)((1 << width) - 1))));
   emit(MOV(dst, src_reg(dst_f)));

   if (wa & WA_SIGN) {
      /* Reinterpret the UINT value as a signed INT value by
       * shifting the sign bit into place, then shifting back
       * preserving sign.
       */
      emit(SHL(dst, src_reg(dst), brw_imm_d(32 - width)));
      emit(ASR(dst, src_reg(dst), brw_imm_d(32 - width)));
   }
}

/**
 * Set up the gather channel based on the swizzle, for gather4.
 */
uint32_t
vec4_visitor::gather_channel(unsigned gather_component, uint32_t sampler)
{
   int swiz = GET_SWZ(key_tex->swizzles[sampler], gather_component);
   switch (swiz) {
      case SWIZZLE_X: return 0;
      case SWIZZLE_Y:
         /* gather4 sampler is broken for green channel on RG32F --
          * we must ask for blue instead.
          */
         if (key_tex->gather_channel_quirk_mask & (1 << sampler))
            return 2;
         return 1;
      case SWIZZLE_Z: return 2;
      case SWIZZLE_W: return 3;
      default:
         unreachable("Not reached"); /* zero, one swizzles handled already */
   }
}

void
vec4_visitor::swizzle_result(ir_texture_opcode op, dst_reg dest,
                             src_reg orig_val, uint32_t sampler,
                             const glsl_type *dest_type)
{
   int s = key_tex->swizzles[sampler];

   dst_reg swizzled_result = dest;

   if (op == ir_query_levels) {
      /* # levels is in .w */
      orig_val.swizzle = BRW_SWIZZLE4(SWIZZLE_W, SWIZZLE_W, SWIZZLE_W, SWIZZLE_W);
      emit(MOV(swizzled_result, orig_val));
      return;
   }

   if (op == ir_txs || dest_type == glsl_type::float_type
			|| s == SWIZZLE_NOOP || op == ir_tg4) {
      emit(MOV(swizzled_result, orig_val));
      return;
   }


   int zero_mask = 0, one_mask = 0, copy_mask = 0;
   int swizzle[4] = {0};

   for (int i = 0; i < 4; i++) {
      switch (GET_SWZ(s, i)) {
      case SWIZZLE_ZERO:
	 zero_mask |= (1 << i);
	 break;
      case SWIZZLE_ONE:
	 one_mask |= (1 << i);
	 break;
      default:
	 copy_mask |= (1 << i);
	 swizzle[i] = GET_SWZ(s, i);
	 break;
      }
   }

   if (copy_mask) {
      orig_val.swizzle = BRW_SWIZZLE4(swizzle[0], swizzle[1], swizzle[2], swizzle[3]);
      swizzled_result.writemask = copy_mask;
      emit(MOV(swizzled_result, orig_val));
   }

   if (zero_mask) {
      swizzled_result.writemask = zero_mask;
      emit(MOV(swizzled_result, brw_imm_f(0.0f)));
   }

   if (one_mask) {
      swizzled_result.writemask = one_mask;
      emit(MOV(swizzled_result, brw_imm_f(1.0f)));
   }
}

void
vec4_visitor::gs_emit_vertex(int stream_id)
{
   unreachable("not reached");
}

void
vec4_visitor::gs_end_primitive()
{
   unreachable("not reached");
}

void
vec4_visitor::emit_untyped_atomic(unsigned atomic_op, unsigned surf_index,
                                  dst_reg dst, src_reg surf_offset,
                                  src_reg src0, src_reg src1)
{
   unsigned mlen = 1 + (src0.file != BAD_FILE) + (src1.file != BAD_FILE);
   src_reg src_payload(this, glsl_type::uint_type, mlen);
   dst_reg payload(src_payload);
   payload.writemask = WRITEMASK_X;

   /* Set the atomic operation offset. */
   emit(MOV(offset(payload, 0), surf_offset));
   unsigned i = 1;

   /* Set the atomic operation arguments. */
   if (src0.file != BAD_FILE) {
      emit(MOV(offset(payload, i), src0));
      i++;
   }

   if (src1.file != BAD_FILE) {
      emit(MOV(offset(payload, i), src1));
      i++;
   }

   /* Emit the instruction.  Note that this maps to the normal SIMD8
    * untyped atomic message on Ivy Bridge, but that's OK because
    * unused channels will be masked out.
    */
   vec4_instruction *inst = emit(SHADER_OPCODE_UNTYPED_ATOMIC, dst,
                                 src_payload,
                                 brw_imm_ud(surf_index), brw_imm_ud(atomic_op));
   inst->mlen = mlen;
}

void
vec4_visitor::emit_untyped_surface_read(unsigned surf_index, dst_reg dst,
                                        src_reg surf_offset)
{
   dst_reg offset(this, glsl_type::uint_type);
   offset.writemask = WRITEMASK_X;

   /* Set the surface read offset. */
   emit(MOV(offset, surf_offset));

   /* Emit the instruction.  Note that this maps to the normal SIMD8
    * untyped surface read message, but that's OK because unused
    * channels will be masked out.
    */
   vec4_instruction *inst = emit(SHADER_OPCODE_UNTYPED_SURFACE_READ, dst,
                                 src_reg(offset),
                                 brw_imm_ud(surf_index), brw_imm_d(1));
   inst->mlen = 1;
}

void
vec4_visitor::emit_ndc_computation()
{
   if (output_reg[VARYING_SLOT_POS].file == BAD_FILE)
      return;

   /* Get the position */
   src_reg pos = src_reg(output_reg[VARYING_SLOT_POS]);

   /* Build ndc coords, which are (x/w, y/w, z/w, 1/w) */
   dst_reg ndc = dst_reg(this, glsl_type::vec4_type);
   output_reg[BRW_VARYING_SLOT_NDC] = ndc;

   current_annotation = "NDC";
   dst_reg ndc_w = ndc;
   ndc_w.writemask = WRITEMASK_W;
   src_reg pos_w = pos;
   pos_w.swizzle = BRW_SWIZZLE4(SWIZZLE_W, SWIZZLE_W, SWIZZLE_W, SWIZZLE_W);
   emit_math(SHADER_OPCODE_RCP, ndc_w, pos_w);

   dst_reg ndc_xyz = ndc;
   ndc_xyz.writemask = WRITEMASK_XYZ;

   emit(MUL(ndc_xyz, pos, src_reg(ndc_w)));
}

void
vec4_visitor::emit_psiz_and_flags(dst_reg reg)
{
   if (devinfo->gen < 6 &&
       ((prog_data->vue_map.slots_valid & VARYING_BIT_PSIZ) ||
        output_reg[VARYING_SLOT_CLIP_DIST0].file != BAD_FILE ||
        devinfo->has_negative_rhw_bug)) {
      dst_reg header1 = dst_reg(this, glsl_type::uvec4_type);
      dst_reg header1_w = header1;
      header1_w.writemask = WRITEMASK_W;

      emit(MOV(header1, brw_imm_ud(0u)));

      if (prog_data->vue_map.slots_valid & VARYING_BIT_PSIZ) {
	 src_reg psiz = src_reg(output_reg[VARYING_SLOT_PSIZ]);

	 current_annotation = "Point size";
	 emit(MUL(header1_w, psiz, brw_imm_f((float)(1 << 11))));
	 emit(AND(header1_w, src_reg(header1_w), brw_imm_d(0x7ff << 8)));
      }

      if (output_reg[VARYING_SLOT_CLIP_DIST0].file != BAD_FILE) {
         current_annotation = "Clipping flags";
         dst_reg flags0 = dst_reg(this, glsl_type::uint_type);
         dst_reg flags1 = dst_reg(this, glsl_type::uint_type);

         emit(CMP(dst_null_f(), src_reg(output_reg[VARYING_SLOT_CLIP_DIST0]), brw_imm_f(0.0f), BRW_CONDITIONAL_L));
         emit(VS_OPCODE_UNPACK_FLAGS_SIMD4X2, flags0, brw_imm_d(0));
         emit(OR(header1_w, src_reg(header1_w), src_reg(flags0)));

         emit(CMP(dst_null_f(), src_reg(output_reg[VARYING_SLOT_CLIP_DIST1]), brw_imm_f(0.0f), BRW_CONDITIONAL_L));
         emit(VS_OPCODE_UNPACK_FLAGS_SIMD4X2, flags1, brw_imm_d(0));
         emit(SHL(flags1, src_reg(flags1), brw_imm_d(4)));
         emit(OR(header1_w, src_reg(header1_w), src_reg(flags1)));
      }

      /* i965 clipping workaround:
       * 1) Test for -ve rhw
       * 2) If set,
       *      set ndc = (0,0,0,0)
       *      set ucp[6] = 1
       *
       * Later, clipping will detect ucp[6] and ensure the primitive is
       * clipped against all fixed planes.
       */
      if (devinfo->has_negative_rhw_bug &&
          output_reg[BRW_VARYING_SLOT_NDC].file != BAD_FILE) {
         src_reg ndc_w = src_reg(output_reg[BRW_VARYING_SLOT_NDC]);
         ndc_w.swizzle = BRW_SWIZZLE_WWWW;
         emit(CMP(dst_null_f(), ndc_w, brw_imm_f(0.0f), BRW_CONDITIONAL_L));
         vec4_instruction *inst;
         inst = emit(OR(header1_w, src_reg(header1_w), brw_imm_ud(1u << 6)));
         inst->predicate = BRW_PREDICATE_NORMAL;
         output_reg[BRW_VARYING_SLOT_NDC].type = BRW_REGISTER_TYPE_F;
         inst = emit(MOV(output_reg[BRW_VARYING_SLOT_NDC], brw_imm_f(0.0f)));
         inst->predicate = BRW_PREDICATE_NORMAL;
      }

      emit(MOV(retype(reg, BRW_REGISTER_TYPE_UD), src_reg(header1)));
   } else if (devinfo->gen < 6) {
      emit(MOV(retype(reg, BRW_REGISTER_TYPE_UD), brw_imm_ud(0u)));
   } else {
      emit(MOV(retype(reg, BRW_REGISTER_TYPE_D), brw_imm_d(0)));
      if (prog_data->vue_map.slots_valid & VARYING_BIT_PSIZ) {
         dst_reg reg_w = reg;
         reg_w.writemask = WRITEMASK_W;
         src_reg reg_as_src = src_reg(output_reg[VARYING_SLOT_PSIZ]);
         reg_as_src.type = reg_w.type;
         reg_as_src.swizzle = brw_swizzle_for_size(1);
         emit(MOV(reg_w, reg_as_src));
      }
      if (prog_data->vue_map.slots_valid & VARYING_BIT_LAYER) {
         dst_reg reg_y = reg;
         reg_y.writemask = WRITEMASK_Y;
         reg_y.type = BRW_REGISTER_TYPE_D;
         output_reg[VARYING_SLOT_LAYER].type = reg_y.type;
         emit(MOV(reg_y, src_reg(output_reg[VARYING_SLOT_LAYER])));
      }
      if (prog_data->vue_map.slots_valid & VARYING_BIT_VIEWPORT) {
         dst_reg reg_z = reg;
         reg_z.writemask = WRITEMASK_Z;
         reg_z.type = BRW_REGISTER_TYPE_D;
         output_reg[VARYING_SLOT_VIEWPORT].type = reg_z.type;
         emit(MOV(reg_z, src_reg(output_reg[VARYING_SLOT_VIEWPORT])));
      }
   }
}

vec4_instruction *
vec4_visitor::emit_generic_urb_slot(dst_reg reg, int varying)
{
   assert(varying < VARYING_SLOT_MAX);
   assert(output_reg[varying].type == reg.type);
   current_annotation = output_reg_annotation[varying];
   if (output_reg[varying].file != BAD_FILE)
      return emit(MOV(reg, src_reg(output_reg[varying])));
   else
      return NULL;
}

void
vec4_visitor::emit_urb_slot(dst_reg reg, int varying)
{
   reg.type = BRW_REGISTER_TYPE_F;
   output_reg[varying].type = reg.type;

   switch (varying) {
   case VARYING_SLOT_PSIZ:
   {
      /* PSIZ is always in slot 0, and is coupled with other flags. */
      current_annotation = "indices, point width, clip flags";
      emit_psiz_and_flags(reg);
      break;
   }
   case BRW_VARYING_SLOT_NDC:
      current_annotation = "NDC";
      if (output_reg[BRW_VARYING_SLOT_NDC].file != BAD_FILE)
         emit(MOV(reg, src_reg(output_reg[BRW_VARYING_SLOT_NDC])));
      break;
   case VARYING_SLOT_POS:
      current_annotation = "gl_Position";
      if (output_reg[VARYING_SLOT_POS].file != BAD_FILE)
         emit(MOV(reg, src_reg(output_reg[VARYING_SLOT_POS])));
      break;
   case VARYING_SLOT_EDGE:
      /* This is present when doing unfilled polygons.  We're supposed to copy
       * the edge flag from the user-provided vertex array
       * (glEdgeFlagPointer), or otherwise we'll copy from the current value
       * of that attribute (starts as 1.0f).  This is then used in clipping to
       * determine which edges should be drawn as wireframe.
       */
      current_annotation = "edge flag";
      emit(MOV(reg, src_reg(dst_reg(ATTR, VERT_ATTRIB_EDGEFLAG,
                                    glsl_type::float_type, WRITEMASK_XYZW))));
      break;
   case BRW_VARYING_SLOT_PAD:
      /* No need to write to this slot */
      break;
   default:
      emit_generic_urb_slot(reg, varying);
      break;
   }
}

static int
align_interleaved_urb_mlen(const struct brw_device_info *devinfo, int mlen)
{
   if (devinfo->gen >= 6) {
      /* URB data written (does not include the message header reg) must
       * be a multiple of 256 bits, or 2 VS registers.  See vol5c.5,
       * section 5.4.3.2.2: URB_INTERLEAVED.
       *
       * URB entries are allocated on a multiple of 1024 bits, so an
       * extra 128 bits written here to make the end align to 256 is
       * no problem.
       */
      if ((mlen % 2) != 1)
	 mlen++;
   }

   return mlen;
}


/**
 * Generates the VUE payload plus the necessary URB write instructions to
 * output it.
 *
 * The VUE layout is documented in Volume 2a.
 */
void
vec4_visitor::emit_vertex()
{
   /* MRF 0 is reserved for the debugger, so start with message header
    * in MRF 1.
    */
   int base_mrf = 1;
   int mrf = base_mrf;
   /* In the process of generating our URB write message contents, we
    * may need to unspill a register or load from an array.  Those
    * reads would use MRFs 14-15.
    */
   int max_usable_mrf = FIRST_SPILL_MRF(devinfo->gen);

   /* The following assertion verifies that max_usable_mrf causes an
    * even-numbered amount of URB write data, which will meet gen6's
    * requirements for length alignment.
    */
   assert ((max_usable_mrf - base_mrf) % 2 == 0);

   /* First mrf is the g0-based message header containing URB handles and
    * such.
    */
   emit_urb_write_header(mrf++);

   if (devinfo->gen < 6) {
      emit_ndc_computation();
   }

   /* We may need to split this up into several URB writes, so do them in a
    * loop.
    */
   int slot = 0;
   bool complete = false;
   do {
      /* URB offset is in URB row increments, and each of our MRFs is half of
       * one of those, since we're doing interleaved writes.
       */
      int offset = slot / 2;

      mrf = base_mrf + 1;
      for (; slot < prog_data->vue_map.num_slots; ++slot) {
         emit_urb_slot(dst_reg(MRF, mrf++),
                       prog_data->vue_map.slot_to_varying[slot]);

         /* If this was max_usable_mrf, we can't fit anything more into this
          * URB WRITE. Same thing if we reached the maximum length available.
          */
         if (mrf > max_usable_mrf ||
             align_interleaved_urb_mlen(devinfo, mrf - base_mrf + 1) > BRW_MAX_MSG_LENGTH) {
            slot++;
            break;
         }
      }

      complete = slot >= prog_data->vue_map.num_slots;
      current_annotation = "URB write";
      vec4_instruction *inst = emit_urb_write_opcode(complete);
      inst->base_mrf = base_mrf;
      inst->mlen = align_interleaved_urb_mlen(devinfo, mrf - base_mrf);
      inst->offset += offset;
   } while(!complete);
}


src_reg
vec4_visitor::get_scratch_offset(bblock_t *block, vec4_instruction *inst,
				 src_reg *reladdr, int reg_offset)
{
   /* Because we store the values to scratch interleaved like our
    * vertex data, we need to scale the vec4 index by 2.
    */
   int message_header_scale = 2;

   /* Pre-gen6, the message header uses byte offsets instead of vec4
    * (16-byte) offset units.
    */
   if (devinfo->gen < 6)
      message_header_scale *= 16;

   if (reladdr) {
      src_reg index = src_reg(this, glsl_type::int_type);

      emit_before(block, inst, ADD(dst_reg(index), *reladdr,
                                   brw_imm_d(reg_offset)));
      emit_before(block, inst, MUL(dst_reg(index), index,
                                   brw_imm_d(message_header_scale)));

      return index;
   } else {
      return brw_imm_d(reg_offset * message_header_scale);
   }
}

src_reg
vec4_visitor::get_pull_constant_offset(bblock_t * block, vec4_instruction *inst,
				       src_reg *reladdr, int reg_offset)
{
   if (reladdr) {
      src_reg index = src_reg(this, glsl_type::int_type);

      emit_before(block, inst, ADD(dst_reg(index), *reladdr,
                                   brw_imm_d(reg_offset)));
      emit_before(block, inst, MUL(dst_reg(index), index, brw_imm_d(16)));

      return index;
   } else if (devinfo->gen >= 8) {
      /* Store the offset in a GRF so we can send-from-GRF. */
      src_reg offset = src_reg(this, glsl_type::int_type);
      emit_before(block, inst, MOV(dst_reg(offset), brw_imm_d(reg_offset * 16)));
      return offset;
   } else {
      return brw_imm_d(reg_offset * 16);
   }
}

/**
 * Emits an instruction before @inst to load the value named by @orig_src
 * from scratch space at @base_offset to @temp.
 *
 * @base_offset is measured in 32-byte units (the size of a register).
 */
void
vec4_visitor::emit_scratch_read(bblock_t *block, vec4_instruction *inst,
				dst_reg temp, src_reg orig_src,
				int base_offset)
{
   int reg_offset = base_offset + orig_src.reg_offset;
   src_reg index = get_scratch_offset(block, inst, orig_src.reladdr,
                                      reg_offset);

   emit_before(block, inst, SCRATCH_READ(temp, index));
}

/**
 * Emits an instruction after @inst to store the value to be written
 * to @orig_dst to scratch space at @base_offset, from @temp.
 *
 * @base_offset is measured in 32-byte units (the size of a register).
 */
void
vec4_visitor::emit_scratch_write(bblock_t *block, vec4_instruction *inst,
                                 int base_offset)
{
   int reg_offset = base_offset + inst->dst.reg_offset;
   src_reg index = get_scratch_offset(block, inst, inst->dst.reladdr,
                                      reg_offset);

   /* Create a temporary register to store *inst's result in.
    *
    * We have to be careful in MOVing from our temporary result register in
    * the scratch write.  If we swizzle from channels of the temporary that
    * weren't initialized, it will confuse live interval analysis, which will
    * make spilling fail to make progress.
    */
   const src_reg temp = swizzle(retype(src_reg(this, glsl_type::vec4_type),
                                       inst->dst.type),
                                brw_swizzle_for_mask(inst->dst.writemask));
   dst_reg dst = dst_reg(brw_writemask(brw_vec8_grf(0, 0),
				       inst->dst.writemask));
   vec4_instruction *write = SCRATCH_WRITE(dst, temp, index);
   if (inst->opcode != BRW_OPCODE_SEL)
      write->predicate = inst->predicate;
   write->ir = inst->ir;
   write->annotation = inst->annotation;
   inst->insert_after(block, write);

   inst->dst.file = temp.file;
   inst->dst.nr = temp.nr;
   inst->dst.reg_offset = temp.reg_offset;
   inst->dst.reladdr = NULL;
}

/**
 * Checks if \p src and/or \p src.reladdr require a scratch read, and if so,
 * adds the scratch read(s) before \p inst. The function also checks for
 * recursive reladdr scratch accesses, issuing the corresponding scratch
 * loads and rewriting reladdr references accordingly.
 *
 * \return \p src if it did not require a scratch load, otherwise, the
 * register holding the result of the scratch load that the caller should
 * use to rewrite src.
 */
src_reg
vec4_visitor::emit_resolve_reladdr(int scratch_loc[], bblock_t *block,
                                   vec4_instruction *inst, src_reg src)
{
   /* Resolve recursive reladdr scratch access by calling ourselves
    * with src.reladdr
    */
   if (src.reladdr)
      *src.reladdr = emit_resolve_reladdr(scratch_loc, block, inst,
                                          *src.reladdr);

   /* Now handle scratch access on src */
   if (src.file == VGRF && scratch_loc[src.nr] != -1) {
      dst_reg temp = dst_reg(this, glsl_type::vec4_type);
      emit_scratch_read(block, inst, temp, src, scratch_loc[src.nr]);
      src.nr = temp.nr;
      src.reg_offset = temp.reg_offset;
      src.reladdr = NULL;
   }

   return src;
}

/**
 * We can't generally support array access in GRF space, because a
 * single instruction's destination can only span 2 contiguous
 * registers.  So, we send all GRF arrays that get variable index
 * access to scratch space.
 */
void
vec4_visitor::move_grf_array_access_to_scratch()
{
   int scratch_loc[this->alloc.count];
   memset(scratch_loc, -1, sizeof(scratch_loc));

   /* First, calculate the set of virtual GRFs that need to be punted
    * to scratch due to having any array access on them, and where in
    * scratch.
    */
   foreach_block_and_inst(block, vec4_instruction, inst, cfg) {
      if (inst->dst.file == VGRF && inst->dst.reladdr) {
         if (scratch_loc[inst->dst.nr] == -1) {
            scratch_loc[inst->dst.nr] = last_scratch;
            last_scratch += this->alloc.sizes[inst->dst.nr];
         }

         for (src_reg *iter = inst->dst.reladdr;
              iter->reladdr;
              iter = iter->reladdr) {
            if (iter->file == VGRF && scratch_loc[iter->nr] == -1) {
               scratch_loc[iter->nr] = last_scratch;
               last_scratch += this->alloc.sizes[iter->nr];
            }
         }
      }

      for (int i = 0 ; i < 3; i++) {
         for (src_reg *iter = &inst->src[i];
              iter->reladdr;
              iter = iter->reladdr) {
            if (iter->file == VGRF && scratch_loc[iter->nr] == -1) {
               scratch_loc[iter->nr] = last_scratch;
               last_scratch += this->alloc.sizes[iter->nr];
            }
         }
      }
   }

   /* Now, for anything that will be accessed through scratch, rewrite
    * it to load/store.  Note that this is a _safe list walk, because
    * we may generate a new scratch_write instruction after the one
    * we're processing.
    */
   foreach_block_and_inst_safe(block, vec4_instruction, inst, cfg) {
      /* Set up the annotation tracking for new generated instructions. */
      base_ir = inst->ir;
      current_annotation = inst->annotation;

      /* First handle scratch access on the dst. Notice we have to handle
       * the case where the dst's reladdr also points to scratch space.
       */
      if (inst->dst.reladdr)
         *inst->dst.reladdr = emit_resolve_reladdr(scratch_loc, block, inst,
                                                   *inst->dst.reladdr);

      /* Now that we have handled any (possibly recursive) reladdr scratch
       * accesses for dst we can safely do the scratch write for dst itself
       */
      if (inst->dst.file == VGRF && scratch_loc[inst->dst.nr] != -1)
         emit_scratch_write(block, inst, scratch_loc[inst->dst.nr]);

      /* Now handle scratch access on any src. In this case, since inst->src[i]
       * already is a src_reg, we can just call emit_resolve_reladdr with
       * inst->src[i] and it will take care of handling scratch loads for
       * both src and src.reladdr (recursively).
       */
      for (int i = 0 ; i < 3; i++) {
         inst->src[i] = emit_resolve_reladdr(scratch_loc, block, inst,
                                             inst->src[i]);
      }
   }
}

/**
 * Emits an instruction before @inst to load the value named by @orig_src
 * from the pull constant buffer (surface) at @base_offset to @temp.
 */
void
vec4_visitor::emit_pull_constant_load(bblock_t *block, vec4_instruction *inst,
				      dst_reg temp, src_reg orig_src,
				      int base_offset)
{
   int reg_offset = base_offset + orig_src.reg_offset;
   const unsigned index = prog_data->base.binding_table.pull_constants_start;
   src_reg offset = get_pull_constant_offset(block, inst, orig_src.reladdr,
                                             reg_offset);

   emit_pull_constant_load_reg(temp,
                               brw_imm_ud(index),
                               offset,
                               block, inst);

   brw_mark_surface_used(&prog_data->base, index);
}

/**
 * Implements array access of uniforms by inserting a
 * PULL_CONSTANT_LOAD instruction.
 *
 * Unlike temporary GRF array access (where we don't support it due to
 * the difficulty of doing relative addressing on instruction
 * destinations), we could potentially do array access of uniforms
 * that were loaded in GRF space as push constants.  In real-world
 * usage we've seen, though, the arrays being used are always larger
 * than we could load as push constants, so just always move all
 * uniform array access out to a pull constant buffer.
 */
void
vec4_visitor::move_uniform_array_access_to_pull_constants()
{
   int pull_constant_loc[this->uniforms];
   memset(pull_constant_loc, -1, sizeof(pull_constant_loc));
   bool nested_reladdr;

   /* Walk through and find array access of uniforms.  Put a copy of that
    * uniform in the pull constant buffer.
    *
    * Note that we don't move constant-indexed accesses to arrays.  No
    * testing has been done of the performance impact of this choice.
    */
   do {
      nested_reladdr = false;

      foreach_block_and_inst_safe(block, vec4_instruction, inst, cfg) {
         for (int i = 0 ; i < 3; i++) {
            if (inst->src[i].file != UNIFORM || !inst->src[i].reladdr)
               continue;

            int uniform = inst->src[i].nr;

            if (inst->src[i].reladdr->reladdr)
               nested_reladdr = true;  /* will need another pass */

            /* If this array isn't already present in the pull constant buffer,
             * add it.
             */
            if (pull_constant_loc[uniform] == -1) {
               const gl_constant_value **values =
                  &stage_prog_data->param[uniform * 4];

               pull_constant_loc[uniform] = stage_prog_data->nr_pull_params / 4;

               assert(uniform < uniform_array_size);
               for (int j = 0; j < uniform_size[uniform] * 4; j++) {
                  stage_prog_data->pull_param[stage_prog_data->nr_pull_params++]
                     = values[j];
               }
            }

            /* Set up the annotation tracking for new generated instructions. */
            base_ir = inst->ir;
            current_annotation = inst->annotation;

            dst_reg temp = dst_reg(this, glsl_type::vec4_type);

            emit_pull_constant_load(block, inst, temp, inst->src[i],
                                    pull_constant_loc[uniform]);

            inst->src[i].file = temp.file;
            inst->src[i].nr = temp.nr;
            inst->src[i].reg_offset = temp.reg_offset;
            inst->src[i].reladdr = NULL;
         }
      }
   } while (nested_reladdr);

   /* Now there are no accesses of the UNIFORM file with a reladdr, so
    * no need to track them as larger-than-vec4 objects.  This will be
    * relied on in cutting out unused uniform vectors from push
    * constants.
    */
   split_uniform_registers();
}

void
vec4_visitor::resolve_ud_negate(src_reg *reg)
{
   if (reg->type != BRW_REGISTER_TYPE_UD ||
       !reg->negate)
      return;

   src_reg temp = src_reg(this, glsl_type::uvec4_type);
   emit(BRW_OPCODE_MOV, dst_reg(temp), *reg);
   *reg = temp;
}

vec4_visitor::vec4_visitor(const struct brw_compiler *compiler,
                           void *log_data,
                           const struct brw_sampler_prog_key_data *key_tex,
                           struct brw_vue_prog_data *prog_data,
                           const nir_shader *shader,
			   void *mem_ctx,
                           bool no_spills,
                           int shader_time_index)
   : backend_shader(compiler, log_data, mem_ctx, shader, &prog_data->base),
     key_tex(key_tex),
     prog_data(prog_data),
     fail_msg(NULL),
     first_non_payload_grf(0),
     need_all_constants_in_pull_buffer(false),
     no_spills(no_spills),
     shader_time_index(shader_time_index),
     last_scratch(0)
{
   this->failed = false;

   this->base_ir = NULL;
   this->current_annotation = NULL;
   memset(this->output_reg_annotation, 0, sizeof(this->output_reg_annotation));

   this->virtual_grf_start = NULL;
   this->virtual_grf_end = NULL;
   this->live_intervals = NULL;

   this->max_grf = devinfo->gen >= 7 ? GEN7_MRF_HACK_START : BRW_MAX_GRF;

   this->uniforms = 0;

   /* Initialize uniform_array_size to at least 1 because pre-gen6 VS requires
    * at least one. See setup_uniforms() in brw_vec4.cpp.
    */
   this->uniform_array_size = 1;
   if (prog_data) {
      this->uniform_array_size =
         MAX2(DIV_ROUND_UP(stage_prog_data->nr_params, 4), 1);
   }

   this->uniform_size = rzalloc_array(mem_ctx, int, this->uniform_array_size);
}

vec4_visitor::~vec4_visitor()
{
}


void
vec4_visitor::fail(const char *format, ...)
{
   va_list va;
   char *msg;

   if (failed)
      return;

   failed = true;

   va_start(va, format);
   msg = ralloc_vasprintf(mem_ctx, format, va);
   va_end(va);
   msg = ralloc_asprintf(mem_ctx, "%s compile failed: %s\n", stage_abbrev, msg);

   this->fail_msg = msg;

   if (debug_enabled) {
      fprintf(stderr, "%s",  msg);
   }
}

} /* namespace brw */
