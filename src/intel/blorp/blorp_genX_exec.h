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

#ifndef BLORP_GENX_EXEC_H
#define BLORP_GENX_EXEC_H

#include "blorp_priv.h"
#include "common/gen_device_info.h"
#include "common/gen_sample_positions.h"
#include "intel_aub.h"

/**
 * This file provides the blorp pipeline setup and execution functionality.
 * It defines the following function:
 *
 * static void
 * blorp_exec(struct blorp_context *blorp, void *batch_data,
 *            const struct blorp_params *params);
 *
 * It is the job of whoever includes this header to wrap this in something
 * to get an externally visible symbol.
 *
 * In order for the blorp_exec function to work, the driver must provide
 * implementations of the following static helper functions.
 */

static void *
blorp_emit_dwords(struct blorp_batch *batch, unsigned n);

static uint64_t
blorp_emit_reloc(struct blorp_batch *batch,
                 void *location, struct blorp_address address, uint32_t delta);

static void *
blorp_alloc_dynamic_state(struct blorp_batch *batch,
                          enum aub_state_struct_type type,
                          uint32_t size,
                          uint32_t alignment,
                          uint32_t *offset);
static void *
blorp_alloc_vertex_buffer(struct blorp_batch *batch, uint32_t size,
                          struct blorp_address *addr);

static void
blorp_alloc_binding_table(struct blorp_batch *batch, unsigned num_entries,
                          unsigned state_size, unsigned state_alignment,
                          uint32_t *bt_offset, uint32_t *surface_offsets,
                          void **surface_maps);

static void
blorp_flush_range(struct blorp_batch *batch, void *start, size_t size);

static void
blorp_surface_reloc(struct blorp_batch *batch, uint32_t ss_offset,
                    struct blorp_address address, uint32_t delta);

static void
blorp_emit_urb_config(struct blorp_batch *batch, unsigned vs_entry_size);

/***** BEGIN blorp_exec implementation ******/

#include "genxml/gen_macros.h"

static uint64_t
_blorp_combine_address(struct blorp_batch *batch, void *location,
                       struct blorp_address address, uint32_t delta)
{
   if (address.buffer == NULL) {
      return address.offset + delta;
   } else {
      return blorp_emit_reloc(batch, location, address, delta);
   }
}

#define __gen_address_type struct blorp_address
#define __gen_user_data struct blorp_batch
#define __gen_combine_address _blorp_combine_address

#include "genxml/genX_pack.h"

#define _blorp_cmd_length(cmd) cmd ## _length
#define _blorp_cmd_length_bias(cmd) cmd ## _length_bias
#define _blorp_cmd_header(cmd) cmd ## _header
#define _blorp_cmd_pack(cmd) cmd ## _pack

#define blorp_emit(batch, cmd, name)                              \
   for (struct cmd name = { _blorp_cmd_header(cmd) },             \
        *_dst = blorp_emit_dwords(batch, _blorp_cmd_length(cmd)); \
        __builtin_expect(_dst != NULL, 1);                        \
        _blorp_cmd_pack(cmd)(batch, (void *)_dst, &name),         \
        _dst = NULL)

#define blorp_emitn(batch, cmd, n) ({                    \
      uint32_t *_dw = blorp_emit_dwords(batch, n);       \
      struct cmd template = {                            \
         _blorp_cmd_header(cmd),                         \
         .DWordLength = n - _blorp_cmd_length_bias(cmd), \
      };                                                 \
      _blorp_cmd_pack(cmd)(batch, _dw, &template);       \
      _dw + 1; /* Array starts at dw[1] */               \
   })

/* 3DSTATE_URB
 * 3DSTATE_URB_VS
 * 3DSTATE_URB_HS
 * 3DSTATE_URB_DS
 * 3DSTATE_URB_GS
 *
 * Assign the entire URB to the VS. Even though the VS disabled, URB space
 * is still needed because the clipper loads the VUE's from the URB. From
 * the Sandybridge PRM, Volume 2, Part 1, Section 3DSTATE,
 * Dword 1.15:0 "VS Number of URB Entries":
 *     This field is always used (even if VS Function Enable is DISABLED).
 *
 * The warning below appears in the PRM (Section 3DSTATE_URB), but we can
 * safely ignore it because this batch contains only one draw call.
 *     Because of URB corruption caused by allocating a previous GS unit
 *     URB entry to the VS unit, software is required to send a “GS NULL
 *     Fence” (Send URB fence with VS URB size == 1 and GS URB size == 0)
 *     plus a dummy DRAW call before any case where VS will be taking over
 *     GS URB space.
 *
 * If the 3DSTATE_URB_VS is emitted, than the others must be also.
 * From the Ivybridge PRM, Volume 2 Part 1, section 1.7.1 3DSTATE_URB_VS:
 *
 *     3DSTATE_URB_HS, 3DSTATE_URB_DS, and 3DSTATE_URB_GS must also be
 *     programmed in order for the programming of this state to be
 *     valid.
 */
static void
emit_urb_config(struct blorp_batch *batch,
                const struct blorp_params *params)
{
   /* Once vertex fetcher has written full VUE entries with complete
    * header the space requirement is as follows per vertex (in bytes):
    *
    *     Header    Position    Program constants
    *   +--------+------------+-------------------+
    *   |   16   |     16     |      n x 16       |
    *   +--------+------------+-------------------+
    *
    * where 'n' stands for number of varying inputs expressed as vec4s.
    */
    const unsigned num_varyings =
       params->wm_prog_data ? params->wm_prog_data->num_varying_inputs : 0;
    const unsigned total_needed = 16 + 16 + num_varyings * 16;

   /* The URB size is expressed in units of 64 bytes (512 bits) */
   const unsigned vs_entry_size = DIV_ROUND_UP(total_needed, 64);

   blorp_emit_urb_config(batch, vs_entry_size);
}

static void
blorp_emit_vertex_data(struct blorp_batch *batch,
                       const struct blorp_params *params,
                       struct blorp_address *addr,
                       uint32_t *size)
{
   const float vertices[] = {
      /* v0 */ (float)params->x1, (float)params->y1, params->z,
      /* v1 */ (float)params->x0, (float)params->y1, params->z,
      /* v2 */ (float)params->x0, (float)params->y0, params->z,
   };

   void *data = blorp_alloc_vertex_buffer(batch, sizeof(vertices), addr);
   memcpy(data, vertices, sizeof(vertices));
   *size = sizeof(vertices);
   blorp_flush_range(batch, data, *size);
}

static void
blorp_emit_input_varying_data(struct blorp_batch *batch,
                              const struct blorp_params *params,
                              struct blorp_address *addr,
                              uint32_t *size)
{
   const unsigned vec4_size_in_bytes = 4 * sizeof(float);
   const unsigned max_num_varyings =
      DIV_ROUND_UP(sizeof(params->wm_inputs), vec4_size_in_bytes);
   const unsigned num_varyings =
      params->wm_prog_data ? params->wm_prog_data->num_varying_inputs : 0;

   *size = 16 + num_varyings * vec4_size_in_bytes;

   const uint32_t *const inputs_src = (const uint32_t *)&params->wm_inputs;
   void *data = blorp_alloc_vertex_buffer(batch, *size, addr);
   uint32_t *inputs = data;

   /* Copy in the VS inputs */
   assert(sizeof(params->vs_inputs) == 16);
   memcpy(inputs, &params->vs_inputs, sizeof(params->vs_inputs));
   inputs += 4;

   if (params->wm_prog_data) {
      /* Walk over the attribute slots, determine if the attribute is used by
       * the program and when necessary copy the values from the input storage
       * to the vertex data buffer.
       */
      for (unsigned i = 0; i < max_num_varyings; i++) {
         const gl_varying_slot attr = VARYING_SLOT_VAR0 + i;

         const int input_index = params->wm_prog_data->urb_setup[attr];
         if (input_index < 0)
            continue;

         memcpy(inputs, inputs_src + i * 4, vec4_size_in_bytes);

         inputs += 4;
      }
   }

   blorp_flush_range(batch, data, *size);
}

static void
blorp_emit_vertex_buffers(struct blorp_batch *batch,
                          const struct blorp_params *params)
{
   struct GENX(VERTEX_BUFFER_STATE) vb[2];
   memset(vb, 0, sizeof(vb));

   uint32_t size;
   blorp_emit_vertex_data(batch, params, &vb[0].BufferStartingAddress, &size);
   vb[0].VertexBufferIndex = 0;
   vb[0].BufferPitch = 3 * sizeof(float);
   vb[0].VertexBufferMOCS = batch->blorp->mocs.vb;
#if GEN_GEN >= 7
   vb[0].AddressModifyEnable = true;
#endif
#if GEN_GEN >= 8
   vb[0].BufferSize = size;
#else
   vb[0].BufferAccessType = VERTEXDATA;
   vb[0].EndAddress = vb[0].BufferStartingAddress;
   vb[0].EndAddress.offset += size - 1;
#endif

   blorp_emit_input_varying_data(batch, params,
                                 &vb[1].BufferStartingAddress, &size);
   vb[1].VertexBufferIndex = 1;
   vb[1].BufferPitch = 0;
   vb[1].VertexBufferMOCS = batch->blorp->mocs.vb;
#if GEN_GEN >= 7
   vb[1].AddressModifyEnable = true;
#endif
#if GEN_GEN >= 8
   vb[1].BufferSize = size;
#else
   vb[1].BufferAccessType = INSTANCEDATA;
   vb[1].EndAddress = vb[1].BufferStartingAddress;
   vb[1].EndAddress.offset += size - 1;
#endif

   const unsigned num_dwords = 1 + GENX(VERTEX_BUFFER_STATE_length) * 2;
   uint32_t *dw = blorp_emitn(batch, GENX(3DSTATE_VERTEX_BUFFERS), num_dwords);

   for (unsigned i = 0; i < 2; i++) {
      GENX(VERTEX_BUFFER_STATE_pack)(batch, dw, &vb[i]);
      dw += GENX(VERTEX_BUFFER_STATE_length);
   }
}

static void
blorp_emit_vertex_elements(struct blorp_batch *batch,
                           const struct blorp_params *params)
{
   const unsigned num_varyings =
      params->wm_prog_data ? params->wm_prog_data->num_varying_inputs : 0;
   const unsigned num_elements = 2 + num_varyings;

   struct GENX(VERTEX_ELEMENT_STATE) ve[num_elements];
   memset(ve, 0, num_elements * sizeof(*ve));

   /* Setup VBO for the rectangle primitive..
    *
    * A rectangle primitive (3DPRIM_RECTLIST) consists of only three
    * vertices. The vertices reside in screen space with DirectX
    * coordinates (that is, (0, 0) is the upper left corner).
    *
    *   v2 ------ implied
    *    |        |
    *    |        |
    *   v1 ----- v0
    *
    * Since the VS is disabled, the clipper loads each VUE directly from
    * the URB. This is controlled by the 3DSTATE_VERTEX_BUFFERS and
    * 3DSTATE_VERTEX_ELEMENTS packets below. The VUE contents are as follows:
    *   dw0: Reserved, MBZ.
    *   dw1: Render Target Array Index. Below vertex fetcher gets programmed
    *        to assign this with primitive instance identifier which will be
    *        used for layered clears. All other renders have only one instance
    *        and therefore the value will be effectively zero.
    *   dw2: Viewport Index. The HiZ op disables viewport mapping and
    *        scissoring, so set the dword to 0.
    *   dw3: Point Width: The HiZ op does not emit the POINTLIST primitive,
    *        so set the dword to 0.
    *   dw4: Vertex Position X.
    *   dw5: Vertex Position Y.
    *   dw6: Vertex Position Z.
    *   dw7: Vertex Position W.
    *
    *   dw8: Flat vertex input 0
    *   dw9: Flat vertex input 1
    *   ...
    *   dwn: Flat vertex input n - 8
    *
    * For details, see the Sandybridge PRM, Volume 2, Part 1, Section 1.5.1
    * "Vertex URB Entry (VUE) Formats".
    *
    * Only vertex position X and Y are going to be variable, Z is fixed to
    * zero and W to one. Header words dw0,2,3 are zero. There is no need to
    * include the fixed values in the vertex buffer. Vertex fetcher can be
    * instructed to fill vertex elements with constant values of one and zero
    * instead of reading them from the buffer.
    * Flat inputs are program constants that are not interpolated. Moreover
    * their values will be the same between vertices.
    *
    * See the vertex element setup below.
    */
   ve[0].VertexBufferIndex = 1;
   ve[0].Valid = true;
   ve[0].SourceElementFormat = ISL_FORMAT_R32G32B32A32_FLOAT;
   ve[0].SourceElementOffset = 0;
   ve[0].Component0Control = VFCOMP_STORE_SRC;

   /* From Gen8 onwards hardware is no more instructed to overwrite components
    * using an element specifier. Instead one has separate 3DSTATE_VF_SGVS
    * (System Generated Value Setup) state packet for it.
    */
#if GEN_GEN >= 8
   ve[0].Component1Control = VFCOMP_STORE_0;
#else
   ve[0].Component1Control = VFCOMP_STORE_IID;
#endif
   ve[0].Component2Control = VFCOMP_STORE_SRC;
   ve[0].Component3Control = VFCOMP_STORE_SRC;

   ve[1].VertexBufferIndex = 0;
   ve[1].Valid = true;
   ve[1].SourceElementFormat = ISL_FORMAT_R32G32B32_FLOAT;
   ve[1].SourceElementOffset = 0;
   ve[1].Component0Control = VFCOMP_STORE_SRC;
   ve[1].Component1Control = VFCOMP_STORE_SRC;
   ve[1].Component2Control = VFCOMP_STORE_SRC;
   ve[1].Component3Control = VFCOMP_STORE_1_FP;

   for (unsigned i = 0; i < num_varyings; ++i) {
      ve[i + 2].VertexBufferIndex = 1;
      ve[i + 2].Valid = true;
      ve[i + 2].SourceElementFormat = ISL_FORMAT_R32G32B32A32_FLOAT;
      ve[i + 2].SourceElementOffset = 16 + i * 4 * sizeof(float);
      ve[i + 2].Component0Control = VFCOMP_STORE_SRC;
      ve[i + 2].Component1Control = VFCOMP_STORE_SRC;
      ve[i + 2].Component2Control = VFCOMP_STORE_SRC;
      ve[i + 2].Component3Control = VFCOMP_STORE_SRC;
   }

   const unsigned num_dwords =
      1 + GENX(VERTEX_ELEMENT_STATE_length) * num_elements;
   uint32_t *dw = blorp_emitn(batch, GENX(3DSTATE_VERTEX_ELEMENTS), num_dwords);

   for (unsigned i = 0; i < num_elements; i++) {
      GENX(VERTEX_ELEMENT_STATE_pack)(batch, dw, &ve[i]);
      dw += GENX(VERTEX_ELEMENT_STATE_length);
   }

#if GEN_GEN >= 8
   /* Overwrite Render Target Array Index (2nd dword) in the VUE header with
    * primitive instance identifier. This is used for layered clears.
    */
   blorp_emit(batch, GENX(3DSTATE_VF_SGVS), sgvs) {
      sgvs.InstanceIDEnable = true;
      sgvs.InstanceIDComponentNumber = COMP_1;
      sgvs.InstanceIDElementOffset = 0;
   }

   for (unsigned i = 0; i < num_elements; i++) {
      blorp_emit(batch, GENX(3DSTATE_VF_INSTANCING), vf) {
         vf.VertexElementIndex = i;
         vf.InstancingEnable = false;
      }
   }

   blorp_emit(batch, GENX(3DSTATE_VF_TOPOLOGY), topo) {
      topo.PrimitiveTopologyType = _3DPRIM_RECTLIST;
   }
#endif
}

static void
blorp_emit_vs_config(struct blorp_batch *batch,
                     const struct blorp_params *params)
{
   struct brw_vs_prog_data *vs_prog_data = params->vs_prog_data;

   blorp_emit(batch, GENX(3DSTATE_VS), vs) {
      if (vs_prog_data) {
         vs.FunctionEnable = true;

         vs.KernelStartPointer = params->vs_prog_kernel;

         vs.DispatchGRFStartRegisterForURBData =
            vs_prog_data->base.base.dispatch_grf_start_reg;
         vs.VertexURBEntryReadLength =
            vs_prog_data->base.urb_read_length;
         vs.VertexURBEntryReadOffset = 0;

         vs.MaximumNumberofThreads =
            batch->blorp->isl_dev->info->max_vs_threads - 1;

#if GEN_GEN >= 8
         vs.SIMD8DispatchEnable =
            vs_prog_data->base.dispatch_mode == DISPATCH_MODE_SIMD8;
#endif
      }
   }
}

static void
blorp_emit_sf_config(struct blorp_batch *batch,
                     const struct blorp_params *params)
{
   const struct brw_wm_prog_data *prog_data = params->wm_prog_data;

   /* 3DSTATE_SF
    *
    * Disable ViewportTransformEnable (dw2.1)
    *
    * From the SandyBridge PRM, Volume 2, Part 1, Section 1.3, "3D
    * Primitives Overview":
    *     RECTLIST: Viewport Mapping must be DISABLED (as is typical with the
    *     use of screen- space coordinates).
    *
    * A solid rectangle must be rendered, so set FrontFaceFillMode (dw2.4:3)
    * and BackFaceFillMode (dw2.5:6) to SOLID(0).
    *
    * From the Sandy Bridge PRM, Volume 2, Part 1, Section
    * 6.4.1.1 3DSTATE_SF, Field FrontFaceFillMode:
    *     SOLID: Any triangle or rectangle object found to be front-facing
    *     is rendered as a solid object. This setting is required when
    *     (rendering rectangle (RECTLIST) objects.
    */

#if GEN_GEN >= 8

   blorp_emit(batch, GENX(3DSTATE_SF), sf);

   blorp_emit(batch, GENX(3DSTATE_RASTER), raster) {
      raster.CullMode = CULLMODE_NONE;
   }

   blorp_emit(batch, GENX(3DSTATE_SBE), sbe) {
      sbe.VertexURBEntryReadOffset = 1;
      if (prog_data) {
         sbe.NumberofSFOutputAttributes = prog_data->num_varying_inputs;
         sbe.VertexURBEntryReadLength = brw_blorp_get_urb_length(prog_data);
         sbe.ConstantInterpolationEnable = prog_data->flat_inputs;
      } else {
         sbe.NumberofSFOutputAttributes = 0;
         sbe.VertexURBEntryReadLength = 1;
      }
      sbe.ForceVertexURBEntryReadLength = true;
      sbe.ForceVertexURBEntryReadOffset = true;

#if GEN_GEN >= 9
      for (unsigned i = 0; i < 32; i++)
         sbe.AttributeActiveComponentFormat[i] = ACF_XYZW;
#endif
   }

#elif GEN_GEN >= 7

   blorp_emit(batch, GENX(3DSTATE_SF), sf) {
      sf.FrontFaceFillMode = FILL_MODE_SOLID;
      sf.BackFaceFillMode = FILL_MODE_SOLID;

      sf.MultisampleRasterizationMode = params->num_samples > 1 ?
         MSRASTMODE_ON_PATTERN : MSRASTMODE_OFF_PIXEL;

#if GEN_GEN == 7
      sf.DepthBufferSurfaceFormat = params->depth_format;
#endif
   }

   blorp_emit(batch, GENX(3DSTATE_SBE), sbe) {
      sbe.VertexURBEntryReadOffset = 1;
      if (prog_data) {
         sbe.NumberofSFOutputAttributes = prog_data->num_varying_inputs;
         sbe.VertexURBEntryReadLength = brw_blorp_get_urb_length(prog_data);
         sbe.ConstantInterpolationEnable = prog_data->flat_inputs;
      } else {
         sbe.NumberofSFOutputAttributes = 0;
         sbe.VertexURBEntryReadLength = 1;
      }
   }

#else /* GEN_GEN <= 6 */

   blorp_emit(batch, GENX(3DSTATE_SF), sf) {
      sf.FrontFaceFillMode = FILL_MODE_SOLID;
      sf.BackFaceFillMode = FILL_MODE_SOLID;

      sf.MultisampleRasterizationMode = params->num_samples > 1 ?
         MSRASTMODE_ON_PATTERN : MSRASTMODE_OFF_PIXEL;

      sf.VertexURBEntryReadOffset = 1;
      if (prog_data) {
         sf.NumberofSFOutputAttributes = prog_data->num_varying_inputs;
         sf.VertexURBEntryReadLength = brw_blorp_get_urb_length(prog_data);
         sf.ConstantInterpolationEnable = prog_data->flat_inputs;
      } else {
         sf.NumberofSFOutputAttributes = 0;
         sf.VertexURBEntryReadLength = 1;
      }
   }

#endif /* GEN_GEN */
}

static void
blorp_emit_ps_config(struct blorp_batch *batch,
                     const struct blorp_params *params)
{
   const struct brw_wm_prog_data *prog_data = params->wm_prog_data;

   /* Even when thread dispatch is disabled, max threads (dw5.25:31) must be
    * nonzero to prevent the GPU from hanging.  While the documentation doesn't
    * mention this explicitly, it notes that the valid range for the field is
    * [1,39] = [2,40] threads, which excludes zero.
    *
    * To be safe (and to minimize extraneous code) we go ahead and fully
    * configure the WM state whether or not there is a WM program.
    */

#if GEN_GEN >= 8

   blorp_emit(batch, GENX(3DSTATE_WM), wm);

   blorp_emit(batch, GENX(3DSTATE_PS), ps) {
      if (params->src.enabled) {
         ps.SamplerCount = 1; /* Up to 4 samplers */
         ps.BindingTableEntryCount = 2;
      } else {
         ps.BindingTableEntryCount = 1;
      }

      if (prog_data) {
         ps.DispatchGRFStartRegisterForConstantSetupData0 =
            prog_data->base.dispatch_grf_start_reg;
         ps.DispatchGRFStartRegisterForConstantSetupData2 =
            prog_data->dispatch_grf_start_reg_2;

         ps._8PixelDispatchEnable = prog_data->dispatch_8;
         ps._16PixelDispatchEnable = prog_data->dispatch_16;

         ps.KernelStartPointer0 = params->wm_prog_kernel;
         ps.KernelStartPointer2 =
            params->wm_prog_kernel + prog_data->prog_offset_2;
      }

      /* 3DSTATE_PS expects the number of threads per PSD, which is always 64;
       * it implicitly scales for different GT levels (which have some # of
       * PSDs).
       *
       * In Gen8 the format is U8-2 whereas in Gen9 it is U8-1.
       */
      if (GEN_GEN >= 9)
         ps.MaximumNumberofThreadsPerPSD = 64 - 1;
      else
         ps.MaximumNumberofThreadsPerPSD = 64 - 2;

      switch (params->fast_clear_op) {
      case BLORP_FAST_CLEAR_OP_NONE:
         break;
#if GEN_GEN >= 9
      case BLORP_FAST_CLEAR_OP_RESOLVE_PARTIAL:
         ps.RenderTargetResolveType = RESOLVE_PARTIAL;
         break;
      case BLORP_FAST_CLEAR_OP_RESOLVE_FULL:
         ps.RenderTargetResolveType = RESOLVE_FULL;
         break;
#else
      case BLORP_FAST_CLEAR_OP_RESOLVE_FULL:
         ps.RenderTargetResolveEnable = true;
         break;
#endif
      case BLORP_FAST_CLEAR_OP_CLEAR:
         ps.RenderTargetFastClearEnable = true;
         break;
      default:
         unreachable("Invalid fast clear op");
      }
   }

   blorp_emit(batch, GENX(3DSTATE_PS_EXTRA), psx) {
      if (prog_data) {
         psx.PixelShaderValid = true;
         psx.AttributeEnable = prog_data->num_varying_inputs > 0;
         psx.PixelShaderIsPerSample = prog_data->persample_dispatch;
      }

      if (params->src.enabled)
         psx.PixelShaderKillsPixel = true;
   }

#elif GEN_GEN >= 7

   blorp_emit(batch, GENX(3DSTATE_WM), wm) {
      switch (params->hiz_op) {
      case BLORP_HIZ_OP_DEPTH_CLEAR:
         wm.DepthBufferClear = true;
         break;
      case BLORP_HIZ_OP_DEPTH_RESOLVE:
         wm.DepthBufferResolveEnable = true;
         break;
      case BLORP_HIZ_OP_HIZ_RESOLVE:
         wm.HierarchicalDepthBufferResolveEnable = true;
         break;
      case BLORP_HIZ_OP_NONE:
         break;
      default:
         unreachable("not reached");
      }

      if (prog_data)
         wm.ThreadDispatchEnable = true;

      if (params->src.enabled)
         wm.PixelShaderKillsPixel = true;

      if (params->num_samples > 1) {
         wm.MultisampleRasterizationMode = MSRASTMODE_ON_PATTERN;
         wm.MultisampleDispatchMode =
            (prog_data && prog_data->persample_dispatch) ?
            MSDISPMODE_PERSAMPLE : MSDISPMODE_PERPIXEL;
      } else {
         wm.MultisampleRasterizationMode = MSRASTMODE_OFF_PIXEL;
         wm.MultisampleDispatchMode = MSDISPMODE_PERSAMPLE;
      }
   }

   blorp_emit(batch, GENX(3DSTATE_PS), ps) {
      ps.MaximumNumberofThreads =
         batch->blorp->isl_dev->info->max_wm_threads - 1;

#if GEN_IS_HASWELL
      ps.SampleMask = 1;
#endif

      if (prog_data) {
         ps.DispatchGRFStartRegisterForConstantSetupData0 =
            prog_data->base.dispatch_grf_start_reg;
         ps.DispatchGRFStartRegisterForConstantSetupData2 =
            prog_data->dispatch_grf_start_reg_2;

         ps.KernelStartPointer0 = params->wm_prog_kernel;
         ps.KernelStartPointer2 =
            params->wm_prog_kernel + prog_data->prog_offset_2;

         ps._8PixelDispatchEnable = prog_data->dispatch_8;
         ps._16PixelDispatchEnable = prog_data->dispatch_16;

         ps.AttributeEnable = prog_data->num_varying_inputs > 0;
      } else {
         /* Gen7 hardware gets angry if we don't enable at least one dispatch
          * mode, so just enable 16-pixel dispatch if we don't have a program.
          */
         ps._16PixelDispatchEnable = true;
      }

      if (params->src.enabled)
         ps.SamplerCount = 1; /* Up to 4 samplers */

      switch (params->fast_clear_op) {
      case BLORP_FAST_CLEAR_OP_NONE:
         break;
      case BLORP_FAST_CLEAR_OP_RESOLVE_FULL:
         ps.RenderTargetResolveEnable = true;
         break;
      case BLORP_FAST_CLEAR_OP_CLEAR:
         ps.RenderTargetFastClearEnable = true;
         break;
      default:
         unreachable("Invalid fast clear op");
      }
   }

#else /* GEN_GEN <= 6 */

   blorp_emit(batch, GENX(3DSTATE_WM), wm) {
      wm.MaximumNumberofThreads =
         batch->blorp->isl_dev->info->max_wm_threads - 1;

      switch (params->hiz_op) {
      case BLORP_HIZ_OP_DEPTH_CLEAR:
         wm.DepthBufferClear = true;
         break;
      case BLORP_HIZ_OP_DEPTH_RESOLVE:
         wm.DepthBufferResolveEnable = true;
         break;
      case BLORP_HIZ_OP_HIZ_RESOLVE:
         wm.HierarchicalDepthBufferResolveEnable = true;
         break;
      case BLORP_HIZ_OP_NONE:
         break;
      default:
         unreachable("not reached");
      }

      if (prog_data) {
         wm.ThreadDispatchEnable = true;

         wm.DispatchGRFStartRegisterForConstantSetupData0 =
            prog_data->base.dispatch_grf_start_reg;
         wm.DispatchGRFStartRegisterForConstantSetupData2 =
            prog_data->dispatch_grf_start_reg_2;

         wm.KernelStartPointer0 = params->wm_prog_kernel;
         wm.KernelStartPointer2 =
            params->wm_prog_kernel + prog_data->prog_offset_2;

         wm._8PixelDispatchEnable = prog_data->dispatch_8;
         wm._16PixelDispatchEnable = prog_data->dispatch_16;

         wm.NumberofSFOutputAttributes = prog_data->num_varying_inputs;
      }

      if (params->src.enabled) {
         wm.SamplerCount = 1; /* Up to 4 samplers */
         wm.PixelShaderKillsPixel = true; /* TODO: temporarily smash on */
      }

      if (params->num_samples > 1) {
         wm.MultisampleRasterizationMode = MSRASTMODE_ON_PATTERN;
         wm.MultisampleDispatchMode =
            (prog_data && prog_data->persample_dispatch) ?
            MSDISPMODE_PERSAMPLE : MSDISPMODE_PERPIXEL;
      } else {
         wm.MultisampleRasterizationMode = MSRASTMODE_OFF_PIXEL;
         wm.MultisampleDispatchMode = MSDISPMODE_PERSAMPLE;
      }
   }

#endif /* GEN_GEN */
}

static const uint32_t isl_to_gen_ds_surftype [] = {
#if GEN_GEN >= 9
   /* From the SKL PRM, "3DSTATE_DEPTH_STENCIL::SurfaceType":
    *
    *    "If depth/stencil is enabled with 1D render target, depth/stencil
    *    surface type needs to be set to 2D surface type and height set to 1.
    *    Depth will use (legacy) TileY and stencil will use TileW. For this
    *    case only, the Surface Type of the depth buffer can be 2D while the
    *    Surface Type of the render target(s) are 1D, representing an
    *    exception to a programming note above.
    */
   [ISL_SURF_DIM_1D] = SURFTYPE_2D,
#else
   [ISL_SURF_DIM_1D] = SURFTYPE_1D,
#endif
   [ISL_SURF_DIM_2D] = SURFTYPE_2D,
   [ISL_SURF_DIM_3D] = SURFTYPE_3D,
};

static void
blorp_emit_depth_stencil_config(struct blorp_batch *batch,
                                const struct blorp_params *params)
{
#if GEN_GEN >= 7
   const uint32_t mocs = 1; /* GEN7_MOCS_L3 */
#else
   const uint32_t mocs = 0;
#endif

   blorp_emit(batch, GENX(3DSTATE_DEPTH_BUFFER), db) {
#if GEN_GEN >= 7
      db.DepthWriteEnable = params->depth.enabled;
      db.StencilWriteEnable = params->stencil.enabled;
#endif

#if GEN_GEN <= 6
      db.SeparateStencilBufferEnable = true;
#endif

      if (params->depth.enabled) {
         db.SurfaceFormat = params->depth_format;
         db.SurfaceType = isl_to_gen_ds_surftype[params->depth.surf.dim];

#if GEN_GEN <= 6
         db.TiledSurface = true;
         db.TileWalk = TILEWALK_YMAJOR;
         db.MIPMapLayoutMode = MIPLAYOUT_BELOW;
#endif

         db.HierarchicalDepthBufferEnable =
            params->depth.aux_usage == ISL_AUX_USAGE_HIZ;

         db.Width = params->depth.surf.logical_level0_px.width - 1;
         db.Height = params->depth.surf.logical_level0_px.height - 1;
         db.RenderTargetViewExtent = db.Depth =
            params->depth.view.array_len - 1;

         db.LOD = params->depth.view.base_level;
         db.MinimumArrayElement = params->depth.view.base_array_layer;

         db.SurfacePitch = params->depth.surf.row_pitch - 1;
#if GEN_GEN >= 8
         db.SurfaceQPitch =
            isl_surf_get_array_pitch_el_rows(&params->depth.surf) >> 2,
#endif

         db.SurfaceBaseAddress = params->depth.addr;
         db.DepthBufferMOCS = mocs;
      } else if (params->stencil.enabled) {
         db.SurfaceFormat = D32_FLOAT;
         db.SurfaceType = isl_to_gen_ds_surftype[params->stencil.surf.dim];

         db.Width = params->stencil.surf.logical_level0_px.width - 1;
         db.Height = params->stencil.surf.logical_level0_px.height - 1;
         db.RenderTargetViewExtent = db.Depth =
            params->stencil.view.array_len - 1;

         db.LOD = params->stencil.view.base_level;
         db.MinimumArrayElement = params->stencil.view.base_array_layer;
      } else {
         db.SurfaceType = SURFTYPE_NULL;
         db.SurfaceFormat = D32_FLOAT;
      }
   }

   blorp_emit(batch, GENX(3DSTATE_HIER_DEPTH_BUFFER), hiz) {
      if (params->depth.aux_usage == ISL_AUX_USAGE_HIZ) {
         hiz.SurfacePitch = params->depth.aux_surf.row_pitch - 1;
         hiz.SurfaceBaseAddress = params->depth.aux_addr;
         hiz.HierarchicalDepthBufferMOCS = mocs;
#if GEN_GEN >= 8
         hiz.SurfaceQPitch =
            isl_surf_get_array_pitch_sa_rows(&params->depth.aux_surf) >> 2;
#endif
      }
   }

   blorp_emit(batch, GENX(3DSTATE_STENCIL_BUFFER), sb) {
      if (params->stencil.enabled) {
#if GEN_GEN >= 8 || GEN_IS_HASWELL
         sb.StencilBufferEnable = true;
#endif

         sb.SurfacePitch = params->stencil.surf.row_pitch - 1,
#if GEN_GEN >= 8
         sb.SurfaceQPitch =
            isl_surf_get_array_pitch_el_rows(&params->stencil.surf) >> 2,
#endif

         sb.SurfaceBaseAddress = params->stencil.addr;
         sb.StencilBufferMOCS = batch->blorp->mocs.tex;
      }
   }

   /* 3DSTATE_CLEAR_PARAMS
    *
    * From the Sandybridge PRM, Volume 2, Part 1, Section 3DSTATE_CLEAR_PARAMS:
    *   [DevSNB] 3DSTATE_CLEAR_PARAMS packet must follow the DEPTH_BUFFER_STATE
    *   packet when HiZ is enabled and the DEPTH_BUFFER_STATE changes.
    */
   blorp_emit(batch, GENX(3DSTATE_CLEAR_PARAMS), clear) {
      clear.DepthClearValueValid = true;
      clear.DepthClearValue = params->depth.clear_color.u32[0];
   }
}

static uint32_t
blorp_emit_blend_state(struct blorp_batch *batch,
                       const struct blorp_params *params)
{
   struct GENX(BLEND_STATE) blend;
   memset(&blend, 0, sizeof(blend));

   for (unsigned i = 0; i < params->num_draw_buffers; ++i) {
      blend.Entry[i].PreBlendColorClampEnable = true;
      blend.Entry[i].PostBlendColorClampEnable = true;
      blend.Entry[i].ColorClampRange = COLORCLAMP_RTFORMAT;

      blend.Entry[i].WriteDisableRed = params->color_write_disable[0];
      blend.Entry[i].WriteDisableGreen = params->color_write_disable[1];
      blend.Entry[i].WriteDisableBlue = params->color_write_disable[2];
      blend.Entry[i].WriteDisableAlpha = params->color_write_disable[3];
   }

   uint32_t offset;
   void *state = blorp_alloc_dynamic_state(batch, AUB_TRACE_BLEND_STATE,
                                           GENX(BLEND_STATE_length) * 4,
                                           64, &offset);
   GENX(BLEND_STATE_pack)(NULL, state, &blend);
   blorp_flush_range(batch, state, GENX(BLEND_STATE_length) * 4);

#if GEN_GEN >= 7
   blorp_emit(batch, GENX(3DSTATE_BLEND_STATE_POINTERS), sp) {
      sp.BlendStatePointer = offset;
#if GEN_GEN >= 8
      sp.BlendStatePointerValid = true;
#endif
   }
#endif

#if GEN_GEN >= 8
   blorp_emit(batch, GENX(3DSTATE_PS_BLEND), ps_blend) {
      ps_blend.HasWriteableRT = true;
   }
#endif

   return offset;
}

static uint32_t
blorp_emit_color_calc_state(struct blorp_batch *batch,
                            const struct blorp_params *params)
{
   struct GENX(COLOR_CALC_STATE) cc = { 0 };

#if GEN_GEN <= 8
   cc.StencilReferenceValue = params->stencil_ref;
#endif

   uint32_t offset;
   void *state = blorp_alloc_dynamic_state(batch, AUB_TRACE_CC_STATE,
                                           GENX(COLOR_CALC_STATE_length) * 4,
                                           64, &offset);
   GENX(COLOR_CALC_STATE_pack)(NULL, state, &cc);
   blorp_flush_range(batch, state, GENX(COLOR_CALC_STATE_length) * 4);

#if GEN_GEN >= 7
   blorp_emit(batch, GENX(3DSTATE_CC_STATE_POINTERS), sp) {
      sp.ColorCalcStatePointer = offset;
#if GEN_GEN >= 8
      sp.ColorCalcStatePointerValid = true;
#endif
   }
#endif

   return offset;
}

static uint32_t
blorp_emit_depth_stencil_state(struct blorp_batch *batch,
                               const struct blorp_params *params)
{
#if GEN_GEN >= 8
   struct GENX(3DSTATE_WM_DEPTH_STENCIL) ds = {
      GENX(3DSTATE_WM_DEPTH_STENCIL_header),
   };
#else
   struct GENX(DEPTH_STENCIL_STATE) ds = { 0 };
#endif

   if (params->depth.enabled) {
      ds.DepthBufferWriteEnable = true;

      switch (params->hiz_op) {
      case BLORP_HIZ_OP_NONE:
         ds.DepthTestEnable = true;
         ds.DepthTestFunction = COMPAREFUNCTION_ALWAYS;
         break;

      /* See the following sections of the Sandy Bridge PRM, Volume 2, Part1:
       *   - 7.5.3.1 Depth Buffer Clear
       *   - 7.5.3.2 Depth Buffer Resolve
       *   - 7.5.3.3 Hierarchical Depth Buffer Resolve
       */
      case BLORP_HIZ_OP_DEPTH_RESOLVE:
         ds.DepthTestEnable = true;
         ds.DepthTestFunction = COMPAREFUNCTION_NEVER;
         break;

      case BLORP_HIZ_OP_DEPTH_CLEAR:
      case BLORP_HIZ_OP_HIZ_RESOLVE:
         ds.DepthTestEnable = false;
         break;
      }
   }

   if (params->stencil.enabled) {
      ds.StencilBufferWriteEnable = true;
      ds.StencilTestEnable = true;
      ds.DoubleSidedStencilEnable = false;

      ds.StencilTestFunction = COMPAREFUNCTION_ALWAYS;
      ds.StencilPassDepthPassOp = STENCILOP_REPLACE;

      ds.StencilWriteMask = params->stencil_mask;
#if GEN_GEN >= 9
      ds.StencilReferenceValue = params->stencil_ref;
#endif
   }

#if GEN_GEN >= 8
   uint32_t offset = 0;
   uint32_t *dw = blorp_emit_dwords(batch,
                                    GENX(3DSTATE_WM_DEPTH_STENCIL_length));
   GENX(3DSTATE_WM_DEPTH_STENCIL_pack)(NULL, dw, &ds);
#else
   uint32_t offset;
   void *state = blorp_alloc_dynamic_state(batch, AUB_TRACE_DEPTH_STENCIL_STATE,
                                           GENX(DEPTH_STENCIL_STATE_length) * 4,
                                           64, &offset);
   GENX(DEPTH_STENCIL_STATE_pack)(NULL, state, &ds);
   blorp_flush_range(batch, state, GENX(DEPTH_STENCIL_STATE_length) * 4);
#endif

#if GEN_GEN == 7
   blorp_emit(batch, GENX(3DSTATE_DEPTH_STENCIL_STATE_POINTERS), sp) {
      sp.PointertoDEPTH_STENCIL_STATE = offset;
   }
#endif

   return offset;
}

static void
blorp_emit_surface_state(struct blorp_batch *batch,
                         const struct brw_blorp_surface_info *surface,
                         void *state, uint32_t state_offset,
                         bool is_render_target)
{
   const struct isl_device *isl_dev = batch->blorp->isl_dev;
   struct isl_surf surf = surface->surf;

   if (surf.dim == ISL_SURF_DIM_1D &&
       surf.dim_layout == ISL_DIM_LAYOUT_GEN4_2D) {
      assert(surf.logical_level0_px.height == 1);
      surf.dim = ISL_SURF_DIM_2D;
   }

   /* Blorp doesn't support HiZ in any of the blit or slow-clear paths */
   enum isl_aux_usage aux_usage = surface->aux_usage;
   if (aux_usage == ISL_AUX_USAGE_HIZ)
      aux_usage = ISL_AUX_USAGE_NONE;

   const uint32_t mocs =
      is_render_target ? batch->blorp->mocs.rb : batch->blorp->mocs.tex;

   isl_surf_fill_state(batch->blorp->isl_dev, state,
                       .surf = &surf, .view = &surface->view,
                       .aux_surf = &surface->aux_surf, .aux_usage = aux_usage,
                       .mocs = mocs, .clear_color = surface->clear_color);

   blorp_surface_reloc(batch, state_offset + isl_dev->ss.addr_offset,
                       surface->addr, 0);

   if (aux_usage != ISL_AUX_USAGE_NONE) {
      /* On gen7 and prior, the bottom 12 bits of the MCS base address are
       * used to store other information.  This should be ok, however, because
       * surface buffer addresses are always 4K page alinged.
       */
      assert((surface->aux_addr.offset & 0xfff) == 0);
      uint32_t *aux_addr = state + isl_dev->ss.aux_addr_offset;
      blorp_surface_reloc(batch, state_offset + isl_dev->ss.aux_addr_offset,
                          surface->aux_addr, *aux_addr);
   }

   blorp_flush_range(batch, state, GENX(RENDER_SURFACE_STATE_length) * 4);
}

static void
blorp_emit_null_surface_state(struct blorp_batch *batch,
                              const struct brw_blorp_surface_info *surface,
                              uint32_t *state)
{
   struct GENX(RENDER_SURFACE_STATE) ss = {
      .SurfaceType = SURFTYPE_NULL,
      .SurfaceFormat = ISL_FORMAT_R8G8B8A8_UNORM,
      .Width = surface->surf.logical_level0_px.width - 1,
      .Height = surface->surf.logical_level0_px.height - 1,
      .MIPCountLOD = surface->view.base_level,
      .MinimumArrayElement = surface->view.base_array_layer,
      .Depth = surface->view.array_len - 1,
      .RenderTargetViewExtent = surface->view.array_len - 1,
      .NumberofMultisamples = ffs(surface->surf.samples) - 1,

#if GEN_GEN >= 7
      .SurfaceArray = surface->surf.dim != ISL_SURF_DIM_3D,
#endif

#if GEN_GEN >= 8
      .TileMode = YMAJOR,
#else
      .TiledSurface = true,
#endif
   };

   GENX(RENDER_SURFACE_STATE_pack)(NULL, state, &ss);

   blorp_flush_range(batch, state, GENX(RENDER_SURFACE_STATE_length) * 4);
}

static void
blorp_emit_surface_states(struct blorp_batch *batch,
                          const struct blorp_params *params)
{
   const struct isl_device *isl_dev = batch->blorp->isl_dev;
   uint32_t bind_offset, surface_offsets[2];
   void *surface_maps[2];

   if (params->use_pre_baked_binding_table) {
      bind_offset = params->pre_baked_binding_table_offset;
   } else {
      unsigned num_surfaces = 1 + params->src.enabled;
      blorp_alloc_binding_table(batch, num_surfaces,
                                isl_dev->ss.size, isl_dev->ss.align,
                                &bind_offset, surface_offsets, surface_maps);

      if (params->dst.enabled) {
         blorp_emit_surface_state(batch, &params->dst,
                                  surface_maps[BLORP_RENDERBUFFER_BT_INDEX],
                                  surface_offsets[BLORP_RENDERBUFFER_BT_INDEX],
                                  true);
      } else {
         assert(params->depth.enabled || params->stencil.enabled);
         const struct brw_blorp_surface_info *surface =
            params->depth.enabled ? &params->depth : &params->stencil;
         blorp_emit_null_surface_state(batch, surface,
                                       surface_maps[BLORP_RENDERBUFFER_BT_INDEX]);
      }

      if (params->src.enabled) {
         blorp_emit_surface_state(batch, &params->src,
                                  surface_maps[BLORP_TEXTURE_BT_INDEX],
                                  surface_offsets[BLORP_TEXTURE_BT_INDEX], false);
      }
   }

#if GEN_GEN >= 7
   blorp_emit(batch, GENX(3DSTATE_BINDING_TABLE_POINTERS_VS), bt);
   blorp_emit(batch, GENX(3DSTATE_BINDING_TABLE_POINTERS_HS), bt);
   blorp_emit(batch, GENX(3DSTATE_BINDING_TABLE_POINTERS_DS), bt);
   blorp_emit(batch, GENX(3DSTATE_BINDING_TABLE_POINTERS_GS), bt);

   blorp_emit(batch, GENX(3DSTATE_BINDING_TABLE_POINTERS_PS), bt) {
      bt.PointertoPSBindingTable = bind_offset;
   }
#else
   blorp_emit(batch, GENX(3DSTATE_BINDING_TABLE_POINTERS), bt) {
      bt.PSBindingTableChange = true;
      bt.PointertoPSBindingTable = bind_offset;
   }
#endif
}

static void
blorp_emit_sampler_state(struct blorp_batch *batch,
                         const struct blorp_params *params)
{
   struct GENX(SAMPLER_STATE) sampler = {
      .MipModeFilter = MIPFILTER_NONE,
      .MagModeFilter = MAPFILTER_LINEAR,
      .MinModeFilter = MAPFILTER_LINEAR,
      .MinLOD = 0,
      .MaxLOD = 0,
      .TCXAddressControlMode = TCM_CLAMP,
      .TCYAddressControlMode = TCM_CLAMP,
      .TCZAddressControlMode = TCM_CLAMP,
      .MaximumAnisotropy = RATIO21,
      .RAddressMinFilterRoundingEnable = true,
      .RAddressMagFilterRoundingEnable = true,
      .VAddressMinFilterRoundingEnable = true,
      .VAddressMagFilterRoundingEnable = true,
      .UAddressMinFilterRoundingEnable = true,
      .UAddressMagFilterRoundingEnable = true,
      .NonnormalizedCoordinateEnable = true,
   };

   uint32_t offset;
   void *state = blorp_alloc_dynamic_state(batch, AUB_TRACE_SAMPLER_STATE,
                                           GENX(SAMPLER_STATE_length) * 4,
                                           32, &offset);
   GENX(SAMPLER_STATE_pack)(NULL, state, &sampler);
   blorp_flush_range(batch, state, GENX(SAMPLER_STATE_length) * 4);

#if GEN_GEN >= 7
   blorp_emit(batch, GENX(3DSTATE_SAMPLER_STATE_POINTERS_PS), ssp) {
      ssp.PointertoPSSamplerState = offset;
   }
#else
   blorp_emit(batch, GENX(3DSTATE_SAMPLER_STATE_POINTERS), ssp) {
      ssp.VSSamplerStateChange = true;
      ssp.GSSamplerStateChange = true;
      ssp.PSSamplerStateChange = true;
      ssp.PointertoPSSamplerState = offset;
   }
#endif
}

static void
blorp_emit_3dstate_multisample(struct blorp_batch *batch,
                               const struct blorp_params *params)
{
   blorp_emit(batch, GENX(3DSTATE_MULTISAMPLE), ms) {
      ms.NumberofMultisamples       = __builtin_ffs(params->num_samples) - 1;

#if GEN_GEN >= 8
      /* The PRM says that this bit is valid only for DX9:
       *
       *    SW can choose to set this bit only for DX9 API. DX10/OGL API's
       *    should not have any effect by setting or not setting this bit.
       */
      ms.PixelPositionOffsetEnable  = false;
      ms.PixelLocation              = CENTER;
#elif GEN_GEN >= 7
      ms.PixelLocation              = PIXLOC_CENTER;

      switch (params->num_samples) {
      case 1:
         GEN_SAMPLE_POS_1X(ms.Sample);
         break;
      case 2:
         GEN_SAMPLE_POS_2X(ms.Sample);
         break;
      case 4:
         GEN_SAMPLE_POS_4X(ms.Sample);
         break;
      case 8:
         GEN_SAMPLE_POS_8X(ms.Sample);
         break;
      default:
         break;
      }
#else
      ms.PixelLocation              = PIXLOC_CENTER;
      GEN_SAMPLE_POS_4X(ms.Sample);
#endif
   }
}

#if GEN_GEN >= 8
/* Emits the Optimized HiZ sequence specified in the BDW+ PRMs. The
 * depth/stencil buffer extents are ignored to handle APIs which perform
 * clearing operations without such information.
 * */
static void
blorp_emit_gen8_hiz_op(struct blorp_batch *batch,
                       const struct blorp_params *params)
{
   /* We should be performing an operation on a depth or stencil buffer.
    */
   assert(params->depth.enabled || params->stencil.enabled);

   /* The stencil buffer should only be enabled if a fast clear operation is
    * requested.
    */
   if (params->stencil.enabled)
      assert(params->hiz_op == BLORP_HIZ_OP_DEPTH_CLEAR);

   /* If we can't alter the depth stencil config and multiple layers are
    * involved, the HiZ op will fail. This is because the op requires that a
    * new config is emitted for each additional layer.
    */
   if (batch->flags & BLORP_BATCH_NO_EMIT_DEPTH_STENCIL) {
      assert(params->num_layers <= 1);
   } else {
      blorp_emit_depth_stencil_config(batch, params);
   }

   blorp_emit(batch, GENX(3DSTATE_WM_HZ_OP), hzp) {
      switch (params->hiz_op) {
      case BLORP_HIZ_OP_DEPTH_CLEAR:
         hzp.StencilBufferClearEnable = params->stencil.enabled;
         hzp.DepthBufferClearEnable = params->depth.enabled;
         hzp.StencilClearValue = params->stencil_ref;
         break;
      case BLORP_HIZ_OP_DEPTH_RESOLVE:
         hzp.DepthBufferResolveEnable = true;
         break;
      case BLORP_HIZ_OP_HIZ_RESOLVE:
         hzp.HierarchicalDepthBufferResolveEnable = true;
         break;
      case BLORP_HIZ_OP_NONE:
         unreachable("Invalid HIZ op");
      }

      hzp.NumberofMultisamples = ffs(params->num_samples) - 1;
      hzp.SampleMask = 0xFFFF;

      /* Due to a hardware issue, this bit MBZ */
      assert(hzp.ScissorRectangleEnable == false);

      /* Contrary to the HW docs both fields are inclusive */
      hzp.ClearRectangleXMin = params->x0;
      hzp.ClearRectangleYMin = params->y0;

      /* Contrary to the HW docs both fields are exclusive */
      hzp.ClearRectangleXMax = params->x1;
      hzp.ClearRectangleYMax = params->y1;
   }

   /* PIPE_CONTROL w/ all bits clear except for “Post-Sync Operation” must set
    * to “Write Immediate Data” enabled.
    */
   blorp_emit(batch, GENX(PIPE_CONTROL), pc) {
      pc.PostSyncOperation = WriteImmediateData;
   }

   blorp_emit(batch, GENX(3DSTATE_WM_HZ_OP), hzp);

   /* Perform depth clear specific flushing */
   if (params->hiz_op == BLORP_HIZ_OP_DEPTH_CLEAR && params->depth.enabled) {
      blorp_emit(batch, GENX(PIPE_CONTROL), pc) {
         pc.DepthStallEnable = true;
         pc.DepthCacheFlushEnable = true;
      }
   }
}
#endif

/* 3DSTATE_VIEWPORT_STATE_POINTERS */
static void
blorp_emit_viewport_state(struct blorp_batch *batch,
                          const struct blorp_params *params)
{
   uint32_t cc_vp_offset;

   void *state = blorp_alloc_dynamic_state(batch, AUB_TRACE_CC_VP_STATE,
                                           GENX(CC_VIEWPORT_length) * 4, 32,
                                           &cc_vp_offset);

   GENX(CC_VIEWPORT_pack)(batch, state,
      &(struct GENX(CC_VIEWPORT)) {
         .MinimumDepth = 0.0,
         .MaximumDepth = 1.0,
      });
   blorp_flush_range(batch, state, GENX(CC_VIEWPORT_length) * 4);

#if GEN_GEN >= 7
   blorp_emit(batch, GENX(3DSTATE_VIEWPORT_STATE_POINTERS_CC), vsp) {
      vsp.CCViewportPointer = cc_vp_offset;
   }
#else
   blorp_emit(batch, GENX(3DSTATE_VIEWPORT_STATE_POINTERS), vsp) {
      vsp.CCViewportStateChange = true;
      vsp.PointertoCC_VIEWPORT = cc_vp_offset;
   }
#endif
}


/**
 * \brief Execute a blit or render pass operation.
 *
 * To execute the operation, this function manually constructs and emits a
 * batch to draw a rectangle primitive. The batchbuffer is flushed before
 * constructing and after emitting the batch.
 *
 * This function alters no GL state.
 */
static void
blorp_exec(struct blorp_batch *batch, const struct blorp_params *params)
{
   uint32_t blend_state_offset = 0;
   uint32_t color_calc_state_offset = 0;
   uint32_t depth_stencil_state_offset;

#if GEN_GEN >= 8
   if (params->hiz_op != BLORP_HIZ_OP_NONE) {
      blorp_emit_gen8_hiz_op(batch, params);
      return;
   }
#endif

   blorp_emit_vertex_buffers(batch, params);
   blorp_emit_vertex_elements(batch, params);

   emit_urb_config(batch, params);

   if (params->wm_prog_data) {
      blend_state_offset = blorp_emit_blend_state(batch, params);
   }
   color_calc_state_offset = blorp_emit_color_calc_state(batch, params);
   depth_stencil_state_offset = blorp_emit_depth_stencil_state(batch, params);

#if GEN_GEN <= 6
   /* 3DSTATE_CC_STATE_POINTERS
    *
    * The pointer offsets are relative to
    * CMD_STATE_BASE_ADDRESS.DynamicStateBaseAddress.
    *
    * The HiZ op doesn't use BLEND_STATE or COLOR_CALC_STATE.
    *
    * The dynamic state emit helpers emit their own STATE_POINTERS packets on
    * gen7+.  However, on gen6 and earlier, they're all lumpped together in
    * one CC_STATE_POINTERS packet so we have to emit that here.
    */
   blorp_emit(batch, GENX(3DSTATE_CC_STATE_POINTERS), cc) {
      cc.BLEND_STATEChange = true;
      cc.COLOR_CALC_STATEChange = true;
      cc.DEPTH_STENCIL_STATEChange = true;
      cc.PointertoBLEND_STATE = blend_state_offset;
      cc.PointertoCOLOR_CALC_STATE = color_calc_state_offset;
      cc.PointertoDEPTH_STENCIL_STATE = depth_stencil_state_offset;
   }
#else
   (void)blend_state_offset;
   (void)color_calc_state_offset;
   (void)depth_stencil_state_offset;
#endif

   blorp_emit(batch, GENX(3DSTATE_CONSTANT_VS), vs);
#if GEN_GEN >= 7
   blorp_emit(batch, GENX(3DSTATE_CONSTANT_HS), hs);
   blorp_emit(batch, GENX(3DSTATE_CONSTANT_DS), DS);
#endif
   blorp_emit(batch, GENX(3DSTATE_CONSTANT_GS), gs);
   blorp_emit(batch, GENX(3DSTATE_CONSTANT_PS), ps);

   blorp_emit_surface_states(batch, params);

   if (params->src.enabled)
      blorp_emit_sampler_state(batch, params);

   blorp_emit_3dstate_multisample(batch, params);

   blorp_emit(batch, GENX(3DSTATE_SAMPLE_MASK), mask) {
      mask.SampleMask = (1 << params->num_samples) - 1;
   }

   /* From the BSpec, 3D Pipeline > Geometry > Vertex Shader > State,
    * 3DSTATE_VS, Dword 5.0 "VS Function Enable":
    *
    *   [DevSNB] A pipeline flush must be programmed prior to a
    *   3DSTATE_VS command that causes the VS Function Enable to
    *   toggle. Pipeline flush can be executed by sending a PIPE_CONTROL
    *   command with CS stall bit set and a post sync operation.
    *
    * We've already done one at the start of the BLORP operation.
    */
   blorp_emit_vs_config(batch, params);
#if GEN_GEN >= 7
   blorp_emit(batch, GENX(3DSTATE_HS), hs);
   blorp_emit(batch, GENX(3DSTATE_TE), te);
   blorp_emit(batch, GENX(3DSTATE_DS), DS);
   blorp_emit(batch, GENX(3DSTATE_STREAMOUT), so);
#endif
   blorp_emit(batch, GENX(3DSTATE_GS), gs);

   blorp_emit(batch, GENX(3DSTATE_CLIP), clip) {
      clip.PerspectiveDivideDisable = true;
   }

   blorp_emit_sf_config(batch, params);
   blorp_emit_ps_config(batch, params);

   blorp_emit_viewport_state(batch, params);

   if (!(batch->flags & BLORP_BATCH_NO_EMIT_DEPTH_STENCIL))
      blorp_emit_depth_stencil_config(batch, params);

   blorp_emit(batch, GENX(3DPRIMITIVE), prim) {
      prim.VertexAccessType = SEQUENTIAL;
      prim.PrimitiveTopologyType = _3DPRIM_RECTLIST;
      prim.VertexCountPerInstance = 3;
      prim.InstanceCount = params->num_layers;
   }
}

#endif /* BLORP_GENX_EXEC_H */
