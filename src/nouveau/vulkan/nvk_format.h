/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_FORMAT_H
#define NVK_FORMAT_H 1

#include "nvk_private.h"
#include "vk_format.h"

#include "util/format/u_formats.h"
#include "nv_device_info.h"

struct nvk_physical_device;

struct nvk_va_format {
   uint8_t bit_widths;
   uint8_t swap_rb:1;
   uint8_t type:7;
};

bool
nvk_format_supports_atomics(const struct nv_device_info *dev,
                            enum pipe_format p_format);

const struct nvk_va_format *
nvk_get_va_format(const struct nvk_physical_device *pdev, VkFormat format);

static inline enum pipe_format
nvk_format_to_pipe_format(enum VkFormat vkformat)
{
   switch (vkformat) {
   case VK_FORMAT_R10X6_UNORM_PACK16:
   case VK_FORMAT_R12X4_UNORM_PACK16:
      return PIPE_FORMAT_R16_UNORM;
   case VK_FORMAT_R10X6G10X6_UNORM_2PACK16:
   case VK_FORMAT_R12X4G12X4_UNORM_2PACK16:
      return PIPE_FORMAT_R16G16_UNORM;
   default:
      return vk_format_to_pipe_format(vkformat);
   }
}

#endif
