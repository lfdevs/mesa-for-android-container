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
#include "panvk_cmd_oq.h"
#include "panvk_entrypoints.h"
#include "panvk_image.h"
#include "panvk_image_view.h"
#include "panvk_physical_device.h"
#include "panvk_shader.h"

#include "vk_command_buffer.h"
#include "vk_format.h"
#include "util/u_tristate.h"

#include "pan_props.h"

#define MAX_VBS 16
#define MAX_RTS 8

struct panvk_cmd_buffer;

struct panvk_attrib_buf {
   uint64_t address;
   unsigned size;
};

struct panvk_resolve_attachment {
   VkResolveModeFlagBits mode;
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
      VkFormat fmts[MAX_RTS];
      uint8_t samples[MAX_RTS];
      struct panvk_resolve_attachment resolve[MAX_RTS];
   } color_attachments;

   struct pan_image_view zs_pview;
   struct pan_image_view s_pview;

   struct {
      struct panvk_image_view *iview;
      VkFormat fmt;
      struct panvk_resolve_attachment resolve;
   } z_attachment, s_attachment;

   struct {
      struct pan_fb_info info;
      bool crc_valid[MAX_RTS];

      /* nr_samples to be used before framebuffer / tiler descriptor are emitted */
      uint32_t nr_samples;

#if PAN_ARCH < 9
      uint32_t bo_count;
      struct pan_kmod_bo *bos[(MAX_RTS * PANVK_MAX_PLANES) + 2];
#endif
   } fb;

#if PAN_ARCH >= 10
   struct pan_ptr fbds;
   uint64_t tiler;

   /* When a secondary command buffer has to flush draws, it disturbs the
    * inherited context, and the primary command buffer needs to know. */
   bool invalidate_inherited_ctx;

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
#endif
};

enum panvk_cmd_graphics_dirty_state {
   PANVK_CMD_GRAPHICS_DIRTY_VS,
   PANVK_CMD_GRAPHICS_DIRTY_FS,
   PANVK_CMD_GRAPHICS_DIRTY_VB,
   PANVK_CMD_GRAPHICS_DIRTY_IB,
   PANVK_CMD_GRAPHICS_DIRTY_OQ,
   PANVK_CMD_GRAPHICS_DIRTY_DESC_STATE,
   PANVK_CMD_GRAPHICS_DIRTY_RENDER_STATE,
   PANVK_CMD_GRAPHICS_DIRTY_VS_PUSH_UNIFORMS,
   PANVK_CMD_GRAPHICS_DIRTY_FS_PUSH_UNIFORMS,
   PANVK_CMD_GRAPHICS_DIRTY_STATE_COUNT,
};

struct panvk_cmd_graphics_state {
   struct panvk_descriptor_state desc_state;

   struct {
      struct vk_vertex_input_state vi;
      struct vk_sample_locations_state sl;
   } dynamic;

   struct panvk_occlusion_query_state occlusion_query;
   struct panvk_graphics_sysvals sysvals;

#if PAN_ARCH < 9
   struct panvk_shader_link link;
#endif

   struct {
      const struct panvk_shader *shader;
      struct panvk_shader_desc_state desc;
      uint64_t push_uniforms;
      bool required;
#if PAN_ARCH < 9
      uint64_t rsd;
#endif
   } fs;

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
      bool previous_draw_was_indirect;
#endif
   } vs;

   struct {
      struct panvk_attrib_buf bufs[MAX_VBS];
      unsigned count;
   } vb;

#if PAN_ARCH >= 10
   struct {
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
#endif

   BITSET_DECLARE(dirty, PANVK_CMD_GRAPHICS_DIRTY_STATE_COUNT);
};

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

#if PAN_ARCH >= 10
struct panvk_device_draw_context {
   struct panvk_priv_bo *fns_bo;
   uint64_t fn_set_fbds_provoking_vertex_stride;
};
#endif

static inline void
panvk_depth_range(const struct panvk_cmd_graphics_state *state,
                  const struct vk_viewport_state *vp,
                  float *z_min, float *z_max)
{
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
      pan_query_tiler_features(&phys_dev->kmod.props);

   uint32_t hierarchy_mask = GENX(pan_select_tiler_hierarchy_mask)(
      state->render.fb.info.width, state->render.fb.info.height,
      tiler_features.max_levels, state->render.fb.info.tile_size,
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
    dyn_gfx_state_dirty(cmdbuf, MS_ALPHA_TO_COVERAGE_ENABLE))

/* After a draw, all dirty flags are cleared except the FS dirty flag which
 * needs to be set again if the draw didn't use the fragment shader. */

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


#if PAN_ARCH >= 10
VkResult
panvk_per_arch(device_draw_context_init)(struct panvk_device *dev);

void
panvk_per_arch(device_draw_context_cleanup)(struct panvk_device *dev);
#endif

void
panvk_per_arch(cmd_init_render_state)(struct panvk_cmd_buffer *cmdbuf,
                                      const VkRenderingInfo *pRenderingInfo);

void
panvk_per_arch(cmd_force_fb_preload)(struct panvk_cmd_buffer *cmdbuf,
                                     const VkRenderingInfo *render_info);

void
panvk_per_arch(cmd_preload_render_area_border)(struct panvk_cmd_buffer *cmdbuf,
                                               const VkRenderingInfo *render_info);

void panvk_per_arch(cmd_resolve_attachments)(struct panvk_cmd_buffer *cmdbuf);
void panvk_per_arch(cmd_select_tile_size)(struct panvk_cmd_buffer *cmdbuf);

struct panvk_draw_info {
   struct {
      uint32_t size;
      uint32_t offset;
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
   } indirect;

#if PAN_ARCH < 9
   uint32_t layer_id;
#endif
};

void
panvk_per_arch(cmd_prepare_draw_sysvals)(struct panvk_cmd_buffer *cmdbuf,
                                         const struct panvk_draw_info *info);

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
   return depth_mask & fs->fs.input_attachment_read;
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

   return stencil_mask & fs->fs.input_attachment_read;
}

#endif
