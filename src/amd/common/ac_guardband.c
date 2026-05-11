/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 * Copyright 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "amd_family.h"

#include "ac_gpu_info.h"
#include "ac_guardband.h"
#include "util/macros.h"

void
ac_compute_guardband(const struct radeon_info *info, int minx, int miny,
                     int maxx, int maxy, enum ac_quant_mode quant_mode,
                     float clip_discard_distance, struct ac_guardband *guardband)
{
   float left, top, right, bottom, max_range, guardband_x, guardband_y;
   float scale[2], translate[2];

   /* Determine the optimal hardware screen offset to center the viewport
    * within the viewport range in order to maximize the guardband size.
    */
   int hw_screen_offset_x = (maxx + minx) / 2;
   int hw_screen_offset_y = (maxy + miny) / 2;

   /* GFX6-GFX7 need to align the offset to an ubertile consisting of all SEs. */
   const unsigned hw_screen_offset_alignment =
      info->gfx_level >= GFX11 ? 32 :
      info->gfx_level >= GFX8 ? 16 : MAX2(info->se_tile_repeat, 16);
   const unsigned max_hw_screen_offset = info->gfx_level >= GFX12 ? 32768 : 8176;

   /* Indexed by quantization modes */
   static int max_viewport_size[] = {65536, 16384, 4096};

   /* Ensure that the whole viewport stays representable in absolute
    * coordinates.
    */
   assert(maxx <= max_viewport_size[quant_mode] &&
          maxy <= max_viewport_size[quant_mode]);

   hw_screen_offset_x = CLAMP(hw_screen_offset_x, 0, max_hw_screen_offset);
   hw_screen_offset_y = CLAMP(hw_screen_offset_y, 0, max_hw_screen_offset);

   /* Align the screen offset by dropping the low bits. */
   hw_screen_offset_x &= ~(hw_screen_offset_alignment - 1);
   hw_screen_offset_y &= ~(hw_screen_offset_alignment - 1);

   /* Apply the offset to center the viewport and maximize the guardband. */
   minx -= hw_screen_offset_x;
   maxx -= hw_screen_offset_x;
   miny -= hw_screen_offset_y;
   maxy -= hw_screen_offset_y;

   /* Reconstruct the viewport transformation from the scissor. */
   translate[0] = (minx + maxx) / 2.0;
   translate[1] = (miny + maxy) / 2.0;
   scale[0] = maxx - translate[0];
   scale[1] = maxy - translate[1];

   /* Treat a 0x0 viewport as 1x1 to prevent division by zero. */
   if (minx == maxx)
      scale[0] = 0.5;
   if (miny == maxy)
      scale[1] = 0.5;

   /* Find the biggest guard band that is inside the supported viewport range.
    * The guard band is specified as a horizontal and vertical distance from
    * (0,0) in clip space.
    *
    * This is done by applying the inverse viewport transformation on the
    * viewport limits to get those limits in clip space.
    *
    * The viewport range is [-max_viewport_size/2 - 1, max_viewport_size/2].
    * (-1 to the min coord because max_viewport_size is odd and ViewportBounds
    * Min/Max are -32768, 32767).
    */
   assert(quant_mode < ARRAY_SIZE(max_viewport_size));
   max_range = max_viewport_size[quant_mode] / 2;
   left = (-max_range - 1 - translate[0]) / scale[0];
   right = (max_range - translate[0]) / scale[0];
   top = (-max_range - 1 - translate[1]) / scale[1];
   bottom = (max_range - translate[1]) / scale[1];

   assert(left <= -1 && top <= -1 && right >= 1 && bottom >= 1);

   guardband_x = MIN2(-left, right);
   guardband_y = MIN2(-top, bottom);

   float discard_x = 1.0;
   float discard_y = 1.0;

   /* Add half the point size / line width */
   discard_x += clip_discard_distance / (2.0 * scale[0]);
   discard_y += clip_discard_distance / (2.0 * scale[1]);

   /* Discard primitives that would lie entirely outside the viewport area. */
   discard_x = MIN2(discard_x, guardband_x);
   discard_y = MIN2(discard_y, guardband_y);

   guardband->clip_x = guardband_x;
   guardband->clip_y = guardband_y;
   guardband->discard_x = discard_x;
   guardband->discard_y = discard_y;
   guardband->hw_screen_offset_x = hw_screen_offset_x;
   guardband->hw_screen_offset_y = hw_screen_offset_y;
}
