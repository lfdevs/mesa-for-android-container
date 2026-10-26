/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_CMD_DRAW_H
#define PANVK_CMD_DRAW_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include "panvk_blend.h"
#include "panvk_cmd_desc_state.h"
#include "panvk_cmd_query.h"
#include "panvk_entrypoints.h"
#include "panvk_image.h"
#include "panvk_image_view.h"
#include "panvk_perf_knobs.h"
#include "panvk_physical_device.h"
#include "panvk_shader.h"

#include "vk_command_buffer.h"
#include "vk_format.h"
#include "vk_meta.h"
#include "util/u_tristate.h"

#include "pan_fb.h"
#include "pan_props.h"

#define MAX_VBS 16

#if PAN_ARCH < 10
#define MAX_FRAMEBUFFER_DIMENSION (1 << 14)
#elif PAN_ARCH < 11
#define MAX_FRAMEBUFFER_DIMENSION (1 << 15)
#else
#define MAX_FRAMEBUFFER_DIMENSION (1 << 16)
#endif

#define MAX_FRAMEBUFFER_LAYERS 256

struct panvk_cmd_buffer;

struct panvk_attrib_buf {
   uint64_t address;
   unsigned size;
};

struct panvk_resolve_attachment {
   VkResolveModeFlagBits mode;
   VkRenderingAttachmentFlagsKHR flags;
   struct panvk_image_view *dst_iview;
};

struct panvk_rendering_state {
   VkRenderingFlags flags;
   uint32_t layer_count;
   uint32_t view_mask;
   enum u_tristate first_provoking_vertex;

   enum vk_rp_attachment_flags bound_attachments;
   struct {
      struct panvk_image_view *iviews[MAX_RTS];
      /* If non-null, preload_iviews[i] overrides iviews[i] for preloads. */
      struct panvk_image_view *preload_iviews[MAX_RTS];
      VkFormat fmts[MAX_RTS];
      uint8_t samples[MAX_RTS];
      struct panvk_resolve_attachment resolve[MAX_RTS];
   } color_attachments;

   struct {
      struct panvk_image_view *iview;
      /* If non-null, preload_iview overrides iview for preloads. */
      struct panvk_image_view *preload_iview;
      VkFormat fmt;
      struct panvk_resolve_attachment resolve;
   } z_attachment, s_attachment;

   /* Used for separate Z/S images */
   struct {
      struct pan_image_view load, spill, store, resolve;
   } z_pview, s_pview;

   struct {
      struct pan_fb_layout layout;
      struct pan_fb_load load;
      struct pan_fb_resolve resolve;
      struct pan_fb_store store;
      struct {
         struct pan_fb_load load;
         struct pan_fb_store store;
      } spill;

      /* nr_samples to be used before framebuffer / tiler descriptor are emitted */
      uint32_t nr_samples;

#if PAN_ARCH < 9
      uint32_t bo_count;
      struct pan_kmod_bo *bos[(MAX_RTS * PANVK_MAX_PLANES) + 2];
#endif
#if PAN_ARCH < 10
      bool needs_load;
      bool needs_store;
#endif
   } fb;

#if PAN_ARCH >= 10
   struct pan_ptr fbds;
   uint64_t tiler;

   /* When a secondary command buffer has to flush draws, it disturbs the
    * inherited context, and the primary command buffer needs to know. */
   bool invalidate_inherited_ctx;

   /* Attachment-less secondary: rasterizationSamples of the CS segment being
    * recorded, the count the primary has to give the inherited context. A
    * secondary cannot change the context in the middle, it cuts its CS where
    * the count changes (cmd_cut_vmr_segment). */
   uint8_t vmr_samples;

   /* True if the last render pass was suspended. */
   bool suspended;

   /* Blocks that can patch to flip the provoking vertex mode if we need to
    * emit FBDs/TDs before we know which mode the application is using */
   struct cs_maybe *maybe_set_tds_provoking_vertex;
   struct cs_maybe *maybe_set_fbds_provoking_vertex;

   struct {
      /* != 0 if the render pass contains one or more occlusion queries to
       * signal. */
      uint64_t chain;

      /* Point to the syncobj of the last occlusion query that was passed
       * to a draw. */
      uint64_t last;
   } oq;

   /* Pipeline statistics queries that ended inside this render pass and
    * are signalled once its fragment jobs are issued. */
   struct panvk_pstats_pending pstats_pending[PANVK_PSTATS_MAX_PENDING];
   uint32_t pstats_pending_count;

   struct {
      uint64_t fbds[3];
   } ir;
#endif
};

enum panvk_cmd_graphics_dirty_state {
   PANVK_CMD_GRAPHICS_DIRTY_BASE_INSTANCE,
   PANVK_CMD_GRAPHICS_DIRTY_VS,
   PANVK_CMD_GRAPHICS_DIRTY_FS,
   PANVK_CMD_GRAPHICS_DIRTY_VB,
   PANVK_CMD_GRAPHICS_DIRTY_OQ,
   PANVK_CMD_GRAPHICS_DIRTY_DESC_STATE,
   PANVK_CMD_GRAPHICS_DIRTY_RENDER_STATE,
   PANVK_CMD_GRAPHICS_DIRTY_VS_PUSH_UNIFORMS,
   PANVK_CMD_GRAPHICS_DIRTY_FS_PUSH_UNIFORMS,
   PANVK_CMD_GRAPHICS_DIRTY_IDVS,
#if PAN_ARCH < 9
   PANVK_CMD_GRAPHICS_DIRTY_GS,
#endif
   PANVK_CMD_GRAPHICS_DIRTY_STATE_COUNT,
};

#if PAN_ARCH >= 10
/* C4 (115): the memory that the fragment jobs issued since the fragment
 * subqueue last waited for all of its jobs may have written: every plane
 * of their attachments, as page-aligned address ranges. A pass that loads
 * none of it does not need that wait before its preload
 * (cmd_init_render_state()). Only valid once this command buffer did such
 * a wait, since the fragment jobs of earlier command buffers can still run
 * until then. Executing secondaries clears it with the rest of the
 * graphics state.
 */
struct panvk_frag_written {
   bool valid;
   /* Out of ranges, or an attachment with an unknown range. */
   bool overflow;
   uint8_t count;
   struct {
      uint64_t start, end;
   } ranges[16];
};
#endif

struct panvk_cmd_graphics_state {
   struct panvk_descriptor_state desc_state;
#if PAN_ARCH < 9
   /* jm-v7 097: the sets changed since the last draw, valid only while
    * every DESC_STATE dirtying since then came through a set bind or push
    * (otherwise DESC_STATE means every set).
    */
   uint32_t v7_dirty_sets;
   bool v7_dirty_sets_valid;
   /* jm-v7 100: availability words of occlusion queries ended in the
    * current batch, written once it closes (after its fragment job).
    */
#define PANVK_V7_MAX_DEFERRED_QUERY_ENDS 64
   uint64_t v7_query_ends[PANVK_V7_MAX_DEFERRED_QUERY_ENDS];
   uint32_t v7_query_end_count;
#endif
#if PAN_ARCH >= 10
   /* A2 (115): the graphics sets whose binding changed since the last draw,
    * exact only while every DESC_STATE dirtying since then came through a
    * set bind or push (otherwise DESC_STATE means every set). Reset by
    * clear_dirty_after_draw().
    */
   uint32_t csf_dirty_sets;
   bool csf_dirty_sets_valid;
#endif

   struct {
      struct vk_vertex_input_state vi;
      struct vk_sample_locations_state sl;
   } dynamic;

   struct panvk_occlusion_query_state occlusion_query;
   struct panvk_prims_generated_query_state prims_generated_query;
   struct panvk_pstats_query_state pstats_query;
#if PAN_ARCH >= 9

   struct {
      bool active;
      uint64_t offsets;
      struct {
         uint64_t addr;
         uint32_t size;
      } buffers[4];
      uint64_t queries[4];
   } xfb;
#else
   /* VK_EXT_transform_feedback, captured from the geometry shader
    * emulation. The write offsets live in GPU memory: each capturing draw
    * reads `offsets` and leaves a fresh slot for the next. last_job orders
    * the capture jobs of the current batch; closing the batch resets it.
    */
   struct {
      bool active;
      uint64_t offsets;
      struct {
         uint64_t addr;
         uint32_t size;
      } buffers[4];
      unsigned last_job;
      /* Active VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT reports, per
       * vertex stream (jm-v7 016).
       */
      uint64_t queries[4];
   } xfb;
#endif
   struct panvk_graphics_sysvals sysvals;

#if PAN_ARCH < 9
   struct panvk_shader_link link;
#endif

   struct {
      const struct panvk_shader *shader;
      struct panvk_shader_desc_state desc;
      uint64_t blend_descs[MAX_RTS];
      uint64_t push_uniforms;
      bool required;
#if PAN_ARCH >= 10
      /* A1 (115): a draw since the last FS emission did not run the bound
       * FS (fs_required() false). The next draw that runs it marks FS and
       * FS_PUSH_UNIFORMS dirty, see clear_dirty_after_draw().
       */
      bool unused;
#endif
#if PAN_ARCH < 9
      uint64_t rsd;
#endif
   } fs;

   struct {
      enum mesa_prim prim;
      bool restart;
   } idvs;

   struct {
      const struct panvk_shader *shader;
      struct panvk_shader_desc_state desc;
      uint64_t push_uniforms;
#if PAN_ARCH < 9
      uint64_t attribs;
      uint64_t attrib_bufs;
      uint64_t indirect_attribs_infos;
      uint64_t indirect_attrib_bufs_infos;
      uint64_t indirect_varying_bufs_infos;
      /* jm-v7 097: what the direct draw's attribute tables (attribs,
       * attrib_bufs) were built for, so an identical draw reuses them.
       */
      struct {
         uint32_t padded_vertex_count;
         uint32_t instance_base;
         uint32_t instance_count;
         int32_t raw_offset;
         uint32_t attrib_count;
         bool valid;
      } attr_key;
#else
      /* The number of times desc is repeated.  Zero means that it is not
       * repeated but also that it's re-usable across draws and we're not
       * allowed to patch it from the CSF.
       */
      uint32_t desc_repeat_count;
#endif
   } vs;

#if PAN_ARCH >= 9
   struct {
      const struct panvk_shader *shader;
      struct panvk_shader_desc_state desc;
   } gs, tcs, tes;
#else
   /* Geometry shaders are emulated by a compute job between the vertex and
    * tiler jobs (panvk_nir_lower_gs.c). The buffers are the ones that job
    * writes and the tiler then reads.
    */
   struct {
      const struct panvk_shader *shader;
      /* What the emulation runs for the current draw: the bound geometry
       * shader, or the passthrough of a clip-space vertex shader
       * (panvk_passthrough_gs()), or NULL.
       */
      const struct panvk_shader *emu;
      struct panvk_shader_desc_state desc;
      uint64_t out_pos;
      uint64_t out_general;
      uint64_t out_index;
      uint64_t in_general;
      /* gl_PointSize (jm-v7 033): the vertex shader's fp16 point sizes
       * (0 when it writes none) and the emulation's, for point output.
       */
      uint64_t in_psiz;
      uint64_t out_psiz;
      uint64_t in_offsets;
      uint64_t out_offsets;
      uint64_t viewports;
      uint32_t out_vertex_count;
      uint32_t out_index_count;
      /* DRAW.instance_primitive_size of the emulation's tiler jobs: the
       * hardware primitive ID (FS gl_PrimitiveID) wraps at it (jm-v7 031).
       */
      uint32_t out_prims_per_instance;
      enum mesa_prim out_prim;
      /* Transform feedback: the counts and staging records the emulation
       * fills when the draw captures (see panvk_gs_push).
       */
      uint64_t xfb_counts;
      uint64_t xfb_staging;
      uint32_t xfb_invocations;
      uint64_t xfb_stream_index;
      uint32_t xfb_stream_slots;
      uint32_t xfb_stream_idx;
   } gs;

   /* Tessellation (jm-v7 014): the tess control shader runs as a compute
    * job, the tess eval shader as the vertex shader of a second draw.
    */
   struct {
      const struct panvk_shader *shader;
   } tcs, tes;

   /* The tessellated draw being recorded: which part (enum
    * panvk_tess_phase), its struct panlib_v7_tess, the
    * Vk*IndirectCommand the tess setup reads, and the tess control job
    * index once emitted (0 on failure).
    */
   struct {
      uint32_t phase;
      uint64_t td;
      uint64_t src_draw;
      unsigned tcs_job;
      /* jm-v7 106: the nested tess eval draw being recorded takes its
       * geometry emulation buffers from the batch's tess_gs_scratch. */
      bool gs_scratch;
   } tess;
#endif

   struct {
      struct panvk_attrib_buf bufs[MAX_VBS];
      unsigned count;
   } vb;

#if PAN_ARCH >= 10
   struct {
      uint32_t base_instance;
      uint32_t attribs_changing_on_base_instance;
   } vi;
#endif

   /* Index buffer */
   struct {
      uint64_t dev_addr;
      uint64_t size;
      uint8_t index_size;
   } ib;

   struct {
      struct panvk_blend_info info;
   } cb;

   struct panvk_rendering_state render;

   bool vk_meta;

#if PAN_ARCH < 9
   uint64_t vpd;
#endif

#if PAN_ARCH >= 10
   uint64_t tsd;

   struct panvk_frag_written frag_written;
#endif

   BITSET_DECLARE(dirty, PANVK_CMD_GRAPHICS_DIRTY_STATE_COUNT);
};

#if PAN_ARCH >= 9
/* Last pre-raster stage whose passthrough selects a viewport (GS, else
 * TES, else VS), or NULL.
 */
static inline const struct panvk_shader *
panvk_prerast_viewport_shader(const struct panvk_cmd_graphics_state *gfx)
{
   const struct panvk_shader *gs = gfx->gs.shader;

   if (gs)
      return gs->gpu_prerast.gs.scissor_clip_loc ? gs : NULL;

   const struct panvk_shader *last =
      gfx->tes.shader ? gfx->tes.shader : gfx->vs.shader;

   return last && last->gpu_prerast.vp_scissor_loc ? last : NULL;
}
#endif

#define dyn_gfx_state_dirty(__cmdbuf, __name)                                  \
   BITSET_TEST((__cmdbuf)->vk.dynamic_graphics_state.dirty,                    \
               MESA_VK_DYNAMIC_##__name)

#define gfx_state_dirty(__cmdbuf, __name)                                      \
   BITSET_TEST((__cmdbuf)->state.gfx.dirty, PANVK_CMD_GRAPHICS_DIRTY_##__name)

#define gfx_state_set_dirty(__cmdbuf, __name)                                  \
   BITSET_SET((__cmdbuf)->state.gfx.dirty, PANVK_CMD_GRAPHICS_DIRTY_##__name)

#define gfx_state_clear_all_dirty(__cmdbuf)                                    \
   BITSET_ZERO((__cmdbuf)->state.gfx.dirty)

#define gfx_state_set_all_dirty(__cmdbuf)                                      \
   BITSET_ONES((__cmdbuf)->state.gfx.dirty)

#define set_gfx_sysval(__cmdbuf, __dirty, __name, __val)                       \
   do {                                                                        \
      struct panvk_graphics_sysvals __new_sysval;                              \
      __new_sysval.__name = __val;                                             \
      if (memcmp(&(__cmdbuf)->state.gfx.sysvals.__name, &__new_sysval.__name,  \
                 sizeof(__new_sysval.__name))) {                               \
         (__cmdbuf)->state.gfx.sysvals.__name = __new_sysval.__name;           \
         BITSET_SET_RANGE(__dirty, sysval_fau_start(graphics, __name),         \
                          sysval_fau_end(graphics, __name));                   \
      }                                                                        \
   } while (0)

#if PAN_ARCH >= 10 && PAN_ARCH < 14
struct panvk_device_draw_context {
   struct panvk_priv_bo *fns_bo;
   uint64_t fn_set_fbds_provoking_vertex_stride;
};
#endif

/* dEQP-VK.draw.renderpass.inverted_depth_ranges.nodepthclamp_deltazero.
 * A zero depth range flattens clip-space Z before the viewport clip.
 */
#define PANVK_MIN_DEPTH_CLIP_RANGE 37.7E-06f

static inline void
panvk_viewport_z_coeffs(const struct vk_viewport_state *vp,
                        const struct vk_rasterization_state *rs,
                        unsigned index, float *scale_z, float *offset_z)
{
   const VkViewport *viewport = &vp->viewports[index];
   float z_min = viewport->minDepth;
   float z_max = viewport->maxDepth;

   if (vk_rasterization_state_depth_clip_enable(rs)) {
      float dz = z_max - z_min;

      if (dz < 0.0f)
         dz = -dz;
      /* minDepth == maxDepth keeps the exact flat depth (D3D MinZ == MaxZ
       * sky boxes and HUDs). Its window Z cannot cull clip-space Z, so such
       * a viewport does not depth clip: inverted_depth_ranges deltazero
       * draws its out-of-range pixels at minDepth.
       */
      if (dz > 0.0f && dz < PANVK_MIN_DEPTH_CLIP_RANGE) {
         float z_sign = z_min <= z_max ? 1.0f : -1.0f;
         float z_center = 0.5f * (z_max + z_min);
         float lo = 0.5f * PANVK_MIN_DEPTH_CLIP_RANGE;
         float hi = 1.0f - lo;

         if (z_center < lo)
            z_center = lo;
         else if (z_center > hi)
            z_center = hi;
         z_min = z_center - 0.5f * z_sign * PANVK_MIN_DEPTH_CLIP_RANGE;
         z_max = z_center + 0.5f * z_sign * PANVK_MIN_DEPTH_CLIP_RANGE;
      }
   }

   if (vp->depth_clip_negative_one_to_one) {
      *scale_z = 0.5f * (z_max - z_min);
      *offset_z = 0.5f * (z_min + z_max);
   } else {
      *scale_z = z_max - z_min;
      *offset_z = z_min;
   }
}

static inline void
panvk_depth_range(const struct panvk_cmd_graphics_state *state,
                  const struct vk_viewport_state *vp,
                  float *z_min, float *z_max)
{
   if (vp->depth_clamp_mode == VK_DEPTH_CLAMP_MODE_USER_DEFINED_RANGE_EXT) {
      *z_min = vp->depth_clamp_range.minDepthClamp;
      *z_max = vp->depth_clamp_range.maxDepthClamp;
      return;
   }

   float a = vp->depth_clip_negative_one_to_one ?
      state->sysvals.viewport.offset.z - state->sysvals.viewport.scale.z :
      state->sysvals.viewport.offset.z;
   float b = state->sysvals.viewport.offset.z + state->sysvals.viewport.scale.z;
   *z_min = MIN2(a, b);
   *z_max = MAX2(a, b);
}

static inline uint32_t
panvk_select_tiler_hierarchy_mask(const struct panvk_physical_device *phys_dev,
                                  const struct panvk_cmd_graphics_state *state,
                                  unsigned bin_ptr_mem_budget)
{
   struct pan_tiler_features tiler_features =
      pan_query_tiler_features(&phys_dev->kmod.dev->props);

   uint32_t hierarchy_mask = GENX(pan_select_tiler_hierarchy_mask)(
      state->render.fb.layout.width_px, state->render.fb.layout.height_px,
      tiler_features.max_levels, state->render.fb.layout.tile_size_px,
      bin_ptr_mem_budget);

   return hierarchy_mask;
}

static inline bool
fs_required(const struct panvk_cmd_graphics_state *state,
            const struct vk_dynamic_graphics_state *dyn_state)
{
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(state->fs.shader);
   const struct pan_shader_info *fs_info = fs ? &fs->info : NULL;
   const struct vk_color_blend_state *cb = &dyn_state->cb;
   const struct vk_rasterization_state *rs = &dyn_state->rs;

   if (rs->rasterizer_discard_enable || !fs_info)
      return false;

   if (fs->fs.depth_bounds_only)
      return dyn_state->ds.depth.bounds_test.enable &&
             (state->render.bound_attachments &
              MESA_VK_RP_ATTACHMENT_DEPTH_BIT);

   /* If we generally have side effects */
   if (fs_info->fs.sidefx)
      return true;

   /* If colour is written we need to execute */
   for (unsigned i = 0; i < cb->attachment_count; ++i) {
      if ((cb->color_write_enables & BITFIELD_BIT(i)) &&
          cb->attachments[i].write_mask)
         return true;
   }

   /* If alpha-to-coverage is enabled, we need to run the fragment shader even
    * if we don't have a color attachment, so depth/stencil updates can be
    * discarded if alpha, and thus coverage, is 0. */
   if (dyn_state->ms.alpha_to_coverage_enable)
      return true;

   /* If the sample mask is updated, we need to run the fragment shader,
    * otherwise the fixed-function depth/stencil results will apply to all
    * samples. */
   if (fs_info->outputs_written & BITFIELD64_BIT(FRAG_RESULT_SAMPLE_MASK))
      return true;

   /* If depth is written and not implied we need to execute.
    * TODO: Predicate on Z/S writes being enabled */
   return (fs_info->fs.writes_depth || fs_info->fs.writes_stencil);
}

static inline bool
cached_fs_required(ASSERTED const struct panvk_cmd_graphics_state *state,
                   ASSERTED const struct vk_dynamic_graphics_state *dyn_state,
                   bool cached_value)
{
   /* Make sure the cached value was properly initialized. */
   assert(fs_required(state, dyn_state) == cached_value);
   return cached_value;
}

#define get_fs(__cmdbuf)                                                       \
   (cached_fs_required(&(__cmdbuf)->state.gfx,                                 \
                       &(__cmdbuf)->vk.dynamic_graphics_state,                 \
                       (__cmdbuf)->state.gfx.fs.required)                      \
       ? (__cmdbuf)->state.gfx.fs.shader                                       \
       : NULL)

/* Anything that might change the value returned by get_fs() makes users of the
 * fragment shader dirty, because not using the fragment shader (when
 * fs_required() returns false) impacts various other things, like VS -> FS
 * linking in the JM backend, or the update of the fragment shader pointer in
 * the CSF backend. Call gfx_state_dirty(cmdbuf, FS) if you only care about
 * fragment shader updates. */

#define fs_user_dirty(__cmdbuf)                                                \
   (gfx_state_dirty(cmdbuf, FS) ||                                             \
    dyn_gfx_state_dirty(cmdbuf, RS_RASTERIZER_DISCARD_ENABLE) ||               \
    dyn_gfx_state_dirty(cmdbuf, CB_ATTACHMENT_COUNT) ||                        \
    dyn_gfx_state_dirty(cmdbuf, CB_COLOR_WRITE_ENABLES) ||                     \
    dyn_gfx_state_dirty(cmdbuf, CB_WRITE_MASKS) ||                             \
    dyn_gfx_state_dirty(cmdbuf, MS_ALPHA_TO_COVERAGE_ENABLE) ||               \
    dyn_gfx_state_dirty(cmdbuf, DS_DEPTH_BOUNDS_TEST_ENABLE) ||               \
    gfx_state_dirty(cmdbuf, RENDER_STATE))

/* After a draw, all dirty flags are cleared except the FS dirty flag which
 * needs to be set again if the draw didn't use the fragment shader.
 *
 * A1 (115), CSF: since csf-v11 092 a pipeline without a fragment shader has
 * the device depth bounds FS bound, which is not used while the test is off,
 * so that rule re-emitted all the FS-side state (blend, ZSD, DCD, tiler
 * flags, FS registers and descriptors) on every depth-only draw. Every input
 * of fs_required() is in fs_user_dirty(), so a draw that starts using the FS
 * again re-emits it anyway; fs.unused only remembers to mark FS and
 * FS_PUSH_UNIFORMS dirty at that draw (panvk_cmd_draw()), and the draws in
 * between leave the FS state clean. PANVK_CSF_FS_IDLE_CLEAN=0 restores the
 * per-draw re-dirtying.
 */

#if PAN_ARCH >= 10
#define clear_dirty_after_draw(__cmdbuf)                                       \
   do {                                                                        \
      bool __fs_unused =                                                       \
         (__cmdbuf)->state.gfx.fs.shader != get_fs(__cmdbuf);                  \
      bool __idle_clean = __fs_unused && PANVK_PERF(FS_IDLE_CLEAN);            \
      bool __set_fs_push_dirty = __fs_unused && !__idle_clean &&               \
                                 gfx_state_dirty(__cmdbuf, FS_PUSH_UNIFORMS);  \
      vk_dynamic_graphics_state_clear_dirty(                                   \
         &(__cmdbuf)->vk.dynamic_graphics_state);                              \
      gfx_state_clear_all_dirty(__cmdbuf);                                     \
      (__cmdbuf)->state.gfx.csf_dirty_sets_valid = false;                      \
      if (__idle_clean)                                                        \
         (__cmdbuf)->state.gfx.fs.unused = true;                               \
      else if (__fs_unused)                                                    \
         gfx_state_set_dirty(__cmdbuf, FS);                                    \
      if (__set_fs_push_dirty)                                                 \
         gfx_state_set_dirty(__cmdbuf, FS_PUSH_UNIFORMS);                      \
   } while (0)
#else
#define clear_dirty_after_draw(__cmdbuf)                                       \
   do {                                                                        \
      bool __set_fs_dirty =                                                    \
         (__cmdbuf)->state.gfx.fs.shader != get_fs(__cmdbuf);                  \
      bool __set_fs_push_dirty =                                               \
         __set_fs_dirty && gfx_state_dirty(__cmdbuf, FS_PUSH_UNIFORMS);        \
      vk_dynamic_graphics_state_clear_dirty(                                   \
         &(__cmdbuf)->vk.dynamic_graphics_state);                              \
      gfx_state_clear_all_dirty(__cmdbuf);                                     \
      if (__set_fs_dirty)                                                      \
         gfx_state_set_dirty(__cmdbuf, FS);                                    \
      if (__set_fs_push_dirty)                                                 \
         gfx_state_set_dirty(__cmdbuf, FS_PUSH_UNIFORMS);                      \
   } while (0)
#endif

#if PAN_ARCH >= 10 && PAN_ARCH < 14
VkResult
panvk_per_arch(device_draw_context_init)(struct panvk_device *dev);

void
panvk_per_arch(device_draw_context_cleanup)(struct panvk_device *dev);
#endif

void
panvk_per_arch(cmd_init_render_state)(struct panvk_cmd_buffer *cmdbuf,
                                      const VkRenderingInfo *pRenderingInfo);

#if PAN_ARCH >= 9
void panvk_per_arch(cmd_select_db_alt_fs)(struct panvk_cmd_buffer *cmdbuf);
#endif

void panvk_per_arch(cmd_select_tile_size)(struct panvk_cmd_buffer *cmdbuf);

#if PAN_ARCH >= 10
void panvk_per_arch(cmd_track_frag_writes)(struct panvk_cmd_buffer *cmdbuf);
#endif

struct panvk_draw_info {
   struct {
      uint64_t buffer_dev_addr;
      uint64_t buffer_size;
      uint32_t index_size;
      uint32_t offset;
      bool restart_enable;
   } index;

   struct {
#if PAN_ARCH < 9
      int32_t raw_offset;
#endif
      int32_t base;
      uint32_t count;
   } vertex;

   struct {
      int32_t base;
      uint32_t count;
   } instance;

   struct {
      uint64_t buffer_dev_addr;
      uint64_t count_buffer_dev_addr;
      uint32_t draw_count;
      uint32_t stride;
      uint32_t draw_index;
   } indirect;

   enum mesa_prim prim;
   bool gpu_prerast_generated;
   /* INDEX_COUNT and INDEX_OFFSET were loaded from a GPU run header. */
   bool index_sr_set;
   /* Already counted by a pipeline statistics query (split chunks). */
   bool pstats_counted;
   /* The primitives generated query was updated by the tessellation kernels. */
   bool prims_generated_counted;
   uint32_t gpu_prerast_prim_base;

#if PAN_ARCH <= 9
   uint32_t layer_id;
#endif
#if PAN_ARCH == 9
   /* GPU-side gpu_prerast_prim_base (a tessellation output range), or 0. */
   uint64_t gpu_prerast_prim_base_addr;
   /* 1 + the viewport whose depth range clamps this draw, 0 for viewport 0
    * (the draws of one viewport each of a multi-viewport draw). */
   uint32_t viewport_clamp;
   /* An indirect gpu_prerast draw known to fit the arena (a tessellation
    * output range): lowered as is, without GPU chunks. */
   bool fits_arena;
#endif
};

#define panvk_draw_info_index(__cmdbuf, __offset) {                            \
   .buffer_dev_addr = (__cmdbuf)->state.gfx.ib.dev_addr,                       \
   .buffer_size = (__cmdbuf)->state.gfx.ib.size,                               \
   .index_size = (__cmdbuf)->state.gfx.ib.index_size,                          \
   .offset = (__offset),                                                       \
   .restart_enable =                                                           \
      (__cmdbuf)->vk.dynamic_graphics_state.ia.primitive_restart_enable,       \
}

#define panvk_get_client_prim(__cmdbuf) vk_topology_to_mesa(                   \
      (__cmdbuf)->vk.dynamic_graphics_state.ia.primitive_topology)

void
panvk_per_arch(cmd_prepare_draw_sysvals)(struct panvk_cmd_buffer *cmdbuf,
                                         const struct panvk_draw_info *info,
                                         const struct panvk_shader_variant *fs);

static inline uint32_t
color_attachment_written_mask(
   const struct panvk_shader_variant *fs,
   const struct vk_color_attachment_location_state *cal)
{
   uint32_t written_by_shader =
      (fs->info.outputs_written >> FRAG_RESULT_DATA0) & BITFIELD_MASK(8);
   uint32_t catt_written_mask = 0;

   for (uint32_t i = 0; i < MAX_RTS; i++) {
      if (cal->color_map[i] == MESA_VK_ATTACHMENT_UNUSED)
         continue;

      uint32_t shader_rt = cal->color_map[i];

      if (written_by_shader & BITFIELD_BIT(shader_rt))
         catt_written_mask |= BITFIELD_BIT(i);
   }

   return catt_written_mask;
}

static inline uint32_t
color_attachment_read_mask(const struct panvk_shader_variant *fs,
                           const struct vk_input_attachment_location_state *ial,
                           uint8_t color_attachment_mask)
{
   uint32_t color_attachment_count =
      ial->color_attachment_count == MESA_VK_COLOR_ATTACHMENT_COUNT_UNKNOWN
         ? util_last_bit(color_attachment_mask)
         : ial->color_attachment_count;
   uint32_t catt_read_mask = 0;

   for (uint32_t i = 0; i < color_attachment_count; i++) {
      if (ial->color_map[i] == MESA_VK_ATTACHMENT_UNUSED)
         continue;

      uint32_t catt_idx = ial->color_map[i] + 1;
      if (fs->fs.input_attachment_read & BITFIELD_BIT(catt_idx)) {
         assert(color_attachment_mask & BITFIELD_BIT(i));
         catt_read_mask |= BITFIELD_BIT(i);
      }
   }

   catt_read_mask |= fs->fs.tile_image_color_read & color_attachment_mask;

   return catt_read_mask;
}

static inline bool
z_attachment_read(const struct panvk_shader_variant *fs,
                  const struct vk_input_attachment_location_state *ial)
{
   uint32_t depth_mask = ial->depth_att == MESA_VK_ATTACHMENT_NO_INDEX
                            ? BITFIELD_BIT(0)
                         : ial->depth_att != MESA_VK_ATTACHMENT_UNUSED
                            ? BITFIELD_BIT(ial->depth_att + 1)
                            : 0;
   return (depth_mask & fs->fs.input_attachment_read) ||
          fs->fs.tile_image_z_read;
}

static inline bool
s_attachment_read(const struct panvk_shader_variant *fs,
                  const struct vk_input_attachment_location_state *ial)
{
   uint32_t stencil_mask = ial->stencil_att == MESA_VK_ATTACHMENT_NO_INDEX
                              ? BITFIELD_BIT(0)
                           : ial->stencil_att != MESA_VK_ATTACHMENT_UNUSED
                              ? BITFIELD_BIT(ial->stencil_att + 1)
                              : 0;

   return (stencil_mask & fs->fs.input_attachment_read) ||
          fs->fs.tile_image_s_read;
}

#if PAN_ARCH >= 10
void panvk_per_arch(cmd_draw_rects)(struct vk_command_buffer *cmd,
                                    struct vk_meta_device *meta,
                                    uint32_t rect_count,
                                    const struct vk_meta_rect *rects);

void panvk_per_arch(cmd_draw_volume)(struct vk_command_buffer *cmd,
                                     struct vk_meta_device *meta,
                                     const struct vk_meta_rect *rect,
                                     uint32_t layer_count);
#endif

#endif
