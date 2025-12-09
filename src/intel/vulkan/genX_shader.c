/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_private.h"
#include "anv_shader.h"

#include "genxml/gen_macros.h"
#include "genxml/genX_pack.h"
#include "genxml/genX_rt_pack.h"

#include "common/intel_compute_slm.h"

#include "nir/nir_xfb_info.h"

#define device_needs_protected(device) \
   ((device)->vk.enabled_features.protectedMemory)

#define anv_gfx_pack(dest, cmd, name) \
   for (struct cmd name = (struct cmd) { __anv_cmd_header(cmd) },       \
           *_dst = (struct cmd *)dest;                                  \
        __builtin_expect(_dst != NULL, 1);                              \
        ({                                                              \
           assert(sizeof(dest) >= 4 * __anv_cmd_length(cmd));           \
           __anv_cmd_pack(cmd)(NULL, _dst, &name);                      \
           _dst = NULL;                                                 \
        }))

static uint32_t
get_sampler_count(const struct anv_shader *shader)
{
   uint32_t count_by_4 = DIV_ROUND_UP(shader->bind_map.sampler_count, 4);

   /* We can potentially have way more than 32 samplers and that's ok.
    * However, the 3DSTATE_XS packets only have 3 bits to specify how
    * many to pre-fetch and all values above 4 are marked reserved.
    */
   return MIN2(count_by_4, 4);
}

static UNUSED struct anv_address
get_scratch_address(struct anv_device *device, struct anv_shader *shader)
{
   return (struct anv_address) {
      .bo = anv_scratch_pool_alloc(device, &device->scratch_pool,
                                   shader->vk.stage,
                                   shader->prog_data->total_scratch),
      .offset = 0,
   };
}

static UNUSED uint32_t
get_scratch_space(const struct anv_shader *shader)
{
   return ffs(shader->prog_data->total_scratch / 2048);
}

static UNUSED uint32_t
get_scratch_surf(struct anv_batch *batch,
                 struct anv_device *device,
                 struct anv_shader *shader,
                 bool protected)
{
   if (shader->prog_data->total_scratch == 0)
      return 0;

   struct anv_scratch_pool *pool = protected ?
      &device->protected_scratch_pool : &device->scratch_pool;
   struct anv_bo *bo =
      anv_scratch_pool_alloc(device, pool, shader->vk.stage,
                             shader->prog_data->total_scratch);
   anv_reloc_list_add_bo(batch->relocs, bo);
   return anv_scratch_pool_get_surf(
      device, pool, shader->prog_data->total_scratch) >>
      ANV_SCRATCH_SPACE_SHIFT(GFX_VER);
}

/* Streamout (can be used by several shaders) */
static void
emit_3dstate_streamout(struct anv_batch *batch,
                       struct anv_device *device,
                       struct anv_shader *shader)
{
   if (shader->xfb_info == NULL) {
      anv_shader_emit(batch, shader, so, GENX(3DSTATE_STREAMOUT), so);
      return;
   }

   const struct nir_xfb_info *xfb_info = shader->xfb_info;
   const struct intel_vue_map *vue_map =
      &brw_vue_prog_data_const(shader->prog_data)->vue_map;

   struct GENX(SO_DECL) so_decl[MAX_XFB_STREAMS][128];
   int next_offset[MAX_XFB_BUFFERS] = {0, 0, 0, 0};
   int decls[MAX_XFB_STREAMS] = {0, 0, 0, 0};

   memset(so_decl, 0, sizeof(so_decl));

   for (unsigned i = 0; i < xfb_info->output_count; i++) {
      const nir_xfb_output_info *output = &xfb_info->outputs[i];
      unsigned buffer = output->buffer;
      unsigned stream = xfb_info->buffer_to_stream[buffer];

      /* Our hardware is unusual in that it requires us to program SO_DECLs
       * for fake "hole" components, rather than simply taking the offset for
       * each real varying. Each hole can have size 1, 2, 3, or 4; we program
       * as many size = 4 holes as we can, then a final hole to accommodate
       * the final 1, 2, or 3 remaining.
       */
      int hole_dwords = (output->offset - next_offset[buffer]) / 4;
      while (hole_dwords > 0) {
         so_decl[stream][decls[stream]++] = (struct GENX(SO_DECL)) {
            .HoleFlag = 1,
            .OutputBufferSlot = buffer,
            .ComponentMask = (1 << MIN2(hole_dwords, 4)) - 1,
         };
         hole_dwords -= 4;
      }

      int varying = output->location;
      uint8_t component_mask = output->component_mask;
      /* VARYING_SLOT_PSIZ contains four scalar fields packed together:
       * - VARYING_SLOT_PRIMITIVE_SHADING_RATE in VARYING_SLOT_PSIZ.x
       * - VARYING_SLOT_LAYER                  in VARYING_SLOT_PSIZ.y
       * - VARYING_SLOT_VIEWPORT               in VARYING_SLOT_PSIZ.z
       * - VARYING_SLOT_PSIZ                   in VARYING_SLOT_PSIZ.w
       */
      if (varying == VARYING_SLOT_PRIMITIVE_SHADING_RATE) {
         varying = VARYING_SLOT_PSIZ;
         component_mask = 1 << 0; // SO_DECL_COMPMASK_X
      } else if (varying == VARYING_SLOT_LAYER) {
         varying = VARYING_SLOT_PSIZ;
         component_mask = 1 << 1; // SO_DECL_COMPMASK_Y
      } else if (varying == VARYING_SLOT_VIEWPORT) {
         varying = VARYING_SLOT_PSIZ;
         component_mask = 1 << 2; // SO_DECL_COMPMASK_Z
      } else if (varying == VARYING_SLOT_PSIZ) {
         component_mask = 1 << 3; // SO_DECL_COMPMASK_W
      }

      next_offset[buffer] = output->offset +
         __builtin_popcount(component_mask) * 4;

      const int slot = vue_map->varying_to_slot[varying];
      if (slot < 0) {
         /* This can happen if the shader never writes to the varying. Insert
          * a hole instead of actual varying data.
          */
         so_decl[stream][decls[stream]++] = (struct GENX(SO_DECL)) {
            .HoleFlag = true,
            .OutputBufferSlot = buffer,
            .ComponentMask = component_mask,
         };
      } else {
         so_decl[stream][decls[stream]++] = (struct GENX(SO_DECL)) {
            .OutputBufferSlot = buffer,
            .RegisterIndex = slot,
            .ComponentMask = component_mask,
         };
      }
   }

   int max_decls = 0;
   for (unsigned s = 0; s < MAX_XFB_STREAMS; s++)
      max_decls = MAX2(max_decls, decls[s]);

   uint8_t sbs[MAX_XFB_STREAMS] = { };
   for (unsigned b = 0; b < MAX_XFB_BUFFERS; b++) {
      if (xfb_info->buffers_written & (1 << b))
         sbs[xfb_info->buffer_to_stream[b]] |= 1 << b;
   }

   uint32_t *dw = anv_shader_emitn(batch, shader, so_decl_list,
                                   3 + 2 * max_decls,
                                   GENX(3DSTATE_SO_DECL_LIST),
                                   .StreamtoBufferSelects0 = sbs[0],
                                   .StreamtoBufferSelects1 = sbs[1],
                                   .StreamtoBufferSelects2 = sbs[2],
                                   .StreamtoBufferSelects3 = sbs[3],
                                   .NumEntries0 = decls[0],
                                   .NumEntries1 = decls[1],
                                   .NumEntries2 = decls[2],
                                   .NumEntries3 = decls[3]);

   for (int i = 0; i < max_decls; i++) {
      GENX(SO_DECL_ENTRY_pack)(NULL, dw + 3 + i * 2,
                               &(struct GENX(SO_DECL_ENTRY)) {
                                  .Stream0Decl = so_decl[0][i],
                                  .Stream1Decl = so_decl[1][i],
                                  .Stream2Decl = so_decl[2][i],
                                  .Stream3Decl = so_decl[3][i],
                               });
   }

   anv_shader_emit(batch, shader, so, GENX(3DSTATE_STREAMOUT), so) {
      so.SOFunctionEnable = true;
      so.SOStatisticsEnable = true;

      so.Buffer0SurfacePitch = xfb_info->buffers[0].stride;
      so.Buffer1SurfacePitch = xfb_info->buffers[1].stride;
      so.Buffer2SurfacePitch = xfb_info->buffers[2].stride;
      so.Buffer3SurfacePitch = xfb_info->buffers[3].stride;

      int urb_entry_read_offset = 0;
      int urb_entry_read_length =
         (vue_map->num_slots + 1) / 2 - urb_entry_read_offset;

      /* We always read the whole vertex. This could be reduced at some point
       * by reading less and offsetting the register index in the SO_DECLs.
       */
      so.Stream0VertexReadOffset = urb_entry_read_offset;
      so.Stream0VertexReadLength = urb_entry_read_length - 1;
      so.Stream1VertexReadOffset = urb_entry_read_offset;
      so.Stream1VertexReadLength = urb_entry_read_length - 1;
      so.Stream2VertexReadOffset = urb_entry_read_offset;
      so.Stream2VertexReadLength = urb_entry_read_length - 1;
      so.Stream3VertexReadOffset = urb_entry_read_offset;
      so.Stream3VertexReadLength = urb_entry_read_length - 1;
   }
}

/* Stage specific packing */

static uint32_t
get_vs_input_elements(const struct brw_vs_prog_data *vs_prog_data)
{
   /* Pull inputs_read out of the VS prog data */
   const uint64_t inputs_read = vs_prog_data->inputs_read;
   const uint64_t double_inputs_read =
      vs_prog_data->double_inputs_read & inputs_read;
   assert((inputs_read & ((1 << VERT_ATTRIB_GENERIC0) - 1)) == 0);
   const uint32_t elements = inputs_read >> VERT_ATTRIB_GENERIC0;
   const uint32_t elements_double = double_inputs_read >> VERT_ATTRIB_GENERIC0;

   return __builtin_popcount(elements) -
          __builtin_popcount(elements_double) / 2;
}

static uint32_t
vertex_element_comp_control(enum isl_format format, unsigned comp)
{
   uint8_t bits;
   switch (comp) {
   case 0: bits = isl_format_layouts[format].channels.r.bits; break;
   case 1: bits = isl_format_layouts[format].channels.g.bits; break;
   case 2: bits = isl_format_layouts[format].channels.b.bits; break;
   case 3: bits = isl_format_layouts[format].channels.a.bits; break;
   default: UNREACHABLE("Invalid component");
   }

   /*
    * Take in account hardware restrictions when dealing with 64-bit floats.
    *
    * From Broadwell spec, command reference structures, page 586:
    *  "When SourceElementFormat is set to one of the *64*_PASSTHRU formats,
    *   64-bit components are stored * in the URB without any conversion. In
    *   this case, vertex elements must be written as 128 or 256 bits, with
    *   VFCOMP_STORE_0 being used to pad the output as required. E.g., if
    *   R64_PASSTHRU is used to copy a 64-bit Red component into the URB,
    *   Component 1 must be specified as VFCOMP_STORE_0 (with Components 2,3
    *   set to VFCOMP_NOSTORE) in order to output a 128-bit vertex element, or
    *   Components 1-3 must be specified as VFCOMP_STORE_0 in order to output
    *   a 256-bit vertex element. Likewise, use of R64G64B64_PASSTHRU requires
    *   Component 3 to be specified as VFCOMP_STORE_0 in order to output a
    *   256-bit vertex element."
    */
   if (bits) {
      return VFCOMP_STORE_SRC;
   } else if (comp >= 2 &&
              !isl_format_layouts[format].channels.b.bits &&
              isl_format_layouts[format].channels.r.type == ISL_RAW) {
      /* When emitting 64-bit attributes, we need to write either 128 or 256
       * bit chunks, using VFCOMP_NOSTORE when not writing the chunk, and
       * VFCOMP_STORE_0 to pad the written chunk */
      return VFCOMP_NOSTORE;
   } else if (comp < 3 ||
              isl_format_layouts[format].channels.r.type == ISL_RAW) {
      /* Note we need to pad with value 0, not 1, due hardware restrictions
       * (see comment above) */
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
emit_ves_vf_instancing(struct anv_batch *batch,
                       uint32_t *vertex_element_dws,
                       struct anv_device *device,
                       struct anv_shader *shader,
                       const struct vk_vertex_input_state *vi)
{
   const struct brw_vs_prog_data *vs_prog_data =
      get_shader_vs_prog_data(shader);
   const uint64_t inputs_read = vs_prog_data->inputs_read;
   const uint64_t double_inputs_read =
      vs_prog_data->double_inputs_read & inputs_read;
   assert((inputs_read & ((1 << VERT_ATTRIB_GENERIC0) - 1)) == 0);
   const uint32_t elements = inputs_read >> VERT_ATTRIB_GENERIC0;
   const uint32_t elements_double = double_inputs_read >> VERT_ATTRIB_GENERIC0;

   for (uint32_t i = 0; i < shader->vs.input_elements; i++) {
      /* The SKL docs for VERTEX_ELEMENT_STATE say:
       *
       *    "All elements must be valid from Element[0] to the last valid
       *    element. (I.e. if Element[2] is valid then Element[1] and
       *    Element[0] must also be valid)."
       *
       * The SKL docs for 3D_Vertex_Component_Control say:
       *
       *    "Don't store this component. (Not valid for Component 0, but can
       *    be used for Component 1-3)."
       *
       * So we can't just leave a vertex element blank and hope for the best.
       * We have to tell the VF hardware to put something in it; so we just
       * store a bunch of zero.
       *
       * TODO: Compact vertex elements so we never end up with holes.
       */
      struct GENX(VERTEX_ELEMENT_STATE) element = {
         .Valid = true,
         .Component0Control = VFCOMP_STORE_0,
         .Component1Control = VFCOMP_STORE_0,
         .Component2Control = VFCOMP_STORE_0,
         .Component3Control = VFCOMP_STORE_0,
      };
      GENX(VERTEX_ELEMENT_STATE_pack)(NULL,
                                      &vertex_element_dws[i * 2],
                                      &element);
   }

   u_foreach_bit(a, vi->attributes_valid) {
      enum isl_format format = anv_get_vbo_format(
         device->physical, vi->attributes[a].format);
      assume(format < ISL_NUM_FORMATS);

      uint32_t binding = vi->attributes[a].binding;
      assert(binding < get_max_vbs(device->info));

      if ((elements & (1 << a)) == 0)
         continue; /* Binding unused */

      uint32_t slot =
         __builtin_popcount(elements & ((1 << a) - 1)) -
         DIV_ROUND_UP(__builtin_popcount(elements_double &
                                        ((1 << a) -1)), 2);

      struct GENX(VERTEX_ELEMENT_STATE) element = {
         .VertexBufferIndex = vi->attributes[a].binding,
         .Valid = true,
         .SourceElementFormat = format,
         .EdgeFlagEnable = false,
         .SourceElementOffset = vi->attributes[a].offset,
         .Component0Control = vertex_element_comp_control(format, 0),
         .Component1Control = vertex_element_comp_control(format, 1),
         .Component2Control = vertex_element_comp_control(format, 2),
         .Component3Control = vertex_element_comp_control(format, 3),
      };
      GENX(VERTEX_ELEMENT_STATE_pack)(NULL,
                                      &vertex_element_dws[slot * 2],
                                      &element);

      /* On Broadwell and later, we have a separate VF_INSTANCING packet
       * that controls instancing.  On Haswell and prior, that's part of
       * VERTEX_BUFFER_STATE which we emit later.
       */
      anv_batch_emit(batch, GENX(3DSTATE_VF_INSTANCING), vfi) {
         bool per_instance = vi->bindings[binding].input_rate ==
            VK_VERTEX_INPUT_RATE_INSTANCE;
         uint32_t divisor = vi->bindings[binding].divisor *
            shader->instance_multiplier;

         vfi.InstancingEnable = per_instance;
         vfi.VertexElementIndex = slot;
         vfi.InstanceDataStepRate = per_instance ? divisor : 1;
      }
   }
}

void
genX(batch_emit_vertex_input)(struct anv_batch *batch,
                              struct anv_device *device,
                              struct anv_shader *shader,
                              const struct vk_vertex_input_state *vi)
{
   const uint32_t ve_count = shader == NULL ? 0 :
      (shader->vs.input_elements + shader->vs.sgvs_count);
   const uint32_t num_dwords = 1 + 2 * MAX2(1, ve_count);
   uint32_t *p = anv_batch_emitn(batch, num_dwords,
                                 GENX(3DSTATE_VERTEX_ELEMENTS));
   if (p == NULL)
      return;

   if (ve_count == 0) {
      memcpy(p + 1, device->physical->gfx_default.empty_vs_input,
             sizeof(device->physical->gfx_default.empty_vs_input));
   } else {
      /* Use dyn->vi to emit the dynamic VERTEX_ELEMENT_STATE input. */
      emit_ves_vf_instancing(batch, p + 1, device, shader, vi);
      /* Then append the VERTEX_ELEMENT_STATE for the draw parameters */
      memcpy(p + 1 + 2 * shader->vs.input_elements,
             shader->vs.sgvs_elements,
             4 * 2 * shader->vs.sgvs_count);
   }
}

static void
emit_vs_shader(struct anv_batch *batch,
               struct anv_device *device,
               struct anv_shader *shader)
{
   const struct intel_device_info *devinfo = device->info;
   const struct brw_vs_prog_data *vs_prog_data =
      get_shader_vs_prog_data(shader);

   shader->vs.input_elements = get_vs_input_elements(vs_prog_data);

   shader->vs.sgvs_count =
      (vs_prog_data->uses_vertexid ||
       vs_prog_data->uses_instanceid ||
       vs_prog_data->uses_firstvertex ||
       vs_prog_data->uses_baseinstance) + vs_prog_data->uses_drawid;

   const bool needs_sgvs_elem = shader->vs.sgvs_count > 1 ||
                                !vs_prog_data->uses_drawid;
   const uint32_t id_slot = shader->vs.input_elements;
   const uint32_t drawid_slot = id_slot + needs_sgvs_elem;
   if (shader->vs.sgvs_count > 0) {
      uint32_t slot_offset = 0;

      if (needs_sgvs_elem) {
#if GFX_VER < 11
         /* From the Broadwell PRM for the 3D_Vertex_Component_Control enum:
          *    "Within a VERTEX_ELEMENT_STATE structure, if a Component
          *    Control field is set to something other than VFCOMP_STORE_SRC,
          *    no higher-numbered Component Control fields may be set to
          *    VFCOMP_STORE_SRC"
          *
          * This means, that if we have BaseInstance, we need BaseVertex as
          * well.  Just do all or nothing.
          */
         uint32_t base_ctrl = (vs_prog_data->uses_firstvertex ||
                               vs_prog_data->uses_baseinstance) ?
                              VFCOMP_STORE_SRC : VFCOMP_STORE_0;
#endif

         struct GENX(VERTEX_ELEMENT_STATE) element = {
            .VertexBufferIndex = ANV_SVGS_VB_INDEX,
            .Valid = true,
            .SourceElementFormat = ISL_FORMAT_R32G32_UINT,
#if GFX_VER >= 11
            /* On gen11, these are taken care of by extra parameter slots */
            .Component0Control = VFCOMP_STORE_0,
            .Component1Control = VFCOMP_STORE_0,
#else
            .Component0Control = base_ctrl,
            .Component1Control = base_ctrl,
#endif
            .Component2Control = VFCOMP_STORE_0,
            .Component3Control = VFCOMP_STORE_0,
         };
         GENX(VERTEX_ELEMENT_STATE_pack)(NULL,
                                         &shader->vs.sgvs_elements[slot_offset * 2],
                                         &element);
         slot_offset++;

         anv_shader_emit(batch, shader, vs.vf_sgvs_instancing,
                         GENX(3DSTATE_VF_INSTANCING), vfi) {
            vfi.VertexElementIndex = id_slot;
         }
      }

      if (vs_prog_data->uses_drawid) {
         struct GENX(VERTEX_ELEMENT_STATE) element = {
            .VertexBufferIndex = ANV_DRAWID_VB_INDEX,
            .Valid = true,
            .SourceElementFormat = ISL_FORMAT_R32_UINT,
#if GFX_VER >= 11
            /* On gen11, this is taken care of by extra parameter slots */
            .Component0Control = VFCOMP_STORE_0,
#else
            .Component0Control = VFCOMP_STORE_SRC,
#endif
            .Component1Control = VFCOMP_STORE_0,
            .Component2Control = VFCOMP_STORE_0,
            .Component3Control = VFCOMP_STORE_0,
         };
         GENX(VERTEX_ELEMENT_STATE_pack)(NULL,
                                         &shader->vs.sgvs_elements[slot_offset * 2],
                                         &element);
         slot_offset++;

         anv_shader_emit(batch, shader, vs.vf_sgvs_instancing,
                         GENX(3DSTATE_VF_INSTANCING), vfi) {
            vfi.VertexElementIndex = drawid_slot;
         }
      }
   }

   anv_shader_emit(batch, shader, vs.vf_sgvs, GENX(3DSTATE_VF_SGVS), sgvs) {
      sgvs.VertexIDEnable              = vs_prog_data->uses_vertexid;
      sgvs.VertexIDComponentNumber     = 2;
      sgvs.VertexIDElementOffset       = id_slot;
      sgvs.InstanceIDEnable            = vs_prog_data->uses_instanceid;
      sgvs.InstanceIDComponentNumber   = 3;
      sgvs.InstanceIDElementOffset     = id_slot;
   }

#if GFX_VER >= 11
   anv_shader_emit(batch, shader, vs.vf_sgvs_2, GENX(3DSTATE_VF_SGVS_2), sgvs) {
      /* gl_BaseVertex */
      sgvs.XP0Enable                   = vs_prog_data->uses_firstvertex;
      sgvs.XP0SourceSelect             = XP0_PARAMETER;
      sgvs.XP0ComponentNumber          = 0;
      sgvs.XP0ElementOffset            = id_slot;

      /* gl_BaseInstance */
      sgvs.XP1Enable                   = vs_prog_data->uses_baseinstance;
      sgvs.XP1SourceSelect             = StartingInstanceLocation;
      sgvs.XP1ComponentNumber          = 1;
      sgvs.XP1ElementOffset            = id_slot;

      /* gl_DrawID */
      sgvs.XP2Enable                   = vs_prog_data->uses_drawid;
      sgvs.XP2ComponentNumber          = 0;
      sgvs.XP2ElementOffset            = drawid_slot;
   }
#endif

   if (device->physical->instance->vf_component_packing) {
      anv_shader_emit(batch, shader, vs.vf_component_packing,
                      GENX(3DSTATE_VF_COMPONENT_PACKING), vfc) {
         vfc.VertexElementEnablesDW[0] = vs_prog_data->vf_component_packing[0];
         vfc.VertexElementEnablesDW[1] = vs_prog_data->vf_component_packing[1];
         vfc.VertexElementEnablesDW[2] = vs_prog_data->vf_component_packing[2];
         vfc.VertexElementEnablesDW[3] = vs_prog_data->vf_component_packing[3];
      }
   }

   uint32_t vs_dwords[GENX(3DSTATE_VS_length)];
   anv_shader_emit_tmp(batch, vs_dwords, GENX(3DSTATE_VS), vs) {
      vs.Enable               = true;
      vs.StatisticsEnable     = true;
      vs.KernelStartPointer   = shader->kernel.offset;
#if GFX_VER < 20
      vs.SIMD8DispatchEnable  =
         vs_prog_data->base.dispatch_mode == DISPATCH_MODE_SIMD8;
#endif

      assert(!vs_prog_data->base.base.use_alt_mode);
#if GFX_VER < 11
      vs.SingleVertexDispatch       = false;
#endif
      vs.VectorMaskEnable           = false;
      /* Wa_1606682166:
       * Incorrect TDL's SSP address shift in SARB for 16:6 & 18:8 modes.
       * Disable the Sampler state prefetch functionality in the SARB by
       * programming 0xB000[30] to '1'.
       */
      vs.SamplerCount               = GFX_VER == 11 ? 0 : get_sampler_count(shader);
      vs.BindingTableEntryCount     = shader->bind_map.surface_count;
      vs.FloatingPointMode          = IEEE754;
      vs.IllegalOpcodeExceptionEnable = false;
      vs.SoftwareExceptionEnable    = false;
      vs.MaximumNumberofThreads     = devinfo->max_vs_threads - 1;

      vs.VertexURBEntryReadLength      = vs_prog_data->base.urb_read_length;
      vs.VertexURBEntryReadOffset      = 0;
      vs.DispatchGRFStartRegisterForURBData =
         vs_prog_data->base.base.dispatch_grf_start_reg;

      vs.UserClipDistanceClipTestEnableBitmask =
         vs_prog_data->base.clip_distance_mask;
      vs.UserClipDistanceCullTestEnableBitmask =
         vs_prog_data->base.cull_distance_mask;

#if GFX_VER >= 30
      vs.RegistersPerThread = ptl_register_blocks(vs_prog_data->base.base.grf_used);
#endif
   }

   anv_shader_emit_merge(batch, shader, vs.vs, vs_dwords, GENX(3DSTATE_VS), vs) {
#if GFX_VERx10 >= 125
      vs.ScratchSpaceBuffer = get_scratch_surf(batch, device, shader, false);
#else
      vs.PerThreadScratchSpace = get_scratch_space(shader);
      vs.ScratchSpaceBasePointer = get_scratch_address(device, shader);
#endif
   }
   if (device_needs_protected(device)) {
      anv_shader_emit_merge(batch, shader, vs.vs_protected,
                            vs_dwords, GENX(3DSTATE_VS), vs) {
#if GFX_VERx10 >= 125
         vs.ScratchSpaceBuffer = get_scratch_surf(batch, device, shader, true);
#else
         vs.PerThreadScratchSpace = get_scratch_space(shader);
         vs.ScratchSpaceBasePointer = get_scratch_address(device, shader);
#endif
      }
   }
}

static void
emit_hs_shader(struct anv_batch *batch,
               struct anv_device *device,
               struct anv_shader *shader)
{
   const struct intel_device_info *devinfo = device->info;
   const struct brw_tcs_prog_data *tcs_prog_data =
      get_shader_tcs_prog_data(shader);

   uint32_t hs_dwords[GENX(3DSTATE_HS_length)];
   anv_shader_emit_tmp(batch, hs_dwords, GENX(3DSTATE_HS), hs) {
      hs.Enable = true;
      hs.StatisticsEnable = true;
      hs.KernelStartPointer = shader->kernel.offset;
      /* Wa_1606682166 */
      hs.SamplerCount = GFX_VER == 11 ? 0 : get_sampler_count(shader);
      hs.BindingTableEntryCount = shader->bind_map.surface_count;

#if GFX_VER >= 12
      /* Wa_1604578095:
       *
       *    Hang occurs when the number of max threads is less than 2 times
       *    the number of instance count. The number of max threads must be
       *    more than 2 times the number of instance count.
       */
      assert((devinfo->max_tcs_threads / 2) > tcs_prog_data->instances);
#endif

      hs.MaximumNumberofThreads = devinfo->max_tcs_threads - 1;
      hs.IncludeVertexHandles = true;
      hs.InstanceCount = tcs_prog_data->instances - 1;

      hs.VertexURBEntryReadLength = 0;
      hs.VertexURBEntryReadOffset = 0;
      hs.DispatchGRFStartRegisterForURBData =
         tcs_prog_data->base.base.dispatch_grf_start_reg & 0x1f;
#if GFX_VER >= 12
      hs.DispatchGRFStartRegisterForURBData5 =
         tcs_prog_data->base.base.dispatch_grf_start_reg >> 5;
#endif

#if GFX_VER == 12
      /*  Patch Count threshold specifies the maximum number of patches that
       *  will be accumulated before a thread dispatch is forced.
       */
      hs.PatchCountThreshold = tcs_prog_data->patch_count_threshold;
#endif

#if GFX_VER < 20
      hs.DispatchMode = tcs_prog_data->base.dispatch_mode;
#endif
      hs.IncludePrimitiveID = tcs_prog_data->include_primitive_id;

#if GFX_VER >= 30
      hs.RegistersPerThread = ptl_register_blocks(tcs_prog_data->base.base.grf_used);
#endif
   };

   anv_shader_emit_merge(batch, shader, hs.hs, hs_dwords, GENX(3DSTATE_HS), hs) {
#if GFX_VERx10 >= 125
      hs.ScratchSpaceBuffer = get_scratch_surf(batch, device, shader, false);
#else
      hs.PerThreadScratchSpace = get_scratch_space(shader);
      hs.ScratchSpaceBasePointer = get_scratch_address(device, shader);
#endif
   }
   if (device_needs_protected(device)) {
      anv_shader_emit_merge(batch, shader, hs.hs_protected,
                            hs_dwords, GENX(3DSTATE_HS), hs) {
#if GFX_VERx10 >= 125
         hs.ScratchSpaceBuffer = get_scratch_surf(batch, device, shader, false);
#else
         hs.PerThreadScratchSpace = get_scratch_space(shader);
         hs.ScratchSpaceBasePointer = get_scratch_address(device, shader);
#endif
      }
   }
}

static void
emit_ds_shader(struct anv_batch *batch,
               struct anv_device *device,
               struct anv_shader *shader)
{
   const struct intel_device_info *devinfo = device->info;
   const struct brw_tes_prog_data *tes_prog_data =
      get_shader_tes_prog_data(shader);

   anv_shader_emit(batch, shader, ds.te, GENX(3DSTATE_TE), te) {
      te.TEEnable = true;
      te.MaximumTessellationFactorOdd = 63.0;
      te.MaximumTessellationFactorNotOdd = 64.0;
#if GFX_VERx10 >= 125
#if GFX_VER >= 20
      if (intel_needs_workaround(device->info, 16025857284))
         te.TessellationDistributionLevel = TEDLEVEL_PATCH;
      else
         te.TessellationDistributionLevel = TEDLEVEL_REGION;
#else
      te.TessellationDistributionLevel = TEDLEVEL_PATCH;
#endif
      /* 64_TRIANGLES */
      te.SmallPatchThreshold = 3;
      /* 1K_TRIANGLES */
      te.TargetBlockSize = 8;
      /* 1K_TRIANGLES */
      te.LocalBOPAccumulatorThreshold = 1;
#endif

#if GFX_VER >= 20
      te.NumberOfRegionsPerPatch = 2;
#endif
   }

   uint32_t ds_dwords[GENX(3DSTATE_DS_length)];
   anv_shader_emit_tmp(batch, ds_dwords, GENX(3DSTATE_DS), ds) {
      ds.Enable = true;
      ds.StatisticsEnable = true;
      ds.KernelStartPointer = shader->kernel.offset;
      /* Wa_1606682166 */
      ds.SamplerCount = GFX_VER == 11 ? 0 : get_sampler_count(shader);
      ds.BindingTableEntryCount = shader->bind_map.surface_count;
      ds.MaximumNumberofThreads = devinfo->max_tes_threads - 1;

      ds.PatchURBEntryReadLength = tes_prog_data->base.urb_read_length;
      ds.PatchURBEntryReadOffset = 0;
      ds.DispatchGRFStartRegisterForURBData =
         tes_prog_data->base.base.dispatch_grf_start_reg;

#if GFX_VER < 11
      ds.DispatchMode =
         tes_prog_data->base.dispatch_mode == DISPATCH_MODE_SIMD8 ?
         DISPATCH_MODE_SIMD8_SINGLE_PATCH :
         DISPATCH_MODE_SIMD4X2;
#else
      assert(tes_prog_data->base.dispatch_mode == INTEL_DISPATCH_MODE_SIMD8);
      ds.DispatchMode = DISPATCH_MODE_SIMD8_SINGLE_PATCH;
#endif

      ds.UserClipDistanceClipTestEnableBitmask =
         tes_prog_data->base.clip_distance_mask;
      ds.UserClipDistanceCullTestEnableBitmask =
         tes_prog_data->base.cull_distance_mask;

#if GFX_VER >= 12
      ds.PrimitiveIDNotRequired = !tes_prog_data->include_primitive_id;
#endif

#if GFX_VER >= 30
      ds.RegistersPerThread = ptl_register_blocks(tes_prog_data->base.base.grf_used);
#endif
   }

   anv_shader_emit_merge(batch, shader, ds.ds, ds_dwords, GENX(3DSTATE_DS), ds) {
#if GFX_VERx10 >= 125
      ds.ScratchSpaceBuffer = get_scratch_surf(batch, device, shader, false);
#else
      ds.PerThreadScratchSpace = get_scratch_space(shader);
      ds.ScratchSpaceBasePointer = get_scratch_address(device, shader);
#endif
   }
   if (device_needs_protected(device)) {
      anv_shader_emit_merge(batch, shader, ds.ds_protected,
                            ds_dwords, GENX(3DSTATE_DS), ds) {
#if GFX_VERx10 >= 125
         ds.ScratchSpaceBuffer = get_scratch_surf(batch, device, shader, true);
#else
         ds.PerThreadScratchSpace = get_scratch_space(shader);
         ds.ScratchSpaceBasePointer = get_scratch_address(device, shader);
#endif
      }
   }
}

static void
emit_gs_shader(struct anv_batch *batch,
               struct anv_device *device,
               struct anv_shader *shader)
{
   const struct intel_device_info *devinfo = device->info;
   const struct brw_gs_prog_data *gs_prog_data =
      get_shader_gs_prog_data(shader);

   uint32_t gs_dwords[GENX(3DSTATE_GS_length)];
   anv_shader_emit_tmp(batch, gs_dwords, GENX(3DSTATE_GS), gs) {
      gs.Enable                  = true;
      gs.StatisticsEnable        = true;
      gs.KernelStartPointer      = shader->kernel.offset;
#if GFX_VER < 20
      gs.DispatchMode            = gs_prog_data->base.dispatch_mode;
#endif

      gs.SingleProgramFlow       = false;
      gs.VectorMaskEnable        = false;
      /* Wa_1606682166 */
      gs.SamplerCount            = GFX_VER == 11 ? 0 : get_sampler_count(shader);
      gs.BindingTableEntryCount  = shader->bind_map.surface_count;
      gs.IncludeVertexHandles    = gs_prog_data->base.include_vue_handles;
      gs.IncludePrimitiveID      = gs_prog_data->include_primitive_id;

      gs.MaximumNumberofThreads  = devinfo->max_gs_threads - 1;

      gs.OutputVertexSize        = gs_prog_data->output_vertex_size_hwords * 2 - 1;
      gs.OutputTopology          = gs_prog_data->output_topology;
      gs.ControlDataFormat       = gs_prog_data->control_data_format;
      gs.ControlDataHeaderSize   = gs_prog_data->control_data_header_size_hwords;
      gs.InstanceControl         = MAX2(gs_prog_data->invocations, 1) - 1;

      gs.ExpectedVertexCount     = gs_prog_data->vertices_in;
      gs.StaticOutput            = gs_prog_data->static_vertex_count >= 0;
      gs.StaticOutputVertexCount = gs_prog_data->static_vertex_count >= 0 ?
                                   gs_prog_data->static_vertex_count : 0;

      gs.VertexURBEntryReadOffset = 0;
      gs.VertexURBEntryReadLength = gs_prog_data->base.urb_read_length;
      gs.DispatchGRFStartRegisterForURBData =
         gs_prog_data->base.base.dispatch_grf_start_reg;

      gs.UserClipDistanceClipTestEnableBitmask =
         gs_prog_data->base.clip_distance_mask;
      gs.UserClipDistanceCullTestEnableBitmask =
         gs_prog_data->base.cull_distance_mask;

#if GFX_VER >= 30
      gs.RegistersPerThread = ptl_register_blocks(gs_prog_data->base.base.grf_used);
#endif
   }

   anv_shader_emit_merge(batch, shader, gs.gs, gs_dwords, GENX(3DSTATE_GS), gs) {
#if GFX_VERx10 >= 125
      gs.ScratchSpaceBuffer = get_scratch_surf(batch, device, shader, false);
#else
      gs.PerThreadScratchSpace = get_scratch_space(shader);
      gs.ScratchSpaceBasePointer = get_scratch_address(device, shader);
#endif
   }
   if (device_needs_protected(device)) {
      anv_shader_emit_merge(batch, shader, gs.gs_protected,
                            gs_dwords, GENX(3DSTATE_GS), gs) {
#if GFX_VERx10 >= 125
         gs.ScratchSpaceBuffer = get_scratch_surf(batch, device, shader, true);
#else
         gs.PerThreadScratchSpace = get_scratch_space(shader);
         gs.ScratchSpaceBasePointer = get_scratch_address(device, shader);
#endif
      }
   }
}

#if GFX_VERx10 >= 125
static void
emit_task_shader(struct anv_batch *batch,
                 struct anv_device *device,
                 struct anv_shader *shader)
{
   const struct intel_device_info *devinfo = device->info;
   const struct brw_task_prog_data *task_prog_data =
      get_shader_task_prog_data(shader);
   const struct intel_cs_dispatch_info task_dispatch =
      brw_cs_get_dispatch_info(devinfo, &task_prog_data->base, NULL);

   uint32_t task_control_dwords[GENX(3DSTATE_TASK_CONTROL_length)];
   anv_shader_emit_tmp(batch, task_control_dwords, GENX(3DSTATE_TASK_CONTROL), tc) {
      tc.TaskShaderEnable = true;
      tc.StatisticsEnable = true;
      tc.MaximumNumberofThreadGroups = 511;
   }

   anv_shader_emit_merge(batch, shader, ts.control,
                         task_control_dwords, GENX(3DSTATE_TASK_CONTROL), tc) {
      tc.ScratchSpaceBuffer = get_scratch_surf(batch, device, shader, false);
   }
   if (device_needs_protected(device)) {
      anv_shader_emit_merge(batch, shader, ts.control_protected,
                            task_control_dwords, GENX(3DSTATE_TASK_CONTROL), tc) {
         tc.ScratchSpaceBuffer = get_scratch_surf(batch, device, shader, true);
      }
   }

   anv_shader_emit(batch, shader, ts.shader, GENX(3DSTATE_TASK_SHADER), task) {
      task.KernelStartPointer                = shader->kernel.offset;
      task.SIMDSize                          = task_dispatch.simd_size / 16;
      task.MessageSIMD                       = task.SIMDSize;
      task.NumberofThreadsinGPGPUThreadGroup = task_dispatch.threads;
      task.ExecutionMask                     = task_dispatch.right_mask;
      task.LocalXMaximum                     = task_dispatch.group_size - 1;
      task.EmitLocalIDX                      = true;

      task.NumberofBarriers                  = task_prog_data->base.uses_barrier;
      task.SharedLocalMemorySize             =
         intel_compute_slm_encode_size(GFX_VER, task_prog_data->base.base.total_shared);
      task.PreferredSLMAllocationSize        =
         intel_compute_preferred_slm_calc_encode_size(devinfo,
                                                      task_prog_data->base.base.total_shared,
                                                      task_dispatch.group_size,
                                                      task_dispatch.simd_size);

      /*
       * 3DSTATE_TASK_SHADER_DATA.InlineData[0:1] will be used for an address
       * of a buffer with push constants and descriptor set table and
       * InlineData[2:7] will be used for first few push constants.
       */
      task.EmitInlineParameter = true;
      task.IndirectDataLength = align(shader->bind_map.push_ranges[0].length * 32, 64);

      task.XP0Required = task_prog_data->uses_drawid;

#if GFX_VER >= 30
      task.RegistersPerThread = ptl_register_blocks(task_prog_data->base.base.grf_used);
#endif
   }

   /* Recommended values from "Task and Mesh Distribution Programming". */
   anv_shader_emit(batch, shader, ts.redistrib,
                   GENX(3DSTATE_TASK_REDISTRIB), redistrib) {
      redistrib.LocalBOTAccumulatorThreshold = MULTIPLIER_1;
      redistrib.SmallTaskThreshold = 1; /* 2^N */
      redistrib.TargetMeshBatchSize = devinfo->num_slices > 2 ? 3 : 5; /* 2^N */
      redistrib.TaskRedistributionLevel = TASKREDISTRIB_BOM;
      redistrib.TaskRedistributionMode = TASKREDISTRIB_RR_STRICT;
   }
}

static void
emit_mesh_shader(struct anv_batch *batch,
                 struct anv_device *device,
                 struct anv_shader *shader)
{
   const struct intel_device_info *devinfo = device->info;
   const struct brw_mesh_prog_data *mesh_prog_data =
      get_shader_mesh_prog_data(shader);
   const struct intel_cs_dispatch_info mesh_dispatch =
      brw_cs_get_dispatch_info(devinfo, &mesh_prog_data->base, NULL);

   uint32_t mesh_control_dwords[GENX(3DSTATE_MESH_CONTROL_length)];
   anv_shader_emit_tmp(batch, mesh_control_dwords, GENX(3DSTATE_MESH_CONTROL), mc) {
      mc.MeshShaderEnable = true;
      mc.StatisticsEnable = true;
      mc.MaximumNumberofThreadGroups = 511;
#if GFX_VER >= 20
      mc.VPandRTAIndexAutostripEnable = mesh_prog_data->autostrip_enable;
#endif
   }

   anv_shader_emit_merge(batch, shader, ms.control,
                         mesh_control_dwords, GENX(3DSTATE_MESH_CONTROL), mc) {
      mc.ScratchSpaceBuffer = get_scratch_surf(batch, device, shader, false);
   }
   if (device_needs_protected(device)) {
      anv_shader_emit_merge(batch, shader, ms.control_protected,
                            mesh_control_dwords, GENX(3DSTATE_MESH_CONTROL), mc) {
         mc.ScratchSpaceBuffer = get_scratch_surf(batch, device, shader, true);
      }
   }

   const unsigned output_topology =
      mesh_prog_data->primitive_type == MESA_PRIM_POINTS ? OUTPUT_POINT :
      mesh_prog_data->primitive_type == MESA_PRIM_LINES  ? OUTPUT_LINE :
                                                             OUTPUT_TRI;

   uint32_t index_format;
   switch (mesh_prog_data->index_format) {
   case BRW_INDEX_FORMAT_U32:
      index_format = INDEX_U32;
      break;
   case BRW_INDEX_FORMAT_U888X:
      index_format = INDEX_U888X;
      break;
   default:
      UNREACHABLE("invalid index format");
   }

   anv_shader_emit(batch, shader, ms.shader, GENX(3DSTATE_MESH_SHADER), mesh) {
      mesh.KernelStartPointer                = shader->kernel.offset;
      mesh.SIMDSize                          = mesh_dispatch.simd_size / 16;
      mesh.MessageSIMD                       = mesh.SIMDSize;
      mesh.NumberofThreadsinGPGPUThreadGroup = mesh_dispatch.threads;
      mesh.ExecutionMask                     = mesh_dispatch.right_mask;
      mesh.LocalXMaximum                     = mesh_dispatch.group_size - 1;
      mesh.EmitLocalIDX                      = true;

      mesh.MaximumPrimitiveCount             = MAX2(mesh_prog_data->map.max_primitives, 1) - 1;
      mesh.OutputTopology                    = output_topology;
      mesh.PerVertexDataPitch                = mesh_prog_data->map.per_vertex_stride / 32;
      mesh.PerPrimitiveDataPresent           = mesh_prog_data->map.per_primitive_stride > 0;
      mesh.PerPrimitiveDataPitch             = mesh_prog_data->map.per_primitive_stride / 32;
      mesh.IndexFormat                       = index_format;

      mesh.NumberofBarriers                  = mesh_prog_data->base.uses_barrier;
      mesh.SharedLocalMemorySize             =
         intel_compute_slm_encode_size(GFX_VER, mesh_prog_data->base.base.total_shared);
      mesh.PreferredSLMAllocationSize        =
         intel_compute_preferred_slm_calc_encode_size(devinfo,
                                                      mesh_prog_data->base.base.total_shared,
                                                      mesh_dispatch.group_size,
                                                      mesh_dispatch.simd_size);

      /*
       * 3DSTATE_MESH_SHADER_DATA.InlineData[0:1] will be used for an address
       * of a buffer with push constants and descriptor set table and
       * InlineData[2:7] will be used for first few push constants.
       */
      mesh.EmitInlineParameter = true;
      mesh.IndirectDataLength = align(shader->bind_map.push_ranges[0].length * 32, 64);

      mesh.XP0Required = mesh_prog_data->uses_drawid;

#if GFX_VER >= 30
      mesh.RegistersPerThread = ptl_register_blocks(mesh_prog_data->base.base.grf_used);
#endif
   }

   /* Recommended values from "Task and Mesh Distribution Programming". */
   anv_shader_emit(batch, shader, ms.distrib, GENX(3DSTATE_MESH_DISTRIB), distrib) {
      distrib.DistributionMode = MESH_RR_FREE;
      distrib.TaskDistributionBatchSize = devinfo->num_slices > 2 ? 4 : 9; /* 2^N thread groups */
      distrib.MeshDistributionBatchSize = devinfo->num_slices > 2 ? 3 : 3; /* 2^N thread groups */
   }

   anv_shader_emit(batch, shader, ms.clip, GENX(3DSTATE_CLIP_MESH), clip_mesh) {
      clip_mesh.PrimitiveHeaderEnable = mesh_prog_data->map.has_per_primitive_header;
      clip_mesh.UserClipDistanceClipTestEnableBitmask = mesh_prog_data->clip_distance_mask;
      clip_mesh.UserClipDistanceCullTestEnableBitmask = mesh_prog_data->cull_distance_mask;
   }

   /* Disable streamout */
   anv_shader_emit(batch, shader, so, GENX(3DSTATE_STREAMOUT), so);
}
#endif /* GFX_VERx10 >= 125 */

static void
emit_ps_shader(struct anv_batch *batch,
               struct anv_device *device,
               struct anv_shader *shader)
{
   const struct intel_device_info *devinfo = device->info;
   const struct brw_wm_prog_data *wm_prog_data =
      get_shader_wm_prog_data(shader);

   uint32_t ps_dwords[GENX(3DSTATE_PS_length)];
   anv_shader_emit_tmp(batch, ps_dwords, GENX(3DSTATE_PS), ps) {
#if GFX_VER == 12
      assert(wm_prog_data->dispatch_multi == 0 ||
             (wm_prog_data->dispatch_multi == 16 && wm_prog_data->max_polygons == 2));
      ps.DualSIMD8DispatchEnable = wm_prog_data->dispatch_multi;
      /* XXX - No major improvement observed from enabling
       *       overlapping subspans, but it could be helpful
       *       in theory when the requirements listed on the
       *       BSpec page for 3DSTATE_PS_BODY are met.
       */
      ps.OverlappingSubspansEnable = false;
#endif

      ps.SingleProgramFlow          = false;
      ps.VectorMaskEnable           = wm_prog_data->uses_vmask;
      /* Wa_1606682166 */
      ps.SamplerCount               = GFX_VER == 11 ? 0 : get_sampler_count(shader);
      ps.BindingTableEntryCount     = shader->bind_map.surface_count;
#if GFX_VER < 20
      ps.PushConstantEnable         =
         wm_prog_data->base.nr_params > 0 ||
         wm_prog_data->base.ubo_ranges[0].length;
#endif

      ps.MaximumNumberofThreadsPerPSD = devinfo->max_threads_per_psd - 1;

#if GFX_VER >= 30
      ps.RegistersPerThread = ptl_register_blocks(wm_prog_data->base.grf_used);
#endif
   }

   anv_shader_emit_merge(batch, shader, ps.ps, ps_dwords, GENX(3DSTATE_PS), ps) {
#if GFX_VERx10 >= 125
      ps.ScratchSpaceBuffer = get_scratch_surf(batch, device, shader, false);
#else
      ps.PerThreadScratchSpace = get_scratch_space(shader);
      ps.ScratchSpaceBasePointer = get_scratch_address(device, shader);
#endif
   }
   if (device_needs_protected(device)) {
      anv_shader_emit_merge(batch, shader, ps.ps_protected,
                            ps_dwords, GENX(3DSTATE_PS), ps) {
#if GFX_VERx10 >= 125
         ps.ScratchSpaceBuffer = get_scratch_surf(batch, device, shader, true);
#else
         ps.PerThreadScratchSpace = get_scratch_space(shader);
         ps.ScratchSpaceBasePointer = get_scratch_address(device, shader);
#endif
      }
   }

   anv_shader_emit(batch, shader, ps.ps_extra, GENX(3DSTATE_PS_EXTRA), ps) {
      ps.PixelShaderValid              = true;
#if GFX_VER < 20
      ps.AttributeEnable               = wm_prog_data->num_varying_inputs > 0;
#endif
      ps.oMaskPresenttoRenderTarget    = wm_prog_data->uses_omask;
      ps.PixelShaderComputedDepthMode  = wm_prog_data->computed_depth_mode;
      ps.PixelShaderUsesSourceDepth    = wm_prog_data->uses_src_depth;
      ps.PixelShaderUsesSourceW        = wm_prog_data->uses_src_w;

      ps.PixelShaderComputesStencil    = wm_prog_data->computed_stencil;
#if GFX_VER >= 20
      assert(!wm_prog_data->pulls_bary);
#else
      ps.PixelShaderPullsBary          = wm_prog_data->pulls_bary;
#endif

#if GFX_VER >= 11
      ps.PixelShaderRequiresSubpixelSampleOffsets =
         wm_prog_data->uses_sample_offsets;
      ps.PixelShaderRequiresNonPerspectiveBaryPlaneCoefficients =
         wm_prog_data->uses_npc_bary_coefficients;
      ps.PixelShaderRequiresPerspectiveBaryPlaneCoefficients =
         wm_prog_data->uses_pc_bary_coefficients;
      ps.PixelShaderRequiresSourceDepthandorWPlaneCoefficients =
         wm_prog_data->uses_depth_w_coefficients;
#endif
   }

   anv_shader_emit(batch, shader, ps.wm, GENX(3DSTATE_WM), wm) {
      wm.StatisticsEnable                    = true;
      wm.LineEndCapAntialiasingRegionWidth   = _05pixels;
      wm.LineAntialiasingRegionWidth         = _10pixels;
      wm.PointRasterizationRule              = RASTRULE_UPPER_LEFT;

      if (wm_prog_data->early_fragment_tests) {
         wm.EarlyDepthStencilControl         = EDSC_PREPS;
      } else if (wm_prog_data->has_side_effects) {
         wm.EarlyDepthStencilControl         = EDSC_PSEXEC;
      } else {
         wm.EarlyDepthStencilControl         = EDSC_NORMAL;
      }
   }
}

static void
emit_cs_shader(struct anv_batch *batch,
               struct anv_device *device,
               struct anv_shader *shader)
{
   const struct intel_device_info *devinfo = device->info;
   const struct brw_cs_prog_data *cs_prog_data =
      get_shader_cs_prog_data(shader);
   const struct intel_cs_dispatch_info dispatch =
      brw_cs_get_dispatch_info(devinfo, cs_prog_data, NULL);

#if GFX_VERx10 >= 125
   struct GENX(COMPUTE_WALKER_BODY) walker =  {
      /* HSD 14016252163: Use of Morton walk order (and batching using a batch
       * size of 4) is expected to increase sampler cache hit rates by
       * increasing sample address locality within a subslice.
       */
#if GFX_VER >= 30
      .DispatchWalkOrder        = cs_prog_data->uses_sampler ?
                                  MortonWalk : LinearWalk,
      .ThreadGroupBatchSize     = cs_prog_data->uses_sampler ?
                                  TG_BATCH_4 : TG_BATCH_1,
#endif
      .SIMDSize                       = dispatch.simd_size / 16,
      .MessageSIMD                    = dispatch.simd_size / 16,
      .GenerateLocalID                = cs_prog_data->generate_local_id != 0,
      .EmitLocal                      = cs_prog_data->generate_local_id,
      .WalkOrder                      = cs_prog_data->walk_order,
      .TileLayout                     = cs_prog_data->walk_order == INTEL_WALK_ORDER_YXZ ?
                                        TileY32bpe : Linear,
      .LocalXMaximum                  = cs_prog_data->local_size[0] - 1,
      .LocalYMaximum                  = cs_prog_data->local_size[1] - 1,
      .LocalZMaximum                  = cs_prog_data->local_size[2] - 1,
      .PostSync                       = {
         .MOCS                        = anv_mocs(device, NULL, 0),
      },
      .InterfaceDescriptor            = {
         .KernelStartPointer                = shader->kernel.offset,
         .SamplerCount                      = DIV_ROUND_UP(
            CLAMP(shader->bind_map.sampler_count, 0, 16), 4),
         /* Typically set to 0 to avoid prefetching on every thread dispatch. */
         .BindingTableEntryCount            = devinfo->verx10 == 125 ?
                                              0 : 1 + MIN2(shader->bind_map.surface_count, 30),
         .NumberofThreadsinGPGPUThreadGroup = dispatch.threads,
         .SharedLocalMemorySize             = intel_compute_slm_encode_size(
            GFX_VER, cs_prog_data->base.total_shared),
         .PreferredSLMAllocationSize        = intel_compute_preferred_slm_calc_encode_size(
            devinfo, cs_prog_data->base.total_shared,
            dispatch.group_size, dispatch.simd_size),
         .NumberOfBarriers                  = cs_prog_data->uses_barrier,
#if GFX_VER >= 30
         .RegistersPerThread                = ptl_register_blocks(cs_prog_data->base.grf_used),
#endif
      },
      .EmitInlineParameter            = cs_prog_data->uses_inline_push_addr,
   };

   assert(ARRAY_SIZE(shader->cs.gfx125.compute_walker_body) >=
          GENX(COMPUTE_WALKER_BODY_length));
   GENX(COMPUTE_WALKER_BODY_pack)(NULL,
                                  shader->cs.gfx125.compute_walker_body,
                                  &walker);
#else
   const uint32_t vfe_curbe_allocation =
      align(cs_prog_data->push.per_thread.regs * dispatch.threads +
            cs_prog_data->push.cross_thread.regs, 2);

   anv_shader_emit(batch, shader, cs.gfx9.vfe, GENX(MEDIA_VFE_STATE), vfe) {
      vfe.StackSize              = 0;
      vfe.MaximumNumberofThreads =
         devinfo->max_cs_threads * devinfo->subslice_total - 1;
      vfe.NumberofURBEntries     = 2;
#if GFX_VER < 11
      vfe.ResetGatewayTimer      = true;
#endif
      vfe.URBEntryAllocationSize = 2;
      vfe.CURBEAllocationSize    = vfe_curbe_allocation;

      if (cs_prog_data->base.total_scratch) {
         /* Broadwell's Per Thread Scratch Space is in the range [0, 11]
          * where 0 = 1k, 1 = 2k, 2 = 4k, ..., 11 = 2M.
          */
         vfe.PerThreadScratchSpace = ffs(cs_prog_data->base.total_scratch) - 11;
         vfe.ScratchSpaceBasePointer = get_scratch_address(device, shader);
      }
   }

   struct GENX(INTERFACE_DESCRIPTOR_DATA) desc = {
      .KernelStartPointer     =
         shader->kernel.offset +
         brw_cs_prog_data_prog_offset(cs_prog_data, dispatch.simd_size),

      /* Wa_1606682166 */
      .SamplerCount           = GFX_VER == 11 ? 0 : get_sampler_count(shader),

      /* We add 1 because the CS indirect parameters buffer isn't accounted
       * for in bind_map.surface_count.
       *
       * Typically set to 0 to avoid prefetching on every thread dispatch.
       */
      .BindingTableEntryCount = devinfo->verx10 == 125 ?
         0 : MIN2(shader->bind_map.surface_count, 30),
      .BarrierEnable          = cs_prog_data->uses_barrier,
      .SharedLocalMemorySize  =
         intel_compute_slm_encode_size(GFX_VER, cs_prog_data->base.total_shared),

      .ConstantURBEntryReadOffset = 0,
      .ConstantURBEntryReadLength = cs_prog_data->push.per_thread.regs,
      .CrossThreadConstantDataReadLength =
         cs_prog_data->push.cross_thread.regs,
#if GFX_VER >= 12
      /* TODO: Check if we are missing workarounds and enable mid-thread
       * preemption.
       *
       * We still have issues with mid-thread preemption (it was already
       * disabled by the kernel on gfx11, due to missing workarounds). It's
       * possible that we are just missing some workarounds, and could enable
       * it later, but for now let's disable it to fix a GPU in compute in Car
       * Chase (and possibly more).
       */
      .ThreadPreemptionDisable = true,
#endif
#if GFX_VERx10 >= 125
      .ThreadGroupDispatchSize =
         intel_compute_threads_group_dispatch_size(dispatch.threads),
#endif

      .NumberofThreadsinGPGPUThreadGroup = dispatch.threads,
   };
   GENX(INTERFACE_DESCRIPTOR_DATA_pack)(batch,
                                        shader->cs.gfx9.idd,
                                        &desc);
#endif
}

void
genX(init_instructions)(struct anv_physical_device *device)
{
   struct GENX(VERTEX_ELEMENT_STATE) empty_ve = {
      .Valid = true,
      .Component0Control = VFCOMP_STORE_0,
      .Component1Control = VFCOMP_STORE_0,
      .Component2Control = VFCOMP_STORE_0,
      .Component3Control = VFCOMP_STORE_0,
   };
   GENX(VERTEX_ELEMENT_STATE_pack)(
      NULL, device->gfx_default.empty_vs_input, &empty_ve);

   anv_gfx_pack(device->gfx_default.vs, GENX(3DSTATE_VS), vs);
   anv_gfx_pack(device->gfx_default.hs, GENX(3DSTATE_HS), hs);
   anv_gfx_pack(device->gfx_default.ds, GENX(3DSTATE_DS), ds);
   anv_gfx_pack(device->gfx_default.gs, GENX(3DSTATE_GS), gs);
   anv_gfx_pack(device->gfx_default.te, GENX(3DSTATE_TE), te);
   anv_gfx_pack(device->gfx_default.so, GENX(3DSTATE_STREAMOUT), so);
   anv_gfx_pack(device->gfx_default.wm, GENX(3DSTATE_WM), wm) {
      wm.StatisticsEnable = true;
   }
   anv_gfx_pack(device->gfx_default.ps, GENX(3DSTATE_PS), ps);
   anv_gfx_pack(device->gfx_default.ps_extra, GENX(3DSTATE_PS_EXTRA), pse);
   anv_gfx_pack(device->gfx_default.ps_extra_dep, GENX(3DSTATE_PS_EXTRA), pse) {
#if GFX_VERx10 >= 125
      pse.EnablePSDependencyOnCPsizeChange = true;
#endif
   }

#if GFX_VERx10 >= 125
   anv_gfx_pack(device->gfx_default.task_control, GENX(3DSTATE_TASK_CONTROL), ts);
   anv_gfx_pack(device->gfx_default.mesh_control, GENX(3DSTATE_MESH_CONTROL), ms);
#endif
}

void
genX(shader_emit)(struct anv_batch *batch,
                  struct anv_device *device,
                  struct anv_shader *shader)
{
   switch (shader->vk.stage) {
   case MESA_SHADER_VERTEX:
      emit_vs_shader(batch, device, shader);
      emit_3dstate_streamout(batch, device, shader);
      break;
   case MESA_SHADER_TESS_CTRL:
      emit_hs_shader(batch, device, shader);
      break;
   case MESA_SHADER_TESS_EVAL:
      emit_ds_shader(batch, device, shader);
      emit_3dstate_streamout(batch, device, shader);
      break;
   case MESA_SHADER_GEOMETRY:
      emit_gs_shader(batch, device, shader);
      emit_3dstate_streamout(batch, device, shader);
      break;
#if GFX_VERx10 >= 125
   case MESA_SHADER_TASK:
      emit_task_shader(batch, device, shader);
      break;
   case MESA_SHADER_MESH:
      emit_mesh_shader(batch, device, shader);
      break;
#endif /* GFX_VERx10 >= 125 */
   case MESA_SHADER_FRAGMENT:
      emit_ps_shader(batch, device, shader);
      break;
   case MESA_SHADER_COMPUTE:
      emit_cs_shader(batch, device, shader);
      break;
   case MESA_SHADER_RAYGEN:
   case MESA_SHADER_ANY_HIT:
   case MESA_SHADER_CLOSEST_HIT:
   case MESA_SHADER_MISS:
   case MESA_SHADER_INTERSECTION:
   case MESA_SHADER_CALLABLE:
      /* Nothing to do */
      break;
   default:
      UNREACHABLE("Invalid stage");
   }
}

void
genX(write_rt_shader_group)(struct anv_device *device,
                            VkRayTracingShaderGroupTypeKHR type,
                            const struct vk_shader **shaders,
                            uint32_t shader_count,
                            void *output)
{
#if GFX_VERx10 >= 125
   switch (type) {
   case VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR: {
      assert(shader_count == 1);
      struct anv_shader *shader = container_of(shaders[0], struct anv_shader, vk);
      struct GENX(RT_GENERAL_SBT_HANDLE) sh = {};
      sh.General = anv_shader_get_bsr(shader, 32);
      GENX(RT_GENERAL_SBT_HANDLE_pack)(NULL, output, &sh);
      break;
   }

   case VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR: {
      assert(shader_count <= 2);
      struct GENX(RT_TRIANGLES_SBT_HANDLE) sh = {};
      bool anyhit_seen = false;
      for (uint32_t i = 0; i < shader_count; i++) {
         struct anv_shader *shader = container_of(shaders[i], struct anv_shader, vk);
         if (shader->vk.stage == MESA_SHADER_CLOSEST_HIT) {
            sh.ClosestHit = anv_shader_get_bsr(shader, 32);
         } else if (shader->vk.stage == MESA_SHADER_ANY_HIT) {
            sh.AnyHit = anv_shader_get_bsr(shader, 24);
            anyhit_seen = true;
         }
      }
      if (!anyhit_seen)
         sh.AnyHit = anv_shader_internal_get_bsr(device->rt_null_ahs, 24);
      GENX(RT_TRIANGLES_SBT_HANDLE_pack)(NULL, output, &sh);
      break;
   }

   case VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR: {
      assert(shader_count <= 3);
      struct GENX(RT_PROCEDURAL_SBT_HANDLE) sh = {};
      for (uint32_t i = 0; i < shader_count; i++) {
         struct anv_shader *shader = container_of(shaders[i], struct anv_shader, vk);
         /* Skip any-hit shader as it should have been fused into the
          * intersection one and the intersection shader is a requirement for
          * this shader groupe type.
          */
         if (shader->vk.stage == MESA_SHADER_CLOSEST_HIT)
            sh.ClosestHit = anv_shader_get_bsr(shader, 32);
         else if (shader->vk.stage == MESA_SHADER_INTERSECTION)
            sh.Intersection = anv_shader_get_bsr(shader, 24);
         else
            assert(shader->vk.stage == MESA_SHADER_ANY_HIT);
      }
      GENX(RT_PROCEDURAL_SBT_HANDLE_pack)(NULL, output, &sh);
      break;
   }

   default:
      UNREACHABLE("Invalid shader group type");
   }
#else
   UNREACHABLE("No RT support");
#endif
}

uint32_t
genX(shader_cmd_size)(struct anv_device *device,
                      mesa_shader_stage stage)
{
   const uint32_t protected_multiplier =
      device_needs_protected(device) ? 2 : 1;
   const uint32_t streamout_dwords =
      GENX(3DSTATE_STREAMOUT_length) +
      3 /* GENX(3DSTATE_SO_DECL_LIST_length) */ +
      GENX(SO_DECL_ENTRY_length) * 128;

   switch (stage) {
   case MESA_SHADER_VERTEX:
      return
         GENX(3DSTATE_VS_length) * protected_multiplier +
         GENX(3DSTATE_VF_COMPONENT_PACKING_length) +
         GENX(3DSTATE_VF_SGVS_length) +
#if GFX_VER >= 11
         GENX(3DSTATE_VF_SGVS_2_length) +
#endif
         2 * GENX(3DSTATE_VF_INSTANCING_length) +
         streamout_dwords;

   case MESA_SHADER_TESS_CTRL:
      return GENX(3DSTATE_HS_length) * protected_multiplier;

   case MESA_SHADER_TESS_EVAL:
      return GENX(3DSTATE_DS_length) * protected_multiplier +
         GENX(3DSTATE_TE_length) +
         streamout_dwords;

   case MESA_SHADER_GEOMETRY:
      return GENX(3DSTATE_GS_length) * protected_multiplier +
         streamout_dwords;

#if GFX_VERx10 >= 125
   case MESA_SHADER_TASK:
      return
         GENX(3DSTATE_TASK_CONTROL_length) +
         GENX(3DSTATE_TASK_SHADER_length) * protected_multiplier +
         GENX(3DSTATE_TASK_REDISTRIB_length);

   case MESA_SHADER_MESH:
      return
         GENX(3DSTATE_MESH_CONTROL_length) +
         GENX(3DSTATE_MESH_SHADER_length) * protected_multiplier +
         GENX(3DSTATE_MESH_DISTRIB_length) +
         GENX(3DSTATE_CLIP_MESH_length) +
         GENX(3DSTATE_STREAMOUT_length);
#endif

   case MESA_SHADER_FRAGMENT:
      return
         GENX(3DSTATE_PS_length) * protected_multiplier +
         GENX(3DSTATE_PS_EXTRA_length) +
         GENX(3DSTATE_WM_length);

   case MESA_SHADER_COMPUTE:
#if GFX_VERx10 >= 125
      return 0;
#else
      return GENX(MEDIA_VFE_STATE_length);
#endif

   default:
      return 0;
   }
}
