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
#include "glsl/ir_uniform.h"

namespace brw {

void
vec4_visitor::emit_nir_code()
{
   nir_shader *nir = prog->nir;

   if (nir->num_inputs > 0)
      nir_setup_inputs(nir);

   if (nir->num_uniforms > 0)
      nir_setup_uniforms(nir);

   nir_setup_system_values(nir);

   /* get the main function and emit it */
   nir_foreach_overload(nir, overload) {
      assert(strcmp(overload->function->name, "main") == 0);
      assert(overload->impl);
      nir_emit_impl(overload->impl);
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
      reg = &this->nir_system_values[SYSTEM_VALUE_VERTEX_ID_ZERO_BASE];
      if (reg->file == BAD_FILE)
         *reg =
            *this->make_reg_for_system_value(SYSTEM_VALUE_VERTEX_ID_ZERO_BASE,
                                             glsl_type::int_type);
      break;

   case nir_intrinsic_load_base_vertex:
      reg = &this->nir_system_values[SYSTEM_VALUE_BASE_VERTEX];
      if (reg->file == BAD_FILE)
         *reg = *this->make_reg_for_system_value(SYSTEM_VALUE_BASE_VERTEX,
                                                 glsl_type::int_type);
      break;

   case nir_intrinsic_load_instance_id:
      reg = &this->nir_system_values[SYSTEM_VALUE_INSTANCE_ID];
      if (reg->file == BAD_FILE)
         *reg = *this->make_reg_for_system_value(SYSTEM_VALUE_INSTANCE_ID,
                                                 glsl_type::int_type);
      break;

   default:
      break;
   }
}

static bool
setup_system_values_block(nir_block *block, void *void_visitor)
{
   vec4_visitor *v = (vec4_visitor *)void_visitor;

   nir_foreach_instr(block, instr) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      v->nir_setup_system_value_intrinsic(intrin);
   }

   return true;
}

void
vec4_visitor::nir_setup_system_values(nir_shader *shader)
{
   nir_system_values = ralloc_array(mem_ctx, dst_reg, SYSTEM_VALUE_MAX);

   nir_foreach_overload(shader, overload) {
      assert(strcmp(overload->function->name, "main") == 0);
      assert(overload->impl);
      nir_foreach_block(overload->impl, setup_system_values_block, this);
   }
}

void
vec4_visitor::nir_setup_inputs(nir_shader *shader)
{
   nir_inputs = ralloc_array(mem_ctx, src_reg, shader->num_inputs);

   foreach_list_typed(nir_variable, var, node, &shader->inputs) {
      int offset = var->data.driver_location;
      unsigned size = type_size(var->type);
      for (unsigned i = 0; i < size; i++) {
         src_reg src = src_reg(ATTR, var->data.location + i, var->type);
         nir_inputs[offset + i] = src;
      }
   }
}

void
vec4_visitor::nir_setup_uniforms(nir_shader *shader)
{
   uniforms = 0;

   nir_uniform_driver_location =
      rzalloc_array(mem_ctx, unsigned, this->uniform_array_size);

   if (shader_prog) {
      foreach_list_typed(nir_variable, var, node, &shader->uniforms) {
         /* UBO's, atomics and samplers don't take up space in the
            uniform file */
         if (var->interface_type != NULL || var->type->contains_atomic() ||
             type_size(var->type) == 0) {
            continue;
         }

         assert(uniforms < uniform_array_size);
         this->uniform_size[uniforms] = type_size(var->type);

         if (strncmp(var->name, "gl_", 3) == 0)
            nir_setup_builtin_uniform(var);
         else
            nir_setup_uniform(var);
      }
   } else {
      /* For ARB_vertex_program, only a single "parameters" variable is
       * generated to support uniform data.
       */
      nir_variable *var = (nir_variable *) shader->uniforms.get_head();
      assert(shader->uniforms.length() == 1 &&
             strcmp(var->name, "parameters") == 0);

      assert(uniforms < uniform_array_size);
      this->uniform_size[uniforms] = type_size(var->type);

      struct gl_program_parameter_list *plist = prog->Parameters;
      for (unsigned p = 0; p < plist->NumParameters; p++) {
         uniform_vector_size[uniforms] = plist->Parameters[p].Size;

         /* Parameters should be either vec4 uniforms or single component
          * constants; matrices and other larger types should have been broken
          * down earlier.
          */
         assert(uniform_vector_size[uniforms] <= 4);

         int i;
         for (i = 0; i < uniform_vector_size[uniforms]; i++) {
            stage_prog_data->param[uniforms * 4 + i] = &plist->ParameterValues[p][i];
         }
         for (; i < 4; i++) {
            static const gl_constant_value zero = { 0.0 };
            stage_prog_data->param[uniforms * 4 + i] = &zero;
         }

         nir_uniform_driver_location[uniforms] = var->data.driver_location;
         uniforms++;
      }
   }
}

void
vec4_visitor::nir_setup_uniform(nir_variable *var)
{
   int namelen = strlen(var->name);

   /* The data for our (non-builtin) uniforms is stored in a series of
    * gl_uniform_driver_storage structs for each subcomponent that
    * glGetUniformLocation() could name.  We know it's been set up in the same
    * order we'd walk the type, so walk the list of storage and find anything
    * with our name, or the prefix of a component that starts with our name.
    */
    for (unsigned u = 0; u < shader_prog->NumUniformStorage; u++) {
       struct gl_uniform_storage *storage = &shader_prog->UniformStorage[u];

       if (storage->builtin)
          continue;

       if (strncmp(var->name, storage->name, namelen) != 0 ||
           (storage->name[namelen] != 0 &&
            storage->name[namelen] != '.' &&
            storage->name[namelen] != '[')) {
          continue;
       }

       gl_constant_value *components = storage->storage;
       unsigned vector_count = (MAX2(storage->array_elements, 1) *
                                storage->type->matrix_columns);

       for (unsigned s = 0; s < vector_count; s++) {
          assert(uniforms < uniform_array_size);
          uniform_vector_size[uniforms] = storage->type->vector_elements;

          int i;
          for (i = 0; i < uniform_vector_size[uniforms]; i++) {
             stage_prog_data->param[uniforms * 4 + i] = components;
             components++;
          }
          for (; i < 4; i++) {
             static const gl_constant_value zero = { 0.0 };
             stage_prog_data->param[uniforms * 4 + i] = &zero;
          }

          nir_uniform_driver_location[uniforms] = var->data.driver_location;
          uniforms++;
       }
    }
}

void
vec4_visitor::nir_setup_builtin_uniform(nir_variable *var)
{
   const nir_state_slot *const slots = var->state_slots;
   assert(var->state_slots != NULL);

   for (unsigned int i = 0; i < var->num_state_slots; i++) {
      /* This state reference has already been setup by ir_to_mesa,
       * but we'll get the same index back here.  We can reference
       * ParameterValues directly, since unlike brw_fs.cpp, we never
       * add new state references during compile.
       */
      int index = _mesa_add_state_reference(this->prog->Parameters,
					    (gl_state_index *)slots[i].tokens);
      gl_constant_value *values =
         &this->prog->Parameters->ParameterValues[index][0];

      assert(uniforms < uniform_array_size);

      for (unsigned j = 0; j < 4; j++)
         stage_prog_data->param[uniforms * 4 + j] =
            &values[GET_SWZ(slots[i].swizzle, j)];

      this->uniform_vector_size[uniforms] =
         (var->type->is_scalar() || var->type->is_vector() ||
          var->type->is_matrix() ? var->type->vector_elements : 4);

      nir_uniform_driver_location[uniforms] = var->data.driver_location;
      uniforms++;
   }
}

void
vec4_visitor::nir_emit_impl(nir_function_impl *impl)
{
   nir_locals = ralloc_array(mem_ctx, dst_reg, impl->reg_alloc);

   foreach_list_typed(nir_register, reg, node, &impl->registers) {
      unsigned array_elems =
         reg->num_array_elems == 0 ? 1 : reg->num_array_elems;

      nir_locals[reg->index] = dst_reg(GRF, alloc.allocate(array_elems));
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

   emit(IF(BRW_PREDICATE_NORMAL));

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
   nir_foreach_instr(block, instr) {
      nir_emit_instr(instr);
   }
}

void
vec4_visitor::nir_emit_instr(nir_instr *instr)
{
   this->base_ir = instr;

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
vec4_visitor::get_nir_dest(nir_dest dest)
{
   assert(!dest.is_ssa);
   return dst_reg_for_nir_reg(this, dest.reg.reg, dest.reg.base_offset,
                              dest.reg.indirect);
}

dst_reg
vec4_visitor::get_nir_dest(nir_dest dest, enum brw_reg_type type)
{
   return retype(get_nir_dest(dest), type);
}

dst_reg
vec4_visitor::get_nir_dest(nir_dest dest, nir_alu_type type)
{
   return get_nir_dest(dest, brw_type_for_nir_type(type));
}

src_reg
vec4_visitor::get_nir_src(nir_src src, enum brw_reg_type type,
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
vec4_visitor::get_nir_src(nir_src src, nir_alu_type type,
                          unsigned num_components)
{
   return get_nir_src(src, brw_type_for_nir_type(type), num_components);
}

src_reg
vec4_visitor::get_nir_src(nir_src src, unsigned num_components)
{
   /* if type is not specified, default to signed int */
   return get_nir_src(src, nir_type_int, num_components);
}

void
vec4_visitor::nir_emit_load_const(nir_load_const_instr *instr)
{
   dst_reg reg = dst_reg(GRF, alloc.allocate(1));
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
         if (instr->value.u[i] == instr->value.u[j]) {
            writemask |= 1 << j;
         }
      }

      reg.writemask = writemask;
      emit(MOV(reg, src_reg(instr->value.i[i])));

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

   bool has_indirect = false;

   switch (instr->intrinsic) {

   case nir_intrinsic_load_input_indirect:
      has_indirect = true;
      /* fallthrough */
   case nir_intrinsic_load_input: {
      int offset = instr->const_index[0];
      src = nir_inputs[offset];

      if (has_indirect) {
         dest.reladdr = new(mem_ctx) src_reg(get_nir_src(instr->src[0],
                                                         BRW_REGISTER_TYPE_D,
                                                         1));
      }
      dest = get_nir_dest(instr->dest, src.type);
      dest.writemask = brw_writemask_for_size(instr->num_components);

      emit(MOV(dest, src));
      break;
   }

   case nir_intrinsic_store_output_indirect:
      has_indirect = true;
      /* fallthrough */
   case nir_intrinsic_store_output: {
      int varying = instr->const_index[0];

      src = get_nir_src(instr->src[0], BRW_REGISTER_TYPE_F,
                        instr->num_components);
      dest = dst_reg(src);

      if (has_indirect) {
         dest.reladdr = new(mem_ctx) src_reg(get_nir_src(instr->src[1],
                                                         BRW_REGISTER_TYPE_D,
                                                         1));
      }
      output_reg[varying] = dest;
      break;
   }

   case nir_intrinsic_load_vertex_id:
      unreachable("should be lowered by lower_vertex_id()");

   case nir_intrinsic_load_vertex_id_zero_base: {
      src_reg vertex_id =
         src_reg(nir_system_values[SYSTEM_VALUE_VERTEX_ID_ZERO_BASE]);
      assert(vertex_id.file != BAD_FILE);
      dest = get_nir_dest(instr->dest, vertex_id.type);
      emit(MOV(dest, vertex_id));
      break;
   }

   case nir_intrinsic_load_base_vertex: {
      src_reg base_vertex =
         src_reg(nir_system_values[SYSTEM_VALUE_BASE_VERTEX]);
      assert(base_vertex.file != BAD_FILE);
      dest = get_nir_dest(instr->dest, base_vertex.type);
      emit(MOV(dest, base_vertex));
      break;
   }

   case nir_intrinsic_load_instance_id: {
      src_reg instance_id =
         src_reg(nir_system_values[SYSTEM_VALUE_INSTANCE_ID]);
      assert(instance_id.file != BAD_FILE);
      dest = get_nir_dest(instr->dest, instance_id.type);
      emit(MOV(dest, instance_id));
      break;
   }

   case nir_intrinsic_load_uniform_indirect:
      has_indirect = true;
      /* fallthrough */
   case nir_intrinsic_load_uniform: {
      int uniform = instr->const_index[0];

      dest = get_nir_dest(instr->dest);

      if (has_indirect) {
         /* Split addressing into uniform and offset */
         int offset = uniform - nir_uniform_driver_location[uniform];
         assert(offset >= 0);

         uniform -= offset;
         assert(uniform >= 0);

         src = src_reg(dst_reg(UNIFORM, uniform));
         src.reg_offset = offset;
         src_reg tmp = get_nir_src(instr->src[0], BRW_REGISTER_TYPE_D, 1);
         src.reladdr = new(mem_ctx) src_reg(tmp);
      } else {
         src = src_reg(dst_reg(UNIFORM, uniform));
      }

      emit(MOV(dest, src));
      break;
   }

   case nir_intrinsic_atomic_counter_read:
   case nir_intrinsic_atomic_counter_inc:
   case nir_intrinsic_atomic_counter_dec: {
      unsigned surf_index = prog_data->base.binding_table.abo_start +
         (unsigned) instr->const_index[0];
      src_reg offset = get_nir_src(instr->src[0], nir_type_int,
                                   instr->num_components);
      dest = get_nir_dest(instr->dest);

      switch (instr->intrinsic) {
         case nir_intrinsic_atomic_counter_inc:
            emit_untyped_atomic(BRW_AOP_INC, surf_index, dest, offset,
                                src_reg(), src_reg());
            break;
         case nir_intrinsic_atomic_counter_dec:
            emit_untyped_atomic(BRW_AOP_PREDEC, surf_index, dest, offset,
                                src_reg(), src_reg());
            break;
         case nir_intrinsic_atomic_counter_read:
            emit_untyped_surface_read(surf_index, dest, offset);
            break;
         default:
            unreachable("Unreachable");
      }

      brw_mark_surface_used(stage_prog_data, surf_index);
      break;
   }

   case nir_intrinsic_load_ubo_indirect:
      has_indirect = true;
      /* fallthrough */
   case nir_intrinsic_load_ubo: {
      nir_const_value *const_block_index = nir_src_as_const_value(instr->src[0]);
      src_reg surf_index;

      dest = get_nir_dest(instr->dest);

      if (const_block_index) {
         /* The block index is a constant, so just emit the binding table entry
          * as an immediate.
          */
         surf_index = src_reg(prog_data->base.binding_table.ubo_start +
                              const_block_index->u[0]);
      } else {
         /* The block index is not a constant. Evaluate the index expression
          * per-channel and add the base UBO index; we have to select a value
          * from any live channel.
          */
         surf_index = src_reg(this, glsl_type::uint_type);
         emit(ADD(dst_reg(surf_index), get_nir_src(instr->src[0], nir_type_int,
                                                   instr->num_components),
                  src_reg(prog_data->base.binding_table.ubo_start)));
         surf_index = emit_uniformize(surf_index);

         /* Assume this may touch any UBO. It would be nice to provide
          * a tighter bound, but the array information is already lowered away.
          */
         brw_mark_surface_used(&prog_data->base,
                               prog_data->base.binding_table.ubo_start +
                               shader_prog->NumUniformBlocks - 1);
      }

      unsigned const_offset = instr->const_index[0];
      src_reg offset;

      if (!has_indirect)  {
         offset = src_reg(const_offset / 16);
      } else {
         offset = src_reg(this, glsl_type::uint_type);
         emit(SHR(dst_reg(offset), get_nir_src(instr->src[1], nir_type_int, 1),
                  src_reg(4u)));
      }

      src_reg packed_consts = src_reg(this, glsl_type::vec4_type);
      packed_consts.type = dest.type;

      emit_pull_constant_load_reg(dst_reg(packed_consts),
                                  surf_index,
                                  offset,
                                  NULL, NULL /* before_block/inst */);

      packed_consts.swizzle = brw_swizzle_for_size(instr->num_components);
      packed_consts.swizzle += BRW_SWIZZLE4(const_offset % 16 / 4,
                                            const_offset % 16 / 4,
                                            const_offset % 16 / 4,
                                            const_offset % 16 / 4);

      emit(MOV(dest, packed_consts));
      break;
   }

   default:
      unreachable("Unknown intrinsic");
   }
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
         if (value0 && value0->u[0] < (1 << 16)) {
            if (devinfo->gen < 7)
               emit(MUL(dst, op[0], op[1]));
            else
               emit(MUL(dst, op[1], op[0]));
         } else if (value1 && value1->u[0] < (1 << 16)) {
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
      emit_math(SHADER_OPCODE_INT_REMAINDER, dst, op[0], op[1]);
      break;

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
      dst_reg tmp = dst_reg(this, glsl_type::bool_type);

      switch (instr->op) {
      case nir_op_ball_fequal2:
      case nir_op_ball_iequal2:
         tmp.writemask = WRITEMASK_XY;
         break;
      case nir_op_ball_fequal3:
      case nir_op_ball_iequal3:
         tmp.writemask = WRITEMASK_XYZ;
         break;
      case nir_op_ball_fequal4:
      case nir_op_ball_iequal4:
         tmp.writemask = WRITEMASK_XYZW;
         break;
      default:
         unreachable("not reached");
      }

      emit(CMP(tmp, op[0], op[1],
               brw_conditional_for_nir_comparison(instr->op)));
      emit(MOV(dst, src_reg(0)));
      inst = emit(MOV(dst, src_reg(~0)));
      inst->predicate = BRW_PREDICATE_ALIGN16_ALL4H;
      break;
   }

   case nir_op_bany_fnequal2:
   case nir_op_bany_inequal2:
   case nir_op_bany_fnequal3:
   case nir_op_bany_inequal3:
   case nir_op_bany_fnequal4:
   case nir_op_bany_inequal4: {
      dst_reg tmp = dst_reg(this, glsl_type::bool_type);

      switch (instr->op) {
      case nir_op_bany_fnequal2:
      case nir_op_bany_inequal2:
         tmp.writemask = WRITEMASK_XY;
         break;
      case nir_op_bany_fnequal3:
      case nir_op_bany_inequal3:
         tmp.writemask = WRITEMASK_XYZ;
         break;
      case nir_op_bany_fnequal4:
      case nir_op_bany_inequal4:
         tmp.writemask = WRITEMASK_XYZW;
         break;
      default:
         unreachable("not reached");
      }

      emit(CMP(tmp, op[0], op[1],
               brw_conditional_for_nir_comparison(instr->op)));

      emit(MOV(dst, src_reg(0)));
      inst = emit(MOV(dst, src_reg(~0)));
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
      emit(AND(dst, op[0], src_reg(1)));
      break;

   case nir_op_b2f:
      op[0].type = BRW_REGISTER_TYPE_D;
      dst.type = BRW_REGISTER_TYPE_D;
      emit(AND(dst, op[0], src_reg(0x3f800000u)));
      dst.type = BRW_REGISTER_TYPE_F;
      break;

   case nir_op_f2b:
      emit(CMP(dst, op[0], src_reg(0.0f), BRW_CONDITIONAL_NZ));
      break;

   case nir_op_i2b:
      emit(CMP(dst, op[0], src_reg(0), BRW_CONDITIONAL_NZ));
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
      src_reg temp = src_reg(this, glsl_type::uint_type);

      inst = emit(FBH(dst_reg(temp), op[0]));
      inst->dst.writemask = WRITEMASK_XYZW;

      /* FBH counts from the MSB side, while GLSL's findMSB() wants the count
       * from the LSB side. If FBH didn't return an error (0xFFFFFFFF), then
       * subtract the result from 31 to convert the MSB count into an LSB count.
       */

      /* FBH only supports UD type for dst, so use a MOV to convert UD to D. */
      temp.swizzle = BRW_SWIZZLE_NOOP;
      emit(MOV(dst, temp));

      src_reg src_tmp = src_reg(dst);
      emit(CMP(dst_null_d(), src_tmp, src_reg(-1), BRW_CONDITIONAL_NZ));

      src_tmp.negate = true;
      inst = emit(ADD(dst, src_tmp, src_reg(31)));
      inst->predicate = BRW_PREDICATE_NORMAL;
      break;
   }

   case nir_op_find_lsb:
      emit(FBL(dst, op[0]));
      break;

   case nir_op_ubitfield_extract:
   case nir_op_ibitfield_extract:
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
      unreachable("not reached: should be handled by "
                  "lower_instructions::bitfield_insert_to_bfm_bfi");

   case nir_op_fsign:
      /* AND(val, 0x80000000) gives the sign bit.
       *
       * Predicated OR ORs 1.0 (0x3f800000) with the sign bit if val is not
       * zero.
       */
      emit(CMP(dst_null_f(), op[0], src_reg(0.0f), BRW_CONDITIONAL_NZ));

      op[0].type = BRW_REGISTER_TYPE_UD;
      dst.type = BRW_REGISTER_TYPE_UD;
      emit(AND(dst, op[0], src_reg(0x80000000u)));

      inst = emit(OR(dst, src_reg(dst), src_reg(0x3f800000u)));
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
      emit(CMP(dst_null_d(), op[0], src_reg(0), BRW_CONDITIONAL_G));
      emit(ASR(dst, op[0], src_reg(31)));
      inst = emit(OR(dst, src_reg(dst), src_reg(1)));
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
      emit(CMP(dst_null_d(), op[0], src_reg(0), BRW_CONDITIONAL_NZ));
      inst = emit(BRW_OPCODE_SEL, dst, op[1], op[2]);
      inst->predicate = BRW_PREDICATE_NORMAL;
      break;

   case nir_op_fdot2:
      inst = emit(BRW_OPCODE_DP2, dst, op[0], op[1]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fdot3:
      inst = emit(BRW_OPCODE_DP3, dst, op[0], op[1]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fdot4:
      inst = emit(BRW_OPCODE_DP4, dst, op[0], op[1]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_bany2:
   case nir_op_bany3:
   case nir_op_bany4: {
      dst_reg tmp = dst_reg(this, glsl_type::bool_type);
      tmp.writemask = brw_writemask_for_size(nir_op_infos[instr->op].input_sizes[0]);

      emit(CMP(tmp, op[0], src_reg(0), BRW_CONDITIONAL_NZ));

      emit(MOV(dst, src_reg(0)));
      inst = emit(MOV(dst, src_reg(~0)));
      inst->predicate = BRW_PREDICATE_ALIGN16_ANY4H;
      break;
   }

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
      emit(AND(masked, src_reg(dst), src_reg(1)));
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
   case nir_texop_tex: op = ir_tex; break;
   case nir_texop_tg4: op = ir_tg4; break;
   case nir_texop_txb: op = ir_txb; break;
   case nir_texop_txd: op = ir_txd; break;
   case nir_texop_txf: op = ir_txf; break;
   case nir_texop_txf_ms: op = ir_txf_ms; break;
   case nir_texop_txl: op = ir_txl; break;
   case nir_texop_txs: op = ir_txs; break;
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
   case nir_type_unsigned:
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
   unsigned sampler = instr->sampler_index;
   src_reg sampler_reg = src_reg(sampler);
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

   /* When tg4 is used with the degenerate ZERO/ONE swizzles, don't bother
    * emitting anything other than setting up the constant result.
    */
   if (instr->op == nir_texop_tg4) {
      int swiz = GET_SWZ(key->tex.swizzles[sampler], instr->component);
      if (swiz == SWIZZLE_ZERO || swiz == SWIZZLE_ONE) {
         emit(MOV(dest, src_reg(swiz == SWIZZLE_ONE ? 1.0f : 0.0f)));
         return;
      }
   }

   /* Load the texture operation sources */
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
         assert(coord_type != NULL);
         if (devinfo->gen >= 7 &&
             key->tex.compressed_multisample_layout_mask & (1<<sampler)) {
            mcs = emit_mcs_fetch(coord_type, coordinate, sampler_reg);
         } else {
            mcs = src_reg(0u);
         }
         mcs = retype(mcs, BRW_REGISTER_TYPE_UD);
         break;
      }

      case nir_tex_src_offset:
         offset_value = get_nir_src(instr->src[i].src, BRW_REGISTER_TYPE_D, 2);
         break;

      case nir_tex_src_sampler_offset: {
         /* The highest sampler which may be used by this operation is
          * the last element of the array. Mark it here, because the generator
          * doesn't have enough information to determine the bound.
          */
         uint32_t array_size = instr->sampler_array_size;
         uint32_t max_used = sampler + array_size - 1;
         if (instr->op == nir_texop_tg4) {
            max_used += prog_data->base.binding_table.gather_texture_start;
         } else {
            max_used += prog_data->base.binding_table.texture_start;
         }

         brw_mark_surface_used(&prog_data->base, max_used);

         /* Emit code to evaluate the actual indexing expression */
         src_reg src = get_nir_src(instr->src[i].src, 1);
         src_reg temp(this, glsl_type::uint_type);
         emit(ADD(dst_reg(temp), src, src_reg(sampler)));
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

   uint32_t constant_offset = 0;
   for (unsigned i = 0; i < 3; i++) {
      if (instr->const_offset[i] != 0) {
         constant_offset = brw_texture_offset(instr->const_offset, 3);
         break;
      }
   }

   /* Stuff the channel select bits in the top of the texture offset */
   if (instr->op == nir_texop_tg4)
      constant_offset |= gather_channel(instr->component, sampler) << 16;

   ir_texture_opcode op = ir_texture_opcode_for_nir_texop(instr->op);

   bool is_cube_array =
      instr->op == nir_texop_txs &&
      instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE &&
      instr->is_array;

   emit_texture(op, dest, dest_type, coordinate, instr->coord_components,
                shadow_comparitor,
                lod, lod2, sample_index,
                constant_offset, offset_value,
                mcs, is_cube_array, sampler, sampler_reg);
}

}
