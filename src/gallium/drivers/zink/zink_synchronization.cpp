/*
 * Copyright © 2023 Valve Corporation
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
 * 
 * Authors:
 *    Mike Blumenkrantz <michael.blumenkrantz@gmail.com>
 */

#include "zink_batch.h"
#include "zink_context.h"
#include "zink_descriptors.h"
#include "zink_resource.h"
#include "zink_screen.h"


static bool
zink_resource_image_needs_barrier(struct zink_resource *res, VkImageLayout new_layout, VkAccessFlags flags, VkPipelineStageFlags pipeline)
{
   return res->layout != new_layout || (res->obj->access_stage & pipeline) != pipeline ||
          (res->obj->access & flags) != flags ||
          zink_resource_access_is_write(res->obj->access) ||
          zink_resource_access_is_write(flags);
}

void
zink_resource_image_barrier_init(VkImageMemoryBarrier *imb, struct zink_resource *res, VkImageLayout new_layout, VkAccessFlags flags, VkPipelineStageFlags pipeline)
{
   VkImageSubresourceRange isr = {
      res->aspect,
      0, VK_REMAINING_MIP_LEVELS,
      0, VK_REMAINING_ARRAY_LAYERS
   };
   *imb = VkImageMemoryBarrier {
      VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      NULL,
      res->obj->access,
      flags,
      res->layout,
      new_layout,
      VK_QUEUE_FAMILY_IGNORED,
      VK_QUEUE_FAMILY_IGNORED,
      res->obj->image,
      isr
   };
}

void
zink_resource_image_barrier2_init(VkImageMemoryBarrier2 *imb, struct zink_resource *res, VkImageLayout new_layout, VkAccessFlags flags, VkPipelineStageFlags pipeline)
{
   VkImageSubresourceRange isr = {
      res->aspect,
      0, VK_REMAINING_MIP_LEVELS,
      0, VK_REMAINING_ARRAY_LAYERS
   };
   *imb = VkImageMemoryBarrier2 {
      VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      NULL,
      res->obj->unordered_access_stage ? res->obj->unordered_access_stage : res->obj->access_stage ? res->obj->access_stage : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      res->obj->unordered_access ? res->obj->unordered_access : res->obj->access,
      pipeline,
      flags,
      res->layout,
      new_layout,
      VK_QUEUE_FAMILY_IGNORED,
      VK_QUEUE_FAMILY_IGNORED,
      res->obj->image,
      isr
   };
}

static inline bool
is_shader_pipline_stage(VkPipelineStageFlags pipeline)
{
   return pipeline & GFX_SHADER_BITS;
}

static void
resource_check_defer_buffer_barrier(struct zink_context *ctx, struct zink_resource *res, VkPipelineStageFlags pipeline)
{
   assert(res->obj->is_buffer);
   /* streamout binds have their own sync: ignore */
   unsigned bind_count0 = res->bind_count[0] - res->so_bind_count;
   if (bind_count0 > 0) {
      /* if has vbo binds and is not a vbo barrier... */
      if ((res->vbo_bind_count && !(pipeline & VK_PIPELINE_STAGE_VERTEX_INPUT_BIT)) ||
         /* or if has more than just vbo binds and is not a gfx stage */
          (res->vbo_bind_count != bind_count0 && !is_shader_pipline_stage(pipeline)))
         /* sync on next draw */
         _mesa_set_add(ctx->need_barriers[0], res);
   }
   if (res->bind_count[1] && !(pipeline & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT))
      /* if writing from non-compute and has compute binds, sync on next dispatch */
      _mesa_set_add(ctx->need_barriers[1], res);
}

static inline bool
unordered_res_exec(const struct zink_context *ctx, const struct zink_resource *res, bool is_write)
{
   /* if all usage is unordered, keep unordered */
   if (res->obj->unordered_read && res->obj->unordered_write)
      return true;
   /* if testing write access but have any ordered read access, cannot promote */
   if (is_write && zink_batch_usage_matches(res->obj->bo->reads.u, ctx->bs) && !res->obj->unordered_read)
      return false;
   /* if write access is unordered or nonexistent, always promote */
   return res->obj->unordered_write || !zink_batch_usage_matches(res->obj->bo->writes.u, ctx->bs);
}

static ALWAYS_INLINE bool
check_unordered_exec(struct zink_context *ctx, struct zink_resource *res, bool is_write)
{
   if (res) {
      if (!res->obj->is_buffer && !zink_screen(ctx->base.screen)->info.have_KHR_unified_image_layouts) {
         if (zink_resource_usage_is_unflushed(res) && !res->obj->unordered_read && !res->obj->unordered_write)
            return false;
      }
      return unordered_res_exec(ctx, res, is_write);
   }
   return true;
}

VkCommandBuffer
zink_get_cmdbuf(struct zink_context *ctx, struct zink_resource *src, struct zink_resource *dst)
{
   bool unordered_exec = !ctx->no_reorder;

   unordered_exec &= check_unordered_exec(ctx, src, false) &&
                     check_unordered_exec(ctx, dst, true);

   if (src)
      src->obj->unordered_read = unordered_exec;
   if (dst)
      dst->obj->unordered_write = unordered_exec;

   if (!unordered_exec || ctx->unordered_blitting)
      zink_batch_no_rp(ctx);

   if (unordered_exec) {
      ctx->bs->has_reordered_work = true;
      return ctx->bs->reordered_cmdbuf;
   }
   ctx->bs->has_work = true;
   return ctx->bs->cmdbuf;
}

ALWAYS_INLINE static void
resource_defer_image_barrier(struct zink_context *ctx, struct zink_resource *res, VkPipelineStageFlags pipeline)
{
   assert(!res->obj->is_buffer);
   assert(!ctx->blitting);

   bool is_compute = pipeline == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
   /* if this is a non-shader barrier and there are binds, always queue a shader barrier */
   bool is_shader = is_shader_pipline_stage(pipeline);

   /* queue a layout change if a layout change will be needed */
   if (res->bind_count[!is_compute])
      _mesa_set_add(ctx->need_barriers[!is_compute], res);
   /* also queue a layout change if this is a non-shader layout */
   if (res->bind_count[is_compute] && !is_shader && !is_compute)
      _mesa_set_add(ctx->need_barriers[is_compute], res);

}

static void
resource_check_defer_image_barrier(struct zink_context *ctx, struct zink_resource *res, VkImageLayout layout, VkPipelineStageFlags pipeline)
{
   bool is_compute = pipeline == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
   /* if this is a non-shader barrier and there are binds, always queue a shader barrier */
   bool is_shader = is_shader_pipline_stage(pipeline);
   if ((is_shader || !res->bind_count[is_compute]) &&
       /* if no layout change is needed between gfx and compute, do nothing */
       !res->bind_count[!is_compute] && (!is_compute || !res->fb_bind_count))
      return;

   if ((res->bind_count[1] && is_shader) || (res->bind_count[0] && is_compute)) {
      /* if the layout is the same between gfx and compute, do nothing */
      if (layout == zink_descriptor_util_image_layout_eval(ctx, res, !is_compute))
         return;
   }
   resource_defer_image_barrier(ctx, res, pipeline);
}

enum barrier_type {
   barrier_default,
   barrier_KHR_synchronzation2
};

template <barrier_type BARRIER_API>
struct emit_memory_barrier {
   static void for_image(struct zink_context *ctx, struct zink_resource *res, VkImageLayout new_layout,
                         VkAccessFlags flags, VkPipelineStageFlags pipeline, bool completed, VkCommandBuffer cmdbuf,
                         bool *queue_import)
   {
      VkImageMemoryBarrier imb;
      zink_resource_image_barrier_init(&imb, res, new_layout, flags, pipeline);
      if (res->obj->needs_zs_evaluate)
         imb.pNext = &res->obj->zs_evaluate;
      res->obj->needs_zs_evaluate = false;
      if (res->queue != zink_screen(ctx->base.screen)->gfx_queue && res->queue != VK_QUEUE_FAMILY_IGNORED) {
         imb.srcQueueFamilyIndex = res->queue;
         imb.dstQueueFamilyIndex = zink_screen(ctx->base.screen)->gfx_queue;
         res->queue = VK_QUEUE_FAMILY_IGNORED;
         *queue_import = true;
      }
      VkAccessFlags src_flags = res->obj->unordered_access_stage ? res->obj->unordered_access_stage : res->obj->access_stage;
      VKCTX(CmdPipelineBarrier)(
          cmdbuf,
          src_flags ? src_flags : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
          pipeline,
          0,
          0, NULL,
          0, NULL,
          1, &imb
          );
   }

   static void for_buffer(struct zink_context *ctx, struct zink_resource *res,
                          VkPipelineStageFlags pipeline,
                          VkAccessFlags flags,
                          bool unordered,
                          bool usage_matches,
                          VkPipelineStageFlags stages,
                          VkAccessFlags src_flags,
                          VkCommandBuffer cmdbuf)
   {
      VkMemoryBarrier bmb;
      bmb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
      bmb.pNext = NULL;
      bmb.srcAccessMask = src_flags;
      bmb.dstAccessMask = flags;
      VKCTX(CmdPipelineBarrier)(
          cmdbuf,
          stages,
          pipeline,
          0,
          1, &bmb,
          0, NULL,
          0, NULL);
   }
};


template <>
struct emit_memory_barrier<barrier_KHR_synchronzation2> {
   static void for_image(struct zink_context *ctx, struct zink_resource *res, VkImageLayout new_layout,
                     VkAccessFlags flags, VkPipelineStageFlags pipeline, bool completed, VkCommandBuffer cmdbuf,
                     bool *queue_import)
   {
      VkImageMemoryBarrier2 imb;
      zink_resource_image_barrier2_init(&imb, res, new_layout, flags, pipeline);
      if (res->obj->needs_zs_evaluate)
         imb.pNext = &res->obj->zs_evaluate;
      res->obj->needs_zs_evaluate = false;
      if (res->queue != zink_screen(ctx->base.screen)->gfx_queue && res->queue != VK_QUEUE_FAMILY_IGNORED) {
         imb.srcQueueFamilyIndex = res->queue;
         imb.dstQueueFamilyIndex = zink_screen(ctx->base.screen)->gfx_queue;
         res->queue = VK_QUEUE_FAMILY_IGNORED;
         *queue_import = true;
      }
      VkDependencyInfo dep = {
         VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
         NULL,
         0,
         0,
         NULL,
         0,
         NULL,
         1,
         &imb
         };
      VKCTX(CmdPipelineBarrier2)(cmdbuf, &dep);
   }

   static void for_buffer(struct zink_context *ctx, struct zink_resource *res,
                          VkPipelineStageFlags pipeline,
                          VkAccessFlags flags,
                          bool unordered,
                          bool usage_matches,
                          VkPipelineStageFlags stages,
                          VkAccessFlags src_flags,
                          VkCommandBuffer cmdbuf)
   {
      VkMemoryBarrier2 bmb;
      bmb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
      bmb.pNext = NULL;
      bmb.srcStageMask = stages;
      bmb.srcAccessMask = src_flags;
      bmb.dstStageMask = pipeline;
      bmb.dstAccessMask = flags;
      VkDependencyInfo dep = {
          VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
          NULL,
          0,
          1,
          &bmb,
          0,
          NULL,
          0,
          NULL
      };
      VKCTX(CmdPipelineBarrier2)(cmdbuf, &dep);
   }
};

template <bool UNSYNCHRONIZED>
struct update_unordered_access_and_get_cmdbuf {
   /* use base template to make the cases for true and false more explicite below */
};

template <>
struct update_unordered_access_and_get_cmdbuf<true> {
   static VkCommandBuffer apply(struct zink_context *ctx, struct zink_resource *res, bool usage_matches, bool is_write)
   {
      assert(!usage_matches || (res->obj->unordered_write && res->obj->unordered_read));
      res->obj->unordered_write = true;
      res->obj->unordered_read = true;
      ctx->bs->has_unsync = true;
      return ctx->bs->unsynchronized_cmdbuf;
   }
};

template <>
struct update_unordered_access_and_get_cmdbuf<false> {
   static VkCommandBuffer apply(struct zink_context *ctx, struct zink_resource *res, bool usage_matches, bool is_write)
   {
      VkCommandBuffer cmdbuf;
      if (!usage_matches) {
         res->obj->unordered_write = true;
         if (is_write || zink_resource_usage_check_completion_fast(zink_screen(ctx->base.screen), res, ZINK_RESOURCE_ACCESS_RW))
            res->obj->unordered_read = true;
      }
      if (zink_resource_usage_matches(res, ctx->bs) && !ctx->unordered_blitting &&
          /* if current batch usage exists with ordered non-transfer access, never promote
           * this avoids layout dsync
           */
          (!res->obj->unordered_read || !res->obj->unordered_write)) {
         cmdbuf = ctx->bs->cmdbuf;
         res->obj->unordered_write = false;
         res->obj->unordered_read = false;
         /* it's impossible to detect this from the caller
       * there should be no valid case where this barrier can occur inside a renderpass
       */
         zink_batch_no_rp(ctx);
      } else {
         cmdbuf = is_write ? zink_get_cmdbuf(ctx, NULL, res) : zink_get_cmdbuf(ctx, res, NULL);
         /* force subsequent barriers to be ordered to avoid layout desync */
         if (cmdbuf != ctx->bs->reordered_cmdbuf) {
            res->obj->unordered_write = false;
            res->obj->unordered_read = false;
         }
      }
      return cmdbuf;
   }
};

/* always accumulate new read accesses, resetting on write access */
static void
apply_new_access(struct zink_context *ctx, struct zink_resource *res, VkAccessFlags flags, VkPipelineStageFlags pipeline, bool unordered, bool usage_matches, bool is_write)
{
   if (unordered) {
      if (is_write) {
         res->obj->unordered_access = flags;
         res->obj->unordered_access_stage = pipeline;
         /* these should get automatically emitted during submission */
         ctx->bs->unordered_write_access |= flags;
         ctx->bs->unordered_write_stages |= pipeline;
      } else {
         if (zink_resource_access_is_write(res->obj->unordered_access)) {
            res->obj->unordered_access = 0;
            res->obj->unordered_access_stage = 0;
         }
         res->obj->unordered_access |= flags;
         res->obj->unordered_access_stage |= pipeline;
      }
   } else {
      res->obj->unordered_access = 0;
      res->obj->unordered_access_stage = 0;
   }
   if (!unordered || !usage_matches || res->obj->ordered_access_is_copied) {
      if (zink_resource_access_is_write(res->obj->unordered_access | res->obj->access)) {
         res->obj->access = 0;
         res->obj->access_stage = 0;
      }
      if (is_write) {
         res->obj->access = flags;
         res->obj->access_stage = pipeline;
      } else {
         res->obj->access |= flags;
         res->obj->access_stage |= pipeline;
      }
      res->obj->ordered_access_is_copied = unordered;
   }
}

template <barrier_type BARRIER_API, bool UNSYNCHRONIZED, bool GENERAL>
void
zink_resource_image_barrier(struct zink_context *ctx, struct zink_resource *res, VkImageLayout new_layout, VkAccessFlags flags, VkPipelineStageFlags pipeline)
{
   bool is_write = zink_resource_access_is_write(flags);
   if (is_write && zink_is_swapchain(res))
      zink_kopper_set_readback_needs_update(res);
   if (!GENERAL && !res->obj->needs_zs_evaluate && !zink_resource_image_needs_barrier(res, new_layout, flags, pipeline) &&
       (res->queue == zink_screen(ctx->base.screen)->gfx_queue || res->queue == VK_QUEUE_FAMILY_IGNORED))
      return;
   bool has_usage = zink_resource_has_usage(res);
   bool completed = !has_usage || zink_resource_usage_check_completion_fast(zink_screen(ctx->base.screen), res, ZINK_RESOURCE_ACCESS_RW);
   bool usage_matches = zink_resource_usage_matches(res, ctx->bs);
   VkCommandBuffer cmdbuf = GENERAL && new_layout == VK_IMAGE_LAYOUT_GENERAL ?
                            (UNSYNCHRONIZED ? ctx->bs->unsynchronized_cmdbuf : is_write ? zink_get_cmdbuf(ctx, NULL, res) : zink_get_cmdbuf(ctx, res, NULL)) :
                            update_unordered_access_and_get_cmdbuf<UNSYNCHRONIZED>::apply(ctx, res, usage_matches, is_write);

   assert(new_layout);
   bool marker = zink_cmd_debug_marker_begin(ctx, cmdbuf, "image_barrier(%s->%s)", vk_ImageLayout_to_str(res->layout), vk_ImageLayout_to_str(new_layout));
   bool queue_import = false;
   emit_memory_barrier<BARRIER_API>::for_image(ctx, res, new_layout, flags, pipeline, completed, cmdbuf, &queue_import);
   zink_cmd_debug_marker_end(ctx, cmdbuf, marker);

   if (!UNSYNCHRONIZED)
      resource_check_defer_image_barrier(ctx, res, new_layout, pipeline);

   res->layout = new_layout;

   if (is_write)
      res->obj->last_write = flags;
   if (GENERAL) {
      bool unordered = ctx->unordered_blitting || cmdbuf == ctx->bs->reordered_cmdbuf;
      apply_new_access(ctx, res, flags, pipeline, unordered, usage_matches, is_write);
   } else {
      res->obj->access = flags;
      res->obj->access_stage = pipeline;
      res->obj->unordered_access = 0;
      res->obj->unordered_access_stage = 0;
      res->obj->ordered_access_is_copied = false;
   }

   if (!(flags & VK_ACCESS_TRANSFER_WRITE_BIT))
      zink_resource_copies_reset(res);

   if (res->obj->dt) {
      struct kopper_displaytarget *cdt = res->obj->dt;
      if (cdt->swapchain->num_acquires && res->obj->dt_idx != UINT32_MAX) {
         cdt->swapchain->images[res->obj->dt_idx].layout = res->layout;
      }
   }
   if (res->obj->exportable && queue_import) {
      simple_mtx_lock(&ctx->bs->exportable_lock);
      for (struct zink_resource *r = res; r; r = zink_resource(r->base.b.next)) {
         VkSemaphore sem = zink_screen_export_dmabuf_semaphore(zink_screen(ctx->base.screen), r);
         if (sem)
            util_dynarray_append(&ctx->bs->fd_wait_semaphores, sem);
      }
      simple_mtx_unlock(&ctx->bs->exportable_lock);
   }
}

bool
zink_check_unordered_transfer_access(struct zink_resource *res, unsigned level, const struct pipe_box *box)
{
   /* always barrier against previous non-transfer writes */
   bool non_transfer_write = res->obj->last_write && res->obj->last_write != VK_ACCESS_TRANSFER_WRITE_BIT;
   /* must barrier if clobbering a previous write */
   bool transfer_clobber = res->obj->last_write == VK_ACCESS_TRANSFER_WRITE_BIT && zink_resource_copy_box_intersects(res, level, box);
   return non_transfer_write || transfer_clobber;
}

bool
zink_check_valid_buffer_src_access(struct zink_context *ctx, struct zink_resource *res, unsigned offset, unsigned size)
{
   return res->obj->access && util_ranges_intersect(&res->valid_buffer_range, offset, offset + size) && !unordered_res_exec(ctx, res, false);
}

void
zink_resource_image_transfer_dst_barrier(struct zink_context *ctx, struct zink_resource *res, unsigned level, const struct pipe_box *box, bool unsync)
{
   if (res->obj->copies_need_reset)
      zink_resource_copies_reset(res);
   /* skip TRANSFER_DST barrier if no intersection from previous copies */
   VkImageLayout layout = zink_screen(ctx->base.screen)->driver_workarounds.general_layout ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
   VkAccessFlags flags = res->obj->access | res->obj->unordered_access;
   if ((flags && !(flags & VK_ACCESS_TRANSFER_WRITE_BIT)) ||
       res->layout != layout ||
       zink_screen(ctx->base.screen)->driver_workarounds.broken_cache_semantics ||
       zink_check_unordered_transfer_access(res, level, box)) {
      if (unsync)
         zink_screen(ctx->base.screen)->image_barrier_unsync(ctx, res, layout, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
      else
         zink_screen(ctx->base.screen)->image_barrier(ctx, res, layout, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
   } else {
      res->obj->unordered_access = VK_ACCESS_TRANSFER_WRITE_BIT;
      res->obj->last_write = VK_ACCESS_TRANSFER_WRITE_BIT;
      res->obj->unordered_access_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

      ctx->bs->unordered_write_access |= VK_ACCESS_TRANSFER_WRITE_BIT;
      ctx->bs->unordered_write_stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
      if (!zink_resource_usage_matches(res, ctx->bs)) {
         res->obj->access = VK_ACCESS_TRANSFER_WRITE_BIT;
         res->obj->access_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
         res->obj->ordered_access_is_copied = true;
      }
   }
   zink_resource_copy_box_add(ctx, res, level, box);
}

bool
zink_resource_buffer_transfer_dst_barrier(struct zink_context *ctx, struct zink_resource *res, unsigned offset, unsigned size)
{
   if (res->obj->copies_need_reset)
      zink_resource_copies_reset(res);
   bool unordered = true;
   struct pipe_box box;
   u_box_3d((int)offset, 0, 0, (int)size, 0, 0, &box);
   bool can_unordered_write = unordered_res_exec(ctx, res, true);
   /* must barrier if something read the valid buffer range */
   bool valid_read = (res->obj->access || res->obj->unordered_access) &&
                     util_ranges_intersect(&res->valid_buffer_range, offset, offset + size) && !can_unordered_write;
   if (valid_read || zink_screen(ctx->base.screen)->driver_workarounds.broken_cache_semantics || zink_check_unordered_transfer_access(res, 0, &box)) {
      zink_screen(ctx->base.screen)->buffer_barrier(ctx, res, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
      unordered = res->obj->unordered_write;
   } else {
      res->obj->unordered_access = VK_ACCESS_TRANSFER_WRITE_BIT;
      res->obj->last_write = VK_ACCESS_TRANSFER_WRITE_BIT;
      res->obj->unordered_access_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

      ctx->bs->unordered_write_access |= VK_ACCESS_TRANSFER_WRITE_BIT;
      ctx->bs->unordered_write_stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
      if (!zink_resource_usage_matches(res, ctx->bs)) {
         res->obj->access = VK_ACCESS_TRANSFER_WRITE_BIT;
         res->obj->access_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
         res->obj->ordered_access_is_copied = true;
      }
   }
   zink_resource_copy_box_add(ctx, res, 0, &box);
   /* this return value implies that the caller could do an unordered op on this resource */
   return unordered;
}

VkPipelineStageFlags
zink_pipeline_flags_from_stage(VkShaderStageFlagBits stage)
{
   switch (stage) {
   case VK_SHADER_STAGE_VERTEX_BIT:
      return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
   case VK_SHADER_STAGE_FRAGMENT_BIT:
      return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
   case VK_SHADER_STAGE_GEOMETRY_BIT:
      return VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
   case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
      return VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;
   case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
      return VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
   case VK_SHADER_STAGE_COMPUTE_BIT:
      return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
   default:
      UNREACHABLE("unknown shader stage bit");
   }
}

ALWAYS_INLINE static bool
resource_needs_barrier(struct zink_resource *res, VkAccessFlags flags, VkPipelineStageFlags pipeline, bool unordered)
{
   return zink_resource_access_is_write(unordered ? res->obj->unordered_access : res->obj->access) ||
          zink_resource_access_is_write(flags) ||
          ((unordered ? res->obj->unordered_access_stage : res->obj->access_stage) & pipeline) != pipeline ||
          ((unordered ? res->obj->unordered_access : res->obj->access) & flags) != flags;
}



template <barrier_type BARRIER_API, bool UNSYNCHRONIZED, bool GENERAL_IMAGE>
void
zink_resource_memory_barrier(struct zink_context *ctx, struct zink_resource *res, VkAccessFlags flags, VkPipelineStageFlags pipeline)
{
   bool is_write = zink_resource_access_is_write(flags);
   bool has_usage = zink_resource_has_usage(res);
   bool completed = !has_usage || zink_resource_usage_check_completion_fast(zink_screen(ctx->base.screen), res, ZINK_RESOURCE_ACCESS_RW);
   bool usage_matches = zink_resource_usage_matches(res, ctx->bs);
   if (!usage_matches) {
      res->obj->unordered_write = true;
      if (is_write || completed)
         res->obj->unordered_read = true;
   }
   bool unordered_usage_matches = res->obj->unordered_access && usage_matches;
   bool unordered = unordered_res_exec(ctx, res, is_write);
   assert(!UNSYNCHRONIZED || !usage_matches);
   if (!resource_needs_barrier(res, flags, pipeline, unordered))
      return;
   if (completed) {
      /* reset access on complete */
      res->obj->access = VK_ACCESS_NONE;
      res->obj->access_stage = VK_PIPELINE_STAGE_NONE;
      res->obj->last_write = VK_ACCESS_NONE;
   } else if (unordered && unordered_usage_matches && res->obj->ordered_access_is_copied) {
      /* always reset propagated access to avoid weirdness */
      res->obj->access = VK_ACCESS_NONE;
      res->obj->access_stage = VK_PIPELINE_STAGE_NONE;
   } else if (!unordered && !unordered_usage_matches) {
      /* reset unordered access on first ordered barrier */
      res->obj->unordered_access = VK_ACCESS_NONE;
      res->obj->unordered_access_stage = VK_PIPELINE_STAGE_NONE;
   }
   if (!usage_matches) {
      /* reset unordered on first new cmdbuf barrier */
      res->obj->unordered_access = VK_ACCESS_NONE;
      res->obj->unordered_access_stage = VK_PIPELINE_STAGE_NONE;
      res->obj->ordered_access_is_copied = false;
   }
   /* unordered barriers can be skipped when:
    * - previous access is not write AND (last write has already been synchronized OR no write is active)
    */
   VkAccessFlags prev_access = !unordered_usage_matches ? res->obj->access : res->obj->unordered_access;
   bool needs_access = zink_resource_access_is_write(prev_access) || (res->obj->last_write && (prev_access & flags) != flags);
   bool can_skip_unordered = !unordered || UNSYNCHRONIZED ? false : !needs_access;
   /* ordered barriers can be skipped if both:
    * - there is no current access
    * - there is no current-batch unordered access
    */
   bool can_skip_ordered = unordered || UNSYNCHRONIZED ? false : (!res->obj->access && !unordered_usage_matches);
   if (ctx->no_reorder)
      can_skip_unordered = can_skip_ordered = false;

   if (!can_skip_unordered && !can_skip_ordered) {
      VkCommandBuffer cmdbuf = UNSYNCHRONIZED ? ctx->bs->unsynchronized_cmdbuf :
                                                is_write ? zink_get_cmdbuf(ctx, NULL, res) : zink_get_cmdbuf(ctx, res, NULL);
      bool marker = false;
      ctx->bs->has_unsync |= UNSYNCHRONIZED;
      if (unlikely(zink_tracing)) {
         char buf[4096];
         zink_string_vkflags_unroll(buf, sizeof(buf), flags, (zink_vkflags_func)vk_AccessFlagBits_to_str);
         marker = zink_cmd_debug_marker_begin(ctx, cmdbuf, "memory_barrier(%s)", buf);
      }

      VkPipelineStageFlags stages = unordered_usage_matches ? res->obj->unordered_access_stage : res->obj->access_stage;
      if (BARRIER_API == barrier_default && !stages)
         stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      VkAccessFlags src_flags = unordered_usage_matches ? res->obj->unordered_access : res->obj->access;
      emit_memory_barrier<BARRIER_API>::for_buffer(ctx, res, pipeline, flags, unordered,usage_matches, stages, src_flags, cmdbuf);

      zink_cmd_debug_marker_end(ctx, cmdbuf, marker);
   }

   if (!UNSYNCHRONIZED && (is_write || res->write_bind_count[0] || res->write_bind_count[1])) {
      if (GENERAL_IMAGE)
         resource_defer_image_barrier(ctx, res, pipeline);
      else
         resource_check_defer_buffer_barrier(ctx, res, pipeline);
   }

   if (is_write)
      res->obj->last_write = flags;
   apply_new_access(ctx, res, flags, pipeline, unordered, usage_matches, is_write);
   if (!(flags & VK_ACCESS_TRANSFER_WRITE_BIT) && GENERAL_IMAGE)
      zink_resource_copies_reset(res);
}

template <bool UNSYNCHRONIZED>
void
zink_resource_image_barrier_general(struct zink_context *ctx, struct zink_resource *res, VkImageLayout new_layout, VkAccessFlags flags, VkPipelineStageFlags pipeline)
{
   assert(new_layout == VK_IMAGE_LAYOUT_GENERAL || new_layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
   /* if this requires an actual image barrier, send it through to the image barrier handlers */
   if (res->obj->needs_zs_evaluate || res->layout != new_layout ||
       (res->queue != zink_screen(ctx->base.screen)->gfx_queue && res->queue != VK_QUEUE_FAMILY_IGNORED)) {
      zink_resource_image_barrier<barrier_KHR_synchronzation2, UNSYNCHRONIZED, true>(ctx, res, new_layout, flags, pipeline);
      return;
   }

   /* this is just a synchronization barrier with GENERAL layout: use memory barrier for better granularity */
   zink_resource_memory_barrier<barrier_KHR_synchronzation2, false, true>(ctx, res, flags, pipeline);
}

void
zink_synchronization_init(struct zink_screen *screen)
{
   if (screen->info.have_vulkan13 || screen->info.have_KHR_synchronization2) {
      screen->buffer_barrier = zink_resource_memory_barrier<barrier_KHR_synchronzation2, false, false>;
      screen->buffer_barrier_unsync = zink_resource_memory_barrier<barrier_KHR_synchronzation2, true, false>;
      if (screen->driver_workarounds.general_layout) {
         screen->image_barrier = zink_resource_image_barrier_general<false>;
         screen->image_barrier_unsync = zink_resource_image_barrier_general<true>;
      } else {
         screen->image_barrier = zink_resource_image_barrier<barrier_KHR_synchronzation2, false, false>;
         screen->image_barrier_unsync = zink_resource_image_barrier<barrier_KHR_synchronzation2, true, false>;
      }
   } else {
      screen->buffer_barrier = zink_resource_memory_barrier<barrier_default, false, false>;
      screen->buffer_barrier_unsync = zink_resource_memory_barrier<barrier_default, true, false>;
      screen->image_barrier = zink_resource_image_barrier<barrier_default, false, false>;
      screen->image_barrier_unsync = zink_resource_image_barrier<barrier_default, true, false>;
   }
}
