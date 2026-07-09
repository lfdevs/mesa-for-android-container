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

#if MFT_CODEC_AV1ENC

#include "reference_frames_tracker_av1.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include "hmft_entrypoints.h"
#include "wpptrace.h"

#include "reference_frames_tracker_av1.tmh"

reference_frames_tracker_av1::reference_frames_tracker_av1( void *logId,
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
      m_gop_state.frame_num = 0;
      m_gop_state.order_hint = 0;

      ResetGopStateToIDR();
      m_frame_state_descriptor.gop_info = &m_gop_state;

      m_frame_state_descriptor.dpb_snapshot.reserve( m_MaxDPBCapacity + 1 );
   }
}

// release reference frame buffers
void
reference_frames_tracker_av1::release_reconpic( reference_frames_tracker_dpb_async_token *pAsyncDPBToken )
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
reference_frames_tracker_av1::begin_frame( reference_frames_tracker_dpb_async_token *pAsyncDPBToken,
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
   uint32_t longTermReferenceFrameInfo = 0xFFFF;
   bool isLTR = false;

   curframe_dpb_buffer = m_DPBManager.get_fresh_dpb_buffer();
   CHECKNULL_GOTO( curframe_dpb_buffer, E_OUTOFMEMORY, done );
   if( m_upTwoPassDPBManager )
   {
      curframe_dpb_downscaled_buffer = m_upTwoPassDPBManager->get_fresh_dpb_buffer();
      CHECKNULL_GOTO( curframe_dpb_downscaled_buffer, E_OUTOFMEMORY, done );
   }

   GOPStateBeginFrame( forceKey );

   if( m_frame_state_descriptor.gop_info->frame_type == PIPE_AV1_ENC_FRAME_TYPE_KEY )
   {
      for( auto &i : m_PrevFramesInfos )
      {
         ( pAsyncDPBToken )->dpb_buffers_to_release.push_back( i.buffer );
         if( m_upTwoPassDPBManager )
            ( pAsyncDPBToken )->dpb_downscaled_buffers_to_release.push_back( i.downscaled_buffer );
      }
      m_PrevFramesInfos.clear();
   }

   m_gop_state.long_term_reference_frame_info = longTermReferenceFrameInfo;

   // fill dpb descriptor
   m_frame_state_descriptor.dpb_snapshot.clear();

   for( const auto &frame : m_PrevFramesInfos )
   {
      m_frame_state_descriptor.dpb_snapshot.emplace_back(
         /*id*/ 0u,
         /* order_hint */ frame.order_hint,
         frame.buffer );
   }

   if( m_frame_state_descriptor.gop_info->reference_type != frame_descriptor_reference_type_none )
   {
      // Add current frame DPB info
      m_frame_state_descriptor.dpb_snapshot.push_back( { /*id*/ 0u,
                                                         /* order_hint */ m_frame_state_descriptor.gop_info->order_hint,
                                                         curframe_dpb_buffer } );

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

      m_PrevFramesInfos.push_back( { m_frame_state_descriptor.gop_info->order_hint,
                                     isLTR,
                                     m_frame_state_descriptor.gop_info->ltr_index,
                                     m_frame_state_descriptor.gop_info->temporal_id,
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

const reference_frames_tracker_frame_descriptor *
reference_frames_tracker_av1::get_frame_descriptor()
{
   return (const reference_frames_tracker_frame_descriptor *) &m_frame_state_descriptor;
}

// reset gop state to IDR
void
reference_frames_tracker_av1::ResetGopStateToIDR()
{
   m_current_gop_frame_position_index = 0;
   m_gop_state.intra_period = m_gopLength;
   m_gop_state.ip_period = m_p_picture_period;
   m_gop_state.frame_type = PIPE_AV1_ENC_FRAME_TYPE_KEY;
   m_gop_state.order_hint = 0;
   m_gop_state.temporal_id = 0;
   m_gop_state.reference_type = frame_descriptor_reference_type_short_term;
   m_gop_state.ltr_index = 0;
   m_gop_state.refresh_frame_flags = 0xFF;
   for( int i = 0; i < 7; ++i )
   {
      m_gop_state.ref_frame_idx[i] = 0;
   }
}

// returns the frame type for the current frame derived using the current frame position index.
pipe_av1_enc_frame_type
reference_frames_tracker_av1::GetNextFrameType()
{
   if( m_current_gop_frame_position_index == 0 )
      return m_force_idr_on_gop_start ? PIPE_AV1_ENC_FRAME_TYPE_KEY : PIPE_AV1_ENC_FRAME_TYPE_INTRA_ONLY;
   else if( m_p_picture_period == 0 )
      return PIPE_AV1_ENC_FRAME_TYPE_KEY;
   else
      return ( m_current_gop_frame_position_index % m_p_picture_period == 0 ) ? PIPE_AV1_ENC_FRAME_TYPE_INTER :
                                                                                PIPE_AV1_ENC_FRAME_TYPE_INTER;
}

// initializes the gop state for the current frame
void
reference_frames_tracker_av1::GOPStateBeginFrame( bool forceKey )
{
   if( m_first_frame_num )
   {
      m_first_frame_num = false;
   }
   else
   {
      m_gop_state.frame_num++;
   }
   pipe_av1_enc_frame_type next_frame_type = GetNextFrameType();
   if( forceKey || next_frame_type == PIPE_AV1_ENC_FRAME_TYPE_KEY )
   {
      ResetGopStateToIDR();
   }
   else
   {
      m_gop_state.order_hint = ( ( m_gop_state.order_hint + 1 ) % ( 1 << 7 ) );
      m_gop_state.frame_type = next_frame_type;
      m_gop_state.long_term_reference_frame_info = 0x0000FFFF;   // [31...16] ltr bitmap, [15...0] ltr index or 0xFFFF for STR
      m_gop_state.reference_type = frame_descriptor_reference_type_short_term;
      m_gop_state.refresh_frame_flags = 0xFF;
      for( int i = 0; i < 7; ++i )
      {
         m_gop_state.ref_frame_idx[i] = 0;
      }
   }
}

// moves the GOP state to the next frame for next frame
void
reference_frames_tracker_av1::advance_frame()
{
   m_current_gop_frame_position_index = ( m_gopLength > 0 ) ?   // Wrap around m_gop_length for non-infinite GOP
                                           ( ( m_current_gop_frame_position_index + 1 ) % m_gopLength ) :
                                           ( m_current_gop_frame_position_index + 1 );
}

#endif
