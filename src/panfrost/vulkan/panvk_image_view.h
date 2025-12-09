/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_IMAGE_VIEW_H
#define PANVK_IMAGE_VIEW_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include <stdint.h>

#include "vk_image.h"

#include "pan_texture.h"

#include "genxml/gen_macros.h"
#include "panvk_image.h"

struct panvk_priv_bo;

struct panvk_image_view {
   struct vk_image_view vk;

   struct pan_image_view pview;

   struct panvk_priv_mem mem;

   struct {
      union {
         struct mali_texture_packed tex[PANVK_MAX_PLANES];
         struct {
            struct mali_texture_packed tex;
            struct mali_texture_packed other_aspect_tex;
         } zs;
      };
#if PAN_ARCH >= 9
      /* Valhall passes a limited texture descriptor to the LEA_TEX instruction */
      struct mali_texture_packed storage_tex[PANVK_MAX_PLANES];
#else
      struct mali_attribute_buffer_packed img_attrib_buf[2];
#endif
   } descs;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_image_view, vk.base, VkImageView,
                               VK_OBJECT_TYPE_IMAGE_VIEW);

static inline uint32_t
panvk_image_view_plane_index(struct panvk_image_view *view)
{
   struct panvk_image *image =
      container_of(view->vk.image, struct panvk_image, vk);

   if (vk_format_is_depth_or_stencil(view->vk.image->format) &&
       view->vk.aspects == VK_IMAGE_ASPECT_COLOR_BIT) {
      /* Color views of ZS is needed for meta copies. 1 byte format is always
       * stencil, and if it's not the stencil component the caller wants, it
       * has to be the depth.
       */
      if (vk_format_get_blocksize(view->vk.view_format) == 1)
         return panvk_plane_index(image, VK_IMAGE_ASPECT_STENCIL_BIT);
      else
         return panvk_plane_index(image, VK_IMAGE_ASPECT_DEPTH_BIT);
   } else {
      return panvk_plane_index(image, view->vk.aspects);
   }
}

static_assert(offsetof(struct panvk_image_view, descs.zs.tex) ==
                       offsetof(struct panvk_image_view, descs.tex),
              "ZS texture descriptor must alias with color texture descriptor");

#endif
