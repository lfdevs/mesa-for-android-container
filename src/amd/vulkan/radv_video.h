/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_VIDEO_H
#define RADV_VIDEO_H

#include "radv_event.h"
#include "vk_video.h"

#include "ac_vcn.h"

#define VL_MACROBLOCK_WIDTH  16
#define VL_MACROBLOCK_HEIGHT 16

struct radv_physical_device;
struct rvcn_sq_var;
struct radv_cmd_buffer;
struct radv_image_create_info;
struct radv_cmd_stream;

#define RADV_ENC_MAX_RATE_LAYER 4

#define RADV_BIND_SESSION_CTX          0
#define RADV_BIND_DECODER_CTX          1
#define RADV_BIND_INTRA_ONLY           2
#define RADV_BIND_ENCODE_QP_MAP        3
#define RADV_BIND_ENCODE_AV1_CDF_STORE RADV_BIND_DECODER_CTX

#define RADV_ENC_FEEDBACK_STATUS_IDX 10

#define RADV_VIDEO_H264_MAX_DPB_SLOTS 17

struct radv_vid_mem {
   struct radv_device_memory *mem;
   VkDeviceSize offset;
   VkDeviceSize size;
};

struct radv_video_session {
   struct vk_video_session vk;

   uint32_t stream_handle;
   unsigned stream_type;
   bool encode;
   enum { DPB_MAX_RES = 0, DPB_DYNAMIC_TIER_1, DPB_DYNAMIC_TIER_2, DPB_DYNAMIC_TIER_3 } dpb_type;
   unsigned db_alignment;

   struct radv_vid_mem sessionctx;
   struct radv_vid_mem ctx;
   struct radv_vid_mem qp_map;
   struct radv_image *intra_only_dpb;

   unsigned dbg_frame_cnt;
   uint32_t enc_standard;
   uint32_t enc_wa_flags;
   uint32_t enc_preset_mode;
};

/**
 *  WRITE_MEMORY support in FW.
 *
 *  none: Not supported at all. Old VCN FW and all UVD.
 *  pcie_atomics: Supported, relies on PCIe atomics.
 *  full: Supported, works also without PCIe atomics.
 */
enum radv_video_write_memory_support {
   RADV_VIDEO_WRITE_MEMORY_SUPPORT_NONE = 0,
   RADV_VIDEO_WRITE_MEMORY_SUPPORT_PCIE_ATOMICS,
   RADV_VIDEO_WRITE_MEMORY_SUPPORT_FULL,
};

VK_DEFINE_NONDISP_HANDLE_CASTS(radv_video_session, vk.base, VkVideoSessionKHR, VK_OBJECT_TYPE_VIDEO_SESSION_KHR)

void radv_init_physical_device_decoder(struct radv_physical_device *pdev);

void radv_video_get_profile_alignments(struct radv_physical_device *pdev, const VkVideoProfileListInfoKHR *profile_list,
                                       uint32_t *width_align_out, uint32_t *height_align_out);

void radv_vcn_sq_header(struct radv_cmd_stream *cs, struct rvcn_sq_var *sq, unsigned type);
void radv_vcn_sq_tail(struct radv_cmd_stream *cs, struct rvcn_sq_var *sq);
void radv_vcn_write_memory(struct radv_cmd_buffer *cmd_buffer, uint64_t va, unsigned value);

void radv_init_physical_device_encoder(struct radv_physical_device *pdevice);
void radv_probe_video_decode(struct radv_physical_device *pdev);
void radv_probe_video_encode(struct radv_physical_device *pdev);
void radv_video_enc_control_video_coding(struct radv_cmd_buffer *cmd_buffer,
                                         const VkVideoCodingControlInfoKHR *pCodingControlInfo);
void radv_video_enc_begin_video_coding(struct radv_cmd_buffer *cmd_buffer, const VkVideoBeginCodingInfoKHR *pBeginInfo);
VkResult radv_video_get_encode_session_memory_requirements(struct radv_device *device, struct radv_video_session *vid,
                                                           uint32_t *pMemoryRequirementsCount,
                                                           VkVideoSessionMemoryRequirementsKHR *pMemoryRequirements);
void radv_video_patch_encode_session_parameters(struct radv_device *device, struct vk_video_session_parameters *params);
void radv_video_get_enc_dpb_image(struct radv_device *device, const struct VkVideoProfileListInfoKHR *profile_list,
                                  struct radv_image *image, struct radv_image_create_info *create_info);
bool radv_video_decode_vp9_supported(const struct radv_physical_device *pdev);
bool radv_video_encode_av1_supported(const struct radv_physical_device *pdev);
bool radv_video_encode_qp_map_supported(const struct radv_physical_device *pdev);
enum radv_video_write_memory_support radv_video_write_memory_supported(const struct radv_physical_device *pdev);
uint32_t radv_video_get_qp_map_texel_size(VkVideoCodecOperationFlagBitsKHR codec);
bool radv_check_vcn_fw_version(const struct radv_physical_device *pdev, uint32_t dec, uint32_t enc, uint32_t rev);

#endif /* RADV_VIDEO_H */
