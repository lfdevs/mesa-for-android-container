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
#include "pan_util.h"
#include "pan_format.h"

#include "util/u_format.h"

static void
panfrost_invert_swizzle(const unsigned char *in, unsigned char *out)
{
        /* First, default to all zeroes to prevent uninitialized junk */

        for (unsigned c = 0; c < 4; ++c)
                out[c] = PIPE_SWIZZLE_0;

        /* Now "do" what the swizzle says */

        for (unsigned c = 0; c < 4; ++c) {
                unsigned char i = in[c];

                /* Who cares? */
                assert(PIPE_SWIZZLE_X == 0);
                if (i > PIPE_SWIZZLE_W)
                        continue;

                /* Invert */
                unsigned idx = i - PIPE_SWIZZLE_X;
                out[idx] = PIPE_SWIZZLE_X + c;
        }
}

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
                .flags = 0x8,
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
        case PIPE_FORMAT_B5G5R5A1_UNORM:
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
        struct bifrost_framebuffer *fb,
        struct bifrost_fb_extra *fbx,
        struct bifrost_render_target *rts,
        unsigned rt_count)
{
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
}

static void
panfrost_mfbd_set_cbuf(
        struct bifrost_render_target *rt,
        struct pipe_surface *surf)
{
        struct panfrost_resource *rsrc = pan_resource(surf->texture);

        unsigned level = surf->u.tex.level;
        unsigned first_layer = surf->u.tex.first_layer;
        assert(surf->u.tex.last_layer == first_layer);
        int stride = rsrc->slices[level].stride;

        mali_ptr base = panfrost_get_texture_address(rsrc, level, first_layer);

        rt->format = panfrost_mfbd_format(surf);

        /* Now, we set the layout specific pieces */

        if (rsrc->layout == PAN_LINEAR) {
                rt->format.block = MALI_MFBD_BLOCK_LINEAR;
                rt->framebuffer = base;
                rt->framebuffer_stride = stride / 16;
        } else if (rsrc->layout == PAN_TILED) {
                rt->format.block = MALI_MFBD_BLOCK_TILED;
                rt->framebuffer = base;
                rt->framebuffer_stride = stride;
        } else if (rsrc->layout == PAN_AFBC) {
                rt->format.block = MALI_MFBD_BLOCK_AFBC;

                unsigned header_size = rsrc->slices[level].header_size;

                rt->framebuffer = base + header_size;
                rt->afbc.metadata = base;
                rt->afbc.stride = 0;
                rt->afbc.unk = 0x30009;

                /* TODO: The blob sets this to something nonzero, but it's not
                 * clear what/how to calculate/if it matters */
                rt->framebuffer_stride = 0;
        } else {
                fprintf(stderr, "Invalid render layout (cbuf)");
                assert(0);
        }
}

/* Is a format encoded like Z24S8 and therefore compatible for render? */

static bool
panfrost_is_z24s8_variant(enum pipe_format fmt)
{
        switch (fmt) {
                case PIPE_FORMAT_Z24_UNORM_S8_UINT:
                case PIPE_FORMAT_Z24X8_UNORM:
                        return true;
                default:
                        return false;
        }
}

static void
panfrost_mfbd_set_zsbuf(
        struct bifrost_framebuffer *fb,
        struct bifrost_fb_extra *fbx,
        struct pipe_surface *surf)
{
        struct panfrost_resource *rsrc = pan_resource(surf->texture);

        unsigned level = surf->u.tex.level;
        assert(surf->u.tex.first_layer == 0);

        unsigned offset = rsrc->slices[level].offset;

        if (rsrc->layout == PAN_AFBC) {
                /* The only Z/S format we can compress is Z24S8 or variants
                 * thereof (handled by the state tracker) */
                assert(panfrost_is_z24s8_variant(surf->format));

                mali_ptr base = rsrc->bo->gpu + offset;
                unsigned header_size = rsrc->slices[level].header_size;

                fb->mfbd_flags |= MALI_MFBD_EXTRA;

                fbx->flags =
                        MALI_EXTRA_PRESENT |
                        MALI_EXTRA_AFBC |
                        MALI_EXTRA_AFBC_ZS |
                        MALI_EXTRA_ZS |
                        0x1; /* unknown */

                fbx->ds_afbc.depth_stencil = base + header_size;
                fbx->ds_afbc.depth_stencil_afbc_metadata = base;
                fbx->ds_afbc.depth_stencil_afbc_stride = 0;

                fbx->ds_afbc.zero1 = 0x10009;
                fbx->ds_afbc.padding = 0x1000;
        } else if (rsrc->layout == PAN_LINEAR) {
                /* TODO: Z32F(S8) support, which is always linear */

                int stride = rsrc->slices[level].stride;

                fb->mfbd_flags |= MALI_MFBD_EXTRA;
                fbx->flags |= MALI_EXTRA_PRESENT | MALI_EXTRA_ZS;

                fbx->ds_linear.depth = rsrc->bo->gpu + offset;
                fbx->ds_linear.depth_stride = stride;

                if (panfrost_is_z24s8_variant(surf->format)) {
                        fbx->flags |= 0x1;
                } else if (surf->format == PIPE_FORMAT_Z32_UNORM) {
                        /* default flags (0 in bottom place) */
                } else if (surf->format == PIPE_FORMAT_Z32_FLOAT) {
                        fbx->flags |= 0xA;
                        fb->mfbd_flags ^= 0x100;
                        fb->mfbd_flags |= 0x200;
                } else if (surf->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) {
                        fbx->flags |= 0x1000A;
                        fb->mfbd_flags ^= 0x100;
                        fb->mfbd_flags |= 0x201;

                        struct panfrost_resource *stencil = rsrc->separate_stencil;
                        struct panfrost_slice stencil_slice = stencil->slices[level];

                        fbx->ds_linear.stencil = stencil->bo->gpu + stencil_slice.offset;
                        fbx->ds_linear.stencil_stride = stencil_slice.stride;
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
        struct bifrost_framebuffer *fb,
        struct bifrost_fb_extra *fbx,
        struct bifrost_render_target *rts,
        unsigned rt_count)
{
        off_t offset = 0;

        /* There may be extra data stuck in the middle */
        bool has_extra = fb->mfbd_flags & MALI_MFBD_EXTRA;

        /* Compute total size for transfer */

        size_t total_sz =
                sizeof(struct bifrost_framebuffer) +
                (has_extra ? sizeof(struct bifrost_fb_extra) : 0) +
                sizeof(struct bifrost_render_target) * 4;

        struct panfrost_transfer m_f_trans =
                panfrost_allocate_transient(batch, total_sz);

        /* Do the transfer */

        UPLOAD(m_f_trans, offset, fb, total_sz);

        if (has_extra)
                UPLOAD(m_f_trans, offset, fbx, total_sz);

        for (unsigned c = 0; c < 4; ++c) {
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

/* Creates an MFBD for the FRAGMENT section of the bound framebuffer */

mali_ptr
panfrost_mfbd_fragment(struct panfrost_batch *batch, bool has_draws)
{
        struct bifrost_framebuffer fb = panfrost_emit_mfbd(batch, has_draws);
        struct bifrost_fb_extra fbx = {};
        struct bifrost_render_target rts[4] = {};

        /* We always upload at least one dummy GL_NONE render target */

        unsigned rt_descriptors = MAX2(batch->key.nr_cbufs, 1);

        fb.rt_count_1 = MALI_POSITIVE(rt_descriptors);
        fb.rt_count_2 = rt_descriptors;
        fb.mfbd_flags = 0x100;

        /* TODO: MRT clear */
        panfrost_mfbd_clear(batch, &fb, &fbx, rts, fb.rt_count_2);


        /* Upload either the render target or a dummy GL_NONE target */

        for (int cb = 0; cb < rt_descriptors; ++cb) {
                struct pipe_surface *surf = batch->key.cbufs[cb];

                if (surf) {
                        panfrost_mfbd_set_cbuf(&rts[cb], surf);

                        /* What is this? Looks like some extension of the bpp
                         * field. Maybe it establishes how much internal
                         * tilebuffer space is reserved? */

                        unsigned bpp = util_format_get_blocksize(surf->format);
                        fb.rt_count_2 = MAX2(fb.rt_count_2, ALIGN_POT(bpp, 4) / 4);
                } else {
                        struct mali_rt_format null_rt = {
                                .unk1 = 0x4000000,
                                .no_preload = true
                        };

                        rts[cb].format = null_rt;
                        rts[cb].framebuffer = 0;
                        rts[cb].framebuffer_stride = 0;
                }

                /* TODO: Break out the field */
                rts[cb].format.unk1 |= (cb * 0x400);
        }

        if (batch->key.zsbuf) {
                panfrost_mfbd_set_zsbuf(&fb, &fbx, batch->key.zsbuf);
        }

        /* When scanning out, the depth buffer is immediately invalidated, so
         * we don't need to waste bandwidth writing it out. This can improve
         * performance substantially (Z32_UNORM 1080p @ 60fps is 475 MB/s of
         * memory bandwidth!).
         *
         * The exception is ReadPixels, but this is not supported on GLES so we
         * can safely ignore it. */

        if (panfrost_batch_is_scanout(batch))
                batch->requirements &= ~PAN_REQ_DEPTH_WRITE;

        /* Actualize the requirements */

        if (batch->requirements & PAN_REQ_MSAA) {
                rts[0].format.flags |= MALI_MFBD_FORMAT_MSAA;

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
                struct panfrost_bo *bo = rsrc->bo;

                if (rsrc->checksummed) {
                        unsigned level = surf->u.tex.level;
                        struct panfrost_slice *slice = &rsrc->slices[level];

                        fb.mfbd_flags |= MALI_MFBD_EXTRA;
                        fbx.flags |= MALI_EXTRA_PRESENT;
                        fbx.checksum_stride = slice->checksum_stride;
                        fbx.checksum = bo->gpu + slice->checksum_offset;
                }
        }

        return panfrost_mfbd_upload(batch, &fb, &fbx, rts, rt_descriptors);
}
