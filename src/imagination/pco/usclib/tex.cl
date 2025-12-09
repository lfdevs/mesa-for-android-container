/*
 * Copyright © 2025 Imagination Technologies Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef __pvr_address_type
#define __pvr_address_type uint64_t
#define __pvr_get_address(addr) addr
#define __pvr_make_address(addr) addr
#endif /* __pvr_address_type */

#include "csbgen/rogue/texstate.h"
#include "libcl.h"
#include "util/u_math.h"


uint
usclib_tex_state_levels(uint4 tex_state)
{
   uint32_t texstate_image_word1[] = {tex_state.z, tex_state.w};
   struct ROGUE_TEXSTATE_IMAGE_WORD1 texstate_image_struct;

   ROGUE_TEXSTATE_IMAGE_WORD1_unpack(texstate_image_word1, &texstate_image_struct);

   return texstate_image_struct.num_mip_levels;
}

uint
usclib_tex_state_samples(uint4 tex_state)
{
   uint32_t texstate_image_word0[] = {tex_state.x, tex_state.y};
   struct ROGUE_TEXSTATE_IMAGE_WORD0 texstate_image_struct;

   ROGUE_TEXSTATE_IMAGE_WORD0_unpack(texstate_image_word0, &texstate_image_struct);

   return 1 << texstate_image_struct.smpcnt;
}

uint2
usclib_tex_state_address(uint4 tex_state)
{
   uint32_t texstate_image_word1[] = {tex_state.z, tex_state.w};
   struct ROGUE_TEXSTATE_IMAGE_WORD1 texstate_image_struct;

   ROGUE_TEXSTATE_IMAGE_WORD1_unpack(texstate_image_word1, &texstate_image_struct);

   uint64_t addr = texstate_image_struct.texaddr;

   return (uint2)(addr & 0xffffffff, addr >> 32);
}

uint
usclib_tex_state_array_max(uint4 tex_state)
{
   uint32_t texstate_image_word1[] = {tex_state.z, tex_state.w};
   struct ROGUE_TEXSTATE_IMAGE_WORD1 texstate_image_struct;

   ROGUE_TEXSTATE_IMAGE_WORD1_unpack(texstate_image_word1, &texstate_image_struct);

   return texstate_image_struct.depth;
}

uint3
usclib_tex_state_size(uint4 tex_state, uint num_comps, enum glsl_sampler_dim dim, bool is_array, bool is_image, uint lod)
{
   uint32_t texstate_image_word0[] = {tex_state.x, tex_state.y};
   uint32_t texstate_image_word1[] = {tex_state.z, tex_state.w};
   struct ROGUE_TEXSTATE_IMAGE_WORD0 texstate_image_struct0;
   struct ROGUE_TEXSTATE_IMAGE_WORD1 texstate_image_struct1;

   ROGUE_TEXSTATE_IMAGE_WORD0_unpack(texstate_image_word0, &texstate_image_struct0);
   ROGUE_TEXSTATE_IMAGE_WORD1_unpack(texstate_image_word1, &texstate_image_struct1);

   lod += texstate_image_struct1.baselevel;

   uint3 size_comps = (uint3)(texstate_image_struct0.width + 1,
                              texstate_image_struct0.height + 1,
                              texstate_image_struct1.depth + 1);

   if (is_array)
      --num_comps;

   for (unsigned u = 0; u < num_comps; ++u)
      size_comps[u] = MAX2(size_comps[u] >> lod, 1);

   if (is_array) {
      if (dim == GLSL_SAMPLER_DIM_1D)
         size_comps.y = size_comps.z;
      else if (dim == GLSL_SAMPLER_DIM_CUBE && is_image)
         size_comps.z /= 6;
   }

   return size_comps;
}

uint
usclib_tex_state_base_level(uint4 tex_state)
{
   uint32_t texstate_image_word1[] = {tex_state.z, tex_state.w};
   struct ROGUE_TEXSTATE_IMAGE_WORD1 texstate_image_struct;

   ROGUE_TEXSTATE_IMAGE_WORD1_unpack(texstate_image_word1, &texstate_image_struct);

   return texstate_image_struct.baselevel;
}

bool
usclib_smp_state_mipfilter(uint4 smp_state)
{
   uint32_t texstate_sampler_word0[] = {smp_state.x, smp_state.y};
   struct ROGUE_TEXSTATE_SAMPLER_WORD0 texstate_sampler_struct;

   ROGUE_TEXSTATE_SAMPLER_WORD0_unpack(texstate_sampler_word0, &texstate_sampler_struct);

   return texstate_sampler_struct.mipfilter;
}

float
usclib_tex_lod_dval_post_clamp_resource_to_view_space(uint4 tex_state, uint4 smp_state, float lod_dval_post_clamp)
{
   lod_dval_post_clamp -= (float)usclib_tex_state_base_level(tex_state);
   if (!usclib_smp_state_mipfilter(smp_state))
      lod_dval_post_clamp = floor(lod_dval_post_clamp + 0.5f);

   return MAX2(lod_dval_post_clamp, 0.0f);
}

/* TODO: this can probably be optimized with nir_interleave. */
uint32_t
usclib_twiddle3d(uint3 coords, uint3 size)
{
   uint32_t width = nir_umax(size.x, 4);
   width = util_next_power_of_two(width);

   uint32_t height = nir_umax(size.y, 4);
   height = util_next_power_of_two(height);

   uint32_t depth = nir_umax(size.z, 4);
   depth = util_next_power_of_two(depth);

   /* Get to the inner 4x4 cube. */
   width /= 4;
   height /= 4;
   depth /= 4;

   uint32_t cx = coords.x / 4;
   uint32_t cy = coords.y / 4;
   uint32_t cz = coords.z / 4;
   uint32_t shift = 0;
   uint32_t cubeoffset = 0;
   uint32_t i = 0;

   while (width > 1 || height > 1 || depth > 1) {
      uint32_t b1, b2, b3;

      if (height > 1) {
         b2 = ((cy & (1 << i)) >> i);
         cubeoffset |= (b2 << shift);
         shift++;
         height >>= 1;
      }

      if (width > 1) {
         b1 = ((cx & (1 << i)) >> i);
         cubeoffset |= (b1 << shift);
         shift++;
         width >>= 1;
      }

      if (depth > 1) {
         b3 = ((cz & (1 << i)) >> i);
         cubeoffset |= (b3 << shift);
         shift++;
         depth >>= 1;
      }

      ++i;
    }

    cubeoffset *= 4 * 4 * 4;

    /* Get to slice. */
    cubeoffset += 4 * 4 * (coords.z % 4);

    /* Twiddle within slice. */
    uint32_t r = (coords.y & 1) | ((coords.x & 1) << 1) | (((coords.y & 2) >> 1) << 2) | (((coords.x & 2) >> 1) << 3);

    return cubeoffset + r;
}
