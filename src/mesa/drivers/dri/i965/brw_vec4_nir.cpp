/*
 * Copyright © 2015 Intel Corporation
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

#include "brw_nir.h"
#include "brw_vec4.h"
#include "brw_vec4_builder.h"
#include "brw_vec4_surface_builder.h"
#include "brw_program.h"

using namespace brw;
using namespace brw::surface_access;

namespace brw {

void
vec4_visitor::emit_nir_code()
{
   if (nir->num_uniforms > 0)
      nir_setup_uniforms();

   nir_setup_system_values();

   /* get the main function and emit it */
   nir_foreach_function(function, nir) {
      assert(strcmp(function->name, "main") == 0);
      assert(function->impl);
      nir_emit_impl(function->impl);
   }
}

void
vec4_visitor::nir_setup_system_value_intrinsic(nir_intrinsic_instr *instr)
{
   dst_reg *reg;

   switch (instr->intrinsic) {
   case nir_intrinsic_load_vertex_id:
      unreachable("should be lowered by lower_vertex_id().");

   case nir_intrinsic_load_vertex_id_zero_base:
      reg = &nir_system_values[SYSTEM_VALUE_VERTEX_ID_ZERO_BASE];
      if (reg->file == BAD_FILE)
         *reg = *make_reg_for_system_value(SYSTEM_VALUE_VERTEX_ID_ZERO_BASE);
      break;

   case nir_intrinsic_load_base_vertex:
      reg = &nir_system_values[SYSTEM_VALUE_BASE_VERTEX];
      if (reg->file == BAD_FILE)
         *reg = *make_reg_for_system_value(SYSTEM_VALUE_BASE_VERTEX);
      break;

   case nir_intrinsic_load_instance_id:
      reg = &nir_system_values[SYSTEM_VALUE_INSTANCE_ID];
      if (reg->file == BAD_FILE)
         *reg = *make_reg_for_system_value(SYSTEM_VALUE_INSTANCE_ID);
      break;

   case nir_intrinsic_load_base_instance:
      reg = &nir_system_values[SYSTEM_VALUE_BASE_INSTANCE];
      if (reg->file == BAD_FILE)
         *reg = *make_reg_for_system_value(SYSTEM_VALUE_BASE_INSTANCE);
      break;

   case nir_intrinsic_load_draw_id:
      reg = &nir_system_values[SYSTEM_VALUE_DRAW_ID];
      if (reg->file == BAD_FILE)
         *reg = *make_reg_for_system_value(SYSTEM_VALUE_DRAW_ID);
      break;

   default:
      break;
   }
}

static bool
setup_system_values_block(nir_block *block, vec4_visitor *v)
{
   nir_foreach_instr(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      v->nir_setup_system_value_intrinsic(intrin);
   }

   return true;
}

void
vec4_visitor::nir_setup_system_values()
{
   nir_system_values = ralloc_array(mem_ctx, dst_reg, SYSTEM_VALUE_MAX);
   for (unsigned i = 0; i < SYSTEM_VALUE_MAX; i++) {
      nir_system_values[i] = dst_reg();
   }

   nir_foreach_function(function, nir) {
      assert(strcmp(function->name, "main") == 0);
      assert(function->impl);
      nir_foreach_block(block, function->impl) {
         setup_system_values_block(block, this);
      }
   }
}

void
vec4_visitor::nir_setup_uniforms()
{
   uniforms = nir->num_uniforms / 16;
}

void
vec4_visitor::nir_emit_impl(nir_function_impl *impl)
{
   nir_locals = ralloc_array(mem_ctx, dst_reg, impl->reg_alloc);
   for (unsigned i = 0; i < impl->reg_alloc; i++) {
      nir_locals[i] = dst_reg();
   }

   foreach_list_typed(nir_register, reg, node, &impl->registers) {
      unsigned array_elems =
         reg->num_array_elems == 0 ? 1 : reg->num_array_elems;

      nir_locals[reg->index] = dst_reg(VGRF, alloc.allocate(array_elems));
   }

   nir_ssa_values = ralloc_array(mem_ctx, dst_reg, impl->ssa_alloc);

   nir_emit_cf_list(&impl->body);
}

void
vec4_visitor::nir_emit_cf_list(exec_list *list)
{
   exec_list_validate(list);
   foreach_list_typed(nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_if:
         nir_emit_if(nir_cf_node_as_if(node));
         break;

      case nir_cf_node_loop:
         nir_emit_loop(nir_cf_node_as_loop(node));
         break;

      case nir_cf_node_block:
         nir_emit_block(nir_cf_node_as_block(node));
         break;

      default:
         unreachable("Invalid CFG node block");
      }
   }
}

void
vec4_visitor::nir_emit_if(nir_if *if_stmt)
{
   /* First, put the condition in f0 */
   src_reg condition = get_nir_src(if_stmt->condition, BRW_REGISTER_TYPE_D, 1);
   vec4_instruction *inst = emit(MOV(dst_null_d(), condition));
   inst->conditional_mod = BRW_CONDITIONAL_NZ;

   /* We can just predicate based on the X channel, as the condition only
    * goes on its own line */
   emit(IF(BRW_PREDICATE_ALIGN16_REPLICATE_X));

   nir_emit_cf_list(&if_stmt->then_list);

   /* note: if the else is empty, dead CF elimination will remove it */
   emit(BRW_OPCODE_ELSE);

   nir_emit_cf_list(&if_stmt->else_list);

   emit(BRW_OPCODE_ENDIF);
}

void
vec4_visitor::nir_emit_loop(nir_loop *loop)
{
   emit(BRW_OPCODE_DO);

   nir_emit_cf_list(&loop->body);

   emit(BRW_OPCODE_WHILE);
}

void
vec4_visitor::nir_emit_block(nir_block *block)
{
   nir_foreach_instr(instr, block) {
      nir_emit_instr(instr);
   }
}

void
vec4_visitor::nir_emit_instr(nir_instr *instr)
{
   base_ir = instr;

   switch (instr->type) {
   case nir_instr_type_load_const:
      nir_emit_load_const(nir_instr_as_load_const(instr));
      break;

   case nir_instr_type_intrinsic:
      nir_emit_intrinsic(nir_instr_as_intrinsic(instr));
      break;

   case nir_instr_type_alu:
      nir_emit_alu(nir_instr_as_alu(instr));
      break;

   case nir_instr_type_jump:
      nir_emit_jump(nir_instr_as_jump(instr));
      break;

   case nir_instr_type_tex:
      nir_emit_texture(nir_instr_as_tex(instr));
      break;

   case nir_instr_type_ssa_undef:
      nir_emit_undef(nir_instr_as_ssa_undef(instr));
      break;

   default:
      fprintf(stderr, "VS instruction not yet implemented by NIR->vec4\n");
      break;
   }
}

static dst_reg
dst_reg_for_nir_reg(vec4_visitor *v, nir_register *nir_reg,
                    unsigned base_offset, nir_src *indirect)
{
   dst_reg reg;

   reg = v->nir_locals[nir_reg->index];
   reg = offset(reg, base_offset);
   if (indirect) {
      reg.reladdr =
         new(v->mem_ctx) src_reg(v->get_nir_src(*indirect,
                                                BRW_REGISTER_TYPE_D,
                                                1));
   }
   return reg;
}

dst_reg
vec4_visitor::get_nir_dest(const nir_dest &dest)
{
   if (dest.is_ssa) {
      dst_reg dst = dst_reg(VGRF, alloc.allocate(1));
      nir_ssa_values[dest.ssa.index] = dst;
      return dst;
   } else {
      return dst_reg_for_nir_reg(this, dest.reg.reg, dest.reg.base_offset,
                                 dest.reg.indirect);
   }
}

dst_reg
vec4_visitor::get_nir_dest(const nir_dest &dest, enum brw_reg_type type)
{
   return retype(get_nir_dest(dest), type);
}

dst_reg
vec4_visitor::get_nir_dest(const nir_dest &dest, nir_alu_type type)
{
   return get_nir_dest(dest, brw_type_for_nir_type(type));
}

src_reg
vec4_visitor::get_nir_src(const nir_src &src, enum brw_reg_type type,
                          unsigned num_components)
{
   dst_reg reg;

   if (src.is_ssa) {
      assert(src.ssa != NULL);
      reg = nir_ssa_values[src.ssa->index];
   }
   else {
     reg = dst_reg_for_nir_reg(this, src.reg.reg, src.reg.base_offset,
                               src.reg.indirect);
   }

   reg = retype(reg, type);

   src_reg reg_as_src = src_reg(reg);
   reg_as_src.swizzle = brw_swizzle_for_size(num_components);
   return reg_as_src;
}

src_reg
vec4_visitor::get_nir_src(const nir_src &src, nir_alu_type type,
                          unsigned num_components)
{
   return get_nir_src(src, brw_type_for_nir_type(type), num_components);
}

src_reg
vec4_visitor::get_nir_src(const nir_src &src, unsigned num_components)
{
   /* if type is not specified, default to signed int */
   return get_nir_src(src, nir_type_int, num_components);
}

src_reg
vec4_visitor::get_indirect_offset(nir_intrinsic_instr *instr)
{
   nir_src *offset_src = nir_get_io_offset_src(instr);
   nir_const_value *const_value = nir_src_as_const_value(*offset_src);

   if (const_value) {
      /* The only constant offset we should find is 0.  brw_nir.c's
       * add_const_offset_to_base() will fold other constant offsets
       * into instr->const_index[0].
       */
      assert(const_value->u32[0] == 0);
      return src_reg();
   }

   return get_nir_src(*offset_src, BRW_REGISTER_TYPE_UD, 1);
}

void
vec4_visitor::nir_emit_load_const(nir_load_const_instr *instr)
{
   dst_reg reg = dst_reg(VGRF, alloc.allocate(1));
   reg.type =  BRW_REGISTER_TYPE_D;

   unsigned remaining = brw_writemask_for_size(instr->def.num_components);

   /* @FIXME: consider emitting vector operations to save some MOVs in
    * cases where the components are representable in 8 bits.
    * For now, we emit a MOV for each distinct value.
    */
   for (unsigned i = 0; i < instr->def.num_components; i++) {
      unsigned writemask = 1 << i;

      if ((remaining & writemask) == 0)
         continue;

      for (unsigned j = i; j < instr->def.num_components; j++) {
         if (instr->value.u32[i] == instr->value.u32[j]) {
            writemask |= 1 << j;
         }
      }

      reg.writemask = writemask;
      emit(MOV(reg, brw_imm_d(instr->value.i32[i])));

      remaining &= ~writemask;
   }

   /* Set final writemask */
   reg.writemask = brw_writemask_for_size(instr->def.num_components);

   nir_ssa_values[instr->def.index] = reg;
}

void
vec4_visitor::nir_emit_intrinsic(nir_intrinsic_instr *instr)
{
   dst_reg dest;
   src_reg src;

   switch (instr->intrinsic) {

   case nir_intrinsic_load_input: {
      nir_const_value *const_offset = nir_src_as_const_value(instr->src[0]);

      /* We set EmitNoIndirectInput for VS */
      assert(const_offset);

      src = src_reg(ATTR, instr->const_index[0] + const_offset->u32[0],
                    glsl_type::uvec4_type);

      dest = get_nir_dest(instr->dest, src.type);
      dest.writemask = brw_writemask_for_size(instr->num_components);

      emit(MOV(dest, src));
      break;
   }

   case nir_intrinsic_store_output: {
      nir_const_value *const_offset = nir_src_as_const_value(instr->src[1]);
      assert(const_offset);

      int varying = instr->const_index[0] + const_offset->u32[0];

      src = get_nir_src(instr->src[0], BRW_REGISTER_TYPE_F,
                        instr->num_components);

      output_reg[varying] = dst_reg(src);
      break;
   }

   case nir_intrinsic_get_buffer_size: {
      nir_const_value *const_uniform_block = nir_src_as_const_value(instr->src[0]);
      unsigned ssbo_index = const_uniform_block ? const_uniform_block->u32[0] : 0;

      const unsigned index =
         prog_data->base.binding_table.ssbo_start + ssbo_index;
      dst_reg result_dst = get_nir_dest(instr->dest);
      vec4_instruction *inst = new(mem_ctx)
         vec4_instruction(VS_OPCODE_GET_BUFFER_SIZE, result_dst);

      inst->base_mrf = 2;
      inst->mlen = 1; /* always at least one */
      inst->src[1] = brw_imm_ud(index);

      /* MRF for the first parameter */
      src_reg lod = brw_imm_d(0);
      int param_base = inst->base_mrf;
      int writemask = WRITEMASK_X;
      emit(MOV(dst_reg(MRF, param_base, glsl_type::int_type, writemask), lod));

      emit(inst);

      brw_mark_surface_used(&prog_data->base, index);
      break;
   }

   case nir_intrinsic_store_ssbo: {
      assert(devinfo->gen >= 7);

      /* Block index */
      src_reg surf_index;
      nir_const_value *const_uniform_block =
         nir_src_as_const_value(instr->src[1]);
      if (const_uniform_block) {
         unsigned index = prog_data->base.binding_table.ssbo_start +
                          const_uniform_block->u32[0];
         surf_index = brw_imm_ud(index);
         brw_mark_surface_used(&prog_data->base, index);
      } else {
         surf_index = src_reg(this, glsl_type::uint_type);
         emit(ADD(dst_reg(surf_index), get_nir_src(instr->src[1], 1),
                  brw_imm_ud(prog_data->base.binding_table.ssbo_start)));
         surf_index = emit_uniformize(surf_index);

         brw_mark_surface_used(&prog_data->base,
                               prog_data->base.binding_table.ssbo_start +
                               nir->info.num_ssbos - 1);
      }

      /* Offset */
      src_reg offset_reg;
      nir_const_value *const_offset = nir_src_as_const_value(instr->src[2]);
      if (const_offset) {
         offset_reg = brw_imm_ud(const_offset->u32[0]);
      } else {
         offset_reg = get_nir_src(instr->src[2], 1);
      }

      /* Value */
      src_reg val_reg = get_nir_src(instr->src[0], 4);

      /* Writemask */
      unsigned write_mask = instr->const_index[0];

      /* IvyBridge does not have a native SIMD4x2 untyped write message so untyped
       * writes will use SIMD8 mode. In order to hide this and keep symmetry across
       * typed and untyped messages and across hardware platforms, the
       * current implementation of the untyped messages will transparently convert
       * the SIMD4x2 payload into an equivalent SIMD8 payload by transposing it
       * and enabling only channel X on the SEND instruction.
       *
       * The above, works well for full vector writes, but not for partial writes
       * where we want to write some channels and not others, like when we have
       * code such as v.xyw = vec3(1,2,4). Because the untyped write messages are
       * quite restrictive with regards to the channel enables we can configure in
       * the message descriptor (not all combinations are allowed) we cannot simply
       * implement these scenarios with a single message while keeping the
       * aforementioned symmetry in the implementation. For now we de decided that
       * it is better to keep the symmetry to reduce complexity, so in situations
       * such as the one described we end up emitting two untyped write messages
       * (one for xy and another for w).
       *
       * The code below packs consecutive channels into a single write message,
       * detects gaps in the vector write and if needed, sends a second message
       * with the remaining channels. If in the future we decide that we want to
       * emit a single message at the expense of losing the symmetry in the
       * implementation we can:
       *
       * 1) For IvyBridge: Only use the red channel of the untyped write SIMD8
       *    message payload. In this mode we can write up to 8 offsets and dwords
       *    to the red channel only (for the two vec4s in the SIMD4x2 execution)
       *    and select which of the 8 channels carry data to write by setting the
       *    appropriate writemask in the dst register of the SEND instruction.
       *    It would require to write a new generator opcode specifically for
       *    IvyBridge since we would need to prepare a SIMD8 payload that could
       *    use any channel, not just X.
       *
       * 2) For Haswell+: Simply send a single write message but set the writemask
       *    on the dst of the SEND instruction to select the channels we want to
       *    write. It would require to modify the current messages to receive
       *    and honor the writemask provided.
       */
      const vec4_builder bld = vec4_builder(this).at_end()
                               .annotate(current_annotation, base_ir);

      int swizzle[4] = { 0, 0, 0, 0};
      int num_channels = 0;
      unsigned skipped_channels = 0;
      int num_components = instr->num_components;
      for (int i = 0; i < num_components; i++) {
         /* Check if this channel needs to be written. If so, record the
          * channel we need to take the data from in the swizzle array
          */
         int component_mask = 1 << i;
         int write_test = write_mask & component_mask;
         if (write_test)
            swizzle[num_channels++] = i;

         /* If we don't have to write this channel it means we have a gap in the
          * vector, so write the channels we accumulated until now, if any. Do
          * the same if this was the last component in the vector.
          */
         if (!write_test || i == num_components - 1) {
            if (num_channels > 0) {
               /* We have channels to write, so update the offset we need to
                * write at to skip the channels we skipped, if any.
                */
               if (skipped_channels > 0) {
                  if (offset_reg.file == IMM) {
                     offset_reg.ud += 4 * skipped_channels;
                  } else {
                     emit(ADD(dst_reg(offset_reg), offset_reg,
                              brw_imm_ud(4 * skipped_channels)));
                  }
               }

               /* Swizzle the data register so we take the data from the channels
                * we need to write and send the write message. This will write
                * num_channels consecutive dwords starting at offset.
                */
               val_reg.swizzle =
                  BRW_SWIZZLE4(swizzle[0], swizzle[1], swizzle[2], swizzle[3]);
               emit_untyped_write(bld, surf_index, offset_reg, val_reg,
                                  1 /* dims */, num_channels /* size */,
                                  BRW_PREDICATE_NONE);

               /* If we have to do a second write we will have to update the
                * offset so that we jump over the channels we have just written
                * now.
                */
               skipped_channels = num_channels;

               /* Restart the count for the next write message */
               num_channels = 0;
            }

            /* We did not write the current channel, so increase skipped count */
            skipped_channels++;
         }
      }

      break;
   }

   case nir_intrinsic_load_ssbo: {
      assert(devinfo->gen >= 7);

      nir_const_value *const_uniform_block =
         nir_src_as_const_value(instr->src[0]);

      src_reg surf_index;
      if (const_uniform_block) {
         unsigned index = prog_data->base.binding_table.ssbo_start +
                          const_uniform_block->u32[0];
         surf_index = brw_imm_ud(index);

         brw_mark_surface_used(&prog_data->base, index);
      } else {
         surf_index = src_reg(this, glsl_type::uint_type);
         emit(ADD(dst_reg(surf_index), get_nir_src(instr->src[0], 1),
                  brw_imm_ud(prog_data->base.binding_table.ssbo_start)));
         surf_index = emit_uniformize(surf_index);

         /* Assume this may touch any UBO. It would be nice to provide
          * a tighter bound, but the array information is already lowered away.
          */
         brw_mark_surface_used(&prog_data->base,
                               prog_data->base.binding_table.ssbo_start +
                               nir->info.num_ssbos - 1);
      }

      src_reg offset_reg;
      nir_const_value *const_offset = nir_src_as_const_value(instr->src[1]);
      if (const_offset) {
         offset_reg = brw_imm_ud(const_offset->u32[0]);
      } else {
         offset_reg = get_nir_src(instr->src[1], 1);
      }

      /* Read the vector */
      const vec4_builder bld = vec4_builder(this).at_end()
         .annotate(current_annotation, base_ir);

      src_reg read_result = emit_untyped_read(bld, surf_index, offset_reg,
                                              1 /* dims */, 4 /* size*/,
                                              BRW_PREDICATE_NONE);
      dst_reg dest = get_nir_dest(instr->dest);
      read_result.type = dest.type;
      read_result.swizzle = brw_swizzle_for_size(instr->num_components);
      emit(MOV(dest, read_result));

      break;
   }

   case nir_intrinsic_ssbo_atomic_add:
      nir_emit_ssbo_atomic(BRW_AOP_ADD, instr);
      break;
   case nir_intrinsic_ssbo_atomic_imin:
      nir_emit_ssbo_atomic(BRW_AOP_IMIN, instr);
      break;
   case nir_intrinsic_ssbo_atomic_umin:
      nir_emit_ssbo_atomic(BRW_AOP_UMIN, instr);
      break;
   case nir_intrinsic_ssbo_atomic_imax:
      nir_emit_ssbo_atomic(BRW_AOP_IMAX, instr);
      break;
   case nir_intrinsic_ssbo_atomic_umax:
      nir_emit_ssbo_atomic(BRW_AOP_UMAX, instr);
      break;
   case nir_intrinsic_ssbo_atomic_and:
      nir_emit_ssbo_atomic(BRW_AOP_AND, instr);
      break;
   case nir_intrinsic_ssbo_atomic_or:
      nir_emit_ssbo_atomic(BRW_AOP_OR, instr);
      break;
   case nir_intrinsic_ssbo_atomic_xor:
      nir_emit_ssbo_atomic(BRW_AOP_XOR, instr);
      break;
   case nir_intrinsic_ssbo_atomic_exchange:
      nir_emit_ssbo_atomic(BRW_AOP_MOV, instr);
      break;
   case nir_intrinsic_ssbo_atomic_comp_swap:
      nir_emit_ssbo_atomic(BRW_AOP_CMPWR, instr);
      break;

   case nir_intrinsic_load_vertex_id:
      unreachable("should be lowered by lower_vertex_id()");

   case nir_intrinsic_load_vertex_id_zero_base:
   case nir_intrinsic_load_base_vertex:
   case nir_intrinsic_load_instance_id:
   case nir_intrinsic_load_base_instance:
   case nir_intrinsic_load_draw_id:
   case nir_intrinsic_load_invocation_id: {
      gl_system_value sv = nir_system_value_from_intrinsic(instr->intrinsic);
      src_reg val = src_reg(nir_system_values[sv]);
      assert(val.file != BAD_FILE);
      dest = get_nir_dest(instr->dest, val.type);
      emit(MOV(dest, val));
      break;
   }

   case nir_intrinsic_load_uniform: {
      /* Offsets are in bytes but they should always be multiples of 4 */
      assert(nir_intrinsic_base(instr) % 4 == 0);

      dest = get_nir_dest(instr->dest);

      src = src_reg(dst_reg(UNIFORM, nir_intrinsic_base(instr) / 16));
      src.type = dest.type;

      /* Uniforms don't actually have to be vec4 aligned.  In the case that
       * it isn't, we have to use a swizzle to shift things around.  They
       * do still have the std140 alignment requirement that vec2's have to
       * be vec2-aligned and vec3's and vec4's have to be vec4-aligned.
       *
       * The swizzle also works in the indirect case as the generator adds
       * the swizzle to the offset for us.
       */
      unsigned shift = (nir_intrinsic_base(instr) % 16) / 4;
      assert(shift + instr->num_components <= 4);

      nir_const_value *const_offset = nir_src_as_const_value(instr->src[0]);
      if (const_offset) {
         /* Offsets are in bytes but they should always be multiples of 4 */
         assert(const_offset->u32[0] % 4 == 0);

         unsigned offset = const_offset->u32[0] + shift * 4;
         src.reg_offset = offset / 16;
         shift = (offset % 16) / 4;
         src.swizzle += BRW_SWIZZLE4(shift, shift, shift, shift);

         emit(MOV(dest, src));
      } else {
         src.swizzle += BRW_SWIZZLE4(shift, shift, shift, shift);

         src_reg indirect = get_nir_src(instr->src[0], BRW_REGISTER_TYPE_UD, 1);

         /* MOV_INDIRECT is going to stomp the whole thing anyway */
         dest.writemask = WRITEMASK_XYZW;

         emit(SHADER_OPCODE_MOV_INDIRECT, dest, src,
              indirect, brw_imm_ud(instr->const_index[1]));
      }
      break;
   }

   case nir_intrinsic_atomic_counter_read:
   case nir_intrinsic_atomic_counter_inc:
   case nir_intrinsic_atomic_counter_dec: {
      unsigned surf_index = prog_data->base.binding_table.abo_start +
         (unsigned) instr->const_index[0];
      src_reg offset = get_nir_src(instr->src[0], nir_type_int,
                                   instr->num_components);
      const src_reg surface = brw_imm_ud(surf_index);
      const vec4_builder bld =
         vec4_builder(this).at_end().annotate(current_annotation, base_ir);
      src_reg tmp;

      dest = get_nir_dest(instr->dest);

      switch (instr->intrinsic) {
      case nir_intrinsic_atomic_counter_inc:
         tmp = emit_untyped_atomic(bld, surface, offset,
                                   src_reg(), src_reg(),
                                   1, 1,
                                   BRW_AOP_INC);
         break;
      case nir_intrinsic_atomic_counter_dec:
         tmp = emit_untyped_atomic(bld, surface, offset,
                                   src_reg(), src_reg(),
                                   1, 1,
                                   BRW_AOP_PREDEC);
         break;
      case nir_intrinsic_atomic_counter_read:
         tmp = emit_untyped_read(bld, surface, offset, 1, 1);
         break;
      default:
         unreachable("Unreachable");
      }

      bld.MOV(retype(dest, tmp.type), tmp);
      brw_mark_surface_used(stage_prog_data, surf_index);
      break;
   }

   case nir_intrinsic_load_ubo: {
      nir_const_value *const_block_index = nir_src_as_const_value(instr->src[0]);
      src_reg surf_index;

      dest = get_nir_dest(instr->dest);

      if (const_block_index) {
         /* The block index is a constant, so just emit the binding table entry
          * as an immediate.
          */
         const unsigned index = prog_data->base.binding_table.ubo_start +
                                const_block_index->u32[0];
         surf_index = brw_imm_ud(index);
         brw_mark_surface_used(&prog_data->base, index);
      } else {
         /* The block index is not a constant. Evaluate the index expression
          * per-channel and add the base UBO index; we have to select a value
          * from any live channel.
          */
         surf_index = src_reg(this, glsl_type::uint_type);
         emit(ADD(dst_reg(surf_index), get_nir_src(instr->src[0], nir_type_int,
                                                   instr->num_components),
                  brw_imm_ud(prog_data->base.binding_table.ubo_start)));
         surf_index = emit_uniformize(surf_index);

         /* Assume this may touch any UBO. It would be nice to provide
          * a tighter bound, but the array information is already lowered away.
          */
         brw_mark_surface_used(&prog_data->base,
                               prog_data->base.binding_table.ubo_start +
                               nir->info.num_ubos - 1);
      }

      src_reg offset;
      nir_const_value *const_offset = nir_src_as_const_value(instr->src[1]);
      if (const_offset) {
         offset = brw_imm_ud(const_offset->u32[0] & ~15);
      } else {
         offset = get_nir_src(instr->src[1], nir_type_int, 1);
      }

      src_reg packed_consts = src_reg(this, glsl_type::vec4_type);
      packed_consts.type = dest.type;

      emit_pull_constant_load_reg(dst_reg(packed_consts),
                                  surf_index,
                                  offset,
                                  NULL, NULL /* before_block/inst */);

      packed_consts.swizzle = brw_swizzle_for_size(instr->num_components);
      if (const_offset) {
         packed_consts.swizzle += BRW_SWIZZLE4(const_offset->u32[0] % 16 / 4,
                                               const_offset->u32[0] % 16 / 4,
                                               const_offset->u32[0] % 16 / 4,
                                               const_offset->u32[0] % 16 / 4);
      }

      emit(MOV(dest, packed_consts));
      break;
   }

   case nir_intrinsic_memory_barrier: {
      const vec4_builder bld =
         vec4_builder(this).at_end().annotate(current_annotation, base_ir);
      const dst_reg tmp = bld.vgrf(BRW_REGISTER_TYPE_UD, 2);
      bld.emit(SHADER_OPCODE_MEMORY_FENCE, tmp)
         ->regs_written = 2;
      break;
   }

   case nir_intrinsic_shader_clock: {
      /* We cannot do anything if there is an event, so ignore it for now */
      const src_reg shader_clock = get_timestamp();
      const enum brw_reg_type type = brw_type_for_base_type(glsl_type::uvec2_type);

      dest = get_nir_dest(instr->dest, type);
      emit(MOV(dest, shader_clock));
      break;
   }

   default:
      unreachable("Unknown intrinsic");
   }
}

void
vec4_visitor::nir_emit_ssbo_atomic(int op, nir_intrinsic_instr *instr)
{
   dst_reg dest;
   if (nir_intrinsic_infos[instr->intrinsic].has_dest)
      dest = get_nir_dest(instr->dest);

   src_reg surface;
   nir_const_value *const_surface = nir_src_as_const_value(instr->src[0]);
   if (const_surface) {
      unsigned surf_index = prog_data->base.binding_table.ssbo_start +
                            const_surface->u32[0];
      surface = brw_imm_ud(surf_index);
      brw_mark_surface_used(&prog_data->base, surf_index);
   } else {
      surface = src_reg(this, glsl_type::uint_type);
      emit(ADD(dst_reg(surface), get_nir_src(instr->src[0]),
               brw_imm_ud(prog_data->base.binding_table.ssbo_start)));

      /* Assume this may touch any UBO. This is the same we do for other
       * UBO/SSBO accesses with non-constant surface.
       */
      brw_mark_surface_used(&prog_data->base,
                            prog_data->base.binding_table.ssbo_start +
                            nir->info.num_ssbos - 1);
   }

   src_reg offset = get_nir_src(instr->src[1], 1);
   src_reg data1 = get_nir_src(instr->src[2], 1);
   src_reg data2;
   if (op == BRW_AOP_CMPWR)
      data2 = get_nir_src(instr->src[3], 1);

   /* Emit the actual atomic operation operation */
   const vec4_builder bld =
      vec4_builder(this).at_end().annotate(current_annotation, base_ir);

   src_reg atomic_result = emit_untyped_atomic(bld, surface, offset,
                                               data1, data2,
                                               1 /* dims */, 1 /* rsize */,
                                               op,
                                               BRW_PREDICATE_NONE);
   dest.type = atomic_result.type;
   bld.MOV(dest, atomic_result);
}

static unsigned
brw_swizzle_for_nir_swizzle(uint8_t swizzle[4])
{
   return BRW_SWIZZLE4(swizzle[0], swizzle[1], swizzle[2], swizzle[3]);
}

static enum brw_conditional_mod
brw_conditional_for_nir_comparison(nir_op op)
{
   switch (op) {
   case nir_op_flt:
   case nir_op_ilt:
   case nir_op_ult:
      return BRW_CONDITIONAL_L;

   case nir_op_fge:
   case nir_op_ige:
   case nir_op_uge:
      return BRW_CONDITIONAL_GE;

   case nir_op_feq:
   case nir_op_ieq:
   case nir_op_ball_fequal2:
   case nir_op_ball_iequal2:
   case nir_op_ball_fequal3:
   case nir_op_ball_iequal3:
   case nir_op_ball_fequal4:
   case nir_op_ball_iequal4:
      return BRW_CONDITIONAL_Z;

   case nir_op_fne:
   case nir_op_ine:
   case nir_op_bany_fnequal2:
   case nir_op_bany_inequal2:
   case nir_op_bany_fnequal3:
   case nir_op_bany_inequal3:
   case nir_op_bany_fnequal4:
   case nir_op_bany_inequal4:
      return BRW_CONDITIONAL_NZ;

   default:
      unreachable("not reached: bad operation for comparison");
   }
}

bool
vec4_visitor::optimize_predicate(nir_alu_instr *instr,
                                 enum brw_predicate *predicate)
{
   if (!instr->src[0].src.is_ssa ||
       instr->src[0].src.ssa->parent_instr->type != nir_instr_type_alu)
      return false;

   nir_alu_instr *cmp_instr =
      nir_instr_as_alu(instr->src[0].src.ssa->parent_instr);

   switch (cmp_instr->op) {
   case nir_op_bany_fnequal2:
   case nir_op_bany_inequal2:
   case nir_op_bany_fnequal3:
   case nir_op_bany_inequal3:
   case nir_op_bany_fnequal4:
   case nir_op_bany_inequal4:
      *predicate = BRW_PREDICATE_ALIGN16_ANY4H;
      break;
   case nir_op_ball_fequal2:
   case nir_op_ball_iequal2:
   case nir_op_ball_fequal3:
   case nir_op_ball_iequal3:
   case nir_op_ball_fequal4:
   case nir_op_ball_iequal4:
      *predicate = BRW_PREDICATE_ALIGN16_ALL4H;
      break;
   default:
      return false;
   }

   unsigned size_swizzle =
      brw_swizzle_for_size(nir_op_infos[cmp_instr->op].input_sizes[0]);

   src_reg op[2];
   assert(nir_op_infos[cmp_instr->op].num_inputs == 2);
   for (unsigned i = 0; i < 2; i++) {
      op[i] = get_nir_src(cmp_instr->src[i].src,
                          nir_op_infos[cmp_instr->op].input_types[i], 4);
      unsigned base_swizzle =
         brw_swizzle_for_nir_swizzle(cmp_instr->src[i].swizzle);
      op[i].swizzle = brw_compose_swizzle(size_swizzle, base_swizzle);
      op[i].abs = cmp_instr->src[i].abs;
      op[i].negate = cmp_instr->src[i].negate;
   }

   emit(CMP(dst_null_d(), op[0], op[1],
            brw_conditional_for_nir_comparison(cmp_instr->op)));

   return true;
}

void
vec4_visitor::nir_emit_alu(nir_alu_instr *instr)
{
   vec4_instruction *inst;

   dst_reg dst = get_nir_dest(instr->dest.dest,
                              nir_op_infos[instr->op].output_type);
   dst.writemask = instr->dest.write_mask;

   src_reg op[4];
   for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
      op[i] = get_nir_src(instr->src[i].src,
                          nir_op_infos[instr->op].input_types[i], 4);
      op[i].swizzle = brw_swizzle_for_nir_swizzle(instr->src[i].swizzle);
      op[i].abs = instr->src[i].abs;
      op[i].negate = instr->src[i].negate;
   }

   switch (instr->op) {
   case nir_op_imov:
   case nir_op_fmov:
      inst = emit(MOV(dst, op[0]));
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4:
      unreachable("not reached: should be handled by lower_vec_to_movs()");

   case nir_op_i2f:
   case nir_op_u2f:
      inst = emit(MOV(dst, op[0]));
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_f2i:
   case nir_op_f2u:
      inst = emit(MOV(dst, op[0]));
      break;

   case nir_op_fadd:
      /* fall through */
   case nir_op_iadd:
      inst = emit(ADD(dst, op[0], op[1]));
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fmul:
      inst = emit(MUL(dst, op[0], op[1]));
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_imul: {
      if (devinfo->gen < 8) {
         nir_const_value *value0 = nir_src_as_const_value(instr->src[0].src);
         nir_const_value *value1 = nir_src_as_const_value(instr->src[1].src);

         /* For integer multiplication, the MUL uses the low 16 bits of one of
          * the operands (src0 through SNB, src1 on IVB and later). The MACH
          * accumulates in the contribution of the upper 16 bits of that
          * operand. If we can determine that one of the args is in the low
          * 16 bits, though, we can just emit a single MUL.
          */
         if (value0 && value0->u32[0] < (1 << 16)) {
            if (devinfo->gen < 7)
               emit(MUL(dst, op[0], op[1]));
            else
               emit(MUL(dst, op[1], op[0]));
         } else if (value1 && value1->u32[0] < (1 << 16)) {
            if (devinfo->gen < 7)
               emit(MUL(dst, op[1], op[0]));
            else
               emit(MUL(dst, op[0], op[1]));
         } else {
            struct brw_reg acc = retype(brw_acc_reg(8), dst.type);

            emit(MUL(acc, op[0], op[1]));
            emit(MACH(dst_null_d(), op[0], op[1]));
            emit(MOV(dst, src_reg(acc)));
         }
      } else {
	 emit(MUL(dst, op[0], op[1]));
      }
      break;
   }

   case nir_op_imul_high:
   case nir_op_umul_high: {
      struct brw_reg acc = retype(brw_acc_reg(8), dst.type);

      if (devinfo->gen >= 8)
         emit(MUL(acc, op[0], retype(op[1], BRW_REGISTER_TYPE_UW)));
      else
         emit(MUL(acc, op[0], op[1]));

      emit(MACH(dst, op[0], op[1]));
      break;
   }

   case nir_op_frcp:
      inst = emit_math(SHADER_OPCODE_RCP, dst, op[0]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fexp2:
      inst = emit_math(SHADER_OPCODE_EXP2, dst, op[0]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_flog2:
      inst = emit_math(SHADER_OPCODE_LOG2, dst, op[0]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fsin:
      inst = emit_math(SHADER_OPCODE_SIN, dst, op[0]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fcos:
      inst = emit_math(SHADER_OPCODE_COS, dst, op[0]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_idiv:
   case nir_op_udiv:
      emit_math(SHADER_OPCODE_INT_QUOTIENT, dst, op[0], op[1]);
      break;

   case nir_op_umod:
   case nir_op_irem:
      /* According to the sign table for INT DIV in the Ivy Bridge PRM, it
       * appears that our hardware just does the right thing for signed
       * remainder.
       */
      emit_math(SHADER_OPCODE_INT_REMAINDER, dst, op[0], op[1]);
      break;

   case nir_op_imod: {
      /* Get a regular C-style remainder.  If a % b == 0, set the predicate. */
      inst = emit_math(SHADER_OPCODE_INT_REMAINDER, dst, op[0], op[1]);

      /* Math instructions don't support conditional mod */
      inst = emit(MOV(dst_null_d(), src_reg(dst)));
      inst->conditional_mod = BRW_CONDITIONAL_NZ;

      /* Now, we need to determine if signs of the sources are different.
       * When we XOR the sources, the top bit is 0 if they are the same and 1
       * if they are different.  We can then use a conditional modifier to
       * turn that into a predicate.  This leads us to an XOR.l instruction.
       *
       * Technically, according to the PRM, you're not allowed to use .l on a
       * XOR instruction.  However, emperical experiments and Curro's reading
       * of the simulator source both indicate that it's safe.
       */
      src_reg tmp = src_reg(this, glsl_type::ivec4_type);
      inst = emit(XOR(dst_reg(tmp), op[0], op[1]));
      inst->predicate = BRW_PREDICATE_NORMAL;
      inst->conditional_mod = BRW_CONDITIONAL_L;

      /* If the result of the initial remainder operation is non-zero and the
       * two sources have different signs, add in a copy of op[1] to get the
       * final integer modulus value.
       */
      inst = emit(ADD(dst, src_reg(dst), op[1]));
      inst->predicate = BRW_PREDICATE_NORMAL;
      break;
   }

   case nir_op_ldexp:
      unreachable("not reached: should be handled by ldexp_to_arith()");

   case nir_op_fsqrt:
      inst = emit_math(SHADER_OPCODE_SQRT, dst, op[0]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_frsq:
      inst = emit_math(SHADER_OPCODE_RSQ, dst, op[0]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fpow:
      inst = emit_math(SHADER_OPCODE_POW, dst, op[0], op[1]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_uadd_carry: {
      struct brw_reg acc = retype(brw_acc_reg(8), BRW_REGISTER_TYPE_UD);

      emit(ADDC(dst_null_ud(), op[0], op[1]));
      emit(MOV(dst, src_reg(acc)));
      break;
   }

   case nir_op_usub_borrow: {
      struct brw_reg acc = retype(brw_acc_reg(8), BRW_REGISTER_TYPE_UD);

      emit(SUBB(dst_null_ud(), op[0], op[1]));
      emit(MOV(dst, src_reg(acc)));
      break;
   }

   case nir_op_ftrunc:
      inst = emit(RNDZ(dst, op[0]));
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fceil: {
      src_reg tmp = src_reg(this, glsl_type::float_type);
      tmp.swizzle =
         brw_swizzle_for_size(instr->src[0].src.is_ssa ?
                              instr->src[0].src.ssa->num_components :
                              instr->src[0].src.reg.reg->num_components);

      op[0].negate = !op[0].negate;
      emit(RNDD(dst_reg(tmp), op[0]));
      tmp.negate = true;
      inst = emit(MOV(dst, tmp));
      inst->saturate = instr->dest.saturate;
      break;
   }

   case nir_op_ffloor:
      inst = emit(RNDD(dst, op[0]));
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_ffract:
      inst = emit(FRC(dst, op[0]));
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fround_even:
      inst = emit(RNDE(dst, op[0]));
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fquantize2f16: {
      /* See also vec4_visitor::emit_pack_half_2x16() */
      src_reg tmp16 = src_reg(this, glsl_type::uvec4_type);
      src_reg tmp32 = src_reg(this, glsl_type::vec4_type);
      src_reg zero = src_reg(this, glsl_type::vec4_type);

      /* Check for denormal */
      src_reg abs_src0 = op[0];
      abs_src0.abs = true;
      emit(CMP(dst_null_f(), abs_src0, brw_imm_f(ldexpf(1.0, -14)),
               BRW_CONDITIONAL_L));
      /* Get the appropriately signed zero */
      emit(AND(retype(dst_reg(zero), BRW_REGISTER_TYPE_UD),
               retype(op[0], BRW_REGISTER_TYPE_UD),
               brw_imm_ud(0x80000000)));
      /* Do the actual F32 -> F16 -> F32 conversion */
      emit(F32TO16(dst_reg(tmp16), op[0]));
      emit(F16TO32(dst_reg(tmp32), tmp16));
      /* Select that or zero based on normal status */
      inst = emit(BRW_OPCODE_SEL, dst, zero, tmp32);
      inst->predicate = BRW_PREDICATE_NORMAL;
      inst->saturate = instr->dest.saturate;
      break;
   }

   case nir_op_fmin:
   case nir_op_imin:
   case nir_op_umin:
      inst = emit_minmax(BRW_CONDITIONAL_L, dst, op[0], op[1]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fmax:
   case nir_op_imax:
   case nir_op_umax:
      inst = emit_minmax(BRW_CONDITIONAL_GE, dst, op[0], op[1]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fddx:
   case nir_op_fddx_coarse:
   case nir_op_fddx_fine:
   case nir_op_fddy:
   case nir_op_fddy_coarse:
   case nir_op_fddy_fine:
      unreachable("derivatives are not valid in vertex shaders");

   case nir_op_flt:
   case nir_op_ilt:
   case nir_op_ult:
   case nir_op_fge:
   case nir_op_ige:
   case nir_op_uge:
   case nir_op_feq:
   case nir_op_ieq:
   case nir_op_fne:
   case nir_op_ine:
      emit(CMP(dst, op[0], op[1],
               brw_conditional_for_nir_comparison(instr->op)));
      break;

   case nir_op_ball_fequal2:
   case nir_op_ball_iequal2:
   case nir_op_ball_fequal3:
   case nir_op_ball_iequal3:
   case nir_op_ball_fequal4:
   case nir_op_ball_iequal4: {
      unsigned swiz =
         brw_swizzle_for_size(nir_op_infos[instr->op].input_sizes[0]);

      emit(CMP(dst_null_d(), swizzle(op[0], swiz), swizzle(op[1], swiz),
               brw_conditional_for_nir_comparison(instr->op)));
      emit(MOV(dst, brw_imm_d(0)));
      inst = emit(MOV(dst, brw_imm_d(~0)));
      inst->predicate = BRW_PREDICATE_ALIGN16_ALL4H;
      break;
   }

   case nir_op_bany_fnequal2:
   case nir_op_bany_inequal2:
   case nir_op_bany_fnequal3:
   case nir_op_bany_inequal3:
   case nir_op_bany_fnequal4:
   case nir_op_bany_inequal4: {
      unsigned swiz =
         brw_swizzle_for_size(nir_op_infos[instr->op].input_sizes[0]);

      emit(CMP(dst_null_d(), swizzle(op[0], swiz), swizzle(op[1], swiz),
               brw_conditional_for_nir_comparison(instr->op)));

      emit(MOV(dst, brw_imm_d(0)));
      inst = emit(MOV(dst, brw_imm_d(~0)));
      inst->predicate = BRW_PREDICATE_ALIGN16_ANY4H;
      break;
   }

   case nir_op_inot:
      if (devinfo->gen >= 8) {
         op[0] = resolve_source_modifiers(op[0]);
      }
      emit(NOT(dst, op[0]));
      break;

   case nir_op_ixor:
      if (devinfo->gen >= 8) {
         op[0] = resolve_source_modifiers(op[0]);
         op[1] = resolve_source_modifiers(op[1]);
      }
      emit(XOR(dst, op[0], op[1]));
      break;

   case nir_op_ior:
      if (devinfo->gen >= 8) {
         op[0] = resolve_source_modifiers(op[0]);
         op[1] = resolve_source_modifiers(op[1]);
      }
      emit(OR(dst, op[0], op[1]));
      break;

   case nir_op_iand:
      if (devinfo->gen >= 8) {
         op[0] = resolve_source_modifiers(op[0]);
         op[1] = resolve_source_modifiers(op[1]);
      }
      emit(AND(dst, op[0], op[1]));
      break;

   case nir_op_b2i:
   case nir_op_b2f:
      emit(MOV(dst, negate(op[0])));
      break;

   case nir_op_f2b:
      emit(CMP(dst, op[0], brw_imm_f(0.0f), BRW_CONDITIONAL_NZ));
      break;

   case nir_op_i2b:
      emit(CMP(dst, op[0], brw_imm_d(0), BRW_CONDITIONAL_NZ));
      break;

   case nir_op_fnoise1_1:
   case nir_op_fnoise1_2:
   case nir_op_fnoise1_3:
   case nir_op_fnoise1_4:
   case nir_op_fnoise2_1:
   case nir_op_fnoise2_2:
   case nir_op_fnoise2_3:
   case nir_op_fnoise2_4:
   case nir_op_fnoise3_1:
   case nir_op_fnoise3_2:
   case nir_op_fnoise3_3:
   case nir_op_fnoise3_4:
   case nir_op_fnoise4_1:
   case nir_op_fnoise4_2:
   case nir_op_fnoise4_3:
   case nir_op_fnoise4_4:
      unreachable("not reached: should be handled by lower_noise");

   case nir_op_unpack_half_2x16_split_x:
   case nir_op_unpack_half_2x16_split_y:
   case nir_op_pack_half_2x16_split:
      unreachable("not reached: should not occur in vertex shader");

   case nir_op_unpack_snorm_2x16:
   case nir_op_unpack_unorm_2x16:
   case nir_op_pack_snorm_2x16:
   case nir_op_pack_unorm_2x16:
      unreachable("not reached: should be handled by lower_packing_builtins");

   case nir_op_pack_uvec4_to_uint:
      unreachable("not reached");

   case nir_op_pack_uvec2_to_uint: {
      dst_reg tmp1 = dst_reg(this, glsl_type::uint_type);
      tmp1.writemask = WRITEMASK_X;
      op[0].swizzle = BRW_SWIZZLE_YYYY;
      emit(SHL(tmp1, op[0], src_reg(brw_imm_ud(16u))));

      dst_reg tmp2 = dst_reg(this, glsl_type::uint_type);
      tmp2.writemask = WRITEMASK_X;
      op[0].swizzle = BRW_SWIZZLE_XXXX;
      emit(AND(tmp2, op[0], src_reg(brw_imm_ud(0xffffu))));

      emit(OR(dst, src_reg(tmp1), src_reg(tmp2)));
      break;
   }

   case nir_op_unpack_half_2x16:
      /* As NIR does not guarantee that we have a correct swizzle outside the
       * boundaries of a vector, and the implementation of emit_unpack_half_2x16
       * uses the source operand in an operation with WRITEMASK_Y while our
       * source operand has only size 1, it accessed incorrect data producing
       * regressions in Piglit. We repeat the swizzle of the first component on the
       * rest of components to avoid regressions. In the vec4_visitor IR code path
       * this is not needed because the operand has already the correct swizzle.
       */
      op[0].swizzle = brw_compose_swizzle(BRW_SWIZZLE_XXXX, op[0].swizzle);
      emit_unpack_half_2x16(dst, op[0]);
      break;

   case nir_op_pack_half_2x16:
      emit_pack_half_2x16(dst, op[0]);
      break;

   case nir_op_unpack_unorm_4x8:
      emit_unpack_unorm_4x8(dst, op[0]);
      break;

   case nir_op_pack_unorm_4x8:
      emit_pack_unorm_4x8(dst, op[0]);
      break;

   case nir_op_unpack_snorm_4x8:
      emit_unpack_snorm_4x8(dst, op[0]);
      break;

   case nir_op_pack_snorm_4x8:
      emit_pack_snorm_4x8(dst, op[0]);
      break;

   case nir_op_bitfield_reverse:
      emit(BFREV(dst, op[0]));
      break;

   case nir_op_bit_count:
      emit(CBIT(dst, op[0]));
      break;

   case nir_op_ufind_msb:
   case nir_op_ifind_msb: {
      emit(FBH(retype(dst, BRW_REGISTER_TYPE_UD), op[0]));

      /* FBH counts from the MSB side, while GLSL's findMSB() wants the count
       * from the LSB side. If FBH didn't return an error (0xFFFFFFFF), then
       * subtract the result from 31 to convert the MSB count into an LSB count.
       */
      src_reg src(dst);
      emit(CMP(dst_null_d(), src, brw_imm_d(-1), BRW_CONDITIONAL_NZ));

      inst = emit(ADD(dst, src, brw_imm_d(31)));
      inst->predicate = BRW_PREDICATE_NORMAL;
      inst->src[0].negate = true;
      break;
   }

   case nir_op_find_lsb:
      emit(FBL(dst, op[0]));
      break;

   case nir_op_ubitfield_extract:
   case nir_op_ibitfield_extract:
      unreachable("should have been lowered");
   case nir_op_ubfe:
   case nir_op_ibfe:
      op[0] = fix_3src_operand(op[0]);
      op[1] = fix_3src_operand(op[1]);
      op[2] = fix_3src_operand(op[2]);

      emit(BFE(dst, op[2], op[1], op[0]));
      break;

   case nir_op_bfm:
      emit(BFI1(dst, op[0], op[1]));
      break;

   case nir_op_bfi:
      op[0] = fix_3src_operand(op[0]);
      op[1] = fix_3src_operand(op[1]);
      op[2] = fix_3src_operand(op[2]);

      emit(BFI2(dst, op[0], op[1], op[2]));
      break;

   case nir_op_bitfield_insert:
      unreachable("not reached: should have been lowered");

   case nir_op_fsign:
      /* AND(val, 0x80000000) gives the sign bit.
       *
       * Predicated OR ORs 1.0 (0x3f800000) with the sign bit if val is not
       * zero.
       */
      emit(CMP(dst_null_f(), op[0], brw_imm_f(0.0f), BRW_CONDITIONAL_NZ));

      op[0].type = BRW_REGISTER_TYPE_UD;
      dst.type = BRW_REGISTER_TYPE_UD;
      emit(AND(dst, op[0], brw_imm_ud(0x80000000u)));

      inst = emit(OR(dst, src_reg(dst), brw_imm_ud(0x3f800000u)));
      inst->predicate = BRW_PREDICATE_NORMAL;
      dst.type = BRW_REGISTER_TYPE_F;

      if (instr->dest.saturate) {
         inst = emit(MOV(dst, src_reg(dst)));
         inst->saturate = true;
      }
      break;

   case nir_op_isign:
      /*  ASR(val, 31) -> negative val generates 0xffffffff (signed -1).
       *               -> non-negative val generates 0x00000000.
       *  Predicated OR sets 1 if val is positive.
       */
      emit(CMP(dst_null_d(), op[0], brw_imm_d(0), BRW_CONDITIONAL_G));
      emit(ASR(dst, op[0], brw_imm_d(31)));
      inst = emit(OR(dst, src_reg(dst), brw_imm_d(1)));
      inst->predicate = BRW_PREDICATE_NORMAL;
      break;

   case nir_op_ishl:
      emit(SHL(dst, op[0], op[1]));
      break;

   case nir_op_ishr:
      emit(ASR(dst, op[0], op[1]));
      break;

   case nir_op_ushr:
      emit(SHR(dst, op[0], op[1]));
      break;

   case nir_op_ffma:
      op[0] = fix_3src_operand(op[0]);
      op[1] = fix_3src_operand(op[1]);
      op[2] = fix_3src_operand(op[2]);

      inst = emit(MAD(dst, op[2], op[1], op[0]));
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_flrp:
      inst = emit_lrp(dst, op[0], op[1], op[2]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_bcsel:
      enum brw_predicate predicate;
      if (!optimize_predicate(instr, &predicate)) {
         emit(CMP(dst_null_d(), op[0], brw_imm_d(0), BRW_CONDITIONAL_NZ));
         switch (dst.writemask) {
         case WRITEMASK_X:
            predicate = BRW_PREDICATE_ALIGN16_REPLICATE_X;
            break;
         case WRITEMASK_Y:
            predicate = BRW_PREDICATE_ALIGN16_REPLICATE_Y;
            break;
         case WRITEMASK_Z:
            predicate = BRW_PREDICATE_ALIGN16_REPLICATE_Z;
            break;
         case WRITEMASK_W:
            predicate = BRW_PREDICATE_ALIGN16_REPLICATE_W;
            break;
         default:
            predicate = BRW_PREDICATE_NORMAL;
            break;
         }
      }
      inst = emit(BRW_OPCODE_SEL, dst, op[1], op[2]);
      inst->predicate = predicate;
      break;

   case nir_op_fdot_replicated2:
      inst = emit(BRW_OPCODE_DP2, dst, op[0], op[1]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fdot_replicated3:
      inst = emit(BRW_OPCODE_DP3, dst, op[0], op[1]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fdot_replicated4:
      inst = emit(BRW_OPCODE_DP4, dst, op[0], op[1]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fdph_replicated:
      inst = emit(BRW_OPCODE_DPH, dst, op[0], op[1]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fabs:
   case nir_op_iabs:
   case nir_op_fneg:
   case nir_op_ineg:
   case nir_op_fsat:
      unreachable("not reached: should be lowered by lower_source mods");

   case nir_op_fdiv:
      unreachable("not reached: should be lowered by DIV_TO_MUL_RCP in the compiler");

   case nir_op_fmod:
      unreachable("not reached: should be lowered by MOD_TO_FLOOR in the compiler");

   case nir_op_fsub:
   case nir_op_isub:
      unreachable("not reached: should be handled by ir_sub_to_add_neg");

   default:
      unreachable("Unimplemented ALU operation");
   }

   /* If we need to do a boolean resolve, replace the result with -(x & 1)
    * to sign extend the low bit to 0/~0
    */
   if (devinfo->gen <= 5 &&
       (instr->instr.pass_flags & BRW_NIR_BOOLEAN_MASK) ==
       BRW_NIR_BOOLEAN_NEEDS_RESOLVE) {
      dst_reg masked = dst_reg(this, glsl_type::int_type);
      masked.writemask = dst.writemask;
      emit(AND(masked, src_reg(dst), brw_imm_d(1)));
      src_reg masked_neg = src_reg(masked);
      masked_neg.negate = true;
      emit(MOV(retype(dst, BRW_REGISTER_TYPE_D), masked_neg));
   }
}

void
vec4_visitor::nir_emit_jump(nir_jump_instr *instr)
{
   switch (instr->type) {
   case nir_jump_break:
      emit(BRW_OPCODE_BREAK);
      break;

   case nir_jump_continue:
      emit(BRW_OPCODE_CONTINUE);
      break;

   case nir_jump_return:
      /* fall through */
   default:
      unreachable("unknown jump");
   }
}

enum ir_texture_opcode
ir_texture_opcode_for_nir_texop(nir_texop texop)
{
   enum ir_texture_opcode op;

   switch (texop) {
   case nir_texop_lod: op = ir_lod; break;
   case nir_texop_query_levels: op = ir_query_levels; break;
   case nir_texop_texture_samples: op = ir_texture_samples; break;
   case nir_texop_tex: op = ir_tex; break;
   case nir_texop_tg4: op = ir_tg4; break;
   case nir_texop_txb: op = ir_txb; break;
   case nir_texop_txd: op = ir_txd; break;
   case nir_texop_txf: op = ir_txf; break;
   case nir_texop_txf_ms: op = ir_txf_ms; break;
   case nir_texop_txl: op = ir_txl; break;
   case nir_texop_txs: op = ir_txs; break;
   case nir_texop_samples_identical: op = ir_samples_identical; break;
   default:
      unreachable("unknown texture opcode");
   }

   return op;
}
const glsl_type *
glsl_type_for_nir_alu_type(nir_alu_type alu_type,
                           unsigned components)
{
   switch (alu_type) {
   case nir_type_float:
      return glsl_type::vec(components);
   case nir_type_int:
      return glsl_type::ivec(components);
   case nir_type_uint:
      return glsl_type::uvec(components);
   case nir_type_bool:
      return glsl_type::bvec(components);
   default:
      return glsl_type::error_type;
   }

   return glsl_type::error_type;
}

void
vec4_visitor::nir_emit_texture(nir_tex_instr *instr)
{
   unsigned texture = instr->texture_index;
   unsigned sampler = instr->sampler_index;
   src_reg texture_reg = brw_imm_ud(texture);
   src_reg sampler_reg = brw_imm_ud(sampler);
   src_reg coordinate;
   const glsl_type *coord_type = NULL;
   src_reg shadow_comparitor;
   src_reg offset_value;
   src_reg lod, lod2;
   src_reg sample_index;
   src_reg mcs;

   const glsl_type *dest_type =
      glsl_type_for_nir_alu_type(instr->dest_type,
                                 nir_tex_instr_dest_size(instr));
   dst_reg dest = get_nir_dest(instr->dest, instr->dest_type);

   /* The hardware requires a LOD for buffer textures */
   if (instr->sampler_dim == GLSL_SAMPLER_DIM_BUF)
      lod = brw_imm_d(0);

   /* Load the texture operation sources */
   uint32_t constant_offset = 0;
   for (unsigned i = 0; i < instr->num_srcs; i++) {
      switch (instr->src[i].src_type) {
      case nir_tex_src_comparitor:
         shadow_comparitor = get_nir_src(instr->src[i].src,
                                         BRW_REGISTER_TYPE_F, 1);
         break;

      case nir_tex_src_coord: {
         unsigned src_size = nir_tex_instr_src_size(instr, i);

         switch (instr->op) {
         case nir_texop_txf:
         case nir_texop_txf_ms:
         case nir_texop_samples_identical:
            coordinate = get_nir_src(instr->src[i].src, BRW_REGISTER_TYPE_D,
                                     src_size);
            coord_type = glsl_type::ivec(src_size);
            break;

         default:
            coordinate = get_nir_src(instr->src[i].src, BRW_REGISTER_TYPE_F,
                                     src_size);
            coord_type = glsl_type::vec(src_size);
            break;
         }
         break;
      }

      case nir_tex_src_ddx:
         lod = get_nir_src(instr->src[i].src, BRW_REGISTER_TYPE_F,
                           nir_tex_instr_src_size(instr, i));
         break;

      case nir_tex_src_ddy:
         lod2 = get_nir_src(instr->src[i].src, BRW_REGISTER_TYPE_F,
                           nir_tex_instr_src_size(instr, i));
         break;

      case nir_tex_src_lod:
         switch (instr->op) {
         case nir_texop_txs:
         case nir_texop_txf:
            lod = get_nir_src(instr->src[i].src, BRW_REGISTER_TYPE_D, 1);
            break;

         default:
            lod = get_nir_src(instr->src[i].src, BRW_REGISTER_TYPE_F, 1);
            break;
         }
         break;

      case nir_tex_src_ms_index: {
         sample_index = get_nir_src(instr->src[i].src, BRW_REGISTER_TYPE_D, 1);
         break;
      }

      case nir_tex_src_offset: {
         nir_const_value *const_offset =
            nir_src_as_const_value(instr->src[i].src);
         if (const_offset) {
            constant_offset = brw_texture_offset(const_offset->i32, 3);
         } else {
            offset_value =
               get_nir_src(instr->src[i].src, BRW_REGISTER_TYPE_D, 2);
         }
         break;
      }

      case nir_tex_src_texture_offset: {
         /* The highest texture which may be used by this operation is
          * the last element of the array. Mark it here, because the generator
          * doesn't have enough information to determine the bound.
          */
         uint32_t array_size = instr->texture_array_size;
         uint32_t max_used = texture + array_size - 1;
         if (instr->op == nir_texop_tg4) {
            max_used += prog_data->base.binding_table.gather_texture_start;
         } else {
            max_used += prog_data->base.binding_table.texture_start;
         }

         brw_mark_surface_used(&prog_data->base, max_used);

         /* Emit code to evaluate the actual indexing expression */
         src_reg src = get_nir_src(instr->src[i].src, 1);
         src_reg temp(this, glsl_type::uint_type);
         emit(ADD(dst_reg(temp), src, brw_imm_ud(texture)));
         texture_reg = emit_uniformize(temp);
         break;
      }

      case nir_tex_src_sampler_offset: {
         /* Emit code to evaluate the actual indexing expression */
         src_reg src = get_nir_src(instr->src[i].src, 1);
         src_reg temp(this, glsl_type::uint_type);
         emit(ADD(dst_reg(temp), src, brw_imm_ud(sampler)));
         sampler_reg = emit_uniformize(temp);
         break;
      }

      case nir_tex_src_projector:
         unreachable("Should be lowered by do_lower_texture_projection");

      case nir_tex_src_bias:
         unreachable("LOD bias is not valid for vertex shaders.\n");

      default:
         unreachable("unknown texture source");
      }
   }

   if (instr->op == nir_texop_txf_ms ||
       instr->op == nir_texop_samples_identical) {
      assert(coord_type != NULL);
      if (devinfo->gen >= 7 &&
          key_tex->compressed_multisample_layout_mask & (1 << texture)) {
         mcs = emit_mcs_fetch(coord_type, coordinate, texture_reg);
      } else {
         mcs = brw_imm_ud(0u);
      }
   }

   /* Stuff the channel select bits in the top of the texture offset */
   if (instr->op == nir_texop_tg4) {
      if (instr->component == 1 &&
          (key_tex->gather_channel_quirk_mask & (1 << texture))) {
         /* gather4 sampler is broken for green channel on RG32F --
          * we must ask for blue instead.
          */
         constant_offset |= 2 << 16;
      } else {
         constant_offset |= instr->component << 16;
      }
   }

   ir_texture_opcode op = ir_texture_opcode_for_nir_texop(instr->op);

   bool is_cube_array =
      instr->op == nir_texop_txs &&
      instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE &&
      instr->is_array;

   emit_texture(op, dest, dest_type, coordinate, instr->coord_components,
                shadow_comparitor,
                lod, lod2, sample_index,
                constant_offset, offset_value,
                mcs, is_cube_array,
                texture, texture_reg, sampler, sampler_reg);
}

void
vec4_visitor::nir_emit_undef(nir_ssa_undef_instr *instr)
{
   nir_ssa_values[instr->def.index] = dst_reg(VGRF, alloc.allocate(1));
}

}
