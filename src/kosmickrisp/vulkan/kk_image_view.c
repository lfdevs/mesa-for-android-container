/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_image_view.h"

#include "kk_device.h"
#include "kk_entrypoints.h"
#include "kk_format.h"
#include "kk_image.h"
#include "kk_physical_device.h"

#include "kosmickrisp/bridge/mtl_bridge.h"
#include "kosmickrisp/bridge/mtl_format.h"

#include "vk_format.h"

static enum pipe_swizzle
vk_swizzle_to_pipe(VkComponentSwizzle swizzle)
{
   switch (swizzle) {
   case VK_COMPONENT_SWIZZLE_R:
      return PIPE_SWIZZLE_X;
   case VK_COMPONENT_SWIZZLE_G:
      return PIPE_SWIZZLE_Y;
   case VK_COMPONENT_SWIZZLE_B:
      return PIPE_SWIZZLE_Z;
   case VK_COMPONENT_SWIZZLE_A:
      return PIPE_SWIZZLE_W;
   case VK_COMPONENT_SWIZZLE_ONE:
      return PIPE_SWIZZLE_1;
   case VK_COMPONENT_SWIZZLE_ZERO:
      return PIPE_SWIZZLE_0;
   default:
      UNREACHABLE("Invalid component swizzle");
   }
}

static enum VkImageViewType
remove_1d_view_types(enum VkImageViewType type)
{
   if (type == VK_IMAGE_VIEW_TYPE_1D)
      return VK_IMAGE_VIEW_TYPE_2D;
   if (type == VK_IMAGE_VIEW_TYPE_1D_ARRAY)
      return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
   return type;
}

VkResult
kk_image_view_init(struct kk_device *dev, struct kk_image_view *view,
                   const VkImageViewCreateInfo *pCreateInfo)
{
   VK_FROM_HANDLE(kk_image, image, pCreateInfo->image);

   memset(view, 0, sizeof(*view));

   vk_image_view_init(&dev->vk, &view->vk, pCreateInfo);

   /* First, figure out which image planes we need.
    * For depth/stencil, we only have plane so simply assert
    * and then map directly betweeen the image and view plane
    */
   if (image->vk.aspects &
       (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
      assert(image->plane_count == 1);
      assert(kk_image_aspects_to_plane(image, view->vk.aspects) == 0);
      view->plane_count = 1;
      view->planes[0].image_plane = 0;
   } else {
      /* For other formats, retrieve the plane count from the aspect mask
       * and then walk through the aspect mask to map each image plane
       * to its corresponding view plane
       */
      assert(util_bitcount(view->vk.aspects) ==
             vk_format_get_plane_count(view->vk.format));
      view->plane_count = 0;
      u_foreach_bit(aspect_bit, view->vk.aspects) {
         uint8_t image_plane =
            kk_image_aspects_to_plane(image, 1u << aspect_bit);
         view->planes[view->plane_count++].image_plane = image_plane;
      }
   }
   /* Finally, fill in each view plane separately */
   for (unsigned view_plane = 0; view_plane < view->plane_count; view_plane++) {
      const uint8_t image_plane = view->planes[view_plane].image_plane;
      struct kk_image_plane *plane = &image->planes[image_plane];

      const struct vk_format_ycbcr_info *ycbcr_info =
         vk_format_get_ycbcr_info(view->vk.format);
      assert(ycbcr_info || view_plane == 0);
      VkFormat plane_format =
         ycbcr_info ? ycbcr_info->planes[view_plane].format : view->vk.format;
      enum pipe_format p_format = vk_format_to_pipe_format(plane_format);
      if (view->vk.aspects == VK_IMAGE_ASPECT_DEPTH_BIT)
         p_format = vk_format_to_pipe_format(image->vk.format);
      else if (view->vk.aspects == VK_IMAGE_ASPECT_STENCIL_BIT)
         p_format = util_format_stencil_only(
            vk_format_to_pipe_format(image->vk.format));

      view->planes[view_plane].format = p_format;
      const struct kk_va_format *supported_format = kk_get_va_format(p_format);
      assert(supported_format);

      struct kk_view_layout view_layout = {
         .view_type = remove_1d_view_types(view->vk.view_type),
         .sample_count_sa = plane->layout.sample_count_sa,
         .format = {.pipe = p_format,
                    .mtl = supported_format->mtl_pixel_format},
         .base_level = view->vk.base_mip_level,
         .num_levels = view->vk.level_count,
         .base_array_layer = view->vk.base_array_layer,
         .array_len = view->vk.layer_count,
         .min_lod_clamp = view->vk.min_lod,
      };
      uint8_t view_swizzle[4] = {vk_swizzle_to_pipe(view->vk.swizzle.r),
                                 vk_swizzle_to_pipe(view->vk.swizzle.g),
                                 vk_swizzle_to_pipe(view->vk.swizzle.b),
                                 vk_swizzle_to_pipe(view->vk.swizzle.a)};
      util_format_compose_swizzles(supported_format->unswizzle.channels,
                                   view_swizzle, view_layout.swizzle.channels);

      /* When sampling a depth/stencil texture Metal returns (d, d, d, 1), but
       * Vulkan requires (d, 0, 0, 1). This means, we need to convert G and B to
       * 0 */
      if (util_format_is_depth_or_stencil(p_format)) {
         if (view_layout.swizzle.red == PIPE_SWIZZLE_Y ||
             view_layout.swizzle.red == PIPE_SWIZZLE_Z)
            view_layout.swizzle.red = PIPE_SWIZZLE_0;
         if (view_layout.swizzle.green == PIPE_SWIZZLE_Y ||
             view_layout.swizzle.green == PIPE_SWIZZLE_Z)
            view_layout.swizzle.green = PIPE_SWIZZLE_0;
         if (view_layout.swizzle.blue == PIPE_SWIZZLE_Y ||
             view_layout.swizzle.blue == PIPE_SWIZZLE_Z)
            view_layout.swizzle.blue = PIPE_SWIZZLE_0;
         if (view_layout.swizzle.alpha == PIPE_SWIZZLE_Y ||
             view_layout.swizzle.alpha == PIPE_SWIZZLE_Z)
            view_layout.swizzle.alpha = PIPE_SWIZZLE_0;
      }

      mtl_texture *mtl_handle = image->planes[image_plane].mtl_handle;
      if (image->vk.image_type == VK_IMAGE_TYPE_3D &&
          view->vk.view_type != VK_IMAGE_VIEW_TYPE_3D)
         mtl_handle = image->planes[image_plane].mtl_handle_array;

      if (view->vk.usage &
          (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) {
         view->planes[view_plane].mtl_handle_sampled =
            mtl_new_texture_view_with(mtl_handle, &view_layout);
         view->planes[view_plane].sampled_gpu_resource_id =
            mtl_texture_get_gpu_resource_id(
               view->planes[view_plane].mtl_handle_sampled);
      }

      if (view->vk.usage & VK_IMAGE_USAGE_STORAGE_BIT) {
         /* For storage images, we can't have any cubes */
         if (view->vk.view_type == VK_IMAGE_VIEW_TYPE_CUBE ||
             view->vk.view_type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)
            view_layout.view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;

         view->planes[view_plane].mtl_handle_storage =
            mtl_new_texture_view_with(mtl_handle, &view_layout);
         view->planes[view_plane].storage_gpu_resource_id =
            mtl_texture_get_gpu_resource_id(
               view->planes[view_plane].mtl_handle_storage);
      }

      if (view->vk.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
         bool requires_type_change =
            view_layout.view_type != VK_IMAGE_VIEW_TYPE_3D &&
            view_layout.view_type != VK_IMAGE_VIEW_TYPE_2D_ARRAY;
         bool requires_format_change = view->vk.format != image->vk.format;
         VkImageViewType original_type = view_layout.view_type;

         /* Required so sampling from input attachments actually return (d, 0,
          * 0, 1) for d/s attachments and render targets cannot have swizzle
          * according to Metal...
          */
         if (requires_type_change || requires_format_change) {
            view_layout.view_type = requires_type_change
                                       ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                       : original_type;
            view->planes[view_plane].mtl_handle_input =
               mtl_new_texture_view_with(mtl_handle, &view_layout);
         } else
            view->planes[view_plane].mtl_handle_input = mtl_retain(mtl_handle);
         view->planes[view_plane].input_gpu_resource_id =
            mtl_texture_get_gpu_resource_id(
               view->planes[view_plane].mtl_handle_input);

         /* Handle mutable formats */
         if (requires_format_change) {
            view_layout.view_type = original_type;
            view_layout.base_array_layer = 0u;
            view_layout.base_level = 0u;
            view_layout.array_len = image->vk.array_layers;
            view_layout.num_levels = image->vk.mip_levels;
            view->planes[view_plane].mtl_handle_render =
               mtl_new_texture_view_with_no_swizzle(mtl_handle, &view_layout);
         } else
            view->planes[view_plane].mtl_handle_render = mtl_retain(mtl_handle);
      }
   }

   return VK_SUCCESS;
}

void
kk_image_view_finish(struct kk_device *dev, struct kk_image_view *view)
{
   for (uint8_t plane = 0; plane < view->plane_count; plane++) {
      if (view->planes[plane].mtl_handle_sampled)
         mtl_release(view->planes[plane].mtl_handle_sampled);

      if (view->planes[plane].mtl_handle_storage)
         mtl_release(view->planes[plane].mtl_handle_storage);

      if (view->planes[plane].mtl_handle_input)
         mtl_release(view->planes[plane].mtl_handle_input);

      if (view->planes[plane].mtl_handle_render)
         mtl_release(view->planes[plane].mtl_handle_render);
   }

   vk_image_view_finish(&view->vk);
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_CreateImageView(VkDevice _device, const VkImageViewCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator, VkImageView *pView)
{
   VK_FROM_HANDLE(kk_device, dev, _device);
   struct kk_image_view *view;
   VkResult result;

   view = vk_alloc2(&dev->vk.alloc, pAllocator, sizeof(*view), 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!view)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = kk_image_view_init(dev, view, pCreateInfo);
   if (result != VK_SUCCESS) {
      vk_free2(&dev->vk.alloc, pAllocator, view);
      return result;
   }

   *pView = kk_image_view_to_handle(view);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
kk_DestroyImageView(VkDevice _device, VkImageView imageView,
                    const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(kk_device, dev, _device);
   VK_FROM_HANDLE(kk_image_view, view, imageView);

   if (!view)
      return;

   kk_image_view_finish(dev, view);
   vk_free2(&dev->vk.alloc, pAllocator, view);
}
