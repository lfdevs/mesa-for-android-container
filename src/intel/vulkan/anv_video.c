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

#include "av1_tables.h"
#include "vk_video/vulkan_video_codecs_common.h"

VkResult
anv_CreateVideoSessionKHR(VkDevice _device,
                           const VkVideoSessionCreateInfoKHR *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkVideoSessionKHR *pVideoSession)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   struct anv_video_session *vid =
      vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*vid), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!vid)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   memset(vid, 0, sizeof(struct anv_video_session));

   VkResult result = vk_video_session_init(&device->vk,
                                           &vid->vk,
                                           pCreateInfo);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, vid);
      return result;
   }

   *pVideoSession = anv_video_session_to_handle(vid);
   return VK_SUCCESS;
}

void
anv_DestroyVideoSessionKHR(VkDevice _device,
                           VkVideoSessionKHR _session,
                           const VkAllocationCallbacks *pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_video_session, vid, _session);
   if (!_session)
      return;

   vk_object_base_finish(&vid->vk.base);
   vk_free2(&device->vk.alloc, pAllocator, vid);
}

VkResult
anv_CreateVideoSessionParametersKHR(VkDevice _device,
                                     const VkVideoSessionParametersCreateInfoKHR *pCreateInfo,
                                     const VkAllocationCallbacks *pAllocator,
                                     VkVideoSessionParametersKHR *pVideoSessionParameters)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_video_session, vid, pCreateInfo->videoSession);
   ANV_FROM_HANDLE(anv_video_session_params, templ, pCreateInfo->videoSessionParametersTemplate);
   struct anv_video_session_params *params =
      vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*params), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!params)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = vk_video_session_parameters_init(&device->vk,
                                                      &params->vk,
                                                      &vid->vk,
                                                      templ ? &templ->vk : NULL,
                                                      pCreateInfo);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, params);
      return result;
   }

   *pVideoSessionParameters = anv_video_session_params_to_handle(params);
   return VK_SUCCESS;
}

void
anv_DestroyVideoSessionParametersKHR(VkDevice _device,
                                      VkVideoSessionParametersKHR _params,
                                      const VkAllocationCallbacks *pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_video_session_params, params, _params);
   if (!_params)
      return;
   vk_video_session_parameters_finish(&device->vk, &params->vk);
   vk_free2(&device->vk.alloc, pAllocator, params);
}

VkResult
anv_GetPhysicalDeviceVideoCapabilitiesKHR(VkPhysicalDevice physicalDevice,
                                           const VkVideoProfileInfoKHR *pVideoProfile,
                                           VkVideoCapabilitiesKHR *pCapabilities)
{
   ANV_FROM_HANDLE(anv_physical_device, pdevice, physicalDevice);

   pCapabilities->minBitstreamBufferOffsetAlignment = 32;
   pCapabilities->minBitstreamBufferSizeAlignment = 1;
   pCapabilities->pictureAccessGranularity.width = ANV_MB_WIDTH;
   pCapabilities->pictureAccessGranularity.height = ANV_MB_HEIGHT;
   pCapabilities->minCodedExtent.width = ANV_MB_WIDTH;
   pCapabilities->minCodedExtent.height = ANV_MB_HEIGHT;
   pCapabilities->maxCodedExtent.width = 4096;
   pCapabilities->maxCodedExtent.height = 4096;
   pCapabilities->flags = VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR;

   struct VkVideoDecodeCapabilitiesKHR *dec_caps = (struct VkVideoDecodeCapabilitiesKHR *)
      vk_find_struct(pCapabilities->pNext, VIDEO_DECODE_CAPABILITIES_KHR);

   if (dec_caps)
      dec_caps->flags = VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR;

   /* H264 allows different luma and chroma bit depths */
   if (pVideoProfile->lumaBitDepth != pVideoProfile->chromaBitDepth)
      return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;

   if (pVideoProfile->chromaSubsampling != VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR)
      return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;

   switch (pVideoProfile->videoCodecOperation) {
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR: {
      struct VkVideoDecodeH264CapabilitiesKHR *ext = (struct VkVideoDecodeH264CapabilitiesKHR *)
         vk_find_struct(pCapabilities->pNext, VIDEO_DECODE_H264_CAPABILITIES_KHR);

      if (pVideoProfile->lumaBitDepth != VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR)
         return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;

      pCapabilities->maxDpbSlots = ANV_VIDEO_H264_MAX_DPB_SLOTS;
      pCapabilities->maxActiveReferencePictures = ANV_VIDEO_H264_MAX_NUM_REF_FRAME;
      pCapabilities->pictureAccessGranularity.width = ANV_MB_WIDTH;
      pCapabilities->pictureAccessGranularity.height = ANV_MB_HEIGHT;
      pCapabilities->minCodedExtent.width = ANV_MB_WIDTH;
      pCapabilities->minCodedExtent.height = ANV_MB_HEIGHT;

      ext->fieldOffsetGranularity.x = 0;
      ext->fieldOffsetGranularity.y = 0;
      ext->maxLevelIdc = STD_VIDEO_H264_LEVEL_IDC_5_1;
      strcpy(pCapabilities->stdHeaderVersion.extensionName, VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME);
      pCapabilities->stdHeaderVersion.specVersion = VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION;
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR: {

      const struct VkVideoDecodeAV1ProfileInfoKHR *av1_profile =
         vk_find_struct_const(pVideoProfile->pNext, VIDEO_DECODE_AV1_PROFILE_INFO_KHR);

      if (av1_profile->stdProfile != STD_VIDEO_AV1_PROFILE_MAIN)
         return VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR;

      if (pVideoProfile->lumaBitDepth != VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR &&
          pVideoProfile->lumaBitDepth != VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR)
         return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;

      struct VkVideoDecodeAV1CapabilitiesKHR *ext = (struct VkVideoDecodeAV1CapabilitiesKHR *)
         vk_find_struct(pCapabilities->pNext, VIDEO_DECODE_AV1_CAPABILITIES_KHR);

      ext->maxLevel = STD_VIDEO_AV1_LEVEL_6_0;

      pCapabilities->maxDpbSlots = 8;
      pCapabilities->maxActiveReferencePictures = 7;
      dec_caps->flags |= VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_DISTINCT_BIT_KHR;

      strcpy(pCapabilities->stdHeaderVersion.extensionName, VK_STD_VULKAN_VIDEO_CODEC_AV1_DECODE_EXTENSION_NAME);
      pCapabilities->stdHeaderVersion.specVersion = VK_STD_VULKAN_VIDEO_CODEC_AV1_DECODE_SPEC_VERSION;
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR: {
      struct VkVideoDecodeH265CapabilitiesKHR *ext = (struct VkVideoDecodeH265CapabilitiesKHR *)
         vk_find_struct(pCapabilities->pNext, VIDEO_DECODE_H265_CAPABILITIES_KHR);

      const struct VkVideoDecodeH265ProfileInfoKHR *h265_profile =
         vk_find_struct_const(pVideoProfile->pNext,
                              VIDEO_DECODE_H265_PROFILE_INFO_KHR);

      /* No hardware supports the scc extension profile */
      if (h265_profile->stdProfileIdc != STD_VIDEO_H265_PROFILE_IDC_MAIN &&
          h265_profile->stdProfileIdc != STD_VIDEO_H265_PROFILE_IDC_MAIN_10 &&
          h265_profile->stdProfileIdc != STD_VIDEO_H265_PROFILE_IDC_MAIN_STILL_PICTURE &&
          h265_profile->stdProfileIdc != STD_VIDEO_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSIONS)
         return VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR;

      /* Skylake only supports the main profile */
      if (h265_profile->stdProfileIdc != STD_VIDEO_H265_PROFILE_IDC_MAIN &&
          h265_profile->stdProfileIdc != STD_VIDEO_H265_PROFILE_IDC_MAIN_STILL_PICTURE &&
          pdevice->info.platform <= INTEL_PLATFORM_SKL)
         return VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR;

      /* Gfx10 and under don't support the range extension profile */
      if (h265_profile->stdProfileIdc != STD_VIDEO_H265_PROFILE_IDC_MAIN &&
          h265_profile->stdProfileIdc != STD_VIDEO_H265_PROFILE_IDC_MAIN_10 &&
          h265_profile->stdProfileIdc != STD_VIDEO_H265_PROFILE_IDC_MAIN_STILL_PICTURE &&
          pdevice->info.ver <= 10)
         return VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR;

      if (pVideoProfile->lumaBitDepth != VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR &&
          pVideoProfile->lumaBitDepth != VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR)
         return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;

      pCapabilities->pictureAccessGranularity.width = ANV_MAX_H265_CTB_SIZE;
      pCapabilities->pictureAccessGranularity.height = ANV_MAX_H265_CTB_SIZE;
      pCapabilities->minCodedExtent.width = ANV_MAX_H265_CTB_SIZE;
      pCapabilities->minCodedExtent.height = ANV_MAX_H265_CTB_SIZE;
      pCapabilities->maxDpbSlots = ANV_VIDEO_H265_MAX_NUM_REF_FRAME;
      pCapabilities->maxActiveReferencePictures = ANV_VIDEO_H265_HCP_NUM_REF_FRAME;

      ext->maxLevelIdc = STD_VIDEO_H265_LEVEL_IDC_6_2;

      strcpy(pCapabilities->stdHeaderVersion.extensionName, VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_EXTENSION_NAME);
      pCapabilities->stdHeaderVersion.specVersion = VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_SPEC_VERSION;
      break;
   }
   default:
      break;
   }

   struct VkVideoEncodeCapabilitiesKHR *enc_caps = (struct VkVideoEncodeCapabilitiesKHR *)
      vk_find_struct(pCapabilities->pNext, VIDEO_ENCODE_CAPABILITIES_KHR);

   if (enc_caps) {
      enc_caps->flags = 0;
      enc_caps->rateControlModes = VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DEFAULT_KHR |
                                   VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR;
      enc_caps->maxRateControlLayers = 1;
      enc_caps->maxQualityLevels = 1;
      enc_caps->encodeInputPictureGranularity.width = 32;
      enc_caps->encodeInputPictureGranularity.height = 32;
      enc_caps->supportedEncodeFeedbackFlags =
         VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BUFFER_OFFSET_BIT_KHR |
         VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BYTES_WRITTEN_BIT_KHR;
   }

   switch (pVideoProfile->videoCodecOperation) {
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR: {
      struct VkVideoEncodeH264CapabilitiesKHR *ext = (struct VkVideoEncodeH264CapabilitiesKHR *)
         vk_find_struct(pCapabilities->pNext, VIDEO_ENCODE_H264_CAPABILITIES_KHR);

      if (ext) {
         ext->flags = VK_VIDEO_ENCODE_H264_CAPABILITY_HRD_COMPLIANCE_BIT_KHR;
         ext->maxLevelIdc = STD_VIDEO_H264_LEVEL_IDC_5_1;
         ext->maxSliceCount = 1;
         ext->maxPPictureL0ReferenceCount = 8;
         ext->maxBPictureL0ReferenceCount = 8;
         ext->maxL1ReferenceCount = 0;
         ext->maxTemporalLayerCount = 0;
         ext->expectDyadicTemporalLayerPattern = false;
         ext->prefersGopRemainingFrames = 0;
         ext->requiresGopRemainingFrames = 0;
         ext->minQp = 10;
         ext->maxQp = 51;
      }

      pCapabilities->minBitstreamBufferOffsetAlignment = 32;
      pCapabilities->minBitstreamBufferSizeAlignment = 4096;

      pCapabilities->maxDpbSlots = ANV_VIDEO_H264_MAX_NUM_REF_FRAME;
      pCapabilities->maxActiveReferencePictures = ANV_VIDEO_H264_MAX_NUM_REF_FRAME;
      pCapabilities->pictureAccessGranularity.width = ANV_MB_WIDTH;
      pCapabilities->pictureAccessGranularity.height = ANV_MB_HEIGHT;
      pCapabilities->minCodedExtent.width = ANV_MB_WIDTH;
      pCapabilities->minCodedExtent.height = ANV_MB_HEIGHT;

      strcpy(pCapabilities->stdHeaderVersion.extensionName, VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_EXTENSION_NAME);
      pCapabilities->stdHeaderVersion.specVersion = VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_SPEC_VERSION;
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR: {
      struct VkVideoEncodeH265CapabilitiesKHR *ext = (struct VkVideoEncodeH265CapabilitiesKHR *)
         vk_find_struct(pCapabilities->pNext, VIDEO_ENCODE_H265_CAPABILITIES_KHR);

      if (ext) {
         ext->flags = 0;
         ext->maxLevelIdc = STD_VIDEO_H265_LEVEL_IDC_5_1;
         ext->ctbSizes = VK_VIDEO_ENCODE_H265_CTB_SIZE_64_BIT_KHR;
         ext->transformBlockSizes = VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_4_BIT_KHR |
                                    VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_8_BIT_KHR |
                                    VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_16_BIT_KHR |
                                    VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_32_BIT_KHR;
         ext->maxPPictureL0ReferenceCount = 8;
         ext->maxBPictureL0ReferenceCount = 8;
         ext->maxL1ReferenceCount = 1;
         ext->minQp = 10;
         ext->maxQp = 51;
         ext->maxSliceSegmentCount = 128;
         ext->maxTiles.width = 1;
         ext->maxTiles.height = 1;
         ext->maxSubLayerCount = 1;
         ext->expectDyadicTemporalSubLayerPattern = false;
         ext->prefersGopRemainingFrames = 0;
         ext->requiresGopRemainingFrames = 0;
      }

      pCapabilities->minBitstreamBufferOffsetAlignment = 4096;
      pCapabilities->minBitstreamBufferSizeAlignment = 4096;

      pCapabilities->maxDpbSlots = ANV_VIDEO_H265_MAX_NUM_REF_FRAME;
      pCapabilities->maxActiveReferencePictures = ANV_VIDEO_H265_MAX_NUM_REF_FRAME;
      pCapabilities->pictureAccessGranularity.width = ANV_MAX_H265_CTB_SIZE;
      pCapabilities->pictureAccessGranularity.height = ANV_MAX_H265_CTB_SIZE;
      pCapabilities->minCodedExtent.width = ANV_MAX_H265_CTB_SIZE;
      pCapabilities->minCodedExtent.height = ANV_MAX_H265_CTB_SIZE;

      strcpy(pCapabilities->stdHeaderVersion.extensionName, VK_STD_VULKAN_VIDEO_CODEC_H265_ENCODE_EXTENSION_NAME);
      pCapabilities->stdHeaderVersion.specVersion = VK_STD_VULKAN_VIDEO_CODEC_H265_ENCODE_SPEC_VERSION;
      break;
   }
   default:
      break;
   }

   return VK_SUCCESS;
}

VkResult
anv_GetPhysicalDeviceVideoFormatPropertiesKHR(VkPhysicalDevice physicalDevice,
                                               const VkPhysicalDeviceVideoFormatInfoKHR *pVideoFormatInfo,
                                               uint32_t *pVideoFormatPropertyCount,
                                               VkVideoFormatPropertiesKHR *pVideoFormatProperties)
{
   VK_OUTARRAY_MAKE_TYPED(VkVideoFormatPropertiesKHR, out,
                          pVideoFormatProperties,
                          pVideoFormatPropertyCount);

   const struct VkVideoProfileListInfoKHR *prof_list = (struct VkVideoProfileListInfoKHR *)
      vk_find_struct_const(pVideoFormatInfo->pNext, VIDEO_PROFILE_LIST_INFO_KHR);

   /* We only support VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT with
    * Y-tiling/Tile4, as supported by the hardware for video decoding.
    * However, we are unable to determine the tiling without modifiers here.
    * So just disable them all.
    */
   const bool decode_dst = !!(pVideoFormatInfo->imageUsage &
                              VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR);

   if (prof_list) {
      for (unsigned i = 0; i < prof_list->profileCount; i++) {
         const VkVideoProfileInfoKHR *profile = &prof_list->pProfiles[i];

         if (profile->lumaBitDepth & VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR ||
             profile->chromaBitDepth & VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR) {
            vk_outarray_append_typed(VkVideoFormatPropertiesKHR, &out, p) {
               p->format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
               p->imageCreateFlags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
               p->imageType = VK_IMAGE_TYPE_2D;
               p->imageTiling = VK_IMAGE_TILING_OPTIMAL;
               p->imageUsageFlags = pVideoFormatInfo->imageUsage;
            }

            if (!decode_dst) {
               vk_outarray_append_typed(VkVideoFormatPropertiesKHR, &out, p) {
                  p->format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
                  p->imageCreateFlags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
                  p->imageType = VK_IMAGE_TYPE_2D;
                  p->imageTiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
                  p->imageUsageFlags = pVideoFormatInfo->imageUsage;
               }
            }
         }

         if (profile->lumaBitDepth & VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR ||
             profile->chromaBitDepth & VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR) {
            vk_outarray_append_typed(VkVideoFormatPropertiesKHR, &out, p) {
               p->format = VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
               p->imageCreateFlags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
               p->imageType = VK_IMAGE_TYPE_2D;
               p->imageTiling = VK_IMAGE_TILING_OPTIMAL;
               p->imageUsageFlags = pVideoFormatInfo->imageUsage;
            }
            if (!decode_dst) {
               vk_outarray_append_typed(VkVideoFormatPropertiesKHR, &out, p) {
                  p->format = VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
                  p->imageCreateFlags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
                  p->imageType = VK_IMAGE_TYPE_2D;
                  p->imageTiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
                  p->imageUsageFlags = pVideoFormatInfo->imageUsage;
               }
            }
         }
      }
   }

   return vk_outarray_status(&out);
}

static uint64_t
get_h264_video_mem_size(struct anv_video_session *vid, uint32_t mem_idx)
{
   uint32_t width_in_mb =
      align(vid->vk.max_coded.width, ANV_MB_WIDTH) / ANV_MB_WIDTH;

   switch (mem_idx) {
   case ANV_VID_MEM_H264_INTRA_ROW_STORE:
      return width_in_mb * 64;
   case ANV_VID_MEM_H264_DEBLOCK_FILTER_ROW_STORE:
      return width_in_mb * 64 * 4;
   case ANV_VID_MEM_H264_BSD_MPC_ROW_SCRATCH:
      return width_in_mb * 64 * 2;
   case ANV_VID_MEM_H264_MPR_ROW_SCRATCH:
      return width_in_mb * 64 * 2;
   default:
      unreachable("unknown memory");
   }
}

static uint64_t
get_h265_video_mem_size(struct anv_video_session *vid, uint32_t mem_idx)
{
   uint32_t bit_shift =
      vid->vk.h265.profile_idc == STD_VIDEO_H265_PROFILE_IDC_MAIN_10 ? 2 : 3;

   /* TODO. these sizes can be determined dynamically depending on ctb sizes of each slice. */
   uint32_t width_in_ctb =
      align(vid->vk.max_coded.width, ANV_MAX_H265_CTB_SIZE) / ANV_MAX_H265_CTB_SIZE;
   uint32_t height_in_ctb =
      align(vid->vk.max_coded.height, ANV_MAX_H265_CTB_SIZE) / ANV_MAX_H265_CTB_SIZE;
   uint64_t size;

   switch (mem_idx) {
   case ANV_VID_MEM_H265_DEBLOCK_FILTER_ROW_STORE_LINE:
   case ANV_VID_MEM_H265_DEBLOCK_FILTER_ROW_STORE_TILE_LINE:
      size = align(vid->vk.max_coded.width, 32) >> bit_shift;
      break;
   case ANV_VID_MEM_H265_DEBLOCK_FILTER_ROW_STORE_TILE_COLUMN:
      size = align(vid->vk.max_coded.height + 6 * height_in_ctb, 32) >> bit_shift;
      break;
   case ANV_VID_MEM_H265_METADATA_LINE:
      size = (((vid->vk.max_coded.width + 15) >> 4) * 188 + width_in_ctb * 9 + 1023) >> 9;
      break;
   case ANV_VID_MEM_H265_METADATA_TILE_LINE:
      size = (((vid->vk.max_coded.width + 15) >> 4) * 172 + width_in_ctb * 9 + 1023) >> 9;
      break;
   case ANV_VID_MEM_H265_METADATA_TILE_COLUMN:
      size = (((vid->vk.max_coded.height + 15) >> 4) * 176 + height_in_ctb * 89 + 1023) >> 9;
      break;
   case ANV_VID_MEM_H265_SAO_LINE:
      size = align((vid->vk.max_coded.width >> 1) + width_in_ctb * 3, 16) >> bit_shift;
      break;
   case ANV_VID_MEM_H265_SAO_TILE_LINE:
      size = align((vid->vk.max_coded.width >> 1) + width_in_ctb * 6, 16) >> bit_shift;
      break;
   case ANV_VID_MEM_H265_SAO_TILE_COLUMN:
      size = align((vid->vk.max_coded.height >> 1) + height_in_ctb * 6, 16) >> bit_shift;
      break;
   case ANV_VID_MEM_H265_SSE_SRC_PIX_ROW_STORE: {
      /* Take the formula from media-driver */
#define CACHELINE_SIZE 64
#define HEVC_MIN_TILE_SIZE 128
      uint32_t max_tile_cols = DIV_ROUND_UP(vid->vk.max_coded.width, HEVC_MIN_TILE_SIZE);
      size = 2 * ((CACHELINE_SIZE * (4 + 4)) << 1) * (width_in_ctb + 3 * max_tile_cols);
      return size;
   }
   default:
      unreachable("unknown memory");
   }

   return size << 6;
}

static void
get_h264_video_session_mem_reqs(struct anv_video_session *vid,
                                VkVideoSessionMemoryRequirementsKHR *mem_reqs,
                                uint32_t *pVideoSessionMemoryRequirementsCount,
                                uint32_t memory_types)
{
   VK_OUTARRAY_MAKE_TYPED(VkVideoSessionMemoryRequirementsKHR,
                          out,
                          mem_reqs,
                          pVideoSessionMemoryRequirementsCount);

   for (unsigned i = 0; i < ANV_VID_MEM_H264_MAX; i++) {
      uint32_t bind_index = ANV_VID_MEM_H264_INTRA_ROW_STORE + i;
      uint64_t size = get_h264_video_mem_size(vid, i);

      vk_outarray_append_typed(VkVideoSessionMemoryRequirementsKHR, &out, p) {
         p->memoryBindIndex = bind_index;
         p->memoryRequirements.size = size;
         p->memoryRequirements.alignment = 4096;
         p->memoryRequirements.memoryTypeBits = memory_types;
      }
   }
}

static void
get_h265_video_session_mem_reqs(struct anv_video_session *vid,
                                VkVideoSessionMemoryRequirementsKHR *mem_reqs,
                                uint32_t *pVideoSessionMemoryRequirementsCount,
                                uint32_t memory_types)
{
   VK_OUTARRAY_MAKE_TYPED(VkVideoSessionMemoryRequirementsKHR,
                          out,
                          mem_reqs,
                          pVideoSessionMemoryRequirementsCount);

   uint32_t mem_cnt = (vid->vk.op & VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) ?
                       ANV_VID_MEM_H265_DEC_MAX : ANV_VID_MEM_H265_ENC_MAX;

   for (unsigned i = 0; i < mem_cnt; i++) {
      uint32_t bind_index =
         ANV_VID_MEM_H265_DEBLOCK_FILTER_ROW_STORE_LINE + i;
      uint64_t size = get_h265_video_mem_size(vid, i);

      vk_outarray_append_typed(VkVideoSessionMemoryRequirementsKHR, &out, p) {
         p->memoryBindIndex = bind_index;
         p->memoryRequirements.size = size;
         p->memoryRequirements.alignment = 4096;
         p->memoryRequirements.memoryTypeBits = memory_types;
      }
   }
}

static const uint8_t av1_buffer_size[ANV_VID_MEM_AV1_MAX][4] = {
   { 2 ,   4   ,   2   ,    4 },  /* bsdLineBuf, */
   { 2 ,   4   ,   2   ,    4 },  /* bsdTileLineBuf, */
   { 2 ,   4   ,   4   ,    8 },  /* intraPredLine, */
   { 2 ,   4   ,   4   ,    8 },  /* intraPredTileLine, */
   { 4 ,   8   ,   4   ,    8 },  /* spatialMvLineBuf, */
   { 4 ,   8   ,   4   ,    8 },  /* spatialMvTileLineBuf, */
   { 1 ,   1   ,   1   ,    1 },  /* lrMetaTileCol, */
   { 7 ,   7   ,   7   ,    7 },  /* lrTileLineY, */
   { 5 ,   5   ,   5   ,    5 },  /* lrTileLineU, */
   { 5 ,   5   ,   5   ,    5 },  /* lrTileLineV, */
   { 9 ,   17  ,   11  ,    21 }, /* deblockLineYBuf, */
   { 3 ,   4   ,   3   ,    5 },  /* deblockLineUBuf, */
   { 3 ,   4   ,   3   ,    5 },  /* deblockLineVBuf, */
   { 9 ,   17  ,   11  ,    21 }, /* deblockTileLineYBuf, */
   { 3 ,   4   ,   3   ,    5 },  /* deblockTileLineVBuf, */
   { 3 ,   4   ,   3   ,    5 },  /* deblockTileLineUBuf, */
   { 8 ,   16  ,   10  ,    20 }, /* deblockTileColYBuf, */
   { 2 ,   4   ,   3   ,    5 },  /* deblockTileColUBuf, */
   { 2 ,   4   ,   3   ,    5 },  /* deblockTileColVBuf, */
   { 8 ,   16  ,   10  ,    20 }, /* cdefLineBuf, */
   { 8 ,   16  ,   10  ,    20 }, /* cdefTileLineBuf, */
   { 8 ,   16  ,   10  ,    20 }, /* cdefTileColBuf, */
   { 1 ,   1   ,   1   ,    1 },  /* cdefMetaTileLine, */
   { 1 ,   1   ,   1   ,    1 },  /* cdefMetaTileCol, */
   { 1 ,   1   ,   1   ,    1 },  /* cdefTopLeftCornerBuf, */
   { 22,   44  ,   29  ,    58 }, /* superResTileColYBuf, */
   { 8 ,   16  ,   10  ,    20 }, /* superResTileColUBuf, */
   { 8 ,   16  ,   10  ,    20 }, /* superResTileColVBuf, */
   { 9 ,   17  ,   11  ,    22 }, /* lrTileColYBuf, */
   { 5 ,   9   ,   6   ,    12 }, /* lrTileColUBuf, */
   { 5 ,   9   ,   6   ,    12 }, /* lrTileColVBuf, */
};

static const uint8_t av1_buffer_size_ext[ANV_VID_MEM_AV1_MAX][4] = {
   { 0 ,    0    ,    0    ,    0 },  /* bsdLineBuf, */
   { 0 ,    0    ,    0    ,    0 },  /* bsdTileLineBuf, */
   { 0 ,    0    ,    0    ,    0 },  /* intraPredLine, */
   { 0 ,    0    ,    0    ,    0 },  /* intraPredTileLine, */
   { 0 ,    0    ,    0    ,    0 },  /* spatialMvLineBuf, */
   { 0 ,    0    ,    0    ,    0 },  /* spatialMvTileLineBuf, */
   { 1 ,    1    ,    1    ,    1 },  /* lrMetaTileCol, */
   { 0 ,    0    ,    0    ,    0 },  /* lrTileLineY, */
   { 0 ,    0    ,    0    ,    0 },  /* lrTileLineU, */
   { 0 ,    0    ,    0    ,    0 },  /* lrTileLineV, */
   { 0 ,    0    ,    0    ,    0 },  /* deblockLineYBuf, */
   { 0 ,    0    ,    0    ,    0 },  /* deblockLineUBuf, */
   { 0 ,    0    ,    0    ,    0 },  /* deblockLineVBuf, */
   { 0 ,    0    ,    0    ,    0 },  /* deblockTileLineYBuf, */
   { 0 ,    0    ,    0    ,    0 },  /* deblockTileLineVBuf, */
   { 0 ,    0    ,    0    ,    0 },  /* deblockTileLineUBuf, */
   { 0 ,    0    ,    0    ,    0 },  /* deblockTileColYBuf, */
   { 0 ,    0    ,    0    ,    0 },  /* deblockTileColUBuf, */
   { 0 ,    0    ,    0    ,    0 },  /* deblockTileColVBuf, */
   { 1 ,    1    ,    2    ,    2 },  /* cdefLineBuf, */
   { 1 ,    1    ,    2    ,    2 },  /* cdefTileLineBuf, */
   { 1 ,    1    ,    2    ,    2 },  /* cdefTileColBuf, */
   { 0 ,    0    ,    0    ,    0 },  /* cdefMetaTileLine, */
   { 1 ,    1    ,    1    ,    1 },  /* cdefMetaTileCol, */
   { 0 ,    0    ,    0    ,    0 },  /* cdefTopLeftCornerBuf, */
   { 22,    44   ,    29   ,    58 }, /* superResTileColYBuf, */
   { 8 ,    16   ,    10   ,    20 }, /* superResTileColUBuf, */
   { 8 ,    16   ,    10   ,    20 }, /* superResTileColVBuf, */
   { 2 ,    2    ,    2    ,    2 },  /* lrTileColYBuf, */
   { 1 ,    1    ,    1    ,    1 },  /* lrTileColUBuf, */
   { 1 ,    1    ,    1    ,    1 },  /* lrTileColVBuf, */
};

static void
get_av1_video_session_mem_reqs(struct anv_video_session *vid,
                               VkVideoSessionMemoryRequirementsKHR *mem_reqs,
                               uint32_t *pVideoSessionMemoryRequirementsCount,
                               uint32_t memory_types)
{
   VK_OUTARRAY_MAKE_TYPED(VkVideoSessionMemoryRequirementsKHR,
                          out,
                          mem_reqs,
                          pVideoSessionMemoryRequirementsCount);
   const uint32_t av1_mi_size_log2         = 2;
   const uint32_t av1_max_mib_size_log2    = 5;
   uint32_t width = vid->vk.max_coded.width;
   uint32_t height = vid->vk.max_coded.height;
   uint32_t mi_cols = width  >> av1_mi_size_log2;
   uint32_t mi_rows = height >> av1_mi_size_log2;
   uint32_t width_in_sb = align(mi_cols, (1 << av1_mi_size_log2)) >> av1_mi_size_log2;
   uint32_t height_in_sb = align(mi_rows, (1 << av1_mi_size_log2)) >> av1_mi_size_log2;
   uint32_t max_tile_width_sb = DIV_ROUND_UP(4096, 1 << (av1_max_mib_size_log2 + av1_mi_size_log2));
   uint32_t max_tile_cols = 16; /* TODO. get the profile to work this out */

   /* Assume 8-bit 128x128 sb is true, can't know at this point */
   int buf_size_idx = 1;

   for (enum anv_vid_mem_av1_types mem = ANV_VID_MEM_AV1_BITSTREAM_LINE_ROWSTORE;
        mem < ANV_VID_MEM_AV1_MAX; mem++) {
      VkDeviceSize buffer_size = 0;

      switch (mem) {
      case ANV_VID_MEM_AV1_BITSTREAM_LINE_ROWSTORE:
      case ANV_VID_MEM_AV1_INTRA_PREDICTION_LINE_ROWSTORE:
      case ANV_VID_MEM_AV1_SPATIAL_MOTION_VECTOR_LINE:
      case ANV_VID_MEM_AV1_DEBLOCKER_FILTER_LINE_Y:
      case ANV_VID_MEM_AV1_DEBLOCKER_FILTER_LINE_U:
      case ANV_VID_MEM_AV1_DEBLOCKER_FILTER_LINE_V:
         buffer_size = max_tile_width_sb * av1_buffer_size[mem][buf_size_idx];
         break;
      case ANV_VID_MEM_AV1_CDEF_FILTER_LINE:
         buffer_size = max_tile_width_sb * av1_buffer_size[mem][buf_size_idx] +
            av1_buffer_size_ext[mem][buf_size_idx];
         break;
      case ANV_VID_MEM_AV1_BITSTREAM_TILE_LINE_ROWSTORE:
      case ANV_VID_MEM_AV1_SPATIAL_MOTION_VECTOR_TILE_LINE:
      case ANV_VID_MEM_AV1_INTRA_PREDICTION_TILE_LINE_ROWSTORE:
      case ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_LINE_Y:
      case ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_LINE_U:
      case ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_LINE_V:
         buffer_size = width_in_sb * av1_buffer_size[mem][buf_size_idx];
         break;

      case ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_LINE_Y:
         buffer_size = max_tile_cols * 7;
         break;
      case ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_LINE_U:
      case ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_LINE_V:
         buffer_size = max_tile_cols * 5;
         break;

      case ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_COLUMN_Y:
      case ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_COLUMN_U:
      case ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_COLUMN_V:
         buffer_size = height_in_sb * av1_buffer_size[mem][buf_size_idx];
         break;
      case ANV_VID_MEM_AV1_CDEF_FILTER_TILE_LINE:
         buffer_size = width_in_sb * av1_buffer_size[mem][buf_size_idx] +
            av1_buffer_size_ext[mem][buf_size_idx];
         break;
      case ANV_VID_MEM_AV1_CDEF_FILTER_META_TILE_LINE:
         buffer_size = max_tile_cols;
         break;
      case ANV_VID_MEM_AV1_CDEF_FILTER_TOP_LEFT_CORNER:
         buffer_size = max_tile_cols * 8; /* TODO. take from profile */
         break;
      case ANV_VID_MEM_AV1_CDEF_FILTER_TILE_COLUMN:
      case ANV_VID_MEM_AV1_CDEF_FILTER_META_TILE_COLUMN:
      case ANV_VID_MEM_AV1_SUPER_RES_TILE_COLUMN_Y:
      case ANV_VID_MEM_AV1_SUPER_RES_TILE_COLUMN_U:
      case ANV_VID_MEM_AV1_SUPER_RES_TILE_COLUMN_V:
      case ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_COLUMN_Y:
      case ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_COLUMN_U:
      case ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_COLUMN_V:
      case ANV_VID_MEM_AV1_LOOP_RESTORATION_META_TILE_COLUMN:
         buffer_size = height_in_sb * av1_buffer_size[mem][buf_size_idx] +
            av1_buffer_size_ext[mem][buf_size_idx];
         break;
      case ANV_VID_MEM_AV1_CDF_DEFAULTS_0:
      case ANV_VID_MEM_AV1_CDF_DEFAULTS_1:
      case ANV_VID_MEM_AV1_CDF_DEFAULTS_2:
      case ANV_VID_MEM_AV1_CDF_DEFAULTS_3:
         buffer_size = av1_cdf_max_num_bytes;
         break;
      case ANV_VID_MEM_AV1_DBD_BUFFER:
         buffer_size = 1;
         break;
      default:
         assert(0);
         break;
      }

      switch (mem) {
      case ANV_VID_MEM_AV1_CDF_DEFAULTS_0:
      case ANV_VID_MEM_AV1_CDF_DEFAULTS_1:
      case ANV_VID_MEM_AV1_CDF_DEFAULTS_2:
      case ANV_VID_MEM_AV1_CDF_DEFAULTS_3:
         break;
      default:
         buffer_size *= 64;
         break;
      }
      vk_outarray_append_typed(VkVideoSessionMemoryRequirementsKHR, &out, p) {
         p->memoryBindIndex = mem;
         p->memoryRequirements.size = buffer_size;
         p->memoryRequirements.alignment = 4096;
         p->memoryRequirements.memoryTypeBits = memory_types;
      }
   }
}

VkResult
anv_GetVideoSessionMemoryRequirementsKHR(VkDevice _device,
                                         VkVideoSessionKHR videoSession,
                                         uint32_t *pVideoSessionMemoryRequirementsCount,
                                         VkVideoSessionMemoryRequirementsKHR *mem_reqs)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_video_session, vid, videoSession);

   uint32_t memory_types =
      (vid->vk.flags & VK_VIDEO_SESSION_CREATE_PROTECTED_CONTENT_BIT_KHR) ?
      device->physical->memory.protected_mem_types :
      device->physical->memory.default_buffer_mem_types;
   switch (vid->vk.op) {
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
      get_h264_video_session_mem_reqs(vid,
                                      mem_reqs,
                                      pVideoSessionMemoryRequirementsCount,
                                      memory_types);
      break;
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
      get_h265_video_session_mem_reqs(vid,
                                      mem_reqs,
                                      pVideoSessionMemoryRequirementsCount,
                                      memory_types);
      break;
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR:
      get_h264_video_session_mem_reqs(vid,
                                      mem_reqs,
                                      pVideoSessionMemoryRequirementsCount,
                                      memory_types);
      break;
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR:
      get_h265_video_session_mem_reqs(vid,
                                      mem_reqs,
                                      pVideoSessionMemoryRequirementsCount,
                                      memory_types);
      break;
   case VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR:
      get_av1_video_session_mem_reqs(vid,
                                     mem_reqs,
                                     pVideoSessionMemoryRequirementsCount,
                                     memory_types);
      break;
   default:
      unreachable("unknown codec");
   }

   return VK_SUCCESS;
}

VkResult
anv_UpdateVideoSessionParametersKHR(VkDevice _device,
                                     VkVideoSessionParametersKHR _params,
                                     const VkVideoSessionParametersUpdateInfoKHR *pUpdateInfo)
{
   ANV_FROM_HANDLE(anv_video_session_params, params, _params);
   return vk_video_session_parameters_update(&params->vk, pUpdateInfo);
}

static void
copy_bind(struct anv_vid_mem *dst,
          const VkBindVideoSessionMemoryInfoKHR *src)
{
   dst->mem = anv_device_memory_from_handle(src->memory);
   dst->offset = src->memoryOffset;
   dst->size = src->memorySize;
}

VkResult
anv_BindVideoSessionMemoryKHR(VkDevice _device,
                              VkVideoSessionKHR videoSession,
                              uint32_t bind_mem_count,
                              const VkBindVideoSessionMemoryInfoKHR *bind_mem)
{
   ANV_FROM_HANDLE(anv_video_session, vid, videoSession);

   switch (vid->vk.op) {
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
   case VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR:
      for (unsigned i = 0; i < bind_mem_count; i++) {
         copy_bind(&vid->vid_mem[bind_mem[i].memoryBindIndex], &bind_mem[i]);
      }
      break;
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR:
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR:
      for (unsigned i = 0; i < bind_mem_count; i++) {
         copy_bind(&vid->vid_mem[bind_mem[i].memoryBindIndex], &bind_mem[i]);
      }
      break;
   default:
      unreachable("unknown codec");
   }
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
anv_GetEncodedVideoSessionParametersKHR(VkDevice device,
                                        const VkVideoEncodeSessionParametersGetInfoKHR* pVideoSessionParametersInfo,
                                        VkVideoEncodeSessionParametersFeedbackInfoKHR* pFeedbackInfo,
                                        size_t *pDataSize,
                                        void *pData)
{
   ANV_FROM_HANDLE(anv_video_session_params, params, pVideoSessionParametersInfo->videoSessionParameters);
   size_t total_size = 0;
   size_t size_limit = 0;

   if (pData)
      size_limit = *pDataSize;

   switch (params->vk.op) {
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR: {
      const struct VkVideoEncodeH264SessionParametersGetInfoKHR *h264_get_info =
         vk_find_struct_const(pVideoSessionParametersInfo->pNext, VIDEO_ENCODE_H264_SESSION_PARAMETERS_GET_INFO_KHR);
      size_t sps_size = 0, pps_size = 0;
      if (h264_get_info->writeStdSPS) {
         for (unsigned i = 0; i < params->vk.h264_enc.h264_sps_count; i++)
            if (params->vk.h264_enc.h264_sps[i].base.seq_parameter_set_id == h264_get_info->stdSPSId)
               vk_video_encode_h264_sps(&params->vk.h264_enc.h264_sps[i].base, size_limit, &sps_size, pData);
      }
      if (h264_get_info->writeStdPPS) {
         char *data_ptr = pData ? (char *)pData + sps_size : NULL;
         for (unsigned i = 0; i < params->vk.h264_enc.h264_pps_count; i++)
            if (params->vk.h264_enc.h264_pps[i].base.pic_parameter_set_id == h264_get_info->stdPPSId) {
               vk_video_encode_h264_pps(&params->vk.h264_enc.h264_pps[i].base, false, size_limit, &pps_size, data_ptr);
            }
      }
      total_size = sps_size + pps_size;
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR: {
      const struct VkVideoEncodeH265SessionParametersGetInfoKHR *h265_get_info =
         vk_find_struct_const(pVideoSessionParametersInfo->pNext, VIDEO_ENCODE_H265_SESSION_PARAMETERS_GET_INFO_KHR);
      size_t sps_size = 0, pps_size = 0, vps_size = 0;
      if (h265_get_info->writeStdVPS) {
         for (unsigned i = 0; i < params->vk.h265_enc.h265_vps_count; i++)
            if (params->vk.h265_enc.h265_vps[i].base.vps_video_parameter_set_id == h265_get_info->stdVPSId)
               vk_video_encode_h265_vps(&params->vk.h265_enc.h265_vps[i].base, size_limit, &vps_size, pData);
      }
      if (h265_get_info->writeStdSPS) {
         char *data_ptr = pData ? (char *)pData + vps_size : NULL;
         for (unsigned i = 0; i < params->vk.h265_enc.h265_sps_count; i++)
            if (params->vk.h265_enc.h265_sps[i].base.sps_seq_parameter_set_id == h265_get_info->stdSPSId) {
               vk_video_encode_h265_sps(&params->vk.h265_enc.h265_sps[i].base, size_limit, &sps_size, data_ptr);
            }
      }
      if (h265_get_info->writeStdPPS) {
         char *data_ptr = pData ? (char *)pData + vps_size + sps_size : NULL;
         for (unsigned i = 0; i < params->vk.h265_enc.h265_pps_count; i++)
            if (params->vk.h265_enc.h265_pps[i].base.pps_seq_parameter_set_id == h265_get_info->stdPPSId) {
               params->vk.h265_enc.h265_pps[i].base.flags.cu_qp_delta_enabled_flag = 0;
               vk_video_encode_h265_pps(&params->vk.h265_enc.h265_pps[i].base, size_limit, &pps_size, data_ptr);
            }
      }
      total_size = sps_size + pps_size + vps_size;
      break;
   }
   default:
      break;
   }

   /* vk_video_encode_h26x functions support to be safe even if size_limit is not enough,
    * so we could just confirm whether pDataSize is valid afterwards.
    */
   if (pData && *pDataSize < total_size) {
      *pDataSize = 0;
      return VK_INCOMPLETE;
   }

   *pDataSize = total_size;
   return VK_SUCCESS;
}

VkResult
anv_GetPhysicalDeviceVideoEncodeQualityLevelPropertiesKHR(VkPhysicalDevice physicalDevice,
                                                          const VkPhysicalDeviceVideoEncodeQualityLevelInfoKHR* pQualityLevelInfo,
                                                          VkVideoEncodeQualityLevelPropertiesKHR* pQualityLevelProperties)
{
   /* TODO. */
   return VK_SUCCESS;
}

static void
init_single_av1_entry(const struct syntax_element_cdf_table_layout *entry, uint16_t *dst_ptr)
{
   uint16_t entry_count_per_cl = entry->entry_count_per_cl;
   uint16_t entry_count_total = entry->entry_count_total;
   uint16_t start_cl = entry->start_cl;

   const uint16_t *src = entry->init_data;
   uint16_t *dst = dst_ptr + start_cl * 32;
   uint16_t entry_count_left = entry_count_total;

   while (entry_count_left >= entry_count_per_cl) {
      memcpy(dst, src, entry_count_per_cl * sizeof(uint16_t));
      entry_count_left -= entry_count_per_cl;

      src += entry_count_per_cl;
      dst += 32;
   }

   if (entry_count_left > 0)
      memcpy(dst, src, entry_count_left * sizeof(uint16_t));
}

#define INIT_TABLE(x) do {\
   for (unsigned i = 0; i < ARRAY_SIZE((x)); i++) \
      init_single_av1_entry(&(x)[i], dst_ptr); \
   } while (0)

static void
init_all_av1_entry(uint16_t *dst_ptr, int index)
{
   INIT_TABLE(av1_cdf_intra_part1);

   switch (index) {
   case 0:
      INIT_TABLE(av1_cdf_intra_coeffs_0);
      break;
   case 1:
      INIT_TABLE(av1_cdf_intra_coeffs_1);
      break;
   case 2:
      INIT_TABLE(av1_cdf_intra_coeffs_2);
      break;
   case 3:
      INIT_TABLE(av1_cdf_intra_coeffs_3);
      break;
   default:
      unreachable("illegal av1 entry\n");
   }
   INIT_TABLE(av1_cdf_intra_part2);
   INIT_TABLE(av1_cdf_inter);
}

void
anv_init_av1_cdf_tables(struct anv_cmd_buffer *cmd,
                        struct anv_video_session *vid)
{
   void *ptr;

   for (unsigned i = 0; i < 4; i++) {
      VkResult result =
         anv_device_map_bo(cmd->device,
                           vid->vid_mem[ANV_VID_MEM_AV1_CDF_DEFAULTS_0 + i].mem->bo,
                           vid->vid_mem[ANV_VID_MEM_AV1_CDF_DEFAULTS_0 + i].offset,
                           vid->vid_mem[ANV_VID_MEM_AV1_CDF_DEFAULTS_0 + i].size,
                           NULL,
                           &ptr);

      if (result != VK_SUCCESS) {
         anv_batch_set_error(&cmd->batch, result);
         return;
      }

      init_all_av1_entry(ptr, i);
      anv_device_unmap_bo(cmd->device, vid->vid_mem[ANV_VID_MEM_AV1_CDF_DEFAULTS_0 + i].mem->bo, ptr,
                          vid->vid_mem[ANV_VID_MEM_AV1_CDF_DEFAULTS_0 + i].size, NULL);

   }
}

uint32_t
anv_video_get_image_mv_size(struct anv_device *device,
                            struct anv_image *image,
                            const struct VkVideoProfileListInfoKHR *profile_list)
{
   uint32_t size = 0;

   for (unsigned i = 0; i < profile_list->profileCount; i++) {
      if (profile_list->pProfiles[i].videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) {
         unsigned w_mb = DIV_ROUND_UP(image->vk.extent.width, ANV_MB_WIDTH);
         unsigned h_mb = DIV_ROUND_UP(image->vk.extent.height, ANV_MB_HEIGHT);
         size = w_mb * h_mb * 128;
      } else if (profile_list->pProfiles[i].videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) {
         unsigned w_mb = DIV_ROUND_UP(image->vk.extent.width, 32);
         unsigned h_mb = DIV_ROUND_UP(image->vk.extent.height, 32);
         size = ALIGN(w_mb * h_mb, 2) << 6;
      } else if (profile_list->pProfiles[i].videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR) {
         const uint32_t av1_mi_size_log2 = 2;
         uint32_t width = image->vk.extent.width;
         uint32_t height = image->vk.extent.height;
         uint32_t mi_cols = width  >> av1_mi_size_log2;
         uint32_t mi_rows = height >> av1_mi_size_log2;
         uint32_t width_in_sb = align(mi_cols, (1 << av1_mi_size_log2)) >> av1_mi_size_log2;
         uint32_t height_in_sb = align(mi_rows, (1 << av1_mi_size_log2)) >> av1_mi_size_log2;
         uint32_t sb_total = width_in_sb * height_in_sb;

         size = sb_total * 16;
      }
   }
   return size;
}
