/*
 * Copyright © 2026 Valve Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "tu_subsampled_image.h"

#include "nir/nir_builder.h"

#include "tu_cmd_buffer.h"

/* If a tile is not subsampled, we treat it as if its fragment area is (1,1)
 * for the purposes of subsampling.
 */
static VkExtent2D
get_effective_frag_area(const struct tu_tile_config *tile, unsigned view)
{
   return (tile->subsampled_views & (1u << view)) ?
      tile->frag_areas[view] : (VkExtent2D) {1, 1};
}

void
tu_emit_subsampled_metadata(struct tu_cmd_buffer *cmd,
                            struct tu_cs *cs,
                            unsigned a,
                            const struct tu_tile_config *tiles,
                            const struct tu_tiling_config *tiling,
                            const struct tu_vsc_config *vsc,
                            const struct tu_framebuffer *fb,
                            const VkOffset2D *fdm_offsets)
{
   const struct tu_image_view *iview = cmd->state.attachments[a];
   float size_ratio_x = (float)iview->image->vk.extent.width /
      iview->image->layout[0].width0;
   float size_ratio_y = (float)iview->image->vk.extent.height /
      iview->image->layout[0].height0;
   for_each_layer (i, cmd->state.pass->attachments[a].used_views |
                      cmd->state.pass->attachments[a].resolve_views,
                   fb->layers) {
      struct tu_subsampled_metadata metadata;

      metadata.hdr.pad0[0] = metadata.hdr.pad0[1] = metadata.hdr.pad0[2] = 0;

      unsigned tile_count;
      if (!tiles || vsc->tile_count.width * vsc->tile_count.height >
          TU_SUBSAMPLED_MAX_BINS) {
         tile_count = 1;
         metadata.hdr.scale_x = 1.0;
         metadata.hdr.scale_y = 1.0;
         metadata.hdr.offset_x = 0.0;
         metadata.hdr.offset_y = 0.0;
         metadata.hdr.bin_stride = 1;
         metadata.bins[0].scale_x = size_ratio_x;
         metadata.bins[0].scale_y = size_ratio_y;
         metadata.bins[0].offset_x = 0.0;
         metadata.bins[0].offset_y = 0.0;
      } else {
         unsigned view = MIN2(i, tu_fdm_num_layers(cmd) - 1);
         VkOffset2D bin_offset = {};
         if (fdm_offsets)
            bin_offset = tu_bin_offset(fdm_offsets[view], tiling);
         tile_count = vsc->tile_count.width * vsc->tile_count.height;
         metadata.hdr.scale_x = (float)iview->vk.extent.width / tiling->tile0.width;
         metadata.hdr.scale_y = (float)iview->vk.extent.height / tiling->tile0.height;
         metadata.hdr.offset_x = (float)bin_offset.x / tiling->tile0.width;
         metadata.hdr.offset_y = (float)bin_offset.y / tiling->tile0.height;
         metadata.hdr.bin_stride = vsc->tile_count.width;

         for (unsigned j = 0; j < tile_count; j++) {
            const struct tu_tile_config *tile = &tiles[j];

            while (tile->merged_tile)
               tile = tile->merged_tile;

            if (!(tile->visible_views & (1u << view)) ||
                !tile->subsampled) {
               metadata.bins[j].scale_x = metadata.bins[j].scale_y = 1.0;
               metadata.bins[j].offset_x = metadata.bins[j].offset_y = 0.0;
               continue;
            }

            VkExtent2D frag_area = get_effective_frag_area(tile, view);
            VkOffset2D fb_bin_start = (VkOffset2D) {
               MAX2(tile->pos.x * (int32_t)tiling->tile0.width - bin_offset.x, 0),
               MAX2(tile->pos.y * (int32_t)tiling->tile0.height - bin_offset.y, 0),
            };
            metadata.bins[j].scale_x = 1.0 / frag_area.width * size_ratio_x;
            metadata.bins[j].scale_y = 1.0 / frag_area.height * size_ratio_y;
            metadata.bins[j].offset_x =
               (float)(tile->subsampled_pos[view].offset.x -
                       fb_bin_start.x / frag_area.width) /
               iview->image->layout[0].width0;
            metadata.bins[j].offset_y =
               (float)(tile->subsampled_pos[view].offset.y -
                       fb_bin_start.y / frag_area.height) /
               iview->image->layout[0].height0;
         }
      }

      uint64_t iova = iview->image->iova +
         iview->image->subsampled_metadata_offset +
         sizeof(struct tu_subsampled_metadata) *
         (iview->vk.base_array_layer + i);

      tu_cs_emit_pkt7(cs, CP_MEM_WRITE,
                      2 + (sizeof(struct tu_subsampled_header) +
                           tile_count * sizeof(struct tu_subsampled_bin)) / 4);
      tu_cs_emit_qw(cs, iova);
      tu_cs_emit_array(cs, (const uint32_t *)&metadata.hdr,
                       sizeof(struct tu_subsampled_header) / 4);
      tu_cs_emit_array(cs, (const uint32_t *)&metadata.bins,
                       sizeof(struct tu_subsampled_bin) * tile_count / 4);
   }

   /* The cache-tracking infrastructure can't be aware of subsampled images,
    * so manually make sure the writes land. Sampling as an image should
    * already insert a CACHE_INVALIDATE + WFI.
    */
   cmd->state.cache.pending_flush_bits |=
      TU_CMD_FLAG_WAIT_MEM_WRITES;
}

nir_def *
tu_get_subsampled_coordinates(nir_builder *b,
                              nir_def *coords,
                              nir_def *descriptor)
{
   nir_def *layer;
   if (coords->num_components > 2)
      layer = nir_f2u16(b, nir_channel(b, coords, 2));
   else
      layer = nir_imm_intN_t(b, 0, 16);

   nir_def *layer_offset =
      nir_imul_imm_nuw(b, layer, sizeof(struct tu_subsampled_metadata) / 16);

   nir_def *hdr0 =
      nir_load_ubo(b, 4, 32, descriptor,
                   nir_ishl_imm(b, nir_u2u32(b, layer_offset), 4),
                   .align_mul = 16,
                   .align_offset = 0,
                   .range = TU_SUBSAMPLED_MAX_LAYERS * sizeof(struct tu_subsampled_metadata));
   nir_def *bin_stride =
      nir_load_ubo(b, 1, 32, descriptor, nir_ishl_imm(b, nir_u2u32(b, nir_iadd_imm(b, layer_offset, 1)), 4),
                   .align_mul = 16,
                   .align_offset = 0,
                   .range = TU_SUBSAMPLED_MAX_LAYERS * sizeof(struct tu_subsampled_metadata));

   nir_def *hdr_scale = nir_channels(b, hdr0, 0x3);
   nir_def *hdr_offset = nir_channels(b, hdr0, 0xc);

   nir_def *bin = nir_f2u16(b, nir_ffma(b, coords, hdr_scale, hdr_offset));
   nir_def *bin_idx = nir_iadd(b, nir_imul(b, nir_channel(b, bin, 1),
                                           nir_u2u16(b, bin_stride)),
                               nir_channel(b, bin, 0));

   bin_idx = nir_iadd_imm(b, nir_iadd(b, bin_idx, layer_offset),
                          sizeof(struct tu_subsampled_header) / 16);

   nir_def *bin_data =
      nir_load_ubo(b, 4, 32, descriptor, nir_ishl_imm(b, nir_u2u32(b, bin_idx), 4),
                   .align_mul = 16,
                   .align_offset = 0,
                   .range = TU_SUBSAMPLED_MAX_LAYERS * sizeof(struct tu_subsampled_metadata));

   nir_def *bin_scale = nir_channels(b, bin_data, 0x3);
   nir_def *bin_offset = nir_channels(b, bin_data, 0xc);

   return nir_ffma(b, coords, bin_scale, bin_offset);
}

/* Calculate the y coordinate in subsampled space of a given number of tiles
 * after the start of "tile".
 */
static void
calc_tile_vert_pos(const struct tu_tile_config *tile,
                   const struct tu_tiling_config *tiling,
                   const struct tu_framebuffer *fb,
                   unsigned view,
                   VkOffset2D bin_offset,
                   unsigned tile_offset,
                   unsigned *pos_y_out)
{
   int offset_px = 0;
   if (tile->pos.y == 0 && tile_offset > 0) {
      /* The first row is a partial row with FDM offset. */
      offset_px += tiling->tile0.height - bin_offset.y;
      tile_offset--;
   }
   offset_px += tiling->tile0.height * tile_offset;

   unsigned pos_y = tile->subsampled_pos[view].offset.y +
      offset_px / get_effective_frag_area(tile, view).height;

   /* The last tile is along the framebuffer edge, so clamp to the framebuffer
    * height.
    */
   *pos_y_out = MIN2(pos_y, tile->subsampled_pos[view].offset.y +
                     tile->subsampled_pos[view].extent.height);
}

static void
calc_tile_horiz_pos(const struct tu_tile_config *tile,
                    const struct tu_tiling_config *tiling,
                    const struct tu_framebuffer *fb,
                    unsigned view,
                    VkOffset2D bin_offset,
                    unsigned tile_offset,
                    unsigned *pos_x_out)
{
   int offset_px = 0;
   if (tile->pos.x == 0 && tile_offset > 0) {
      /* The first column is a partial column with FDM offset. */
      offset_px += tiling->tile0.width - bin_offset.x;
      tile_offset--;
   }
   offset_px += tiling->tile0.width * tile_offset;

   unsigned pos_x = tile->subsampled_pos[view].offset.x +
      offset_px / get_effective_frag_area(tile, view).width;

   /* The last tile is along the framebuffer edge, so clamp to the framebuffer
    * width.
    */
   *pos_x_out = MIN2(pos_x, tile->subsampled_pos[view].offset.x +
                     tile->subsampled_pos[view].extent.width);
}

/* Given two tiles "tile" and "other_tile", calculate the y coordinates of
 * their shared vertical edge in subsampled space relative to "tile". That is,
 * calculate the y coordinates along the edge of "tile" where "other_tile"
 * will touch it after scaling up to framebuffer coordinates. The start and
 * end may be the same coordinate if "tile" and "other_tile" only share a
 * corner, but this will be extended when handling corners.
 */
static void
calc_shared_vert_edge(const struct tu_tile_config *tile,
                      const struct tu_tile_config *other_tile,
                      const struct tu_tiling_config *tiling,
                      const struct tu_framebuffer *fb,
                      unsigned view,
                      VkOffset2D bin_offset,
                      unsigned *out_start,
                      unsigned *out_end)
{
   int other_start_tile = MAX2(other_tile->pos.y - tile->pos.y, 0);
   assert(other_start_tile <= tile->sysmem_extent.height);
   calc_tile_vert_pos(tile, tiling, fb, view, bin_offset,
                      other_start_tile, out_start);
   int other_end_tile =
      MIN2(tile->pos.y + tile->sysmem_extent.height,
           other_tile->pos.y + other_tile->sysmem_extent.height) - tile->pos.y;
   assert(other_end_tile >= 0);
   calc_tile_vert_pos(tile, tiling, fb, view, bin_offset,
                      other_end_tile, out_end);
}

static void
calc_shared_horiz_edge(const struct tu_tile_config *tile,
                       const struct tu_tile_config *other_tile,
                       const struct tu_tiling_config *tiling,
                       const struct tu_framebuffer *fb,
                       unsigned view,
                       VkOffset2D bin_offset,
                       unsigned *out_start,
                       unsigned *out_end)
{
   int other_start_tile = MAX2(other_tile->pos.x - tile->pos.x, 0);
   assert(other_start_tile <= tile->sysmem_extent.width);
   calc_tile_horiz_pos(tile, tiling, fb, view, bin_offset,
                       other_start_tile, out_start);
   int other_end_tile =
      MIN2(tile->pos.x + tile->sysmem_extent.width,
           other_tile->pos.x + other_tile->sysmem_extent.width) - tile->pos.x;
   assert(other_end_tile >= 0);
   calc_tile_horiz_pos(tile, tiling, fb, view, bin_offset,
                       other_end_tile, out_end);
}

/* Extend vertical-edge blit start and end for apron corners. */
static void
handle_vertical_corners(const struct tu_tile_config *tile,
                        const struct tu_tile_config *other_tile,
                        unsigned view,
                        VkRect2D *tile_dst,
                        struct tu_rect2d_float *other_src)
{
   float other_apron_height =
      (float)APRON_SIZE * get_effective_frag_area(tile, view).height /
      get_effective_frag_area(other_tile, view).height;
   if ((unsigned)other_src->y_start > other_tile->subsampled_pos[view].offset.y) {
      tile_dst->offset.y -= APRON_SIZE;
      tile_dst->extent.height += APRON_SIZE;
      other_src->y_start -= other_apron_height;
   }
   if ((unsigned)other_src->y_end <
       other_tile->subsampled_pos[view].offset.y +
       other_tile->subsampled_pos[view].extent.height) {
      tile_dst->extent.height += APRON_SIZE;
      other_src->y_end += other_apron_height;
   }
}

static void
handle_horizontal_corners(const struct tu_tile_config *tile,
                          const struct tu_tile_config *other_tile,
                          unsigned view,
                          VkRect2D *tile_dst,
                          struct tu_rect2d_float *other_src)
{
   float other_apron_width =
      (float)APRON_SIZE * get_effective_frag_area(tile, view).width /
      get_effective_frag_area(other_tile, view).width;
   if (other_src->x_start > other_tile->subsampled_pos[view].offset.x) {
      tile_dst->offset.x -= APRON_SIZE;
      tile_dst->extent.width += APRON_SIZE;
      other_src->x_start -= other_apron_width;
   }
   if ((unsigned)other_src->x_end <
       other_tile->subsampled_pos[view].offset.x +
       other_tile->subsampled_pos[view].extent.width) {
      tile_dst->extent.width += APRON_SIZE;
      other_src->x_end += other_apron_width;
   }
}
unsigned
tu_calc_subsampled_aprons(VkRect2D *dst,
                          struct tu_rect2d_float *src,
                          unsigned view,
                          const struct tu_tile_config *tiles,
                          const struct tu_tiling_config *tiling,
                          const struct tu_vsc_config *vsc,
                          const struct tu_framebuffer *fb,
                          const VkOffset2D *fdm_offsets)
{
   unsigned count = 0;

   VkOffset2D bin_offset = {};
   if (fdm_offsets)
      bin_offset = tu_bin_offset(fdm_offsets[view], tiling);

   for (unsigned y = 0; y < vsc->tile_count.height; y++) {
      for (unsigned x = 0; x < vsc->tile_count.width; x++) {
         const struct tu_tile_config *tile = &tiles[y * vsc->tile_count.width + x];

         if (tile->merged_tile || !(tile->visible_views & (1u << view)))
             continue;

         int x_neighbor = tile->pos.x + tile->sysmem_extent.width;
         int y_neighbor = tile->pos.y + tile->sysmem_extent.height;

         /* Start with vertically adjacent tiles. For a given neighbor to the
          * right, produce aprons for both this tile and its neighbor along
          * their shared edge. We handle tiles that only share an edge:
          *
          *     -------- -------
          *    |        |       |
          *    |  tile  | other |
          *    |        |       |
          *     -------- -------
          *
          * Tiles that only share a corner:
          *
          *              -------
          *             |       |
          *             | other |
          *             |       |
          *     -------- -------
          *    |        |
          *    |  tile  |
          *    |        |
          *     -------- 
          * 
          * And tiles where the corner of one tile comes from the edge of
          * another:
          *
          *              -------
          *             |       |
          *             |       |
          *             |       |
          *     --------| other |
          *    |        |       |
          *    |  tile  |       |
          *    |        |       |
          *     -------- -------
          *
          */
         if (x_neighbor < vsc->tile_count.width) {
            int y_start = MAX2(tile->pos.y - 1, 0);
            int y_end = MIN2(tile->pos.y + tile->sysmem_extent.height,
                             vsc->tile_count.height - 1);
            const struct tu_tile_config *other_tile;

            /* Sweep all tiles directly to the right, keeping in mind
             * merged tiles.
             */
            for (int y = y_start; y <= y_end;
                 y = other_tile->pos.y + other_tile->sysmem_extent.height) {
               other_tile = tu_get_merged_tile_const(&tiles[y * vsc->tile_count.width + x_neighbor]);

               if (!(other_tile->visible_views & (1u << view)))
                   continue;

               /* If they are next to each other then neither needs an apron. */
               if (tile->subsampled_pos[view].offset.x +
                   tile->subsampled_pos[view].extent.width ==
                   other_tile->subsampled_pos[view].offset.x)
                  continue;

               /* If other_tile isn't entirely to the right of tile, it is not
                * vertically adjacent and will be handled below instead.
                */
               if (other_tile->pos.x < tile->pos.x + tile->sysmem_extent.width)
                  continue;

               VkExtent2D frag_area = get_effective_frag_area(tile, view);
               VkExtent2D other_frag_area =
                  get_effective_frag_area(other_tile, view);

               unsigned tile_start, tile_end;
               calc_shared_vert_edge(tile, other_tile, tiling, fb, view,
                                     bin_offset, &tile_start, &tile_end);

               unsigned other_tile_start, other_tile_end;
               calc_shared_vert_edge(other_tile, tile, tiling, fb, view,
                                     bin_offset, &other_tile_start,
                                     &other_tile_end);

               VkRect2D tile_dst;

               tile_dst.offset.y = tile_start;
               tile_dst.extent.height = tile_end - tile_start;

               tile_dst.offset.x = tile->subsampled_pos[view].offset.x +
                  tile->subsampled_pos[view].extent.width;
               tile_dst.extent.width = APRON_SIZE;

               struct tu_rect2d_float other_src;

               other_src.x_start = other_tile->subsampled_pos[view].offset.x;
               other_src.x_end = other_src.x_start +
                  (float)APRON_SIZE * frag_area.width / other_frag_area.width;

               other_src.y_start = other_tile_start;
               other_src.y_end = other_tile_end;

               /* Extend start and end for apron corners. */
               handle_vertical_corners(tile, other_tile, view, &tile_dst,
                                       &other_src);

               /* Add other_tile -> tile blit to the list. */
               dst[count] = tile_dst;
               src[count] = other_src;
               count++;

               VkRect2D other_dst;

               other_dst.offset.y = other_tile_start;
               other_dst.extent.height = other_tile_end - other_tile_start;

               other_dst.offset.x =
                  other_tile->subsampled_pos[view].offset.x - APRON_SIZE;
               other_dst.extent.width = APRON_SIZE;

               struct tu_rect2d_float tile_src;

               tile_src.x_end = tile->subsampled_pos[view].offset.x
                  + tile->subsampled_pos[view].extent.width;
               tile_src.x_start = tile_src.x_end -
                  (float)APRON_SIZE * other_frag_area.width / frag_area.width;

               tile_src.y_start = tile_start;
               tile_src.y_end = tile_end;

               handle_vertical_corners(other_tile, tile, view, &other_dst,
                                       &tile_src);

               /* Add tile -> other_tile blit to the list. */
               dst[count] = other_dst;
               src[count] = tile_src;
               count++;
            }
         }

         /* Now do the same thing but for horizontally adjacent tiles. Because
          * the above loop handled tiles that only share a corner, we only
          * have to handle neighbors below it that share an edge. However,
          * these neighbors may also share a corner if they are merged tiles.
          */
         if (y_neighbor < vsc->tile_count.height) {
            const struct tu_tile_config *other_tile;

            /* Sweep all tiles directly below, keeping in mind merged tiles.
             */
            for (int x = tile->pos.x;
                 x < tile->pos.x + tile->sysmem_extent.width;
                 x = other_tile->pos.x + other_tile->sysmem_extent.width) {
               other_tile = tu_get_merged_tile_const(&tiles[y_neighbor * vsc->tile_count.width + x]);

               if (!(other_tile->visible_views & (1u << view)))
                   continue;

               /* If both are next to each other then neither needs an apron. */
               if (tile->subsampled_pos[view].offset.y +
                   tile->subsampled_pos[view].extent.height ==
                   other_tile->subsampled_pos[view].offset.y)
                  continue;

               VkExtent2D frag_area = get_effective_frag_area(tile, view);
               VkExtent2D other_frag_area =
                  get_effective_frag_area(other_tile, view);

               unsigned tile_start, tile_end;
               calc_shared_horiz_edge(tile, other_tile, tiling, fb, view,
                                      bin_offset, &tile_start, &tile_end);

               unsigned other_tile_start, other_tile_end;
               calc_shared_horiz_edge(other_tile, tile, tiling, fb, view,
                                      bin_offset, &other_tile_start,
                                      &other_tile_end);

               VkRect2D tile_dst;

               tile_dst.offset.x = tile_start;
               tile_dst.extent.width = tile_end - tile_start;

               tile_dst.offset.y = tile->subsampled_pos[view].offset.y +
                  tile->subsampled_pos[view].extent.height;
               tile_dst.extent.height = APRON_SIZE;

               struct tu_rect2d_float other_src;

               other_src.y_start = other_tile->subsampled_pos[view].offset.y;
               other_src.y_end = other_src.y_start +
                  (float)APRON_SIZE * frag_area.height / other_frag_area.height;

               other_src.x_start = other_tile_start;
               other_src.x_end = other_tile_end;

               /* Extend start and end for apron corners. */
               handle_horizontal_corners(tile, other_tile, view, &tile_dst,
                                         &other_src);

               /* Add other_tile -> tile blit to the list. */
               dst[count] = tile_dst;
               src[count] = other_src;
               assert(tile_dst.offset.x >= 0);
               assert(tile_dst.offset.y >= 0);
               count++;

               VkRect2D other_dst;

               other_dst.offset.x = other_tile_start;
               other_dst.extent.width = other_tile_end - other_tile_start;

               other_dst.offset.y =
                  other_tile->subsampled_pos[view].offset.y - APRON_SIZE;
               other_dst.extent.height = APRON_SIZE;

               struct tu_rect2d_float tile_src;

               tile_src.y_end = tile->subsampled_pos[view].offset.y
                  + tile->subsampled_pos[view].extent.height;
               tile_src.y_start = tile_src.y_end -
                  (float)APRON_SIZE * other_frag_area.height / frag_area.height;

               tile_src.x_start = tile_start;
               tile_src.x_end = tile_end;

               handle_horizontal_corners(other_tile, tile, view, &other_dst,
                                         &tile_src);

               /* Add tile -> other_tile blit to the list. */
               dst[count] = other_dst;
               src[count] = tile_src;
               assert(other_dst.offset.x >= 0);
               assert(other_dst.offset.y >= 0);
               count++;
            }
         }
      }
   }

   return count;
}
