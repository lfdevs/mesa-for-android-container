/*
 * Copyright 2024 Autodesk, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wsi_common_metal_layer.h"

#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>

void
wsi_metal_layer_size(const CAMetalLayer *metal_layer,
   uint32_t *width, uint32_t *height)
{
   @autoreleasepool {
      /* The reason why "drawableSize" is not being used here because it will
       * only return non-zero values if there has actually been any kind of
       * drawable allocated (acquired). Without this we also run into crashes
       * in KosmicKrisp. Initializing the CAMetalLayer with a NSView with
       * a frame does not create the drawable. Due to this, we fail/crash
       * tests in the following Vulkan CTS test family:
       * dEQP-VK.wsi.metal.surface.*
       *
       * There are 2 possible ways to fix this:
       * 1. The one implemented.
       * 2. Return the special value allowed by the spec to state that we will
       *    actually give it a value once the swapchain is created
       *    https://docs.vulkan.org/refpages/latest/refpages/source/VkSurfaceCapabilitiesKHR.html
       */
      CGSize size = metal_layer.bounds.size;
      CGFloat scaleFactor = metal_layer.contentsScale;
      size.width *= scaleFactor;
      size.height *= scaleFactor;

      if (width)
         *width = size.width;
      if (height)
         *height = size.height;
   }
}

static VkResult
get_mtl_pixel_format(VkFormat format, MTLPixelFormat *metal_format)
{
   switch (format) {
      case VK_FORMAT_B8G8R8A8_SRGB:
         *metal_format = MTLPixelFormatBGRA8Unorm_sRGB;
         break;
      case VK_FORMAT_B8G8R8A8_UNORM:
         *metal_format = MTLPixelFormatBGRA8Unorm;
         break;
      case VK_FORMAT_R16G16B16A16_SFLOAT:
         *metal_format = MTLPixelFormatRGBA16Float;
         break;
      case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
         *metal_format = MTLPixelFormatRGB10A2Unorm;
         break;
      case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
         *metal_format = MTLPixelFormatBGR10A2Unorm;
         break;
      default:
         return VK_ERROR_FORMAT_NOT_SUPPORTED;
   }

   return VK_SUCCESS;
}

static VkResult
get_cg_color_space(VkColorSpaceKHR color_space, CGColorSpaceRef *cg_color_space)
{
   CFStringRef color_space_name;
   switch (color_space) {
      case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
         color_space_name = kCGColorSpaceSRGB;
         break;
      case VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT:
         color_space_name = kCGColorSpaceDisplayP3;
         break;
      case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
         color_space_name = kCGColorSpaceExtendedLinearSRGB;
         break;
      case VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT:
         color_space_name = kCGColorSpaceLinearDisplayP3;
         break;
      case VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT:
         color_space_name = kCGColorSpaceDCIP3;
         break;
      case VK_COLOR_SPACE_BT709_NONLINEAR_EXT:
         color_space_name = kCGColorSpaceITUR_709;
         break;
      case VK_COLOR_SPACE_BT2020_LINEAR_EXT:
         color_space_name = kCGColorSpaceLinearITUR_2020;
         break;
      case VK_COLOR_SPACE_ADOBERGB_NONLINEAR_EXT:
         color_space_name = kCGColorSpaceAdobeRGB1998;
         break;
      case VK_COLOR_SPACE_PASS_THROUGH_EXT:
         color_space_name = nil;
         break;
      case VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT:
         color_space_name = kCGColorSpaceExtendedSRGB;
         break;
      default:
         return VK_ERROR_FORMAT_NOT_SUPPORTED;
   }

   if (color_space_name) {
      *cg_color_space = CGColorSpaceCreateWithName(color_space_name);
   } else {
      *cg_color_space = nil;
   }

   return VK_SUCCESS;
}

VkResult
wsi_metal_layer_configure(const CAMetalLayer *metal_layer,
   uint32_t width, uint32_t height, uint32_t image_count,
   VkFormat format, VkColorSpaceKHR color_space,
   bool enable_opaque, bool enable_immediate)
{
   @autoreleasepool {
      MTLPixelFormat metal_format;
      VkResult result = get_mtl_pixel_format(format, &metal_format);
      if (result != VK_SUCCESS)
         return result;

      CGColorSpaceRef cg_color_space;
      result = get_cg_color_space(color_space, &cg_color_space);
      if (result != VK_SUCCESS)
         return result;

      if (metal_layer.device == nil)
         metal_layer.device = metal_layer.preferredDevice;

      /* So acquire timeout works */
      metal_layer.allowsNextDrawableTimeout = YES;
      /* So we can blit to the drawable */
      metal_layer.framebufferOnly = NO;

      metal_layer.maximumDrawableCount = image_count;
      metal_layer.drawableSize = (CGSize){.width = width, .height = height};
      metal_layer.opaque = enable_opaque;
      metal_layer.displaySyncEnabled = !enable_immediate;
      metal_layer.pixelFormat = metal_format;

      metal_layer.colorspace = cg_color_space;
      /* Needs release: https://github.com/KhronosGroup/MoltenVK/issues/940 */
      CGColorSpaceRelease(cg_color_space);
   }

   return VK_SUCCESS;
}

CAMetalDrawable *
wsi_metal_layer_acquire_drawable(const CAMetalLayer *metal_layer)
{
   @autoreleasepool {
      id<CAMetalDrawable> drawable = [metal_layer nextDrawable];
      return (CAMetalDrawable *)[drawable retain];
   }
}

void
wsi_metal_release_drawable(CAMetalDrawable *drawable_ptr)
{
   [(id<CAMetalDrawable>)drawable_ptr release];
}

struct wsi_metal_layer_blit_context {
   id<MTLDevice> device;
   id<MTLCommandQueue> commandQueue;
};

struct wsi_metal_layer_blit_context *
wsi_create_metal_layer_blit_context()
{
   @autoreleasepool {
      struct wsi_metal_layer_blit_context *context = malloc(sizeof(struct wsi_metal_layer_blit_context));
      memset((void*)context, 0, sizeof(*context));

      context->device = MTLCreateSystemDefaultDevice();
      context->commandQueue = [context->device newCommandQueue];

      return context;
   }
}

void
wsi_destroy_metal_layer_blit_context(struct wsi_metal_layer_blit_context *context)
{
   [context->commandQueue release];
   [context->device release];
   context->device = nil;
   context->commandQueue = nil;
   free(context);
}

void
wsi_metal_layer_blit_and_present(struct wsi_metal_layer_blit_context *context,
   CAMetalDrawable **drawable_ptr, void *buffer,
   uint32_t width, uint32_t height, uint32_t row_pitch)
{
   @autoreleasepool {
      id<CAMetalDrawable> drawable = [(id<CAMetalDrawable>)*drawable_ptr autorelease];

      id<MTLCommandBuffer> commandBuffer = [context->commandQueue commandBuffer];
      id<MTLBlitCommandEncoder> commandEncoder = [commandBuffer blitCommandEncoder];

      NSUInteger image_size = height * row_pitch;
      id<MTLBuffer> image_buffer = [[context->device newBufferWithBytesNoCopy:buffer
         length:image_size
         options:MTLResourceStorageModeShared
         deallocator:nil] autorelease];

      [commandEncoder copyFromBuffer:image_buffer
         sourceOffset:0
         sourceBytesPerRow:row_pitch
         sourceBytesPerImage:image_size
         sourceSize:MTLSizeMake(width, height, 1)
         toTexture:drawable.texture
         destinationSlice:0
         destinationLevel:0
         destinationOrigin:MTLOriginMake(0, 0, 0)];
      [commandEncoder endEncoding];
      [commandBuffer presentDrawable:drawable];
      [commandBuffer commit];

      *drawable_ptr = nil;
   }
}
