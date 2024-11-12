/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_buffer.h"
#include "nvk_entrypoints.h"
#include "nvk_cmd_buffer.h"
#include "nvk_device.h"
#include "nvk_format.h"
#include "nvk_image.h"
#include "nvk_image_view.h"
#include "nvk_mme.h"
#include "nvk_physical_device.h"
#include "nvk_shader.h"

#include "util/bitpack_helpers.h"
#include "vk_format.h"
#include "vk_render_pass.h"
#include "vk_standard_sample_locations.h"

#include "nv_push_cl902d.h"
#include "nv_push_cl9097.h"
#include "nv_push_cl90b5.h"
#include "nv_push_cl90c0.h"
#include "nv_push_cla097.h"
#include "nv_push_clb097.h"
#include "nv_push_clb197.h"
#include "nv_push_clc397.h"
#include "nv_push_clc597.h"
#include "drf.h"

static inline uint16_t
nvk_cmd_buffer_3d_cls(struct nvk_cmd_buffer *cmd)
{
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);
   struct nvk_physical_device *pdev = nvk_device_physical(dev);
   return pdev->info.cls_eng3d;
}

static void
mme_set_priv_reg(struct mme_builder *b,
                 struct mme_value value,
                 struct mme_value mask,
                 struct mme_value reg)
{
   mme_mthd(b, NV9097_WAIT_FOR_IDLE);
   mme_emit(b, mme_zero());

   mme_mthd(b, NVK_SET_MME_SCRATCH(FALCON_0));
   mme_emit(b, mme_zero());
   mme_emit(b, value);
   mme_emit(b, mask);

   mme_mthd(b, NV9097_SET_FALCON04);
   mme_emit(b, reg);

   struct mme_value loop_cond = mme_mov(b, mme_zero());
   mme_while(b, ine, loop_cond, mme_imm(1)) {
      mme_state_to(b, loop_cond, NVK_SET_MME_SCRATCH(FALCON_0));
      mme_mthd(b, NV9097_NO_OPERATION);
      mme_emit(b, mme_zero());
   };
}

void
nvk_mme_set_priv_reg(struct mme_builder *b)
{
   struct mme_value value = mme_load(b);
   struct mme_value mask = mme_load(b);
   struct mme_value reg = mme_load(b);

   mme_set_priv_reg(b, value, mask, reg);
}

void
nvk_mme_set_conservative_raster_state(struct mme_builder *b)
{
   struct mme_value new_state = mme_load(b);
   struct mme_value old_state =
      nvk_mme_load_scratch(b, CONSERVATIVE_RASTER_STATE);

   mme_if(b, ine, new_state, old_state) {
      nvk_mme_store_scratch(b, CONSERVATIVE_RASTER_STATE, new_state);
      mme_set_priv_reg(b, new_state, mme_imm(BITFIELD_RANGE(23, 2)),
                       mme_imm(0x418800));
   }
}

#define NVK_DRAW_CB0_SIZE sizeof(struct nvk_root_descriptor_table)

void
nvk_mme_select_cb0(struct mme_builder *b)
{
   struct mme_value addr_hi = nvk_mme_load_scratch(b, CB0_ADDR_HI);
   struct mme_value addr_lo = nvk_mme_load_scratch(b, CB0_ADDR_LO);

   mme_mthd(b, NV9097_SET_CONSTANT_BUFFER_SELECTOR_A);
   mme_emit(b, mme_imm(NVK_DRAW_CB0_SIZE));
   mme_emit(b, addr_hi);
   mme_emit(b, addr_lo);
}

static uint32_t nvk_mme_anti_alias_init(void);

VkResult
nvk_push_draw_state_init(struct nvk_queue *queue, struct nv_push *p)
{
   struct nvk_device *dev = nvk_queue_device(queue);
   struct nvk_physical_device *pdev = nvk_device_physical(dev);

   /* 3D state */
   P_MTHD(p, NV9097, SET_OBJECT);
   P_NV9097_SET_OBJECT(p, {
      .class_id = pdev->info.cls_eng3d,
      .engine_id = 0,
   });

   for (uint32_t mme = 0, mme_pos = 0; mme < NVK_MME_COUNT; mme++) {
      size_t size;
      uint32_t *dw = nvk_build_mme(&pdev->info, mme, &size);
      if (dw == NULL)
         return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

      assert(size % sizeof(uint32_t) == 0);
      const uint32_t num_dw = size / sizeof(uint32_t);

      P_MTHD(p, NV9097, LOAD_MME_START_ADDRESS_RAM_POINTER);
      P_NV9097_LOAD_MME_START_ADDRESS_RAM_POINTER(p, mme);
      P_NV9097_LOAD_MME_START_ADDRESS_RAM(p, mme_pos);

      P_1INC(p, NV9097, LOAD_MME_INSTRUCTION_RAM_POINTER);
      P_NV9097_LOAD_MME_INSTRUCTION_RAM_POINTER(p, mme_pos);
      P_INLINE_ARRAY(p, dw, num_dw);

      mme_pos += num_dw;

      free(dw);
   }

   if (pdev->info.cls_eng3d >= TURING_A)
      P_IMMD(p, NVC597, SET_MME_DATA_FIFO_CONFIG, FIFO_SIZE_SIZE_4KB);

   /* Enable FP helper invocation memory loads
    *
    * For generations with firmware support for our `SET_PRIV_REG` mme method
    * we simply use that. On older generations we'll let the kernel do it.
    * Starting with GSP we have to do it via the firmware anyway.
    *
    * This clears bit 3 of gr_gpcs_tpcs_sm_disp_ctrl
    *
    * Without it,
    * dEQP-VK.subgroups.vote.frag_helper.subgroupallequal_bvec2_fragment will
    * occasionally fail.
    */
   if (pdev->info.cls_eng3d >= MAXWELL_B) {
      unsigned reg = pdev->info.cls_eng3d >= VOLTA_A ? 0x419ba4 : 0x419f78;
      P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_SET_PRIV_REG));
      P_INLINE_DATA(p, 0);
      P_INLINE_DATA(p, BITFIELD_BIT(3));
      P_INLINE_DATA(p, reg);
   }

   /* Disable Out Of Range Address exceptions
    *
    * From the SPH documentation:
    *
    *    "The SPH fields StoreReqStart and StoreReqEnd set a range of
    *    attributes whose corresponding Odmap values of ST or ST_LAST are
    *    treated as ST_REQ. Normally, for an attribute whose Omap bit is TRUE
    *    and Odmap value is ST, when the shader writes data to this output, it
    *    can not count on being able to read it back, since the next
    *    downstream shader might have its Imap bit FALSE, thereby causing the
    *    Bmap bit to be FALSE. By including a ST type of attribute in the
    *    range of StoreReqStart and StoreReqEnd, the attribute’s Odmap value
    *    is treated as ST_REQ, so an Omap bit being TRUE causes the Bmap bit
    *    to be TRUE. This guarantees the shader program can output the value
    *    and then read it back later. This will save register space."
    *
    * It's unclear exactly what's going on but this seems to imply that the
    * hardware actually ANDs the output mask of one shader stage together with
    * the input mask of the subsequent shader stage to determine which values
    * are actually used.
    *
    * In the case when we have an empty fragment shader, it seems the hardware
    * doesn't allocate any output memory for final geometry stage at all and
    * so any writes to outputs from the final shader stage generates an Out Of
    * Range Address exception.  We could fix this by eliminating unused
    * outputs via cross-stage linking but that won't work in the case of
    * VK_EXT_shader_object and VK_EXT_graphics_pipeline_library fast-link.
    * Instead, the easiest solution is to just disable the exception.
    *
    * NOTE (Faith):
    *
    *    This above analysis is 100% conjecture on my part based on a creative
    *    reading of the SPH docs and what I saw when trying to run certain
    *    OpenGL CTS tests on NVK + Zink.  Without access to NVIDIA HW
    *    engineers, have no way of verifying this analysis.
    *
    *    The CTS test in question is:
    *
    *    KHR-GL46.tessellation_shader.tessellation_control_to_tessellation_evaluation.gl_tessLevel
    *
    * This should also prevent any issues with array overruns on I/O arrays.
    * Before, they would get an exception and kill the context whereas now
    * they should gently get ignored.
    *
    * This clears bit 14 of gr_gpcs_tpcs_sms_hww_warp_esr_report_mask
    */
   if (pdev->info.cls_eng3d >= MAXWELL_B) {
      unsigned reg = pdev->info.cls_eng3d >= VOLTA_A ? 0x419ea8 : 0x419e44;
      P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_SET_PRIV_REG));
      P_INLINE_DATA(p, 0);
      P_INLINE_DATA(p, BITFIELD_BIT(14));
      P_INLINE_DATA(p, reg);
   }

   /* Set CONSERVATIVE_RASTER_STATE to an invalid value, to ensure the
    * hardware reg is always set the first time conservative rasterization
    * is enabled */
   P_IMMD(p, NV9097, SET_MME_SHADOW_SCRATCH(NVK_MME_SCRATCH_CONSERVATIVE_RASTER_STATE),
                     ~0);

   /* Initialize tessellation parameters */
   P_IMMD(p, NV9097, SET_MME_SHADOW_SCRATCH(NVK_MME_SCRATCH_TESS_PARAMS), 0);
   P_IMMD(p, NV9097, SET_TESSELLATION_PARAMETERS, {});

   P_IMMD(p, NV9097, SET_RENDER_ENABLE_C, MODE_TRUE);

   P_IMMD(p, NV9097, SET_Z_COMPRESSION, ENABLE_TRUE);
   P_MTHD(p, NV9097, SET_COLOR_COMPRESSION(0));
   for (unsigned i = 0; i < 8; i++)
      P_NV9097_SET_COLOR_COMPRESSION(p, i, ENABLE_TRUE);

   P_IMMD(p, NV9097, SET_CT_SELECT, { .target_count = 1 });

//   P_MTHD(cmd->push, NVC0_3D, CSAA_ENABLE);
//   P_INLINE_DATA(cmd->push, 0);

   P_IMMD(p, NV9097, SET_ALIASED_LINE_WIDTH_ENABLE, V_TRUE);

   P_IMMD(p, NV9097, SET_DA_PRIMITIVE_RESTART_VERTEX_ARRAY, ENABLE_FALSE);

   P_IMMD(p, NV9097, SET_BLEND_SEPARATE_FOR_ALPHA, ENABLE_TRUE);
   P_IMMD(p, NV9097, SET_SINGLE_CT_WRITE_CONTROL, ENABLE_TRUE);
   P_IMMD(p, NV9097, SET_SINGLE_ROP_CONTROL, ENABLE_FALSE);
   P_IMMD(p, NV9097, SET_TWO_SIDED_STENCIL_TEST, ENABLE_TRUE);

   P_IMMD(p, NV9097, SET_SHADE_MODE, V_OGL_SMOOTH);

   P_IMMD(p, NV9097, SET_API_VISIBLE_CALL_LIMIT, V__128);

   P_IMMD(p, NV9097, SET_ZCULL_STATS, ENABLE_TRUE);

   P_IMMD(p, NV9097, SET_L1_CONFIGURATION,
                     DIRECTLY_ADDRESSABLE_MEMORY_SIZE_48KB);

   P_IMMD(p, NV9097, SET_REDUCE_COLOR_THRESHOLDS_ENABLE, V_FALSE);
   P_IMMD(p, NV9097, SET_REDUCE_COLOR_THRESHOLDS_UNORM8, {
      .all_covered_all_hit_once = 0xff,
   });
   P_MTHD(p, NV9097, SET_REDUCE_COLOR_THRESHOLDS_UNORM10);
   P_NV9097_SET_REDUCE_COLOR_THRESHOLDS_UNORM10(p, {
      .all_covered_all_hit_once = 0xff,
   });
   P_NV9097_SET_REDUCE_COLOR_THRESHOLDS_UNORM16(p, {
      .all_covered_all_hit_once = 0xff,
   });
   P_NV9097_SET_REDUCE_COLOR_THRESHOLDS_FP11(p, {
      .all_covered_all_hit_once = 0x3f,
   });
   P_NV9097_SET_REDUCE_COLOR_THRESHOLDS_FP16(p, {
      .all_covered_all_hit_once = 0xff,
   });
   P_NV9097_SET_REDUCE_COLOR_THRESHOLDS_SRGB8(p, {
      .all_covered_all_hit_once = 0xff,
   });

   if (pdev->info.cls_eng3d < VOLTA_A)
      P_IMMD(p, NV9097, SET_ALPHA_FRACTION, 0x3f);

   P_IMMD(p, NV9097, CHECK_SPH_VERSION, {
      .current = 3,
      .oldest_supported = 3,
   });
   P_IMMD(p, NV9097, CHECK_AAM_VERSION, {
      .current = 2,
      .oldest_supported = 2,
   });

   if (pdev->info.cls_eng3d < MAXWELL_A)
      P_IMMD(p, NV9097, SET_SHADER_SCHEDULING, MODE_OLDEST_THREAD_FIRST);

   P_IMMD(p, NV9097, SET_L2_CACHE_CONTROL_FOR_ROP_PREFETCH_READ_REQUESTS,
                     POLICY_EVICT_NORMAL);
   P_IMMD(p, NV9097, SET_L2_CACHE_CONTROL_FOR_ROP_NONINTERLOCKED_READ_REQUESTS,
                     POLICY_EVICT_NORMAL);
   P_IMMD(p, NV9097, SET_L2_CACHE_CONTROL_FOR_ROP_INTERLOCKED_READ_REQUESTS,
                     POLICY_EVICT_NORMAL);
   P_IMMD(p, NV9097, SET_L2_CACHE_CONTROL_FOR_ROP_NONINTERLOCKED_WRITE_REQUESTS,
                     POLICY_EVICT_NORMAL);
   P_IMMD(p, NV9097, SET_L2_CACHE_CONTROL_FOR_ROP_INTERLOCKED_WRITE_REQUESTS,
                     POLICY_EVICT_NORMAL);

   P_IMMD(p, NV9097, SET_BLEND_PER_FORMAT_ENABLE, SNORM8_UNORM16_SNORM16_TRUE);

   P_IMMD(p, NV9097, SET_ATTRIBUTE_DEFAULT, {
      .color_front_diffuse    = COLOR_FRONT_DIFFUSE_VECTOR_0001,
      .color_front_specular   = COLOR_FRONT_SPECULAR_VECTOR_0001,
      .generic_vector         = GENERIC_VECTOR_VECTOR_0001,
      .fixed_fnc_texture      = FIXED_FNC_TEXTURE_VECTOR_0001,
      .dx9_color0             = DX9_COLOR0_VECTOR_0001,
      .dx9_color1_to_color15  = DX9_COLOR1_TO_COLOR15_VECTOR_0000,
   });

   P_IMMD(p, NV9097, SET_DA_OUTPUT, VERTEX_ID_USES_ARRAY_START_TRUE);

   P_IMMD(p, NV9097, SET_RENDER_ENABLE_CONTROL,
                     CONDITIONAL_LOAD_CONSTANT_BUFFER_FALSE);

   P_IMMD(p, NV9097, SET_PS_OUTPUT_SAMPLE_MASK_USAGE, {
      .enable                       = ENABLE_TRUE,
      .qualify_by_anti_alias_enable = QUALIFY_BY_ANTI_ALIAS_ENABLE_ENABLE,
   });

   if (pdev->info.cls_eng3d < VOLTA_A)
      P_IMMD(p, NV9097, SET_PRIM_CIRCULAR_BUFFER_THROTTLE, 0x3fffff);

   P_IMMD(p, NV9097, SET_BLEND_OPT_CONTROL, ALLOW_FLOAT_PIXEL_KILLS_TRUE);
   P_IMMD(p, NV9097, SET_BLEND_FLOAT_OPTION, ZERO_TIMES_ANYTHING_IS_ZERO_TRUE);
   P_IMMD(p, NV9097, SET_BLEND_STATE_PER_TARGET, ENABLE_TRUE);

   if (pdev->info.cls_eng3d < MAXWELL_A)
      P_IMMD(p, NV9097, SET_MAX_TI_WARPS_PER_BATCH, 3);

   if (pdev->info.cls_eng3d >= KEPLER_A &&
       pdev->info.cls_eng3d < MAXWELL_A) {
      P_IMMD(p, NVA097, SET_TEXTURE_INSTRUCTION_OPERAND,
                        ORDERING_KEPLER_ORDER);
   }

   P_IMMD(p, NV9097, SET_ALPHA_TEST, ENABLE_FALSE);
   P_IMMD(p, NV9097, SET_TWO_SIDED_LIGHT, ENABLE_FALSE);
   P_IMMD(p, NV9097, SET_COLOR_CLAMP, ENABLE_TRUE);
   P_IMMD(p, NV9097, SET_PS_SATURATE, {
      .output0 = OUTPUT0_FALSE,
      .output1 = OUTPUT1_FALSE,
      .output2 = OUTPUT2_FALSE,
      .output3 = OUTPUT3_FALSE,
      .output4 = OUTPUT4_FALSE,
      .output5 = OUTPUT5_FALSE,
      .output6 = OUTPUT6_FALSE,
      .output7 = OUTPUT7_FALSE,
   });

   P_IMMD(p, NV9097, SET_POINT_SIZE, fui(1.0));
   P_IMMD(p, NV9097, SET_ATTRIBUTE_POINT_SIZE, { .enable = ENABLE_TRUE });

   /* From vulkan spec's point rasterization:
    * "Point rasterization produces a fragment for each fragment area group of
    * framebuffer pixels with one or more sample points that intersect a region
    * centered at the point’s (xf,yf).
    * This region is a square with side equal to the current point size.
    * ... (xf,yf) is the exact, unrounded framebuffer coordinate of the vertex
    * for the point"
    *
    * So it seems we always need square points with PointCoords like OpenGL
    * point sprites.
    *
    * From OpenGL compatibility spec:
    * Basic point rasterization:
    * "If point sprites are enabled, then point rasterization produces a
    * fragment for each framebuffer pixel whose center lies inside a square
    * centered at the point’s (xw, yw), with side length equal to the current
    * point size.
    * ... and xw and yw are the exact, unrounded window coordinates of the
    * vertex for the point"
    *
    * And Point multisample rasterization:
    * "This region is a circle having diameter equal to the current point width
    * if POINT_SPRITE is disabled, or a square with side equal to the current
    * point width if POINT_SPRITE is enabled."
    */
   P_IMMD(p, NV9097, SET_POINT_SPRITE, ENABLE_TRUE);
   P_IMMD(p, NV9097, SET_POINT_SPRITE_SELECT, {
      .rmode      = RMODE_ZERO,
      .origin     = ORIGIN_TOP,
      .texture0   = TEXTURE0_PASSTHROUGH,
      .texture1   = TEXTURE1_PASSTHROUGH,
      .texture2   = TEXTURE2_PASSTHROUGH,
      .texture3   = TEXTURE3_PASSTHROUGH,
      .texture4   = TEXTURE4_PASSTHROUGH,
      .texture5   = TEXTURE5_PASSTHROUGH,
      .texture6   = TEXTURE6_PASSTHROUGH,
      .texture7   = TEXTURE7_PASSTHROUGH,
      .texture8   = TEXTURE8_PASSTHROUGH,
      .texture9   = TEXTURE9_PASSTHROUGH,
   });

   /* OpenGL's GL_POINT_SMOOTH */
   P_IMMD(p, NV9097, SET_ANTI_ALIASED_POINT, ENABLE_FALSE);

   if (pdev->info.cls_eng3d >= MAXWELL_B)
      P_IMMD(p, NVB197, SET_FILL_VIA_TRIANGLE, MODE_DISABLED);

   P_IMMD(p, NV9097, SET_POLY_SMOOTH, ENABLE_FALSE);

   P_IMMD(p, NV9097, SET_VIEWPORT_PIXEL, CENTER_AT_HALF_INTEGERS);

   P_IMMD(p, NV9097, SET_MME_SHADOW_SCRATCH(NVK_MME_SCRATCH_SHADING_RATE_CONTROL), 0);
   P_IMMD(p, NV9097, SET_MME_SHADOW_SCRATCH(NVK_MME_SCRATCH_ANTI_ALIAS),
          nvk_mme_anti_alias_init());

   /* Enable multisample rasterization even for one sample rasterization,
    * this way we get strict lines and rectangular line support.
    * More info at: DirectX rasterization rules
    */
   P_IMMD(p, NV9097, SET_ANTI_ALIAS_ENABLE, V_TRUE);

   if (pdev->info.cls_eng3d >= MAXWELL_B) {
      P_IMMD(p, NVB197, SET_POST_PS_INITIAL_COVERAGE, true);
      P_IMMD(p, NVB197, SET_OFFSET_RENDER_TARGET_INDEX,
                        BY_VIEWPORT_INDEX_FALSE);
   }

   /* TODO: Vertex runout */

   P_IMMD(p, NV9097, SET_WINDOW_ORIGIN, {
      .mode    = MODE_UPPER_LEFT,
      .flip_y  = FLIP_Y_FALSE,
   });

   P_MTHD(p, NV9097, SET_WINDOW_OFFSET_X);
   P_NV9097_SET_WINDOW_OFFSET_X(p, 0);
   P_NV9097_SET_WINDOW_OFFSET_Y(p, 0);

   P_IMMD(p, NV9097, SET_ACTIVE_ZCULL_REGION, 0x3f);
   P_IMMD(p, NV9097, SET_WINDOW_CLIP_ENABLE, V_FALSE);
   P_IMMD(p, NV9097, SET_CLIP_ID_TEST, ENABLE_FALSE);

//   P_IMMD(p, NV9097, X_X_X_SET_CLEAR_CONTROL, {
//      .respect_stencil_mask   = RESPECT_STENCIL_MASK_FALSE,
//      .use_clear_rect         = USE_CLEAR_RECT_FALSE,
//   });

   P_IMMD(p, NV9097, SET_VIEWPORT_SCALE_OFFSET, ENABLE_TRUE);

   P_IMMD(p, NV9097, SET_VIEWPORT_CLIP_CONTROL, {
      .min_z_zero_max_z_one      = MIN_Z_ZERO_MAX_Z_ONE_FALSE,
      .pixel_min_z               = PIXEL_MIN_Z_CLAMP,
      .pixel_max_z               = PIXEL_MAX_Z_CLAMP,
      .geometry_guardband        = GEOMETRY_GUARDBAND_SCALE_256,
      .line_point_cull_guardband = LINE_POINT_CULL_GUARDBAND_SCALE_256,
      .geometry_clip             = GEOMETRY_CLIP_WZERO_CLIP,
      .geometry_guardband_z      = GEOMETRY_GUARDBAND_Z_SAME_AS_XY_GUARDBAND,
   });

   for (unsigned i = 0; i < 16; i++)
      P_IMMD(p, NV9097, SET_SCISSOR_ENABLE(i), V_FALSE);

   P_IMMD(p, NV9097, SET_CT_MRT_ENABLE, V_TRUE);

   if (pdev->info.cls_eng3d >= TURING_A) {
      /* I don't know what these values actually mean.  I just copied them
       * from the way the blob sets up the hardware.
       */
      P_MTHD(p, NVC597, SET_VARIABLE_PIXEL_RATE_SAMPLE_ORDER(0));
      P_NVC597_SET_VARIABLE_PIXEL_RATE_SAMPLE_ORDER(p, 0, 0xa23eb139);
      P_NVC597_SET_VARIABLE_PIXEL_RATE_SAMPLE_ORDER(p, 1, 0xfb72ea61);
      P_NVC597_SET_VARIABLE_PIXEL_RATE_SAMPLE_ORDER(p, 2, 0xd950c843);
      P_NVC597_SET_VARIABLE_PIXEL_RATE_SAMPLE_ORDER(p, 3, 0x88fac4e5);
      P_NVC597_SET_VARIABLE_PIXEL_RATE_SAMPLE_ORDER(p, 4, 0x1ab3e1b6);
      P_NVC597_SET_VARIABLE_PIXEL_RATE_SAMPLE_ORDER(p, 5, 0xa98fedc2);
      P_NVC597_SET_VARIABLE_PIXEL_RATE_SAMPLE_ORDER(p, 6, 0x2107654b);
      P_NVC597_SET_VARIABLE_PIXEL_RATE_SAMPLE_ORDER(p, 7, 0xe0539773);
      P_NVC597_SET_VARIABLE_PIXEL_RATE_SAMPLE_ORDER(p, 8, 0x698badcf);
      P_NVC597_SET_VARIABLE_PIXEL_RATE_SAMPLE_ORDER(p, 9, 0x71032547);
      P_NVC597_SET_VARIABLE_PIXEL_RATE_SAMPLE_ORDER(p, 10, 0xdef05397);
      P_NVC597_SET_VARIABLE_PIXEL_RATE_SAMPLE_ORDER(p, 11, 0x56789abc);
      P_NVC597_SET_VARIABLE_PIXEL_RATE_SAMPLE_ORDER(p, 12, 0x1234);
   }

   if (pdev->info.cls_eng3d < VOLTA_A) {
      uint64_t shader_base_addr =
         nvk_heap_contiguous_base_address(&dev->shader_heap);

      P_MTHD(p, NV9097, SET_PROGRAM_REGION_A);
      P_NV9097_SET_PROGRAM_REGION_A(p, shader_base_addr >> 32);
      P_NV9097_SET_PROGRAM_REGION_B(p, shader_base_addr);
   }

   for (uint32_t group = 0; group < 5; group++) {
      for (uint32_t slot = 0; slot < 16; slot++) {
         P_IMMD(p, NV9097, BIND_GROUP_CONSTANT_BUFFER(group), {
            .valid = VALID_FALSE,
            .shader_slot = slot,
         });
      }
   }

//   P_MTHD(cmd->push, NVC0_3D, MACRO_GP_SELECT);
//   P_INLINE_DATA(cmd->push, 0x40);
   P_IMMD(p, NV9097, SET_RT_LAYER, {
      .v = 0,
      .control = CONTROL_V_SELECTS_LAYER,
   });
//   P_MTHD(cmd->push, NVC0_3D, MACRO_TEP_SELECT;
//   P_INLINE_DATA(cmd->push, 0x30);

   P_IMMD(p, NV9097, SET_POINT_CENTER_MODE, V_OGL);
   P_IMMD(p, NV9097, SET_EDGE_FLAG, V_TRUE);
   P_IMMD(p, NV9097, SET_SAMPLER_BINDING, V_INDEPENDENTLY);

   uint64_t zero_addr = dev->zero_page->va->addr;
   P_MTHD(p, NV9097, SET_VERTEX_STREAM_SUBSTITUTE_A);
   P_NV9097_SET_VERTEX_STREAM_SUBSTITUTE_A(p, zero_addr >> 32);
   P_NV9097_SET_VERTEX_STREAM_SUBSTITUTE_B(p, zero_addr);

   P_MTHD(p, NV9097, SET_MME_SHADOW_SCRATCH(NVK_MME_SCRATCH_VB_ENABLES));
   P_NV9097_SET_MME_SHADOW_SCRATCH(p, NVK_MME_SCRATCH_VB_ENABLES, 0);
   for (uint32_t b = 0; b < 32; b++) {
      P_IMMD(p, NV9097, SET_VERTEX_STREAM_A_FORMAT(b), {
         .enable = false,
      });
   }

   if (pdev->info.cls_eng3d >= FERMI_A &&
       pdev->info.cls_eng3d < MAXWELL_A) {
      assert(dev->vab_memory);
      uint64_t vab_addr = dev->vab_memory->va->addr;
      P_MTHD(p, NV9097, SET_VAB_MEMORY_AREA_A);
      P_NV9097_SET_VAB_MEMORY_AREA_A(p, vab_addr >> 32);
      P_NV9097_SET_VAB_MEMORY_AREA_B(p, vab_addr);
      P_NV9097_SET_VAB_MEMORY_AREA_C(p, SIZE_BYTES_256K);
   }

   if (pdev->info.cls_eng3d == MAXWELL_A)
      P_IMMD(p, NVB097, SET_SELECT_MAXWELL_TEXTURE_HEADERS, V_TRUE);

   /* Store the address to CB0 in a pair of state registers */
   uint64_t cb0_addr = queue->draw_cb0->va->addr;
   P_MTHD(p, NV9097, SET_MME_SHADOW_SCRATCH(NVK_MME_SCRATCH_CB0_ADDR_HI));
   P_NV9097_SET_MME_SHADOW_SCRATCH(p, NVK_MME_SCRATCH_CB0_ADDR_HI, cb0_addr >> 32);
   P_NV9097_SET_MME_SHADOW_SCRATCH(p, NVK_MME_SCRATCH_CB0_ADDR_LO, cb0_addr);

   /* Store the address to the zero page in a pair of state registers */
   P_MTHD(p, NV9097, SET_MME_SHADOW_SCRATCH(NVK_MME_SCRATCH_ZERO_ADDR_HI));
   P_NV9097_SET_MME_SHADOW_SCRATCH(p, NVK_MME_SCRATCH_ZERO_ADDR_HI, zero_addr >> 32);
   P_NV9097_SET_MME_SHADOW_SCRATCH(p, NVK_MME_SCRATCH_ZERO_ADDR_LO, zero_addr);

   /* We leave CB0 selected by default */
   P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_SELECT_CB0));
   P_INLINE_DATA(p, 0);

   /* Bind CB0 to all shader groups */
   for (uint32_t group = 0; group < 5; group++) {
      P_IMMD(p, NV9097, BIND_GROUP_CONSTANT_BUFFER(group), {
         .valid = VALID_TRUE,
         .shader_slot = 0,
      });
   }

   /* Zero out CB0 */
   P_1INC(p, NV9097, LOAD_CONSTANT_BUFFER_OFFSET);
   P_NV9097_LOAD_CONSTANT_BUFFER_OFFSET(p, 0);
   for (uint32_t dw = 0; dw < NVK_DRAW_CB0_SIZE / 4; dw++)
      P_INLINE_DATA(p, 0);

   /* These are shadowed in cb0 so they need to be zeroed as well for
    * consistency.
    */
   P_IMMD(p, NV9097, SET_GLOBAL_BASE_INSTANCE_INDEX, 0);
   P_MTHD(p, NV9097, SET_MME_SHADOW_SCRATCH(NVK_MME_SCRATCH_CB0_FIRST_VERTEX));
   P_NV9097_SET_MME_SHADOW_SCRATCH(p, NVK_MME_SCRATCH_CB0_FIRST_VERTEX, 0);
   P_NV9097_SET_MME_SHADOW_SCRATCH(p, NVK_MME_SCRATCH_CB0_DRAW_INDEX, 0);
   P_NV9097_SET_MME_SHADOW_SCRATCH(p, NVK_MME_SCRATCH_CB0_VIEW_INDEX, 0);

   return VK_SUCCESS;
}

static void
nvk_cmd_buffer_dirty_render_pass(struct nvk_cmd_buffer *cmd)
{
   struct vk_dynamic_graphics_state *dyn = &cmd->vk.dynamic_graphics_state;

   /* These depend on color attachment count */
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_CB_COLOR_WRITE_ENABLES);
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_CB_BLEND_ENABLES);
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_CB_BLEND_EQUATIONS);
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_CB_WRITE_MASKS);

   /* These depend on the depth/stencil format */
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_TEST_ENABLE);
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_WRITE_ENABLE);
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_ENABLE);
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_TEST_ENABLE);

   /* This may depend on render targets for ESO */
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_MS_RASTERIZATION_SAMPLES);

   /* This may depend on render targets */
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_COLOR_ATTACHMENT_MAP);

   /* Might be required for depthClampZeroOne */
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_RS_DEPTH_CLAMP_ENABLE);
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_RS_DEPTH_CLIP_ENABLE);
}

static void
nvk_cmd_flush_gfx_root_desc(struct nvk_cmd_buffer *cmd,
                            struct nvk_descriptor_state *desc,
                            size_t offset, size_t size)
{
   const uint32_t start_dw = offset / 4;
   const uint32_t end_dw = DIV_ROUND_UP(offset + size, 4);
   const uint32_t len_dw = end_dw - start_dw;

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 2 + len_dw);
   P_1INC(p, NV9097, LOAD_CONSTANT_BUFFER_OFFSET);
   P_NV9097_LOAD_CONSTANT_BUFFER_OFFSET(p, start_dw * 4);

   const uint32_t *root_dw = (uint32_t *)desc->root;
   P_INLINE_ARRAY(p, &root_dw[start_dw], len_dw);
}

void
nvk_cmd_buffer_begin_graphics(struct nvk_cmd_buffer *cmd,
                              const VkCommandBufferBeginInfo *pBeginInfo)
{
   if (cmd->vk.level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
      struct nv_push *p = nvk_cmd_buffer_push(cmd, 5);
      P_MTHD(p, NV9097, INVALIDATE_SAMPLER_CACHE_NO_WFI);
      P_NV9097_INVALIDATE_SAMPLER_CACHE_NO_WFI(p, {
         .lines = LINES_ALL,
      });
      P_NV9097_INVALIDATE_TEXTURE_HEADER_CACHE_NO_WFI(p, {
         .lines = LINES_ALL,
      });

      P_IMMD(p, NVA097, INVALIDATE_SHADER_CACHES_NO_WFI, {
         .constant = CONSTANT_TRUE,
      });
   }

   cmd->state.gfx.descriptors.flush_root = nvk_cmd_flush_gfx_root_desc;

   if (cmd->vk.level != VK_COMMAND_BUFFER_LEVEL_PRIMARY &&
       (pBeginInfo->flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT)) {
      char gcbiar_data[VK_GCBIARR_DATA_SIZE(NVK_MAX_RTS)];
      const VkRenderingInfo *resume_info =
         vk_get_command_buffer_inheritance_as_rendering_resume(cmd->vk.level,
                                                               pBeginInfo,
                                                               gcbiar_data);
      if (resume_info) {
         nvk_CmdBeginRendering(nvk_cmd_buffer_to_handle(cmd), resume_info);
      } else {
         const VkCommandBufferInheritanceRenderingInfo *inheritance_info =
            vk_get_command_buffer_inheritance_rendering_info(cmd->vk.level,
                                                             pBeginInfo);
         assert(inheritance_info);

         struct nvk_rendering_state *render = &cmd->state.gfx.render;
         render->flags = inheritance_info->flags;
         render->area = (VkRect2D) { };
         render->layer_count = 0;
         render->view_mask = inheritance_info->viewMask;
         render->samples = inheritance_info->rasterizationSamples;

         render->color_att_count = inheritance_info->colorAttachmentCount;
         for (uint32_t i = 0; i < render->color_att_count; i++) {
            render->color_att[i].vk_format =
               inheritance_info->pColorAttachmentFormats[i];
         }
         render->depth_att.vk_format =
            inheritance_info->depthAttachmentFormat;
         render->stencil_att.vk_format =
            inheritance_info->stencilAttachmentFormat;

         const VkRenderingAttachmentLocationInfoKHR att_loc_info_default = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_LOCATION_INFO_KHR,
            .colorAttachmentCount = inheritance_info->colorAttachmentCount,
         };
         const VkRenderingAttachmentLocationInfoKHR *att_loc_info =
            vk_get_command_buffer_rendering_attachment_location_info(
               cmd->vk.level, pBeginInfo);
         if (att_loc_info == NULL)
            att_loc_info = &att_loc_info_default;

         vk_cmd_set_rendering_attachment_locations(&cmd->vk, att_loc_info);

         nvk_cmd_buffer_dirty_render_pass(cmd);
      }
   }

   cmd->state.gfx.shaders_dirty = ~0;
}

void
nvk_cmd_invalidate_graphics_state(struct nvk_cmd_buffer *cmd)
{
   vk_dynamic_graphics_state_dirty_all(&cmd->vk.dynamic_graphics_state);

   /* From the Vulkan 1.3.275 spec:
    *
    *    "...There is one exception to this rule - if the primary command
    *    buffer is inside a render pass instance, then the render pass and
    *    subpass state is not disturbed by executing secondary command
    *    buffers."
    *
    * We need to reset everything EXCEPT the render pass state.
    */
   struct nvk_rendering_state render_save = cmd->state.gfx.render;
   memset(&cmd->state.gfx, 0, sizeof(cmd->state.gfx));
   cmd->state.gfx.render = render_save;

   /* We need to keep the flush_root callback */
   cmd->state.gfx.descriptors.flush_root = nvk_cmd_flush_gfx_root_desc;

   cmd->state.gfx.shaders_dirty = ~0;
}

static void
nvk_attachment_init(struct nvk_attachment *att,
                    const VkRenderingAttachmentInfo *info)
{
   if (info == NULL || info->imageView == VK_NULL_HANDLE) {
      *att = (struct nvk_attachment) { .iview = NULL, };
      return;
   }

   VK_FROM_HANDLE(nvk_image_view, iview, info->imageView);
   *att = (struct nvk_attachment) {
      .vk_format = iview->vk.format,
      .iview = iview,
   };

   if (info->resolveMode != VK_RESOLVE_MODE_NONE) {
      VK_FROM_HANDLE(nvk_image_view, res_iview, info->resolveImageView);
      att->resolve_mode = info->resolveMode;
      att->resolve_iview = res_iview;
   }

   att->store_op = info->storeOp;
}

static uint32_t
nil_to_nv9097_samples_mode(enum nil_sample_layout sample_layout)
{
#define MODE(S) [NIL_SAMPLE_LAYOUT_##S] = NV9097_SET_ANTI_ALIAS_SAMPLES_MODE_##S
   uint16_t nil_to_nv9097[] = {
      MODE(1X1),
      MODE(2X1),
      MODE(2X1_D3D),
      MODE(2X2),
      MODE(4X2),
      MODE(4X2_D3D),
      MODE(4X4),
   };
#undef MODE
   assert(sample_layout < ARRAY_SIZE(nil_to_nv9097));
   assert(sample_layout == NIL_SAMPLE_LAYOUT_1X1 ||
          nil_to_nv9097[sample_layout] != 0);

   return nil_to_nv9097[sample_layout];
}

static uint32_t nvk_mme_anti_alias_samples(uint32_t samples);

static void
nvk_cmd_set_sample_layout(struct nvk_cmd_buffer *cmd,
                          enum nil_sample_layout sample_layout)
{
   const uint32_t samples = nil_sample_layout_samples(sample_layout);
   struct nv_push *p = nvk_cmd_buffer_push(cmd, 14);

   P_IMMD(p, NV9097, SET_ANTI_ALIAS,
          nil_to_nv9097_samples_mode(sample_layout));

   switch (sample_layout) {
   case NIL_SAMPLE_LAYOUT_1X1:
   case NIL_SAMPLE_LAYOUT_2X1:
   case NIL_SAMPLE_LAYOUT_2X1_D3D:
      /* These only have two modes: Single-pass or per-sample */
      P_MTHD(p, NV9097, SET_MME_SHADOW_SCRATCH(NVK_MME_SCRATCH_SAMPLE_MASKS_2PASS_0));
      P_INLINE_DATA(p, 0);
      P_INLINE_DATA(p, 0);
      P_INLINE_DATA(p, 0);
      P_INLINE_DATA(p, 0);
      P_MTHD(p, NV9097, SET_MME_SHADOW_SCRATCH(NVK_MME_SCRATCH_SAMPLE_MASKS_4PASS_0));
      P_INLINE_DATA(p, 0);
      P_INLINE_DATA(p, 0);
      P_INLINE_DATA(p, 0);
      P_INLINE_DATA(p, 0);
      break;

   case NIL_SAMPLE_LAYOUT_2X2:
      P_MTHD(p, NV9097, SET_MME_SHADOW_SCRATCH(NVK_MME_SCRATCH_SAMPLE_MASKS_2PASS_0));
      P_INLINE_DATA(p, 0x000a0005);
      P_INLINE_DATA(p, 0x000a0005);
      P_INLINE_DATA(p, 0);
      P_INLINE_DATA(p, 0);
      P_MTHD(p, NV9097, SET_MME_SHADOW_SCRATCH(NVK_MME_SCRATCH_SAMPLE_MASKS_4PASS_0));
      P_INLINE_DATA(p, 0);
      P_INLINE_DATA(p, 0);
      P_INLINE_DATA(p, 0);
      P_INLINE_DATA(p, 0);
      break;

   case NIL_SAMPLE_LAYOUT_4X2:
      P_MTHD(p, NV9097, SET_MME_SHADOW_SCRATCH(NVK_MME_SCRATCH_SAMPLE_MASKS_2PASS_0));
      P_INLINE_DATA(p, 0x000f000f);
      P_INLINE_DATA(p, 0x000f000f);
      P_INLINE_DATA(p, 0x00f000f0);
      P_INLINE_DATA(p, 0x00f000f0);
      P_MTHD(p, NV9097, SET_MME_SHADOW_SCRATCH(NVK_MME_SCRATCH_SAMPLE_MASKS_4PASS_0));
      P_INLINE_DATA(p, 0x00030003);
      P_INLINE_DATA(p, 0x000c000c);
      P_INLINE_DATA(p, 0x00300030);
      P_INLINE_DATA(p, 0x00c000c0);
      break;

   case NIL_SAMPLE_LAYOUT_4X2_D3D:
      P_MTHD(p, NV9097, SET_MME_SHADOW_SCRATCH(NVK_MME_SCRATCH_SAMPLE_MASKS_2PASS_0));
      P_INLINE_DATA(p, 0x003a00c5);
      P_INLINE_DATA(p, 0x003a00c5);
      P_INLINE_DATA(p, 0x003a003a);
      P_INLINE_DATA(p, 0x00c500c5);
      P_MTHD(p, NV9097, SET_MME_SHADOW_SCRATCH(NVK_MME_SCRATCH_SAMPLE_MASKS_4PASS_0));
      P_INLINE_DATA(p, 0x00120081);
      P_INLINE_DATA(p, 0x00280044);
      P_INLINE_DATA(p, 0x00280012);
      P_INLINE_DATA(p, 0x00810044);
      break;

   default:
      unreachable("Unknown sample layout");
   }

   P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_SET_ANTI_ALIAS));
   P_INLINE_DATA(p, nvk_mme_anti_alias_samples(samples));
}

VKAPI_ATTR void VKAPI_CALL
nvk_GetRenderingAreaGranularityKHR(
    VkDevice device,
    const VkRenderingAreaInfoKHR *pRenderingAreaInfo,
    VkExtent2D *pGranularity)
{
   *pGranularity = (VkExtent2D) { .width = 1, .height = 1 };
}

static bool
nvk_rendering_all_linear(const struct nvk_rendering_state *render)
{
   /* Depth and stencil are never linear */
   if (render->depth_att.iview || render->stencil_att.iview)
      return false;

   for (uint32_t i = 0; i < render->color_att_count; i++) {
      const struct nvk_image_view *iview = render->color_att[i].iview;
      if (iview == NULL)
         continue;

      const struct nvk_image *image = (struct nvk_image *)iview->vk.image;
      const uint8_t ip = iview->planes[0].image_plane;
      const struct nil_image_level *level =
         &image->planes[ip].nil.levels[iview->vk.base_mip_level];

      if (level->tiling.gob_type != NIL_GOB_TYPE_LINEAR)
         return false;
   }

   return true;
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdBeginRendering(VkCommandBuffer commandBuffer,
                      const VkRenderingInfo *pRenderingInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   struct nvk_rendering_state *render = &cmd->state.gfx.render;

   memset(render, 0, sizeof(*render));

   render->flags = pRenderingInfo->flags;
   render->area = pRenderingInfo->renderArea;
   render->view_mask = pRenderingInfo->viewMask;
   render->layer_count = pRenderingInfo->layerCount;
   render->samples = 0;

   const uint32_t layer_count =
      render->view_mask ? util_last_bit(render->view_mask) :
                          render->layer_count;

   render->color_att_count = pRenderingInfo->colorAttachmentCount;
   for (uint32_t i = 0; i < render->color_att_count; i++) {
      nvk_attachment_init(&render->color_att[i],
                          &pRenderingInfo->pColorAttachments[i]);
   }

   nvk_attachment_init(&render->depth_att,
                       pRenderingInfo->pDepthAttachment);
   nvk_attachment_init(&render->stencil_att,
                       pRenderingInfo->pStencilAttachment);

   const VkRenderingFragmentShadingRateAttachmentInfoKHR *fsr_att_info =
      vk_find_struct_const(pRenderingInfo->pNext,
                           RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR);
   if (fsr_att_info != NULL && fsr_att_info->imageView != VK_NULL_HANDLE) {
      VK_FROM_HANDLE(nvk_image_view, iview, fsr_att_info->imageView);
      render->fsr_att = (struct nvk_attachment) {
         .vk_format = iview->vk.format,
         .iview = iview,
         .store_op = VK_ATTACHMENT_STORE_OP_NONE,
      };
   }

   render->all_linear = nvk_rendering_all_linear(render);

   const VkRenderingAttachmentLocationInfoKHR ral_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_LOCATION_INFO_KHR,
      .colorAttachmentCount = pRenderingInfo->colorAttachmentCount,
   };
   vk_cmd_set_rendering_attachment_locations(&cmd->vk, &ral_info);

   nvk_cmd_buffer_dirty_render_pass(cmd);

   struct nv_push *p = nvk_cmd_buffer_push(cmd, NVK_MAX_RTS * 12 + 34);

   P_IMMD(p, NV9097, SET_MME_SHADOW_SCRATCH(NVK_MME_SCRATCH_VIEW_MASK),
          render->view_mask);

   P_MTHD(p, NV9097, SET_SURFACE_CLIP_HORIZONTAL);
   P_NV9097_SET_SURFACE_CLIP_HORIZONTAL(p, {
      .x       = render->area.offset.x,
      .width   = render->area.extent.width,
   });
   P_NV9097_SET_SURFACE_CLIP_VERTICAL(p, {
      .y       = render->area.offset.y,
      .height  = render->area.extent.height,
   });

   enum nil_sample_layout sample_layout = NIL_SAMPLE_LAYOUT_INVALID;

   /* We always emit SET_COLOR_TARGET_A(i) for every color target, regardless
    * of the number of targets in the render pass.  This ensures that we have
    * no left over pointers from previous render passes in the hardware.  This
    * also allows us to point at any render target with SET_CT_SELECT and know
    * that it's either a valid render target or NULL.
    */
   for (uint32_t i = 0; i < NVK_MAX_RTS; i++) {
      if (render->color_att[i].iview) {
         const struct nvk_image_view *iview = render->color_att[i].iview;
         const struct nvk_image *image = (struct nvk_image *)iview->vk.image;
         /* Rendering to multi-planar images is valid for a specific single
          * plane only, so assert that what we have is a single-plane, obtain
          * its index, and begin rendering
          */
         assert(iview->plane_count == 1);
         const uint8_t ip = iview->planes[0].image_plane;
         const struct nvk_image_plane *plane = &image->planes[ip];

         if (!render->all_linear &&
             plane->nil.levels[0].tiling.gob_type == NIL_GOB_TYPE_LINEAR)
            plane = &image->linear_tiled_shadow;

         const struct nil_image *nil_image = &plane->nil;
         const struct nil_image_level *level =
            &nil_image->levels[iview->vk.base_mip_level];
         struct nil_Extent4D_Samples level_extent_sa =
            nil_image_level_extent_sa(nil_image, iview->vk.base_mip_level);

         assert(sample_layout == NIL_SAMPLE_LAYOUT_INVALID ||
                sample_layout == nil_image->sample_layout);
         sample_layout = nil_image->sample_layout;
         render->samples = image->vk.samples;

         uint64_t addr = nvk_image_plane_base_address(plane) + level->offset_B;

         if (nil_image->dim == NIL_IMAGE_DIM_3D) {
            addr += nil_image_level_z_offset_B(nil_image,
                                               iview->vk.base_mip_level,
                                               iview->vk.base_array_layer);
            assert(layer_count <= iview->vk.extent.depth);
         } else {
            addr += iview->vk.base_array_layer *
                    (uint64_t)nil_image->array_stride_B;
            assert(layer_count <= iview->vk.layer_count);
         }

         P_MTHD(p, NV9097, SET_COLOR_TARGET_A(i));
         P_NV9097_SET_COLOR_TARGET_A(p, i, addr >> 32);
         P_NV9097_SET_COLOR_TARGET_B(p, i, addr);

         if (level->tiling.gob_type != NIL_GOB_TYPE_LINEAR) {
            const enum pipe_format p_format =
               nvk_format_to_pipe_format(iview->vk.format);

            /* We use the stride for depth/stencil targets because the Z/S
             * hardware has no concept of a tile width.  Instead, we just set
             * the width to the stride divided by bpp.
             */
            const uint32_t row_stride_el =
               level->row_stride_B / util_format_get_blocksize(p_format);
            P_NV9097_SET_COLOR_TARGET_WIDTH(p, i, row_stride_el);
            P_NV9097_SET_COLOR_TARGET_HEIGHT(p, i, level_extent_sa.height);
            const uint8_t ct_format = nil_format_to_color_target(p_format);
            P_NV9097_SET_COLOR_TARGET_FORMAT(p, i, ct_format);

            P_NV9097_SET_COLOR_TARGET_MEMORY(p, i, {
               .block_width   = BLOCK_WIDTH_ONE_GOB,
               .block_height  = level->tiling.y_log2,
               .block_depth   = level->tiling.z_log2,
               .layout        = LAYOUT_BLOCKLINEAR,
               .third_dimension_control = (nil_image->dim == NIL_IMAGE_DIM_3D) ?
                  THIRD_DIMENSION_CONTROL_THIRD_DIMENSION_DEFINES_DEPTH_SIZE :
                  THIRD_DIMENSION_CONTROL_THIRD_DIMENSION_DEFINES_ARRAY_SIZE,
            });

            P_NV9097_SET_COLOR_TARGET_THIRD_DIMENSION(p, i, layer_count);
            P_NV9097_SET_COLOR_TARGET_ARRAY_PITCH(p, i,
               nil_image->array_stride_B >> 2);
            P_NV9097_SET_COLOR_TARGET_LAYER(p, i, 0);
         } else {
            /* NVIDIA can only render to 2D linear images */
            assert(nil_image->dim == NIL_IMAGE_DIM_2D);
            /* NVIDIA can only render to non-multisampled images */
            assert(sample_layout == NIL_SAMPLE_LAYOUT_1X1);
            /* NVIDIA doesn't support linear array images */
            assert(iview->vk.base_array_layer == 0 && layer_count == 1);

            uint32_t pitch = level->row_stride_B;
            const enum pipe_format p_format =
               nvk_format_to_pipe_format(iview->vk.format);
            /* When memory layout is set to LAYOUT_PITCH, the WIDTH field
             * takes row pitch
             */
            P_NV9097_SET_COLOR_TARGET_WIDTH(p, i, pitch);
            P_NV9097_SET_COLOR_TARGET_HEIGHT(p, i, level_extent_sa.height);

            const uint8_t ct_format = nil_format_to_color_target(p_format);
            P_NV9097_SET_COLOR_TARGET_FORMAT(p, i, ct_format);

            P_NV9097_SET_COLOR_TARGET_MEMORY(p, i, {
               .layout = LAYOUT_PITCH,
               .third_dimension_control =
                  THIRD_DIMENSION_CONTROL_THIRD_DIMENSION_DEFINES_ARRAY_SIZE,
            });

            P_NV9097_SET_COLOR_TARGET_THIRD_DIMENSION(p, i, 1);
            P_NV9097_SET_COLOR_TARGET_ARRAY_PITCH(p, i, 0);
            P_NV9097_SET_COLOR_TARGET_LAYER(p, i, 0);
         }

         P_IMMD(p, NV9097, SET_COLOR_COMPRESSION(i), nil_image->compressed);
      } else {
         P_MTHD(p, NV9097, SET_COLOR_TARGET_A(i));
         P_NV9097_SET_COLOR_TARGET_A(p, i, 0);
         P_NV9097_SET_COLOR_TARGET_B(p, i, 0);
         P_NV9097_SET_COLOR_TARGET_WIDTH(p, i, 64);
         P_NV9097_SET_COLOR_TARGET_HEIGHT(p, i, 0);
         P_NV9097_SET_COLOR_TARGET_FORMAT(p, i, V_DISABLED);
         P_NV9097_SET_COLOR_TARGET_MEMORY(p, i, {
            .layout        = LAYOUT_BLOCKLINEAR,
         });
         P_NV9097_SET_COLOR_TARGET_THIRD_DIMENSION(p, i, layer_count);
         P_NV9097_SET_COLOR_TARGET_ARRAY_PITCH(p, i, 0);
         P_NV9097_SET_COLOR_TARGET_LAYER(p, i, 0);

         P_IMMD(p, NV9097, SET_COLOR_COMPRESSION(i), ENABLE_TRUE);
      }
   }

   if (render->depth_att.iview || render->stencil_att.iview) {
      struct nvk_image_view *iview = render->depth_att.iview ?
                                     render->depth_att.iview :
                                     render->stencil_att.iview;
      const struct nvk_image *image = (struct nvk_image *)iview->vk.image;
      /* Depth/stencil are always single-plane */
      assert(iview->plane_count == 1);
      const uint8_t ip = iview->planes[0].image_plane;
      struct nil_image nil_image = image->planes[ip].nil;

      uint64_t addr = nvk_image_base_address(image, ip);
      uint32_t mip_level = iview->vk.base_mip_level;
      uint32_t base_array_layer = iview->vk.base_array_layer;

      if (nil_image.dim == NIL_IMAGE_DIM_3D) {
         uint64_t level_offset_B;
         nil_image = nil_image_3d_level_as_2d_array(&nil_image, mip_level,
                                                    &level_offset_B);
         addr += level_offset_B;
         mip_level = 0;
         base_array_layer = 0;
         assert(layer_count <= iview->vk.extent.depth);
      } else {
         assert(layer_count <= iview->vk.layer_count);
      }

      const struct nil_image_level *level = &nil_image.levels[mip_level];
      addr += level->offset_B;

      assert(sample_layout == NIL_SAMPLE_LAYOUT_INVALID ||
             sample_layout == nil_image.sample_layout);
      sample_layout = nil_image.sample_layout;
      render->samples = image->vk.samples;

      P_MTHD(p, NV9097, SET_ZT_A);
      P_NV9097_SET_ZT_A(p, addr >> 32);
      P_NV9097_SET_ZT_B(p, addr);
      const enum pipe_format p_format =
         nvk_format_to_pipe_format(iview->vk.format);
      const uint8_t zs_format = nil_format_to_depth_stencil(p_format);
      P_NV9097_SET_ZT_FORMAT(p, zs_format);
      assert(level->tiling.gob_type != NIL_GOB_TYPE_LINEAR);
      assert(level->tiling.z_log2 == 0);
      P_NV9097_SET_ZT_BLOCK_SIZE(p, {
         .width = WIDTH_ONE_GOB,
         .height = level->tiling.y_log2,
         .depth = DEPTH_ONE_GOB,
      });
      P_NV9097_SET_ZT_ARRAY_PITCH(p, nil_image.array_stride_B >> 2);

      P_IMMD(p, NV9097, SET_ZT_SELECT, 1 /* target_count */);

      struct nil_Extent4D_Samples level_extent_sa =
         nil_image_level_extent_sa(&nil_image, mip_level);

      /* We use the stride for depth/stencil targets because the Z/S hardware
       * has no concept of a tile width.  Instead, we just set the width to
       * the stride divided by bpp.
       */
      const uint32_t row_stride_el =
         level->row_stride_B / util_format_get_blocksize(p_format);

      P_MTHD(p, NV9097, SET_ZT_SIZE_A);
      P_NV9097_SET_ZT_SIZE_A(p, row_stride_el);
      P_NV9097_SET_ZT_SIZE_B(p, level_extent_sa.height);
      P_NV9097_SET_ZT_SIZE_C(p, {
         .third_dimension  = base_array_layer + layer_count,
         .control          = CONTROL_THIRD_DIMENSION_DEFINES_ARRAY_SIZE,
      });

      P_IMMD(p, NV9097, SET_ZT_LAYER, base_array_layer);

      P_IMMD(p, NV9097, SET_Z_COMPRESSION, nil_image.compressed);

      if (nvk_cmd_buffer_3d_cls(cmd) >= MAXWELL_B) {
         P_IMMD(p, NVC597, SET_ZT_SPARSE, {
            .enable = ENABLE_FALSE,
         });
      }
   } else {
      P_IMMD(p, NV9097, SET_ZT_SELECT, 0 /* target_count */);
   }

   if (render->fsr_att.iview) {
      const struct nvk_image_view *iview = render->fsr_att.iview;
      const struct nvk_image *image = (struct nvk_image *)iview->vk.image;

      /* Fragment shading rate images are always single-plane */
      assert(iview->plane_count == 1);
      const uint8_t ip = iview->planes[0].image_plane;
      const struct nil_image *nil_image = &image->planes[ip].nil;

      /* Fragment shading rate images are always 2D */
      assert(nil_image->dim == NIL_IMAGE_DIM_2D);
      assert(nil_image->sample_layout == NIL_SAMPLE_LAYOUT_1X1);

      uint64_t addr = nvk_image_base_address(image, ip);
      uint32_t mip_level = iview->vk.base_mip_level;
      struct nil_Extent4D_Samples level_extent_sa =
         nil_image_level_extent_sa(nil_image, mip_level);

      const struct nil_image_level *level = &nil_image->levels[mip_level];
      addr += level->offset_B;

      P_MTHD(p, NVC597, SET_SHADING_RATE_INDEX_SURFACE_ADDRESS_A(0));
      P_NVC597_SET_SHADING_RATE_INDEX_SURFACE_ADDRESS_A(p, 0, addr >> 32);
      P_NVC597_SET_SHADING_RATE_INDEX_SURFACE_ADDRESS_B(p, 0, addr);
      P_NVC597_SET_SHADING_RATE_INDEX_SURFACE_SIZE_A(p, 0, {
         .width = level_extent_sa.width,
         .height = level_extent_sa.height,
      });
      P_NVC597_SET_SHADING_RATE_INDEX_SURFACE_SIZE_B(p, 0,
         iview->vk.layer_count + iview->vk.base_array_layer);
      P_NVC597_SET_SHADING_RATE_INDEX_SURFACE_LAYER(p, 0,
         iview->vk.base_array_layer);
      P_NVC597_SET_SHADING_RATE_INDEX_SURFACE_ARRAY_PITCH(p, 0,
         nil_image->array_stride_B >> 2);
      assert(level->tiling.gob_type != NIL_GOB_TYPE_LINEAR);
      assert(level->tiling.z_log2 == 0);
      P_NVC597_SET_SHADING_RATE_INDEX_SURFACE_BLOCK_SIZE(p, 0, {
         .width = WIDTH_ONE_GOB,
         .height = level->tiling.y_log2,
         .depth = DEPTH_ONE_GOB,
      });

      const enum pipe_format p_format =
         nvk_format_to_pipe_format(iview->vk.format);
      const uint32_t row_stride_el =
         level->row_stride_B / util_format_get_blocksize(p_format);
      P_NVC597_SET_SHADING_RATE_INDEX_SURFACE_ALLOCATED_SIZE(p, 0,
         row_stride_el);
   } else {
      P_MTHD(p, NVC597, SET_SHADING_RATE_INDEX_SURFACE_ADDRESS_A(0));
      P_NVC597_SET_SHADING_RATE_INDEX_SURFACE_ADDRESS_A(p, 0, 0);
      P_NVC597_SET_SHADING_RATE_INDEX_SURFACE_ADDRESS_B(p, 0, 0);
      P_NVC597_SET_SHADING_RATE_INDEX_SURFACE_SIZE_A(p, 0, { });
      P_NVC597_SET_SHADING_RATE_INDEX_SURFACE_SIZE_B(p, 0, 0);
      P_NVC597_SET_SHADING_RATE_INDEX_SURFACE_LAYER(p, 0, 0);
      P_NVC597_SET_SHADING_RATE_INDEX_SURFACE_ARRAY_PITCH(p, 0, 0);
      P_NVC597_SET_SHADING_RATE_INDEX_SURFACE_BLOCK_SIZE(p, 0, { });
      P_NVC597_SET_SHADING_RATE_INDEX_SURFACE_ALLOCATED_SIZE(p, 0, 0);
   }

   /* From the Vulkan 1.3.275 spec:
    *
    *    "It is legal for a subpass to use no color or depth/stencil
    *    attachments, either because it has no attachment references or
    *    because all of them are VK_ATTACHMENT_UNUSED. This kind of subpass
    *    can use shader side effects such as image stores and atomics to
    *    produce an output. In this case, the subpass continues to use the
    *    width, height, and layers of the framebuffer to define the dimensions
    *    of the rendering area, and the rasterizationSamples from each
    *    pipeline’s VkPipelineMultisampleStateCreateInfo to define the number
    *    of samples used in rasterization;"
    *
    * In the case where we have attachments, we emit SET_ANTI_ALIAS here
    * because SET_COLOR_TARGET_* and SET_ZT_* don't have any other way of
    * specifying the sample layout and we want to ensure it matches.  When
    * we don't have any attachments, we defer SET_ANTI_ALIAS to draw time
    * where we base it on dynamic rasterizationSamples.
    */
   if (sample_layout != NIL_SAMPLE_LAYOUT_INVALID)
      nvk_cmd_set_sample_layout(cmd, sample_layout);

   if (render->flags & VK_RENDERING_RESUMING_BIT)
      return;

   for (uint32_t i = 0; i < pRenderingInfo->colorAttachmentCount; i++) {
      const struct nvk_image_view *iview = render->color_att[i].iview;
      if (iview == NULL)
         continue;

      const struct nvk_image *image = (struct nvk_image *)iview->vk.image;
      assert(iview->plane_count == 1);
      const uint8_t ip = iview->planes[0].image_plane;
      const struct nvk_image_plane *plane = &image->planes[ip];

      const VkAttachmentLoadOp load_op =
         pRenderingInfo->pColorAttachments[i].loadOp;
      if (!render->all_linear &&
          plane->nil.levels[0].tiling.gob_type == NIL_GOB_TYPE_LINEAR &&
          load_op == VK_ATTACHMENT_LOAD_OP_LOAD)
         nvk_linear_render_copy(cmd, iview, render->area, true);
   }

   uint32_t clear_count = 0;
   VkClearAttachment clear_att[NVK_MAX_RTS + 1];
   for (uint32_t i = 0; i < pRenderingInfo->colorAttachmentCount; i++) {
      const VkRenderingAttachmentInfo *att_info =
         &pRenderingInfo->pColorAttachments[i];
      if (att_info->imageView == VK_NULL_HANDLE ||
          att_info->loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR)
         continue;

      clear_att[clear_count++] = (VkClearAttachment) {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .colorAttachment = i,
         .clearValue = att_info->clearValue,
      };
   }

   clear_att[clear_count] = (VkClearAttachment) { .aspectMask = 0, };
   if (pRenderingInfo->pDepthAttachment != NULL &&
       pRenderingInfo->pDepthAttachment->imageView != VK_NULL_HANDLE &&
       pRenderingInfo->pDepthAttachment->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
      clear_att[clear_count].aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
      clear_att[clear_count].clearValue.depthStencil.depth =
         pRenderingInfo->pDepthAttachment->clearValue.depthStencil.depth;
   }
   if (pRenderingInfo->pStencilAttachment != NULL &&
       pRenderingInfo->pStencilAttachment->imageView != VK_NULL_HANDLE &&
       pRenderingInfo->pStencilAttachment->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
      clear_att[clear_count].aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
      clear_att[clear_count].clearValue.depthStencil.stencil =
         pRenderingInfo->pStencilAttachment->clearValue.depthStencil.stencil;
   }
   if (clear_att[clear_count].aspectMask != 0)
      clear_count++;

   if (clear_count > 0) {
      const VkClearRect clear_rect = {
         .rect = render->area,
         .baseArrayLayer = 0,
         .layerCount = render->view_mask ? 1 : render->layer_count,
      };

      p = nvk_cmd_buffer_push(cmd, 2);
      P_MTHD(p, NV9097, SET_RENDER_ENABLE_OVERRIDE);
      P_NV9097_SET_RENDER_ENABLE_OVERRIDE(p, MODE_ALWAYS_RENDER);

      nvk_CmdClearAttachments(nvk_cmd_buffer_to_handle(cmd),
                              clear_count, clear_att, 1, &clear_rect);
      p = nvk_cmd_buffer_push(cmd, 2);
      P_MTHD(p, NV9097, SET_RENDER_ENABLE_OVERRIDE);
      P_NV9097_SET_RENDER_ENABLE_OVERRIDE(p, MODE_USE_RENDER_ENABLE);
   }

   /* TODO: Attachment clears */
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdEndRendering(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   struct nvk_rendering_state *render = &cmd->state.gfx.render;

   if (!(render->flags & VK_RENDERING_SUSPENDING_BIT)) {
      for (uint32_t i = 0; i < render->color_att_count; i++) {
         struct nvk_image_view *iview = render->color_att[i].iview;
         if (iview == NULL)
            continue;

         struct nvk_image *image = (struct nvk_image *)iview->vk.image;
         const uint8_t ip = iview->planes[0].image_plane;
         const struct nvk_image_plane *plane = &image->planes[ip];
         if (!render->all_linear &&
             plane->nil.levels[0].tiling.gob_type == NIL_GOB_TYPE_LINEAR &&
             render->color_att[i].store_op == VK_ATTACHMENT_STORE_OP_STORE)
            nvk_linear_render_copy(cmd, iview, render->area, false);
      }
   }

   bool need_resolve = false;

   /* Translate render state back to VK for meta */
   VkRenderingAttachmentInfo vk_color_att[NVK_MAX_RTS];
   for (uint32_t i = 0; i < render->color_att_count; i++) {
      if (render->color_att[i].resolve_mode != VK_RESOLVE_MODE_NONE)
         need_resolve = true;

      vk_color_att[i] = (VkRenderingAttachmentInfo) {
         .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
         .imageView = nvk_image_view_to_handle(render->color_att[i].iview),
         .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
         .resolveMode = render->color_att[i].resolve_mode,
         .resolveImageView =
            nvk_image_view_to_handle(render->color_att[i].resolve_iview),
         .resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
      };
   }

   const VkRenderingAttachmentInfo vk_depth_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = nvk_image_view_to_handle(render->depth_att.iview),
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
      .resolveMode = render->depth_att.resolve_mode,
      .resolveImageView =
         nvk_image_view_to_handle(render->depth_att.resolve_iview),
      .resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
   };
   if (render->depth_att.resolve_mode != VK_RESOLVE_MODE_NONE)
      need_resolve = true;

   const VkRenderingAttachmentInfo vk_stencil_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = nvk_image_view_to_handle(render->stencil_att.iview),
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
      .resolveMode = render->stencil_att.resolve_mode,
      .resolveImageView =
         nvk_image_view_to_handle(render->stencil_att.resolve_iview),
      .resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
   };
   if (render->stencil_att.resolve_mode != VK_RESOLVE_MODE_NONE)
      need_resolve = true;

   const VkRenderingInfo vk_render = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = render->area,
      .layerCount = render->layer_count,
      .viewMask = render->view_mask,
      .colorAttachmentCount = render->color_att_count,
      .pColorAttachments = vk_color_att,
      .pDepthAttachment = &vk_depth_att,
      .pStencilAttachment = &vk_stencil_att,
   };

   if (render->flags & VK_RENDERING_SUSPENDING_BIT)
      need_resolve = false;

   memset(render, 0, sizeof(*render));

   if (need_resolve) {
      struct nv_push *p = nvk_cmd_buffer_push(cmd, 2);
      P_IMMD(p, NVA097, INVALIDATE_TEXTURE_DATA_CACHE, {
         .lines = LINES_ALL,
      });

      nvk_meta_resolve_rendering(cmd, &vk_render);
   }
}

void
nvk_cmd_bind_graphics_shader(struct nvk_cmd_buffer *cmd,
                             const gl_shader_stage stage,
                             struct nvk_shader *shader)
{
   assert(stage < ARRAY_SIZE(cmd->state.gfx.shaders));
   if (cmd->state.gfx.shaders[stage] == shader)
      return;

   cmd->state.gfx.shaders[stage] = shader;
   cmd->state.gfx.shaders_dirty |= mesa_to_vk_shader_stage(stage);
}

uint32_t
nvk_mme_tess_params(enum nak_ts_domain domain,
                    enum nak_ts_spacing spacing,
                    enum nak_ts_prims prims)
{
   /* This is laid out the same as SET_TESSELLATION_PARAMETERS, only with an
    * extra bit for lower_left
    */
   uint16_t params = ((uint16_t)domain << 0) |
                     ((uint16_t)spacing << 4) |
                     ((uint16_t)prims << 8);
   return nvk_mme_val_mask(params, 0x0fff);
}

static uint32_t
nvk_mme_tess_lower_left(bool lower_left)
{
   return nvk_mme_val_mask((uint16_t)lower_left << 12, 1u << 12);
}

void
nvk_mme_set_tess_params(struct mme_builder *b)
{
   struct mme_value val_mask = mme_load(b);
   struct mme_value old_params = nvk_mme_load_scratch(b, TESS_PARAMS);
   struct mme_value params = nvk_mme_set_masked(b, old_params, val_mask);
   mme_free_reg(b, val_mask);

   mme_if(b, ine, params, old_params) {
      nvk_mme_store_scratch(b, TESS_PARAMS, params);

      /* lower_left lives at bit 12 */
      struct mme_value lower_left = mme_merge(b, mme_zero(), params, 0, 1, 12);

      /* Only the bottom 12 bits are valid to put in HW */
      mme_merge_to(b, params, mme_zero(), params, 0, 12, 0);

      /* If we're using a lower-left orientation, we need to flip triangles
       * between CW and CCW.
       */
      mme_if(b, ine, lower_left, mme_zero()) {
         struct mme_value prims_cw = mme_imm(NAK_TS_PRIMS_TRIANGLES_CW);
         struct mme_value prims_ccw = mme_imm(NAK_TS_PRIMS_TRIANGLES_CCW);

         struct mme_value prims = mme_merge(b, mme_zero(), params, 0, 4, 8);
         mme_if(b, ieq, prims, prims_cw) {
            mme_merge_to(b, params, params, prims_ccw, 8, 4, 0);
         }
         mme_if(b, ieq, prims, prims_ccw) {
            mme_merge_to(b, params, params, prims_cw, 8, 4, 0);
         }
         mme_free_reg(b, prims);
      }
      mme_free_reg(b, lower_left);

      mme_mthd(b, NV9097_SET_TESSELLATION_PARAMETERS);
      mme_emit(b, params);
   }
}

const struct nvk_mme_test_case nvk_mme_set_tess_params_tests[] = {{
   /* This case doesn't change the state so it should do nothing */
   .init = (struct nvk_mme_mthd_data[]) {
      { NVK_SET_MME_SCRATCH(TESS_PARAMS), 0 },
      { }
   },
   .params = (uint32_t[]) { 0xffff0000 },
   .expected = (struct nvk_mme_mthd_data[]) {
      { }
   },
}, {
   /* TRIANGLE, INTEGER, TRIANGLES_CW, lower_left = false */
   .init = (struct nvk_mme_mthd_data[]) {
      { NVK_SET_MME_SCRATCH(TESS_PARAMS), 0 },
      { }
   },
   .params = (uint32_t[]) { 0xffff0201 },
   .expected = (struct nvk_mme_mthd_data[]) {
      { NVK_SET_MME_SCRATCH(TESS_PARAMS), 0x0201 },
      { NV9097_SET_TESSELLATION_PARAMETERS, 0x0201 },
      { }
   },
}, {
   /* TRIANGLE, INTEGER, TRIANGLES_CW, lower_left = true */
   .init = (struct nvk_mme_mthd_data[]) {
      { NVK_SET_MME_SCRATCH(TESS_PARAMS), 0x0201 },
      { }
   },
   .params = (uint32_t[]) { 0x10001000 },
   .expected = (struct nvk_mme_mthd_data[]) {
      { NVK_SET_MME_SCRATCH(TESS_PARAMS), 0x1201 },
      { NV9097_SET_TESSELLATION_PARAMETERS, 0x0301 },
      { }
   },
}, {
   /* TRIANGLE, INTEGER, TRIANGLES_CCW, lower_left = true */
   .init = (struct nvk_mme_mthd_data[]) {
      { NVK_SET_MME_SCRATCH(TESS_PARAMS), 0x0301 },
      { }
   },
   .params = (uint32_t[]) { 0x10001000 },
   .expected = (struct nvk_mme_mthd_data[]) {
      { NVK_SET_MME_SCRATCH(TESS_PARAMS), 0x1301 },
      { NV9097_SET_TESSELLATION_PARAMETERS, 0x0201 },
      { }
   },
}, {}};

void
nvk_cmd_flush_gfx_shaders(struct nvk_cmd_buffer *cmd)
{
   if (cmd->state.gfx.shaders_dirty == 0)
      return;

   /* Map shader types to shaders */
   struct nvk_shader *type_shader[6] = { NULL, };
   uint32_t types_dirty = 0;

   u_foreach_bit(s, cmd->state.gfx.shaders_dirty &
                    NVK_SHADER_STAGE_GRAPHICS_BITS) {
      gl_shader_stage stage = vk_to_mesa_shader_stage(1 << s);
      uint32_t type = mesa_to_nv9097_shader_type(stage);
      types_dirty |= BITFIELD_BIT(type);

      /* Only copy non-NULL shaders because mesh/task alias with vertex and
       * tessellation stages.
       */
      struct nvk_shader *shader = cmd->state.gfx.shaders[stage];
      if (shader != NULL) {
         assert(type < ARRAY_SIZE(type_shader));
         assert(type_shader[type] == NULL);
         type_shader[type] = shader;

         const struct nvk_cbuf_map *cbuf_map = &shader->cbuf_map;
         struct nvk_cbuf_group *cbuf_group =
            &cmd->state.gfx.cbuf_groups[nvk_cbuf_binding_for_stage(stage)];
         for (uint32_t i = 0; i < cbuf_map->cbuf_count; i++) {
            if (memcmp(&cbuf_group->cbufs[i], &cbuf_map->cbufs[i],
                       sizeof(cbuf_group->cbufs[i])) != 0) {
               cbuf_group->cbufs[i] = cbuf_map->cbufs[i];
               cbuf_group->dirty |= BITFIELD_BIT(i);
            }
         }
      }
   }

   u_foreach_bit(type, types_dirty) {
      struct nvk_shader *shader = type_shader[type];
      if (shader == NULL) {
         struct nv_push *p = nvk_cmd_buffer_push(cmd, 2);
         P_IMMD(p, NV9097, SET_PIPELINE_SHADER(type), {
            .enable  = ENABLE_FALSE,
            .type    = type,
         });
      } else {
         struct nv_push *p = nvk_cmd_buffer_push(cmd, shader->push_dw_count);
         nv_push_raw(p, shader->push_dw, shader->push_dw_count);
      }
   }

   if (cmd->state.gfx.shaders_dirty & NVK_SHADER_STAGE_VTGM_BITS) {
      struct nvk_shader *last_vtgm = NULL;
      u_foreach_bit(s, NVK_SHADER_STAGE_VTGM_BITS) {
         gl_shader_stage stage = vk_to_mesa_shader_stage(1 << s);
         if (cmd->state.gfx.shaders[stage] != NULL)
            last_vtgm = cmd->state.gfx.shaders[stage];
      }

      assert(last_vtgm->vtgm_push_dw_count > last_vtgm->push_dw_count);
      const uint16_t dw_start = last_vtgm->push_dw_count;
      const uint16_t dw_count = last_vtgm->vtgm_push_dw_count - dw_start;
      struct nv_push *p = nvk_cmd_buffer_push(cmd, dw_count);
      nv_push_raw(p, &last_vtgm->push_dw[dw_start], dw_count);
   }

   cmd->state.gfx.shaders_dirty = 0;
}

void
nvk_mme_set_vb_enables(struct mme_builder *b)
{
   struct mme_value enables = mme_load(b);
   struct mme_value old_enables = nvk_mme_load_scratch(b, VB_ENABLES);
   nvk_mme_store_scratch(b, VB_ENABLES, enables);

   struct mme_value changed = mme_xor(b, enables, old_enables);
   mme_free_reg(b, old_enables);

   struct mme_value vb_idx4 = mme_mov(b, mme_zero());
   mme_while(b, ine, changed, mme_zero()) {
      mme_if(b, ine, mme_and(b, changed, mme_imm(1)), mme_zero()) {
         struct mme_value state =
            mme_state_arr(b, NV9097_SET_VERTEX_STREAM_A_FORMAT(0), vb_idx4);
         mme_merge_to(b, state, state, enables, 12, 1, 0);
         mme_mthd_arr(b, NV9097_SET_VERTEX_STREAM_A_FORMAT(0), vb_idx4);
         mme_emit(b, state);
      }
      mme_add_to(b, vb_idx4, vb_idx4, mme_imm(4));
      mme_srl_to(b, changed, changed, mme_imm(1));
      mme_srl_to(b, enables, enables, mme_imm(1));
   }
}

static uint32_t
nvk_mme_vb_stride(uint32_t vb_idx, uint32_t stride)
{
   assert(stride < (1 << 12));
   assert(vb_idx < (1 << 5));
   return (vb_idx << 16) | stride;
}

void
nvk_mme_set_vb_stride(struct mme_builder *b)
{
   /* Param is laid out as
    *
    *    bits 0..11  : stride
    *    bits 16..21 : VB index
    */
   struct mme_value param = mme_load(b);

   struct mme_value vb_idx4 = mme_merge(b, mme_zero(), param, 2, 5, 16);

   struct mme_value state =
      mme_state_arr(b, NV9097_SET_VERTEX_STREAM_A_FORMAT(0), vb_idx4);
   struct mme_value new_state = mme_merge(b, state, param, 0, 12, 0);
   mme_if(b, ine, state, new_state) {
      mme_mthd_arr(b, NV9097_SET_VERTEX_STREAM_A_FORMAT(0), vb_idx4);
      mme_emit(b, new_state);
   }
}

static void
nvk_flush_vi_state(struct nvk_cmd_buffer *cmd)
{
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);
   struct nvk_physical_device *pdev = nvk_device_physical(dev);
   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 258);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VI_BINDINGS_VALID)) {
      P_1INC(p, NVB197, CALL_MME_MACRO(NVK_MME_SET_VB_ENABLES));
      P_INLINE_DATA(p, dyn->vi->bindings_valid);
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VI) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VI_BINDINGS_VALID)) {
      u_foreach_bit(a, dyn->vi->attributes_valid) {
         const struct nvk_va_format *fmt =
            nvk_get_va_format(pdev, dyn->vi->attributes[a].format);

         P_IMMD(p, NV9097, SET_VERTEX_ATTRIBUTE_A(a), {
            .stream                 = dyn->vi->attributes[a].binding,
            .offset                 = dyn->vi->attributes[a].offset,
            .component_bit_widths   = fmt->bit_widths,
            .numerical_type         = fmt->type,
            .swap_r_and_b           = fmt->swap_rb,
         });
      }

      u_foreach_bit(b, dyn->vi->bindings_valid) {
         const bool instanced = dyn->vi->bindings[b].input_rate ==
                                VK_VERTEX_INPUT_RATE_INSTANCE;
         P_IMMD(p, NV9097, SET_VERTEX_STREAM_INSTANCE_A(b), instanced);
         P_IMMD(p, NV9097, SET_VERTEX_STREAM_A_FREQUENCY(b),
            dyn->vi->bindings[b].divisor);
      }
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VI_BINDINGS_VALID) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VI_BINDING_STRIDES)) {
      u_foreach_bit(b, dyn->vi->bindings_valid) {
         assert(dyn->vi_binding_strides[b] < (1 << 12));
         P_1INC(p, NVB197, CALL_MME_MACRO(NVK_MME_SET_VB_STRIDE));
         P_INLINE_DATA(p, nvk_mme_vb_stride(b, dyn->vi_binding_strides[b]));
      }
   }
}

static uint32_t
vk_to_nv9097_primitive_topology(VkPrimitiveTopology prim)
{
   switch (prim) {
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
      return NV9097_BEGIN_OP_POINTS;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
      return NV9097_BEGIN_OP_LINES;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
      return NV9097_BEGIN_OP_LINE_STRIP;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
   case VK_PRIMITIVE_TOPOLOGY_META_RECT_LIST_MESA:
#pragma GCC diagnostic pop
      return NV9097_BEGIN_OP_TRIANGLES;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
      return NV9097_BEGIN_OP_TRIANGLE_STRIP;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
      return NV9097_BEGIN_OP_TRIANGLE_FAN;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
      return NV9097_BEGIN_OP_LINELIST_ADJCY;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
      return NV9097_BEGIN_OP_LINESTRIP_ADJCY;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
      return NV9097_BEGIN_OP_TRIANGLELIST_ADJCY;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
      return NV9097_BEGIN_OP_TRIANGLESTRIP_ADJCY;
   case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
      return NV9097_BEGIN_OP_PATCH;
   default:
      unreachable("Invalid primitive topology");
   }
}

static void
nvk_flush_ia_state(struct nvk_cmd_buffer *cmd)
{
   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_IA_PRIMITIVE_TOPOLOGY)) {
      uint32_t begin;
      V_NV9097_BEGIN(begin, {
         .op = vk_to_nv9097_primitive_topology(dyn->ia.primitive_topology),
         .primitive_id = NV9097_BEGIN_PRIMITIVE_ID_FIRST,
         .instance_id = NV9097_BEGIN_INSTANCE_ID_FIRST,
         .split_mode = SPLIT_MODE_NORMAL_BEGIN_NORMAL_END,
      });

      struct nv_push *p = nvk_cmd_buffer_push(cmd, 2);
      P_MTHD(p, NV9097, SET_MME_SHADOW_SCRATCH(NVK_MME_SCRATCH_DRAW_BEGIN));
      P_INLINE_DATA(p, begin);
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_IA_PRIMITIVE_RESTART_ENABLE)) {
      struct nv_push *p = nvk_cmd_buffer_push(cmd, 2);
      P_IMMD(p, NV9097, SET_DA_PRIMITIVE_RESTART,
             dyn->ia.primitive_restart_enable);
   }
}

static void
nvk_flush_ts_state(struct nvk_cmd_buffer *cmd)
{
   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;
   struct nv_push *p = nvk_cmd_buffer_push(cmd, 4);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_TS_PATCH_CONTROL_POINTS)) {
      /* The hardware gets grumpy if we set this to 0 so make sure we set it
       * to at least 1 in case it's dirty but uninitialized.
       */
      P_IMMD(p, NV9097, SET_PATCH, MAX2(1, dyn->ts.patch_control_points));
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_TS_DOMAIN_ORIGIN)) {
      P_1INC(p, NVB197, CALL_MME_MACRO(NVK_MME_SET_TESS_PARAMS));
      P_INLINE_DATA(p, nvk_mme_tess_lower_left(
         dyn->ts.domain_origin == VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT));
   }
}

static void
nvk_flush_vp_state(struct nvk_cmd_buffer *cmd)
{
   const struct nvk_device *dev = nvk_cmd_buffer_device(cmd);

   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;

   struct nv_push *p =
      nvk_cmd_buffer_push(cmd, 18 * dyn->vp.viewport_count + 4 * NVK_MAX_VIEWPORTS);

   /* Nothing to do for MESA_VK_DYNAMIC_VP_VIEWPORT_COUNT */

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_VIEWPORTS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_DEPTH_CLAMP_RANGE)) {
      for (uint32_t i = 0; i < dyn->vp.viewport_count; i++) {
         const VkViewport *vp = &dyn->vp.viewports[i];

         /* These exactly match the spec values.  Nvidia hardware oddities
          * are accounted for later.
          */
         const float o_x = vp->x + 0.5f * vp->width;
         const float o_y = vp->y + 0.5f * vp->height;
         const float o_z = !dyn->vp.depth_clip_negative_one_to_one ?
                           vp->minDepth :
                           (vp->maxDepth + vp->minDepth) * 0.5f;

         const float p_x = vp->width;
         const float p_y = vp->height;
         const float p_z = !dyn->vp.depth_clip_negative_one_to_one ?
                           vp->maxDepth - vp->minDepth :
                           (vp->maxDepth - vp->minDepth) * 0.5f;

         P_MTHD(p, NV9097, SET_VIEWPORT_SCALE_X(i));
         P_NV9097_SET_VIEWPORT_SCALE_X(p, i, fui(0.5f * p_x));
         P_NV9097_SET_VIEWPORT_SCALE_Y(p, i, fui(0.5f * p_y));
         P_NV9097_SET_VIEWPORT_SCALE_Z(p, i, fui(p_z));

         P_NV9097_SET_VIEWPORT_OFFSET_X(p, i, fui(o_x));
         P_NV9097_SET_VIEWPORT_OFFSET_Y(p, i, fui(o_y));
         P_NV9097_SET_VIEWPORT_OFFSET_Z(p, i, fui(o_z));

         const bool user_defined_range =
            dyn->vp.depth_clamp_mode == VK_DEPTH_CLAMP_MODE_USER_DEFINED_RANGE_EXT;
         float xmin = vp->x;
         float xmax = vp->x + vp->width;
         float ymin = MIN2(vp->y, vp->y + vp->height);
         float ymax = MAX2(vp->y, vp->y + vp->height);
         float zmin = user_defined_range ?
                      dyn->vp.depth_clamp_range.minDepthClamp :
                      MIN2(vp->minDepth, vp->maxDepth);
         float zmax = user_defined_range ?
                      dyn->vp.depth_clamp_range.maxDepthClamp :
                      MAX2(vp->minDepth, vp->maxDepth);
         assert(xmin <= xmax && ymin <= ymax && zmin <= zmax);

         const float max_dim = (float)0xffff;
         xmin = CLAMP(xmin, 0, max_dim);
         xmax = CLAMP(xmax, 0, max_dim);
         ymin = CLAMP(ymin, 0, max_dim);
         ymax = CLAMP(ymax, 0, max_dim);

         if (!dev->vk.enabled_extensions.EXT_depth_range_unrestricted) {
            assert(0.0 <= zmin && zmin <= 1.0);
            assert(0.0 <= zmax && zmax <= 1.0);
         }

         P_MTHD(p, NV9097, SET_VIEWPORT_CLIP_HORIZONTAL(i));
         P_NV9097_SET_VIEWPORT_CLIP_HORIZONTAL(p, i, {
            .x0      = xmin,
            .width   = xmax - xmin,
         });
         P_NV9097_SET_VIEWPORT_CLIP_VERTICAL(p, i, {
            .y0      = ymin,
            .height  = ymax - ymin,
         });

         if (nvk_cmd_buffer_3d_cls(cmd) >= VOLTA_A) {
            P_NV9097_SET_VIEWPORT_CLIP_MIN_Z(p, i, fui(zmin));
            P_NV9097_SET_VIEWPORT_CLIP_MAX_Z(p, i, fui(zmax));
         } else {
            P_1INC(p, NVB197, CALL_MME_MACRO(NVK_MME_SET_VIEWPORT_MIN_MAX_Z));
            P_INLINE_DATA(p, i);
            P_INLINE_DATA(p, fui(zmin));
            P_INLINE_DATA(p, fui(zmax));
         }

         if (nvk_cmd_buffer_3d_cls(cmd) >= MAXWELL_B) {
            P_IMMD(p, NVB197, SET_VIEWPORT_COORDINATE_SWIZZLE(i), {
               .x = X_POS_X,
               .y = Y_POS_Y,
               .z = Z_POS_Z,
               .w = W_POS_W,
            });
         }
      }
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE)) {
      P_IMMD(p, NV9097, SET_VIEWPORT_Z_CLIP,
             dyn->vp.depth_clip_negative_one_to_one ?
             RANGE_NEGATIVE_W_TO_POSITIVE_W :
             RANGE_ZERO_TO_POSITIVE_W);
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_SCISSOR_COUNT)) {
      for (unsigned i = dyn->vp.scissor_count; i < NVK_MAX_VIEWPORTS; i++)
         P_IMMD(p, NV9097, SET_SCISSOR_ENABLE(i), V_FALSE);
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_SCISSORS)) {
      for (unsigned i = 0; i < dyn->vp.scissor_count; i++) {
         const VkRect2D *s = &dyn->vp.scissors[i];

         const uint32_t xmin = MIN2(16384, s->offset.x);
         const uint32_t xmax = MIN2(16384, s->offset.x + s->extent.width);
         const uint32_t ymin = MIN2(16384, s->offset.y);
         const uint32_t ymax = MIN2(16384, s->offset.y + s->extent.height);

         P_MTHD(p, NV9097, SET_SCISSOR_ENABLE(i));
         P_NV9097_SET_SCISSOR_ENABLE(p, i, V_TRUE);
         P_NV9097_SET_SCISSOR_HORIZONTAL(p, i, {
            .xmin = xmin,
            .xmax = xmax,
         });
         P_NV9097_SET_SCISSOR_VERTICAL(p, i, {
            .ymin = ymin,
            .ymax = ymax,
         });
      }
   }
}

static uint32_t
vk_to_nv9097_polygon_mode(VkPolygonMode vk_mode)
{
   ASSERTED uint16_t vk_to_nv9097[] = {
      [VK_POLYGON_MODE_FILL]  = NV9097_SET_FRONT_POLYGON_MODE_V_FILL,
      [VK_POLYGON_MODE_LINE]  = NV9097_SET_FRONT_POLYGON_MODE_V_LINE,
      [VK_POLYGON_MODE_POINT] = NV9097_SET_FRONT_POLYGON_MODE_V_POINT,
   };
   assert(vk_mode < ARRAY_SIZE(vk_to_nv9097));

   uint32_t nv9097_mode = 0x1b00 | (2 - vk_mode);
   assert(nv9097_mode == vk_to_nv9097[vk_mode]);
   return nv9097_mode;
}

static uint32_t
vk_to_nv9097_cull_mode(VkCullModeFlags vk_cull_mode)
{
   static const uint16_t vk_to_nv9097[] = {
      [VK_CULL_MODE_FRONT_BIT]      = NV9097_OGL_SET_CULL_FACE_V_FRONT,
      [VK_CULL_MODE_BACK_BIT]       = NV9097_OGL_SET_CULL_FACE_V_BACK,
      [VK_CULL_MODE_FRONT_AND_BACK] = NV9097_OGL_SET_CULL_FACE_V_FRONT_AND_BACK,
   };
   assert(vk_cull_mode < ARRAY_SIZE(vk_to_nv9097));
   return vk_to_nv9097[vk_cull_mode];
}

static uint32_t
vk_to_nv9097_front_face(VkFrontFace vk_face)
{
   /* Vulkan and OpenGL are backwards here because Vulkan assumes the D3D
    * convention in which framebuffer coordinates always start in the upper
    * left while OpenGL has framebuffer coordinates starting in the lower
    * left.  Therefore, we want the reverse of the hardware enum name.
    */
   ASSERTED static const uint16_t vk_to_nv9097[] = {
      [VK_FRONT_FACE_COUNTER_CLOCKWISE]   = NV9097_OGL_SET_FRONT_FACE_V_CCW,
      [VK_FRONT_FACE_CLOCKWISE]           = NV9097_OGL_SET_FRONT_FACE_V_CW,
   };
   assert(vk_face < ARRAY_SIZE(vk_to_nv9097));

   uint32_t nv9097_face = 0x900 | (1 - vk_face);
   assert(nv9097_face == vk_to_nv9097[vk_face]);
   return nv9097_face;
}

static uint32_t
vk_to_nv9097_provoking_vertex(VkProvokingVertexModeEXT vk_mode)
{
   STATIC_ASSERT(VK_PROVOKING_VERTEX_MODE_FIRST_VERTEX_EXT ==
                 NV9097_SET_PROVOKING_VERTEX_V_FIRST);
   STATIC_ASSERT(VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT ==
                 NV9097_SET_PROVOKING_VERTEX_V_LAST);
   return vk_mode;
}

void
nvk_mme_set_viewport_min_max_z(struct mme_builder *b)
{
   struct mme_value vp_idx = mme_load(b);
   struct mme_value min_z = mme_load(b);
   struct mme_value max_z = mme_load(b);

   /* Multiply by 2 because it's an array with stride 8 */
   mme_sll_to(b, vp_idx, vp_idx, mme_imm(1));
   mme_mthd_arr(b, NVK_SET_MME_SCRATCH(VIEWPORT0_MIN_Z), vp_idx);
   mme_emit(b, min_z);
   mme_emit(b, max_z);

   struct mme_value z_clamp = nvk_mme_load_scratch(b, Z_CLAMP);
   mme_if(b, ine, z_clamp, mme_zero()) {
      /* Multiply by 2 again because this array has stride 16 */
      mme_sll_to(b, vp_idx, vp_idx, mme_imm(1));
      mme_mthd_arr(b, NV9097_SET_VIEWPORT_CLIP_MIN_Z(0), vp_idx);
      mme_emit(b, min_z);
      mme_emit(b, max_z);
   }
}

void
nvk_mme_set_z_clamp(struct mme_builder *b)
{
   struct mme_value z_clamp = mme_load(b);
   struct mme_value old_z_clamp = nvk_mme_load_scratch(b, Z_CLAMP);
   mme_if(b, ine, z_clamp, old_z_clamp) {
      nvk_mme_store_scratch(b, Z_CLAMP, z_clamp);

      mme_if(b, ine, z_clamp, mme_zero()) {
         struct mme_value i_2 = mme_mov(b, mme_zero());
         mme_while(b, ine, i_2, mme_imm(NVK_MAX_VIEWPORTS * 2)) {
            struct mme_value min_z =
               mme_state_arr(b, NVK_SET_MME_SCRATCH(VIEWPORT0_MIN_Z), i_2);
            struct mme_value max_z =
               mme_state_arr(b, NVK_SET_MME_SCRATCH(VIEWPORT0_MAX_Z), i_2);

            struct mme_value i_4 = mme_sll(b, i_2, mme_imm(1));
            mme_mthd_arr(b, NV9097_SET_VIEWPORT_CLIP_MIN_Z(0), i_4);
            mme_emit(b, min_z);
            mme_emit(b, max_z);

            mme_free_reg(b, i_4);
            mme_free_reg(b, min_z);
            mme_free_reg(b, max_z);

            mme_add_to(b, i_2, i_2, mme_imm(2));
         }
         mme_free_reg(b, i_2);
      }
      mme_if(b, ieq, z_clamp, mme_zero()) {
         struct mme_value i_4 = mme_mov(b, mme_zero());
         mme_while(b, ine, i_4, mme_imm(NVK_MAX_VIEWPORTS * 4)) {
            mme_mthd_arr(b, NV9097_SET_VIEWPORT_CLIP_MIN_Z(0), i_4);
            mme_emit(b, mme_imm(fui(-INFINITY)));
            mme_emit(b, mme_imm(fui(INFINITY)));

            mme_add_to(b, i_4, i_4, mme_imm(4));
         }
         mme_free_reg(b, i_4);
      }
   }
}

static void
nvk_flush_rs_state(struct nvk_cmd_buffer *cmd)
{
   const struct nvk_device *dev = nvk_cmd_buffer_device(cmd);
   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;
   const struct nvk_rendering_state *render =
      &cmd->state.gfx.render;

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 46);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_RASTERIZER_DISCARD_ENABLE))
      P_IMMD(p, NV9097, SET_RASTER_ENABLE, !dyn->rs.rasterizer_discard_enable);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_DEPTH_CLIP_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_DEPTH_CLAMP_ENABLE)) {
      const bool z_clamp = dyn->rs.depth_clamp_enable;
      const bool z_clip = vk_rasterization_state_depth_clip_enable(&dyn->rs);
      /* z_clamp_zero_one accounts for the interaction between
       * depthClampZeroOne and depthRangeUnrestricted as mentioned in the
       * Vulkan spec. depthClampZeroOne adds an additional clamp and doesn't
       * modify the clip/clamp threshold.  We are expected to clamp to [0,1]
       * when any one of these conditions are fulfilled:
       * - depth_range_unrestricted is not enabled
       * - depthClampZeroOne is enabled but depth
       *    format is not floating point or depthRangeUnrestricted
       *    is not enabled
       * - fixed point depth format
      */
      const bool z_clamp_zero_one =
         !vk_format_has_float_depth(render->depth_att.vk_format) ||
         (dev->vk.enabled_features.depthClampZeroOne &&
         !dev->vk.enabled_extensions.EXT_depth_range_unrestricted);

      P_IMMD(p, NVC397, SET_VIEWPORT_CLIP_CONTROL, {
         /* We only set Z clip range if clamp is requested.  Otherwise, we
          * leave it set to -/+INF and clip using the guardband below.
          *
          * depthClampZeroOne is independent of normal depth clamping and
          * does not modify the clip/clamp threshold.  The Vulkan spec
          * guarantees that, in the cases where depthClampZeroOne applies,
          * the [zmin, zmax] is inside [0, 1].  This means that, if z_clamp
          * is enabled, we can just do the regular clamp.  If z_clamp is
          * disabled and z_clamp_zero_one is enabled then we need to
          * apply the [0, 1] clamp.
          */
         .min_z_zero_max_z_one = (!z_clamp && z_clamp_zero_one)
                                 ? MIN_Z_ZERO_MAX_Z_ONE_TRUE
                                 : MIN_Z_ZERO_MAX_Z_ONE_FALSE,
         .z_clip_range = (nvk_cmd_buffer_3d_cls(cmd) >= VOLTA_A &&
                          (z_clamp || !z_clamp_zero_one))
                         ? (z_clamp ? Z_CLIP_RANGE_MIN_Z_MAX_Z
                                    : Z_CLIP_RANGE_MINUS_INF_PLUS_INF)
                         : Z_CLIP_RANGE_USE_FIELD_MIN_Z_ZERO_MAX_Z_ONE,

         .pixel_min_z = PIXEL_MIN_Z_CLAMP,
         .pixel_max_z = PIXEL_MAX_Z_CLAMP,

         .geometry_guardband = GEOMETRY_GUARDBAND_SCALE_256,
         .line_point_cull_guardband = LINE_POINT_CULL_GUARDBAND_SCALE_256,
         .geometry_clip = z_clip ? GEOMETRY_CLIP_FRUSTUM_XYZ_CLIP
                                 : GEOMETRY_CLIP_FRUSTUM_XY_CLIP,

         /* We clip depth with the geometry clipper to ensure that it gets
          * clipped before depth bias is applied.  If we leave it up to the
          * raserizer clipper (pixel_min/max_z = CLIP), it will clip too late
          * in the pipeline.  This can be seen in two different ways:
          *
          *  - When depth bias is enabled, the bias is applied post-clipping.
          *    If we clip in the rasterizer, it will clip according to the
          *    post-bias depth which is wrong.
          *
          *  - If the fragment shader overrides the depth by writing to
          *    gl_FragDepth, it should be clipped according to the original
          *    geometry, not accoring to gl_FragDepth.
          *
          * In order to always get the geometry clipper, we need to set a
          * tight guardband (geometry_guardband_z = SCALE_1).
          */
         .geometry_guardband_z = z_clip ? GEOMETRY_GUARDBAND_Z_SCALE_1
                                        : GEOMETRY_GUARDBAND_Z_SCALE_256,
      });

      /* Pre-Volta, we don't have SET_VIEWPORT_CLIP_CONTROL::z_clip_range.
       * Instead, we have to emulate it by smashing VIEWPORT_CLIP_MIN/MAX_Z
       * based on whether or not z_clamp is set. This is done by a pair of
       * macros, one of which is called here and the other is called in
       * viewport setup.
       */
      if (nvk_cmd_buffer_3d_cls(cmd) < VOLTA_A) {
         P_1INC(p, NVB197, CALL_MME_MACRO(NVK_MME_SET_Z_CLAMP));
         P_INLINE_DATA(p, z_clamp);
      }
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_POLYGON_MODE)) {
      uint32_t polygon_mode = vk_to_nv9097_polygon_mode(dyn->rs.polygon_mode);
      P_MTHD(p, NV9097, SET_FRONT_POLYGON_MODE);
      P_NV9097_SET_FRONT_POLYGON_MODE(p, polygon_mode);
      P_NV9097_SET_BACK_POLYGON_MODE(p, polygon_mode);
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_CULL_MODE)) {
      P_IMMD(p, NV9097, OGL_SET_CULL, dyn->rs.cull_mode != VK_CULL_MODE_NONE);

      if (dyn->rs.cull_mode != VK_CULL_MODE_NONE) {
         uint32_t face = vk_to_nv9097_cull_mode(dyn->rs.cull_mode);
         P_IMMD(p, NV9097, OGL_SET_CULL_FACE, face);
      }
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_FRONT_FACE)) {
      P_IMMD(p, NV9097, OGL_SET_FRONT_FACE,
         vk_to_nv9097_front_face(dyn->rs.front_face));
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_PROVOKING_VERTEX)) {
      P_IMMD(p, NV9097, SET_PROVOKING_VERTEX,
             vk_to_nv9097_provoking_vertex(dyn->rs.provoking_vertex));
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_DEPTH_BIAS_ENABLE)) {
      P_MTHD(p, NV9097, SET_POLY_OFFSET_POINT);
      P_NV9097_SET_POLY_OFFSET_POINT(p, dyn->rs.depth_bias.enable);
      P_NV9097_SET_POLY_OFFSET_LINE(p, dyn->rs.depth_bias.enable);
      P_NV9097_SET_POLY_OFFSET_FILL(p, dyn->rs.depth_bias.enable);
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_DEPTH_BIAS_FACTORS)) {
      switch (dyn->rs.depth_bias.representation) {
      case VK_DEPTH_BIAS_REPRESENTATION_LEAST_REPRESENTABLE_VALUE_FORMAT_EXT:
         P_IMMD(p, NV9097, SET_DEPTH_BIAS_CONTROL,
                DEPTH_FORMAT_DEPENDENT_TRUE);
         break;
      case VK_DEPTH_BIAS_REPRESENTATION_LEAST_REPRESENTABLE_VALUE_FORCE_UNORM_EXT:
         P_IMMD(p, NV9097, SET_DEPTH_BIAS_CONTROL,
                DEPTH_FORMAT_DEPENDENT_FALSE);
         break;
      case VK_DEPTH_BIAS_REPRESENTATION_FLOAT_EXT:
      default:
         unreachable("Unsupported depth bias representation");
      }
      /* TODO: The blob multiplies by 2 for some reason. We don't. */
      P_IMMD(p, NV9097, SET_DEPTH_BIAS, fui(dyn->rs.depth_bias.constant));
      P_IMMD(p, NV9097, SET_SLOPE_SCALE_DEPTH_BIAS, fui(dyn->rs.depth_bias.slope));
      P_IMMD(p, NV9097, SET_DEPTH_BIAS_CLAMP, fui(dyn->rs.depth_bias.clamp));
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_LINE_WIDTH)) {
      P_MTHD(p, NV9097, SET_LINE_WIDTH_FLOAT);
      P_NV9097_SET_LINE_WIDTH_FLOAT(p, fui(dyn->rs.line.width));
      P_NV9097_SET_ALIASED_LINE_WIDTH_FLOAT(p, fui(dyn->rs.line.width));
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_LINE_MODE)) {
      switch (dyn->rs.line.mode) {
      case VK_LINE_RASTERIZATION_MODE_DEFAULT_KHR:
      case VK_LINE_RASTERIZATION_MODE_RECTANGULAR_KHR:
         P_IMMD(p, NV9097, SET_LINE_MULTISAMPLE_OVERRIDE, ENABLE_FALSE);
         P_IMMD(p, NV9097, SET_ANTI_ALIASED_LINE, ENABLE_FALSE);
         break;

      case VK_LINE_RASTERIZATION_MODE_BRESENHAM_KHR:
         P_IMMD(p, NV9097, SET_LINE_MULTISAMPLE_OVERRIDE, ENABLE_TRUE);
         P_IMMD(p, NV9097, SET_ANTI_ALIASED_LINE, ENABLE_FALSE);
         break;

      case VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_KHR:
         P_IMMD(p, NV9097, SET_LINE_MULTISAMPLE_OVERRIDE, ENABLE_TRUE);
         P_IMMD(p, NV9097, SET_ANTI_ALIASED_LINE, ENABLE_TRUE);
         break;

      default:
         unreachable("Invalid line rasterization mode");
      }
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_LINE_STIPPLE_ENABLE))
      P_IMMD(p, NV9097, SET_LINE_STIPPLE, dyn->rs.line.stipple.enable);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_LINE_STIPPLE)) {
      /* map factor from [1,256] to [0, 255] */
      uint32_t stipple_factor = CLAMP(dyn->rs.line.stipple.factor, 1, 256) - 1;
      P_IMMD(p, NV9097, SET_LINE_STIPPLE_PARAMETERS, {
         .factor  = stipple_factor,
         .pattern = dyn->rs.line.stipple.pattern,
      });
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_RASTERIZATION_STREAM))
      P_IMMD(p, NV9097, SET_RASTER_INPUT, dyn->rs.rasterization_stream);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_CONSERVATIVE_MODE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_EXTRA_PRIMITIVE_OVERESTIMATION_SIZE)) {
      if (nvk_cmd_buffer_3d_cls(cmd) < MAXWELL_B) {
         assert(dyn->rs.conservative_mode == VK_CONSERVATIVE_RASTERIZATION_MODE_DISABLED_EXT);
      } else if (dyn->rs.conservative_mode == VK_CONSERVATIVE_RASTERIZATION_MODE_DISABLED_EXT) {
         P_IMMD(p, NVB197, SET_CONSERVATIVE_RASTER, ENABLE_FALSE);
      } else {
         uint32_t extra_overestimate =
            MIN2(3, dyn->rs.extra_primitive_overestimation_size * 4);

         if (nvk_cmd_buffer_3d_cls(cmd) >= VOLTA_A) {
            P_IMMD(p, NVC397, SET_CONSERVATIVE_RASTER_CONTROL, {
               .extra_prim_bloat = extra_overestimate,
               .copy_inner_to_outer =
                  (dyn->rs.conservative_mode == VK_CONSERVATIVE_RASTERIZATION_MODE_UNDERESTIMATE_EXT),
               .triangle_snap_mode = TRIANGLE_SNAP_MODE_MODE_PRE_SNAP,
               .line_and_point_snap_mode = LINE_AND_POINT_SNAP_MODE_MODE_PRE_SNAP,
               .uncertainty_region_size = UNCERTAINTY_REGION_SIZE_SIZE_512,
            });
         } else {
            P_1INC(p, NVB197, CALL_MME_MACRO(NVK_MME_SET_CONSERVATIVE_RASTER_STATE));
            P_INLINE_DATA(p, extra_overestimate << 23);
         }
         P_IMMD(p, NVB197, SET_CONSERVATIVE_RASTER, ENABLE_TRUE);
      }
   }
}

uint32_t
nvk_mme_shading_rate_control_sample_shading(bool sample_shading)
{
   return nvk_mme_val_mask((!sample_shading) << 1, 1 << 1);
}

static uint32_t
nvk_mme_shading_rate_control_enable(bool enable)
{
   return nvk_mme_val_mask(enable, 1 << 0);
}

void
nvk_mme_set_shading_rate_control(struct mme_builder *b)
{
   if (b->devinfo->cls_eng3d < TURING_A)
      return;

   struct mme_value val_mask = mme_load(b);
   struct mme_value old_src = nvk_mme_load_scratch(b, SHADING_RATE_CONTROL);
   struct mme_value src = nvk_mme_set_masked(b, old_src, val_mask);
   mme_free_reg(b, val_mask);

   mme_if(b, ine, src, old_src) {
      mme_free_reg(b, old_src);
      nvk_mme_store_scratch(b, SHADING_RATE_CONTROL, src);

      struct mme_value enable1 = mme_merge(b, mme_zero(), src, 0, 1, 0);
      struct mme_value enable2 = mme_merge(b, mme_zero(), src, 0, 1, 1);
      struct mme_value enable = mme_and(b, enable1, enable2);

      struct mme_value i = mme_mov(b, mme_zero());
      mme_while(b, ine, i, mme_imm(16 * 4)) {
         mme_mthd_arr(b, NVC597_SET_VARIABLE_PIXEL_RATE_SHADING_CONTROL(0), i);
         mme_emit(b, enable);
         mme_add_to(b, i, i, mme_imm(4));
      }
   }
}

static void
nvk_mme_set_shading_rate_control_test_check(
   const struct nv_device_info *devinfo,
   const struct nvk_mme_test_case *test,
   const struct nvk_mme_mthd_data *results)
{
   if (devinfo->cls_eng3d < TURING_A)
      return;

   assert(results[0].mthd == NVK_SET_MME_SCRATCH(SHADING_RATE_CONTROL));
   bool enable = (results[0].data & 3) == 3;

   for (uint32_t i = 0; i < 16; i++) {
      assert(results[i + 1].mthd ==
             NVC597_SET_VARIABLE_PIXEL_RATE_SHADING_CONTROL(i));
      assert(results[i + 1].data == enable);
   }
}

const struct nvk_mme_test_case nvk_mme_set_shading_rate_control_tests[] = {{
   .init = (struct nvk_mme_mthd_data[]) {
      { NVK_SET_MME_SCRATCH(SHADING_RATE_CONTROL), 0 },
      { }
   },
   .params = (uint32_t[]) { 0x00030003 },
   .check = nvk_mme_set_shading_rate_control_test_check,
}, {
   .init = (struct nvk_mme_mthd_data[]) {
      { NVK_SET_MME_SCRATCH(SHADING_RATE_CONTROL), 0 },
      { }
   },
   .params = (uint32_t[]) { 0x00030001 },
   .check = nvk_mme_set_shading_rate_control_test_check,
}, {}};

static VkExtent2D
nvk_combine_fs_log2_rates(VkFragmentShadingRateCombinerOpKHR op,
                          VkExtent2D a_log2, VkExtent2D b_log2)
{
   switch (op) {
   case VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR:
      return a_log2;

   case VK_FRAGMENT_SHADING_RATE_COMBINER_OP_REPLACE_KHR:
      return b_log2;

   case VK_FRAGMENT_SHADING_RATE_COMBINER_OP_MIN_KHR:
      return (VkExtent2D) {
         .width = MIN2(a_log2.width, b_log2.width),
         .height = MIN2(a_log2.height, b_log2.height),
      };

   case VK_FRAGMENT_SHADING_RATE_COMBINER_OP_MAX_KHR:
      return (VkExtent2D) {
         .width = MAX2(a_log2.width, b_log2.width),
         .height = MAX2(a_log2.height, b_log2.height),
      };

   case VK_FRAGMENT_SHADING_RATE_COMBINER_OP_MUL_KHR:
      return (VkExtent2D) {
         .width = a_log2.width + b_log2.width,
         .height = a_log2.height + b_log2.height,
      };

   default:
      unreachable("Invalid FSR combiner op");
   }
}

static uint8_t
vk_to_nvc597_shading_rate_log2(VkExtent2D rate_log2)
{
   rate_log2.width = MIN2(rate_log2.width, 2);
   rate_log2.height = MIN2(rate_log2.height, 2);
   const uint8_t idx = (rate_log2.width << 2) | rate_log2.height;

   /* From the Vulkan 1.3.297 spec:
    *
    *    "A fragment shading rate Rxy representing any of Axy, Bxy or Cxy
    *    is clamped as follows. [...] From this list of supported rates,
    *    the following steps are applied in order, to select a single
    *    value:
    *
    *     1. Keep only rates where Rx' ≤ Rx and Ry' ≤ Ry.
    *
    *        - Implementations may also keep rates where Rx' ≤ Ry and
    *          Ry' ≤ Rx.
    *
    *     2. Keep only rates with the highest area (Rx' × Ry').
    *
    *     3. Keep only rates with the lowest aspect ratio (Rx' + Ry').
    *
    *     4. In cases where a wide (e.g. 4x1) and tall (e.g. 1x4) rate
    *        remain, the implementation may choose either rate. However, it
    *        must choose this rate consistently for the same shading rates,
    *        render pass transform, and combiner operations for the
    *        lifetime of the VkDevice.
    *
    * We have the following rates: 1x1, 2x1, 1x2, 2x2, 4x2, 2x4, 4x4.
    */
   static const uint8_t vk_to_nvc597[] = {
#define NVC597_FSR(X) NVC597_SET_VARIABLE_PIXEL_RATE_SHADING_INDEX_TO_RATE_A_RATE_INDEX0_PS_##X
      NVC597_FSR(X1_PER_RASTER_PIXEL),
      NVC597_FSR(X1_PER_1X2_RASTER_PIXELS),
      NVC597_FSR(X1_PER_1X2_RASTER_PIXELS), /* 1x4 */
      NVC597_FSR(X1_PER_1X2_RASTER_PIXELS), /* 1x8 */
      NVC597_FSR(X1_PER_2X1_RASTER_PIXELS),
      NVC597_FSR(X1_PER_2X2_RASTER_PIXELS),
      NVC597_FSR(X1_PER_2X4_RASTER_PIXELS),
      NVC597_FSR(X1_PER_2X4_RASTER_PIXELS), /* 2x8 */
      NVC597_FSR(X1_PER_2X1_RASTER_PIXELS), /* 4x1 */
      NVC597_FSR(X1_PER_4X2_RASTER_PIXELS),
      NVC597_FSR(X1_PER_4X4_RASTER_PIXELS),
      NVC597_FSR(X1_PER_4X4_RASTER_PIXELS), /* 4x8 */
      NVC597_FSR(X1_PER_2X1_RASTER_PIXELS), /* 8x1 */
      NVC597_FSR(X1_PER_4X2_RASTER_PIXELS), /* 8x2 */
      NVC597_FSR(X1_PER_4X4_RASTER_PIXELS), /* 8x4 */
      NVC597_FSR(X1_PER_4X4_RASTER_PIXELS), /* 8x8 */
#undef NVC597_FSR
   };

   assert(idx < ARRAY_SIZE(vk_to_nvc597));
   return vk_to_nvc597[idx];
}

static void
nvk_flush_fsr_state(struct nvk_cmd_buffer *cmd)
{
   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;

   if (nvk_cmd_buffer_3d_cls(cmd) < TURING_A) {
      assert(vk_fragment_shading_rate_is_disabled(&dyn->fsr));
      return;
   }

   if (!BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_FSR))
      return;

   if (vk_fragment_shading_rate_is_disabled(&dyn->fsr)) {
      struct nv_push *p = nvk_cmd_buffer_push(cmd, 2);
      P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_SET_SHADING_RATE_CONTROL));
      P_INLINE_DATA(p, nvk_mme_shading_rate_control_enable(false));
   } else {
      struct nv_push *p = nvk_cmd_buffer_push(cmd, 2 + 16 * 3);

      assert(util_is_power_of_two_or_zero(dyn->fsr.fragment_size.width));
      assert(util_is_power_of_two_or_zero(dyn->fsr.fragment_size.height));
      const VkExtent2D state_fs_log2 = {
         .width = util_logbase2(dyn->fsr.fragment_size.width),
         .height = util_logbase2(dyn->fsr.fragment_size.height),
      };

      for (uint32_t prim_idx = 0; prim_idx < 16; prim_idx++) {
         const VkExtent2D prim_fs_log2 = {
            .width = (prim_idx >> 2) & 3,
            .height = prim_idx & 3,
         };

         const VkExtent2D state_prim_fs_log2 =
            nvk_combine_fs_log2_rates(dyn->fsr.combiner_ops[0],
                                      state_fs_log2, prim_fs_log2);

         uint8_t rates[16] = {};
         for (uint32_t att_idx = 0; att_idx < 16; att_idx++) {
            const VkExtent2D att_fs_log2 = {
               .width = (att_idx >> 2) & 3,
               .height = att_idx & 3,
            };

            const VkExtent2D fs_log2 =
               nvk_combine_fs_log2_rates(dyn->fsr.combiner_ops[1],
                                         state_prim_fs_log2, att_fs_log2);

            rates[att_idx] = vk_to_nvc597_shading_rate_log2(fs_log2);
         }

         P_MTHD(p, NVC597, SET_VARIABLE_PIXEL_RATE_SHADING_INDEX_TO_RATE_A(prim_idx));
         P_NVC597_SET_VARIABLE_PIXEL_RATE_SHADING_INDEX_TO_RATE_A(p, prim_idx, {
            .rate_index0 = rates[0],
            .rate_index1 = rates[1],
            .rate_index2 = rates[2],
            .rate_index3 = rates[3],
            .rate_index4 = rates[4],
            .rate_index5 = rates[5],
            .rate_index6 = rates[6],
            .rate_index7 = rates[7],
         });
         P_NVC597_SET_VARIABLE_PIXEL_RATE_SHADING_INDEX_TO_RATE_B(p, prim_idx, {
            .rate_index8 = rates[8],
            .rate_index9 = rates[9],
            .rate_index10 = rates[10],
            .rate_index11 = rates[11],
            .rate_index12 = rates[12],
            .rate_index13 = rates[13],
            .rate_index14 = rates[14],
            .rate_index15 = rates[15],
         });
      }

      P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_SET_SHADING_RATE_CONTROL));
      P_INLINE_DATA(p, nvk_mme_shading_rate_control_enable(true));
   }
}

static uint32_t
nvk_mme_anti_alias_init(void)
{
   /* This is a valid value but we never set it so it ensures that the macro
    * will actually run the first time we set anything.
    */
   return 0xf;
}

uint32_t
nvk_mme_anti_alias_min_sample_shading(float mss)
{
   /* The value we want to comput in the MME is
    *
    *    passes = next_pow2(samples * minSampleShading)
    *
    * Since samples is already a power of two,
    *
    *    passes_log2 = log2_ceil(samples * minSampleShading)
    *                = log2_ceil(samples / (1.0 / minSampleShading))
    *                = samples_log2 - log2_floor(1.0 / minSampleShading)
    *
    * if we assume (1.0 / min_sample_shading) >= 1.0.  This last bit is
    * something we can compute in the MME as long as the float math on the
    * right-hand side happens  on the CPU.
    */
   float rcp_mss = CLAMP(1.0 / mss, 1.0f, 16.0f);
   uint32_t rcp_mss_log2 = util_logbase2(floorf(rcp_mss));

   assert(rcp_mss_log2 != nvk_mme_anti_alias_init());

   return nvk_mme_val_mask(rcp_mss_log2 << 0, 0x000f);
}

static uint32_t
nvk_mme_anti_alias_samples(uint32_t samples)
{
   assert(util_is_power_of_two_or_zero(samples));
   const uint32_t samples_log2 = util_logbase2(MAX2(1, samples));

   return nvk_mme_val_mask(samples_log2 << 4, 0x00f0);
}

void
nvk_mme_set_anti_alias(struct mme_builder *b)
{
   struct mme_value val_mask = mme_load(b);
   struct mme_value old_anti_alias = nvk_mme_load_scratch(b, ANTI_ALIAS);
   struct mme_value anti_alias =
      nvk_mme_set_masked(b, old_anti_alias, val_mask);
   mme_free_reg(b, val_mask);

   mme_if(b, ine, anti_alias, old_anti_alias) {
      mme_free_reg(b, old_anti_alias);
      nvk_mme_store_scratch(b, ANTI_ALIAS, anti_alias);

      struct mme_value rcp_mss_log2 =
         mme_merge(b, mme_zero(), anti_alias, 0, 4, 0);
      struct mme_value samples_log2 =
         mme_merge(b, mme_zero(), anti_alias, 0, 4, 4);
      mme_free_reg(b, anti_alias);

      /* We've already done all the hard work on the CPU in
       * nvk_mme_min_sample_shading().  All we have to do here is add the two
       * log2 values and clamp so we don't get negative.
       */
      struct mme_value passes_log2 = mme_sub(b, samples_log2, rcp_mss_log2);
      mme_free_reg(b, rcp_mss_log2);

      /* passes = MAX(passes, 1) */
      struct mme_value neg = mme_srl(b, passes_log2, mme_imm(31));
      mme_if(b, ine, neg, mme_zero()) {
         mme_mov_to(b, passes_log2, mme_zero());
      }
      mme_free_reg(b, neg);

      /*
       * NV9097_SET_HYBRID_ANTI_ALIAS_CONTROL {
       *    ...
       *    .centroid = passes > 1 ? CENTROID_PER_PASS
       *                           : CENTROID_PER_FRAGMENT,
       * }
       */
      struct mme_value aac = mme_mov(b,
         mme_imm(NV9097_SET_HYBRID_ANTI_ALIAS_CONTROL_CENTROID_PER_FRAGMENT
                 << DRF_LO(NV9097_SET_HYBRID_ANTI_ALIAS_CONTROL_CENTROID)));
      mme_if(b, ine, passes_log2, mme_zero()) {
         mme_mov_to(b, aac,
            mme_imm(NV9097_SET_HYBRID_ANTI_ALIAS_CONTROL_CENTROID_PER_PASS
                    << DRF_LO(NV9097_SET_HYBRID_ANTI_ALIAS_CONTROL_CENTROID)));
      }

      struct mme_value passes = mme_sll(b, mme_imm(1), passes_log2);
      mme_merge_to(b, aac, aac, passes, 0, 4, 0);
      mme_free_reg(b, passes);

      mme_mthd(b, NV9097_SET_HYBRID_ANTI_ALIAS_CONTROL);
      mme_emit(b, aac);
      mme_free_reg(b, aac);

      /* Now we need to emit sample masks per-sample. Annoyingly, we have to
       * pack these in pairs.
       */
      STATIC_ASSERT(sizeof(struct nak_sample_mask) == 2);

      mme_mthd(b, NV9097_LOAD_CONSTANT_BUFFER_OFFSET);
      mme_emit(b, mme_imm(nvk_root_descriptor_offset(draw.sample_masks)));
      mme_mthd(b, NV9097_LOAD_CONSTANT_BUFFER(0));

      /* Annoyingly, we have to pack these in pairs */

      struct mme_value samples_per_pass_log2 =
         mme_sub(b, samples_log2, passes_log2);
      mme_free_reg(b, samples_log2);

      mme_if(b, ieq, samples_per_pass_log2, mme_zero()) {
         /* One sample per pass, we can just blast it out */
         for (uint32_t i = 0; i < NVK_MAX_SAMPLES; i += 2) {
            uint32_t mask0 = 1 << i;
            uint32_t mask1 = 1 << (i + 1);
            mme_emit(b, mme_imm(mask0 | (mask1 << 16)));
         }
      }

      mme_if(b, ine, samples_per_pass_log2, mme_zero()) {
         mme_if(b, ieq, passes_log2, mme_zero()) {
            /* It's a single pass so we can use 0xffff */
            for (uint32_t i = 0; i < NVK_MAX_SAMPLES / 2; i++)
               mme_emit(b, mme_imm(~0));
         }

         mme_if(b, ieq, passes_log2, mme_imm(1)) {
            for (uint32_t i = 0; i < NVK_MAX_SAMPLES / 2; i++) {
               struct mme_value mask =
                  nvk_mme_load_scratch_arr(b, SAMPLE_MASKS_2PASS_0, i);
               mme_emit(b, mask);
               mme_free_reg(b, mask);
            }
         }

         mme_if(b, ieq, passes_log2, mme_imm(2)) {
            for (uint32_t i = 0; i < NVK_MAX_SAMPLES / 2; i++) {
               struct mme_value mask =
                  nvk_mme_load_scratch_arr(b, SAMPLE_MASKS_4PASS_0, i);
               mme_emit(b, mask);
               mme_free_reg(b, mask);
            }
         }
      }
   }
}

const struct nvk_mme_test_case nvk_mme_set_anti_alias_tests[] = {{
   /* This case doesn't change the state so it should do nothing */
   .init = (struct nvk_mme_mthd_data[]) {
      { NVK_SET_MME_SCRATCH(ANTI_ALIAS), 0 },
      { }
   },
   .params = (uint32_t[]) { 0xffff0000 },
   .expected = (struct nvk_mme_mthd_data[]) {
      { }
   },
}, {
   /* Single sample, minSampleShading = 1.0 */
   .init = (struct nvk_mme_mthd_data[]) {
      { NVK_SET_MME_SCRATCH(ANTI_ALIAS), 0xf },
      { }
   },
   .params = (uint32_t[]) { 0xffff0000 },
   .expected = (struct nvk_mme_mthd_data[]) {
      { NVK_SET_MME_SCRATCH(ANTI_ALIAS), 0 },
      { NV9097_SET_HYBRID_ANTI_ALIAS_CONTROL, 0x1 },
      { NV9097_LOAD_CONSTANT_BUFFER_OFFSET,
        nvk_root_descriptor_offset(draw.sample_masks) },
      { NV9097_LOAD_CONSTANT_BUFFER(0), 0x020001 },
      { NV9097_LOAD_CONSTANT_BUFFER(1), 0x080004 },
      { NV9097_LOAD_CONSTANT_BUFFER(2), 0x200010 },
      { NV9097_LOAD_CONSTANT_BUFFER(3), 0x800040 },
      { }
   },
}, {
   /* Single sample, minSampleShading = 0.25 */
   .init = (struct nvk_mme_mthd_data[]) {
      { NVK_SET_MME_SCRATCH(ANTI_ALIAS), 0xf },
      { }
   },
   .params = (uint32_t[]) { 0xffff0002 },
   .expected = (struct nvk_mme_mthd_data[]) {
      { NVK_SET_MME_SCRATCH(ANTI_ALIAS), 0x2 },
      { NV9097_SET_HYBRID_ANTI_ALIAS_CONTROL, 0x1 },
      { NV9097_LOAD_CONSTANT_BUFFER_OFFSET,
        nvk_root_descriptor_offset(draw.sample_masks) },
      { NV9097_LOAD_CONSTANT_BUFFER(0), 0x020001 },
      { NV9097_LOAD_CONSTANT_BUFFER(1), 0x080004 },
      { NV9097_LOAD_CONSTANT_BUFFER(2), 0x200010 },
      { NV9097_LOAD_CONSTANT_BUFFER(3), 0x800040 },
      { }
   },
}, {
   /* 8 samples, minSampleShading = 0.5 */
   .init = (struct nvk_mme_mthd_data[]) {
      { NVK_SET_MME_SCRATCH(ANTI_ALIAS), 0x1 },
      { NVK_SET_MME_SCRATCH(SAMPLE_MASKS_4PASS_0), 0x030003 },
      { NVK_SET_MME_SCRATCH(SAMPLE_MASKS_4PASS_1), 0x0c000c },
      { NVK_SET_MME_SCRATCH(SAMPLE_MASKS_4PASS_2), 0x300030 },
      { NVK_SET_MME_SCRATCH(SAMPLE_MASKS_4PASS_3), 0xc000c0 },
      { }
   },
   .params = (uint32_t[]) { 0x00f00030 },
   .expected = (struct nvk_mme_mthd_data[]) {
      { NVK_SET_MME_SCRATCH(ANTI_ALIAS), 0x31 },
      { NV9097_SET_HYBRID_ANTI_ALIAS_CONTROL, 0x14 },
      { NV9097_LOAD_CONSTANT_BUFFER_OFFSET,
        nvk_root_descriptor_offset(draw.sample_masks) },
      { NV9097_LOAD_CONSTANT_BUFFER(0), 0x030003 },
      { NV9097_LOAD_CONSTANT_BUFFER(1), 0x0c000c },
      { NV9097_LOAD_CONSTANT_BUFFER(2), 0x300030 },
      { NV9097_LOAD_CONSTANT_BUFFER(3), 0xc000c0 },
      { }
   },
}, {
   /* 8 samples, minSampleShading = 0.25 */
   .init = (struct nvk_mme_mthd_data[]) {
      { NVK_SET_MME_SCRATCH(ANTI_ALIAS), 0x30 },
      { NVK_SET_MME_SCRATCH(SAMPLE_MASKS_2PASS_0), 0x0f000f },
      { NVK_SET_MME_SCRATCH(SAMPLE_MASKS_2PASS_1), 0x0f000f },
      { NVK_SET_MME_SCRATCH(SAMPLE_MASKS_2PASS_2), 0xf000f0 },
      { NVK_SET_MME_SCRATCH(SAMPLE_MASKS_2PASS_3), 0xf000f0 },
      { }
   },
   .params = (uint32_t[]) { 0x000f0002 },
   .expected = (struct nvk_mme_mthd_data[]) {
      { NVK_SET_MME_SCRATCH(ANTI_ALIAS), 0x32 },
      { NV9097_SET_HYBRID_ANTI_ALIAS_CONTROL, 0x12 },
      { NV9097_LOAD_CONSTANT_BUFFER_OFFSET,
        nvk_root_descriptor_offset(draw.sample_masks) },
      { NV9097_LOAD_CONSTANT_BUFFER(0), 0x0f000f },
      { NV9097_LOAD_CONSTANT_BUFFER(1), 0x0f000f },
      { NV9097_LOAD_CONSTANT_BUFFER(2), 0xf000f0 },
      { NV9097_LOAD_CONSTANT_BUFFER(3), 0xf000f0 },
      { }
   },
}, {}};

static VkSampleLocationEXT
vk_sample_location(const struct vk_sample_locations_state *sl,
                   uint32_t x, uint32_t y, uint32_t s)
{
   x = x % sl->grid_size.width;
   y = y % sl->grid_size.height;

   return sl->locations[(x + y * sl->grid_size.width) * sl->per_pixel + s];
}

static struct nak_sample_location
vk_to_nak_sample_location(VkSampleLocationEXT loc)
{
   return (struct nak_sample_location) {
      .x_u4 = util_bitpack_ufixed_clamp(loc.x, 0, 3, 4),
      .y_u4 = util_bitpack_ufixed_clamp(loc.y, 0, 3, 4),
   };
}

static void
nvk_flush_ms_state(struct nvk_cmd_buffer *cmd)
{
   const struct nvk_rendering_state *render = &cmd->state.gfx.render;
   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_RASTERIZATION_SAMPLES)) {
      /* When we don't have any attachments, we can't know the sample count
       * from the render pass so we need to emit SET_ANTI_ALIAS here.  See the
       * comment in nvk_BeginRendering() for more details.
       */
      if (render->samples == 0) {
         /* Multisample information MAY be missing (rasterizationSamples == 0)
          * if rasterizer discard is enabled.  However, this isn't valid in
          * the hardware so always use at least one sample.
          */
         const uint32_t samples = MAX2(1, dyn->ms.rasterization_samples);
         nvk_cmd_set_sample_layout(cmd, nil_choose_sample_layout(samples));
      } else {
         /* Multisample information MAY be missing (rasterizationSamples == 0)
          * if rasterizer discard is enabled.
          */
         assert(dyn->ms.rasterization_samples == 0 ||
                dyn->ms.rasterization_samples == render->samples);
      }
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_ALPHA_TO_COVERAGE_ENABLE) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_ALPHA_TO_ONE_ENABLE)) {
      struct nv_push *p = nvk_cmd_buffer_push(cmd, 2);
      P_IMMD(p, NV9097, SET_ANTI_ALIAS_ALPHA_CONTROL, {
         .alpha_to_coverage = dyn->ms.alpha_to_coverage_enable,
         .alpha_to_one      = dyn->ms.alpha_to_one_enable,
      });
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_RASTERIZATION_SAMPLES) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_SAMPLE_LOCATIONS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_SAMPLE_LOCATIONS_ENABLE)) {
      const struct vk_sample_locations_state *sl;
      if (dyn->ms.sample_locations_enable) {
         sl = dyn->ms.sample_locations;
      } else {
         const uint32_t samples = MAX2(1, dyn->ms.rasterization_samples);
         sl = vk_standard_sample_locations_state(samples);
      }

      struct nak_sample_location push_sl[NVK_MAX_SAMPLES];
      for (uint32_t i = 0; i < sl->per_pixel; i++)
         push_sl[i] = vk_to_nak_sample_location(sl->locations[i]);

      nvk_descriptor_state_set_root_array(cmd, &cmd->state.gfx.descriptors,
                                          draw.sample_locations,
                                          0, NVK_MAX_SAMPLES, push_sl);

      if (nvk_cmd_buffer_3d_cls(cmd) >= MAXWELL_B) {
         struct nak_sample_location loc[16];
         for (uint32_t n = 0; n < ARRAY_SIZE(loc); n++) {
            const uint32_t s = n % sl->per_pixel;
            const uint32_t px = n / sl->per_pixel;
            const uint32_t x = px % 2;
            const uint32_t y = px / 2;

            loc[n] = vk_to_nak_sample_location(vk_sample_location(sl, x, y, s));
         }

         struct nv_push *p = nvk_cmd_buffer_push(cmd, 5);

         P_MTHD(p, NVB197, SET_ANTI_ALIAS_SAMPLE_POSITIONS(0));
         for (uint32_t i = 0; i < 4; i++) {
            P_NVB197_SET_ANTI_ALIAS_SAMPLE_POSITIONS(p, i, {
               .x0 = loc[i * 4 + 0].x_u4,
               .y0 = loc[i * 4 + 0].y_u4,
               .x1 = loc[i * 4 + 1].x_u4,
               .y1 = loc[i * 4 + 1].y_u4,
               .x2 = loc[i * 4 + 2].x_u4,
               .y2 = loc[i * 4 + 2].y_u4,
               .x3 = loc[i * 4 + 3].x_u4,
               .y3 = loc[i * 4 + 3].y_u4,
            });
         }
      }
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_MS_SAMPLE_MASK)) {
      struct nv_push *p = nvk_cmd_buffer_push(cmd, 5);
      P_MTHD(p, NV9097, SET_SAMPLE_MASK_X0_Y0);
      P_NV9097_SET_SAMPLE_MASK_X0_Y0(p, dyn->ms.sample_mask & 0xffff);
      P_NV9097_SET_SAMPLE_MASK_X1_Y0(p, dyn->ms.sample_mask & 0xffff);
      P_NV9097_SET_SAMPLE_MASK_X0_Y1(p, dyn->ms.sample_mask & 0xffff);
      P_NV9097_SET_SAMPLE_MASK_X1_Y1(p, dyn->ms.sample_mask & 0xffff);
   }
}

static uint32_t
vk_to_nv9097_compare_op(VkCompareOp vk_op)
{
   ASSERTED static const uint16_t vk_to_nv9097[] = {
      [VK_COMPARE_OP_NEVER]            = NV9097_SET_DEPTH_FUNC_V_OGL_NEVER,
      [VK_COMPARE_OP_LESS]             = NV9097_SET_DEPTH_FUNC_V_OGL_LESS,
      [VK_COMPARE_OP_EQUAL]            = NV9097_SET_DEPTH_FUNC_V_OGL_EQUAL,
      [VK_COMPARE_OP_LESS_OR_EQUAL]    = NV9097_SET_DEPTH_FUNC_V_OGL_LEQUAL,
      [VK_COMPARE_OP_GREATER]          = NV9097_SET_DEPTH_FUNC_V_OGL_GREATER,
      [VK_COMPARE_OP_NOT_EQUAL]        = NV9097_SET_DEPTH_FUNC_V_OGL_NOTEQUAL,
      [VK_COMPARE_OP_GREATER_OR_EQUAL] = NV9097_SET_DEPTH_FUNC_V_OGL_GEQUAL,
      [VK_COMPARE_OP_ALWAYS]           = NV9097_SET_DEPTH_FUNC_V_OGL_ALWAYS,
   };
   assert(vk_op < ARRAY_SIZE(vk_to_nv9097));

   uint32_t nv9097_op = 0x200 | vk_op;
   assert(nv9097_op == vk_to_nv9097[vk_op]);
   return nv9097_op;
}

static uint32_t
vk_to_nv9097_stencil_op(VkStencilOp vk_op)
{
#define OP(vk, nv) [VK_STENCIL_OP_##vk] = NV9097_SET_STENCIL_OP_FAIL_V_##nv
   ASSERTED static const uint16_t vk_to_nv9097[] = {
      OP(KEEP,                D3D_KEEP),
      OP(ZERO,                D3D_ZERO),
      OP(REPLACE,             D3D_REPLACE),
      OP(INCREMENT_AND_CLAMP, D3D_INCRSAT),
      OP(DECREMENT_AND_CLAMP, D3D_DECRSAT),
      OP(INVERT,              D3D_INVERT),
      OP(INCREMENT_AND_WRAP,  D3D_INCR),
      OP(DECREMENT_AND_WRAP,  D3D_DECR),
   };
   assert(vk_op < ARRAY_SIZE(vk_to_nv9097));
#undef OP

   uint32_t nv9097_op = vk_op + 1;
   assert(nv9097_op == vk_to_nv9097[vk_op]);
   return nv9097_op;
}

static void
nvk_flush_ds_state(struct nvk_cmd_buffer *cmd)
{
   struct nv_push *p = nvk_cmd_buffer_push(cmd, 35);

   const struct nvk_rendering_state *render = &cmd->state.gfx.render;
   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_TEST_ENABLE)) {
      bool enable = dyn->ds.depth.test_enable &&
                    render->depth_att.vk_format != VK_FORMAT_UNDEFINED;
      P_IMMD(p, NV9097, SET_DEPTH_TEST, enable);
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_WRITE_ENABLE)) {
      bool enable = dyn->ds.depth.write_enable &&
                    render->depth_att.vk_format != VK_FORMAT_UNDEFINED;
      P_IMMD(p, NV9097, SET_DEPTH_WRITE, enable);
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_COMPARE_OP)) {
      const uint32_t func = vk_to_nv9097_compare_op(dyn->ds.depth.compare_op);
      P_IMMD(p, NV9097, SET_DEPTH_FUNC, func);
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_ENABLE)) {
      bool enable = dyn->ds.depth.bounds_test.enable &&
                    render->depth_att.vk_format != VK_FORMAT_UNDEFINED;
      P_IMMD(p, NV9097, SET_DEPTH_BOUNDS_TEST, enable);
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_BOUNDS)) {
      P_MTHD(p, NV9097, SET_DEPTH_BOUNDS_MIN);
      P_NV9097_SET_DEPTH_BOUNDS_MIN(p, fui(dyn->ds.depth.bounds_test.min));
      P_NV9097_SET_DEPTH_BOUNDS_MAX(p, fui(dyn->ds.depth.bounds_test.max));
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_TEST_ENABLE)) {
      bool enable = dyn->ds.stencil.test_enable &&
                    render->stencil_att.vk_format != VK_FORMAT_UNDEFINED;
      P_IMMD(p, NV9097, SET_STENCIL_TEST, enable);
   }

   const struct vk_stencil_test_face_state *front = &dyn->ds.stencil.front;
   const struct vk_stencil_test_face_state *back = &dyn->ds.stencil.back;
   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_OP)) {
      P_MTHD(p, NV9097, SET_STENCIL_OP_FAIL);
      P_NV9097_SET_STENCIL_OP_FAIL(p, vk_to_nv9097_stencil_op(front->op.fail));
      P_NV9097_SET_STENCIL_OP_ZFAIL(p, vk_to_nv9097_stencil_op(front->op.depth_fail));
      P_NV9097_SET_STENCIL_OP_ZPASS(p, vk_to_nv9097_stencil_op(front->op.pass));
      P_NV9097_SET_STENCIL_FUNC(p, vk_to_nv9097_compare_op(front->op.compare));

      P_MTHD(p, NV9097, SET_BACK_STENCIL_OP_FAIL);
      P_NV9097_SET_BACK_STENCIL_OP_FAIL(p, vk_to_nv9097_stencil_op(back->op.fail));
      P_NV9097_SET_BACK_STENCIL_OP_ZFAIL(p, vk_to_nv9097_stencil_op(back->op.depth_fail));
      P_NV9097_SET_BACK_STENCIL_OP_ZPASS(p, vk_to_nv9097_stencil_op(back->op.pass));
      P_NV9097_SET_BACK_STENCIL_FUNC(p, vk_to_nv9097_compare_op(back->op.compare));
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_COMPARE_MASK)) {
      P_IMMD(p, NV9097, SET_STENCIL_FUNC_MASK, front->compare_mask);
      P_IMMD(p, NV9097, SET_BACK_STENCIL_FUNC_MASK, back->compare_mask);
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_WRITE_MASK)) {
      P_IMMD(p, NV9097, SET_STENCIL_MASK, front->write_mask);
      P_IMMD(p, NV9097, SET_BACK_STENCIL_MASK, back->write_mask);
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_REFERENCE)) {
      P_IMMD(p, NV9097, SET_STENCIL_FUNC_REF, front->reference);
      P_IMMD(p, NV9097, SET_BACK_STENCIL_FUNC_REF, back->reference);
   }
}

static uint32_t
vk_to_nv9097_logic_op(VkLogicOp vk_op)
{
   ASSERTED uint16_t vk_to_nv9097[] = {
      [VK_LOGIC_OP_CLEAR]           = NV9097_SET_LOGIC_OP_FUNC_V_CLEAR,
      [VK_LOGIC_OP_AND]             = NV9097_SET_LOGIC_OP_FUNC_V_AND,
      [VK_LOGIC_OP_AND_REVERSE]     = NV9097_SET_LOGIC_OP_FUNC_V_AND_REVERSE,
      [VK_LOGIC_OP_COPY]            = NV9097_SET_LOGIC_OP_FUNC_V_COPY,
      [VK_LOGIC_OP_AND_INVERTED]    = NV9097_SET_LOGIC_OP_FUNC_V_AND_INVERTED,
      [VK_LOGIC_OP_NO_OP]           = NV9097_SET_LOGIC_OP_FUNC_V_NOOP,
      [VK_LOGIC_OP_XOR]             = NV9097_SET_LOGIC_OP_FUNC_V_XOR,
      [VK_LOGIC_OP_OR]              = NV9097_SET_LOGIC_OP_FUNC_V_OR,
      [VK_LOGIC_OP_NOR]             = NV9097_SET_LOGIC_OP_FUNC_V_NOR,
      [VK_LOGIC_OP_EQUIVALENT]      = NV9097_SET_LOGIC_OP_FUNC_V_EQUIV,
      [VK_LOGIC_OP_INVERT]          = NV9097_SET_LOGIC_OP_FUNC_V_INVERT,
      [VK_LOGIC_OP_OR_REVERSE]      = NV9097_SET_LOGIC_OP_FUNC_V_OR_REVERSE,
      [VK_LOGIC_OP_COPY_INVERTED]   = NV9097_SET_LOGIC_OP_FUNC_V_COPY_INVERTED,
      [VK_LOGIC_OP_OR_INVERTED]     = NV9097_SET_LOGIC_OP_FUNC_V_OR_INVERTED,
      [VK_LOGIC_OP_NAND]            = NV9097_SET_LOGIC_OP_FUNC_V_NAND,
      [VK_LOGIC_OP_SET]             = NV9097_SET_LOGIC_OP_FUNC_V_SET,
   };
   assert(vk_op < ARRAY_SIZE(vk_to_nv9097));

   uint32_t nv9097_op = 0x1500 | vk_op;
   assert(nv9097_op == vk_to_nv9097[vk_op]);
   return nv9097_op;
}

static uint32_t
vk_to_nv9097_blend_op(VkBlendOp vk_op)
{
#define OP(vk, nv) [VK_BLEND_OP_##vk] = NV9097_SET_BLEND_COLOR_OP_V_OGL_##nv
   ASSERTED uint16_t vk_to_nv9097[] = {
      OP(ADD,              FUNC_ADD),
      OP(SUBTRACT,         FUNC_SUBTRACT),
      OP(REVERSE_SUBTRACT, FUNC_REVERSE_SUBTRACT),
      OP(MIN,              MIN),
      OP(MAX,              MAX),
   };
   assert(vk_op < ARRAY_SIZE(vk_to_nv9097));
#undef OP

   return vk_to_nv9097[vk_op];
}

static uint32_t
vk_to_nv9097_blend_factor(VkBlendFactor vk_factor)
{
#define FACTOR(vk, nv) [VK_BLEND_FACTOR_##vk] = \
   NV9097_SET_BLEND_COLOR_SOURCE_COEFF_V_##nv
   ASSERTED uint16_t vk_to_nv9097[] = {
      FACTOR(ZERO,                     OGL_ZERO),
      FACTOR(ONE,                      OGL_ONE),
      FACTOR(SRC_COLOR,                OGL_SRC_COLOR),
      FACTOR(ONE_MINUS_SRC_COLOR,      OGL_ONE_MINUS_SRC_COLOR),
      FACTOR(DST_COLOR,                OGL_DST_COLOR),
      FACTOR(ONE_MINUS_DST_COLOR,      OGL_ONE_MINUS_DST_COLOR),
      FACTOR(SRC_ALPHA,                OGL_SRC_ALPHA),
      FACTOR(ONE_MINUS_SRC_ALPHA,      OGL_ONE_MINUS_SRC_ALPHA),
      FACTOR(DST_ALPHA,                OGL_DST_ALPHA),
      FACTOR(ONE_MINUS_DST_ALPHA,      OGL_ONE_MINUS_DST_ALPHA),
      FACTOR(CONSTANT_COLOR,           OGL_CONSTANT_COLOR),
      FACTOR(ONE_MINUS_CONSTANT_COLOR, OGL_ONE_MINUS_CONSTANT_COLOR),
      FACTOR(CONSTANT_ALPHA,           OGL_CONSTANT_ALPHA),
      FACTOR(ONE_MINUS_CONSTANT_ALPHA, OGL_ONE_MINUS_CONSTANT_ALPHA),
      FACTOR(SRC_ALPHA_SATURATE,       OGL_SRC_ALPHA_SATURATE),
      FACTOR(SRC1_COLOR,               OGL_SRC1COLOR),
      FACTOR(ONE_MINUS_SRC1_COLOR,     OGL_INVSRC1COLOR),
      FACTOR(SRC1_ALPHA,               OGL_SRC1ALPHA),
      FACTOR(ONE_MINUS_SRC1_ALPHA,     OGL_INVSRC1ALPHA),
   };
   assert(vk_factor < ARRAY_SIZE(vk_to_nv9097));
#undef FACTOR

   return vk_to_nv9097[vk_factor];
}

void
nvk_mme_set_write_mask(struct mme_builder *b)
{
   struct mme_value count = mme_load(b);
   struct mme_value mask = mme_load(b);

   /*
    * mask is a bit field
    *
    * attachment index 88887777666655554444333322221111
    * component        abgrabgrabgrabgrabgrabgrabgrabgr
   */

   struct mme_value common_mask = mme_mov(b, mme_imm(1));
   struct mme_value first = mme_and(b, mask, mme_imm(BITFIELD_RANGE(0, 4)));
   struct mme_value i = mme_mov(b, mme_zero());

   mme_while(b, ine, i, count) {
      /*
         We call NV9097_SET_CT_WRITE per attachment. It needs a value as:
         0x0000 0000 0000 0000 000a 000b 000g 000r

         So for i=0 a mask of
         0x0000 0000 0000 0000 0000 0000 0000 1111
         becomes
         0x0000 0000 0000 0000 0001 0001 0001 0001
      */

      struct mme_value val = mme_merge(b, mme_zero(), mask, 0, 1, 0);
      mme_merge_to(b, val, val, mask, 4, 1, 1);
      mme_merge_to(b, val, val, mask, 8, 1, 2);
      mme_merge_to(b, val, val, mask, 12, 1, 3);

      mme_mthd_arr(b, NV9097_SET_CT_WRITE(0), i);
      mme_emit(b, val);
      mme_free_reg(b, val);

      /* Check if all masks are common */
      struct mme_value temp = mme_add(b, mask, mme_imm(BITFIELD_RANGE(0, 4)));
      mme_if(b, ine, first, temp) {
         mme_mov_to(b, common_mask, mme_zero());
      }
      mme_free_reg(b, temp);

      mme_srl_to(b, mask, mask, mme_imm(4));

      mme_add_to(b, i, i, mme_imm(1));
   }

   mme_mthd(b, NV9097_SET_SINGLE_CT_WRITE_CONTROL);
   mme_emit(b, common_mask);
}

static void
nvk_flush_cb_state(struct nvk_cmd_buffer *cmd)
{
   struct nvk_rendering_state *render = &cmd->state.gfx.render;
   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;

   struct nv_push *p =
      nvk_cmd_buffer_push(cmd, 15 + 10 * render->color_att_count);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_LOGIC_OP_ENABLE))
      P_IMMD(p, NV9097, SET_LOGIC_OP, dyn->cb.logic_op_enable);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_LOGIC_OP)) {
      const uint32_t func = vk_to_nv9097_logic_op(dyn->cb.logic_op);
      P_IMMD(p, NV9097, SET_LOGIC_OP_FUNC, func);
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_BLEND_ENABLES)) {
      for (uint8_t a = 0; a < render->color_att_count; a++) {
         P_IMMD(p, NV9097, SET_BLEND(a), dyn->cb.attachments[a].blend_enable);
      }
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_BLEND_EQUATIONS)) {
      for (uint8_t a = 0; a < render->color_att_count; a++) {
         const struct vk_color_blend_attachment_state *att =
            &dyn->cb.attachments[a];
         P_MTHD(p, NV9097, SET_BLEND_PER_TARGET_SEPARATE_FOR_ALPHA(a));
         P_NV9097_SET_BLEND_PER_TARGET_SEPARATE_FOR_ALPHA(p, a, ENABLE_TRUE);
         P_NV9097_SET_BLEND_PER_TARGET_COLOR_OP(p, a,
               vk_to_nv9097_blend_op(att->color_blend_op));
         P_NV9097_SET_BLEND_PER_TARGET_COLOR_SOURCE_COEFF(p, a,
               vk_to_nv9097_blend_factor(att->src_color_blend_factor));
         P_NV9097_SET_BLEND_PER_TARGET_COLOR_DEST_COEFF(p, a,
               vk_to_nv9097_blend_factor(att->dst_color_blend_factor));
         P_NV9097_SET_BLEND_PER_TARGET_ALPHA_OP(p, a,
               vk_to_nv9097_blend_op(att->alpha_blend_op));
         P_NV9097_SET_BLEND_PER_TARGET_ALPHA_SOURCE_COEFF(p, a,
               vk_to_nv9097_blend_factor(att->src_alpha_blend_factor));
         P_NV9097_SET_BLEND_PER_TARGET_ALPHA_DEST_COEFF(p, a,
               vk_to_nv9097_blend_factor(att->dst_alpha_blend_factor));
      }
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_WRITE_MASKS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_COLOR_WRITE_ENABLES) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RP_ATTACHMENTS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_COLOR_ATTACHMENT_MAP)) {
      uint32_t color_write_enables = 0x0;
      for (uint8_t a = 0; a < render->color_att_count; a++) {
         if (dyn->cb.color_write_enables & BITFIELD_BIT(a))
            color_write_enables |= 0xf << (4 * a);
      }

      uint32_t cb_att_write_mask = 0x0;
      for (uint8_t a = 0; a < render->color_att_count; a++)
         cb_att_write_mask |= dyn->cb.attachments[a].write_mask << (a * 4);

      uint32_t rp_att_write_mask = 0x0;
      for (uint8_t a = 0; a < MESA_VK_MAX_COLOR_ATTACHMENTS; a++) {
         if (dyn->rp.attachments & (MESA_VK_RP_ATTACHMENT_COLOR_0_BIT << a))
            rp_att_write_mask |= 0xf << (4 * a);
      }

      uint32_t att_has_loc_mask = 0x0;
      for (uint8_t a = 0; a < MESA_VK_MAX_COLOR_ATTACHMENTS; a++) {
         if (dyn->cal.color_map[a] != MESA_VK_ATTACHMENT_UNUSED)
            att_has_loc_mask |= 0xf << (4 * a);
      }

      P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_SET_WRITE_MASK));
      P_INLINE_DATA(p, render->color_att_count);
      P_INLINE_DATA(p, color_write_enables &
                       cb_att_write_mask &
                       rp_att_write_mask &
                       att_has_loc_mask);
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_COLOR_ATTACHMENT_MAP)) {
      int8_t loc_att[NVK_MAX_RTS] = { -1, -1, -1, -1, -1, -1, -1, -1};
      uint8_t max_loc = 0;
      uint32_t att_used = 0;
      for (uint8_t a = 0; a < MESA_VK_MAX_COLOR_ATTACHMENTS; a++) {
         if (dyn->cal.color_map[a] == MESA_VK_ATTACHMENT_UNUSED)
            continue;

         att_used |= BITFIELD_BIT(a);

         assert(dyn->cal.color_map[a] < NVK_MAX_RTS);
         loc_att[dyn->cal.color_map[a]] = a;
         max_loc = MAX2(max_loc, dyn->cal.color_map[a]);
      }

      for (uint8_t l = 0; l < NVK_MAX_RTS; l++) {
         if (loc_att[l] >= 0)
            continue;

         /* Just grab any color attachment.  The way we set up color targets
          * in BeginRenderPass ensures that every color target is either the
          * valid color target referenced by this render pass or a valid NULL
          * target.  If we end up mapping to some other target in this render
          * pass, the handling of att_has_loc_mask above will ensure that no
          * color writes actually happen.
          */
         uint8_t a = ffs(~att_used) - 1;
         att_used |= BITFIELD_BIT(a);
         loc_att[l] = a;
      }

      P_IMMD(p, NV9097, SET_CT_SELECT, {
         .target_count = max_loc + 1,
         .target0 = loc_att[0],
         .target1 = loc_att[1],
         .target2 = loc_att[2],
         .target3 = loc_att[3],
         .target4 = loc_att[4],
         .target5 = loc_att[5],
         .target6 = loc_att[6],
         .target7 = loc_att[7],
      });
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_BLEND_CONSTANTS)) {
      P_MTHD(p, NV9097, SET_BLEND_CONST_RED);
      P_NV9097_SET_BLEND_CONST_RED(p,     fui(dyn->cb.blend_constants[0]));
      P_NV9097_SET_BLEND_CONST_GREEN(p,   fui(dyn->cb.blend_constants[1]));
      P_NV9097_SET_BLEND_CONST_BLUE(p,    fui(dyn->cb.blend_constants[2]));
      P_NV9097_SET_BLEND_CONST_ALPHA(p,   fui(dyn->cb.blend_constants[3]));
   }
}

void
nvk_cmd_flush_gfx_dynamic_state(struct nvk_cmd_buffer *cmd)
{
   struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;

   if (!vk_dynamic_graphics_state_any_dirty(dyn))
      return;

   nvk_flush_vi_state(cmd);
   nvk_flush_ia_state(cmd);
   nvk_flush_ts_state(cmd);
   nvk_flush_vp_state(cmd);
   nvk_flush_rs_state(cmd);
   nvk_flush_fsr_state(cmd);
   nvk_flush_ms_state(cmd);
   nvk_flush_ds_state(cmd);
   nvk_flush_cb_state(cmd);

   vk_dynamic_graphics_state_clear_dirty(dyn);
}

void
nvk_mme_bind_cbuf_desc(struct mme_builder *b)
{
   /* First 4 bits are group, later bits are slot */
   struct mme_value group_slot = mme_load(b);

   struct mme_value addr_lo, addr_hi, size;
   if (nvk_use_bindless_cbuf(b->devinfo)) {
      if (b->devinfo->cls_eng3d >= TURING_A) {
         struct mme_value64 addr = mme_load_addr64(b);
         mme_tu104_read_fifoed(b, addr, mme_imm(2));
      }

      /* Load the descriptor */
      struct mme_value desc_lo = mme_load(b);
      struct mme_value desc_hi = mme_load(b);

      /* The bottom 45 bits are addr >> 4 */
      addr_lo = mme_merge(b, mme_zero(), desc_lo, 4, 28, 0);
      addr_hi = mme_merge(b, mme_zero(), desc_lo, 0, 4, 28);
      mme_merge_to(b, addr_hi, addr_hi, desc_hi, 4, 13, 0);

      /* The top 19 bits are size >> 4 */
      size = mme_merge(b, mme_zero(), desc_hi, 4, 19, 13);

      mme_free_reg(b, desc_hi);
      mme_free_reg(b, desc_lo);
   } else {
      if (b->devinfo->cls_eng3d >= TURING_A) {
         struct mme_value64 addr = mme_load_addr64(b);
         mme_tu104_read_fifoed(b, addr, mme_imm(3));
      }

      /* Load the descriptor */
      addr_lo = mme_load(b);
      addr_hi = mme_load(b);
      size = mme_load(b);
   }

   struct mme_value cb = mme_alloc_reg(b);
   mme_if(b, ieq, size, mme_zero()) {
      /* Bottim bit is the valid bit, 8:4 are shader slot */
      mme_merge_to(b, cb, mme_zero(), group_slot, 4, 5, 4);
   }

   mme_if(b, ine, size, mme_zero()) {
      /* size = max(size, NVK_MAX_CBUF_SIZE) */
      assert(util_is_power_of_two_nonzero(NVK_MAX_CBUF_SIZE));
      struct mme_value is_large =
         mme_and(b, size, mme_imm(~(NVK_MAX_CBUF_SIZE - 1)));
      mme_if(b, ine, is_large, mme_zero()) {
         mme_mov_to(b, size, mme_imm(NVK_MAX_CBUF_SIZE));
      }

      mme_mthd(b, NV9097_SET_CONSTANT_BUFFER_SELECTOR_A);
      mme_emit(b, size);
      mme_emit(b, addr_hi);
      mme_emit(b, addr_lo);

      /* Bottom bit is the valid bit, 8:4 are shader slot */
      mme_merge_to(b, cb, mme_imm(1), group_slot, 4, 5, 4);
   }

   mme_free_reg(b, addr_hi);
   mme_free_reg(b, addr_lo);
   mme_free_reg(b, size);

   /* The group comes in the bottom 4 bits in group_slot and we need to
    * combine it with the method.  However, unlike most array methods with a
    * stride if 1 dword, BIND_GROUP_CONSTANT_BUFFER has a stride of 32B or 8
    * dwords.  This means we need to also shift by 3.
    */
   struct mme_value group = mme_merge(b, mme_imm(0), group_slot, 3, 4, 0);
   mme_mthd_arr(b, NV9097_BIND_GROUP_CONSTANT_BUFFER(0), group);
   mme_emit(b, cb);
}

void
nvk_cmd_flush_gfx_cbufs(struct nvk_cmd_buffer *cmd)
{
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);
   struct nvk_physical_device *pdev = nvk_device_physical(dev);
   const uint32_t min_cbuf_alignment = nvk_min_cbuf_alignment(&pdev->info);
   struct nvk_descriptor_state *desc = &cmd->state.gfx.descriptors;

   /* Find cbuf maps for the 5 cbuf groups */
   const struct nvk_shader *cbuf_shaders[5] = { NULL, };
   for (gl_shader_stage stage = 0; stage < MESA_SHADER_STAGES; stage++) {
      const struct nvk_shader *shader = cmd->state.gfx.shaders[stage];
      if (shader == NULL)
         continue;

      uint32_t group = nvk_cbuf_binding_for_stage(stage);
      assert(group < ARRAY_SIZE(cbuf_shaders));
      cbuf_shaders[group] = shader;
   }

   bool bound_any_cbuf = false;
   for (uint32_t g = 0; g < ARRAY_SIZE(cbuf_shaders); g++) {
      if (cbuf_shaders[g] == NULL)
         continue;

      const struct nvk_shader *shader = cbuf_shaders[g];
      const struct nvk_cbuf_map *cbuf_map = &shader->cbuf_map;
      struct nvk_cbuf_group *group = &cmd->state.gfx.cbuf_groups[g];

      /* We only bother to re-bind cbufs that are in use */
      const uint32_t rebind =
         group->dirty & BITFIELD_MASK(cbuf_map->cbuf_count);
      if (!rebind)
         continue;

      u_foreach_bit(c, rebind) {
         const struct nvk_cbuf *cbuf = &group->cbufs[c];

         /* We bind these at the very end */
         if (cbuf->type == NVK_CBUF_TYPE_ROOT_DESC)
            continue;

         bound_any_cbuf = true;

         struct nvk_buffer_address ba;
         if (nvk_cmd_buffer_get_cbuf_addr(cmd, desc, shader, cbuf, &ba)) {
            assert(ba.base_addr % min_cbuf_alignment == 0);
            ba.size = align(ba.size, min_cbuf_alignment);
            ba.size = MIN2(ba.size, NVK_MAX_CBUF_SIZE);

            struct nv_push *p = nvk_cmd_buffer_push(cmd, 6);

            if (ba.size > 0) {
               P_MTHD(p, NV9097, SET_CONSTANT_BUFFER_SELECTOR_A);
               P_NV9097_SET_CONSTANT_BUFFER_SELECTOR_A(p, ba.size);
               P_NV9097_SET_CONSTANT_BUFFER_SELECTOR_B(p, ba.base_addr >> 32);
               P_NV9097_SET_CONSTANT_BUFFER_SELECTOR_C(p, ba.base_addr);
            }

            P_IMMD(p, NV9097, BIND_GROUP_CONSTANT_BUFFER(g), {
               .valid = ba.size > 0,
               .shader_slot = c,
            });
         } else {
            uint64_t desc_addr =
               nvk_cmd_buffer_get_cbuf_descriptor_addr(cmd, desc, cbuf);

            if (nvk_cmd_buffer_3d_cls(cmd) >= TURING_A) {
               struct nv_push *p = nvk_cmd_buffer_push(cmd, 4);

               P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_BIND_CBUF_DESC));
               P_INLINE_DATA(p, g | (c << 4));
               P_INLINE_DATA(p, desc_addr >> 32);
               P_INLINE_DATA(p, desc_addr);
            } else {
               struct nv_push *p = nvk_cmd_buffer_push(cmd, 2);

               P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_BIND_CBUF_DESC));
               P_INLINE_DATA(p, g | (c << 4));

               nv_push_update_count(p, 3);
               nvk_cmd_buffer_push_indirect(cmd, desc_addr, 12);
            }
         }
      }

      group->dirty &= ~rebind;
   }

   /* We bind all root descriptors last so that CONSTANT_BUFFER_SELECTOR is
    * always left pointing at the root descriptor table.  This way draw
    * parameters and similar MME root table updates always hit the root
    * descriptor table and not some random UBO.
    */
   if (bound_any_cbuf) {
      struct nv_push *p = nvk_cmd_buffer_push(cmd, 2);
      P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_SELECT_CB0));
      P_INLINE_DATA(p, 0);
   }
}

static void
nvk_cmd_flush_gfx_state(struct nvk_cmd_buffer *cmd)
{
   nvk_cmd_buffer_flush_push_descriptors(cmd, &cmd->state.gfx.descriptors);
   nvk_cmd_flush_gfx_dynamic_state(cmd);
   nvk_cmd_flush_gfx_shaders(cmd);
   nvk_cmd_flush_gfx_cbufs(cmd);
}

void
nvk_mme_bind_ib(struct mme_builder *b)
{
   struct mme_value64 addr = mme_load_addr64(b);
   struct mme_value size_B = mme_load(b);

   struct mme_value addr_or = mme_or(b, addr.lo, addr.hi);
   mme_if(b, ieq, addr_or, mme_zero()) {
      mme_mov_to(b, size_B, mme_zero());
   }
   mme_free_reg(b, addr_or);

   if (b->devinfo->cls_eng3d < TURING_A) {
      mme_if(b, ieq, size_B, mme_zero()) {
         nvk_mme_load_scratch_to(b, addr.hi, ZERO_ADDR_HI);
         nvk_mme_load_scratch_to(b, addr.lo, ZERO_ADDR_LO);
      }
   }

   mme_mthd(b, NV9097_SET_INDEX_BUFFER_A);
   mme_emit(b, addr.hi);
   mme_emit(b, addr.lo);

   if (b->devinfo->cls_eng3d >= TURING_A) {
      mme_mthd(b, NVC597_SET_INDEX_BUFFER_SIZE_A);
      mme_emit(b, mme_zero());
      mme_emit(b, size_B);
   } else {
      /* Convert to an end address */
      mme_add64_to(b, addr, addr, mme_value64(size_B, mme_zero()));
      mme_add64_to(b, addr, addr, mme_imm64(-1));

      /* mme_mthd(b, NV9097_SET_INDEX_BUFFER_C); */
      mme_emit(b, addr.hi);
      mme_emit(b, addr.lo);
   }
   mme_free_reg64(b, addr);
   mme_free_reg(b, size_B);

   struct mme_value fmt = mme_load(b);
   struct mme_value restart = mme_mov(b, mme_imm(UINT32_MAX));
   struct mme_value index_type = mme_mov(b,
      mme_imm(NVC597_SET_INDEX_BUFFER_E_INDEX_SIZE_FOUR_BYTES));

   /* The Vulkan and D3D enums don't overlap so we can handle both at the same
    * time with one MME macro.
    */
   UNUSED static const uint32_t DXGI_FORMAT_R32_UINT = 42;
   static const uint32_t DXGI_FORMAT_R16_UINT = 57;
   static const uint32_t DXGI_FORMAT_R8_UINT = 62;

   mme_if(b, ieq, fmt, mme_imm(VK_INDEX_TYPE_UINT16)) {
      mme_mov_to(b, restart, mme_imm(UINT16_MAX));
      mme_mov_to(b, index_type,
                 mme_imm(NVC597_SET_INDEX_BUFFER_E_INDEX_SIZE_TWO_BYTES));
   }

   mme_if(b, ieq, fmt, mme_imm(DXGI_FORMAT_R16_UINT)) {
      mme_mov_to(b, restart, mme_imm(UINT16_MAX));
      mme_mov_to(b, index_type,
                 mme_imm(NVC597_SET_INDEX_BUFFER_E_INDEX_SIZE_TWO_BYTES));
   }

   mme_if(b, ieq, fmt, mme_imm(VK_INDEX_TYPE_UINT8_KHR)) {
      mme_mov_to(b, restart, mme_imm(UINT8_MAX));
      mme_mov_to(b, index_type,
                 mme_imm(NVC597_SET_INDEX_BUFFER_E_INDEX_SIZE_ONE_BYTE));
   }

   mme_if(b, ieq, fmt, mme_imm(DXGI_FORMAT_R8_UINT)) {
      mme_mov_to(b, restart, mme_imm(UINT8_MAX));
      mme_mov_to(b, index_type,
                 mme_imm(NVC597_SET_INDEX_BUFFER_E_INDEX_SIZE_ONE_BYTE));
   }

   mme_mthd(b, NV9097_SET_DA_PRIMITIVE_RESTART_INDEX);
   mme_emit(b, restart);

   mme_mthd(b, NV9097_SET_INDEX_BUFFER_E);
   mme_emit(b, index_type);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdBindIndexBuffer2KHR(VkCommandBuffer commandBuffer,
                           VkBuffer _buffer,
                           VkDeviceSize offset,
                           VkDeviceSize size,
                           VkIndexType indexType)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvk_buffer, buffer, _buffer);
   struct nvk_addr_range addr_range =
      nvk_buffer_addr_range(buffer, offset, size);

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 5);
   P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_BIND_IB));
   P_INLINE_DATA(p, addr_range.addr >> 32);
   P_INLINE_DATA(p, addr_range.addr);
   assert(addr_range.range <= UINT32_MAX);
   P_INLINE_DATA(p, addr_range.range);
   P_INLINE_DATA(p, indexType);
}

void
nvk_mme_bind_vb(struct mme_builder *b)
{
   struct mme_value vb_idx = mme_load(b);
   struct mme_value64 addr = mme_load_addr64(b);
   struct mme_value size_B = mme_load(b);

   struct mme_value addr_or = mme_or(b, addr.lo, addr.hi);
   mme_if(b, ieq, addr_or, mme_zero()) {
      mme_mov_to(b, size_B, mme_zero());
   }
   mme_free_reg(b, addr_or);

   if (b->devinfo->cls_eng3d < TURING_A) {
      mme_if(b, ieq, size_B, mme_zero()) {
         nvk_mme_load_scratch_to(b, addr.hi, ZERO_ADDR_HI);
         nvk_mme_load_scratch_to(b, addr.lo, ZERO_ADDR_LO);
      }
   }

   struct mme_value vb_idx4 = mme_sll(b, vb_idx, mme_imm(2));
   mme_mthd_arr(b, NV9097_SET_VERTEX_STREAM_A_LOCATION_A(0), vb_idx4);
   mme_free_reg(b, vb_idx4);
   mme_emit(b, addr.hi);
   mme_emit(b, addr.lo);

   if (b->devinfo->cls_eng3d >= TURING_A) {
      struct mme_value vb_idx2 = mme_sll(b, vb_idx, mme_imm(1));
      mme_mthd_arr(b, NVC597_SET_VERTEX_STREAM_SIZE_A(0), vb_idx2);
      mme_emit(b, mme_zero());
      mme_emit(b, size_B);
   } else {
      /* Convert to an end address */
      mme_add64_to(b, addr, addr, mme_value64(size_B, mme_zero()));
      mme_add64_to(b, addr, addr, mme_imm64(-1));

      struct mme_value vb_idx2 = mme_sll(b, vb_idx, mme_imm(1));
      mme_mthd_arr(b, NV9097_SET_VERTEX_STREAM_LIMIT_A_A(0), vb_idx2);
      mme_emit(b, addr.hi);
      mme_emit(b, addr.lo);
   }
}

static void
nvk_mme_bind_vb_test_check(const struct nv_device_info *devinfo,
                           const struct nvk_mme_test_case *test,
                           const struct nvk_mme_mthd_data *results)
{
   const uint32_t vb_idx = test->params[0];
   const uint32_t addr_hi = test->params[1];
   const uint32_t addr_lo = test->params[2];

   uint32_t size_B = test->params[3];
   if (addr_hi == 0 && addr_lo == 0)
      size_B = 0;

   assert(results[0].mthd == NV9097_SET_VERTEX_STREAM_A_LOCATION_A(vb_idx));
   assert(results[1].mthd == NV9097_SET_VERTEX_STREAM_A_LOCATION_B(vb_idx));

   if (devinfo->cls_eng3d >= TURING_A) {
      assert(results[0].data == addr_hi);
      assert(results[1].data == addr_lo);

      assert(results[2].mthd == NVC597_SET_VERTEX_STREAM_SIZE_A(3));
      assert(results[3].mthd == NVC597_SET_VERTEX_STREAM_SIZE_B(3));
      assert(results[2].data == 0);
      assert(results[3].data == size_B);
   } else {
      uint64_t addr = ((uint64_t)addr_hi << 32) | addr_lo;
      if (size_B == 0)
         addr = ((uint64_t)test->init[0].data << 32) | test->init[1].data;

      assert(results[0].data == addr >> 32);
      assert(results[1].data == (uint32_t)addr);

      const uint64_t limit = (addr + size_B) - 1;
      assert(results[2].mthd == NV9097_SET_VERTEX_STREAM_LIMIT_A_A(3));
      assert(results[3].mthd == NV9097_SET_VERTEX_STREAM_LIMIT_A_B(3));
      assert(results[2].data == limit >> 32);
      assert(results[3].data == (uint32_t)limit);
   }
}

const struct nvk_mme_test_case nvk_mme_bind_vb_tests[] = {{
   .params = (uint32_t[]) { 3, 0xff3, 0xff4ab000, 0x10000 },
   .check = nvk_mme_bind_vb_test_check,
}, {
   .init = (struct nvk_mme_mthd_data[]) {
      { NVK_SET_MME_SCRATCH(ZERO_ADDR_HI), 0xff3 },
      { NVK_SET_MME_SCRATCH(ZERO_ADDR_LO), 0xff356000 },
      { }
   },
   .params = (uint32_t[]) { 3, 0xff3, 0xff4ab000, 0 },
   .check = nvk_mme_bind_vb_test_check,
}, {
   .init = (struct nvk_mme_mthd_data[]) {
      { NVK_SET_MME_SCRATCH(ZERO_ADDR_HI), 0xff3 },
      { NVK_SET_MME_SCRATCH(ZERO_ADDR_LO), 0xff356000 },
      { }
   },
   .params = (uint32_t[]) { 3, 0, 0, 0x800 },
   .check = nvk_mme_bind_vb_test_check,
}, {}};

void
nvk_cmd_bind_vertex_buffer(struct nvk_cmd_buffer *cmd, uint32_t vb_idx,
                           struct nvk_addr_range addr_range)
{
   /* Used for meta save/restore */
   if (vb_idx == 0)
      cmd->state.gfx.vb0 = addr_range;

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 5);
   P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_BIND_VB));
   P_INLINE_DATA(p, vb_idx);
   P_INLINE_DATA(p, addr_range.addr >> 32);
   P_INLINE_DATA(p, addr_range.addr);
   assert(addr_range.range <= UINT32_MAX);
   P_INLINE_DATA(p, addr_range.range);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdBindVertexBuffers2(VkCommandBuffer commandBuffer,
                          uint32_t firstBinding,
                          uint32_t bindingCount,
                          const VkBuffer *pBuffers,
                          const VkDeviceSize *pOffsets,
                          const VkDeviceSize *pSizes,
                          const VkDeviceSize *pStrides)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   if (pStrides) {
      vk_cmd_set_vertex_binding_strides(&cmd->vk, firstBinding,
                                        bindingCount, pStrides);
   }

   for (uint32_t i = 0; i < bindingCount; i++) {
      VK_FROM_HANDLE(nvk_buffer, buffer, pBuffers[i]);
      uint32_t idx = firstBinding + i;

      uint64_t size = pSizes ? pSizes[i] : VK_WHOLE_SIZE;
      const struct nvk_addr_range addr_range =
         nvk_buffer_addr_range(buffer, pOffsets[i], size);

      nvk_cmd_bind_vertex_buffer(cmd, idx, addr_range);
   }
}

static void
nvk_mme_set_cb0_mthd(struct mme_builder *b,
                     uint16_t cb0_offset,
                     uint16_t mthd,
                     struct mme_value val)
{
   if (b->devinfo->cls_eng3d >= TURING_A) {
      struct mme_value old = mme_state(b, mthd);
      mme_if(b, ine, old, val) {
         mme_mthd(b, mthd);
         mme_emit(b, val);

         mme_mthd(b, NV9097_LOAD_CONSTANT_BUFFER_OFFSET);
         mme_emit(b, mme_imm(cb0_offset));
         mme_mthd(b, NV9097_LOAD_CONSTANT_BUFFER(0));
         mme_emit(b, val);
      }
      mme_free_reg(b, old);
   } else {
      /* Fermi is really tight on registers. Don't bother with the if and set
       * both unconditionally for now.
       */
      mme_mthd(b, mthd);
      mme_emit(b, val);

      mme_mthd(b, NV9097_LOAD_CONSTANT_BUFFER_OFFSET);
      mme_emit(b, mme_imm(cb0_offset));
      mme_mthd(b, NV9097_LOAD_CONSTANT_BUFFER(0));
      mme_emit(b, val);
   }
}

static void
nvk_mme_set_cb0_scratch(struct mme_builder *b,
                        uint16_t cb0_offset,
                        enum nvk_mme_scratch scratch,
                        struct mme_value val)
{
   const uint16_t mthd = NV9097_SET_MME_SHADOW_SCRATCH(scratch);
   nvk_mme_set_cb0_mthd(b, cb0_offset, mthd, val);
}

struct mme_draw_params {
   struct mme_value base_vertex;
   struct mme_value first_vertex;
   struct mme_value first_instance;
   struct mme_value draw_index;
};

static void
nvk_mme_build_set_draw_params(struct mme_builder *b,
                              const struct mme_draw_params *p)
{
   nvk_mme_set_cb0_scratch(b, nvk_root_descriptor_offset(draw.base_vertex),
                           NVK_MME_SCRATCH_CB0_FIRST_VERTEX,
                           p->first_vertex);
   nvk_mme_set_cb0_mthd(b, nvk_root_descriptor_offset(draw.base_instance),
                        NV9097_SET_GLOBAL_BASE_INSTANCE_INDEX,
                        p->first_instance);
   nvk_mme_set_cb0_scratch(b, nvk_root_descriptor_offset(draw.draw_index),
                           NVK_MME_SCRATCH_CB0_DRAW_INDEX,
                           p->draw_index);
   nvk_mme_set_cb0_scratch(b, nvk_root_descriptor_offset(draw.view_index),
                           NVK_MME_SCRATCH_CB0_VIEW_INDEX,
                           mme_zero());

   mme_mthd(b, NV9097_SET_GLOBAL_BASE_VERTEX_INDEX);
   mme_emit(b, p->base_vertex);
   mme_mthd(b, NV9097_SET_VERTEX_ID_BASE);
   mme_emit(b, p->base_vertex);
}

static void
nvk_mme_emit_view_index(struct mme_builder *b, struct mme_value view_index)
{
   /* Set the push constant */
   nvk_mme_set_cb0_scratch(b, nvk_root_descriptor_offset(draw.view_index),
                           NVK_MME_SCRATCH_CB0_VIEW_INDEX,
                           view_index);

   /* Set the layer to the view index */
   STATIC_ASSERT(DRF_LO(NV9097_SET_RT_LAYER_V) == 0);
   STATIC_ASSERT(NV9097_SET_RT_LAYER_CONTROL_V_SELECTS_LAYER == 0);
   mme_mthd(b, NV9097_SET_RT_LAYER);
   mme_emit(b, view_index);
}

static void
nvk_mme_build_draw_loop(struct mme_builder *b,
                        struct mme_value instance_count,
                        struct mme_value first_vertex,
                        struct mme_value vertex_count)
{
   struct mme_value begin = nvk_mme_load_scratch(b, DRAW_BEGIN);

   mme_loop(b, instance_count) {
      mme_mthd(b, NV9097_BEGIN);
      mme_emit(b, begin);

      mme_mthd(b, NV9097_SET_VERTEX_ARRAY_START);
      mme_emit(b, first_vertex);
      mme_emit(b, vertex_count);

      mme_mthd(b, NV9097_END);
      mme_emit(b, mme_zero());

      mme_set_field_enum(b, begin, NV9097_BEGIN_INSTANCE_ID, SUBSEQUENT);
   }

   mme_free_reg(b, begin);
}

static void
nvk_mme_build_draw(struct mme_builder *b,
                   struct mme_value draw_index)
{
   /* These are in VkDrawIndirectCommand order */
   struct mme_value vertex_count = mme_load(b);
   struct mme_value instance_count = mme_load(b);
   struct mme_value first_vertex = mme_load(b);
   struct mme_value first_instance = mme_load(b);

   struct mme_draw_params params = {
      .first_vertex = first_vertex,
      .first_instance = first_instance,
      .draw_index = draw_index,
   };
   nvk_mme_build_set_draw_params(b, &params);

   mme_free_reg(b, first_instance);

   if (b->devinfo->cls_eng3d < TURING_A)
      nvk_mme_spill(b, DRAW_IDX, draw_index);

   struct mme_value view_mask = nvk_mme_load_scratch(b, VIEW_MASK);
   mme_if(b, ieq, view_mask, mme_zero()) {
      mme_free_reg(b, view_mask);

      nvk_mme_build_draw_loop(b, instance_count,
                              first_vertex, vertex_count);
   }

   view_mask = nvk_mme_load_scratch(b, VIEW_MASK);
   mme_if(b, ine, view_mask, mme_zero()) {
      mme_free_reg(b, view_mask);

      struct mme_value view = mme_mov(b, mme_zero());
      mme_while(b, ine, view, mme_imm(32)) {
         view_mask = nvk_mme_load_scratch(b, VIEW_MASK);
         struct mme_value has_view = mme_bfe(b, view_mask, view, 1);
         mme_free_reg(b, view_mask);
         mme_if(b, ine, has_view, mme_zero()) {
            mme_free_reg(b, has_view);
            nvk_mme_emit_view_index(b, view);
            nvk_mme_build_draw_loop(b, instance_count,
                                    first_vertex, vertex_count);
         }

         mme_add_to(b, view, view, mme_imm(1));
      }
      mme_free_reg(b, view);
   }

   mme_free_reg(b, instance_count);
   mme_free_reg(b, first_vertex);
   mme_free_reg(b, vertex_count);

   if (b->devinfo->cls_eng3d < TURING_A)
      nvk_mme_unspill(b, DRAW_IDX, draw_index);
}

void
nvk_mme_draw(struct mme_builder *b)
{
   struct mme_value draw_index = mme_load(b);
   nvk_mme_build_draw(b, draw_index);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdDraw(VkCommandBuffer commandBuffer,
            uint32_t vertexCount,
            uint32_t instanceCount,
            uint32_t firstVertex,
            uint32_t firstInstance)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   nvk_cmd_flush_gfx_state(cmd);

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 6);
   P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_DRAW));
   P_INLINE_DATA(p, 0 /* draw_index */);
   P_INLINE_DATA(p, vertexCount);
   P_INLINE_DATA(p, instanceCount);
   P_INLINE_DATA(p, firstVertex);
   P_INLINE_DATA(p, firstInstance);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdDrawMultiEXT(VkCommandBuffer commandBuffer,
                    uint32_t drawCount,
                    const VkMultiDrawInfoEXT *pVertexInfo,
                    uint32_t instanceCount,
                    uint32_t firstInstance,
                    uint32_t stride)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   nvk_cmd_flush_gfx_state(cmd);

   for (uint32_t draw_index = 0; draw_index < drawCount; draw_index++) {
      struct nv_push *p = nvk_cmd_buffer_push(cmd, 6);
      P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_DRAW));
      P_INLINE_DATA(p, draw_index);
      P_INLINE_DATA(p, pVertexInfo->vertexCount);
      P_INLINE_DATA(p, instanceCount);
      P_INLINE_DATA(p, pVertexInfo->firstVertex);
      P_INLINE_DATA(p, firstInstance);

      pVertexInfo = ((void *)pVertexInfo) + stride;
   }
}

static void
nvk_mme_build_draw_indexed_loop(struct mme_builder *b,
                                struct mme_value instance_count,
                                struct mme_value first_index,
                                struct mme_value index_count)
{
   struct mme_value begin = nvk_mme_load_scratch(b, DRAW_BEGIN);

   mme_loop(b, instance_count) {
      mme_mthd(b, NV9097_BEGIN);
      mme_emit(b, begin);

      mme_mthd(b, NV9097_SET_INDEX_BUFFER_F);
      mme_emit(b, first_index);
      mme_emit(b, index_count);

      mme_mthd(b, NV9097_END);
      mme_emit(b, mme_zero());

      mme_set_field_enum(b, begin, NV9097_BEGIN_INSTANCE_ID, SUBSEQUENT);
   }

   mme_free_reg(b, begin);
}

static void
nvk_mme_build_draw_indexed(struct mme_builder *b,
                           struct mme_value draw_index)
{
   /* These are in VkDrawIndexedIndirectCommand order */
   struct mme_value index_count = mme_load(b);
   struct mme_value instance_count = mme_load(b);
   struct mme_value first_index = mme_load(b);
   struct mme_value vertex_offset = mme_load(b);
   struct mme_value first_instance = mme_load(b);

   struct mme_draw_params params = {
      .base_vertex = vertex_offset,
      .first_vertex = vertex_offset,
      .first_instance = first_instance,
      .draw_index = draw_index,
   };
   nvk_mme_build_set_draw_params(b, &params);

   mme_free_reg(b, vertex_offset);
   mme_free_reg(b, first_instance);

   if (b->devinfo->cls_eng3d < TURING_A)
      nvk_mme_spill(b, DRAW_IDX, draw_index);

   struct mme_value view_mask = nvk_mme_load_scratch(b, VIEW_MASK);
   mme_if(b, ieq, view_mask, mme_zero()) {
      mme_free_reg(b, view_mask);

      nvk_mme_build_draw_indexed_loop(b, instance_count,
                                      first_index, index_count);
   }

   view_mask = nvk_mme_load_scratch(b, VIEW_MASK);
   mme_if(b, ine, view_mask, mme_zero()) {
      mme_free_reg(b, view_mask);

      struct mme_value view = mme_mov(b, mme_zero());
      mme_while(b, ine, view, mme_imm(32)) {
         view_mask = nvk_mme_load_scratch(b, VIEW_MASK);
         struct mme_value has_view = mme_bfe(b, view_mask, view, 1);
         mme_free_reg(b, view_mask);
         mme_if(b, ine, has_view, mme_zero()) {
            mme_free_reg(b, has_view);
            nvk_mme_emit_view_index(b, view);
            nvk_mme_build_draw_indexed_loop(b, instance_count,
                                            first_index, index_count);
         }

         mme_add_to(b, view, view, mme_imm(1));
      }
      mme_free_reg(b, view);
   }

   mme_free_reg(b, instance_count);
   mme_free_reg(b, first_index);
   mme_free_reg(b, index_count);

   if (b->devinfo->cls_eng3d < TURING_A)
      nvk_mme_unspill(b, DRAW_IDX, draw_index);
}

void
nvk_mme_draw_indexed(struct mme_builder *b)
{
   struct mme_value draw_index = mme_load(b);
   nvk_mme_build_draw_indexed(b, draw_index);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdDrawIndexed(VkCommandBuffer commandBuffer,
                   uint32_t indexCount,
                   uint32_t instanceCount,
                   uint32_t firstIndex,
                   int32_t vertexOffset,
                   uint32_t firstInstance)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   nvk_cmd_flush_gfx_state(cmd);

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 7);
   P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_DRAW_INDEXED));
   P_INLINE_DATA(p, 0 /* draw_index */);
   P_INLINE_DATA(p, indexCount);
   P_INLINE_DATA(p, instanceCount);
   P_INLINE_DATA(p, firstIndex);
   P_INLINE_DATA(p, vertexOffset);
   P_INLINE_DATA(p, firstInstance);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdDrawMultiIndexedEXT(VkCommandBuffer commandBuffer,
                           uint32_t drawCount,
                           const VkMultiDrawIndexedInfoEXT *pIndexInfo,
                           uint32_t instanceCount,
                           uint32_t firstInstance,
                           uint32_t stride,
                           const int32_t *pVertexOffset)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   nvk_cmd_flush_gfx_state(cmd);

   for (uint32_t draw_index = 0; draw_index < drawCount; draw_index++) {
      const uint32_t vertex_offset =
         pVertexOffset != NULL ? *pVertexOffset : pIndexInfo->vertexOffset;

      struct nv_push *p = nvk_cmd_buffer_push(cmd, 7);
      P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_DRAW_INDEXED));
      P_INLINE_DATA(p, draw_index);
      P_INLINE_DATA(p, pIndexInfo->indexCount);
      P_INLINE_DATA(p, instanceCount);
      P_INLINE_DATA(p, pIndexInfo->firstIndex);
      P_INLINE_DATA(p, vertex_offset);
      P_INLINE_DATA(p, firstInstance);

      pIndexInfo = ((void *)pIndexInfo) + stride;
   }
}

void
nvk_mme_draw_indirect(struct mme_builder *b)
{
   if (b->devinfo->cls_eng3d >= TURING_A) {
      struct mme_value64 draw_addr = mme_load_addr64(b);
      struct mme_value draw_count = mme_load(b);
      struct mme_value stride = mme_load(b);

      struct mme_value draw = mme_mov(b, mme_zero());
      mme_while(b, ult, draw, draw_count) {
         mme_tu104_read_fifoed(b, draw_addr, mme_imm(4));

         nvk_mme_build_draw(b, draw);

         mme_add_to(b, draw, draw, mme_imm(1));
         mme_add64_to(b, draw_addr, draw_addr, mme_value64(stride, mme_zero()));
      }
   } else {
      struct mme_value draw_count = mme_load(b);
      nvk_mme_load_to_scratch(b, DRAW_PAD_DW);

      struct mme_value draw = mme_mov(b, mme_zero());
      mme_while(b, ine, draw, draw_count) {
         nvk_mme_spill(b, DRAW_COUNT, draw_count);

         nvk_mme_build_draw(b, draw);
         mme_add_to(b, draw, draw, mme_imm(1));

         struct mme_value pad_dw = nvk_mme_load_scratch(b, DRAW_PAD_DW);
         mme_loop(b, pad_dw) {
            mme_free_reg(b, mme_load(b));
         }
         mme_free_reg(b, pad_dw);

         nvk_mme_unspill(b, DRAW_COUNT, draw_count);
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdDrawIndirect(VkCommandBuffer commandBuffer,
                    VkBuffer _buffer,
                    VkDeviceSize offset,
                    uint32_t drawCount,
                    uint32_t stride)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvk_buffer, buffer, _buffer);

   /* From the Vulkan 1.3.238 spec:
    *
    *    VUID-vkCmdDrawIndirect-drawCount-00476
    *
    *    "If drawCount is greater than 1, stride must be a multiple of 4 and
    *    must be greater than or equal to sizeof(VkDrawIndirectCommand)"
    *
    * and
    *
    *    "If drawCount is less than or equal to one, stride is ignored."
    */
   if (drawCount > 1) {
      assert(stride % 4 == 0);
      assert(stride >= sizeof(VkDrawIndirectCommand));
   } else {
      stride = sizeof(VkDrawIndirectCommand);
   }

   nvk_cmd_flush_gfx_state(cmd);

   if (nvk_cmd_buffer_3d_cls(cmd) >= TURING_A) {
      struct nv_push *p = nvk_cmd_buffer_push(cmd, 5);
      P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_DRAW_INDIRECT));
      uint64_t draw_addr = nvk_buffer_address(buffer, offset);
      P_INLINE_DATA(p, draw_addr >> 32);
      P_INLINE_DATA(p, draw_addr);
      P_INLINE_DATA(p, drawCount);
      P_INLINE_DATA(p, stride);
   } else {
      const uint32_t max_draws_per_push =
         ((NV_PUSH_MAX_COUNT - 3) * 4) / stride;

      uint64_t draw_addr = nvk_buffer_address(buffer, offset);
      while (drawCount) {
         const uint32_t count = MIN2(drawCount, max_draws_per_push);

         struct nv_push *p = nvk_cmd_buffer_push(cmd, 3);
         P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_DRAW_INDIRECT));
         P_INLINE_DATA(p, count);
         P_INLINE_DATA(p, (stride - sizeof(VkDrawIndirectCommand)) / 4);

         uint64_t range = count * (uint64_t)stride;
         nv_push_update_count(p, range / 4);
         nvk_cmd_buffer_push_indirect(cmd, draw_addr, range);

         draw_addr += range;
         drawCount -= count;
      }
   }
}

void
nvk_mme_draw_indexed_indirect(struct mme_builder *b)
{
   if (b->devinfo->cls_eng3d >= TURING_A) {
      struct mme_value64 draw_addr = mme_load_addr64(b);
      struct mme_value draw_count = mme_load(b);
      struct mme_value stride = mme_load(b);

      struct mme_value draw = mme_mov(b, mme_zero());
      mme_while(b, ult, draw, draw_count) {
         mme_tu104_read_fifoed(b, draw_addr, mme_imm(5));

         nvk_mme_build_draw_indexed(b, draw);

         mme_add_to(b, draw, draw, mme_imm(1));
         mme_add64_to(b, draw_addr, draw_addr, mme_value64(stride, mme_zero()));
      }
   } else {
      struct mme_value draw_count = mme_load(b);
      nvk_mme_load_to_scratch(b, DRAW_PAD_DW);

      struct mme_value draw = mme_mov(b, mme_zero());
      mme_while(b, ine, draw, draw_count) {
         nvk_mme_spill(b, DRAW_COUNT, draw_count);

         nvk_mme_build_draw_indexed(b, draw);
         mme_add_to(b, draw, draw, mme_imm(1));

         struct mme_value pad_dw = nvk_mme_load_scratch(b, DRAW_PAD_DW);
         mme_loop(b, pad_dw) {
            mme_free_reg(b, mme_load(b));
         }
         mme_free_reg(b, pad_dw);

         nvk_mme_unspill(b, DRAW_COUNT, draw_count);
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer,
                           VkBuffer _buffer,
                           VkDeviceSize offset,
                           uint32_t drawCount,
                           uint32_t stride)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvk_buffer, buffer, _buffer);

   /* From the Vulkan 1.3.238 spec:
    *
    *    VUID-vkCmdDrawIndexedIndirect-drawCount-00528
    *
    *    "If drawCount is greater than 1, stride must be a multiple of 4 and
    *    must be greater than or equal to sizeof(VkDrawIndexedIndirectCommand)"
    *
    * and
    *
    *    "If drawCount is less than or equal to one, stride is ignored."
    */
   if (drawCount > 1) {
      assert(stride % 4 == 0);
      assert(stride >= sizeof(VkDrawIndexedIndirectCommand));
   } else {
      stride = sizeof(VkDrawIndexedIndirectCommand);
   }

   nvk_cmd_flush_gfx_state(cmd);

   if (nvk_cmd_buffer_3d_cls(cmd) >= TURING_A) {
      struct nv_push *p = nvk_cmd_buffer_push(cmd, 5);
      P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_DRAW_INDEXED_INDIRECT));
      uint64_t draw_addr = nvk_buffer_address(buffer, offset);
      P_INLINE_DATA(p, draw_addr >> 32);
      P_INLINE_DATA(p, draw_addr);
      P_INLINE_DATA(p, drawCount);
      P_INLINE_DATA(p, stride);
   } else {
      const uint32_t max_draws_per_push =
         ((NV_PUSH_MAX_COUNT - 3) * 4) / stride;

      uint64_t draw_addr = nvk_buffer_address(buffer, offset);
      while (drawCount) {
         const uint32_t count = MIN2(drawCount, max_draws_per_push);

         struct nv_push *p = nvk_cmd_buffer_push(cmd, 3);
         P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_DRAW_INDEXED_INDIRECT));
         P_INLINE_DATA(p, count);
         P_INLINE_DATA(p, (stride - sizeof(VkDrawIndexedIndirectCommand)) / 4);

         uint64_t range = count * (uint64_t)stride;
         nv_push_update_count(p, range / 4);
         nvk_cmd_buffer_push_indirect(cmd, draw_addr, range);

         draw_addr += range;
         drawCount -= count;
      }
   }
}

void
nvk_mme_draw_indirect_count(struct mme_builder *b)
{
   if (b->devinfo->cls_eng3d < TURING_A)
      return;

   struct mme_value64 draw_addr = mme_load_addr64(b);
   struct mme_value64 draw_count_addr = mme_load_addr64(b);
   struct mme_value draw_max = mme_load(b);
   struct mme_value stride = mme_load(b);

   mme_tu104_read_fifoed(b, draw_count_addr, mme_imm(1));
   mme_free_reg64(b, draw_count_addr);
   struct mme_value draw_count_buf = mme_load(b);

   mme_if(b, ule, draw_count_buf, draw_max) {
      mme_mov_to(b, draw_max, draw_count_buf);
   }
   mme_free_reg(b, draw_count_buf);

   struct mme_value draw = mme_mov(b, mme_zero());
   mme_while(b, ult, draw, draw_max) {
      mme_tu104_read_fifoed(b, draw_addr, mme_imm(4));

      nvk_mme_build_draw(b, draw);

      mme_add_to(b, draw, draw, mme_imm(1));
      mme_add64_to(b, draw_addr, draw_addr, mme_value64(stride, mme_zero()));
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdDrawIndirectCount(VkCommandBuffer commandBuffer,
                         VkBuffer _buffer,
                         VkDeviceSize offset,
                         VkBuffer countBuffer,
                         VkDeviceSize countBufferOffset,
                         uint32_t maxDrawCount,
                         uint32_t stride)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvk_buffer, buffer, _buffer);
   VK_FROM_HANDLE(nvk_buffer, count_buffer, countBuffer);

   /* TODO: Indirect count draw pre-Turing */
   assert(nvk_cmd_buffer_3d_cls(cmd) >= TURING_A);

   nvk_cmd_flush_gfx_state(cmd);

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 7);
   P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_DRAW_INDIRECT_COUNT));
   uint64_t draw_addr = nvk_buffer_address(buffer, offset);
   P_INLINE_DATA(p, draw_addr >> 32);
   P_INLINE_DATA(p, draw_addr);
   uint64_t draw_count_addr = nvk_buffer_address(count_buffer,
                                                 countBufferOffset);
   P_INLINE_DATA(p, draw_count_addr >> 32);
   P_INLINE_DATA(p, draw_count_addr);
   P_INLINE_DATA(p, maxDrawCount);
   P_INLINE_DATA(p, stride);
}

void
nvk_mme_draw_indexed_indirect_count(struct mme_builder *b)
{
   if (b->devinfo->cls_eng3d < TURING_A)
      return;

   struct mme_value64 draw_addr = mme_load_addr64(b);
   struct mme_value64 draw_count_addr = mme_load_addr64(b);
   struct mme_value draw_max = mme_load(b);
   struct mme_value stride = mme_load(b);

   mme_tu104_read_fifoed(b, draw_count_addr, mme_imm(1));
   mme_free_reg64(b, draw_count_addr);
   struct mme_value draw_count_buf = mme_load(b);

   mme_if(b, ule, draw_count_buf, draw_max) {
      mme_mov_to(b, draw_max, draw_count_buf);
   }
   mme_free_reg(b, draw_count_buf);

   struct mme_value draw = mme_mov(b, mme_zero());
   mme_while(b, ult, draw, draw_max) {
      mme_tu104_read_fifoed(b, draw_addr, mme_imm(5));

      nvk_mme_build_draw_indexed(b, draw);

      mme_add_to(b, draw, draw, mme_imm(1));
      mme_add64_to(b, draw_addr, draw_addr, mme_value64(stride, mme_zero()));
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer,
                                VkBuffer _buffer,
                                VkDeviceSize offset,
                                VkBuffer countBuffer,
                                VkDeviceSize countBufferOffset,
                                uint32_t maxDrawCount,
                                uint32_t stride)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvk_buffer, buffer, _buffer);
   VK_FROM_HANDLE(nvk_buffer, count_buffer, countBuffer);

   /* TODO: Indexed indirect count draw pre-Turing */
   assert(nvk_cmd_buffer_3d_cls(cmd) >= TURING_A);

   nvk_cmd_flush_gfx_state(cmd);

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 7);
   P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_DRAW_INDEXED_INDIRECT_COUNT));
   uint64_t draw_addr = nvk_buffer_address(buffer, offset);
   P_INLINE_DATA(p, draw_addr >> 32);
   P_INLINE_DATA(p, draw_addr);
   uint64_t draw_count_addr = nvk_buffer_address(count_buffer,
                                                 countBufferOffset);
   P_INLINE_DATA(p, draw_count_addr >> 32);
   P_INLINE_DATA(p, draw_count_addr);
   P_INLINE_DATA(p, maxDrawCount);
   P_INLINE_DATA(p, stride);
}

static void
nvk_mme_xfb_draw_indirect_loop(struct mme_builder *b,
                               struct mme_value instance_count,
                               struct mme_value counter)
{
   struct mme_value begin = nvk_mme_load_scratch(b, DRAW_BEGIN);

   mme_loop(b, instance_count) {
      mme_mthd(b, NV9097_BEGIN);
      mme_emit(b, begin);

      mme_mthd(b, NV9097_DRAW_AUTO);
      mme_emit(b, counter);

      mme_mthd(b, NV9097_END);
      mme_emit(b, mme_zero());

      mme_set_field_enum(b, begin, NV9097_BEGIN_INSTANCE_ID, SUBSEQUENT);
   }

   mme_free_reg(b, begin);
}

void
nvk_mme_xfb_draw_indirect(struct mme_builder *b)
{
   struct mme_value instance_count = mme_load(b);
   struct mme_value first_instance = mme_load(b);

   if (b->devinfo->cls_eng3d >= TURING_A) {
      struct mme_value64 counter_addr = mme_load_addr64(b);
      mme_tu104_read_fifoed(b, counter_addr, mme_imm(1));
      mme_free_reg(b, counter_addr.lo);
      mme_free_reg(b, counter_addr.hi);
   }
   struct mme_value counter = mme_load(b);

   struct mme_draw_params params = {
      .first_instance = first_instance,
   };
   nvk_mme_build_set_draw_params(b, &params);

   mme_free_reg(b, first_instance);

   struct mme_value view_mask = nvk_mme_load_scratch(b, VIEW_MASK);
   mme_if(b, ieq, view_mask, mme_zero()) {
      mme_free_reg(b, view_mask);

      nvk_mme_xfb_draw_indirect_loop(b, instance_count, counter);
   }

   view_mask = nvk_mme_load_scratch(b, VIEW_MASK);
   mme_if(b, ine, view_mask, mme_zero()) {
      mme_free_reg(b, view_mask);

      struct mme_value view = mme_mov(b, mme_zero());
      mme_while(b, ine, view, mme_imm(32)) {
         view_mask = nvk_mme_load_scratch(b, VIEW_MASK);
         struct mme_value has_view = mme_bfe(b, view_mask, view, 1);
         mme_free_reg(b, view_mask);
         mme_if(b, ine, has_view, mme_zero()) {
            mme_free_reg(b, has_view);
            nvk_mme_emit_view_index(b, view);
            nvk_mme_xfb_draw_indirect_loop(b, instance_count, counter);
         }

         mme_add_to(b, view, view, mme_imm(1));
      }
   }

   mme_free_reg(b, instance_count);
   mme_free_reg(b, counter);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdDrawIndirectByteCountEXT(VkCommandBuffer commandBuffer,
                                uint32_t instanceCount,
                                uint32_t firstInstance,
                                VkBuffer counterBuffer,
                                VkDeviceSize counterBufferOffset,
                                uint32_t counterOffset,
                                uint32_t vertexStride)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvk_buffer, counter_buffer, counterBuffer);

   nvk_cmd_flush_gfx_state(cmd);

   uint64_t counter_addr = nvk_buffer_address(counter_buffer,
                                              counterBufferOffset);

   if (nvk_cmd_buffer_3d_cls(cmd) >= TURING_A) {
      struct nv_push *p = nvk_cmd_buffer_push(cmd, 9);
      P_IMMD(p, NV9097, SET_DRAW_AUTO_START, counterOffset);
      P_IMMD(p, NV9097, SET_DRAW_AUTO_STRIDE, vertexStride);

      P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_XFB_DRAW_INDIRECT));
      P_INLINE_DATA(p, instanceCount);
      P_INLINE_DATA(p, firstInstance);
      P_INLINE_DATA(p, counter_addr >> 32);
      P_INLINE_DATA(p, counter_addr);
   } else {
      struct nv_push *p = nvk_cmd_buffer_push(cmd, 8);
      P_IMMD(p, NV9097, SET_DRAW_AUTO_START, counterOffset);
      P_IMMD(p, NV9097, SET_DRAW_AUTO_STRIDE, vertexStride);

      P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_XFB_DRAW_INDIRECT));
      P_INLINE_DATA(p, instanceCount);
      P_INLINE_DATA(p, firstInstance);
      nv_push_update_count(p, 1);
      nvk_cmd_buffer_push_indirect(cmd, counter_addr, 4);
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdBindTransformFeedbackBuffersEXT(VkCommandBuffer commandBuffer,
                                       uint32_t firstBinding,
                                       uint32_t bindingCount,
                                       const VkBuffer *pBuffers,
                                       const VkDeviceSize *pOffsets,
                                       const VkDeviceSize *pSizes)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   for (uint32_t i = 0; i < bindingCount; i++) {
      VK_FROM_HANDLE(nvk_buffer, buffer, pBuffers[i]);
      uint32_t idx = firstBinding + i;
      uint64_t size = pSizes ? pSizes[i] : VK_WHOLE_SIZE;
      struct nvk_addr_range addr_range =
         nvk_buffer_addr_range(buffer, pOffsets[i], size);
      assert(addr_range.range <= UINT32_MAX);

      struct nv_push *p = nvk_cmd_buffer_push(cmd, 5);

      P_MTHD(p, NV9097, SET_STREAM_OUT_BUFFER_ENABLE(idx));
      P_NV9097_SET_STREAM_OUT_BUFFER_ENABLE(p, idx, V_TRUE);
      P_NV9097_SET_STREAM_OUT_BUFFER_ADDRESS_A(p, idx, addr_range.addr >> 32);
      P_NV9097_SET_STREAM_OUT_BUFFER_ADDRESS_B(p, idx, addr_range.addr);
      P_NV9097_SET_STREAM_OUT_BUFFER_SIZE(p, idx, (uint32_t)addr_range.range);
   }

   // TODO: do we need to SET_STREAM_OUT_BUFFER_ENABLE V_FALSE ?
}

void
nvk_mme_xfb_counter_load(struct mme_builder *b)
{
   struct mme_value buffer = mme_load(b);

   struct mme_value counter;
   if (b->devinfo->cls_eng3d >= TURING_A) {
      struct mme_value64 counter_addr = mme_load_addr64(b);

      mme_tu104_read_fifoed(b, counter_addr, mme_imm(1));
      mme_free_reg(b, counter_addr.lo);
      mme_free_reg(b, counter_addr.hi);

      counter = mme_load(b);
   } else {
      counter = mme_load(b);
   }

   mme_mthd_arr(b, NV9097_SET_STREAM_OUT_BUFFER_LOAD_WRITE_POINTER(0), buffer);
   mme_emit(b, counter);

   mme_free_reg(b, counter);
   mme_free_reg(b, buffer);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdBeginTransformFeedbackEXT(VkCommandBuffer commandBuffer,
                                 uint32_t firstCounterBuffer,
                                 uint32_t counterBufferCount,
                                 const VkBuffer *pCounterBuffers,
                                 const VkDeviceSize *pCounterBufferOffsets)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   const uint32_t max_buffers = 4;

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 2 + 2 * max_buffers);

   P_IMMD(p, NV9097, SET_STREAM_OUTPUT, ENABLE_TRUE);
   for (uint32_t i = 0; i < max_buffers; ++i) {
      P_IMMD(p, NV9097, SET_STREAM_OUT_BUFFER_LOAD_WRITE_POINTER(i), 0);
   }

   for (uint32_t i = 0; i < counterBufferCount; ++i) {
      if (pCounterBuffers[i] == VK_NULL_HANDLE)
         continue;

      VK_FROM_HANDLE(nvk_buffer, buffer, pCounterBuffers[i]);
      // index of counter buffer corresponts to index of transform buffer
      uint32_t cb_idx = firstCounterBuffer + i;
      uint64_t offset = pCounterBufferOffsets ? pCounterBufferOffsets[i] : 0;
      uint64_t cb_addr = nvk_buffer_address(buffer, offset);

      if (nvk_cmd_buffer_3d_cls(cmd) >= TURING_A) {
         struct nv_push *p = nvk_cmd_buffer_push(cmd, 4);
         P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_XFB_COUNTER_LOAD));
         /* The STREAM_OUT_BUFFER_LOAD_WRITE_POINTER registers are 8 dword stride */
         P_INLINE_DATA(p, cb_idx * 8);
         P_INLINE_DATA(p, cb_addr >> 32);
         P_INLINE_DATA(p, cb_addr);
      } else {
         struct nv_push *p = nvk_cmd_buffer_push(cmd, 2);
         P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_XFB_COUNTER_LOAD));
         P_INLINE_DATA(p, cb_idx);
         nv_push_update_count(p, 1);
         nvk_cmd_buffer_push_indirect(cmd, cb_addr, 4);
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdEndTransformFeedbackEXT(VkCommandBuffer commandBuffer,
                               uint32_t firstCounterBuffer,
                               uint32_t counterBufferCount,
                               const VkBuffer *pCounterBuffers,
                               const VkDeviceSize *pCounterBufferOffsets)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 5 * counterBufferCount + 2);

   P_IMMD(p, NV9097, SET_STREAM_OUTPUT, ENABLE_FALSE);

   for (uint32_t i = 0; i < counterBufferCount; ++i) {
      if (pCounterBuffers[i] == VK_NULL_HANDLE)
         continue;

      VK_FROM_HANDLE(nvk_buffer, buffer, pCounterBuffers[i]);
      // index of counter buffer corresponts to index of transform buffer
      uint32_t cb_idx = firstCounterBuffer + i;
      uint64_t offset = pCounterBufferOffsets ? pCounterBufferOffsets[i] : 0;
      uint64_t cb_addr = nvk_buffer_address(buffer, offset);

      P_MTHD(p, NV9097, SET_REPORT_SEMAPHORE_A);
      P_NV9097_SET_REPORT_SEMAPHORE_A(p, cb_addr >> 32);
      P_NV9097_SET_REPORT_SEMAPHORE_B(p, cb_addr);
      P_NV9097_SET_REPORT_SEMAPHORE_C(p, 0);
      P_NV9097_SET_REPORT_SEMAPHORE_D(p, {
         .operation = OPERATION_REPORT_ONLY,
         .pipeline_location = PIPELINE_LOCATION_STREAMING_OUTPUT,
         .report = REPORT_STREAMING_BYTE_COUNT,
         .sub_report = cb_idx,
         .structure_size = STRUCTURE_SIZE_ONE_WORD,
      });
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdBeginConditionalRenderingEXT(VkCommandBuffer commandBuffer,
                                    const VkConditionalRenderingBeginInfoEXT *pConditionalRenderingBegin)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvk_buffer, buffer, pConditionalRenderingBegin->buffer);

   uint64_t addr = nvk_buffer_address(buffer, pConditionalRenderingBegin->offset);
   bool inverted = pConditionalRenderingBegin->flags &
      VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT;

   /* From the Vulkan 1.3.280 spec:
    *
    *    "If the 32-bit value at offset in buffer memory is zero,
    *     then the rendering commands are discarded,
    *     otherwise they are executed as normal."
    *
    * The hardware compare a 64-bit value, as such we are required to copy it.
    */
   uint64_t tmp_addr;
   VkResult result = nvk_cmd_buffer_cond_render_alloc(cmd, &tmp_addr);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 26);

   P_MTHD(p, NV90B5, OFFSET_IN_UPPER);
   P_NV90B5_OFFSET_IN_UPPER(p, addr >> 32);
   P_NV90B5_OFFSET_IN_LOWER(p, addr & 0xffffffff);
   P_NV90B5_OFFSET_OUT_UPPER(p, tmp_addr >> 32);
   P_NV90B5_OFFSET_OUT_LOWER(p, tmp_addr & 0xffffffff);
   P_NV90B5_PITCH_IN(p, 4);
   P_NV90B5_PITCH_OUT(p, 4);
   P_NV90B5_LINE_LENGTH_IN(p, 4);
   P_NV90B5_LINE_COUNT(p, 1);

   P_IMMD(p, NV90B5, SET_REMAP_COMPONENTS, {
      .dst_x = DST_X_SRC_X,
      .dst_y = DST_Y_SRC_X,
      .dst_z = DST_Z_NO_WRITE,
      .dst_w = DST_W_NO_WRITE,
      .component_size = COMPONENT_SIZE_ONE,
      .num_src_components = NUM_SRC_COMPONENTS_ONE,
      .num_dst_components = NUM_DST_COMPONENTS_TWO,
   });

   P_IMMD(p, NV90B5, LAUNCH_DMA, {
      .data_transfer_type = DATA_TRANSFER_TYPE_PIPELINED,
      .multi_line_enable = MULTI_LINE_ENABLE_TRUE,
      .flush_enable = FLUSH_ENABLE_TRUE,
      .src_memory_layout = SRC_MEMORY_LAYOUT_PITCH,
      .dst_memory_layout = DST_MEMORY_LAYOUT_PITCH,
      .remap_enable = REMAP_ENABLE_TRUE,
   });

   P_MTHD(p, NV9097, SET_RENDER_ENABLE_A);
   P_NV9097_SET_RENDER_ENABLE_A(p, tmp_addr >> 32);
   P_NV9097_SET_RENDER_ENABLE_B(p, tmp_addr & 0xfffffff0);
   P_NV9097_SET_RENDER_ENABLE_C(p, inverted ? MODE_RENDER_IF_EQUAL : MODE_RENDER_IF_NOT_EQUAL);

   P_MTHD(p, NV90C0, SET_RENDER_ENABLE_A);
   P_NV90C0_SET_RENDER_ENABLE_A(p, tmp_addr >> 32);
   P_NV90C0_SET_RENDER_ENABLE_B(p, tmp_addr & 0xfffffff0);
   P_NV90C0_SET_RENDER_ENABLE_C(p, inverted ? MODE_RENDER_IF_EQUAL : MODE_RENDER_IF_NOT_EQUAL);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdEndConditionalRenderingEXT(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 12);
   P_MTHD(p, NV9097, SET_RENDER_ENABLE_A);
   P_NV9097_SET_RENDER_ENABLE_A(p, 0);
   P_NV9097_SET_RENDER_ENABLE_B(p, 0);
   P_NV9097_SET_RENDER_ENABLE_C(p, MODE_TRUE);

   P_MTHD(p, NV90C0, SET_RENDER_ENABLE_A);
   P_NV90C0_SET_RENDER_ENABLE_A(p, 0);
   P_NV90C0_SET_RENDER_ENABLE_B(p, 0);
   P_NV90C0_SET_RENDER_ENABLE_C(p, MODE_TRUE);
}
