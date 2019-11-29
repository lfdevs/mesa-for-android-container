/*
 * Copyright (C) 2019 Collabora, Ltd.
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

#include <stdio.h>
#include "util/u_memory.h"
#include "pan_blend_shaders.h"
#include "pan_blending.h"
#include "pan_bo.h"

/* A given Gallium blend state can be encoded to the hardware in numerous,
 * dramatically divergent ways due to the interactions of blending with
 * framebuffer formats. Conceptually, there are two modes:
 *
 * - Fixed-function blending (for suitable framebuffer formats, suitable blend
 *   state, and suitable blend constant)
 *
 * - Blend shaders (for everything else)
 *
 * A given Gallium blend configuration will compile to exactly one
 * fixed-function blend state, if it compiles to any, although the constant
 * will vary across runs as that is tracked outside of the Gallium CSO.
 *
 * However, that same blend configuration will compile to many different blend
 * shaders, depending on the framebuffer formats active. The rationale is that
 * blend shaders override not just fixed-function blending but also
 * fixed-function format conversion. As such, each blend shader must be
 * hardcoded to a particular framebuffer format to correctly pack/unpack it. As
 * a concrete example, to the hardware there is no difference (!) between RG16F
 * and RG16UI -- both are simply 4-byte-per-pixel chunks. Thus both formats
 * require a blend shader (even with blending is totally disabled!), required
 * to do conversion as necessary (if necessary).
 *
 * All of this state is encapsulated in the panfrost_blend_state struct
 * (our subclass of pipe_blend_state).
 */

/* Given an initialized CSO and a particular framebuffer format, grab a
 * blend shader, generating and compiling it if it doesn't exist
 * (lazy-loading in a way). This routine, when the cache hits, should
 * befast, suitable for calling every draw to avoid wacky dirty
 * tracking paths. If the cache hits, boom, done. */

static struct panfrost_blend_shader *
panfrost_get_blend_shader(
        struct panfrost_context *ctx,
        struct panfrost_blend_state *blend,
        enum pipe_format fmt,
        unsigned rt)
{
        /* Prevent NULL collision issues.. */
        assert(fmt != 0);

        /* Check the cache */
        struct hash_table_u64 *shaders = blend->rt[rt].shaders;

        struct panfrost_blend_shader *shader =
                _mesa_hash_table_u64_search(shaders, fmt);

        if (shader)
                return shader;

        /* Cache miss. Build one instead, cache it, and go */

        struct panfrost_blend_shader generated =
                panfrost_compile_blend_shader(ctx, &blend->base, fmt);

        shader = mem_dup(&generated, sizeof(generated));
        _mesa_hash_table_u64_insert(shaders, fmt, shader);
        return  shader;
}

/* Create a blend CSO. Essentially, try to compile a fixed-function
 * expression and initialize blend shaders */

static void *
panfrost_create_blend_state(struct pipe_context *pipe,
                            const struct pipe_blend_state *blend)
{
        struct panfrost_context *ctx = pan_context(pipe);
        struct panfrost_blend_state *so = rzalloc(ctx, struct panfrost_blend_state);
        so->base = *blend;

        /* TODO: The following features are not yet implemented */
        assert(!blend->logicop_enable);
        assert(!blend->alpha_to_coverage);
        assert(!blend->alpha_to_one);

        for (unsigned c = 0; c < PIPE_MAX_COLOR_BUFS; ++c) {
                struct panfrost_blend_rt *rt = &so->rt[c];

                /* There are two paths. First, we would like to try a
                 * fixed-function if we can */

                /* Without indep blending, the first RT settings replicate */

                unsigned g =
                        blend->independent_blend_enable ? c : 0;

                rt->has_fixed_function =
                        panfrost_make_fixed_blend_mode(
                                &blend->rt[g],
                                &rt->equation,
                                &rt->constant_mask,
                                blend->rt[g].colormask);

                /* Regardless if that works, we also need to initialize
                 * the blend shaders */

                rt->shaders = _mesa_hash_table_u64_create(so);
        }

        return so;
}

static void
panfrost_bind_blend_state(struct pipe_context *pipe,
                          void *cso)
{
        struct panfrost_context *ctx = pan_context(pipe);
        struct panfrost_screen *screen = pan_screen(ctx->base.screen);
        struct pipe_blend_state *blend = (struct pipe_blend_state *) cso;
        struct panfrost_blend_state *pblend = (struct panfrost_blend_state *) cso;
        ctx->blend = pblend;

        if (!blend)
                return;

        if (screen->require_sfbd) {
                SET_BIT(ctx->fragment_shader_core.unknown2_4, MALI_NO_DITHER, !blend->dither);
        }

        /* Shader itself is not dirty, but the shader core is */
        ctx->dirty |= PAN_DIRTY_FS;
}

static void
panfrost_delete_blend_shader(struct hash_entry *entry)
{
        struct panfrost_blend_shader *shader = (struct panfrost_blend_shader *)entry->data;
        free(shader->buffer);
        free(shader);
}

static void
panfrost_delete_blend_state(struct pipe_context *pipe,
                            void *cso)
{
        struct panfrost_blend_state *blend = (struct panfrost_blend_state *) cso;

        for (unsigned c = 0; c < 4; ++c) {
                struct panfrost_blend_rt *rt = &blend->rt[c];
                _mesa_hash_table_u64_clear(rt->shaders, panfrost_delete_blend_shader);
        }
        ralloc_free(blend);
}

static void
panfrost_set_blend_color(struct pipe_context *pipe,
                         const struct pipe_blend_color *blend_color)
{
        struct panfrost_context *ctx = pan_context(pipe);

        if (blend_color)
                ctx->blend_color = *blend_color;
}

/* Given a vec4 of constants, reduce it to just a single constant according to
 * the mask (if we can) */

static bool
panfrost_blend_constant(float *out, float *in, unsigned mask)
{
        /* If there is no components used, it automatically works. Do set a
         * dummy constant just to avoid reading uninitialized memory. */

        if (!mask) {
                *out = 0.0;
                return true;
        }

        /* Find some starter mask */
        unsigned first = ffs(mask) - 1;
        float cons = in[first];
        mask ^= (1 << first);

        /* Ensure the rest are equal */
        while (mask) {
                unsigned i = u_bit_scan(&mask);

                if (in[i] != cons) {
                        *out = 0.0;
                        return false;
                }
        }

        /* Otherwise, we're good to go */
        *out = cons;
        return true;
}

/* Create a final blend given the context */

struct panfrost_blend_final
panfrost_get_blend_for_context(struct panfrost_context *ctx, unsigned rti)
{
        struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);

        /* Grab the format, falling back gracefully if called invalidly (which
         * has to happen for no-color-attachment FBOs, for instance)  */
        struct pipe_framebuffer_state *fb = &ctx->pipe_framebuffer;
        enum pipe_format fmt = PIPE_FORMAT_R8G8B8A8_UNORM;

        if ((fb->nr_cbufs > rti) && fb->cbufs[rti])
                fmt = fb->cbufs[rti]->format;

        /* Grab the blend state */
        struct panfrost_blend_state *blend = ctx->blend;
        assert(blend);

        struct panfrost_blend_rt *rt = &blend->rt[rti];

        struct panfrost_blend_final final;

        /* First, we'll try a fixed function path */
        if (rt->has_fixed_function && panfrost_can_fixed_blend(fmt)) {
                if (panfrost_blend_constant(
                            &final.equation.constant,
                            ctx->blend_color.color,
                            rt->constant_mask)) {
                        /* There's an equation and suitable constant, so we're good to go */
                        final.is_shader = false;
                        final.equation.equation = &rt->equation;

                        final.no_blending =
                                (rt->equation.rgb_mode == 0x122) &&
                                (rt->equation.alpha_mode == 0x122) &&
                                (rt->equation.color_mask == 0xf);

                        return final;
                }
        }

        /* Otherwise, we need to grab a shader */
        struct panfrost_blend_shader *shader = panfrost_get_blend_shader(ctx, blend, fmt, rti);
        final.is_shader = true;
        final.no_blending = false;
        final.shader.work_count = shader->work_count;
        final.shader.first_tag = shader->first_tag;

        /* Upload the shader */
        final.shader.bo = panfrost_batch_create_bo(batch, shader->size,
                                                   PAN_BO_EXECUTE,
                                                   PAN_BO_ACCESS_PRIVATE |
                                                   PAN_BO_ACCESS_READ |
                                                   PAN_BO_ACCESS_VERTEX_TILER |
                                                   PAN_BO_ACCESS_FRAGMENT);
        memcpy(final.shader.bo->cpu, shader->buffer, shader->size);

        if (shader->patch_index) {
                /* We have to specialize the blend shader to use constants, so
                 * patch in the current constants */

                float *patch = (float *) (final.shader.bo->cpu + shader->patch_index);
                memcpy(patch, ctx->blend_color.color, sizeof(float) * 4);
        }

        return final;
}

void
panfrost_blend_context_init(struct pipe_context *pipe)
{
        pipe->create_blend_state = panfrost_create_blend_state;
        pipe->bind_blend_state   = panfrost_bind_blend_state;
        pipe->delete_blend_state = panfrost_delete_blend_state;

        pipe->set_blend_color = panfrost_set_blend_color;
}
