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

#if MFT_CODEC_H265ENC

#include <deque>
#include <memory>
#include <queue>

#include "dpb_buffer_manager.h"
#include "reference_frames_tracker.h"

typedef struct frame_descriptor_hevc
{
   uint32_t intra_period;
   uint32_t ip_period;
   pipe_h2645_enc_picture_type frame_type;
   bool is_used_as_future_reference;
   uint32_t picture_order_count;   // corresponds to PicOrderCntVal
   frame_descriptor_reference_type reference_type;
   uint32_t ltr_index;
   uint32_t pic_order_cnt_type;
   uint8_t temporal_id;
   uint8_t log2_max_pic_order_cnt_lsb_minus4;
   uint32_t long_term_reference_frame_info;   // corresponds to MFT attribute MFSampleExtension_LongTermReferenceFrameInfo
} frame_descriptor_hevc;

typedef struct reference_frames_tracker_frame_descriptor_hevc
{
   reference_frames_tracker_frame_descriptor base;
   const struct frame_descriptor_hevc *gop_info;
   std::vector<uint8_t> l0_reference_list;
   std::vector<struct pipe_h264_ref_list_mod_entry> ref_list0_mod_operations;
   std::vector<pipe_h264_enc_dpb_entry> dpb_snapshot;   // for HEVC use the same, ignore frame_num/idx
   std::vector<uint32_t> dirty_rect_frame_num;          // index corresponds to dbp_snapshot
} reference_frames_tracker_frame_descriptor_hevc;

class reference_frames_tracker_hevc : public reference_frames_tracker
{
 public:
   typedef struct PrevFrameInfo
   {
      uint32_t picture_order_count;
      bool is_ltr;
      uint32_t ltr_index;
      uint8_t temporal_id;
      uint32_t dirty_rect_frame_num;
      struct pipe_video_buffer *buffer;
      struct pipe_video_buffer *downscaled_buffer;
   } PrevFrameInfo;

   // used to sort PrevFrameInfo array
   typedef struct RefSortList
   {
      uint8_t pos;   // index into location in the PrevFrameInfo array
      // below are from PrevFrameInfo
      uint32_t picture_order_count;
      bool is_ltr;
      uint32_t ltr_index;
      uint8_t temporal_id;
   } RefSortList;

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
   reference_frames_tracker_hevc( void *logId,
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
   uint32_t PrepareFrameRefLists();

   void MarkLTRIndex( uint32_t index );
   bool IsLTRIndexInLTRBitmap( uint32_t index );

   reference_frames_tracker_frame_descriptor_hevc m_frame_state_descriptor = {};

   uint32_t m_MaxL0References = 0;
   uint32_t m_MaxDPBCapacity = 0;
   uint32_t m_MaxLongTermReferences = 0;

   uint32_t m_ActiveLTRBitmap = 0;

   std::deque<struct PrevFrameInfo> m_PrevFramesInfos;

   const void *m_logId = {};
   struct pipe_video_codec *m_codec;
   dpb_buffer_manager m_DPBManager;
   std::unique_ptr<dpb_buffer_manager> m_upTwoPassDPBManager;

   bool m_pendingMarkLTR = false;
   uint32_t m_pendingMarkLTRIndex = 0;

   // GOP Tracker
   void GOPStateBeginFrame( bool forceKey );

   pipe_h2645_enc_picture_type GetNextFrameType();
   void ResetGopStateToIDR();

   uint32_t m_gopLength = 0;
   uint32_t m_p_picture_period = 0;
   bool m_force_idr_on_gop_start = true;

   uint32_t m_current_gop_frame_position_index = 0;

   frame_descriptor_hevc m_gop_state;
   // GOP Tracker
};

typedef struct intra_refresh_tracker_frame_descriptor_hevc
{
   struct reference_frames_tracker_frame_descriptor_hevc base;
   struct intra_refresh_slices_config slices_config;
   uint32_t current_ir_wave_frame_index;
   struct pipe_enc_intra_refresh intra_refresh_params;
} intra_refresh_tracker_frame_descriptor_hevc;

class intra_refresh_tracker_row_hevc : public reference_frames_tracker, public intra_refresh_tracker
{
   // reference_frames_tracker
 public:
   HRESULT begin_frame( reference_frames_tracker_dpb_async_token *pAsyncDPBToken,
                        bool forceKey,
                        bool markLTR,
                        uint32_t mark_ltr_index,
                        bool useLTR,
                        uint32_t use_ltr_bitmap,
                        bool layerCountSet,
                        uint32_t layerCount,
                        bool dirtyRectFrameNumSet,
                        uint32_t dirtyRectFrameNum );
   void advance_frame();
   const reference_frames_tracker_frame_descriptor *get_frame_descriptor();
   void release_reconpic( reference_frames_tracker_dpb_async_token *pAsyncDPBToken );

 public:
   // intra_refresh_tracker
   bool start_ir_wave();
   // intra_refresh_tracker_row_h264x
   intra_refresh_tracker_row_hevc( reference_frames_tracker *ref_pic_tracker,
                                   uint32_t ir_wave_duration,
                                   intra_refresh_slices_config non_ir_wave_slices_config,
                                   uint32_t total_frame_macroblocks,
                                   bool continuous_refresh = true );
   ~intra_refresh_tracker_row_hevc();

 private:
   bool m_continuous_refresh = true;
   const uint32_t m_ir_wave_duration = 0u;
   reference_frames_tracker *m_ref_pics_tracker = {};
   const intra_refresh_slices_config m_non_ir_wave_slices_config = {};
   intra_refresh_tracker_frame_descriptor_hevc m_ir_state_desc = {};
   uint32_t m_total_frame_macroblocks = 0;
   void reset_ir_state_desc();
};

#endif
