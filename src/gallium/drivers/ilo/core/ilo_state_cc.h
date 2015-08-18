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

#ifndef ILO_STATE_CC_H
#define ILO_STATE_CC_H

#include "genhw/genhw.h"

#include "ilo_core.h"
#include "ilo_dev.h"

/*
 * From the Sandy Bridge PRM, volume 2 part 1, page 38:
 *
 *     "Render Target Index. Specifies the render target index that will be
 *      used to select blend state from BLEND_STATE.
 *      Format = U3"
 */
#define ILO_STATE_CC_BLEND_MAX_RT_COUNT 8

enum ilo_state_cc_dirty_bits {
   ILO_STATE_CC_3DSTATE_WM_DEPTH_STENCIL           = (1 << 0),
   ILO_STATE_CC_3DSTATE_PS_BLEND                   = (1 << 1),
   ILO_STATE_CC_DEPTH_STENCIL_STATE                = (1 << 2),
   ILO_STATE_CC_BLEND_STATE                        = (1 << 3),
   ILO_STATE_CC_COLOR_CALC_STATE                   = (1 << 4),
};

/**
 * AlphaCoverage and AlphaTest.
 */
struct ilo_state_cc_alpha_info {
   bool cv_sample_count_one;
   bool cv_float_source0_alpha;

   bool alpha_to_coverage;
   bool alpha_to_one;

   bool test_enable;
   enum gen_compare_function test_func;
};

struct ilo_state_cc_stencil_op_info {
   enum gen_compare_function test_func;
   enum gen_stencil_op fail_op;
   enum gen_stencil_op zfail_op;
   enum gen_stencil_op zpass_op;
};

/**
 * StencilTest.
 */
struct ilo_state_cc_stencil_info {
   bool cv_has_buffer;

   bool test_enable;
   bool twosided_enable;

   struct ilo_state_cc_stencil_op_info front;
   struct ilo_state_cc_stencil_op_info back;
};

/**
 * DepthTest.
 */
struct ilo_state_cc_depth_info {
   bool cv_has_buffer;

   bool test_enable;
   /* independent from test_enable */
   bool write_enable;

   enum gen_compare_function test_func;
};

struct ilo_state_cc_blend_rt_info {
   bool cv_has_buffer;
   bool cv_is_unorm;
   bool cv_is_integer;

   uint8_t argb_write_disables;

   bool logicop_enable;
   enum gen_logic_op logicop_func;

   bool blend_enable;
   bool force_dst_alpha_one;
   enum gen_blend_factor rgb_src;
   enum gen_blend_factor rgb_dst;
   enum gen_blend_function rgb_func;
   enum gen_blend_factor a_src;
   enum gen_blend_factor a_dst;
   enum gen_blend_function a_func;
};

/**
 * ColorBufferBlending, Dithering, and LogicOps.
 */
struct ilo_state_cc_blend_info {
   const struct ilo_state_cc_blend_rt_info *rt;
   uint8_t rt_count;

   bool dither_enable;
};

struct ilo_state_cc_stencil_params_info {
   uint8_t test_ref;
   uint8_t test_mask;
   uint8_t write_mask;
};

/**
 * CC parameters.
 */
struct ilo_state_cc_params_info {
   float alpha_ref;

   struct ilo_state_cc_stencil_params_info stencil_front;
   struct ilo_state_cc_stencil_params_info stencil_back;

   float blend_rgba[4];
};

/**
 * Pixel processing.
 */
struct ilo_state_cc_info {
   struct ilo_state_cc_alpha_info alpha;
   struct ilo_state_cc_stencil_info stencil;
   struct ilo_state_cc_depth_info depth;
   struct ilo_state_cc_blend_info blend;

   struct ilo_state_cc_params_info params;
};

struct ilo_state_cc {
   uint32_t ds[3];

   uint8_t blend_state_count;
   uint32_t blend[1 + 1 + 2 * ILO_STATE_CC_BLEND_MAX_RT_COUNT];

   uint32_t cc[6];
};

struct ilo_state_cc_delta {
   uint32_t dirty;
};

bool
ilo_state_cc_init(struct ilo_state_cc *cc,
                  const struct ilo_dev *dev,
                  const struct ilo_state_cc_info *info);

bool
ilo_state_cc_set_info(struct ilo_state_cc *cc,
                      const struct ilo_dev *dev,
                      const struct ilo_state_cc_info *info);

bool
ilo_state_cc_set_params(struct ilo_state_cc *cc,
                        const struct ilo_dev *dev,
                        const struct ilo_state_cc_params_info *params);

void
ilo_state_cc_full_delta(const struct ilo_state_cc *cc,
                        const struct ilo_dev *dev,
                        struct ilo_state_cc_delta *delta);

void
ilo_state_cc_get_delta(const struct ilo_state_cc *cc,
                       const struct ilo_dev *dev,
                       const struct ilo_state_cc *old,
                       struct ilo_state_cc_delta *delta);

#endif /* ILO_STATE_CC_H */
