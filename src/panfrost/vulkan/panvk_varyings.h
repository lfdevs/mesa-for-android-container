/*
 * Copyright (C) 2021 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef PANVK_VARYINGS_H
#define PANVK_VARYINGS_H

#include "util/bitset.h"
#include "util/format/u_format.h"

#include "compiler/shader_enums.h"
#include "midgard_pack.h"
#include "panfrost-job.h"

struct pan_pool;
struct panvk_device;

enum panvk_varying_buf_id {
   PANVK_VARY_BUF_GENERAL,
   PANVK_VARY_BUF_POSITION,
   PANVK_VARY_BUF_PSIZ,
   PANVK_VARY_BUF_PNTCOORD,
   PANVK_VARY_BUF_FRAGCOORD,

   /* Keep last */
   PANVK_VARY_BUF_MAX,
};

struct panvk_varying {
   unsigned buf;
   unsigned offset;
   enum pipe_format format;
};

struct panvk_varying_buf {
   mali_ptr address;
   void *cpu;
   unsigned stride;
   unsigned size;
};

struct panvk_varyings_info {
   struct panvk_varying varying[VARYING_SLOT_MAX];
   BITSET_DECLARE(active, VARYING_SLOT_MAX);
   struct panvk_varying_buf buf[VARYING_SLOT_MAX];
   struct {
      unsigned count;
      gl_varying_slot loc[VARYING_SLOT_MAX];
   } stage[MESA_SHADER_STAGES];
   unsigned buf_mask;
};

void
panvk_varyings_alloc(struct panvk_varyings_info *varyings,
                     struct pan_pool *varying_mem_pool,
                     unsigned vertex_count);

unsigned
panvk_varyings_buf_count(const struct panvk_device *dev,
                         struct panvk_varyings_info *varyings);

static inline unsigned
panvk_varying_buf_index(const struct panvk_varyings_info *varyings,
                        enum panvk_varying_buf_id b)
{
   return util_bitcount(varyings->buf_mask & BITFIELD_MASK(b));
}

static inline enum panvk_varying_buf_id
panvk_varying_buf_id(bool fs, gl_varying_slot loc)
{
   switch (loc) {
   case VARYING_SLOT_POS:
      return fs ? PANVK_VARY_BUF_FRAGCOORD : PANVK_VARY_BUF_POSITION;
   case VARYING_SLOT_PSIZ:
      return PANVK_VARY_BUF_PSIZ;
   case VARYING_SLOT_PNTC:
      return PANVK_VARY_BUF_PNTCOORD;
   default:
      return PANVK_VARY_BUF_GENERAL;
   }
}

static inline bool
panvk_varying_is_builtin(gl_shader_stage stage, gl_varying_slot loc)
{
   bool fs = stage == MESA_SHADER_FRAGMENT;

   switch (loc) {
   case VARYING_SLOT_POS:
   case VARYING_SLOT_PNTC:
      return fs;
   default:
      return false;
   }
}

static inline enum mali_attribute_special
panvk_varying_special_buf_id(enum panvk_varying_buf_id buf_id)
{
   switch (buf_id) {
   case PANVK_VARY_BUF_PNTCOORD:
      return MALI_ATTRIBUTE_SPECIAL_POINT_COORD;
   case PANVK_VARY_BUF_FRAGCOORD:
      return MALI_ATTRIBUTE_SPECIAL_FRAG_COORD;
   default:
      return 0;
   }
}

static inline unsigned
panvk_varying_size(const struct panvk_varyings_info *varyings,
                   gl_varying_slot loc)
{
   switch (loc) {
   case VARYING_SLOT_POS:
      return sizeof(float) * 4;
   case VARYING_SLOT_PSIZ:
      return sizeof(uint16_t);
   default:
      return util_format_get_blocksize(varyings->varying[loc].format);
   }
}

#endif
