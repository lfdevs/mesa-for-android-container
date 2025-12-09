/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_IMAGE_LAYOUT_H
#define KK_IMAGE_LAYOUT_H 1

#include "kosmickrisp/bridge/mtl_types.h"

#include "util/format/u_formats.h"

#include "vulkan/vulkan.h"

#define KK_MAX_MIP_LEVELS 16

struct kk_device;
struct VkImageCreateInfo;
enum pipe_swizzle;

struct kk_image_layout {
   /** Width, height, and depth in pixels at level 0 */
   uint32_t width_px, height_px, depth_px, layers;

   enum mtl_texture_type type;

   /** Number of samples per pixel. 1 if multisampling is disabled. */
   uint8_t sample_count_sa;

   /** Number of miplevels. 1 if no mipmapping is used. */
   uint8_t levels;

   uint8_t optimized_layout;

   enum mtl_texture_usage usage;

   /** Texture format */
   struct {
      enum pipe_format pipe;
      uint32_t mtl;
   } format;

   /* Required to correctly set image swizzle for non-native formats */
   /* Would love to use enum pipe_swizzle, but it's bigger than the required
    * type for util_format_compose_swizzles... */
   struct {
      uint8_t red;
      uint8_t green;
      uint8_t blue;
      uint8_t alpha;
   } swizzle;

   /**
    * If tiling is LINEAR, the number of bytes between adjacent rows of
    * elements. Otherwise, this field is zero.
    */
   uint32_t linear_stride_B;

   /**
    * Stride between layers of an array texture, including a cube map. Layer i
    * begins at offset (i * layer_stride_B) from the beginning of the texture.
    *
    * If depth_px = 1, the value of this field is UNDEFINED.
    */
   uint64_t layer_stride_B;

   /**
    * Offsets of mip levels within a layer.
    */
   uint64_t level_offsets_B[KK_MAX_MIP_LEVELS];

   /**
    * If tiling is TWIDDLED, the stride in elements used for each mip level
    * within a layer. Calculating level strides is the sole responsibility of
    * ail_initialized_twiddled. This is necessary because compressed pixel
    * formats may add extra stride padding.
    */
   uint32_t stride_el[KK_MAX_MIP_LEVELS];

   /* Size of entire texture */
   uint64_t size_B;

   /* Alignment required */
   uint64_t align_B;
};

struct kk_view_layout {
   /** Type */
   VkImageViewType view_type;

   /** Number of samples per pixel. 1 if multisampling is disabled.
    * Required to be able to correctly set the MTLTextureType.
    */
   uint8_t sample_count_sa;

   /** Texture format */
   struct {
      enum pipe_format pipe;
      uint32_t mtl;
   } format;

   /** Array base level. 0 if no array is used. */
   uint16_t base_array_layer;

   /** Array length. 1 if no array is used. */
   uint16_t array_len;

   /** Swizzle */
   /* Would love to use enum pipe_swizzle, but it's bigger than the required
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
   } swizzle;

   /** Mipmap base level. 0 if no mipmapping is used. */
   uint8_t base_level;

   /** Number of miplevels. 1 if no mipmapping is used. */
   uint8_t num_levels;

   uint16_t min_lod_clamp;
};

void kk_image_layout_init(const struct kk_device *dev,
                          const struct VkImageCreateInfo *create_info,
                          enum pipe_format format, const uint8_t width_scale,
                          const uint8_t height_scale,
                          struct kk_image_layout *layout);

#endif /* KK_IMAGE_LAYOUT_H */
