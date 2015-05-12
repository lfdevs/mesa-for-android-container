/*
 * Copyright © 2010 Intel Corporation
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

#include "glsl/ir.h"
#include "glsl/ir_optimization.h"
#include "glsl/nir/glsl_to_nir.h"
#include "brw_fs.h"

static void
nir_optimize(nir_shader *nir)
{
   bool progress;
   do {
      progress = false;
      nir_lower_vars_to_ssa(nir);
      nir_validate_shader(nir);
      nir_lower_alu_to_scalar(nir);
      nir_validate_shader(nir);
      progress |= nir_copy_prop(nir);
      nir_validate_shader(nir);
      nir_lower_phis_to_scalar(nir);
      nir_validate_shader(nir);
      progress |= nir_copy_prop(nir);
      nir_validate_shader(nir);
      progress |= nir_opt_dce(nir);
      nir_validate_shader(nir);
      progress |= nir_opt_cse(nir);
      nir_validate_shader(nir);
      progress |= nir_opt_peephole_select(nir);
      nir_validate_shader(nir);
      progress |= nir_opt_algebraic(nir);
      nir_validate_shader(nir);
      progress |= nir_opt_constant_folding(nir);
      nir_validate_shader(nir);
      progress |= nir_opt_remove_phis(nir);
      nir_validate_shader(nir);
   } while (progress);
}

static bool
count_nir_instrs_in_block(nir_block *block, void *state)
{
   int *count = (int *) state;
   nir_foreach_instr(block, instr) {
      *count = *count + 1;
   }
   return true;
}

static int
count_nir_instrs(nir_shader *nir)
{
   int count = 0;
   nir_foreach_overload(nir, overload) {
      if (!overload->impl)
         continue;
      nir_foreach_block(overload->impl, count_nir_instrs_in_block, &count);
   }
   return count;
}

void
fs_visitor::emit_nir_code()
{
   /* first, lower the GLSL IR shader to NIR */
   lower_output_reads(shader->base.ir);
   nir_shader *nir = glsl_to_nir(shader->base.ir, NULL, true);
   nir_validate_shader(nir);

   nir_lower_global_vars_to_local(nir);
   nir_validate_shader(nir);

   nir_split_var_copies(nir);
   nir_validate_shader(nir);

   nir_optimize(nir);

   /* Lower a bunch of stuff */
   nir_lower_var_copies(nir);
   nir_validate_shader(nir);

   nir_lower_io(nir);
   nir_validate_shader(nir);

   nir_lower_locals_to_regs(nir);
   nir_validate_shader(nir);

   nir_remove_dead_variables(nir);
   nir_validate_shader(nir);

   nir_lower_samplers(nir, shader_prog, shader->base.Program);
   nir_validate_shader(nir);

   nir_lower_system_values(nir);
   nir_validate_shader(nir);

   nir_lower_atomics(nir);
   nir_validate_shader(nir);

   nir_optimize(nir);

   nir_lower_to_source_mods(nir);
   nir_validate_shader(nir);
   nir_copy_prop(nir);
   nir_validate_shader(nir);

   if (INTEL_DEBUG & DEBUG_WM) {
      fprintf(stderr, "NIR (SSA form) for fragment shader:\n");
      nir_print_shader(nir, stderr);
   }

   if (dispatch_width == 8) {
      static GLuint msg_id = 0;
      _mesa_gl_debug(&brw->ctx, &msg_id,
                     MESA_DEBUG_SOURCE_SHADER_COMPILER,
                     MESA_DEBUG_TYPE_OTHER,
                     MESA_DEBUG_SEVERITY_NOTIFICATION,
                     "FS NIR shader: %d inst\n",
                     count_nir_instrs(nir));
   }

   nir_convert_from_ssa(nir);
   nir_validate_shader(nir);

   /* emit the arrays used for inputs and outputs - load/store intrinsics will
    * be converted to reads/writes of these arrays
    */

   if (nir->num_inputs > 0) {
      nir_inputs = vgrf(nir->num_inputs);
      nir_setup_inputs(nir);
   }

   if (nir->num_outputs > 0) {
      nir_outputs = vgrf(nir->num_outputs);
      nir_setup_outputs(nir);
   }

   if (nir->num_uniforms > 0) {
      nir_uniforms = fs_reg(UNIFORM, 0);
      nir_setup_uniforms(nir);
   }

   nir_emit_system_values(nir);

   nir_globals = ralloc_array(mem_ctx, fs_reg, nir->reg_alloc);
   foreach_list_typed(nir_register, reg, node, &nir->registers) {
      unsigned array_elems =
         reg->num_array_elems == 0 ? 1 : reg->num_array_elems;
      unsigned size = array_elems * reg->num_components;
      nir_globals[reg->index] = vgrf(size);
   }

   /* get the main function and emit it */
   nir_foreach_overload(nir, overload) {
      assert(strcmp(overload->function->name, "main") == 0);
      assert(overload->impl);
      nir_emit_impl(overload->impl);
   }

   if (INTEL_DEBUG & DEBUG_WM) {
      fprintf(stderr, "NIR (final form) for fragment shader:\n");
      nir_print_shader(nir, stderr);
   }

   ralloc_free(nir);
}

void
fs_visitor::nir_setup_inputs(nir_shader *shader)
{
   struct hash_entry *entry;
   hash_table_foreach(shader->inputs, entry) {
      nir_variable *var = (nir_variable *) entry->data;
      fs_reg varying = offset(nir_inputs, var->data.driver_location);

      fs_reg reg;
      if (!strcmp(var->name, "gl_FragCoord")) {
         reg = *emit_fragcoord_interpolation(var->data.pixel_center_integer,
                                             var->data.origin_upper_left);
         emit_percomp(MOV(varying, reg), 0xF);
      } else if (!strcmp(var->name, "gl_FrontFacing")) {
         reg = *emit_frontfacing_interpolation();
         emit(MOV(retype(varying, BRW_REGISTER_TYPE_UD), reg));
      } else {
         emit_general_interpolation(varying, var->name, var->type,
                                    (glsl_interp_qualifier) var->data.interpolation,
                                    var->data.location, var->data.centroid,
                                    var->data.sample);
      }
   }
}

void
fs_visitor::nir_setup_outputs(nir_shader *shader)
{
   brw_wm_prog_key *key = (brw_wm_prog_key*) this->key;

   struct hash_entry *entry;
   hash_table_foreach(shader->outputs, entry) {
      nir_variable *var = (nir_variable *) entry->data;
      fs_reg reg = offset(nir_outputs, var->data.driver_location);

      if (var->data.index > 0) {
         assert(var->data.location == FRAG_RESULT_DATA0);
         assert(var->data.index == 1);
         this->dual_src_output = reg;
         this->do_dual_src = true;
      } else if (var->data.location == FRAG_RESULT_COLOR) {
         /* Writing gl_FragColor outputs to all color regions. */
         for (unsigned int i = 0; i < MAX2(key->nr_color_regions, 1); i++) {
            this->outputs[i] = reg;
            this->output_components[i] = 4;
         }
      } else if (var->data.location == FRAG_RESULT_DEPTH) {
         this->frag_depth = reg;
      } else if (var->data.location == FRAG_RESULT_SAMPLE_MASK) {
         this->sample_mask = reg;
      } else {
         /* gl_FragData or a user-defined FS output */
         assert(var->data.location >= FRAG_RESULT_DATA0 &&
                var->data.location < FRAG_RESULT_DATA0 + BRW_MAX_DRAW_BUFFERS);

         int vector_elements =
            var->type->is_array() ? var->type->fields.array->vector_elements
                                  : var->type->vector_elements;

         /* General color output. */
         for (unsigned int i = 0; i < MAX2(1, var->type->length); i++) {
            int output = var->data.location - FRAG_RESULT_DATA0 + i;
            this->outputs[output] = offset(reg, vector_elements * i);
            this->output_components[output] = vector_elements;
         }
      }
   }
}

void
fs_visitor::nir_setup_uniforms(nir_shader *shader)
{
   uniforms = shader->num_uniforms;
   param_size[0] = shader->num_uniforms;

   if (dispatch_width != 8)
      return;

   struct hash_entry *entry;
   hash_table_foreach(shader->uniforms, entry) {
      nir_variable *var = (nir_variable *) entry->data;

      /* UBO's and atomics don't take up space in the uniform file */

      if (var->interface_type != NULL || var->type->contains_atomic())
         continue;

      if (strncmp(var->name, "gl_", 3) == 0)
         nir_setup_builtin_uniform(var);
      else
         nir_setup_uniform(var);
   }
}

void
fs_visitor::nir_setup_uniform(nir_variable *var)
{
   int namelen = strlen(var->name);

   /* The data for our (non-builtin) uniforms is stored in a series of
      * gl_uniform_driver_storage structs for each subcomponent that
      * glGetUniformLocation() could name.  We know it's been set up in the
      * same order we'd walk the type, so walk the list of storage and find
      * anything with our name, or the prefix of a component that starts with
      * our name.
      */
   unsigned index = var->data.driver_location;
   for (unsigned u = 0; u < shader_prog->NumUserUniformStorage; u++) {
      struct gl_uniform_storage *storage = &shader_prog->UniformStorage[u];

      if (strncmp(var->name, storage->name, namelen) != 0 ||
         (storage->name[namelen] != 0 &&
         storage->name[namelen] != '.' &&
         storage->name[namelen] != '[')) {
         continue;
      }

      unsigned slots = storage->type->component_slots();
      if (storage->array_elements)
         slots *= storage->array_elements;

      for (unsigned i = 0; i < slots; i++) {
         stage_prog_data->param[index++] = &storage->storage[i];
      }
   }

   /* Make sure we actually initialized the right amount of stuff here. */
   assert(var->data.driver_location + var->type->component_slots() == index);
}

void
fs_visitor::nir_setup_builtin_uniform(nir_variable *var)
{
   const nir_state_slot *const slots = var->state_slots;
   assert(var->state_slots != NULL);

   unsigned uniform_index = var->data.driver_location;
   for (unsigned int i = 0; i < var->num_state_slots; i++) {
      /* This state reference has already been setup by ir_to_mesa, but we'll
       * get the same index back here.
       */
      int index = _mesa_add_state_reference(this->prog->Parameters,
                                            (gl_state_index *)slots[i].tokens);

      /* Add each of the unique swizzles of the element as a parameter.
       * This'll end up matching the expected layout of the
       * array/matrix/structure we're trying to fill in.
       */
      int last_swiz = -1;
      for (unsigned int j = 0; j < 4; j++) {
         int swiz = GET_SWZ(slots[i].swizzle, j);
         if (swiz == last_swiz)
            break;
         last_swiz = swiz;

         stage_prog_data->param[uniform_index++] =
            &prog->Parameters->ParameterValues[index][swiz];
      }
   }
}

static bool
emit_system_values_block(nir_block *block, void *void_visitor)
{
   fs_visitor *v = (fs_visitor *)void_visitor;
   fs_reg *reg;

   nir_foreach_instr(block, instr) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_load_sample_pos:
         assert(v->stage == MESA_SHADER_FRAGMENT);
         reg = &v->nir_system_values[SYSTEM_VALUE_SAMPLE_POS];
         if (reg->file == BAD_FILE)
            *reg = *v->emit_samplepos_setup();
         break;

      case nir_intrinsic_load_sample_id:
         assert(v->stage == MESA_SHADER_FRAGMENT);
         reg = &v->nir_system_values[SYSTEM_VALUE_SAMPLE_ID];
         if (reg->file == BAD_FILE)
            *reg = *v->emit_sampleid_setup();
         break;

      case nir_intrinsic_load_sample_mask_in:
         assert(v->stage == MESA_SHADER_FRAGMENT);
         assert(v->brw->gen >= 7);
         reg = &v->nir_system_values[SYSTEM_VALUE_SAMPLE_MASK_IN];
         if (reg->file == BAD_FILE)
            *reg = fs_reg(retype(brw_vec8_grf(v->payload.sample_mask_in_reg, 0),
                                 BRW_REGISTER_TYPE_D));
         break;

      default:
         break;
      }
   }

   return true;
}

void
fs_visitor::nir_emit_system_values(nir_shader *shader)
{
   nir_system_values = ralloc_array(mem_ctx, fs_reg, SYSTEM_VALUE_MAX);
   nir_foreach_overload(shader, overload) {
      assert(strcmp(overload->function->name, "main") == 0);
      assert(overload->impl);
      nir_foreach_block(overload->impl, emit_system_values_block, this);
   }
}

void
fs_visitor::nir_emit_impl(nir_function_impl *impl)
{
   nir_locals = reralloc(mem_ctx, nir_locals, fs_reg, impl->reg_alloc);
   foreach_list_typed(nir_register, reg, node, &impl->registers) {
      unsigned array_elems =
         reg->num_array_elems == 0 ? 1 : reg->num_array_elems;
      unsigned size = array_elems * reg->num_components;
      nir_locals[reg->index] = vgrf(size);
   }

   nir_emit_cf_list(&impl->body);
}

void
fs_visitor::nir_emit_cf_list(exec_list *list)
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
fs_visitor::nir_emit_if(nir_if *if_stmt)
{
   /* first, put the condition into f0 */
   fs_inst *inst = emit(MOV(reg_null_d,
                            retype(get_nir_src(if_stmt->condition),
                                   BRW_REGISTER_TYPE_UD)));
   inst->conditional_mod = BRW_CONDITIONAL_NZ;

   emit(IF(BRW_PREDICATE_NORMAL));

   nir_emit_cf_list(&if_stmt->then_list);

   /* note: if the else is empty, dead CF elimination will remove it */
   emit(BRW_OPCODE_ELSE);

   nir_emit_cf_list(&if_stmt->else_list);

   emit(BRW_OPCODE_ENDIF);

   if (!try_replace_with_sel() && brw->gen < 6) {
      no16("Can't support (non-uniform) control flow on SIMD16\n");
   }
}

void
fs_visitor::nir_emit_loop(nir_loop *loop)
{
   if (brw->gen < 6) {
      no16("Can't support (non-uniform) control flow on SIMD16\n");
   }

   emit(BRW_OPCODE_DO);

   nir_emit_cf_list(&loop->body);

   emit(BRW_OPCODE_WHILE);
}

void
fs_visitor::nir_emit_block(nir_block *block)
{
   nir_foreach_instr(block, instr) {
      nir_emit_instr(instr);
   }
}

void
fs_visitor::nir_emit_instr(nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_alu:
      nir_emit_alu(nir_instr_as_alu(instr));
      break;

   case nir_instr_type_intrinsic:
      nir_emit_intrinsic(nir_instr_as_intrinsic(instr));
      break;

   case nir_instr_type_tex:
      nir_emit_texture(nir_instr_as_tex(instr));
      break;

   case nir_instr_type_load_const:
      /* We can hit these, but we do nothing now and use them as
       * immediates later.
       */
      break;

   case nir_instr_type_jump:
      nir_emit_jump(nir_instr_as_jump(instr));
      break;

   default:
      unreachable("unknown instruction type");
   }
}

static brw_reg_type
brw_type_for_nir_type(nir_alu_type type)
{
   switch (type) {
   case nir_type_bool:
   case nir_type_unsigned:
      return BRW_REGISTER_TYPE_UD;
   case nir_type_int:
      return BRW_REGISTER_TYPE_D;
   case nir_type_float:
      return BRW_REGISTER_TYPE_F;
   default:
      unreachable("unknown type");
   }

   return BRW_REGISTER_TYPE_F;
}

void
fs_visitor::nir_emit_alu(nir_alu_instr *instr)
{
   struct brw_wm_prog_key *fs_key = (struct brw_wm_prog_key *) this->key;
   fs_inst *inst;

   fs_reg result = get_nir_dest(instr->dest.dest);
   result.type = brw_type_for_nir_type(nir_op_infos[instr->op].output_type);

   fs_reg op[4];
   for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
      op[i] = get_nir_src(instr->src[i].src);
      op[i].type = brw_type_for_nir_type(nir_op_infos[instr->op].input_types[i]);
      op[i].abs = instr->src[i].abs;
      op[i].negate = instr->src[i].negate;
   }

   /* We get a bunch of mov's out of the from_ssa pass and they may still
    * be vectorized.  We'll handle them as a special-case.  We'll also
    * handle vecN here because it's basically the same thing.
    */
   switch (instr->op) {
   case nir_op_imov:
   case nir_op_fmov:
   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4: {
      fs_reg temp = result;
      bool need_extra_copy = false;
      for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
         if (!instr->src[i].src.is_ssa &&
             instr->dest.dest.reg.reg == instr->src[i].src.reg.reg) {
            need_extra_copy = true;
            temp = retype(vgrf(4), result.type);
            break;
         }
      }

      for (unsigned i = 0; i < 4; i++) {
         if (!(instr->dest.write_mask & (1 << i)))
            continue;

         if (instr->op == nir_op_imov || instr->op == nir_op_fmov) {
            inst = emit(MOV(offset(temp, i),
                        offset(op[0], instr->src[0].swizzle[i])));
         } else {
            inst = emit(MOV(offset(temp, i),
                        offset(op[i], instr->src[i].swizzle[0])));
         }
         inst->saturate = instr->dest.saturate;
      }

      /* In this case the source and destination registers were the same,
       * so we need to insert an extra set of moves in order to deal with
       * any swizzling.
       */
      if (need_extra_copy) {
         for (unsigned i = 0; i < 4; i++) {
            if (!(instr->dest.write_mask & (1 << i)))
               continue;

            emit(MOV(offset(result, i), offset(temp, i)));
         }
      }
      return;
   }
   default:
      break;
   }

   /* At this point, we have dealt with any instruction that operates on
    * more than a single channel.  Therefore, we can just adjust the source
    * and destination registers for that channel and emit the instruction.
    */
   unsigned channel = 0;
   if (nir_op_infos[instr->op].output_size == 0) {
      /* Since NIR is doing the scalarizing for us, we should only ever see
       * vectorized operations with a single channel.
       */
      assert(_mesa_bitcount(instr->dest.write_mask) == 1);
      channel = ffs(instr->dest.write_mask) - 1;

      result = offset(result, channel);
   }

   for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
      assert(nir_op_infos[instr->op].input_sizes[i] < 2);
      op[i] = offset(op[i], instr->src[i].swizzle[channel]);
   }

   switch (instr->op) {
   case nir_op_i2f:
   case nir_op_u2f:
      inst = emit(MOV(result, op[0]));
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_f2i:
   case nir_op_f2u:
      emit(MOV(result, op[0]));
      break;

   case nir_op_fsign: {
      /* AND(val, 0x80000000) gives the sign bit.
         *
         * Predicated OR ORs 1.0 (0x3f800000) with the sign bit if val is not
         * zero.
         */
      emit(CMP(reg_null_f, op[0], fs_reg(0.0f), BRW_CONDITIONAL_NZ));

      fs_reg result_int = retype(result, BRW_REGISTER_TYPE_UD);
      op[0].type = BRW_REGISTER_TYPE_UD;
      result.type = BRW_REGISTER_TYPE_UD;
      emit(AND(result_int, op[0], fs_reg(0x80000000u)));

      inst = emit(OR(result_int, result_int, fs_reg(0x3f800000u)));
      inst->predicate = BRW_PREDICATE_NORMAL;
      if (instr->dest.saturate) {
         inst = emit(MOV(result, result));
         inst->saturate = true;
      }
      break;
   }

   case nir_op_isign:
      /*  ASR(val, 31) -> negative val generates 0xffffffff (signed -1).
       *               -> non-negative val generates 0x00000000.
       *  Predicated OR sets 1 if val is positive.
       */
      emit(CMP(reg_null_d, op[0], fs_reg(0), BRW_CONDITIONAL_G));
      emit(ASR(result, op[0], fs_reg(31)));
      inst = emit(OR(result, result, fs_reg(1)));
      inst->predicate = BRW_PREDICATE_NORMAL;
      break;

   case nir_op_frcp:
      inst = emit_math(SHADER_OPCODE_RCP, result, op[0]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fexp2:
      inst = emit_math(SHADER_OPCODE_EXP2, result, op[0]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_flog2:
      inst = emit_math(SHADER_OPCODE_LOG2, result, op[0]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fexp:
   case nir_op_flog:
      unreachable("not reached: should be handled by ir_explog_to_explog2");

   case nir_op_fsin:
   case nir_op_fsin_reduced:
      inst = emit_math(SHADER_OPCODE_SIN, result, op[0]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fcos:
   case nir_op_fcos_reduced:
      inst = emit_math(SHADER_OPCODE_COS, result, op[0]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fddx:
      if (fs_key->high_quality_derivatives) {
         inst = emit(FS_OPCODE_DDX_FINE, result, op[0]);
      } else {
         inst = emit(FS_OPCODE_DDX_COARSE, result, op[0]);
      }
      inst->saturate = instr->dest.saturate;
      break;
   case nir_op_fddx_fine:
      inst = emit(FS_OPCODE_DDX_FINE, result, op[0]);
      inst->saturate = instr->dest.saturate;
      break;
   case nir_op_fddx_coarse:
      inst = emit(FS_OPCODE_DDX_COARSE, result, op[0]);
      inst->saturate = instr->dest.saturate;
      break;
   case nir_op_fddy:
      if (fs_key->high_quality_derivatives) {
         inst = emit(FS_OPCODE_DDY_FINE, result, op[0],
                     fs_reg(fs_key->render_to_fbo));
      } else {
         inst = emit(FS_OPCODE_DDY_COARSE, result, op[0],
                     fs_reg(fs_key->render_to_fbo));
      }
      inst->saturate = instr->dest.saturate;
      break;
   case nir_op_fddy_fine:
      inst = emit(FS_OPCODE_DDY_FINE, result, op[0],
                  fs_reg(fs_key->render_to_fbo));
      inst->saturate = instr->dest.saturate;
      break;
   case nir_op_fddy_coarse:
      inst = emit(FS_OPCODE_DDY_COARSE, result, op[0],
                  fs_reg(fs_key->render_to_fbo));
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fadd:
   case nir_op_iadd:
      inst = emit(ADD(result, op[0], op[1]));
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fmul:
      inst = emit(MUL(result, op[0], op[1]));
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_imul: {
      /* TODO put in the 16-bit constant optimization once we have SSA */

      if (brw->gen >= 7)
         no16("SIMD16 explicit accumulator operands unsupported\n");

      struct brw_reg acc = retype(brw_acc_reg(dispatch_width), result.type);

      emit(MUL(acc, op[0], op[1]));
      emit(MACH(reg_null_d, op[0], op[1]));
      emit(MOV(result, fs_reg(acc)));
      break;
   }

   case nir_op_imul_high:
   case nir_op_umul_high: {
      if (brw->gen >= 7)
         no16("SIMD16 explicit accumulator operands unsupported\n");

      struct brw_reg acc = retype(brw_acc_reg(dispatch_width), result.type);

      emit(MUL(acc, op[0], op[1]));
      emit(MACH(result, op[0], op[1]));
      break;
   }

   case nir_op_idiv:
   case nir_op_udiv:
      emit_math(SHADER_OPCODE_INT_QUOTIENT, result, op[0], op[1]);
      break;

   case nir_op_uadd_carry: {
      if (brw->gen >= 7)
         no16("SIMD16 explicit accumulator operands unsupported\n");

      struct brw_reg acc = retype(brw_acc_reg(dispatch_width),
                                  BRW_REGISTER_TYPE_UD);

      emit(ADDC(reg_null_ud, op[0], op[1]));
      emit(MOV(result, fs_reg(acc)));
      break;
   }

   case nir_op_usub_borrow: {
      if (brw->gen >= 7)
         no16("SIMD16 explicit accumulator operands unsupported\n");

      struct brw_reg acc = retype(brw_acc_reg(dispatch_width),
                                  BRW_REGISTER_TYPE_UD);

      emit(SUBB(reg_null_ud, op[0], op[1]));
      emit(MOV(result, fs_reg(acc)));
      break;
   }

   case nir_op_umod:
      emit_math(SHADER_OPCODE_INT_REMAINDER, result, op[0], op[1]);
      break;

   case nir_op_flt:
   case nir_op_ilt:
   case nir_op_ult:
      emit(CMP(result, op[0], op[1], BRW_CONDITIONAL_L));
      break;

   case nir_op_fge:
   case nir_op_ige:
   case nir_op_uge:
      emit(CMP(result, op[0], op[1], BRW_CONDITIONAL_GE));
      break;

   case nir_op_feq:
   case nir_op_ieq:
      emit(CMP(result, op[0], op[1], BRW_CONDITIONAL_Z));
      break;

   case nir_op_fne:
   case nir_op_ine:
      emit(CMP(result, op[0], op[1], BRW_CONDITIONAL_NZ));
      break;

   case nir_op_inot:
      emit(NOT(result, op[0]));
      break;
   case nir_op_ixor:
      emit(XOR(result, op[0], op[1]));
      break;
   case nir_op_ior:
      emit(OR(result, op[0], op[1]));
      break;
   case nir_op_iand:
      emit(AND(result, op[0], op[1]));
      break;

   case nir_op_fdot2:
   case nir_op_fdot3:
   case nir_op_fdot4:
   case nir_op_bany2:
   case nir_op_bany3:
   case nir_op_bany4:
   case nir_op_ball2:
   case nir_op_ball3:
   case nir_op_ball4:
   case nir_op_ball_fequal2:
   case nir_op_ball_iequal2:
   case nir_op_ball_fequal3:
   case nir_op_ball_iequal3:
   case nir_op_ball_fequal4:
   case nir_op_ball_iequal4:
   case nir_op_bany_fnequal2:
   case nir_op_bany_inequal2:
   case nir_op_bany_fnequal3:
   case nir_op_bany_inequal3:
   case nir_op_bany_fnequal4:
   case nir_op_bany_inequal4:
      unreachable("Lowered by nir_lower_alu_reductions");

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

   case nir_op_ldexp:
      unreachable("not reached: should be handled by ldexp_to_arith()");

   case nir_op_fsqrt:
      inst = emit_math(SHADER_OPCODE_SQRT, result, op[0]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_frsq:
      inst = emit_math(SHADER_OPCODE_RSQ, result, op[0]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_b2i:
      emit(AND(result, op[0], fs_reg(1)));
      break;
   case nir_op_b2f:
      emit(AND(retype(result, BRW_REGISTER_TYPE_UD), op[0], fs_reg(0x3f800000u)));
      break;

   case nir_op_f2b:
      emit(CMP(result, op[0], fs_reg(0.0f), BRW_CONDITIONAL_NZ));
      break;
   case nir_op_i2b:
      emit(CMP(result, op[0], fs_reg(0), BRW_CONDITIONAL_NZ));
      break;

   case nir_op_ftrunc:
      inst = emit(RNDZ(result, op[0]));
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fceil: {
      op[0].negate = !op[0].negate;
      fs_reg temp = vgrf(glsl_type::float_type);
      emit(RNDD(temp, op[0]));
      temp.negate = true;
      inst = emit(MOV(result, temp));
      inst->saturate = instr->dest.saturate;
      break;
   }
   case nir_op_ffloor:
      inst = emit(RNDD(result, op[0]));
      inst->saturate = instr->dest.saturate;
      break;
   case nir_op_ffract:
      inst = emit(FRC(result, op[0]));
      inst->saturate = instr->dest.saturate;
      break;
   case nir_op_fround_even:
      inst = emit(RNDE(result, op[0]));
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fmin:
   case nir_op_imin:
   case nir_op_umin:
      if (brw->gen >= 6) {
         inst = emit(BRW_OPCODE_SEL, result, op[0], op[1]);
         inst->conditional_mod = BRW_CONDITIONAL_L;
      } else {
         emit(CMP(reg_null_d, op[0], op[1], BRW_CONDITIONAL_L));
         inst = emit(SEL(result, op[0], op[1]));
      }
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fmax:
   case nir_op_imax:
   case nir_op_umax:
      if (brw->gen >= 6) {
         inst = emit(BRW_OPCODE_SEL, result, op[0], op[1]);
         inst->conditional_mod = BRW_CONDITIONAL_GE;
      } else {
         emit(CMP(reg_null_d, op[0], op[1], BRW_CONDITIONAL_GE));
         inst = emit(SEL(result, op[0], op[1]));
      }
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_pack_snorm_2x16:
   case nir_op_pack_snorm_4x8:
   case nir_op_pack_unorm_2x16:
   case nir_op_pack_unorm_4x8:
   case nir_op_unpack_snorm_2x16:
   case nir_op_unpack_snorm_4x8:
   case nir_op_unpack_unorm_2x16:
   case nir_op_unpack_unorm_4x8:
   case nir_op_unpack_half_2x16:
   case nir_op_pack_half_2x16:
      unreachable("not reached: should be handled by lower_packing_builtins");

   case nir_op_unpack_half_2x16_split_x:
      inst = emit(FS_OPCODE_UNPACK_HALF_2x16_SPLIT_X, result, op[0]);
      inst->saturate = instr->dest.saturate;
      break;
   case nir_op_unpack_half_2x16_split_y:
      inst = emit(FS_OPCODE_UNPACK_HALF_2x16_SPLIT_Y, result, op[0]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_fpow:
      inst = emit_math(SHADER_OPCODE_POW, result, op[0], op[1]);
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_bitfield_reverse:
      emit(BFREV(result, op[0]));
      break;

   case nir_op_bit_count:
      emit(CBIT(result, op[0]));
      break;

   case nir_op_ufind_msb:
   case nir_op_ifind_msb: {
      emit(FBH(retype(result, BRW_REGISTER_TYPE_UD), op[0]));

      /* FBH counts from the MSB side, while GLSL's findMSB() wants the count
       * from the LSB side. If FBH didn't return an error (0xFFFFFFFF), then
       * subtract the result from 31 to convert the MSB count into an LSB count.
       */

      emit(CMP(reg_null_d, result, fs_reg(-1), BRW_CONDITIONAL_NZ));
      fs_reg neg_result(result);
      neg_result.negate = true;
      inst = emit(ADD(result, neg_result, fs_reg(31)));
      inst->predicate = BRW_PREDICATE_NORMAL;
      break;
   }

   case nir_op_find_lsb:
      emit(FBL(result, op[0]));
      break;

   case nir_op_ubitfield_extract:
   case nir_op_ibitfield_extract:
      emit(BFE(result, op[2], op[1], op[0]));
      break;
   case nir_op_bfm:
      emit(BFI1(result, op[0], op[1]));
      break;
   case nir_op_bfi:
      emit(BFI2(result, op[0], op[1], op[2]));
      break;

   case nir_op_bitfield_insert:
      unreachable("not reached: should be handled by "
                  "lower_instructions::bitfield_insert_to_bfm_bfi");

   case nir_op_ishl:
      emit(SHL(result, op[0], op[1]));
      break;
   case nir_op_ishr:
      emit(ASR(result, op[0], op[1]));
      break;
   case nir_op_ushr:
      emit(SHR(result, op[0], op[1]));
      break;

   case nir_op_pack_half_2x16_split:
      emit(FS_OPCODE_PACK_HALF_2x16_SPLIT, result, op[0], op[1]);
      break;

   case nir_op_ffma:
      inst = emit(MAD(result, op[2], op[1], op[0]));
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_flrp:
      /* TODO emulate for gen < 6 */
      inst = emit(LRP(result, op[2], op[1], op[0]));
      inst->saturate = instr->dest.saturate;
      break;

   case nir_op_bcsel:
      emit(CMP(reg_null_d, op[0], fs_reg(0), BRW_CONDITIONAL_NZ));
      inst = emit(SEL(result, op[1], op[2]));
      inst->predicate = BRW_PREDICATE_NORMAL;
      break;

   default:
      unreachable("unhandled instruction");
   }
}

fs_reg
fs_visitor::get_nir_src(nir_src src)
{
   if (src.is_ssa) {
      assert(src.ssa->parent_instr->type == nir_instr_type_load_const);
      nir_load_const_instr *load = nir_instr_as_load_const(src.ssa->parent_instr);
      fs_reg reg = vgrf(src.ssa->num_components);
      reg.type = BRW_REGISTER_TYPE_D;

      for (unsigned i = 0; i < src.ssa->num_components; ++i)
         emit(MOV(offset(reg, i), fs_reg(load->value.i[i])));

      return reg;
   } else {
      fs_reg reg;
      if (src.reg.reg->is_global)
         reg = nir_globals[src.reg.reg->index];
      else
         reg = nir_locals[src.reg.reg->index];

      /* to avoid floating-point denorm flushing problems, set the type by
       * default to D - instructions that need floating point semantics will set
       * this to F if they need to
       */
      reg = retype(offset(reg, src.reg.base_offset), BRW_REGISTER_TYPE_D);
      if (src.reg.indirect) {
         reg.reladdr = new(mem_ctx) fs_reg();
         *reg.reladdr = retype(get_nir_src(*src.reg.indirect),
                               BRW_REGISTER_TYPE_D);
      }

      return reg;
   }
}

fs_reg
fs_visitor::get_nir_dest(nir_dest dest)
{
   fs_reg reg;
   if (dest.reg.reg->is_global)
      reg = nir_globals[dest.reg.reg->index];
   else
      reg = nir_locals[dest.reg.reg->index];

   reg = offset(reg, dest.reg.base_offset);
   if (dest.reg.indirect) {
      reg.reladdr = new(mem_ctx) fs_reg();
      *reg.reladdr = retype(get_nir_src(*dest.reg.indirect),
                            BRW_REGISTER_TYPE_D);
   }

   return reg;
}

void
fs_visitor::emit_percomp(fs_inst *inst, unsigned wr_mask)
{
   for (unsigned i = 0; i < 4; i++) {
      if (!((wr_mask >> i) & 1))
         continue;

      fs_inst *new_inst = new(mem_ctx) fs_inst(*inst);
      new_inst->dst = offset(new_inst->dst, i);
      for (unsigned j = 0; j < new_inst->sources; j++)
         if (inst->src[j].file == GRF)
            new_inst->src[j] = offset(new_inst->src[j], i);

      emit(new_inst);
   }
}

void
fs_visitor::nir_emit_intrinsic(nir_intrinsic_instr *instr)
{
   fs_reg dest;
   if (nir_intrinsic_infos[instr->intrinsic].has_dest)
      dest = get_nir_dest(instr->dest);

   bool has_indirect = false;

   switch (instr->intrinsic) {
   case nir_intrinsic_discard: {
      /* We track our discarded pixels in f0.1.  By predicating on it, we can
       * update just the flag bits that aren't yet discarded.  By emitting a
       * CMP of g0 != g0, all our currently executing channels will get turned
       * off.
       */
      fs_reg some_reg = fs_reg(retype(brw_vec8_grf(0, 0),
                                    BRW_REGISTER_TYPE_UW));
      fs_inst *cmp = emit(CMP(reg_null_f, some_reg, some_reg,
                              BRW_CONDITIONAL_NZ));
      cmp->predicate = BRW_PREDICATE_NORMAL;
      cmp->flag_subreg = 1;

      if (brw->gen >= 6) {
         /* For performance, after a discard, jump to the end of the shader.
         * Only jump if all relevant channels have been discarded.
         */
         fs_inst *discard_jump = emit(FS_OPCODE_DISCARD_JUMP);
         discard_jump->flag_subreg = 1;

         discard_jump->predicate = (dispatch_width == 8)
                                 ? BRW_PREDICATE_ALIGN1_ANY8H
                                 : BRW_PREDICATE_ALIGN1_ANY16H;
         discard_jump->predicate_inverse = true;
      }

      break;
   }

   case nir_intrinsic_atomic_counter_inc:
   case nir_intrinsic_atomic_counter_dec:
   case nir_intrinsic_atomic_counter_read: {
      unsigned surf_index = prog_data->binding_table.abo_start +
                            (unsigned) instr->const_index[0];
      fs_reg offset = fs_reg(get_nir_src(instr->src[0]));

      switch (instr->intrinsic) {
         case nir_intrinsic_atomic_counter_inc:
            emit_untyped_atomic(BRW_AOP_INC, surf_index, dest, offset,
                                fs_reg(), fs_reg());
            break;
         case nir_intrinsic_atomic_counter_dec:
            emit_untyped_atomic(BRW_AOP_PREDEC, surf_index, dest, offset,
                                fs_reg(), fs_reg());
            break;
         case nir_intrinsic_atomic_counter_read:
            emit_untyped_surface_read(surf_index, dest, offset);
            break;
         default:
            unreachable("Unreachable");
      }
      break;
   }

   case nir_intrinsic_load_front_face:
      assert(!"TODO");

   case nir_intrinsic_load_sample_mask_in: {
      fs_reg sample_mask_in = nir_system_values[SYSTEM_VALUE_SAMPLE_MASK_IN];
      assert(sample_mask_in.file != BAD_FILE);
      dest.type = sample_mask_in.type;
      emit(MOV(dest, sample_mask_in));
      break;
   }

   case nir_intrinsic_load_sample_pos: {
      fs_reg sample_pos = nir_system_values[SYSTEM_VALUE_SAMPLE_POS];
      assert(sample_pos.file != BAD_FILE);
      dest.type = sample_pos.type;
      emit(MOV(dest, sample_pos));
      emit(MOV(offset(dest, 1), offset(sample_pos, 1)));
      break;
   }

   case nir_intrinsic_load_sample_id: {
      fs_reg sample_id = nir_system_values[SYSTEM_VALUE_SAMPLE_ID];
      assert(sample_id.file != BAD_FILE);
      dest.type = sample_id.type;
      emit(MOV(dest, sample_id));
      break;
   }

   case nir_intrinsic_load_uniform_indirect:
      has_indirect = true;
   case nir_intrinsic_load_uniform: {
      unsigned index = 0;
      for (int i = 0; i < instr->const_index[1]; i++) {
         for (unsigned j = 0; j < instr->num_components; j++) {
            fs_reg src = offset(retype(nir_uniforms, dest.type),
                                instr->const_index[0] + index);
            if (has_indirect)
               src.reladdr = new(mem_ctx) fs_reg(get_nir_src(instr->src[0]));
            index++;

            emit(MOV(dest, src));
            dest = offset(dest, 1);
         }
      }
      break;
   }

   case nir_intrinsic_load_ubo_indirect:
      has_indirect = true;
   case nir_intrinsic_load_ubo: {
      nir_const_value *const_index = nir_src_as_const_value(instr->src[0]);
      fs_reg surf_index;

      if (const_index) {
         surf_index = fs_reg(stage_prog_data->binding_table.ubo_start +
                             const_index->u[0]);
      } else {
         /* The block index is not a constant. Evaluate the index expression
          * per-channel and add the base UBO index; the generator will select
          * a value from any live channel.
          */
         surf_index = vgrf(glsl_type::uint_type);
         emit(ADD(surf_index, get_nir_src(instr->src[0]),
                  fs_reg(stage_prog_data->binding_table.ubo_start)))
            ->force_writemask_all = true;

         /* Assume this may touch any UBO. It would be nice to provide
          * a tighter bound, but the array information is already lowered away.
          */
         brw_mark_surface_used(prog_data,
                               stage_prog_data->binding_table.ubo_start +
                               shader_prog->NumUniformBlocks - 1);
      }

      if (has_indirect) {
         /* Turn the byte offset into a dword offset. */
         fs_reg base_offset = vgrf(glsl_type::int_type);
         emit(SHR(base_offset, retype(get_nir_src(instr->src[1]),
                                 BRW_REGISTER_TYPE_D),
                  fs_reg(2)));

         unsigned vec4_offset = instr->const_index[0] / 4;
         for (int i = 0; i < instr->num_components; i++)
            emit(VARYING_PULL_CONSTANT_LOAD(offset(dest, i), surf_index,
                                            base_offset, vec4_offset + i));
      } else {
         fs_reg packed_consts = vgrf(glsl_type::float_type);
         packed_consts.type = dest.type;

         fs_reg const_offset_reg((unsigned) instr->const_index[0] & ~15);
         emit(FS_OPCODE_UNIFORM_PULL_CONSTANT_LOAD, packed_consts,
              surf_index, const_offset_reg);

         for (unsigned i = 0; i < instr->num_components; i++) {
            packed_consts.set_smear(instr->const_index[0] % 16 / 4 + i);

            /* The std140 packing rules don't allow vectors to cross 16-byte
             * boundaries, and a reg is 32 bytes.
             */
            assert(packed_consts.subreg_offset < 32);

            emit(MOV(dest, packed_consts));
            dest = offset(dest, 1);
         }
      }
      break;
   }

   case nir_intrinsic_load_input_indirect:
      has_indirect = true;
   case nir_intrinsic_load_input: {
      unsigned index = 0;
      for (int i = 0; i < instr->const_index[1]; i++) {
         for (unsigned j = 0; j < instr->num_components; j++) {
            fs_reg src = offset(retype(nir_inputs, dest.type),
                                instr->const_index[0] + index);
            if (has_indirect)
               src.reladdr = new(mem_ctx) fs_reg(get_nir_src(instr->src[0]));
            index++;

            emit(MOV(dest, src));
            dest = offset(dest, 1);
         }
      }
      break;
   }

   /* Handle ARB_gpu_shader5 interpolation intrinsics
    *
    * It's worth a quick word of explanation as to why we handle the full
    * variable-based interpolation intrinsic rather than a lowered version
    * with like we do for other inputs.  We have to do that because the way
    * we set up inputs doesn't allow us to use the already setup inputs for
    * interpolation.  At the beginning of the shader, we go through all of
    * the input variables and do the initial interpolation and put it in
    * the nir_inputs array based on its location as determined in
    * nir_lower_io.  If the input isn't used, dead code cleans up and
    * everything works fine.  However, when we get to the ARB_gpu_shader5
    * interpolation intrinsics, we need to reinterpolate the input
    * differently.  If we used an intrinsic that just had an index it would
    * only give us the offset into the nir_inputs array.  However, this is
    * useless because that value is post-interpolation and we need
    * pre-interpolation.  In order to get the actual location of the bits
    * we get from the vertex fetching hardware, we need the variable.
    */
   case nir_intrinsic_interp_var_at_centroid:
   case nir_intrinsic_interp_var_at_sample:
   case nir_intrinsic_interp_var_at_offset: {
      /* in SIMD16 mode, the pixel interpolator returns coords interleaved
       * 8 channels at a time, same as the barycentric coords presented in
       * the FS payload. this requires a bit of extra work to support.
       */
      no16("interpolate_at_* not yet supported in SIMD16 mode.");

      fs_reg dst_x = vgrf(2);
      fs_reg dst_y = offset(dst_x, 1);

      /* For most messages, we need one reg of ignored data; the hardware
       * requires mlen==1 even when there is no payload. in the per-slot
       * offset case, we'll replace this with the proper source data.
       */
      fs_reg src = vgrf(glsl_type::float_type);
      int mlen = 1;     /* one reg unless overriden */
      fs_inst *inst;

      switch (instr->intrinsic) {
      case nir_intrinsic_interp_var_at_centroid:
         inst = emit(FS_OPCODE_INTERPOLATE_AT_CENTROID, dst_x, src, fs_reg(0u));
         break;

      case nir_intrinsic_interp_var_at_sample: {
         /* XXX: We should probably handle non-constant sample id's */
         nir_const_value *const_sample = nir_src_as_const_value(instr->src[0]);
         assert(const_sample);
         unsigned msg_data = const_sample ? const_sample->i[0] << 4 : 0;
         inst = emit(FS_OPCODE_INTERPOLATE_AT_SAMPLE, dst_x, src,
                     fs_reg(msg_data));
         break;
      }

      case nir_intrinsic_interp_var_at_offset: {
         nir_const_value *const_offset = nir_src_as_const_value(instr->src[0]);

         if (const_offset) {
            unsigned off_x = MIN2((int)(const_offset->f[0] * 16), 7) & 0xf;
            unsigned off_y = MIN2((int)(const_offset->f[1] * 16), 7) & 0xf;

            inst = emit(FS_OPCODE_INTERPOLATE_AT_SHARED_OFFSET, dst_x, src,
                        fs_reg(off_x | (off_y << 4)));
         } else {
            src = vgrf(glsl_type::ivec2_type);
            fs_reg offset_src = retype(get_nir_src(instr->src[0]),
                                       BRW_REGISTER_TYPE_F);
            for (int i = 0; i < 2; i++) {
               fs_reg temp = vgrf(glsl_type::float_type);
               emit(MUL(temp, offset(offset_src, i), fs_reg(16.0f)));
               fs_reg itemp = vgrf(glsl_type::int_type);
               emit(MOV(itemp, temp));  /* float to int */

               /* Clamp the upper end of the range to +7/16.
                * ARB_gpu_shader5 requires that we support a maximum offset
                * of +0.5, which isn't representable in a S0.4 value -- if
                * we didn't clamp it, we'd end up with -8/16, which is the
                * opposite of what the shader author wanted.
                *
                * This is legal due to ARB_gpu_shader5's quantization
                * rules:
                *
                * "Not all values of <offset> may be supported; x and y
                * offsets may be rounded to fixed-point values with the
                * number of fraction bits given by the
                * implementation-dependent constant
                * FRAGMENT_INTERPOLATION_OFFSET_BITS"
                */

               emit(BRW_OPCODE_SEL, offset(src, i), itemp, fs_reg(7))
                   ->conditional_mod = BRW_CONDITIONAL_L; /* min(src2, 7) */
            }

            mlen = 2;
            inst = emit(FS_OPCODE_INTERPOLATE_AT_PER_SLOT_OFFSET, dst_x, src,
                        fs_reg(0u));
         }
         break;
      }

      default:
         unreachable("Invalid intrinsic");
      }

      inst->mlen = mlen;
      inst->regs_written = 2; /* 2 floats per slot returned */
      inst->pi_noperspective = instr->variables[0]->var->data.interpolation ==
                               INTERP_QUALIFIER_NOPERSPECTIVE;

      for (unsigned j = 0; j < instr->num_components; j++) {
         fs_reg src = interp_reg(instr->variables[0]->var->data.location, j);
         src.type = dest.type;

         emit(FS_OPCODE_LINTERP, dest, dst_x, dst_y, src);
         dest = offset(dest, 1);
      }
      break;
   }

   case nir_intrinsic_store_output_indirect:
      has_indirect = true;
   case nir_intrinsic_store_output: {
      fs_reg src = get_nir_src(instr->src[0]);
      unsigned index = 0;
      for (int i = 0; i < instr->const_index[1]; i++) {
         for (unsigned j = 0; j < instr->num_components; j++) {
            fs_reg new_dest = offset(retype(nir_outputs, src.type),
                                     instr->const_index[0] + index);
            if (has_indirect)
               src.reladdr = new(mem_ctx) fs_reg(get_nir_src(instr->src[1]));
            index++;
            emit(MOV(new_dest, src));
            src = offset(src, 1);
         }
      }
      break;
   }

   default:
      unreachable("unknown intrinsic");
   }
}

void
fs_visitor::nir_emit_texture(nir_tex_instr *instr)
{
   brw_wm_prog_key *key = (brw_wm_prog_key*) this->key;
   unsigned sampler = instr->sampler_index;
   fs_reg sampler_reg(sampler);

   /* FINISHME: We're failing to recompile our programs when the sampler is
    * updated.  This only matters for the texture rectangle scale parameters
    * (pre-gen6, or gen6+ with GL_CLAMP).
    */
   int texunit = prog->SamplerUnits[sampler];

   int gather_component = instr->component;

   bool is_rect = instr->sampler_dim == GLSL_SAMPLER_DIM_RECT;

   bool is_cube_array = instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE &&
                        instr->is_array;

   int lod_components, offset_components = 0;

   fs_reg coordinate, shadow_comparitor, lod, lod2, sample_index, mcs, offset;

   for (unsigned i = 0; i < instr->num_srcs; i++) {
      fs_reg src = get_nir_src(instr->src[i].src);
      switch (instr->src[i].src_type) {
      case nir_tex_src_bias:
         lod = retype(src, BRW_REGISTER_TYPE_F);
         break;
      case nir_tex_src_comparitor:
         shadow_comparitor = retype(src, BRW_REGISTER_TYPE_F);
         break;
      case nir_tex_src_coord:
         switch (instr->op) {
         case nir_texop_txf:
         case nir_texop_txf_ms:
            coordinate = retype(src, BRW_REGISTER_TYPE_D);
            break;
         default:
            coordinate = retype(src, BRW_REGISTER_TYPE_F);
            break;
         }
         break;
      case nir_tex_src_ddx:
         lod = retype(src, BRW_REGISTER_TYPE_F);
         lod_components = nir_tex_instr_src_size(instr, i);
         break;
      case nir_tex_src_ddy:
         lod2 = retype(src, BRW_REGISTER_TYPE_F);
         break;
      case nir_tex_src_lod:
         switch (instr->op) {
         case nir_texop_txs:
            lod = retype(src, BRW_REGISTER_TYPE_UD);
            break;
         case nir_texop_txf:
            lod = retype(src, BRW_REGISTER_TYPE_D);
            break;
         default:
            lod = retype(src, BRW_REGISTER_TYPE_F);
            break;
         }
         break;
      case nir_tex_src_ms_index:
         sample_index = retype(src, BRW_REGISTER_TYPE_UD);
         break;
      case nir_tex_src_offset:
         offset = retype(src, BRW_REGISTER_TYPE_D);
         if (instr->is_array)
            offset_components = instr->coord_components - 1;
         else
            offset_components = instr->coord_components;
         break;
      case nir_tex_src_projector:
         unreachable("should be lowered");

      case nir_tex_src_sampler_offset: {
         /* Figure out the highest possible sampler index and mark it as used */
         uint32_t max_used = sampler + instr->sampler_array_size - 1;
         if (instr->op == nir_texop_tg4 && brw->gen < 8) {
            max_used += stage_prog_data->binding_table.gather_texture_start;
         } else {
            max_used += stage_prog_data->binding_table.texture_start;
         }
         brw_mark_surface_used(prog_data, max_used);

         /* Emit code to evaluate the actual indexing expression */
         sampler_reg = vgrf(glsl_type::uint_type);
         emit(ADD(sampler_reg, src, fs_reg(sampler)))
             ->force_writemask_all = true;
         break;
      }

      default:
         unreachable("unknown texture source");
      }
   }

   if (instr->op == nir_texop_txf_ms) {
      if (brw->gen >= 7 && key->tex.compressed_multisample_layout_mask & (1<<sampler))
         mcs = emit_mcs_fetch(coordinate, instr->coord_components, sampler_reg);
      else
         mcs = fs_reg(0u);
   }

   for (unsigned i = 0; i < 3; i++) {
      if (instr->const_offset[i] != 0) {
         assert(offset_components == 0);
         offset = fs_reg(brw_texture_offset(ctx, instr->const_offset, 3));
         break;
      }
   }

   enum glsl_base_type dest_base_type;
   switch (instr->dest_type) {
   case nir_type_float:
      dest_base_type = GLSL_TYPE_FLOAT;
      break;
   case nir_type_int:
      dest_base_type = GLSL_TYPE_INT;
      break;
   case nir_type_unsigned:
      dest_base_type = GLSL_TYPE_UINT;
      break;
   default:
      unreachable("bad type");
   }

   const glsl_type *dest_type =
      glsl_type::get_instance(dest_base_type, nir_tex_instr_dest_size(instr),
                              1);

   ir_texture_opcode op;
   switch (instr->op) {
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

   emit_texture(op, dest_type, coordinate, instr->coord_components,
                shadow_comparitor, lod, lod2, lod_components, sample_index,
                offset, offset_components, mcs, gather_component,
                is_cube_array, is_rect, sampler, sampler_reg, texunit);

   fs_reg dest = get_nir_dest(instr->dest);
   dest.type = this->result.type;
   unsigned num_components = nir_tex_instr_dest_size(instr);
   emit_percomp(MOV(dest, this->result), (1 << num_components) - 1);
}

void
fs_visitor::nir_emit_jump(nir_jump_instr *instr)
{
   switch (instr->type) {
   case nir_jump_break:
      emit(BRW_OPCODE_BREAK);
      break;
   case nir_jump_continue:
      emit(BRW_OPCODE_CONTINUE);
      break;
   case nir_jump_return:
   default:
      unreachable("unknown jump");
   }
}
