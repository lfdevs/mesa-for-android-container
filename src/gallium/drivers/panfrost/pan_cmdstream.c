/*
 * Copyright (C) 2018 Alyssa Rosenzweig
 * Copyright (C) 2020 Collabora Ltd.
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
 */

#include "util/macros.h"
#include "util/u_prim.h"
#include "util/u_vbuf.h"

#include "panfrost-quirks.h"

#include "pan_pool.h"
#include "pan_bo.h"
#include "pan_cmdstream.h"
#include "pan_context.h"
#include "pan_job.h"

/* If a BO is accessed for a particular shader stage, will it be in the primary
 * batch (vertex/tiler) or the secondary batch (fragment)? Anything but
 * fragment will be primary, e.g. compute jobs will be considered
 * "vertex/tiler" by analogy */

static inline uint32_t
panfrost_bo_access_for_stage(enum pipe_shader_type stage)
{
        assert(stage == PIPE_SHADER_FRAGMENT ||
               stage == PIPE_SHADER_VERTEX ||
               stage == PIPE_SHADER_COMPUTE);

        return stage == PIPE_SHADER_FRAGMENT ?
               PAN_BO_ACCESS_FRAGMENT :
               PAN_BO_ACCESS_VERTEX_TILER;
}

static void
panfrost_vt_emit_shared_memory(struct panfrost_context *ctx,
                               struct mali_vertex_tiler_postfix *postfix)
{
        struct panfrost_device *dev = pan_device(ctx->base.screen);
        struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);

        unsigned shift = panfrost_get_stack_shift(batch->stack_size);
        struct mali_shared_memory shared = {
                .stack_shift = shift,
                .scratchpad = panfrost_batch_get_scratchpad(batch, shift, dev->thread_tls_alloc, dev->core_count)->gpu,
                .shared_workgroup_count = ~0,
        };
        postfix->shared_memory = panfrost_pool_upload(&batch->pool, &shared, sizeof(shared));
}

static void
panfrost_vt_attach_framebuffer(struct panfrost_context *ctx,
                               struct mali_vertex_tiler_postfix *postfix)
{
        struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);
        postfix->shared_memory = panfrost_batch_reserve_framebuffer(batch);
}

static void
panfrost_vt_update_rasterizer(struct panfrost_context *ctx,
                              struct mali_vertex_tiler_prefix *prefix,
                              struct mali_vertex_tiler_postfix *postfix)
{
        struct panfrost_rasterizer *rasterizer = ctx->rasterizer;

        postfix->gl_enables |= 0x7;
        SET_BIT(postfix->gl_enables, MALI_FRONT_CCW_TOP,
                rasterizer && rasterizer->base.front_ccw);
        SET_BIT(postfix->gl_enables, MALI_CULL_FACE_FRONT,
                rasterizer && (rasterizer->base.cull_face & PIPE_FACE_FRONT));
        SET_BIT(postfix->gl_enables, MALI_CULL_FACE_BACK,
                rasterizer && (rasterizer->base.cull_face & PIPE_FACE_BACK));
        SET_BIT(prefix->unknown_draw, MALI_DRAW_FLATSHADE_FIRST,
                rasterizer && rasterizer->base.flatshade_first);
}

void
panfrost_vt_update_primitive_size(struct panfrost_context *ctx,
                                  struct mali_vertex_tiler_prefix *prefix,
                                  union midgard_primitive_size *primitive_size)
{
        struct panfrost_rasterizer *rasterizer = ctx->rasterizer;

        if (!panfrost_writes_point_size(ctx)) {
                bool points = prefix->draw_mode == MALI_POINTS;
                float val = 0.0f;

                if (rasterizer)
                        val = points ?
                              rasterizer->base.point_size :
                              rasterizer->base.line_width;

                primitive_size->constant = val;
        }
}

static void
panfrost_vt_update_occlusion_query(struct panfrost_context *ctx,
                                   struct mali_vertex_tiler_postfix *postfix)
{
        SET_BIT(postfix->gl_enables, MALI_OCCLUSION_QUERY, ctx->occlusion_query);
        if (ctx->occlusion_query) {
                postfix->occlusion_counter = ctx->occlusion_query->bo->gpu;
                panfrost_batch_add_bo(ctx->batch, ctx->occlusion_query->bo,
                                      PAN_BO_ACCESS_SHARED |
                                      PAN_BO_ACCESS_RW |
                                      PAN_BO_ACCESS_FRAGMENT);
        } else {
                postfix->occlusion_counter = 0;
        }
}

void
panfrost_vt_init(struct panfrost_context *ctx,
                 enum pipe_shader_type stage,
                 struct mali_vertex_tiler_prefix *prefix,
                 struct mali_vertex_tiler_postfix *postfix)
{
        struct panfrost_device *device = pan_device(ctx->base.screen);

        if (!ctx->shader[stage])
                return;

        memset(prefix, 0, sizeof(*prefix));
        memset(postfix, 0, sizeof(*postfix));

        if (device->quirks & IS_BIFROST) {
                postfix->gl_enables = 0x2;
                panfrost_vt_emit_shared_memory(ctx, postfix);
        } else {
                postfix->gl_enables = 0x6;
                panfrost_vt_attach_framebuffer(ctx, postfix);
        }

        if (stage == PIPE_SHADER_FRAGMENT) {
                panfrost_vt_update_occlusion_query(ctx, postfix);
                panfrost_vt_update_rasterizer(ctx, prefix, postfix);
        }
}

static unsigned
panfrost_translate_index_size(unsigned size)
{
        switch (size) {
        case 1:
                return MALI_DRAW_INDEXED_UINT8;

        case 2:
                return MALI_DRAW_INDEXED_UINT16;

        case 4:
                return MALI_DRAW_INDEXED_UINT32;

        default:
                unreachable("Invalid index size");
        }
}

/* Gets a GPU address for the associated index buffer. Only gauranteed to be
 * good for the duration of the draw (transient), could last longer. Also get
 * the bounds on the index buffer for the range accessed by the draw. We do
 * these operations together because there are natural optimizations which
 * require them to be together. */

static mali_ptr
panfrost_get_index_buffer_bounded(struct panfrost_context *ctx,
                                  const struct pipe_draw_info *info,
                                  unsigned *min_index, unsigned *max_index)
{
        struct panfrost_resource *rsrc = pan_resource(info->index.resource);
        struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);
        off_t offset = info->start * info->index_size;
        bool needs_indices = true;
        mali_ptr out = 0;

        if (info->max_index != ~0u) {
                *min_index = info->min_index;
                *max_index = info->max_index;
                needs_indices = false;
        }

        if (!info->has_user_indices) {
                /* Only resources can be directly mapped */
                panfrost_batch_add_bo(batch, rsrc->bo,
                                      PAN_BO_ACCESS_SHARED |
                                      PAN_BO_ACCESS_READ |
                                      PAN_BO_ACCESS_VERTEX_TILER);
                out = rsrc->bo->gpu + offset;

                /* Check the cache */
                needs_indices = !panfrost_minmax_cache_get(rsrc->index_cache,
                                                           info->start,
                                                           info->count,
                                                           min_index,
                                                           max_index);
        } else {
                /* Otherwise, we need to upload to transient memory */
                const uint8_t *ibuf8 = (const uint8_t *) info->index.user;
                out = panfrost_pool_upload(&batch->pool, ibuf8 + offset,
                                                info->count *
                                                info->index_size);
        }

        if (needs_indices) {
                /* Fallback */
                u_vbuf_get_minmax_index(&ctx->base, info, min_index, max_index);

                if (!info->has_user_indices)
                        panfrost_minmax_cache_add(rsrc->index_cache,
                                                  info->start, info->count,
                                                  *min_index, *max_index);
        }

        return out;
}

void
panfrost_vt_set_draw_info(struct panfrost_context *ctx,
                          const struct pipe_draw_info *info,
                          enum mali_draw_mode draw_mode,
                          struct mali_vertex_tiler_postfix *vertex_postfix,
                          struct mali_vertex_tiler_prefix *tiler_prefix,
                          struct mali_vertex_tiler_postfix *tiler_postfix,
                          unsigned *vertex_count,
                          unsigned *padded_count)
{
        tiler_prefix->draw_mode = draw_mode;

        unsigned draw_flags = 0;

        if (panfrost_writes_point_size(ctx))
                draw_flags |= MALI_DRAW_VARYING_SIZE;

        if (info->primitive_restart)
                draw_flags |= MALI_DRAW_PRIMITIVE_RESTART_FIXED_INDEX;

        /* These doesn't make much sense */

        draw_flags |= 0x3000;

        if (info->index_size) {
                unsigned min_index = 0, max_index = 0;

                tiler_prefix->indices = panfrost_get_index_buffer_bounded(ctx,
                                                                       info,
                                                                       &min_index,
                                                                       &max_index);

                /* Use the corresponding values */
                *vertex_count = max_index - min_index + 1;
                tiler_postfix->offset_start = vertex_postfix->offset_start = min_index + info->index_bias;
                tiler_prefix->offset_bias_correction = -min_index;
                tiler_prefix->index_count = MALI_POSITIVE(info->count);
                draw_flags |= panfrost_translate_index_size(info->index_size);
        } else {
                tiler_prefix->indices = 0;
                *vertex_count = ctx->vertex_count;
                tiler_postfix->offset_start = vertex_postfix->offset_start = info->start;
                tiler_prefix->offset_bias_correction = 0;
                tiler_prefix->index_count = MALI_POSITIVE(ctx->vertex_count);
        }

        tiler_prefix->unknown_draw = draw_flags;

        /* Encode the padded vertex count */

        if (info->instance_count > 1) {
                *padded_count = panfrost_padded_vertex_count(*vertex_count);

                unsigned shift = __builtin_ctz(ctx->padded_count);
                unsigned k = ctx->padded_count >> (shift + 1);

                tiler_postfix->instance_shift = vertex_postfix->instance_shift = shift;
                tiler_postfix->instance_odd = vertex_postfix->instance_odd = k;
        } else {
                *padded_count = *vertex_count;

                /* Reset instancing state */
                tiler_postfix->instance_shift = vertex_postfix->instance_shift = 0;
                tiler_postfix->instance_odd = vertex_postfix->instance_odd = 0;
        }
}

static void
panfrost_shader_meta_init(struct panfrost_context *ctx,
                          enum pipe_shader_type st,
                          struct mali_shader_meta *meta)
{
        const struct panfrost_device *dev = pan_device(ctx->base.screen);
        struct panfrost_shader_state *ss = panfrost_get_shader_state(ctx, st);

        memset(meta, 0, sizeof(*meta));
        meta->shader = (ss->bo ? ss->bo->gpu : 0) | ss->first_tag;
        meta->attribute_count = ss->attribute_count;
        meta->varying_count = ss->varying_count;
        meta->texture_count = ctx->sampler_view_count[st];
        meta->sampler_count = ctx->sampler_count[st];

        if (dev->quirks & IS_BIFROST) {
                if (st == PIPE_SHADER_VERTEX)
                        meta->bifrost1.unk1 = 0x800000;
                else {
                        /* First clause ATEST |= 0x4000000.
                         * Less than 32 regs |= 0x200 */
                        meta->bifrost1.unk1 = 0x950020;
                }

                meta->bifrost1.uniform_buffer_count = panfrost_ubo_count(ctx, st);
                if (st == PIPE_SHADER_VERTEX)
                        meta->bifrost2.preload_regs = 0xC0;
                else {
                        meta->bifrost2.preload_regs = 0x1;
                        SET_BIT(meta->bifrost2.preload_regs, 0x10, ss->reads_frag_coord);
                }

                meta->bifrost2.uniform_count = MIN2(ss->uniform_count,
                                                    ss->uniform_cutoff);
        } else {
                meta->midgard1.uniform_count = MIN2(ss->uniform_count,
                                                    ss->uniform_cutoff);
                meta->midgard1.work_count = ss->work_reg_count;

                /* TODO: This is not conformant on ES3 */
                meta->midgard1.flags_hi = MALI_SUPPRESS_INF_NAN;

                meta->midgard1.flags_lo = 0x20;
                meta->midgard1.uniform_buffer_count = panfrost_ubo_count(ctx, st);

                SET_BIT(meta->midgard1.flags_lo, MALI_WRITES_GLOBAL, ss->writes_global);
        }
}

static unsigned
panfrost_translate_compare_func(enum pipe_compare_func in)
{
        switch (in) {
        case PIPE_FUNC_NEVER:
                return MALI_FUNC_NEVER;

        case PIPE_FUNC_LESS:
                return MALI_FUNC_LESS;

        case PIPE_FUNC_EQUAL:
                return MALI_FUNC_EQUAL;

        case PIPE_FUNC_LEQUAL:
                return MALI_FUNC_LEQUAL;

        case PIPE_FUNC_GREATER:
                return MALI_FUNC_GREATER;

        case PIPE_FUNC_NOTEQUAL:
                return MALI_FUNC_NOTEQUAL;

        case PIPE_FUNC_GEQUAL:
                return MALI_FUNC_GEQUAL;

        case PIPE_FUNC_ALWAYS:
                return MALI_FUNC_ALWAYS;

        default:
                unreachable("Invalid func");
        }
}

static unsigned
panfrost_translate_stencil_op(enum pipe_stencil_op in)
{
        switch (in) {
        case PIPE_STENCIL_OP_KEEP:
                return MALI_STENCIL_KEEP;

        case PIPE_STENCIL_OP_ZERO:
                return MALI_STENCIL_ZERO;

        case PIPE_STENCIL_OP_REPLACE:
               return MALI_STENCIL_REPLACE;

        case PIPE_STENCIL_OP_INCR:
                return MALI_STENCIL_INCR;

        case PIPE_STENCIL_OP_DECR:
                return MALI_STENCIL_DECR;

        case PIPE_STENCIL_OP_INCR_WRAP:
                return MALI_STENCIL_INCR_WRAP;

        case PIPE_STENCIL_OP_DECR_WRAP:
                return MALI_STENCIL_DECR_WRAP;

        case PIPE_STENCIL_OP_INVERT:
                return MALI_STENCIL_INVERT;

        default:
                unreachable("Invalid stencil op");
        }
}

static unsigned
translate_tex_wrap(enum pipe_tex_wrap w)
{
        switch (w) {
        case PIPE_TEX_WRAP_REPEAT:
                return MALI_WRAP_REPEAT;

        case PIPE_TEX_WRAP_CLAMP:
                return MALI_WRAP_CLAMP;

        case PIPE_TEX_WRAP_CLAMP_TO_EDGE:
                return MALI_WRAP_CLAMP_TO_EDGE;

        case PIPE_TEX_WRAP_CLAMP_TO_BORDER:
                return MALI_WRAP_CLAMP_TO_BORDER;

        case PIPE_TEX_WRAP_MIRROR_REPEAT:
                return MALI_WRAP_MIRRORED_REPEAT;

        case PIPE_TEX_WRAP_MIRROR_CLAMP:
                return MALI_WRAP_MIRRORED_CLAMP;

        case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE:
                return MALI_WRAP_MIRRORED_CLAMP_TO_EDGE;

        case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER:
                return MALI_WRAP_MIRRORED_CLAMP_TO_BORDER;

        default:
                unreachable("Invalid wrap");
        }
}

void panfrost_sampler_desc_init(const struct pipe_sampler_state *cso,
                                struct mali_sampler_descriptor *hw)
{
        unsigned func = panfrost_translate_compare_func(cso->compare_func);
        bool min_nearest = cso->min_img_filter == PIPE_TEX_FILTER_NEAREST;
        bool mag_nearest = cso->mag_img_filter == PIPE_TEX_FILTER_NEAREST;
        bool mip_linear  = cso->min_mip_filter == PIPE_TEX_MIPFILTER_LINEAR;
        unsigned min_filter = min_nearest ? MALI_SAMP_MIN_NEAREST : 0;
        unsigned mag_filter = mag_nearest ? MALI_SAMP_MAG_NEAREST : 0;
        unsigned mip_filter = mip_linear  ?
                              (MALI_SAMP_MIP_LINEAR_1 | MALI_SAMP_MIP_LINEAR_2) : 0;
        unsigned normalized = cso->normalized_coords ? MALI_SAMP_NORM_COORDS : 0;

        *hw = (struct mali_sampler_descriptor) {
                .filter_mode = min_filter | mag_filter | mip_filter |
                               normalized,
                .wrap_s = translate_tex_wrap(cso->wrap_s),
                .wrap_t = translate_tex_wrap(cso->wrap_t),
                .wrap_r = translate_tex_wrap(cso->wrap_r),
                .compare_func = cso->compare_mode ?
                        panfrost_flip_compare_func(func) :
                        MALI_FUNC_NEVER,
                .border_color = {
                        cso->border_color.f[0],
                        cso->border_color.f[1],
                        cso->border_color.f[2],
                        cso->border_color.f[3]
                },
                .min_lod = FIXED_16(cso->min_lod, false), /* clamp at 0 */
                .max_lod = FIXED_16(cso->max_lod, false),
                .lod_bias = FIXED_16(cso->lod_bias, true), /* can be negative */
                .seamless_cube_map = cso->seamless_cube_map,
        };

        /* If necessary, we disable mipmapping in the sampler descriptor by
         * clamping the LOD as tight as possible (from 0 to epsilon,
         * essentially -- remember these are fixed point numbers, so
         * epsilon=1/256) */

        if (cso->min_mip_filter == PIPE_TEX_MIPFILTER_NONE)
                hw->max_lod = hw->min_lod + 1;
}

void panfrost_sampler_desc_init_bifrost(const struct pipe_sampler_state *cso,
                                        struct bifrost_sampler_descriptor *hw)
{
        *hw = (struct bifrost_sampler_descriptor) {
                .unk1 = 0x1,
                .wrap_s = translate_tex_wrap(cso->wrap_s),
                .wrap_t = translate_tex_wrap(cso->wrap_t),
                .wrap_r = translate_tex_wrap(cso->wrap_r),
                .unk8 = 0x8,
                .min_filter = cso->min_img_filter == PIPE_TEX_FILTER_NEAREST,
                .norm_coords = cso->normalized_coords,
                .mip_filter = cso->min_mip_filter == PIPE_TEX_MIPFILTER_LINEAR,
                .mag_filter = cso->mag_img_filter == PIPE_TEX_FILTER_LINEAR,
                .min_lod = FIXED_16(cso->min_lod, false), /* clamp at 0 */
                .max_lod = FIXED_16(cso->max_lod, false),
        };

        /* If necessary, we disable mipmapping in the sampler descriptor by
         * clamping the LOD as tight as possible (from 0 to epsilon,
         * essentially -- remember these are fixed point numbers, so
         * epsilon=1/256) */

        if (cso->min_mip_filter == PIPE_TEX_MIPFILTER_NONE)
                hw->max_lod = hw->min_lod + 1;
}

static void
panfrost_make_stencil_state(const struct pipe_stencil_state *in,
                            struct mali_stencil_test *out)
{
        out->ref = 0; /* Gallium gets it from elsewhere */

        out->mask = in->valuemask;
        out->func = panfrost_translate_compare_func(in->func);
        out->sfail = panfrost_translate_stencil_op(in->fail_op);
        out->dpfail = panfrost_translate_stencil_op(in->zfail_op);
        out->dppass = panfrost_translate_stencil_op(in->zpass_op);
}

static void
panfrost_frag_meta_rasterizer_update(struct panfrost_context *ctx,
                                     struct mali_shader_meta *fragmeta)
{
        if (!ctx->rasterizer) {
                SET_BIT(fragmeta->unknown2_4, MALI_NO_MSAA, true);
                SET_BIT(fragmeta->unknown2_3, MALI_HAS_MSAA, false);
                fragmeta->depth_units = 0.0f;
                fragmeta->depth_factor = 0.0f;
                SET_BIT(fragmeta->unknown2_4, MALI_DEPTH_RANGE_A, false);
                SET_BIT(fragmeta->unknown2_4, MALI_DEPTH_RANGE_B, false);
                SET_BIT(fragmeta->unknown2_3, MALI_DEPTH_CLIP_NEAR, true);
                SET_BIT(fragmeta->unknown2_3, MALI_DEPTH_CLIP_FAR, true);
                return;
        }

        struct pipe_rasterizer_state *rast = &ctx->rasterizer->base;

        bool msaa = rast->multisample;

        /* TODO: Sample size */
        SET_BIT(fragmeta->unknown2_3, MALI_HAS_MSAA, msaa);
        SET_BIT(fragmeta->unknown2_4, MALI_NO_MSAA, !msaa);

        struct panfrost_shader_state *fs;
        fs = panfrost_get_shader_state(ctx, PIPE_SHADER_FRAGMENT);

        /* EXT_shader_framebuffer_fetch requires the shader to be run
         * per-sample when outputs are read. */
        bool per_sample = ctx->min_samples > 1 || fs->outputs_read;
        SET_BIT(fragmeta->unknown2_3, MALI_PER_SAMPLE, msaa && per_sample);

        fragmeta->depth_units = rast->offset_units * 2.0f;
        fragmeta->depth_factor = rast->offset_scale;

        /* XXX: Which bit is which? Does this maybe allow offseting not-tri? */

        SET_BIT(fragmeta->unknown2_4, MALI_DEPTH_RANGE_A, rast->offset_tri);
        SET_BIT(fragmeta->unknown2_4, MALI_DEPTH_RANGE_B, rast->offset_tri);

        SET_BIT(fragmeta->unknown2_3, MALI_DEPTH_CLIP_NEAR, rast->depth_clip_near);
        SET_BIT(fragmeta->unknown2_3, MALI_DEPTH_CLIP_FAR, rast->depth_clip_far);
}

static void
panfrost_frag_meta_zsa_update(struct panfrost_context *ctx,
                              struct mali_shader_meta *fragmeta)
{
        const struct pipe_depth_stencil_alpha_state *zsa = ctx->depth_stencil;
        int zfunc = PIPE_FUNC_ALWAYS;

        if (!zsa) {
                struct pipe_stencil_state default_stencil = {
                        .enabled = 0,
                        .func = PIPE_FUNC_ALWAYS,
                        .fail_op = MALI_STENCIL_KEEP,
                        .zfail_op = MALI_STENCIL_KEEP,
                        .zpass_op = MALI_STENCIL_KEEP,
                        .writemask = 0xFF,
                        .valuemask = 0xFF
                };

                panfrost_make_stencil_state(&default_stencil,
                                            &fragmeta->stencil_front);
                fragmeta->stencil_mask_front = default_stencil.writemask;
                fragmeta->stencil_back = fragmeta->stencil_front;
                fragmeta->stencil_mask_back = default_stencil.writemask;
                SET_BIT(fragmeta->unknown2_4, MALI_STENCIL_TEST, false);
                SET_BIT(fragmeta->unknown2_3, MALI_DEPTH_WRITEMASK, false);
        } else {
                SET_BIT(fragmeta->unknown2_4, MALI_STENCIL_TEST,
                        zsa->stencil[0].enabled);
                panfrost_make_stencil_state(&zsa->stencil[0],
                                            &fragmeta->stencil_front);
                fragmeta->stencil_mask_front = zsa->stencil[0].writemask;
                fragmeta->stencil_front.ref = ctx->stencil_ref.ref_value[0];

                /* If back-stencil is not enabled, use the front values */

                if (zsa->stencil[1].enabled) {
                        panfrost_make_stencil_state(&zsa->stencil[1],
                                                    &fragmeta->stencil_back);
                        fragmeta->stencil_mask_back = zsa->stencil[1].writemask;
                        fragmeta->stencil_back.ref = ctx->stencil_ref.ref_value[1];
                } else {
                        fragmeta->stencil_back = fragmeta->stencil_front;
                        fragmeta->stencil_mask_back = fragmeta->stencil_mask_front;
                        fragmeta->stencil_back.ref = fragmeta->stencil_front.ref;
                }

                if (zsa->depth.enabled)
                        zfunc = zsa->depth.func;

                /* Depth state (TODO: Refactor) */

                SET_BIT(fragmeta->unknown2_3, MALI_DEPTH_WRITEMASK,
                        zsa->depth.writemask);
        }

        fragmeta->unknown2_3 &= ~MALI_DEPTH_FUNC_MASK;
        fragmeta->unknown2_3 |= MALI_DEPTH_FUNC(panfrost_translate_compare_func(zfunc));
}

static bool
panfrost_fs_required(
                struct panfrost_shader_state *fs,
                struct panfrost_blend_final *blend,
                unsigned rt_count)
{
        /* If we generally have side effects */
        if (fs->fs_sidefx)
                return true;

        /* If colour is written we need to execute */
        for (unsigned i = 0; i < rt_count; ++i) {
                if (!blend[i].no_colour)
                        return true;
        }

        /* If depth is written and not implied we need to execute.
         * TODO: Predicate on Z/S writes being enabled */
        return (fs->writes_depth || fs->writes_stencil);
}

static void
panfrost_frag_meta_blend_update(struct panfrost_context *ctx,
                                struct mali_shader_meta *fragmeta,
                                void *rts)
{
        struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);
        const struct panfrost_device *dev = pan_device(ctx->base.screen);
        struct panfrost_shader_state *fs;
        fs = panfrost_get_shader_state(ctx, PIPE_SHADER_FRAGMENT);

        SET_BIT(fragmeta->unknown2_4, MALI_NO_DITHER,
                (dev->quirks & MIDGARD_SFBD) && ctx->blend &&
                !ctx->blend->base.dither);

        SET_BIT(fragmeta->unknown2_4, MALI_ALPHA_TO_COVERAGE,
                        ctx->blend->base.alpha_to_coverage);

        /* Get blending setup */
        unsigned rt_count = MAX2(ctx->pipe_framebuffer.nr_cbufs, 1);

        struct panfrost_blend_final blend[PIPE_MAX_COLOR_BUFS];
        unsigned shader_offset = 0;
        struct panfrost_bo *shader_bo = NULL;

        for (unsigned c = 0; c < rt_count; ++c)
                blend[c] = panfrost_get_blend_for_context(ctx, c, &shader_bo,
                                                          &shader_offset);

        /* Disable shader execution if we can */
        if (dev->quirks & MIDGARD_SHADERLESS
                        && !panfrost_fs_required(fs, blend, rt_count)) {
                fragmeta->shader = 0;
                fragmeta->attribute_count = 0;
                fragmeta->varying_count = 0;
                fragmeta->texture_count = 0;
                fragmeta->sampler_count = 0;

                /* This feature is not known to work on Bifrost */
                fragmeta->midgard1.work_count = 1;
                fragmeta->midgard1.uniform_count = 0;
                fragmeta->midgard1.uniform_buffer_count = 0;
        }

         /* If there is a blend shader, work registers are shared. We impose 8
          * work registers as a limit for blend shaders. Should be lower XXX */

        if (!(dev->quirks & IS_BIFROST)) {
                for (unsigned c = 0; c < rt_count; ++c) {
                        if (blend[c].is_shader) {
                                fragmeta->midgard1.work_count =
                                        MAX2(fragmeta->midgard1.work_count, 8);
                        }
                }
        }

        /* Even on MFBD, the shader descriptor gets blend shaders. It's *also*
         * copied to the blend_meta appended (by convention), but this is the
         * field actually read by the hardware. (Or maybe both are read...?).
         * Specify the last RTi with a blend shader. */

        fragmeta->blend.shader = 0;

        for (signed rt = (rt_count - 1); rt >= 0; --rt) {
                if (!blend[rt].is_shader)
                        continue;

                fragmeta->blend.shader = blend[rt].shader.gpu |
                                         blend[rt].shader.first_tag;
                break;
        }

        if (dev->quirks & MIDGARD_SFBD) {
                /* When only a single render target platform is used, the blend
                 * information is inside the shader meta itself. We additionally
                 * need to signal CAN_DISCARD for nontrivial blend modes (so
                 * we're able to read back the destination buffer) */

                SET_BIT(fragmeta->unknown2_3, MALI_HAS_BLEND_SHADER,
                        blend[0].is_shader);

                if (!blend[0].is_shader) {
                        fragmeta->blend.equation = *blend[0].equation.equation;
                        fragmeta->blend.constant = blend[0].equation.constant;
                }

                SET_BIT(fragmeta->unknown2_3, MALI_CAN_DISCARD,
                        !blend[0].no_blending || fs->can_discard); 

                batch->draws |= PIPE_CLEAR_COLOR0;
                return;
        }

        if (dev->quirks & IS_BIFROST) {
                bool no_blend = true;

                for (unsigned i = 0; i < rt_count; ++i)
                        no_blend &= (blend[i].no_blending | blend[i].no_colour);

                SET_BIT(fragmeta->bifrost1.unk1, MALI_BIFROST_EARLY_Z,
                        !fs->can_discard && !fs->writes_depth && no_blend);
        }

        /* Additional blend descriptor tacked on for jobs using MFBD */

        for (unsigned i = 0; i < rt_count; ++i) {
                unsigned flags = 0;

                if (ctx->pipe_framebuffer.nr_cbufs > i && !blend[i].no_colour) {
                        flags = 0x200;
                        batch->draws |= (PIPE_CLEAR_COLOR0 << i);

                        bool is_srgb = (ctx->pipe_framebuffer.nr_cbufs > i) &&
                                       (ctx->pipe_framebuffer.cbufs[i]) &&
                                       util_format_is_srgb(ctx->pipe_framebuffer.cbufs[i]->format);

                        SET_BIT(flags, MALI_BLEND_MRT_SHADER, blend[i].is_shader);
                        SET_BIT(flags, MALI_BLEND_LOAD_TIB, !blend[i].no_blending);
                        SET_BIT(flags, MALI_BLEND_SRGB, is_srgb);
                        SET_BIT(flags, MALI_BLEND_NO_DITHER, !ctx->blend->base.dither);
                }

                if (dev->quirks & IS_BIFROST) {
                        struct bifrost_blend_rt *brts = rts;

                        brts[i].flags = flags;

                        if (blend[i].is_shader) {
                                /* The blend shader's address needs to be at
                                 * the same top 32 bit as the fragment shader.
                                 * TODO: Ensure that's always the case.
                                 */
                                assert((blend[i].shader.gpu & (0xffffffffull << 32)) ==
                                       (fs->bo->gpu & (0xffffffffull << 32)));
                                brts[i].shader = blend[i].shader.gpu;
                                brts[i].unk2 = 0x0;
                        } else if (ctx->pipe_framebuffer.nr_cbufs > i) {
                                enum pipe_format format = ctx->pipe_framebuffer.cbufs[i]->format;
                                const struct util_format_description *format_desc;
                                format_desc = util_format_description(format);

                                brts[i].equation = *blend[i].equation.equation;

                                /* TODO: this is a bit more complicated */
                                brts[i].constant = blend[i].equation.constant;

                                brts[i].format = panfrost_format_to_bifrost_blend(format_desc);

                                /* 0x19 disables blending and forces REPLACE
                                 * mode (equivalent to rgb_mode = alpha_mode =
                                 * x122, colour mask = 0xF). 0x1a allows
                                 * blending. */
                                brts[i].unk2 = blend[i].no_blending ? 0x19 : 0x1a;

                                brts[i].shader_type = fs->blend_types[i];
                        } else {
                                /* Dummy attachment for depth-only */
                                brts[i].unk2 = 0x3;
                                brts[i].shader_type = fs->blend_types[i];
                        }
                } else {
                        struct midgard_blend_rt *mrts = rts;
                        mrts[i].flags = flags;

                        if (blend[i].is_shader) {
                                mrts[i].blend.shader = blend[i].shader.gpu | blend[i].shader.first_tag;
                        } else {
                                mrts[i].blend.equation = *blend[i].equation.equation;
                                mrts[i].blend.constant = blend[i].equation.constant;
                        }
                }
        }
}

static void
panfrost_frag_shader_meta_init(struct panfrost_context *ctx,
                               struct mali_shader_meta *fragmeta,
                               void *rts)
{
        const struct panfrost_device *dev = pan_device(ctx->base.screen);
        struct panfrost_shader_state *fs;

        fs = panfrost_get_shader_state(ctx, PIPE_SHADER_FRAGMENT);

        bool msaa = ctx->rasterizer && ctx->rasterizer->base.multisample;
        fragmeta->coverage_mask = (msaa ? ctx->sample_mask : ~0) & 0xF;

        fragmeta->unknown2_3 = MALI_DEPTH_FUNC(MALI_FUNC_ALWAYS) | 0x10;
        fragmeta->unknown2_4 = 0x4e0;

        /* unknown2_4 has 0x10 bit set on T6XX and T720. We don't know why this
         * is required (independent of 32-bit/64-bit descriptors), or why it's
         * not used on later GPU revisions. Otherwise, all shader jobs fault on
         * these earlier chips (perhaps this is a chicken bit of some kind).
         * More investigation is needed. */

        SET_BIT(fragmeta->unknown2_4, 0x10, dev->quirks & MIDGARD_SFBD);

        if (dev->quirks & IS_BIFROST) {
                /* TODO */
        } else {
                /* Depending on whether it's legal to in the given shader, we try to
                 * enable early-z testing. TODO: respect e-z force */

                SET_BIT(fragmeta->midgard1.flags_lo, MALI_EARLY_Z,
                        !fs->can_discard && !fs->writes_global &&
                        !fs->writes_depth && !fs->writes_stencil &&
                        !ctx->blend->base.alpha_to_coverage);

                /* Add the writes Z/S flags if needed. */
                SET_BIT(fragmeta->midgard1.flags_lo, MALI_WRITES_Z, fs->writes_depth);
                SET_BIT(fragmeta->midgard1.flags_hi, MALI_WRITES_S, fs->writes_stencil);

                /* Any time texturing is used, derivatives are implicitly calculated,
                 * so we need to enable helper invocations */

                SET_BIT(fragmeta->midgard1.flags_lo, MALI_HELPER_INVOCATIONS,
                        fs->helper_invocations);

                /* If discard is enabled, which bit we set to convey this
                 * depends on if depth/stencil is used for the draw or not.
                 * Just one of depth OR stencil is enough to trigger this. */

                const struct pipe_depth_stencil_alpha_state *zsa = ctx->depth_stencil;
                bool zs_enabled = fs->writes_depth || fs->writes_stencil;

                if (zsa) {
                        zs_enabled |= (zsa->depth.enabled && zsa->depth.func != PIPE_FUNC_ALWAYS);
                        zs_enabled |= zsa->stencil[0].enabled;
                }

                SET_BIT(fragmeta->midgard1.flags_lo, MALI_READS_TILEBUFFER,
                        fs->outputs_read || (!zs_enabled && fs->can_discard));
                SET_BIT(fragmeta->midgard1.flags_lo, MALI_READS_ZS, zs_enabled && fs->can_discard);
        }

        panfrost_frag_meta_rasterizer_update(ctx, fragmeta);
        panfrost_frag_meta_zsa_update(ctx, fragmeta);
        panfrost_frag_meta_blend_update(ctx, fragmeta, rts);
}

void
panfrost_emit_shader_meta(struct panfrost_batch *batch,
                          enum pipe_shader_type st,
                          struct mali_vertex_tiler_postfix *postfix)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_shader_state *ss = panfrost_get_shader_state(ctx, st);

        if (!ss) {
                postfix->shader = 0;
                return;
        }

        struct mali_shader_meta meta;

        panfrost_shader_meta_init(ctx, st, &meta);

        /* Add the shader BO to the batch. */
        panfrost_batch_add_bo(batch, ss->bo,
                              PAN_BO_ACCESS_PRIVATE |
                              PAN_BO_ACCESS_READ |
                              panfrost_bo_access_for_stage(st));

        mali_ptr shader_ptr;

        if (st == PIPE_SHADER_FRAGMENT) {
                struct panfrost_device *dev = pan_device(ctx->base.screen);
                unsigned rt_count = MAX2(ctx->pipe_framebuffer.nr_cbufs, 1);
                size_t desc_size = sizeof(meta);
                void *rts = NULL;
                struct panfrost_transfer xfer;
                unsigned rt_size;

                if (dev->quirks & MIDGARD_SFBD)
                        rt_size = 0;
                else if (dev->quirks & IS_BIFROST)
                        rt_size = sizeof(struct bifrost_blend_rt);
                else
                        rt_size = sizeof(struct midgard_blend_rt);

                desc_size += rt_size * rt_count;

                if (rt_size)
                        rts = rzalloc_size(ctx, rt_size * rt_count);

                panfrost_frag_shader_meta_init(ctx, &meta, rts);

                xfer = panfrost_pool_alloc(&batch->pool, desc_size);

                memcpy(xfer.cpu, &meta, sizeof(meta));
                memcpy(xfer.cpu + sizeof(meta), rts, rt_size * rt_count);

                if (rt_size)
                        ralloc_free(rts);

                shader_ptr = xfer.gpu;
        } else {
                shader_ptr = panfrost_pool_upload(&batch->pool, &meta,
                                                       sizeof(meta));
        }

        postfix->shader = shader_ptr;
}

static void
panfrost_mali_viewport_init(struct panfrost_context *ctx,
                            struct mali_viewport *mvp)
{
        const struct pipe_viewport_state *vp = &ctx->pipe_viewport;

        /* Clip bounds are encoded as floats. The viewport itself is encoded as
         * (somewhat) asymmetric ints. */

        const struct pipe_scissor_state *ss = &ctx->scissor;

        memset(mvp, 0, sizeof(*mvp));

        /* By default, do no viewport clipping, i.e. clip to (-inf, inf) in
         * each direction. Clipping to the viewport in theory should work, but
         * in practice causes issues when we're not explicitly trying to
         * scissor */

        *mvp = (struct mali_viewport) {
                .clip_minx = -INFINITY,
                .clip_miny = -INFINITY,
                .clip_maxx = INFINITY,
                .clip_maxy = INFINITY,
        };

        /* Always scissor to the viewport by default. */
        float vp_minx = (int) (vp->translate[0] - fabsf(vp->scale[0]));
        float vp_maxx = (int) (vp->translate[0] + fabsf(vp->scale[0]));

        float vp_miny = (int) (vp->translate[1] - fabsf(vp->scale[1]));
        float vp_maxy = (int) (vp->translate[1] + fabsf(vp->scale[1]));

        float minz = (vp->translate[2] - fabsf(vp->scale[2]));
        float maxz = (vp->translate[2] + fabsf(vp->scale[2]));

        /* Apply the scissor test */

        unsigned minx, miny, maxx, maxy;

        if (ss && ctx->rasterizer && ctx->rasterizer->base.scissor) {
                minx = MAX2(ss->minx, vp_minx);
                miny = MAX2(ss->miny, vp_miny);
                maxx = MIN2(ss->maxx, vp_maxx);
                maxy = MIN2(ss->maxy, vp_maxy);
        } else {
                minx = vp_minx;
                miny = vp_miny;
                maxx = vp_maxx;
                maxy = vp_maxy;
        }

        /* Hardware needs the min/max to be strictly ordered, so flip if we
         * need to. The viewport transformation in the vertex shader will
         * handle the negatives if we don't */

        if (miny > maxy) {
                unsigned temp = miny;
                miny = maxy;
                maxy = temp;
        }

        if (minx > maxx) {
                unsigned temp = minx;
                minx = maxx;
                maxx = temp;
        }

        if (minz > maxz) {
                float temp = minz;
                minz = maxz;
                maxz = temp;
        }

        /* Clamp to the framebuffer size as a last check */

        minx = MIN2(ctx->pipe_framebuffer.width, minx);
        maxx = MIN2(ctx->pipe_framebuffer.width, maxx);

        miny = MIN2(ctx->pipe_framebuffer.height, miny);
        maxy = MIN2(ctx->pipe_framebuffer.height, maxy);

        /* Upload */

        mvp->viewport0[0] = minx;
        mvp->viewport1[0] = MALI_POSITIVE(maxx);

        mvp->viewport0[1] = miny;
        mvp->viewport1[1] = MALI_POSITIVE(maxy);

        bool clip_near = true;
        bool clip_far = true;

        if (ctx->rasterizer) {
                clip_near = ctx->rasterizer->base.depth_clip_near;
                clip_far = ctx->rasterizer->base.depth_clip_far;
        }

        mvp->clip_minz = clip_near ? minz : -INFINITY;
        mvp->clip_maxz = clip_far ? maxz : INFINITY;
}

void
panfrost_emit_viewport(struct panfrost_batch *batch,
                       struct mali_vertex_tiler_postfix *tiler_postfix)
{
        struct panfrost_context *ctx = batch->ctx;
        struct mali_viewport mvp;

        panfrost_mali_viewport_init(batch->ctx,  &mvp);

        /* Update the job, unless we're doing wallpapering (whose lack of
         * scissor we can ignore, since if we "miss" a tile of wallpaper, it'll
         * just... be faster :) */

        if (!ctx->wallpaper_batch)
                panfrost_batch_union_scissor(batch, mvp.viewport0[0],
                                             mvp.viewport0[1],
                                             mvp.viewport1[0] + 1,
                                             mvp.viewport1[1] + 1);

        tiler_postfix->viewport = panfrost_pool_upload(&batch->pool, &mvp,
                                                            sizeof(mvp));
}

static mali_ptr
panfrost_map_constant_buffer_gpu(struct panfrost_batch *batch,
                                 enum pipe_shader_type st,
                                 struct panfrost_constant_buffer *buf,
                                 unsigned index)
{
        struct pipe_constant_buffer *cb = &buf->cb[index];
        struct panfrost_resource *rsrc = pan_resource(cb->buffer);

        if (rsrc) {
                panfrost_batch_add_bo(batch, rsrc->bo,
                                      PAN_BO_ACCESS_SHARED |
                                      PAN_BO_ACCESS_READ |
                                      panfrost_bo_access_for_stage(st));

                /* Alignment gauranteed by
                 * PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT */
                return rsrc->bo->gpu + cb->buffer_offset;
        } else if (cb->user_buffer) {
                return panfrost_pool_upload(&batch->pool,
                                                 cb->user_buffer +
                                                 cb->buffer_offset,
                                                 cb->buffer_size);
        } else {
                unreachable("No constant buffer");
        }
}

struct sysval_uniform {
        union {
                float f[4];
                int32_t i[4];
                uint32_t u[4];
                uint64_t du[2];
        };
};

static void
panfrost_upload_viewport_scale_sysval(struct panfrost_batch *batch,
                                      struct sysval_uniform *uniform)
{
        struct panfrost_context *ctx = batch->ctx;
        const struct pipe_viewport_state *vp = &ctx->pipe_viewport;

        uniform->f[0] = vp->scale[0];
        uniform->f[1] = vp->scale[1];
        uniform->f[2] = vp->scale[2];
}

static void
panfrost_upload_viewport_offset_sysval(struct panfrost_batch *batch,
                                       struct sysval_uniform *uniform)
{
        struct panfrost_context *ctx = batch->ctx;
        const struct pipe_viewport_state *vp = &ctx->pipe_viewport;

        uniform->f[0] = vp->translate[0];
        uniform->f[1] = vp->translate[1];
        uniform->f[2] = vp->translate[2];
}

static void panfrost_upload_txs_sysval(struct panfrost_batch *batch,
                                       enum pipe_shader_type st,
                                       unsigned int sysvalid,
                                       struct sysval_uniform *uniform)
{
        struct panfrost_context *ctx = batch->ctx;
        unsigned texidx = PAN_SYSVAL_ID_TO_TXS_TEX_IDX(sysvalid);
        unsigned dim = PAN_SYSVAL_ID_TO_TXS_DIM(sysvalid);
        bool is_array = PAN_SYSVAL_ID_TO_TXS_IS_ARRAY(sysvalid);
        struct pipe_sampler_view *tex = &ctx->sampler_views[st][texidx]->base;

        assert(dim);
        uniform->i[0] = u_minify(tex->texture->width0, tex->u.tex.first_level);

        if (dim > 1)
                uniform->i[1] = u_minify(tex->texture->height0,
                                         tex->u.tex.first_level);

        if (dim > 2)
                uniform->i[2] = u_minify(tex->texture->depth0,
                                         tex->u.tex.first_level);

        if (is_array)
                uniform->i[dim] = tex->texture->array_size;
}

static void
panfrost_upload_ssbo_sysval(struct panfrost_batch *batch,
                            enum pipe_shader_type st,
                            unsigned ssbo_id,
                            struct sysval_uniform *uniform)
{
        struct panfrost_context *ctx = batch->ctx;

        assert(ctx->ssbo_mask[st] & (1 << ssbo_id));
        struct pipe_shader_buffer sb = ctx->ssbo[st][ssbo_id];

        /* Compute address */
        struct panfrost_bo *bo = pan_resource(sb.buffer)->bo;

        panfrost_batch_add_bo(batch, bo,
                              PAN_BO_ACCESS_SHARED | PAN_BO_ACCESS_RW |
                              panfrost_bo_access_for_stage(st));

        /* Upload address and size as sysval */
        uniform->du[0] = bo->gpu + sb.buffer_offset;
        uniform->u[2] = sb.buffer_size;
}

static void
panfrost_upload_sampler_sysval(struct panfrost_batch *batch,
                               enum pipe_shader_type st,
                               unsigned samp_idx,
                               struct sysval_uniform *uniform)
{
        struct panfrost_context *ctx = batch->ctx;
        struct pipe_sampler_state *sampl = &ctx->samplers[st][samp_idx]->base;

        uniform->f[0] = sampl->min_lod;
        uniform->f[1] = sampl->max_lod;
        uniform->f[2] = sampl->lod_bias;

        /* Even without any errata, Midgard represents "no mipmapping" as
         * fixing the LOD with the clamps; keep behaviour consistent. c.f.
         * panfrost_create_sampler_state which also explains our choice of
         * epsilon value (again to keep behaviour consistent) */

        if (sampl->min_mip_filter == PIPE_TEX_MIPFILTER_NONE)
                uniform->f[1] = uniform->f[0] + (1.0/256.0);
}

static void
panfrost_upload_num_work_groups_sysval(struct panfrost_batch *batch,
                                       struct sysval_uniform *uniform)
{
        struct panfrost_context *ctx = batch->ctx;

        uniform->u[0] = ctx->compute_grid->grid[0];
        uniform->u[1] = ctx->compute_grid->grid[1];
        uniform->u[2] = ctx->compute_grid->grid[2];
}

static void
panfrost_upload_sysvals(struct panfrost_batch *batch, void *buf,
                        struct panfrost_shader_state *ss,
                        enum pipe_shader_type st)
{
        struct sysval_uniform *uniforms = (void *)buf;

        for (unsigned i = 0; i < ss->sysval_count; ++i) {
                int sysval = ss->sysval[i];

                switch (PAN_SYSVAL_TYPE(sysval)) {
                case PAN_SYSVAL_VIEWPORT_SCALE:
                        panfrost_upload_viewport_scale_sysval(batch,
                                                              &uniforms[i]);
                        break;
                case PAN_SYSVAL_VIEWPORT_OFFSET:
                        panfrost_upload_viewport_offset_sysval(batch,
                                                               &uniforms[i]);
                        break;
                case PAN_SYSVAL_TEXTURE_SIZE:
                        panfrost_upload_txs_sysval(batch, st,
                                                   PAN_SYSVAL_ID(sysval),
                                                   &uniforms[i]);
                        break;
                case PAN_SYSVAL_SSBO:
                        panfrost_upload_ssbo_sysval(batch, st,
                                                    PAN_SYSVAL_ID(sysval),
                                                    &uniforms[i]);
                        break;
                case PAN_SYSVAL_NUM_WORK_GROUPS:
                        panfrost_upload_num_work_groups_sysval(batch,
                                                               &uniforms[i]);
                        break;
                case PAN_SYSVAL_SAMPLER:
                        panfrost_upload_sampler_sysval(batch, st,
                                                       PAN_SYSVAL_ID(sysval),
                                                       &uniforms[i]);
                        break;
                default:
                        assert(0);
                }
        }
}

static const void *
panfrost_map_constant_buffer_cpu(struct panfrost_constant_buffer *buf,
                                 unsigned index)
{
        struct pipe_constant_buffer *cb = &buf->cb[index];
        struct panfrost_resource *rsrc = pan_resource(cb->buffer);

        if (rsrc)
                return rsrc->bo->cpu;
        else if (cb->user_buffer)
                return cb->user_buffer;
        else
                unreachable("No constant buffer");
}

void
panfrost_emit_const_buf(struct panfrost_batch *batch,
                        enum pipe_shader_type stage,
                        struct mali_vertex_tiler_postfix *postfix)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_shader_variants *all = ctx->shader[stage];

        if (!all)
                return;

        struct panfrost_constant_buffer *buf = &ctx->constant_buffer[stage];

        struct panfrost_shader_state *ss = &all->variants[all->active_variant];

        /* Uniforms are implicitly UBO #0 */
        bool has_uniforms = buf->enabled_mask & (1 << 0);

        /* Allocate room for the sysval and the uniforms */
        size_t sys_size = sizeof(float) * 4 * ss->sysval_count;
        size_t uniform_size = has_uniforms ? (buf->cb[0].buffer_size) : 0;
        size_t size = sys_size + uniform_size;
        struct panfrost_transfer transfer = panfrost_pool_alloc(&batch->pool,
                                                                        size);

        /* Upload sysvals requested by the shader */
        panfrost_upload_sysvals(batch, transfer.cpu, ss, stage);

        /* Upload uniforms */
        if (has_uniforms && uniform_size) {
                const void *cpu = panfrost_map_constant_buffer_cpu(buf, 0);
                memcpy(transfer.cpu + sys_size, cpu, uniform_size);
        }

        /* Next up, attach UBOs. UBO #0 is the uniforms we just
         * uploaded */

        unsigned ubo_count = panfrost_ubo_count(ctx, stage);
        assert(ubo_count >= 1);

        size_t sz = sizeof(uint64_t) * ubo_count;
        uint64_t ubos[PAN_MAX_CONST_BUFFERS];
        int uniform_count = ss->uniform_count;

        /* Upload uniforms as a UBO */
        ubos[0] = MALI_MAKE_UBO(2 + uniform_count, transfer.gpu);

        /* The rest are honest-to-goodness UBOs */

        for (unsigned ubo = 1; ubo < ubo_count; ++ubo) {
                size_t usz = buf->cb[ubo].buffer_size;
                bool enabled = buf->enabled_mask & (1 << ubo);
                bool empty = usz == 0;

                if (!enabled || empty) {
                        /* Stub out disabled UBOs to catch accesses */
                        ubos[ubo] = MALI_MAKE_UBO(0, 0xDEAD0000);
                        continue;
                }

                mali_ptr gpu = panfrost_map_constant_buffer_gpu(batch, stage,
                                                                buf, ubo);

                unsigned bytes_per_field = 16;
                unsigned aligned = ALIGN_POT(usz, bytes_per_field);
                ubos[ubo] = MALI_MAKE_UBO(aligned / bytes_per_field, gpu);
        }

        mali_ptr ubufs = panfrost_pool_upload(&batch->pool, ubos, sz);
        postfix->uniforms = transfer.gpu;
        postfix->uniform_buffers = ubufs;

        buf->dirty_mask = 0;
}

void
panfrost_emit_shared_memory(struct panfrost_batch *batch,
                            const struct pipe_grid_info *info,
                            struct midgard_payload_vertex_tiler *vtp)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_shader_variants *all = ctx->shader[PIPE_SHADER_COMPUTE];
        struct panfrost_shader_state *ss = &all->variants[all->active_variant];
        unsigned single_size = util_next_power_of_two(MAX2(ss->shared_size,
                                                           128));
        unsigned shared_size = single_size * info->grid[0] * info->grid[1] *
                               info->grid[2] * 4;
        struct panfrost_bo *bo = panfrost_batch_get_shared_memory(batch,
                                                                  shared_size,
                                                                  1);

        struct mali_shared_memory shared = {
                .shared_memory = bo->gpu,
                .shared_workgroup_count =
                        util_logbase2_ceil(info->grid[0]) +
                        util_logbase2_ceil(info->grid[1]) +
                        util_logbase2_ceil(info->grid[2]),
                .shared_unk1 = 0x2,
                .shared_shift = util_logbase2(single_size) - 1
        };

        vtp->postfix.shared_memory = panfrost_pool_upload(&batch->pool, &shared,
                                                               sizeof(shared));
}

static mali_ptr
panfrost_get_tex_desc(struct panfrost_batch *batch,
                      enum pipe_shader_type st,
                      struct panfrost_sampler_view *view)
{
        if (!view)
                return (mali_ptr) 0;

        struct pipe_sampler_view *pview = &view->base;
        struct panfrost_resource *rsrc = pan_resource(pview->texture);

        /* Add the BO to the job so it's retained until the job is done. */

        panfrost_batch_add_bo(batch, rsrc->bo,
                              PAN_BO_ACCESS_SHARED | PAN_BO_ACCESS_READ |
                              panfrost_bo_access_for_stage(st));

        panfrost_batch_add_bo(batch, view->bo,
                              PAN_BO_ACCESS_SHARED | PAN_BO_ACCESS_READ |
                              panfrost_bo_access_for_stage(st));

        return view->bo->gpu;
}

static void
panfrost_update_sampler_view(struct panfrost_sampler_view *view,
                             struct pipe_context *pctx)
{
        struct panfrost_resource *rsrc = pan_resource(view->base.texture);
        if (view->texture_bo != rsrc->bo->gpu ||
            view->layout != rsrc->layout) {
                panfrost_bo_unreference(view->bo);
                panfrost_create_sampler_view_bo(view, pctx, &rsrc->base);
        }
}

void
panfrost_emit_texture_descriptors(struct panfrost_batch *batch,
                                  enum pipe_shader_type stage,
                                  struct mali_vertex_tiler_postfix *postfix)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_device *device = pan_device(ctx->base.screen);

        if (!ctx->sampler_view_count[stage])
                return;

        if (device->quirks & IS_BIFROST) {
                struct bifrost_texture_descriptor *descriptors;

                descriptors = malloc(sizeof(struct bifrost_texture_descriptor) *
                                     ctx->sampler_view_count[stage]);

                for (int i = 0; i < ctx->sampler_view_count[stage]; ++i) {
                        struct panfrost_sampler_view *view = ctx->sampler_views[stage][i];
                        struct pipe_sampler_view *pview = &view->base;
                        struct panfrost_resource *rsrc = pan_resource(pview->texture);
                        panfrost_update_sampler_view(view, &ctx->base);

                        /* Add the BOs to the job so they are retained until the job is done. */

                        panfrost_batch_add_bo(batch, rsrc->bo,
                                              PAN_BO_ACCESS_SHARED | PAN_BO_ACCESS_READ |
                                              panfrost_bo_access_for_stage(stage));

                        panfrost_batch_add_bo(batch, view->bo,
                                              PAN_BO_ACCESS_SHARED | PAN_BO_ACCESS_READ |
                                              panfrost_bo_access_for_stage(stage));

                        memcpy(&descriptors[i], view->bifrost_descriptor, sizeof(*view->bifrost_descriptor));
                }

                postfix->textures = panfrost_pool_upload(&batch->pool,
                                                              descriptors,
                                                              sizeof(struct bifrost_texture_descriptor) *
                                                                      ctx->sampler_view_count[stage]);

                free(descriptors);
        } else {
                uint64_t trampolines[PIPE_MAX_SHADER_SAMPLER_VIEWS];

                for (int i = 0; i < ctx->sampler_view_count[stage]; ++i) {
                        struct panfrost_sampler_view *view = ctx->sampler_views[stage][i];

                        panfrost_update_sampler_view(view, &ctx->base);

                        trampolines[i] = panfrost_get_tex_desc(batch, stage, view);
                }

                postfix->textures = panfrost_pool_upload(&batch->pool,
                                                              trampolines,
                                                              sizeof(uint64_t) *
                                                              ctx->sampler_view_count[stage]);
        }
}

void
panfrost_emit_sampler_descriptors(struct panfrost_batch *batch,
                                  enum pipe_shader_type stage,
                                  struct mali_vertex_tiler_postfix *postfix)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_device *device = pan_device(ctx->base.screen);

        if (!ctx->sampler_count[stage])
                return;

        if (device->quirks & IS_BIFROST) {
                size_t desc_size = sizeof(struct bifrost_sampler_descriptor);
                size_t transfer_size = desc_size * ctx->sampler_count[stage];
                struct panfrost_transfer transfer = panfrost_pool_alloc(&batch->pool,
                                                                                transfer_size);
                struct bifrost_sampler_descriptor *desc = (struct bifrost_sampler_descriptor *)transfer.cpu;

                for (int i = 0; i < ctx->sampler_count[stage]; ++i)
                        desc[i] = ctx->samplers[stage][i]->bifrost_hw;

                postfix->sampler_descriptor = transfer.gpu;
        } else {
                size_t desc_size = sizeof(struct mali_sampler_descriptor);
                size_t transfer_size = desc_size * ctx->sampler_count[stage];
                struct panfrost_transfer transfer = panfrost_pool_alloc(&batch->pool,
                                                                                transfer_size);
                struct mali_sampler_descriptor *desc = (struct mali_sampler_descriptor *)transfer.cpu;

                for (int i = 0; i < ctx->sampler_count[stage]; ++i)
                        desc[i] = ctx->samplers[stage][i]->midgard_hw;

                postfix->sampler_descriptor = transfer.gpu;
        }
}

void
panfrost_emit_vertex_attr_meta(struct panfrost_batch *batch,
                               struct mali_vertex_tiler_postfix *vertex_postfix)
{
        struct panfrost_context *ctx = batch->ctx;

        if (!ctx->vertex)
                return;

        struct panfrost_vertex_state *so = ctx->vertex;

        panfrost_vertex_state_upd_attr_offs(ctx, vertex_postfix);
        vertex_postfix->attribute_meta = panfrost_pool_upload(&batch->pool, so->hw,
                                                               sizeof(*so->hw) *
                                                               PAN_MAX_ATTRIBUTE);
}

void
panfrost_emit_vertex_data(struct panfrost_batch *batch,
                          struct mali_vertex_tiler_postfix *vertex_postfix)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_vertex_state *so = ctx->vertex;

        /* Staged mali_attr, and index into them. i =/= k, depending on the
         * vertex buffer mask and instancing. Twice as much room is allocated,
         * for a worst case of NPOT_DIVIDEs which take up extra slot */
        union mali_attr attrs[PIPE_MAX_ATTRIBS * 2];
        unsigned k = 0;

        for (unsigned i = 0; i < so->num_elements; ++i) {
                /* We map a mali_attr to be 1:1 with the mali_attr_meta, which
                 * means duplicating some vertex buffers (who cares? aside from
                 * maybe some caching implications but I somehow doubt that
                 * matters) */

                struct pipe_vertex_element *elem = &so->pipe[i];
                unsigned vbi = elem->vertex_buffer_index;

                /* The exception to 1:1 mapping is that we can have multiple
                 * entries (NPOT divisors), so we fixup anyways */

                so->hw[i].index = k;

                if (!(ctx->vb_mask & (1 << vbi)))
                        continue;

                struct pipe_vertex_buffer *buf = &ctx->vertex_buffers[vbi];
                struct panfrost_resource *rsrc;

                rsrc = pan_resource(buf->buffer.resource);
                if (!rsrc)
                        continue;

                /* Align to 64 bytes by masking off the lower bits. This
                 * will be adjusted back when we fixup the src_offset in
                 * mali_attr_meta */

                mali_ptr raw_addr = rsrc->bo->gpu + buf->buffer_offset;
                mali_ptr addr = raw_addr & ~63;
                unsigned chopped_addr = raw_addr - addr;

                /* Add a dependency of the batch on the vertex buffer */
                panfrost_batch_add_bo(batch, rsrc->bo,
                                      PAN_BO_ACCESS_SHARED |
                                      PAN_BO_ACCESS_READ |
                                      PAN_BO_ACCESS_VERTEX_TILER);

                /* Set common fields */
                attrs[k].elements = addr;
                attrs[k].stride = buf->stride;

                /* Since we advanced the base pointer, we shrink the buffer
                 * size */
                attrs[k].size = rsrc->base.width0 - buf->buffer_offset;

                /* We need to add the extra size we masked off (for
                 * correctness) so the data doesn't get clamped away */
                attrs[k].size += chopped_addr;

                /* For non-instancing make sure we initialize */
                attrs[k].shift = attrs[k].extra_flags = 0;

                /* Instancing uses a dramatically different code path than
                 * linear, so dispatch for the actual emission now that the
                 * common code is finished */

                unsigned divisor = elem->instance_divisor;

                if (divisor && ctx->instance_count == 1) {
                        /* Silly corner case where there's a divisor(=1) but
                         * there's no legitimate instancing. So we want *every*
                         * attribute to be the same. So set stride to zero so
                         * we don't go anywhere. */

                        attrs[k].size = attrs[k].stride + chopped_addr;
                        attrs[k].stride = 0;
                        attrs[k++].elements |= MALI_ATTR_LINEAR;
                } else if (ctx->instance_count <= 1) {
                        /* Normal, non-instanced attributes */
                        attrs[k++].elements |= MALI_ATTR_LINEAR;
                } else {
                        unsigned instance_shift = vertex_postfix->instance_shift;
                        unsigned instance_odd = vertex_postfix->instance_odd;

                        k += panfrost_vertex_instanced(ctx->padded_count,
                                                       instance_shift,
                                                       instance_odd,
                                                       divisor, &attrs[k]);
                }
        }

        /* Add special gl_VertexID/gl_InstanceID buffers */

        panfrost_vertex_id(ctx->padded_count, &attrs[k]);
        so->hw[PAN_VERTEX_ID].index = k++;
        panfrost_instance_id(ctx->padded_count, &attrs[k]);
        so->hw[PAN_INSTANCE_ID].index = k++;

        /* Upload whatever we emitted and go */

        vertex_postfix->attributes = panfrost_pool_upload(&batch->pool, attrs,
                                                           k * sizeof(*attrs));
}

static mali_ptr
panfrost_emit_varyings(struct panfrost_batch *batch, union mali_attr *slot,
                       unsigned stride, unsigned count)
{
        /* Fill out the descriptor */
        slot->stride = stride;
        slot->size = stride * count;
        slot->shift = slot->extra_flags = 0;

        struct panfrost_transfer transfer = panfrost_pool_alloc(&batch->pool,
                                                                        slot->size);

        slot->elements = transfer.gpu | MALI_ATTR_LINEAR;

        return transfer.gpu;
}

static unsigned
panfrost_streamout_offset(unsigned stride, unsigned offset,
                        struct pipe_stream_output_target *target)
{
        return (target->buffer_offset + (offset * stride * 4)) & 63;
}

static void
panfrost_emit_streamout(struct panfrost_batch *batch, union mali_attr *slot,
                        unsigned stride, unsigned offset, unsigned count,
                        struct pipe_stream_output_target *target)
{
        /* Fill out the descriptor */
        slot->stride = stride * 4;
        slot->shift = slot->extra_flags = 0;

        unsigned max_size = target->buffer_size;
        unsigned expected_size = slot->stride * count;

        /* Grab the BO and bind it to the batch */
        struct panfrost_bo *bo = pan_resource(target->buffer)->bo;

        /* Varyings are WRITE from the perspective of the VERTEX but READ from
         * the perspective of the TILER and FRAGMENT.
         */
        panfrost_batch_add_bo(batch, bo,
                              PAN_BO_ACCESS_SHARED |
                              PAN_BO_ACCESS_RW |
                              PAN_BO_ACCESS_VERTEX_TILER |
                              PAN_BO_ACCESS_FRAGMENT);

        /* We will have an offset applied to get alignment */
        mali_ptr addr = bo->gpu + target->buffer_offset + (offset * slot->stride);
        slot->elements = (addr & ~63) | MALI_ATTR_LINEAR;
        slot->size = MIN2(max_size, expected_size) + (addr & 63);
}

static bool
has_point_coord(unsigned mask, gl_varying_slot loc)
{
        if ((loc >= VARYING_SLOT_TEX0) && (loc <= VARYING_SLOT_TEX7))
                return (mask & (1 << (loc - VARYING_SLOT_TEX0)));
        else if (loc == VARYING_SLOT_PNTC)
                return (mask & (1 << 8));
        else
                return false;
}

/* Helpers for manipulating stream out information so we can pack varyings
 * accordingly. Compute the src_offset for a given captured varying */

static struct pipe_stream_output *
pan_get_so(struct pipe_stream_output_info *info, gl_varying_slot loc)
{
        for (unsigned i = 0; i < info->num_outputs; ++i) {
                if (info->output[i].register_index == loc)
                        return &info->output[i];
        }

        unreachable("Varying not captured");
}

static unsigned
pan_varying_size(enum mali_format fmt)
{
        unsigned type = MALI_EXTRACT_TYPE(fmt);
        unsigned chan = MALI_EXTRACT_CHANNELS(fmt);
        unsigned bits = MALI_EXTRACT_BITS(fmt);
        unsigned bpc = 0;

        if (bits == MALI_CHANNEL_FLOAT) {
                /* No doubles */
                bool fp16 = (type == MALI_FORMAT_SINT);
                assert(fp16 || (type == MALI_FORMAT_UNORM));

                bpc = fp16 ? 2 : 4;
        } else {
                assert(type >= MALI_FORMAT_SNORM && type <= MALI_FORMAT_SINT);

                /* See the enums */
                bits = 1 << bits;
                assert(bits >= 8);
                bpc = bits / 8;
        }

        return bpc * chan;
}

/* Indices for named (non-XFB) varyings that are present. These are packed
 * tightly so they correspond to a bitfield present (P) indexed by (1 <<
 * PAN_VARY_*). This has the nice property that you can lookup the buffer index
 * of a given special field given a shift S by:
 *
 *      idx = popcount(P & ((1 << S) - 1))
 *
 * That is... look at all of the varyings that come earlier and count them, the
 * count is the new index since plus one. Likewise, the total number of special
 * buffers required is simply popcount(P)
 */

enum pan_special_varying {
        PAN_VARY_GENERAL = 0,
        PAN_VARY_POSITION = 1,
        PAN_VARY_PSIZ = 2,
        PAN_VARY_PNTCOORD = 3,
        PAN_VARY_FACE = 4,
        PAN_VARY_FRAGCOORD = 5,

        /* Keep last */
        PAN_VARY_MAX,
};

/* Given a varying, figure out which index it correpsonds to */

static inline unsigned
pan_varying_index(unsigned present, enum pan_special_varying v)
{
        unsigned mask = (1 << v) - 1;
        return util_bitcount(present & mask);
}

/* Get the base offset for XFB buffers, which by convention come after
 * everything else. Wrapper function for semantic reasons; by construction this
 * is just popcount. */

static inline unsigned
pan_xfb_base(unsigned present)
{
        return util_bitcount(present);
}

/* Computes the present mask for varyings so we can start emitting varying records */

static inline unsigned
pan_varying_present(
        struct panfrost_shader_state *vs,
        struct panfrost_shader_state *fs,
        unsigned quirks)
{
        /* At the moment we always emit general and position buffers. Not
         * strictly necessary but usually harmless */

        unsigned present = (1 << PAN_VARY_GENERAL) | (1 << PAN_VARY_POSITION);

        /* Enable special buffers by the shader info */

        if (vs->writes_point_size)
                present |= (1 << PAN_VARY_PSIZ);

        if (fs->reads_point_coord)
                present |= (1 << PAN_VARY_PNTCOORD);

        if (fs->reads_face)
                present |= (1 << PAN_VARY_FACE);

        if (fs->reads_frag_coord && !(quirks & IS_BIFROST))
                present |= (1 << PAN_VARY_FRAGCOORD);

        /* Also, if we have a point sprite, we need a point coord buffer */

        for (unsigned i = 0; i < fs->varying_count; i++)  {
                gl_varying_slot loc = fs->varyings_loc[i];

                if (has_point_coord(fs->point_sprite_mask, loc))
                        present |= (1 << PAN_VARY_PNTCOORD);
        }

        return present;
}

/* Emitters for varying records */

static struct mali_attr_meta
pan_emit_vary(unsigned present, enum pan_special_varying buf,
                unsigned quirks, enum mali_format format,
                unsigned offset)
{
        unsigned nr_channels = MALI_EXTRACT_CHANNELS(format);

        struct mali_attr_meta meta = {
                .index = pan_varying_index(present, buf),
                .unknown1 = quirks & IS_BIFROST ? 0x0 : 0x2,
                .swizzle = quirks & HAS_SWIZZLES ?
                        panfrost_get_default_swizzle(nr_channels) :
                        panfrost_bifrost_swizzle(nr_channels),
                .format = format,
                .src_offset = offset
        };

        return meta;
}

/* General varying that is unused */

static struct mali_attr_meta
pan_emit_vary_only(unsigned present, unsigned quirks)
{
        return pan_emit_vary(present, 0, quirks, MALI_VARYING_DISCARD, 0);
}

/* Special records */

static const enum mali_format pan_varying_formats[PAN_VARY_MAX] = {
        [PAN_VARY_POSITION]     = MALI_VARYING_POS,
        [PAN_VARY_PSIZ]         = MALI_R16F,
        [PAN_VARY_PNTCOORD]     = MALI_R16F,
        [PAN_VARY_FACE]         = MALI_R32I,
        [PAN_VARY_FRAGCOORD]    = MALI_RGBA32F
};

static struct mali_attr_meta
pan_emit_vary_special(unsigned present, enum pan_special_varying buf,
                unsigned quirks)
{
        assert(buf < PAN_VARY_MAX);
        return pan_emit_vary(present, buf, quirks, pan_varying_formats[buf], 0);
}

static enum mali_format
pan_xfb_format(enum mali_format format, unsigned nr)
{
        if (MALI_EXTRACT_BITS(format) == MALI_CHANNEL_FLOAT)
                return MALI_R32F | MALI_NR_CHANNELS(nr);
        else
                return MALI_EXTRACT_TYPE(format) | MALI_NR_CHANNELS(nr) | MALI_CHANNEL_32;
}

/* Transform feedback records. Note struct pipe_stream_output is (if packed as
 * a bitfield) 32-bit, smaller than a 64-bit pointer, so may as well pass by
 * value. */

static struct mali_attr_meta
pan_emit_vary_xfb(unsigned present,
                unsigned max_xfb,
                unsigned *streamout_offsets,
                unsigned quirks,
                enum mali_format format,
                struct pipe_stream_output o)
{
        /* Otherwise construct a record for it */
        struct mali_attr_meta meta = {
                /* XFB buffers come after everything else */
                .index = pan_xfb_base(present) + o.output_buffer,

                /* As usual unknown bit */
                .unknown1 = quirks & IS_BIFROST ? 0x0 : 0x2,

                /* Override swizzle with number of channels */
                .swizzle = quirks & HAS_SWIZZLES ?
                        panfrost_get_default_swizzle(o.num_components) :
                        panfrost_bifrost_swizzle(o.num_components),

                /* Override number of channels and precision to highp */
                .format = pan_xfb_format(format, o.num_components),

                /* Apply given offsets together */
                .src_offset = (o.dst_offset * 4) /* dwords */
                        + streamout_offsets[o.output_buffer]
        };

        return meta;
}

/* Determine if we should capture a varying for XFB. This requires actually
 * having a buffer for it. If we don't capture it, we'll fallback to a general
 * varying path (linked or unlinked, possibly discarding the write) */

static bool
panfrost_xfb_captured(struct panfrost_shader_state *xfb,
                unsigned loc, unsigned max_xfb)
{
        if (!(xfb->so_mask & (1ll << loc)))
                return false;

        struct pipe_stream_output *o = pan_get_so(&xfb->stream_output, loc);
        return o->output_buffer < max_xfb;
}

/* Higher-level wrapper around all of the above, classifying a varying into one
 * of the above types */

static struct mali_attr_meta
panfrost_emit_varying(
                struct panfrost_shader_state *stage,
                struct panfrost_shader_state *other,
                struct panfrost_shader_state *xfb,
                unsigned present,
                unsigned max_xfb,
                unsigned *streamout_offsets,
                unsigned quirks,
                unsigned *gen_offsets,
                enum mali_format *gen_formats,
                unsigned *gen_stride,
                unsigned idx,
                bool should_alloc,
                bool is_fragment)
{
        gl_varying_slot loc = stage->varyings_loc[idx];
        enum mali_format format = stage->varyings[idx];

        /* Override format to match linkage */
        if (!should_alloc && gen_formats[idx])
                format = gen_formats[idx];

        if (has_point_coord(stage->point_sprite_mask, loc)) {
                return pan_emit_vary_special(present, PAN_VARY_PNTCOORD, quirks);
        } else if (panfrost_xfb_captured(xfb, loc, max_xfb)) {
                struct pipe_stream_output *o = pan_get_so(&xfb->stream_output, loc);
                return pan_emit_vary_xfb(present, max_xfb, streamout_offsets, quirks, format, *o);
        } else if (loc == VARYING_SLOT_POS) {
                if (is_fragment)
                        return pan_emit_vary_special(present, PAN_VARY_FRAGCOORD, quirks);
                else
                        return pan_emit_vary_special(present, PAN_VARY_POSITION, quirks);
        } else if (loc == VARYING_SLOT_PSIZ) {
                return pan_emit_vary_special(present, PAN_VARY_PSIZ, quirks);
        } else if (loc == VARYING_SLOT_PNTC) {
                return pan_emit_vary_special(present, PAN_VARY_PNTCOORD, quirks);
        } else if (loc == VARYING_SLOT_FACE) {
                return pan_emit_vary_special(present, PAN_VARY_FACE, quirks);
        }

        /* We've exhausted special cases, so it's otherwise a general varying. Check if we're linked */
        signed other_idx = -1;

        for (unsigned j = 0; j < other->varying_count; ++j) {
                if (other->varyings_loc[j] == loc) {
                        other_idx = j;
                        break;
                }
        }

        if (other_idx < 0)
                return pan_emit_vary_only(present, quirks);

        unsigned offset = gen_offsets[other_idx];

        if (should_alloc) {
                /* We're linked, so allocate a space via a watermark allocation */
                enum mali_format alt = other->varyings[other_idx];

                /* Do interpolation at minimum precision */
                unsigned size_main = pan_varying_size(format);
                unsigned size_alt = pan_varying_size(alt);
                unsigned size = MIN2(size_main, size_alt);

                /* If a varying is marked for XFB but not actually captured, we
                 * should match the format to the format that would otherwise
                 * be used for XFB, since dEQP checks for invariance here. It's
                 * unclear if this is required by the spec. */

                if (xfb->so_mask & (1ull << loc)) {
                        struct pipe_stream_output *o = pan_get_so(&xfb->stream_output, loc);
                        format = pan_xfb_format(format, o->num_components);
                        size = pan_varying_size(format);
                } else if (size == size_alt) {
                        format = alt;
                }

                gen_offsets[idx] = *gen_stride;
                gen_formats[other_idx] = format;
                offset = *gen_stride;
                *gen_stride += size;
        }

        return pan_emit_vary(present, PAN_VARY_GENERAL,
                        quirks, format, offset);
}

static void
pan_emit_special_input(union mali_attr *varyings,
                unsigned present,
                enum pan_special_varying v,
                mali_ptr addr)
{
        if (present & (1 << v)) {
                /* Ensure we write exactly once for performance and with fields
                 * zeroed appropriately to avoid flakes */

                union mali_attr s = {
                        .elements = addr
                };

                varyings[pan_varying_index(present, v)] = s;
        }
}

void
panfrost_emit_varying_descriptor(struct panfrost_batch *batch,
                                 unsigned vertex_count,
                                 struct mali_vertex_tiler_postfix *vertex_postfix,
                                 struct mali_vertex_tiler_postfix *tiler_postfix,
                                 union midgard_primitive_size *primitive_size)
{
        /* Load the shaders */
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_device *dev = pan_device(ctx->base.screen);
        struct panfrost_shader_state *vs, *fs;
        size_t vs_size, fs_size;

        /* Allocate the varying descriptor */

        vs = panfrost_get_shader_state(ctx, PIPE_SHADER_VERTEX);
        fs = panfrost_get_shader_state(ctx, PIPE_SHADER_FRAGMENT);
        vs_size = sizeof(struct mali_attr_meta) * vs->varying_count;
        fs_size = sizeof(struct mali_attr_meta) * fs->varying_count;

        struct panfrost_transfer trans = panfrost_pool_alloc(&batch->pool,
                                                                     vs_size +
                                                                     fs_size);

        struct pipe_stream_output_info *so = &vs->stream_output;
        unsigned present = pan_varying_present(vs, fs, dev->quirks);

        /* Check if this varying is linked by us. This is the case for
         * general-purpose, non-captured varyings. If it is, link it. If it's
         * not, use the provided stream out information to determine the
         * offset, since it was already linked for us. */

        unsigned gen_offsets[32];
        enum mali_format gen_formats[32];
        memset(gen_offsets, 0, sizeof(gen_offsets));
        memset(gen_formats, 0, sizeof(gen_formats));

        unsigned gen_stride = 0;
        assert(vs->varying_count < ARRAY_SIZE(gen_offsets));
        assert(fs->varying_count < ARRAY_SIZE(gen_offsets));

        unsigned streamout_offsets[32];

        for (unsigned i = 0; i < ctx->streamout.num_targets; ++i) {
                streamout_offsets[i] = panfrost_streamout_offset(
                                        so->stride[i],
                                        ctx->streamout.offsets[i],
                                        ctx->streamout.targets[i]);
        }

        struct mali_attr_meta *ovs = (struct mali_attr_meta *)trans.cpu;
        struct mali_attr_meta *ofs = ovs + vs->varying_count;

        for (unsigned i = 0; i < vs->varying_count; i++) {
                ovs[i] = panfrost_emit_varying(vs, fs, vs, present,
                                ctx->streamout.num_targets, streamout_offsets,
                                dev->quirks,
                                gen_offsets, gen_formats, &gen_stride, i, true, false);
        }

        for (unsigned i = 0; i < fs->varying_count; i++) {
                ofs[i] = panfrost_emit_varying(fs, vs, vs, present,
                                ctx->streamout.num_targets, streamout_offsets,
                                dev->quirks,
                                gen_offsets, gen_formats, &gen_stride, i, false, true);
        }

        unsigned xfb_base = pan_xfb_base(present);
        struct panfrost_transfer T = panfrost_pool_alloc(&batch->pool,
                        sizeof(union mali_attr) * (xfb_base + ctx->streamout.num_targets));
        union mali_attr *varyings = (union mali_attr *) T.cpu;

        /* Emit the stream out buffers */

        unsigned out_count = u_stream_outputs_for_vertices(ctx->active_prim,
                                                           ctx->vertex_count);

        for (unsigned i = 0; i < ctx->streamout.num_targets; ++i) {
                panfrost_emit_streamout(batch, &varyings[xfb_base + i],
                                        so->stride[i],
                                        ctx->streamout.offsets[i],
                                        out_count,
                                        ctx->streamout.targets[i]);
        }

        panfrost_emit_varyings(batch,
                        &varyings[pan_varying_index(present, PAN_VARY_GENERAL)],
                        gen_stride, vertex_count);

        /* fp32 vec4 gl_Position */
        tiler_postfix->position_varying = panfrost_emit_varyings(batch,
                        &varyings[pan_varying_index(present, PAN_VARY_POSITION)],
                        sizeof(float) * 4, vertex_count);

        if (present & (1 << PAN_VARY_PSIZ)) {
                primitive_size->pointer = panfrost_emit_varyings(batch,
                                &varyings[pan_varying_index(present, PAN_VARY_PSIZ)],
                                2, vertex_count);
        }

        pan_emit_special_input(varyings, present, PAN_VARY_PNTCOORD, MALI_VARYING_POINT_COORD);
        pan_emit_special_input(varyings, present, PAN_VARY_FACE, MALI_VARYING_FRONT_FACING);
        pan_emit_special_input(varyings, present, PAN_VARY_FRAGCOORD, MALI_VARYING_FRAG_COORD);

        vertex_postfix->varyings = T.gpu;
        tiler_postfix->varyings = T.gpu;

        vertex_postfix->varying_meta = trans.gpu;
        tiler_postfix->varying_meta = trans.gpu + vs_size;
}

void
panfrost_emit_vertex_tiler_jobs(struct panfrost_batch *batch,
                                struct mali_vertex_tiler_prefix *vertex_prefix,
                                struct mali_vertex_tiler_postfix *vertex_postfix,
                                struct mali_vertex_tiler_prefix *tiler_prefix,
                                struct mali_vertex_tiler_postfix *tiler_postfix,
                                union midgard_primitive_size *primitive_size)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_device *device = pan_device(ctx->base.screen);
        bool wallpapering = ctx->wallpaper_batch && batch->scoreboard.tiler_dep;
        struct bifrost_payload_vertex bifrost_vertex = {0,};
        struct bifrost_payload_tiler bifrost_tiler = {0,};
        struct midgard_payload_vertex_tiler midgard_vertex = {0,};
        struct midgard_payload_vertex_tiler midgard_tiler = {0,};
        void *vp, *tp;
        size_t vp_size, tp_size;

        if (device->quirks & IS_BIFROST) {
                bifrost_vertex.prefix = *vertex_prefix;
                bifrost_vertex.postfix = *vertex_postfix;
                vp = &bifrost_vertex;
                vp_size = sizeof(bifrost_vertex);

                bifrost_tiler.prefix = *tiler_prefix;
                bifrost_tiler.tiler.primitive_size = *primitive_size;
                bifrost_tiler.tiler.tiler_meta = panfrost_batch_get_tiler_meta(batch, ~0);
                bifrost_tiler.postfix = *tiler_postfix;
                tp = &bifrost_tiler;
                tp_size = sizeof(bifrost_tiler);
        } else {
                midgard_vertex.prefix = *vertex_prefix;
                midgard_vertex.postfix = *vertex_postfix;
                vp = &midgard_vertex;
                vp_size = sizeof(midgard_vertex);

                midgard_tiler.prefix = *tiler_prefix;
                midgard_tiler.postfix = *tiler_postfix;
                midgard_tiler.primitive_size = *primitive_size;
                tp = &midgard_tiler;
                tp_size = sizeof(midgard_tiler);
        }

        if (wallpapering) {
                /* Inject in reverse order, with "predicted" job indices.
                 * THIS IS A HACK XXX */
                panfrost_new_job(&batch->pool, &batch->scoreboard, JOB_TYPE_TILER, false,
                                 batch->scoreboard.job_index + 2, tp, tp_size, true);
                panfrost_new_job(&batch->pool, &batch->scoreboard, JOB_TYPE_VERTEX, false, 0,
                                 vp, vp_size, true);
                return;
        }

        /* If rasterizer discard is enable, only submit the vertex */

        bool rasterizer_discard = ctx->rasterizer &&
                                  ctx->rasterizer->base.rasterizer_discard;

        unsigned vertex = panfrost_new_job(&batch->pool, &batch->scoreboard, JOB_TYPE_VERTEX, false, 0,
                                           vp, vp_size, false);

        if (rasterizer_discard)
                return;

        panfrost_new_job(&batch->pool, &batch->scoreboard, JOB_TYPE_TILER, false, vertex, tp, tp_size,
                         false);
}

/* TODO: stop hardcoding this */
mali_ptr
panfrost_emit_sample_locations(struct panfrost_batch *batch)
{
        uint16_t locations[] = {
            128, 128,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            0, 256,
            128, 128,
            0, 0,
            0, 0,
            0, 0,
            0, 0,
            0, 0,
            0, 0,
            0, 0,
            0, 0,
            0, 0,
            0, 0,
            0, 0,
            0, 0,
            0, 0,
            0, 0,
            0, 0,
        };

        return panfrost_pool_upload(&batch->pool, locations, 96 * sizeof(uint16_t));
}
