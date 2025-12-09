/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_encoder.h"

#include <Metal/MTLBlitCommandEncoder.h>
#include <Metal/MTLComputeCommandEncoder.h>
#include <Metal/MTLRenderCommandEncoder.h>

/* Common encoder utils */
void
mtl_end_encoding(void *encoder)
{
   @autoreleasepool {
      id<MTLCommandEncoder> enc = (id<MTLComputeCommandEncoder>)encoder;
      [enc endEncoding];
   }
}

/* MTLBlitEncoder */
mtl_blit_encoder *
mtl_new_blit_command_encoder(mtl_command_buffer *cmd_buffer)
{
   @autoreleasepool {
      id<MTLCommandBuffer> cmd_buf = (id<MTLCommandBuffer>)cmd_buffer;
      return [[cmd_buf blitCommandEncoder] retain];
   }
}

void
mtl_blit_update_fence(mtl_blit_encoder *encoder,
                      mtl_fence *fence)
{
   @autoreleasepool {
      id<MTLBlitCommandEncoder> enc = (id<MTLBlitCommandEncoder>)encoder;
      id<MTLFence> f = (id<MTLFence>)fence;
      [enc updateFence:f];
   }
}

void
mtl_blit_wait_for_fence(mtl_blit_encoder *encoder,
                            mtl_fence *fence)
{
   @autoreleasepool {
      id<MTLBlitCommandEncoder> enc = (id<MTLBlitCommandEncoder>)encoder;
      id<MTLFence> f = (id<MTLFence>)fence;
      [enc waitForFence:f];
   }
}

void
mtl_copy_from_buffer_to_buffer(mtl_blit_encoder *blit_enc_handle,
                               mtl_buffer *src_buf, size_t src_offset,
                               mtl_buffer *dst_buf, size_t dst_offset,
                               size_t size)
{
   @autoreleasepool {
      id<MTLBlitCommandEncoder> blit = (id<MTLBlitCommandEncoder>)blit_enc_handle;
      id<MTLBuffer> mtl_src_buffer = (id<MTLBuffer>)src_buf;
      id<MTLBuffer> mtl_dst_buffer = (id<MTLBuffer>)dst_buf;
      [blit copyFromBuffer:mtl_src_buffer sourceOffset:src_offset toBuffer:mtl_dst_buffer destinationOffset:dst_offset size:size];
   }
}

void
mtl_copy_from_buffer_to_texture(mtl_blit_encoder *blit_enc_handle,
                                struct mtl_buffer_image_copy *data)
{
   @autoreleasepool {
      const MTLSize size = MTLSizeMake(data->image_size.x, data->image_size.y, data->image_size.z);
      const MTLOrigin origin = MTLOriginMake(data->image_origin.x, data->image_origin.y, data->image_origin.z);
      id<MTLBlitCommandEncoder> blit = (id<MTLBlitCommandEncoder>)blit_enc_handle;
      id<MTLBuffer> buffer = (id<MTLBuffer>)data->buffer;
      id<MTLTexture> image = (id<MTLTexture>)data->image;
      [blit copyFromBuffer:buffer
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
mtl_copy_from_texture_to_buffer(mtl_blit_encoder *blit_enc_handle,
                                struct mtl_buffer_image_copy *data)
{
   @autoreleasepool {
      const MTLSize size = MTLSizeMake(data->image_size.x, data->image_size.y, data->image_size.z);
      const MTLOrigin origin = MTLOriginMake(data->image_origin.x, data->image_origin.y, data->image_origin.z);
      id<MTLBlitCommandEncoder> blit = (id<MTLBlitCommandEncoder>)blit_enc_handle;
      id<MTLBuffer> buffer = (id<MTLBuffer>)data->buffer;
      id<MTLTexture> image = (id<MTLTexture>)data->image;
      [blit copyFromTexture:image
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
mtl_copy_from_texture_to_texture(mtl_blit_encoder *blit_enc_handle,
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
      id<MTLBlitCommandEncoder> blit = (id<MTLBlitCommandEncoder>)blit_enc_handle;
      id<MTLTexture> mtl_src_tex = (id<MTLTexture>)src_tex_handle;
      [blit copyFromTexture:mtl_src_tex
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

/* MTLComputeEncoder */
mtl_compute_encoder *
mtl_new_compute_command_encoder(mtl_command_buffer *cmd_buffer)
{
   @autoreleasepool {
      id<MTLCommandBuffer> cmd_buf = (id<MTLCommandBuffer>)cmd_buffer;
      return [[cmd_buf computeCommandEncoder] retain];
   }
}

void
mtl_compute_update_fence(mtl_compute_encoder *encoder, mtl_fence *fence)
{
   @autoreleasepool {
      id<MTLComputeCommandEncoder> enc = (id<MTLComputeCommandEncoder>)encoder;
      id<MTLFence> f = (id<MTLFence>)fence;
      [enc updateFence:f];
   }
}

void
mtl_compute_wait_for_fence(mtl_compute_encoder *encoder, mtl_fence *fence)
{
   @autoreleasepool {
      id<MTLComputeCommandEncoder> enc = (id<MTLComputeCommandEncoder>)encoder;
      id<MTLFence> f = (id<MTLFence>)fence;
      [enc waitForFence:f];
   }
}

void
mtl_compute_set_pipeline_state(mtl_compute_encoder *encoder,
                               mtl_compute_pipeline_state *state_handle)
{
   @autoreleasepool {
      id<MTLComputeCommandEncoder> enc = (id<MTLComputeCommandEncoder>)encoder;
      id<MTLComputePipelineState> state = (id<MTLComputePipelineState>)state_handle;
      [enc setComputePipelineState:state];
   }
}

void
mtl_compute_set_buffer(mtl_compute_encoder *encoder,
                       mtl_buffer *buffer, size_t offset, size_t index)
{
   @autoreleasepool {
      id<MTLComputeCommandEncoder> enc = (id<MTLComputeCommandEncoder>)encoder;
      id<MTLBuffer> buf = (id<MTLBuffer>)buffer;
      [enc setBuffer:buf offset:offset atIndex:index];
   }
}

void
mtl_compute_use_heaps(mtl_compute_encoder *encoder, mtl_heap **heaps,
                      uint32_t count)
{
   @autoreleasepool {
      id<MTLComputeCommandEncoder> enc = (id<MTLComputeCommandEncoder>)encoder;
      id<MTLHeap> *handles = (id<MTLHeap>*)heaps;
      [enc useHeaps:handles count:count];
   }
}

void
mtl_dispatch_threads(mtl_compute_encoder *encoder,
                     struct mtl_size grid_size, struct mtl_size local_size)
{
   @autoreleasepool {
      id<MTLComputeCommandEncoder> enc = (id<MTLComputeCommandEncoder>)encoder;
      MTLSize thread_count = MTLSizeMake(grid_size.x * local_size.x,
                                         grid_size.y * local_size.y,
                                         grid_size.z * local_size.z);
      MTLSize threads_per_threadgroup = MTLSizeMake(local_size.x,
                                                    local_size.y,
                                                    local_size.z);

      // TODO_KOSMICKRISP can we rely on nonuniform threadgroup size support?
      [enc dispatchThreads:thread_count threadsPerThreadgroup:threads_per_threadgroup];
   }
}

void
mtl_dispatch_threadgroups_with_indirect_buffer(mtl_compute_encoder *encoder,
                                               mtl_buffer *buffer,
                                               uint32_t offset,
                                               struct mtl_size local_size)
{
   @autoreleasepool {
      id<MTLComputeCommandEncoder> enc = (id<MTLComputeCommandEncoder>)encoder;
      id<MTLBuffer> buf = (id<MTLBuffer>)buffer;
      MTLSize threads_per_threadgroup = MTLSizeMake(local_size.x,
                                                    local_size.y,
                                                    local_size.z);

      [enc dispatchThreadgroupsWithIndirectBuffer:buf indirectBufferOffset:offset threadsPerThreadgroup:threads_per_threadgroup];
   }
}

/* MTLRenderEncoder */

/* Encoder commands */
mtl_render_encoder *
mtl_new_render_command_encoder_with_descriptor(
   mtl_command_buffer *command_buffer, mtl_render_pass_descriptor *descriptor)
{
   @autoreleasepool {
      id<MTLCommandBuffer> cmd = (id<MTLCommandBuffer>)command_buffer;
      MTLRenderPassDescriptor *desc = (MTLRenderPassDescriptor *)descriptor;
      return [[cmd renderCommandEncoderWithDescriptor:desc] retain];
   }
}

void
mtl_render_update_fence(mtl_render_encoder *encoder, mtl_fence *fence)
{
   @autoreleasepool {
      id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      id<MTLFence> f = (id<MTLFence>)fence;
      [enc updateFence:f afterStages:MTLRenderStageFragment];
   }
}

void
mtl_render_wait_for_fence(mtl_render_encoder *encoder, mtl_fence *fence)
{
   @autoreleasepool {
      id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      id<MTLFence> f = (id<MTLFence>)fence;
      [enc waitForFence:f beforeStages:MTLRenderStageVertex];
   }
}

void
mtl_set_viewports(mtl_render_encoder *encoder, struct mtl_viewport *viewports,
                  uint32_t count)
{
   @autoreleasepool {
      id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      MTLViewport *vps = (MTLViewport *)viewports;
      [enc setViewports:vps count:count];
   }
}

void
mtl_set_scissor_rects(mtl_render_encoder *encoder,
                      struct mtl_scissor_rect *scissor_rects, uint32_t count)
{
   @autoreleasepool {
      id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      MTLScissorRect *rects = (MTLScissorRect *)scissor_rects;
      [enc setScissorRects:rects count:count];
   }
}

void
mtl_render_set_pipeline_state(mtl_render_encoder *encoder,
                              mtl_render_pipeline_state *pipeline)
{
   @autoreleasepool {
      id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      id<MTLRenderPipelineState> pipe = (id<MTLRenderPipelineState>)pipeline;
      [enc setRenderPipelineState:pipe];
   }
}

void
mtl_set_depth_stencil_state(mtl_render_encoder *encoder,
                            mtl_depth_stencil_state *state)
{
   @autoreleasepool {
      id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      id<MTLDepthStencilState> s = (id<MTLDepthStencilState>)state;
      [enc setDepthStencilState:s];
   }
}

void
mtl_set_stencil_references(mtl_render_encoder *encoder, uint32_t front,
                           uint32_t back)
{
   @autoreleasepool {
      id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      [enc setStencilFrontReferenceValue:front backReferenceValue:back];
   }
}

void
mtl_set_front_face_winding(mtl_render_encoder *encoder,
                           enum mtl_winding winding)
{
   @autoreleasepool {
      id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      [enc setFrontFacingWinding:(MTLWinding)winding];
   }
}

void
mtl_set_cull_mode(mtl_render_encoder *encoder, enum mtl_cull_mode mode)
{
   @autoreleasepool {
      id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      [enc setCullMode:(MTLCullMode)mode];
   }
}

void
mtl_set_visibility_result_mode(mtl_render_encoder *encoder,
                               enum mtl_visibility_result_mode mode,
                               size_t offset)
{
   @autoreleasepool {
      id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      [enc setVisibilityResultMode:(MTLVisibilityResultMode)mode offset:offset];
   }
}

void
mtl_set_depth_bias(mtl_render_encoder *encoder, float depth_bias,
                   float slope_scale, float clamp)
{
   @autoreleasepool {
      id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      [enc setDepthBias:depth_bias slopeScale:slope_scale clamp:clamp];
   }
}

void
mtl_set_depth_clip_mode(mtl_render_encoder *encoder,
                        enum mtl_depth_clip_mode mode)
{
   @autoreleasepool {
      id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      [enc setDepthClipMode:(MTLDepthClipMode)mode];
   }
}

void
mtl_set_vertex_amplification_count(mtl_render_encoder *encoder,
                                   uint32_t *layer_ids, uint32_t id_count)
{
   @autoreleasepool {
      id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      MTLVertexAmplificationViewMapping mappings[32];
      for (uint32_t i = 0u; i < id_count; ++i) {
         mappings[i].renderTargetArrayIndexOffset = layer_ids[i];
         mappings[i].viewportArrayIndexOffset = 0u;
      }
      [enc setVertexAmplificationCount:id_count viewMappings:mappings];
   }
}

void
mtl_set_vertex_buffer(mtl_render_encoder *encoder, mtl_buffer *buffer,
                      uint32_t offset, uint32_t index)
{
   @autoreleasepool {
      id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      id<MTLBuffer> buf = (id<MTLBuffer>)buffer;
      [enc setVertexBuffer:buf offset:offset atIndex:index];
   }
}

void
mtl_set_fragment_buffer(mtl_render_encoder *encoder, mtl_buffer *buffer,
                        uint32_t offset, uint32_t index)
{
   @autoreleasepool {
      id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      id<MTLBuffer> buf = (id<MTLBuffer>)buffer;
      [enc setFragmentBuffer:buf offset:offset atIndex:index];
   }
}

void
mtl_draw_primitives(mtl_render_encoder *encoder,
                    enum mtl_primitive_type primitve_type, uint32_t vertexStart,
                    uint32_t vertexCount, uint32_t instanceCount,
                    uint32_t baseInstance)
{
   @autoreleasepool {
      id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      MTLPrimitiveType type = (MTLPrimitiveType)primitve_type;
      [enc drawPrimitives:type vertexStart:vertexStart vertexCount:vertexCount instanceCount:instanceCount baseInstance:baseInstance];
   }
}

void
mtl_draw_indexed_primitives(
   mtl_render_encoder *encoder, enum mtl_primitive_type primitve_type,
   uint32_t index_count, enum mtl_index_type index_type,
   mtl_buffer *index_buffer, uint32_t index_buffer_offset,
   uint32_t instance_count, int32_t base_vertex, uint32_t base_instance)
{
   @autoreleasepool {
      id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      id<MTLBuffer> buf = (id<MTLBuffer>)index_buffer;
      MTLIndexType ndx_type = (MTLIndexType)index_type;
      MTLPrimitiveType primitive = (MTLPrimitiveType)primitve_type;
      [enc drawIndexedPrimitives:primitive indexCount:index_count indexType:ndx_type indexBuffer:buf indexBufferOffset:index_buffer_offset instanceCount:instance_count baseVertex:base_vertex baseInstance:base_instance];
   }
}

void
mtl_draw_primitives_indirect(mtl_render_encoder *encoder,
                             enum mtl_primitive_type primitve_type,
                             mtl_buffer *indirect_buffer,
                             uint64_t indirect_buffer_offset)
{
   @autoreleasepool {
      id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      id<MTLBuffer> buf = (id<MTLBuffer>)indirect_buffer;
      MTLPrimitiveType type = (MTLPrimitiveType)primitve_type;
      [enc drawPrimitives:type indirectBuffer:buf indirectBufferOffset:indirect_buffer_offset];
   }
}

void
mtl_draw_indexed_primitives_indirect(mtl_render_encoder *encoder,
                                     enum mtl_primitive_type primitve_type,
                                     enum mtl_index_type index_type,
                                     mtl_buffer *index_buffer,
                                     uint32_t index_buffer_offset,
                                     mtl_buffer *indirect_buffer,
                                     uint64_t indirect_buffer_offset)
{
   @autoreleasepool {
      id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      id<MTLBuffer> buf = (id<MTLBuffer>)indirect_buffer;
      id<MTLBuffer> ndx_buf = (id<MTLBuffer>)index_buffer;
      MTLPrimitiveType type = (MTLPrimitiveType)primitve_type;
      MTLIndexType ndx_type = (MTLIndexType)index_type;
      [enc drawIndexedPrimitives:type indexType:ndx_type indexBuffer:ndx_buf indexBufferOffset:index_buffer_offset indirectBuffer:buf indirectBufferOffset:indirect_buffer_offset];
   }
}

void
mtl_render_use_resource(mtl_compute_encoder *encoder, mtl_resource *res_handle,
                        uint32_t usage)
{
   @autoreleasepool {
      id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      id<MTLResource> res = (id<MTLResource>)res_handle;
      [enc useResource:res usage:(MTLResourceUsage)usage stages:MTLRenderStageVertex|MTLRenderStageFragment];
   }
}

void
mtl_render_use_resources(mtl_render_encoder *encoder,
                         mtl_resource **resource_handles, uint32_t count,
                         enum mtl_resource_usage usage)
{
   @autoreleasepool {
      // id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      id<MTLResource> *handles = (id<MTLResource>*)resource_handles;
      for (uint32_t i = 0u; i < count; ++i) {
         if (handles[i] != NULL)
            mtl_render_use_resource(encoder, handles[i], usage);
      }
      /* TODO_KOSMICKRISP No null values in the array or Metal complains */
      // [enc useResources:handles count:count usage:(MTLResourceUsage)usage];
   }
}

void
mtl_render_use_heaps(mtl_render_encoder *encoder, mtl_heap **heaps,
                     uint32_t count)
{
   @autoreleasepool {
      id<MTLRenderCommandEncoder> enc = (id<MTLRenderCommandEncoder>)encoder;
      id<MTLHeap> *handles = (id<MTLHeap>*)heaps;
      [enc useHeaps:handles count:count stages:MTLRenderStageVertex|MTLRenderStageFragment];
   }
}
