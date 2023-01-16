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

#include "anv_private.h"
#include "anv_generated_indirect_draws.h"

#if GFX_VER < 11
#error "Generated draws optimization not supported prior to Gfx11"
#endif

/* This is a maximum number of items a fragment shader can generate due to the
 * viewport size.
 */
#define MAX_GENERATED_DRAW_COUNT (8192 * 8192)

static void
genX(cmd_buffer_emit_generate_draws_pipeline)(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_batch *batch = &cmd_buffer->generation_batch;
   struct anv_device *device = cmd_buffer->device;
   const struct anv_shader_bin *draw_kernel = device->generated_draw_kernel;
   const struct brw_wm_prog_data *prog_data =
      brw_wm_prog_data_const(draw_kernel->prog_data);

   uint32_t *dw = anv_batch_emitn(batch,
                                  1 + 2 * GENX(VERTEX_ELEMENT_STATE_length),
                                  GENX(3DSTATE_VERTEX_ELEMENTS));
   /* You might think there is some shady stuff going here and you would be
    * right. We're setting up 2 VERTEX_ELEMENT_STATE yet we're only providing
    * 1 (positions) VERTEX_BUFFER_STATE later.
    *
    * Find more about how to set up a 3D pipeline with a fragment shader but
    * without a vertex shader in blorp_emit_vertex_elements() in
    * blorp_genX_exec.h.
    */
   GENX(VERTEX_ELEMENT_STATE_pack)(
      batch, dw + 1, &(struct GENX(VERTEX_ELEMENT_STATE)) {
         .VertexBufferIndex = 1,
         .Valid = true,
         .SourceElementFormat = ISL_FORMAT_R32G32B32A32_FLOAT,
         .SourceElementOffset = 0,
         .Component0Control = VFCOMP_STORE_SRC,
         .Component1Control = VFCOMP_STORE_0,
         .Component2Control = VFCOMP_STORE_0,
         .Component3Control = VFCOMP_STORE_0,
      });
   GENX(VERTEX_ELEMENT_STATE_pack)(
      batch, dw + 3, &(struct GENX(VERTEX_ELEMENT_STATE)) {
         .VertexBufferIndex   = 0,
         .Valid               = true,
         .SourceElementFormat = ISL_FORMAT_R32G32B32_FLOAT,
         .SourceElementOffset = 0,
         .Component0Control   = VFCOMP_STORE_SRC,
         .Component1Control   = VFCOMP_STORE_SRC,
         .Component2Control   = VFCOMP_STORE_SRC,
         .Component3Control   = VFCOMP_STORE_1_FP,
      });

   anv_batch_emit(batch, GENX(3DSTATE_VF_STATISTICS), vf);
   anv_batch_emit(batch, GENX(3DSTATE_VF_SGVS), sgvs) {
      sgvs.InstanceIDEnable = true;
      sgvs.InstanceIDComponentNumber = COMP_1;
      sgvs.InstanceIDElementOffset = 0;
   }
   anv_batch_emit(batch, GENX(3DSTATE_VF_SGVS_2), sgvs);
   anv_batch_emit(batch, GENX(3DSTATE_VF_INSTANCING), vfi) {
      vfi.InstancingEnable   = false;
      vfi.VertexElementIndex = 0;
   }
   anv_batch_emit(batch, GENX(3DSTATE_VF_INSTANCING), vfi) {
      vfi.InstancingEnable   = false;
      vfi.VertexElementIndex = 1;
   }

   anv_batch_emit(batch, GENX(3DSTATE_VF_TOPOLOGY), topo) {
      topo.PrimitiveTopologyType = _3DPRIM_RECTLIST;
   }

   /* Emit URB setup.  We tell it that the VS is active because we want it to
    * allocate space for the VS.  Even though one isn't run, we need VUEs to
    * store the data that VF is going to pass to SOL.
    */
   const unsigned entry_size[4] = { DIV_ROUND_UP(32, 64), 1, 1, 1 };

   genX(emit_l3_config)(batch, device, device->generated_draw_l3_config);

   cmd_buffer->state.current_l3_config = device->generated_draw_l3_config;

   genX(emit_urb_setup)(device, batch, device->generated_draw_l3_config,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        entry_size, NULL);

   anv_batch_emit(batch, GENX(3DSTATE_PS_BLEND), ps_blend) {
      ps_blend.HasWriteableRT = true;
   }

   anv_batch_emit(batch, GENX(3DSTATE_WM_DEPTH_STENCIL), wm);

#if GFX_VER >= 12
   anv_batch_emit(batch, GENX(3DSTATE_DEPTH_BOUNDS), db) {
      db.DepthBoundsTestEnable = false;
      db.DepthBoundsTestMinValue = 0.0;
      db.DepthBoundsTestMaxValue = 1.0;
   }
#endif

   anv_batch_emit(batch, GENX(3DSTATE_MULTISAMPLE), ms);
   anv_batch_emit(batch, GENX(3DSTATE_SAMPLE_MASK), sm) {
      sm.SampleMask = 0x1;
   }

   anv_batch_emit(batch, GENX(3DSTATE_VS), vs);
   anv_batch_emit(batch, GENX(3DSTATE_HS), hs);
   anv_batch_emit(batch, GENX(3DSTATE_TE), te);
   anv_batch_emit(batch, GENX(3DSTATE_DS), DS);

   anv_batch_emit(batch, GENX(3DSTATE_STREAMOUT), so);

   anv_batch_emit(batch, GENX(3DSTATE_GS), gs);

   anv_batch_emit(batch, GENX(3DSTATE_CLIP), clip) {
      clip.PerspectiveDivideDisable = true;
   }

   anv_batch_emit(batch, GENX(3DSTATE_SF), sf) {
#if GFX_VER >= 12
      sf.DerefBlockSize = INTEL_URB_DEREF_BLOCK_SIZE_32; // TODO
#endif
   }

   anv_batch_emit(batch, GENX(3DSTATE_RASTER), raster) {
      raster.CullMode = CULLMODE_NONE;
   }

   anv_batch_emit(batch, GENX(3DSTATE_SBE), sbe) {
      sbe.VertexURBEntryReadOffset = 1;
      sbe.NumberofSFOutputAttributes = prog_data->num_varying_inputs;
      sbe.VertexURBEntryReadLength = MAX2((prog_data->num_varying_inputs + 1) / 2, 1);
      sbe.ConstantInterpolationEnable = prog_data->flat_inputs;
      sbe.ForceVertexURBEntryReadLength = true;
      sbe.ForceVertexURBEntryReadOffset = true;
      for (unsigned i = 0; i < 32; i++)
         sbe.AttributeActiveComponentFormat[i] = ACF_XYZW;
   }

   anv_batch_emit(batch, GENX(3DSTATE_WM), wm) {
      //wm.ForceThreadDispatchEnable = ForceON;
   }

   anv_batch_emit(batch, GENX(3DSTATE_PS_EXTRA), psx) {
      psx.PixelShaderValid = true;
      psx.AttributeEnable = prog_data->num_varying_inputs > 0;
      psx.PixelShaderIsPerSample = prog_data->persample_dispatch;
      psx.PixelShaderComputedDepthMode = prog_data->computed_depth_mode;
      psx.PixelShaderComputesStencil = prog_data->computed_stencil;
   }

   anv_batch_emit(batch, GENX(3DSTATE_VIEWPORT_STATE_POINTERS_CC), cc) {
      struct anv_state cc_state =
         anv_cmd_buffer_alloc_dynamic_state(cmd_buffer, 4 * GENX(CC_VIEWPORT_length), 32);
      struct GENX(CC_VIEWPORT) cc_viewport = {
         .MinimumDepth = 0.0f,
         .MaximumDepth = 1.0f,
      };
      GENX(CC_VIEWPORT_pack)(NULL, cc_state.map, &cc_viewport);
      cc.CCViewportPointer = cc_state.offset;
   }

#if GFX_VER >= 12
   /* Disable Primitive Replication. */
   anv_batch_emit(batch, GENX(3DSTATE_PRIMITIVE_REPLICATION), pr);
#endif

   anv_batch_emit(batch, GENX(3DSTATE_PUSH_CONSTANT_ALLOC_VS), alloc);
   anv_batch_emit(batch, GENX(3DSTATE_PUSH_CONSTANT_ALLOC_HS), alloc);
   anv_batch_emit(batch, GENX(3DSTATE_PUSH_CONSTANT_ALLOC_DS), alloc);
   anv_batch_emit(batch, GENX(3DSTATE_PUSH_CONSTANT_ALLOC_GS), alloc);
   anv_batch_emit(batch, GENX(3DSTATE_PUSH_CONSTANT_ALLOC_PS), alloc) {
      alloc.ConstantBufferOffset = 0;
      alloc.ConstantBufferSize   = cmd_buffer->device->info->max_constant_urb_size_kb;
   }

#if GFX_VERx10 == 125
   /* DG2: Wa_22011440098
    * MTL: Wa_18022330953
    *
    * In 3D mode, after programming push constant alloc command immediately
    * program push constant command(ZERO length) without any commit between
    * them.
    */
   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_CONSTANT_ALL), c) {
      /* Update empty push constants for all stages (bitmask = 11111b) */
      c.ShaderUpdateEnable = 0x1f;
      c.MOCS = anv_mocs(cmd_buffer->device, NULL, 0);
   }
#endif

   cmd_buffer->state.gfx.vb_dirty = BITFIELD_BIT(0) | BITFIELD_BIT(1);
   cmd_buffer->state.gfx.dirty |= ~(ANV_CMD_DIRTY_INDEX_BUFFER |
                                    ANV_CMD_DIRTY_XFB_ENABLE);
   cmd_buffer->state.push_constants_dirty |= VK_SHADER_STAGE_ALL_GRAPHICS;
   cmd_buffer->state.gfx.push_constant_stages = VK_SHADER_STAGE_FRAGMENT_BIT;
   vk_dynamic_graphics_state_dirty_all(&cmd_buffer->vk.dynamic_graphics_state);

   anv_add_pending_pipe_bits(cmd_buffer,
                             ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT |
                             ANV_PIPE_STALL_AT_SCOREBOARD_BIT,
                             "after generation batch BTI change");
}

static void
genX(cmd_buffer_emit_generate_draws_vertex)(struct anv_cmd_buffer *cmd_buffer,
                                            uint32_t draw_count)
{
   struct anv_batch *batch = &cmd_buffer->generation_batch;
   struct anv_state vs_data_state =
      anv_cmd_buffer_alloc_dynamic_state(
         cmd_buffer, 9 * sizeof(uint32_t), 32);

   float x0 = 0.0f, x1 = MIN2(draw_count, 8192);
   float y0 = 0.0f, y1 = DIV_ROUND_UP(draw_count, 8192);
   float z = 0.0f;

   float *vertices = vs_data_state.map;
   vertices[0] = x1; vertices[1] = y1; vertices[2] = z; /* v0 */
   vertices[3] = x0; vertices[4] = y1; vertices[5] = z; /* v1 */
   vertices[6] = x0; vertices[7] = y0; vertices[8] = z; /* v2 */

   uint32_t *dw = anv_batch_emitn(batch,
                                  1 + GENX(VERTEX_BUFFER_STATE_length),
                                  GENX(3DSTATE_VERTEX_BUFFERS));
   GENX(VERTEX_BUFFER_STATE_pack)(batch, dw + 1,
                                  &(struct GENX(VERTEX_BUFFER_STATE)) {
         .VertexBufferIndex     = 0,
         .AddressModifyEnable   = true,
         .BufferStartingAddress = (struct anv_address) {
            .bo = cmd_buffer->device->dynamic_state_pool.block_pool.bo,
            .offset = vs_data_state.offset,
         },
         .BufferPitch           = 3 * sizeof(float),
         .BufferSize            = 9 * sizeof(float),
         .MOCS                  = anv_mocs(cmd_buffer->device, NULL, 0),
#if GFX_VER >= 12
         .L3BypassDisable       = true,
#endif
      });
}

static struct anv_state
genX(cmd_buffer_alloc_generated_push_data)(struct anv_cmd_buffer *cmd_buffer)
{
   return anv_cmd_buffer_alloc_dynamic_state(cmd_buffer,
                                             sizeof(struct anv_generate_indirect_params),
                                             ANV_UBO_ALIGNMENT);
}


static struct anv_state
genX(cmd_buffer_emit_generated_push_data)(struct anv_cmd_buffer *cmd_buffer,
                                          struct anv_state push_data_state)
{
   struct anv_batch *batch = &cmd_buffer->generation_batch;
   struct anv_address push_data_addr = anv_state_pool_state_address(
      &cmd_buffer->device->dynamic_state_pool, push_data_state);

#if GFX_VER >= 12
   const uint32_t num_dwords = GENX(3DSTATE_CONSTANT_ALL_length) +
      GENX(3DSTATE_CONSTANT_ALL_DATA_length);
   uint32_t *dw =
      anv_batch_emitn(batch, num_dwords,
                      GENX(3DSTATE_CONSTANT_ALL),
                      .ShaderUpdateEnable = BITFIELD_BIT(MESA_SHADER_FRAGMENT),
                      .PointerBufferMask = 0x1,
                      .MOCS = anv_mocs(cmd_buffer->device, NULL, 0));

   GENX(3DSTATE_CONSTANT_ALL_DATA_pack)(
      batch, dw + GENX(3DSTATE_CONSTANT_ALL_length),
      &(struct GENX(3DSTATE_CONSTANT_ALL_DATA)) {
         .PointerToConstantBuffer = push_data_addr,
         .ConstantBufferReadLength = DIV_ROUND_UP(push_data_state.alloc_size, 32),
      });
#else
   anv_batch_emit(batch, GENX(3DSTATE_CONSTANT_PS), c) {
      c.MOCS = anv_mocs(cmd_buffer->device, NULL, 0);
      c.ConstantBody.ReadLength[0] = DIV_ROUND_UP(push_data_state.alloc_size, 32);
      c.ConstantBody.Buffer[0] = push_data_addr;
   }
#endif

   return push_data_state;
}

static void
genX(cmd_buffer_emit_generate_draws)(struct anv_cmd_buffer *cmd_buffer,
                                     struct anv_address generated_cmds_addr,
                                     uint32_t generated_cmds_size,
                                     struct anv_address indirect_data_addr,
                                     uint32_t indirect_data_stride,
                                     uint32_t item_base,
                                     uint32_t item_count,
                                     bool indexed)
{
   struct anv_device *device = cmd_buffer->device;
   struct anv_batch *batch = &cmd_buffer->generation_batch;
   const struct anv_shader_bin *draw_kernel = device->generated_draw_kernel;
   const struct brw_wm_prog_data *prog_data =
      brw_wm_prog_data_const(draw_kernel->prog_data);

   anv_batch_emit(batch, GENX(3DSTATE_PS), ps) {
      ps.BindingTableEntryCount = 2;
      ps.PushConstantEnable     = prog_data->base.nr_params > 0 ||
                                  prog_data->base.ubo_ranges[0].length;

      ps._8PixelDispatchEnable = prog_data->dispatch_8;
      ps._16PixelDispatchEnable = prog_data->dispatch_16;
      ps._32PixelDispatchEnable = prog_data->dispatch_32;

      ps.DispatchGRFStartRegisterForConstantSetupData0 =
         brw_wm_prog_data_dispatch_grf_start_reg(prog_data, ps, 0);
      ps.DispatchGRFStartRegisterForConstantSetupData1 =
         brw_wm_prog_data_dispatch_grf_start_reg(prog_data, ps, 1);
      ps.DispatchGRFStartRegisterForConstantSetupData2 =
         brw_wm_prog_data_dispatch_grf_start_reg(prog_data, ps, 2);

      ps.KernelStartPointer0 = draw_kernel->kernel.offset +
         brw_wm_prog_data_prog_offset(prog_data, ps, 0);
      ps.KernelStartPointer1 = draw_kernel->kernel.offset +
         brw_wm_prog_data_prog_offset(prog_data, ps, 1);
      ps.KernelStartPointer2 = draw_kernel->kernel.offset +
         brw_wm_prog_data_prog_offset(prog_data, ps, 2);

      ps.MaximumNumberofThreadsPerPSD = device->info->max_threads_per_psd - 1;
   }

   genX(cmd_buffer_emit_generate_draws_vertex)(cmd_buffer, item_count);

   struct anv_state push_data_state =
      genX(cmd_buffer_alloc_generated_push_data)(cmd_buffer);

   struct anv_graphics_pipeline *pipeline = cmd_buffer->state.gfx.pipeline;

   struct anv_generate_indirect_params *push_data = push_data_state.map;
   *push_data = (struct anv_generate_indirect_params) {
      .draw                      = {
         .is_indexed             = indexed,
         .is_predicated          = cmd_buffer->state.conditional_render_enabled,
         .draw_base              = item_base,
         .draw_count             = item_count,
         .instance_multiplier    = pipeline->instance_multiplier,
         .indirect_data_stride   = indirect_data_stride,
      },
      .indirect_data_addr        = anv_address_physical(indirect_data_addr),
      .generated_cmds_addr       = anv_address_physical(generated_cmds_addr),
   };

   genX(cmd_buffer_emit_generated_push_data)(cmd_buffer, push_data_state);

   anv_batch_emit(batch, GENX(3DPRIMITIVE), prim) {
      prim.VertexAccessType         = SEQUENTIAL;
      prim.PrimitiveTopologyType    = _3DPRIM_RECTLIST;
      prim.VertexCountPerInstance   = 3;
      prim.InstanceCount            = 1;
   }
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

   genX(cmd_buffer_emit_generate_draws_pipeline)(cmd_buffer);
}

static void
genX(cmd_buffer_emit_indirect_generated_draws)(struct anv_cmd_buffer *cmd_buffer,
                                               struct anv_address indirect_data_addr,
                                               uint32_t indirect_data_stride,
                                               uint32_t draw_count,
                                               bool indexed)
{
   genX(flush_pipeline_select_3d)(cmd_buffer);

   /* Apply the pipeline flush here so the indirect data is available for the
    * generation shader.
    */
   genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);

   if (anv_address_is_null(cmd_buffer->generation_return_addr))
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

   const uint32_t draw_cmd_stride = 4 * GENX(3DPRIMITIVE_EXTENDED_length);

   uint32_t item_base = 0;
   while (item_base < draw_count) {
      const uint32_t item_count = MIN2(draw_count - item_base,
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

      genX(cmd_buffer_emit_generate_draws)(
         cmd_buffer,
         anv_batch_current_address(&cmd_buffer->batch),
         draw_cmd_size,
         indirect_data_addr,
         indirect_data_stride,
         item_base,
         item_count,
         indexed);

      anv_batch_advance(&cmd_buffer->batch, draw_cmd_size);

      item_base += item_count;
   }
}

static void
genX(cmd_buffer_emit_generate_draws_count)(struct anv_cmd_buffer *cmd_buffer,
                                           struct anv_address generated_cmds_addr,
                                           uint32_t generated_cmds_size,
                                           struct anv_address indirect_data_addr,
                                           uint32_t indirect_data_stride,
                                           uint32_t item_base,
                                           uint32_t item_count,
                                           struct anv_address count_addr,
                                           bool indexed)
{
   struct anv_device *device = cmd_buffer->device;
   struct anv_batch *batch = &cmd_buffer->generation_batch;
   const struct anv_shader_bin *draw_kernel =
      device->generated_draw_count_kernel;
   const struct brw_wm_prog_data *prog_data =
      brw_wm_prog_data_const(draw_kernel->prog_data);

   anv_batch_emit(batch, GENX(3DSTATE_PS), ps) {
      ps.BindingTableEntryCount = 2;
      ps.PushConstantEnable     = prog_data->base.nr_params > 0 ||
                                  prog_data->base.ubo_ranges[0].length;

      ps._8PixelDispatchEnable = prog_data->dispatch_8;
      ps._16PixelDispatchEnable = prog_data->dispatch_16;
      ps._32PixelDispatchEnable = prog_data->dispatch_32;

      ps.DispatchGRFStartRegisterForConstantSetupData0 =
         brw_wm_prog_data_dispatch_grf_start_reg(prog_data, ps, 0);
      ps.DispatchGRFStartRegisterForConstantSetupData1 =
         brw_wm_prog_data_dispatch_grf_start_reg(prog_data, ps, 1);
      ps.DispatchGRFStartRegisterForConstantSetupData2 =
         brw_wm_prog_data_dispatch_grf_start_reg(prog_data, ps, 2);

      ps.KernelStartPointer0 = draw_kernel->kernel.offset +
         brw_wm_prog_data_prog_offset(prog_data, ps, 0);
      ps.KernelStartPointer1 = draw_kernel->kernel.offset +
         brw_wm_prog_data_prog_offset(prog_data, ps, 1);
      ps.KernelStartPointer2 = draw_kernel->kernel.offset +
         brw_wm_prog_data_prog_offset(prog_data, ps, 2);

      ps.MaximumNumberofThreadsPerPSD = device->info->max_threads_per_psd - 1;
   }

   genX(cmd_buffer_emit_generate_draws_vertex)(cmd_buffer, item_count);

   struct anv_state push_data_state =
      genX(cmd_buffer_alloc_generated_push_data)(cmd_buffer);

   struct anv_graphics_pipeline *pipeline = cmd_buffer->state.gfx.pipeline;
   uint64_t end_cmd_addr =
      anv_address_physical(
         anv_address_add(generated_cmds_addr, generated_cmds_size));

   struct anv_generate_indirect_params *push_data = push_data_state.map;
   *push_data = (struct anv_generate_indirect_params) {
      .draw_count                = {
         .is_indexed             = indexed,
         .is_predicated          = cmd_buffer->state.conditional_render_enabled,
         .draw_base              = item_base,
         .item_count             = item_count,
         .draw_count             = 0, // Edit this through a the command streamer
         .instance_multiplier    = pipeline->instance_multiplier,
         .indirect_data_stride   = indirect_data_stride,
         .end_addr_ldw           = end_cmd_addr & 0xffffffff,
         .end_addr_udw           = end_cmd_addr >> 32,
      },
      .indirect_data_addr        = anv_address_physical(indirect_data_addr),
      .generated_cmds_addr       = anv_address_physical(generated_cmds_addr),
   };

   /* Copy the draw count into the push constants so that the generation gets
    * the value straight away and doesn't even need to access memory.
    */
   struct mi_builder b;
   mi_builder_init(&b, cmd_buffer->device->info, batch);
   mi_memcpy(&b,
             anv_address_add((struct anv_address) {
                   .bo = cmd_buffer->device->dynamic_state_pool.block_pool.bo,
                   .offset = push_data_state.offset,
                },
                offsetof(struct anv_generate_indirect_params, draw_count.draw_count)),
             count_addr, 4);

   /* Only emit the data after the memcpy above. */
   genX(cmd_buffer_emit_generated_push_data)(cmd_buffer, push_data_state);

   anv_batch_emit(batch, GENX(3DPRIMITIVE), prim) {
      prim.VertexAccessType         = SEQUENTIAL;
      prim.PrimitiveTopologyType    = _3DPRIM_RECTLIST;
      prim.VertexCountPerInstance   = 3;
      prim.InstanceCount            = 1;
   }
}

static void
genX(cmd_buffer_emit_indirect_generated_draws_count)(struct anv_cmd_buffer *cmd_buffer,
                                                     struct anv_address indirect_data_addr,
                                                     uint32_t indirect_data_stride,
                                                     struct anv_address count_addr,
                                                     uint32_t max_draw_count,
                                                     bool indexed)
{
   genX(flush_pipeline_select_3d)(cmd_buffer);

   /* Apply the pipeline flush here so the indirect data is available for the
    * generation shader.
    */
   genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);

   if (anv_address_is_null(cmd_buffer->generation_return_addr))
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

   const uint32_t draw_cmd_stride = 4 * GENX(3DPRIMITIVE_EXTENDED_length);

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

      genX(cmd_buffer_emit_generate_draws_count)(
         cmd_buffer,
         anv_batch_current_address(&cmd_buffer->batch),
         draw_cmd_size,
         anv_address_add(indirect_data_addr,
                         item_base * indirect_data_stride),
         indirect_data_stride,
         item_base,
         item_count,
         count_addr,
         indexed);

      anv_batch_advance(&cmd_buffer->batch, draw_cmd_size);

      item_base += item_count;
   }
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
                                 ANV_PIPE_DATA_CACHE_FLUSH_BIT |
                                 ANV_PIPE_CS_STALL_BIT);

#if GFX_VER >= 12
   anv_batch_emit(batch, GENX(MI_ARB_CHECK), arb) {
      arb.PreParserDisableMask = true;
      arb.PreParserDisable = false;
   }
#endif

#if GFX_VER < 12
   /* Prior to Gfx12 we cannot disable the CS prefetch, so we have to emit a
    * bunch of NOOPs to ensure we do not have generated commands loaded into
    * the CS cache prior to them having been generated.
    */
   const struct intel_device_info *devinfo = cmd_buffer->device->info;
   const enum intel_engine_class engine_class = cmd_buffer->queue_family->engine_class;
   for (uint32_t i = 0; i < devinfo->engine_class_prefetch[engine_class] / 4; i++)
      anv_batch_emit(batch, GENX(MI_NOOP), noop);
#endif

   /* Return to the main batch. */
   anv_batch_emit(batch, GENX(MI_BATCH_BUFFER_START), bbs) {
      bbs.AddressSpaceIndicator = ASI_PPGTT;
      bbs.BatchBufferStartAddress = cmd_buffer->generation_return_addr;
   }

   cmd_buffer->generation_return_addr = ANV_NULL_ADDRESS;
}

#endif /* GENX_CMD_GENERATED_INDIRECT_DRAW_H */
