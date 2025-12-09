/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_render_state.h"

#include "mtl_format.h"

/* TODO_KOSMICKRISP Remove */
#include "vk_to_mtl_map.h"

/* TODO_KOSMICKRISP Remove */
#include "vulkan/vulkan.h"

#include <Metal/MTLRenderPass.h>
#include <Metal/MTLRenderPipeline.h>
#include <Metal/MTLDepthStencil.h>

/* Render pass descriptor */
mtl_render_pass_descriptor *
mtl_new_render_pass_descriptor(void)
{
   @autoreleasepool {
      return [[MTLRenderPassDescriptor renderPassDescriptor] retain];
   }
}

mtl_render_pass_attachment_descriptor *
mtl_render_pass_descriptor_get_color_attachment(
   mtl_render_pass_descriptor *descriptor, uint32_t index)
{
   @autoreleasepool {
      MTLRenderPassDescriptor *desc = (MTLRenderPassDescriptor *)descriptor;
      return desc.colorAttachments[index];
   }
}

mtl_render_pass_attachment_descriptor *
mtl_render_pass_descriptor_get_depth_attachment(
   mtl_render_pass_descriptor *descriptor)
{
   @autoreleasepool {
      MTLRenderPassDescriptor *desc = (MTLRenderPassDescriptor *)descriptor;
      return desc.depthAttachment;
   }
}

mtl_render_pass_attachment_descriptor *
mtl_render_pass_descriptor_get_stencil_attachment(
   mtl_render_pass_descriptor *descriptor)
{
   @autoreleasepool {
      MTLRenderPassDescriptor *desc = (MTLRenderPassDescriptor *)descriptor;
      return desc.stencilAttachment;
   }
}

void
mtl_render_pass_attachment_descriptor_set_texture(
   mtl_render_pass_attachment_descriptor *descriptor, mtl_texture *texture)
{
   @autoreleasepool {
      MTLRenderPassAttachmentDescriptor *desc = (MTLRenderPassAttachmentDescriptor *)descriptor;
      desc.texture = (id<MTLTexture>)texture;
   }
}

void
mtl_render_pass_attachment_descriptor_set_level(
   mtl_render_pass_attachment_descriptor *descriptor, uint32_t level)
{
   @autoreleasepool {
      MTLRenderPassAttachmentDescriptor *desc = (MTLRenderPassAttachmentDescriptor *)descriptor;
      desc.level = level;
   }
}

void
mtl_render_pass_attachment_descriptor_set_slice(
   mtl_render_pass_attachment_descriptor *descriptor, uint32_t slice)
{
   @autoreleasepool {
      MTLRenderPassAttachmentDescriptor *desc = (MTLRenderPassAttachmentDescriptor *)descriptor;
      desc.slice = slice;
   }
}

void
mtl_render_pass_attachment_descriptor_set_load_action(
   mtl_render_pass_attachment_descriptor *descriptor,
   enum mtl_load_action action)
{
   @autoreleasepool {
      MTLRenderPassAttachmentDescriptor *desc = (MTLRenderPassAttachmentDescriptor *)descriptor;
      desc.loadAction = (MTLLoadAction)action;
   }
}

void
mtl_render_pass_attachment_descriptor_set_store_action(
   mtl_render_pass_attachment_descriptor *descriptor,
   enum mtl_store_action action)
{
   @autoreleasepool {
      MTLRenderPassAttachmentDescriptor *desc = (MTLRenderPassAttachmentDescriptor *)descriptor;
      desc.storeAction = (MTLStoreAction)action;
      desc.storeActionOptions = MTLStoreActionOptionNone; /* TODO_KOSMICKRISP Maybe expose this? */
   }
}

void
mtl_render_pass_attachment_descriptor_set_clear_color(
   mtl_render_pass_attachment_descriptor *descriptor,
   struct mtl_clear_color clear_color)
{
   @autoreleasepool {
      MTLRenderPassColorAttachmentDescriptor *desc = (MTLRenderPassColorAttachmentDescriptor *)descriptor;
      desc.clearColor = MTLClearColorMake(clear_color.red, clear_color.green, clear_color.blue, clear_color.alpha);
   }
}

void
mtl_render_pass_attachment_descriptor_set_clear_depth(
   mtl_render_pass_attachment_descriptor *descriptor, double depth)
{
   @autoreleasepool {
      MTLRenderPassDepthAttachmentDescriptor *desc = (MTLRenderPassDepthAttachmentDescriptor *)descriptor;
      desc.clearDepth = depth;
   }
}

void
mtl_render_pass_attachment_descriptor_set_clear_stencil(mtl_render_pass_attachment_descriptor *descriptor,
                                                        uint32_t stencil)
{
   @autoreleasepool {
      MTLRenderPassStencilAttachmentDescriptor *desc = (MTLRenderPassStencilAttachmentDescriptor *)descriptor;
      desc.clearStencil = stencil;
   }
}

void
mtl_render_pass_descriptor_set_render_target_array_length(mtl_render_pass_descriptor *descriptor,
                                                          uint32_t length)
{
   @autoreleasepool {
      MTLRenderPassDescriptor *desc = (MTLRenderPassDescriptor *)descriptor;
      desc.renderTargetArrayLength = length;
   }
}

void
mtl_render_pass_descriptor_set_render_target_width(mtl_render_pass_descriptor *descriptor,
                                                   uint32_t width)
{
   @autoreleasepool {
      MTLRenderPassDescriptor *desc = (MTLRenderPassDescriptor *)descriptor;
      desc.renderTargetWidth = width;
   }
}

void
mtl_render_pass_descriptor_set_render_target_height(mtl_render_pass_descriptor *descriptor,
                                                    uint32_t height)
{
   @autoreleasepool {
      MTLRenderPassDescriptor *desc = (MTLRenderPassDescriptor *)descriptor;
      desc.renderTargetHeight = height;
   }
}

void
mtl_render_pass_descriptor_set_default_raster_sample_count(mtl_render_pass_descriptor *descriptor,
                                                           uint32_t sample_count)
{
   @autoreleasepool {
      MTLRenderPassDescriptor *desc = (MTLRenderPassDescriptor *)descriptor;
      desc.defaultRasterSampleCount = sample_count;
   }
}

void
mtl_render_pass_descriptor_set_visibility_buffer(mtl_render_pass_descriptor *descriptor,
                                                 mtl_buffer *visibility_buffer)
{
   @autoreleasepool {
      MTLRenderPassDescriptor *desc = (MTLRenderPassDescriptor *)descriptor;
      id<MTLBuffer> buffer = (id<MTLBuffer>)visibility_buffer;
      desc.visibilityResultBuffer = buffer;
   }
}

/* Render pipeline descriptor */
mtl_render_pipeline_descriptor *
mtl_new_render_pipeline_descriptor()
{
   @autoreleasepool {
      return [[MTLRenderPipelineDescriptor alloc] init];
   }
}

void
mtl_render_pipeline_descriptor_set_vertex_shader(mtl_render_pass_descriptor *descriptor,
                                                 mtl_function *shader)
{
   @autoreleasepool {
      MTLRenderPipelineDescriptor *desc = (MTLRenderPipelineDescriptor *)descriptor;
      desc.vertexFunction = (id<MTLFunction>)shader;
   }
}

void
mtl_render_pipeline_descriptor_set_fragment_shader(mtl_render_pass_descriptor *descriptor,
                                                   mtl_function *shader)
{
   @autoreleasepool {
      MTLRenderPipelineDescriptor *desc = (MTLRenderPipelineDescriptor *)descriptor;
      desc.fragmentFunction = (id<MTLFunction>)shader;
   }
}

void
mtl_render_pipeline_descriptor_set_input_primitive_topology(mtl_render_pass_descriptor *descriptor,
                                                            enum mtl_primitive_topology_class class)
{
   @autoreleasepool {
      MTLRenderPipelineDescriptor *desc = (MTLRenderPipelineDescriptor *)descriptor;
      desc.inputPrimitiveTopology = (MTLPrimitiveTopologyClass)class;
   }
}

void
mtl_render_pipeline_descriptor_set_color_attachment_format(mtl_render_pass_descriptor *descriptor,
                                                           uint8_t index,
                                                           enum mtl_pixel_format format)
{
   @autoreleasepool {
      MTLRenderPipelineDescriptor *desc = (MTLRenderPipelineDescriptor *)descriptor;
      desc.colorAttachments[index].pixelFormat = (MTLPixelFormat)format;
   }
}

void
mtl_render_pipeline_descriptor_set_depth_attachment_format(mtl_render_pass_descriptor *descriptor,
                                                           enum mtl_pixel_format format)
{
   @autoreleasepool {
      MTLRenderPipelineDescriptor *desc = (MTLRenderPipelineDescriptor *)descriptor;
      desc.depthAttachmentPixelFormat = (MTLPixelFormat)format;
   }
}

void
mtl_render_pipeline_descriptor_set_stencil_attachment_format(mtl_render_pass_descriptor *descriptor,
                                                             enum mtl_pixel_format format)
{
   @autoreleasepool {
      MTLRenderPipelineDescriptor *desc = (MTLRenderPipelineDescriptor *)descriptor;
      desc.stencilAttachmentPixelFormat = (MTLPixelFormat)format;
   }
}

void
mtl_render_pipeline_descriptor_set_raster_sample_count(mtl_render_pass_descriptor *descriptor,
                                                       uint32_t sample_count)
{
   @autoreleasepool {
      MTLRenderPipelineDescriptor *desc = (MTLRenderPipelineDescriptor *)descriptor;
      desc.rasterSampleCount = sample_count;
   }
}

void
mtl_render_pipeline_descriptor_set_alpha_to_coverage(mtl_render_pass_descriptor *descriptor,
                                                     bool enabled)
{
   @autoreleasepool {
      MTLRenderPipelineDescriptor *desc = (MTLRenderPipelineDescriptor *)descriptor;
      desc.alphaToCoverageEnabled = enabled;
   }
}

void
mtl_render_pipeline_descriptor_set_alpha_to_one(mtl_render_pass_descriptor *descriptor,
                                                bool enabled)
{
   @autoreleasepool {
      MTLRenderPipelineDescriptor *desc = (MTLRenderPipelineDescriptor *)descriptor;
      desc.alphaToOneEnabled = enabled;
   }
}

void
mtl_render_pipeline_descriptor_set_rasterization_enabled(mtl_render_pass_descriptor *descriptor,
                                                         bool enabled)
{
   @autoreleasepool {
      MTLRenderPipelineDescriptor *desc = (MTLRenderPipelineDescriptor *)descriptor;
      desc.rasterizationEnabled = enabled;
   }
}

void
mtl_render_pipeline_descriptor_set_max_vertex_amplification_count( mtl_render_pass_descriptor *descriptor,
                                                                  uint32_t count)
{
   @autoreleasepool {
      MTLRenderPipelineDescriptor *desc = (MTLRenderPipelineDescriptor *)descriptor;
      desc.maxVertexAmplificationCount = count;
   }
}

/* Render pipeline */
mtl_render_pipeline_state *
mtl_new_render_pipeline(mtl_device *device, mtl_render_pass_descriptor *descriptor)
{
   @autoreleasepool {
      MTLRenderPipelineDescriptor *desc = (MTLRenderPipelineDescriptor *)descriptor;
      id<MTLDevice> dev = (id<MTLDevice>)device;
      NSError *error = nil;
      mtl_render_pipeline_state *pipeline = [dev newRenderPipelineStateWithDescriptor:desc error:&error];
      if (error != nil) {
         fprintf(stderr, "Failed to create MTLLibrary: %s\n", [error.localizedDescription UTF8String]);
      }

      return pipeline;
   }
}

/* Stencil descriptor */
mtl_stencil_descriptor *
mtl_new_stencil_descriptor()
{
   @autoreleasepool {
      return [[MTLStencilDescriptor new] init];
   }
}

/* TODO_KOSMICKRISP Move this to map */
static MTLStencilOperation
map_vk_stencil_op_to_mtl_stencil_operation(VkStencilOp op)
{
   switch (op) {
   case VK_STENCIL_OP_KEEP:
      return MTLStencilOperationKeep;
   case VK_STENCIL_OP_ZERO:
      return MTLStencilOperationZero;
   case VK_STENCIL_OP_REPLACE:
      return MTLStencilOperationReplace;
   case VK_STENCIL_OP_INCREMENT_AND_CLAMP:
      return MTLStencilOperationIncrementClamp;
   case VK_STENCIL_OP_DECREMENT_AND_CLAMP:
      return MTLStencilOperationDecrementClamp;
   case VK_STENCIL_OP_INVERT:
      return MTLStencilOperationInvert;
   case VK_STENCIL_OP_INCREMENT_AND_WRAP:
      return MTLStencilOperationIncrementWrap;
   case VK_STENCIL_OP_DECREMENT_AND_WRAP:
      return MTLStencilOperationDecrementWrap;
   default:
      assert(false && "Unsupported VkStencilOp");
      return MTLStencilOperationZero;
   };
}

void
mtl_stencil_descriptor_set_stencil_failure_operation(mtl_stencil_descriptor *descriptor, VkStencilOp op)
{
   @autoreleasepool {
      MTLStencilDescriptor *desc = (MTLStencilDescriptor *)descriptor;
      desc.stencilFailureOperation = map_vk_stencil_op_to_mtl_stencil_operation(op);
   }
}

void
mtl_stencil_descriptor_set_depth_failure_operation(mtl_stencil_descriptor *descriptor, VkStencilOp op)
{
   @autoreleasepool {
      MTLStencilDescriptor *desc = (MTLStencilDescriptor *)descriptor;
      desc.depthFailureOperation = map_vk_stencil_op_to_mtl_stencil_operation(op);
   }
}

void
mtl_stencil_descriptor_set_depth_stencil_pass_operation(mtl_stencil_descriptor *descriptor, VkStencilOp op)
{
   @autoreleasepool {
      MTLStencilDescriptor *desc = (MTLStencilDescriptor *)descriptor;
      desc.depthStencilPassOperation = map_vk_stencil_op_to_mtl_stencil_operation(op);
   }
}

void
mtl_stencil_descriptor_set_stencil_compare_function(mtl_stencil_descriptor *descriptor, VkCompareOp op)
{
   @autoreleasepool {
      MTLStencilDescriptor *desc = (MTLStencilDescriptor *)descriptor;
      desc.stencilCompareFunction = (MTLCompareFunction)vk_compare_op_to_mtl_compare_function(op);
   }
}

void
mtl_stencil_descriptor_set_read_mask(mtl_stencil_descriptor *descriptor, uint32_t mask)
{
   @autoreleasepool {
      MTLStencilDescriptor *desc = (MTLStencilDescriptor *)descriptor;
      desc.readMask = mask;
   }
}

void
mtl_stencil_descriptor_set_write_mask(mtl_stencil_descriptor *descriptor, uint32_t mask)
{
   @autoreleasepool {
      MTLStencilDescriptor *desc = (MTLStencilDescriptor *)descriptor;
      desc.writeMask = mask;
   }
}

/* Depth stencil descriptor */
mtl_depth_stencil_descriptor *
mtl_new_depth_stencil_descriptor()
{
   @autoreleasepool {
      return [[MTLDepthStencilDescriptor new] init];
   }
}

void
mtl_depth_stencil_descriptor_set_depth_compare_function(mtl_depth_stencil_descriptor *descriptor, VkCompareOp op)
{
   @autoreleasepool {
      MTLDepthStencilDescriptor *desc = (MTLDepthStencilDescriptor *)descriptor;
      desc.depthCompareFunction = (MTLCompareFunction)vk_compare_op_to_mtl_compare_function(op);
   }
}

void
mtl_depth_stencil_descriptor_set_depth_write_enabled(mtl_depth_stencil_descriptor *descriptor, bool enable_write)
{
   @autoreleasepool {
      MTLDepthStencilDescriptor *desc = (MTLDepthStencilDescriptor *)descriptor;
      desc.depthWriteEnabled = enable_write;
   }
}

void
mtl_depth_stencil_descriptor_set_back_face_stencil(mtl_depth_stencil_descriptor *descriptor, mtl_stencil_descriptor *stencil_descriptor)
{
   @autoreleasepool {
      MTLDepthStencilDescriptor *desc = (MTLDepthStencilDescriptor *)descriptor;
      desc.backFaceStencil = (MTLStencilDescriptor *)stencil_descriptor;
   }
}

void
mtl_depth_stencil_descriptor_set_front_face_stencil(mtl_depth_stencil_descriptor *descriptor, mtl_stencil_descriptor *stencil_descriptor)
{
   @autoreleasepool {
      MTLDepthStencilDescriptor *desc = (MTLDepthStencilDescriptor *)descriptor;
      desc.frontFaceStencil = (MTLStencilDescriptor *)stencil_descriptor;
   }
}

mtl_depth_stencil_state *
mtl_new_depth_stencil_state(mtl_device *device, mtl_depth_stencil_descriptor *descriptor)
{
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;
      MTLDepthStencilDescriptor *desc = (MTLDepthStencilDescriptor *)descriptor;
      return [dev newDepthStencilStateWithDescriptor:desc];
   }
}
