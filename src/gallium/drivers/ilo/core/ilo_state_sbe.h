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

#ifndef ILO_STATE_SBE_H
#define ILO_STATE_SBE_H

#include "genhw/genhw.h"

#include "ilo_core.h"
#include "ilo_dev.h"

/*
 * From the Sandy Bridge PRM, volume 2 part 1, page 264:
 *
 *     "Number of SF Output Attributes sets the number of attributes that will
 *      be output from the SF stage, not including position. This can be used
 *      to specify up to 32, and may differ from the number of input
 *      attributes."
 *
 *     "The first or last set of 16 attributes can be swizzled according to
 *      certain state fields."
 */
#define ILO_STATE_SBE_MAX_ATTR_COUNT 32
#define ILO_STATE_SBE_MAX_SWIZZLE_COUNT 16

struct ilo_state_sbe_swizzle_info {
   /* select an attribute from read ones */
   enum gen_inputattr_select attr_select;
   uint8_t attr;

   bool force_zeros;
};

struct ilo_state_sbe_info {
   uint8_t attr_count;

   /* which VUE attributes to read */
   uint8_t cv_vue_attr_count;
   uint8_t vue_read_base;
   uint8_t vue_read_count;
   bool has_min_read_count;

   bool cv_is_point;
   bool point_sprite_origin_lower_left;
   /* force sprite coordinates to the four corner vertices of the point */
   uint32_t point_sprite_enables;

   /* force attr at the provoking vertex to a0 and zero to a1/a2 */
   uint32_t const_interp_enables;

   bool swizzle_enable;
   /* swizzle attribute 16 to 31 instead; Gen7+ only */
   bool swizzle_16_31;
   uint8_t swizzle_count;
   const struct ilo_state_sbe_swizzle_info *swizzles;
};

struct ilo_state_sbe {
   uint32_t sbe[3];
   uint32_t swiz[8];
};

bool
ilo_state_sbe_init(struct ilo_state_sbe *sbe,
                   const struct ilo_dev *dev,
                   const struct ilo_state_sbe_info *info);

bool
ilo_state_sbe_init_for_rectlist(struct ilo_state_sbe *sbe,
                                const struct ilo_dev *dev,
                                uint8_t read_base,
                                uint8_t read_count);

bool
ilo_state_sbe_set_info(struct ilo_state_sbe *sbe,
                       const struct ilo_dev *dev,
                       const struct ilo_state_sbe_info *info);

#endif /* ILO_STATE_SBE_H */
