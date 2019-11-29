/*
 * Copyright (c) 2017 Etnaviv Project
 * Copyright (C) 2017 Zodiac Inflight Innovations
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Wladimir J. van der Laan <laanwj@gmail.com>
 */

#ifndef H_ETNAVIV_TEXTURE_DESC
#define H_ETNAVIV_TEXTURE_DESC

#include "drm/etnaviv_drmif.h"

#include "etnaviv_texture.h"

#include "pipe/p_context.h"
#include "pipe/p_state.h"

#include "hw/state_3d.xml.h"

struct etna_context;

struct etna_sampler_state_desc {
   struct pipe_sampler_state base;
   uint32_t SAMP_CTRL0;
   uint32_t SAMP_CTRL1;
   uint32_t SAMP_LOD_MINMAX;
   uint32_t SAMP_LOD_BIAS;
};

static inline struct etna_sampler_state_desc *
etna_sampler_state_desc(struct pipe_sampler_state *samp)
{
   return (struct etna_sampler_state_desc *)samp;
}

struct etna_sampler_view_desc {
   struct pipe_sampler_view base;
   /* format-dependent merged with sampler state */
   uint32_t SAMP_CTRL0;
   uint32_t SAMP_CTRL1;

   struct etna_bo *bo;
   struct etna_reloc DESC_ADDR;
   struct etna_sampler_ts ts;
};

static inline struct etna_sampler_view_desc *
etna_sampler_view_desc(struct pipe_sampler_view *view)
{
   return (struct etna_sampler_view_desc *)view;
}

/* Initialize context for descriptor-based texture views and descriptors */
void
etna_texture_desc_init(struct pipe_context *pctx);

#endif

