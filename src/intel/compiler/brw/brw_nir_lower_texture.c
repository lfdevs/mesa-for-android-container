/*
 * Copyright © 2024 Intel Corporation
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

#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_builtin_builder.h"
#include "compiler/nir/nir_format_convert.h"
#include "brw_nir.h"
#include "brw_sampler.h"

/**
 * Takes care of lowering to target HW messages payload.
 *
 * For example:
 *    - HW has no gather4_po_i_b so lower to gather_po_l.
 *    - HW has no sample_po_b_c message, so lower the bias into the LOD to switch
 *      to sample_po_c_l instead.
 */
static bool
pre_lower_texture_instr(nir_builder *b,
                        nir_tex_instr *tex,
                        void *data)
{
   switch (tex->op) {
   case nir_texop_txb: {
      int bias_index = nir_tex_instr_src_index(tex, nir_tex_src_bias);
      assert(bias_index != -1);
      int comparator_index = nir_tex_instr_src_index(tex, nir_tex_src_comparator);
      int offset_index = nir_tex_instr_src_index(tex, nir_tex_src_offset);

      if (comparator_index == -1 || offset_index == -1)
         return false;

      if (brw_nir_tex_offset_in_constant_range(tex, offset_index))
         return false;

      b->cursor = nir_before_instr(&tex->instr);

      tex->op = nir_texop_txl;

      nir_def *bias = tex->src[bias_index].src.ssa;
      nir_tex_instr_remove_src(tex, bias_index);

      nir_def *lod = nir_fadd(b, bias, nir_get_texture_lod(b, tex));
      nir_tex_instr_add_src(tex, nir_tex_src_lod, lod);
      return true;
   }

   case nir_texop_tg4: {
      if (!tex->is_gather_implicit_lod)
         return false;

      nir_def *bias = nir_steal_tex_src(tex, nir_tex_src_bias);
      if (!bias)
         return false;

      b->cursor = nir_before_instr(&tex->instr);

      tex->is_gather_implicit_lod = false;

      nir_def *lod = nir_fadd(b, bias, nir_get_texture_lod(b, tex));
      nir_tex_instr_add_src(tex, nir_tex_src_lod, lod);
      return true;
   }

   default:
      return false;
   }
}

bool
brw_nir_pre_lower_texture(nir_shader *shader)
{
   return nir_shader_tex_pass(shader,
                              pre_lower_texture_instr,
                              nir_metadata_control_flow,
                              NULL);
}

/**
 * Pack either the explicit LOD or LOD bias and the array index together.
 */
static bool
pack_lod_and_array_index(nir_builder *b, nir_tex_instr *tex)
{
   /* If 32-bit texture coordinates are used, pack either the explicit LOD or
    * LOD bias and the array index into a single (32-bit) value.
    */
   int lod_index = nir_tex_instr_src_index(tex, nir_tex_src_lod);
   if (lod_index < 0) {
      lod_index = nir_tex_instr_src_index(tex, nir_tex_src_bias);

      /* The explicit LOD or LOD bias may not be found if this lowering has
       * already occured.  The explicit LOD may also not be found in some
       * cases where it is zero.
       */
      if (lod_index < 0)
         return false;
   }

   assert(nir_tex_instr_src_type(tex, lod_index) == nir_type_float);

   /* Also do not perform this packing if the explicit LOD is zero. */
   if (tex->op == nir_texop_txl &&
       nir_src_is_const(tex->src[lod_index].src) &&
       nir_src_as_float(tex->src[lod_index].src) == 0.0) {
      return false;
   }

   const int coord_index = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   assert(coord_index >= 0);

   nir_def *lod = tex->src[lod_index].src.ssa;
   nir_def *coord = tex->src[coord_index].src.ssa;

   assert(nir_tex_instr_src_type(tex, coord_index) == nir_type_float);

   if (coord->bit_size < 32)
      return false;

   b->cursor = nir_before_instr(&tex->instr);

   /* First, combine the two values.  The packing format is a little weird.
    * The explicit LOD / LOD bias is stored as float, as normal.  However, the
    * array index is converted to an integer and smashed into the low 9 bits.
    */
   const unsigned array_index = tex->coord_components - 1;

   nir_def *clamped_ai =
      nir_umin(b,
               nir_f2u32(b, nir_fround_even(b, nir_channel(b, coord,
                                                           array_index))),
               nir_imm_int(b, 511));

   nir_def *lod_ai = nir_ior(b, nir_iand_imm(b, lod, 0xfffffe00), clamped_ai);

   /* Second, replace the coordinate with a new value that has one fewer
    * component (i.e., drop the array index).
    */
   nir_def *reduced_coord = nir_trim_vector(b, coord,
                                            tex->coord_components - 1);
   tex->coord_components--;

   /* Finally, remove the old sources and add the new. */
   nir_src_rewrite(&tex->src[coord_index].src, reduced_coord);

   nir_tex_instr_remove_src(tex, lod_index);
   nir_tex_instr_add_src(tex, nir_tex_src_backend1, lod_ai);

   return true;
}

static nir_def *
build_packed_offset(nir_builder *b,
                    nir_def *offset,
                    unsigned offset_bits,
                    unsigned offset_count)
{
   offset = nir_iand_imm(b, offset, BITFIELD_MASK(offset_bits));

   nir_def *offuvr = nir_channel(b, offset, 0);
   for (unsigned i = 1; i < MIN2(offset->num_components, offset_count); i++) {
      nir_def *chan = nir_channel(b, offset, i);
      offuvr = nir_ior(b, offuvr, nir_ishl_imm(b, chan, i * offset_bits));
   }

   return offuvr;
}

/**
 * Pack either the explicit LOD/Bias and the offset together.
 */
static bool
pack_lod_or_bias_and_offset(nir_builder *b, nir_tex_instr *tex,
                            unsigned offset_bits,
                            unsigned offset_count)
{
   int offset_index = nir_tex_instr_src_index(tex, nir_tex_src_offset);
   if (offset_index < 0)
      return false;

   /* If 32-bit texture coordinates are used, pack either the explicit LOD or
    * LOD bias and the array index into a single (32-bit) value.
    */
   int lod_index = nir_tex_instr_src_index(tex, nir_tex_src_lod);
   if (lod_index < 0) {
      lod_index = nir_tex_instr_src_index(tex, nir_tex_src_bias);

      /* The explicit LOD or LOD bias may not be found if this lowering has
       * already occured.  The explicit LOD may also not be found in some
       * cases where it is zero.
       */
      if (lod_index < 0)
         return false;
   }

   assert(nir_tex_instr_src_type(tex, lod_index) == nir_type_float);

   /* Also do not perform this packing if the explicit LOD is zero. */
   if (nir_src_is_const(tex->src[lod_index].src) &&
       nir_src_as_float(tex->src[lod_index].src) == 0.0) {
      return false;
   }

   nir_def *lod = tex->src[lod_index].src.ssa;

   b->cursor = nir_before_instr(&tex->instr);

   /* When using the programmable offsets instruction gather4_po_l_c with
    * SIMD16 or SIMD32 the U, V offsets are combined with LOD/bias parameters
    * on the 12 LSBs. For the offset parameters on gather instructions the 6
    * least significant bits are honored as signed value with a range
    * [-32..31].
    *
    * Pack Offset U, and V for texture gather with offsets.
    *
    *    ------------------------------------------
    *    |Bits     | [31:12]  | [11:6]  | [5:0]   |
    *    ------------------------------------------
    *    |OffsetUV | LOD/Bias | OffsetV | OffsetU |
    *    ------------------------------------------
    *
    * Or
    *    ---------------------------------------------------
    *    |Bits     | [31:12]  |  [11:9] |  [8:5]  |  [4:0]  |
    *    ----------------------------------------------------
    *    |OffsetUV | LOD/Bias | OffsetR | OffsetV | OffsetU |
    *    ----------------------------------------------------
    */
   nir_def *offuvr = build_packed_offset(
      b, tex->src[offset_index].src.ssa, offset_bits, offset_count);

   nir_def *packed = nir_ior(b, offuvr, nir_iand_imm(b, lod, 0xFFFFF000));
   nir_tex_instr_remove_src(tex, offset_index);
   nir_tex_instr_add_src(tex, nir_tex_src_backend1, packed);

   return true;
}

static bool
pack_offset_r(nir_builder *b, nir_tex_instr *tex,
              unsigned offset_bits, unsigned offset_count)
{
   nir_def *offset = nir_steal_tex_src(tex, nir_tex_src_offset);
   if (!offset)
      return false;

   const int coord_index = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   assert(coord_index >= 0);

   b->cursor = nir_before_instr(&tex->instr);

   nir_def *coord = tex->src[coord_index].src.ssa;

   nir_def *offuvr = build_packed_offset(
      b, offset, offset_bits, offset_count);

   nir_def *packed = nir_ishl_imm(b, offuvr, 12);

   assert(tex->coord_components != 4);
   if (tex->coord_components == 3) {
      nir_def *clamped_r =
         nir_umin_imm(
            b,
            nir_f2u32(b, nir_fround_even(b, nir_channel(b, coord, 2))),
            0xfff);

      packed = nir_ior(b, packed, clamped_r);

      nir_def *reduced_coord = nir_trim_vector(b, coord, 2);
      tex->coord_components = 2;

      nir_src_rewrite(&tex->src[coord_index].src, reduced_coord);
   }

   nir_tex_instr_add_src(tex, nir_tex_src_backend1, packed);

   return true;
}

static bool
pack_offset(nir_builder *b, nir_tex_instr *tex,
            unsigned offset_bits, unsigned offset_count)
{
   nir_def *offset = nir_steal_tex_src(tex, nir_tex_src_offset);
   if (!offset)
      return false;

   b->cursor = nir_before_instr(&tex->instr);

   nir_def *offuvr = build_packed_offset(
      b, offset, offset_bits, offset_count);

   nir_tex_instr_add_src(tex, nir_tex_src_backend1, offuvr);

   return true;
}

/* Sampler header offset format described in SKL PRMs Volume 7:
 * 3D-Media-GPGPU, Sampler, Message Header.
 */
static bool
pack_header_offset(nir_builder *b, nir_tex_instr *tex)
{
   nir_def *_offset = nir_steal_tex_src(tex, nir_tex_src_offset);
   if (!_offset)
      return false;

   b->cursor = nir_before_instr(&tex->instr);

   static const unsigned bits4[] = { 4, 4, 4, };
   nir_def *offset = nir_iand_imm(b, nir_format_clamp_sint(b, _offset, bits4), 0xf);

   nir_def *offuvr = nir_ishl_imm(b, nir_channel(b, offset, 0), 8);
   for (unsigned i = 1; i < MIN2(offset->num_components, 3); i++) {
      nir_def *chan = nir_channel(b, offset, i);
      offuvr = nir_ior(b, offuvr, nir_ishl_imm(b, chan, 8 - (4 * i)));
   }

   nir_tex_instr_add_src(tex, nir_tex_src_backend2, offuvr);

   return true;
}

static bool
brw_nir_lower_texture_instr(nir_builder *b, nir_tex_instr *tex, void *cb_data)
{
   enum brw_sampler_opcode sampler_opcode = tex->backend_flags;
   bool progress = false;

   const struct brw_sampler_payload_desc *payload_desc =
      brw_get_sampler_payload_desc(sampler_opcode);
   bool has_offset_param = false;

   for (uint32_t i = 0; payload_desc->sources[i].param != BRW_SAMPLER_PAYLOAD_PARAM_INVALID; i++) {
#define PARAM_CASE(name) case BRW_SAMPLER_PAYLOAD_PARAM_##name
      switch (payload_desc->sources[i].param) {
      PARAM_CASE(LOD_AI):
      PARAM_CASE(BIAS_AI):
         progress |= pack_lod_and_array_index(b, tex);
         break;
      PARAM_CASE(BIAS_OFFUV6):
      PARAM_CASE(LOD_OFFUV6):
         progress |= pack_lod_or_bias_and_offset(b, tex, 6, 2);
         has_offset_param = true;
         break;
      PARAM_CASE(BIAS_OFFUVR4):
      PARAM_CASE(LOD_OFFUVR4):
         progress |= pack_lod_or_bias_and_offset(b, tex, 4, 3);
         has_offset_param = true;
         break;
      PARAM_CASE(OFFUV4_R):
         progress |= pack_offset_r(b, tex, 4, 2);
         has_offset_param = true;
         break;
      PARAM_CASE(OFFUVR4_R):
         progress |= pack_offset_r(b, tex, 4, 3);
         has_offset_param = true;
         break;
      PARAM_CASE(OFFUV6_R):
         progress |= pack_offset_r(b, tex, 6, 2);
         has_offset_param = true;
         break;
      PARAM_CASE(OFFUV4):
         progress |= pack_offset(b, tex, 4, 2);
         has_offset_param = true;
         break;
      PARAM_CASE(OFFUVR4):
         progress |= pack_offset(b, tex, 4, 3);
         has_offset_param = true;
         break;
      PARAM_CASE(OFFUV6):
         progress |= pack_offset(b, tex, 6, 2);
         has_offset_param = true;
         break;
      PARAM_CASE(OFFUVR6):
         progress |= pack_offset(b, tex, 6, 3);
         has_offset_param = true;
         break;
      PARAM_CASE(OFFU):
      PARAM_CASE(OFFV):
         has_offset_param = true;
         break;
      default:
         break;
      }
#undef PARAM_CASE
   }

   /* Handle pre-Xe2 dynamic programmable offsets */
   int offset_idx;
   if (!has_offset_param &&
       (offset_idx = nir_tex_instr_src_index(tex, nir_tex_src_offset)) >= 0 &&
       !brw_nir_tex_offset_in_constant_range(tex, offset_idx))
      progress |= pack_header_offset(b, tex);

   return progress;
}

bool
brw_nir_lower_texture(nir_shader *shader)
{
   return nir_shader_tex_pass(shader, brw_nir_lower_texture_instr,
                              nir_metadata_none, NULL);
}

static bool
brw_nir_lower_texture_opcode_instr(nir_builder *b, nir_tex_instr *tex, void *cb_data)
{
   const struct intel_device_info *devinfo = cb_data;

   tex->backend_flags = brw_get_sampler_opcode_from_tex(devinfo, tex);

   return true;
}

bool
brw_nir_texture_backend_opcode(nir_shader *shader,
                               const struct intel_device_info *devinfo)
{
   return nir_shader_tex_pass(shader, brw_nir_lower_texture_opcode_instr,
                              nir_metadata_all, (void *)devinfo);
}

static bool
brw_nir_lower_mcs_fetch_instr(nir_builder *b, nir_tex_instr *tex, void *cb_data)
{
   switch (tex->op) {
   case nir_texop_txf_ms:
   case nir_texop_samples_identical:
      break;

   default:
      /* Nothing to do */
      return false;
   }

   /* Only happens with BLORP shaders */
   if (nir_tex_instr_src_index(tex, nir_tex_src_ms_mcs_intel) != -1)
      return false;

   const struct intel_device_info *devinfo = cb_data;
   const bool needs_16bit_txf_ms_payload = devinfo->verx10 >= 125;

   b->cursor = nir_before_instr(&tex->instr);

   /* Convert all sources to 16bit */
   unsigned n_mcs_sources = 0;
   for (uint32_t i = 0; i < tex->num_srcs; i++) {
      switch (tex->src[i].src_type) {
      case nir_tex_src_texture_handle:
      case nir_tex_src_texture_offset:
      case nir_tex_src_texture_deref:
         n_mcs_sources++;
         break;

      case nir_tex_src_coord:
      case nir_tex_src_lod:
         n_mcs_sources++;
         FALLTHROUGH;
      default:
         if (needs_16bit_txf_ms_payload) {
            nir_src_rewrite(&tex->src[i].src,
                            nir_u2u16(b, tex->src[i].src.ssa));
         }
         break;
      }
   }

   nir_tex_instr *mcs_tex = nir_tex_instr_create(b->shader, n_mcs_sources);
   mcs_tex->op = nir_texop_txf_ms_mcs_intel;
   mcs_tex->dest_type = nir_type_uint32;
   mcs_tex->sampler_dim = tex->sampler_dim;
   mcs_tex->coord_components = tex->coord_components;
   mcs_tex->texture_index = tex->texture_index;
   mcs_tex->sampler_index = tex->sampler_index;
   mcs_tex->is_array = tex->is_array;
   mcs_tex->can_speculate = tex->can_speculate;

   uint32_t mcs_src = 0;
   for (uint32_t i = 0; i < tex->num_srcs; i++) {
      switch (tex->src[i].src_type) {
      case nir_tex_src_texture_handle:
      case nir_tex_src_texture_offset:
      case nir_tex_src_texture_deref:
      case nir_tex_src_coord:
      case nir_tex_src_lod:
         assert(mcs_src < mcs_tex->num_srcs);
         mcs_tex->src[mcs_src++] =
            nir_tex_src_for_ssa(tex->src[i].src_type,
                                tex->src[i].src.ssa);
         break;

      default:
         break;
      }
   }

   nir_def_init(&mcs_tex->instr, &mcs_tex->def, 4, 32);
   nir_builder_instr_insert(b, &mcs_tex->instr);

   nir_def *mcs_data = &mcs_tex->def;
   if (tex->op == nir_texop_txf_ms) {
      if (needs_16bit_txf_ms_payload) {
         mcs_data =
            nir_vec4(b,
                     nir_unpack_32_2x16_split_x(b, nir_channel(b, mcs_data, 0)),
                     nir_unpack_32_2x16_split_y(b, nir_channel(b, mcs_data, 0)),
                     nir_unpack_32_2x16_split_x(b, nir_channel(b, mcs_data, 1)),
                     nir_unpack_32_2x16_split_y(b, nir_channel(b, mcs_data, 1)));
      }

      nir_tex_instr_add_src(tex, nir_tex_src_ms_mcs_intel, mcs_data);
   } else {
      assert(tex->op == nir_texop_samples_identical);

      nir_def_replace(&tex->def,
                      nir_ieq_imm(
                         b,
                         nir_ior(b,
                                 nir_channel(b, mcs_data, 0),
                                 nir_channel(b, mcs_data, 1)),
                         0));
   }

   return true;
}

bool
brw_nir_lower_mcs_fetch(nir_shader *shader,
                        const struct intel_device_info *devinfo)
{
   return nir_shader_tex_pass(shader,
                              brw_nir_lower_mcs_fetch_instr,
                              nir_metadata_control_flow,
                              (void *)devinfo);
}
