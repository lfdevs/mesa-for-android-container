/*
 * Copyright © 2014-2015 Broadcom
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

/** @file vc4_job.c
 *
 * Functions for submitting VC4 render jobs to the kernel.
 */

#include <xf86drm.h>
#include "vc4_context.h"

void
vc4_job_init(struct vc4_context *vc4)
{
        vc4_init_cl(vc4, &vc4->bcl);
        vc4_init_cl(vc4, &vc4->shader_rec);
        vc4_init_cl(vc4, &vc4->uniforms);
        vc4_init_cl(vc4, &vc4->bo_handles);
        vc4_init_cl(vc4, &vc4->bo_pointers);
        vc4_job_reset(vc4);
}

void
vc4_job_reset(struct vc4_context *vc4)
{
        struct vc4_bo **referenced_bos = vc4->bo_pointers.base;
        for (int i = 0; i < cl_offset(&vc4->bo_handles) / 4; i++) {
                vc4_bo_unreference(&referenced_bos[i]);
        }
        vc4_reset_cl(&vc4->bcl);
        vc4_reset_cl(&vc4->shader_rec);
        vc4_reset_cl(&vc4->uniforms);
        vc4_reset_cl(&vc4->bo_handles);
        vc4_reset_cl(&vc4->bo_pointers);
        vc4->shader_rec_count = 0;

        vc4->needs_flush = false;
        vc4->draw_calls_queued = 0;

        /* We have no hardware context saved between our draw calls, so we
         * need to flag the next draw as needing all state emitted.  Emitting
         * all state at the start of our draws is also what ensures that we
         * return to the state we need after a previous tile has finished.
         */
        vc4->dirty = ~0;
        vc4->resolve = 0;
        vc4->cleared = 0;

        vc4->draw_min_x = ~0;
        vc4->draw_min_y = ~0;
        vc4->draw_max_x = 0;
        vc4->draw_max_y = 0;
}

static void
vc4_submit_setup_rcl_surface(struct vc4_context *vc4,
                             struct drm_vc4_submit_rcl_surface *submit_surf,
                             struct pipe_surface *psurf,
                             bool is_depth, bool is_write)
{
        struct vc4_surface *surf = vc4_surface(psurf);

        if (!surf) {
                submit_surf->hindex = ~0;
                return;
        }

        struct vc4_resource *rsc = vc4_resource(psurf->texture);
        submit_surf->hindex = vc4_gem_hindex(vc4, rsc->bo);
        submit_surf->offset = surf->offset;

        if (psurf->texture->nr_samples <= 1) {
                if (is_depth) {
                        submit_surf->bits =
                                VC4_SET_FIELD(VC4_LOADSTORE_TILE_BUFFER_ZS,
                                              VC4_LOADSTORE_TILE_BUFFER_BUFFER);

                } else {
                        submit_surf->bits =
                                VC4_SET_FIELD(VC4_LOADSTORE_TILE_BUFFER_COLOR,
                                              VC4_LOADSTORE_TILE_BUFFER_BUFFER) |
                                VC4_SET_FIELD(vc4_rt_format_is_565(psurf->format) ?
                                              VC4_LOADSTORE_TILE_BUFFER_BGR565 :
                                              VC4_LOADSTORE_TILE_BUFFER_RGBA8888,
                                              VC4_LOADSTORE_TILE_BUFFER_FORMAT);
                }
                submit_surf->bits |=
                        VC4_SET_FIELD(surf->tiling,
                                      VC4_LOADSTORE_TILE_BUFFER_TILING);
        } else {
                assert(!is_write);
                submit_surf->flags |= VC4_SUBMIT_RCL_SURFACE_READ_IS_FULL_RES;
        }

        if (is_write)
                rsc->writes++;
}

static void
vc4_submit_setup_rcl_render_config_surface(struct vc4_context *vc4,
                                           struct drm_vc4_submit_rcl_surface *submit_surf,
                                           struct pipe_surface *psurf)
{
        struct vc4_surface *surf = vc4_surface(psurf);

        if (!surf) {
                submit_surf->hindex = ~0;
                return;
        }

        struct vc4_resource *rsc = vc4_resource(psurf->texture);
        submit_surf->hindex = vc4_gem_hindex(vc4, rsc->bo);
        submit_surf->offset = surf->offset;

        if (psurf->texture->nr_samples <= 1) {
                submit_surf->bits =
                        VC4_SET_FIELD(vc4_rt_format_is_565(surf->base.format) ?
                                      VC4_RENDER_CONFIG_FORMAT_BGR565 :
                                      VC4_RENDER_CONFIG_FORMAT_RGBA8888,
                                      VC4_RENDER_CONFIG_FORMAT) |
                        VC4_SET_FIELD(surf->tiling,
                                      VC4_RENDER_CONFIG_MEMORY_FORMAT);
        }

        rsc->writes++;
}

static void
vc4_submit_setup_rcl_msaa_surface(struct vc4_context *vc4,
                                  struct drm_vc4_submit_rcl_surface *submit_surf,
                                  struct pipe_surface *psurf)
{
        struct vc4_surface *surf = vc4_surface(psurf);

        if (!surf) {
                submit_surf->hindex = ~0;
                return;
        }

        struct vc4_resource *rsc = vc4_resource(psurf->texture);
        submit_surf->hindex = vc4_gem_hindex(vc4, rsc->bo);
        submit_surf->offset = surf->offset;
        submit_surf->bits = 0;
        rsc->writes++;
}

/**
 * Submits the job to the kernel and then reinitializes it.
 */
void
vc4_job_submit(struct vc4_context *vc4)
{
        if (vc4_debug & VC4_DEBUG_CL) {
                fprintf(stderr, "BCL:\n");
                vc4_dump_cl(vc4->bcl.base, cl_offset(&vc4->bcl), false);
        }

        struct drm_vc4_submit_cl submit;
        memset(&submit, 0, sizeof(submit));

        cl_ensure_space(&vc4->bo_handles, 6 * sizeof(uint32_t));
        cl_ensure_space(&vc4->bo_pointers, 6 * sizeof(struct vc4_bo *));

        vc4_submit_setup_rcl_surface(vc4, &submit.color_read,
                                     vc4->color_read, false, false);
        vc4_submit_setup_rcl_render_config_surface(vc4, &submit.color_write,
                                                   vc4->color_write);
        vc4_submit_setup_rcl_surface(vc4, &submit.zs_read,
                                     vc4->zs_read, true, false);
        vc4_submit_setup_rcl_surface(vc4, &submit.zs_write,
                                     vc4->zs_write, true, true);

        vc4_submit_setup_rcl_msaa_surface(vc4, &submit.msaa_color_write,
                                          vc4->msaa_color_write);
        vc4_submit_setup_rcl_msaa_surface(vc4, &submit.msaa_zs_write,
                                          vc4->msaa_zs_write);

        if (vc4->msaa) {
                /* This bit controls how many pixels the general
                 * (i.e. subsampled) loads/stores are iterating over
                 * (multisample loads replicate out to the other samples).
                 */
                submit.color_write.bits |= VC4_RENDER_CONFIG_MS_MODE_4X;
                /* Controls whether color_write's
                 * VC4_PACKET_STORE_MS_TILE_BUFFER does 4x decimation
                 */
                submit.color_write.bits |= VC4_RENDER_CONFIG_DECIMATE_MODE_4X;
        }

        submit.bo_handles = (uintptr_t)vc4->bo_handles.base;
        submit.bo_handle_count = cl_offset(&vc4->bo_handles) / 4;
        submit.bin_cl = (uintptr_t)vc4->bcl.base;
        submit.bin_cl_size = cl_offset(&vc4->bcl);
        submit.shader_rec = (uintptr_t)vc4->shader_rec.base;
        submit.shader_rec_size = cl_offset(&vc4->shader_rec);
        submit.shader_rec_count = vc4->shader_rec_count;
        submit.uniforms = (uintptr_t)vc4->uniforms.base;
        submit.uniforms_size = cl_offset(&vc4->uniforms);

        assert(vc4->draw_min_x != ~0 && vc4->draw_min_y != ~0);
        submit.min_x_tile = vc4->draw_min_x / vc4->tile_width;
        submit.min_y_tile = vc4->draw_min_y / vc4->tile_height;
        submit.max_x_tile = (vc4->draw_max_x - 1) / vc4->tile_width;
        submit.max_y_tile = (vc4->draw_max_y - 1) / vc4->tile_height;
        submit.width = vc4->draw_width;
        submit.height = vc4->draw_height;
        if (vc4->cleared) {
                submit.flags |= VC4_SUBMIT_CL_USE_CLEAR_COLOR;
                submit.clear_color[0] = vc4->clear_color[0];
                submit.clear_color[1] = vc4->clear_color[1];
                submit.clear_z = vc4->clear_depth;
                submit.clear_s = vc4->clear_stencil;
        }

        if (!(vc4_debug & VC4_DEBUG_NORAST)) {
                int ret;

#ifndef USE_VC4_SIMULATOR
                ret = drmIoctl(vc4->fd, DRM_IOCTL_VC4_SUBMIT_CL, &submit);
#else
                ret = vc4_simulator_flush(vc4, &submit);
#endif
                static bool warned = false;
                if (ret && !warned) {
                        fprintf(stderr, "Draw call returned %s.  "
                                        "Expect corruption.\n", strerror(errno));
                        warned = true;
                } else if (!ret) {
                        vc4->last_emit_seqno = submit.seqno;
                }
        }

        if (vc4->last_emit_seqno - vc4->screen->finished_seqno > 5) {
                if (!vc4_wait_seqno(vc4->screen,
                                    vc4->last_emit_seqno - 5,
                                    PIPE_TIMEOUT_INFINITE,
                                    "job throttling")) {
                        fprintf(stderr, "Job throttling failed\n");
                }
        }

        if (vc4_debug & VC4_DEBUG_ALWAYS_SYNC) {
                if (!vc4_wait_seqno(vc4->screen, vc4->last_emit_seqno,
                                    PIPE_TIMEOUT_INFINITE, "sync")) {
                        fprintf(stderr, "Wait failed.\n");
                        abort();
                }
        }

        vc4_job_reset(vc4);
}
