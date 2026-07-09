/*
 * Copyright 2026 Google LLC
 *
 * SPDX-License-Identifier: MIT
 */

#include "panvk_device.h"

#include "vk_limits.h"

#include "nir.h"
#include "nir_builder.h"
#include "pan_nir.h"

struct lower_tile_image_ctx {
   uint32_t color_read;
   bool z_read;
   bool s_read;
};

static bool
lower_tile_image_load(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_tile_image)
      return false;

   struct lower_tile_image_ctx *ctx = data;
   const nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   nir_alu_type dest_type = nir_intrinsic_dest_type(intr);
   enum gl_access_qualifier access = nir_intrinsic_access(intr) & ACCESS_COHERENT;
   gl_frag_result location = sem.location;

   nir_def *offset = intr->src[0].ssa;
   nir_def *sample = intr->src[1].ssa;

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *pos, *conversion;

   if (location == FRAG_RESULT_DEPTH || location == FRAG_RESULT_STENCIL) {
      const bool is_depth = location == FRAG_RESULT_DEPTH;

      if (is_depth)
         ctx->z_read = true;
      else
         ctx->s_read = true;

      dest_type = is_depth ? nir_type_float32 : nir_type_uint32;
      conversion = nir_imm_int(b, 0);
      pos = pan_nir_tile_location_sample(b, location, sample);
   } else {
      uint8_t base_rt = location - FRAG_RESULT_DATA0;
      nir_def *rt;

      if (nir_src_is_const(intr->src[0])) {
         uint8_t conv_rt = base_rt + nir_src_as_uint(intr->src[0]);
         rt = nir_imm_int(b, conv_rt);
         ctx->color_read |= BITFIELD_BIT(conv_rt);
      } else {
         /* A dynamic attachment index assumes a uniform MRT format. */
         rt = nir_iadd_imm(b, offset, base_rt);
         ctx->color_read |=
            BITFIELD_RANGE(base_rt, MESA_VK_MAX_COLOR_ATTACHMENTS - base_rt);
      }

      /* Runtime conversion keeps the shader format-independent so the on-disk
       * cache is not poisoned across pipelines differing only in format.
       */
      conversion = nir_load_input_attachment_conv_pan(b, nir_iadd_imm(b, rt, 1));
      pos = pan_nir_tile_rt_sample(b, rt, sample);
   }

   nir_io_semantics tile_sem = {
      .location = location,
      .num_slots = 1,
   };

   nir_def *res = nir_load_tile_pan(
      b, intr->def.num_components, intr->def.bit_size, pos,
      pan_nir_tile_default_coverage(b), conversion, .dest_type = dest_type,
      .access = access, .io_semantics = tile_sem);

   /* A stencil load leaves garbage in the upper 24 bits. */
   if (location == FRAG_RESULT_STENCIL)
      res = nir_iand_imm(b, res, 0xff);

   nir_def_replace(&intr->def, res);

   return true;
}

bool
panvk_nir_lower_tile_image(nir_shader *nir, uint32_t *color_read_out,
                           bool *z_read_out, bool *s_read_out)
{
   struct lower_tile_image_ctx ctx = { 0 };
   assert(color_read_out && z_read_out && s_read_out);

   bool progress = nir_shader_intrinsics_pass(nir, lower_tile_image_load,
                                              nir_metadata_control_flow, &ctx);

   /* The tile image variables themselves are now dead. */
   if (progress)
      NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_image, NULL);

   *color_read_out = ctx.color_read;
   *z_read_out = ctx.z_read;
   *s_read_out = ctx.s_read;

   return progress;
}
