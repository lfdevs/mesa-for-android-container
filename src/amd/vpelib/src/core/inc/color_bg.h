/* Copyright 2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#pragma once

#include "color.h"

enum vpe_status vpe_bg_color_outside_cs_gamut(
    const struct vpe_priv *vpe_priv, struct vpe_color *bg_color);

bool vpe_bg_csc(struct vpe_color *bg_color, enum color_space cs);

bool vpe_is_global_bg_blend_applied(struct stream_ctx *stream_ctx);

bool vpe_is_limited_cs(enum color_space cs);

bool vpe_is_yuv_cs(enum color_space cs);

void vpe_bg_degam(struct transfer_func *output_tf, struct vpe_color *bg_color);

void vpe_bg_inverse_gamut_remap(
    enum color_space output_cs, struct transfer_func *output_tf, struct vpe_color *bg_color);

void vpe_bg_format_and_limited_conversion(enum color_space output_cs,
    enum vpe_surface_pixel_format pixel_format, struct vpe_color *bg_color);

void vpe_bg_color_space_conversion(enum color_space output_cs, struct vpe_color *bg_color);

void vpe_inverse_output_csc(enum color_space output_cs, struct vpe_color *bg_color);
