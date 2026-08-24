/*
 * Copyright © 2023 Collabora, Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_cmd_buffer.h"

#include "nvk_buffer.h"
#include "nvk_entrypoints.h"
#include "nvk_image.h"
#include "nvk_image_view.h"
#include "nvk_rust.h"
#include "nvk_video_session.h"

#include "nv_push_cl906f.h"

#include "nvidia/video/nvdec_drv.h"

VKAPI_ATTR void VKAPI_CALL
nvk_CmdBeginVideoCodingKHR(VkCommandBuffer commandBuffer,
                           const VkVideoBeginCodingInfoKHR *pBeginInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvk_video_session, vid, pBeginInfo->videoSession);
   VK_FROM_HANDLE(vk_video_session_parameters, params,
                  pBeginInfo->videoSessionParameters);

   cmd->state.video.vid = vid;
   cmd->state.video.params = params;

   nvk_video_cmd_begin_video_coding_khr(cmd, pBeginInfo);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdControlVideoCodingKHR(VkCommandBuffer commandBuffer,
                             const VkVideoCodingControlInfoKHR *pCodingControlInfo)
{
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdEndVideoCodingKHR(VkCommandBuffer commandBuffer,
                         const VkVideoEndCodingInfoKHR *pEndCodingInfo)
{
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdDecodeVideoKHR(VkCommandBuffer commandBuffer,
                      const VkVideoDecodeInfoKHR *frame_info)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   VkResult result = nvk_video_cmd_decode_video_khr(cmd, frame_info);
   if (unlikely(result != VK_SUCCESS))
      vk_command_buffer_set_error(&cmd->vk, result);
}
