/*
 * Copyright © 2014-2017 Broadcom
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
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
 */

/* This file generates the per-v3d-version function prototypes.  It must only
 * be included from vc5_context.h.
 */

struct v3d_hw;
struct vc5_format;

void v3dX(emit_state)(struct pipe_context *pctx);
void v3dX(emit_rcl)(struct vc5_job *job);
void v3dX(draw_init)(struct pipe_context *pctx);
void v3dX(state_init)(struct pipe_context *pctx);

void v3dX(bcl_epilogue)(struct vc5_context *vc5, struct vc5_job *job);

void v3dX(simulator_init_regs)(struct v3d_hw *v3d);
int v3dX(simulator_get_param_ioctl)(struct v3d_hw *v3d,
                                    struct drm_vc5_get_param *args);
void v3dX(simulator_flush)(struct v3d_hw *v3d, struct drm_vc5_submit_cl *submit,
                           uint32_t gmp_ofs);
const struct vc5_format *v3dX(get_format_desc)(enum pipe_format f);
void v3dX(get_internal_type_bpp_for_output_format)(uint32_t format,
                                                   uint32_t *type,
                                                   uint32_t *bpp);
