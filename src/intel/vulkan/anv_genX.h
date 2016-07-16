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
 * Gen-specific function declarations.  This header must *not* be included
 * directly.  Instead, it is included multiple times by gen8_private.h.
 * 
 * In this header file, the usual genx() macro is available.
 */

VkResult genX(init_device_state)(struct anv_device *device);

void genX(cmd_buffer_emit_state_base_address)(struct anv_cmd_buffer *cmd_buffer);

struct anv_state
genX(cmd_buffer_alloc_null_surface_state)(struct anv_cmd_buffer *cmd_buffer,
                                          struct anv_framebuffer *fb);

void genX(cmd_buffer_set_subpass)(struct anv_cmd_buffer *cmd_buffer,
                                  struct anv_subpass *subpass);

void genX(cmd_buffer_apply_pipe_flushes)(struct anv_cmd_buffer *cmd_buffer);

void genX(flush_pipeline_select_3d)(struct anv_cmd_buffer *cmd_buffer);
void genX(flush_pipeline_select_gpgpu)(struct anv_cmd_buffer *cmd_buffer);

void genX(setup_pipeline_l3_config)(struct anv_pipeline *pipeline);

void genX(cmd_buffer_config_l3)(struct anv_cmd_buffer *cmd_buffer,
                                const struct anv_pipeline *pipeline);

void genX(cmd_buffer_flush_state)(struct anv_cmd_buffer *cmd_buffer);
void genX(cmd_buffer_flush_dynamic_state)(struct anv_cmd_buffer *cmd_buffer);

void genX(cmd_buffer_flush_compute_state)(struct anv_cmd_buffer *cmd_buffer);

VkResult
genX(graphics_pipeline_create)(VkDevice _device,
                               struct anv_pipeline_cache *cache,
                               const VkGraphicsPipelineCreateInfo *pCreateInfo,
                               const struct anv_graphics_pipeline_create_info *extra,
                               const VkAllocationCallbacks *alloc,
                               VkPipeline *pPipeline);

VkResult
genX(compute_pipeline_create)(VkDevice _device,
                              struct anv_pipeline_cache *cache,
                              const VkComputePipelineCreateInfo *pCreateInfo,
                              const VkAllocationCallbacks *alloc,
                              VkPipeline *pPipeline);
