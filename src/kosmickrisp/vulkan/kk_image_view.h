/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_IMAGE_VIEW_H
#define KK_IMAGE_VIEW_H 1

#include "kk_private.h"

#include "kosmickrisp/bridge/mtl_types.h"

#include "util/format/u_formats.h"

#include "vk_image.h"

struct kk_device;

struct kk_image_view {
   struct vk_image_view vk;

   uint8_t plane_count;
   struct {
      uint8_t image_plane;

      enum pipe_format format;

      mtl_texture *mtl_handle_sampled;
      mtl_texture
         *mtl_handle_storage; // TODO_KOSMICKRISP We can probably get rid of
                              // this once we lower 2D cubes and 3D to 2D array?

      /* Cached handle so we don't have to retrieve it from the image when we
       * render */
      mtl_texture *mtl_handle_render;

      /* Input attachment handle. Required since input attachments needs to be
       * arrays, and sampled may not be */
      mtl_texture *mtl_handle_input;

      uint64_t sampled_gpu_resource_id;
      uint64_t storage_gpu_resource_id;
      uint64_t input_gpu_resource_id;
   } planes[3];
};

VK_DEFINE_NONDISP_HANDLE_CASTS(kk_image_view, vk.base, VkImageView,
                               VK_OBJECT_TYPE_IMAGE_VIEW)

VkResult kk_image_view_init(struct kk_device *dev, struct kk_image_view *view,
                            const VkImageViewCreateInfo *pCreateInfo);

void kk_image_view_finish(struct kk_device *dev, struct kk_image_view *view);

#endif /* KK_IMAGE_VIEW_H */
