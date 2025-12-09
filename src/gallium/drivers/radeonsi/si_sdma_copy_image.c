/*
 * Copyright 2010 Jerome Glisse <glisse@freedesktop.org>
 * Copyright 2015-2021 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_build_pm4.h"
#include "sid.h"
#include "util/u_memory.h"
#include "ac_formats.h"
#include "ac_cmdbuf_sdma.h"

static
bool si_prepare_for_sdma_copy(struct si_context *sctx, struct si_texture *dst,struct si_texture *src)
{
   if (dst->surface.bpe != src->surface.bpe)
      return false;

   /* MSAA: Blits don't exist in the real world. */
   if (src->buffer.b.b.nr_samples > 1 || dst->buffer.b.b.nr_samples > 1)
      return false;

   if (dst->buffer.b.b.last_level != 0 || src->buffer.b.b.last_level != 0)
      return false;

   return true;
}

static unsigned minify_as_blocks(unsigned width, unsigned level, unsigned blk_w)
{
   width = u_minify(width, level);
   return DIV_ROUND_UP(width, blk_w);
}

static bool si_sdma_v4_v5_copy_texture(struct si_context *sctx, struct si_texture *sdst,
                                       struct si_texture *ssrc)
{
   bool is_v5 = sctx->gfx_level >= GFX10;
   bool is_v7 = sctx->gfx_level >= GFX12;
   unsigned bpp = sdst->surface.bpe;
   uint64_t dst_address = sdst->buffer.gpu_address + sdst->surface.u.gfx9.surf_offset;
   uint64_t src_address = ssrc->buffer.gpu_address + ssrc->surface.u.gfx9.surf_offset;
   unsigned dst_pitch = sdst->surface.u.gfx9.surf_pitch;
   unsigned src_pitch = ssrc->surface.u.gfx9.surf_pitch;
   unsigned copy_width = DIV_ROUND_UP(ssrc->buffer.b.b.width0, ssrc->surface.blk_w);
   unsigned copy_height = DIV_ROUND_UP(ssrc->buffer.b.b.height0, ssrc->surface.blk_h);

   bool tmz = (ssrc->buffer.flags & RADEON_FLAG_ENCRYPTED);
   assert (!tmz || (sdst->buffer.flags & RADEON_FLAG_ENCRYPTED));

   /* Linear -> linear sub-window copy. */
   if (ssrc->surface.is_linear && sdst->surface.is_linear) {
      struct radeon_cmdbuf *cs = sctx->sdma_cs;

      uint64_t bytes = (uint64_t)src_pitch * copy_height * bpp;

      src_address += ssrc->surface.u.gfx9.offset[0];
      dst_address += sdst->surface.u.gfx9.offset[0];

      while (bytes > 0) {
         uint64_t bytes_written =
            ac_emit_sdma_copy_linear(&cs->current, sctx->screen->info.sdma_ip_version,
                                     src_address, dst_address, bytes, tmz);

         bytes -= bytes_written;
         src_address += bytes_written;
         dst_address += bytes_written;
      }

      return true;
   }

   /* Linear <-> Tiled sub-window copy */
   if (ssrc->surface.is_linear != sdst->surface.is_linear) {
      struct si_texture *tiled = ssrc->surface.is_linear ? sdst : ssrc;
      struct si_texture *linear = tiled == ssrc ? sdst : ssrc;
      unsigned tiled_width = DIV_ROUND_UP(tiled->buffer.b.b.width0, tiled->surface.blk_w);
      unsigned tiled_height = DIV_ROUND_UP(tiled->buffer.b.b.height0, tiled->surface.blk_h);
      unsigned linear_pitch = linear == ssrc ? src_pitch : dst_pitch;
      uint64_t linear_slice_pitch = linear->surface.u.gfx9.surf_slice_size / bpp;
      uint64_t tiled_address = tiled == ssrc ? src_address : dst_address;
      uint64_t linear_address = linear == ssrc ? src_address : dst_address;
      struct radeon_cmdbuf *cs = sctx->sdma_cs;
      assert(tiled->buffer.b.b.depth0 == 1);
      bool dcc;

      if (is_v7) {
         /* Compress only when dst has DCC. If src has DCC, it automatically decompresses according
          * to PTE.D (page table bit) even if we don't enable DCC in the packet.
          */
         dcc = tiled == sdst &&
               tiled->buffer.flags & RADEON_FLAG_GFX12_ALLOW_DCC;

         /* Check if everything fits into the bitfields */
         if (!(tiled_width <= (1 << 16) && tiled_height <= (1 << 16) &&
               linear_pitch <= (1 << 16) && linear_slice_pitch <= (1ull << 32) &&
               copy_width <= (1 << 16) && copy_height <= (1 << 16)))
            return false;
      } else {
         /* Only SDMA 5 supports DCC with SDMA */
         dcc = is_v5 && vi_dcc_enabled(tiled, 0);

         /* Check if everything fits into the bitfields */
         if (!(tiled_width <= (1 << 14) && tiled_height <= (1 << 14) &&
               linear_pitch <= (1 << 14) && linear_slice_pitch <= (1 << 28) &&
               copy_width <= (1 << 14) && copy_height <= (1 << 14)))
            return false;
      }

      linear_address += linear->surface.u.gfx9.offset[0];

      const uint64_t md_address = dcc ? tiled_address + tiled->surface.meta_offset : 0;
      const bool detile = linear == sdst;

      const struct ac_sdma_surf_linear surf_linear = {
         .va = linear_address,
         .offset =
            {
               .x = 0,
               .y = 0,
               .z = 0,
            },
         .pitch = linear_pitch,
         .slice_pitch = linear_slice_pitch,
      };

      const struct ac_sdma_surf_tiled surf_tiled = {
         .surf = &tiled->surface,
         .va = tiled_address | (tiled->surface.tile_swizzle << 8),
         .format = tiled->buffer.b.b.format,
         .bpp = bpp,
         .offset =
            {
               .x = 0,
               .y = 0,
               .z = 0,
            },
         .extent = {
            .width = tiled_width,
            .height = tiled_height,
            .depth = 1,
         },
         .first_level = 0,
         .num_levels = tiled->buffer.b.b.last_level + 1,
         .is_compressed = dcc,
         .surf_type = 0,
         .meta_va = md_address,
         .htile_enabled = false,
      };

      ac_emit_sdma_copy_tiled_sub_window(&cs->current, &sctx->screen->info,
                                         &surf_linear, &surf_tiled, detile,
                                         copy_width, copy_height, 1, tmz);

      return true;
   }

   return false;
}

static
bool cik_sdma_copy_texture(struct si_context *sctx, struct si_texture *sdst, struct si_texture *ssrc)
{
   struct radeon_info *info = &sctx->screen->info;
   unsigned bpp = sdst->surface.bpe;
   uint64_t dst_address = sdst->buffer.gpu_address + sdst->surface.u.legacy.level[0].offset_256B * 256;
   uint64_t src_address = ssrc->buffer.gpu_address + ssrc->surface.u.legacy.level[0].offset_256B * 256;
   unsigned dst_mode = sdst->surface.u.legacy.level[0].mode;
   unsigned src_mode = ssrc->surface.u.legacy.level[0].mode;
   unsigned dst_tile_index = sdst->surface.u.legacy.tiling_index[0];
   unsigned src_tile_index = ssrc->surface.u.legacy.tiling_index[0];
   unsigned dst_tile_mode = info->si_tile_mode_array[dst_tile_index];
   unsigned src_tile_mode = info->si_tile_mode_array[src_tile_index];
   unsigned dst_micro_mode = G_009910_MICRO_TILE_MODE_NEW(dst_tile_mode);
   unsigned src_micro_mode = G_009910_MICRO_TILE_MODE_NEW(src_tile_mode);
   unsigned dst_tile_swizzle = dst_mode == RADEON_SURF_MODE_2D ? sdst->surface.tile_swizzle : 0;
   unsigned src_tile_swizzle = src_mode == RADEON_SURF_MODE_2D ? ssrc->surface.tile_swizzle : 0;
   unsigned dst_pitch = sdst->surface.u.legacy.level[0].nblk_x;
   unsigned src_pitch = ssrc->surface.u.legacy.level[0].nblk_x;
   uint64_t dst_slice_pitch =
      ((uint64_t)sdst->surface.u.legacy.level[0].slice_size_dw * 4) / bpp;
   uint64_t src_slice_pitch =
      ((uint64_t)ssrc->surface.u.legacy.level[0].slice_size_dw * 4) / bpp;
   unsigned dst_width = minify_as_blocks(sdst->buffer.b.b.width0, 0, sdst->surface.blk_w);
   unsigned src_width = minify_as_blocks(ssrc->buffer.b.b.width0, 0, ssrc->surface.blk_w);
   unsigned copy_width = DIV_ROUND_UP(ssrc->buffer.b.b.width0, ssrc->surface.blk_w);
   unsigned copy_height = DIV_ROUND_UP(ssrc->buffer.b.b.height0, ssrc->surface.blk_h);

   dst_address |= dst_tile_swizzle << 8;
   src_address |= src_tile_swizzle << 8;

   /* Linear -> linear sub-window copy. */
   if (dst_mode == RADEON_SURF_MODE_LINEAR_ALIGNED && src_mode == RADEON_SURF_MODE_LINEAR_ALIGNED &&
       /* check if everything fits into the bitfields */
       src_pitch <= (1 << 14) && dst_pitch <= (1 << 14) && src_slice_pitch <= (1 << 28) &&
       dst_slice_pitch <= (1 << 28) && copy_width <= (1 << 14) && copy_height <= (1 << 14) &&
       /* HW limitation - GFX7: */
       (sctx->gfx_level != GFX7 ||
        (copy_width < (1 << 14) && copy_height < (1 << 14))) &&
       /* HW limitation - some GFX7 parts: */
       ((sctx->family != CHIP_BONAIRE && sctx->family != CHIP_KAVERI) ||
        (copy_width != (1 << 14) && copy_height != (1 << 14)))) {
      struct radeon_cmdbuf *cs = sctx->sdma_cs;

      const struct ac_sdma_surf_linear surf_src = {
         .va = src_address,
         .offset =
            {
               .x = 0,
               .y = 0,
               .z = 0,
            },
         .bpp = bpp,
         .pitch = src_pitch,
         .slice_pitch = src_slice_pitch,
      };

      const struct ac_sdma_surf_linear surf_dst = {
         .va = dst_address,
         .offset =
            {
               .x = 0,
               .y = 0,
               .z = 0,
            },
         .bpp = bpp,
         .pitch = dst_pitch,
         .slice_pitch = dst_slice_pitch,
      };

      ac_emit_sdma_copy_linear_sub_window(&cs->current, info->sdma_ip_version,
                                          &surf_src, &surf_dst, copy_width,
                                          copy_height, 1);
      return true;
   }

   /* Tiled <-> linear sub-window copy. */
   if ((src_mode >= RADEON_SURF_MODE_1D) != (dst_mode >= RADEON_SURF_MODE_1D)) {
      struct si_texture *tiled = src_mode >= RADEON_SURF_MODE_1D ? ssrc : sdst;
      struct si_texture *linear = tiled == ssrc ? sdst : ssrc;
      unsigned tiled_width = tiled == ssrc ? src_width : dst_width;
      unsigned linear_width = linear == ssrc ? src_width : dst_width;
      unsigned tiled_pitch = tiled == ssrc ? src_pitch : dst_pitch;
      unsigned linear_pitch = linear == ssrc ? src_pitch : dst_pitch;
      unsigned tiled_slice_pitch = tiled == ssrc ? src_slice_pitch : dst_slice_pitch;
      unsigned linear_slice_pitch = linear == ssrc ? src_slice_pitch : dst_slice_pitch;
      uint64_t tiled_address = tiled == ssrc ? src_address : dst_address;
      uint64_t linear_address = linear == ssrc ? src_address : dst_address;
      unsigned tiled_micro_mode = tiled == ssrc ? src_micro_mode : dst_micro_mode;

      assert(tiled_pitch % 8 == 0);
      assert(tiled_slice_pitch % 64 == 0);
      unsigned pitch_tile_max = tiled_pitch / 8 - 1;
      unsigned slice_tile_max = tiled_slice_pitch / 64 - 1;
      unsigned xalign = MAX2(1, 4 / bpp);
      unsigned copy_width_aligned = copy_width;

      /* If the region ends at the last pixel and is unaligned, we
       * can copy the remainder of the line that is not visible to
       * make it aligned.
       */
      if (copy_width % xalign != 0 && 0 + copy_width == linear_width &&
          copy_width == tiled_width &&
          align(copy_width, xalign) <= linear_pitch &&
          align(copy_width, xalign) <= tiled_pitch)
         copy_width_aligned = align(copy_width, xalign);

      /* HW limitations. */
      if ((sctx->family == CHIP_BONAIRE || sctx->family == CHIP_KAVERI) &&
          linear_pitch - 1 == 0x3fff && bpp == 16)
         return false;

      if ((sctx->family == CHIP_BONAIRE || sctx->family == CHIP_KAVERI ||
           sctx->family == CHIP_KABINI) &&
          (copy_width == (1 << 14) || copy_height == (1 << 14)))
         return false;

      /* The hw can read outside of the given linear buffer bounds,
       * or access those pages but not touch the memory in case
       * of writes. (it still causes a VM fault)
       *
       * Out-of-bounds memory access or page directory access must
       * be prevented.
       */
      int64_t start_linear_address, end_linear_address;
      unsigned granularity;

      /* Deduce the size of reads from the linear surface. */
      switch (tiled_micro_mode) {
      case V_009910_ADDR_SURF_DISPLAY_MICRO_TILING:
         granularity = bpp == 1 ? 64 / (8 * bpp) : 128 / (8 * bpp);
         break;
      case V_009910_ADDR_SURF_THIN_MICRO_TILING:
      case V_009910_ADDR_SURF_DEPTH_MICRO_TILING:
         if (0 /* TODO: THICK microtiling */)
            granularity =
               bpp == 1 ? 32 / (8 * bpp)
                        : bpp == 2 ? 64 / (8 * bpp) : bpp <= 8 ? 128 / (8 * bpp) : 256 / (8 * bpp);
         else
            granularity = bpp <= 2 ? 64 / (8 * bpp) : bpp <= 8 ? 128 / (8 * bpp) : 256 / (8 * bpp);
         break;
      default:
         return false;
      }

      /* The linear reads start at tiled_x & ~(granularity - 1).
       * If linear_x == 0 && tiled_x % granularity != 0, the hw
       * starts reading from an address preceding linear_address!!!
       */
      start_linear_address =
         (uint64_t)linear->surface.u.legacy.level[0].offset_256B * 256;

      end_linear_address =
         (uint64_t)linear->surface.u.legacy.level[0].offset_256B * 256 +
         bpp * ((copy_height - 1) * (uint64_t)linear_pitch + copy_width);

      if ((0 + copy_width) % granularity)
         end_linear_address += granularity - (0 + copy_width) % granularity;

      if (start_linear_address < 0 || end_linear_address > linear->surface.surf_size)
         return false;

      /* Check requirements. */
      if (tiled_address % 256 == 0 && linear_address % 4 == 0 && linear_pitch % xalign == 0 &&
          copy_width_aligned % xalign == 0 &&
          tiled_micro_mode != V_009910_ADDR_SURF_ROTATED_MICRO_TILING &&
          /* check if everything fits into the bitfields */
          tiled->surface.u.legacy.tile_split <= 4096 && pitch_tile_max < (1 << 11) &&
          slice_tile_max < (1 << 22) && linear_pitch <= (1 << 14) &&
          linear_slice_pitch <= (1 << 28) && copy_width_aligned <= (1 << 14) &&
          copy_height <= (1 << 14)) {
         struct radeon_cmdbuf *cs = sctx->sdma_cs;
         const bool detile = linear == sdst;

         const struct ac_sdma_surf_linear surf_linear = {
            .va = linear_address,
            .offset =
               {
                  .x = 0,
                  .y = 0,
                  .z = 0,
               },
            .pitch = linear_pitch,
            .slice_pitch = linear_slice_pitch,
         };

         const struct ac_sdma_surf_tiled surf_tiled = {
            .surf = &tiled->surface,
            .va = tiled_address,
            .bpp = bpp,
            .offset =
               {
                  .x = 0,
                  .y = 0,
                  .z = 0,
               },
            .extent =
               {
                  .width = pitch_tile_max + 1,
                  .height = slice_tile_max + 1,
                  .depth = 1,
               },
            .first_level = 0,
            .num_levels = 1,
            .is_compressed = false,
            .htile_enabled = false,
         };

         ac_emit_sdma_copy_tiled_sub_window(&cs->current, info, &surf_linear,
                                            &surf_tiled, detile, copy_width_aligned,
                                            copy_height, 1, false);
         return true;
      }
   }

   return false;
}

bool si_sdma_copy_image(struct si_context *sctx, struct si_texture *dst, struct si_texture *src)
{
   struct radeon_winsys *ws = sctx->ws;

   if (!sctx->sdma_cs) {
      if (sctx->screen->debug_flags & DBG(NO_DMA) || sctx->gfx_level < GFX7)
         return false;

      sctx->sdma_cs = CALLOC_STRUCT(radeon_cmdbuf);
      if (!ws->cs_create(sctx->sdma_cs, sctx->ctx, AMD_IP_SDMA, NULL, NULL)) {
         sctx->screen->debug_flags |= DBG(NO_DMA);
         return false;
      }
   }

   if (!si_prepare_for_sdma_copy(sctx, dst, src))
      return false;

   /* TODO: DCC compression is possible on GFX10+. See si_set_mutable_tex_desc_fields for
    * additional constraints.
    * For now, the only use-case of SDMA is DRI_PRIME tiled->linear copy, and linear dst
    * never has DCC.
    */
   if (vi_dcc_enabled(dst, 0))
      return false;

   /* Decompress DCC on older chips where SDMA can't read it. */
   if (vi_dcc_enabled(src, 0) && sctx->gfx_level < GFX10)
      si_decompress_dcc(sctx, src);

   /* Always flush the gfx queue to get the winsys to handle the dependencies for us. */
   si_flush_gfx_cs(sctx, 0, NULL);

   switch (sctx->gfx_level) {
      case GFX7:
      case GFX8:
         if (!cik_sdma_copy_texture(sctx, dst, src))
            return false;
         break;
      case GFX9:
      case GFX10:
      case GFX10_3:
      case GFX11:
      case GFX11_5:
      case GFX12:
         if (!si_sdma_v4_v5_copy_texture(sctx, dst, src))
            return false;
         break;
      default:
         return false;
   }

   radeon_add_to_buffer_list(sctx, sctx->sdma_cs, &src->buffer, RADEON_USAGE_READ |
                             RADEON_PRIO_SAMPLER_TEXTURE);
   radeon_add_to_buffer_list(sctx, sctx->sdma_cs, &dst->buffer, RADEON_USAGE_WRITE |
                             RADEON_PRIO_SAMPLER_TEXTURE);

   unsigned flags = RADEON_FLUSH_START_NEXT_GFX_IB_NOW;
   if (unlikely(radeon_uses_secure_bos(sctx->ws))) {
      if ((bool) (src->buffer.flags & RADEON_FLAG_ENCRYPTED) !=
          sctx->ws->cs_is_secure(sctx->sdma_cs)) {
         flags = RADEON_FLUSH_TOGGLE_SECURE_SUBMISSION;
      }
   }

   return ws->cs_flush(sctx->sdma_cs, flags, NULL) == 0;
}
