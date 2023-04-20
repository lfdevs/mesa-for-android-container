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

#define ANV_GENERATED_FLAG_INDEXED    BITFIELD_BIT(0)
#define ANV_GENERATED_FLAG_PREDICATED BITFIELD_BIT(1)
#define ANV_GENERATED_FLAG_DRAWID     BITFIELD_BIT(2)
#define ANV_GENERATED_FLAG_BASE       BITFIELD_BIT(3)

/* This needs to match common_generated_draws.glsl :
 *
 *    layout(set = 0, binding = 2) uniform block
 */
struct anv_generated_indirect_draw_params {
   /* Draw ID buffer address (only used on Gfx9) */
   uint64_t draw_id_addr;
   /* Indirect data buffer address (only used on Gfx9) */
   uint64_t indirect_data_addr;
   /* Stride between each elements of the indirect data buffer */
   uint32_t indirect_data_stride;
   uint32_t flags; /* 0-7: bits, 8-15: mocs, 16-23: cmd_dws */
   /* Base number of the draw ID, it is added to the index computed from the
    * gl_FragCoord
    */
   uint32_t draw_base;

   /* Number of draws to generate */
   uint32_t draw_count;

   /* Maximum number of draws (equals to draw_count for indirect draws without
    * an indirect count)
    */
   uint32_t max_draw_count;

   /* Instance multiplier for multi view */
   uint32_t instance_multiplier;

   /* Address where to jump at after the generated draw (only used with
    * indirect draw count variants)
    */
   uint64_t end_addr;
};

struct anv_generated_indirect_params {
   struct anv_generated_indirect_draw_params draw;

   /* Global address of binding 0 */
   uint64_t indirect_data_addr;

   /* Global address of binding 1 */
   uint64_t generated_cmds_addr;

   /* Global address of binding 2 */
   uint64_t draw_ids_addr;

   /* CPU side pointer to the previous item when number of draws has to be
    * split into smaller chunks, see while loop in
    * genX(cmd_buffer_emit_indirect_generated_draws)
    */
   struct anv_generated_indirect_params *prev;
};

#endif /* ANV_GENERATED_INDIRECT_DRAWS_H */
