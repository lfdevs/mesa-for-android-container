/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#pragma once
#include <vector>
#include "pipe_headers.h"

typedef enum frame_descriptor_reference_type
{
   frame_descriptor_reference_type_none = 0,
   frame_descriptor_reference_type_short_term = 1,
   frame_descriptor_reference_type_long_term = 2,
} frame_descriptor_reference_type;

typedef struct reference_frames_tracker_frame_descriptor
{
} reference_frames_tracker_frame_descriptor;

class reference_frames_tracker_dpb_async_token
{
 public:
   std::vector<pipe_video_buffer *> dpb_buffers_to_release;
   std::vector<pipe_video_buffer *> dpb_downscaled_buffers_to_release;
};

class reference_frames_tracker
{
 public:
   // pass control variables for current frame to reference tracker and compute reference frame states
   virtual HRESULT begin_frame( reference_frames_tracker_dpb_async_token *pAsyncDPBToken,
                                bool forceKey,
                                bool markLTR,
                                uint32_t markLTRIndex,
                                bool useLTR,
                                uint32_t useLTRBitmap,
                                bool layerCountSet,
                                uint32_t layerCount,
                                bool dirtyRectFrameNumSet,
                                uint32_t dirtyRectFrameNum ) = 0;

   // moves the GOP state to the next frame for next frame
   virtual void advance_frame() = 0;

   // release reference frame buffers
   virtual void release_reconpic( reference_frames_tracker_dpb_async_token *pAsyncDPBToken ) = 0;

   // returns frame descriptor
   virtual const reference_frames_tracker_frame_descriptor *get_frame_descriptor() = 0;
   virtual ~reference_frames_tracker()
   { }
};

typedef struct intra_refresh_slices_config
{
   enum pipe_video_slice_mode slice_mode;
   // Use with PIPE_VIDEO_SLICE_MODE_BLOCKS
   unsigned num_slice_descriptors;
   struct h264_slice_descriptor slices_descriptors[128];
   // Use with PIPE_VIDEO_SLICE_MODE_MAX_SLICE_SLICE
   unsigned max_slice_bytes;
} intra_refresh_slices_config;

class intra_refresh_tracker
{
 public:
   // start intra refresh tracker wave for the current frame
   virtual bool start_ir_wave() = 0;
   virtual ~intra_refresh_tracker()
   { }
};
