/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#ifndef TU_FORMATS_H
#define TU_FORMATS_H

#include <stdbool.h>

#include "util/format/u_format.h"
#include "vulkan/vulkan_core.h"

#include "common/fd6_hw.h"

struct tu_native_format
{
   enum a6xx_format fmt : 8;
   enum a3xx_color_swap swap : 8;
};

struct tu_native_format tu6_format_vtx(enum pipe_format format);
struct tu_native_format tu6_format_color(enum pipe_format format, enum a6xx_tile_mode tile_mode,
                                         bool is_mutable);
struct tu_native_format tu6_format_texture(enum pipe_format format, enum a6xx_tile_mode tile_mode,
                                           bool is_mutable);

bool tu6_mutable_format_list_ubwc_compatible(const struct fd_dev_info *info,
                                             const VkImageFormatListCreateInfo *fmt_list);

#endif /* TU_FORMATS_H */
