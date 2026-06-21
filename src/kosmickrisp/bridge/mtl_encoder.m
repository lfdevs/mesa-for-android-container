/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_encoder.h"

#include <Metal/MTL4ComputeCommandEncoder.h>
#include <Metal/MTL4RenderCommandEncoder.h>
#include <Metal/MTL4CommandBuffer.h>
#include <Metal/MTLRenderPass.h>

/* Common encoder utils */
void
mtl_end_encoding(void *encoder)
{
   @autoreleasepool {
      id<MTL4CommandEncoder> enc = (id<MTL4CommandEncoder>)encoder;
      [enc endEncoding];
   }
}

void
mtl_barrier_after_stages(void *encoder, enum mtl_stages after_stages,
                         enum mtl_stages before_queue_stages)
{
   @autoreleasepool {
      id<MTL4CommandEncoder> enc = (id<MTL4CommandEncoder>)encoder;
      /* TODO_KOSMICKRISP Expose visibility option since resource alias should only be required
       * if occlusion queries are used */
      [enc barrierAfterStages:(MTLStages)after_stages beforeQueueStages:(MTLStages)before_queue_stages
           visibilityOptions:MTL4VisibilityOptionResourceAlias];
   }
}

void
mtl_barrier_after_encoder_stages(void *encoder, enum mtl_stages after_stages,
                                 enum mtl_stages before_queue_stages)
{
   @autoreleasepool {
      id<MTL4CommandEncoder> enc = (id<MTL4CommandEncoder>)encoder;
      [enc barrierAfterEncoderStages:(MTLStages)after_stages beforeEncoderStages:(MTLStages)before_queue_stages
           visibilityOptions:MTL4VisibilityOptionResourceAlias];
   }
}

void
mtl_update_fence(void *encoder, mtl_fence *fence, enum mtl_stages after_stages)
{
   @autoreleasepool {
      id<MTL4CommandEncoder> enc = (id<MTL4CommandEncoder>)encoder;
      id<MTLFence> f = (id<MTLFence>)fence;
      [enc updateFence:f afterEncoderStages:(MTLStages)after_stages];
   }
}

void
mtl_wait_for_fence(void *encoder, mtl_fence *fence,
                   enum mtl_stages before_stages)
{
   @autoreleasepool {
      id<MTL4CommandEncoder> enc = (id<MTL4CommandEncoder>)encoder;
      id<MTLFence> f = (id<MTLFence>)fence;
      [enc waitForFence:f beforeEncoderStages:(MTLStages)before_stages];
   }
}

/* MTLComputeEncoder */
mtl_compute_encoder *
mtl_new_compute_command_encoder(mtl_command_buffer *cmd_buffer)
{
   @autoreleasepool {
      id<MTL4CommandBuffer> cmd_buf = (id<MTL4CommandBuffer>)cmd_buffer;
      return [[cmd_buf computeCommandEncoder] retain];
   }
}

void
mtl_copy_from_buffer_to_buffer(mtl_compute_encoder *encoder,
                               mtl_buffer *src_buf, size_t src_offset,
                               mtl_buffer *dst_buf, size_t dst_offset,
                               size_t size)
{
   @autoreleasepool {
      id<MTL4ComputeCommandEncoder> enc = (id<MTL4ComputeCommandEncoder>)encoder;
      id<MTLBuffer> mtl_src_buffer = (id<MTLBuffer>)src_buf;
      id<MTLBuffer> mtl_dst_buffer = (id<MTLBuffer>)dst_buf;
      [enc copyFromBuffer:mtl_src_buffer sourceOffset:src_offset toBuffer:mtl_dst_buffer destinationOffset:dst_offset size:size];
   }
}

void
mtl_copy_from_buffer_to_texture(mtl_compute_encoder *encoder,
                                struct mtl_buffer_image_copy *data)
{
   @autoreleasepool {
      const MTLSize size = MTLSizeMake(data->image_size.x, data->image_size.y, data->image_size.z);
      const MTLOrigin origin = MTLOriginMake(data->image_origin.x, data->image_origin.y, data->image_origin.z);
      id<MTL4ComputeCommandEncoder> enc = (id<MTL4ComputeCommandEncoder>)encoder;
      id<MTLBuffer> buffer = (id<MTLBuffer>)data->buffer;
      id<MTLTexture> image = (id<MTLTexture>)data->image;
      [enc copyFromBuffer:buffer
             sourceOffset:data->buffer_offset_B
        sourceBytesPerRow:data->buffer_stride_B
      sourceBytesPerImage:data->buffer_2d_image_size_B
               sourceSize:size
                toTexture:image
         destinationSlice:data->image_slice
         destinationLevel:data->image_level
        destinationOrigin:origin
                  options:(MTLBlitOption)data->options];
   }
}

void
mtl_copy_from_texture_to_buffer(mtl_compute_encoder *encoder,
                                struct mtl_buffer_image_copy *data)
{
   @autoreleasepool {
      const MTLSize size = MTLSizeMake(data->image_size.x, data->image_size.y, data->image_size.z);
      const MTLOrigin origin = MTLOriginMake(data->image_origin.x, data->image_origin.y, data->image_origin.z);
      id<MTL4ComputeCommandEncoder> enc = (id<MTL4ComputeCommandEncoder>)encoder;
      id<MTLBuffer> buffer = (id<MTLBuffer>)data->buffer;
      id<MTLTexture> image = (id<MTLTexture>)data->image;
      [enc copyFromTexture:image
               sourceSlice:data->image_slice
               sourceLevel:data->image_level
              sourceOrigin:origin
                sourceSize:size
                  toBuffer:buffer
         destinationOffset:data->buffer_offset_B
    destinationBytesPerRow:data->buffer_stride_B
  destinationBytesPerImage:data->buffer_2d_image_size_B
                   options:(MTLBlitOption)data->options];
   }
}

void
mtl_copy_from_texture_to_texture(mtl_compute_encoder *encoder,
                                 mtl_texture *src_tex_handle, size_t src_slice,
                                 size_t src_level, struct mtl_origin src_origin,
                                 struct mtl_size src_size,
                                 mtl_texture *dst_tex_handle, size_t dst_slice,
                                 size_t dst_level, struct mtl_origin dst_origin)
{
   @autoreleasepool {
      MTLOrigin mtl_src_origin = MTLOriginMake(src_origin.x, src_origin.y, src_origin.z);
      MTLSize mtl_src_size = MTLSizeMake(src_size.x, src_size.y, src_size.z);
      MTLOrigin mtl_dst_origin = MTLOriginMake(dst_origin.x, dst_origin.y, dst_origin.z);
      id<MTLTexture> mtl_dst_tex = (id<MTLTexture>)dst_tex_handle;
      id<MTL4ComputeCommandEncoder> enc = (id<MTL4ComputeCommandEncoder>)encoder;
      id<MTLTexture> mtl_src_tex = (id<MTLTexture>)src_tex_handle;
      [enc copyFromTexture:mtl_src_tex
               sourceSlice:src_slice
               sourceLevel:src_level
              sourceOrigin:mtl_src_origin
                sourceSize:mtl_src_size
                 toTexture:mtl_dst_tex
          destinationSlice:dst_slice
          destinationLevel:dst_level
         destinationOrigin:mtl_dst_origin];
   }
}

void
mtl_compute_set_pipeline_state(mtl_compute_encoder *encoder,
                               mtl_compute_pipeline_state *state_handle)
{
   @autoreleasepool {
      id<MTL4ComputeCommandEncoder> enc = (id<MTL4ComputeCommandEncoder>)encoder;
      id<MTLComputePipelineState> state = (id<MTLComputePipelineState>)state_handle;
      [enc setComputePipelineState:state];
   }
}

void
mtl_compute_set_argument_table(mtl_compute_encoder *encoder,
                               mtl_argument_table *table)
{
   @autoreleasepool {
      id<MTL4ComputeCommandEncoder> enc = (id<MTL4ComputeCommandEncoder>)encoder;
      id<MTL4ArgumentTable> t = (id<MTL4ArgumentTable>)table;
      [enc setArgumentTable:t];
   }
}

void
mtl_dispatch_threads(mtl_compute_encoder *encoder,
                     struct mtl_size grid_size, struct mtl_size local_size)
{
   @autoreleasepool {
      id<MTL4ComputeCommandEncoder> enc = (id<MTL4ComputeCommandEncoder>)encoder;
      MTLSize thread_count = MTLSizeMake(grid_size.x, grid_size.y, grid_size.z);
      MTLSize threads_per_threadgroup = MTLSizeMake(local_size.x, local_size.y,
                                                    local_size.z);

      [enc dispatchThreads:thread_count threadsPerThreadgroup:threads_per_threadgroup];
   }
}

void
mtl_dispatch_threadgroups_with_indirect_buffer(mtl_compute_encoder *encoder,
                                               uint64_t addr,
                                               struct mtl_size local_size)
{
   @autoreleasepool {
      id<MTL4ComputeCommandEncoder> enc = (id<MTL4ComputeCommandEncoder>)encoder;
      MTLSize threads_per_threadgroup = MTLSizeMake(local_size.x,
                                                    local_size.y,
                                                    local_size.z);

      [enc dispatchThreadgroupsWithIndirectBuffer:addr threadsPerThreadgroup:threads_per_threadgroup];
   }
}

/* MTLRenderEncoder */

/* Encoder commands */
mtl_render_encoder *
mtl_new_render_command_encoder_with_descriptor(
   mtl_command_buffer *command_buffer, mtl_render_pass_descriptor *descriptor)
{
   @autoreleasepool {
      id<MTL4CommandBuffer> cmd = (id<MTL4CommandBuffer>)command_buffer;
      MTL4RenderPassDescriptor *desc = (MTL4RenderPassDescriptor *)descriptor;
      return [[cmd renderCommandEncoderWithDescriptor:desc] retain];
   }
}

void
mtl_set_viewports(mtl_render_encoder *encoder, struct mtl_viewport *viewports,
                  uint32_t count)
{
   @autoreleasepool {
      id<MTL4RenderCommandEncoder> enc = (id<MTL4RenderCommandEncoder>)encoder;
      MTLViewport *vps = (MTLViewport *)viewports;
      [enc setViewports:vps count:count];
   }
}

void
mtl_set_scissor_rects(mtl_render_encoder *encoder,
                      struct mtl_scissor_rect *scissor_rects, uint32_t count)
{
   @autoreleasepool {
      id<MTL4RenderCommandEncoder> enc = (id<MTL4RenderCommandEncoder>)encoder;
      MTLScissorRect *rects = (MTLScissorRect *)scissor_rects;
      [enc setScissorRects:rects count:count];
   }
}

void
mtl_render_set_pipeline_state(mtl_render_encoder *encoder,
                              mtl_render_pipeline_state *pipeline)
{
   @autoreleasepool {
      id<MTL4RenderCommandEncoder> enc = (id<MTL4RenderCommandEncoder>)encoder;
      id<MTLRenderPipelineState> pipe = (id<MTLRenderPipelineState>)pipeline;
      [enc setRenderPipelineState:pipe];
   }
}

void
mtl_set_depth_stencil_state(mtl_render_encoder *encoder,
                            mtl_depth_stencil_state *state)
{
   @autoreleasepool {
      id<MTL4RenderCommandEncoder> enc = (id<MTL4RenderCommandEncoder>)encoder;
      id<MTLDepthStencilState> s = (id<MTLDepthStencilState>)state;
      [enc setDepthStencilState:s];
   }
}

void
mtl_set_stencil_references(mtl_render_encoder *encoder, uint32_t front,
                           uint32_t back)
{
   @autoreleasepool {
      id<MTL4RenderCommandEncoder> enc = (id<MTL4RenderCommandEncoder>)encoder;
      [enc setStencilFrontReferenceValue:front backReferenceValue:back];
   }
}

void
mtl_set_front_face_winding(mtl_render_encoder *encoder,
                           enum mtl_winding winding)
{
   @autoreleasepool {
      id<MTL4RenderCommandEncoder> enc = (id<MTL4RenderCommandEncoder>)encoder;
      [enc setFrontFacingWinding:(MTLWinding)winding];
   }
}

void
mtl_set_cull_mode(mtl_render_encoder *encoder, enum mtl_cull_mode mode)
{
   @autoreleasepool {
      id<MTL4RenderCommandEncoder> enc = (id<MTL4RenderCommandEncoder>)encoder;
      [enc setCullMode:(MTLCullMode)mode];
   }
}

void
mtl_set_visibility_result_mode(mtl_render_encoder *encoder,
                               enum mtl_visibility_result_mode mode,
                               size_t offset)
{
   @autoreleasepool {
      id<MTL4RenderCommandEncoder> enc = (id<MTL4RenderCommandEncoder>)encoder;
      [enc setVisibilityResultMode:(MTLVisibilityResultMode)mode offset:offset];
   }
}

void
mtl_set_depth_bias(mtl_render_encoder *encoder, float depth_bias,
                   float slope_scale, float clamp)
{
   @autoreleasepool {
      id<MTL4RenderCommandEncoder> enc = (id<MTL4RenderCommandEncoder>)encoder;
      [enc setDepthBias:depth_bias slopeScale:slope_scale clamp:clamp];
   }
}

void
mtl_set_depth_clip_mode(mtl_render_encoder *encoder,
                        enum mtl_depth_clip_mode mode)
{
   @autoreleasepool {
      id<MTL4RenderCommandEncoder> enc = (id<MTL4RenderCommandEncoder>)encoder;
      [enc setDepthClipMode:(MTLDepthClipMode)mode];
   }
}

void
mtl_set_vertex_amplification_count(mtl_render_encoder *encoder,
                                   uint32_t *layer_ids, uint32_t id_count)
{
   @autoreleasepool {
      id<MTL4RenderCommandEncoder> enc = (id<MTL4RenderCommandEncoder>)encoder;
      MTLVertexAmplificationViewMapping mappings[32];
      for (uint32_t i = 0u; i < id_count; ++i) {
         mappings[i].renderTargetArrayIndexOffset = layer_ids[i];
         mappings[i].viewportArrayIndexOffset = 0u;
      }
      [enc setVertexAmplificationCount:id_count viewMappings:mappings];
   }
}

void
mtl_render_set_argument_table(mtl_render_encoder *encoder,
                              mtl_argument_table *table,
                              enum mtl_render_stages stages)
{
   @autoreleasepool {
      id<MTL4RenderCommandEncoder> enc = (id<MTL4RenderCommandEncoder>)encoder;
      id<MTL4ArgumentTable> t = (id<MTL4ArgumentTable>)table;
      [enc setArgumentTable:t atStages:(MTLRenderStages)stages];
   }
}

void
mtl_draw_primitives(mtl_render_encoder *encoder,
                    enum mtl_primitive_type primitve_type, uint32_t vertexStart,
                    uint32_t vertexCount, uint32_t instanceCount,
                    uint32_t baseInstance)
{
   @autoreleasepool {
      id<MTL4RenderCommandEncoder> enc = (id<MTL4RenderCommandEncoder>)encoder;
      MTLPrimitiveType type = (MTLPrimitiveType)primitve_type;
      [enc drawPrimitives:type vertexStart:vertexStart vertexCount:vertexCount instanceCount:instanceCount baseInstance:baseInstance];
   }
}

void
mtl_draw_indexed_primitives(mtl_render_encoder *encoder,
                            enum mtl_primitive_type primitve_type,
                            uint32_t index_count,
                            enum mtl_index_type index_type, uint64_t index_addr,
                            uint64_t index_buffer_length,
                            uint32_t instance_count, int32_t base_vertex,
                            uint32_t base_instance)
{
   @autoreleasepool {
      id<MTL4RenderCommandEncoder> enc = (id<MTL4RenderCommandEncoder>)encoder;
      MTLIndexType ndx_type = (MTLIndexType)index_type;
      MTLPrimitiveType primitive = (MTLPrimitiveType)primitve_type;
      [enc drawIndexedPrimitives:primitive
                      indexCount:index_count
                       indexType:ndx_type
                     indexBuffer:index_addr
               indexBufferLength:index_buffer_length
                   instanceCount:instance_count
                      baseVertex:base_vertex
                    baseInstance:base_instance];
   }
}

void
mtl_draw_primitives_indirect(mtl_render_encoder *encoder,
                             enum mtl_primitive_type primitve_type,
                             uint64_t addr)
{
   @autoreleasepool {
      id<MTL4RenderCommandEncoder> enc = (id<MTL4RenderCommandEncoder>)encoder;
      [enc drawPrimitives:(MTLPrimitiveType)primitve_type indirectBuffer:addr];
   }
}

void
mtl_draw_indexed_primitives_indirect(mtl_render_encoder *encoder,
                                     enum mtl_primitive_type primitve_type,
                                     enum mtl_index_type index_type,
                                     uint64_t index_addr,
                                     uint64_t index_buffer_length,
                                     uint64_t addr)
{
   @autoreleasepool {
      id<MTL4RenderCommandEncoder> enc = (id<MTL4RenderCommandEncoder>)encoder;
      MTLPrimitiveType type = (MTLPrimitiveType)primitve_type;
      MTLIndexType ndx_type = (MTLIndexType)index_type;
      [enc drawIndexedPrimitives:type
                       indexType:ndx_type
                     indexBuffer:index_addr
               indexBufferLength:index_buffer_length
                  indirectBuffer:addr];
   }
}
