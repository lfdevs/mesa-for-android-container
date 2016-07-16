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
#include "brw_fs.h"
#include "brw_cfg.h"
#include "brw_vs.h"
#include "brw_nir.h"
#include "brw_vec4_builder.h"
#include "brw_vec4_live_variables.h"
#include "brw_dead_control_flow.h"
#include "program/prog_parameter.h"

#define MAX_INSTRUCTION (1 << 30)

using namespace brw;

namespace brw {

void
src_reg::init()
{
   memset(this, 0, sizeof(*this));

   this->file = BAD_FILE;
}

src_reg::src_reg(enum brw_reg_file file, int nr, const glsl_type *type)
{
   init();

   this->file = file;
   this->nr = nr;
   if (type && (type->is_scalar() || type->is_vector() || type->is_matrix()))
      this->swizzle = brw_swizzle_for_size(type->vector_elements);
   else
      this->swizzle = BRW_SWIZZLE_XYZW;
   if (type)
      this->type = brw_type_for_base_type(type);
}

/** Generic unset register constructor. */
src_reg::src_reg()
{
   init();
}

src_reg::src_reg(struct ::brw_reg reg) :
   backend_reg(reg)
{
   this->reg_offset = 0;
   this->reladdr = NULL;
}

src_reg::src_reg(const dst_reg &reg) :
   backend_reg(reg)
{
   this->reladdr = reg.reladdr;
   this->swizzle = brw_swizzle_for_mask(reg.writemask);
}

void
dst_reg::init()
{
   memset(this, 0, sizeof(*this));
   this->file = BAD_FILE;
   this->writemask = WRITEMASK_XYZW;
}

dst_reg::dst_reg()
{
   init();
}

dst_reg::dst_reg(enum brw_reg_file file, int nr)
{
   init();

   this->file = file;
   this->nr = nr;
}

dst_reg::dst_reg(enum brw_reg_file file, int nr, const glsl_type *type,
                 unsigned writemask)
{
   init();

   this->file = file;
   this->nr = nr;
   this->type = brw_type_for_base_type(type);
   this->writemask = writemask;
}

dst_reg::dst_reg(enum brw_reg_file file, int nr, brw_reg_type type,
                 unsigned writemask)
{
   init();

   this->file = file;
   this->nr = nr;
   this->type = type;
   this->writemask = writemask;
}

dst_reg::dst_reg(struct ::brw_reg reg) :
   backend_reg(reg)
{
   this->reg_offset = 0;
   this->reladdr = NULL;
}

dst_reg::dst_reg(const src_reg &reg) :
   backend_reg(reg)
{
   this->writemask = brw_mask_for_swizzle(reg.swizzle);
   this->reladdr = reg.reladdr;
}

bool
dst_reg::equals(const dst_reg &r) const
{
   return (this->backend_reg::equals(r) &&
           (reladdr == r.reladdr ||
            (reladdr && r.reladdr && reladdr->equals(*r.reladdr))));
}

bool
vec4_instruction::is_send_from_grf()
{
   switch (opcode) {
   case SHADER_OPCODE_SHADER_TIME_ADD:
   case VS_OPCODE_PULL_CONSTANT_LOAD_GEN7:
   case SHADER_OPCODE_UNTYPED_ATOMIC:
   case SHADER_OPCODE_UNTYPED_SURFACE_READ:
   case SHADER_OPCODE_UNTYPED_SURFACE_WRITE:
   case SHADER_OPCODE_TYPED_ATOMIC:
   case SHADER_OPCODE_TYPED_SURFACE_READ:
   case SHADER_OPCODE_TYPED_SURFACE_WRITE:
   case VEC4_OPCODE_URB_READ:
   case TCS_OPCODE_URB_WRITE:
   case TCS_OPCODE_RELEASE_INPUT:
   case SHADER_OPCODE_BARRIER:
      return true;
   default:
      return false;
   }
}

/**
 * Returns true if this instruction's sources and destinations cannot
 * safely be the same register.
 *
 * In most cases, a register can be written over safely by the same
 * instruction that is its last use.  For a single instruction, the
 * sources are dereferenced before writing of the destination starts
 * (naturally).
 *
 * However, there are a few cases where this can be problematic:
 *
 * - Virtual opcodes that translate to multiple instructions in the
 *   code generator: if src == dst and one instruction writes the
 *   destination before a later instruction reads the source, then
 *   src will have been clobbered.
 *
 * The register allocator uses this information to set up conflicts between
 * GRF sources and the destination.
 */
bool
vec4_instruction::has_source_and_destination_hazard() const
{
   switch (opcode) {
   case TCS_OPCODE_SET_INPUT_URB_OFFSETS:
   case TCS_OPCODE_SET_OUTPUT_URB_OFFSETS:
   case TES_OPCODE_ADD_INDIRECT_URB_OFFSET:
      return true;
   default:
      return false;
   }
}

unsigned
vec4_instruction::regs_read(unsigned arg) const
{
   if (src[arg].file == BAD_FILE)
      return 0;

   switch (opcode) {
   case SHADER_OPCODE_SHADER_TIME_ADD:
   case SHADER_OPCODE_UNTYPED_ATOMIC:
   case SHADER_OPCODE_UNTYPED_SURFACE_READ:
   case SHADER_OPCODE_UNTYPED_SURFACE_WRITE:
   case SHADER_OPCODE_TYPED_ATOMIC:
   case SHADER_OPCODE_TYPED_SURFACE_READ:
   case SHADER_OPCODE_TYPED_SURFACE_WRITE:
   case TCS_OPCODE_URB_WRITE:
      return arg == 0 ? mlen : 1;

   case VS_OPCODE_PULL_CONSTANT_LOAD_GEN7:
      return arg == 1 ? mlen : 1;

   default:
      return 1;
   }
}

bool
vec4_instruction::can_do_source_mods(const struct brw_device_info *devinfo)
{
   if (devinfo->gen == 6 && is_math())
      return false;

   if (is_send_from_grf())
      return false;

   if (!backend_instruction::can_do_source_mods())
      return false;

   return true;
}

bool
vec4_instruction::can_do_writemask(const struct brw_device_info *devinfo)
{
   switch (opcode) {
   case SHADER_OPCODE_GEN4_SCRATCH_READ:
   case VS_OPCODE_PULL_CONSTANT_LOAD:
   case VS_OPCODE_PULL_CONSTANT_LOAD_GEN7:
   case VS_OPCODE_SET_SIMD4X2_HEADER_GEN9:
   case TCS_OPCODE_SET_INPUT_URB_OFFSETS:
   case TCS_OPCODE_SET_OUTPUT_URB_OFFSETS:
   case TES_OPCODE_CREATE_INPUT_READ_HEADER:
   case TES_OPCODE_ADD_INDIRECT_URB_OFFSET:
   case VEC4_OPCODE_URB_READ:
   case SHADER_OPCODE_MOV_INDIRECT:
      return false;
   default:
      /* The MATH instruction on Gen6 only executes in align1 mode, which does
       * not support writemasking.
       */
      if (devinfo->gen == 6 && is_math())
         return false;

      if (is_tex())
         return false;

      return true;
   }
}

bool
vec4_instruction::can_change_types() const
{
   return dst.type == src[0].type &&
          !src[0].abs && !src[0].negate && !saturate &&
          (opcode == BRW_OPCODE_MOV ||
           (opcode == BRW_OPCODE_SEL &&
            dst.type == src[1].type &&
            predicate != BRW_PREDICATE_NONE &&
            !src[1].abs && !src[1].negate));
}

/**
 * Returns how many MRFs an opcode will write over.
 *
 * Note that this is not the 0 or 1 implied writes in an actual gen
 * instruction -- the generate_* functions generate additional MOVs
 * for setup.
 */
int
vec4_visitor::implied_mrf_writes(vec4_instruction *inst)
{
   if (inst->mlen == 0 || inst->is_send_from_grf())
      return 0;

   switch (inst->opcode) {
   case SHADER_OPCODE_RCP:
   case SHADER_OPCODE_RSQ:
   case SHADER_OPCODE_SQRT:
   case SHADER_OPCODE_EXP2:
   case SHADER_OPCODE_LOG2:
   case SHADER_OPCODE_SIN:
   case SHADER_OPCODE_COS:
      return 1;
   case SHADER_OPCODE_INT_QUOTIENT:
   case SHADER_OPCODE_INT_REMAINDER:
   case SHADER_OPCODE_POW:
   case TCS_OPCODE_THREAD_END:
      return 2;
   case VS_OPCODE_URB_WRITE:
      return 1;
   case VS_OPCODE_PULL_CONSTANT_LOAD:
      return 2;
   case SHADER_OPCODE_GEN4_SCRATCH_READ:
      return 2;
   case SHADER_OPCODE_GEN4_SCRATCH_WRITE:
      return 3;
   case GS_OPCODE_URB_WRITE:
   case GS_OPCODE_URB_WRITE_ALLOCATE:
   case GS_OPCODE_THREAD_END:
      return 0;
   case GS_OPCODE_FF_SYNC:
      return 1;
   case TCS_OPCODE_URB_WRITE:
      return 0;
   case SHADER_OPCODE_SHADER_TIME_ADD:
      return 0;
   case SHADER_OPCODE_TEX:
   case SHADER_OPCODE_TXL:
   case SHADER_OPCODE_TXD:
   case SHADER_OPCODE_TXF:
   case SHADER_OPCODE_TXF_CMS:
   case SHADER_OPCODE_TXF_CMS_W:
   case SHADER_OPCODE_TXF_MCS:
   case SHADER_OPCODE_TXS:
   case SHADER_OPCODE_TG4:
   case SHADER_OPCODE_TG4_OFFSET:
   case SHADER_OPCODE_SAMPLEINFO:
   case VS_OPCODE_GET_BUFFER_SIZE:
      return inst->header_size;
   default:
      unreachable("not reached");
   }
}

bool
src_reg::equals(const src_reg &r) const
{
   return (this->backend_reg::equals(r) &&
	   !reladdr && !r.reladdr);
}

bool
vec4_visitor::opt_vector_float()
{
   bool progress = false;

   int last_reg = -1, last_reg_offset = -1;
   enum brw_reg_file last_reg_file = BAD_FILE;

   uint8_t imm[4] = { 0 };
   int inst_count = 0;
   vec4_instruction *imm_inst[4];
   unsigned writemask = 0;
   enum brw_reg_type dest_type = BRW_REGISTER_TYPE_F;

   foreach_block_and_inst_safe(block, vec4_instruction, inst, cfg) {
      int vf = -1;
      enum brw_reg_type need_type;

      /* Look for unconditional MOVs from an immediate with a partial
       * writemask.  Skip type-conversion MOVs other than integer 0,
       * where the type doesn't matter.  See if the immediate can be
       * represented as a VF.
       */
      if (inst->opcode == BRW_OPCODE_MOV &&
          inst->src[0].file == IMM &&
          inst->predicate == BRW_PREDICATE_NONE &&
          inst->dst.writemask != WRITEMASK_XYZW &&
          (inst->src[0].type == inst->dst.type || inst->src[0].d == 0)) {

         vf = brw_float_to_vf(inst->src[0].d);
         need_type = BRW_REGISTER_TYPE_D;

         if (vf == -1) {
            vf = brw_float_to_vf(inst->src[0].f);
            need_type = BRW_REGISTER_TYPE_F;
         }
      } else {
         last_reg = -1;
      }

      /* If this wasn't a MOV, or the destination register doesn't match,
       * or we have to switch destination types, then this breaks our
       * sequence.  Combine anything we've accumulated so far.
       */
      if (last_reg != inst->dst.nr ||
          last_reg_offset != inst->dst.reg_offset ||
          last_reg_file != inst->dst.file ||
          (vf > 0 && dest_type != need_type)) {

         if (inst_count > 1) {
            unsigned vf;
            memcpy(&vf, imm, sizeof(vf));
            vec4_instruction *mov = MOV(imm_inst[0]->dst, brw_imm_vf(vf));
            mov->dst.type = dest_type;
            mov->dst.writemask = writemask;
            inst->insert_before(block, mov);

            for (int i = 0; i < inst_count; i++) {
               imm_inst[i]->remove(block);
            }

            progress = true;
         }

         inst_count = 0;
         last_reg = -1;
         writemask = 0;
         dest_type = BRW_REGISTER_TYPE_F;

         for (int i = 0; i < 4; i++) {
            imm[i] = 0;
         }
      }

      /* Record this instruction's value (if it was representable). */
      if (vf != -1) {
         if ((inst->dst.writemask & WRITEMASK_X) != 0)
            imm[0] = vf;
         if ((inst->dst.writemask & WRITEMASK_Y) != 0)
            imm[1] = vf;
         if ((inst->dst.writemask & WRITEMASK_Z) != 0)
            imm[2] = vf;
         if ((inst->dst.writemask & WRITEMASK_W) != 0)
            imm[3] = vf;

         writemask |= inst->dst.writemask;
         imm_inst[inst_count++] = inst;

         last_reg = inst->dst.nr;
         last_reg_offset = inst->dst.reg_offset;
         last_reg_file = inst->dst.file;
         if (vf > 0)
            dest_type = need_type;
      }
   }

   if (progress)
      invalidate_live_intervals();

   return progress;
}

/* Replaces unused channels of a swizzle with channels that are used.
 *
 * For instance, this pass transforms
 *
 *    mov vgrf4.yz, vgrf5.wxzy
 *
 * into
 *
 *    mov vgrf4.yz, vgrf5.xxzx
 *
 * This eliminates false uses of some channels, letting dead code elimination
 * remove the instructions that wrote them.
 */
bool
vec4_visitor::opt_reduce_swizzle()
{
   bool progress = false;

   foreach_block_and_inst_safe(block, vec4_instruction, inst, cfg) {
      if (inst->dst.file == BAD_FILE ||
          inst->dst.file == ARF ||
          inst->dst.file == FIXED_GRF ||
          inst->is_send_from_grf())
         continue;

      unsigned swizzle;

      /* Determine which channels of the sources are read. */
      switch (inst->opcode) {
      case VEC4_OPCODE_PACK_BYTES:
      case BRW_OPCODE_DP4:
      case BRW_OPCODE_DPH: /* FINISHME: DPH reads only three channels of src0,
                            *           but all four of src1.
                            */
         swizzle = brw_swizzle_for_size(4);
         break;
      case BRW_OPCODE_DP3:
         swizzle = brw_swizzle_for_size(3);
         break;
      case BRW_OPCODE_DP2:
         swizzle = brw_swizzle_for_size(2);
         break;
      default:
         swizzle = brw_swizzle_for_mask(inst->dst.writemask);
         break;
      }

      /* Update sources' swizzles. */
      for (int i = 0; i < 3; i++) {
         if (inst->src[i].file != VGRF &&
             inst->src[i].file != ATTR &&
             inst->src[i].file != UNIFORM)
            continue;

         const unsigned new_swizzle =
            brw_compose_swizzle(swizzle, inst->src[i].swizzle);
         if (inst->src[i].swizzle != new_swizzle) {
            inst->src[i].swizzle = new_swizzle;
            progress = true;
         }
      }
   }

   if (progress)
      invalidate_live_intervals();

   return progress;
}

void
vec4_visitor::split_uniform_registers()
{
   /* Prior to this, uniforms have been in an array sized according to
    * the number of vector uniforms present, sparsely filled (so an
    * aggregate results in reg indices being skipped over).  Now we're
    * going to cut those aggregates up so each .nr index is one
    * vector.  The goal is to make elimination of unused uniform
    * components easier later.
    */
   foreach_block_and_inst(block, vec4_instruction, inst, cfg) {
      for (int i = 0 ; i < 3; i++) {
	 if (inst->src[i].file != UNIFORM)
	    continue;

	 assert(!inst->src[i].reladdr);

         inst->src[i].nr += inst->src[i].reg_offset;
	 inst->src[i].reg_offset = 0;
      }
   }
}

void
vec4_visitor::pack_uniform_registers()
{
   uint8_t chans_used[this->uniforms];
   int new_loc[this->uniforms];
   int new_chan[this->uniforms];

   memset(chans_used, 0, sizeof(chans_used));
   memset(new_loc, 0, sizeof(new_loc));
   memset(new_chan, 0, sizeof(new_chan));

   /* Find which uniform vectors are actually used by the program.  We
    * expect unused vector elements when we've moved array access out
    * to pull constants, and from some GLSL code generators like wine.
    */
   foreach_block_and_inst(block, vec4_instruction, inst, cfg) {
      unsigned readmask;
      switch (inst->opcode) {
      case VEC4_OPCODE_PACK_BYTES:
      case BRW_OPCODE_DP4:
      case BRW_OPCODE_DPH:
         readmask = 0xf;
         break;
      case BRW_OPCODE_DP3:
         readmask = 0x7;
         break;
      case BRW_OPCODE_DP2:
         readmask = 0x3;
         break;
      default:
         readmask = inst->dst.writemask;
         break;
      }

      for (int i = 0 ; i < 3; i++) {
         if (inst->src[i].file != UNIFORM)
            continue;

         int reg = inst->src[i].nr;
         for (int c = 0; c < 4; c++) {
            if (!(readmask & (1 << c)))
               continue;

            chans_used[reg] = MAX2(chans_used[reg],
                                   BRW_GET_SWZ(inst->src[i].swizzle, c) + 1);
         }
      }

      if (inst->opcode == SHADER_OPCODE_MOV_INDIRECT &&
          inst->src[0].file == UNIFORM) {
         assert(inst->src[2].file == BRW_IMMEDIATE_VALUE);
         assert(inst->src[0].subnr == 0);

         unsigned bytes_read = inst->src[2].ud;
         assert(bytes_read % 4 == 0);
         unsigned vec4s_read = DIV_ROUND_UP(bytes_read, 16);

         /* We just mark every register touched by a MOV_INDIRECT as being
          * fully used.  This ensures that it doesn't broken up piecewise by
          * the next part of our packing algorithm.
          */
         int reg = inst->src[0].nr;
         for (unsigned i = 0; i < vec4s_read; i++)
            chans_used[reg + i] = 4;
      }
   }

   int new_uniform_count = 0;

   /* Now, figure out a packing of the live uniform vectors into our
    * push constants.
    */
   for (int src = 0; src < uniforms; src++) {
      int size = chans_used[src];

      if (size == 0)
         continue;

      int dst;
      /* Find the lowest place we can slot this uniform in. */
      for (dst = 0; dst < src; dst++) {
	 if (chans_used[dst] + size <= 4)
	    break;
      }

      if (src == dst) {
	 new_loc[src] = dst;
	 new_chan[src] = 0;
      } else {
	 new_loc[src] = dst;
	 new_chan[src] = chans_used[dst];

	 /* Move the references to the data */
	 for (int j = 0; j < size; j++) {
	    stage_prog_data->param[dst * 4 + new_chan[src] + j] =
	       stage_prog_data->param[src * 4 + j];
	 }

	 chans_used[dst] += size;
	 chans_used[src] = 0;
      }

      new_uniform_count = MAX2(new_uniform_count, dst + 1);
   }

   this->uniforms = new_uniform_count;

   /* Now, update the instructions for our repacked uniforms. */
   foreach_block_and_inst(block, vec4_instruction, inst, cfg) {
      for (int i = 0 ; i < 3; i++) {
         int src = inst->src[i].nr;

	 if (inst->src[i].file != UNIFORM)
	    continue;

         inst->src[i].nr = new_loc[src];
         inst->src[i].swizzle += BRW_SWIZZLE4(new_chan[src], new_chan[src],
                                              new_chan[src], new_chan[src]);
      }
   }
}

/**
 * Does algebraic optimizations (0 * a = 0, 1 * a = a, a + 0 = a).
 *
 * While GLSL IR also performs this optimization, we end up with it in
 * our instruction stream for a couple of reasons.  One is that we
 * sometimes generate silly instructions, for example in array access
 * where we'll generate "ADD offset, index, base" even if base is 0.
 * The other is that GLSL IR's constant propagation doesn't track the
 * components of aggregates, so some VS patterns (initialize matrix to
 * 0, accumulate in vertex blending factors) end up breaking down to
 * instructions involving 0.
 */
bool
vec4_visitor::opt_algebraic()
{
   bool progress = false;

   foreach_block_and_inst(block, vec4_instruction, inst, cfg) {
      switch (inst->opcode) {
      case BRW_OPCODE_MOV:
         if (inst->src[0].file != IMM)
            break;

         if (inst->saturate) {
            if (inst->dst.type != inst->src[0].type)
               assert(!"unimplemented: saturate mixed types");

            if (brw_saturate_immediate(inst->dst.type,
                                       &inst->src[0].as_brw_reg())) {
               inst->saturate = false;
               progress = true;
            }
         }
         break;

      case VEC4_OPCODE_UNPACK_UNIFORM:
         if (inst->src[0].file != UNIFORM) {
            inst->opcode = BRW_OPCODE_MOV;
            progress = true;
         }
         break;

      case BRW_OPCODE_ADD:
	 if (inst->src[1].is_zero()) {
	    inst->opcode = BRW_OPCODE_MOV;
	    inst->src[1] = src_reg();
	    progress = true;
	 }
	 break;

      case BRW_OPCODE_MUL:
	 if (inst->src[1].is_zero()) {
	    inst->opcode = BRW_OPCODE_MOV;
	    switch (inst->src[0].type) {
	    case BRW_REGISTER_TYPE_F:
	       inst->src[0] = brw_imm_f(0.0f);
	       break;
	    case BRW_REGISTER_TYPE_D:
	       inst->src[0] = brw_imm_d(0);
	       break;
	    case BRW_REGISTER_TYPE_UD:
	       inst->src[0] = brw_imm_ud(0u);
	       break;
	    default:
	       unreachable("not reached");
	    }
	    inst->src[1] = src_reg();
	    progress = true;
	 } else if (inst->src[1].is_one()) {
	    inst->opcode = BRW_OPCODE_MOV;
	    inst->src[1] = src_reg();
	    progress = true;
         } else if (inst->src[1].is_negative_one()) {
            inst->opcode = BRW_OPCODE_MOV;
            inst->src[0].negate = !inst->src[0].negate;
            inst->src[1] = src_reg();
            progress = true;
	 }
	 break;
      case BRW_OPCODE_CMP:
         if (inst->conditional_mod == BRW_CONDITIONAL_GE &&
             inst->src[0].abs &&
             inst->src[0].negate &&
             inst->src[1].is_zero()) {
            inst->src[0].abs = false;
            inst->src[0].negate = false;
            inst->conditional_mod = BRW_CONDITIONAL_Z;
            progress = true;
            break;
         }
         break;
      case SHADER_OPCODE_BROADCAST:
         if (is_uniform(inst->src[0]) ||
             inst->src[1].is_zero()) {
            inst->opcode = BRW_OPCODE_MOV;
            inst->src[1] = src_reg();
            inst->force_writemask_all = true;
            progress = true;
         }
         break;

      default:
	 break;
      }
   }

   if (progress)
      invalidate_live_intervals();

   return progress;
}

/**
 * Only a limited number of hardware registers may be used for push
 * constants, so this turns access to the overflowed constants into
 * pull constants.
 */
void
vec4_visitor::move_push_constants_to_pull_constants()
{
   int pull_constant_loc[this->uniforms];

   /* Only allow 32 registers (256 uniform components) as push constants,
    * which is the limit on gen6.
    *
    * If changing this value, note the limitation about total_regs in
    * brw_curbe.c.
    */
   int max_uniform_components = 32 * 8;
   if (this->uniforms * 4 <= max_uniform_components)
      return;

   /* Make some sort of choice as to which uniforms get sent to pull
    * constants.  We could potentially do something clever here like
    * look for the most infrequently used uniform vec4s, but leave
    * that for later.
    */
   for (int i = 0; i < this->uniforms * 4; i += 4) {
      pull_constant_loc[i / 4] = -1;

      if (i >= max_uniform_components) {
	 const gl_constant_value **values = &stage_prog_data->param[i];

	 /* Try to find an existing copy of this uniform in the pull
	  * constants if it was part of an array access already.
	  */
	 for (unsigned int j = 0; j < stage_prog_data->nr_pull_params; j += 4) {
	    int matches;

	    for (matches = 0; matches < 4; matches++) {
	       if (stage_prog_data->pull_param[j + matches] != values[matches])
		  break;
	    }

	    if (matches == 4) {
	       pull_constant_loc[i / 4] = j / 4;
	       break;
	    }
	 }

	 if (pull_constant_loc[i / 4] == -1) {
	    assert(stage_prog_data->nr_pull_params % 4 == 0);
	    pull_constant_loc[i / 4] = stage_prog_data->nr_pull_params / 4;

	    for (int j = 0; j < 4; j++) {
	       stage_prog_data->pull_param[stage_prog_data->nr_pull_params++] =
                  values[j];
	    }
	 }
      }
   }

   /* Now actually rewrite usage of the things we've moved to pull
    * constants.
    */
   foreach_block_and_inst_safe(block, vec4_instruction, inst, cfg) {
      for (int i = 0 ; i < 3; i++) {
	 if (inst->src[i].file != UNIFORM ||
             pull_constant_loc[inst->src[i].nr] == -1)
	    continue;

         int uniform = inst->src[i].nr;

	 dst_reg temp = dst_reg(this, glsl_type::vec4_type);

	 emit_pull_constant_load(block, inst, temp, inst->src[i],
				 pull_constant_loc[uniform], src_reg());

	 inst->src[i].file = temp.file;
         inst->src[i].nr = temp.nr;
	 inst->src[i].reg_offset = temp.reg_offset;
	 inst->src[i].reladdr = NULL;
      }
   }

   /* Repack push constants to remove the now-unused ones. */
   pack_uniform_registers();
}

/* Conditions for which we want to avoid setting the dependency control bits */
bool
vec4_visitor::is_dep_ctrl_unsafe(const vec4_instruction *inst)
{
#define IS_DWORD(reg) \
   (reg.type == BRW_REGISTER_TYPE_UD || \
    reg.type == BRW_REGISTER_TYPE_D)

   /* "When source or destination datatype is 64b or operation is integer DWord
    * multiply, DepCtrl must not be used."
    * May apply to future SoCs as well.
    */
   if (devinfo->is_cherryview) {
      if (inst->opcode == BRW_OPCODE_MUL &&
         IS_DWORD(inst->src[0]) &&
         IS_DWORD(inst->src[1]))
         return true;
   }
#undef IS_DWORD

   if (devinfo->gen >= 8) {
      if (inst->opcode == BRW_OPCODE_F32TO16)
         return true;
   }

   /*
    * mlen:
    * In the presence of send messages, totally interrupt dependency
    * control. They're long enough that the chance of dependency
    * control around them just doesn't matter.
    *
    * predicate:
    * From the Ivy Bridge PRM, volume 4 part 3.7, page 80:
    * When a sequence of NoDDChk and NoDDClr are used, the last instruction that
    * completes the scoreboard clear must have a non-zero execution mask. This
    * means, if any kind of predication can change the execution mask or channel
    * enable of the last instruction, the optimization must be avoided. This is
    * to avoid instructions being shot down the pipeline when no writes are
    * required.
    *
    * math:
    * Dependency control does not work well over math instructions.
    * NB: Discovered empirically
    */
   return (inst->mlen || inst->predicate || inst->is_math());
}

/**
 * Sets the dependency control fields on instructions after register
 * allocation and before the generator is run.
 *
 * When you have a sequence of instructions like:
 *
 * DP4 temp.x vertex uniform[0]
 * DP4 temp.y vertex uniform[0]
 * DP4 temp.z vertex uniform[0]
 * DP4 temp.w vertex uniform[0]
 *
 * The hardware doesn't know that it can actually run the later instructions
 * while the previous ones are in flight, producing stalls.  However, we have
 * manual fields we can set in the instructions that let it do so.
 */
void
vec4_visitor::opt_set_dependency_control()
{
   vec4_instruction *last_grf_write[BRW_MAX_GRF];
   uint8_t grf_channels_written[BRW_MAX_GRF];
   vec4_instruction *last_mrf_write[BRW_MAX_GRF];
   uint8_t mrf_channels_written[BRW_MAX_GRF];

   assert(prog_data->total_grf ||
          !"Must be called after register allocation");

   foreach_block (block, cfg) {
      memset(last_grf_write, 0, sizeof(last_grf_write));
      memset(last_mrf_write, 0, sizeof(last_mrf_write));

      foreach_inst_in_block (vec4_instruction, inst, block) {
         /* If we read from a register that we were doing dependency control
          * on, don't do dependency control across the read.
          */
         for (int i = 0; i < 3; i++) {
            int reg = inst->src[i].nr + inst->src[i].reg_offset;
            if (inst->src[i].file == VGRF) {
               last_grf_write[reg] = NULL;
            } else if (inst->src[i].file == FIXED_GRF) {
               memset(last_grf_write, 0, sizeof(last_grf_write));
               break;
            }
            assert(inst->src[i].file != MRF);
         }

         if (is_dep_ctrl_unsafe(inst)) {
            memset(last_grf_write, 0, sizeof(last_grf_write));
            memset(last_mrf_write, 0, sizeof(last_mrf_write));
            continue;
         }

         /* Now, see if we can do dependency control for this instruction
          * against a previous one writing to its destination.
          */
         int reg = inst->dst.nr + inst->dst.reg_offset;
         if (inst->dst.file == VGRF || inst->dst.file == FIXED_GRF) {
            if (last_grf_write[reg] &&
                !(inst->dst.writemask & grf_channels_written[reg])) {
               last_grf_write[reg]->no_dd_clear = true;
               inst->no_dd_check = true;
            } else {
               grf_channels_written[reg] = 0;
            }

            last_grf_write[reg] = inst;
            grf_channels_written[reg] |= inst->dst.writemask;
         } else if (inst->dst.file == MRF) {
            if (last_mrf_write[reg] &&
                !(inst->dst.writemask & mrf_channels_written[reg])) {
               last_mrf_write[reg]->no_dd_clear = true;
               inst->no_dd_check = true;
            } else {
               mrf_channels_written[reg] = 0;
            }

            last_mrf_write[reg] = inst;
            mrf_channels_written[reg] |= inst->dst.writemask;
         }
      }
   }
}

bool
vec4_instruction::can_reswizzle(const struct brw_device_info *devinfo,
                                int dst_writemask,
                                int swizzle,
                                int swizzle_mask)
{
   /* Gen6 MATH instructions can not execute in align16 mode, so swizzles
    * are not allowed.
    */
   if (devinfo->gen == 6 && is_math() && swizzle != BRW_SWIZZLE_XYZW)
      return false;

   if (!can_do_writemask(devinfo) && dst_writemask != WRITEMASK_XYZW)
      return false;

   /* If this instruction sets anything not referenced by swizzle, then we'd
    * totally break it when we reswizzle.
    */
   if (dst.writemask & ~swizzle_mask)
      return false;

   if (mlen > 0)
      return false;

   for (int i = 0; i < 3; i++) {
      if (src[i].is_accumulator())
         return false;
   }

   return true;
}

/**
 * For any channels in the swizzle's source that were populated by this
 * instruction, rewrite the instruction to put the appropriate result directly
 * in those channels.
 *
 * e.g. for swizzle=yywx, MUL a.xy b c -> MUL a.yy_x b.yy z.yy_x
 */
void
vec4_instruction::reswizzle(int dst_writemask, int swizzle)
{
   /* Destination write mask doesn't correspond to source swizzle for the dot
    * product and pack_bytes instructions.
    */
   if (opcode != BRW_OPCODE_DP4 && opcode != BRW_OPCODE_DPH &&
       opcode != BRW_OPCODE_DP3 && opcode != BRW_OPCODE_DP2 &&
       opcode != VEC4_OPCODE_PACK_BYTES) {
      for (int i = 0; i < 3; i++) {
         if (src[i].file == BAD_FILE || src[i].file == IMM)
            continue;

         src[i].swizzle = brw_compose_swizzle(swizzle, src[i].swizzle);
      }
   }

   /* Apply the specified swizzle and writemask to the original mask of
    * written components.
    */
   dst.writemask = dst_writemask &
                   brw_apply_swizzle_to_mask(swizzle, dst.writemask);
}

/*
 * Tries to reduce extra MOV instructions by taking temporary GRFs that get
 * just written and then MOVed into another reg and making the original write
 * of the GRF write directly to the final destination instead.
 */
bool
vec4_visitor::opt_register_coalesce()
{
   bool progress = false;
   int next_ip = 0;

   calculate_live_intervals();

   foreach_block_and_inst_safe (block, vec4_instruction, inst, cfg) {
      int ip = next_ip;
      next_ip++;

      if (inst->opcode != BRW_OPCODE_MOV ||
          (inst->dst.file != VGRF && inst->dst.file != MRF) ||
	  inst->predicate ||
	  inst->src[0].file != VGRF ||
	  inst->dst.type != inst->src[0].type ||
	  inst->src[0].abs || inst->src[0].negate || inst->src[0].reladdr)
	 continue;

      /* Remove no-op MOVs */
      if (inst->dst.file == inst->src[0].file &&
          inst->dst.nr == inst->src[0].nr &&
          inst->dst.reg_offset == inst->src[0].reg_offset) {
         bool is_nop_mov = true;

         for (unsigned c = 0; c < 4; c++) {
            if ((inst->dst.writemask & (1 << c)) == 0)
               continue;

            if (BRW_GET_SWZ(inst->src[0].swizzle, c) != c) {
               is_nop_mov = false;
               break;
            }
         }

         if (is_nop_mov) {
            inst->remove(block);
            progress = true;
            continue;
         }
      }

      bool to_mrf = (inst->dst.file == MRF);

      /* Can't coalesce this GRF if someone else was going to
       * read it later.
       */
      if (var_range_end(var_from_reg(alloc, inst->src[0]), 4) > ip)
	 continue;

      /* We need to check interference with the final destination between this
       * instruction and the earliest instruction involved in writing the GRF
       * we're eliminating.  To do that, keep track of which of our source
       * channels we've seen initialized.
       */
      const unsigned chans_needed =
         brw_apply_inv_swizzle_to_mask(inst->src[0].swizzle,
                                       inst->dst.writemask);
      unsigned chans_remaining = chans_needed;

      /* Now walk up the instruction stream trying to see if we can rewrite
       * everything writing to the temporary to write into the destination
       * instead.
       */
      vec4_instruction *_scan_inst = (vec4_instruction *)inst->prev;
      foreach_inst_in_block_reverse_starting_from(vec4_instruction, scan_inst,
                                                  inst) {
         _scan_inst = scan_inst;

         if (inst->src[0].in_range(scan_inst->dst, scan_inst->regs_written)) {
            /* Found something writing to the reg we want to coalesce away. */
            if (to_mrf) {
               /* SEND instructions can't have MRF as a destination. */
               if (scan_inst->mlen)
                  break;

               if (devinfo->gen == 6) {
                  /* gen6 math instructions must have the destination be
                   * VGRF, so no compute-to-MRF for them.
                   */
                  if (scan_inst->is_math()) {
                     break;
                  }
               }
            }

            /* This doesn't handle saturation on the instruction we
             * want to coalesce away if the register types do not match.
             * But if scan_inst is a non type-converting 'mov', we can fix
             * the types later.
             */
            if (inst->saturate &&
                inst->dst.type != scan_inst->dst.type &&
                !(scan_inst->opcode == BRW_OPCODE_MOV &&
                  scan_inst->dst.type == scan_inst->src[0].type))
               break;

            /* If we can't handle the swizzle, bail. */
            if (!scan_inst->can_reswizzle(devinfo, inst->dst.writemask,
                                          inst->src[0].swizzle,
                                          chans_needed)) {
               break;
            }

            /* This doesn't handle coalescing of multiple registers. */
            if (scan_inst->regs_written > 1)
               break;

	    /* Mark which channels we found unconditional writes for. */
	    if (!scan_inst->predicate)
               chans_remaining &= ~scan_inst->dst.writemask;

	    if (chans_remaining == 0)
	       break;
	 }

         /* You can't read from an MRF, so if someone else reads our MRF's
          * source GRF that we wanted to rewrite, that stops us.  If it's a
          * GRF we're trying to coalesce to, we don't actually handle
          * rewriting sources so bail in that case as well.
          */
	 bool interfered = false;
	 for (int i = 0; i < 3; i++) {
            if (inst->src[0].in_range(scan_inst->src[i],
                                      scan_inst->regs_read(i)))
	       interfered = true;
	 }
	 if (interfered)
	    break;

         /* If somebody else writes the same channels of our destination here,
          * we can't coalesce before that.
          */
         if (inst->dst.in_range(scan_inst->dst, scan_inst->regs_written) &&
             (inst->dst.writemask & scan_inst->dst.writemask) != 0) {
            break;
         }

         /* Check for reads of the register we're trying to coalesce into.  We
          * can't go rewriting instructions above that to put some other value
          * in the register instead.
          */
         if (to_mrf && scan_inst->mlen > 0) {
            if (inst->dst.nr >= scan_inst->base_mrf &&
                inst->dst.nr < scan_inst->base_mrf + scan_inst->mlen) {
               break;
            }
         } else {
            for (int i = 0; i < 3; i++) {
               if (inst->dst.in_range(scan_inst->src[i],
                                      scan_inst->regs_read(i)))
                  interfered = true;
            }
            if (interfered)
               break;
         }
      }

      if (chans_remaining == 0) {
	 /* If we've made it here, we have an MOV we want to coalesce out, and
	  * a scan_inst pointing to the earliest instruction involved in
	  * computing the value.  Now go rewrite the instruction stream
	  * between the two.
	  */
         vec4_instruction *scan_inst = _scan_inst;
	 while (scan_inst != inst) {
	    if (scan_inst->dst.file == VGRF &&
                scan_inst->dst.nr == inst->src[0].nr &&
		scan_inst->dst.reg_offset == inst->src[0].reg_offset) {
               scan_inst->reswizzle(inst->dst.writemask,
                                    inst->src[0].swizzle);
	       scan_inst->dst.file = inst->dst.file;
               scan_inst->dst.nr = inst->dst.nr;
	       scan_inst->dst.reg_offset = inst->dst.reg_offset;
               if (inst->saturate &&
                   inst->dst.type != scan_inst->dst.type) {
                  /* If we have reached this point, scan_inst is a non
                   * type-converting 'mov' and we can modify its register types
                   * to match the ones in inst. Otherwise, we could have an
                   * incorrect saturation result.
                   */
                  scan_inst->dst.type = inst->dst.type;
                  scan_inst->src[0].type = inst->src[0].type;
               }
	       scan_inst->saturate |= inst->saturate;
	    }
	    scan_inst = (vec4_instruction *)scan_inst->next;
	 }
	 inst->remove(block);
	 progress = true;
      }
   }

   if (progress)
      invalidate_live_intervals();

   return progress;
}

/**
 * Eliminate FIND_LIVE_CHANNEL instructions occurring outside any control
 * flow.  We could probably do better here with some form of divergence
 * analysis.
 */
bool
vec4_visitor::eliminate_find_live_channel()
{
   bool progress = false;
   unsigned depth = 0;

   foreach_block_and_inst_safe(block, vec4_instruction, inst, cfg) {
      switch (inst->opcode) {
      case BRW_OPCODE_IF:
      case BRW_OPCODE_DO:
         depth++;
         break;

      case BRW_OPCODE_ENDIF:
      case BRW_OPCODE_WHILE:
         depth--;
         break;

      case SHADER_OPCODE_FIND_LIVE_CHANNEL:
         if (depth == 0) {
            inst->opcode = BRW_OPCODE_MOV;
            inst->src[0] = brw_imm_d(0);
            inst->force_writemask_all = true;
            progress = true;
         }
         break;

      default:
         break;
      }
   }

   return progress;
}

/**
 * Splits virtual GRFs requesting more than one contiguous physical register.
 *
 * We initially create large virtual GRFs for temporary structures, arrays,
 * and matrices, so that the dereference visitor functions can add reg_offsets
 * to work their way down to the actual member being accessed.  But when it
 * comes to optimization, we'd like to treat each register as individual
 * storage if possible.
 *
 * So far, the only thing that might prevent splitting is a send message from
 * a GRF on IVB.
 */
void
vec4_visitor::split_virtual_grfs()
{
   int num_vars = this->alloc.count;
   int new_virtual_grf[num_vars];
   bool split_grf[num_vars];

   memset(new_virtual_grf, 0, sizeof(new_virtual_grf));

   /* Try to split anything > 0 sized. */
   for (int i = 0; i < num_vars; i++) {
      split_grf[i] = this->alloc.sizes[i] != 1;
   }

   /* Check that the instructions are compatible with the registers we're trying
    * to split.
    */
   foreach_block_and_inst(block, vec4_instruction, inst, cfg) {
      if (inst->dst.file == VGRF && inst->regs_written > 1)
         split_grf[inst->dst.nr] = false;

      for (int i = 0; i < 3; i++) {
         if (inst->src[i].file == VGRF && inst->regs_read(i) > 1)
            split_grf[inst->src[i].nr] = false;
      }
   }

   /* Allocate new space for split regs.  Note that the virtual
    * numbers will be contiguous.
    */
   for (int i = 0; i < num_vars; i++) {
      if (!split_grf[i])
         continue;

      new_virtual_grf[i] = alloc.allocate(1);
      for (unsigned j = 2; j < this->alloc.sizes[i]; j++) {
         unsigned reg = alloc.allocate(1);
         assert(reg == new_virtual_grf[i] + j - 1);
         (void) reg;
      }
      this->alloc.sizes[i] = 1;
   }

   foreach_block_and_inst(block, vec4_instruction, inst, cfg) {
      if (inst->dst.file == VGRF && split_grf[inst->dst.nr] &&
          inst->dst.reg_offset != 0) {
         inst->dst.nr = (new_virtual_grf[inst->dst.nr] +
                          inst->dst.reg_offset - 1);
         inst->dst.reg_offset = 0;
      }
      for (int i = 0; i < 3; i++) {
         if (inst->src[i].file == VGRF && split_grf[inst->src[i].nr] &&
             inst->src[i].reg_offset != 0) {
            inst->src[i].nr = (new_virtual_grf[inst->src[i].nr] +
                                inst->src[i].reg_offset - 1);
            inst->src[i].reg_offset = 0;
         }
      }
   }
   invalidate_live_intervals();
}

void
vec4_visitor::dump_instruction(backend_instruction *be_inst)
{
   dump_instruction(be_inst, stderr);
}

void
vec4_visitor::dump_instruction(backend_instruction *be_inst, FILE *file)
{
   vec4_instruction *inst = (vec4_instruction *)be_inst;

   if (inst->predicate) {
      fprintf(file, "(%cf0.%d%s) ",
              inst->predicate_inverse ? '-' : '+',
              inst->flag_subreg,
              pred_ctrl_align16[inst->predicate]);
   }

   fprintf(file, "%s", brw_instruction_name(devinfo, inst->opcode));
   if (inst->saturate)
      fprintf(file, ".sat");
   if (inst->conditional_mod) {
      fprintf(file, "%s", conditional_modifier[inst->conditional_mod]);
      if (!inst->predicate &&
          (devinfo->gen < 5 || (inst->opcode != BRW_OPCODE_SEL &&
                                inst->opcode != BRW_OPCODE_IF &&
                                inst->opcode != BRW_OPCODE_WHILE))) {
         fprintf(file, ".f0.%d", inst->flag_subreg);
      }
   }
   fprintf(file, " ");

   switch (inst->dst.file) {
   case VGRF:
      fprintf(file, "vgrf%d.%d", inst->dst.nr, inst->dst.reg_offset);
      break;
   case FIXED_GRF:
      fprintf(file, "g%d", inst->dst.nr);
      break;
   case MRF:
      fprintf(file, "m%d", inst->dst.nr);
      break;
   case ARF:
      switch (inst->dst.nr) {
      case BRW_ARF_NULL:
         fprintf(file, "null");
         break;
      case BRW_ARF_ADDRESS:
         fprintf(file, "a0.%d", inst->dst.subnr);
         break;
      case BRW_ARF_ACCUMULATOR:
         fprintf(file, "acc%d", inst->dst.subnr);
         break;
      case BRW_ARF_FLAG:
         fprintf(file, "f%d.%d", inst->dst.nr & 0xf, inst->dst.subnr);
         break;
      default:
         fprintf(file, "arf%d.%d", inst->dst.nr & 0xf, inst->dst.subnr);
         break;
      }
      if (inst->dst.subnr)
         fprintf(file, "+%d", inst->dst.subnr);
      break;
   case BAD_FILE:
      fprintf(file, "(null)");
      break;
   case IMM:
   case ATTR:
   case UNIFORM:
      unreachable("not reached");
   }
   if (inst->dst.writemask != WRITEMASK_XYZW) {
      fprintf(file, ".");
      if (inst->dst.writemask & 1)
         fprintf(file, "x");
      if (inst->dst.writemask & 2)
         fprintf(file, "y");
      if (inst->dst.writemask & 4)
         fprintf(file, "z");
      if (inst->dst.writemask & 8)
         fprintf(file, "w");
   }
   fprintf(file, ":%s", brw_reg_type_letters(inst->dst.type));

   if (inst->src[0].file != BAD_FILE)
      fprintf(file, ", ");

   for (int i = 0; i < 3 && inst->src[i].file != BAD_FILE; i++) {
      if (inst->src[i].negate)
         fprintf(file, "-");
      if (inst->src[i].abs)
         fprintf(file, "|");
      switch (inst->src[i].file) {
      case VGRF:
         fprintf(file, "vgrf%d", inst->src[i].nr);
         break;
      case FIXED_GRF:
         fprintf(file, "g%d", inst->src[i].nr);
         break;
      case ATTR:
         fprintf(file, "attr%d", inst->src[i].nr);
         break;
      case UNIFORM:
         fprintf(file, "u%d", inst->src[i].nr);
         break;
      case IMM:
         switch (inst->src[i].type) {
         case BRW_REGISTER_TYPE_F:
            fprintf(file, "%fF", inst->src[i].f);
            break;
         case BRW_REGISTER_TYPE_D:
            fprintf(file, "%dD", inst->src[i].d);
            break;
         case BRW_REGISTER_TYPE_UD:
            fprintf(file, "%uU", inst->src[i].ud);
            break;
         case BRW_REGISTER_TYPE_VF:
            fprintf(file, "[%-gF, %-gF, %-gF, %-gF]",
                    brw_vf_to_float((inst->src[i].ud >>  0) & 0xff),
                    brw_vf_to_float((inst->src[i].ud >>  8) & 0xff),
                    brw_vf_to_float((inst->src[i].ud >> 16) & 0xff),
                    brw_vf_to_float((inst->src[i].ud >> 24) & 0xff));
            break;
         default:
            fprintf(file, "???");
            break;
         }
         break;
      case ARF:
         switch (inst->src[i].nr) {
         case BRW_ARF_NULL:
            fprintf(file, "null");
            break;
         case BRW_ARF_ADDRESS:
            fprintf(file, "a0.%d", inst->src[i].subnr);
            break;
         case BRW_ARF_ACCUMULATOR:
            fprintf(file, "acc%d", inst->src[i].subnr);
            break;
         case BRW_ARF_FLAG:
            fprintf(file, "f%d.%d", inst->src[i].nr & 0xf, inst->src[i].subnr);
            break;
         default:
            fprintf(file, "arf%d.%d", inst->src[i].nr & 0xf, inst->src[i].subnr);
            break;
         }
         if (inst->src[i].subnr)
            fprintf(file, "+%d", inst->src[i].subnr);
         break;
      case BAD_FILE:
         fprintf(file, "(null)");
         break;
      case MRF:
         unreachable("not reached");
      }

      /* Don't print .0; and only VGRFs have reg_offsets and sizes */
      if (inst->src[i].reg_offset != 0 &&
          inst->src[i].file == VGRF &&
          alloc.sizes[inst->src[i].nr] != 1)
         fprintf(file, ".%d", inst->src[i].reg_offset);

      if (inst->src[i].file != IMM) {
         static const char *chans[4] = {"x", "y", "z", "w"};
         fprintf(file, ".");
         for (int c = 0; c < 4; c++) {
            fprintf(file, "%s", chans[BRW_GET_SWZ(inst->src[i].swizzle, c)]);
         }
      }

      if (inst->src[i].abs)
         fprintf(file, "|");

      if (inst->src[i].file != IMM) {
         fprintf(file, ":%s", brw_reg_type_letters(inst->src[i].type));
      }

      if (i < 2 && inst->src[i + 1].file != BAD_FILE)
         fprintf(file, ", ");
   }

   if (inst->force_writemask_all)
      fprintf(file, " NoMask");

   fprintf(file, "\n");
}


static inline struct brw_reg
attribute_to_hw_reg(int attr, bool interleaved)
{
   if (interleaved)
      return stride(brw_vec4_grf(attr / 2, (attr % 2) * 4), 0, 4, 1);
   else
      return brw_vec8_grf(attr, 0);
}


/**
 * Replace each register of type ATTR in this->instructions with a reference
 * to a fixed HW register.
 *
 * If interleaved is true, then each attribute takes up half a register, with
 * register N containing attribute 2*N in its first half and attribute 2*N+1
 * in its second half (this corresponds to the payload setup used by geometry
 * shaders in "single" or "dual instanced" dispatch mode).  If interleaved is
 * false, then each attribute takes up a whole register, with register N
 * containing attribute N (this corresponds to the payload setup used by
 * vertex shaders, and by geometry shaders in "dual object" dispatch mode).
 */
void
vec4_visitor::lower_attributes_to_hw_regs(const int *attribute_map,
                                          bool interleaved)
{
   foreach_block_and_inst(block, vec4_instruction, inst, cfg) {
      for (int i = 0; i < 3; i++) {
	 if (inst->src[i].file != ATTR)
	    continue;

         int grf = attribute_map[inst->src[i].nr + inst->src[i].reg_offset];

         /* All attributes used in the shader need to have been assigned a
          * hardware register by the caller
          */
         assert(grf != 0);

	 struct brw_reg reg = attribute_to_hw_reg(grf, interleaved);
	 reg.swizzle = inst->src[i].swizzle;
         reg.type = inst->src[i].type;
	 if (inst->src[i].abs)
	    reg = brw_abs(reg);
	 if (inst->src[i].negate)
	    reg = negate(reg);

         inst->src[i] = reg;
      }
   }
}

int
vec4_vs_visitor::setup_attributes(int payload_reg)
{
   int nr_attributes;
   int attribute_map[VERT_ATTRIB_MAX + 2];
   memset(attribute_map, 0, sizeof(attribute_map));

   nr_attributes = 0;
   for (int i = 0; i < VERT_ATTRIB_MAX; i++) {
      if (vs_prog_data->inputs_read & BITFIELD64_BIT(i)) {
	 attribute_map[i] = payload_reg + nr_attributes;
	 nr_attributes++;
      }
   }

   /* VertexID is stored by the VF as the last vertex element, but we
    * don't represent it with a flag in inputs_read, so we call it
    * VERT_ATTRIB_MAX.
    */
   if (vs_prog_data->uses_vertexid || vs_prog_data->uses_instanceid ||
       vs_prog_data->uses_basevertex || vs_prog_data->uses_baseinstance) {
      attribute_map[VERT_ATTRIB_MAX] = payload_reg + nr_attributes;
      nr_attributes++;
   }

   if (vs_prog_data->uses_drawid) {
      attribute_map[VERT_ATTRIB_MAX + 1] = payload_reg + nr_attributes;
      nr_attributes++;
   }

   lower_attributes_to_hw_regs(attribute_map, false /* interleaved */);

   return payload_reg + vs_prog_data->nr_attributes;
}

int
vec4_visitor::setup_uniforms(int reg)
{
   prog_data->base.dispatch_grf_start_reg = reg;

   /* The pre-gen6 VS requires that some push constants get loaded no
    * matter what, or the GPU would hang.
    */
   if (devinfo->gen < 6 && this->uniforms == 0) {
      stage_prog_data->param =
         reralloc(NULL, stage_prog_data->param, const gl_constant_value *, 4);
      for (unsigned int i = 0; i < 4; i++) {
	 unsigned int slot = this->uniforms * 4 + i;
	 static gl_constant_value zero = { 0.0 };
	 stage_prog_data->param[slot] = &zero;
      }

      this->uniforms++;
      reg++;
   } else {
      reg += ALIGN(uniforms, 2) / 2;
   }

   stage_prog_data->nr_params = this->uniforms * 4;

   prog_data->base.curb_read_length =
      reg - prog_data->base.dispatch_grf_start_reg;

   return reg;
}

void
vec4_vs_visitor::setup_payload(void)
{
   int reg = 0;

   /* The payload always contains important data in g0, which contains
    * the URB handles that are passed on to the URB write at the end
    * of the thread.  So, we always start push constants at g1.
    */
   reg++;

   reg = setup_uniforms(reg);

   reg = setup_attributes(reg);

   this->first_non_payload_grf = reg;
}

bool
vec4_visitor::lower_minmax()
{
   assert(devinfo->gen < 6);

   bool progress = false;

   foreach_block_and_inst_safe(block, vec4_instruction, inst, cfg) {
      const vec4_builder ibld(this, block, inst);

      if (inst->opcode == BRW_OPCODE_SEL &&
          inst->predicate == BRW_PREDICATE_NONE) {
         /* FIXME: Using CMP doesn't preserve the NaN propagation semantics of
          *        the original SEL.L/GE instruction
          */
         ibld.CMP(ibld.null_reg_d(), inst->src[0], inst->src[1],
                  inst->conditional_mod);
         inst->predicate = BRW_PREDICATE_NORMAL;
         inst->conditional_mod = BRW_CONDITIONAL_NONE;

         progress = true;
      }
   }

   if (progress)
      invalidate_live_intervals();

   return progress;
}

src_reg
vec4_visitor::get_timestamp()
{
   assert(devinfo->gen >= 7);

   src_reg ts = src_reg(brw_reg(BRW_ARCHITECTURE_REGISTER_FILE,
                                BRW_ARF_TIMESTAMP,
                                0,
                                0,
                                0,
                                BRW_REGISTER_TYPE_UD,
                                BRW_VERTICAL_STRIDE_0,
                                BRW_WIDTH_4,
                                BRW_HORIZONTAL_STRIDE_4,
                                BRW_SWIZZLE_XYZW,
                                WRITEMASK_XYZW));

   dst_reg dst = dst_reg(this, glsl_type::uvec4_type);

   vec4_instruction *mov = emit(MOV(dst, ts));
   /* We want to read the 3 fields we care about (mostly field 0, but also 2)
    * even if it's not enabled in the dispatch.
    */
   mov->force_writemask_all = true;

   return src_reg(dst);
}

void
vec4_visitor::emit_shader_time_begin()
{
   current_annotation = "shader time start";
   shader_start_time = get_timestamp();
}

void
vec4_visitor::emit_shader_time_end()
{
   current_annotation = "shader time end";
   src_reg shader_end_time = get_timestamp();


   /* Check that there weren't any timestamp reset events (assuming these
    * were the only two timestamp reads that happened).
    */
   src_reg reset_end = shader_end_time;
   reset_end.swizzle = BRW_SWIZZLE_ZZZZ;
   vec4_instruction *test = emit(AND(dst_null_ud(), reset_end, brw_imm_ud(1u)));
   test->conditional_mod = BRW_CONDITIONAL_Z;

   emit(IF(BRW_PREDICATE_NORMAL));

   /* Take the current timestamp and get the delta. */
   shader_start_time.negate = true;
   dst_reg diff = dst_reg(this, glsl_type::uint_type);
   emit(ADD(diff, shader_start_time, shader_end_time));

   /* If there were no instructions between the two timestamp gets, the diff
    * is 2 cycles.  Remove that overhead, so I can forget about that when
    * trying to determine the time taken for single instructions.
    */
   emit(ADD(diff, src_reg(diff), brw_imm_ud(-2u)));

   emit_shader_time_write(0, src_reg(diff));
   emit_shader_time_write(1, brw_imm_ud(1u));
   emit(BRW_OPCODE_ELSE);
   emit_shader_time_write(2, brw_imm_ud(1u));
   emit(BRW_OPCODE_ENDIF);
}

void
vec4_visitor::emit_shader_time_write(int shader_time_subindex, src_reg value)
{
   dst_reg dst =
      dst_reg(this, glsl_type::get_array_instance(glsl_type::vec4_type, 2));

   dst_reg offset = dst;
   dst_reg time = dst;
   time.reg_offset++;

   offset.type = BRW_REGISTER_TYPE_UD;
   int index = shader_time_index * 3 + shader_time_subindex;
   emit(MOV(offset, brw_imm_d(index * SHADER_TIME_STRIDE)));

   time.type = BRW_REGISTER_TYPE_UD;
   emit(MOV(time, value));

   vec4_instruction *inst =
      emit(SHADER_OPCODE_SHADER_TIME_ADD, dst_reg(), src_reg(dst));
   inst->mlen = 2;
}

void
vec4_visitor::convert_to_hw_regs()
{
   foreach_block_and_inst(block, vec4_instruction, inst, cfg) {
      for (int i = 0; i < 3; i++) {
         struct src_reg &src = inst->src[i];
         struct brw_reg reg;
         switch (src.file) {
         case VGRF:
            reg = brw_vec8_grf(src.nr + src.reg_offset, 0);
            reg.type = src.type;
            reg.swizzle = src.swizzle;
            reg.abs = src.abs;
            reg.negate = src.negate;
            break;

         case UNIFORM:
            reg = stride(brw_vec4_grf(prog_data->base.dispatch_grf_start_reg +
                                      (src.nr + src.reg_offset) / 2,
                                      ((src.nr + src.reg_offset) % 2) * 4),
                         0, 4, 1);
            reg.type = src.type;
            reg.swizzle = src.swizzle;
            reg.abs = src.abs;
            reg.negate = src.negate;

            /* This should have been moved to pull constants. */
            assert(!src.reladdr);
            break;

         case ARF:
         case FIXED_GRF:
         case IMM:
            continue;

         case BAD_FILE:
            /* Probably unused. */
            reg = brw_null_reg();
            break;

         case MRF:
         case ATTR:
            unreachable("not reached");
         }

         src = reg;
      }

      if (inst->is_3src(devinfo)) {
         /* 3-src instructions with scalar sources support arbitrary subnr,
          * but don't actually use swizzles.  Convert swizzle into subnr.
          */
         for (int i = 0; i < 3; i++) {
            if (inst->src[i].vstride == BRW_VERTICAL_STRIDE_0) {
               assert(brw_is_single_value_swizzle(inst->src[i].swizzle));
               inst->src[i].subnr += 4 * BRW_GET_SWZ(inst->src[i].swizzle, 0);
            }
         }
      }

      dst_reg &dst = inst->dst;
      struct brw_reg reg;

      switch (inst->dst.file) {
      case VGRF:
         reg = brw_vec8_grf(dst.nr + dst.reg_offset, 0);
         reg.type = dst.type;
         reg.writemask = dst.writemask;
         break;

      case MRF:
         assert(((dst.nr + dst.reg_offset) & ~BRW_MRF_COMPR4) < BRW_MAX_MRF(devinfo->gen));
         reg = brw_message_reg(dst.nr + dst.reg_offset);
         reg.type = dst.type;
         reg.writemask = dst.writemask;
         break;

      case ARF:
      case FIXED_GRF:
         reg = dst.as_brw_reg();
         break;

      case BAD_FILE:
         reg = brw_null_reg();
         break;

      case IMM:
      case ATTR:
      case UNIFORM:
         unreachable("not reached");
      }

      dst = reg;
   }
}

bool
vec4_visitor::run()
{
   if (shader_time_index >= 0)
      emit_shader_time_begin();

   emit_prolog();

   emit_nir_code();
   if (failed)
      return false;
   base_ir = NULL;

   emit_thread_end();

   calculate_cfg();

   /* Before any optimization, push array accesses out to scratch
    * space where we need them to be.  This pass may allocate new
    * virtual GRFs, so we want to do it early.  It also makes sure
    * that we have reladdr computations available for CSE, since we'll
    * often do repeated subexpressions for those.
    */
   move_grf_array_access_to_scratch();
   move_uniform_array_access_to_pull_constants();

   pack_uniform_registers();
   move_push_constants_to_pull_constants();
   split_virtual_grfs();

#define OPT(pass, args...) ({                                          \
      pass_num++;                                                      \
      bool this_progress = pass(args);                                 \
                                                                       \
      if (unlikely(INTEL_DEBUG & DEBUG_OPTIMIZER) && this_progress) {  \
         char filename[64];                                            \
         snprintf(filename, 64, "%s-%s-%02d-%02d-" #pass,              \
                  stage_abbrev, nir->info.name, iteration, pass_num);  \
                                                                       \
         backend_shader::dump_instructions(filename);                  \
      }                                                                \
                                                                       \
      progress = progress || this_progress;                            \
      this_progress;                                                   \
   })


   if (unlikely(INTEL_DEBUG & DEBUG_OPTIMIZER)) {
      char filename[64];
      snprintf(filename, 64, "%s-%s-00-00-start",
               stage_abbrev, nir->info.name);

      backend_shader::dump_instructions(filename);
   }

   bool progress;
   int iteration = 0;
   int pass_num = 0;
   do {
      progress = false;
      pass_num = 0;
      iteration++;

      OPT(opt_predicated_break, this);
      OPT(opt_reduce_swizzle);
      OPT(dead_code_eliminate);
      OPT(dead_control_flow_eliminate, this);
      OPT(opt_copy_propagation);
      OPT(opt_cmod_propagation);
      OPT(opt_cse);
      OPT(opt_algebraic);
      OPT(opt_register_coalesce);
      OPT(eliminate_find_live_channel);
   } while (progress);

   pass_num = 0;

   if (OPT(opt_vector_float)) {
      OPT(opt_cse);
      OPT(opt_copy_propagation, false);
      OPT(opt_copy_propagation, true);
      OPT(dead_code_eliminate);
   }

   if (devinfo->gen <= 5 && OPT(lower_minmax)) {
      OPT(opt_cmod_propagation);
      OPT(opt_cse);
      OPT(opt_copy_propagation);
      OPT(dead_code_eliminate);
   }

   if (failed)
      return false;

   setup_payload();

   if (unlikely(INTEL_DEBUG & DEBUG_SPILL_VEC4)) {
      /* Debug of register spilling: Go spill everything. */
      const int grf_count = alloc.count;
      float spill_costs[alloc.count];
      bool no_spill[alloc.count];
      evaluate_spill_costs(spill_costs, no_spill);
      for (int i = 0; i < grf_count; i++) {
         if (no_spill[i])
            continue;
         spill_reg(i);
      }
   }

   bool allocated_without_spills = reg_allocate();

   if (!allocated_without_spills) {
      compiler->shader_perf_log(log_data,
                                "%s shader triggered register spilling.  "
                                "Try reducing the number of live vec4 values "
                                "to improve performance.\n",
                                stage_name);

      while (!reg_allocate()) {
         if (failed)
            return false;
      }
   }

   opt_schedule_instructions();

   opt_set_dependency_control();

   convert_to_hw_regs();

   if (last_scratch > 0) {
      prog_data->base.total_scratch =
         brw_get_scratch_size(last_scratch * REG_SIZE);
   }

   return !failed;
}

} /* namespace brw */

extern "C" {

/**
 * Compile a vertex shader.
 *
 * Returns the final assembly and the program's size.
 */
const unsigned *
brw_compile_vs(const struct brw_compiler *compiler, void *log_data,
               void *mem_ctx,
               const struct brw_vs_prog_key *key,
               struct brw_vs_prog_data *prog_data,
               const nir_shader *src_shader,
               gl_clip_plane *clip_planes,
               bool use_legacy_snorm_formula,
               int shader_time_index,
               unsigned *final_assembly_size,
               char **error_str)
{
   const bool is_scalar = compiler->scalar_stage[MESA_SHADER_VERTEX];
   nir_shader *shader = nir_shader_clone(mem_ctx, src_shader);
   shader = brw_nir_apply_sampler_key(shader, compiler->devinfo, &key->tex,
                                      is_scalar);
   brw_nir_lower_vs_inputs(shader, compiler->devinfo, is_scalar,
                           use_legacy_snorm_formula, key->gl_attrib_wa_flags);
   brw_nir_lower_vue_outputs(shader, is_scalar);
   shader = brw_postprocess_nir(shader, compiler->devinfo, is_scalar);

   const unsigned *assembly = NULL;

   unsigned nr_attributes = _mesa_bitcount_64(prog_data->inputs_read);

   /* gl_VertexID and gl_InstanceID are system values, but arrive via an
    * incoming vertex attribute.  So, add an extra slot.
    */
   if (shader->info.system_values_read &
       (BITFIELD64_BIT(SYSTEM_VALUE_BASE_VERTEX) |
        BITFIELD64_BIT(SYSTEM_VALUE_BASE_INSTANCE) |
        BITFIELD64_BIT(SYSTEM_VALUE_VERTEX_ID_ZERO_BASE) |
        BITFIELD64_BIT(SYSTEM_VALUE_INSTANCE_ID))) {
      nr_attributes++;
   }

   /* gl_DrawID has its very own vec4 */
   if (shader->info.system_values_read & BITFIELD64_BIT(SYSTEM_VALUE_DRAW_ID)) {
      nr_attributes++;
   }

   unsigned nr_attribute_slots =
      nr_attributes +
      _mesa_bitcount_64(shader->info.double_inputs_read);

   /* The 3DSTATE_VS documentation lists the lower bound on "Vertex URB Entry
    * Read Length" as 1 in vec4 mode, and 0 in SIMD8 mode.  Empirically, in
    * vec4 mode, the hardware appears to wedge unless we read something.
    */
   if (is_scalar)
      prog_data->base.urb_read_length =
         DIV_ROUND_UP(nr_attribute_slots, 2);
   else
      prog_data->base.urb_read_length =
         DIV_ROUND_UP(MAX2(nr_attribute_slots, 1), 2);

   prog_data->nr_attributes = nr_attributes;
   prog_data->nr_attribute_slots = nr_attribute_slots;

   /* Since vertex shaders reuse the same VUE entry for inputs and outputs
    * (overwriting the original contents), we need to make sure the size is
    * the larger of the two.
    */
   const unsigned vue_entries =
      MAX2(nr_attribute_slots, (unsigned)prog_data->base.vue_map.num_slots);

   if (compiler->devinfo->gen == 6)
      prog_data->base.urb_entry_size = DIV_ROUND_UP(vue_entries, 8);
   else
      prog_data->base.urb_entry_size = DIV_ROUND_UP(vue_entries, 4);

   if (is_scalar) {
      prog_data->base.dispatch_mode = DISPATCH_MODE_SIMD8;

      fs_visitor v(compiler, log_data, mem_ctx, key, &prog_data->base.base,
                   NULL, /* prog; Only used for TEXTURE_RECTANGLE on gen < 8 */
                   shader, 8, shader_time_index);
      if (!v.run_vs(clip_planes)) {
         if (error_str)
            *error_str = ralloc_strdup(mem_ctx, v.fail_msg);

         return NULL;
      }

      prog_data->base.base.dispatch_grf_start_reg = v.payload.num_regs;

      fs_generator g(compiler, log_data, mem_ctx, (void *) key,
                     &prog_data->base.base, v.promoted_constants,
                     v.runtime_check_aads_emit, MESA_SHADER_VERTEX);
      if (INTEL_DEBUG & DEBUG_VS) {
         const char *debug_name =
            ralloc_asprintf(mem_ctx, "%s vertex shader %s",
                            shader->info.label ? shader->info.label : "unnamed",
                            shader->info.name);

         g.enable_debug(debug_name);
      }
      g.generate_code(v.cfg, 8);
      assembly = g.get_assembly(final_assembly_size);
   }

   if (!assembly) {
      prog_data->base.dispatch_mode = DISPATCH_MODE_4X2_DUAL_OBJECT;

      vec4_vs_visitor v(compiler, log_data, key, prog_data,
                        shader, clip_planes, mem_ctx,
                        shader_time_index, use_legacy_snorm_formula);
      if (!v.run()) {
         if (error_str)
            *error_str = ralloc_strdup(mem_ctx, v.fail_msg);

         return NULL;
      }

      assembly = brw_vec4_generate_assembly(compiler, log_data, mem_ctx,
                                            shader, &prog_data->base, v.cfg,
                                            final_assembly_size);
   }

   return assembly;
}

} /* extern "C" */
