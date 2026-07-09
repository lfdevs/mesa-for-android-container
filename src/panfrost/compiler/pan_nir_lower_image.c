/*
 * Copyright (C) 2026 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_nir.h"
#include "panfrost/model/pan_model.h"

static bool
lower_image_size(nir_builder *b, nir_intrinsic_instr *intr, uint64_t gpu_id)
{
   const enum glsl_sampler_dim dim = nir_intrinsic_image_dim(intr);
   const bool is_array = nir_intrinsic_image_array(intr);

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *handle = intr->src[0].ssa;

   nir_def *res;
   if (pan_arch(gpu_id) >= 9) {
      if (dim == GLSL_SAMPLER_DIM_BUF)
         res = pan_nir_load_va_buf_size_el(b, handle);
      else
         res = pan_nir_load_va_tex_size(b, handle, dim, is_array);
   } else {
      /* Not handled yet */
      return false;
   }

   nir_def_replace(&intr->def, res);
   return true;
}

static bool
lower_image_samples(nir_builder *b, nir_intrinsic_instr *intr, uint64_t gpu_id)
{
   assert(nir_intrinsic_image_dim(intr) == GLSL_SAMPLER_DIM_MS);

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *handle = intr->src[0].ssa;

   nir_def *res;
   if (pan_arch(gpu_id) >= 9) {
      res = pan_nir_load_va_tex_samples(b, handle);
   } else {
      /* Not handled yet */
      return false;
   }

   nir_def_replace(&intr->def, res);
   return true;
}

static nir_def *
pack_image_coords(nir_builder *b, nir_def *coords, nir_def *msaa_idx,
                  enum glsl_sampler_dim dim, bool is_array, uint64_t gpu_id)
{
   /* Must be lowered before this pass */
   assert(dim != GLSL_SAMPLER_DIM_BUF);

   bool is_ms = dim == GLSL_SAMPLER_DIM_MS;
   unsigned num_components = glsl_get_sampler_dim_coordinate_components(dim);

   /* Cube and CubeArray is just a 2D array */
   if (dim == GLSL_SAMPLER_DIM_CUBE) {
      is_array = true;
      num_components = 2;
   }
   unsigned num_coords = num_components + (is_array ? 1 : 0);

   /* We expect the parameters in this order:
    * -  < v9: [S, T, R | array_index] (if array, R is unused)
    * - >= v9: [S, T, R, array_index]
    * All coordinates are in 16-bits and the higher bits MUST be bounds-checked.
    * First put the parameters in the right order:
    */
   nir_def *params[4] = { NULL, };

   unsigned ch_idx = 0;
   for (unsigned i = 0; i < num_components; i++)
      params[ch_idx++] = nir_channel(b, coords, i);

   if (is_ms) {
      /* On bifrost, MSAA must be lowered */
      assert(pan_arch(gpu_id) >= 9);
      assert(msaa_idx != NULL);
      params[ch_idx++] = msaa_idx;
   }
   if (is_array) {
      unsigned index = pan_arch(gpu_id) >= 9 ? 3 : 2;
      assert(ch_idx <= index);
      params[index] = nir_channel(b, coords, num_components);
   }

   /* Now we need to trim the highest bits, bound-checking them, to do that we
    * can use a simple trick, if num_coords == 1, then we should fill only the S
    * texel coordinate, but we also know that the underlying image descriptor
    * must have matching dimensionality (VUID-vkCmdDraw-viewType-07752) and that
    * LD_TEX bound-checks each coordinate treating the bound as 1 if the
    * image does not have high enough dimensions.  Given that, we can "overflow"
    * the higher 16 bits as the T coordinate and the LD_TEX/LEA_TEX instr will
    * treat that as an OOB IFF T != 0.  When more than one coordinate must be
    * checked, we OR together the high bits and push them in an unused
    * parameter.  When there are no unused parameters (only case: 2DMS array)
    * the sample_id must always be less than 16, we can stump that with an invalid
    * value.
    */
   nir_def *clamped_chan[4] = { NULL, };

   /* Small optimization, if the T channel is free, we can use it as an
    * overflow of the S channel, this will prevent a lot of split/unsplit and
    * save an IOR operation when we have two free channels to use as bound
    * checks.
    */
   if (params[1] == NULL) {
      assert(num_coords == 1 || (num_coords == 2 && is_array));
      clamped_chan[0] = nir_unpack_32_2x16_split_x(b, params[0]);
      clamped_chan[1] = nir_unpack_32_2x16_split_y(b, params[0]);
      params[0] = NULL;
   }

   /* TODO: we could skip a lot of instructions if robustness is disabled */
   nir_def *oob = NULL;
   /* Truncate and accumulate OOB */
   for (unsigned i = 0; i < ARRAY_SIZE(params); i++) {
      if (params[i] == NULL)
         continue;

      nir_def *ch_oob = nir_unpack_32_2x16_split_y(b, params[i]);
      clamped_chan[i] = nir_unpack_32_2x16_split_x(b, params[i]);
      params[i] = NULL;

      oob = (oob == NULL) ? ch_oob : nir_ior(b, oob, ch_oob);
   }

   /* Place OOB in the first unused coord  */
   for (unsigned i = 0; i < ARRAY_SIZE(params) && oob != NULL; i++) {
      if (clamped_chan[i] == NULL) {
         clamped_chan[i] = oob;
         oob = NULL;
      }
   }
   /* If all channels are full (unlucky!) stomp the sample_index chan */
   if (oob != NULL) {
      assert(num_coords == 3 && is_array && is_ms);
      assert(pan_arch(gpu_id) >= 9);/* Bifrost doesn't have MSAA */
      clamped_chan[2] = nir_bcsel(b, nir_ine_imm(b, oob, 0),
                                  nir_imm_intN_t(b, 0xFFFF, 16),
                                  clamped_chan[2]);
   }

   /* Fill the remaining NULLs with zero */
   for (unsigned i = 0; i < ARRAY_SIZE(params); i++) {
      if (clamped_chan[i] == NULL)
         clamped_chan[i] = nir_imm_zero(b, 1, 16);
   }

   /* Pack it in 2x32 (X=low, Y=high) */
   nir_def *xy = nir_pack_32_2x16_split(b, clamped_chan[0], clamped_chan[1]);
   nir_def *zw = nir_pack_32_2x16_split(b, clamped_chan[2], clamped_chan[3]);

   return nir_vec2(b, xy, zw);
}

static bool
lower_image_load(nir_builder *b, nir_intrinsic_instr *intr, uint64_t gpu_id)
{
   enum glsl_sampler_dim dim = nir_intrinsic_image_dim(intr);
   bool image_array = nir_intrinsic_image_array(intr);

   nir_def *handle = intr->src[0].ssa;
   nir_def *coords = intr->src[1].ssa;
   nir_def *msaa_idx = intr->src[2].ssa;

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *packed = pack_image_coords(b, coords, msaa_idx, dim, image_array,
                                       gpu_id);
   nir_def *new_def =
      nir_load_tex_pan(b, intr->num_components, intr->def.bit_size,
                       packed, handle,
                       .dest_type = nir_intrinsic_dest_type(intr),
                       .access = nir_intrinsic_access(intr));
   nir_def_replace(&intr->def, new_def);
   return true;
}

static bool
lower_image_store(nir_builder *b, nir_intrinsic_instr *intr, uint64_t gpu_id)
{
   enum glsl_sampler_dim dim = nir_intrinsic_image_dim(intr);
   bool image_array = nir_intrinsic_image_array(intr);

   /* Due to SPIR-V limitations, the source type is not fully reliable: it
    * reports uint32 even for write_imagei.  This causes an incorrect
    * u32->s32->u32 roundtrip which incurs an unwanted clamping.  Use auto32
    * instead, which will match per the OpenCL spec.  Of course this does
    * not work for 16-bit stores, but those are not available in OpenCL.
    */
   ASSERTED nir_alu_type orig_type = nir_intrinsic_src_type(intr);
   assert(nir_alu_type_get_type_size(orig_type) == 32);
   nir_alu_type src_type = 32;

   nir_def *handle = intr->src[0].ssa;
   nir_def *coords = intr->src[1].ssa;
   nir_def *msaa_idx = intr->src[2].ssa;
   nir_def *data = intr->src[3].ssa;

   enum pipe_format format = nir_intrinsic_format(intr);

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *packed = pack_image_coords(b, coords, msaa_idx, dim, image_array,
                                       gpu_id);

   nir_def *tex = nir_lea_tex_pan(b, packed, handle, .src_type = src_type);
   nir_def *addr = nir_pack_64_2x32(b, nir_trim_vector(b, tex, 2));
   nir_def *cvt  = nir_channel(b, tex, 2);

   /* nir_opt_shrink_stores doesn't handle custom intrinsics */
   if (format != PIPE_FORMAT_NONE) {
      unsigned components = util_format_get_nr_components(format);
      if (components < intr->num_components) {
         data = nir_trim_vector(b, data, components);
         intr->num_components = components;
      }
   }

   nir_instr_remove(&intr->instr);
   nir_store_global_cvt_pan(b, data, addr, cvt, .src_type = src_type);
   return true;
}

static bool
lower_image_texel_addr(nir_builder *b, nir_intrinsic_instr *intr,
                       uint64_t gpu_id)
{
   enum glsl_sampler_dim dim = nir_intrinsic_image_dim(intr);
   bool image_array = nir_intrinsic_image_array(intr);

   assert(dim != GLSL_SAMPLER_DIM_MS);

   nir_def *handle = intr->src[0].ssa;
   nir_def *coords = intr->src[1].ssa;
   nir_alu_type src_type = 32; /* auto32 */

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *packed = pack_image_coords(b, coords, NULL, dim, image_array,
                                       gpu_id);
   nir_def *tex = nir_lea_tex_pan(b, packed, handle, .src_type = src_type);
   nir_def *addr = nir_pack_64_2x32(b, nir_trim_vector(b, tex, 2));
   nir_def_replace(&intr->def, addr);
   return true;
}

static bool
lower_image_intr(nir_builder *b, nir_intrinsic_instr *intr, void *cb_data)
{
   uint64_t gpu_id = *(uint64_t *)cb_data;

   switch (intr->intrinsic) {
   case nir_intrinsic_image_size:
      return lower_image_size(b, intr, gpu_id);

   case nir_intrinsic_image_samples:
      return lower_image_samples(b, intr, gpu_id);

   case nir_intrinsic_image_load:
      return lower_image_load(b, intr, gpu_id);

   case nir_intrinsic_image_store:
      return lower_image_store(b, intr, gpu_id);

   case nir_intrinsic_image_texel_address:
      return lower_image_texel_addr(b, intr, gpu_id);

   default:
      return false;
   }
}

bool
pan_nir_lower_image(nir_shader *nir, uint64_t gpu_id)
{
   return nir_shader_intrinsics_pass(nir, lower_image_intr,
                                     nir_metadata_none, &gpu_id);
}
