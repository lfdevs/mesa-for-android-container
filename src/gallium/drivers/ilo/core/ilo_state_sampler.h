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

#ifndef ILO_STATE_SAMPLER_H
#define ILO_STATE_SAMPLER_H

#include "genhw/genhw.h"

#include "ilo_core.h"
#include "ilo_dev.h"

struct ilo_state_surface;

struct ilo_state_sampler_info {
   bool non_normalized;

   float lod_bias;
   float min_lod;
   float max_lod;

   enum gen_mip_filter mip_filter;
   enum gen_map_filter min_filter;
   enum gen_map_filter mag_filter;
   enum gen_aniso_ratio max_anisotropy;

   enum gen_texcoord_mode tcx_ctrl;
   enum gen_texcoord_mode tcy_ctrl;
   enum gen_texcoord_mode tcz_ctrl;

   enum gen_prefilter_op shadow_func;
};

struct ilo_state_sampler_border_info {
   union {
      float f[4];
      uint32_t ui[4];
   } rgba;

   bool is_integer;
};

struct ilo_state_sampler {
   uint32_t sampler[3];

   uint32_t filter_integer;
   uint32_t filter_3d;

   uint32_t addr_ctrl_1d;
   uint32_t addr_ctrl_2d_3d;
   uint32_t addr_ctrl_cube;

   bool non_normalized;
   bool base_to_surf_min_lod;
};

struct ilo_state_sampler_border {
   uint32_t color[12];
};

bool
ilo_state_sampler_init(struct ilo_state_sampler *sampler,
                       const struct ilo_dev *dev,
                       const struct ilo_state_sampler_info *info);

bool
ilo_state_sampler_init_disabled(struct ilo_state_sampler *sampler,
                                const struct ilo_dev *dev);

bool
ilo_state_sampler_set_surface(struct ilo_state_sampler *sampler,
                              const struct ilo_dev *dev,
                              const struct ilo_state_surface *surf);

bool
ilo_state_sampler_border_init(struct ilo_state_sampler_border *border,
                              const struct ilo_dev *dev,
                              const struct ilo_state_sampler_border_info *info);

#endif /* ILO_STATE_SAMPLER_H */
