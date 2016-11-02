/*
 * Copyright 2011 Joakim Sindholt <opensource@zhasha.com>
 * Copyright 2013 Christoph Bumiller
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE. */

#include "device9.h"
#include "basetexture9.h"
#include "buffer9.h"
#include "indexbuffer9.h"
#include "surface9.h"
#include "vertexbuffer9.h"
#include "vertexdeclaration9.h"
#include "vertexshader9.h"
#include "pixelshader9.h"
#include "nine_pipe.h"
#include "nine_ff.h"
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "cso_cache/cso_context.h"
#include "util/u_upload_mgr.h"
#include "util/u_math.h"
#include "util/u_box.h"
#include "util/u_simple_shaders.h"

#define DBG_CHANNEL DBG_DEVICE

/* Check if some states need to be set dirty */

static inline DWORD
check_multisample(struct NineDevice9 *device)
{
    DWORD *rs = device->state.rs;
    DWORD new_value = (rs[D3DRS_ZENABLE] || rs[D3DRS_STENCILENABLE]) &&
                      device->state.rt[0]->desc.MultiSampleType >= 1 &&
                      rs[D3DRS_MULTISAMPLEANTIALIAS];
    if (rs[NINED3DRS_MULTISAMPLE] != new_value) {
        rs[NINED3DRS_MULTISAMPLE] = new_value;
        return NINE_STATE_RASTERIZER;
    }
    return 0;
}

/* State preparation only */

static inline void
prepare_blend(struct NineDevice9 *device)
{
    nine_convert_blend_state(&device->state.pipe.blend, device->state.rs);
    device->state.commit |= NINE_STATE_COMMIT_BLEND;
}

static inline void
prepare_dsa(struct NineDevice9 *device)
{
    nine_convert_dsa_state(&device->state.pipe.dsa, device->state.rs);
    device->state.commit |= NINE_STATE_COMMIT_DSA;
}

static inline void
prepare_rasterizer(struct NineDevice9 *device)
{
    nine_convert_rasterizer_state(device, &device->state.pipe.rast, device->state.rs);
    device->state.commit |= NINE_STATE_COMMIT_RASTERIZER;
}

static void
prepare_vs_constants_userbuf_swvp(struct NineDevice9 *device)
{
    struct nine_state *state = &device->state;

    if (state->changed.vs_const_f || state->changed.group & NINE_STATE_SWVP) {
        struct pipe_constant_buffer cb;

        cb.buffer = NULL;
        cb.buffer_offset = 0;
        cb.buffer_size = 4096 * sizeof(float[4]);
        cb.user_buffer = state->vs_const_f_swvp;

        if (state->vs->lconstf.ranges) {
            const struct nine_lconstf *lconstf =  &device->state.vs->lconstf;
            const struct nine_range *r = lconstf->ranges;
            unsigned n = 0;
            float *dst = device->state.vs_lconstf_temp;
            float *src = (float *)cb.user_buffer;
            memcpy(dst, src, cb.buffer_size);
            while (r) {
                unsigned p = r->bgn;
                unsigned c = r->end - r->bgn;
                memcpy(&dst[p * 4], &lconstf->data[n * 4], c * 4 * sizeof(float));
                n += c;
                r = r->next;
            }
            cb.user_buffer = dst;
        }

        state->pipe.cb0_swvp = cb;

        cb.user_buffer = (char *)cb.user_buffer + 4096 * sizeof(float[4]);
        state->pipe.cb1_swvp = cb;
    }

    if (state->changed.vs_const_i || state->changed.group & NINE_STATE_SWVP) {
        struct pipe_constant_buffer cb;

        cb.buffer = NULL;
        cb.buffer_offset = 0;
        cb.buffer_size = 2048 * sizeof(float[4]);
        cb.user_buffer = state->vs_const_i;

        state->pipe.cb2_swvp = cb;
    }

    if (state->changed.vs_const_b || state->changed.group & NINE_STATE_SWVP) {
        struct pipe_constant_buffer cb;

        cb.buffer = NULL;
        cb.buffer_offset = 0;
        cb.buffer_size = 512 * sizeof(float[4]);
        cb.user_buffer = state->vs_const_b;

        state->pipe.cb3_swvp = cb;
    }

    if (!device->driver_caps.user_cbufs) {
        struct pipe_constant_buffer *cb = &(state->pipe.cb0_swvp);
        u_upload_data(device->constbuf_uploader,
                      0,
                      cb->buffer_size,
                      device->constbuf_alignment,
                      cb->user_buffer,
                      &(cb->buffer_offset),
                      &(cb->buffer));
        u_upload_unmap(device->constbuf_uploader);
        cb->user_buffer = NULL;

        cb = &(state->pipe.cb1_swvp);
        u_upload_data(device->constbuf_uploader,
                      0,
                      cb->buffer_size,
                      device->constbuf_alignment,
                      cb->user_buffer,
                      &(cb->buffer_offset),
                      &(cb->buffer));
        u_upload_unmap(device->constbuf_uploader);
        cb->user_buffer = NULL;

        cb = &(state->pipe.cb2_swvp);
        u_upload_data(device->constbuf_uploader,
                      0,
                      cb->buffer_size,
                      device->constbuf_alignment,
                      cb->user_buffer,
                      &(cb->buffer_offset),
                      &(cb->buffer));
        u_upload_unmap(device->constbuf_uploader);
        cb->user_buffer = NULL;

        cb = &(state->pipe.cb3_swvp);
        u_upload_data(device->constbuf_uploader,
                      0,
                      cb->buffer_size,
                      device->constbuf_alignment,
                      cb->user_buffer,
                      &(cb->buffer_offset),
                      &(cb->buffer));
        u_upload_unmap(device->constbuf_uploader);
        cb->user_buffer = NULL;
    }

    if (device->state.changed.vs_const_f) {
        struct nine_range *r = device->state.changed.vs_const_f;
        struct nine_range *p = r;
        while (p->next)
            p = p->next;
        nine_range_pool_put_chain(&device->range_pool, r, p);
        device->state.changed.vs_const_f = NULL;
    }

    if (device->state.changed.vs_const_i) {
        struct nine_range *r = device->state.changed.vs_const_i;
        struct nine_range *p = r;
        while (p->next)
            p = p->next;
        nine_range_pool_put_chain(&device->range_pool, r, p);
        device->state.changed.vs_const_i = NULL;
    }

    if (device->state.changed.vs_const_b) {
        struct nine_range *r = device->state.changed.vs_const_b;
        struct nine_range *p = r;
        while (p->next)
            p = p->next;
        nine_range_pool_put_chain(&device->range_pool, r, p);
        device->state.changed.vs_const_b = NULL;
    }

    state->changed.group &= ~NINE_STATE_VS_CONST;
    state->commit |= NINE_STATE_COMMIT_CONST_VS;
}

static void
prepare_vs_constants_userbuf(struct NineDevice9 *device)
{
    struct nine_state *state = &device->state;
    struct pipe_constant_buffer cb;
    cb.buffer = NULL;
    cb.buffer_offset = 0;
    cb.buffer_size = device->state.vs->const_used_size;
    cb.user_buffer = device->state.vs_const_f;

    if (device->swvp) {
        prepare_vs_constants_userbuf_swvp(device);
        return;
    }

    if (state->changed.vs_const_i || state->changed.group & NINE_STATE_SWVP) {
        int *idst = (int *)&state->vs_const_f[4 * device->max_vs_const_f];
        memcpy(idst, state->vs_const_i, NINE_MAX_CONST_I * sizeof(int[4]));
    }

    if (state->changed.vs_const_b || state->changed.group & NINE_STATE_SWVP) {
        int *idst = (int *)&state->vs_const_f[4 * device->max_vs_const_f];
        uint32_t *bdst = (uint32_t *)&idst[4 * NINE_MAX_CONST_I];
        memcpy(bdst, state->vs_const_b, NINE_MAX_CONST_B * sizeof(BOOL));
    }

    if (device->state.changed.vs_const_i) {
        struct nine_range *r = device->state.changed.vs_const_i;
        struct nine_range *p = r;
        while (p->next)
            p = p->next;
        nine_range_pool_put_chain(&device->range_pool, r, p);
        device->state.changed.vs_const_i = NULL;
    }

    if (device->state.changed.vs_const_b) {
        struct nine_range *r = device->state.changed.vs_const_b;
        struct nine_range *p = r;
        while (p->next)
            p = p->next;
        nine_range_pool_put_chain(&device->range_pool, r, p);
        device->state.changed.vs_const_b = NULL;
    }

    if (!cb.buffer_size)
        return;

    if (device->state.vs->lconstf.ranges) {
        /* TODO: Can we make it so that we don't have to copy everything ? */
        const struct nine_lconstf *lconstf =  &device->state.vs->lconstf;
        const struct nine_range *r = lconstf->ranges;
        unsigned n = 0;
        float *dst = device->state.vs_lconstf_temp;
        float *src = (float *)cb.user_buffer;
        memcpy(dst, src, cb.buffer_size);
        while (r) {
            unsigned p = r->bgn;
            unsigned c = r->end - r->bgn;
            memcpy(&dst[p * 4], &lconstf->data[n * 4], c * 4 * sizeof(float));
            n += c;
            r = r->next;
        }
        cb.user_buffer = dst;
    }

    if (!device->driver_caps.user_cbufs) {
        u_upload_data(device->constbuf_uploader,
                      0,
                      cb.buffer_size,
                      device->constbuf_alignment,
                      cb.user_buffer,
                      &cb.buffer_offset,
                      &cb.buffer);
        u_upload_unmap(device->constbuf_uploader);
        cb.user_buffer = NULL;
    }

    state->pipe.cb_vs = cb;

    if (device->state.changed.vs_const_f) {
        struct nine_range *r = device->state.changed.vs_const_f;
        struct nine_range *p = r;
        while (p->next)
            p = p->next;
        nine_range_pool_put_chain(&device->range_pool, r, p);
        device->state.changed.vs_const_f = NULL;
    }

    state->changed.group &= ~NINE_STATE_VS_CONST;
    state->commit |= NINE_STATE_COMMIT_CONST_VS;
}

static void
prepare_ps_constants_userbuf(struct NineDevice9 *device)
{
    struct nine_state *state = &device->state;
    struct pipe_constant_buffer cb;
    cb.buffer = NULL;
    cb.buffer_offset = 0;
    cb.buffer_size = device->state.ps->const_used_size;
    cb.user_buffer = device->state.ps_const_f;

    if (state->changed.ps_const_i) {
        int *idst = (int *)&state->ps_const_f[4 * device->max_ps_const_f];
        memcpy(idst, state->ps_const_i, sizeof(state->ps_const_i));
        state->changed.ps_const_i = 0;
    }
    if (state->changed.ps_const_b) {
        int *idst = (int *)&state->ps_const_f[4 * device->max_ps_const_f];
        uint32_t *bdst = (uint32_t *)&idst[4 * NINE_MAX_CONST_I];
        memcpy(bdst, state->ps_const_b, sizeof(state->ps_const_b));
        state->changed.ps_const_b = 0;
    }

    /* Upload special constants needed to implement PS1.x instructions like TEXBEM,TEXBEML and BEM */
    if (device->state.ps->bumpenvmat_needed) {
        memcpy(device->state.ps_lconstf_temp, cb.user_buffer, cb.buffer_size);
        memcpy(&device->state.ps_lconstf_temp[4 * 8], &device->state.bumpmap_vars, sizeof(device->state.bumpmap_vars));

        cb.user_buffer = device->state.ps_lconstf_temp;
    }

    if (state->ps->byte_code.version < 0x30 &&
        state->rs[D3DRS_FOGENABLE]) {
        float *dst = &state->ps_lconstf_temp[4 * 32];
        if (cb.user_buffer != state->ps_lconstf_temp) {
            memcpy(state->ps_lconstf_temp, cb.user_buffer, cb.buffer_size);
            cb.user_buffer = state->ps_lconstf_temp;
        }

        d3dcolor_to_rgba(dst, state->rs[D3DRS_FOGCOLOR]);
        if (state->rs[D3DRS_FOGTABLEMODE] == D3DFOG_LINEAR) {
            dst[4] = asfloat(state->rs[D3DRS_FOGEND]);
            dst[5] = 1.0f / (asfloat(state->rs[D3DRS_FOGEND]) - asfloat(state->rs[D3DRS_FOGSTART]));
        } else if (state->rs[D3DRS_FOGTABLEMODE] != D3DFOG_NONE) {
            dst[4] = asfloat(state->rs[D3DRS_FOGDENSITY]);
        }
        cb.buffer_size = 4 * 4 * 34;
    }

    if (!cb.buffer_size)
        return;

    if (!device->driver_caps.user_cbufs) {
        u_upload_data(device->constbuf_uploader,
                      0,
                      cb.buffer_size,
                      device->constbuf_alignment,
                      cb.user_buffer,
                      &cb.buffer_offset,
                      &cb.buffer);
        u_upload_unmap(device->constbuf_uploader);
        cb.user_buffer = NULL;
    }

    state->pipe.cb_ps = cb;

    if (device->state.changed.ps_const_f) {
        struct nine_range *r = device->state.changed.ps_const_f;
        struct nine_range *p = r;
        while (p->next)
            p = p->next;
        nine_range_pool_put_chain(&device->range_pool, r, p);
        device->state.changed.ps_const_f = NULL;
    }
    state->changed.group &= ~NINE_STATE_PS_CONST;
    state->commit |= NINE_STATE_COMMIT_CONST_PS;
}

static inline uint32_t
prepare_vs(struct NineDevice9 *device, uint8_t shader_changed)
{
    struct nine_state *state = &device->state;
    struct NineVertexShader9 *vs = state->vs;
    uint32_t changed_group = 0;
    int has_key_changed = 0;

    if (likely(state->programmable_vs))
        has_key_changed = NineVertexShader9_UpdateKey(vs, device);

    if (!shader_changed && !has_key_changed)
        return 0;

    /* likely because we dislike FF */
    if (likely(state->programmable_vs)) {
        state->cso.vs = NineVertexShader9_GetVariant(vs);
    } else {
        vs = device->ff.vs;
        state->cso.vs = vs->ff_cso;
    }

    if (state->rs[NINED3DRS_VSPOINTSIZE] != vs->point_size) {
        state->rs[NINED3DRS_VSPOINTSIZE] = vs->point_size;
        changed_group |= NINE_STATE_RASTERIZER;
    }

    if ((state->bound_samplers_mask_vs & vs->sampler_mask) != vs->sampler_mask)
        /* Bound dummy sampler. */
        changed_group |= NINE_STATE_SAMPLER;

    state->commit |= NINE_STATE_COMMIT_VS;
    return changed_group;
}

static inline uint32_t
prepare_ps(struct NineDevice9 *device, uint8_t shader_changed)
{
    struct nine_state *state = &device->state;
    struct NinePixelShader9 *ps = state->ps;
    uint32_t changed_group = 0;
    int has_key_changed = 0;

    if (likely(ps))
        has_key_changed = NinePixelShader9_UpdateKey(ps, state);

    if (!shader_changed && !has_key_changed)
        return 0;

    if (likely(ps)) {
        state->cso.ps = NinePixelShader9_GetVariant(ps);
    } else {
        ps = device->ff.ps;
        state->cso.ps = ps->ff_cso;
    }

    if ((state->bound_samplers_mask_ps & ps->sampler_mask) != ps->sampler_mask)
        /* Bound dummy sampler. */
        changed_group |= NINE_STATE_SAMPLER;

    state->commit |= NINE_STATE_COMMIT_PS;
    return changed_group;
}

/* State preparation incremental */

/* State preparation + State commit */

static void
update_framebuffer(struct NineDevice9 *device, bool is_clear)
{
    struct pipe_context *pipe = device->pipe;
    struct nine_state *state = &device->state;
    struct pipe_framebuffer_state *fb = &device->state.fb;
    unsigned i;
    struct NineSurface9 *rt0 = state->rt[0];
    unsigned w = rt0->desc.Width;
    unsigned h = rt0->desc.Height;
    unsigned nr_samples = rt0->base.info.nr_samples;
    unsigned ps_mask = state->ps ? state->ps->rt_mask : 1;
    unsigned mask = is_clear ? 0xf : ps_mask;
    const int sRGB = state->rs[D3DRS_SRGBWRITEENABLE] ? 1 : 0;

    DBG("\n");

    state->rt_mask = 0x0;
    fb->nr_cbufs = 0;

    /* all render targets must have the same size and the depth buffer must be
     * bigger. Multisample has to match, according to spec. But some apps do
     * things wrong there, and no error is returned. The behaviour they get
     * apparently is that depth buffer is disabled if it doesn't match.
     * Surely the same for render targets. */

    /* Special case: D3DFMT_NULL is used to bound no real render target,
     * but render to depth buffer. We have to not take into account the render
     * target info. TODO: know what should happen when there are several render targers
     * and the first one is D3DFMT_NULL */
    if (rt0->desc.Format == D3DFMT_NULL && state->ds) {
        w = state->ds->desc.Width;
        h = state->ds->desc.Height;
        nr_samples = state->ds->base.info.nr_samples;
    }

    for (i = 0; i < device->caps.NumSimultaneousRTs; ++i) {
        struct NineSurface9 *rt = state->rt[i];

        if (rt && rt->desc.Format != D3DFMT_NULL && (mask & (1 << i)) &&
            rt->desc.Width == w && rt->desc.Height == h &&
            rt->base.info.nr_samples == nr_samples) {
            fb->cbufs[i] = NineSurface9_GetSurface(rt, sRGB);
            state->rt_mask |= 1 << i;
            fb->nr_cbufs = i + 1;

            if (unlikely(rt->desc.Usage & D3DUSAGE_AUTOGENMIPMAP)) {
                assert(rt->texture == D3DRTYPE_TEXTURE ||
                       rt->texture == D3DRTYPE_CUBETEXTURE);
                NineBaseTexture9(rt->base.base.container)->dirty_mip = TRUE;
            }
        } else {
            /* Color outputs must match RT slot,
             * drivers will have to handle NULL entries for GL, too.
             */
            fb->cbufs[i] = NULL;
        }
    }

    if (state->ds && state->ds->desc.Width >= w &&
        state->ds->desc.Height >= h &&
        state->ds->base.info.nr_samples == nr_samples) {
        fb->zsbuf = NineSurface9_GetSurface(state->ds, 0);
    } else {
        fb->zsbuf = NULL;
    }

    fb->width = w;
    fb->height = h;

    pipe->set_framebuffer_state(pipe, fb); /* XXX: cso ? */

    if (is_clear && state->rt_mask == ps_mask)
        state->changed.group &= ~NINE_STATE_FB;
}

static void
update_viewport(struct NineDevice9 *device)
{
    const D3DVIEWPORT9 *vport = &device->state.viewport;
    struct pipe_viewport_state pvport;

    /* D3D coordinates are:
     * -1 .. +1 for X,Y and
     *  0 .. +1 for Z (we use pipe_rasterizer_state.clip_halfz)
     */
    pvport.scale[0] = (float)vport->Width * 0.5f;
    pvport.scale[1] = (float)vport->Height * -0.5f;
    pvport.scale[2] = vport->MaxZ - vport->MinZ;
    pvport.translate[0] = (float)vport->Width * 0.5f + (float)vport->X;
    pvport.translate[1] = (float)vport->Height * 0.5f + (float)vport->Y;
    pvport.translate[2] = vport->MinZ;

    /* We found R600 and SI cards have some imprecision
     * on the barycentric coordinates used for interpolation.
     * Some shaders rely on having something precise.
     * We found that the proprietary driver has the imprecision issue,
     * except when the render target width and height are powers of two.
     * It is using some sort of workaround for these cases
     * which covers likely all the cases the applications rely
     * on something precise.
     * We haven't found the workaround, but it seems like it's better
     * for applications if the imprecision is biased towards infinity
     * instead of -infinity (which is what measured). So shift slightly
     * the viewport: not enough to change rasterization result (in particular
     * for multisampling), but enough to make the imprecision biased
     * towards infinity. We do this shift only if render target width and
     * height are powers of two.
     * Solves 'red shadows' bug on UE3 games.
     */
    if (device->driver_bugs.buggy_barycentrics &&
        ((vport->Width & (vport->Width-1)) == 0) &&
        ((vport->Height & (vport->Height-1)) == 0)) {
        pvport.translate[0] -= 1.0f / 128.0f;
        pvport.translate[1] -= 1.0f / 128.0f;
    }

    cso_set_viewport(device->cso, &pvport);
}

/* Loop through VS inputs and pick the vertex elements with the declared
 * usage from the vertex declaration, then insert the instance divisor from
 * the stream source frequency setting.
 */
static void
update_vertex_elements(struct NineDevice9 *device)
{
    struct nine_state *state = &device->state;
    const struct NineVertexDeclaration9 *vdecl = device->state.vdecl;
    const struct NineVertexShader9 *vs;
    unsigned n, b, i;
    int index;
    char vdecl_index_map[16]; /* vs->num_inputs <= 16 */
    char used_streams[device->caps.MaxStreams];
    int dummy_vbo_stream = -1;
    BOOL need_dummy_vbo = FALSE;
    struct pipe_vertex_element ve[PIPE_MAX_ATTRIBS];

    state->stream_usage_mask = 0;
    memset(vdecl_index_map, -1, 16);
    memset(used_streams, 0, device->caps.MaxStreams);
    vs = state->programmable_vs ? device->state.vs : device->ff.vs;

    if (vdecl) {
        for (n = 0; n < vs->num_inputs; ++n) {
            DBG("looking up input %u (usage %u) from vdecl(%p)\n",
                n, vs->input_map[n].ndecl, vdecl);

            for (i = 0; i < vdecl->nelems; i++) {
                if (vdecl->usage_map[i] == vs->input_map[n].ndecl) {
                    vdecl_index_map[n] = i;
                    used_streams[vdecl->elems[i].vertex_buffer_index] = 1;
                    break;
                }
            }
            if (vdecl_index_map[n] < 0)
                need_dummy_vbo = TRUE;
        }
    } else {
        /* No vertex declaration. Likely will never happen in practice,
         * but we need not crash on this */
        need_dummy_vbo = TRUE;
    }

    if (need_dummy_vbo) {
        for (i = 0; i < device->caps.MaxStreams; i++ ) {
            if (!used_streams[i]) {
                dummy_vbo_stream = i;
                break;
            }
        }
    }
    /* there are less vertex shader inputs than stream slots,
     * so if we need a slot for the dummy vbo, we should have found one */
    assert (!need_dummy_vbo || dummy_vbo_stream != -1);

    for (n = 0; n < vs->num_inputs; ++n) {
        index = vdecl_index_map[n];
        if (index >= 0) {
            ve[n] = vdecl->elems[index];
            b = ve[n].vertex_buffer_index;
            state->stream_usage_mask |= 1 << b;
            /* XXX wine just uses 1 here: */
            if (state->stream_freq[b] & D3DSTREAMSOURCE_INSTANCEDATA)
                ve[n].instance_divisor = state->stream_freq[b] & 0x7FFFFF;
        } else {
            /* if the vertex declaration is incomplete compared to what the
             * vertex shader needs, we bind a dummy vbo with 0 0 0 0.
             * This is not precised by the spec, but is the behaviour
             * tested on win */
            ve[n].vertex_buffer_index = dummy_vbo_stream;
            ve[n].src_format = PIPE_FORMAT_R32G32B32A32_FLOAT;
            ve[n].src_offset = 0;
            ve[n].instance_divisor = 0;
        }
    }

    if (state->dummy_vbo_bound_at != dummy_vbo_stream) {
        if (state->dummy_vbo_bound_at >= 0)
            state->changed.vtxbuf |= 1 << state->dummy_vbo_bound_at;
        if (dummy_vbo_stream >= 0) {
            state->changed.vtxbuf |= 1 << dummy_vbo_stream;
            state->vbo_bound_done = FALSE;
        }
        state->dummy_vbo_bound_at = dummy_vbo_stream;
    }

    cso_set_vertex_elements(device->cso, vs->num_inputs, ve);

    state->changed.stream_freq = 0;
}

static void
update_vertex_buffers(struct NineDevice9 *device)
{
    struct pipe_context *pipe = device->pipe;
    struct nine_state *state = &device->state;
    struct pipe_vertex_buffer dummy_vtxbuf;
    uint32_t mask = state->changed.vtxbuf;
    unsigned i;

    DBG("mask=%x\n", mask);

    if (state->dummy_vbo_bound_at >= 0) {
        if (!state->vbo_bound_done) {
            dummy_vtxbuf.buffer = device->dummy_vbo;
            dummy_vtxbuf.stride = 0;
            dummy_vtxbuf.user_buffer = NULL;
            dummy_vtxbuf.buffer_offset = 0;
            pipe->set_vertex_buffers(pipe, state->dummy_vbo_bound_at,
                                     1, &dummy_vtxbuf);
            state->vbo_bound_done = TRUE;
        }
        mask &= ~(1 << state->dummy_vbo_bound_at);
    }

    for (i = 0; mask; mask >>= 1, ++i) {
        if (mask & 1) {
            if (state->vtxbuf[i].buffer)
                pipe->set_vertex_buffers(pipe, i, 1, &state->vtxbuf[i]);
            else
                pipe->set_vertex_buffers(pipe, i, 1, NULL);
        }
    }

    state->changed.vtxbuf = 0;
}

static inline boolean
update_sampler_derived(struct nine_state *state, unsigned s)
{
    boolean changed = FALSE;

    if (state->samp[s][NINED3DSAMP_SHADOW] != state->texture[s]->shadow) {
        changed = TRUE;
        state->samp[s][NINED3DSAMP_SHADOW] = state->texture[s]->shadow;
    }

    if (state->samp[s][NINED3DSAMP_CUBETEX] !=
        (NineResource9(state->texture[s])->type == D3DRTYPE_CUBETEXTURE)) {
        changed = TRUE;
        state->samp[s][NINED3DSAMP_CUBETEX] =
                NineResource9(state->texture[s])->type == D3DRTYPE_CUBETEXTURE;
    }

    if (state->samp[s][D3DSAMP_MIPFILTER] != D3DTEXF_NONE) {
        int lod = state->samp[s][D3DSAMP_MAXMIPLEVEL] - state->texture[s]->managed.lod;
        if (lod < 0)
            lod = 0;
        if (state->samp[s][NINED3DSAMP_MINLOD] != lod) {
            changed = TRUE;
            state->samp[s][NINED3DSAMP_MINLOD] = lod;
        }
    } else {
        state->changed.sampler[s] &= ~0x300; /* lod changes irrelevant */
    }

    return changed;
}

/* TODO: add sRGB override to pipe_sampler_state ? */
static void
update_textures_and_samplers(struct NineDevice9 *device)
{
    struct nine_state *state = &device->state;
    struct pipe_sampler_view *view[NINE_MAX_SAMPLERS];
    unsigned num_textures;
    unsigned i;
    boolean commit_samplers;
    uint16_t sampler_mask = state->ps ? state->ps->sampler_mask :
                            device->ff.ps->sampler_mask;

    /* TODO: Can we reduce iterations here ? */

    commit_samplers = FALSE;
    state->bound_samplers_mask_ps = 0;
    for (num_textures = 0, i = 0; i < NINE_MAX_SAMPLERS_PS; ++i) {
        const unsigned s = NINE_SAMPLER_PS(i);
        int sRGB;

        if (!state->texture[s] && !(sampler_mask & (1 << i))) {
            view[i] = NULL;
            continue;
        }

        if (state->texture[s]) {
            sRGB = state->samp[s][D3DSAMP_SRGBTEXTURE] ? 1 : 0;

            view[i] = NineBaseTexture9_GetSamplerView(state->texture[s], sRGB);
            num_textures = i + 1;

            if (update_sampler_derived(state, s) || (state->changed.sampler[s] & 0x05fe)) {
                state->changed.sampler[s] = 0;
                commit_samplers = TRUE;
                nine_convert_sampler_state(device->cso, s, state->samp[s]);
            }
        } else {
            /* Bind dummy sampler. We do not bind dummy sampler when
             * it is not needed because it could add overhead. The
             * dummy sampler should have r=g=b=0 and a=1. We do not
             * unbind dummy sampler directly when they are not needed
             * anymore, but they're going to be removed as long as texture
             * or sampler states are changed. */
            view[i] = device->dummy_sampler_view;
            num_textures = i + 1;

            cso_single_sampler(device->cso, PIPE_SHADER_FRAGMENT,
                               s - NINE_SAMPLER_PS(0), &device->dummy_sampler_state);

            commit_samplers = TRUE;
            state->changed.sampler[s] = ~0;
        }

        state->bound_samplers_mask_ps |= (1 << s);
    }

    cso_set_sampler_views(device->cso, PIPE_SHADER_FRAGMENT, num_textures, view);

    if (commit_samplers)
        cso_single_sampler_done(device->cso, PIPE_SHADER_FRAGMENT);

    commit_samplers = FALSE;
    sampler_mask = state->programmable_vs ? state->vs->sampler_mask : 0;
    state->bound_samplers_mask_vs = 0;
    for (num_textures = 0, i = 0; i < NINE_MAX_SAMPLERS_VS; ++i) {
        const unsigned s = NINE_SAMPLER_VS(i);
        int sRGB;

        if (!state->texture[s] && !(sampler_mask & (1 << i))) {
            view[i] = NULL;
            continue;
        }

        if (state->texture[s]) {
            sRGB = state->samp[s][D3DSAMP_SRGBTEXTURE] ? 1 : 0;

            view[i] = NineBaseTexture9_GetSamplerView(state->texture[s], sRGB);
            num_textures = i + 1;

            if (update_sampler_derived(state, s) || (state->changed.sampler[s] & 0x05fe)) {
                state->changed.sampler[s] = 0;
                commit_samplers = TRUE;
                nine_convert_sampler_state(device->cso, s, state->samp[s]);
            }
        } else {
            /* Bind dummy sampler. We do not bind dummy sampler when
             * it is not needed because it could add overhead. The
             * dummy sampler should have r=g=b=0 and a=1. We do not
             * unbind dummy sampler directly when they are not needed
             * anymore, but they're going to be removed as long as texture
             * or sampler states are changed. */
            view[i] = device->dummy_sampler_view;
            num_textures = i + 1;

            cso_single_sampler(device->cso, PIPE_SHADER_VERTEX,
                               s - NINE_SAMPLER_VS(0), &device->dummy_sampler_state);

            commit_samplers = TRUE;
            state->changed.sampler[s] = ~0;
        }

        state->bound_samplers_mask_vs |= (1 << s);
    }

    cso_set_sampler_views(device->cso, PIPE_SHADER_VERTEX, num_textures, view);

    if (commit_samplers)
        cso_single_sampler_done(device->cso, PIPE_SHADER_VERTEX);

    state->changed.texture = 0;
}

/* State commit only */

static inline void
commit_blend(struct NineDevice9 *device)
{
    cso_set_blend(device->cso, &device->state.pipe.blend);
}

static inline void
commit_dsa(struct NineDevice9 *device)
{
    cso_set_depth_stencil_alpha(device->cso, &device->state.pipe.dsa);
}

static inline void
commit_scissor(struct NineDevice9 *device)
{
    struct pipe_context *pipe = device->pipe;

    pipe->set_scissor_states(pipe, 0, 1, &device->state.scissor);
}

static inline void
commit_rasterizer(struct NineDevice9 *device)
{
    cso_set_rasterizer(device->cso, &device->state.pipe.rast);
}

static inline void
commit_index_buffer(struct NineDevice9 *device)
{
    struct pipe_context *pipe = device->pipe;
    if (device->state.idxbuf)
        pipe->set_index_buffer(pipe, &device->state.idxbuf->buffer);
    else
        pipe->set_index_buffer(pipe, NULL);
}

static inline void
commit_vs_constants(struct NineDevice9 *device)
{
    struct pipe_context *pipe = device->pipe;

    if (unlikely(!device->state.programmable_vs))
        pipe->set_constant_buffer(pipe, PIPE_SHADER_VERTEX, 0, &device->state.pipe.cb_vs_ff);
    else {
        if (device->swvp) {
            pipe->set_constant_buffer(pipe, PIPE_SHADER_VERTEX, 0, &device->state.pipe.cb0_swvp);
            pipe->set_constant_buffer(pipe, PIPE_SHADER_VERTEX, 1, &device->state.pipe.cb1_swvp);
            pipe->set_constant_buffer(pipe, PIPE_SHADER_VERTEX, 2, &device->state.pipe.cb2_swvp);
            pipe->set_constant_buffer(pipe, PIPE_SHADER_VERTEX, 3, &device->state.pipe.cb3_swvp);
        } else {
            pipe->set_constant_buffer(pipe, PIPE_SHADER_VERTEX, 0, &device->state.pipe.cb_vs);
        }
    }
}

static inline void
commit_ps_constants(struct NineDevice9 *device)
{
    struct pipe_context *pipe = device->pipe;

    if (unlikely(!device->state.ps))
        pipe->set_constant_buffer(pipe, PIPE_SHADER_FRAGMENT, 0, &device->state.pipe.cb_ps_ff);
    else
        pipe->set_constant_buffer(pipe, PIPE_SHADER_FRAGMENT, 0, &device->state.pipe.cb_ps);
}

static inline void
commit_vs(struct NineDevice9 *device)
{
    struct nine_state *state = &device->state;

    device->pipe->bind_vs_state(device->pipe, state->cso.vs);
}


static inline void
commit_ps(struct NineDevice9 *device)
{
    struct nine_state *state = &device->state;

    device->pipe->bind_fs_state(device->pipe, state->cso.ps);
}
/* State Update */

#define NINE_STATE_SHADER_CHANGE_VS \
   (NINE_STATE_VS |         \
    NINE_STATE_TEXTURE |    \
    NINE_STATE_FOG_SHADER | \
    NINE_STATE_POINTSIZE_SHADER | \
    NINE_STATE_SWVP)

#define NINE_STATE_SHADER_CHANGE_PS \
   (NINE_STATE_PS |         \
    NINE_STATE_TEXTURE |    \
    NINE_STATE_FOG_SHADER | \
    NINE_STATE_PS1X_SHADER)

#define NINE_STATE_FREQUENT \
   (NINE_STATE_RASTERIZER | \
    NINE_STATE_TEXTURE |    \
    NINE_STATE_SAMPLER |    \
    NINE_STATE_VS_CONST |   \
    NINE_STATE_PS_CONST |   \
    NINE_STATE_MULTISAMPLE)

#define NINE_STATE_COMMON \
   (NINE_STATE_FB |       \
    NINE_STATE_BLEND |    \
    NINE_STATE_DSA |      \
    NINE_STATE_VIEWPORT | \
    NINE_STATE_VDECL |    \
    NINE_STATE_IDXBUF |   \
    NINE_STATE_STREAMFREQ)

#define NINE_STATE_RARE      \
   (NINE_STATE_SCISSOR |     \
    NINE_STATE_BLEND_COLOR | \
    NINE_STATE_STENCIL_REF | \
    NINE_STATE_SAMPLE_MASK)


/* TODO: only go through dirty textures */
static void
validate_textures(struct NineDevice9 *device)
{
    struct NineBaseTexture9 *tex, *ptr;
    LIST_FOR_EACH_ENTRY_SAFE(tex, ptr, &device->update_textures, list) {
        list_delinit(&tex->list);
        NineBaseTexture9_Validate(tex);
    }
}

static void
update_managed_buffers(struct NineDevice9 *device)
{
    struct NineBuffer9 *buf, *ptr;
    LIST_FOR_EACH_ENTRY_SAFE(buf, ptr, &device->update_buffers, managed.list) {
        list_delinit(&buf->managed.list);
        NineBuffer9_Upload(buf);
    }
}

void
nine_update_state_framebuffer_clear(struct NineDevice9 *device)
{
    struct nine_state *state = &device->state;

    validate_textures(device);

    if (state->changed.group & NINE_STATE_FB)
        update_framebuffer(device, TRUE);
}

boolean
nine_update_state(struct NineDevice9 *device)
{
    struct pipe_context *pipe = device->pipe;
    struct nine_state *state = &device->state;
    uint32_t group;

    DBG("changed state groups: %x\n", state->changed.group);

    /* NOTE: We may want to use the cso cache for everything, or let
     * NineDevice9.RestoreNonCSOState actually set the states, then we wouldn't
     * have to care about state being clobbered here and could merge this back
     * into update_textures. Except, we also need to re-validate textures that
     * may be dirty anyway, even if no texture bindings changed.
     */
    validate_textures(device); /* may clobber state */
    update_managed_buffers(device);

    /* ff_update may change VS/PS dirty bits */
    if (unlikely(!state->programmable_vs || !state->ps))
        nine_ff_update(device);
    group = state->changed.group;

    if (group & (NINE_STATE_SHADER_CHANGE_VS | NINE_STATE_SHADER_CHANGE_PS)) {
        if (group & NINE_STATE_SHADER_CHANGE_VS)
            group |= prepare_vs(device, (group & NINE_STATE_VS) != 0); /* may set NINE_STATE_RASTERIZER and NINE_STATE_SAMPLER*/
        if (group & NINE_STATE_SHADER_CHANGE_PS)
            group |= prepare_ps(device, (group & NINE_STATE_PS) != 0);
    }

    if (group & (NINE_STATE_COMMON | NINE_STATE_VS)) {
        if (group & NINE_STATE_FB)
            update_framebuffer(device, FALSE);
        if (group & NINE_STATE_BLEND)
            prepare_blend(device);
        if (group & NINE_STATE_DSA)
            prepare_dsa(device);
        if (group & NINE_STATE_VIEWPORT)
            update_viewport(device);
        if (group & (NINE_STATE_VDECL | NINE_STATE_VS | NINE_STATE_STREAMFREQ))
            update_vertex_elements(device);
        if (group & NINE_STATE_IDXBUF)
            commit_index_buffer(device);
    }

    if (likely(group & (NINE_STATE_FREQUENT | NINE_STATE_VS | NINE_STATE_PS | NINE_STATE_SWVP))) {
        if (group & NINE_STATE_MULTISAMPLE)
            group |= check_multisample(device);
        if (group & NINE_STATE_RASTERIZER)
            prepare_rasterizer(device);
        if (group & (NINE_STATE_TEXTURE | NINE_STATE_SAMPLER))
            update_textures_and_samplers(device);
        if ((group & (NINE_STATE_VS_CONST | NINE_STATE_VS | NINE_STATE_SWVP)) && state->programmable_vs)
            prepare_vs_constants_userbuf(device);
        if ((group & (NINE_STATE_PS_CONST | NINE_STATE_PS)) && state->ps)
            prepare_ps_constants_userbuf(device);
    }

    if (state->changed.vtxbuf)
        update_vertex_buffers(device);

    if (state->commit & NINE_STATE_COMMIT_BLEND)
        commit_blend(device);
    if (state->commit & NINE_STATE_COMMIT_DSA)
        commit_dsa(device);
    if (state->commit & NINE_STATE_COMMIT_RASTERIZER)
        commit_rasterizer(device);
    if (state->commit & NINE_STATE_COMMIT_CONST_VS)
        commit_vs_constants(device);
    if (state->commit & NINE_STATE_COMMIT_CONST_PS)
        commit_ps_constants(device);
    if (state->commit & NINE_STATE_COMMIT_VS)
        commit_vs(device);
    if (state->commit & NINE_STATE_COMMIT_PS)
        commit_ps(device);

    state->commit = 0;

    if (unlikely(state->changed.ucp)) {
        pipe->set_clip_state(pipe, &state->clip);
        state->changed.ucp = 0;
    }

    if (unlikely(group & NINE_STATE_RARE)) {
        if (group & NINE_STATE_SCISSOR)
            commit_scissor(device);
        if (group & NINE_STATE_BLEND_COLOR) {
            struct pipe_blend_color color;
            d3dcolor_to_rgba(&color.color[0], state->rs[D3DRS_BLENDFACTOR]);
            pipe->set_blend_color(pipe, &color);
        }
        if (group & NINE_STATE_SAMPLE_MASK) {
            if (state->rt[0]->desc.MultiSampleType == D3DMULTISAMPLE_NONMASKABLE) {
                pipe->set_sample_mask(pipe, ~0);
            } else {
                pipe->set_sample_mask(pipe, state->rs[D3DRS_MULTISAMPLEMASK]);
            }
        }
        if (group & NINE_STATE_STENCIL_REF) {
            struct pipe_stencil_ref ref;
            ref.ref_value[0] = state->rs[D3DRS_STENCILREF];
            ref.ref_value[1] = ref.ref_value[0];
            pipe->set_stencil_ref(pipe, &ref);
        }
    }

    device->state.changed.group &=
        (NINE_STATE_FF | NINE_STATE_VS_CONST | NINE_STATE_PS_CONST);

    DBG("finished\n");

    return TRUE;
}

/* State defaults */

static const DWORD nine_render_state_defaults[NINED3DRS_LAST + 1] =
{
 /* [D3DRS_ZENABLE] = D3DZB_TRUE; wine: auto_depth_stencil */
    [D3DRS_ZENABLE] = D3DZB_FALSE,
    [D3DRS_FILLMODE] = D3DFILL_SOLID,
    [D3DRS_SHADEMODE] = D3DSHADE_GOURAUD,
/*  [D3DRS_LINEPATTERN] = 0x00000000, */
    [D3DRS_ZWRITEENABLE] = TRUE,
    [D3DRS_ALPHATESTENABLE] = FALSE,
    [D3DRS_LASTPIXEL] = TRUE,
    [D3DRS_SRCBLEND] = D3DBLEND_ONE,
    [D3DRS_DESTBLEND] = D3DBLEND_ZERO,
    [D3DRS_CULLMODE] = D3DCULL_CCW,
    [D3DRS_ZFUNC] = D3DCMP_LESSEQUAL,
    [D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS,
    [D3DRS_ALPHAREF] = 0,
    [D3DRS_DITHERENABLE] = FALSE,
    [D3DRS_ALPHABLENDENABLE] = FALSE,
    [D3DRS_FOGENABLE] = FALSE,
    [D3DRS_SPECULARENABLE] = FALSE,
/*  [D3DRS_ZVISIBLE] = 0, */
    [D3DRS_FOGCOLOR] = 0,
    [D3DRS_FOGTABLEMODE] = D3DFOG_NONE,
    [D3DRS_FOGSTART] = 0x00000000,
    [D3DRS_FOGEND] = 0x3F800000,
    [D3DRS_FOGDENSITY] = 0x3F800000,
/*  [D3DRS_EDGEANTIALIAS] = FALSE, */
    [D3DRS_RANGEFOGENABLE] = FALSE,
    [D3DRS_STENCILENABLE] = FALSE,
    [D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP,
    [D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP,
    [D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP,
    [D3DRS_STENCILREF] = 0,
    [D3DRS_STENCILMASK] = 0xFFFFFFFF,
    [D3DRS_STENCILFUNC] = D3DCMP_ALWAYS,
    [D3DRS_STENCILWRITEMASK] = 0xFFFFFFFF,
    [D3DRS_TEXTUREFACTOR] = 0xFFFFFFFF,
    [D3DRS_WRAP0] = 0,
    [D3DRS_WRAP1] = 0,
    [D3DRS_WRAP2] = 0,
    [D3DRS_WRAP3] = 0,
    [D3DRS_WRAP4] = 0,
    [D3DRS_WRAP5] = 0,
    [D3DRS_WRAP6] = 0,
    [D3DRS_WRAP7] = 0,
    [D3DRS_CLIPPING] = TRUE,
    [D3DRS_LIGHTING] = TRUE,
    [D3DRS_AMBIENT] = 0,
    [D3DRS_FOGVERTEXMODE] = D3DFOG_NONE,
    [D3DRS_COLORVERTEX] = TRUE,
    [D3DRS_LOCALVIEWER] = TRUE,
    [D3DRS_NORMALIZENORMALS] = FALSE,
    [D3DRS_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1,
    [D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2,
    [D3DRS_AMBIENTMATERIALSOURCE] = D3DMCS_MATERIAL,
    [D3DRS_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL,
    [D3DRS_VERTEXBLEND] = D3DVBF_DISABLE,
    [D3DRS_CLIPPLANEENABLE] = 0,
/*  [D3DRS_SOFTWAREVERTEXPROCESSING] = FALSE, */
    [D3DRS_POINTSIZE] = 0x3F800000,
    [D3DRS_POINTSIZE_MIN] = 0x3F800000,
    [D3DRS_POINTSPRITEENABLE] = FALSE,
    [D3DRS_POINTSCALEENABLE] = FALSE,
    [D3DRS_POINTSCALE_A] = 0x3F800000,
    [D3DRS_POINTSCALE_B] = 0x00000000,
    [D3DRS_POINTSCALE_C] = 0x00000000,
    [D3DRS_MULTISAMPLEANTIALIAS] = TRUE,
    [D3DRS_MULTISAMPLEMASK] = 0xFFFFFFFF,
    [D3DRS_PATCHEDGESTYLE] = D3DPATCHEDGE_DISCRETE,
/*  [D3DRS_PATCHSEGMENTS] = 0x3F800000, */
    [D3DRS_DEBUGMONITORTOKEN] = 0xDEADCAFE,
    [D3DRS_POINTSIZE_MAX] = 0x3F800000, /* depends on cap */
    [D3DRS_INDEXEDVERTEXBLENDENABLE] = FALSE,
    [D3DRS_COLORWRITEENABLE] = 0x0000000f,
    [D3DRS_TWEENFACTOR] = 0x00000000,
    [D3DRS_BLENDOP] = D3DBLENDOP_ADD,
    [D3DRS_POSITIONDEGREE] = D3DDEGREE_CUBIC,
    [D3DRS_NORMALDEGREE] = D3DDEGREE_LINEAR,
    [D3DRS_SCISSORTESTENABLE] = FALSE,
    [D3DRS_SLOPESCALEDEPTHBIAS] = 0,
    [D3DRS_MINTESSELLATIONLEVEL] = 0x3F800000,
    [D3DRS_MAXTESSELLATIONLEVEL] = 0x3F800000,
    [D3DRS_ANTIALIASEDLINEENABLE] = FALSE,
    [D3DRS_ADAPTIVETESS_X] = 0x00000000,
    [D3DRS_ADAPTIVETESS_Y] = 0x00000000,
    [D3DRS_ADAPTIVETESS_Z] = 0x3F800000,
    [D3DRS_ADAPTIVETESS_W] = 0x00000000,
    [D3DRS_ENABLEADAPTIVETESSELLATION] = FALSE,
    [D3DRS_TWOSIDEDSTENCILMODE] = FALSE,
    [D3DRS_CCW_STENCILFAIL] = D3DSTENCILOP_KEEP,
    [D3DRS_CCW_STENCILZFAIL] = D3DSTENCILOP_KEEP,
    [D3DRS_CCW_STENCILPASS] = D3DSTENCILOP_KEEP,
    [D3DRS_CCW_STENCILFUNC] = D3DCMP_ALWAYS,
    [D3DRS_COLORWRITEENABLE1] = 0x0000000F,
    [D3DRS_COLORWRITEENABLE2] = 0x0000000F,
    [D3DRS_COLORWRITEENABLE3] = 0x0000000F,
    [D3DRS_BLENDFACTOR] = 0xFFFFFFFF,
    [D3DRS_SRGBWRITEENABLE] = 0,
    [D3DRS_DEPTHBIAS] = 0,
    [D3DRS_WRAP8] = 0,
    [D3DRS_WRAP9] = 0,
    [D3DRS_WRAP10] = 0,
    [D3DRS_WRAP11] = 0,
    [D3DRS_WRAP12] = 0,
    [D3DRS_WRAP13] = 0,
    [D3DRS_WRAP14] = 0,
    [D3DRS_WRAP15] = 0,
    [D3DRS_SEPARATEALPHABLENDENABLE] = FALSE,
    [D3DRS_SRCBLENDALPHA] = D3DBLEND_ONE,
    [D3DRS_DESTBLENDALPHA] = D3DBLEND_ZERO,
    [D3DRS_BLENDOPALPHA] = D3DBLENDOP_ADD,
    [NINED3DRS_VSPOINTSIZE] = FALSE,
    [NINED3DRS_RTMASK] = 0xf,
    [NINED3DRS_ALPHACOVERAGE] = FALSE,
    [NINED3DRS_MULTISAMPLE] = FALSE
};
static const DWORD nine_tex_stage_state_defaults[NINED3DTSS_LAST + 1] =
{
    [D3DTSS_COLOROP] = D3DTOP_DISABLE,
    [D3DTSS_ALPHAOP] = D3DTOP_DISABLE,
    [D3DTSS_COLORARG1] = D3DTA_TEXTURE,
    [D3DTSS_COLORARG2] = D3DTA_CURRENT,
    [D3DTSS_COLORARG0] = D3DTA_CURRENT,
    [D3DTSS_ALPHAARG1] = D3DTA_TEXTURE,
    [D3DTSS_ALPHAARG2] = D3DTA_CURRENT,
    [D3DTSS_ALPHAARG0] = D3DTA_CURRENT,
    [D3DTSS_RESULTARG] = D3DTA_CURRENT,
    [D3DTSS_BUMPENVMAT00] = 0,
    [D3DTSS_BUMPENVMAT01] = 0,
    [D3DTSS_BUMPENVMAT10] = 0,
    [D3DTSS_BUMPENVMAT11] = 0,
    [D3DTSS_BUMPENVLSCALE] = 0,
    [D3DTSS_BUMPENVLOFFSET] = 0,
    [D3DTSS_TEXCOORDINDEX] = 0,
    [D3DTSS_TEXTURETRANSFORMFLAGS] = D3DTTFF_DISABLE,
};
static const DWORD nine_samp_state_defaults[NINED3DSAMP_LAST + 1] =
{
    [D3DSAMP_ADDRESSU] = D3DTADDRESS_WRAP,
    [D3DSAMP_ADDRESSV] = D3DTADDRESS_WRAP,
    [D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP,
    [D3DSAMP_BORDERCOLOR] = 0,
    [D3DSAMP_MAGFILTER] = D3DTEXF_POINT,
    [D3DSAMP_MINFILTER] = D3DTEXF_POINT,
    [D3DSAMP_MIPFILTER] = D3DTEXF_NONE,
    [D3DSAMP_MIPMAPLODBIAS] = 0,
    [D3DSAMP_MAXMIPLEVEL] = 0,
    [D3DSAMP_MAXANISOTROPY] = 1,
    [D3DSAMP_SRGBTEXTURE] = 0,
    [D3DSAMP_ELEMENTINDEX] = 0,
    [D3DSAMP_DMAPOFFSET] = 0,
    [NINED3DSAMP_MINLOD] = 0,
    [NINED3DSAMP_SHADOW] = 0,
    [NINED3DSAMP_CUBETEX] = 0
};

void nine_state_restore_non_cso(struct NineDevice9 *device)
{
    struct nine_state *state = &device->state;

    state->changed.group = NINE_STATE_ALL;
    state->changed.vtxbuf = (1ULL << device->caps.MaxStreams) - 1;
    state->changed.ucp = (1 << PIPE_MAX_CLIP_PLANES) - 1;
    state->changed.texture = NINE_PS_SAMPLERS_MASK | NINE_VS_SAMPLERS_MASK;
    state->commit |= NINE_STATE_COMMIT_CONST_VS | NINE_STATE_COMMIT_CONST_PS;
}

void
nine_state_set_defaults(struct NineDevice9 *device, const D3DCAPS9 *caps,
                        boolean is_reset)
{
    struct nine_state *state = &device->state;
    unsigned s;

    /* Initialize defaults.
     */
    memcpy(state->rs, nine_render_state_defaults, sizeof(state->rs));

    for (s = 0; s < ARRAY_SIZE(state->ff.tex_stage); ++s) {
        memcpy(&state->ff.tex_stage[s], nine_tex_stage_state_defaults,
               sizeof(state->ff.tex_stage[s]));
        state->ff.tex_stage[s][D3DTSS_TEXCOORDINDEX] = s;
    }
    state->ff.tex_stage[0][D3DTSS_COLOROP] = D3DTOP_MODULATE;
    state->ff.tex_stage[0][D3DTSS_ALPHAOP] = D3DTOP_SELECTARG1;
    memset(&state->bumpmap_vars, 0, sizeof(state->bumpmap_vars));

    for (s = 0; s < ARRAY_SIZE(state->samp); ++s) {
        memcpy(&state->samp[s], nine_samp_state_defaults,
               sizeof(state->samp[s]));
    }

    if (state->vs_const_f)
        memset(state->vs_const_f, 0, device->vs_const_size);
    if (state->ps_const_f)
        memset(state->ps_const_f, 0, device->ps_const_size);

    /* Cap dependent initial state:
     */
    state->rs[D3DRS_POINTSIZE_MAX] = fui(caps->MaxPointSize);

    memcpy(state->rs_advertised, state->rs, sizeof(state->rs));

    /* Set changed flags to initialize driver.
     */
    state->changed.group = NINE_STATE_ALL;
    state->changed.vtxbuf = (1ULL << device->caps.MaxStreams) - 1;
    state->changed.ucp = (1 << PIPE_MAX_CLIP_PLANES) - 1;
    state->changed.texture = NINE_PS_SAMPLERS_MASK | NINE_VS_SAMPLERS_MASK;

    state->ff.changed.transform[0] = ~0;
    state->ff.changed.transform[D3DTS_WORLD / 32] |= 1 << (D3DTS_WORLD % 32);

    if (!is_reset) {
        state->viewport.MinZ = 0.0f;
        state->viewport.MaxZ = 1.0f;
    }

    for (s = 0; s < ARRAY_SIZE(state->changed.sampler); ++s)
        state->changed.sampler[s] = ~0;

    if (!is_reset) {
        state->dummy_vbo_bound_at = -1;
        state->vbo_bound_done = FALSE;
    }
}

void
nine_state_clear(struct nine_state *state, const boolean device)
{
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(state->rt); ++i)
       nine_bind(&state->rt[i], NULL);
    nine_bind(&state->ds, NULL);
    nine_bind(&state->vs, NULL);
    nine_bind(&state->ps, NULL);
    nine_bind(&state->vdecl, NULL);
    for (i = 0; i < PIPE_MAX_ATTRIBS; ++i) {
        nine_bind(&state->stream[i], NULL);
        pipe_resource_reference(&state->vtxbuf[i].buffer, NULL);
    }
    nine_bind(&state->idxbuf, NULL);
    for (i = 0; i < NINE_MAX_SAMPLERS; ++i) {
        if (device &&
            state->texture[i] &&
          --state->texture[i]->bind_count == 0)
            list_delinit(&state->texture[i]->list);
        nine_bind(&state->texture[i], NULL);
    }
}

void
nine_state_init_sw(struct NineDevice9 *device)
{
    struct pipe_context *pipe_sw = device->pipe_sw;
    struct pipe_rasterizer_state rast;
    struct pipe_blend_state blend;
    struct pipe_depth_stencil_alpha_state dsa;
    struct pipe_framebuffer_state fb;

    /* Only used with Streamout */
    memset(&rast, 0, sizeof(rast));
    rast.rasterizer_discard = true;
    rast.point_quad_rasterization = 1; /* to make llvmpipe happy */
    cso_set_rasterizer(device->cso_sw, &rast);

    /* dummy settings */
    memset(&blend, 0, sizeof(blend));
    memset(&dsa, 0, sizeof(dsa));
    memset(&fb, 0, sizeof(fb));
    cso_set_blend(device->cso_sw, &blend);
    cso_set_depth_stencil_alpha(device->cso_sw, &dsa);
    cso_set_framebuffer(device->cso_sw, &fb);
    cso_set_viewport_dims(device->cso_sw, 1.0, 1.0, false);
    cso_set_fragment_shader_handle(device->cso_sw, util_make_empty_fragment_shader(pipe_sw));
}

/* There is duplication with update_vertex_elements.
 * TODO: Share the code */

static void
update_vertex_elements_sw(struct NineDevice9 *device)
{
    struct nine_state *state = &device->state;
    const struct NineVertexDeclaration9 *vdecl = device->state.vdecl;
    const struct NineVertexShader9 *vs;
    unsigned n, b, i;
    int index;
    char vdecl_index_map[16]; /* vs->num_inputs <= 16 */
    char used_streams[device->caps.MaxStreams];
    int dummy_vbo_stream = -1;
    BOOL need_dummy_vbo = FALSE;
    struct pipe_vertex_element ve[PIPE_MAX_ATTRIBS];

    state->stream_usage_mask = 0;
    memset(vdecl_index_map, -1, 16);
    memset(used_streams, 0, device->caps.MaxStreams);
    vs = state->programmable_vs ? device->state.vs : device->ff.vs;

    if (vdecl) {
        for (n = 0; n < vs->num_inputs; ++n) {
            DBG("looking up input %u (usage %u) from vdecl(%p)\n",
                n, vs->input_map[n].ndecl, vdecl);

            for (i = 0; i < vdecl->nelems; i++) {
                if (vdecl->usage_map[i] == vs->input_map[n].ndecl) {
                    vdecl_index_map[n] = i;
                    used_streams[vdecl->elems[i].vertex_buffer_index] = 1;
                    break;
                }
            }
            if (vdecl_index_map[n] < 0)
                need_dummy_vbo = TRUE;
        }
    } else {
        /* No vertex declaration. Likely will never happen in practice,
         * but we need not crash on this */
        need_dummy_vbo = TRUE;
    }

    if (need_dummy_vbo) {
        for (i = 0; i < device->caps.MaxStreams; i++ ) {
            if (!used_streams[i]) {
                dummy_vbo_stream = i;
                break;
            }
        }
    }
    /* there are less vertex shader inputs than stream slots,
     * so if we need a slot for the dummy vbo, we should have found one */
    assert (!need_dummy_vbo || dummy_vbo_stream != -1);

    for (n = 0; n < vs->num_inputs; ++n) {
        index = vdecl_index_map[n];
        if (index >= 0) {
            ve[n] = vdecl->elems[index];
            b = ve[n].vertex_buffer_index;
            state->stream_usage_mask |= 1 << b;
            /* XXX wine just uses 1 here: */
            if (state->stream_freq[b] & D3DSTREAMSOURCE_INSTANCEDATA)
                ve[n].instance_divisor = state->stream_freq[b] & 0x7FFFFF;
        } else {
            /* if the vertex declaration is incomplete compared to what the
             * vertex shader needs, we bind a dummy vbo with 0 0 0 0.
             * This is not precised by the spec, but is the behaviour
             * tested on win */
            ve[n].vertex_buffer_index = dummy_vbo_stream;
            ve[n].src_format = PIPE_FORMAT_R32G32B32A32_FLOAT;
            ve[n].src_offset = 0;
            ve[n].instance_divisor = 0;
        }
    }

    if (state->dummy_vbo_bound_at != dummy_vbo_stream) {
        if (state->dummy_vbo_bound_at >= 0)
            state->changed.vtxbuf |= 1 << state->dummy_vbo_bound_at;
        if (dummy_vbo_stream >= 0) {
            state->changed.vtxbuf |= 1 << dummy_vbo_stream;
            state->vbo_bound_done = FALSE;
        }
        state->dummy_vbo_bound_at = dummy_vbo_stream;
    }

    cso_set_vertex_elements(device->cso_sw, vs->num_inputs, ve);
}

static void
update_vertex_buffers_sw(struct NineDevice9 *device, int start_vertice, int num_vertices)
{
    struct pipe_context *pipe = device->pipe;
    struct pipe_context *pipe_sw = device->pipe_sw;
    struct nine_state *state = &device->state;
    struct pipe_vertex_buffer vtxbuf;
    uint32_t mask = 0xf;
    unsigned i;

    DBG("mask=%x\n", mask);

    assert (state->dummy_vbo_bound_at < 0);
    /* TODO: handle dummy_vbo_bound_at */

    for (i = 0; mask; mask >>= 1, ++i) {
        if (mask & 1) {
            if (state->vtxbuf[i].buffer) {
                struct pipe_resource *buf;
                struct pipe_box box;

                vtxbuf = state->vtxbuf[i];

                DBG("Locking %p (offset %d, length %d)\n", vtxbuf.buffer,
                    vtxbuf.buffer_offset, num_vertices * vtxbuf.stride);

                u_box_1d(vtxbuf.buffer_offset + start_vertice * vtxbuf.stride,
                         num_vertices * vtxbuf.stride, &box);
                buf = vtxbuf.buffer;
                vtxbuf.user_buffer = pipe->transfer_map(pipe, buf, 0, PIPE_TRANSFER_READ, &box,
                                                        &(state->transfers_so[i]));
                vtxbuf.buffer = NULL;
                if (!device->driver_caps.user_sw_vbufs) {
                    u_upload_data(device->vertex_sw_uploader,
                                  0,
                                  box.width,
                                  16,
                                  vtxbuf.user_buffer,
                                  &(vtxbuf.buffer_offset),
                                  &(vtxbuf.buffer));
                    u_upload_unmap(device->vertex_sw_uploader);
                    vtxbuf.user_buffer = NULL;
                }
                pipe_sw->set_vertex_buffers(pipe_sw, i, 1, &vtxbuf);
                if (vtxbuf.buffer)
                    pipe_resource_reference(&vtxbuf.buffer, NULL);
            } else
                pipe_sw->set_vertex_buffers(pipe_sw, i, 1, NULL);
        }
    }
}

static void
update_vs_constants_sw(struct NineDevice9 *device)
{
    struct nine_state *state = &device->state;
    struct pipe_context *pipe_sw = device->pipe_sw;

    DBG("updating\n");

    {
        struct pipe_constant_buffer cb;
        const void *buf;

        cb.buffer = NULL;
        cb.buffer_offset = 0;
        cb.buffer_size = 4096 * sizeof(float[4]);
        cb.user_buffer = state->vs_const_f_swvp;

        if (state->vs->lconstf.ranges) {
            const struct nine_lconstf *lconstf =  &device->state.vs->lconstf;
            const struct nine_range *r = lconstf->ranges;
            unsigned n = 0;
            float *dst = device->state.vs_lconstf_temp;
            float *src = (float *)cb.user_buffer;
            memcpy(dst, src, 8192 * sizeof(float[4]));
            while (r) {
                unsigned p = r->bgn;
                unsigned c = r->end - r->bgn;
                memcpy(&dst[p * 4], &lconstf->data[n * 4], c * 4 * sizeof(float));
                n += c;
                r = r->next;
            }
            cb.user_buffer = dst;
        }

        buf = cb.user_buffer;
        if (!device->driver_caps.user_sw_cbufs) {
            u_upload_data(device->constbuf_sw_uploader,
                          0,
                          cb.buffer_size,
                          16,
                          cb.user_buffer,
                          &(cb.buffer_offset),
                          &(cb.buffer));
            u_upload_unmap(device->constbuf_sw_uploader);
            cb.user_buffer = NULL;
        }

        pipe_sw->set_constant_buffer(pipe_sw, PIPE_SHADER_VERTEX, 0, &cb);
        if (cb.buffer)
            pipe_resource_reference(&cb.buffer, NULL);

        cb.user_buffer = (char *)buf + 4096 * sizeof(float[4]);
        if (!device->driver_caps.user_sw_cbufs) {
            u_upload_data(device->constbuf_sw_uploader,
                          0,
                          cb.buffer_size,
                          16,
                          cb.user_buffer,
                          &(cb.buffer_offset),
                          &(cb.buffer));
            u_upload_unmap(device->constbuf_sw_uploader);
            cb.user_buffer = NULL;
        }

        pipe_sw->set_constant_buffer(pipe_sw, PIPE_SHADER_VERTEX, 1, &cb);
        if (cb.buffer)
            pipe_resource_reference(&cb.buffer, NULL);
    }

    {
        struct pipe_constant_buffer cb;

        cb.buffer = NULL;
        cb.buffer_offset = 0;
        cb.buffer_size = 2048 * sizeof(float[4]);
        cb.user_buffer = state->vs_const_i;

        if (!device->driver_caps.user_sw_cbufs) {
            u_upload_data(device->constbuf_sw_uploader,
                          0,
                          cb.buffer_size,
                          16,
                          cb.user_buffer,
                          &(cb.buffer_offset),
                          &(cb.buffer));
            u_upload_unmap(device->constbuf_sw_uploader);
            cb.user_buffer = NULL;
        }

        pipe_sw->set_constant_buffer(pipe_sw, PIPE_SHADER_VERTEX, 2, &cb);
        if (cb.buffer)
            pipe_resource_reference(&cb.buffer, NULL);
    }

    {
        struct pipe_constant_buffer cb;

        cb.buffer = NULL;
        cb.buffer_offset = 0;
        cb.buffer_size = 512 * sizeof(float[4]);
        cb.user_buffer = state->vs_const_b;

        if (!device->driver_caps.user_sw_cbufs) {
            u_upload_data(device->constbuf_sw_uploader,
                          0,
                          cb.buffer_size,
                          16,
                          cb.user_buffer,
                          &(cb.buffer_offset),
                          &(cb.buffer));
            u_upload_unmap(device->constbuf_sw_uploader);
            cb.user_buffer = NULL;
        }

        pipe_sw->set_constant_buffer(pipe_sw, PIPE_SHADER_VERTEX, 3, &cb);
        if (cb.buffer)
            pipe_resource_reference(&cb.buffer, NULL);
    }

    {
        struct pipe_constant_buffer cb;
        const D3DVIEWPORT9 *vport = &device->state.viewport;
        float viewport_data[8] = {(float)vport->Width * 0.5f,
            (float)vport->Height * -0.5f, vport->MaxZ - vport->MinZ, 0.f,
            (float)vport->Width * 0.5f + (float)vport->X,
            (float)vport->Height * 0.5f + (float)vport->Y,
            vport->MinZ, 0.f};

        cb.buffer = NULL;
        cb.buffer_offset = 0;
        cb.buffer_size = 2 * sizeof(float[4]);
        cb.user_buffer = viewport_data;

        {
            u_upload_data(device->constbuf_sw_uploader,
                          0,
                          cb.buffer_size,
                          16,
                          cb.user_buffer,
                          &(cb.buffer_offset),
                          &(cb.buffer));
            u_upload_unmap(device->constbuf_sw_uploader);
            cb.user_buffer = NULL;
        }

        pipe_sw->set_constant_buffer(pipe_sw, PIPE_SHADER_VERTEX, 4, &cb);
        if (cb.buffer)
            pipe_resource_reference(&cb.buffer, NULL);
    }

}

void
nine_state_prepare_draw_sw(struct NineDevice9 *device, struct NineVertexDeclaration9 *vdecl_out,
                           int start_vertice, int num_vertices, struct pipe_stream_output_info *so)
{
    struct nine_state *state = &device->state;

    struct NineVertexShader9 *vs = state->programmable_vs ? device->state.vs : device->ff.vs;

    assert(state->programmable_vs);

    DBG("Preparing draw\n");
    cso_set_vertex_shader_handle(device->cso_sw,
                                 NineVertexShader9_GetVariantProcessVertices(vs, vdecl_out, so));
    update_vertex_elements_sw(device);
    update_vertex_buffers_sw(device, start_vertice, num_vertices);
    update_vs_constants_sw(device);
    DBG("Preparation succeeded\n");
}

void
nine_state_after_draw_sw(struct NineDevice9 *device)
{
    struct nine_state *state = &device->state;
    struct pipe_context *pipe = device->pipe;
    struct pipe_context *pipe_sw = device->pipe_sw;
    int i;

    for (i = 0; i < 4; i++) {
        pipe_sw->set_vertex_buffers(pipe_sw, i, 1, NULL);
        if (state->transfers_so[i])
            pipe->transfer_unmap(pipe, state->transfers_so[i]);
        state->transfers_so[i] = NULL;
    }
}

void
nine_state_destroy_sw(struct NineDevice9 *device)
{
    (void) device;
    /* Everything destroyed with cso */
}

/*
static const DWORD nine_render_states_pixel[] =
{
    D3DRS_ALPHABLENDENABLE,
    D3DRS_ALPHAFUNC,
    D3DRS_ALPHAREF,
    D3DRS_ALPHATESTENABLE,
    D3DRS_ANTIALIASEDLINEENABLE,
    D3DRS_BLENDFACTOR,
    D3DRS_BLENDOP,
    D3DRS_BLENDOPALPHA,
    D3DRS_CCW_STENCILFAIL,
    D3DRS_CCW_STENCILPASS,
    D3DRS_CCW_STENCILZFAIL,
    D3DRS_COLORWRITEENABLE,
    D3DRS_COLORWRITEENABLE1,
    D3DRS_COLORWRITEENABLE2,
    D3DRS_COLORWRITEENABLE3,
    D3DRS_DEPTHBIAS,
    D3DRS_DESTBLEND,
    D3DRS_DESTBLENDALPHA,
    D3DRS_DITHERENABLE,
    D3DRS_FILLMODE,
    D3DRS_FOGDENSITY,
    D3DRS_FOGEND,
    D3DRS_FOGSTART,
    D3DRS_LASTPIXEL,
    D3DRS_SCISSORTESTENABLE,
    D3DRS_SEPARATEALPHABLENDENABLE,
    D3DRS_SHADEMODE,
    D3DRS_SLOPESCALEDEPTHBIAS,
    D3DRS_SRCBLEND,
    D3DRS_SRCBLENDALPHA,
    D3DRS_SRGBWRITEENABLE,
    D3DRS_STENCILENABLE,
    D3DRS_STENCILFAIL,
    D3DRS_STENCILFUNC,
    D3DRS_STENCILMASK,
    D3DRS_STENCILPASS,
    D3DRS_STENCILREF,
    D3DRS_STENCILWRITEMASK,
    D3DRS_STENCILZFAIL,
    D3DRS_TEXTUREFACTOR,
    D3DRS_TWOSIDEDSTENCILMODE,
    D3DRS_WRAP0,
    D3DRS_WRAP1,
    D3DRS_WRAP10,
    D3DRS_WRAP11,
    D3DRS_WRAP12,
    D3DRS_WRAP13,
    D3DRS_WRAP14,
    D3DRS_WRAP15,
    D3DRS_WRAP2,
    D3DRS_WRAP3,
    D3DRS_WRAP4,
    D3DRS_WRAP5,
    D3DRS_WRAP6,
    D3DRS_WRAP7,
    D3DRS_WRAP8,
    D3DRS_WRAP9,
    D3DRS_ZENABLE,
    D3DRS_ZFUNC,
    D3DRS_ZWRITEENABLE
};
*/
const uint32_t nine_render_states_pixel[(NINED3DRS_LAST + 31) / 32] =
{
    0x0f99c380, 0x1ff00070, 0x00000000, 0x00000000,
    0x000000ff, 0xde01c900, 0x0003ffcf
};

/*
static const DWORD nine_render_states_vertex[] =
{
    D3DRS_ADAPTIVETESS_W,
    D3DRS_ADAPTIVETESS_X,
    D3DRS_ADAPTIVETESS_Y,
    D3DRS_ADAPTIVETESS_Z,
    D3DRS_AMBIENT,
    D3DRS_AMBIENTMATERIALSOURCE,
    D3DRS_CLIPPING,
    D3DRS_CLIPPLANEENABLE,
    D3DRS_COLORVERTEX,
    D3DRS_CULLMODE,
    D3DRS_DIFFUSEMATERIALSOURCE,
    D3DRS_EMISSIVEMATERIALSOURCE,
    D3DRS_ENABLEADAPTIVETESSELLATION,
    D3DRS_FOGCOLOR,
    D3DRS_FOGDENSITY,
    D3DRS_FOGENABLE,
    D3DRS_FOGEND,
    D3DRS_FOGSTART,
    D3DRS_FOGTABLEMODE,
    D3DRS_FOGVERTEXMODE,
    D3DRS_INDEXEDVERTEXBLENDENABLE,
    D3DRS_LIGHTING,
    D3DRS_LOCALVIEWER,
    D3DRS_MAXTESSELLATIONLEVEL,
    D3DRS_MINTESSELLATIONLEVEL,
    D3DRS_MULTISAMPLEANTIALIAS,
    D3DRS_MULTISAMPLEMASK,
    D3DRS_NORMALDEGREE,
    D3DRS_NORMALIZENORMALS,
    D3DRS_PATCHEDGESTYLE,
    D3DRS_POINTSCALE_A,
    D3DRS_POINTSCALE_B,
    D3DRS_POINTSCALE_C,
    D3DRS_POINTSCALEENABLE,
    D3DRS_POINTSIZE,
    D3DRS_POINTSIZE_MAX,
    D3DRS_POINTSIZE_MIN,
    D3DRS_POINTSPRITEENABLE,
    D3DRS_POSITIONDEGREE,
    D3DRS_RANGEFOGENABLE,
    D3DRS_SHADEMODE,
    D3DRS_SPECULARENABLE,
    D3DRS_SPECULARMATERIALSOURCE,
    D3DRS_TWEENFACTOR,
    D3DRS_VERTEXBLEND
};
*/
const uint32_t nine_render_states_vertex[(NINED3DRS_LAST + 31) / 32] =
{
    0x30400200, 0x0001007c, 0x00000000, 0x00000000,
    0xfd9efb00, 0x01fc34cf, 0x00000000
};

/* TODO: put in the right values */
const uint32_t nine_render_state_group[NINED3DRS_LAST + 1] =
{
    [D3DRS_ZENABLE] = NINE_STATE_DSA | NINE_STATE_MULTISAMPLE,
    [D3DRS_FILLMODE] = NINE_STATE_RASTERIZER,
    [D3DRS_SHADEMODE] = NINE_STATE_RASTERIZER,
    [D3DRS_ZWRITEENABLE] = NINE_STATE_DSA,
    [D3DRS_ALPHATESTENABLE] = NINE_STATE_DSA,
    [D3DRS_LASTPIXEL] = NINE_STATE_RASTERIZER,
    [D3DRS_SRCBLEND] = NINE_STATE_BLEND,
    [D3DRS_DESTBLEND] = NINE_STATE_BLEND,
    [D3DRS_CULLMODE] = NINE_STATE_RASTERIZER,
    [D3DRS_ZFUNC] = NINE_STATE_DSA,
    [D3DRS_ALPHAREF] = NINE_STATE_DSA,
    [D3DRS_ALPHAFUNC] = NINE_STATE_DSA,
    [D3DRS_DITHERENABLE] = NINE_STATE_BLEND,
    [D3DRS_ALPHABLENDENABLE] = NINE_STATE_BLEND,
    [D3DRS_FOGENABLE] = NINE_STATE_FF_OTHER | NINE_STATE_FOG_SHADER | NINE_STATE_PS_CONST,
    [D3DRS_SPECULARENABLE] = NINE_STATE_FF_LIGHTING,
    [D3DRS_FOGCOLOR] = NINE_STATE_FF_OTHER | NINE_STATE_PS_CONST,
    [D3DRS_FOGTABLEMODE] = NINE_STATE_FF_OTHER | NINE_STATE_FOG_SHADER | NINE_STATE_PS_CONST,
    [D3DRS_FOGSTART] = NINE_STATE_FF_OTHER | NINE_STATE_PS_CONST,
    [D3DRS_FOGEND] = NINE_STATE_FF_OTHER | NINE_STATE_PS_CONST,
    [D3DRS_FOGDENSITY] = NINE_STATE_FF_OTHER | NINE_STATE_PS_CONST,
    [D3DRS_RANGEFOGENABLE] = NINE_STATE_FF_OTHER,
    [D3DRS_STENCILENABLE] = NINE_STATE_DSA | NINE_STATE_MULTISAMPLE,
    [D3DRS_STENCILFAIL] = NINE_STATE_DSA,
    [D3DRS_STENCILZFAIL] = NINE_STATE_DSA,
    [D3DRS_STENCILPASS] = NINE_STATE_DSA,
    [D3DRS_STENCILFUNC] = NINE_STATE_DSA,
    [D3DRS_STENCILREF] = NINE_STATE_STENCIL_REF,
    [D3DRS_STENCILMASK] = NINE_STATE_DSA,
    [D3DRS_STENCILWRITEMASK] = NINE_STATE_DSA,
    [D3DRS_TEXTUREFACTOR] = NINE_STATE_FF_PSSTAGES,
    [D3DRS_WRAP0] = NINE_STATE_UNHANDLED, /* cylindrical wrap is crazy */
    [D3DRS_WRAP1] = NINE_STATE_UNHANDLED,
    [D3DRS_WRAP2] = NINE_STATE_UNHANDLED,
    [D3DRS_WRAP3] = NINE_STATE_UNHANDLED,
    [D3DRS_WRAP4] = NINE_STATE_UNHANDLED,
    [D3DRS_WRAP5] = NINE_STATE_UNHANDLED,
    [D3DRS_WRAP6] = NINE_STATE_UNHANDLED,
    [D3DRS_WRAP7] = NINE_STATE_UNHANDLED,
    [D3DRS_CLIPPING] = 0, /* software vertex processing only */
    [D3DRS_LIGHTING] = NINE_STATE_FF_LIGHTING,
    [D3DRS_AMBIENT] = NINE_STATE_FF_LIGHTING | NINE_STATE_FF_MATERIAL,
    [D3DRS_FOGVERTEXMODE] = NINE_STATE_FF_OTHER,
    [D3DRS_COLORVERTEX] = NINE_STATE_FF_LIGHTING,
    [D3DRS_LOCALVIEWER] = NINE_STATE_FF_LIGHTING,
    [D3DRS_NORMALIZENORMALS] = NINE_STATE_FF_OTHER,
    [D3DRS_DIFFUSEMATERIALSOURCE] = NINE_STATE_FF_LIGHTING,
    [D3DRS_SPECULARMATERIALSOURCE] = NINE_STATE_FF_LIGHTING,
    [D3DRS_AMBIENTMATERIALSOURCE] = NINE_STATE_FF_LIGHTING,
    [D3DRS_EMISSIVEMATERIALSOURCE] = NINE_STATE_FF_LIGHTING,
    [D3DRS_VERTEXBLEND] = NINE_STATE_FF_OTHER,
    [D3DRS_CLIPPLANEENABLE] = NINE_STATE_RASTERIZER,
    [D3DRS_POINTSIZE] = NINE_STATE_RASTERIZER,
    [D3DRS_POINTSIZE_MIN] = NINE_STATE_RASTERIZER | NINE_STATE_POINTSIZE_SHADER,
    [D3DRS_POINTSPRITEENABLE] = NINE_STATE_RASTERIZER,
    [D3DRS_POINTSCALEENABLE] = NINE_STATE_FF_OTHER,
    [D3DRS_POINTSCALE_A] = NINE_STATE_FF_OTHER,
    [D3DRS_POINTSCALE_B] = NINE_STATE_FF_OTHER,
    [D3DRS_POINTSCALE_C] = NINE_STATE_FF_OTHER,
    [D3DRS_MULTISAMPLEANTIALIAS] = NINE_STATE_MULTISAMPLE,
    [D3DRS_MULTISAMPLEMASK] = NINE_STATE_SAMPLE_MASK,
    [D3DRS_PATCHEDGESTYLE] = NINE_STATE_UNHANDLED,
    [D3DRS_DEBUGMONITORTOKEN] = NINE_STATE_UNHANDLED,
    [D3DRS_POINTSIZE_MAX] = NINE_STATE_RASTERIZER | NINE_STATE_POINTSIZE_SHADER,
    [D3DRS_INDEXEDVERTEXBLENDENABLE] = NINE_STATE_FF_OTHER,
    [D3DRS_COLORWRITEENABLE] = NINE_STATE_BLEND,
    [D3DRS_TWEENFACTOR] = NINE_STATE_FF_OTHER,
    [D3DRS_BLENDOP] = NINE_STATE_BLEND,
    [D3DRS_POSITIONDEGREE] = NINE_STATE_UNHANDLED,
    [D3DRS_NORMALDEGREE] = NINE_STATE_UNHANDLED,
    [D3DRS_SCISSORTESTENABLE] = NINE_STATE_RASTERIZER,
    [D3DRS_SLOPESCALEDEPTHBIAS] = NINE_STATE_RASTERIZER,
    [D3DRS_ANTIALIASEDLINEENABLE] = NINE_STATE_RASTERIZER,
    [D3DRS_MINTESSELLATIONLEVEL] = NINE_STATE_UNHANDLED,
    [D3DRS_MAXTESSELLATIONLEVEL] = NINE_STATE_UNHANDLED,
    [D3DRS_ADAPTIVETESS_X] = NINE_STATE_UNHANDLED,
    [D3DRS_ADAPTIVETESS_Y] = NINE_STATE_UNHANDLED,
    [D3DRS_ADAPTIVETESS_Z] = NINE_STATE_UNHANDLED,
    [D3DRS_ADAPTIVETESS_W] = NINE_STATE_UNHANDLED,
    [D3DRS_ENABLEADAPTIVETESSELLATION] = NINE_STATE_UNHANDLED,
    [D3DRS_TWOSIDEDSTENCILMODE] = NINE_STATE_DSA,
    [D3DRS_CCW_STENCILFAIL] = NINE_STATE_DSA,
    [D3DRS_CCW_STENCILZFAIL] = NINE_STATE_DSA,
    [D3DRS_CCW_STENCILPASS] = NINE_STATE_DSA,
    [D3DRS_CCW_STENCILFUNC] = NINE_STATE_DSA,
    [D3DRS_COLORWRITEENABLE1] = NINE_STATE_BLEND,
    [D3DRS_COLORWRITEENABLE2] = NINE_STATE_BLEND,
    [D3DRS_COLORWRITEENABLE3] = NINE_STATE_BLEND,
    [D3DRS_BLENDFACTOR] = NINE_STATE_BLEND_COLOR,
    [D3DRS_SRGBWRITEENABLE] = NINE_STATE_FB,
    [D3DRS_DEPTHBIAS] = NINE_STATE_RASTERIZER,
    [D3DRS_WRAP8] = NINE_STATE_UNHANDLED, /* cylwrap has to be done via GP */
    [D3DRS_WRAP9] = NINE_STATE_UNHANDLED,
    [D3DRS_WRAP10] = NINE_STATE_UNHANDLED,
    [D3DRS_WRAP11] = NINE_STATE_UNHANDLED,
    [D3DRS_WRAP12] = NINE_STATE_UNHANDLED,
    [D3DRS_WRAP13] = NINE_STATE_UNHANDLED,
    [D3DRS_WRAP14] = NINE_STATE_UNHANDLED,
    [D3DRS_WRAP15] = NINE_STATE_UNHANDLED,
    [D3DRS_SEPARATEALPHABLENDENABLE] = NINE_STATE_BLEND,
    [D3DRS_SRCBLENDALPHA] = NINE_STATE_BLEND,
    [D3DRS_DESTBLENDALPHA] = NINE_STATE_BLEND,
    [D3DRS_BLENDOPALPHA] = NINE_STATE_BLEND
};

/* Misc */

D3DMATRIX *
nine_state_access_transform(struct nine_state *state, D3DTRANSFORMSTATETYPE t,
                            boolean alloc)
{
    static D3DMATRIX Identity = { .m[0] = { 1, 0, 0, 0 },
                                  .m[1] = { 0, 1, 0, 0 },
                                  .m[2] = { 0, 0, 1, 0 },
                                  .m[3] = { 0, 0, 0, 1 } };
    unsigned index;

    switch (t) {
    case D3DTS_VIEW: index = 0; break;
    case D3DTS_PROJECTION: index = 1; break;
    case D3DTS_TEXTURE0: index = 2; break;
    case D3DTS_TEXTURE1: index = 3; break;
    case D3DTS_TEXTURE2: index = 4; break;
    case D3DTS_TEXTURE3: index = 5; break;
    case D3DTS_TEXTURE4: index = 6; break;
    case D3DTS_TEXTURE5: index = 7; break;
    case D3DTS_TEXTURE6: index = 8; break;
    case D3DTS_TEXTURE7: index = 9; break;
    default:
        if (!(t >= D3DTS_WORLDMATRIX(0) && t <= D3DTS_WORLDMATRIX(255)))
            return NULL;
        index = 10 + (t - D3DTS_WORLDMATRIX(0));
        break;
    }

    if (index >= state->ff.num_transforms) {
        unsigned N = index + 1;
        unsigned n = state->ff.num_transforms;

        if (!alloc)
            return &Identity;
        state->ff.transform = REALLOC(state->ff.transform,
                                      n * sizeof(D3DMATRIX),
                                      N * sizeof(D3DMATRIX));
        for (; n < N; ++n)
            state->ff.transform[n] = Identity;
        state->ff.num_transforms = N;
    }
    return &state->ff.transform[index];
}

#define D3DRS_TO_STRING_CASE(n) case D3DRS_##n: return "D3DRS_"#n
const char *nine_d3drs_to_string(DWORD State)
{
    switch (State) {
    D3DRS_TO_STRING_CASE(ZENABLE);
    D3DRS_TO_STRING_CASE(FILLMODE);
    D3DRS_TO_STRING_CASE(SHADEMODE);
    D3DRS_TO_STRING_CASE(ZWRITEENABLE);
    D3DRS_TO_STRING_CASE(ALPHATESTENABLE);
    D3DRS_TO_STRING_CASE(LASTPIXEL);
    D3DRS_TO_STRING_CASE(SRCBLEND);
    D3DRS_TO_STRING_CASE(DESTBLEND);
    D3DRS_TO_STRING_CASE(CULLMODE);
    D3DRS_TO_STRING_CASE(ZFUNC);
    D3DRS_TO_STRING_CASE(ALPHAREF);
    D3DRS_TO_STRING_CASE(ALPHAFUNC);
    D3DRS_TO_STRING_CASE(DITHERENABLE);
    D3DRS_TO_STRING_CASE(ALPHABLENDENABLE);
    D3DRS_TO_STRING_CASE(FOGENABLE);
    D3DRS_TO_STRING_CASE(SPECULARENABLE);
    D3DRS_TO_STRING_CASE(FOGCOLOR);
    D3DRS_TO_STRING_CASE(FOGTABLEMODE);
    D3DRS_TO_STRING_CASE(FOGSTART);
    D3DRS_TO_STRING_CASE(FOGEND);
    D3DRS_TO_STRING_CASE(FOGDENSITY);
    D3DRS_TO_STRING_CASE(RANGEFOGENABLE);
    D3DRS_TO_STRING_CASE(STENCILENABLE);
    D3DRS_TO_STRING_CASE(STENCILFAIL);
    D3DRS_TO_STRING_CASE(STENCILZFAIL);
    D3DRS_TO_STRING_CASE(STENCILPASS);
    D3DRS_TO_STRING_CASE(STENCILFUNC);
    D3DRS_TO_STRING_CASE(STENCILREF);
    D3DRS_TO_STRING_CASE(STENCILMASK);
    D3DRS_TO_STRING_CASE(STENCILWRITEMASK);
    D3DRS_TO_STRING_CASE(TEXTUREFACTOR);
    D3DRS_TO_STRING_CASE(WRAP0);
    D3DRS_TO_STRING_CASE(WRAP1);
    D3DRS_TO_STRING_CASE(WRAP2);
    D3DRS_TO_STRING_CASE(WRAP3);
    D3DRS_TO_STRING_CASE(WRAP4);
    D3DRS_TO_STRING_CASE(WRAP5);
    D3DRS_TO_STRING_CASE(WRAP6);
    D3DRS_TO_STRING_CASE(WRAP7);
    D3DRS_TO_STRING_CASE(CLIPPING);
    D3DRS_TO_STRING_CASE(LIGHTING);
    D3DRS_TO_STRING_CASE(AMBIENT);
    D3DRS_TO_STRING_CASE(FOGVERTEXMODE);
    D3DRS_TO_STRING_CASE(COLORVERTEX);
    D3DRS_TO_STRING_CASE(LOCALVIEWER);
    D3DRS_TO_STRING_CASE(NORMALIZENORMALS);
    D3DRS_TO_STRING_CASE(DIFFUSEMATERIALSOURCE);
    D3DRS_TO_STRING_CASE(SPECULARMATERIALSOURCE);
    D3DRS_TO_STRING_CASE(AMBIENTMATERIALSOURCE);
    D3DRS_TO_STRING_CASE(EMISSIVEMATERIALSOURCE);
    D3DRS_TO_STRING_CASE(VERTEXBLEND);
    D3DRS_TO_STRING_CASE(CLIPPLANEENABLE);
    D3DRS_TO_STRING_CASE(POINTSIZE);
    D3DRS_TO_STRING_CASE(POINTSIZE_MIN);
    D3DRS_TO_STRING_CASE(POINTSPRITEENABLE);
    D3DRS_TO_STRING_CASE(POINTSCALEENABLE);
    D3DRS_TO_STRING_CASE(POINTSCALE_A);
    D3DRS_TO_STRING_CASE(POINTSCALE_B);
    D3DRS_TO_STRING_CASE(POINTSCALE_C);
    D3DRS_TO_STRING_CASE(MULTISAMPLEANTIALIAS);
    D3DRS_TO_STRING_CASE(MULTISAMPLEMASK);
    D3DRS_TO_STRING_CASE(PATCHEDGESTYLE);
    D3DRS_TO_STRING_CASE(DEBUGMONITORTOKEN);
    D3DRS_TO_STRING_CASE(POINTSIZE_MAX);
    D3DRS_TO_STRING_CASE(INDEXEDVERTEXBLENDENABLE);
    D3DRS_TO_STRING_CASE(COLORWRITEENABLE);
    D3DRS_TO_STRING_CASE(TWEENFACTOR);
    D3DRS_TO_STRING_CASE(BLENDOP);
    D3DRS_TO_STRING_CASE(POSITIONDEGREE);
    D3DRS_TO_STRING_CASE(NORMALDEGREE);
    D3DRS_TO_STRING_CASE(SCISSORTESTENABLE);
    D3DRS_TO_STRING_CASE(SLOPESCALEDEPTHBIAS);
    D3DRS_TO_STRING_CASE(ANTIALIASEDLINEENABLE);
    D3DRS_TO_STRING_CASE(MINTESSELLATIONLEVEL);
    D3DRS_TO_STRING_CASE(MAXTESSELLATIONLEVEL);
    D3DRS_TO_STRING_CASE(ADAPTIVETESS_X);
    D3DRS_TO_STRING_CASE(ADAPTIVETESS_Y);
    D3DRS_TO_STRING_CASE(ADAPTIVETESS_Z);
    D3DRS_TO_STRING_CASE(ADAPTIVETESS_W);
    D3DRS_TO_STRING_CASE(ENABLEADAPTIVETESSELLATION);
    D3DRS_TO_STRING_CASE(TWOSIDEDSTENCILMODE);
    D3DRS_TO_STRING_CASE(CCW_STENCILFAIL);
    D3DRS_TO_STRING_CASE(CCW_STENCILZFAIL);
    D3DRS_TO_STRING_CASE(CCW_STENCILPASS);
    D3DRS_TO_STRING_CASE(CCW_STENCILFUNC);
    D3DRS_TO_STRING_CASE(COLORWRITEENABLE1);
    D3DRS_TO_STRING_CASE(COLORWRITEENABLE2);
    D3DRS_TO_STRING_CASE(COLORWRITEENABLE3);
    D3DRS_TO_STRING_CASE(BLENDFACTOR);
    D3DRS_TO_STRING_CASE(SRGBWRITEENABLE);
    D3DRS_TO_STRING_CASE(DEPTHBIAS);
    D3DRS_TO_STRING_CASE(WRAP8);
    D3DRS_TO_STRING_CASE(WRAP9);
    D3DRS_TO_STRING_CASE(WRAP10);
    D3DRS_TO_STRING_CASE(WRAP11);
    D3DRS_TO_STRING_CASE(WRAP12);
    D3DRS_TO_STRING_CASE(WRAP13);
    D3DRS_TO_STRING_CASE(WRAP14);
    D3DRS_TO_STRING_CASE(WRAP15);
    D3DRS_TO_STRING_CASE(SEPARATEALPHABLENDENABLE);
    D3DRS_TO_STRING_CASE(SRCBLENDALPHA);
    D3DRS_TO_STRING_CASE(DESTBLENDALPHA);
    D3DRS_TO_STRING_CASE(BLENDOPALPHA);
    default:
        return "(invalid)";
    }
}
