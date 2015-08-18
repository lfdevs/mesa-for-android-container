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

#ifndef ILO_STATE_URB_H
#define ILO_STATE_URB_H

#include "genhw/genhw.h"

#include "ilo_core.h"
#include "ilo_dev.h"

enum ilo_state_urb_dirty_bits {
   ILO_STATE_URB_3DSTATE_PUSH_CONSTANT_ALLOC_VS = (1 << 0),
   ILO_STATE_URB_3DSTATE_PUSH_CONSTANT_ALLOC_HS = (1 << 1),
   ILO_STATE_URB_3DSTATE_PUSH_CONSTANT_ALLOC_DS = (1 << 2),
   ILO_STATE_URB_3DSTATE_PUSH_CONSTANT_ALLOC_GS = (1 << 3),
   ILO_STATE_URB_3DSTATE_PUSH_CONSTANT_ALLOC_PS = (1 << 4),
   ILO_STATE_URB_3DSTATE_URB_VS                 = (1 << 5),
   ILO_STATE_URB_3DSTATE_URB_HS                 = (1 << 6),
   ILO_STATE_URB_3DSTATE_URB_DS                 = (1 << 7),
   ILO_STATE_URB_3DSTATE_URB_GS                 = (1 << 8),
};

/**
 * URB entry allocation sizes and sizes of constant data extracted from PCBs
 * to threads.
 */
struct ilo_state_urb_info {
   bool gs_enable;

   bool vs_const_data;
   bool hs_const_data;
   bool ds_const_data;
   bool gs_const_data;
   bool ps_const_data;

   uint16_t ve_entry_size;
   uint16_t vs_entry_size;
   uint16_t hs_entry_size;
   uint16_t ds_entry_size;
   uint16_t gs_entry_size;
};

struct ilo_state_urb {
   uint32_t pcb[5];
   uint32_t urb[4];
};

struct ilo_state_urb_delta {
   uint32_t dirty;
};

bool
ilo_state_urb_init(struct ilo_state_urb *urb,
                   const struct ilo_dev *dev,
                   const struct ilo_state_urb_info *info);

bool
ilo_state_urb_init_for_rectlist(struct ilo_state_urb *urb,
                                const struct ilo_dev *dev,
                                uint8_t vf_attr_count);

bool
ilo_state_urb_set_info(struct ilo_state_urb *urb,
                       const struct ilo_dev *dev,
                       const struct ilo_state_urb_info *info);

void
ilo_state_urb_full_delta(const struct ilo_state_urb *urb,
                         const struct ilo_dev *dev,
                         struct ilo_state_urb_delta *delta);

void
ilo_state_urb_get_delta(const struct ilo_state_urb *urb,
                        const struct ilo_dev *dev,
                        const struct ilo_state_urb *old,
                        struct ilo_state_urb_delta *delta);

#endif /* ILO_STATE_URB_H */
