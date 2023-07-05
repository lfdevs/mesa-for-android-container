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
#include "anv_generated_indirect_draws.h"

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
#if GFX_VER >= 11
   anv_batch_emit(batch, GENX(3DSTATE_VF_SGVS_2), sgvs);
#endif
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

   enum intel_urb_deref_block_size deref_block_size;
   genX(emit_urb_setup)(device, batch, device->generated_draw_l3_config,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        entry_size, &deref_block_size);

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

#if GFX_VERx10 >= 125
   if (device->vk.enabled_extensions.NV_mesh_shader ||
       device->vk.enabled_extensions.EXT_mesh_shader) {
      anv_batch_emit(batch, GENX(3DSTATE_MESH_CONTROL), mesh);
      anv_batch_emit(batch, GENX(3DSTATE_TASK_CONTROL), task);
   }
#endif

   anv_batch_emit(batch, GENX(3DSTATE_STREAMOUT), so);

   anv_batch_emit(batch, GENX(3DSTATE_GS), gs);

   anv_batch_emit(batch, GENX(3DSTATE_CLIP), clip) {
      clip.PerspectiveDivideDisable = true;
   }

   anv_batch_emit(batch, GENX(3DSTATE_SF), sf) {
#if GFX_VER >= 12
      sf.DerefBlockSize = deref_block_size;
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

   anv_batch_emit(batch, GENX(3DSTATE_WM), wm);

   anv_batch_emit(batch, GENX(3DSTATE_PS), ps) {
      intel_set_ps_dispatch_state(&ps, device->info, prog_data,
                                  1 /* rasterization_samples */,
                                  0 /* msaa_flags */);

      ps.VectorMaskEnable       = prog_data->uses_vmask;

      ps.BindingTableEntryCount = GFX_VER == 9 ? 1 : 0;
      ps.PushConstantEnable     = prog_data->base.nr_params > 0 ||
                                  prog_data->base.ubo_ranges[0].length;

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
    *
    * Note that Wa_16011448509 isn't needed here as all address bits are zero.
    */
   anv_batch_emit(batch, GENX(3DSTATE_CONSTANT_ALL), c) {
      /* Update empty push constants for all stages (bitmask = 11111b) */
      c.ShaderUpdateEnable = 0x1f;
      c.MOCS = anv_mocs(cmd_buffer->device, NULL, 0);
   }
#endif

#if GFX_VER == 9
   /* Allocate a binding table for Gfx9 for 2 reason :
    *
    *   1. we need a to emit a 3DSTATE_BINDING_TABLE_POINTERS_PS to make the
    *      HW apply the preceeding 3DSTATE_CONSTANT_PS
    *
    *   2. Emitting an empty 3DSTATE_BINDING_TABLE_POINTERS_PS would cause RT
    *      writes (even though they're empty) to disturb later writes
    *      (probably due to RT cache)
    *
    * Our binding table only has one entry to the null surface.
    */
   uint32_t bt_offset;
   cmd_buffer->generation_bt_state =
      anv_cmd_buffer_alloc_binding_table(cmd_buffer, 1, &bt_offset);
   if (cmd_buffer->generation_bt_state.map == NULL) {
      VkResult result = anv_cmd_buffer_new_binding_table_block(cmd_buffer);
      if (result != VK_SUCCESS)
         return;

      /* Re-emit state base addresses so we get the new surface state base
       * address before we start emitting binding tables etc.
       */
      genX(cmd_buffer_emit_state_base_address)(cmd_buffer);

      cmd_buffer->generation_bt_state =
         anv_cmd_buffer_alloc_binding_table(cmd_buffer, 1, &bt_offset);
      assert(cmd_buffer->generation_bt_state.map != NULL);
   }

   uint32_t *bt_map = cmd_buffer->generation_bt_state.map;
   bt_map[0] = anv_bindless_state_for_binding_table(
      cmd_buffer->device->null_surface_state).offset + bt_offset;

   cmd_buffer->state.descriptors_dirty |= VK_SHADER_STAGE_FRAGMENT_BIT;
#endif

   cmd_buffer->state.gfx.vb_dirty = BITFIELD_BIT(0);
   cmd_buffer->state.gfx.dirty |= ~(ANV_CMD_DIRTY_INDEX_BUFFER |
                                    ANV_CMD_DIRTY_XFB_ENABLE);
   cmd_buffer->state.push_constants_dirty |= VK_SHADER_STAGE_FRAGMENT_BIT;
   cmd_buffer->state.gfx.push_constant_stages = VK_SHADER_STAGE_FRAGMENT_BIT;
   vk_dynamic_graphics_state_dirty_all(&cmd_buffer->vk.dynamic_graphics_state);
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

static void
genX(cmd_buffer_emit_generated_push_data)(struct anv_cmd_buffer *cmd_buffer,
                                          struct anv_state push_data_state)
{
   struct anv_batch *batch = &cmd_buffer->generation_batch;
   struct anv_address push_data_addr = anv_state_pool_state_address(
      &cmd_buffer->device->dynamic_state_pool, push_data_state);

   /* Don't use 3DSTATE_CONSTANT_ALL on Gfx12.0 due to Wa_16011448509 */
#if GFX_VERx10 > 120
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
   /* The Skylake PRM contains the following restriction:
    *
    *    "The driver must ensure The following case does not occur
    *     without a flush to the 3D engine: 3DSTATE_CONSTANT_* with
    *     buffer 3 read length equal to zero committed followed by a
    *     3DSTATE_CONSTANT_* with buffer 0 read length not equal to
    *     zero committed."
    *
    * To avoid this, we program the highest slot.
    */
   anv_batch_emit(batch, GENX(3DSTATE_CONSTANT_PS), c) {
      c.MOCS = anv_mocs(cmd_buffer->device, NULL, 0);
      c.ConstantBody.ReadLength[3] = DIV_ROUND_UP(push_data_state.alloc_size, 32);
      c.ConstantBody.Buffer[3] = push_data_addr;
   }
#endif
}

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
   struct anv_batch *batch = &cmd_buffer->generation_batch;

   genX(cmd_buffer_emit_generate_draws_vertex)(cmd_buffer, item_count);

   struct anv_state push_data_state =
      anv_cmd_buffer_alloc_dynamic_state(cmd_buffer,
                                         sizeof(struct anv_generated_indirect_params),
                                         ANV_UBO_ALIGNMENT);

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
                                   (anv_mocs(cmd_buffer->device, indirect_data_addr.bo,
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
      mi_builder_init(&b, cmd_buffer->device->info, batch);
      mi_memcpy(&b,
                anv_address_add((struct anv_address) {
                      .bo = cmd_buffer->device->dynamic_state_pool.block_pool.bo,
                      .offset = push_data_state.offset,
                   },
                   offsetof(struct anv_generated_indirect_params, draw.draw_count)),
                count_addr, 4);

      /* Make sure the memcpy landed for the generating draw call to pick up
       * the value.
       */
      anv_batch_emit(batch, GENX(PIPE_CONTROL), pc) {
         pc.CommandStreamerStallEnable = true;
      }
   }

   /* Only emit the data after the memcpy above. */
   genX(cmd_buffer_emit_generated_push_data)(cmd_buffer, push_data_state);

#if GFX_VER == 9
   /* Why are the push constants not flushed without a binding table
    * update??
    */
   anv_batch_emit(batch, GENX(3DSTATE_BINDING_TABLE_POINTERS_PS), btp) {
      btp.PointertoPSBindingTable = cmd_buffer->generation_bt_state.offset;
   }
#endif

   anv_batch_emit(batch, GENX(3DPRIMITIVE), prim) {
      prim.VertexAccessType         = SEQUENTIAL;
      prim.PrimitiveTopologyType    = _3DPRIM_RECTLIST;
      prim.VertexCountPerInstance   = 3;
      prim.InstanceCount            = 1;
   }

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

   genX(cmd_buffer_emit_generate_draws_pipeline)(cmd_buffer);

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
      genX(cmd_buffer_set_binding_for_gfx8_vb_flush)(cmd_buffer, 0,
                                                     (struct anv_address) {
                                                        .offset = DYNAMIC_STATE_POOL_MIN_ADDRESS,
                                                     },
                                                     DYNAMIC_STATE_POOL_SIZE);
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
                                 NULL /* query_bits */);

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
