/*
 * Copyright (C) 2008 VMware, Inc.
 * Copyright (C) 2014 Broadcom
 * Copyright (C) 2018-2019 Alyssa Rosenzweig
 * Copyright (C) 2019-2020 Collabora, Ltd.
 * Copyright (C) 2025 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef __PAN_TEXTURE_H
#define __PAN_TEXTURE_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include "pan_image.h"

struct pan_ptr;
struct mali_texture_packed;

#if PAN_ARCH >= 7
void GENX(pan_texture_swizzle_replicate_x)(struct pan_image_view *iview);
#endif

#if PAN_ARCH == 7
void GENX(pan_texture_afbc_reswizzle)(struct pan_image_view *iview);
#endif

unsigned
GENX(pan_texture_estimate_payload_size)(const struct pan_image_view *iview);

unsigned
GENX(pan_texture_get_payload_alignment)(const struct pan_image_view *iview);

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
void GENX(pan_tex_emit_interleaved_64k_payload_entry)(
      const struct pan_image_view *iview, unsigned mip_level,
      unsigned layer_or_z_slice, unsigned sample, void **payload);

void GENX(pan_tex_emit_afrc_payload_entry)(
      const struct pan_image_view *iview, unsigned mip_level,
      unsigned layer_or_z_slice, unsigned sample, void **payload);
#endif

#endif
