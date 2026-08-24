/*
 * Copyright © 2026 Valve Corporation.
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#include "tu_common.h"

#ifndef TU_TILE_CONFIG_H
#define TU_TILE_CONFIG_H

struct tu_tile_config {
   VkOffset2D pos;
   uint32_t pipe;
   uint32_t slot_mask;
   uint32_t visible_views;
   
   /* Whether to use subsampled_pos instead of the normal origin in
    * framebuffer space when storing this tile for subsampled images.
    */
   bool subsampled;

   /* If subsampled is true, whether custom resolves store to subsampled
    * images or not. If false, custom resolves ignore subsampled state.
    */
   bool custom_resolve_subsampled;

   /* If subsampled is true, whether this is a border tile that may not be
    * aligned.
    */
   bool subsampled_border;

   /* If subsampled is true, which views to store subsampled. If true, the
    * view is stored low-resolution as is, if false the view is expanded to
    * its full size in sysmem when resolving. However the origin of the tile
    * in subsampled space is always subsampled_pos when subsampled is true,
    * regardless of the value of this field.
    */
   uint32_t subsampled_views;

   /* Used internally. */
   unsigned worklist_idx;

   /* The tile this tile was merged with. */
   struct tu_tile_config *merged_tile;

   /* For subsampled images, the start of the tile in the final subsampled
    * image for each view. This may or may not be the start of the tile in
    * framebuffer space, due to the need to shift tiles over.
    */
   VkRect2D subsampled_pos[MAX_VIEWS];

   /* For merged tiles, the extent in tiles when resolved to system memory.
    */
   VkExtent2D sysmem_extent;

   /* For merged tiles, the extent in tiles in GMEM. This can only be more
    * than 1 if there is extra free space from an unused view.
    */
   VkExtent2D gmem_extent;

   VkExtent2D frag_areas[MAX_VIEWS];
};

/* After merging, follow the trail of merged_tile pointers back to the tile
 * this tile was ultimately merged with.
 */
static inline struct tu_tile_config *
tu_get_merged_tile(struct tu_tile_config *tile)
{
   while (tile->merged_tile)
      tile = tile->merged_tile;
   return tile;
}

static inline const struct tu_tile_config *
tu_get_merged_tile_const(const struct tu_tile_config *tile)
{
   while (tile->merged_tile)
      tile = tile->merged_tile;
   return tile;
}

struct tu_tile_config *
tu_calc_tile_config(struct tu_cmd_buffer *cmd, const struct tu_vsc_config *vsc,
                    const struct tu_image_view *fdm, const VkOffset2D *fdm_offsets);

void
tu_calc_bin_visibility(struct tu_cmd_buffer *cmd,
                       struct tu_tile_config *tile,
                       const VkOffset2D *offsets);

#endif
