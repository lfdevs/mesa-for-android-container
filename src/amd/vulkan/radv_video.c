/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 * Copyright 2021 Red Hat Inc.
 * All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _WIN32
#include "drm-uapi/amdgpu_drm.h"
#endif

#include "util/vl_zscan_data.h"
#include "ac_vcn_dec.h"

#include "radv_buffer.h"
#include "radv_cs.h"
#include "radv_debug.h"
#include "radv_device_memory.h"
#include "radv_entrypoints.h"
#include "radv_image.h"
#include "radv_image_view.h"
#include "radv_video.h"

#define RADV_VIDEO_H264_MAX_NUM_REF_FRAME 16
#define RADV_VIDEO_H265_MAX_DPB_SLOTS     17
#define RADV_VIDEO_H265_MAX_NUM_REF_FRAME 15
#define RADV_VIDEO_AV1_MAX_DPB_SLOTS      9
#define RADV_VIDEO_AV1_MAX_NUM_REF_FRAME  7
#define RADV_VIDEO_VP9_MAX_DPB_SLOTS      9
#define RADV_VIDEO_VP9_MAX_NUM_REF_FRAME  3

/* Not 100% sure this isn't too much but works */
#define VID_DEFAULT_ALIGNMENT 256

/* add a new set register command to the IB */
static void
set_reg(struct radv_cmd_buffer *cmd_buffer, unsigned reg, uint32_t val)
{
   struct radv_cmd_stream *cs = cmd_buffer->cs;

   radeon_begin(cs);
   radeon_emit(RDECODE_PKT0(reg >> 2, 0));
   radeon_emit(val);
   radeon_end();
}

bool
radv_check_vcn_fw_version(const struct radv_physical_device *pdev, uint32_t dec, uint32_t enc, uint32_t rev)
{
   return pdev->info.vcn_dec_version > dec || pdev->info.vcn_enc_minor_version > enc ||
          (pdev->info.vcn_dec_version == dec && pdev->info.vcn_enc_minor_version == enc &&
           pdev->info.vcn_fw_revision >= rev);
}

static bool
radv_enable_tier3(struct radv_physical_device *pdev, VkVideoCodecOperationFlagBitsKHR operation)
{
   if (pdev->info.vcn_ip_version < VCN_5_0_0)
      return false;

   if (operation == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR)
      return radv_check_vcn_fw_version(pdev, 9, 9, 14);

   return true;
}

static bool
radv_enable_tier2(struct radv_physical_device *pdev)
{
   if (pdev->info.vcn_ip_version >= VCN_3_0_0)
      return true;
   return false;
}

static uint32_t
radv_video_get_db_alignment(struct radv_physical_device *pdev, int width, bool is_h265_main_10_or_av1)
{
   if (pdev->info.vcn_ip_version >= VCN_2_0_0 && width > 32 && is_h265_main_10_or_av1)
      return 64;
   return 32;
}

static bool
radv_vid_buffer_upload_alloc(struct radv_cmd_buffer *cmd_buffer, unsigned size, unsigned *out_offset, void **ptr)
{
   return radv_cmd_buffer_upload_alloc_aligned(cmd_buffer, size, VID_DEFAULT_ALIGNMENT, out_offset, ptr);
}

/* vcn unified queue (sq) ib header */
void
radv_vcn_sq_header(struct radv_cmd_stream *cs, struct rvcn_sq_var *sq, unsigned type)
{
   /* vcn ib engine info */
   radeon_begin(cs);
   radeon_emit(RADEON_VCN_ENGINE_INFO_SIZE);
   radeon_emit(RADEON_VCN_ENGINE_INFO);
   radeon_emit(type);
   radeon_emit(0);
   radeon_end();

   sq->engine_ib_size_of_packages = &cs->b->buf[cs->b->cdw - 1];
}

void
radv_vcn_sq_tail(struct radv_cmd_stream *cs, struct rvcn_sq_var *sq)
{
   uint32_t *end;
   uint32_t size_in_dw;

   end = &cs->b->buf[cs->b->cdw];

   if (sq->engine_ib_size_of_packages == NULL)
      return;

   size_in_dw = end - sq->engine_ib_size_of_packages + 3; /* package_size, package_type, engine_type */
   *sq->engine_ib_size_of_packages = size_in_dw * sizeof(uint32_t);
}

void
radv_vcn_write_memory(struct radv_cmd_buffer *cmd_buffer, uint64_t va, unsigned value)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_physical_device *pdev = radv_device_physical(device);
   struct rvcn_sq_var sq;
   struct radv_cmd_stream *cs = cmd_buffer->cs;
   enum radv_video_write_memory_support support = radv_video_write_memory_supported(pdev);

   if (support == RADV_VIDEO_WRITE_MEMORY_SUPPORT_NONE)
      return;

   if (support == RADV_VIDEO_WRITE_MEMORY_SUPPORT_PCIE_ATOMICS) {
      fprintf(stderr, "radv: VCN WRITE_MEMORY requires PCIe atomics support. Expect issues "
                      "if PCIe atomics are not enabled on current device.\n");
   }

   bool separate_queue = pdev->vid_decode_ip != AMD_IP_VCN_UNIFIED;
   if (cmd_buffer->qf == RADV_QUEUE_VIDEO_DEC && separate_queue && pdev->vid_dec_reg.data2) {
      radeon_check_space(device->ws, cs->b, 8);
      set_reg(cmd_buffer, pdev->vid_dec_reg.data0, va & 0xffffffff);
      set_reg(cmd_buffer, pdev->vid_dec_reg.data1, va >> 32);
      set_reg(cmd_buffer, pdev->vid_dec_reg.data2, value);
      set_reg(cmd_buffer, pdev->vid_dec_reg.cmd, RDECODE_CMD_WRITE_MEMORY << 1);
      return;
   }

   radeon_check_space(device->ws, cs->b, 256);
   radv_vcn_sq_header(cs, &sq, RADEON_VCN_ENGINE_TYPE_COMMON);
   struct rvcn_cmn_engine_ib_package *ib_header = (struct rvcn_cmn_engine_ib_package *)&(cs->b->buf[cs->b->cdw]);
   ib_header->package_size = sizeof(struct rvcn_cmn_engine_ib_package) + sizeof(struct rvcn_cmn_engine_op_writememory);
   cs->b->cdw++;
   ib_header->package_type = RADEON_VCN_IB_COMMON_OP_WRITEMEMORY;
   cs->b->cdw++;

   struct rvcn_cmn_engine_op_writememory *write_memory =
      (struct rvcn_cmn_engine_op_writememory *)&(cs->b->buf[cs->b->cdw]);
   write_memory->dest_addr_lo = va & 0xffffffff;
   write_memory->dest_addr_hi = va >> 32;
   write_memory->data = value;

   cs->b->cdw += sizeof(*write_memory) / 4;
   radv_vcn_sq_tail(cs, &sq);
}

void
radv_init_physical_device_decoder(struct radv_physical_device *pdev)
{
   if (pdev->info.vcn_ip_version >= VCN_4_0_0)
      pdev->vid_decode_ip = AMD_IP_VCN_UNIFIED;
   else if (radv_has_uvd(pdev))
      pdev->vid_decode_ip = AMD_IP_UVD;
   else
      pdev->vid_decode_ip = AMD_IP_VCN_DEC;

   switch (pdev->info.vcn_ip_version) {
   case VCN_1_0_0:
   case VCN_1_0_1:
      pdev->vid_dec_reg.data0 = RDECODE_VCN1_GPCOM_VCPU_DATA0;
      pdev->vid_dec_reg.data1 = RDECODE_VCN1_GPCOM_VCPU_DATA1;
      pdev->vid_dec_reg.cmd = RDECODE_VCN1_GPCOM_VCPU_CMD;
      pdev->vid_dec_reg.cntl = RDECODE_VCN1_ENGINE_CNTL;
      break;
   case VCN_2_0_0:
   case VCN_2_0_2:
   case VCN_2_0_3:
   case VCN_2_2_0:
      pdev->vid_dec_reg.data0 = RDECODE_VCN2_GPCOM_VCPU_DATA0;
      pdev->vid_dec_reg.data1 = RDECODE_VCN2_GPCOM_VCPU_DATA1;
      pdev->vid_dec_reg.data2 = RDECODE_VCN2_GPCOM_VCPU_DATA2;
      pdev->vid_dec_reg.cmd = RDECODE_VCN2_GPCOM_VCPU_CMD;
      pdev->vid_dec_reg.cntl = RDECODE_VCN2_ENGINE_CNTL;
      break;
   case VCN_2_5_0:
   case VCN_2_6_0:
   case VCN_3_0_0:
   case VCN_3_0_2:
   case VCN_3_0_16:
   case VCN_3_0_33:
   case VCN_3_1_1:
   case VCN_3_1_2:
      pdev->vid_dec_reg.data0 = RDECODE_VCN2_5_GPCOM_VCPU_DATA0;
      pdev->vid_dec_reg.data1 = RDECODE_VCN2_5_GPCOM_VCPU_DATA1;
      pdev->vid_dec_reg.data2 = RDECODE_VCN2_5_GPCOM_VCPU_DATA2;
      pdev->vid_dec_reg.cmd = RDECODE_VCN2_5_GPCOM_VCPU_CMD;
      pdev->vid_dec_reg.cntl = RDECODE_VCN2_5_ENGINE_CNTL;
      break;
   default:
      break;
   }
}

void
radv_probe_video_decode(struct radv_physical_device *pdev)
{
   const struct radv_instance *instance = radv_physical_device_instance(pdev);

   pdev->video_decode_enabled = false;

   if (instance->debug_flags & RADV_DEBUG_NO_VIDEO)
      return;

   /* WRITE_MEMORY is needed for SetEvent and is required to pass CTS */
   pdev->video_decode_enabled = radv_video_write_memory_supported(pdev);

   if (instance->experimental_flags & RADV_EXPERIMENTAL_VIDEO_DECODE) {
      pdev->video_decode_enabled = true;
   }
}

static void
radv_video_patch_session_parameters(struct radv_device *device, struct vk_video_session_parameters *params)
{
   switch (params->op) {
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
   default:
      return;
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR:
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR:
   case VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR:
      radv_video_patch_encode_session_parameters(device, params);
      break;
   }
}

static VkResult
create_intra_only_dpb(struct radv_device *dev, struct radv_physical_device *pdev, struct radv_video_session *vid,
                      const VkVideoProfileInfoKHR *profile, const VkAllocationCallbacks *alloc)
{
   VkVideoProfileListInfoKHR profile_list = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR,
      .profileCount = 1,
      .pProfiles = profile,
   };

   VkPhysicalDeviceVideoFormatInfoKHR format_info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR,
      .pNext = &profile_list,
      .imageUsage = vid->encode ? VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR : VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR,
   };

   VkVideoFormatPropertiesKHR format_props = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR,
   };
   uint32_t format_props_count = 1;

   VkResult result = radv_GetPhysicalDeviceVideoFormatPropertiesKHR(radv_physical_device_to_handle(pdev), &format_info,
                                                                    &format_props_count, &format_props);
   if (result != VK_SUCCESS)
      return result;

   VkImageCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = &profile_list,
      .flags = format_props.imageCreateFlags,
      .imageType = format_props.imageType,
      .format = format_props.format,
      .extent.width = vid->vk.max_coded.width,
      .extent.height = vid->vk.max_coded.height,
      .extent.depth = 1,
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = format_props.imageTiling,
      .usage = format_props.imageUsageFlags,
   };

   VkImage image;
   result = radv_image_create(radv_device_to_handle(dev), &(struct radv_image_create_info){.vk_info = &create_info},
                              alloc, &image, true);
   if (result != VK_SUCCESS)
      return result;

   vid->intra_only_dpb = radv_image_from_handle(image);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateVideoSessionKHR(VkDevice _device, const VkVideoSessionCreateInfoKHR *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator, VkVideoSessionKHR *pVideoSession)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);

   struct radv_video_session *vid =
      vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*vid), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!vid)
      return vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   memset(vid, 0, sizeof(struct radv_video_session));

   VkResult result = vk_video_session_init(&device->vk, &vid->vk, pCreateInfo);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, vid);
      return result;
   }

   enum ac_video_codec codec;
   bool need_intra_only_dpb = vid->vk.max_dpb_slots == 0;

   switch (vid->vk.op) {
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
      codec = AC_VIDEO_CODEC_AVC;
      break;
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
      codec = AC_VIDEO_CODEC_HEVC;
      break;
   case VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR:
      codec = AC_VIDEO_CODEC_AV1;
      need_intra_only_dpb &= vid->vk.av1.film_grain_support;
      break;
   case VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR:
      codec = AC_VIDEO_CODEC_VP9;
      break;
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR:
      vid->encode = true;
      vid->enc_standard = RENCODE_ENCODE_STANDARD_H264;
      break;
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR:
      vid->encode = true;
      vid->enc_standard = RENCODE_ENCODE_STANDARD_HEVC;
      break;
   case VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR:
      vid->encode = true;
      vid->enc_standard = RENCODE_ENCODE_STANDARD_AV1;
      if (pdev->info.vcn_ip_version == VCN_4_0_2 || pdev->info.vcn_ip_version == VCN_4_0_5 ||
          pdev->info.vcn_ip_version == VCN_4_0_6) {
         vid->enc_wa_flags = 1;
      }
      break;
   default:
      return VK_ERROR_FEATURE_NOT_PRESENT;
   }

   if (!vid->encode) {
      struct ac_video_dec_session_param session_param = {
         .codec = codec,
         .max_width = vid->vk.max_coded.width,
         .max_height = vid->vk.max_coded.height,
         .max_num_ref = vid->vk.max_dpb_slots,
      };

      switch (vid->vk.chroma_subsampling) {
      case VK_VIDEO_CHROMA_SUBSAMPLING_MONOCHROME_BIT_KHR:
         session_param.sub_sample = AC_VIDEO_SUBSAMPLE_400;
         break;
      case VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR:
         session_param.sub_sample = AC_VIDEO_SUBSAMPLE_420;
         break;
      case VK_VIDEO_CHROMA_SUBSAMPLING_422_BIT_KHR:
         session_param.sub_sample = AC_VIDEO_SUBSAMPLE_422;
         break;
      case VK_VIDEO_CHROMA_SUBSAMPLING_444_BIT_KHR:
         session_param.sub_sample = AC_VIDEO_SUBSAMPLE_444;
         break;
      default:
         UNREACHABLE("Invalid chroma subsampling");
      }

      switch (vid->vk.chroma_bit_depth) {
      case VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR:
         session_param.max_bit_depth = 8;
         break;
      case VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR:
         session_param.max_bit_depth = 10;
         break;
      case VK_VIDEO_COMPONENT_BIT_DEPTH_12_BIT_KHR:
         session_param.max_bit_depth = 12;
         break;
      default:
         UNREACHABLE("Invalid chroma bit depth");
      }

      vid->dec = ac_create_video_decoder(&pdev->info, &session_param);
      if (!vid->dec)
         return VK_ERROR_INITIALIZATION_FAILED;
   }

   if (need_intra_only_dpb) {
      result = create_intra_only_dpb(device, pdev, vid, pCreateInfo->pVideoProfile, pAllocator);
      if (result != VK_SUCCESS) {
         vk_free2(&device->vk.alloc, pAllocator, vid);
         return result;
      }
   }

   *pVideoSession = radv_video_session_to_handle(vid);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyVideoSessionKHR(VkDevice _device, VkVideoSessionKHR _session, const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(radv_video_session, vid, _session);
   if (!_session)
      return;

   radv_DestroyImage(_device, radv_image_to_handle(vid->intra_only_dpb), pAllocator);

   if (vid->dec)
      vid->dec->destroy(vid->dec);

   vk_video_session_finish(&vid->vk);
   vk_free2(&device->vk.alloc, pAllocator, vid);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateVideoSessionParametersKHR(VkDevice _device, const VkVideoSessionParametersCreateInfoKHR *pCreateInfo,
                                     const VkAllocationCallbacks *pAllocator,
                                     VkVideoSessionParametersKHR *pVideoSessionParameters)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);

   struct vk_video_session_parameters *params =
      vk_video_session_parameters_create(&device->vk, pCreateInfo, pAllocator, sizeof(*params));
   if (!params)
      return vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   radv_video_patch_session_parameters(device, params);

   *pVideoSessionParameters = vk_video_session_parameters_to_handle(params);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyVideoSessionParametersKHR(VkDevice _device, VkVideoSessionParametersKHR _params,
                                      const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(vk_video_session_parameters, params, _params);

   vk_video_session_parameters_destroy(&device->vk, pAllocator, params);
}

static VkResult
radv_video_is_profile_supported(struct radv_physical_device *pdev, const VkVideoProfileInfoKHR *pVideoProfile)
{
   VkResult result = vk_video_is_profile_supported(pVideoProfile);
   if (result != VK_SUCCESS)
      return result;

   switch (pVideoProfile->videoCodecOperation) {
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR: {
      const struct VkVideoDecodeH264ProfileInfoKHR *h264_profile =
         vk_find_struct_const(pVideoProfile->pNext, VIDEO_DECODE_H264_PROFILE_INFO_KHR);

      if (h264_profile->stdProfileIdc != STD_VIDEO_H264_PROFILE_IDC_BASELINE &&
          h264_profile->stdProfileIdc != STD_VIDEO_H264_PROFILE_IDC_MAIN &&
          h264_profile->stdProfileIdc != STD_VIDEO_H264_PROFILE_IDC_HIGH)
         return VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR;

      if (h264_profile->pictureLayout == VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_INTERLACED_SEPARATE_PLANES_BIT_KHR)
         return VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR;

      break;
   }
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR: {
      const bool have_10bit = pdev->info.family >= CHIP_STONEY;
      const struct VkVideoDecodeH265ProfileInfoKHR *h265_profile =
         vk_find_struct_const(pVideoProfile->pNext, VIDEO_DECODE_H265_PROFILE_INFO_KHR);

      if (h265_profile->stdProfileIdc != STD_VIDEO_H265_PROFILE_IDC_MAIN &&
          (!have_10bit || h265_profile->stdProfileIdc != STD_VIDEO_H265_PROFILE_IDC_MAIN_10) &&
          h265_profile->stdProfileIdc != STD_VIDEO_H265_PROFILE_IDC_MAIN_STILL_PICTURE)
         return VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR;

      if (pVideoProfile->lumaBitDepth != pVideoProfile->chromaBitDepth)
         return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;

      break;
   }
   case VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR: {
      const bool have_12bit = pdev->info.vcn_ip_version >= VCN_5_0_0 || pdev->info.vcn_ip_version == VCN_4_0_0;
      const struct VkVideoDecodeAV1ProfileInfoKHR *av1_profile =
         vk_find_struct_const(pVideoProfile->pNext, VIDEO_DECODE_AV1_PROFILE_INFO_KHR);

      if (av1_profile->stdProfile != STD_VIDEO_AV1_PROFILE_MAIN &&
          (!have_12bit || av1_profile->stdProfile != STD_VIDEO_AV1_PROFILE_PROFESSIONAL))
         return VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR;

      if (pVideoProfile->chromaSubsampling != VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR &&
          pVideoProfile->chromaSubsampling != VK_VIDEO_CHROMA_SUBSAMPLING_MONOCHROME_BIT_KHR)
         return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;

      break;
   }
   case VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR: {
      const struct VkVideoDecodeVP9ProfileInfoKHR *vp9_profile =
         vk_find_struct_const(pVideoProfile->pNext, VIDEO_DECODE_VP9_PROFILE_INFO_KHR);

      if (vp9_profile->stdProfile != STD_VIDEO_VP9_PROFILE_0 && vp9_profile->stdProfile != STD_VIDEO_VP9_PROFILE_2)
         return VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR;

      if (pVideoProfile->lumaBitDepth == VK_VIDEO_COMPONENT_BIT_DEPTH_12_BIT_KHR)
         return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;

      break;
   }
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR: {
      const struct VkVideoEncodeH264ProfileInfoKHR *h264_profile =
         vk_find_struct_const(pVideoProfile->pNext, VIDEO_ENCODE_H264_PROFILE_INFO_KHR);

      if (h264_profile->stdProfileIdc != STD_VIDEO_H264_PROFILE_IDC_BASELINE &&
          h264_profile->stdProfileIdc != STD_VIDEO_H264_PROFILE_IDC_MAIN &&
          h264_profile->stdProfileIdc != STD_VIDEO_H264_PROFILE_IDC_HIGH)
         return VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR;

      if (pVideoProfile->chromaSubsampling != VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR)
         return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;

      break;
   }
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR: {
      const bool have_10bit = pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_2;
      const struct VkVideoEncodeH265ProfileInfoKHR *h265_profile =
         vk_find_struct_const(pVideoProfile->pNext, VIDEO_ENCODE_H265_PROFILE_INFO_KHR);

      if (h265_profile->stdProfileIdc != STD_VIDEO_H265_PROFILE_IDC_MAIN &&
          (!have_10bit || h265_profile->stdProfileIdc != STD_VIDEO_H265_PROFILE_IDC_MAIN_10) &&
          h265_profile->stdProfileIdc != STD_VIDEO_H265_PROFILE_IDC_MAIN_STILL_PICTURE)
         return VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR;

      if (pVideoProfile->chromaSubsampling != VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR)
         return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;

      if (pVideoProfile->lumaBitDepth != pVideoProfile->chromaBitDepth)
         return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;

      break;
   }
   case VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR: {
      const struct VkVideoEncodeAV1ProfileInfoKHR *av1_profile =
         vk_find_struct_const(pVideoProfile->pNext, VIDEO_ENCODE_AV1_PROFILE_INFO_KHR);

      if (av1_profile->stdProfile != STD_VIDEO_AV1_PROFILE_MAIN)
         return VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR;

      if (pVideoProfile->chromaSubsampling != VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR)
         return VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR;

      break;
   }
   default:
      return VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR;
   }

   return VK_SUCCESS;
}

uint32_t
radv_video_get_qp_map_texel_size(VkVideoCodecOperationFlagBitsKHR codec)
{
   switch (codec) {
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR:
      return 16;
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR:
   case VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR:
      return 64;
   default:
      UNREACHABLE("Unsupported video codec operation");
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetPhysicalDeviceVideoCapabilitiesKHR(VkPhysicalDevice physicalDevice, const VkVideoProfileInfoKHR *pVideoProfile,
                                           VkVideoCapabilitiesKHR *pCapabilities)
{
   VK_FROM_HANDLE(radv_physical_device, pdev, physicalDevice);
   const struct video_codec_cap *cap = NULL;
   bool is_encode = false;

   VkResult res = radv_video_is_profile_supported(pdev, pVideoProfile);
   if (res != VK_SUCCESS)
      return res;

   switch (pVideoProfile->videoCodecOperation) {
#ifndef _WIN32
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
      cap = &pdev->info.dec_caps.codec_info[AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC];
      break;
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
      cap = &pdev->info.dec_caps.codec_info[AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC];
      break;
   case VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR:
      cap = &pdev->info.dec_caps.codec_info[AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_AV1];
      break;
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR:
      cap = &pdev->info.enc_caps.codec_info[AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_MPEG4_AVC];
      is_encode = true;
      break;
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR:
      cap = &pdev->info.enc_caps.codec_info[AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_HEVC];
      is_encode = true;
      break;
   case VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR:
      cap = &pdev->info.dec_caps.codec_info[AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_VP9];
      break;
   case VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR:
      cap = &pdev->info.enc_caps.codec_info[AMDGPU_INFO_VIDEO_CAPS_CODEC_IDX_AV1];
      is_encode = true;
      break;
#endif
   default:
      UNREACHABLE("unsupported operation");
   }

   if (cap && !cap->valid)
      cap = NULL;

   if (cap) {
      pCapabilities->maxCodedExtent.width = cap->max_width;
      pCapabilities->maxCodedExtent.height = cap->max_height;
   } else {
      switch (pVideoProfile->videoCodecOperation) {
      case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
         pCapabilities->maxCodedExtent.width = (pdev->info.family < CHIP_TONGA) ? 2048 : 4096;
         pCapabilities->maxCodedExtent.height = (pdev->info.family < CHIP_TONGA) ? 1152 : 4096;
         break;
      case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
         pCapabilities->maxCodedExtent.width =
            (pdev->info.family < CHIP_RENOIR) ? ((pdev->info.family < CHIP_TONGA) ? 2048 : 4096) : 8192;
         pCapabilities->maxCodedExtent.height =
            (pdev->info.family < CHIP_RENOIR) ? ((pdev->info.family < CHIP_TONGA) ? 1152 : 4096) : 4352;
         break;
      case VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR:
         pCapabilities->maxCodedExtent.width =
            (pdev->info.family < CHIP_RENOIR) ? ((pdev->info.family < CHIP_TONGA) ? 2048 : 4096) : 8192;
         pCapabilities->maxCodedExtent.height =
            (pdev->info.family < CHIP_RENOIR) ? ((pdev->info.family < CHIP_TONGA) ? 1152 : 4096) : 4352;
         break;
      default:
         break;
      }
   }

   pCapabilities->flags = 0;
   pCapabilities->pictureAccessGranularity.width = VK_VIDEO_H264_MACROBLOCK_WIDTH;
   pCapabilities->pictureAccessGranularity.height = VK_VIDEO_H264_MACROBLOCK_HEIGHT;
   pCapabilities->minCodedExtent.width = 64;
   pCapabilities->minCodedExtent.height = 64;

   struct VkVideoDecodeCapabilitiesKHR *dec_caps = NULL;
   struct VkVideoEncodeCapabilitiesKHR *enc_caps = NULL;
   if (!is_encode) {
      dec_caps =
         (struct VkVideoDecodeCapabilitiesKHR *)vk_find_struct(pCapabilities->pNext, VIDEO_DECODE_CAPABILITIES_KHR);
      if (dec_caps) {
         dec_caps->flags = VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_DISTINCT_BIT_KHR;
         if (radv_enable_tier3(pdev, pVideoProfile->videoCodecOperation))
            dec_caps->flags |= VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR;
      }
      pCapabilities->minBitstreamBufferOffsetAlignment = 128;
      pCapabilities->minBitstreamBufferSizeAlignment = 128;
   } else {
      enc_caps = vk_find_struct(pCapabilities->pNext, VIDEO_ENCODE_CAPABILITIES_KHR);
      struct VkVideoEncodeIntraRefreshCapabilitiesKHR *intra_refresh_caps =
         vk_find_struct(pCapabilities->pNext, VIDEO_ENCODE_INTRA_REFRESH_CAPABILITIES_KHR);
      struct VkVideoEncodeQuantizationMapCapabilitiesKHR *qp_map_caps =
         vk_find_struct(pCapabilities->pNext, VIDEO_ENCODE_QUANTIZATION_MAP_CAPABILITIES_KHR);
      struct VkVideoEncodeRgbConversionCapabilitiesVALVE *rgb_caps =
         vk_find_struct(pCapabilities->pNext, VIDEO_ENCODE_RGB_CONVERSION_CAPABILITIES_VALVE);

      if (enc_caps) {
         enc_caps->flags = 0;
         enc_caps->rateControlModes = VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR |
                                      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_CBR_BIT_KHR |
                                      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_VBR_BIT_KHR;
         enc_caps->maxRateControlLayers = RADV_ENC_MAX_RATE_LAYER;
         enc_caps->maxBitrate = 1000000000;
         enc_caps->maxQualityLevels = pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_4 ? 4 : 3;
         enc_caps->encodeInputPictureGranularity = pCapabilities->pictureAccessGranularity;
         enc_caps->supportedEncodeFeedbackFlags = VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BUFFER_OFFSET_BIT_KHR |
                                                  VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BYTES_WRITTEN_BIT_KHR;

         if (radv_video_encode_qp_map_supported(pdev))
            enc_caps->flags |= VK_VIDEO_ENCODE_CAPABILITY_QUANTIZATION_DELTA_MAP_BIT_KHR;
      }
      if (intra_refresh_caps) {
         intra_refresh_caps->intraRefreshModes = VK_VIDEO_ENCODE_INTRA_REFRESH_MODE_BLOCK_BASED_BIT_KHR |
                                                 VK_VIDEO_ENCODE_INTRA_REFRESH_MODE_BLOCK_ROW_BASED_BIT_KHR |
                                                 VK_VIDEO_ENCODE_INTRA_REFRESH_MODE_BLOCK_COLUMN_BASED_BIT_KHR;
         intra_refresh_caps->maxIntraRefreshCycleDuration = 256;
         intra_refresh_caps->maxIntraRefreshActiveReferencePictures = 1;
         intra_refresh_caps->partitionIndependentIntraRefreshRegions = true;
         intra_refresh_caps->nonRectangularIntraRefreshRegions = false;
      }
      if (qp_map_caps) {
         const uint32_t qp_map_texel_size = radv_video_get_qp_map_texel_size(pVideoProfile->videoCodecOperation);
         qp_map_caps->maxQuantizationMapExtent.width = pCapabilities->maxCodedExtent.width / qp_map_texel_size;
         qp_map_caps->maxQuantizationMapExtent.height = pCapabilities->maxCodedExtent.height / qp_map_texel_size;
      }
      if (rgb_caps) {
         rgb_caps->rgbModels = VK_VIDEO_ENCODE_RGB_MODEL_CONVERSION_YCBCR_709_BIT_VALVE |
                               VK_VIDEO_ENCODE_RGB_MODEL_CONVERSION_YCBCR_2020_BIT_VALVE;
         rgb_caps->rgbRanges = VK_VIDEO_ENCODE_RGB_RANGE_COMPRESSION_FULL_RANGE_BIT_VALVE |
                               VK_VIDEO_ENCODE_RGB_RANGE_COMPRESSION_NARROW_RANGE_BIT_VALVE;
         rgb_caps->xChromaOffsets = VK_VIDEO_ENCODE_RGB_CHROMA_OFFSET_COSITED_EVEN_BIT_VALVE;
         rgb_caps->yChromaOffsets = VK_VIDEO_ENCODE_RGB_CHROMA_OFFSET_MIDPOINT_BIT_VALVE |
                                    VK_VIDEO_ENCODE_RGB_CHROMA_OFFSET_COSITED_EVEN_BIT_VALVE;
      }
      pCapabilities->minBitstreamBufferOffsetAlignment = 256;
      pCapabilities->minBitstreamBufferSizeAlignment = 8;
      if (pdev->info.vcn_ip_version >= VCN_5_0_0)
         pCapabilities->flags |= VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR;
   }

   switch (pVideoProfile->videoCodecOperation) {
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR: {
      struct VkVideoDecodeH264CapabilitiesKHR *ext =
         vk_find_struct(pCapabilities->pNext, VIDEO_DECODE_H264_CAPABILITIES_KHR);

      pCapabilities->maxDpbSlots = RADV_VIDEO_H264_MAX_DPB_SLOTS;
      pCapabilities->maxActiveReferencePictures = RADV_VIDEO_H264_MAX_NUM_REF_FRAME;

      /* for h264 on navi21+ separate dpb images should work */
      if (radv_enable_tier2(pdev))
         pCapabilities->flags |= VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR;
      ext->fieldOffsetGranularity.x = 0;
      ext->fieldOffsetGranularity.y = 0;
      ext->maxLevelIdc = STD_VIDEO_H264_LEVEL_IDC_5_1;
      strcpy(pCapabilities->stdHeaderVersion.extensionName, VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME);
      pCapabilities->stdHeaderVersion.specVersion = VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION;
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR: {
      struct VkVideoDecodeH265CapabilitiesKHR *ext =
         vk_find_struct(pCapabilities->pNext, VIDEO_DECODE_H265_CAPABILITIES_KHR);

      pCapabilities->maxDpbSlots = RADV_VIDEO_H265_MAX_DPB_SLOTS;
      pCapabilities->maxActiveReferencePictures = RADV_VIDEO_H265_MAX_NUM_REF_FRAME;
      /* for h265 on navi21+ separate dpb images should work */
      if (radv_enable_tier2(pdev))
         pCapabilities->flags |= VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR;
      ext->maxLevelIdc = STD_VIDEO_H265_LEVEL_IDC_5_1;
      strcpy(pCapabilities->stdHeaderVersion.extensionName, VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_EXTENSION_NAME);
      pCapabilities->stdHeaderVersion.specVersion = VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_SPEC_VERSION;
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR: {
      struct VkVideoDecodeAV1CapabilitiesKHR *ext =
         vk_find_struct(pCapabilities->pNext, VIDEO_DECODE_AV1_CAPABILITIES_KHR);

      const struct VkVideoDecodeAV1ProfileInfoKHR *av1_profile =
         vk_find_struct_const(pVideoProfile->pNext, VIDEO_DECODE_AV1_PROFILE_INFO_KHR);

      if (av1_profile->stdProfile == STD_VIDEO_AV1_PROFILE_PROFESSIONAL)
         dec_caps->flags &= ~VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR;

      pCapabilities->maxDpbSlots = RADV_VIDEO_AV1_MAX_DPB_SLOTS;
      pCapabilities->maxActiveReferencePictures = RADV_VIDEO_AV1_MAX_NUM_REF_FRAME;
      pCapabilities->flags |= VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR;

      ext->maxLevel = STD_VIDEO_AV1_LEVEL_6_1; /* For VCN3/4, the only h/w currently with AV1 decode support */
      strcpy(pCapabilities->stdHeaderVersion.extensionName, VK_STD_VULKAN_VIDEO_CODEC_AV1_DECODE_EXTENSION_NAME);
      pCapabilities->stdHeaderVersion.specVersion = VK_STD_VULKAN_VIDEO_CODEC_AV1_DECODE_SPEC_VERSION;
      pCapabilities->minCodedExtent.width = 16;
      pCapabilities->minCodedExtent.height = 16;
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR: {
      struct VkVideoDecodeVP9CapabilitiesKHR *ext =
         vk_find_struct(pCapabilities->pNext, VIDEO_DECODE_VP9_CAPABILITIES_KHR);

      pCapabilities->maxDpbSlots = RADV_VIDEO_VP9_MAX_DPB_SLOTS;
      pCapabilities->maxActiveReferencePictures = RADV_VIDEO_VP9_MAX_NUM_REF_FRAME;
      if (pdev->info.vcn_ip_version >= VCN_3_0_0)
         pCapabilities->flags |= VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR;
      ext->maxLevel = STD_VIDEO_VP9_LEVEL_6_2;
      strcpy(pCapabilities->stdHeaderVersion.extensionName, VK_STD_VULKAN_VIDEO_CODEC_VP9_DECODE_EXTENSION_NAME);
      pCapabilities->stdHeaderVersion.specVersion = VK_STD_VULKAN_VIDEO_CODEC_VP9_DECODE_SPEC_VERSION;
      pCapabilities->minCodedExtent.width = 16;
      pCapabilities->minCodedExtent.height = 16;
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR: {
      struct VkVideoEncodeH264CapabilitiesKHR *ext =
         vk_find_struct(pCapabilities->pNext, VIDEO_ENCODE_H264_CAPABILITIES_KHR);
      struct VkVideoEncodeH264QuantizationMapCapabilitiesKHR *qp_map_caps =
         vk_find_struct(pCapabilities->pNext, VIDEO_ENCODE_H264_QUANTIZATION_MAP_CAPABILITIES_KHR);

      ext->flags = VK_VIDEO_ENCODE_H264_CAPABILITY_HRD_COMPLIANCE_BIT_KHR |
                   VK_VIDEO_ENCODE_H264_CAPABILITY_PER_PICTURE_TYPE_MIN_MAX_QP_BIT_KHR |
                   VK_VIDEO_ENCODE_H264_CAPABILITY_ROW_UNALIGNED_SLICE_BIT_KHR;
      if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_3)
         ext->flags |= VK_VIDEO_ENCODE_H264_CAPABILITY_B_FRAME_IN_L0_LIST_BIT_KHR;

      ext->maxLevelIdc = cap ? cap->max_level : 0;
      ext->maxSliceCount = 128;
      ext->maxPPictureL0ReferenceCount = pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_3 ? 2 : 1;
      ext->maxBPictureL0ReferenceCount = pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_3 ? 1 : 0;
      ext->maxL1ReferenceCount = pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_3 ? 1 : 0;
      ext->maxTemporalLayerCount = 4;
      ext->expectDyadicTemporalLayerPattern = false;
      ext->minQp = 0;
      ext->maxQp = 51;
      ext->prefersGopRemainingFrames = false;
      ext->requiresGopRemainingFrames = false;
      ext->stdSyntaxFlags = VK_VIDEO_ENCODE_H264_STD_CONSTRAINED_INTRA_PRED_FLAG_SET_BIT_KHR |
                            VK_VIDEO_ENCODE_H264_STD_ENTROPY_CODING_MODE_FLAG_UNSET_BIT_KHR |
                            VK_VIDEO_ENCODE_H264_STD_ENTROPY_CODING_MODE_FLAG_SET_BIT_KHR;
      if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_3)
         ext->stdSyntaxFlags |= VK_VIDEO_ENCODE_H264_STD_WEIGHTED_BIPRED_IDC_IMPLICIT_BIT_KHR;
      if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5)
         ext->stdSyntaxFlags |= VK_VIDEO_ENCODE_H264_STD_TRANSFORM_8X8_MODE_FLAG_SET_BIT_KHR;

      strcpy(pCapabilities->stdHeaderVersion.extensionName, VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_EXTENSION_NAME);
      pCapabilities->stdHeaderVersion.specVersion = VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_SPEC_VERSION;
      pCapabilities->maxDpbSlots = RADV_VIDEO_H264_MAX_DPB_SLOTS;
      pCapabilities->maxActiveReferencePictures =
         MAX2(ext->maxPPictureL0ReferenceCount, ext->maxBPictureL0ReferenceCount + ext->maxL1ReferenceCount);
      pCapabilities->minCodedExtent.width = pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5 ? 96 : 128;
      pCapabilities->minCodedExtent.height = pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5 ? 32 : 128;

      if (qp_map_caps) {
         qp_map_caps->minQpDelta = -51;
         qp_map_caps->maxQpDelta = 51;
      }
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR: {
      struct VkVideoEncodeH265CapabilitiesKHR *ext =
         vk_find_struct(pCapabilities->pNext, VIDEO_ENCODE_H265_CAPABILITIES_KHR);
      struct VkVideoEncodeH265QuantizationMapCapabilitiesKHR *qp_map_caps =
         vk_find_struct(pCapabilities->pNext, VIDEO_ENCODE_H265_QUANTIZATION_MAP_CAPABILITIES_KHR);

      pCapabilities->pictureAccessGranularity.width = VK_VIDEO_H265_CTU_MAX_WIDTH;
      if (enc_caps) {
         enc_caps->encodeInputPictureGranularity = pCapabilities->pictureAccessGranularity;
         /* VCN1 can't enable rate control modes or qp maps due to missing cu_qp_delta FW interface. */
         if (pdev->enc_hw_ver == RADV_VIDEO_ENC_HW_1_2) {
            enc_caps->rateControlModes = VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR;
            enc_caps->flags &= ~VK_VIDEO_ENCODE_CAPABILITY_QUANTIZATION_DELTA_MAP_BIT_KHR;
         }
      }

      ext->flags = VK_VIDEO_ENCODE_H265_CAPABILITY_HRD_COMPLIANCE_BIT_KHR |
                   VK_VIDEO_ENCODE_H265_CAPABILITY_PER_PICTURE_TYPE_MIN_MAX_QP_BIT_KHR |
                   VK_VIDEO_ENCODE_H265_CAPABILITY_ROW_UNALIGNED_SLICE_SEGMENT_BIT_KHR;
      ext->maxLevelIdc = cap ? cap->max_level : 0;
      ext->maxSliceSegmentCount = 128;
      ext->maxTiles.width = 1;
      ext->maxTiles.height = 1;
      ext->ctbSizes = VK_VIDEO_ENCODE_H265_CTB_SIZE_64_BIT_KHR;
      ext->transformBlockSizes =
         VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_4_BIT_KHR | VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_8_BIT_KHR |
         VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_16_BIT_KHR | VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_32_BIT_KHR;
      ext->maxPPictureL0ReferenceCount = 1;
      ext->maxBPictureL0ReferenceCount = 0;
      ext->maxL1ReferenceCount = 0;
      ext->maxSubLayerCount = 4;
      ext->expectDyadicTemporalSubLayerPattern = false;
      ext->minQp = 0;
      ext->maxQp = 51;
      ext->prefersGopRemainingFrames = false;
      ext->requiresGopRemainingFrames = false;
      ext->stdSyntaxFlags = VK_VIDEO_ENCODE_H265_STD_CONSTRAINED_INTRA_PRED_FLAG_SET_BIT_KHR |
                            VK_VIDEO_ENCODE_H265_STD_DEBLOCKING_FILTER_OVERRIDE_ENABLED_FLAG_SET_BIT_KHR |
                            VK_VIDEO_ENCODE_H265_STD_CONSTRAINED_INTRA_PRED_FLAG_SET_BIT_KHR |
                            VK_VIDEO_ENCODE_H265_STD_ENTROPY_CODING_SYNC_ENABLED_FLAG_SET_BIT_KHR;

      if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_2)
         ext->stdSyntaxFlags |= VK_VIDEO_ENCODE_H265_STD_SAMPLE_ADAPTIVE_OFFSET_ENABLED_FLAG_SET_BIT_KHR;

      if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_3)
         ext->stdSyntaxFlags |= VK_VIDEO_ENCODE_H265_STD_TRANSFORM_SKIP_ENABLED_FLAG_SET_BIT_KHR |
                                VK_VIDEO_ENCODE_H265_STD_TRANSFORM_SKIP_ENABLED_FLAG_UNSET_BIT_KHR;
      strcpy(pCapabilities->stdHeaderVersion.extensionName, VK_STD_VULKAN_VIDEO_CODEC_H265_ENCODE_EXTENSION_NAME);
      pCapabilities->stdHeaderVersion.specVersion = VK_STD_VULKAN_VIDEO_CODEC_H265_ENCODE_SPEC_VERSION;
      pCapabilities->maxDpbSlots = RADV_VIDEO_H265_MAX_DPB_SLOTS;
      pCapabilities->maxActiveReferencePictures =
         MAX2(ext->maxPPictureL0ReferenceCount, ext->maxBPictureL0ReferenceCount + ext->maxL1ReferenceCount);
      pCapabilities->minCodedExtent.width = pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5 ? 384 : 130;
      pCapabilities->minCodedExtent.height = 128;

      if (qp_map_caps) {
         qp_map_caps->minQpDelta = -51;
         qp_map_caps->maxQpDelta = 51;
      }
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR: {
      struct VkVideoEncodeAV1CapabilitiesKHR *ext =
         vk_find_struct(pCapabilities->pNext, VIDEO_ENCODE_AV1_CAPABILITIES_KHR);
      struct VkVideoEncodeAV1QuantizationMapCapabilitiesKHR *qp_map_caps =
         vk_find_struct(pCapabilities->pNext, VIDEO_ENCODE_AV1_QUANTIZATION_MAP_CAPABILITIES_KHR);

      pCapabilities->maxDpbSlots = RADV_VIDEO_AV1_MAX_DPB_SLOTS;
      pCapabilities->maxActiveReferencePictures = RADV_VIDEO_AV1_MAX_NUM_REF_FRAME;
      strcpy(pCapabilities->stdHeaderVersion.extensionName, VK_STD_VULKAN_VIDEO_CODEC_AV1_ENCODE_EXTENSION_NAME);
      pCapabilities->stdHeaderVersion.specVersion = VK_STD_VULKAN_VIDEO_CODEC_AV1_ENCODE_SPEC_VERSION;
      ext->flags = VK_VIDEO_ENCODE_AV1_CAPABILITY_PER_RATE_CONTROL_GROUP_MIN_MAX_Q_INDEX_BIT_KHR |
                   VK_VIDEO_ENCODE_AV1_CAPABILITY_GENERATE_OBU_EXTENSION_HEADER_BIT_KHR |
                   VK_VIDEO_ENCODE_AV1_CAPABILITY_FRAME_SIZE_OVERRIDE_BIT_KHR;
      ext->maxLevel = STD_VIDEO_AV1_LEVEL_6_0;
      ext->codedPictureAlignment.width = pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5 ? 8 : 64;
      ext->codedPictureAlignment.height = pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5 ? 2 : 16;
      pCapabilities->pictureAccessGranularity = ext->codedPictureAlignment;
      if (enc_caps)
         enc_caps->encodeInputPictureGranularity = pCapabilities->pictureAccessGranularity;
      ext->maxTiles.width = 2;
      ext->maxTiles.height = 16;
      ext->minTileSize.width = pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5 ? 256 : 128;
      ext->minTileSize.height = 64;
      ext->maxTileSize.width = 4096;
      ext->maxTileSize.height = 4096;
      ext->superblockSizes = VK_VIDEO_ENCODE_AV1_SUPERBLOCK_SIZE_64_BIT_KHR;
      ext->maxSingleReferenceCount = 1;
      ext->singleReferenceNameMask =
         (1 << (STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME - STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME));
      if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5) {
         ext->maxUnidirectionalCompoundReferenceCount = 2;
         ext->maxUnidirectionalCompoundGroup1ReferenceCount = 2;
         ext->unidirectionalCompoundReferenceNameMask =
            (1 << (STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME - STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME)) |
            (1 << (STD_VIDEO_AV1_REFERENCE_NAME_GOLDEN_FRAME - STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME));
         ext->maxBidirectionalCompoundReferenceCount = 2;
         ext->maxBidirectionalCompoundGroup1ReferenceCount = 1;
         ext->maxBidirectionalCompoundGroup2ReferenceCount = 1;
         ext->bidirectionalCompoundReferenceNameMask =
            (1 << (STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME - STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME)) |
            (1 << (STD_VIDEO_AV1_REFERENCE_NAME_ALTREF_FRAME - STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME));
      } else {
         ext->maxUnidirectionalCompoundReferenceCount = 0;
         ext->maxUnidirectionalCompoundGroup1ReferenceCount = 0;
         ext->unidirectionalCompoundReferenceNameMask = 0;
         ext->maxBidirectionalCompoundReferenceCount = 0;
         ext->maxBidirectionalCompoundGroup1ReferenceCount = 0;
         ext->maxBidirectionalCompoundGroup2ReferenceCount = 0;
         ext->bidirectionalCompoundReferenceNameMask = 0;
      }
      ext->maxTemporalLayerCount = 4;
      ext->maxSpatialLayerCount = 1;
      ext->maxOperatingPoints = 4;
      ext->minQIndex = 1;
      ext->maxQIndex = 255;
      ext->prefersGopRemainingFrames = false;
      ext->requiresGopRemainingFrames = false;
      ext->stdSyntaxFlags = 0;
      if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5) {
         ext->stdSyntaxFlags |=
            VK_VIDEO_ENCODE_AV1_STD_SKIP_MODE_PRESENT_UNSET_BIT_KHR | VK_VIDEO_ENCODE_AV1_STD_DELTA_Q_BIT_KHR;
      }
      pCapabilities->minCodedExtent.width = pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5 ? 320 : 128;
      pCapabilities->minCodedExtent.height = 128;

      if (qp_map_caps) {
         qp_map_caps->minQIndexDelta = -255;
         qp_map_caps->maxQIndexDelta = 255;
      }
      break;
   }
   default:
      break;
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetPhysicalDeviceVideoFormatPropertiesKHR(VkPhysicalDevice physicalDevice,
                                               const VkPhysicalDeviceVideoFormatInfoKHR *pVideoFormatInfo,
                                               uint32_t *pVideoFormatPropertyCount,
                                               VkVideoFormatPropertiesKHR *pVideoFormatProperties)
{
   VK_FROM_HANDLE(radv_physical_device, pdev, physicalDevice);

   if ((pVideoFormatInfo->imageUsage &
        (VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR | VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR |
         VK_IMAGE_USAGE_VIDEO_ENCODE_QUANTIZATION_DELTA_MAP_BIT_KHR)) &&
       !pdev->video_encode_enabled)
      return VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR;

   /* Cannot be a QP map and other video enc/dec usages, as they are totally different formats. */
   if ((pVideoFormatInfo->imageUsage & VK_IMAGE_USAGE_VIDEO_ENCODE_QUANTIZATION_DELTA_MAP_BIT_KHR) &&
       (pVideoFormatInfo->imageUsage &
        (VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR | VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR |
         VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR | VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR)))
      return VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR;

   /* VCN < 5 requires separate allocates for DPB and decode video. */
   if (pdev->info.vcn_ip_version < VCN_5_0_0 &&
       (pVideoFormatInfo->imageUsage &
        (VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR | VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR)) ==
          (VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR | VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR))
      return VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR;

   VkFormat formats[2] = {VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED};
   uint32_t qp_map_texel_size = 0;
   const struct VkVideoProfileListInfoKHR *prof_list =
      vk_find_struct_const(pVideoFormatInfo->pNext, VIDEO_PROFILE_LIST_INFO_KHR);
   if (prof_list) {
      for (unsigned i = 0; i < prof_list->profileCount; i++) {
         const VkVideoProfileInfoKHR *profile = &prof_list->pProfiles[i];
         const VkVideoEncodeProfileRgbConversionInfoVALVE *rgbProfile =
            vk_find_struct_const(profile, VIDEO_ENCODE_PROFILE_RGB_CONVERSION_INFO_VALVE);
         bool rgb = rgbProfile && rgbProfile->performEncodeRgbConversion &&
                    (pVideoFormatInfo->imageUsage & VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR);

         /* "If any of the video profiles specified via VkVideoProfileListInfoKHR::pProfiles are not
          * supported, then this command returns one of the video-profile-specific error codes."
          */
         VkResult res = radv_video_is_profile_supported(pdev, profile);
         if (res != VK_SUCCESS)
            return res;

         VkFormat profile_formats[2] = {VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED};
         if (pVideoFormatInfo->imageUsage & VK_IMAGE_USAGE_VIDEO_ENCODE_QUANTIZATION_DELTA_MAP_BIT_KHR) {
            const uint32_t profile_qp_map_texel_size = radv_video_get_qp_map_texel_size(profile->videoCodecOperation);

            /* All profiles must share the same qp_map texel size. */
            if (qp_map_texel_size != 0 && qp_map_texel_size != profile_qp_map_texel_size)
               return VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR;

            qp_map_texel_size = profile_qp_map_texel_size;

            profile_formats[0] = pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5 ? VK_FORMAT_R16_SINT : VK_FORMAT_R32_SINT;
         } else if (rgb) {
            if (profile->lumaBitDepth == VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR) {
               profile_formats[0] = VK_FORMAT_B8G8R8A8_UNORM;
               profile_formats[1] = VK_FORMAT_R8G8B8A8_UNORM;
            } else if (profile->lumaBitDepth == VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR) {
               profile_formats[0] = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
               profile_formats[1] = VK_FORMAT_A2R10G10B10_UNORM_PACK32;
            }
         } else {
            if (profile->lumaBitDepth == VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR)
               profile_formats[0] = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
            else if (profile->lumaBitDepth == VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR)
               profile_formats[0] = VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
            else if (profile->lumaBitDepth == VK_VIDEO_COMPONENT_BIT_DEPTH_12_BIT_KHR)
               profile_formats[0] = VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16;
         }

         for (int j = 0; j < 2; j++) {
            /* All profiles must share the same format. */
            if (formats[j] != VK_FORMAT_UNDEFINED && formats[j] != profile_formats[j])
               return VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR;

            formats[j] = profile_formats[j];
         }
      }
   } else {
      /* On AMD, we need a codec specified for qp map as the extents differ. */
      if (pVideoFormatInfo->imageUsage & VK_IMAGE_USAGE_VIDEO_ENCODE_QUANTIZATION_DELTA_MAP_BIT_KHR)
         return VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR;

      formats[0] = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
   }

   if (formats[0] == VK_FORMAT_UNDEFINED)
      return VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR;

   const bool qp_map = pVideoFormatInfo->imageUsage & VK_IMAGE_USAGE_VIDEO_ENCODE_QUANTIZATION_DELTA_MAP_BIT_KHR;
   const bool dpb = pVideoFormatInfo->imageUsage &
                    (VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR | VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR);
   const bool src_dst = pVideoFormatInfo->imageUsage &
                        (VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR | VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR);
   VkImageTiling tiling[3];
   uint32_t num_tiling = 0;

   tiling[num_tiling++] = VK_IMAGE_TILING_OPTIMAL;

   if ((src_dst || qp_map) && !dpb)
      tiling[num_tiling++] = VK_IMAGE_TILING_LINEAR;

   if ((src_dst || qp_map) && pdev->info.gfx_level >= GFX9)
      tiling[num_tiling++] = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;

   VkImageUsageFlags usage_flags = pVideoFormatInfo->imageUsage;

   if (usage_flags & VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR)
      usage_flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

   if (usage_flags & VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR)
      usage_flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

   VK_OUTARRAY_MAKE_TYPED(VkVideoFormatPropertiesKHR, out, pVideoFormatProperties, pVideoFormatPropertyCount);

   for (uint32_t i = 0; i < 2; i++) {
      VkFormat format = formats[i];
      if (format == VK_FORMAT_UNDEFINED)
         break;
      for (uint32_t j = 0; j < num_tiling; j++) {
         vk_outarray_append_typed(VkVideoFormatPropertiesKHR, &out, p)
         {
            p->format = format;
            p->componentMapping.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            p->componentMapping.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            p->componentMapping.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            p->componentMapping.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            p->imageCreateFlags = 0;
            if (src_dst || qp_map)
               p->imageCreateFlags |=
                  VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT | VK_IMAGE_CREATE_ALIAS_BIT;
            p->imageType = VK_IMAGE_TYPE_2D;
            p->imageTiling = tiling[j];
            p->imageUsageFlags = usage_flags;

            if (qp_map) {
               struct VkVideoFormatQuantizationMapPropertiesKHR *qp_map_props =
                  vk_find_struct(p->pNext, VIDEO_FORMAT_QUANTIZATION_MAP_PROPERTIES_KHR);
               struct VkVideoFormatH265QuantizationMapPropertiesKHR *qp_map_h265_props =
                  vk_find_struct(p->pNext, VIDEO_FORMAT_H265_QUANTIZATION_MAP_PROPERTIES_KHR);
               struct VkVideoFormatAV1QuantizationMapPropertiesKHR *qp_map_av1_props =
                  vk_find_struct(p->pNext, VIDEO_FORMAT_AV1_QUANTIZATION_MAP_PROPERTIES_KHR);

               if (qp_map_props)
                  qp_map_props->quantizationMapTexelSize = (VkExtent2D){qp_map_texel_size, qp_map_texel_size};
               if (qp_map_h265_props)
                  qp_map_h265_props->compatibleCtbSizes = VK_VIDEO_ENCODE_H265_CTB_SIZE_64_BIT_KHR;
               if (qp_map_av1_props)
                  qp_map_av1_props->compatibleSuperblockSizes = VK_VIDEO_ENCODE_AV1_SUPERBLOCK_SIZE_64_BIT_KHR;
            }
         }
      }
   }

   return vk_outarray_status(&out);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetVideoSessionMemoryRequirementsKHR(VkDevice _device, VkVideoSessionKHR videoSession,
                                          uint32_t *pMemoryRequirementsCount,
                                          VkVideoSessionMemoryRequirementsKHR *pMemoryRequirements)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(radv_video_session, vid, videoSession);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   uint32_t memory_type_bits = (1u << pdev->memory_properties.memoryTypeCount) - 1;
   uint32_t memory_type_bits_visible = 0;

   /* These buffers are only mapped once during session reset, for performance reasons
    * we should prefer visible VRAM when available.
    */
   for (unsigned i = 0; i < pdev->memory_properties.memoryTypeCount; i++) {
      VkMemoryPropertyFlags flags = pdev->memory_properties.memoryTypes[i].propertyFlags;

      if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
          (flags & (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT)))
         memory_type_bits_visible |= (1 << i);
   }

   if (vid->encode) {
      return radv_video_get_encode_session_memory_requirements(device, vid, pMemoryRequirementsCount,
                                                               pMemoryRequirements);
   }
   VK_OUTARRAY_MAKE_TYPED(VkVideoSessionMemoryRequirementsKHR, out, pMemoryRequirements, pMemoryRequirementsCount);

   if (vid->dec->session_size) {
      vk_outarray_append_typed(VkVideoSessionMemoryRequirementsKHR, &out, m)
      {
         m->memoryBindIndex = RADV_BIND_SESSION_CTX;
         m->memoryRequirements.size = vid->dec->session_size;
         m->memoryRequirements.alignment = 0;
         m->memoryRequirements.memoryTypeBits =
            vid->dec->init_session_buf ? memory_type_bits_visible : memory_type_bits;
      }
   }

   if (vid->intra_only_dpb) {
      vk_outarray_append_typed(VkVideoSessionMemoryRequirementsKHR, &out, m)
      {
         m->memoryBindIndex = RADV_BIND_INTRA_ONLY;
         m->memoryRequirements.size = vid->intra_only_dpb->size,
         m->memoryRequirements.alignment = vid->intra_only_dpb->alignment,
         m->memoryRequirements.memoryTypeBits = memory_type_bits;
      }
   }
   return vk_outarray_status(&out);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_UpdateVideoSessionParametersKHR(VkDevice _device, VkVideoSessionParametersKHR videoSessionParameters,
                                     const VkVideoSessionParametersUpdateInfoKHR *pUpdateInfo)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(vk_video_session_parameters, params, videoSessionParameters);

   VkResult result = vk_video_session_parameters_update(params, pUpdateInfo);
   if (result != VK_SUCCESS)
      return result;
   radv_video_patch_session_parameters(device, params);
   return result;
}

static void
copy_bind(struct radv_vid_mem *dst, const VkBindVideoSessionMemoryInfoKHR *src)
{
   dst->mem = radv_device_memory_from_handle(src->memory);
   dst->offset = src->memoryOffset;
   dst->size = src->memorySize;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_BindVideoSessionMemoryKHR(VkDevice _device, VkVideoSessionKHR videoSession, uint32_t videoSessionBindMemoryCount,
                               const VkBindVideoSessionMemoryInfoKHR *pBindSessionMemoryInfos)
{
   VK_FROM_HANDLE(radv_video_session, vid, videoSession);
   struct radv_device *device = radv_device_from_handle(_device);

   for (unsigned i = 0; i < videoSessionBindMemoryCount; i++) {
      switch (pBindSessionMemoryInfos[i].memoryBindIndex) {
      case RADV_BIND_SESSION_CTX:
         copy_bind(&vid->sessionctx, &pBindSessionMemoryInfos[i]);
         if (vid->dec && vid->dec->init_session_buf) {
            uint8_t *ptr = radv_buffer_map(device->ws, vid->sessionctx.mem->bo);
            ptr += vid->sessionctx.offset;
            vid->dec->init_session_buf(vid->dec, ptr);
            device->ws->buffer_unmap(device->ws, vid->sessionctx.mem->bo, false);
         }
         break;
      case RADV_BIND_ENCODE_CTX:
         copy_bind(&vid->ctx, &pBindSessionMemoryInfos[i]);
         break;
      case RADV_BIND_ENCODE_AV1_CDF_STORE:
         copy_bind(&vid->default_cdf, &pBindSessionMemoryInfos[i]);
         if (vid->encode)
            radv_video_enc_init_cdf(device, vid);
         break;
      case RADV_BIND_INTRA_ONLY: {
         VkBindImageMemoryInfo bind_image = {
            .sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
            .image = radv_image_to_handle(vid->intra_only_dpb),
            .memory = pBindSessionMemoryInfos[i].memory,
            .memoryOffset = pBindSessionMemoryInfos[i].memoryOffset,
         };
         VkResult result = radv_BindImageMemory2(_device, 1, &bind_image);
         if (result != VK_SUCCESS)
            return result;
         break;
      }
      case RADV_BIND_ENCODE_QP_MAP:
         copy_bind(&vid->qp_map, &pBindSessionMemoryInfos[i]);
         break;
      default:
         assert(0);
         break;
      }
   }
   return VK_SUCCESS;
}

static const uint8_t h264_levels[] = {10, 11, 12, 13, 20, 21, 22, 30, 31, 32, 40, 41, 42, 50, 51, 52, 60, 61, 62};
static uint8_t
get_h264_level(StdVideoH264LevelIdc level)
{
   assert(level <= STD_VIDEO_H264_LEVEL_IDC_6_2);
   return h264_levels[level];
}

static void
update_h264_scaling(unsigned char scaling_list_4x4[6][16], unsigned char scaling_list_8x8[2][64],
                    const StdVideoH264ScalingLists *scaling_lists)
{
   for (int i = 0; i < STD_VIDEO_H264_SCALING_LIST_4X4_NUM_LISTS; i++) {
      for (int j = 0; j < STD_VIDEO_H264_SCALING_LIST_4X4_NUM_ELEMENTS; j++)
         scaling_list_4x4[i][vl_zscan_normal_16[j]] = scaling_lists->ScalingList4x4[i][j];
   }

   for (int i = 0; i < 2; i++) {
      for (int j = 0; j < STD_VIDEO_H264_SCALING_LIST_8X8_NUM_ELEMENTS; j++)
         scaling_list_8x8[i][vl_zscan_normal[j]] = scaling_lists->ScalingList8x8[i][j];
   }
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdBeginVideoCodingKHR(VkCommandBuffer commandBuffer, const VkVideoBeginCodingInfoKHR *pBeginInfo)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(radv_video_session, vid, pBeginInfo->videoSession);
   VK_FROM_HANDLE(vk_video_session_parameters, params, pBeginInfo->videoSessionParameters);

   cmd_buffer->video.vid = vid;
   cmd_buffer->video.params = params;

   if (cmd_buffer->video.vid->encode)
      radv_video_enc_begin_video_coding(cmd_buffer, pBeginInfo);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdControlVideoCodingKHR(VkCommandBuffer commandBuffer, const VkVideoCodingControlInfoKHR *pCodingControlInfo)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_video_session *vid = cmd_buffer->video.vid;
   struct radv_cmd_stream *cs = cmd_buffer->cs;

   if (cmd_buffer->video.vid->encode) {
      radv_video_enc_control_video_coding(cmd_buffer, pCodingControlInfo);
      return;
   }

   radeon_check_space(device->ws, cs->b, vid->dec->max_create_cmd_dw);

   struct ac_video_dec_create_cmd cmd = {
      .cmd_buffer = cs->b->buf + cs->b->cdw,
   };

   if (vid->sessionctx.mem) {
      cmd.session_va = radv_buffer_get_va(vid->sessionctx.mem->bo) + vid->sessionctx.offset;
      radv_cs_add_buffer(device->ws, cs->b, vid->sessionctx.mem->bo);
   }

   if (vid->dec->embedded_size) {
      uint32_t offset;
      radv_vid_buffer_upload_alloc(cmd_buffer, vid->dec->embedded_size, &offset, &cmd.embedded_ptr);
      cmd.embedded_va = radv_buffer_get_va(cmd_buffer->upload.upload_bo) + offset;
   }

   int ret = vid->dec->build_create_cmd(vid->dec, &cmd);
   cs->b->cdw += cmd.out.cmd_dw;
   assert(ret == 0);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdEndVideoCodingKHR(VkCommandBuffer commandBuffer, const VkVideoEndCodingInfoKHR *pEndCodingInfo)
{
}

static void
get_h264_param(struct radv_video_session *vid, struct vk_video_session_parameters *params,
               const struct VkVideoDecodeInfoKHR *frame_info, struct ac_video_dec_decode_cmd *cmd)
{
   struct ac_video_dec_avc *avc = &cmd->codec_param.avc;
   const struct VkVideoDecodeH264PictureInfoKHR *h264_pic_info =
      vk_find_struct_const(frame_info->pNext, VIDEO_DECODE_H264_PICTURE_INFO_KHR);
   const StdVideoH264SequenceParameterSet *sps;
   const StdVideoH264PictureParameterSet *pps;

   vk_video_get_h264_parameters(&vid->vk, params, frame_info, h264_pic_info, &sps, &pps);

   cmd->bitstream_va += h264_pic_info->pSliceOffsets[0];

   avc->sps_flags.direct_8x8_inference_flag = sps->flags.direct_8x8_inference_flag;
   avc->sps_flags.frame_mbs_only_flag = sps->flags.frame_mbs_only_flag;
   avc->sps_flags.delta_pic_order_always_zero_flag = sps->flags.delta_pic_order_always_zero_flag;
   avc->sps_flags.gaps_in_frame_num_value_allowed_flag = sps->flags.gaps_in_frame_num_value_allowed_flag;

   avc->pps_flags.transform_8x8_mode_flag = pps->flags.transform_8x8_mode_flag;
   avc->pps_flags.constrained_intra_pred_flag = pps->flags.constrained_intra_pred_flag;
   avc->pps_flags.deblocking_filter_control_present_flag = pps->flags.deblocking_filter_control_present_flag;
   avc->pps_flags.weighted_bipred_idc = pps->weighted_bipred_idc;
   avc->pps_flags.redundant_pic_cnt_present_flag = pps->flags.redundant_pic_cnt_present_flag;
   avc->pps_flags.constrained_intra_pred_flag = pps->flags.constrained_intra_pred_flag;
   avc->pps_flags.deblocking_filter_control_present_flag = pps->flags.deblocking_filter_control_present_flag;
   avc->pps_flags.weighted_pred_flag = pps->flags.weighted_pred_flag;
   avc->pps_flags.bottom_field_pic_order_in_frame_present_flag =
      pps->flags.bottom_field_pic_order_in_frame_present_flag;
   avc->pps_flags.entropy_coding_mode_flag = pps->flags.entropy_coding_mode_flag;

   avc->pic_flags.mbaff_frame_flag = sps->flags.mb_adaptive_frame_field_flag;
   avc->pic_flags.chroma_format_idc = sps->chroma_format_idc;

   avc->profile_idc = sps->profile_idc;
   avc->level_idc = get_h264_level(sps->level_idc);
   avc->pic_width_in_mbs_minus1 = sps->pic_width_in_mbs_minus1;
   avc->pic_height_in_mbs_minus1 = sps->pic_height_in_map_units_minus1;
   avc->bit_depth_luma_minus8 = sps->bit_depth_luma_minus8;
   avc->bit_depth_chroma_minus8 = sps->bit_depth_chroma_minus8;
   avc->log2_max_frame_num_minus4 = sps->log2_max_frame_num_minus4;
   avc->pic_order_cnt_type = sps->pic_order_cnt_type;
   avc->log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_pic_order_cnt_lsb_minus4;

   avc->pic_init_qp_minus26 = pps->pic_init_qp_minus26;
   avc->chroma_qp_index_offset = pps->chroma_qp_index_offset;
   avc->second_chroma_qp_index_offset = pps->second_chroma_qp_index_offset;

   StdVideoH264ScalingLists scaling_lists;
   vk_video_derive_h264_scaling_list(sps, pps, &scaling_lists);
   update_h264_scaling(avc->scaling_list_4x4, avc->scaling_list_8x8, &scaling_lists);

   avc->num_ref_idx_l0_default_active_minus1 = pps->num_ref_idx_l0_default_active_minus1;
   avc->num_ref_idx_l1_default_active_minus1 = pps->num_ref_idx_l1_default_active_minus1;

   avc->curr_field_order_cnt[0] = h264_pic_info->pStdPictureInfo->PicOrderCnt[0];
   avc->curr_field_order_cnt[1] = h264_pic_info->pStdPictureInfo->PicOrderCnt[1];

   avc->frame_num = h264_pic_info->pStdPictureInfo->frame_num;

   avc->max_num_ref_frames = sps->max_num_ref_frames;

   avc->curr_pic_id = frame_info->pSetupReferenceSlot ? frame_info->pSetupReferenceSlot->slotIndex : 0;

   for (unsigned i = 0; i < H264_MAX_NUM_REF_PICS; i++)
      avc->ref_frame_id_list[i] = 0xff;

   for (unsigned i = 0; i < frame_info->referenceSlotCount; i++) {
      const struct VkVideoDecodeH264DpbSlotInfoKHR *dpb_slot =
         vk_find_struct_const(frame_info->pReferenceSlots[i].pNext, VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR);

      avc->frame_num_list[i] = dpb_slot->pStdReferenceInfo->FrameNum;
      avc->field_order_cnt_list[i][0] = dpb_slot->pStdReferenceInfo->PicOrderCnt[0];
      avc->field_order_cnt_list[i][1] = dpb_slot->pStdReferenceInfo->PicOrderCnt[1];
      avc->ref_frame_id_list[i] = frame_info->pReferenceSlots[i].slotIndex;

      if (dpb_slot->pStdReferenceInfo->flags.top_field_flag)
         avc->used_for_reference_flags |= (1 << (2 * i));
      if (dpb_slot->pStdReferenceInfo->flags.bottom_field_flag)
         avc->used_for_reference_flags |= (1 << (2 * i + 1));
      if (!dpb_slot->pStdReferenceInfo->flags.top_field_flag && !dpb_slot->pStdReferenceInfo->flags.bottom_field_flag)
         avc->used_for_reference_flags |= (3 << (2 * i));
      if (dpb_slot->pStdReferenceInfo->flags.used_for_long_term_reference)
         avc->used_for_long_term_ref_flags |= 1 << i;
      if (dpb_slot->pStdReferenceInfo->flags.is_non_existing)
         avc->non_existing_frame_flags |= 1 << i;
   }
   avc->curr_pic_ref_frame_num = frame_info->referenceSlotCount;
}

static void
get_h265_param(struct radv_video_session *vid, struct vk_video_session_parameters *params,
               const struct VkVideoDecodeInfoKHR *frame_info, struct ac_video_dec_decode_cmd *cmd)
{
   struct ac_video_dec_hevc *hevc = &cmd->codec_param.hevc;
   const struct VkVideoDecodeH265PictureInfoKHR *h265_pic_info =
      vk_find_struct_const(frame_info->pNext, VIDEO_DECODE_H265_PICTURE_INFO_KHR);
   const StdVideoH265SequenceParameterSet *sps;
   const StdVideoH265PictureParameterSet *pps;

   vk_video_get_h265_parameters(&vid->vk, params, frame_info, h265_pic_info, &sps, &pps);

   cmd->width = sps->pic_width_in_luma_samples;
   cmd->height = sps->pic_height_in_luma_samples;

   hevc->sps_flags.separate_colour_plane_flag = sps->flags.separate_colour_plane_flag;
   hevc->sps_flags.scaling_list_enabled_flag = sps->flags.scaling_list_enabled_flag;
   hevc->sps_flags.amp_enabled_flag = sps->flags.amp_enabled_flag;
   hevc->sps_flags.sample_adaptive_offset_enabled_flag = sps->flags.sample_adaptive_offset_enabled_flag;
   hevc->sps_flags.pcm_enabled_flag = sps->flags.pcm_enabled_flag;
   hevc->sps_flags.pcm_loop_filter_disabled_flag = sps->flags.pcm_loop_filter_disabled_flag;
   hevc->sps_flags.long_term_ref_pics_present_flag = sps->flags.long_term_ref_pics_present_flag;
   hevc->sps_flags.sps_temporal_mvp_enabled_flag = sps->flags.sps_temporal_mvp_enabled_flag;
   hevc->sps_flags.strong_intra_smoothing_enabled_flag = sps->flags.strong_intra_smoothing_enabled_flag;

   hevc->pps_flags.dependent_slice_segments_enabled_flag = pps->flags.dependent_slice_segments_enabled_flag;
   hevc->pps_flags.output_flag_present_flag = pps->flags.output_flag_present_flag;
   hevc->pps_flags.sign_data_hiding_enabled_flag = pps->flags.sign_data_hiding_enabled_flag;
   hevc->pps_flags.cabac_init_present_flag = pps->flags.cabac_init_present_flag;
   hevc->pps_flags.constrained_intra_pred_flag = pps->flags.constrained_intra_pred_flag;
   hevc->pps_flags.transform_skip_enabled_flag = pps->flags.transform_skip_enabled_flag;
   hevc->pps_flags.cu_qp_delta_enabled_flag = pps->flags.cu_qp_delta_enabled_flag;
   hevc->pps_flags.pps_slice_chroma_qp_offsets_present_flag = pps->flags.pps_slice_chroma_qp_offsets_present_flag;
   hevc->pps_flags.weighted_pred_flag = pps->flags.weighted_pred_flag;
   hevc->pps_flags.weighted_bipred_flag = pps->flags.weighted_bipred_flag;
   hevc->pps_flags.transquant_bypass_enabled_flag = pps->flags.transquant_bypass_enabled_flag;
   hevc->pps_flags.tiles_enabled_flag = pps->flags.tiles_enabled_flag;
   hevc->pps_flags.entropy_coding_sync_enabled_flag = pps->flags.entropy_coding_sync_enabled_flag;
   hevc->pps_flags.uniform_spacing_flag = pps->flags.uniform_spacing_flag;
   hevc->pps_flags.loop_filter_across_tiles_enabled_flag = pps->flags.loop_filter_across_tiles_enabled_flag;
   hevc->pps_flags.pps_loop_filter_across_slices_enabled_flag = pps->flags.pps_loop_filter_across_slices_enabled_flag;
   hevc->pps_flags.deblocking_filter_override_enabled_flag = pps->flags.deblocking_filter_override_enabled_flag;
   hevc->pps_flags.pps_deblocking_filter_disabled_flag = pps->flags.pps_deblocking_filter_disabled_flag;
   hevc->pps_flags.lists_modification_present_flag = pps->flags.lists_modification_present_flag;
   hevc->pps_flags.slice_segment_header_extension_present_flag = pps->flags.slice_segment_header_extension_present_flag;

   hevc->pic_flags.irap_pic_flag = h265_pic_info->pStdPictureInfo->flags.IrapPicFlag;
   hevc->pic_flags.idr_pic_flag = h265_pic_info->pStdPictureInfo->flags.IdrPicFlag;
   hevc->pic_flags.is_ref_pic_flag = frame_info->pSetupReferenceSlot != NULL;

   hevc->sps_max_dec_pic_buffering_minus1 =
      sps->pDecPicBufMgr ? sps->pDecPicBufMgr->max_dec_pic_buffering_minus1[sps->sps_max_sub_layers_minus1] : 0;
   hevc->chroma_format_idc = sps->chroma_format_idc;
   hevc->pic_width_in_luma_samples = sps->pic_width_in_luma_samples;
   hevc->pic_height_in_luma_samples = sps->pic_height_in_luma_samples;
   hevc->bit_depth_luma_minus8 = sps->bit_depth_luma_minus8;
   hevc->bit_depth_chroma_minus8 = sps->bit_depth_chroma_minus8;
   hevc->log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_pic_order_cnt_lsb_minus4;
   hevc->log2_min_luma_coding_block_size_minus3 = sps->log2_min_luma_coding_block_size_minus3;
   hevc->log2_diff_max_min_luma_coding_block_size = sps->log2_diff_max_min_luma_coding_block_size;
   hevc->log2_min_transform_block_size_minus2 = sps->log2_min_luma_transform_block_size_minus2;
   hevc->log2_diff_max_min_transform_block_size = sps->log2_diff_max_min_luma_transform_block_size;
   hevc->max_transform_hierarchy_depth_inter = sps->max_transform_hierarchy_depth_inter;
   hevc->max_transform_hierarchy_depth_intra = sps->max_transform_hierarchy_depth_intra;
   if (sps->flags.pcm_enabled_flag) {
      hevc->pcm_sample_bit_depth_luma_minus1 = sps->pcm_sample_bit_depth_luma_minus1;
      hevc->pcm_sample_bit_depth_chroma_minus1 = sps->pcm_sample_bit_depth_chroma_minus1;
      hevc->log2_min_pcm_luma_coding_block_size_minus3 = sps->log2_min_pcm_luma_coding_block_size_minus3;
      hevc->log2_diff_max_min_pcm_luma_coding_block_size = sps->log2_diff_max_min_pcm_luma_coding_block_size;
   }
   hevc->num_extra_slice_header_bits = pps->num_extra_slice_header_bits;
   hevc->init_qp_minus26 = pps->init_qp_minus26;
   hevc->diff_cu_qp_delta_depth = pps->diff_cu_qp_delta_depth;
   hevc->pps_cb_qp_offset = pps->pps_cb_qp_offset;
   hevc->pps_cr_qp_offset = pps->pps_cr_qp_offset;
   hevc->pps_beta_offset_div2 = pps->pps_beta_offset_div2;
   hevc->pps_tc_offset_div2 = pps->pps_tc_offset_div2;
   hevc->log2_parallel_merge_level_minus2 = pps->log2_parallel_merge_level_minus2;
   hevc->num_tile_columns_minus1 = pps->num_tile_columns_minus1;
   hevc->num_tile_rows_minus1 = pps->num_tile_rows_minus1;

   for (unsigned i = 0; i < H265_TILE_COLS_LIST_SIZE; ++i)
      hevc->column_width_minus1[i] = pps->column_width_minus1[i];

   for (unsigned i = 0; i < H265_TILE_ROWS_LIST_SIZE; ++i)
      hevc->row_height_minus1[i] = pps->row_height_minus1[i];

   const StdVideoH265ScalingLists *scaling_lists = NULL;
   vk_video_derive_h265_scaling_list(sps, pps, &scaling_lists);
   if (scaling_lists) {
      memcpy(hevc->scaling_list_4x4, scaling_lists->ScalingList4x4, sizeof(hevc->scaling_list_4x4));
      memcpy(hevc->scaling_list_8x8, scaling_lists->ScalingList8x8, sizeof(hevc->scaling_list_8x8));
      memcpy(hevc->scaling_list_16x16, scaling_lists->ScalingList16x16, sizeof(hevc->scaling_list_16x16));
      memcpy(hevc->scaling_list_32x32, scaling_lists->ScalingList32x32, sizeof(hevc->scaling_list_32x32));
      memcpy(hevc->scaling_list_dc_coef_16x16, scaling_lists->ScalingListDCCoef16x16,
             sizeof(hevc->scaling_list_dc_coef_16x16));
      memcpy(hevc->scaling_list_dc_coef_32x32, scaling_lists->ScalingListDCCoef32x32,
             sizeof(hevc->scaling_list_dc_coef_32x32));
   }

   hevc->num_short_term_ref_pic_sets = sps->num_short_term_ref_pic_sets;
   hevc->num_long_term_ref_pics_sps = sps->num_long_term_ref_pics_sps;
   hevc->num_ref_idx_l0_default_active_minus1 = pps->num_ref_idx_l0_default_active_minus1;
   hevc->num_ref_idx_l1_default_active_minus1 = pps->num_ref_idx_l1_default_active_minus1;
   hevc->num_delta_pocs_of_ref_rps_idx = h265_pic_info->pStdPictureInfo->NumDeltaPocsOfRefRpsIdx;
   hevc->num_bits_for_st_ref_pic_set_in_slice = h265_pic_info->pStdPictureInfo->NumBitsForSTRefPicSetInSlice;
   hevc->curr_poc = h265_pic_info->pStdPictureInfo->PicOrderCntVal;

   hevc->curr_pic_id = frame_info->pSetupReferenceSlot ? frame_info->pSetupReferenceSlot->slotIndex : 0;

   for (unsigned i = 0; i < H265_MAX_NUM_REF_PICS; i++)
      hevc->ref_pic_id_list[i] = 0x7f;

   uint8_t idxs[16];
   memset(idxs, 0xff, sizeof(idxs));

   for (unsigned i = 0; i < frame_info->referenceSlotCount; i++) {
      const struct VkVideoDecodeH265DpbSlotInfoKHR *dpb_slot =
         vk_find_struct_const(frame_info->pReferenceSlots[i].pNext, VIDEO_DECODE_H265_DPB_SLOT_INFO_KHR);
      int idx = frame_info->pReferenceSlots[i].slotIndex;

      hevc->ref_poc_list[i] = dpb_slot->pStdReferenceInfo->PicOrderCntVal;
      hevc->ref_pic_id_list[i] = idx;

      idxs[idx] = i;
   }

#define IDXS(x) ((x) == 0xff ? 0xff : idxs[(x)])
   for (unsigned i = 0; i < H265_MAX_RPS_SIZE; ++i) {
      hevc->ref_pic_set_st_curr_before[i] = IDXS(h265_pic_info->pStdPictureInfo->RefPicSetStCurrBefore[i]);
      hevc->ref_pic_set_st_curr_after[i] = IDXS(h265_pic_info->pStdPictureInfo->RefPicSetStCurrAfter[i]);
      hevc->ref_pic_set_lt_curr[i] = IDXS(h265_pic_info->pStdPictureInfo->RefPicSetLtCurr[i]);
   }
}

#define AV1_SUPERRES_NUM       8
#define AV1_SUPERRES_DENOM_MIN 9

static void
get_av1_param(struct radv_video_session *vid, struct vk_video_session_parameters *params,
              const struct VkVideoDecodeInfoKHR *frame_info, struct ac_video_dec_decode_cmd *cmd)
{
   struct ac_video_dec_av1 *av1 = &cmd->codec_param.av1;
   const struct VkVideoDecodeAV1PictureInfoKHR *av1_pic_info =
      vk_find_struct_const(frame_info->pNext, VIDEO_DECODE_AV1_PICTURE_INFO_KHR);
   const StdVideoDecodeAV1PictureInfo *pi = av1_pic_info->pStdPictureInfo;
   const StdVideoAV1SequenceHeader *seq_hdr;

   vk_video_get_av1_parameters(&vid->vk, params, frame_info, &seq_hdr);

   av1->pic_flags.use_128x128_superblock = seq_hdr->flags.use_128x128_superblock;
   av1->pic_flags.enable_filter_intra = seq_hdr->flags.enable_filter_intra;
   av1->pic_flags.enable_intra_edge_filter = seq_hdr->flags.enable_intra_edge_filter;
   av1->pic_flags.enable_interintra_compound = seq_hdr->flags.enable_interintra_compound;
   av1->pic_flags.enable_masked_compound = seq_hdr->flags.enable_masked_compound;
   av1->pic_flags.enable_dual_filter = seq_hdr->flags.enable_dual_filter;
   av1->pic_flags.enable_jnt_comp = seq_hdr->flags.enable_jnt_comp;
   av1->pic_flags.enable_ref_frame_mvs = seq_hdr->flags.enable_ref_frame_mvs;
   av1->pic_flags.enable_cdef = seq_hdr->flags.enable_cdef;
   av1->pic_flags.enable_restoration = pi->flags.UsesLr;
   av1->pic_flags.film_grain_params_present = seq_hdr->flags.film_grain_params_present;
   av1->pic_flags.disable_cdf_update = pi->flags.disable_cdf_update;
   av1->pic_flags.use_superres = pi->flags.use_superres;
   av1->pic_flags.allow_screen_content_tools = pi->flags.allow_screen_content_tools;
   av1->pic_flags.force_integer_mv = pi->flags.force_integer_mv;
   av1->pic_flags.allow_intrabc = pi->flags.allow_intrabc;
   av1->pic_flags.allow_high_precision_mv = pi->flags.allow_high_precision_mv;
   av1->pic_flags.is_motion_mode_switchable = pi->flags.is_motion_mode_switchable;
   av1->pic_flags.use_ref_frame_mvs = pi->flags.use_ref_frame_mvs;
   av1->pic_flags.disable_frame_end_update_cdf = pi->flags.disable_frame_end_update_cdf;
   av1->pic_flags.allow_warped_motion = pi->flags.allow_warped_motion;
   av1->pic_flags.reduced_tx_set = pi->flags.reduced_tx_set;
   av1->pic_flags.reference_select = pi->flags.reference_select;
   av1->pic_flags.skip_mode_present = pi->flags.skip_mode_present;
   av1->pic_flags.show_frame = 1;
   av1->pic_flags.showable_frame = 1;
   av1->pic_flags.ref_frame_update = !!pi->refresh_frame_flags && frame_info->pSetupReferenceSlot;

   switch (vid->vk.chroma_subsampling) {
   case VK_VIDEO_CHROMA_SUBSAMPLING_MONOCHROME_BIT_KHR:
      av1->color_config_flags.mono_chrome = 1;
      av1->color_config_flags.subsampling_x = 1;
      av1->color_config_flags.subsampling_y = 1;
      break;
   case VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR:
      av1->color_config_flags.mono_chrome = 0;
      av1->color_config_flags.subsampling_x = 1;
      av1->color_config_flags.subsampling_y = 1;
      break;
   case VK_VIDEO_CHROMA_SUBSAMPLING_422_BIT_KHR:
      av1->color_config_flags.mono_chrome = 0;
      av1->color_config_flags.subsampling_x = 1;
      av1->color_config_flags.subsampling_y = 0;
      break;
   case VK_VIDEO_CHROMA_SUBSAMPLING_444_BIT_KHR:
      av1->color_config_flags.mono_chrome = 0;
      av1->color_config_flags.subsampling_x = 0;
      av1->color_config_flags.subsampling_y = 0;
      break;
   }

   av1->width = frame_info->dstPictureResource.codedExtent.width;
   av1->height = frame_info->dstPictureResource.codedExtent.height;
   av1->max_width = seq_hdr->max_frame_width_minus_1 + 1;
   av1->max_height = seq_hdr->max_frame_height_minus_1 + 1;
   av1->bit_depth = seq_hdr->pColorConfig ? seq_hdr->pColorConfig->BitDepth : 8;
   av1->seq_profile = seq_hdr->seq_profile;
   av1->tx_mode = pi->TxMode;
   av1->frame_type = pi->frame_type;
   av1->primary_ref_frame = pi->primary_ref_frame;
   av1->order_hints = pi->OrderHint;
   av1->order_hint_bits = seq_hdr->order_hint_bits_minus_1 + 1;
   av1->interp_filter = pi->interpolation_filter;
   av1->superres_denom = pi->flags.use_superres ? pi->coded_denom + AV1_SUPERRES_DENOM_MIN : AV1_SUPERRES_NUM;

   if (pi->pLoopFilter) {
      av1->loop_filter.loop_filter_flags.mode_ref_delta_enabled = pi->pLoopFilter->flags.loop_filter_delta_enabled;
      av1->loop_filter.loop_filter_flags.mode_ref_delta_update = pi->pLoopFilter->flags.loop_filter_delta_update;
      av1->loop_filter.loop_filter_flags.delta_lf_multi = pi->flags.delta_lf_multi;
      av1->loop_filter.loop_filter_flags.delta_lf_present = pi->flags.delta_lf_present;
      av1->loop_filter.loop_filter_level[0] = pi->pLoopFilter->loop_filter_level[0];
      av1->loop_filter.loop_filter_level[1] = pi->pLoopFilter->loop_filter_level[1];
      av1->loop_filter.loop_filter_level[2] = pi->pLoopFilter->loop_filter_level[2];
      av1->loop_filter.loop_filter_level[3] = pi->pLoopFilter->loop_filter_level[3];
      av1->loop_filter.loop_filter_sharpness = pi->pLoopFilter->loop_filter_sharpness;
      memcpy(av1->loop_filter.loop_filter_ref_deltas, pi->pLoopFilter->loop_filter_ref_deltas,
             sizeof(av1->loop_filter.loop_filter_ref_deltas));
      memcpy(av1->loop_filter.loop_filter_mode_deltas, pi->pLoopFilter->loop_filter_mode_deltas,
             sizeof(av1->loop_filter.loop_filter_mode_deltas));
      av1->loop_filter.delta_lf_res = pi->delta_lf_res;
   }

   if (pi->flags.UsesLr && pi->pLoopRestoration) {
      av1->loop_restoration.frame_restoration_type[0] = pi->pLoopRestoration->FrameRestorationType[0];
      av1->loop_restoration.frame_restoration_type[1] = pi->pLoopRestoration->FrameRestorationType[1];
      av1->loop_restoration.frame_restoration_type[2] = pi->pLoopRestoration->FrameRestorationType[2];
      for (unsigned i = 0; i < AV1_MAX_NUM_PLANES; ++i)
         av1->loop_restoration.log2_restoration_size_minus5[i] = pi->pLoopRestoration->LoopRestorationSize[i];
   }

   if (pi->pQuantization) {
      av1->quantization.flags.delta_q_present = pi->flags.delta_q_present;
      av1->quantization.delta_q_res = pi->delta_q_res;
      av1->quantization.base_q_idx = pi->pQuantization->base_q_idx;
      av1->quantization.delta_q_y_dc = pi->pQuantization->DeltaQYDc;
      av1->quantization.delta_q_u_dc = pi->pQuantization->DeltaQUDc;
      av1->quantization.delta_q_u_ac = pi->pQuantization->DeltaQUAc;
      av1->quantization.delta_q_v_dc = pi->pQuantization->DeltaQVDc;
      av1->quantization.delta_q_v_ac = pi->pQuantization->DeltaQVAc;
      if (pi->pQuantization->flags.using_qmatrix) {
         av1->quantization.qm_y = pi->pQuantization->qm_y | 0xf0;
         av1->quantization.qm_u = pi->pQuantization->qm_u | 0xf0;
         av1->quantization.qm_v = pi->pQuantization->qm_v | 0xf0;
      } else {
         av1->quantization.qm_y = 0xff;
         av1->quantization.qm_u = 0xff;
         av1->quantization.qm_v = 0xff;
      }
   }

   if (pi->pSegmentation) {
      av1->segmentation.flags.segmentation_enabled = pi->flags.segmentation_enabled;
      av1->segmentation.flags.segmentation_update_map = pi->flags.segmentation_update_map;
      av1->segmentation.flags.segmentation_temporal_update = pi->flags.segmentation_temporal_update;
      av1->segmentation.flags.segmentation_update_data = pi->flags.segmentation_update_data;

      if (pi->flags.segmentation_enabled) {
         memcpy(av1->segmentation.feature_data, pi->pSegmentation->FeatureData, sizeof(av1->segmentation.feature_data));
         memcpy(av1->segmentation.feature_mask, pi->pSegmentation->FeatureEnabled,
                sizeof(av1->segmentation.feature_mask));
      }
   }

   if (pi->pCDEF) {
      av1->cdef.cdef_damping_minus3 = pi->pCDEF->cdef_damping_minus_3;
      av1->cdef.cdef_bits = pi->pCDEF->cdef_bits;
      for (unsigned i = 0; i < AV1_MAX_CDEF_FILTER_STRENGTHS; i++) {
         av1->cdef.cdef_y_pri_strength[i] = pi->pCDEF->cdef_y_pri_strength[i];
         av1->cdef.cdef_y_sec_strength[i] = pi->pCDEF->cdef_y_sec_strength[i];
         av1->cdef.cdef_uv_pri_strength[i] = pi->pCDEF->cdef_uv_pri_strength[i];
         av1->cdef.cdef_uv_sec_strength[i] = pi->pCDEF->cdef_uv_sec_strength[i];
      }
   }

   av1->film_grain.flags.apply_grain = pi->flags.apply_grain;
   if (av1->film_grain.flags.apply_grain && pi->pFilmGrain) {
      av1->film_grain.flags.chroma_scaling_from_luma = pi->pFilmGrain->flags.chroma_scaling_from_luma;
      av1->film_grain.flags.overlap_flag = pi->pFilmGrain->flags.overlap_flag;
      av1->film_grain.flags.clip_to_restricted_range = pi->pFilmGrain->flags.clip_to_restricted_range;
      av1->film_grain.grain_scaling_minus8 = pi->pFilmGrain->grain_scaling_minus_8;
      av1->film_grain.ar_coeff_lag = pi->pFilmGrain->ar_coeff_lag;
      av1->film_grain.ar_coeff_shift_minus6 = pi->pFilmGrain->ar_coeff_shift_minus_6;
      av1->film_grain.grain_scale_shift = pi->pFilmGrain->grain_scale_shift;
      av1->film_grain.grain_seed = pi->pFilmGrain->grain_seed;
      av1->film_grain.num_y_points = pi->pFilmGrain->num_y_points;
      memcpy(av1->film_grain.point_y_value, pi->pFilmGrain->point_y_value, sizeof(av1->film_grain.point_y_value));
      memcpy(av1->film_grain.point_y_scaling, pi->pFilmGrain->point_y_scaling, sizeof(av1->film_grain.point_y_scaling));
      av1->film_grain.num_cb_points = pi->pFilmGrain->num_cb_points;
      memcpy(av1->film_grain.point_cb_value, pi->pFilmGrain->point_cb_value, sizeof(av1->film_grain.point_cb_value));
      memcpy(av1->film_grain.point_cb_scaling, pi->pFilmGrain->point_cb_scaling,
             sizeof(av1->film_grain.point_cb_scaling));
      av1->film_grain.num_cr_points = pi->pFilmGrain->num_cr_points;
      memcpy(av1->film_grain.point_cr_value, pi->pFilmGrain->point_cr_value, sizeof(av1->film_grain.point_cr_value));
      memcpy(av1->film_grain.point_cr_scaling, pi->pFilmGrain->point_cr_scaling,
             sizeof(av1->film_grain.point_cr_scaling));
      for (unsigned i = 0; i < AV1_MAX_NUM_POS_LUMA; i++)
         av1->film_grain.ar_coeffs_y_plus128[i] = pi->pFilmGrain->ar_coeffs_y_plus_128[i];
      for (unsigned i = 0; i < AV1_MAX_NUM_POS_CHROMA; i++) {
         av1->film_grain.ar_coeffs_cb_plus128[i] = pi->pFilmGrain->ar_coeffs_cb_plus_128[i];
         av1->film_grain.ar_coeffs_cr_plus128[i] = pi->pFilmGrain->ar_coeffs_cr_plus_128[i];
      }
      av1->film_grain.cb_mult = pi->pFilmGrain->cb_mult;
      av1->film_grain.cb_luma_mult = pi->pFilmGrain->cb_luma_mult;
      av1->film_grain.cb_offset = pi->pFilmGrain->cb_offset;
      av1->film_grain.cr_mult = pi->pFilmGrain->cr_mult;
      av1->film_grain.cr_luma_mult = pi->pFilmGrain->cr_luma_mult;
      av1->film_grain.cr_offset = pi->pFilmGrain->cr_offset;
   }

   if (pi->pTileInfo) {
      av1->tile_info.tile_cols = pi->pTileInfo->TileCols;
      av1->tile_info.tile_rows = pi->pTileInfo->TileRows;
      av1->tile_info.context_update_tile_id = pi->pTileInfo->context_update_tile_id;
      for (unsigned i = 0; i < AV1_MAX_TILE_COLS + 1; ++i) {
         const unsigned sb_shift = seq_hdr->flags.use_128x128_superblock ? 5 : 4;
         av1->tile_info.tile_col_start_sb[i] = pi->pTileInfo->pMiColStarts[i] >> sb_shift;
         av1->tile_info.tile_row_start_sb[i] = pi->pTileInfo->pMiRowStarts[i] >> sb_shift;
      }
      memcpy(av1->tile_info.width_in_sbs, pi->pTileInfo->pWidthInSbsMinus1, sizeof(av1->tile_info.width_in_sbs));
      memcpy(av1->tile_info.height_in_sbs, pi->pTileInfo->pHeightInSbsMinus1, sizeof(av1->tile_info.height_in_sbs));
      for (unsigned i = 0; i < AV1_MAX_NUM_TILES; ++i) {
         av1->tile_info.tile_offset[i] = av1_pic_info->pTileOffsets[i];
         av1->tile_info.tile_size[i] = av1_pic_info->pTileSizes[i];
      }

      /* pMi{Row,Col}Starts is unreliable, some apps send SB, some send MI, so use
       * p{Width,Height}InSbsMinus1 instead. But for uniform_tile_spacing_flag,
       * those are not defined by spec.
       *
       * TODO: Remove when FFmpeg and CTS are fixed.
       */
      if (pi->pTileInfo->flags.uniform_tile_spacing_flag) {
         VkExtent2D frameExtent = frame_info->dstPictureResource.codedExtent;
         if (pi->flags.use_superres)
            frameExtent.width = (frameExtent.width * 8 + av1->superres_denom / 2) / av1->superres_denom;
         const unsigned sb_size = seq_hdr->flags.use_128x128_superblock ? 128 : 64;
         const unsigned sb_width = DIV_ROUND_UP(frameExtent.width, sb_size);
         const unsigned sb_height = DIV_ROUND_UP(frameExtent.height, sb_size);
         const unsigned tile_width_sb = DIV_ROUND_UP(sb_width, pi->pTileInfo->TileCols);
         const unsigned tile_height_sb = DIV_ROUND_UP(sb_height, pi->pTileInfo->TileRows);

         av1->tile_info.tile_col_start_sb[0] = 0;
         for (unsigned i = 1; i < pi->pTileInfo->TileCols; ++i)
            av1->tile_info.tile_col_start_sb[i] = av1->tile_info.tile_col_start_sb[i - 1] + tile_width_sb;
         av1->tile_info.tile_col_start_sb[pi->pTileInfo->TileCols] = sb_width;

         av1->tile_info.tile_row_start_sb[0] = 0;
         for (unsigned i = 1; i < pi->pTileInfo->TileRows; ++i)
            av1->tile_info.tile_row_start_sb[i] = av1->tile_info.tile_row_start_sb[i - 1] + tile_height_sb;
         av1->tile_info.tile_row_start_sb[pi->pTileInfo->TileRows] = sb_height;
      } else {
         av1->tile_info.tile_col_start_sb[0] = 0;
         assert(pi->pTileInfo->pMiColStarts[0] == 0);
         for (unsigned i = 0; i < pi->pTileInfo->TileCols; ++i)
            av1->tile_info.tile_col_start_sb[i + 1] =
               av1->tile_info.tile_col_start_sb[i] + pi->pTileInfo->pWidthInSbsMinus1[i] + 1;

         av1->tile_info.tile_row_start_sb[0] = 0;
         assert(pi->pTileInfo->pMiRowStarts[0] == 0);
         for (unsigned i = 0; i < pi->pTileInfo->TileRows; ++i)
            av1->tile_info.tile_row_start_sb[i + 1] =
               av1->tile_info.tile_row_start_sb[i] + pi->pTileInfo->pHeightInSbsMinus1[i] + 1;
      }
   }

   if (pi->pGlobalMotion) {
      for (unsigned i = 0; i < AV1_NUM_REF_FRAMES; ++i) {
         av1->global_motion.gm_type[i] = pi->pGlobalMotion->GmType[i];
         for (unsigned j = 0; j < AV1_GLOBAL_MOTION_PARAMS; ++j)
            av1->global_motion.gm_params[i][j] = pi->pGlobalMotion->gm_params[i][j];
      }
   }

   av1->cur_id = frame_info->pSetupReferenceSlot ? frame_info->pSetupReferenceSlot->slotIndex : 0;

   for (unsigned i = 0; i < AV1_NUM_REF_FRAMES; i++)
      av1->ref_frame_id_list[i] = 0x7f;

   uint16_t used_slots = 1 << av1->cur_id;
   int idxs[RADV_VIDEO_AV1_MAX_DPB_SLOTS];
   unsigned i = 0;
   for (i = 0; i < frame_info->referenceSlotCount; i++) {
      int idx = frame_info->pReferenceSlots[i].slotIndex;
      idxs[idx] = i;
      av1->ref_frame_id_list[i] = idx;
      used_slots |= 1 << idx;
   }
   /* Go through all the slots and fill in the ones that haven't been used. */
   for (unsigned j = 0; j < STD_VIDEO_AV1_NUM_REF_FRAMES + 1; j++) {
      if ((used_slots & (1 << j)) == 0) {
         av1->ref_frame_id_list[i] = j;
         used_slots |= 1 << j;
         i++;
      }
   }
   assert(used_slots == 0x1ff && i == STD_VIDEO_AV1_NUM_REF_FRAMES);

   for (i = 0; i < AV1_TOTAL_REFS_PER_FRAME; ++i) {
      if (av1_pic_info->referenceNameSlotIndices[i] < 0) {
         av1->ref_frames[i].ref_id = 0x7f;
         continue;
      }

      int idx = idxs[av1_pic_info->referenceNameSlotIndices[i]];
      const VkVideoReferenceSlotInfoKHR *info = &frame_info->pReferenceSlots[idx];
      const VkVideoDecodeAV1DpbSlotInfoKHR *dpb_slot =
         vk_find_struct_const(info->pNext, VIDEO_DECODE_AV1_DPB_SLOT_INFO_KHR);

      av1->ref_frames[i].ref_id = info->slotIndex;
      av1->ref_frames[i].width = info->pPictureResource->codedExtent.width;
      av1->ref_frames[i].height = info->pPictureResource->codedExtent.height;
      av1->ref_frames[i].ref_frame_sign_bias = dpb_slot->pStdReferenceInfo->RefFrameSignBias;
   }
}

static void
get_vp9_param(struct radv_video_session *vid, struct vk_video_session_parameters *params,
              const struct VkVideoDecodeInfoKHR *frame_info, struct ac_video_dec_decode_cmd *cmd)
{
   struct ac_video_dec_vp9 *vp9 = &cmd->codec_param.vp9;
   const struct VkVideoDecodeVP9PictureInfoKHR *vp9_pic_info =
      vk_find_struct_const(frame_info->pNext, VIDEO_DECODE_VP9_PICTURE_INFO_KHR);
   const StdVideoDecodeVP9PictureInfo *std_pic_info = vp9_pic_info->pStdPictureInfo;

   vp9->pic_flags.error_resilient_mode = std_pic_info->flags.error_resilient_mode;
   vp9->pic_flags.intra_only = std_pic_info->flags.intra_only;
   vp9->pic_flags.allow_high_precision_mv = std_pic_info->flags.allow_high_precision_mv;
   vp9->pic_flags.refresh_frame_context = std_pic_info->flags.refresh_frame_context;
   vp9->pic_flags.frame_parallel_decoding_mode = std_pic_info->flags.frame_parallel_decoding_mode;
   vp9->pic_flags.show_frame = 1;
   vp9->pic_flags.use_prev_frame_mvs = std_pic_info->flags.UsePrevFrameMvs;

   vp9->profile = vid->vk.vp9.profile;
   vp9->width = frame_info->dstPictureResource.codedExtent.width;
   vp9->height = frame_info->dstPictureResource.codedExtent.height;
   vp9->frame_context_idx = std_pic_info->frame_context_idx;
   vp9->reset_frame_context = std_pic_info->reset_frame_context;
   vp9->cur_id = frame_info->pSetupReferenceSlot ? frame_info->pSetupReferenceSlot->slotIndex : 0;

   if (std_pic_info->pColorConfig)
      vp9->bit_depth_luma_minus8 = vp9->bit_depth_chroma_minus8 = std_pic_info->pColorConfig->BitDepth - 8;

   vp9->frame_type = std_pic_info->frame_type;
   vp9->interp_filter = std_pic_info->interpolation_filter;

   vp9->base_q_idx = std_pic_info->base_q_idx;
   vp9->y_dc_delta_q = std_pic_info->delta_q_y_dc;
   vp9->uv_ac_delta_q = std_pic_info->delta_q_uv_ac;
   vp9->uv_dc_delta_q = std_pic_info->delta_q_uv_dc;

   vp9->log2_tile_cols = std_pic_info->tile_cols_log2;
   vp9->log2_tile_rows = std_pic_info->tile_rows_log2;

   vp9->uncompressed_header_offset = vp9_pic_info->uncompressedHeaderOffset;
   vp9->compressed_header_size = vp9_pic_info->tilesOffset - vp9_pic_info->compressedHeaderOffset;
   vp9->uncompressed_header_size = vp9_pic_info->compressedHeaderOffset - vp9_pic_info->uncompressedHeaderOffset;

   for (unsigned i = 0; i < VP9_TOTAL_REFS_PER_FRAME; i++) {
      vp9->ref_frames[i] =
         vp9_pic_info->referenceNameSlotIndices[i] == -1 ? 0x7f : vp9_pic_info->referenceNameSlotIndices[i];
   }

   uint16_t used_slots = 1 << vp9->cur_id;
   int idx = 0;
   for (idx = 0; idx < frame_info->referenceSlotCount; idx++) {
      int32_t slotIndex = frame_info->pReferenceSlots[idx].slotIndex;
      vp9->ref_frame_id_list[idx] = slotIndex;
      used_slots |= 1 << slotIndex;
   }
   /* Go through all the slots and fill in the ones that haven't been used. */
   for (unsigned j = 0; j < STD_VIDEO_VP9_NUM_REF_FRAMES + 1; j++) {
      if ((used_slots & (1 << j)) == 0) {
         vp9->ref_frame_id_list[idx] = j;
         used_slots |= 1 << j;
         idx++;
      }
   }

   for (unsigned i = STD_VIDEO_VP9_REFERENCE_NAME_LAST_FRAME; i <= STD_VIDEO_VP9_REFERENCE_NAME_ALTREF_FRAME; i++)
      vp9->ref_frame_sign_bias[i] = std_pic_info->ref_frame_sign_bias_mask & (1 << i) ? 1 : 0;

   if (std_pic_info->pLoopFilter) {
      vp9->loop_filter.loop_filter_flags.mode_ref_delta_enabled =
         std_pic_info->pLoopFilter->flags.loop_filter_delta_enabled;
      vp9->loop_filter.loop_filter_flags.mode_ref_delta_update =
         std_pic_info->pLoopFilter->flags.loop_filter_delta_update;
      vp9->loop_filter.loop_filter_level = std_pic_info->pLoopFilter->loop_filter_level;
      vp9->loop_filter.loop_filter_sharpness = std_pic_info->pLoopFilter->loop_filter_sharpness;

      for (unsigned i = 0; i < VP9_MAX_REF_FRAMES; i++)
         vp9->loop_filter.loop_filter_ref_deltas[i] = std_pic_info->pLoopFilter->loop_filter_ref_deltas[i];
      for (unsigned i = 0; i < VP9_LOOP_FILTER_ADJUSTMENTS; i++)
         vp9->loop_filter.loop_filter_mode_deltas[i] = std_pic_info->pLoopFilter->loop_filter_mode_deltas[i];
   }

   if (std_pic_info->flags.segmentation_enabled && std_pic_info->pSegmentation) {
      vp9->segmentation.flags.segmentation_enabled = 1;
      vp9->segmentation.flags.segmentation_update_map = std_pic_info->pSegmentation->flags.segmentation_update_map;
      vp9->segmentation.flags.segmentation_temporal_update =
         std_pic_info->pSegmentation->flags.segmentation_temporal_update;
      vp9->segmentation.flags.segmentation_update_data = std_pic_info->pSegmentation->flags.segmentation_update_data;
      vp9->segmentation.flags.segmentation_abs_delta =
         std_pic_info->pSegmentation->flags.segmentation_abs_or_delta_update;

      for (unsigned i = 0; i < VP9_MAX_SEGMENTS; i++) {
         vp9->segmentation.feature_mask[i] = std_pic_info->pSegmentation->FeatureEnabled[i];
         for (unsigned j = 0; j < VP9_SEG_LVL_MAX; j++)
            vp9->segmentation.feature_data[i][j] = std_pic_info->pSegmentation->FeatureData[i][j];
      }

      for (unsigned i = 0; i < STD_VIDEO_VP9_MAX_SEGMENTATION_TREE_PROBS; i++)
         vp9->segmentation.tree_probs[i] = std_pic_info->pSegmentation->segmentation_tree_probs[i];
      for (unsigned i = 0; i < STD_VIDEO_VP9_MAX_SEGMENTATION_PRED_PROB; i++)
         vp9->segmentation.pred_probs[i] = std_pic_info->pSegmentation->segmentation_pred_prob[i];
   }

   switch (vid->vk.chroma_subsampling) {
   case VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR:
      vp9->color_config_flags.subsampling_x = 1;
      vp9->color_config_flags.subsampling_y = 1;
      break;
   case VK_VIDEO_CHROMA_SUBSAMPLING_422_BIT_KHR:
      vp9->color_config_flags.subsampling_x = 1;
      vp9->color_config_flags.subsampling_y = 0;
      break;
   case VK_VIDEO_CHROMA_SUBSAMPLING_444_BIT_KHR:
      vp9->color_config_flags.subsampling_x = 0;
      vp9->color_config_flags.subsampling_y = 0;
      break;
   default:
      break;
   }
}

static void
fill_surface(struct radv_cmd_buffer *cmd_buf, struct radv_image *img, uint32_t slice, bool interleaved_planes,
             enum amd_gfx_level gfx_level, struct ac_video_surface *surf)
{
   surf->format = vk_format_to_pipe_format(img->vk.format);
   surf->size = img->size;
   surf->num_planes = img->plane_count;

   if (interleaved_planes) {
      assert(gfx_level >= GFX9);
      uint32_t plane_sizes[4] = {0};
      for (uint32_t i = 1; i <= surf->num_planes; i++)
         plane_sizes[i] = plane_sizes[i - 1] + img->planes[i - 1].surface.u.gfx9.surf_slice_size;

      for (uint32_t i = 0; i < surf->num_planes; i++) {
         surf->planes[i].va = img->bindings[0].addr + slice * plane_sizes[surf->num_planes] + plane_sizes[i];
         surf->planes[i].surf = &img->planes[i].surface;
      }
   } else {
      for (uint32_t i = 0; i < surf->num_planes; i++) {
         surf->planes[i].va = img->bindings[0].addr;
         if (gfx_level >= GFX9)
            surf->planes[i].va +=
               img->planes[i].surface.u.gfx9.surf_offset + slice * img->planes[i].surface.u.gfx9.surf_slice_size;
         else
            surf->planes[i].va += (uint64_t)img->planes[i].surface.u.legacy.level[0].offset_256B * 256 +
                                  slice * (uint64_t)img->planes[i].surface.u.legacy.level[0].slice_size_dw * 4;
         surf->planes[i].surf = &img->planes[i].surface;
      }
   }

   radv_cs_add_buffer(radv_cmd_buffer_device(cmd_buf)->ws, cmd_buf->cs->b, img->bindings[0].bo);
}

static enum ac_video_dec_tier
select_tier(struct radv_device *device, struct radv_video_session *vid, const struct VkVideoDecodeInfoKHR *frame_info)
{
   struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_image_view *dst_iv = radv_image_view_from_handle(frame_info->dstPictureResource.imageViewBinding);
   struct radv_image_plane *luma = &dst_iv->image->planes[0];

   if (vid->dec->tiers & AC_VIDEO_DEC_TIER3 && radv_enable_tier3(pdev, vid->vk.op)) {
      VkImageUsageFlags coincide = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR | VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR;
      if (luma->surface.is_linear || (dst_iv->image->vk.usage & coincide) != coincide)
         return AC_VIDEO_DEC_TIER2;
      else
         return AC_VIDEO_DEC_TIER3;
   }

   if (vid->dec->tiers & AC_VIDEO_DEC_TIER2)
      return AC_VIDEO_DEC_TIER2;

   if (vid->dec->tiers & AC_VIDEO_DEC_TIER1)
      return AC_VIDEO_DEC_TIER1;

   return AC_VIDEO_DEC_TIER0;
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdDecodeVideoKHR(VkCommandBuffer commandBuffer, const VkVideoDecodeInfoKHR *frame_info)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(radv_buffer, src_buffer, frame_info->srcBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_video_session *vid = cmd_buffer->video.vid;
   struct vk_video_session_parameters *params = cmd_buffer->video.params;
   struct radv_cmd_stream *cs = cmd_buffer->cs;

   radeon_check_space(device->ws, cs->b, vid->dec->max_decode_cmd_dw);

   struct ac_video_dec_decode_cmd cmd = {
      .cmd_buffer = cs->b->buf + cs->b->cdw,
      .bitstream_size = frame_info->srcBufferRange,
      .width = frame_info->dstPictureResource.codedExtent.width,
      .height = frame_info->dstPictureResource.codedExtent.height,
      .tier = select_tier(device, vid, frame_info),
      .low_latency = radv_physical_device_instance(pdev)->perftest_flags & RADV_PERFTEST_LOWLATENCYDEC,
   };

   if (vid->sessionctx.mem) {
      cmd.session_va = radv_buffer_get_va(vid->sessionctx.mem->bo) + vid->sessionctx.offset;
      radv_cs_add_buffer(device->ws, cs->b, vid->sessionctx.mem->bo);
   }

   if (vid->dec->embedded_size) {
      uint32_t offset;
      radv_vid_buffer_upload_alloc(cmd_buffer, vid->dec->embedded_size, &offset, &cmd.embedded_ptr);
      cmd.embedded_va = radv_buffer_get_va(cmd_buffer->upload.upload_bo) + offset;
   }

   switch (vid->vk.op) {
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
      get_h264_param(vid, params, frame_info, &cmd);
      break;
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
      get_h265_param(vid, params, frame_info, &cmd);
      break;
   case VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR:
      get_av1_param(vid, params, frame_info, &cmd);
      break;
   case VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR:
      get_vp9_param(vid, params, frame_info, &cmd);
      break;
   default:
      UNREACHABLE("Invalid op");
   }

   cmd.bitstream_va += vk_buffer_address(&src_buffer->vk, frame_info->srcBufferOffset);
   radv_cs_add_buffer(device->ws, cs->b, src_buffer->bo);

   VK_FROM_HANDLE(radv_image_view, db, frame_info->dstPictureResource.imageViewBinding);
   uint32_t db_slice = db->vk.base_array_layer + frame_info->dstPictureResource.baseArrayLayer;
   fill_surface(cmd_buffer, db->image, db_slice, false, pdev->info.gfx_level, &cmd.decode_surface);

   if (cmd.tier >= AC_VIDEO_DEC_TIER2) {
      for (uint32_t i = 0; i < frame_info->referenceSlotCount; i++) {
         VK_FROM_HANDLE(radv_image_view, iv, frame_info->pReferenceSlots[i].pPictureResource->imageViewBinding);
         fill_surface(cmd_buffer, iv->image,
                      iv->vk.base_array_layer + frame_info->pReferenceSlots[i].pPictureResource->baseArrayLayer,
                      cmd.tier != AC_VIDEO_DEC_TIER3, pdev->info.gfx_level, &cmd.ref_surfaces[i]);
         cmd.ref_id[i] = frame_info->pReferenceSlots[i].slotIndex;
         cmd.num_refs++;
      }

      if (vid->vk.op == VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR) {
         uint16_t used_slots = 1 << (frame_info->pSetupReferenceSlot ? frame_info->pSetupReferenceSlot->slotIndex : 0);
         for (uint32_t i = 0; i < frame_info->referenceSlotCount; i++)
            used_slots |= 1 << cmd.ref_id[i];
         for (uint32_t i = frame_info->referenceSlotCount; i < STD_VIDEO_AV1_NUM_REF_FRAMES; i++) {
            for (uint32_t j = 0; j < STD_VIDEO_AV1_NUM_REF_FRAMES + 1; j++) {
               if ((used_slots & (1 << j)) == 0) {
                  VK_FROM_HANDLE(radv_image_view, dpb, frame_info->dstPictureResource.imageViewBinding);
                  fill_surface(cmd_buffer, dpb->image, 0, false, pdev->info.gfx_level,
                               &cmd.ref_surfaces[cmd.num_refs++]);
                  cmd.ref_id[i] = j;
                  used_slots |= 1 << j;
                  break;
               }
            }
         }
      }

      if (frame_info->pSetupReferenceSlot) {
         VK_FROM_HANDLE(radv_image_view, dpb, frame_info->pSetupReferenceSlot->pPictureResource->imageViewBinding);
         uint32_t dpb_slice =
            dpb->vk.base_array_layer + frame_info->pSetupReferenceSlot->pPictureResource->baseArrayLayer;
         if (db != dpb || db_slice != dpb_slice) {
            fill_surface(cmd_buffer, dpb->image, dpb_slice, cmd.tier != AC_VIDEO_DEC_TIER3, pdev->info.gfx_level,
                         &cmd.ref_surfaces[cmd.num_refs]);
            cmd.cur_id = frame_info->pSetupReferenceSlot->slotIndex;
            cmd.ref_id[cmd.num_refs++] = cmd.cur_id;
         }
      } else if (vid->intra_only_dpb) {
         fill_surface(cmd_buffer, vid->intra_only_dpb, 0, false, pdev->info.gfx_level, &cmd.ref_surfaces[cmd.num_refs]);
         cmd.cur_id = 0;
         cmd.ref_id[cmd.num_refs++] = cmd.cur_id;
      } else if (cmd.tier == AC_VIDEO_DEC_TIER2) {
         VK_FROM_HANDLE(radv_image_view, dpb, frame_info->dstPictureResource.imageViewBinding);
         fill_surface(cmd_buffer, dpb->image, 0, false, pdev->info.gfx_level, &cmd.ref_surfaces[cmd.num_refs]);
         cmd.cur_id = 0;
         cmd.ref_id[cmd.num_refs++] = cmd.cur_id;
      }
   } else {
      struct radv_image *dpb;
      if (frame_info->pSetupReferenceSlot)
         dpb = radv_image_view_from_handle(frame_info->pSetupReferenceSlot->pPictureResource->imageViewBinding)->image;
      else if (vid->intra_only_dpb)
         dpb = vid->intra_only_dpb;
      else
         dpb = radv_image_view_from_handle(frame_info->dstPictureResource.imageViewBinding)->image;
      fill_surface(cmd_buffer, dpb, 0, false, pdev->info.gfx_level, &cmd.ref_surfaces[0]);
   }

   int ret = vid->dec->build_decode_cmd(vid->dec, &cmd);
   cs->b->cdw += cmd.out.cmd_dw;
   assert(ret == 0);
}

void
radv_video_get_profile_alignments(struct radv_physical_device *pdev, const VkVideoProfileListInfoKHR *profile_list,
                                  uint32_t *width_align_out, uint32_t *height_align_out)
{
   vk_video_get_profile_alignments(profile_list, width_align_out, height_align_out);
   bool is_h265_main_10 = false;

   if (profile_list) {
      for (unsigned i = 0; i < profile_list->profileCount; i++) {
         if (profile_list->pProfiles[i].videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR) {
            const struct VkVideoDecodeH265ProfileInfoKHR *h265_profile =
               vk_find_struct_const(profile_list->pProfiles[i].pNext, VIDEO_DECODE_H265_PROFILE_INFO_KHR);
            if (h265_profile->stdProfileIdc == STD_VIDEO_H265_PROFILE_IDC_MAIN_10)
               is_h265_main_10 = true;
         }
      }
   } else
      is_h265_main_10 = true;

   uint32_t db_alignment = radv_video_get_db_alignment(pdev, 64, is_h265_main_10);
   *width_align_out = MAX2(*width_align_out, db_alignment);
   *height_align_out = MAX2(*height_align_out, db_alignment);
}

void
radv_video_get_uvd_dpb_image(struct radv_physical_device *pdev, const struct VkVideoProfileListInfoKHR *profile_list,
                             struct radv_image *image)
{
   const bool is_10bit = image->vk.format == VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
   struct ac_video_dec_session_param param = {
      .sub_sample = AC_VIDEO_SUBSAMPLE_420,
      .max_width = image->vk.extent.width,
      .max_height = image->vk.extent.height,
      .max_bit_depth = is_10bit ? 10 : 8,
      .max_num_ref = image->vk.array_layers,
   };

   for (uint32_t i = 0; i < profile_list->profileCount; i++) {
      switch (profile_list->pProfiles[i].videoCodecOperation) {
      case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
         param.codec = AC_VIDEO_CODEC_AVC;
         break;
      case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
         param.codec = AC_VIDEO_CODEC_HEVC;
         break;
      default:
         UNREACHABLE("Invalid operation");
      }
      image->size = MAX2(image->size, ac_video_dec_dpb_size(&pdev->info, &param));
   }
}

bool
radv_video_decode_vp9_supported(const struct radv_physical_device *pdev)
{
   if (pdev->info.vcn_ip_version >= VCN_5_0_0)
      return radv_check_vcn_fw_version(pdev, 9, 7, 18);
   else if (pdev->info.vcn_ip_version >= VCN_4_0_0)
      return radv_check_vcn_fw_version(pdev, 9, 23, 13);
   else if (pdev->info.vcn_ip_version >= VCN_3_0_0)
      return radv_check_vcn_fw_version(pdev, 4, 33, 7);
   else if (pdev->info.vcn_ip_version >= VCN_2_0_0)
      return radv_check_vcn_fw_version(pdev, 8, 24, 4);
   else
      return false;
}
