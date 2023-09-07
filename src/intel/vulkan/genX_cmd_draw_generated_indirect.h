/*
 * Copyright © 2022 Intel Corporation
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

#ifndef GENX_CMD_GENERATED_INDIRECT_DRAW_H
#define GENX_CMD_GENERATED_INDIRECT_DRAW_H

#include <assert.h>
#include <stdbool.h>

#include "util/macros.h"

#include "common/intel_genX_state.h"

#include "anv_private.h"
#include "anv_internal_kernels.h"
#include "genX_simple_shader.h"

/* This is a maximum number of items a fragment shader can generate due to the
 * viewport size.
 */
#define MAX_GENERATED_DRAW_COUNT (8192 * 8192)

static struct anv_generated_indirect_params *
genX(cmd_buffer_emit_generate_draws)(struct anv_cmd_buffer *cmd_buffer,
                                     struct anv_address generated_cmds_addr,
                                     uint32_t generated_cmd_stride,
                                     struct anv_address indirect_data_addr,
                                     uint32_t indirect_data_stride,
                                     struct anv_address draw_id_addr,
                                     uint32_t item_base,
                                     uint32_t item_count,
                                     struct anv_address count_addr,
                                     uint32_t max_count,
                                     bool indexed)
{
   struct anv_device *device = cmd_buffer->device;
   struct anv_batch *batch = &cmd_buffer->generation_batch;

   struct anv_state push_data_state =
      genX(simple_shader_alloc_push)(&cmd_buffer->generation_shader_state,
                                     sizeof(struct anv_generated_indirect_params));

   struct anv_graphics_pipeline *pipeline = cmd_buffer->state.gfx.pipeline;
   const struct brw_vs_prog_data *vs_prog_data = get_vs_prog_data(pipeline);

   struct anv_generated_indirect_params *push_data = push_data_state.map;
   *push_data = (struct anv_generated_indirect_params) {
      .draw                      = {
         .draw_id_addr           = anv_address_physical(draw_id_addr),
         .indirect_data_addr     = anv_address_physical(indirect_data_addr),
         .indirect_data_stride   = indirect_data_stride,
         .flags                  = (indexed ? ANV_GENERATED_FLAG_INDEXED : 0) |
                                   (cmd_buffer->state.conditional_render_enabled ?
                                    ANV_GENERATED_FLAG_PREDICATED : 0) |
                                   ((vs_prog_data->uses_firstvertex ||
                                     vs_prog_data->uses_baseinstance) ?
                                    ANV_GENERATED_FLAG_BASE : 0) |
                                   (vs_prog_data->uses_drawid ? ANV_GENERATED_FLAG_DRAWID : 0) |
                                   (anv_mocs(device, indirect_data_addr.bo,
                                             ISL_SURF_USAGE_VERTEX_BUFFER_BIT) << 8) |
                                   ((generated_cmd_stride / 4) << 16),
         .draw_base              = item_base,
         /* If count_addr is not NULL, we'll edit it through a the command
          * streamer.
          */
         .draw_count             = anv_address_is_null(count_addr) ? max_count : 0,
         .max_draw_count         = max_count,
         .instance_multiplier    = pipeline->instance_multiplier,
      },
      .indirect_data_addr        = anv_address_physical(indirect_data_addr),
      .generated_cmds_addr       = anv_address_physical(generated_cmds_addr),
      .draw_ids_addr             = anv_address_physical(draw_id_addr),
   };

   if (!anv_address_is_null(count_addr)) {
      /* Copy the draw count into the push constants so that the generation
       * gets the value straight away and doesn't even need to access memory.
       */
      struct mi_builder b;
      mi_builder_init(&b, device->info, batch);
      mi_memcpy(&b,
                anv_address_add(
                   genX(simple_shader_push_state_address)(
                      &cmd_buffer->generation_shader_state,
                      push_data_state),
                   offsetof(struct anv_generated_indirect_params, draw.draw_count)),
                count_addr, 4);

      /* Make sure the memcpy landed for the generating draw call to pick up
       * the value.
       */
      genX(batch_emit_pipe_control)(batch, cmd_buffer->device->info,
                                    ANV_PIPE_CS_STALL_BIT);
   }

   genX(emit_simple_shader_dispatch)(&cmd_buffer->generation_shader_state,
                                     item_count, push_data_state);

   return push_data;
}

static void
genX(cmd_buffer_emit_indirect_generated_draws_init)(struct anv_cmd_buffer *cmd_buffer)
{
#if GFX_VER >= 12
   anv_batch_emit(&cmd_buffer->batch, GENX(MI_ARB_CHECK), arb) {
      arb.PreParserDisableMask = true;
      arb.PreParserDisable = true;
   }
#endif

   anv_batch_emit_ensure_space(&cmd_buffer->generation_batch, 4);

   trace_intel_begin_generate_draws(&cmd_buffer->trace);

   anv_batch_emit(&cmd_buffer->batch, GENX(MI_BATCH_BUFFER_START), bbs) {
      bbs.AddressSpaceIndicator = ASI_PPGTT;
      bbs.BatchBufferStartAddress =
         anv_batch_current_address(&cmd_buffer->generation_batch);
   }

   cmd_buffer->generation_return_addr = anv_batch_current_address(&cmd_buffer->batch);

   trace_intel_end_generate_draws(&cmd_buffer->trace);

   struct anv_device *device = cmd_buffer->device;
   struct anv_simple_shader *state = &cmd_buffer->generation_shader_state;
   *state = (struct anv_simple_shader) {
      .cmd_buffer = cmd_buffer,
      .batch      = &cmd_buffer->generation_batch,
      .kernel     = device->internal_kernels[ANV_INTERNAL_KERNEL_GENERATED_DRAWS],
      .l3_config  = device->internal_kernels_l3_config,
   };

   genX(emit_simple_shader_init)(state);
}

static struct anv_address
genX(cmd_buffer_get_draw_id_addr)(struct anv_cmd_buffer *cmd_buffer,
                                  uint32_t draw_id_count)
{
#if GFX_VER >= 11
   return ANV_NULL_ADDRESS;
#else
   struct anv_graphics_pipeline *pipeline = cmd_buffer->state.gfx.pipeline;
   const struct brw_vs_prog_data *vs_prog_data = get_vs_prog_data(pipeline);
   if (!vs_prog_data->uses_drawid)
      return ANV_NULL_ADDRESS;

   struct anv_state draw_id_state =
      anv_cmd_buffer_alloc_dynamic_state(cmd_buffer, 4 * draw_id_count, 4);
   return anv_state_pool_state_address(&cmd_buffer->device->dynamic_state_pool,
                                       draw_id_state);
#endif
}

static uint32_t
genX(cmd_buffer_get_generated_draw_stride)(struct anv_cmd_buffer *cmd_buffer)
{
   /* With the extended parameters in 3DPRIMITIVE on Gfx11+ we can emit
    * everything. Prior to this, we need to emit a couple of
    * VERTEX_BUFFER_STATE.
    */
#if GFX_VER >= 11
   return 4 * GENX(3DPRIMITIVE_EXTENDED_length);
#else
   struct anv_graphics_pipeline *pipeline = cmd_buffer->state.gfx.pipeline;
   const struct brw_vs_prog_data *vs_prog_data = get_vs_prog_data(pipeline);

   uint32_t len = 0;

   if (vs_prog_data->uses_firstvertex ||
       vs_prog_data->uses_baseinstance ||
       vs_prog_data->uses_drawid) {
      len += 4; /* 3DSTATE_VERTEX_BUFFERS */

      if (vs_prog_data->uses_firstvertex ||
          vs_prog_data->uses_baseinstance)
         len += 4 * GENX(VERTEX_BUFFER_STATE_length);

      if (vs_prog_data->uses_drawid)
         len += 4 * GENX(VERTEX_BUFFER_STATE_length);
   }

   return len + 4 * GENX(3DPRIMITIVE_length);
#endif
}

static void
genX(cmd_buffer_rewrite_forward_end_addr)(struct anv_cmd_buffer *cmd_buffer,
                                          struct anv_generated_indirect_params *params)
{
   /* We don't know the end_addr until we have emitted all the generation
    * draws. Go and edit the address of all the push parameters.
    */
   uint64_t end_addr =
      anv_address_physical(anv_batch_current_address(&cmd_buffer->batch));
   while (params != NULL) {
      params->draw.end_addr = end_addr;
      params = params->prev;
   }
}

static void
genX(cmd_buffer_emit_indirect_generated_draws)(struct anv_cmd_buffer *cmd_buffer,
                                               struct anv_address indirect_data_addr,
                                               uint32_t indirect_data_stride,
                                               struct anv_address count_addr,
                                               uint32_t max_draw_count,
                                               bool indexed)
{
   const bool start_generation_batch =
      anv_address_is_null(cmd_buffer->generation_return_addr);

   genX(flush_pipeline_select_3d)(cmd_buffer);

   struct anv_address draw_id_addr =
      genX(cmd_buffer_get_draw_id_addr)(cmd_buffer, max_draw_count);

#if GFX_VER == 9
   /* Mark the VB-0 as using the entire dynamic state pool area, but only for
    * the draw call starting the generation batch. All the following ones will
    * use the same area.
    */
   if (start_generation_batch) {
      struct anv_device *device = cmd_buffer->device;
      genX(cmd_buffer_set_binding_for_gfx8_vb_flush)(
         cmd_buffer, 0,
         (struct anv_address) {
            .offset = device->physical->va.dynamic_state_pool.addr,
         },
         device->physical->va.dynamic_state_pool.size);
   }

   struct anv_graphics_pipeline *pipeline = cmd_buffer->state.gfx.pipeline;
   const struct brw_vs_prog_data *vs_prog_data = get_vs_prog_data(pipeline);

   if (vs_prog_data->uses_baseinstance ||
       vs_prog_data->uses_firstvertex) {
      /* We're using the indirect buffer directly to source base instance &
       * first vertex values. Mark the entire area as used.
       */
      genX(cmd_buffer_set_binding_for_gfx8_vb_flush)(cmd_buffer, ANV_SVGS_VB_INDEX,
                                                     indirect_data_addr,
                                                     indirect_data_stride * max_draw_count);
   }

   if (vs_prog_data->uses_drawid) {
      /* Mark the whole draw id buffer as used. */
      genX(cmd_buffer_set_binding_for_gfx8_vb_flush)(cmd_buffer, ANV_SVGS_VB_INDEX,
                                                     draw_id_addr,
                                                     sizeof(uint32_t) * max_draw_count);
   }
#endif

   /* Apply the pipeline flush here so the indirect data is available for the
    * generation shader.
    */
   genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);

   if (start_generation_batch)
      genX(cmd_buffer_emit_indirect_generated_draws_init)(cmd_buffer);

   /* In order to have the vertex fetch gather the data we need to have a non
    * 0 stride. It's possible to have a 0 stride given by the application when
    * draw_count is 1, but we need a correct value for the
    * VERTEX_BUFFER_STATE::BufferPitch, so ensure the caller set this
    * correctly :
    *
    * Vulkan spec, vkCmdDrawIndirect:
    *
    *   "If drawCount is less than or equal to one, stride is ignored."
    */
   assert(indirect_data_stride > 0);

   if (cmd_buffer->state.conditional_render_enabled)
      genX(cmd_emit_conditional_render_predicate)(cmd_buffer);

   /* Emit the 3D state in the main batch. */
   genX(cmd_buffer_flush_gfx_state)(cmd_buffer);

   const uint32_t draw_cmd_stride =
      genX(cmd_buffer_get_generated_draw_stride)(cmd_buffer);

   struct anv_generated_indirect_params *last_params = NULL;
   uint32_t item_base = 0;
   while (item_base < max_draw_count) {
      const uint32_t item_count = MIN2(max_draw_count - item_base,
                                       MAX_GENERATED_DRAW_COUNT);
      const uint32_t draw_cmd_size = item_count * draw_cmd_stride;

      /* Ensure we have enough contiguous space for all the draws so that the
       * compute shader can edit all the 3DPRIMITIVEs from a single base
       * address.
       *
       * TODO: we might have to split that if the amount of space is to large (at
       *       1Mb?).
       */
      VkResult result = anv_batch_emit_ensure_space(&cmd_buffer->batch,
                                                    draw_cmd_size);
      if (result != VK_SUCCESS)
         return;

      struct anv_generated_indirect_params *params =
         genX(cmd_buffer_emit_generate_draws)(
            cmd_buffer,
            anv_batch_current_address(&cmd_buffer->batch),
            draw_cmd_stride,
            anv_address_add(indirect_data_addr,
                            item_base * indirect_data_stride),
            indirect_data_stride,
            anv_address_add(draw_id_addr, 4 * item_base),
            item_base,
            item_count,
            count_addr,
            max_draw_count,
            indexed);

      anv_batch_advance(&cmd_buffer->batch, draw_cmd_size);

      item_base += item_count;

      params->prev = last_params;
      last_params = params;
   }

   genX(cmd_buffer_rewrite_forward_end_addr)(cmd_buffer, last_params);

#if GFX_VER == 9
   update_dirty_vbs_for_gfx8_vb_flush(cmd_buffer, indexed ? RANDOM : SEQUENTIAL);
#endif
}

static void
genX(cmd_buffer_flush_generated_draws)(struct anv_cmd_buffer *cmd_buffer)
{
   /* No return address setup means we don't have to do anything */
   if (anv_address_is_null(cmd_buffer->generation_return_addr))
      return;

   struct anv_batch *batch = &cmd_buffer->generation_batch;

   /* Wait for all the generation vertex shader to generate the commands. */
   genX(emit_apply_pipe_flushes)(batch,
                                 cmd_buffer->device,
                                 _3D,
#if GFX_VER == 9
                                 ANV_PIPE_VF_CACHE_INVALIDATE_BIT |
#endif
                                 ANV_PIPE_DATA_CACHE_FLUSH_BIT |
                                 ANV_PIPE_CS_STALL_BIT,
                                 NULL /* emitted_bits */);

#if GFX_VER >= 12
   anv_batch_emit(batch, GENX(MI_ARB_CHECK), arb) {
      arb.PreParserDisableMask = true;
      arb.PreParserDisable = false;
   }
#else
   /* Prior to Gfx12 we cannot disable the CS prefetch but it doesn't matter
    * as the prefetch shouldn't follow the MI_BATCH_BUFFER_START.
    */
#endif

   /* Return to the main batch. */
   anv_batch_emit(batch, GENX(MI_BATCH_BUFFER_START), bbs) {
      bbs.AddressSpaceIndicator = ASI_PPGTT;
      bbs.BatchBufferStartAddress = cmd_buffer->generation_return_addr;
   }

   cmd_buffer->generation_return_addr = ANV_NULL_ADDRESS;
}

#endif /* GENX_CMD_GENERATED_INDIRECT_DRAW_H */
