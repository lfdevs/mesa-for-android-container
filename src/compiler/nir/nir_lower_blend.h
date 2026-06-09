/*
 * Copyright © 2019 Alyssa Rosenzweig
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#ifndef NIR_BLEND_H
#define NIR_BLEND_H

#include "compiler/nir/nir_defines.h"
#include "util/blend.h"
#include "util/format/u_formats.h"

/* These structs encapsulates the blend state such that it can be lowered
 * cleanly
 */

typedef struct {
   enum pipe_blend_func func;
   enum pipe_blendfactor src_factor;
   enum pipe_blendfactor dst_factor;
} nir_lower_blend_channel;

typedef struct {
   enum pipe_format format;

   nir_lower_blend_channel rgb;
   nir_lower_blend_channel alpha;

   /* 4-bit colormask. 0x0 for none, 0xF for RGBA, 0x1 for R */
   unsigned colormask:4;

   unsigned advanced_blend:1;
   enum pipe_advanced_blend_mode blend_mode;
   bool src_premultiplied;
   bool dst_premultiplied;
   enum pipe_blend_overlap_mode overlap;
} nir_lower_blend_rt;

typedef struct {
   nir_lower_blend_rt rt[8];

   bool logicop_enable;
   enum pipe_logicop logicop_func;

   /* If set, will use load_blend_const_color_{r,g,b,a}_float instead of
    * load_blend_const_color_rgba */
   bool scalar_blend_const;
} nir_lower_blend_options;

#define nir_color_logicop(...) MESA_CALL_WITH_LTR_ARGS(_nir_color_logicop, __VA_ARGS__)
nir_def *
_nir_color_logicop(nir_builder *b, nir_def *src, nir_def *dst,
                  enum pipe_logicop func, enum pipe_format format);

#define nir_color_blend(...) MESA_CALL_WITH_LTR_ARGS(_nir_color_blend, __VA_ARGS__)
nir_def *
_nir_color_blend(nir_builder *b, nir_def *src0, nir_def *src1, nir_def *dst,
                const nir_lower_blend_rt *rt, bool scalar_blend_const);

#define nir_color_mask(...) MESA_CALL_WITH_LTR_ARGS(_nir_color_mask, __VA_ARGS__)
nir_def *
_nir_color_mask(nir_builder *b, nir_def *src, nir_def *dst, unsigned mask);

bool nir_lower_blend(nir_shader *shader,
                     const nir_lower_blend_options *options);

#endif
