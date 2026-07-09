/*
 * Copyright (C) 2021 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef __PAN_DESC_H
#define __PAN_DESC_H

#include "genxml/gen_macros.h"

#include "kmod/pan_kmod.h"
#include "pan_image.h"
#include "pan_pool.h"

struct pan_buffer_view {
   enum pipe_format format;
   struct {
      unsigned narrow;
      unsigned hdr;
   } astc;
   unsigned width_el;
   uint64_t base;
};

struct pan_compute_dim {
   uint32_t x, y, z;
};

struct pan_crc_state {
   /* Pointer to BO mapping. */
   struct pan_ptr *ptr;

   /* Is the CRC buffer valid? Implicitly refers to the first slice. */
   bool valid;
};

struct pan_fb_color_attachment {
   const struct pan_image_view *view;
   struct pan_crc_state *crc_state;
   bool clear;
   bool preload;
   bool discard;
   uint32_t clear_value[4];
};

struct pan_fb_zs_attachment {
   struct {
      const struct pan_image_view *zs, *s;
   } view;

   struct {
      bool z, s;
   } clear;

   struct {
      bool z, s;
   } discard;

   struct {
      bool z, s;
   } preload;

   struct {
      float depth;
      uint8_t stencil;
   } clear_value;
};

struct pan_tiler_context {
   union {
      struct {
         uint64_t desc;
         /* A tiler descriptor can only handle a limited amount of layers.
          * If the number of layers is bigger than this, several tiler
          * descriptors will be issued, each with a different layer_offset.
          *
          * Note: on v14+, only up to one tiler descriptor is supported and
          * layer_offset is deprecated.
          */
         uint8_t layer_offset;
      } valhall;
      struct {
         uint64_t desc;
      } bifrost;
      struct {
         /* Sum of vertex counts (for non-indexed draws), index counts, or ~0 if
          * any indirect draws are used. Helps tune hierarchy masks.
          */
         uint32_t vertex_count;
         bool disable;
         bool no_hierarchical_tiling;
         uint64_t polygon_list;
         struct {
            uint64_t start;
            unsigned size;
         } heap;
      } midgard;
   };
};

struct pan_tls_info {
   struct {
      uint64_t ptr;
      unsigned size;
   } tls;

   struct {
      unsigned instances;
      uint64_t ptr;
      unsigned size;
   } wls;
};

struct pan_fb_bifrost_info {
   struct {
      struct pan_ptr dcds;
      unsigned modes[3];
   } pre_post;
};

struct pan_bbox {
   unsigned minx, miny, maxx, maxy;
};

struct pan_fb_info {
   unsigned width, height;
   /* Draw-extent controlled by viewports/scissors.
    * Max values are exclusive */
   struct pan_bbox draw_extent;
   /* frame_bounding_box controls the bounding box in the framebuffer
    * descriptor for the entire pass. This is being controlled by the
    * renderArea of a renderpass in Vulkan. On GL, this covers the
    * entire frame. Max values are exclusive. */
   struct pan_bbox frame_bounding_box;
   unsigned nr_samples;
   unsigned force_samples; /* samples used for rasterization */
   unsigned rt_count;
   struct pan_fb_color_attachment rts[8];
   struct pan_fb_zs_attachment zs;

   struct {
      unsigned stride;
      uint64_t base;
   } tile_map;

   union {
      struct pan_fb_bifrost_info bifrost;
   };

   /* Optimal tile buffer size. */
   unsigned tile_buf_budget;
   unsigned z_tile_buf_budget;
   unsigned tile_size;
   unsigned cbuf_allocation;

   /* Sample position array. */
   uint64_t sample_positions;

   /* Only used on Valhall */
   bool sprite_coord_origin;
   bool first_provoking_vertex;
   bool allow_hsr_prepass;

   /* indicates whether pixel local storage is enabled */
   bool pls_enabled;
};

struct pan_crc {
   /* Empty Tile Elimination clear color */
   uint64_t clear_color;

   /* Selected RT index (8 max), -1 if none. */
   int8_t index;

   /* Transaction Elimination flags */
   bool read  : 1;
   bool write : 1;

   /* Force clean writes for CRC buffer init */
   bool force_clean_tile_write : 1;

   /* Empty Tile Elimination flags */
   bool empty_tile_read  : 1;
   bool empty_tile_write : 1;
};

static inline bool
pan_crc_is_enabled(struct pan_crc *crc)
{
   return crc->index != -1;
}

struct pan_clean_tile {
   /* clean_tile_write_enable mask on the 8 color attachments. */
   uint8_t write_rt_mask;

   /* clean_tile_write_enable flag on the depth/stencil attachment. */
   uint8_t write_zs : 1;
};

static inline bool
pan_fb_info_is_fully_covered(const struct pan_fb_info *fb)
{
   return !fb->draw_extent.minx &&
      !fb->draw_extent.miny &&
      fb->draw_extent.maxx == (fb->width - 1) &&
      fb->draw_extent.maxy == (fb->height - 1);
}

static inline void
pan_crc_state_invalidate(struct pan_crc_state *state)
{
   state->valid = false;
}

static inline void
pan_crc_state_set_ptr(struct pan_crc_state *state, struct pan_ptr *ptr)
{
   state->ptr = ptr;
}

static inline bool
pan_clean_tile_write_rt_enabled(struct pan_clean_tile clean_tile,
                                unsigned index)
{
   return (clean_tile.write_rt_mask >> index) & 1;
}

static inline bool
pan_clean_tile_write_zs_enabled(struct pan_clean_tile clean_tile)
{
   return clean_tile.write_zs;
}

static inline bool
pan_clean_tile_write_any_set(struct pan_clean_tile clean_tile)
{
   return clean_tile.write_rt_mask || clean_tile.write_zs;
}

static inline unsigned
pan_zsbuf_bytes_per_pixel(const struct pan_fb_info *fb)
{
   unsigned samples = fb->nr_samples;

   const struct pan_image_view *zs_view = fb->zs.view.zs;
   if (zs_view)
      samples = zs_view->nr_samples;

   const struct pan_image_view *s_view = fb->zs.view.s;
   if (s_view)
      samples = MAX2(samples, s_view->nr_samples);

   /* Depth is always stored in a 32-bit float. Stencil requires depth to
    * be allocated, but doesn't have it's own budget; it's tied to the
    * depth buffer.
    */
   return sizeof(float) * samples;
}

static inline unsigned
pan_wls_instances(const struct pan_compute_dim *dim)
{
   return util_next_power_of_two(dim->x) * util_next_power_of_two(dim->y) *
          util_next_power_of_two(dim->z);
}

static inline unsigned
pan_wls_adjust_size(unsigned wls_size)
{
   return util_next_power_of_two(MAX2(wls_size, 128));
}

static inline unsigned
pan_calc_workgroups_per_task(const struct pan_compute_dim *shader_local_size,
                             const struct pan_kmod_dev_props *props)
{
   /* Each shader core can run N tasks and a total of M threads at any single
    * time, thus each task should ideally have no more than M/N threads. */
   unsigned max_threads_per_task =
      props->max_threads_per_core / props->max_tasks_per_core;

   /* To achieve the best utilization, we should aim for as many workgroups
    * per tasks as we can fit without exceeding the above thread limit */
   unsigned threads_per_wg =
      shader_local_size->x * shader_local_size->y * shader_local_size->z;
   assert(threads_per_wg > 0 && threads_per_wg <= props->max_threads_per_wg);
   unsigned wg_per_task = DIV_ROUND_UP(max_threads_per_task, threads_per_wg);
   assert(wg_per_task > 0 && wg_per_task <= max_threads_per_task);

   return wg_per_task;
}

static inline unsigned
pan_calc_wls_instances(const struct pan_compute_dim *shader_local_size,
                       const struct pan_kmod_dev_props *props,
                       const struct pan_compute_dim *dim)
{
   /* NOTE: If the instance count is lower than the number of workgroups
    * being dispatched, the HW will hold back workgroups until instances
    * can be reused. */
   unsigned instances;
   unsigned wg_per_task =
      pan_calc_workgroups_per_task(shader_local_size, props);
   unsigned max_instances_per_core =
      util_next_power_of_two(wg_per_task * props->max_tasks_per_core);

   /* Not passing workgroup dimensions implies indirect compute. */
   if (!dim) {
      /* Assume we utilize all shader cores to the max */
      instances = max_instances_per_core;
   } else {
      /* NOTE: There is no benefit from allocating more instances than what
       * can concurrently be used by the HW */
      instances = MIN2(pan_wls_instances(dim), max_instances_per_core);
   }
   return instances;
}

static inline unsigned
pan_calc_total_wls_size(unsigned wls_size, unsigned wls_instances,
                        unsigned max_core_id_plus_one)
{
   unsigned size = pan_wls_adjust_size(wls_size);

   return size * wls_instances * max_core_id_plus_one;
}

#ifdef PAN_ARCH

static inline enum mali_sample_pattern
pan_sample_pattern(unsigned samples)
{
   switch (samples) {
   case 1:
      return MALI_SAMPLE_PATTERN_SINGLE_SAMPLED;
#if PAN_ARCH >= 12
   case 2:
      return MALI_SAMPLE_PATTERN_ROTATED_2X_GRID;
#endif
   case 4:
      return MALI_SAMPLE_PATTERN_ROTATED_4X_GRID;
   case 8:
      return MALI_SAMPLE_PATTERN_D3D_8X_GRID;
#if PAN_ARCH >= 5
   case 16:
      return MALI_SAMPLE_PATTERN_D3D_16X_GRID;
#endif
   default:
      UNREACHABLE("Unsupported sample count");
   }
}

static inline struct pan_image_block_size
pan_effective_tile_block_size(unsigned tile_size)
{
   /* Tile is either a square or a rect whose width is twice the height. */
   unsigned shift_h = util_logbase2(tile_size);
   unsigned shift_w = shift_h + 1;
   unsigned h = 1 << (shift_h >> 1);
   unsigned w = 1 << (shift_w >> 1);

   return (struct pan_image_block_size){w, h};
}

#if PAN_ARCH >= 6
/* All GPUs starting from Bifrost are affected by issue TSIX-2033:
 *
 *      Forcing clean_tile_writes breaks INTERSECT readbacks
 *
 * To workaround, use the pre-frame shader mode ALWAYS instead of INTERSECT if
 * clean_tile_write_enable is set on either one of the color, depth or stencil
 * buffers. Since INTERSECT is a hint that the hardware may ignore, this
 * cannot affect correctness, only performance. */

static enum mali_pre_post_frame_shader_mode
pan_fix_frame_shader_mode(enum mali_pre_post_frame_shader_mode mode,
                          bool force_clean_tile)
{
   if (force_clean_tile && mode == MALI_PRE_POST_FRAME_SHADER_MODE_INTERSECT)
      return MALI_PRE_POST_FRAME_SHADER_MODE_ALWAYS;
   else
      return mode;
}
#endif

void GENX(pan_select_tile_size)(struct pan_fb_info *fb);

bool GENX(pan_force_clean_write_on)(const struct pan_image *image,
                                    unsigned fb_tile_size_px);

struct pan_clean_tile
   GENX(pan_get_clean_tile_info)(const struct pan_fb_info *fb);

void GENX(pan_emit_tls)(const struct pan_tls_info *info,
                        struct mali_local_storage_packed *out);

int GENX(pan_select_crc_rt)(const struct pan_fb_info *fb, unsigned tile_size);

struct pan_attachment_info {
   const struct pan_image_view *iview;
   unsigned layer_or_z_slice;
   unsigned fb_tile_size_px;
};

#if PAN_ARCH >= 5
void GENX(pan_emit_default_color_attachment)(enum pipe_format format,
                                             void *payload);
void GENX(pan_emit_linear_color_attachment)(const struct pan_attachment_info *att,
                                            void *payload);
void GENX(pan_emit_linear_s_attachment)(const struct pan_attachment_info *att,
                                        void *payload);
void GENX(pan_emit_linear_zs_attachment)(const struct pan_attachment_info *att,
                                         void *payload);
void GENX(pan_emit_u_tiled_color_attachment)(const struct pan_attachment_info *att,
                                             void *payload);
void GENX(pan_emit_u_tiled_s_attachment)(const struct pan_attachment_info *att,
                                         void *payload);
void GENX(pan_emit_u_tiled_zs_attachment)(const struct pan_attachment_info *att,
                                          void *payload);
void GENX(pan_emit_afbc_color_attachment)(const struct pan_attachment_info *att,
                                          void *payload);
void GENX(pan_emit_afbc_zs_attachment)(const struct pan_attachment_info *att,
                                       void *payload);
void GENX(pan_emit_afbc_s_attachment)(const struct pan_attachment_info *att,
                                      void *payload);
#endif

#if PAN_ARCH >= 10
void
GENX(pan_emit_interleaved_64k_color_attachment)(const struct pan_attachment_info *att,
                                                void *payload);
void GENX(pan_emit_interleaved_64k_zs_attachment)(const struct pan_attachment_info *att,
                                                  void *payload);
void GENX(pan_emit_interleaved_64k_s_attachment)(const struct pan_attachment_info *att,
                                                 void *payload);

void GENX(pan_emit_afrc_color_attachment)(const struct pan_attachment_info *att,
                                          void *payload);
#endif

struct pan_fbd_descs {
#if PAN_ARCH <= 13
   struct mali_framebuffer_packed *fbd;
#endif
#if PAN_ARCH >= 5
   struct mali_zs_crc_extension_packed *zs_crc;
   struct mali_render_target_packed *rts;
#endif
};

unsigned GENX(pan_emit_fbd)(const struct pan_fb_info *fb, unsigned layer_idx,
                            const struct pan_tls_info *tls,
                            const struct pan_tiler_context *tiler_ctx,
                            const struct pan_fbd_descs *out);

#if PAN_ARCH >= 6
unsigned GENX(pan_select_tiler_hierarchy_mask)(uint32_t width, uint32_t height,
                                               uint32_t max_levels,
                                               uint32_t tile_size,
                                               uint32_t mem_budget);
#endif

#if PAN_ARCH <= 9
void GENX(pan_emit_fragment_job_payload)(const struct pan_fb_info *fb,
                                         uint64_t fbd, void *out);
#endif

#endif /* ifdef PAN_ARCH */

#endif
