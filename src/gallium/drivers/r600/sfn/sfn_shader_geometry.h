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


#ifndef SFN_GEOMETRYSHADERFROMNIR_H
#define SFN_GEOMETRYSHADERFROMNIR_H

#include "sfn_vertexstageexport.h"

namespace r600 {

class GeometryShaderFromNir : public VertexStage
{
public:
   GeometryShaderFromNir(r600_pipe_shader *sh, r600_pipe_shader_selector& sel, const r600_shader_key& key, enum chip_class chip_class);
   bool do_emit_load_deref(const nir_variable *in_var, nir_intrinsic_instr* instr) override;
   bool do_emit_store_deref(const nir_variable *out_var, nir_intrinsic_instr* instr) override;
   bool scan_sysvalue_access(nir_instr *instr) override;
   PValue primitive_id() override {return m_primitive_id;}

private:
   struct ArrayDeref {
      const nir_variable *var;
      const nir_src *index;
   };

   bool do_process_inputs(nir_variable *input) override;
   bool do_allocate_reserved_registers() override;
   bool do_process_outputs(nir_variable *output) override;
   bool emit_deref_instruction_override(nir_deref_instr* instr) override;
   bool emit_intrinsic_instruction_override(nir_intrinsic_instr* instr) override;
   bool emit_load_from_array(nir_intrinsic_instr* instr, const ArrayDeref& array_deref);
   bool emit_vertex(nir_intrinsic_instr* instr, bool cut);
   void emit_adj_fix();

   void do_finalize() override;

   r600_pipe_shader *m_pipe_shader;
   const pipe_stream_output_info *m_so_info;

   std::array<PValue, 6> m_per_vertex_offsets;
   PValue m_primitive_id;
   PValue m_invocation_id;
   PValue m_export_base;
   bool m_first_vertex_emitted;

   std::map<unsigned, ArrayDeref> m_in_array_deref;
   int  m_offset;
   int  m_next_input_ring_offset;
   r600_shader_key m_key;
   int m_num_clip_dist;
   unsigned m_cur_ring_output;
   bool m_gs_tri_strip_adj_fix;

   std::map<int, MemRingOutIntruction *> streamout_data;
};

}

#endif // SFN_GEOMETRYSHADERFROMNIR_H
