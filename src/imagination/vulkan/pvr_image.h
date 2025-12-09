/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * based in part on radv driver which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PVR_IMAGE_H
#define PVR_IMAGE_H

#include <stdint.h>

#include "vk_image.h"

#include "pvr_common.h"
#include "pvr_types.h"

struct pvr_mip_level {
   /* Offset of the mip level in bytes */
   uint32_t offset;

   /* Aligned mip level size in bytes */
   uint32_t size;

   /* Aligned row length in bytes */
   uint32_t pitch;

   /* Aligned height in bytes */
   uint32_t height_pitch;
};

struct pvr_image {
   struct vk_image vk;

   /* vma this image is bound to */
   struct pvr_winsys_vma *vma;

   /* Device address the image is mapped to in device virtual address space */
   pvr_dev_addr_t dev_addr;

   /* Derived and other state */
   VkExtent3D physical_extent;
   enum pvr_memlayout memlayout;
   VkDeviceSize layer_size;
   VkDeviceSize size;

   VkDeviceSize alignment;

   struct pvr_mip_level mip_levels[14];
};

struct pvr_image_view {
   struct vk_image_view vk;

   /* Prepacked Texture Image dword 0 and 1. It will be copied to the
    * descriptor info during pvr_UpdateDescriptorSets().
    *
    * We create separate texture states for sampling, storage and input
    * attachment cases.
    */
   struct pvr_image_descriptor image_state[PVR_TEXTURE_STATE_MAX_ENUM];
};

VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_image, vk.base, VkImage, VK_OBJECT_TYPE_IMAGE)

VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_image_view,
                               vk.base,
                               VkImageView,
                               VK_OBJECT_TYPE_IMAGE_VIEW)

static inline const struct pvr_image *
vk_to_pvr_image(const struct vk_image *image)
{
   return container_of(image, const struct pvr_image, vk);
}

static inline const struct pvr_image *
pvr_image_view_get_image(const struct pvr_image_view *const iview)
{
   return vk_to_pvr_image(iview->vk.image);
}

void pvr_get_image_subresource_layout(const struct pvr_image *image,
                                      const VkImageSubresource *subresource,
                                      VkSubresourceLayout *layout);

#endif /* PVR_IMAGE_H */
