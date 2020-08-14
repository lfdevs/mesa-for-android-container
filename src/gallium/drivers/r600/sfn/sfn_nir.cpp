/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2019 Collabora LTD
 *
 * Author: Gert Wollny <gert.wollny@collabora.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "sfn_nir.h"
#include "nir_builder.h"

#include "../r600_pipe.h"
#include "../r600_shader.h"

#include "sfn_instruction_tex.h"

#include "sfn_shader_vertex.h"
#include "sfn_shader_fragment.h"
#include "sfn_shader_geometry.h"
#include "sfn_shader_compute.h"
#include "sfn_shader_tcs.h"
#include "sfn_shader_tess_eval.h"
#include "sfn_nir_lower_fs_out_to_vector.h"
#include "sfn_ir_to_assembly.h"

#include <vector>

namespace r600 {

using std::vector;

ShaderFromNir::ShaderFromNir():sh(nullptr),
   m_current_if_id(0),
   m_current_loop_id(0)
{
}

bool ShaderFromNir::lower(const nir_shader *shader, r600_pipe_shader *pipe_shader,
                          r600_pipe_shader_selector *sel, r600_shader_key& key,
                          struct r600_shader* gs_shader, enum chip_class _chip_class)
{
   sh = shader;
   chip_class = _chip_class;
   assert(sh);

   switch (shader->info.stage) {
   case MESA_SHADER_VERTEX:
      impl.reset(new VertexShaderFromNir(pipe_shader, *sel, key, gs_shader, chip_class));
      break;
   case MESA_SHADER_TESS_CTRL:
      sfn_log << SfnLog::trans << "Start TCS\n";
      impl.reset(new TcsShaderFromNir(pipe_shader, *sel, key, chip_class));
      break;
   case MESA_SHADER_TESS_EVAL:
      sfn_log << SfnLog::trans << "Start TESS_EVAL\n";
      impl.reset(new TEvalShaderFromNir(pipe_shader, *sel, key, gs_shader, chip_class));
      break;
   case MESA_SHADER_GEOMETRY:
      sfn_log << SfnLog::trans << "Start GS\n";
      impl.reset(new GeometryShaderFromNir(pipe_shader, *sel, key, chip_class));
      break;
   case MESA_SHADER_FRAGMENT:
      sfn_log << SfnLog::trans << "Start FS\n";
      impl.reset(new FragmentShaderFromNir(*shader, pipe_shader->shader, *sel, key, chip_class));
      break;
   case MESA_SHADER_COMPUTE:
      sfn_log << SfnLog::trans << "Start CS\n";
      impl.reset(new ComputeShaderFromNir(pipe_shader, *sel, key, chip_class));
      break;
   default:
      return false;
   }

   sfn_log << SfnLog::trans << "Process declarations\n";
   if (!process_declaration())
      return false;

   // at this point all functions should be inlined
   const nir_function *func = reinterpret_cast<const nir_function *>(exec_list_get_head_const(&sh->functions));

   sfn_log << SfnLog::trans << "Scan shader\n";
   nir_foreach_block(block, func->impl) {
      nir_foreach_instr(instr, block) {
         if (!impl->scan_instruction(instr)) {
            fprintf(stderr, "Unhandled sysvalue access ");
            nir_print_instr(instr, stderr);
            fprintf(stderr, "\n");
            return false;
         }
      }
   }

   sfn_log << SfnLog::trans << "Reserve registers\n";
   if (!impl->allocate_reserved_registers()) {
      return false;
   }

   ValuePool::array_list arrays;
   sfn_log << SfnLog::trans << "Allocate local registers\n";
   foreach_list_typed(nir_register, reg, node, &func->impl->registers) {
      impl->allocate_local_register(*reg, arrays);
   }

   sfn_log << SfnLog::trans << "Emit shader start\n";
   impl->allocate_arrays(arrays);

   impl->emit_shader_start();

   sfn_log << SfnLog::trans << "Process shader \n";
   foreach_list_typed(nir_cf_node, node, node, &func->impl->body) {
      if (!process_cf_node(node))
         return false;
   }

   // Add optimizations here
   sfn_log << SfnLog::trans << "Finalize\n";
   impl->finalize();

   if (!sfn_log.has_debug_flag(SfnLog::nomerge)) {
      sfn_log << SfnLog::trans << "Merge registers\n";
      impl->remap_registers();
   }
   sfn_log << SfnLog::trans << "Finished translating to R600 IR\n";
   return true;
}

Shader ShaderFromNir::shader() const
{
   return Shader{impl->m_output, impl->get_temp_registers()};
}


bool ShaderFromNir::process_cf_node(nir_cf_node *node)
{
   SFN_TRACE_FUNC(SfnLog::flow, "CF");
   switch (node->type) {
   case nir_cf_node_block:
      return process_block(nir_cf_node_as_block(node));
   case nir_cf_node_if:
      return process_if(nir_cf_node_as_if(node));
   case nir_cf_node_loop:
      return process_loop(nir_cf_node_as_loop(node));
   default:
      return false;
   }
}

bool ShaderFromNir::process_if(nir_if *if_stmt)
{
   SFN_TRACE_FUNC(SfnLog::flow, "IF");

   if (!impl->emit_if_start(m_current_if_id, if_stmt))
      return false;

   int if_id = m_current_if_id++;
   m_if_stack.push(if_id);

   foreach_list_typed(nir_cf_node, n, node, &if_stmt->then_list)
         if (!process_cf_node(n)) return false;

   if (!if_stmt->then_list.is_empty()) {
      if (!impl->emit_else_start(if_id))
         return false;

      foreach_list_typed(nir_cf_node, n, node, &if_stmt->else_list)
            if (!process_cf_node(n)) return false;
   }

   if (!impl->emit_ifelse_end(if_id))
      return false;

   m_if_stack.pop();
   return true;
}

bool ShaderFromNir::process_loop(nir_loop *node)
{
   SFN_TRACE_FUNC(SfnLog::flow, "LOOP");
   int loop_id = m_current_loop_id++;

   if (!impl->emit_loop_start(loop_id))
      return false;

   foreach_list_typed(nir_cf_node, n, node, &node->body)
         if (!process_cf_node(n)) return false;

   if (!impl->emit_loop_end(loop_id))
      return false;

   return true;
}

bool ShaderFromNir::process_block(nir_block *block)
{
   SFN_TRACE_FUNC(SfnLog::flow, "BLOCK");
   nir_foreach_instr(instr, block) {
      int r = emit_instruction(instr);
      if (!r) {
         sfn_log << SfnLog::err << "R600: Unsupported instruction: "
                 << *instr << "\n";
         return false;
      }
   }
   return true;
}


ShaderFromNir::~ShaderFromNir()
{
}

pipe_shader_type ShaderFromNir::processor_type() const
{
   return impl->m_processor_type;
}


bool ShaderFromNir::emit_instruction(nir_instr *instr)
{
   assert(impl);

   sfn_log << SfnLog::instr << "Read instruction " << *instr << "\n";

   switch (instr->type) {
   case nir_instr_type_alu:
      return impl->emit_alu_instruction(instr);
   case nir_instr_type_deref:
      return impl->emit_deref_instruction(nir_instr_as_deref(instr));
   case nir_instr_type_intrinsic:
      return impl->emit_intrinsic_instruction(nir_instr_as_intrinsic(instr));
   case nir_instr_type_load_const:
      return impl->set_literal_constant(nir_instr_as_load_const(instr));
   case nir_instr_type_tex:
      return impl->emit_tex_instruction(instr);
   case nir_instr_type_jump:
      return impl->emit_jump_instruction(nir_instr_as_jump(instr));
   default:
      fprintf(stderr, "R600: %s: ShaderFromNir Unsupported instruction: type %d:'", __func__, instr->type);
      nir_print_instr(instr, stderr);
      fprintf(stderr, "'\n");
      return false;
   case nir_instr_type_ssa_undef:
      return impl->create_undef(nir_instr_as_ssa_undef(instr));
      return true;
   }
}

bool ShaderFromNir::process_declaration()
{
   // scan declarations
   nir_foreach_shader_in_variable(variable, sh) {
      if (!impl->process_inputs(variable)) {
         fprintf(stderr, "R600: error parsing input varible %s\n", variable->name);
         return false;
      }
   }

   // scan declarations
   nir_foreach_shader_out_variable(variable, sh) {
      if (!impl->process_outputs(variable)) {
         fprintf(stderr, "R600: error parsing outputs varible %s\n", variable->name);
         return false;
      }
   }

   // scan declarations
   nir_foreach_variable_with_modes(variable, sh, nir_var_uniform |
                                                 nir_var_mem_ubo |
                                                 nir_var_mem_ssbo) {
      if (!impl->process_uniforms(variable)) {
         fprintf(stderr, "R600: error parsing outputs varible %s\n", variable->name);
         return false;
      }
   }

   return true;
}

const std::vector<InstructionBlock>& ShaderFromNir::shader_ir() const
{
   assert(impl);
   return impl->m_output;
}


AssemblyFromShader::~AssemblyFromShader()
{
}

bool AssemblyFromShader::lower(const std::vector<InstructionBlock>& ir)
{
   return do_lower(ir);
}

static nir_ssa_def *
r600_nir_lower_pack_unpack_2x16_impl(nir_builder *b, nir_instr *instr, void *_options)
{
   nir_alu_instr *alu = nir_instr_as_alu(instr);

   switch (alu->op) {
   case nir_op_unpack_half_2x16: {
      nir_ssa_def *packed = nir_ssa_for_alu_src(b, alu, 0);
      return  nir_vec2(b, nir_unpack_half_2x16_split_x(b, packed),
                       nir_unpack_half_2x16_split_y(b, packed));

   }
   case nir_op_pack_half_2x16: {
      nir_ssa_def *src_vec2 = nir_ssa_for_alu_src(b, alu, 0);
      return nir_pack_half_2x16_split(b, nir_channel(b, src_vec2, 0),
                                      nir_channel(b, src_vec2, 1));
   }
   default:
      return nullptr;
   }
}

bool r600_nir_lower_pack_unpack_2x16_filter(const nir_instr *instr, const void *_options)
{
   return instr->type == nir_instr_type_alu;
}

bool r600_nir_lower_pack_unpack_2x16(nir_shader *shader)
{
   return nir_shader_lower_instructions(shader,
                                        r600_nir_lower_pack_unpack_2x16_filter,
                                        r600_nir_lower_pack_unpack_2x16_impl,
                                        nullptr);
};

static void
r600_nir_lower_scratch_address_impl(nir_builder *b, nir_intrinsic_instr *instr)
{
   b->cursor = nir_before_instr(&instr->instr);

   int address_index = 0;
   int align;

   if (instr->intrinsic == nir_intrinsic_store_scratch) {
      align  = instr->src[0].ssa->num_components;
      address_index = 1;
   } else{
      align = instr->dest.ssa.num_components;
   }

   nir_ssa_def *address = instr->src[address_index].ssa;
   nir_ssa_def *new_address = nir_ishr(b, address,  nir_imm_int(b, 4 * align));

   nir_instr_rewrite_src(&instr->instr, &instr->src[address_index],
                         nir_src_for_ssa(new_address));
}

bool r600_lower_scratch_addresses(nir_shader *shader)
{
   bool progress = false;
   nir_foreach_function(function, shader) {
      nir_builder build;
      nir_builder_init(&build, function->impl);

      nir_foreach_block(block, function->impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            nir_intrinsic_instr *op = nir_instr_as_intrinsic(instr);
            if (op->intrinsic != nir_intrinsic_load_scratch &&
                op->intrinsic != nir_intrinsic_store_scratch)
               continue;
            r600_nir_lower_scratch_address_impl(&build, op);
            progress = true;
         }
      }
   }
   return progress;
}

static nir_ssa_def *
r600_lower_ubo_to_align16_impl(nir_builder *b, nir_instr *instr, void *_options)
{
   b->cursor = nir_before_instr(instr);

   nir_intrinsic_instr *op = nir_instr_as_intrinsic(instr);
   assert(op->intrinsic == nir_intrinsic_load_ubo);

   bool const_address = (nir_src_is_const(op->src[1]) && nir_src_is_const(op->src[0]));

   nir_ssa_def *offset = op->src[1].ssa;

   /* This is ugly: With const addressing we can actually set a proper fetch target mask,
    * but for this we need the component encoded, we don't shift and do de decoding in the
    * backend. Otherwise we shift by four and resolve the component here
    * (TODO: encode the start component in the intrinsic when the offset base is non-constant
    * but a multiple of 16 */

   nir_ssa_def *new_offset = offset;
   if (!const_address)
      new_offset = nir_ishr(b, offset,  nir_imm_int(b, 4));

   nir_intrinsic_instr *load = nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_ubo_r600);
   load->num_components = const_address ? op->num_components : 4;
   load->src[0] = op->src[0];
   load->src[1] = nir_src_for_ssa(new_offset);
   nir_intrinsic_set_align(load, nir_intrinsic_align_mul(op), nir_intrinsic_align_offset(op));

   nir_ssa_dest_init(&load->instr, &load->dest, load->num_components, 32, NULL);
   nir_builder_instr_insert(b, &load->instr);

   /* when four components are loaded or both the offset and the location
    * are constant, then the backend can deal with it better */
   if (op->num_components == 4 || const_address)
      return &load->dest.ssa;

   /* What comes below is a performance disaster when the offset is not constant
    * because then we have to assume that any component can be the first one and we
    * have to pick the result manually. */
   nir_ssa_def *first_comp = nir_iand(b, nir_ishr(b, offset,  nir_imm_int(b, 2)),
                                      nir_imm_int(b,3));

   const unsigned swz_000[4] = {0, 0, 0};
   nir_ssa_def *component_select = nir_ieq(b, r600_imm_ivec3(b, 0, 1, 2),
                                           nir_swizzle(b, first_comp, swz_000, 3));

   if (op->num_components == 1) {
      nir_ssa_def *check0 = nir_bcsel(b, nir_channel(b, component_select, 0),
                                      nir_channel(b, &load->dest.ssa, 0),
                                      nir_channel(b, &load->dest.ssa, 3));
      nir_ssa_def *check1 = nir_bcsel(b, nir_channel(b, component_select, 1),
                                      nir_channel(b, &load->dest.ssa, 1),
                                      check0);
      return nir_bcsel(b, nir_channel(b, component_select, 2),
                       nir_channel(b, &load->dest.ssa, 2),
                       check1);
   } else if (op->num_components == 2) {
      const unsigned szw_01[2] = {0, 1};
      const unsigned szw_12[2] = {1, 2};
      const unsigned szw_23[2] = {2, 3};

      nir_ssa_def *check0 = nir_bcsel(b, nir_channel(b, component_select, 0),
                                      nir_swizzle(b, &load->dest.ssa, szw_01, 2),
                                      nir_swizzle(b, &load->dest.ssa, szw_23, 2));
      return nir_bcsel(b, nir_channel(b, component_select, 1),
                                      nir_swizzle(b, &load->dest.ssa, szw_12, 2),
                                      check0);
   } else {
      const unsigned szw_012[3] = {0, 1, 2};
      const unsigned szw_123[3] = {1, 2, 3};
      return nir_bcsel(b, nir_channel(b, component_select, 0),
                       nir_swizzle(b, &load->dest.ssa, szw_012, 3),
                       nir_swizzle(b, &load->dest.ssa, szw_123, 3));
   }
}

bool r600_lower_ubo_to_align16_filter(const nir_instr *instr, const void *_options)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *op = nir_instr_as_intrinsic(instr);
   return op->intrinsic == nir_intrinsic_load_ubo;
}


bool r600_lower_ubo_to_align16(nir_shader *shader)
{
   return nir_shader_lower_instructions(shader,
                                        r600_lower_ubo_to_align16_filter,
                                        r600_lower_ubo_to_align16_impl,
                                        nullptr);
}

}

using r600::r600_nir_lower_int_tg4;
using r600::r600_nir_lower_pack_unpack_2x16;
using r600::r600_lower_scratch_addresses;
using r600::r600_lower_fs_out_to_vector;
using r600::r600_lower_ubo_to_align16;

int
r600_glsl_type_size(const struct glsl_type *type, bool is_bindless)
{
   return glsl_count_vec4_slots(type, false, is_bindless);
}

void
r600_get_natural_size_align_bytes(const struct glsl_type *type,
                                  unsigned *size, unsigned *align)
{
   if (type->base_type != GLSL_TYPE_ARRAY) {
      *align = 1;
      *size = 1;
   } else {
      unsigned elem_size, elem_align;
      glsl_get_natural_size_align_bytes(type->fields.array,
                                        &elem_size, &elem_align);
      *align = 1;
      *size = type->length;
   }
}

static bool
r600_lower_shared_io_impl(nir_function *func)
{
   nir_builder b;
   nir_builder_init(&b, func->impl);

   bool progress = false;
   nir_foreach_block(block, func->impl) {
      nir_foreach_instr_safe(instr, block) {

         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *op = nir_instr_as_intrinsic(instr);
         if (op->intrinsic != nir_intrinsic_load_shared &&
             op->intrinsic != nir_intrinsic_store_shared)
            continue;

         b.cursor = nir_before_instr(instr);

         if (op->intrinsic == nir_intrinsic_load_shared) {
            nir_ssa_def *addr = op->src[0].ssa;

            switch (nir_dest_num_components(op->dest)) {
            case 2: {
               auto addr2 = nir_iadd_imm(&b, addr, 4);
               addr = nir_vec2(&b, addr, addr2);
               break;
            }
            case 3: {
               auto addr2 = nir_iadd(&b, addr, nir_imm_ivec2(&b, 4, 8));
               addr = nir_vec3(&b, addr,
                               nir_channel(&b, addr2, 0),
                               nir_channel(&b, addr2, 1));
               break;
            }
            case 4: {
               addr = nir_iadd(&b, addr, nir_imm_ivec4(&b, 0, 4, 8, 12));
               break;
            }
            }

            auto load = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_local_shared_r600);
            load->num_components = nir_dest_num_components(op->dest);
            load->src[0] = nir_src_for_ssa(addr);
            nir_ssa_dest_init(&load->instr, &load->dest,
                              load->num_components, 32, NULL);
            nir_ssa_def_rewrite_uses(&op->dest.ssa, nir_src_for_ssa(&load->dest.ssa));
            nir_builder_instr_insert(&b, &load->instr);
         } else {
            nir_ssa_def *addr = op->src[1].ssa;
            for (int i = 0; i < 2; ++i) {
               unsigned test_mask = (0x3 << 2 * i);
               if (!(nir_intrinsic_write_mask(op) & test_mask))
                  continue;

               auto store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_store_local_shared_r600);
               unsigned writemask = nir_intrinsic_write_mask(op) & test_mask;
               nir_intrinsic_set_write_mask(store, writemask);
               store->src[0] = nir_src_for_ssa(op->src[0].ssa);
               store->num_components = store->src[0].ssa->num_components;
               bool start_even = (writemask & (1u << (2 * i)));

               auto addr2 = nir_iadd(&b, addr, nir_imm_int(&b, 8 * i + (start_even ? 0 : 4)));
               store->src[1] = nir_src_for_ssa(addr2);

               nir_builder_instr_insert(&b, &store->instr);
            }
         }
         nir_instr_remove(instr);
         progress = true;
      }
   }
   return progress;
}

static bool
r600_lower_shared_io(nir_shader *nir)
{
	bool progress=false;
	nir_foreach_function(function, nir) {
		if (function->impl &&
			 r600_lower_shared_io_impl(function))
			progress = true;
	}
	return progress;
}

static bool
optimize_once(nir_shader *shader)
{
   bool progress = false;
   NIR_PASS(progress, shader, nir_copy_prop);
   NIR_PASS(progress, shader, nir_opt_dce);
   NIR_PASS(progress, shader, nir_opt_algebraic);
   NIR_PASS(progress, shader, nir_opt_constant_folding);
   NIR_PASS(progress, shader, nir_opt_copy_prop_vars);
   NIR_PASS(progress, shader, nir_opt_vectorize);

   NIR_PASS(progress, shader, nir_opt_remove_phis);

   if (nir_opt_trivial_continues(shader)) {
           progress = true;
           NIR_PASS(progress, shader, nir_copy_prop);
           NIR_PASS(progress, shader, nir_opt_dce);
   }

   NIR_PASS(progress, shader, nir_opt_if, false);
   NIR_PASS(progress, shader, nir_opt_dead_cf);
   NIR_PASS(progress, shader, nir_opt_cse);
   NIR_PASS(progress, shader, nir_opt_peephole_select, 200, true, true);

   NIR_PASS(progress, shader, nir_opt_conditional_discard);
   NIR_PASS(progress, shader, nir_opt_dce);
   NIR_PASS(progress, shader, nir_opt_undef);
   return progress;
}

bool has_saturate(const nir_function *func)
{
   nir_foreach_block(block, func->impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_alu) {
            auto alu = nir_instr_as_alu(instr);
            if (alu->dest.saturate)
               return true;
         }
      }
   }
   return false;
}

int r600_shader_from_nir(struct r600_context *rctx,
                         struct r600_pipe_shader *pipeshader,
                         r600_shader_key *key)
{
   char filename[4000];
   struct r600_pipe_shader_selector *sel = pipeshader->selector;

   r600::ShaderFromNir convert;

   if (rctx->screen->b.debug_flags & DBG_PREOPT_IR) {
      fprintf(stderr, "PRE-OPT-NIR-----------.------------------------------\n");
      nir_print_shader(sel->nir, stderr);
      fprintf(stderr, "END PRE-OPT-NIR--------------------------------------\n\n");
   }

   NIR_PASS_V(sel->nir, nir_lower_vars_to_ssa);
   NIR_PASS_V(sel->nir, nir_lower_regs_to_ssa);
   NIR_PASS_V(sel->nir, nir_lower_phis_to_scalar);

   NIR_PASS_V(sel->nir, r600_lower_shared_io);

   static const struct nir_lower_tex_options lower_tex_options = {
      .lower_txp = ~0u,
   };
   NIR_PASS_V(sel->nir, nir_lower_tex, &lower_tex_options);
   NIR_PASS_V(sel->nir, r600::r600_nir_lower_txl_txf_array_or_cube);

   NIR_PASS_V(sel->nir, r600_nir_lower_int_tg4);
   NIR_PASS_V(sel->nir, r600_nir_lower_pack_unpack_2x16);

   NIR_PASS_V(sel->nir, nir_lower_io, nir_var_uniform, r600_glsl_type_size,
              nir_lower_io_lower_64bit_to_32);

   if (sel->nir->info.stage == MESA_SHADER_VERTEX)
      NIR_PASS_V(sel->nir, r600_vectorize_vs_inputs);

   if (sel->nir->info.stage == MESA_SHADER_FRAGMENT)
      NIR_PASS_V(sel->nir, r600_lower_fs_out_to_vector);

   if (sel->nir->info.stage == MESA_SHADER_TESS_CTRL ||
       (sel->nir->info.stage == MESA_SHADER_VERTEX && key->vs.as_ls)) {
      NIR_PASS_V(sel->nir, nir_lower_io, nir_var_shader_out, r600_glsl_type_size,
                 nir_lower_io_lower_64bit_to_32);
      NIR_PASS_V(sel->nir, r600_lower_tess_io, (pipe_prim_type)key->tcs.prim_mode);
   }

   if (sel->nir->info.stage == MESA_SHADER_TESS_CTRL ||
       sel->nir->info.stage == MESA_SHADER_TESS_EVAL) {
      NIR_PASS_V(sel->nir, nir_lower_io, nir_var_shader_in, r600_glsl_type_size,
                 nir_lower_io_lower_64bit_to_32);
   }

   if (sel->nir->info.stage == MESA_SHADER_TESS_CTRL ||
       sel->nir->info.stage == MESA_SHADER_TESS_EVAL ||
       (sel->nir->info.stage == MESA_SHADER_VERTEX && key->vs.as_ls)) {
      auto prim_type = sel->nir->info.stage == MESA_SHADER_TESS_CTRL ?
                          key->tcs.prim_mode : sel->nir->info.tess.primitive_mode;
      NIR_PASS_V(sel->nir, r600_lower_tess_io, static_cast<pipe_prim_type>(prim_type));
   }


   if (sel->nir->info.stage == MESA_SHADER_TESS_CTRL)
      NIR_PASS_V(sel->nir, r600_append_tcs_TF_emission,
                 (pipe_prim_type)key->tcs.prim_mode);


   const nir_function *func = reinterpret_cast<const nir_function *>(exec_list_get_head_const(&sel->nir->functions));
   bool optimize = func->impl->registers.length() == 0 && !has_saturate(func);

   if (optimize) {
      optimize_once(sel->nir);
      NIR_PASS_V(sel->nir, r600_lower_ubo_to_align16);
   }
   /* It seems the output of this optimization is cached somewhere, and
    * when there are registers, then we can no longer copy propagate, so
    * skip the optimization then. (There is probably a better way, but yeah)
    */
   if (optimize)
      while(optimize_once(sel->nir));

   NIR_PASS_V(sel->nir, nir_remove_dead_variables, nir_var_shader_in, NULL);
   NIR_PASS_V(sel->nir, nir_remove_dead_variables,  nir_var_shader_out, NULL);


   NIR_PASS_V(sel->nir, nir_lower_vars_to_scratch,
              nir_var_function_temp,
              40,
              r600_get_natural_size_align_bytes);

   while (optimize && optimize_once(sel->nir));

   NIR_PASS_V(sel->nir, nir_lower_locals_to_regs);
   //NIR_PASS_V(sel->nir, nir_opt_algebraic);
   //NIR_PASS_V(sel->nir, nir_copy_prop);
   NIR_PASS_V(sel->nir, nir_lower_to_source_mods, nir_lower_float_source_mods);
   NIR_PASS_V(sel->nir, nir_convert_from_ssa, true);
   NIR_PASS_V(sel->nir, nir_opt_dce);

   if ((rctx->screen->b.debug_flags & DBG_NIR) &&
       (rctx->screen->b.debug_flags & DBG_ALL_SHADERS)) {
      fprintf(stderr, "-- NIR --------------------------------------------------------\n");
      struct nir_function *func = (struct nir_function *)exec_list_get_head(&sel->nir->functions);
      nir_index_ssa_defs(func->impl);
      nir_print_shader(sel->nir, stderr);
      fprintf(stderr, "-- END --------------------------------------------------------\n");
   }

   memset(&pipeshader->shader, 0, sizeof(r600_shader));
   pipeshader->scratch_space_needed = sel->nir->scratch_size;

   if (sel->nir->info.stage == MESA_SHADER_TESS_EVAL ||
       sel->nir->info.stage == MESA_SHADER_VERTEX ||
       sel->nir->info.stage == MESA_SHADER_GEOMETRY) {
      pipeshader->shader.clip_dist_write |= ((1 << sel->nir->info.clip_distance_array_size) - 1);
      pipeshader->shader.cull_dist_write = ((1 << sel->nir->info.cull_distance_array_size) - 1)
                                           << sel->nir->info.clip_distance_array_size;
      pipeshader->shader.cc_dist_mask = (1 <<  (sel->nir->info.cull_distance_array_size +
                                                sel->nir->info.clip_distance_array_size)) - 1;
   }

   struct r600_shader* gs_shader = nullptr;
   if (rctx->gs_shader)
      gs_shader = &rctx->gs_shader->current->shader;
   r600_screen *rscreen = rctx->screen;

   bool r = convert.lower(sel->nir, pipeshader, sel, *key, gs_shader, rscreen->b.chip_class);
   if (!r || rctx->screen->b.debug_flags & DBG_ALL_SHADERS) {
      static int shnr = 0;

      snprintf(filename, 4000, "nir-%s_%d.inc", sel->nir->info.name, shnr++);

      if (access(filename, F_OK) == -1) {
         FILE *f = fopen(filename, "w");

         if (f) {
            fprintf(f, "const char *shader_blob_%s = {\nR\"(", sel->nir->info.name);
            nir_print_shader(sel->nir, f);
            fprintf(f, ")\";\n");
            fclose(f);
         }
      }
      if (!r)
         return -2;
   }

   auto shader = convert.shader();

   r600_bytecode_init(&pipeshader->shader.bc, rscreen->b.chip_class, rscreen->b.family,
                      rscreen->has_compressed_msaa_texturing);

   r600::sfn_log << r600::SfnLog::shader_info
                 << "pipeshader->shader.processor_type = "
                 << pipeshader->shader.processor_type << "\n";

   pipeshader->shader.bc.type = pipeshader->shader.processor_type;
   pipeshader->shader.bc.isa = rctx->isa;

   r600::AssemblyFromShaderLegacy afs(&pipeshader->shader, key);
   if (!afs.lower(shader.m_ir)) {
      R600_ERR("%s: Lowering to assembly failed\n", __func__);
      return -1;
   }

   if (sel->nir->info.stage == MESA_SHADER_GEOMETRY) {
      r600::sfn_log << r600::SfnLog::shader_info << "Geometry shader, create copy shader\n";
      generate_gs_copy_shader(rctx, pipeshader, &sel->so);
      assert(pipeshader->gs_copy_shader);
   } else {
      r600::sfn_log << r600::SfnLog::shader_info << "This is not a Geometry shader\n";
   }

   return 0;
}
