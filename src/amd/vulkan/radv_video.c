/**************************************************************************
 *
 * Copyright 2017 Advanced Micro Devices, Inc.
 * Copyright 2021 Red Hat Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
#include "radv_private.h"

#include "vk_video/vulkan_video_codecs_common.h"
#include "ac_vcn_dec.h"
#include "ac_uvd_dec.h"

#define NUM_H264_REFS 17
#define NUM_H265_REFS 8
#define FB_BUFFER_OFFSET             0x1000
#define FB_BUFFER_SIZE               2048
#define FB_BUFFER_SIZE_TONGA     (2048 * 64)
#define IT_SCALING_TABLE_SIZE        992
#define RDECODE_SESSION_CONTEXT_SIZE (128 * 1024)

/* Not 100% sure this isn't too much but works */
#define VID_DEFAULT_ALIGNMENT 256

static bool
radv_vid_buffer_upload_alloc(struct radv_cmd_buffer *cmd_buffer, unsigned size,
                             unsigned *out_offset, void **ptr)
{
   return radv_cmd_buffer_upload_alloc_aligned(cmd_buffer, size, VID_DEFAULT_ALIGNMENT,
                                               out_offset, ptr);
}

/* generate an stream handle */
static unsigned si_vid_alloc_stream_handle()
{
   static unsigned counter = 0;
   unsigned stream_handle = 0;
   unsigned pid = getpid();
   int i;

   for (i = 0; i < 32; ++i)
      stream_handle |= ((pid >> i) & 1) << (31 - i);

   stream_handle ^= ++counter;
   return stream_handle;
}

void
radv_init_physical_device_decoder(struct radv_physical_device *pdevice)
{
   switch (pdevice->rad_info.family) {
   case CHIP_VEGA10:
   case CHIP_VEGA12:
   case CHIP_VEGA20:
      pdevice->vid_dec_reg.data0 = RUVD_GPCOM_VCPU_DATA0_SOC15;
      pdevice->vid_dec_reg.data1 = RUVD_GPCOM_VCPU_DATA1_SOC15;
      pdevice->vid_dec_reg.cmd = RUVD_GPCOM_VCPU_CMD_SOC15;
      pdevice->vid_dec_reg.cntl = RUVD_ENGINE_CNTL_SOC15;
      break;
   case CHIP_RAVEN:
   case CHIP_RAVEN2:
      pdevice->vid_dec_reg.data0 = RDECODE_VCN1_GPCOM_VCPU_DATA0;
      pdevice->vid_dec_reg.data1 = RDECODE_VCN1_GPCOM_VCPU_DATA1;
      pdevice->vid_dec_reg.cmd = RDECODE_VCN1_GPCOM_VCPU_CMD;
      pdevice->vid_dec_reg.cntl = RDECODE_VCN1_ENGINE_CNTL;
      break;
   case CHIP_NAVI10:
   case CHIP_NAVI12:
   case CHIP_NAVI14:
   case CHIP_RENOIR:
      pdevice->vid_dec_reg.data0 = RDECODE_VCN2_GPCOM_VCPU_DATA0;
      pdevice->vid_dec_reg.data1 = RDECODE_VCN2_GPCOM_VCPU_DATA1;
      pdevice->vid_dec_reg.cmd = RDECODE_VCN2_GPCOM_VCPU_CMD;
      pdevice->vid_dec_reg.cntl = RDECODE_VCN2_ENGINE_CNTL;
      break;
   case CHIP_MI100:
   case CHIP_MI200:
   case CHIP_NAVI21:
   case CHIP_NAVI22:
   case CHIP_NAVI23:
   case CHIP_NAVI24:
   case CHIP_VANGOGH:
   case CHIP_REMBRANDT:
      pdevice->vid_dec_reg.data0 = RDECODE_VCN2_5_GPCOM_VCPU_DATA0;
      pdevice->vid_dec_reg.data1 = RDECODE_VCN2_5_GPCOM_VCPU_DATA1;
      pdevice->vid_dec_reg.cmd = RDECODE_VCN2_5_GPCOM_VCPU_CMD;
      pdevice->vid_dec_reg.cntl = RDECODE_VCN2_5_ENGINE_CNTL;
      break;
   default:
      if (radv_has_uvd(pdevice)) {
         pdevice->vid_dec_reg.data0 = RUVD_GPCOM_VCPU_DATA0;
         pdevice->vid_dec_reg.data1 = RUVD_GPCOM_VCPU_DATA1;
         pdevice->vid_dec_reg.cmd = RUVD_GPCOM_VCPU_CMD;
         pdevice->vid_dec_reg.cntl = RUVD_ENGINE_CNTL;
      }
      break;
   }
}

static bool have_it(struct radv_video_session *vid)
{
   return vid->stream_type == RDECODE_CODEC_H264_PERF || vid->stream_type == RDECODE_CODEC_H265;
}

static unsigned calc_ctx_size_h264_perf(struct radv_video_session *vid)
{
   unsigned width_in_mb, height_in_mb, ctx_size;
   unsigned width = align(vid->vk.max_coded.width, VL_MACROBLOCK_WIDTH);
   unsigned height = align(vid->vk.max_coded.height, VL_MACROBLOCK_HEIGHT);

   unsigned max_references = vid->vk.max_dpb_slots + 1;

   // picture width & height in 16 pixel units
   width_in_mb = width / VL_MACROBLOCK_WIDTH;
   height_in_mb = align(height / VL_MACROBLOCK_HEIGHT, 2);

   ctx_size = max_references * align(width_in_mb * height_in_mb * 192, 256);

   return ctx_size;
}

static unsigned calc_ctx_size_h265_main(struct radv_video_session *vid)
{
   unsigned width = align(vid->vk.max_coded.width, VL_MACROBLOCK_WIDTH);
   unsigned height = align(vid->vk.max_coded.height, VL_MACROBLOCK_HEIGHT);

   unsigned max_references = vid->vk.max_dpb_slots + 1;

   if (vid->vk.max_coded.width * vid->vk.max_coded.height >= 4096 * 2000)
      max_references = MAX2(max_references, 8);
   else
      max_references = MAX2(max_references, 17);

   width = align(width, 16);
   height = align(height, 16);
   return ((width + 255) / 16) * ((height + 255) / 16) * 16 * max_references + 52 * 1024;
}

static unsigned calc_ctx_size_h265_main10(struct radv_video_session *vid)
{
   unsigned log2_ctb_size, width_in_ctb, height_in_ctb, num_16x16_block_per_ctb;
   unsigned context_buffer_size_per_ctb_row, cm_buffer_size, max_mb_address, db_left_tile_pxl_size;
   unsigned db_left_tile_ctx_size = 4096 / 16 * (32 + 16 * 4);

   unsigned width = align(vid->vk.max_coded.width, VL_MACROBLOCK_WIDTH);
   unsigned height = align(vid->vk.max_coded.height, VL_MACROBLOCK_HEIGHT);
   unsigned coeff_10bit = 2;

   unsigned max_references = vid->vk.max_dpb_slots + 1;

   if (vid->vk.max_coded.width * vid->vk.max_coded.height >= 4096 * 2000)
      max_references = MAX2(max_references, 8);
   else
      max_references = MAX2(max_references, 17);

   /* 64x64 is the maximum ctb size. */
   log2_ctb_size = 6;

   width_in_ctb = (width + ((1 << log2_ctb_size) - 1)) >> log2_ctb_size;
   height_in_ctb = (height + ((1 << log2_ctb_size) - 1)) >> log2_ctb_size;

   num_16x16_block_per_ctb = ((1 << log2_ctb_size) >> 4) * ((1 << log2_ctb_size) >> 4);
   context_buffer_size_per_ctb_row = align(width_in_ctb * num_16x16_block_per_ctb * 16, 256);
   max_mb_address = (unsigned)ceil(height * 8 / 2048.0);

   cm_buffer_size = max_references * context_buffer_size_per_ctb_row * height_in_ctb;
   db_left_tile_pxl_size = coeff_10bit * (max_mb_address * 2 * 2048 + 1024);

   return cm_buffer_size + db_left_tile_ctx_size + db_left_tile_pxl_size;
}

VkResult
radv_CreateVideoSessionKHR(VkDevice _device,
                           const VkVideoSessionCreateInfoKHR *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkVideoSessionKHR *pVideoSession)
{
   RADV_FROM_HANDLE(radv_device, device, _device);

   struct radv_video_session *vid =
      vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*vid), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!vid)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   memset(vid, 0, sizeof(struct radv_video_session));

   VkResult result = vk_video_session_init(&device->vk,
                                           &vid->vk,
                                           pCreateInfo);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, vid);
      return result;
   }

   vid->interlaced = false;
   vid->dpb_type = DPB_MAX_RES;

   switch (vid->vk.op) {
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
      vid->stream_type = RDECODE_CODEC_H264_PERF;
      if (device->physical_device->rad_info.family >= CHIP_NAVI21)
         vid->dpb_type = DPB_DYNAMIC_TIER_2;
      break;
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
      vid->stream_type = RDECODE_CODEC_H265;
      if (device->physical_device->rad_info.family >= CHIP_NAVI21)
         vid->dpb_type = DPB_DYNAMIC_TIER_2;
      break;
   default:
      return VK_ERROR_FEATURE_NOT_PRESENT;
   }

   vid->stream_handle = si_vid_alloc_stream_handle();
   vid->dbg_frame_cnt = 0;
   vid->db_alignment = (device->physical_device->rad_info.family >= CHIP_RENOIR &&
                        vid->vk.max_coded.width > 32 &&
                        (vid->stream_type == RDECODE_CODEC_H265 &&
                         vid->vk.h265.profile_idc == STD_VIDEO_H265_PROFILE_IDC_MAIN_10)) ? 64 : 32;

   *pVideoSession = radv_video_session_to_handle(vid);
   return VK_SUCCESS;
}

void
radv_DestroyVideoSessionKHR(VkDevice _device,
                            VkVideoSessionKHR _session,
                            const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_video_session, vid, _session);
   if (!_session)
      return;

   vk_object_base_finish(&vid->vk.base);
   vk_free2(&device->vk.alloc, pAllocator, vid);
}


VkResult
radv_CreateVideoSessionParametersKHR(VkDevice _device,
                                     const VkVideoSessionParametersCreateInfoKHR *pCreateInfo,
                                     const VkAllocationCallbacks *pAllocator,
                                     VkVideoSessionParametersKHR *pVideoSessionParameters)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_video_session, vid, pCreateInfo->videoSession);
   RADV_FROM_HANDLE(radv_video_session_params, templ, pCreateInfo->videoSessionParametersTemplate);
   struct radv_video_session_params *params =
      vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*params), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!params)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = vk_video_session_parameters_init(&device->vk,
                                                      &params->vk,
                                                      &vid->vk,
                                                      templ ? &templ->vk : NULL,
                                                      pCreateInfo);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, params);
      return result;
   }

   *pVideoSessionParameters = radv_video_session_params_to_handle(params);
   return VK_SUCCESS;
}

void
radv_DestroyVideoSessionParametersKHR(VkDevice _device,
                                      VkVideoSessionParametersKHR _params,
                                      const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_video_session_params, params, _params);

   vk_video_session_parameters_finish(&device->vk, &params->vk);
   vk_free2(&device->vk.alloc, pAllocator, params);
}

VkResult
radv_GetPhysicalDeviceVideoCapabilitiesKHR(VkPhysicalDevice physicalDevice,
                                           const VkVideoProfileInfoKHR *pVideoProfile,
                                           VkVideoCapabilitiesKHR *pCapabilities)
{
   RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
   struct video_codec_cap *cap = NULL;

   switch (pVideoProfile->videoCodecOperation) {
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
      cap = &pdevice->rad_info.dec_caps.codec_info[RADV_VIDEO_FORMAT_MPEG4_AVC];
      break;
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
      cap = &pdevice->rad_info.dec_caps.codec_info[RADV_VIDEO_FORMAT_HEVC];
      break;
   default:
      unreachable("unsupported operation");
   }

   if (cap && !cap->valid)
      cap = NULL;

   pCapabilities->flags = 0;
   pCapabilities->minBitstreamBufferOffsetAlignment = 128;
   pCapabilities->minBitstreamBufferSizeAlignment = 128;
   pCapabilities->pictureAccessGranularity.width = VL_MACROBLOCK_WIDTH;
   pCapabilities->pictureAccessGranularity.height = VL_MACROBLOCK_HEIGHT;
   pCapabilities->minCodedExtent.width = VL_MACROBLOCK_WIDTH;
   pCapabilities->minCodedExtent.height = VL_MACROBLOCK_HEIGHT;

   struct VkVideoDecodeCapabilitiesKHR *dec_caps = (struct VkVideoDecodeCapabilitiesKHR *)
      vk_find_struct(pCapabilities->pNext, VIDEO_DECODE_CAPABILITIES_KHR);
   if (dec_caps)
      dec_caps->flags = VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_DISTINCT_BIT_KHR;

   switch (pVideoProfile->videoCodecOperation) {
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR: {
      struct VkVideoDecodeH264CapabilitiesKHR *ext = (struct VkVideoDecodeH264CapabilitiesKHR *)
         vk_find_struct(pCapabilities->pNext, VIDEO_DECODE_H264_CAPABILITIES_KHR);
      pCapabilities->maxDpbSlots = NUM_H264_REFS;
      pCapabilities->maxActiveReferencePictures = NUM_H264_REFS;

      /* for h264 on navi21+ separate dpb images should work */
      if (pdevice->rad_info.family >= CHIP_NAVI21)
         pCapabilities->flags |= VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR;
      ext->fieldOffsetGranularity.x = 0;
      ext->fieldOffsetGranularity.y = 0;
      ext->maxLevelIdc = 51;
      strcpy(pCapabilities->stdHeaderVersion.extensionName, VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME);
      pCapabilities->stdHeaderVersion.specVersion = VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION;
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR: {
      struct VkVideoDecodeH265CapabilitiesKHR *ext = (struct VkVideoDecodeH265CapabilitiesKHR *)
         vk_find_struct(pCapabilities->pNext, VIDEO_DECODE_H265_CAPABILITIES_KHR);
      pCapabilities->maxDpbSlots = NUM_H264_REFS;
      pCapabilities->maxActiveReferencePictures = NUM_H265_REFS;
      /* for h265 on navi21+ separate dpb images should work */
      if (pdevice->rad_info.family >= CHIP_NAVI21)
         pCapabilities->flags |= VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR;
      ext->maxLevelIdc = 51;
      strcpy(pCapabilities->stdHeaderVersion.extensionName, VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_EXTENSION_NAME);
      pCapabilities->stdHeaderVersion.specVersion = VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_SPEC_VERSION;
      break;
   }
   default:
      break;
   }
   if (cap) {
      pCapabilities->maxCodedExtent.width = cap->max_width;
      pCapabilities->maxCodedExtent.height = cap->max_height;
   } else {
      switch (pVideoProfile->videoCodecOperation) {
      case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
         pCapabilities->maxCodedExtent.width = (pdevice->rad_info.family < CHIP_TONGA) ? 2048 : 4096;
         pCapabilities->maxCodedExtent.height = (pdevice->rad_info.family < CHIP_TONGA) ? 1152 : 4096;
         break;
      case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
         pCapabilities->maxCodedExtent.width = (pdevice->rad_info.family < CHIP_RENOIR) ?
            ((pdevice->rad_info.family < CHIP_TONGA) ? 2048 : 4096) : 8192;
         pCapabilities->maxCodedExtent.height = (pdevice->rad_info.family < CHIP_RENOIR) ?
            ((pdevice->rad_info.family < CHIP_TONGA) ? 1152 : 4096) : 4352;
         break;
      default:
         break;
      }
   }

   return VK_SUCCESS;
}

VkResult
radv_GetPhysicalDeviceVideoFormatPropertiesKHR(VkPhysicalDevice physicalDevice,
                                               const VkPhysicalDeviceVideoFormatInfoKHR *pVideoFormatInfo,
                                               uint32_t *pVideoFormatPropertyCount,
                                               VkVideoFormatPropertiesKHR *pVideoFormatProperties)
{
   /* radv requires separate allocates for DPB and decode video. */
   if ((pVideoFormatInfo->imageUsage & (VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR |
                                        VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR)) ==
       (VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR | VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   *pVideoFormatPropertyCount = 1;

   bool need_10bit = false;
   const struct VkVideoProfileListInfoKHR *prof_list = (struct VkVideoProfileListInfoKHR *)
      vk_find_struct_const(pVideoFormatInfo->pNext, VIDEO_PROFILE_LIST_INFO_KHR);
   if (prof_list) {
      for (unsigned i = 0; i < prof_list->profileCount; i++) {
         const VkVideoProfileInfoKHR *profile = &prof_list->pProfiles[i];
         if (profile->lumaBitDepth & VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR)
            need_10bit = true;
      }
   }

   (*pVideoFormatPropertyCount)++;
   if (need_10bit)
      (*pVideoFormatPropertyCount)++;

   if (!pVideoFormatProperties)
      return VK_SUCCESS;

   int idx = 0;
   pVideoFormatProperties[idx].format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
   pVideoFormatProperties[idx].imageType = VK_IMAGE_TYPE_2D;
   pVideoFormatProperties[idx].imageTiling = VK_IMAGE_TILING_OPTIMAL;
   pVideoFormatProperties[idx].imageUsageFlags = pVideoFormatInfo->imageUsage;
   idx++;
   if (need_10bit) {
      pVideoFormatProperties[idx].format = VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
      pVideoFormatProperties[idx].imageType = VK_IMAGE_TYPE_2D;
      pVideoFormatProperties[idx].imageTiling = VK_IMAGE_TILING_OPTIMAL;
      pVideoFormatProperties[idx].imageUsageFlags = pVideoFormatInfo->imageUsage;
      idx++;
   }
   return VK_SUCCESS;
}

#define RADV_BIND_SESSION_CTX 0
#define RADV_BIND_DECODER_CTX 1

VkResult
radv_GetVideoSessionMemoryRequirementsKHR(VkDevice _device,
                                          VkVideoSessionKHR videoSession,
                                          uint32_t *pMemoryRequirementsCount,
                                          VkVideoSessionMemoryRequirementsKHR *pMemoryRequirements)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_video_session, vid, videoSession);
   uint32_t memory_type_bits = (1u << device->physical_device->memory_properties.memoryTypeCount) - 1;
   uint32_t num_memory_reqs = 0;
   int idx = 0;

   if (device->physical_device->rad_info.family >= CHIP_POLARIS10)
      num_memory_reqs++;

   if (vid->stream_type == RDECODE_CODEC_H264_PERF || vid->stream_type == RDECODE_CODEC_H265)
      num_memory_reqs++;

   *pMemoryRequirementsCount = num_memory_reqs;

   if (!pMemoryRequirements)
      return VK_SUCCESS;

   /* 1 buffer for session context */
   if (device->physical_device->rad_info.family >= CHIP_POLARIS10) {
      pMemoryRequirements[idx].memoryBindIndex = RADV_BIND_SESSION_CTX;
      pMemoryRequirements[idx].memoryRequirements.size = RDECODE_SESSION_CONTEXT_SIZE;
      pMemoryRequirements[idx].memoryRequirements.alignment = 0;
      pMemoryRequirements[idx].memoryRequirements.memoryTypeBits = memory_type_bits;
      idx++;
   }

   if (vid->stream_type == RDECODE_CODEC_H264_PERF &&
       device->physical_device->rad_info.family >= CHIP_POLARIS10) {
      pMemoryRequirements[idx].memoryBindIndex = RADV_BIND_DECODER_CTX;
      pMemoryRequirements[idx].memoryRequirements.size = align(calc_ctx_size_h264_perf(vid), 4096);
      pMemoryRequirements[idx].memoryRequirements.alignment = 0;
      pMemoryRequirements[idx].memoryRequirements.memoryTypeBits = memory_type_bits;
   }
   if (vid->stream_type == RDECODE_CODEC_H265) {
      uint32_t ctx_size;

      if (vid->vk.h265.profile_idc == STD_VIDEO_H265_PROFILE_IDC_MAIN_10)
         ctx_size = calc_ctx_size_h265_main10(vid);
      else
         ctx_size = calc_ctx_size_h265_main(vid);
      pMemoryRequirements[idx].memoryBindIndex = RADV_BIND_DECODER_CTX;
      pMemoryRequirements[idx].memoryRequirements.size = align(ctx_size, 4096);
      pMemoryRequirements[idx].memoryRequirements.alignment = 0;
      pMemoryRequirements[idx].memoryRequirements.memoryTypeBits = memory_type_bits;
   }
   return VK_SUCCESS;
}

VkResult
radv_UpdateVideoSessionParametersKHR(VkDevice _device,
                                     VkVideoSessionParametersKHR videoSessionParameters,
                                     const VkVideoSessionParametersUpdateInfoKHR *pUpdateInfo)
{
   RADV_FROM_HANDLE(radv_video_session_params, params, videoSessionParameters);

   return vk_video_session_parameters_update(&params->vk, pUpdateInfo);
}

static void
copy_bind(struct radv_vid_mem *dst,
          const VkBindVideoSessionMemoryInfoKHR *src)
{
   dst->mem = radv_device_memory_from_handle(src->memory);
   dst->offset = src->memoryOffset;
   dst->size = src->memorySize;
}

VkResult
radv_BindVideoSessionMemoryKHR(VkDevice _device,
                               VkVideoSessionKHR videoSession,
                               uint32_t videoSessionBindMemoryCount,
                               const VkBindVideoSessionMemoryInfoKHR *pBindSessionMemoryInfos)
{
   RADV_FROM_HANDLE(radv_video_session, vid, videoSession);

   for (unsigned i = 0; i < videoSessionBindMemoryCount; i++) {
      switch (pBindSessionMemoryInfos[i].memoryBindIndex) {
      case RADV_BIND_SESSION_CTX:
         copy_bind(&vid->sessionctx, &pBindSessionMemoryInfos[i]);
         break;
      case RADV_BIND_DECODER_CTX:
         copy_bind(&vid->ctx, &pBindSessionMemoryInfos[i]);
         break;
      default:
         assert(0);
         break;
      }
   }
   return VK_SUCCESS;
}

/* add a new set register command to the IB */
static void set_reg(struct radv_cmd_buffer *cmd_buffer, unsigned reg, uint32_t val)
{
   struct radeon_cmdbuf *cs = cmd_buffer->cs;
   radeon_emit(cs, RDECODE_PKT0(reg >> 2, 0));
   radeon_emit(cs, val);
}

static void send_cmd(struct radv_cmd_buffer *cmd_buffer, unsigned cmd,
                     struct radeon_winsys_bo *bo, uint32_t offset)
{
   struct radv_physical_device *pdev = cmd_buffer->device->physical_device;
   uint64_t addr;

   radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs, bo);
   addr = radv_buffer_get_va(bo);
   addr += offset;
   set_reg(cmd_buffer, pdev->vid_dec_reg.data0, addr);
   set_reg(cmd_buffer, pdev->vid_dec_reg.data1, addr >> 32);
   set_reg(cmd_buffer, pdev->vid_dec_reg.cmd, cmd << 1);
}

static void rvcn_dec_message_create(struct radv_video_session *vid,
                                    void *ptr, uint32_t size)
{
   rvcn_dec_message_header_t *header = ptr;
   rvcn_dec_message_create_t *create = (void *)((char *)ptr + sizeof(rvcn_dec_message_header_t));

   memset(ptr, 0, size);
   header->header_size = sizeof(rvcn_dec_message_header_t);
   header->total_size = size;
   header->num_buffers = 1;
   header->msg_type = RDECODE_MSG_CREATE;
   header->stream_handle = vid->stream_handle;
   header->status_report_feedback_number = 0;

   header->index[0].message_id = RDECODE_MESSAGE_CREATE;
   header->index[0].offset = sizeof(rvcn_dec_message_header_t);
   header->index[0].size = sizeof(rvcn_dec_message_create_t);
   header->index[0].filled = 0;

   create->stream_type = vid->stream_type;
   create->session_flags = 0;
   create->width_in_samples = vid->vk.max_coded.width;
   create->height_in_samples = vid->vk.max_coded.height;
}

static void rvcn_dec_message_feedback(void *ptr)
{
   rvcn_dec_feedback_header_t *header = (void *)ptr;

   header->header_size = sizeof(rvcn_dec_feedback_header_t);
   header->total_size = sizeof(rvcn_dec_feedback_header_t);
   header->num_buffers = 0;
}

static const uint8_t h264_levels[] = { 10, 11, 12, 13, 20, 21, 22,
                                       30, 31, 32, 40, 41, 42,
                                       50, 51, 52, 60, 61, 62 };
static uint8_t get_h264_level(StdVideoH264LevelIdc level)
{
   assert (level <= STD_VIDEO_H264_LEVEL_IDC_6_2);
   return h264_levels[level];
}

static rvcn_dec_message_avc_t get_h264_msg(struct radv_video_session *vid,
                                           struct radv_video_session_params *params,
                                           const struct VkVideoDecodeInfoKHR *frame_info,
                                           uint32_t *slice_offset,
                                           uint32_t *width_in_samples,
                                           uint32_t *height_in_samples,
                                           void *it_ptr)
{
   rvcn_dec_message_avc_t result;
   const struct VkVideoDecodeH264PictureInfoKHR *h264_pic_info =
      vk_find_struct_const(frame_info->pNext, VIDEO_DECODE_H264_PICTURE_INFO_KHR);

   *slice_offset = h264_pic_info->pSliceOffsets[0];

   memset(&result, 0, sizeof(result));

   assert(params->vk.h264_dec.std_sps_count > 0);
   const StdVideoH264SequenceParameterSet *sps = vk_video_find_h264_dec_std_sps(&params->vk, h264_pic_info->pStdPictureInfo->seq_parameter_set_id);
   switch (sps->profile_idc) {
   case STD_VIDEO_H264_PROFILE_IDC_BASELINE:
      result.profile = RDECODE_H264_PROFILE_BASELINE;
      break;
   case STD_VIDEO_H264_PROFILE_IDC_MAIN:
      result.profile = RDECODE_H264_PROFILE_MAIN;
      break;
   case STD_VIDEO_H264_PROFILE_IDC_HIGH:
      result.profile = RDECODE_H264_PROFILE_HIGH;
      break;
   default:
      fprintf(stderr, "UNSUPPORTED CODEC %d\n", sps->profile_idc);
      result.profile= RDECODE_H264_PROFILE_MAIN;
      break;
   }

   *width_in_samples = (sps->pic_width_in_mbs_minus1 + 1) * 16;
   *height_in_samples = (sps->pic_height_in_map_units_minus1 + 1) * 16;
   if (!sps->flags.frame_mbs_only_flag)
      *height_in_samples *= 2;
   result.level = get_h264_level(sps->level_idc);

   result.sps_info_flags = 0;

   result.sps_info_flags |= sps->flags.direct_8x8_inference_flag << 0;
   result.sps_info_flags |= sps->flags.mb_adaptive_frame_field_flag << 1;
   result.sps_info_flags |= sps->flags.frame_mbs_only_flag << 2;
   result.sps_info_flags |= sps->flags.delta_pic_order_always_zero_flag << 3;
   if (vid->dpb_type != DPB_DYNAMIC_TIER_2)
      result.sps_info_flags |= 1 << RDECODE_SPS_INFO_H264_EXTENSION_SUPPORT_FLAG_SHIFT;

   result.bit_depth_luma_minus8 = sps->bit_depth_luma_minus8;
   result.bit_depth_chroma_minus8 = sps->bit_depth_chroma_minus8;
   result.log2_max_frame_num_minus4 = sps->log2_max_frame_num_minus4;
   result.pic_order_cnt_type = sps->pic_order_cnt_type;
   result.log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_pic_order_cnt_lsb_minus4;

   result.chroma_format = sps->chroma_format_idc;

   const StdVideoH264PictureParameterSet *pps = vk_video_find_h264_dec_std_pps(&params->vk, h264_pic_info->pStdPictureInfo->pic_parameter_set_id);
   result.pps_info_flags = 0;
   result.pps_info_flags |= pps->flags.transform_8x8_mode_flag << 0;
   result.pps_info_flags |= pps->flags.redundant_pic_cnt_present_flag << 1;
   result.pps_info_flags |= pps->flags.constrained_intra_pred_flag << 2;
   result.pps_info_flags |= pps->flags.deblocking_filter_control_present_flag << 3;
   result.pps_info_flags |= pps->weighted_bipred_idc << 4;
   result.pps_info_flags |= pps->flags.weighted_pred_flag << 6;
   result.pps_info_flags |= pps->flags.bottom_field_pic_order_in_frame_present_flag << 7;
   result.pps_info_flags |= pps->flags.entropy_coding_mode_flag << 8;

   result.pic_init_qp_minus26 = pps->pic_init_qp_minus26;
   result.chroma_qp_index_offset = pps->chroma_qp_index_offset;
   result.second_chroma_qp_index_offset = pps->second_chroma_qp_index_offset;

   if (pps->flags.pic_scaling_matrix_present_flag) {
      memcpy(result.scaling_list_4x4, pps->pScalingLists->ScalingList4x4, 6 * 16);
      memcpy(result.scaling_list_8x8[0], pps->pScalingLists->ScalingList8x8[0], 64);
      memcpy(result.scaling_list_8x8[1], pps->pScalingLists->ScalingList8x8[3], 64);
   } else if (sps->flags.seq_scaling_matrix_present_flag) {
      memcpy(result.scaling_list_4x4, sps->pScalingLists->ScalingList4x4, 6 * 16);
      memcpy(result.scaling_list_8x8[0], sps->pScalingLists->ScalingList8x8[0], 64);
      memcpy(result.scaling_list_8x8[1], sps->pScalingLists->ScalingList8x8[3], 64);
   } else {
      memset(result.scaling_list_4x4, 0x10, 6*16);
      memset(result.scaling_list_8x8, 0x10, 2*64);
   }

   memset(it_ptr, 0, IT_SCALING_TABLE_SIZE);
   memcpy(it_ptr, result.scaling_list_4x4, 6 * 16);
   memcpy((char *)it_ptr + 96, result.scaling_list_8x8, 2 * 64);

   result.num_ref_idx_l0_active_minus1 = pps->num_ref_idx_l0_default_active_minus1;
   result.num_ref_idx_l1_active_minus1 = pps->num_ref_idx_l1_default_active_minus1;

   result.curr_field_order_cnt_list[0] = h264_pic_info->pStdPictureInfo->PicOrderCnt[0];
   result.curr_field_order_cnt_list[1] = h264_pic_info->pStdPictureInfo->PicOrderCnt[1];

   result.frame_num = h264_pic_info->pStdPictureInfo->frame_num;

   result.num_ref_frames = sps->max_num_ref_frames;
   result.non_existing_frame_flags = 0;
   result.used_for_reference_flags = 0;

   memset(result.ref_frame_list, 0xff, sizeof(unsigned char) * 16);
   memset(result.frame_num_list, 0, sizeof(unsigned int) * 16);
   for (unsigned i = 0; i < frame_info->referenceSlotCount; i++) {
      int idx = frame_info->pReferenceSlots[i].slotIndex;
      const struct VkVideoDecodeH264DpbSlotInfoKHR *dpb_slot =
         vk_find_struct_const(frame_info->pReferenceSlots[i].pNext, VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR);

      result.frame_num_list[idx] = idx;
      result.field_order_cnt_list[idx][0] = dpb_slot->pStdReferenceInfo->PicOrderCnt[0];
      result.field_order_cnt_list[idx][1] = dpb_slot->pStdReferenceInfo->PicOrderCnt[1];

      result.ref_frame_list[idx] = idx;

      if (dpb_slot->pStdReferenceInfo->flags.top_field_flag)
         result.used_for_reference_flags |= (1 << (2 * idx));
      if (dpb_slot->pStdReferenceInfo->flags.bottom_field_flag)
         result.used_for_reference_flags |= (1 << (2 * idx + 1));

      if (!dpb_slot->pStdReferenceInfo->flags.top_field_flag &&
          !dpb_slot->pStdReferenceInfo->flags.bottom_field_flag)
         result.used_for_reference_flags |= (3 << (2 * idx));

      if (dpb_slot->pStdReferenceInfo->flags.used_for_long_term_reference)
        result.ref_frame_list[idx] |= 0x80;
      if (dpb_slot->pStdReferenceInfo->flags.is_non_existing)
         result.non_existing_frame_flags |= 1 << idx;

   }
   result.curr_pic_ref_frame_num = frame_info->referenceSlotCount;
   result.decoded_pic_idx = frame_info->pSetupReferenceSlot->slotIndex;

   return result;
}

static rvcn_dec_message_hevc_t get_h265_msg(struct radv_device *device,
                                            struct radv_video_session *vid,
                                            struct radv_video_session_params *params,
                                            const struct VkVideoDecodeInfoKHR *frame_info,
                                            void *it_ptr)
{
   rvcn_dec_message_hevc_t result;
   int i, j;
   const struct VkVideoDecodeH265PictureInfoKHR *h265_pic_info =
      vk_find_struct_const(frame_info->pNext, VIDEO_DECODE_H265_PICTURE_INFO_KHR);
   memset(&result, 0, sizeof(result));

   const StdVideoH265SequenceParameterSet *sps = vk_video_find_h265_dec_std_sps(&params->vk, h265_pic_info->pStdPictureInfo->sps_video_parameter_set_id);
   const StdVideoH265PictureParameterSet *pps = vk_video_find_h265_dec_std_pps(&params->vk, h265_pic_info->pStdPictureInfo->pps_pic_parameter_set_id);

   result.sps_info_flags = 0;
   result.sps_info_flags |= sps->flags.scaling_list_enabled_flag << 0;
   result.sps_info_flags |= sps->flags.amp_enabled_flag << 1;
   result.sps_info_flags |= sps->flags.sample_adaptive_offset_enabled_flag << 2;
   result.sps_info_flags |= sps->flags.pcm_enabled_flag << 3;
   result.sps_info_flags |= sps->flags.pcm_loop_filter_disabled_flag << 4;
   result.sps_info_flags |= sps->flags.long_term_ref_pics_present_flag << 5;
   result.sps_info_flags |= sps->flags.sps_temporal_mvp_enabled_flag << 6;
   result.sps_info_flags |= sps->flags.strong_intra_smoothing_enabled_flag << 7;
   result.sps_info_flags |= sps->flags.separate_colour_plane_flag << 8;

   if (device->physical_device->rad_info.family == CHIP_CARRIZO)
      result.sps_info_flags |= 1 << 9;

   result.chroma_format = sps->chroma_format_idc;
   result.bit_depth_luma_minus8 = sps->bit_depth_luma_minus8;
   result.bit_depth_chroma_minus8 = sps->bit_depth_chroma_minus8;
   result.log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_pic_order_cnt_lsb_minus4;
   result.sps_max_dec_pic_buffering_minus1 = sps->pDecPicBufMgr->max_dec_pic_buffering_minus1[0];
   result.log2_min_luma_coding_block_size_minus3 =
      sps->log2_min_luma_coding_block_size_minus3;
   result.log2_diff_max_min_luma_coding_block_size =
      sps->log2_diff_max_min_luma_coding_block_size;
   result.log2_min_transform_block_size_minus2 =
      sps->log2_min_luma_transform_block_size_minus2;
   result.log2_diff_max_min_transform_block_size =
      sps->log2_diff_max_min_luma_transform_block_size;
   result.max_transform_hierarchy_depth_inter = sps->max_transform_hierarchy_depth_inter;
   result.max_transform_hierarchy_depth_intra = sps->max_transform_hierarchy_depth_intra;
   if (sps->flags.pcm_enabled_flag) {
      result.pcm_sample_bit_depth_luma_minus1 = sps->pcm_sample_bit_depth_luma_minus1;
      result.pcm_sample_bit_depth_chroma_minus1 = sps->pcm_sample_bit_depth_chroma_minus1;
      result.log2_min_pcm_luma_coding_block_size_minus3 = sps->log2_min_pcm_luma_coding_block_size_minus3;
      result.log2_diff_max_min_pcm_luma_coding_block_size = sps->log2_diff_max_min_pcm_luma_coding_block_size;
   }
   result.num_short_term_ref_pic_sets = sps->num_short_term_ref_pic_sets;

   result.pps_info_flags = 0;
   result.pps_info_flags |= pps->flags.dependent_slice_segments_enabled_flag << 0;
   result.pps_info_flags |= pps->flags.output_flag_present_flag << 1;
   result.pps_info_flags |= pps->flags.sign_data_hiding_enabled_flag << 2;
   result.pps_info_flags |= pps->flags.cabac_init_present_flag << 3;
   result.pps_info_flags |= pps->flags.constrained_intra_pred_flag << 4;
   result.pps_info_flags |= pps->flags.transform_skip_enabled_flag << 5;
   result.pps_info_flags |= pps->flags.cu_qp_delta_enabled_flag << 6;
   result.pps_info_flags |= pps->flags.pps_slice_chroma_qp_offsets_present_flag << 7;
   result.pps_info_flags |= pps->flags.weighted_pred_flag << 8;
   result.pps_info_flags |= pps->flags.weighted_bipred_flag << 9;
   result.pps_info_flags |= pps->flags.transquant_bypass_enabled_flag << 10;
   result.pps_info_flags |= pps->flags.tiles_enabled_flag << 11;
   result.pps_info_flags |= pps->flags.entropy_coding_sync_enabled_flag << 12;
   result.pps_info_flags |= pps->flags.uniform_spacing_flag << 13;
   result.pps_info_flags |= pps->flags.loop_filter_across_tiles_enabled_flag << 14;
   result.pps_info_flags |= pps->flags.pps_loop_filter_across_slices_enabled_flag << 15;
   result.pps_info_flags |= pps->flags.deblocking_filter_override_enabled_flag << 16;
   result.pps_info_flags |= pps->flags.pps_deblocking_filter_disabled_flag << 17;
   result.pps_info_flags |= pps->flags.lists_modification_present_flag << 18;
   result.pps_info_flags |= pps->flags.slice_segment_header_extension_present_flag << 19;

   result.num_extra_slice_header_bits = pps->num_extra_slice_header_bits;
   result.num_long_term_ref_pic_sps = sps->num_long_term_ref_pics_sps;
   result.num_ref_idx_l0_default_active_minus1 = pps->num_ref_idx_l0_default_active_minus1;
   result.num_ref_idx_l1_default_active_minus1 = pps->num_ref_idx_l1_default_active_minus1;
   result.pps_cb_qp_offset = pps->pps_cb_qp_offset;
   result.pps_cr_qp_offset = pps->pps_cr_qp_offset;
   result.pps_beta_offset_div2 = pps->pps_beta_offset_div2;
   result.pps_tc_offset_div2 = pps->pps_tc_offset_div2;
   result.diff_cu_qp_delta_depth = pps->diff_cu_qp_delta_depth;
   result.num_tile_columns_minus1 = pps->num_tile_columns_minus1;
   result.num_tile_rows_minus1 = pps->num_tile_rows_minus1;
   result.log2_parallel_merge_level_minus2 = pps->log2_parallel_merge_level_minus2;
   result.init_qp_minus26 = pps->init_qp_minus26;

   for (i = 0; i < 19; ++i)
      result.column_width_minus1[i] = pps->column_width_minus1[i];

   for (i = 0; i < 21; ++i)
      result.row_height_minus1[i] = pps->row_height_minus1[i];

   result.num_delta_pocs_ref_rps_idx = h265_pic_info->pStdPictureInfo->NumDeltaPocsOfRefRpsIdx;
   result.curr_poc = h265_pic_info->pStdPictureInfo->PicOrderCntVal;

   memset(result.poc_list, 0, 16 * sizeof(int));
   memset(result.ref_pic_list, 0x7f, 16);
   for (i = 0; i < frame_info->referenceSlotCount; i++) {
      const struct VkVideoDecodeH265DpbSlotInfoKHR *dpb_slot =
         vk_find_struct_const(frame_info->pReferenceSlots[i].pNext, VIDEO_DECODE_H265_DPB_SLOT_INFO_KHR);
      int idx = frame_info->pReferenceSlots[i].slotIndex;
      result.poc_list[idx] = dpb_slot->pStdReferenceInfo->PicOrderCntVal;
      result.ref_pic_list[idx] = idx;
   }
   result.curr_idx = frame_info->pSetupReferenceSlot->slotIndex;

   for (i = 0; i < 8; ++i)
      result.ref_pic_set_st_curr_before[i] = h265_pic_info->pStdPictureInfo->RefPicSetStCurrBefore[i];

   for (i = 0; i < 8; ++i)
      result.ref_pic_set_st_curr_after[i] = h265_pic_info->pStdPictureInfo->RefPicSetStCurrAfter[i];

   for (i = 0; i < 8; ++i)
      result.ref_pic_set_lt_curr[i] = h265_pic_info->pStdPictureInfo->RefPicSetLtCurr[i];

   if (sps->flags.sps_scaling_list_data_present_flag) {
      for (i = 0; i < 6; ++i)
         result.ucScalingListDCCoefSizeID2[i] = sps->pScalingLists->ScalingListDCCoef16x16[i];

      for (i = 0; i < 2; ++i)
         result.ucScalingListDCCoefSizeID3[i] = sps->pScalingLists->ScalingListDCCoef32x32[i];

      memcpy(it_ptr, sps->pScalingLists->ScalingList4x4, 6 * 16);
      memcpy((char *)it_ptr + 96, sps->pScalingLists->ScalingList8x8, 6 * 64);
      memcpy((char *)it_ptr + 480, sps->pScalingLists->ScalingList16x16, 6 * 64);
      memcpy((char *)it_ptr + 864, sps->pScalingLists->ScalingList32x32, 2 * 64);
   }

   for (i = 0; i < 2; i++) {
      for (j = 0; j < 15; j++)
         result.direct_reflist[i][j] = 0xff;//pic->RefPicList[i][j];
   }

   if (vid->vk.h265.profile_idc == STD_VIDEO_H265_PROFILE_IDC_MAIN_10) {
      if (vid->vk.picture_format == VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16) {
         result.p010_mode = 1;
         result.msb_mode = 1;
      } else {
         result.p010_mode = 0;
         result.luma_10to8 = 5;
         result.chroma_10to8 = 5;
         result.hevc_reserved[0] = 4; /* sclr_luma10to8 */
         result.hevc_reserved[1] = 4; /* sclr_chroma10to8 */
      }
   }

   return result;
}

static bool rvcn_dec_message_decode(struct radv_cmd_buffer *cmd_buffer,
                                    struct radv_video_session *vid,
                                    struct radv_video_session_params *params,
                                    void *ptr,
                                    void *it_ptr,
                                    uint32_t *slice_offset,
                                    const struct VkVideoDecodeInfoKHR *frame_info)
{
   struct radv_device *device = cmd_buffer->device;
   rvcn_dec_message_header_t *header;
   rvcn_dec_message_index_t *index_codec;
   rvcn_dec_message_decode_t *decode;
   rvcn_dec_message_index_t *index_dynamic_dpb = NULL;
   rvcn_dec_message_dynamic_dpb_t2_t *dynamic_dpb_t2 = NULL;
   void *codec;
   unsigned sizes = 0, offset_decode, offset_codec, offset_dynamic_dpb;
   struct radv_image_view *dst_iv = radv_image_view_from_handle(frame_info->dstPictureResource.imageViewBinding);
   struct radv_image *img = dst_iv->image;
   struct radv_image_plane *luma = &img->planes[0];
   struct radv_image_plane *chroma = &img->planes[1];
   struct radv_image_view *dpb_iv = radv_image_view_from_handle(frame_info->pSetupReferenceSlot->pPictureResource->imageViewBinding);
   struct radv_image *dpb = dpb_iv->image;

   header = ptr;
   sizes += sizeof(rvcn_dec_message_header_t);

   index_codec = (void *)((char *)header + sizes);
   sizes += sizeof(rvcn_dec_message_index_t);

   if (vid->dpb_type == DPB_DYNAMIC_TIER_2) {
      index_dynamic_dpb = (void *)((char *)header + sizes);
      sizes += sizeof(rvcn_dec_message_index_t);
   }

   offset_decode = sizes;
   decode = (void *)((char*)header + sizes);
   sizes += sizeof(rvcn_dec_message_decode_t);

   if (vid->dpb_type == DPB_DYNAMIC_TIER_2) {
      offset_dynamic_dpb = sizes;
      dynamic_dpb_t2 = (void*)((char *)header + sizes);
      sizes += sizeof(rvcn_dec_message_dynamic_dpb_t2_t);
   }

   offset_codec = sizes;
   codec = (void *)((char *)header + sizes);

   memset(ptr, 0, sizes);

   header->header_size = sizeof(rvcn_dec_message_header_t);
   header->total_size = sizes;
   header->msg_type = RDECODE_MSG_DECODE;
   header->stream_handle = vid->stream_handle;
   header->status_report_feedback_number = vid->dbg_frame_cnt++;

   header->index[0].message_id = RDECODE_MESSAGE_DECODE;
   header->index[0].offset = offset_decode;
   header->index[0].size = sizeof(rvcn_dec_message_decode_t);
   header->index[0].filled = 0;
   header->num_buffers = 1;

   index_codec->offset = offset_codec;
   index_codec->size = sizeof(rvcn_dec_message_avc_t);
   index_codec->filled = 0;
   ++header->num_buffers;

   if (vid->dpb_type == DPB_DYNAMIC_TIER_2) {
      index_dynamic_dpb->message_id = RDECODE_MESSAGE_DYNAMIC_DPB;
      index_dynamic_dpb->offset = offset_dynamic_dpb;
      index_dynamic_dpb->filled = 0;
      ++header->num_buffers;
      index_dynamic_dpb->size = sizeof(rvcn_dec_message_dynamic_dpb_t2_t);
   }

   decode->stream_type = vid->stream_type;
   decode->decode_flags = 0;
   decode->width_in_samples = frame_info->dstPictureResource.codedExtent.width;
   decode->height_in_samples = frame_info->dstPictureResource.codedExtent.height;

   decode->bsd_size = frame_info->srcBufferRange;

   decode->dpb_size = (vid->dpb_type != DPB_DYNAMIC_TIER_2) ? dpb->size : 0;

   decode->dt_size = dst_iv->image->planes[0].surface.total_size +
      dst_iv->image->planes[1].surface.total_size;
   decode->sct_size = 0;
   decode->sc_coeff_size = 0;

   decode->sw_ctxt_size = RDECODE_SESSION_CONTEXT_SIZE;
   decode->db_pitch = align(frame_info->dstPictureResource.codedExtent.width, vid->db_alignment);

   if (device->physical_device->rad_info.family >= CHIP_NAVI21 &&
       (vid->stream_type == RDECODE_CODEC_H265 &&
        vid->vk.h265.profile_idc == STD_VIDEO_H265_PROFILE_IDC_MAIN_10))
      decode->db_aligned_height = align(frame_info->dstPictureResource.codedExtent.height, 64);

   decode->dt_pitch = luma->surface.u.gfx9.surf_pitch * luma->surface.blk_w;
   decode->dt_uv_pitch = chroma->surface.u.gfx9.surf_pitch * chroma->surface.blk_w;

   if (luma->surface.meta_offset) {
      fprintf(stderr, "DCC SURFACES NOT SUPPORTED.\n");
      return false;
   }

   decode->dt_tiling_mode = 0;
   decode->dt_swizzle_mode = luma->surface.u.gfx9.swizzle_mode;
   decode->dt_array_mode = RDECODE_ARRAY_MODE_LINEAR;
   decode->dt_field_mode = vid->interlaced ? 1 : 0;
   decode->dt_surf_tile_config = 0;
   decode->dt_uv_surf_tile_config = 0;

   decode->dt_luma_top_offset = luma->surface.u.gfx9.surf_offset;
   decode->dt_chroma_top_offset = chroma->surface.u.gfx9.surf_offset;

   if (decode->dt_field_mode) {
      decode->dt_luma_bottom_offset =
         luma->surface.u.gfx9.surf_offset + luma->surface.u.gfx9.surf_slice_size;
      decode->dt_chroma_bottom_offset =
         chroma->surface.u.gfx9.surf_offset + chroma->surface.u.gfx9.surf_slice_size;
   } else {
      decode->dt_luma_bottom_offset = decode->dt_luma_top_offset;
      decode->dt_chroma_bottom_offset = decode->dt_chroma_top_offset;
   }

   *slice_offset = 0;
   switch (vid->vk.op) {
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR: {
      rvcn_dec_message_avc_t avc = get_h264_msg(vid, params, frame_info, slice_offset, &decode->width_in_samples, &decode->height_in_samples, it_ptr);
      memcpy(codec, (void *)&avc, sizeof(rvcn_dec_message_avc_t));
      index_codec->message_id = RDECODE_MESSAGE_AVC;
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR: {
      rvcn_dec_message_hevc_t hevc = get_h265_msg(device, vid, params, frame_info, it_ptr);
      memcpy(codec, (void *)&hevc, sizeof(rvcn_dec_message_hevc_t));
      index_codec->message_id = RDECODE_MESSAGE_HEVC;
      break;
   }
   default:
      unreachable("unknown operation");
   }

   decode->hw_ctxt_size = vid->ctx.size;

   if (vid->dpb_type != DPB_DYNAMIC_TIER_2)
      return true;

   uint64_t addr;
   for (int i = 0; i < frame_info->referenceSlotCount; i++) {
      struct radv_image_view *f_dpb_iv = radv_image_view_from_handle(frame_info->pReferenceSlots[i].pPictureResource->imageViewBinding);
      struct radv_image *dpb_img = f_dpb_iv->image;
      int idx = frame_info->pReferenceSlots[i].slotIndex;

      radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs, dpb_img->bindings[0].bo);
      addr = radv_buffer_get_va(dpb_img->bindings[0].bo) + dpb_img->bindings[0].offset;

      dynamic_dpb_t2->dpbAddrLo[idx] = addr;
      dynamic_dpb_t2->dpbAddrHi[idx] = addr >> 32;
      ++dynamic_dpb_t2->dpbArraySize;
   }

   radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs, dpb->bindings[0].bo);
   addr = radv_buffer_get_va(dpb->bindings[0].bo);

   dynamic_dpb_t2->dpbCurrLo = addr;
   dynamic_dpb_t2->dpbCurrHi = addr >> 32;

   decode->decode_flags = 1;
   dynamic_dpb_t2->dpbConfigFlags = 0;
   dynamic_dpb_t2->dpbLumaPitch = luma->surface.u.gfx9.surf_pitch;
   dynamic_dpb_t2->dpbLumaAlignedHeight = luma->surface.u.gfx9.surf_height;
   dynamic_dpb_t2->dpbLumaAlignedSize = luma->surface.u.gfx9.surf_slice_size;

   dynamic_dpb_t2->dpbChromaPitch = chroma->surface.u.gfx9.surf_pitch;
   dynamic_dpb_t2->dpbChromaAlignedHeight = chroma->surface.u.gfx9.surf_height;
   dynamic_dpb_t2->dpbChromaAlignedSize = chroma->surface.u.gfx9.surf_slice_size;

   return true;
}

static struct ruvd_h264 get_uvd_h264_msg(struct radv_video_session *vid,
                                         struct radv_video_session_params *params,
                                         const struct VkVideoDecodeInfoKHR *frame_info,
                                         uint32_t *slice_offset,
                                         uint32_t *width_in_samples,
                                         uint32_t *height_in_samples,
                                         void *it_ptr)
{
   struct ruvd_h264 result;
   const struct VkVideoDecodeH264PictureInfoKHR *h264_pic_info =
      vk_find_struct_const(frame_info->pNext, VIDEO_DECODE_H264_PICTURE_INFO_KHR);

   *slice_offset = h264_pic_info->pSliceOffsets[0];

   memset(&result, 0, sizeof(result));

   const StdVideoH264SequenceParameterSet *sps = vk_video_find_h264_dec_std_sps(&params->vk, h264_pic_info->pStdPictureInfo->seq_parameter_set_id);
   switch (sps->profile_idc) {
   case STD_VIDEO_H264_PROFILE_IDC_BASELINE:
      result.profile = RUVD_H264_PROFILE_BASELINE;
      break;
   case STD_VIDEO_H264_PROFILE_IDC_MAIN:
      result.profile = RUVD_H264_PROFILE_MAIN;
      break;
   case STD_VIDEO_H264_PROFILE_IDC_HIGH:
      result.profile = RUVD_H264_PROFILE_HIGH;
      break;
   default:
      fprintf(stderr, "UNSUPPORTED CODEC %d\n", sps->profile_idc);
      result.profile = RUVD_H264_PROFILE_MAIN;
      break;
   }

   *width_in_samples = (sps->pic_width_in_mbs_minus1 + 1) * 16;
   *height_in_samples = (sps->pic_height_in_map_units_minus1 + 1) * 16;
   if (!sps->flags.frame_mbs_only_flag)
      *height_in_samples *= 2;
   result.level = get_h264_level(sps->level_idc);

   result.sps_info_flags = 0;

   result.sps_info_flags |= sps->flags.direct_8x8_inference_flag << 0;
   result.sps_info_flags |= sps->flags.mb_adaptive_frame_field_flag << 1;
   result.sps_info_flags |= sps->flags.frame_mbs_only_flag << 2;
   result.sps_info_flags |= sps->flags.delta_pic_order_always_zero_flag << 3;
   result.sps_info_flags |= 1 << RDECODE_SPS_INFO_H264_EXTENSION_SUPPORT_FLAG_SHIFT;

   result.bit_depth_luma_minus8 = sps->bit_depth_luma_minus8;
   result.bit_depth_chroma_minus8 = sps->bit_depth_chroma_minus8;
   result.log2_max_frame_num_minus4 = sps->log2_max_frame_num_minus4;
   result.pic_order_cnt_type = sps->pic_order_cnt_type;
   result.log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_pic_order_cnt_lsb_minus4;

   result.chroma_format = sps->chroma_format_idc;

   const StdVideoH264PictureParameterSet *pps = vk_video_find_h264_dec_std_pps(&params->vk, h264_pic_info->pStdPictureInfo->pic_parameter_set_id);
   result.pps_info_flags = 0;
   result.pps_info_flags |= pps->flags.transform_8x8_mode_flag << 0;
   result.pps_info_flags |= pps->flags.redundant_pic_cnt_present_flag << 1;
   result.pps_info_flags |= pps->flags.constrained_intra_pred_flag << 2;
   result.pps_info_flags |= pps->flags.deblocking_filter_control_present_flag << 3;
   result.pps_info_flags |= pps->weighted_bipred_idc << 4;
   result.pps_info_flags |= pps->flags.weighted_pred_flag << 6;
   result.pps_info_flags |= pps->flags.bottom_field_pic_order_in_frame_present_flag << 7;
   result.pps_info_flags |= pps->flags.entropy_coding_mode_flag << 8;

   result.pic_init_qp_minus26 = pps->pic_init_qp_minus26;
   result.chroma_qp_index_offset = pps->chroma_qp_index_offset;
   result.second_chroma_qp_index_offset = pps->second_chroma_qp_index_offset;

   if (pps->flags.pic_scaling_matrix_present_flag) {
      memcpy(result.scaling_list_4x4, pps->pScalingLists->ScalingList4x4, 6 * 16);
      memcpy(result.scaling_list_8x8[0], pps->pScalingLists->ScalingList8x8[0], 64);
      memcpy(result.scaling_list_8x8[1], pps->pScalingLists->ScalingList8x8[3], 64);
   } else if (sps->flags.seq_scaling_matrix_present_flag) {
      memcpy(result.scaling_list_4x4, sps->pScalingLists->ScalingList4x4, 6 * 16);
      memcpy(result.scaling_list_8x8[0], sps->pScalingLists->ScalingList8x8[0], 64);
      memcpy(result.scaling_list_8x8[1], sps->pScalingLists->ScalingList8x8[3], 64);
   } else {
      memset(result.scaling_list_4x4, 0x10, 6*16);
      memset(result.scaling_list_8x8, 0x10, 2*64);
   }

   memcpy(it_ptr, result.scaling_list_4x4, 6 * 16);
   memcpy((char *)it_ptr + 96, result.scaling_list_8x8, 2 * 64);

   result.num_ref_idx_l0_active_minus1 = pps->num_ref_idx_l0_default_active_minus1;
   result.num_ref_idx_l1_active_minus1 = pps->num_ref_idx_l1_default_active_minus1;

   result.curr_field_order_cnt_list[0] = h264_pic_info->pStdPictureInfo->PicOrderCnt[0];
   result.curr_field_order_cnt_list[1] = h264_pic_info->pStdPictureInfo->PicOrderCnt[1];

   result.frame_num = h264_pic_info->pStdPictureInfo->frame_num;

   result.num_ref_frames = sps->max_num_ref_frames;
   for (unsigned i = 0; i < frame_info->referenceSlotCount; i++) {
      int idx = frame_info->pReferenceSlots[i].slotIndex;
      const struct VkVideoDecodeH264DpbSlotInfoKHR *dpb_slot =
         vk_find_struct_const(frame_info->pReferenceSlots[i].pNext, VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR);

      result.frame_num_list[idx] = dpb_slot->pStdReferenceInfo->FrameNum;
      result.field_order_cnt_list[idx][0] = dpb_slot->pStdReferenceInfo->PicOrderCnt[0];
      result.field_order_cnt_list[idx][1] = dpb_slot->pStdReferenceInfo->PicOrderCnt[1];
   }
   result.decoded_pic_idx = frame_info->pSetupReferenceSlot->slotIndex;

   return result;
}

static struct ruvd_h265 get_uvd_h265_msg(struct radv_device *device,
                                         struct radv_video_session *vid,
                                         struct radv_video_session_params *params,
                                         const struct VkVideoDecodeInfoKHR *frame_info,
                                         void *it_ptr)
{
   struct ruvd_h265 result;
   int i, j;
   const struct VkVideoDecodeH265PictureInfoKHR *h265_pic_info =
      vk_find_struct_const(frame_info->pNext, VIDEO_DECODE_H265_PICTURE_INFO_KHR);

   memset(&result, 0, sizeof(result));

   const StdVideoH265SequenceParameterSet *sps = vk_video_find_h265_dec_std_sps(&params->vk, h265_pic_info->pStdPictureInfo->sps_video_parameter_set_id);
   const StdVideoH265PictureParameterSet *pps = vk_video_find_h265_dec_std_pps(&params->vk, h265_pic_info->pStdPictureInfo->pps_pic_parameter_set_id);

   result.sps_info_flags = 0;
   result.sps_info_flags |= sps->flags.scaling_list_enabled_flag << 0;
   result.sps_info_flags |= sps->flags.amp_enabled_flag << 1;
   result.sps_info_flags |= sps->flags.sample_adaptive_offset_enabled_flag << 2;
   result.sps_info_flags |= sps->flags.pcm_enabled_flag << 3;
   result.sps_info_flags |= sps->flags.pcm_loop_filter_disabled_flag << 4;
   result.sps_info_flags |= sps->flags.long_term_ref_pics_present_flag << 5;
   result.sps_info_flags |= sps->flags.sps_temporal_mvp_enabled_flag << 6;
   result.sps_info_flags |= sps->flags.strong_intra_smoothing_enabled_flag << 7;
   result.sps_info_flags |= sps->flags.separate_colour_plane_flag << 8;

   if (device->physical_device->rad_info.family == CHIP_CARRIZO)
      result.sps_info_flags |= 1 << 9;

   result.chroma_format = sps->chroma_format_idc;
   result.bit_depth_luma_minus8 = sps->bit_depth_luma_minus8;
   result.bit_depth_chroma_minus8 = sps->bit_depth_chroma_minus8;
   result.log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_pic_order_cnt_lsb_minus4;
   result.sps_max_dec_pic_buffering_minus1 = sps->pDecPicBufMgr->max_dec_pic_buffering_minus1[0];
   result.log2_min_luma_coding_block_size_minus3 =
      sps->log2_min_luma_coding_block_size_minus3;
   result.log2_diff_max_min_luma_coding_block_size =
      sps->log2_diff_max_min_luma_coding_block_size;
   result.log2_min_transform_block_size_minus2 =
      sps->log2_min_luma_transform_block_size_minus2;
   result.log2_diff_max_min_transform_block_size =
      sps->log2_diff_max_min_luma_transform_block_size;
   result.max_transform_hierarchy_depth_inter = sps->max_transform_hierarchy_depth_inter;
   result.max_transform_hierarchy_depth_intra = sps->max_transform_hierarchy_depth_intra;
   result.pcm_sample_bit_depth_luma_minus1 = sps->pcm_sample_bit_depth_luma_minus1;
   result.pcm_sample_bit_depth_chroma_minus1 = sps->pcm_sample_bit_depth_chroma_minus1;
   result.log2_min_pcm_luma_coding_block_size_minus3 =
      sps->log2_min_pcm_luma_coding_block_size_minus3;
   result.log2_diff_max_min_pcm_luma_coding_block_size =
      sps->log2_diff_max_min_pcm_luma_coding_block_size;
   result.num_short_term_ref_pic_sets = sps->num_short_term_ref_pic_sets;

   result.pps_info_flags = 0;
   result.pps_info_flags |= pps->flags.dependent_slice_segments_enabled_flag << 0;
   result.pps_info_flags |= pps->flags.output_flag_present_flag << 1;
   result.pps_info_flags |= pps->flags.sign_data_hiding_enabled_flag << 2;
   result.pps_info_flags |= pps->flags.cabac_init_present_flag << 3;
   result.pps_info_flags |= pps->flags.constrained_intra_pred_flag << 4;
   result.pps_info_flags |= pps->flags.transform_skip_enabled_flag << 5;
   result.pps_info_flags |= pps->flags.cu_qp_delta_enabled_flag << 6;
   result.pps_info_flags |= pps->flags.pps_slice_chroma_qp_offsets_present_flag << 7;
   result.pps_info_flags |= pps->flags.weighted_pred_flag << 8;
   result.pps_info_flags |= pps->flags.weighted_bipred_flag << 9;
   result.pps_info_flags |= pps->flags.transquant_bypass_enabled_flag << 10;
   result.pps_info_flags |= pps->flags.tiles_enabled_flag << 11;
   result.pps_info_flags |= pps->flags.entropy_coding_sync_enabled_flag << 12;
   result.pps_info_flags |= pps->flags.uniform_spacing_flag << 13;
   result.pps_info_flags |= pps->flags.loop_filter_across_tiles_enabled_flag << 14;
   result.pps_info_flags |= pps->flags.pps_loop_filter_across_slices_enabled_flag << 15;
   result.pps_info_flags |= pps->flags.deblocking_filter_override_enabled_flag << 16;
   result.pps_info_flags |= pps->flags.pps_deblocking_filter_disabled_flag << 17;
   result.pps_info_flags |= pps->flags.lists_modification_present_flag << 18;
   result.pps_info_flags |= pps->flags.slice_segment_header_extension_present_flag << 19;

   result.num_extra_slice_header_bits = pps->num_extra_slice_header_bits;
   result.num_long_term_ref_pic_sps = sps->num_long_term_ref_pics_sps;
   result.num_ref_idx_l0_default_active_minus1 = pps->num_ref_idx_l0_default_active_minus1;
   result.num_ref_idx_l1_default_active_minus1 = pps->num_ref_idx_l1_default_active_minus1;
   result.pps_cb_qp_offset = pps->pps_cb_qp_offset;
   result.pps_cr_qp_offset = pps->pps_cr_qp_offset;
   result.pps_beta_offset_div2 = pps->pps_beta_offset_div2;
   result.pps_tc_offset_div2 = pps->pps_tc_offset_div2;
   result.diff_cu_qp_delta_depth = pps->diff_cu_qp_delta_depth;
   result.num_tile_columns_minus1 = pps->num_tile_columns_minus1;
   result.num_tile_rows_minus1 = pps->num_tile_rows_minus1;
   result.log2_parallel_merge_level_minus2 = pps->log2_parallel_merge_level_minus2;
   result.init_qp_minus26 = pps->init_qp_minus26;

   for (i = 0; i < 19; ++i)
      result.column_width_minus1[i] = pps->column_width_minus1[i];

   for (i = 0; i < 21; ++i)
      result.row_height_minus1[i] = pps->row_height_minus1[i];

   result.num_delta_pocs_ref_rps_idx = h265_pic_info->pStdPictureInfo->NumDeltaPocsOfRefRpsIdx;
   result.curr_poc = h265_pic_info->pStdPictureInfo->PicOrderCntVal;

   memset(result.poc_list, 0, 16 * sizeof(int));
   memset(result.ref_pic_list, 0x7f, 16);
   for (i = 0; i < frame_info->referenceSlotCount; i++) {
      const struct VkVideoDecodeH265DpbSlotInfoKHR *dpb_slot =
         vk_find_struct_const(frame_info->pReferenceSlots[i].pNext, VIDEO_DECODE_H265_DPB_SLOT_INFO_KHR);
      int idx = frame_info->pReferenceSlots[i].slotIndex;
      result.poc_list[idx] = dpb_slot->pStdReferenceInfo->PicOrderCntVal;
      result.ref_pic_list[idx] = idx;
   }
   result.curr_idx = frame_info->pSetupReferenceSlot->slotIndex;

   for (i = 0; i < 8; ++i)
      result.ref_pic_set_st_curr_before[i] = h265_pic_info->pStdPictureInfo->RefPicSetStCurrBefore[i];

   for (i = 0; i < 8; ++i)
      result.ref_pic_set_st_curr_after[i] = h265_pic_info->pStdPictureInfo->RefPicSetStCurrAfter[i];

   for (i = 0; i < 8; ++i)
      result.ref_pic_set_lt_curr[i] = h265_pic_info->pStdPictureInfo->RefPicSetLtCurr[i];

   if (sps->flags.sps_scaling_list_data_present_flag) {
      for (i = 0; i < 6; ++i)
         result.ucScalingListDCCoefSizeID2[i] = sps->pScalingLists->ScalingListDCCoef16x16[i];

      for (i = 0; i < 2; ++i)
         result.ucScalingListDCCoefSizeID3[i] = sps->pScalingLists->ScalingListDCCoef32x32[i];

      memcpy(it_ptr, sps->pScalingLists->ScalingList4x4, 6 * 16);
      memcpy((char *)it_ptr + 96, sps->pScalingLists->ScalingList8x8, 6 * 64);
      memcpy((char *)it_ptr + 480, sps->pScalingLists->ScalingList16x16, 6 * 64);
      memcpy((char *)it_ptr + 864, sps->pScalingLists->ScalingList32x32, 2 * 64);
   }

   for (i = 0; i < 2; i++) {
      for (j = 0; j < 15; j++)
         result.direct_reflist[i][j] = 0xff;//pic->RefPicList[i][j];
   }

   if (vid->vk.h265.profile_idc == STD_VIDEO_H265_PROFILE_IDC_MAIN_10) {
      if (vid->vk.picture_format == VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16) {
         result.p010_mode = 1;
         result.msb_mode = 1;
      } else {
         result.p010_mode = 0;
         result.luma_10to8 = 5;
         result.chroma_10to8 = 5;
         result.sclr_luma10to8 = 4;
         result.sclr_chroma10to8 = 4;
      }
   }

   return result;
}

static unsigned texture_offset_legacy(struct radeon_surf *surface, unsigned layer)
{
   return (uint64_t)surface->u.legacy.level[0].offset_256B * 256 +
      layer * (uint64_t)surface->u.legacy.level[0].slice_size_dw * 4;
}

static bool ruvd_dec_message_decode(struct radv_device *device,
                                    struct radv_video_session *vid,
                                    struct radv_video_session_params *params,
                                    void *ptr,
                                    void *it_ptr,
                                    uint32_t *slice_offset,
                                    const struct VkVideoDecodeInfoKHR *frame_info)
{
   struct ruvd_msg *msg = ptr;
   struct radv_image_view *dst_iv = radv_image_view_from_handle(frame_info->dstPictureResource.imageViewBinding);
   struct radv_image *img = dst_iv->image;
   struct radv_image_plane *luma = &img->planes[0];
   struct radv_image_plane *chroma = &img->planes[1];
   struct radv_image_view *dpb_iv = radv_image_view_from_handle(frame_info->pSetupReferenceSlot->pPictureResource->imageViewBinding);
   struct radv_image *dpb = dpb_iv->image;

   memset(msg, 0, sizeof(struct ruvd_msg));
   msg->size = sizeof(*msg);
   msg->msg_type = RUVD_MSG_DECODE;
   msg->stream_handle = vid->stream_handle;
   msg->status_report_feedback_number = vid->dbg_frame_cnt++;

   msg->body.decode.stream_type = vid->stream_type;
   msg->body.decode.decode_flags = 0x1;
   msg->body.decode.width_in_samples = frame_info->dstPictureResource.codedExtent.width;
   msg->body.decode.height_in_samples = frame_info->dstPictureResource.codedExtent.height;

   msg->body.decode.dpb_size = (vid->dpb_type != DPB_DYNAMIC_TIER_2) ? dpb->size : 0;
   msg->body.decode.bsd_size = frame_info->srcBufferRange;
   msg->body.decode.db_pitch = align(frame_info->dstPictureResource.codedExtent.width, vid->db_alignment);

   if (vid->stream_type == RUVD_CODEC_H264_PERF &&
       device->physical_device->rad_info.family >= CHIP_POLARIS10)
      msg->body.decode.dpb_reserved = vid->ctx.size;

   *slice_offset = 0;
   switch (vid->vk.op) {
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR: {
      msg->body.decode.codec.h264 = get_uvd_h264_msg(vid, params, frame_info,
						     slice_offset,
						     &msg->body.decode.width_in_samples,
						     &msg->body.decode.height_in_samples,
						     it_ptr);
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR: {
      msg->body.decode.codec.h265 = get_uvd_h265_msg(device, vid, params, frame_info, it_ptr);

      if (vid->ctx.mem)
         msg->body.decode.dpb_reserved = vid->ctx.size;
      break;
   }
   default:
      return false;
   }

   msg->body.decode.dt_field_mode = false;

   if (device->physical_device->rad_info.gfx_level >= GFX9) {
      msg->body.decode.dt_pitch = luma->surface.u.gfx9.surf_pitch * luma->surface.blk_w;
      msg->body.decode.dt_tiling_mode = RUVD_TILE_LINEAR;
      msg->body.decode.dt_array_mode = RUVD_ARRAY_MODE_LINEAR;
      msg->body.decode.dt_luma_top_offset = luma->surface.u.gfx9.surf_offset;
      msg->body.decode.dt_chroma_top_offset = chroma->surface.u.gfx9.surf_offset;
      if (msg->body.decode.dt_field_mode) {
         msg->body.decode.dt_luma_bottom_offset =
            luma->surface.u.gfx9.surf_offset + luma->surface.u.gfx9.surf_slice_size;
         msg->body.decode.dt_chroma_bottom_offset =
            chroma->surface.u.gfx9.surf_offset + chroma->surface.u.gfx9.surf_slice_size;
      } else {
         msg->body.decode.dt_luma_bottom_offset = msg->body.decode.dt_luma_top_offset;
         msg->body.decode.dt_chroma_bottom_offset = msg->body.decode.dt_chroma_top_offset;
      }
      msg->body.decode.dt_surf_tile_config = 0;
   } else {
      msg->body.decode.dt_pitch = luma->surface.u.legacy.level[0].nblk_x * luma->surface.blk_w;
      switch (luma->surface.u.legacy.level[0].mode) {
      case RADEON_SURF_MODE_LINEAR_ALIGNED:
         msg->body.decode.dt_tiling_mode = RUVD_TILE_LINEAR;
         msg->body.decode.dt_array_mode = RUVD_ARRAY_MODE_LINEAR;
         break;
      case RADEON_SURF_MODE_1D:
         msg->body.decode.dt_tiling_mode = RUVD_TILE_8X8;
         msg->body.decode.dt_array_mode = RUVD_ARRAY_MODE_1D_THIN;
         break;
      case RADEON_SURF_MODE_2D:
         msg->body.decode.dt_tiling_mode = RUVD_TILE_8X8;
         msg->body.decode.dt_array_mode = RUVD_ARRAY_MODE_2D_THIN;
         break;
      default:
         assert(0);
         break;
      }

      msg->body.decode.dt_luma_top_offset = texture_offset_legacy(&luma->surface, 0);
      if (chroma)
         msg->body.decode.dt_chroma_top_offset = texture_offset_legacy(&chroma->surface, 0);
      if (msg->body.decode.dt_field_mode) {
         msg->body.decode.dt_luma_bottom_offset = texture_offset_legacy(&luma->surface, 1);
         if (chroma)
            msg->body.decode.dt_chroma_bottom_offset = texture_offset_legacy(&chroma->surface, 1);
      } else {
         msg->body.decode.dt_luma_bottom_offset = msg->body.decode.dt_luma_top_offset;
         msg->body.decode.dt_chroma_bottom_offset = msg->body.decode.dt_chroma_top_offset;
      }

      if (chroma) {
         assert(luma->surface.u.legacy.bankw == chroma->surface.u.legacy.bankw);
         assert(luma->surface.u.legacy.bankh == chroma->surface.u.legacy.bankh);
         assert(luma->surface.u.legacy.mtilea == chroma->surface.u.legacy.mtilea);
      }

      msg->body.decode.dt_surf_tile_config |= RUVD_BANK_WIDTH(util_logbase2(luma->surface.u.legacy.bankw));
      msg->body.decode.dt_surf_tile_config |= RUVD_BANK_HEIGHT(util_logbase2(luma->surface.u.legacy.bankh));
      msg->body.decode.dt_surf_tile_config |=
         RUVD_MACRO_TILE_ASPECT_RATIO(util_logbase2(luma->surface.u.legacy.mtilea));
   }

   if (device->physical_device->rad_info.family >= CHIP_STONEY)
      msg->body.decode.dt_wa_chroma_top_offset = msg->body.decode.dt_pitch / 2;

   msg->body.decode.db_surf_tile_config = msg->body.decode.dt_surf_tile_config;
   msg->body.decode.extension_support = 0x1;

   return true;
}

static void ruvd_dec_message_create(struct radv_video_session *vid,
                                    void *ptr)
{
   struct ruvd_msg *msg = ptr;

   memset(ptr, 0, sizeof(*msg));
   msg->size = sizeof(*msg);
   msg->msg_type = RUVD_MSG_CREATE;
   msg->stream_handle = vid->stream_handle;
   msg->body.create.stream_type = vid->stream_type;
   msg->body.create.width_in_samples = vid->vk.max_coded.width;
   msg->body.create.height_in_samples = vid->vk.max_coded.height;
}

void
radv_CmdBeginVideoCodingKHR(VkCommandBuffer commandBuffer,
                            const VkVideoBeginCodingInfoKHR *pBeginInfo)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   RADV_FROM_HANDLE(radv_video_session, vid, pBeginInfo->videoSession);
   RADV_FROM_HANDLE(radv_video_session_params, params, pBeginInfo->videoSessionParameters);

   cmd_buffer->video.vid = vid;
   cmd_buffer->video.params = params;
}

static void
radv_vcn_cmd_reset(struct radv_cmd_buffer *cmd_buffer)
{
   struct radv_video_session *vid = cmd_buffer->video.vid;
   uint32_t size = sizeof(rvcn_dec_message_header_t) + sizeof(rvcn_dec_message_create_t);

   void *ptr;
   uint32_t out_offset;
   radv_vid_buffer_upload_alloc(cmd_buffer, size, &out_offset,
                                &ptr);

   rvcn_dec_message_create(vid, ptr, size);
   send_cmd(cmd_buffer, RDECODE_CMD_SESSION_CONTEXT_BUFFER, vid->sessionctx.mem->bo, vid->sessionctx.offset);
   send_cmd(cmd_buffer, RDECODE_CMD_MSG_BUFFER, cmd_buffer->upload.upload_bo, out_offset);
   /* pad out the IB to the 16 dword boundary - otherwise the fw seems to be unhappy */
   for (unsigned i = 0; i < 8; i++)
      radeon_emit(cmd_buffer->cs, 0x81ff);
}

static void
radv_uvd_cmd_reset(struct radv_cmd_buffer *cmd_buffer)
{
   struct radv_video_session *vid = cmd_buffer->video.vid;
   uint32_t size = sizeof(struct ruvd_msg);
   void *ptr;
   uint32_t out_offset;
   radv_vid_buffer_upload_alloc(cmd_buffer, size, &out_offset,
                                &ptr);

   ruvd_dec_message_create(vid, ptr);
   if (vid->sessionctx.mem)
      send_cmd(cmd_buffer, RDECODE_CMD_SESSION_CONTEXT_BUFFER, vid->sessionctx.mem->bo, vid->sessionctx.offset);
   send_cmd(cmd_buffer, RDECODE_CMD_MSG_BUFFER, cmd_buffer->upload.upload_bo, out_offset);
   /* pad out the IB to the 16 dword boundary - otherwise the fw seems to be unhappy */
   for (unsigned i = 0; i < 8; i++)
      radeon_emit(cmd_buffer->cs, 0x81ff);
}

void
radv_CmdControlVideoCodingKHR(VkCommandBuffer commandBuffer,
                              const VkVideoCodingControlInfoKHR *pCodingControlInfo)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   if (pCodingControlInfo->flags & VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR) {
      if (radv_has_uvd(cmd_buffer->device->physical_device))
         radv_uvd_cmd_reset(cmd_buffer);
      else
         radv_vcn_cmd_reset(cmd_buffer);
   }
}

void
radv_CmdEndVideoCodingKHR(VkCommandBuffer commandBuffer,
                          const VkVideoEndCodingInfoKHR *pEndCodingInfo)
{
}

static void
radv_uvd_decode_video(struct radv_cmd_buffer *cmd_buffer,
                      const VkVideoDecodeInfoKHR *frame_info)
{
   RADV_FROM_HANDLE(radv_buffer, src_buffer, frame_info->srcBuffer);
   struct radv_video_session *vid = cmd_buffer->video.vid;
   struct radv_video_session_params *params = cmd_buffer->video.params;
   unsigned size = sizeof(struct ruvd_msg);
   void *ptr, *fb_ptr, *it_ptr = NULL;
   uint32_t out_offset, fb_offset, it_offset = 0;
   struct radeon_winsys_bo *msg_bo, *fb_bo, *it_bo = NULL;
   unsigned fb_size = (cmd_buffer->device->physical_device->rad_info.family == CHIP_TONGA) ? FB_BUFFER_SIZE_TONGA : FB_BUFFER_SIZE;

   radv_vid_buffer_upload_alloc(cmd_buffer, fb_size, &fb_offset,
                                &fb_ptr);
   fb_bo = cmd_buffer->upload.upload_bo;
   if (have_it(vid)) {
      radv_vid_buffer_upload_alloc(cmd_buffer, IT_SCALING_TABLE_SIZE, &it_offset,
                                   &it_ptr);
      it_bo = cmd_buffer->upload.upload_bo;
   }

   radv_vid_buffer_upload_alloc(cmd_buffer, size, &out_offset,
                                &ptr);
   msg_bo = cmd_buffer->upload.upload_bo;

   uint32_t slice_offset;
   ruvd_dec_message_decode(cmd_buffer->device, vid, params, ptr, it_ptr, &slice_offset, frame_info);
   rvcn_dec_message_feedback(fb_ptr);
   if (vid->sessionctx.mem)
      send_cmd(cmd_buffer, RDECODE_CMD_SESSION_CONTEXT_BUFFER, vid->sessionctx.mem->bo, vid->sessionctx.offset);
   send_cmd(cmd_buffer, RDECODE_CMD_MSG_BUFFER, msg_bo, out_offset);

   if (vid->dpb_type != DPB_DYNAMIC_TIER_2) {
      struct radv_image_view *dpb_iv = radv_image_view_from_handle(frame_info->pSetupReferenceSlot->pPictureResource->imageViewBinding);
      struct radv_image *dpb = dpb_iv->image;
      send_cmd(cmd_buffer, RDECODE_CMD_DPB_BUFFER, dpb->bindings[0].bo, dpb->bindings[0].offset);
   }

   if (vid->ctx.mem)
      send_cmd(cmd_buffer, RDECODE_CMD_CONTEXT_BUFFER, vid->ctx.mem->bo, vid->ctx.offset);

   send_cmd(cmd_buffer, RDECODE_CMD_BITSTREAM_BUFFER, src_buffer->bo, src_buffer->offset + frame_info->srcBufferOffset + slice_offset);

   struct radv_image_view *dst_iv = radv_image_view_from_handle(frame_info->dstPictureResource.imageViewBinding);
   struct radv_image *img = dst_iv->image;
   send_cmd(cmd_buffer, RDECODE_CMD_DECODING_TARGET_BUFFER, img->bindings[0].bo, img->bindings[0].offset);
   send_cmd(cmd_buffer, RDECODE_CMD_FEEDBACK_BUFFER, fb_bo, fb_offset);
   if (have_it(vid))
      send_cmd(cmd_buffer, RDECODE_CMD_IT_SCALING_TABLE_BUFFER, it_bo, it_offset);

   set_reg(cmd_buffer, cmd_buffer->device->physical_device->vid_dec_reg.cntl, 1);
}

static void
radv_vcn_decode_video(struct radv_cmd_buffer *cmd_buffer,
                      const VkVideoDecodeInfoKHR *frame_info)
{
   RADV_FROM_HANDLE(radv_buffer, src_buffer, frame_info->srcBuffer);
   struct radv_video_session *vid = cmd_buffer->video.vid;
   struct radv_video_session_params *params = cmd_buffer->video.params;
   unsigned size = 0;
   void *ptr, *fb_ptr, *it_ptr = NULL;
   uint32_t out_offset, fb_offset, it_offset = 0;
   struct radeon_winsys_bo *msg_bo, *fb_bo, *it_bo = NULL;

   size += sizeof(rvcn_dec_message_header_t); /* header */
   size += sizeof(rvcn_dec_message_index_t);  /* codec */
   if (vid->dpb_type == DPB_DYNAMIC_TIER_2) {
      size += sizeof(rvcn_dec_message_index_t);
      size += sizeof(rvcn_dec_message_dynamic_dpb_t2_t);
   }
   size += sizeof(rvcn_dec_message_decode_t);   /* decode */
   switch (vid->vk.op) {
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
      size += sizeof(rvcn_dec_message_avc_t);
      break;
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
      size += sizeof(rvcn_dec_message_hevc_t);
      break;
   default:
      unreachable("unsupported codec.");
   }

   radv_vid_buffer_upload_alloc(cmd_buffer, FB_BUFFER_SIZE, &fb_offset,
                                &fb_ptr);
   fb_bo = cmd_buffer->upload.upload_bo;
   if (have_it(vid)) {
      radv_vid_buffer_upload_alloc(cmd_buffer, IT_SCALING_TABLE_SIZE, &it_offset,
                                   &it_ptr);
      it_bo = cmd_buffer->upload.upload_bo;
   }

   radv_vid_buffer_upload_alloc(cmd_buffer, size, &out_offset,
                                &ptr);
   msg_bo = cmd_buffer->upload.upload_bo;

   uint32_t slice_offset;	  
   rvcn_dec_message_decode(cmd_buffer, vid, params, ptr, it_ptr, &slice_offset, frame_info);
   rvcn_dec_message_feedback(fb_ptr);
   send_cmd(cmd_buffer, RDECODE_CMD_SESSION_CONTEXT_BUFFER, vid->sessionctx.mem->bo, vid->sessionctx.offset);
   send_cmd(cmd_buffer, RDECODE_CMD_MSG_BUFFER, msg_bo, out_offset);

   if (vid->dpb_type != DPB_DYNAMIC_TIER_2) {
      struct radv_image_view *dpb_iv = radv_image_view_from_handle(frame_info->pSetupReferenceSlot->pPictureResource->imageViewBinding);
      struct radv_image *dpb = dpb_iv->image;
      send_cmd(cmd_buffer, RDECODE_CMD_DPB_BUFFER, dpb->bindings[0].bo, dpb->bindings[0].offset);
   }

   if (vid->ctx.mem)
      send_cmd(cmd_buffer, RDECODE_CMD_CONTEXT_BUFFER, vid->ctx.mem->bo, vid->ctx.offset);

   send_cmd(cmd_buffer, RDECODE_CMD_BITSTREAM_BUFFER, src_buffer->bo, src_buffer->offset + frame_info->srcBufferOffset + slice_offset);

   struct radv_image_view *dst_iv = radv_image_view_from_handle(frame_info->dstPictureResource.imageViewBinding);
   struct radv_image *img = dst_iv->image;
   send_cmd(cmd_buffer, RDECODE_CMD_DECODING_TARGET_BUFFER, img->bindings[0].bo, img->bindings[0].offset);
   send_cmd(cmd_buffer, RDECODE_CMD_FEEDBACK_BUFFER, fb_bo, fb_offset);
   if (have_it(vid))
      send_cmd(cmd_buffer, RDECODE_CMD_IT_SCALING_TABLE_BUFFER, it_bo, it_offset);

   set_reg(cmd_buffer, cmd_buffer->device->physical_device->vid_dec_reg.cntl, 1);
}

void
radv_CmdDecodeVideoKHR(VkCommandBuffer commandBuffer,
                       const VkVideoDecodeInfoKHR *frame_info)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

   if (radv_has_uvd(cmd_buffer->device->physical_device))
      radv_uvd_decode_video(cmd_buffer, frame_info);
   else
      radv_vcn_decode_video(cmd_buffer, frame_info);
}
