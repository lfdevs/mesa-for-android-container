/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2015 LunarG, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#ifndef ILO_STATE_RASTER_H
#define ILO_STATE_RASTER_H

#include "genhw/genhw.h"

#include "ilo_core.h"
#include "ilo_dev.h"

enum ilo_state_raster_dirty_bits {
   ILO_STATE_RASTER_3DSTATE_CLIP                   = (1 << 0),
   ILO_STATE_RASTER_3DSTATE_SF                     = (1 << 1),
   ILO_STATE_RASTER_3DSTATE_RASTER                 = (1 << 2),
   ILO_STATE_RASTER_3DSTATE_MULTISAMPLE            = (1 << 3),
   ILO_STATE_RASTER_3DSTATE_SAMPLE_MASK            = (1 << 4),
   ILO_STATE_RASTER_3DSTATE_WM                     = (1 << 5),
   ILO_STATE_RASTER_3DSTATE_WM_HZ_OP               = (1 << 6),
   ILO_STATE_RASTER_3DSTATE_AA_LINE_PARAMETERS     = (1 << 7),
};

enum ilo_state_raster_earlyz_op {
   ILO_STATE_RASTER_EARLYZ_NORMAL,
   ILO_STATE_RASTER_EARLYZ_DEPTH_CLEAR,
   ILO_STATE_RASTER_EARLYZ_DEPTH_RESOLVE,
   ILO_STATE_RASTER_EARLYZ_HIZ_RESOLVE,
};

/**
 * VUE readback, VertexClipTest, ClipDetermination, and primitive output.
 */
struct ilo_state_raster_clip_info {
   bool clip_enable;
   /* CL_INVOCATION_COUNT and CL_PRIMITIVES_COUNT */
   bool stats_enable;

   uint8_t viewport_count;
   bool force_rtaindex_zero;

   /* these should be mutually exclusive */
   uint8_t user_cull_enables;
   uint8_t user_clip_enables;

   bool gb_test_enable;
   bool xy_test_enable;

   /* far/near must be enabled together prior to Gen9 */
   bool z_far_enable;
   bool z_near_enable;
   bool z_near_zero;
};

/**
 * Primitive assembly, viewport transformation, scissoring, MSAA, etc.
 */
struct ilo_state_raster_setup_info {
   bool cv_is_rectangle;

   bool first_vertex_provoking;
   bool viewport_transform;

   bool scissor_enable;

   /* MSAA enables for lines and non-lines */
   bool msaa_enable;
   bool line_msaa_enable;
};

/**
 * 3DOBJ_POINT rasterization rules.
 */
struct ilo_state_raster_point_info {
   /* ignored when msaa_enable is set */
   bool aa_enable;

   bool programmable_width;
};

/**
 * 3DOBJ_LINE rasterization rules.
 */
struct ilo_state_raster_line_info {
   /* ignored when line_msaa_enable is set */
   bool aa_enable;

   /* ignored when line_msaa_enable or aa_enable is set */
   bool stipple_enable;
   bool giq_enable;
   bool giq_last_pixel;
};

/**
 * 3DOBJ_TRIANGLE rasterization rules.
 */
struct ilo_state_raster_tri_info {
   enum gen_front_winding front_winding;
   enum gen_cull_mode cull_mode;
   enum gen_fill_mode fill_mode_front;
   enum gen_fill_mode fill_mode_back;

   enum gen_depth_format depth_offset_format;
   bool depth_offset_solid;
   bool depth_offset_wireframe;
   bool depth_offset_point;

   bool poly_stipple_enable;
};

/**
 * Scan conversion.
 */
struct ilo_state_raster_scan_info {
   /* PS_DEPTH_COUNT and PS_INVOCATION_COUNT */
   bool stats_enable;

   uint8_t sample_count;

   /* pixel location for non-MSAA or 1x-MSAA */
   enum gen_pixel_location pixloc;

   uint32_t sample_mask;

   /* interpolations */
   enum gen_zw_interp zw_interp;
   uint8_t barycentric_interps;

   /* Gen7+ only */
   enum gen_edsc_mode earlyz_control;
   enum ilo_state_raster_earlyz_op earlyz_op;
   bool earlyz_stencil_clear;
};

/**
 * Raster parameters.
 */
struct ilo_state_raster_params_info {
   bool any_integer_rt;
   bool hiz_enable;

   float point_width;
   float line_width;

   /* const term will be scaled by 'r' */
   float depth_offset_const;
   float depth_offset_scale;
   float depth_offset_clamp;
};

struct ilo_state_raster_info {
   struct ilo_state_raster_clip_info clip;
   struct ilo_state_raster_setup_info setup;
   struct ilo_state_raster_point_info point;
   struct ilo_state_raster_line_info line;
   struct ilo_state_raster_tri_info tri;
   struct ilo_state_raster_scan_info scan;

   struct ilo_state_raster_params_info params;
};

struct ilo_state_raster {
   uint32_t clip[3];
   uint32_t sf[3];
   uint32_t raster[4];
   uint32_t sample[2];
   uint32_t wm[3];

   bool line_aa_enable;
   bool line_giq_enable;
};

struct ilo_state_raster_delta {
   uint32_t dirty;
};

struct ilo_state_sample_pattern_offset_info {
   /* in U0.4 */
   uint8_t x;
   uint8_t y;
};

struct ilo_state_sample_pattern_info {
   struct ilo_state_sample_pattern_offset_info pattern_1x[1];
   struct ilo_state_sample_pattern_offset_info pattern_2x[2];
   struct ilo_state_sample_pattern_offset_info pattern_4x[4];
   struct ilo_state_sample_pattern_offset_info pattern_8x[8];
   struct ilo_state_sample_pattern_offset_info pattern_16x[16];
};

struct ilo_state_sample_pattern {
   uint8_t pattern_1x[1];
   uint8_t pattern_2x[2];
   uint8_t pattern_4x[4];
   uint8_t pattern_8x[8];
   uint8_t pattern_16x[16];
};

struct ilo_state_line_stipple_info {
   uint16_t pattern;
   uint16_t repeat_count;
};

struct ilo_state_line_stipple {
   uint32_t stipple[2];
};

struct ilo_state_poly_stipple_info {
   uint32_t pattern[32];
};

struct ilo_state_poly_stipple {
   uint32_t stipple[32];
};

bool
ilo_state_raster_init(struct ilo_state_raster *rs,
                      const struct ilo_dev *dev,
                      const struct ilo_state_raster_info *info);

bool
ilo_state_raster_init_for_rectlist(struct ilo_state_raster *rs,
                                   const struct ilo_dev *dev,
                                   uint8_t sample_count,
                                   enum ilo_state_raster_earlyz_op earlyz_op,
                                   bool earlyz_stencil_clear);

bool
ilo_state_raster_set_info(struct ilo_state_raster *rs,
                          const struct ilo_dev *dev,
                          const struct ilo_state_raster_info *info);

bool
ilo_state_raster_set_params(struct ilo_state_raster *rs,
                            const struct ilo_dev *dev,
                            const struct ilo_state_raster_params_info *params);

void
ilo_state_raster_full_delta(const struct ilo_state_raster *rs,
                            const struct ilo_dev *dev,
                            struct ilo_state_raster_delta *delta);

void
ilo_state_raster_get_delta(const struct ilo_state_raster *rs,
                           const struct ilo_dev *dev,
                           const struct ilo_state_raster *old,
                           struct ilo_state_raster_delta *delta);

bool
ilo_state_sample_pattern_init(struct ilo_state_sample_pattern *pattern,
                              const struct ilo_dev *dev,
                              const struct ilo_state_sample_pattern_info *info);

bool
ilo_state_sample_pattern_init_default(struct ilo_state_sample_pattern *pattern,
                                      const struct ilo_dev *dev);

const uint8_t *
ilo_state_sample_pattern_get_packed_offsets(const struct ilo_state_sample_pattern *pattern,
                                            const struct ilo_dev *dev,
                                            uint8_t sample_count);

void
ilo_state_sample_pattern_get_offset(const struct ilo_state_sample_pattern *pattern,
                                    const struct ilo_dev *dev,
                                    uint8_t sample_count, uint8_t sample_index,
                                    uint8_t *x, uint8_t *y);
bool
ilo_state_line_stipple_set_info(struct ilo_state_line_stipple *stipple,
                                const struct ilo_dev *dev,
                                const struct ilo_state_line_stipple_info *info);

bool
ilo_state_poly_stipple_set_info(struct ilo_state_poly_stipple *stipple,
                                const struct ilo_dev *dev,
                                const struct ilo_state_poly_stipple_info *info);

#endif /* ILO_STATE_RASTER_H */
