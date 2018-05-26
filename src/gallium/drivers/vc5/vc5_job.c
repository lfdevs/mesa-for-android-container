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

/** @file vc5_job.c
 *
 * Functions for submitting VC5 render jobs to the kernel.
 */

#include <xf86drm.h>
#include "vc5_context.h"
/* The OQ/semaphore packets are the same across V3D versions. */
#define V3D_VERSION 33
#include "broadcom/cle/v3dx_pack.h"
#include "broadcom/common/v3d_macros.h"
#include "util/hash_table.h"
#include "util/ralloc.h"
#include "util/set.h"
#include "broadcom/clif/clif_dump.h"

static void
remove_from_ht(struct hash_table *ht, void *key)
{
        struct hash_entry *entry = _mesa_hash_table_search(ht, key);
        _mesa_hash_table_remove(ht, entry);
}

static void
vc5_job_free(struct vc5_context *vc5, struct vc5_job *job)
{
        struct set_entry *entry;

        set_foreach(job->bos, entry) {
                struct vc5_bo *bo = (struct vc5_bo *)entry->key;
                vc5_bo_unreference(&bo);
        }

        remove_from_ht(vc5->jobs, &job->key);

        if (job->write_prscs) {
                struct set_entry *entry;

                set_foreach(job->write_prscs, entry) {
                        const struct pipe_resource *prsc = entry->key;

                        remove_from_ht(vc5->write_jobs, (void *)prsc);
                }
        }

        for (int i = 0; i < VC5_MAX_DRAW_BUFFERS; i++) {
                if (job->cbufs[i]) {
                        remove_from_ht(vc5->write_jobs, job->cbufs[i]->texture);
                        pipe_surface_reference(&job->cbufs[i], NULL);
                }
        }
        if (job->zsbuf) {
                remove_from_ht(vc5->write_jobs, job->zsbuf->texture);
                pipe_surface_reference(&job->zsbuf, NULL);
        }

        if (vc5->job == job)
                vc5->job = NULL;

        vc5_destroy_cl(&job->bcl);
        vc5_destroy_cl(&job->rcl);
        vc5_destroy_cl(&job->indirect);
        vc5_bo_unreference(&job->tile_alloc);
        vc5_bo_unreference(&job->tile_state);

        ralloc_free(job);
}

static struct vc5_job *
vc5_job_create(struct vc5_context *vc5)
{
        struct vc5_job *job = rzalloc(vc5, struct vc5_job);

        job->vc5 = vc5;

        vc5_init_cl(job, &job->bcl);
        vc5_init_cl(job, &job->rcl);
        vc5_init_cl(job, &job->indirect);

        job->draw_min_x = ~0;
        job->draw_min_y = ~0;
        job->draw_max_x = 0;
        job->draw_max_y = 0;

        job->bos = _mesa_set_create(job,
                                    _mesa_hash_pointer,
                                    _mesa_key_pointer_equal);
        return job;
}

void
vc5_job_add_bo(struct vc5_job *job, struct vc5_bo *bo)
{
        if (!bo)
                return;

        if (_mesa_set_search(job->bos, bo))
                return;

        vc5_bo_reference(bo);
        _mesa_set_add(job->bos, bo);
        job->referenced_size += bo->size;

        uint32_t *bo_handles = (void *)(uintptr_t)job->submit.bo_handles;

        if (job->submit.bo_handle_count >= job->bo_handles_size) {
                job->bo_handles_size = MAX2(4, job->bo_handles_size * 2);
                bo_handles = reralloc(job, bo_handles,
                                      uint32_t, job->bo_handles_size);
                job->submit.bo_handles = (uintptr_t)(void *)bo_handles;
        }
        bo_handles[job->submit.bo_handle_count++] = bo->handle;
}

void
vc5_job_add_write_resource(struct vc5_job *job, struct pipe_resource *prsc)
{
        struct vc5_context *vc5 = job->vc5;

        if (!job->write_prscs) {
                job->write_prscs = _mesa_set_create(job,
                                                    _mesa_hash_pointer,
                                                    _mesa_key_pointer_equal);
        }

        _mesa_set_add(job->write_prscs, prsc);
        _mesa_hash_table_insert(vc5->write_jobs, prsc, job);
}

void
vc5_flush_jobs_writing_resource(struct vc5_context *vc5,
                                struct pipe_resource *prsc)
{
        struct hash_entry *entry = _mesa_hash_table_search(vc5->write_jobs,
                                                           prsc);
        if (entry) {
                struct vc5_job *job = entry->data;
                vc5_job_submit(vc5, job);
        }
}

void
vc5_flush_jobs_reading_resource(struct vc5_context *vc5,
                                struct pipe_resource *prsc)
{
        struct vc5_resource *rsc = vc5_resource(prsc);

        vc5_flush_jobs_writing_resource(vc5, prsc);

        struct hash_entry *entry;
        hash_table_foreach(vc5->jobs, entry) {
                struct vc5_job *job = entry->data;

                if (_mesa_set_search(job->bos, rsc->bo)) {
                        vc5_job_submit(vc5, job);
                        /* Reminder: vc5->jobs is safe to keep iterating even
                         * after deletion of an entry.
                         */
                        continue;
                }
        }
}

static void
vc5_job_set_tile_buffer_size(struct vc5_job *job)
{
        static const uint8_t tile_sizes[] = {
                64, 64,
                64, 32,
                32, 32,
                32, 16,
                16, 16,
        };
        int tile_size_index = 0;
        if (job->msaa)
                tile_size_index += 2;

        if (job->cbufs[3] || job->cbufs[2])
                tile_size_index += 2;
        else if (job->cbufs[1])
                tile_size_index++;

        int max_bpp = RENDER_TARGET_MAXIMUM_32BPP;
        for (int i = 0; i < VC5_MAX_DRAW_BUFFERS; i++) {
                if (job->cbufs[i]) {
                        struct vc5_surface *surf = vc5_surface(job->cbufs[i]);
                        max_bpp = MAX2(max_bpp, surf->internal_bpp);
                }
        }
        job->internal_bpp = max_bpp;
        STATIC_ASSERT(RENDER_TARGET_MAXIMUM_32BPP == 0);
        tile_size_index += max_bpp;

        assert(tile_size_index < ARRAY_SIZE(tile_sizes));
        job->tile_width = tile_sizes[tile_size_index * 2 + 0];
        job->tile_height = tile_sizes[tile_size_index * 2 + 1];
}

/**
 * Returns a vc5_job struture for tracking V3D rendering to a particular FBO.
 *
 * If we've already started rendering to this FBO, then return old same job,
 * otherwise make a new one.  If we're beginning rendering to an FBO, make
 * sure that any previous reads of the FBO (or writes to its color/Z surfaces)
 * have been flushed.
 */
struct vc5_job *
vc5_get_job(struct vc5_context *vc5,
            struct pipe_surface **cbufs, struct pipe_surface *zsbuf)
{
        /* Return the existing job for this FBO if we have one */
        struct vc5_job_key local_key = {
                .cbufs = {
                        cbufs[0],
                        cbufs[1],
                        cbufs[2],
                        cbufs[3],
                },
                .zsbuf = zsbuf,
        };
        struct hash_entry *entry = _mesa_hash_table_search(vc5->jobs,
                                                           &local_key);
        if (entry)
                return entry->data;

        /* Creating a new job.  Make sure that any previous jobs reading or
         * writing these buffers are flushed.
         */
        struct vc5_job *job = vc5_job_create(vc5);

        for (int i = 0; i < VC5_MAX_DRAW_BUFFERS; i++) {
                if (cbufs[i]) {
                        vc5_flush_jobs_reading_resource(vc5, cbufs[i]->texture);
                        pipe_surface_reference(&job->cbufs[i], cbufs[i]);

                        if (cbufs[i]->texture->nr_samples > 1)
                                job->msaa = true;
                }
        }
        if (zsbuf) {
                vc5_flush_jobs_reading_resource(vc5, zsbuf->texture);
                pipe_surface_reference(&job->zsbuf, zsbuf);
                if (zsbuf->texture->nr_samples > 1)
                        job->msaa = true;
        }

        vc5_job_set_tile_buffer_size(job);

        for (int i = 0; i < VC5_MAX_DRAW_BUFFERS; i++) {
                if (cbufs[i])
                        _mesa_hash_table_insert(vc5->write_jobs,
                                                cbufs[i]->texture, job);
        }
        if (zsbuf)
                _mesa_hash_table_insert(vc5->write_jobs, zsbuf->texture, job);

        memcpy(&job->key, &local_key, sizeof(local_key));
        _mesa_hash_table_insert(vc5->jobs, &job->key, job);

        return job;
}

struct vc5_job *
vc5_get_job_for_fbo(struct vc5_context *vc5)
{
        if (vc5->job)
                return vc5->job;

        struct pipe_surface **cbufs = vc5->framebuffer.cbufs;
        struct pipe_surface *zsbuf = vc5->framebuffer.zsbuf;
        struct vc5_job *job = vc5_get_job(vc5, cbufs, zsbuf);

        /* The dirty flags are tracking what's been updated while vc5->job has
         * been bound, so set them all to ~0 when switching between jobs.  We
         * also need to reset all state at the start of rendering.
         */
        vc5->dirty = ~0;

        /* If we're binding to uninitialized buffers, no need to load their
         * contents before drawing.
         */
        for (int i = 0; i < 4; i++) {
                if (cbufs[i]) {
                        struct vc5_resource *rsc = vc5_resource(cbufs[i]->texture);
                        if (!rsc->writes)
                                job->cleared |= PIPE_CLEAR_COLOR0 << i;
                }
        }

        if (zsbuf) {
                struct vc5_resource *rsc = vc5_resource(zsbuf->texture);
                if (!rsc->writes)
                        job->cleared |= PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL;
        }

        job->draw_tiles_x = DIV_ROUND_UP(vc5->framebuffer.width,
                                         job->tile_width);
        job->draw_tiles_y = DIV_ROUND_UP(vc5->framebuffer.height,
                                         job->tile_height);

        vc5->job = job;

        return job;
}

static bool
vc5_clif_dump_lookup(void *data, uint32_t addr, void **vaddr)
{
        struct vc5_job *job = data;
        struct set_entry *entry;

        set_foreach(job->bos, entry) {
                struct vc5_bo *bo = (void *)entry->key;

                if (addr >= bo->offset &&
                    addr < bo->offset + bo->size) {
                        vc5_bo_map(bo);
                        *vaddr = bo->map + addr - bo->offset;
                        return true;
                }
        }

        return false;
}

static void
vc5_clif_dump(struct vc5_context *vc5, struct vc5_job *job)
{
        if (!(V3D_DEBUG & V3D_DEBUG_CL))
                return;

        struct clif_dump *clif = clif_dump_init(&vc5->screen->devinfo,
                                                stderr, vc5_clif_dump_lookup,
                                                job);

        fprintf(stderr, "BCL: 0x%08x..0x%08x\n",
                job->submit.bcl_start, job->submit.bcl_end);

        clif_dump_add_cl(clif, job->submit.bcl_start, job->submit.bcl_end);

        fprintf(stderr, "RCL: 0x%08x..0x%08x\n",
                job->submit.rcl_start, job->submit.rcl_end);
        clif_dump_add_cl(clif, job->submit.rcl_start, job->submit.rcl_end);
}

/**
 * Submits the job to the kernel and then reinitializes it.
 */
void
vc5_job_submit(struct vc5_context *vc5, struct vc5_job *job)
{
        MAYBE_UNUSED struct vc5_screen *screen = vc5->screen;

        if (!job->needs_flush)
                goto done;

        if (vc5->screen->devinfo.ver >= 41)
                v3d41_emit_rcl(job);
        else
                v3d33_emit_rcl(job);

        if (cl_offset(&job->bcl) > 0) {
                if (screen->devinfo.ver >= 41)
                        v3d41_bcl_epilogue(vc5, job);
                else
                        v3d33_bcl_epilogue(vc5, job);
        }

        job->submit.out_sync = vc5->out_sync;
        job->submit.bcl_end = job->bcl.bo->offset + cl_offset(&job->bcl);
        job->submit.rcl_end = job->rcl.bo->offset + cl_offset(&job->rcl);

        /* On V3D 4.1, the tile alloc/state setup moved to register writes
         * instead of binner packets.
         */
        if (screen->devinfo.ver >= 41) {
                vc5_job_add_bo(job, job->tile_alloc);
                job->submit.qma = job->tile_alloc->offset;
                job->submit.qms = job->tile_alloc->size;

                vc5_job_add_bo(job, job->tile_state);
                job->submit.qts = job->tile_state->offset;
        }

        vc5_clif_dump(vc5, job);

        if (!(V3D_DEBUG & V3D_DEBUG_NORAST)) {
                int ret;

#ifndef USE_VC5_SIMULATOR
                ret = drmIoctl(vc5->fd, DRM_IOCTL_VC5_SUBMIT_CL, &job->submit);
#else
                ret = vc5_simulator_flush(vc5, &job->submit, job);
#endif
                static bool warned = false;
                if (ret && !warned) {
                        fprintf(stderr, "Draw call returned %s.  "
                                        "Expect corruption.\n", strerror(errno));
                        warned = true;
                }
        }

done:
        vc5_job_free(vc5, job);
}

static bool
vc5_job_compare(const void *a, const void *b)
{
        return memcmp(a, b, sizeof(struct vc5_job_key)) == 0;
}

static uint32_t
vc5_job_hash(const void *key)
{
        return _mesa_hash_data(key, sizeof(struct vc5_job_key));
}

void
vc5_job_init(struct vc5_context *vc5)
{
        vc5->jobs = _mesa_hash_table_create(vc5,
                                            vc5_job_hash,
                                            vc5_job_compare);
        vc5->write_jobs = _mesa_hash_table_create(vc5,
                                                  _mesa_hash_pointer,
                                                  _mesa_key_pointer_equal);
}

