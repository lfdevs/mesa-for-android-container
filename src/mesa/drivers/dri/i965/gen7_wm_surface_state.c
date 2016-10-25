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
#include "main/mtypes.h"
#include "main/blend.h"
#include "main/samplerobj.h"
#include "main/texformat.h"
#include "main/teximage.h"
#include "program/prog_parameter.h"
#include "program/prog_instruction.h"

#include "intel_mipmap_tree.h"
#include "intel_batchbuffer.h"
#include "intel_tex.h"
#include "intel_fbo.h"
#include "intel_buffer_objects.h"

#include "brw_context.h"
#include "brw_state.h"
#include "brw_defines.h"
#include "brw_wm.h"

void
gen7_check_surface_setup(uint32_t *surf, bool is_render_target)
{
   unsigned num_multisamples = surf[4] & INTEL_MASK(5, 3);
   unsigned multisampled_surface_storage_format = surf[4] & (1 << 6);
   unsigned surface_array_spacing = surf[0] & (1 << 10);
   bool is_multisampled = num_multisamples != GEN7_SURFACE_MULTISAMPLECOUNT_1;

   (void) surface_array_spacing;

   /* From the Ivybridge PRM, Volume 4 Part 1, page 66 (RENDER_SURFACE_STATE
    * dword 0 bit 10 "Surface Array Spacing" Programming Notes):
    *
    *   If Multisampled Surface Storage Format is MSFMT_MSS and Number of
    *   Multisamples is not MULTISAMPLECOUNT_1, this field must be set to
    *   ARYSPC_LOD0.
    */
   if (multisampled_surface_storage_format == GEN7_SURFACE_MSFMT_MSS
       && is_multisampled)
      assert(surface_array_spacing == GEN7_SURFACE_ARYSPC_LOD0);

   /* From the Ivybridge PRM, Volume 4 Part 1, page 72 (RENDER_SURFACE_STATE
    * dword 4 bit 6 "Multisampled Surface Storage" Programming Notes):
    *
    *   All multisampled render target surfaces must have this field set to
    *   MSFMT_MSS.
    *
    * But also:
    *
    *   This field is ignored if Number of Multisamples is MULTISAMPLECOUNT_1.
    */
   if (is_render_target && is_multisampled) {
      assert(multisampled_surface_storage_format == GEN7_SURFACE_MSFMT_MSS);
   }

   /* From the Ivybridge PRM, Volume 4 Part 1, page 72 (RENDER_SURFACE_STATE
    * dword 4 bit 6 "Multisampled Surface Storage Format" Errata):
    *
    *   If the surface’s Number of Multisamples is MULTISAMPLECOUNT_8, Width
    *   is >= 8192 (meaning the actual surface width is >= 8193 pixels), this
    *   field must be set to MSFMT_MSS.
    */
   uint32_t width = GET_FIELD(surf[2], GEN7_SURFACE_WIDTH) + 1;
   if (num_multisamples == GEN7_SURFACE_MULTISAMPLECOUNT_8 && width >= 8193) {
      assert(multisampled_surface_storage_format == GEN7_SURFACE_MSFMT_MSS);
   }

   /* From the Ivybridge PRM, Volume 4 Part 1, page 72 (RENDER_SURFACE_STATE
    * dword 4 bit 6 "Multisampled Surface Storage Format" Errata):
    *
    *   If the surface’s Number of Multisamples is MULTISAMPLECOUNT_8,
    *   ((Depth+1) * (Height+1)) is > 4,194,304, OR if the surface’s Number of
    *   Multisamples is MULTISAMPLECOUNT_4, ((Depth+1) * (Height+1)) is >
    *   8,388,608, this field must be set to MSFMT_DEPTH_STENCIL.This field
    *   must be set to MSFMT_DEPTH_STENCIL if Surface Format is one of the
    *   following: I24X8_UNORM, L24X8_UNORM, A24X8_UNORM, or
    *   R24_UNORM_X8_TYPELESS.
    *
    * But also (from the Programming Notes):
    *
    *   This field is ignored if Number of Multisamples is MULTISAMPLECOUNT_1.
    */
   uint32_t depth = GET_FIELD(surf[3], BRW_SURFACE_DEPTH) + 1;
   uint32_t height = GET_FIELD(surf[2], GEN7_SURFACE_HEIGHT) + 1;
   if (num_multisamples == GEN7_SURFACE_MULTISAMPLECOUNT_8 &&
       depth * height > 4194304) {
      assert(multisampled_surface_storage_format ==
             GEN7_SURFACE_MSFMT_DEPTH_STENCIL);
   }
   if (num_multisamples == GEN7_SURFACE_MULTISAMPLECOUNT_4 &&
       depth * height > 8388608) {
      assert(multisampled_surface_storage_format ==
             GEN7_SURFACE_MSFMT_DEPTH_STENCIL);
   }
   if (is_multisampled) {
      switch (GET_FIELD(surf[0], BRW_SURFACE_FORMAT)) {
      case BRW_SURFACEFORMAT_I24X8_UNORM:
      case BRW_SURFACEFORMAT_L24X8_UNORM:
      case BRW_SURFACEFORMAT_A24X8_UNORM:
      case BRW_SURFACEFORMAT_R24_UNORM_X8_TYPELESS:
         assert(multisampled_surface_storage_format ==
                GEN7_SURFACE_MSFMT_DEPTH_STENCIL);
      }
   }
}

/**
 * Creates a null surface.
 *
 * This is used when the shader doesn't write to any color output.  An FB
 * write to target 0 will still be emitted, because that's how the thread is
 * terminated (and computed depth is returned), so we need to have the
 * hardware discard the target 0 color output..
 */
static void
gen7_emit_null_surface_state(struct brw_context *brw,
                             unsigned width,
                             unsigned height,
                             unsigned samples,
                             uint32_t *out_offset)
{
   /* From the Ivy bridge PRM, Vol4 Part1 p62 (Surface Type: Programming
    * Notes):
    *
    *     A null surface is used in instances where an actual surface is not
    *     bound. When a write message is generated to a null surface, no
    *     actual surface is written to. When a read message (including any
    *     sampling engine message) is generated to a null surface, the result
    *     is all zeros. Note that a null surface type is allowed to be used
    *     with all messages, even if it is not specificially indicated as
    *     supported. All of the remaining fields in surface state are ignored
    *     for null surfaces, with the following exceptions: Width, Height,
    *     Depth, LOD, and Render Target View Extent fields must match the
    *     depth buffer’s corresponding state for all render target surfaces,
    *     including null.
    */
   uint32_t *surf = brw_state_batch(brw, AUB_TRACE_SURFACE_STATE, 8 * 4, 32,
                                    out_offset);
   memset(surf, 0, 8 * 4);

   /* From the Ivybridge PRM, Volume 4, Part 1, page 65,
    * Tiled Surface: Programming Notes:
    * "If Surface Type is SURFTYPE_NULL, this field must be TRUE."
    */
   surf[0] = BRW_SURFACE_NULL << BRW_SURFACE_TYPE_SHIFT |
             BRW_SURFACEFORMAT_B8G8R8A8_UNORM << BRW_SURFACE_FORMAT_SHIFT |
             GEN7_SURFACE_TILING_Y;

   surf[2] = SET_FIELD(width - 1, GEN7_SURFACE_WIDTH) |
             SET_FIELD(height - 1, GEN7_SURFACE_HEIGHT);

   gen7_check_surface_setup(surf, true /* is_render_target */);
}

void
gen7_init_vtable_surface_functions(struct brw_context *brw)
{
   brw->vtbl.update_renderbuffer_surface = brw_update_renderbuffer_surface;
   brw->vtbl.emit_null_surface_state = gen7_emit_null_surface_state;
}
