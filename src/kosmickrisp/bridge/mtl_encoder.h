/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef MTL_ENCODER_H
#define MTL_ENCODER_H 1

#include "mtl_types.h"

#include <stdint.h>

/* Common encoder utils */
void mtl_end_encoding(void *encoder);
void mtl_barrier_after_stages(void *encoder, enum mtl_stages after_stages,
                              enum mtl_stages before_queue_stages);
void mtl_barrier_after_encoder_stages(void *encoder,
                                      enum mtl_stages after_stages,
                                      enum mtl_stages before_queue_stages);

void mtl_update_fence(void *encoder, mtl_fence *fence,
                      enum mtl_stages after_stages);

void mtl_wait_for_fence(void *encoder, mtl_fence *fence,
                        enum mtl_stages before_stages);

/* MTLComputeEncoder */
mtl_compute_encoder *
mtl_new_compute_command_encoder(mtl_command_buffer *cmd_buffer);

void mtl_copy_from_buffer_to_buffer(mtl_compute_encoder *encoder,
                                    mtl_buffer *src_buf, size_t src_offset,
                                    mtl_buffer *dst_buf, size_t dst_offset,
                                    size_t size);

void mtl_copy_from_buffer_to_texture(mtl_compute_encoder *encoder,
                                     struct mtl_buffer_image_copy *data);

void mtl_copy_from_texture_to_buffer(mtl_compute_encoder *encoder,
                                     struct mtl_buffer_image_copy *data);

void mtl_copy_from_texture_to_texture(
   mtl_compute_encoder *encoder, mtl_texture *src_tex_handle, size_t src_slice,
   size_t src_level, struct mtl_origin src_origin, struct mtl_size src_size,
   mtl_texture *dst_tex_handle, size_t dst_slice, size_t dst_level,
   struct mtl_origin dst_origin);

void mtl_compute_set_pipeline_state(mtl_compute_encoder *encoder,
                                    mtl_compute_pipeline_state *state_handle);

void mtl_compute_set_argument_table(mtl_compute_encoder *encoder,
                                    mtl_argument_table *table);

void mtl_dispatch_threads(mtl_compute_encoder *encoder,
                          struct mtl_size grid_size,
                          struct mtl_size local_size);

void mtl_dispatch_threadgroups_with_indirect_buffer(
   mtl_compute_encoder *encoder, uint64_t addr, struct mtl_size local_size);

/* MTLRenderEncoder */
mtl_render_encoder *mtl_new_render_command_encoder_with_descriptor(
   mtl_command_buffer *command_buffer, mtl_render_pass_descriptor *descriptor);

void mtl_set_viewports(mtl_render_encoder *encoder,
                       struct mtl_viewport *viewports, uint32_t count);

void mtl_set_scissor_rects(mtl_render_encoder *encoder,
                           struct mtl_scissor_rect *scissor_rects,
                           uint32_t count);

void mtl_render_set_pipeline_state(mtl_render_encoder *encoder,
                                   mtl_render_pipeline_state *pipeline);

void mtl_set_depth_stencil_state(mtl_render_encoder *encoder,
                                 mtl_depth_stencil_state *state);

void mtl_set_stencil_references(mtl_render_encoder *encoder, uint32_t front,
                                uint32_t back);

void mtl_set_front_face_winding(mtl_render_encoder *encoder,
                                enum mtl_winding winding);

void mtl_set_cull_mode(mtl_render_encoder *encoder, enum mtl_cull_mode mode);

void mtl_set_visibility_result_mode(mtl_render_encoder *encoder,
                                    enum mtl_visibility_result_mode mode,
                                    size_t offset);

void mtl_set_depth_bias(mtl_render_encoder *encoder, float depth_bias,
                        float slope_scale, float clamp);

void mtl_set_depth_clip_mode(mtl_render_encoder *encoder,
                             enum mtl_depth_clip_mode mode);

void mtl_set_vertex_amplification_count(mtl_render_encoder *encoder,
                                        uint32_t *layer_ids, uint32_t id_count);

void mtl_render_set_argument_table(mtl_render_encoder *encoder,
                                   mtl_argument_table *table,
                                   enum mtl_render_stages stages);

void mtl_draw_primitives(mtl_render_encoder *encoder,
                         enum mtl_primitive_type primitve_type,
                         uint32_t vertexStart, uint32_t vertexCount,
                         uint32_t instanceCount, uint32_t baseInstance);

void mtl_draw_indexed_primitives(
   mtl_render_encoder *encoder, enum mtl_primitive_type primitve_type,
   uint32_t index_count, enum mtl_index_type index_type, uint64_t index_addr,
   uint64_t index_buffer_length, uint32_t instance_count, int32_t base_vertex,
   uint32_t base_instance);

void mtl_draw_primitives_indirect(mtl_render_encoder *encoder,
                                  enum mtl_primitive_type primitve_type,
                                  uint64_t addr);

void mtl_draw_indexed_primitives_indirect(mtl_render_encoder *encoder,
                                          enum mtl_primitive_type primitve_type,
                                          enum mtl_index_type index_type,
                                          uint64_t index_addr,
                                          uint64_t index_buffer_length,
                                          uint64_t addr);

#endif /* MTL_ENCODER_H */
