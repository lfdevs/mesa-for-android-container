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

#include "dd_pipe.h"

#include "util/u_dump.h"
#include "util/u_format.h"
#include "tgsi/tgsi_scan.h"


enum call_type
{
   CALL_DRAW_VBO,
   CALL_LAUNCH_GRID,
   CALL_RESOURCE_COPY_REGION,
   CALL_BLIT,
   CALL_FLUSH_RESOURCE,
   CALL_CLEAR,
   CALL_CLEAR_BUFFER,
   CALL_CLEAR_RENDER_TARGET,
   CALL_CLEAR_DEPTH_STENCIL,
};

struct call_resource_copy_region
{
   struct pipe_resource *dst;
   unsigned dst_level;
   unsigned dstx, dsty, dstz;
   struct pipe_resource *src;
   unsigned src_level;
   const struct pipe_box *src_box;
};

struct call_clear
{
   unsigned buffers;
   const union pipe_color_union *color;
   double depth;
   unsigned stencil;
};

struct call_clear_buffer
{
   struct pipe_resource *res;
   unsigned offset;
   unsigned size;
   const void *clear_value;
   int clear_value_size;
};

struct dd_call
{
   enum call_type type;

   union {
      struct pipe_draw_info draw_vbo;
      struct pipe_grid_info launch_grid;
      struct call_resource_copy_region resource_copy_region;
      struct pipe_blit_info blit;
      struct pipe_resource *flush_resource;
      struct call_clear clear;
      struct call_clear_buffer clear_buffer;
   } info;
};

static FILE *
dd_get_file_stream(struct dd_context *dctx)
{
   struct dd_screen *dscreen = dd_screen(dctx->base.screen);
   struct pipe_screen *screen = dctx->pipe->screen;
   FILE *f = dd_get_debug_file(dscreen->verbose);
   if (!f)
      return NULL;

   fprintf(f, "Driver vendor: %s\n", screen->get_vendor(screen));
   fprintf(f, "Device vendor: %s\n", screen->get_device_vendor(screen));
   fprintf(f, "Device name: %s\n\n", screen->get_name(screen));
   return f;
}

static void
dd_close_file_stream(FILE *f)
{
   fclose(f);
}

static unsigned
dd_num_active_viewports(struct dd_context *dctx)
{
   struct tgsi_shader_info info;
   const struct tgsi_token *tokens;

   if (dctx->shaders[PIPE_SHADER_GEOMETRY])
      tokens = dctx->shaders[PIPE_SHADER_GEOMETRY]->state.shader.tokens;
   else if (dctx->shaders[PIPE_SHADER_TESS_EVAL])
      tokens = dctx->shaders[PIPE_SHADER_TESS_EVAL]->state.shader.tokens;
   else if (dctx->shaders[PIPE_SHADER_VERTEX])
      tokens = dctx->shaders[PIPE_SHADER_VERTEX]->state.shader.tokens;
   else
      return 1;

   tgsi_scan_shader(tokens, &info);
   return info.writes_viewport_index ? PIPE_MAX_VIEWPORTS : 1;
}

#define COLOR_RESET	"\033[0m"
#define COLOR_SHADER	"\033[1;32m"
#define COLOR_STATE	"\033[1;33m"

#define DUMP(name, var) do { \
   fprintf(f, COLOR_STATE #name ": " COLOR_RESET); \
   util_dump_##name(f, var); \
   fprintf(f, "\n"); \
} while(0)

#define DUMP_I(name, var, i) do { \
   fprintf(f, COLOR_STATE #name " %i: " COLOR_RESET, i); \
   util_dump_##name(f, var); \
   fprintf(f, "\n"); \
} while(0)

#define DUMP_M(name, var, member) do { \
   fprintf(f, "  " #member ": "); \
   util_dump_##name(f, (var)->member); \
   fprintf(f, "\n"); \
} while(0)

#define DUMP_M_ADDR(name, var, member) do { \
   fprintf(f, "  " #member ": "); \
   util_dump_##name(f, &(var)->member); \
   fprintf(f, "\n"); \
} while(0)

static void
print_named_value(FILE *f, const char *name, int value)
{
   fprintf(f, COLOR_STATE "%s" COLOR_RESET " = %i\n", name, value);
}

static void
print_named_xvalue(FILE *f, const char *name, int value)
{
   fprintf(f, COLOR_STATE "%s" COLOR_RESET " = 0x%08x\n", name, value);
}

static void
util_dump_uint(FILE *f, unsigned i)
{
   fprintf(f, "%u", i);
}

static void
util_dump_hex(FILE *f, unsigned i)
{
   fprintf(f, "0x%x", i);
}

static void
util_dump_double(FILE *f, double d)
{
   fprintf(f, "%f", d);
}

static void
util_dump_format(FILE *f, enum pipe_format format)
{
   fprintf(f, "%s", util_format_name(format));
}

static void
util_dump_color_union(FILE *f, const union pipe_color_union *color)
{
   fprintf(f, "{f = {%f, %f, %f, %f}, ui = {%u, %u, %u, %u}",
           color->f[0], color->f[1], color->f[2], color->f[3],
           color->ui[0], color->ui[1], color->ui[2], color->ui[3]);
}

static void
util_dump_query(FILE *f, struct dd_query *query)
{
   if (query->type >= PIPE_QUERY_DRIVER_SPECIFIC)
      fprintf(f, "PIPE_QUERY_DRIVER_SPECIFIC + %i",
              query->type - PIPE_QUERY_DRIVER_SPECIFIC);
   else
      fprintf(f, "%s", util_dump_query_type(query->type, false));
}

static void
dd_dump_render_condition(struct dd_context *dctx, FILE *f)
{
   if (dctx->render_cond.query) {
      fprintf(f, "render condition:\n");
      DUMP_M(query, &dctx->render_cond, query);
      DUMP_M(uint, &dctx->render_cond, condition);
      DUMP_M(uint, &dctx->render_cond, mode);
      fprintf(f, "\n");
   }
}

static void
dd_dump_draw_vbo(struct dd_context *dctx, struct pipe_draw_info *info, FILE *f)
{
   int sh, i;
   const char *shader_str[PIPE_SHADER_TYPES];

   shader_str[PIPE_SHADER_VERTEX] = "VERTEX";
   shader_str[PIPE_SHADER_TESS_CTRL] = "TESS_CTRL";
   shader_str[PIPE_SHADER_TESS_EVAL] = "TESS_EVAL";
   shader_str[PIPE_SHADER_GEOMETRY] = "GEOMETRY";
   shader_str[PIPE_SHADER_FRAGMENT] = "FRAGMENT";
   shader_str[PIPE_SHADER_COMPUTE] = "COMPUTE";

   DUMP(draw_info, info);
   if (info->indexed) {
      DUMP(index_buffer, &dctx->index_buffer);
      if (dctx->index_buffer.buffer)
         DUMP_M(resource, &dctx->index_buffer, buffer);
   }
   if (info->count_from_stream_output)
      DUMP_M(stream_output_target, info,
             count_from_stream_output);
   if (info->indirect)
      DUMP_M(resource, info, indirect);
   fprintf(f, "\n");

   /* TODO: dump active queries */

   dd_dump_render_condition(dctx, f);

   for (i = 0; i < PIPE_MAX_ATTRIBS; i++)
      if (dctx->vertex_buffers[i].buffer ||
          dctx->vertex_buffers[i].user_buffer) {
         DUMP_I(vertex_buffer, &dctx->vertex_buffers[i], i);
         if (dctx->vertex_buffers[i].buffer)
            DUMP_M(resource, &dctx->vertex_buffers[i], buffer);
      }

   if (dctx->velems) {
      print_named_value(f, "num vertex elements",
                        dctx->velems->state.velems.count);
      for (i = 0; i < dctx->velems->state.velems.count; i++) {
         fprintf(f, "  ");
         DUMP_I(vertex_element, &dctx->velems->state.velems.velems[i], i);
      }
   }

   print_named_value(f, "num stream output targets", dctx->num_so_targets);
   for (i = 0; i < dctx->num_so_targets; i++)
      if (dctx->so_targets[i]) {
         DUMP_I(stream_output_target, dctx->so_targets[i], i);
         DUMP_M(resource, dctx->so_targets[i], buffer);
         fprintf(f, "  offset = %i\n", dctx->so_offsets[i]);
      }

   fprintf(f, "\n");
   for (sh = 0; sh < PIPE_SHADER_TYPES; sh++) {
      if (sh == PIPE_SHADER_COMPUTE)
         continue;

      if (sh == PIPE_SHADER_TESS_CTRL &&
          !dctx->shaders[PIPE_SHADER_TESS_CTRL] &&
          dctx->shaders[PIPE_SHADER_TESS_EVAL])
         fprintf(f, "tess_state: {default_outer_level = {%f, %f, %f, %f}, "
                 "default_inner_level = {%f, %f}}\n",
                 dctx->tess_default_levels[0],
                 dctx->tess_default_levels[1],
                 dctx->tess_default_levels[2],
                 dctx->tess_default_levels[3],
                 dctx->tess_default_levels[4],
                 dctx->tess_default_levels[5]);

      if (sh == PIPE_SHADER_FRAGMENT)
         if (dctx->rs) {
            unsigned num_viewports = dd_num_active_viewports(dctx);

            if (dctx->rs->state.rs.clip_plane_enable)
               DUMP(clip_state, &dctx->clip_state);

            for (i = 0; i < num_viewports; i++)
               DUMP_I(viewport_state, &dctx->viewports[i], i);

            if (dctx->rs->state.rs.scissor)
               for (i = 0; i < num_viewports; i++)
                  DUMP_I(scissor_state, &dctx->scissors[i], i);

            DUMP(rasterizer_state, &dctx->rs->state.rs);

            if (dctx->rs->state.rs.poly_stipple_enable)
               DUMP(poly_stipple, &dctx->polygon_stipple);
            fprintf(f, "\n");
         }

      if (!dctx->shaders[sh])
         continue;

      fprintf(f, COLOR_SHADER "begin shader: %s" COLOR_RESET "\n", shader_str[sh]);
      DUMP(shader_state, &dctx->shaders[sh]->state.shader);

      for (i = 0; i < PIPE_MAX_CONSTANT_BUFFERS; i++)
         if (dctx->constant_buffers[sh][i].buffer ||
             dctx->constant_buffers[sh][i].user_buffer) {
            DUMP_I(constant_buffer, &dctx->constant_buffers[sh][i], i);
            if (dctx->constant_buffers[sh][i].buffer)
               DUMP_M(resource, &dctx->constant_buffers[sh][i], buffer);
         }

      for (i = 0; i < PIPE_MAX_SAMPLERS; i++)
         if (dctx->sampler_states[sh][i])
            DUMP_I(sampler_state, &dctx->sampler_states[sh][i]->state.sampler, i);

      for (i = 0; i < PIPE_MAX_SAMPLERS; i++)
         if (dctx->sampler_views[sh][i]) {
            DUMP_I(sampler_view, dctx->sampler_views[sh][i], i);
            DUMP_M(resource, dctx->sampler_views[sh][i], texture);
         }

      /* TODO: print shader images */
      /* TODO: print shader buffers */

      fprintf(f, COLOR_SHADER "end shader: %s" COLOR_RESET "\n\n", shader_str[sh]);
   }

   if (dctx->dsa)
      DUMP(depth_stencil_alpha_state, &dctx->dsa->state.dsa);
   DUMP(stencil_ref, &dctx->stencil_ref);

   if (dctx->blend)
      DUMP(blend_state, &dctx->blend->state.blend);
   DUMP(blend_color, &dctx->blend_color);

   print_named_value(f, "min_samples", dctx->min_samples);
   print_named_xvalue(f, "sample_mask", dctx->sample_mask);
   fprintf(f, "\n");

   DUMP(framebuffer_state, &dctx->framebuffer_state);
   for (i = 0; i < dctx->framebuffer_state.nr_cbufs; i++)
      if (dctx->framebuffer_state.cbufs[i]) {
         fprintf(f, "  " COLOR_STATE "cbufs[%i]:" COLOR_RESET "\n    ", i);
         DUMP(surface, dctx->framebuffer_state.cbufs[i]);
         fprintf(f, "    ");
         DUMP(resource, dctx->framebuffer_state.cbufs[i]->texture);
      }
   if (dctx->framebuffer_state.zsbuf) {
      fprintf(f, "  " COLOR_STATE "zsbuf:" COLOR_RESET "\n    ");
      DUMP(surface, dctx->framebuffer_state.zsbuf);
      fprintf(f, "    ");
      DUMP(resource, dctx->framebuffer_state.zsbuf->texture);
   }
   fprintf(f, "\n");
}

static void
dd_dump_launch_grid(struct dd_context *dctx, struct pipe_grid_info *info, FILE *f)
{
   fprintf(f, "%s:\n", __func__+8);
   /* TODO */
}

static void
dd_dump_resource_copy_region(struct dd_context *dctx,
                             struct call_resource_copy_region *info,
                             FILE *f)
{
   fprintf(f, "%s:\n", __func__+8);
   DUMP_M(resource, info, dst);
   DUMP_M(uint, info, dst_level);
   DUMP_M(uint, info, dstx);
   DUMP_M(uint, info, dsty);
   DUMP_M(uint, info, dstz);
   DUMP_M(resource, info, src);
   DUMP_M(uint, info, src_level);
   DUMP_M(box, info, src_box);
}

static void
dd_dump_blit(struct dd_context *dctx, struct pipe_blit_info *info, FILE *f)
{
   fprintf(f, "%s:\n", __func__+8);
   DUMP_M(resource, info, dst.resource);
   DUMP_M(uint, info, dst.level);
   DUMP_M_ADDR(box, info, dst.box);
   DUMP_M(format, info, dst.format);

   DUMP_M(resource, info, src.resource);
   DUMP_M(uint, info, src.level);
   DUMP_M_ADDR(box, info, src.box);
   DUMP_M(format, info, src.format);

   DUMP_M(hex, info, mask);
   DUMP_M(uint, info, filter);
   DUMP_M(uint, info, scissor_enable);
   DUMP_M_ADDR(scissor_state, info, scissor);
   DUMP_M(uint, info, render_condition_enable);

   if (info->render_condition_enable)
      dd_dump_render_condition(dctx, f);
}

static void
dd_dump_flush_resource(struct dd_context *dctx, struct pipe_resource *res,
                       FILE *f)
{
   fprintf(f, "%s:\n", __func__+8);
   DUMP(resource, res);
}

static void
dd_dump_clear(struct dd_context *dctx, struct call_clear *info, FILE *f)
{
   fprintf(f, "%s:\n", __func__+8);
   DUMP_M(uint, info, buffers);
   DUMP_M(color_union, info, color);
   DUMP_M(double, info, depth);
   DUMP_M(hex, info, stencil);
}

static void
dd_dump_clear_buffer(struct dd_context *dctx, struct call_clear_buffer *info,
                     FILE *f)
{
   int i;
   const char *value = (const char*)info->clear_value;

   fprintf(f, "%s:\n", __func__+8);
   DUMP_M(resource, info, res);
   DUMP_M(uint, info, offset);
   DUMP_M(uint, info, size);
   DUMP_M(uint, info, clear_value_size);

   fprintf(f, "  clear_value:");
   for (i = 0; i < info->clear_value_size; i++)
      fprintf(f, " %02x", value[i]);
   fprintf(f, "\n");
}

static void
dd_dump_clear_render_target(struct dd_context *dctx, FILE *f)
{
   fprintf(f, "%s:\n", __func__+8);
   /* TODO */
}

static void
dd_dump_clear_depth_stencil(struct dd_context *dctx, FILE *f)
{
   fprintf(f, "%s:\n", __func__+8);
   /* TODO */
}

static void
dd_dump_driver_state(struct dd_context *dctx, FILE *f, unsigned flags)
{
   if (dctx->pipe->dump_debug_state) {
	   fprintf(f,"\n\n**************************************************"
		     "***************************\n");
	   fprintf(f, "Driver-specific state:\n\n");
	   dctx->pipe->dump_debug_state(dctx->pipe, f, flags);
   }
}

static void
dd_dump_call(struct dd_context *dctx, struct dd_call *call, unsigned flags)
{
   FILE *f = dd_get_file_stream(dctx);

   if (!f)
      return;

   switch (call->type) {
   case CALL_DRAW_VBO:
      dd_dump_draw_vbo(dctx, &call->info.draw_vbo, f);
      break;
   case CALL_LAUNCH_GRID:
      dd_dump_launch_grid(dctx, &call->info.launch_grid, f);
      break;
   case CALL_RESOURCE_COPY_REGION:
      dd_dump_resource_copy_region(dctx, &call->info.resource_copy_region, f);
      break;
   case CALL_BLIT:
      dd_dump_blit(dctx, &call->info.blit, f);
      break;
   case CALL_FLUSH_RESOURCE:
      dd_dump_flush_resource(dctx, call->info.flush_resource, f);
      break;
   case CALL_CLEAR:
      dd_dump_clear(dctx, &call->info.clear, f);
      break;
   case CALL_CLEAR_BUFFER:
      dd_dump_clear_buffer(dctx, &call->info.clear_buffer, f);
      break;
   case CALL_CLEAR_RENDER_TARGET:
      dd_dump_clear_render_target(dctx, f);
      break;
   case CALL_CLEAR_DEPTH_STENCIL:
      dd_dump_clear_depth_stencil(dctx, f);
   }

   dd_dump_driver_state(dctx, f, flags);
   dd_close_file_stream(f);
}

static void
dd_kill_process(void)
{
   sync();
   fprintf(stderr, "dd: Aborting the process...\n");
   fflush(stdout);
   fflush(stderr);
   abort();
}

static bool
dd_flush_and_check_hang(struct dd_context *dctx,
                        struct pipe_fence_handle **flush_fence,
                        unsigned flush_flags)
{
   struct pipe_fence_handle *fence = NULL;
   struct pipe_context *pipe = dctx->pipe;
   struct pipe_screen *screen = pipe->screen;
   uint64_t timeout_ms = dd_screen(dctx->base.screen)->timeout_ms;
   bool idle;

   assert(timeout_ms > 0);

   pipe->flush(pipe, &fence, flush_flags);
   if (flush_fence)
      screen->fence_reference(screen, flush_fence, fence);
   if (!fence)
      return false;

   idle = screen->fence_finish(screen, fence, timeout_ms * 1000000);
   screen->fence_reference(screen, &fence, NULL);
   if (!idle)
      fprintf(stderr, "dd: GPU hang detected!\n");
   return !idle;
}

static void
dd_flush_and_handle_hang(struct dd_context *dctx,
                         struct pipe_fence_handle **fence, unsigned flags,
                         const char *cause)
{
   if (dd_flush_and_check_hang(dctx, fence, flags)) {
      FILE *f = dd_get_file_stream(dctx);

      if (f) {
         fprintf(f, "dd: %s.\n", cause);
         dd_dump_driver_state(dctx, f, PIPE_DEBUG_DEVICE_IS_HUNG);
         dd_close_file_stream(f);
      }

      /* Terminate the process to prevent future hangs. */
      dd_kill_process();
   }
}

static void
dd_context_flush(struct pipe_context *_pipe,
                 struct pipe_fence_handle **fence, unsigned flags)
{
   struct dd_context *dctx = dd_context(_pipe);
   struct pipe_context *pipe = dctx->pipe;

   switch (dd_screen(dctx->base.screen)->mode) {
   case DD_DETECT_HANGS:
      dd_flush_and_handle_hang(dctx, fence, flags,
                               "GPU hang detected in pipe->flush()");
      break;
   case DD_DUMP_ALL_CALLS:
      pipe->flush(pipe, fence, flags);
      break;
   default:
      assert(0);
   }
}

static void
dd_before_draw(struct dd_context *dctx)
{
   struct dd_screen *dscreen = dd_screen(dctx->base.screen);

   if (dscreen->mode == DD_DETECT_HANGS &&
       !dscreen->no_flush &&
       dctx->num_draw_calls >= dscreen->skip_count)
      dd_flush_and_handle_hang(dctx, NULL, 0,
                               "GPU hang most likely caused by internal "
                               "driver commands");
}

static void
dd_after_draw(struct dd_context *dctx, struct dd_call *call)
{
   struct dd_screen *dscreen = dd_screen(dctx->base.screen);
   struct pipe_context *pipe = dctx->pipe;

   if (dctx->num_draw_calls >= dscreen->skip_count) {
      switch (dscreen->mode) {
      case DD_DETECT_HANGS:
         if (!dscreen->no_flush &&
            dd_flush_and_check_hang(dctx, NULL, 0)) {
            dd_dump_call(dctx, call, PIPE_DEBUG_DEVICE_IS_HUNG);

            /* Terminate the process to prevent future hangs. */
            dd_kill_process();
         }
         break;
      case DD_DUMP_ALL_CALLS:
         if (!dscreen->no_flush)
            pipe->flush(pipe, NULL, 0);
         dd_dump_call(dctx, call, 0);
         break;
      default:
         assert(0);
      }
   }

   ++dctx->num_draw_calls;
   if (dscreen->skip_count && dctx->num_draw_calls % 10000 == 0)
      fprintf(stderr, "Gallium debugger reached %u draw calls.\n",
              dctx->num_draw_calls);
}

static void
dd_context_draw_vbo(struct pipe_context *_pipe,
                    const struct pipe_draw_info *info)
{
   struct dd_context *dctx = dd_context(_pipe);
   struct pipe_context *pipe = dctx->pipe;
   struct dd_call call;

   call.type = CALL_DRAW_VBO;
   call.info.draw_vbo = *info;

   dd_before_draw(dctx);
   pipe->draw_vbo(pipe, info);
   dd_after_draw(dctx, &call);
}

static void
dd_context_launch_grid(struct pipe_context *_pipe,
                       const struct pipe_grid_info *info)
{
   struct dd_context *dctx = dd_context(_pipe);
   struct pipe_context *pipe = dctx->pipe;
   struct dd_call call;

   call.type = CALL_LAUNCH_GRID;
   call.info.launch_grid = *info;

   dd_before_draw(dctx);
   pipe->launch_grid(pipe, info);
   dd_after_draw(dctx, &call);
}

static void
dd_context_resource_copy_region(struct pipe_context *_pipe,
                                struct pipe_resource *dst, unsigned dst_level,
                                unsigned dstx, unsigned dsty, unsigned dstz,
                                struct pipe_resource *src, unsigned src_level,
                                const struct pipe_box *src_box)
{
   struct dd_context *dctx = dd_context(_pipe);
   struct pipe_context *pipe = dctx->pipe;
   struct dd_call call;

   call.type = CALL_RESOURCE_COPY_REGION;
   call.info.resource_copy_region.dst = dst;
   call.info.resource_copy_region.dst_level = dst_level;
   call.info.resource_copy_region.dstx = dstx;
   call.info.resource_copy_region.dsty = dsty;
   call.info.resource_copy_region.dstz = dstz;
   call.info.resource_copy_region.src = src;
   call.info.resource_copy_region.src_level = src_level;
   call.info.resource_copy_region.src_box = src_box;

   dd_before_draw(dctx);
   pipe->resource_copy_region(pipe,
                              dst, dst_level, dstx, dsty, dstz,
                              src, src_level, src_box);
   dd_after_draw(dctx, &call);
}

static void
dd_context_blit(struct pipe_context *_pipe, const struct pipe_blit_info *info)
{
   struct dd_context *dctx = dd_context(_pipe);
   struct pipe_context *pipe = dctx->pipe;
   struct dd_call call;

   call.type = CALL_BLIT;
   call.info.blit = *info;

   dd_before_draw(dctx);
   pipe->blit(pipe, info);
   dd_after_draw(dctx, &call);
}

static void
dd_context_flush_resource(struct pipe_context *_pipe,
                          struct pipe_resource *resource)
{
   struct dd_context *dctx = dd_context(_pipe);
   struct pipe_context *pipe = dctx->pipe;
   struct dd_call call;

   call.type = CALL_FLUSH_RESOURCE;
   call.info.flush_resource = resource;

   dd_before_draw(dctx);
   pipe->flush_resource(pipe, resource);
   dd_after_draw(dctx, &call);
}

static void
dd_context_clear(struct pipe_context *_pipe, unsigned buffers,
                 const union pipe_color_union *color, double depth,
                 unsigned stencil)
{
   struct dd_context *dctx = dd_context(_pipe);
   struct pipe_context *pipe = dctx->pipe;
   struct dd_call call;

   call.type = CALL_CLEAR;
   call.info.clear.buffers = buffers;
   call.info.clear.color = color;
   call.info.clear.depth = depth;
   call.info.clear.stencil = stencil;

   dd_before_draw(dctx);
   pipe->clear(pipe, buffers, color, depth, stencil);
   dd_after_draw(dctx, &call);
}

static void
dd_context_clear_render_target(struct pipe_context *_pipe,
                               struct pipe_surface *dst,
                               const union pipe_color_union *color,
                               unsigned dstx, unsigned dsty,
                               unsigned width, unsigned height)
{
   struct dd_context *dctx = dd_context(_pipe);
   struct pipe_context *pipe = dctx->pipe;
   struct dd_call call;

   call.type = CALL_CLEAR_RENDER_TARGET;

   dd_before_draw(dctx);
   pipe->clear_render_target(pipe, dst, color, dstx, dsty, width, height);
   dd_after_draw(dctx, &call);
}

static void
dd_context_clear_depth_stencil(struct pipe_context *_pipe,
                               struct pipe_surface *dst, unsigned clear_flags,
                               double depth, unsigned stencil, unsigned dstx,
                               unsigned dsty, unsigned width, unsigned height)
{
   struct dd_context *dctx = dd_context(_pipe);
   struct pipe_context *pipe = dctx->pipe;
   struct dd_call call;

   call.type = CALL_CLEAR_DEPTH_STENCIL;

   dd_before_draw(dctx);
   pipe->clear_depth_stencil(pipe, dst, clear_flags, depth, stencil,
                             dstx, dsty, width, height);
   dd_after_draw(dctx, &call);
}

static void
dd_context_clear_buffer(struct pipe_context *_pipe, struct pipe_resource *res,
                        unsigned offset, unsigned size,
                        const void *clear_value, int clear_value_size)
{
   struct dd_context *dctx = dd_context(_pipe);
   struct pipe_context *pipe = dctx->pipe;
   struct dd_call call;

   call.type = CALL_CLEAR_BUFFER;
   call.info.clear_buffer.res = res;
   call.info.clear_buffer.offset = offset;
   call.info.clear_buffer.size = size;
   call.info.clear_buffer.clear_value = clear_value;
   call.info.clear_buffer.clear_value_size = clear_value_size;

   dd_before_draw(dctx);
   pipe->clear_buffer(pipe, res, offset, size, clear_value, clear_value_size);
   dd_after_draw(dctx, &call);
}

void
dd_init_draw_functions(struct dd_context *dctx)
{
   CTX_INIT(flush);
   CTX_INIT(draw_vbo);
   CTX_INIT(launch_grid);
   CTX_INIT(resource_copy_region);
   CTX_INIT(blit);
   CTX_INIT(clear);
   CTX_INIT(clear_render_target);
   CTX_INIT(clear_depth_stencil);
   CTX_INIT(clear_buffer);
   CTX_INIT(flush_resource);
   /* launch_grid */
}
