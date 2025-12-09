/*
 * Copyright © 2021 Red Hat
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "anv_private.h"

#include "genxml/gen_macros.h"
#include "genxml/genX_video_pack.h"

#include "util/vl_zscan_data.h"

void
genX(CmdBeginVideoCodingKHR)(VkCommandBuffer commandBuffer,
                             const VkVideoBeginCodingInfoKHR *pBeginInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_video_session, vid, pBeginInfo->videoSession);
   VK_FROM_HANDLE(vk_video_session_parameters, params,
                  pBeginInfo->videoSessionParameters);

   cmd_buffer->video.vid = vid;
   cmd_buffer->video.params = params;

   if (vid->vk.op != VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR)
      return;

   if (!vid->cdf_initialized) {
      anv_init_av1_cdf_tables(cmd_buffer, vid);
      vid->cdf_initialized = true;
   }
}

void
genX(CmdControlVideoCodingKHR)(VkCommandBuffer commandBuffer,
                               const VkVideoCodingControlInfoKHR *pCodingControlInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   if (pCodingControlInfo->flags & VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR) {
      anv_batch_emit(&cmd_buffer->batch, GENX(MI_FLUSH_DW), flush) {
         flush.VideoPipelineCacheInvalidate = 1;
      }
   }

   if (pCodingControlInfo->flags &  VK_VIDEO_CODING_CONTROL_ENCODE_RATE_CONTROL_BIT_KHR) {
      const struct VkVideoEncodeRateControlInfoKHR *rate_control_info =
         vk_find_struct_const(pCodingControlInfo->pNext, VIDEO_ENCODE_RATE_CONTROL_INFO_KHR);

      /* Support for only CQP rate control for the moment */
      assert((rate_control_info->rateControlMode == VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DEFAULT_KHR) ||
             (rate_control_info->rateControlMode == VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR));

      cmd_buffer->video.vid->rc_mode = rate_control_info->rateControlMode;
   }
}

void
genX(CmdEndVideoCodingKHR)(VkCommandBuffer commandBuffer,
                           const VkVideoEndCodingInfoKHR *pEndCodingInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->video.vid = NULL;
   cmd_buffer->video.params = NULL;
}

/*
 * The default scan order of scaling lists is up-right-diagonal
 * according to the spec. But the device requires raster order,
 * so we need to convert from the passed scaling lists.
 */
static void
anv_h265_matrix_from_uprightdiagonal(StdVideoH265ScalingLists *out_sl,
                                     const StdVideoH265ScalingLists *sl)
{
  uint8_t i, j;

  for (i = 0; i < 6; i++) {
     for (j = 0; j < STD_VIDEO_H265_SCALING_LIST_4X4_NUM_ELEMENTS; j++)
        out_sl->ScalingList4x4[i][vl_zscan_h265_up_right_diagonal_16[j]] =
           sl->ScalingList4x4[i][j];

     for (j = 0; j < STD_VIDEO_H265_SCALING_LIST_8X8_NUM_ELEMENTS; j++)
        out_sl->ScalingList8x8[i][vl_zscan_h265_up_right_diagonal[j]] =
           sl->ScalingList8x8[i][j];

     for (j = 0; j < STD_VIDEO_H265_SCALING_LIST_16X16_NUM_ELEMENTS; j++)
        out_sl->ScalingList16x16[i][vl_zscan_h265_up_right_diagonal[j]] =
           sl->ScalingList16x16[i][j];
  }

  for (i = 0; i < STD_VIDEO_H265_SCALING_LIST_32X32_NUM_LISTS; i++) {
     for (j = 0; j < STD_VIDEO_H265_SCALING_LIST_32X32_NUM_ELEMENTS; j++)
        out_sl->ScalingList32x32[i][vl_zscan_h265_up_right_diagonal[j]] =
           sl->ScalingList32x32[i][j];
  }
}

static void
scaling_list(struct anv_cmd_buffer *cmd_buffer,
             const StdVideoH265ScalingLists *scaling_list)
{
   StdVideoH265ScalingLists out_sl = {0, };

   anv_h265_matrix_from_uprightdiagonal(&out_sl, scaling_list);

   /* 4x4, 8x8, 16x16, 32x32 */
   for (uint8_t size = 0; size < 4; size++) {
      /* Intra, Inter */
      for (uint8_t pred = 0; pred < 2; pred++) {
         /* Y, Cb, Cr */
         for (uint8_t color = 0; color < 3; color++) {
            if (size == 3 && color > 0)
               continue;

            anv_batch_emit(&cmd_buffer->batch, GENX(HCP_QM_STATE), qm) {
               qm.SizeID = size;
               qm.PredictionType = pred;
               qm.ColorComponent = color;

               qm.DCCoefficient = size > 1 ?
                  (size == 2 ? scaling_list->ScalingListDCCoef16x16[3 * pred + color] :
                               scaling_list->ScalingListDCCoef32x32[pred]) : 0;

               if (size == 0) {
                  for (uint8_t i = 0; i < 4; i++)
                     for (uint8_t j = 0; j < 4; j++)
                        qm.QuantizerMatrix8x8[4 * i + j] =
                           out_sl.ScalingList4x4[3 * pred + color][4 * i + j];
               } else if (size == 1) {
                  for (uint8_t i = 0; i < 8; i++)
                     for (uint8_t j = 0; j < 8; j++)
                        qm.QuantizerMatrix8x8[8 * i + j] =
                           out_sl.ScalingList8x8[3 * pred + color][8 * i + j];
               } else if (size == 2) {
                  for (uint8_t i = 0; i < 8; i++)
                     for (uint8_t j = 0; j < 8; j++)
                        qm.QuantizerMatrix8x8[8 * i + j] =
                           out_sl.ScalingList16x16[3 * pred + color][8 * i + j];
               } else if (size == 3) {
                  for (uint8_t i = 0; i < 8; i++)
                     for (uint8_t j = 0; j < 8; j++)
                        qm.QuantizerMatrix8x8[8 * i + j] =
                           out_sl.ScalingList32x32[pred][8 * i + j];
               }
            }
         }
      }
   }
}

static void
anv_h265_decode_video(struct anv_cmd_buffer *cmd_buffer,
                      const VkVideoDecodeInfoKHR *frame_info)
{
   ANV_FROM_HANDLE(anv_buffer, src_buffer, frame_info->srcBuffer);
   struct anv_video_session *vid = cmd_buffer->video.vid;
   struct vk_video_session_parameters *params = cmd_buffer->video.params;

   const struct VkVideoDecodeH265PictureInfoKHR *h265_pic_info =
      vk_find_struct_const(frame_info->pNext, VIDEO_DECODE_H265_PICTURE_INFO_KHR);

   const StdVideoH265SequenceParameterSet *sps;
   const StdVideoH265PictureParameterSet *pps;

   vk_video_get_h265_parameters(&vid->vk, params, frame_info, h265_pic_info, &sps, &pps);

   struct vk_video_h265_reference ref_slots[2][8] = { 0 };
   uint8_t dpb_idx[ANV_VIDEO_H265_MAX_NUM_REF_FRAME] = { 0,};
   bool is_10bit = sps->bit_depth_chroma_minus8 || sps->bit_depth_luma_minus8;

   anv_batch_emit(&cmd_buffer->batch, GENX(MI_FLUSH_DW), flush) {
      flush.VideoPipelineCacheInvalidate = 1;
   };

#if GFX_VER >= 12
   anv_batch_emit(&cmd_buffer->batch, GENX(MI_FORCE_WAKEUP), wake) {
      wake.HEVCPowerWellControl = 1;
      wake.MaskBits = 768;
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(VD_CONTROL_STATE), cs) {
      cs.PipelineInitialization = true;
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(MFX_WAIT), mfx) {
      mfx.MFXSyncControlFlag = 1;
   }
#endif

   anv_batch_emit(&cmd_buffer->batch, GENX(HCP_PIPE_MODE_SELECT), sel) {
      sel.CodecSelect = Decode;
      sel.CodecStandardSelect = HEVC;
   }

#if GFX_VER >= 12
   anv_batch_emit(&cmd_buffer->batch, GENX(MFX_WAIT), mfx) {
      mfx.MFXSyncControlFlag = 1;
   }
#endif

   const struct anv_image_view *iv =
      anv_image_view_from_handle(frame_info->dstPictureResource.imageViewBinding);
   const struct anv_image *img = iv->image;

   anv_batch_emit(&cmd_buffer->batch, GENX(HCP_SURFACE_STATE), ss) {
      ss.SurfacePitch = img->planes[0].primary_surface.isl.row_pitch_B - 1;
      ss.SurfaceID = HCP_CurrentDecodedPicture;
      ss.SurfaceFormat = is_10bit ? P010 : PLANAR_420_8;

      ss.YOffsetforUCb = img->planes[1].primary_surface.memory_range.offset /
         img->planes[0].primary_surface.isl.row_pitch_B;

#if GFX_VER >= 11
      ss.DefaultAlphaValue = 0xffff;
#endif
   }

#if GFX_VER >= 12
   /* Seems to need to set same states to ref as decode on gen12 */
   anv_batch_emit(&cmd_buffer->batch, GENX(HCP_SURFACE_STATE), ss) {
      ss.SurfacePitch = img->planes[0].primary_surface.isl.row_pitch_B - 1;
      ss.SurfaceID = HCP_ReferencePicture;
      ss.SurfaceFormat = is_10bit ? P010 : PLANAR_420_8;

      ss.YOffsetforUCb = img->planes[1].primary_surface.memory_range.offset /
         img->planes[0].primary_surface.isl.row_pitch_B;

      ss.DefaultAlphaValue = 0xffff;
   }
#endif

   anv_batch_emit(&cmd_buffer->batch, GENX(HCP_PIPE_BUF_ADDR_STATE), buf) {
      buf.DecodedPictureAddress =
         anv_image_dpb_address(iv, frame_info->dstPictureResource.baseArrayLayer);

      buf.DecodedPictureMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.DecodedPictureAddress.bo, 0),
#if GFX_VERx10 >= 125
         .TiledResourceMode = TRMODE_TILEF,
#endif
      };

      buf.DeblockingFilterLineBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_DEBLOCK_FILTER_ROW_STORE_LINE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_DEBLOCK_FILTER_ROW_STORE_LINE].offset
      };

      buf.DeblockingFilterLineBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.DeblockingFilterLineBufferAddress.bo, 0),
      };

      buf.DeblockingFilterTileLineBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_DEBLOCK_FILTER_ROW_STORE_TILE_LINE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_DEBLOCK_FILTER_ROW_STORE_TILE_LINE].offset
      };

      buf.DeblockingFilterTileLineBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.DeblockingFilterTileLineBufferAddress.bo, 0),
      };

      buf.DeblockingFilterTileColumnBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_DEBLOCK_FILTER_ROW_STORE_TILE_COLUMN].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_DEBLOCK_FILTER_ROW_STORE_TILE_COLUMN].offset
      };

      buf.DeblockingFilterTileColumnBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.DeblockingFilterTileColumnBufferAddress.bo, 0),
      };

      buf.MetadataLineBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_METADATA_LINE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_METADATA_LINE].offset
      };

      buf.MetadataLineBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.MetadataLineBufferAddress.bo, 0),
      };

      buf.MetadataTileLineBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_METADATA_TILE_LINE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_METADATA_TILE_LINE].offset
      };

      buf.MetadataTileLineBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.MetadataTileLineBufferAddress.bo, 0),
      };

      buf.MetadataTileColumnBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_METADATA_TILE_COLUMN].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_METADATA_TILE_COLUMN].offset
      };

      buf.MetadataTileColumnBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.MetadataTileColumnBufferAddress.bo, 0),
      };

      buf.SAOLineBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_SAO_LINE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_SAO_LINE].offset
      };

      buf.SAOLineBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.SAOLineBufferAddress.bo, 0),
      };

      buf.SAOTileLineBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_SAO_TILE_LINE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_SAO_TILE_LINE].offset
      };

      buf.SAOTileLineBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.SAOTileLineBufferAddress.bo, 0),
      };

      buf.SAOTileColumnBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_SAO_TILE_COLUMN].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_SAO_TILE_COLUMN].offset
      };

      buf.SAOTileColumnBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.SAOTileColumnBufferAddress.bo, 0),
      };

      buf.CurrentMVTemporalBufferAddress =
         anv_image_dmv_top_address(iv, frame_info->dstPictureResource.baseArrayLayer);

      buf.CurrentMVTemporalBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.CurrentMVTemporalBufferAddress.bo, 0),
      };

      for (unsigned i = 0; i < frame_info->referenceSlotCount; i++) {
         const struct anv_image_view *ref_iv =
            anv_image_view_from_handle(frame_info->pReferenceSlots[i].pPictureResource->imageViewBinding);
         int slot_idx = frame_info->pReferenceSlots[i].slotIndex;

         assert(slot_idx < ANV_VIDEO_H265_MAX_NUM_REF_FRAME);

         if (slot_idx < 0)
            continue;
         dpb_idx[slot_idx] = i;

         buf.ReferencePictureAddress[i] =
            anv_image_dpb_address(ref_iv, frame_info->pReferenceSlots[i].pPictureResource->baseArrayLayer);
      }

      buf.ReferencePictureMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
#if GFX_VERx10 >= 125
         .TiledResourceMode = TRMODE_TILEF,
#endif
      };

      buf.OriginalUncompressedPictureSourceMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };

      buf.StreamOutDataDestinationMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };

      buf.DecodedPictureStatusBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };

      buf.LCUILDBStreamOutBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };

      for (unsigned i = 0; i < frame_info->referenceSlotCount; i++) {
         const struct anv_image_view *ref_iv =
            anv_image_view_from_handle(frame_info->pReferenceSlots[i].pPictureResource->imageViewBinding);

         buf.CollocatedMVTemporalBufferAddress[i] =
            anv_image_dmv_top_address(ref_iv, frame_info->pReferenceSlots[i].pPictureResource->baseArrayLayer);
      }

      buf.CollocatedMVTemporalBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.CollocatedMVTemporalBufferAddress[0].bo, 0),
      };

      buf.VP9ProbabilityBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };

      buf.VP9SegmentIDBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };

      buf.VP9HVDLineRowStoreBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };

      buf.VP9HVDTileRowStoreBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
#if GFX_VER >= 11
      buf.SAOStreamOutDataDestinationBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.FrameStatisticsStreamOutDataDestinationBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.SSESourcePixelRowStoreBufferMemoryAddressAttributesReadWrite = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.HCPScalabilitySliceStateBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.HCPScalabilityCABACDecodedSyntaxElementsBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.MVUpperRightColumnStoreBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.IntraPredictionUpperRightColumnStoreBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.IntraPredictionLeftReconColumnStoreBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
#endif
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(HCP_IND_OBJ_BASE_ADDR_STATE), indirect) {
      indirect.HCPIndirectBitstreamObjectBaseAddress =
         anv_address_add(src_buffer->address, frame_info->srcBufferOffset & ~4095);

      indirect.HCPIndirectBitstreamObjectMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, src_buffer->address.bo, 0),
      };

      indirect.HCPIndirectBitstreamObjectAccessUpperBound =
         anv_address_add(src_buffer->address, align64(frame_info->srcBufferRange, 4096));

      indirect.HCPIndirectCUObjectMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };

      indirect.HCPPAKBSEObjectMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };

#if GFX_VER >= 11
      indirect.HCPVP9PAKCompressedHeaderSyntaxStreamInMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      indirect.HCPVP9PAKProbabilityCounterStreamOutMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      indirect.HCPVP9PAKProbabilityDeltasStreamInMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      indirect.HCPVP9PAKTileRecordStreamOutMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      indirect.HCPVP9PAKCULevelStatisticStreamOutMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
#endif
   }

   const StdVideoH265ScalingLists *scaling_lists = NULL;
   vk_video_derive_h265_scaling_list(sps, pps, &scaling_lists);

   if (scaling_lists) {
      scaling_list(cmd_buffer, scaling_lists);
   } else {
      for (uint8_t size = 0; size < 4; size++) {
         for (uint8_t pred = 0; pred < 2; pred++) {
            for (uint8_t color = 0; color < 3; color++) {

               if (size == 3 && color > 0)
                  continue;

               anv_batch_emit(&cmd_buffer->batch, GENX(HCP_QM_STATE), qm) {
                  qm.SizeID = size;
                  qm.PredictionType = pred;
                  qm.ColorComponent = color;
                  qm.DCCoefficient = (size > 1) ? 16 : 0;
                  unsigned len = (size == 0) ? 16 : 64;

                  for (uint8_t q = 0; q < len; q++)
                     qm.QuantizerMatrix8x8[q] = 0x10;
               }
            }
         }
      }
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(HCP_PIC_STATE), pic) {
      pic.FrameWidthInMinimumCodingBlockSize =
         sps->pic_width_in_luma_samples / (1 << (sps->log2_min_luma_coding_block_size_minus3 + 3)) - 1;
      pic.FrameHeightInMinimumCodingBlockSize =
         sps->pic_height_in_luma_samples / (1 << (sps->log2_min_luma_coding_block_size_minus3 + 3))  - 1;

      pic.MinCUSize = sps->log2_min_luma_coding_block_size_minus3 & 0x3;
      pic.LCUSize = (sps->log2_diff_max_min_luma_coding_block_size +
                     sps->log2_min_luma_coding_block_size_minus3) & 0x3;

      pic.MinTUSize = sps->log2_min_luma_transform_block_size_minus2 & 0x3;
      pic.MaxTUSize = (sps->log2_diff_max_min_luma_transform_block_size + sps->log2_min_luma_transform_block_size_minus2) & 0x3;
      pic.MinPCMSize = sps->log2_min_pcm_luma_coding_block_size_minus3 & 0x3;
      pic.MaxPCMSize = (sps->log2_diff_max_min_pcm_luma_coding_block_size + sps->log2_min_pcm_luma_coding_block_size_minus3) & 0x3;

#if GFX_VER >= 11
      pic.Log2SAOOffsetScaleLuma = pps->log2_sao_offset_scale_luma;
      pic.Log2SAOOffsetScaleChroma = pps->log2_sao_offset_scale_chroma;
      pic.ChromaQPOffsetListLength = pps->chroma_qp_offset_list_len_minus1;
      pic.DiffCUChromaQPOffsetDepth = pps->diff_cu_chroma_qp_offset_depth;
      pic.ChromaQPOffsetListEnable = pps->flags.chroma_qp_offset_list_enabled_flag;
      pic.ChromaSubsampling = sps->chroma_format_idc;

      pic.HighPrecisionOffsetsEnable = sps->flags.high_precision_offsets_enabled_flag;
      pic.Log2MaxTransformSkipSize = pps->log2_max_transform_skip_block_size_minus2 + 2;
      pic.CrossComponentPredictionEnable = pps->flags.cross_component_prediction_enabled_flag;
      pic.CABACBypassAlignmentEnable = sps->flags.cabac_bypass_alignment_enabled_flag;
      pic.PersistentRiceAdaptationEnable = sps->flags.persistent_rice_adaptation_enabled_flag;
      pic.IntraSmoothingDisable = sps->flags.intra_smoothing_disabled_flag;
      pic.ExplicitRDPCMEnable = sps->flags.explicit_rdpcm_enabled_flag;
      pic.ImplicitRDPCMEnable = sps->flags.implicit_rdpcm_enabled_flag;
      pic.TransformSkipContextEnable = sps->flags.transform_skip_context_enabled_flag;
      pic.TransformSkipRotationEnable = sps->flags.transform_skip_rotation_enabled_flag;
      pic.SPSRangeExtensionEnable = sps->flags.sps_range_extension_flag;
#endif

      pic.CollocatedPictureIsISlice = false;
      pic.CurrentPictureIsISlice = false;
      pic.SampleAdaptiveOffsetEnable = sps->flags.sample_adaptive_offset_enabled_flag;
      pic.PCMEnable = sps->flags.pcm_enabled_flag;
      pic.CUQPDeltaEnable = pps->flags.cu_qp_delta_enabled_flag;
      pic.MaxDQPDepth = pps->diff_cu_qp_delta_depth;
      pic.PCMLoopFilterDisable = sps->flags.pcm_loop_filter_disabled_flag;
      pic.ConstrainedIntraPrediction = pps->flags.constrained_intra_pred_flag;
      pic.Log2ParallelMergeLevel = pps->log2_parallel_merge_level_minus2;
      pic.SignDataHiding = pps->flags.sign_data_hiding_enabled_flag;
      pic.LoopFilterEnable = pps->flags.loop_filter_across_tiles_enabled_flag;
      pic.EntropyCodingSyncEnable = pps->flags.entropy_coding_sync_enabled_flag;
      pic.TilingEnable = pps->flags.tiles_enabled_flag;
      pic.WeightedBiPredicationEnable = pps->flags.weighted_bipred_flag;
      pic.WeightedPredicationEnable = pps->flags.weighted_pred_flag;
      pic.FieldPic = 0;
      pic.TopField = true;
      pic.TransformSkipEnable = pps->flags.transform_skip_enabled_flag;
      pic.AMPEnable = sps->flags.amp_enabled_flag;
      pic.TransquantBypassEnable = pps->flags.transquant_bypass_enabled_flag;
      pic.StrongIntraSmoothingEnable = sps->flags.strong_intra_smoothing_enabled_flag;
      pic.CUPacketStructure = 0;

      pic.PictureCbQPOffset = pps->pps_cb_qp_offset;
      pic.PictureCrQPOffset = pps->pps_cr_qp_offset;
      pic.IntraMaxTransformHierarchyDepth = sps->max_transform_hierarchy_depth_intra;
      pic.InterMaxTransformHierarchyDepth = sps->max_transform_hierarchy_depth_inter;
      pic.ChromaPCMSampleBitDepth = sps->pcm_sample_bit_depth_chroma_minus1 & 0xf;
      pic.LumaPCMSampleBitDepth = sps->pcm_sample_bit_depth_luma_minus1 & 0xf;

      pic.ChromaBitDepth = sps->bit_depth_chroma_minus8;
      pic.LumaBitDepth = sps->bit_depth_luma_minus8;

#if GFX_VER >= 11
      pic.CbQPOffsetList0 = pps->cb_qp_offset_list[0];
      pic.CbQPOffsetList1 = pps->cb_qp_offset_list[1];
      pic.CbQPOffsetList2 = pps->cb_qp_offset_list[2];
      pic.CbQPOffsetList3 = pps->cb_qp_offset_list[3];
      pic.CbQPOffsetList4 = pps->cb_qp_offset_list[4];
      pic.CbQPOffsetList5 = pps->cb_qp_offset_list[5];

      pic.CrQPOffsetList0 = pps->cr_qp_offset_list[0];
      pic.CrQPOffsetList1 = pps->cr_qp_offset_list[1];
      pic.CrQPOffsetList2 = pps->cr_qp_offset_list[2];
      pic.CrQPOffsetList3 = pps->cr_qp_offset_list[3];
      pic.CrQPOffsetList4 = pps->cr_qp_offset_list[4];
      pic.CrQPOffsetList5 = pps->cr_qp_offset_list[5];
#endif
   }

   if (pps->flags.tiles_enabled_flag) {
      int cum = 0;
      anv_batch_emit(&cmd_buffer->batch, GENX(HCP_TILE_STATE), tile) {
         tile.NumberofTileColumns = pps->num_tile_columns_minus1;
         tile.NumberofTileRows = pps->num_tile_rows_minus1;
         for (unsigned i = 0; i < 5; i++) {
            tile.ColumnPosition[i].CtbPos0i = cum;
            if ((4 * i) == pps->num_tile_columns_minus1)
               break;

            cum += pps->column_width_minus1[4 * i] + 1;
            tile.ColumnPosition[i].CtbPos1i = cum;

            if ((4 * i + 1) == pps->num_tile_columns_minus1)
               break;
            cum += pps->column_width_minus1[4 * i + 1] + 1;
            tile.ColumnPosition[i].CtbPos2i = cum;

            if ((4 * i + 2) == pps->num_tile_columns_minus1)
               break;
            cum += pps->column_width_minus1[4 * i + 2] + 1;
            tile.ColumnPosition[i].CtbPos3i = cum;

            if ((4 * i + 3) >= MIN2(pps->num_tile_columns_minus1,
                                    ARRAY_SIZE(pps->column_width_minus1)))
               break;

            cum += pps->column_width_minus1[4 * i + 3] + 1;
         }

         cum = 0;

         for (unsigned i = 0; i < 5; i++) {
            tile.Rowposition[i].CtbPos0i = cum;
            if ((4 * i) == pps->num_tile_rows_minus1)
               break;

            cum += pps->row_height_minus1[4 * i] + 1;
            tile.Rowposition[i].CtbPos1i = cum;

            if ((4 * i + 1) == pps->num_tile_rows_minus1)
               break;
            cum += pps->row_height_minus1[4 * i + 1] + 1;
            tile.Rowposition[i].CtbPos2i = cum;

            if ((4 * i + 2) == pps->num_tile_rows_minus1)
               break;
            cum += pps->row_height_minus1[4 * i + 2] + 1;
            tile.Rowposition[i].CtbPos3i = cum;

            if ((4 * i + 3) == pps->num_tile_rows_minus1)
               break;

            cum += pps->row_height_minus1[4 * i + 3] + 1;
         }

         if (pps->num_tile_rows_minus1 == 20) {
            tile.Rowposition[5].CtbPos0i = cum;
         }
         if (pps->num_tile_rows_minus1 == 20) {
            tile.Rowposition[5].CtbPos0i = cum;
            cum += pps->row_height_minus1[20] + 1;
            tile.Rowposition[5].CtbPos1i = cum;
         }
      }
   }

   /* Slice parsing */
   uint32_t last_slice = h265_pic_info->sliceSegmentCount - 1;
   void *slice_map;
   VkResult result =
      anv_device_map_bo(cmd_buffer->device,
                        src_buffer->address.bo,
                        src_buffer->address.offset,
                        frame_info->srcBufferRange + frame_info->srcBufferOffset,
                        NULL /* placed_addr */,
                        &slice_map);
   if (result != VK_SUCCESS) {
      anv_batch_set_error(&cmd_buffer->batch, result);
      return;
   }

   slice_map += frame_info->srcBufferOffset;

   struct vk_video_h265_slice_params slice_params[h265_pic_info->sliceSegmentCount];

   /* All slices should be parsed in advance to collect information necessary */
   for (unsigned s = 0; s < h265_pic_info->sliceSegmentCount; s++) {
      uint32_t current_offset = h265_pic_info->pSliceSegmentOffsets[s];
      void *map = slice_map + current_offset;
      uint32_t slice_size = 0;

      if (s == last_slice)
         slice_size = frame_info->srcBufferRange - current_offset;
      else
         slice_size = h265_pic_info->pSliceSegmentOffsets[s + 1] - current_offset;

      vk_video_parse_h265_slice_header(frame_info, h265_pic_info, sps, pps, map, slice_size, &slice_params[s]);
      vk_fill_video_h265_reference_info(frame_info, h265_pic_info, &slice_params[s], ref_slots);
   }

   anv_device_unmap_bo(cmd_buffer->device, src_buffer->address.bo,
                       slice_map, frame_info->srcBufferRange,
                       false /* replace */);

   for (unsigned s = 0; s < h265_pic_info->sliceSegmentCount; s++) {
      uint32_t ctb_size = 1 << (sps->log2_diff_max_min_luma_coding_block_size +
          sps->log2_min_luma_coding_block_size_minus3 + 3);
      uint32_t pic_width_in_min_cbs_y = sps->pic_width_in_luma_samples /
         (1 << (sps->log2_min_luma_coding_block_size_minus3 + 3));
      uint32_t width_in_pix = (1 << (sps->log2_min_luma_coding_block_size_minus3 + 3)) *
         pic_width_in_min_cbs_y;
      uint32_t ctb_w = DIV_ROUND_UP(width_in_pix, ctb_size);
      bool is_last = (s == last_slice);
      int slice_qp = (slice_params[s].slice_qp_delta + pps->init_qp_minus26 + 26) & 0x3f;

      anv_batch_emit(&cmd_buffer->batch, GENX(HCP_SLICE_STATE), slice) {
         slice.SliceHorizontalPosition = slice_params[s].slice_segment_address % ctb_w;
         slice.SliceVerticalPosition = slice_params[s].slice_segment_address / ctb_w;

         if (is_last) {
            slice.NextSliceHorizontalPosition = 0;
            slice.NextSliceVerticalPosition = 0;
         } else {
            slice.NextSliceHorizontalPosition = (slice_params[s + 1].slice_segment_address) % ctb_w;
            slice.NextSliceVerticalPosition = (slice_params[s + 1].slice_segment_address) / ctb_w;
         }

         slice.SliceType = slice_params[s].slice_type;
         slice.LastSlice = is_last;
         slice.DependentSlice = slice_params[s].dependent_slice_segment;
         slice.SliceTemporalMVPEnable = slice_params[s].temporal_mvp_enable;
         slice.SliceQP = abs(slice_qp);
         slice.SliceQPSign = slice_qp >= 0 ? 0 : 1;
         slice.SliceCbQPOffset = slice_params[s].slice_cb_qp_offset;
         slice.SliceCrQPOffset = slice_params[s].slice_cr_qp_offset;
         slice.SliceHeaderDisableDeblockingFilter = pps->flags.deblocking_filter_override_enabled_flag ?
               slice_params[s].disable_deblocking_filter_idc : pps->flags.pps_deblocking_filter_disabled_flag;
         slice.SliceTCOffsetDiv2 = pps->flags.deblocking_filter_override_enabled_flag ?
               slice_params[s].tc_offset_div2 : pps->pps_tc_offset_div2;
         slice.SliceBetaOffsetDiv2 = pps->flags.deblocking_filter_override_enabled_flag ?
               slice_params[s].beta_offset_div2 : pps->pps_beta_offset_div2;
         slice.SliceLoopFilterEnable = slice_params[s].loop_filter_across_slices_enable;
         slice.SliceSAOChroma = slice_params[s].sao_chroma_flag;
         slice.SliceSAOLuma = slice_params[s].sao_luma_flag;
         slice.MVDL1Zero = slice_params[s].mvd_l1_zero_flag;

         uint8_t low_delay = true;

         if (slice_params[s].slice_type == STD_VIDEO_H265_SLICE_TYPE_I) {
            low_delay = false;
         } else {
            for (unsigned i = 0; i < slice_params[s].num_ref_idx_l0_active; i++) {
               int slot_idx = ref_slots[0][i].slot_index;

               if (vk_video_h265_poc_by_slot(frame_info, slot_idx) >
                     h265_pic_info->pStdPictureInfo->PicOrderCntVal) {
                  low_delay = false;
                  break;
               }
            }

            for (unsigned i = 0; i < slice_params[s].num_ref_idx_l1_active; i++) {
               int slot_idx = ref_slots[1][i].slot_index;
               if (vk_video_h265_poc_by_slot(frame_info, slot_idx) >
                     h265_pic_info->pStdPictureInfo->PicOrderCntVal) {
                  low_delay = false;
                  break;
               }
            }
         }

         slice.LowDelay = low_delay;
         slice.CollocatedFromL0 = slice_params[s].collocated_list == 0 ? true : false;
         slice.Log2WeightDenominatorChroma = slice_params[s].luma_log2_weight_denom +
            (slice_params[s].chroma_log2_weight_denom - slice_params[s].luma_log2_weight_denom);
         slice.Log2WeightDenominatorLuma = slice_params[s].luma_log2_weight_denom;
         slice.CABACInit = slice_params[s].cabac_init_idc;
         slice.MaxMergeIndex = slice_params[s].max_num_merge_cand - 1;
         slice.CollocatedMVTemporalBufferIndex =
            dpb_idx[ref_slots[slice_params[s].collocated_list][slice_params[s].collocated_ref_idx].slot_index];
         assert(slice.CollocatedMVTemporalBufferIndex < ANV_VIDEO_H265_HCP_NUM_REF_FRAME);

         slice.SliceHeaderLength = slice_params[s].slice_data_bytes_offset;
         slice.CABACZeroWordInsertionEnable = false;
         slice.EmulationByteSliceInsertEnable = false;
         slice.TailInsertionPresent = false;
         slice.SliceDataInsertionPresent = false;
         slice.HeaderInsertionPresent = false;

         slice.IndirectPAKBSEDataStartOffset = 0;
         slice.TransformSkipLambda = 0;
         slice.TransformSkipNumberofNonZeroCoeffsFactor0 = 0;
         slice.TransformSkipNumberofZeroCoeffsFactor0 = 0;
         slice.TransformSkipNumberofNonZeroCoeffsFactor1 = 0;
         slice.TransformSkipNumberofZeroCoeffsFactor1 = 0;

#if GFX_VER >= 12
         slice.OriginalSliceStartCtbX = slice_params[s].slice_segment_address % ctb_w;
         slice.OriginalSliceStartCtbY = slice_params[s].slice_segment_address / ctb_w;
#endif
      }

      if (slice_params[s].slice_type != STD_VIDEO_H265_SLICE_TYPE_I) {
         anv_batch_emit(&cmd_buffer->batch, GENX(HCP_REF_IDX_STATE), ref) {
            ref.ReferencePictureListSelect = 0;
            ref.NumberofReferenceIndexesActive = slice_params[s].num_ref_idx_l0_active - 1;

            for (unsigned i = 0; i < ref.NumberofReferenceIndexesActive + 1; i++) {
               int slot_idx = ref_slots[0][i].slot_index;
               unsigned poc = ref_slots[0][i].pic_order_cnt;
               int32_t diff_poc = h265_pic_info->pStdPictureInfo->PicOrderCntVal - poc;

               assert(dpb_idx[slot_idx] < ANV_VIDEO_H265_HCP_NUM_REF_FRAME);

               ref.ReferenceListEntry[i].ListEntry = dpb_idx[slot_idx];
               ref.ReferenceListEntry[i].ReferencePicturetbValue = CLAMP(diff_poc, -128, 127) & 0xff;
               ref.ReferenceListEntry[i].TopField = true;
               ref.ReferenceListEntry[i].LongTermReference = ref_slots[0][i].lt;
            }
         }
      }

      if (slice_params[s].slice_type == STD_VIDEO_H265_SLICE_TYPE_B) {
         anv_batch_emit(&cmd_buffer->batch, GENX(HCP_REF_IDX_STATE), ref) {
            ref.ReferencePictureListSelect = 1;
            ref.NumberofReferenceIndexesActive = slice_params[s].num_ref_idx_l1_active - 1;

            for (unsigned i = 0; i < ref.NumberofReferenceIndexesActive + 1; i++) {
               int slot_idx = ref_slots[1][i].slot_index;;
               unsigned poc = ref_slots[1][i].pic_order_cnt;
               int32_t diff_poc = h265_pic_info->pStdPictureInfo->PicOrderCntVal - poc;

               assert(dpb_idx[slot_idx] < ANV_VIDEO_H265_HCP_NUM_REF_FRAME);

               ref.ReferenceListEntry[i].ListEntry = dpb_idx[slot_idx];
               ref.ReferenceListEntry[i].ReferencePicturetbValue = CLAMP(diff_poc, -128, 127) & 0xff;
               ref.ReferenceListEntry[i].TopField = true;
               ref.ReferenceListEntry[i].LongTermReference = ref_slots[1][i].lt;
            }
         }
      }

      if ((pps->flags.weighted_pred_flag && (slice_params[s].slice_type == STD_VIDEO_H265_SLICE_TYPE_P)) ||
            (pps->flags.weighted_bipred_flag && (slice_params[s].slice_type == STD_VIDEO_H265_SLICE_TYPE_B))) {
         anv_batch_emit(&cmd_buffer->batch, GENX(HCP_WEIGHTOFFSET_STATE), w) {
            w.ReferencePictureListSelect = 0;

            for (unsigned i = 0; i < ANV_VIDEO_H265_MAX_NUM_REF_FRAME; i++) {
               w.LumaOffsets[i].DeltaLumaWeightLX = slice_params[s].delta_luma_weight_l0[i] & 0xff;
               w.LumaOffsets[i].LumaOffsetLX = slice_params[s].luma_offset_l0[i] & 0xff;
               w.ChromaOffsets[i].DeltaChromaWeightLX0 = slice_params[s].delta_chroma_weight_l0[i][0] & 0xff;
               w.ChromaOffsets[i].ChromaOffsetLX0 = slice_params[s].chroma_offset_l0[i][0] & 0xff;
               w.ChromaOffsets[i].DeltaChromaWeightLX1 = slice_params[s].delta_chroma_weight_l0[i][1] & 0xff;
               w.ChromaOffsets[i].ChromaOffsetLX1 = slice_params[s].chroma_offset_l0[i][1] & 0xff;
            }
         }

         if (slice_params[s].slice_type == STD_VIDEO_H265_SLICE_TYPE_B) {
            anv_batch_emit(&cmd_buffer->batch, GENX(HCP_WEIGHTOFFSET_STATE), w) {
               w.ReferencePictureListSelect = 1;

               for (unsigned i = 0; i < ANV_VIDEO_H265_MAX_NUM_REF_FRAME; i++) {
                  w.LumaOffsets[i].DeltaLumaWeightLX = slice_params[s].delta_luma_weight_l1[i] & 0xff;
                  w.LumaOffsets[i].LumaOffsetLX = slice_params[s].luma_offset_l1[i] & 0xff;
                  w.ChromaOffsets[i].DeltaChromaWeightLX0 = slice_params[s].delta_chroma_weight_l1[i][0] & 0xff;
                  w.ChromaOffsets[i].DeltaChromaWeightLX1 = slice_params[s].delta_chroma_weight_l1[i][1] & 0xff;
                  w.ChromaOffsets[i].ChromaOffsetLX0 = slice_params[s].chroma_offset_l1[i][0] & 0xff;
                  w.ChromaOffsets[i].ChromaOffsetLX1 = slice_params[s].chroma_offset_l1[i][1] & 0xff;
               }
            }
         }
      }

      uint32_t buffer_offset = frame_info->srcBufferOffset & 4095;

      anv_batch_emit(&cmd_buffer->batch, GENX(HCP_BSD_OBJECT), bsd) {
         bsd.IndirectBSDDataLength = slice_params[s].slice_size - 3;
         bsd.IndirectBSDDataStartAddress = buffer_offset + h265_pic_info->pSliceSegmentOffsets[s] + 3;
      }
   }

#if GFX_VER >= 12
   anv_batch_emit(&cmd_buffer->batch, GENX(VD_CONTROL_STATE), cs) {
      cs.MemoryImplicitFlush = true;
   }
#endif

   anv_batch_emit(&cmd_buffer->batch, GENX(VD_PIPELINE_FLUSH), flush) {
      flush.HEVCPipelineDone = true;
      flush.HEVCPipelineCommandFlush = true;
      flush.VDCommandMessageParserDone = true;
   }
}

static void
anv_h264_decode_video(struct anv_cmd_buffer *cmd_buffer,
                      const VkVideoDecodeInfoKHR *frame_info)
{
   ANV_FROM_HANDLE(anv_buffer, src_buffer, frame_info->srcBuffer);
   struct anv_video_session *vid = cmd_buffer->video.vid;
   struct vk_video_session_parameters *params = cmd_buffer->video.params;
   const struct VkVideoDecodeH264PictureInfoKHR *h264_pic_info =
      vk_find_struct_const(frame_info->pNext, VIDEO_DECODE_H264_PICTURE_INFO_KHR);

   const StdVideoH264SequenceParameterSet *sps;
   const StdVideoH264PictureParameterSet *pps;

   vk_video_get_h264_parameters(&vid->vk, params, frame_info, h264_pic_info, &sps, &pps);

   uint8_t dpb_slots[ANV_VIDEO_H264_MAX_DPB_SLOTS] = { 0,};

   anv_batch_emit(&cmd_buffer->batch, GENX(MI_FLUSH_DW), flush) {
      flush.DWordLength = 2;
      flush.VideoPipelineCacheInvalidate = 1;
   };

#if GFX_VER >= 12
   anv_batch_emit(&cmd_buffer->batch, GENX(MI_FORCE_WAKEUP), wake) {
      wake.MFXPowerWellControl = 1;
      wake.MaskBits = 768;
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(MFX_WAIT), mfx) {
      mfx.MFXSyncControlFlag = 1;
   }
#endif

   anv_batch_emit(&cmd_buffer->batch, GENX(MFX_PIPE_MODE_SELECT), sel) {
      sel.StandardSelect = SS_AVC;
      sel.CodecSelect = Decode;
      sel.DecoderShortFormatMode = ShortFormatDriverInterface;
      sel.DecoderModeSelect = VLDMode; // Hardcoded

      sel.PreDeblockingOutputEnable = 0;
      sel.PostDeblockingOutputEnable = 1;
   }

#if GFX_VER >= 12
   anv_batch_emit(&cmd_buffer->batch, GENX(MFX_WAIT), mfx) {
      mfx.MFXSyncControlFlag = 1;
   }
#endif

   const struct anv_image_view *iv = anv_image_view_from_handle(frame_info->dstPictureResource.imageViewBinding);
   const struct anv_image *img = iv->image;
   anv_batch_emit(&cmd_buffer->batch, GENX(MFX_SURFACE_STATE), ss) {
      ss.Width = img->vk.extent.width - 1;
      ss.Height = img->vk.extent.height - 1;
      ss.SurfaceFormat = PLANAR_420_8; // assert on this?
      ss.InterleaveChroma = 1;
      ss.SurfacePitch = img->planes[0].primary_surface.isl.row_pitch_B - 1;
      ss.TiledSurface = img->planes[0].primary_surface.isl.tiling != ISL_TILING_LINEAR;
      ss.TileWalk = TW_YMAJOR;

      ss.YOffsetforUCb = ss.YOffsetforVCr =
         img->planes[1].primary_surface.memory_range.offset / img->planes[0].primary_surface.isl.row_pitch_B;
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(MFX_PIPE_BUF_ADDR_STATE), buf) {
      bool use_pre_deblock = false;
      if (use_pre_deblock) {
         buf.PreDeblockingDestinationAddress =
            anv_image_dpb_address(iv, frame_info->dstPictureResource.baseArrayLayer);
      } else {
         buf.PostDeblockingDestinationAddress =
            anv_image_dpb_address(iv, frame_info->dstPictureResource.baseArrayLayer);
      }
      buf.PreDeblockingDestinationAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.PreDeblockingDestinationAddress.bo, 0),
      };
      buf.PostDeblockingDestinationAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.PostDeblockingDestinationAddress.bo, 0),
      };

      buf.IntraRowStoreScratchBufferAddress = (struct anv_address) { vid->vid_mem[ANV_VID_MEM_H264_INTRA_ROW_STORE].mem->bo, vid->vid_mem[ANV_VID_MEM_H264_INTRA_ROW_STORE].offset };
      buf.IntraRowStoreScratchBufferAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.IntraRowStoreScratchBufferAddress.bo, 0),
      };
      buf.DeblockingFilterRowStoreScratchAddress = (struct anv_address) { vid->vid_mem[ANV_VID_MEM_H264_DEBLOCK_FILTER_ROW_STORE].mem->bo, vid->vid_mem[ANV_VID_MEM_H264_DEBLOCK_FILTER_ROW_STORE].offset };
      buf.DeblockingFilterRowStoreScratchAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.DeblockingFilterRowStoreScratchAddress.bo, 0),
      };
      buf.MBStatusBufferAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.MBILDBStreamOutBufferAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.SecondMBILDBStreamOutBufferAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.ScaledReferenceSurfaceAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.OriginalUncompressedPictureSourceAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.StreamOutDataDestinationAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };

      struct anv_bo *ref_bo = NULL;
      for (unsigned i = 0; i < frame_info->referenceSlotCount; i++) {
         const struct anv_image_view *ref_iv = anv_image_view_from_handle(frame_info->pReferenceSlots[i].pPictureResource->imageViewBinding);
         int idx = frame_info->pReferenceSlots[i].slotIndex;

         assert(idx < ANV_VIDEO_H264_MAX_DPB_SLOTS);

         if (idx < 0)
            continue;

         dpb_slots[idx] = i;

         buf.ReferencePictureAddress[i] =
            anv_image_dpb_address(ref_iv, frame_info->pReferenceSlots[i].pPictureResource->baseArrayLayer);

         if (i == 0) {
            ref_bo = ref_iv->image->bindings[0].address.bo;
         }
      }
      buf.ReferencePictureAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, ref_bo, 0),
      };
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(MFX_IND_OBJ_BASE_ADDR_STATE), index_obj) {
      index_obj.MFXIndirectBitstreamObjectAddress = anv_address_add(src_buffer->address,
                                                                    frame_info->srcBufferOffset & ~4095);
      index_obj.MFXIndirectBitstreamObjectAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, src_buffer->address.bo, 0),
      };
      index_obj.MFXIndirectMVObjectAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      index_obj.MFDIndirectITCOEFFObjectAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      index_obj.MFDIndirectITDBLKObjectAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      index_obj.MFCIndirectPAKBSEObjectAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(MFX_BSP_BUF_BASE_ADDR_STATE), bsp) {
      bsp.BSDMPCRowStoreScratchBufferAddress = (struct anv_address) { vid->vid_mem[ANV_VID_MEM_H264_BSD_MPC_ROW_SCRATCH].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H264_BSD_MPC_ROW_SCRATCH].offset };

      bsp.BSDMPCRowStoreScratchBufferAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, bsp.BSDMPCRowStoreScratchBufferAddress.bo, 0),
      };
      bsp.MPRRowStoreScratchBufferAddress = (struct anv_address) { vid->vid_mem[ANV_VID_MEM_H264_MPR_ROW_SCRATCH].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H264_MPR_ROW_SCRATCH].offset };

      bsp.MPRRowStoreScratchBufferAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, bsp.MPRRowStoreScratchBufferAddress.bo, 0),
      };
      bsp.BitplaneReadBufferAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(MFD_AVC_DPB_STATE), avc_dpb) {
      for (unsigned i = 0; i < frame_info->referenceSlotCount; i++) {
         const struct VkVideoDecodeH264DpbSlotInfoKHR *dpb_slot =
            vk_find_struct_const(frame_info->pReferenceSlots[i].pNext, VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR);
         const StdVideoDecodeH264ReferenceInfo *ref_info = dpb_slot->pStdReferenceInfo;

         if (frame_info->pReferenceSlots[i].slotIndex < 0)
            continue;

         int idx = dpb_slots[frame_info->pReferenceSlots[i].slotIndex];

         avc_dpb.NonExistingFrame[idx] = ref_info->flags.is_non_existing;
         avc_dpb.LongTermFrame[idx] = ref_info->flags.used_for_long_term_reference;
         if (!ref_info->flags.top_field_flag && !ref_info->flags.bottom_field_flag)
            avc_dpb.UsedforReference[idx] = 3;
         else
            avc_dpb.UsedforReference[idx] = ref_info->flags.top_field_flag | (ref_info->flags.bottom_field_flag << 1);
         avc_dpb.LTSTFrameNumberList[idx] = ref_info->FrameNum;
      }
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(MFD_AVC_PICID_STATE), picid) {
      unsigned i = 0;
      picid.PictureIDRemappingDisable = false;

      for (i = 0; i < frame_info->referenceSlotCount; i++)
         picid.PictureID[i] = frame_info->pReferenceSlots[i].slotIndex;

      for (; i < ANV_VIDEO_H264_MAX_NUM_REF_FRAME; i++)
         picid.PictureID[i] = 0xffff;
   }

   uint32_t pic_height = sps->pic_height_in_map_units_minus1 + 1;
   if (!sps->flags.frame_mbs_only_flag)
      pic_height *= 2;
   anv_batch_emit(&cmd_buffer->batch, GENX(MFX_AVC_IMG_STATE), avc_img) {
      avc_img.FrameWidth = sps->pic_width_in_mbs_minus1;
      avc_img.FrameHeight = pic_height - 1;
      avc_img.FrameSize = (sps->pic_width_in_mbs_minus1 + 1) * pic_height;

      if (!h264_pic_info->pStdPictureInfo->flags.field_pic_flag)
         avc_img.ImageStructure = FramePicture;
      else if (h264_pic_info->pStdPictureInfo->flags.bottom_field_flag)
         avc_img.ImageStructure = BottomFieldPicture;
      else
         avc_img.ImageStructure = TopFieldPicture;

      avc_img.WeightedBiPredictionIDC = pps->weighted_bipred_idc;
      avc_img.WeightedPredictionEnable = pps->flags.weighted_pred_flag;
      avc_img.FirstChromaQPOffset = pps->chroma_qp_index_offset;
      avc_img.SecondChromaQPOffset = pps->second_chroma_qp_index_offset;
      avc_img.FieldPicture = h264_pic_info->pStdPictureInfo->flags.field_pic_flag;
      avc_img.MBAFFMode = (sps->flags.mb_adaptive_frame_field_flag &&
                           !h264_pic_info->pStdPictureInfo->flags.field_pic_flag);
      avc_img.FrameMBOnly = sps->flags.frame_mbs_only_flag;
      avc_img._8x8IDCTTransformMode = pps->flags.transform_8x8_mode_flag;
      avc_img.Direct8x8Inference = sps->flags.direct_8x8_inference_flag;
      avc_img.ConstrainedIntraPrediction = pps->flags.constrained_intra_pred_flag;
      avc_img.NonReferencePicture = !h264_pic_info->pStdPictureInfo->flags.is_reference;
      avc_img.EntropyCodingSyncEnable = pps->flags.entropy_coding_mode_flag;
      avc_img.ChromaFormatIDC = sps->chroma_format_idc;
      avc_img.TrellisQuantizationChromaDisable = true;
      avc_img.NumberofReferenceFrames = frame_info->referenceSlotCount;
      avc_img.NumberofActiveReferencePicturesfromL0 = pps->num_ref_idx_l0_default_active_minus1 + 1;
      avc_img.NumberofActiveReferencePicturesfromL1 = pps->num_ref_idx_l1_default_active_minus1 + 1;
      avc_img.InitialQPValue = pps->pic_init_qp_minus26;
      avc_img.PicOrderPresent = pps->flags.bottom_field_pic_order_in_frame_present_flag;
      avc_img.DeltaPicOrderAlwaysZero = sps->flags.delta_pic_order_always_zero_flag;
      avc_img.PicOrderCountType = sps->pic_order_cnt_type;
      avc_img.DeblockingFilterControlPresent = pps->flags.deblocking_filter_control_present_flag;
      avc_img.RedundantPicCountPresent = pps->flags.redundant_pic_cnt_present_flag;
      avc_img.Log2MaxFrameNumber = sps->log2_max_frame_num_minus4;
      avc_img.Log2MaxPicOrderCountLSB = sps->log2_max_pic_order_cnt_lsb_minus4;
      avc_img.CurrentPictureFrameNumber = h264_pic_info->pStdPictureInfo->frame_num;
   }

   StdVideoH264ScalingLists scaling_lists;
   vk_video_derive_h264_scaling_list(sps, pps, &scaling_lists);
   anv_batch_emit(&cmd_buffer->batch, GENX(MFX_QM_STATE), qm) {
      qm.DWordLength = 16;
      qm.AVC = AVC_4x4_Intra_MATRIX;
      for (unsigned m = 0; m < 3; m++)
         for (unsigned q = 0; q < 16; q++)
            qm.ForwardQuantizerMatrix[m * 16 + vl_zscan_normal_16[q]] = scaling_lists.ScalingList4x4[m][q];
   }
   anv_batch_emit(&cmd_buffer->batch, GENX(MFX_QM_STATE), qm) {
      qm.DWordLength = 16;
      qm.AVC = AVC_4x4_Inter_MATRIX;
      for (unsigned m = 0; m < 3; m++)
         for (unsigned q = 0; q < 16; q++)
            qm.ForwardQuantizerMatrix[m * 16 + vl_zscan_normal_16[q]] = scaling_lists.ScalingList4x4[m + 3][q];
   }
   if (pps->flags.transform_8x8_mode_flag) {
      anv_batch_emit(&cmd_buffer->batch, GENX(MFX_QM_STATE), qm) {
         qm.DWordLength = 16;
         qm.AVC = AVC_8x8_Intra_MATRIX;
         for (unsigned q = 0; q < 64; q++)
            qm.ForwardQuantizerMatrix[vl_zscan_normal[q]] = scaling_lists.ScalingList8x8[0][q];
      }
      anv_batch_emit(&cmd_buffer->batch, GENX(MFX_QM_STATE), qm) {
         qm.DWordLength = 16;
         qm.AVC = AVC_8x8_Inter_MATRIX;
         for (unsigned q = 0; q < 64; q++)
            qm.ForwardQuantizerMatrix[vl_zscan_normal[q]] = scaling_lists.ScalingList8x8[1][q];
      }
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(MFX_AVC_DIRECTMODE_STATE), avc_directmode) {
      /* bind reference frame DMV */
      struct anv_bo *dmv_bo = NULL;
      for (unsigned i = 0; i < frame_info->referenceSlotCount; i++) {
         if (frame_info->pReferenceSlots[i].slotIndex < 0)
            continue;

         int idx = dpb_slots[frame_info->pReferenceSlots[i].slotIndex];

         const struct VkVideoDecodeH264DpbSlotInfoKHR *dpb_slot =
            vk_find_struct_const(frame_info->pReferenceSlots[i].pNext, VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR);
         const struct anv_image_view *ref_iv = anv_image_view_from_handle(frame_info->pReferenceSlots[i].pPictureResource->imageViewBinding);
         const StdVideoDecodeH264ReferenceInfo *ref_info = dpb_slot->pStdReferenceInfo;
         avc_directmode.DirectMVBufferAddress[idx] =
            anv_image_dmv_top_address(ref_iv, frame_info->pReferenceSlots[i].pPictureResource->baseArrayLayer);
         if (i == 0) {
            dmv_bo = ref_iv->image->bindings[0].address.bo;
         }
         avc_directmode.POCList[2 * idx] = ref_info->PicOrderCnt[0];
         avc_directmode.POCList[2 * idx + 1] = ref_info->PicOrderCnt[1];
      }
      avc_directmode.DirectMVBufferAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, dmv_bo, 0),
      };

      avc_directmode.DirectMVBufferWriteAddress =
         anv_image_dmv_top_address(iv, frame_info->dstPictureResource.baseArrayLayer);
      avc_directmode.DirectMVBufferWriteAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, img->bindings[0].address.bo, 0),
      };
      avc_directmode.POCList[32] = h264_pic_info->pStdPictureInfo->PicOrderCnt[0];
      avc_directmode.POCList[33] = h264_pic_info->pStdPictureInfo->PicOrderCnt[1];
   }

   uint32_t buffer_offset = frame_info->srcBufferOffset & 4095;
#define HEADER_OFFSET 3
   for (unsigned s = 0; s < h264_pic_info->sliceCount; s++) {
      bool last_slice = s == (h264_pic_info->sliceCount - 1);
      uint32_t current_offset = h264_pic_info->pSliceOffsets[s];
      uint32_t this_end;
      if (!last_slice) {
         uint32_t next_offset = h264_pic_info->pSliceOffsets[s + 1];
         uint32_t next_end = h264_pic_info->pSliceOffsets[s + 2];
         if (s == h264_pic_info->sliceCount - 2)
            next_end = frame_info->srcBufferRange;
         anv_batch_emit(&cmd_buffer->batch, GENX(MFD_AVC_SLICEADDR), sliceaddr) {
            sliceaddr.IndirectBSDDataLength = next_end - next_offset - HEADER_OFFSET;
            /* start decoding after the 3-byte header. */
            sliceaddr.IndirectBSDDataStartAddress = buffer_offset + next_offset + HEADER_OFFSET;
         };
         this_end = next_offset;
      } else
         this_end = frame_info->srcBufferRange;
      anv_batch_emit(&cmd_buffer->batch, GENX(MFD_AVC_BSD_OBJECT), avc_bsd) {
         avc_bsd.IndirectBSDDataLength = this_end - current_offset - HEADER_OFFSET;
         /* start decoding after the 3-byte header. */
         avc_bsd.IndirectBSDDataStartAddress = buffer_offset + current_offset + HEADER_OFFSET;
         avc_bsd.InlineData.LastSlice = last_slice;
         avc_bsd.InlineData.FixPrevMBSkipped = 1;
         avc_bsd.InlineData.IntraPredictionErrorControl = 1;
         avc_bsd.InlineData.Intra8x84x4PredictionErrorConcealmentControl = 1;
         avc_bsd.InlineData.ISliceConcealmentMode = 1;
      };
   }
}

#if GFX_VERx10 >= 120

enum av1_seg_index
{
   SEG_LVL_ALT_Q          = 0,            /* Use alternate Quantizer */
   SEG_LVL_ALT_LFYV,                      /* Use alternate loop filter value on y plane vertical */
   SEG_LVL_ALT_LFYH,                      /* Use alternate loop filter value on y plane horizontal */
   SEG_LVL_ALT_LFU,                       /* Use alternate loop filter value on u plane */
   SEG_LVL_ALT_LFV,                       /* Use alternate loop filter value on v plane */
   SEG_LVL_REF_FRAME,                     /* Optional Segment reference frame */
   SEG_LVL_SKIP,                          /* Optional Segment (0,0) + skip mode */
   SEG_LVL_GLOBAL_MV,                     /* Global MV */
};

enum av1_ref_frame
{
   AV1_NONE_FRAME               = -1,       /* none frame */
   AV1_INTRA_FRAME              = 0,        /* intra frame, which means the current frame */
   AV1_LAST_FRAME               = 1,        /* last frame */
   AV1_LAST2_FRAME              = 2,        /* last2 frame */
   AV1_LAST3_FRAME              = 3,        /* last3 frame */
   AV1_GOLDEN_FRAME             = 4,        /* golden frame */
   AV1_BWDREF_FRAME             = 5,        /* bwdref frame */
   AV1_ALTREF2_FRAME            = 6,        /* altref2 frame */
   AV1_ALTREF_FRAME             = 7,        /* altref frame */
   AV1_TOTAL_REFS_PER_FRAME     = 8,        /* total reference frame number */
   AV1_NUM_INTER_REFS           = AV1_ALTREF_FRAME - AV1_LAST_FRAME + 1   /* total number of inter ref frames */
};

enum av1_gm_type
{
   AV1_IDENTITY      = 0,
   AV1_TRANSLATION,
   AV1_ROTZOOM,
   AV1_AFFINE,
};

static const uint32_t btdl_cache_offset = 0;
static const uint32_t smvl_cache_offset = 128;
static const uint32_t ipdl_cache_offset = 384;
static const uint32_t dfly_cache_offset = 640;
static const uint32_t dflu_cache_offset = 1344;
static const uint32_t dflv_cache_offset = 1536;
static const uint32_t cdef_cache_offset = 1728;

static const uint32_t av1_max_qindex          = 255;
static const uint32_t av1_num_qm_levels       = 16;
static const uint32_t av1_scaling_factor      = (1 << 14);

static const uint32_t av1_warped_model_prec_bits  = 16;  /* Warp model precision bits */
static const uint32_t av1_gm_trans_prec_diff      = 10;  /* Warp model precision bits - gm transformation precision bits */
static const uint32_t av1_gm_trans_only_prec_diff = 13;  /* Warp model precision bits - 3 */
static const uint32_t av1_gm_alpha_prec_diff      = 1;   /* Warp model precision bits - gm alpha precision bits */

static const uint32_t av1_max_mib_size_log2 = 5;
static const uint32_t av1_min_mib_size_log2 = 4;
static const uint32_t av1_mi_size_log2 = 2;

static const uint32_t av1_rs_scale_subpel_bits = 14;
static const uint32_t av1_rs_scale_subpel_mask = 16383;
static const uint32_t av1_rs_scale_extra_off = 128;
static const uint32_t av1_mfmv_stack_size = 3;

static int32_t chroma_xstep_qn = 0;
static int32_t luma_xstep_qn = 0;
static int32_t chroma_x0_qn[64] = { 0, };
static int32_t luma_x0_qn[64] = { 0, };

static uint32_t
get_qindex(const VkVideoDecodeAV1PictureInfoKHR *av1_pic_info,
           uint32_t segment_id)
{
   const StdVideoDecodeAV1PictureInfo *std_pic_info = av1_pic_info->pStdPictureInfo;
   uint8_t base_qindex = std_pic_info->pQuantization->base_q_idx;
   uint32_t feature_mask = std_pic_info->pSegmentation->FeatureEnabled[segment_id];
   if (std_pic_info->flags.segmentation_enabled &&
       feature_mask & (1 << SEG_LVL_ALT_Q)) {
      int data = std_pic_info->pSegmentation->FeatureData[segment_id][SEG_LVL_ALT_Q];
      return CLAMP(base_qindex + data, 0, av1_max_qindex);
   } else
      return base_qindex;
}

static bool
frame_is_key_or_intra(const StdVideoAV1FrameType frame_type)
{
   return (frame_type == STD_VIDEO_AV1_FRAME_TYPE_INTRA_ONLY ||
           frame_type == STD_VIDEO_AV1_FRAME_TYPE_KEY);
}

static int32_t
get_relative_dist(const VkVideoDecodeAV1PictureInfoKHR *av1_pic_info,
                  const StdVideoAV1SequenceHeader *seq_hdr,
                  int32_t a, int32_t b)
{
   if (!seq_hdr->flags.enable_order_hint)
      return 0;

   int32_t bits = seq_hdr->order_hint_bits_minus_1 + 1;
   int32_t diff = a - b;
   int32_t m = 1 << (bits - 1);
   diff = (diff & (m - 1)) - (diff & m);

   return diff;
}

struct av1_refs_info {
   const struct anv_image_view *iv;
   uint32_t array_layer;
   uint8_t order_hint;
   uint8_t ref_order_hints[STD_VIDEO_AV1_NUM_REF_FRAMES];
   uint8_t disable_frame_end_update_cdf;
   uint8_t idx;
   uint8_t frame_type;
   uint32_t frame_width;
   uint32_t frame_height;
   uint8_t default_cdf_index;
};

static int
find_cdf_index(const struct anv_video_session *vid,
               const struct av1_refs_info *refs_info,
               const struct anv_image_view *iv,
               uint32_t array_layer)
{
   for (uint32_t i = 0; i < STD_VIDEO_AV1_NUM_REF_FRAMES; i++) {
      if (vid) {
         if (!vid->prev_refs[i].iv)
            continue;

         if (vid->prev_refs[i].iv == iv &&
             vid->prev_refs[i].array_layer == array_layer)
            return vid->prev_refs[i].default_cdf_index;
      } else {
         if (!refs_info[i].iv)
            continue;

         if (refs_info[i].iv == iv &&
             refs_info[i].array_layer == array_layer)
            return refs_info[i].default_cdf_index;
      }
   }

   return 0;
}

static void
anv_av1_decode_video_tile(struct anv_cmd_buffer *cmd_buffer,
                          const VkVideoDecodeInfoKHR *frame_info,
                          const StdVideoAV1SequenceHeader *seq_hdr,
                          int tile_idx)
{
   ANV_FROM_HANDLE(anv_buffer, src_buffer, frame_info->srcBuffer);
   struct anv_video_session *vid = cmd_buffer->video.vid;
   const VkVideoDecodeAV1PictureInfoKHR *av1_pic_info =
      vk_find_struct_const(frame_info->pNext, VIDEO_DECODE_AV1_PICTURE_INFO_KHR);
   const StdVideoDecodeAV1PictureInfo *std_pic_info = av1_pic_info->pStdPictureInfo;

   int cdf_index = 0;
   if (std_pic_info->pQuantization->base_q_idx <= 20)
      cdf_index = 0;
   else if (std_pic_info->pQuantization->base_q_idx <= 60)
      cdf_index = 1;
   else if (std_pic_info->pQuantization->base_q_idx <= 120)
      cdf_index = 2;
   else
      cdf_index = 3;

   anv_batch_emit(&cmd_buffer->batch, GENX(MI_FORCE_WAKEUP), wake) {
      wake.HEVCPowerWellControl = true;
      wake.MaskBits = 768;
   }
   anv_batch_emit(&cmd_buffer->batch, GENX(MI_FORCE_WAKEUP), wake) {
      wake.HEVCPowerWellControl = true;
      wake.MaskBits = 768;
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(MI_FLUSH_DW), flush) {
      flush.DWordLength = 2;
      flush.VideoPipelineCacheInvalidate = 1;
   };

   anv_batch_emit(&cmd_buffer->batch, GENX(AVP_VD_CONTROL_STATE), vd) {
      vd.VDControlState.PipelineInitialization = 1;
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(MFX_WAIT), mfx) {
      mfx.MFXSyncControlFlag = 1;
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(AVP_PIPE_MODE_SELECT), sel) {
      sel.CodecSelect = Decode;
      sel.MultiEngineMode = SingleEngineMode;
   };

   anv_batch_emit(&cmd_buffer->batch, GENX(MFX_WAIT), mfx) {
      mfx.MFXSyncControlFlag = 1;
   }

   struct av1_refs_info ref_info[AV1_TOTAL_REFS_PER_FRAME] = { 0, };

   const struct anv_image_view *dst_iv =
      anv_image_view_from_handle(frame_info->dstPictureResource.imageViewBinding);
   const struct anv_image *dst_img = dst_iv->image;
   const struct anv_image_view *dpb_iv = frame_info->pSetupReferenceSlot ?
      anv_image_view_from_handle(frame_info->pSetupReferenceSlot->pPictureResource->imageViewBinding) :
      dst_iv;
   const uint32_t dpb_array_layer = frame_info->pSetupReferenceSlot ?
      frame_info->pSetupReferenceSlot->pPictureResource->baseArrayLayer :
      frame_info->dstPictureResource.baseArrayLayer;
   const struct anv_image *dpb_img = dpb_iv->image;
   const bool is_10bit = seq_hdr->pColorConfig->BitDepth == 10;
   const bool is_grain = vid->vk.av1.film_grain_support && std_pic_info->flags.apply_grain;

#if GFX_VER < 20
   if (is_grain) {
      anv_finishme("Film grain synthesis on pre-Xe2 hardware via shader kernel");
   }
#endif

   VkExtent2D frameExtent = frame_info->dstPictureResource.codedExtent;
   int denom = std_pic_info->coded_denom + 9;
   unsigned downscaled_width = (frameExtent.width * 8 + denom / 2) / denom;

   ref_info[AV1_INTRA_FRAME].iv = dpb_iv;
   ref_info[AV1_INTRA_FRAME].array_layer = dpb_array_layer;
   ref_info[AV1_INTRA_FRAME].frame_width = frameExtent.width;
   ref_info[AV1_INTRA_FRAME].frame_height = frameExtent.height;

   if (dpb_img && frame_info->referenceSlotCount) {
      ref_info[AV1_INTRA_FRAME].order_hint = std_pic_info->OrderHint;
      ref_info[AV1_INTRA_FRAME].disable_frame_end_update_cdf =
         std_pic_info->flags.disable_frame_end_update_cdf;
   }

   for (int i = 0; i < STD_VIDEO_AV1_REFS_PER_FRAME; ++i) {
      uint8_t ref_idx = av1_pic_info->referenceNameSlotIndices[i];

      for (unsigned j = 0; j < frame_info->referenceSlotCount; j++) {
         int idx = frame_info->pReferenceSlots[j].slotIndex;

         if (ref_idx == idx) {
            const struct anv_image_view *ref_iv =
               anv_image_view_from_handle(frame_info->pReferenceSlots[j].pPictureResource->imageViewBinding);
            const uint32_t ref_array_layer = frame_info->pReferenceSlots[j].pPictureResource->baseArrayLayer;
            const struct VkVideoDecodeAV1DpbSlotInfoKHR *dpb_slot =
               vk_find_struct_const(frame_info->pReferenceSlots[j].pNext, VIDEO_DECODE_AV1_DPB_SLOT_INFO_KHR);
            const struct StdVideoDecodeAV1ReferenceInfo *std_ref_info = dpb_slot->pStdReferenceInfo;

            ref_info[i + 1].idx = idx;
            ref_info[i + 1].frame_type = std_ref_info->frame_type;
            ref_info[i + 1].frame_width = frameExtent.width;
            ref_info[i + 1].frame_height = frameExtent.height;
            ref_info[i + 1].iv = ref_iv;
            ref_info[i + 1].array_layer = ref_array_layer;
            ref_info[i + 1].order_hint = std_ref_info->OrderHint;
            memcpy(ref_info[i + 1].ref_order_hints, std_ref_info->SavedOrderHints, STD_VIDEO_AV1_NUM_REF_FRAMES);
            ref_info[i + 1].disable_frame_end_update_cdf = std_ref_info->flags.disable_frame_end_update_cdf;
            ref_info[i + 1].default_cdf_index = find_cdf_index(vid, NULL, ref_iv, ref_array_layer);
         }
      }
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(AVP_SURFACE_STATE), ss) {
      ss.SurfaceFormat = is_10bit ? AVP_P010 : AVP_PLANAR_420_8;
      ss.SurfacePitchMinus1 = dpb_img->planes[0].primary_surface.isl.row_pitch_B - 1;
      ss.YOffsetforUCb = dpb_img->planes[1].primary_surface.memory_range.offset /
                         dpb_img->planes[0].primary_surface.isl.row_pitch_B;
   };

   if (!frame_is_key_or_intra(std_pic_info->frame_type)) {
      for (enum av1_ref_frame r = AV1_INTRA_FRAME; r <= AV1_ALTREF_FRAME; r++) {
         if (ref_info[r].iv && frame_info->referenceSlotCount) {
            anv_batch_emit(&cmd_buffer->batch, GENX(AVP_SURFACE_STATE), ss) {
               ss.SurfaceID = 0x6 + r;
               ss.SurfaceFormat = is_10bit ? AVP_P010 : AVP_PLANAR_420_8;
               ss.SurfacePitchMinus1 = ref_info[r].iv->image->planes[0].primary_surface.isl.row_pitch_B - 1;
               ss.YOffsetforUCb = ref_info[r].iv->image->planes[1].primary_surface.memory_range.offset /
                                  ref_info[r].iv->image->planes[0].primary_surface.isl.row_pitch_B;
            }
         }
      }
   }

   if (std_pic_info->flags.allow_intrabc) {
      anv_batch_emit(&cmd_buffer->batch, GENX(AVP_SURFACE_STATE), ss) {
         ss.SurfaceID = 0xE;
         ss.SurfaceFormat = is_10bit ? AVP_P010 : AVP_PLANAR_420_8;
         ss.SurfacePitchMinus1 = dpb_img->planes[0].primary_surface.isl.row_pitch_B - 1;
         ss.YOffsetforUCb = dpb_img->planes[1].primary_surface.memory_range.offset /
                            dpb_img->planes[0].primary_surface.isl.row_pitch_B;
      }
   }

   if (is_grain) {
      anv_batch_emit(&cmd_buffer->batch, GENX(AVP_SURFACE_STATE), ss) {
         ss.SurfaceID = 0x2;
         ss.SurfaceFormat = is_10bit ? AVP_P010 : AVP_PLANAR_420_8;
         ss.SurfacePitchMinus1 = dst_img->planes[0].primary_surface.isl.row_pitch_B - 1;
         ss.YOffsetforUCb = dst_img->planes[1].primary_surface.memory_range.offset /
                            dst_img->planes[0].primary_surface.isl.row_pitch_B;
      }
   }

   bool use_internal_cache_mem = true;

#if GFX_VERx10 == 125
   assert(dst_img->planes[0].primary_surface.isl.tiling == ISL_TILING_4);
#endif
   anv_batch_emit(&cmd_buffer->batch, GENX(AVP_PIPE_BUF_ADDR_STATE), buf) {
      buf.DecodedOutputFrameBufferAddress =
         anv_image_dpb_address(dpb_iv, dpb_array_layer);
      buf.DecodedOutputFrameBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.DecodedOutputFrameBufferAddress.bo, 0),
#if GFX_VERx10 >= 125
         .TiledResourceMode = TRMODE_TILEF,
#endif
      };
      buf.CurrentFrameMVWriteBufferAddress =
         anv_image_dmv_top_address(dpb_iv, dpb_array_layer);
      buf.CurrentFrameMVWriteBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.CurrentFrameMVWriteBufferAddress.bo, 0),
      };

      if (std_pic_info->flags.allow_intrabc) {
         buf.IntraBCDecodedOutputFrameBufferAddress =
            anv_image_dpb_address(dpb_iv, dpb_array_layer);
      }

      buf.IntraBCDecodedOutputFrameBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.IntraBCDecodedOutputFrameBufferAddress.bo, 0),
#if GFX_VERx10 >= 125
         .TiledResourceMode = TRMODE_TILEF,
#endif
      };

      if (use_internal_cache_mem) {
         buf.BitstreamLineRowstoreBufferAddress = (struct anv_address) {
            NULL,
            btdl_cache_offset * 64
         };

         buf.BitstreamLineRowstoreBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
            .RowStoreScratchBufferCacheSelect = 1,
         };
      } else {
         buf.BitstreamLineRowstoreBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_BITSTREAM_LINE_ROWSTORE].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_BITSTREAM_LINE_ROWSTORE].offset
         };
         buf.BitstreamLineRowstoreBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_BITSTREAM_LINE_ROWSTORE].mem->bo, 0),
         };
      }

      buf.BitstreamTileLineRowstoreBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_BITSTREAM_TILE_LINE_ROWSTORE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_BITSTREAM_TILE_LINE_ROWSTORE].offset
      };

      buf.BitstreamTileLineRowstoreBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_BITSTREAM_TILE_LINE_ROWSTORE].mem->bo, 0),
      };

      if (use_internal_cache_mem) {
         buf.IntraPredictionLineRowstoreBufferAddress = (struct anv_address) {
            NULL,
            ipdl_cache_offset * 64
         };
         buf.IntraPredictionLineRowstoreBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
            .RowStoreScratchBufferCacheSelect = 1
         };
      } else {
         buf.IntraPredictionLineRowstoreBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_INTRA_PREDICTION_LINE_ROWSTORE].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_INTRA_PREDICTION_LINE_ROWSTORE].offset
         };
         buf.IntraPredictionLineRowstoreBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_INTRA_PREDICTION_LINE_ROWSTORE].mem->bo, 0),
         };
      }
      buf.IntraPredictionTileLineRowstoreBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_INTRA_PREDICTION_TILE_LINE_ROWSTORE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_INTRA_PREDICTION_TILE_LINE_ROWSTORE].offset
      };

      buf.IntraPredictionTileLineRowstoreBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_INTRA_PREDICTION_TILE_LINE_ROWSTORE].mem->bo, 0),
      };

      if (use_internal_cache_mem) {
         buf.SpatialMotionVectorLineBufferAddress = (struct anv_address) {
            NULL,
            smvl_cache_offset * 64
         };
         buf.SpatialMotionVectorLineBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
            .RowStoreScratchBufferCacheSelect = 1
         };
      } else {
         buf.SpatialMotionVectorLineBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_SPATIAL_MOTION_VECTOR_LINE].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_SPATIAL_MOTION_VECTOR_LINE].offset
         };
         buf.SpatialMotionVectorLineBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_SPATIAL_MOTION_VECTOR_LINE].mem->bo, 0),
         };
      }

      buf.SpatialMotionVectorTileLineBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_SPATIAL_MOTION_VECTOR_TILE_LINE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_SPATIAL_MOTION_VECTOR_TILE_LINE].offset
      };

      buf.SpatialMotionVectorTileLineBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_SPATIAL_MOTION_VECTOR_TILE_LINE].mem->bo, 0),
      };

      buf.LoopRestorationMetaTileColumnBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_META_TILE_COLUMN].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_META_TILE_COLUMN].offset
      };
      buf.LoopRestorationMetaTileColumnBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_META_TILE_COLUMN].mem->bo, 0),
      };

      buf.LoopRestorationFilterTileLineYBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_LINE_Y].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_LINE_Y].offset
      };
      buf.LoopRestorationFilterTileLineYBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_LINE_Y].mem->bo, 0),
      };

      buf.LoopRestorationFilterTileLineUBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_LINE_U].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_LINE_U].offset
      };

      buf.LoopRestorationFilterTileLineUBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_LINE_U].mem->bo, 0),
      };

      buf.LoopRestorationFilterTileLineVBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_LINE_V].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_LINE_V].offset
      };

      buf.LoopRestorationFilterTileLineVBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_LINE_V].mem->bo, 0),
      };

      if (use_internal_cache_mem) {
         buf.DeblockerFilterLineYBufferAddress = (struct anv_address) {
            NULL,
            dfly_cache_offset * 64
         };
         buf.DeblockerFilterLineYBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
            .RowStoreScratchBufferCacheSelect = 1,
         };
      } else {
         buf.DeblockerFilterLineYBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_LINE_Y].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_LINE_Y].offset
         };
         buf.DeblockerFilterLineYBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_LINE_Y].mem->bo, 0),
         };
      }

      if (use_internal_cache_mem) {
         buf.DeblockerFilterLineUBufferAddress = (struct anv_address) {
            NULL,
            dflu_cache_offset * 64
         };
         buf.DeblockerFilterLineUBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
            .RowStoreScratchBufferCacheSelect = 1,
         };
      } else {
         buf.DeblockerFilterLineUBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_LINE_U].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_LINE_U].offset
         };
         buf.DeblockerFilterLineUBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_LINE_U].mem->bo, 0),
         };
      }
      if (use_internal_cache_mem) {
         buf.DeblockerFilterLineVBufferAddress = (struct anv_address) {
            NULL,
            dflv_cache_offset * 64
         };
         buf.DeblockerFilterLineVBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
            .RowStoreScratchBufferCacheSelect = 1,
         };
      } else {
         buf.DeblockerFilterLineVBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_LINE_V].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_LINE_V].offset
         };
         buf.DeblockerFilterLineVBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_LINE_V].mem->bo, 0),
         };
      }

      buf.DeblockerFilterTileLineYBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_LINE_Y].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_LINE_Y].offset
      };
      buf.DeblockerFilterTileLineYBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_LINE_Y].mem->bo, 0),
      };

      buf.DeblockerFilterTileLineUBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_LINE_U].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_LINE_U].offset
      };

      buf.DeblockerFilterTileLineUBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_LINE_U].mem->bo, 0),
      };

      buf.DeblockerFilterTileLineVBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_LINE_V].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_LINE_V].offset
      };
      buf.DeblockerFilterTileLineVBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_LINE_V].mem->bo, 0),
      };

      buf.DeblockerFilterTileColumnYBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_COLUMN_Y].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_COLUMN_Y].offset
      };

      buf.DeblockerFilterTileColumnYBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_COLUMN_Y].mem->bo, 0),
      };

      buf.DeblockerFilterTileColumnUBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_COLUMN_U].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_COLUMN_U].offset
      };

      buf.DeblockerFilterTileColumnUBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_COLUMN_U].mem->bo, 0),
      };
      buf.DeblockerFilterTileColumnVBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_COLUMN_V].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_COLUMN_V].offset
      };
      buf.DeblockerFilterTileColumnVBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_COLUMN_V].mem->bo, 0),
      };

      if (use_internal_cache_mem) {
         buf.CDEFFilterLineBufferAddress = (struct anv_address) { NULL, cdef_cache_offset * 64};
         buf.CDEFFilterLineBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
            .RowStoreScratchBufferCacheSelect = 1,
         };
      } else {
         buf.CDEFFilterLineBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_LINE].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_LINE].offset
         };
         buf.CDEFFilterLineBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_LINE].mem->bo, 0),
         };
      }

      buf.CDEFFilterTileLineBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_TILE_LINE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_TILE_LINE].offset
      };
      buf.CDEFFilterTileLineBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_TILE_LINE].mem->bo, 0),
      };

      buf.CDEFFilterTileColumnBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_TILE_COLUMN].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_TILE_COLUMN].offset
      };
      buf.CDEFFilterTileColumnBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_TILE_COLUMN].mem->bo, 0),
      };

      buf.CDEFFilterMetaTileLineBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_META_TILE_LINE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_META_TILE_LINE].offset
      };
      buf.CDEFFilterMetaTileLineBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_META_TILE_LINE].mem->bo, 0),
      };

      buf.CDEFFilterMetaTileColumnBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_META_TILE_COLUMN].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_META_TILE_COLUMN].offset
      };

      buf.CDEFFilterMetaTileColumnBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_META_TILE_COLUMN].mem->bo, 0),
      };

      buf.CDEFFilterTopLeftCornerBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_TOP_LEFT_CORNER].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_TOP_LEFT_CORNER].offset
      };
      buf.CDEFFilterTopLeftCornerBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_TOP_LEFT_CORNER].mem->bo, 0),
      };

      buf.SuperResTileColumnYBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_SUPER_RES_TILE_COLUMN_Y].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_SUPER_RES_TILE_COLUMN_Y].offset
      };
      buf.SuperResTileColumnYBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_SUPER_RES_TILE_COLUMN_Y].mem->bo, 0),
      };

      buf.SuperResTileColumnUBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_SUPER_RES_TILE_COLUMN_U].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_SUPER_RES_TILE_COLUMN_U].offset
      };
      buf.SuperResTileColumnUBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_SUPER_RES_TILE_COLUMN_U].mem->bo, 0),
      };

      buf.SuperResTileColumnVBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_SUPER_RES_TILE_COLUMN_V].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_SUPER_RES_TILE_COLUMN_V].offset
      };
      buf.SuperResTileColumnVBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_SUPER_RES_TILE_COLUMN_V].mem->bo, 0),
      };

      buf.LoopRestorationFilterTileColumnYBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_COLUMN_Y].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_COLUMN_Y].offset
      };

      buf.LoopRestorationFilterTileColumnYBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_COLUMN_Y].mem->bo, 0),
      };

      buf.LoopRestorationFilterTileColumnUBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_COLUMN_U].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_COLUMN_U].offset
      };

      buf.LoopRestorationFilterTileColumnUBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_COLUMN_U].mem->bo, 0),
      };

      buf.LoopRestorationFilterTileColumnVBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_COLUMN_V].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_COLUMN_V].offset
      };
      buf.LoopRestorationFilterTileColumnVBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_COLUMN_V].mem->bo, 0),
      };
#if GFX_VER >= 20
      buf.LoopRestorationFilterTileColumnAlignmentReadWriteBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_COLUMN_ALIGNMENT_RW].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_COLUMN_ALIGNMENT_RW].offset
      };
      buf.LoopRestorationFilterTileColumnAlignmentReadWriteBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_COLUMN_ALIGNMENT_RW].mem->bo, 0),
      };
#endif

      struct anv_bo *ref_bo = NULL;
      struct anv_bo *collocated_bo = NULL;

      if (std_pic_info->frame_type != STD_VIDEO_AV1_FRAME_TYPE_KEY) {
         for (enum av1_ref_frame r = AV1_INTRA_FRAME; r <= AV1_ALTREF_FRAME; r++) {
            const struct anv_image_view *ref_iv = ref_info[r].iv;
            const struct anv_image *ref_img = ref_iv->image;
            if (ref_iv) {

               buf.ReferencePictureAddress[r] =
                  anv_image_dpb_address(ref_iv, ref_info[r].array_layer);
               buf.CollocatedMVTemporalBufferAddress[r] =
                  anv_image_dmv_top_address(ref_iv, ref_info[r].array_layer);

               if (!ref_bo)
                  ref_bo = ref_img->bindings[0].address.bo;
               if (!collocated_bo)
                  collocated_bo = ref_img->bindings[ref_img->vid_dmv_top_surface.binding].address.bo;

            }
         }
      }

      buf.ReferencePictureAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, ref_bo, 0),
#if GFX_VERx10 >= 125
         .TiledResourceMode = TRMODE_TILEF,
#endif
      };
      buf.CollocatedMVTemporalBufferAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, collocated_bo, 0),
      };

      bool use_default_cdf = false;

      if (std_pic_info->primary_ref_frame == 7) {
          use_default_cdf = true;
      } else {
         if (ref_info[std_pic_info->primary_ref_frame + 1].disable_frame_end_update_cdf) {
            use_default_cdf = true;

            const struct anv_image_view *ref_iv = ref_info[std_pic_info->primary_ref_frame + 1].iv;
            const uint32_t ref_layer = ref_info[std_pic_info->primary_ref_frame + 1].array_layer;
            cdf_index = find_cdf_index(vid, NULL, ref_iv, ref_layer);
         }
      }

      if (use_default_cdf) {
         buf.CDFTablesInitializationBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_CDF_DEFAULTS_0 + cdf_index].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_CDF_DEFAULTS_0 + cdf_index].offset };

         ref_info[0].default_cdf_index = cdf_index;
      } else {
         const struct anv_image_view *ref_iv = ref_info[std_pic_info->primary_ref_frame + 1].iv;
         const uint32_t ref_layer = ref_info[std_pic_info->primary_ref_frame + 1].array_layer;
         buf.CDFTablesInitializationBufferAddress = anv_image_av1_table_address(ref_iv, ref_layer);
      }
      buf.CDFTablesInitializationBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.CDFTablesInitializationBufferAddress.bo, 0),
      };

      if (!std_pic_info->flags.disable_frame_end_update_cdf) {
         buf.CDFTablesBackwardAdaptationBufferAddress =
            anv_image_av1_table_address(ref_info[0].iv, ref_info[0].array_layer);
      }

      buf.CDFTablesBackwardAdaptationBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.CDFTablesBackwardAdaptationBufferAddress.bo, 0),
      };
      buf.AV1SegmentIDReadBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.AV1SegmentIDWriteBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.DecodedFrameStatusErrorBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };

      buf.DecodedBlockDataStreamoutBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_DBD_BUFFER].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_DBD_BUFFER].offset
      };
      buf.DecodedBlockDataStreamoutBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[ANV_VID_MEM_AV1_DBD_BUFFER].mem->bo, 0),
      };
#if GFX_VERx10 >= 125
      buf.OriginalUncompressedPictureSourceBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.DownscaledUncompressedPictureSourceBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.TileSizeStreamoutBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.TileStatisticsStreamoutBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.CUStreamoutBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.SSELineReadWriteBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.SSETileLineReadWriteBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.PostCDEFPixelsBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
#endif
#if GFX_VER >= 20
      if (is_grain) {
         buf.FilmGrainInjectedOutputFrameBufferAddress =
            anv_image_dpb_address(dst_iv, frame_info->dstPictureResource.baseArrayLayer);
         buf.FilmGrainSampleTemplateAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_FILM_GRAIN_SAMPLE_TEMPLATE].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_FILM_GRAIN_SAMPLE_TEMPLATE].offset
         };
         buf.FilmGrainTileColumnDataReadWriteBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_FILM_GRAIN_TILE_COLUMN_RW].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_FILM_GRAIN_TILE_COLUMN_RW].offset
         };
      }
      buf.FilmGrainInjectedOutputFrameBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.FilmGrainInjectedOutputFrameBufferAddress.bo, 0),
         .TiledResourceMode = TRMODE_TILEF,
      };
      buf.FilmGrainSampleTemplateAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.FilmGrainSampleTemplateAddress.bo, 0),
      };
      buf.FilmGrainTileColumnDataReadWriteBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.FilmGrainTileColumnDataReadWriteBufferAddress.bo, 0),
      };
#endif
   };

   anv_batch_emit(&cmd_buffer->batch, GENX(AVP_IND_OBJ_BASE_ADDR_STATE), ind) {
      ind.AVPIndirectBitstreamObjectBaseAddress = anv_address_add(src_buffer->address,
                                                                  frame_info->srcBufferOffset & ~4095);
      ind.AVPIndirectBitstreamObjectAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, src_buffer->address.bo, 0),
      };
#if GFX_VERx10 >= 125
      ind.AVPIndirectCUObjectMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
#endif
   }

   uint32_t frame_restoration_type[3];
   for (unsigned i = 0; i < 3; i++) {
      frame_restoration_type[i] = std_pic_info->pLoopRestoration ?
         std_pic_info->pLoopRestoration->FrameRestorationType[i] : 0;
   }

   uint32_t ref_mask = 0;
   uint32_t ref_frame_sign_bias = 0;
   uint32_t ref_frame_side = 0;
   for (enum av1_ref_frame r = AV1_LAST_FRAME; r <= AV1_ALTREF_FRAME; r++) {
      if (seq_hdr->flags.enable_order_hint &&
          !frame_is_key_or_intra(std_pic_info->frame_type)) {
         if (get_relative_dist(av1_pic_info, seq_hdr,
                               ref_info[r].order_hint, ref_info[AV1_INTRA_FRAME].order_hint) > 0)
            ref_frame_sign_bias |= (1 << r);

         if ((get_relative_dist(av1_pic_info, seq_hdr,
                                ref_info[r].order_hint, ref_info[AV1_INTRA_FRAME].order_hint) > 0) ||
             ref_info[r].order_hint == ref_info[AV1_INTRA_FRAME].order_hint)
            ref_frame_side |= (1 << r);
      }
   }

   uint8_t num_mfmv = 0;
   uint8_t mfmv_ref[7] = { 0, };
   if (!frame_is_key_or_intra(std_pic_info->frame_type) &&
         std_pic_info->flags.use_ref_frame_mvs &&
         seq_hdr->order_hint_bits_minus_1 + 1) {

      assert (seq_hdr->flags.enable_order_hint);

      int total = av1_mfmv_stack_size - 1;

      if (ref_info[AV1_LAST_FRAME].ref_order_hints[AV1_ALTREF_FRAME - AV1_LAST_FRAME + 1] !=
          ref_info[AV1_GOLDEN_FRAME].order_hint) {

         if (!frame_is_key_or_intra(ref_info[0 + 1].frame_type)) {
            total = av1_mfmv_stack_size;
            mfmv_ref[num_mfmv++] = AV1_LAST_FRAME - AV1_LAST_FRAME;
         }
      }

      if (get_relative_dist(av1_pic_info, seq_hdr,
                            ref_info[AV1_BWDREF_FRAME].order_hint,
                            ref_info[AV1_INTRA_FRAME].order_hint) > 0 &&
          !frame_is_key_or_intra(ref_info[AV1_BWDREF_FRAME - AV1_LAST_FRAME + 1].frame_type)) {
         mfmv_ref[num_mfmv++] = AV1_BWDREF_FRAME - AV1_LAST_FRAME;
      }

      if (get_relative_dist(av1_pic_info, seq_hdr,
                            ref_info[AV1_ALTREF2_FRAME].order_hint,
                            ref_info[AV1_INTRA_FRAME].order_hint) > 0 &&
          !frame_is_key_or_intra(ref_info[AV1_ALTREF2_FRAME - AV1_LAST_FRAME + 1].frame_type)) {
         mfmv_ref[num_mfmv++] = AV1_ALTREF2_FRAME - AV1_LAST_FRAME;
      }

      if (num_mfmv < total &&
          get_relative_dist(av1_pic_info, seq_hdr,
                            ref_info[AV1_ALTREF_FRAME].order_hint,
                            ref_info[AV1_INTRA_FRAME].order_hint) > 0 &&
          !frame_is_key_or_intra(ref_info[AV1_ALTREF_FRAME - AV1_LAST_FRAME + 1].frame_type)) {
         mfmv_ref[num_mfmv++] = AV1_ALTREF_FRAME - AV1_LAST_FRAME;
      }

      if (num_mfmv < total &&
          !frame_is_key_or_intra(ref_info[AV1_LAST2_FRAME - AV1_LAST_FRAME + 1].frame_type)) {
         mfmv_ref[num_mfmv++] = AV1_LAST2_FRAME - AV1_LAST_FRAME;
      }
   }

   assert(num_mfmv <= 7);

   for (unsigned int i = 0; i < num_mfmv; i++) {
      ref_mask |= (1 << mfmv_ref[i]);
   }

   uint8_t preskip_segid = 0;
   uint8_t last_active_segid = 0;
   bool frame_lossless = true;
   bool lossless[8] = { false };

   for (unsigned i = 0; i < 8; i++) {
      for (unsigned j = 0; j < 8; j++) {
         if (std_pic_info->pSegmentation->FeatureEnabled[i] & (1 << j)) {
            last_active_segid = i;
            if (j >= 5)
               preskip_segid = 1;
         }
      }
      uint32_t qindex = get_qindex(av1_pic_info, i);
      lossless[i] = (qindex == 0) &&
         (std_pic_info->pQuantization->DeltaQYDc == 0) &&
         (std_pic_info->pQuantization->DeltaQUAc == 0) &&
         (std_pic_info->pQuantization->DeltaQUDc == 0) &&
         (std_pic_info->pQuantization->DeltaQVAc == 0) &&
         (std_pic_info->pQuantization->DeltaQVDc == 0);
      frame_lossless &= lossless[i];
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(AVP_PIC_STATE), pic) {
      if (std_pic_info->flags.use_superres) {
         pic.FrameWidth = downscaled_width - 1;
      } else {
         pic.FrameWidth = frameExtent.width - 1;
      }
      pic.FrameHeight = frameExtent.height - 1;

      if (seq_hdr->pColorConfig->BitDepth == 12)
         pic.SequencePixelBitDepthIdc = SeqPix_12bit;
      else if (seq_hdr->pColorConfig->BitDepth == 10)
         pic.SequencePixelBitDepthIdc = SeqPix_10bit;
      else
         pic.SequencePixelBitDepthIdc = SeqPix_8bit;
      if (seq_hdr->pColorConfig->subsampling_x == 1 &&
          seq_hdr->pColorConfig->subsampling_y == 1) {
         if (seq_hdr->pColorConfig->flags.mono_chrome)
            pic.SequenceChromaSubSamplingFormat = SS_Monochrome;
         else
            pic.SequenceChromaSubSamplingFormat = SS_420;
      } else if (seq_hdr->pColorConfig->subsampling_x == 1 &&
                 seq_hdr->pColorConfig->subsampling_y == 0) {
         pic.SequenceChromaSubSamplingFormat = SS_422;
      } else if (seq_hdr->pColorConfig->subsampling_x == 0 &&
                 seq_hdr->pColorConfig->subsampling_y == 0) {
         pic.SequenceChromaSubSamplingFormat = SS_444;
      }

      pic.SequenceSuperblockSizeUsed = seq_hdr->flags.use_128x128_superblock;
      pic.SequenceEnableOrderHintFlag = seq_hdr->flags.enable_order_hint;
      pic.SequenceOrderHintBitsMinus1 = seq_hdr->flags.enable_order_hint ? seq_hdr->order_hint_bits_minus_1 : 0;
      pic.SequenceEnableFilterIntraFlag = seq_hdr->flags.enable_filter_intra;
      pic.SequenceEnableIntraEdgeFilterFlag = seq_hdr->flags.enable_intra_edge_filter;
      pic.SequenceEnableDualFilterFlag = seq_hdr->flags.enable_dual_filter;
      pic.SequenceEnableInterIntraCompoundFlag = seq_hdr->flags.enable_interintra_compound;
      pic.SequenceEnableMaskedCompoundFlag = seq_hdr->flags.enable_masked_compound;
      pic.SequenceEnableJointCompoundFlag = seq_hdr->flags.enable_jnt_comp;
      pic.AllowScreenContentToolsFlag = std_pic_info->flags.allow_screen_content_tools;
      pic.ForceIntegerMVFlag = std_pic_info->flags.force_integer_mv;
      pic.AllowWarpedMotionFlag = std_pic_info->flags.allow_warped_motion;
      pic.UseCDEFFilterFlag = !frame_lossless && seq_hdr->flags.enable_cdef && !std_pic_info->flags.allow_intrabc;
      pic.UseSuperResFlag = std_pic_info->flags.use_superres;
      pic.FrameLevelLoopRestorationFilterEnable = frame_restoration_type[0] || frame_restoration_type[1] || frame_restoration_type[2];
      pic.FrameType = std_pic_info->frame_type;
      pic.IntraOnlyFlag = frame_is_key_or_intra(std_pic_info->frame_type);
      pic.ErrorResilientModeFlag = std_pic_info->flags.error_resilient_mode;
      pic.AllowIntraBCFlag = std_pic_info->flags.allow_intrabc;
      pic.PrimaryReferenceFrameIdx = std_pic_info->primary_ref_frame;
      pic.SegmentationEnableFlag = std_pic_info->flags.segmentation_enabled;
      pic.SegmentationUpdateMapFlag = std_pic_info->flags.segmentation_update_map;
      pic.SegmentationTemporalUpdateFlag = pic.IntraOnlyFlag ? 0 : std_pic_info->flags.segmentation_temporal_update;
      pic.PreSkipSegmentIDFlag = preskip_segid;
      pic.LastActiveSegmentSegmentID = last_active_segid;
      pic.DeltaQPresentFlag = std_pic_info->flags.delta_q_present;
      pic.DeltaQRes = std_pic_info->delta_q_res;
      pic.FrameCodedLosslessMode = frame_lossless;
      pic.SegmentMapisZeroFlag = 0; /* TODO */
      pic.SegmentIDBufferStreamInEnableFlag = 0; /* TODO */
      pic.SegmentIDBufferStreamOutEnableFlag = 0; /* TODO */
      pic.BaseQindex = std_pic_info->pQuantization->base_q_idx;
      pic.YdcdeltaQ = std_pic_info->pQuantization->DeltaQYDc;
      pic.UdcdeltaQ = std_pic_info->pQuantization->DeltaQUDc;
      pic.UacdeltaQ = std_pic_info->pQuantization->DeltaQUAc;
      pic.VdcdeltaQ = std_pic_info->pQuantization->DeltaQVDc;
      pic.VacdeltaQ = std_pic_info->pQuantization->DeltaQVAc;
      pic.AllowHighPrecisionMV = std_pic_info->flags.allow_high_precision_mv;
      pic.FrameLevelReferenceModeSelect = !(std_pic_info->flags.reference_select == 0);
      pic.McompFilterType = std_pic_info->interpolation_filter;
      pic.MotionModeSwitchableFlag = std_pic_info->flags.is_motion_mode_switchable;
      pic.UseReferenceFrameMVSetFlag = std_pic_info->flags.use_ref_frame_mvs;
      pic.ReferenceFrameSignBias = ref_frame_sign_bias;
      pic.CurrentFrameOrderHint = std_pic_info->OrderHint;
      pic.ReducedTxSetUsed = std_pic_info->flags.reduced_tx_set;
      pic.FrameTransformMode = std_pic_info->TxMode;
      pic.SkipModePresentFlag = std_pic_info->flags.skip_mode_present;
      pic.SkipModeFrame0 = std_pic_info->SkipModeFrame[0];
      pic.SkipModeFrame1 = std_pic_info->SkipModeFrame[1];
      pic.ReferenceFrameSide = ref_frame_side;
      pic.GlobalMotionType1 = std_pic_info->pGlobalMotion->GmType[1];
      pic.GlobalMotionType2 = std_pic_info->pGlobalMotion->GmType[2];
      pic.GlobalMotionType3 = std_pic_info->pGlobalMotion->GmType[3];
      pic.GlobalMotionType4 = std_pic_info->pGlobalMotion->GmType[4];
      pic.GlobalMotionType5 = std_pic_info->pGlobalMotion->GmType[5];
      pic.GlobalMotionType6 = std_pic_info->pGlobalMotion->GmType[6];
      pic.GlobalMotionType7 = std_pic_info->pGlobalMotion->GmType[7];
      pic.FrameLevelGlobalMotionInvalidFlags = 0;
#if GFX_VER >= 20
      pic.ApplyFilmGrainFlag = is_grain;
#endif

      uint8_t idx = 0;
      int warp_params[8][6] = { 0, };

      for (enum av1_ref_frame r = AV1_LAST_FRAME; r <= AV1_ALTREF_FRAME; r++) {
         unsigned gm_type = std_pic_info->pGlobalMotion->GmType[r];

         for (uint32_t i = 0; i < STD_VIDEO_AV1_GLOBAL_MOTION_PARAMS; i++) {
            warp_params[r][i] = std_pic_info->pGlobalMotion->gm_params[r][i];
         }

         if (gm_type >= AV1_ROTZOOM) {
            warp_params[r][2] -= (1 << av1_warped_model_prec_bits);
            warp_params[r][2] >>= av1_gm_alpha_prec_diff;
            warp_params[r][3] >>= av1_gm_alpha_prec_diff;
         }

         if (gm_type == AV1_AFFINE) {
            warp_params[r][4] >>= av1_gm_alpha_prec_diff;
            warp_params[r][5] -= (1 << av1_warped_model_prec_bits);
            warp_params[r][5] >>= av1_gm_alpha_prec_diff;
         } else {
            warp_params[r][4] = -warp_params[r][3];
            warp_params[r][5] = warp_params[r][2];
         }

         if (gm_type >= AV1_TRANSLATION) {
            int trans_shift =
               (gm_type == AV1_TRANSLATION) ?
                  av1_gm_trans_only_prec_diff + (std_pic_info->flags.allow_high_precision_mv ? 0 : 1) :
                  av1_gm_trans_prec_diff;

            warp_params[r][0] >>= trans_shift;
            warp_params[r][1] >>= trans_shift;
         }

      }

      for (enum av1_ref_frame r = AV1_LAST_FRAME; r <= AV1_ALTREF_FRAME; r++) {
         for (uint32_t i = 0; i < STD_VIDEO_AV1_GLOBAL_MOTION_PARAMS; i++)
            pic.WarpParameters[idx++] =  warp_params[r][i] & 0xffff;
      }

      pic.ReferenceFrameIdx1 = AV1_LAST_FRAME;
      pic.ReferenceFrameIdx2 = AV1_LAST2_FRAME;
      pic.ReferenceFrameIdx3 = AV1_LAST3_FRAME;
      pic.ReferenceFrameIdx4 = AV1_GOLDEN_FRAME;
      pic.ReferenceFrameIdx5 = AV1_BWDREF_FRAME;
      pic.ReferenceFrameIdx6 = AV1_ALTREF2_FRAME;
      pic.ReferenceFrameIdx7 = AV1_ALTREF_FRAME;

      if (!frame_is_key_or_intra(std_pic_info->frame_type)) {
         for (enum av1_ref_frame r = AV1_INTRA_FRAME; r <= AV1_ALTREF_FRAME; r++) {

            int ref_width = ref_info[r].frame_width - 1;
            int ref_height = ref_info[r].frame_height - 1;

            int cur_frame_width = std_pic_info->flags.use_superres ? downscaled_width : frameExtent.width;
            int cur_frame_height = frameExtent.height;

            uint32_t h_scale_factor =
               ((ref_width + 1) * av1_scaling_factor + (cur_frame_width >> 1)) / cur_frame_width;
            uint32_t v_scale_factor =
               ((ref_height + 1) * av1_scaling_factor + (cur_frame_height >> 1)) / cur_frame_height;

            switch (r) {
            case AV1_INTRA_FRAME:
               pic.IntraFrameWidthinPixelMinus1 = cur_frame_width - 1;
               pic.IntraFrameHeightinPixelMinus1 = cur_frame_height - 1;
               pic.VerticalScaleFactorForIntra = av1_scaling_factor;
               pic.HorizontalScaleFactorForIntra = av1_scaling_factor;
               break;
            case AV1_LAST_FRAME:
               pic.LastFrameWidthinPixelMinus1 = ref_width;
               pic.LastFrameHeightinPixelMinus1 = ref_height;
               pic.VerticalScaleFactorForLast = v_scale_factor;
               pic.HorizontalScaleFactorForLast = h_scale_factor;
               break;
            case AV1_LAST2_FRAME:
               pic.Last2FrameWidthinPixelMinus1 = ref_width;
               pic.Last2FrameHeightinPixelMinus1 = ref_height;
               pic.VerticalScaleFactorForLast2 = v_scale_factor;
               pic.HorizontalScaleFactorForLast2 = h_scale_factor;
               break;
            case AV1_LAST3_FRAME:
               pic.Last3FrameWidthinPixelMinus1 = ref_width;
               pic.Last3FrameHeightinPixelMinus1 = ref_height;
               pic.VerticalScaleFactorForLast3 = v_scale_factor;
               pic.HorizontalScaleFactorForLast3 = h_scale_factor;
               break;
            case AV1_GOLDEN_FRAME:
               pic.GoldenFrameWidthinPixelMinus1 = ref_width;
               pic.GoldenFrameHeightinPixelMinus1 = ref_height;
               pic.VerticalScaleFactorForGolden = v_scale_factor;
               pic.HorizontalScaleFactorForGolden = h_scale_factor;
               break;
            case AV1_BWDREF_FRAME:
               pic.BWDREFFrameWidthinPixelMinus1 = ref_width;
               pic.BWDREFFrameHeightinPixelMinus1 = ref_height;
               pic.VerticalScaleFactorForBWDREF = v_scale_factor;
               pic.HorizontalScaleFactorForBWDREF = h_scale_factor;
               break;
            case AV1_ALTREF2_FRAME:
               pic.ALTREF2FrameWidthinPixelMinus1 = ref_width;
               pic.ALTREF2FrameHeightinPixelMinus1 = ref_height;
               pic.VerticalScaleFactorForALTREF2 = v_scale_factor;
               pic.HorizontalScaleFactorForALTREF2 = h_scale_factor;
               break;
            case AV1_ALTREF_FRAME:
               pic.ALTREFFrameWidthinPixelMinus1 = ref_width;
               pic.ALTREFFrameHeightinPixelMinus1 = ref_height;
               pic.VerticalScaleFactorForALTREF = v_scale_factor;
               pic.HorizontalScaleFactorForALTREF = h_scale_factor;
               break;
            default:
               break;
            }
         }
      }

      pic.FrameLevelGlobalMotionInvalidFlags = 0;
      for (enum av1_ref_frame r = AV1_INTRA_FRAME; r <= AV1_ALTREF_FRAME; r++)
         pic.ReferenceFrameOrderHint[r] = ref_info[r].order_hint;
   };

   anv_batch_emit(&cmd_buffer->batch, GENX(AVP_INTER_PRED_STATE), inter) {
      inter.ActiveReferenceBitmask = ref_mask;

      for (enum av1_ref_frame r = AV1_LAST_FRAME; r <= AV1_ALTREF_FRAME; r++) {
         switch (r) {
         case AV1_LAST_FRAME:
            for (unsigned j = 0; j < 7; j++)
               inter.SavedOrderHints0[j] = ref_info[r].ref_order_hints[j + 1];
            break;
         case AV1_LAST2_FRAME:
            for (unsigned j = 0; j < 7; j++)
               inter.SavedOrderHints1[j] = ref_info[r].ref_order_hints[j + 1];
            break;
         case AV1_LAST3_FRAME:
            for (unsigned j = 0; j < 7; j++)
               inter.SavedOrderHints2[j] = ref_info[r].ref_order_hints[j + 1];
            break;
         case AV1_GOLDEN_FRAME:
            for (unsigned j = 0; j < 7; j++)
               inter.SavedOrderHints3[j] = ref_info[r].ref_order_hints[j + 1];
            break;
         case AV1_BWDREF_FRAME:
            for (unsigned j = 0; j < 7; j++)
               inter.SavedOrderHints4[j] = ref_info[r].ref_order_hints[j + 1];
            break;
         case AV1_ALTREF2_FRAME:
            for (unsigned j = 0; j < 7; j++)
               inter.SavedOrderHints5[j] = ref_info[r].ref_order_hints[j + 1];
            break;
         case AV1_ALTREF_FRAME:
            for (unsigned j = 0; j < 7; j++)
               inter.SavedOrderHints6[j] = ref_info[r].ref_order_hints[j + 1];
            break;
         default:
            break;
         }
      }
   }

   for (unsigned i = 0; i < 8; ++i) {
      anv_batch_emit(&cmd_buffer->batch, GENX(AVP_SEGMENT_STATE), seg) {
         seg.SegmentID = i;
         seg.SegmentFeatureMask = std_pic_info->pSegmentation->FeatureEnabled[i];
         seg.SegmentDeltaQindex = std_pic_info->pSegmentation->FeatureData[i][SEG_LVL_ALT_Q];
         seg.SegmentBlockSkipFlag = std_pic_info->pSegmentation->FeatureData[i][SEG_LVL_SKIP];
         seg.SegmentBlockGlobalMVFlag = std_pic_info->pSegmentation->FeatureData[i][SEG_LVL_GLOBAL_MV];
         seg.SegmentLosslessFlag = lossless[i];
         if (lossless[i] || !std_pic_info->pQuantization->flags.using_qmatrix) {
            seg.SegmentLumaYQMLevel = av1_num_qm_levels - 1;
            seg.SegmentChromaUQMLevel = av1_num_qm_levels - 1;
            seg.SegmentChromaVQMLevel = av1_num_qm_levels - 1;
         } else {
            seg.SegmentLumaYQMLevel = std_pic_info->pQuantization->qm_y;
            seg.SegmentChromaUQMLevel = std_pic_info->pQuantization->qm_u;
            seg.SegmentChromaVQMLevel = std_pic_info->pQuantization->qm_v;
         }
         /* TODO. handling negative values?
         seg.SegmentDeltaLoopFilterLevelLumaVertical = std_pic_info->pSegmentation->FeatureData[i][SEG_LVL_ALT_LFYV];
         seg.SegmentDeltaLoopFilterLevelLumaHorizontal = std_pic_info->pSegmentation->FeatureData[i][SEG_LVL_ALT_LFYH];
         seg.SegmentDeltaLoopFilterLevelChromaU = std_pic_info->pSegmentation->FeatureData[i][SEG_LVL_ALT_LFU];
         seg.SegmentDeltaLoopFilterLevelChromaV = std_pic_info->pSegmentation->FeatureData[i][SEG_LVL_ALT_LFV];
         seg.SegmentReferenceFrame = std_pic_info->pSegmentation->FeatureData[i][SEG_LVL_REF_FRAME];
         */
      };

      if (!std_pic_info->flags.segmentation_enabled)
          break;
   }

   const StdVideoAV1LoopFilter *lf = std_pic_info->pLoopFilter;
   const StdVideoAV1CDEF *cdef = std_pic_info->pCDEF;
   uint32_t cdef_strengths[8] = { 0 }, cdef_uv_strengths[8] = { 0 };
   for (unsigned i = 0; i < (1 << cdef->cdef_bits); ++i) {
      cdef_strengths[i] = (cdef->cdef_y_pri_strength[i] << 2) +
         cdef->cdef_y_sec_strength[i];
      cdef_uv_strengths[i] = (cdef->cdef_uv_pri_strength[i] << 2) +
         cdef->cdef_uv_sec_strength[i];
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(AVP_INLOOP_FILTER_STATE), fil) {
      fil.LumaYDeblockerFilterLevelVertical = lf->loop_filter_level[0];
      fil.LumaYDeblockerFilterLevelHorizontal = lf->loop_filter_level[1];
      fil.ChromaUDeblockerFilterLevel = lf->loop_filter_level[2];
      fil.ChromaVDeblockerFilterLevel = lf->loop_filter_level[3];
      fil.DeblockerFilterSharpnessLevel = lf->loop_filter_sharpness;
      fil.DeblockerFilterModeRefDeltaEnableFlag = lf->flags.loop_filter_delta_enabled;
      fil.DeblockerDeltaLFResolution = std_pic_info->delta_lf_res;
      fil.DeblockerFilterDeltaLFMultiFlag = std_pic_info->flags.delta_lf_multi;
      fil.DeblockerFilterDeltaLFPresentFlag = std_pic_info->flags.delta_lf_present;
      fil.DeblockerFilterRefDeltas0 = lf->loop_filter_ref_deltas[0];
      fil.DeblockerFilterRefDeltas1 = lf->loop_filter_ref_deltas[1];
      fil.DeblockerFilterRefDeltas2 = lf->loop_filter_ref_deltas[2];
      fil.DeblockerFilterRefDeltas3 = lf->loop_filter_ref_deltas[3];
      fil.DeblockerFilterRefDeltas4 = lf->loop_filter_ref_deltas[4];
      fil.DeblockerFilterRefDeltas5 = lf->loop_filter_ref_deltas[5];
      fil.DeblockerFilterRefDeltas6 = lf->loop_filter_ref_deltas[6];
      fil.DeblockerFilterRefDeltas7 = lf->loop_filter_ref_deltas[7];
      fil.DeblockerFilterModeDeltas0 = lf->loop_filter_mode_deltas[0];
      fil.DeblockerFilterModeDeltas1 = lf->loop_filter_mode_deltas[1];
      fil.CDEFYStrength0 = cdef_strengths[0];
      fil.CDEFYStrength1 = cdef_strengths[1];
      fil.CDEFYStrength2 = cdef_strengths[2];
      fil.CDEFYStrength3 = cdef_strengths[3];
      fil.CDEFBits = cdef->cdef_bits;
      fil.CDEFFilterDmpaingFactorMinus3 = cdef->cdef_damping_minus_3;
      fil.CDEFYStrength4 = cdef_strengths[4];
      fil.CDEFYStrength5 = cdef_strengths[5];
      fil.CDEFYStrength6 = cdef_strengths[6];
      fil.CDEFYStrength7 = cdef_strengths[7];
      fil.CDEFUVStrength0 = cdef_uv_strengths[0];
      fil.CDEFUVStrength1 = cdef_uv_strengths[1];
      fil.CDEFUVStrength2 = cdef_uv_strengths[2];
      fil.CDEFUVStrength3 = cdef_uv_strengths[3];
      fil.CDEFUVStrength4 = cdef_uv_strengths[4];
      fil.CDEFUVStrength5 = cdef_uv_strengths[5];
      fil.CDEFUVStrength6 = cdef_uv_strengths[6];
      fil.CDEFUVStrength7 = cdef_uv_strengths[7];
      fil.SuperResUpscaledFrameWidthMinus1 = frameExtent.width - 1;
      fil.SuperResDenom = std_pic_info->flags.use_superres ? denom : 8;
      fil.FrameLoopRestorationFilterLumaY = frame_restoration_type[0];
      fil.FrameLoopRestorationFilterChromaU = frame_restoration_type[1];
      fil.FrameLoopRestorationFilterChromaV = frame_restoration_type[2];

      bool loop_restoration_filter_enable =
         frame_restoration_type[0] || frame_restoration_type[1] || frame_restoration_type[2];

      fil.LoopRestorationUnitSizeLumaY =loop_restoration_filter_enable ?
         std_pic_info->pLoopRestoration->LoopRestorationSize[0] : 0;
      fil.UseSameLoopRestorationUnitSizeChromasUVFlag = (frame_restoration_type[1] != 0 || frame_restoration_type[2] !=0) ?
         std_pic_info->pLoopRestoration->LoopRestorationSize[0] == std_pic_info->pLoopRestoration->LoopRestorationSize[1] : false;

      fil.LumaPlanex_step_qn = luma_xstep_qn;
      fil.LumaPlanex0_qn = luma_x0_qn[tile_idx];
      fil.ChromaPlanex_step_qn = chroma_xstep_qn;
      fil.ChromaPlanex0_qn = chroma_x0_qn[tile_idx];

   };

#if GFX_VER >= 20
   if (is_grain) {
      const StdVideoAV1FilmGrain* filmGrainParams = std_pic_info->pFilmGrain;
      anv_batch_emit(&cmd_buffer->batch, GENX(AVP_FILM_GRAIN_STATE), fg) {
         fg.GrainRandomSeed = filmGrainParams->grain_seed;
         fg.ClipToRestrictedRangeFlag = filmGrainParams->flags.clip_to_restricted_range;
         fg.GrainNoiseOverlapFlag = filmGrainParams->flags.overlap_flag;
         fg.ChromaScalingFromLumaFlag = filmGrainParams->flags.chroma_scaling_from_luma;
         fg.MCIdentityFlag = seq_hdr->pColorConfig->matrix_coefficients == STD_VIDEO_AV1_MATRIX_COEFFICIENTS_IDENTITY;
         fg.NumberofLumaPoints = filmGrainParams->num_y_points;
         fg.NumberofChromaCbPoints = filmGrainParams->num_cb_points;
         fg.NumberofChromaCrPoints = filmGrainParams->num_cr_points;
         fg.GrainScalingMinus8 = filmGrainParams->grain_scaling_minus_8;
         fg.ARCoeffLag = filmGrainParams->ar_coeff_lag;
         fg.ARCoeffShiftMinus6 = filmGrainParams->ar_coeff_shift_minus_6;
         fg.GrainScaleShift = filmGrainParams->grain_scale_shift;
         for (unsigned i = 0; i < STD_VIDEO_AV1_MAX_NUM_Y_POINTS; ++i) {
            fg.PointLumaValue[i] = filmGrainParams->point_y_value[i];
            fg.PointLumaScaling[i] = filmGrainParams->point_y_scaling[i];
         }
         for (unsigned i = 0; i < STD_VIDEO_AV1_MAX_NUM_CB_POINTS; ++i) {
            fg.PointCBValue[i] = filmGrainParams->point_cb_value[i];
            fg.PointCBScaling[i] = filmGrainParams->point_cb_scaling[i];
         }
         for (unsigned i = 0; i < STD_VIDEO_AV1_MAX_NUM_CR_POINTS; ++i) {
            fg.PointCRValue[i] = filmGrainParams->point_cr_value[i];
            fg.PointCRScaling[i] = filmGrainParams->point_cr_scaling[i];
         }
         for (unsigned i = 0; i < STD_VIDEO_AV1_MAX_NUM_POS_LUMA; ++i) {
            fg.ARCoeffLumaPlus128[i] = (uint8_t) filmGrainParams->ar_coeffs_y_plus_128[i];
         }
         for (unsigned i = 0; i < STD_VIDEO_AV1_MAX_NUM_POS_CHROMA; ++i) {
            fg.ARCoeffChromaCBPlus128[i] = (uint8_t) filmGrainParams->ar_coeffs_cb_plus_128[i];
            fg.ARCoeffChromaCRPlus128[i] = (uint8_t) filmGrainParams->ar_coeffs_cr_plus_128[i];
         }
         fg.CBMult = filmGrainParams->cb_mult;
         fg.CBLumaMult = filmGrainParams->cb_luma_mult;
         fg.CBOffset = filmGrainParams->cb_offset;
         fg.CRMult = filmGrainParams->cr_mult;
         fg.CRLumaMult = filmGrainParams->cr_luma_mult;
         fg.CROffset = filmGrainParams->cr_offset;
      };
   }
#endif

   unsigned column = tile_idx % std_pic_info->pTileInfo->TileCols;
   unsigned row = tile_idx / std_pic_info->pTileInfo->TileCols;
   bool last_tile = (column == std_pic_info->pTileInfo->TileCols - 1) &&
                    (row == std_pic_info->pTileInfo->TileRows - 1);

   anv_batch_emit(&cmd_buffer->batch, GENX(AVP_TILE_CODING), til) {
      til.FrameTileID = tile_idx;
      til.TGTileNum = tile_idx;
      til.TileGroupID = 0;
      til.TileColumnPositioninSBUnit = std_pic_info->pTileInfo->pMiColStarts[column];
      til.TileRowPositioninSBUnit = std_pic_info->pTileInfo->pMiRowStarts[row];
      til.TileWidthinSBMinus1 = std_pic_info->pTileInfo->pWidthInSbsMinus1[column];
      til.TileHeightinSBMinus1 = std_pic_info->pTileInfo->pHeightInSbsMinus1[row];
      til.IsLastTileofRowFlag = column == std_pic_info->pTileInfo->TileCols - 1;
      til.IsLastTileofColumnFlag = row == std_pic_info->pTileInfo->TileRows - 1;
      til.IsStartTileofTileGroupFlag = tile_idx == 0;
      til.IsEndTileofTileGroupFlag = (column == std_pic_info->pTileInfo->TileCols - 1) &&
         (row == std_pic_info->pTileInfo->TileRows - 1);
      til.IsLastTileofFrameFlag = (column == std_pic_info->pTileInfo->TileCols - 1) &&
         (row == std_pic_info->pTileInfo->TileRows - 1);
      til.DisableCDFUpdateFlag = std_pic_info->flags.disable_cdf_update;
      til.DisableFrameContextUpdateFlag = std_pic_info->flags.disable_frame_end_update_cdf || (tile_idx != std_pic_info->pTileInfo->context_update_tile_id);
      til.NumberofActiveBEPipes = 1;
      til.NumofTileColumnsinFrameMinus1 = std_pic_info->pTileInfo->TileCols - 1;
      til.NumofTileRowsinFrameMinus1 = std_pic_info->pTileInfo->TileRows - 1;
   };

   anv_batch_emit(&cmd_buffer->batch, GENX(AVP_BSD_OBJECT), bsd) {
      bsd.TileIndirectBSDDataLength = av1_pic_info->pTileSizes[tile_idx];
      bsd.TileIndirectDataStartAddress = (frame_info->srcBufferOffset & 4095) +
                                         av1_pic_info->pTileOffsets[tile_idx];
   };

   if (last_tile) {
      anv_batch_emit(&cmd_buffer->batch, GENX(AVP_VD_CONTROL_STATE), vd) {
         vd.VDControlState.MemoryImplicitFlush = 1;
      }

      anv_batch_emit(&cmd_buffer->batch, GENX(VD_PIPELINE_FLUSH), vd) {
         vd.AVPPipelineDone = 1;
         vd.VDCommandMessageParserDone = 1;
         vd.AVPPipelineCommandFlush = 1;
      }
   }

   /* Set necessary info from current refs to the prev_refs */
   for (int i = 0; i < STD_VIDEO_AV1_NUM_REF_FRAMES; ++i) {
      vid->prev_refs[i].iv = ref_info[i].iv;
      vid->prev_refs[i].array_layer = ref_info[i].array_layer;
      vid->prev_refs[i].default_cdf_index =
         i == 0 ? ref_info[i].default_cdf_index :
                  find_cdf_index(NULL, ref_info, ref_info[i].iv,
                                 ref_info[i].array_layer);
   }
}

static void
anv_av1_calculate_xstep_qn(struct anv_cmd_buffer *cmd_buffer,
                           const VkVideoDecodeInfoKHR *frame_info,
                           const StdVideoAV1SequenceHeader *seq_hdr)
{
   const VkVideoDecodeAV1PictureInfoKHR *av1_pic_info =
      vk_find_struct_const(frame_info->pNext, VIDEO_DECODE_AV1_PICTURE_INFO_KHR);
   const StdVideoDecodeAV1PictureInfo *std_pic_info = av1_pic_info->pStdPictureInfo;
   VkExtent2D frameExtent = frame_info->dstPictureResource.codedExtent;
   unsigned tile_cols = std_pic_info->pTileInfo->TileCols;

   if (!std_pic_info->flags.use_superres) {
      luma_xstep_qn = chroma_xstep_qn = 0;
      memset(luma_x0_qn, 0, sizeof(luma_x0_qn));
      memset(chroma_x0_qn, 0, sizeof(chroma_x0_qn));

      return;
   }

   int32_t mib_size_log2 = seq_hdr->flags.use_128x128_superblock ?
      av1_max_mib_size_log2 : av1_min_mib_size_log2;

   int32_t mi_cols = align(frameExtent.width, 8) >> mib_size_log2;

   int denom = std_pic_info->coded_denom + 9;
   unsigned downscaled_width = (frameExtent.width * 8 + denom / 2) / denom;

   for (uint8_t i = 0; i < 2; i++) { /* i == 0 : luma, i == 1 : chroma */
      int subsampling_x = seq_hdr->pColorConfig->subsampling_x;
      int ssx = i & subsampling_x;
      int downscaled = align(downscaled_width, 2) >> ssx;
      int upscaled = align(frameExtent.width, 2) >> ssx;

      int xstep_qn = ((downscaled << av1_rs_scale_subpel_bits) + upscaled / 2) / upscaled;

      if (i == 0)
         luma_xstep_qn = xstep_qn;
      else
         chroma_xstep_qn = xstep_qn;

      int32_t err = upscaled * xstep_qn - (downscaled << av1_rs_scale_subpel_bits);
      int32_t x0 = (-((upscaled - downscaled) << (av1_rs_scale_subpel_bits - 1)) + upscaled / 2) /
         upscaled + av1_rs_scale_extra_off - err / 2;

      x0 = (int32_t)(x0 & av1_rs_scale_subpel_mask);

      for (unsigned j = 0; j < tile_cols; j++) {
         int32_t tile_col_end_sb;
         bool last_col = (j == tile_cols - 1);

         if (i == 0)
            luma_x0_qn[j] = x0;
         else
            chroma_x0_qn[j] = x0;

         if (!last_col) {
            tile_col_end_sb = std_pic_info->pTileInfo->pMiColStarts[j + 1];
         } else {
            tile_col_end_sb = std_pic_info->pTileInfo->pMiColStarts[tile_cols - 1] +
                              std_pic_info->pTileInfo->pWidthInSbsMinus1[tile_cols - 1];
         }


         int32_t mi_col_end = tile_col_end_sb >> mib_size_log2;
         mi_col_end = MIN2(mi_col_end, mi_cols);

         int32_t downscaled_x1 = mi_col_end << (av1_mi_size_log2 - ssx);
         int32_t downscaled_x0 = std_pic_info->pTileInfo->pMiColStarts[j] << mib_size_log2 << (av1_mi_size_log2 - ssx);

         int32_t src_w = downscaled_x1 - downscaled_x0;
         int32_t upscaled_x0 = (downscaled_x0 * denom) / 8;
         int32_t upscaled_x1;

         if (last_col) {
            upscaled_x1 = upscaled;
         } else
            upscaled_x1 = (downscaled_x1 * denom) / 8;

         int32_t dst_w = upscaled_x1 - upscaled_x0;

         x0 += (dst_w * xstep_qn) - (src_w << av1_rs_scale_subpel_bits);
      }

   }
}

static void
anv_av1_decode_video(struct anv_cmd_buffer *cmd_buffer,
                     const VkVideoDecodeInfoKHR *frame_info)
{
   const VkVideoDecodeAV1PictureInfoKHR *av1_pic_info =
      vk_find_struct_const(frame_info->pNext, VIDEO_DECODE_AV1_PICTURE_INFO_KHR);
   struct anv_video_session *vid = cmd_buffer->video.vid;
   struct vk_video_session_parameters *params = cmd_buffer->video.params;
   const StdVideoAV1SequenceHeader *seq_hdr;

   vk_video_get_av1_parameters(&vid->vk, params, frame_info, &seq_hdr);

   anv_av1_calculate_xstep_qn(cmd_buffer, frame_info, seq_hdr);

   for (unsigned t = 0; t < av1_pic_info->tileCount; t++)
      anv_av1_decode_video_tile(cmd_buffer, frame_info, seq_hdr, t);
}
#endif

enum vp9_seg_lvl_features
{
   VP9_SEG_LVL_ALT_Q = 0,        /* Use alternate Quantizer */
   VP9_SEG_LVL_ALT_L = 1,        /* Use alternate loop filter value */
   VP9_SEG_LVL_REF_FRAME = 2,    /* Optional Segment reference frame */
   VP9_SEG_LVL_SKIP = 3,         /* Optional Segment (0,0) + skip mode */
   VP9_SEG_LVL_MAX = 4           /* Number of segment features */
};

static uint8_t
anv_vp9_get_ref_idx(const struct VkVideoDecodeInfoKHR *frame_info, int slot_id)
{
   for (unsigned i = 0; i < frame_info->referenceSlotCount; i++) {
      if (slot_id == frame_info->pReferenceSlots[i].slotIndex)
         return i;
   }

   return -1;
}

static void
anv_vp9_decide_prob_tbl_set(struct anv_video_session *vid,
                            const StdVideoDecodeVP9PictureInfo *std_pic)
{
   const bool key_frame = std_pic->frame_type == STD_VIDEO_VP9_FRAME_TYPE_KEY;
   const bool key_frame_or_intra_only =
      std_pic->flags.intra_only || std_pic->frame_type == STD_VIDEO_VP9_FRAME_TYPE_KEY;
   const StdVideoVP9Segmentation *segmentation = std_pic->pSegmentation;

   /* Probability table setting */
   bool reset_all = key_frame || std_pic->flags.error_resilient_mode ||
                    (std_pic->reset_frame_context == 3 && std_pic->flags.intra_only);

   bool reset_specified = std_pic->reset_frame_context == 2 && std_pic->flags.intra_only;
   bool copy_seg_prob = false;
   bool copy_seg_prob_default = false;
   bool do_save_interprobs = false;
   bool do_restore_interprobs = false;

   if (std_pic->flags.segmentation_enabled) {
      if (segmentation->flags.segmentation_update_map) {
         for (int i = 0; i < 4; i++)
            BITSET_CLEAR(vid->copy_seg_probs, i);

         copy_seg_prob = true;
      } else if (!BITSET_TEST(vid->copy_seg_probs, std_pic->frame_context_idx)) {
         copy_seg_prob_default = true;
      }
      /* Mask for indicating seg probs are copied */
      BITSET_SET(vid->copy_seg_probs, std_pic->frame_context_idx);
   }


   /*
    * The prob_tbl_set can have the following:
    *
    * 0: Reset all
    * 1: Reset partially from INTER_MODE_PROBS_OFFSET to SEG_PROBS_OFFSET
    * 2: Copy seg prob
    * 3: Copy seg prob default
    * 4: Save inter probs
    * 5: Restore inter probs
    */

   if (reset_all) {
      BITSET_SET(vid->prob_tbl_set, 0);
      BITSET_SET(vid->frame_ctx_reset_mask, std_pic->frame_context_idx);

      for (int i = 1; i < 4; i++)
         BITSET_CLEAR(vid->frame_ctx_reset_mask, i);
      vid->pending_frame_partial_reset = key_frame_or_intra_only;
      vid->saved_inter_probs = false;
   } else if (reset_specified) {
      if (std_pic->frame_context_idx == 0) {
         BITSET_SET(vid->prob_tbl_set, 0);
         vid->pending_frame_partial_reset = true;
      } else {
         BITSET_CLEAR(vid->frame_ctx_reset_mask, std_pic->frame_context_idx);
         if (!vid->pending_frame_partial_reset) {
            if (!vid->saved_inter_probs) {
               vid->saved_inter_probs = true;
               do_save_interprobs = true;
            }
            BITSET_SET(vid->prob_tbl_set, 1);
         }
      }
   } else if (std_pic->flags.intra_only) {
      if (!vid->pending_frame_partial_reset) {
         if (!vid->saved_inter_probs) {
            vid->saved_inter_probs = true;
            do_save_interprobs = true;
         }
         BITSET_SET(vid->prob_tbl_set, 1);
      }
   } else {
      if (std_pic->frame_context_idx == 0) {
         if (vid->pending_frame_partial_reset) {
            BITSET_SET(vid->prob_tbl_set, 1);
            vid->pending_frame_partial_reset = false;
         } else if (vid->saved_inter_probs) {
            vid->saved_inter_probs = false;
            do_restore_interprobs = true;
         }
      } else {
         if (!key_frame_or_intra_only &&
             !BITSET_TEST(vid->frame_ctx_reset_mask, std_pic->frame_context_idx)) {
            BITSET_SET(vid->frame_ctx_reset_mask, std_pic->frame_context_idx);
            BITSET_SET(vid->prob_tbl_set, 0);
         }
      }
   }

   if (copy_seg_prob) {
      BITSET_SET(vid->prob_tbl_set, 2);
   } else if (copy_seg_prob_default) {
      BITSET_SET(vid->prob_tbl_set, 3);
   }

   if (do_save_interprobs) {
      BITSET_SET(vid->prob_tbl_set, 4);
   } else if (do_restore_interprobs) {
      BITSET_SET(vid->prob_tbl_set, 5);
   }

   return;
}

static void
anv_vp9_decode_video(struct anv_cmd_buffer *cmd_buffer,
                     const VkVideoDecodeInfoKHR *frame_info)
{
   ANV_FROM_HANDLE(anv_buffer, src_buffer, frame_info->srcBuffer);
   struct anv_video_session *vid = cmd_buffer->video.vid;

   const struct VkVideoDecodeVP9PictureInfoKHR *vp9_pic_info =
      vk_find_struct_const(frame_info->pNext, VIDEO_DECODE_VP9_PICTURE_INFO_KHR);

   const StdVideoDecodeVP9PictureInfo *std_pic = vp9_pic_info->pStdPictureInfo;
   const StdVideoVP9Segmentation *segmentation = std_pic->pSegmentation;
   const StdVideoVP9LoopFilter *loop_filter = std_pic->pLoopFilter;

   const bool key_frame = std_pic->frame_type == STD_VIDEO_VP9_FRAME_TYPE_KEY;
   const bool key_frame_or_intra_only =
      std_pic->flags.intra_only || std_pic->frame_type == STD_VIDEO_VP9_FRAME_TYPE_KEY;

   /* 12 bit doesn't support for the moment */
   bool is_10bit = std_pic->pColorConfig->BitDepth > 8;
   uint32_t prob_id = ANV_VID_MEM_VP9_PROBABILITY_0 + std_pic->frame_context_idx;
   bool is_scaling = false;

#if GFX_VER >= 12
   anv_batch_emit(&cmd_buffer->batch, GENX(MI_FORCE_WAKEUP), wake) {
      wake.HEVCPowerWellControl = 1;
      wake.MaskBits = 768;
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(VD_CONTROL_STATE), cs) {
      cs.PipelineInitialization = true;
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(MFX_WAIT), mfx) {
      mfx.MFXSyncControlFlag = 1;
   }
#endif

   anv_batch_emit(&cmd_buffer->batch, GENX(HCP_PIPE_MODE_SELECT), sel) {
      sel.CodecSelect = Decode;
      sel.CodecStandardSelect = VP9;
   }

#if GFX_VER >= 12
   anv_batch_emit(&cmd_buffer->batch, GENX(MFX_WAIT), mfx) {
      mfx.MFXSyncControlFlag = 1;
   }
#endif

   const struct anv_image_view *dst_iv =
      anv_image_view_from_handle(frame_info->dstPictureResource.imageViewBinding);

   const struct anv_image_view *iv = frame_info->pSetupReferenceSlot ?
      anv_image_view_from_handle(frame_info->pSetupReferenceSlot->pPictureResource->imageViewBinding) :
      dst_iv;
   const uint32_t array_layer = frame_info->pSetupReferenceSlot ?
      frame_info->pSetupReferenceSlot->pPictureResource->baseArrayLayer :
      frame_info->dstPictureResource.baseArrayLayer;

   const struct anv_image *img = iv->image;

   uint32_t frame_width = frame_info->dstPictureResource.codedExtent.width;
   uint32_t frame_height = frame_info->dstPictureResource.codedExtent.height;

   if (vid->vp9_last_frame.width != 0 &&
         (frame_width != vid->vp9_last_frame.width ||
          frame_height != vid->vp9_last_frame.height)) {
      is_scaling = true;
   }

   anv_vp9_decide_prob_tbl_set(vid, std_pic);
   anv_update_vp9_tables(cmd_buffer, vid, prob_id, key_frame_or_intra_only, segmentation);

   if (key_frame_or_intra_only || is_scaling || std_pic->flags.error_resilient_mode)
      anv_vp9_reset_segment_id(cmd_buffer, vid);

   anv_batch_emit(&cmd_buffer->batch, GENX(HCP_SURFACE_STATE), ss) {
      ss.SurfacePitch = img->planes[0].primary_surface.isl.row_pitch_B - 1;
      ss.SurfaceID = HCP_CurrentDecodedPicture;
      ss.SurfaceFormat = is_10bit ? P010 : PLANAR_420_8;

      ss.YOffsetforUCb = img->planes[1].primary_surface.memory_range.offset /
         img->planes[0].primary_surface.isl.row_pitch_B;

#if GFX_VER >= 11
      ss.DefaultAlphaValue = 0xffff;
#endif
   }

   uint32_t last_frame_width = 0, last_frame_height = 0;
   uint32_t golden_frame_width = 0, golden_frame_height = 0;
   uint32_t altref_frame_width = 0, altref_frame_height = 0;

   if (!key_frame_or_intra_only) {
      for (uint8_t i = 0; i < 3; i++) {

         uint8_t idx = anv_vp9_get_ref_idx(frame_info, vp9_pic_info->referenceNameSlotIndices[i]);

         const struct anv_image_view *ref_iv =
            anv_image_view_from_handle(frame_info->pReferenceSlots[idx].pPictureResource->imageViewBinding);
         const struct anv_image *ref_img = ref_iv->image;

         assert(frame_info->pReferenceSlots[idx].slotIndex == vp9_pic_info->referenceNameSlotIndices[i]);

         if (i == 0) {
            last_frame_width = frame_info->pReferenceSlots[idx].pPictureResource->codedExtent.width;
            last_frame_height = frame_info->pReferenceSlots[idx].pPictureResource->codedExtent.height;
         } else if (i == 1) {
            golden_frame_width = frame_info->pReferenceSlots[idx].pPictureResource->codedExtent.width;
            golden_frame_height = frame_info->pReferenceSlots[idx].pPictureResource->codedExtent.height;
         } else if (i == 2) {
            altref_frame_width = frame_info->pReferenceSlots[idx].pPictureResource->codedExtent.width;
            altref_frame_height = frame_info->pReferenceSlots[idx].pPictureResource->codedExtent.height;
         }

         anv_batch_emit(&cmd_buffer->batch, GENX(HCP_SURFACE_STATE), ss) {
            ss.SurfacePitch = ref_img->planes[0].primary_surface.isl.row_pitch_B - 1;
            ss.SurfaceID = i + 2;
            ss.SurfaceFormat = is_10bit ? P010 : PLANAR_420_8;

            ss.YOffsetforUCb = ref_img->planes[1].primary_surface.memory_range.offset /
               ref_img->planes[0].primary_surface.isl.row_pitch_B;

#if GFX_VER >= 11
            ss.DefaultAlphaValue = 0xffff;
#endif
         }
      }
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(HCP_PIPE_BUF_ADDR_STATE), buf) {
      buf.DecodedPictureAddress = anv_image_dpb_address(iv, array_layer);

      buf.DecodedPictureMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.DecodedPictureAddress.bo, 0),
#if GFX_VERx10 >= 125
         .TiledResourceMode = TRMODE_TILEF,
#endif
      };

      buf.DeblockingFilterLineBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_VP9_DEBLOCK_FILTER_ROW_STORE_LINE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_VP9_DEBLOCK_FILTER_ROW_STORE_LINE].offset
      };

      buf.DeblockingFilterLineBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.DeblockingFilterLineBufferAddress.bo, 0),
      };

      buf.DeblockingFilterTileLineBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_VP9_DEBLOCK_FILTER_ROW_STORE_TILE_LINE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_VP9_DEBLOCK_FILTER_ROW_STORE_TILE_LINE].offset
      };

      buf.DeblockingFilterTileLineBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.DeblockingFilterTileLineBufferAddress.bo, 0),
      };

      buf.DeblockingFilterTileColumnBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_VP9_DEBLOCK_FILTER_ROW_STORE_TILE_COLUMN].mem->bo,
         vid->vid_mem[ANV_VID_MEM_VP9_DEBLOCK_FILTER_ROW_STORE_TILE_COLUMN].offset
      };

      buf.DeblockingFilterTileColumnBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.DeblockingFilterTileColumnBufferAddress.bo, 0),
      };

      buf.MetadataLineBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_VP9_METADATA_LINE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_VP9_METADATA_LINE].offset
      };

      buf.MetadataLineBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.MetadataLineBufferAddress.bo, 0),
      };

      buf.MetadataTileLineBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_VP9_METADATA_TILE_LINE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_VP9_METADATA_TILE_LINE].offset
      };

      buf.MetadataTileLineBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.MetadataTileLineBufferAddress.bo, 0),
      };

      buf.MetadataTileColumnBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_VP9_METADATA_TILE_COLUMN].mem->bo,
         vid->vid_mem[ANV_VID_MEM_VP9_METADATA_TILE_COLUMN].offset
      };

      buf.MetadataTileColumnBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.MetadataTileColumnBufferAddress.bo, 0),
      };

      buf.SAOLineBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };

      buf.SAOTileLineBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };

      buf.SAOTileColumnBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };

      /* For VP9 decoding, only 2 mv buffers are used alternately */
      uint8_t cur_mv_idx = 0, col_mv_idx = 0;

      if (!key_frame_or_intra_only) {
         if (vid->vp9_last_frame.mv_in_turn) {
            cur_mv_idx = ANV_VID_MEM_VP9_MV_1;
            col_mv_idx = ANV_VID_MEM_VP9_MV_2;
         } else {
            cur_mv_idx = ANV_VID_MEM_VP9_MV_2;
            col_mv_idx = ANV_VID_MEM_VP9_MV_1;
         }
      }

      if (!key_frame_or_intra_only) {
         buf.CurrentMVTemporalBufferAddress = (struct anv_address) {
            vid->vid_mem[cur_mv_idx].mem->bo,
            vid->vid_mem[cur_mv_idx].offset
         };
      }

      buf.CurrentMVTemporalBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, vid->vid_mem[cur_mv_idx].mem->bo, 0),
      };

      if (!key_frame_or_intra_only) {
         for (uint8_t i = 0; i < 3; i++) {
            uint8_t idx = anv_vp9_get_ref_idx(frame_info, vp9_pic_info->referenceNameSlotIndices[i]);

            const struct anv_image_view *ref_iv =
               anv_image_view_from_handle(frame_info->pReferenceSlots[idx].pPictureResource->imageViewBinding);

            assert(frame_info->pReferenceSlots[idx].slotIndex == vp9_pic_info->referenceNameSlotIndices[i]);

            buf.ReferencePictureAddress[i] =
               anv_image_dpb_address(ref_iv, frame_info->pReferenceSlots[idx].pPictureResource->baseArrayLayer);
         }
      }

      buf.ReferencePictureMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
#if GFX_VERx10 >= 125
         .TiledResourceMode = TRMODE_TILEF,
#endif
      };

      buf.OriginalUncompressedPictureSourceMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };

      buf.StreamOutDataDestinationMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };

      buf.DecodedPictureStatusBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };

      buf.LCUILDBStreamOutBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };

      if (!key_frame_or_intra_only) {
         buf.CollocatedMVTemporalBufferAddress[0] = (struct anv_address) {
            vid->vid_mem[col_mv_idx].mem->bo,
            vid->vid_mem[col_mv_idx].offset
         };
      }

      buf.CollocatedMVTemporalBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.CollocatedMVTemporalBufferAddress[0].bo, 0),
      };

      buf.VP9ProbabilityBufferAddress = (struct anv_address) {
         vid->vid_mem[prob_id].mem->bo,
         vid->vid_mem[prob_id].offset
      };

      buf.VP9ProbabilityBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.VP9ProbabilityBufferAddress.bo, 0),
      };

      buf.VP9SegmentIDBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_VP9_SEGMENT_ID].mem->bo,
         vid->vid_mem[ANV_VID_MEM_VP9_SEGMENT_ID].offset
      };

      buf.VP9SegmentIDBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };

      buf.VP9HVDLineRowStoreBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_VP9_HVD_LINE_ROW_STORE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_VP9_HVD_LINE_ROW_STORE].offset
      };

      buf.VP9HVDLineRowStoreBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.VP9HVDLineRowStoreBufferAddress.bo, 0),
      };

      buf.VP9HVDTileRowStoreBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_VP9_HVD_TILE_ROW_STORE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_VP9_HVD_TILE_ROW_STORE].offset
      };

      buf.VP9HVDTileRowStoreBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, buf.VP9HVDTileRowStoreBufferAddress.bo, 0),
      };

#if GFX_VER >= 11
      buf.SAOStreamOutDataDestinationBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.FrameStatisticsStreamOutDataDestinationBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.SSESourcePixelRowStoreBufferMemoryAddressAttributesReadWrite = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.HCPScalabilitySliceStateBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.HCPScalabilityCABACDecodedSyntaxElementsBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.MVUpperRightColumnStoreBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.IntraPredictionUpperRightColumnStoreBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      buf.IntraPredictionLeftReconColumnStoreBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
#endif
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(HCP_IND_OBJ_BASE_ADDR_STATE), indirect) {
      indirect.HCPIndirectBitstreamObjectBaseAddress =
         anv_address_add(src_buffer->address, frame_info->srcBufferOffset & ~4095);

      indirect.HCPIndirectBitstreamObjectMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, src_buffer->address.bo, 0),
      };

      indirect.HCPIndirectCUObjectMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };

      indirect.HCPPAKBSEObjectMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };

#if GFX_VER >= 11
      indirect.HCPVP9PAKCompressedHeaderSyntaxStreamInMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      indirect.HCPVP9PAKProbabilityCounterStreamOutMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      indirect.HCPVP9PAKProbabilityDeltasStreamInMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      indirect.HCPVP9PAKTileRecordStreamOutMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
      indirect.HCPVP9PAKCULevelStatisticStreamOutMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd_buffer->device, NULL, 0),
      };
#endif
   }

   uint32_t num_segments;
   if (!std_pic->flags.segmentation_enabled)
      num_segments = 1;
   else
      num_segments = 8;

   for (uint32_t i = 0; i < num_segments; i++) {
      anv_batch_emit(&cmd_buffer->batch, GENX(HCP_VP9_SEGMENT_STATE), seg) {
         uint32_t qyac = std_pic->base_q_idx;
         uint8_t lvl = loop_filter->loop_filter_level;
         bool skip = false;
         bool ref_enable = false;
         uint16_t ref_val = 0;

         seg.SegmentID = i;

         if (std_pic->flags.segmentation_enabled && segmentation) {
            u_foreach_bit(val, segmentation->FeatureEnabled[i]) {
               if (val == VP9_SEG_LVL_ALT_Q) {
                  qyac = segmentation->FeatureData[i][val];
                  if (!segmentation->flags.segmentation_abs_or_delta_update)
                     qyac += std_pic->base_q_idx;
               } else if (val == VP9_SEG_LVL_ALT_L) {
                  lvl = segmentation->FeatureData[i][val];
                  if (!std_pic->pSegmentation->flags.segmentation_abs_or_delta_update)
                     lvl += loop_filter->loop_filter_level;
                  lvl = CLAMP(lvl, 0, 63);
               } else if (val == VP9_SEG_LVL_REF_FRAME) {
                  ref_enable = true;
                  ref_val = segmentation->FeatureData[i][val];
               } else if (val == VP9_SEG_LVL_SKIP) {
                  skip = true;
               }
            }
         }

         seg.SegmentSkipped = skip;
         seg.SegmentReferenceEnable = ref_enable;
         seg.SegmentReference = ref_val;

         int16_t qmul[2][2] = { 0, };
         anv_calculate_qmul(vp9_pic_info, qyac, i, (int16_t *)qmul);

         seg.LumaDCQuantScale = qmul[0][0];
         seg.LumaACQuantScale = qmul[0][1];
         seg.ChromaDCQuantScale = qmul[1][0];
         seg.ChromaACQuantScale = qmul[1][1];

         if (loop_filter->flags.loop_filter_delta_enabled) {
            int shift = loop_filter->loop_filter_level >= 32;

            seg.FilterLevelRef0Mode0 = CLAMP(lvl +
                  loop_filter->loop_filter_ref_deltas[0] * (1 << shift), 0, 63);
            seg.FilterLevelRef0Mode1 = seg.FilterLevelRef0Mode0;

            seg.FilterLevelRef1Mode0 = CLAMP(lvl +
                  (loop_filter->loop_filter_ref_deltas[1] +
                   loop_filter->loop_filter_mode_deltas[0]) * (1 << shift), 0, 0x3f);
            seg.FilterLevelRef1Mode1 = CLAMP(lvl +
                  (loop_filter->loop_filter_ref_deltas[1] +
                   loop_filter->loop_filter_mode_deltas[1]) * (1 << shift), 0, 0x3f);

            seg.FilterLevelRef2Mode0 = CLAMP(lvl +
                  (loop_filter->loop_filter_ref_deltas[2] +
                   loop_filter->loop_filter_mode_deltas[0]) * (1 << shift), 0, 0x3f);

            seg.FilterLevelRef2Mode1 = CLAMP(lvl +
                  (loop_filter->loop_filter_ref_deltas[2] +
                   loop_filter->loop_filter_mode_deltas[1]) * (1 << shift), 0, 0x3f);

            seg.FilterLevelRef3Mode0 = CLAMP(lvl +
                  (loop_filter->loop_filter_ref_deltas[3] +
                   loop_filter->loop_filter_mode_deltas[0]) * (1 << shift), 0, 0x3f);

            seg.FilterLevelRef3Mode1 = CLAMP(lvl +
                  (loop_filter->loop_filter_ref_deltas[3] +
                   loop_filter->loop_filter_mode_deltas[1]) * (1 << shift), 0, 0x3f);

            /* Clear filter level when filter level is zero in the picture param */
            if (!loop_filter->loop_filter_level) {
               seg.FilterLevelRef0Mode0 = 0;
               seg.FilterLevelRef2Mode0 = 0;
            }
         } else {
            seg.FilterLevelRef0Mode0 = lvl;
            seg.FilterLevelRef0Mode1 = lvl;
            seg.FilterLevelRef1Mode0 = lvl;
            seg.FilterLevelRef1Mode1 = lvl;
            seg.FilterLevelRef2Mode0 = lvl;
            seg.FilterLevelRef2Mode1 = lvl;
            seg.FilterLevelRef3Mode0 = lvl;
            seg.FilterLevelRef3Mode1 = lvl;
         }
      }
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(HCP_VP9_PIC_STATE), pic) {
      if (std_pic->flags.segmentation_enabled)
         assert(segmentation != NULL);
      pic.FrameWidth = align(frame_width, 8) - 1;
      pic.FrameHeight = align(frame_height, 8) - 1;
      /* STD_VIDEO_VP9_FRAME_TYPE_KEY == VP9_Key_frmae
       * STD_VIDEO_VP9_FRAME_TYPE_NON_KEY == VP9_InterFrame
       */
      pic.FrameType = std_pic->frame_type;
      pic.AdaptProbabilities = !std_pic->flags.error_resilient_mode && !std_pic->flags.frame_parallel_decoding_mode;
      pic.IntraOnly = std_pic->flags.intra_only;
      pic.RefreshFrameContextEnable = std_pic->flags.refresh_frame_context;
      pic.ErrorResilientModeEnable = std_pic->flags.error_resilient_mode;
      pic.FrameParallelDecodingModeEnable = std_pic->flags.frame_parallel_decoding_mode;
      pic.FilterLevel = std_pic->pLoopFilter->loop_filter_level;
      pic.SharpnessLevel = std_pic->pLoopFilter->loop_filter_sharpness;
      pic.SegmentationEnable = std_pic->flags.segmentation_enabled;
      pic.SegmentationUpdateMap = segmentation && segmentation->flags.segmentation_update_map;
      pic.LosslessMode = (std_pic->base_q_idx == 0) && (std_pic->delta_q_y_dc == 0) &&
                         (std_pic->delta_q_uv_dc == 0) && (std_pic->delta_q_uv_ac == 0);
      pic.SegmentIDStreamOutEnable = pic.SegmentationUpdateMap;


      pic.SegmentIDStreamInEnable = key_frame_or_intra_only ||
         (std_pic->flags.segmentation_enabled &&
            (!segmentation->flags.segmentation_update_map || segmentation->flags.segmentation_temporal_update)) ||
         std_pic->flags.error_resilient_mode ||
         is_scaling;

      pic.Log2TileColumn = std_pic->tile_cols_log2;
      pic.Log2TileRow = std_pic->tile_rows_log2;

      if (std_pic->pColorConfig->subsampling_x == 1 || std_pic->pColorConfig->subsampling_y == 1) {
         pic.ChromaSamplingFormat = Format_420;
      } else if (std_pic->pColorConfig->subsampling_x == 1 || std_pic->pColorConfig->subsampling_y == 0) {
         pic.ChromaSamplingFormat = Format_422;
      } else if (std_pic->pColorConfig->subsampling_x == 0 || std_pic->pColorConfig->subsampling_y == 0) {
         pic.ChromaSamplingFormat = Format_444;
      }

      pic.BitDepth = is_10bit ? _10bit : _8bit;
      pic.ProfileLevel = std_pic->profile;

      pic.UncompressedHeaderLength = vp9_pic_info->compressedHeaderOffset - vp9_pic_info->uncompressedHeaderOffset;
      pic.FirstPartitionSize = vp9_pic_info->tilesOffset - vp9_pic_info->compressedHeaderOffset;
      pic.MotionCompScalingEnable = true;

      if (!key_frame) {
         pic.AllowHiPrecisionMV = std_pic->flags.allow_high_precision_mv;
         pic.MotionCompensationFilterType = std_pic->interpolation_filter;
         pic.SegmentationTemporalUpdate = key_frame_or_intra_only || !segmentation ? 0 : segmentation->flags.segmentation_temporal_update;

         bool use_pre_frame_mvs = !((std_pic->flags.error_resilient_mode) ||
                                   (frame_width != vid->vp9_last_frame.width) ||
                                   (frame_height != vid->vp9_last_frame.height) ||
                                   vid->vp9_last_frame.key_frame ||
                                   !vid->vp9_last_frame.show_frame);

         if (is_scaling)
            use_pre_frame_mvs = false;

         pic.ReferenceFrameSignBias  = std_pic->ref_frame_sign_bias_mask >> 1;
         pic.LastFrameType = vid->vp9_last_frame.frame_type;
         pic.UsePrevinFindMVReferences = use_pre_frame_mvs;

         pic.HorizontalScaleFactorforLAST = (last_frame_width << ANV_VP9_SCALE_FACTOR_SHIFT) / frame_width;
         pic.VerticalScaleFactorforLAST = (last_frame_height << ANV_VP9_SCALE_FACTOR_SHIFT) / frame_height;
         pic.HorizontalScaleFactorforGOLDEN = (golden_frame_width << ANV_VP9_SCALE_FACTOR_SHIFT) / frame_width;
         pic.VerticalScaleFactorforGOLDEN = (golden_frame_height << ANV_VP9_SCALE_FACTOR_SHIFT) / frame_height;;
         pic.HorizontalScaleFactorforALTREF = (altref_frame_width << ANV_VP9_SCALE_FACTOR_SHIFT) / frame_width;
         pic.VerticalScaleFactorforALTREF = (altref_frame_height << ANV_VP9_SCALE_FACTOR_SHIFT) / frame_height;;

         pic.LastFrameWidth = last_frame_width == 0 ? 0 : last_frame_width - 1;
         pic.LastFrameHeight = last_frame_height == 0 ? 0 : last_frame_height - 1;
         pic.GoldenFrameWidth = golden_frame_width == 0 ? 0 : golden_frame_width - 1;
         pic.GoldenFrameHeight = golden_frame_height == 0 ? 0 : golden_frame_height - 1;
         pic.AltrefFrameWidth = altref_frame_width == 0 ? 0 : altref_frame_width - 1;
         pic.AltrefFrameHeight = altref_frame_height == 0 ? 0 : altref_frame_height - 1;

      }

   }

   vid->vp9_last_frame.frame_type = std_pic->frame_type;
   vid->vp9_last_frame.width = frame_width;
   vid->vp9_last_frame.height = frame_height;
   vid->vp9_last_frame.key_frame = key_frame_or_intra_only;
   vid->vp9_last_frame.show_frame = std_pic->flags.show_frame;
   vid->vp9_last_frame.mv_in_turn = !vid->vp9_last_frame.mv_in_turn;

   anv_batch_emit(&cmd_buffer->batch, GENX(HCP_BSD_OBJECT), bsd) {
      bsd.IndirectBSDDataLength = frame_info->srcBufferRange - vp9_pic_info->compressedHeaderOffset;
      bsd.IndirectBSDDataStartAddress = (frame_info->srcBufferOffset & 4095) + vp9_pic_info->compressedHeaderOffset;
   }

#if GFX_VER >= 12
   anv_batch_emit(&cmd_buffer->batch, GENX(VD_CONTROL_STATE), cs) {
      cs.MemoryImplicitFlush = true;
   }
#endif

   anv_batch_emit(&cmd_buffer->batch, GENX(VD_PIPELINE_FLUSH), flush) {
      flush.HEVCPipelineDone = true;
      flush.HEVCPipelineCommandFlush = true;
      flush.VDCommandMessageParserDone = true;
   }
}

static void
handle_inline_query_end(struct anv_cmd_buffer *cmd_buffer,
                        const VkVideoInlineQueryInfoKHR *inline_query)
{
   ANV_FROM_HANDLE(anv_query_pool, pool, inline_query->queryPool);
   if (pool == VK_NULL_HANDLE)
      return;

   struct anv_address query_addr = {
      .bo = pool->bo,
      .offset = inline_query->firstQuery * pool->stride,
   };

   anv_batch_emit(&cmd_buffer->batch, GENX(MI_FLUSH_DW), flush) {
      flush.PostSyncOperation = WriteImmediateData;
      flush.Address = query_addr;
      flush.ImmediateData = true;
   }
}

void
genX(CmdDecodeVideoKHR)(VkCommandBuffer commandBuffer,
                        const VkVideoDecodeInfoKHR *frame_info)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   if (anv_batch_has_error(&cmd_buffer->batch))
      return;

   const VkVideoInlineQueryInfoKHR *inline_query =
      vk_find_struct_const(frame_info->pNext, VIDEO_INLINE_QUERY_INFO_KHR);

   switch (cmd_buffer->video.vid->vk.op) {
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
      anv_h264_decode_video(cmd_buffer, frame_info);
      break;
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
      anv_h265_decode_video(cmd_buffer, frame_info);
      break;
#if GFX_VERx10 >= 120
   case VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR:
      anv_av1_decode_video(cmd_buffer, frame_info);
      break;
#endif
   case VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR:
      anv_vp9_decode_video(cmd_buffer, frame_info);
      break;
   default:
      assert(0);
   }

   if (inline_query)
      handle_inline_query_end(cmd_buffer, inline_query);
}
