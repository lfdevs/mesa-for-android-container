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

#if MFT_CODEC_H264ENC

#include "reference_frames_tracker_h264.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include "hmft_entrypoints.h"
#include "wpptrace.h"

#include "reference_frames_tracker_h264.tmh"

reference_frames_tracker_h264::reference_frames_tracker_h264( void *logId,
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
                                                              bool bSendUnwrappedPOC,
                                                              std::unique_ptr<dpb_buffer_manager> upTwoPassDPBManager,
                                                              HRESULT &hr )
   : m_logId( logId ),
     m_codec( codec ),
     m_MaxL0References( MaxL0References ),
     m_MaxDPBCapacity( MaxDPBCapacity ),
     m_MaxLongTermReferences( MaxLongTermReferences ),
     m_bSendUnwrappedPOC( bSendUnwrappedPOC ),
     m_ALL_LTR_VALID_MASK( ( 1 << m_MaxLongTermReferences ) - 1 ),
     m_DPBManager(
        logId,
        m_codec,
        textureWidth,
        textureHeight,
        ConvertProfileToFormat( m_codec->profile ),
        m_codec->max_references + 1 /*curr pic*/ +
           ( bLowLatency ? 0 : MFT_INPUT_QUEUE_DEPTH ), /*MFT process input queue depth for delayed in flight recon pic release*/
        hr ),
     m_upTwoPassDPBManager( std::move( upTwoPassDPBManager ) )
{
   if( SUCCEEDED( hr ) )
   {
      assert( m_MaxL0References == 1 );
      m_bLayerCountSet = bLayerCountSet;
      m_uiLayerCount = layerCount;
      m_ValidLTRBitmap = m_ALL_LTR_VALID_MASK;

      m_gopLength = gopLength;
      m_layer_count_set = bLayerCountSet;
      m_layer_count = layerCount;
      m_force_idr_on_gop_start = true;

      assert( uiBPictureCount == 0 );
      m_p_picture_period = uiBPictureCount + 1;
      m_gop_state.idr_pic_id = 0;

      const uint32_t maxFrameNumBitsMinus4 = 4;   // legal range is 0 to 12, we will fix to 4 which corresponds to [0..255]
      m_gop_state.log2_max_frame_num_minus4 = maxFrameNumBitsMinus4;
      m_gop_state.log2_max_pic_order_cnt_lsb_minus4 = maxFrameNumBitsMinus4 + 1;
      m_max_frame_num = 1 << ( maxFrameNumBitsMinus4 + 4 );
      ResetGopStateToIDR();

      m_frame_state_descriptor.gop_info = &m_gop_state;
   }
}

// release reference frame buffers
void
reference_frames_tracker_h264::release_reconpic( reference_frames_tracker_dpb_async_token *pAsyncDPBToken )
{
   if( pAsyncDPBToken )
   {
      for( unsigned i = 0; i < pAsyncDPBToken->dpb_buffers_to_release.size(); i++ )
         m_DPBManager.release_dpb_buffer( pAsyncDPBToken->dpb_buffers_to_release[i] );

      if( m_upTwoPassDPBManager )
      {
         for( unsigned i = 0; i < pAsyncDPBToken->dpb_downscaled_buffers_to_release.size(); i++ )
            m_upTwoPassDPBManager->release_dpb_buffer( pAsyncDPBToken->dpb_downscaled_buffers_to_release[i] );
      }

      delete pAsyncDPBToken;
   }
}

// pass control variables for current frame to reference tracker and compute reference frame states
HRESULT
reference_frames_tracker_h264::begin_frame( reference_frames_tracker_dpb_async_token *pAsyncDPBToken,
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
   bool isLTR;
   uint32_t longTermReferenceFrameInfo;

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

   assert( m_bLayerCountSet == layerCountSet );

   GOPStateBeginFrame( forceKey );

   m_frame_state_descriptor.mmco_operations.clear();
   m_frame_state_descriptor.l0_reference_list.clear();
   m_frame_state_descriptor.ref_list0_mod_operations.clear();

   if( m_frame_state_descriptor.gop_info->frame_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR )
   {
      for( auto &i : m_PrevFramesInfos )
      {
         ( pAsyncDPBToken )->dpb_buffers_to_release.push_back( i.buffer );
         if( m_upTwoPassDPBManager )
            ( pAsyncDPBToken )->dpb_downscaled_buffers_to_release.push_back( i.downscaled_buffer );
      }
      m_PrevFramesInfos.clear();
      m_checkValidSTR = false;
      m_ValidSTRFrameNumNoWrap = UINT64_MAX;
      m_ActiveLTRBitmap = 0;
      m_ValidLTRBitmap = m_ALL_LTR_VALID_MASK;
      if( m_MaxLongTermReferences > 0 )
      {
         m_bSendMaxLongTermReferences = true;
      }
   }

   if( m_MaxLongTermReferences > 0 && m_frame_state_descriptor.gop_info->temporal_id == 0 )
   {
      // h264 auto marking
      uint32_t numActiveLTRs = GetNumberOfActiveLTR();
      if( !m_pendingMarkLTR && numActiveLTRs < m_MaxLongTermReferences )
      {

         int emptyIndex = FindEmptyLTRIndex();
         assert( emptyIndex != -1 );
         m_pendingMarkLTR = true;
         m_pendingMarkLTRIndex = emptyIndex;
      }

      if( m_pendingMarkLTR )
      {
         assert( m_gop_state.temporal_id == 0 );

         m_gop_state.reference_type = frame_descriptor_reference_type_long_term;
         m_gop_state.ltr_index = m_pendingMarkLTRIndex;
         m_pendingMarkLTR = false;
      }
   }

   if( m_frame_state_descriptor.gop_info->temporal_id == 0 )
   {
      if( layerCount != m_uiLayerCount )
      {
         m_uiLayerCount = layerCount;
         m_layer_count = m_uiLayerCount;
      }
   }

   isLTR = ( m_frame_state_descriptor.gop_info->reference_type == frame_descriptor_reference_type_long_term );
   if( m_frame_state_descriptor.gop_info->frame_type == PIPE_H2645_ENC_PICTURE_TYPE_P )
   {
      ltrUsedBitMask = PrepareFrameRefLists( useLTR, useLTRBitmap );
   }

   longTermReferenceFrameInfo = ( ltrUsedBitMask << 16 ) | ( isLTR ? m_frame_state_descriptor.gop_info->ltr_index : 0xFFFF );
   m_gop_state.long_term_reference_frame_info = longTermReferenceFrameInfo;   // update GOP state

   // fill dpb descriptor
   m_frame_state_descriptor.dpb_snapshot.clear();
   m_frame_state_descriptor.dirty_rect_frame_num.clear();

   // Add prev frames DPB info
   for( unsigned i = 0; i < m_PrevFramesInfos.size(); i++ )
   {
      m_frame_state_descriptor.dpb_snapshot.push_back( {
         /*id*/ 0u,
         /*frame_idx*/ m_PrevFramesInfos[i].is_ltr ? m_PrevFramesInfos[i].ltr_index : m_PrevFramesInfos[i].frame_num,
         m_PrevFramesInfos[i].picture_order_count,
         m_PrevFramesInfos[i].temporal_id,
         m_PrevFramesInfos[i].is_ltr,
         m_PrevFramesInfos[i].buffer,
         m_PrevFramesInfos[i].downscaled_buffer,
      } );
      m_frame_state_descriptor.dirty_rect_frame_num.push_back( m_PrevFramesInfos[i].dirty_rect_frame_num );
   }

   if( m_frame_state_descriptor.gop_info->reference_type != frame_descriptor_reference_type_none )
   {
      // Add current frame DPB info
      m_frame_state_descriptor.dpb_snapshot.push_back( {
         /*id*/ 0u,
         isLTR ? m_frame_state_descriptor.gop_info->ltr_index : m_frame_state_descriptor.gop_info->frame_num,
         m_frame_state_descriptor.gop_info->picture_order_count,
         m_frame_state_descriptor.gop_info->temporal_id,
         isLTR,
         curframe_dpb_buffer,
         curframe_dpb_downscaled_buffer,
      } );
      m_frame_state_descriptor.dirty_rect_frame_num.push_back( dirtyRectFrameNum );


      if( m_MaxLongTermReferences > 0 )
      {
         if( m_bSendMaxLongTermReferences && m_frame_state_descriptor.gop_info->frame_type != PIPE_H2645_ENC_PICTURE_TYPE_IDR )
         {
            emit_mmco_max_long_term_references();
            m_bSendMaxLongTermReferences = false;
         }

         if( isLTR )
         {
            emit_mmc0_mark_current_frame_as_ltr( m_frame_state_descriptor.gop_info->ltr_index );
         }

         if( m_frame_state_descriptor.mmco_operations.size() != 0 )
         {
            emit_mmco_end_of_memory_management();
         }
      }

      if( m_frame_state_descriptor.mmco_operations.size() == 0 )
      {
         // Save frame infos if used as reference for next frame
         // Remove oldest short-term if DPB full
         if( m_PrevFramesInfos.size() == m_MaxDPBCapacity )
         {
            auto entryToRemove = std::find_if( m_PrevFramesInfos.begin(), m_PrevFramesInfos.end(), [&]( const PrevFrameInfo &p ) {
               return !p.is_ltr;
            } );
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
                                     m_frame_state_descriptor.gop_info->frame_num,
                                     m_frame_state_descriptor.gop_info->frame_num_no_wrap,
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
reference_frames_tracker_h264::PrepareFrameRefLists( bool useLTR, uint32_t useLTRBitmap )
{
   if( useLTR )
   {
      m_ValidLTRBitmap = useLTRBitmap & m_ALL_LTR_VALID_MASK;
      m_checkValidSTR = true;
      m_ValidSTRFrameNumNoWrap = m_frame_state_descriptor.gop_info->frame_num_no_wrap;
   }

   std::vector<RefSortList> ltrIndices;
   std::vector<RefSortList> strIndices;

   for( size_t i = 0; i < m_PrevFramesInfos.size(); ++i )
   {
      RefSortList item = { static_cast<uint8_t>( i ),
                           m_PrevFramesInfos[i].frame_num_no_wrap,
                           m_PrevFramesInfos[i].is_ltr,
                           m_PrevFramesInfos[i].ltr_index,
                           m_PrevFramesInfos[i].temporal_id };

      if( m_PrevFramesInfos[i].is_ltr )
      {
         ltrIndices.push_back( item );
      }
      else
      {
         strIndices.push_back( item );
      }
   }

   std::sort( strIndices.begin(), strIndices.end(), []( const RefSortList &a, const RefSortList &b ) {
      return a.frame_num_no_wrap > b.frame_num_no_wrap;
   } );   // sort STR descending

   bool usedSTR = false;
   if( !useLTR )
   {
      bool foundSuitableSTR = false;
      size_t suitableSTRIndex = 0;
      uint8_t current_tid = m_frame_state_descriptor.gop_info->temporal_id;
      uint8_t target_tid = current_tid > 0 ? current_tid - 1 : 0;
      for( size_t i = 0; i < strIndices.size(); ++i )
      {
         if( ( strIndices[i].temporal_id <= target_tid ) &&
             !( m_checkValidSTR && strIndices[i].frame_num_no_wrap <= m_ValidSTRFrameNumNoWrap ) )
         {
            foundSuitableSTR = true;
            suitableSTRIndex = i;
            break;
         }
      }

      if( foundSuitableSTR )
      {
         if( current_tid == 0 )
         {
            assert( strIndices[suitableSTRIndex].temporal_id == 0 );
         }
         else
         {
            assert( strIndices[suitableSTRIndex].temporal_id < current_tid );
         }

         if( suitableSTRIndex == 0 )
         {
            m_frame_state_descriptor.l0_reference_list.push_back( strIndices[suitableSTRIndex].pos );
         }
         else
         {
            uint64_t predFrameNumNoWrap = m_frame_state_descriptor.gop_info->frame_num_no_wrap;
            m_frame_state_descriptor.l0_reference_list.push_back( strIndices[suitableSTRIndex].pos );
            m_frame_state_descriptor.ref_list0_mod_operations.push_back(
               { 0u, static_cast<uint32_t>( predFrameNumNoWrap - strIndices[suitableSTRIndex].frame_num_no_wrap - 1 ), 0u } );
            predFrameNumNoWrap = strIndices[suitableSTRIndex].frame_num_no_wrap;
         }
         usedSTR = true;
      }
   }

   if( !usedSTR )
   {
      std::sort( ltrIndices.begin(), ltrIndices.end(), []( const RefSortList &a, const RefSortList &b ) {
         return a.frame_num_no_wrap > b.frame_num_no_wrap;
      } );   // sort LTR by frame_num descending

      for( auto ltr_idx : ltrIndices )
      {
         if( IsLTRIndexInValidBitMap( ltr_idx.ltr_index ) )
         {
            m_frame_state_descriptor.l0_reference_list.push_back( ltr_idx.pos );
            m_frame_state_descriptor.ref_list0_mod_operations.push_back( { 2u, 0u, ltr_idx.ltr_index } );
            break;
         }
      }
   }

   if( m_frame_state_descriptor.ref_list0_mod_operations.size() > 0 )
   {
      // attach at the back the modification_of_pic_nums_idc = 3 = End modification_of_pic_nums_idc syntax element loop
      m_frame_state_descriptor.ref_list0_mod_operations.push_back( { 3u, 0u, 0u } );
   }

   assert( m_frame_state_descriptor.l0_reference_list.size() == 1 );
   assert( m_frame_state_descriptor.ref_list0_mod_operations.size() <= 2 );

   uint32_t ltrUsedBitmask = 0;
   for( size_t i = 0; i < m_frame_state_descriptor.l0_reference_list.size(); ++i )
   {
      int idx = m_frame_state_descriptor.l0_reference_list[i];
      if( m_PrevFramesInfos[idx].is_ltr )
      {
         ltrUsedBitmask |= ( 1 << m_PrevFramesInfos[idx].ltr_index );
      }
   }

   return ltrUsedBitmask;
}

const reference_frames_tracker_frame_descriptor *
reference_frames_tracker_h264::get_frame_descriptor()
{
   return (const reference_frames_tracker_frame_descriptor *) &m_frame_state_descriptor;
}

// emit the mmco mark the current frame as ltr command
void
reference_frames_tracker_h264::emit_mmc0_mark_current_frame_as_ltr( uint32_t ref_frame_index )
{
   m_frame_state_descriptor.mmco_operations.push_back( {
      // uint8_t memory_management_control_operation;
      // Mark the current picture as "used for long-term reference" and assign a long-term frame index to it
      6u,
      // uint8_t difference_of_pic_nums_minus1;
      0u,
      // uint8_t long_term_pic_num;
      0u,
      // uint8_t long_term_frame_idx;
      static_cast<uint8_t>( ref_frame_index ),
      // uint8_t max_long_term_frame_idx_plus1;
      0u,
   } );
}

// emit the mmco max long term references command
void
reference_frames_tracker_h264::emit_mmco_max_long_term_references()
{
   // Set max_long_term_frame_idx_plus1 to m_MaxLongTermReferences
   m_frame_state_descriptor.mmco_operations.push_back( {
      // uint8_t memory_management_control_operation;
      // Specify the maximum long-term frame index and mark all long-term reference pictures having long-term
      // frame indices greater than the maximum value as "unused for reference"
      4u,
      // difference_of_pic_nums_minus1;
      0u,
      // uint8_t long_term_pic_num;
      0u,
      // uint8_t long_term_frame_idx;
      0u,
      // uint8_t max_long_term_frame_idx_plus1;
      static_cast<uint8_t>( m_MaxLongTermReferences ),
   } );
}

// emit the mmco end of memory management control command
void
reference_frames_tracker_h264::emit_mmco_end_of_memory_management()
{
   m_frame_state_descriptor.mmco_operations.push_back( { 0u, 0u, 0u, 0u, 0u } );
}

// returns the the number active LTR
uint32_t
reference_frames_tracker_h264::GetNumberOfActiveLTR()
{
   uint32_t activeLTR = 0;
   for( uint32_t i = 0; i < m_MaxLongTermReferences; ++i )
   {
      if( m_ActiveLTRBitmap & ( 1 << i ) )
      {
         activeLTR++;
      }
   }
   return activeLTR;
}

// find a free ltr index in the active LTR bitmap (for LTR auto marking)
int
reference_frames_tracker_h264::FindEmptyLTRIndex()
{
   int emptyIndex = -1;
   for( int i = 0; i < static_cast<int>( m_MaxLongTermReferences ); ++i )
   {
      if( ( m_ActiveLTRBitmap & ( 1 << i ) ) == 0 )
      {
         emptyIndex = i;
         break;
      }
   }
   return emptyIndex;
}

// mark the ltr index in the active LTR bitmap
void
reference_frames_tracker_h264::MarkLTRIndex( uint32_t index )
{
   assert( index < m_MaxLongTermReferences );
   m_ActiveLTRBitmap |= ( 1 << index );
   m_ValidLTRBitmap |= ( 1 << index );
}

// return whether the LTR index is in the active LTR bitmap which contains the active LTR indices.
bool
reference_frames_tracker_h264::IsLTRIndexInLTRBitmap( uint32_t index )
{
   assert( index < m_MaxLongTermReferences );
   return m_ActiveLTRBitmap & ( 1 << index );
}

// return whether the LTR index is in the valid LTR bitmap which contains the valid LTR indices.
bool
reference_frames_tracker_h264::IsLTRIndexInValidBitMap( uint32_t index )
{
   assert( index < m_MaxLongTermReferences );
   return m_ValidLTRBitmap & ( 1 << index );
}

// returns the frame type for the current frame derived using the current frame position index.
void
reference_frames_tracker_h264::ResetGopStateToIDR()
{
   m_current_gop_frame_position_index = 0;
   m_gop_state.intra_period = m_gopLength;
   m_gop_state.ip_period = m_p_picture_period;
   m_gop_state.frame_type = PIPE_H2645_ENC_PICTURE_TYPE_IDR;
   m_gop_state.frame_num = 0;
   m_gop_state.frame_num_no_wrap = 0;
   m_gop_state.current_reference_frame_count = 1;
   m_gop_state.picture_order_count = 0;
   m_gop_state.temporal_id = 0;
   m_gop_state.pic_order_cnt_type = ( m_p_picture_period > 2 ) ? 0u : 2u;
   m_gop_state.reference_type = frame_descriptor_reference_type_short_term;
   m_gop_state.ltr_index = 0;
}

pipe_h2645_enc_picture_type
reference_frames_tracker_h264::GetNextFrameType()
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
reference_frames_tracker_h264::GOPStateBeginFrame( bool forceKey )
{
   m_gop_state.frame_type = GetNextFrameType();
   uint32_t temporal_id = 0;
   if( m_layer_count_set )
   {
      assert( m_layer_count <= 2 );
      temporal_id = ( m_current_gop_frame_position_index & ( m_layer_count - 1 ) );
   }
   m_gop_state.long_term_reference_frame_info = 0x0000FFFF;   // [31...16] ltr bitmap, [15...0] ltr index or 0xFFFF for STR

   if( forceKey || m_gop_state.frame_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR )
   {
      if( m_first_idr )
      {
         m_first_idr = false;
      }
      else
      {
         m_gop_state.idr_pic_id++;
      }

      ResetGopStateToIDR();
   }
   else
   {
      // perhaps should add some handling for the unreasonably long encode case.
      assert( m_gop_state.current_reference_frame_count != UINT64_MAX );

      m_gop_state.frame_num = ( m_gop_state.current_reference_frame_count ) % m_max_frame_num;
      m_gop_state.frame_num_no_wrap = m_gop_state.current_reference_frame_count;

      if( temporal_id == 0 )
      {
         m_gop_state.reference_type = frame_descriptor_reference_type_short_term;
         m_gop_state.temporal_id = 0;
         if( m_bSendUnwrappedPOC )
         {
            m_gop_state.picture_order_count = ( 2 * m_gop_state.frame_num_no_wrap );
         }
         else
         {
            m_gop_state.picture_order_count = ( 2 * m_gop_state.frame_num ) % ( 2 * m_max_frame_num );
         }
         m_gop_state.current_reference_frame_count++;
      }
      else
      {
         m_gop_state.reference_type = frame_descriptor_reference_type_none;
         m_gop_state.temporal_id = 1;
         if( m_bSendUnwrappedPOC )
         {
            m_gop_state.picture_order_count = ( 2 * m_gop_state.frame_num_no_wrap - 1 );
         }
         else
         {
            m_gop_state.picture_order_count = ( 2 * m_gop_state.frame_num - 1 ) % ( 2 * m_max_frame_num );
         }
      }
   }
}

// moves the GOP state to the next frame for next frame
void
reference_frames_tracker_h264::advance_frame()
{
   m_current_gop_frame_position_index = ( m_gopLength > 0 ) ?   // Wrap around m_gop_length for non-infinite GOP
                                           ( ( m_current_gop_frame_position_index + 1 ) % m_gopLength ) :
                                           ( m_current_gop_frame_position_index + 1 );
}

//
// Intra Refresh Tracker
//

void
intra_refresh_tracker_row_h264::reset_ir_state_desc()
{
   m_ir_state_desc.base = *( (reference_frames_tracker_frame_descriptor_h264 *) m_ref_pics_tracker->get_frame_descriptor() );
   m_ir_state_desc.slices_config = m_non_ir_wave_slices_config;
   m_ir_state_desc.current_ir_wave_frame_index = 0;
   m_ir_state_desc.intra_refresh_params.mode = INTRA_REFRESH_MODE_NONE;
   m_ir_state_desc.intra_refresh_params.need_sequence_header = false;
   m_ir_state_desc.intra_refresh_params.offset = 0;
   m_ir_state_desc.intra_refresh_params.region_size = 0;
}

intra_refresh_tracker_row_h264::intra_refresh_tracker_row_h264( reference_frames_tracker *ref_pic_tracker,
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

intra_refresh_tracker_row_h264::~intra_refresh_tracker_row_h264()
{
   if( m_ref_pics_tracker )
      delete m_ref_pics_tracker;
}

// forward to underlying reference tracker
void
intra_refresh_tracker_row_h264::release_reconpic( reference_frames_tracker_dpb_async_token *pAsyncDPBToken )
{
   m_ref_pics_tracker->release_reconpic( pAsyncDPBToken );
}

// start intra refresh wave and then forward to underlying reference tracker
HRESULT
intra_refresh_tracker_row_h264::begin_frame( reference_frames_tracker_dpb_async_token *pAsyncDPBToken,
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
      reference_frames_tracker_frame_descriptor_h264 *underlying_frame_desc =
         (reference_frames_tracker_frame_descriptor_h264 *) m_ref_pics_tracker->get_frame_descriptor();
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
intra_refresh_tracker_row_h264::advance_frame()
{
   m_ref_pics_tracker->advance_frame();
}

const reference_frames_tracker_frame_descriptor *
intra_refresh_tracker_row_h264::get_frame_descriptor()
{
   m_ir_state_desc.base = *( (reference_frames_tracker_frame_descriptor_h264 *) m_ref_pics_tracker->get_frame_descriptor() );
   return (reference_frames_tracker_frame_descriptor *) &m_ir_state_desc.base;
}

// start intra refresh tracker wave for the current frame
bool
intra_refresh_tracker_row_h264::start_ir_wave()
{
   auto frame_type = ( (reference_frames_tracker_frame_descriptor_h264 *) get_frame_descriptor() )->gop_info->frame_type;
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
