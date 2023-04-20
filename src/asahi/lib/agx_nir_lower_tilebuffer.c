/*
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "compiler/agx_internal_formats.h"
#include "agx_nir_format_helpers.h"
#include "agx_tilebuffer.h"
#include "nir.h"
#include "nir_builder.h"

#define ALL_SAMPLES 0xFF

struct ctx {
   struct agx_tilebuffer_layout *tib;
   uint8_t *colormasks;
   bool *translucent;
};

static bool
tib_filter(const nir_instr *instr, UNUSED const void *_)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_store_output &&
       intr->intrinsic != nir_intrinsic_load_output)
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   assert(sem.dual_source_blend_index == 0 && "todo: dual source blending");
   return (sem.location >= FRAG_RESULT_DATA0);
}

static nir_ssa_def *
tib_impl(nir_builder *b, nir_instr *instr, void *data)
{
   struct ctx *ctx = data;
   struct agx_tilebuffer_layout *tib = ctx->tib;
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   unsigned rt = sem.location - FRAG_RESULT_DATA0;
   assert(rt < ARRAY_SIZE(tib->logical_format));

   enum pipe_format logical_format = tib->logical_format[rt];
   enum pipe_format format = agx_tilebuffer_physical_format(tib, rt);
   unsigned comps = util_format_get_nr_components(logical_format);

   if (intr->intrinsic == nir_intrinsic_store_output) {
      /* Only write components that actually exist */
      uint16_t write_mask = BITFIELD_MASK(comps);

      /* Delete stores to nonexistent render targets */
      if (logical_format == PIPE_FORMAT_NONE)
         return NIR_LOWER_INSTR_PROGRESS_REPLACE;

      /* Only write colours masked by the blend state */
      if (ctx->colormasks)
         write_mask &= ctx->colormasks[rt];

      /* Masked stores require a translucent pass type */
      if (write_mask != BITFIELD_MASK(comps)) {
         assert(ctx->translucent != NULL &&
                "colour masking requires translucency");

         assert(agx_internal_format_supports_mask(format) &&
                "write mask but format cannot be masked");

         *(ctx->translucent) = true;
      }

      /* But we ignore the NIR write mask for that, since it's basically an
       * optimization hint.
       */
      if (agx_internal_format_supports_mask(format))
         write_mask &= nir_intrinsic_write_mask(intr);

      /* Delete stores that are entirely masked out */
      if (!write_mask)
         return NIR_LOWER_INSTR_PROGRESS_REPLACE;

      nir_ssa_def *value = intr->src[0].ssa;

      /* Trim to format as required by hardware */
      value = nir_trim_vector(b, intr->src[0].ssa, comps);

      /* The hardware cannot extend for a 32-bit format. Extend ourselves. */
      if (format == PIPE_FORMAT_R32_UINT && value->bit_size == 16) {
         if (util_format_is_pure_sint(logical_format))
            value = nir_i2i32(b, value);
         else if (util_format_is_pure_uint(logical_format))
            value = nir_u2u32(b, value);
         else
            value = nir_f2f32(b, value);
      }

      nir_store_local_pixel_agx(b, value, nir_imm_intN_t(b, ALL_SAMPLES, 16),
                                .base = tib->offset_B[rt],
                                .write_mask = write_mask, .format = format);

      return NIR_LOWER_INSTR_PROGRESS_REPLACE;
   } else {
      uint8_t bit_size = nir_dest_bit_size(intr->dest);

      /* Loads from non-existent render targets are undefined in NIR but not
       * possible to encode in the hardware, delete them.
       */
      if (logical_format == PIPE_FORMAT_NONE)
         return nir_ssa_undef(b, intr->num_components, bit_size);

      bool f16 = (format == PIPE_FORMAT_R16_FLOAT);

      /* Don't load with F16 */
      if (f16)
         format = PIPE_FORMAT_R16_UINT;

      nir_ssa_def *res = nir_load_local_pixel_agx(
         b, MIN2(intr->num_components, comps), f16 ? 16 : bit_size,
         nir_imm_intN_t(b, ALL_SAMPLES, 16), .base = tib->offset_B[rt],
         .format = format);

      /* Extend floats */
      if (f16 && nir_dest_bit_size(intr->dest) != 16) {
         assert(nir_dest_bit_size(intr->dest) == 32);
         res = nir_f2f32(b, res);
      }

      res = nir_sign_extend_if_sint(b, res, logical_format);
      res = nir_pad_vector(b, res, intr->num_components);

      return res;
   }
}

bool
agx_nir_lower_tilebuffer(nir_shader *shader, struct agx_tilebuffer_layout *tib,
                         uint8_t *colormasks, bool *translucent)
{
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);

   struct ctx ctx = {
      .tib = tib,
      .colormasks = colormasks,
      .translucent = translucent,
   };

   return nir_shader_lower_instructions(shader, tib_filter, tib_impl, &ctx);
}
