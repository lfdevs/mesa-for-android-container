/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_texture.h"

/* TODO_LUNARG Remove */
#include "kk_image_layout.h"

/* TODO_LUNARG Remove */
#include "vulkan/vulkan.h"

#include <Metal/MTLTexture.h>

uint64_t
mtl_texture_get_gpu_resource_id(mtl_texture *texture)
{
   @autoreleasepool {
      id<MTLTexture> tex = (id<MTLTexture>)texture;
      return (uint64_t)[tex gpuResourceID]._impl;
   }
}

/* TODO_KOSMICKRISP This should be part of the mapping */
static uint32_t
mtl_texture_view_type(uint32_t type, uint8_t sample_count)
{
   switch (type) {
   case VK_IMAGE_VIEW_TYPE_1D:
      return MTLTextureType1D;
   case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
      return MTLTextureType1DArray;
   case VK_IMAGE_VIEW_TYPE_2D:
      return sample_count > 1u ? MTLTextureType2DMultisample : MTLTextureType2D;;
   case VK_IMAGE_VIEW_TYPE_CUBE:
      return MTLTextureTypeCube;
   case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
      return MTLTextureTypeCubeArray;
   case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
      return sample_count > 1u ? MTLTextureType2DMultisampleArray : MTLTextureType2DArray;
   case VK_IMAGE_VIEW_TYPE_3D:
      return MTLTextureType3D;
   default:
      assert(false && "Unsupported VkViewType");
      return MTLTextureType1D;
   }
}

static MTLTextureSwizzle
mtl_texture_swizzle(enum pipe_swizzle swizzle)
{
   const MTLTextureSwizzle map[] =
      {
         [PIPE_SWIZZLE_X] = MTLTextureSwizzleRed,
         [PIPE_SWIZZLE_Y] = MTLTextureSwizzleGreen,
         [PIPE_SWIZZLE_Z] = MTLTextureSwizzleBlue,
         [PIPE_SWIZZLE_W] = MTLTextureSwizzleAlpha,
         [PIPE_SWIZZLE_0] = MTLTextureSwizzleZero,
         [PIPE_SWIZZLE_1] = MTLTextureSwizzleOne,
      };

   return map[swizzle];
}

mtl_texture *
mtl_new_texture_view_with(mtl_texture *texture, const struct kk_view_layout *layout)
{
   @autoreleasepool {
      id<MTLTexture> tex = (id<MTLTexture>)texture;
      MTLTextureType type = mtl_texture_view_type(layout->view_type, layout->sample_count_sa);
      NSRange levels = NSMakeRange(layout->base_level, layout->num_levels);
      NSRange slices = NSMakeRange(layout->base_array_layer, layout->array_len);
      MTLTextureSwizzleChannels swizzle = MTLTextureSwizzleChannelsMake(mtl_texture_swizzle(layout->swizzle.red),
                                                                        mtl_texture_swizzle(layout->swizzle.green),
                                                                        mtl_texture_swizzle(layout->swizzle.blue),
                                                                        mtl_texture_swizzle(layout->swizzle.alpha));
      return [tex newTextureViewWithPixelFormat:layout->format.mtl textureType:type levels:levels slices:slices swizzle:swizzle];
   }
}

mtl_texture *
mtl_new_texture_view_with_no_swizzle(mtl_texture *texture, const struct kk_view_layout *layout)
{
   @autoreleasepool {
      id<MTLTexture> tex = (id<MTLTexture>)texture;
      MTLTextureType type = mtl_texture_view_type(layout->view_type, layout->sample_count_sa);
      NSRange levels = NSMakeRange(layout->base_level, layout->num_levels);
      NSRange slices = NSMakeRange(layout->base_array_layer, layout->array_len);
      return [tex newTextureViewWithPixelFormat:layout->format.mtl textureType:type levels:levels slices:slices];
   }
}

