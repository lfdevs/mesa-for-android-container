/*
 * Copyright © 2015 Intel Corporation
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

#ifndef GENX_PIPELINE_UTIL_H
#define GENX_PIPELINE_UTIL_H

#include "common/gen_l3_config.h"
#include "common/gen_sample_positions.h"
#include "vk_format_info.h"

static uint32_t
vertex_element_comp_control(enum isl_format format, unsigned comp)
{
   uint8_t bits;
   switch (comp) {
   case 0: bits = isl_format_layouts[format].channels.r.bits; break;
   case 1: bits = isl_format_layouts[format].channels.g.bits; break;
   case 2: bits = isl_format_layouts[format].channels.b.bits; break;
   case 3: bits = isl_format_layouts[format].channels.a.bits; break;
   default: unreachable("Invalid component");
   }

   if (bits) {
      return VFCOMP_STORE_SRC;
   } else if (comp < 3) {
      return VFCOMP_STORE_0;
   } else if (isl_format_layouts[format].channels.r.type == ISL_UINT ||
            isl_format_layouts[format].channels.r.type == ISL_SINT) {
      assert(comp == 3);
      return VFCOMP_STORE_1_INT;
   } else {
      assert(comp == 3);
      return VFCOMP_STORE_1_FP;
   }
}

static void
emit_vertex_input(struct anv_pipeline *pipeline,
                  const VkPipelineVertexInputStateCreateInfo *info)
{
   const struct brw_vs_prog_data *vs_prog_data = get_vs_prog_data(pipeline);

   /* Pull inputs_read out of the VS prog data */
   const uint64_t inputs_read = vs_prog_data->inputs_read;
   assert((inputs_read & ((1 << VERT_ATTRIB_GENERIC0) - 1)) == 0);
   const uint32_t elements = inputs_read >> VERT_ATTRIB_GENERIC0;

#if GEN_GEN >= 8
   /* On BDW+, we only need to allocate space for base ids.  Setting up
    * the actual vertex and instance id is a separate packet.
    */
   const bool needs_svgs_elem = vs_prog_data->uses_basevertex ||
                                vs_prog_data->uses_baseinstance;
#else
   /* On Haswell and prior, vertex and instance id are created by using the
    * ComponentControl fields, so we need an element for any of them.
    */
   const bool needs_svgs_elem = vs_prog_data->uses_vertexid ||
                                vs_prog_data->uses_instanceid ||
                                vs_prog_data->uses_basevertex ||
                                vs_prog_data->uses_baseinstance;
#endif

   uint32_t elem_count = __builtin_popcount(elements) + needs_svgs_elem;
   if (elem_count == 0)
      return;

   uint32_t *p;

   const uint32_t num_dwords = 1 + elem_count * 2;
   p = anv_batch_emitn(&pipeline->batch, num_dwords,
                       GENX(3DSTATE_VERTEX_ELEMENTS));
   memset(p + 1, 0, (num_dwords - 1) * 4);

   for (uint32_t i = 0; i < info->vertexAttributeDescriptionCount; i++) {
      const VkVertexInputAttributeDescription *desc =
         &info->pVertexAttributeDescriptions[i];
      enum isl_format format = anv_get_isl_format(&pipeline->device->info,
                                                  desc->format,
                                                  VK_IMAGE_ASPECT_COLOR_BIT,
                                                  VK_IMAGE_TILING_LINEAR);

      assert(desc->binding < 32);

      if ((elements & (1 << desc->location)) == 0)
         continue; /* Binding unused */

      uint32_t slot = __builtin_popcount(elements & ((1 << desc->location) - 1));

      struct GENX(VERTEX_ELEMENT_STATE) element = {
         .VertexBufferIndex = desc->binding,
         .Valid = true,
         .SourceElementFormat = format,
         .EdgeFlagEnable = false,
         .SourceElementOffset = desc->offset,
         .Component0Control = vertex_element_comp_control(format, 0),
         .Component1Control = vertex_element_comp_control(format, 1),
         .Component2Control = vertex_element_comp_control(format, 2),
         .Component3Control = vertex_element_comp_control(format, 3),
      };
      GENX(VERTEX_ELEMENT_STATE_pack)(NULL, &p[1 + slot * 2], &element);

#if GEN_GEN >= 8
      /* On Broadwell and later, we have a separate VF_INSTANCING packet
       * that controls instancing.  On Haswell and prior, that's part of
       * VERTEX_BUFFER_STATE which we emit later.
       */
      anv_batch_emit(&pipeline->batch, GENX(3DSTATE_VF_INSTANCING), vfi) {
         vfi.InstancingEnable = pipeline->instancing_enable[desc->binding];
         vfi.VertexElementIndex = slot;
         /* Vulkan so far doesn't have an instance divisor, so
          * this is always 1 (ignored if not instancing). */
         vfi.InstanceDataStepRate = 1;
      }
#endif
   }

   const uint32_t id_slot = __builtin_popcount(elements);
   if (needs_svgs_elem) {
      /* From the Broadwell PRM for the 3D_Vertex_Component_Control enum:
       *    "Within a VERTEX_ELEMENT_STATE structure, if a Component
       *    Control field is set to something other than VFCOMP_STORE_SRC,
       *    no higher-numbered Component Control fields may be set to
       *    VFCOMP_STORE_SRC"
       *
       * This means, that if we have BaseInstance, we need BaseVertex as
       * well.  Just do all or nothing.
       */
      uint32_t base_ctrl = (vs_prog_data->uses_basevertex ||
                            vs_prog_data->uses_baseinstance) ?
                           VFCOMP_STORE_SRC : VFCOMP_STORE_0;

      struct GENX(VERTEX_ELEMENT_STATE) element = {
         .VertexBufferIndex = 32, /* Reserved for this */
         .Valid = true,
         .SourceElementFormat = ISL_FORMAT_R32G32_UINT,
         .Component0Control = base_ctrl,
         .Component1Control = base_ctrl,
#if GEN_GEN >= 8
         .Component2Control = VFCOMP_STORE_0,
         .Component3Control = VFCOMP_STORE_0,
#else
         .Component2Control = VFCOMP_STORE_VID,
         .Component3Control = VFCOMP_STORE_IID,
#endif
      };
      GENX(VERTEX_ELEMENT_STATE_pack)(NULL, &p[1 + id_slot * 2], &element);
   }

#if GEN_GEN >= 8
   anv_batch_emit(&pipeline->batch, GENX(3DSTATE_VF_SGVS), sgvs) {
      sgvs.VertexIDEnable              = vs_prog_data->uses_vertexid;
      sgvs.VertexIDComponentNumber     = 2;
      sgvs.VertexIDElementOffset       = id_slot;
      sgvs.InstanceIDEnable            = vs_prog_data->uses_instanceid;
      sgvs.InstanceIDComponentNumber   = 3;
      sgvs.InstanceIDElementOffset     = id_slot;
   }
#endif
}

void
genX(emit_urb_setup)(struct anv_device *device, struct anv_batch *batch,
                     VkShaderStageFlags active_stages,
                     unsigned vs_size, unsigned gs_size,
                     const struct gen_l3_config *l3_config)
{
   if (!(active_stages & VK_SHADER_STAGE_VERTEX_BIT))
      vs_size = 1;

   if (!(active_stages & VK_SHADER_STAGE_GEOMETRY_BIT))
      gs_size = 1;

   unsigned vs_entry_size_bytes = vs_size * 64;
   unsigned gs_entry_size_bytes = gs_size * 64;

   /* From p35 of the Ivy Bridge PRM (section 1.7.1: 3DSTATE_URB_GS):
    *
    *     VS Number of URB Entries must be divisible by 8 if the VS URB Entry
    *     Allocation Size is less than 9 512-bit URB entries.
    *
    * Similar text exists for GS.
    */
   unsigned vs_granularity = (vs_size < 9) ? 8 : 1;
   unsigned gs_granularity = (gs_size < 9) ? 8 : 1;

   /* URB allocations must be done in 8k chunks. */
   unsigned chunk_size_bytes = 8192;

   /* Determine the size of the URB in chunks. */
   const unsigned total_urb_size =
      gen_get_l3_config_urb_size(&device->info, l3_config);
   const unsigned urb_chunks = total_urb_size * 1024 / chunk_size_bytes;

   /* Reserve space for push constants */
   unsigned push_constant_kb;
   if (device->info.gen >= 8)
      push_constant_kb = 32;
   else if (device->info.is_haswell)
      push_constant_kb = device->info.gt == 3 ? 32 : 16;
   else
      push_constant_kb = 16;

   unsigned push_constant_bytes = push_constant_kb * 1024;
   unsigned push_constant_chunks =
      push_constant_bytes / chunk_size_bytes;

   /* Initially, assign each stage the minimum amount of URB space it needs,
    * and make a note of how much additional space it "wants" (the amount of
    * additional space it could actually make use of).
    */

   /* VS has a lower limit on the number of URB entries */
   unsigned vs_chunks =
      ALIGN(device->info.urb.min_vs_entries * vs_entry_size_bytes,
            chunk_size_bytes) / chunk_size_bytes;
   unsigned vs_wants =
      ALIGN(device->info.urb.max_vs_entries * vs_entry_size_bytes,
            chunk_size_bytes) / chunk_size_bytes - vs_chunks;

   unsigned gs_chunks = 0;
   unsigned gs_wants = 0;
   if (active_stages & VK_SHADER_STAGE_GEOMETRY_BIT) {
      /* There are two constraints on the minimum amount of URB space we can
       * allocate:
       *
       * (1) We need room for at least 2 URB entries, since we always operate
       * the GS in DUAL_OBJECT mode.
       *
       * (2) We can't allocate less than nr_gs_entries_granularity.
       */
      gs_chunks = ALIGN(MAX2(gs_granularity, 2) * gs_entry_size_bytes,
                        chunk_size_bytes) / chunk_size_bytes;
      gs_wants =
         ALIGN(device->info.urb.max_gs_entries * gs_entry_size_bytes,
               chunk_size_bytes) / chunk_size_bytes - gs_chunks;
   }

   /* There should always be enough URB space to satisfy the minimum
    * requirements of each stage.
    */
   unsigned total_needs = push_constant_chunks + vs_chunks + gs_chunks;
   assert(total_needs <= urb_chunks);

   /* Mete out remaining space (if any) in proportion to "wants". */
   unsigned total_wants = vs_wants + gs_wants;
   unsigned remaining_space = urb_chunks - total_needs;
   if (remaining_space > total_wants)
      remaining_space = total_wants;
   if (remaining_space > 0) {
      unsigned vs_additional = (unsigned)
         round(vs_wants * (((double) remaining_space) / total_wants));
      vs_chunks += vs_additional;
      remaining_space -= vs_additional;
      gs_chunks += remaining_space;
   }

   /* Sanity check that we haven't over-allocated. */
   assert(push_constant_chunks + vs_chunks + gs_chunks <= urb_chunks);

   /* Finally, compute the number of entries that can fit in the space
    * allocated to each stage.
    */
   unsigned nr_vs_entries = vs_chunks * chunk_size_bytes / vs_entry_size_bytes;
   unsigned nr_gs_entries = gs_chunks * chunk_size_bytes / gs_entry_size_bytes;

   /* Since we rounded up when computing *_wants, this may be slightly more
    * than the maximum allowed amount, so correct for that.
    */
   nr_vs_entries = MIN2(nr_vs_entries, device->info.urb.max_vs_entries);
   nr_gs_entries = MIN2(nr_gs_entries, device->info.urb.max_gs_entries);

   /* Ensure that we program a multiple of the granularity. */
   nr_vs_entries = ROUND_DOWN_TO(nr_vs_entries, vs_granularity);
   nr_gs_entries = ROUND_DOWN_TO(nr_gs_entries, gs_granularity);

   /* Finally, sanity check to make sure we have at least the minimum number
    * of entries needed for each stage.
    */
   assert(nr_vs_entries >= device->info.urb.min_vs_entries);
   if (active_stages & VK_SHADER_STAGE_GEOMETRY_BIT)
      assert(nr_gs_entries >= 2);

#if GEN_GEN == 7 && !GEN_IS_HASWELL
   /* From the IVB PRM Vol. 2, Part 1, Section 3.2.1:
    *
    *    "A PIPE_CONTROL with Post-Sync Operation set to 1h and a depth stall
    *    needs to be sent just prior to any 3DSTATE_VS, 3DSTATE_URB_VS,
    *    3DSTATE_CONSTANT_VS, 3DSTATE_BINDING_TABLE_POINTER_VS,
    *    3DSTATE_SAMPLER_STATE_POINTER_VS command.  Only one PIPE_CONTROL
    *    needs to be sent before any combination of VS associated 3DSTATE."
    */
   anv_batch_emit(batch, GEN7_PIPE_CONTROL, pc) {
      pc.DepthStallEnable  = true;
      pc.PostSyncOperation = WriteImmediateData;
      pc.Address           = (struct anv_address) { &device->workaround_bo, 0 };
   }
#endif

   /* Lay out the URB in the following order:
    * - push constants
    * - VS
    * - GS
    */
   anv_batch_emit(batch, GENX(3DSTATE_URB_VS), urb) {
      urb.VSURBStartingAddress      = push_constant_chunks;
      urb.VSURBEntryAllocationSize  = vs_size - 1;
      urb.VSNumberofURBEntries      = nr_vs_entries;
   }

   anv_batch_emit(batch, GENX(3DSTATE_URB_HS), urb) {
      urb.HSURBStartingAddress      = push_constant_chunks;
   }

   anv_batch_emit(batch, GENX(3DSTATE_URB_DS), urb) {
      urb.DSURBStartingAddress      = push_constant_chunks;
   }

   anv_batch_emit(batch, GENX(3DSTATE_URB_GS), urb) {
      urb.GSURBStartingAddress      = push_constant_chunks + vs_chunks;
      urb.GSURBEntryAllocationSize  = gs_size - 1;
      urb.GSNumberofURBEntries      = nr_gs_entries;
   }
}

static inline void
emit_urb_setup(struct anv_pipeline *pipeline)
{
   unsigned vs_entry_size =
      (pipeline->active_stages & VK_SHADER_STAGE_VERTEX_BIT) ?
      get_vs_prog_data(pipeline)->base.urb_entry_size : 0;
   unsigned gs_entry_size =
      (pipeline->active_stages & VK_SHADER_STAGE_GEOMETRY_BIT) ?
      get_gs_prog_data(pipeline)->base.urb_entry_size : 0;

   genX(emit_urb_setup)(pipeline->device, &pipeline->batch,
                        pipeline->active_stages, vs_entry_size, gs_entry_size,
                        pipeline->urb.l3_config);
}

static void
emit_3dstate_sbe(struct anv_pipeline *pipeline)
{
   const struct brw_vs_prog_data *vs_prog_data = get_vs_prog_data(pipeline);
   const struct brw_gs_prog_data *gs_prog_data = get_gs_prog_data(pipeline);
   const struct brw_wm_prog_data *wm_prog_data = get_wm_prog_data(pipeline);
   const struct brw_vue_map *fs_input_map;

   if (pipeline->gs_kernel == NO_KERNEL)
      fs_input_map = &vs_prog_data->base.vue_map;
   else
      fs_input_map = &gs_prog_data->base.vue_map;

   struct GENX(3DSTATE_SBE) sbe = {
      GENX(3DSTATE_SBE_header),
      .AttributeSwizzleEnable = true,
      .PointSpriteTextureCoordinateOrigin = UPPERLEFT,
      .NumberofSFOutputAttributes = wm_prog_data->num_varying_inputs,
      .ConstantInterpolationEnable = wm_prog_data->flat_inputs,
   };

#if GEN_GEN >= 9
   for (unsigned i = 0; i < 32; i++)
      sbe.AttributeActiveComponentFormat[i] = ACF_XYZW;
#endif

#if GEN_GEN >= 8
   /* On Broadwell, they broke 3DSTATE_SBE into two packets */
   struct GENX(3DSTATE_SBE_SWIZ) swiz = {
      GENX(3DSTATE_SBE_SWIZ_header),
   };
#else
#  define swiz sbe
#endif

   int max_source_attr = 0;
   for (int attr = 0; attr < VARYING_SLOT_MAX; attr++) {
      int input_index = wm_prog_data->urb_setup[attr];

      if (input_index < 0)
         continue;

      if (attr == VARYING_SLOT_PNTC) {
         sbe.PointSpriteTextureCoordinateEnable = 1 << input_index;
         continue;
      }

      const int slot = fs_input_map->varying_to_slot[attr];

      if (input_index >= 16)
         continue;

      if (slot == -1) {
         /* This attribute does not exist in the VUE--that means that the
          * vertex shader did not write to it.  It could be that it's a
          * regular varying read by the fragment shader but not written by
          * the vertex shader or it's gl_PrimitiveID. In the first case the
          * value is undefined, in the second it needs to be
          * gl_PrimitiveID.
          */
         swiz.Attribute[input_index].ConstantSource = PRIM_ID;
         swiz.Attribute[input_index].ComponentOverrideX = true;
         swiz.Attribute[input_index].ComponentOverrideY = true;
         swiz.Attribute[input_index].ComponentOverrideZ = true;
         swiz.Attribute[input_index].ComponentOverrideW = true;
      } else {
         assert(slot >= 2);
         const int source_attr = slot - 2;
         max_source_attr = MAX2(max_source_attr, source_attr);
         /* We have to subtract two slots to accout for the URB entry output
          * read offset in the VS and GS stages.
          */
         swiz.Attribute[input_index].SourceAttribute = source_attr;
      }
   }

   sbe.VertexURBEntryReadOffset = 1; /* Skip the VUE header and position slots */
   sbe.VertexURBEntryReadLength = DIV_ROUND_UP(max_source_attr + 1, 2);

   uint32_t *dw = anv_batch_emit_dwords(&pipeline->batch,
                                        GENX(3DSTATE_SBE_length));
   GENX(3DSTATE_SBE_pack)(&pipeline->batch, dw, &sbe);

#if GEN_GEN >= 8
   dw = anv_batch_emit_dwords(&pipeline->batch, GENX(3DSTATE_SBE_SWIZ_length));
   GENX(3DSTATE_SBE_SWIZ_pack)(&pipeline->batch, dw, &swiz);
#endif
}

static inline uint32_t
scratch_space(const struct brw_stage_prog_data *prog_data)
{
   return ffs(prog_data->total_scratch / 2048);
}

static const uint32_t vk_to_gen_cullmode[] = {
   [VK_CULL_MODE_NONE]                       = CULLMODE_NONE,
   [VK_CULL_MODE_FRONT_BIT]                  = CULLMODE_FRONT,
   [VK_CULL_MODE_BACK_BIT]                   = CULLMODE_BACK,
   [VK_CULL_MODE_FRONT_AND_BACK]             = CULLMODE_BOTH
};

static const uint32_t vk_to_gen_fillmode[] = {
   [VK_POLYGON_MODE_FILL]                    = FILL_MODE_SOLID,
   [VK_POLYGON_MODE_LINE]                    = FILL_MODE_WIREFRAME,
   [VK_POLYGON_MODE_POINT]                   = FILL_MODE_POINT,
};

static const uint32_t vk_to_gen_front_face[] = {
   [VK_FRONT_FACE_COUNTER_CLOCKWISE]         = 1,
   [VK_FRONT_FACE_CLOCKWISE]                 = 0
};

static void
emit_rs_state(struct anv_pipeline *pipeline,
              const VkPipelineRasterizationStateCreateInfo *rs_info,
              const VkPipelineMultisampleStateCreateInfo *ms_info,
              const struct anv_render_pass *pass,
              const struct anv_subpass *subpass)
{
   struct GENX(3DSTATE_SF) sf = {
      GENX(3DSTATE_SF_header),
   };

   sf.ViewportTransformEnable = true;
   sf.StatisticsEnable = true;
   sf.TriangleStripListProvokingVertexSelect = 0;
   sf.LineStripListProvokingVertexSelect = 0;
   sf.TriangleFanProvokingVertexSelect = 1;
   sf.PointWidthSource = Vertex;
   sf.PointWidth = 1.0;

#if GEN_GEN >= 8
   struct GENX(3DSTATE_RASTER) raster = {
      GENX(3DSTATE_RASTER_header),
   };
#else
#  define raster sf
#endif

   /* For details on 3DSTATE_RASTER multisample state, see the BSpec table
    * "Multisample Modes State".
    */
#if GEN_GEN >= 8
   raster.DXMultisampleRasterizationEnable = true;
   raster.ForcedSampleCount = FSC_NUMRASTSAMPLES_0;
   raster.ForceMultisampling = false;
#else
   raster.MultisampleRasterizationMode =
      (ms_info && ms_info->rasterizationSamples > 1) ?
      MSRASTMODE_ON_PATTERN : MSRASTMODE_OFF_PIXEL;
#endif

   raster.FrontWinding = vk_to_gen_front_face[rs_info->frontFace];
   raster.CullMode = vk_to_gen_cullmode[rs_info->cullMode];
   raster.FrontFaceFillMode = vk_to_gen_fillmode[rs_info->polygonMode];
   raster.BackFaceFillMode = vk_to_gen_fillmode[rs_info->polygonMode];
   raster.ScissorRectangleEnable = true;

#if GEN_GEN >= 9
   /* GEN9+ splits ViewportZClipTestEnable into near and far enable bits */
   raster.ViewportZFarClipTestEnable = !pipeline->depth_clamp_enable;
   raster.ViewportZNearClipTestEnable = !pipeline->depth_clamp_enable;
#elif GEN_GEN >= 8
   raster.ViewportZClipTestEnable = !pipeline->depth_clamp_enable;
#endif

   raster.GlobalDepthOffsetEnableSolid = rs_info->depthBiasEnable;
   raster.GlobalDepthOffsetEnableWireframe = rs_info->depthBiasEnable;
   raster.GlobalDepthOffsetEnablePoint = rs_info->depthBiasEnable;

#if GEN_GEN == 7
   /* Gen7 requires that we provide the depth format in 3DSTATE_SF so that it
    * can get the depth offsets correct.
    */
   if (subpass->depth_stencil_attachment < pass->attachment_count) {
      VkFormat vk_format =
         pass->attachments[subpass->depth_stencil_attachment].format;
      assert(vk_format_is_depth_or_stencil(vk_format));
      if (vk_format_aspects(vk_format) & VK_IMAGE_ASPECT_DEPTH_BIT) {
         enum isl_format isl_format =
            anv_get_isl_format(&pipeline->device->info, vk_format,
                               VK_IMAGE_ASPECT_DEPTH_BIT,
                               VK_IMAGE_TILING_OPTIMAL);
         sf.DepthBufferSurfaceFormat =
            isl_format_get_depth_format(isl_format, false);
      }
   }
#endif

#if GEN_GEN >= 8
   GENX(3DSTATE_SF_pack)(NULL, pipeline->gen8.sf, &sf);
   GENX(3DSTATE_RASTER_pack)(NULL, pipeline->gen8.raster, &raster);
#else
#  undef raster
   GENX(3DSTATE_SF_pack)(NULL, &pipeline->gen7.sf, &sf);
#endif
}

static void
emit_ms_state(struct anv_pipeline *pipeline,
              const VkPipelineMultisampleStateCreateInfo *info)
{
   uint32_t samples = 1;
   uint32_t log2_samples = 0;

   /* From the Vulkan 1.0 spec:
    *    If pSampleMask is NULL, it is treated as if the mask has all bits
    *    enabled, i.e. no coverage is removed from fragments.
    *
    * 3DSTATE_SAMPLE_MASK.SampleMask is 16 bits.
    */
#if GEN_GEN >= 8
   uint32_t sample_mask = 0xffff;
#else
   uint32_t sample_mask = 0xff;
#endif

   if (info) {
      samples = info->rasterizationSamples;
      log2_samples = __builtin_ffs(samples) - 1;
   }

   if (info && info->pSampleMask)
      sample_mask &= info->pSampleMask[0];

   anv_batch_emit(&pipeline->batch, GENX(3DSTATE_MULTISAMPLE), ms) {
      ms.NumberofMultisamples       = log2_samples;

#if GEN_GEN >= 8
      /* The PRM says that this bit is valid only for DX9:
       *
       *    SW can choose to set this bit only for DX9 API. DX10/OGL API's
       *    should not have any effect by setting or not setting this bit.
       */
      ms.PixelPositionOffsetEnable  = false;
      ms.PixelLocation              = CENTER;
#else
      ms.PixelLocation              = PIXLOC_CENTER;

      switch (samples) {
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
#endif
   }

   anv_batch_emit(&pipeline->batch, GENX(3DSTATE_SAMPLE_MASK), sm) {
      sm.SampleMask = sample_mask;
   }
}

static const uint32_t vk_to_gen_logic_op[] = {
   [VK_LOGIC_OP_COPY]                        = LOGICOP_COPY,
   [VK_LOGIC_OP_CLEAR]                       = LOGICOP_CLEAR,
   [VK_LOGIC_OP_AND]                         = LOGICOP_AND,
   [VK_LOGIC_OP_AND_REVERSE]                 = LOGICOP_AND_REVERSE,
   [VK_LOGIC_OP_AND_INVERTED]                = LOGICOP_AND_INVERTED,
   [VK_LOGIC_OP_NO_OP]                       = LOGICOP_NOOP,
   [VK_LOGIC_OP_XOR]                         = LOGICOP_XOR,
   [VK_LOGIC_OP_OR]                          = LOGICOP_OR,
   [VK_LOGIC_OP_NOR]                         = LOGICOP_NOR,
   [VK_LOGIC_OP_EQUIVALENT]                  = LOGICOP_EQUIV,
   [VK_LOGIC_OP_INVERT]                      = LOGICOP_INVERT,
   [VK_LOGIC_OP_OR_REVERSE]                  = LOGICOP_OR_REVERSE,
   [VK_LOGIC_OP_COPY_INVERTED]               = LOGICOP_COPY_INVERTED,
   [VK_LOGIC_OP_OR_INVERTED]                 = LOGICOP_OR_INVERTED,
   [VK_LOGIC_OP_NAND]                        = LOGICOP_NAND,
   [VK_LOGIC_OP_SET]                         = LOGICOP_SET,
};

static const uint32_t vk_to_gen_blend[] = {
   [VK_BLEND_FACTOR_ZERO]                    = BLENDFACTOR_ZERO,
   [VK_BLEND_FACTOR_ONE]                     = BLENDFACTOR_ONE,
   [VK_BLEND_FACTOR_SRC_COLOR]               = BLENDFACTOR_SRC_COLOR,
   [VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR]     = BLENDFACTOR_INV_SRC_COLOR,
   [VK_BLEND_FACTOR_DST_COLOR]               = BLENDFACTOR_DST_COLOR,
   [VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR]     = BLENDFACTOR_INV_DST_COLOR,
   [VK_BLEND_FACTOR_SRC_ALPHA]               = BLENDFACTOR_SRC_ALPHA,
   [VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA]     = BLENDFACTOR_INV_SRC_ALPHA,
   [VK_BLEND_FACTOR_DST_ALPHA]               = BLENDFACTOR_DST_ALPHA,
   [VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA]     = BLENDFACTOR_INV_DST_ALPHA,
   [VK_BLEND_FACTOR_CONSTANT_COLOR]          = BLENDFACTOR_CONST_COLOR,
   [VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR]= BLENDFACTOR_INV_CONST_COLOR,
   [VK_BLEND_FACTOR_CONSTANT_ALPHA]          = BLENDFACTOR_CONST_ALPHA,
   [VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA]= BLENDFACTOR_INV_CONST_ALPHA,
   [VK_BLEND_FACTOR_SRC_ALPHA_SATURATE]      = BLENDFACTOR_SRC_ALPHA_SATURATE,
   [VK_BLEND_FACTOR_SRC1_COLOR]              = BLENDFACTOR_SRC1_COLOR,
   [VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR]    = BLENDFACTOR_INV_SRC1_COLOR,
   [VK_BLEND_FACTOR_SRC1_ALPHA]              = BLENDFACTOR_SRC1_ALPHA,
   [VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA]    = BLENDFACTOR_INV_SRC1_ALPHA,
};

static const uint32_t vk_to_gen_blend_op[] = {
   [VK_BLEND_OP_ADD]                         = BLENDFUNCTION_ADD,
   [VK_BLEND_OP_SUBTRACT]                    = BLENDFUNCTION_SUBTRACT,
   [VK_BLEND_OP_REVERSE_SUBTRACT]            = BLENDFUNCTION_REVERSE_SUBTRACT,
   [VK_BLEND_OP_MIN]                         = BLENDFUNCTION_MIN,
   [VK_BLEND_OP_MAX]                         = BLENDFUNCTION_MAX,
};

static const uint32_t vk_to_gen_compare_op[] = {
   [VK_COMPARE_OP_NEVER]                        = PREFILTEROPNEVER,
   [VK_COMPARE_OP_LESS]                         = PREFILTEROPLESS,
   [VK_COMPARE_OP_EQUAL]                        = PREFILTEROPEQUAL,
   [VK_COMPARE_OP_LESS_OR_EQUAL]                = PREFILTEROPLEQUAL,
   [VK_COMPARE_OP_GREATER]                      = PREFILTEROPGREATER,
   [VK_COMPARE_OP_NOT_EQUAL]                    = PREFILTEROPNOTEQUAL,
   [VK_COMPARE_OP_GREATER_OR_EQUAL]             = PREFILTEROPGEQUAL,
   [VK_COMPARE_OP_ALWAYS]                       = PREFILTEROPALWAYS,
};

static const uint32_t vk_to_gen_stencil_op[] = {
   [VK_STENCIL_OP_KEEP]                         = STENCILOP_KEEP,
   [VK_STENCIL_OP_ZERO]                         = STENCILOP_ZERO,
   [VK_STENCIL_OP_REPLACE]                      = STENCILOP_REPLACE,
   [VK_STENCIL_OP_INCREMENT_AND_CLAMP]          = STENCILOP_INCRSAT,
   [VK_STENCIL_OP_DECREMENT_AND_CLAMP]          = STENCILOP_DECRSAT,
   [VK_STENCIL_OP_INVERT]                       = STENCILOP_INVERT,
   [VK_STENCIL_OP_INCREMENT_AND_WRAP]           = STENCILOP_INCR,
   [VK_STENCIL_OP_DECREMENT_AND_WRAP]           = STENCILOP_DECR,
};

static void
emit_ds_state(struct anv_pipeline *pipeline,
              const VkPipelineDepthStencilStateCreateInfo *info,
              const struct anv_render_pass *pass,
              const struct anv_subpass *subpass)
{
#if GEN_GEN == 7
#  define depth_stencil_dw pipeline->gen7.depth_stencil_state
#elif GEN_GEN == 8
#  define depth_stencil_dw pipeline->gen8.wm_depth_stencil
#else
#  define depth_stencil_dw pipeline->gen9.wm_depth_stencil
#endif

   if (info == NULL) {
      /* We're going to OR this together with the dynamic state.  We need
       * to make sure it's initialized to something useful.
       */
      memset(depth_stencil_dw, 0, sizeof(depth_stencil_dw));
      return;
   }

   /* VkBool32 depthBoundsTestEnable; // optional (depth_bounds_test) */

#if GEN_GEN <= 7
   struct GENX(DEPTH_STENCIL_STATE) depth_stencil = {
#else
   struct GENX(3DSTATE_WM_DEPTH_STENCIL) depth_stencil = {
#endif
      .DepthTestEnable = info->depthTestEnable,
      .DepthBufferWriteEnable = info->depthWriteEnable,
      .DepthTestFunction = vk_to_gen_compare_op[info->depthCompareOp],
      .DoubleSidedStencilEnable = true,

      .StencilTestEnable = info->stencilTestEnable,
      .StencilBufferWriteEnable = info->stencilTestEnable,
      .StencilFailOp = vk_to_gen_stencil_op[info->front.failOp],
      .StencilPassDepthPassOp = vk_to_gen_stencil_op[info->front.passOp],
      .StencilPassDepthFailOp = vk_to_gen_stencil_op[info->front.depthFailOp],
      .StencilTestFunction = vk_to_gen_compare_op[info->front.compareOp],
      .BackfaceStencilFailOp = vk_to_gen_stencil_op[info->back.failOp],
      .BackfaceStencilPassDepthPassOp = vk_to_gen_stencil_op[info->back.passOp],
      .BackfaceStencilPassDepthFailOp =vk_to_gen_stencil_op[info->back.depthFailOp],
      .BackfaceStencilTestFunction = vk_to_gen_compare_op[info->back.compareOp],
   };

   VkImageAspectFlags aspects = 0;
   if (subpass->depth_stencil_attachment != VK_ATTACHMENT_UNUSED) {
      VkFormat depth_stencil_format =
         pass->attachments[subpass->depth_stencil_attachment].format;
      aspects = vk_format_aspects(depth_stencil_format);
   }

   /* The Vulkan spec requires that if either depth or stencil is not present,
    * the pipeline is to act as if the test silently passes.
    */
   if (!(aspects & VK_IMAGE_ASPECT_DEPTH_BIT)) {
      depth_stencil.DepthBufferWriteEnable = false;
      depth_stencil.DepthTestFunction = PREFILTEROPALWAYS;
   }

   if (!(aspects & VK_IMAGE_ASPECT_STENCIL_BIT)) {
      depth_stencil.StencilBufferWriteEnable = false;
      depth_stencil.StencilTestFunction = PREFILTEROPALWAYS;
      depth_stencil.BackfaceStencilTestFunction = PREFILTEROPALWAYS;
   }

   /* From the Broadwell PRM:
    *
    *    "If Depth_Test_Enable = 1 AND Depth_Test_func = EQUAL, the
    *    Depth_Write_Enable must be set to 0."
    */
   if (info->depthTestEnable && info->depthCompareOp == VK_COMPARE_OP_EQUAL)
      depth_stencil.DepthBufferWriteEnable = false;

#if GEN_GEN <= 7
   GENX(DEPTH_STENCIL_STATE_pack)(NULL, depth_stencil_dw, &depth_stencil);
#else
   GENX(3DSTATE_WM_DEPTH_STENCIL_pack)(NULL, depth_stencil_dw, &depth_stencil);
#endif
}

static void
emit_cb_state(struct anv_pipeline *pipeline,
              const VkPipelineColorBlendStateCreateInfo *info,
              const VkPipelineMultisampleStateCreateInfo *ms_info)
{
   struct anv_device *device = pipeline->device;

   const uint32_t num_dwords = GENX(BLEND_STATE_length);
   pipeline->blend_state =
      anv_state_pool_alloc(&device->dynamic_state_pool, num_dwords * 4, 64);

   struct GENX(BLEND_STATE) blend_state = {
#if GEN_GEN >= 8
      .AlphaToCoverageEnable = ms_info && ms_info->alphaToCoverageEnable,
      .AlphaToOneEnable = ms_info && ms_info->alphaToOneEnable,
#else
      /* Make sure it gets zeroed */
      .Entry = { { 0, }, },
#endif
   };

   /* Default everything to disabled */
   for (uint32_t i = 0; i < 8; i++) {
      blend_state.Entry[i].WriteDisableAlpha = true;
      blend_state.Entry[i].WriteDisableRed = true;
      blend_state.Entry[i].WriteDisableGreen = true;
      blend_state.Entry[i].WriteDisableBlue = true;
   }

   uint32_t surface_count = 0;
   struct anv_pipeline_bind_map *map;
   if (anv_pipeline_has_stage(pipeline, MESA_SHADER_FRAGMENT)) {
      map = &pipeline->shaders[MESA_SHADER_FRAGMENT]->bind_map;
      surface_count = map->surface_count;
   }

   bool has_writeable_rt = false;
   for (unsigned i = 0; i < surface_count; i++) {
      struct anv_pipeline_binding *binding = &map->surface_to_descriptor[i];

      /* All color attachments are at the beginning of the binding table */
      if (binding->set != ANV_DESCRIPTOR_SET_COLOR_ATTACHMENTS)
         break;

      /* We can have at most 8 attachments */
      assert(i < 8);

      if (binding->index >= info->attachmentCount)
         continue;

      assert(binding->binding == 0);
      const VkPipelineColorBlendAttachmentState *a =
         &info->pAttachments[binding->index];

      blend_state.Entry[i] = (struct GENX(BLEND_STATE_ENTRY)) {
#if GEN_GEN < 8
         .AlphaToCoverageEnable = ms_info && ms_info->alphaToCoverageEnable,
         .AlphaToOneEnable = ms_info && ms_info->alphaToOneEnable,
#endif
         .LogicOpEnable = info->logicOpEnable,
         .LogicOpFunction = vk_to_gen_logic_op[info->logicOp],
         .ColorBufferBlendEnable = a->blendEnable,
         .ColorClampRange = COLORCLAMP_RTFORMAT,
         .PreBlendColorClampEnable = true,
         .PostBlendColorClampEnable = true,
         .SourceBlendFactor = vk_to_gen_blend[a->srcColorBlendFactor],
         .DestinationBlendFactor = vk_to_gen_blend[a->dstColorBlendFactor],
         .ColorBlendFunction = vk_to_gen_blend_op[a->colorBlendOp],
         .SourceAlphaBlendFactor = vk_to_gen_blend[a->srcAlphaBlendFactor],
         .DestinationAlphaBlendFactor = vk_to_gen_blend[a->dstAlphaBlendFactor],
         .AlphaBlendFunction = vk_to_gen_blend_op[a->alphaBlendOp],
         .WriteDisableAlpha = !(a->colorWriteMask & VK_COLOR_COMPONENT_A_BIT),
         .WriteDisableRed = !(a->colorWriteMask & VK_COLOR_COMPONENT_R_BIT),
         .WriteDisableGreen = !(a->colorWriteMask & VK_COLOR_COMPONENT_G_BIT),
         .WriteDisableBlue = !(a->colorWriteMask & VK_COLOR_COMPONENT_B_BIT),
      };

      if (a->srcColorBlendFactor != a->srcAlphaBlendFactor ||
          a->dstColorBlendFactor != a->dstAlphaBlendFactor ||
          a->colorBlendOp != a->alphaBlendOp) {
#if GEN_GEN >= 8
         blend_state.IndependentAlphaBlendEnable = true;
#else
         blend_state.Entry[i].IndependentAlphaBlendEnable = true;
#endif
      }

      if (a->colorWriteMask != 0)
         has_writeable_rt = true;

      /* Our hardware applies the blend factor prior to the blend function
       * regardless of what function is used.  Technically, this means the
       * hardware can do MORE than GL or Vulkan specify.  However, it also
       * means that, for MIN and MAX, we have to stomp the blend factor to
       * ONE to make it a no-op.
       */
      if (a->colorBlendOp == VK_BLEND_OP_MIN ||
          a->colorBlendOp == VK_BLEND_OP_MAX) {
         blend_state.Entry[i].SourceBlendFactor = BLENDFACTOR_ONE;
         blend_state.Entry[i].DestinationBlendFactor = BLENDFACTOR_ONE;
      }
      if (a->alphaBlendOp == VK_BLEND_OP_MIN ||
          a->alphaBlendOp == VK_BLEND_OP_MAX) {
         blend_state.Entry[i].SourceAlphaBlendFactor = BLENDFACTOR_ONE;
         blend_state.Entry[i].DestinationAlphaBlendFactor = BLENDFACTOR_ONE;
      }
   }

#if GEN_GEN >= 8
   struct GENX(BLEND_STATE_ENTRY) *bs0 = &blend_state.Entry[0];
   anv_batch_emit(&pipeline->batch, GENX(3DSTATE_PS_BLEND), blend) {
      blend.AlphaToCoverageEnable         = blend_state.AlphaToCoverageEnable;
      blend.HasWriteableRT                = has_writeable_rt;
      blend.ColorBufferBlendEnable        = bs0->ColorBufferBlendEnable;
      blend.SourceAlphaBlendFactor        = bs0->SourceAlphaBlendFactor;
      blend.DestinationAlphaBlendFactor   = bs0->DestinationAlphaBlendFactor;
      blend.SourceBlendFactor             = bs0->SourceBlendFactor;
      blend.DestinationBlendFactor        = bs0->DestinationBlendFactor;
      blend.AlphaTestEnable               = false;
      blend.IndependentAlphaBlendEnable   =
         blend_state.IndependentAlphaBlendEnable;
   }
#else
   (void)has_writeable_rt;
#endif

   GENX(BLEND_STATE_pack)(NULL, pipeline->blend_state.map, &blend_state);
   if (!device->info.has_llc)
      anv_state_clflush(pipeline->blend_state);

   anv_batch_emit(&pipeline->batch, GENX(3DSTATE_BLEND_STATE_POINTERS), bsp) {
      bsp.BlendStatePointer      = pipeline->blend_state.offset;
#if GEN_GEN >= 8
      bsp.BlendStatePointerValid = true;
#endif
   }
}

static void
emit_3dstate_clip(struct anv_pipeline *pipeline,
                  const VkPipelineViewportStateCreateInfo *vp_info,
                  const VkPipelineRasterizationStateCreateInfo *rs_info)
{
   const struct brw_wm_prog_data *wm_prog_data = get_wm_prog_data(pipeline);
   (void) wm_prog_data;
   anv_batch_emit(&pipeline->batch, GENX(3DSTATE_CLIP), clip) {
      clip.ClipEnable               = true;
      clip.EarlyCullEnable          = true;
      clip.APIMode                  = APIMODE_D3D,
      clip.ViewportXYClipTestEnable = true;

      clip.ClipMode = CLIPMODE_NORMAL;

      clip.TriangleStripListProvokingVertexSelect = 0;
      clip.LineStripListProvokingVertexSelect     = 0;
      clip.TriangleFanProvokingVertexSelect       = 1;

      clip.MinimumPointWidth = 0.125;
      clip.MaximumPointWidth = 255.875;
      clip.MaximumVPIndex    = (vp_info ? vp_info->viewportCount : 1) - 1;

#if GEN_GEN == 7
      clip.FrontWinding            = vk_to_gen_front_face[rs_info->frontFace];
      clip.CullMode                = vk_to_gen_cullmode[rs_info->cullMode];
      clip.ViewportZClipTestEnable = !pipeline->depth_clamp_enable;
#else
      clip.NonPerspectiveBarycentricEnable = wm_prog_data ?
         (wm_prog_data->barycentric_interp_modes & 0x38) != 0 : 0;
#endif
   }
}

static void
emit_3dstate_streamout(struct anv_pipeline *pipeline,
                       const VkPipelineRasterizationStateCreateInfo *rs_info)
{
   anv_batch_emit(&pipeline->batch, GENX(3DSTATE_STREAMOUT), so) {
      so.RenderingDisable = rs_info->rasterizerDiscardEnable;
   }
}

#endif /* GENX_PIPELINE_UTIL_H */
