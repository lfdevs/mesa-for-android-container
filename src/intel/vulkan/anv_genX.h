/*
 * Copyright © 2016 Intel Corporation
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

/*
 * NOTE: The header can be included multiple times, from the same file.
 */

/*
 * Gen-specific function declarations.  This header must *not* be included
 * directly.  Instead, it is included multiple times by anv_private.h.
 *
 * In this header file, the usual genx() macro is available.
 */

#ifndef ANV_PRIVATE_H
#error This file is included by means other than anv_private.h
#endif

struct intel_sample_positions;

extern const uint32_t genX(vk_to_intel_cullmode)[];

extern const uint32_t genX(vk_to_intel_front_face)[];

extern const uint32_t genX(vk_to_intel_primitive_type)[];

extern const uint32_t genX(vk_to_intel_compare_op)[];

extern const uint32_t genX(vk_to_intel_stencil_op)[];

extern const uint32_t genX(vk_to_intel_logic_op)[];

extern const uint32_t genX(vk_to_intel_fillmode)[];

void genX(init_physical_device_state)(struct anv_physical_device *device);

VkResult genX(init_device_state)(struct anv_device *device);

void genX(init_cps_device_state)(struct anv_device *device);

void genX(cmd_buffer_emit_state_base_address)(struct anv_cmd_buffer *cmd_buffer);

void genX(cmd_buffer_apply_pipe_flushes)(struct anv_cmd_buffer *cmd_buffer);

void genX(cmd_buffer_emit_gfx12_depth_wa)(struct anv_cmd_buffer *cmd_buffer,
                                          const struct isl_surf *surf);

void genX(cmd_buffer_set_binding_for_gfx8_vb_flush)(struct anv_cmd_buffer *cmd_buffer,
                                                    int vb_index,
                                                    struct anv_address vb_address,
                                                    uint32_t vb_size);
void genX(cmd_buffer_update_dirty_vbs_for_gfx8_vb_flush)(struct anv_cmd_buffer *cmd_buffer,
                                                         uint32_t access_type,
                                                         uint64_t vb_used);

void genX(cmd_buffer_emit_hashing_mode)(struct anv_cmd_buffer *cmd_buffer,
                                        unsigned width, unsigned height,
                                        unsigned scale);

void genX(flush_pipeline_select_3d)(struct anv_cmd_buffer *cmd_buffer);
void genX(flush_pipeline_select_gpgpu)(struct anv_cmd_buffer *cmd_buffer);
void genX(emit_pipeline_select)(struct anv_batch *batch, uint32_t pipeline);

void genX(apply_task_urb_workaround)(struct anv_cmd_buffer *cmd_buffer);

void genX(emit_vertex_input)(struct anv_batch *batch,
                             uint32_t *vertex_element_dws,
                             const struct anv_graphics_pipeline *pipeline,
                             const struct vk_vertex_input_state *vi);

enum anv_pipe_bits
genX(emit_apply_pipe_flushes)(struct anv_batch *batch,
                              struct anv_device *device,
                              uint32_t current_pipeline,
                              enum anv_pipe_bits bits,
                              enum anv_query_bits *query_bits);

void genX(emit_so_memcpy_init)(struct anv_memcpy_state *state,
                               struct anv_device *device,
                               struct anv_batch *batch);

void genX(emit_so_memcpy_fini)(struct anv_memcpy_state *state);

void genX(emit_so_memcpy_end)(struct anv_memcpy_state *state);

void genX(emit_so_memcpy)(struct anv_memcpy_state *state,
                          struct anv_address dst, struct anv_address src,
                          uint32_t size);

void genX(emit_l3_config)(struct anv_batch *batch,
                          const struct anv_device *device,
                          const struct intel_l3_config *cfg);

void genX(cmd_buffer_config_l3)(struct anv_cmd_buffer *cmd_buffer,
                                const struct intel_l3_config *cfg);

void genX(cmd_buffer_flush_dynamic_state)(struct anv_cmd_buffer *cmd_buffer);

void genX(cmd_buffer_enable_pma_fix)(struct anv_cmd_buffer *cmd_buffer,
                                     bool enable);

void genX(cmd_buffer_mark_image_written)(struct anv_cmd_buffer *cmd_buffer,
                                         const struct anv_image *image,
                                         VkImageAspectFlagBits aspect,
                                         enum isl_aux_usage aux_usage,
                                         uint32_t level,
                                         uint32_t base_layer,
                                         uint32_t layer_count);

void genX(cmd_emit_conditional_render_predicate)(struct anv_cmd_buffer *cmd_buffer);

struct anv_state genX(cmd_buffer_ray_query_globals)(struct anv_cmd_buffer *cmd_buffer);

void genX(cmd_buffer_ensure_cfe_state)(struct anv_cmd_buffer *cmd_buffer,
                                       uint32_t total_scratch);

void
genX(emit_urb_setup)(struct anv_device *device, struct anv_batch *batch,
                     const struct intel_l3_config *l3_config,
                     VkShaderStageFlags active_stages,
                     const unsigned entry_size[4],
                     enum intel_urb_deref_block_size *deref_block_size);

void genX(emit_multisample)(struct anv_batch *batch, uint32_t samples);

void genX(emit_sample_pattern)(struct anv_batch *batch,
                               const struct vk_sample_locations_state *sl);

void genX(cmd_buffer_so_memcpy)(struct anv_cmd_buffer *cmd_buffer,
                                struct anv_address dst, struct anv_address src,
                                uint32_t size);

void genX(cmd_buffer_dispatch_kernel)(struct anv_cmd_buffer *cmd_buffer,
                                      struct anv_kernel *kernel,
                                      const uint32_t *global_size, /* NULL for indirect */
                                      uint32_t arg_count,
                                      const struct anv_kernel_arg *args);

void genX(blorp_exec)(struct blorp_batch *batch,
                      const struct blorp_params *params);

void genX(cmd_emit_timestamp)(struct anv_batch *batch,
                              struct anv_device *device,
                              struct anv_address addr,
                              enum anv_timestamp_capture_type);

void genX(batch_emit_dummy_post_sync_op)(struct anv_batch *batch,
                                         struct anv_device *device,
                                         uint32_t primitive_topology,
                                         uint32_t vertex_count);

void
genX(rasterization_mode)(VkPolygonMode raster_mode,
                         VkLineRasterizationModeEXT line_mode,
                         float line_width,
                         uint32_t *api_mode,
                         bool *msaa_rasterization_enable);

VkPolygonMode
genX(raster_polygon_mode)(struct anv_graphics_pipeline *pipeline,
                          VkPolygonMode polygon_mode,
                          VkPrimitiveTopology primitive_topology);

void
genX(graphics_pipeline_emit)(struct anv_graphics_pipeline *pipeline,
                             const struct vk_graphics_pipeline_state *state);

void
genX(compute_pipeline_emit)(struct anv_compute_pipeline *pipeline);

void
genX(ray_tracing_pipeline_emit)(struct anv_ray_tracing_pipeline *pipeline);

#define anv_shader_bin_get_bsr(bin, local_arg_offset) ({             \
   assert((local_arg_offset) % 8 == 0);                              \
   const struct brw_bs_prog_data *prog_data =                        \
      brw_bs_prog_data_const(bin->prog_data);                        \
   assert(prog_data->simd_size == 8 || prog_data->simd_size == 16);  \
                                                                     \
   (struct GENX(BINDLESS_SHADER_RECORD)) {                           \
      .OffsetToLocalArguments = (local_arg_offset) / 8,              \
      .BindlessShaderDispatchMode =                                  \
         prog_data->simd_size == 16 ? RT_SIMD16 : RT_SIMD8,          \
      .KernelStartPointer = bin->kernel.offset,                      \
   };                                                                \
})

void
genX(batch_set_preemption)(struct anv_batch *batch, bool value);

void
genX(cmd_buffer_set_preemption)(struct anv_cmd_buffer *cmd_buffer, bool value);
