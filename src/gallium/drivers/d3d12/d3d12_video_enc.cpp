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

#include "d3d12_common.h"

#include "d3d12_util.h"
#include "d3d12_context.h"
#include "d3d12_format.h"
#include "d3d12_resource.h"
#include "d3d12_screen.h"
#include "d3d12_surface.h"
#include "d3d12_video_enc.h"
#if VIDEO_CODEC_H264ENC
#include "d3d12_video_enc_h264.h"
#endif
#if VIDEO_CODEC_H265ENC
#include "d3d12_video_enc_hevc.h"
#endif
#if VIDEO_CODEC_AV1ENC
#include "d3d12_video_enc_av1.h"
#endif
#include "d3d12_video_buffer.h"
#include "d3d12_video_texture_array_dpb_manager.h"
#include "d3d12_video_array_of_textures_dpb_manager.h"
#include "d3d12_video_encoder_references_manager_h264.h"
#include "d3d12_video_encoder_references_manager_hevc.h"
#include "d3d12_video_encoder_references_manager_av1.h"
#include "d3d12_residency.h"
#include "d3d12_interop_public.h"

#include "vl/vl_video_buffer.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_video.h"

#include <cmath>

D3D12_VIDEO_ENCODER_CODEC
d3d12_video_encoder_convert_codec_to_d3d12_enc_codec(enum pipe_video_profile profile)
{
   switch (u_reduce_video_profile(profile)) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         return D3D12_VIDEO_ENCODER_CODEC_H264;
      } break;
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         return D3D12_VIDEO_ENCODER_CODEC_HEVC;
      } break;
      case PIPE_VIDEO_FORMAT_AV1:
      {
         return D3D12_VIDEO_ENCODER_CODEC_AV1;
      } break;
      case PIPE_VIDEO_FORMAT_MPEG12:
      case PIPE_VIDEO_FORMAT_MPEG4:
      case PIPE_VIDEO_FORMAT_VC1:
      case PIPE_VIDEO_FORMAT_JPEG:
      case PIPE_VIDEO_FORMAT_VP9:
      case PIPE_VIDEO_FORMAT_UNKNOWN:
      default:
      {
         UNREACHABLE("Unsupported pipe_video_profile");
      } break;
   }
}

size_t
d3d12_video_encoder_pool_current_index(struct d3d12_video_encoder *pD3D12Enc)
{
   return static_cast<size_t>(pD3D12Enc->m_fenceValue % pD3D12Enc->m_MaxQueueAsyncDepth);
}

size_t
d3d12_video_encoder_metadata_current_index(struct d3d12_video_encoder *pD3D12Enc)
{
   return static_cast<size_t>(pD3D12Enc->m_fenceValue % pD3D12Enc->m_MaxMetadataBuffersCount);
}

void
d3d12_video_encoder_flush(struct pipe_video_codec *codec)
{
   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;
   assert(pD3D12Enc);
   assert(pD3D12Enc->m_spD3D12VideoDevice);
   assert(pD3D12Enc->m_spEncodeCommandQueue);

   size_t current_pool_idx = d3d12_video_encoder_pool_current_index(pD3D12Enc);
   if (pD3D12Enc->m_inflightResourcesPool[current_pool_idx].encode_result & PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED) {
      debug_printf("WARNING: [d3d12_video_encoder] d3d12_video_encoder_flush - Frame submission %" PRIu64 " failed. Encoder lost, please recreate pipe_video_codec object \n", pD3D12Enc->m_fenceValue);
      assert(false);
      return;
   }

   // Wait for the Encode on context completion fence for this frame to ensure all context operations prior to encoding are completed
   struct d3d12_fence *casted_context_completion_fence = d3d12_fence(pD3D12Enc->m_inflightResourcesPool[current_pool_idx].context_completion_fence);
   if (casted_context_completion_fence)
   {
      pD3D12Enc->m_spEncodeCommandQueue->Wait(casted_context_completion_fence->cmdqueue_fence, casted_context_completion_fence->value);
      pD3D12Enc->m_pD3D12Screen->base.fence_reference(&pD3D12Enc->m_pD3D12Screen->base, &pD3D12Enc->m_inflightResourcesPool[current_pool_idx].context_completion_fence, NULL);
   }

   // Wait for the Encode on context completion fence for this frame to ensure all context operations prior to encoding are completed
   struct d3d12_fence *casted_headers_upload_completion_fence = d3d12_fence(pD3D12Enc->m_inflightResourcesPool[current_pool_idx].headers_upload_completion_fence);
   if (casted_headers_upload_completion_fence)
   {
      pD3D12Enc->m_spEncodeCommandQueue->Wait(casted_headers_upload_completion_fence->cmdqueue_fence, casted_headers_upload_completion_fence->value);
      pD3D12Enc->m_pD3D12Screen->base.fence_reference(&pD3D12Enc->m_pD3D12Screen->base, &pD3D12Enc->m_inflightResourcesPool[current_pool_idx].headers_upload_completion_fence, NULL);
   }

   // Wait on residency fence for this frame to ensure all resources used in encoding are resident
   if (pD3D12Enc->m_spResidencyFence && pD3D12Enc->m_ResidencyFenceValue > 0)
   {
      pD3D12Enc->m_spEncodeCommandQueue->Wait(pD3D12Enc->m_spResidencyFence.Get(), pD3D12Enc->m_ResidencyFenceValue);
   }

   struct d3d12_fence *input_surface_fence = pD3D12Enc->m_inflightResourcesPool[current_pool_idx].m_InputSurfaceFence;
   if (input_surface_fence)
      d3d12_fence_wait_impl(input_surface_fence, pD3D12Enc->m_spEncodeCommandQueue.Get(),
                            pD3D12Enc->m_inflightResourcesPool[current_pool_idx].m_InputSurfaceFenceValue);

   if (!pD3D12Enc->m_bPendingWorkNotFlushed) {
      debug_printf("[d3d12_video_encoder] d3d12_video_encoder_flush started. Nothing to flush, all up to date.\n");
   } else {
      debug_printf("[d3d12_video_encoder] d3d12_video_encoder_flush started. Will flush video queue work async"
                    " on fenceValue: %" PRIu64 "\n",
                    pD3D12Enc->m_fenceValue);

      HRESULT hr = S_OK;
#ifdef MESA_DEBUG
      hr = pD3D12Enc->m_pD3D12Screen->dev->GetDeviceRemovedReason();
      if (hr != S_OK) {
         debug_printf("[d3d12_video_encoder] d3d12_video_encoder_flush"
                         " - D3D12Device was removed BEFORE commandlist "
                         "execution with HR %x.\n",
                         (unsigned)hr);
         goto flush_fail;
      }
#endif

      hr = pD3D12Enc->m_spEncodeCommandList->Close();
      if (FAILED(hr)) {
         debug_printf("[d3d12_video_encoder] d3d12_video_encoder_flush - Can't close command list with HR %x\n", (unsigned)hr);
         goto flush_fail;
      }

      hr = pD3D12Enc->m_spResolveCommandList->Close();
      if (FAILED(hr)) {
         debug_printf("[d3d12_video_encoder] d3d12_video_encoder_flush - Can't close command list with HR %x\n", (unsigned)hr);
         goto flush_fail;
      }

      ID3D12CommandList *ppCommandLists[1] = { pD3D12Enc->m_spEncodeCommandList.Get() };
      pD3D12Enc->m_spEncodeCommandQueue->ExecuteCommandLists(1, ppCommandLists);
      pD3D12Enc->m_spEncodeCommandQueue->Signal(pD3D12Enc->m_spLastSliceFence.Get(), pD3D12Enc->m_LastSliceFenceValue);
      ID3D12CommandList *ppCommandLists2[1] = { pD3D12Enc->m_spResolveCommandList.Get() };
      pD3D12Enc->m_spResolveCommandQueue->Wait(pD3D12Enc->m_spLastSliceFence.Get(), pD3D12Enc->m_LastSliceFenceValue);
      pD3D12Enc->m_spResolveCommandQueue->ExecuteCommandLists(1, ppCommandLists2);
      pD3D12Enc->m_spResolveCommandQueue->Signal(pD3D12Enc->m_spFence.Get(), pD3D12Enc->m_fenceValue);
      
#ifdef MESA_DEBUG
      // Validate device was not removed
      hr = pD3D12Enc->m_pD3D12Screen->dev->GetDeviceRemovedReason();
      if (hr != S_OK) {
         debug_printf("[d3d12_video_encoder] d3d12_video_encoder_flush" 
                         " - D3D12Device was removed AFTER commandlist "
                         "execution with HR %x, but wasn't before.\n",
                         (unsigned)hr);
         goto flush_fail;
      }
#endif

      pD3D12Enc->m_fenceValue++;
      pD3D12Enc->m_LastSliceFenceValue++;
      pD3D12Enc->m_bPendingWorkNotFlushed = false;
   }
   return;

flush_fail:
   debug_printf("[d3d12_video_encoder] d3d12_video_encoder_flush failed for fenceValue: %" PRIu64 "\n", pD3D12Enc->m_fenceValue);
   pD3D12Enc->m_inflightResourcesPool[current_pool_idx].encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;
   pD3D12Enc->m_spEncodedFrameMetadata[d3d12_video_encoder_metadata_current_index(pD3D12Enc)].encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;
   assert(false);
}

bool
d3d12_video_encoder_sync_completion(struct pipe_video_codec *codec,
                                    size_t pool_index,
                                    uint64_t timeout_ns)
{
      struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;
      assert(pD3D12Enc);
      assert(pD3D12Enc->m_spD3D12VideoDevice);
      assert(pD3D12Enc->m_spEncodeCommandQueue);
      HRESULT hr = S_OK;

      auto &pool_entry = pD3D12Enc->m_inflightResourcesPool[pool_index];
      if (!d3d12_fence_finish(pool_entry.m_CompletionFence.get(), timeout_ns))
         return false;

      debug_printf("[d3d12_video_encoder] d3d12_video_encoder_sync_completion - resetting ID3D12CommandAllocator %p...\n",
         pool_entry.m_spCommandAllocator.Get());
      hr = pool_entry.m_spCommandAllocator->Reset();
      if(FAILED(hr)) {
         debug_printf("failed with %x.\n", (unsigned)hr);
         goto sync_with_token_fail;
      }

      debug_printf("[d3d12_video_encoder] d3d12_video_encoder_sync_completion - resetting ID3D12CommandAllocator %p...\n",
      pool_entry.m_spResolveCommandAllocator.Get());
      hr = pool_entry.m_spResolveCommandAllocator->Reset();
      if(FAILED(hr)) {
         debug_printf("failed with %x.\n", (unsigned)hr);
         goto sync_with_token_fail;
      }

      // Release references granted on end_frame for this inflight operations
      pool_entry.m_spEncoder.Reset();
      pool_entry.m_spEncoderHeap.Reset();
      pool_entry.m_References.reset();
      pool_entry.m_InputSurfaceFence = NULL;

#ifdef MESA_DEBUG
      // Validate device was not removed
      hr = pD3D12Enc->m_pD3D12Screen->dev->GetDeviceRemovedReason();
      if (hr != S_OK) {
         debug_printf("[d3d12_video_encoder] d3d12_video_encoder_sync_completion"
                         " - D3D12Device was removed AFTER d3d12_video_encoder_ensure_fence_finished "
                         "execution with HR %x, but wasn't before.\n",
                         (unsigned)hr);
         goto sync_with_token_fail;
      }
#endif

      debug_printf(
         "[d3d12_video_encoder] d3d12_video_encoder_sync_completion - GPU execution finalized for pool index: %" PRIu64 "\n",
         (uint64_t)pool_index);

      return true;

sync_with_token_fail:
   debug_printf("[d3d12_video_encoder] d3d12_video_encoder_sync_completion failed for pool index: %" PRIu64 "\n", (uint64_t)pool_index);
   pool_entry.encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;
   assert(false);
   return false;
}

/**
 * Destroys a d3d12_video_encoder
 * Call destroy_XX for applicable XX nested member types before deallocating
 * Destroy methods should check != nullptr on their input target argument as this method can be called as part of
 * cleanup from failure on the creation method
 */
void
d3d12_video_encoder_destroy(struct pipe_video_codec *codec)
{
   if (codec == nullptr) {
      return;
   }

   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;

      // Flush pending work before destroying
   if(pD3D12Enc->m_bPendingWorkNotFlushed){
      size_t pool_index = d3d12_video_encoder_pool_current_index(pD3D12Enc);
      d3d12_video_encoder_flush(codec);
      d3d12_video_encoder_sync_completion(codec, pool_index, OS_TIMEOUT_INFINITE);
   }

   if (pD3D12Enc->m_SliceHeaderRepackBuffer)
      pD3D12Enc->m_screen->resource_destroy(pD3D12Enc->m_screen, pD3D12Enc->m_SliceHeaderRepackBuffer);

   struct d3d12_context* ctx = d3d12_context(pD3D12Enc->base.context);
   if (ctx->priority_manager)
   {
      if (ctx->priority_manager->unregister_work_queue(ctx->priority_manager, pD3D12Enc->m_spEncodeCommandQueue.Get()) != 0)
      {
         debug_printf("D3D12: Failed to unregister command queue with frontend priority manager\n");
         assert(false);
      }

      if (ctx->priority_manager->unregister_work_queue(ctx->priority_manager, pD3D12Enc->m_spResolveCommandQueue.Get()) != 0)
      {
         debug_printf("D3D12: Failed to unregister command queue with frontend priority manager\n");
         assert(false);
      }
   }

   // Call d3d12_video_encoder dtor to make ComPtr and other member's destructors work
   delete pD3D12Enc;
}

static const char *
d3d12_video_encoder_friendly_frame_type_h264(D3D12_VIDEO_ENCODER_FRAME_TYPE_H264 picType)
{
   switch (picType) {
      case D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_P_FRAME:
      {
         return "H264_P_FRAME";
      } break;
      case D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME:
      {
         return "H264_B_FRAME";
      } break;
      case D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_I_FRAME:
      {
         return "H264_I_FRAME";
      } break;
      case D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_IDR_FRAME:
      {
         return "H264_IDR_FRAME";
      } break;
      default:
      {
         UNREACHABLE("Unsupported pipe_h2645_enc_picture_type");
      } break;
   }
}

static D3D12_VIDEO_ENCODER_FRAME_INPUT_MOTION_UNIT_PRECISION
d3d12_video_encoder_convert_move_precision(enum pipe_enc_move_info_precision_unit precision)
{
      switch (precision)
      {
         case PIPE_ENC_MOVE_INFO_PRECISION_UNIT_FULL_PIXEL:
         {
            return D3D12_VIDEO_ENCODER_FRAME_INPUT_MOTION_UNIT_PRECISION_FULL_PIXEL;
         } break;
         case PIPE_ENC_MOVE_INFO_PRECISION_UNIT_HALF_PIXEL:
         {
            return D3D12_VIDEO_ENCODER_FRAME_INPUT_MOTION_UNIT_PRECISION_HALF_PIXEL;
         } break;
         case PIPE_ENC_MOVE_INFO_PRECISION_UNIT_QUARTER_PIXEL:
         {
            return D3D12_VIDEO_ENCODER_FRAME_INPUT_MOTION_UNIT_PRECISION_QUARTER_PIXEL;
         } break;
         default:
         {
            UNREACHABLE("Unsupported pipe_enc_move_info");
            return D3D12_VIDEO_ENCODER_FRAME_INPUT_MOTION_UNIT_PRECISION_FULL_PIXEL;
         } break;
      }
}

void
d3d12_video_encoder_update_move_rects(struct d3d12_video_encoder *pD3D12Enc,
                                      const struct pipe_enc_move_info& rects)
{
   pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc = {};
      pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapSource = rects.input_mode == PIPE_ENC_MOVE_INFO_INPUT_MODE_RECTS ?
         D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_CPU_BUFFER : D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_GPU_TEXTURE;


   if (rects.input_mode == PIPE_ENC_MOVE_INFO_INPUT_MODE_RECTS)
   {
      assert(rects.num_rects <= PIPE_ENC_MOVE_RECTS_NUM_MAX);
      pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.RectsInfo.NumMoveRegions = std::min(rects.num_rects, static_cast<uint32_t>(PIPE_ENC_MOVE_RECTS_NUM_MAX));
      pD3D12Enc->m_currentEncodeConfig.m_MoveRectsArray.resize(pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.RectsInfo.NumMoveRegions);
      for (uint32_t i = 0; i < pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.RectsInfo.NumMoveRegions; i++) {
         pD3D12Enc->m_currentEncodeConfig.m_MoveRectsArray[i].SourcePoint.x = rects.rects[i].source_point.x;
         pD3D12Enc->m_currentEncodeConfig.m_MoveRectsArray[i].SourcePoint.y = rects.rects[i].source_point.y;
         pD3D12Enc->m_currentEncodeConfig.m_MoveRectsArray[i].DestRect.top = rects.rects[i].dest_rect.top;
         pD3D12Enc->m_currentEncodeConfig.m_MoveRectsArray[i].DestRect.left = rects.rects[i].dest_rect.left;
         pD3D12Enc->m_currentEncodeConfig.m_MoveRectsArray[i].DestRect.right = rects.rects[i].dest_rect.right;
         pD3D12Enc->m_currentEncodeConfig.m_MoveRectsArray[i].DestRect.bottom = rects.rects[i].dest_rect.bottom;
      }
      pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.RectsInfo.pMoveRegions = pD3D12Enc->m_currentEncodeConfig.m_MoveRectsArray.data();

      pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.RectsInfo.MotionSearchModeConfiguration.MotionSearchMode = D3D12_VIDEO_ENCODER_FRAME_MOTION_SEARCH_MODE_FULL_SEARCH;
      pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.RectsInfo.MotionSearchModeConfiguration.SearchDeviationLimit = 0u;

      pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.RectsInfo.SourceDPBFrameReference = rects.dpb_reference_index;

      pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.RectsInfo.Flags = rects.overlapping_rects ? D3D12_VIDEO_ENCODER_MOVEREGION_INFO_FLAG_MULTIPLE_HINTS :
                                                                                       D3D12_VIDEO_ENCODER_MOVEREGION_INFO_FLAG_NONE;

      pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.RectsInfo.MotionUnitPrecision = d3d12_video_encoder_convert_move_precision(rects.precision);
   }
   else if (rects.input_mode == PIPE_ENC_MOVE_INFO_INPUT_MODE_MAP)
   {
      pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.MotionSearchModeConfiguration.MotionSearchMode = D3D12_VIDEO_ENCODER_FRAME_MOTION_SEARCH_MODE_FULL_SEARCH;
      pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.MotionSearchModeConfiguration.SearchDeviationLimit = 0u;
      pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.NumHintsPerPixel = rects.num_hints;
      pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.ppMotionVectorMaps.resize(rects.num_hints);
      pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.ppMotionVectorMapsMetadata.resize(rects.num_hints);
      pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.pMotionVectorMapsGalliumResources.resize(rects.num_hints);
      pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.pMotionVectorMapsMetadataGalliumResources.resize(rects.num_hints);
      for (unsigned i = 0; i < rects.num_hints; i++)
      {
         assert(i < PIPE_ENC_MOVE_MAP_MAX_HINTS);
         pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.pMotionVectorMapsGalliumResources[i] = d3d12_resource(rects.gpu_motion_vectors_map[i]);
         pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.ppMotionVectorMaps[i] = d3d12_resource_resource(pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.pMotionVectorMapsGalliumResources[i]);
         pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.pMotionVectorMapsSubresources = NULL;
         pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.pMotionVectorMapsMetadataGalliumResources[i] = d3d12_resource(rects.gpu_motion_metadata_map[i]);
         pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.ppMotionVectorMapsMetadata[i] = d3d12_resource_resource(pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.pMotionVectorMapsMetadataGalliumResources[i]);
         pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.pMotionVectorMapsMetadataSubresources = NULL;
      }

      pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.MotionUnitPrecision = d3d12_video_encoder_convert_move_precision(rects.precision);
      // pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.PictureControlConfiguration is set later as not all the params are ready at this stage
   }
}

static void d3d12_video_encoder_is_gpu_qmap_input_feature_enabled(struct d3d12_video_encoder* pD3D12Enc, BOOL& isEnabled, D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE &outMapSourceEnabled)
{
   isEnabled = FALSE;
   //
   // Prefer GPU QP Map over CPU QP Delta Map if both are enabled
   //

   if (pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.CPUInput.AppRequested)
   {
      isEnabled = TRUE;
      outMapSourceEnabled = D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_CPU_BUFFER;
      assert(!pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.GPUInput.AppRequested); // When enabling CPU QP Map, GPU QP Delta must be disabled
   }

   if (pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.GPUInput.AppRequested)
   {
      isEnabled = TRUE;
      outMapSourceEnabled = D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_GPU_TEXTURE;
      assert(!pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.CPUInput.AppRequested); // When enabling GPU QP Map, CPU QP Delta must be disabled
   }
}

void
d3d12_video_encoder_update_qpmap_input(struct d3d12_video_encoder *pD3D12Enc,
                                       struct pipe_enc_qpmap_input_info &qpmap_info,
                                       struct pipe_enc_roi roi,
                                       uint32_t temporal_id)
{
   //
   // Clear QPDelta context for this frame
   //
   pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc = {};
   pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[temporal_id].m_Flags &= ~D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_DELTA_QP;

   //
   // Check if CPU/GPU QP Maps are enabled and store it in the context
   //
   if (qpmap_info.input_qp_mode == PIPE_ENC_QPMAP_INPUT_MODE_GPU_RESOURCE &&
       qpmap_info.input_gpu_qpmap)
   {
      pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.GPUInput.AppRequested = true;
      pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.GPUInput.InputMap =
         d3d12_resource(qpmap_info.input_gpu_qpmap);
      pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[temporal_id].m_Flags |=
         D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_DELTA_QP;
   }

   if (roi.num > 0)
   {
      pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.CPUInput.AppRequested = true;
      // pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.CPUInput.* QP matrices are copied over in
      // d3d12_video_encoder_xxx.cpp by calling d3d12_video_encoder_update_picparams_region_of_interest_qpmap method
      // from the different ROI structures/ranges passed by the application
      pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[temporal_id].m_Flags |=
         D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_DELTA_QP;
   } else if (qpmap_info.qp_map_values_count > 0 &&
              (qpmap_info.input_qp_mode == PIPE_ENC_QPMAP_INPUT_MODE_CPU_BUFFER_16BIT ||
               qpmap_info.input_qp_mode == PIPE_ENC_QPMAP_INPUT_MODE_CPU_BUFFER_8BIT))
   {
      pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.CPUInputBuffer.AppRequested = true;
      pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.CPUInputBuffer.m_qp_map_count =
         qpmap_info.qp_map_values_count;
      pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.CPUInputBuffer.m_input_qp_mode =
         static_cast<d3d12_video_encoder_input_qp_mode>(qpmap_info.input_qp_mode);
      pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[temporal_id].m_Flags |=
         D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_DELTA_QP;
      if (qpmap_info.input_qp_mode == PIPE_ENC_QPMAP_INPUT_MODE_CPU_BUFFER_8BIT && qpmap_info.input_qpmap_cpu) {
         // 8-bit Path
         pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.CPUInputBuffer.m_p_qp_map_cpu =
            qpmap_info.input_qpmap_cpu;
         debug_printf("[d3d12_video_encoder_update_qpmap_input] Using 8-bit CPU QP Map input mode with %u QP values Map Ptr = %p.\n",
            qpmap_info.qp_map_values_count,
            pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.CPUInputBuffer.m_p_qp_map_cpu);

      } else if (qpmap_info.input_qp_mode == PIPE_ENC_QPMAP_INPUT_MODE_CPU_BUFFER_16BIT && qpmap_info.input_qpmap_cpu) {
         // 16-bit Path
         pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.CPUInputBuffer.m_p_qp_map_cpu =
            qpmap_info.input_qpmap_cpu;
         debug_printf("[d3d12_video_encoder_update_qpmap_input] Using 16-bit CPU QP Map input mode with %u QP values Map Ptr = %p.\n",
                      qpmap_info.qp_map_values_count,
                      pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.CPUInputBuffer.m_p_qp_map_cpu);
      }
   }
}

void
d3d12_video_encoder_update_rate_control_saq(struct d3d12_video_encoder *pD3D12Enc,
                                            uint32_t saq_strength,
                                            uint32_t temporal_id)
{
   //
   // Clear SAQ flag for this frame and only enable it below if saq_strength > 0
   //
   pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[temporal_id].m_Flags &= ~D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_SPATIAL_ADAPTIVE_QP;

   if (saq_strength > 0)
   {
      pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[temporal_id].m_Flags |=
         D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_SPATIAL_ADAPTIVE_QP;
   }
}

/*
* Called on encoder creation with the encoder creation parameters
*/
void d3d12_video_encoder_initialize_two_pass(struct d3d12_video_encoder *pD3D12Enc,
                                             const struct pipe_enc_two_pass_encoder_config& two_pass)
{

   pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc = {};

   pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.two_pass_support.value = 
      pD3D12Enc->m_screen->get_video_param(pD3D12Enc->m_screen,
                                           pD3D12Enc->base.profile,
                                           pD3D12Enc->base.entrypoint,
                                           PIPE_VIDEO_CAP_ENC_TWO_PASS);

   pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.AppRequested = two_pass.enable;
   if (two_pass.pow2_downscale_factor > 0)
   {
      pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.Pow2DownscaleFactor = two_pass.pow2_downscale_factor;
      pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.bUseExternalDPBScaling = two_pass.skip_1st_dpb_texture;
   }
}

static
struct pipe_enc_two_pass_frame_config
d3d12_video_encoder_get_two_pass_config_from_picparams(struct pipe_picture_desc* picture,
                                                       enum pipe_video_format codec)
{
   struct pipe_enc_two_pass_frame_config twopass_frame_config = {};
   switch (codec) {
#if VIDEO_CODEC_H264ENC
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         twopass_frame_config = ((struct pipe_h264_enc_picture_desc *)picture)->twopass_frame_config;
      } break;
#endif
#if VIDEO_CODEC_H265ENC
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         twopass_frame_config = ((struct pipe_h265_enc_picture_desc *)picture)->twopass_frame_config;
      } break;
#endif
      default:
      {
         UNREACHABLE("Unsupported pipe_video_format");
      } break;
   }
   return twopass_frame_config;
}

/*
* Caller once per frame to update the frame specific two pass settings
* such as the skip_1st_pass_on_frame flag.
*
* This is called after the encoder has been created and the
* encoder settings have been initialized in d3d12_video_encoder_initialize_two_pass
* with the encoder creation two pass parameters.
*/
void
d3d12_video_encoder_update_two_pass_frame_settings(struct d3d12_video_encoder *pD3D12Enc,
                                                   enum pipe_video_format codec,
                                                   struct pipe_picture_desc* picture)
{
   if (pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.AppRequested)
   {
      struct pipe_enc_two_pass_frame_config two_pass_frame_cfg = d3d12_video_encoder_get_two_pass_config_from_picparams(picture, codec);

      // Assume two pass enabled for all frames unless supports_dynamic_1st_pass_skip is not supported and skip requested
      pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.bSkipTwoPassInCurrentFrame = false;
      if (pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.two_pass_support.bits.supports_dynamic_1st_pass_skip)
      {
         // Honor the app's request to skip the 1st pass if supports_dynamic_1st_pass_skip supported
         pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.bSkipTwoPassInCurrentFrame = two_pass_frame_cfg.skip_1st_pass != 0;
      }

      //
      // For when two pass is enabled for this frame AND Pow2DownscaleFactor > 0
      // also convert input downscaled texture and input recon pics (in/out)
      //
      if (!pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.bSkipTwoPassInCurrentFrame)
      {
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_FRAME_ANALYSIS;

         if (pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.Pow2DownscaleFactor > 0)
         {

            //
            // Convert the input downscaled texture from the pic params
            //
            struct d3d12_video_buffer *pDownscaledInputBuffer12 = (struct d3d12_video_buffer *) two_pass_frame_cfg.downscaled_source;
            pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.pDownscaledInputTexture = pDownscaledInputBuffer12 ? d3d12_resource_resource(pDownscaledInputBuffer12->texture) : NULL;

            //
            // Convert the DPB input and output params from the picparams DPB array
            //
            switch (codec) {
#if VIDEO_CODEC_H264ENC
               case PIPE_VIDEO_FORMAT_MPEG4_AVC:
               {
                  struct pipe_h264_enc_picture_desc *h264Pic = (struct pipe_h264_enc_picture_desc *) picture;
                  pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pResources.resize(h264Pic->dpb_size);
                  pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pSubresources.resize(h264Pic->dpb_size);
                  pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.FrameAnalysisReconstructedPictureOutput = { NULL, 0u };
                  for (uint8_t i = 0; i < h264Pic->dpb_size; i++) {
                     struct d3d12_video_buffer *vidbuf = (struct d3d12_video_buffer *) h264Pic->dpb[i].downscaled_buffer;
                     pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pResources[i] = d3d12_resource_resource(vidbuf->texture);
                     pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pSubresources[i] = vidbuf->idx_texarray_slots;
                     if (!pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.bUseExternalDPBScaling && // Pass NULL to the output recon pic 1st pass if bUseExternalDPBScaling set
                        (h264Pic->dpb[i].pic_order_cnt == h264Pic->pic_order_cnt))
                     {
                        pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.FrameAnalysisReconstructedPictureOutput.pReconstructedPicture =
                           pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pResources[i];
                        pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.FrameAnalysisReconstructedPictureOutput.ReconstructedPictureSubresource =
                           pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pSubresources[i];
                     }
                  }

                  // Now that we found the recon pio in the loop above
                  // only fill the references for frame types as DX12 expects
                  if ((h264Pic->picture_type == PIPE_H2645_ENC_PICTURE_TYPE_I) ||
                     (h264Pic->picture_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR))
                  {
                     pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pResources.clear();
                     pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pSubresources.clear();
                  }

               } break;
   #endif
   #if VIDEO_CODEC_H265ENC
               case PIPE_VIDEO_FORMAT_HEVC:
               {
                  struct pipe_h265_enc_picture_desc *h265Pic = (struct pipe_h265_enc_picture_desc *) picture;
                  pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pResources.resize(h265Pic->dpb_size);
                  pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pSubresources.resize(h265Pic->dpb_size);
                  pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.FrameAnalysisReconstructedPictureOutput = { NULL, 0u };
                  for (uint8_t i = 0; i < h265Pic->dpb_size; i++) {
                     struct d3d12_video_buffer *vidbuf = (struct d3d12_video_buffer *) h265Pic->dpb[i].downscaled_buffer;
                     pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pResources[i] = d3d12_resource_resource(vidbuf->texture);
                     pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pSubresources[i] = vidbuf->idx_texarray_slots;

                     if (!pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.bUseExternalDPBScaling && // Pass NULL to the output recon pic 1st pass if bUseExternalDPBScaling set
                        (h265Pic->dpb[i].pic_order_cnt == h265Pic->pic_order_cnt))
                     {
                        pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.FrameAnalysisReconstructedPictureOutput.pReconstructedPicture =
                           pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pResources[i];
                        pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.FrameAnalysisReconstructedPictureOutput.ReconstructedPictureSubresource =
                           pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pSubresources[i];
                     }
                  }

                  // Now that we found the recon pio in the loop above
                  // only fill the references for frame types as DX12 expects
                  if (h265Pic->picture_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR)
                  {
                     pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pResources.clear();
                     pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pSubresources.clear();
                  }

               } break;
#endif
               default:
               {
                  UNREACHABLE("Unsupported pipe_video_format");
               } break;
            }
         }
      }
   }
}

void
d3d12_video_encoder_update_dirty_rects(struct d3d12_video_encoder *pD3D12Enc,
                                       const struct pipe_enc_dirty_info& rects)
{
   pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc = {};

   pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapSource = rects.input_mode == PIPE_ENC_DIRTY_INFO_INPUT_MODE_RECTS ?
      D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_CPU_BUFFER : D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_GPU_TEXTURE;

   if(rects.input_mode == PIPE_ENC_DIRTY_INFO_INPUT_MODE_RECTS)
   {
      pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.RectsInfo.SourceDPBFrameReference = rects.dpb_reference_index;
      pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.RectsInfo.FullFrameIdentical = rects.full_frame_skip;
      pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.RectsInfo.MapValuesType =
         (rects.dirty_info_type == PIPE_ENC_DIRTY_INFO_TYPE_DIRTY) ? D3D12_VIDEO_ENCODER_DIRTY_REGIONS_MAP_VALUES_MODE_DIRTY :
                                                                     D3D12_VIDEO_ENCODER_DIRTY_REGIONS_MAP_VALUES_MODE_SKIP;

      if (!pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.RectsInfo.FullFrameIdentical)
      {
         assert(rects.num_rects <= PIPE_ENC_DIRTY_RECTS_NUM_MAX);
         pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.RectsInfo.NumDirtyRects = std::min(rects.num_rects, static_cast<uint32_t>(PIPE_ENC_DIRTY_RECTS_NUM_MAX));
         pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsArray.resize(pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.RectsInfo.NumDirtyRects);
         for (uint32_t i = 0; i < pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.RectsInfo.NumDirtyRects; i++) {
            pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsArray[i].top = rects.rects[i].top;
            pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsArray[i].left = rects.rects[i].left;
            pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsArray[i].right = rects.rects[i].right;
            pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsArray[i].bottom = rects.rects[i].bottom;
         }
         pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.RectsInfo.pDirtyRects = pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsArray.data();
      }
   }
   else if (rects.input_mode == PIPE_ENC_DIRTY_INFO_INPUT_MODE_MAP)
   {
      pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.SourceDPBFrameReference = rects.dpb_reference_index;
      pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.FullFrameIdentical = rects.full_frame_skip;
      pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.MapValuesType =
         (rects.dirty_info_type == PIPE_ENC_DIRTY_INFO_TYPE_DIRTY) ? D3D12_VIDEO_ENCODER_DIRTY_REGIONS_MAP_VALUES_MODE_DIRTY :
                                                                     D3D12_VIDEO_ENCODER_DIRTY_REGIONS_MAP_VALUES_MODE_SKIP;

      assert(pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.FullFrameIdentical || rects.map);
      pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.InputMap =
         pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.FullFrameIdentical ? NULL : d3d12_resource(rects.map);
      assert(pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.FullFrameIdentical ||
         pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.InputMap);
   }
}

void
d3d12_video_encoder_update_picparams_tracking(struct d3d12_video_encoder *pD3D12Enc,
                                              struct pipe_video_buffer *  srcTexture,
                                              struct pipe_picture_desc *  picture)
{
      D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA1 currentPicParams =
         d3d12_video_encoder_get_current_picture_param_settings(pD3D12Enc);

   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   bool bUsedAsReference = false;
   switch (codec) {
#if VIDEO_CODEC_H264ENC
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         d3d12_video_encoder_update_current_frame_pic_params_info_h264(pD3D12Enc, srcTexture, picture, currentPicParams.pH264PicData, bUsedAsReference);
      } break;
#endif
#if VIDEO_CODEC_H265ENC
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         d3d12_video_encoder_update_current_frame_pic_params_info_hevc(pD3D12Enc, srcTexture, picture, currentPicParams.pHEVCPicData, bUsedAsReference);
      } break;
#endif
#if VIDEO_CODEC_AV1ENC
      case PIPE_VIDEO_FORMAT_AV1:
      {
         d3d12_video_encoder_update_current_frame_pic_params_info_av1(pD3D12Enc, srcTexture, picture, currentPicParams.pAV1PicData, bUsedAsReference);
      } break;
#endif
      default:
      {
         UNREACHABLE("Unsupported pipe_video_format");
      } break;
   }

   size_t current_metadata_slot = d3d12_video_encoder_metadata_current_index(pD3D12Enc);
   debug_printf("d3d12_video_encoder_update_picparams_tracking submission saving snapshot for fenceValue %" PRIu64 " current_metadata_slot %" PRIu64 " - POC %d picture_type %s LayoutMode %d SlicesCount %d IRMode %d IRIndex %d\n",
                pD3D12Enc->m_fenceValue,
                static_cast<uint64_t>(current_metadata_slot),
                pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_associatedEncodeConfig.m_encoderPicParamsDesc.m_H264PicData.PictureOrderCountNumber,
                d3d12_video_encoder_friendly_frame_type_h264(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_associatedEncodeConfig.m_encoderPicParamsDesc.m_H264PicData.FrameType),
                pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_associatedEncodeConfig.m_encoderSliceConfigMode,
                pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_associatedEncodeConfig.m_encoderSliceConfigDesc.m_SlicesPartition_H264.NumberOfSlicesPerFrame,
                static_cast<uint32_t>(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_associatedEncodeConfig.m_IntraRefresh.Mode),
                pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_associatedEncodeConfig.m_IntraRefreshCurrentFrameIndex);
}

bool
d3d12_video_encoder_uses_direct_dpb(enum pipe_video_format codec)
{
   switch (codec) {
#if VIDEO_CODEC_H264ENC
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         return true;
      } break;
#endif
#if VIDEO_CODEC_H265ENC
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         return true;
      } break;
#endif
#if VIDEO_CODEC_AV1ENC
      case PIPE_VIDEO_FORMAT_AV1:
      {
        return false;
      } break;
#endif
      default:
      {
         UNREACHABLE("Unsupported pipe_video_format");
      } break;
   }
}

bool
d3d12_video_encoder_reconfigure_encoder_objects(struct d3d12_video_encoder *pD3D12Enc,
                                                struct pipe_video_buffer *  srcTexture,
                                                struct pipe_picture_desc *  picture)
{
   bool codecChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_codec) != 0);
   bool profileChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_profile) != 0);
   bool levelChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_level) != 0);
   bool codecConfigChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_codec_config) != 0);
   bool inputFormatChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_input_format) != 0);
   bool resolutionChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_resolution) != 0);
   bool rateControlChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_rate_control) != 0);
   bool slicesChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_slices) != 0);
   bool gopChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_gop) != 0);
   bool motionPrecisionLimitChanged = ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags &
                                        d3d12_video_encoder_config_dirty_flag_motion_precision_limit) != 0);
   bool irChanged = ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags &
                                        d3d12_video_encoder_config_dirty_flag_intra_refresh) != 0);
   [[maybe_unused]] bool dirtyRegionsChanged = ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags &
                                        d3d12_video_encoder_config_dirty_flag_dirty_regions) != 0);

   // Events that that trigger a re-creation of the reference picture manager
   // Stores codec agnostic textures so only input format, resolution and gop (num dpb references) affects this
   if (!pD3D12Enc->m_upDPBManager
       // || codecChanged
       // || profileChanged
       // || levelChanged
       // || codecConfigChanged
       || inputFormatChanged ||
       resolutionChanged
       // || rateControlChanged
       // || slicesChanged
       || gopChanged
       // || motionPrecisionLimitChanged
   ) {
      if (!pD3D12Enc->m_upDPBManager) {
         debug_printf("[d3d12_video_encoder] d3d12_video_encoder_reconfigure_encoder_objects - Creating Reference "
                       "Pictures Manager for the first time\n");
      } else {
         debug_printf("[d3d12_video_encoder] Reconfiguration triggered -> Re-creating Reference Pictures Manager\n");
      }

      enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
      if (!d3d12_video_encoder_uses_direct_dpb(codec))
      {
         D3D12_RESOURCE_FLAGS resourceAllocFlags =
            D3D12_RESOURCE_FLAG_VIDEO_ENCODE_REFERENCE_ONLY | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
         bool     fArrayOfTextures = ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                                 D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RECONSTRUCTED_FRAMES_REQUIRE_TEXTURE_ARRAYS) == 0);
         uint32_t texturePoolSize  = d3d12_video_encoder_get_current_max_dpb_capacity(pD3D12Enc);
         assert(texturePoolSize < UINT16_MAX);
         pD3D12Enc->m_upDPBStorageManager.reset();
         if (fArrayOfTextures) {
            pD3D12Enc->m_upDPBStorageManager = std::make_unique<d3d12_array_of_textures_dpb_manager>(
               static_cast<uint16_t>(texturePoolSize),
               pD3D12Enc->m_pD3D12Screen->dev,
               pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
               pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
               resourceAllocFlags,
               true,   // setNullSubresourcesOnAllZero - D3D12 Video Encode expects nullptr pSubresources if AoT,
               pD3D12Enc->m_NodeMask,
               /*use underlying pool, we can't reuse upper level allocations, need D3D12_RESOURCE_FLAG_VIDEO_ENCODE_REFERENCE_ONLY*/
               true);
         } else {
            pD3D12Enc->m_upDPBStorageManager = std::make_unique<d3d12_texture_array_dpb_manager>(
               static_cast<uint16_t>(texturePoolSize),
               pD3D12Enc->m_pD3D12Screen->dev,
               pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
               pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
               resourceAllocFlags,
               pD3D12Enc->m_NodeMask);
         }
      }

      d3d12_video_encoder_create_reference_picture_manager(pD3D12Enc, picture);
   }

   bool reCreatedEncoder = false;
   // Events that that trigger a re-creation of the encoder
   if (!pD3D12Enc->m_spVideoEncoder || codecChanged ||
       profileChanged
       // || levelChanged // Only affects encoder heap
       || codecConfigChanged ||
       inputFormatChanged
       // || resolutionChanged // Only affects encoder heap
       // Only re-create if there is NO SUPPORT for reconfiguring rateControl on the fly
       || (rateControlChanged && ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                                   D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RATE_CONTROL_RECONFIGURATION_AVAILABLE) ==
                                  0 /*checking the flag is NOT set*/))
       // Only re-create if there is NO SUPPORT for reconfiguring slices on the fly
       || (slicesChanged && ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                              D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SUBREGION_LAYOUT_RECONFIGURATION_AVAILABLE) ==
                             0 /*checking the flag is NOT set*/))
       // Only re-create if there is NO SUPPORT for reconfiguring gop on the fly
       || (gopChanged && ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                           D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SEQUENCE_GOP_RECONFIGURATION_AVAILABLE) ==
                          0 /*checking the flag is NOT set*/)) ||
       motionPrecisionLimitChanged) {
      if (!pD3D12Enc->m_spVideoEncoder) {
         debug_printf("[d3d12_video_encoder] d3d12_video_encoder_reconfigure_encoder_objects - Creating "
                       "D3D12VideoEncoder for the first time\n");
      } else {
         debug_printf("[d3d12_video_encoder] Reconfiguration triggered -> Re-creating D3D12VideoEncoder\n");
         reCreatedEncoder = true;
      }

      D3D12_VIDEO_ENCODER_DESC encoderDesc = { pD3D12Enc->m_NodeMask,
                                               D3D12_VIDEO_ENCODER_FLAG_NONE,
                                               pD3D12Enc->m_currentEncodeConfig.m_encoderCodecDesc,
                                               d3d12_video_encoder_get_current_profile_desc(pD3D12Enc),
                                               pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
                                               d3d12_video_encoder_get_current_codec_config_desc(pD3D12Enc),
                                               pD3D12Enc->m_currentEncodeConfig.m_encoderMotionPrecisionLimit };

      // Create encoder
      pD3D12Enc->m_spVideoEncoder.Reset();
      HRESULT hr = pD3D12Enc->m_spD3D12VideoDevice->CreateVideoEncoder(&encoderDesc,
                                                             IID_PPV_ARGS(pD3D12Enc->m_spVideoEncoder.GetAddressOf()));
      if (FAILED(hr)) {
         debug_printf("CreateVideoEncoder failed with HR %x\n", (unsigned)hr);
         return false;
      }
   }

   bool reCreatedEncoderHeap = false;
   // Events that that trigger a re-creation of the encoder heap
   if (!pD3D12Enc->m_spVideoEncoderHeap || codecChanged || profileChanged ||
       levelChanged
       // || codecConfigChanged // Only affects encoder
       || inputFormatChanged   // Might affect internal textures in the heap
       || resolutionChanged
       // Only re-create if there is NO SUPPORT for reconfiguring rateControl on the fly
       || (rateControlChanged && ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                                   D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RATE_CONTROL_RECONFIGURATION_AVAILABLE) ==
                                  0 /*checking the flag is NOT set*/))
       // Only re-create if there is NO SUPPORT for reconfiguring slices on the fly
       || (slicesChanged && ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                              D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SUBREGION_LAYOUT_RECONFIGURATION_AVAILABLE) ==
                             0 /*checking the flag is NOT set*/))
       // Only re-create if there is NO SUPPORT for reconfiguring gop on the fly
       || (gopChanged && ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                           D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SEQUENCE_GOP_RECONFIGURATION_AVAILABLE) ==
                          0 /*checking the flag is NOT set*/))
       // || motionPrecisionLimitChanged // Only affects encoder
       // Re-create encoder heap if dirty regions changes and the current heap doesn't already support them
       || (dirtyRegionsChanged && ((pD3D12Enc->m_spVideoEncoderHeap->GetEncoderHeapFlags() & D3D12_VIDEO_ENCODER_HEAP_FLAG_ALLOW_DIRTY_REGIONS) == 0))
   ) {
      if (!pD3D12Enc->m_spVideoEncoderHeap) {
         debug_printf("[d3d12_video_encoder] d3d12_video_encoder_reconfigure_encoder_objects - Creating "
                       "D3D12VideoEncoderHeap for the first time\n");
      } else {
         debug_printf("[d3d12_video_encoder] Reconfiguration triggered -> Re-creating D3D12VideoEncoderHeap\n");
         reCreatedEncoderHeap = true;
      }

      HRESULT hr = S_OK;
      // Use cached ID3D12VideoDevice4 interface if available
      if (pD3D12Enc->m_spD3D12VideoDevice4)
      {
         D3D12_VIDEO_ENCODER_HEAP_FLAGS heapFlags = D3D12_VIDEO_ENCODER_HEAP_FLAG_NONE;
         if (pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps.DirtyRegions.DirtyRegionsSupportFlags) {
            heapFlags |= D3D12_VIDEO_ENCODER_HEAP_FLAG_ALLOW_DIRTY_REGIONS;
         }

         //
         // Prefer individual slice buffers when possible
         //
         if (pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
            D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SUBREGION_NOTIFICATION_ARRAY_OF_BUFFERS_AVAILABLE)
         {
            heapFlags |= D3D12_VIDEO_ENCODER_HEAP_FLAG_ALLOW_SUBREGION_NOTIFICATION_ARRAY_OF_BUFFERS;
         }
         else if (pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
            D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SUBREGION_NOTIFICATION_SINGLE_BUFFER_AVAILABLE)
         {
            heapFlags |= D3D12_VIDEO_ENCODER_HEAP_FLAG_ALLOW_SUBREGION_NOTIFICATION_SINGLE_BUFFER;
         }

         if (pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.AppRequested)
         {
            heapFlags |= D3D12_VIDEO_ENCODER_HEAP_FLAG_ALLOW_RATE_CONTROL_FRAME_ANALYSIS;
         }

         D3D12_VIDEO_ENCODER_HEAP_DESC1 heapDesc1 = {
            pD3D12Enc->m_NodeMask,
            heapFlags,
            pD3D12Enc->m_currentEncodeConfig.m_encoderCodecDesc,
            d3d12_video_encoder_get_current_profile_desc(pD3D12Enc),
            d3d12_video_encoder_get_current_level_desc(pD3D12Enc),
            // resolution list count
            1,
            // resolution list
            &pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
            // UINT Pow2DownscaleFactor
            pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.AppRequested ?
               pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.Pow2DownscaleFactor
               : 0,
         };

         // Create encoder heap
         pD3D12Enc->m_spVideoEncoderHeap.Reset();
         pD3D12Enc->m_spVideoEncoderHeap1.Reset();
         hr = pD3D12Enc->m_spD3D12VideoDevice4->CreateVideoEncoderHeap1(&heapDesc1,
                                                              IID_PPV_ARGS(pD3D12Enc->m_spVideoEncoderHeap1.GetAddressOf()));
         if (SUCCEEDED(hr))
         {
            hr = pD3D12Enc->m_spVideoEncoderHeap1->QueryInterface(IID_PPV_ARGS(pD3D12Enc->m_spVideoEncoderHeap.GetAddressOf())); 
         }
      }
      else
      {
         D3D12_VIDEO_ENCODER_HEAP_DESC heapDesc = { pD3D12Enc->m_NodeMask,
                                                   D3D12_VIDEO_ENCODER_HEAP_FLAG_NONE,
                                                   pD3D12Enc->m_currentEncodeConfig.m_encoderCodecDesc,
                                                   d3d12_video_encoder_get_current_profile_desc(pD3D12Enc),
                                                   d3d12_video_encoder_get_current_level_desc(pD3D12Enc),
                                                   // resolution list count
                                                   1,
                                                   // resolution list
                                                   &pD3D12Enc->m_currentEncodeConfig.m_currentResolution };

         // Create encoder heap
         pD3D12Enc->m_spVideoEncoderHeap.Reset();
         hr = pD3D12Enc->m_spD3D12VideoDevice->CreateVideoEncoderHeap(&heapDesc,
                                                                              IID_PPV_ARGS(pD3D12Enc->m_spVideoEncoderHeap.GetAddressOf()));
      }

      if (FAILED(hr)) {
         debug_printf("CreateVideoEncoderHeap failed with HR %x\n", (unsigned)hr);
         return false;
      }
   }

   // If on-the-fly reconfiguration happened without object recreation, set
   // D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_*_CHANGED reconfiguration flags in EncodeFrame

   // When driver workaround for rate control reconfig is active we cannot send to the driver the
   // D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_RATE_CONTROL_CHANGE since it's not actually reporting
   // support for setting it.
   if ((pD3D12Enc->driver_workarounds & d3d12_video_encoder_driver_workaround_rate_control_reconfig) == 0) {
      if (rateControlChanged &&
         ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
            D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RATE_CONTROL_RECONFIGURATION_AVAILABLE) !=
         0 /*checking if the flag it's actually set*/) &&
         (pD3D12Enc->m_fenceValue > 1) && (!reCreatedEncoder || !reCreatedEncoderHeap)) {
         pD3D12Enc->m_currentEncodeConfig.m_seqFlags |= D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_RATE_CONTROL_CHANGE;
      }
   }

   if (slicesChanged &&
       ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
         D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SUBREGION_LAYOUT_RECONFIGURATION_AVAILABLE) !=
        0 /*checking if the flag it's actually set*/) &&
       (pD3D12Enc->m_fenceValue > 1) && (!reCreatedEncoder || !reCreatedEncoderHeap)) {
      pD3D12Enc->m_currentEncodeConfig.m_seqFlags |= D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_SUBREGION_LAYOUT_CHANGE;
   }

   if (gopChanged &&
       ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
         D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SEQUENCE_GOP_RECONFIGURATION_AVAILABLE) !=
        0 /*checking if the flag it's actually set*/) &&
       (pD3D12Enc->m_fenceValue > 1) && (!reCreatedEncoder || !reCreatedEncoderHeap)) {
      pD3D12Enc->m_currentEncodeConfig.m_seqFlags |= D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_GOP_SEQUENCE_CHANGE;
   }

   if(irChanged)
      pD3D12Enc->m_currentEncodeConfig.m_seqFlags |= D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_REQUEST_INTRA_REFRESH;

   return true;
}

void
d3d12_video_encoder_create_reference_picture_manager(struct d3d12_video_encoder *pD3D12Enc, struct pipe_picture_desc *  picture)
{
   pD3D12Enc->m_upDPBManager.reset();
   pD3D12Enc->m_upBitstreamBuilder.reset();
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
#if VIDEO_CODEC_H264ENC
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         bool     fArrayOfTextures = ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                                 D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RECONSTRUCTED_FRAMES_REQUIRE_TEXTURE_ARRAYS) == 0);
         pD3D12Enc->m_upDPBManager = std::make_unique<d3d12_video_encoder_references_manager_h264>(fArrayOfTextures);
         pD3D12Enc->m_upBitstreamBuilder = std::make_unique<d3d12_video_bitstream_builder_h264>();
      } break;
#endif
#if VIDEO_CODEC_H265ENC
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         bool     fArrayOfTextures = ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                                 D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RECONSTRUCTED_FRAMES_REQUIRE_TEXTURE_ARRAYS) == 0);
         pD3D12Enc->m_upDPBManager = std::make_unique<d3d12_video_encoder_references_manager_hevc>(fArrayOfTextures);
         pD3D12Enc->m_upBitstreamBuilder = std::make_unique<d3d12_video_bitstream_builder_hevc>();
      } break;
#endif
#if VIDEO_CODEC_AV1ENC
      case PIPE_VIDEO_FORMAT_AV1:
      {
         bool hasInterFrames =
            (pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_AV1SequenceStructure.InterFramePeriod > 0) &&
            ((pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_AV1SequenceStructure.IntraDistance == 0) ||
             (pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_AV1SequenceStructure.InterFramePeriod <
              pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_AV1SequenceStructure.IntraDistance));

         pD3D12Enc->m_upDPBManager = std::make_unique<d3d12_video_encoder_references_manager_av1>(
            hasInterFrames,
            *pD3D12Enc->m_upDPBStorageManager
         );

         // We use packed headers and pist encode execution syntax for AV1
         pD3D12Enc->m_upBitstreamBuilder = std::make_unique<d3d12_video_bitstream_builder_av1>();
      } break;
#endif
      default:
      {
         UNREACHABLE("Unsupported pipe_video_format");
      } break;
   }
}

D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA
d3d12_video_encoder_get_current_slice_param_settings(struct d3d12_video_encoder *pD3D12Enc)
{
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA subregionData = {};
   if (pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode ==
             D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_FULL_FRAME) {
      return subregionData;
   }

   if (pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode ==
             D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_AUTO) {
      return subregionData;
   }

   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
#if VIDEO_CODEC_H264ENC
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
      subregionData.pSlicesPartition_H264 =
         &pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigDesc.m_SlicesPartition_H264;
      subregionData.DataSize = sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_SLICES);
      } break;
#endif
#if VIDEO_CODEC_H265ENC
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         subregionData.pSlicesPartition_HEVC =
            &pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigDesc.m_SlicesPartition_HEVC;
         subregionData.DataSize = sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_SLICES);
      } break;
#endif
#if VIDEO_CODEC_AV1ENC
      case PIPE_VIDEO_FORMAT_AV1:
      {
         subregionData.pTilesPartition_AV1 =
            &pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigDesc.m_TilesConfig_AV1.TilesPartition;
         subregionData.DataSize = sizeof(D3D12_VIDEO_ENCODER_AV1_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_TILES);
      } break;
#endif
      default:
      {
         UNREACHABLE("Unsupported pipe_video_format");
      } break;
   }

   return subregionData;
}


D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA
d3d12_video_encoder_get_current_picture_param_settings_legacy(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA curPicParamsData = {};
   switch (codec) {
#if VIDEO_CODEC_H264ENC
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         curPicParamsData.pH264PicData = &pD3D12Enc->m_currentEncodeConfig.m_encoderPicParamsDesc.m_H264PicData;
         curPicParamsData.DataSize     = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderPicParamsDesc.m_H264PicData);
      } break;
#endif
#if VIDEO_CODEC_H265ENC
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         // D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC2 is binary compatible with D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC
         curPicParamsData.pHEVCPicData  = reinterpret_cast<D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC*>(&pD3D12Enc->m_currentEncodeConfig.m_encoderPicParamsDesc.m_HEVCPicData);
         curPicParamsData.DataSize      = sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC);
      } break;
#endif
#if VIDEO_CODEC_AV1ENC
      case PIPE_VIDEO_FORMAT_AV1:
      {
         curPicParamsData.pAV1PicData = &pD3D12Enc->m_currentEncodeConfig.m_encoderPicParamsDesc.m_AV1PicData;
         curPicParamsData.DataSize     = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderPicParamsDesc.m_AV1PicData);
      } break;
#endif
      default:
      {
         UNREACHABLE("Unsupported pipe_video_format");
      } break;
   }
   return curPicParamsData;
}

D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA1
d3d12_video_encoder_get_current_picture_param_settings(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA1 curPicParamsData = {};
   switch (codec) {
#if VIDEO_CODEC_H264ENC
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         curPicParamsData.pH264PicData = &pD3D12Enc->m_currentEncodeConfig.m_encoderPicParamsDesc.m_H264PicData;
         curPicParamsData.DataSize     = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderPicParamsDesc.m_H264PicData);
      } break;
#endif
#if VIDEO_CODEC_H265ENC
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         curPicParamsData.pHEVCPicData  = &pD3D12Enc->m_currentEncodeConfig.m_encoderPicParamsDesc.m_HEVCPicData;
         curPicParamsData.DataSize      = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderPicParamsDesc.m_HEVCPicData);
      } break;
#endif
#if VIDEO_CODEC_AV1ENC
      case PIPE_VIDEO_FORMAT_AV1:
      {
         curPicParamsData.pAV1PicData = &pD3D12Enc->m_currentEncodeConfig.m_encoderPicParamsDesc.m_AV1PicData;
         curPicParamsData.DataSize     = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderPicParamsDesc.m_AV1PicData);
      } break;
#endif
      default:
      {
         UNREACHABLE("Unsupported pipe_video_format");
      } break;
   }
   return curPicParamsData;
}

D3D12_VIDEO_ENCODER_RATE_CONTROL
d3d12_video_encoder_get_current_rate_control_settings(struct d3d12_video_encoder *pD3D12Enc)
{
   D3D12_VIDEO_ENCODER_RATE_CONTROL curRateControlDesc = {};
   curRateControlDesc.Mode            = pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Mode;
   curRateControlDesc.Flags           = pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Flags;
   curRateControlDesc.TargetFrameRate = pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_FrameRate;

   if ((curRateControlDesc.Flags & D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_EXTENSION1_SUPPORT) != 0)
   {
      switch (pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Mode) {
         case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_ABSOLUTE_QP_MAP:
         {
            curRateControlDesc.ConfigParams.pConfiguration_CQP1 = nullptr;
            curRateControlDesc.ConfigParams.DataSize           = 0;
         } break;
         case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP:
         {
            curRateControlDesc.ConfigParams.pConfiguration_CQP1 =
               &pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Config.m_Configuration_CQP1;
            curRateControlDesc.ConfigParams.DataSize =
               sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Config.m_Configuration_CQP1);
         } break;
         case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR:
         {
            curRateControlDesc.ConfigParams.pConfiguration_CBR1 =
               &pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Config.m_Configuration_CBR1;
            curRateControlDesc.ConfigParams.DataSize =
               sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Config.m_Configuration_CBR1);
         } break;
         case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR:
         {
            curRateControlDesc.ConfigParams.pConfiguration_VBR1 =
               &pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Config.m_Configuration_VBR1;
            curRateControlDesc.ConfigParams.DataSize =
               sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Config.m_Configuration_VBR1);
         } break;
         case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR:
         {
            curRateControlDesc.ConfigParams.pConfiguration_QVBR1 =
               &pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Config.m_Configuration_QVBR1;
            curRateControlDesc.ConfigParams.DataSize =
               sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Config.m_Configuration_QVBR1);
         } break;
         default:
         {
            UNREACHABLE("Unsupported D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE");
         } break;
      }
   }
   else 
   {
      switch (pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Mode) {
         case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_ABSOLUTE_QP_MAP:
         {
            curRateControlDesc.ConfigParams.pConfiguration_CQP = nullptr;
            curRateControlDesc.ConfigParams.DataSize           = 0;
         } break;
         case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP:
         {
            curRateControlDesc.ConfigParams.pConfiguration_CQP =
               &pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Config.m_Configuration_CQP;
            curRateControlDesc.ConfigParams.DataSize =
               sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Config.m_Configuration_CQP);
         } break;
         case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR:
         {
            curRateControlDesc.ConfigParams.pConfiguration_CBR =
               &pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Config.m_Configuration_CBR;
            curRateControlDesc.ConfigParams.DataSize =
               sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Config.m_Configuration_CBR);
         } break;
         case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR:
         {
            curRateControlDesc.ConfigParams.pConfiguration_VBR =
               &pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Config.m_Configuration_VBR;
            curRateControlDesc.ConfigParams.DataSize =
               sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Config.m_Configuration_VBR);
         } break;
         case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR:
         {
            curRateControlDesc.ConfigParams.pConfiguration_QVBR =
               &pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Config.m_Configuration_QVBR;
            curRateControlDesc.ConfigParams.DataSize =
               sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Config.m_Configuration_QVBR);
         } break;
         default:
         {
            UNREACHABLE("Unsupported D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE");
         } break;
      }
   }

   return curRateControlDesc;
}

D3D12_VIDEO_ENCODER_LEVEL_SETTING
d3d12_video_encoder_get_current_level_desc(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
#if VIDEO_CODEC_H264ENC
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         D3D12_VIDEO_ENCODER_LEVEL_SETTING curLevelDesc = {};
         curLevelDesc.pH264LevelSetting = &pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_H264LevelSetting;
         curLevelDesc.DataSize = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_H264LevelSetting);
         return curLevelDesc;
      } break;
#endif
#if VIDEO_CODEC_H265ENC
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         D3D12_VIDEO_ENCODER_LEVEL_SETTING curLevelDesc = {};
         curLevelDesc.pHEVCLevelSetting = &pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_HEVCLevelSetting;
         curLevelDesc.DataSize = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_HEVCLevelSetting);
         return curLevelDesc;
      } break;
#endif
#if VIDEO_CODEC_AV1ENC
      case PIPE_VIDEO_FORMAT_AV1:
      {
         D3D12_VIDEO_ENCODER_LEVEL_SETTING curLevelDesc = {};
         curLevelDesc.pAV1LevelSetting = &pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_AV1LevelSetting;
         curLevelDesc.DataSize = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_AV1LevelSetting);
         return curLevelDesc;
      } break;
#endif
      default:
      {
         UNREACHABLE("Unsupported pipe_video_format");
      } break;
   }
}

void
d3d12_video_encoder_build_pre_encode_codec_headers(struct d3d12_video_encoder *pD3D12Enc,
                                                   bool &postEncodeHeadersNeeded,
                                                   uint64_t &preEncodeGeneratedHeadersByteSize,
                                                   std::vector<uint64_t> &pWrittenCodecUnitsSizes)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
#if VIDEO_CODEC_H264ENC
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         postEncodeHeadersNeeded = false;
         preEncodeGeneratedHeadersByteSize = d3d12_video_encoder_build_codec_headers_h264(pD3D12Enc, pWrittenCodecUnitsSizes);
      } break;
#endif
#if VIDEO_CODEC_H265ENC
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         postEncodeHeadersNeeded = false;
         preEncodeGeneratedHeadersByteSize = d3d12_video_encoder_build_codec_headers_hevc(pD3D12Enc, pWrittenCodecUnitsSizes);
      } break;
#endif
#if VIDEO_CODEC_AV1ENC
      case PIPE_VIDEO_FORMAT_AV1:
      { 
         pD3D12Enc->m_BitstreamHeadersBuffer.resize(0);
         postEncodeHeadersNeeded = true;
         preEncodeGeneratedHeadersByteSize = 0;
         pWrittenCodecUnitsSizes.clear();
      } break;
#endif
      default:
      {
         UNREACHABLE("Unsupported pipe_video_format");
      } break;
   }
}

D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE
d3d12_video_encoder_get_current_gop_desc(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
#if VIDEO_CODEC_H264ENC
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE curGOPDesc = {};
         curGOPDesc.pH264GroupOfPictures =
            &pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures;
         curGOPDesc.DataSize = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures);
         return curGOPDesc;
      } break;
#endif
#if VIDEO_CODEC_H265ENC
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE curGOPDesc = {};
         curGOPDesc.pHEVCGroupOfPictures =
            &pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_HEVCGroupOfPictures;
         curGOPDesc.DataSize = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_HEVCGroupOfPictures);
         return curGOPDesc;
      } break;
#endif
#if VIDEO_CODEC_AV1ENC
      case PIPE_VIDEO_FORMAT_AV1:
      {
         D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE curGOPDesc = {};
         curGOPDesc.pAV1SequenceStructure =
            &pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_AV1SequenceStructure;
         curGOPDesc.DataSize = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_AV1SequenceStructure);
         return curGOPDesc;
      } break;
#endif
      default:
      {
         UNREACHABLE("Unsupported pipe_video_format");
      } break;
   }
}

D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION
d3d12_video_encoder_get_current_codec_config_desc(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
#if VIDEO_CODEC_H264ENC
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION codecConfigDesc = {};
         codecConfigDesc.pH264Config = &pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificConfigDesc.m_H264Config;
         codecConfigDesc.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificConfigDesc.m_H264Config);
         return codecConfigDesc;
      } break;
#endif
#if VIDEO_CODEC_H265ENC
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION codecConfigDesc = {};
         codecConfigDesc.pHEVCConfig = &pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificConfigDesc.m_HEVCConfig;
         codecConfigDesc.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificConfigDesc.m_HEVCConfig);
         return codecConfigDesc;
      } break;
#endif
#if VIDEO_CODEC_AV1ENC
      case PIPE_VIDEO_FORMAT_AV1:
      {
         D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION codecConfigDesc = {};
         codecConfigDesc.pAV1Config = &pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificConfigDesc.m_AV1Config;
         codecConfigDesc.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificConfigDesc.m_AV1Config);
         return codecConfigDesc;
      } break;
#endif
      default:
      {
         UNREACHABLE("Unsupported pipe_video_format");
      } break;
   }
}

D3D12_VIDEO_ENCODER_CODEC
d3d12_video_encoder_get_current_codec(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
#if VIDEO_CODEC_H264ENC
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         return D3D12_VIDEO_ENCODER_CODEC_H264;
      } break;
#endif
#if VIDEO_CODEC_H265ENC
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         return D3D12_VIDEO_ENCODER_CODEC_HEVC;
      } break;
#endif
#if VIDEO_CODEC_AV1ENC
      case PIPE_VIDEO_FORMAT_AV1:
      {
         return D3D12_VIDEO_ENCODER_CODEC_AV1;
      } break;
#endif
      default:
      {
         UNREACHABLE("Unsupported pipe_video_format");
      } break;
   }
}

static void
d3d12_video_encoder_disable_rc_vbv_sizes(struct D3D12EncodeRateControlState & rcState)
{
   rcState.m_Flags &= ~D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_VBV_SIZES;
   switch (rcState.m_Mode) {
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR:
      {
         rcState.m_Config.m_Configuration_CBR.VBVCapacity = 0;
         rcState.m_Config.m_Configuration_CBR.InitialVBVFullness = 0;
      } break;
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR:
      {
         rcState.m_Config.m_Configuration_VBR.VBVCapacity = 0;
         rcState.m_Config.m_Configuration_VBR.InitialVBVFullness = 0;
      } break;
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR:
      {
         rcState.m_Config.m_Configuration_QVBR1.VBVCapacity = 0;
         rcState.m_Config.m_Configuration_QVBR1.InitialVBVFullness = 0;
      } break;
      default:
      {
         UNREACHABLE("Unsupported D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE for VBV Sizes");
      } break;
   }
}

static void
d3d12_video_encoder_disable_rc_maxframesize(struct D3D12EncodeRateControlState & rcState)
{
   rcState.m_Flags &= ~D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_MAX_FRAME_SIZE;
   rcState.max_frame_size = 0;
   switch (rcState.m_Mode) {
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR:
      {
         rcState.m_Config.m_Configuration_CBR.MaxFrameBitSize = 0;
      } break;
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR:
      {
         rcState.m_Config.m_Configuration_VBR.MaxFrameBitSize = 0;
      } break;
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR:
      {
         rcState.m_Config.m_Configuration_QVBR.MaxFrameBitSize = 0;
      } break;
      default:
      {
         UNREACHABLE("Unsupported D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE for VBV Sizes");
      } break;
   }
}

static bool
d3d12_video_encoder_is_qualitylevel_in_range(struct D3D12EncodeRateControlState & rcState, UINT MaxQualityVsSpeed)
{
   switch (rcState.m_Mode) {
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP:
      {
         return rcState.m_Config.m_Configuration_CQP1.QualityVsSpeed <= MaxQualityVsSpeed;
      } break;
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR:
      {
         return rcState.m_Config.m_Configuration_CBR1.QualityVsSpeed <= MaxQualityVsSpeed;
      } break;
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR:
      {
         return rcState.m_Config.m_Configuration_VBR1.QualityVsSpeed <= MaxQualityVsSpeed;
      } break;
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR:
      {
         return rcState.m_Config.m_Configuration_QVBR1.QualityVsSpeed <= MaxQualityVsSpeed;
      } break;
      default:
      {
         UNREACHABLE("Unsupported D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE");
      } break;
   }
}

static void
d3d12_video_encoder_disable_rc_qualitylevels(struct D3D12EncodeRateControlState & rcState)
{
   rcState.m_Flags &= ~D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_QUALITY_VS_SPEED;
   switch (rcState.m_Mode) {
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP:
      {
         rcState.m_Config.m_Configuration_CQP1.QualityVsSpeed = 0;
      } break;
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR:
      {
         rcState.m_Config.m_Configuration_CBR1.QualityVsSpeed = 0;
      } break;
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR:
      {
         rcState.m_Config.m_Configuration_VBR1.QualityVsSpeed = 0;
      } break;
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR:
      {
         rcState.m_Config.m_Configuration_QVBR1.QualityVsSpeed = 0;
      } break;
      default:
      {
         UNREACHABLE("Unsupported D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE");
      } break;
   }
}

static void
d3d12_video_encoder_disable_rc_deltaqp(struct D3D12EncodeRateControlState & rcState)
{
   rcState.m_Flags &= ~D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_DELTA_QP;
}

static void
d3d12_video_encoder_disable_rc_minmaxqp(struct D3D12EncodeRateControlState & rcState)
{
   rcState.m_Flags &= ~D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_QP_RANGE;
   switch (rcState.m_Mode) {
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR:
      {
         rcState.m_Config.m_Configuration_CBR.MinQP = 0;
         rcState.m_Config.m_Configuration_CBR.MaxQP = 0;
      } break;
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR:
      {
         rcState.m_Config.m_Configuration_VBR.MinQP = 0;
         rcState.m_Config.m_Configuration_VBR.MaxQP = 0;
      } break;
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR:
      {
         rcState.m_Config.m_Configuration_QVBR.MinQP = 0;
         rcState.m_Config.m_Configuration_QVBR.MaxQP = 0;
      } break;
      default:
      {
         UNREACHABLE("Unsupported D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE for VBV Sizes");
      } break;
   }
}

static bool d3d12_video_encoder_is_move_regions_feature_enabled(struct d3d12_video_encoder* pD3D12Enc, D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE mapSource)
{
   if (pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapSource != mapSource)
   {
      return false;
   }

   if (mapSource == D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_CPU_BUFFER)
   {
      return pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.RectsInfo.NumMoveRegions > 0;
   }
   else if (mapSource == D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_GPU_TEXTURE)
   {
      return pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.NumHintsPerPixel > 0;
   }
   return false;
}

static bool d3d12_video_encoder_is_dirty_regions_feature_enabled(struct d3d12_video_encoder* pD3D12Enc, D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE mapSource)
{
   if (pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapSource != mapSource)
   {
      return false;
   }

   if (mapSource == D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_CPU_BUFFER)
   {
      return pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.RectsInfo.FullFrameIdentical ||
            (pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.RectsInfo.NumDirtyRects > 0);
   }
   else if (mapSource == D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_GPU_TEXTURE)
   {
      return pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.FullFrameIdentical ||
            (pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.InputMap != NULL);
   }
   return false;
}

static void
d3d12_video_encoder_disable_rc_extended1_to_legacy(struct D3D12EncodeRateControlState & rcState)
{
   rcState.m_Flags &= ~D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_EXTENSION1_SUPPORT;
   // Also remove features that require extension1 enabled (eg. quality levels)
   rcState.m_Flags &= ~D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_QUALITY_VS_SPEED;
   // rcState.m_Configuration_XXX and m_Configuration_XXX1 are unions, can be aliased
   // as the m_Configuration_XXX1 extensions are binary backcompat with m_Configuration_XXX
}

///
/// Call d3d12_video_encoder_query_d3d12_driver_caps and see if any optional feature requested
/// is not supported, disable it, query again until finding a negotiated cap/feature set
/// Note that with fallbacks, the upper layer will not get exactly the encoding seetings they requested
/// but for very particular settings it's better to continue with warnings than failing the whole encoding process
///
bool d3d12_video_encoder_negotiate_requested_features_and_d3d12_driver_caps(struct d3d12_video_encoder *pD3D12Enc, D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT2 &capEncoderSupportData) {

   ///
   /// Check for general support
   /// Check for validation errors (some drivers return general support but also validation errors anyways, work around for those unexpected cases)
   ///

   bool configSupported = d3d12_video_encoder_query_d3d12_driver_caps(pD3D12Enc, /*inout*/ capEncoderSupportData)
    && (((capEncoderSupportData.SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_GENERAL_SUPPORT_OK) != 0)
                        && (capEncoderSupportData.ValidationFlags == D3D12_VIDEO_ENCODER_VALIDATION_FLAG_NONE));

   ///
   /// If D3D12_FEATURE_VIDEO_ENCODER_SUPPORT is not supported, try falling back to unsetting optional features and check for caps again
   ///   

   if (!configSupported) {
      debug_printf("[d3d12_video_encoder] WARNING: D3D12_FEATURE_VIDEO_ENCODER_SUPPORT is not supported, trying fallback to unsetting optional features\n");

      bool isRequestingVBVSizesSupported = ((capEncoderSupportData.SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RATE_CONTROL_VBV_SIZE_CONFIG_AVAILABLE) != 0);
      bool isClientRequestingVBVSizes = ((pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Flags & D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_VBV_SIZES) != 0);
      
      if(isClientRequestingVBVSizes && !isRequestingVBVSizesSupported) {
         debug_printf("[d3d12_video_encoder] WARNING: Requested D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_VBV_SIZES with VBVCapacity and InitialVBVFullness is not supported, will continue encoding unsetting this feature as fallback.\n");
         d3d12_video_encoder_disable_rc_vbv_sizes(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex]);
      }

      bool isRequestingPeakFrameSizeSupported = ((capEncoderSupportData.SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RATE_CONTROL_MAX_FRAME_SIZE_AVAILABLE) != 0);
      bool isClientRequestingPeakFrameSize = ((pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Flags & D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_MAX_FRAME_SIZE) != 0);

      if(isClientRequestingPeakFrameSize && !isRequestingPeakFrameSizeSupported) {
         debug_printf("[d3d12_video_encoder] WARNING: Requested D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_MAX_FRAME_SIZE with MaxFrameBitSize but the feature is not supported, will continue encoding unsetting this feature as fallback.\n");
         d3d12_video_encoder_disable_rc_maxframesize(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex]);
      }

      bool isRequestingQPRangesSupported = ((capEncoderSupportData.SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RATE_CONTROL_ADJUSTABLE_QP_RANGE_AVAILABLE) != 0);
      bool isClientRequestingQPRanges = ((pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Flags & D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_QP_RANGE) != 0);

      if(isClientRequestingQPRanges && !isRequestingQPRangesSupported) {
         debug_printf("[d3d12_video_encoder] WARNING: Requested D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_QP_RANGE with QPMin QPMax but the feature is not supported, will continue encoding unsetting this feature as fallback.\n");
         d3d12_video_encoder_disable_rc_minmaxqp(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex]);
      }

      bool isRequestingDeltaQPSupported = ((capEncoderSupportData.SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RATE_CONTROL_DELTA_QP_AVAILABLE) != 0);
      bool isClientRequestingDeltaQP = ((pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Flags & D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_DELTA_QP) != 0);

      if(isClientRequestingDeltaQP && !isRequestingDeltaQPSupported) {
         debug_printf("[d3d12_video_encoder] WARNING: Requested D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_DELTA_QP but the feature is not supported, will continue encoding unsetting this feature as fallback.\n");
         d3d12_video_encoder_disable_rc_deltaqp(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex]);
      }

      bool isRequestingExtended1RCSupported = ((capEncoderSupportData.SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RATE_CONTROL_EXTENSION1_SUPPORT) != 0);
      bool isClientRequestingExtended1RC = ((pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Flags & D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_EXTENSION1_SUPPORT) != 0);

      if(isClientRequestingExtended1RC && !isRequestingExtended1RCSupported) {
         debug_printf("[d3d12_video_encoder] WARNING: Requested D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_EXTENSION1_SUPPORT but the feature is not supported, will continue encoding unsetting this feature and dependent features as fallback.\n");
         d3d12_video_encoder_disable_rc_extended1_to_legacy(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex]);
      }

      /* d3d12_video_encoder_disable_rc_extended1_to_legacy may change m_Flags */
      if ((pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Flags & D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_EXTENSION1_SUPPORT) != 0)
      { // Quality levels also requires D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_EXTENSION1_SUPPORT
         bool isRequestingQualityLevelsSupported = ((capEncoderSupportData.SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RATE_CONTROL_QUALITY_VS_SPEED_AVAILABLE) != 0);
         bool isClientRequestingQualityLevels = ((pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Flags & D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_QUALITY_VS_SPEED) != 0);

         if (isClientRequestingQualityLevels)
         {
            if (!isRequestingQualityLevelsSupported) {
               debug_printf("[d3d12_video_encoder] WARNING: Requested D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_QUALITY_VS_SPEED but the feature is not supported, will continue encoding unsetting this feature as fallback.\n");
               d3d12_video_encoder_disable_rc_qualitylevels(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex]);
            } else if (!d3d12_video_encoder_is_qualitylevel_in_range(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex], capEncoderSupportData.MaxQualityVsSpeed)) {
               debug_printf("[d3d12_video_encoder] WARNING: Requested D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_QUALITY_VS_SPEED but the value is out of supported range, will continue encoding unsetting this feature as fallback.\n");
               d3d12_video_encoder_disable_rc_qualitylevels(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex]);
            }
         }
      }

      /* Try fallback for multi-slice/tile not supported with single subregion mode */
      if ((capEncoderSupportData.ValidationFlags & D3D12_VIDEO_ENCODER_VALIDATION_FLAG_SUBREGION_LAYOUT_MODE_NOT_SUPPORTED) != 0) {
         pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode = D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_FULL_FRAME;
         debug_printf("[d3d12_video_encoder] WARNING: Requested slice/tile mode not supported by driver, will continue encoding with single subregion encoding.\n");
      }

      ///
      /// Try fallback configuration
      ///
      configSupported = d3d12_video_encoder_query_d3d12_driver_caps(pD3D12Enc, /*inout*/ capEncoderSupportData)
         && (((capEncoderSupportData.SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_GENERAL_SUPPORT_OK) != 0)
                        && (capEncoderSupportData.ValidationFlags == D3D12_VIDEO_ENCODER_VALIDATION_FLAG_NONE));
   }

   if (pD3D12Enc->m_currentEncodeConfig.m_IntraRefresh.IntraRefreshDuration >
      pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps.MaxIntraRefreshFrameDuration)
   {
      debug_printf("[d3d12_video_encoder] Desired duration of intrarefresh %d is not supported (higher than max "
                  "reported IR duration %d in query caps) for current resolution.\n",
                  pD3D12Enc->m_currentEncodeConfig.m_IntraRefresh.IntraRefreshDuration,
                  pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps.MaxIntraRefreshFrameDuration);
      capEncoderSupportData.ValidationFlags |= D3D12_VIDEO_ENCODER_VALIDATION_FLAG_INTRA_REFRESH_MODE_NOT_SUPPORTED;
      configSupported = false;
   }

   if(!configSupported) {
      debug_printf("[d3d12_video_encoder] Cap negotiation failed, see more details below:\n");
      
      if ((capEncoderSupportData.ValidationFlags & D3D12_VIDEO_ENCODER_VALIDATION_FLAG_CODEC_NOT_SUPPORTED) != 0) {
         debug_printf("[d3d12_video_encoder] Requested codec is not supported\n");
      }

      if ((capEncoderSupportData.ValidationFlags &
         D3D12_VIDEO_ENCODER_VALIDATION_FLAG_RESOLUTION_NOT_SUPPORTED_IN_LIST) != 0) {
         debug_printf("[d3d12_video_encoder] Requested resolution is not supported\n");
      }

      if ((capEncoderSupportData.ValidationFlags &
         D3D12_VIDEO_ENCODER_VALIDATION_FLAG_RATE_CONTROL_CONFIGURATION_NOT_SUPPORTED) != 0) {
         debug_printf("[d3d12_video_encoder] Requested bitrate or rc config is not supported\n");
      }

      if ((capEncoderSupportData.ValidationFlags &
         D3D12_VIDEO_ENCODER_VALIDATION_FLAG_CODEC_CONFIGURATION_NOT_SUPPORTED) != 0) {
         debug_printf("[d3d12_video_encoder] Requested codec config is not supported\n");
      }

      if ((capEncoderSupportData.ValidationFlags &
         D3D12_VIDEO_ENCODER_VALIDATION_FLAG_RATE_CONTROL_MODE_NOT_SUPPORTED) != 0) {
         debug_printf("[d3d12_video_encoder] Requested rate control mode is not supported\n");
      }

      if ((capEncoderSupportData.ValidationFlags &
         D3D12_VIDEO_ENCODER_VALIDATION_FLAG_INTRA_REFRESH_MODE_NOT_SUPPORTED) != 0) {
         debug_printf("[d3d12_video_encoder] Requested intra refresh config is not supported\n");
      }

      if ((capEncoderSupportData.ValidationFlags &
         D3D12_VIDEO_ENCODER_VALIDATION_FLAG_SUBREGION_LAYOUT_MODE_NOT_SUPPORTED) != 0) {
         debug_printf("[d3d12_video_encoder] Requested subregion layout mode is not supported\n");
      }

      if ((capEncoderSupportData.ValidationFlags & D3D12_VIDEO_ENCODER_VALIDATION_FLAG_INPUT_FORMAT_NOT_SUPPORTED) !=
         0) {
         debug_printf("[d3d12_video_encoder] Requested input dxgi format is not supported\n");
      }
      if ((capEncoderSupportData.ValidationFlags & D3D12_VIDEO_ENCODER_VALIDATION_FLAG_DIRTY_REGIONS_NOT_SUPPORTED ) !=
         0) {
         debug_printf("[d3d12_video_encoder] Requested input dirty regions is not supported\n");
      }

      if ((capEncoderSupportData.ValidationFlags & D3D12_VIDEO_ENCODER_VALIDATION_FLAG_FRAME_ANALYSIS_NOT_SUPPORTED ) !=
         0) {
         debug_printf("[d3d12_video_encoder] Requested two pass encode is not supported\n");
      }
   }

   if (!pD3D12Enc->m_prevFrameEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex]
            .CompareEqual(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex])) {
      pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |= d3d12_video_encoder_config_dirty_flag_rate_control;
   }

   return configSupported;
}

bool d3d12_video_encoder_query_d3d12_driver_caps(struct d3d12_video_encoder *pD3D12Enc, D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT2 &capEncoderSupportData) {
   capEncoderSupportData.NodeIndex                                = pD3D12Enc->m_NodeIndex;
   capEncoderSupportData.Codec                                    = d3d12_video_encoder_get_current_codec(pD3D12Enc);
   capEncoderSupportData.InputFormat            = pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format;
   capEncoderSupportData.RateControl            = d3d12_video_encoder_get_current_rate_control_settings(pD3D12Enc);
   capEncoderSupportData.IntraRefresh           = pD3D12Enc->m_currentEncodeConfig.m_IntraRefresh.Mode;
   capEncoderSupportData.SubregionFrameEncoding = pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode;
   capEncoderSupportData.ResolutionsListCount   = 1;
   capEncoderSupportData.pResolutionList        = &pD3D12Enc->m_currentEncodeConfig.m_currentResolution;
   capEncoderSupportData.CodecGopSequence       = d3d12_video_encoder_get_current_gop_desc(pD3D12Enc);
   capEncoderSupportData.MaxReferenceFramesInDPB =
      std::max(2u, d3d12_video_encoder_get_current_max_dpb_capacity(pD3D12Enc)) - 1u; // we only want the number of references (not the current pic slot too)
   capEncoderSupportData.CodecConfiguration = d3d12_video_encoder_get_current_codec_config_desc(pD3D12Enc);

   // Set dirty regions input info to cap
   capEncoderSupportData.DirtyRegions.MapSource = pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapSource;
   capEncoderSupportData.DirtyRegions.Enabled = d3d12_video_encoder_is_dirty_regions_feature_enabled(pD3D12Enc, pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapSource);
   if (capEncoderSupportData.DirtyRegions.Enabled)
   {
      capEncoderSupportData.DirtyRegions.MapValuesType = (capEncoderSupportData.DirtyRegions.MapSource == D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_CPU_BUFFER) ?
                                                            pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.RectsInfo.MapValuesType :
                                                            pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.MapValuesType;
   }

   d3d12_video_encoder_is_gpu_qmap_input_feature_enabled(pD3D12Enc, /*output param*/ capEncoderSupportData.QPMap.Enabled, /*output param*/ capEncoderSupportData.QPMap.MapSource);

   capEncoderSupportData.MotionSearch.MapSource = pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapSource;
   capEncoderSupportData.MotionSearch.Enabled = d3d12_video_encoder_is_move_regions_feature_enabled(pD3D12Enc, pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapSource);
   if (capEncoderSupportData.MotionSearch.Enabled)
   {
      capEncoderSupportData.MotionSearch.MotionSearchMode = D3D12_VIDEO_ENCODER_FRAME_MOTION_SEARCH_MODE_FULL_SEARCH;
      capEncoderSupportData.MotionSearch.BidirectionalRefFrameEnabled = TRUE;
   }

   capEncoderSupportData.FrameAnalysis.Enabled = pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.AppRequested;
   if (capEncoderSupportData.FrameAnalysis.Enabled)
   {
      capEncoderSupportData.FrameAnalysis.Pow2DownscaleFactor = pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.Pow2DownscaleFactor;
   }

   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
#if VIDEO_CODEC_H264ENC
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         capEncoderSupportData.SuggestedProfile.pH264Profile =
            &pD3D12Enc->m_currentEncodeCapabilities.m_encoderSuggestedProfileDesc.m_H264Profile;
         capEncoderSupportData.SuggestedProfile.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeCapabilities.m_encoderSuggestedProfileDesc.m_H264Profile);
         capEncoderSupportData.SuggestedLevel.pH264LevelSetting =
            &pD3D12Enc->m_currentEncodeCapabilities.m_encoderLevelSuggestedDesc.m_H264LevelSetting;
         capEncoderSupportData.SuggestedLevel.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeCapabilities.m_encoderLevelSuggestedDesc.m_H264LevelSetting);
      } break;
#endif
#if VIDEO_CODEC_H265ENC
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         capEncoderSupportData.SuggestedProfile.pHEVCProfile =
            &pD3D12Enc->m_currentEncodeCapabilities.m_encoderSuggestedProfileDesc.m_HEVCProfile;
         capEncoderSupportData.SuggestedProfile.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeCapabilities.m_encoderSuggestedProfileDesc.m_HEVCProfile);
         capEncoderSupportData.SuggestedLevel.pHEVCLevelSetting =
            &pD3D12Enc->m_currentEncodeCapabilities.m_encoderLevelSuggestedDesc.m_HEVCLevelSetting;
         capEncoderSupportData.SuggestedLevel.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeCapabilities.m_encoderLevelSuggestedDesc.m_HEVCLevelSetting);
      } break;
#endif
#if VIDEO_CODEC_AV1ENC
      case PIPE_VIDEO_FORMAT_AV1:
      {
         capEncoderSupportData.SuggestedProfile.pAV1Profile =
            &pD3D12Enc->m_currentEncodeCapabilities.m_encoderSuggestedProfileDesc.m_AV1Profile;
         capEncoderSupportData.SuggestedProfile.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeCapabilities.m_encoderSuggestedProfileDesc.m_AV1Profile);
         capEncoderSupportData.SuggestedLevel.pAV1LevelSetting =
            &pD3D12Enc->m_currentEncodeCapabilities.m_encoderLevelSuggestedDesc.m_AV1LevelSetting;
         capEncoderSupportData.SuggestedLevel.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeCapabilities.m_encoderLevelSuggestedDesc.m_AV1LevelSetting);
      } break;
#endif
      default:
      {
         UNREACHABLE("Unsupported pipe_video_format");
      } break;
   }

   // prepare inout storage for the resolution dependent result.
   capEncoderSupportData.pResolutionDependentSupport =
      &pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps;
   
   capEncoderSupportData.SubregionFrameEncodingData = d3d12_video_encoder_get_current_slice_param_settings(pD3D12Enc);
   HRESULT hr = pD3D12Enc->m_spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_SUPPORT2,
                                                                     &capEncoderSupportData,
                                                                     sizeof(capEncoderSupportData));

   if (FAILED(hr)) {
      debug_printf("CheckFeatureSupport D3D12_FEATURE_VIDEO_ENCODER_SUPPORT2 failed with HR %x\n", (unsigned)hr);
      debug_printf("Falling back to check previous query version D3D12_FEATURE_VIDEO_ENCODER_SUPPORT1...\n");

      // D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT2 extends D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT1
      // in a binary compatible way, so just cast it and try with the older query D3D12_FEATURE_VIDEO_ENCODER_SUPPORT1
      D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT1 * casted_down_cap_data = reinterpret_cast<D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT1*>(&capEncoderSupportData);
      hr = pD3D12Enc->m_spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_SUPPORT1,
                                                                casted_down_cap_data,
                                                                sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT1));
   }

   if (FAILED(hr)) {
      debug_printf("CheckFeatureSupport D3D12_FEATURE_VIDEO_ENCODER_SUPPORT1 failed with HR %x\n", (unsigned)hr);
      debug_printf("Falling back to check previous query version D3D12_FEATURE_VIDEO_ENCODER_SUPPORT...\n");

      // D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT1 extends D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT
      // in a binary compatible way, so just cast it and try with the older query D3D12_FEATURE_VIDEO_ENCODER_SUPPORT
      D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT * casted_down_cap_data = reinterpret_cast<D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT*>(&capEncoderSupportData);

      //
      // Remove legacy query parameters for features not supported in older OS when using older OS support query
      // since the D3D12 older runtime will not recognize the new flags and structures
      // Update both encoder current config and re-generate support cap rate control input
      //
      pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc
         [pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Flags &= ~D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_EXTENSION1_SUPPORT;

      d3d12_video_encoder_disable_rc_qualitylevels(
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex]);

      capEncoderSupportData.RateControl = d3d12_video_encoder_get_current_rate_control_settings(pD3D12Enc);

      hr = pD3D12Enc->m_spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_SUPPORT,
                                                                         casted_down_cap_data,
                                                                         sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT));
      if (FAILED(hr)) {
         debug_printf("CheckFeatureSupport D3D12_FEATURE_VIDEO_ENCODER_SUPPORT failed with HR %x\n", (unsigned)hr);
         return false;
      }
   }

   // Workaround for drivers supporting rate control reconfiguration but not reporting it
   // and having issues with encoder state/heap objects recreation
   if (pD3D12Enc->m_pD3D12Screen->vendor_id == 0x8086 /* HW_VENDOR_INTEL */) {
      // If IHV driver doesn't report reconfiguration, force doing the reconfiguration without object recreation
      if ((capEncoderSupportData.SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RATE_CONTROL_RECONFIGURATION_AVAILABLE) == 0) {
         pD3D12Enc->driver_workarounds |= d3d12_video_encoder_driver_workaround_rate_control_reconfig;
         capEncoderSupportData.SupportFlags |= D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RATE_CONTROL_RECONFIGURATION_AVAILABLE;
      }
   }

   pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags    = capEncoderSupportData.SupportFlags;
   pD3D12Enc->m_currentEncodeCapabilities.m_ValidationFlags = capEncoderSupportData.ValidationFlags;

   if ((capEncoderSupportData.DirtyRegions.MapSource == D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_GPU_TEXTURE) &&
       (capEncoderSupportData.DirtyRegions.Enabled))
   {
      // Query specifics of staging resource for dirty regions
      pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.capInputLayoutDirtyRegion =
      {
         // UINT NodeIndex;
         0u,
         // D3D12_VIDEO_ENCODER_INPUT_MAP_SESSION_INFO SessionInfo;
         {
            capEncoderSupportData.Codec,
            d3d12_video_encoder_get_current_profile_desc(pD3D12Enc),
            d3d12_video_encoder_get_current_level_desc(pD3D12Enc),
            pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
            // D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC
            pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
            d3d12_video_encoder_get_current_codec_config_desc(pD3D12Enc),
            capEncoderSupportData.SubregionFrameEncoding,
            capEncoderSupportData.SubregionFrameEncodingData
         },
         // D3D12_VIDEO_ENCODER_INPUT_MAP_TYPE MapType;
         D3D12_VIDEO_ENCODER_INPUT_MAP_TYPE_DIRTY_REGIONS,
         // BOOL IsSupported;
         FALSE,
         // UINT64 MaxResolvedBufferAllocationSize;
         0u,
      };

      hr = pD3D12Enc->m_spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT,
                                                                &pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.capInputLayoutDirtyRegion,
                                                                sizeof(pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.capInputLayoutDirtyRegion));

      if (FAILED(hr)) {
         debug_printf("CheckFeatureSupport D3D12_FEATURE_VIDEO_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT failed with HR %x\n", (unsigned)hr);
         return false;
      }
   }

   if ((capEncoderSupportData.QPMap.MapSource == D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_GPU_TEXTURE) &&
       (capEncoderSupportData.QPMap.Enabled))
   {
      // Query specifics of staging resource for QPMap regions
      pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.GPUInput.capInputLayoutQPMap =
      {
         // UINT NodeIndex;
         0u,
         // D3D12_VIDEO_ENCODER_INPUT_MAP_SESSION_INFO SessionInfo;
         {
            capEncoderSupportData.Codec,
            d3d12_video_encoder_get_current_profile_desc(pD3D12Enc),
            d3d12_video_encoder_get_current_level_desc(pD3D12Enc),
            pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
            // D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC
            pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
            d3d12_video_encoder_get_current_codec_config_desc(pD3D12Enc),
            capEncoderSupportData.SubregionFrameEncoding,
            capEncoderSupportData.SubregionFrameEncodingData
         },
         // D3D12_VIDEO_ENCODER_INPUT_MAP_TYPE MapType;
         D3D12_VIDEO_ENCODER_INPUT_MAP_TYPE_QUANTIZATION_MATRIX,
         // BOOL IsSupported;
         FALSE,
         // UINT64 MaxResolvedBufferAllocationSize;
         0u,
      };

      hr = pD3D12Enc->m_spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT,
                                                                &pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.GPUInput.capInputLayoutQPMap,
                                                                sizeof(pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.GPUInput.capInputLayoutQPMap));

      if (FAILED(hr)) {
         debug_printf("CheckFeatureSupport D3D12_FEATURE_VIDEO_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT failed with HR %x\n", (unsigned)hr);
         return false;
      }
   }

   if ((capEncoderSupportData.MotionSearch.MapSource == D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_GPU_TEXTURE) &&
       (capEncoderSupportData.MotionSearch.Enabled))
   {
      // Query specifics of staging resource for move regions
      pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.capInputLayoutMotionVectors =
      {
         // UINT NodeIndex;
         0u,
         // D3D12_VIDEO_ENCODER_INPUT_MAP_SESSION_INFO SessionInfo;
         {
            capEncoderSupportData.Codec,
            d3d12_video_encoder_get_current_profile_desc(pD3D12Enc),
            d3d12_video_encoder_get_current_level_desc(pD3D12Enc),
            pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
            // D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC
            pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
            d3d12_video_encoder_get_current_codec_config_desc(pD3D12Enc),
            capEncoderSupportData.SubregionFrameEncoding,
            capEncoderSupportData.SubregionFrameEncodingData
         },
         // D3D12_VIDEO_ENCODER_INPUT_MAP_TYPE MapType;
         D3D12_VIDEO_ENCODER_INPUT_MAP_TYPE_MOTION_VECTORS,
         // BOOL IsSupported;
         FALSE,
         // UINT64 MaxResolvedBufferAllocationSize;
         0u,
      };

      hr = pD3D12Enc->m_spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT,
                                                                &pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.capInputLayoutMotionVectors,
                                                                sizeof(pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.capInputLayoutMotionVectors));

      if (FAILED(hr)) {
         debug_printf("CheckFeatureSupport D3D12_FEATURE_VIDEO_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT failed with HR %x\n", (unsigned)hr);
         return false;
      }
   }

   return true;
}

bool d3d12_video_encoder_check_subregion_mode_support(struct d3d12_video_encoder *pD3D12Enc,
                                    D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE requestedSlicesMode
   )
{
   D3D12_FEATURE_DATA_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE capDataSubregionLayout = { }; 
   capDataSubregionLayout.NodeIndex = pD3D12Enc->m_NodeIndex;
   capDataSubregionLayout.Codec = d3d12_video_encoder_get_current_codec(pD3D12Enc);
   capDataSubregionLayout.Profile = d3d12_video_encoder_get_current_profile_desc(pD3D12Enc);
   capDataSubregionLayout.Level = d3d12_video_encoder_get_current_level_desc(pD3D12Enc);
   capDataSubregionLayout.SubregionMode = requestedSlicesMode;
   HRESULT hr = pD3D12Enc->m_spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE, &capDataSubregionLayout, sizeof(capDataSubregionLayout));
   if (FAILED(hr)) {
      debug_printf("CheckFeatureSupport failed with HR %x\n", (unsigned)hr);
      return false;
   }
   return capDataSubregionLayout.IsSupported;
}

D3D12_VIDEO_ENCODER_PROFILE_DESC
d3d12_video_encoder_get_current_profile_desc(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
#if VIDEO_CODEC_H264ENC
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         D3D12_VIDEO_ENCODER_PROFILE_DESC curProfDesc = {};
         curProfDesc.pH264Profile = &pD3D12Enc->m_currentEncodeConfig.m_encoderProfileDesc.m_H264Profile;
         curProfDesc.DataSize     = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderProfileDesc.m_H264Profile);
         return curProfDesc;
      } break;
#endif
#if VIDEO_CODEC_H265ENC
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         D3D12_VIDEO_ENCODER_PROFILE_DESC curProfDesc = {};
         curProfDesc.pHEVCProfile = &pD3D12Enc->m_currentEncodeConfig.m_encoderProfileDesc.m_HEVCProfile;
         curProfDesc.DataSize     = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderProfileDesc.m_HEVCProfile);
         return curProfDesc;
      } break;
#endif
#if VIDEO_CODEC_AV1ENC
      case PIPE_VIDEO_FORMAT_AV1:
      {
         D3D12_VIDEO_ENCODER_PROFILE_DESC curProfDesc = {};
         curProfDesc.pAV1Profile = &pD3D12Enc->m_currentEncodeConfig.m_encoderProfileDesc.m_AV1Profile;
         curProfDesc.DataSize     = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderProfileDesc.m_AV1Profile);
         return curProfDesc;
      } break;
#endif
      default:
      {
         UNREACHABLE("Unsupported pipe_video_format");
      } break;
   }
}

uint32_t
d3d12_video_encoder_get_current_max_dpb_capacity(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
#if VIDEO_CODEC_H264ENC
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         return PIPE_H264_MAX_REFERENCES + 1u /* current frame reconstructed picture */;
      } break;
#endif
#if VIDEO_CODEC_H265ENC
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         return PIPE_H265_MAX_REFERENCES + 1u /* current frame reconstructed picture */;
      } break;
#endif
#if VIDEO_CODEC_AV1ENC
      case PIPE_VIDEO_FORMAT_AV1:
      {
         return PIPE_AV1_MAX_REFERENCES + 1u /* current frame reconstructed picture */;
      } break;
#endif
      default:
      {
         UNREACHABLE("Unsupported pipe_video_format");
      } break;
   }
}

void
d3d12_video_encoder_update_output_stats_resources(struct d3d12_video_encoder *pD3D12Enc,
                                                  struct pipe_resource* qpmap,
                                                  struct pipe_resource* satdmap,
                                                  struct pipe_resource* rcbitsmap,
                                                  struct pipe_resource* psnrmap)
{
   pD3D12Enc->m_currentEncodeConfig.m_GPUQPStatsResource = d3d12_resource(qpmap);
   pD3D12Enc->m_currentEncodeConfig.m_GPUSATDStatsResource = d3d12_resource(satdmap);
   pD3D12Enc->m_currentEncodeConfig.m_GPURCBitAllocationStatsResource = d3d12_resource(rcbitsmap);
   pD3D12Enc->m_currentEncodeConfig.m_GPUPSNRAllocationStatsResource = d3d12_resource(psnrmap);
}

bool
d3d12_video_encoder_update_current_encoder_config_state(struct d3d12_video_encoder *pD3D12Enc,
                                                        const D3D12_VIDEO_SAMPLE& srcTextureDesc,
                                                        struct pipe_picture_desc *  picture)
{
   pD3D12Enc->m_prevFrameEncodeConfig = pD3D12Enc->m_currentEncodeConfig;

   bool bCodecUpdatesSuccess = false;
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
#if VIDEO_CODEC_H264ENC
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         d3d12_video_encoder_update_output_stats_resources(pD3D12Enc,
                                                           ((struct pipe_h264_enc_picture_desc *)picture)->gpu_stats_qp_map,
                                                           ((struct pipe_h264_enc_picture_desc *)picture)->gpu_stats_satd_map,
                                                           ((struct pipe_h264_enc_picture_desc *)picture)->gpu_stats_rc_bitallocation_map,
                                                           ((struct pipe_h264_enc_picture_desc *)picture)->gpu_stats_psnr);

         d3d12_video_encoder_update_move_rects(pD3D12Enc, ((struct pipe_h264_enc_picture_desc *)picture)->move_info);
         d3d12_video_encoder_update_dirty_rects(pD3D12Enc, ((struct pipe_h264_enc_picture_desc *)picture)->dirty_info);
         d3d12_video_encoder_update_qpmap_input(pD3D12Enc, ((struct pipe_h264_enc_picture_desc *)picture)->input_qpmap_info,
                                                           ((struct pipe_h264_enc_picture_desc *)picture)->roi,
                                                           ((struct pipe_h264_enc_picture_desc *)picture)->pic_ctrl.temporal_id);
         d3d12_video_encoder_update_two_pass_frame_settings(pD3D12Enc, codec, picture);
         d3d12_video_encoder_update_rate_control_saq(pD3D12Enc,
                                                     ((struct pipe_h264_enc_picture_desc *)picture)->rate_ctrl[((struct pipe_h264_enc_picture_desc *)picture)->pic_ctrl.temporal_id].spatial_adaptive_quantization_strength,
                                                     ((struct pipe_h264_enc_picture_desc *)picture)->pic_ctrl.temporal_id);
         // ...encoder_config_state_h264 calls encoder support cap, set any state before this call
         bCodecUpdatesSuccess = d3d12_video_encoder_update_current_encoder_config_state_h264(pD3D12Enc, srcTextureDesc, picture);
      } break;
#endif
#if VIDEO_CODEC_H265ENC
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         d3d12_video_encoder_update_output_stats_resources(pD3D12Enc,
                                                           ((struct pipe_h265_enc_picture_desc *)picture)->gpu_stats_qp_map,
                                                           ((struct pipe_h265_enc_picture_desc *)picture)->gpu_stats_satd_map,
                                                           ((struct pipe_h265_enc_picture_desc *)picture)->gpu_stats_rc_bitallocation_map,
                                                           ((struct pipe_h265_enc_picture_desc *)picture)->gpu_stats_psnr);

         d3d12_video_encoder_update_move_rects(pD3D12Enc, ((struct pipe_h265_enc_picture_desc *)picture)->move_info);
         d3d12_video_encoder_update_dirty_rects(pD3D12Enc, ((struct pipe_h265_enc_picture_desc *)picture)->dirty_info);
         d3d12_video_encoder_update_qpmap_input(pD3D12Enc, ((struct pipe_h265_enc_picture_desc *) picture)->input_qpmap_info,
                                                           ((struct pipe_h265_enc_picture_desc *)picture)->roi,
                                                           ((struct pipe_h265_enc_picture_desc *)picture)->pic.temporal_id);
         d3d12_video_encoder_update_two_pass_frame_settings(pD3D12Enc, codec, picture);
         d3d12_video_encoder_update_rate_control_saq(pD3D12Enc,
                                                    ((struct pipe_h265_enc_picture_desc *)picture)->rc[((struct pipe_h265_enc_picture_desc *)picture)->pic.temporal_id].spatial_adaptive_quantization_strength,
                                                    ((struct pipe_h265_enc_picture_desc *)picture)->pic.temporal_id);
         // ...encoder_config_state_hevc calls encoder support cap, set any state before this call
         bCodecUpdatesSuccess = d3d12_video_encoder_update_current_encoder_config_state_hevc(pD3D12Enc, srcTextureDesc, picture);
      } break;
#endif
#if VIDEO_CODEC_AV1ENC
      case PIPE_VIDEO_FORMAT_AV1:
      {
         d3d12_video_encoder_update_qpmap_input(pD3D12Enc, ((struct pipe_av1_enc_picture_desc *) picture)->input_qpmap_info,
                                                           ((struct pipe_av1_enc_picture_desc *)picture)->roi,
                                                           ((struct pipe_av1_enc_picture_desc *)picture)->temporal_id);
         // ...encoder_config_state_av1 calls encoder support cap, set any state before this call
         bCodecUpdatesSuccess = d3d12_video_encoder_update_current_encoder_config_state_av1(pD3D12Enc, srcTextureDesc, picture);
      } break;
#endif
      default:
      {
         UNREACHABLE("Unsupported pipe_video_format");
      } break;
   }

   // Set dirty region changes
   if (memcmp(&pD3D12Enc->m_prevFrameEncodeConfig.m_DirtyRectsDesc,
              &pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc,
              sizeof(pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc)) != 0) {
      pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |= d3d12_video_encoder_config_dirty_flag_dirty_regions;
   }

   return bCodecUpdatesSuccess;
}

bool
d3d12_video_encoder_create_command_objects(struct d3d12_video_encoder *pD3D12Enc)
{
   assert(pD3D12Enc->m_spD3D12VideoDevice);

   D3D12_COMMAND_QUEUE_DESC commandQueueDesc = { D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE };
   if (pD3D12Enc->m_pD3D12Screen->supports_dynamic_queue_priority)
      commandQueueDesc.Flags |= D3D12_COMMAND_QUEUE_FLAG_ALLOW_DYNAMIC_PRIORITY;
   HRESULT                  hr               = pD3D12Enc->m_pD3D12Screen->dev->CreateCommandQueue(
      &commandQueueDesc,
      IID_PPV_ARGS(pD3D12Enc->m_spEncodeCommandQueue.GetAddressOf()));
   if (FAILED(hr)) {
      debug_printf("[d3d12_video_encoder] d3d12_video_encoder_create_command_objects - Call to CreateCommandQueue "
                      "failed with HR %x\n",
                      (unsigned)hr);
      return false;
   }

   hr = pD3D12Enc->m_pD3D12Screen->dev->CreateCommandQueue(&commandQueueDesc,
      IID_PPV_ARGS(pD3D12Enc->m_spResolveCommandQueue.GetAddressOf()));
   if (FAILED(hr)) {
      debug_printf("[d3d12_video_encoder] d3d12_video_encoder_create_command_objects - Call to CreateCommandQueue "
                      "failed with HR %x\n",
                      (unsigned)hr);
      return false;
   }

   hr = pD3D12Enc->m_pD3D12Screen->dev->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&pD3D12Enc->m_spFence));
   if (FAILED(hr)) {
      debug_printf(
         "[d3d12_video_encoder] d3d12_video_encoder_create_command_objects - Call to CreateFence failed with HR %x\n",
         (unsigned)hr);
      return false;
   }

   hr = pD3D12Enc->m_pD3D12Screen->dev->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&pD3D12Enc->m_spResidencyFence));
   if (FAILED(hr)) {
      debug_printf(
         "[d3d12_video_encoder] d3d12_video_encoder_create_command_objects - Call to CreateFence failed with HR %x\n",
         (unsigned)hr);
      return false;
   }

   hr = pD3D12Enc->m_pD3D12Screen->dev->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&pD3D12Enc->m_spLastSliceFence));
   if (FAILED(hr)) {
      debug_printf(
         "[d3d12_video_encoder] d3d12_video_encoder_create_command_objects - Call to CreateFence failed with HR %x\n",
         (unsigned)hr);
      return false;
   }

   uint64_t CompletionFenceValue = pD3D12Enc->m_fenceValue;
   for (auto& inputResource : pD3D12Enc->m_inflightResourcesPool)
   {
      // Create associated command allocator for Encode, Resolve operations
      hr = pD3D12Enc->m_pD3D12Screen->dev->CreateCommandAllocator(
         D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE,
         IID_PPV_ARGS(inputResource.m_spCommandAllocator.GetAddressOf()));
      if (FAILED(hr)) {
         debug_printf("[d3d12_video_encoder] d3d12_video_encoder_create_command_objects - Call to "
                        "CreateCommandAllocator failed with HR %x\n",
                        (unsigned)hr);
         return false;
      }

      // Create associated command allocator for Resolve operations
      hr = pD3D12Enc->m_pD3D12Screen->dev->CreateCommandAllocator(
         D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE,
         IID_PPV_ARGS(inputResource.m_spResolveCommandAllocator.GetAddressOf()));
      if (FAILED(hr)) {
         debug_printf("[d3d12_video_encoder] d3d12_video_encoder_create_command_objects - Call to "
                        "CreateCommandAllocator failed with HR %x\n",
                        (unsigned)hr);
         return false;
      }

      // Initialize fence for the in flight resource pool slot
      inputResource.m_CompletionFence.reset(d3d12_create_fence_raw(pD3D12Enc->m_spFence.Get(), CompletionFenceValue));
      inputResource.m_CompletionFence.reset(d3d12_create_fence_raw(pD3D12Enc->m_spLastSliceFence.Get(), CompletionFenceValue));
      CompletionFenceValue++;
   }

   ComPtr<ID3D12Device4> spD3D12Device4;
   if (FAILED(pD3D12Enc->m_pD3D12Screen->dev->QueryInterface(
          IID_PPV_ARGS(spD3D12Device4.GetAddressOf())))) {
      debug_printf(
         "[d3d12_video_encoder] d3d12_video_encoder_create_encoder - D3D12 Device has no Video encode support\n");
      return false;
   }

   hr = spD3D12Device4->CreateCommandList1(0,
                        D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE,
                        D3D12_COMMAND_LIST_FLAG_NONE,
                        IID_PPV_ARGS(pD3D12Enc->m_spEncodeCommandList.GetAddressOf()));

   if (FAILED(hr)) {
      debug_printf("[d3d12_video_encoder] d3d12_video_encoder_create_command_objects - Call to CreateCommandList "
                      "failed with HR %x\n",
                      (unsigned)hr);
      return false;
   }

   // Cache ID3D12VideoEncodeCommandList4 interface for EncodeFrame1 optimization
   pD3D12Enc->m_spEncodeCommandList->QueryInterface(IID_PPV_ARGS(pD3D12Enc->m_spEncodeCommandList4.GetAddressOf()));

   hr = spD3D12Device4->CreateCommandList1(0,
                        D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE,
                        D3D12_COMMAND_LIST_FLAG_NONE,
                        IID_PPV_ARGS(pD3D12Enc->m_spResolveCommandList.GetAddressOf()));

   if (FAILED(hr)) {
      debug_printf("[d3d12_video_encoder] d3d12_video_encoder_create_command_objects - Call to CreateCommandList "
                      "failed with HR %x\n",
                      (unsigned)hr);
      return false;
   }

   // Cache ID3D12VideoEncodeCommandList4 interface for ResolveEncoderOutputMetadata1 optimization
   pD3D12Enc->m_spResolveCommandList->QueryInterface(IID_PPV_ARGS(pD3D12Enc->m_spResolveCommandList4.GetAddressOf()));

   return true;
}

// Helper function to calculate the max output bitstream size based on width, height, and format
// This function uses an approach based on common video formats and their typical compression ratios
inline
UINT d3d12_video_encoder_calculate_max_output_compressed_bitstream_size(
   UINT uiWidth,
   UINT uiHeight,
   DXGI_FORMAT format
)
{
   assert((uiHeight > 16) &&
         (uiWidth > 16) &&
         (format != DXGI_FORMAT_UNKNOWN));

   const UINT MIN_BUFFER_SIZE = 256 * 1024; // 256KB minimum buffer size
   const UINT MAX_BUFFER_SIZE = 20 * 1024 * 1024; // Maximum buffer size of 20MB
   const float EXPECTED_COMPRESSION_FACTOR = 2.0f; // Assume 50% of calculated size after compression of raw pixel sizes

   UINT alignedWidth = (uiWidth + 15) & ~15;
   UINT alignedHeight = (uiHeight + 15) & ~15;
   UINT bufferSize = 0;
   switch (format) {
      case DXGI_FORMAT_NV12:
         // NV12: Y plane (1 byte/pixel) + UV plane (1/2 byte/pixel) = 1.5 bytes/pixel
         bufferSize = alignedWidth * alignedHeight * 3 / 2;
         break;
      case DXGI_FORMAT_P010:
         // P010: Y plane (2 bytes/pixel) + UV plane (1 byte/pixel) = 3 bytes/pixel
         bufferSize = alignedWidth * alignedHeight * 3;
         break;
      case DXGI_FORMAT_AYUV:
         // AYUV: 4 bytes/pixel
         bufferSize = alignedWidth * alignedHeight * 4;
         break;
      default:
         // Fallback formula for other formats: assume 15 bits/pixel (1.875 bytes/pixel)
         bufferSize = (((alignedHeight) * (alignedWidth) * 15) >> 3);
         break;
   }

   // Apply EXPECTED_COMPRESSION_FACTOR constant (% of calculated size)
   bufferSize = static_cast<UINT>(std::ceil(bufferSize / EXPECTED_COMPRESSION_FACTOR));

   // Clamp buffer size between minimum and maximum limits
   bufferSize = std::max(MIN_BUFFER_SIZE, std::min(bufferSize, MAX_BUFFER_SIZE));

   return bufferSize;
}


struct pipe_video_codec *
d3d12_video_encoder_create_encoder(struct pipe_context *context, const struct pipe_video_codec *codec)
{
   struct d3d12_context *pD3D12Ctx = (struct d3d12_context *) context;

   ///
   /// Initialize d3d12_video_encoder
   ///

   // Not using new doesn't call ctor and the initializations in the class declaration are lost
   struct d3d12_video_encoder *pD3D12Enc = new d3d12_video_encoder;

   pD3D12Enc->m_MaxQueueAsyncDepth = pD3D12Ctx->max_video_encoding_async_depth;
   assert(pD3D12Enc->m_MaxQueueAsyncDepth > 0);
   pD3D12Enc->m_MaxMetadataBuffersCount = 2 * pD3D12Enc->m_MaxQueueAsyncDepth;

   pD3D12Enc->m_spEncodedFrameMetadata.resize(pD3D12Enc->m_MaxMetadataBuffersCount);
   pD3D12Enc->m_inflightResourcesPool.resize(pD3D12Enc->m_MaxQueueAsyncDepth);

   pD3D12Enc->base         = *codec;
   pD3D12Enc->m_screen     = context->screen;
   pD3D12Enc->base.context = context;
   pD3D12Enc->base.width   = codec->width;
   pD3D12Enc->base.height  = codec->height;
   pD3D12Enc->base.max_references  = codec->max_references;
   // Only fill methods that are supported by the d3d12 encoder, leaving null the rest (ie. encode_* / encode_macroblock)
   pD3D12Enc->base.destroy          = d3d12_video_encoder_destroy;
   pD3D12Enc->base.begin_frame      = d3d12_video_encoder_begin_frame;
   pD3D12Enc->base.encode_bitstream = d3d12_video_encoder_encode_bitstream;
   pD3D12Enc->base.end_frame        = d3d12_video_encoder_end_frame;
   pD3D12Enc->base.flush            = d3d12_video_encoder_flush;
   pD3D12Enc->base.get_encode_headers = d3d12_video_encoder_get_encode_headers;
   pD3D12Enc->base.get_feedback     = d3d12_video_encoder_get_feedback;
   pD3D12Enc->base.create_dpb_buffer = d3d12_video_create_dpb_buffer;
   pD3D12Enc->base.fence_wait       = d3d12_video_encoder_fence_wait;
   pD3D12Enc->base.destroy_fence = d3d12_video_destroy_fence;
   pD3D12Enc->base.encode_bitstream_sliced = d3d12_video_encoder_encode_bitstream_sliced;
   pD3D12Enc->base.get_slice_bitstream_data = d3d12_video_encoder_get_slice_bitstream_data;

   pD3D12Enc->m_pD3D12Screen       = d3d12_screen(pD3D12Ctx->base.screen);

   if (FAILED(pD3D12Enc->m_pD3D12Screen->dev->QueryInterface(
          IID_PPV_ARGS(pD3D12Enc->m_spD3D12VideoDevice.GetAddressOf())))) {
      debug_printf(
         "[d3d12_video_encoder] d3d12_video_encoder_create_encoder - D3D12 Device has no Video encode support\n");
      goto failed;
   }

   // Cache ID3D12VideoDevice4 interface
   pD3D12Enc->m_spD3D12VideoDevice->QueryInterface(
      IID_PPV_ARGS(pD3D12Enc->m_spD3D12VideoDevice4.GetAddressOf()));
   // Note: m_spD3D12VideoDevice4 may be nullptr if the interface is not supported

   pD3D12Enc->m_MaxOutputBitstreamSize = d3d12_video_encoder_calculate_max_output_compressed_bitstream_size(
      codec->width,
      codec->height,
      d3d12_convert_pipe_video_profile_to_dxgi_format(pD3D12Enc->base.profile)
   );

   debug_printf("[d3d12_video_encoder] d3d12_video_encoder_create_encoder - Calculated max output bitstream size: %u bytes (%u Kb, %u Mb) for %ux%u DXGI_FORMAT %u\n",
                pD3D12Enc->m_MaxOutputBitstreamSize,
                pD3D12Enc->m_MaxOutputBitstreamSize / 1024,
                pD3D12Enc->m_MaxOutputBitstreamSize / (1024 * 1024),
                codec->width,
                codec->height,
                static_cast<UINT>(d3d12_convert_pipe_video_profile_to_dxgi_format(pD3D12Enc->base.profile)));

   if (!d3d12_video_encoder_create_command_objects(pD3D12Enc)) {
      debug_printf("[d3d12_video_encoder] d3d12_video_encoder_create_encoder - Failure on "
                      "d3d12_video_encoder_create_command_objects\n");
      goto failed;
   }

   // Cache quality levels cap
   pD3D12Enc->max_quality_levels = context->screen->get_video_param(context->screen, codec->profile,
                                    codec->entrypoint,
                                    PIPE_VIDEO_CAP_ENC_QUALITY_LEVEL);

   // Cache texture array requirement for reconstructed frames for d3d12_video_create_dpb_buffer calls
   if (d3d12_video_encode_requires_texture_array_dpb(pD3D12Enc->m_pD3D12Screen, codec->profile))
      pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags |= D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RECONSTRUCTED_FRAMES_REQUIRE_TEXTURE_ARRAYS;

   // Cache max num ltr frames
   pD3D12Enc->max_num_ltr_frames =
      context->screen->get_video_param(context->screen,
                                       codec->profile,
                                       codec->entrypoint,
                                       PIPE_VIDEO_CAP_ENC_MAX_LONG_TERM_REFERENCES_PER_FRAME);
   pD3D12Enc->supports_sliced_fences.value = context->screen->get_video_param(context->screen, codec->profile,
                                                                              codec->entrypoint,
                                                                              PIPE_VIDEO_CAP_ENC_SLICED_NOTIFICATIONS);
   d3d12_video_encoder_initialize_two_pass(pD3D12Enc, codec->two_pass);

   if (pD3D12Ctx->priority_manager)
   {
      // Register queue with priority manager
      if (pD3D12Ctx->priority_manager->register_work_queue(pD3D12Ctx->priority_manager, pD3D12Enc->m_spEncodeCommandQueue.Get()) != 0)
      {
         debug_printf("[d3d12_video_encoder] d3d12_video_encoder_create_encoder - Failure on "
                      "pipe_priority_manager::register_work_queue\n");
         goto failed;
      }

      // Register queue with priority manager
      if (pD3D12Ctx->priority_manager->register_work_queue(pD3D12Ctx->priority_manager, pD3D12Enc->m_spResolveCommandQueue.Get()) != 0)
      {
         debug_printf("[d3d12_video_encoder] d3d12_video_encoder_create_encoder - Failure on "
                      "pipe_priority_manager::register_work_queue\n");
         goto failed;
      }

   }

   // Cache max slices cap
   pD3D12Enc->screen_max_slices_per_frame = context->screen->get_video_param(context->screen, codec->profile,
                                                                             codec->entrypoint,
                                                                             PIPE_VIDEO_CAP_ENC_MAX_SLICES_PER_FRAME);
   return &pD3D12Enc->base;

failed:
   if (pD3D12Enc != nullptr) {
      d3d12_video_encoder_destroy((struct pipe_video_codec *) pD3D12Enc);
   }

   return nullptr;
}

bool
d3d12_video_encoder_prepare_output_buffers(struct d3d12_video_encoder *pD3D12Enc,
                                           struct pipe_video_buffer *  srcTexture,
                                           struct pipe_picture_desc *  picture)
{
   pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.NodeIndex = pD3D12Enc->m_NodeIndex;
   pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.Codec =
      pD3D12Enc->m_currentEncodeConfig.m_encoderCodecDesc;
   pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.Profile =
      d3d12_video_encoder_get_current_profile_desc(pD3D12Enc);
   pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.InputFormat =
      pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format;
   pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.PictureTargetResolution =
      pD3D12Enc->m_currentEncodeConfig.m_currentResolution;

   // Assume all stats supported by the driver will be required and use max allocation to avoid reallocating between frames
   pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.OptionalMetadata = D3D12_VIDEO_ENCODER_OPTIONAL_METADATA_ENABLE_FLAG_NONE;

   if ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_PER_BLOCK_QP_MAP_METADATA_AVAILABLE))
   {
      pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.OptionalMetadata |=
         D3D12_VIDEO_ENCODER_OPTIONAL_METADATA_ENABLE_FLAG_QP_MAP;
   }

   if ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_PER_BLOCK_SATD_MAP_METADATA_AVAILABLE))
   {
      pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.OptionalMetadata |=
         D3D12_VIDEO_ENCODER_OPTIONAL_METADATA_ENABLE_FLAG_SATD_MAP;
   }

   if ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_PER_BLOCK_RC_BIT_ALLOCATION_MAP_METADATA_AVAILABLE))
   {
      pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.OptionalMetadata |=
         D3D12_VIDEO_ENCODER_OPTIONAL_METADATA_ENABLE_FLAG_RC_BIT_ALLOCATION_MAP;
   }

   if ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_FRAME_PSNR_METADATA_AVAILABLE))
   {
      pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.OptionalMetadata |=
         D3D12_VIDEO_ENCODER_OPTIONAL_METADATA_ENABLE_FLAG_FRAME_PSNR;
   }

   pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.CodecConfiguration = d3d12_video_encoder_get_current_codec_config_desc(pD3D12Enc);

   HRESULT hr = pD3D12Enc->m_spD3D12VideoDevice->CheckFeatureSupport(
      D3D12_FEATURE_VIDEO_ENCODER_RESOURCE_REQUIREMENTS1,
      &pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps,
      sizeof(pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps));

   if (FAILED(hr)) {
      debug_printf("CheckFeatureSupport D3D12_FEATURE_VIDEO_ENCODER_RESOURCE_REQUIREMENTS1 failed with HR %x\n", (unsigned)hr);
      debug_printf("Falling back to check previous query version D3D12_FEATURE_VIDEO_ENCODER_RESOURCE_REQUIREMENTS...\n");

      // D3D12_FEATURE_VIDEO_ENCODER_RESOURCE_REQUIREMENTS1 extends D3D12_FEATURE_VIDEO_ENCODER_RESOURCE_REQUIREMENTS
      // in a binary compatible way, so just cast it and try with the older query D3D12_FEATURE_VIDEO_ENCODER_RESOURCE_REQUIREMENTS
      D3D12_FEATURE_DATA_VIDEO_ENCODER_RESOURCE_REQUIREMENTS* casted_down_cap_data = reinterpret_cast<D3D12_FEATURE_DATA_VIDEO_ENCODER_RESOURCE_REQUIREMENTS*>(&pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps);
      hr = pD3D12Enc->m_spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_RESOURCE_REQUIREMENTS,
                                                  casted_down_cap_data,
                                                  sizeof(*casted_down_cap_data));
   }

   if (FAILED(hr)) {
      debug_printf("CheckFeatureSupport failed with HR %x\n", (unsigned)hr);
      return false;
   }

   if (!pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.IsSupported) {
      debug_printf("[d3d12_video_encoder] D3D12_FEATURE_VIDEO_ENCODER_RESOURCE_REQUIREMENTS arguments are not supported.\n");
      return false;
   }

   size_t current_metadata_slot = d3d12_video_encoder_metadata_current_index(pD3D12Enc);

   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   d3d12_video_encoder_calculate_metadata_resolved_buffer_size(
      codec,
      pD3D12Enc->m_currentEncodeCapabilities.m_MaxSlicesInOutput,
      pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].bufferSize);

   if ((pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].spBuffer == nullptr) ||
       (GetDesc(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].spBuffer.Get()).Width <
        pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].bufferSize)) {
      D3D12_HEAP_PROPERTIES Properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
      CD3DX12_RESOURCE_DESC resolvedMetadataBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].bufferSize);

      pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].spBuffer.Reset();
      HRESULT hr = pD3D12Enc->m_pD3D12Screen->dev->CreateCommittedResource(
         &Properties,
         D3D12_HEAP_FLAG_NONE,
         &resolvedMetadataBufferDesc,
         D3D12_RESOURCE_STATE_COPY_DEST,
         nullptr,
         IID_PPV_ARGS(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].spBuffer.GetAddressOf()));

      if (FAILED(hr)) {
         debug_printf("CreateCommittedResource failed with HR %x\n", (unsigned)hr);
         return false;
      }
   }

   if ((pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_spMetadataOutputBuffer == nullptr) ||
       (GetDesc(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_spMetadataOutputBuffer.Get()).Width <
        pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.MaxEncoderOutputMetadataBufferSize)) {
      D3D12_HEAP_PROPERTIES Properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
      CD3DX12_RESOURCE_DESC metadataBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
         pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.MaxEncoderOutputMetadataBufferSize);

      pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_spMetadataOutputBuffer.Reset();
      HRESULT hr = pD3D12Enc->m_pD3D12Screen->dev->CreateCommittedResource(
         &Properties,
         D3D12_HEAP_FLAG_NONE,
         &metadataBufferDesc,
         D3D12_RESOURCE_STATE_COMMON,
         nullptr,
         IID_PPV_ARGS(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_spMetadataOutputBuffer.GetAddressOf()));

      if (FAILED(hr)) {
         debug_printf("CreateCommittedResource failed with HR %x\n", (unsigned)hr);
         return false;
      }
   }
   return true;
}

bool
d3d12_video_encoder_prepare_input_buffers(struct d3d12_video_encoder *pD3D12Enc)
{
   // Go over any features that may need additional input buffers
   // and create them on demand (if the previous allocation is not big enough)

   HRESULT hr = S_OK;
   D3D12_HEAP_PROPERTIES Properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
   
   // Cache the current pool index to avoid repeated modulo operations
   const size_t current_pool_index = d3d12_video_encoder_pool_current_index(pD3D12Enc);
   if (d3d12_video_encoder_is_dirty_regions_feature_enabled(pD3D12Enc, D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_GPU_TEXTURE))
   {
      bool bNeedsCreation = (pD3D12Enc->m_inflightResourcesPool[current_pool_index].m_spDirtyRectsResolvedOpaqueMap == NULL) ||
                            (GetDesc(pD3D12Enc->m_inflightResourcesPool[current_pool_index].m_spDirtyRectsResolvedOpaqueMap.Get()).Width <
                             pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.capInputLayoutDirtyRegion.MaxResolvedBufferAllocationSize);
      if (bNeedsCreation)
      {
         pD3D12Enc->m_inflightResourcesPool[current_pool_index].m_spDirtyRectsResolvedOpaqueMap.Reset();
         CD3DX12_RESOURCE_DESC subregionOffsetsDesc = CD3DX12_RESOURCE_DESC::Buffer(pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.capInputLayoutDirtyRegion.MaxResolvedBufferAllocationSize);
         hr = pD3D12Enc->m_pD3D12Screen->dev->CreateCommittedResource(&Properties,
            D3D12_HEAP_FLAG_NONE,
            &subregionOffsetsDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&pD3D12Enc->m_inflightResourcesPool[current_pool_index].m_spDirtyRectsResolvedOpaqueMap));
         if (FAILED(hr))
         {
            debug_printf("CreateCommittedResource for m_spDirtyRectsResolvedOpaqueMap failed with HR %x\n", (unsigned)hr);
         }
      }
   }

   BOOL QPMapEnabled = FALSE;
   D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE QPMapSource = D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_CPU_BUFFER;
   d3d12_video_encoder_is_gpu_qmap_input_feature_enabled(pD3D12Enc, /*output param*/ QPMapEnabled, /*output param*/ QPMapSource);
   if (QPMapEnabled && (QPMapSource == D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_GPU_TEXTURE))
   {
      bool bNeedsCreation = (pD3D12Enc->m_inflightResourcesPool[current_pool_index].m_spQPMapResolvedOpaqueMap == NULL) ||
                            (GetDesc(pD3D12Enc->m_inflightResourcesPool[current_pool_index].m_spQPMapResolvedOpaqueMap.Get()).Width <
                             pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.GPUInput.capInputLayoutQPMap.MaxResolvedBufferAllocationSize);
      if (bNeedsCreation)
      {
         pD3D12Enc->m_inflightResourcesPool[current_pool_index].m_spQPMapResolvedOpaqueMap.Reset();
         CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.GPUInput.capInputLayoutQPMap.MaxResolvedBufferAllocationSize);
         hr = pD3D12Enc->m_pD3D12Screen->dev->CreateCommittedResource(&Properties,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&pD3D12Enc->m_inflightResourcesPool[current_pool_index].m_spQPMapResolvedOpaqueMap));
         if (FAILED(hr))
         {
            debug_printf("CreateCommittedResource for m_spQPMapResolvedOpaqueMap failed with HR %x\n", (unsigned)hr);
         }
      }
   }

   if (d3d12_video_encoder_is_move_regions_feature_enabled(pD3D12Enc, D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_GPU_TEXTURE))
   {
      bool bNeedsCreation = (pD3D12Enc->m_inflightResourcesPool[current_pool_index].m_spMotionVectorsResolvedOpaqueMap == NULL) ||
                            (GetDesc(pD3D12Enc->m_inflightResourcesPool[current_pool_index].m_spMotionVectorsResolvedOpaqueMap.Get()).Width <
                             pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.capInputLayoutMotionVectors.MaxResolvedBufferAllocationSize);
      if (bNeedsCreation)
      {
         pD3D12Enc->m_inflightResourcesPool[current_pool_index].m_spMotionVectorsResolvedOpaqueMap.Reset();
         CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.capInputLayoutMotionVectors.MaxResolvedBufferAllocationSize);
         hr = pD3D12Enc->m_pD3D12Screen->dev->CreateCommittedResource(&Properties,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&pD3D12Enc->m_inflightResourcesPool[current_pool_index].m_spMotionVectorsResolvedOpaqueMap));
         if (FAILED(hr))
         {
            debug_printf("CreateCommittedResource for m_spMotionVectorsResolvedOpaqueMap failed with HR %x\n", (unsigned)hr);
         }
      }
   }
   return SUCCEEDED(hr);
}

bool
d3d12_video_encoder_reconfigure_session(struct d3d12_video_encoder *pD3D12Enc,
                                        struct pipe_video_buffer *  srcTexture,
                                        struct pipe_picture_desc *  picture)
{
   assert(pD3D12Enc->m_spD3D12VideoDevice);
   D3D12_VIDEO_SAMPLE srcTextureDesc = {};
   srcTextureDesc.Width = srcTexture->width;
   srcTextureDesc.Height = srcTexture->height;
   srcTextureDesc.Format.Format = d3d12_get_format(srcTexture->buffer_format);
   if(!d3d12_video_encoder_update_current_encoder_config_state(pD3D12Enc, srcTextureDesc, picture)) {
      debug_printf("d3d12_video_encoder_update_current_encoder_config_state failed!\n");
      return false;
   }
   if(!d3d12_video_encoder_reconfigure_encoder_objects(pD3D12Enc, srcTexture, picture)) {
      debug_printf("d3d12_video_encoder_reconfigure_encoder_objects failed!\n");
      return false;
   }
   d3d12_video_encoder_update_picparams_tracking(pD3D12Enc, srcTexture, picture);
   if(!d3d12_video_encoder_prepare_output_buffers(pD3D12Enc, srcTexture, picture)) {
      debug_printf("d3d12_video_encoder_prepare_output_buffers failed!\n");
      return false;
   }
   if(!d3d12_video_encoder_prepare_input_buffers(pD3D12Enc)) {
      debug_printf("d3d12_video_encoder_prepare_input_buffers failed!\n");
      return false;
   }

   // Save frame size expectation snapshot from record time to resolve at get_feedback time (after execution)
   size_t current_metadata_slot = d3d12_video_encoder_metadata_current_index(pD3D12Enc);
   pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].expected_max_frame_size =
      pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].max_frame_size;

   pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].expected_max_slice_size =
      (pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode == D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_BYTES_PER_SUBREGION) ?
      pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigDesc.m_SlicesPartition_H264.MaxBytesPerSlice : 0;

   return true;
}

/**
 * start encoding of a new frame
 */
void
d3d12_video_encoder_begin_frame(struct pipe_video_codec * codec,
                                struct pipe_video_buffer *target,
                                struct pipe_picture_desc *picture)
{
   // Do nothing here. Initialize happens on encoder creation, re-config (if any) happens in
   // d3d12_video_encoder_encode_bitstream
   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;
   assert(pD3D12Enc);
   HRESULT hr = S_OK;
   debug_printf("[d3d12_video_encoder] d3d12_video_encoder_begin_frame started for fenceValue: %" PRIu64 "\n",
                 pD3D12Enc->m_fenceValue);

   // Cache frequently used index calculations to avoid repeated modulo operations
   const size_t current_pool_index = d3d12_video_encoder_pool_current_index(pD3D12Enc);
   const size_t current_metadata_index = d3d12_video_encoder_metadata_current_index(pD3D12Enc);

   ///
   /// Wait here to make sure the next in flight resource set is empty before using it
   ///
   if (pD3D12Enc->m_fenceValue > pD3D12Enc->m_MaxQueueAsyncDepth) {
      debug_printf("[d3d12_video_encoder] d3d12_video_encoder_begin_frame Waiting for completion of in flight resource sets with previous work for pool index:"
                   "%" PRIu64 "\n",
                   (uint64_t)current_pool_index);
      d3d12_fence_finish(pD3D12Enc->m_inflightResourcesPool[current_pool_index].m_CompletionFence.get(), OS_TIMEOUT_INFINITE);
   }

   if (!d3d12_video_encoder_reconfigure_session(pD3D12Enc, target, picture)) {
      debug_printf("[d3d12_video_encoder] d3d12_video_encoder_begin_frame - Failure on "
                      "d3d12_video_encoder_reconfigure_session\n");
      goto fail;
   }

   hr = pD3D12Enc->m_spEncodeCommandList->Reset(pD3D12Enc->m_inflightResourcesPool[current_pool_index].m_spCommandAllocator.Get());
   if (FAILED(hr)) {
      debug_printf(
         "[d3d12_video_encoder] d3d12_video_encoder_flush - resetting ID3D12GraphicsCommandList failed with HR %x\n",
         (unsigned)hr);
      goto fail;
   }

   hr = pD3D12Enc->m_spResolveCommandList->Reset(pD3D12Enc->m_inflightResourcesPool[current_pool_index].m_spResolveCommandAllocator.Get());
   if (FAILED(hr)) {
      debug_printf(
         "[d3d12_video_encoder] d3d12_video_encoder_flush - resetting ID3D12GraphicsCommandList failed with HR %x\n",
         (unsigned)hr);
      goto fail;
   }

   pD3D12Enc->m_inflightResourcesPool[current_pool_index].m_InputSurfaceFence = d3d12_fence(picture->in_fence);
   pD3D12Enc->m_inflightResourcesPool[current_pool_index].m_InputSurfaceFenceValue = picture->in_fence_value;
   pD3D12Enc->m_inflightResourcesPool[current_pool_index].encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_OK;
   pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_index].encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_OK;

   debug_printf("[d3d12_video_encoder] d3d12_video_encoder_begin_frame finalized for fenceValue: %" PRIu64 "\n",
                 pD3D12Enc->m_fenceValue);
   return;

fail:
   debug_printf("[d3d12_video_encoder] d3d12_video_encoder_begin_frame failed for fenceValue: %" PRIu64 "\n",
                pD3D12Enc->m_fenceValue);
   pD3D12Enc->m_inflightResourcesPool[current_pool_index].encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;
   pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_index].encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;
   assert(false);
}

void
d3d12_video_encoder_calculate_metadata_resolved_buffer_size(enum pipe_video_format codec, uint32_t maxSliceNumber, uint64_t &bufferSize)
{
   bufferSize = sizeof(D3D12_VIDEO_ENCODER_OUTPUT_METADATA) +
                (maxSliceNumber * sizeof(D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA));
                
   switch (codec) {
#if VIDEO_CODEC_H264ENC
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
         break;
#endif
#if VIDEO_CODEC_H265ENC
      case PIPE_VIDEO_FORMAT_HEVC:
         break;
#endif
#if VIDEO_CODEC_AV1ENC
      case PIPE_VIDEO_FORMAT_AV1:
      {
         size_t extra_av1_size = d3d12_video_encoder_calculate_metadata_resolved_buffer_size_av1(maxSliceNumber);
         bufferSize += extra_av1_size;
      } break;
#endif
      default:
      {
         UNREACHABLE("Unsupported pipe_video_format");
      } break;
   }
}

// Returns the number of slices that the output will contain for fixed slicing modes
// and the maximum number of slices the output might contain for dynamic slicing modes (eg. max bytes per slice)
uint32_t
d3d12_video_encoder_calculate_max_slices_count_in_output(
   D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE                          slicesMode,
   const D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_SLICES *slicesConfig,
   uint32_t                                                                 MaxSubregionsNumberFromCaps,
   const D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC&                       sequenceTargetResolution,
   uint32_t                                                                 SubregionBlockPixelsSize)
{
   uint32_t pic_width_in_subregion_units =
      static_cast<uint32_t>(std::ceil(sequenceTargetResolution.Width / static_cast<double>(SubregionBlockPixelsSize)));
   uint32_t pic_height_in_subregion_units =
      static_cast<uint32_t>(std::ceil(sequenceTargetResolution.Height / static_cast<double>(SubregionBlockPixelsSize)));
   uint32_t total_picture_subregion_units = pic_width_in_subregion_units * pic_height_in_subregion_units;
   uint32_t maxSlices                     = 0u;
   switch (slicesMode) {
      case D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_FULL_FRAME:
      {
         maxSlices = 1u;
      } break;
      case D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_BYTES_PER_SUBREGION:
      case D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_AUTO:
      {
         maxSlices = MaxSubregionsNumberFromCaps;
      } break;
      case D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_SQUARE_UNITS_PER_SUBREGION_ROW_UNALIGNED:
      {
         maxSlices = static_cast<uint32_t>(
            std::ceil(total_picture_subregion_units / static_cast<double>(slicesConfig->NumberOfCodingUnitsPerSlice)));
      } break;
      case D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_ROWS_PER_SUBREGION:
      {
         maxSlices = static_cast<uint32_t>(
            std::ceil(pic_height_in_subregion_units / static_cast<double>(slicesConfig->NumberOfRowsPerSlice)));
      } break;
      case D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_SUBREGIONS_PER_FRAME:
      {
         maxSlices = slicesConfig->NumberOfSlicesPerFrame;
      } break;
      default:
      {
         UNREACHABLE("Unsupported D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE");
      } break;
   }

   return maxSlices;
}

static inline bool
d3d12_buffer_maps_directly(pipe_resource *buffer)
{
   return buffer->target == PIPE_BUFFER &&
          buffer->usage != PIPE_USAGE_DEFAULT &&
          buffer->usage != PIPE_USAGE_IMMUTABLE;
}

void
d3d12_video_encoder_get_slice_bitstream_data(struct pipe_video_codec *codec,
                                             void *feedback,
                                             unsigned slice_idx,
                                             struct codec_unit_location_t *codec_unit_metadata,
                                             unsigned *codec_unit_metadata_count)
{
   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;
   struct d3d12_fence *feedback_fence = (struct d3d12_fence *) feedback;
   uint64_t requested_metadata_fence = feedback_fence->value;

   //
   // Only resolve them once and cache them for future calls
   //
   size_t current_metadata_slot = (requested_metadata_fence % pD3D12Enc->m_MaxMetadataBuffersCount);

   if (pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppResolvedSubregionSizes[slice_idx] == 0)
   {
#ifdef MESA_DEBUG
      HRESULT hr = pD3D12Enc->m_pD3D12Screen->dev->GetDeviceRemovedReason();
      if (hr != S_OK) {
         debug_printf("Error: d3d12_video_encoder_get_slice_bitstream_data for Encode GPU command for fence %" PRIu64 " failed with GetDeviceRemovedReason: %x\n",
                        requested_metadata_fence,
                        (unsigned)hr);
         assert(false);
         if (codec_unit_metadata_count)
            *codec_unit_metadata_count = 0u;
         return;
      }
#endif

      bool wait_res = d3d12_fence_finish(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSubregionPipeFences[slice_idx].get(), OS_TIMEOUT_INFINITE);
      if (!wait_res) {
         debug_printf("Error: d3d12_video_encoder_get_slice_bitstream_data for Encode GPU command for fence %" PRIu64 " failed on d3d12_video_encoder_ensure_fence_finished\n",
                        requested_metadata_fence);
         assert(false);
         if (codec_unit_metadata_count)
            *codec_unit_metadata_count = 0u;
         return;
      }

      if((pD3D12Enc->m_fenceValue - requested_metadata_fence) > pD3D12Enc->m_MaxMetadataBuffersCount)
      {
         debug_printf("[d3d12_video_encoder_get_slice_bitstream_data] Requested metadata for fence %" PRIu64 " at current fence %" PRIu64
            " is too far back in time for the ring buffer of size %" PRIu64 " we keep track off - "
            " Please increase the m_MaxMetadataBuffersCount of the encoder and try again.\n",
            requested_metadata_fence,
            pD3D12Enc->m_fenceValue,
            static_cast<uint64_t>(pD3D12Enc->m_MaxMetadataBuffersCount));
         if (codec_unit_metadata_count)
            *codec_unit_metadata_count = 0u;
         assert(false);
         return;
      }

      // Use native D3D12 API for direct buffer mapping - buffers are created with D3D12_HEAP_TYPE_READBACK
      ID3D12Resource* pSizesResource = pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppSubregionSizes[slice_idx];
      ID3D12Resource* pOffsetsResource = pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppSubregionOffsets[slice_idx];
      
      assert(pSizesResource && pOffsetsResource);
      
#if MESA_DEBUG
      // Verify resources are CPU accessible (created with D3D12_HEAP_TYPE_READBACK)
      D3D12_HEAP_PROPERTIES heapProps;
      D3D12_HEAP_FLAGS heapFlags;
      pSizesResource->GetHeapProperties(&heapProps, &heapFlags);
      assert(heapProps.Type == D3D12_HEAP_TYPE_READBACK);
      pOffsetsResource->GetHeapProperties(&heapProps, &heapFlags);
      assert(heapProps.Type == D3D12_HEAP_TYPE_READBACK);
#endif
      
      // Map sizes buffer
      D3D12_RANGE readRange = { 0, sizeof(UINT64) };
      void* pSizesMappedPtr;
      hr = pSizesResource->Map(0, &readRange, &pSizesMappedPtr);
      if (FAILED(hr)) {
         debug_printf("Error: d3d12_video_encoder_get_slice_bitstream_data failed to map sizes buffer for fence %" PRIu64 " with HR %x\n",
                        requested_metadata_fence, (unsigned)hr);
         if (codec_unit_metadata_count)
            *codec_unit_metadata_count = 0u;
         return;
      }
      pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppResolvedSubregionSizes[slice_idx] = *reinterpret_cast<UINT64*>(pSizesMappedPtr);
      pSizesResource->Unmap(0, nullptr);
      
      // Map offsets buffer
      void* pOffsetsMappedPtr;
      hr = pOffsetsResource->Map(0, &readRange, &pOffsetsMappedPtr);
      if (FAILED(hr)) {
         debug_printf("Error: d3d12_video_encoder_get_slice_bitstream_data failed to map offsets buffer for fence %" PRIu64 " with HR %x\n",
                        requested_metadata_fence, (unsigned)hr);
         if (codec_unit_metadata_count)
            *codec_unit_metadata_count = 0u;
         return;
      }
      pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppResolvedSubregionOffsets[slice_idx] = *reinterpret_cast<UINT64*>(pOffsetsMappedPtr);
      pOffsetsResource->Unmap(0, nullptr);

      // We may have added packed nals before each slice (e.g prefix nal)
      // lets upload them into the output buffer
      for (uint32_t slice_nal_idx = 0; slice_nal_idx < pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSliceHeaders[slice_idx].size();slice_nal_idx++)
      {
         uint64_t nal_byte_size = pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSliceHeaders[slice_idx][slice_nal_idx].buffer.size();

         // As per DX12 spec, the driver will begin writing the slice at ppSubregionOffsets[slice_idx]
         // and this offset includes the pSubregionBitstreamsBaseOffsets[slice_idx] passed by the app
         // that are left empty before the slice begins, leaving room for things like header packing
         assert(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSubregionBitstreamsBaseOffsets[slice_idx] >= nal_byte_size);
         assert(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppResolvedSubregionOffsets[slice_idx] >= nal_byte_size);
         assert(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppResolvedSubregionOffsets[slice_idx] >=
            pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSubregionBitstreamsBaseOffsets[slice_idx]);

         uint64_t nal_placing_offset = pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppResolvedSubregionOffsets[slice_idx] - nal_byte_size;

         // Buffer size check before buffer_subdata
         struct pipe_resource *dst_buffer = pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].comp_bit_destinations[slice_idx];
         if (dst_buffer->width0 < (nal_placing_offset + nal_byte_size)) {
            assert (false);
            debug_printf("Error: d3d12_video_encoder_get_slice_bitstream_data buffer_subdata would overflow destination buffer. "
                        "Buffer size: %u, required: %" PRIu64 " (offset: %" PRIu64 " + size: %" PRIu64 ")\n",
                        dst_buffer->width0, nal_placing_offset + nal_byte_size, nal_placing_offset, nal_byte_size);
            if (codec_unit_metadata_count) {
               *codec_unit_metadata_count = 1u;
               if (codec_unit_metadata) {
                  codec_unit_metadata[0].flags = PIPE_VIDEO_CODEC_UNIT_LOCATION_FLAG_MAX_SLICE_SIZE_OVERFLOW;
                  codec_unit_metadata[0].size = 0;
                  codec_unit_metadata[0].offset = 0;
               }
            }
            return;
         }

         // We upload it here since for single buffer case, we don't know the exact absolute ppSubregionOffsets of the slice in the buffer until slice fence is signaled
         pD3D12Enc->base.context->buffer_subdata(pD3D12Enc->base.context,                                                                                            // context
                                                 dst_buffer,                                                                                                         // dst buffer
                                                 PIPE_MAP_WRITE,                                                                                                     // usage PIPE_MAP_x
                                                 static_cast<unsigned int>(nal_placing_offset),                                                                      // offset
                                                 static_cast<unsigned int>(nal_byte_size),                                                                           // src size
                                                 pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSliceHeaders[slice_idx][slice_nal_idx].buffer.data());  // src data
      }

      // If we uploaded new slice headers, flush and wait for the context to upload them
      if ((pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSliceHeaders[slice_idx].size() > 0) &&
         !d3d12_buffer_maps_directly(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].comp_bit_destinations[slice_idx])) // If the buffer maps directly, the buffer_subdata is synchronous on unmap, no need to flush
      {
         struct pipe_fence_handle *pUploadGPUCompletionFence = NULL;
         pD3D12Enc->base.context->flush(pD3D12Enc->base.context,
                                       &pUploadGPUCompletionFence,
                                       PIPE_FLUSH_ASYNC | PIPE_FLUSH_HINT_FINISH);
         assert(pUploadGPUCompletionFence);
         pD3D12Enc->m_pD3D12Screen->base.fence_finish(&pD3D12Enc->m_pD3D12Screen->base,
                                                      NULL,
                                                      pUploadGPUCompletionFence,
                                                      OS_TIMEOUT_INFINITE);
         pD3D12Enc->m_pD3D12Screen->base.fence_reference(&pD3D12Enc->m_pD3D12Screen->base,
                                                         &pUploadGPUCompletionFence,
                                                         NULL);
      }
   }

   // Buffer size check for resolved subregion
   uint64_t subregion_end_offset = pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppResolvedSubregionOffsets[slice_idx] +
                                   pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppResolvedSubregionSizes[slice_idx];
   if (pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].comp_bit_destinations[slice_idx]->width0 < subregion_end_offset) {
      debug_printf("Error: d3d12_video_encoder_get_slice_bitstream_data resolved subregion extends beyond buffer boundary. "
                  "Buffer size: %u, subregion end offset: %" PRIu64 " (offset: %" PRIu64 " + size: %" PRIu64 ")\n",
                  pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].comp_bit_destinations[slice_idx]->width0,
                  subregion_end_offset,
                  pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppResolvedSubregionOffsets[slice_idx],
                  pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppResolvedSubregionSizes[slice_idx]);
      if (codec_unit_metadata_count) {
         *codec_unit_metadata_count = 1u;
         if (codec_unit_metadata) {
            codec_unit_metadata[0].flags = PIPE_VIDEO_CODEC_UNIT_LOCATION_FLAG_MAX_SLICE_SIZE_OVERFLOW;
            codec_unit_metadata[0].size = 0;
            codec_unit_metadata[0].offset = 0;
         }
      }
      return;
   }

   *codec_unit_metadata_count = 1u; // one slice
   if (slice_idx == 0) // On the first slice we may have added other packed codec units
      *codec_unit_metadata_count += static_cast<unsigned int>(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pWrittenCodecUnitsSizes.size());

   // We may have added packed nals before each slice (e.g prefix nal)
   *codec_unit_metadata_count += static_cast<unsigned int>(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSliceHeaders[slice_idx].size());

   // When codec_unit_metadata is null, only report the number of NALs (codec_unit_metadata_count)
   if (codec_unit_metadata != NULL)
   {
      uint64_t output_buffer_size = 0u;
      uint32_t codec_unit_idx = 0;
      while ((slice_idx == 0)/*On the first slice we may have added other packed codec units*/ &&
         (codec_unit_idx < pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pWrittenCodecUnitsSizes.size()))
      {
         codec_unit_metadata[codec_unit_idx].flags = PIPE_VIDEO_CODEC_UNIT_LOCATION_FLAG_SINGLE_NALU;
         codec_unit_metadata[codec_unit_idx].size = pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pWrittenCodecUnitsSizes[codec_unit_idx];
         codec_unit_metadata[codec_unit_idx].offset = output_buffer_size;
         output_buffer_size += pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pWrittenCodecUnitsSizes[codec_unit_idx];
         codec_unit_idx++;
      }

      // We may have added packed nals before each slice (e.g prefix nal)
      for (uint32_t slice_nal_idx = 0; slice_nal_idx < pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSliceHeaders[slice_idx].size();slice_nal_idx++)
      {
         codec_unit_metadata[codec_unit_idx].flags = PIPE_VIDEO_CODEC_UNIT_LOCATION_FLAG_SINGLE_NALU;
         codec_unit_metadata[codec_unit_idx].size = static_cast<uint64_t>(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSliceHeaders[slice_idx][slice_nal_idx].buffer.size());
         codec_unit_metadata[codec_unit_idx].offset = output_buffer_size;
         output_buffer_size += codec_unit_metadata[codec_unit_idx].size;
         codec_unit_idx++;
      }

      codec_unit_metadata[codec_unit_idx].size = pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppResolvedSubregionSizes[slice_idx];
      codec_unit_metadata[codec_unit_idx].offset = pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppResolvedSubregionOffsets[slice_idx];
      codec_unit_metadata[codec_unit_idx].flags = PIPE_VIDEO_CODEC_UNIT_LOCATION_FLAG_SINGLE_NALU;

      assert((codec_unit_idx + 1) == *codec_unit_metadata_count);
   }
}

void
d3d12_video_encoder_encode_bitstream_sliced(struct pipe_video_codec *codec,
                                            struct pipe_video_buffer *source,
                                            unsigned num_slice_objects,
                                            struct pipe_resource **slice_destinations,
                                            struct pipe_fence_handle **slice_fences,
                                            struct pipe_fence_handle **last_slice_completion_fence,
                                            void **feedback)
{
   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;
   if(!pD3D12Enc->supports_sliced_fences.bits.supported)
   {
      assert(false);
      return;
   }

   d3d12_video_encoder_encode_bitstream_impl(codec,
                                             source,
                                             num_slice_objects,
                                             slice_destinations,
                                             slice_fences,
                                             last_slice_completion_fence,
                                             feedback);
}

void
d3d12_video_encoder_encode_bitstream(struct pipe_video_codec * codec,
                                     struct pipe_video_buffer *source,
                                     struct pipe_resource *    destination,
                                     void **                   feedback)
{
   struct pipe_fence_handle *slice_fences = NULL;
   struct pipe_fence_handle *last_slice_completion_fence = NULL;
   d3d12_video_encoder_encode_bitstream_impl(codec,
                                             source,
                                             1 /*num_slice_objects*/,
                                             &destination /*slice_destinations*/,
                                             &slice_fences,
                                             &last_slice_completion_fence,
                                             feedback);
}

void
d3d12_video_encoder_encode_bitstream_impl(struct pipe_video_codec *codec,
                                          struct pipe_video_buffer *source,
                                          unsigned num_slice_objects,
                                          struct pipe_resource **slice_destinations,
                                          struct pipe_fence_handle **slice_fences,
                                          [[maybe_unused]] struct pipe_fence_handle **last_slice_completion_fence,
                                          void **feedback)
{
   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;
   assert(pD3D12Enc);
   debug_printf("[d3d12_video_encoder] d3d12_video_encoder_encode_bitstream started for fenceValue: %" PRIu64 "\n",
                 pD3D12Enc->m_fenceValue);
   assert(pD3D12Enc->m_spD3D12VideoDevice);
   assert(pD3D12Enc->m_spEncodeCommandQueue);
   assert(pD3D12Enc->m_pD3D12Screen);

   struct d3d12_video_buffer *pInputVideoBuffer = (struct d3d12_video_buffer *) source;

   // Detect and flush any pending operations on input/output resources to the encoder and if necessary
   // early flush the context for any operations that may be pending on input/output resources to the encoder
   // but do not block on completion until encoder flush
   {
      struct d3d12_context *ctx = d3d12_context(pD3D12Enc->base.context);
      struct d3d12_batch *batch = d3d12_current_batch(ctx);
      
      bool needs_flush = d3d12_batch_has_references(batch, pInputVideoBuffer->texture->bo, false);
      
      // Check quantization matrix input map
      if (!needs_flush && pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.GPUInput.InputMap) {
         needs_flush = d3d12_batch_has_references(batch, pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.GPUInput.InputMap->bo, false);
      }

      // Check dirty rects input map
      if (!needs_flush && pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.InputMap) {
         needs_flush = d3d12_batch_has_references(batch, pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.InputMap->bo, false);
      }

      // Check motion vector input maps
      auto &mvMaps = pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo;
      for (unsigned i = 0; !needs_flush && i < mvMaps.pMotionVectorMapsGalliumResources.size(); i++) {
         if (mvMaps.pMotionVectorMapsGalliumResources[i]) {
            needs_flush = d3d12_batch_has_references(batch, mvMaps.pMotionVectorMapsGalliumResources[i]->bo, false);
         }
         if (!needs_flush && i < mvMaps.pMotionVectorMapsMetadataGalliumResources.size() && mvMaps.pMotionVectorMapsMetadataGalliumResources[i]) {
            needs_flush = d3d12_batch_has_references(batch, mvMaps.pMotionVectorMapsMetadataGalliumResources[i]->bo, false);
         }
      }
      
      if (needs_flush) {
         debug_printf("[d3d12_video_encoder] d3d12_video_encoder_encode_bitstream_impl - Flushing pD3D12Enc->base.context.\n");
         pD3D12Enc->base.context->flush(
            pD3D12Enc->base.context,
            &pD3D12Enc->m_inflightResourcesPool[d3d12_video_encoder_pool_current_index(pD3D12Enc)].context_completion_fence,
            PIPE_FLUSH_ASYNC | PIPE_FLUSH_HINT_FINISH);
         assert(pD3D12Enc->m_inflightResourcesPool[d3d12_video_encoder_pool_current_index(pD3D12Enc)].context_completion_fence);
      }
   }

   // Clear reusable barrier vectors
   pD3D12Enc->m_rgCurrentFrameStateTransitions.clear();
   pD3D12Enc->m_rgReferenceTransitions.clear();
   pD3D12Enc->m_pResolveInputDataBarriers.clear();
   pD3D12Enc->m_pTwoPassExtraBarriers.clear();
   pD3D12Enc->m_pSlicedEncodingExtraBarriers.clear();
   pD3D12Enc->m_rgResolveMetadataStateTransitions.clear();
   pD3D12Enc->m_outputStatsBarriers.clear();

   // Clear reusable resource vectors
   pD3D12Enc->m_pOutputBitstreamBuffers.clear();
   pD3D12Enc->m_pOutputBufferD3D12Resources.clear();

   if (pD3D12Enc->m_inflightResourcesPool[d3d12_video_encoder_pool_current_index(pD3D12Enc)].encode_result & PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED) {
      debug_printf("WARNING: [d3d12_video_encoder] d3d12_video_encoder_encode_bitstream - Frame submission %" PRIu64 " failed. Encoder lost, please recreate pipe_video_codec object\n", pD3D12Enc->m_fenceValue);
      assert(false);
      return;
   }

   assert(pInputVideoBuffer);
   ID3D12Resource *pInputVideoD3D12Res        = d3d12_resource_resource(pInputVideoBuffer->texture);
   uint32_t        inputVideoD3D12Subresource = 0u;

   pD3D12Enc->m_pOutputBitstreamBuffers.resize(num_slice_objects, NULL);
   for (uint32_t slice_idx = 0; slice_idx < num_slice_objects;slice_idx++) {
      pD3D12Enc->m_pOutputBitstreamBuffers[slice_idx] = (struct d3d12_resource *) slice_destinations[slice_idx];
   }

   // Make permanently resident for video use - batch promote all slice buffers
   d3d12_promote_to_permanent_residency(pD3D12Enc->m_pD3D12Screen, pD3D12Enc->m_pOutputBitstreamBuffers.data(), num_slice_objects, pD3D12Enc->m_spResidencyFence.Get(), &pD3D12Enc->m_ResidencyFenceValue);

   // Make permanently resident for video use
   d3d12_promote_to_permanent_residency(pD3D12Enc->m_pD3D12Screen, &pInputVideoBuffer->texture, 1, pD3D12Enc->m_spResidencyFence.Get(), &pD3D12Enc->m_ResidencyFenceValue);

   size_t current_metadata_slot = d3d12_video_encoder_metadata_current_index(pD3D12Enc);

   /* Warning if the previous finished async execution stored was read not by get_feedback()
      before overwriting. This should be handled correctly by the app by calling vaSyncBuffer/vaSyncSurface
      without having the async depth going beyond pD3D12Enc->m_MaxMetadataBuffersCount frames without syncing */
   if(!pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].bRead) {
      debug_printf("WARNING: [d3d12_video_encoder] d3d12_video_encoder_encode_bitstream - overwriting metadata slot %" PRIu64 " before calling get_feedback", static_cast<uint64_t>(current_metadata_slot));
      assert(false);
   }
   pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].bRead = false;

   ///
   /// Record Encode operation
   ///

   ///
   /// Process pre-encode bitstream headers
   ///

   // Decide the D3D12 buffer EncodeFrame will write to based on pre-post encode headers generation policy
   pD3D12Enc->m_pOutputBufferD3D12Resources.resize(num_slice_objects, NULL);

   d3d12_video_encoder_build_pre_encode_codec_headers(pD3D12Enc, 
                                                      pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].postEncodeHeadersNeeded,
                                                      pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].preEncodeGeneratedHeadersByteSize,
                                                      pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pWrittenCodecUnitsSizes);
   assert(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].preEncodeGeneratedHeadersByteSize == pD3D12Enc->m_BitstreamHeadersBuffer.size());
   pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].preEncodeGeneratedHeadersBytePadding = 0;

   // Save the pipe destination buffer the headers need to be written to in get_feedback if post encode headers needed or H264 SVC NAL prefixes, etc
   pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].comp_bit_destinations.resize(num_slice_objects, NULL);
   for (uint32_t slice_idx = 0; slice_idx < num_slice_objects;slice_idx++) {
      pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].comp_bit_destinations[slice_idx] = &pD3D12Enc->m_pOutputBitstreamBuffers[slice_idx]->base.b;
   }

   // Only upload headers now and leave prefix offset space gap in compressed bitstream if the codec builds headers before execution.
   if (!pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].postEncodeHeadersNeeded)
   {

      // Headers are written before encode execution, have EncodeFrame write directly into the pipe destination buffer
      for (uint32_t slice_idx = 0; slice_idx < num_slice_objects;slice_idx++) {
         pD3D12Enc->m_pOutputBufferD3D12Resources[slice_idx] = d3d12_resource_resource(pD3D12Enc->m_pOutputBitstreamBuffers[slice_idx]);
      }

      // It can happen that codecs like H264/HEVC don't write pre-headers for all frames (ie. reuse previous PPS)
      if (pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].preEncodeGeneratedHeadersByteSize > 0)
      {
         // If driver needs offset alignment for bitstream resource, we will pad zeroes on the codec header to this end.
         if (
            (pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.CompressedBitstreamBufferAccessAlignment > 1)
            && ((pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].preEncodeGeneratedHeadersByteSize % pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.CompressedBitstreamBufferAccessAlignment) != 0)
         ) {
            uint64_t new_size = align64(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].preEncodeGeneratedHeadersByteSize, pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.CompressedBitstreamBufferAccessAlignment);
            pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].preEncodeGeneratedHeadersBytePadding = new_size - pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].preEncodeGeneratedHeadersByteSize;
            pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].preEncodeGeneratedHeadersByteSize = new_size;
            pD3D12Enc->m_BitstreamHeadersBuffer.resize(static_cast<size_t>(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].preEncodeGeneratedHeadersByteSize), 0);
         }

         // Upload the CPU buffers with the bitstream headers to the compressed bitstream resource in the interval
         // [0..pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].preEncodeGeneratedHeadersByteSize)
         // Note: The buffer_subdata is queued in pD3D12Enc->base.context but doesn't execute immediately
         // But we early flush the context below to ensure the data upload starts as soon as possible in parallel
         // to this function. Then in d3d12_video_encoder_flush we Wait() GPU Wait the encoder execution
         // behind it

         pD3D12Enc->base.context->buffer_subdata(
            pD3D12Enc->base.context,         // context
            &pD3D12Enc->m_pOutputBitstreamBuffers[0/*first slice buffer*/]->base.b, // dst buffer
            PIPE_MAP_WRITE,                  // usage PIPE_MAP_x
            0,                               // offset
            static_cast<unsigned int>(pD3D12Enc->m_BitstreamHeadersBuffer.size()),
            pD3D12Enc->m_BitstreamHeadersBuffer.data());

         if (!d3d12_buffer_maps_directly(&pD3D12Enc->m_pOutputBitstreamBuffers[0/*first slice buffer*/]->base.b)) // If the buffer maps directly, the buffer_subdata is synchronous on unmap, no need to flush
         {
            // If the destination buffer doesn't map directly (eg. DEFAULT usage), we need to flush
            // and set a fence to ensure the upload is finished before EncodeFrame reads from it

            debug_printf("[d3d12_video_encoder] d3d12_video_encoder_encode_bitstream_impl - Flushing pD3D12Enc->m_BitstreamHeadersBuffer data upload.\n");
            pD3D12Enc->base.context->flush(
               pD3D12Enc->base.context,
               &pD3D12Enc->m_inflightResourcesPool[d3d12_video_encoder_pool_current_index(pD3D12Enc)].headers_upload_completion_fence,
               PIPE_FLUSH_ASYNC | PIPE_FLUSH_HINT_FINISH);

            assert(pD3D12Enc->m_inflightResourcesPool[d3d12_video_encoder_pool_current_index(pD3D12Enc)].headers_upload_completion_fence);
         }
      }
   }
   else
   {
      pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].spStagingBitstreams.resize(num_slice_objects, NULL);
      for (uint32_t slice_idx = 0; slice_idx < num_slice_objects;slice_idx++) {
         assert(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].preEncodeGeneratedHeadersByteSize == 0);

         if ((slice_idx > 0) && !pD3D12Enc->supports_sliced_fences.bits.multiple_buffers_required) {
            // For multi slice notification and multiple_buffers_required = 0, use the same staging for all
            // spStagingBitstreams[] entries
            pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].spStagingBitstreams[slice_idx] =
               pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].spStagingBitstreams[0/*first slice*/];
         } else if (pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].spStagingBitstreams[slice_idx] == nullptr) {
            D3D12_HEAP_PROPERTIES Properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
            CD3DX12_RESOURCE_DESC resolvedMetadataBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(pD3D12Enc->m_MaxOutputBitstreamSize);
            HRESULT hr = pD3D12Enc->m_pD3D12Screen->dev->CreateCommittedResource(
               &Properties,
               D3D12_HEAP_FLAG_NONE,
               &resolvedMetadataBufferDesc,
               D3D12_RESOURCE_STATE_COPY_DEST,
               nullptr,
               IID_PPV_ARGS(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].spStagingBitstreams[slice_idx].GetAddressOf()));

            if (FAILED(hr)) {
               debug_printf("CreateCommittedResource failed with HR %x\n", (unsigned)hr);
               pD3D12Enc->m_inflightResourcesPool[d3d12_video_encoder_pool_current_index(pD3D12Enc)].encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;
               pD3D12Enc->m_spEncodedFrameMetadata[d3d12_video_encoder_metadata_current_index(pD3D12Enc)].encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;
               assert(false);
               return;
            }
         }

         // Headers are written after execution, have EncodeFrame write into a staging buffer
         // and then get_feedback will pack the finalized bitstream and copy into comp_bit_destinations[0 /*first slice*/]
         pD3D12Enc->m_pOutputBufferD3D12Resources[slice_idx] = pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].spStagingBitstreams[slice_idx].Get();
      }
   }

   pD3D12Enc->m_inflightResourcesPool[d3d12_video_encoder_pool_current_index(pD3D12Enc)].
         m_CompletionFence.reset(d3d12_create_fence_raw(pD3D12Enc->m_spFence.Get(), pD3D12Enc->m_fenceValue));

   pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_fence.reset(d3d12_create_fence_raw(pD3D12Enc->m_spFence.Get(), pD3D12Enc->m_fenceValue));

   *feedback = (void*)pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_fence.get();

   pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_LastSliceFence.reset(d3d12_create_fence_raw(pD3D12Enc->m_spLastSliceFence.Get(), pD3D12Enc->m_LastSliceFenceValue));
   d3d12_fence_reference((struct d3d12_fence **)last_slice_completion_fence, pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_LastSliceFence.get());

   pD3D12Enc->m_rgCurrentFrameStateTransitions = {
      // We take advantage of the promotion/decay rules for buffers like the output bitstream buffers
      // but for clarity we still specify the transitions of buffers (e.g metadata opaque) used between
      // ResolveInputData and EncodeFrame or EncodeFrame and ResolveOutputMetadata as read/write dependencies
      CD3DX12_RESOURCE_BARRIER::Transition(pInputVideoD3D12Res,
                                           D3D12_RESOURCE_STATE_COMMON,
                                           D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ),
      CD3DX12_RESOURCE_BARRIER::Transition(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_spMetadataOutputBuffer.Get(),
                                           D3D12_RESOURCE_STATE_COMMON,
                                           D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE)
   };

   pD3D12Enc->m_spEncodeCommandList->ResourceBarrier(static_cast<UINT>(pD3D12Enc->m_rgCurrentFrameStateTransitions.size()),
                                                     pD3D12Enc->m_rgCurrentFrameStateTransitions.data());

   D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE reconPicOutputTextureDesc =
      pD3D12Enc->m_upDPBManager->get_current_frame_recon_pic_output_allocation();
   D3D12_VIDEO_ENCODE_REFERENCE_FRAMES referenceFramesDescriptor =
      pD3D12Enc->m_upDPBManager->get_current_reference_frames();
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_FLAGS picCtrlFlags = D3D12_VIDEO_ENCODER_PICTURE_CONTROL_FLAG_NONE;

   // Transition DPB reference pictures to read mode
   if ((referenceFramesDescriptor.NumTexture2Ds > 0) ||
       (pD3D12Enc->m_upDPBManager->is_current_frame_used_as_reference())) {

      if (reconPicOutputTextureDesc.pReconstructedPicture != nullptr)
         picCtrlFlags |= D3D12_VIDEO_ENCODER_PICTURE_CONTROL_FLAG_USED_AS_REFERENCE_PICTURE;

      // Check if array of textures vs texture array

      if (referenceFramesDescriptor.pSubresources == nullptr) {

         // Reserve allocation for AoT transitions count
         pD3D12Enc->m_rgReferenceTransitions.reserve(static_cast<size_t>(referenceFramesDescriptor.NumTexture2Ds +
            ((reconPicOutputTextureDesc.pReconstructedPicture != nullptr) ? 1u : 0u)));

         // Array of resources mode for reference pictures

         // Transition all subresources of each reference frame independent resource allocation
         for (uint32_t referenceIdx = 0; referenceIdx < referenceFramesDescriptor.NumTexture2Ds; referenceIdx++) {
            if (reconPicOutputTextureDesc.pReconstructedPicture != referenceFramesDescriptor.ppTexture2Ds[referenceIdx]) {
               pD3D12Enc->m_rgReferenceTransitions.push_back(
                  CD3DX12_RESOURCE_BARRIER::Transition(referenceFramesDescriptor.ppTexture2Ds[referenceIdx],
                                                      D3D12_RESOURCE_STATE_COMMON,
                                                      D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ));
            }
         }

         // Transition all subresources the output recon pic independent resource allocation
         if (reconPicOutputTextureDesc.pReconstructedPicture != nullptr) {
            pD3D12Enc->m_rgReferenceTransitions.push_back(
               CD3DX12_RESOURCE_BARRIER::Transition(reconPicOutputTextureDesc.pReconstructedPicture,
                                                    D3D12_RESOURCE_STATE_COMMON,
                                                    D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE));
         }
      } else if (referenceFramesDescriptor.NumTexture2Ds > 0) {

         // texture array mode for reference pictures

         // In Texture array mode, the dpb storage allocator uses the same texture array for all the input
         // reference pics in ppTexture2Ds and also for the pReconstructedPicture output allocations, just different
         // subresources.

         CD3DX12_RESOURCE_DESC referencesTexArrayDesc(GetDesc(referenceFramesDescriptor.ppTexture2Ds[0]));

#if MESA_DEBUG
   // the reconpic output should be all the same texarray allocation
   if((reconPicOutputTextureDesc.pReconstructedPicture) && (referenceFramesDescriptor.NumTexture2Ds > 0))
      assert(referenceFramesDescriptor.ppTexture2Ds[0] == reconPicOutputTextureDesc.pReconstructedPicture);

   for (uint32_t refIndex = 0; refIndex < referenceFramesDescriptor.NumTexture2Ds; refIndex++) {
            // all reference frames inputs should be all the same texarray allocation
            assert(referenceFramesDescriptor.ppTexture2Ds[0] ==
                   referenceFramesDescriptor.ppTexture2Ds[refIndex]);
   }
#endif

         // Reserve allocation for texture array transitions count
         pD3D12Enc->m_rgReferenceTransitions.reserve(
            static_cast<size_t>(pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.PlaneCount * referencesTexArrayDesc.DepthOrArraySize));

         for (uint32_t referenceSubresource = 0; referenceSubresource < referencesTexArrayDesc.DepthOrArraySize;
              referenceSubresource++) {

            uint32_t MipLevel, PlaneSlice, ArraySlice;
            D3D12DecomposeSubresource(referenceSubresource,
                                      referencesTexArrayDesc.MipLevels,
                                      referencesTexArrayDesc.ArraySize(),
                                      MipLevel,
                                      ArraySlice,
                                      PlaneSlice);

            for (PlaneSlice = 0; PlaneSlice < pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.PlaneCount;
                 PlaneSlice++) {

               uint32_t planeOutputSubresource =
                  referencesTexArrayDesc.CalcSubresource(MipLevel, ArraySlice, PlaneSlice);

               pD3D12Enc->m_rgReferenceTransitions.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
                  // Always same allocation in texarray mode
                  referenceFramesDescriptor.ppTexture2Ds[0],
                  D3D12_RESOURCE_STATE_COMMON,
                  // If this is the subresource for the reconpic output allocation, transition to ENCODE_WRITE
                  // Otherwise, it's a subresource for an input reference picture, transition to ENCODE_READ
                  (referenceSubresource == reconPicOutputTextureDesc.ReconstructedPictureSubresource) ?
                     D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE :
                     D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ,
                  planeOutputSubresource));
            }
         }
      }

      if (pD3D12Enc->m_rgReferenceTransitions.size() > 0) {
         pD3D12Enc->m_spEncodeCommandList->ResourceBarrier(static_cast<uint32_t>(pD3D12Enc->m_rgReferenceTransitions.size()),
                                                           pD3D12Enc->m_rgReferenceTransitions.data());
      }
   }

   // Update current frame pic params state after reconfiguring above.
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA1 currentPicParams =
      d3d12_video_encoder_get_current_picture_param_settings(pD3D12Enc);

   if (!pD3D12Enc->m_upDPBManager->get_current_frame_picture_control_data(currentPicParams)) {
      debug_printf("[d3d12_video_encoder_encode_bitstream] get_current_frame_picture_control_data failed!\n");
      pD3D12Enc->m_inflightResourcesPool[d3d12_video_encoder_pool_current_index(pD3D12Enc)].encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;
      pD3D12Enc->m_spEncodedFrameMetadata[d3d12_video_encoder_metadata_current_index(pD3D12Enc)].encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;
      assert(false);
      return;
   }

   // Stores D3D12_VIDEO_ENCODER_AV1_REFERENCE_PICTURE_DESCRIPTOR in the associated metadata
   // for header generation after execution (if applicable)
   if (pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].postEncodeHeadersNeeded) {
      d3d12_video_encoder_store_current_picture_references(pD3D12Enc, current_metadata_slot);
   }

   //
   // Prepare any additional slice/tile headers
   //
   uint64_t sliceHeadersSize = 0u; // To pass to IHV driver for rate control budget hint
   pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSliceHeaders.resize(pD3D12Enc->m_currentEncodeCapabilities.m_MaxSlicesInOutput, {});
#if VIDEO_CODEC_H264ENC
   // Add H264 temporal layers slice nal prefixes if necessary
   if ((D3D12_VIDEO_ENCODER_CODEC_H264 == pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_associatedEncodeConfig.m_encoderCodecDesc)
         && (pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_associatedEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_svcprefix_slice_header)
         && (pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_associatedEncodeConfig.m_encoderCodecSpecificSequenceStateDescH264.num_temporal_layers > 1))
   {
      size_t written_prefix_nal_bytes = 0;
      std::vector<uint8_t> pSVCNalPayload;
      d3d12_video_encoder_build_slice_svc_prefix_nalu_h264(pD3D12Enc,
                                                           pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot],
                                                           pSVCNalPayload,
                                                           pSVCNalPayload.begin(),
                                                           written_prefix_nal_bytes);

      for (uint32_t slice_idx = 0; slice_idx < pD3D12Enc->m_currentEncodeCapabilities.m_MaxSlicesInOutput; slice_idx++)
      {
         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSliceHeaders[slice_idx].resize(1u);
         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSliceHeaders[slice_idx][0u] = { NAL_TYPE_PREFIX, pSVCNalPayload };
         sliceHeadersSize += pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSliceHeaders[slice_idx][0u].buffer.size();
      }
   }
#endif

   // Use cached ID3D12VideoEncodeCommandList4 interfaces if available
   if (pD3D12Enc->m_spEncodeCommandList4 && pD3D12Enc->m_spResolveCommandList4) {

      // Update current frame pic params state after reconfiguring above.
      D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA1 currentPicParams =
         d3d12_video_encoder_get_current_picture_param_settings(pD3D12Enc);

      if (!pD3D12Enc->m_upDPBManager->get_current_frame_picture_control_data(currentPicParams)) {
         debug_printf("[d3d12_video_encoder_encode_bitstream] get_current_frame_picture_control_data failed!\n");
         pD3D12Enc->m_inflightResourcesPool[d3d12_video_encoder_pool_current_index(pD3D12Enc)].encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;
         pD3D12Enc->m_spEncodedFrameMetadata[d3d12_video_encoder_metadata_current_index(pD3D12Enc)].encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;
         assert(false);
         return;
      }

      D3D12_VIDEO_ENCODER_DIRTY_REGIONS dirtyRegions = { };
      dirtyRegions.MapSource = pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapSource;

      if (d3d12_video_encoder_is_dirty_regions_feature_enabled(pD3D12Enc, dirtyRegions.MapSource))
      {
         picCtrlFlags |= D3D12_VIDEO_ENCODER_PICTURE_CONTROL_FLAG_ENABLE_DIRTY_REGIONS_INPUT;
         if (dirtyRegions.MapSource == D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_CPU_BUFFER)
         {
            dirtyRegions.pCPUBuffer = &pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.RectsInfo;
            if (pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.FullFrameIdentical)
            {
               // When this parameter is TRUE, pDirtyRects must be NULL and the driver will interpret it
               // as a dirty regions map being present and an all-zero matrix in mode D3D12_VIDEO_ENCODER_DIRTY_REGIONS_MAP_VALUES_MODE_DIRTY.
               dirtyRegions.pCPUBuffer->pDirtyRects = NULL;
            }
         }
         else if (dirtyRegions.MapSource == D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_GPU_TEXTURE)
         {
            dirtyRegions.pOpaqueLayoutBuffer = pD3D12Enc->m_inflightResourcesPool[d3d12_video_encoder_pool_current_index(pD3D12Enc)].m_spDirtyRectsResolvedOpaqueMap.Get();

            pD3D12Enc->m_pResolveInputDataBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(dirtyRegions.pOpaqueLayoutBuffer,
                                                                                    D3D12_RESOURCE_STATE_COMMON,
                                                                                    D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE));

            if (pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.InputMap)
            {
               assert(!pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.FullFrameIdentical); // When this parameter is TRUE, pDirtyRegionsMap must be NULL
               pD3D12Enc->m_pResolveInputDataBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(d3d12_resource_resource(pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.InputMap),
                                                                                       D3D12_RESOURCE_STATE_COMMON,
                                                                                       D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ));
            }

            // see below std::warp for reversal to common after ResolveInputParamLayout is done
            pD3D12Enc->m_spEncodeCommandList4->ResourceBarrier(static_cast<uint32_t>(pD3D12Enc->m_pResolveInputDataBarriers.size()),
                                                                                    pD3D12Enc->m_pResolveInputDataBarriers.data());
            D3D12_VIDEO_ENCODER_INPUT_MAP_DATA ResolveInputData = {};
            ResolveInputData.MapType = D3D12_VIDEO_ENCODER_INPUT_MAP_TYPE_DIRTY_REGIONS;
            ResolveInputData.DirtyRegions.FullFrameIdentical = pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.FullFrameIdentical;
            ResolveInputData.DirtyRegions.pDirtyRegionsMap = pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.FullFrameIdentical ? NULL : d3d12_resource_resource(pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.InputMap);
            ResolveInputData.DirtyRegions.MapValuesType = pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.MapValuesType;
            ResolveInputData.DirtyRegions.SourceDPBFrameReference = pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.SourceDPBFrameReference;
            D3D12_VIDEO_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT_INPUT_ARGUMENTS resolveInputParamLayoutInput =
            {
               pD3D12Enc->m_currentEncodeConfig.m_DirtyRectsDesc.MapInfo.capInputLayoutDirtyRegion.SessionInfo,
               ResolveInputData,
            };
            D3D12_VIDEO_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT_OUTPUT_ARGUMENTS resolveInputParamLayoutOutput =
            {
               dirtyRegions.pOpaqueLayoutBuffer,
            };

            pD3D12Enc->m_spEncodeCommandList4->ResolveInputParamLayout(&resolveInputParamLayoutInput, &resolveInputParamLayoutOutput);
            for (auto &BarrierDesc : pD3D12Enc->m_pResolveInputDataBarriers) {
               std::swap(BarrierDesc.Transition.StateBefore, BarrierDesc.Transition.StateAfter);
            }
            pD3D12Enc->m_spEncodeCommandList4->ResourceBarrier(static_cast<uint32_t>(pD3D12Enc->m_pResolveInputDataBarriers.size()),
                                                            pD3D12Enc->m_pResolveInputDataBarriers.data());
         }
      }

      D3D12_VIDEO_ENCODER_QUANTIZATION_OPAQUE_MAP QuantizationTextureMap = {};
      BOOL QPMapEnabled = false;
      D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE QPMapSource = D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_CPU_BUFFER;
      d3d12_video_encoder_is_gpu_qmap_input_feature_enabled(pD3D12Enc, /*output param*/ QPMapEnabled, /*output param*/ QPMapSource);
      if (QPMapEnabled && (QPMapSource == D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_GPU_TEXTURE))
      {
         picCtrlFlags |= D3D12_VIDEO_ENCODER_PICTURE_CONTROL_FLAG_ENABLE_QUANTIZATION_MATRIX_INPUT;
         QuantizationTextureMap.pOpaqueQuantizationMap = pD3D12Enc->m_inflightResourcesPool[d3d12_video_encoder_pool_current_index(pD3D12Enc)].m_spQPMapResolvedOpaqueMap.Get();

         pD3D12Enc->m_pResolveInputDataBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(QuantizationTextureMap.pOpaqueQuantizationMap,
                                                                                 D3D12_RESOURCE_STATE_COMMON,
                                                                                 D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE));

         pD3D12Enc->m_pResolveInputDataBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(d3d12_resource_resource(pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.GPUInput.InputMap),
                                                                                 D3D12_RESOURCE_STATE_COMMON,
                                                                                 D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ));

         // see below std::warp for reversal to common after ResolveInputParamLayout is done
         pD3D12Enc->m_spEncodeCommandList4->ResourceBarrier(static_cast<uint32_t>(pD3D12Enc->m_pResolveInputDataBarriers.size()),
                                                                                 pD3D12Enc->m_pResolveInputDataBarriers.data());
         D3D12_VIDEO_ENCODER_INPUT_MAP_DATA ResolveInputData = {};
         ResolveInputData.MapType = D3D12_VIDEO_ENCODER_INPUT_MAP_TYPE_QUANTIZATION_MATRIX;
         ResolveInputData.Quantization.pQuantizationMap = d3d12_resource_resource(pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.GPUInput.InputMap);
         D3D12_VIDEO_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT_INPUT_ARGUMENTS resolveInputParamLayoutInput =
         {
            pD3D12Enc->m_currentEncodeConfig.m_QuantizationMatrixDesc.GPUInput.capInputLayoutQPMap.SessionInfo,
            ResolveInputData,
         };
         D3D12_VIDEO_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT_OUTPUT_ARGUMENTS resolveInputParamLayoutOutput =
         {
            QuantizationTextureMap.pOpaqueQuantizationMap,
         };

         pD3D12Enc->m_spEncodeCommandList4->ResolveInputParamLayout(&resolveInputParamLayoutInput, &resolveInputParamLayoutOutput);
         for (auto &BarrierDesc : pD3D12Enc->m_pResolveInputDataBarriers) {
            std::swap(BarrierDesc.Transition.StateBefore, BarrierDesc.Transition.StateAfter);
         }
         pD3D12Enc->m_spEncodeCommandList4->ResourceBarrier(static_cast<uint32_t>(pD3D12Enc->m_pResolveInputDataBarriers.size()),
                                                         pD3D12Enc->m_pResolveInputDataBarriers.data());
      }

      D3D12_VIDEO_ENCODER_FRAME_MOTION_VECTORS motionRegions = { };
      motionRegions.MapSource = pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapSource;
      if (d3d12_video_encoder_is_move_regions_feature_enabled(pD3D12Enc, motionRegions.MapSource))
      {
         picCtrlFlags |= D3D12_VIDEO_ENCODER_PICTURE_CONTROL_FLAG_ENABLE_MOTION_VECTORS_INPUT;
         if (motionRegions.MapSource == D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_CPU_BUFFER)
         {
            motionRegions.MapSource = D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_CPU_BUFFER;
            motionRegions.pCPUBuffer = &pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.RectsInfo;
         }
         else if (motionRegions.MapSource == D3D12_VIDEO_ENCODER_INPUT_MAP_SOURCE_GPU_TEXTURE)
         {
            motionRegions.pOpaqueLayoutBuffer = pD3D12Enc->m_inflightResourcesPool[d3d12_video_encoder_pool_current_index(pD3D12Enc)].m_spMotionVectorsResolvedOpaqueMap.Get();
            pD3D12Enc->m_pResolveInputDataBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(motionRegions.pOpaqueLayoutBuffer,
                                                                                    D3D12_RESOURCE_STATE_COMMON,
                                                                                    D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE));

            for (unsigned i = 0; i < pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.NumHintsPerPixel; i++)
            {
               pD3D12Enc->m_pResolveInputDataBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.ppMotionVectorMaps[i],
                                                                                       D3D12_RESOURCE_STATE_COMMON,
                                                                                       D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ));
               pD3D12Enc->m_pResolveInputDataBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.ppMotionVectorMapsMetadata[i],
                                                                                       D3D12_RESOURCE_STATE_COMMON,
                                                                                       D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ));
            }

            // see below std::swap for reversal to common after ResolveInputParamLayout is done
            pD3D12Enc->m_spEncodeCommandList4->ResourceBarrier(static_cast<uint32_t>(pD3D12Enc->m_pResolveInputDataBarriers.size()),
                                                                                    pD3D12Enc->m_pResolveInputDataBarriers.data());
            D3D12_VIDEO_ENCODER_INPUT_MAP_DATA ResolveInputData = {};
            ResolveInputData.MapType = D3D12_VIDEO_ENCODER_INPUT_MAP_TYPE_MOTION_VECTORS;
            ResolveInputData.MotionVectors.MotionSearchModeConfiguration = pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.MotionSearchModeConfiguration;
            ResolveInputData.MotionVectors.NumHintsPerPixel = pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.NumHintsPerPixel;
            ResolveInputData.MotionVectors.ppMotionVectorMaps = pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.ppMotionVectorMaps.data();
            ResolveInputData.MotionVectors.ppMotionVectorMapsMetadata = pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.ppMotionVectorMapsMetadata.data();
            ResolveInputData.MotionVectors.pMotionVectorMapsSubresources = pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.pMotionVectorMapsSubresources;
            ResolveInputData.MotionVectors.pMotionVectorMapsMetadataSubresources = pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.pMotionVectorMapsMetadataSubresources;
            ResolveInputData.MotionVectors.MotionUnitPrecision = pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.MotionUnitPrecision;
            ResolveInputData.MotionVectors.PictureControlConfiguration = currentPicParams;

            D3D12_VIDEO_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT_INPUT_ARGUMENTS resolveInputParamLayoutInput =
            {
               pD3D12Enc->m_currentEncodeConfig.m_MoveRectsDesc.MapInfo.capInputLayoutMotionVectors.SessionInfo,
               ResolveInputData,
            };
            D3D12_VIDEO_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT_OUTPUT_ARGUMENTS resolveInputParamLayoutOutput =
            {
               motionRegions.pOpaqueLayoutBuffer,
            };

            pD3D12Enc->m_spEncodeCommandList4->ResolveInputParamLayout(&resolveInputParamLayoutInput, &resolveInputParamLayoutOutput);
            for (auto &BarrierDesc : pD3D12Enc->m_pResolveInputDataBarriers) {
               std::swap(BarrierDesc.Transition.StateBefore, BarrierDesc.Transition.StateAfter);
            }
            pD3D12Enc->m_spEncodeCommandList4->ResourceBarrier(static_cast<uint32_t>(pD3D12Enc->m_pResolveInputDataBarriers.size()),
                                                            pD3D12Enc->m_pResolveInputDataBarriers.data());
         }
      }

      ID3D12Resource* d12_gpu_stats_qp_map = NULL;
      D3D12_VIDEO_ENCODER_OPTIONAL_METADATA_ENABLE_FLAGS optionalMetadataFlags = D3D12_VIDEO_ENCODER_OPTIONAL_METADATA_ENABLE_FLAG_NONE;
      if (pD3D12Enc->m_currentEncodeConfig.m_GPUQPStatsResource) {
         optionalMetadataFlags |= D3D12_VIDEO_ENCODER_OPTIONAL_METADATA_ENABLE_FLAG_QP_MAP;
         d3d12_promote_to_permanent_residency(pD3D12Enc->m_pD3D12Screen, &pD3D12Enc->m_currentEncodeConfig.m_GPUQPStatsResource,
                                              1, pD3D12Enc->m_spResidencyFence.Get(), &pD3D12Enc->m_ResidencyFenceValue);
         d12_gpu_stats_qp_map = d3d12_resource_resource(pD3D12Enc->m_currentEncodeConfig.m_GPUQPStatsResource);
      }

      ID3D12Resource* d12_gpu_stats_satd_map = NULL;
      if (pD3D12Enc->m_currentEncodeConfig.m_GPUSATDStatsResource) {
         optionalMetadataFlags |= D3D12_VIDEO_ENCODER_OPTIONAL_METADATA_ENABLE_FLAG_SATD_MAP;
         d3d12_promote_to_permanent_residency(pD3D12Enc->m_pD3D12Screen, &pD3D12Enc->m_currentEncodeConfig.m_GPUSATDStatsResource,
                                              1, pD3D12Enc->m_spResidencyFence.Get(), &pD3D12Enc->m_ResidencyFenceValue);
         d12_gpu_stats_satd_map = d3d12_resource_resource(pD3D12Enc->m_currentEncodeConfig.m_GPUSATDStatsResource);
      }

      ID3D12Resource* d12_gpu_stats_rc_bitallocation_map = NULL;
      if (pD3D12Enc->m_currentEncodeConfig.m_GPURCBitAllocationStatsResource) {
         optionalMetadataFlags |= D3D12_VIDEO_ENCODER_OPTIONAL_METADATA_ENABLE_FLAG_RC_BIT_ALLOCATION_MAP;
         d3d12_promote_to_permanent_residency(pD3D12Enc->m_pD3D12Screen, &pD3D12Enc->m_currentEncodeConfig.m_GPURCBitAllocationStatsResource,
                                              1, pD3D12Enc->m_spResidencyFence.Get(), &pD3D12Enc->m_ResidencyFenceValue);
         d12_gpu_stats_rc_bitallocation_map = d3d12_resource_resource(pD3D12Enc->m_currentEncodeConfig.m_GPURCBitAllocationStatsResource);
      }

      ID3D12Resource* d12_gpu_stats_psnr = NULL;
      if (pD3D12Enc->m_currentEncodeConfig.m_GPUPSNRAllocationStatsResource) {
         optionalMetadataFlags |= D3D12_VIDEO_ENCODER_OPTIONAL_METADATA_ENABLE_FLAG_FRAME_PSNR;
         d3d12_promote_to_permanent_residency(pD3D12Enc->m_pD3D12Screen, &pD3D12Enc->m_currentEncodeConfig.m_GPUPSNRAllocationStatsResource,
                                              1, pD3D12Enc->m_spResidencyFence.Get(), &pD3D12Enc->m_ResidencyFenceValue);
         d12_gpu_stats_psnr = d3d12_resource_resource(pD3D12Enc->m_currentEncodeConfig.m_GPUPSNRAllocationStatsResource);
      }

      D3D12_VIDEO_ENCODER_FRAME_ANALYSIS FrameAnalysis = {};
      D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE FrameAnalysisReconstructedPicture = {};

      if ((pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.AppRequested) &&
         (!pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.bSkipTwoPassInCurrentFrame))
      {
         // Validate two pass is enabled in RC flags
         assert((pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex].m_Flags &
                 D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_FRAME_ANALYSIS) != 0u);

         //
         // When Pow2DownscaleFactor is zero, is full resolution two pass, which leaves FrameAnalysis empty/zero filled.
         // For lower 1st pass resolution, we fill FrameAnalysis appropiately
         //         

         if (pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.Pow2DownscaleFactor > 0)
         {
            //
            // Schedule barrier transitions (reverse ones are scheduled later by doing swap to pTwoPassExtraBarriers)
            //

            if (pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.pDownscaledInputTexture)
            {
                 pD3D12Enc->m_pTwoPassExtraBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
                                                 pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.pDownscaledInputTexture,
                                                 D3D12_RESOURCE_STATE_COMMON,
                                                 D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ));
            }

            if (pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pResources.size() > 0)
            {
               if ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                   D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RECONSTRUCTED_FRAMES_REQUIRE_TEXTURE_ARRAYS) != 0)
               {
                  pD3D12Enc->m_pTwoPassExtraBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
                                                  pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pResources[0],
                                                  D3D12_RESOURCE_STATE_COMMON,
                                                  D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ));
               }
               else
               {
                  for (unsigned i = 0; i < pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pResources.size(); i++)
                     pD3D12Enc->m_pTwoPassExtraBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
                                                     pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pResources[i],
                                                     D3D12_RESOURCE_STATE_COMMON,
                                                     D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ));
               }
            }

            if (pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.FrameAnalysisReconstructedPictureOutput.pReconstructedPicture) // can be NULL if external dpb scaling
            {
               pD3D12Enc->m_pTwoPassExtraBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
                                             pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.FrameAnalysisReconstructedPictureOutput.pReconstructedPicture,
                                             D3D12_RESOURCE_STATE_COMMON,
                                             D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE));
            }

            //
            // Set EncodeFrame params
            //

            FrameAnalysisReconstructedPicture = pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.FrameAnalysisReconstructedPictureOutput;

            FrameAnalysis =
            {
               // ID3D12Resource *pDownscaledFrame;
               pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.pDownscaledInputTexture,
               // UINT64 Subresource;
               0u,
               // D3D12_VIDEO_ENCODE_REFERENCE_FRAMES DownscaledReferences;
               {
                  // UINT NumTexture2Ds;
                  static_cast<UINT>(pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pResources.size()),
                  // _Field_size_full_(NumTexture2Ds)  ID3D12Resource **ppTexture2Ds;
                  pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pResources.data(),
                  // _Field_size_full_(NumTexture2Ds)  UINT *pSubresources;
                  pD3D12Enc->m_currentEncodeConfig.m_TwoPassEncodeDesc.DownscaledReferences.pSubresources.data(),
               },
            };
         }

         pD3D12Enc->m_spEncodeCommandList4->ResourceBarrier(static_cast<uint32_t>(pD3D12Enc->m_pTwoPassExtraBarriers.size()),
                                               pD3D12Enc->m_pTwoPassExtraBarriers.data());
      }

      const D3D12_VIDEO_ENCODER_ENCODEFRAME_INPUT_ARGUMENTS1 inputStreamArguments = {
         // D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_DESC
         { // D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAGS
         pD3D12Enc->m_currentEncodeConfig.m_seqFlags,
         // D3D12_VIDEO_ENCODER_INTRA_REFRESH
         pD3D12Enc->m_currentEncodeConfig.m_IntraRefresh,
         d3d12_video_encoder_get_current_rate_control_settings(pD3D12Enc),
         // D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC
         pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
         pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode,
         d3d12_video_encoder_get_current_slice_param_settings(pD3D12Enc),
         d3d12_video_encoder_get_current_gop_desc(pD3D12Enc) },
         // D3D12_VIDEO_ENCODER_PICTURE_CONTROL_DESC1
         { // uint32_t IntraRefreshFrameIndex;
         pD3D12Enc->m_currentEncodeConfig.m_IntraRefreshCurrentFrameIndex,
         // D3D12_VIDEO_ENCODER_PICTURE_CONTROL_FLAGS Flags;
         picCtrlFlags,
         // D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA1 PictureControlCodecData;
         currentPicParams,
         // D3D12_VIDEO_ENCODE_REFERENCE_FRAMES ReferenceFrames;
         referenceFramesDescriptor,
            // D3D12_VIDEO_ENCODER_FRAME_MOTION_VECTORS MotionVectors;
            motionRegions,
            // D3D12_VIDEO_ENCODER_DIRTY_REGIONS DirtyRects;
            dirtyRegions,
            // D3D12_VIDEO_ENCODER_QUANTIZATION_OPAQUE_MAP QuantizationTextureMap;
            QuantizationTextureMap,
            // D3D12_VIDEO_ENCODER_FRAME_ANALYSIS FrameAnalysis;
            FrameAnalysis,
         },
         pInputVideoD3D12Res,
         inputVideoD3D12Subresource,
         static_cast<UINT>(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].preEncodeGeneratedHeadersByteSize + sliceHeadersSize),
         // budgeting. - User can also calculate headers fixed size beforehand (eg. no VUI,
         // etc) and build them with final values after EncodeFrame is executed
         // D3D12_VIDEO_ENCODER_OPTIONAL_METADATA_ENABLE_FLAGS OptionalMetadata;
         optionalMetadataFlags, // must match with ResolveEncodeOutputMetadata flags
      };

      //
      // Configure the encoder notification mode
      //

      pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSubregionPipeFences.clear();
      pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppSubregionFenceValues.clear();
      pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppSubregionSizes.clear();
      pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppSubregionOffsets.clear();
      pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppSubregionFences.clear();
      pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppResolvedSubregionSizes.clear();
      pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppResolvedSubregionOffsets.clear();
      pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSubregionBitstreamsBaseOffsets.clear();

      D3D12_VIDEO_ENCODER_COMPRESSED_BITSTREAM1 bitstreamArgs = { };
      if (num_slice_objects > 1)
      {
         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].SubregionNotificationMode = D3D12_VIDEO_ENCODER_COMPRESSED_BITSTREAM_NOTIFICATION_MODE_SUBREGIONS;
         bitstreamArgs.NotificationMode = D3D12_VIDEO_ENCODER_COMPRESSED_BITSTREAM_NOTIFICATION_MODE_SUBREGIONS;

         //
         // Prefer individual slice buffers when possible
         //
         D3D12_VIDEO_ENCODER_SUBREGION_COMPRESSED_BITSTREAM_BUFFER_MODE slicedEncodeBufferMode = D3D12_VIDEO_ENCODER_SUBREGION_COMPRESSED_BITSTREAM_BUFFER_MODE_ARRAY_OF_BUFFERS;
         if (pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
            D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SUBREGION_NOTIFICATION_ARRAY_OF_BUFFERS_AVAILABLE)
         {
            slicedEncodeBufferMode = D3D12_VIDEO_ENCODER_SUBREGION_COMPRESSED_BITSTREAM_BUFFER_MODE_ARRAY_OF_BUFFERS;
         }
         else if (pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
            D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SUBREGION_NOTIFICATION_SINGLE_BUFFER_AVAILABLE)
         {
            slicedEncodeBufferMode = D3D12_VIDEO_ENCODER_SUBREGION_COMPRESSED_BITSTREAM_BUFFER_MODE_SINGLE_BUFFER;
   #if MESA_DEBUG
            for (uint32_t i = 0; i < num_slice_objects;i++)
               assert(pD3D12Enc->m_pOutputBufferD3D12Resources[i] == pD3D12Enc->m_pOutputBufferD3D12Resources[0]);
   #endif
         }
         else
         {
            debug_printf("User requested sliced encoding, but there is no HW support for it (PIPE_VIDEO_CAP_ENC_SLICED_NOTIFICATIONS)\n");
            assert(pD3D12Enc->supports_sliced_fences.bits.supported);
            pD3D12Enc->m_inflightResourcesPool[d3d12_video_encoder_pool_current_index(pD3D12Enc)].encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;
            pD3D12Enc->m_spEncodedFrameMetadata[pD3D12Enc->m_fenceValue % pD3D12Enc->m_MaxMetadataBuffersCount].encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;
            assert(false);
            return;
         }

         //
         // Create sizes and offsets results buffers
         //
         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pspSubregionSizes.resize(num_slice_objects, {});
         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pspSubregionOffsets.resize(num_slice_objects, {});
         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pspSubregionFences.resize(num_slice_objects, NULL);
         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSubregionPipeFences.resize(num_slice_objects);
         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppSubregionFenceValues.resize(num_slice_objects, pD3D12Enc->m_fenceValue);

         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppSubregionSizes.resize(num_slice_objects, NULL);
         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppSubregionOffsets.resize(num_slice_objects, NULL);
         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppSubregionFences.resize(num_slice_objects, NULL);
         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppResolvedSubregionSizes.resize(num_slice_objects, 0u);
         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppResolvedSubregionOffsets.resize(num_slice_objects, 0u);
         D3D12_HEAP_PROPERTIES Properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
         [[maybe_unused]] HRESULT hr = S_OK;
         // pD3D12Enc->m_pSlicedEncodingExtraBarriers.resize(num_slice_objects);
         for (uint32_t i = 0; i < num_slice_objects;i++)
         {
            if ((pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pspSubregionOffsets[i] == nullptr) ||
               (GetDesc(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pspSubregionOffsets[i].Get()).Width < num_slice_objects * sizeof(UINT64)))
            {
               pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pspSubregionOffsets[i].Reset();
               CD3DX12_RESOURCE_DESC subregionOffsetsDesc = CD3DX12_RESOURCE_DESC::Buffer(num_slice_objects * sizeof(UINT64));
               hr = pD3D12Enc->m_pD3D12Screen->dev->CreateCommittedResource(&Properties,
                  D3D12_HEAP_FLAG_NONE,
                  &subregionOffsetsDesc,
                  D3D12_RESOURCE_STATE_COPY_DEST,
                  nullptr,
                  IID_PPV_ARGS(&pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pspSubregionOffsets[i]));
            }

            pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppSubregionOffsets[i] = pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pspSubregionOffsets[i].Get();

            if ((pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pspSubregionSizes[i] == nullptr) ||
               (GetDesc(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pspSubregionSizes[i].Get()).Width < num_slice_objects * sizeof(UINT64)))
            {
               pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pspSubregionSizes[i].Reset();
               CD3DX12_RESOURCE_DESC subregionSizesDesc = CD3DX12_RESOURCE_DESC::Buffer(num_slice_objects * sizeof(UINT64));
               hr = pD3D12Enc->m_pD3D12Screen->dev->CreateCommittedResource(&Properties,
                  D3D12_HEAP_FLAG_NONE,
                  &subregionSizesDesc,
                  D3D12_RESOURCE_STATE_COPY_DEST,
                  nullptr,
                  IID_PPV_ARGS(&pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pspSubregionSizes[i]));
            }

            pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppSubregionSizes[i] = pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pspSubregionSizes[i].Get();

            if (pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pspSubregionFences[i] == nullptr)
               hr = pD3D12Enc->m_pD3D12Screen->dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pspSubregionFences[i]));
            pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppSubregionFences[i] = pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pspSubregionFences[i].Get();

            pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSubregionPipeFences[i].reset(
               d3d12_create_fence_raw(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pspSubregionFences[i].Get(),
                                      pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppSubregionFenceValues[i]));

            d3d12_fence_reference((struct d3d12_fence **)&slice_fences[i], pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSubregionPipeFences[i].get());
         }

         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSubregionBitstreamsBaseOffsets.resize(num_slice_objects, 0u);
         // Set the first slice buffer offset since we may have uploaded SPS/PPS etc in there
         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSubregionBitstreamsBaseOffsets[0] =
            pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].preEncodeGeneratedHeadersByteSize;

         //
         // Reserve space on each slice base offset for any generated slice headers
         //
         for (uint32_t slice_idx = 0; slice_idx < num_slice_objects; slice_idx++)
            for (auto& nal : pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSliceHeaders[slice_idx])
               pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSubregionBitstreamsBaseOffsets[slice_idx] += static_cast<uint64_t>(nal.buffer.size());

         // D3D12_VIDEO_ENCODER_SUBREGION_COMPRESSED_BITSTREAM
         bitstreamArgs.SubregionOutputBuffers =
         {
            slicedEncodeBufferMode,
            num_slice_objects,
            pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSubregionBitstreamsBaseOffsets.data(),
            pD3D12Enc->m_pOutputBufferD3D12Resources.data(),
            pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppSubregionSizes.data(),
            pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppSubregionOffsets.data(),
            pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppSubregionFences.data(),
            pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].ppSubregionFenceValues.data()
         };

      }
      else if (num_slice_objects == 1)
      {
         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].SubregionNotificationMode = D3D12_VIDEO_ENCODER_COMPRESSED_BITSTREAM_NOTIFICATION_MODE_FULL_FRAME;
         bitstreamArgs.NotificationMode = D3D12_VIDEO_ENCODER_COMPRESSED_BITSTREAM_NOTIFICATION_MODE_FULL_FRAME;
         // D3D12_VIDEO_ENCODER_COMPRESSED_BITSTREAM
         bitstreamArgs.FrameOutputBuffer =
         {
            pD3D12Enc->m_pOutputBufferD3D12Resources[0],
            pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].preEncodeGeneratedHeadersByteSize,
         };
      }

      const D3D12_VIDEO_ENCODER_ENCODEFRAME_OUTPUT_ARGUMENTS1 outputStreamArguments = {
         // D3D12_VIDEO_ENCODER_COMPRESSED_BITSTREAM1
         bitstreamArgs,
         // D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE
         reconPicOutputTextureDesc,
         // D3D12_VIDEO_ENCODER_ENCODE_OPERATION_METADATA_BUFFER
         { pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_spMetadataOutputBuffer.Get(), 0 },
         // D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE FrameAnalysisReconstructedPicture;
         FrameAnalysisReconstructedPicture,
      };

      debug_printf("DX12 EncodeFrame submission fenceValue %" PRIu64 " current_metadata_slot %" PRIu64 " - POC %d picture_type %s LayoutMode %d SlicesCount %d IRMode %d IRIndex %d\n",
                  pD3D12Enc->m_fenceValue,
                  static_cast<uint64_t>(current_metadata_slot),
                  inputStreamArguments.PictureControlDesc.PictureControlCodecData.pH264PicData->PictureOrderCountNumber,
                  d3d12_video_encoder_friendly_frame_type_h264(inputStreamArguments.PictureControlDesc.PictureControlCodecData.pH264PicData->FrameType),
                  inputStreamArguments.SequenceControlDesc.SelectedLayoutMode,
                  inputStreamArguments.SequenceControlDesc.FrameSubregionsLayoutData.pSlicesPartition_H264 ? inputStreamArguments.SequenceControlDesc.FrameSubregionsLayoutData.pSlicesPartition_H264->NumberOfSlicesPerFrame : 1u,
                  static_cast<uint32_t>(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_associatedEncodeConfig.m_IntraRefresh.Mode),
                  pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_associatedEncodeConfig.m_IntraRefreshCurrentFrameIndex);


      // Record EncodeFrame - use cached ID3D12VideoEncoderHeap1 interface
      // If cached CommandList4 interfaces are available, ID3D12VideoEncoderHeap1 should be available
      pD3D12Enc->m_spEncodeCommandList4->EncodeFrame1(pD3D12Enc->m_spVideoEncoder.Get(),
                                                   pD3D12Enc->m_spVideoEncoderHeap1.Get(),
                                                   &inputStreamArguments,
                                                   &outputStreamArguments);

      pD3D12Enc->m_rgResolveMetadataStateTransitions = {
         CD3DX12_RESOURCE_BARRIER::Transition(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_spMetadataOutputBuffer.Get(),
                                             D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE,
                                             D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ),
         CD3DX12_RESOURCE_BARRIER::Transition(pInputVideoD3D12Res,
                                             D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ,
                                             D3D12_RESOURCE_STATE_COMMON),

      };

      pD3D12Enc->m_spResolveCommandList4->ResourceBarrier(static_cast<uint32_t>(pD3D12Enc->m_rgResolveMetadataStateTransitions.size()),
                                                      pD3D12Enc->m_rgResolveMetadataStateTransitions.data());

      if (d12_gpu_stats_qp_map) {
         pD3D12Enc->m_outputStatsBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(d12_gpu_stats_qp_map,
                                                                              D3D12_RESOURCE_STATE_COMMON,
                                                                              D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE));
      }

      if (d12_gpu_stats_satd_map) {
         pD3D12Enc->m_outputStatsBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(d12_gpu_stats_satd_map,
                                                                              D3D12_RESOURCE_STATE_COMMON,
                                                                              D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE));
      }

      if (d12_gpu_stats_rc_bitallocation_map) {
         pD3D12Enc->m_outputStatsBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(d12_gpu_stats_rc_bitallocation_map,
                                                                              D3D12_RESOURCE_STATE_COMMON,
                                                                              D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE));
      }

      if (d12_gpu_stats_psnr) {
         pD3D12Enc->m_outputStatsBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(d12_gpu_stats_psnr,
                                                                              D3D12_RESOURCE_STATE_COMMON,
                                                                              D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE));
      }

      pD3D12Enc->m_spResolveCommandList4->ResourceBarrier(static_cast<uint32_t>(pD3D12Enc->m_outputStatsBarriers.size()),
                                                      pD3D12Enc->m_outputStatsBarriers.data());
      const D3D12_VIDEO_ENCODER_RESOLVE_METADATA_INPUT_ARGUMENTS1 inputMetadataCmd = {
         pD3D12Enc->m_currentEncodeConfig.m_encoderCodecDesc,
         d3d12_video_encoder_get_current_profile_desc(pD3D12Enc),
         pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
         // D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC
         pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
         { pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_spMetadataOutputBuffer.Get(), 0 },
         // D3D12_VIDEO_ENCODER_OPTIONAL_METADATA_ENABLE_FLAGS OptionalMetadata;
         optionalMetadataFlags, // must match with EncodeFrame flags
         // D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION CodecConfiguration;
         d3d12_video_encoder_get_current_codec_config_desc(pD3D12Enc),
      };

      const D3D12_VIDEO_ENCODER_RESOLVE_METADATA_OUTPUT_ARGUMENTS1 outputMetadataCmd = {
         /*If offset were to change, has to be aligned to pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.EncoderMetadataBufferAccessAlignment*/
         { pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].spBuffer.Get(), 0 },
         // ID3D12Resource *pOutputQPMap;
         d12_gpu_stats_qp_map,
         // ID3D12Resource *pOutputSATDMap;
         d12_gpu_stats_satd_map,
         // ID3D12Resource *pOutputBitAllocationMap;
         d12_gpu_stats_rc_bitallocation_map,
         // D3D12_VIDEO_ENCODER_ENCODE_OPERATION_METADATA_BUFFER ResolvedFramePSNRData;
         {
         // ID3D12Resource *pBuffer;
            d12_gpu_stats_psnr,
         // UINT64 Offset;
            0u,
         },
         // D3D12_VIDEO_ENCODER_ENCODE_OPERATION_METADATA_BUFFER ResolvedSubregionsPSNRData;
         {},
      };

      pD3D12Enc->m_spResolveCommandList4->ResolveEncoderOutputMetadata1(&inputMetadataCmd, &outputMetadataCmd);

      debug_printf("[d3d12_video_encoder_encode_bitstream] EncodeFrame slot %" PRIu64 " encoder %p encoderheap %p input tex %p output bitstream %p raw metadata buf %p resolved metadata buf %p Command allocator %p\n",
                  static_cast<uint64_t>(d3d12_video_encoder_pool_current_index(pD3D12Enc)),
                  pD3D12Enc->m_spVideoEncoder.Get(),
                  pD3D12Enc->m_spVideoEncoderHeap.Get(),
                  inputStreamArguments.pInputFrame,
                  outputStreamArguments.Bitstream.FrameOutputBuffer.pBuffer,
                  inputMetadataCmd.HWLayoutMetadata.pBuffer,
                  outputMetadataCmd.ResolvedLayoutMetadata.pBuffer,
                  pD3D12Enc->m_inflightResourcesPool[d3d12_video_encoder_pool_current_index(pD3D12Enc)].m_spCommandAllocator.Get());

      // Transition DPB reference pictures back to COMMON
      if ((referenceFramesDescriptor.NumTexture2Ds > 0) ||
         (pD3D12Enc->m_upDPBManager->is_current_frame_used_as_reference())) {
         for (auto &BarrierDesc : pD3D12Enc->m_rgReferenceTransitions) {
            std::swap(BarrierDesc.Transition.StateBefore, BarrierDesc.Transition.StateAfter);
         }

         if (pD3D12Enc->m_rgReferenceTransitions.size() > 0) {
            pD3D12Enc->m_spEncodeCommandList4->ResourceBarrier(static_cast<uint32_t>(pD3D12Enc->m_rgReferenceTransitions.size()),
                                                            pD3D12Enc->m_rgReferenceTransitions.data());
         }
      }

      D3D12_RESOURCE_BARRIER rgRevertResolveMetadataStateTransitions[] = {
         CD3DX12_RESOURCE_BARRIER::Transition(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_spMetadataOutputBuffer.Get(),
                                             D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ,
                                             D3D12_RESOURCE_STATE_COMMON),
      };

      pD3D12Enc->m_spResolveCommandList4->ResourceBarrier(_countof(rgRevertResolveMetadataStateTransitions),
                                                      rgRevertResolveMetadataStateTransitions);

      // Revert output_stats_barriers
      for (auto &BarrierDesc : pD3D12Enc->m_outputStatsBarriers) {
         std::swap(BarrierDesc.Transition.StateBefore, BarrierDesc.Transition.StateAfter);
      }
      pD3D12Enc->m_spResolveCommandList4->ResourceBarrier(static_cast<uint32_t>(pD3D12Enc->m_outputStatsBarriers.size()),
                                          pD3D12Enc->m_outputStatsBarriers.data());
      for (auto &BarrierDesc : pD3D12Enc->m_pSlicedEncodingExtraBarriers) {
         std::swap(BarrierDesc.Transition.StateBefore, BarrierDesc.Transition.StateAfter);
      }
      pD3D12Enc->m_spEncodeCommandList4->ResourceBarrier(static_cast<uint32_t>(pD3D12Enc->m_pSlicedEncodingExtraBarriers.size()),
                                                      pD3D12Enc->m_pSlicedEncodingExtraBarriers.data());

      for (auto &BarrierDesc : pD3D12Enc->m_pTwoPassExtraBarriers) {
         std::swap(BarrierDesc.Transition.StateBefore, BarrierDesc.Transition.StateAfter);
      }
      pD3D12Enc->m_spEncodeCommandList4->ResourceBarrier(static_cast<uint32_t>(pD3D12Enc->m_pTwoPassExtraBarriers.size()),
                                                      pD3D12Enc->m_pTwoPassExtraBarriers.data());
   }
   else
   {
      D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA currentPicParamsLegacy =
         d3d12_video_encoder_get_current_picture_param_settings_legacy(pD3D12Enc);
      const D3D12_VIDEO_ENCODER_ENCODEFRAME_INPUT_ARGUMENTS inputStreamArguments = {
         // D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_DESC
         { // D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAGS
            pD3D12Enc->m_currentEncodeConfig.m_seqFlags,
            // D3D12_VIDEO_ENCODER_INTRA_REFRESH
            pD3D12Enc->m_currentEncodeConfig.m_IntraRefresh,
            d3d12_video_encoder_get_current_rate_control_settings(pD3D12Enc),
            // D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC
            pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
            pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode,
            d3d12_video_encoder_get_current_slice_param_settings(pD3D12Enc),
            d3d12_video_encoder_get_current_gop_desc(pD3D12Enc) },
         // D3D12_VIDEO_ENCODER_PICTURE_CONTROL_DESC
         { // uint32_t IntraRefreshFrameIndex;
            pD3D12Enc->m_currentEncodeConfig.m_IntraRefreshCurrentFrameIndex,
            // D3D12_VIDEO_ENCODER_PICTURE_CONTROL_FLAGS Flags;
            picCtrlFlags,
            // D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA PictureControlCodecData;
            currentPicParamsLegacy,
            // D3D12_VIDEO_ENCODE_REFERENCE_FRAMES ReferenceFrames;
            referenceFramesDescriptor
         },
         pInputVideoD3D12Res,
         inputVideoD3D12Subresource,
         static_cast<UINT>(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].preEncodeGeneratedHeadersByteSize + sliceHeadersSize)
         // budgeting. - User can also calculate headers fixed size beforehand (eg. no VUI,
         // etc) and build them with final values after EncodeFrame is executed
      };

      const D3D12_VIDEO_ENCODER_ENCODEFRAME_OUTPUT_ARGUMENTS outputStreamArguments = {
         // D3D12_VIDEO_ENCODER_COMPRESSED_BITSTREAM
         {
            pD3D12Enc->m_pOutputBufferD3D12Resources[0],
            pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].preEncodeGeneratedHeadersByteSize,
         },
         // D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE
         reconPicOutputTextureDesc,
         // D3D12_VIDEO_ENCODER_ENCODE_OPERATION_METADATA_BUFFER
         { pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_spMetadataOutputBuffer.Get(), 0 }
      };

      pD3D12Enc->m_spEncodeCommandList->EncodeFrame(pD3D12Enc->m_spVideoEncoder.Get(),
                                                    pD3D12Enc->m_spVideoEncoderHeap.Get(),
                                                    &inputStreamArguments,
                                                    &outputStreamArguments);

      pD3D12Enc->m_rgResolveMetadataStateTransitions = {
         CD3DX12_RESOURCE_BARRIER::Transition(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_spMetadataOutputBuffer.Get(),
                                             D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE,
                                             D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ),
         CD3DX12_RESOURCE_BARRIER::Transition(pInputVideoD3D12Res,
                                             D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ,
                                             D3D12_RESOURCE_STATE_COMMON),
      };

      pD3D12Enc->m_spResolveCommandList->ResourceBarrier(static_cast<uint32_t>(pD3D12Enc->m_rgResolveMetadataStateTransitions.size()),
                                                         pD3D12Enc->m_rgResolveMetadataStateTransitions.data());

      const D3D12_VIDEO_ENCODER_RESOLVE_METADATA_INPUT_ARGUMENTS inputMetadataCmd = {
         pD3D12Enc->m_currentEncodeConfig.m_encoderCodecDesc,
         d3d12_video_encoder_get_current_profile_desc(pD3D12Enc),
         pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
         // D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC
         pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
         { pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_spMetadataOutputBuffer.Get(), 0 }
      };

      const D3D12_VIDEO_ENCODER_RESOLVE_METADATA_OUTPUT_ARGUMENTS outputMetadataCmd = {
         /*If offset were to change, has to be aligned to pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.EncoderMetadataBufferAccessAlignment*/
         { pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].spBuffer.Get(), 0 }
      };
         
      pD3D12Enc->m_spResolveCommandList->ResolveEncoderOutputMetadata(&inputMetadataCmd, &outputMetadataCmd);
      
      debug_printf("[d3d12_video_encoder_encode_bitstream] EncodeFrame slot %" PRIu64 " encoder %p encoderheap %p input tex %p output bitstream %p raw metadata buf %p resolved metadata buf %p Command allocator %p\n",
                  static_cast<uint64_t>(d3d12_video_encoder_pool_current_index(pD3D12Enc)),
                  pD3D12Enc->m_spVideoEncoder.Get(),
                  pD3D12Enc->m_spVideoEncoderHeap.Get(),
                  inputStreamArguments.pInputFrame,
                  outputStreamArguments.Bitstream.pBuffer,
                  inputMetadataCmd.HWLayoutMetadata.pBuffer,
                  outputMetadataCmd.ResolvedLayoutMetadata.pBuffer,
                  pD3D12Enc->m_inflightResourcesPool[d3d12_video_encoder_pool_current_index(pD3D12Enc)].m_spCommandAllocator.Get());

      // Transition DPB reference pictures back to COMMON
      if ((referenceFramesDescriptor.NumTexture2Ds > 0) ||
         (pD3D12Enc->m_upDPBManager->is_current_frame_used_as_reference())) {
         for (auto &BarrierDesc : pD3D12Enc->m_rgReferenceTransitions) {
            std::swap(BarrierDesc.Transition.StateBefore, BarrierDesc.Transition.StateAfter);
         }

         if (pD3D12Enc->m_rgReferenceTransitions.size() > 0) {
            pD3D12Enc->m_spEncodeCommandList->ResourceBarrier(static_cast<uint32_t>(pD3D12Enc->m_rgReferenceTransitions.size()),
                                                            pD3D12Enc->m_rgReferenceTransitions.data());
         }
      }

      D3D12_RESOURCE_BARRIER rgRevertResolveMetadataStateTransitions[] = {
         CD3DX12_RESOURCE_BARRIER::Transition(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_spMetadataOutputBuffer.Get(),
                                             D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ,
                                             D3D12_RESOURCE_STATE_COMMON),
      };

      pD3D12Enc->m_spResolveCommandList->ResourceBarrier(_countof(rgRevertResolveMetadataStateTransitions),
                                                   rgRevertResolveMetadataStateTransitions);
   }
   debug_printf("[d3d12_video_encoder] d3d12_video_encoder_encode_bitstream finalized for fenceValue: %" PRIu64 "\n",
                 pD3D12Enc->m_fenceValue);
}

void
d3d12_video_encoder_get_feedback(struct pipe_video_codec *codec,
                                  void *feedback,
                                  unsigned *output_buffer_size,
                                  struct pipe_enc_feedback_metadata* pMetadata)
{
   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;
   assert(pD3D12Enc);

   struct d3d12_fence *feedback_fence = (struct d3d12_fence *) feedback;
   uint64_t requested_metadata_fence = feedback_fence->value;

   struct pipe_enc_feedback_metadata opt_metadata;
   memset(&opt_metadata, 0, sizeof(opt_metadata));

#ifdef MESA_DEBUG
   HRESULT hr = pD3D12Enc->m_pD3D12Screen->dev->GetDeviceRemovedReason();
   if (hr != S_OK) {
      opt_metadata.encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;
      debug_printf("Error: d3d12_video_encoder_get_feedback for Encode GPU command for fence %" PRIu64 " failed with GetDeviceRemovedReason: %x\n",
                     requested_metadata_fence,
                     (unsigned)hr);
      assert(false);
      if(pMetadata)
         *pMetadata = opt_metadata;
      return;
   }
#endif

   size_t current_metadata_slot = static_cast<size_t>(requested_metadata_fence % pD3D12Enc->m_MaxMetadataBuffersCount);
   opt_metadata.encode_result = pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].encode_result;
   if (opt_metadata.encode_result & PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED) {
      debug_printf("Error: d3d12_video_encoder_get_feedback for Encode GPU command for fence %" PRIu64 " failed on submission with encode_result: %x\n",
                     requested_metadata_fence,
                     opt_metadata.encode_result);
      assert(false);
      if(pMetadata)
         *pMetadata = opt_metadata;
      return;
   }
   
   
   bool wait_res = d3d12_fence_finish(feedback_fence, OS_TIMEOUT_INFINITE);
   if (!wait_res) {
      opt_metadata.encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;
      debug_printf("Error: d3d12_video_encoder_get_feedback for Encode GPU command for fence %" PRIu64 " failed on d3d12_video_encoder_sync_completion\n",
                     requested_metadata_fence);
      assert(false);
      if(pMetadata)
         *pMetadata = opt_metadata;
      return;
   }

   opt_metadata.encode_result = pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].encode_result;
   if (opt_metadata.encode_result & PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED) {
      debug_printf("Error: d3d12_video_encoder_get_feedback for Encode GPU command for fence %" PRIu64 " failed on GPU fence wait with encode_result: %x\n",
                     requested_metadata_fence,
                     opt_metadata.encode_result);
      assert(false);
      if(pMetadata)
         *pMetadata = opt_metadata;
      return;
   }

   debug_printf("d3d12_video_encoder_get_feedback with feedback: %" PRIu64 ", resources slot %" PRIu64 " metadata resolved ID3D12Resource buffer %p metadata required size %" PRIu64 "\n",
      requested_metadata_fence,
      (requested_metadata_fence % pD3D12Enc->m_MaxQueueAsyncDepth),
      pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].spBuffer.Get(),
      pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].bufferSize);

   if((pD3D12Enc->m_fenceValue - requested_metadata_fence) > pD3D12Enc->m_MaxMetadataBuffersCount)
   {
      debug_printf("[d3d12_video_encoder_get_feedback] Requested metadata for fence %" PRIu64 " at current fence %" PRIu64
         " is too far back in time for the ring buffer of size %" PRIu64 " we keep track off - "
         " Please increase the m_MaxMetadataBuffersCount of the encoder and try again.\n",
         requested_metadata_fence,
         pD3D12Enc->m_fenceValue,
         static_cast<uint64_t>(pD3D12Enc->m_MaxMetadataBuffersCount));
      opt_metadata.encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;
      assert(false);
      if(pMetadata)
         *pMetadata = opt_metadata;
      return;
   }

   // Extract encode metadata
   D3D12_VIDEO_ENCODER_OUTPUT_METADATA                       encoderMetadata;
   std::vector<D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA> pSubregionsMetadata;
   bool bSuccess = d3d12_video_encoder_extract_encode_metadata(
                     pD3D12Enc,
                     feedback,
                     pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot],
                     encoderMetadata,
                     pSubregionsMetadata);

   if (!bSuccess) {
      opt_metadata.encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;
      debug_printf("[d3d12_video_encoder] Encode GPU command for fence %" PRIu64 " failed - could not extract encode metadata\n",
                     requested_metadata_fence);
      assert(false);
      if(pMetadata)
         *pMetadata = opt_metadata;
      return;
   }

   // Validate encoder output metadata
   if ((encoderMetadata.EncodeErrorFlags != D3D12_VIDEO_ENCODER_ENCODE_ERROR_FLAG_NO_ERROR) || (encoderMetadata.EncodedBitstreamWrittenBytesCount == 0)) {
      opt_metadata.encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;
      debug_printf("[d3d12_video_encoder] Encode GPU command for fence %" PRIu64 " failed - EncodeErrorFlags: %" PRIu64 "\n",
                     requested_metadata_fence,
                     encoderMetadata.EncodeErrorFlags);
      assert(false);
      if(pMetadata)
         *pMetadata = opt_metadata;
      return;
   }

   uint64_t unpadded_frame_size = 0;
   if(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].postEncodeHeadersNeeded)
   {
      *output_buffer_size = d3d12_video_encoder_build_post_encode_codec_bitstream(
         pD3D12Enc,
         requested_metadata_fence,
         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot]
      );
      for (uint32_t i = 0; i < pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pWrittenCodecUnitsSizes.size(); i++)
      {
         opt_metadata.codec_unit_metadata[opt_metadata.codec_unit_metadata_count].size = pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pWrittenCodecUnitsSizes[i];
         opt_metadata.codec_unit_metadata[opt_metadata.codec_unit_metadata_count].offset = unpadded_frame_size;
         unpadded_frame_size += opt_metadata.codec_unit_metadata[opt_metadata.codec_unit_metadata_count].size;
         opt_metadata.codec_unit_metadata_count++;
      }
   }
   else
   {
      // Re-pack slices with any extra slice headers
      // if we are in full frame notification mode (otherwise each slice buffer packs independently)
      //
      if (pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].SubregionNotificationMode == D3D12_VIDEO_ENCODER_COMPRESSED_BITSTREAM_NOTIFICATION_MODE_FULL_FRAME)
      {
         // Only repack if any slice has any headers to write
         uint32_t num_slice_headers = 0u;
         for (auto& slice_hdrs : pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSliceHeaders)
            num_slice_headers += static_cast<uint32_t>(slice_hdrs.size());
         if (num_slice_headers > 0)
         {
            if (!pD3D12Enc->m_SliceHeaderRepackBuffer) {
               struct pipe_resource templ = { };
               memset(&templ, 0, sizeof(templ));
               templ.target = PIPE_BUFFER;
               templ.usage = PIPE_USAGE_DEFAULT;
               templ.format = PIPE_FORMAT_R8_UINT;
               templ.width0 = pD3D12Enc->m_MaxOutputBitstreamSize;
               templ.height0 = 1;
               templ.depth0 = 1;
               templ.array_size = 1;
               pD3D12Enc->m_SliceHeaderRepackBuffer = pD3D12Enc->m_screen->resource_create(pD3D12Enc->m_screen, &templ);
            }

            //
            // Copy slices from driver comp_bit_destinations[0/*first slice*/] into m_SliceHeaderRepackBuffer with collated slices headers
            //
            // Skip SPS, PPS, etc first preEncodeGeneratedHeadersByteSize bytes in src_driver_buffer_read_bytes
            uint32_t src_driver_buffer_read_bytes = static_cast<uint32_t>(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].preEncodeGeneratedHeadersByteSize);
            uint32_t dst_tmp_buffer_written_bytes = 0u;
            for (uint32_t cur_slice_idx = 0; cur_slice_idx < pSubregionsMetadata.size(); cur_slice_idx++)
            {
               uint32_t slice_headers_count = static_cast<uint32_t>(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSliceHeaders[cur_slice_idx].size());
               for (uint32_t slice_nal_idx = 0; slice_nal_idx < slice_headers_count; slice_nal_idx++)
               {
                  uint64_t slice_nal_size = static_cast<uint64_t>(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSliceHeaders[cur_slice_idx][slice_nal_idx].buffer.size());
                  void* slice_nal_buffer = pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSliceHeaders[cur_slice_idx][slice_nal_idx].buffer.data();

                  // Ensure we have enough space
                  assert((dst_tmp_buffer_written_bytes + slice_nal_size) <= pD3D12Enc->m_SliceHeaderRepackBuffer->width0);
                  if ((dst_tmp_buffer_written_bytes + slice_nal_size) > pD3D12Enc->m_SliceHeaderRepackBuffer->width0) {
                     opt_metadata.encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;
                     debug_printf("[d3d12_video_encoder] Insufficient compressed buffer size passed from frontend while repacking slice headers.\n");
                     assert(false);
                     if(pMetadata)
                        *pMetadata = opt_metadata;
                     return;
                  }

                  // Upload slice header to m_SliceHeaderRepackBuffer
                  pD3D12Enc->base.context->buffer_subdata(pD3D12Enc->base.context,               // context
                                                          pD3D12Enc->m_SliceHeaderRepackBuffer,  // dst buffer
                                                          PIPE_MAP_WRITE,                        // dst usage PIPE_MAP_x
                                                          dst_tmp_buffer_written_bytes,          // dst offset
                                                          static_cast<unsigned int>(slice_nal_size), // src size
                                                          slice_nal_buffer);                     // src void* buffer
                  dst_tmp_buffer_written_bytes += static_cast<unsigned int>(slice_nal_size);

                  // Copy slice (padded as-is) pSubregionsMetadata[cur_slice_idx].bSize at src_driver_buffer_read_bytes into m_SliceHeaderRepackBuffer AFTER the slice nal (slice_nal_size) to m_SliceHeaderRepackBuffer
                  struct pipe_box src_box = {};
                  u_box_3d(src_driver_buffer_read_bytes,                   // x
                           0,                                              // y
                           0,                                              // z
                           static_cast<int>(pSubregionsMetadata[cur_slice_idx].bSize), // width
                           1,                                              // height
                           1,                                              // depth
                           &src_box
                  );

                  // Ensure we have enough space
                  assert((dst_tmp_buffer_written_bytes + src_box.width) <= pD3D12Enc->m_SliceHeaderRepackBuffer->width0);
                  if ((dst_tmp_buffer_written_bytes + src_box.width) > pD3D12Enc->m_SliceHeaderRepackBuffer->width0) {
                     opt_metadata.encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;
                     debug_printf("[d3d12_video_encoder] Insufficient compressed buffer size passed from frontend while repacking slice headers.\n");
                     assert(false);
                     if(pMetadata)
                        *pMetadata = opt_metadata;
                     return;
                  }

                  pD3D12Enc->base.context->resource_copy_region(pD3D12Enc->base.context,                                                                              //  ctx
                                                                pD3D12Enc->m_SliceHeaderRepackBuffer,                                                                 //  dst
                                                                0,                                                                                                    //  dst_level
                                                                dst_tmp_buffer_written_bytes,                                                                         //  dstX - Skip the other headers in the final bitstream (e.g SPS, PPS, etc)
                                                                0,                                                                                                    //  dstY
                                                                0,                                                                                                    //  dstZ
                                                                pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].comp_bit_destinations[0/*first slice*/],   //  src
                                                                0,                                                                                                    //  src level
                                                                &src_box);
                  src_driver_buffer_read_bytes += static_cast<uint32_t>(pSubregionsMetadata[cur_slice_idx].bSize);
                  dst_tmp_buffer_written_bytes += static_cast<uint32_t>(pSubregionsMetadata[cur_slice_idx].bSize);
               }
            }

            //
            // Copy from m_SliceHeaderRepackBuffer with slice NALs and slices back into comp_bit_destinations[0/*first slice*/]
            //

            // Make sure we have enough space in destination buffer
            if (dst_tmp_buffer_written_bytes >
               (pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].preEncodeGeneratedHeadersByteSize + pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].comp_bit_destinations[0/*first slice*/]->width0))
            {
               opt_metadata.encode_result = PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_FAILED;
               debug_printf("[d3d12_video_encoder] Insufficient compressed buffer size passed from frontend while repacking slice headers.\n");
               assert(false);
               if(pMetadata)
                  *pMetadata = opt_metadata;
               return;
            }

            // Do the copy
            struct pipe_box src_box = {};
            u_box_3d(0,                                              // x
                     0,                                              // y
                     0,                                              // z
                     static_cast<int>(dst_tmp_buffer_written_bytes), // width
                     1,                                              // height
                     1,                                              // depth
                     &src_box
            );

            pD3D12Enc->base.context->resource_copy_region(pD3D12Enc->base.context,                                                                    // ctx
                                                         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].comp_bit_destinations[0/*first slice*/],             // dst
                                                         0,                                                                                           // dst_level
                                                         static_cast<unsigned int>(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].preEncodeGeneratedHeadersByteSize),// dstX - Skip the other headers in the final bitstream (e.g SPS, PPS, etc)
                                                         0,                                                                                           // dstY
                                                         0,                                                                                           // dstZ
                                                         pD3D12Enc->m_SliceHeaderRepackBuffer,                                                             // src
                                                         0,                                                                                           // src level
                                                         &src_box);

            //
            // Flush copies in batch and wait on this CPU thread for GPU work completion
            //
            struct pipe_fence_handle *pUploadGPUCompletionFence = NULL;
            pD3D12Enc->base.context->flush(pD3D12Enc->base.context,
                                          &pUploadGPUCompletionFence,
                                          PIPE_FLUSH_ASYNC | PIPE_FLUSH_HINT_FINISH);
            assert(pUploadGPUCompletionFence);
            pD3D12Enc->m_pD3D12Screen->base.fence_finish(&pD3D12Enc->m_pD3D12Screen->base,
                                                         NULL,
                                                         pUploadGPUCompletionFence,
                                                         OS_TIMEOUT_INFINITE);
            pD3D12Enc->m_pD3D12Screen->base.fence_reference(&pD3D12Enc->m_pD3D12Screen->base,
                                                            &pUploadGPUCompletionFence,
                                                            NULL);
         }
      }

      *output_buffer_size = 0;
      for (uint32_t i = 0; i < pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pWrittenCodecUnitsSizes.size() ; i++) {
         unpadded_frame_size += pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pWrittenCodecUnitsSizes[i];
         opt_metadata.codec_unit_metadata[opt_metadata.codec_unit_metadata_count].size = pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pWrittenCodecUnitsSizes[i];
         opt_metadata.codec_unit_metadata[opt_metadata.codec_unit_metadata_count].offset = *output_buffer_size;
         *output_buffer_size += static_cast<unsigned int>(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pWrittenCodecUnitsSizes[i]);
         opt_metadata.codec_unit_metadata_count++;
      }

      // Add padding between pre encode headers (e.g EncodeFrame driver offset alignment) and the first slice
      *output_buffer_size += static_cast<unsigned int>(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].preEncodeGeneratedHeadersBytePadding);

      debug_printf("D3D12 backend readback submission for frame with fence %" PRIu64 " current_metadata_slot %" PRIu64 " - PictureOrderCountNumber %d FrameType %s num_slice_descriptors %d IRMode %d IRIndex %d\n",
         requested_metadata_fence,
         static_cast<uint64_t>(current_metadata_slot),
         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_associatedEncodeConfig.m_encoderPicParamsDesc.m_H264PicData.PictureOrderCountNumber,
         d3d12_video_encoder_friendly_frame_type_h264(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_associatedEncodeConfig.m_encoderPicParamsDesc.m_H264PicData.FrameType),
         static_cast<uint32_t>(pSubregionsMetadata.size()),
         static_cast<uint32_t>(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_associatedEncodeConfig.m_IntraRefresh.Mode),
         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_associatedEncodeConfig.m_IntraRefreshCurrentFrameIndex);

      for (uint32_t i = 0; i < pSubregionsMetadata.size(); i++)
      {
         if (pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSliceHeaders.size() > 0) {
            for (uint32_t slice_nal_idx = 0; slice_nal_idx < pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSliceHeaders[i].size();slice_nal_idx++)
            {
               uint64_t nal_size = static_cast<uint64_t>(pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].pSliceHeaders[i][slice_nal_idx].buffer.size());
               unpadded_frame_size += nal_size;
               opt_metadata.codec_unit_metadata[opt_metadata.codec_unit_metadata_count].flags = PIPE_VIDEO_CODEC_UNIT_LOCATION_FLAG_SINGLE_NALU;
               opt_metadata.codec_unit_metadata[opt_metadata.codec_unit_metadata_count].size = nal_size;
               opt_metadata.codec_unit_metadata[opt_metadata.codec_unit_metadata_count].offset = *output_buffer_size;
               *output_buffer_size += static_cast<unsigned int>(nal_size);
               opt_metadata.codec_unit_metadata_count++;
            }
         }

         uint64_t unpadded_slice_size = pSubregionsMetadata[i].bSize - pSubregionsMetadata[i].bStartOffset;
         unpadded_frame_size += unpadded_slice_size;
         opt_metadata.codec_unit_metadata[opt_metadata.codec_unit_metadata_count].flags = PIPE_VIDEO_CODEC_UNIT_LOCATION_FLAG_SINGLE_NALU;
         opt_metadata.codec_unit_metadata[opt_metadata.codec_unit_metadata_count].size = unpadded_slice_size;
         opt_metadata.codec_unit_metadata[opt_metadata.codec_unit_metadata_count].offset = (*output_buffer_size) + static_cast<unsigned int>(pSubregionsMetadata[i].bStartOffset);
         *output_buffer_size += static_cast<unsigned int>(pSubregionsMetadata[i].bSize);
         if ((pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].expected_max_slice_size > 0) &&
             (unpadded_slice_size > pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].expected_max_slice_size))
            opt_metadata.codec_unit_metadata[opt_metadata.codec_unit_metadata_count].flags |= PIPE_VIDEO_CODEC_UNIT_LOCATION_FLAG_MAX_SLICE_SIZE_OVERFLOW;
         opt_metadata.codec_unit_metadata_count++;
      }
   }

   if ((pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].expected_max_frame_size > 0) &&
      (unpadded_frame_size > pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].expected_max_frame_size))
      opt_metadata.encode_result |= PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_MAX_FRAME_SIZE_OVERFLOW;

   opt_metadata.average_frame_qp = static_cast<unsigned int>(encoderMetadata.EncodeStats.AverageQP);

   opt_metadata.present_metadata = (PIPE_VIDEO_FEEDBACK_METADATA_TYPE_BITSTREAM_SIZE |
                                    PIPE_VIDEO_FEEDBACK_METADATA_TYPE_ENCODE_RESULT |
                                    PIPE_VIDEO_FEEDBACK_METADATA_TYPE_CODEC_UNIT_LOCATION |
                                    PIPE_VIDEO_FEEDBACK_METADATA_TYPE_MAX_FRAME_SIZE_OVERFLOW |
                                    PIPE_VIDEO_FEEDBACK_METADATA_TYPE_MAX_SLICE_SIZE_OVERFLOW |
                                    PIPE_VIDEO_FEEDBACK_METADATA_TYPE_AVERAGE_FRAME_QP);

   if (pMetadata)
      *pMetadata = opt_metadata;

   debug_printf("[d3d12_video_encoder_get_feedback] Requested metadata for encoded frame at fence %" PRIu64 " is:\n"
                "\tfeedback was requested at current fence: %" PRIu64 "\n"
                "\toutput_buffer_size (including padding): %d\n"
                "\tunpadded_frame_size: %" PRIu64 "\n"
                "\ttotal padding: %" PRIu64 "\n"
                "\tcodec_unit_metadata_count: %d\n",
                pD3D12Enc->m_fenceValue,
                requested_metadata_fence,
                *output_buffer_size,
                unpadded_frame_size,
                static_cast<uint64_t>(static_cast<uint64_t>(*output_buffer_size) - unpadded_frame_size),
                opt_metadata.codec_unit_metadata_count);

   for (uint32_t i = 0; i < opt_metadata.codec_unit_metadata_count; i++) {
      debug_printf("\tcodec_unit_metadata[%d].offset: %" PRIu64" - codec_unit_metadata[%d].size: %" PRIu64" \n",
         i,
         opt_metadata.codec_unit_metadata[i].offset,
         i,
         opt_metadata.codec_unit_metadata[i].size);
   }

   if ((pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].SubregionNotificationMode == D3D12_VIDEO_ENCODER_COMPRESSED_BITSTREAM_NOTIFICATION_MODE_FULL_FRAME) &&
       (*output_buffer_size >= pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].comp_bit_destinations[0/*first slice*/]->width0))
   {
      debug_printf("[d3d12_video_encoder_get_feedback] Warning: Encoded bitstream size %d exceeds the allocated output buffer size %d\n",
         *output_buffer_size,
         pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].comp_bit_destinations[0/*first slice*/]->width0);
      opt_metadata.encode_result |= PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_MAX_FRAME_SIZE_OVERFLOW;
      assert(false);
   }

   pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].bRead = true;
}

unsigned
d3d12_video_encoder_build_post_encode_codec_bitstream(struct d3d12_video_encoder * pD3D12Enc,
                                             uint64_t associated_fence_value,
                                             EncodedBitstreamResolvedMetadata& associatedMetadata)
{
   enum pipe_video_format codec_format = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec_format) {
#if VIDEO_CODEC_H264ENC
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         return 0;
      } break;
#endif
#if VIDEO_CODEC_H265ENC
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         return 0;
      } break; // Do not need post encode values in headers
#endif
#if VIDEO_CODEC_AV1ENC
      case PIPE_VIDEO_FORMAT_AV1:
      {
         return d3d12_video_encoder_build_post_encode_codec_bitstream_av1(
            // Current encoder
            pD3D12Enc,
            // associated fence value
            associated_fence_value,
            // Metadata desc
            associatedMetadata
         );
      } break;
#endif
      default:
         UNREACHABLE("Unsupported pipe_video_format");
   }
}

bool
d3d12_video_encoder_extract_encode_metadata(
   struct d3d12_video_encoder *                               pD3D12Enc,
   void                                                       *feedback,                 // input
   struct EncodedBitstreamResolvedMetadata &                  raw_metadata,              // input
   D3D12_VIDEO_ENCODER_OUTPUT_METADATA &                      parsedMetadata,            // output
   std::vector<D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA> &pSubregionsMetadata        // output
)
{
   ID3D12Resource *pResolvedMetadataBuffer = raw_metadata.spBuffer.Get();
   uint64_t resourceMetadataSize = raw_metadata.bufferSize;

#if MESA_DEBUG
   // Verify resource is CPU accessible (created with D3D12_HEAP_TYPE_READBACK)
   D3D12_HEAP_PROPERTIES heapProps;
   D3D12_HEAP_FLAGS heapFlags;
   pResolvedMetadataBuffer->GetHeapProperties(&heapProps, &heapFlags);
   assert(heapProps.Type == D3D12_HEAP_TYPE_READBACK);
#endif

   // Map metadata buffer using native D3D12 API
   D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(resourceMetadataSize) };
   void *pMetadataBufferSrc;
   HRESULT hr = pResolvedMetadataBuffer->Map(0, &readRange, &pMetadataBufferSrc);
   if (FAILED(hr)) {
      debug_printf("Error: d3d12_video_encoder_extract_encode_metadata failed to map metadata buffer with HR %x\n", (unsigned)hr);
      return false;
   }

   // Clear output
   memset(&parsedMetadata, 0, sizeof(D3D12_VIDEO_ENCODER_OUTPUT_METADATA));

   // Calculate sizes
   uint64_t encoderMetadataSize = sizeof(D3D12_VIDEO_ENCODER_OUTPUT_METADATA);

   // Copy buffer to the appropriate D3D12_VIDEO_ENCODER_OUTPUT_METADATA memory layout
   parsedMetadata = *reinterpret_cast<D3D12_VIDEO_ENCODER_OUTPUT_METADATA *>(pMetadataBufferSrc);

   // As specified in D3D12 Encode spec, the array base for metadata for the slices
   // (D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA[]) is placed in memory immediately after the
   // D3D12_VIDEO_ENCODER_OUTPUT_METADATA structure
   D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA *pFrameSubregionMetadata =
      reinterpret_cast<D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA *>(reinterpret_cast<uint8_t *>(pMetadataBufferSrc) +
                                                                       encoderMetadataSize);

   if (raw_metadata.SubregionNotificationMode == D3D12_VIDEO_ENCODER_COMPRESSED_BITSTREAM_NOTIFICATION_MODE_FULL_FRAME)
   {
      // Copy fields into D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA
      assert(parsedMetadata.WrittenSubregionsCount < SIZE_MAX);
      pSubregionsMetadata.resize(static_cast<size_t>(parsedMetadata.WrittenSubregionsCount));
      for (uint32_t sliceIdx = 0; sliceIdx < parsedMetadata.WrittenSubregionsCount; sliceIdx++) {
         pSubregionsMetadata[sliceIdx].bHeaderSize  = pFrameSubregionMetadata[sliceIdx].bHeaderSize;
         pSubregionsMetadata[sliceIdx].bSize        = pFrameSubregionMetadata[sliceIdx].bSize;
         pSubregionsMetadata[sliceIdx].bStartOffset = pFrameSubregionMetadata[sliceIdx].bStartOffset;
      }
   }
   else if (raw_metadata.SubregionNotificationMode == D3D12_VIDEO_ENCODER_COMPRESSED_BITSTREAM_NOTIFICATION_MODE_SUBREGIONS) {
      // Driver metadata doesn't have the subregions nor EncodedBitstreamWrittenBytesCount info on this case, let's get them from d3d12_video_encoder_get_slice_bitstream_data instead
      parsedMetadata.EncodedBitstreamWrittenBytesCount = 0u;

      // We need to be careful when dealing with AUTO slice layout mode
      // as the number of subregions may not match the number of slices raw_metadata.pspSubregionFences.size()
      if (raw_metadata.m_associatedEncodeConfig.m_encoderSliceConfigMode == D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_AUTO)
      {
         // Wait on the last slice fence to ensure all slices are completed
         bool bLastSliceWaitResult = d3d12_fence_finish(raw_metadata.m_LastSliceFence.get(), OS_TIMEOUT_INFINITE);
         assert(bLastSliceWaitResult);
         if (!bLastSliceWaitResult)
         {
            debug_printf("Error: d3d12_video_encoder_extract_encode_metadata failed to wait on last slice fence\n");
            pResolvedMetadataBuffer->Unmap(0, nullptr);
            return false;
         }

         // Count how many pSliceFences are actually signaled -> this is the actual number of slices actually produced
         // and save that into parsedMetadata.WrittenSubregionsCount
         uint32_t max_potential_slice_count = static_cast<uint32_t>(raw_metadata.pspSubregionFences.size());
         parsedMetadata.WrittenSubregionsCount = 0;
         bool bSliceSignaled = false;
         uint32_t slice_idx = 0;
         do
         {
            bSliceSignaled = d3d12_fence_finish(raw_metadata.pSubregionPipeFences[slice_idx].get(),  0 /* No wait, just see if signaled */ );

            if(bSliceSignaled)
            {
               parsedMetadata.WrittenSubregionsCount++;
            }

            slice_idx++;
         } while ((slice_idx < max_potential_slice_count) &&
                  bSliceSignaled);

         if( parsedMetadata.WrittenSubregionsCount != max_potential_slice_count)
         {
            debug_printf("Info: d3d12_video_encoder_extract_encode_metadata AUTO slice layout mode detected %"
                         PRIu32 " signaled slices out of max potential %"
                         PRIu32 " slices.\n",
                         static_cast<UINT32>(parsedMetadata.WrittenSubregionsCount),
                         max_potential_slice_count);
         }
      }
      else
      {
         parsedMetadata.WrittenSubregionsCount = static_cast<UINT64>(raw_metadata.pspSubregionFences.size());
      }

      pSubregionsMetadata.resize(static_cast<size_t>(parsedMetadata.WrittenSubregionsCount));
      std::vector<struct codec_unit_location_t> slice_codec_units(4u);
      for (uint32_t sliceIdx = 0; sliceIdx < parsedMetadata.WrittenSubregionsCount; sliceIdx++) {
         unsigned codec_unit_metadata_count = 0u;
         d3d12_video_encoder_get_slice_bitstream_data(&pD3D12Enc->base,
                                                      feedback,
                                                      sliceIdx,
                                                      NULL /*get count in first call*/,
                                                      &codec_unit_metadata_count);

         if (codec_unit_metadata_count == 0) {
            assert(false);
            debug_printf("Error: d3d12_video_encoder_extract_encode_metadata slice %u has zero codec units\n", sliceIdx);
            pResolvedMetadataBuffer->Unmap(0, nullptr);
            return false;
         }

         slice_codec_units.resize(codec_unit_metadata_count);
         d3d12_video_encoder_get_slice_bitstream_data(&pD3D12Enc->base,
                                                      feedback,
                                                      sliceIdx,
                                                      slice_codec_units.data(),
                                                      &codec_unit_metadata_count);

         // Check for slice size overflow flag
         for (unsigned unit_idx = 0; unit_idx < codec_unit_metadata_count; unit_idx++) {
            if (slice_codec_units[unit_idx].flags & PIPE_VIDEO_CODEC_UNIT_LOCATION_FLAG_MAX_SLICE_SIZE_OVERFLOW) {
               debug_printf("Error: d3d12_video_encoder_extract_encode_metadata slice %u unit %u has size overflow flag set\n", sliceIdx, unit_idx);
               pResolvedMetadataBuffer->Unmap(0, nullptr);
               return false;
            }
         }

         // In some cases the slice buffer will contain packed codec units like SPS, PPS for H264, etc
         // In here we only want the slice NAL, and it's safe to assume this is always the latest NAL
         pSubregionsMetadata[sliceIdx].bHeaderSize  = 0u;
         pSubregionsMetadata[sliceIdx].bSize        = slice_codec_units[codec_unit_metadata_count - 1].size;
         pSubregionsMetadata[sliceIdx].bStartOffset = 0u;

         parsedMetadata.EncodedBitstreamWrittenBytesCount += pSubregionsMetadata[sliceIdx].bSize;
      }
   }

   // Unmap the buffer using native D3D12 API
   pResolvedMetadataBuffer->Unmap(0, nullptr);
   return true;
}

/**
 * end encoding of the current frame
 */
int
d3d12_video_encoder_end_frame(struct pipe_video_codec * codec,
                              struct pipe_video_buffer *target,
                              struct pipe_picture_desc *picture)
{
   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;
   assert(pD3D12Enc);
   debug_printf("[d3d12_video_encoder] d3d12_video_encoder_end_frame started for fenceValue: %" PRIu64 "\n",
                 pD3D12Enc->m_fenceValue);

   if (pD3D12Enc->m_inflightResourcesPool[d3d12_video_encoder_pool_current_index(pD3D12Enc)].encode_result != PIPE_VIDEO_FEEDBACK_METADATA_ENCODE_FLAG_OK) {
      debug_printf("WARNING: [d3d12_video_encoder] d3d12_video_encoder_end_frame - Frame submission %" PRIu64 " failed. Encoder lost, please recreate pipe_video_codec object\n", pD3D12Enc->m_fenceValue);
      assert(false);
      return 1;
   }

   // Signal finish of current frame encoding to the picture management tracker
   pD3D12Enc->m_upDPBManager->end_frame();

   // Save extra references of Encoder, EncoderHeap and DPB allocations in case
   // there's a reconfiguration that trigers the construction of new objects
   pD3D12Enc->m_inflightResourcesPool[d3d12_video_encoder_pool_current_index(pD3D12Enc)].m_spEncoder = pD3D12Enc->m_spVideoEncoder;
   pD3D12Enc->m_inflightResourcesPool[d3d12_video_encoder_pool_current_index(pD3D12Enc)].m_spEncoderHeap = pD3D12Enc->m_spVideoEncoderHeap;
   pD3D12Enc->m_inflightResourcesPool[d3d12_video_encoder_pool_current_index(pD3D12Enc)].m_References = pD3D12Enc->m_upDPBStorageManager;

   debug_printf("[d3d12_video_encoder] d3d12_video_encoder_end_frame finalized for fenceValue: %" PRIu64 "\n",
                 pD3D12Enc->m_fenceValue);

   pD3D12Enc->m_bPendingWorkNotFlushed = true;

   size_t current_metadata_slot = d3d12_video_encoder_metadata_current_index(pD3D12Enc);
   if (picture->out_fence)
      d3d12_fence_reference((struct d3d12_fence **)picture->out_fence, pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_fence.get());

   return 0;
}

void
d3d12_video_encoder_store_current_picture_references(d3d12_video_encoder *pD3D12Enc,
                                                     uint64_t current_metadata_slot)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
#if VIDEO_CODEC_H264ENC
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         // Not needed (not post encode headers)
      } break;
#endif
#if VIDEO_CODEC_H265ENC
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         // Not needed (not post encode headers)
      } break;
#endif
#if VIDEO_CODEC_AV1ENC
      case PIPE_VIDEO_FORMAT_AV1:
      {
         d3d12_video_encoder_store_current_picture_references_av1(pD3D12Enc, current_metadata_slot);
      } break;
#endif
      default:
      {
         UNREACHABLE("Unsupported pipe_video_format");
      } break;
   }
}

int d3d12_video_encoder_get_encode_headers([[maybe_unused]] struct pipe_video_codec *codec,
                                           [[maybe_unused]] struct pipe_picture_desc *picture,
                                           [[maybe_unused]] void* bitstream_buf,
                                           [[maybe_unused]] unsigned *bitstream_buf_size)
{
#if (VIDEO_CODEC_H264ENC || VIDEO_CODEC_H265ENC)
   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;
   D3D12_VIDEO_SAMPLE srcTextureDesc = {};
   srcTextureDesc.Width = pD3D12Enc->base.width;
   srcTextureDesc.Height = pD3D12Enc->base.height;
   srcTextureDesc.Format.Format = d3d12_get_format(picture->input_format);
   if(!d3d12_video_encoder_update_current_encoder_config_state(pD3D12Enc, srcTextureDesc, picture))
      return EINVAL;

   if (!pD3D12Enc->m_upBitstreamBuilder) {
#if VIDEO_CODEC_H264ENC
      if (u_reduce_video_profile(pD3D12Enc->base.profile) == PIPE_VIDEO_FORMAT_MPEG4_AVC)
         pD3D12Enc->m_upBitstreamBuilder = std::make_unique<d3d12_video_bitstream_builder_h264>();
#endif
#if VIDEO_CODEC_H265ENC
      if (u_reduce_video_profile(pD3D12Enc->base.profile) == PIPE_VIDEO_FORMAT_HEVC)
         pD3D12Enc->m_upBitstreamBuilder = std::make_unique<d3d12_video_bitstream_builder_hevc>();
#endif
   }
   bool postEncodeHeadersNeeded = false;
   uint64_t preEncodeGeneratedHeadersByteSize = 0;
   std::vector<uint64_t> pWrittenCodecUnitsSizes;
   pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |= d3d12_video_encoder_config_dirty_flag_sequence_header;
   d3d12_video_encoder_build_pre_encode_codec_headers(pD3D12Enc,
                                                      postEncodeHeadersNeeded,
                                                      preEncodeGeneratedHeadersByteSize,
                                                      pWrittenCodecUnitsSizes);
   if (preEncodeGeneratedHeadersByteSize > *bitstream_buf_size)
      return ENOMEM;

   *bitstream_buf_size = static_cast<unsigned>(pD3D12Enc->m_BitstreamHeadersBuffer.size());
   memcpy(bitstream_buf,
          pD3D12Enc->m_BitstreamHeadersBuffer.data(),
          *bitstream_buf_size);
   return 0;
#else
   return ENOTSUP;
#endif
}

template void
d3d12_video_encoder_update_picparams_region_of_interest_qpmap(struct d3d12_video_encoder *pD3D12Enc,
                                                              const struct pipe_enc_roi *roi_config,
                                                              int32_t min_delta_qp,
                                                              int32_t max_delta_qp,
                                                              std::vector<int16_t>& pQPMap);

template void
d3d12_video_encoder_update_picparams_region_of_interest_qpmap(struct d3d12_video_encoder *pD3D12Enc,
                                                              const struct pipe_enc_roi *roi_config,
                                                              int32_t min_delta_qp,
                                                              int32_t max_delta_qp,
                                                              std::vector<int8_t>& pQPMap);

template<typename T>
void
d3d12_video_encoder_update_picparams_region_of_interest_qpmap(struct d3d12_video_encoder *pD3D12Enc,
                                                              const struct pipe_enc_roi *roi_config,
                                                              int32_t min_delta_qp,
                                                              int32_t max_delta_qp,
                                                              std::vector<T>& pQPMap)
{
   static_assert(ARRAY_SIZE(roi_config->region) == PIPE_ENC_ROI_REGION_NUM_MAX);
   assert(roi_config->num > 0);
   assert(roi_config->num <= PIPE_ENC_ROI_REGION_NUM_MAX);
   assert(min_delta_qp < 0);
   assert(max_delta_qp > 0);

   // Set all the QP blocks with zero QP Delta, then only fill in the regions that have a non-zero delta value
   uint32_t QPMapRegionPixelsSize = pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps.QPMapRegionPixelsSize;
   size_t pic_width_in_qpmap_block_units = static_cast<size_t>(std::ceil(pD3D12Enc->m_currentEncodeConfig.m_currentResolution.Width /
      static_cast<double>(QPMapRegionPixelsSize)));
   size_t pic_height_in_qpmap_block_units = static_cast<size_t>(std::ceil(pD3D12Enc->m_currentEncodeConfig.m_currentResolution.Height /
      static_cast<double>(QPMapRegionPixelsSize)));
   size_t total_picture_qpmap_block_units = pic_width_in_qpmap_block_units * pic_height_in_qpmap_block_units;
   pQPMap.resize(total_picture_qpmap_block_units, 0u);

   // Loop in reverse for priority of overlapping regions as per p_video_state roi parameter docs
   for (int32_t i = (roi_config->num - 1); i >= 0 ; i--)
   {
      auto& cur_region = roi_config->region[i];
      if (cur_region.valid)
      {
         uint32_t bucket_start_block_x = cur_region.x / QPMapRegionPixelsSize;
         uint32_t bucket_start_block_y = cur_region.y / QPMapRegionPixelsSize;
         uint32_t bucket_end_block_x = static_cast<uint32_t>(std::ceil((cur_region.x + cur_region.width) / static_cast<double>(QPMapRegionPixelsSize)) - 1);
         uint32_t bucket_end_block_y = static_cast<uint32_t>(std::ceil((cur_region.y + cur_region.height) / static_cast<double>(QPMapRegionPixelsSize)) - 1);
         for (uint32_t i = bucket_start_block_x; i <= bucket_end_block_x; i++)
            for (uint32_t j = bucket_start_block_y; j <= bucket_end_block_y; j++)
               pQPMap[(j * pic_width_in_qpmap_block_units) + i] = static_cast<T>(CLAMP(cur_region.qp_value, min_delta_qp, max_delta_qp));
      }
   }
}

int
d3d12_video_encoder_fence_wait(struct pipe_video_codec *codec,
                               struct pipe_fence_handle *_fence,
                               uint64_t timeout)
{
   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;
   assert(pD3D12Enc);
   struct d3d12_fence *fence = (struct d3d12_fence *) _fence;
   assert(fence);

   bool wait_res = d3d12_fence_finish(fence, timeout);
   if (wait_res) {
      // Opportunistically reset batches
      for (uint32_t i = 0; i < pD3D12Enc->m_MaxQueueAsyncDepth; ++i)
         d3d12_video_encoder_sync_completion(codec, i, 0);
   }

   // Return semantics based on p_video_codec interface
   // ret == 0 -> Encode in progress
   // ret != 0 -> Encode completed
   return wait_res ? 1 : 0;
}

int
d3d12_video_encoder_get_last_slice_completion_fence(struct pipe_video_codec *codec,
                                                    void *feedback,
                                                    pipe_fence_handle **last_slice_completion_fence)
{
   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;
   assert(pD3D12Enc);

   if (!pD3D12Enc || !feedback || !last_slice_completion_fence) {
      return -1;
   }

   struct d3d12_fence *feedback_fence = (struct d3d12_fence *) feedback;
   uint64_t requested_metadata_fence = feedback_fence->value;
   size_t current_metadata_slot = static_cast<size_t>(requested_metadata_fence % pD3D12Enc->m_MaxMetadataBuffersCount);

   // Check if the requested metadata is valid
   if ((pD3D12Enc->m_fenceValue - requested_metadata_fence) > pD3D12Enc->m_MaxMetadataBuffersCount) {
      debug_printf("[d3d12_video_encoder_get_last_slice_completion_fence] Requested metadata for fence %" PRIu64 " at current fence %" PRIu64
         " is too far back in time for the ring buffer of size %" PRIu64 "\n",
         requested_metadata_fence,
         pD3D12Enc->m_fenceValue,
         static_cast<uint64_t>(pD3D12Enc->m_MaxMetadataBuffersCount));
      return -1;
   }

   // Get the last slice completion fence for this frame
   if (pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_LastSliceFence) {
      d3d12_fence_reference((struct d3d12_fence **)last_slice_completion_fence, 
                     pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_LastSliceFence.get());
      return 0;
   }

   return -1;
}