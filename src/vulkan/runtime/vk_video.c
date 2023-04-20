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

#include "vk_video.h"
#include "vk_util.h"
#include "vk_log.h"
#include "vk_alloc.h"
#include "vk_device.h"

VkResult
vk_video_session_init(struct vk_device *device,
                      struct vk_video_session *vid,
                      const VkVideoSessionCreateInfoKHR *create_info)
{
   vk_object_base_init(device, &vid->base, VK_OBJECT_TYPE_VIDEO_SESSION_KHR);

   vid->op = create_info->pVideoProfile->videoCodecOperation;
   vid->max_coded = create_info->maxCodedExtent;
   vid->picture_format = create_info->pictureFormat;
   vid->ref_format = create_info->referencePictureFormat;
   vid->max_dpb_slots = create_info->maxDpbSlots;
   vid->max_active_ref_pics = create_info->maxActiveReferencePictures;

   switch (vid->op) {
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR: {
      const struct VkVideoDecodeH264ProfileInfoKHR *h264_profile =
         vk_find_struct_const(create_info->pVideoProfile->pNext,
                              VIDEO_DECODE_H264_PROFILE_INFO_KHR);
      vid->h264.profile_idc = h264_profile->stdProfileIdc;
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR: {
      const struct VkVideoDecodeH265ProfileInfoKHR *h265_profile =
         vk_find_struct_const(create_info->pVideoProfile->pNext,
                              VIDEO_DECODE_H265_PROFILE_INFO_KHR);
      vid->h265.profile_idc = h265_profile->stdProfileIdc;
      break;
   }
   default:
      return VK_ERROR_FEATURE_NOT_PRESENT;
   }

   return VK_SUCCESS;
}

#define FIND(PARAMSET, SS, SET, ID)                                     \
   static PARAMSET *find_##SS##_##SET(const struct vk_video_session_parameters *params, uint32_t id) { \
      for (unsigned i = 0; i < params->SS.SET##_count; i++) {           \
         if (params->SS.SET[i].ID == id)                                \
            return &params->SS.SET[i];                                  \
      }                                                                 \
      return NULL;                                                      \
   }                                                                    \
                                                                        \
   static void add_##SS##_##SET(struct vk_video_session_parameters *params, \
                                const PARAMSET *new_set, bool noreplace) {  \
      PARAMSET *set = find_##SS##_##SET(params, new_set->ID);           \
      if (set) {                                                        \
	 if (noreplace)                                                 \
            return;                                                     \
         *set = *new_set;                                               \
      } else                                                            \
         params->SS.SET[params->SS.SET##_count++] = *new_set;           \
   }                                                                    \
                                                                        \
   static VkResult update_##SS##_##SET(struct vk_video_session_parameters *params, \
                                       uint32_t count, const PARAMSET *updates) { \
      if (params->SS.SET##_count + count >= params->SS.max_##SET##_count) \
         return VK_ERROR_TOO_MANY_OBJECTS;                              \
      typed_memcpy(&params->SS.SET[params->SS.SET##_count], updates, count); \
      params->SS.SET##_count += count;                                  \
      return VK_SUCCESS;                                                \
   }

FIND(StdVideoH264SequenceParameterSet, h264_dec, std_sps, seq_parameter_set_id)
FIND(StdVideoH264PictureParameterSet, h264_dec, std_pps, pic_parameter_set_id)
FIND(StdVideoH265VideoParameterSet, h265_dec, std_vps, vps_video_parameter_set_id)
FIND(StdVideoH265SequenceParameterSet, h265_dec, std_sps, sps_seq_parameter_set_id)
FIND(StdVideoH265PictureParameterSet, h265_dec, std_pps, pps_pic_parameter_set_id)

static void
init_add_h264_session_parameters(struct vk_video_session_parameters *params,
                                 const struct VkVideoDecodeH264SessionParametersAddInfoKHR *h264_add,
                                 const struct vk_video_session_parameters *templ)
{
   unsigned i;

   if (h264_add) {
      for (i = 0; i < h264_add->stdSPSCount; i++) {
         add_h264_dec_std_sps(params, &h264_add->pStdSPSs[i], false);
      }
   }
   if (templ) {
      for (i = 0; i < templ->h264_dec.std_sps_count; i++) {
         add_h264_dec_std_sps(params, &templ->h264_dec.std_sps[i], true);
      }
   }

   if (h264_add) {
      for (i = 0; i < h264_add->stdPPSCount; i++) {
         add_h264_dec_std_pps(params, &h264_add->pStdPPSs[i], false);
      }
   }
   if (templ) {
      for (i = 0; i < templ->h264_dec.std_pps_count; i++) {
         add_h264_dec_std_pps(params, &templ->h264_dec.std_pps[i], true);
      }
   }
}

static void
init_add_h265_session_parameters(struct vk_video_session_parameters *params,
                                 const struct VkVideoDecodeH265SessionParametersAddInfoKHR *h265_add,
                                 const struct vk_video_session_parameters *templ)
{
   unsigned i;

   if (h265_add) {
      for (i = 0; i < h265_add->stdVPSCount; i++) {
         add_h265_dec_std_vps(params, &h265_add->pStdVPSs[i], false);
      }
   }
   if (templ) {
      for (i = 0; i < templ->h265_dec.std_vps_count; i++) {
         add_h265_dec_std_vps(params, &templ->h265_dec.std_vps[i], true);
      }
   }
   if (h265_add) {
      for (i = 0; i < h265_add->stdSPSCount; i++) {
         add_h265_dec_std_sps(params, &h265_add->pStdSPSs[i], false);
      }
   }
   if (templ) {
      for (i = 0; i < templ->h265_dec.std_sps_count; i++) {
         add_h265_dec_std_sps(params, &templ->h265_dec.std_sps[i], true);
      }
   }

   if (h265_add) {
      for (i = 0; i < h265_add->stdPPSCount; i++) {
         add_h265_dec_std_pps(params, &h265_add->pStdPPSs[i], false);
      }
   }
   if (templ) {
      for (i = 0; i < templ->h265_dec.std_pps_count; i++) {
         add_h265_dec_std_pps(params, &templ->h265_dec.std_pps[i], true);
      }
   }
}

VkResult
vk_video_session_parameters_init(struct vk_device *device,
                                 struct vk_video_session_parameters *params,
                                 const struct vk_video_session *vid,
                                 const struct vk_video_session_parameters *templ,
                                 const VkVideoSessionParametersCreateInfoKHR *create_info)
{
   memset(params, 0, sizeof(*params));
   vk_object_base_init(device, &params->base, VK_OBJECT_TYPE_VIDEO_SESSION_PARAMETERS_KHR);

   params->op = vid->op;

   switch (vid->op) {
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR: {
      const struct VkVideoDecodeH264SessionParametersCreateInfoKHR *h264_create =
         vk_find_struct_const(create_info->pNext, VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR);

      params->h264_dec.max_std_sps_count = h264_create->maxStdSPSCount;
      params->h264_dec.max_std_pps_count = h264_create->maxStdPPSCount;

      uint32_t sps_size = params->h264_dec.max_std_sps_count * sizeof(StdVideoH264SequenceParameterSet);
      uint32_t pps_size = params->h264_dec.max_std_pps_count * sizeof(StdVideoH264PictureParameterSet);

      params->h264_dec.std_sps = vk_alloc(&device->alloc, sps_size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      params->h264_dec.std_pps = vk_alloc(&device->alloc, pps_size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!params->h264_dec.std_sps || !params->h264_dec.std_pps) {
         vk_free(&device->alloc, params->h264_dec.std_sps);
         vk_free(&device->alloc, params->h264_dec.std_pps);
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      }

      init_add_h264_session_parameters(params, h264_create->pParametersAddInfo, templ);
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR: {
      const struct VkVideoDecodeH265SessionParametersCreateInfoKHR *h265_create =
         vk_find_struct_const(create_info->pNext, VIDEO_DECODE_H265_SESSION_PARAMETERS_CREATE_INFO_KHR);

      params->h265_dec.max_std_vps_count = h265_create->maxStdVPSCount;
      params->h265_dec.max_std_sps_count = h265_create->maxStdSPSCount;
      params->h265_dec.max_std_pps_count = h265_create->maxStdPPSCount;

      uint32_t vps_size = params->h265_dec.max_std_vps_count * sizeof(StdVideoH265VideoParameterSet);
      uint32_t sps_size = params->h265_dec.max_std_sps_count * sizeof(StdVideoH265SequenceParameterSet);
      uint32_t pps_size = params->h265_dec.max_std_pps_count * sizeof(StdVideoH265PictureParameterSet);

      params->h265_dec.std_vps = vk_alloc(&device->alloc, vps_size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      params->h265_dec.std_sps = vk_alloc(&device->alloc, sps_size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      params->h265_dec.std_pps = vk_alloc(&device->alloc, pps_size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!params->h265_dec.std_sps || !params->h265_dec.std_pps || !params->h265_dec.std_vps) {
         vk_free(&device->alloc, params->h265_dec.std_vps);
         vk_free(&device->alloc, params->h265_dec.std_sps);
         vk_free(&device->alloc, params->h265_dec.std_pps);
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      }

      init_add_h265_session_parameters(params, h265_create->pParametersAddInfo, templ);
      break;
   }
   default:
      unreachable("Unsupported video codec operation");
      break;
   }
   return VK_SUCCESS;
}

void
vk_video_session_parameters_finish(struct vk_device *device,
                                   struct vk_video_session_parameters *params)
{
   switch (params->op) {
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
      vk_free(&device->alloc, params->h264_dec.std_sps);
      vk_free(&device->alloc, params->h264_dec.std_pps);
      break;
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
      vk_free(&device->alloc, params->h265_dec.std_vps);
      vk_free(&device->alloc, params->h265_dec.std_sps);
      vk_free(&device->alloc, params->h265_dec.std_pps);
      break;
   default:
      break;
   }
   vk_object_base_finish(&params->base);
}

static VkResult
update_sps(struct vk_video_session_parameters *params,
           uint32_t count, const StdVideoH264SequenceParameterSet *adds)
{
    if (params->h264_dec.std_sps_count + count >= params->h264_dec.max_std_sps_count)
      return VK_ERROR_TOO_MANY_OBJECTS;

   typed_memcpy(&params->h264_dec.std_sps[params->h264_dec.std_sps_count], adds, count);
   params->h264_dec.std_sps_count += count;
   return VK_SUCCESS;
}

static VkResult
update_h264_session_parameters(struct vk_video_session_parameters *params,
                               const struct VkVideoDecodeH264SessionParametersAddInfoKHR *h264_add)
{
   VkResult result = VK_SUCCESS;

   result = update_h264_dec_std_sps(params, h264_add->stdSPSCount, h264_add->pStdSPSs);
   if (result != VK_SUCCESS)
      return result;

   result = update_h264_dec_std_pps(params, h264_add->stdPPSCount, h264_add->pStdPPSs);
   return result;
}

static VkResult
update_h265_session_parameters(struct vk_video_session_parameters *params,
                               const struct VkVideoDecodeH265SessionParametersAddInfoKHR *h265_add)
{
   VkResult result = VK_SUCCESS;
   result = update_h265_dec_std_vps(params, h265_add->stdVPSCount, h265_add->pStdVPSs);
   if (result != VK_SUCCESS)
      return result;

   result = update_h265_dec_std_sps(params, h265_add->stdSPSCount, h265_add->pStdSPSs);
   if (result != VK_SUCCESS)
      return result;

   result = update_h265_dec_std_pps(params, h265_add->stdPPSCount, h265_add->pStdPPSs);
   return result;
}

VkResult
vk_video_session_parameters_update(struct vk_video_session_parameters *params,
                                   const VkVideoSessionParametersUpdateInfoKHR *update)
{
   /* 39.6.5. Decoder Parameter Sets -
    * "The provided H.264 SPS/PPS parameters must be within the limits specified during decoder
    * creation for the decoder specified in VkVideoSessionParametersCreateInfoKHR."
    */

   /*
    * There is no need to deduplicate here.
    * videoSessionParameters must not already contain a StdVideoH264PictureParameterSet entry with
    * both seq_parameter_set_id and pic_parameter_set_id matching any of the elements of
    * VkVideoDecodeH264SessionParametersAddInfoKHR::pStdPPS
    */
   VkResult result = VK_SUCCESS;

   switch (params->op) {
   case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR: {
      const struct VkVideoDecodeH264SessionParametersAddInfoKHR *h264_add =
         vk_find_struct_const(update->pNext, VIDEO_DECODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR);
      return update_h264_session_parameters(params, h264_add);
   }
   case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR: {
      const struct VkVideoDecodeH265SessionParametersAddInfoKHR *h265_add =
         vk_find_struct_const(update->pNext, VIDEO_DECODE_H265_SESSION_PARAMETERS_ADD_INFO_KHR);

      return update_h265_session_parameters(params, h265_add);
   }
   default:
      unreachable("Unknown codec\n");
   }
   return result;
}

const StdVideoH264SequenceParameterSet *
vk_video_find_h264_dec_std_sps(const struct vk_video_session_parameters *params,
                               uint32_t id)
{
   return find_h264_dec_std_sps(params, id);
}

const StdVideoH264PictureParameterSet *
vk_video_find_h264_dec_std_pps(const struct vk_video_session_parameters *params,
                               uint32_t id)
{
   return find_h264_dec_std_pps(params, id);
}

const StdVideoH265VideoParameterSet *
vk_video_find_h265_dec_std_vps(const struct vk_video_session_parameters *params,
                               uint32_t id)
{
   return find_h265_dec_std_vps(params, id);
}

const StdVideoH265SequenceParameterSet *
vk_video_find_h265_dec_std_sps(const struct vk_video_session_parameters *params,
                               uint32_t id)
{
   return find_h265_dec_std_sps(params, id);
}

const StdVideoH265PictureParameterSet *
vk_video_find_h265_dec_std_pps(const struct vk_video_session_parameters *params,
                               uint32_t id)
{
   return find_h265_dec_std_pps(params, id);
}
