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

#ifndef ILO_STATE_VIEWPORT_H
#define ILO_STATE_VIEWPORT_H

#include "genhw/genhw.h"

#include "ilo_core.h"
#include "ilo_dev.h"

/*
 * From the Sandy Bridge PRM, volume 2 part 1, page 38:
 *
 *     "... 16 sets of viewport (VP) state parameters in the Clip unit's
 *      VertexClipTest function and in the SF unit's ViewportMapping and
 *      Scissor functions."
 */
#define ILO_STATE_VIEWPORT_MAX_COUNT 16

enum ilo_state_viewport_dirty_bits {
   ILO_STATE_VIEWPORT_SF_CLIP_VIEWPORT             = (1 << 0),
   ILO_STATE_VIEWPORT_CC_VIEWPORT                  = (1 << 1),
   ILO_STATE_VIEWPORT_SCISSOR_RECT                 = (1 << 2),
};

struct ilo_state_viewport_matrix_info {
   float scale[3];
   float translate[3];
};

struct ilo_state_viewport_scissor_info {
   /* all inclusive */
   uint16_t min_x;
   uint16_t min_y;
   uint16_t max_x;
   uint16_t max_y;
};

struct ilo_state_viewport_params_info {
   const struct ilo_state_viewport_matrix_info *matrices;
   const struct ilo_state_viewport_scissor_info *scissors;
   uint8_t count;
};

struct ilo_state_viewport_info {
   void *data;
   size_t data_size;

   struct ilo_state_viewport_params_info params;
};

struct ilo_state_viewport {
   void *data;
   uint8_t array_size;

   uint8_t count;
   uint32_t (*sf_clip)[16];
   uint32_t (*cc)[2];
   uint32_t (*scissor)[2];
};

struct ilo_state_viewport_delta {
   uint32_t dirty;
};

static inline size_t
ilo_state_viewport_data_size(const struct ilo_dev *dev, uint8_t array_size)
{
   const struct ilo_state_viewport *vp = NULL;
   return (sizeof(vp->sf_clip[0]) +
           sizeof(vp->cc[0]) +
           sizeof(vp->scissor[0])) * array_size;
}

bool
ilo_state_viewport_init(struct ilo_state_viewport *vp,
                        const struct ilo_dev *dev,
                        const struct ilo_state_viewport_info *info);

bool
ilo_state_viewport_init_data_only(struct ilo_state_viewport *vp,
                                  const struct ilo_dev *dev,
                                  void *data, size_t data_size);

bool
ilo_state_viewport_init_for_rectlist(struct ilo_state_viewport *vp,
                                     const struct ilo_dev *dev,
                                     void *data, size_t data_size);

bool
ilo_state_viewport_set_params(struct ilo_state_viewport *vp,
                              const struct ilo_dev *dev,
                              const struct ilo_state_viewport_params_info *params,
                              bool scissors_only);

void
ilo_state_viewport_full_delta(const struct ilo_state_viewport *vp,
                              const struct ilo_dev *dev,
                              struct ilo_state_viewport_delta *delta);

void
ilo_state_viewport_get_delta(const struct ilo_state_viewport *vp,
                             const struct ilo_dev *dev,
                             const struct ilo_state_viewport *old,
                             struct ilo_state_viewport_delta *delta);

#endif /* ILO_STATE_VIEWPORT_H */
