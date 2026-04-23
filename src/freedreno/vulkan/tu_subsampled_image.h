/*
 * Copyright © 2026 Valve Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "tu_common.h"

#include <stdint.h>

/* Describe the format used for subsampled image metadata. This is attached to
 * subsampled images, via a separate UBO descriptor after the image
 * descriptor. It is written after the render pass which writes to the image,
 * and is read via code injected into the shader when sampling from a
 * subsampled image.
 */

/* The maximum number of bins a subsampled image can have before we disable
 * subsampling.
 */
#define TU_SUBSAMPLED_MAX_BINS 512

/* The maximum number of layers a view of a subsampled image can have.
 *
 * There is one metadata structure per layer, and the view uses a UBO for the
 * metadata, so this is bounded by the maximum UBO size.
 *
 * TODO: When we implement fdm2, we should expose this as
 * maxSubsampledArrayLayers. The Vulkan spec says that the minimum value for
 * maxSubsampledArrayLayers is 2, so users can only rely on 2 layers even
 * though we support more.
 */
#define TU_SUBSAMPLED_MAX_LAYERS 6

/* This is 2 to allow for floating-point precision errors and in case the user
 * uses bicubic filtering.
 */
#define APRON_SIZE 2

struct tu_subsampled_bin {
   float scale_x;
   float scale_y;
   float offset_x;
   float offset_y;
};

struct tu_subsampled_header {
   /* The bin coordinate to use is calculated as:
    * bin = int(coord * scale + offset)
    */
   float scale_x;
   float scale_y;
   float offset_x;
   float offset_y;

   uint32_t bin_stride;
   uint32_t pad0[3];
};

struct tu_subsampled_metadata {
   struct tu_subsampled_header hdr;

   struct tu_subsampled_bin bins[TU_SUBSAMPLED_MAX_BINS];
};

void
tu_emit_subsampled_metadata(struct tu_cmd_buffer *cmd,
                            struct tu_cs *cs,
                            unsigned a,
                            const struct tu_tile_config *tiles,
                            const struct tu_tiling_config *tiling,
                            const struct tu_vsc_config *vsc,
                            const struct tu_framebuffer *fb,
                            const VkOffset2D *fdm_offsets);

unsigned
tu_calc_subsampled_aprons(VkRect2D *dst,
                          struct tu_rect2d_float *src,
                          unsigned view,
                          const struct tu_tile_config *tiles,
                          const struct tu_tiling_config *tiling,
                          const struct tu_vsc_config *vsc,
                          const struct tu_framebuffer *fb,
                          const VkOffset2D *fdm_offsets);

nir_def *
tu_get_subsampled_coordinates(nir_builder *b,
                              nir_def *coords,
                              nir_def *descriptor);
