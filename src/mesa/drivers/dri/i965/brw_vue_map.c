/*
 * Copyright © 2011 Intel Corporation
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

/**
 * @file brw_vue_map.c
 *
 * This file computes the "VUE map" for a (non-fragment) shader stage, which
 * describes the layout of its output varyings.  The VUE map is used to match
 * outputs from one stage with the inputs of the next.
 *
 * Largely, varyings can be placed however we like - producers/consumers simply
 * have to agree on the layout.  However, there is also a "VUE Header" that
 * prescribes a fixed-layout for items that interact with fixed function
 * hardware, such as the clipper and rasterizer.
 *
 * Authors:
 *   Paul Berry <stereotype441@gmail.com>
 *   Chris Forbes <chrisf@ijw.co.nz>
 *   Eric Anholt <eric@anholt.net>
 */


#include "main/compiler.h"
#include "brw_context.h"

static inline void
assign_vue_slot(struct brw_vue_map *vue_map, int varying)
{
   /* Make sure this varying hasn't been assigned a slot already */
   assert (vue_map->varying_to_slot[varying] == -1);

   vue_map->varying_to_slot[varying] = vue_map->num_slots;
   vue_map->slot_to_varying[vue_map->num_slots++] = varying;
}

/**
 * Compute the VUE map for a shader stage.
 */
void
brw_compute_vue_map(const struct brw_device_info *devinfo,
                    struct brw_vue_map *vue_map,
                    GLbitfield64 slots_valid)
{
   vue_map->slots_valid = slots_valid;
   int i;

   /* gl_Layer and gl_ViewportIndex don't get their own varying slots -- they
    * are stored in the first VUE slot (VARYING_SLOT_PSIZ).
    */
   slots_valid &= ~(VARYING_BIT_LAYER | VARYING_BIT_VIEWPORT);

   /* Make sure that the values we store in vue_map->varying_to_slot and
    * vue_map->slot_to_varying won't overflow the signed chars that are used
    * to store them.  Note that since vue_map->slot_to_varying sometimes holds
    * values equal to BRW_VARYING_SLOT_COUNT, we need to ensure that
    * BRW_VARYING_SLOT_COUNT is <= 127, not 128.
    */
   STATIC_ASSERT(BRW_VARYING_SLOT_COUNT <= 127);

   vue_map->num_slots = 0;
   for (i = 0; i < BRW_VARYING_SLOT_COUNT; ++i) {
      vue_map->varying_to_slot[i] = -1;
      vue_map->slot_to_varying[i] = BRW_VARYING_SLOT_COUNT;
   }

   /* VUE header: format depends on chip generation and whether clipping is
    * enabled.
    *
    * See the Sandybridge PRM, Volume 2 Part 1, section 1.5.1 (page 30),
    * "Vertex URB Entry (VUE) Formats" which describes the VUE header layout.
    */
   if (devinfo->gen < 6) {
      /* There are 8 dwords in VUE header pre-Ironlake:
       * dword 0-3 is indices, point width, clip flags.
       * dword 4-7 is ndc position
       * dword 8-11 is the first vertex data.
       *
       * On Ironlake the VUE header is nominally 20 dwords, but the hardware
       * will accept the same header layout as Gen4 [and should be a bit faster]
       */
      assign_vue_slot(vue_map, VARYING_SLOT_PSIZ);
      assign_vue_slot(vue_map, BRW_VARYING_SLOT_NDC);
      assign_vue_slot(vue_map, VARYING_SLOT_POS);
   } else {
      /* There are 8 or 16 DWs (D0-D15) in VUE header on Sandybridge:
       * dword 0-3 of the header is indices, point width, clip flags.
       * dword 4-7 is the 4D space position
       * dword 8-15 of the vertex header is the user clip distance if
       * enabled.
       * dword 8-11 or 16-19 is the first vertex element data we fill.
       */
      assign_vue_slot(vue_map, VARYING_SLOT_PSIZ);
      assign_vue_slot(vue_map, VARYING_SLOT_POS);
      if (slots_valid & BITFIELD64_BIT(VARYING_SLOT_CLIP_DIST0))
         assign_vue_slot(vue_map, VARYING_SLOT_CLIP_DIST0);
      if (slots_valid & BITFIELD64_BIT(VARYING_SLOT_CLIP_DIST1))
         assign_vue_slot(vue_map, VARYING_SLOT_CLIP_DIST1);

      /* front and back colors need to be consecutive so that we can use
       * ATTRIBUTE_SWIZZLE_INPUTATTR_FACING to swizzle them when doing
       * two-sided color.
       */
      if (slots_valid & BITFIELD64_BIT(VARYING_SLOT_COL0))
         assign_vue_slot(vue_map, VARYING_SLOT_COL0);
      if (slots_valid & BITFIELD64_BIT(VARYING_SLOT_BFC0))
         assign_vue_slot(vue_map, VARYING_SLOT_BFC0);
      if (slots_valid & BITFIELD64_BIT(VARYING_SLOT_COL1))
         assign_vue_slot(vue_map, VARYING_SLOT_COL1);
      if (slots_valid & BITFIELD64_BIT(VARYING_SLOT_BFC1))
         assign_vue_slot(vue_map, VARYING_SLOT_BFC1);
   }

   /* The hardware doesn't care about the rest of the vertex outputs, so just
    * assign them contiguously.  Don't reassign outputs that already have a
    * slot.
    *
    * We generally don't need to assign a slot for VARYING_SLOT_CLIP_VERTEX,
    * since it's encoded as the clip distances by emit_clip_distances().
    * However, it may be output by transform feedback, and we'd rather not
    * recompute state when TF changes, so we just always include it.
    */
   for (int i = 0; i < VARYING_SLOT_MAX; ++i) {
      if ((slots_valid & BITFIELD64_BIT(i)) &&
          vue_map->varying_to_slot[i] == -1) {
         assign_vue_slot(vue_map, i);
      }
   }
}
