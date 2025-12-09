/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_META_H
#define PANVK_META_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include "panvk_image.h"
#include "panvk_mempool.h"

#include "vk_format.h"
#include "vk_meta.h"

enum panvk_meta_object_key_type {
   PANVK_META_OBJECT_KEY_BLEND_SHADER = VK_META_OBJECT_KEY_DRIVER_OFFSET,
   PANVK_META_OBJECT_KEY_COPY_DESC_SHADER,
   PANVK_META_OBJECT_KEY_FB_PRELOAD_SHADER,
};

static inline VkFormat
panvk_meta_get_uint_format_for_blk_size(unsigned blk_sz)
{
   switch (blk_sz) {
   case 1:
      return VK_FORMAT_R8_UINT;
   case 2:
      return VK_FORMAT_R8G8_UINT;
   case 3:
      return VK_FORMAT_R8G8B8_UINT;
   case 4:
      return VK_FORMAT_R8G8B8A8_UINT;
   case 6:
      return VK_FORMAT_R16G16B16_UINT;
   case 8:
      return VK_FORMAT_R32G32_UINT;
   case 12:
      return VK_FORMAT_R32G32B32_UINT;
   case 16:
      return VK_FORMAT_R32G32B32A32_UINT;
   default:
      return VK_FORMAT_UNDEFINED;
   }
}

static inline VkFormat
panvk_meta_get_blendable_format_for_blk_size(unsigned blk_sz)
{
   /* We expect _UINT formats to be used if the blocksize is greater than
    * 32-bit.
    */
   assert(blk_sz <= 4);

   switch (blk_sz) {
   case 1:
      return VK_FORMAT_R8_UNORM;
   case 2:
      return VK_FORMAT_R8G8_UNORM;
   case 3:
      return VK_FORMAT_R8G8B8_UNORM;
   case 4:
      return VK_FORMAT_R8G8B8A8_UNORM;
   default:
      return VK_FORMAT_UNDEFINED;
   }
}

static inline struct vk_meta_copy_image_properties
panvk_meta_copy_get_image_properties(struct panvk_image *img,
                                     bool use_gfx_pipeline, bool is_destination)
{
   uint64_t mod = img->vk.drm_format_mod;
   enum pipe_format pfmt = vk_format_to_pipe_format(img->vk.format);
   struct vk_meta_copy_image_properties props;
   memset(&props, 0, sizeof(props));
   const struct vk_format_ycbcr_info *ycbcr_info =
      vk_format_get_ycbcr_info(img->vk.format);
   const bool is_afbc = drm_is_afbc(img->vk.drm_format_mod);
   /* Format re-interpretation is not an option on Bifrost */
   const bool preserve_img_fmt = is_afbc && PAN_ARCH <= 7;
   /* We prefer to use formats that are blendable by the fixed function
    * hardware to avoid blend shaders. On Bifrost, only UNORM/sRGB
    * is allowed, so we use UNORM formats when creating depth/stencil views too.
    */
   const bool prefer_blendable = use_gfx_pipeline && is_destination;

   if (vk_format_is_depth_or_stencil(img->vk.format)) {
      switch (img->vk.format) {
      case VK_FORMAT_S8_UINT:
         props.stencil.view_format =
            prefer_blendable ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8_UINT;
         props.stencil.component_mask = BITFIELD_MASK(1);
         break;
      case VK_FORMAT_D24_UNORM_S8_UINT:
         if (panvk_image_is_planar_depth_stencil(img)) {
            if (img->planes[0].image.props.format ==
                PIPE_FORMAT_Z24_UNORM_PACKED) {
               props.depth.view_format =
                  prefer_blendable ? VK_FORMAT_R8G8B8_UNORM : VK_FORMAT_R8G8B8_UINT;
            } else {
               assert(img->planes[0].image.props.format ==
                      PIPE_FORMAT_Z24X8_UNORM);
               props.depth.view_format = prefer_blendable ? VK_FORMAT_R8G8B8A8_UNORM
                                                   : VK_FORMAT_R8G8B8A8_UINT;
            }
            props.depth.component_mask = BITFIELD_MASK(3);
            props.stencil.view_format =
               prefer_blendable ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8_UINT;
            props.stencil.component_mask = BITFIELD_BIT(0);
         } else {
            props.depth.view_format = prefer_blendable
                                         ? VK_FORMAT_R8G8B8A8_UNORM
                                         : VK_FORMAT_R8G8B8A8_UINT;
            props.depth.component_mask = BITFIELD_MASK(3);
            props.stencil.view_format = props.depth.view_format;
            props.stencil.component_mask = BITFIELD_BIT(3);
         }
         break;
      case VK_FORMAT_X8_D24_UNORM_PACK32:
         props.depth.view_format =
            prefer_blendable ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_UINT;
         props.depth.component_mask = BITFIELD_MASK(3);
         break;
      case VK_FORMAT_D32_SFLOAT_S8_UINT:
         assert(panvk_image_is_planar_depth_stencil(img));
         props.depth.view_format =
            prefer_blendable ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_UINT;
         props.depth.component_mask = BITFIELD_MASK(4);
         props.stencil.view_format =
            prefer_blendable ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8_UINT;
         props.stencil.component_mask = BITFIELD_BIT(0);
         break;
      case VK_FORMAT_D16_UNORM:
         props.depth.view_format =
            prefer_blendable ? VK_FORMAT_R8G8_UNORM : VK_FORMAT_R8G8_UINT;
         props.depth.component_mask = BITFIELD_MASK(2);
         break;
      case VK_FORMAT_D32_SFLOAT:
         props.depth.view_format = prefer_blendable ? VK_FORMAT_R8G8B8A8_UNORM
                                                    : VK_FORMAT_R8G8B8A8_UINT;
         props.depth.component_mask = BITFIELD_MASK(4);
         break;
      default:
         assert(!"Invalid ZS format");
         break;
      }
   } else if (ycbcr_info) {
      for (uint32_t p = 0; p < ycbcr_info->n_planes; p++) {
         unsigned blk_sz =
            vk_format_get_blocksize(ycbcr_info->planes[p].format);
         if (preserve_img_fmt) {
            props.plane[p].view_format = ycbcr_info->planes[p].format;
         } else {
            props.plane[p].view_format =
            /* there are no blendable formats with blk_sz > 4 */
               (prefer_blendable && blk_sz <= 4) ?
               panvk_meta_get_blendable_format_for_blk_size(blk_sz) :
               panvk_meta_get_uint_format_for_blk_size(blk_sz);
         }
      }
   } else {
      unsigned blk_sz = util_format_get_blocksize(pfmt);

      if (preserve_img_fmt) {
         props.color.view_format = img->vk.format;
      } else {
         props.color.view_format =
            /* there are no blendable formats with blk_sz > 4 */
            (prefer_blendable && blk_sz <= 4) ?
            panvk_meta_get_blendable_format_for_blk_size(blk_sz) :
            panvk_meta_get_uint_format_for_blk_size(blk_sz);
      }
   }

   if (mod == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED ||
       drm_is_afbc(mod)) {
      props.tile_size.width = 16;
      props.tile_size.height = 16;
      props.tile_size.depth = 1;
   } else {
      /* When linear, pretend we have a 1D-tile so we end up with a <64,1,1>
       * workgroup. */
      props.tile_size.width = 64;
      props.tile_size.height = 1;
      props.tile_size.depth = 1;
   }

   return props;
}

#if PAN_ARCH < 9
struct panvk_cmd_buffer;
struct panvk_descriptor_state;
struct panvk_device;
struct panvk_shader_variant;
struct panvk_shader_desc_state;

VkResult panvk_per_arch(meta_get_copy_desc_job)(
   struct panvk_cmd_buffer *cmdbuf, const struct panvk_shader_variant *shader,
   const struct panvk_descriptor_state *desc_state,
   const struct panvk_shader_desc_state *shader_desc_state,
   uint32_t attrib_buf_idx_offset, struct pan_ptr *job_desc);
#endif

void panvk_per_arch(transition_image_layout_sync_scope)(
   const VkImageMemoryBarrier2 *barrier,
   VkPipelineStageFlags2 *out_stages, VkAccessFlags2 *out_access);
void panvk_per_arch(cmd_transition_image_layout)(
   VkCommandBuffer _cmdbuf,
   const VkImageMemoryBarrier2 *barrier);

#endif
