/* Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

#include "si_shader_internal.h"
#include "nir_builder.h"

bool si_nir_lower_polygon_stipple(nir_shader *nir)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);

   nir_builder builder = nir_builder_at(nir_before_impl(impl));
   nir_builder *b = &builder;

   /* Load the buffer descriptor. */
   nir_def *desc = nir_load_polygon_stipple_buffer_amd(b);

   /* Use the fixed-point gl_FragCoord input.
    * Since the stipple pattern is 32x32 and it repeats, just get 5 bits
    * per coordinate to get the repeating effect.
    */
   nir_def *pixel_coord = nir_u2u32(b, nir_iand_imm(b, nir_load_pixel_coord(b), 0x1f));

   nir_def *zero = nir_imm_int(b, 0);
   /* The stipple pattern is 32x32, each row has 32 bits. */
   nir_def *offset = nir_ishl_imm(b, nir_channel(b, pixel_coord, 1), 2);
   nir_def *row = nir_load_buffer_amd(b, 1, 32, desc, offset, zero, zero,
                                      .access = ACCESS_CAN_REORDER | ACCESS_CAN_SPECULATE);
   nir_def *bit = nir_ubfe(b, row, nir_channel(b, pixel_coord, 0), nir_imm_int(b, 1));

   nir_def *pass = nir_i2b(b, bit);
   nir_discard_if(b, nir_inot(b, pass));

   return nir_progress(true, impl, nir_metadata_control_flow);
}
