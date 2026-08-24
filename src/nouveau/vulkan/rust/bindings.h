/*
 * Copyright © 2024 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#include "vulkan/runtime/vk_video.h"
#include "vulkan/util/vk_util.h"
#include "nouveau/headers/nv_push.h"
#include "nouveau/vulkan/nvk_device_memory.h"
#include "nouveau/vulkan/nvk_buffer.h"
#include "nouveau/vulkan/nvk_image_view.h"
#include "nouveau/vulkan/nvk_image.h"
#include "nouveau/vulkan/nvk_cmd_buffer.h"
#include "nouveau/vulkan/nvk_video_session.h"
#include "nouveau/headers/nvidia/video/nvdec_drv.h"
