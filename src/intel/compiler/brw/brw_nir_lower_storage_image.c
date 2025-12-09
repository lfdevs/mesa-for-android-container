/*
 * Copyright © 2018 Intel Corporation
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

#include "isl/isl.h"

#include "brw_nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_format_convert.h"

struct brw_nir_lower_storage_image_state {
   const struct brw_compiler *compiler;
   struct brw_nir_lower_storage_image_opts opts;
};

struct format_info {
   const struct isl_format_layout *fmtl;
   unsigned chans;
   unsigned bits[4];
};

static struct format_info
get_format_info(enum isl_format fmt)
{
   const struct isl_format_layout *fmtl = isl_format_get_layout(fmt);

   return (struct format_info) {
      .fmtl = fmtl,
      .chans = isl_format_get_num_channels(fmt),
      .bits = {
         fmtl->channels.r.bits,
         fmtl->channels.g.bits,
         fmtl->channels.b.bits,
         fmtl->channels.a.bits
      },
   };
}

static bool
skip_storage_format(const struct intel_device_info *devinfo,
                    enum isl_format format)
{
   if (!isl_is_storage_image_format(devinfo, format))
      return true;

   return format == isl_lower_storage_image_format(devinfo, format);
}

static nir_def *
convert_color_for_load(nir_builder *b, const struct intel_device_info *devinfo,
                       nir_def *color,
                       enum isl_format image_fmt, enum isl_format lower_fmt,
                       unsigned dest_components)
{
   if (image_fmt == lower_fmt)
      goto expand_vec;

   if (image_fmt == ISL_FORMAT_R11G11B10_FLOAT) {
      assert(lower_fmt == ISL_FORMAT_R32_UINT);
      color = nir_format_unpack_11f11f10f(b, color);
      goto expand_vec;
   } else if (image_fmt == ISL_FORMAT_R64_PASSTHRU) {
      assert(lower_fmt == ISL_FORMAT_R32G32_UINT);
      color = nir_pack_64_2x32(b, nir_channels(b, color, 0x3));
      goto expand_vec;
   }

   struct format_info image = get_format_info(image_fmt);
   struct format_info lower = get_format_info(lower_fmt);

   const bool needs_sign_extension =
      isl_format_has_snorm_channel(image_fmt) ||
      isl_format_has_sint_channel(image_fmt);

   /* We only check the red channel to detect if we need to pack/unpack */
   assert(image.bits[0] != lower.bits[0] ||
          memcmp(image.bits, lower.bits, sizeof(image.bits)) == 0);

   if (image.bits[0] != lower.bits[0] && lower_fmt == ISL_FORMAT_R32_UINT) {
      if (needs_sign_extension)
         color = nir_format_unpack_sint(b, color, image.bits, image.chans);
      else
         color = nir_format_unpack_uint(b, color, image.bits, image.chans);
   } else {
      /* All these formats are homogeneous */
      for (unsigned i = 1; i < image.chans; i++)
         assert(image.bits[i] == image.bits[0]);

      if (image.bits[0] != lower.bits[0]) {
         color = nir_format_bitcast_uvec_unmasked(b, color, lower.bits[0],
                                                  image.bits[0]);
      }

      if (needs_sign_extension)
         color = nir_format_sign_extend_ivec(b, color, image.bits);
   }

   switch (image.fmtl->channels.r.type) {
   case ISL_UNORM:
      assert(isl_format_has_uint_channel(lower_fmt));
      color = nir_format_unorm_to_float(b, color, image.bits);
      break;

   case ISL_SNORM:
      assert(isl_format_has_uint_channel(lower_fmt));
      color = nir_format_snorm_to_float(b, color, image.bits);
      break;

   case ISL_SFLOAT:
      if (image.bits[0] == 16)
         color = nir_unpack_half_2x16_split_x(b, color);
      break;

   case ISL_UINT:
   case ISL_SINT:
      break;

   default:
      UNREACHABLE("Invalid image channel type");
   }

expand_vec:
   assert(dest_components == 1 || dest_components == 4);
   assert(color->num_components <= dest_components);
   if (color->num_components == dest_components)
      return color;

   nir_def *comps[4];
   for (unsigned i = 0; i < color->num_components; i++)
      comps[i] = nir_channel(b, color, i);

   for (unsigned i = color->num_components; i < 3; i++)
      comps[i] = nir_imm_intN_t(b, 0, color->bit_size);

   if (color->num_components < 4) {
      if (isl_format_has_int_channel(image_fmt) ||
          image_fmt == ISL_FORMAT_R64_PASSTHRU)
         comps[3] = nir_imm_intN_t(b, 1, color->bit_size);
      else
         comps[3] = nir_imm_floatN_t(b, 1, color->bit_size);
   }

   return nir_vec(b, comps, dest_components);
}

static nir_def *
convert_color_for_load_format(nir_builder *b,
                              const struct brw_compiler *compiler,
                              nir_def *color,
                              nir_def *surface_format)
{
   nir_def *conversions[20] = {};
   assert((compiler->num_lowered_storage_formats + 1) < ARRAY_SIZE(conversions));

   for (unsigned i = 0; i < compiler->num_lowered_storage_formats; i++) {
      enum isl_format format =
         compiler->lowered_storage_formats[i];
      enum isl_format lowered_format =
         isl_lower_storage_image_format(compiler->devinfo, format);
      unsigned lowered_components =
         isl_format_get_num_channels(lowered_format);

      nir_push_if(b, nir_ieq_imm(b, surface_format, format));
      {
         conversions[i] = convert_color_for_load(
            b, compiler->devinfo,
            nir_channels(b, color, nir_component_mask(lowered_components)),
            format, lowered_format, color->num_components);
      }
      nir_push_else(b, NULL);
   }

   /* When the HW does the conversion automatically */
   conversions[compiler->num_lowered_storage_formats] = nir_mov(b, color);

   for (unsigned f = 0; f < compiler->num_lowered_storage_formats; f++) {
      nir_pop_if(b, NULL);

      conversions[compiler->num_lowered_storage_formats - f - 1] =
         nir_if_phi(b, conversions[compiler->num_lowered_storage_formats - f - 1],
                       conversions[compiler->num_lowered_storage_formats - f]);
   }

   return conversions[0];
}

static bool
lower_image_load_instr_without_format(nir_builder *b,
                                      const struct brw_nir_lower_storage_image_state *state,
                                      nir_intrinsic_instr *intrin)
{
   /* This lowering relies on Gfx9+ HW behavior for typed reads (RAW values) */
   assert(state->compiler->devinfo->ver >= 9);

   /* Use an undef to hold the uses of the load while we do the color
    * conversion.
    */
   nir_def *placeholder = nir_undef(b, 4, 32);
   nir_def_rewrite_uses(&intrin->def, placeholder);

   b->cursor = nir_after_instr(&intrin->instr);

   nir_def *img = intrin->src[0].ssa;

   assert(nir_intrinsic_format(intrin) == PIPE_FORMAT_NONE);

   nir_def *image_fmt = nir_image_deref_load_param_intel(
      b, 1, 32, img, .base = ISL_SURF_PARAM_FORMAT);

   nir_def *color = convert_color_for_load_format(
      b, state->compiler, &intrin->def, image_fmt);

   nir_def_replace(placeholder, color);
   return true;
}

static bool
lower_image_load_instr(nir_builder *b,
                       const struct intel_device_info *devinfo,
                       nir_intrinsic_instr *intrin,
                       bool sparse)
{
   assert(nir_intrinsic_format(intrin) != PIPE_FORMAT_NONE);

   const enum isl_format image_fmt =
      isl_format_for_pipe_format(nir_intrinsic_format(intrin));

   assert(isl_has_matching_typed_storage_image_format(devinfo, image_fmt));
   const enum isl_format lower_fmt =
      isl_lower_storage_image_format(devinfo, image_fmt);
   const unsigned dest_components =
      sparse ? (intrin->num_components - 1) : intrin->num_components;

   if (intrin->def.bit_size == 64 &&
       isl_format_get_layout(lower_fmt)->channels.r.bits == 32) {
      intrin->def.num_components = 2;
      intrin->def.bit_size = 32;
   }

   /* Use an undef to hold the uses of the load while we do the color
    * conversion.
    */
   nir_def *placeholder = nir_undef(b, 4, 32);
   nir_def_rewrite_uses(&intrin->def, placeholder);

   intrin->num_components = isl_format_get_num_channels(lower_fmt);
   intrin->def.num_components = intrin->num_components;

   b->cursor = nir_after_instr(&intrin->instr);

   nir_def *color = convert_color_for_load(b, devinfo, &intrin->def, image_fmt, lower_fmt,
                                           dest_components);

   if (sparse) {
      /* Put the sparse component back on the original instruction */
      intrin->num_components++;
      intrin->def.num_components = intrin->num_components;

      /* Carry over the sparse component without modifying it with the
       * converted color.
       */
      nir_def *sparse_color[NIR_MAX_VEC_COMPONENTS];
      for (unsigned i = 0; i < dest_components; i++)
         sparse_color[i] = nir_channel(b, color, i);
      sparse_color[dest_components] =
         nir_u2uN(b,
                  nir_channel(b, &intrin->def, intrin->num_components - 1),
                  color->bit_size);
      color = nir_vec(b, sparse_color, dest_components + 1);
   }

   nir_def_replace(placeholder, color);
   return true;
}

static nir_def *
convert_color_for_store(nir_builder *b, const struct intel_device_info *devinfo,
                        nir_def *color,
                        enum isl_format image_fmt, enum isl_format lower_fmt)
{
   struct format_info image = get_format_info(image_fmt);
   struct format_info lower = get_format_info(lower_fmt);

   color = nir_trim_vector(b, color, image.chans);

   if (image_fmt == lower_fmt)
      return color;

   if (image_fmt == ISL_FORMAT_R11G11B10_FLOAT) {
      assert(lower_fmt == ISL_FORMAT_R32_UINT);
      return nir_format_pack_11f11f10f(b, color);
   } else if (image_fmt == ISL_FORMAT_R64_PASSTHRU) {
      assert(lower_fmt == ISL_FORMAT_R32G32_UINT);
      return nir_unpack_64_2x32(b, color);
   }

   switch (image.fmtl->channels.r.type) {
   case ISL_UNORM:
      assert(isl_format_has_uint_channel(lower_fmt));
      color = nir_format_float_to_unorm(b, color, image.bits);
      break;

   case ISL_SNORM:
      assert(isl_format_has_uint_channel(lower_fmt));
      color = nir_format_float_to_snorm(b, color, image.bits);
      break;

   case ISL_SFLOAT:
      if (image.bits[0] == 16)
         color = nir_format_float_to_half(b, color);
      break;

   case ISL_UINT:
      color = nir_format_clamp_uint(b, color, image.bits);
      break;

   case ISL_SINT:
      color = nir_format_clamp_sint(b, color, image.bits);
      break;

   default:
      UNREACHABLE("Invalid image channel type");
   }

   if (image.bits[0] < 32 &&
       (isl_format_has_snorm_channel(image_fmt) ||
        isl_format_has_sint_channel(image_fmt)))
      color = nir_format_mask_uvec(b, color, image.bits);

   if (image.bits[0] != lower.bits[0] && lower_fmt == ISL_FORMAT_R32_UINT) {
      color = nir_format_pack_uint(b, color, image.bits, image.chans);
   } else {
      /* All these formats are homogeneous */
      for (unsigned i = 1; i < image.chans; i++)
         assert(image.bits[i] == image.bits[0]);

      if (image.bits[0] != lower.bits[0]) {
         color = nir_format_bitcast_uvec_unmasked(b, color, image.bits[0],
                                                  lower.bits[0]);
      }
   }

   return color;
}

static bool
lower_image_store_instr(nir_builder *b,
                        const struct brw_nir_lower_storage_image_opts *opts,
                        const struct intel_device_info *devinfo,
                        nir_intrinsic_instr *intrin)
{
   enum pipe_format pipe_format = nir_intrinsic_format(intrin);

   if (pipe_format == PIPE_FORMAT_NONE)
      return false;

   const struct util_format_description *fmt_desc =
      util_format_description(pipe_format);
   const bool is_r64_fmt =
      fmt_desc->block.bits == 64 && fmt_desc->nr_channels == 1;
   if ((!opts->lower_stores && !is_r64_fmt) ||
       (!opts->lower_stores_64bit && is_r64_fmt))
      return false;

   /* For write-only surfaces non-64bit bpc, we trust that the hardware can
    * just do the conversion for us.
    */
   if ((nir_intrinsic_access(intrin) & ACCESS_NON_READABLE) && !is_r64_fmt)
      return false;

   const enum isl_format image_fmt =
      isl_format_for_pipe_format(pipe_format);

   assert(isl_has_matching_typed_storage_image_format(devinfo, image_fmt));
   const enum isl_format lower_fmt =
      isl_lower_storage_image_format(devinfo, image_fmt);

   /* Color conversion goes before the store */
   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *color = convert_color_for_store(b, devinfo,
                                                intrin->src[3].ssa,
                                                image_fmt, lower_fmt);
   intrin->num_components = isl_format_get_num_channels(lower_fmt);
   nir_src_rewrite(&intrin->src[3], color);

   return true;
}

static bool
lower(nir_builder *b, nir_intrinsic_instr *intrin, void *cb_data)
{
   const struct brw_nir_lower_storage_image_state *state = cb_data;

   switch (intrin->intrinsic) {
   case nir_intrinsic_image_deref_load: {
      if (nir_intrinsic_format(intrin) == PIPE_FORMAT_NONE) {
         if (state->opts.lower_loads_without_formats)
            return lower_image_load_instr_without_format(b, state, intrin);
      } else {
         if (state->opts.lower_loads) {
            return lower_image_load_instr(b, state->compiler->devinfo,
                                          intrin, false);
         }
      }
      return false;
   }

   case nir_intrinsic_image_deref_sparse_load: {
      if (nir_intrinsic_format(intrin) == PIPE_FORMAT_NONE) {
         if (state->opts.lower_loads_without_formats)
            return lower_image_load_instr_without_format(b, state, intrin);
      } else {
         if (state->opts.lower_loads) {
            return lower_image_load_instr(b, state->compiler->devinfo,
                                          intrin, true);
         }
      }
      return false;
   }

   case nir_intrinsic_image_deref_store:
      return lower_image_store_instr(
         b, &state->opts, state->compiler->devinfo, intrin);

   default:
      /* Nothing to do */
      return false;
   }
}

bool
brw_nir_lower_storage_image(nir_shader *shader,
                            const struct brw_compiler *compiler,
                            const struct brw_nir_lower_storage_image_opts *opts)
{
   bool progress = false;

   const nir_lower_image_options image_options = {
      .lower_cube_size = true,
      .lower_image_samples_to_one = true,
   };

   progress |= nir_lower_image(shader, &image_options);

   const struct brw_nir_lower_storage_image_state storage_options = {
      .compiler = compiler,
      .opts = *opts,
   };
   progress |= nir_shader_intrinsics_pass(shader, lower, nir_metadata_none,
                                         (void *)&storage_options);

   return progress;
}
