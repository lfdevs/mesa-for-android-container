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
#include "vp9_tables.h"
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

   vk_video_session_finish(&vid->vk);
   vk_free2(&device->vk.alloc, pAllocator, vid);
}

static VkResult
video_profile_supported(struct anv_physical_device *pdevice,
                  const VkVideoProfileInfoKHR *profile)
{
   VkResult result = vk_video_is_profile_supported(profile);
   if (result != VK_SUCCESS)
      return result;

   /* ANV doesn't support different luma and chroma bit depths for all codecs. */
   if (profile->lumaBitDepth != profile->chromaBitDepth)
      return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;

   /* ANV doesn't support 12 bit for all codecs for the moment */
   if (profile->lumaBitDepth & VK_VIDEO_COMPONENT_BIT_DEPTH_12_BIT_KHR)
      return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;

   if (profile->chromaSubsampling != VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR)
      return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;

   switch (profile->videoCodecOperation) {

   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR: {
      if (profile->lumaBitDepth != VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR)
         return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR: {
      const struct VkVideoDecodeH265ProfileInfoKHR *h265_dec_profile =
         vk_find_struct_const(profile->pNext,
                              VIDEO_DECODE_H265_PROFILE_INFO_KHR);

      const struct VkVideoEncodeH265ProfileInfoKHR *h265_enc_profile =
         vk_find_struct_const(profile->pNext,
                              VIDEO_ENCODE_H265_PROFILE_INFO_KHR);

      StdVideoH265ProfileIdc profile_idc = h265_dec_profile ?
         h265_dec_profile->stdProfileIdc : h265_enc_profile->stdProfileIdc;

      /* No hardware supports the scc extension profile */
      if (profile_idc != STD_VIDEO_H265_PROFILE_IDC_MAIN &&
          profile_idc != STD_VIDEO_H265_PROFILE_IDC_MAIN_10 &&
          profile_idc != STD_VIDEO_H265_PROFILE_IDC_MAIN_STILL_PICTURE &&
          profile_idc != STD_VIDEO_H265_PROFILE_IDC_FORMAT_RANGE_EXTENSIONS)
         return VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR;

      /* Skylake only supports the main profile */
      if (profile_idc != STD_VIDEO_H265_PROFILE_IDC_MAIN &&
          profile_idc != STD_VIDEO_H265_PROFILE_IDC_MAIN_STILL_PICTURE &&
          pdevice->info.platform <= INTEL_PLATFORM_SKL)
         return VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR;

      /* Gfx10 and under don't support the range extension profile */
      if (profile_idc != STD_VIDEO_H265_PROFILE_IDC_MAIN &&
          profile_idc != STD_VIDEO_H265_PROFILE_IDC_MAIN_10 &&
          profile_idc != STD_VIDEO_H265_PROFILE_IDC_MAIN_STILL_PICTURE &&
          pdevice->info.ver <= 10)
         return VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR;

      if (profile_idc == STD_VIDEO_H265_PROFILE_IDC_MAIN ||
          profile_idc == STD_VIDEO_H265_PROFILE_IDC_MAIN_STILL_PICTURE) {
         if (profile->lumaBitDepth != VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR)
            return VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR;
      }

      break;
   }
   case VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR: {
      const struct VkVideoDecodeAV1ProfileInfoKHR *av1_profile =
         vk_find_struct_const(profile->pNext, VIDEO_DECODE_AV1_PROFILE_INFO_KHR);

      if (av1_profile->stdProfile != STD_VIDEO_AV1_PROFILE_MAIN)
         return VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR;

      /* ANV doesn't support filmgrain under Gen20 */
      if (av1_profile->filmGrainSupport && pdevice->info.ver < 20)
         return VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR;

      break;
   }
   case VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR: {
      const struct VkVideoDecodeVP9ProfileInfoKHR *vp9_profile =
         vk_find_struct_const(profile->pNext, VIDEO_DECODE_VP9_PROFILE_INFO_KHR);

      if (vp9_profile->stdProfile != STD_VIDEO_VP9_PROFILE_0 &&
          vp9_profile->stdProfile != STD_VIDEO_VP9_PROFILE_2)
         return VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR;

      /* Check the profile compatiblity with chroma/luma subsampling */
      if (vp9_profile->stdProfile == STD_VIDEO_VP9_PROFILE_0 &&
          profile->lumaBitDepth == VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR)
         return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;

      if (vp9_profile->stdProfile == STD_VIDEO_VP9_PROFILE_2 &&
          profile->lumaBitDepth == VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR)
         return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;

      break;
   }
   case VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR: {
      return VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR;
   }
   default:
      UNREACHABLE("unknown codec\n");
      break;
   }

   return VK_SUCCESS;
}

VkResult
anv_GetPhysicalDeviceVideoCapabilitiesKHR(VkPhysicalDevice physicalDevice,
                                           const VkVideoProfileInfoKHR *pVideoProfile,
                                           VkVideoCapabilitiesKHR *pCapabilities)
{
   ANV_FROM_HANDLE(anv_physical_device, pdevice, physicalDevice);

   VkResult res = video_profile_supported(pdevice, pVideoProfile);
   if (res != VK_SUCCESS)
      return res;

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

   switch (pVideoProfile->videoCodecOperation) {
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR: {
      struct VkVideoDecodeH264CapabilitiesKHR *ext = (struct VkVideoDecodeH264CapabilitiesKHR *)
         vk_find_struct(pCapabilities->pNext, VIDEO_DECODE_H264_CAPABILITIES_KHR);

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
      struct VkVideoDecodeAV1CapabilitiesKHR *ext = (struct VkVideoDecodeAV1CapabilitiesKHR *)
         vk_find_struct(pCapabilities->pNext, VIDEO_DECODE_AV1_CAPABILITIES_KHR);

      ext->maxLevel = STD_VIDEO_AV1_LEVEL_6_0;

      pCapabilities->maxDpbSlots = STD_VIDEO_AV1_NUM_REF_FRAMES + 1;
      pCapabilities->maxActiveReferencePictures = STD_VIDEO_AV1_NUM_REF_FRAMES;

      strcpy(pCapabilities->stdHeaderVersion.extensionName, VK_STD_VULKAN_VIDEO_CODEC_AV1_DECODE_EXTENSION_NAME);
      pCapabilities->stdHeaderVersion.specVersion = VK_STD_VULKAN_VIDEO_CODEC_AV1_DECODE_SPEC_VERSION;
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR: {
      struct VkVideoDecodeH265CapabilitiesKHR *ext = (struct VkVideoDecodeH265CapabilitiesKHR *)
         vk_find_struct(pCapabilities->pNext, VIDEO_DECODE_H265_CAPABILITIES_KHR);

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
   case VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR: {
      struct VkVideoDecodeVP9CapabilitiesKHR *ext = (struct VkVideoDecodeVP9CapabilitiesKHR *)
         vk_find_struct(pCapabilities->pNext, VIDEO_DECODE_VP9_CAPABILITIES_KHR);

      pCapabilities->maxDpbSlots = STD_VIDEO_VP9_NUM_REF_FRAMES + 4;
      pCapabilities->maxActiveReferencePictures = STD_VIDEO_VP9_REFS_PER_FRAME;
      pCapabilities->pictureAccessGranularity.width = 8;
      pCapabilities->pictureAccessGranularity.height = 8;
      pCapabilities->minCodedExtent.width = 8;
      pCapabilities->minCodedExtent.height = 8;

      ext->maxLevel = 4;

      strcpy(pCapabilities->stdHeaderVersion.extensionName, VK_STD_VULKAN_VIDEO_CODEC_VP9_DECODE_EXTENSION_NAME);
      pCapabilities->stdHeaderVersion.specVersion = VK_STD_VULKAN_VIDEO_CODEC_VP9_DECODE_SPEC_VERSION;
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
         ext->flags = VK_VIDEO_ENCODE_H264_CAPABILITY_HRD_COMPLIANCE_BIT_KHR |
                      VK_VIDEO_ENCODE_H264_CAPABILITY_PER_PICTURE_TYPE_MIN_MAX_QP_BIT_KHR;
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
         ext->stdSyntaxFlags = VK_VIDEO_ENCODE_H264_STD_CONSTRAINED_INTRA_PRED_FLAG_SET_BIT_KHR |
                               VK_VIDEO_ENCODE_H264_STD_ENTROPY_CODING_MODE_FLAG_UNSET_BIT_KHR |
                               VK_VIDEO_ENCODE_H264_STD_ENTROPY_CODING_MODE_FLAG_SET_BIT_KHR |
                               VK_VIDEO_ENCODE_H264_STD_DEBLOCKING_FILTER_DISABLED_BIT_KHR |
                               VK_VIDEO_ENCODE_H264_STD_DEBLOCKING_FILTER_ENABLED_BIT_KHR |
                               VK_VIDEO_ENCODE_H264_STD_DEBLOCKING_FILTER_PARTIAL_BIT_KHR |
                               VK_VIDEO_ENCODE_H264_STD_TRANSFORM_8X8_MODE_FLAG_SET_BIT_KHR |
                               VK_VIDEO_ENCODE_H264_STD_CHROMA_QP_INDEX_OFFSET_BIT_KHR |
                               VK_VIDEO_ENCODE_H264_STD_SECOND_CHROMA_QP_INDEX_OFFSET_BIT_KHR;
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
         ext->flags = VK_VIDEO_ENCODE_H265_CAPABILITY_PER_PICTURE_TYPE_MIN_MAX_QP_BIT_KHR;
         ext->maxLevelIdc = STD_VIDEO_H265_LEVEL_IDC_5_1;
         ext->ctbSizes = VK_VIDEO_ENCODE_H265_CTB_SIZE_32_BIT_KHR |
                         VK_VIDEO_ENCODE_H265_CTB_SIZE_64_BIT_KHR;
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
         ext->stdSyntaxFlags = VK_VIDEO_ENCODE_H265_STD_SAMPLE_ADAPTIVE_OFFSET_ENABLED_FLAG_SET_BIT_KHR |
                               VK_VIDEO_ENCODE_H265_STD_PCM_ENABLED_FLAG_SET_BIT_KHR |
                               VK_VIDEO_ENCODE_H265_STD_TRANSFORM_SKIP_ENABLED_FLAG_SET_BIT_KHR |
                               VK_VIDEO_ENCODE_H265_STD_CONSTRAINED_INTRA_PRED_FLAG_SET_BIT_KHR;
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
   ANV_FROM_HANDLE(anv_physical_device, pdevice, physicalDevice);

   VK_OUTARRAY_MAKE_TYPED(VkVideoFormatPropertiesKHR, out,
                          pVideoFormatProperties,
                          pVideoFormatPropertyCount);

   const struct VkVideoProfileListInfoKHR *prof_list = (struct VkVideoProfileListInfoKHR *)
      vk_find_struct_const(pVideoFormatInfo->pNext, VIDEO_PROFILE_LIST_INFO_KHR);

   VkImageUsageFlags supportedImageUsage = pVideoFormatInfo->imageUsage;
   VkImageCreateFlags supportedImageCreateFlags = 0;

   if (pVideoFormatInfo->imageUsage & VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR ||
       pVideoFormatInfo->imageUsage & VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR)
   {
      supportedImageUsage |= (VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT);

      supportedImageCreateFlags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT |
                                  VK_IMAGE_CREATE_EXTENDED_USAGE_BIT |
                                  VK_IMAGE_CREATE_VIDEO_PROFILE_INDEPENDENT_BIT_KHR;
   }

   if (prof_list) {
      for (unsigned i = 0; i < prof_list->profileCount; i++) {
         const VkVideoProfileInfoKHR *profile = &prof_list->pProfiles[i];

         /* Spec says:
          *    If any of the video profiles specified via
          *    VkVideoProfileListInfoKHR::pProfiles are not supported,
          *    then this command returns one of the video-profile-specific
          *    error codes.
          */
         VkResult res = video_profile_supported(pdevice, profile);
         if (res != VK_SUCCESS)
            return res;

         const VkImageUsageFlags dpb_output_coincide_usage =
            VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR |
            VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;

         const bool decode_dpb_only =
            (pVideoFormatInfo->imageUsage & dpb_output_coincide_usage) ==
            VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
         const bool decode_dst_only =
            (pVideoFormatInfo->imageUsage & dpb_output_coincide_usage) ==
            VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;

         /* Decoder supports only
          * VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR.
          *
          * So we need to set both usages.
          */
         if (decode_dpb_only || decode_dst_only)
            supportedImageUsage |= dpb_output_coincide_usage;

         if (profile->lumaBitDepth == VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR) {
            vk_outarray_append_typed(VkVideoFormatPropertiesKHR, &out, p) {
               p->format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
               p->imageCreateFlags = supportedImageCreateFlags;
               p->imageType = VK_IMAGE_TYPE_2D;
               p->imageTiling = VK_IMAGE_TILING_OPTIMAL;
               p->imageUsageFlags = supportedImageUsage;
            }
         }

         if (profile->lumaBitDepth == VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR) {
            vk_outarray_append_typed(VkVideoFormatPropertiesKHR, &out, p) {
               p->format = VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
               p->imageCreateFlags = supportedImageCreateFlags;
               p->imageType = VK_IMAGE_TYPE_2D;
               p->imageTiling = VK_IMAGE_TILING_OPTIMAL;
               p->imageUsageFlags = supportedImageUsage;
            }
         }
      }
   }

   if (*pVideoFormatPropertyCount == 0)
      return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;

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
      UNREACHABLE("unknown memory");
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
      UNREACHABLE("unknown memory");
   }

   return size << 6;
}

static uint64_t
get_vp9_video_mem_size(struct anv_video_session *vid, uint32_t mem_idx)
{
   uint32_t width_in_ctb =
      DIV_ROUND_UP(vid->vk.max_coded.width, ANV_MAX_VP9_CTB_SIZE);
   uint32_t height_in_ctb =
      DIV_ROUND_UP(vid->vk.max_coded.height, ANV_MAX_VP9_CTB_SIZE);
   uint64_t size;

   switch (mem_idx) {
   case ANV_VID_MEM_VP9_DEBLOCK_FILTER_ROW_STORE_LINE:
   case ANV_VID_MEM_VP9_DEBLOCK_FILTER_ROW_STORE_TILE_LINE:
      /* if profile <= 1: multiply 18, if profile > 1: multiply 36
       * But we don't know the profile here, so use 36.
       */
      size = width_in_ctb * 36;
      break;
   case ANV_VID_MEM_VP9_DEBLOCK_FILTER_ROW_STORE_TILE_COLUMN:
      size = height_in_ctb * 34;
      break;
   case ANV_VID_MEM_VP9_METADATA_LINE:
   case ANV_VID_MEM_VP9_METADATA_TILE_LINE:
      size = width_in_ctb * 5;
      break;
   case ANV_VID_MEM_VP9_METADATA_TILE_COLUMN:
      size = height_in_ctb * 5;
      break;
   case ANV_VID_MEM_VP9_PROBABILITY_0:
   case ANV_VID_MEM_VP9_PROBABILITY_1:
   case ANV_VID_MEM_VP9_PROBABILITY_2:
   case ANV_VID_MEM_VP9_PROBABILITY_3:
      size = 32;
      break;
   case ANV_VID_MEM_VP9_SEGMENT_ID:
      size = (uint64_t)width_in_ctb * height_in_ctb;
      break;
   case ANV_VID_MEM_VP9_HVD_LINE_ROW_STORE:
   case ANV_VID_MEM_VP9_HVD_TILE_ROW_STORE:
      size = width_in_ctb;
      break;
   case ANV_VID_MEM_VP9_MV_1:
   case ANV_VID_MEM_VP9_MV_2:
      size = ((uint64_t)width_in_ctb * height_in_ctb * 9);
      break;
   default:
      UNREACHABLE("unknown memory");
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

static void
get_vp9_video_session_mem_reqs(struct anv_video_session *vid,
                                VkVideoSessionMemoryRequirementsKHR *mem_reqs,
                                uint32_t *pVideoSessionMemoryRequirementsCount,
                                uint32_t memory_types)
{
   VK_OUTARRAY_MAKE_TYPED(VkVideoSessionMemoryRequirementsKHR,
                          out,
                          mem_reqs,
                          pVideoSessionMemoryRequirementsCount);

   for (unsigned i = 0; i < ANV_VID_MEM_VP9_DEC_MAX; i++) {
      uint32_t bind_index =
         ANV_VID_MEM_VP9_DEBLOCK_FILTER_ROW_STORE_LINE + i;
      uint64_t size = get_vp9_video_mem_size(vid, i);

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
   { 4 ,   8   ,   5   ,    10 }, /* lrTileColAlignBuffer, */
   { 2 ,   4   ,   3   ,    5 },  /* fgTileColBuf, */
   { 96,   96  ,   192 ,    192 },/* fgSampleTmpBuf, */
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
   { 1 ,    1    ,    1    ,    1 },  /* lrTileColAlignBuffer, */
   { 1 ,    1    ,    1    ,    1 },  /* fgTileColBuf, */
   { 0 ,    0    ,    0    ,    0 },  /* fgSampleTmpBuf, */
};

const uint32_t av1_mi_size_log2         = 2;
const uint32_t av1_max_mib_size_log2    = 5;

static void
get_av1_sb_size(uint32_t *w_in_sb, uint32_t *h_in_sb)
{
   const uint32_t width = 4096;
   const uint32_t height = 4096;

   uint32_t mi_cols = width  >> av1_mi_size_log2;
   uint32_t mi_rows = height >> av1_mi_size_log2;

   uint32_t width_in_sb = align(mi_cols, (1 << av1_mi_size_log2)) >> av1_mi_size_log2;
   uint32_t height_in_sb = align(mi_rows, (1 << av1_mi_size_log2)) >> av1_mi_size_log2;

   *w_in_sb = width_in_sb;
   *h_in_sb = height_in_sb;

   return;
}

static void
get_av1_video_session_mem_reqs(struct anv_device *dev,
                               struct anv_video_session *vid,
                               VkVideoSessionMemoryRequirementsKHR *mem_reqs,
                               uint32_t *pVideoSessionMemoryRequirementsCount,
                               uint32_t memory_types)
{
   VK_OUTARRAY_MAKE_TYPED(VkVideoSessionMemoryRequirementsKHR,
                          out,
                          mem_reqs,
                          pVideoSessionMemoryRequirementsCount);

   uint32_t width_in_sb, height_in_sb;
   get_av1_sb_size(&width_in_sb, &height_in_sb);

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
      case ANV_VID_MEM_AV1_LOOP_RESTORATION_FILTER_TILE_COLUMN_ALIGNMENT_RW:
         if (dev->info->ver < 20)
            continue;
         buffer_size = height_in_sb * av1_buffer_size[mem][buf_size_idx] +
            av1_buffer_size_ext[mem][buf_size_idx];
         break;
      case ANV_VID_MEM_AV1_FILM_GRAIN_TILE_COLUMN_RW:
         if (dev->info->ver < 20 || !vid->vk.av1.film_grain_support)
            continue;
         buffer_size = height_in_sb * av1_buffer_size[mem][buf_size_idx] +
            av1_buffer_size_ext[mem][buf_size_idx];
         break;
      case ANV_VID_MEM_AV1_FILM_GRAIN_SAMPLE_TEMPLATE:
         if (dev->info->ver < 20 || !vid->vk.av1.film_grain_support)
            continue;
         buffer_size = av1_buffer_size[mem][buf_size_idx] +
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
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR:
      get_h264_video_session_mem_reqs(vid,
                                      mem_reqs,
                                      pVideoSessionMemoryRequirementsCount,
                                      memory_types);
      break;
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR:
      get_h265_video_session_mem_reqs(vid,
                                      mem_reqs,
                                      pVideoSessionMemoryRequirementsCount,
                                      memory_types);
      break;
   case VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR:
      get_av1_video_session_mem_reqs(device,
                                     vid,
                                     mem_reqs,
                                     pVideoSessionMemoryRequirementsCount,
                                     memory_types);
      break;
   case VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR:
      get_vp9_video_session_mem_reqs(vid,
                                     mem_reqs,
                                     pVideoSessionMemoryRequirementsCount,
                                     memory_types);
      break;
   default:
      UNREACHABLE("unknown codec");
   }

   return VK_SUCCESS;
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
   case VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR:
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
      UNREACHABLE("unknown codec");
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
   VK_FROM_HANDLE(vk_video_session_parameters, params,
                  pVideoSessionParametersInfo->videoSessionParameters);
   size_t total_size = 0;
   size_t size_limit = 0;

   if (pData)
      size_limit = *pDataSize;

   switch (params->op) {
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR: {
      const struct VkVideoEncodeH264SessionParametersGetInfoKHR *h264_get_info =
         vk_find_struct_const(pVideoSessionParametersInfo->pNext, VIDEO_ENCODE_H264_SESSION_PARAMETERS_GET_INFO_KHR);
      size_t sps_size = 0, pps_size = 0;
      if (h264_get_info->writeStdSPS) {
         for (unsigned i = 0; i < params->h264_enc.h264_sps_count; i++)
            if (params->h264_enc.h264_sps[i].base.seq_parameter_set_id == h264_get_info->stdSPSId)
               vk_video_encode_h264_sps(&params->h264_enc.h264_sps[i].base, size_limit, &sps_size, pData);
      }
      if (h264_get_info->writeStdPPS) {
         char *data_ptr = pData ? (char *)pData + sps_size : NULL;
         for (unsigned i = 0; i < params->h264_enc.h264_pps_count; i++)
            if (params->h264_enc.h264_pps[i].base.pic_parameter_set_id == h264_get_info->stdPPSId) {
               vk_video_encode_h264_pps(&params->h264_enc.h264_pps[i].base,
                                        params->h264_enc.profile_idc == STD_VIDEO_H264_PROFILE_IDC_HIGH,
                                        size_limit, &pps_size, data_ptr);
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
         for (unsigned i = 0; i < params->h265_enc.h265_vps_count; i++)
            if (params->h265_enc.h265_vps[i].base.vps_video_parameter_set_id == h265_get_info->stdVPSId)
               vk_video_encode_h265_vps(&params->h265_enc.h265_vps[i].base, size_limit, &vps_size, pData);
      }
      if (h265_get_info->writeStdSPS) {
         char *data_ptr = pData ? (char *)pData + vps_size : NULL;
         for (unsigned i = 0; i < params->h265_enc.h265_sps_count; i++)
            if (params->h265_enc.h265_sps[i].base.sps_seq_parameter_set_id == h265_get_info->stdSPSId) {
               vk_video_encode_h265_sps(&params->h265_enc.h265_sps[i].base, size_limit, &sps_size, data_ptr);
            }
      }
      if (h265_get_info->writeStdPPS) {
         char *data_ptr = pData ? (char *)pData + vps_size + sps_size : NULL;
         for (unsigned i = 0; i < params->h265_enc.h265_pps_count; i++)
            if (params->h265_enc.h265_pps[i].base.pps_seq_parameter_set_id == h265_get_info->stdPPSId) {
               params->h265_enc.h265_pps[i].base.flags.cu_qp_delta_enabled_flag = 0;
               vk_video_encode_h265_pps(&params->h265_enc.h265_pps[i].base, size_limit, &pps_size, data_ptr);
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
   const VkVideoProfileInfoKHR *profile = pQualityLevelInfo->pVideoProfile;

   pQualityLevelProperties->preferredRateControlMode = VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DEFAULT_KHR;

   switch (profile->videoCodecOperation) {
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR: {
      VkVideoEncodeH264QualityLevelPropertiesKHR *ext =
         vk_find_struct(pQualityLevelProperties->pNext, VIDEO_ENCODE_H264_QUALITY_LEVEL_PROPERTIES_KHR);

      ext->preferredIdrPeriod = 60;
      ext->preferredGopFrameCount = 60;
      ext->preferredConstantQp = (VkVideoEncodeH264QpKHR) { 26, 26, 26 };
      /* For Low-Power(VDENC) mode */
      ext->preferredMaxL0ReferenceCount = 3;
      ext->preferredMaxL1ReferenceCount = 0;
      ext->preferredStdEntropyCodingModeFlag = true;
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR: {
      VkVideoEncodeH265QualityLevelPropertiesKHR *ext =
         vk_find_struct(pQualityLevelProperties->pNext, VIDEO_ENCODE_H265_QUALITY_LEVEL_PROPERTIES_KHR);
      ext->preferredIdrPeriod = 60;
      ext->preferredGopFrameCount = 60;
      /* For Low-Power(VDENC) mode */
      ext->preferredMaxL0ReferenceCount = 3;
      ext->preferredMaxL1ReferenceCount = 3;
      ext->preferredConstantQp = (VkVideoEncodeH265QpKHR) { 26, 26, 26 };
      break;
   }
   default:
      return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;
   }

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
      UNREACHABLE("illegal av1 entry\n");
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

#define VP9_CTX_DEFAULT(field) {                                \
   assert(sizeof(ctx.field) == sizeof(default_##field));        \
   memcpy(ctx.field, default_##field, sizeof(default_##field)); \
}

static void
vp9_prob_buf_update(struct anv_video_session *vid,
                    void *ptr,
                    bool key_frame,
                    const StdVideoVP9Segmentation *seg)
{
   vp9_frame_context ctx = { 0, };

   /* Reset all */
   if (BITSET_TEST(vid->prob_tbl_set, 0)) {
      ctx.tx_probs = default_tx_probs;
      VP9_CTX_DEFAULT(coef_probs_4x4);
      VP9_CTX_DEFAULT(coef_probs_8x8);
      VP9_CTX_DEFAULT(coef_probs_16x16);
      VP9_CTX_DEFAULT(coef_probs_32x32);

      VP9_CTX_DEFAULT(skip_probs);

      if (key_frame) {
         memcpy(ctx.partition_probs, vp9_kf_partition_probs,
                sizeof(vp9_kf_partition_probs));
         memcpy(ctx.uv_mode_probs, vp9_kf_uv_mode_probs,
                sizeof(vp9_kf_uv_mode_probs));
      } else {
         VP9_CTX_DEFAULT(inter_mode_probs);
         VP9_CTX_DEFAULT(switchable_interp_prob);
         VP9_CTX_DEFAULT(intra_inter_prob);
         VP9_CTX_DEFAULT(comp_inter_prob);
         VP9_CTX_DEFAULT(single_ref_prob);
         VP9_CTX_DEFAULT(comp_ref_prob);
         VP9_CTX_DEFAULT(y_mode_prob);
         VP9_CTX_DEFAULT(partition_probs);
         ctx.nmvc = default_nmv_context;
         VP9_CTX_DEFAULT(uv_mode_probs);
      }

      memcpy(ptr, &ctx, sizeof(vp9_frame_context));
   }

   /* Reset partially */
   if (BITSET_TEST(vid->prob_tbl_set, 1)) {
      if (key_frame) {
         memcpy(ctx.partition_probs, vp9_kf_partition_probs,
                sizeof(vp9_kf_partition_probs));
         memcpy(ctx.uv_mode_probs, vp9_kf_uv_mode_probs,
                sizeof(vp9_kf_uv_mode_probs));
      } else {
         VP9_CTX_DEFAULT(inter_mode_probs);
         VP9_CTX_DEFAULT(switchable_interp_prob);
         VP9_CTX_DEFAULT(intra_inter_prob);
         VP9_CTX_DEFAULT(comp_inter_prob);
         VP9_CTX_DEFAULT(single_ref_prob);
         VP9_CTX_DEFAULT(comp_ref_prob);
         VP9_CTX_DEFAULT(y_mode_prob);
         VP9_CTX_DEFAULT(partition_probs);
         ctx.nmvc = default_nmv_context;
         VP9_CTX_DEFAULT(uv_mode_probs);
      }

      memcpy(ptr + INTER_MODE_PROBS_OFFSET, &ctx.inter_mode_probs, INTER_MODE_PROBS_SIZE);
   }

   /* Copy seg probs */
   if (BITSET_TEST(vid->prob_tbl_set, 2)) {
      memcpy(ctx.seg_tree_probs, seg->segmentation_tree_probs,
             sizeof(ctx.seg_tree_probs));
      memcpy(ctx.seg_pred_probs, seg->segmentation_pred_prob,
             sizeof(ctx.seg_pred_probs));
      memcpy(ptr + SEG_PROBS_OFFSET, &ctx.seg_tree_probs,
             SEG_TREE_PROBS + PREDICTION_PROBS);
   } else if (BITSET_TEST(vid->prob_tbl_set, 3)) {
      VP9_CTX_DEFAULT(seg_tree_probs);
      VP9_CTX_DEFAULT(seg_pred_probs);
      memcpy(ptr + SEG_PROBS_OFFSET, &ctx,
             SEG_TREE_PROBS + PREDICTION_PROBS);
   }

   /* TODO for 4, 5 */
}

void
anv_update_vp9_tables(struct anv_cmd_buffer *cmd,
                      struct anv_video_session *vid,
                      uint32_t prob_id,
                      bool key_frame,
                      const StdVideoVP9Segmentation *seg)
{
   void *prob_map;

   VkResult result =
      anv_device_map_bo(cmd->device,
                        vid->vid_mem[prob_id].mem->bo,
                        vid->vid_mem[prob_id].offset,
                        vid->vid_mem[prob_id].size,
                        NULL /* placed_addr */,
                        &prob_map);

   if (result != VK_SUCCESS) {
      anv_batch_set_error(&cmd->batch, result);
      return;
   }

   vp9_prob_buf_update(vid, prob_map, key_frame, seg);

   /* Clear probability setting table */
   for (int i = 0; i < 6; i++)
      BITSET_CLEAR(vid->prob_tbl_set, i);

   anv_device_unmap_bo(cmd->device,
                       vid->vid_mem[prob_id].mem->bo,
                       prob_map,
                       vid->vid_mem[prob_id].size, false);
}

void
anv_calculate_qmul(const struct VkVideoDecodeVP9PictureInfoKHR *vp9_pic,
                   uint32_t qyac,
                   uint32_t seg_id,
                   int16_t *ptr)
{
   const StdVideoDecodeVP9PictureInfo *std_pic = vp9_pic->pStdPictureInfo;

   uint32_t bpp_index = std_pic->pColorConfig->BitDepth > 8 ? 1 : 0;

   uint32_t qydc = (qyac + std_pic->delta_q_y_dc) & 0xff;
   uint32_t quvdc = (qyac + std_pic->delta_q_uv_dc) & 0xff;
   uint32_t quvac = (qyac + std_pic->delta_q_uv_ac) & 0xff;

   int16_t qmul[2][2] = { 0, };

   qmul[0][0] = vp9_dc_qlookup[bpp_index][qydc];
   qmul[0][1] = vp9_ac_qlookup[bpp_index][qyac];
   qmul[1][0] = vp9_dc_qlookup[bpp_index][quvdc];
   qmul[1][1] = vp9_ac_qlookup[bpp_index][quvac];

   memcpy(ptr, qmul, sizeof(qmul));
}

void
anv_vp9_reset_segment_id(struct anv_cmd_buffer *cmd, struct anv_video_session *vid)
{
   void *map;

   VkResult result =
      anv_device_map_bo(cmd->device,
                        vid->vid_mem[ANV_VID_MEM_VP9_SEGMENT_ID].mem->bo,
                        vid->vid_mem[ANV_VID_MEM_VP9_SEGMENT_ID].offset,
                        vid->vid_mem[ANV_VID_MEM_VP9_SEGMENT_ID].size,
                        NULL,
                        &map);

   if (result != VK_SUCCESS) {
      anv_batch_set_error(&cmd->batch, result);
      return;
   }

   memset(map, 0, vid->vid_mem[ANV_VID_MEM_VP9_SEGMENT_ID].size);
   anv_device_unmap_bo(cmd->device,
                       vid->vid_mem[ANV_VID_MEM_VP9_SEGMENT_ID].mem->bo,
                       map,
                       vid->vid_mem[ANV_VID_MEM_VP9_SEGMENT_ID].size, NULL);
}

uint32_t
anv_video_get_image_mv_size(struct anv_device *device,
                            struct anv_image *image,
                            const struct VkVideoProfileListInfoKHR *profile_list)
{
   uint32_t size = 0;

   for (unsigned i = 0; i < profile_list->profileCount; i++) {
      if (profile_list->pProfiles[i].videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR ||
          profile_list->pProfiles[i].videoCodecOperation == VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR) {
         unsigned w_mb = DIV_ROUND_UP(image->vk.extent.width, ANV_MB_WIDTH);
         unsigned h_mb = DIV_ROUND_UP(image->vk.extent.height, ANV_MB_HEIGHT);
         size = w_mb * h_mb * 128;
      } else if (profile_list->pProfiles[i].videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR ||
                 profile_list->pProfiles[i].videoCodecOperation == VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR) {
         unsigned w_mb = DIV_ROUND_UP(image->vk.extent.width, 32);
         unsigned h_mb = DIV_ROUND_UP(image->vk.extent.height, 32);
         size = align(w_mb * h_mb, 2) << 6;
      } else if (profile_list->pProfiles[i].videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR) {
         unsigned w_ctb = DIV_ROUND_UP(image->vk.extent.width, ANV_MAX_VP9_CTB_SIZE);
         unsigned h_ctb = DIV_ROUND_UP(image->vk.extent.height, ANV_MAX_VP9_CTB_SIZE);

         size = (w_ctb * h_ctb * 9) << 6;
      } else if (profile_list->pProfiles[i].videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR) {
         uint32_t width_in_sb, height_in_sb;
         get_av1_sb_size(&width_in_sb, &height_in_sb);
         uint32_t sb_total = width_in_sb * height_in_sb;

         size = sb_total * 16;
      }
   }
   return size;
}
