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
                  const VkPipelineVertexInputStateCreateInfo *info,
                  const struct anv_graphics_pipeline_create_info *extra)
{
   const struct brw_vs_prog_data *vs_prog_data = get_vs_prog_data(pipeline);

   uint32_t elements;
   if (extra && extra->disable_vs) {
      /* If the VS is disabled, just assume the user knows what they're
       * doing and apply the layout blindly.  This can only come from
       * meta, so this *should* be safe.
       */
      elements = 0;
      for (uint32_t i = 0; i < info->vertexAttributeDescriptionCount; i++)
         elements |= (1 << info->pVertexAttributeDescriptions[i].location);
   } else {
      /* Pull inputs_read out of the VS prog data */
      uint64_t inputs_read = vs_prog_data->inputs_read;
      assert((inputs_read & ((1 << VERT_ATTRIB_GENERIC0) - 1)) == 0);
      elements = inputs_read >> VERT_ATTRIB_GENERIC0;
   }

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
         vfi.InstancingEnable = pipeline->instancing_enable[desc->binding],
         vfi.VertexElementIndex = slot,
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

static inline void
emit_urb_setup(struct anv_pipeline *pipeline)
{
#if GEN_GEN == 7 && !GEN_IS_HASWELL
   struct anv_device *device = pipeline->device;

   /* From the IVB PRM Vol. 2, Part 1, Section 3.2.1:
    *
    *    "A PIPE_CONTROL with Post-Sync Operation set to 1h and a depth stall
    *    needs to be sent just prior to any 3DSTATE_VS, 3DSTATE_URB_VS,
    *    3DSTATE_CONSTANT_VS, 3DSTATE_BINDING_TABLE_POINTER_VS,
    *    3DSTATE_SAMPLER_STATE_POINTER_VS command.  Only one PIPE_CONTROL
    *    needs to be sent before any combination of VS associated 3DSTATE."
    */
   anv_batch_emit(&pipeline->batch, GEN7_PIPE_CONTROL, pc) {
      pc.DepthStallEnable  = true;
      pc.PostSyncOperation = WriteImmediateData;
      pc.Address           = (struct anv_address) { &device->workaround_bo, 0 };
   }
#endif

   for (int i = MESA_SHADER_VERTEX; i <= MESA_SHADER_GEOMETRY; i++) {
      anv_batch_emit(&pipeline->batch, GENX(3DSTATE_URB_VS), urb) {
         urb._3DCommandSubOpcode       = 48 + i;
         urb.VSURBStartingAddress      = pipeline->urb.start[i];
         urb.VSURBEntryAllocationSize  = pipeline->urb.size[i] - 1;
         urb.VSNumberofURBEntries      = pipeline->urb.entries[i];
      }
   }
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

#if GEN_GEN >= 9
      .Attribute0ActiveComponentFormat = ACF_XYZW,
      .Attribute1ActiveComponentFormat = ACF_XYZW,
      .Attribute2ActiveComponentFormat = ACF_XYZW,
      .Attribute3ActiveComponentFormat = ACF_XYZW,
      .Attribute4ActiveComponentFormat = ACF_XYZW,
      .Attribute5ActiveComponentFormat = ACF_XYZW,
      .Attribute6ActiveComponentFormat = ACF_XYZW,
      .Attribute7ActiveComponentFormat = ACF_XYZW,
      .Attribute8ActiveComponentFormat = ACF_XYZW,
      .Attribute9ActiveComponentFormat = ACF_XYZW,
      .Attribute10ActiveComponentFormat = ACF_XYZW,
      .Attribute11ActiveComponentFormat = ACF_XYZW,
      .Attribute12ActiveComponentFormat = ACF_XYZW,
      .Attribute13ActiveComponentFormat = ACF_XYZW,
      .Attribute14ActiveComponentFormat = ACF_XYZW,
      .Attribute15ActiveComponentFormat = ACF_XYZW,
      /* wow, much field, very attribute */
      .Attribute16ActiveComponentFormat = ACF_XYZW,
      .Attribute17ActiveComponentFormat = ACF_XYZW,
      .Attribute18ActiveComponentFormat = ACF_XYZW,
      .Attribute19ActiveComponentFormat = ACF_XYZW,
      .Attribute20ActiveComponentFormat = ACF_XYZW,
      .Attribute21ActiveComponentFormat = ACF_XYZW,
      .Attribute22ActiveComponentFormat = ACF_XYZW,
      .Attribute23ActiveComponentFormat = ACF_XYZW,
      .Attribute24ActiveComponentFormat = ACF_XYZW,
      .Attribute25ActiveComponentFormat = ACF_XYZW,
      .Attribute26ActiveComponentFormat = ACF_XYZW,
      .Attribute27ActiveComponentFormat = ACF_XYZW,
      .Attribute28ActiveComponentFormat = ACF_XYZW,
      .Attribute29ActiveComponentFormat = ACF_XYZW,
      .Attribute28ActiveComponentFormat = ACF_XYZW,
      .Attribute29ActiveComponentFormat = ACF_XYZW,
      .Attribute30ActiveComponentFormat = ACF_XYZW,
#endif
   };

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
   if (pass->attachments == NULL) {
      /* This comes from meta.  Assume we have verything. */
      aspects = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
   } else if (subpass->depth_stencil_attachment != VK_ATTACHMENT_UNUSED) {
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
