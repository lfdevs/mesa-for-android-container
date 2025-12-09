/*
 * Copyright © 2011 Red Hat All Rights Reserved.
 * Copyright © 2014 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "amdgpu_winsys.h"
#include "util/format/u_format.h"

static int amdgpu_surface_sanity(const struct pipe_resource *tex)
{
   switch (tex->target) {
   case PIPE_TEXTURE_1D:
      if (tex->height0 > 1)
         return -EINVAL;
      FALLTHROUGH;
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_RECT:
      if (tex->depth0 > 1 || tex->array_size > 1)
         return -EINVAL;
      break;
   case PIPE_TEXTURE_3D:
      if (tex->array_size > 1)
         return -EINVAL;
      break;
   case PIPE_TEXTURE_1D_ARRAY:
      if (tex->height0 > 1)
         return -EINVAL;
      FALLTHROUGH;
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_CUBE_ARRAY:
      if (tex->depth0 > 1)
         return -EINVAL;
      break;
   default:
      return -EINVAL;
   }
   return 0;
}

static int amdgpu_surface_init(struct radeon_winsys *rws,
                               const struct radeon_info *info,
                               const struct pipe_resource *tex,
                               uint64_t flags, unsigned bpe,
                               enum radeon_surf_mode mode,
                               struct radeon_surf *surf)
{
   struct amdgpu_winsys *aws = amdgpu_winsys(rws);
   int r;

   r = amdgpu_surface_sanity(tex);
   if (r)
      return r;

   struct ac_surf_config config;

   config.info.width = tex->width0;
   config.info.height = tex->height0;
   config.info.depth = tex->depth0;
   config.info.array_size = tex->array_size;
   config.info.samples = tex->nr_samples;
   config.info.storage_samples = tex->nr_storage_samples;
   config.info.levels = tex->last_level + 1;
   config.info.num_channels = util_format_get_nr_components(tex->format);
   config.is_1d = tex->target == PIPE_TEXTURE_1D ||
                  tex->target == PIPE_TEXTURE_1D_ARRAY;
   config.is_3d = tex->target == PIPE_TEXTURE_3D;
   config.is_cube = tex->target == PIPE_TEXTURE_CUBE;
   config.is_array = tex->target == PIPE_TEXTURE_1D_ARRAY ||
                     tex->target == PIPE_TEXTURE_2D_ARRAY ||
                     tex->target == PIPE_TEXTURE_CUBE_ARRAY;
   config.blk_w = util_format_get_blockwidth(tex->format);
   config.blk_h = util_format_get_blockheight(tex->format);
   config.bpe = bpe;
   config.surf_flags = flags;
   config.modifier = surf->modifier;

   if (info->gfx_level >= GFX9) {
      config.gfx9.swizzle_mode = surf->u.gfx9.swizzle_mode;
      config.gfx9.dcc_number_type = surf->u.gfx9.dcc_number_type;
      config.gfx9.dcc_data_format = surf->u.gfx9.dcc_data_format;
      config.gfx9.dcc_max_compressed_block_size = surf->u.gfx9.color.dcc.max_compressed_block_size;
      config.gfx9.dcc_independent_64B_blocks = surf->u.gfx9.color.dcc.independent_64B_blocks;
      config.gfx9.dcc_independent_128B_blocks = surf->u.gfx9.color.dcc.independent_128B_blocks;
      config.gfx9.dcc_write_compress_disable = surf->u.gfx9.dcc_write_compress_disable;
      config.gfx9.display_dcc_pitch_max = surf->u.gfx9.color.display_dcc_pitch_max;
   } else {
      config.gfx6.pipe_config = surf->u.legacy.pipe_config;
      config.gfx6.bankw = surf->u.legacy.bankw;
      config.gfx6.bankh = surf->u.legacy.bankh;
      config.gfx6.tile_split = surf->u.legacy.tile_split;
      config.gfx6.mtilea = surf->u.legacy.mtilea;
      config.gfx6.num_banks = surf->u.legacy.num_banks;
   }

   /* Use radeon_info from the driver, not the winsys. The driver is allowed to change it. */
   return ac_compute_surface(aws->addrlib, info, &config, mode, surf);
}

static uint64_t
amdgpu_surface_offset_from_coord(struct radeon_winsys *rws,
                                 const struct radeon_info *info,
                                 const struct radeon_surf *surf,
                                 const struct pipe_resource *tex,
                                 unsigned level, unsigned x,
                                 unsigned y, unsigned layer)
{
   struct amdgpu_winsys *aws = amdgpu_winsys(rws);
   unsigned samples = MAX2(1, tex->nr_samples);

   const struct ac_surf_info surf_info = {
      .width = tex->width0,
      .height = tex->height0,
      .depth = tex->depth0,
      .samples = samples,
      .storage_samples = samples,
      .levels = tex->last_level + 1,
      .num_channels = util_format_get_last_component(tex->format) + 1,
      .array_size = tex->array_size,
   };

   return ac_surface_addr_from_coord(
      aws->addrlib, info, surf, &surf_info,
      level, x, y, layer, tex->target == PIPE_TEXTURE_3D);
}

void amdgpu_surface_init_functions(struct amdgpu_screen_winsys *sws)
{
   sws->base.surface_init = amdgpu_surface_init;
   sws->base.surface_offset_from_coord = amdgpu_surface_offset_from_coord;
}
