/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef ZINK_CONTEXT_H
#define ZINK_CONTEXT_H

#define ZINK_SHADER_COUNT (PIPE_SHADER_TYPES - 1)

#define ZINK_DEFAULT_MAX_DESCS 5000

#include "zink_clear.h"
#include "zink_pipeline.h"
#include "zink_batch.h"
#include "zink_compiler.h"
#include "zink_descriptors.h"
#include "zink_surface.h"

#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/u_rect.h"
#include "util/u_threaded_context.h"

#include "util/slab.h"
#include "util/list.h"
#include "util/u_dynarray.h"

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

struct blitter_context;
struct list_head;

struct zink_blend_state;
struct zink_depth_stencil_alpha_state;
struct zink_gfx_program;
struct zink_rasterizer_state;
struct zink_resource;
struct zink_vertex_elements_state;

enum zink_blit_flags {
   ZINK_BLIT_NORMAL = 1 << 0,
   ZINK_BLIT_SAVE_FS = 1 << 1,
   ZINK_BLIT_SAVE_FB = 1 << 2,
   ZINK_BLIT_SAVE_TEXTURES = 1 << 3,
   ZINK_BLIT_NO_COND_RENDER = 1 << 4,
};

struct zink_sampler_state {
   VkSampler sampler;
   uint32_t hash;
   struct zink_descriptor_refs desc_set_refs;
   struct zink_batch_usage *batch_uses;
   bool custom_border_color;
};

struct zink_buffer_view {
   struct pipe_reference reference;
   VkBufferViewCreateInfo bvci;
   VkBufferView buffer_view;
   uint32_t hash;
   struct zink_batch_usage *batch_uses;
   struct zink_descriptor_refs desc_set_refs;
};

struct zink_sampler_view {
   struct pipe_sampler_view base;
   union {
      struct zink_surface *image_view;
      struct zink_buffer_view *buffer_view;
   };
};

struct zink_image_view {
   struct pipe_image_view base;
   union {
      struct zink_surface *surface;
      struct zink_buffer_view *buffer_view;
   };
};

static inline struct zink_sampler_view *
zink_sampler_view(struct pipe_sampler_view *pview)
{
   return (struct zink_sampler_view *)pview;
}

struct zink_so_target {
   struct pipe_stream_output_target base;
   struct pipe_resource *counter_buffer;
   VkDeviceSize counter_buffer_offset;
   uint32_t stride;
   bool counter_buffer_valid;
};

static inline struct zink_so_target *
zink_so_target(struct pipe_stream_output_target *so_target)
{
   return (struct zink_so_target *)so_target;
}

struct zink_viewport_state {
   struct pipe_viewport_state viewport_states[PIPE_MAX_VIEWPORTS];
   struct pipe_scissor_state scissor_states[PIPE_MAX_VIEWPORTS];
   uint8_t num_viewports;
};


struct zink_descriptor_surface {
   union {
      struct zink_surface *surface;
      struct zink_buffer_view *bufferview;
   };
   bool is_buffer;
};

struct zink_context {
   struct pipe_context base;
   struct threaded_context *tc;
   struct slab_child_pool transfer_pool;
   struct slab_child_pool transfer_pool_unsync;
   struct blitter_context *blitter;

   struct pipe_device_reset_callback reset;

   uint32_t curr_batch; //the current batch id

   simple_mtx_t batch_mtx;
   struct zink_fence *deferred_fence;
   struct zink_fence *last_fence; //the last command buffer submitted
   struct hash_table batch_states; //submitted batch states
   struct util_dynarray free_batch_states; //unused batch states
   VkDeviceSize resource_size; //the accumulated size of resources in submitted buffers
   struct zink_batch batch;

   unsigned shader_has_inlinable_uniforms_mask;
   unsigned inlinable_uniforms_valid_mask;
   uint32_t inlinable_uniforms[PIPE_SHADER_TYPES][MAX_INLINABLE_UNIFORMS];

   struct pipe_constant_buffer ubos[PIPE_SHADER_TYPES][PIPE_MAX_CONSTANT_BUFFERS];
   struct pipe_shader_buffer ssbos[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_BUFFERS];
   uint32_t writable_ssbos[PIPE_SHADER_TYPES];
   struct zink_image_view image_views[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_IMAGES];

   struct pipe_framebuffer_state fb_state;

   struct zink_vertex_elements_state *element_state;
   struct zink_rasterizer_state *rast_state;
   struct zink_depth_stencil_alpha_state *dsa_state;

   struct hash_table desc_set_layouts[ZINK_DESCRIPTOR_TYPES];
   bool pipeline_changed[2]; //gfx, compute

   struct zink_shader *gfx_stages[ZINK_SHADER_COUNT];
   struct zink_shader *last_vertex_stage;
   struct zink_gfx_pipeline_state gfx_pipeline_state;
   enum pipe_prim_type gfx_prim_mode;
   struct hash_table *program_cache;
   struct zink_gfx_program *curr_program;

   struct zink_descriptor_data *dd;

   struct zink_shader *compute_stage;
   struct zink_compute_pipeline_state compute_pipeline_state;
   struct hash_table *compute_program_cache;
   struct zink_compute_program *curr_compute;

   unsigned dirty_shader_stages : 6; /* mask of changed shader stages */
   bool last_vertex_stage_dirty;

   struct hash_table *render_pass_cache;
   bool new_swapchain;
   bool fb_changed;
   bool rp_changed;

   struct zink_framebuffer *framebuffer;
   struct zink_framebuffer_clear fb_clears[PIPE_MAX_COLOR_BUFS + 1];
   uint16_t clears_enabled;
   uint16_t rp_clears_enabled;

   struct pipe_vertex_buffer vertex_buffers[PIPE_MAX_ATTRIBS];
   bool vertex_buffers_dirty;

   void *sampler_states[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
   struct pipe_sampler_view *sampler_views[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];

   struct zink_viewport_state vp_state;
   bool vp_state_changed;
   bool scissor_changed;

   float line_width;
   float blend_constants[4];

   bool sample_locations_changed;
   VkSampleLocationEXT vk_sample_locations[PIPE_MAX_SAMPLE_LOCATION_GRID_SIZE * PIPE_MAX_SAMPLE_LOCATION_GRID_SIZE];
   uint8_t sample_locations[2 * 4 * 8 * 16];

   bool drawid_broken;

   struct pipe_stencil_ref stencil_ref;

   union {
      struct {
         float default_inner_level[2];
         float default_outer_level[4];
      };
      float tess_levels[6];
   };

   struct list_head suspended_queries;
   struct list_head primitives_generated_queries;
   bool queries_disabled, render_condition_active;
   struct {
      struct zink_query *query;
      bool inverted;
   } render_condition;

   struct pipe_resource *dummy_vertex_buffer;
   struct pipe_resource *dummy_xfb_buffer;
   struct pipe_surface *dummy_surface;
   struct zink_buffer_view *dummy_bufferview;

   struct {
      /* descriptor info */
      VkDescriptorBufferInfo ubos[PIPE_SHADER_TYPES][PIPE_MAX_CONSTANT_BUFFERS];
      uint32_t push_valid;
      uint8_t num_ubos[PIPE_SHADER_TYPES];

      VkDescriptorBufferInfo ssbos[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_BUFFERS];
      uint8_t num_ssbos[PIPE_SHADER_TYPES];

      VkDescriptorImageInfo textures[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
      VkBufferView tbos[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
      uint8_t num_samplers[PIPE_SHADER_TYPES];
      uint8_t num_sampler_views[PIPE_SHADER_TYPES];

      VkDescriptorImageInfo images[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_IMAGES];
      VkBufferView texel_images[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_IMAGES];
      uint8_t num_images[PIPE_SHADER_TYPES];

      struct zink_resource *descriptor_res[ZINK_DESCRIPTOR_TYPES][PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
      struct zink_descriptor_surface sampler_surfaces[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
      struct zink_descriptor_surface image_surfaces[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_IMAGES];
   } di;
   bool descriptor_refs_dirty[2];
   struct set *need_barriers[2]; //gfx, compute
   struct set update_barriers[2][2]; //[gfx, compute][current, next]
   uint8_t barrier_set_idx[2];

   uint32_t num_so_targets;
   struct pipe_stream_output_target *so_targets[PIPE_MAX_SO_OUTPUTS];
   bool dirty_so_targets;
   bool xfb_barrier;
   bool first_frame_done;
   bool have_timelines;

   bool is_device_lost;
   bool rast_state_changed : 1;
   bool dsa_state_changed : 1;
   bool stencil_ref_changed : 1;
};

static inline struct zink_context *
zink_context(struct pipe_context *context)
{
   return (struct zink_context *)context;
}

static inline bool
zink_fb_clear_enabled(const struct zink_context *ctx, unsigned idx)
{
   if (idx == PIPE_MAX_COLOR_BUFS)
      return ctx->clears_enabled & PIPE_CLEAR_DEPTHSTENCIL;
   return ctx->clears_enabled & (PIPE_CLEAR_COLOR0 << idx);
}

struct zink_batch *
zink_batch_rp(struct zink_context *ctx);

struct zink_batch *
zink_batch_no_rp(struct zink_context *ctx);

void
zink_fence_wait(struct pipe_context *ctx);

void
zink_wait_on_batch(struct zink_context *ctx, uint32_t batch_id);

bool
zink_check_batch_completion(struct zink_context *ctx, uint32_t batch_id);

void
zink_flush_queue(struct zink_context *ctx);

void
zink_maybe_flush_or_stall(struct zink_context *ctx);

bool
zink_resource_access_is_write(VkAccessFlags flags);

void
zink_resource_buffer_barrier(struct zink_context *ctx, struct zink_batch *batch, struct zink_resource *res, VkAccessFlags flags, VkPipelineStageFlags pipeline);
void
zink_fake_buffer_barrier(struct zink_resource *res, VkAccessFlags flags, VkPipelineStageFlags pipeline);
bool
zink_resource_image_needs_barrier(struct zink_resource *res, VkImageLayout new_layout, VkAccessFlags flags, VkPipelineStageFlags pipeline);
bool
zink_resource_image_barrier_init(VkImageMemoryBarrier *imb, struct zink_resource *res, VkImageLayout new_layout, VkAccessFlags flags, VkPipelineStageFlags pipeline);
void
zink_resource_image_barrier(struct zink_context *ctx, struct zink_batch *batch, struct zink_resource *res,
                      VkImageLayout new_layout, VkAccessFlags flags, VkPipelineStageFlags pipeline);

bool
zink_resource_needs_barrier(struct zink_resource *res, VkImageLayout layout, VkAccessFlags flags, VkPipelineStageFlags pipeline);
void
zink_resource_barrier(struct zink_context *ctx, struct zink_batch *batch, struct zink_resource *res, VkImageLayout layout, VkAccessFlags flags, VkPipelineStageFlags pipeline);

 void
 zink_begin_render_pass(struct zink_context *ctx,
                        struct zink_batch *batch);

VkPipelineStageFlags
zink_pipeline_flags_from_stage(VkShaderStageFlagBits stage);

VkShaderStageFlagBits
zink_shader_stage(enum pipe_shader_type type);

struct pipe_context *
zink_context_create(struct pipe_screen *pscreen, void *priv, unsigned flags);

void
zink_context_query_init(struct pipe_context *ctx);

void
zink_blit_begin(struct zink_context *ctx, enum zink_blit_flags flags);

void
zink_blit(struct pipe_context *pctx,
          const struct pipe_blit_info *info);

bool
zink_blit_region_fills(struct u_rect region, unsigned width, unsigned height);

bool
zink_blit_region_covers(struct u_rect region, struct u_rect covers);

static inline struct u_rect
zink_rect_from_box(const struct pipe_box *box)
{
   return (struct u_rect){box->x, box->x + box->width, box->y, box->y + box->height};
}

void
zink_resource_rebind(struct zink_context *ctx, struct zink_resource *res);

void
zink_rebind_framebuffer(struct zink_context *ctx, struct zink_resource *res);

void
zink_draw_vbo(struct pipe_context *pctx,
              const struct pipe_draw_info *dinfo,
              unsigned drawid_offset,
              const struct pipe_draw_indirect_info *indirect,
              const struct pipe_draw_start_count_bias *draws,
              unsigned num_draws);

void
zink_launch_grid(struct pipe_context *pctx, const struct pipe_grid_info *info);

void
zink_copy_buffer(struct zink_context *ctx, struct zink_batch *batch, struct zink_resource *dst, struct zink_resource *src,
                 unsigned dst_offset, unsigned src_offset, unsigned size);

void
zink_copy_image_buffer(struct zink_context *ctx, struct zink_batch *batch, struct zink_resource *dst, struct zink_resource *src,
                       unsigned dst_level, unsigned dstx, unsigned dsty, unsigned dstz,
                       unsigned src_level, const struct pipe_box *src_box, enum pipe_map_flags map_flags);

void
zink_destroy_buffer_view(struct zink_screen *screen, struct zink_buffer_view *buffer_view);

void
debug_describe_zink_buffer_view(char *buf, const struct zink_buffer_view *ptr);

static inline void
zink_buffer_view_reference(struct zink_screen *screen,
                           struct zink_buffer_view **dst,
                           struct zink_buffer_view *src)
{
   struct zink_buffer_view *old_dst = dst ? *dst : NULL;

   if (pipe_reference_described(old_dst ? &old_dst->reference : NULL, &src->reference,
                                (debug_reference_descriptor)debug_describe_zink_buffer_view))
      zink_destroy_buffer_view(screen, old_dst);
   if (dst) *dst = src;
}

void
zink_update_descriptor_refs(struct zink_context *ctx, bool compute);

void
zink_init_vk_sample_locations(struct zink_context *ctx, VkSampleLocationsInfoEXT *loc);


static inline VkPipelineStageFlags
zink_pipeline_flags_from_pipe_stage(enum pipe_shader_type pstage)
{
   switch (pstage) {
   case PIPE_SHADER_VERTEX:
      return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
   case PIPE_SHADER_FRAGMENT:
      return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
   case PIPE_SHADER_GEOMETRY:
      return VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
   case PIPE_SHADER_TESS_CTRL:
      return VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;
   case PIPE_SHADER_TESS_EVAL:
      return VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
   case PIPE_SHADER_COMPUTE:
      return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
   default:
      unreachable("unknown shader stage");
   }
}

#ifdef __cplusplus
}
#endif

#endif
