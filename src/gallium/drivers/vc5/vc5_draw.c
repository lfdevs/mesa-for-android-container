/*
 * Copyright © 2014-2017 Broadcom
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

#include "util/u_blitter.h"
#include "util/u_prim.h"
#include "util/u_format.h"
#include "util/u_pack_color.h"
#include "util/u_prim_restart.h"
#include "util/u_upload_mgr.h"
#include "indices/u_primconvert.h"

#include "vc5_context.h"
#include "vc5_resource.h"
#include "vc5_cl.h"
#include "broadcom/compiler/v3d_compiler.h"
#include "broadcom/common/v3d_macros.h"
#include "broadcom/cle/v3dx_pack.h"

/**
 * Does the initial bining command list setup for drawing to a given FBO.
 */
static void
vc5_start_draw(struct vc5_context *vc5)
{
        struct vc5_job *job = vc5->job;

        if (job->needs_flush)
                return;

        /* Get space to emit our BCL state, using a branch to jump to a new BO
         * if necessary.
         */
        vc5_cl_ensure_space_with_branch(&job->bcl, 256 /* XXX */);

        job->submit.bcl_start = job->bcl.bo->offset;
        vc5_job_add_bo(job, job->bcl.bo);

        job->tile_alloc = vc5_bo_alloc(vc5->screen, 1024 * 1024, "tile alloc");
        uint32_t tsda_per_tile_size = vc5->screen->devinfo.ver >= 40 ? 256 : 64;
        job->tile_state = vc5_bo_alloc(vc5->screen,
                                       job->draw_tiles_y *
                                       job->draw_tiles_x *
                                       tsda_per_tile_size,
                                       "TSDA");

#if V3D_VERSION < 40
        /* "Binning mode lists start with a Tile Binning Mode Configuration
         * item (120)"
         *
         * Part1 signals the end of binning config setup.
         */
        cl_emit(&job->bcl, TILE_BINNING_MODE_CONFIGURATION_PART2, config) {
                config.tile_allocation_memory_address =
                        cl_address(job->tile_alloc, 0);
                config.tile_allocation_memory_size = job->tile_alloc->size;
        }
#endif

        cl_emit(&job->bcl, TILE_BINNING_MODE_CONFIGURATION_PART1, config) {
#if V3D_VERSION >= 40
                config.width_in_pixels_minus_1 = vc5->framebuffer.width - 1;
                config.height_in_pixels_minus_1 = vc5->framebuffer.height - 1;
                config.number_of_render_targets_minus_1 =
                        MAX2(vc5->framebuffer.nr_cbufs, 1) - 1;
#else /* V3D_VERSION < 40 */
                config.tile_state_data_array_base_address =
                        cl_address(job->tile_state, 0);

                config.width_in_tiles = job->draw_tiles_x;
                config.height_in_tiles = job->draw_tiles_y;
                /* Must be >= 1 */
                config.number_of_render_targets =
                        MAX2(vc5->framebuffer.nr_cbufs, 1);
#endif /* V3D_VERSION < 40 */

                config.multisample_mode_4x = job->msaa;

                config.maximum_bpp_of_all_render_targets = job->internal_bpp;
        }

        /* There's definitely nothing in the VCD cache we want. */
        cl_emit(&job->bcl, FLUSH_VCD_CACHE, bin);

        /* Disable any leftover OQ state from another job. */
        cl_emit(&job->bcl, OCCLUSION_QUERY_COUNTER, counter);

        /* "Binning mode lists must have a Start Tile Binning item (6) after
         *  any prefix state data before the binning list proper starts."
         */
        cl_emit(&job->bcl, START_TILE_BINNING, bin);

        job->needs_flush = true;
        job->draw_width = vc5->framebuffer.width;
        job->draw_height = vc5->framebuffer.height;
}

static void
vc5_predraw_check_textures(struct pipe_context *pctx,
                           struct vc5_texture_stateobj *stage_tex)
{
        struct vc5_context *vc5 = vc5_context(pctx);

        for (int i = 0; i < stage_tex->num_textures; i++) {
                struct pipe_sampler_view *view = stage_tex->textures[i];
                if (!view)
                        continue;

                vc5_flush_jobs_writing_resource(vc5, view->texture);
        }
}

static void
vc5_emit_gl_shader_state(struct vc5_context *vc5,
                         const struct pipe_draw_info *info)
{
        struct vc5_job *job = vc5->job;
        /* VC5_DIRTY_VTXSTATE */
        struct vc5_vertex_stateobj *vtx = vc5->vtx;
        /* VC5_DIRTY_VTXBUF */
        struct vc5_vertexbuf_stateobj *vertexbuf = &vc5->vertexbuf;

        /* Upload the uniforms to the indirect CL first */
        struct vc5_cl_reloc fs_uniforms =
                vc5_write_uniforms(vc5, vc5->prog.fs,
                                   &vc5->constbuf[PIPE_SHADER_FRAGMENT],
                                   &vc5->fragtex);
        struct vc5_cl_reloc vs_uniforms =
                vc5_write_uniforms(vc5, vc5->prog.vs,
                                   &vc5->constbuf[PIPE_SHADER_VERTEX],
                                   &vc5->verttex);
        struct vc5_cl_reloc cs_uniforms =
                vc5_write_uniforms(vc5, vc5->prog.cs,
                                   &vc5->constbuf[PIPE_SHADER_VERTEX],
                                   &vc5->verttex);

        /* See GFXH-930 workaround below */
        uint32_t num_elements_to_emit = MAX2(vtx->num_elements, 1);
        uint32_t shader_rec_offset =
                vc5_cl_ensure_space(&job->indirect,
                                    cl_packet_length(GL_SHADER_STATE_RECORD) +
                                    num_elements_to_emit *
                                    cl_packet_length(GL_SHADER_STATE_ATTRIBUTE_RECORD),
                                    32);

        cl_emit(&job->indirect, GL_SHADER_STATE_RECORD, shader) {
                shader.enable_clipping = true;
                /* VC5_DIRTY_PRIM_MODE | VC5_DIRTY_RASTERIZER */
                shader.point_size_in_shaded_vertex_data =
                        (info->mode == PIPE_PRIM_POINTS &&
                         vc5->rasterizer->base.point_size_per_vertex);

                /* Must be set if the shader modifies Z, discards, or modifies
                 * the sample mask.  For any of these cases, the fragment
                 * shader needs to write the Z value (even just discards).
                 */
                shader.fragment_shader_does_z_writes =
                        (vc5->prog.fs->prog_data.fs->writes_z ||
                         vc5->prog.fs->prog_data.fs->discard);

                shader.number_of_varyings_in_fragment_shader =
                        vc5->prog.fs->prog_data.base->num_inputs;

                shader.propagate_nans = true;

                shader.coordinate_shader_code_address =
                        cl_address(vc5->prog.cs->bo, 0);
                shader.vertex_shader_code_address =
                        cl_address(vc5->prog.vs->bo, 0);
                shader.fragment_shader_code_address =
                        cl_address(vc5->prog.fs->bo, 0);

                /* XXX: Use combined input/output size flag in the common
                 * case.
                 */
                shader.coordinate_shader_has_separate_input_and_output_vpm_blocks = true;
                shader.vertex_shader_has_separate_input_and_output_vpm_blocks = true;
                shader.coordinate_shader_input_vpm_segment_size =
                        MAX2(vc5->prog.cs->prog_data.vs->vpm_input_size, 1);
                shader.vertex_shader_input_vpm_segment_size =
                        MAX2(vc5->prog.vs->prog_data.vs->vpm_input_size, 1);

                shader.coordinate_shader_output_vpm_segment_size =
                        vc5->prog.cs->prog_data.vs->vpm_output_size;
                shader.vertex_shader_output_vpm_segment_size =
                        vc5->prog.vs->prog_data.vs->vpm_output_size;

                shader.coordinate_shader_uniforms_address = cs_uniforms;
                shader.vertex_shader_uniforms_address = vs_uniforms;
                shader.fragment_shader_uniforms_address = fs_uniforms;

#if V3D_VERSION >= 41
                shader.coordinate_shader_4_way_threadable =
                        vc5->prog.cs->prog_data.vs->base.threads == 4;
                shader.vertex_shader_4_way_threadable =
                        vc5->prog.vs->prog_data.vs->base.threads == 4;
                shader.fragment_shader_4_way_threadable =
                        vc5->prog.fs->prog_data.fs->base.threads == 4;

                shader.coordinate_shader_start_in_final_thread_section =
                        vc5->prog.cs->prog_data.vs->base.single_seg;
                shader.vertex_shader_start_in_final_thread_section =
                        vc5->prog.vs->prog_data.vs->base.single_seg;
                shader.fragment_shader_start_in_final_thread_section =
                        vc5->prog.fs->prog_data.fs->base.single_seg;
#else
                shader.coordinate_shader_4_way_threadable =
                        vc5->prog.cs->prog_data.vs->base.threads == 4;
                shader.coordinate_shader_2_way_threadable =
                        vc5->prog.cs->prog_data.vs->base.threads == 2;
                shader.vertex_shader_4_way_threadable =
                        vc5->prog.vs->prog_data.vs->base.threads == 4;
                shader.vertex_shader_2_way_threadable =
                        vc5->prog.vs->prog_data.vs->base.threads == 2;
                shader.fragment_shader_4_way_threadable =
                        vc5->prog.fs->prog_data.fs->base.threads == 4;
                shader.fragment_shader_2_way_threadable =
                        vc5->prog.fs->prog_data.fs->base.threads == 2;
#endif

                shader.vertex_id_read_by_coordinate_shader =
                        vc5->prog.cs->prog_data.vs->uses_vid;
                shader.instance_id_read_by_coordinate_shader =
                        vc5->prog.cs->prog_data.vs->uses_iid;
                shader.vertex_id_read_by_vertex_shader =
                        vc5->prog.vs->prog_data.vs->uses_vid;
                shader.instance_id_read_by_vertex_shader =
                        vc5->prog.vs->prog_data.vs->uses_iid;

                shader.address_of_default_attribute_values =
                        cl_address(vtx->default_attribute_values, 0);
        }

        for (int i = 0; i < vtx->num_elements; i++) {
                struct pipe_vertex_element *elem = &vtx->pipe[i];
                struct pipe_vertex_buffer *vb =
                        &vertexbuf->vb[elem->vertex_buffer_index];
                struct vc5_resource *rsc = vc5_resource(vb->buffer.resource);

                const uint32_t size =
                        cl_packet_length(GL_SHADER_STATE_ATTRIBUTE_RECORD);
                cl_emit_with_prepacked(&job->indirect,
                                       GL_SHADER_STATE_ATTRIBUTE_RECORD,
                                       &vtx->attrs[i * size], attr) {
                        attr.stride = vb->stride;
                        attr.address = cl_address(rsc->bo,
                                                  vb->buffer_offset +
                                                  elem->src_offset);
                        attr.number_of_values_read_by_coordinate_shader =
                                vc5->prog.cs->prog_data.vs->vattr_sizes[i];
                        attr.number_of_values_read_by_vertex_shader =
                                vc5->prog.vs->prog_data.vs->vattr_sizes[i];
#if V3D_VERSION >= 41
                        attr.maximum_index = 0xffffff;
#endif
                }
        }

        if (vtx->num_elements == 0) {
                /* GFXH-930: At least one attribute must be enabled and read
                 * by CS and VS.  If we have no attributes being consumed by
                 * the shader, set up a dummy to be loaded into the VPM.
                 */
                cl_emit(&job->indirect, GL_SHADER_STATE_ATTRIBUTE_RECORD, attr) {
                        /* Valid address of data whose value will be unused. */
                        attr.address = cl_address(job->indirect.bo, 0);

                        attr.type = ATTRIBUTE_FLOAT;
                        attr.stride = 0;
                        attr.vec_size = 1;

                        attr.number_of_values_read_by_coordinate_shader = 1;
                        attr.number_of_values_read_by_vertex_shader = 1;
                }
        }

        cl_emit(&job->bcl, GL_SHADER_STATE, state) {
                state.address = cl_address(job->indirect.bo, shader_rec_offset);
                state.number_of_attribute_arrays = num_elements_to_emit;
        }

        vc5_bo_unreference(&cs_uniforms.bo);
        vc5_bo_unreference(&vs_uniforms.bo);
        vc5_bo_unreference(&fs_uniforms.bo);

        job->shader_rec_count++;
}

/**
 * Computes the various transform feedback statistics, since they can't be
 * recorded by CL packets.
 */
static void
vc5_tf_statistics_record(struct vc5_context *vc5,
                         const struct pipe_draw_info *info,
                         bool prim_tf)
{
        if (!vc5->active_queries)
                return;

        uint32_t prims = u_prims_for_vertices(info->mode, info->count);
        vc5->prims_generated += prims;

        if (prim_tf) {
                /* XXX: Only count if we didn't overflow. */
                vc5->tf_prims_generated += prims;
        }
}

static void
vc5_update_job_ez(struct vc5_context *vc5, struct vc5_job *job)
{
        switch (vc5->zsa->ez_state) {
        case VC5_EZ_UNDECIDED:
                /* If the Z/S state didn't pick a direction but didn't
                 * disable, then go along with the current EZ state.  This
                 * allows EZ optimization for Z func == EQUAL or NEVER.
                 */
                break;

        case VC5_EZ_LT_LE:
        case VC5_EZ_GT_GE:
                /* If the Z/S state picked a direction, then it needs to match
                 * the current direction if we've decided on one.
                 */
                if (job->ez_state == VC5_EZ_UNDECIDED)
                        job->ez_state = vc5->zsa->ez_state;
                else if (job->ez_state != vc5->zsa->ez_state)
                        job->ez_state = VC5_EZ_DISABLED;
                break;

        case VC5_EZ_DISABLED:
                /* If the current Z/S state disables EZ because of a bad Z
                 * func or stencil operation, then we can't do any more EZ in
                 * this frame.
                 */
                job->ez_state = VC5_EZ_DISABLED;
                break;
        }

        /* If the FS affects the Z of the pixels, then it may update against
         * the chosen EZ direction (though we could use
         * ARB_conservative_depth's hints to avoid this)
         */
        if (vc5->prog.fs->prog_data.fs->writes_z) {
                job->ez_state = VC5_EZ_DISABLED;
        }

        if (job->first_ez_state == VC5_EZ_UNDECIDED)
                job->first_ez_state = job->ez_state;
}

static void
vc5_draw_vbo(struct pipe_context *pctx, const struct pipe_draw_info *info)
{
        struct vc5_context *vc5 = vc5_context(pctx);

        if (!info->count_from_stream_output && !info->indirect &&
            !info->primitive_restart &&
            !u_trim_pipe_prim(info->mode, (unsigned*)&info->count))
                return;

        /* Fall back for weird desktop GL primitive restart values. */
        if (info->primitive_restart &&
            info->index_size) {
                uint32_t mask = ~0;

                switch (info->index_size) {
                case 2:
                        mask = 0xffff;
                        break;
                case 1:
                        mask = 0xff;
                        break;
                }

                if (info->restart_index != mask) {
                        util_draw_vbo_without_prim_restart(pctx, info);
                        return;
                }
        }

        if (info->mode >= PIPE_PRIM_QUADS) {
                util_primconvert_save_rasterizer_state(vc5->primconvert, &vc5->rasterizer->base);
                util_primconvert_draw_vbo(vc5->primconvert, info);
                perf_debug("Fallback conversion for %d %s vertices\n",
                           info->count, u_prim_name(info->mode));
                return;
        }

        /* Before setting up the draw, flush anything writing to the textures
         * that we read from.
         */
        vc5_predraw_check_textures(pctx, &vc5->verttex);
        vc5_predraw_check_textures(pctx, &vc5->fragtex);

        struct vc5_job *job = vc5_get_job_for_fbo(vc5);

        /* Get space to emit our draw call into the BCL, using a branch to
         * jump to a new BO if necessary.
         */
        vc5_cl_ensure_space_with_branch(&job->bcl, 256 /* XXX */);

        if (vc5->prim_mode != info->mode) {
                vc5->prim_mode = info->mode;
                vc5->dirty |= VC5_DIRTY_PRIM_MODE;
        }

        vc5_start_draw(vc5);
        vc5_update_compiled_shaders(vc5, info->mode);
        vc5_update_job_ez(vc5, job);

#if V3D_VERSION >= 41
        v3d41_emit_state(pctx);
#else
        v3d33_emit_state(pctx);
#endif

        if (vc5->dirty & (VC5_DIRTY_VTXBUF |
                          VC5_DIRTY_VTXSTATE |
                          VC5_DIRTY_PRIM_MODE |
                          VC5_DIRTY_RASTERIZER |
                          VC5_DIRTY_COMPILED_CS |
                          VC5_DIRTY_COMPILED_VS |
                          VC5_DIRTY_COMPILED_FS |
                          vc5->prog.cs->uniform_dirty_bits |
                          vc5->prog.vs->uniform_dirty_bits |
                          vc5->prog.fs->uniform_dirty_bits)) {
                vc5_emit_gl_shader_state(vc5, info);
        }

        vc5->dirty = 0;

        /* The Base Vertex/Base Instance packet sets those values to nonzero
         * for the next draw call only.
         */
        if (info->index_bias || info->start_instance) {
                cl_emit(&job->bcl, BASE_VERTEX_BASE_INSTANCE, base) {
                        base.base_instance = info->start_instance;
                        base.base_vertex = info->index_bias;
                }
        }

        uint32_t prim_tf_enable = 0;
#if V3D_VERSION < 40
        /* V3D 3.x: The HW only processes transform feedback on primitives
         * with the flag set.
         */
        if (vc5->streamout.num_targets)
                prim_tf_enable = (V3D_PRIM_POINTS_TF - V3D_PRIM_POINTS);
#endif

        vc5_tf_statistics_record(vc5, info, vc5->streamout.num_targets);

        /* Note that the primitive type fields match with OpenGL/gallium
         * definitions, up to but not including QUADS.
         */
        if (info->index_size) {
                uint32_t index_size = info->index_size;
                uint32_t offset = info->start * index_size;
                struct pipe_resource *prsc;
                if (info->has_user_indices) {
                        prsc = NULL;
                        u_upload_data(vc5->uploader, 0,
                                      info->count * info->index_size, 4,
                                      info->index.user,
                                      &offset, &prsc);
                } else {
                        prsc = info->index.resource;
                }
                struct vc5_resource *rsc = vc5_resource(prsc);

#if V3D_VERSION >= 40
                cl_emit(&job->bcl, INDEX_BUFFER_SETUP, ib) {
                        ib.address = cl_address(rsc->bo, 0);
                        ib.size = rsc->bo->size;
                }
#endif

                if (info->instance_count > 1) {
                        cl_emit(&job->bcl, INDEXED_INSTANCED_PRIMITIVE_LIST, prim) {
                                prim.index_type = ffs(info->index_size) - 1;
#if V3D_VERSION >= 40
                                prim.index_offset = offset;
#else /* V3D_VERSION < 40 */
                                prim.maximum_index = (1u << 31) - 1; /* XXX */
                                prim.address_of_indices_list =
                                        cl_address(rsc->bo, offset);
#endif /* V3D_VERSION < 40 */
                                prim.mode = info->mode | prim_tf_enable;
                                prim.enable_primitive_restarts = info->primitive_restart;

                                prim.number_of_instances = info->instance_count;
                                prim.instance_length = info->count;
                        }
                } else {
                        cl_emit(&job->bcl, INDEXED_PRIMITIVE_LIST, prim) {
                                prim.index_type = ffs(info->index_size) - 1;
                                prim.length = info->count;
#if V3D_VERSION >= 40
                                prim.index_offset = offset;
#else /* V3D_VERSION < 40 */
                                prim.maximum_index = (1u << 31) - 1; /* XXX */
                                prim.address_of_indices_list =
                                        cl_address(rsc->bo, offset);
#endif /* V3D_VERSION < 40 */
                                prim.mode = info->mode | prim_tf_enable;
                                prim.enable_primitive_restarts = info->primitive_restart;
                        }
                }

                job->draw_calls_queued++;

                if (info->has_user_indices)
                        pipe_resource_reference(&prsc, NULL);
        } else {
                if (info->instance_count > 1) {
                        cl_emit(&job->bcl, VERTEX_ARRAY_INSTANCED_PRIMITIVES, prim) {
                                prim.mode = info->mode | prim_tf_enable;
                                prim.index_of_first_vertex = info->start;
                                prim.number_of_instances = info->instance_count;
                                prim.instance_length = info->count;
                        }
                } else {
                        cl_emit(&job->bcl, VERTEX_ARRAY_PRIMITIVES, prim) {
                                prim.mode = info->mode | prim_tf_enable;
                                prim.length = info->count;
                                prim.index_of_first_vertex = info->start;
                        }
                }
        }
        job->draw_calls_queued++;

        if (vc5->zsa && job->zsbuf &&
            (vc5->zsa->base.depth.enabled ||
             vc5->zsa->base.stencil[0].enabled)) {
                struct vc5_resource *rsc = vc5_resource(job->zsbuf->texture);
                vc5_job_add_bo(job, rsc->bo);

                if (vc5->zsa->base.depth.enabled) {
                        job->resolve |= PIPE_CLEAR_DEPTH;
                        rsc->initialized_buffers = PIPE_CLEAR_DEPTH;
                }

                if (vc5->zsa->base.stencil[0].enabled) {
                        job->resolve |= PIPE_CLEAR_STENCIL;
                        rsc->initialized_buffers |= PIPE_CLEAR_STENCIL;
                }
        }

        for (int i = 0; i < VC5_MAX_DRAW_BUFFERS; i++) {
                uint32_t bit = PIPE_CLEAR_COLOR0 << i;

                if (job->resolve & bit || !job->cbufs[i])
                        continue;
                struct vc5_resource *rsc = vc5_resource(job->cbufs[i]->texture);

                job->resolve |= bit;
                vc5_job_add_bo(job, rsc->bo);
        }

        if (job->referenced_size > 768 * 1024 * 1024) {
                perf_debug("Flushing job with %dkb to try to free up memory\n",
                        job->referenced_size / 1024);
                vc5_flush(pctx);
        }

        if (V3D_DEBUG & V3D_DEBUG_ALWAYS_FLUSH)
                vc5_flush(pctx);
}

static void
vc5_clear(struct pipe_context *pctx, unsigned buffers,
          const union pipe_color_union *color, double depth, unsigned stencil)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        struct vc5_job *job = vc5_get_job_for_fbo(vc5);

        /* We can't flag new buffers for clearing once we've queued draws.  We
         * could avoid this by using the 3d engine to clear.
         */
        if (job->draw_calls_queued) {
                perf_debug("Flushing rendering to process new clear.\n");
                vc5_job_submit(vc5, job);
                job = vc5_get_job_for_fbo(vc5);
        }

        for (int i = 0; i < VC5_MAX_DRAW_BUFFERS; i++) {
                uint32_t bit = PIPE_CLEAR_COLOR0 << i;
                if (!(buffers & bit))
                        continue;

                struct pipe_surface *psurf = vc5->framebuffer.cbufs[i];
                struct vc5_surface *surf = vc5_surface(psurf);
                struct vc5_resource *rsc = vc5_resource(psurf->texture);

                union util_color uc;
                uint32_t internal_size = 4 << surf->internal_bpp;

                static union pipe_color_union swapped_color;
                if (vc5->swap_color_rb & (1 << i)) {
                        swapped_color.f[0] = color->f[2];
                        swapped_color.f[1] = color->f[1];
                        swapped_color.f[2] = color->f[0];
                        swapped_color.f[3] = color->f[3];
                        color = &swapped_color;
                }

                switch (surf->internal_type) {
                case V3D_INTERNAL_TYPE_8:
                        util_pack_color(color->f, PIPE_FORMAT_R8G8B8A8_UNORM,
                                        &uc);
                        memcpy(job->clear_color[i], uc.ui, internal_size);
                        break;
                case V3D_INTERNAL_TYPE_8I:
                case V3D_INTERNAL_TYPE_8UI:
                        job->clear_color[i][0] = ((color->ui[0] & 0xff) |
                                                  (color->ui[1] & 0xff) << 8 |
                                                  (color->ui[2] & 0xff) << 16 |
                                                  (color->ui[3] & 0xff) << 24);
                        break;
                case V3D_INTERNAL_TYPE_16F:
                        util_pack_color(color->f, PIPE_FORMAT_R16G16B16A16_FLOAT,
                                        &uc);
                        memcpy(job->clear_color[i], uc.ui, internal_size);
                        break;
                case V3D_INTERNAL_TYPE_16I:
                case V3D_INTERNAL_TYPE_16UI:
                        job->clear_color[i][0] = ((color->ui[0] & 0xffff) |
                                                  color->ui[1] << 16);
                        job->clear_color[i][1] = ((color->ui[2] & 0xffff) |
                                                  color->ui[3] << 16);
                        break;
                case V3D_INTERNAL_TYPE_32F:
                case V3D_INTERNAL_TYPE_32I:
                case V3D_INTERNAL_TYPE_32UI:
                        memcpy(job->clear_color[i], color->ui, internal_size);
                        break;
                }

                rsc->initialized_buffers |= bit;
        }

        unsigned zsclear = buffers & PIPE_CLEAR_DEPTHSTENCIL;
        if (zsclear) {
                struct vc5_resource *rsc =
                        vc5_resource(vc5->framebuffer.zsbuf->texture);

                if (zsclear & PIPE_CLEAR_DEPTH)
                        job->clear_z = depth;
                if (zsclear & PIPE_CLEAR_STENCIL)
                        job->clear_s = stencil;

                rsc->initialized_buffers |= zsclear;
        }

        job->draw_min_x = 0;
        job->draw_min_y = 0;
        job->draw_max_x = vc5->framebuffer.width;
        job->draw_max_y = vc5->framebuffer.height;
        job->cleared |= buffers;
        job->resolve |= buffers;

        vc5_start_draw(vc5);
}

static void
vc5_clear_render_target(struct pipe_context *pctx, struct pipe_surface *ps,
                        const union pipe_color_union *color,
                        unsigned x, unsigned y, unsigned w, unsigned h,
                        bool render_condition_enabled)
{
        fprintf(stderr, "unimpl: clear RT\n");
}

static void
vc5_clear_depth_stencil(struct pipe_context *pctx, struct pipe_surface *ps,
                        unsigned buffers, double depth, unsigned stencil,
                        unsigned x, unsigned y, unsigned w, unsigned h,
                        bool render_condition_enabled)
{
        fprintf(stderr, "unimpl: clear DS\n");
}

void
v3dX(draw_init)(struct pipe_context *pctx)
{
        pctx->draw_vbo = vc5_draw_vbo;
        pctx->clear = vc5_clear;
        pctx->clear_render_target = vc5_clear_render_target;
        pctx->clear_depth_stencil = vc5_clear_depth_stencil;
}
