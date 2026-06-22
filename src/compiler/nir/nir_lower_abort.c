/*
 * Copyright © 2026 Valve Corporation
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

#include "nir.h"
#include "nir_builder.h"
#include "nir_builder_opcodes.h"

/* This pass lowers nir_abort() to global stores. The buffer layout is
 * described as follows:
 *
 * uint32_t offset; (and total written size)
 * uint8_t data[];
 *
 * For each message:
 * uint64_t msg_data_size;
 * uint8_t msg_data[msg_data_size];
 */
static bool
lower_abort_intrin(nir_builder *b, nir_intrinsic_instr *intrin, void *_options)
{
   const nir_lower_abort_options *options = _options;
   if (intrin->intrinsic != nir_intrinsic_abort)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   const unsigned ptr_bit_size =
      options->ptr_bit_size ? options->ptr_bit_size : nir_get_ptr_bitsize(b->shader);

   nir_def *buffer_addr;
   if (options->buffer_addr) {
      buffer_addr = nir_imm_intN_t(b, options->buffer_addr, ptr_bit_size);
   } else {
      buffer_addr = nir_load_abort_buffer_address(b, ptr_bit_size);
   }

   /* Atomic add a buffer size counter to determine where to write. */
   nir_deref_instr *buffer =
      nir_build_deref_cast(b, buffer_addr, nir_var_mem_global,
                           glsl_array_type(glsl_uint8_t_type(), 0, 1), 0);

   nir_deref_instr *message_deref = nir_src_as_deref(intrin->src[0]);

   const unsigned slot_header_size = sizeof(uint64_t);
   const unsigned message_data_size = glsl_get_explicit_size(message_deref->type, true);
   const unsigned slot_size = slot_header_size + message_data_size;

   /* Increment the counter at the beginning of the buffer */
   nir_deref_instr *counter = nir_build_deref_array_imm(b, buffer, 0);
   counter = nir_build_deref_cast(b, &counter->def, nir_var_mem_global,
                                  glsl_uint_type(), 0);
   counter->cast.align_mul = 4;
   nir_def *offset =
      nir_deref_atomic(b, 32, &counter->def, nir_imm_int(b, slot_size),
                       .atomic_op = nir_atomic_op_iadd);

   /* Check if we're still in-bounds */
   nir_def *buffer_size;
   if (options->max_buffer_size) {
      buffer_size = nir_imm_int(b, options->max_buffer_size);
   } else {
      buffer_size = nir_load_abort_buffer_size(b);
   }

   nir_push_if(b, nir_ult(b, offset, nir_iadd_imm(b, buffer_size, -slot_size)));
   {
      nir_def *base = nir_u2uN(b, offset, ptr_bit_size);

      /* Store the message data size as 64-bit. */
      nir_deref_instr *message_data_size_deref =
         nir_build_deref_cast(b, &nir_build_deref_array(b, buffer, base)->def,
                              nir_var_mem_global, glsl_uint64_t_type(), 0);
      message_data_size_deref->cast.align_mul = 1;
      nir_store_deref(b, message_data_size_deref, nir_imm_int64(b, message_data_size), ~0);

      /* Store the message data. */
      nir_def *dst_addr = nir_iadd_imm(b, base, slot_header_size);
      nir_deref_instr *dst =
         nir_build_deref_cast(b, &nir_build_deref_array(b, buffer, dst_addr)->def,
                              nir_var_mem_global, message_deref->type, 0);
      nir_copy_deref(b, dst, message_deref);
   }
   nir_pop_if(b, NULL);

   /* Abort is a jump instruction so can only appear at the end of a block.
    * The abort might be in the middle of a block. So, wrap it and let control
    * flow optimization clean up after us.
    */
   nir_push_if(b, nir_imm_true(b));
   {
      nir_jump(b, nir_jump_abort);
   }
   nir_pop_if(b, NULL);

   nir_instr_remove(&intrin->instr);
   return true;
}

bool
nir_lower_abort(nir_shader *nir, const nir_lower_abort_options *options)
{
   return nir_shader_intrinsics_pass(nir, lower_abort_intrin,
                                     nir_metadata_none,
                                     (void *)options);
}
