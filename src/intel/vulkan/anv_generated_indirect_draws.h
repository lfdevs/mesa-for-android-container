/*
 * Copyright © 2022 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef ANV_GENERATED_INDIRECT_DRAWS_H
#define ANV_GENERATED_INDIRECT_DRAWS_H

#include <stdint.h>

/* This needs to match generated_draws.glsl :
 *
 *    layout(set = 0, binding = 2) uniform block
 */
struct anv_generated_indirect_draw_params {
   uint32_t is_indexed;
   uint32_t is_predicated;
   uint32_t draw_base;
   uint32_t draw_count;
   uint32_t instance_multiplier;
   uint32_t indirect_data_stride;
};

/* This needs to match generated_draws_count.glsl :
 *
 *    layout(set = 0, binding = 2) uniform block
 */
struct anv_generated_indirect_draw_count_params {
   uint32_t is_indexed;
   uint32_t is_predicated;
   uint32_t draw_base;
   uint32_t item_count;
   uint32_t draw_count;
   uint32_t instance_multiplier;
   uint32_t indirect_data_stride;
   uint32_t end_addr_ldw;
   uint32_t end_addr_udw;
};

struct anv_generate_indirect_params {
   union {
      struct anv_generated_indirect_draw_params       draw;
      struct anv_generated_indirect_draw_count_params draw_count;
   };

   /* Global address of binding 0 */
   uint64_t indirect_data_addr;

   /* Global address of binding 1 */
   uint64_t generated_cmds_addr;
};

#endif /* ANV_GENERATED_INDIRECT_DRAWS_H */
