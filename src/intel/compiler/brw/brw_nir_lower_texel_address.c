/*
 * Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "isl/isl.h"

#include "brw_nir.h"
#include "compiler/nir/nir_builder.h"

struct lower_state {
   const struct intel_device_info *devinfo;
   enum isl_tiling tiling;
};

struct coord_swizzle {
   uint32_t addr_bit;
   uint32_t coord_bit;
   uint32_t n_bits;
};

#define EMPTY_SWIZZLE ((struct coord_swizzle) { \
         .addr_bit = UINT32_MAX, \
         .coord_bit = UINT32_MAX, \
         .n_bits = 0, \
      })

static nir_def *
add_swizzle(nir_builder *b,
            nir_def *old_offset,
            nir_def *coord,
            struct coord_swizzle swizzle)
{
   if (swizzle.n_bits == 0)
      return old_offset;

   nir_def *masked_coord =
      nir_iand_imm(b, coord,
                   ((1u << swizzle.n_bits) - 1) << swizzle.coord_bit);

   nir_def *bits;
   if (swizzle.addr_bit > swizzle.coord_bit)
      bits = nir_ishl_imm(b, masked_coord, swizzle.addr_bit - swizzle.coord_bit);
   else if (swizzle.addr_bit < swizzle.coord_bit)
      bits = nir_ushr_imm(b, masked_coord, swizzle.coord_bit - swizzle.addr_bit);
   else
      bits = masked_coord;

   return old_offset != NULL ? nir_ior(b, old_offset, bits) : bits;
}

/**
 * Adjust application coordinates to a vec3 suited for detiling.
 *
 * The detiling algorithm rely on u,v,r components but application coordinates
 * can skip a component for example for 1DArray image, we'll be handed a
 * vec2 : u,r
 */
static nir_def *
adjust_coords(nir_builder *b, nir_def *in_coords,
              enum glsl_sampler_dim dim, bool is_array)
{
   nir_def *coords[3];
   unsigned i = 0, n_dims = glsl_get_sampler_dim_coordinate_components(dim);
   for (; i < n_dims; i++)
      coords[i] = nir_channel(b, in_coords, i);
   for (; i < 2; i++)
      coords[i] = nir_imm_int(b, 0);
   if (i < 3) {
      coords[i++] = is_array ? nir_channel(b, in_coords, n_dims) :
         nir_imm_int(b, 0);
   }

   return nir_vec(b, coords, ARRAY_SIZE(coords));
}

static nir_def *
load_image_param(nir_builder *b, nir_def *surface_handle, unsigned index)
{
   unsigned num_components, bit_size;
   switch (index) {
   case ISL_SURF_PARAM_BASE_ADDRESSS:
      bit_size = 64;
      num_components = 1;
      break;
   case ISL_SURF_PARAM_TILE_MODE:
   case ISL_SURF_PARAM_PITCH:
   case ISL_SURF_PARAM_QPITCH:
      bit_size = 32;
      num_components = 1;
      break;
   default:
      UNREACHABLE("Invalid param offset");
   }

   return nir_image_deref_load_param_intel(b, num_components, bit_size,
                                           surface_handle,
                                           .base = index);
}

static nir_def *
image_linear_address(nir_builder *b,
                     unsigned bpp,
                     nir_def **coords,
                     nir_def *pitch,
                     nir_def *qpitch)
{
   nir_def *qpitch_in_bytes = nir_imul(b, qpitch, nir_imax_imm(b, pitch, bpp / 8));
   return nir_iadd(b,
                   nir_iadd(b,
                            nir_imul(b, coords[2], qpitch_in_bytes),
                            nir_imul(b, coords[1], pitch)),
                   coords[0]);
}

static enum isl_surf_dim
glsl_sampler_dim_to_isl(enum glsl_sampler_dim dim)
{
   return dim == GLSL_SAMPLER_DIM_1D ? ISL_SURF_DIM_1D :
      dim == GLSL_SAMPLER_DIM_3D ? ISL_SURF_DIM_3D :
      ISL_SURF_DIM_2D;
}

static nir_def *
image_tiled_address(nir_builder *b,
                    enum isl_tiling tiling,
                    enum glsl_sampler_dim dim,
                    bool is_array,
                    unsigned bpp,
                    nir_def **coords,
                    nir_def *pitch,
                    nir_def *qpitch)
{
   struct isl_tile_info tile_info;
   isl_tiling_get_info(tiling,
                       glsl_sampler_dim_to_isl(dim),
                       ISL_MSAA_LAYOUT_NONE,
                       bpp,
                       1,
                       &tile_info);

   /* With 2D images, the V coordinate should include the array layer since
    * the TileId/IntraTile offset should be computed using the QPitch not
    * being aligned to a tile's height.
    */
   if (dim == GLSL_SAMPLER_DIM_2D && is_array) {
      coords[1] = nir_iadd(b, coords[1], nir_imul(b, qpitch, coords[2]));
      coords[2] = nir_imm_int(b, 0);
   }

   /* Compute the intra tile offset using the swizzle (swizzles use u,v,r,p
    * but we do not support msaa images)
    */
   nir_def *tiled_addr = NULL;
   for (unsigned c = 0; c < 3; c++) {
      struct coord_swizzle swizzle = EMPTY_SWIZZLE;
      for (unsigned i = ffs(bpp / 8) - 1; i < tile_info.swiz_count; i++) {
         /* Bit is not for this component, ignore */
         if (ISL_ADDR_SWIZ_COMPONENT(tile_info.swiz[i]) != c) {
            tiled_addr = add_swizzle(b, tiled_addr, coords[c], swizzle);
            swizzle = EMPTY_SWIZZLE;
            continue;
         }

         const uint32_t coord_bit = ISL_ADDR_SWIZ_INDEX(tile_info.swiz[i]);

         swizzle.addr_bit = MIN2(i, swizzle.addr_bit);
         swizzle.coord_bit = MIN2(coord_bit, swizzle.coord_bit);
         swizzle.n_bits++;
      }

      tiled_addr = add_swizzle(b, tiled_addr, coords[c], swizzle);
   }

   /* Look at the used bits in the swizzle to figure out the tiling
    * coefficients.
    */
   isl_tile_extent coefficients =
      isl_swizzle_get_tile_coefficients(tile_info.swiz,
                                        tile_info.swiz_count,
                                        bpp / 8);

   /* Apply the generic tile id computation as described in the PRMs Volume
    * 5: Memory Data Formats:
    *
    *    "TileID = [(r » Cr) * (QPitch » Cv) + (v » Cv)] * (Pitch » Cu) + (u » Cu)"
    *
    * There is a different formula for 2D images:
    *    "TileID = (v » Cv) * (Pitch » Cu) + (u » Cu)"
    *    with v = (r * QPitch) + v
    *
    * But the 3D formula works for 2D with the array layer being moved into
    * the V coordinate.
    */
   nir_def *tile_id =
      nir_iadd(b,
               nir_imul(b,
                        nir_iadd(b,
                                 nir_imul(b,
                                          nir_ushr_imm(b, coords[2], coefficients.d),
                                          nir_ushr_imm(b, qpitch, coefficients.h)),
                                 nir_ushr_imm(b, coords[1], coefficients.h)),
                        nir_ushr_imm(b, pitch, coefficients.w)),
               nir_ushr_imm(b, coords[0], coefficients.w));

   return nir_iadd(b, tiled_addr,
                   nir_imul_imm(b, tile_id,
                                tile_info.phys_extent_B.w *
                                tile_info.phys_extent_B.h));
}


/** Calculate the offset in memory of the texel given by \p coord.
 *
 * This is meant to be used with untyped surface messages to access a tiled
 * surface, what involves taking into account the tiling.
 *
*/
static nir_def *
image_address(nir_builder *b,
              const struct intel_device_info *devinfo,
              enum isl_tiling tiling,
              enum glsl_sampler_dim dim,
              bool is_array,
              enum pipe_format format,
              nir_def *surface_handle,
              nir_def *coords_vec)
{
   const struct util_format_description *desc =
      util_format_description(format);
   const unsigned bpp = desc->block.bits;

   coords_vec = adjust_coords(b, coords_vec, dim, is_array);
   assert(coords_vec->num_components == 3);

   /* Ignore the bottom bits based on the format bpp */
   const unsigned start_bit = ffs(bpp / 8) - 1;

   /* Build coords with the x component in bytes */
   nir_def *coords[3];
   coords[0] = nir_ishl_imm(b, nir_channel(b, coords_vec, 0), start_bit);
   for (unsigned c = 1; c < ARRAY_SIZE(coords); c++)
      coords[c] = nir_channel(b, coords_vec, c);

   nir_def *pitch =
      load_image_param(b, surface_handle, ISL_SURF_PARAM_PITCH);
   nir_def *qpitch =
      load_image_param(b, surface_handle, ISL_SURF_PARAM_QPITCH);

   if (!isl_tiling_supports_dimensions(devinfo, tiling,
                                       glsl_sampler_dim_to_isl(dim)))
      return image_linear_address(b, bpp, coords, pitch, qpitch);

   nir_def *linear_addr = NULL;
   nir_def *tiled_addr = NULL;

   nir_def *tile_mode =
      load_image_param(b, surface_handle, ISL_SURF_PARAM_TILE_MODE);
   nir_push_if(b, nir_ieq_imm(b, tile_mode, 0));
   {
      linear_addr = image_linear_address(b, bpp, coords, pitch, qpitch);
   }
   nir_push_else(b, NULL);
   {
      tiled_addr = image_tiled_address(b, tiling, dim, is_array,
                                       bpp, coords, pitch, qpitch);
   }
   nir_pop_if(b, NULL);

   nir_def *addr = nir_if_phi(b, linear_addr, tiled_addr);

   return addr;
}

static bool
brw_nir_lower_texel_address_instr(nir_builder *b,
                                  nir_intrinsic_instr *intrin,
                                  void *data)
{
   struct lower_state *state = data;

   switch (intrin->intrinsic) {
   case nir_intrinsic_image_texel_address:
   case nir_intrinsic_image_deref_texel_address:
   case nir_intrinsic_bindless_image_texel_address:
      break;

   default:
      return false;
   }

   b->cursor = nir_instr_remove(&intrin->instr);

   nir_def *addr = nir_iadd(
      b,
      load_image_param(b, intrin->src[0].ssa, ISL_SURF_PARAM_BASE_ADDRESSS),
      nir_u2u64(b,
                image_address(b,
                              state->devinfo,
                              state->tiling,
                              nir_intrinsic_image_dim(intrin),
                              nir_intrinsic_image_array(intrin),
                              nir_intrinsic_format(intrin),
                              intrin->src[0].ssa,
                              intrin->src[1].ssa)));

   nir_def_rewrite_uses(&intrin->def, addr);

   return true;
}

bool
brw_nir_lower_texel_address(nir_shader *shader,
                            const struct intel_device_info *devinfo,
                            enum isl_tiling tiling)
{
   struct lower_state state = { .devinfo = devinfo, .tiling = tiling, };

   return nir_shader_intrinsics_pass(shader,
                                     brw_nir_lower_texel_address_instr,
                                     nir_metadata_none,
                                     &state);
}
