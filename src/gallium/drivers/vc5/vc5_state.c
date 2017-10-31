/*
 * Copyright © 2014-2017 Broadcom
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
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

#include "pipe/p_state.h"
#include "util/u_format.h"
#include "util/u_inlines.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/u_half.h"
#include "util/u_helpers.h"

#include "vc5_context.h"
#include "broadcom/cle/v3d_packet_v33_pack.h"

static void *
vc5_generic_cso_state_create(const void *src, uint32_t size)
{
        void *dst = calloc(1, size);
        if (!dst)
                return NULL;
        memcpy(dst, src, size);
        return dst;
}

static void
vc5_generic_cso_state_delete(struct pipe_context *pctx, void *hwcso)
{
        free(hwcso);
}

static void
vc5_set_blend_color(struct pipe_context *pctx,
                    const struct pipe_blend_color *blend_color)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        vc5->blend_color.f = *blend_color;
        for (int i = 0; i < 4; i++) {
                vc5->blend_color.hf[i] =
                        util_float_to_half(blend_color->color[i]);
        }
        vc5->dirty |= VC5_DIRTY_BLEND_COLOR;
}

static void
vc5_set_stencil_ref(struct pipe_context *pctx,
                    const struct pipe_stencil_ref *stencil_ref)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        vc5->stencil_ref = *stencil_ref;
        vc5->dirty |= VC5_DIRTY_STENCIL_REF;
}

static void
vc5_set_clip_state(struct pipe_context *pctx,
                   const struct pipe_clip_state *clip)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        vc5->clip = *clip;
        vc5->dirty |= VC5_DIRTY_CLIP;
}

static void
vc5_set_sample_mask(struct pipe_context *pctx, unsigned sample_mask)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        vc5->sample_mask = sample_mask & ((1 << VC5_MAX_SAMPLES) - 1);
        vc5->dirty |= VC5_DIRTY_SAMPLE_MASK;
}

static uint16_t
float_to_187_half(float f)
{
        return fui(f) >> 16;
}

static void *
vc5_create_rasterizer_state(struct pipe_context *pctx,
                            const struct pipe_rasterizer_state *cso)
{
        struct vc5_rasterizer_state *so;

        so = CALLOC_STRUCT(vc5_rasterizer_state);
        if (!so)
                return NULL;

        so->base = *cso;

        /* Workaround: HW-2726 PTB does not handle zero-size points (BCM2835,
         * BCM21553).
         */
        so->point_size = MAX2(cso->point_size, .125f);

        if (cso->offset_tri) {
                so->offset_units = float_to_187_half(cso->offset_units);
                so->offset_factor = float_to_187_half(cso->offset_scale);
        }

        return so;
}

/* Blend state is baked into shaders. */
static void *
vc5_create_blend_state(struct pipe_context *pctx,
                       const struct pipe_blend_state *cso)
{
        return vc5_generic_cso_state_create(cso, sizeof(*cso));
}

static void *
vc5_create_depth_stencil_alpha_state(struct pipe_context *pctx,
                                     const struct pipe_depth_stencil_alpha_state *cso)
{
        struct vc5_depth_stencil_alpha_state *so;

        so = CALLOC_STRUCT(vc5_depth_stencil_alpha_state);
        if (!so)
                return NULL;

        so->base = *cso;

        if (cso->depth.enabled) {
                /* We only handle early Z in the < direction because otherwise
                 * we'd have to runtime guess which direction to set in the
                 * render config.
                 */
                so->early_z_enable =
                        ((cso->depth.func == PIPE_FUNC_LESS ||
                          cso->depth.func == PIPE_FUNC_LEQUAL) &&
                         (!cso->stencil[0].enabled ||
                          (cso->stencil[0].zfail_op == PIPE_STENCIL_OP_KEEP &&
                           (!cso->stencil[1].enabled ||
                            cso->stencil[1].zfail_op == PIPE_STENCIL_OP_KEEP))));
        }

        return so;
}

static void
vc5_set_polygon_stipple(struct pipe_context *pctx,
                        const struct pipe_poly_stipple *stipple)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        vc5->stipple = *stipple;
        vc5->dirty |= VC5_DIRTY_STIPPLE;
}

static void
vc5_set_scissor_states(struct pipe_context *pctx,
                       unsigned start_slot,
                       unsigned num_scissors,
                       const struct pipe_scissor_state *scissor)
{
        struct vc5_context *vc5 = vc5_context(pctx);

        vc5->scissor = *scissor;
        vc5->dirty |= VC5_DIRTY_SCISSOR;
}

static void
vc5_set_viewport_states(struct pipe_context *pctx,
                        unsigned start_slot,
                        unsigned num_viewports,
                        const struct pipe_viewport_state *viewport)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        vc5->viewport = *viewport;
        vc5->dirty |= VC5_DIRTY_VIEWPORT;
}

static void
vc5_set_vertex_buffers(struct pipe_context *pctx,
                       unsigned start_slot, unsigned count,
                       const struct pipe_vertex_buffer *vb)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        struct vc5_vertexbuf_stateobj *so = &vc5->vertexbuf;

        util_set_vertex_buffers_mask(so->vb, &so->enabled_mask, vb,
                                     start_slot, count);
        so->count = util_last_bit(so->enabled_mask);

        vc5->dirty |= VC5_DIRTY_VTXBUF;
}

static void
vc5_blend_state_bind(struct pipe_context *pctx, void *hwcso)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        vc5->blend = hwcso;
        vc5->dirty |= VC5_DIRTY_BLEND;
}

static void
vc5_rasterizer_state_bind(struct pipe_context *pctx, void *hwcso)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        struct vc5_rasterizer_state *rast = hwcso;

        if (vc5->rasterizer && rast &&
            vc5->rasterizer->base.flatshade != rast->base.flatshade) {
                vc5->dirty |= VC5_DIRTY_FLAT_SHADE_FLAGS;
        }

        vc5->rasterizer = hwcso;
        vc5->dirty |= VC5_DIRTY_RASTERIZER;
}

static void
vc5_zsa_state_bind(struct pipe_context *pctx, void *hwcso)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        vc5->zsa = hwcso;
        vc5->dirty |= VC5_DIRTY_ZSA;
}

static void *
vc5_vertex_state_create(struct pipe_context *pctx, unsigned num_elements,
                        const struct pipe_vertex_element *elements)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        struct vc5_vertex_stateobj *so = CALLOC_STRUCT(vc5_vertex_stateobj);

        if (!so)
                return NULL;

        memcpy(so->pipe, elements, sizeof(*elements) * num_elements);
        so->num_elements = num_elements;

        for (int i = 0; i < so->num_elements; i++) {
                const struct pipe_vertex_element *elem = &elements[i];
                const struct util_format_description *desc =
                        util_format_description(elem->src_format);
                uint32_t r_size = desc->channel[0].size;

                struct V3D33_GL_SHADER_STATE_ATTRIBUTE_RECORD attr_unpacked = {
                        /* vec_size == 0 means 4 */
                        .vec_size = desc->nr_channels & 3,
                        .signed_int_type = (desc->channel[0].type ==
                                            UTIL_FORMAT_TYPE_SIGNED),

                        .normalized_int_type = desc->channel[0].normalized,
                        .read_as_int_uint = desc->channel[0].pure_integer,
                        .instance_divisor = elem->instance_divisor,
                };

                switch (desc->channel[0].type) {
                case UTIL_FORMAT_TYPE_FLOAT:
                        if (r_size == 32) {
                                attr_unpacked.type = ATTRIBUTE_FLOAT;
                        } else {
                                assert(r_size == 16);
                                attr_unpacked.type = ATTRIBUTE_HALF_FLOAT;
                        }
                        break;

                case UTIL_FORMAT_TYPE_SIGNED:
                case UTIL_FORMAT_TYPE_UNSIGNED:
                        switch (r_size) {
                        case 32:
                                attr_unpacked.type = ATTRIBUTE_INT;
                                break;
                        case 16:
                                attr_unpacked.type = ATTRIBUTE_SHORT;
                                break;
                        case 10:
                                attr_unpacked.type = ATTRIBUTE_INT2_10_10_10;
                                break;
                        case 8:
                                attr_unpacked.type = ATTRIBUTE_BYTE;
                                break;
                        default:
                                fprintf(stderr,
                                        "format %s unsupported\n",
                                        desc->name);
                                attr_unpacked.type = ATTRIBUTE_BYTE;
                                abort();
                        }
                        break;

                default:
                        fprintf(stderr,
                                "format %s unsupported\n",
                                desc->name);
                        abort();
                }

                const uint32_t size =
                        cl_packet_length(GL_SHADER_STATE_ATTRIBUTE_RECORD);
                V3D33_GL_SHADER_STATE_ATTRIBUTE_RECORD_pack(NULL,
                                                            (uint8_t *)&so->attrs[i * size],
                                                            &attr_unpacked);
        }

        /* Set up the default attribute values in case any of the vertex
         * elements use them.
         */
        so->default_attribute_values = vc5_bo_alloc(vc5->screen,
                                                    VC5_MAX_ATTRIBUTES *
                                                    4 * sizeof(float),
                                                    "default attributes");
        uint32_t *attrs = vc5_bo_map(so->default_attribute_values);
        for (int i = 0; i < VC5_MAX_ATTRIBUTES; i++) {
                attrs[i * 4 + 0] = 0;
                attrs[i * 4 + 1] = 0;
                attrs[i * 4 + 2] = 0;
                if (i < so->num_elements &&
                    util_format_is_pure_integer(so->pipe[i].src_format)) {
                        attrs[i * 4 + 3] = 1;
                } else {
                        attrs[i * 4 + 3] = fui(1.0);
                }
        }

        return so;
}

static void
vc5_vertex_state_bind(struct pipe_context *pctx, void *hwcso)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        vc5->vtx = hwcso;
        vc5->dirty |= VC5_DIRTY_VTXSTATE;
}

static void
vc5_set_constant_buffer(struct pipe_context *pctx, uint shader, uint index,
                        const struct pipe_constant_buffer *cb)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        struct vc5_constbuf_stateobj *so = &vc5->constbuf[shader];

        util_copy_constant_buffer(&so->cb[index], cb);

        /* Note that the state tracker can unbind constant buffers by
         * passing NULL here.
         */
        if (unlikely(!cb)) {
                so->enabled_mask &= ~(1 << index);
                so->dirty_mask &= ~(1 << index);
                return;
        }

        so->enabled_mask |= 1 << index;
        so->dirty_mask |= 1 << index;
        vc5->dirty |= VC5_DIRTY_CONSTBUF;
}

static void
vc5_set_framebuffer_state(struct pipe_context *pctx,
                          const struct pipe_framebuffer_state *framebuffer)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        struct pipe_framebuffer_state *cso = &vc5->framebuffer;
        unsigned i;

        vc5->job = NULL;

        for (i = 0; i < framebuffer->nr_cbufs; i++)
                pipe_surface_reference(&cso->cbufs[i], framebuffer->cbufs[i]);
        for (; i < vc5->framebuffer.nr_cbufs; i++)
                pipe_surface_reference(&cso->cbufs[i], NULL);

        cso->nr_cbufs = framebuffer->nr_cbufs;

        pipe_surface_reference(&cso->zsbuf, framebuffer->zsbuf);

        cso->width = framebuffer->width;
        cso->height = framebuffer->height;

        vc5->dirty |= VC5_DIRTY_FRAMEBUFFER;
}

static struct vc5_texture_stateobj *
vc5_get_stage_tex(struct vc5_context *vc5, enum pipe_shader_type shader)
{
        switch (shader) {
        case PIPE_SHADER_FRAGMENT:
                vc5->dirty |= VC5_DIRTY_FRAGTEX;
                return &vc5->fragtex;
                break;
        case PIPE_SHADER_VERTEX:
                vc5->dirty |= VC5_DIRTY_VERTTEX;
                return &vc5->verttex;
                break;
        default:
                fprintf(stderr, "Unknown shader target %d\n", shader);
                abort();
        }
}

static uint32_t translate_wrap(uint32_t pipe_wrap, bool using_nearest)
{
        switch (pipe_wrap) {
        case PIPE_TEX_WRAP_REPEAT:
                return 0;
        case PIPE_TEX_WRAP_CLAMP_TO_EDGE:
                return 1;
        case PIPE_TEX_WRAP_MIRROR_REPEAT:
                return 2;
        case PIPE_TEX_WRAP_CLAMP_TO_BORDER:
                return 3;
        case PIPE_TEX_WRAP_CLAMP:
                return (using_nearest ? 1 : 3);
        default:
                unreachable("Unknown wrap mode");
        }
}


static void *
vc5_create_sampler_state(struct pipe_context *pctx,
                         const struct pipe_sampler_state *cso)
{
        struct vc5_sampler_state *so = CALLOC_STRUCT(vc5_sampler_state);

        if (!so)
                return NULL;

        memcpy(so, cso, sizeof(*cso));

        bool either_nearest =
                (cso->mag_img_filter == PIPE_TEX_MIPFILTER_NEAREST ||
                 cso->min_img_filter == PIPE_TEX_MIPFILTER_NEAREST);

        struct V3D33_TEXTURE_UNIFORM_PARAMETER_0_CFG_MODE1 p0_unpacked = {
                .s_wrap_mode = translate_wrap(cso->wrap_s, either_nearest),
                .t_wrap_mode = translate_wrap(cso->wrap_t, either_nearest),
                .r_wrap_mode = translate_wrap(cso->wrap_r, either_nearest),
        };
        V3D33_TEXTURE_UNIFORM_PARAMETER_0_CFG_MODE1_pack(NULL,
                                                         (uint8_t *)&so->p0,
                                                         &p0_unpacked);

        struct V3D33_TEXTURE_SHADER_STATE state_unpacked = {
                cl_packet_header(TEXTURE_SHADER_STATE),

                .min_level_of_detail = MAX2(cso->min_lod, 0.0),
                .depth_compare_function = cso->compare_func,
                .fixed_bias = cso->lod_bias,
        };
        STATIC_ASSERT(ARRAY_SIZE(so->texture_shader_state) ==
                      cl_packet_length(TEXTURE_SHADER_STATE));
        cl_packet_pack(TEXTURE_SHADER_STATE)(NULL, so->texture_shader_state,
                                             &state_unpacked);

        return so;
}

static void
vc5_sampler_states_bind(struct pipe_context *pctx,
                        enum pipe_shader_type shader, unsigned start,
                        unsigned nr, void **hwcso)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        struct vc5_texture_stateobj *stage_tex = vc5_get_stage_tex(vc5, shader);

        assert(start == 0);
        unsigned i;
        unsigned new_nr = 0;

        for (i = 0; i < nr; i++) {
                if (hwcso[i])
                        new_nr = i + 1;
                stage_tex->samplers[i] = hwcso[i];
        }

        for (; i < stage_tex->num_samplers; i++) {
                stage_tex->samplers[i] = NULL;
        }

        stage_tex->num_samplers = new_nr;
}

static uint32_t
translate_swizzle(unsigned char pipe_swizzle)
{
        switch (pipe_swizzle) {
        case PIPE_SWIZZLE_0:
                return 0;
        case PIPE_SWIZZLE_1:
                return 1;
        case PIPE_SWIZZLE_X:
        case PIPE_SWIZZLE_Y:
        case PIPE_SWIZZLE_Z:
        case PIPE_SWIZZLE_W:
                return 2 + pipe_swizzle;
        default:
                unreachable("unknown swizzle");
        }
}

static struct pipe_sampler_view *
vc5_create_sampler_view(struct pipe_context *pctx, struct pipe_resource *prsc,
                        const struct pipe_sampler_view *cso)
{
        struct vc5_sampler_view *so = CALLOC_STRUCT(vc5_sampler_view);
        struct vc5_resource *rsc = vc5_resource(prsc);

        if (!so)
                return NULL;

        so->base = *cso;

        pipe_reference(NULL, &prsc->reference);

        struct V3D33_TEXTURE_UNIFORM_PARAMETER_1_CFG_MODE1 unpacked = {
        };

        unpacked.return_word_0_of_texture_data = true;
        if (vc5_get_tex_return_size(cso->format) == 16) {
                unpacked.return_word_1_of_texture_data = true;
        } else {
                int chans = vc5_get_tex_return_channels(cso->format);

                if (chans > 1)
                        unpacked.return_word_1_of_texture_data = true;
                if (chans > 2)
                        unpacked.return_word_2_of_texture_data = true;
                if (chans > 3)
                        unpacked.return_word_3_of_texture_data = true;
        }

        V3D33_TEXTURE_UNIFORM_PARAMETER_1_CFG_MODE1_pack(NULL,
                                                         (uint8_t *)&so->p1,
                                                         &unpacked);

        /* Compute the sampler view's swizzle up front. This will be plugged
         * into either the sampler (for 16-bit returns) or the shader's
         * texture key (for 32)
         */
        uint8_t view_swizzle[4] = {
                cso->swizzle_r,
                cso->swizzle_g,
                cso->swizzle_b,
                cso->swizzle_a
        };
        const uint8_t *fmt_swizzle = vc5_get_format_swizzle(so->base.format);
        util_format_compose_swizzles(fmt_swizzle, view_swizzle, so->swizzle);

        so->base.texture = prsc;
        so->base.reference.count = 1;
        so->base.context = pctx;

        struct V3D33_TEXTURE_SHADER_STATE state_unpacked = {
                cl_packet_header(TEXTURE_SHADER_STATE),

                .image_width = prsc->width0,
                .image_height = prsc->height0,
                .image_depth = prsc->depth0,

                .texture_type = rsc->tex_format,
                .srgb = util_format_is_srgb(cso->format),

                .base_level = cso->u.tex.first_level,
                .array_stride_64_byte_aligned = rsc->cube_map_stride / 64,
        };

        /* Note: Contrary to the docs, the swizzle still applies even
         * if the return size is 32.  It's just that you probably want
         * to swizzle in the shader, because you need the Y/Z/W
         * channels to be defined.
         */
        if (vc5_get_tex_return_size(cso->format) != 32) {
                state_unpacked.swizzle_r = translate_swizzle(so->swizzle[0]);
                state_unpacked.swizzle_g = translate_swizzle(so->swizzle[1]);
                state_unpacked.swizzle_b = translate_swizzle(so->swizzle[2]);
                state_unpacked.swizzle_a = translate_swizzle(so->swizzle[3]);
        } else {
                state_unpacked.swizzle_r = translate_swizzle(PIPE_SWIZZLE_X);
                state_unpacked.swizzle_g = translate_swizzle(PIPE_SWIZZLE_Y);
                state_unpacked.swizzle_b = translate_swizzle(PIPE_SWIZZLE_Z);
                state_unpacked.swizzle_a = translate_swizzle(PIPE_SWIZZLE_W);
        }

        /* XXX: While we need to use this flag to enable tiled
         * resource sharing (even a small shared buffer should be UIF,
         * not UBLINEAR or raster), this is also at the moment
         * patching up the fact that our resource layout's decisions
         * about XOR don't quite match the HW's.
         */
        switch (rsc->slices[0].tiling) {
        case VC5_TILING_UIF_NO_XOR:
        case VC5_TILING_UIF_XOR:
                state_unpacked.level_0_is_strictly_uif = true;
                state_unpacked.level_0_xor_enable = false;
                break;
        default:
                break;
        }

        STATIC_ASSERT(ARRAY_SIZE(so->texture_shader_state) ==
                      cl_packet_length(TEXTURE_SHADER_STATE));
        cl_packet_pack(TEXTURE_SHADER_STATE)(NULL, so->texture_shader_state,
                                             &state_unpacked);

        return &so->base;
}

static void
vc5_sampler_view_destroy(struct pipe_context *pctx,
                         struct pipe_sampler_view *view)
{
        pipe_resource_reference(&view->texture, NULL);
        free(view);
}

static void
vc5_set_sampler_views(struct pipe_context *pctx,
                      enum pipe_shader_type shader,
                      unsigned start, unsigned nr,
                      struct pipe_sampler_view **views)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        struct vc5_texture_stateobj *stage_tex = vc5_get_stage_tex(vc5, shader);
        unsigned i;
        unsigned new_nr = 0;

        assert(start == 0);

        for (i = 0; i < nr; i++) {
                if (views[i])
                        new_nr = i + 1;
                pipe_sampler_view_reference(&stage_tex->textures[i], views[i]);
        }

        for (; i < stage_tex->num_textures; i++) {
                pipe_sampler_view_reference(&stage_tex->textures[i], NULL);
        }

        stage_tex->num_textures = new_nr;
}

static struct pipe_stream_output_target *
vc5_create_stream_output_target(struct pipe_context *pctx,
                                struct pipe_resource *prsc,
                                unsigned buffer_offset,
                                unsigned buffer_size)
{
        struct pipe_stream_output_target *target;

        target = CALLOC_STRUCT(pipe_stream_output_target);
        if (!target)
                return NULL;

        pipe_reference_init(&target->reference, 1);
        pipe_resource_reference(&target->buffer, prsc);

        target->context = pctx;
        target->buffer_offset = buffer_offset;
        target->buffer_size = buffer_size;

        return target;
}

static void
vc5_stream_output_target_destroy(struct pipe_context *pctx,
                                 struct pipe_stream_output_target *target)
{
        pipe_resource_reference(&target->buffer, NULL);
        free(target);
}

static void
vc5_set_stream_output_targets(struct pipe_context *pctx,
                              unsigned num_targets,
                              struct pipe_stream_output_target **targets,
                              const unsigned *offsets)
{
        struct vc5_context *ctx = vc5_context(pctx);
        struct vc5_streamout_stateobj *so = &ctx->streamout;
        unsigned i;

        assert(num_targets <= ARRAY_SIZE(so->targets));

        for (i = 0; i < num_targets; i++)
                pipe_so_target_reference(&so->targets[i], targets[i]);

        for (; i < so->num_targets; i++)
                pipe_so_target_reference(&so->targets[i], NULL);

        so->num_targets = num_targets;

        ctx->dirty |= VC5_DIRTY_STREAMOUT;
}

void
vc5_state_init(struct pipe_context *pctx)
{
        pctx->set_blend_color = vc5_set_blend_color;
        pctx->set_stencil_ref = vc5_set_stencil_ref;
        pctx->set_clip_state = vc5_set_clip_state;
        pctx->set_sample_mask = vc5_set_sample_mask;
        pctx->set_constant_buffer = vc5_set_constant_buffer;
        pctx->set_framebuffer_state = vc5_set_framebuffer_state;
        pctx->set_polygon_stipple = vc5_set_polygon_stipple;
        pctx->set_scissor_states = vc5_set_scissor_states;
        pctx->set_viewport_states = vc5_set_viewport_states;

        pctx->set_vertex_buffers = vc5_set_vertex_buffers;

        pctx->create_blend_state = vc5_create_blend_state;
        pctx->bind_blend_state = vc5_blend_state_bind;
        pctx->delete_blend_state = vc5_generic_cso_state_delete;

        pctx->create_rasterizer_state = vc5_create_rasterizer_state;
        pctx->bind_rasterizer_state = vc5_rasterizer_state_bind;
        pctx->delete_rasterizer_state = vc5_generic_cso_state_delete;

        pctx->create_depth_stencil_alpha_state = vc5_create_depth_stencil_alpha_state;
        pctx->bind_depth_stencil_alpha_state = vc5_zsa_state_bind;
        pctx->delete_depth_stencil_alpha_state = vc5_generic_cso_state_delete;

        pctx->create_vertex_elements_state = vc5_vertex_state_create;
        pctx->delete_vertex_elements_state = vc5_generic_cso_state_delete;
        pctx->bind_vertex_elements_state = vc5_vertex_state_bind;

        pctx->create_sampler_state = vc5_create_sampler_state;
        pctx->delete_sampler_state = vc5_generic_cso_state_delete;
        pctx->bind_sampler_states = vc5_sampler_states_bind;

        pctx->create_sampler_view = vc5_create_sampler_view;
        pctx->sampler_view_destroy = vc5_sampler_view_destroy;
        pctx->set_sampler_views = vc5_set_sampler_views;

        pctx->create_stream_output_target = vc5_create_stream_output_target;
        pctx->stream_output_target_destroy = vc5_stream_output_target_destroy;
        pctx->set_stream_output_targets = vc5_set_stream_output_targets;
}
