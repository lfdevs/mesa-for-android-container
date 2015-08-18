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

#ifndef ILO_STATE_ZS_H
#define ILO_STATE_ZS_H

#include "genhw/genhw.h"

#include "ilo_core.h"
#include "ilo_dev.h"

struct ilo_vma;
struct ilo_image;

struct ilo_state_zs_info {
   /* both optional */
   const struct ilo_image *z_img;
   const struct ilo_image *s_img;
   uint8_t level;
   uint16_t slice_base;
   uint16_t slice_count;

   const struct ilo_vma *z_vma;
   const struct ilo_vma *s_vma;
   const struct ilo_vma *hiz_vma;

   enum gen_surface_type type;
   enum gen_depth_format format;

   /* ignored prior to Gen7 */
   bool z_readonly;
   bool s_readonly;
};

struct ilo_state_zs {
   uint32_t depth[5];
   uint32_t stencil[3];
   uint32_t hiz[3];

   const struct ilo_vma *z_vma;
   const struct ilo_vma *s_vma;
   const struct ilo_vma *hiz_vma;

   bool z_readonly;
   bool s_readonly;
};

bool
ilo_state_zs_init(struct ilo_state_zs *zs,
                  const struct ilo_dev *dev,
                  const struct ilo_state_zs_info *info);

bool
ilo_state_zs_init_for_null(struct ilo_state_zs *zs,
                           const struct ilo_dev *dev);

bool
ilo_state_zs_disable_hiz(struct ilo_state_zs *zs,
                         const struct ilo_dev *dev);

#endif /* ILO_STATE_ZS_H */
