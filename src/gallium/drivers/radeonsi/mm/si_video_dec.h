/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SI_VIDEO_DEC
#define SI_VIDEO_DEC

#include "pipe/p_video_codec.h"
#include "winsys/radeon_winsys.h"
#include "ac_video_dec.h"

#define SI_VIDEO_DEC_NUM_BUFS 4

struct si_video_dec {
   struct pipe_video_codec base;

   struct pipe_screen *screen;
   struct radeon_winsys *ws;
   struct radeon_cmdbuf cs;
   struct pipe_context *ectx;

   bool error;
   struct ac_video_dec *dec;
   struct si_resource *session_buffer;
   struct si_resource *session_tmz_buffer;
   struct si_resource *dpb_buffer;
   struct si_resource *bs_buffers[SI_VIDEO_DEC_NUM_BUFS];
   struct si_resource *emb_buffers[SI_VIDEO_DEC_NUM_BUFS];
   void *bs_ptr;
   unsigned bs_size;
   unsigned cur_buffer;
   unsigned dpb_size;
   unsigned dpb_alignment;

   struct pipe_resource *dpb[AC_VIDEO_DEC_MAX_REFS];
   struct pipe_video_buffer *render_pic_list[AC_VIDEO_DEC_MAX_REFS];
};

struct si_video_dec_inst {
   struct pipe_video_codec base;
   struct pipe_video_codec **inst;
   unsigned num_inst;
   unsigned cur_inst;
};

struct pipe_video_codec *si_create_video_decoder(struct pipe_context *context, const struct pipe_video_codec *templ);

#endif
