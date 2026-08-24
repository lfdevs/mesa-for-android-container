/*
 * Copyright (C) 2026 Amazon.com, Inc. or its affiliates
 * SPDX-License-Identifier: MIT
 */

#ifndef __PAN_BLITTER_H__
#define __PAN_BLITTER_H__

#include "pan_context.h"

struct blitter_context *panfrost_blitter_create(struct pipe_context *pipe);

/* Callers should ensure that all AFBC/AFRC resources that will be used in the
 * blit operation are legalized before calling blitter operations, otherwise
 * we may trigger a recursive blit */
void panfrost_blitter_blit_legalized(struct pipe_context *pipe,
                                     const struct pipe_blit_info *info);

void panfrost_blitter_blit(struct pipe_context *pipe,
                           const struct pipe_blit_info *info);

void panfrost_blitter_clear(struct pipe_context *pipe, unsigned buffers,
                            uint32_t color_clear_mask,
                            uint8_t stencil_clear_mask,
                            const struct pipe_scissor_state *scissor_state,
                            const union pipe_color_union *color, double depth,
                            unsigned stencil);

void panfrost_blitter_clear_depth_stencil(struct pipe_context *pipe,
                                          struct pipe_surface *dst,
                                          unsigned clear_flags, double depth,
                                          unsigned stencil, unsigned dstx,
                                          unsigned dsty, unsigned width,
                                          unsigned height,
                                          bool render_condition_enabled);

void panfrost_blitter_clear_render_target(struct pipe_context *pipe,
                                          struct pipe_surface *dst,
                                          const union pipe_color_union *color,
                                          unsigned dstx, unsigned dsty,
                                          unsigned width, unsigned height,
                                          bool render_condition_enabled);

bool panfrost_blitter_generate_mipmap(struct pipe_context *pipe,
                                      struct pipe_resource *tex,
                                      enum pipe_format format,
                                      unsigned base_level,
                                      unsigned last_level,
                                      unsigned first_layer,
                                      unsigned last_layer);

void panfrost_blitter_resource_copy_region(struct pipe_context *pipe,
                                           struct pipe_resource *dst,
                                           unsigned dst_level, unsigned dst_x,
                                           unsigned dst_y, unsigned dst_z,
                                           struct pipe_resource *src,
                                           unsigned src_level,
                                           const struct pipe_box *src_box);

#endif
