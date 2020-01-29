/*
 * Copyright (C) 2019 Collabora, Ltd.
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
 *
 * Authors (Collabora):
 *      Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

/**
 * @file
 *
 * Implements framebuffer format conversions in software, specifically for
 * blend shaders on Midgard/Bifrost. load_output/store_output (derefs more
 * correctly -- pre I/O lowering) normally for the fragment stage within the
 * blend shader will operate with purely vec4 float ("nir") encodings. This
 * lowering stage, to be run before I/O is lowered, converts the native
 * framebuffer format to a NIR encoding after loads and vice versa before
 * stores. This pass is designed for a single render target; Midgard duplicates
 * blend shaders for MRT to simplify everything.
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_format_convert.h"
#include "nir_lower_blend.h"
#include "util/format/u_format.h"

/* Determines the best NIR intrinsic to load a tile buffer of a given type,
 * using native format conversion where possible. RGBA8 UNORM has a fast path
 * (on some chips). Otherwise, we default to raw reads. */

static nir_intrinsic_op
nir_best_load_for_format(
      const struct util_format_description *desc,
      unsigned *special_bitsize,
      unsigned *special_components,
      unsigned gpu_id)
{
   if (util_format_is_unorm8(desc) && gpu_id != 0x750) {
      *special_bitsize = 16;
      return nir_intrinsic_load_output_u8_as_fp16_pan;
   } else if (desc->format == PIPE_FORMAT_R11G11B10_FLOAT) {
      *special_bitsize = 32;
      *special_components = 1;
      return nir_intrinsic_load_raw_output_pan;
   } else
      return nir_intrinsic_load_raw_output_pan;
}


/* Converters for UNORM8 formats, e.g. R8G8B8A8_UNORM */

static nir_ssa_def *
nir_float_to_unorm8(nir_builder *b, nir_ssa_def *c_float)
{
   /* First, we degrade quality to fp16; we don't need the extra bits */
   nir_ssa_def *degraded = /*nir_f2f16(b, c_float)*/c_float;

   /* Scale from [0, 1] to [0, 255.0] */
   nir_ssa_def *scaled = nir_fmul_imm(b, nir_fsat(b, degraded), 255.0);

   /* Next, we type convert */
   nir_ssa_def *converted = nir_u2u8(b, nir_f2u16(b,
                                     nir_fround_even(b, nir_f2f16(b, scaled))));

   return converted;
}

static nir_ssa_def *
nir_unorm8_to_float(nir_builder *b, nir_ssa_def *c_native)
{
   /* First, we convert up from u8 to f16 */
   nir_ssa_def *converted = nir_f2f32(b, nir_u2f16(b, nir_u2u16(b, c_native)));

   /* Next, we scale down from [0, 255.0] to [0, 1] */
   nir_ssa_def *scaled = nir_fsat(b, nir_fmul_imm(b, converted, 1.0/255.0));

   return scaled;
}

/* Converters for UNORM4 formats, packing the final result into 16-bit */

static nir_ssa_def *
nir_float_to_unorm4(nir_builder *b, nir_ssa_def *c_float)
{
   /* First, we degrade quality to fp16; we don't need the extra bits */
   nir_ssa_def *degraded = nir_f2f16(b, c_float);

   /* Scale from [0, 1] to [0, 15.0] */
   nir_ssa_def *scaled = nir_fmul_imm(b, nir_fsat(b, degraded), 15.0);

   /* Next, we type convert to u16 */
   nir_ssa_def *converted = nir_f2u16(b,
                                      nir_fround_even(b, scaled));

   /* In u16 land, we now need to pack */
   nir_ssa_def *cr = nir_channel(b, converted, 0);
   nir_ssa_def *cg = nir_channel(b, converted, 1);
   nir_ssa_def *cb = nir_channel(b, converted, 2);
   nir_ssa_def *ca = nir_channel(b, converted, 3);

   nir_ssa_def *pack =
      nir_ior(b,
              nir_ior(b, cr, nir_ishl(b, cg, nir_imm_int(b, 4))),
              nir_ior(b, nir_ishl(b, cb, nir_imm_int(b, 8)), nir_ishl(b, ca, nir_imm_int(b, 12))));

   return pack;
}

static nir_ssa_def *
nir_float_to_rgb10a2(nir_builder *b, nir_ssa_def *c_float, bool normalize)
{
   nir_ssa_def *converted = c_float;

   if (normalize) {
      nir_ssa_def *scaled = nir_fmul(b, nir_fsat(b, c_float),
                                     nir_imm_vec4(b, 1023.0, 1023.0, 1023.0, 3.0));

      converted = nir_f2u32(b,
                            nir_fround_even(b, scaled));
   }

   nir_ssa_def *cr = nir_channel(b, converted, 0);
   nir_ssa_def *cg = nir_channel(b, converted, 1);
   nir_ssa_def *cb = nir_channel(b, converted, 2);
   nir_ssa_def *ca = nir_channel(b, converted, 3);

   nir_ssa_def *pack =
      nir_ior(b,
              nir_ior(b, cr, nir_ishl(b, cg, nir_imm_int(b, 10))),
              nir_ior(b, nir_ishl(b, cb, nir_imm_int(b, 20)), nir_ishl(b, ca, nir_imm_int(b, 30))));

   return pack;
}

static nir_ssa_def *
nir_float_to_rgb5a1(nir_builder *b, nir_ssa_def *c_float)
{
   nir_ssa_def *degraded = nir_f2f16(b, c_float);

   nir_ssa_def *scaled = nir_fmul(b, nir_fsat(b, degraded),
                                  nir_imm_vec4_16(b, 31.0, 31.0, 31.0, 1.0));

   nir_ssa_def *converted = nir_f2u16(b,
                                      nir_fround_even(b, scaled));

   nir_ssa_def *cr = nir_channel(b, converted, 0);
   nir_ssa_def *cg = nir_channel(b, converted, 1);
   nir_ssa_def *cb = nir_channel(b, converted, 2);
   nir_ssa_def *ca = nir_channel(b, converted, 3);

   nir_ssa_def *pack =
      nir_ior(b,
              nir_ior(b, cr, nir_ishl(b, cg, nir_imm_int(b, 5))),
              nir_ior(b, nir_ishl(b, cb, nir_imm_int(b, 10)), nir_ishl(b, ca, nir_imm_int(b, 15))));

   return pack;
}

static nir_ssa_def *
nir_shader_to_native(nir_builder *b,
                     nir_ssa_def *c_shader,
                     const struct util_format_description *desc,
                     unsigned bits,
                     bool homogenous_bits)
{
   bool float_or_pure_int =
      util_format_is_float(desc->format) ||
      util_format_is_pure_integer(desc->format);

   if (util_format_is_unorm8(desc))
      return nir_float_to_unorm8(b, c_shader);
   else if (homogenous_bits && float_or_pure_int)
      return c_shader; /* type is already correct */

   //unsigned bgra[4] = { 2, 1, 0, 3 }; /* BGRA */
   //c_shader = nir_swizzle(b, c_shader, swiz, 4);

   /* Special formats */
   switch (desc->format) {
   case PIPE_FORMAT_B4G4R4A4_UNORM:
   case PIPE_FORMAT_B4G4R4X4_UNORM:
   case PIPE_FORMAT_A4R4_UNORM:
   case PIPE_FORMAT_R4A4_UNORM:
   case PIPE_FORMAT_A4B4G4R4_UNORM:
      return nir_float_to_unorm4(b, c_shader);

   case PIPE_FORMAT_R10G10B10A2_UNORM:
   case PIPE_FORMAT_B10G10R10A2_UNORM:
   case PIPE_FORMAT_R10G10B10X2_UNORM:
   case PIPE_FORMAT_B10G10R10X2_UNORM:
      return nir_float_to_rgb10a2(b, c_shader, true);

   case PIPE_FORMAT_R10G10B10A2_UINT:
      return nir_float_to_rgb10a2(b, c_shader, false);

   case PIPE_FORMAT_B5G5R5A1_UNORM:
      return nir_float_to_rgb5a1(b, c_shader);

   case PIPE_FORMAT_R11G11B10_FLOAT:
      return nir_format_pack_11f11f10f(b, c_shader);

   default:
      printf("%s\n", desc->name);
      unreachable("Unknown format name");
   }
}

static nir_ssa_def *
nir_native_to_shader(nir_builder *b,
                     nir_ssa_def *c_native,
                     nir_intrinsic_op op,
                     const struct util_format_description *desc,
                     unsigned bits,
                     bool homogenous_bits)
{
   bool float_or_pure_int =
      util_format_is_float(desc->format) ||
      util_format_is_pure_integer(desc->format);

   /* Handle preconverted formats */
   if (op == nir_intrinsic_load_output_u8_as_fp16_pan) {
      assert(util_format_is_unorm8(desc));
      return nir_f2f32(b, c_native);
   }

   /* Otherwise, we're raw */
   assert(op == nir_intrinsic_load_raw_output_pan);

   if (util_format_is_unorm8(desc))
      return nir_unorm8_to_float(b, c_native);
   else if (homogenous_bits && float_or_pure_int)
      return c_native; /* type is already correct */

   /* Special formats */
   switch (desc->format) {
   case PIPE_FORMAT_R11G11B10_FLOAT: {
      nir_ssa_def *unpacked = nir_format_unpack_11f11f10f(b, c_native);

      /* Extend to vec4 with alpha */
      nir_ssa_def *components[4] = {
         nir_channel(b, unpacked, 0),
         nir_channel(b, unpacked, 1),
         nir_channel(b, unpacked, 2),
         nir_imm_float(b, 1.0)
      };

      return nir_vec(b, components, 4);
   }

   default:
      printf("%s\n", desc->name);
      unreachable("Unknown format name");
   }
}

void
nir_lower_framebuffer(nir_shader *shader, enum pipe_format format,
                      unsigned gpu_id)
{
   /* Blend shaders are represented as special fragment shaders */
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);

   const struct util_format_description *format_desc =
      util_format_description(format);

   unsigned nr_channels = format_desc->nr_channels;
   unsigned bits = format_desc->channel[0].size;

   /* Do all channels have the same bit count? */
   bool homogenous_bits = true;

   for (unsigned c = 1; c < nr_channels; ++c)
      homogenous_bits &= (format_desc->channel[c].size == bits);

   if (format == PIPE_FORMAT_R11G11B10_FLOAT)
      homogenous_bits = false;

   /* Figure out the formats for the raw */
   unsigned raw_bitsize_in = bits;
   unsigned raw_bitsize_out = bits;
   unsigned raw_out_components = 4;

   /* We pack a 4-bit vec4 as 16-bit vec1 */
   if ((homogenous_bits && bits == 4 && util_format_is_unorm(format)) || format == PIPE_FORMAT_B5G5R5A1_UNORM) {
      raw_bitsize_out = 16;
      raw_out_components = 1;
   } else if (format == PIPE_FORMAT_R10G10B10A2_UNORM || format == PIPE_FORMAT_B10G10R10A2_UNORM || format == PIPE_FORMAT_R10G10B10A2_UINT || format == PIPE_FORMAT_R11G11B10_FLOAT) {
      raw_bitsize_out = 32;
      raw_out_components = 1;
   }

   nir_foreach_function(func, shader) {
      nir_foreach_block(block, func->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

            bool is_load = intr->intrinsic == nir_intrinsic_load_deref;
            bool is_store = intr->intrinsic == nir_intrinsic_store_deref;

            if (!(is_load || is_store))
               continue;

            /* Don't worry about MRT */
            nir_variable *var = nir_intrinsic_get_var(intr, 0);

            if (var->data.location != FRAG_RESULT_COLOR)
               continue;

            nir_builder b;
            nir_builder_init(&b, func->impl);

            if (is_store) {
               /* For stores, add conversion before */
               b.cursor = nir_before_instr(instr);

               /* Grab the input color */
               nir_ssa_def *c_nir = nir_ssa_for_src(&b, intr->src[1], 4);

               /* Apply sRGB transform */

               if (format_desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB) {
                  nir_ssa_def *rgb = nir_channels(&b, c_nir, 0x7);
                  nir_ssa_def *trans = nir_format_linear_to_srgb(&b, rgb);

                  nir_ssa_def *comp[4] = {
                     nir_channel(&b, trans, 0),
                     nir_channel(&b, trans, 1),
                     nir_channel(&b, trans, 2),
                     nir_channel(&b, c_nir, 3),
                  };

                  c_nir = nir_vec(&b, comp, 4);
               }

               /* Format convert */
               nir_ssa_def *converted = nir_shader_to_native(&b, c_nir, format_desc, bits, homogenous_bits);

               if (util_format_is_float(format)) {
                  if (raw_bitsize_out == 16)
                     converted = nir_f2f16(&b, converted);
                  else if (raw_bitsize_out == 32)
                     converted = nir_f2f32(&b, converted);
               } else {
                  converted = nir_i2i(&b, converted, raw_bitsize_out);
               }

               /* Rewrite to use a native store by creating a new intrinsic */
               nir_intrinsic_instr *new =
                  nir_intrinsic_instr_create(shader, nir_intrinsic_store_raw_output_pan);
               new->src[0] = nir_src_for_ssa(converted);

               new->num_components = raw_out_components;

               nir_builder_instr_insert(&b, &new->instr);

               /* (And finally removing the old) */
               nir_instr_remove(instr);
            } else {
               /* For loads, add conversion after */
               b.cursor = nir_after_instr(instr);

               /* Determine the best op for the format/hardware */
               unsigned bitsize = raw_bitsize_in;
               unsigned components = 4;
               nir_intrinsic_op op = nir_best_load_for_format(format_desc,
                                                              &bitsize,
                                                              &components,
                                                              gpu_id);

               /* Rewrite to use a native load by creating a new intrinsic */
               nir_intrinsic_instr *new = nir_intrinsic_instr_create(shader, op);
               new->num_components = components;

               nir_ssa_dest_init(&new->instr, &new->dest, components, bitsize, NULL);
               nir_builder_instr_insert(&b, &new->instr);

               /* Convert the raw value */
               nir_ssa_def *raw = &new->dest.ssa;
               nir_ssa_def *converted = nir_native_to_shader(&b, raw, op, format_desc, bits, homogenous_bits);

               if (util_format_is_float(format))
                  converted = nir_f2f32(&b, converted);
               else
                  converted = nir_i2i32(&b, converted);

               /* Rewrite to use the converted value */
               nir_src rewritten = nir_src_for_ssa(converted);
               nir_ssa_def_rewrite_uses_after(&intr->dest.ssa, rewritten, instr);

               /* Finally, remove the old load */
               nir_instr_remove(instr);
            }
         }
      }

      nir_metadata_preserve(func->impl, nir_metadata_block_index |
                            nir_metadata_dominance);
   }
}
