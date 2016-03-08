/*
 * Copyright © 2013 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * \file brw_vec4_tes.cpp
 *
 * Tessellaton evaluation shader specific code derived from the vec4_visitor class.
 */

#include "brw_vec4_tes.h"

namespace brw {

vec4_tes_visitor::vec4_tes_visitor(const struct brw_compiler *compiler,
                                  void *log_data,
                                  const struct brw_tes_prog_key *key,
                                  struct brw_tes_prog_data *prog_data,
                                  const nir_shader *shader,
                                  void *mem_ctx,
                                  int shader_time_index)
   : vec4_visitor(compiler, log_data, &key->tex, &prog_data->base,
                  shader, mem_ctx, false, shader_time_index)
{
}


dst_reg *
vec4_tes_visitor::make_reg_for_system_value(int location, const glsl_type *type)
{
   return NULL;
}

void
vec4_tes_visitor::nir_setup_system_value_intrinsic(nir_intrinsic_instr *instr)
{
   const struct brw_tes_prog_data *tes_prog_data =
      (const struct brw_tes_prog_data *) prog_data;

   switch (instr->intrinsic) {
   case nir_intrinsic_load_tess_level_outer: {
      dst_reg dst(this, glsl_type::vec4_type);
      nir_system_values[SYSTEM_VALUE_TESS_LEVEL_OUTER] = dst;

      dst_reg temp(this, glsl_type::vec4_type);
      vec4_instruction *read =
         emit(VEC4_OPCODE_URB_READ, temp, input_read_header);
      read->offset = 1;
      read->urb_write_flags = BRW_URB_WRITE_PER_SLOT_OFFSET;
      emit(MOV(dst, swizzle(src_reg(temp), BRW_SWIZZLE_WZYX)));
      break;
   }
   case nir_intrinsic_load_tess_level_inner: {
      dst_reg dst(this, glsl_type::vec2_type);
      nir_system_values[SYSTEM_VALUE_TESS_LEVEL_INNER] = dst;

      /* Set up the message header to reference the proper parts of the URB */
      dst_reg temp(this, glsl_type::vec4_type);
      vec4_instruction *read =
         emit(VEC4_OPCODE_URB_READ, temp, input_read_header);
      read->urb_write_flags = BRW_URB_WRITE_PER_SLOT_OFFSET;
      if (tes_prog_data->domain == BRW_TESS_DOMAIN_QUAD) {
         emit(MOV(dst, swizzle(src_reg(temp), BRW_SWIZZLE_WZYX)));
      } else {
         read->offset = 1;
         emit(MOV(dst, src_reg(temp)));
      }
      break;
   }
   default:
      vec4_visitor::nir_setup_system_value_intrinsic(instr);
   }
}


void
vec4_tes_visitor::setup_payload()
{
   int reg = 0;

   /* The payload always contains important data in r0 and r1, which contains
    * the URB handles that are passed on to the URB write at the end
    * of the thread.
    */
   reg += 2;

   reg = setup_uniforms(reg);

   this->first_non_payload_grf = reg;
}


void
vec4_tes_visitor::emit_prolog()
{
   input_read_header = src_reg(this, glsl_type::uvec4_type);
   emit(TES_OPCODE_CREATE_INPUT_READ_HEADER, dst_reg(input_read_header));

   this->current_annotation = NULL;
}


void
vec4_tes_visitor::emit_urb_write_header(int mrf)
{
   /* No need to do anything for DS; an implied write to this MRF will be
    * performed by VS_OPCODE_URB_WRITE.
    */
   (void) mrf;
}


vec4_instruction *
vec4_tes_visitor::emit_urb_write_opcode(bool complete)
{
   /* For DS, the URB writes end the thread. */
   if (complete) {
      if (INTEL_DEBUG & DEBUG_SHADER_TIME)
         emit_shader_time_end();
   }

   vec4_instruction *inst = emit(VS_OPCODE_URB_WRITE);
   inst->urb_write_flags = complete ?
      BRW_URB_WRITE_EOT_COMPLETE : BRW_URB_WRITE_NO_FLAGS;

   return inst;
}

void
vec4_tes_visitor::nir_emit_intrinsic(nir_intrinsic_instr *instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_tess_coord:
      /* gl_TessCoord is part of the payload in g1 channels 0-2 and 4-6. */
      emit(MOV(get_nir_dest(instr->dest, BRW_REGISTER_TYPE_F),
               src_reg(brw_vec8_grf(1, 0))));
      break;
   case nir_intrinsic_load_primitive_id:
      emit(TES_OPCODE_GET_PRIMITIVE_ID,
           get_nir_dest(instr->dest, BRW_REGISTER_TYPE_UD));
      break;

   case nir_intrinsic_load_input:
   case nir_intrinsic_load_per_vertex_input: {
      src_reg indirect_offset = get_indirect_offset(instr);
      unsigned imm_offset = instr->const_index[0];
      src_reg header = input_read_header;

      if (indirect_offset.file != BAD_FILE) {
         header = src_reg(this, glsl_type::uvec4_type);
         emit(TES_OPCODE_ADD_INDIRECT_URB_OFFSET, dst_reg(header),
              input_read_header, indirect_offset);
      }

      dst_reg temp(this, glsl_type::ivec4_type);
      vec4_instruction *read =
         emit(VEC4_OPCODE_URB_READ, temp, src_reg(header));
      read->offset = imm_offset;
      read->urb_write_flags = BRW_URB_WRITE_PER_SLOT_OFFSET;

      /* Copy to target.  We might end up with some funky writemasks landing
       * in here, but we really don't want them in the above pseudo-ops.
       */
      dst_reg dst = get_nir_dest(instr->dest, BRW_REGISTER_TYPE_D);
      dst.writemask = brw_writemask_for_size(instr->num_components);
      emit(MOV(dst, src_reg(temp)));
      break;
   }
   default:
      vec4_visitor::nir_emit_intrinsic(instr);
   }
}


void
vec4_tes_visitor::emit_thread_end()
{
   /* For DS, we always end the thread by emitting a single vertex.
    * emit_urb_write_opcode() will take care of setting the eot flag on the
    * SEND instruction.
    */
   emit_vertex();
}

} /* namespace brw */
