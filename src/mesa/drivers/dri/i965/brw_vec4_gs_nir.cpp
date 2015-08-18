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
#include "brw_vec4_gs_visitor.h"

namespace brw {

void
vec4_gs_visitor::nir_setup_inputs(nir_shader *shader)
{
   nir_inputs = ralloc_array(mem_ctx, src_reg, shader->num_inputs);

   foreach_list_typed(nir_variable, var, node, &shader->inputs) {
      int offset = var->data.driver_location;
      if (var->type->base_type == GLSL_TYPE_ARRAY) {
         /* Geometry shader inputs are arrays, but they use an unusual array
          * layout: instead of all array elements for a given geometry shader
          * input being stored consecutively, all geometry shader inputs are
          * interleaved into one giant array. At this stage of compilation, we
          * assume that the stride of the array is BRW_VARYING_SLOT_COUNT.
          * Later, setup_attributes() will remap our accesses to the actual
          * input array.
          */
         assert(var->type->length > 0);
         int length = var->type->length;
         int size = type_size(var->type) / length;
         for (int i = 0; i < length; i++) {
            int location = var->data.location + i * BRW_VARYING_SLOT_COUNT;
            for (int j = 0; j < size; j++) {
               src_reg src = src_reg(ATTR, location + j, var->type);
               src = retype(src, brw_type_for_base_type(var->type));
               nir_inputs[offset] = src;
               offset++;
            }
         }
      } else {
         int size = type_size(var->type);
         for (int i = 0; i < size; i++) {
            src_reg src = src_reg(ATTR, var->data.location + i, var->type);
            src = retype(src, brw_type_for_base_type(var->type));
            nir_inputs[offset] = src;
            offset++;
         }
      }
   }
}

void
vec4_gs_visitor::nir_setup_system_value_intrinsic(nir_intrinsic_instr *instr)
{
   dst_reg *reg;

   switch (instr->intrinsic) {
   case nir_intrinsic_load_invocation_id:
      reg = &this->nir_system_values[SYSTEM_VALUE_INVOCATION_ID];
      if (reg->file == BAD_FILE)
         *reg = *this->make_reg_for_system_value(SYSTEM_VALUE_INVOCATION_ID,
                                                 glsl_type::int_type);
      break;

   default:
      vec4_visitor::nir_setup_system_value_intrinsic(instr);
   }

}

void
vec4_gs_visitor::nir_emit_intrinsic(nir_intrinsic_instr *instr)
{
   dst_reg dest;
   src_reg src;

   switch (instr->intrinsic) {
   case nir_intrinsic_emit_vertex: {
      int stream_id = instr->const_index[0];
      gs_emit_vertex(stream_id);
      break;
   }

   case nir_intrinsic_end_primitive:
      gs_end_primitive();
      break;

   case nir_intrinsic_load_invocation_id: {
      src_reg invocation_id =
         src_reg(nir_system_values[SYSTEM_VALUE_INVOCATION_ID]);
      assert(invocation_id.file != BAD_FILE);
      dest = get_nir_dest(instr->dest, invocation_id.type);
      emit(MOV(dest, invocation_id));
      break;
   }

   default:
      vec4_visitor::nir_emit_intrinsic(instr);
   }
}
}
