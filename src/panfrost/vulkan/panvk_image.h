/*
 * Copyright © 2021 Collabora Ltd.
 * Copyright © 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_IMAGE_H
#define PANVK_IMAGE_H

#include "util/format/u_format.h"

#include "vk_format.h"
#include "vk_image.h"

#include "pan_image.h"

#define PANVK_MAX_PLANES 3

struct panvk_device_memory;
struct panvk_physical_device;

/* Right now, planar YUV images are treated as N different images, hence the 1:1
 * association between pan_image and pan_image_plane, but this can be optimized
 * once planar YUV support is hooked up. */
struct panvk_image_plane {
   struct pan_image image;
   struct pan_image_plane plane;

   struct panvk_device_memory *mem;

   /* Plane offset inside the memory object. */
   uint64_t mem_offset;
};

struct panvk_image {
   struct vk_image vk;

   struct {
      VkDeviceAddress device_address;
   } sparse;

   uint8_t plane_count;
   struct panvk_image_plane planes[PANVK_MAX_PLANES];

   /* One image each for 2x 4x 8x 16x. We don't support more than 16x. */
   VkImage ms_imgs[4];
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_image, vk.base, VkImage,
                               VK_OBJECT_TYPE_IMAGE)

/* Check whether it is possible that images in a given configuration may use
 * AFBC tiling. This function does not have access to all of the relevant
 * image configuration, and returns true if any images with the specified
 * configuration subset may use AFBC. */
bool panvk_image_can_use_afbc(
   struct panvk_physical_device *phys_dev, VkFormat fmt,
   VkImageUsageFlags usage, VkImageType type, VkImageTiling tiling,
   VkImageCreateFlags flags);

static inline unsigned
panvk_plane_index(const struct panvk_image *image,
                  VkImageAspectFlags aspect_mask)
{
   switch (aspect_mask) {
   default:
      return 0;
   case VK_IMAGE_ASPECT_PLANE_1_BIT:
      return 1;
   case VK_IMAGE_ASPECT_PLANE_2_BIT:
      return 2;
   case VK_IMAGE_ASPECT_STENCIL_BIT:
      assert(image->plane_count > 0);
      return image->plane_count - 1;
   }
}

static inline bool
panvk_image_use_yuv_tex(unsigned arch, VkFormat format)
{
   if (arch < 9 || arch >= 14)
      return false;

   switch (format) {
   case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
   case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
   case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM:
   case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:
   case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
      return true;
   default:
      break;
   }

   return false;
}

static inline unsigned
panvk_image_get_tex_count(unsigned arch, VkFormat format)
{
   return panvk_image_use_yuv_tex(arch, format)
             ? 1
             : vk_format_get_plane_count(format);
}

#define PAN_IMAGE_FROM(arch, image, plane)                                     \
   (panvk_image_use_yuv_tex(arch, image->vk.format)                            \
       ? &image->planes[0].image                                               \
       : &image->planes[plane].image)

#define PAN_IMAGE_PLANE_INDEX_FROM(arch, image, plane)                         \
   (panvk_image_use_yuv_tex(arch, image->vk.format) ? plane : 0)

static inline bool
panvk_image_is_interleaved_depth_stencil(const struct panvk_image *image){
   return image->plane_count == 1 &&
          vk_format_aspects(image->vk.format) ==
             (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
}

static inline bool
panvk_image_is_planar_depth_stencil(const struct panvk_image *image){
   return image->plane_count > 1 &&
          vk_format_aspects(image->vk.format) ==
             (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
}

static inline enum pipe_format
panvk_image_depth_only_pfmt(const struct panvk_image *image)
{
   assert(vk_format_has_depth(image->vk.format));

   return util_format_get_depth_only(image->planes[0].image.props.format);
}

static inline enum pipe_format
panvk_image_stencil_only_pfmt(const struct panvk_image *image)
{
   assert(vk_format_has_stencil(image->vk.format));

   return util_format_stencil_only(
      image->planes[image->plane_count - 1].image.props.format);
}

VkResult panvk_image_init(struct panvk_image *image,
                          const VkImageCreateInfo *pCreateInfo);

struct panvk_sparse_block_desc {
   struct VkExtent3D extent;
   uint64_t size_B;

   /* Whether this is a standard (as defined by Vulkan) sparse block size, for
    * the given arguments */
   bool standard;
};

struct panvk_sparse_block_desc panvk_get_sparse_block_desc(VkImageType type, VkFormat format);

static inline bool
panvk_sparse_block_is_valid(struct panvk_sparse_block_desc desc) { return desc.size_B > 0; }

VkSparseImageFormatProperties panvk_get_sparse_image_fmt_props(VkImageType type, VkFormat format);

#endif
