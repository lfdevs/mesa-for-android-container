/*
 * Copyright (c) 2022 Amazon.com, Inc. or its affiliates.
 * Copyright (C) 2019-2022 Collabora, Ltd.
 * Copyright (C) 2019 Red Hat Inc.
 * Copyright (C) 2018 Alyssa Rosenzweig
 * Copyright © 2017 Intel Corporation
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
#include "pan_bo.h"
#include "pan_shader.h"
#include "util/u_memory.h"
#include "nir/tgsi_to_nir.h"
#include "nir_serialize.h"

static struct panfrost_uncompiled_shader *
panfrost_alloc_shader(const nir_shader *nir)
{
        struct panfrost_uncompiled_shader *so =
                rzalloc(NULL, struct panfrost_uncompiled_shader);

        simple_mtx_init(&so->lock, mtx_plain);
        util_dynarray_init(&so->variants, so);

        so->nir = nir;

        /* Serialize the NIR to a binary blob that we can hash for the disk
         * cache. Drop unnecessary information (like variable names) so the
         * serialized NIR is smaller, and also to let us detect more isomorphic
         * shaders when hashing, increasing cache hits.
         */
        struct blob blob;
        blob_init(&blob);
        nir_serialize(&blob, nir, true);
        _mesa_sha1_compute(blob.data, blob.size, so->nir_sha1);
        blob_finish(&blob);

        return so;
}

static struct panfrost_compiled_shader *
panfrost_alloc_variant(struct panfrost_uncompiled_shader *so)
{
        return util_dynarray_grow(&so->variants, struct panfrost_compiled_shader, 1);
}

static void
panfrost_shader_compile(struct panfrost_screen *screen,
                        const nir_shader *ir,
                        struct util_debug_callback *dbg,
                        struct panfrost_shader_key *key,
                        unsigned req_local_mem,
                        unsigned fixed_varying_mask,
                        struct panfrost_shader_binary *out)
{
        struct panfrost_device *dev = pan_device(&screen->base);

        nir_shader *s = nir_shader_clone(NULL, ir);

        struct panfrost_compile_inputs inputs = {
                .debug = dbg,
                .gpu_id = dev->gpu_id,
                .fixed_sysval_ubo = -1,
        };

        /* Lower this early so the backends don't have to worry about it */
        if (s->info.stage == MESA_SHADER_FRAGMENT) {
                inputs.fixed_varying_mask = key->fs.fixed_varying_mask;

                if (s->info.outputs_written & BITFIELD_BIT(FRAG_RESULT_COLOR)) {
                        NIR_PASS_V(s, nir_lower_fragcolor,
                                   key->fs.nr_cbufs_for_fragcolor);
                }

                if (key->fs.sprite_coord_enable) {
                        NIR_PASS_V(s, nir_lower_texcoord_replace,
                                   key->fs.sprite_coord_enable,
                                   true /* point coord is sysval */,
                                   false /* Y-invert */);
                }

                if (key->fs.clip_plane_enable) {
                        NIR_PASS_V(s, nir_lower_clip_fs,
                                   key->fs.clip_plane_enable,
                                   false);
                }

                memcpy(inputs.rt_formats, key->fs.rt_formats, sizeof(inputs.rt_formats));
        } else if (s->info.stage == MESA_SHADER_VERTEX) {
                inputs.fixed_varying_mask = fixed_varying_mask;

                /* No IDVS for internal XFB shaders */
                inputs.no_idvs = s->info.has_transform_feedback_varyings;
        }

        util_dynarray_init(&out->binary, NULL);
        screen->vtbl.compile_shader(s, &inputs, &out->binary, &out->info);

        assert(req_local_mem >= out->info.wls_size);
        out->info.wls_size = req_local_mem;

        /* In both clone and tgsi_to_nir paths, the shader is ralloc'd against
         * a NULL context
         */
        ralloc_free(s);
}

static void
panfrost_shader_get(struct pipe_screen *pscreen,
                    struct panfrost_pool *shader_pool,
                    struct panfrost_pool *desc_pool,
                    struct panfrost_uncompiled_shader *uncompiled,
                    struct util_debug_callback *dbg,
                    struct panfrost_compiled_shader *state,
                    unsigned req_local_mem)
{
        struct panfrost_screen *screen = pan_screen(pscreen);
        struct panfrost_device *dev = pan_device(pscreen);

        struct panfrost_shader_binary res = { 0 };

        /* Try to retrieve the variant from the disk cache. If that fails,
         * compile a new variant and store in the disk cache for later reuse.
         */
        if (!panfrost_disk_cache_retrieve(screen->disk_cache, uncompiled, &state->key, &res)) {
                panfrost_shader_compile(screen, uncompiled->nir, dbg, &state->key,
                                        req_local_mem,
                                        uncompiled->fixed_varying_mask, &res);

                panfrost_disk_cache_store(screen->disk_cache, uncompiled, &state->key, &res);
        }

        state->info = res.info;

        if (res.binary.size) {
                state->bin = panfrost_pool_take_ref(shader_pool,
                        pan_pool_upload_aligned(&shader_pool->base,
                                res.binary.data, res.binary.size, 128));
        }

        util_dynarray_fini(&res.binary);

        /* Don't upload RSD for fragment shaders since they need draw-time
         * merging for e.g. depth/stencil/alpha. RSDs are replaced by simpler
         * shader program descriptors on Valhall, which can be preuploaded even
         * for fragment shaders. */
        bool upload = !(uncompiled->nir->info.stage == MESA_SHADER_FRAGMENT && dev->arch <= 7);
        screen->vtbl.prepare_shader(state, desc_pool, upload);

        panfrost_analyze_sysvals(state);
}

static void
panfrost_build_key(struct panfrost_context *ctx,
                   struct panfrost_shader_key *key,
                   const nir_shader *nir)
{
        /* We don't currently have vertex shader variants */
        if (nir->info.stage != MESA_SHADER_FRAGMENT)
               return;

        struct panfrost_device *dev = pan_device(ctx->base.screen);
        struct pipe_framebuffer_state *fb = &ctx->pipe_framebuffer;
        struct pipe_rasterizer_state *rast = (void *) ctx->rasterizer;
        struct panfrost_uncompiled_shader *vs = ctx->uncompiled[MESA_SHADER_VERTEX];

        /* gl_FragColor lowering needs the number of colour buffers */
        if (nir->info.outputs_written & BITFIELD_BIT(FRAG_RESULT_COLOR)) {
                key->fs.nr_cbufs_for_fragcolor = fb->nr_cbufs;
        }

        /* Point sprite lowering needed on Bifrost and newer */
        if (dev->arch >= 6 && rast && ctx->active_prim == PIPE_PRIM_POINTS) {
                key->fs.sprite_coord_enable = rast->sprite_coord_enable;
        }

        /* User clip plane lowering needed everywhere */
        if (rast) {
                key->fs.clip_plane_enable = rast->clip_plane_enable;
        }

        if (dev->arch <= 5) {
                u_foreach_bit(i, (nir->info.outputs_read >> FRAG_RESULT_DATA0)) {
                        enum pipe_format fmt = PIPE_FORMAT_R8G8B8A8_UNORM;

                        if ((fb->nr_cbufs > i) && fb->cbufs[i])
                                fmt = fb->cbufs[i]->format;

                        if (panfrost_blendable_formats_v6[fmt].internal)
                                fmt = PIPE_FORMAT_NONE;

                        key->fs.rt_formats[i] = fmt;
                }
        }

        /* Funny desktop GL varying lowering on Valhall */
        if (dev->arch >= 9) {
                assert(vs != NULL && "too early");
                key->fs.fixed_varying_mask = vs->fixed_varying_mask;
        }
}

/**
 * Fix an uncompiled shader's stream output info, and produce a bitmask
 * of which VARYING_SLOT_* are captured for stream output.
 *
 * Core Gallium stores output->register_index as a "slot" number, where
 * slots are assigned consecutively to all outputs in info->outputs_written.
 * This naive packing of outputs doesn't work for us - we too have slots,
 * but the layout is defined by the VUE map, which we won't have until we
 * compile a specific shader variant.  So, we remap these and simply store
 * VARYING_SLOT_* in our copy's output->register_index fields.
 *
 * We then produce a bitmask of outputs which are used for SO.
 *
 * Implementation from iris.
 */

static uint64_t
update_so_info(struct pipe_stream_output_info *so_info,
               uint64_t outputs_written)
{
	uint64_t so_outputs = 0;
	uint8_t reverse_map[64] = {0};
	unsigned slot = 0;

	while (outputs_written)
		reverse_map[slot++] = u_bit_scan64(&outputs_written);

	for (unsigned i = 0; i < so_info->num_outputs; i++) {
		struct pipe_stream_output *output = &so_info->output[i];

		/* Map Gallium's condensed "slots" back to real VARYING_SLOT_* enums */
		output->register_index = reverse_map[output->register_index];

		so_outputs |= 1ull << output->register_index;
	}

	return so_outputs;
}

static struct panfrost_compiled_shader *
panfrost_new_variant_locked(
        struct panfrost_context *ctx,
        struct panfrost_uncompiled_shader *uncompiled,
        struct panfrost_shader_key *key)
{
        struct panfrost_compiled_shader *prog = panfrost_alloc_variant(uncompiled);

        *prog = (struct panfrost_compiled_shader) {
                .key = *key,
                .stream_output = uncompiled->stream_output,
        };

        panfrost_shader_get(ctx->base.screen, &ctx->shaders, &ctx->descs,
                            uncompiled, &ctx->base.debug, prog, 0);

        /* Fixup the stream out information */
        prog->so_mask =
                update_so_info(&prog->stream_output,
                               prog->info.outputs_written);

        prog->earlyzs = pan_earlyzs_analyze(&prog->info);

        return prog;
}

static void
panfrost_bind_shader_state(
        struct pipe_context *pctx,
        void *hwcso,
        enum pipe_shader_type type)
{
        struct panfrost_context *ctx = pan_context(pctx);
        ctx->uncompiled[type] = hwcso;
        ctx->prog[type] = NULL;

        ctx->dirty |= PAN_DIRTY_TLS_SIZE;
        ctx->dirty_shader[type] |= PAN_DIRTY_STAGE_SHADER;

        if (hwcso)
                panfrost_update_shader_variant(ctx, type);
}

void
panfrost_update_shader_variant(struct panfrost_context *ctx,
                               enum pipe_shader_type type)
{
        /* No shader variants for compute */
        if (type == PIPE_SHADER_COMPUTE)
                return;

        /* We need linking information, defer this */
        if (type == PIPE_SHADER_FRAGMENT && !ctx->uncompiled[PIPE_SHADER_VERTEX])
                return;

        /* Also defer, happens with GALLIUM_HUD */
        if (!ctx->uncompiled[type])
                return;

        /* Match the appropriate variant */
        struct panfrost_uncompiled_shader *uncompiled = ctx->uncompiled[type];
        struct panfrost_compiled_shader *compiled = NULL;

        simple_mtx_lock(&uncompiled->lock);

        struct panfrost_shader_key key = { 0 };
        panfrost_build_key(ctx, &key, uncompiled->nir);

        util_dynarray_foreach(&uncompiled->variants, struct panfrost_compiled_shader, so) {
                if (memcmp(&key, &so->key, sizeof(key)) == 0) {
                        compiled = so;
                        break;
                }
        }

        if (compiled == NULL)
                compiled = panfrost_new_variant_locked(ctx, uncompiled, &key);

        ctx->prog[type] = compiled;

        /* TODO: it would be more efficient to release the lock before
         * compiling instead of after, but that can race if thread A compiles a
         * variant while thread B searches for that same variant */
        simple_mtx_unlock(&uncompiled->lock);
}

static void
panfrost_bind_vs_state(struct pipe_context *pctx, void *hwcso)
{
        panfrost_bind_shader_state(pctx, hwcso, PIPE_SHADER_VERTEX);

        /* Fragment shaders are linked with vertex shaders */
        struct panfrost_context *ctx = pan_context(pctx);
        panfrost_update_shader_variant(ctx, PIPE_SHADER_FRAGMENT);
}

static void
panfrost_bind_fs_state(struct pipe_context *pctx, void *hwcso)
{
        panfrost_bind_shader_state(pctx, hwcso, PIPE_SHADER_FRAGMENT);
}

static void *
panfrost_create_shader_state(
        struct pipe_context *pctx,
        const struct pipe_shader_state *cso)
{
        nir_shader *nir = (cso->type == PIPE_SHADER_IR_TGSI) ?
                          tgsi_to_nir(cso->tokens, pctx->screen, false) :
                          cso->ir.nir;

        struct panfrost_uncompiled_shader *so = panfrost_alloc_shader(nir);

        /* The driver gets ownership of the nir_shader for graphics. The NIR is
         * ralloc'd. Free the NIR when we free the uncompiled shader.
         */
        ralloc_steal(so, nir);

        so->stream_output = cso->stream_output;
        so->nir = nir;

        /* Fix linkage early */
        if (so->nir->info.stage == MESA_SHADER_VERTEX) {
                so->fixed_varying_mask =
                        (so->nir->info.outputs_written & BITFIELD_MASK(VARYING_SLOT_VAR0)) &
                        ~VARYING_BIT_POS & ~VARYING_BIT_PSIZ;
        }

        /* If this shader uses transform feedback, compile the transform
         * feedback program. This is a special shader variant.
         */
        struct panfrost_context *ctx = pan_context(pctx);

        if (so->nir->xfb_info) {
                nir_shader *xfb = nir_shader_clone(NULL, so->nir);
                xfb->info.name = ralloc_asprintf(xfb, "%s@xfb", xfb->info.name);
                xfb->info.internal = true;

                so->xfb = calloc(1, sizeof(struct panfrost_compiled_shader));
                so->xfb->key.vs_is_xfb = true;

                panfrost_shader_get(ctx->base.screen, &ctx->shaders, &ctx->descs,
                                    so, &ctx->base.debug, so->xfb, 0);

                /* Since transform feedback is handled via the transform
                 * feedback program, the original program no longer uses XFB
                 */
                nir->info.has_transform_feedback_varyings = false;
        }

        /* Compile the program. We don't use vertex shader keys, so there will
         * be no further vertex shader variants. We do have fragment shader
         * keys, but we can still compile with a default key that will work most
         * of the time.
         */
        struct panfrost_shader_key key = { 0 };

        /* gl_FragColor lowering needs the number of colour buffers on desktop
         * GL, where it acts as an implicit broadcast to all colour buffers.
         *
         * However, gl_FragColor is a legacy feature, so assume that if
         * gl_FragColor is used, there is only a single render target. The
         * implicit broadcast is neither especially useful nor required by GLES.
         */
        if (so->nir->info.stage == MESA_SHADER_FRAGMENT &&
            so->nir->info.outputs_written & BITFIELD_BIT(FRAG_RESULT_COLOR)) {

                key.fs.nr_cbufs_for_fragcolor = 1;
        }

        /* Creating a CSO is single-threaded, so it's ok to use the
         * locked function without explicitly taking the lock. Creating a
         * default variant acts as a precompile.
         */
        panfrost_new_variant_locked(ctx, so, &key);

        return so;
}

static void
panfrost_delete_shader_state(struct pipe_context *pctx, void *so)
{
        struct panfrost_uncompiled_shader *cso = (struct panfrost_uncompiled_shader *) so;

        util_dynarray_foreach(&cso->variants, struct panfrost_compiled_shader, so) {
                panfrost_bo_unreference(so->bin.bo);
                panfrost_bo_unreference(so->state.bo);
                panfrost_bo_unreference(so->linkage.bo);
        }

        if (cso->xfb) {
                panfrost_bo_unreference(cso->xfb->bin.bo);
                panfrost_bo_unreference(cso->xfb->state.bo);
                panfrost_bo_unreference(cso->xfb->linkage.bo);
                free(cso->xfb);
        }

        simple_mtx_destroy(&cso->lock);

        ralloc_free(so);
}

/*
 * Create a compute CSO. As compute kernels do not require variants, they are
 * precompiled, creating both the uncompiled and compiled shaders now.
 */
static void *
panfrost_create_compute_state(
        struct pipe_context *pctx,
        const struct pipe_compute_state *cso)
{
        struct panfrost_context *ctx = pan_context(pctx);
        struct panfrost_uncompiled_shader *so = panfrost_alloc_shader(cso->prog);
        struct panfrost_compiled_shader *v = panfrost_alloc_variant(so);
        memset(v, 0, sizeof *v);

        assert(cso->ir_type == PIPE_SHADER_IR_NIR && "TGSI kernels unsupported");

        panfrost_shader_get(pctx->screen, &ctx->shaders, &ctx->descs,
                            so, &ctx->base.debug, v, cso->req_local_mem);

        /* The NIR becomes invalid after this. For compute kernels, we never
         * need to access it again. Don't keep a dangling pointer around.
         */
        so->nir = NULL;

        return so;
}

static void
panfrost_bind_compute_state(struct pipe_context *pipe, void *cso)
{
        struct panfrost_context *ctx = pan_context(pipe);
        struct panfrost_uncompiled_shader *uncompiled = cso;

        ctx->uncompiled[PIPE_SHADER_COMPUTE] = uncompiled;

        ctx->prog[PIPE_SHADER_COMPUTE] =
                uncompiled ? util_dynarray_begin(&uncompiled->variants) : NULL;
}

void
panfrost_shader_context_init(struct pipe_context *pctx)
{
        pctx->create_vs_state = panfrost_create_shader_state;
        pctx->delete_vs_state = panfrost_delete_shader_state;
        pctx->bind_vs_state = panfrost_bind_vs_state;

        pctx->create_fs_state = panfrost_create_shader_state;
        pctx->delete_fs_state = panfrost_delete_shader_state;
        pctx->bind_fs_state = panfrost_bind_fs_state;

        pctx->create_compute_state = panfrost_create_compute_state;
        pctx->bind_compute_state = panfrost_bind_compute_state;
        pctx->delete_compute_state = panfrost_delete_shader_state;
}
