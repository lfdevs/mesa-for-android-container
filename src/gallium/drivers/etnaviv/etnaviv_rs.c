/*
 * Copyright (c) 2012-2015 Etnaviv Project
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

#include "etnaviv_rs.h"
#include "etnaviv_screen.h"
#include "etnaviv_tiling.h"
#include "etnaviv_util.h"

#include "hw/common.xml.h"
#include "hw/state.xml.h"
#include "hw/state_3d.xml.h"

#include <assert.h>

void
etna_compile_rs_state(struct etna_context *ctx, struct compiled_rs_state *cs,
                      const struct rs_state *rs)
{
   memset(cs, 0, sizeof(*cs));

   /* TILED and SUPERTILED layout have their strides multiplied with 4 in RS */
   unsigned source_stride_shift = COND(rs->source_tiling != ETNA_LAYOUT_LINEAR, 2);
   unsigned dest_stride_shift = COND(rs->dest_tiling != ETNA_LAYOUT_LINEAR, 2);

   /* tiling == ETNA_LAYOUT_MULTI_TILED or ETNA_LAYOUT_MULTI_SUPERTILED? */
   int source_multi = COND(rs->source_tiling & ETNA_LAYOUT_BIT_MULTI, 1);
   int dest_multi = COND(rs->dest_tiling & ETNA_LAYOUT_BIT_MULTI, 1);

   /* Vivante RS needs widths to be a multiple of 16 or bad things
    * happen, such as scribbing over memory, or the GPU hanging,
    * even for non-tiled formats.  As this is serious, use abort().
    */
   if (rs->width & ETNA_RS_WIDTH_MASK)
      abort();

   /* TODO could just pre-generate command buffer, would simply submit to one memcpy */
   cs->RS_CONFIG = VIVS_RS_CONFIG_SOURCE_FORMAT(rs->source_format) |
                   COND(rs->downsample_x, VIVS_RS_CONFIG_DOWNSAMPLE_X) |
                   COND(rs->downsample_y, VIVS_RS_CONFIG_DOWNSAMPLE_Y) |
                   COND(rs->source_tiling & 1, VIVS_RS_CONFIG_SOURCE_TILED) |
                   VIVS_RS_CONFIG_DEST_FORMAT(rs->dest_format) |
                   COND(rs->dest_tiling & 1, VIVS_RS_CONFIG_DEST_TILED) |
                   COND(rs->swap_rb, VIVS_RS_CONFIG_SWAP_RB) |
                   COND(rs->flip, VIVS_RS_CONFIG_FLIP);

   cs->RS_SOURCE_STRIDE = (rs->source_stride << source_stride_shift) |
                          COND(rs->source_tiling & 2, VIVS_RS_SOURCE_STRIDE_TILING) |
                          COND(source_multi, VIVS_RS_SOURCE_STRIDE_MULTI);

   cs->source[0].bo = rs->source;
   cs->source[0].offset = rs->source_offset;
   cs->source[0].flags = ETNA_RELOC_READ;

   cs->dest[0].bo = rs->dest;
   cs->dest[0].offset = rs->dest_offset;
   cs->dest[0].flags = ETNA_RELOC_WRITE;

   cs->RS_DEST_STRIDE = (rs->dest_stride << dest_stride_shift) |
                        COND(rs->dest_tiling & 2, VIVS_RS_DEST_STRIDE_TILING) |
                        COND(dest_multi, VIVS_RS_DEST_STRIDE_MULTI);

   if (ctx->specs.pixel_pipes == 1) {
      cs->RS_WINDOW_SIZE = VIVS_RS_WINDOW_SIZE_WIDTH(rs->width) |
                           VIVS_RS_WINDOW_SIZE_HEIGHT(rs->height);
   } else if (ctx->specs.pixel_pipes == 2) {
      assert((rs->height & 7) == 0); /* GPU hangs happen if height not 8-aligned */

      if (source_multi) {
         cs->source[1].bo = rs->source;
         cs->source[1].offset = rs->source_offset + rs->source_stride * rs->source_padded_height / 2;
         cs->source[1].flags = ETNA_RELOC_READ;
      }

      if (dest_multi) {
         cs->dest[1].bo = rs->dest;
         cs->dest[1].offset = rs->dest_offset + rs->dest_stride * rs->dest_padded_height / 2;
         cs->dest[1].flags = ETNA_RELOC_WRITE;
      }

      cs->RS_WINDOW_SIZE = VIVS_RS_WINDOW_SIZE_WIDTH(rs->width) |
                           VIVS_RS_WINDOW_SIZE_HEIGHT(rs->height / 2);
   } else {
      abort();
   }

   cs->RS_PIPE_OFFSET[0] = VIVS_RS_PIPE_OFFSET_X(0) | VIVS_RS_PIPE_OFFSET_Y(0);
   cs->RS_PIPE_OFFSET[1] = VIVS_RS_PIPE_OFFSET_X(0) | VIVS_RS_PIPE_OFFSET_Y(rs->height / 2);
   cs->RS_DITHER[0] = rs->dither[0];
   cs->RS_DITHER[1] = rs->dither[1];
   cs->RS_CLEAR_CONTROL = VIVS_RS_CLEAR_CONTROL_BITS(rs->clear_bits) | rs->clear_mode;
   cs->RS_FILL_VALUE[0] = rs->clear_value[0];
   cs->RS_FILL_VALUE[1] = rs->clear_value[1];
   cs->RS_FILL_VALUE[2] = rs->clear_value[2];
   cs->RS_FILL_VALUE[3] = rs->clear_value[3];
   cs->RS_EXTRA_CONFIG = VIVS_RS_EXTRA_CONFIG_AA(rs->aa) |
                         VIVS_RS_EXTRA_CONFIG_ENDIAN(rs->endian_mode);
}

void
etna_modify_rs_clearbits(struct compiled_rs_state *cs, uint32_t clear_bits)
{
   cs->RS_CLEAR_CONTROL &= ~VIVS_RS_CLEAR_CONTROL_BITS__MASK;
   cs->RS_CLEAR_CONTROL |= VIVS_RS_CLEAR_CONTROL_BITS(clear_bits);
}
