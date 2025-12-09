/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_encoder.h"

/* Common encoder utils */
void
mtl_end_encoding(void *encoder)
{
}

/* MTLBlitEncoder */
mtl_blit_encoder *
mtl_new_blit_command_encoder(mtl_command_buffer *cmd_buffer)
{
   return NULL;
}

void
mtl_blit_update_fence(mtl_blit_encoder *encoder, mtl_fence *fence)
{
}

void
mtl_blit_wait_for_fence(mtl_blit_encoder *encoder, mtl_fence *fence)
{
}

void
mtl_copy_from_buffer_to_buffer(mtl_blit_encoder *blit_enc_handle,
                               mtl_buffer *src_buf, size_t src_offset,
                               mtl_buffer *dst_buf, size_t dst_offset,
                               size_t size)
{
}

void
mtl_copy_from_buffer_to_texture(mtl_blit_encoder *blit_enc_handle,
                                struct mtl_buffer_image_copy *data)
{
}

void
mtl_copy_from_texture_to_buffer(mtl_blit_encoder *blit_enc_handle,
                                struct mtl_buffer_image_copy *data)
{
}

void
mtl_copy_from_texture_to_texture(mtl_blit_encoder *blit_enc_handle,
                                 mtl_texture *src_tex_handle, size_t src_slice,
                                 size_t src_level, struct mtl_origin src_origin,
                                 struct mtl_size src_size,
                                 mtl_texture *dst_tex_handle, size_t dst_slice,
                                 size_t dst_level, struct mtl_origin dst_origin)
{
}

/* MTLComputeEncoder */
mtl_compute_encoder *
mtl_new_compute_command_encoder(mtl_command_buffer *cmd_buffer)
{
   return NULL;
}

void
mtl_compute_update_fence(mtl_compute_encoder *encoder, mtl_fence *fence)
{
}

void
mtl_compute_wait_for_fence(mtl_compute_encoder *encoder, mtl_fence *fence)
{
}

void
mtl_compute_set_pipeline_state(mtl_compute_encoder *encoder,
                               mtl_compute_pipeline_state *state_handle)
{
}

void
mtl_compute_set_buffer(mtl_compute_encoder *encoder, mtl_buffer *buffer,
                       size_t offset, size_t index)
{
}

void
mtl_compute_use_heaps(mtl_compute_encoder *encoder, mtl_heap **heaps,
                      uint32_t count)
{
}

void
mtl_dispatch_threads(mtl_compute_encoder *encoder, struct mtl_size grid_size,
                     struct mtl_size local_size)
{
}

void
mtl_dispatch_threadgroups_with_indirect_buffer(mtl_compute_encoder *encoder,
                                               mtl_buffer *buffer,
                                               uint32_t offset,
                                               struct mtl_size local_size)
{
}

/* MTLRenderEncoder */
mtl_render_encoder *
mtl_new_render_command_encoder_with_descriptor(
   mtl_command_buffer *command_buffer, mtl_render_pass_descriptor *descriptor)
{
   return NULL;
}

void
mtl_render_update_fence(mtl_render_encoder *encoder, mtl_fence *fence)
{
}

void
mtl_render_wait_for_fence(mtl_render_encoder *encoder, mtl_fence *fence)
{
}

void
mtl_set_viewports(mtl_render_encoder *encoder, struct mtl_viewport *viewports,
                  uint32_t count)
{
}

void
mtl_set_scissor_rects(mtl_render_encoder *encoder,
                      struct mtl_scissor_rect *scissor_rects, uint32_t count)
{
}

void
mtl_render_set_pipeline_state(mtl_render_encoder *encoder,
                              mtl_render_pipeline_state *pipeline)
{
}

void
mtl_set_depth_stencil_state(mtl_render_encoder *encoder,
                            mtl_depth_stencil_state *state)
{
}

void
mtl_set_stencil_references(mtl_render_encoder *encoder, uint32_t front,
                           uint32_t back)
{
}

void
mtl_set_front_face_winding(mtl_render_encoder *encoder,
                           enum mtl_winding winding)
{
}

void
mtl_set_cull_mode(mtl_render_encoder *encoder, enum mtl_cull_mode mode)
{
}

void
mtl_set_visibility_result_mode(mtl_render_encoder *encoder,
                               enum mtl_visibility_result_mode mode,
                               size_t offset)
{
}

void
mtl_set_depth_bias(mtl_render_encoder *encoder, float depth_bias,
                   float slope_scale, float clamp)
{
}

void
mtl_set_depth_clip_mode(mtl_render_encoder *encoder,
                        enum mtl_depth_clip_mode mode)
{
}

void
mtl_set_vertex_amplification_count(mtl_render_encoder *encoder,
                                   uint32_t *layer_ids, uint32_t id_count)
{
}

void
mtl_set_vertex_buffer(mtl_render_encoder *encoder, mtl_buffer *buffer,
                      uint32_t offset, uint32_t index)
{
}

void
mtl_set_fragment_buffer(mtl_render_encoder *encoder, mtl_buffer *buffer,
                        uint32_t offset, uint32_t index)
{
}

void
mtl_draw_primitives(mtl_render_encoder *encoder,
                    enum mtl_primitive_type primitve_type, uint32_t vertexStart,
                    uint32_t vertexCount, uint32_t instanceCount,
                    uint32_t baseInstance)
{
}

void
mtl_draw_indexed_primitives(
   mtl_render_encoder *encoder, enum mtl_primitive_type primitve_type,
   uint32_t index_count, enum mtl_index_type index_type,
   mtl_buffer *index_buffer, uint32_t index_buffer_offset,
   uint32_t instance_count, int32_t base_vertex, uint32_t base_instance)
{
}

void
mtl_draw_primitives_indirect(mtl_render_encoder *encoder,
                             enum mtl_primitive_type primitve_type,
                             mtl_buffer *indirect_buffer,
                             uint64_t indirect_buffer_offset)
{
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
}

void
mtl_render_use_resource(mtl_compute_encoder *encoder, mtl_resource *res_handle,
                        uint32_t usage)
{
}

void
mtl_render_use_resources(mtl_render_encoder *encoder,
                         mtl_resource **resource_handles, uint32_t count,
                         enum mtl_resource_usage usage)
{
}

void
mtl_render_use_heaps(mtl_render_encoder *encoder, mtl_heap **heaps,
                     uint32_t count)
{
}
