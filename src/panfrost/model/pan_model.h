/*
 * Copyright (C) 2019 Collabora, Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#ifndef PAN_MODEL_H
#define PAN_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#include "util/macros.h"

/** Implementation-defined tiler features */
struct pan_tiler_features {
   /** Number of bytes per tiler bin */
   unsigned bin_size;

   /** Maximum number of levels that may be simultaneously enabled.
    * Invariant: bitcount(hierarchy_mask) <= max_levels */
   unsigned max_levels;
};

#define ARCH_MAJOR     BITFIELD_RANGE(28, 4)
#define ARCH_MINOR     BITFIELD_RANGE(24, 4)
#define ARCH_REV       BITFIELD_RANGE(20, 4)
#define PRODUCT_MAJOR  BITFIELD_RANGE(16, 4)
#define VERSION_MAJOR  BITFIELD_RANGE(12, 4)
#define VERSION_MINOR  BITFIELD_RANGE(4, 8)
#define VERSION_STATUS BITFIELD_RANGE(0, 4)

struct pan_model {
   /* GPU product ID */
   uint32_t gpu_prod_id;

   /* Mask to apply to the GPU ID to get a product ID. */
   uint32_t gpu_prod_id_mask;

   /* GPU variant. */
   uint32_t gpu_variant;

   /* Marketing name for the GPU, used as the GL_RENDERER */
   const char *name;

   /* Set of associated performance counters */
   const char *performance_counters;

   /* Minimum GPU revision required for anisotropic filtering. ~0 and 0
    * means "no revisions support anisotropy" and "all revisions support
    * anistropy" respectively -- so checking for anisotropy is simply
    * comparing the reivsion.
    */
   uint32_t min_rev_anisotropic;

   struct {
      /* Default tilebuffer size in bytes for the model. */
      uint32_t color_size;

      /* Default tilebuffer depth size in bytes for the model. */
      uint32_t z_size;
   } tilebuffer;

   /* Maximum number of pixels, texels, and FMA ops, per clock per shader
    * core, or 0 if it can't be determined for the given GPU. */
   struct {
      uint32_t pixel;
      uint32_t texel;
      uint32_t fma;
   } rates;

   struct {
      /* The GPU lacks the capability for hierarchical tiling, without
       * an "Advanced Tiling Unit", instead requiring a single bin
       * size for the entire framebuffer be selected by the driver
       */
      bool no_hierarchical_tiling;
      bool max_4x_msaa;
   } quirks;
};

const struct pan_model *pan_get_model(uint32_t gpu_id, uint32_t gpu_variant);

/* Returns the architecture version given a GPU ID, either from a table for
 * old-style Midgard versions or directly for new-style Bifrost/Valhall
 * versions */

static inline unsigned
pan_arch(unsigned gpu_id)
{
   switch (gpu_id >> 16) {
   case 0x600:
   case 0x620:
   case 0x720:
      return 4;
   case 0x750:
   case 0x820:
   case 0x830:
   case 0x860:
   case 0x880:
      return 5;
   default:
      return gpu_id >> 28;
   }
}

#endif
