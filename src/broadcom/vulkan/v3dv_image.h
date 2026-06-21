/*
 * Copyright © 2026 Raspberry Pi Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#ifndef V3DV_IMAGE_H
#define V3DV_IMAGE_H

#include "v3dv_common.h"
#include "vk_image.h"
#include "v3dv_limits.h"
#include "common/v3d_limits.h"
#include "common/v3d_tiling.h"

struct v3dv_device;
struct v3dv_device_memory;
struct v3dv_format;
struct v3dv_format_plane;

#define V3D_OUTPUT_IMAGE_FORMAT_NO 255
#define TEXTURE_DATA_FORMAT_NO     255

/* Note that although VkImageAspectFlags would allow to combine more than one
 * PLANE bit, for all the use cases we implement that use VkImageAspectFlags,
 * only one plane is allowed, like for example vkCmdCopyImage:
 *
 *   "If srcImage has a VkFormat with two planes then for each element of
 *    pRegions, srcSubresource.aspectMask must be VK_IMAGE_ASPECT_PLANE_0_BIT
 *    or VK_IMAGE_ASPECT_PLANE_1_BIT"
 *
 */
static uint8_t v3dv_plane_from_aspect(VkImageAspectFlags aspect)
{
   switch (aspect) {
   case VK_IMAGE_ASPECT_COLOR_BIT:
   case VK_IMAGE_ASPECT_DEPTH_BIT:
   case VK_IMAGE_ASPECT_STENCIL_BIT:
   case VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT:
   case VK_IMAGE_ASPECT_PLANE_0_BIT:
   case VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT:
      return 0;
   case VK_IMAGE_ASPECT_PLANE_1_BIT:
   case VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT:
      return 1;
   case VK_IMAGE_ASPECT_PLANE_2_BIT:
   case VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT:
      return 2;
   default:
      UNREACHABLE("invalid image aspect");
   }
}

struct v3d_resource_slice {
   uint32_t offset;
   uint32_t stride;
   uint32_t padded_height;
   uint32_t width;
   uint32_t height;
   /* Size of a single pane of the slice.  For 3D textures, there will be
    * a number of panes equal to the minified, power-of-two-aligned
    * depth.
    */
   uint32_t size;
   uint8_t ub_pad;
   enum v3d_tiling_mode tiling;
   uint32_t padded_height_of_output_image_in_uif_blocks;
};

bool v3dv_format_swizzle_needs_rb_swap(const uint8_t *swizzle);
bool v3dv_format_swizzle_needs_reverse(const uint8_t *swizzle);

struct v3dv_image {
   struct vk_image vk;

   const struct v3dv_format *format;
   bool tiled;

   uint8_t plane_count;

   /* If 0, this is a multi-plane image with use disjoint memory, where each
    * plane binds a different device memory. Otherwise, all the planes share
    * the same device memory and this stores the total size of the image in
    * bytes.
    */
   uint32_t non_disjoint_size;

   struct {
      uint32_t cpp;

      /* Sized at V3D_MAX_MIP_LEVELS + 2 (= 15) to support the
       * V3D_WEBGPU_OVERRIDE=1 path: it advertises maxImageDimension2D = 8192
       * (14 mip levels, 0..13) and maxImageDimension1D/3D = 16384 (15 mip
       * levels, 0..14), two more than the default V3D_MAX_MIP_LEVELS (13
       * levels, 0..12 for the 4096 HW limit).
       */
      struct v3d_resource_slice slices[V3D_MAX_MIP_LEVELS + 2];
      /* Total size of the plane in bytes. */
      uint64_t size;
      uint32_t cube_map_stride;

      /* If not using disjoint memory, mem and mem_offset is the same for all
       * planes, in which case mem_offset is the offset of plane 0.
       */
      struct v3dv_device_memory *mem;
      VkDeviceSize mem_offset;
      uint32_t alignment;

      /* Pre-subsampled per plane width and height
       */
      uint32_t width;
      uint32_t height;

      /* Even if we can get it from the parent image format, we keep the
       * format here for convenience
       */
      VkFormat vk_format;
   } planes[V3DV_MAX_PLANE_COUNT];

   /* Used only when sampling a linear texture (which V3D doesn't support).
    * This holds a tiled copy of the image we can use for that purpose.
    */
   struct v3dv_image *shadow;

   /* Image is a WSI image.
    */
   bool from_wsi;
};

VkResult
v3dv_image_init(struct v3dv_device *device,
                const VkImageCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator,
                struct v3dv_image *image);

VkImageViewType v3dv_image_type_to_view_type(VkImageType type);

static uint32_t
v3dv_image_aspect_to_plane(const struct v3dv_image *image,
                           VkImageAspectFlagBits aspect)
{
   assert(util_bitcount(aspect) == 1 && (aspect & image->vk.aspects));

   /* Because we always put image and view planes in aspect-bit-order, the
    * plane index is the number of bits in the image aspect before aspect.
    */
   return util_bitcount(image->vk.aspects & (aspect - 1));
}

struct v3dv_image_view {
   struct vk_image_view vk;

   const struct v3dv_format *format;

   uint8_t view_swizzle[4];

   uint8_t plane_count;
   struct {
      uint8_t image_plane;

      bool swap_rb;
      bool channel_reverse;
      uint32_t internal_bpp;
      uint32_t internal_type;
      uint32_t offset;

      /* Precomputed swizzle (composed from the view swizzle and the format
       * swizzle).
       *
       * This could be also included on the descriptor bo, but the shader state
       * packet doesn't need it on a bo, so we can just avoid a memory copy
       */
      uint8_t swizzle[4];

      /* Prepacked TEXTURE_SHADER_STATE. It will be copied to the descriptor info
       * during UpdateDescriptorSets.
       *
       * Empirical tests show that cube arrays need a different shader state
       * depending on whether they are used with a sampler or not, so for these
       * we generate two states and select the one to use based on the descriptor
       * type.
       */
      uint8_t texture_shader_state[2][V3DV_TEXTURE_SHADER_STATE_LENGTH];
   } planes[V3DV_MAX_PLANE_COUNT];

   /* Used only when sampling a linear texture (which V3D doesn't support).
    * This would represent a view over the tiled shadow image.
    */
   struct v3dv_image_view *shadow;
};

VkResult v3dv_create_image_view(struct v3dv_device *device,
                                const VkImageViewCreateInfo *pCreateInfo,
                                VkImageView *pView);

uint32_t v3dv_layer_offset(const struct v3dv_image *image, uint32_t level, uint32_t layer,
                           uint8_t plane);

struct v3dv_buffer {
   struct vk_object_base base;

   VkDeviceSize size;
   VkBufferUsageFlagBits2KHR usage;
   uint32_t alignment;

   struct v3dv_device_memory *mem;
   VkDeviceSize mem_offset;
};

void
v3dv_buffer_init(struct v3dv_device *device,
                 const VkBufferCreateInfo *pCreateInfo,
                 struct v3dv_buffer *buffer,
                 uint32_t alignment);

void
v3dv_buffer_bind_memory(const VkBindBufferMemoryInfo *info);

struct v3dv_buffer_view {
   struct vk_object_base base;

   struct v3dv_buffer *buffer;

   VkFormat vk_format;
   const struct v3dv_format *format;
   uint32_t internal_bpp;
   uint32_t internal_type;

   uint32_t offset;
   uint32_t size;
   uint32_t num_elements;

   /* Prepacked TEXTURE_SHADER_STATE. */
   uint8_t texture_shader_state[V3DV_TEXTURE_SHADER_STATE_LENGTH];
};

const uint8_t *v3dv_get_format_swizzle(struct v3dv_device *device, VkFormat f,
                                       uint8_t plane);
const struct v3dv_format *
v3dv_get_compatible_tfu_format(struct v3dv_device *device,
                               uint32_t bpp, VkFormat *out_vk_format);
bool v3dv_buffer_format_supports_features(struct v3dv_device *device,
                                          VkFormat vk_format,
                                          VkFormatFeatureFlags2 features);

VkResult
v3dv_update_image_layout(struct v3dv_device *device,
                         struct v3dv_image *image,
                         uint64_t modifier,
                         bool disjoint,
                         const VkImageDrmFormatModifierExplicitCreateInfoEXT *explicit_mod_info);

VK_DEFINE_NONDISP_HANDLE_CASTS(v3dv_buffer, base, VkBuffer,
                               VK_OBJECT_TYPE_BUFFER)
VK_DEFINE_NONDISP_HANDLE_CASTS(v3dv_buffer_view, base, VkBufferView,
                               VK_OBJECT_TYPE_BUFFER_VIEW)
VK_DEFINE_NONDISP_HANDLE_CASTS(v3dv_image, vk.base, VkImage,
                               VK_OBJECT_TYPE_IMAGE)
VK_DEFINE_NONDISP_HANDLE_CASTS(v3dv_image_view, vk.base, VkImageView,
                               VK_OBJECT_TYPE_IMAGE_VIEW)

#endif /* V3DV_IMAGE_H */
