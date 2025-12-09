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

#ifndef D3D12_VIDEO_ENCODE_REFERENCES_MANAGER_HEVC_H
#define D3D12_VIDEO_ENCODE_REFERENCES_MANAGER_HEVC_H

#include "d3d12_video_types.h"
#include "d3d12_video_encoder_references_manager.h"
#include "pipe/p_video_state.h"

class d3d12_video_encoder_references_manager_hevc : public d3d12_video_encoder_references_manager_interface
{
 public:
   void begin_frame(const D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA1& curFrameData,
                    bool bUsedAsReference,
                    struct pipe_picture_desc *picture);
   bool get_current_frame_picture_control_data(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA1 &codecAllocation);
   D3D12_VIDEO_ENCODE_REFERENCE_FRAMES get_current_reference_frames();

   bool is_current_frame_used_as_reference()
   {
      return m_isCurrentFrameUsedAsReference;
   }
   D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE get_current_frame_recon_pic_output_allocation()
   {
      return m_CurrentFrameReferencesData.ReconstructedPicTexture;
   }
   void end_frame()
   { }

   d3d12_video_encoder_references_manager_hevc(bool fArrayOfTextures) : m_fArrayOfTextures(fArrayOfTextures)
   {
      // Reserve memory for typical HEVC encoder usage to avoid per-frame allocations
      m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.reserve(PIPE_H265_MAX_DPB_SIZE);
      m_CurrentFrameReferencesData.ReferenceTextures.pResources.reserve(PIPE_H265_MAX_DPB_SIZE);
      m_CurrentFrameReferencesData.ReferenceTextures.pSubresources.reserve(PIPE_H265_MAX_DPB_SIZE);
      m_CurrentFrameReferencesData.pList0ReferenceFrames.reserve(PIPE_H265_MAX_NUM_LIST_REF);
      m_CurrentFrameReferencesData.pList1ReferenceFrames.reserve(PIPE_H265_MAX_NUM_LIST_REF);
      m_CurrentFrameReferencesData.pList0RefPicModifications.reserve(PIPE_H265_MAX_NUM_LIST_REF);
      m_CurrentFrameReferencesData.pList1RefPicModifications.reserve(PIPE_H265_MAX_NUM_LIST_REF);
   }

   ~d3d12_video_encoder_references_manager_hevc()
   { }

 private:
   // Class helpers
   void update_fifo_dpb_push_front_cur_recon_pic();
#ifdef MESA_DEBUG
   void print_dpb();
   void print_l0_l1_lists();
#endif

   // Class members
   struct d3d12_video_dpb
   {
      std::vector<ID3D12Resource *> pResources;
      std::vector<uint32_t> pSubresources;
   };

   struct current_frame_references_data
   {
      std::vector<D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_HEVC> pReferenceFramesReconPictureDescriptors;
      D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE ReconstructedPicTexture;
      d3d12_video_dpb ReferenceTextures;
      std::vector<UINT> pList0ReferenceFrames;
      std::vector<UINT> pList1ReferenceFrames;
      std::vector<UINT> pList0RefPicModifications;
      std::vector<UINT> pList1RefPicModifications;
   };

   current_frame_references_data m_CurrentFrameReferencesData;

   bool m_isCurrentFrameUsedAsReference = false;
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC2 m_curFrameState = {};
   bool m_fArrayOfTextures = false;
};

#endif
