/*
 * Copyright (c) 2024 Intel Corporation
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

#include "intel_nir.h"
#include "intel_shader_enums.h"
#include "compiler/nir/nir_builder.h"

static bool
lower_printf_intrinsics(nir_builder *b, nir_intrinsic_instr *intrin, void *_)
{
   b->cursor = nir_before_instr(&intrin->instr);

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_printf_buffer_address:
      nir_def_replace(
         &intrin->def,
         nir_pack_64_2x32_split(
            b,
            nir_load_reloc_const_intel(b, BRW_SHADER_RELOC_PRINTF_BUFFER_ADDR_LOW),
            nir_load_reloc_const_intel(b, BRW_SHADER_RELOC_PRINTF_BUFFER_ADDR_HIGH)));
      return true;

   case nir_intrinsic_load_printf_buffer_size:
      nir_def_replace(
         &intrin->def,
         nir_load_reloc_const_intel(b, BRW_SHADER_RELOC_PRINTF_BUFFER_SIZE));
      return true;

   default:
      return false;
   }
}

bool
intel_nir_lower_printf(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir, lower_printf_intrinsics,
                                     nir_metadata_control_flow, NULL);
}
