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

#include "zink_context.h"
#include "zink_clear.h"
#include "zink_kopper.h"
#include "zink_query.h"
#include "zink_render_pass.h"
#include "zink_resource.h"
#include "zink_screen.h"
#include "zink_surface.h"

#include "util/u_memory.h"
#include "util/u_string.h"
#include "util/u_blitter.h"

static VkImageLayout
get_color_rt_layout(const struct zink_rt_attrib *rt)
{
   return rt->feedback_loop ? VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT : rt->fbfetch ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
}

static VkImageLayout
get_zs_rt_layout(const struct zink_rt_attrib *rt)
{
   bool has_clear = rt->clear_color || rt->clear_stencil;
   if (rt->feedback_loop)
      return VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT;
   return rt->needs_write || has_clear ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
}

VkImageLayout
zink_render_pass_attachment_get_barrier_info(const struct zink_rt_attrib *rt, bool color,
                                             VkPipelineStageFlags *pipeline, VkAccessFlags *access)
{
   *access = 0;
   if (color) {
      *pipeline = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      *access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      if (!rt->clear_color && !rt->invalid)
         *access |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
      return get_color_rt_layout(rt);
   }

   *pipeline = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
   if (!rt->clear_color && !rt->clear_stencil)
      *access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
   if (rt->clear_color || rt->clear_stencil || rt->needs_write)
      *access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
   return get_zs_rt_layout(rt);
}

VkImageLayout
zink_tc_renderpass_info_parse(struct zink_context *ctx, const struct tc_renderpass_info *info, unsigned idx, VkPipelineStageFlags *pipeline, VkAccessFlags *access)
{
   if (idx < PIPE_MAX_COLOR_BUFS) {
      *pipeline = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      *access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      if (info->cbuf_load & BITFIELD_BIT(idx))
         *access |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
      return (ctx->feedback_loops & BITFIELD_BIT(idx)) ? VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT :
             (info->cbuf_fbfetch & BITFIELD_BIT(idx)) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   } else {
      *access = 0;
      if (info->zsbuf_load || info->zsbuf_read_dsa)
         *access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
      if (info->zsbuf_clear | info->zsbuf_clear_partial | info->zsbuf_write_fs | info->zsbuf_write_dsa)
         *access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      assert(*access);
      *pipeline = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
      if (ctx->feedback_loops & BITFIELD_BIT(PIPE_MAX_COLOR_BUFS))
         return VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT;
      return (info->zsbuf_clear | info->zsbuf_clear_partial | info->zsbuf_write_fs | info->zsbuf_write_dsa) ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
   }
}

void
zink_init_zs_attachment(struct zink_context *ctx, struct zink_rt_attrib *rt)
{
   const struct pipe_framebuffer_state *fb = &ctx->fb_state;
   struct zink_resource *zsbuf = zink_resource(fb->zsbuf.texture);
   struct zink_framebuffer_clear *fb_clear = &ctx->fb_clears[PIPE_MAX_COLOR_BUFS];
   rt->format = zsbuf->format;
   rt->samples = MAX3(ctx->fb_state.zsbuf.nr_samples, ctx->fb_state.zsbuf.texture->nr_samples, 1);
   rt->clear_color = zink_fb_clear_enabled(ctx, PIPE_MAX_COLOR_BUFS) &&
                                         !zink_fb_clear_first_needs_explicit(fb_clear) &&
                                         (zink_fb_clear_element(fb_clear, 0)->zs.bits & PIPE_CLEAR_DEPTH);
   rt->clear_stencil = zink_fb_clear_enabled(ctx, PIPE_MAX_COLOR_BUFS) &&
                                           !zink_fb_clear_first_needs_explicit(fb_clear) &&
                                           (zink_fb_clear_element(fb_clear, 0)->zs.bits & PIPE_CLEAR_STENCIL);
   const uint64_t outputs_written = ctx->gfx_stages[MESA_SHADER_FRAGMENT] ?
                                    ctx->gfx_stages[MESA_SHADER_FRAGMENT]->info.outputs_written : 0;
   bool needs_write_z = (ctx->dsa_state && ctx->dsa_state->hw_state.depth_write) ||
                       outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH);
   needs_write_z |= (ctx->fb_state.zsbuf.nr_samples && !zink_screen(ctx->base.screen)->info.have_EXT_multisampled_render_to_single_sampled) ||
                    rt->clear_color ||
                    (zink_fb_clear_enabled(ctx, PIPE_MAX_COLOR_BUFS) && (zink_fb_clear_element(fb_clear, 0)->zs.bits & PIPE_CLEAR_DEPTH));

   bool needs_write_s = (ctx->dsa_state && (util_writes_stencil(&ctx->dsa_state->base.stencil[0]) || util_writes_stencil(&ctx->dsa_state->base.stencil[1]))) || 
                        rt->clear_stencil || (outputs_written & BITFIELD64_BIT(FRAG_RESULT_STENCIL)) ||
                        (zink_fb_clear_enabled(ctx, PIPE_MAX_COLOR_BUFS) && (zink_fb_clear_element(fb_clear, 0)->zs.bits & PIPE_CLEAR_STENCIL));
   rt->needs_write = needs_write_z | needs_write_s;
   rt->invalid = !zsbuf->valid;
   rt->feedback_loop = (ctx->feedback_loops & BITFIELD_BIT(PIPE_MAX_COLOR_BUFS)) > 0;
}

void
zink_tc_init_zs_attachment(struct zink_context *ctx, const struct tc_renderpass_info *info, struct zink_rt_attrib *rt)
{
   const struct pipe_framebuffer_state *fb = &ctx->fb_state;
   struct zink_resource *zsbuf = zink_resource(fb->zsbuf.texture);
   struct zink_framebuffer_clear *fb_clear = &ctx->fb_clears[PIPE_MAX_COLOR_BUFS];
   rt->format = zsbuf->format;
   rt->samples = MAX3(ctx->fb_state.zsbuf.nr_samples, ctx->fb_state.zsbuf.texture->nr_samples, 1);
   rt->clear_color = zink_fb_clear_enabled(ctx, PIPE_MAX_COLOR_BUFS) &&
                                         !zink_fb_clear_first_needs_explicit(fb_clear) &&
                                         (zink_fb_clear_element(fb_clear, 0)->zs.bits & PIPE_CLEAR_DEPTH);
   rt->clear_stencil = zink_fb_clear_enabled(ctx, PIPE_MAX_COLOR_BUFS) &&
                                           !zink_fb_clear_first_needs_explicit(fb_clear) &&
                                           (zink_fb_clear_element(fb_clear, 0)->zs.bits & PIPE_CLEAR_STENCIL);
   rt->needs_write = info->zsbuf_clear | info->zsbuf_clear_partial | info->zsbuf_write_fs | info->zsbuf_write_dsa;
   rt->invalid = !zsbuf->valid;
   rt->feedback_loop = (ctx->feedback_loops & BITFIELD_BIT(PIPE_MAX_COLOR_BUFS)) > 0;
}

void
zink_init_color_attachment(struct zink_context *ctx, unsigned i, struct zink_rt_attrib *rt)
{
   const struct pipe_framebuffer_state *fb = &ctx->fb_state;
   struct zink_resource *res = zink_resource(fb->cbufs[i].texture);
   if (res) {
      rt->format = ctx->fb_formats[i];
      rt->samples = MAX3(ctx->fb_state.cbufs[i].nr_samples, ctx->fb_state.cbufs[i].texture->nr_samples, 1);
      rt->clear_color = zink_fb_clear_enabled(ctx, i) && !zink_fb_clear_first_needs_explicit(&ctx->fb_clears[i]);
      rt->invalid = !res->valid;
      rt->fbfetch = (ctx->fbfetch_outputs & BITFIELD_BIT(i)) > 0;
      rt->feedback_loop = (ctx->feedback_loops & BITFIELD_BIT(i)) > 0;
   } else {
      memset(rt, 0, sizeof(struct zink_rt_attrib));
      rt->format = VK_FORMAT_R8G8B8A8_UNORM;
      rt->samples = fb->samples;
   }
}

void
zink_tc_init_color_attachment(struct zink_context *ctx, const struct tc_renderpass_info *info, unsigned i, struct zink_rt_attrib *rt)
{
   const struct pipe_framebuffer_state *fb = &ctx->fb_state;
   struct zink_resource *res = zink_resource(fb->cbufs[i].texture);
   if (res) {
      rt->format = ctx->fb_formats[i];
      rt->samples = MAX3(ctx->fb_state.cbufs[i].nr_samples, ctx->fb_state.cbufs[i].texture->nr_samples, 1);
      rt->clear_color = zink_fb_clear_enabled(ctx, i) && !zink_fb_clear_first_needs_explicit(&ctx->fb_clears[i]);
      rt->invalid = !res->valid;
      rt->fbfetch = (info->cbuf_fbfetch & BITFIELD_BIT(i)) > 0;
      rt->feedback_loop = (ctx->feedback_loops & BITFIELD_BIT(i)) > 0;
   } else {
      memset(rt, 0, sizeof(struct zink_rt_attrib));
      rt->format = VK_FORMAT_R8G8B8A8_UNORM;
      rt->samples = fb->samples;
   }
}

void
zink_render_msaa_expand(struct zink_context *ctx, uint32_t msaa_expand_mask)
{
   assert(msaa_expand_mask);

   bool blitting = ctx->blitting;
   u_foreach_bit(i, msaa_expand_mask) {
      zink_create_transient_surface(ctx, &ctx->fb_state.cbufs[i], ctx->fb_state.cbufs[i].nr_samples);
      struct pipe_resource *src = ctx->fb_state.cbufs[i].texture;
      struct zink_resource *res = zink_resource(src);
      struct zink_resource *transient = res->transient;
      /* skip replicate blit if the image will be full-cleared */
      if ((i == PIPE_MAX_COLOR_BUFS && (ctx->rp_clears_enabled & PIPE_CLEAR_DEPTHSTENCIL)) ||
            (ctx->rp_clears_enabled >> 2) & BITFIELD_BIT(i)) {
         transient->valid |= zink_fb_clear_full_exists(ctx, i);
      }
      if (transient->valid)
         continue;
      struct pipe_surface dst_view = ctx->fb_state.cbufs[i];
      dst_view.texture = &transient->base.b;
      dst_view.nr_samples = 0;
      struct pipe_sampler_view src_templ, *src_view;
      struct pipe_box dstbox;

      u_box_3d(0, 0, 0, ctx->fb_state.width, ctx->fb_state.height,
               1 + dst_view.last_layer - dst_view.first_layer, &dstbox);

      util_blitter_default_src_texture(ctx->blitter, &src_templ, src, ctx->fb_state.cbufs[i].level);
      src_view = ctx->base.create_sampler_view(&ctx->base, src, &src_templ);

      zink_blit_begin(ctx, ZINK_BLIT_SAVE_FB | ZINK_BLIT_SAVE_FS | ZINK_BLIT_SAVE_TEXTURES);
      ctx->blitting = false;
      zink_blit_barriers(ctx, zink_resource(src), transient, true);
      ctx->blitting = true;
      unsigned clear_mask = i == PIPE_MAX_COLOR_BUFS ?
                              (BITFIELD_MASK(PIPE_MAX_COLOR_BUFS) << 2) :
                              (PIPE_CLEAR_DEPTHSTENCIL | ((BITFIELD_MASK(PIPE_MAX_COLOR_BUFS) & ~BITFIELD_BIT(i)) << 2));
      unsigned clears_enabled = ctx->clears_enabled & clear_mask;
      unsigned rp_clears_enabled = ctx->rp_clears_enabled & clear_mask;
      ctx->clears_enabled &= ~clear_mask;
      ctx->rp_clears_enabled &= ~clear_mask;
      util_blitter_blit_generic(ctx->blitter, &dst_view, &dstbox,
                                 src_view, &dstbox, ctx->fb_state.width, ctx->fb_state.height,
                                 PIPE_MASK_RGBAZS, PIPE_TEX_FILTER_NEAREST, NULL,
                                 false, false, 0, NULL);
      ctx->clears_enabled = clears_enabled;
      ctx->rp_clears_enabled = rp_clears_enabled;
      ctx->blitting = false;
      if (blitting) {
         zink_blit_barriers(ctx, NULL, transient, true);
         zink_blit_barriers(ctx, NULL, zink_resource(src), true);
      }
      ctx->blitting = blitting;
      pipe_sampler_view_reference(&src_view, NULL);
      transient->valid = true;
   }
}

void
zink_render_fixup_swapchain(struct zink_context *ctx)
{
   if ((ctx->swapchain_size.width || ctx->swapchain_size.height)) {
      unsigned old_w = ctx->fb_state.width;
      unsigned old_h = ctx->fb_state.height;
      ctx->fb_state.width = ctx->swapchain_size.width;
      ctx->fb_state.height = ctx->swapchain_size.height;
      ctx->dynamic_fb.info.renderArea.extent.width = MIN2(ctx->dynamic_fb.info.renderArea.extent.width, ctx->fb_state.width);
      ctx->dynamic_fb.info.renderArea.extent.height = MIN2(ctx->dynamic_fb.info.renderArea.extent.height, ctx->fb_state.height);
      zink_kopper_fixup_depth_buffer(ctx);
      if (ctx->fb_state.width != old_w || ctx->fb_state.height != old_h)
         ctx->scissor_changed = true;
      ctx->swapchain_size.width = ctx->swapchain_size.height = 0;
   }
}
