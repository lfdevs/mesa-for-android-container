/*
 * Copyright 2022 Alyssa Rosenzweig
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_nir_lower_vbo.h"

#include "kk_cmd_buffer.h"

#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_format_convert.h"
#include "util/bitset.h"
#include "util/u_math.h"
#include "shader_enums.h"

struct ctx {
   struct kk_attribute *attribs;
   bool requires_vertex_id;
   bool requires_instance_id;
   bool requires_base_instance;
};

static bool
is_rgb10_a2(const struct util_format_description *desc)
{
   return desc->channel[0].shift == 0 && desc->channel[0].size == 10 &&
          desc->channel[1].shift == 10 && desc->channel[1].size == 10 &&
          desc->channel[2].shift == 20 && desc->channel[2].size == 10 &&
          desc->channel[3].shift == 30 && desc->channel[3].size == 2;
}

static bool
is_rg11_b10(const struct util_format_description *desc)
{
   return desc->channel[0].shift == 0 && desc->channel[0].size == 11 &&
          desc->channel[1].shift == 11 && desc->channel[1].size == 11 &&
          desc->channel[2].shift == 22 && desc->channel[2].size == 10;
}

static enum pipe_format
kk_vbo_internal_format(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);

   /* RGB10A2 and RG11B10 require loading as uint and then unpack */
   if (is_rgb10_a2(desc) || is_rg11_b10(desc))
      return PIPE_FORMAT_R32_UINT;

   /* R11G11B10F is native and special */
   if (format == PIPE_FORMAT_R11G11B10_FLOAT)
      return format;

   /* No other non-array formats handled */
   if (!desc->is_array)
      return PIPE_FORMAT_NONE;

   /* Otherwise look at one (any) channel */
   int idx = util_format_get_first_non_void_channel(format);
   if (idx < 0)
      return PIPE_FORMAT_NONE;

   /* We only handle RGB formats (we could do SRGB if we wanted though?) */
   if ((desc->colorspace != UTIL_FORMAT_COLORSPACE_RGB) ||
       (desc->layout != UTIL_FORMAT_LAYOUT_PLAIN))
      return PIPE_FORMAT_NONE;

   /* We have native 8-bit and 16-bit normalized formats */
   struct util_format_channel_description chan = desc->channel[idx];

   /* Otherwise map to the corresponding integer format */
   switch (chan.size) {
   case 32:
      return PIPE_FORMAT_R32_UINT;
   case 16:
      return PIPE_FORMAT_R16_UINT;
   case 8:
      return PIPE_FORMAT_R8_UINT;
   default:
      return PIPE_FORMAT_NONE;
   }
}

bool
kk_vbo_supports_format(enum pipe_format format)
{
   return kk_vbo_internal_format(format) != PIPE_FORMAT_NONE;
}

static nir_def *
apply_swizzle_channel(nir_builder *b, nir_def *vec, unsigned swizzle,
                      bool is_int)
{
   switch (swizzle) {
   case PIPE_SWIZZLE_X:
      return nir_channel(b, vec, 0);
   case PIPE_SWIZZLE_Y:
      return nir_channel(b, vec, 1);
   case PIPE_SWIZZLE_Z:
      return nir_channel(b, vec, 2);
   case PIPE_SWIZZLE_W:
      return nir_channel(b, vec, 3);
   case PIPE_SWIZZLE_0:
      return nir_imm_intN_t(b, 0, vec->bit_size);
   case PIPE_SWIZZLE_1:
      return is_int ? nir_imm_intN_t(b, 1, vec->bit_size)
                    : nir_imm_floatN_t(b, 1.0, vec->bit_size);
   default:
      UNREACHABLE("Invalid swizzle channel");
   }
}

static bool
pass(struct nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_input)
      return false;

   struct ctx *ctx = data;
   struct kk_attribute *attribs = ctx->attribs;
   b->cursor = nir_instr_remove(&intr->instr);

   nir_src *offset_src = nir_get_io_offset_src(intr);
   assert(nir_src_is_const(*offset_src) && "no attribute indirects");
   unsigned index = nir_intrinsic_base(intr) + nir_src_as_uint(*offset_src);

   struct kk_attribute attrib = attribs[index];

   const struct util_format_description *desc =
      util_format_description(attrib.format);
   int chan = util_format_get_first_non_void_channel(attrib.format);
   assert(chan >= 0);

   bool is_float = desc->channel[chan].type == UTIL_FORMAT_TYPE_FLOAT;
   bool is_unsigned = desc->channel[chan].type == UTIL_FORMAT_TYPE_UNSIGNED;
   bool is_signed = desc->channel[chan].type == UTIL_FORMAT_TYPE_SIGNED;
   bool is_fixed = desc->channel[chan].type == UTIL_FORMAT_TYPE_FIXED;
   bool is_int = util_format_is_pure_integer(attrib.format);

   assert((is_float ^ is_unsigned ^ is_signed ^ is_fixed) && "Invalid format");

   enum pipe_format interchange_format = kk_vbo_internal_format(attrib.format);
   assert(interchange_format != PIPE_FORMAT_NONE);

   unsigned interchange_align = util_format_get_blocksize(interchange_format);
   unsigned interchange_comps = util_format_get_nr_components(attrib.format);

   /* In the hardware, uint formats zero-extend and float formats convert.
    * However, non-uint formats using a uint interchange format shouldn't be
    * zero extended.
    */
   unsigned interchange_register_size =
      util_format_is_pure_uint(interchange_format) ? (interchange_align * 8)
                                                   : intr->def.bit_size;

   /* Non-UNORM R10G10B10A2 loaded as a scalar and unpacked */
   if (interchange_format == PIPE_FORMAT_R32_UINT && !desc->is_array)
      interchange_comps = 1;

   /* Calculate the element to fetch the vertex for. Divide the instance ID by
    * the divisor for per-instance data. Divisor=0 specifies per-vertex data.
    */
   nir_def *el;
   if (attrib.instanced) {
      if (attrib.divisor > 0) {
         /* Metal's instance_id has base_instance included */
         nir_def *instance_id =
            nir_isub(b, nir_load_instance_id(b), nir_load_base_instance(b));
         el = nir_udiv_imm(b, instance_id, attrib.divisor);
         ctx->requires_instance_id = true;
      } else
         el = nir_imm_int(b, 0);

      el = nir_iadd(b, el, nir_load_base_instance(b));
      ctx->requires_base_instance = true;

      BITSET_SET(b->shader->info.system_values_read,
                 SYSTEM_VALUE_BASE_INSTANCE);
   } else {
      el = nir_load_vertex_id(b);
      ctx->requires_vertex_id = true;
   }

   /* Load the pointer of the buffer from the argument buffer */
   nir_def *argbuf = nir_load_buffer_ptr_kk(b, 1, 64, .binding = 0);
   uint64_t attrib_base_offset =
      offsetof(struct kk_root_descriptor_table, draw.attrib_base[index]);
   nir_def *base = nir_load_global_constant(
      b, 1, 64, nir_iadd_imm(b, argbuf, attrib_base_offset));

   uint64_t buffer_stride_offset = offsetof(
      struct kk_root_descriptor_table, draw.buffer_strides[attrib.binding]);
   nir_def *stride = nir_load_global_constant(
      b, 1, 32, nir_iadd_imm(b, argbuf, buffer_stride_offset));
   nir_def *stride_offset_el =
      nir_imul(b, el, nir_udiv_imm(b, stride, interchange_align));

   /* Load the raw vector */
   nir_def *memory = nir_load_constant_agx(
      b, interchange_comps, interchange_register_size, base, stride_offset_el,
      .format = interchange_format, .base = 0u);

   unsigned dest_size = intr->def.bit_size;
   unsigned bits[] = {desc->channel[chan].size, desc->channel[chan].size,
                      desc->channel[chan].size, desc->channel[chan].size};

   /* Unpack non-native formats */
   if (is_rg11_b10(desc)) {
      memory = nir_format_unpack_11f11f10f(b, memory);
   } else if (is_rgb10_a2(desc)) {
      bits[0] = 10;
      bits[1] = 10;
      bits[2] = 10;
      bits[3] = 2;
      if (is_signed)
         memory = nir_format_unpack_sint(b, memory, bits, 4);
      else
         memory = nir_format_unpack_uint(b, memory, bits, 4);
   }

   if (desc->channel[chan].normalized) {
      if (is_signed)
         memory = nir_format_snorm_to_float(b, memory, bits);
      else
         memory = nir_format_unorm_to_float(b, memory, bits);
   } else if (desc->channel[chan].pure_integer) {
      if (is_signed)
         memory = nir_i2iN(b, memory, dest_size);
      else
         memory = nir_u2uN(b, memory, dest_size);
   } else {
      if (is_unsigned)
         memory = nir_u2fN(b, memory, dest_size);
      else if (is_signed || is_fixed)
         memory = nir_i2fN(b, memory, dest_size);
      else
         memory = nir_f2fN(b, memory, dest_size);

      /* 16.16 fixed-point weirdo GL formats need to be scaled */
      if (is_fixed) {
         assert(desc->is_array && desc->channel[chan].size == 32);
         assert(dest_size == 32 && "overflow if smaller");
         memory = nir_fmul_imm(b, memory, 1.0 / 65536.0);
      }
   }

   /* We now have a properly formatted vector of the components in memory. Apply
    * the format swizzle forwards to trim/pad/reorder as needed.
    */
   nir_def *channels[4] = {NULL};

   for (unsigned i = 0; i < intr->num_components; ++i) {
      unsigned c = nir_intrinsic_component(intr) + i;
      channels[i] = apply_swizzle_channel(b, memory, desc->swizzle[c], is_int);
   }

   nir_def *logical = nir_vec(b, channels, intr->num_components);
   nir_def_rewrite_uses(&intr->def, logical);
   return true;
}

bool
kk_nir_lower_vbo(nir_shader *nir, struct kk_attribute *attribs)
{
   assert(nir->info.stage == MESA_SHADER_VERTEX);

   struct ctx ctx = {.attribs = attribs};
   bool progress =
      nir_shader_intrinsics_pass(nir, pass, nir_metadata_control_flow, &ctx);

   if (ctx.requires_instance_id)
      BITSET_SET(nir->info.system_values_read, SYSTEM_VALUE_INSTANCE_ID);
   if (ctx.requires_base_instance)
      BITSET_SET(nir->info.system_values_read, SYSTEM_VALUE_BASE_INSTANCE);
   if (ctx.requires_vertex_id)
      BITSET_SET(nir->info.system_values_read, SYSTEM_VALUE_VERTEX_ID);
   return progress;
}
