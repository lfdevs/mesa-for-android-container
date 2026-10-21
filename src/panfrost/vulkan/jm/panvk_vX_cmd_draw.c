#include <stdlib.h>
#include <stdio.h>
/*
 * Copyright © 2024 Collabora Ltd.
 * Copyright © 2026 NXP
 *
 * Derived from tu_cmd_buffer.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "../../lib/pan_trace_gate.h"
#include "genxml/gen_macros.h"

#include "panvk_buffer.h"
#include "panvk_cmd_alloc.h"
#include "panvk_cmd_buffer.h"
#include "panvk_cmd_desc_state.h"
#include "panvk_cmd_draw.h"
#include "panvk_cmd_meta.h"
#include "panvk_cmd_precomp.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_image.h"
#include "panvk_image_view.h"
#include "panvk_instance.h"
#include "panvk_meta.h"
#include "panvk_priv_bo.h"
#include "panvk_shader.h"

#include "draw_helper.h"
#include "libpan/draw_helper_vp.h"
#include "pan_desc.h"
#include "pan_earlyzs.h"
#include "pan_encoder.h"
#include "pan_format.h"
#include "pan_jc.h"
#include "pan_props.h"
#include "pan_shader.h"

#include "vk_format.h"
#include "vk_meta.h"
#include "vk_pipeline_layout.h"

/* PANVK_NO_SMC=1 disables shader_modifies_coverage (debug A/B) */
#include <stdio.h>
#include <stdlib.h>
static __attribute__((unused)) bool
v61a_no_smc(void)
{
   static int v = -1;
   if (v < 0) {
      const char *e = getenv("PANVK_NO_SMC");
      v = (e && atoi(e)) ? 1 : 0;
      (void)0;
   }
   return v;
}

#if PAN_ARCH != 9
/* TODO(v9): draw-call path (RSD, attribute buffers, indirect varying
 * bufs) not yet implemented for arch 9 -- needs a from-scratch MVS/SPD
 * rewrite, see pan_jm.c PAN_ARCH==9 sections for reference pattern. */


struct panvk_draw_data {
   struct panvk_draw_info info;
   unsigned vertex_range;
   unsigned padded_vertex_count;
   struct mali_invocation_packed invocation;
   struct {
      uint64_t varyings;
      uint64_t attributes;
      uint64_t attribute_bufs;
   } vs;
   struct {
      uint64_t rsd;
      uint64_t varyings;
   } fs;
   uint64_t varying_bufs;
   /* With a geometry shader the tiler reads a different set of varying
    * buffers, the ones the emulation job writes. Zero without one.
    */
   uint64_t gs_varying_bufs;
   /* Indexed draws: where the index min/max helper leaves the smallest
    * index, which the vertex job starts at.
    */
   uint64_t index_min_addr;
   /* vkCmdDrawIndexed goes through the indirect path, but its counts are
    * known here: they size the geometry shader emulation exactly.
    */
   uint32_t cpu_count, cpu_instances;
   /* A job the indirect draw helper must wait for: the one that writes the
    * command (vkCmdDrawIndirectByteCountEXT), 0 for none.
    */
   unsigned indirect_dep_job;
   /* Tessellation (jm-v7 014): which part of a tessellated draw this is
    * (enum panvk_tess_phase), and for the tess eval draw the input primitive
    * cap of the geometry emulation (0: PANVK_GS_INDIRECT_MAX_PRIMS).
    */
   uint32_t tess_phase;
   uint32_t gs_max_prims;
   /* Per-viewport tiler jobs (jm-v7 031, panvk_gs_vp_split()): how many
    * (0: one tiler job), the ones with a non-empty scissor box, and the
    * emulation's per-vertex viewport indices.
    */
   uint32_t vp_split;
   uint32_t vp_split_mask;
   uint64_t gs_out_viewport;
   uint64_t position;
   union {
      uint64_t psiz;
      float line_width;
   };
   uint64_t tls;
   uint64_t fb;
   const struct pan_tiler_context *tiler_ctx;
   uint64_t viewport;
   struct {
      struct pan_ptr vertex_copy_desc;
      struct pan_ptr frag_copy_desc;
      union {
         struct {
            struct pan_ptr vertex;
            struct pan_ptr tiler;
         };
         struct pan_ptr idvs;
      };
   } jobs;
   struct {
      uint64_t attribs;
      uint64_t attrib_bufs;
   } indirect_info;
};

/* A tessellated draw is recorded as two draws (panvk_tess_draw()): the
 * vertex shader over the API draw followed by the tess control job instead
 * of a tiler job, then the tess eval shader as the vertex shader of an
 * indirect draw over the tessellator indices.
 */
enum panvk_tess_phase {
   PANVK_TESS_PHASE_NONE = 0,
   PANVK_TESS_PHASE_VS_TCS,
   PANVK_TESS_PHASE_TES,
};

static bool
is_indirect_draw(const struct panvk_draw_data *draw)
{
   return draw->info.indirect.buffer_dev_addr != 0 ||
          draw->info.index.index_size != 0;
}

/* Geometry shader input assembly: how the draw's topology groups vertices
 * into the shader's input primitives, and how many one instance makes.
 */
static enum panvk_gs_topology
gs_topology(enum mesa_prim prim)
{
   switch (prim) {
   case MESA_PRIM_LINE_STRIP:
   case MESA_PRIM_TRIANGLE_STRIP:
   case MESA_PRIM_LINE_STRIP_ADJACENCY:
      return PANVK_GS_TOPO_STRIP;
   case MESA_PRIM_TRIANGLE_FAN:
      return PANVK_GS_TOPO_FAN;
   case MESA_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return PANVK_GS_TOPO_TRI_STRIP_ADJ;
   default:
      return PANVK_GS_TOPO_LIST;
   }
}

static uint32_t
gs_prims_per_instance(enum mesa_prim prim, uint32_t n,
                      const struct panvk_shader_variant *gs)
{
   const uint32_t vpp = MAX2(gs->gs.input_verts_per_prim, 1);

   switch (gs_topology(prim)) {
   case PANVK_GS_TOPO_STRIP:
      return n >= vpp ? n - (vpp - 1) : 0;
   case PANVK_GS_TOPO_FAN:
      return n >= 3 ? n - 2 : 0;
   case PANVK_GS_TOPO_TRI_STRIP_ADJ:
      return n >= 6 ? (n - 4) / 2 : 0;
   default:
      return n / vpp;
   }
}

/* Input primitives the emulation is dispatched for: exact when the counts
 * are known on the CPU, the cap for a real indirect draw.
 */
static uint32_t
gs_input_prims(const struct panvk_draw_data *draw,
               const struct panvk_shader_variant *gs)
{
   if (draw->info.indirect.buffer_dev_addr && !draw->cpu_count) {
      if (draw->gs_max_prims)
         return draw->gs_max_prims;
      /* jm-v7 023: a budget of output vertices (growable memory), not a
       * fixed 4096 primitives: transform feedback of large indirect draws
       * through the passthrough GS was cut at 4096 primitives.
       */
      const uint32_t per_prim = MAX2(gs->gs.max_output_verts, 1) *
                                MAX2(gs->gs.invocations, 1);
      return CLAMP(PANVK_GS_INDIRECT_MAX_VERTS / per_prim,
                   PANVK_GS_INDIRECT_MAX_PRIMS,
                   PANVK_GS_INDIRECT_MAX_PRIMS_BIG);
   }

   const uint32_t n =
      draw->cpu_count ? draw->cpu_count : draw->info.vertex.count;
   const uint32_t inst =
      draw->cpu_count ? draw->cpu_instances : draw->info.instance.count;
   return gs_prims_per_instance(draw->info.prim, n, gs) * inst;
}

static bool
has_depth_att(struct panvk_cmd_buffer *cmdbuf)
{
   return (cmdbuf->state.gfx.render.bound_attachments &
           MESA_VK_RP_ATTACHMENT_DEPTH_BIT) != 0;
}

static bool
has_stencil_att(struct panvk_cmd_buffer *cmdbuf)
{
   return (cmdbuf->state.gfx.render.bound_attachments &
           MESA_VK_RP_ATTACHMENT_STENCIL_BIT) != 0;
}

static bool
writes_depth(struct panvk_cmd_buffer *cmdbuf)
{
   const struct vk_depth_stencil_state *ds =
      &cmdbuf->vk.dynamic_graphics_state.ds;

   return has_depth_att(cmdbuf) && ds->depth.test_enable &&
          ds->depth.write_enable && ds->depth.compare_op != VK_COMPARE_OP_NEVER;
}

static bool
writes_stencil(struct panvk_cmd_buffer *cmdbuf)
{
   const struct vk_depth_stencil_state *ds =
      &cmdbuf->vk.dynamic_graphics_state.ds;

   return has_stencil_att(cmdbuf) && ds->stencil.test_enable &&
          ((ds->stencil.front.write_mask &&
            (ds->stencil.front.op.fail != VK_STENCIL_OP_KEEP ||
             ds->stencil.front.op.pass != VK_STENCIL_OP_KEEP ||
             ds->stencil.front.op.depth_fail != VK_STENCIL_OP_KEEP)) ||
           (ds->stencil.back.write_mask &&
            (ds->stencil.back.op.fail != VK_STENCIL_OP_KEEP ||
             ds->stencil.back.op.pass != VK_STENCIL_OP_KEEP ||
             ds->stencil.back.op.depth_fail != VK_STENCIL_OP_KEEP)));
}

static bool
ds_test_always_passes(struct panvk_cmd_buffer *cmdbuf)
{
   const struct vk_depth_stencil_state *ds =
      &cmdbuf->vk.dynamic_graphics_state.ds;

   if (!has_depth_att(cmdbuf))
      return true;

   if (ds->depth.test_enable && ds->depth.compare_op != VK_COMPARE_OP_ALWAYS)
      return false;

   if (ds->stencil.test_enable &&
       (ds->stencil.front.op.compare != VK_COMPARE_OP_ALWAYS ||
        ds->stencil.back.op.compare != VK_COMPARE_OP_ALWAYS))
      return false;

   return true;
}

static inline enum mali_func
translate_compare_func(VkCompareOp comp)
{
   STATIC_ASSERT(VK_COMPARE_OP_NEVER == (VkCompareOp)MALI_FUNC_NEVER);
   STATIC_ASSERT(VK_COMPARE_OP_LESS == (VkCompareOp)MALI_FUNC_LESS);
   STATIC_ASSERT(VK_COMPARE_OP_EQUAL == (VkCompareOp)MALI_FUNC_EQUAL);
   STATIC_ASSERT(VK_COMPARE_OP_LESS_OR_EQUAL == (VkCompareOp)MALI_FUNC_LEQUAL);
   STATIC_ASSERT(VK_COMPARE_OP_GREATER == (VkCompareOp)MALI_FUNC_GREATER);
   STATIC_ASSERT(VK_COMPARE_OP_NOT_EQUAL == (VkCompareOp)MALI_FUNC_NOT_EQUAL);
   STATIC_ASSERT(VK_COMPARE_OP_GREATER_OR_EQUAL ==
                 (VkCompareOp)MALI_FUNC_GEQUAL);
   STATIC_ASSERT(VK_COMPARE_OP_ALWAYS == (VkCompareOp)MALI_FUNC_ALWAYS);

   return (enum mali_func)comp;
}

static enum mali_stencil_op
translate_stencil_op(VkStencilOp in)
{
   switch (in) {
   case VK_STENCIL_OP_KEEP:
      return MALI_STENCIL_OP_KEEP;
   case VK_STENCIL_OP_ZERO:
      return MALI_STENCIL_OP_ZERO;
   case VK_STENCIL_OP_REPLACE:
      return MALI_STENCIL_OP_REPLACE;
   case VK_STENCIL_OP_INCREMENT_AND_CLAMP:
      return MALI_STENCIL_OP_INCR_SAT;
   case VK_STENCIL_OP_DECREMENT_AND_CLAMP:
      return MALI_STENCIL_OP_DECR_SAT;
   case VK_STENCIL_OP_INCREMENT_AND_WRAP:
      return MALI_STENCIL_OP_INCR_WRAP;
   case VK_STENCIL_OP_DECREMENT_AND_WRAP:
      return MALI_STENCIL_OP_DECR_WRAP;
   case VK_STENCIL_OP_INVERT:
      return MALI_STENCIL_OP_INVERT;
   default:
      UNREACHABLE("Invalid stencil op");
   }
}

static VkResult
panvk_draw_prepare_fs_rsd(struct panvk_cmd_buffer *cmdbuf,
                          struct panvk_draw_data *draw)
{
   bool dirty = dyn_gfx_state_dirty(cmdbuf, RS_RASTERIZER_DISCARD_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLAMP_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLIP_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_BIAS_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_BIAS_FACTORS) ||
                dyn_gfx_state_dirty(cmdbuf, RS_LINE_MODE) ||
                /* line mode needs primitive topology */
                dyn_gfx_state_dirty(cmdbuf, IA_PRIMITIVE_TOPOLOGY) ||
                dyn_gfx_state_dirty(cmdbuf, CB_LOGIC_OP_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, CB_LOGIC_OP) ||
                dyn_gfx_state_dirty(cmdbuf, CB_ATTACHMENT_COUNT) ||
                dyn_gfx_state_dirty(cmdbuf, CB_COLOR_WRITE_ENABLES) ||
                dyn_gfx_state_dirty(cmdbuf, CB_BLEND_ENABLES) ||
                dyn_gfx_state_dirty(cmdbuf, CB_BLEND_EQUATIONS) ||
                dyn_gfx_state_dirty(cmdbuf, CB_WRITE_MASKS) ||
                dyn_gfx_state_dirty(cmdbuf, CB_BLEND_CONSTANTS) ||
                dyn_gfx_state_dirty(cmdbuf, COLOR_ATTACHMENT_MAP) ||
                dyn_gfx_state_dirty(cmdbuf, DS_DEPTH_TEST_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, DS_DEPTH_WRITE_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, DS_DEPTH_COMPARE_OP) ||
                dyn_gfx_state_dirty(cmdbuf, DS_DEPTH_COMPARE_OP) ||
                dyn_gfx_state_dirty(cmdbuf, DS_STENCIL_TEST_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, DS_STENCIL_OP) ||
                dyn_gfx_state_dirty(cmdbuf, DS_STENCIL_COMPARE_MASK) ||
                dyn_gfx_state_dirty(cmdbuf, DS_STENCIL_WRITE_MASK) ||
                dyn_gfx_state_dirty(cmdbuf, DS_STENCIL_REFERENCE) ||
                dyn_gfx_state_dirty(cmdbuf, MS_RASTERIZATION_SAMPLES) ||
                dyn_gfx_state_dirty(cmdbuf, MS_SAMPLE_MASK) ||
                dyn_gfx_state_dirty(cmdbuf, MS_ALPHA_TO_COVERAGE_ENABLE) ||
                dyn_gfx_state_dirty(cmdbuf, MS_ALPHA_TO_ONE_ENABLE) ||
                gfx_state_dirty(cmdbuf, FS) || gfx_state_dirty(cmdbuf, OQ) ||
                gfx_state_dirty(cmdbuf, RENDER_STATE);

   if (!dirty) {
      draw->fs.rsd = cmdbuf->state.gfx.fs.rsd;
      return VK_SUCCESS;
   }

   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_rasterization_state *rs = &dyns->rs;
   const struct vk_depth_stencil_state *ds = &dyns->ds;
   const struct vk_input_assembly_state *ia = &dyns->ia;
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));
   const struct pan_shader_info *fs_info = fs ? &fs->info : NULL;
   uint32_t bd_count = cmdbuf->state.gfx.render.fb.layout.rt_count;
   bool test_s = has_stencil_att(cmdbuf) && ds->stencil.test_enable;
   bool test_z = has_depth_att(cmdbuf) && ds->depth.test_enable;
   bool writes_z = writes_depth(cmdbuf);
   bool writes_s = writes_stencil(cmdbuf);

   bool msaa = dyns->ms.rasterization_samples > 1;
   if ((ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST ||
        ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP) &&
       rs->line.mode == VK_LINE_RASTERIZATION_MODE_BRESENHAM) {
      /* we need to disable MSAA when rendering bresenham lines.
       *
       * From the Vulkan spec:
       *   "When Bresenham lines are being rasterized, sample locations may
       *    all be treated as being at the pixel center (this may affect
       *    attribute and depth interpolation).""
       */
      msaa = false;
   }

   struct pan_ptr ptr = panvk_cmd_alloc_desc_aggregate(
      cmdbuf, PAN_DESC(RENDERER_STATE), PAN_DESC_ARRAY(bd_count, BLEND));
   if (!ptr.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   struct mali_renderer_state_packed *rsd = ptr.cpu;
   struct mali_blend_packed *bds = ptr.cpu + pan_size(RENDERER_STATE);
   struct panvk_blend_info *binfo = &cmdbuf->state.gfx.cb.info;

   uint64_t fs_code = panvk_shader_variant_get_dev_addr(fs);

   if (fs_info != NULL) {
      panvk_per_arch(blend_emit_descs)(cmdbuf, bds);
   } else {
      for (unsigned i = 0; i < bd_count; i++) {
         pan_pack(&bds[i], BLEND, cfg) {
            cfg.enable = false;
            cfg.internal.mode = MALI_BLEND_MODE_OFF;
         }
      }
   }

   pan_pack(rsd, RENDERER_STATE, cfg) {
      bool alpha_to_coverage = dyns->ms.alpha_to_coverage_enable;

      if (fs) {
         pan_shader_prepare_rsd(fs_info, fs_code, &cfg);

         uint8_t rt_mask = cmdbuf->state.gfx.render.bound_attachments &
                           MESA_VK_RP_ATTACHMENT_ANY_COLOR_BITS;
         uint8_t rt_written = color_attachment_written_mask(
            fs, &cmdbuf->vk.dynamic_graphics_state.cal);
         uint8_t rt_read = color_attachment_read_mask(fs, &dyns->ial, rt_mask);
         /* Depth bounds emulation (jm-v7 015): the lowered FS reads the
          * tile depth with LD_TILE, as an input attachment read would. Same
          * condition as the ds.enable sysval.
          */
         const bool db_on = fs->fs.depth_bounds &&
                            dyns->ds.depth.bounds_test.enable &&
                            (cmdbuf->state.gfx.render.bound_attachments &
                             MESA_VK_RP_ATTACHMENT_DEPTH_BIT);
         enum pan_earlyzs_zs_tilebuf_read zs_read =
            (z_attachment_read(fs, &dyns->ial) ||
             s_attachment_read(fs, &dyns->ial) || db_on)
               ? PAN_EARLYZS_ZS_TILEBUF_READ_NO_OPT
               : PAN_EARLYZS_ZS_TILEBUF_NOT_READ;

         cfg.properties.allow_forward_pixel_to_kill =
            fs_info->fs.can_fpk && !(rt_mask & ~rt_written) &&
            !(rt_read & rt_written) && !alpha_to_coverage &&
            !binfo->any_dest_read;

         bool writes_zs = writes_z || writes_s;
         bool zs_always_passes = ds_test_always_passes(cmdbuf);
         bool oq = cmdbuf->state.gfx.occlusion_query.mode !=
                   MALI_OCCLUSION_MODE_DISABLED;

         struct pan_earlyzs_state earlyzs =
            pan_earlyzs_get(fs->fs.earlyzs_lut, writes_zs || oq,
                            alpha_to_coverage, zs_always_passes, zs_read);

         /* early ZS check for FPK is performed by HW on v7+ */
         cfg.properties.allow_forward_pixel_to_be_killed =
            !fs->info.writes_global &&
            ((PAN_ARCH > 6) || earlyzs.kill != MALI_PIXEL_KILL_FORCE_LATE);

         cfg.properties.pixel_kill_operation = earlyzs.kill;
         cfg.properties.zs_update_operation = earlyzs.update;
         /* With EarlyFragmentTests, an early ZS update would let the depth
          * bounds FS read its own depth and keep the writes of samples
          * outside the bounds: test early, update after the FS (v9 JM
          * jm-v9/018, csf-v11/116). Not when the app FS discards, writes
          * SampleMask or uses alpha-to-coverage.
          */
         if (db_on && fs->info.fs.early_fragment_tests &&
             (writes_zs || oq) && !fs->info.fs.can_discard &&
             !fs->fs.depth_bounds_app_mask && !alpha_to_coverage)
            cfg.properties.zs_update_operation = MALI_PIXEL_KILL_FORCE_LATE;
         cfg.multisample_misc.evaluate_per_sample =
            (fs->info.fs.sample_shading && dyns->ms.rasterization_samples > 1);
      } else {
         cfg.properties.depth_source = MALI_DEPTH_SOURCE_FIXED_FUNCTION;
         cfg.properties.allow_forward_pixel_to_kill = true;
         cfg.properties.allow_forward_pixel_to_be_killed = true;
         cfg.properties.zs_update_operation = MALI_PIXEL_KILL_FORCE_EARLY;
      }

      cfg.multisample_misc.multisample_enable = msaa;
      cfg.multisample_misc.sample_mask = dyns->ms.sample_mask;

      cfg.multisample_misc.depth_function =
         test_z ? translate_compare_func(ds->depth.compare_op)
                : MALI_FUNC_ALWAYS;

      cfg.multisample_misc.depth_write_mask = writes_z;
      cfg.multisample_misc.fixed_function_near_discard =
      cfg.multisample_misc.fixed_function_far_discard =
         vk_rasterization_state_depth_clip_enable(rs);
      cfg.multisample_misc.fixed_function_depth_range_fixed =
         !rs->depth_clamp_enable;
      cfg.multisample_misc.shader_depth_range_fixed = true;

      cfg.stencil_mask_misc.stencil_enable = test_s;
      cfg.stencil_mask_misc.alpha_to_coverage = alpha_to_coverage;
      cfg.stencil_mask_misc.alpha_test_compare_function = MALI_FUNC_ALWAYS;
      cfg.stencil_mask_misc.front_facing_depth_bias = rs->depth_bias.enable;
      cfg.stencil_mask_misc.back_facing_depth_bias = rs->depth_bias.enable;

      if (rs->line.mode == VK_LINE_RASTERIZATION_MODE_BRESENHAM)
         cfg.stencil_mask_misc.aligned_line_ends = true;

      cfg.depth_units = rs->depth_bias.constant_factor;
      cfg.depth_factor = rs->depth_bias.slope_factor;
      cfg.depth_bias_clamp = rs->depth_bias.clamp;

      cfg.stencil_front.mask = ds->stencil.front.compare_mask;
      cfg.stencil_back.mask = ds->stencil.back.compare_mask;

      cfg.stencil_mask_misc.stencil_mask_front = ds->stencil.front.write_mask;
      cfg.stencil_mask_misc.stencil_mask_back = ds->stencil.back.write_mask;

      cfg.stencil_front.reference_value = ds->stencil.front.reference;
      cfg.stencil_back.reference_value = ds->stencil.back.reference;

      if (test_s) {
         cfg.stencil_front.compare_function =
            translate_compare_func(ds->stencil.front.op.compare);
         cfg.stencil_front.stencil_fail =
            translate_stencil_op(ds->stencil.front.op.fail);
         cfg.stencil_front.depth_fail =
            translate_stencil_op(ds->stencil.front.op.depth_fail);
         cfg.stencil_front.depth_pass =
            translate_stencil_op(ds->stencil.front.op.pass);
         cfg.stencil_back.compare_function =
            translate_compare_func(ds->stencil.back.op.compare);
         cfg.stencil_back.stencil_fail =
            translate_stencil_op(ds->stencil.back.op.fail);
         cfg.stencil_back.depth_fail =
            translate_stencil_op(ds->stencil.back.op.depth_fail);
         cfg.stencil_back.depth_pass =
            translate_stencil_op(ds->stencil.back.op.pass);
      }
   }

   cmdbuf->state.gfx.fs.rsd = ptr.gpu;
   draw->fs.rsd = cmdbuf->state.gfx.fs.rsd;
   return VK_SUCCESS;
}

static VkResult
panvk_draw_prepare_tiler_context(struct panvk_cmd_buffer *cmdbuf,
                                 struct panvk_draw_data *draw)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   VkResult result =
      panvk_per_arch(cmd_prepare_tiler_context)(cmdbuf, draw->info.layer_id);
   if (result != VK_SUCCESS)
      return result;

   draw->tiler_ctx = &batch->tiler.ctx;
   return VK_SUCCESS;
}

/* The allocator indirect draws take their varyings from, see
 * panlib_patch_varying_bufs(). One per batch (or per ~1024 indirect draws).
 */
static VkResult
panvk_draw_get_indirect_varying_info(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);

   if (cmdbuf->state.gfx.vs.indirect_varying_bufs_infos)
      return VK_SUCCESS;

   struct pan_ptr bufs_info_storage = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(struct libpan_draw_helper_varying_buf_info), 8);

   if (!bufs_info_storage.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   cmdbuf->state.gfx.vs.indirect_varying_bufs_infos = bufs_info_storage.gpu;

   struct libpan_draw_helper_varying_buf_info *vary_bufs_info =
      bufs_info_storage.cpu;
   vary_bufs_info->address = dev->indirect_varying_buffer->addr.dev;
   vary_bufs_info->size = PANVK_JM_MAX_PER_VTX_ATTRIBUTES_INDIRECT_SIZE *
                          PANVK_JM_MAX_VERTICES_INDIRECT;
   vary_bufs_info->offset = 0;
   return VK_SUCCESS;
}

static uint32_t panvk_gs_vp_split(const struct panvk_cmd_buffer *cmdbuf);

/* Geometry emulation buffers: GPU-only, from the growable pool for the
 * emulation of a tess eval draw (jm-v7 033), else the descriptor pool.
 */
static struct pan_ptr
gs_alloc(struct panvk_cmd_buffer *cmdbuf, bool growable, uint64_t size,
         unsigned alignment)
{
   if (growable)
      return panvk_cmd_alloc_dev_mem(cmdbuf, gs, size, alignment);
   return panvk_cmd_alloc_dev_mem(cmdbuf, desc, size, alignment);
}

static VkResult
panvk_draw_prepare_varyings(struct panvk_cmd_buffer *cmdbuf,
                            struct panvk_draw_data *draw)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   const struct panvk_shader_link *link = &cmdbuf->state.gfx.link;
   struct pan_ptr bufs = panvk_cmd_alloc_desc_array(
      cmdbuf, PANVK_VARY_BUF_MAX + 1, ATTRIBUTE_BUFFER);
   if (!bufs.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   struct mali_attribute_buffer_packed *buf_descs = bufs.cpu;
   const struct vk_input_assembly_state *ia =
      &cmdbuf->vk.dynamic_graphics_state.ia;
   const struct panvk_shader *gs_shader = cmdbuf->state.gfx.gs.emu;
   bool writes_point_size =
      !gs_shader && vs->info.vs.writes_point_size &&
      (ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST ||
       (draw->tess_phase == PANVK_TESS_PHASE_TES &&
        draw->info.prim == MESA_PRIM_POINTS));
   uint64_t psiz_buf = 0, general_buf = 0;

   if (is_indirect_draw(draw)) {
      VkResult result = panvk_draw_get_indirect_varying_info(cmdbuf);
      if (result != VK_SUCCESS)
         return result;
   }

   for (unsigned i = 0; i < PANVK_VARY_BUF_MAX; i++) {
      uint32_t buf_size;
      uint64_t buf_addr;
      if (is_indirect_draw(draw)) {
         buf_addr = dev->poly_heap.buffer->addr.dev;
         buf_size = 0;
      } else {
         buf_size = draw->padded_vertex_count * draw->info.instance.count *
                    link->buf_strides[i];
         buf_addr =
            buf_size
               ? panvk_cmd_alloc_dev_mem(cmdbuf, varying, buf_size, 64).gpu
               : 0;

         if (buf_size && !buf_addr)
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
      }

      pan_pack(&buf_descs[i], ATTRIBUTE_BUFFER, cfg) {
         cfg.stride = link->buf_strides[i];
         cfg.size = buf_size;
         cfg.pointer = buf_addr;
      }

      if (i == PANVK_VARY_BUF_POSITION)
         draw->position = buf_addr;

      if (i == PANVK_VARY_BUF_PSIZ)
         psiz_buf = buf_addr;

      if (i == PANVK_VARY_BUF_GENERAL)
         general_buf = buf_addr;
   }

   /* We need an empty entry to stop prefetching on Bifrost */
   memset(bufs.cpu + (pan_size(ATTRIBUTE_BUFFER) * PANVK_VARY_BUF_MAX), 0,
          pan_size(ATTRIBUTE_BUFFER));

   if (writes_point_size)
      draw->psiz = psiz_buf;
   else if (ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST ||
            ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP ||
            (draw->tess_phase == PANVK_TESS_PHASE_TES &&
             draw->info.prim == MESA_PRIM_LINES))
      draw->line_width = cmdbuf->vk.dynamic_graphics_state.rs.line.width;
   else
      draw->line_width = 1.0f;

   draw->varying_bufs = bufs.gpu;
   /* The geometry emulation and the tess control job read the vertex
    * shader's generic varyings from here (for an indirect draw, from the
    * patched descriptor instead).
    */
   cmdbuf->state.gfx.gs.in_general = general_buf;
   cmdbuf->state.gfx.gs.in_psiz =
      vs->info.vs.writes_point_size && link->buf_strides[PANVK_VARY_BUF_PSIZ]
         ? psiz_buf
         : 0;
   cmdbuf->state.gfx.gs.out_psiz = 0;

   /* Geometry shader emulation: a second set of varying buffers, written by
    * the emulation job and read by the tiler. Sized for the worst case
    * (every input primitive emits its declared maximum): the real count is
    * only known once the shader ran.
    */
   draw->gs_varying_bufs = 0;
   if (gs_shader) {
      const struct panvk_shader_variant *gs =
         panvk_shader_only_variant(gs_shader);
      const uint32_t in_prims = gs_input_prims(draw, gs) * gs->gs.invocations;
      const uint32_t out_verts = in_prims * gs->gs.max_output_verts;
      const uint32_t out_indices =
         in_prims * gs->gs.max_output_prims * gs->gs.output_verts_per_prim;
      /* jm-v7 033: a tess eval draw's emulation is sized for
       * PANVK_TESS_GS_MAX_PRIMS, from growable memory.
       */
      /* jm-v7 023: so is a real indirect draw's, sized for
       * PANVK_GS_INDIRECT_MAX_VERTS: only the pages its real count touches
       * get committed.
       */
      const bool growable =
         draw->tess_phase == PANVK_TESS_PHASE_TES ||
         (draw->info.indirect.buffer_dev_addr && !draw->cpu_count);
      const bool gs_psiz = gs->gs.output_verts_per_prim == 1 &&
                           (gs->gs.outputs_written & VARYING_BIT_PSIZ);

      struct pan_ptr gs_bufs = panvk_cmd_alloc_desc_array(
         cmdbuf, PANVK_VARY_BUF_MAX + 1, ATTRIBUTE_BUFFER);
      if (!gs_bufs.gpu)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;

      struct mali_attribute_buffer_packed *gs_descs = gs_bufs.cpu;
      uint64_t gs_addrs[PANVK_VARY_BUF_MAX] = {0};
      for (unsigned i = 0; i < PANVK_VARY_BUF_MAX; i++) {
         /* Point size (jm-v7 033): fp32, only for a shader emitting
          * points with gl_PointSize.
          */
         const uint32_t stride =
            i == PANVK_VARY_BUF_GENERAL ? link->gs_out_stride
            : i == PANVK_VARY_BUF_PSIZ  ? (gs_psiz ? 4 : 0)
                                        : link->buf_strides[i];
         const uint32_t sz = out_verts * stride;
         /* Not the varying pool: the emulation writes with plain compute
          * stores, the tiler only needs a pointer and a stride.
          */
         gs_addrs[i] =
            sz ? gs_alloc(cmdbuf, growable, sz, 64).gpu : 0;
         if (sz && !gs_addrs[i])
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;

         pan_pack(&gs_descs[i], ATTRIBUTE_BUFFER, cfg) {
            cfg.stride = stride;
            cfg.size = sz;
            cfg.pointer = gs_addrs[i];
         }
      }
      memset(gs_bufs.cpu + (pan_size(ATTRIBUTE_BUFFER) * PANVK_VARY_BUF_MAX),
             0, pan_size(ATTRIBUTE_BUFFER));

      /* Per-viewport index lists after the full one (jm-v7 031). */
      draw->vp_split = gs->gs.writes_viewport ? panvk_gs_vp_split(cmdbuf) : 0;
      draw->gs_out_viewport = 0;
      if (draw->vp_split) {
         draw->gs_out_viewport =
            gs_alloc(cmdbuf, growable, MAX2(out_verts, 1) * 4, 4)
               .gpu;
         if (!draw->gs_out_viewport)
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
      }
      const uint64_t idx =
         gs_alloc(cmdbuf, growable,
                                 (uint64_t)MAX2(out_indices, 1) * 4 *
                                    (1 + draw->vp_split),
                                 4)
            .gpu;
      /* slot -> byte offset tables: the vertex shader's generic layout on
       * the way in, the emulation's own on the way out.
       */
      struct pan_ptr offs =
         panvk_cmd_alloc_dev_mem(cmdbuf, desc, 2 * VARYING_SLOT_MAX * 4, 4);
      if (!idx || !offs.gpu)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;
      memcpy(offs.cpu, link->buf_offsets, VARYING_SLOT_MAX * 4);
      memcpy((uint32_t *)offs.cpu + VARYING_SLOT_MAX, link->gs_out_offsets,
             VARYING_SLOT_MAX * 4);

      draw->gs_varying_bufs = gs_bufs.gpu;
      cmdbuf->state.gfx.gs.out_pos = gs_addrs[PANVK_VARY_BUF_POSITION];
      cmdbuf->state.gfx.gs.out_general = gs_addrs[PANVK_VARY_BUF_GENERAL];
      cmdbuf->state.gfx.gs.out_psiz = gs_addrs[PANVK_VARY_BUF_PSIZ];
      cmdbuf->state.gfx.gs.out_index = idx;
      cmdbuf->state.gfx.gs.in_offsets = offs.gpu;
      cmdbuf->state.gfx.gs.out_offsets = offs.gpu + VARYING_SLOT_MAX * 4;
      cmdbuf->state.gfx.gs.out_vertex_count = out_verts;
      cmdbuf->state.gfx.gs.out_index_count = out_indices;
      /* The FS gl_PrimitiveID is the tiler's primitive index modulo
       * DRAW.instance_primitive_size (1 made it always 0). The output slots
       * are instance-major: wrap per instance when that size is known and
       * encodable exactly, else never.
       */
      {
         const uint32_t out_prims =
            MAX2(in_prims * gs->gs.max_output_prims, 1);
         uint32_t wrap = pan_padded_vertex_count(out_prims);
         const bool cpu_known =
            !(draw->info.indirect.buffer_dev_addr && !draw->cpu_count);
         const uint32_t inst =
            draw->cpu_count ? draw->cpu_instances : draw->info.instance.count;
         if (cpu_known && inst > 1 && out_prims % inst == 0 &&
             pan_padded_vertex_count(out_prims / inst) == out_prims / inst)
            wrap = out_prims / inst;
         cmdbuf->state.gfx.gs.out_prims_per_instance = wrap;
      }
      /* The emulation writes independent primitives: the tiler draws a
       * list, whatever strip the shader declared.
       */
      cmdbuf->state.gfx.gs.out_prim =
         gs->gs.output_verts_per_prim == 3   ? MESA_PRIM_TRIANGLES
         : gs->gs.output_verts_per_prim == 2 ? MESA_PRIM_LINES
                                             : MESA_PRIM_POINTS;
      draw->line_width = cmdbuf->state.gfx.gs.out_prim == MESA_PRIM_LINES
                            ? cmdbuf->vk.dynamic_graphics_state.rs.line.width
                            : 1.0f;

      /* Transform feedback: a primitive count per emulation invocation and
       * a capture record per output vertex slot, every stream up to the
       * last one emitted on with its own copy. Captured on layer 0 only.
       */
      cmdbuf->state.gfx.gs.xfb_counts = 0;
      cmdbuf->state.gfx.gs.xfb_staging = 0;
      cmdbuf->state.gfx.gs.xfb_invocations = 0;
      cmdbuf->state.gfx.gs.xfb_stream_index = 0;
      cmdbuf->state.gfx.gs.xfb_stream_slots = 0;
      cmdbuf->state.gfx.gs.xfb_stream_idx = 0;
      if (cmdbuf->state.gfx.xfb.active && cmdbuf->state.gfx.xfb.offsets &&
          gs->gs.xfb_dwords && draw->info.layer_id == 0 && in_prims) {
         const unsigned nstreams = util_last_bit(gs->gs.stream_mask);
         const uint32_t slots = MAX2(out_verts, 1);
         struct pan_ptr counts = gs_alloc(
            cmdbuf, growable, nstreams * in_prims * 4, 4);
         struct pan_ptr staging = gs_alloc(
            cmdbuf, growable, nstreams * slots * gs->gs.xfb_dwords * 4, 4);
         struct pan_ptr stream_index = {0};
         if (nstreams > 1)
            stream_index = gs_alloc(
               cmdbuf, growable, (nstreams - 1) * MAX2(out_indices, 1) * 4, 4);
         if (!counts.gpu || !staging.gpu ||
             (nstreams > 1 && !stream_index.gpu))
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
         cmdbuf->state.gfx.gs.xfb_counts = counts.gpu;
         cmdbuf->state.gfx.gs.xfb_staging = staging.gpu;
         cmdbuf->state.gfx.gs.xfb_invocations = in_prims;
         cmdbuf->state.gfx.gs.xfb_stream_index = stream_index.gpu;
         cmdbuf->state.gfx.gs.xfb_stream_slots = slots;
         cmdbuf->state.gfx.gs.xfb_stream_idx = out_indices;
      }
   }

   draw->vs.varyings = panvk_priv_mem_dev_addr(link->vs.attribs);
   draw->fs.varyings = panvk_priv_mem_dev_addr(link->fs.attribs);
   return VK_SUCCESS;
}

static void
panvk_draw_emit_attrib_buf(
   const struct panvk_draw_data *draw,
   const struct vk_vertex_binding_state *buf_info, uint32_t stride,
   const struct panvk_attrib_buf *buf,
   struct mali_attribute_buffer_packed *desc,
   struct libpan_draw_helper_attrib_buf_info *helper_buf_info)
{
   uint64_t addr = buf->address & ~63ULL;
   unsigned size = buf->size + (buf->address & 63);
   unsigned divisor = draw->padded_vertex_count * buf_info->divisor;
   bool per_instance = buf_info->input_rate == VK_VERTEX_INPUT_RATE_INSTANCE;
   struct mali_attribute_buffer_packed *buf_ext = &desc[1];

   /* In case of indirect draw, the descriptor will be patched at runtime */
   if (helper_buf_info != NULL) {
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D;
         cfg.pointer = addr;
         cfg.size = size;
      }

      helper_buf_info->divisor = buf_info->divisor;
      helper_buf_info->stride = stride;
      helper_buf_info->per_instance = per_instance;
   } else if (draw->info.instance.count <= 1) {
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D;
         cfg.stride = per_instance ? 0 : stride;
         cfg.pointer = addr;
         cfg.size = size;
      }
   } else if (!per_instance) {
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D_MODULUS;
         cfg.divisor = draw->padded_vertex_count;
         cfg.stride = stride;
         cfg.pointer = addr;
         cfg.size = size;
      }
   } else if (!divisor) {
      /* instance_divisor == 0 means all instances share the same value.
       * Make it a 1D array with a zero stride.
       */
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D;
         cfg.stride = 0;
         cfg.pointer = addr;
         cfg.size = size;
      }
   } else if (util_is_power_of_two_or_zero(divisor)) {
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D_POT_DIVISOR;
         cfg.stride = stride;
         cfg.pointer = addr;
         cfg.size = size;
         cfg.divisor_r = __builtin_ctz(divisor);
      }
   } else {
      unsigned divisor_r = 0, divisor_e = 0;
      unsigned divisor_d =
         pan_compute_npot_divisor(divisor, &divisor_r, &divisor_e);
      pan_pack(desc, ATTRIBUTE_BUFFER, cfg) {
         cfg.type = MALI_ATTRIBUTE_TYPE_1D_NPOT_DIVISOR;
         cfg.stride = stride;
         cfg.pointer = addr;
         cfg.size = size;
         cfg.divisor_r = divisor_r;
         cfg.divisor_e = divisor_e;
      }

      pan_cast_and_pack(buf_ext, ATTRIBUTE_BUFFER_CONTINUATION_NPOT, cfg) {
         cfg.divisor_numerator = divisor_d;
         cfg.divisor = buf_info->divisor;
      }

      buf_ext = NULL;
   }

   /* If the buffer extension wasn't used, memset(0) */
   if (buf_ext)
      memset(buf_ext, 0, pan_size(ATTRIBUTE_BUFFER));
}

static void
panvk_draw_emit_attrib(const struct panvk_draw_data *draw,
                       const struct vk_vertex_attribute_state *attrib_info,
                       const struct vk_vertex_binding_state *buf_info,
                       const struct panvk_attrib_buf *buf,
                       struct mali_attribute_packed *desc,
                       struct libpan_draw_helper_attrib_info *helper_attrib_info)
{
   bool per_instance = buf_info->input_rate == VK_VERTEX_INPUT_RATE_INSTANCE;
   enum pipe_format f = vk_format_to_pipe_format(attrib_info->format);
   unsigned buf_idx = attrib_info->binding;

   pan_pack(desc, ATTRIBUTE, cfg) {
      cfg.buffer_index = buf_idx * 2;
      cfg.offset_enable = true;
      cfg.format = GENX(pan_format_from_pipe_format)(f)->hw;

      uint32_t offset = attrib_info->offset + (buf->address & 63);

      /* In case of indirect draw, the descriptor will be patched at runtime */
      if (helper_attrib_info != NULL) {
         helper_attrib_info->base_offset = offset;
         helper_attrib_info->stride = per_instance ? buf_info->stride : 0;
         helper_attrib_info->start_stride =
            per_instance && buf_info->divisor ? buf_info->stride : 0;
      } else {
         cfg.offset = offset;
         if (per_instance)
            cfg.offset += draw->info.instance.base * buf_info->stride;
         /* Bifrost adds the DCD offset_start (the raw vertex offset) to the
          * index of instanced attributes as well: undo it, as gallium's
          * panfrost_emit_vertex_data() does. Divisor 0 has a zero stride. */
         if (per_instance && buf_info->divisor &&
             draw->info.instance.count > 1)
            cfg.offset -=
               (int32_t)(draw->info.vertex.raw_offset * buf_info->stride);
      }
   }
}

/* Don't re-use vertex attributes, always re-emit them.  Descriptors depend on
 * per-draw parameters (e.g. vertex_count, instancing), which are likely to
 * change every draw, so don't bother trying to save an attribute[_buffer]
 * re-emission.
 */
static VkResult
panvk_draw_prepare_vs_attribs(struct panvk_cmd_buffer *cmdbuf,
                              struct panvk_draw_data *draw)
{
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader_variant *vs_variant =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_vertex_input_state *vi = dyns->vi;
   unsigned num_imgs = vs->desc_info.others.count[PANVK_BIFROST_DESC_TABLE_IMG];
   unsigned num_vbs = util_last_bit(vi->bindings_valid);
   unsigned num_vs_attribs =
      num_imgs ? MAX_VS_ATTRIBS
               : MAX2(util_last_bit(vi->attributes_valid),
                      vs_variant->info.attribute_count);
   unsigned attrib_count = num_vs_attribs + num_imgs;

   unsigned attrib_buf_count = (num_vbs + num_imgs) * 2;
   struct pan_ptr bufs = panvk_cmd_alloc_desc_array(
      cmdbuf, attrib_buf_count + 1, ATTRIBUTE_BUFFER);
   struct mali_attribute_buffer_packed *attrib_buf_descs = bufs.cpu;
   struct pan_ptr attribs =
      panvk_cmd_alloc_desc_array(cmdbuf, attrib_count, ATTRIBUTE);
   struct mali_attribute_packed *attrib_descs = attribs.cpu;

   if (!bufs.gpu || (attrib_count && !attribs.gpu))
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   struct libpan_draw_helper_attrib_buf_info *bufs_infos = NULL;
   struct libpan_draw_helper_attrib_info *attribs_infos = NULL;

   if (is_indirect_draw(draw)) {
      struct pan_ptr bufs_infos_storage = panvk_cmd_alloc_dev_mem(
         cmdbuf, desc,
         num_vbs * sizeof(struct libpan_draw_helper_attrib_buf_info), 8);
      struct pan_ptr attribs_infos_storage = panvk_cmd_alloc_dev_mem(
         cmdbuf, desc,
         num_vs_attribs * sizeof(struct libpan_draw_helper_attrib_info), 8);

      if (!bufs_infos_storage.gpu ||
          (num_vs_attribs && !attribs_infos_storage.gpu))
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;

      cmdbuf->state.gfx.vs.indirect_attrib_bufs_infos = bufs_infos_storage.gpu;
      cmdbuf->state.gfx.vs.indirect_attribs_infos = attribs_infos_storage.gpu;
      bufs_infos = bufs_infos_storage.cpu;
      attribs_infos = attribs_infos_storage.cpu;
   }

   for (unsigned i = 0; i < num_vbs; i++) {
      if (vi->bindings_valid & BITFIELD_BIT(i)) {
         struct libpan_draw_helper_attrib_buf_info *helper_buf_info =
            bufs_infos ? &bufs_infos[i] : NULL;
         panvk_draw_emit_attrib_buf(draw, &vi->bindings[i],
                                    dyns->vi_binding_strides[i],
                                    &cmdbuf->state.gfx.vb.bufs[i],
                                    &attrib_buf_descs[i * 2], helper_buf_info);
      } else {
         memset(&attrib_buf_descs[i * 2], 0, sizeof(*attrib_buf_descs) * 2);
      }
   }

   for (unsigned i = 0; i < num_vs_attribs; i++) {
      if (vi->attributes_valid & BITFIELD_BIT(i)) {
         unsigned buf_idx = vi->attributes[i].binding;
         struct libpan_draw_helper_attrib_info *helper_attrib_info =
            attribs_infos ? &attribs_infos[i] : NULL;
         panvk_draw_emit_attrib(draw, &vi->attributes[i],
                                &vi->bindings[buf_idx],
                                &cmdbuf->state.gfx.vb.bufs[buf_idx],
                                &attrib_descs[i], helper_attrib_info);
      } else {
         /* Unbound attributes read the zero default */
         pan_pack(&attrib_descs[i], ATTRIBUTE, cfg)
            cfg.format = MALI_PACK_FMT(CONSTANT, 0000, L);
      }
   }

   /* A NULL entry is needed to stop prefecting on Bifrost */
   memset(bufs.cpu + (pan_size(ATTRIBUTE_BUFFER) * attrib_buf_count), 0,
          pan_size(ATTRIBUTE_BUFFER));

   cmdbuf->state.gfx.vs.attrib_bufs = bufs.gpu;
   cmdbuf->state.gfx.vs.attribs = attribs.gpu;

   if (num_imgs) {
      cmdbuf->state.gfx.vs.desc.img_attrib_table =
         attribs.gpu + (MAX_VS_ATTRIBS * pan_size(ATTRIBUTE));
      cmdbuf->state.gfx.vs.desc.tables[PANVK_BIFROST_DESC_TABLE_IMG] =
         bufs.gpu + (num_vbs * pan_size(ATTRIBUTE_BUFFER) * 2);
   }

   return VK_SUCCESS;
}

static void
panvk_draw_prepare_attributes(struct panvk_cmd_buffer *cmdbuf,
                              struct panvk_draw_data *draw)
{
   panvk_draw_prepare_vs_attribs(cmdbuf, draw);
   draw->vs.attributes = cmdbuf->state.gfx.vs.attribs;
   draw->vs.attribute_bufs = cmdbuf->state.gfx.vs.attrib_bufs;
   draw->indirect_info.attribs = cmdbuf->state.gfx.vs.indirect_attribs_infos;
   draw->indirect_info.attrib_bufs =
      cmdbuf->state.gfx.vs.indirect_attrib_bufs_infos;
}

/* The scissor box of viewport i: the viewport rectangle cut by scissor i,
 * max exclusive.
 */
static void
panvk_viewport_scissor_box(const struct vk_viewport_state *vp, unsigned i,
                           int *minx, int *miny, int *maxx, int *maxy)
{
   const VkViewport *viewport = &vp->viewports[i];
   const VkRect2D *scissor = &vp->scissors[MIN2(i, vp->scissor_count - 1)];

   /* The spec says "width must be greater than 0.0" */
   assert(viewport->width >= 0);
   *minx = (int)viewport->x;
   *maxx = (int)(viewport->x + viewport->width);

   /* Viewport height can be negative */
   *miny = MIN2((int)viewport->y, (int)(viewport->y + viewport->height));
   *maxy = MAX2((int)viewport->y, (int)(viewport->y + viewport->height));

   assert(scissor->offset.x >= 0 && scissor->offset.y >= 0);
   *minx = MAX2(scissor->offset.x, *minx);
   *miny = MAX2(scissor->offset.y, *miny);
   *maxx = MIN2(scissor->offset.x + scissor->extent.width, *maxx);
   *maxy = MIN2(scissor->offset.y + scissor->extent.height, *maxy);
}

/* multiViewport (jm-v7 017): the bound geometry emulation selects the
 * viewport per primitive, so the tiler gets the union of the scissor boxes
 * and depth ranges, and each primitive is cut to its own scissor in the FS
 * (panvk_gs_lower_options::scissor_slot).
 */
static bool
panvk_multi_viewport_draw(const struct panvk_cmd_buffer *cmdbuf)
{
   const struct panvk_shader *emu = cmdbuf->state.gfx.gs.emu;

   return emu && panvk_shader_only_variant(emu)->gs.writes_viewport &&
          cmdbuf->vk.dynamic_graphics_state.vp.viewport_count > 1;
}

static void
panvk_viewport_depth_range_i(const struct panvk_cmd_buffer *cmdbuf,
                             unsigned i, float *z_min, float *z_max)
{
   const struct vk_viewport_state *vp = &cmdbuf->vk.dynamic_graphics_state.vp;

   if (vp->depth_clamp_mode == VK_DEPTH_CLAMP_MODE_USER_DEFINED_RANGE_EXT) {
      *z_min = vp->depth_clamp_range.minDepthClamp;
      *z_max = vp->depth_clamp_range.maxDepthClamp;
      return;
   }

   float scale, offset;
   panvk_viewport_z_coeffs(vp, &cmdbuf->vk.dynamic_graphics_state.rs, i,
                           &scale, &offset);
   const float a = vp->depth_clip_negative_one_to_one ? offset - scale : offset;
   const float b = offset + scale;
   *z_min = MIN2(a, b);
   *z_max = MAX2(a, b);
}

static void
panvk_emit_viewport(struct panvk_cmd_buffer *cmdbuf,
                    struct mali_viewport_packed *vpd)
{
   const struct vk_viewport_state *vp = &cmdbuf->vk.dynamic_graphics_state.vp;

   if (vp->viewport_count < 1)
      return;

   float minz, maxz;
   panvk_depth_range(&cmdbuf->state.gfx, &cmdbuf->vk.dynamic_graphics_state.vp,
                     &minz, &maxz);

   int minx, miny, maxx, maxy;
   panvk_viewport_scissor_box(vp, 0, &minx, &miny, &maxx, &maxy);

   if (panvk_multi_viewport_draw(cmdbuf)) {
      for (unsigned i = 1; i < vp->viewport_count; i++) {
         int x0, y0, x1, y1;
         panvk_viewport_scissor_box(vp, i, &x0, &y0, &x1, &y1);
         if (x1 <= x0 || y1 <= y0)
            continue;
         if (maxx <= minx || maxy <= miny) {
            minx = x0, miny = y0, maxx = x1, maxy = y1;
         } else {
            minx = MIN2(minx, x0);
            miny = MIN2(miny, y0);
            maxx = MAX2(maxx, x1);
            maxy = MAX2(maxy, y1);
         }

         float z0, z1;
         panvk_viewport_depth_range_i(cmdbuf, i, &z0, &z1);
         minz = MIN2(minz, z0);
         maxz = MAX2(maxz, z1);
      }
   }

   /* Make sure we don't end up with a max < min when width/height is 0 */
   maxx = maxx > minx ? maxx - 1 : maxx;
   maxy = maxy > miny ? maxy - 1 : maxy;

   /* Clamp viewport scissor to valid range */
   minx = CLAMP(minx, 0, UINT16_MAX);
   maxx = CLAMP(maxx, 0, UINT16_MAX);
   miny = CLAMP(miny, 0, UINT16_MAX);
   maxy = CLAMP(maxy, 0, UINT16_MAX);

   pan_pack(vpd, VIEWPORT, cfg) {
      cfg.scissor_minimum_x = minx;
      cfg.scissor_minimum_y = miny;
      cfg.scissor_maximum_x = maxx;
      cfg.scissor_maximum_y = maxy;
      cfg.minimum_z = minz;
      cfg.maximum_z = maxz;
   }
}

/* jm-v7 031: the tiler clamps and clips depth to its one viewport's range,
 * so a union of different depth ranges lets primitives through that their
 * own viewport clamps or clips. When the viewports a GS may select have
 * different depth ranges, each viewport gets its own index list and tiler
 * job (panvk_gs_push::vp_split). Returns that viewport count, 0 when the
 * union is exact (jm-v7 017).
 *
 * Limit: primitives are then rasterized per viewport, so two primitives of
 * different viewports whose scissor boxes overlap are not drawn in API
 * order.
 */
static uint32_t
panvk_gs_vp_split(const struct panvk_cmd_buffer *cmdbuf)
{
   if (!panvk_multi_viewport_draw(cmdbuf))
      return 0;

   const struct vk_viewport_state *vp = &cmdbuf->vk.dynamic_graphics_state.vp;
   const uint32_t n = MIN2(vp->viewport_count, PANVK_GS_MAX_VIEWPORTS);
   float z0, z1;
   panvk_viewport_depth_range_i(cmdbuf, 0, &z0, &z1);
   for (uint32_t i = 1; i < n; i++) {
      float a, b;
      panvk_viewport_depth_range_i(cmdbuf, i, &a, &b);
      if (a != z0 || b != z1)
         return n;
   }
   return 0;
}

/* The VIEWPORT descriptor of viewport i alone: its scissor box and depth
 * range (jm-v7 031). Returns false for an empty box: that viewport's tiler
 * job is left out of the chain.
 */
static bool
panvk_emit_viewport_i(struct panvk_cmd_buffer *cmdbuf, unsigned i,
                      struct mali_viewport_packed *vpd)
{
   const struct vk_viewport_state *vp = &cmdbuf->vk.dynamic_graphics_state.vp;
   float minz, maxz;
   int minx, miny, maxx, maxy;

   if (i == 0)
      panvk_depth_range(&cmdbuf->state.gfx, vp, &minz, &maxz);
   else
      panvk_viewport_depth_range_i(cmdbuf, i, &minz, &maxz);
   panvk_viewport_scissor_box(vp, i, &minx, &miny, &maxx, &maxy);

   const bool empty = maxx <= minx || maxy <= miny;
   maxx = maxx > minx ? maxx - 1 : maxx;
   maxy = maxy > miny ? maxy - 1 : maxy;
   minx = CLAMP(minx, 0, UINT16_MAX);
   maxx = CLAMP(maxx, 0, UINT16_MAX);
   miny = CLAMP(miny, 0, UINT16_MAX);
   maxy = CLAMP(maxy, 0, UINT16_MAX);

   pan_pack(vpd, VIEWPORT, cfg) {
      cfg.scissor_minimum_x = minx;
      cfg.scissor_minimum_y = miny;
      cfg.scissor_maximum_x = maxx;
      cfg.scissor_maximum_y = maxy;
      cfg.minimum_z = minz;
      cfg.maximum_z = maxz;
   }
   return !empty;
}

static VkResult
panvk_draw_prepare_viewport(struct panvk_cmd_buffer *cmdbuf,
                            struct panvk_draw_data *draw)
{
   /* When rasterizerDiscardEnable is active, it is allowed to have viewport and
    * scissor disabled.
    * As a result, we define an empty one.
    */
   if (!cmdbuf->state.gfx.vpd || dyn_gfx_state_dirty(cmdbuf, VP_VIEWPORTS) ||
       dyn_gfx_state_dirty(cmdbuf, VP_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE) ||
       dyn_gfx_state_dirty(cmdbuf, VP_SCISSORS) ||
       /* The union of the viewports while a GS selects one (017). */
       gfx_state_dirty(cmdbuf, GS) ||
       dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLIP_ENABLE) ||
       dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLAMP_ENABLE) ||
       dyn_gfx_state_dirty(cmdbuf, VP_DEPTH_CLAMP_RANGE)) {
      struct pan_ptr vp = panvk_cmd_alloc_desc(cmdbuf, VIEWPORT);
      if (!vp.gpu)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;

      panvk_emit_viewport(cmdbuf, vp.cpu);
      cmdbuf->state.gfx.vpd = vp.gpu;
   }

   draw->viewport = cmdbuf->state.gfx.vpd;
   return VK_SUCCESS;
}

static void
panvk_emit_vertex_dcd(struct panvk_cmd_buffer *cmdbuf,
                      const struct panvk_draw_data *draw,
                      struct mali_draw_packed *dcd)
{
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   const struct panvk_shader_desc_state *vs_desc_state =
      &cmdbuf->state.gfx.vs.desc;

   pan_pack(dcd, DRAW, cfg) {
      cfg.state = panvk_priv_mem_dev_addr(vs->rsd);
      cfg.attributes = draw->vs.attributes;
      cfg.attribute_buffers = draw->vs.attribute_bufs;
      cfg.varyings = draw->vs.varyings;
      cfg.varying_buffers = draw->varying_bufs;
      cfg.thread_storage = draw->tls;

      /* In case of indirect draw, the descriptor will be patched at runtime */
      if (!is_indirect_draw(draw)) {
         cfg.offset_start = draw->info.vertex.raw_offset;
         cfg.instance_size =
            draw->info.instance.count > 1 ? draw->padded_vertex_count : 1;
      }

      cfg.uniform_buffers = vs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_UBO];
      cfg.push_uniforms = cmdbuf->state.gfx.vs.push_uniforms;
      cfg.textures = vs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_TEXTURE];
      cfg.samplers = vs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_SAMPLER];
   }
}

static VkResult
panvk_draw_prepare_vertex_job(struct panvk_cmd_buffer *cmdbuf,
                              struct panvk_draw_data *draw)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   struct pan_ptr ptr = panvk_cmd_alloc_desc(cmdbuf, COMPUTE_JOB);
   if (!ptr.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   util_dynarray_append(&batch->jobs, ptr.cpu);
   draw->jobs.vertex = ptr;

   memcpy(pan_section_ptr(ptr.cpu, COMPUTE_JOB, INVOCATION), &draw->invocation,
          pan_size(INVOCATION));

   pan_section_pack(ptr.cpu, COMPUTE_JOB, PARAMETERS, cfg) {
      cfg.job_task_split = 5;
   }

   panvk_emit_vertex_dcd(cmdbuf, draw,
                         pan_section_ptr(ptr.cpu, COMPUTE_JOB, DRAW));
   return VK_SUCCESS;
}

static enum mali_draw_mode
translate_prim(enum mesa_prim prim)
{
   switch (prim) {
   case MESA_PRIM_POINTS:
      return MALI_DRAW_MODE_POINTS;
   case MESA_PRIM_LINES:
      return MALI_DRAW_MODE_LINES;
   case MESA_PRIM_LINE_STRIP:
      return MALI_DRAW_MODE_LINE_STRIP;
   case MESA_PRIM_TRIANGLES:
      return MALI_DRAW_MODE_TRIANGLES;
   case MESA_PRIM_TRIANGLE_STRIP:
      return MALI_DRAW_MODE_TRIANGLE_STRIP;
   case MESA_PRIM_TRIANGLE_FAN:
      return MALI_DRAW_MODE_TRIANGLE_FAN;
#if PAN_ARCH >= 9
   case MESA_PRIM_LINES_ADJACENCY:
      return MALI_DRAW_MODE_LINES_ADJACENCY;
   case MESA_PRIM_LINE_STRIP_ADJACENCY:
      return MALI_DRAW_MODE_LINE_STRIP_ADJACENCY;
   case MESA_PRIM_TRIANGLES_ADJACENCY:
      return MALI_DRAW_MODE_TRIANGLES_ADJACENCY;
   case MESA_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return MALI_DRAW_MODE_TRIANGLE_STRIP_ADJACENCY;
#endif
   default:
      UNREACHABLE("Invalid primitive type");
   }
}

static void
panvk_emit_tiler_primitive(struct panvk_cmd_buffer *cmdbuf,
                           const struct panvk_draw_data *draw,
                           struct mali_primitive_packed *prim)
{
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_input_assembly_state *ia = &dyns->ia;
   const struct vk_rasterization_state *rs = &dyns->rs;
   const bool gs = draw->gs_varying_bufs != 0;
   bool writes_point_size =
      (gs && cmdbuf->state.gfx.gs.out_psiz) ||
      (!gs && vs->info.vs.writes_point_size &&
       (ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST ||
        (draw->tess_phase == PANVK_TESS_PHASE_TES &&
         draw->info.prim == MESA_PRIM_POINTS)));
   bool secondary_shader = vs->info.vs.secondary_enable && fs != NULL;
   assert(!(vs->info.outputs_written & VARYING_BIT_PRIMITIVE_ID));
   bool fs_reads_primitive_id = fs ? fs->info.fs.reads_primitive_id : false;

   pan_pack(prim, PRIMITIVE, cfg) {
      cfg.draw_mode =
         translate_prim(gs ? cmdbuf->state.gfx.gs.out_prim : draw->info.prim);
      if (writes_point_size)
         cfg.point_size_array_format =
            gs ? MALI_POINT_SIZE_ARRAY_FORMAT_FP32
               : MALI_POINT_SIZE_ARRAY_FORMAT_FP16;
      cfg.primitive_index_enable = fs_reads_primitive_id;
      cfg.primitive_index_writeback = fs_reads_primitive_id;

      cfg.first_provoking_vertex =
         cmdbuf->state.gfx.render.first_provoking_vertex != U_TRISTATE_NO;

      if (draw->info.index.restart_enable)
         cfg.primitive_restart = MALI_PRIMITIVE_RESTART_IMPLICIT;
      cfg.job_task_split = 6;

      if (draw->info.index.index_size) {
         switch (draw->info.index.index_size) {
         case 4:
            cfg.index_type = MALI_INDEX_TYPE_UINT32;
            break;
         case 2:
            cfg.index_type = MALI_INDEX_TYPE_UINT16;
            break;
         case 1:
            cfg.index_type = MALI_INDEX_TYPE_UINT8;
            break;
         default:
            UNREACHABLE("Invalid index size");
         }
      }

      /* In case of indirect draw, the descriptor will be patched at runtime */
      cfg.index_count = is_indirect_draw(draw) ? 1 : draw->info.vertex.count;

      /* With a geometry shader the tiler draws what the emulation wrote: an
       * indexed list over its own index buffer. The emulation stores the
       * real count of an indirect draw itself (tiler_index_count).
       */
      if (gs) {
         cfg.primitive_restart = MALI_PRIMITIVE_RESTART_NONE;
         cfg.index_type = MALI_INDEX_TYPE_UINT32;
         cfg.index_count = cmdbuf->state.gfx.gs.out_index_count;
         cfg.indices = cmdbuf->state.gfx.gs.out_index;
         cfg.base_vertex_offset = 0;
      }

      cfg.low_depth_cull = cfg.high_depth_cull =
         vk_rasterization_state_depth_clip_enable(rs);

      cfg.secondary_shader = secondary_shader;
   }
}

static void
panvk_emit_tiler_primitive_size(struct panvk_cmd_buffer *cmdbuf,
                                const struct panvk_draw_data *draw,
                                struct mali_primitive_size_packed *primsz)
{
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   /* A tess eval shader in point mode draws points of its gl_PointSize
    * (jm-v7 033): its draw's prim is POINTS. A geometry shader emitting
    * points with gl_PointSize has its own size buffer (gs.out_psiz).
    */
   const bool gs_psiz =
      draw->gs_varying_bufs && cmdbuf->state.gfx.gs.out_psiz;
   const bool writes_point_size =
      gs_psiz || (!draw->gs_varying_bufs && vs->info.vs.writes_point_size &&
                  draw->info.prim == MESA_PRIM_POINTS);

   pan_pack(primsz, PRIMITIVE_SIZE, cfg) {
      if (writes_point_size) {
         cfg.size_array = gs_psiz ? cmdbuf->state.gfx.gs.out_psiz : draw->psiz;
      } else {
         cfg.fixed_sized = draw->line_width;
      }
   }
}

static void
panvk_emit_tiler_dcd(struct panvk_cmd_buffer *cmdbuf,
                     const struct panvk_draw_data *draw,
                     struct mali_draw_packed *dcd)
{
   struct panvk_shader_desc_state *fs_desc_state = &cmdbuf->state.gfx.fs.desc;
   const struct vk_rasterization_state *rs =
      &cmdbuf->vk.dynamic_graphics_state.rs;

   /* A geometry shader's output replaces the application's primitives. */
   const bool gs = draw->gs_varying_bufs != 0;
   enum mesa_prim reduced_prim =
      u_reduced_prim(gs ? cmdbuf->state.gfx.gs.out_prim : draw->info.prim);
   const bool non_polygon = reduced_prim != MESA_PRIM_TRIANGLES;

   pan_pack(dcd, DRAW, cfg) {
      cfg.front_face_ccw = rs->front_face == VK_FRONT_FACE_COUNTER_CLOCKWISE;
      cfg.cull_front_face =
         !non_polygon && (rs->cull_mode & VK_CULL_MODE_FRONT_BIT) != 0;
      cfg.cull_back_face =
         !non_polygon && (rs->cull_mode & VK_CULL_MODE_BACK_BIT) != 0;

      cfg.position = gs ? cmdbuf->state.gfx.gs.out_pos : draw->position;
      cfg.state = draw->fs.rsd;
      cfg.attributes = fs_desc_state->img_attrib_table;
      cfg.attribute_buffers =
         fs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_IMG];
      cfg.viewport = draw->viewport;
      cfg.varyings = draw->fs.varyings;
      cfg.varying_buffers =
         cfg.varyings ? (gs ? draw->gs_varying_bufs : draw->varying_bufs) : 0;
      cfg.thread_storage = draw->tls;

      /* For all primitives but lines DRAW.flat_shading_vertex must
       * be set to 0 and the provoking vertex is selected with the
       * PRIMITIVE.first_provoking_vertex field.
       */
      if (reduced_prim == MESA_PRIM_LINES)
         cfg.flat_shading_vertex = true;

      /* In case of indirect draw, the descriptor will be patched at runtime */
      if (gs) {
         /* One instance. Both sizes are "padded" fields: 0 does not encode. */
         cfg.offset_start = 0;
         cfg.instance_size = 1;
         cfg.instance_primitive_size =
            cmdbuf->state.gfx.gs.out_prims_per_instance
               ? cmdbuf->state.gfx.gs.out_prims_per_instance
               : 1;
      } else if (!is_indirect_draw(draw)) {
         cfg.offset_start = draw->info.vertex.raw_offset;
         cfg.instance_size =
            draw->info.instance.count > 1 ? draw->padded_vertex_count : 1;
         uint32_t primitives_per_instance =
            DIV_ROUND_UP(draw->padded_vertex_count,
                         mesa_vertices_per_prim(draw->info.prim));
         /* instance_primitive_size has the same restrictions as
          * padded_vertex_count, so we can use pan_padded_vertex_count here. */
         cfg.instance_primitive_size =
            pan_padded_vertex_count(primitives_per_instance);
      }

      cfg.uniform_buffers = fs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_UBO];
      cfg.push_uniforms = cmdbuf->state.gfx.fs.push_uniforms;
      cfg.textures = fs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_TEXTURE];
      cfg.samplers = fs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_SAMPLER];

      cfg.occlusion_query = cmdbuf->state.gfx.occlusion_query.mode;
      cfg.occlusion = cmdbuf->state.gfx.occlusion_query.ptr;
#if PAN_ARCH == 9
      cfg.scissor_to_bounding_box = true;
#endif
   }
}

static void
set_provoking_vertex_mode(struct panvk_cmd_buffer *cmdbuf,
                          enum u_tristate first_provoking_vertex)
{
   struct panvk_cmd_graphics_state *state = &cmdbuf->state.gfx;

   if (first_provoking_vertex != U_TRISTATE_UNSET) {
      /* If this is not the first draw, first_provoking_vertex should match
       * the one from the previous draws. Unfortunately, we can't check it
       * when the render pass is inherited. */
      assert(state->render.first_provoking_vertex == U_TRISTATE_UNSET ||
             state->render.first_provoking_vertex == first_provoking_vertex);
      state->render.first_provoking_vertex = first_provoking_vertex;
   }

   /* Once we emit the first FBDs/TDs, we need to commit to a state. If we
    * choose the wrong one, we will fail the assert when the next application
    * draw happens (with a different state). Use PROVOKING_VERTEX_MODE_FIRST
    * because it's the vulkan default, and so likely to be right more often.
    *
    * TODO: handle this case better */
   if (state->render.first_provoking_vertex == U_TRISTATE_UNSET)
      state->render.first_provoking_vertex = U_TRISTATE_YES;
}

static void panvk_draw_fill_tiler_job(struct panvk_cmd_buffer *cmdbuf,
                                      struct panvk_draw_data *draw,
                                      struct pan_ptr ptr, uint64_t indices);

static VkResult
panvk_draw_prepare_tiler_job(struct panvk_cmd_buffer *cmdbuf,
                             struct panvk_draw_data *draw)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   struct pan_ptr ptr;

   if (cmdbuf->state.gfx.fs.required) {
      const struct panvk_shader_desc_info *fs_desc_info =
         &cmdbuf->state.gfx.fs.shader->desc_info;
      struct panvk_shader_desc_state *fs_desc_state =
         &cmdbuf->state.gfx.fs.desc;
      VkResult result = panvk_per_arch(meta_get_copy_desc_job)(
         cmdbuf, fs_desc_info, &cmdbuf->state.gfx.desc_state,
         fs_desc_state, 0, &ptr);
      if (result != VK_SUCCESS)
         return result;
   }

   if (ptr.cpu)
      util_dynarray_append(&batch->jobs, ptr.cpu);

   draw->jobs.frag_copy_desc = ptr;

   /* Per-viewport tiler jobs (jm-v7 031): one array, the emulation patches
    * each one's index count.
    */
   const uint32_t njobs = draw->gs_varying_bufs && draw->vp_split
                             ? draw->vp_split
                             : 1;
   ptr = njobs > 1 ? panvk_cmd_alloc_desc_array(cmdbuf, njobs, TILER_JOB)
                   : panvk_cmd_alloc_desc(cmdbuf, TILER_JOB);
   if (!ptr.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   util_dynarray_append(&batch->jobs, ptr.cpu);
   draw->jobs.tiler = ptr;
   draw->vp_split_mask = 0;
   if (njobs > 1) {
      struct pan_ptr vps = panvk_cmd_alloc_desc_array(cmdbuf, njobs, VIEWPORT);
      if (!vps.gpu)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;
      const uint64_t union_vp = draw->viewport;
      for (uint32_t i = 0; i < njobs; i++) {
         struct pan_ptr job = {
            .cpu = (uint8_t *)ptr.cpu + i * pan_size(TILER_JOB),
            .gpu = ptr.gpu + i * pan_size(TILER_JOB),
         };
         if (i)
            util_dynarray_append(&batch->jobs, job.cpu);
         struct mali_viewport_packed *vpd =
            (struct mali_viewport_packed *)((uint8_t *)vps.cpu +
                                            i * pan_size(VIEWPORT));
         if (panvk_emit_viewport_i(cmdbuf, i, vpd))
            draw->vp_split_mask |= BITFIELD_BIT(i);
         draw->viewport = vps.gpu + i * pan_size(VIEWPORT);
         panvk_draw_fill_tiler_job(
            cmdbuf, draw, job,
            cmdbuf->state.gfx.gs.out_index +
               (uint64_t)(1 + i) * cmdbuf->state.gfx.gs.out_index_count * 4);
      }
      draw->viewport = union_vp;
      return VK_SUCCESS;
   }

   panvk_draw_fill_tiler_job(cmdbuf, draw, ptr, 0);
   return VK_SUCCESS;
}

/* The tiler job of a draw at job; indices, when not 0, replaces the
 * emulation's index list (a per-viewport copy).
 */
static void
panvk_draw_fill_tiler_job(struct panvk_cmd_buffer *cmdbuf,
                          struct panvk_draw_data *draw, struct pan_ptr ptr,
                          uint64_t indices)
{
   if (draw->gs_varying_bufs) {
      /* The tiler iterates the geometry shader's output. */
      pan_pack_work_groups_compute(
         pan_section_ptr(ptr.cpu, TILER_JOB, INVOCATION), 1,
         cmdbuf->state.gfx.gs.out_vertex_count, 1, 1, 1, 1, true, false);
   } else {
      memcpy(pan_section_ptr(ptr.cpu, TILER_JOB, INVOCATION),
             &draw->invocation, pan_size(INVOCATION));
   }

   panvk_emit_tiler_primitive(cmdbuf, draw,
                              pan_section_ptr(ptr.cpu, TILER_JOB, PRIMITIVE));
   if (indices) {
      /* PRIMITIVE.Indices (words 4-5, Bifrost): the per-viewport copy. */
      uint32_t *prim =
         (uint32_t *)pan_section_ptr(ptr.cpu, TILER_JOB, PRIMITIVE);
      prim[4] = (uint32_t)indices;
      prim[5] = (uint32_t)(indices >> 32);
   }

   panvk_emit_tiler_primitive_size(
      cmdbuf, draw, pan_section_ptr(ptr.cpu, TILER_JOB, PRIMITIVE_SIZE));

   panvk_emit_tiler_dcd(cmdbuf, draw,
                        pan_section_ptr(ptr.cpu, TILER_JOB, DRAW));

   pan_section_pack(ptr.cpu, TILER_JOB, TILER, cfg) {
      cfg.address = PAN_ARCH >= 9 ? draw->tiler_ctx->valhall.desc
                                  : draw->tiler_ctx->bifrost.desc;
   }

   pan_section_pack(ptr.cpu, TILER_JOB, PADDING, padding)
      ;
}

static VkResult
panvk_draw_prepare_idvs_job(struct panvk_cmd_buffer *cmdbuf,
                            struct panvk_draw_data *draw)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   struct pan_ptr ptr = panvk_cmd_alloc_desc(cmdbuf, INDEXED_VERTEX_JOB);
   if (!ptr.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   util_dynarray_append(&batch->jobs, ptr.cpu);
   draw->jobs.idvs = ptr;

   memcpy(pan_section_ptr(ptr.cpu, INDEXED_VERTEX_JOB, INVOCATION),
          &draw->invocation, pan_size(INVOCATION));

   panvk_emit_tiler_primitive(
      cmdbuf, draw, pan_section_ptr(ptr.cpu, INDEXED_VERTEX_JOB, PRIMITIVE));

   panvk_emit_tiler_primitive_size(
      cmdbuf, draw,
      pan_section_ptr(ptr.cpu, INDEXED_VERTEX_JOB, PRIMITIVE_SIZE));

   pan_section_pack(ptr.cpu, INDEXED_VERTEX_JOB, TILER, cfg) {
      cfg.address = PAN_ARCH >= 9 ? draw->tiler_ctx->valhall.desc
                                  : draw->tiler_ctx->bifrost.desc;
   }

   pan_section_pack(ptr.cpu, INDEXED_VERTEX_JOB, PADDING, _) {
   }

   panvk_emit_tiler_dcd(
      cmdbuf, draw,
      pan_section_ptr(ptr.cpu, INDEXED_VERTEX_JOB, FRAGMENT_DRAW));

   panvk_emit_vertex_dcd(
      cmdbuf, draw, pan_section_ptr(ptr.cpu, INDEXED_VERTEX_JOB, VERTEX_DRAW));
   return VK_SUCCESS;
}

static VkResult
panvk_draw_prepare_vs_copy_desc_job(struct panvk_cmd_buffer *cmdbuf,
                                    struct panvk_draw_data *draw)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   const struct panvk_shader_desc_info *vs_desc_info =
      &cmdbuf->state.gfx.vs.shader->desc_info;
   const struct panvk_shader_desc_state *vs_desc_state =
      &cmdbuf->state.gfx.vs.desc;
   const struct vk_vertex_input_state *vi =
      cmdbuf->vk.dynamic_graphics_state.vi;
   unsigned num_vbs = util_last_bit(vi->bindings_valid);
   struct pan_ptr ptr;

   VkResult result = panvk_per_arch(meta_get_copy_desc_job)(
      cmdbuf, vs_desc_info, &cmdbuf->state.gfx.desc_state, vs_desc_state,
      num_vbs * pan_size(ATTRIBUTE_BUFFER) * 2, &ptr);
   if (result != VK_SUCCESS)
      return result;

   if (ptr.cpu) {
      util_dynarray_append(&batch->jobs, ptr.cpu);
   }

   draw->jobs.vertex_copy_desc = ptr;
   return VK_SUCCESS;
}

static VkResult
panvk_draw_prepare_fs_copy_desc_job(struct panvk_cmd_buffer *cmdbuf,
                                    struct panvk_draw_data *draw)
{
   const struct panvk_shader_desc_info *fs_desc_info =
      &cmdbuf->state.gfx.fs.shader->desc_info;
   struct panvk_shader_desc_state *fs_desc_state = &cmdbuf->state.gfx.fs.desc;
   struct panvk_batch *batch = cmdbuf->cur_batch;
   struct pan_ptr ptr;

   VkResult result = panvk_per_arch(meta_get_copy_desc_job)(
      cmdbuf, fs_desc_info, &cmdbuf->state.gfx.desc_state,
      fs_desc_state, 0, &ptr);
   if (result != VK_SUCCESS)
      return result;

   if (ptr.cpu) {
      util_dynarray_append(&batch->jobs, ptr.cpu);
   }

   draw->jobs.frag_copy_desc = ptr;
   return VK_SUCCESS;
}

static VkResult
panvk_cmd_prepare_draw_link_shaders(struct panvk_cmd_buffer *cmd)
{
   struct panvk_cmd_graphics_state *gfx = &cmd->state.gfx;

   if (!gfx_state_dirty(cmd, VS) && !gfx_state_dirty(cmd, FS) &&
       !gfx_state_dirty(cmd, GS))
      return VK_SUCCESS;

   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmd->state.gfx.vs.shader);
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmd));
   const struct panvk_shader_variant *gs =
      cmd->state.gfx.gs.emu ? panvk_shader_only_variant(cmd->state.gfx.gs.emu)
                            : NULL;

   /* The vertex shader of a tessellated draw feeds the tess control job:
    * every generic slot is kept, as for a geometry shader, and nothing is
    * rasterized.
    */
   if (gfx->tess.phase == PANVK_TESS_PHASE_VS_TCS) {
      gs = panvk_shader_only_variant(gfx->tcs.shader);
      fs = NULL;
   }

   VkResult result =
      panvk_per_arch(link_shaders)(&cmd->desc_pool, vs, gs, fs, &gfx->link);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return result;
   }

   return VK_SUCCESS;
}

/* fillModeNonSolid (jm-v7 018): a triangle topology drawn in line or point
 * polygon mode without a geometry shader.
 */
static bool
v7_polygon_mode_draw(const struct panvk_cmd_buffer *cmdbuf, enum mesa_prim prim)
{
   const VkPolygonMode mode =
      cmdbuf->vk.dynamic_graphics_state.rs.polygon_mode;
   const int cls = panvk_xfb_gs_class(prim);

   return (mode == VK_POLYGON_MODE_LINE || mode == VK_POLYGON_MODE_POINT) &&
          (cls == PANVK_XFB_GS_TRIANGLES || cls == PANVK_XFB_GS_TRIANGLES_ADJ) &&
          !cmdbuf->vk.dynamic_graphics_state.rs.rasterizer_discard_enable;
}

static VkResult
prepare_draw(struct panvk_cmd_buffer *cmdbuf, struct panvk_draw_data *draw)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   struct panvk_shader_desc_state *vs_desc_state = &cmdbuf->state.gfx.vs.desc;
   struct panvk_shader_desc_state *fs_desc_state = &cmdbuf->state.gfx.fs.desc;
   struct panvk_descriptor_state *desc_state = &cmdbuf->state.gfx.desc_state;
   const struct vk_rasterization_state *rs =
      &cmdbuf->vk.dynamic_graphics_state.rs;
   VkResult result;
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));

   /* There are only 16 bits in the descriptor for the job ID. Each job has a
    * pilot shader dealing with descriptor copies, and we need one
    * <vertex,tiler> pair per draw.
    */
   if (batch->vtc_jc.job_index + ((5 + PANVK_GS_MAX_VIEWPORTS) * cmdbuf->state.gfx.render.layer_count) >=
       UINT16_MAX) {
      panvk_per_arch(cmd_close_batch)(cmdbuf);
      batch = panvk_per_arch(cmd_open_batch)(cmdbuf);
   }

   /* The geometry emulation runs the bound geometry shader, or for a vertex
    * shader compiled to feed it (transform feedback, or a shader object
    * linked to a geometry shader that is not bound) the passthrough
    * matching this draw's topology. Switching between them relinks.
    */
   const struct panvk_shader *emu = cmdbuf->state.gfx.gs.shader;
   if (draw->tess_phase == PANVK_TESS_PHASE_VS_TCS) {
      /* The geometry shader runs after the tess eval shader. */
      emu = NULL;
   } else if (!emu && vs->gs.vs_clip_space) {
      emu = panvk_per_arch(passthrough_gs)(
         to_panvk_device(cmdbuf->vk.base.device), cmdbuf->state.gfx.vs.shader,
         draw->info.prim);
      /* The vertex shader writes clip space: without the emulation there is
       * nothing the tiler could draw.
       */
      if (!emu) {
         vk_command_buffer_set_error(&cmdbuf->vk,
                                     VK_ERROR_OUT_OF_DEVICE_MEMORY);
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;
      }
   } else if (!emu && v7_polygon_mode_draw(cmdbuf, draw->info.prim)) {
      /* fillModeNonSolid (jm-v7 018): the tiler has no polygon mode, so
       * the triangles go through a passthrough that emits their edges or
       * vertices (and culls faces itself).
       */
      emu = panvk_per_arch(polygon_gs)(
         to_panvk_device(cmdbuf->vk.base.device), cmdbuf->state.gfx.vs.shader,
         draw->info.prim, cmdbuf->vk.dynamic_graphics_state.rs.polygon_mode);
      if (!emu) {
         vk_command_buffer_set_error(&cmdbuf->vk,
                                     VK_ERROR_OUT_OF_DEVICE_MEMORY);
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;
      }
   }
   if (emu != cmdbuf->state.gfx.gs.emu) {
      cmdbuf->state.gfx.gs.emu = emu;
      gfx_state_set_dirty(cmdbuf, GS);
   }

   if (fs_user_dirty(cmdbuf) || gfx_state_dirty(cmdbuf, VS) ||
       gfx_state_dirty(cmdbuf, GS)) {
      result = panvk_cmd_prepare_draw_link_shaders(cmdbuf);
      if (result != VK_SUCCESS)
         return result;
   }

   if (cmdbuf->state.gfx.vk_meta) {
      /* vk_meta doesn't care about the provoking vertex mode, we should use
       * the same mode that the application uses. */
      set_provoking_vertex_mode(cmdbuf, U_TRISTATE_UNSET);
   } else {
      enum u_tristate first_provoking_vertex = u_tristate_make(
         cmdbuf->vk.dynamic_graphics_state.rs.provoking_vertex ==
         VK_PROVOKING_VERTEX_MODE_FIRST_VERTEX_EXT);
      set_provoking_vertex_mode(cmdbuf, first_provoking_vertex);
   }

   if (!rs->rasterizer_discard_enable) {
      ASSERTED const struct pan_fb_layout *fb =
         &cmdbuf->state.gfx.render.fb.layout;
      uint32_t *nr_samples = &cmdbuf->state.gfx.render.fb.nr_samples;
      uint32_t rasterization_samples =
         cmdbuf->vk.dynamic_graphics_state.ms.rasterization_samples;

      /* If there's no attachment, and the FB descriptor hasn't been allocated
       * yet, we patch nr_samples to match rasterization_samples, otherwise, we
       * make sure those two numbers match. */
      if (!batch->fb.desc.gpu && !cmdbuf->state.gfx.render.bound_attachments) {
         assert(rasterization_samples > 0);
         *nr_samples = rasterization_samples;
      } else {
         assert(rasterization_samples == *nr_samples);
      }

      /* In case we already emitted tiler/framebuffer descriptors, we ensure
       * that the sample count didn't change
       * XXX: This currently can happen in case we resume a render pass with no
       * attachements and without any draw as the FBD is emitted when suspending.
       */
      assert(fb->sample_count == 0 ||
             fb->sample_count == cmdbuf->state.gfx.render.fb.nr_samples);

      result = panvk_per_arch(cmd_alloc_fb_desc)(cmdbuf);
      if (result != VK_SUCCESS)
         return result;
   }

   panvk_per_arch(cmd_select_tile_size)(cmdbuf);

   result = panvk_per_arch(cmd_alloc_tls_desc)(cmdbuf, true);
   if (result != VK_SUCCESS)
      return result;

   const struct panvk_shader_desc_info *vs_desc_info =
      &cmdbuf->state.gfx.vs.shader->desc_info;
   const struct panvk_shader_desc_info *fs_desc_info =
      fs ? &cmdbuf->state.gfx.fs.shader->desc_info : NULL;

   uint32_t used_set_mask =
      vs_desc_info->used_set_mask | (fs ? fs_desc_info->used_set_mask : 0);

   if (gfx_state_dirty(cmdbuf, DESC_STATE) || gfx_state_dirty(cmdbuf, VS) ||
       gfx_state_dirty(cmdbuf, FS)) {
      result = panvk_per_arch(cmd_prepare_push_descs)(cmdbuf, desc_state,
                                                      used_set_mask);
      if (result != VK_SUCCESS)
         return result;
   }

   if (gfx_state_dirty(cmdbuf, DESC_STATE) || gfx_state_dirty(cmdbuf, VS)) {
      result = panvk_per_arch(cmd_prepare_shader_desc_tables)(
         cmdbuf, desc_state, vs_desc_info, false, vs_desc_state);
      if (result != VK_SUCCESS)
         return result;

      result = panvk_per_arch(cmd_prepare_dyn_ssbos)(
         cmdbuf, desc_state, vs_desc_info, vs_desc_state);
      if (result != VK_SUCCESS)
         return result;
   }

   /* This allocates and initializes the image table, which we need before we
    * can copy descriptors.
    */
   panvk_draw_prepare_attributes(cmdbuf, draw);

   if (gfx_state_dirty(cmdbuf, DESC_STATE) || gfx_state_dirty(cmdbuf, VS)) {
      result = panvk_draw_prepare_vs_copy_desc_job(cmdbuf, draw);
      if (result != VK_SUCCESS)
         return result;
   }

   if (gfx_state_dirty(cmdbuf, DESC_STATE) || gfx_state_dirty(cmdbuf, FS)) {
      if (fs == NULL) {
         /* No need to setup the FS desc tables if the FS is not executed. */
         memset(fs_desc_state, 0, sizeof(*fs_desc_state));
      } else {
         result = panvk_per_arch(cmd_prepare_shader_desc_tables)(
            cmdbuf, desc_state, fs_desc_info, true, fs_desc_state);
         if (result != VK_SUCCESS)
            return result;

         result = panvk_per_arch(cmd_prepare_dyn_ssbos)(
            cmdbuf, desc_state, fs_desc_info, fs_desc_state);
         if (result != VK_SUCCESS)
            return result;

         result = panvk_draw_prepare_fs_copy_desc_job(cmdbuf, draw);
         if (result != VK_SUCCESS)
            return result;
      }
   }

   draw->tls = batch->tls.gpu;
   draw->fb = batch->fb.desc.gpu;

   result = panvk_draw_prepare_fs_rsd(cmdbuf, draw);
   if (result != VK_SUCCESS)
      return result;

   batch->tlsinfo.tls.size = MAX3(vs->info.tls_size, fs ? fs->info.tls_size : 0,
                                  batch->tlsinfo.tls.size);

   panvk_per_arch(cmd_prepare_draw_sysvals)(cmdbuf, &draw->info, fs);

   /* Viewport emission requires up-to-date {scale,offset}.z for min/max Z,
    * so we need to call it after calling cmd_prepare_draw_sysvals(), but
    * viewports are the same for all layers, so we only emit when layer_id=0.
    */
   result = panvk_draw_prepare_viewport(cmdbuf, draw);
   if (result != VK_SUCCESS)
      return result;

   return VK_SUCCESS;
}

static VkResult
prepare_draw_layer(struct panvk_cmd_buffer *cmdbuf,
                   struct panvk_draw_data *draw, uint32_t layer)
{
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));
   VkResult result;

   /* Before the varyings: transform feedback captures on layer 0 only. */
   draw->info.layer_id = layer;
   result = panvk_draw_prepare_varyings(cmdbuf, draw);
   if (result != VK_SUCCESS)
      return result;

   if (draw->info.layer_id > 0) {
      cmdbuf->state.gfx.sysvals.layer_id = draw->info.layer_id;
      gfx_state_set_dirty(cmdbuf, FS_PUSH_UNIFORMS);
   }

   struct pan_ptr vs_push_uniforms;
   result = panvk_per_arch(cmd_prepare_gfx_push_uniforms)(
      cmdbuf, vs, &vs_push_uniforms, 1);
   if (result != VK_SUCCESS)
      return result;
   cmdbuf->state.gfx.vs.push_uniforms = vs_push_uniforms.gpu;

   if (fs) {
      struct pan_ptr fs_push_uniforms;
      result = panvk_per_arch(cmd_prepare_gfx_push_uniforms)(
         cmdbuf, fs, &fs_push_uniforms, 1);
      if (result != VK_SUCCESS)
         return result;
      cmdbuf->state.gfx.fs.push_uniforms = fs_push_uniforms.gpu;
   }

   result = panvk_draw_prepare_tiler_context(cmdbuf, draw);
   if (result != VK_SUCCESS)
      return result;

   if (vs->info.vs.idvs) {
      result = panvk_draw_prepare_idvs_job(cmdbuf, draw);
      if (result != VK_SUCCESS)
         return result;
   } else {
      result = panvk_draw_prepare_vertex_job(cmdbuf, draw);
      if (result != VK_SUCCESS)
         return result;

      /* A vertex shader that writes no gl_Position leaves the position
       * buffer undefined: rasterize nothing rather than garbage.
       */
      /* Same for a geometry shader without gl_Position (jm-v7 025): its
       * position buffer is not even allocated.
       */
      const bool writes_pos =
         draw->gs_varying_bufs
            ? cmdbuf->state.gfx.gs.out_index_count != 0 &&
                 cmdbuf->state.gfx.gs.out_pos != 0 &&
                 (panvk_shader_only_variant(cmdbuf->state.gfx.gs.emu)
                     ->gs.outputs_written &
                  VARYING_BIT_POS)
            : pan_varying_layout_find_slot(&vs->info.varyings.formats,
                                           VARYING_SLOT_POS) != NULL;
      bool needs_tiling =
         writes_pos && draw->tess_phase != PANVK_TESS_PHASE_VS_TCS &&
         (!cmdbuf->vk.dynamic_graphics_state.rs.rasterizer_discard_enable ||
          cmdbuf->state.gfx.occlusion_query.mode !=
             MALI_OCCLUSION_MODE_DISABLED);

      if (needs_tiling) {
         result = panvk_draw_prepare_tiler_job(cmdbuf, draw);
         if (result != VK_SUCCESS)
            return result;
      }
   }

   return VK_SUCCESS;
}

/*
 * The descriptors of a shader the emulation runs as a compute job. It was
 * compiled as a compute shader, so it reads the bound graphics sets the way
 * a compute shader reads its own: tables filled by a copy job, and set
 * addresses in its compute sysvals. *copy_job is 0 when there is nothing to
 * copy. Returns false when an allocation fails.
 */
static bool
emu_prepare_descs(struct panvk_cmd_buffer *cmdbuf,
                  const struct panvk_shader *shader,
                  struct panvk_compute_sysvals *sysvals,
                  struct panvk_shader_desc_state *ds, unsigned *copy_job)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   struct panvk_descriptor_state *desc_state = &cmdbuf->state.gfx.desc_state;
   const struct panvk_shader_desc_info *info = &shader->desc_info;

   *ds = (struct panvk_shader_desc_state){0};
   *copy_job = 0;

   if (panvk_per_arch(cmd_prepare_push_descs)(cmdbuf, desc_state,
                                              info->used_set_mask) !=
          VK_SUCCESS ||
       panvk_per_arch(cmd_prepare_shader_desc_tables)(cmdbuf, desc_state, info,
                                                      true, ds) != VK_SUCCESS ||
       panvk_per_arch(cmd_prepare_dyn_ssbos)(cmdbuf, desc_state, info, ds) !=
          VK_SUCCESS)
      return false;

   u_foreach_bit(i, info->used_set_mask) {
      if (i < MAX_SETS && desc_state->sets[i])
         sysvals->desc.sets[i] = desc_state->sets[i]->descs.dev;
   }
   sysvals->desc.sets[PANVK_DESC_TABLE_CS_DYN_SSBOS] = ds->dyn_ssbos;

   struct pan_ptr job;
   if (panvk_per_arch(meta_get_copy_desc_job)(cmdbuf, info, desc_state, ds, 0,
                                              &job) != VK_SUCCESS)
      return false;
   if (job.cpu) {
      util_dynarray_append(&batch->jobs, job.cpu);
      *copy_job = pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_COMPUTE, false,
                                 false, 0, 0, &job, false);
   }
   return true;
}

/* PANVK_GS_CULL_* of the polygon-mode passthrough (jm-v7 018). */
static uint32_t
v7_polygon_cull(const struct panvk_cmd_buffer *cmdbuf)
{
   const struct vk_rasterization_state *rs =
      &cmdbuf->vk.dynamic_graphics_state.rs;
   uint32_t cull = 0;

   if (rs->cull_mode & VK_CULL_MODE_FRONT_BIT)
      cull |= PANVK_GS_CULL_FRONT;
   if (rs->cull_mode & VK_CULL_MODE_BACK_BIT)
      cull |= PANVK_GS_CULL_BACK;
   if (rs->front_face == VK_FRONT_FACE_COUNTER_CLOCKWISE)
      cull |= PANVK_GS_FRONT_CCW;
   return cull;
}

/*
 * The geometry shader as a compute job between the vertex job (dep_job_id)
 * and the tiler job. Returns the job the tiler waits on, 0 on failure.
 */
static unsigned
emit_gs_job(struct panvk_cmd_buffer *cmdbuf, struct panvk_draw_data *draw,
            unsigned dep_job_id)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   const struct panvk_shader *gs_shader = cmdbuf->state.gfx.gs.emu;
   const struct panvk_shader_variant *gs = panvk_shader_only_variant(gs_shader);
   const struct panvk_shader_link *link = &cmdbuf->state.gfx.link;
   const struct panvk_graphics_sysvals *gfx_sysvals = &cmdbuf->state.gfx.sysvals;

   if (!panvk_priv_mem_check_alloc(gs->rsd))
      return 0;

   const bool indirect = is_indirect_draw(draw);
   const uint32_t in_prims = gs_input_prims(draw, gs);
   if (!in_prims)
      return dep_job_id;

   /* Primitive restart (jm-v7 030): the emulation assembles strips and fans
    * by position, so a restart-free list of the draw's primitives is built
    * first (panlib_v7_gs_restart_list()), and the emulation draws that list.
    * Indexed draws always take the indirect path here.
    */
   uint64_t index_buf =
      draw->info.index.index_size ? draw->info.index.buffer_dev_addr : 0;
   uint32_t index_size = draw->info.index.index_size;
   uint32_t topology = gs_topology(draw->info.prim);
   uint64_t indirect_cmd = indirect ? draw->info.indirect.buffer_dev_addr : 0;
   const uint32_t in_vpp = gs->gs.input_verts_per_prim;
   if (index_size && draw->info.index.restart_enable &&
       topology != PANVK_GS_TOPO_LIST && in_vpp && indirect_cmd) {
      /* Primitives per instance never exceed the dispatched total. */
      const uint32_t cap = in_prims;
      struct pan_ptr list = panvk_cmd_alloc_dev_mem(
         cmdbuf, desc, (uint64_t)cap * in_vpp * sizeof(uint32_t), 4);
      struct pan_ptr cmd = panvk_cmd_alloc_dev_mem(
         cmdbuf, desc, sizeof(VkDrawIndexedIndirectCommand), 16);
      if (!list.gpu || !cmd.gpu)
         return 0;

      struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
      /* A barrier: the command may come from earlier GPU work. */
      panlib_v7_gs_restart_list(
         &ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER, list.gpu, cmd.gpu,
         indirect_cmd, draw->info.index.buffer_dev_addr,
         draw->info.index.buffer_size / index_size, index_size, topology,
         in_vpp, cap);

      index_buf = list.gpu;
      index_size = 4;
      topology = PANVK_GS_TOPO_LIST;
      indirect_cmd = cmd.gpu;
   }

   /* gl_ViewportIndex (jm-v7 017): each viewport's transform and scissor
    * box. An index past viewportCount (undefined) uses the last viewport.
    */
   uint64_t viewports = 0;
   if (gs->gs.writes_viewport) {
      const struct vk_viewport_state *vps =
         &cmdbuf->vk.dynamic_graphics_state.vp;
      struct pan_ptr vp = panvk_cmd_alloc_dev_mem(
         cmdbuf, desc, PANVK_GS_MAX_VIEWPORTS * PANVK_GS_VIEWPORT_STRIDE, 4);
      if (!vp.gpu)
         return 0;
      for (unsigned i = 0; i < PANVK_GS_MAX_VIEWPORTS; i++) {
         float *e = (float *)((uint8_t *)vp.cpu + i * PANVK_GS_VIEWPORT_STRIDE);
         const unsigned v = MIN2(i, MAX2(vps->viewport_count, 1) - 1);
         if (v == 0) {
            memcpy(e, &gfx_sysvals->viewport, 24);
         } else {
            const VkViewport *w = &vps->viewports[v];
            float z_scale, z_offset;
            panvk_viewport_z_coeffs(vps, &cmdbuf->vk.dynamic_graphics_state.rs,
                                    v, &z_scale, &z_offset);
            e[0] = 0.5f * w->width;
            e[1] = 0.5f * w->height;
            e[2] = z_scale;
            e[3] = 0.5f * w->width + w->x;
            e[4] = 0.5f * w->height + w->y;
            e[5] = z_offset;
         }
         int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
         if (vps->viewport_count)
            panvk_viewport_scissor_box(vps, v, &x0, &y0, &x1, &y1);
         e[6] = x0;
         e[7] = y0;
         e[8] = MAX2(x1, x0);
         e[9] = MAX2(y1, y0);
      }
      viewports = vp.gpu;
   }

   /* Pipeline statistics of the app's geometry shader (not of the xfb
    * passthrough), counted by the emulation itself (jm-v7 016). Layered
    * draws repeat per layer: count layer 0, every view with multiview.
    */
   uint64_t pstats = 0;
   const struct panvk_pstats_query_state *pq = &cmdbuf->state.gfx.pstats_query;
   if (pq->report && cmdbuf->state.gfx.gs.shader && !cmdbuf->state.gfx.vk_meta &&
       (cmdbuf->state.gfx.render.view_mask || draw->info.layer_id == 0)) {
#define PSTAT(name) VK_QUERY_PIPELINE_STATISTIC_##name##_BIT
      const uint64_t addrs[4] = {
         panvk_pstats_counter_addr(pq, PSTAT(GEOMETRY_SHADER_INVOCATIONS), true),
         panvk_pstats_counter_addr(pq, PSTAT(GEOMETRY_SHADER_PRIMITIVES), true),
         panvk_pstats_counter_addr(pq, PSTAT(CLIPPING_INVOCATIONS), true),
         panvk_pstats_counter_addr(pq, PSTAT(CLIPPING_PRIMITIVES), true),
      };
#undef PSTAT
      if (addrs[0] || addrs[1] || addrs[2] || addrs[3]) {
         struct pan_ptr tbl =
            panvk_cmd_alloc_dev_mem(cmdbuf, desc, sizeof(addrs), 8);
         if (!tbl.gpu)
            return 0;
         memcpy(tbl.cpu, addrs, sizeof(addrs));
         pstats = tbl.gpu;
      }
   }

   /* One invocation per (input primitive, gl_InvocationID) pair. */
   const uint32_t wg =
      DIV_ROUND_UP(in_prims * gs->gs.invocations, PANVK_GS_WORKGROUP_SIZE);
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);

   struct panvk_compute_sysvals sysvals = {0};
   sysvals.num_work_groups.x = wg;
   sysvals.num_work_groups.y = 1;
   sysvals.num_work_groups.z = 1;
   sysvals.local_group_size.x = PANVK_GS_WORKGROUP_SIZE;
   sysvals.local_group_size.y = 1;
   sysvals.local_group_size.z = 1;
   sysvals.gs = (struct panvk_gs_push){
      .in_pos = draw->position,
      .in_general = cmdbuf->state.gfx.gs.in_general,
      .out_pos = cmdbuf->state.gfx.gs.out_pos,
      .out_general = cmdbuf->state.gfx.gs.out_general,
      .out_index = cmdbuf->state.gfx.gs.out_index,
      .in_offsets = cmdbuf->state.gfx.gs.in_offsets,
      .out_offsets = cmdbuf->state.gfx.gs.out_offsets,
      .in_general_stride = link->buf_strides[PANVK_VARY_BUF_GENERAL],
      .out_general_stride = link->gs_out_stride,
      .num_input_prims = in_prims,
      /* Indexed draws always take the indirect path here: index_buf is the
       * buffer base, the command supplies firstIndex.
       */
      .index_buf = index_buf,
      .index_size = index_size,
      .topology = topology,
      .prims_per_instance =
         indirect ? 0
                  : gs_prims_per_instance(draw->info.prim,
                                          draw->info.vertex.count, gs),
      .instance_stride =
         !indirect && draw->info.instance.count > 1
            ? draw->padded_vertex_count
            : 0,
      .indirect_cmd = indirect_cmd,
      .in_pos_desc = indirect ? draw->varying_bufs +
                                   PANVK_VARY_BUF_POSITION *
                                      pan_size(ATTRIBUTE_BUFFER)
                              : 0,
      .in_general_desc = indirect ? draw->varying_bufs +
                                       PANVK_VARY_BUF_GENERAL *
                                          pan_size(ATTRIBUTE_BUFFER)
                                  : 0,
      .vertex_dcd = indirect ? draw->jobs.vertex.gpu +
                                  pan_section_offset(COMPUTE_JOB, DRAW)
                             : 0,
      .index_min = draw->index_min_addr,
      /* An indirect draw's real index count is only known on the GPU. */
      .tiler_index_count =
         indirect && draw->jobs.tiler.gpu
            ? draw->jobs.tiler.gpu + pan_section_offset(TILER_JOB, PRIMITIVE) +
                 12
            : 0,
      .xfb_counts = cmdbuf->state.gfx.gs.xfb_counts,
      .xfb_staging = cmdbuf->state.gfx.gs.xfb_staging,
      .xfb_stream_index = cmdbuf->state.gfx.gs.xfb_stream_index,
      .xfb_stream_slots = cmdbuf->state.gfx.gs.xfb_stream_slots,
      .xfb_stream_idx = cmdbuf->state.gfx.gs.xfb_stream_idx,
      .xfb_invocations = cmdbuf->state.gfx.gs.xfb_invocations,
      .in_screen_space = !vs->gs.vs_clip_space,
      .pstats = pstats,
      .polygon_cull = gs->gs.polygon ? v7_polygon_cull(cmdbuf) : 0,
      .viewports = viewports,
      .out_viewport = draw->gs_out_viewport,
      .vp_split = draw->vp_split,
      .index_region = cmdbuf->state.gfx.gs.out_index_count,
      .tiler_job_stride = pan_size(TILER_JOB),
      .in_psiz = cmdbuf->state.gfx.gs.in_psiz,
      .in_psiz_desc = indirect && cmdbuf->state.gfx.gs.in_psiz
                         ? draw->varying_bufs +
                              PANVK_VARY_BUF_PSIZ * pan_size(ATTRIBUTE_BUFFER)
                         : 0,
      .in_psiz_stride = link->buf_strides[PANVK_VARY_BUF_PSIZ],
      .out_psiz = cmdbuf->state.gfx.gs.out_psiz,
      .viewport_scale = {gfx_sysvals->viewport.scale.x,
                         gfx_sysvals->viewport.scale.y,
                         gfx_sysvals->viewport.scale.z},
      .viewport_offset = {gfx_sysvals->viewport.offset.x,
                          gfx_sysvals->viewport.offset.y,
                          gfx_sysvals->viewport.offset.z},
   };

   struct panvk_shader_desc_state ds;
   unsigned copy_job;
   if (!emu_prepare_descs(cmdbuf, gs_shader, &sysvals, &ds, &copy_job))
      return 0;

   struct pan_ptr push_uniforms;
   if (panvk_per_arch(cmd_prepare_gs_push_uniforms)(
          cmdbuf, gs, &sysvals, &push_uniforms) != VK_SUCCESS)
      return 0;

   struct pan_compute_dim dim = {wg, 1, 1};
   uint64_t tls =
      panvk_per_arch(cmd_dispatch_prepare_tls)(cmdbuf, gs, &dim, false);
   if (!tls)
      return 0;

   struct pan_ptr job = panvk_cmd_alloc_desc(cmdbuf, COMPUTE_JOB);
   if (!job.gpu)
      return 0;

   pan_pack_work_groups_compute(
      pan_section_ptr(job.cpu, COMPUTE_JOB, INVOCATION), wg, 1, 1,
      PANVK_GS_WORKGROUP_SIZE, 1, 1, false, false);

   pan_section_pack(job.cpu, COMPUTE_JOB, PARAMETERS, cfg) {
      cfg.job_task_split = util_logbase2_ceil(PANVK_GS_WORKGROUP_SIZE + 1) + 2;
   }

   pan_section_pack(job.cpu, COMPUTE_JOB, DRAW, cfg) {
      cfg.state = panvk_priv_mem_dev_addr(gs->rsd);
      cfg.attributes = ds.img_attrib_table;
      cfg.attribute_buffers = ds.tables[PANVK_BIFROST_DESC_TABLE_IMG];
      cfg.uniform_buffers = ds.tables[PANVK_BIFROST_DESC_TABLE_UBO];
      cfg.textures = ds.tables[PANVK_BIFROST_DESC_TABLE_TEXTURE];
      cfg.samplers = ds.tables[PANVK_BIFROST_DESC_TABLE_SAMPLER];
      cfg.push_uniforms = push_uniforms.gpu;
      cfg.thread_storage = tls;
   }

   util_dynarray_append(&batch->jobs, job.cpu);

   /* barrier: the vertex job's varying writes must be visible to this one.
    * suppress_prefetch: the tiler descriptor after it may be patched here.
    */
   return pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_COMPUTE, true, true,
                         dep_job_id, copy_job, &job, false);
}

/*
 * Transform feedback for a draw whose geometry shader emulation just ran
 * (dep_job_id). panlib_xfb_scan puts the primitives in draw order and fits
 * them into the buffers, panlib_xfb_copy moves the records. The capture
 * jobs of a batch are chained through xfb.last_job, and each draw reads the
 * write offsets the previous one left in its own slot.
 */
static void
emit_xfb_jobs(struct panvk_cmd_buffer *cmdbuf, unsigned dep_job_id)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   const struct panvk_shader_variant *gs =
      panvk_shader_only_variant(cmdbuf->state.gfx.gs.emu);
   const uint32_t n = cmdbuf->state.gfx.gs.xfb_invocations;
   const uint32_t k_dw = gs->gs.xfb_dwords;
   const uint32_t idx_per_invocation =
      gs->gs.max_output_prims * gs->gs.output_verts_per_prim;

   /* One scan and copy per vertex stream, over the buffers that stream
    * captures to (the others get stride 0, which both kernels skip).
    */
   u_foreach_bit(s, gs->gs.stream_mask) {
      bool captures = false;
      for (unsigned b = 0; b < 4; b++)
         captures |= gs->gs.xfb_strides[b] && gs->gs.xfb_buffer_stream[b] == s;
      /* A stream query still counts the primitives of a stream that
       * captures nothing (jm-v7 016): scan only.
       */
      const uint64_t query =
         cmdbuf->state.gfx.vk_meta ? 0 : cmdbuf->state.gfx.xfb.queries[s];
      if (!captures && !query)
         continue;

      /* [draw block][16: offsets after this draw][k_dw: table]
       * [n: first primitive per invocation]
       */
      struct pan_ptr mem = panvk_cmd_alloc_dev_mem(
         cmdbuf, desc, sizeof(struct libpan_xfb_draw) + 16 + (k_dw + n) * 4,
         16);
      if (!mem.gpu)
         return;

      const uint64_t slot = mem.gpu + sizeof(struct libpan_xfb_draw);
      const uint64_t table = slot + 16;
      struct libpan_xfb_draw *d = mem.cpu;
      *d = (struct libpan_xfb_draw){
         .offsets_in = cmdbuf->state.gfx.xfb.offsets,
         .offsets_out = slot,
         .counts = cmdbuf->state.gfx.gs.xfb_counts + (uint64_t)s * n * 4,
         .first = table + k_dw * 4,
         .indices = s ? cmdbuf->state.gfx.gs.xfb_stream_index +
                           (uint64_t)(s - 1) *
                              cmdbuf->state.gfx.gs.xfb_stream_idx * 4
                      : cmdbuf->state.gfx.gs.out_index,
         .staging = cmdbuf->state.gfx.gs.xfb_staging,
         .table = table,
         .invocations = n,
         .idx_per_invocation = idx_per_invocation,
         .vpp = gs->gs.output_verts_per_prim,
         .record_dwords = k_dw,
         .query = query,
      };
      for (unsigned b = 0; b < 4; b++) {
         d->buffer[b] = cmdbuf->state.gfx.xfb.buffers[b].addr;
         d->size[b] = cmdbuf->state.gfx.xfb.buffers[b].size;
         d->stride[b] =
            gs->gs.xfb_buffer_stream[b] == s ? gs->gs.xfb_strides[b] : 0;
      }
      memcpy((uint8_t *)mem.cpu + sizeof(struct libpan_xfb_draw) + 16,
             gs->gs.xfb_table, k_dw * 4);
      cmdbuf->state.gfx.xfb.offsets = slot;

      struct panvk_precomp_ctx precomp_ctx = panvk_per_arch(precomp_cs)(cmdbuf);
      const struct panlib_xfb_scan_args scan = {.d = mem.gpu};
      panlib_xfb_scan_struct(
         &precomp_ctx,
         panlib_1d_with_jm_deps(1, dep_job_id, cmdbuf->state.gfx.xfb.last_job),
         PANLIB_BARRIER_JM_BARRIER, scan);
      const unsigned scanned = batch->vtc_jc.job_index;

      if (captures) {
         const struct panlib_xfb_copy_args copy = {.d = mem.gpu};
         panlib_xfb_copy_struct(
            &precomp_ctx,
            panlib_1d_with_jm_deps(DIV_ROUND_UP(n, 64), scanned, 0),
            PANLIB_BARRIER_JM_BARRIER, copy);
      }
      cmdbuf->state.gfx.xfb.last_job = batch->vtc_jc.job_index;
   }
}

/*
 * Tessellation, after the vertex job of the API draw (dep_job_id):
 * panlib_v7_tess_setup() sizes the tess control job from the draw's command,
 * then the tess control shader runs as compute, one workgroup per patch and
 * instance, one invocation per output vertex. It reads the vertex shader's
 * varyings like the geometry emulation does (panvk_nir_lower_tcs_inputs()).
 * Returns the tess control job index, 0 on failure.
 */
static unsigned
emit_tess_tcs_job(struct panvk_cmd_buffer *cmdbuf, struct panvk_draw_data *draw,
                  unsigned dep_job_id)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   const struct panvk_shader *tcs_shader = cmdbuf->state.gfx.tcs.shader;
   const struct panvk_shader_variant *tcs =
      panvk_shader_only_variant(tcs_shader);
   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   const struct panvk_shader_link *link = &cmdbuf->state.gfx.link;
   const struct panvk_graphics_sysvals *gfx_sysvals = &cmdbuf->state.gfx.sysvals;
   const bool indirect = is_indirect_draw(draw);
   const uint32_t local_size = MAX2(tcs->gs.tess.out_patch_size, 1);

   if (!panvk_priv_mem_check_alloc(tcs->rsd))
      return 0;

   /* The vertex shader's generic layout, by slot. */
   struct pan_ptr offs =
      panvk_cmd_alloc_dev_mem(cmdbuf, desc, VARYING_SLOT_MAX * 4, 4);
   if (!offs.gpu)
      return 0;
   memcpy(offs.cpu, link->buf_offsets, VARYING_SLOT_MAX * 4);

   /* The grid is written by panlib_v7_tess_setup(), the shader does not read
    * the workgroup count.
    */
   struct panvk_compute_sysvals sysvals = {0};
   sysvals.num_work_groups.x = 1;
   sysvals.num_work_groups.y = 1;
   sysvals.num_work_groups.z = 1;
   sysvals.local_group_size.x = local_size;
   sysvals.local_group_size.y = 1;
   sysvals.local_group_size.z = 1;
   sysvals.gs = (struct panvk_gs_push){
      .in_pos = draw->position,
      .in_general = cmdbuf->state.gfx.gs.in_general,
      .in_offsets = offs.gpu,
      .in_general_stride = link->buf_strides[PANVK_VARY_BUF_GENERAL],
      /* Indexed draws always take the indirect path: index_buf is the
       * buffer base, the command supplies firstIndex.
       */
      .index_buf = draw->info.index.index_size
                      ? draw->info.index.buffer_dev_addr
                      : 0,
      .index_size = draw->info.index.index_size,
      .topology = PANVK_GS_TOPO_LIST,
      .instance_stride =
         !indirect && draw->info.instance.count > 1
            ? draw->padded_vertex_count
            : 0,
      .indirect_cmd = indirect ? draw->info.indirect.buffer_dev_addr : 0,
      .in_pos_desc = indirect ? draw->varying_bufs +
                                   PANVK_VARY_BUF_POSITION *
                                      pan_size(ATTRIBUTE_BUFFER)
                              : 0,
      .in_general_desc = indirect ? draw->varying_bufs +
                                       PANVK_VARY_BUF_GENERAL *
                                          pan_size(ATTRIBUTE_BUFFER)
                                  : 0,
      .vertex_dcd = indirect ? draw->jobs.vertex.gpu +
                                  pan_section_offset(COMPUTE_JOB, DRAW)
                             : 0,
      .index_min = draw->index_min_addr,
      .in_psiz = cmdbuf->state.gfx.gs.in_psiz,
      .in_psiz_desc = indirect && cmdbuf->state.gfx.gs.in_psiz
                         ? draw->varying_bufs +
                              PANVK_VARY_BUF_PSIZ * pan_size(ATTRIBUTE_BUFFER)
                         : 0,
      .in_psiz_stride = link->buf_strides[PANVK_VARY_BUF_PSIZ],
      .in_screen_space = !vs->gs.vs_clip_space,
      .viewport_scale = {gfx_sysvals->viewport.scale.x,
                         gfx_sysvals->viewport.scale.y,
                         gfx_sysvals->viewport.scale.z},
      .viewport_offset = {gfx_sysvals->viewport.offset.x,
                          gfx_sysvals->viewport.offset.y,
                          gfx_sysvals->viewport.offset.z},
      .tess_params =
         cmdbuf->state.gfx.tess.td + offsetof(struct panlib_v7_tess, p),
      .patch_size =
         MAX2(cmdbuf->vk.dynamic_graphics_state.ts.patch_control_points, 1),
   };

   struct panvk_shader_desc_state ds;
   unsigned copy_job;
   if (!emu_prepare_descs(cmdbuf, tcs_shader, &sysvals, &ds, &copy_job))
      return 0;

   struct pan_ptr push_uniforms;
   if (panvk_per_arch(cmd_prepare_gs_push_uniforms)(
          cmdbuf, tcs, &sysvals, &push_uniforms) != VK_SUCCESS)
      return 0;

   struct pan_compute_dim dim = {1, 1, 1};
   uint64_t tls =
      panvk_per_arch(cmd_dispatch_prepare_tls)(cmdbuf, tcs, &dim, false);
   if (!tls)
      return 0;

   struct pan_ptr job = panvk_cmd_alloc_desc(cmdbuf, COMPUTE_JOB);
   if (!job.gpu)
      return 0;

   pan_pack_work_groups_compute(
      pan_section_ptr(job.cpu, COMPUTE_JOB, INVOCATION), 1, 1, 1, local_size,
      1, 1, false, false);

   pan_section_pack(job.cpu, COMPUTE_JOB, PARAMETERS, cfg) {
      cfg.job_task_split = util_logbase2_ceil(local_size + 1) + 2;
   }

   pan_section_pack(job.cpu, COMPUTE_JOB, DRAW, cfg) {
      cfg.state = panvk_priv_mem_dev_addr(tcs->rsd);
      cfg.attributes = ds.img_attrib_table;
      cfg.attribute_buffers = ds.tables[PANVK_BIFROST_DESC_TABLE_IMG];
      cfg.uniform_buffers = ds.tables[PANVK_BIFROST_DESC_TABLE_UBO];
      cfg.textures = ds.tables[PANVK_BIFROST_DESC_TABLE_TEXTURE];
      cfg.samplers = ds.tables[PANVK_BIFROST_DESC_TABLE_SAMPLER];
      cfg.push_uniforms = push_uniforms.gpu;
      cfg.thread_storage = tls;
   }

   util_dynarray_append(&batch->jobs, job.cpu);

   /* A barrier: the first write of this draw to the tess arena, after every
    * earlier job of the chain (the previous tessellated draw is done with
    * it). It patches the tess control job, which must not be prefetched.
    */
   struct panvk_precomp_ctx precomp_ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   panlib_v7_tess_setup(&precomp_ctx, panlib_1d(1),
                        (enum panlib_barrier)(PANLIB_BARRIER_JM_BARRIER |
                                              PANLIB_BARRIER_JM_SUPPRESS_PREFETCH),
                        cmdbuf->state.gfx.tess.td,
                        cmdbuf->state.gfx.tess.src_draw, job.gpu);

   return pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_COMPUTE, true, true,
                         dep_job_id, copy_job, &job, false);
}

static void panvk_tess_draw(struct panvk_cmd_buffer *cmdbuf,
                            const struct panvk_draw_data *draw);

/* Pipeline statistics of one draw (jm-v7 016), counted on the GPU by
 * panlib_v7_update_pstats_draw() from the draw's Vk[Indexed]DrawIndirect-
 * Command (written here for vkCmdDraw). The API draw counts the input
 * assembly, vertex shader and tess control patches; the tess eval draw of a
 * tessellated draw counts the tess eval invocations. Clipping is counted
 * here only without a geometry shader or tessellation: the geometry
 * emulation counts its own output (panvk_nir_lower_gs.c). vk_meta draws are
 * not counted.
 */
/* The draw's tiler job, or its per-viewport tiler jobs (jm-v7 031, those
 * with a non-empty scissor box), after dep_job_id.
 */
static void
add_tiler_jobs(struct panvk_batch *batch, struct panvk_draw_data *draw,
               unsigned dep_job_id, bool suppress_prefetch)
{
   if (!draw->gs_varying_bufs || !draw->vp_split) {
      pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_TILER, false,
                     suppress_prefetch, dep_job_id, 0, &draw->jobs.tiler,
                     false);
      return;
   }

   u_foreach_bit(i, draw->vp_split_mask) {
      struct pan_ptr job = {
         .cpu = (uint8_t *)draw->jobs.tiler.cpu + i * pan_size(TILER_JOB),
         .gpu = draw->jobs.tiler.gpu + i * pan_size(TILER_JOB),
      };
      pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_TILER, false,
                     suppress_prefetch, dep_job_id, 0, &job, false);
   }
}

static void
v7_count_draw_queries(struct panvk_cmd_buffer *cmdbuf,
                      const struct panvk_draw_data *draw)
{
#define PSTAT(name) VK_QUERY_PIPELINE_STATISTIC_##name##_BIT
   const struct panvk_pstats_query_state *q = &cmdbuf->state.gfx.pstats_query;

   if (!q->report || cmdbuf->state.gfx.vk_meta ||
       draw->tess_phase == PANVK_TESS_PHASE_VS_TCS)
      return;

   const bool tes = draw->tess_phase == PANVK_TESS_PHASE_TES;
   const bool gs = cmdbuf->state.gfx.gs.shader != NULL;
   const bool patches = draw->info.prim == MESA_PRIM_PATCHES;
   /* IA vertices, IA primitives, VS invocations, clipping invocations,
    * clipping primitives, TCS patches: see panlib_v7_update_pstats_draw.
    */
   uint32_t bits[6] = {0};
   if (tes) {
      bits[2] = PSTAT(TESSELLATION_EVALUATION_SHADER_INVOCATIONS);
   } else {
      bits[0] = PSTAT(INPUT_ASSEMBLY_VERTICES);
      bits[1] = PSTAT(INPUT_ASSEMBLY_PRIMITIVES);
      bits[2] = PSTAT(VERTEX_SHADER_INVOCATIONS);
      if (patches)
         bits[5] = PSTAT(TESSELLATION_CONTROL_SHADER_PATCHES);
   }
   if (!gs && !patches) {
      bits[3] = PSTAT(CLIPPING_INVOCATIONS);
      bits[4] = PSTAT(CLIPPING_PRIMITIVES);
   }

   uint32_t slots = 0;
   bool any = false;
   for (uint32_t i = 0; i < 6; i++) {
      uint32_t slot = 0xf;
      if (bits[i] && (q->mask & bits[i])) {
         slot = util_bitcount(q->mask & (bits[i] - 1));
         any = true;
      }
      slots |= slot << (i * 4);
   }
   if (!any)
      return;

   uint64_t cmd = draw->info.indirect.buffer_dev_addr;
   if (!cmd) {
      struct pan_ptr ptr = panvk_cmd_alloc_dev_mem(
         cmdbuf, desc, sizeof(VkDrawIndexedIndirectCommand), 16);
      if (!ptr.gpu)
         return;
      *(VkDrawIndexedIndirectCommand *)ptr.cpu = (VkDrawIndexedIndirectCommand){
         .indexCount = draw->info.vertex.count,
         .instanceCount = draw->info.instance.count,
      };
      cmd = ptr.gpu;
   }

   const uint32_t index_size = tes ? 0 : draw->info.index.index_size;
   const uint32_t view_mask = cmdbuf->state.gfx.render.view_mask;
   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   /* A barrier: the command may come from earlier GPU work (indirect
    * buffers, the tessellator, vkCmdDrawIndirectByteCountEXT).
    */
   panlib_v7_update_pstats_draw(
      &ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER,
      q->report + util_bitcount(q->mask) * sizeof(uint64_t), slots, cmd,
      index_size ? cmdbuf->state.gfx.ib.dev_addr : 0,
      index_size ? cmdbuf->state.gfx.ib.size / index_size : 0, index_size,
      index_size && draw->info.index.restart_enable ? 1 : 0, draw->info.prim,
      view_mask ? util_bitcount(view_mask) : 1,
      cmdbuf->vk.dynamic_graphics_state.ts.patch_control_points);
#undef PSTAT
}

static void
panvk_cmd_draw(struct panvk_cmd_buffer *cmdbuf, struct panvk_draw_data *draw)
{
   const struct panvk_shader_variant *vs = panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   VkResult result;

   /* If there's no vertex shader, we can skip the draw, unless a geometry
    * shader runs after it (jm-v7 025): an empty vertex shader (no binary,
    * no RSD) still feeds one that only reads gl_PrimitiveIDIn & co. The
    * vertex job is then left out.
    */
   const bool vs_job = panvk_priv_mem_check_alloc(vs->rsd);
   if (!vs_job && (!cmdbuf->state.gfx.gs.shader ||
                   draw->info.prim == MESA_PRIM_PATCHES || draw->tess_phase))
      return;

   v7_count_draw_queries(cmdbuf, draw);

   if (draw->info.prim == MESA_PRIM_PATCHES && !draw->tess_phase) {
      panvk_tess_draw(cmdbuf, draw);
      return;
   }

   /* Needs to be done before get_fs() is called because it depends on
    * fs.required being initialized. */
   cmdbuf->state.gfx.fs.required =
      fs_required(&cmdbuf->state.gfx, &cmdbuf->vk.dynamic_graphics_state);

   result = prepare_draw(cmdbuf, draw);
   if (result != VK_SUCCESS)
      return;

   pan_pack_work_groups_compute(&draw->invocation, 1, draw->vertex_range,
                                draw->info.instance.count, 1, 1, 1, true,
                                false);

   struct panvk_batch *batch = cmdbuf->cur_batch;

   unsigned copy_desc_job_id =
      draw->jobs.vertex_copy_desc.gpu
         ? pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_COMPUTE, false, false,
                          0, 0, &draw->jobs.vertex_copy_desc, false)
         : 0;

   if (draw->jobs.frag_copy_desc.gpu) {
      /* We don't need to add frag_copy_desc as a dependency because the
       * tiler job doesn't execute the fragment shader, the fragment job
       * will, and the tiler/fragment synchronization happens at the batch
       * level. */
      pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_COMPUTE, false, false, 0, 0,
                     &draw->jobs.frag_copy_desc, false);
   }

   uint32_t view_mask = cmdbuf->state.gfx.render.view_mask;
   assert(view_mask == 0 || util_bitcount(view_mask) <= batch->fb.layer_count);
   uint32_t enabled_layer_count = view_mask
                                     ? util_bitcount(view_mask)
                                     : cmdbuf->state.gfx.render.layer_count;

   /* The vertex shader of a tessellated draw runs once, the layers are the
    * tess eval draw's.
    */
   if (draw->tess_phase == PANVK_TESS_PHASE_VS_TCS)
      enabled_layer_count = 1;

   for (uint32_t i = 0; i < enabled_layer_count; i++) {
      const uint32_t layer = (view_mask != 0) ? u_bit_scan(&view_mask) : i;
      result = prepare_draw_layer(cmdbuf, draw, layer);
      if (result != VK_SUCCESS)
         return;

      if (vs->info.vs.idvs) {
         pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_INDEXED_VERTEX, false,
                        false, 0, copy_desc_job_id, &draw->jobs.idvs, false);
      } else {
         unsigned vjob_id =
            vs_job ? pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_VERTEX,
                                    false, false, 0, copy_desc_job_id,
                                    &draw->jobs.vertex, false)
                   : copy_desc_job_id;

         /* Tessellation: the tess control job instead of a tiler job. The
          * rest of the draw is recorded by panvk_tess_draw().
          */
         if (draw->tess_phase == PANVK_TESS_PHASE_VS_TCS) {
            cmdbuf->state.gfx.tess.tcs_job =
               emit_tess_tcs_job(cmdbuf, draw, vjob_id);
            return;
         }

         /* Geometry shader emulation between the vertex and tiler jobs. */
         if (draw->gs_varying_bufs) {
            vjob_id = emit_gs_job(cmdbuf, draw, vjob_id);
            if (!vjob_id)
               return;
            if (cmdbuf->state.gfx.gs.xfb_counts)
               emit_xfb_jobs(cmdbuf, vjob_id);
         }

         if (draw->jobs.tiler.gpu != 0)
            add_tiler_jobs(batch, draw, vjob_id, false);
      }
   }

   clear_dirty_after_draw(cmdbuf);
}

static void
panvk_cmd_draw_indirect(struct panvk_cmd_buffer *cmdbuf,
                        struct panvk_draw_data *draw)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct panvk_shader_variant *vs = panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   VkResult result;

   /* If there's no vertex shader, we can skip the draw, unless a geometry
    * shader runs after it (jm-v7 025): an empty vertex shader (no binary,
    * no RSD) still feeds one that only reads gl_PrimitiveIDIn & co. The
    * vertex job is then left out.
    */
   const bool vs_job = panvk_priv_mem_check_alloc(vs->rsd);
   if (!vs_job && (!cmdbuf->state.gfx.gs.shader ||
                   draw->info.prim == MESA_PRIM_PATCHES || draw->tess_phase))
      return;

   v7_count_draw_queries(cmdbuf, draw);

   if (draw->info.prim == MESA_PRIM_PATCHES && !draw->tess_phase) {
      panvk_tess_draw(cmdbuf, draw);
      return;
   }

   /* Needs to be done before get_fs() is called because it depends on
    * fs.required being initialized. */
   cmdbuf->state.gfx.fs.required =
      fs_required(&cmdbuf->state.gfx, &cmdbuf->vk.dynamic_graphics_state);

   result = prepare_draw(cmdbuf, draw);
   if (result != VK_SUCCESS)
      return;

   struct panvk_batch *batch = cmdbuf->cur_batch;
   const struct vk_vertex_input_state *vi =
      cmdbuf->vk.dynamic_graphics_state.vi;

   unsigned init_heap_job_id;
   result = panvk_per_arch(cmd_init_poly_heap)(cmdbuf, &init_heap_job_id);
   if (result != VK_SUCCESS)
      return;

   unsigned copy_desc_job_id =
      draw->jobs.vertex_copy_desc.gpu
         ? pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_COMPUTE, false, false,
                          0, 0, &draw->jobs.vertex_copy_desc, false)
         : 0;

   if (draw->jobs.frag_copy_desc.gpu) {
      /* We don't need to add frag_copy_desc as a dependency because the
       * tiler job doesn't execute the fragment shader, the fragment job
       * will, and the tiler/fragment synchronization happens at the batch
       * level. */
      pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_COMPUTE, false, false, 0, 0,
                     &draw->jobs.frag_copy_desc, false);
   }

   uint32_t view_mask = cmdbuf->state.gfx.render.view_mask;
   assert(view_mask == 0 || util_bitcount(view_mask) <= batch->fb.layer_count);
   uint32_t enabled_layer_count = view_mask
                                     ? util_bitcount(view_mask)
                                     : cmdbuf->state.gfx.render.layer_count;

   struct panvk_precomp_ctx precomp_ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   uint64_t index_min_max_res_ptr = 0;
   uint32_t job_before_indirect_helper = copy_desc_job_id;
   if (draw->info.index.index_size) {
      index_min_max_res_ptr =
         panvk_cmd_alloc_dev_mem(
            cmdbuf, desc,
            sizeof(struct libpan_draw_helper_index_min_max_result), 8)
            .gpu;
      const struct panlib_draw_index_minmax_search_helper_args args = {
         .index_buffer_ptr = draw->info.index.buffer_dev_addr,
         .cmd = draw->info.indirect.buffer_dev_addr,
         .min_ptr =
            index_min_max_res_ptr +
            offsetof(struct libpan_draw_helper_index_min_max_result, min),
         .max_ptr =
            index_min_max_res_ptr +
            offsetof(struct libpan_draw_helper_index_min_max_result, max),
      };

      struct libpan_draw_helper_index_min_max_result val = {
         .min = ((uint64_t)1 << (draw->info.index.index_size * 8)) - 1,
         .max = 0,
      };
      uint64_t *raw_val = (uint64_t *)&val;

      struct pan_ptr write_job =
         pan_pool_alloc_desc(&cmdbuf->desc_pool.base, WRITE_VALUE_JOB);

      pan_section_pack(write_job.cpu, WRITE_VALUE_JOB, PAYLOAD, payload) {
         payload.type = MALI_WRITE_VALUE_TYPE_IMMEDIATE_64;
         payload.address = index_min_max_res_ptr;
         payload.immediate_value = *raw_val;
      };

      unsigned write_job_id =
         pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_WRITE_VALUE, false, false,
                        0, copy_desc_job_id, &write_job, false);
      util_dynarray_append(&batch->jobs, write_job.cpu);

      const uint32_t index_count =
         draw->info.index.buffer_size / draw->info.index.index_size;
      uint32_t wg_count = DIV_ROUND_UP(index_count, 65536);
      assert(wg_count <= 65536);

      panlib_draw_index_minmax_search_helper_struct(
         &precomp_ctx, panlib_1d_with_jm_deps(wg_count, 0, write_job_id),
         PANLIB_BARRIER_NONE, args, util_logbase2(draw->info.index.index_size),
         draw->info.index.restart_enable);
      job_before_indirect_helper = batch->vtc_jc.job_index;
      draw->index_min_addr =
         index_min_max_res_ptr +
         offsetof(struct libpan_draw_helper_index_min_max_result, min);
   }

   /* The vertex shader of a tessellated draw runs once. */
   if (draw->tess_phase == PANVK_TESS_PHASE_VS_TCS)
      enabled_layer_count = 1;

   for (uint32_t i = 0; i < enabled_layer_count; i++) {
      const uint32_t layer = (view_mask != 0) ? u_bit_scan(&view_mask) : i;

      /* Force a new push uniform block to be allocated */
      gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);

      result = prepare_draw_layer(cmdbuf, draw, layer);
      if (result != VK_SUCCESS)
         return;

      assert(draw->info.indirect.buffer_dev_addr != 0 ||
             draw->info.index.index_size);

      uint32_t attrib_bufs_valid = vi->bindings_valid;
      uint32_t attribs_valid = vi->attributes_valid;
      uint64_t first_vertex_sysval = 0x8ull << 60;
      uint64_t first_instance_sysval = 0x8ull << 60;
      uint64_t raw_vertex_offset_sysval = 0x8ull << 60;
      if (shader_uses_sysval(vs, graphics, vs.first_vertex)) {
         first_vertex_sysval = cmdbuf->state.gfx.vs.push_uniforms +
                               shader_remapped_sysval_offset(
                                  vs, sysval_offset(graphics, vs.first_vertex));
      }

      if (shader_uses_sysval(vs, graphics, vs.base_instance)) {
         first_instance_sysval =
            cmdbuf->state.gfx.vs.push_uniforms +
            shader_remapped_sysval_offset(
               vs, sysval_offset(graphics, vs.base_instance));
      }

      if (shader_uses_sysval(vs, graphics, vs.raw_vertex_offset)) {
         raw_vertex_offset_sysval =
            cmdbuf->state.gfx.vs.push_uniforms +
            shader_remapped_sysval_offset(
               vs, sysval_offset(graphics, vs.raw_vertex_offset));
      }

      enum panlib_barrier indirect_barrier =
         PANLIB_BARRIER_JM_SUPPRESS_PREFETCH;
      struct panlib_precomp_grid indirect_grid = panlib_1d_with_jm_deps(
         1, draw->indirect_dep_job, job_before_indirect_helper);

      if (draw->info.indirect.buffer_dev_addr != 0 &&
          draw->info.index.index_size) {
         const struct panlib_draw_indexed_indirect_helper_args args = {
            .cmd = draw->info.indirect.buffer_dev_addr,
            .heap = panvk_priv_mem_dev_addr(dev->poly_heap.state),
            .index_buffer_ptr = draw->info.index.buffer_dev_addr,
            .index_min_max_res = index_min_max_res_ptr,
            .index_size = draw->info.index.index_size,
            .primitive_vertex_count =
               mesa_vertices_per_prim(draw->info.prim),
            .varying_bufs_descs = draw->varying_bufs,
            .attrib_bufs_descs = draw->vs.attribute_bufs,
            .attrib_bufs_infos = draw->indirect_info.attrib_bufs,
            .attrib_bufs_valid = attrib_bufs_valid,
            .attribs_valid = attribs_valid,
            .attribs_descs = draw->vs.attributes,
            .attribs_infos = draw->indirect_info.attribs,
            .first_vertex_sysval = first_vertex_sysval,
            .first_instance_sysval = first_instance_sysval,
            .raw_vertex_offset_sysval = raw_vertex_offset_sysval,
            .idvs_job = vs->info.vs.idvs ? draw->jobs.idvs.gpu : 0,
            .vertex_job = draw->jobs.vertex.gpu,
            /* With a geometry shader the tiler draws what the emulation
             * wrote, set up on the CPU.
             */
            .tiler_job = draw->gs_varying_bufs ? 0 : draw->jobs.tiler.gpu,
         };
         panlib_draw_indexed_indirect_helper_struct(&precomp_ctx, indirect_grid,
                                                    indirect_barrier, args);
      } else if (draw->info.indirect.buffer_dev_addr != 0) {
         const struct panlib_draw_indirect_helper_args args = {
            .cmd = draw->info.indirect.buffer_dev_addr,
            .primitive_vertex_count =
               mesa_vertices_per_prim(draw->info.prim),
            .heap = panvk_priv_mem_dev_addr(dev->poly_heap.state),
            .varying_bufs_descs = draw->varying_bufs,
            .attrib_bufs_descs = draw->vs.attribute_bufs,
            .attrib_bufs_infos = draw->indirect_info.attrib_bufs,
            .attrib_bufs_valid = attrib_bufs_valid,
            .attribs_valid = attribs_valid,
            .attribs_descs = draw->vs.attributes,
            .attribs_infos = draw->indirect_info.attribs,
            .first_vertex_sysval = first_vertex_sysval,
            .first_instance_sysval = first_instance_sysval,
            .raw_vertex_offset_sysval = raw_vertex_offset_sysval,
            .idvs_job = vs->info.vs.idvs ? draw->jobs.idvs.gpu : 0,
            .vertex_job = draw->jobs.vertex.gpu,
            .tiler_job = draw->gs_varying_bufs ? 0 : draw->jobs.tiler.gpu,
         };
         panlib_draw_indirect_helper_struct(&precomp_ctx, indirect_grid,
                                            indirect_barrier, args);
      } else {
         assert(false && "Invalid indirect draw");
      }

      /* Grab the index of the indirect helper job */
      uint32_t prev_job = batch->vtc_jc.job_index;

      if (vs->info.vs.idvs) {
         pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_INDEXED_VERTEX, false,
                        false, 0, prev_job, &draw->jobs.idvs, false);
      } else {
         unsigned vjob_id =
            vs_job ? pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_VERTEX,
                                    false, true, 0, prev_job,
                                    &draw->jobs.vertex, false)
                   : prev_job;

         /* Tessellation: the tess control job instead of a tiler job, the
          * rest of the draw is recorded by panvk_tess_draw(). No batch split
          * and no dirty state reset in between.
          */
         if (draw->tess_phase == PANVK_TESS_PHASE_VS_TCS) {
            cmdbuf->state.gfx.tess.tcs_job =
               emit_tess_tcs_job(cmdbuf, draw, vjob_id);
            return;
         }

         /* Geometry shader emulation; it writes the tiler's index count,
          * so the tiler descriptor must not be prefetched.
          */
         const bool gs = draw->gs_varying_bufs != 0;
         if (gs) {
            vjob_id = emit_gs_job(cmdbuf, draw, vjob_id);
            if (!vjob_id)
               return;
            if (cmdbuf->state.gfx.gs.xfb_counts)
               emit_xfb_jobs(cmdbuf, vjob_id);
         }

         if (draw->jobs.tiler.gpu != 0)
            add_tiler_jobs(batch, draw, vjob_id, gs);
      }
   }

   /*
    * We split every ~1024 indirect draw.
    * This is here for multiple reasons:
    * - The heap offset need to be reset at some point to avoid going outside of
    * bounds.
    * - It is possible to always end up with timeouts for batches with 4k draws
    * (see "dEQP-VK.api.command_buffers.many_indirect_draws_on_secondary") At
    * the same time, because of how TLS works on Mali, we should not split too
    * much as this will cause the TLS budget to go crazy.
    */
   if (batch->vtc_jc.job_index > (5 * 1024)) {
      panvk_per_arch(cmd_close_batch)(cmdbuf);
      batch = panvk_per_arch(cmd_open_batch)(cmdbuf);
      cmdbuf->state.uses_poly_heap = false;
   }

   clear_dirty_after_draw(cmdbuf);
}

/* Job slots a tessellated draw may take in the batch, see panvk_tess_draw():
 * the vertex shader draw and the tess control job, the tessellator kernels,
 * and per layer the tess eval draw with its geometry emulation and transform
 * feedback jobs.
 */
static uint32_t
panvk_tess_draw_jobs(uint32_t layers)
{
   return 32 + 24 * layers;
}

/*
 * Tessellation on v6/v7 (jm-v7 014). Mali has no tessellation stage, so a
 * tessellated draw becomes, in one job chain:
 *
 * 1. the vertex shader over the API draw (compiled for clip space), with the
 *    tess control job after it instead of a tiler job (emit_tess_tcs_job());
 * 2. the libpoly tessellator: index counts per patch, an inclusive prefix
 *    sum that keeps the whole patches that fit, then the indices;
 * 3. an indirect draw of the tessellator indices with the tess eval shader as
 *    its vertex shader, the draw command written by the prefix sum. A
 *    geometry shader or transform feedback after it goes through the
 *    geometry emulation of that draw, layers and views are its own.
 *
 * Every kernel is a barrier job: they share the device tess arena with the
 * previous tessellated draw. No CPU readback, direct and indirect draws take
 * the same path.
 *
 * Limits: at most max_patches patches (whole instances, or the first patches
 * of the first instance), PANVK_TESS_MAX_INDICES indices, what the indirect
 * varying buffer still holds, and PANVK_TESS_GS_MAX_PRIMS primitives into a
 * geometry shader or transform feedback. Patches past them are dropped.
 */
static void
panvk_tess_draw(struct panvk_cmd_buffer *cmdbuf,
                const struct panvk_draw_data *draw)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_graphics_state *gfx = &cmdbuf->state.gfx;
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct panvk_shader *vs = gfx->vs.shader;
   const struct panvk_shader *tcs = gfx->tcs.shader;
   const struct panvk_shader *tes = gfx->tes.shader;

   if (!vs || !tcs || !tes || !dev->tess_arena) {
      mesa_logw_once("panvk: patch list draw without tessellation shaders");
      return;
   }

   const struct panvk_shader_variant *tcs_v = panvk_shader_only_variant(tcs);
   const struct panvk_shader_variant *tes_v = panvk_shader_hw_variant(tes);
   const struct panvk_tess_info *tc = &tcs_v->gs.tess;
   const struct panvk_tess_info *te = &tes_v->gs.tess;
   const uint32_t domain =
      te->domain != PANVK_TESS_UNSET ? te->domain : tc->domain;
   const uint32_t spacing =
      te->spacing != PANVK_TESS_UNSET ? te->spacing : tc->spacing;
   const bool points = te->point_mode || tc->point_mode;
   const bool ccw =
      (te->ccw || tc->ccw) !=
      (dyns->ts.domain_origin == VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT);
   const uint32_t ps = MAX2(dyns->ts.patch_control_points, 1);

   if (domain == PANVK_TESS_UNSET || spacing == PANVK_TESS_UNSET ||
       !tc->out_patch_size) {
      mesa_logw_once("panvk: tessellation state incomplete, draw skipped");
      return;
   }

   const bool indirect = is_indirect_draw(draw);
   /* What the CPU knows of the draw: vkCmdDraw and vkCmdDrawIndexed. */
   const bool cpu_known = !indirect || draw->cpu_count;
   const uint32_t cpu_vertices =
      draw->cpu_count ? draw->cpu_count : draw->info.vertex.count;
   const uint32_t cpu_instances =
      draw->cpu_count ? draw->cpu_instances : draw->info.instance.count;
   if (cpu_known && (cpu_vertices < ps || !cpu_instances))
      return;

   const enum mesa_prim out_prim = points        ? MESA_PRIM_POINTS
                                   : domain == 2 ? MESA_PRIM_LINES
                                                 : MESA_PRIM_TRIANGLES;
   const uint32_t vpp = mesa_vertices_per_prim(out_prim);

   /* A geometry shader or transform feedback after the tess eval shader:
    * the geometry emulation of the tess eval draw is sized for
    * PANVK_TESS_GS_MAX_PRIMS input primitives, keep no more indices.
    */
   const bool nested = gfx->gs.shader || tes_v->gs.vs_clip_space ||
                       v7_polygon_mode_draw(cmdbuf, out_prim);
   uint32_t max_indices = PANVK_TESS_MAX_INDICES;
   if (nested)
      max_indices = MIN2(max_indices, PANVK_TESS_GS_MAX_PRIMS * vpp);

   /* The tess arena: tess control output, counts and coordinate offsets per
    * patch, the domain point heap and the index buffer. Every kept patch has
    * at least as many indices as domain points, and an allocation is padded
    * to 16 bytes, so the heap cannot overflow (poly_heap_alloc_offs() aborts
    * on overflow).
    */
   const uint64_t tcs_stride_B = (uint64_t)tc->stride_el * 4;
   const uint64_t heap_fixed_B =
      (uint64_t)max_indices * sizeof(struct poly_tess_point) + 64;
   const uint64_t fixed_B =
      heap_fixed_B + (uint64_t)max_indices * sizeof(uint32_t) + 5 * 64;
   const uint64_t per_patch_B =
      tcs_stride_B + 2 * sizeof(uint32_t) + 32 /* heap padding */;
   const uint32_t max_patches = MIN2(
      PANVK_TESS_MAX_PATCHES, (PANVK_TESS_HEAP_SIZE - fixed_B) / per_patch_B);
   if (!max_patches) {
      mesa_logw_once("panvk: tess control output too large, draw skipped");
      return;
   }

   if (cpu_known && (uint64_t)(cpu_vertices / ps) * cpu_instances > max_patches)
      mesa_logw_once("panvk: tessellated draw of %" PRIu64
                     " patches, only %u are drawn",
                     (uint64_t)(cpu_vertices / ps) * cpu_instances,
                     max_patches);

   const uint64_t arena = dev->tess_arena->addr.dev;
   uint64_t off = 0;
   const uint64_t l_tcs = arena + off;
   off += ALIGN_POT((uint64_t)max_patches * tcs_stride_B, 64);
   const uint64_t l_counts = arena + off;
   off += ALIGN_POT((uint64_t)max_patches * sizeof(uint32_t), 64);
   const uint64_t l_coord_allocs = arena + off;
   off += ALIGN_POT((uint64_t)max_patches * sizeof(uint32_t), 64);
   const uint64_t l_heap = arena + off;
   const uint32_t heap_size = heap_fixed_B + (uint64_t)max_patches * 32;
   off += ALIGN_POT((uint64_t)heap_size, 64);
   const uint64_t l_index = arena + off;
   off += (uint64_t)max_indices * sizeof(uint32_t);
   assert(off <= PANVK_TESS_HEAP_SIZE);

   /* Every job of the draw goes in this batch: the tess eval draw depends on
    * the tessellator by job index. vkCmdDrawIndirectByteCountEXT reserved
    * the room already (its command job is a dependency).
    */
   uint32_t view_mask = gfx->render.view_mask;
   const uint32_t layers =
      view_mask ? util_bitcount(view_mask) : gfx->render.layer_count;
   if (!cmdbuf->cur_batch)
      panvk_per_arch(cmd_open_batch)(cmdbuf);
   if (!draw->indirect_dep_job &&
       cmdbuf->cur_batch->vtc_jc.job_index + panvk_tess_draw_jobs(layers) >=
          UINT16_MAX) {
      panvk_per_arch(cmd_close_batch)(cmdbuf);
      panvk_per_arch(cmd_open_batch)(cmdbuf);
   }

   struct pan_ptr td =
      panvk_cmd_alloc_dev_mem(cmdbuf, desc, sizeof(struct panlib_v7_tess), 16);
   struct pan_ptr direct = {0};
   if (!indirect)
      direct = panvk_cmd_alloc_dev_mem(cmdbuf, desc,
                                       sizeof(VkDrawIndirectCommand), 16);
   if (!td.gpu || (!indirect && !direct.gpu) ||
       panvk_draw_get_indirect_varying_info(cmdbuf) != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmdbuf->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      return;
   }

#define TD(f) (td.gpu + offsetof(struct panlib_v7_tess, f))
   struct panlib_v7_tess *t = td.cpu;
   memset(t, 0, sizeof(*t));
   t->p = (struct poly_tess_params){
      .heap = TD(heap),
      .patch_coord_buffer = l_heap,
      .coord_allocs = l_coord_allocs,
      .tcs_buffer = l_tcs,
      .counts = l_counts,
      .index_buffer = l_index,
      .tcs_per_vertex_outputs = tc->per_vertex_outputs,
      .input_patch_size = ps,
      .output_patch_size = tc->out_patch_size,
      .tcs_patch_constants = tc->patch_constants,
      .tcs_stride_el = tc->stride_el,
      .partitioning = spacing,
      .points_mode = points,
      .isolines = domain == 2,
      .ccw = ccw,
   };
   t->heap.base = l_heap;
   t->heap.size = heap_size;
   t->input_patch_size = ps;
   t->out_patch_size = tc->out_patch_size;
   t->max_patches = max_patches;
   t->max_indices = max_indices;

   /* The tess eval draw takes 16 bytes of position, the point size and the
    * generic varyings per vertex and layer from the indirect varying buffer
    * (panlib_patch_varying_bufs()), each allocation padded to 64 bytes.
    */
   t->varying_info = gfx->vs.indirect_varying_bufs_infos;
   t->varying_bytes =
      layers * (16 + 4 + tes_v->info.varyings.formats.generic_size_B);
   t->varying_margin = layers * 3 * 64 + (1u << 20);

   if (!indirect) {
      *(VkDrawIndirectCommand *)direct.cpu = (VkDrawIndirectCommand){
         .vertexCount = draw->info.vertex.count,
         .instanceCount = draw->info.instance.count,
         .firstVertex = draw->info.vertex.base,
         .firstInstance = draw->info.instance.base,
      };
   }

   /* 1. The vertex shader and the tess control job. */
   gfx->tess.td = td.gpu;
   gfx->tess.src_draw =
      indirect ? draw->info.indirect.buffer_dev_addr : direct.gpu;
   gfx->tess.tcs_job = 0;
   gfx->tess.phase = PANVK_TESS_PHASE_VS_TCS;
   gfx_state_set_dirty(cmdbuf, VS);
   gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);

   struct panvk_draw_data vs_draw = *draw;
   vs_draw.tess_phase = PANVK_TESS_PHASE_VS_TCS;
   if (indirect)
      panvk_cmd_draw_indirect(cmdbuf, &vs_draw);
   else
      panvk_cmd_draw(cmdbuf, &vs_draw);

   gfx->tess.phase = PANVK_TESS_PHASE_NONE;
   /* The next draw relinks and prepares the vertex shader again. */
   gfx_state_set_dirty(cmdbuf, VS);
   gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
   if (!gfx->tess.tcs_job) {
      vk_command_buffer_set_error(&cmdbuf->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      return;
   }

   /* 2. The tessellator. */
   struct panvk_precomp_ctx precomp_ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   const struct panlib_precomp_grid tess_grid =
      panlib_1d(DIV_ROUND_UP(max_patches, 64));
   panlib_v7_tessellate(&precomp_ctx, tess_grid, PANLIB_BARRIER_JM_BARRIER,
                        TD(p), domain, POLY_TESS_MODE_COUNT);
   panlib_v7_tess_prefix(&precomp_ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER,
                         td.gpu);
   panlib_v7_tessellate(&precomp_ctx, tess_grid, PANLIB_BARRIER_JM_BARRIER,
                        TD(p), domain, POLY_TESS_MODE_WITH_COUNTS);
   const unsigned tess_job = cmdbuf->cur_batch->vtc_jc.job_index;

   /* 3. The tess eval shader as the vertex shader of the generated draw. */
   gfx->vs.shader = tes;
   gfx->sysvals.tess_params = TD(p);
   struct panvk_draw_data out = {
      .info = {
         .indirect.buffer_dev_addr = TD(draw),
         .indirect.draw_count = 1,
         .indirect.stride = sizeof(VkDrawIndirectCommand),
         .prim = out_prim,
      },
      .indirect_dep_job = tess_job,
      .tess_phase = PANVK_TESS_PHASE_TES,
      .gs_max_prims = nested ? max_indices / vpp : 0,
   };
   panvk_cmd_draw_indirect(cmdbuf, &out);
#undef TD

   gfx->vs.shader = vs;
   gfx_state_set_dirty(cmdbuf, VS);
   gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
}

static unsigned
padded_vertex_count(struct panvk_cmd_buffer *cmdbuf, uint32_t vertex_count,
                    uint32_t instance_count)
{
   if (instance_count == 1)
      return vertex_count;

   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);
   bool idvs = vs->info.vs.idvs;

   /* Index-Driven Vertex Shading requires different instances to
    * have different cache lines for position results. Each vertex
    * position is 16 bytes and the Mali cache line is 64 bytes, so
    * the instance count must be aligned to 4 vertices.
    */
   if (idvs)
      vertex_count = ALIGN_POT(vertex_count, 4);

   return pan_padded_vertex_count(vertex_count);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDraw)(VkCommandBuffer commandBuffer, uint32_t vertexCount,
                        uint32_t instanceCount, uint32_t firstVertex,
                        uint32_t firstInstance)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   if (instanceCount == 0 || vertexCount == 0)
      return;

   /* gl_BaseVertexARB is a signed integer, and it should expose the value of
    * firstVertex in a non-indexed draw. */
   assert(firstVertex < INT32_MAX);

   /* gl_BaseInstance is a signed integer, and it should expose the value of
    * firstInstnace. */
   assert(firstInstance < INT32_MAX);

   struct panvk_draw_data draw = {
      .info = {
         .vertex.base = firstVertex,
         .vertex.raw_offset = firstVertex,
         .vertex.count = vertexCount,
         .instance.base = firstInstance,
         .instance.count = instanceCount,
         .prim = panvk_get_client_prim(cmdbuf),
      },
      .vertex_range = vertexCount,
      .padded_vertex_count =
         padded_vertex_count(cmdbuf, vertexCount, instanceCount),
   };

   panvk_cmd_draw(cmdbuf, &draw);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndexed)(VkCommandBuffer commandBuffer,
                               uint32_t indexCount, uint32_t instanceCount,
                               uint32_t firstIndex, int32_t vertexOffset,
                               uint32_t firstInstance)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   if (instanceCount == 0 || indexCount == 0)
      return;

   /* gl_BaseInstance is a signed integer, and it should expose the value of
    * firstInstnace. */
   assert(firstInstance < INT32_MAX);

   struct pan_ptr indirect_index_alloc = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(struct VkDrawIndexedIndirectCommand), 8);

   struct VkDrawIndexedIndirectCommand *indirect_index_alloc_ptr =
      indirect_index_alloc.cpu;

   *indirect_index_alloc_ptr = (struct VkDrawIndexedIndirectCommand){
      .indexCount = indexCount,
      .instanceCount = instanceCount,
      .firstIndex = firstIndex,
      .vertexOffset = vertexOffset,
      .firstInstance = firstInstance,
   };

   struct panvk_draw_data draw = {
      .info = {
         .index = panvk_draw_info_index(cmdbuf, 0),
         .indirect.buffer_dev_addr = indirect_index_alloc.gpu,
         .indirect.draw_count = 1,
         .indirect.stride = 0,
         .prim = panvk_get_client_prim(cmdbuf),
      },
      .cpu_count = indexCount,
      .cpu_instances = instanceCount,
   };

   panvk_cmd_draw_indirect(cmdbuf, &draw);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndirect)(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                                VkDeviceSize offset, uint32_t drawCount,
                                uint32_t stride)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_buffer, buffer, _buffer);

   /* The job manager takes one indirect draw at a time: multiDrawIndirect
    * is drawCount of them, each reading its own command.
    */
   for (uint32_t i = 0; i < drawCount; i++) {
      struct panvk_draw_data draw = {
         .info = {
            .indirect.buffer_dev_addr =
               panvk_buffer_gpu_ptr(buffer, offset + (VkDeviceSize)i * stride),
            .indirect.draw_count = 1,
            .indirect.stride = stride,
            .prim = panvk_get_client_prim(cmdbuf),
         },
      };

      panvk_cmd_draw_indirect(cmdbuf, &draw);
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndexedIndirect)(VkCommandBuffer commandBuffer,
                                       VkBuffer _buffer, VkDeviceSize offset,
                                       uint32_t drawCount, uint32_t stride)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_buffer, buffer, _buffer);

   /* Because we don't currently advertise nullDescriptor for JM, it is only
    * valid to draw with a null index buffer if the draw accesses 0 indices.
    * For direct draws, this is covered by checks on instancedCount and
    * indexCount. For indirect draws we need to add an additional check, under
    * the assumption that if the index buffer is null, the draw must be empty.
    */
   if (drawCount == 0 || cmdbuf->state.gfx.ib.size == 0)
      return;

   /* One indirect draw at a time, as in CmdDrawIndirect. */
   for (uint32_t i = 0; i < drawCount; i++) {
      struct panvk_draw_data draw = {
         .info = {
            .index = panvk_draw_info_index(cmdbuf, 0),
            .indirect.buffer_dev_addr =
               panvk_buffer_gpu_ptr(buffer, offset + (VkDeviceSize)i * stride),
            .indirect.draw_count = 1,
            .indirect.stride = stride,
            .prim = panvk_get_client_prim(cmdbuf),
         },
      };

      panvk_cmd_draw_indirect(cmdbuf, &draw);
   }
}

/* VK_EXT_transform_feedback, captured from the geometry shader emulation. */
VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBindTransformFeedbackBuffersEXT)(
   VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount,
   const VkBuffer *pBuffers, const VkDeviceSize *pOffsets,
   const VkDeviceSize *pSizes)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   for (uint32_t i = 0; i < bindingCount && firstBinding + i < 4; i++) {
      VK_FROM_HANDLE(panvk_buffer, buffer, pBuffers[i]);
      uint64_t size = vk_buffer_range(&buffer->vk, pOffsets[i],
                                      pSizes ? pSizes[i] : VK_WHOLE_SIZE);

      cmdbuf->state.gfx.xfb.buffers[firstBinding + i].addr =
         panvk_buffer_gpu_ptr(buffer, pOffsets[i]);
      cmdbuf->state.gfx.xfb.buffers[firstBinding + i].size =
         MIN2(size, UINT32_MAX);
   }
}

/* Load the write offsets from the counter buffers (Begin) or store them
 * there (End). Without a counter Begin starts at 0, written by the CPU.
 */
static void
xfb_counters(struct panvk_cmd_buffer *cmdbuf, uint32_t first, uint32_t count,
             const VkBuffer *buffers, const VkDeviceSize *offsets, bool load)
{
   uint64_t c[4] = {0};
   for (uint32_t i = 0; buffers && i < count && first + i < 4; i++) {
      VK_FROM_HANDLE(panvk_buffer, buffer, buffers[i]);
      if (buffer)
         c[first + i] = panvk_buffer_gpu_ptr(buffer, offsets ? offsets[i] : 0);
   }
   const bool any = c[0] || c[1] || c[2] || c[3];

   if (load) {
      struct pan_ptr slot = panvk_cmd_alloc_dev_mem(cmdbuf, desc, 16, 16);
      if (!slot.gpu)
         return;
      memset(slot.cpu, 0, 16);
      cmdbuf->state.gfx.xfb.offsets = slot.gpu;
   }
   if (!any || !cmdbuf->state.gfx.xfb.offsets)
      return;

   struct panvk_precomp_ctx precomp_ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   const struct panlib_xfb_counters_args args = {
      .offsets = cmdbuf->state.gfx.xfb.offsets,
      .c0 = c[0],
      .c1 = c[1],
      .c2 = c[2],
      .c3 = c[3],
      .load = load,
   };
   panlib_xfb_counters_struct(
      &precomp_ctx, panlib_1d_with_jm_deps(1, 0, cmdbuf->state.gfx.xfb.last_job),
      PANLIB_BARRIER_JM_BARRIER, args);
   cmdbuf->state.gfx.xfb.last_job = cmdbuf->cur_batch->vtc_jc.job_index;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBeginTransformFeedbackEXT)(
   VkCommandBuffer commandBuffer, uint32_t firstCounterBuffer,
   uint32_t counterBufferCount, const VkBuffer *pCounterBuffers,
   const VkDeviceSize *pCounterBufferOffsets)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   xfb_counters(cmdbuf, firstCounterBuffer, counterBufferCount,
                pCounterBuffers, pCounterBufferOffsets, true);
   cmdbuf->state.gfx.xfb.active = true;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdEndTransformFeedbackEXT)(
   VkCommandBuffer commandBuffer, uint32_t firstCounterBuffer,
   uint32_t counterBufferCount, const VkBuffer *pCounterBuffers,
   const VkDeviceSize *pCounterBufferOffsets)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   xfb_counters(cmdbuf, firstCounterBuffer, counterBufferCount,
                pCounterBuffers, pCounterBufferOffsets, false);
   cmdbuf->state.gfx.xfb.active = false;
}

/* transformFeedbackDraw: a one-thread job turns the counter into a
 * VkDrawIndirectCommand, then it is an ordinary indirect draw whose helper
 * waits for that job.
 */
VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndirectByteCountEXT)(
   VkCommandBuffer commandBuffer, uint32_t instanceCount,
   uint32_t firstInstance, VkBuffer counterBuffer,
   VkDeviceSize counterBufferOffset, uint32_t counterOffset,
   uint32_t vertexStride)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_buffer, counter, counterBuffer);

   if (!instanceCount)
      return;

   struct pan_ptr cmd = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(VkDrawIndirectCommand), 16);
   if (!cmd.gpu)
      return;

   /* The draw may open a new batch (job index overflow), which would leave
    * this job behind in the old one: open it first.
    */
   if (!cmdbuf->cur_batch)
      panvk_per_arch(cmd_open_batch)(cmdbuf);
   const uint32_t draw_jobs =
      panvk_get_client_prim(cmdbuf) == MESA_PRIM_PATCHES
         ? panvk_tess_draw_jobs(cmdbuf->state.gfx.render.layer_count)
         : 4 * cmdbuf->state.gfx.render.layer_count;
   if (cmdbuf->cur_batch->vtc_jc.job_index + draw_jobs + 1 >= UINT16_MAX) {
      panvk_per_arch(cmd_close_batch)(cmdbuf);
      panvk_per_arch(cmd_open_batch)(cmdbuf);
   }

   /* A barrier: the counter is written by an earlier End in this chain. */
   struct panvk_precomp_ctx precomp_ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   const struct panlib_xfb_byte_count_draw_args args = {
      .counter = panvk_buffer_gpu_ptr(counter, counterBufferOffset),
      .counter_offset = counterOffset,
      .stride = vertexStride,
      .instance_count = instanceCount,
      .first_instance = firstInstance,
      .draw = cmd.gpu,
   };
   panlib_xfb_byte_count_draw_struct(&precomp_ctx, panlib_1d(1),
                                     PANLIB_BARRIER_JM_BARRIER, args);

   struct panvk_draw_data draw = {
      .info = {
         .indirect.buffer_dev_addr = cmd.gpu,
         .indirect.draw_count = 1,
         .indirect.stride = sizeof(VkDrawIndirectCommand),
         .prim = panvk_get_client_prim(cmdbuf),
      },
      .indirect_dep_job = cmdbuf->cur_batch->vtc_jc.job_index,
   };

   panvk_cmd_draw_indirect(cmdbuf, &draw);
}
#else /* PAN_ARCH == 9 */

static enum mali_draw_mode
v9_translate_prim(enum mesa_prim prim)
{
   switch (prim) {
   case MESA_PRIM_POINTS:                   return MALI_DRAW_MODE_POINTS;
   case MESA_PRIM_LINES:                    return MALI_DRAW_MODE_LINES;
   case MESA_PRIM_LINE_STRIP:               return MALI_DRAW_MODE_LINE_STRIP;
   case MESA_PRIM_TRIANGLES:                return MALI_DRAW_MODE_TRIANGLES;
   case MESA_PRIM_TRIANGLE_STRIP:           return MALI_DRAW_MODE_TRIANGLE_STRIP;
   case MESA_PRIM_TRIANGLE_FAN:             return MALI_DRAW_MODE_TRIANGLE_FAN;
   case MESA_PRIM_LINES_ADJACENCY:          return MALI_DRAW_MODE_LINES_ADJACENCY;
   case MESA_PRIM_LINE_STRIP_ADJACENCY:     return MALI_DRAW_MODE_LINE_STRIP_ADJACENCY;
   case MESA_PRIM_TRIANGLES_ADJACENCY:      return MALI_DRAW_MODE_TRIANGLES_ADJACENCY;
   case MESA_PRIM_TRIANGLE_STRIP_ADJACENCY: return MALI_DRAW_MODE_TRIANGLE_STRIP_ADJACENCY;
   default:
      UNREACHABLE("Invalid primitive type");
   }
}

static enum mali_func
v9_translate_compare_func(VkCompareOp comp)
{
   return (enum mali_func)comp;
}

static enum mali_stencil_op
v9_translate_stencil_op(VkStencilOp in)
{
   switch (in) {
   case VK_STENCIL_OP_KEEP:                return MALI_STENCIL_OP_KEEP;
   case VK_STENCIL_OP_ZERO:                return MALI_STENCIL_OP_ZERO;
   case VK_STENCIL_OP_REPLACE:             return MALI_STENCIL_OP_REPLACE;
   case VK_STENCIL_OP_INCREMENT_AND_CLAMP: return MALI_STENCIL_OP_INCR_SAT;
   case VK_STENCIL_OP_DECREMENT_AND_CLAMP: return MALI_STENCIL_OP_DECR_SAT;
   case VK_STENCIL_OP_INCREMENT_AND_WRAP:  return MALI_STENCIL_OP_INCR_WRAP;
   case VK_STENCIL_OP_DECREMENT_AND_WRAP:  return MALI_STENCIL_OP_DECR_WRAP;
   case VK_STENCIL_OP_INVERT:              return MALI_STENCIL_OP_INVERT;
   default:
      UNREACHABLE("Invalid stencil op");
   }
}

static void
v9_emit_vs_attrib(struct panvk_cmd_buffer *cmdbuf, uint32_t attrib_idx,
                  uint32_t vb_desc_offset, struct mali_attribute_packed *desc,
                  uint32_t base_instance)
{
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_vertex_input_state *vi = dyns->vi;
   const struct vk_vertex_attribute_state *attrib_info = &vi->attributes[attrib_idx];
   const struct vk_vertex_binding_state *buf_info = &vi->bindings[attrib_info->binding];
   const uint32_t stride = dyns->vi_binding_strides[attrib_info->binding];
   bool per_instance = buf_info->input_rate == VK_VERTEX_INPUT_RATE_INSTANCE;
   enum pipe_format f = vk_format_to_pipe_format(attrib_info->format);

   pan_pack(desc, ATTRIBUTE, cfg) {
      /* Valhall fetches per-instance attributes with the 0-based instance
       * (divided by the divisor), so fold firstInstance into the attribute
       * offset: element = firstInstance + (instance - firstInstance) / div,
       * also for divisor 0. Same as CSF emit_vs_attrib(). */
      cfg.offset = attrib_info->offset +
                   (per_instance ? base_instance * stride : 0);
      cfg.format = GENX(pan_format_from_pipe_format)(f)->hw;
      cfg.table = 0;
      cfg.buffer_index = vb_desc_offset + attrib_info->binding;
      cfg.stride = stride;
      if (!per_instance) {
         cfg.attribute_type = MALI_ATTRIBUTE_TYPE_1D;
         cfg.frequency = MALI_ATTRIBUTE_FREQUENCY_VERTEX;
         cfg.offset_enable = true;
      } else if (buf_info->divisor <= 1) {
         cfg.attribute_type = MALI_ATTRIBUTE_TYPE_1D;
         cfg.frequency = MALI_ATTRIBUTE_FREQUENCY_INSTANCE;
         if (buf_info->divisor == 0)
            cfg.stride = 0;
      } else if (util_is_power_of_two_or_zero(buf_info->divisor)) {
         cfg.attribute_type = MALI_ATTRIBUTE_TYPE_1D_POT_DIVISOR;
         cfg.frequency = MALI_ATTRIBUTE_FREQUENCY_INSTANCE;
         cfg.divisor_r = __builtin_ctz(buf_info->divisor);
      } else {
         cfg.attribute_type = MALI_ATTRIBUTE_TYPE_1D_NPOT_DIVISOR;
         cfg.frequency = MALI_ATTRIBUTE_FREQUENCY_INSTANCE;
         cfg.divisor_d = pan_compute_npot_divisor(buf_info->divisor,
                                                  &cfg.divisor_r, &cfg.divisor_e);
      }
   }
}

static VkResult
v9_prepare_vs_driver_set_ex(struct panvk_cmd_buffer *cmdbuf,
                            const struct panvk_shader *vs_shader,
                            struct panvk_shader_desc_state *st,
                            uint32_t base_instance)
{
   const struct panvk_shader_desc_info *vs_desc_info = &vs_shader->desc_info;
   const struct vk_vertex_input_state *vi =
      cmdbuf->vk.dynamic_graphics_state.vi;
   const struct panvk_descriptor_state *desc_state = &cmdbuf->state.gfx.desc_state;

   uint32_t vb_count = 0;
   u_foreach_bit(i, vi->attributes_valid)
      vb_count = MAX2(vi->attributes[i].binding + 1, vb_count);

   uint32_t vb_offset = vs_desc_info->dyn_bufs.count + MAX_VS_ATTRIBS + 1;
   uint32_t desc_count = vb_offset + vb_count;
   struct pan_ptr driver_set = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, desc_count * PANVK_DESCRIPTOR_SIZE, PANVK_DESCRIPTOR_SIZE);
   if (!driver_set.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   struct panvk_opaque_desc *descs = driver_set.cpu;
   for (uint32_t i = 0; i < MAX_VS_ATTRIBS; i++) {
      if (vi->attributes_valid & BITFIELD_BIT(i))
         v9_emit_vs_attrib(cmdbuf, i, vb_offset, (void *)&descs[i], base_instance);
      else
         pan_cast_and_pack(&descs[i], NULL_DESCRIPTOR, cfg);
   }

   pan_cast_and_pack(&descs[MAX_VS_ATTRIBS], SAMPLER, cfg) {
      cfg.clamp_integer_array_indices = false;
   }
   panvk_per_arch(cmd_fill_dyn_bufs)(desc_state, vs_desc_info,
      (struct mali_buffer_packed *)(&descs[MAX_VS_ATTRIBS + 1]));

   for (uint32_t i = 0; i < vb_count; i++) {
      const struct panvk_attrib_buf *vb = &cmdbuf->state.gfx.vb.bufs[i];
      if ((vi->bindings_valid & BITFIELD_BIT(i)) && vb->size) {
         pan_cast_and_pack(&descs[vb_offset + i], BUFFER, cfg) {
            cfg.address = vb->address;
            cfg.size = vb->size;
         }
      } else {
         pan_cast_and_pack(&descs[vb_offset + i], NULL_DESCRIPTOR, cfg);
      }
   }

   st->driver_set.dev_addr = driver_set.gpu;
   st->driver_set.size = desc_count * PANVK_DESCRIPTOR_SIZE;
   return VK_SUCCESS;
}

static VkResult
v9_prepare_vs_driver_set(struct panvk_cmd_buffer *cmdbuf, uint32_t base_instance)
{
   return v9_prepare_vs_driver_set_ex(cmdbuf, cmdbuf->state.gfx.vs.shader,
                                      &cmdbuf->state.gfx.vs.desc, base_instance);
}

/* Driver set of a stage without vertex attributes (FS, GS kernel): the
 * dummy sampler and the dynamic buffers. */
static VkResult
v9_prepare_stage_driver_set(struct panvk_cmd_buffer *cmdbuf,
                            const struct panvk_shader *shader,
                            struct panvk_shader_desc_state *st)
{
   const struct panvk_shader_desc_info *info = &shader->desc_info;
   uint32_t desc_count = info->dyn_bufs.count + 1;
   struct pan_ptr driver_set = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, desc_count * PANVK_DESCRIPTOR_SIZE, PANVK_DESCRIPTOR_SIZE);
   if (desc_count && !driver_set.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   struct panvk_opaque_desc *descs = driver_set.cpu;
   pan_cast_and_pack(&descs[0], SAMPLER, cfg) {
      cfg.clamp_integer_array_indices = false;
   }
   panvk_per_arch(cmd_fill_dyn_bufs)(&cmdbuf->state.gfx.desc_state, info,
      (struct mali_buffer_packed *)(&descs[1]));
   st->driver_set.dev_addr = driver_set.gpu;
   st->driver_set.size = desc_count * PANVK_DESCRIPTOR_SIZE;
   return VK_SUCCESS;
}

static VkResult
v9_prepare_fs_driver_set(struct panvk_cmd_buffer *cmdbuf)
{
   if (!cmdbuf->state.gfx.fs.shader)
      return VK_SUCCESS;

   return v9_prepare_stage_driver_set(cmdbuf, cmdbuf->state.gfx.fs.shader,
                                      &cmdbuf->state.gfx.fs.desc);
}

static VkResult
v9_prepare_blend_zsd(struct panvk_cmd_buffer *cmdbuf,
                     const struct panvk_shader_variant *fs,
                     uint64_t *blend_gpu, uint64_t *zsd_gpu)
{
   uint32_t bd_count = cmdbuf->state.gfx.render.fb.layout.rt_count;
   struct pan_ptr bptr = panvk_cmd_alloc_desc_array(cmdbuf, bd_count, BLEND);
   if (bd_count && !bptr.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   if (fs) {
      VkResult r = panvk_per_arch(blend_emit_descs)(cmdbuf, bptr.cpu);
      if (r != VK_SUCCESS)
         return r;
   } else {
      struct mali_blend_packed *bds = bptr.cpu;
      for (unsigned i = 0; i < bd_count; i++) {
         pan_pack(&bds[i], BLEND, cfg) {
            cfg.enable = false;
            cfg.internal.mode = MALI_BLEND_MODE_OFF;
         }
      }
   }
   *blend_gpu = bptr.gpu;

   const struct vk_dynamic_graphics_state *dyns = &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_depth_stencil_state *ds = &dyns->ds;
   const struct vk_rasterization_state *rs = &dyns->rs;
   bool test_z = (cmdbuf->state.gfx.render.bound_attachments &
                  MESA_VK_RP_ATTACHMENT_DEPTH_BIT) && ds->depth.test_enable;
   bool test_s = (cmdbuf->state.gfx.render.bound_attachments &
                  MESA_VK_RP_ATTACHMENT_STENCIL_BIT) && ds->stencil.test_enable;

   struct pan_ptr zsd = panvk_cmd_alloc_desc(cmdbuf, DEPTH_STENCIL);
   if (!zsd.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   pan_cast_and_pack(zsd.cpu, DEPTH_STENCIL, cfg) {
      cfg.stencil_test_enable = test_s;
      if (test_s) {
         cfg.front_compare_function = v9_translate_compare_func(ds->stencil.front.op.compare);
         cfg.front_stencil_fail = v9_translate_stencil_op(ds->stencil.front.op.fail);
         cfg.front_depth_fail = v9_translate_stencil_op(ds->stencil.front.op.depth_fail);
         cfg.front_depth_pass = v9_translate_stencil_op(ds->stencil.front.op.pass);
         cfg.back_compare_function = v9_translate_compare_func(ds->stencil.back.op.compare);
         cfg.back_stencil_fail = v9_translate_stencil_op(ds->stencil.back.op.fail);
         cfg.back_depth_fail = v9_translate_stencil_op(ds->stencil.back.op.depth_fail);
         cfg.back_depth_pass = v9_translate_stencil_op(ds->stencil.back.op.pass);
      }
      cfg.stencil_from_shader = fs ? fs->info.fs.writes_stencil : 0;
      cfg.front_write_mask = ds->stencil.front.write_mask;
      cfg.back_write_mask = ds->stencil.back.write_mask;
      cfg.front_value_mask = ds->stencil.front.compare_mask;
      cfg.back_value_mask = ds->stencil.back.compare_mask;
      cfg.front_reference_value = ds->stencil.front.reference;
      cfg.back_reference_value = ds->stencil.back.reference;
      cfg.depth_cull_enable = vk_rasterization_state_depth_clip_enable(rs);
      if (rs->depth_clamp_enable)
         cfg.depth_clamp_mode = MALI_DEPTH_CLAMP_MODE_BOUNDS;
      if (fs)
         cfg.depth_source = pan_depth_source(&fs->info);
      cfg.depth_write_enable = test_z && ds->depth.write_enable;
      cfg.depth_bias_enable = rs->depth_bias.enable;
      cfg.depth_function = test_z ? v9_translate_compare_func(ds->depth.compare_op)
                                  : MALI_FUNC_ALWAYS;
      cfg.depth_units = rs->depth_bias.constant_factor;
      cfg.depth_factor = rs->depth_bias.slope_factor;
      cfg.depth_bias_clamp = rs->depth_bias.clamp;
   }
   *zsd_gpu = zsd.gpu;
   return VK_SUCCESS;
}

/* A viewport-selecting shader with more than one viewport: its passthrough
 * does each primitive's viewport transform and scissor itself (clip planes),
 * the fixed function only clamps depth. */
static bool
v9_multi_viewport(const struct panvk_cmd_buffer *cmdbuf)
{
   return panvk_prerast_viewport_shader(&cmdbuf->state.gfx) &&
          cmdbuf->vk.dynamic_graphics_state.vp.viewport_count > 1;
}

/* Depth clamp range of one viewport, CSF panvk_viewport_depth_range(). */
static void
v9_viewport_depth_range(const struct panvk_cmd_buffer *cmdbuf, unsigned index,
                        float *z_min, float *z_max)
{
   const struct vk_viewport_state *vp = &cmdbuf->vk.dynamic_graphics_state.vp;

   if (vp->depth_clamp_mode == VK_DEPTH_CLAMP_MODE_USER_DEFINED_RANGE_EXT) {
      *z_min = vp->depth_clamp_range.minDepthClamp;
      *z_max = vp->depth_clamp_range.maxDepthClamp;
      return;
   }

   float sz, oz;
   panvk_viewport_z_coeffs(vp, &cmdbuf->vk.dynamic_graphics_state.rs, index,
                           &sz, &oz);
   float a = vp->depth_clip_negative_one_to_one ? oz - sz : oz;
   float b = oz + sz;
   *z_min = MIN2(a, b);
   *z_max = MAX2(a, b);
}

static void
v9_emit_scissor(struct panvk_cmd_buffer *cmdbuf, struct mali_scissor_packed *out)
{
   if (v9_multi_viewport(cmdbuf)) {
      /* The framebuffer: it cannot cut a primitive of another viewport. */
      uint32_t w = cmdbuf->state.gfx.render.fb.layout.width_px;
      uint32_t h = cmdbuf->state.gfx.render.fb.layout.height_px;

      w = w ? w : MAX_FRAMEBUFFER_DIMENSION;
      h = h ? h : MAX_FRAMEBUFFER_DIMENSION;
      pan_pack(out, SCISSOR, cfg) {
         cfg.scissor_minimum_x = 0;
         cfg.scissor_minimum_y = 0;
         cfg.scissor_maximum_x = MIN2(w - 1, (uint32_t)UINT16_MAX);
         cfg.scissor_maximum_y = MIN2(h - 1, (uint32_t)UINT16_MAX);
      }
      return;
   }

   const VkViewport *vp = &cmdbuf->vk.dynamic_graphics_state.vp.viewports[0];
   const VkRect2D *sc = &cmdbuf->vk.dynamic_graphics_state.vp.scissors[0];
   int minx = MAX2(sc->offset.x, (int)vp->x);
   int maxx = MIN2(sc->offset.x + (int)sc->extent.width, (int)(vp->x + vp->width));
   int miny = MAX2(sc->offset.y, (int)MIN2(vp->y, vp->y + vp->height));
   int maxy = MIN2(sc->offset.y + (int)sc->extent.height,
                   (int)MAX2(vp->y, vp->y + vp->height));
   maxx = maxx > minx ? maxx - 1 : maxx;
   maxy = maxy > miny ? maxy - 1 : maxy;
   pan_pack(out, SCISSOR, cfg) {
      cfg.scissor_minimum_x = CLAMP(minx, 0, UINT16_MAX);
      cfg.scissor_minimum_y = CLAMP(miny, 0, UINT16_MAX);
      cfg.scissor_maximum_x = CLAMP(maxx, 0, UINT16_MAX);
      cfg.scissor_maximum_y = CLAMP(maxy, 0, UINT16_MAX);
   }
}

/* PANVK_SKIP_FS workaround (default off): some fragment shaders that spill
 * registers run past the kernel job timeout (Far Cry 3). 1 = skip draws
 * whose fragment shader spills (tls > 0), 2 = also skip shaders using 64
 * registers. Skipped draws are culled, so those objects are missing. */
static bool
panvk_skip_fs(unsigned regs, unsigned tls)
{
   static int mode = -1;

   if (mode < 0) {
      const char *e = getenv("PANVK_SKIP_FS");
      mode = e ? atoi(e) : 0;
      if (mode)
         fprintf(stderr, "panvk: PANVK_SKIP_FS=%d\n", mode);
   }
   return (mode == 1 && tls > 0) || (mode == 2 && (tls > 0 || regs > 32));
}

/* Instance-rate attributes of an indirect draw: the patch kernels fold the
 * GPU-side firstInstance into their Offset (v9_emit_vs_attrib only saw the
 * placeholder 0). Returns false when out of memory. */
static bool
v9_inst_attr_info(struct panvk_cmd_buffer *cmdbuf, uint32_t *mask,
                  uint64_t *info_gpu, uint64_t *vs_attribs)
{
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_vertex_input_state *vi = dyns->vi;
   uint32_t info[MAX_VS_ATTRIBS][3] = {0};

   *mask = 0;
   *info_gpu = 0;
   *vs_attribs = 0;
   u_foreach_bit(a, vi->attributes_valid) {
      const uint32_t b = vi->attributes[a].binding;
      if (vi->bindings[b].input_rate != VK_VERTEX_INPUT_RATE_INSTANCE)
         continue;
      *mask |= BITFIELD_BIT(a);
      info[a][0] = vi->attributes[a].offset;
      info[a][1] = dyns->vi_binding_strides[b];
      info[a][2] = vi->bindings[b].divisor;
   }
   if (!*mask)
      return true;

   struct pan_ptr ii = panvk_cmd_alloc_dev_mem(cmdbuf, desc, sizeof(info), 4);
   if (!ii.gpu)
      return false;
   memcpy(ii.cpu, info, sizeof(info));
   *info_gpu = ii.gpu;
   *vs_attribs = cmdbuf->state.gfx.vs.desc.driver_set.dev_addr;
   return true;
}

static VkResult
v9_emit_malloc_vertex_job(struct panvk_cmd_buffer *cmdbuf,
                          const struct panvk_draw_info *info,
                          const struct panvk_shader_variant *vs,
                          const struct panvk_shader_variant *fs,
                          uint64_t blend_gpu, uint64_t zsd_gpu)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;
   struct pan_ptr job = panvk_cmd_alloc_desc(cmdbuf, MALLOC_VERTEX_JOB);
   /* Shaders that spill or use scratch (e.g. DXVK indexable temps) need
    * thread-local storage; the v9 path never sized it. */
   batch->tlsinfo.tls.size = MAX3(vs->info.tls_size,
                                  fs ? fs->info.tls_size : 0,
                                  batch->tlsinfo.tls.size);
   if (!job.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   util_dynarray_append(&batch->jobs, job.cpu);
   util_dynarray_append(&batch->malloc_vertex_jobs, job.cpu);

   const struct vk_dynamic_graphics_state *dyns = &cmdbuf->vk.dynamic_graphics_state;
   const struct vk_rasterization_state *rs = &dyns->rs;
   enum mesa_prim reduced = u_reduced_prim(info->prim);
   bool polygon = reduced == MESA_PRIM_TRIANGLES;
   float z_min, z_max;
   if (info->viewport_clamp)
      v9_viewport_depth_range(cmdbuf, info->viewport_clamp - 1, &z_min, &z_max);
   else
      panvk_depth_range(&cmdbuf->state.gfx, &dyns->vp, &z_min, &z_max);

   const bool secondary_shader = vs->info.vs.secondary_enable && fs != NULL;
   /* Use the real varying count from the linked vs/fs pair instead of a
    * vkcube-only hardcoded guess. Position is IDVS-side and not counted here,
    * so vs varying count (fs consumes a subset/same set) drives the stride. */
   const unsigned vary_slots = MAX2(vs->info.varyings.formats.count, 1);
   /* The compiler places varyings by layout offset, not densely by count:
    * use the layout's real byte size (as CSF does) so a vertex's varyings
    * never spill into the next vertex packet. Never smaller than before. */
   const unsigned vary_bytes =
      MAX2(vary_slots * 16, ALIGN_POT(vs->info.varyings.formats.generic_size_B, 16));
   const unsigned vary_stride = secondary_shader ? vary_bytes : 0;
   PANVK_TRACE_PRINTF(
           "[V9-VARY] secondary=%d slots=%u packet=%u attr=%u var_spd=%d\n",
           (int)secondary_shader, vary_slots,
           secondary_shader ? vary_stride + 16 : 16, vary_stride,
           (int)panvk_priv_mem_check_alloc(vs->spds.var));

   pan_section_pack(job.cpu, MALLOC_VERTEX_JOB, PRIMITIVE, cfg) {
      cfg.draw_mode = v9_translate_prim(info->prim);
      cfg.allow_rotating_primitives = polygon;
      cfg.primitive_restart = false;
      cfg.low_depth_cull = cfg.high_depth_cull =
         vk_rasterization_state_depth_clip_enable(rs);
      cfg.index_count = info->vertex.count;
      cfg.base_vertex_offset = info->vertex.base;
      if (info->index.index_size == 4)
         cfg.index_type = MALI_INDEX_TYPE_UINT32;
      else if (info->index.index_size == 2)
         cfg.index_type = MALI_INDEX_TYPE_UINT16;
      else if (info->index.index_size == 1)
         cfg.index_type = MALI_INDEX_TYPE_UINT8;
      else
         cfg.index_type = MALI_INDEX_TYPE_NONE;
      cfg.primitive_restart = info->index.restart_enable;
      cfg.secondary_shader = secondary_shader;
      /* gl_PrimitiveID (Noysz 0067, CSF): the HW primitive index for the
       * FS, overridden by the value the raster VS writes (GS, TES or
       * viewport passthrough). A written ID needs the override even when
       * the FS does not read it: without it the G57 drops every primitive
       * whose ID is not 0 (TES viewport-select draws of 2+ patches). */
      const bool writes_prim_id =
         (vs->info.outputs_written & VARYING_BIT_PRIMITIVE_ID) != 0;
      cfg.primitive_index_enable =
         writes_prim_id || (fs && fs->info.fs.reads_primitive_id);
      cfg.primitive_index_override = writes_prim_id;
   }
   pan_section_pack(job.cpu, MALLOC_VERTEX_JOB, INSTANCE_COUNT, cfg) {
      cfg.count = info->instance.count;
   }
   pan_section_pack(job.cpu, MALLOC_VERTEX_JOB, ALLOCATION, cfg) {
      cfg.vertex_packet_stride = secondary_shader ? (vary_stride + 16) : 16;
      cfg.vertex_attribute_stride = vary_stride;
   }
   pan_section_pack(job.cpu, MALLOC_VERTEX_JOB, TILER, cfg) {
      cfg.address = batch->tiler.ctx.valhall.desc;
   }
   v9_emit_scissor(cmdbuf, pan_section_ptr(job.cpu, MALLOC_VERTEX_JOB, SCISSOR));
   pan_section_pack(job.cpu, MALLOC_VERTEX_JOB, PRIMITIVE_SIZE, cfg) {
      cfg.fixed_sized = rs->line.width;
   }
   pan_section_pack(job.cpu, MALLOC_VERTEX_JOB, INDICES, cfg) {
      cfg.address = info->index.buffer_dev_addr
                    ? (info->index.buffer_dev_addr + info->index.offset)
                    : 0;
   }
   (void)0;

   static int tess_nocull = -1;
   if (tess_nocull < 0) {
      const char *e = getenv("PANVK_TESS_NOCULL");
      tess_nocull = e && e[0] == '1';
      if (tess_nocull)
         (void)0;
   }
   pan_section_pack(job.cpu, MALLOC_VERTEX_JOB, DRAW, cfg) {
      cfg.flags_0.cull_front_face = !tess_nocull && polygon && (rs->cull_mode & VK_CULL_MODE_FRONT_BIT);
      cfg.flags_0.cull_back_face = !tess_nocull && polygon && (rs->cull_mode & VK_CULL_MODE_BACK_BIT);
      cfg.flags_0.front_face_ccw =
         rs->front_face == VK_FRONT_FACE_COUNTER_CLOCKWISE;
      cfg.flags_0.multisample_enable = dyns->ms.rasterization_samples > 1;
      cfg.flags_1.sample_mask = cfg.flags_0.multisample_enable ? dyns->ms.sample_mask : 0xFFFF;
      cfg.flags_0.aligned_line_ends = rs->line.mode == VK_LINE_RASTERIZATION_MODE_BRESENHAM;
      cfg.vertex_array.packet = true;
      /* v58e: occlusion queries on the v9 MALLOC_VERTEX path */
      cfg.flags_0.occlusion_query = cmdbuf->state.gfx.occlusion_query.mode;
      cfg.occlusion = cmdbuf->state.gfx.occlusion_query.ptr;
      cfg.minimum_z = z_min;
      cfg.maximum_z = z_max;
      cfg.depth_stencil = zsd_gpu;
      cfg.blend = blend_gpu;
      cfg.blend_count = MAX2(cmdbuf->state.gfx.render.fb.layout.rt_count, 1);
      if (fs) {
         static int force_early = -1;
         if (force_early < 0) {
            const char *e = getenv("PANVK_FORCE_EARLYZS");
            force_early = e && e[0] == '1';
         }
         const bool late_zs = !force_early &&
            !fs->info.fs.early_fragment_tests &&
            (fs->info.fs.can_discard || fs->info.fs.writes_depth ||
             fs->info.fs.writes_stencil || fs->info.fs.writes_coverage ||
             dyns->ms.alpha_to_coverage_enable);
         if (panvk_skip_fs(fs->info.work_reg_count, fs->info.tls_size)) {
            cfg.flags_0.cull_front_face = true;
            cfg.flags_0.cull_back_face = true;
         }
         cfg.flags_0.pixel_kill_operation =
            late_zs ? MALI_PIXEL_KILL_FORCE_LATE : MALI_PIXEL_KILL_FORCE_EARLY;
         cfg.flags_0.zs_update_operation =
            late_zs ? MALI_PIXEL_KILL_FORCE_LATE : MALI_PIXEL_KILL_FORCE_EARLY;
         /* Depth bounds emulation (csf-v11 092/116): the lowered FS reads the
          * tile depth and clears SampleMask bits outside the bounds, which
          * already makes ZS late without EarlyFragmentTests. With them, an
          * early update would let the FS read its own depth and keep the
          * writes of samples outside the bounds: test early, update after
          * the FS. Not when the app FS discards, writes SampleMask or uses
          * alpha-to-coverage (those must not gate EFT ZS writes). Same
          * condition as the ds.enable sysval. */
         const bool db_on = fs->fs.depth_bounds &&
                            dyns->ds.depth.bounds_test.enable &&
                            (cmdbuf->state.gfx.render.bound_attachments &
                             MESA_VK_RP_ATTACHMENT_DEPTH_BIT);
         const bool writes_zs_or_oq =
            ((cmdbuf->state.gfx.render.bound_attachments &
              MESA_VK_RP_ATTACHMENT_DEPTH_BIT) &&
             dyns->ds.depth.test_enable && dyns->ds.depth.write_enable) ||
            ((cmdbuf->state.gfx.render.bound_attachments &
              MESA_VK_RP_ATTACHMENT_STENCIL_BIT) &&
             dyns->ds.stencil.test_enable) ||
            cmdbuf->state.gfx.occlusion_query.mode !=
               MALI_OCCLUSION_MODE_DISABLED;
         if (db_on && fs->info.fs.early_fragment_tests && writes_zs_or_oq &&
             !fs->info.fs.can_discard && !fs->fs.depth_bounds_app_mask &&
             !dyns->ms.alpha_to_coverage_enable)
            cfg.flags_0.zs_update_operation = MALI_PIXEL_KILL_FORCE_LATE;
         /* Per-sample shading (sampleShadingEnable, gl_SampleID and friends),
          * and with MSAA for a blend shader, which writes one sample at a
          * time: CSF build_dcd_flags(). After Noysz/panvk-g99-jm 0059. */
         cfg.flags_0.evaluate_per_sample =
            (fs->info.fs.sample_shading ||
             cmdbuf->state.gfx.cb.info.needs_shader) &&
            dyns->ms.rasterization_samples > 1;
         cfg.flags_0.alpha_to_coverage = dyns->ms.alpha_to_coverage_enable;
         /* the GPU must wait for the shader's coverage (discard,
          * sample mask, alpha-to-coverage) before updating depth/stencil. */
         cfg.flags_0.shader_modifies_coverage = !v61a_no_smc() &&
            (fs->info.fs.can_discard || fs->info.fs.writes_coverage ||
             dyns->ms.alpha_to_coverage_enable);
         /* FPK would discard what lies underneath; only allowed when blending
          * doesn't read the destination (and no alpha-to-coverage). */
         cfg.flags_0.allow_forward_pixel_to_kill =
            fs->info.fs.can_fpk &&
            !cmdbuf->state.gfx.cb.info.any_dest_read &&
            !dyns->ms.alpha_to_coverage_enable;
         cfg.flags_0.allow_forward_pixel_to_be_killed = !fs->info.writes_global;
         cfg.flags_1.render_target_mask =
            color_attachment_written_mask(fs, &cmdbuf->vk.dynamic_graphics_state.cal);
         cfg.shader.resources = cmdbuf->state.gfx.fs.desc.res_table;
         cfg.shader.shader = panvk_priv_mem_dev_addr(fs->spd);
         cfg.shader.thread_storage = batch->tls.gpu;
         cfg.shader.fau = cmdbuf->state.gfx.fs.push_uniforms;
         cfg.shader.fau_count = fs->fau.total_count;
      } else {
         cfg.flags_0.pixel_kill_operation = MALI_PIXEL_KILL_FORCE_EARLY;
         cfg.flags_0.zs_update_operation = MALI_PIXEL_KILL_FORCE_EARLY;
         cfg.flags_0.allow_forward_pixel_to_kill = true;
         cfg.flags_0.allow_forward_pixel_to_be_killed = true;
      }
   }

   const bool points = info->prim == MESA_PRIM_POINTS;
   pan_section_pack(job.cpu, MALLOC_VERTEX_JOB, POSITION, cfg) {
      cfg.resources = cmdbuf->state.gfx.vs.desc.res_table;
      cfg.thread_storage = batch->tls.gpu;
      cfg.shader = points ? panvk_priv_mem_dev_addr(vs->spds.pos_points)
                          : panvk_priv_mem_dev_addr(vs->spds.pos_triangles);
      cfg.fau = cmdbuf->state.gfx.vs.push_uniforms;
      cfg.fau_count = vs->fau.total_count;
   }

   (void)0;
   pan_section_pack(job.cpu, MALLOC_VERTEX_JOB, VARYING, cfg) {
      if (panvk_priv_mem_check_alloc(vs->spds.var)) {
         cfg.resources = cmdbuf->state.gfx.vs.desc.res_table;
         cfg.thread_storage = batch->tls.gpu;
         cfg.shader = panvk_priv_mem_dev_addr(vs->spds.var);
         cfg.fau = cmdbuf->state.gfx.vs.push_uniforms;
         cfg.fau_count = vs->fau.total_count;
      } else {
         cfg.resources = 0;
         cfg.thread_storage = 0;
         cfg.shader = 0;
         cfg.fau = 0;
         cfg.fau_count = 0;
      }
   }

   unsigned indirect_dep = 0;
   if (info->indirect.buffer_dev_addr) {
      uint64_t fv_ptr = 0, bi_ptr = 0;
      if (shader_uses_sysval(vs, graphics, vs.first_vertex))
         fv_ptr = cmdbuf->state.gfx.vs.push_uniforms +
                  shader_remapped_sysval_offset(
                     vs, sysval_offset(graphics, vs.first_vertex));
      if (shader_uses_sysval(vs, graphics, vs.base_instance))
         bi_ptr = cmdbuf->state.gfx.vs.push_uniforms +
                  shader_remapped_sysval_offset(
                     vs, sysval_offset(graphics, vs.base_instance));

      uint32_t inst_mask;
      uint64_t inst_info_gpu, vs_attribs;
      if (!v9_inst_attr_info(cmdbuf, &inst_mask, &inst_info_gpu, &vs_attribs))
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;

      struct panvk_precomp_ctx pctx = panvk_per_arch(precomp_cs)(cmdbuf);
      panlib_v9_draw_indirect_patch(&pctx, panlib_1d(1),
                                    (enum panlib_barrier)(PANLIB_BARRIER_JM_BARRIER |
                                    PANLIB_BARRIER_JM_SUPPRESS_PREFETCH),
                                    info->indirect.buffer_dev_addr, job.gpu,
                                    info->index.buffer_dev_addr, fv_ptr, bi_ptr,
                                    info->index.index_size,
                                    info->indirect.count_buffer_dev_addr,
                                    info->indirect.draw_index, vs_attribs,
                                    inst_mask, inst_info_gpu);
      indirect_dep = batch->vtc_jc.job_index;
   }
   static int draw_barrier = -1;
   if (draw_barrier < 0) {
      const char *e = getenv("PANVK_TESS_DRAWBARRIER");
      draw_barrier = e && e[0] == '1';
      if (draw_barrier)
         (void)0;
   }
   /* A gpu_prerast generated draw reads the records and indices the
    * producer jobs just wrote: wait for them. */
   const bool barrier = draw_barrier || info->gpu_prerast_generated;
   pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_MALLOC_VERTEX, barrier,
                  draw_barrier != 0, 0, indirect_dep, &job, false);
   return VK_SUCCESS;
}

/* gpu_prerast on v9 JM. The pre-raster stages the hardware cannot run (the
 * transform feedback capture, later GS and tessellation) run on the shared
 * gpu_prerast shader variants, as COMPUTE jobs in the batch's vertex/tiler
 * chain, and the records they leave in the device arena are drawn with the
 * passthrough VS through MALLOC_VERTEX. Same variants and arena layout as
 * csf/panvk_vX_cmd_draw.c (gpu_prerast_vs/_xfb/_draw_generated). JM has no
 * command stream: what CSF stores with cs_store32 the CPU writes into
 * per-draw memory, and job barriers order the jobs. Every prerast job is a
 * barrier, so the next draw's producer only starts once the previous
 * generated draw stopped reading the arena; vertex/tiler atoms are ordered
 * by the queue. After Noysz/panvk-g99-jm 0060 (JM gfx-compute helper).
 */
static void v9_cmd_draw_layer(struct panvk_cmd_buffer *cmdbuf,
                              struct panvk_draw_info *info);

#define v9_prerast_unhandled(why)                                              \
   mesa_loge_once("panvk: v9 gpu_prerast cannot lower this draw (%s), "       \
                  "skipping it", why)

static bool
v9_prerast_xfb_active(const struct panvk_cmd_buffer *cmdbuf)
{
   const struct panvk_shader *last = cmdbuf->state.gfx.gs.shader
                                        ? cmdbuf->state.gfx.gs.shader
                                        : cmdbuf->state.gfx.vs.shader;

   return cmdbuf->state.gfx.xfb.active && last &&
          panvk_gpu_prerast_has_xfb(&last->gpu_prerast.xfb);
}

/* Polygon mode of a draw without a GS, see gpu_prerast_poly_mode(). */
static enum panvk_gpu_prerast_poly_mode
v9_prerast_poly_mode(const struct panvk_cmd_buffer *cmdbuf,
                     const struct panvk_draw_info *info)
{
   return cmdbuf->state.gfx.gs.shader
             ? PANVK_GPU_PRERAST_POLY_NONE
             : panvk_per_arch(gpu_prerast_poly_mode)(cmdbuf, info);
}

static bool
v9_prerast_needed(const struct panvk_cmd_buffer *cmdbuf,
                  const struct panvk_draw_info *info)
{
   return !info->gpu_prerast_generated &&
          (cmdbuf->state.gfx.gs.shader ||
           panvk_prerast_viewport_shader(&cmdbuf->state.gfx) ||
           v9_prerast_xfb_active(cmdbuf) ||
           v9_prerast_poly_mode(cmdbuf, info) != PANVK_GPU_PRERAST_POLY_NONE);
}

/* VS stores and atomics run once per vertex through gpu_prerast, like CSF
 * gpu_prerast_wanted(). Draws it cannot lower fall back to IDVS, which runs
 * them per position and varying shader invocation (allowed: the number of
 * VS invocations is implementation-defined). */
static bool
v9_prerast_side_effects(struct panvk_cmd_buffer *cmdbuf,
                        const struct panvk_draw_info *info)
{
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;

   if (info->gpu_prerast_generated || !vs ||
       !vs->gpu_prerast.has_side_effects)
      return false;

   /* v9_prerast_draw() splits direct draws over the arena. */
   struct panvk_draw_info probe = *info;
   if (!probe.indirect.buffer_dev_addr)
      probe.vertex.count = probe.instance.count = 1;
   return panvk_per_arch(gpu_prerast_can_lower)(cmdbuf, &probe,
                                                &vs->gpu_prerast);
}

/* Raster VS of a draw: the GS passthrough after a GS, the VS passthrough for
 * generated draws and viewport-selecting VSs, else the HW variant. */
static const struct panvk_shader_variant *
v9_raster_vs_variant(const struct panvk_cmd_buffer *cmdbuf, bool generated)
{
   const struct panvk_shader *gs = cmdbuf->state.gfx.gs.shader;
   if (gs)
      return &gs->variants[PANVK_GS_VARIANT_PASSTHROUGH];

   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   return generated || (vs && vs->gpu_prerast.vp_scissor_loc)
             ? panvk_shader_gpu_prerast_variant(
                  vs, PANVK_VS_VARIANT_GPU_PASSTHROUGH)
             : panvk_shader_hw_variant(vs);
}

/* One COMPUTE job of a gpu_prerast variant in the vertex/tiler chain, a
 * barrier: it waits for every earlier job of the batch. */
/* COMPUTE job of a gpu_prerast variant. It is added to the vertex/tiler
 * chain by v9_prerast_add_job() as a barrier: it waits for every earlier job
 * of the batch. Indirect jobs get their workgroup counts from
 * panlib_v9_prerast_indirect(), which must be added before them. */
static struct pan_ptr
v9_prerast_job(struct panvk_cmd_buffer *cmdbuf,
               const struct panvk_shader_variant *cs, uint64_t res_table,
               uint64_t push, uint32_t x, uint32_t y, uint32_t z,
               bool indirect)
{
   struct pan_compute_dim dim = {x, y, z};
   uint64_t tsd =
      panvk_per_arch(cmd_dispatch_prepare_tls)(cmdbuf, cs, &dim, indirect);
   struct pan_ptr job = panvk_cmd_alloc_desc(cmdbuf, COMPUTE_JOB);

   if (!tsd || !job.gpu)
      return (struct pan_ptr){0};

   pan_section_pack(job.cpu, COMPUTE_JOB, PAYLOAD, cfg) {
      cfg.workgroup_size_x = MAX2(cs->cs.local_size.x, 1);
      cfg.workgroup_size_y = MAX2(cs->cs.local_size.y, 1);
      cfg.workgroup_size_z = MAX2(cs->cs.local_size.z, 1);
      cfg.workgroup_count_x = x;
      cfg.workgroup_count_y = y;
      cfg.workgroup_count_z = z;
      cfg.task_increment = 1;
      cfg.task_axis = MALI_TASK_AXIS_Z;
      cfg.compute.resources = res_table;
      cfg.compute.shader = panvk_priv_mem_dev_addr(cs->spd);
      cfg.compute.thread_storage = tsd;
      cfg.compute.fau = push;
      cfg.compute.fau_count = cs->fau.total_count;
   }
   return job;
}

static void
v9_prerast_add_job(struct panvk_cmd_buffer *cmdbuf, struct pan_ptr *job)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;

   util_dynarray_append(&batch->jobs, job->cpu);
   pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_COMPUTE, true, false, 0, 0,
                  job, false);
}

/* Fills the panvk_gpu_prerast_params of a direct draw, like the STORE_PARAM
 * list of CSF gpu_prerast_vs(). */
static void
v9_prerast_fill_params(const struct panvk_draw_info *src,
                       const struct panvk_shader *shader, uint64_t records,
                       uint64_t indices, struct panvk_gpu_prerast_params *p)
{
   memset(p, 0, sizeof(*p));
   p->vertex.index_buffer = src->index.buffer_dev_addr;
   p->vertex.index_size_B = src->index.index_size;
   p->vertex.index_buffer_range_el =
      src->index.index_size ? src->index.buffer_size / src->index.index_size
                            : 0;
   p->vertex.verts_per_instance = src->vertex.count;
   p->vertex.output_buffer = records;
   p->vertex.outputs = shader->gpu_prerast.io.outputs_written;
   /* JM draw infos carry the index offset in bytes, the params (like CSF
    * draw infos) in indices. */
   p->index_offset =
      src->index.index_size ? src->index.offset / src->index.index_size : 0;
   p->vertex_count = src->vertex.count;
   p->instance_count = src->instance.count;
   p->first_instance = src->instance.base;
   p->generated_indices = indices;
   p->restart_index = src->index.index_size == 1   ? UINT8_MAX
                      : src->index.index_size == 2 ? UINT16_MAX
                                                   : UINT32_MAX;
   p->primitive_restart = src->index.restart_enable;
   p->record_capacity =
      panvk_gpu_prerast_record_capacity(shader->gpu_prerast.io.record_stride);
   p->draw_id = src->indirect.draw_index;
}

/* VS (or TES) descriptors and resource table for the lowered variant. */
static VkResult
v9_prerast_prepare_vs_descs(struct panvk_cmd_buffer *cmdbuf,
                            const struct panvk_draw_info *info)
{
   struct panvk_descriptor_state *desc_state = &cmdbuf->state.gfx.desc_state;
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   VkResult result = panvk_per_arch(cmd_prepare_push_descs)(
      cmdbuf, desc_state, vs->desc_info.used_set_mask);
   if (result != VK_SUCCESS)
      return result;

   result = v9_prepare_vs_driver_set(cmdbuf, info->instance.base);
   if (result != VK_SUCCESS)
      return result;

   return panvk_per_arch(cmd_prepare_shader_res_table)(
      cmdbuf, desc_state, &vs->desc_info, &cmdbuf->state.gfx.vs.desc, 1);
}

/* Lowered VS of a draw: one invocation per vertex and instance, records at
 * arena + PANVK_GPU_PRERAST_RECORD_OFFSET. Fills the per-draw params (the
 * counts of an indirect draw come from panlib_v9_prerast_indirect()) and
 * prepares the job, the caller adds it. */
struct v9_prerast_vs_job {
   uint64_t params;
   struct panvk_gpu_prerast_params *params_cpu;
   uint64_t push;
   struct pan_ptr job;
};

static bool
v9_prerast_vs(struct panvk_cmd_buffer *cmdbuf,
              const struct panvk_draw_info *src, uint64_t arena,
              struct v9_prerast_vs_job *out)
{
   const struct panvk_shader *shader = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader_variant *lowered =
      panvk_shader_gpu_prerast_variant(shader, PANVK_VS_VARIANT_GPU_LOWERED);
   const uint64_t records = arena + PANVK_GPU_PRERAST_RECORD_OFFSET;
   const uint64_t indices = arena + PANVK_GPU_PRERAST_INDEX_OFFSET;
   const bool indirect = src->indirect.buffer_dev_addr != 0;

   struct pan_ptr params = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(struct panvk_gpu_prerast_params), 64);
   if (!params.gpu)
      return false;
   v9_prerast_fill_params(src, shader, records, indices, params.cpu);

   if (v9_prerast_prepare_vs_descs(cmdbuf, src) != VK_SUCCESS)
      return false;

   panvk_per_arch(cmd_prepare_draw_sysvals)(
      cmdbuf, src, panvk_shader_only_variant(get_fs(cmdbuf)));
   cmdbuf->state.gfx.sysvals.gpu_prerast_params = params.gpu;
   cmdbuf->state.gfx.sysvals.gpu_prerast_records = records;

   struct pan_ptr push;
   if (panvk_per_arch(cmd_prepare_gfx_push_uniforms)(cmdbuf, lowered, &push,
                                                     1) != VK_SUCCESS)
      return false;

   out->params = params.gpu;
   out->params_cpu = params.cpu;
   out->push = push.gpu;
   out->job = v9_prerast_job(cmdbuf, lowered,
                             cmdbuf->state.gfx.vs.desc.res_table, push.gpu,
                             indirect ? 1 : src->vertex.count,
                             indirect ? 1 : src->instance.count, 1, indirect);
   return out->job.gpu != 0;
}

/* GS kernel of a draw, CSF gpu_prerast_gs_run(): one invocation per input
 * vertex slot, instance and GS invocation, reading the lowered VS records and
 * appending strips to arena + PANVK_GPU_PRERAST_GS_RECORD_OFFSET/_INDEX_OFFSET.
 * The GS params and the generated draw command live in per-draw memory, not
 * the arena: the CPU fills the params, a fill job zeroes the draw command
 * the kernel appends to (so a resubmitted command buffer starts from 0). The
 * job is prepared here and added by the caller. After Noysz/panvk-g99-jm
 * 0063/0065. */
struct v9_prerast_gs_job {
   uint64_t params;
   uint64_t draw;
   struct pan_ptr job;
};

static bool
v9_prerast_gs(struct panvk_cmd_buffer *cmdbuf,
              const struct panvk_draw_info *src, uint64_t arena,
              struct v9_prerast_gs_job *out)
{
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader *gs = cmdbuf->state.gfx.gs.shader;
   const struct panvk_gpu_prerast_gs_info *gsi = &gs->gpu_prerast.gs;
   const struct panvk_shader_variant *kernel =
      &gs->variants[PANVK_GS_VARIANT_KERNEL];
   struct panvk_descriptor_state *desc_state = &cmdbuf->state.gfx.desc_state;
   struct panvk_shader_desc_state *gs_desc = &cmdbuf->state.gfx.gs.desc;
   const bool indirect = src->indirect.buffer_dev_addr != 0;

   if (panvk_per_arch(cmd_prepare_push_descs)(
          cmdbuf, desc_state, gs->desc_info.used_set_mask) != VK_SUCCESS ||
       v9_prepare_stage_driver_set(cmdbuf, gs, gs_desc) != VK_SUCCESS ||
       panvk_per_arch(cmd_prepare_shader_res_table)(
          cmdbuf, desc_state, &gs->desc_info, gs_desc, 1) != VK_SUCCESS)
      return false;

   struct pan_ptr params = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(struct panvk_gpu_prerast_gs_params), 64);
   struct pan_ptr draw = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(VkDrawIndexedIndirectCommand), 16);
   if (!params.gpu || !draw.gpu)
      return false;

   const struct panvk_pstats_query_state *pstats =
      &cmdbuf->state.gfx.pstats_query;
   struct panvk_gpu_prerast_gs_params *p = params.cpu;
   *p = (struct panvk_gpu_prerast_gs_params){
      .vs_records = arena + PANVK_GPU_PRERAST_RECORD_OFFSET,
      .vs_outputs = vs->gpu_prerast.io.outputs_written,
      .gen_indices = arena + PANVK_GPU_PRERAST_INDEX_OFFSET,
      .records = arena + PANVK_GPU_PRERAST_GS_RECORD_OFFSET,
      .indices = arena + PANVK_GPU_PRERAST_GS_INDEX_OFFSET,
      .draw = draw.gpu,
      /* panlib_v9_prerast_indirect() writes the counts of indirect draws. */
      .vertex_count = indirect ? 0 : src->vertex.count,
      .instance_count = indirect ? 0 : src->instance.count,
      .primitive_restart = src->index.restart_enable,
      .prim = src->prim,
      .cap_slots = panvk_gpu_prerast_gs_cap_slots(gsi),
      .vs_record_capacity =
         panvk_gpu_prerast_record_capacity(vs->gpu_prerast.io.record_stride),
      .provoking_last = cmdbuf->vk.dynamic_graphics_state.rs.provoking_vertex ==
                        VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT,
      .prim_id_base = src->gpu_prerast_prim_base,
      .raster_stream =
         cmdbuf->vk.dynamic_graphics_state.rs.rasterization_stream,
      .stats = {
         panvk_pstats_counter_addr(
            pstats, VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT,
            true),
         panvk_pstats_counter_addr(
            pstats, VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT,
            true),
         panvk_pstats_counter_addr(
            pstats, VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT, true),
         panvk_pstats_counter_addr(
            pstats, VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT, true),
      },
      .prims_generated = cmdbuf->state.gfx.prims_generated_query.ptr,
      .strips = arena + PANVK_GPU_PRERAST_GS_STRIP_OFFSET,
   };

   struct pan_ptr push;
   if (panvk_per_arch(cmd_prepare_gfx_push_uniforms)(cmdbuf, kernel, &push,
                                                     1) != VK_SUCCESS)
      return false;
   if (shader_uses_sysval(kernel, graphics, gpu_prerast_params))
      *(uint64_t *)((uint8_t *)push.cpu +
                    shader_remapped_sysval_offset(
                       kernel, sysval_offset(graphics, gpu_prerast_params))) =
         params.gpu;

   out->params = params.gpu;
   out->draw = draw.gpu;
   out->job = v9_prerast_job(cmdbuf, kernel, gs_desc->res_table, push.gpu,
                             indirect ? 1 : src->vertex.count,
                             indirect ? 1 : src->instance.count,
                             gsi->invocations, indirect);
   return out->job.gpu != 0;
}

/* Jobs before the GS kernel: zero its draw command, and with primitive
 * restart build the strip table (panlib_gs_strip_table()) from the lowered
 * VS indices. */
static void
v9_prerast_gs_prologue(struct panvk_cmd_buffer *cmdbuf,
                       const struct panvk_draw_info *src, uint64_t arena,
                       const struct v9_prerast_gs_job *gs)
{
   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);

   panlib_fill_scalar(&ctx, panlib_1d(sizeof(VkDrawIndexedIndirectCommand) / 4),
                      PANLIB_BARRIER_JM_BARRIER, gs->draw, 0);

   if (src->gpu_prerast_prim_base_addr)
      panlib_v9_copy32(
         &ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER,
         gs->params + offsetof(struct panvk_gpu_prerast_gs_params, prim_id_base),
         src->gpu_prerast_prim_base_addr);

   if (src->index.restart_enable)
      panlib_gs_strip_table(
         &ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER,
         arena + PANVK_GPU_PRERAST_INDEX_OFFSET,
         gs->params + offsetof(struct panvk_gpu_prerast_gs_params, vertex_count),
         PANVK_GPU_PRERAST_GS_STRIP_CAP, src->prim,
         arena + PANVK_GPU_PRERAST_GS_STRIP_OFFSET);
}

/* fillModeNonSolid, the polygon kernel of CSF gpu_prerast_vs(): one
 * invocation per lowered VS record, turning each triangle of the records
 * into PANVK_GPU_PRERAST_POLY_INDICES_PER_PRIM line or point indices (culled
 * like the rasterizer would) at arena + PANVK_GPU_PRERAST_POLY_INDEX_OFFSET,
 * and writing the draw command. The command lives in per-draw memory,
 * zeroed by a fill job first: an indirect draw past its count or empty runs
 * no kernel. Fills the poly params, prepares the job (added by the caller
 * after the lowered VS job) and returns the draw command, 0 on failure. */
static uint64_t
v9_prerast_poly(struct panvk_cmd_buffer *cmdbuf,
                const struct panvk_draw_info *src, uint64_t arena,
                const struct v9_prerast_vs_job *vs,
                enum panvk_gpu_prerast_poly_mode mode, struct pan_ptr *job)
{
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct panvk_shader_variant *polygon = panvk_shader_gpu_prerast_variant(
      cmdbuf->state.gfx.vs.shader, PANVK_VS_VARIANT_GPU_POLYGON);
   const bool indirect = src->indirect.buffer_dev_addr != 0;
   struct pan_ptr draw = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(VkDrawIndexedIndirectCommand), 16);
   struct pan_ptr push;

   if (!draw.gpu ||
       panvk_per_arch(cmd_prepare_gfx_push_uniforms)(cmdbuf, polygon, &push,
                                                     1) != VK_SUCCESS)
      return 0;
   if (shader_uses_sysval(polygon, graphics, gpu_prerast_params))
      *(uint64_t *)((uint8_t *)push.cpu +
                    shader_remapped_sysval_offset(
                       polygon, sysval_offset(graphics, gpu_prerast_params))) =
         vs->params;

   struct panvk_gpu_prerast_params *p = vs->params_cpu;
   p->poly.indices = arena + PANVK_GPU_PRERAST_POLY_INDEX_OFFSET;
   p->poly.draw = draw.gpu;
   p->poly.mode = mode;
   p->poly.prim = src->prim;
   p->poly.cull = dyns->rs.cull_mode;
   p->poly.front_ccw = dyns->rs.front_face == VK_FRONT_FACE_COUNTER_CLOCKWISE;
   p->poly.flip_y = dyns->vp.viewports[0].height < 0.0f;

   *job = v9_prerast_job(cmdbuf, polygon, cmdbuf->state.gfx.vs.desc.res_table,
                         push.gpu, indirect ? 1 : src->vertex.count,
                         indirect ? 1 : src->instance.count, 1, indirect);
   if (!job->gpu)
      return 0;

   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   panlib_fill_scalar(&ctx, panlib_1d(sizeof(VkDrawIndexedIndirectCommand) / 4),
                      PANLIB_BARRIER_JM_BARRIER, draw.gpu, 0);
   return draw.gpu;
}

/* panlib_xfb_capture() over the records the lowered VS or GS left, CSF
 * gpu_prerast_xfb() without the conditional rendering part (v9 has no
 * VK_EXT_conditional_rendering). */
static void
v9_prerast_xfb(struct panvk_cmd_buffer *cmdbuf,
               const struct panvk_draw_info *src, uint64_t arena,
               uint64_t params)
{
   const struct panvk_shader *gs = cmdbuf->state.gfx.gs.shader;
   const struct panvk_shader *last = gs ? gs : cmdbuf->state.gfx.vs.shader;
   const struct panvk_gpu_prerast_xfb_info *xi = &last->gpu_prerast.xfb;
   struct pan_ptr ptr = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(struct panlib_xfb_params), 16);

   if (!ptr.gpu)
      return;

   struct panlib_xfb_params *x = ptr.cpu;
   memset(x, 0, sizeof(*x));
   x->offsets = cmdbuf->state.gfx.xfb.offsets;
   for (unsigned i = 0; i < PANLIB_XFB_MAX_BUFFERS; i++) {
      x->buffers[i] = cmdbuf->state.gfx.xfb.buffers[i].addr;
      x->sizes[i] = cmdbuf->state.gfx.xfb.buffers[i].size;
      x->strides[i] = xi->stride[i];
      x->buffer_to_stream |= xi->buffer_to_stream[i] << (2 * i);
      x->queries[i] = cmdbuf->state.gfx.xfb.queries[i];
   }
   x->output_count = xi->output_count;
   memcpy(x->outputs, xi->outputs, xi->output_count * sizeof(uint32_t));

   if (gs) {
      const struct panvk_gpu_prerast_gs_info *g = &gs->gpu_prerast.gs;

      x->records = arena + PANVK_GPU_PRERAST_GS_RECORD_OFFSET;
      x->counts = params + offsetof(struct panvk_gpu_prerast_gs_params,
                                    vertex_count);
      x->record_stride = g->record_stride;
      x->record_capacity = panvk_gpu_prerast_gs_cap_slots(g);
      x->prim = g->output_prim;
      x->gs_max_vertices = g->max_vertices;
      x->gs_invocations = g->invocations;
   } else {
      x->records = arena + PANVK_GPU_PRERAST_RECORD_OFFSET;
      x->indices = arena + PANVK_GPU_PRERAST_INDEX_OFFSET;
      x->counts =
         params + offsetof(struct panvk_gpu_prerast_params, vertex_count);
      x->record_stride = last->gpu_prerast.io.record_stride;
      x->record_capacity = panvk_gpu_prerast_record_capacity(x->record_stride);
      x->prim = src->prim;
      x->restart = src->index.index_size && src->index.restart_enable;
   }
   x->active = 1;

   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   panlib_xfb_capture(&ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER, ptr.gpu);
}

/* Draws the records of a lowering. VS only: the generated indices (vertex
 * IDs of instance 0, ~0 at restarts) with the passthrough VS, gen_draw is
 * the VkDrawIndexedIndirectCommand panlib_v9_prerast_indirect() wrote for an
 * indirect draw, 0 for a direct one. GS: the strips with the GS passthrough,
 * gen_draw is the GS kernel's draw command. Polygon mode: the line or point
 * indices of v9_prerast_poly(), gen_draw is its draw command. */
static void
v9_prerast_draw_generated(struct panvk_cmd_buffer *cmdbuf,
                          const struct panvk_draw_info *src, uint64_t arena,
                          uint64_t gen_draw,
                          enum panvk_gpu_prerast_poly_mode poly)
{
   struct panvk_draw_info generated = *src;

   generated.index.buffer_dev_addr = arena + PANVK_GPU_PRERAST_INDEX_OFFSET;
   generated.index.buffer_size =
      PANVK_GPU_PRERAST_MAX_INVOCATIONS * sizeof(uint32_t);
   generated.index.index_size = 4;
   generated.index.offset = 0;
   generated.vertex.base = 0;
   generated.indirect.buffer_dev_addr = gen_draw;
   generated.indirect.count_buffer_dev_addr = 0;
   generated.indirect.draw_index = 0;
   generated.gpu_prerast_generated = true;
   cmdbuf->state.gfx.sysvals.gpu_prerast_records =
      arena + PANVK_GPU_PRERAST_RECORD_OFFSET;

   /* The GS strips: one instance, restart-separated, the count the kernel
    * appended in gen_draw. */
   const struct panvk_shader *gs = cmdbuf->state.gfx.gs.shader;
   if (gs) {
      switch (gs->gpu_prerast.gs.output_prim) {
      case MESA_PRIM_POINTS:
         generated.prim = MESA_PRIM_POINTS;
         break;
      case MESA_PRIM_LINE_STRIP:
         generated.prim = MESA_PRIM_LINE_STRIP;
         break;
      default:
         generated.prim = MESA_PRIM_TRIANGLE_STRIP;
         break;
      }
      generated.index.buffer_dev_addr = arena + PANVK_GPU_PRERAST_GS_INDEX_OFFSET;
      generated.index.buffer_size =
         PANVK_GPU_PRERAST_GS_INDEX_CAPACITY * sizeof(uint32_t);
      generated.index.restart_enable = true;
      generated.vertex.count = 1;
      generated.instance.base = 0;
      generated.instance.count = 1;
      cmdbuf->state.gfx.sysvals.gpu_prerast_records =
         arena + PANVK_GPU_PRERAST_GS_RECORD_OFFSET;
   } else if (poly != PANVK_GPU_PRERAST_POLY_NONE) {
      generated.prim = poly == PANVK_GPU_PRERAST_POLY_POINT ? MESA_PRIM_POINTS
                                                            : MESA_PRIM_LINES;
      generated.index.buffer_dev_addr =
         arena + PANVK_GPU_PRERAST_POLY_INDEX_OFFSET;
      generated.index.buffer_size = PANVK_GPU_PRERAST_MAX_INVOCATIONS *
                                    PANVK_GPU_PRERAST_POLY_INDICES_PER_PRIM *
                                    sizeof(uint32_t);
      generated.index.restart_enable = true;
      generated.vertex.count = 1;
      generated.instance.base = 0;
      generated.instance.count = 1;
   }
   gfx_state_set_dirty(cmdbuf, VS);
   gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
   v9_cmd_draw_layer(cmdbuf, &generated);
   gfx_state_set_dirty(cmdbuf, VS);
   gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
}

/* panlib_v9_prerast_indirect() for an indirect draw: patches the params and
 * the lowered VS job, returns the generated draw command, 0 on failure. */
static uint64_t
v9_prerast_indirect(struct panvk_cmd_buffer *cmdbuf,
                    const struct panvk_draw_info *src,
                    const struct v9_prerast_vs_job *vs,
                    const struct v9_prerast_gs_job *gs, uint64_t chunk,
                    uint64_t poly_job)
{
   const struct panvk_shader *shader = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader_variant *lowered =
      panvk_shader_gpu_prerast_variant(shader, PANVK_VS_VARIANT_GPU_LOWERED);
   struct pan_ptr args = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(struct panlib_v9_prerast_patch), 16);
   struct pan_ptr gen = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(VkDrawIndexedIndirectCommand), 16);
   uint32_t inst_mask;
   uint64_t inst_info, vs_attribs;

   if (!args.gpu || !gen.gpu ||
       !v9_inst_attr_info(cmdbuf, &inst_mask, &inst_info, &vs_attribs))
      return 0;

   static_assert(offsetof(struct panvk_gpu_prerast_params, instance_count) ==
                    offsetof(struct panvk_gpu_prerast_params, vertex_count) + 4,
                 "panlib_v9_prerast_indirect counts layout");
   static_assert(offsetof(struct panvk_gpu_prerast_params, first_instance) ==
                    offsetof(struct panvk_gpu_prerast_params, vertex_count) + 8,
                 "panlib_v9_prerast_indirect counts layout");
   static_assert(offsetof(struct panvk_gpu_prerast_params, index_offset) ==
                    offsetof(struct panvk_gpu_prerast_params, vertex_count) + 20,
                 "panlib_v9_prerast_indirect counts layout");
   static_assert(offsetof(struct panvk_gpu_prerast_params, chunk_vertex) ==
                    offsetof(struct panvk_gpu_prerast_params, vertex_count) + 32,
                 "panlib_v9_prerast_indirect counts layout");
   static_assert(offsetof(struct panvk_gpu_prerast_params, chunk_instance) ==
                    offsetof(struct panvk_gpu_prerast_params, vertex_count) + 36,
                 "panlib_v9_prerast_indirect counts layout");

   struct panlib_v9_prerast_patch *a = args.cpu;
   *a = (struct panlib_v9_prerast_patch){
      .cmd = src->indirect.buffer_dev_addr,
      .draw_count = src->indirect.count_buffer_dev_addr,
      .counts =
         vs->params + offsetof(struct panvk_gpu_prerast_params, vertex_count),
      .verts_per_instance =
         vs->params + offsetof(struct panvk_gpu_prerast_params,
                               vertex.verts_per_instance),
      .vs_job = vs->job.gpu,
      .vs_attribs = vs_attribs,
      .inst_attr_info = inst_info,
      .gen_draw = gen.gpu,
      .chunk = chunk,
      .indexed = src->index.index_size != 0,
      .draw_index = src->indirect.draw_index,
      .capacity = panvk_gpu_prerast_record_capacity(
         shader->gpu_prerast.io.record_stride),
      .inst_attr_mask = inst_mask,
   };
   if (gs) {
      const struct panvk_gpu_prerast_gs_info *gsi =
         &cmdbuf->state.gfx.gs.shader->gpu_prerast.gs;

      a->gs_job = gs->job.gpu;
      a->gs_counts =
         gs->params + offsetof(struct panvk_gpu_prerast_gs_params, vertex_count);
      a->gs_invocations = gsi->invocations;
      /* The GS slots bound the draw too, see gpu_prerast_can_lower(). */
      a->capacity = MIN2(a->capacity,
                         panvk_gpu_prerast_gs_cap_slots(gsi) / gsi->invocations);
   } else if (poly_job) {
      /* No GS with a polygon mode: the GS job slot sizes the polygon kernel
       * job like the lowered VS one (z = 1). gs_counts takes the {vertex,
       * instance} counts the kernel already wrote to the params. */
      a->gs_job = poly_job;
      a->gs_counts = a->counts;
      a->gs_invocations = 1;
   }
   if (shader_uses_sysval(lowered, graphics, vs.first_vertex))
      a->first_vertex_sysval =
         vs->push + shader_remapped_sysval_offset(
                       lowered, sysval_offset(graphics, vs.first_vertex));
   if (shader_uses_sysval(lowered, graphics, vs.base_instance))
      a->base_instance_sysval =
         vs->push + shader_remapped_sysval_offset(
                       lowered, sysval_offset(graphics, vs.base_instance));

   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   panlib_v9_prerast_indirect(&ctx, panlib_1d(1),
                              (enum panlib_barrier)(PANLIB_BARRIER_JM_BARRIER |
                              PANLIB_BARRIER_JM_SUPPRESS_PREFETCH),
                              args.gpu);
   return gen.gpu;
}

/* Multi-viewport form of v9_prerast_draw_generated(): panlib_vp_runs() sorts
 * the primitives of the records into viewport runs, as on CSF
 * (gpu_prerast_vp_prepare()), panlib_v9_vp_partition() packs them per
 * viewport, and each viewport gets one draw clamped to its depth range. One
 * draw when every viewport has the same range. counts is the VS params
 * {vertex count, instance count} or the GS draw command. */
static void
v9_prerast_draw_viewports(struct panvk_cmd_buffer *cmdbuf,
                          const struct panvk_draw_info *src, uint64_t arena,
                          uint64_t counts)
{
   const struct panvk_shader *gs = cmdbuf->state.gfx.gs.shader;
   const struct panvk_shader *vps =
      panvk_prerast_viewport_shader(&cmdbuf->state.gfx);
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));
   const uint32_t vp_count =
      MIN2(cmdbuf->vk.dynamic_graphics_state.vp.viewport_count,
           PANLIB_VP_MAX_VIEWPORTS);
   uint64_t in_indices, records, outputs;
   uint32_t record_stride, prim, index_count = 0, out_count = 0;
   uint32_t run_capacity = 0;
   bool count_ok, per_instance;

   if (gs) {
      const struct panvk_gpu_prerast_gs_info *gsi = &gs->gpu_prerast.gs;
      const uint32_t slots = panvk_gpu_prerast_gs_cap_slots(gsi);

      count_ok = slots && gsi->index_stride &&
                 !__builtin_mul_overflow(slots, gsi->index_stride,
                                         &index_count);
      /* A strip expands to 3 indices per triangle. */
      out_count = gsi->output_prim == MESA_PRIM_TRIANGLE_STRIP &&
                        index_count > 2
                     ? (index_count - 2) * 3
                     : index_count;
      run_capacity = count_ok ? MIN2(out_count, PANLIB_VP_MAX_RUNS) : 0;
      in_indices = arena + PANVK_GPU_PRERAST_GS_INDEX_OFFSET;
      records = arena + PANVK_GPU_PRERAST_GS_RECORD_OFFSET;
      outputs = gsi->outputs_written;
      record_stride = gsi->record_stride;
      prim = gsi->output_prim;
      per_instance = false;
   } else {
      const struct panvk_gpu_prerast_io *io = &vps->gpu_prerast.io;

      index_count = panvk_gpu_prerast_record_capacity(io->record_stride);
      uint64_t total = index_count;
      if (!src->indirect.buffer_dev_addr)
         total = MIN2(total,
                      (uint64_t)src->vertex.count * src->instance.count);
      const uint32_t mult =
         src->prim == MESA_PRIM_LINE_STRIP                 ? 2
         : (src->prim == MESA_PRIM_TRIANGLE_STRIP ||
            src->prim == MESA_PRIM_TRIANGLE_FAN)           ? 3
                                                           : 1;
      const uint32_t vpp = src->prim == MESA_PRIM_POINTS ? 1
                           : (src->prim == MESA_PRIM_LINES ||
                              src->prim == MESA_PRIM_LINE_STRIP)
                              ? 2
                              : 3;
      count_ok = index_count && total && total * mult <= UINT32_MAX;
      out_count = count_ok ? total * mult : 0;
      run_capacity =
         count_ok ? MIN2(DIV_ROUND_UP(out_count, vpp), PANLIB_VP_MAX_RUNS) : 0;
      in_indices = arena + PANVK_GPU_PRERAST_INDEX_OFFSET;
      records = arena + PANVK_GPU_PRERAST_RECORD_OFFSET;
      outputs = io->outputs_written;
      record_stride = io->record_stride;
      prim = src->prim;
      per_instance = true;
   }

   uint64_t run_bytes = 0, index_bytes = 0;
   if (!count_ok || index_count > PANLIB_VP_MAX_INPUT_INDICES ||
       out_count > PANLIB_VP_MAX_OUT_INDICES ||
       !panlib_vp_run_bytes(run_capacity, &run_bytes) ||
       !panvk_gpu_prerast_checked_size(out_count, sizeof(uint32_t), 4,
                                       &index_bytes)) {
      mesa_loge_once("panvk: viewport run list does not fit, draw discarded");
      vk_command_buffer_set_error(&cmdbuf->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      return;
   }

   /* One draw when no viewport's depth clamp differs from viewport 0's. */
   float z0_min, z0_max;
   bool merged = true;
   v9_viewport_depth_range(cmdbuf, 0, &z0_min, &z0_max);
   for (uint32_t v = 1; v < vp_count; v++) {
      float lo, hi;
      v9_viewport_depth_range(cmdbuf, v, &lo, &hi);
      merged &= lo == z0_min && hi == z0_max;
   }

   struct pan_ptr runs = panvk_cmd_alloc_dev_mem(cmdbuf, desc, run_bytes, 16);
   struct pan_ptr out = panvk_cmd_alloc_dev_mem(cmdbuf, desc, index_bytes, 16);
   struct pan_ptr part = {0};
   if (!merged)
      part = panvk_cmd_alloc_dev_mem(cmdbuf, desc, index_bytes, 16);
   struct pan_ptr draws = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, PANLIB_VP_MAX_VIEWPORTS * 5 * sizeof(uint32_t), 16);
   if (!runs.gpu || !out.gpu || (!merged && !part.gpu) || !draws.gpu) {
      vk_command_buffer_set_error(&cmdbuf->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      return;
   }

   const struct panlib_vp_runs_launch_args args = {
      .in_indices = in_indices,
      .records = records,
      .out_indices = out.gpu,
      .header = runs.gpu,
      .outputs_written = outputs,
      .counts = counts,
      .record_stride = record_stride,
      .viewport_count = vp_count,
      .prim = prim,
      .provoking_last = cmdbuf->vk.dynamic_graphics_state.rs.provoking_vertex ==
                        VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT,
      .index_bound = index_count,
      .index_capacity = out_count,
      .run_capacity = run_capacity,
      .per_instance = per_instance,
      /* A VS has no PrimitiveID output (a TES has one): give the FS one that
       * does not restart with each draw. */
      .pack_prim_id = per_instance && fs && fs->info.fs.reads_primitive_id &&
                      !(outputs & VARYING_BIT_PRIMITIVE_ID),
      .prim_base = src->gpu_prerast_prim_base,
   };
   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   panlib_vp_runs_launch_struct(&ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER,
                                args);
   panlib_v9_vp_partition(&ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER,
                          runs.gpu, out.gpu, merged ? 0 : part.gpu, draws.gpu,
                          vp_count, merged);

   struct panvk_draw_info generated = *src;
   generated.prim = prim == MESA_PRIM_POINTS ? MESA_PRIM_POINTS
                    : (prim == MESA_PRIM_LINE_STRIP || prim == MESA_PRIM_LINES)
                       ? MESA_PRIM_LINES
                       : MESA_PRIM_TRIANGLES;
   generated.index.buffer_dev_addr = merged ? out.gpu : part.gpu;
   generated.index.buffer_size = index_bytes;
   generated.index.index_size = 4;
   generated.index.offset = 0;
   generated.index.restart_enable = false;
   generated.vertex.base = 0;
   generated.vertex.count = 1;
   generated.instance.base = 0;
   generated.instance.count = 1;
   generated.indirect.count_buffer_dev_addr = 0;
   generated.indirect.draw_index = 0;
   generated.gpu_prerast_generated = true;

   for (uint32_t v = 0; v < (merged ? 1 : vp_count); v++) {
      cmdbuf->state.gfx.sysvals.gpu_prerast_records = records;
      generated.indirect.buffer_dev_addr =
         draws.gpu + v * 5 * sizeof(uint32_t);
      generated.viewport_clamp = merged ? 0 : v + 1;
      gfx_state_set_dirty(cmdbuf, VS);
      gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
      v9_cmd_draw_layer(cmdbuf, &generated);
   }
   gfx_state_set_dirty(cmdbuf, VS);
   gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
}

/* Lowers one draw that fits the arena (direct) or is dropped when it does
 * not (indirect, see panlib_v9_prerast_indirect()). */
static void
v9_prerast_draw_one(struct panvk_cmd_buffer *cmdbuf,
                    const struct panvk_draw_info *src, uint64_t chunk)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct panvk_shader *shader = cmdbuf->state.gfx.vs.shader;
   const uint64_t arena = panvk_priv_mem_dev_addr(dev->gpu_prerast_arena);

   if (!arena ||
       !panvk_per_arch(gpu_prerast_can_lower)(cmdbuf, src,
                                              &shader->gpu_prerast)) {
      v9_prerast_unhandled("shader variants or arena missing");
      return;
   }

   if (!cmdbuf->cur_batch)
      panvk_per_arch(cmd_open_batch)(cmdbuf);

   /* Patch, producer, capture, draw patch and draw jobs. */
   if (cmdbuf->cur_batch->vtc_jc.job_index + 16 >= UINT16_MAX) {
      panvk_per_arch(cmd_close_batch)(cmdbuf);
      panvk_per_arch(cmd_open_batch)(cmdbuf);
   }

   if (panvk_per_arch(cmd_alloc_tls_desc)(cmdbuf, true) != VK_SUCCESS)
      return;

   const bool has_gs = cmdbuf->state.gfx.gs.shader != NULL;
   const enum panvk_gpu_prerast_poly_mode poly = v9_prerast_poly_mode(cmdbuf, src);
   struct v9_prerast_vs_job vs;
   struct v9_prerast_gs_job gs;
   struct pan_ptr poly_job = {0};
   uint64_t gen_draw = 0, poly_draw = 0;
   if (!v9_prerast_vs(cmdbuf, src, arena, &vs) ||
       (has_gs && !v9_prerast_gs(cmdbuf, src, arena, &gs)) ||
       (poly != PANVK_GPU_PRERAST_POLY_NONE &&
        !(poly_draw = v9_prerast_poly(cmdbuf, src, arena, &vs, poly,
                                      &poly_job))) ||
       (src->indirect.buffer_dev_addr &&
        !(gen_draw = v9_prerast_indirect(cmdbuf, src, &vs,
                                         has_gs ? &gs : NULL, chunk,
                                         poly_job.gpu)))) {
      vk_command_buffer_set_error(&cmdbuf->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      return;
   }
   v9_prerast_add_job(cmdbuf, &vs.job);
   if (has_gs) {
      v9_prerast_gs_prologue(cmdbuf, src, arena, &gs);
      v9_prerast_add_job(cmdbuf, &gs.job);
      gen_draw = gs.draw;
   }
   if (poly != PANVK_GPU_PRERAST_POLY_NONE) {
      v9_prerast_add_job(cmdbuf, &poly_job);
      gen_draw = poly_draw;
   }

   /* Captured once per draw: multiview and transform feedback are
    * exclusive, a layered draw captures layer 0. */
   if (src->layer_id == 0 && v9_prerast_xfb_active(cmdbuf))
      v9_prerast_xfb(cmdbuf, src, arena, has_gs ? gs.params : vs.params);

   /* Capture-only draws (rasterizer discard) have nothing to tile. */
   if (cmdbuf->vk.dynamic_graphics_state.rs.rasterizer_discard_enable)
      return;

   /* A polygon-mode draw has no viewport runs, as on CSF: its passthrough
    * still picks the viewport per vertex. */
   if (v9_multi_viewport(cmdbuf) && poly == PANVK_GPU_PRERAST_POLY_NONE)
      v9_prerast_draw_viewports(
         cmdbuf, src, arena,
         has_gs ? gs.draw
                : vs.params + offsetof(struct panvk_gpu_prerast_params,
                                       vertex_count));
   else
      v9_prerast_draw_generated(cmdbuf, src, arena, gen_draw, poly);
}

/* Fans over the arena: chunks of hub + window vertices, like CSF
 * gpu_prerast_split_fan(). */
static void
v9_prerast_split_fan(struct panvk_cmd_buffer *cmdbuf,
                     const struct panvk_draw_info *draw, uint32_t max_verts)
{
   const uint32_t isz = draw->index.index_size;
   /* First index in elements, the JM offset is in bytes. */
   const uint32_t first = isz ? draw->index.offset / isz : 0;
   const uint32_t win = max_verts - 1;
   struct panvk_draw_info chunk = *draw;

   chunk.index.index_size = 4;
   chunk.index.offset = 0;
   chunk.index.restart_enable = false;
   chunk.instance.count = 1;

   for (uint32_t inst = 0; inst < draw->instance.count; inst++) {
      chunk.instance.base = draw->instance.base + inst;
      for (uint32_t s = 1; s + 1 < draw->vertex.count; s += win - 1) {
         const uint32_t n = MIN2(win, draw->vertex.count - s);
         struct pan_ptr ib = panvk_cmd_alloc_dev_mem(
            cmdbuf, desc, (n + 1) * sizeof(uint32_t), 16);
         if (!ib.gpu) {
            vk_command_buffer_set_error(&cmdbuf->vk,
                                        VK_ERROR_OUT_OF_DEVICE_MEMORY);
            return;
         }

         /* The lowered VS job of the chunk is a barrier, it waits for this
          * one. */
         struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
         panlib_fan_chunk_indices(
            &ctx, panlib_1d(DIV_ROUND_UP(n + 1, 64)), PANLIB_BARRIER_NONE,
            ib.gpu, draw->index.buffer_dev_addr, isz,
            isz ? draw->index.buffer_size / isz : 0,
            first, first + s, n + 1);

         chunk.index.buffer_dev_addr = ib.gpu;
         chunk.index.buffer_size = (n + 1) * sizeof(uint32_t);
         chunk.vertex.count = n + 1;
         chunk.gpu_prerast_prim_base = s - 1;
         v9_prerast_draw_one(cmdbuf, &chunk, 0);
      }
   }
}

static bool v9_prerast_reserve_jobs(struct panvk_cmd_buffer *cmdbuf,
                                    uint32_t jobs);

/* Topologies panlib_chunk_step() cuts by vertex range, as on CSF. */
static bool
v9_prerast_chunkable(enum mesa_prim prim)
{
   switch (prim) {
   case MESA_PRIM_POINTS:
   case MESA_PRIM_LINES:
   case MESA_PRIM_TRIANGLES:
   case MESA_PRIM_LINES_ADJACENCY:
   case MESA_PRIM_TRIANGLES_ADJACENCY:
   case MESA_PRIM_LINE_STRIP:
   case MESA_PRIM_LINE_STRIP_ADJACENCY:
   case MESA_PRIM_TRIANGLE_STRIP:
   case MESA_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return true;
   default:
      return false;
   }
}

/* Indirect draws, the JM form of CSF gpu_prerast_chunk_loop(). JM has no
 * loop: V9_PRERAST_INDIRECT_CHUNKS iterations are unrolled into the job
 * chain, each a panlib_chunk_step() planning the next chunk of the draw (at
 * most max_verts vertices x instances) and the lowering of
 * that chunk as an indirect draw. A draw the arena holds is one chunk. The
 * iterations past the end of the draw get an empty chunk, whose jobs
 * panlib_v9_prerast_indirect() makes NULL jobs. No CPU reads anything back.
 * ponytail: a draw needing more chunks loses the rest, as when it was
 * dropped whole before; a GPU job chain jump back to the step would lift the
 * bound. Without a GS, gl_PrimitiveID restarts at 0 in each chunk (the GPU
 * prim_base only reaches the GS, as on CSF).
 */
#define V9_PRERAST_INDIRECT_CHUNKS 8
/* Bound of the GPU chunks of one direct draw (strips with restart), so that a
 * draw cannot blow up the command buffer. */
#define V9_PRERAST_DIRECT_CHUNKS 1024

static void
v9_prerast_chunked(struct panvk_cmd_buffer *cmdbuf,
                   const struct panvk_draw_info *draw, uint32_t max_verts,
                   uint32_t chunks)
{
   const bool indexed = draw->index.index_size != 0;
   struct pan_ptr cd =
      panvk_cmd_alloc_dev_mem(cmdbuf, desc, sizeof(struct panlib_chunk_draw), 16);
   if (!cd.gpu) {
      vk_command_buffer_set_error(&cmdbuf->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      return;
   }

   /* A direct draw gets its draw command written here. */
   uint64_t src = draw->indirect.buffer_dev_addr;
   if (!src) {
      struct pan_ptr rec =
         panvk_cmd_alloc_dev_mem(cmdbuf, desc, 5 * sizeof(uint32_t), 4);
      if (!rec.gpu) {
         vk_command_buffer_set_error(&cmdbuf->vk,
                                     VK_ERROR_OUT_OF_DEVICE_MEMORY);
         return;
      }

      uint32_t *r = rec.cpu;
      r[0] = draw->vertex.count;
      r[1] = draw->instance.count;
      if (indexed) {
         /* JM draw infos carry the index offset in bytes. */
         r[2] = draw->index.offset / draw->index.index_size;
         r[3] = draw->vertex.base;
         r[4] = draw->instance.base;
      } else {
         r[2] = draw->vertex.base;
         r[3] = draw->instance.base;
         r[4] = 0;
      }
      src = rec.gpu;
   }

#define CD(f) (cd.gpu + offsetof(struct panlib_chunk_draw, f))
   struct panlib_chunk_draw *c = cd.cpu;
   memset(c, 0, sizeof(*c));
   c->index_buffer = draw->index.buffer_dev_addr;
   /* v9_count_draw_queries() counted the primitives generated. */
   c->prims_generated = 0;
   c->indexed = indexed;
   c->max_verts = max_verts;
   c->prim = draw->prim;
   c->restart = indexed && draw->index.restart_enable;
   c->index_size = draw->index.index_size;
   c->index_count_el =
      indexed ? draw->index.buffer_size / draw->index.index_size : 0;
   c->view_count = 1;
   c->max_insts = UINT32_MAX;
   /* ponytail: a mid-strip cut of a triangle strip with adjacency changes
    * the winding parity and end adjacency, as the cuts without restart do. */
   c->cut_tail = draw->prim != MESA_PRIM_TRIANGLE_STRIP_ADJACENCY;

   /* What the lowering sees: an indirect draw that is one chunk of the API
    * draw. The chunk start is added to the VS invocation IDs, so the base
    * vertex and base instance stay the API ones. */
   struct panvk_draw_info chunk = *draw;
   chunk.index.offset = 0;
   chunk.vertex.base = 0;
   chunk.vertex.count = 1;
   chunk.instance.base = 0;
   chunk.instance.count = 1;
   chunk.indirect.buffer_dev_addr = CD(draw);
   chunk.indirect.count_buffer_dev_addr = 0;
   chunk.indirect.draw_count = 1;
   chunk.indirect.stride = 5 * sizeof(uint32_t);
   chunk.pstats_counted = true;
   chunk.prims_generated_counted = true;
   chunk.gpu_prerast_prim_base = 0;
   if (cmdbuf->state.gfx.gs.shader)
      chunk.gpu_prerast_prim_base_addr = CD(prim_base);

   if (!v9_prerast_reserve_jobs(cmdbuf, 2))
      return;

   /* The cut happens on the GPU, the CPU cannot tell when: say it once. */
   if (draw->indirect.buffer_dev_addr)
      mesa_logw_once("panvk: v9 indirect gpu_prerast draws (GS, transform "
                     "feedback, polygon mode, viewport select, VS stores) are "
                     "drawn in at most %u chunks of %u vertices; the rest of "
                     "a larger draw is dropped",
                     chunks, max_verts);

   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   panlib_chunk_setup(&ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER, cd.gpu,
                      src, draw->indirect.count_buffer_dev_addr,
                      draw->indirect.draw_index);

   for (uint32_t i = 0; i < chunks; i++) {
      if (!v9_prerast_reserve_jobs(cmdbuf, 2))
         return;

      ctx = panvk_per_arch(precomp_cs)(cmdbuf);
      panlib_chunk_step(&ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER, cd.gpu);
      v9_prerast_draw_one(cmdbuf, &chunk, CD(chunk_vertex));
   }
#undef CD
}

/* Direct draws the arena does not hold are split on the CPU, the CPU half of
 * CSF gpu_prerast_split(); indirect ones on the GPU, see
 * v9_prerast_chunked(). Direct strips with primitive restart over the arena
 * are not lowered.
 */
static void
v9_prerast_draw(struct panvk_cmd_buffer *cmdbuf,
                const struct panvk_draw_info *draw)
{
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader *gs = cmdbuf->state.gfx.gs.shader;
   uint32_t max_verts =
      panvk_gpu_prerast_record_capacity(vs->gpu_prerast.io.record_stride);
   if (gs)
      max_verts = MIN2(max_verts,
                       panvk_gpu_prerast_gs_cap_slots(&gs->gpu_prerast.gs) /
                          gs->gpu_prerast.gs.invocations);

   uint32_t align, overlap, prim_div;
   panlib_chunk_topology(draw->prim, &align, &overlap, &prim_div);

   /* v9_draw_indirect_unrolled() hands over one draw at a time. */
   if (draw->indirect.buffer_dev_addr) {
      if (!draw->fits_arena && v9_prerast_chunkable(draw->prim) &&
          max_verts >= overlap + align)
         v9_prerast_chunked(cmdbuf, draw, max_verts,
                            V9_PRERAST_INDIRECT_CHUNKS);
      else
         v9_prerast_draw_one(cmdbuf, draw, 0);
      return;
   }

   if ((uint64_t)draw->vertex.count * draw->instance.count <= max_verts) {
      v9_prerast_draw_one(cmdbuf, draw, 0);
      return;
   }

   struct panvk_draw_info chunk = *draw;
   if (draw->vertex.count <= max_verts) {
      const uint32_t step = max_verts / draw->vertex.count;
      for (uint32_t i = 0; i < draw->instance.count; i += step) {
         chunk.instance.base = draw->instance.base + i;
         chunk.instance.count = MIN2(step, draw->instance.count - i);
         v9_prerast_draw_one(cmdbuf, &chunk, 0);
      }
      return;
   }

   if (draw->prim == MESA_PRIM_TRIANGLE_FAN &&
       !(draw->index.index_size && draw->index.restart_enable) &&
       max_verts >= 3) {
      v9_prerast_split_fan(cmdbuf, draw, max_verts);
      return;
   }

   const uint32_t step =
      max_verts > overlap ? (max_verts - overlap) / align * align : 0;
   if (!step || draw->prim == MESA_PRIM_TRIANGLE_FAN ||
       draw->prim == MESA_PRIM_PATCHES) {
      v9_prerast_unhandled("vertex count over the arena capacity");
      return;
   }

   /* Restart indices cut the strips where the index buffer says: chunked on
    * the GPU like an indirect draw. With cut_tail every chunk but the last
    * two of an instance moves on by at least step / 2 vertices. */
   if (overlap && draw->index.index_size && draw->index.restart_enable) {
      if (draw->prim == MESA_PRIM_TRIANGLE_STRIP_ADJACENCY) {
         v9_prerast_unhandled("vertex count over the arena capacity");
         return;
      }
      const uint64_t chunks =
         (DIV_ROUND_UP((uint64_t)draw->vertex.count, MAX2(step / 2, 1)) + 2) *
         draw->instance.count;
      if (chunks > V9_PRERAST_DIRECT_CHUNKS) {
         v9_prerast_unhandled("restart strip with too many chunks");
         return;
      }
      v9_prerast_chunked(cmdbuf, draw, max_verts, chunks);
      return;
   }

   chunk.instance.count = 1;
   for (uint32_t inst = 0; inst < draw->instance.count; inst++) {
      chunk.instance.base = draw->instance.base + inst;
      for (uint32_t i = 0; i + overlap < draw->vertex.count; i += step) {
         chunk.vertex.count = MIN2(step + overlap, draw->vertex.count - i);
         if (draw->index.index_size)
            chunk.index.offset = draw->index.offset + i * draw->index.index_size;
         else
            chunk.vertex.base = draw->vertex.base + i;
         chunk.gpu_prerast_prim_base = i / prim_div;
         v9_prerast_draw_one(cmdbuf, &chunk, 0);
      }
   }
}

/* Room for the next jobs in the 16-bit job index space of the batch, and a
 * TLS descriptor for them. */
static bool
v9_prerast_reserve_jobs(struct panvk_cmd_buffer *cmdbuf, uint32_t jobs)
{
   if (!cmdbuf->cur_batch)
      panvk_per_arch(cmd_open_batch)(cmdbuf);

   if (cmdbuf->cur_batch->vtc_jc.job_index + jobs >= UINT16_MAX) {
      panvk_per_arch(cmd_close_batch)(cmdbuf);
      panvk_per_arch(cmd_open_batch)(cmdbuf);
   }

   return panvk_per_arch(cmd_alloc_tls_desc)(cmdbuf, true) == VK_SUCCESS;
}

/* Tessellation on v9 JM. CSF gpu_prerast_tess() runs the chunks of a draw
 * as a command stream loop until the kernels clear "more". JM has no loop:
 * the iterations are unrolled into the job chain, each one the jobs of a CSF
 * iteration, every job a barrier. An iteration past the end of the draw finds
 * nothing left: panlib_tess_step() zeroes the VS and TCS grids, whose patch
 * jobs then make them NULL jobs, the tessellator kernels return at once and
 * panlib_tess_draw() writes an empty draw that the draw patch makes a NULL
 * job. No CPU reads anything back.
 *
 * Chunks are sized on the GPU from the real tessellation levels: a chunk
 * plans up to V9_TESS_CHUNK_PATCHES patches, and panlib_tess_draw() keeps the
 * longest run of them whose indices fit the index budget of the arena
 * (idx_limit), handing the rest to the next chunk. Low levels thus take few
 * iterations, the worst case (every patch at level 64) still fits.
 *
 * Direct draws get the iterations of that worst case, at most
 * V9_TESS_MAX_ITERATIONS; indirect draws V9_TESS_INDIRECT_ITERATIONS. A
 * draw whose real output needs more loses the rest.
 * ponytail: fixed bounds, JM has no job chain loop.
 *
 * When the TES output goes through gpu_prerast again (GS, transform
 * feedback, viewport select, TES stores), an iteration lowers at most one
 * arena of tessellator indices (range), a chunk over it takes several
 * iterations. After Noysz/panvk-g99-jm 0062/0065 (libpoly kernels in the
 * vertex/tiler chain), on the shared chunked kernels of csf-v11.
 */
#define V9_TESS_CHUNK_PATCHES 4096
#define V9_TESS_INDIRECT_ITERATIONS 32
#define V9_TESS_MAX_ITERATIONS 1024
/* Jobs of one iteration, including a nested GS lowering and its draw. */
#define V9_TESS_ITERATION_JOBS 32

/* The v9 tess arena: per-patch TCS outputs, counts and point allocations for
 * the patches of a chunk, then an index budget shared by the chunk and the
 * domain points of those indices. A patch has no more domain points than
 * indices, so the heap holds the points of any run of patches that fits the
 * index budget. Returns the patches per chunk, 0 if the arena is too small.
 */
static uint32_t
v9_tess_layout(uint64_t arena, uint32_t tcs_stride_B, uint32_t vs_record_stride,
               uint32_t patch_size, struct panvk_gpu_prerast_tess_layout *l)
{
   const uint64_t data = PANVK_GPU_PRERAST_TESS_ARENA_SIZE -
                         PANVK_GPU_PRERAST_TESS_DATA_OFFSET - 4 * 16;
   const uint64_t per_patch =
      tcs_stride_B + 2 * sizeof(uint32_t) + 16 /* point alloc alignment */;
   const uint32_t patches = MIN3(
      panvk_gpu_prerast_record_capacity(vs_record_stride) / patch_size,
      V9_TESS_CHUNK_PATCHES, data / 2 / per_patch);
   uint64_t off = arena + PANVK_GPU_PRERAST_TESS_DATA_OFFSET;

   if (!patches)
      return 0;

   l->tcs = off;
   off += ALIGN_POT((uint64_t)patches * tcs_stride_B, 16);
   l->counts = off;
   off += ALIGN_POT((uint64_t)patches * sizeof(uint32_t), 16);
   l->coord_allocs = off;
   off += ALIGN_POT((uint64_t)patches * sizeof(uint32_t), 16);

   const uint64_t left =
      arena + PANVK_GPU_PRERAST_TESS_ARENA_SIZE - off - patches * 16 - 64;
   const uint64_t indices =
      left / (sizeof(uint32_t) + sizeof(struct poly_tess_point));
   if (indices < PANVK_GPU_PRERAST_TESS_MAX_INDICES)
      return 0;

   l->heap = off;
   l->heap_size =
      indices * sizeof(struct poly_tess_point) + patches * 16 + 16;
   off += ALIGN_POT((uint64_t)l->heap_size, 16);
   l->index = off;
   l->index_size = indices * sizeof(uint32_t);
   assert(off + l->index_size <= arena + PANVK_GPU_PRERAST_TESS_ARENA_SIZE);
   return patches;
}

static void
v9_tess_draw(struct panvk_cmd_buffer *cmdbuf,
             const struct panvk_draw_info *draw)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const struct panvk_shader *vs = cmdbuf->state.gfx.vs.shader;
   const struct panvk_shader *tcs = cmdbuf->state.gfx.tcs.shader;
   const struct panvk_shader *tes = cmdbuf->state.gfx.tes.shader;
   const struct panvk_shader *gs = cmdbuf->state.gfx.gs.shader;
   const uint64_t arena = panvk_priv_mem_dev_addr(dev->gpu_prerast_tess_arena);

   if (!vs || !tcs || !tes || !tcs->gpu_prerast.tess.compiled ||
       !vs->gpu_prerast.lowered_vs_compiled || !arena) {
      v9_prerast_unhandled("tessellation shaders missing");
      return;
   }

   const struct panvk_gpu_prerast_tess_info *tc = &tcs->gpu_prerast.tess;
   const struct panvk_gpu_prerast_tess_info *te = &tes->gpu_prerast.tess;
   const uint32_t domain =
      te->domain != PANVK_GPU_PRERAST_TESS_UNSET ? te->domain : tc->domain;
   const uint32_t spacing =
      te->spacing != PANVK_GPU_PRERAST_TESS_UNSET ? te->spacing : tc->spacing;
   const bool points = te->point_mode || tc->point_mode;
   const bool ccw =
      (te->ccw || tc->ccw) !=
      (dyns->ts.domain_origin == VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT);
   const uint32_t ps = MAX2(dyns->ts.patch_control_points, 1);
   struct panvk_gpu_prerast_tess_layout l;
   const uint32_t max_patches = v9_tess_layout(
      arena, tc->stride_el * 4, vs->gpu_prerast.io.record_stride, ps, &l);

   if (domain == PANVK_GPU_PRERAST_TESS_UNSET ||
       spacing == PANVK_GPU_PRERAST_TESS_UNSET || !max_patches) {
      v9_prerast_unhandled("tessellation state");
      return;
   }

   const bool indirect = draw->indirect.buffer_dev_addr != 0;
   const bool indexed = draw->index.index_size != 0;
   if (!indirect && (draw->vertex.count < ps || !draw->instance.count))
      return;

   /* The draw of the tessellator output, one index range per iteration. */
   struct panvk_draw_info out = *draw;
   out.prim = points        ? MESA_PRIM_POINTS
              : domain == 2 ? MESA_PRIM_LINES
                            : MESA_PRIM_TRIANGLES;
   out.index.index_size = 4;
   out.index.offset = 0;
   out.index.restart_enable = false;
   out.vertex.base = 0;
   out.vertex.count = 1;
   out.instance.base = 0;
   out.instance.count = 1;
   out.indirect.count_buffer_dev_addr = 0;
   out.indirect.draw_count = 1;
   out.indirect.draw_index = 0;
   out.pstats_counted = true;
   out.prims_generated_counted = true;
   out.gpu_prerast_prim_base = 0;
   out.gpu_prerast_prim_base_addr = 0;
   /* An index range is at most range indices, see below. */
   out.fits_arena = true;

   const uint32_t vpp = mesa_vertices_per_prim(out.prim);

   /* What v9_cmd_draw_layer() does with the TES output. */
   cmdbuf->state.gfx.vs.shader = tes;
   const bool nested = v9_prerast_needed(cmdbuf, &out) ||
                       v9_prerast_side_effects(cmdbuf, &out);
   cmdbuf->state.gfx.vs.shader = vs;

   uint32_t range = 0;
   if (nested) {
      /* Same vertex budget as v9_prerast_draw(). */
      uint32_t verts =
         panvk_gpu_prerast_record_capacity(tes->gpu_prerast.io.record_stride);
      if (gs)
         verts = MIN2(verts, panvk_gpu_prerast_gs_cap_slots(&gs->gpu_prerast.gs) /
                                MAX2(gs->gpu_prerast.gs.invocations, 1));
      range = verts / vpp * vpp;
      if (!range) {
         v9_prerast_unhandled("tessellation output range");
         return;
      }
   }

   /* Worst case of the GPU chunk plan: every patch at the maximum level, so
    * a chunk keeps min_keep patches. A chunk cut inside an instance costs one
    * more for the rest of that instance, and a chunk over the range one more
    * iteration per range of indices. */
   const uint32_t idx_limit = l.index_size / sizeof(uint32_t);
   const uint32_t min_keep = MAX2(
      MIN2(max_patches, idx_limit / PANVK_GPU_PRERAST_TESS_MAX_INDICES), 1);
   uint64_t iters = V9_TESS_INDIRECT_ITERATIONS;
   if (!indirect) {
      const uint32_t ppi = draw->vertex.count / ps;
      const uint64_t patches = (uint64_t)ppi * draw->instance.count;
      const uint64_t planned =
         ppi <= max_patches
            ? DIV_ROUND_UP((uint64_t)draw->instance.count,
                           MIN2(max_patches / ppi, PANLIB_CHUNK_MAX_INSTANCES))
            : (uint64_t)draw->instance.count *
                 DIV_ROUND_UP((uint64_t)ppi, max_patches);

      iters = planned + 2 * DIV_ROUND_UP(patches, min_keep);
      if (range)
         iters += DIV_ROUND_UP(patches * PANVK_GPU_PRERAST_TESS_MAX_INDICES,
                               range);
      if (iters > V9_TESS_MAX_ITERATIONS) {
         mesa_logw_once("panvk: v9 tessellated draws get at most %u "
                        "iterations of up to %u patches; the output past "
                        "them is dropped",
                        V9_TESS_MAX_ITERATIONS, max_patches);
         iters = V9_TESS_MAX_ITERATIONS;
      }
   } else {
      mesa_logw_once("panvk: v9 indirect tessellated draws get at most %u "
                     "iterations of up to %u patches; the output past them "
                     "is dropped",
                     V9_TESS_INDIRECT_ITERATIONS, max_patches);
   }

   struct pan_ptr td = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(struct panlib_tess_draw), 16);
   /* Direct draws have no app buffer to read the counts from. */
   struct pan_ptr direct_draw = {0};
   if (!indirect)
      direct_draw =
         panvk_cmd_alloc_dev_mem(cmdbuf, desc, 5 * sizeof(uint32_t), 4);
   if (!td.gpu || (!indirect && !direct_draw.gpu)) {
      vk_command_buffer_set_error(&cmdbuf->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      return;
   }

#define TD(f) (td.gpu + offsetof(struct panlib_tess_draw, f))
   const struct panvk_pstats_query_state *pstats =
      &cmdbuf->state.gfx.pstats_query;
   struct panlib_tess_draw *t = td.cpu;

   /* ponytail: the TES invocation count is a 32-bit add from the
    * tessellator, it wraps past 4G domain points per query.
    */
   memset(t, 0, sizeof(*t));
   t->p = (struct poly_tess_params){
      .heap = TD(heap),
      .patch_coord_buffer = l.heap,
      .coord_allocs = l.coord_allocs,
      .tcs_buffer = l.tcs,
      .counts = l.counts,
      .index_buffer = l.index,
      .statistic = panvk_pstats_counter_addr(
         pstats,
         VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT,
         true),
      .tcs_per_vertex_outputs = tc->per_vertex_outputs,
      .input_patch_size = ps,
      .output_patch_size = tc->out_patch_size,
      .tcs_patch_constants = tc->patch_constants,
      .tcs_stride_el = tc->stride_el,
      .partitioning = spacing,
      .points_mode = points,
      .isolines = domain == 2,
      .ccw = ccw,
   };
   t->heap.base = l.heap;
   t->heap.size = l.heap_size;
   t->input_patch_size = ps;
   t->max_patches = max_patches;
   t->indexed = indexed;
   t->range = range;
   t->verts_per_prim = vpp;
   t->idx_limit = idx_limit;

   uint64_t src = draw->indirect.buffer_dev_addr;
   if (!indirect) {
      uint32_t *rec = direct_draw.cpu;

      rec[0] = draw->vertex.count;
      rec[1] = draw->instance.count;
      if (indexed) {
         /* JM draw infos carry the index offset in bytes. */
         rec[2] = draw->index.offset / draw->index.index_size;
         rec[3] = draw->vertex.base;
         rec[4] = draw->instance.base;
      } else {
         rec[2] = draw->vertex.base;
         rec[3] = draw->instance.base;
         rec[4] = 0;
      }
      src = direct_draw.gpu;
   }

   if (!v9_prerast_reserve_jobs(cmdbuf, V9_TESS_ITERATION_JOBS))
      return;

   /* TCS descriptors, shared by every iteration. */
   struct panvk_descriptor_state *desc_state = &cmdbuf->state.gfx.desc_state;
   struct panvk_shader_desc_state *tcs_desc = &cmdbuf->state.gfx.tcs.desc;
   const struct panvk_shader_variant *tcs_v = panvk_shader_only_variant(tcs);
   if (panvk_per_arch(cmd_prepare_push_descs)(
          cmdbuf, desc_state, tcs->desc_info.used_set_mask) != VK_SUCCESS ||
       v9_prepare_stage_driver_set(cmdbuf, tcs, tcs_desc) != VK_SUCCESS ||
       panvk_per_arch(cmd_prepare_shader_res_table)(
          cmdbuf, desc_state, &tcs->desc_info, tcs_desc, 1) != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmdbuf->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      return;
   }

   cmdbuf->state.gfx.sysvals.gpu_prerast_tess = TD(p);
   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   panlib_tess_setup(&ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER, td.gpu,
                     src, draw->indirect.count_buffer_dev_addr,
                     indirect ? draw->indirect.draw_index : 0);

   /* The part of the API draw the VS runs over, see panlib_tess_step(). */
   struct panvk_draw_info vs_src = *draw;
   vs_src.indirect.buffer_dev_addr = TD(vs_draw);
   vs_src.indirect.count_buffer_dev_addr = 0;
   vs_src.indirect.draw_count = 1;
   vs_src.instance.base = 0;

   out.index.buffer_dev_addr = l.index;
   out.index.buffer_size = l.index_size;
   out.indirect.buffer_dev_addr = TD(draw);
   /* The GS primitive ID continues over the iterations of a chunk. */
   if (gs)
      out.gpu_prerast_prim_base_addr = TD(gs_prim_base);

   const enum panlib_barrier patch_barrier =
      (enum panlib_barrier)(PANLIB_BARRIER_JM_BARRIER |
                            PANLIB_BARRIER_JM_SUPPRESS_PREFETCH);
   const struct panlib_precomp_grid tess_grid =
      panlib_1d(DIV_ROUND_UP(max_patches, 64));
   const uint64_t prims_generated =
      gs ? 0 : cmdbuf->state.gfx.prims_generated_query.ptr;
   const uint64_t c_invs =
      gs ? 0
         : panvk_pstats_counter_addr(
              pstats, VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT,
              true);
   const uint64_t c_prims =
      gs ? 0
         : panvk_pstats_counter_addr(
              pstats, VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT,
              true);
   const uint64_t tcs_stat = panvk_pstats_counter_addr(
      pstats, VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT,
      true);
   t->tcs_stat = tcs_stat;

   bool ok = true;
   for (uint32_t it = 0; ok && it < iters; it++) {
      ok = v9_prerast_reserve_jobs(cmdbuf, V9_TESS_ITERATION_JOBS);
      if (!ok)
         break;

      panlib_tess_step(&ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER, td.gpu,
                       tcs_stat);

      /* VS over the chunk, sized by its patch job. */
      struct v9_prerast_vs_job vsj;
      ok = v9_prerast_vs(cmdbuf, &vs_src, arena, &vsj) &&
           v9_prerast_indirect(cmdbuf, &vs_src, &vsj, NULL, TD(chunk_vertex),
                               0);
      if (!ok)
         break;
      v9_prerast_add_job(cmdbuf, &vsj.job);

      /* TCS, one workgroup per patch of the chunk. */
      struct pan_ptr push;
      ok = panvk_per_arch(cmd_prepare_gfx_push_uniforms)(cmdbuf, tcs_v, &push,
                                                         1) == VK_SUCCESS;
      if (!ok)
         break;
      if (shader_uses_sysval(tcs_v, graphics, gpu_prerast_params))
         *(uint64_t *)((uint8_t *)push.cpu +
                       shader_remapped_sysval_offset(
                          tcs_v, sysval_offset(graphics, gpu_prerast_params))) =
            vsj.params;
      if (shader_uses_sysval(tcs_v, graphics, gpu_prerast_tess))
         *(uint64_t *)((uint8_t *)push.cpu +
                       shader_remapped_sysval_offset(
                          tcs_v, sysval_offset(graphics, gpu_prerast_tess))) =
            TD(p);
      struct pan_ptr tcs_job = v9_prerast_job(
         cmdbuf, tcs_v, tcs_desc->res_table, push.gpu, max_patches, 1, 1, true);
      ok = tcs_job.gpu != 0;
      if (!ok)
         break;
      panlib_v9_compute_job_grid(&ctx, panlib_1d(1), patch_barrier,
                                 tcs_job.gpu, TD(tcs_grid));
      v9_prerast_add_job(cmdbuf, &tcs_job);

      panlib_tessellate(&ctx, tess_grid, PANLIB_BARRIER_JM_BARRIER, TD(p),
                        domain, POLY_TESS_MODE_COUNT);
      panlib_tess_draw(&ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER, td.gpu,
                       c_invs, c_prims, prims_generated);
      panlib_tessellate(&ctx, tess_grid, PANLIB_BARRIER_JM_BARRIER, TD(p),
                        domain, POLY_TESS_MODE_WITH_COUNTS);

      /* This iteration's index range with the TES as the VS: a MALLOC_VERTEX
       * draw, or another gpu_prerast lowering when nested. */
      struct panvk_draw_info raster = out;
      cmdbuf->state.gfx.vs.shader = tes;
      gfx_state_set_dirty(cmdbuf, VS);
      gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
      v9_cmd_draw_layer(cmdbuf, &raster);
      cmdbuf->state.gfx.vs.shader = vs;
      gfx_state_set_dirty(cmdbuf, VS);
      gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
   }
#undef TD

   if (!ok)
      vk_command_buffer_set_error(&cmdbuf->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
}

static void
v9_cmd_draw_layer(struct panvk_cmd_buffer *cmdbuf, struct panvk_draw_info *info)
{
   /* The tess and gpu_prerast paths call get_fs() too. */
   cmdbuf->state.gfx.fs.required =
      fs_required(&cmdbuf->state.gfx, &cmdbuf->vk.dynamic_graphics_state);

   if (info->prim == MESA_PRIM_PATCHES) {
      v9_tess_draw(cmdbuf, info);
      return;
   }

   if (v9_prerast_needed(cmdbuf, info) ||
       v9_prerast_side_effects(cmdbuf, info)) {
      v9_prerast_draw(cmdbuf, info);
      return;
   }

   const struct panvk_shader_variant *vs =
      v9_raster_vs_variant(cmdbuf, info->gpu_prerast_generated);
   if (!vs || !panvk_priv_mem_check_alloc(vs->spds.pos_triangles))
      return;


   const struct panvk_shader_variant *fs = panvk_shader_only_variant(get_fs(cmdbuf));

   if (!cmdbuf->cur_batch)
      panvk_per_arch(cmd_open_batch)(cmdbuf);

   /* There are only 16 bits in the job header for the job index, and a draw
    * takes up to two jobs (indirect patch + MALLOC_VERTEX). */
   if (cmdbuf->cur_batch->vtc_jc.job_index + 4 >= UINT16_MAX) {
      panvk_per_arch(cmd_close_batch)(cmdbuf);
      panvk_per_arch(cmd_open_batch)(cmdbuf);
   }

   if (cmdbuf->state.gfx.render.first_provoking_vertex == U_TRISTATE_UNSET) {
      cmdbuf->state.gfx.render.first_provoking_vertex = u_tristate_make(
         cmdbuf->vk.dynamic_graphics_state.rs.provoking_vertex ==
         VK_PROVOKING_VERTEX_MODE_FIRST_VERTEX_EXT);
      if (cmdbuf->state.gfx.render.first_provoking_vertex == U_TRISTATE_UNSET)
         cmdbuf->state.gfx.render.first_provoking_vertex = U_TRISTATE_YES;
   }

   const struct vk_rasterization_state *rs = &cmdbuf->vk.dynamic_graphics_state.rs;
   VkResult result;
   if (!rs->rasterizer_discard_enable) {
      result = panvk_per_arch(cmd_alloc_fb_desc)(cmdbuf);
      if (result != VK_SUCCESS)
         return;
   }
   panvk_per_arch(cmd_select_tile_size)(cmdbuf);
   result = panvk_per_arch(cmd_alloc_tls_desc)(cmdbuf, true);
   if (result != VK_SUCCESS)
      return;
   result = panvk_per_arch(cmd_prepare_tiler_context)(cmdbuf, info->layer_id);
   if (result != VK_SUCCESS)
      return;

   struct panvk_descriptor_state *desc_state = &cmdbuf->state.gfx.desc_state;
   const struct panvk_shader_desc_info *vs_info = &cmdbuf->state.gfx.vs.shader->desc_info;
   uint32_t used = vs_info->used_set_mask;
   if (fs)
      used |= cmdbuf->state.gfx.fs.shader->desc_info.used_set_mask;
   result = panvk_per_arch(cmd_prepare_push_descs)(cmdbuf, desc_state, used);
   if (result != VK_SUCCESS)
      return;
   result = v9_prepare_vs_driver_set(cmdbuf, info->instance.base);
   if (result != VK_SUCCESS)
      return;
   result = panvk_per_arch(cmd_prepare_shader_res_table)(
      cmdbuf, desc_state, vs_info, &cmdbuf->state.gfx.vs.desc, 1);
   if (result != VK_SUCCESS)
      return;
   if (fs) {
      result = v9_prepare_fs_driver_set(cmdbuf);
      if (result != VK_SUCCESS)
         return;
      result = panvk_per_arch(cmd_prepare_shader_res_table)(
         cmdbuf, desc_state, &cmdbuf->state.gfx.fs.shader->desc_info,
         &cmdbuf->state.gfx.fs.desc, 1);
      if (result != VK_SUCCESS)
         return;
   }

   /*
    * blend_emit_descs() updates fs.blend_descs[], which are copied into
    * fragment push uniforms by cmd_prepare_draw_sysvals().  Prepare blend
    * descriptors before serializing push uniforms.
    */
   uint64_t blend_gpu = 0, zsd_gpu = 0;
   result = v9_prepare_blend_zsd(cmdbuf, fs, &blend_gpu, &zsd_gpu);
   if (result != VK_SUCCESS)
      return;

   panvk_per_arch(cmd_prepare_draw_sysvals)(cmdbuf, info, fs);

   struct pan_ptr vs_push;
   result = panvk_per_arch(cmd_prepare_gfx_push_uniforms)(cmdbuf, vs, &vs_push, 1);
   if (result != VK_SUCCESS)
      return;
   cmdbuf->state.gfx.vs.push_uniforms = vs_push.gpu;

   if (fs) {
      struct pan_ptr fs_push;
      result = panvk_per_arch(cmd_prepare_gfx_push_uniforms)(cmdbuf, fs, &fs_push, 1);
      if (result != VK_SUCCESS)
         return;
      cmdbuf->state.gfx.fs.push_uniforms = fs_push.gpu;
   }
   (void)0;
   result = v9_emit_malloc_vertex_job(cmdbuf, info, vs, fs, blend_gpu, zsd_gpu);
   if (result != VK_SUCCESS)
      return;
   clear_dirty_after_draw(cmdbuf);
   if (info->indirect.buffer_dev_addr)
      gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
}

/* Pipeline statistics and primitives generated queries of one API draw. JM
 * has no command stream to add with, so a panlib_update_pstats_draw()
 * workgroup counts every draw on the GPU, direct ones from a
 * VkDrawIndexedIndirectCommand made here. Same counters as CSF
 * cmd_pstats_draw(): the GS and tessellation kernels count their own output
 * (clipping and primitives generated).
 */
static void
v9_count_draw_queries(struct panvk_cmd_buffer *cmdbuf,
                      const struct panvk_draw_info *draw)
{
#define PSTAT(name) VK_QUERY_PIPELINE_STATISTIC_##name##_BIT
   const struct panvk_pstats_query_state *q = &cmdbuf->state.gfx.pstats_query;
   const bool kernels_count = cmdbuf->state.gfx.gs.shader ||
                              cmdbuf->state.gfx.tes.shader;
   const uint64_t prims_generated =
      kernels_count ? 0 : cmdbuf->state.gfx.prims_generated_query.ptr;
   uint32_t counted = 0;

   if (q->report) {
      counted = q->mask & (PSTAT(INPUT_ASSEMBLY_VERTICES) |
                           PSTAT(INPUT_ASSEMBLY_PRIMITIVES) |
                           PSTAT(VERTEX_SHADER_INVOCATIONS));
      if (!kernels_count)
         counted |= q->mask & (PSTAT(CLIPPING_INVOCATIONS) |
                               PSTAT(CLIPPING_PRIMITIVES));
   }

   if (!counted && !prims_generated)
      return;

   if (!v9_prerast_reserve_jobs(cmdbuf, 4))
      return;

   const uint32_t index_size = draw->index.index_size;
   uint64_t cmd = draw->indirect.buffer_dev_addr;
   uint32_t cmd_stride = draw->indirect.stride;
   uint32_t draw_count = draw->indirect.draw_count;

   if (!cmd) {
      struct pan_ptr ptr = panvk_cmd_alloc_dev_mem(
         cmdbuf, desc, sizeof(VkDrawIndexedIndirectCommand), 16);
      if (!ptr.gpu)
         return;

      /* JM index offsets are in bytes. */
      *(VkDrawIndexedIndirectCommand *)ptr.cpu = (VkDrawIndexedIndirectCommand){
         .indexCount = draw->vertex.count,
         .instanceCount = draw->instance.count,
         .firstIndex = index_size ? draw->index.offset / index_size : 0,
         .vertexOffset = draw->vertex.base,
         .firstInstance = draw->instance.base,
      };
      cmd = ptr.gpu;
      cmd_stride = 0;
      draw_count = 1;
   }

   const uint32_t view_mask = cmdbuf->state.gfx.render.view_mask;
   struct panlib_update_pstats_draw_args args = {
      .cmd = cmd,
      .cmd_stride = cmd_stride,
      .count_buffer = draw->indirect.count_buffer_dev_addr,
      .draw_index = draw->indirect.draw_index,
      .index_buffer = cmdbuf->state.gfx.ib.dev_addr,
      .index_buffer_size_el =
         index_size ? cmdbuf->state.gfx.ib.size / index_size : 0,
      .index_size_B = index_size,
      .restart = index_size && draw->index.restart_enable,
      .prim = draw->prim,
      .view_count = view_mask ? util_bitcount(view_mask) : 1,
      .patch_size = cmdbuf->vk.dynamic_graphics_state.ts.patch_control_points,
   };
   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);

   if (counted) {
      static const uint32_t order[] = {
         PSTAT(INPUT_ASSEMBLY_VERTICES), PSTAT(INPUT_ASSEMBLY_PRIMITIVES),
         PSTAT(VERTEX_SHADER_INVOCATIONS), PSTAT(CLIPPING_INVOCATIONS),
         PSTAT(CLIPPING_PRIMITIVES),
      };

      args.stats = q->report + util_bitcount(q->mask) * sizeof(uint64_t);
      args.slots = 0;
      for (uint32_t i = 0; i < ARRAY_SIZE(order); i++) {
         uint32_t slot = (counted & order[i])
                            ? util_bitcount(q->mask & (order[i] - 1))
                            : 0xf;
         args.slots |= slot << (i * 4);
      }
      panlib_update_pstats_draw_struct(&ctx, panlib_1d(draw_count),
                                       PANLIB_BARRIER_JM_BARRIER, args);
   }

   /* The primitives generated query is the IA primitives counter alone. */
   if (prims_generated) {
      args.stats = prims_generated;
      args.slots = 0xfff0f;
      panlib_update_pstats_draw_struct(&ctx, panlib_1d(draw_count),
                                       PANLIB_BARRIER_JM_BARRIER, args);
   }
#undef PSTAT
}

/* Multiview and layered rendering, same scheme as the Bifrost path: the draw
 * is recorded once per view (or per framebuffer layer when the VS writes
 * gl_Layer), each copy with its own tiler context and the view or layer in
 * the layer_id sysval. The fragment jobs are emitted per layer when the
 * batch is closed. */
/* variableMultisampleRate: an attachment-less pass takes the sample count of
 * its draws. The FBD and tiler context of a batch have one, so a draw with
 * another count closes the batch (its fragment job keeps the old count) and
 * the next one is laid out with the new count. Secondaries are replayed into
 * the primary on JM, so this covers them too. The JM form of csf-v11
 * 074/095/114. */
static void
v9_attachmentless_samples(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_rendering_state *render = &cmdbuf->state.gfx.render;
   const struct vk_dynamic_graphics_state *dyns =
      &cmdbuf->vk.dynamic_graphics_state;
   const uint32_t samples = MAX2(dyns->ms.rasterization_samples, 1);

   if (render->bound_attachments || dyns->rs.rasterizer_discard_enable ||
       samples == render->fb.nr_samples)
      return;

   if (cmdbuf->cur_batch && cmdbuf->cur_batch->fb.desc.gpu) {
      panvk_per_arch(cmd_close_batch)(cmdbuf);
      panvk_per_arch(cmd_open_batch)(cmdbuf);
   }
   render->fb.nr_samples = samples;
   /* cmd_select_tile_size() picks the tile size for the new count. */
   render->fb.layout.sample_count = 0;
}

/* VK_EXT_conditional_rendering on JM. A job chain has no predication, so a
 * draw or dispatch recorded in a conditional rendering block is preceded by
 * a panlib_v9_cond_render job that reads the predicate and gives each job of
 * the draw its recorded type (passes) or NULL (fails). The jobs are only
 * known after the draw is recorded: cmd_cond_render_end() walks the
 * vertex/tiler chains from the predicate job, makes the jobs of its batch
 * depend on it (the predicate job also suppresses prefetch, as the indirect
 * patch jobs do), and hands it the list. Jobs in batches the draw opened run
 * in later atoms. Pipeline state is emitted per draw on JM, so a skipped
 * draw consumes nothing later draws need. */
void
panvk_per_arch(cmd_cond_render_begin)(struct panvk_cmd_buffer *cmdbuf,
                                      struct panvk_cond_render_mark *m)
{
   *m = (struct panvk_cond_render_mark){0};
   if (!cmdbuf->state.cond_render.enabled)
      return;

   struct pan_ptr list_ptr =
      panvk_cmd_alloc_dev_mem(cmdbuf, desc, sizeof(uint64_t), 8);
   if (!list_ptr.gpu)
      return;
   *(uint64_t *)list_ptr.cpu = 0;

   if (cmdbuf->cur_batch &&
       cmdbuf->cur_batch->vtc_jc.job_index + 2 >= UINT16_MAX) {
      panvk_per_arch(cmd_close_batch)(cmdbuf);
      panvk_per_arch(cmd_open_batch)(cmdbuf);
   }

   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   panlib_v9_cond_render(&ctx, panlib_1d(1),
                         PANLIB_BARRIER_JM_SUPPRESS_PREFETCH,
                         cmdbuf->state.cond_render.addr,
                         cmdbuf->state.cond_render.inverted, list_ptr.gpu);

   m->batch = cmdbuf->cur_batch;
   m->job = m->batch->vtc_jc.prev_job;
   m->index = m->batch->vtc_jc.job_index;
   m->list = list_ptr.cpu;
}

/* Job descriptors all come from desc_pool. */
static struct mali_job_header_packed *
v9_job_cpu(struct panvk_cmd_buffer *cmdbuf, uint64_t gpu)
{
   struct list_head *lists[] = {&cmdbuf->desc_pool.bos,
                                &cmdbuf->desc_pool.big_bos};

   for (unsigned l = 0; l < ARRAY_SIZE(lists); l++) {
      list_for_each_entry(struct panvk_priv_bo, bo, lists[l], node) {
         if (gpu >= bo->addr.dev &&
             gpu < bo->addr.dev + pan_kmod_bo_size(bo->bo))
            return (void *)((uint8_t *)bo->addr.host + (gpu - bo->addr.dev));
      }
   }

   return NULL;
}

/* Appends (address, type) of the chain jobs from next on. pred_index != 0:
 * same chain as the predicate job, add a dependency on it. */
static void
v9_cond_render_collect(struct panvk_cmd_buffer *cmdbuf,
                       struct util_dynarray *jobs, uint64_t next,
                       unsigned pred_index)
{
   while (next) {
      struct mali_job_header_packed *hdr = v9_job_cpu(cmdbuf, next);
      assert(hdr);
      if (!hdr)
         return;

      pan_unpack(hdr, JOB_HEADER, h);
      util_dynarray_append(jobs, next);
      util_dynarray_append(jobs, (uint64_t)h.type);

      if (pred_index) {
         if (!h.dependency_1)
            h.dependency_1 = pred_index;
         else if (!h.dependency_2)
            h.dependency_2 = pred_index;
         else if (h.dependency_1 < pred_index && h.dependency_2 < pred_index)
            h.barrier = true;

         pan_pack(hdr, JOB_HEADER, cfg) {
            cfg = h;
         }
      }

      next = h.next;
   }
}

void
panvk_per_arch(cmd_cond_render_end)(struct panvk_cmd_buffer *cmdbuf,
                                    struct panvk_cond_render_mark *m)
{
   if (!m->batch)
      return;

   struct util_dynarray jobs = UTIL_DYNARRAY_INIT;
   pan_unpack(m->job, JOB_HEADER, pred);
   v9_cond_render_collect(cmdbuf, &jobs, pred.next, m->index);

   /* The draw closed the predicate job's batch: add the batches after it. */
   if (m->batch != cmdbuf->cur_batch) {
      for (struct list_head *n = m->batch->node.next; n != &cmdbuf->batches;
           n = n->next) {
         struct panvk_batch *b = list_entry(n, struct panvk_batch, node);
         v9_cond_render_collect(cmdbuf, &jobs, b->vtc_jc.first_job, 0);
      }
      if (cmdbuf->cur_batch)
         v9_cond_render_collect(cmdbuf, &jobs,
                                cmdbuf->cur_batch->vtc_jc.first_job, 0);
   }

   const uint32_t n = util_dynarray_num_elements(&jobs, uint64_t);
   struct pan_ptr list =
      panvk_cmd_alloc_dev_mem(cmdbuf, desc, (1 + n) * sizeof(uint64_t), 8);
   if (list.gpu) {
      uint64_t *l = list.cpu;
      l[0] = n / 2;
      if (n)
         memcpy(&l[1], jobs.data, n * sizeof(uint64_t));
      *m->list = list.gpu;
   }

   util_dynarray_fini(&jobs);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBeginConditionalRendering2EXT)(
   VkCommandBuffer commandBuffer,
   const VkConditionalRenderingBeginInfo2EXT *pConditionalRenderingBegin)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   cmdbuf->state.cond_render.enabled = true;
   cmdbuf->state.cond_render.addr =
      pConditionalRenderingBegin->addressRange.address;
   cmdbuf->state.cond_render.inverted =
      pConditionalRenderingBegin->flags &
      VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdEndConditionalRenderingEXT)(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   cmdbuf->state.cond_render.enabled = false;
}

static void
v9_cmd_draw(struct panvk_cmd_buffer *cmdbuf, struct panvk_draw_info *info)
{
   struct panvk_cond_render_mark cond;
   panvk_per_arch(cmd_cond_render_begin)(cmdbuf, &cond);

   v9_attachmentless_samples(cmdbuf);

   uint32_t view_mask = cmdbuf->state.gfx.render.view_mask;
   uint32_t layer_count = view_mask ? util_bitcount(view_mask)
                                    : cmdbuf->state.gfx.render.layer_count;
   /* The last pre-rasterization stage writes gl_Layer. */
   const struct panvk_shader *vs = cmdbuf->state.gfx.gs.shader
                                      ? cmdbuf->state.gfx.gs.shader
                                   : info->prim == MESA_PRIM_PATCHES
                                      ? cmdbuf->state.gfx.tes.shader
                                      : cmdbuf->state.gfx.vs.shader;

   /* Without a gl_Layer write, primitives go to layer 0 only. */
   if (!view_mask && (!vs || !vs->writes_layer))
      layer_count = 1;

   /* Counted once per API draw, not per layer. */
   if (!info->pstats_counted)
      v9_count_draw_queries(cmdbuf, info);

   /* A layered draw re-runs the GS and tessellation kernels per layer: only
    * the first run counts into the queries. Views count, as on CSF. */
   const bool layered = !view_mask && layer_count > 1;
   const struct panvk_pstats_query_state pstats =
      cmdbuf->state.gfx.pstats_query;
   const uint64_t prims_generated =
      cmdbuf->state.gfx.prims_generated_query.ptr;

   for (uint32_t i = 0; i < MAX2(layer_count, 1); i++) {
      info->layer_id = view_mask ? u_bit_scan(&view_mask) : i;
      if (layered && i == 1) {
         cmdbuf->state.gfx.pstats_query.report = 0;
         cmdbuf->state.gfx.prims_generated_query.ptr = 0;
      }
      v9_cmd_draw_layer(cmdbuf, info);
   }

   cmdbuf->state.gfx.pstats_query = pstats;
   cmdbuf->state.gfx.prims_generated_query.ptr = prims_generated;

   panvk_per_arch(cmd_cond_render_end)(cmdbuf, &cond);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDraw)(VkCommandBuffer commandBuffer, uint32_t vertexCount,
                        uint32_t instanceCount, uint32_t firstVertex,
                        uint32_t firstInstance)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   if (instanceCount == 0 || vertexCount == 0)
      return;
   struct panvk_draw_info info = {
      .vertex.base = firstVertex,
      .vertex.count = vertexCount,
      .instance.base = firstInstance,
      .instance.count = instanceCount,
      .prim = panvk_get_client_prim(cmdbuf),
   };
   v9_cmd_draw(cmdbuf, &info);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndexed)(VkCommandBuffer commandBuffer,
                               uint32_t indexCount, uint32_t instanceCount,
                               uint32_t firstIndex, int32_t vertexOffset,
                               uint32_t firstInstance)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   if (instanceCount == 0 || indexCount == 0)
      return;

   uint32_t index_size = cmdbuf->state.gfx.ib.index_size;
   if (index_size == 0 || cmdbuf->state.gfx.ib.size == 0) {
      (void)0;
      return;
   }

   struct panvk_draw_info info = {
      .index = panvk_draw_info_index(cmdbuf, firstIndex * index_size),
      .vertex.base = vertexOffset,
      .vertex.count = indexCount,
      .instance.base = firstInstance,
      .instance.count = instanceCount,
      .prim = panvk_get_client_prim(cmdbuf),
   };
   v9_cmd_draw(cmdbuf, &info);
}

/* JM has no multi-draw descriptor: drawCount (or maxDrawCount) is unrolled
 * into one indirect patch + MALLOC_VERTEX pair per draw, each with its own
 * push-uniform block (the patch writes sysvals in it) and gl_DrawID. For the
 * *Count variants the patch turns draws at or past the GPU-side count into
 * NULL jobs. */
static void
v9_draw_indirect_unrolled(struct panvk_cmd_buffer *cmdbuf,
                          struct panvk_buffer *buffer, VkDeviceSize offset,
                          uint32_t draw_count, uint32_t stride, bool indexed,
                          uint64_t count_buffer_dev_addr)
{
   if (indexed &&
       (cmdbuf->state.gfx.ib.index_size == 0 || cmdbuf->state.gfx.ib.size == 0))
      return;

   for (uint32_t i = 0; i < draw_count; i++) {
      struct panvk_draw_info info = {
         .vertex.count = 1,
         .instance.count = 1,
         .indirect.buffer_dev_addr =
            panvk_buffer_gpu_ptr(buffer, offset + (uint64_t)i * stride),
         .indirect.count_buffer_dev_addr = count_buffer_dev_addr,
         .indirect.draw_count = 1,
         .indirect.draw_index = i,
         .prim = panvk_get_client_prim(cmdbuf),
      };
      if (indexed)
         info.index = (__typeof__(info.index))panvk_draw_info_index(cmdbuf, 0);

      gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
      v9_cmd_draw(cmdbuf, &info);
      gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndirect)(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                                VkDeviceSize offset, uint32_t drawCount,
                                uint32_t stride)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_buffer, buffer, _buffer);

   v9_draw_indirect_unrolled(cmdbuf, buffer, offset, drawCount, stride, false,
                             0);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndexedIndirect)(VkCommandBuffer commandBuffer,
                                       VkBuffer _buffer, VkDeviceSize offset,
                                       uint32_t drawCount, uint32_t stride)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_buffer, buffer, _buffer);

   v9_draw_indirect_unrolled(cmdbuf, buffer, offset, drawCount, stride, true,
                             0);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndirectCount)(VkCommandBuffer commandBuffer,
                                     VkBuffer _buffer, VkDeviceSize offset,
                                     VkBuffer countBuffer,
                                     VkDeviceSize countBufferOffset,
                                     uint32_t maxDrawCount, uint32_t stride)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_buffer, buffer, _buffer);
   VK_FROM_HANDLE(panvk_buffer, count_buffer, countBuffer);

   v9_draw_indirect_unrolled(
      cmdbuf, buffer, offset, maxDrawCount, stride, false,
      panvk_buffer_gpu_ptr(count_buffer, countBufferOffset));
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndexedIndirectCount)(VkCommandBuffer commandBuffer,
                                            VkBuffer _buffer,
                                            VkDeviceSize offset,
                                            VkBuffer countBuffer,
                                            VkDeviceSize countBufferOffset,
                                            uint32_t maxDrawCount,
                                            uint32_t stride)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_buffer, buffer, _buffer);
   VK_FROM_HANDLE(panvk_buffer, count_buffer, countBuffer);

   v9_draw_indirect_unrolled(
      cmdbuf, buffer, offset, maxDrawCount, stride, true,
      panvk_buffer_gpu_ptr(count_buffer, countBufferOffset));
}

/* VK_EXT_transform_feedback on v9 JM: CSF's entrypoints with the counter
 * loads and stores done by one-thread jobs instead of cs loads/stores.
 * After Noysz/panvk-g99-jm 0060/0064. */
VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBindTransformFeedbackBuffersEXT)(
   VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount,
   const VkBuffer *pBuffers, const VkDeviceSize *pOffsets,
   const VkDeviceSize *pSizes)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   for (uint32_t i = 0; i < bindingCount; i++) {
      VK_FROM_HANDLE(panvk_buffer, buffer, pBuffers[i]);
      uint64_t size = vk_buffer_range(&buffer->vk, pOffsets[i],
                                      pSizes ? pSizes[i] : VK_WHOLE_SIZE);

      cmdbuf->state.gfx.xfb.buffers[firstBinding + i].addr =
         panvk_buffer_gpu_ptr(buffer, pOffsets[i]);
      cmdbuf->state.gfx.xfb.buffers[firstBinding + i].size =
         MIN2(size, UINT32_MAX);
   }
}

/* Counter addresses of buffers 0..3, 0 when not given; returns the mask of
 * the given ones. */
static uint32_t
v9_xfb_counters(uint32_t first, uint32_t count, const VkBuffer *buffers,
                const VkDeviceSize *offsets,
                uint64_t addr[PANLIB_XFB_MAX_BUFFERS])
{
   uint32_t mask = 0;

   for (uint32_t i = 0; i < PANLIB_XFB_MAX_BUFFERS; i++) {
      addr[i] = 0;
      if (!buffers || i < first || i - first >= count)
         continue;

      VK_FROM_HANDLE(panvk_buffer, buffer, buffers[i - first]);
      if (!buffer)
         continue;

      addr[i] = panvk_buffer_gpu_ptr(buffer, offsets ? offsets[i - first] : 0);
      mask |= BITFIELD_BIT(i);
   }
   return mask;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBeginTransformFeedbackEXT)(
   VkCommandBuffer commandBuffer, uint32_t firstCounterBuffer,
   uint32_t counterBufferCount, const VkBuffer *pCounterBuffers,
   const VkDeviceSize *pCounterBufferOffsets)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   uint64_t c[PANLIB_XFB_MAX_BUFFERS];
   uint32_t mask = v9_xfb_counters(firstCounterBuffer, counterBufferCount,
                                   pCounterBuffers, pCounterBufferOffsets, c);
   struct pan_ptr offsets = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, PANLIB_XFB_MAX_BUFFERS * sizeof(uint32_t), 16);

   if (!offsets.gpu)
      return;

   /* Always a job, also with no counter buffer: the captures advance the
    * offsets, a resubmitted command buffer must start from 0 again. */
   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   panlib_v9_xfb_begin(&ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER,
                       offsets.gpu, mask, c[0], c[1], c[2], c[3]);

   cmdbuf->state.gfx.xfb.offsets = offsets.gpu;
   cmdbuf->state.gfx.xfb.active = true;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdEndTransformFeedbackEXT)(
   VkCommandBuffer commandBuffer, uint32_t firstCounterBuffer,
   uint32_t counterBufferCount, const VkBuffer *pCounterBuffers,
   const VkDeviceSize *pCounterBufferOffsets)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   const uint64_t offsets = cmdbuf->state.gfx.xfb.offsets;
   uint64_t c[PANLIB_XFB_MAX_BUFFERS];
   uint32_t mask = v9_xfb_counters(firstCounterBuffer, counterBufferCount,
                                   pCounterBuffers, pCounterBufferOffsets, c);

   cmdbuf->state.gfx.xfb.active = false;
   cmdbuf->state.gfx.xfb.offsets = 0;
   if (!offsets || !mask)
      return;

   /* A barrier job: it runs after every capture of the batch. */
   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   panlib_v9_xfb_end(&ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER, offsets,
                     mask, c[0], c[1], c[2], c[3]);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndirectByteCountEXT)(
   VkCommandBuffer commandBuffer, uint32_t instanceCount,
   uint32_t firstInstance, VkBuffer counterBuffer,
   VkDeviceSize counterBufferOffset, uint32_t counterOffset,
   uint32_t vertexStride)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_buffer, counter, counterBuffer);

   if (!instanceCount)
      return;

   struct pan_ptr draw = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, sizeof(VkDrawIndirectCommand), 16);
   if (!draw.gpu)
      return;

   /* The indirect patch job of the draw is a barrier, it waits for this. */
   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   panlib_xfb_byte_count_draw(
      &ctx, panlib_1d(1), PANLIB_BARRIER_JM_BARRIER,
      panvk_buffer_gpu_ptr(counter, counterBufferOffset), counterOffset,
      vertexStride, instanceCount, firstInstance, draw.gpu);

   struct panvk_draw_info info = {
      .vertex.count = 1,
      .instance.count = 1,
      .indirect.buffer_dev_addr = draw.gpu,
      .indirect.draw_count = 1,
      .prim = panvk_get_client_prim(cmdbuf),
   };

   gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
   v9_cmd_draw(cmdbuf, &info);
   gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
}

#endif /* PAN_ARCH != 9 */

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBeginRendering)(VkCommandBuffer commandBuffer,
                                  const VkRenderingInfo *pRenderingInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_cmd_graphics_state *state = &cmdbuf->state.gfx;
   bool resuming = pRenderingInfo->flags & VK_RENDERING_RESUMING_BIT;

   /* When resuming from a suspended pass, the state should be unchanged. */
   if (resuming && cmdbuf->cur_batch) {
      state->render.flags = pRenderingInfo->flags;
   } else {
      /* If we're not resuming, cur_batch should be NULL.  However, this
       * currently isn't true because of how events are implemented.
       *
       * XXX: Rewrite events to not close and open batch and add an assert here.
       */
      if (cmdbuf->cur_batch)
         panvk_per_arch(cmd_close_batch)(cmdbuf);

      panvk_per_arch(cmd_init_render_state)(cmdbuf, pRenderingInfo);
      cmdbuf->state.gfx.render.fb.needs_load = !resuming;
   }

   if (!cmdbuf->cur_batch)
      panvk_per_arch(cmd_open_batch)(cmdbuf);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdEndRendering2KHR)(
   VkCommandBuffer commandBuffer,
   UNUSED const VkRenderingEndInfoKHR *pRenderingEndInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   if (!(cmdbuf->state.gfx.render.flags & VK_RENDERING_SUSPENDING_BIT)) {
      const struct pan_fb_load *fb_load = &cmdbuf->state.gfx.render.fb.load;
      bool always_load = fb_load->z.always || fb_load->s.always;
      for (unsigned rt = 0; rt < PAN_MAX_RTS; rt++) {
         if (fb_load->rts[rt].always)
            always_load = true;
      }

      if (always_load)
         panvk_per_arch(cmd_alloc_fb_desc)(cmdbuf);

      cmdbuf->state.gfx.render.fb.needs_store = true;

      panvk_per_arch(cmd_close_batch)(cmdbuf);
      cmdbuf->cur_batch = NULL;
      panvk_per_arch(cmd_meta_resolve_attachments)(cmdbuf);
#if PAN_ARCH == 9
      /* Outside the pass, timestamp and occlusion queries take one slot. */
      cmdbuf->state.gfx.render.view_mask = 0;
#endif
   }
}
