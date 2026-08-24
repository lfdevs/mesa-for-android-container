/*
 * Copyright © 2024 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#include <nvk_cmd_buffer.h>
#include <nvk_image_view.h>
#include <vulkan/vulkan_core.h>

void nvk_video_cmd_begin_video_coding_khr(struct nvk_cmd_buffer *cmd,
                                          const VkVideoBeginCodingInfoKHR *pBeginInfo);

void nvk_video_cmd_decode_video_khr(struct nvk_cmd_buffer *cmd,
                                    const struct VkVideoDecodeInfoKHR *frame_info,
                                    const struct nvk_buffer *src_buffer,
                                    struct nvk_image_view *dst_iv);

void nvk_video_create_video_session(struct nvk_video_session *pVideoSession);
void nvk_video_destroy_video_session(struct nvk_video_session *pVideoSession);
