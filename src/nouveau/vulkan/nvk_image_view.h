/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_IMAGE_VIEW_H
#define NVK_IMAGE_VIEW_H 1

#include "nvk_descriptor_types.h"

#include "vk_image.h"

#include "nil.h"

struct nvk_device;

struct nvk_image_view_descriptor {
   /* The actual descriptor data which gets copied into the heap */
   union nvk_image_descriptor desc;

   /** Index in the image descriptor table, else 0 */
   uint32_t desc_index;
};

struct nvk_image_view {
   struct vk_image_view vk;

   bool separate_zs;

   uint8_t plane_count;
   struct {
      uint8_t image_plane;

      nil_sample_layout sample_layout;

      struct nvk_image_view_descriptor sampled;
      struct nvk_image_view_descriptor storage;
   } planes[NVK_MAX_IMAGE_PLANES];
};

VK_DEFINE_NONDISP_HANDLE_CASTS(nvk_image_view, vk.base, VkImageView,
                               VK_OBJECT_TYPE_IMAGE_VIEW)

struct nvk_image_view_capture {
   union {
      struct {
         uint32_t sampled_desc_index;
         uint32_t storage_desc_index;
      } single_plane;

      struct {
         struct {
            uint32_t desc_index;
         } planes[NVK_MAX_IMAGE_PLANES];
      } ycbcr;
   };
};

VkResult nvk_image_view_init(struct nvk_device *dev,
                             struct nvk_image_view *view,
                             const VkImageViewCreateInfo *pCreateInfo);

void nvk_image_view_finish(struct nvk_device *dev,
                           struct nvk_image_view *view);

#endif
