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

#if MFT_CODEC_AV1ENC

#include <deque>
#include <memory>
#include <queue>

#include "dpb_buffer_manager.h"
#include "reference_frames_tracker.h"

typedef struct frame_descriptor_av1
{
   uint32_t intra_period;
   uint32_t ip_period;
   pipe_av1_enc_frame_type frame_type;
   uint32_t frame_num;
   uint32_t order_hint;
   uint8_t refresh_frame_flags;
   uint8_t ref_frame_idx[7];

   frame_descriptor_reference_type reference_type;
   uint32_t ltr_index;
   uint8_t temporal_id;
   uint32_t long_term_reference_frame_info;   // corresponds to MFT attribute MFSampleExtension_LongTermReferenceFrameInfo
} frame_descriptor_av1;

typedef struct reference_frames_tracker_frame_descriptor_av1
{
   reference_frames_tracker_frame_descriptor base;
   const struct frame_descriptor_av1 *gop_info;
   std::vector<pipe_av1_enc_dpb_entry> dpb_snapshot;
} reference_frames_tracker_frame_descriptor_av1;

class reference_frames_tracker_av1 : public reference_frames_tracker
{
 public:
   typedef struct PrevFrameInfo
   {
      uint32_t order_hint;
      bool is_ltr;
      uint32_t ltr_index;
      uint8_t temporal_id;
      struct pipe_video_buffer *buffer;
      struct pipe_video_buffer *downscaled_buffer;
   } PrevFrameInfo;

   // Declare reference_frames_tracker interface methods
   HRESULT begin_frame( reference_frames_tracker_dpb_async_token *pAsyncDPBToken,
                        bool forceKey,
                        bool markLTR,
                        uint32_t markLTRIndex,
                        bool useLTR,
                        uint32_t useLTRBitmap,
                        bool layerCountSet,
                        uint32_t layerCount,
                        bool dirtyRectFrameNumSet,
                        uint32_t dirtyRectFrameNum );

   void advance_frame();   // GOPTracker

   const reference_frames_tracker_frame_descriptor *get_frame_descriptor();
   void release_reconpic( reference_frames_tracker_dpb_async_token *pAsyncDPBToken );

   // Declare other methods
   reference_frames_tracker_av1( void *logId,
                                 struct pipe_video_codec *codec,
                                 uint32_t textureWidth,
                                 uint32_t textureHeight,
                                 uint32_t gopLength,
                                 uint32_t uiBPictureCount,
                                 bool bLayerCountSet,
                                 uint32_t layerCount,
                                 bool bLowLatency,
                                 uint32_t MaxL0References,
                                 uint32_t MaxDPBCapacity,
                                 uint32_t MaxLongTermReferences,
                                 std::unique_ptr<dpb_buffer_manager> upTwoPassDPBManager,
                                 HRESULT &hr );

 private:
   reference_frames_tracker_frame_descriptor_av1 m_frame_state_descriptor = {};

   uint32_t m_MaxL0References = 0;
   uint32_t m_MaxDPBCapacity = 0;
   uint32_t m_MaxLongTermReferences = 0;

   std::deque<struct PrevFrameInfo> m_PrevFramesInfos;

   const void *m_logId = {};
   struct pipe_video_codec *m_codec;
   dpb_buffer_manager m_DPBManager;
   std::unique_ptr<dpb_buffer_manager> m_upTwoPassDPBManager;

   // GOP Tracker
   void GOPStateBeginFrame( bool forceKey );

   pipe_av1_enc_frame_type GetNextFrameType();
   void ResetGopStateToIDR();

   uint32_t m_gopLength = 0;
   uint32_t m_p_picture_period = 0;
   bool m_force_idr_on_gop_start = true;

   uint32_t m_current_gop_frame_position_index = 0;

   bool m_first_frame_num = true;
   frame_descriptor_av1 m_gop_state;
   // GOP Tracker
};

#endif
