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
#include "util/u_framebuffer.h"
#include "util/u_helpers.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_scan.h"
#include "os/os_time.h"
#include <inttypes.h>


static FILE *
dd_get_file_stream(struct dd_screen *dscreen, unsigned apitrace_call_number)
{
   struct pipe_screen *screen = dscreen->screen;
   char cmd_line[4096];

   FILE *f = dd_get_debug_file(dscreen->verbose);
   if (!f)
      return NULL;

   if (os_get_command_line(cmd_line, sizeof(cmd_line)))
      fprintf(f, "Command: %s\n", cmd_line);
   fprintf(f, "Driver vendor: %s\n", screen->get_vendor(screen));
   fprintf(f, "Device vendor: %s\n", screen->get_device_vendor(screen));
   fprintf(f, "Device name: %s\n\n", screen->get_name(screen));

   if (apitrace_call_number)
      fprintf(f, "Last apitrace call: %u\n\n",
              apitrace_call_number);
   return f;
}

static void
dd_dump_dmesg(FILE *f)
{
   char line[2000];
   FILE *p = popen("dmesg | tail -n60", "r");

   if (!p)
      return;

   fprintf(f, "\nLast 60 lines of dmesg:\n\n");
   while (fgets(line, sizeof(line), p))
      fputs(line, f);

   pclose(p);
}

static void
dd_close_file_stream(FILE *f)
{
   fclose(f);
}

static unsigned
dd_num_active_viewports(struct dd_draw_state *dstate)
{
   struct tgsi_shader_info info;
   const struct tgsi_token *tokens;

   if (dstate->shaders[PIPE_SHADER_GEOMETRY])
      tokens = dstate->shaders[PIPE_SHADER_GEOMETRY]->state.shader.tokens;
   else if (dstate->shaders[PIPE_SHADER_TESS_EVAL])
      tokens = dstate->shaders[PIPE_SHADER_TESS_EVAL]->state.shader.tokens;
   else if (dstate->shaders[PIPE_SHADER_VERTEX])
      tokens = dstate->shaders[PIPE_SHADER_VERTEX]->state.shader.tokens;
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
dd_dump_render_condition(struct dd_draw_state *dstate, FILE *f)
{
   if (dstate->render_cond.query) {
      fprintf(f, "render condition:\n");
      DUMP_M(query, &dstate->render_cond, query);
      DUMP_M(uint, &dstate->render_cond, condition);
      DUMP_M(uint, &dstate->render_cond, mode);
      fprintf(f, "\n");
   }
}

static void
dd_dump_draw_vbo(struct dd_draw_state *dstate, struct pipe_draw_info *info, FILE *f)
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
      DUMP(index_buffer, &dstate->index_buffer);
      if (dstate->index_buffer.buffer)
         DUMP_M(resource, &dstate->index_buffer, buffer);
   }
   if (info->count_from_stream_output)
      DUMP_M(stream_output_target, info,
             count_from_stream_output);
   if (info->indirect)
      DUMP_M(resource, info, indirect);
   fprintf(f, "\n");

   /* TODO: dump active queries */

   dd_dump_render_condition(dstate, f);

   for (i = 0; i < PIPE_MAX_ATTRIBS; i++)
      if (dstate->vertex_buffers[i].buffer ||
          dstate->vertex_buffers[i].user_buffer) {
         DUMP_I(vertex_buffer, &dstate->vertex_buffers[i], i);
         if (dstate->vertex_buffers[i].buffer)
            DUMP_M(resource, &dstate->vertex_buffers[i], buffer);
      }

   if (dstate->velems) {
      print_named_value(f, "num vertex elements",
                        dstate->velems->state.velems.count);
      for (i = 0; i < dstate->velems->state.velems.count; i++) {
         fprintf(f, "  ");
         DUMP_I(vertex_element, &dstate->velems->state.velems.velems[i], i);
      }
   }

   print_named_value(f, "num stream output targets", dstate->num_so_targets);
   for (i = 0; i < dstate->num_so_targets; i++)
      if (dstate->so_targets[i]) {
         DUMP_I(stream_output_target, dstate->so_targets[i], i);
         DUMP_M(resource, dstate->so_targets[i], buffer);
         fprintf(f, "  offset = %i\n", dstate->so_offsets[i]);
      }

   fprintf(f, "\n");
   for (sh = 0; sh < PIPE_SHADER_TYPES; sh++) {
      if (sh == PIPE_SHADER_COMPUTE)
         continue;

      if (sh == PIPE_SHADER_TESS_CTRL &&
          !dstate->shaders[PIPE_SHADER_TESS_CTRL] &&
          dstate->shaders[PIPE_SHADER_TESS_EVAL])
         fprintf(f, "tess_state: {default_outer_level = {%f, %f, %f, %f}, "
                 "default_inner_level = {%f, %f}}\n",
                 dstate->tess_default_levels[0],
                 dstate->tess_default_levels[1],
                 dstate->tess_default_levels[2],
                 dstate->tess_default_levels[3],
                 dstate->tess_default_levels[4],
                 dstate->tess_default_levels[5]);

      if (sh == PIPE_SHADER_FRAGMENT)
         if (dstate->rs) {
            unsigned num_viewports = dd_num_active_viewports(dstate);

            if (dstate->rs->state.rs.clip_plane_enable)
               DUMP(clip_state, &dstate->clip_state);

            for (i = 0; i < num_viewports; i++)
               DUMP_I(viewport_state, &dstate->viewports[i], i);

            if (dstate->rs->state.rs.scissor)
               for (i = 0; i < num_viewports; i++)
                  DUMP_I(scissor_state, &dstate->scissors[i], i);

            DUMP(rasterizer_state, &dstate->rs->state.rs);

            if (dstate->rs->state.rs.poly_stipple_enable)
               DUMP(poly_stipple, &dstate->polygon_stipple);
            fprintf(f, "\n");
         }

      if (!dstate->shaders[sh])
         continue;

      fprintf(f, COLOR_SHADER "begin shader: %s" COLOR_RESET "\n", shader_str[sh]);
      DUMP(shader_state, &dstate->shaders[sh]->state.shader);

      for (i = 0; i < PIPE_MAX_CONSTANT_BUFFERS; i++)
         if (dstate->constant_buffers[sh][i].buffer ||
             dstate->constant_buffers[sh][i].user_buffer) {
            DUMP_I(constant_buffer, &dstate->constant_buffers[sh][i], i);
            if (dstate->constant_buffers[sh][i].buffer)
               DUMP_M(resource, &dstate->constant_buffers[sh][i], buffer);
         }

      for (i = 0; i < PIPE_MAX_SAMPLERS; i++)
         if (dstate->sampler_states[sh][i])
            DUMP_I(sampler_state, &dstate->sampler_states[sh][i]->state.sampler, i);

      for (i = 0; i < PIPE_MAX_SAMPLERS; i++)
         if (dstate->sampler_views[sh][i]) {
            DUMP_I(sampler_view, dstate->sampler_views[sh][i], i);
            DUMP_M(resource, dstate->sampler_views[sh][i], texture);
         }

      for (i = 0; i < PIPE_MAX_SHADER_IMAGES; i++)
         if (dstate->shader_images[sh][i].resource) {
            DUMP_I(image_view, &dstate->shader_images[sh][i], i);
            if (dstate->shader_images[sh][i].resource)
               DUMP_M(resource, &dstate->shader_images[sh][i], resource);
         }

      for (i = 0; i < PIPE_MAX_SHADER_BUFFERS; i++)
         if (dstate->shader_buffers[sh][i].buffer) {
            DUMP_I(shader_buffer, &dstate->shader_buffers[sh][i], i);
            if (dstate->shader_buffers[sh][i].buffer)
               DUMP_M(resource, &dstate->shader_buffers[sh][i], buffer);
         }

      fprintf(f, COLOR_SHADER "end shader: %s" COLOR_RESET "\n\n", shader_str[sh]);
   }

   if (dstate->dsa)
      DUMP(depth_stencil_alpha_state, &dstate->dsa->state.dsa);
   DUMP(stencil_ref, &dstate->stencil_ref);

   if (dstate->blend)
      DUMP(blend_state, &dstate->blend->state.blend);
   DUMP(blend_color, &dstate->blend_color);

   print_named_value(f, "min_samples", dstate->min_samples);
   print_named_xvalue(f, "sample_mask", dstate->sample_mask);
   fprintf(f, "\n");

   DUMP(framebuffer_state, &dstate->framebuffer_state);
   for (i = 0; i < dstate->framebuffer_state.nr_cbufs; i++)
      if (dstate->framebuffer_state.cbufs[i]) {
         fprintf(f, "  " COLOR_STATE "cbufs[%i]:" COLOR_RESET "\n    ", i);
         DUMP(surface, dstate->framebuffer_state.cbufs[i]);
         fprintf(f, "    ");
         DUMP(resource, dstate->framebuffer_state.cbufs[i]->texture);
      }
   if (dstate->framebuffer_state.zsbuf) {
      fprintf(f, "  " COLOR_STATE "zsbuf:" COLOR_RESET "\n    ");
      DUMP(surface, dstate->framebuffer_state.zsbuf);
      fprintf(f, "    ");
      DUMP(resource, dstate->framebuffer_state.zsbuf->texture);
   }
   fprintf(f, "\n");
}

static void
dd_dump_launch_grid(struct dd_draw_state *dstate, struct pipe_grid_info *info, FILE *f)
{
   fprintf(f, "%s:\n", __func__+8);
   /* TODO */
}

static void
dd_dump_resource_copy_region(struct dd_draw_state *dstate,
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
   DUMP_M_ADDR(box, info, src_box);
}

static void
dd_dump_blit(struct dd_draw_state *dstate, struct pipe_blit_info *info, FILE *f)
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
      dd_dump_render_condition(dstate, f);
}

static void
dd_dump_generate_mipmap(struct dd_draw_state *dstate, FILE *f)
{
   fprintf(f, "%s:\n", __func__+8);
   /* TODO */
}

static void
dd_dump_flush_resource(struct dd_draw_state *dstate, struct pipe_resource *res,
                       FILE *f)
{
   fprintf(f, "%s:\n", __func__+8);
   DUMP(resource, res);
}

static void
dd_dump_clear(struct dd_draw_state *dstate, struct call_clear *info, FILE *f)
{
   fprintf(f, "%s:\n", __func__+8);
   DUMP_M(uint, info, buffers);
   DUMP_M_ADDR(color_union, info, color);
   DUMP_M(double, info, depth);
   DUMP_M(hex, info, stencil);
}

static void
dd_dump_clear_buffer(struct dd_draw_state *dstate, struct call_clear_buffer *info,
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
dd_dump_clear_render_target(struct dd_draw_state *dstate, FILE *f)
{
   fprintf(f, "%s:\n", __func__+8);
   /* TODO */
}

static void
dd_dump_clear_depth_stencil(struct dd_draw_state *dstate, FILE *f)
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
dd_dump_call(FILE *f, struct dd_draw_state *state, struct dd_call *call)
{
   switch (call->type) {
   case CALL_DRAW_VBO:
      dd_dump_draw_vbo(state, &call->info.draw_vbo, f);
      break;
   case CALL_LAUNCH_GRID:
      dd_dump_launch_grid(state, &call->info.launch_grid, f);
      break;
   case CALL_RESOURCE_COPY_REGION:
      dd_dump_resource_copy_region(state,
                                   &call->info.resource_copy_region, f);
      break;
   case CALL_BLIT:
      dd_dump_blit(state, &call->info.blit, f);
      break;
   case CALL_FLUSH_RESOURCE:
      dd_dump_flush_resource(state, call->info.flush_resource, f);
      break;
   case CALL_CLEAR:
      dd_dump_clear(state, &call->info.clear, f);
      break;
   case CALL_CLEAR_BUFFER:
      dd_dump_clear_buffer(state, &call->info.clear_buffer, f);
      break;
   case CALL_CLEAR_RENDER_TARGET:
      dd_dump_clear_render_target(state, f);
      break;
   case CALL_CLEAR_DEPTH_STENCIL:
      dd_dump_clear_depth_stencil(state, f);
      break;
   case CALL_GENERATE_MIPMAP:
      dd_dump_generate_mipmap(state, f);
      break;
   }
}

static void
dd_write_report(struct dd_context *dctx, struct dd_call *call, unsigned flags,
                bool dump_dmesg)
{
   FILE *f = dd_get_file_stream(dd_screen(dctx->base.screen),
                                dctx->draw_state.apitrace_call_number);

   if (!f)
      return;

   dd_dump_call(f, &dctx->draw_state, call);
   dd_dump_driver_state(dctx, f, flags);
   if (dump_dmesg)
      dd_dump_dmesg(f);
   dd_close_file_stream(f);
}

static void
dd_kill_process(void)
{
   sync();
   fprintf(stderr, "dd: Aborting the process...\n");
   fflush(stdout);
   fflush(stderr);
   exit(1);
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

   idle = screen->fence_finish(screen, pipe, fence, timeout_ms * 1000000);
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
      FILE *f = dd_get_file_stream(dd_screen(dctx->base.screen),
                                   dctx->draw_state.apitrace_call_number);

      if (f) {
         fprintf(f, "dd: %s.\n", cause);
         dd_dump_driver_state(dctx, f,
                              PIPE_DUMP_DEVICE_STATUS_REGISTERS |
                              PIPE_DUMP_CURRENT_STATES |
                              PIPE_DUMP_CURRENT_SHADERS |
                              PIPE_DUMP_LAST_COMMAND_BUFFER);
         dd_dump_dmesg(f);
         dd_close_file_stream(f);
      }

      /* Terminate the process to prevent future hangs. */
      dd_kill_process();
   }
}

static void
dd_unreference_copy_of_call(struct dd_call *dst)
{
   switch (dst->type) {
   case CALL_DRAW_VBO:
      pipe_so_target_reference(&dst->info.draw_vbo.count_from_stream_output, NULL);
      pipe_resource_reference(&dst->info.draw_vbo.indirect, NULL);
      pipe_resource_reference(&dst->info.draw_vbo.indirect_params, NULL);
      break;
   case CALL_LAUNCH_GRID:
      pipe_resource_reference(&dst->info.launch_grid.indirect, NULL);
      break;
   case CALL_RESOURCE_COPY_REGION:
      pipe_resource_reference(&dst->info.resource_copy_region.dst, NULL);
      pipe_resource_reference(&dst->info.resource_copy_region.src, NULL);
      break;
   case CALL_BLIT:
      pipe_resource_reference(&dst->info.blit.dst.resource, NULL);
      pipe_resource_reference(&dst->info.blit.src.resource, NULL);
      break;
   case CALL_FLUSH_RESOURCE:
      pipe_resource_reference(&dst->info.flush_resource, NULL);
      break;
   case CALL_CLEAR:
      break;
   case CALL_CLEAR_BUFFER:
      pipe_resource_reference(&dst->info.clear_buffer.res, NULL);
      break;
   case CALL_CLEAR_RENDER_TARGET:
      break;
   case CALL_CLEAR_DEPTH_STENCIL:
      break;
   case CALL_GENERATE_MIPMAP:
      pipe_resource_reference(&dst->info.generate_mipmap.res, NULL);
      break;
   }
}

static void
dd_copy_call(struct dd_call *dst, struct dd_call *src)
{
   dst->type = src->type;

   switch (src->type) {
   case CALL_DRAW_VBO:
      pipe_so_target_reference(&dst->info.draw_vbo.count_from_stream_output,
                               src->info.draw_vbo.count_from_stream_output);
      pipe_resource_reference(&dst->info.draw_vbo.indirect,
                              src->info.draw_vbo.indirect);
      pipe_resource_reference(&dst->info.draw_vbo.indirect_params,
                              src->info.draw_vbo.indirect_params);
      dst->info.draw_vbo = src->info.draw_vbo;
      break;
   case CALL_LAUNCH_GRID:
      pipe_resource_reference(&dst->info.launch_grid.indirect,
                              src->info.launch_grid.indirect);
      dst->info.launch_grid = src->info.launch_grid;
      break;
   case CALL_RESOURCE_COPY_REGION:
      pipe_resource_reference(&dst->info.resource_copy_region.dst,
                              src->info.resource_copy_region.dst);
      pipe_resource_reference(&dst->info.resource_copy_region.src,
                              src->info.resource_copy_region.src);
      dst->info.resource_copy_region = src->info.resource_copy_region;
      break;
   case CALL_BLIT:
      pipe_resource_reference(&dst->info.blit.dst.resource,
                              src->info.blit.dst.resource);
      pipe_resource_reference(&dst->info.blit.src.resource,
                              src->info.blit.src.resource);
      dst->info.blit = src->info.blit;
      break;
   case CALL_FLUSH_RESOURCE:
      pipe_resource_reference(&dst->info.flush_resource,
                              src->info.flush_resource);
      break;
   case CALL_CLEAR:
      dst->info.clear = src->info.clear;
      break;
   case CALL_CLEAR_BUFFER:
      pipe_resource_reference(&dst->info.clear_buffer.res,
                              src->info.clear_buffer.res);
      dst->info.clear_buffer = src->info.clear_buffer;
      break;
   case CALL_CLEAR_RENDER_TARGET:
      break;
   case CALL_CLEAR_DEPTH_STENCIL:
      break;
   case CALL_GENERATE_MIPMAP:
      pipe_resource_reference(&dst->info.generate_mipmap.res,
                              src->info.generate_mipmap.res);
      dst->info.generate_mipmap = src->info.generate_mipmap;
      break;
   }
}

static void
dd_init_copy_of_draw_state(struct dd_draw_state_copy *state)
{
   unsigned i,j;

   /* Just clear pointers to gallium objects. Don't clear the whole structure,
    * because it would kill performance with its size of 130 KB.
    */
   memset(&state->base.index_buffer, 0,
          sizeof(state->base.index_buffer));
   memset(state->base.vertex_buffers, 0,
          sizeof(state->base.vertex_buffers));
   memset(state->base.so_targets, 0,
          sizeof(state->base.so_targets));
   memset(state->base.constant_buffers, 0,
          sizeof(state->base.constant_buffers));
   memset(state->base.sampler_views, 0,
          sizeof(state->base.sampler_views));
   memset(state->base.shader_images, 0,
          sizeof(state->base.shader_images));
   memset(state->base.shader_buffers, 0,
          sizeof(state->base.shader_buffers));
   memset(&state->base.framebuffer_state, 0,
          sizeof(state->base.framebuffer_state));

   memset(state->shaders, 0, sizeof(state->shaders));

   state->base.render_cond.query = &state->render_cond;

   for (i = 0; i < PIPE_SHADER_TYPES; i++) {
      state->base.shaders[i] = &state->shaders[i];
      for (j = 0; j < PIPE_MAX_SAMPLERS; j++)
         state->base.sampler_states[i][j] = &state->sampler_states[i][j];
   }

   state->base.velems = &state->velems;
   state->base.rs = &state->rs;
   state->base.dsa = &state->dsa;
   state->base.blend = &state->blend;
}

static void
dd_unreference_copy_of_draw_state(struct dd_draw_state_copy *state)
{
   struct dd_draw_state *dst = &state->base;
   unsigned i,j;

   util_set_index_buffer(&dst->index_buffer, NULL);

   for (i = 0; i < ARRAY_SIZE(dst->vertex_buffers); i++)
      pipe_resource_reference(&dst->vertex_buffers[i].buffer, NULL);
   for (i = 0; i < ARRAY_SIZE(dst->so_targets); i++)
      pipe_so_target_reference(&dst->so_targets[i], NULL);

   for (i = 0; i < PIPE_SHADER_TYPES; i++) {
      if (dst->shaders[i])
         tgsi_free_tokens(dst->shaders[i]->state.shader.tokens);

      for (j = 0; j < PIPE_MAX_CONSTANT_BUFFERS; j++)
         pipe_resource_reference(&dst->constant_buffers[i][j].buffer, NULL);
      for (j = 0; j < PIPE_MAX_SAMPLERS; j++)
         pipe_sampler_view_reference(&dst->sampler_views[i][j], NULL);
      for (j = 0; j < PIPE_MAX_SHADER_IMAGES; j++)
         pipe_resource_reference(&dst->shader_images[i][j].resource, NULL);
      for (j = 0; j < PIPE_MAX_SHADER_BUFFERS; j++)
         pipe_resource_reference(&dst->shader_buffers[i][j].buffer, NULL);
   }

   util_unreference_framebuffer_state(&dst->framebuffer_state);
}

static void
dd_copy_draw_state(struct dd_draw_state *dst, struct dd_draw_state *src)
{
   unsigned i,j;

   if (src->render_cond.query) {
      *dst->render_cond.query = *src->render_cond.query;
      dst->render_cond.condition = src->render_cond.condition;
      dst->render_cond.mode = src->render_cond.mode;
   } else {
      dst->render_cond.query = NULL;
   }

   util_set_index_buffer(&dst->index_buffer, &src->index_buffer);

   for (i = 0; i < ARRAY_SIZE(src->vertex_buffers); i++) {
      pipe_resource_reference(&dst->vertex_buffers[i].buffer,
                              src->vertex_buffers[i].buffer);
      memcpy(&dst->vertex_buffers[i], &src->vertex_buffers[i],
             sizeof(src->vertex_buffers[i]));
   }

   dst->num_so_targets = src->num_so_targets;
   for (i = 0; i < ARRAY_SIZE(src->so_targets); i++)
      pipe_so_target_reference(&dst->so_targets[i], src->so_targets[i]);
   memcpy(dst->so_offsets, src->so_offsets, sizeof(src->so_offsets));

   for (i = 0; i < PIPE_SHADER_TYPES; i++) {
      if (!src->shaders[i]) {
         dst->shaders[i] = NULL;
         continue;
      }

      if (src->shaders[i]) {
         dst->shaders[i]->state.shader = src->shaders[i]->state.shader;
         dst->shaders[i]->state.shader.tokens =
            tgsi_dup_tokens(src->shaders[i]->state.shader.tokens);
      } else {
         dst->shaders[i] = NULL;
      }

      for (j = 0; j < PIPE_MAX_CONSTANT_BUFFERS; j++) {
         pipe_resource_reference(&dst->constant_buffers[i][j].buffer,
                                 src->constant_buffers[i][j].buffer);
         memcpy(&dst->constant_buffers[i][j], &src->constant_buffers[i][j],
                sizeof(src->constant_buffers[i][j]));
      }

      for (j = 0; j < PIPE_MAX_SAMPLERS; j++) {
         pipe_sampler_view_reference(&dst->sampler_views[i][j],
                                     src->sampler_views[i][j]);
         if (src->sampler_states[i][j])
            dst->sampler_states[i][j]->state.sampler =
               src->sampler_states[i][j]->state.sampler;
         else
            dst->sampler_states[i][j] = NULL;
      }

      for (j = 0; j < PIPE_MAX_SHADER_IMAGES; j++) {
         pipe_resource_reference(&dst->shader_images[i][j].resource,
                                 src->shader_images[i][j].resource);
         memcpy(&dst->shader_images[i][j], &src->shader_images[i][j],
                sizeof(src->shader_images[i][j]));
      }

      for (j = 0; j < PIPE_MAX_SHADER_BUFFERS; j++) {
         pipe_resource_reference(&dst->shader_buffers[i][j].buffer,
                                 src->shader_buffers[i][j].buffer);
         memcpy(&dst->shader_buffers[i][j], &src->shader_buffers[i][j],
                sizeof(src->shader_buffers[i][j]));
      }
   }

   if (src->velems)
      dst->velems->state.velems = src->velems->state.velems;
   else
      dst->velems = NULL;

   if (src->rs)
      dst->rs->state.rs = src->rs->state.rs;
   else
      dst->rs = NULL;

   if (src->dsa)
      dst->dsa->state.dsa = src->dsa->state.dsa;
   else
      dst->dsa = NULL;

   if (src->blend)
      dst->blend->state.blend = src->blend->state.blend;
   else
      dst->blend = NULL;

   dst->blend_color = src->blend_color;
   dst->stencil_ref = src->stencil_ref;
   dst->sample_mask = src->sample_mask;
   dst->min_samples = src->min_samples;
   dst->clip_state = src->clip_state;
   util_copy_framebuffer_state(&dst->framebuffer_state, &src->framebuffer_state);
   memcpy(dst->scissors, src->scissors, sizeof(src->scissors));
   memcpy(dst->viewports, src->viewports, sizeof(src->viewports));
   memcpy(dst->tess_default_levels, src->tess_default_levels,
          sizeof(src->tess_default_levels));
   dst->apitrace_call_number = src->apitrace_call_number;
}

static void
dd_free_record(struct dd_draw_record **record)
{
   struct dd_draw_record *next = (*record)->next;

   dd_unreference_copy_of_call(&(*record)->call);
   dd_unreference_copy_of_draw_state(&(*record)->draw_state);
   FREE((*record)->driver_state_log);
   FREE(*record);
   *record = next;
}

static void
dd_dump_record(struct dd_context *dctx, struct dd_draw_record *record,
               uint32_t hw_sequence_no, int64_t now)
{
   FILE *f = dd_get_file_stream(dd_screen(dctx->base.screen),
                                record->draw_state.base.apitrace_call_number);
   if (!f)
      return;

   fprintf(f, "Draw call sequence # = %u\n", record->sequence_no);
   fprintf(f, "HW reached sequence # = %u\n", hw_sequence_no);
   fprintf(f, "Elapsed time = %"PRIi64" ms\n\n",
           (now - record->timestamp) / 1000);

   dd_dump_call(f, &record->draw_state.base, &record->call);
   fprintf(f, "%s\n", record->driver_state_log);

   dctx->pipe->dump_debug_state(dctx->pipe, f,
                                PIPE_DUMP_DEVICE_STATUS_REGISTERS);
   dd_dump_dmesg(f);
   fclose(f);
}

PIPE_THREAD_ROUTINE(dd_thread_pipelined_hang_detect, input)
{
   struct dd_context *dctx = (struct dd_context *)input;
   struct dd_screen *dscreen = dd_screen(dctx->base.screen);

   pipe_mutex_lock(dctx->mutex);

   while (!dctx->kill_thread) {
      struct dd_draw_record **record = &dctx->records;

      /* Loop over all records. */
      while (*record) {
         int64_t now;

         /* If the fence has been signalled, release the record and all older
          * records.
          */
         if (*dctx->mapped_fence >= (*record)->sequence_no) {
            while (*record)
               dd_free_record(record);
            break;
         }

         /* The fence hasn't been signalled. Check the timeout. */
         now = os_time_get();
         if (os_time_timeout((*record)->timestamp,
                             (*record)->timestamp + dscreen->timeout_ms * 1000,
                             now)) {
            fprintf(stderr, "GPU hang detected.\n");

            /* Get the oldest unsignalled draw call. */
            while ((*record)->next &&
                   *dctx->mapped_fence < (*record)->next->sequence_no)
               record = &(*record)->next;

            dd_dump_record(dctx, *record, *dctx->mapped_fence, now);
            dd_kill_process();
         }

         record = &(*record)->next;
      }

      /* Unlock and sleep before starting all over again. */
      pipe_mutex_unlock(dctx->mutex);
      os_time_sleep(10000); /* 10 ms */
      pipe_mutex_lock(dctx->mutex);
   }

   /* Thread termination. */
   while (dctx->records)
      dd_free_record(&dctx->records);

   pipe_mutex_unlock(dctx->mutex);
   return 0;
}

static char *
dd_get_driver_shader_log(struct dd_context *dctx)
{
#if defined(PIPE_OS_LINUX)
   FILE *f;
   char *buf;
   int written_bytes;

   if (!dctx->max_log_buffer_size)
      dctx->max_log_buffer_size = 16 * 1024;

   /* Keep increasing the buffer size until there is enough space.
    *
    * open_memstream can resize automatically, but it's VERY SLOW.
    * fmemopen is much faster.
    */
   while (1) {
      buf = malloc(dctx->max_log_buffer_size);
      buf[0] = 0;

      f = fmemopen(buf, dctx->max_log_buffer_size, "a");
      if (!f) {
         free(buf);
         return NULL;
      }

      dd_dump_driver_state(dctx, f, PIPE_DUMP_CURRENT_SHADERS);
      written_bytes = ftell(f);
      fclose(f);

      /* Return if the backing buffer is large enough. */
      if (written_bytes < dctx->max_log_buffer_size - 1)
         break;

      /* Try again. */
      free(buf);
      dctx->max_log_buffer_size *= 2;
   }

   return buf;
#else
   /* Return an empty string. */
   return (char*)calloc(1, 4);
#endif
}

static void
dd_pipelined_process_draw(struct dd_context *dctx, struct dd_call *call)
{
   struct pipe_context *pipe = dctx->pipe;
   struct dd_draw_record *record;
   char *log;

   /* Make a record of the draw call. */
   record = MALLOC_STRUCT(dd_draw_record);
   if (!record)
      return;

   /* Create the log. */
   log = dd_get_driver_shader_log(dctx);
   if (!log) {
      FREE(record);
      return;
   }

   /* Update the fence with the GPU.
    *
    * radeonsi/clear_buffer waits in the command processor until shaders are
    * idle before writing to memory. That's a necessary condition for isolating
    * draw calls.
    */
   dctx->sequence_no++;
   pipe->clear_buffer(pipe, dctx->fence, 0, 4, &dctx->sequence_no, 4);

   /* Initialize the record. */
   record->timestamp = os_time_get();
   record->sequence_no = dctx->sequence_no;
   record->driver_state_log = log;

   memset(&record->call, 0, sizeof(record->call));
   dd_copy_call(&record->call, call);

   dd_init_copy_of_draw_state(&record->draw_state);
   dd_copy_draw_state(&record->draw_state.base, &dctx->draw_state);

   /* Add the record to the list. */
   pipe_mutex_lock(dctx->mutex);
   record->next = dctx->records;
   dctx->records = record;
   pipe_mutex_unlock(dctx->mutex);
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
   case DD_DETECT_HANGS_PIPELINED: /* nothing to do here */
   case DD_DUMP_ALL_CALLS:
   case DD_DUMP_APITRACE_CALL:
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
            dd_write_report(dctx, call,
                         PIPE_DUMP_DEVICE_STATUS_REGISTERS |
                         PIPE_DUMP_CURRENT_STATES |
                         PIPE_DUMP_CURRENT_SHADERS |
                         PIPE_DUMP_LAST_COMMAND_BUFFER,
                         true);

            /* Terminate the process to prevent future hangs. */
            dd_kill_process();
         }
         break;
      case DD_DETECT_HANGS_PIPELINED:
         dd_pipelined_process_draw(dctx, call);
         break;
      case DD_DUMP_ALL_CALLS:
         if (!dscreen->no_flush)
            pipe->flush(pipe, NULL, 0);
         dd_write_report(dctx, call,
                         PIPE_DUMP_CURRENT_STATES |
                         PIPE_DUMP_CURRENT_SHADERS |
                         PIPE_DUMP_LAST_COMMAND_BUFFER,
                         false);
         break;
      case DD_DUMP_APITRACE_CALL:
         if (dscreen->apitrace_dump_call ==
             dctx->draw_state.apitrace_call_number) {
            dd_write_report(dctx, call,
                            PIPE_DUMP_CURRENT_STATES |
                            PIPE_DUMP_CURRENT_SHADERS,
                            false);
            /* No need to continue. */
            exit(0);
         }
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
   call.info.resource_copy_region.src_box = *src_box;

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

static boolean
dd_context_generate_mipmap(struct pipe_context *_pipe,
                           struct pipe_resource *res,
                           enum pipe_format format,
                           unsigned base_level,
                           unsigned last_level,
                           unsigned first_layer,
                           unsigned last_layer)
{
   struct dd_context *dctx = dd_context(_pipe);
   struct pipe_context *pipe = dctx->pipe;
   struct dd_call call;
   boolean result;

   call.type = CALL_GENERATE_MIPMAP;
   call.info.generate_mipmap.res = res;
   call.info.generate_mipmap.format = format;
   call.info.generate_mipmap.base_level = base_level;
   call.info.generate_mipmap.last_level = last_level;
   call.info.generate_mipmap.first_layer = first_layer;
   call.info.generate_mipmap.last_layer = last_layer;

   dd_before_draw(dctx);
   result = pipe->generate_mipmap(pipe, res, format, base_level, last_level,
                                  first_layer, last_layer);
   dd_after_draw(dctx, &call);
   return result;
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
   call.info.clear.color = *color;
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
                               unsigned width, unsigned height,
                               bool render_condition_enabled)
{
   struct dd_context *dctx = dd_context(_pipe);
   struct pipe_context *pipe = dctx->pipe;
   struct dd_call call;

   call.type = CALL_CLEAR_RENDER_TARGET;

   dd_before_draw(dctx);
   pipe->clear_render_target(pipe, dst, color, dstx, dsty, width, height,
                             render_condition_enabled);
   dd_after_draw(dctx, &call);
}

static void
dd_context_clear_depth_stencil(struct pipe_context *_pipe,
                               struct pipe_surface *dst, unsigned clear_flags,
                               double depth, unsigned stencil, unsigned dstx,
                               unsigned dsty, unsigned width, unsigned height,
                               bool render_condition_enabled)
{
   struct dd_context *dctx = dd_context(_pipe);
   struct pipe_context *pipe = dctx->pipe;
   struct dd_call call;

   call.type = CALL_CLEAR_DEPTH_STENCIL;

   dd_before_draw(dctx);
   pipe->clear_depth_stencil(pipe, dst, clear_flags, depth, stencil,
                             dstx, dsty, width, height,
                             render_condition_enabled);
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
   CTX_INIT(generate_mipmap);
}
