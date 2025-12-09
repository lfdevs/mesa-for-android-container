/*
 * Copyright (C) 2008 VMware, Inc.
 * Copyright (C) 2014 Broadcom
 * Copyright (C) 2018-2019 Alyssa Rosenzweig
 * Copyright (C) 2019-2020 Collabora, Ltd.
 * Copyright (C) 2025 Arm Ltd.
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
 */

#ifndef __PAN_TEXTURE_H
#define __PAN_TEXTURE_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include "pan_image.h"

struct pan_ptr;
struct mali_texture_packed;
struct mali_buffer_packed;
struct pan_buffer_view;

#if PAN_ARCH >= 7
void GENX(pan_texture_swizzle_replicate_x)(struct pan_image_view *iview);
#endif

#if PAN_ARCH == 7
void GENX(pan_texture_afbc_reswizzle)(struct pan_image_view *iview);
#endif

unsigned
GENX(pan_texture_estimate_payload_size)(const struct pan_image_view *iview);

void GENX(pan_sampled_texture_emit)(const struct pan_image_view *iview,
                                    struct mali_texture_packed *out,
                                    const struct pan_ptr *payload);

void GENX(pan_tex_emit_linear_payload_entry)(const struct pan_image_view *iview,
                                             unsigned mip_level,
                                             unsigned layer_or_z_slice,
                                             unsigned sample, void **payload);

void GENX(pan_tex_emit_u_tiled_payload_entry)(
   const struct pan_image_view *iview, unsigned mip_level,
   unsigned layer_or_z_slice, unsigned sample, void **payload);

void
GENX(pan_tex_emit_afbc_payload_entry)(const struct pan_image_view *iview,
                                      unsigned mip_level, unsigned layer_or_z_slice,
                                      unsigned sample, void **payload);

#if PAN_ARCH >= 9
void GENX(pan_storage_texture_emit)(const struct pan_image_view *iview,
                                    struct mali_texture_packed *out,
                                    const struct pan_ptr *payload);
#endif

void GENX(pan_tex_emit_linear_payload_entry)(const struct pan_image_view *iview,
                                             unsigned mip_level,
                                             unsigned layer_or_z_slice,
                                             unsigned sample, void **payload);
void GENX(pan_tex_emit_u_tiled_payload_entry)(
   const struct pan_image_view *iview, unsigned mip_level,
   unsigned layer_or_z_slice, unsigned sample, void **payload);
void GENX(pan_tex_emit_afbc_payload_entry)(const struct pan_image_view *iview,
                                           unsigned mip_level,
                                           unsigned layer_or_z_slice,
                                           unsigned sample, void **payload);

#if PAN_ARCH >= 10
void GENX(pan_tex_emit_afrc_payload_entry)(
      const struct pan_image_view *iview, unsigned mip_level,
      unsigned layer_or_z_slice, unsigned sample, void **payload);
#endif

#if PAN_ARCH >= 9
void GENX(pan_buffer_texture_emit)(const struct pan_buffer_view *bview,
                                   struct mali_buffer_packed *out);
#else
void GENX(pan_buffer_texture_emit)(const struct pan_buffer_view *bview,
                                   struct mali_texture_packed *out,
                                   const struct pan_ptr *payload);
#endif

#endif
