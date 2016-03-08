/*
 * Copyright (c) 2014 Scott Mansell
 * Copyright © 2014 Broadcom
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
 */

#include "util/u_prim.h"
#include "util/u_format.h"
#include "util/u_pack_color.h"
#include "util/u_upload_mgr.h"
#include "indices/u_primconvert.h"

#include "vc4_context.h"
#include "vc4_resource.h"

static void
vc4_get_draw_cl_space(struct vc4_context *vc4)
{
        /* Binner gets our packet state -- vc4_emit.c contents,
         * and the primitive itself.
         */
        cl_ensure_space(&vc4->bcl, 256);

        /* Nothing for rcl -- that's covered by vc4_context.c */

        /* shader_rec gets up to 12 dwords of reloc handles plus a maximally
         * sized shader_rec (104 bytes base for 8 vattrs plus 32 bytes of
         * vattr stride).
         */
        cl_ensure_space(&vc4->shader_rec, 12 * sizeof(uint32_t) + 104 + 8 * 32);

        /* Uniforms are covered by vc4_write_uniforms(). */

        /* There could be up to 16 textures per stage, plus misc other
         * pointers.
         */
        cl_ensure_space(&vc4->bo_handles, (2 * 16 + 20) * sizeof(uint32_t));
        cl_ensure_space(&vc4->bo_pointers,
                        (2 * 16 + 20) * sizeof(struct vc4_bo *));
}

/**
 * Does the initial bining command list setup for drawing to a given FBO.
 */
static void
vc4_start_draw(struct vc4_context *vc4)
{
        if (vc4->needs_flush)
                return;

        vc4_get_draw_cl_space(vc4);

        struct vc4_cl_out *bcl = cl_start(&vc4->bcl);
        //   Tile state data is 48 bytes per tile, I think it can be thrown away
        //   as soon as binning is finished.
        cl_u8(&bcl, VC4_PACKET_TILE_BINNING_MODE_CONFIG);
        cl_u32(&bcl, 0); /* tile alloc addr, filled by kernel */
        cl_u32(&bcl, 0); /* tile alloc size, filled by kernel */
        cl_u32(&bcl, 0); /* tile state addr, filled by kernel */
        cl_u8(&bcl, vc4->draw_tiles_x);
        cl_u8(&bcl, vc4->draw_tiles_y);
        /* Other flags are filled by kernel. */
        cl_u8(&bcl, vc4->msaa ? VC4_BIN_CONFIG_MS_MODE_4X : 0);

        /* START_TILE_BINNING resets the statechange counters in the hardware,
         * which are what is used when a primitive is binned to a tile to
         * figure out what new state packets need to be written to that tile's
         * command list.
         */
        cl_u8(&bcl, VC4_PACKET_START_TILE_BINNING);

        /* Reset the current compressed primitives format.  This gets modified
         * by VC4_PACKET_GL_INDEXED_PRIMITIVE and
         * VC4_PACKET_GL_ARRAY_PRIMITIVE, so it needs to be reset at the start
         * of every tile.
         */
        cl_u8(&bcl, VC4_PACKET_PRIMITIVE_LIST_FORMAT);
        cl_u8(&bcl, (VC4_PRIMITIVE_LIST_FORMAT_16_INDEX |
                     VC4_PRIMITIVE_LIST_FORMAT_TYPE_TRIANGLES));

        vc4->needs_flush = true;
        vc4->draw_calls_queued++;
        vc4->draw_width = vc4->framebuffer.width;
        vc4->draw_height = vc4->framebuffer.height;

        cl_end(&vc4->bcl, bcl);
}

static void
vc4_update_shadow_textures(struct pipe_context *pctx,
                           struct vc4_texture_stateobj *stage_tex)
{
        for (int i = 0; i < stage_tex->num_textures; i++) {
                struct pipe_sampler_view *view = stage_tex->textures[i];
                if (!view)
                        continue;
                struct vc4_resource *rsc = vc4_resource(view->texture);
                if (rsc->shadow_parent)
                        vc4_update_shadow_baselevel_texture(pctx, view);
        }
}

static void
vc4_emit_gl_shader_state(struct vc4_context *vc4, const struct pipe_draw_info *info)
{
        /* VC4_DIRTY_VTXSTATE */
        struct vc4_vertex_stateobj *vtx = vc4->vtx;
        /* VC4_DIRTY_VTXBUF */
        struct vc4_vertexbuf_stateobj *vertexbuf = &vc4->vertexbuf;

        /* The simulator throws a fit if VS or CS don't read an attribute, so
         * we emit a dummy read.
         */
        uint32_t num_elements_emit = MAX2(vtx->num_elements, 1);
        /* Emit the shader record. */
        struct vc4_cl_out *shader_rec =
                cl_start_shader_reloc(&vc4->shader_rec, 3 + num_elements_emit);
        /* VC4_DIRTY_PRIM_MODE | VC4_DIRTY_RASTERIZER */
        cl_u16(&shader_rec,
               VC4_SHADER_FLAG_ENABLE_CLIPPING |
               VC4_SHADER_FLAG_FS_SINGLE_THREAD |
               ((info->mode == PIPE_PRIM_POINTS &&
                 vc4->rasterizer->base.point_size_per_vertex) ?
                VC4_SHADER_FLAG_VS_POINT_SIZE : 0));

        /* VC4_DIRTY_COMPILED_FS */
        cl_u8(&shader_rec, 0); /* fs num uniforms (unused) */
        cl_u8(&shader_rec, vc4->prog.fs->num_inputs);
        cl_reloc(vc4, &vc4->shader_rec, &shader_rec, vc4->prog.fs->bo, 0);
        cl_u32(&shader_rec, 0); /* UBO offset written by kernel */

        /* VC4_DIRTY_COMPILED_VS */
        cl_u16(&shader_rec, 0); /* vs num uniforms */
        cl_u8(&shader_rec, vc4->prog.vs->vattrs_live);
        cl_u8(&shader_rec, vc4->prog.vs->vattr_offsets[8]);
        cl_reloc(vc4, &vc4->shader_rec, &shader_rec, vc4->prog.vs->bo, 0);
        cl_u32(&shader_rec, 0); /* UBO offset written by kernel */

        /* VC4_DIRTY_COMPILED_CS */
        cl_u16(&shader_rec, 0); /* cs num uniforms */
        cl_u8(&shader_rec, vc4->prog.cs->vattrs_live);
        cl_u8(&shader_rec, vc4->prog.cs->vattr_offsets[8]);
        cl_reloc(vc4, &vc4->shader_rec, &shader_rec, vc4->prog.cs->bo, 0);
        cl_u32(&shader_rec, 0); /* UBO offset written by kernel */

        uint32_t max_index = 0xffff;
        for (int i = 0; i < vtx->num_elements; i++) {
                struct pipe_vertex_element *elem = &vtx->pipe[i];
                struct pipe_vertex_buffer *vb =
                        &vertexbuf->vb[elem->vertex_buffer_index];
                struct vc4_resource *rsc = vc4_resource(vb->buffer);
                /* not vc4->dirty tracked: vc4->last_index_bias */
                uint32_t offset = (vb->buffer_offset +
                                   elem->src_offset +
                                   vb->stride * info->index_bias);
                uint32_t vb_size = rsc->bo->size - offset;
                uint32_t elem_size =
                        util_format_get_blocksize(elem->src_format);

                cl_reloc(vc4, &vc4->shader_rec, &shader_rec, rsc->bo, offset);
                cl_u8(&shader_rec, elem_size - 1);
                cl_u8(&shader_rec, vb->stride);
                cl_u8(&shader_rec, vc4->prog.vs->vattr_offsets[i]);
                cl_u8(&shader_rec, vc4->prog.cs->vattr_offsets[i]);

                if (vb->stride > 0) {
                        max_index = MIN2(max_index,
                                         (vb_size - elem_size) / vb->stride);
                }
        }

        if (vtx->num_elements == 0) {
                assert(num_elements_emit == 1);
                struct vc4_bo *bo = vc4_bo_alloc(vc4->screen, 4096, "scratch VBO");
                cl_reloc(vc4, &vc4->shader_rec, &shader_rec, bo, 0);
                cl_u8(&shader_rec, 16 - 1); /* element size */
                cl_u8(&shader_rec, 0); /* stride */
                cl_u8(&shader_rec, 0); /* VS VPM offset */
                cl_u8(&shader_rec, 0); /* CS VPM offset */
                vc4_bo_unreference(&bo);
        }
        cl_end(&vc4->shader_rec, shader_rec);

        struct vc4_cl_out *bcl = cl_start(&vc4->bcl);
        /* the actual draw call. */
        cl_u8(&bcl, VC4_PACKET_GL_SHADER_STATE);
        assert(vtx->num_elements <= 8);
        /* Note that number of attributes == 0 in the packet means 8
         * attributes.  This field also contains the offset into shader_rec.
         */
        cl_u32(&bcl, num_elements_emit & 0x7);
        cl_end(&vc4->bcl, bcl);

        vc4_write_uniforms(vc4, vc4->prog.fs,
                           &vc4->constbuf[PIPE_SHADER_FRAGMENT],
                           &vc4->fragtex);
        vc4_write_uniforms(vc4, vc4->prog.vs,
                           &vc4->constbuf[PIPE_SHADER_VERTEX],
                           &vc4->verttex);
        vc4_write_uniforms(vc4, vc4->prog.cs,
                           &vc4->constbuf[PIPE_SHADER_VERTEX],
                           &vc4->verttex);

        vc4->last_index_bias = info->index_bias;
        vc4->max_index = max_index;
}

/**
 * HW-2116 workaround: Flush the batch before triggering the hardware state
 * counter wraparound behavior.
 *
 * State updates are tracked by a global counter which increments at the first
 * state update after a draw or a START_BINNING.  Tiles can then have their
 * state updated at draw time with a set of cheap checks for whether the
 * state's copy of the global counter matches the global counter the last time
 * that state was written to the tile.
 *
 * The state counters are relatively small and wrap around quickly, so you
 * could get false negatives for needing to update a particular state in the
 * tile.  To avoid this, the hardware attempts to write all of the state in
 * the tile at wraparound time.  This apparently is broken, so we just flush
 * everything before that behavior is triggered.  A batch flush is sufficient
 * to get our current contents drawn and reset the counters to 0.
 *
 * Note that we can't just use VC4_PACKET_FLUSH_ALL, because that caps the
 * tiles with VC4_PACKET_RETURN_FROM_LIST.
 */
static void
vc4_hw_2116_workaround(struct pipe_context *pctx)
{
        struct vc4_context *vc4 = vc4_context(pctx);

        if (vc4->draw_calls_queued == 0x1ef0) {
                perf_debug("Flushing batch due to HW-2116 workaround "
                           "(too many draw calls per scene\n");
                vc4_flush(pctx);
        }
}

static void
vc4_draw_vbo(struct pipe_context *pctx, const struct pipe_draw_info *info)
{
        struct vc4_context *vc4 = vc4_context(pctx);

        if (info->mode >= PIPE_PRIM_QUADS) {
                util_primconvert_save_index_buffer(vc4->primconvert, &vc4->indexbuf);
                util_primconvert_save_rasterizer_state(vc4->primconvert, &vc4->rasterizer->base);
                util_primconvert_draw_vbo(vc4->primconvert, info);
                perf_debug("Fallback conversion for %d %s vertices\n",
                           info->count, u_prim_name(info->mode));
                return;
        }

        /* Before setting up the draw, do any fixup blits necessary. */
        vc4_update_shadow_textures(pctx, &vc4->verttex);
        vc4_update_shadow_textures(pctx, &vc4->fragtex);

        vc4_hw_2116_workaround(pctx);

        vc4_get_draw_cl_space(vc4);

        if (vc4->prim_mode != info->mode) {
                vc4->prim_mode = info->mode;
                vc4->dirty |= VC4_DIRTY_PRIM_MODE;
        }

        vc4_start_draw(vc4);
        vc4_update_compiled_shaders(vc4, info->mode);

        vc4_emit_state(pctx);

        if ((vc4->dirty & (VC4_DIRTY_VTXBUF |
                           VC4_DIRTY_VTXSTATE |
                           VC4_DIRTY_PRIM_MODE |
                           VC4_DIRTY_RASTERIZER |
                           VC4_DIRTY_COMPILED_CS |
                           VC4_DIRTY_COMPILED_VS |
                           VC4_DIRTY_COMPILED_FS |
                           vc4->prog.cs->uniform_dirty_bits |
                           vc4->prog.vs->uniform_dirty_bits |
                           vc4->prog.fs->uniform_dirty_bits)) ||
            vc4->last_index_bias != info->index_bias) {
                vc4_emit_gl_shader_state(vc4, info);
        }

        vc4->dirty = 0;

        /* Note that the primitive type fields match with OpenGL/gallium
         * definitions, up to but not including QUADS.
         */
        struct vc4_cl_out *bcl = cl_start(&vc4->bcl);
        if (info->indexed) {
                uint32_t offset = vc4->indexbuf.offset;
                uint32_t index_size = vc4->indexbuf.index_size;
                struct pipe_resource *prsc;
                if (vc4->indexbuf.index_size == 4) {
                        prsc = vc4_get_shadow_index_buffer(pctx, &vc4->indexbuf,
                                                           info->count, &offset);
                        index_size = 2;
                } else {
                        if (vc4->indexbuf.user_buffer) {
                                prsc = NULL;
                                u_upload_data(vc4->uploader, 0,
                                              info->count * index_size, 4,
                                              vc4->indexbuf.user_buffer,
                                              &offset, &prsc);
                        } else {
                                prsc = vc4->indexbuf.buffer;
                        }
                }
                struct vc4_resource *rsc = vc4_resource(prsc);

                cl_start_reloc(&vc4->bcl, &bcl, 1);
                cl_u8(&bcl, VC4_PACKET_GL_INDEXED_PRIMITIVE);
                cl_u8(&bcl,
                      info->mode |
                      (index_size == 2 ?
                       VC4_INDEX_BUFFER_U16:
                       VC4_INDEX_BUFFER_U8));
                cl_u32(&bcl, info->count);
                cl_reloc(vc4, &vc4->bcl, &bcl, rsc->bo, offset);
                cl_u32(&bcl, vc4->max_index);

                if (vc4->indexbuf.index_size == 4 || vc4->indexbuf.user_buffer)
                        pipe_resource_reference(&prsc, NULL);
        } else {
                cl_u8(&bcl, VC4_PACKET_GL_ARRAY_PRIMITIVE);
                cl_u8(&bcl, info->mode);
                cl_u32(&bcl, info->count);
                cl_u32(&bcl, info->start);
        }
        cl_end(&vc4->bcl, bcl);

        if (vc4->zsa && vc4->zsa->base.depth.enabled) {
                vc4->resolve |= PIPE_CLEAR_DEPTH;
        }
        if (vc4->zsa && vc4->zsa->base.stencil[0].enabled)
                vc4->resolve |= PIPE_CLEAR_STENCIL;
        vc4->resolve |= PIPE_CLEAR_COLOR0;

        vc4->shader_rec_count++;

        if (vc4_debug & VC4_DEBUG_ALWAYS_FLUSH)
                vc4_flush(pctx);
}

static uint32_t
pack_rgba(enum pipe_format format, const float *rgba)
{
        union util_color uc;
        util_pack_color(rgba, format, &uc);
        if (util_format_get_blocksize(format) == 2)
                return uc.us;
        else
                return uc.ui[0];
}

static void
vc4_clear(struct pipe_context *pctx, unsigned buffers,
          const union pipe_color_union *color, double depth, unsigned stencil)
{
        struct vc4_context *vc4 = vc4_context(pctx);

        /* We can't flag new buffers for clearing once we've queued draws.  We
         * could avoid this by using the 3d engine to clear.
         */
        if (vc4->draw_calls_queued) {
                perf_debug("Flushing rendering to process new clear.\n");
                vc4_flush(pctx);
        }

        if (buffers & PIPE_CLEAR_COLOR0) {
                vc4->clear_color[0] = vc4->clear_color[1] =
                        pack_rgba(vc4->framebuffer.cbufs[0]->format,
                                  color->f);
        }

        if (buffers & PIPE_CLEAR_DEPTH) {
                /* Though the depth buffer is stored with Z in the high 24,
                 * for this field we just need to store it in the low 24.
                 */
                vc4->clear_depth = util_pack_z(PIPE_FORMAT_Z24X8_UNORM, depth);
        }

        if (buffers & PIPE_CLEAR_STENCIL)
                vc4->clear_stencil = stencil;

        vc4->draw_min_x = 0;
        vc4->draw_min_y = 0;
        vc4->draw_max_x = vc4->framebuffer.width;
        vc4->draw_max_y = vc4->framebuffer.height;
        vc4->cleared |= buffers;
        vc4->resolve |= buffers;

        vc4_start_draw(vc4);
}

static void
vc4_clear_render_target(struct pipe_context *pctx, struct pipe_surface *ps,
                        const union pipe_color_union *color,
                        unsigned x, unsigned y, unsigned w, unsigned h)
{
        fprintf(stderr, "unimpl: clear RT\n");
}

static void
vc4_clear_depth_stencil(struct pipe_context *pctx, struct pipe_surface *ps,
                        unsigned buffers, double depth, unsigned stencil,
                        unsigned x, unsigned y, unsigned w, unsigned h)
{
        fprintf(stderr, "unimpl: clear DS\n");
}

void
vc4_draw_init(struct pipe_context *pctx)
{
        pctx->draw_vbo = vc4_draw_vbo;
        pctx->clear = vc4_clear;
        pctx->clear_render_target = vc4_clear_render_target;
        pctx->clear_depth_stencil = vc4_clear_depth_stencil;
}
