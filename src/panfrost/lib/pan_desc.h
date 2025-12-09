/*
 * Copyright (C) 2021 Collabora, Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 *   Boris Brezillon <boris.brezillon@collabora.com>
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

struct pan_fb_color_attachment {
   const struct pan_image_view *view;
   bool *crc_valid;
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

   /* indicates whether pixel local storage is enabled */
   bool pls_enabled;
};

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

void GENX(pan_select_tile_size)(struct pan_fb_info *fb);

void GENX(pan_emit_tls)(const struct pan_tls_info *info,
                        struct mali_local_storage_packed *out);

int GENX(pan_select_crc_rt)(const struct pan_fb_info *fb, unsigned tile_size);

#if PAN_ARCH >= 5
void GENX(pan_emit_linear_color_attachment)(const struct pan_fb_info *fb,
                                            unsigned rt_idx,
                                            unsigned layer_or_z_slice,
                                            unsigned cbuf_offset,
                                            void *payload);
void GENX(pan_emit_linear_s_attachment)(const struct pan_fb_info *fb,
                                        unsigned layer_or_z_slice,
                                        void *payload);
void GENX(pan_emit_linear_zs_attachment)(const struct pan_fb_info *fb,
                                         unsigned layer_or_z_slice,
                                         void *payload);
void GENX(pan_emit_u_tiled_color_attachment)(const struct pan_fb_info *fb,
                                             unsigned rt_idx,
                                             unsigned layer_or_z_slice,
                                             unsigned cbuf_offset,
                                             void *payload);
void GENX(pan_emit_u_tiled_s_attachment)(const struct pan_fb_info *fb,
                                         unsigned layer_or_z_slice,
                                         void *payload);
void GENX(pan_emit_u_tiled_zs_attachment)(const struct pan_fb_info *fb,
                                          unsigned layer_or_z_slice,
                                          void *payload);
void GENX(pan_emit_afbc_color_attachment)(const struct pan_fb_info *fb,
                                          unsigned rt_idx,
                                          unsigned layer_or_z_slice,
                                          unsigned cbuf_offset, void *payload);
void GENX(pan_emit_afbc_zs_attachment)(const struct pan_fb_info *fb,
                                       unsigned layer_or_z_slice,
                                       void *payload);
void GENX(pan_emit_afbc_s_attachment)(const struct pan_fb_info *fb,
                                      unsigned layer_or_z_slice,
                                      void *payload);
#endif

#if PAN_ARCH >= 10
void GENX(pan_emit_afrc_color_attachment)(const struct pan_fb_info *fb,
                                          unsigned rt_idx,
                                          unsigned layer_or_z_slice,
                                          unsigned cbuf_offset, void *payload);
#endif

unsigned GENX(pan_emit_fbd)(const struct pan_fb_info *fb, unsigned layer_idx,
                            const struct pan_tls_info *tls,
                            const struct pan_tiler_context *tiler_ctx,
                            void *out);

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
