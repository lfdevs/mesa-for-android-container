/*
 * Copyright 2018-2019 Alyssa Rosenzweig
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
 */

#include "pan_bo.h"
#include "pan_context.h"
#include "pan_cmdstream.h"
#include "pan_util.h"
#include "panfrost-quirks.h"

static struct mali_rt_format
panfrost_mfbd_format(struct pipe_surface *surf)
{
        /* Explode details on the format */

        const struct util_format_description *desc =
                util_format_description(surf->format);

        /* The swizzle for rendering is inverted from texturing */

        unsigned char swizzle[4];
        panfrost_invert_swizzle(desc->swizzle, swizzle);

        /* Fill in accordingly, defaulting to 8-bit UNORM */

        struct mali_rt_format fmt = {
                .unk1 = 0x4000000,
                .unk2 = 0x1,
                .nr_channels = MALI_POSITIVE(desc->nr_channels),
                .unk3 = 0x4,
                .flags = 0x2,
                .swizzle = panfrost_translate_swizzle_4(swizzle),
                .no_preload = true
        };

        if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB)
                fmt.flags |= MALI_MFBD_FORMAT_SRGB;

        /* sRGB handled as a dedicated flag */
        enum pipe_format linearized = util_format_linear(surf->format);

        /* If RGB, we're good to go */
        if (util_format_is_unorm8(desc))
                return fmt;

        /* Set flags for alternative formats */

        switch (linearized) {
        case PIPE_FORMAT_B5G6R5_UNORM:
                fmt.unk1 = 0x14000000;
                fmt.nr_channels = MALI_POSITIVE(2);
                fmt.unk3 |= 0x1;
                break;

        case PIPE_FORMAT_A4B4G4R4_UNORM:
        case PIPE_FORMAT_B4G4R4A4_UNORM:
        case PIPE_FORMAT_R4G4B4A4_UNORM:
                fmt.unk1 = 0x10000000;
                fmt.unk3 = 0x5;
                fmt.nr_channels = MALI_POSITIVE(1);
                break;

        case PIPE_FORMAT_R10G10B10A2_UNORM:
        case PIPE_FORMAT_B10G10R10A2_UNORM:
        case PIPE_FORMAT_R10G10B10X2_UNORM:
        case PIPE_FORMAT_B10G10R10X2_UNORM:
                fmt.unk1 = 0x08000000;
                fmt.unk3 = 0x6;
                fmt.nr_channels = MALI_POSITIVE(1);
                break;

        case PIPE_FORMAT_B5G5R5A1_UNORM:
        case PIPE_FORMAT_R5G5B5A1_UNORM:
        case PIPE_FORMAT_B5G5R5X1_UNORM:
                fmt.unk1 = 0x18000000;
                fmt.unk3 = 0x7;
                fmt.nr_channels = MALI_POSITIVE(2);
                break;

        /* Generic 8-bit */
        case PIPE_FORMAT_R8_UINT:
        case PIPE_FORMAT_R8_SINT:
                fmt.unk1 = 0x80000000;
                fmt.unk3 = 0x0;
                fmt.nr_channels = MALI_POSITIVE(1);
                break;

        /* Generic 32-bit */
        case PIPE_FORMAT_R11G11B10_FLOAT:
        case PIPE_FORMAT_R8G8B8A8_UINT:
        case PIPE_FORMAT_R8G8B8A8_SINT:
        case PIPE_FORMAT_R16G16_FLOAT:
        case PIPE_FORMAT_R16G16_UINT:
        case PIPE_FORMAT_R16G16_SINT:
        case PIPE_FORMAT_R32_FLOAT:
        case PIPE_FORMAT_R32_UINT:
        case PIPE_FORMAT_R32_SINT:
        case PIPE_FORMAT_R10G10B10A2_UINT:
                fmt.unk1 = 0x88000000;
                fmt.unk3 = 0x0;
                fmt.nr_channels = MALI_POSITIVE(4);
                break;

        /* Generic 16-bit */
        case PIPE_FORMAT_R8G8_UINT:
        case PIPE_FORMAT_R8G8_SINT:
        case PIPE_FORMAT_R16_FLOAT:
        case PIPE_FORMAT_R16_UINT:
        case PIPE_FORMAT_R16_SINT:
                fmt.unk1 = 0x84000000;
                fmt.unk3 = 0x0;
                fmt.nr_channels = MALI_POSITIVE(2);
                break;

        /* Generic 64-bit */
        case PIPE_FORMAT_R32G32_FLOAT:
        case PIPE_FORMAT_R32G32_SINT:
        case PIPE_FORMAT_R32G32_UINT:
        case PIPE_FORMAT_R16G16B16A16_FLOAT:
        case PIPE_FORMAT_R16G16B16A16_SINT:
        case PIPE_FORMAT_R16G16B16A16_UINT:
                fmt.unk1 = 0x8c000000;
                fmt.unk3 = 0x1;
                fmt.nr_channels = MALI_POSITIVE(2);
                break;

        /* Generic 128-bit */
        case PIPE_FORMAT_R32G32B32A32_FLOAT:
        case PIPE_FORMAT_R32G32B32A32_SINT:
        case PIPE_FORMAT_R32G32B32A32_UINT:
                fmt.unk1 = 0x90000000;
                fmt.unk3 = 0x1;
                fmt.nr_channels = MALI_POSITIVE(4);
                break;

        default:
                unreachable("Invalid format rendering");
        }

        return fmt;
}


static void
panfrost_mfbd_clear(
        struct panfrost_batch *batch,
        struct mali_framebuffer *fb,
        struct mali_framebuffer_extra *fbx,
        struct mali_render_target *rts,
        unsigned rt_count)
{
        struct panfrost_context *ctx = batch->ctx;
        struct pipe_context *gallium = (struct pipe_context *) ctx;
        struct panfrost_device *dev = pan_device(gallium->screen);

        for (unsigned i = 0; i < rt_count; ++i) {
                if (!(batch->clear & (PIPE_CLEAR_COLOR0 << i)))
                        continue;

                rts[i].clear_color_1 = batch->clear_color[i][0];
                rts[i].clear_color_2 = batch->clear_color[i][1];
                rts[i].clear_color_3 = batch->clear_color[i][2];
                rts[i].clear_color_4 = batch->clear_color[i][3];
        }

        if (batch->clear & PIPE_CLEAR_DEPTH) {
                fb->clear_depth = batch->clear_depth;
        }

        if (batch->clear & PIPE_CLEAR_STENCIL) {
                fb->clear_stencil = batch->clear_stencil;
        }

        if (dev->quirks & IS_BIFROST) {
                fbx->clear_color_1 = batch->clear_color[0][0];
                fbx->clear_color_2 = 0xc0000000 | (fbx->clear_color_1 & 0xffff); /* WTF? */
        }
}

static void
panfrost_mfbd_set_cbuf(
        struct mali_render_target *rt,
        struct pipe_surface *surf)
{
        struct panfrost_resource *rsrc = pan_resource(surf->texture);
        struct panfrost_device *dev = pan_device(surf->context->screen);
        bool is_bifrost = dev->quirks & IS_BIFROST;

        unsigned level = surf->u.tex.level;
        unsigned first_layer = surf->u.tex.first_layer;
        assert(surf->u.tex.last_layer == first_layer);
        int stride = rsrc->slices[level].stride;

        /* Only set layer_stride for layered MSAA rendering  */

        unsigned nr_samples = surf->texture->nr_samples;
        unsigned layer_stride = (nr_samples > 1) ? rsrc->slices[level].size0 : 0;

        mali_ptr base = panfrost_get_texture_address(rsrc, level, first_layer, 0);

        rt->format = panfrost_mfbd_format(surf);

        if (layer_stride)
                rt->format.msaa = MALI_MSAA_LAYERED;
        else if (surf->nr_samples)
                rt->format.msaa = MALI_MSAA_AVERAGE;
        else
                rt->format.msaa = MALI_MSAA_SINGLE;

        /* Now, we set the layout specific pieces */

        if (rsrc->layout == MALI_TEXTURE_LINEAR) {
                if (is_bifrost) {
                        rt->format.unk4 = 0x1;
                } else {
                        rt->format.block = MALI_BLOCK_LINEAR;
                }

                rt->framebuffer = base;
                rt->framebuffer_stride = stride / 16;
                rt->layer_stride = layer_stride;
        } else if (rsrc->layout == MALI_TEXTURE_TILED) {
                if (is_bifrost) {
                        rt->format.unk3 |= 0x8;
                } else {
                        rt->format.block = MALI_BLOCK_TILED;
                }

                rt->framebuffer = base;
                rt->framebuffer_stride = stride;
                rt->layer_stride = layer_stride;
        } else if (rsrc->layout == MALI_TEXTURE_AFBC) {
                rt->format.block = MALI_BLOCK_AFBC;

                unsigned header_size = rsrc->slices[level].header_size;

                rt->framebuffer = base + header_size;
                rt->layer_stride = layer_stride;
                rt->afbc.metadata = base;
                rt->afbc.stride = 0;
                rt->afbc.flags = MALI_AFBC_FLAGS;

                unsigned components = util_format_get_nr_components(surf->format);

                /* The "lossless colorspace transform" is lossy for R and RG formats */
                if (components >= 3)
                   rt->afbc.flags |= MALI_AFBC_YTR;

                /* TODO: The blob sets this to something nonzero, but it's not
                 * clear what/how to calculate/if it matters */
                rt->framebuffer_stride = 0;
        } else {
                fprintf(stderr, "Invalid render layout (cbuf)");
                assert(0);
        }
}

static void
panfrost_mfbd_set_zsbuf(
        struct mali_framebuffer *fb,
        struct mali_framebuffer_extra *fbx,
        struct pipe_surface *surf)
{
        struct panfrost_device *dev = pan_device(surf->context->screen);
        bool is_bifrost = dev->quirks & IS_BIFROST;
        struct panfrost_resource *rsrc = pan_resource(surf->texture);

        unsigned nr_samples = surf->texture->nr_samples;
        nr_samples = MAX2(nr_samples, 1);

        fbx->zs_samples = MALI_POSITIVE(nr_samples);

        unsigned level = surf->u.tex.level;
        unsigned first_layer = surf->u.tex.first_layer;
        assert(surf->u.tex.last_layer == first_layer);

        mali_ptr base = panfrost_get_texture_address(rsrc, level, first_layer, 0);

        if (rsrc->layout == MALI_TEXTURE_AFBC) {
                /* The only Z/S format we can compress is Z24S8 or variants
                 * thereof (handled by the gallium frontend) */
                assert(panfrost_is_z24s8_variant(surf->format));

                unsigned header_size = rsrc->slices[level].header_size;

                fb->mfbd_flags |= MALI_MFBD_EXTRA | MALI_MFBD_DEPTH_WRITE;

                fbx->flags_hi |= MALI_EXTRA_PRESENT;
                fbx->flags_lo |= MALI_EXTRA_ZS | 0x1; /* unknown */
                fbx->zs_block = MALI_BLOCK_AFBC;

                fbx->ds_afbc.depth_stencil = base + header_size;
                fbx->ds_afbc.depth_stencil_afbc_metadata = base;
                fbx->ds_afbc.depth_stencil_afbc_stride = 0;

                fbx->ds_afbc.flags = MALI_AFBC_FLAGS;
                fbx->ds_afbc.padding = 0x1000;
        } else if (rsrc->layout == MALI_TEXTURE_LINEAR || rsrc->layout == MALI_TEXTURE_TILED) {
                /* TODO: Z32F(S8) support, which is always linear */

                int stride = rsrc->slices[level].stride;

                unsigned layer_stride = (nr_samples > 1) ? rsrc->slices[level].size0 : 0;

                fb->mfbd_flags |= MALI_MFBD_EXTRA | MALI_MFBD_DEPTH_WRITE;
                fbx->flags_hi |= MALI_EXTRA_PRESENT;
                fbx->flags_lo |= MALI_EXTRA_ZS;

                fbx->ds_linear.depth = base;

                if (rsrc->layout == MALI_TEXTURE_LINEAR) {
                        fbx->zs_block = MALI_BLOCK_LINEAR;
                        fbx->ds_linear.depth_stride = stride / 16;
                        fbx->ds_linear.depth_layer_stride = layer_stride;
                } else {
                        if (is_bifrost) {
                                fbx->zs_block = MALI_BLOCK_UNKNOWN;
                                fbx->flags_hi |= 0x440;
                                fbx->flags_lo |= 0x1;
                        } else {
                                fbx->zs_block = MALI_BLOCK_TILED;
                        }

                        fbx->ds_linear.depth_stride = stride;
                        fbx->ds_linear.depth_layer_stride = layer_stride;
                }

                if (panfrost_is_z24s8_variant(surf->format)) {
                        fbx->flags_lo |= 0x1;
                } else if (surf->format == PIPE_FORMAT_Z32_FLOAT) {
                        fbx->flags_lo |= 0xA;
                        fb->mfbd_flags ^= 0x100;
                        fb->mfbd_flags |= 0x200;
                } else if (surf->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) {
                        fbx->flags_hi |= 0x40;
                        fbx->flags_lo |= 0xA;
                        fb->mfbd_flags ^= 0x100;
                        fb->mfbd_flags |= 0x201;

                        struct panfrost_resource *stencil = rsrc->separate_stencil;
                        struct panfrost_slice stencil_slice = stencil->slices[level];
                        unsigned stencil_layer_stride = (nr_samples > 1) ? stencil_slice.size0 : 0;

                        fbx->ds_linear.stencil = panfrost_get_texture_address(stencil, level, first_layer, 0);
                        fbx->ds_linear.stencil_stride = stencil_slice.stride;
                        fbx->ds_linear.stencil_layer_stride = stencil_layer_stride;
                }

        } else {
                assert(0);
        }
}

/* Helper for sequential uploads used for MFBD */

#define UPLOAD(dest, offset, src, max) { \
        size_t sz = sizeof(*src); \
        memcpy(dest.cpu + offset, src, sz); \
        assert((offset + sz) <= max); \
        offset += sz; \
}

static mali_ptr
panfrost_mfbd_upload(struct panfrost_batch *batch,
        struct mali_framebuffer *fb,
        struct mali_framebuffer_extra *fbx,
        struct mali_render_target *rts,
        unsigned rt_count)
{
        off_t offset = 0;

        /* There may be extra data stuck in the middle */
        bool has_extra = fb->mfbd_flags & MALI_MFBD_EXTRA;

        /* Compute total size for transfer */

        size_t total_sz =
                sizeof(struct mali_framebuffer) +
                (has_extra ? sizeof(struct mali_framebuffer_extra) : 0) +
                sizeof(struct mali_render_target) * 8;

        struct panfrost_transfer m_f_trans =
                panfrost_pool_alloc(&batch->pool, total_sz);

        /* Do the transfer */

        UPLOAD(m_f_trans, offset, fb, total_sz);

        if (has_extra)
                UPLOAD(m_f_trans, offset, fbx, total_sz);

        for (unsigned c = 0; c < 8; ++c) {
                UPLOAD(m_f_trans, offset, &rts[c], total_sz);
        }

        /* Return pointer suitable for the fragment section */
        unsigned tag =
                MALI_MFBD |
                (has_extra ? MALI_MFBD_TAG_EXTRA : 0) |
                (MALI_POSITIVE(rt_count) << 2);

        return m_f_trans.gpu | tag;
}

#undef UPLOAD

/* Determines the # of bytes per pixel we need to reserve for a given format in
 * the tilebuffer (compared to 128-bit budget, etc). Usually the same as the
 * bytes per pixel of the format itself, but there are some special cases I
 * don't understand. */

static unsigned
pan_bytes_per_pixel_tib(enum pipe_format format)
{
        const struct util_format_description *desc =
                util_format_description(format);

        if (util_format_is_unorm8(desc) || format == PIPE_FORMAT_B5G6R5_UNORM)
                return 4;

        return desc->block.bits / 8;
}

/* Determines whether a framebuffer uses too much tilebuffer space (requiring
 * us to scale up the tile at a performance penalty). This is conservative but
 * afaict you get 128-bits per pixel normally */

static unsigned
pan_tib_size(struct panfrost_batch *batch)
{
        unsigned size = 0;

        for (int cb = 0; cb < batch->key.nr_cbufs; ++cb) {
                struct pipe_surface *surf = batch->key.cbufs[cb];
                assert(surf);
                size += pan_bytes_per_pixel_tib(surf->format);
        }

        return size;
}

static unsigned
pan_tib_shift(struct panfrost_batch *batch)
{
        unsigned size = pan_tib_size(batch);

        if (size > 128)
                return 4;
        else if (size > 64)
                return 5;
        else if (size > 32)
                return 6;
        else if (size > 16)
                return 7;
        else
                return 8;
}

static struct mali_framebuffer
panfrost_emit_mfbd(struct panfrost_batch *batch, unsigned vertex_count)
{
        struct panfrost_context *ctx = batch->ctx;
        struct pipe_context *gallium = (struct pipe_context *) ctx;
        struct panfrost_device *dev = pan_device(gallium->screen);

        unsigned width = batch->key.width;
        unsigned height = batch->key.height;

        struct mali_framebuffer mfbd = {
                .width1 = MALI_POSITIVE(width),
                .height1 = MALI_POSITIVE(height),
                .width2 = MALI_POSITIVE(width),
                .height2 = MALI_POSITIVE(height),

                /* Configures tib size */
                .unk1 = (pan_tib_shift(batch) << 9) | 0x80,

                .rt_count_1 = MALI_POSITIVE(MAX2(batch->key.nr_cbufs, 1)),
                .rt_count_2 = 4,
        };

        if (dev->quirks & IS_BIFROST) {
                mfbd.msaa.sample_locations = panfrost_emit_sample_locations(batch);
                mfbd.tiler_meta = panfrost_batch_get_tiler_meta(batch, vertex_count);
        } else {
                unsigned shift = panfrost_get_stack_shift(batch->stack_size);
                struct panfrost_bo *bo = panfrost_batch_get_scratchpad(batch,
                                                                       shift,
                                                                       dev->thread_tls_alloc,
                                                                       dev->core_count);
                mfbd.shared_memory.stack_shift = shift;
                mfbd.shared_memory.scratchpad = bo->gpu;
                mfbd.shared_memory.shared_workgroup_count = ~0;

                mfbd.tiler = panfrost_emit_midg_tiler(batch, vertex_count);
        }

        return mfbd;
}

void
panfrost_attach_mfbd(struct panfrost_batch *batch, unsigned vertex_count)
{
        struct mali_framebuffer mfbd =
                panfrost_emit_mfbd(batch, vertex_count);

        memcpy(batch->framebuffer.cpu, &mfbd, sizeof(mfbd));
}

/* Creates an MFBD for the FRAGMENT section of the bound framebuffer */

mali_ptr
panfrost_mfbd_fragment(struct panfrost_batch *batch, bool has_draws)
{
        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);
        bool is_bifrost = dev->quirks & IS_BIFROST;

        struct mali_framebuffer fb = panfrost_emit_mfbd(batch, has_draws);
        struct mali_framebuffer_extra fbx = {0};
        struct mali_render_target rts[8] = {0};

        /* We always upload at least one dummy GL_NONE render target */

        unsigned rt_descriptors = MAX2(batch->key.nr_cbufs, 1);

        fb.rt_count_1 = MALI_POSITIVE(rt_descriptors);
        fb.mfbd_flags = 0x100;

        panfrost_mfbd_clear(batch, &fb, &fbx, rts, rt_descriptors);

        /* Upload either the render target or a dummy GL_NONE target */

        unsigned offset = 0;
        unsigned tib_shift = pan_tib_shift(batch);

        for (int cb = 0; cb < rt_descriptors; ++cb) {
                struct pipe_surface *surf = batch->key.cbufs[cb];
                unsigned rt_offset = offset << tib_shift;

                if (surf && ((batch->clear | batch->draws) & (PIPE_CLEAR_COLOR0 << cb))) {
                        if (MAX2(surf->nr_samples, surf->texture->nr_samples) > 1)
                                batch->requirements |= PAN_REQ_MSAA;

                        panfrost_mfbd_set_cbuf(&rts[cb], surf);

                        offset += pan_bytes_per_pixel_tib(surf->format);
                } else {
                        struct mali_rt_format null_rt = {
                                .unk1 = 0x4000000,
                                .no_preload = true
                        };

                        if (is_bifrost) {
                                null_rt.flags = 0x2;
                                null_rt.unk3 = 0x8;
                        }

                        rts[cb].format = null_rt;
                        rts[cb].framebuffer = 0;
                        rts[cb].framebuffer_stride = 0;
                }

                /* TODO: Break out the field */
                rts[cb].format.unk1 |= rt_offset;
        }

        fb.rt_count_2 = MAX2(DIV_ROUND_UP(offset, 1 << (10 - tib_shift)), 1);

        if (batch->key.zsbuf && ((batch->clear | batch->draws) & PIPE_CLEAR_DEPTHSTENCIL)) {
                if (MAX2(batch->key.zsbuf->nr_samples, batch->key.zsbuf->nr_samples) > 1)
                        batch->requirements |= PAN_REQ_MSAA;

                panfrost_mfbd_set_zsbuf(&fb, &fbx, batch->key.zsbuf);
        }

        /* When scanning out, the depth buffer is immediately invalidated, so
         * we don't need to waste bandwidth writing it out. This can improve
         * performance substantially (Z24X8_UNORM 1080p @ 60fps is 475 MB/s of
         * memory bandwidth!).
         *
         * The exception is ReadPixels, but this is not supported on GLES so we
         * can safely ignore it. */

        if (panfrost_batch_is_scanout(batch))
                batch->requirements &= ~PAN_REQ_DEPTH_WRITE;

        /* Actualize the requirements */

        if (batch->requirements & PAN_REQ_MSAA) {
                /* XXX */
                fb.unk1 |= (1 << 4) | (1 << 1);
                fb.rt_count_2 = 4;
        }

        if (batch->requirements & PAN_REQ_DEPTH_WRITE)
                fb.mfbd_flags |= MALI_MFBD_DEPTH_WRITE;

        /* Checksumming only works with a single render target */

        if (batch->key.nr_cbufs == 1) {
                struct pipe_surface *surf = batch->key.cbufs[0];
                struct panfrost_resource *rsrc = pan_resource(surf->texture);

                if (rsrc->checksummed) {
                        unsigned level = surf->u.tex.level;
                        struct panfrost_slice *slice = &rsrc->slices[level];

                        fb.mfbd_flags |= MALI_MFBD_EXTRA;
                        fbx.flags_hi |= MALI_EXTRA_PRESENT;
                        fbx.checksum_stride = slice->checksum_stride;
                        if (slice->checksum_bo)
                                fbx.checksum = slice->checksum_bo->gpu;
                        else
                                fbx.checksum = rsrc->bo->gpu + slice->checksum_offset;
                }
        }

        return panfrost_mfbd_upload(batch, &fb, &fbx, rts, rt_descriptors);
}
