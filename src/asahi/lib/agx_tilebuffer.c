/*
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "agx_tilebuffer.h"
#include <assert.h>
#include "compiler/agx_internal_formats.h"
#include "util/format/u_format.h"
#include "agx_formats.h"
#include "agx_usc.h"

/* Maximum number of bytes per tile on G13G. This may change in future versions
 * of the architecture.
 */
#define MAX_BYTES_PER_TILE (32768 - 1)

/* Select the largest tile size that fits */
static struct agx_tile_size
agx_select_tile_size(unsigned bytes_per_pixel)
{
   /* clang-format off */
   struct agx_tile_size sizes[] = {
      { 32, 32 },
      { 32, 16 },
      { 16, 16 }
   };
   /* clang-format on */

   for (unsigned i = 0; i < ARRAY_SIZE(sizes); ++i) {
      struct agx_tile_size size = sizes[i];

      if ((bytes_per_pixel * size.width * size.height) <= MAX_BYTES_PER_TILE)
         return size;
   }

   unreachable("No supported tile size meets the bytes per pixel requirement");
}

struct agx_tilebuffer_layout
agx_build_tilebuffer_layout(enum pipe_format *formats, uint8_t nr_cbufs,
                            uint8_t nr_samples)
{
   struct agx_tilebuffer_layout tib = {.nr_samples = nr_samples};

   uint32_t offset_B = 0;

   for (unsigned rt = 0; rt < nr_cbufs; ++rt) {
      tib.logical_format[rt] = formats[rt];

      /* Require natural alignment for tilebuffer allocations. This could be
       * optimized, but this shouldn't be a problem in practice.
       */
      enum pipe_format physical_fmt = agx_tilebuffer_physical_format(&tib, rt);
      unsigned align_B = util_format_get_blocksize(physical_fmt);
      offset_B = ALIGN_POT(offset_B, align_B);

      tib.offset_B[rt] = offset_B;

      unsigned nr = util_format_get_nr_components(physical_fmt) == 1
                       ? util_format_get_nr_components(formats[rt])
                       : 1;

      unsigned size_B = align_B * nr;
      offset_B += size_B;
   }

   assert(offset_B <= 64 && "TIB strides must be <= 64");
   tib.sample_size_B = ALIGN_POT(offset_B, 8);

   tib.tile_size = agx_select_tile_size(tib.sample_size_B * nr_samples);
   return tib;
}

enum pipe_format
agx_tilebuffer_physical_format(struct agx_tilebuffer_layout *tib, unsigned rt)
{
   return agx_pixel_format[tib->logical_format[rt]].internal;
}

bool
agx_tilebuffer_supports_mask(struct agx_tilebuffer_layout *tib, unsigned rt)
{
   enum pipe_format fmt = agx_tilebuffer_physical_format(tib, rt);
   return agx_internal_format_supports_mask((enum agx_internal_formats)fmt);
}

static unsigned
agx_shared_layout_from_tile_size(struct agx_tile_size t)
{
   if (t.width == 32 && t.height == 32)
      return AGX_SHARED_LAYOUT_32X32;
   else if (t.width == 32 && t.height == 16)
      return AGX_SHARED_LAYOUT_32X16;
   else if (t.width == 16 && t.height == 16)
      return AGX_SHARED_LAYOUT_16X16;
   else
      unreachable("Invalid tile size");
}

uint32_t
agx_tilebuffer_total_size(struct agx_tilebuffer_layout *tib)
{
   return tib->sample_size_B * tib->nr_samples * tib->tile_size.width *
          tib->tile_size.height;
}

void
agx_usc_tilebuffer(struct agx_usc_builder *b, struct agx_tilebuffer_layout *tib)
{
   agx_usc_pack(b, SHARED, cfg) {
      cfg.uses_shared_memory = true;
      cfg.layout = agx_shared_layout_from_tile_size(tib->tile_size);
      cfg.sample_stride_in_8_bytes = tib->sample_size_B / 8;
      cfg.sample_count = tib->nr_samples;
      cfg.bytes_per_threadgroup = agx_tilebuffer_total_size(tib);
   }
}
