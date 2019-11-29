/*
 * Copyright (C) 2019 Collabora, Ltd.
 * Copyright (C) 2019 Red Hat Inc.
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
 * Authors (Collabora):
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 *
 */

#include "pan_context.h"
#include "util/u_memory.h"
#include "nir_serialize.h"

/* Compute CSOs are tracked like graphics shader CSOs, but are
 * considerably simpler. We do not implement multiple
 * variants/keying. So the CSO create function just goes ahead and
 * compiles the thing. */

static void *
panfrost_create_compute_state(
        struct pipe_context *pctx,
        const struct pipe_compute_state *cso)
{
        struct panfrost_context *ctx = pan_context(pctx);

        struct panfrost_shader_variants *so = CALLOC_STRUCT(panfrost_shader_variants);
        so->cbase = *cso;
        so->is_compute = true;

        struct panfrost_shader_state *v = &so->variants[0];

        so->variant_count = 1;
        so->active_variant = 0;

        v->tripipe = malloc(sizeof(struct mali_shader_meta));

        if (cso->ir_type == PIPE_SHADER_IR_NIR_SERIALIZED) {
                struct blob_reader reader;
                const struct pipe_binary_program_header *hdr = cso->prog;

                blob_reader_init(&reader, hdr->blob, hdr->num_bytes);
                so->cbase.prog = nir_deserialize(NULL, &midgard_nir_options, &reader);
                so->cbase.ir_type = PIPE_SHADER_IR_NIR;
        }

        panfrost_shader_compile(ctx, v->tripipe,
                        so->cbase.ir_type, so->cbase.prog,
                        MESA_SHADER_COMPUTE, v, NULL);

        return so;
}

static void
panfrost_bind_compute_state(struct pipe_context *pipe, void *cso)
{
        struct panfrost_context *ctx = pan_context(pipe);

        struct panfrost_shader_variants *variants =
                (struct panfrost_shader_variants *) cso;

        ctx->shader[PIPE_SHADER_COMPUTE] = variants;
}

static void
panfrost_delete_compute_state(struct pipe_context *pipe, void *cso)
{
        free(cso);
}

/* Launch grid is the compute equivalent of draw_vbo, so in this routine, we
 * construct the COMPUTE job and some of its payload.
 */

static void
panfrost_launch_grid(struct pipe_context *pipe,
                const struct pipe_grid_info *info)
{
        struct panfrost_context *ctx = pan_context(pipe);

        /* TODO: Do we want a special compute-only batch? */
        struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);

        ctx->compute_grid = info;

        struct mali_job_descriptor_header job = {
                .job_type = JOB_TYPE_COMPUTE,
                .job_descriptor_size = 1,
                .job_barrier = 1
        };

        /* TODO: Stub */
        struct midgard_payload_vertex_tiler *payload = &ctx->payloads[PIPE_SHADER_COMPUTE];

        panfrost_emit_for_draw(ctx, false);

        /* Compute jobs have a "compute FBD". It's not a real framebuffer
         * descriptor - there is no framebuffer - but it takes the place of
         * one. As far as I can tell, it's actually the beginning of a
         * single-render-target framebuffer descriptor with almost everything
         * zeroed out.
         */
        struct mali_compute_fbd compute_fbd = {
                .unknown1 = {
                        0, 0x1F, 0, 0, 0, 0, 0, 0
                }
        };

        payload->postfix.framebuffer =
                panfrost_upload_transient(batch, &compute_fbd, sizeof(compute_fbd));

        /* Invoke according to the grid info */

        panfrost_pack_work_groups_compute(&payload->prefix,
                        info->grid[0], info->grid[1], info->grid[2],
                        info->block[0], info->block[1], info->block[2], false);

        /* Upload the payload */

        struct panfrost_transfer transfer = panfrost_allocate_transient(batch, sizeof(job) + sizeof(*payload));
        memcpy(transfer.cpu, &job, sizeof(job));
        memcpy(transfer.cpu + sizeof(job), payload, sizeof(*payload));

        /* Queue the job */
        panfrost_scoreboard_queue_compute_job(batch, transfer);

        panfrost_flush_all_batches(ctx, true);
}

void
panfrost_compute_context_init(struct pipe_context *pctx)
{
        pctx->create_compute_state = panfrost_create_compute_state;
        pctx->bind_compute_state = panfrost_bind_compute_state;
        pctx->delete_compute_state = panfrost_delete_compute_state;

        pctx->launch_grid = panfrost_launch_grid;
}


