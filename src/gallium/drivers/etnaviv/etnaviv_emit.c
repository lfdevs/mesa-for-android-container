/*
 * Copyright (c) 2014-2015 Etnaviv Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Wladimir J. van der Laan <laanwj@gmail.com>
 */

#include "etnaviv_emit.h"

#include "etnaviv_blend.h"
#include "etnaviv_compiler.h"
#include "etnaviv_context.h"
#include "etnaviv_rasterizer.h"
#include "etnaviv_resource.h"
#include "etnaviv_rs.h"
#include "etnaviv_screen.h"
#include "etnaviv_shader.h"
#include "etnaviv_texture.h"
#include "etnaviv_translate.h"
#include "etnaviv_uniforms.h"
#include "etnaviv_util.h"
#include "etnaviv_zsa.h"
#include "hw/common.xml.h"
#include "hw/state.xml.h"
#include "util/u_math.h"

struct etna_coalesce {
   uint32_t start;
   uint32_t last_reg;
   uint32_t last_fixp;
};

/* Queue a STALL command (queues 2 words) */
static inline void
CMD_STALL(struct etna_cmd_stream *stream, uint32_t from, uint32_t to)
{
   etna_cmd_stream_emit(stream, VIV_FE_STALL_HEADER_OP_STALL);
   etna_cmd_stream_emit(stream, VIV_FE_STALL_TOKEN_FROM(from) | VIV_FE_STALL_TOKEN_TO(to));
}

void
etna_stall(struct etna_cmd_stream *stream, uint32_t from, uint32_t to)
{
   etna_cmd_stream_reserve(stream, 4);

   etna_emit_load_state(stream, VIVS_GL_SEMAPHORE_TOKEN >> 2, 1, 0);
   etna_cmd_stream_emit(stream, VIVS_GL_SEMAPHORE_TOKEN_FROM(from) | VIVS_GL_SEMAPHORE_TOKEN_TO(to));

   if (from == SYNC_RECIPIENT_FE) {
      /* if the frontend is to be stalled, queue a STALL frontend command */
      CMD_STALL(stream, from, to);
   } else {
      /* otherwise, load the STALL token state */
      etna_emit_load_state(stream, VIVS_GL_STALL_TOKEN >> 2, 1, 0);
      etna_cmd_stream_emit(stream, VIVS_GL_STALL_TOKEN_FROM(from) | VIVS_GL_STALL_TOKEN_TO(to));
   }
}

static void
etna_coalesce_start(struct etna_cmd_stream *stream,
                    struct etna_coalesce *coalesce)
{
   coalesce->start = etna_cmd_stream_offset(stream);
   coalesce->last_reg = 0;
   coalesce->last_fixp = 0;
}

static void
etna_coalesce_end(struct etna_cmd_stream *stream,
                  struct etna_coalesce *coalesce)
{
   uint32_t end = etna_cmd_stream_offset(stream);
   uint32_t size = end - coalesce->start;

   if (size) {
      uint32_t offset = coalesce->start - 1;
      uint32_t value = etna_cmd_stream_get(stream, offset);

      value |= VIV_FE_LOAD_STATE_HEADER_COUNT(size);
      etna_cmd_stream_set(stream, offset, value);
   }

   /* append needed padding */
   if (end % 2 == 1)
      etna_cmd_stream_emit(stream, 0xdeadbeef);
}

static void
check_coalsence(struct etna_cmd_stream *stream, struct etna_coalesce *coalesce,
                uint32_t reg, uint32_t fixp)
{
   if (coalesce->last_reg != 0) {
      if (((coalesce->last_reg + 4) != reg) || (coalesce->last_fixp != fixp)) {
         etna_coalesce_end(stream, coalesce);
         etna_emit_load_state(stream, reg >> 2, 0, fixp);
         coalesce->start = etna_cmd_stream_offset(stream);
      }
   } else {
      etna_emit_load_state(stream, reg >> 2, 0, fixp);
      coalesce->start = etna_cmd_stream_offset(stream);
   }

   coalesce->last_reg = reg;
   coalesce->last_fixp = fixp;
}

static inline void
etna_coalsence_emit(struct etna_cmd_stream *stream,
                    struct etna_coalesce *coalesce, uint32_t reg,
                    uint32_t value)
{
   check_coalsence(stream, coalesce, reg, 0);
   etna_cmd_stream_emit(stream, value);
}

static inline void
etna_coalsence_emit_fixp(struct etna_cmd_stream *stream,
                         struct etna_coalesce *coalesce, uint32_t reg,
                         uint32_t value)
{
   check_coalsence(stream, coalesce, reg, 1);
   etna_cmd_stream_emit(stream, value);
}

static inline void
etna_coalsence_emit_reloc(struct etna_cmd_stream *stream,
                          struct etna_coalesce *coalesce, uint32_t reg,
                          const struct etna_reloc *r)
{
   if (r->bo) {
      check_coalsence(stream, coalesce, reg, 0);
      etna_cmd_stream_reloc(stream, r);
   }
}

#define EMIT_STATE(state_name, src_value) \
   etna_coalsence_emit(stream, &coalesce, VIVS_##state_name, src_value)

#define EMIT_STATE_FIXP(state_name, src_value) \
   etna_coalsence_emit_fixp(stream, &coalesce, VIVS_##state_name, src_value)

#define EMIT_STATE_RELOC(state_name, src_value) \
   etna_coalsence_emit_reloc(stream, &coalesce, VIVS_##state_name, src_value)

/* submit RS state, without any processing and no dependence on context
 * except TS if this is a source-to-destination blit. */
void
etna_submit_rs_state(struct etna_context *ctx,
                     const struct compiled_rs_state *cs)
{
   struct etna_screen *screen = etna_screen(ctx->base.screen);
   struct etna_cmd_stream *stream = ctx->stream;
   struct etna_coalesce coalesce;

   if (screen->specs.pixel_pipes == 1) {
      etna_cmd_stream_reserve(stream, 22);
      etna_coalesce_start(stream, &coalesce);
      /* 0/1 */ EMIT_STATE(RS_CONFIG, cs->RS_CONFIG);
      /* 2   */ EMIT_STATE_RELOC(RS_SOURCE_ADDR, &cs->source[0]);
      /* 3   */ EMIT_STATE(RS_SOURCE_STRIDE, cs->RS_SOURCE_STRIDE);
      /* 4   */ EMIT_STATE_RELOC(RS_DEST_ADDR, &cs->dest[0]);
      /* 5   */ EMIT_STATE(RS_DEST_STRIDE, cs->RS_DEST_STRIDE);
      /* 6/7 */ EMIT_STATE(RS_WINDOW_SIZE, cs->RS_WINDOW_SIZE);
      /* 8/9 */ EMIT_STATE(RS_DITHER(0), cs->RS_DITHER[0]);
      /*10   */ EMIT_STATE(RS_DITHER(1), cs->RS_DITHER[1]);
      /*11 - pad */
      /*12/13*/ EMIT_STATE(RS_CLEAR_CONTROL, cs->RS_CLEAR_CONTROL);
      /*14   */ EMIT_STATE(RS_FILL_VALUE(0), cs->RS_FILL_VALUE[0]);
      /*15   */ EMIT_STATE(RS_FILL_VALUE(1), cs->RS_FILL_VALUE[1]);
      /*16   */ EMIT_STATE(RS_FILL_VALUE(2), cs->RS_FILL_VALUE[2]);
      /*17   */ EMIT_STATE(RS_FILL_VALUE(3), cs->RS_FILL_VALUE[3]);
      /*18/19*/ EMIT_STATE(RS_EXTRA_CONFIG, cs->RS_EXTRA_CONFIG);
      /*20/21*/ EMIT_STATE(RS_KICKER, 0xbeebbeeb);
      etna_coalesce_end(stream, &coalesce);
   } else if (screen->specs.pixel_pipes == 2) {
      etna_cmd_stream_reserve(stream, 34); /* worst case - both pipes multi=1 */
      etna_coalesce_start(stream, &coalesce);
      /* 0/1 */ EMIT_STATE(RS_CONFIG, cs->RS_CONFIG);
      /* 2/3 */ EMIT_STATE(RS_SOURCE_STRIDE, cs->RS_SOURCE_STRIDE);
      /* 4/5 */ EMIT_STATE(RS_DEST_STRIDE, cs->RS_DEST_STRIDE);
      /* 6/7 */ EMIT_STATE_RELOC(RS_PIPE_SOURCE_ADDR(0), &cs->source[0]);
      if (cs->RS_SOURCE_STRIDE & VIVS_RS_SOURCE_STRIDE_MULTI) {
         /*8 */ EMIT_STATE_RELOC(RS_PIPE_SOURCE_ADDR(1), &cs->source[1]);
         /*9 - pad */
      }
      /*10/11*/ EMIT_STATE_RELOC(RS_PIPE_DEST_ADDR(0), &cs->dest[0]);
      if (cs->RS_DEST_STRIDE & VIVS_RS_DEST_STRIDE_MULTI) {
         /*12*/ EMIT_STATE_RELOC(RS_PIPE_DEST_ADDR(1), &cs->dest[1]);
         /*13 - pad */
      }
      /*14/15*/ EMIT_STATE(RS_PIPE_OFFSET(0), cs->RS_PIPE_OFFSET[0]);
      /*16   */ EMIT_STATE(RS_PIPE_OFFSET(1), cs->RS_PIPE_OFFSET[1]);
      /*17 - pad */
      /*18/19*/ EMIT_STATE(RS_WINDOW_SIZE, cs->RS_WINDOW_SIZE);
      /*20/21*/ EMIT_STATE(RS_DITHER(0), cs->RS_DITHER[0]);
      /*22   */ EMIT_STATE(RS_DITHER(1), cs->RS_DITHER[1]);
      /*23 - pad */
      /*24/25*/ EMIT_STATE(RS_CLEAR_CONTROL, cs->RS_CLEAR_CONTROL);
      /*26   */ EMIT_STATE(RS_FILL_VALUE(0), cs->RS_FILL_VALUE[0]);
      /*27   */ EMIT_STATE(RS_FILL_VALUE(1), cs->RS_FILL_VALUE[1]);
      /*28   */ EMIT_STATE(RS_FILL_VALUE(2), cs->RS_FILL_VALUE[2]);
      /*29   */ EMIT_STATE(RS_FILL_VALUE(3), cs->RS_FILL_VALUE[3]);
      /*30/31*/ EMIT_STATE(RS_EXTRA_CONFIG, cs->RS_EXTRA_CONFIG);
      /*32/33*/ EMIT_STATE(RS_KICKER, 0xbeebbeeb);
      etna_coalesce_end(stream, &coalesce);
   } else {
      abort();
   }
}

/* Create bit field that specifies which samplers are active and thus need to be
 * programmed
 * 32 bits is enough for 32 samplers. As far as I know this is the upper bound
 * supported on any Vivante hw
 * up to GC4000.
 */
static uint32_t
active_samplers_bits(struct etna_context *ctx)
{
   return ctx->active_sampler_views & ctx->active_samplers;
}

#define ETNA_3D_CONTEXT_SIZE  (400) /* keep this number above "Total state updates (fixed)" from gen_weave_state tool */

static unsigned
required_stream_size(struct etna_context *ctx)
{
   unsigned size = ETNA_3D_CONTEXT_SIZE;

   /* stall + flush */
   size += 2 + 4;

   /* vertex elements */
   size += ctx->vertex_elements->num_elements + 1;

   /* uniforms - worst case (2 words per uniform load) */
   size += ctx->vs->uniforms.const_count * 2;
   size += ctx->fs->uniforms.const_count * 2;

   /* shader */
   size += ctx->shader_state.vs_inst_mem_size + 1;
   size += ctx->shader_state.ps_inst_mem_size + 1;

   /* DRAW_INDEXED_PRIMITIVES command */
   size += 6;

   /* reserve for alignment etc. */
   size += 64;

   return size;
}

/* Weave state before draw operation. This function merges all the compiled
 * state blocks under the context into one device register state. Parts of
 * this state that are changed since last call (dirty) will be uploaded as
 * state changes in the command buffer. */
void
etna_emit_state(struct etna_context *ctx)
{
   struct etna_cmd_stream *stream = ctx->stream;
   uint32_t active_samplers = active_samplers_bits(ctx);

   /* Pre-reserve the command buffer space which we are likely to need.
    * This must cover all the state emitted below, and the following
    * draw command. */
   etna_cmd_stream_reserve(stream, required_stream_size(ctx));

   uint32_t dirty = ctx->dirty;

   /* Pre-processing: see what caches we need to flush before making state changes. */
   uint32_t to_flush = 0;
   if (unlikely(dirty & (ETNA_DIRTY_BLEND))) {
      /* Need flush COLOR when changing PE.COLOR_FORMAT.OVERWRITE. */
#if 0
        /* TODO*/
        if ((ctx->gpu3d.PE_COLOR_FORMAT & VIVS_PE_COLOR_FORMAT_OVERWRITE) !=
           (etna_blend_state(ctx->blend)->PE_COLOR_FORMAT & VIVS_PE_COLOR_FORMAT_OVERWRITE))
#endif
      to_flush |= VIVS_GL_FLUSH_CACHE_COLOR;
   }
   if (unlikely(dirty & (ETNA_DIRTY_TEXTURE_CACHES)))
      to_flush |= VIVS_GL_FLUSH_CACHE_TEXTURE;
   if (unlikely(dirty & (ETNA_DIRTY_FRAMEBUFFER))) /* Framebuffer config changed? */
      to_flush |= VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH;
   if (DBG_ENABLED(ETNA_DBG_CFLUSH_ALL))
      to_flush |= VIVS_GL_FLUSH_CACHE_TEXTURE | VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH;

   if (to_flush) {
      etna_set_state(stream, VIVS_GL_FLUSH_CACHE, to_flush);
      etna_stall(stream, SYNC_RECIPIENT_RA, SYNC_RECIPIENT_PE);
   }

   /* If MULTI_SAMPLE_CONFIG.MSAA_SAMPLES changed, clobber affected shader
    * state to make sure it is always rewritten. */
   if (unlikely(dirty & (ETNA_DIRTY_FRAMEBUFFER))) {
      if ((ctx->gpu3d.GL_MULTI_SAMPLE_CONFIG & VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_SAMPLES__MASK) !=
          (ctx->framebuffer.GL_MULTI_SAMPLE_CONFIG & VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_SAMPLES__MASK)) {
         /* XXX what does the GPU set these states to on MSAA samples change?
          * Does it do the right thing?
          * (increase/decrease as necessary) or something else? Just set some
          * invalid value until we know for
          * sure. */
         ctx->gpu3d.PS_INPUT_COUNT = 0xffffffff;
         ctx->gpu3d.PS_TEMP_REGISTER_CONTROL = 0xffffffff;
      }
   }

   /* Update vertex elements. This is different from any of the other states, in that
    * a) the number of vertex elements written matters: so write only active ones
    * b) the vertex element states must all be written: do not skip entries that stay the same */
   if (dirty & (ETNA_DIRTY_VERTEX_ELEMENTS)) {
      /* Special case: vertex elements must always be sent in full if changed */
      /*00600*/ etna_set_state_multi(stream, VIVS_FE_VERTEX_ELEMENT_CONFIG(0),
         ctx->vertex_elements->num_elements,
         ctx->vertex_elements->FE_VERTEX_ELEMENT_CONFIG);
   }

   /* The following code is originally generated by gen_merge_state.py, to
    * emit state in increasing order of address (this makes it possible to merge
    * consecutive register updates into one SET_STATE command)
    *
    * There have been some manual changes, where the weaving operation is not
    * simply bitwise or:
    * - scissor fixp
    * - num vertex elements
    * - scissor handling
    * - num samplers
    * - texture lod
    * - ETNA_DIRTY_TS
    * - removed ETNA_DIRTY_BASE_SETUP statements -- these are guaranteed to not
    * change anyway
    * - PS / framebuffer interaction for MSAA
    * - move update of GL_MULTI_SAMPLE_CONFIG first
    * - add unlikely()/likely()
    */
   struct etna_coalesce coalesce;

   etna_coalesce_start(stream, &coalesce);

   /* begin only EMIT_STATE -- make sure no new etna_reserve calls are done here
    * directly
    *    or indirectly */
   /* multi sample config is set first, and outside of the normal sorting
    * order, as changing the multisample state clobbers PS.INPUT_COUNT (and
    * possibly PS.TEMP_REGISTER_CONTROL).
    */
   if (unlikely(dirty & (ETNA_DIRTY_FRAMEBUFFER | ETNA_DIRTY_SAMPLE_MASK))) {
      uint32_t val = VIVS_GL_MULTI_SAMPLE_CONFIG_MSAA_ENABLES(ctx->sample_mask);
      val |= ctx->framebuffer.GL_MULTI_SAMPLE_CONFIG;

      /*03818*/ EMIT_STATE(GL_MULTI_SAMPLE_CONFIG, val);
   }
   if (likely(dirty & (ETNA_DIRTY_INDEX_BUFFER)) &&
       ctx->index_buffer.ib.buffer) {
      /*00644*/ EMIT_STATE_RELOC(FE_INDEX_STREAM_BASE_ADDR, &ctx->index_buffer.FE_INDEX_STREAM_BASE_ADDR);
      /*00648*/ EMIT_STATE(FE_INDEX_STREAM_CONTROL, ctx->index_buffer.FE_INDEX_STREAM_CONTROL);
   }
   if (likely(dirty & (ETNA_DIRTY_VERTEX_BUFFERS))) {
      /*0064C*/ EMIT_STATE_RELOC(FE_VERTEX_STREAM_BASE_ADDR, &ctx->vertex_buffer.cvb[0].FE_VERTEX_STREAM_BASE_ADDR);
      /*00650*/ EMIT_STATE(FE_VERTEX_STREAM_CONTROL, ctx->vertex_buffer.cvb[0].FE_VERTEX_STREAM_CONTROL);
   }
   if (likely(dirty & (ETNA_DIRTY_INDEX_BUFFER))) {
      /*00674*/ EMIT_STATE(FE_PRIMITIVE_RESTART_INDEX, ctx->index_buffer.FE_PRIMITIVE_RESTART_INDEX);
   }
   if (likely(dirty & (ETNA_DIRTY_VERTEX_BUFFERS))) {
      for (int x = 1; x < ctx->vertex_buffer.count; ++x) {
         /*00680*/ EMIT_STATE_RELOC(FE_VERTEX_STREAMS_BASE_ADDR(x), &ctx->vertex_buffer.cvb[x].FE_VERTEX_STREAM_BASE_ADDR);
      }
      for (int x = 1; x < ctx->vertex_buffer.count; ++x) {
         if (ctx->vertex_buffer.cvb[x].FE_VERTEX_STREAM_BASE_ADDR.bo) {
            /*006A0*/ EMIT_STATE(FE_VERTEX_STREAMS_CONTROL(x), ctx->vertex_buffer.cvb[x].FE_VERTEX_STREAM_CONTROL);
         }
      }
   }
   if (unlikely(dirty & (ETNA_DIRTY_SHADER))) {
      /*00800*/ EMIT_STATE(VS_END_PC, ctx->shader_state.VS_END_PC);
   }
   if (unlikely(dirty & (ETNA_DIRTY_SHADER | ETNA_DIRTY_RASTERIZER))) {
      bool point_size_per_vertex =
         etna_rasterizer_state(ctx->rasterizer)->point_size_per_vertex;

      /*00804*/ EMIT_STATE(VS_OUTPUT_COUNT,
                           point_size_per_vertex
                              ? ctx->shader_state.VS_OUTPUT_COUNT_PSIZE
                              : ctx->shader_state.VS_OUTPUT_COUNT);
   }
   if (unlikely(dirty & (ETNA_DIRTY_VERTEX_ELEMENTS | ETNA_DIRTY_SHADER))) {
      /*00808*/ EMIT_STATE(VS_INPUT_COUNT, ctx->shader_state.VS_INPUT_COUNT);
      /*0080C*/ EMIT_STATE(VS_TEMP_REGISTER_CONTROL, ctx->shader_state.VS_TEMP_REGISTER_CONTROL);
   }
   if (unlikely(dirty & (ETNA_DIRTY_SHADER))) {
      for (int x = 0; x < 4; ++x) {
         /*00810*/ EMIT_STATE(VS_OUTPUT(x), ctx->shader_state.VS_OUTPUT[x]);
      }
   }
   if (unlikely(dirty & (ETNA_DIRTY_VERTEX_ELEMENTS | ETNA_DIRTY_SHADER))) {
      for (int x = 0; x < 4; ++x) {
         /*00820*/ EMIT_STATE(VS_INPUT(x), ctx->shader_state.VS_INPUT[x]);
      }
   }
   if (unlikely(dirty & (ETNA_DIRTY_SHADER))) {
      /*00830*/ EMIT_STATE(VS_LOAD_BALANCING, ctx->shader_state.VS_LOAD_BALANCING);
      /*00838*/ EMIT_STATE(VS_START_PC, ctx->shader_state.VS_START_PC);
      if (ctx->specs.has_shader_range_registers) {
         /*0085C*/ EMIT_STATE(VS_RANGE, (ctx->shader_state.vs_inst_mem_size / 4 - 1) << 16);
      }
   }
   if (unlikely(dirty & (ETNA_DIRTY_VIEWPORT))) {
      /*00A00*/ EMIT_STATE_FIXP(PA_VIEWPORT_SCALE_X, ctx->viewport.PA_VIEWPORT_SCALE_X);
      /*00A04*/ EMIT_STATE_FIXP(PA_VIEWPORT_SCALE_Y, ctx->viewport.PA_VIEWPORT_SCALE_Y);
      /*00A08*/ EMIT_STATE(PA_VIEWPORT_SCALE_Z, ctx->viewport.PA_VIEWPORT_SCALE_Z);
      /*00A0C*/ EMIT_STATE_FIXP(PA_VIEWPORT_OFFSET_X, ctx->viewport.PA_VIEWPORT_OFFSET_X);
      /*00A10*/ EMIT_STATE_FIXP(PA_VIEWPORT_OFFSET_Y, ctx->viewport.PA_VIEWPORT_OFFSET_Y);
      /*00A14*/ EMIT_STATE(PA_VIEWPORT_OFFSET_Z, ctx->viewport.PA_VIEWPORT_OFFSET_Z);
   }
   if (unlikely(dirty & (ETNA_DIRTY_RASTERIZER))) {
      struct etna_rasterizer_state *rasterizer = etna_rasterizer_state(ctx->rasterizer);

      /*00A18*/ EMIT_STATE(PA_LINE_WIDTH, rasterizer->PA_LINE_WIDTH);
      /*00A1C*/ EMIT_STATE(PA_POINT_SIZE, rasterizer->PA_POINT_SIZE);
      /*00A28*/ EMIT_STATE(PA_SYSTEM_MODE, rasterizer->PA_SYSTEM_MODE);
   }
   if (unlikely(dirty & (ETNA_DIRTY_SHADER))) {
      /*00A30*/ EMIT_STATE(PA_ATTRIBUTE_ELEMENT_COUNT, ctx->shader_state.PA_ATTRIBUTE_ELEMENT_COUNT);
   }
   if (unlikely(dirty & (ETNA_DIRTY_RASTERIZER | ETNA_DIRTY_SHADER))) {
      uint32_t val = etna_rasterizer_state(ctx->rasterizer)->PA_CONFIG;
      /*00A34*/ EMIT_STATE(PA_CONFIG, val & ctx->shader_state.PA_CONFIG);
   }
   if (unlikely(dirty & (ETNA_DIRTY_RASTERIZER))) {
      struct etna_rasterizer_state *rasterizer = etna_rasterizer_state(ctx->rasterizer);
      /*00A38*/ EMIT_STATE(PA_WIDE_LINE_WIDTH0, rasterizer->PA_LINE_WIDTH);
      /*00A3C*/ EMIT_STATE(PA_WIDE_LINE_WIDTH1, rasterizer->PA_LINE_WIDTH);
   }
   if (unlikely(dirty & (ETNA_DIRTY_SHADER))) {
      for (int x = 0; x < 10; ++x) {
         /*00A40*/ EMIT_STATE(PA_SHADER_ATTRIBUTES(x), ctx->shader_state.PA_SHADER_ATTRIBUTES[x]);
      }
   }
   if (unlikely(dirty & (ETNA_DIRTY_SCISSOR | ETNA_DIRTY_FRAMEBUFFER |
                         ETNA_DIRTY_RASTERIZER | ETNA_DIRTY_VIEWPORT))) {
      /* this is a bit of a mess: rasterizer.scissor determines whether to use
       * only the framebuffer scissor, or specific scissor state, and the
       * viewport clips too so the logic spans four CSOs */
      struct etna_rasterizer_state *rasterizer = etna_rasterizer_state(ctx->rasterizer);

      uint32_t scissor_left =
         MAX2(ctx->framebuffer.SE_SCISSOR_LEFT, ctx->viewport.SE_SCISSOR_LEFT);
      uint32_t scissor_top =
         MAX2(ctx->framebuffer.SE_SCISSOR_TOP, ctx->viewport.SE_SCISSOR_TOP);
      uint32_t scissor_right =
         MIN2(ctx->framebuffer.SE_SCISSOR_RIGHT, ctx->viewport.SE_SCISSOR_RIGHT);
      uint32_t scissor_bottom =
         MIN2(ctx->framebuffer.SE_SCISSOR_BOTTOM, ctx->viewport.SE_SCISSOR_BOTTOM);

      if (rasterizer->scissor) {
         scissor_left = MAX2(ctx->scissor.SE_SCISSOR_LEFT, scissor_left);
         scissor_top = MAX2(ctx->scissor.SE_SCISSOR_TOP, scissor_top);
         scissor_right = MIN2(ctx->scissor.SE_SCISSOR_RIGHT, scissor_right);
         scissor_bottom = MIN2(ctx->scissor.SE_SCISSOR_BOTTOM, scissor_bottom);
      }

      /*00C00*/ EMIT_STATE_FIXP(SE_SCISSOR_LEFT, scissor_left);
      /*00C04*/ EMIT_STATE_FIXP(SE_SCISSOR_TOP, scissor_top);
      /*00C08*/ EMIT_STATE_FIXP(SE_SCISSOR_RIGHT, scissor_right);
      /*00C0C*/ EMIT_STATE_FIXP(SE_SCISSOR_BOTTOM, scissor_bottom);
   }
   if (unlikely(dirty & (ETNA_DIRTY_RASTERIZER))) {
      struct etna_rasterizer_state *rasterizer = etna_rasterizer_state(ctx->rasterizer);

      /*00C10*/ EMIT_STATE(SE_DEPTH_SCALE, rasterizer->SE_DEPTH_SCALE);
      /*00C14*/ EMIT_STATE(SE_DEPTH_BIAS, rasterizer->SE_DEPTH_BIAS);
      /*00C18*/ EMIT_STATE(SE_CONFIG, rasterizer->SE_CONFIG);
   }
   if (unlikely(dirty & (ETNA_DIRTY_SCISSOR | ETNA_DIRTY_FRAMEBUFFER |
                         ETNA_DIRTY_RASTERIZER | ETNA_DIRTY_VIEWPORT))) {
      struct etna_rasterizer_state *rasterizer = etna_rasterizer_state(ctx->rasterizer);

      uint32_t clip_right =
         MIN2(ctx->framebuffer.SE_CLIP_RIGHT, ctx->viewport.SE_CLIP_RIGHT);
      uint32_t clip_bottom =
         MIN2(ctx->framebuffer.SE_CLIP_BOTTOM, ctx->viewport.SE_CLIP_BOTTOM);

      if (rasterizer->scissor) {
         clip_right = MIN2(ctx->scissor.SE_CLIP_RIGHT, clip_right);
         clip_bottom = MIN2(ctx->scissor.SE_CLIP_BOTTOM, clip_bottom);
      }

      /*00C20*/ EMIT_STATE_FIXP(SE_CLIP_RIGHT, clip_right);
      /*00C24*/ EMIT_STATE_FIXP(SE_CLIP_BOTTOM, clip_bottom);
   }
   if (unlikely(dirty & (ETNA_DIRTY_SHADER))) {
      /*00E00*/ EMIT_STATE(RA_CONTROL, ctx->shader_state.RA_CONTROL);
   }
   if (unlikely(dirty & (ETNA_DIRTY_FRAMEBUFFER))) {
      /*00E04*/ EMIT_STATE(RA_MULTISAMPLE_UNK00E04, ctx->framebuffer.RA_MULTISAMPLE_UNK00E04);
      for (int x = 0; x < 4; ++x) {
         /*00E10*/ EMIT_STATE(RA_MULTISAMPLE_UNK00E10(x), ctx->framebuffer.RA_MULTISAMPLE_UNK00E10[x]);
      }
      for (int x = 0; x < 16; ++x) {
         /*00E40*/ EMIT_STATE(RA_CENTROID_TABLE(x), ctx->framebuffer.RA_CENTROID_TABLE[x]);
      }
   }
   if (unlikely(dirty & (ETNA_DIRTY_SHADER | ETNA_DIRTY_FRAMEBUFFER))) {
      /*01000*/ EMIT_STATE(PS_END_PC, ctx->shader_state.PS_END_PC);
      /*01004*/ EMIT_STATE(PS_OUTPUT_REG, ctx->shader_state.PS_OUTPUT_REG);
      /*01008*/ EMIT_STATE(PS_INPUT_COUNT,
                           ctx->framebuffer.msaa_mode
                              ? ctx->shader_state.PS_INPUT_COUNT_MSAA
                              : ctx->shader_state.PS_INPUT_COUNT);
      /*0100C*/ EMIT_STATE(PS_TEMP_REGISTER_CONTROL,
                           ctx->framebuffer.msaa_mode
                              ? ctx->shader_state.PS_TEMP_REGISTER_CONTROL_MSAA
                              : ctx->shader_state.PS_TEMP_REGISTER_CONTROL);
      /*01010*/ EMIT_STATE(PS_CONTROL, ctx->shader_state.PS_CONTROL);
      /*01018*/ EMIT_STATE(PS_START_PC, ctx->shader_state.PS_START_PC);
      if (ctx->specs.has_shader_range_registers) {
         /*0101C*/ EMIT_STATE(PS_RANGE, ((ctx->shader_state.ps_inst_mem_size / 4 - 1 + 0x100) << 16) |
                                        0x100);
      }
   }
   if (unlikely(dirty & (ETNA_DIRTY_ZSA | ETNA_DIRTY_FRAMEBUFFER))) {
      uint32_t val = etna_zsa_state(ctx->zsa)->PE_DEPTH_CONFIG;
      /*01400*/ EMIT_STATE(PE_DEPTH_CONFIG, val | ctx->framebuffer.PE_DEPTH_CONFIG);
   }
   if (unlikely(dirty & (ETNA_DIRTY_VIEWPORT))) {
      /*01404*/ EMIT_STATE(PE_DEPTH_NEAR, ctx->viewport.PE_DEPTH_NEAR);
      /*01408*/ EMIT_STATE(PE_DEPTH_FAR, ctx->viewport.PE_DEPTH_FAR);
   }
   if (unlikely(dirty & (ETNA_DIRTY_FRAMEBUFFER))) {
      /*0140C*/ EMIT_STATE(PE_DEPTH_NORMALIZE, ctx->framebuffer.PE_DEPTH_NORMALIZE);

      if (ctx->specs.pixel_pipes == 1) {
         /*01410*/ EMIT_STATE_RELOC(PE_DEPTH_ADDR, &ctx->framebuffer.PE_DEPTH_ADDR);
      }

      /*01414*/ EMIT_STATE(PE_DEPTH_STRIDE, ctx->framebuffer.PE_DEPTH_STRIDE);
   }
   if (unlikely(dirty & (ETNA_DIRTY_ZSA))) {
      uint32_t val = etna_zsa_state(ctx->zsa)->PE_STENCIL_OP;
      /*01418*/ EMIT_STATE(PE_STENCIL_OP, val);
   }
   if (unlikely(dirty & (ETNA_DIRTY_ZSA | ETNA_DIRTY_STENCIL_REF))) {
      uint32_t val = etna_zsa_state(ctx->zsa)->PE_STENCIL_CONFIG;
      /*0141C*/ EMIT_STATE(PE_STENCIL_CONFIG, val | ctx->stencil_ref.PE_STENCIL_CONFIG);
   }
   if (unlikely(dirty & (ETNA_DIRTY_ZSA))) {
      uint32_t val = etna_zsa_state(ctx->zsa)->PE_ALPHA_OP;
      /*01420*/ EMIT_STATE(PE_ALPHA_OP, val);
   }
   if (unlikely(dirty & (ETNA_DIRTY_BLEND_COLOR))) {
      /*01424*/ EMIT_STATE(PE_ALPHA_BLEND_COLOR, ctx->blend_color.PE_ALPHA_BLEND_COLOR);
   }
   if (unlikely(dirty & (ETNA_DIRTY_BLEND))) {
      uint32_t val = etna_blend_state(ctx->blend)->PE_ALPHA_CONFIG;
      /*01428*/ EMIT_STATE(PE_ALPHA_CONFIG, val);
   }
   if (unlikely(dirty & (ETNA_DIRTY_BLEND | ETNA_DIRTY_FRAMEBUFFER))) {
      uint32_t val;
      /* Use the components and overwrite bits in framebuffer.PE_COLOR_FORMAT
       * as a mask to enable the bits from blend PE_COLOR_FORMAT */
      val = ~(VIVS_PE_COLOR_FORMAT_COMPONENTS__MASK |
              VIVS_PE_COLOR_FORMAT_OVERWRITE);
      val |= etna_blend_state(ctx->blend)->PE_COLOR_FORMAT;
      val &= ctx->framebuffer.PE_COLOR_FORMAT;
      /*0142C*/ EMIT_STATE(PE_COLOR_FORMAT, val);
   }
   if (unlikely(dirty & (ETNA_DIRTY_FRAMEBUFFER))) {
      if (ctx->specs.pixel_pipes == 1) {
         /*01430*/ EMIT_STATE_RELOC(PE_COLOR_ADDR, &ctx->framebuffer.PE_COLOR_ADDR);
         /*01434*/ EMIT_STATE(PE_COLOR_STRIDE, ctx->framebuffer.PE_COLOR_STRIDE);
         /*01454*/ EMIT_STATE(PE_HDEPTH_CONTROL, ctx->framebuffer.PE_HDEPTH_CONTROL);
      } else if (ctx->specs.pixel_pipes == 2) {
         /*01434*/ EMIT_STATE(PE_COLOR_STRIDE, ctx->framebuffer.PE_COLOR_STRIDE);
         /*01454*/ EMIT_STATE(PE_HDEPTH_CONTROL, ctx->framebuffer.PE_HDEPTH_CONTROL);
         /*01460*/ EMIT_STATE_RELOC(PE_PIPE_COLOR_ADDR(0), &ctx->framebuffer.PE_PIPE_COLOR_ADDR[0]);
         /*01464*/ EMIT_STATE_RELOC(PE_PIPE_COLOR_ADDR(1), &ctx->framebuffer.PE_PIPE_COLOR_ADDR[1]);
         /*01480*/ EMIT_STATE_RELOC(PE_PIPE_DEPTH_ADDR(0), &ctx->framebuffer.PE_PIPE_DEPTH_ADDR[0]);
         /*01484*/ EMIT_STATE_RELOC(PE_PIPE_DEPTH_ADDR(1), &ctx->framebuffer.PE_PIPE_DEPTH_ADDR[1]);
      } else {
         abort();
      }
   }
   if (unlikely(dirty & (ETNA_DIRTY_STENCIL_REF))) {
      /*014A0*/ EMIT_STATE(PE_STENCIL_CONFIG_EXT, ctx->stencil_ref.PE_STENCIL_CONFIG_EXT);
   }
   if (unlikely(dirty & (ETNA_DIRTY_BLEND))) {
      struct etna_blend_state *blend = etna_blend_state(ctx->blend);

      /*014A4*/ EMIT_STATE(PE_LOGIC_OP, blend->PE_LOGIC_OP);
      for (int x = 0; x < 2; ++x) {
         /*014A8*/ EMIT_STATE(PE_DITHER(x), blend->PE_DITHER[x]);
      }
   }
   if (unlikely(dirty & (ETNA_DIRTY_FRAMEBUFFER | ETNA_DIRTY_TS))) {
      /*01654*/ EMIT_STATE(TS_MEM_CONFIG, ctx->framebuffer.TS_MEM_CONFIG);
      /*01658*/ EMIT_STATE_RELOC(TS_COLOR_STATUS_BASE, &ctx->framebuffer.TS_COLOR_STATUS_BASE);
      /*0165C*/ EMIT_STATE_RELOC(TS_COLOR_SURFACE_BASE, &ctx->framebuffer.TS_COLOR_SURFACE_BASE);
      /*01660*/ EMIT_STATE(TS_COLOR_CLEAR_VALUE, ctx->framebuffer.TS_COLOR_CLEAR_VALUE);
      /*01664*/ EMIT_STATE_RELOC(TS_DEPTH_STATUS_BASE, &ctx->framebuffer.TS_DEPTH_STATUS_BASE);
      /*01668*/ EMIT_STATE_RELOC(TS_DEPTH_SURFACE_BASE, &ctx->framebuffer.TS_DEPTH_SURFACE_BASE);
      /*0166C*/ EMIT_STATE(TS_DEPTH_CLEAR_VALUE, ctx->framebuffer.TS_DEPTH_CLEAR_VALUE);
   }
   if (unlikely(dirty & (ETNA_DIRTY_SAMPLER_VIEWS | ETNA_DIRTY_SAMPLERS))) {
      for (int x = 0; x < VIVS_TE_SAMPLER__LEN; ++x) {
         uint32_t val = 0; /* 0 == sampler inactive */

         /* set active samplers to their configuration value (determined by both
          * the sampler state and sampler view) */
         if ((1 << x) & active_samplers) {
            struct etna_sampler_state *ss = etna_sampler_state(ctx->sampler[x]);
            struct etna_sampler_view *sv = etna_sampler_view(ctx->sampler_view[x]);

            val = (ss->TE_SAMPLER_CONFIG0 & sv->TE_SAMPLER_CONFIG0_MASK) |
                  sv->TE_SAMPLER_CONFIG0;
         }

         /*02000*/ EMIT_STATE(TE_SAMPLER_CONFIG0(x), val);
      }
   }
   if (unlikely(dirty & (ETNA_DIRTY_SAMPLER_VIEWS))) {
      struct etna_sampler_view *sv;

      for (int x = 0; x < VIVS_TE_SAMPLER__LEN; ++x) {
         if ((1 << x) & active_samplers) {
            sv = etna_sampler_view(ctx->sampler_view[x]);
            /*02040*/ EMIT_STATE(TE_SAMPLER_SIZE(x), sv->TE_SAMPLER_SIZE);
         }
      }
      for (int x = 0; x < VIVS_TE_SAMPLER__LEN; ++x) {
         if ((1 << x) & active_samplers) {
            sv = etna_sampler_view(ctx->sampler_view[x]);
            /*02080*/ EMIT_STATE(TE_SAMPLER_LOG_SIZE(x), sv->TE_SAMPLER_LOG_SIZE);
         }
      }
   }
   if (unlikely(dirty & (ETNA_DIRTY_SAMPLER_VIEWS | ETNA_DIRTY_SAMPLERS))) {
      struct etna_sampler_state *ss;
      struct etna_sampler_view *sv;

      for (int x = 0; x < VIVS_TE_SAMPLER__LEN; ++x) {
         if ((1 << x) & active_samplers) {
            ss = etna_sampler_state(ctx->sampler[x]);
            sv = etna_sampler_view(ctx->sampler_view[x]);

            /* min and max lod is determined both by the sampler and the view */
            /*020C0*/ EMIT_STATE(TE_SAMPLER_LOD_CONFIG(x),
                                 ss->TE_SAMPLER_LOD_CONFIG |
                                 VIVS_TE_SAMPLER_LOD_CONFIG_MAX(MIN2(ss->max_lod, sv->max_lod)) |
                                 VIVS_TE_SAMPLER_LOD_CONFIG_MIN(MAX2(ss->min_lod, sv->min_lod)));
         }
      }
      for (int x = 0; x < VIVS_TE_SAMPLER__LEN; ++x) {
         if ((1 << x) & active_samplers) {
            ss = etna_sampler_state(ctx->sampler[x]);
            sv = etna_sampler_view(ctx->sampler_view[x]);

            /*021C0*/ EMIT_STATE(TE_SAMPLER_CONFIG1(x), ss->TE_SAMPLER_CONFIG1 |
                                                        sv->TE_SAMPLER_CONFIG1);
         }
      }
   }
   if (unlikely(dirty & (ETNA_DIRTY_SAMPLER_VIEWS))) {
      for (int y = 0; y < VIVS_TE_SAMPLER_LOD_ADDR__LEN; ++y) {
         for (int x = 0; x < VIVS_TE_SAMPLER__LEN; ++x) {
            if ((1 << x) & active_samplers) {
               struct etna_sampler_view *sv = etna_sampler_view(ctx->sampler_view[x]);
               /*02400*/ EMIT_STATE_RELOC(TE_SAMPLER_LOD_ADDR(x, y),&sv->TE_SAMPLER_LOD_ADDR[y]);
            }
         }
      }
   }
   if (unlikely(dirty & (ETNA_DIRTY_SHADER))) {
      /*0381C*/ EMIT_STATE(GL_VARYING_TOTAL_COMPONENTS, ctx->shader_state.GL_VARYING_TOTAL_COMPONENTS);
      /*03820*/ EMIT_STATE(GL_VARYING_NUM_COMPONENTS, ctx->shader_state.GL_VARYING_NUM_COMPONENTS);
      for (int x = 0; x < 2; ++x) {
         /*03828*/ EMIT_STATE(GL_VARYING_COMPONENT_USE(x), ctx->shader_state.GL_VARYING_COMPONENT_USE[x]);
      }
   }
   etna_coalesce_end(stream, &coalesce);
   /* end only EMIT_STATE */

   /* Insert a FE/PE stall as changing the shader instructions (and maybe
    * the uniforms) can corrupt the previous in-progress draw operation.
    * Observed with amoeba on GC2000 during the right-to-left rendering
    * of PI, and can cause GPU hangs immediately after.
    * I summise that this is because the "new" locations at 0xc000 are not
    * properly protected against updates as other states seem to be. Hence,
    * we detect the "new" vertex shader instruction offset to apply this. */
   if (ctx->dirty & (ETNA_DIRTY_SHADER | ETNA_DIRTY_CONSTBUF) && ctx->specs.vs_offset > 0x4000)
      etna_stall(ctx->stream, SYNC_RECIPIENT_FE, SYNC_RECIPIENT_PE);

   /* We need to update the uniform cache only if one of the following bits are
    * set in ctx->dirty:
    * - ETNA_DIRTY_SHADER
    * - ETNA_DIRTY_CONSTBUF
    * - uniforms_dirty_bits
    *
    * In case of ETNA_DIRTY_SHADER we need load all uniforms from the cache. In
    * all
    * other cases we can load on the changed uniforms.
    */
   static const uint32_t uniform_dirty_bits =
      ETNA_DIRTY_SHADER | ETNA_DIRTY_CONSTBUF;

   if (dirty & (uniform_dirty_bits | ctx->fs->uniforms_dirty_bits))
      etna_uniforms_write(
         ctx, ctx->vs, &ctx->constant_buffer[PIPE_SHADER_VERTEX],
         ctx->shader_state.VS_UNIFORMS, &ctx->shader_state.vs_uniforms_size);

   if (dirty & (uniform_dirty_bits | ctx->vs->uniforms_dirty_bits))
      etna_uniforms_write(
         ctx, ctx->fs, &ctx->constant_buffer[PIPE_SHADER_FRAGMENT],
         ctx->shader_state.PS_UNIFORMS, &ctx->shader_state.ps_uniforms_size);

   /**** Large dynamically-sized state ****/
   if (dirty & (ETNA_DIRTY_SHADER)) {
      /* Special case: a new shader was loaded; simply re-load all uniforms and
       * shader code at once */
      /*04000 or 0C000*/
      etna_set_state_multi(stream, ctx->specs.vs_offset,
                           ctx->shader_state.vs_inst_mem_size,
                           ctx->shader_state.VS_INST_MEM);
      /*06000 or 0D000*/
      etna_set_state_multi(stream, ctx->specs.ps_offset,
                           ctx->shader_state.ps_inst_mem_size,
                           ctx->shader_state.PS_INST_MEM);
      /*05000*/ etna_set_state_multi(stream, VIVS_VS_UNIFORMS(0),
                                     ctx->shader_state.vs_uniforms_size,
                                     ctx->shader_state.VS_UNIFORMS);
      /*07000*/ etna_set_state_multi(stream, VIVS_PS_UNIFORMS(0),
                                     ctx->shader_state.ps_uniforms_size,
                                     ctx->shader_state.PS_UNIFORMS);

      /* Copy uniforms to gpu3d, so that incremental updates to uniforms are
       * possible as long as the
       * same shader remains bound */
      ctx->gpu3d.vs_uniforms_size = ctx->shader_state.vs_uniforms_size;
      ctx->gpu3d.ps_uniforms_size = ctx->shader_state.ps_uniforms_size;
      memcpy(ctx->gpu3d.VS_UNIFORMS, ctx->shader_state.VS_UNIFORMS,
             ctx->shader_state.vs_uniforms_size * 4);
      memcpy(ctx->gpu3d.PS_UNIFORMS, ctx->shader_state.PS_UNIFORMS,
             ctx->shader_state.ps_uniforms_size * 4);
   } else {
      etna_coalesce_start(stream, &coalesce);
      for (int x = 0; x < ctx->vs->uniforms.const_count; ++x) {
         if (ctx->gpu3d.VS_UNIFORMS[x] != ctx->shader_state.VS_UNIFORMS[x]) {
            /*05000*/ EMIT_STATE(VS_UNIFORMS(x), ctx->shader_state.VS_UNIFORMS[x]);
            ctx->gpu3d.VS_UNIFORMS[x] = ctx->shader_state.VS_UNIFORMS[x];
         }
      }
      etna_coalesce_end(stream, &coalesce);

      etna_coalesce_start(stream, &coalesce);
      for (int x = 0; x < ctx->fs->uniforms.const_count; ++x) {
         if (ctx->gpu3d.PS_UNIFORMS[x] != ctx->shader_state.PS_UNIFORMS[x]) {
            /*07000*/ EMIT_STATE(PS_UNIFORMS(x), ctx->shader_state.PS_UNIFORMS[x]);
            ctx->gpu3d.PS_UNIFORMS[x] = ctx->shader_state.PS_UNIFORMS[x];
         }
      }
      etna_coalesce_end(stream, &coalesce);
   }
/**** End of state update ****/
#undef EMIT_STATE
#undef EMIT_STATE_FIXP
#undef EMIT_STATE_RELOC
   ctx->dirty = 0;
}
