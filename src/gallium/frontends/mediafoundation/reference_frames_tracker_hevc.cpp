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
#if MFT_CODEC_H265ENC
#include "reference_frames_tracker_hevc.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include "hmft_entrypoints.h"
#include "wpptrace.h"

#include "reference_frames_tracker_hevc.tmh"

reference_frames_tracker_hevc::reference_frames_tracker_hevc( void *logId,
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
                                                              HRESULT &hr )
   : m_logId( logId ),
     m_codec( codec ),
     m_MaxL0References( MaxL0References ),
     m_MaxDPBCapacity( MaxDPBCapacity ),
     m_MaxLongTermReferences( MaxLongTermReferences ),
     m_DPBManager(
        logId,
        m_codec,
        textureWidth,
        textureHeight,
        ConvertProfileToFormat( m_codec->profile ),
        m_codec->max_references + 1 /*curr pic*/ +
           ( bLowLatency ? 0 : MFT_INPUT_QUEUE_DEPTH ) /*MFT process input queue depth for delayed in flight recon pic release*/,
        hr ),
     m_upTwoPassDPBManager( std::move( upTwoPassDPBManager ) )
{
   if( SUCCEEDED( hr ) )
   {
      assert( m_MaxL0References == 1 );

      m_gopLength = gopLength;
      m_force_idr_on_gop_start = true;
      m_p_picture_period = uiBPictureCount + 1;
      m_gop_state.log2_max_pic_order_cnt_lsb_minus4 = 4;   // legal range is 0 to 12, we will fix to 4 which corresponds to [0..255]
      ResetGopStateToIDR();
      m_frame_state_descriptor.gop_info = &m_gop_state;

      m_frame_state_descriptor.l0_reference_list.resize( 1 );
      m_frame_state_descriptor.dpb_snapshot.reserve( m_MaxDPBCapacity + 1 );
      m_frame_state_descriptor.dirty_rect_frame_num.reserve( m_MaxDPBCapacity + 1 );
   }
}

// release reference frame buffers
void
reference_frames_tracker_hevc::release_reconpic( reference_frames_tracker_dpb_async_token *pAsyncDPBToken )
{
   if( pAsyncDPBToken )
   {
      for( const auto &buffer : pAsyncDPBToken->dpb_buffers_to_release )
         m_DPBManager.release_dpb_buffer( buffer );

      if( m_upTwoPassDPBManager )
      {
         for( const auto &buffer : pAsyncDPBToken->dpb_downscaled_buffers_to_release )
            m_upTwoPassDPBManager->release_dpb_buffer( buffer );
      }

      delete pAsyncDPBToken;
   }
}

// pass control variables for current frame to reference tracker and compute reference frame states
HRESULT
reference_frames_tracker_hevc::begin_frame( reference_frames_tracker_dpb_async_token *pAsyncDPBToken,
                                            bool forceKey,
                                            bool markLTR,
                                            uint32_t markLTRIndex,
                                            bool useLTR,
                                            uint32_t useLTRBitmap,
                                            bool layerCountSet,
                                            uint32_t layerCount,
                                            bool dirtyRectFrameNumSet,
                                            uint32_t dirtyRectFrameNum )
{
   HRESULT hr = S_OK;

   struct pipe_video_buffer *curframe_dpb_buffer = nullptr;
   struct pipe_video_buffer *curframe_dpb_downscaled_buffer = nullptr;
   uint32_t ltrUsedBitMask = 0;
   uint32_t longTermReferenceFrameInfo;
   bool isLTR;

   curframe_dpb_buffer = m_DPBManager.get_fresh_dpb_buffer();
   CHECKNULL_GOTO( curframe_dpb_buffer, E_OUTOFMEMORY, done );
   if( m_upTwoPassDPBManager )
   {
      curframe_dpb_downscaled_buffer = m_upTwoPassDPBManager->get_fresh_dpb_buffer();
      CHECKNULL_GOTO( curframe_dpb_downscaled_buffer, E_OUTOFMEMORY, done );
   }

   if( markLTR )
   {
      if( m_pendingMarkLTR )
      {
         debug_printf( "MFT: Mark LTR dropped due to pending LTR\n" );
      }
      else
      {
         m_pendingMarkLTR = markLTR;
         m_pendingMarkLTRIndex = markLTRIndex;
      }
   }

   GOPStateBeginFrame( forceKey );

   if( m_frame_state_descriptor.gop_info->frame_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR )
   {
      for( auto &i : m_PrevFramesInfos )
      {
         ( pAsyncDPBToken )->dpb_buffers_to_release.push_back( i.buffer );
         if( m_upTwoPassDPBManager )
            ( pAsyncDPBToken )->dpb_downscaled_buffers_to_release.push_back( i.downscaled_buffer );
      }
      m_PrevFramesInfos.clear();
      m_ActiveLTRBitmap = 0;
   }

   if( m_MaxLongTermReferences > 0 && m_frame_state_descriptor.gop_info->temporal_id == 0 )
   {

      if( m_pendingMarkLTR )
      {
         assert( m_gop_state.temporal_id == 0 );
         m_gop_state.reference_type = frame_descriptor_reference_type_long_term;
         m_gop_state.ltr_index = m_pendingMarkLTRIndex;
         m_pendingMarkLTR = false;
      }
   }

   isLTR = ( m_frame_state_descriptor.gop_info->reference_type == frame_descriptor_reference_type_long_term );

   if( m_frame_state_descriptor.gop_info->frame_type == PIPE_H2645_ENC_PICTURE_TYPE_P )
   {
      if( useLTR )
      {
         auto itr = m_PrevFramesInfos.begin();

         while( itr != m_PrevFramesInfos.end() )
         {
            bool is_ltr = itr->is_ltr;
            uint32_t ltr_index = itr->ltr_index;
            if( !is_ltr || ( is_ltr && !( useLTRBitmap & ( 1 << ltr_index ) ) ) )
            {
               ( pAsyncDPBToken )->dpb_buffers_to_release.push_back( itr->buffer );
               if( m_upTwoPassDPBManager )
                  ( pAsyncDPBToken )->dpb_downscaled_buffers_to_release.push_back( itr->downscaled_buffer );
               itr = m_PrevFramesInfos.erase( itr );
            }
            else
            {
               ++itr;
            }
         }

         m_ActiveLTRBitmap = useLTRBitmap & 0xFFFF;   // synchronize active LTR bitmap with useLTRBitmap
      }

      ltrUsedBitMask = PrepareFrameRefLists();
   }

   longTermReferenceFrameInfo = ( ltrUsedBitMask << 16 ) | ( isLTR ? m_frame_state_descriptor.gop_info->ltr_index : 0xFFFF );
   m_gop_state.long_term_reference_frame_info = longTermReferenceFrameInfo;   // Update GOP State

   // fill dpb descriptor
   m_frame_state_descriptor.dpb_snapshot.clear();
   m_frame_state_descriptor.dirty_rect_frame_num.clear();

   for( const auto &frame : m_PrevFramesInfos )
   {
      m_frame_state_descriptor.dpb_snapshot.emplace_back(
         /*id*/ 0u,
         /*frame_idx ignored*/ 0u,
         frame.picture_order_count,
         frame.temporal_id,
         frame.is_ltr,
         frame.buffer,
         frame.downscaled_buffer );
      m_frame_state_descriptor.dirty_rect_frame_num.push_back( frame.dirty_rect_frame_num );
   }

   if( m_frame_state_descriptor.gop_info->reference_type != frame_descriptor_reference_type_none )
   {
      // Add current frame DPB info
      m_frame_state_descriptor.dpb_snapshot.push_back( { /*id*/ 0u,
                                                         /* frame_idx - ignored */ 0u,
                                                         m_frame_state_descriptor.gop_info->picture_order_count,
                                                         m_frame_state_descriptor.gop_info->temporal_id,
                                                         isLTR,
                                                         curframe_dpb_buffer,
                                                         curframe_dpb_downscaled_buffer } );
      m_frame_state_descriptor.dirty_rect_frame_num.push_back( dirtyRectFrameNum );

      // if( m_frame_state_descriptor.gop_info->is_used_as_future_reference )
      // Save frame infos if used as reference for next frame
      // Remove oldest short-term if DPB full
      if( m_PrevFramesInfos.size() == m_MaxDPBCapacity )
      {
         auto entryToRemove =
            std::find_if( m_PrevFramesInfos.begin(), m_PrevFramesInfos.end(), [&]( const PrevFrameInfo &p ) { return !p.is_ltr; } );
         assert( entryToRemove != m_PrevFramesInfos.end() );
         if( entryToRemove == m_PrevFramesInfos.end() )
         {
            UNREACHABLE( "Unexpected zero STR" );
         }
         ( pAsyncDPBToken )->dpb_buffers_to_release.push_back( entryToRemove->buffer );
         if( m_upTwoPassDPBManager )
            ( pAsyncDPBToken )->dpb_downscaled_buffers_to_release.push_back( entryToRemove->downscaled_buffer );
         m_PrevFramesInfos.erase( entryToRemove );
      }

      if( isLTR )
      {
         // if current LTR index is already used, we will remove the existing LTR picture.
         if( IsLTRIndexInLTRBitmap( m_frame_state_descriptor.gop_info->ltr_index ) )
         {
            auto entryToRemove = m_PrevFramesInfos.begin();
            for( ; entryToRemove != m_PrevFramesInfos.end(); ++entryToRemove )
            {
               if( entryToRemove->is_ltr && entryToRemove->ltr_index == m_frame_state_descriptor.gop_info->ltr_index )
               {
                  break;
               }
            }
            assert( entryToRemove != m_PrevFramesInfos.end() );
            if( entryToRemove == m_PrevFramesInfos.end() )
            {
               UNREACHABLE( "Unexpected LTR replacement in Bitmap but not in PrevFramesInfos" );
            }
            ( pAsyncDPBToken )->dpb_buffers_to_release.push_back( entryToRemove->buffer );
            if( m_upTwoPassDPBManager )
               ( pAsyncDPBToken )->dpb_downscaled_buffers_to_release.push_back( entryToRemove->downscaled_buffer );
            m_PrevFramesInfos.erase( entryToRemove );
         }
         MarkLTRIndex( m_frame_state_descriptor.gop_info->ltr_index );
      }

      m_PrevFramesInfos.push_back( { m_frame_state_descriptor.gop_info->picture_order_count,
                                     isLTR,
                                     m_frame_state_descriptor.gop_info->ltr_index,
                                     m_frame_state_descriptor.gop_info->temporal_id,
                                     dirtyRectFrameNum,
                                     curframe_dpb_buffer,
                                     curframe_dpb_downscaled_buffer } );
   }
   else
   {
      ( pAsyncDPBToken )->dpb_buffers_to_release.push_back( curframe_dpb_buffer );
      if( m_upTwoPassDPBManager )
         ( pAsyncDPBToken )->dpb_downscaled_buffers_to_release.push_back( curframe_dpb_downscaled_buffer );
   }

done:
   return hr;
}

// prepare the reference list for the current frame
uint32_t
reference_frames_tracker_hevc::PrepareFrameRefLists()
{
   assert( m_PrevFramesInfos.size() >= 1 );

   const auto max_poc_iter =
      std::max_element( m_PrevFramesInfos.cbegin(), m_PrevFramesInfos.cend(), []( const auto &a, const auto &b ) noexcept {
         return a.picture_order_count < b.picture_order_count;
      } );

   m_frame_state_descriptor.l0_reference_list[0] =
      static_cast<uint8_t>( std::distance( m_PrevFramesInfos.cbegin(), max_poc_iter ) );

   const uint32_t ltrUsedBitmask = max_poc_iter->is_ltr ? ( 1u << max_poc_iter->ltr_index ) : 0u;

   return ltrUsedBitmask;
}

const reference_frames_tracker_frame_descriptor *
reference_frames_tracker_hevc::get_frame_descriptor()
{
   return (const reference_frames_tracker_frame_descriptor *) &m_frame_state_descriptor;
}

// mark the ltr index in the active LTR bitmap
void
reference_frames_tracker_hevc::MarkLTRIndex( uint32_t index )
{
   assert( index < m_MaxLongTermReferences );
   m_ActiveLTRBitmap |= ( 1 << index );
}

// return whether the LTR index is in the active LTR bitmap which contains the active LTR indices.
bool
reference_frames_tracker_hevc::IsLTRIndexInLTRBitmap( uint32_t index )
{
   assert( index < m_MaxLongTermReferences );
   return m_ActiveLTRBitmap & ( 1 << index );
}

// reset gop state to IDR
void
reference_frames_tracker_hevc::ResetGopStateToIDR()
{
   m_current_gop_frame_position_index = 0;
   m_gop_state.intra_period = m_gopLength;
   m_gop_state.ip_period = m_p_picture_period;
   m_gop_state.frame_type = PIPE_H2645_ENC_PICTURE_TYPE_IDR;
   m_gop_state.picture_order_count = 0;
   m_gop_state.temporal_id = 0;
   m_gop_state.is_used_as_future_reference = true;
   m_gop_state.pic_order_cnt_type = ( m_p_picture_period > 2 ) ? 0u : 2u;   // might not be needed
   m_gop_state.reference_type = frame_descriptor_reference_type_short_term;
   m_gop_state.ltr_index = 0;
}

// returns the frame type for the current frame derived using the current frame position index.
pipe_h2645_enc_picture_type
reference_frames_tracker_hevc::GetNextFrameType()
{
   if( m_current_gop_frame_position_index == 0 )
      return m_force_idr_on_gop_start ? PIPE_H2645_ENC_PICTURE_TYPE_IDR : PIPE_H2645_ENC_PICTURE_TYPE_I;
   else if( m_p_picture_period == 0 )
      return PIPE_H2645_ENC_PICTURE_TYPE_I;
   else
      return ( m_current_gop_frame_position_index % m_p_picture_period == 0 ) ? PIPE_H2645_ENC_PICTURE_TYPE_P :
                                                                                PIPE_H2645_ENC_PICTURE_TYPE_B;
}

// initializes the gop state for the current frame
void
reference_frames_tracker_hevc::GOPStateBeginFrame( bool forceKey )
{
   pipe_h2645_enc_picture_type next_frame_type = GetNextFrameType();
   if( forceKey || next_frame_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR )
   {
      ResetGopStateToIDR();
   }
   else
   {
      m_gop_state.picture_order_count++;
      m_gop_state.is_used_as_future_reference = true;   // Note to future self: Do not use B frames as references for other frames
      m_gop_state.frame_type = next_frame_type;
      m_gop_state.long_term_reference_frame_info = 0x0000FFFF;   // [31...16] ltr bitmap, [15...0] ltr index or 0xFFFF for STR
      m_gop_state.reference_type = frame_descriptor_reference_type_short_term;
   }
}

// moves the GOP state to the next frame for next frame
void
reference_frames_tracker_hevc::advance_frame()
{
   m_current_gop_frame_position_index = ( m_gopLength > 0 ) ?   // Wrap around m_gop_length for non-infinite GOP
                                           ( ( m_current_gop_frame_position_index + 1 ) % m_gopLength ) :
                                           ( m_current_gop_frame_position_index + 1 );
}

//
// Intra Refresh Tracker
//

void
intra_refresh_tracker_row_hevc::reset_ir_state_desc()
{
   m_ir_state_desc.base = *( (reference_frames_tracker_frame_descriptor_hevc *) m_ref_pics_tracker->get_frame_descriptor() );
   m_ir_state_desc.slices_config = m_non_ir_wave_slices_config;
   m_ir_state_desc.current_ir_wave_frame_index = 0;
   m_ir_state_desc.intra_refresh_params.mode = INTRA_REFRESH_MODE_NONE;
   m_ir_state_desc.intra_refresh_params.need_sequence_header = false;
   m_ir_state_desc.intra_refresh_params.offset = 0;
   m_ir_state_desc.intra_refresh_params.region_size = 0;
}

intra_refresh_tracker_row_hevc::intra_refresh_tracker_row_hevc( reference_frames_tracker *ref_pic_tracker,
                                                                uint32_t ir_wave_duration,
                                                                intra_refresh_slices_config non_ir_wave_slices_config,
                                                                uint32_t total_frame_macroblocks,
                                                                bool continuous_refresh )
   : m_ir_wave_duration( ir_wave_duration ),
     m_ref_pics_tracker( ref_pic_tracker ),
     m_non_ir_wave_slices_config( non_ir_wave_slices_config ),
     m_ir_state_desc( {} ),
     m_total_frame_macroblocks( total_frame_macroblocks ),
     m_continuous_refresh( continuous_refresh )
{
   reset_ir_state_desc();
}

intra_refresh_tracker_row_hevc::~intra_refresh_tracker_row_hevc()
{
   if( m_ref_pics_tracker )
      delete m_ref_pics_tracker;
}

// forward to underlying reference tracker
void
intra_refresh_tracker_row_hevc::release_reconpic( reference_frames_tracker_dpb_async_token *pAsyncDPBToken )
{
   m_ref_pics_tracker->release_reconpic( pAsyncDPBToken );
}

// start intra refresh wave and then forward to underlying reference tracker
HRESULT
intra_refresh_tracker_row_hevc::begin_frame( reference_frames_tracker_dpb_async_token *pAsyncDPBToken,
                                             bool forceKey,
                                             bool markLTR,
                                             uint32_t mark_ltr_index,
                                             bool useLTR,
                                             uint32_t use_ltr_bitmap,
                                             bool layerCountSet,
                                             uint32_t layerCount,
                                             bool dirtyRectFrameNumSet,
                                             uint32_t dirtyRectFrameNum )
{
   HRESULT hr = S_OK;
   if( m_ir_state_desc.intra_refresh_params.mode == INTRA_REFRESH_MODE_UNIT_ROWS )
   {
      if( ( ++m_ir_state_desc.current_ir_wave_frame_index ) < m_ir_wave_duration )
      {
         m_ir_state_desc.intra_refresh_params.need_sequence_header = false;
         m_ir_state_desc.intra_refresh_params.offset += m_ir_state_desc.intra_refresh_params.region_size;
      }
      else
      {
         reset_ir_state_desc();
      }
   }

   CHECKHR_GOTO( m_ref_pics_tracker->begin_frame( pAsyncDPBToken,
                                                  forceKey,
                                                  markLTR,
                                                  mark_ltr_index,
                                                  useLTR,
                                                  use_ltr_bitmap,
                                                  layerCountSet,
                                                  layerCount,
                                                  dirtyRectFrameNumSet,
                                                  dirtyRectFrameNum ),
                 done );

   {
      // If the underlying GOP tracker signaled an IDR (e.g a new GOP started) let's end any active IR wave
      reference_frames_tracker_frame_descriptor_hevc *underlying_frame_desc =
         (reference_frames_tracker_frame_descriptor_hevc *) m_ref_pics_tracker->get_frame_descriptor();
      if( underlying_frame_desc->gop_info->frame_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR )
      {
         reset_ir_state_desc();
      }
      else if(   // For P, B frames, restart the continuous IR wave if not already active
         ( ( underlying_frame_desc->gop_info->frame_type == PIPE_H2645_ENC_PICTURE_TYPE_P ) ||
           ( underlying_frame_desc->gop_info->frame_type == PIPE_H2645_ENC_PICTURE_TYPE_B ) ) &&
         ( m_continuous_refresh && ( m_ir_state_desc.intra_refresh_params.mode == INTRA_REFRESH_MODE_NONE ) ) )
      {
         start_ir_wave();
      }
   }
done:
   return hr;
}

// forward to underlying reference tracker
void
intra_refresh_tracker_row_hevc::advance_frame()
{
   m_ref_pics_tracker->advance_frame();
}

const reference_frames_tracker_frame_descriptor *
intra_refresh_tracker_row_hevc::get_frame_descriptor()
{
   m_ir_state_desc.base = *( (reference_frames_tracker_frame_descriptor_hevc *) m_ref_pics_tracker->get_frame_descriptor() );
   return (reference_frames_tracker_frame_descriptor *) &m_ir_state_desc.base;
}

// start intra refresh tracker wave for the current frame
bool
intra_refresh_tracker_row_hevc::start_ir_wave()
{
   auto frame_type = ( (reference_frames_tracker_frame_descriptor_hevc *) get_frame_descriptor() )->gop_info->frame_type;
   if( ( frame_type != PIPE_H2645_ENC_PICTURE_TYPE_B ) && ( frame_type != PIPE_H2645_ENC_PICTURE_TYPE_P ) )
   {
      debug_printf( "[intra_refresh_tracker_row_h264x::start_ir_wave] Error: IR wave can be only started on P/B frames.\n" );
      assert( false );
      return false;
   }

   if( m_ir_state_desc.intra_refresh_params.mode == INTRA_REFRESH_MODE_UNIT_ROWS )
   {
      debug_printf( "[intra_refresh_tracker_row_h264x::start_ir_wave] - Error: Another IR wave is currently active.\n" );
      assert( false );
      return false;
   }

   // Start IR wave with m_ir_wave_duration slices per frame (as per DX12 intra-refresh spec)
   m_ir_state_desc.intra_refresh_params.mode = INTRA_REFRESH_MODE_UNIT_ROWS;
   m_ir_state_desc.intra_refresh_params.need_sequence_header = true;
   m_ir_state_desc.intra_refresh_params.offset = 0;
   m_ir_state_desc.intra_refresh_params.region_size = m_total_frame_macroblocks / m_ir_wave_duration;

   m_ir_state_desc.slices_config.slice_mode = PIPE_VIDEO_SLICE_MODE_BLOCKS;
   m_ir_state_desc.slices_config.num_slice_descriptors = m_ir_wave_duration;
   uint32_t slice_starting_mb = 0;
   memset( m_ir_state_desc.slices_config.slices_descriptors, 0, sizeof( m_ir_state_desc.slices_config.slices_descriptors ) );
   for( uint32_t i = 0; i < m_ir_state_desc.slices_config.num_slice_descriptors; i++ )
   {
      m_ir_state_desc.slices_config.slices_descriptors[i].macroblock_address = slice_starting_mb;
      m_ir_state_desc.slices_config.slices_descriptors[i].num_macroblocks = m_ir_state_desc.intra_refresh_params.region_size;
      slice_starting_mb += m_ir_state_desc.intra_refresh_params.region_size;
   }

   return true;
}

#endif
