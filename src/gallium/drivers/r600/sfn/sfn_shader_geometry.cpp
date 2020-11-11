/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2018 Collabora LTD
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

#include "sfn_shader_geometry.h"
#include "sfn_instruction_misc.h"
#include "sfn_instruction_fetch.h"
#include "sfn_shaderio.h"

namespace r600 {

GeometryShaderFromNir::GeometryShaderFromNir(r600_pipe_shader *sh,
                                             r600_pipe_shader_selector &sel,
                                             const r600_shader_key &key,
                                             enum chip_class chip_class):
   VertexStage(PIPE_SHADER_GEOMETRY, sel, sh->shader,
               sh->scratch_space_needed, chip_class, key.gs.first_atomic_counter),
   m_pipe_shader(sh),
   m_so_info(&sel.so),
   m_first_vertex_emitted(false),
   m_offset(0),
   m_next_input_ring_offset(0),
   m_key(key),
   m_num_clip_dist(0),
   m_cur_ring_output(0),
   m_gs_tri_strip_adj_fix(false)
{
   sh_info().atomic_base = key.gs.first_atomic_counter;
}

bool GeometryShaderFromNir::do_emit_load_deref(UNUSED const nir_variable *in_var, UNUSED nir_intrinsic_instr* instr)
{
   return false;
}

bool GeometryShaderFromNir::do_emit_store_deref(const nir_variable *out_var, nir_intrinsic_instr* instr)
{
   uint32_t write_mask = nir_intrinsic_write_mask(instr);
   GPRVector::Swizzle swz = swizzle_from_mask(write_mask);
   auto out_value = vec_from_nir_with_fetch_constant(instr->src[1], write_mask, swz, true);

   sh_info().output[out_var->data.driver_location].write_mask = write_mask;

   auto ir = new MemRingOutIntruction(cf_mem_ring, mem_write_ind, out_value,
                                      4 * out_var->data.driver_location,
                                      instr->num_components, m_export_base);

   streamout_data[out_var->data.location] = ir;

   return true;
}

bool GeometryShaderFromNir::scan_sysvalue_access(UNUSED nir_instr *instr)
{
   return true;
}

bool GeometryShaderFromNir::do_process_inputs(nir_variable *input)
{

   if (input->data.location == VARYING_SLOT_POS ||
       input->data.location == VARYING_SLOT_PSIZ ||
       input->data.location == VARYING_SLOT_FOGC ||
       input->data.location == VARYING_SLOT_CLIP_VERTEX ||
       input->data.location == VARYING_SLOT_CLIP_DIST0 ||
       input->data.location == VARYING_SLOT_CLIP_DIST1 ||
       input->data.location == VARYING_SLOT_COL0 ||
       input->data.location == VARYING_SLOT_COL1 ||
       input->data.location == VARYING_SLOT_BFC0 ||
       input->data.location == VARYING_SLOT_BFC1 ||
       input->data.location == VARYING_SLOT_PNTC ||
       (input->data.location >= VARYING_SLOT_VAR0 &&
       input->data.location <= VARYING_SLOT_VAR31) ||
       (input->data.location >= VARYING_SLOT_TEX0 &&
       input->data.location <= VARYING_SLOT_TEX7)) {

      r600_shader_io& io = sh_info().input[input->data.driver_location];
      auto semantic = r600_get_varying_semantic(input->data.location);
      io.name = semantic.first;
      io.sid = semantic.second;

      io.ring_offset = 16 * input->data.driver_location;
      ++sh_info().ninput;
      m_next_input_ring_offset += 16;
      return true;
   }

   return false;
}

bool GeometryShaderFromNir::do_process_outputs(nir_variable *output)
{
   if (output->data.location == VARYING_SLOT_COL0 ||
       output->data.location == VARYING_SLOT_COL1 ||
       (output->data.location >= VARYING_SLOT_VAR0 &&
       output->data.location <= VARYING_SLOT_VAR31) ||
       (output->data.location >= VARYING_SLOT_TEX0 &&
       output->data.location <= VARYING_SLOT_TEX7) ||
       output->data.location == VARYING_SLOT_BFC0 ||
       output->data.location == VARYING_SLOT_BFC1 ||
       output->data.location == VARYING_SLOT_PNTC ||
       output->data.location == VARYING_SLOT_CLIP_VERTEX ||
       output->data.location == VARYING_SLOT_CLIP_DIST0 ||
       output->data.location == VARYING_SLOT_CLIP_DIST1 ||
       output->data.location == VARYING_SLOT_PRIMITIVE_ID ||
       output->data.location == VARYING_SLOT_POS ||
       output->data.location == VARYING_SLOT_PSIZ ||
       output->data.location == VARYING_SLOT_LAYER ||
       output->data.location == VARYING_SLOT_VIEWPORT ||
       output->data.location == VARYING_SLOT_FOGC) {
      r600_shader_io& io = sh_info().output[output->data.driver_location];

      auto semantic = r600_get_varying_semantic(output->data.location);
      io.name = semantic.first;
      io.sid = semantic.second;

      evaluate_spi_sid(io);
      ++sh_info().noutput;

      if (output->data.location == VARYING_SLOT_CLIP_DIST0 ||
          output->data.location == VARYING_SLOT_CLIP_DIST1) {
         m_num_clip_dist += 4;
      }

      if (output->data.location == VARYING_SLOT_VIEWPORT) {
         sh_info().vs_out_viewport = 1;
         sh_info().vs_out_misc_write = 1;
      }
      return true;
   }
   return false;
}


bool GeometryShaderFromNir::do_allocate_reserved_registers()
{
   const int sel[6] = {0, 0 ,0, 1, 1, 1};
   const int chan[6] = {0, 1 ,3, 0, 1, 2};

   increment_reserved_registers();
   increment_reserved_registers();

   /* Reserve registers used by the shaders (should check how many
    * components are actually used */
   for (int i = 0; i < 6; ++i) {
      auto reg = new GPRValue(sel[i], chan[i]);
      reg->set_as_input();
      m_per_vertex_offsets[i].reset(reg);
      inject_register(sel[i], chan[i], m_per_vertex_offsets[i], false);
   }
   auto reg = new GPRValue(0, 2);
   reg->set_as_input();
   m_primitive_id.reset(reg);
   inject_register(0, 2, m_primitive_id, false);

   reg = new GPRValue(1, 3);
   reg->set_as_input();
   m_invocation_id.reset(reg);
   inject_register(1, 3, m_invocation_id, false);

   m_export_base = get_temp_register();
   emit_instruction(new AluInstruction(op1_mov, m_export_base, Value::zero, {alu_write, alu_last_instr}));

   sh_info().ring_item_sizes[0] = m_next_input_ring_offset;

   if (m_key.gs.tri_strip_adj_fix)
      emit_adj_fix();

   return true;
}

void GeometryShaderFromNir::emit_adj_fix()
{
   PValue adjhelp0(new  GPRValue(m_export_base->sel(), 1));
   emit_instruction(op2_and_int, adjhelp0, {m_primitive_id, Value::one_i}, {alu_write, alu_last_instr});

   int help2 = allocate_temp_register();
   int reg_indices[6];
   int reg_chanels[6] = {0, 1, 2, 3, 2, 3};

   int rotate_indices[6] = {4, 5, 0, 1, 2, 3};

   reg_indices[0] = reg_indices[1] = reg_indices[2] = reg_indices[3] = help2;
   reg_indices[4] = reg_indices[5] = m_export_base->sel();

   std::array<PValue, 6> adjhelp;

   AluInstruction *ir = nullptr;
   for (int i = 0; i < 6; i++) {
      adjhelp[i].reset(new GPRValue(reg_indices[i], reg_chanels[i]));
      ir = new AluInstruction(op3_cnde_int, adjhelp[i],
                             {adjhelp0, m_per_vertex_offsets[i],
                              m_per_vertex_offsets[rotate_indices[i]]},
                             {alu_write});
      if (i == 3)
         ir->set_flag(alu_last_instr);
      emit_instruction(ir);
   }
   ir->set_flag(alu_last_instr);

   for (int i = 0; i < 6; i++)
      m_per_vertex_offsets[i] = adjhelp[i];
}

bool GeometryShaderFromNir::emit_deref_instruction_override(nir_deref_instr* instr)
{
   if (instr->deref_type  == nir_deref_type_array) {
      auto var = get_deref_location(instr->parent);
      ArrayDeref ad = {var, &instr->arr.index};
      assert(instr->dest.is_ssa);
      m_in_array_deref[instr->dest.ssa.index] = ad;

      /* Problem: nir_intrinsice_load_deref tries to lookup the
       * variable, and will not find it, need to override that too */
      return true;
   }
   return false;
}

bool GeometryShaderFromNir::emit_intrinsic_instruction_override(nir_intrinsic_instr* instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_deref: {
      auto& src = instr->src[0];
      assert(src.is_ssa);
      auto array = m_in_array_deref.find(src.ssa->index);
      if (array != m_in_array_deref.end())
         return emit_load_from_array(instr, array->second);
   } break;
   case nir_intrinsic_emit_vertex:
      return emit_vertex(instr, false);
   case nir_intrinsic_end_primitive:
      return emit_vertex(instr, true);
   case nir_intrinsic_load_primitive_id:
      return load_preloaded_value(instr->dest, 0, m_primitive_id);
   case nir_intrinsic_load_invocation_id:
      return load_preloaded_value(instr->dest, 0, m_invocation_id);
   default:
      ;
   }
   return false;
}

bool GeometryShaderFromNir::emit_vertex(nir_intrinsic_instr* instr, bool cut)
{
   int stream = nir_intrinsic_stream_id(instr);
   assert(stream < 4);

   for(auto v: streamout_data) {
      if (stream == 0 || v.first != VARYING_SLOT_POS) {
         v.second->patch_ring(stream);
         emit_instruction(v.second);
      } else
         delete v.second;
   }
   streamout_data.clear();
   emit_instruction(new EmitVertex(stream, cut));

   if (!cut)
      emit_instruction(new AluInstruction(op2_add_int, m_export_base, m_export_base,
                                          PValue(new LiteralValue(sh_info().noutput)),
                                          {alu_write, alu_last_instr}));

   return true;
}

bool GeometryShaderFromNir::emit_load_from_array(nir_intrinsic_instr* instr,
                                                 const ArrayDeref& array_deref)
{
   auto dest = vec_from_nir(instr->dest, instr->num_components);

   auto literal_index = nir_src_as_const_value(*array_deref.index);

   if (!literal_index) {
      sfn_log << SfnLog::err << "GS: Indirect input addressing not (yet) supported\n";
      return false;
   }
   assert(literal_index->u32 < 6);

   PValue addr = m_per_vertex_offsets[literal_index->u32];
   auto fetch = new FetchInstruction(vc_fetch, no_index_offset, dest, addr,
                                     16 * array_deref.var->data.driver_location,
                                     R600_GS_RING_CONST_BUFFER, PValue(), bim_none, true);
   emit_instruction(fetch);
   return true;
}

void GeometryShaderFromNir::do_finalize()
{
   if (m_num_clip_dist) {
      sh_info().cc_dist_mask = (1 << m_num_clip_dist) - 1;
      sh_info().clip_dist_write = (1 << m_num_clip_dist) - 1;
   }
}

}
