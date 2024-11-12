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
      unreachable("Invalid image channel type");
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
      comps[i] = nir_imm_int(b, 0);

   if (color->num_components < 4) {
      if (isl_format_has_int_channel(image_fmt))
         comps[3] = nir_imm_int(b, 1);
      else
         comps[3] = nir_imm_float(b, 1);
   }

   return nir_vec(b, comps, dest_components);
}

static bool
lower_image_load_instr(nir_builder *b,
                       const struct intel_device_info *devinfo,
                       nir_intrinsic_instr *intrin,
                       bool sparse)
{
   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
   nir_variable *var = nir_deref_instr_get_variable(deref);

   if (var->data.image.format == PIPE_FORMAT_NONE)
      return false;

   const enum isl_format image_fmt =
      isl_format_for_pipe_format(var->data.image.format);

   assert(isl_has_matching_typed_storage_image_format(devinfo, image_fmt));
   const enum isl_format lower_fmt =
      isl_lower_storage_image_format(devinfo, image_fmt);
   const unsigned dest_components =
      sparse ? (intrin->num_components - 1) : intrin->num_components;

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
         nir_channel(b, &intrin->def, intrin->num_components - 1);
      color = nir_vec(b, sparse_color, dest_components + 1);
   }

   nir_def_rewrite_uses(placeholder, color);
   nir_instr_remove(placeholder->parent_instr);

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
      unreachable("Invalid image channel type");
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
                        const struct intel_device_info *devinfo,
                        nir_intrinsic_instr *intrin)
{
   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
   nir_variable *var = nir_deref_instr_get_variable(deref);

   /* For write-only surfaces, we trust that the hardware can just do the
    * conversion for us.
    */
   if (var->data.access & ACCESS_NON_READABLE)
      return false;

   if (var->data.image.format == PIPE_FORMAT_NONE)
      return false;

   const enum isl_format image_fmt =
      isl_format_for_pipe_format(var->data.image.format);

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
brw_nir_lower_storage_image_instr(nir_builder *b,
                                  nir_instr *instr,
                                  void *cb_data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;
   const struct brw_nir_lower_storage_image_opts *opts = cb_data;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   switch (intrin->intrinsic) {
   case nir_intrinsic_image_deref_load:
      if (opts->lower_loads)
         return lower_image_load_instr(b, opts->devinfo, intrin, false);
      return false;

   case nir_intrinsic_image_deref_sparse_load:
      if (opts->lower_loads)
         return lower_image_load_instr(b, opts->devinfo, intrin, true);
      return false;

   case nir_intrinsic_image_deref_store:
      if (opts->lower_stores)
         return lower_image_store_instr(b, opts->devinfo, intrin);
      return false;

   default:
      /* Nothing to do */
      return false;
   }
}

bool
brw_nir_lower_storage_image(nir_shader *shader,
                            const struct brw_nir_lower_storage_image_opts *opts)
{
   bool progress = false;

   const nir_lower_image_options image_options = {
      .lower_cube_size = true,
      .lower_image_samples_to_one = true,
   };

   progress |= nir_lower_image(shader, &image_options);

   progress |= nir_shader_instructions_pass(shader,
                                            brw_nir_lower_storage_image_instr,
                                            nir_metadata_none,
                                            (void *)opts);

   return progress;
}
