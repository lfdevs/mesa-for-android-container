/**************************************************************************
 *
 * Copyright 2015 Advanced Micro Devices, Inc.
 * Copyright 2008 VMware, Inc.
 * All Rights Reserved.
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
 *
 **************************************************************************/

#ifndef DD_H_
#define DD_H_

#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "pipe/p_screen.h"
#include "dd_util.h"

enum dd_mode {
   DD_DETECT_HANGS,
   DD_DUMP_ALL_CALLS
};

struct dd_screen
{
   struct pipe_screen base;
   struct pipe_screen *screen;
   unsigned timeout_ms;
   enum dd_mode mode;
   bool no_flush;
   bool verbose;
   unsigned skip_count;
};

struct dd_query
{
   unsigned type;
   struct pipe_query *query;
};

struct dd_state
{
   void *cso;

   union {
      struct pipe_blend_state blend;
      struct pipe_depth_stencil_alpha_state dsa;
      struct pipe_rasterizer_state rs;
      struct pipe_sampler_state sampler;
      struct {
         struct pipe_vertex_element velems[PIPE_MAX_ATTRIBS];
         unsigned count;
      } velems;
      struct pipe_shader_state shader;
   } state;
};

struct dd_context
{
   struct pipe_context base;
   struct pipe_context *pipe;

   struct {
      struct dd_query *query;
      bool condition;
      unsigned mode;
   } render_cond;

   struct pipe_index_buffer index_buffer;
   struct pipe_vertex_buffer vertex_buffers[PIPE_MAX_ATTRIBS];

   unsigned num_so_targets;
   struct pipe_stream_output_target *so_targets[PIPE_MAX_SO_BUFFERS];
   unsigned so_offsets[PIPE_MAX_SO_BUFFERS];

   struct dd_state *shaders[PIPE_SHADER_TYPES];
   struct pipe_constant_buffer constant_buffers[PIPE_SHADER_TYPES][PIPE_MAX_CONSTANT_BUFFERS];
   struct pipe_sampler_view *sampler_views[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
   struct dd_state *sampler_states[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
   struct pipe_image_view shader_images[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_IMAGES];
   struct pipe_shader_buffer shader_buffers[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_BUFFERS];

   struct dd_state *velems;
   struct dd_state *rs;
   struct dd_state *dsa;
   struct dd_state *blend;

   struct pipe_blend_color blend_color;
   struct pipe_stencil_ref stencil_ref;
   unsigned sample_mask;
   unsigned min_samples;
   struct pipe_clip_state clip_state;
   struct pipe_framebuffer_state framebuffer_state;
   struct pipe_poly_stipple polygon_stipple;
   struct pipe_scissor_state scissors[PIPE_MAX_VIEWPORTS];
   struct pipe_viewport_state viewports[PIPE_MAX_VIEWPORTS];
   float tess_default_levels[6];

   unsigned num_draw_calls;
};


struct pipe_context *
dd_context_create(struct dd_screen *dscreen, struct pipe_context *pipe);

void
dd_init_draw_functions(struct dd_context *dctx);


static inline struct dd_context *
dd_context(struct pipe_context *pipe)
{
   return (struct dd_context *)pipe;
}

static inline struct dd_screen *
dd_screen(struct pipe_screen *screen)
{
   return (struct dd_screen*)screen;
}


#define CTX_INIT(_member) \
   dctx->base._member = dctx->pipe->_member ? dd_context_##_member : NULL

#endif /* DD_H_ */
