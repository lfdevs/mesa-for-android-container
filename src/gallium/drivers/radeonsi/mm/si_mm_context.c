/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_mm.h"
#include "si_pipe.h"
#include "si_video.h"

bool si_init_mm_context(struct si_screen *sscreen, struct si_context *sctx, unsigned flags)
{
   /* Initialize multimedia functions if supported. */
   if (sscreen->b.get_video_param) {
      sctx->b.create_video_codec = si_video_codec_create;
      sctx->b.create_video_buffer = si_video_buffer_create;
      if (sscreen->b.resource_create_with_modifiers)
         sctx->b.create_video_buffer_with_modifiers = si_video_buffer_create_with_modifiers;
      return true;
   }
   return false;
}

void si_fini_mm_context(struct si_context *sctx) {
}
