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

#ifndef ILO_STATE_SURFACE_H
#define ILO_STATE_SURFACE_H

#include "genhw/genhw.h"

#include "ilo_core.h"
#include "ilo_dev.h"

enum ilo_state_surface_access {
   ILO_STATE_SURFACE_ACCESS_SAMPLER,      /* sampling engine surfaces */
   ILO_STATE_SURFACE_ACCESS_DP_RENDER,    /* render target surfaces */
   ILO_STATE_SURFACE_ACCESS_DP_TYPED,     /* typed surfaces */
   ILO_STATE_SURFACE_ACCESS_DP_UNTYPED,   /* untyped surfaces */
   ILO_STATE_SURFACE_ACCESS_DP_DATA,
   ILO_STATE_SURFACE_ACCESS_DP_SVB,
};

struct ilo_vma;
struct ilo_image;

struct ilo_state_surface_buffer_info {
   const struct ilo_vma *vma;
   uint32_t offset;
   uint32_t size;

   enum ilo_state_surface_access access;

   /* format_size may be less than, equal to, or greater than struct_size */
   enum gen_surface_format format;
   uint8_t format_size;

   bool readonly;
   uint16_t struct_size;
};

struct ilo_state_surface_image_info {
   const struct ilo_image *img;
   uint8_t level_base;
   uint8_t level_count;
   uint16_t slice_base;
   uint16_t slice_count;

   const struct ilo_vma *vma;
   const struct ilo_vma *aux_vma;

   enum ilo_state_surface_access access;

   enum gen_surface_type type;

   enum gen_surface_format format;
   bool is_integer;

   bool readonly;
   bool is_array;
};

struct ilo_state_surface {
   uint32_t surface[13];

   const struct ilo_vma *vma;
   const struct ilo_vma *aux_vma;

   enum gen_surface_type type;
   uint8_t min_lod;
   uint8_t mip_count;
   bool is_integer;

   bool readonly;
   bool scanout;
};

bool
ilo_state_surface_valid_format(const struct ilo_dev *dev,
                               enum ilo_state_surface_access access,
                               enum gen_surface_format format);

uint32_t
ilo_state_surface_buffer_size(const struct ilo_dev *dev,
                              enum ilo_state_surface_access access,
                              uint32_t size, uint32_t *alignment);

bool
ilo_state_surface_init_for_null(struct ilo_state_surface *surf,
                                const struct ilo_dev *dev);

bool
ilo_state_surface_init_for_buffer(struct ilo_state_surface *surf,
                                  const struct ilo_dev *dev,
                                  const struct ilo_state_surface_buffer_info *info);

bool
ilo_state_surface_init_for_image(struct ilo_state_surface *surf,
                                 const struct ilo_dev *dev,
                                 const struct ilo_state_surface_image_info *info);

bool
ilo_state_surface_set_scs(struct ilo_state_surface *surf,
                          const struct ilo_dev *dev,
                          enum gen_surface_scs rgba[4]);

#endif /* ILO_STATE_SURFACE_H */
