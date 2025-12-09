/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD6_TEXTURE_H_
#define FD6_TEXTURE_H_

#include "pipe/p_context.h"

#include "freedreno_resource.h"
#include "freedreno_texture.h"

#include "fd6_context.h"
#include "fdl/fd6_format_table.h"


/* Border color layout is diff from a4xx/a5xx.. if it turns out to be
 * the same as a6xx then move this somewhere common ;-)
 *
 * Entry layout looks like (total size, 0x80 bytes):
 */

struct PACKED fd6_bcolor_entry {
   uint32_t fp32[4];
   uint16_t ui16[4];
   int16_t si16[4];
   uint16_t fp16[4];
   uint16_t rgb565;
   uint16_t rgb5a1;
   uint16_t rgba4;
   uint8_t __pad0[2];
   uint8_t ui8[4];
   int8_t si8[4];
   uint32_t rgb10a2;
   uint32_t z24;
   uint16_t srgb[4]; /* appears to duplicate fp16[], but clamped, used for srgb */
   uint8_t __pad1[56];
};

#define FD6_BORDER_COLOR_SIZE sizeof(struct fd6_bcolor_entry)
#define FD6_MAX_BORDER_COLORS 256

struct fd6_sampler_stateobj {
   struct pipe_sampler_state base;
   uint32_t descriptor[4];
   uint16_t seqno;
};

static inline struct fd6_sampler_stateobj *
fd6_sampler_stateobj(struct pipe_sampler_state *samp)
{
   return (struct fd6_sampler_stateobj *)samp;
}

struct fd6_pipe_sampler_view {
   struct pipe_sampler_view base;
   uint16_t seqno;

   /* TEX_CONST descriptor */
   uint32_t descriptor[FDL6_TEX_CONST_DWORDS];

   /* For detecting when a resource has transitioned from UBWC compressed
    * to uncompressed, which means the sampler state needs to be updated
    */
   uint16_t rsc_seqno;
};

static inline struct fd6_pipe_sampler_view *
fd6_pipe_sampler_view(struct pipe_sampler_view *pview)
{
   return (struct fd6_pipe_sampler_view *)pview;
}

template <chip CHIP>
void fd6_texture_init(struct pipe_context *pctx);
void fd6_texture_fini(struct pipe_context *pctx);

/*
 * Texture stateobj:
 *
 * The sampler and sampler-view state is mapped to a single hardware
 * stateobj which can be emit'd as a pointer in a CP_SET_DRAW_STATE
 * packet, to avoid the overhead of re-generating the entire cmdstream
 * when application toggles thru multiple different texture states.
 */

struct fd6_texture_key {
   uint16_t view_seqno[16];
   uint16_t samp_seqno[16];
   uint8_t type;
};

struct fd6_texture_state {
   struct fd6_texture_key key;
   struct fd_ringbuffer *stateobj;
   /**
    * Track the rsc seqno's associated with the texture views so
    * we know what to invalidate when a rsc is rebound when the
    * underlying bo changes.  (For example, demotion from UBWC.)
    */
   uint16_t view_rsc_seqno[16];
   bool invalidate;
};

template <chip CHIP>
struct fd6_texture_state *
fd6_texture_state(struct fd_context *ctx, mesa_shader_stage type) assert_dt;


static inline void
fd6_layout_tex2d_from_buf(struct fdl_layout *layout,
                          const struct fd_dev_info *info,
                          enum pipe_format format,
                          const struct pipe_tex2d_from_buf *tex2d_from_buf)
{
   unsigned block_size = util_format_get_blocksize(format);

   struct fdl_explicit_layout explicit_layout = {
      .offset = tex2d_from_buf->offset * block_size,
      .pitch = tex2d_from_buf->row_stride * block_size,
   };

   struct fdl_image_params params = {
      .format = format,
      .nr_samples = 1,
      .width0 = tex2d_from_buf->width,
      .height0 = tex2d_from_buf->height,
      .depth0 = 1,
      .mip_levels = 1,
      .array_size = 1,
   };

   *layout = (struct fdl_layout) {
      .ubwc = false,
      .tile_all = false,
      .tile_mode = TILE6_LINEAR,
   };

   ASSERTED bool ret = fdl6_layout_image(layout, info, &params, &explicit_layout);
   assert(ret);
}

#endif /* FD6_TEXTURE_H_ */
