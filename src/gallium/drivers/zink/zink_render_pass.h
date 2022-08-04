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

#ifndef ZINK_RENDERPASS_H
#define ZINK_RENDERPASS_H

#include <vulkan/vulkan.h>

#include "pipe/p_state.h"
#include "util/u_inlines.h"

struct zink_screen;

struct zink_rt_attrib {
  VkFormat format;
  VkSampleCountFlagBits samples;
  bool clear_color;
  union {
     bool clear_stencil;
     bool fbfetch;
  };
  union {
     bool invalid;
     bool needs_write;
  };
  bool resolve;
  bool mixed_zs;
};

struct zink_render_pass_state {
   union {
      struct {
         uint8_t num_cbufs : 5; /* PIPE_MAX_COLOR_BUFS = 8 */
         uint8_t have_zsbuf : 1;
         uint8_t samples:1; //for fs samplemask
         uint32_t num_zsresolves : 1;
         uint32_t num_cresolves : 24; /* PIPE_MAX_COLOR_BUFS, but this is a struct hole */
      };
      uint32_t val; //for comparison
   };
   struct zink_rt_attrib rts[PIPE_MAX_COLOR_BUFS + 1];
   unsigned num_rts;
   uint32_t clears; //for extra verification and update flagging
   uint32_t msaa_expand_mask;
};

struct zink_pipeline_rt {
   VkFormat format;
   VkSampleCountFlagBits samples;
};

struct zink_render_pass_pipeline_state {
   uint32_t num_attachments:22;
   uint32_t fbfetch:1;
   uint32_t color_read:1;
   uint32_t depth_read:1;
   uint32_t depth_write:1;
   uint32_t num_cresolves:4;
   uint32_t num_zsresolves:1;
   bool samples:1; //for fs samplemask
   struct zink_pipeline_rt attachments[PIPE_MAX_COLOR_BUFS + 1];
   unsigned id;
};

struct zink_render_pass {
   VkRenderPass render_pass;
   struct zink_render_pass_state state;
   unsigned pipeline_state;
};

struct zink_render_pass *
zink_create_render_pass(struct zink_screen *screen,
                        struct zink_render_pass_state *state,
                        struct zink_render_pass_pipeline_state *pstate);

void
zink_destroy_render_pass(struct zink_screen *screen,
                         struct zink_render_pass *rp);


unsigned
zink_begin_render_pass(struct zink_context *ctx);
void
zink_end_render_pass(struct zink_context *ctx);

VkImageLayout
zink_render_pass_attachment_get_barrier_info(const struct zink_rt_attrib *rt, bool color, VkPipelineStageFlags *pipeline, VkAccessFlags *access);

bool
zink_init_render_pass(struct zink_context *ctx);
void
zink_render_update_swapchain(struct zink_context *ctx);
void
zink_init_zs_attachment(struct zink_context *ctx, struct zink_rt_attrib *rt);
void
zink_init_color_attachment(struct zink_context *ctx, unsigned i, struct zink_rt_attrib *rt);
#endif
