/*
 * Copyright (C) 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
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
 *    Rob Clark <robclark@freedesktop.org>
 */

#define FD_BO_NO_HARDPIN 1

#include "pipe/p_state.h"
#include "util/format/u_format.h"
#include "util/u_helpers.h"
#include "util/u_memory.h"
#include "util/u_string.h"
#include "util/u_viewport.h"

#include "freedreno_query_hw.h"
#include "freedreno_resource.h"
#include "freedreno_state.h"
#include "freedreno_tracepoints.h"

#include "fd6_blend.h"
#include "fd6_const.h"
#include "fd6_context.h"
#include "fd6_emit.h"
#include "fd6_image.h"
#include "fd6_pack.h"
#include "fd6_program.h"
#include "fd6_rasterizer.h"
#include "fd6_texture.h"
#include "fd6_zsa.h"

static void
fd6_emit_fb_tex(struct fd_ringbuffer *state, struct fd_context *ctx) assert_dt
{
   /* This is patched with the appropriate descriptor for GMEM or
    * sysmem rendering path in fd6_gmem
    */
   OUT_RINGP(state, 0, &ctx->batch->fb_read_patches);
   for (unsigned i = 1; i < FDL6_TEX_CONST_DWORDS; i++)
      OUT_RING(state, 0);
}

void
fd6_emit_textures(struct fd_context *ctx, struct fd_ringbuffer *ring,
                  enum pipe_shader_type type, struct fd_texture_stateobj *tex,
                  /* can be NULL if no image/SSBO/fb state to merge in: */
                  const struct ir3_shader_variant *v)
{
   unsigned opcode, tex_samp_reg, tex_const_reg, tex_count_reg;
   enum a6xx_state_block sb;

   switch (type) {
   case PIPE_SHADER_VERTEX:
      sb = SB6_VS_TEX;
      opcode = CP_LOAD_STATE6_GEOM;
      tex_samp_reg = REG_A6XX_SP_VS_TEX_SAMP;
      tex_const_reg = REG_A6XX_SP_VS_TEX_CONST;
      tex_count_reg = REG_A6XX_SP_VS_TEX_COUNT;
      break;
   case PIPE_SHADER_TESS_CTRL:
      sb = SB6_HS_TEX;
      opcode = CP_LOAD_STATE6_GEOM;
      tex_samp_reg = REG_A6XX_SP_HS_TEX_SAMP;
      tex_const_reg = REG_A6XX_SP_HS_TEX_CONST;
      tex_count_reg = REG_A6XX_SP_HS_TEX_COUNT;
      break;
   case PIPE_SHADER_TESS_EVAL:
      sb = SB6_DS_TEX;
      opcode = CP_LOAD_STATE6_GEOM;
      tex_samp_reg = REG_A6XX_SP_DS_TEX_SAMP;
      tex_const_reg = REG_A6XX_SP_DS_TEX_CONST;
      tex_count_reg = REG_A6XX_SP_DS_TEX_COUNT;
      break;
   case PIPE_SHADER_GEOMETRY:
      sb = SB6_GS_TEX;
      opcode = CP_LOAD_STATE6_GEOM;
      tex_samp_reg = REG_A6XX_SP_GS_TEX_SAMP;
      tex_const_reg = REG_A6XX_SP_GS_TEX_CONST;
      tex_count_reg = REG_A6XX_SP_GS_TEX_COUNT;
      break;
   case PIPE_SHADER_FRAGMENT:
      sb = SB6_FS_TEX;
      opcode = CP_LOAD_STATE6_FRAG;
      tex_samp_reg = REG_A6XX_SP_FS_TEX_SAMP;
      tex_const_reg = REG_A6XX_SP_FS_TEX_CONST;
      tex_count_reg = REG_A6XX_SP_FS_TEX_COUNT;
      break;
   case PIPE_SHADER_COMPUTE:
      sb = SB6_CS_TEX;
      opcode = CP_LOAD_STATE6_FRAG;
      tex_samp_reg = REG_A6XX_SP_CS_TEX_SAMP;
      tex_const_reg = REG_A6XX_SP_CS_TEX_CONST;
      tex_count_reg = REG_A6XX_SP_CS_TEX_COUNT;
      break;
   default:
      unreachable("bad state block");
   }

   if (tex->num_samplers > 0) {
      struct fd_ringbuffer *state =
         fd_ringbuffer_new_object(ctx->pipe, tex->num_samplers * 4 * 4);
      for (unsigned i = 0; i < tex->num_samplers; i++) {
         static const struct fd6_sampler_stateobj dummy_sampler = {};
         const struct fd6_sampler_stateobj *sampler =
            tex->samplers[i] ? fd6_sampler_stateobj(tex->samplers[i])
                             : &dummy_sampler;
         OUT_RING(state, sampler->texsamp0);
         OUT_RING(state, sampler->texsamp1);
         OUT_RING(state, sampler->texsamp2);
         OUT_RING(state, sampler->texsamp3);
      }

      /* output sampler state: */
      OUT_PKT7(ring, opcode, 3);
      OUT_RING(ring, CP_LOAD_STATE6_0_DST_OFF(0) |
                        CP_LOAD_STATE6_0_STATE_TYPE(ST6_SHADER) |
                        CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
                        CP_LOAD_STATE6_0_STATE_BLOCK(sb) |
                        CP_LOAD_STATE6_0_NUM_UNIT(tex->num_samplers));
      OUT_RB(ring, state); /* SRC_ADDR_LO/HI */

      OUT_PKT4(ring, tex_samp_reg, 2);
      OUT_RB(ring, state); /* SRC_ADDR_LO/HI */

      fd_ringbuffer_del(state);
   }

   unsigned num_merged_textures = tex->num_textures;
   unsigned num_textures = tex->num_textures;
   if (v) {
      num_merged_textures += v->image_mapping.num_tex;

      if (v->fb_read)
         num_merged_textures++;

      /* There could be more bound textures than what the shader uses.
       * Which isn't known at shader compile time.  So in the case we
       * are merging tex state, only emit the textures that the shader
       * uses (since the image/SSBO related tex state comes immediately
       * after)
       */
      num_textures = v->image_mapping.tex_base;
   }

   if (num_merged_textures > 0) {
      struct fd_ringbuffer *state =
         fd_ringbuffer_new_object(ctx->pipe, num_merged_textures * 16 * 4);
      for (unsigned i = 0; i < num_textures; i++) {
         const struct fd6_pipe_sampler_view *view;

         if (tex->textures[i]) {
            view = fd6_pipe_sampler_view(tex->textures[i]);
            if (unlikely(view->rsc_seqno !=
                         fd_resource(view->base.texture)->seqno)) {
               fd6_sampler_view_update(ctx,
                                       fd6_pipe_sampler_view(tex->textures[i]));
            }
         } else {
            static const struct fd6_pipe_sampler_view dummy_view = {};
            view = &dummy_view;
         }

         OUT_RING(state, view->descriptor[0]);
         OUT_RING(state, view->descriptor[1]);
         OUT_RING(state, view->descriptor[2]);
         OUT_RING(state, view->descriptor[3]);

         if (view->ptr1) {
            OUT_RELOC(state, view->ptr1->bo, view->descriptor[4],
                      (uint64_t)view->descriptor[5] << 32, 0);
         } else {
            OUT_RING(state, view->descriptor[4]);
            OUT_RING(state, view->descriptor[5]);
         }

         OUT_RING(state, view->descriptor[6]);

         if (view->ptr2) {
            OUT_RELOC(state, view->ptr2->bo, view->descriptor[7], 0, 0);
         } else {
            OUT_RING(state, view->descriptor[7]);
            OUT_RING(state, view->descriptor[8]);
         }

         OUT_RING(state, view->descriptor[9]);
         OUT_RING(state, view->descriptor[10]);
         OUT_RING(state, view->descriptor[11]);
         OUT_RING(state, view->descriptor[12]);
         OUT_RING(state, view->descriptor[13]);
         OUT_RING(state, view->descriptor[14]);
         OUT_RING(state, view->descriptor[15]);
      }

      if (v) {
         const struct ir3_ibo_mapping *mapping = &v->image_mapping;
         struct fd_shaderbuf_stateobj *buf = &ctx->shaderbuf[type];
         struct fd_shaderimg_stateobj *img = &ctx->shaderimg[type];

         for (unsigned i = 0; i < mapping->num_tex; i++) {
            unsigned idx = mapping->tex_to_image[i];
            if (idx & IBO_SSBO) {
               fd6_emit_ssbo_tex(ctx, state, &buf->sb[idx & ~IBO_SSBO]);
            } else {
               fd6_emit_image_tex(ctx, state, &img->si[idx]);
            }
         }

         if (v->fb_read) {
            fd6_emit_fb_tex(state, ctx);
         }
      }

      /* emit texture state: */
      OUT_PKT7(ring, opcode, 3);
      OUT_RING(ring, CP_LOAD_STATE6_0_DST_OFF(0) |
                        CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                        CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
                        CP_LOAD_STATE6_0_STATE_BLOCK(sb) |
                        CP_LOAD_STATE6_0_NUM_UNIT(num_merged_textures));
      OUT_RB(ring, state); /* SRC_ADDR_LO/HI */

      OUT_PKT4(ring, tex_const_reg, 2);
      OUT_RB(ring, state); /* SRC_ADDR_LO/HI */

      fd_ringbuffer_del(state);
   }

   OUT_PKT4(ring, tex_count_reg, 1);
   OUT_RING(ring, num_merged_textures);
}

/* Emits combined texture state, which also includes any Image/SSBO
 * related texture state merged in (because we must have all texture
 * state for a given stage in a single buffer).  In the fast-path, if
 * we don't need to merge in any image/ssbo related texture state, we
 * just use cached texture stateobj.  Otherwise we generate a single-
 * use stateobj.
 *
 * TODO Is there some sane way we can still use cached texture stateobj
 * with image/ssbo in use?
 */
static void
fd6_emit_combined_textures(struct fd6_emit *emit,
                           enum pipe_shader_type type,
                           const struct ir3_shader_variant *v) assert_dt
{
   struct fd_context *ctx = emit->ctx;

   static const enum fd6_state_id  s[PIPE_SHADER_TYPES] = {
      [PIPE_SHADER_VERTEX]    = FD6_GROUP_VS_TEX,
      [PIPE_SHADER_TESS_CTRL] = FD6_GROUP_HS_TEX,
      [PIPE_SHADER_TESS_EVAL] = FD6_GROUP_DS_TEX,
      [PIPE_SHADER_GEOMETRY]  = FD6_GROUP_GS_TEX,
      [PIPE_SHADER_FRAGMENT]  = FD6_GROUP_FS_TEX,
   };

   assert((type < ARRAY_SIZE(s)) && s[type]);

   if (!v->image_mapping.num_tex && !v->fb_read) {
      /* in the fast-path, when we don't have to mix in any image/SSBO
       * related texture state, we can just lookup the stateobj and
       * re-emit that:
       *
       * Also, framebuffer-read is a slow-path because an extra
       * texture needs to be inserted.
       */
      if (ctx->tex[type].num_textures > 0) {
         struct fd6_texture_state *tex =
            fd6_texture_state(ctx, type, &ctx->tex[type]);

         fd6_state_add_group(&emit->state, tex->stateobj, s[type]);

         fd6_texture_state_reference(&tex, NULL);
      }
   } else {
      /* In the slow-path, create a one-shot texture state object
       * if either TEX|PROG|SSBO|IMAGE state is dirty:
       */
      if ((ctx->dirty_shader[type] &
           (FD_DIRTY_SHADER_TEX | FD_DIRTY_SHADER_PROG | FD_DIRTY_SHADER_IMAGE |
            FD_DIRTY_SHADER_SSBO)) ||
          v->fb_read) {
         struct fd_texture_stateobj *tex = &ctx->tex[type];
         struct fd_ringbuffer *stateobj = fd_submit_new_ringbuffer(
            ctx->batch->submit, 0x1000, FD_RINGBUFFER_STREAMING);

         fd6_emit_textures(ctx, stateobj, type, tex, v);

         fd6_state_take_group(&emit->state, stateobj, s[type]);
      }
   }
}

static struct fd_ringbuffer *
build_vbo_state(struct fd6_emit *emit) assert_dt
{
   const struct fd_vertex_state *vtx = &emit->ctx->vtx;

   /* Limit PKT4 size, because at max count (32) we would overflow the
    * size of the PKT4 size field:
    */
   const unsigned maxcnt = 16;
   const unsigned cnt = vtx->vertexbuf.count;
   const unsigned dwords = (cnt * 4) /* per vbo: reg64 + two reg32 */
               + (1 + cnt / maxcnt); /* PKT4 hdr every 16 vbo's */

   struct fd_ringbuffer *ring = fd_submit_new_ringbuffer(
      emit->ctx->batch->submit, 4 * dwords, FD_RINGBUFFER_STREAMING);

   for (int32_t j = 0; j < cnt; j++) {
      if ((j % maxcnt) == 0) {
         unsigned sz = MIN2(maxcnt, cnt - j);
         OUT_PKT4(ring, REG_A6XX_VFD_FETCH(j), 4 * sz);
      }
      const struct pipe_vertex_buffer *vb = &vtx->vertexbuf.vb[j];
      struct fd_resource *rsc = fd_resource(vb->buffer.resource);
      if (rsc == NULL) {
         OUT_RING(ring, 0);
         OUT_RING(ring, 0);
         OUT_RING(ring, 0);
         OUT_RING(ring, 0);
      } else {
         uint32_t off = vb->buffer_offset;
         uint32_t size = vb->buffer.resource->width0 - off;

         OUT_RELOC(ring, rsc->bo, off, 0, 0);
         OUT_RING(ring, size);       /* VFD_FETCH[j].SIZE */
         OUT_RING(ring, vb->stride); /* VFD_FETCH[j].STRIDE */
      }
   }

   return ring;
}

static enum a6xx_ztest_mode
compute_ztest_mode(struct fd6_emit *emit, bool lrz_valid) assert_dt
{
   struct fd_context *ctx = emit->ctx;
   struct pipe_framebuffer_state *pfb = &ctx->batch->framebuffer;
   struct fd6_zsa_stateobj *zsa = fd6_zsa_stateobj(ctx->zsa);
   const struct ir3_shader_variant *fs = emit->fs;

   if (fs->fs.early_fragment_tests)
      return A6XX_EARLY_Z;

   if (fs->no_earlyz || fs->writes_pos || !zsa->base.depth_enabled ||
       fs->writes_stencilref) {
      return A6XX_LATE_Z;
   } else if ((fs->has_kill || zsa->alpha_test) &&
              (zsa->writes_zs || !pfb->zsbuf)) {
      /* Slightly odd, but seems like the hw wants us to select
       * LATE_Z mode if there is no depth buffer + discard.  Either
       * that, or when occlusion query is enabled.  See:
       *
       * dEQP-GLES31.functional.fbo.no_attachments.*
       */
      return lrz_valid ? A6XX_EARLY_LRZ_LATE_Z : A6XX_LATE_Z;
   } else {
      return A6XX_EARLY_Z;
   }
}

/**
 * Calculate normalized LRZ state based on zsa/prog/blend state, updating
 * the zsbuf's lrz state as necessary to detect the cases where we need
 * to invalidate lrz.
 */
static struct fd6_lrz_state
compute_lrz_state(struct fd6_emit *emit) assert_dt
{
   struct fd_context *ctx = emit->ctx;
   struct pipe_framebuffer_state *pfb = &ctx->batch->framebuffer;
   const struct ir3_shader_variant *fs = emit->fs;
   struct fd6_lrz_state lrz;

   if (!pfb->zsbuf) {
      memset(&lrz, 0, sizeof(lrz));
      lrz.z_mode = compute_ztest_mode(emit, false);
      return lrz;
   }

   struct fd6_blend_stateobj *blend = fd6_blend_stateobj(ctx->blend);
   struct fd6_zsa_stateobj *zsa = fd6_zsa_stateobj(ctx->zsa);
   struct fd_resource *rsc = fd_resource(pfb->zsbuf->texture);

   lrz = zsa->lrz;

   /* normalize lrz state: */
   if (blend->reads_dest || fs->writes_pos || fs->no_earlyz || fs->has_kill ||
       blend->base.alpha_to_coverage) {
      lrz.write = false;
   }

   /* Unwritten channels *that actually exist* are a form of blending
    * reading the dest from the PoV of LRZ, but the valid dst channels
    * isn't known when blend CSO is constructed so we need to handle
    * that here.
    */
   if (ctx->all_mrt_channel_mask & ~blend->all_mrt_write_mask)
      lrz.write = false;

   /* if we change depthfunc direction, bail out on using LRZ.  The
    * LRZ buffer encodes a min/max depth value per block, but if
    * we switch from GT/GE <-> LT/LE, those values cannot be
    * interpreted properly.
    */
   if (zsa->base.depth_enabled && (rsc->lrz_direction != FD_LRZ_UNKNOWN) &&
       (rsc->lrz_direction != lrz.direction)) {
      rsc->lrz_valid = false;
   }

   if (zsa->invalidate_lrz || !rsc->lrz_valid) {
      rsc->lrz_valid = false;
      memset(&lrz, 0, sizeof(lrz));
   }

   if (fs->no_earlyz || fs->writes_pos) {
      lrz.enable = false;
      lrz.write = false;
      lrz.test = false;
   }

   lrz.z_mode = compute_ztest_mode(emit, rsc->lrz_valid);

   /* Once we start writing to the real depth buffer, we lock in the
    * direction for LRZ.. if we have to skip a LRZ write for any
    * reason, it is still safe to have LRZ until there is a direction
    * reversal.  Prior to the reversal, since we disabled LRZ writes
    * in the "unsafe" cases, this just means that the LRZ test may
    * not early-discard some things that end up not passing a later
    * test (ie. be overly concervative).  But once you have a reversal
    * of direction, it is possible to increase/decrease the z value
    * to the point where the overly-conservative test is incorrect.
    */
   if (zsa->base.depth_writemask) {
      rsc->lrz_direction = lrz.direction;
   }

   return lrz;
}

static struct fd_ringbuffer *
build_lrz(struct fd6_emit *emit) assert_dt
{
   struct fd_context *ctx = emit->ctx;
   struct fd6_context *fd6_ctx = fd6_context(ctx);
   struct fd6_lrz_state lrz = compute_lrz_state(emit);

   /* If the LRZ state has not changed, we can skip the emit: */
   if (!ctx->last.dirty &&
       !memcmp(&fd6_ctx->last.lrz, &lrz, sizeof(lrz)))
      return NULL;

   fd6_ctx->last.lrz = lrz;

   struct fd_ringbuffer *ring = fd_submit_new_ringbuffer(
      ctx->batch->submit, 8 * 4, FD_RINGBUFFER_STREAMING);

   OUT_REG(ring,
           A6XX_GRAS_LRZ_CNTL(.enable = lrz.enable, .lrz_write = lrz.write,
                              .greater = lrz.direction == FD_LRZ_GREATER,
                              .z_test_enable = lrz.test, ));
   OUT_REG(ring, A6XX_RB_LRZ_CNTL(.enable = lrz.enable, ));

   OUT_REG(ring, A6XX_RB_DEPTH_PLANE_CNTL(.z_mode = lrz.z_mode, ));

   OUT_REG(ring, A6XX_GRAS_SU_DEPTH_PLANE_CNTL(.z_mode = lrz.z_mode, ));

   return ring;
}

static struct fd_ringbuffer *
build_scissor(struct fd6_emit *emit) assert_dt
{
   struct fd_context *ctx = emit->ctx;
   struct pipe_scissor_state *scissors = fd_context_get_scissor(ctx);
   unsigned num_viewports = emit->prog->num_viewports;

   struct fd_ringbuffer *ring = fd_submit_new_ringbuffer(
      emit->ctx->batch->submit, (1 + (2 * num_viewports)) * 4, FD_RINGBUFFER_STREAMING);

   OUT_PKT4(ring, REG_A6XX_GRAS_SC_SCREEN_SCISSOR_TL(0), 2 * num_viewports);
   for (unsigned i = 0; i < num_viewports; i++) {
      OUT_RING(ring, A6XX_GRAS_SC_SCREEN_SCISSOR_TL_X(scissors[i].minx) |
               A6XX_GRAS_SC_SCREEN_SCISSOR_TL_Y(scissors[i].miny));
      OUT_RING(ring, A6XX_GRAS_SC_SCREEN_SCISSOR_BR_X(scissors[i].maxx) |
               A6XX_GRAS_SC_SCREEN_SCISSOR_BR_Y(scissors[i].maxy));
   }

   return ring;
}

/* Combination of FD_DIRTY_FRAMEBUFFER | FD_DIRTY_RASTERIZER_DISCARD |
 * FD_DIRTY_PROG | FD_DIRTY_DUAL_BLEND
 */
static struct fd_ringbuffer *
build_prog_fb_rast(struct fd6_emit *emit) assert_dt
{
   struct fd_context *ctx = emit->ctx;
   struct pipe_framebuffer_state *pfb = &ctx->batch->framebuffer;
   const struct fd6_program_state *prog = fd6_emit_get_prog(emit);
   const struct ir3_shader_variant *fs = emit->fs;

   struct fd_ringbuffer *ring = fd_submit_new_ringbuffer(
      ctx->batch->submit, 9 * 4, FD_RINGBUFFER_STREAMING);

   unsigned nr = pfb->nr_cbufs;

   if (ctx->rasterizer->rasterizer_discard)
      nr = 0;

   struct fd6_blend_stateobj *blend = fd6_blend_stateobj(ctx->blend);

   if (blend->use_dual_src_blend)
      nr++;

   OUT_PKT4(ring, REG_A6XX_RB_FS_OUTPUT_CNTL0, 2);
   OUT_RING(ring, COND(fs->writes_pos, A6XX_RB_FS_OUTPUT_CNTL0_FRAG_WRITES_Z) |
                     COND(fs->writes_smask && pfb->samples > 1,
                          A6XX_RB_FS_OUTPUT_CNTL0_FRAG_WRITES_SAMPMASK) |
                     COND(fs->writes_stencilref,
                          A6XX_RB_FS_OUTPUT_CNTL0_FRAG_WRITES_STENCILREF) |
                     COND(blend->use_dual_src_blend,
                          A6XX_RB_FS_OUTPUT_CNTL0_DUAL_COLOR_IN_ENABLE));
   OUT_RING(ring, A6XX_RB_FS_OUTPUT_CNTL1_MRT(nr));

   OUT_PKT4(ring, REG_A6XX_SP_FS_OUTPUT_CNTL1, 1);
   OUT_RING(ring, A6XX_SP_FS_OUTPUT_CNTL1_MRT(nr));

   unsigned mrt_components = 0;
   for (unsigned i = 0; i < pfb->nr_cbufs; i++) {
      if (!pfb->cbufs[i])
         continue;
      mrt_components |= 0xf << (i * 4);
   }

   /* dual source blending has an extra fs output in the 2nd slot */
   if (blend->use_dual_src_blend)
      mrt_components |= 0xf << 4;

   mrt_components &= prog->mrt_components;

   OUT_REG(ring, A6XX_SP_FS_RENDER_COMPONENTS(.dword = mrt_components));
   OUT_REG(ring, A6XX_RB_RENDER_COMPONENTS(.dword = mrt_components));

   return ring;
}

static struct fd_ringbuffer *
build_blend_color(struct fd6_emit *emit) assert_dt
{
   struct fd_context *ctx = emit->ctx;
   struct pipe_blend_color *bcolor = &ctx->blend_color;
   struct fd_ringbuffer *ring = fd_submit_new_ringbuffer(
      ctx->batch->submit, 5 * 4, FD_RINGBUFFER_STREAMING);

   OUT_REG(ring, A6XX_RB_BLEND_RED_F32(bcolor->color[0]),
           A6XX_RB_BLEND_GREEN_F32(bcolor->color[1]),
           A6XX_RB_BLEND_BLUE_F32(bcolor->color[2]),
           A6XX_RB_BLEND_ALPHA_F32(bcolor->color[3]));

   return ring;
}

static struct fd_ringbuffer *
build_ibo(struct fd6_emit *emit) assert_dt
{
   struct fd_context *ctx = emit->ctx;

   if (emit->hs) {
      assert(ir3_shader_nibo(emit->hs) == 0);
      assert(ir3_shader_nibo(emit->ds) == 0);
   }
   if (emit->gs) {
      assert(ir3_shader_nibo(emit->gs) == 0);
   }

   unsigned nibo = ir3_shader_nibo(emit->fs);

   if (nibo == 0)
      return NULL;

   struct fd_ringbuffer *ibo_state =
      fd6_build_ibo_state(ctx, emit->fs, PIPE_SHADER_FRAGMENT);
   struct fd_ringbuffer *ring = fd_submit_new_ringbuffer(
      ctx->batch->submit, 0x100, FD_RINGBUFFER_STREAMING);

   OUT_PKT7(ring, CP_LOAD_STATE6, 3);
   OUT_RING(ring, CP_LOAD_STATE6_0_DST_OFF(0) |
                     CP_LOAD_STATE6_0_STATE_TYPE(ST6_SHADER) |
                     CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
                     CP_LOAD_STATE6_0_STATE_BLOCK(SB6_IBO) |
                     CP_LOAD_STATE6_0_NUM_UNIT(nibo));
   OUT_RB(ring, ibo_state);

   OUT_PKT4(ring, REG_A6XX_SP_IBO, 2);
   OUT_RB(ring, ibo_state);

   /* TODO if we used CP_SET_DRAW_STATE for compute shaders, we could
    * de-duplicate this from program->config_stateobj
    */
   OUT_PKT4(ring, REG_A6XX_SP_IBO_COUNT, 1);
   OUT_RING(ring, nibo);

   fd_ringbuffer_del(ibo_state);

   return ring;
}

static void
fd6_emit_streamout(struct fd_ringbuffer *ring, struct fd6_emit *emit) assert_dt
{
   struct fd_context *ctx = emit->ctx;
   const struct fd6_program_state *prog = fd6_emit_get_prog(emit);
   const struct ir3_stream_output_info *info = prog->stream_output;
   struct fd_streamout_stateobj *so = &ctx->streamout;
   unsigned streamout_mask = 0;

   if (!info)
      return;

   for (unsigned i = 0; i < so->num_targets; i++) {
      struct fd_stream_output_target *target =
         fd_stream_output_target(so->targets[i]);

      if (!target)
         continue;

      target->stride = info->stride[i];

      OUT_PKT4(ring, REG_A6XX_VPC_SO_BUFFER_BASE(i), 3);
      /* VPC_SO[i].BUFFER_BASE_LO: */
      OUT_RELOC(ring, fd_resource(target->base.buffer)->bo, 0, 0, 0);
      OUT_RING(ring, target->base.buffer_size + target->base.buffer_offset);

      struct fd_bo *offset_bo = fd_resource(target->offset_buf)->bo;

      if (so->reset & (1 << i)) {
         assert(so->offsets[i] == 0);

         OUT_PKT7(ring, CP_MEM_WRITE, 3);
         OUT_RELOC(ring, offset_bo, 0, 0, 0);
         OUT_RING(ring, target->base.buffer_offset);

         OUT_PKT4(ring, REG_A6XX_VPC_SO_BUFFER_OFFSET(i), 1);
         OUT_RING(ring, target->base.buffer_offset);
      } else {
         OUT_PKT7(ring, CP_MEM_TO_REG, 3);
         OUT_RING(ring, CP_MEM_TO_REG_0_REG(REG_A6XX_VPC_SO_BUFFER_OFFSET(i)) |
                           CP_MEM_TO_REG_0_SHIFT_BY_2 | CP_MEM_TO_REG_0_UNK31 |
                           CP_MEM_TO_REG_0_CNT(0));
         OUT_RELOC(ring, offset_bo, 0, 0, 0);
      }

      // After a draw HW would write the new offset to offset_bo
      OUT_PKT4(ring, REG_A6XX_VPC_SO_FLUSH_BASE(i), 2);
      OUT_RELOC(ring, offset_bo, 0, 0, 0);

      so->reset &= ~(1 << i);

      streamout_mask |= (1 << i);
   }

   if (streamout_mask) {
      fd6_state_add_group(&emit->state, prog->streamout_stateobj, FD6_GROUP_SO);
   } else if (ctx->last.streamout_mask != 0) {
      /* If we transition from a draw with streamout to one without, turn
       * off streamout.
       */
      fd6_state_add_group(&emit->state, fd6_context(ctx)->streamout_disable_stateobj,
                         FD6_GROUP_SO);
   }

   /* Make sure that any use of our TFB outputs (indirect draw source or shader
    * UBO reads) comes after the TFB output is written.  From the GL 4.6 core
    * spec:
    *
    *     "Buffers should not be bound or in use for both transform feedback and
    *      other purposes in the GL.  Specifically, if a buffer object is
    *      simultaneously bound to a transform feedback buffer binding point
    *      and elsewhere in the GL, any writes to or reads from the buffer
    *      generate undefined values."
    *
    * So we idle whenever SO buffers change.  Note that this function is called
    * on every draw with TFB enabled, so check the dirty flag for the buffers
    * themselves.
    */
   if (ctx->dirty & FD_DIRTY_STREAMOUT)
      fd_wfi(ctx->batch, ring);

   ctx->last.streamout_mask = streamout_mask;
   emit->streamout_mask = streamout_mask;
}

/**
 * Stuff that less frequently changes and isn't (yet) moved into stategroups
 */
static void
fd6_emit_non_ring(struct fd_ringbuffer *ring, struct fd6_emit *emit) assert_dt
{
   struct fd_context *ctx = emit->ctx;
   const enum fd_dirty_3d_state dirty = ctx->dirty;
   unsigned num_viewports = emit->prog->num_viewports;

   if (dirty & FD_DIRTY_STENCIL_REF) {
      struct pipe_stencil_ref *sr = &ctx->stencil_ref;

      OUT_PKT4(ring, REG_A6XX_RB_STENCILREF, 1);
      OUT_RING(ring, A6XX_RB_STENCILREF_REF(sr->ref_value[0]) |
                        A6XX_RB_STENCILREF_BFREF(sr->ref_value[1]));
   }

   if (dirty & (FD_DIRTY_VIEWPORT | FD_DIRTY_PROG)) {
      for (unsigned i = 0; i < num_viewports; i++) {
         struct pipe_scissor_state *scissor = &ctx->viewport_scissor[i];
         struct pipe_viewport_state *vp = & ctx->viewport[i];

         OUT_REG(ring, A6XX_GRAS_CL_VPORT_XOFFSET(i, vp->translate[0]),
                 A6XX_GRAS_CL_VPORT_XSCALE(i, vp->scale[0]),
                 A6XX_GRAS_CL_VPORT_YOFFSET(i, vp->translate[1]),
                 A6XX_GRAS_CL_VPORT_YSCALE(i, vp->scale[1]),
                 A6XX_GRAS_CL_VPORT_ZOFFSET(i, vp->translate[2]),
                 A6XX_GRAS_CL_VPORT_ZSCALE(i, vp->scale[2]));

         OUT_REG(
               ring,
               A6XX_GRAS_SC_VIEWPORT_SCISSOR_TL(i,
                                                .x = scissor->minx,
                                                .y = scissor->miny),
               A6XX_GRAS_SC_VIEWPORT_SCISSOR_BR(i,
                                                .x = scissor->maxx,
                                                .y = scissor->maxy));
      }

      OUT_REG(ring, A6XX_GRAS_CL_GUARDBAND_CLIP_ADJ(.horz = ctx->guardband.x,
                                                    .vert = ctx->guardband.y));
   }

   /* The clamp ranges are only used when the rasterizer wants depth
    * clamping.
    */
   if ((dirty & (FD_DIRTY_VIEWPORT | FD_DIRTY_RASTERIZER | FD_DIRTY_PROG)) &&
       fd_depth_clamp_enabled(ctx)) {
      for (unsigned i = 0; i < num_viewports; i++) {
         struct pipe_viewport_state *vp = & ctx->viewport[i];
         float zmin, zmax;

         util_viewport_zmin_zmax(vp, ctx->rasterizer->clip_halfz,
                                 &zmin, &zmax);

         OUT_REG(ring, A6XX_GRAS_CL_Z_CLAMP_MIN(i, zmin),
                 A6XX_GRAS_CL_Z_CLAMP_MAX(i, zmax));

         /* TODO: what to do about this and multi viewport ? */
         if (i == 0)
            OUT_REG(ring, A6XX_RB_Z_CLAMP_MIN(zmin), A6XX_RB_Z_CLAMP_MAX(zmax));
      }
   }
}

void
fd6_emit_3d_state(struct fd_ringbuffer *ring, struct fd6_emit *emit)
{
   struct fd_context *ctx = emit->ctx;
   struct pipe_framebuffer_state *pfb = &ctx->batch->framebuffer;
   const struct fd6_program_state *prog = fd6_emit_get_prog(emit);
   const struct ir3_shader_variant *vs = emit->vs;
   const struct ir3_shader_variant *hs = emit->hs;
   const struct ir3_shader_variant *ds = emit->ds;
   const struct ir3_shader_variant *gs = emit->gs;
   const struct ir3_shader_variant *fs = emit->fs;

   emit_marker6(ring, 5);

   /* NOTE: we track fb_read differently than _BLEND_ENABLED since we
    * might decide to do sysmem in some cases when blend is enabled:
    */
   if (fs->fb_read)
      ctx->batch->gmem_reason |= FD_GMEM_FB_READ;

   u_foreach_bit (b, emit->dirty_groups) {
      enum fd6_state_id group = b;
      struct fd_ringbuffer *state = NULL;

      switch (group) {
      case FD6_GROUP_VTXSTATE:
         state = fd6_vertex_stateobj(ctx->vtx.vtx)->stateobj;
         fd6_state_add_group(&emit->state, state, FD6_GROUP_VTXSTATE);
         break;
      case FD6_GROUP_VBO:
         state = build_vbo_state(emit);
         fd6_state_take_group(&emit->state, state, FD6_GROUP_VBO);
         break;
      case FD6_GROUP_ZSA:
         state = fd6_zsa_state(
            ctx,
            util_format_is_pure_integer(pipe_surface_format(pfb->cbufs[0])),
            fd_depth_clamp_enabled(ctx));
         fd6_state_add_group(&emit->state, state, FD6_GROUP_ZSA);
         break;
      case FD6_GROUP_LRZ:
         state = build_lrz(emit);
         if (state)
            fd6_state_take_group(&emit->state, state, FD6_GROUP_LRZ);
         break;
      case FD6_GROUP_SCISSOR:
         state = build_scissor(emit);
         fd6_state_take_group(&emit->state, state, FD6_GROUP_SCISSOR);
         break;
      case FD6_GROUP_PROG:
         fd6_state_add_group(&emit->state, prog->config_stateobj,
                             FD6_GROUP_PROG_CONFIG);
         fd6_state_add_group(&emit->state, prog->stateobj, FD6_GROUP_PROG);
         fd6_state_add_group(&emit->state, prog->binning_stateobj,
                             FD6_GROUP_PROG_BINNING);

         /* emit remaining streaming program state, ie. what depends on
          * other emit state, so cannot be pre-baked.
          */
         fd6_state_take_group(&emit->state, fd6_program_interp_state(emit),
                              FD6_GROUP_PROG_INTERP);
         break;
      case FD6_GROUP_RASTERIZER:
         state = fd6_rasterizer_state(ctx, emit->primitive_restart);
         fd6_state_add_group(&emit->state, state, FD6_GROUP_RASTERIZER);
         break;
      case FD6_GROUP_PROG_FB_RAST:
         state = build_prog_fb_rast(emit);
         fd6_state_take_group(&emit->state, state, FD6_GROUP_PROG_FB_RAST);
         break;
      case FD6_GROUP_BLEND:
         state = fd6_blend_variant(ctx->blend, pfb->samples, ctx->sample_mask)
                    ->stateobj;
         fd6_state_add_group(&emit->state, state, FD6_GROUP_BLEND);
         break;
      case FD6_GROUP_BLEND_COLOR:
         state = build_blend_color(emit);
         fd6_state_take_group(&emit->state, state, FD6_GROUP_BLEND_COLOR);
         break;
      case FD6_GROUP_IBO:
         state = build_ibo(emit);
         fd6_state_take_group(&emit->state, state, FD6_GROUP_IBO);
         break;
      case FD6_GROUP_CONST:
         state = fd6_build_user_consts(emit);
         fd6_state_take_group(&emit->state, state, FD6_GROUP_CONST);
         break;
      case FD6_GROUP_DRIVER_PARAMS:
         state = fd6_build_driver_params(emit);
         fd6_state_take_group(&emit->state, state, FD6_GROUP_DRIVER_PARAMS);
         break;
      case FD6_GROUP_PRIMITIVE_PARAMS:
         state = fd6_build_tess_consts(emit);
         fd6_state_take_group(&emit->state, state, FD6_GROUP_PRIMITIVE_PARAMS);
         break;
      case FD6_GROUP_VS_TEX:
         fd6_emit_combined_textures(emit, PIPE_SHADER_VERTEX, vs);
         break;
      case FD6_GROUP_HS_TEX:
         if (hs) {
            fd6_emit_combined_textures(emit, PIPE_SHADER_TESS_CTRL, hs);
         }
         break;
      case FD6_GROUP_DS_TEX:
         if (ds) {
            fd6_emit_combined_textures(emit, PIPE_SHADER_TESS_EVAL, ds);
         }
         break;
      case FD6_GROUP_GS_TEX:
         if (gs) {
            fd6_emit_combined_textures(emit, PIPE_SHADER_GEOMETRY, gs);
         }
         break;
      case FD6_GROUP_FS_TEX:
         fd6_emit_combined_textures(emit, PIPE_SHADER_FRAGMENT, fs);
         break;
      case FD6_GROUP_SO:
         fd6_emit_streamout(ring, emit);
         break;
      case FD6_GROUP_NON_GROUP:
         fd6_emit_non_ring(ring, emit);
         break;
      default:
         break;
      }
   }

   fd6_state_emit(&emit->state, ring);
}

void
fd6_emit_cs_state(struct fd_context *ctx, struct fd_ringbuffer *ring,
                  struct ir3_shader_variant *cp)
{
   enum fd_dirty_shader_state dirty = ctx->dirty_shader[PIPE_SHADER_COMPUTE];

   if (dirty & (FD_DIRTY_SHADER_TEX | FD_DIRTY_SHADER_PROG |
                FD_DIRTY_SHADER_IMAGE | FD_DIRTY_SHADER_SSBO)) {
      struct fd_texture_stateobj *tex = &ctx->tex[PIPE_SHADER_COMPUTE];

      fd6_emit_textures(ctx, ring, PIPE_SHADER_COMPUTE, tex, cp);

      OUT_PKT4(ring, REG_A6XX_SP_VS_TEX_COUNT, 1);
      OUT_RING(ring, 0);

      OUT_PKT4(ring, REG_A6XX_SP_HS_TEX_COUNT, 1);
      OUT_RING(ring, 0);

      OUT_PKT4(ring, REG_A6XX_SP_DS_TEX_COUNT, 1);
      OUT_RING(ring, 0);

      OUT_PKT4(ring, REG_A6XX_SP_GS_TEX_COUNT, 1);
      OUT_RING(ring, 0);

      OUT_PKT4(ring, REG_A6XX_SP_FS_TEX_COUNT, 1);
      OUT_RING(ring, 0);
   }

   if (dirty & (FD_DIRTY_SHADER_SSBO | FD_DIRTY_SHADER_IMAGE)) {
      struct fd_ringbuffer *state =
         fd6_build_ibo_state(ctx, cp, PIPE_SHADER_COMPUTE);

      OUT_PKT7(ring, CP_LOAD_STATE6_FRAG, 3);
      OUT_RING(ring, CP_LOAD_STATE6_0_DST_OFF(0) |
                        CP_LOAD_STATE6_0_STATE_TYPE(ST6_IBO) |
                        CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
                        CP_LOAD_STATE6_0_STATE_BLOCK(SB6_CS_SHADER) |
                        CP_LOAD_STATE6_0_NUM_UNIT(ir3_shader_nibo(cp)));
      OUT_RB(ring, state);

      OUT_PKT4(ring, REG_A6XX_SP_CS_IBO, 2);
      OUT_RB(ring, state);

      OUT_PKT4(ring, REG_A6XX_SP_CS_IBO_COUNT, 1);
      OUT_RING(ring, ir3_shader_nibo(cp));

      fd_ringbuffer_del(state);
   }
}

/* emit setup at begin of new cmdstream buffer (don't rely on previous
 * state, there could have been a context switch between ioctls):
 */
void
fd6_emit_restore(struct fd_batch *batch, struct fd_ringbuffer *ring)
{
   struct fd_screen *screen = batch->ctx->screen;

   if (!batch->nondraw) {
      trace_start_state_restore(&batch->trace, ring);
   }

   fd6_cache_inv(batch, ring);

   OUT_REG(ring,
           A6XX_HLSQ_INVALIDATE_CMD(.vs_state = true, .hs_state = true,
                                    .ds_state = true, .gs_state = true,
                                    .fs_state = true, .cs_state = true,
                                    .gfx_ibo = true, .cs_ibo = true,
                                    .gfx_shared_const = true,
                                    .cs_shared_const = true,
                                    .gfx_bindless = 0x1f, .cs_bindless = 0x1f));

   OUT_WFI5(ring);

   WRITE(REG_A6XX_RB_DBG_ECO_CNTL, screen->info->a6xx.magic.RB_DBG_ECO_CNTL);
   WRITE(REG_A6XX_SP_FLOAT_CNTL, A6XX_SP_FLOAT_CNTL_F16_NO_INF);
   WRITE(REG_A6XX_SP_DBG_ECO_CNTL, 0);
   WRITE(REG_A6XX_SP_PERFCTR_ENABLE, 0x3f);
   WRITE(REG_A6XX_TPL1_UNKNOWN_B605, 0x44);
   WRITE(REG_A6XX_TPL1_DBG_ECO_CNTL, screen->info->a6xx.magic.TPL1_DBG_ECO_CNTL);
   WRITE(REG_A6XX_HLSQ_UNKNOWN_BE00, 0x80);
   WRITE(REG_A6XX_HLSQ_UNKNOWN_BE01, 0);

   WRITE(REG_A6XX_VPC_DBG_ECO_CNTL, 0);
   WRITE(REG_A6XX_GRAS_DBG_ECO_CNTL, 0x880);
   WRITE(REG_A6XX_HLSQ_DBG_ECO_CNTL, 0x80000);
   WRITE(REG_A6XX_SP_CHICKEN_BITS, 0x1430);
   WRITE(REG_A6XX_SP_IBO_COUNT, 0);
   WRITE(REG_A6XX_SP_UNKNOWN_B182, 0);
   WRITE(REG_A6XX_HLSQ_SHARED_CONSTS, 0);
   WRITE(REG_A6XX_UCHE_UNKNOWN_0E12, 0x3200000);
   WRITE(REG_A6XX_UCHE_CLIENT_PF, 4);
   WRITE(REG_A6XX_RB_UNKNOWN_8E01, 0x1);
   WRITE(REG_A6XX_SP_MODE_CONTROL,
         A6XX_SP_MODE_CONTROL_CONSTANT_DEMOTION_ENABLE | 4);
   WRITE(REG_A6XX_VFD_ADD_OFFSET, A6XX_VFD_ADD_OFFSET_VERTEX);
   WRITE(REG_A6XX_RB_UNKNOWN_8811, 0x00000010);
   WRITE(REG_A6XX_PC_MODE_CNTL, 0x1f);

   WRITE(REG_A6XX_GRAS_LRZ_PS_INPUT_CNTL, 0);
   WRITE(REG_A6XX_GRAS_SAMPLE_CNTL, 0);
   WRITE(REG_A6XX_GRAS_UNKNOWN_8110, 0x2);

   WRITE(REG_A6XX_RB_UNKNOWN_8818, 0);
   WRITE(REG_A6XX_RB_UNKNOWN_8819, 0);
   WRITE(REG_A6XX_RB_UNKNOWN_881A, 0);
   WRITE(REG_A6XX_RB_UNKNOWN_881B, 0);
   WRITE(REG_A6XX_RB_UNKNOWN_881C, 0);
   WRITE(REG_A6XX_RB_UNKNOWN_881D, 0);
   WRITE(REG_A6XX_RB_UNKNOWN_881E, 0);
   WRITE(REG_A6XX_RB_UNKNOWN_88F0, 0);

   WRITE(REG_A6XX_VPC_POINT_COORD_INVERT, A6XX_VPC_POINT_COORD_INVERT(0).value);
   WRITE(REG_A6XX_VPC_UNKNOWN_9300, 0);

   WRITE(REG_A6XX_VPC_SO_DISABLE, A6XX_VPC_SO_DISABLE(true).value);

   WRITE(REG_A6XX_PC_RASTER_CNTL, 0);

   WRITE(REG_A6XX_PC_MULTIVIEW_CNTL, 0);

   WRITE(REG_A6XX_SP_UNKNOWN_B183, 0);

   WRITE(REG_A6XX_GRAS_SU_CONSERVATIVE_RAS_CNTL, 0);
   WRITE(REG_A6XX_GRAS_VS_LAYER_CNTL, 0);
   WRITE(REG_A6XX_GRAS_SC_CNTL, A6XX_GRAS_SC_CNTL_CCUSINGLECACHELINESIZE(2));
   WRITE(REG_A6XX_GRAS_UNKNOWN_80AF, 0);
   WRITE(REG_A6XX_VPC_UNKNOWN_9210, 0);
   WRITE(REG_A6XX_VPC_UNKNOWN_9211, 0);
   WRITE(REG_A6XX_VPC_UNKNOWN_9602, 0);
   WRITE(REG_A6XX_PC_UNKNOWN_9E72, 0);
   WRITE(REG_A6XX_SP_TP_SAMPLE_CONFIG, 0);
   /* NOTE blob seems to (mostly?) use 0xb2 for SP_TP_MODE_CNTL
    * but this seems to kill texture gather offsets.
    */
   WRITE(REG_A6XX_SP_TP_MODE_CNTL, 0xa0 |
         A6XX_SP_TP_MODE_CNTL_ISAMMODE(ISAMMODE_GL));
   WRITE(REG_A6XX_RB_SAMPLE_CONFIG, 0);
   WRITE(REG_A6XX_GRAS_SAMPLE_CONFIG, 0);
   WRITE(REG_A6XX_RB_Z_BOUNDS_MIN, 0);
   WRITE(REG_A6XX_RB_Z_BOUNDS_MAX, 0);
   WRITE(REG_A6XX_HLSQ_CONTROL_5_REG, 0xfc);

   emit_marker6(ring, 7);

   OUT_PKT4(ring, REG_A6XX_VFD_MODE_CNTL, 1);
   OUT_RING(ring, 0x00000000); /* VFD_MODE_CNTL */

   WRITE(REG_A6XX_VFD_MULTIVIEW_CNTL, 0);

   OUT_PKT4(ring, REG_A6XX_PC_MODE_CNTL, 1);
   OUT_RING(ring, 0x0000001f); /* PC_MODE_CNTL */

   /* Clear any potential pending state groups to be safe: */
   OUT_PKT7(ring, CP_SET_DRAW_STATE, 3);
   OUT_RING(ring, CP_SET_DRAW_STATE__0_COUNT(0) |
                     CP_SET_DRAW_STATE__0_DISABLE_ALL_GROUPS |
                     CP_SET_DRAW_STATE__0_GROUP_ID(0));
   OUT_RING(ring, CP_SET_DRAW_STATE__1_ADDR_LO(0));
   OUT_RING(ring, CP_SET_DRAW_STATE__2_ADDR_HI(0));

   OUT_PKT4(ring, REG_A6XX_VPC_SO_STREAM_CNTL, 1);
   OUT_RING(ring, 0x00000000); /* VPC_SO_STREAM_CNTL */

   OUT_PKT4(ring, REG_A6XX_GRAS_LRZ_CNTL, 1);
   OUT_RING(ring, 0x00000000);

   OUT_PKT4(ring, REG_A6XX_RB_LRZ_CNTL, 1);
   OUT_RING(ring, 0x00000000);

   /* Initialize VFD_FETCH[n].SIZE to zero to avoid iova faults trying
    * to fetch from a VFD_FETCH[n].BASE which we've potentially inherited
    * from another process:
    */
   for (int32_t i = 0; i < 32; i++) {
      OUT_PKT4(ring, REG_A6XX_VFD_FETCH_SIZE(i), 1);
      OUT_RING(ring, 0);
   }

   /* This happens after all drawing has been emitted to the draw CS, so we know
    * whether we need the tess BO pointers.
    */
   if (batch->tessellation) {
      assert(screen->tess_bo);
      OUT_PKT4(ring, REG_A6XX_PC_TESSFACTOR_ADDR, 2);
      OUT_RELOC(ring, screen->tess_bo, 0, 0, 0);
      /* Updating PC_TESSFACTOR_ADDR could race with the next draw which uses it. */
      OUT_WFI5(ring);
   }

   OUT_PKT4(ring, REG_A6XX_SP_TP_BORDER_COLOR_BASE_ADDR, 2);
   OUT_RELOC(ring, fd6_context(batch->ctx)->bcolor_mem, 0, 0, 0);

   OUT_PKT4(ring, REG_A6XX_SP_PS_TP_BORDER_COLOR_BASE_ADDR, 2);
   OUT_RELOC(ring, fd6_context(batch->ctx)->bcolor_mem, 0, 0, 0);

   if (!batch->nondraw) {
      trace_end_state_restore(&batch->trace, ring);
   }
}

static void
fd6_mem_to_mem(struct fd_ringbuffer *ring, struct pipe_resource *dst,
               unsigned dst_off, struct pipe_resource *src, unsigned src_off,
               unsigned sizedwords)
{
   struct fd_bo *src_bo = fd_resource(src)->bo;
   struct fd_bo *dst_bo = fd_resource(dst)->bo;
   unsigned i;

   for (i = 0; i < sizedwords; i++) {
      OUT_PKT7(ring, CP_MEM_TO_MEM, 5);
      OUT_RING(ring, 0x00000000);
      OUT_RELOC(ring, dst_bo, dst_off, 0, 0);
      OUT_RELOC(ring, src_bo, src_off, 0, 0);

      dst_off += 4;
      src_off += 4;
   }
}

/* this is *almost* the same as fd6_cache_flush().. which I guess
 * could be re-worked to be something a bit more generic w/ param
 * indicating what needs to be flushed..  although that would mean
 * figuring out which events trigger what state to flush..
 */
static void
fd6_framebuffer_barrier(struct fd_context *ctx) assert_dt
{
   struct fd6_context *fd6_ctx = fd6_context(ctx);
   struct fd_batch *batch = fd_context_batch_locked(ctx);
   struct fd_ringbuffer *ring = batch->draw;
   unsigned seqno;

   fd_batch_needs_flush(batch);

   seqno = fd6_event_write(batch, ring, RB_DONE_TS, true);

   OUT_PKT7(ring, CP_WAIT_REG_MEM, 6);
   OUT_RING(ring, CP_WAIT_REG_MEM_0_FUNCTION(WRITE_EQ) |
                     CP_WAIT_REG_MEM_0_POLL_MEMORY);
   OUT_RELOC(ring, control_ptr(fd6_ctx, seqno));
   OUT_RING(ring, CP_WAIT_REG_MEM_3_REF(seqno));
   OUT_RING(ring, CP_WAIT_REG_MEM_4_MASK(~0));
   OUT_RING(ring, CP_WAIT_REG_MEM_5_DELAY_LOOP_CYCLES(16));

   fd6_event_write(batch, ring, PC_CCU_FLUSH_COLOR_TS, true);
   fd6_event_write(batch, ring, PC_CCU_FLUSH_DEPTH_TS, true);

   seqno = fd6_event_write(batch, ring, CACHE_FLUSH_TS, true);
   fd_wfi(batch, ring);

   fd6_event_write(batch, ring, CACHE_INVALIDATE, false);

   OUT_PKT7(ring, CP_WAIT_MEM_GTE, 4);
   OUT_RING(ring, CP_WAIT_MEM_GTE_0_RESERVED(0));
   OUT_RELOC(ring, control_ptr(fd6_ctx, seqno));
   OUT_RING(ring, CP_WAIT_MEM_GTE_3_REF(seqno));

   fd_batch_unlock_submit(batch);
   fd_batch_reference(&batch, NULL);
}

void
fd6_emit_init_screen(struct pipe_screen *pscreen)
{
   struct fd_screen *screen = fd_screen(pscreen);
   screen->emit_ib = fd6_emit_ib;
   screen->mem_to_mem = fd6_mem_to_mem;
}

void
fd6_emit_init(struct pipe_context *pctx) disable_thread_safety_analysis
{
   struct fd_context *ctx = fd_context(pctx);
   ctx->framebuffer_barrier = fd6_framebuffer_barrier;
}
