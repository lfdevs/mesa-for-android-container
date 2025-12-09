/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_FORMAT_H
#define KK_FORMAT_H 1

#include "kk_private.h"

#include "util/format/u_format.h"

struct kk_physical_device;
enum pipe_format;
enum mtl_pixel_format;

struct kk_va_format {
   /* Transforms from the native format to the emulated format.
    * Would love to use enum pipe_swizzle, but it's bigger than the required
    * type for util_format_compose_swizzles... */
   struct {
      union {
         struct {
            uint8_t red;
            uint8_t green;
            uint8_t blue;
            uint8_t alpha;
         };
         uint8_t channels[4];
      };
   } unswizzle;
   uint32_t mtl_pixel_format;
   uint8_t bit_widths;
   uint8_t filter  : 1;
   uint8_t write   : 1;
   uint8_t color   : 1;
   uint8_t blend   : 1;
   uint8_t msaa    : 1;
   uint8_t resolve : 1;
   uint8_t sparse  : 1;
   uint8_t atomic  : 1;
   struct {
      uint8_t write      : 1;
      uint8_t read       : 1;
      uint8_t read_write : 1;
   } texel_buffer;
   uint8_t is_native : 1;
};

const struct kk_va_format *kk_get_va_format(enum pipe_format format);

enum mtl_pixel_format vk_format_to_mtl_pixel_format(enum VkFormat vkformat);

#endif /* KK_FORMAT_H */
