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

#ifndef ILO_STATE_COMPUTE_H
#define ILO_STATE_COMPUTE_H

#include "genhw/genhw.h"

#include "ilo_core.h"
#include "ilo_dev.h"

/*
 * From the Haswell PRM, volume 7, page 836:
 *
 *     "The first 64 URB entries are reserved for the interface
 *      description..."
 */
#define ILO_STATE_COMPUTE_MAX_INTERFACE_COUNT 64

struct ilo_state_compute_interface_info {
   /* usually 0 unless there are multiple interfaces */
   uint32_t kernel_offset;

   uint32_t scratch_size;

   uint8_t sampler_count;
   uint8_t surface_count;

   uint16_t thread_group_size;
   uint32_t slm_size;

   uint16_t curbe_read_offset;
   uint16_t curbe_read_length;
   uint16_t cross_thread_curbe_read_length;
};

struct ilo_state_compute_info {
   void *data;
   size_t data_size;

   const struct ilo_state_compute_interface_info *interfaces;
   uint8_t interface_count;

   uint32_t cv_urb_alloc_size;
   uint32_t curbe_alloc_size;
};

struct ilo_state_compute {
   uint32_t vfe[3];

   uint32_t (*idrt)[6];
   uint8_t idrt_count;
};

static inline size_t
ilo_state_compute_data_size(const struct ilo_dev *dev,
                            uint8_t interface_count)
{
   const struct ilo_state_compute *compute = NULL;
   return sizeof(compute->idrt[0]) * interface_count;
}

bool
ilo_state_compute_init(struct ilo_state_compute *compute,
                       const struct ilo_dev *dev,
                       const struct ilo_state_compute_info *info);

#endif /* ILO_STATE_COMPUTE_H */
