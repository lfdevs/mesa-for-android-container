/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
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
 ***************************************************************************/

#include "swr_screen.h"
#include "swr_context.h"
#include "swr_resource.h"
#include "swr_fence.h"
#include "swr_query.h"
#include "jit_api.h"

#include "util/u_draw.h"
#include "util/u_prim.h"

/*
 * Convert mesa PIPE_PRIM_X to SWR enum PRIMITIVE_TOPOLOGY
 */
static INLINE enum PRIMITIVE_TOPOLOGY
swr_convert_prim_topology(const unsigned mode)
{
   switch (mode) {
   case PIPE_PRIM_POINTS:
      return TOP_POINT_LIST;
   case PIPE_PRIM_LINES:
      return TOP_LINE_LIST;
   case PIPE_PRIM_LINE_LOOP:
      return TOP_LINE_LOOP;
   case PIPE_PRIM_LINE_STRIP:
      return TOP_LINE_STRIP;
   case PIPE_PRIM_TRIANGLES:
      return TOP_TRIANGLE_LIST;
   case PIPE_PRIM_TRIANGLE_STRIP:
      return TOP_TRIANGLE_STRIP;
   case PIPE_PRIM_TRIANGLE_FAN:
      return TOP_TRIANGLE_FAN;
   case PIPE_PRIM_QUADS:
      return TOP_QUAD_LIST;
   case PIPE_PRIM_QUAD_STRIP:
      return TOP_QUAD_STRIP;
   case PIPE_PRIM_POLYGON:
      return TOP_TRIANGLE_FAN; /* XXX TOP_POLYGON; */
   case PIPE_PRIM_LINES_ADJACENCY:
      return TOP_LINE_LIST_ADJ;
   case PIPE_PRIM_LINE_STRIP_ADJACENCY:
      return TOP_LISTSTRIP_ADJ;
   case PIPE_PRIM_TRIANGLES_ADJACENCY:
      return TOP_TRI_LIST_ADJ;
   case PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return TOP_TRI_STRIP_ADJ;
   default:
      assert(0 && "Unknown topology");
      return TOP_UNKNOWN;
   }
};


/*
 * Draw vertex arrays, with optional indexing, optional instancing.
 */
static void
swr_draw_vbo(struct pipe_context *pipe, const struct pipe_draw_info *info)
{
   struct swr_context *ctx = swr_context(pipe);

   if (!swr_check_render_cond(pipe))
      return;

   if (info->indirect) {
      util_draw_indirect(pipe, info);
      return;
   }

   /* Update derived state, pass draw info to update function */
   swr_update_derived(pipe, info);

   swr_update_draw_context(ctx);

   if (ctx->vs->pipe.stream_output.num_outputs) {
      if (!ctx->vs->soFunc[info->mode]) {
         STREAMOUT_COMPILE_STATE state = {0};
         struct pipe_stream_output_info *so = &ctx->vs->pipe.stream_output;

         state.numVertsPerPrim = u_vertices_per_prim(info->mode);

         uint32_t offsets[MAX_SO_STREAMS] = {0};
         uint32_t num = 0;

         for (uint32_t i = 0; i < so->num_outputs; i++) {
            assert(so->output[i].stream == 0); // @todo
            uint32_t output_buffer = so->output[i].output_buffer;
            if (so->output[i].dst_offset != offsets[output_buffer]) {
               // hole - need to fill
               state.stream.decl[num].bufferIndex = output_buffer;
               state.stream.decl[num].hole = true;
               state.stream.decl[num].componentMask =
                  (1 << (so->output[i].dst_offset - offsets[output_buffer]))
                  - 1;
               num++;
               offsets[output_buffer] = so->output[i].dst_offset;
            }

            state.stream.decl[num].bufferIndex = output_buffer;
            state.stream.decl[num].attribSlot = so->output[i].register_index - 1;
            state.stream.decl[num].componentMask =
               ((1 << so->output[i].num_components) - 1)
               << so->output[i].start_component;
            state.stream.decl[num].hole = false;
            num++;

            offsets[output_buffer] += so->output[i].num_components;
         }

         state.stream.numDecls = num;

         HANDLE hJitMgr = swr_screen(pipe->screen)->hJitMgr;
         ctx->vs->soFunc[info->mode] = JitCompileStreamout(hJitMgr, state);
         debug_printf("so shader    %p\n", ctx->vs->soFunc[info->mode]);
         assert(ctx->vs->soFunc[info->mode] && "Error: SoShader = NULL");
      }

      SwrSetSoFunc(ctx->swrContext, ctx->vs->soFunc[info->mode], 0);
   }

   struct swr_vertex_element_state *velems = ctx->velems;
   if (!velems->fsFunc
       || (velems->fsState.cutIndex != info->restart_index)
       || (velems->fsState.bEnableCutIndex != info->primitive_restart)) {

      velems->fsState.cutIndex = info->restart_index;
      velems->fsState.bEnableCutIndex = info->primitive_restart;

      /* Create Fetch Shader */
      HANDLE hJitMgr = swr_screen(ctx->pipe.screen)->hJitMgr;
      velems->fsFunc = JitCompileFetch(hJitMgr, velems->fsState);

      debug_printf("fetch shader %p\n", velems->fsFunc);
      assert(velems->fsFunc && "Error: FetchShader = NULL");
   }

   SwrSetFetchFunc(ctx->swrContext, velems->fsFunc);

   /* Set up frontend state
    * XXX setup provokingVertex & topologyProvokingVertex */
   SWR_FRONTEND_STATE feState = {0};
   if (ctx->rasterizer->flatshade_first) {
      feState.provokingVertex = {1, 0, 0};
   } else {
      feState.provokingVertex = {2, 1, 2};
   }

   switch (info->mode) {
   case PIPE_PRIM_TRIANGLE_FAN:
      feState.topologyProvokingVertex = feState.provokingVertex.triFan;
      break;
   case PIPE_PRIM_TRIANGLE_STRIP:
   case PIPE_PRIM_TRIANGLES:
      feState.topologyProvokingVertex = feState.provokingVertex.triStripList;
      break;
   case PIPE_PRIM_QUAD_STRIP:
   case PIPE_PRIM_QUADS:
      if (ctx->rasterizer->flatshade_first)
         feState.topologyProvokingVertex = 0;
      else
         feState.topologyProvokingVertex = 3;
      break;
   case PIPE_PRIM_LINES:
   case PIPE_PRIM_LINE_LOOP:
   case PIPE_PRIM_LINE_STRIP:
      feState.topologyProvokingVertex = feState.provokingVertex.lineStripList;
      break;
   default:
      feState.topologyProvokingVertex = 0;
   }

   feState.bEnableCutIndex = info->primitive_restart;
   SwrSetFrontendState(ctx->swrContext, &feState);

   if (info->indexed)
      SwrDrawIndexedInstanced(ctx->swrContext,
                              swr_convert_prim_topology(info->mode),
                              info->count,
                              info->instance_count,
                              info->start,
                              info->index_bias,
                              info->start_instance);
   else
      SwrDrawInstanced(ctx->swrContext,
                       swr_convert_prim_topology(info->mode),
                       info->count,
                       info->instance_count,
                       info->start,
                       info->start_instance);
}


static void
swr_flush(struct pipe_context *pipe,
          struct pipe_fence_handle **fence,
          unsigned flags)
{
   struct swr_context *ctx = swr_context(pipe);
   struct swr_screen *screen = swr_screen(pipe->screen);
   struct pipe_surface *cb = ctx->framebuffer.cbufs[0];

   /* If the current renderTarget is the display surface, store tiles back to
    * the surface, in preparation for present (swr_flush_frontbuffer).
    * Other renderTargets get stored back when attachment changes or
    * swr_surface_destroy */
   if (cb && swr_resource(cb->texture)->display_target)
      swr_store_dirty_resource(pipe, cb->texture, SWR_TILE_RESOLVED);

   if (fence)
      swr_fence_reference(pipe->screen, fence, screen->flush_fence);
}

void
swr_finish(struct pipe_context *pipe)
{
   struct pipe_fence_handle *fence = nullptr;

   swr_flush(pipe, &fence, 0);
   swr_fence_finish(pipe->screen, NULL, fence, 0);
   swr_fence_reference(pipe->screen, &fence, NULL);
}


/*
 * Store SWR HotTiles back to renderTarget surface.
 */
void
swr_store_render_target(struct pipe_context *pipe,
                        uint32_t attachment,
                        enum SWR_TILE_STATE post_tile_state)
{
   struct swr_context *ctx = swr_context(pipe);
   struct swr_draw_context *pDC = &ctx->swrDC;
   struct SWR_SURFACE_STATE *renderTarget = &pDC->renderTargets[attachment];

   /* Only proceed if there's a valid surface to store to */
   if (renderTarget->pBaseAddress) {
      swr_update_draw_context(ctx);
      SWR_RECT full_rect =
         {0, 0,
          (int32_t)u_minify(renderTarget->width, renderTarget->lod),
          (int32_t)u_minify(renderTarget->height, renderTarget->lod)};
      SwrStoreTiles(ctx->swrContext,
                    1 << attachment,
                    post_tile_state,
                    full_rect);
   }
}

void
swr_store_dirty_resource(struct pipe_context *pipe,
                         struct pipe_resource *resource,
                         enum SWR_TILE_STATE post_tile_state)
{
   /* Only store resource if it has been written to */
   if (swr_resource(resource)->status & SWR_RESOURCE_WRITE) {
      struct swr_context *ctx = swr_context(pipe);
      struct swr_screen *screen = swr_screen(pipe->screen);
      struct swr_resource *spr = swr_resource(resource);

      swr_draw_context *pDC = &ctx->swrDC;
      SWR_SURFACE_STATE *renderTargets = pDC->renderTargets;
      for (uint32_t i = 0; i < SWR_NUM_ATTACHMENTS; i++)
         if (renderTargets[i].pBaseAddress == spr->swr.pBaseAddress ||
             (spr->secondary.pBaseAddress &&
              renderTargets[i].pBaseAddress == spr->secondary.pBaseAddress)) {
            swr_store_render_target(pipe, i, post_tile_state);

            /* Mesa thinks depth/stencil are fused, so we'll never get an
             * explicit resource for stencil.  So, if checking depth, then
             * also check for stencil. */
            if (spr->has_stencil && (i == SWR_ATTACHMENT_DEPTH)) {
               swr_store_render_target(
                  pipe, SWR_ATTACHMENT_STENCIL, post_tile_state);
            }

            /* This fence signals StoreTiles completion */
            swr_fence_submit(ctx, screen->flush_fence);

            break;
         }
   }
}

void
swr_draw_init(struct pipe_context *pipe)
{
   pipe->draw_vbo = swr_draw_vbo;
   pipe->flush = swr_flush;
}
