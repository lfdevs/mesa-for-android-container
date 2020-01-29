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

#include "util/format/u_format.h"

static struct mali_sfbd_format
panfrost_sfbd_format(struct pipe_surface *surf)
{
        /* Explode details on the format */

        const struct util_format_description *desc =
                util_format_description(surf->format);

        /* The swizzle for rendering is inverted from texturing */

        unsigned char swizzle[4];
        panfrost_invert_swizzle(desc->swizzle, swizzle);

        struct mali_sfbd_format fmt = {
                .unk1 = 0x1,
                .swizzle = panfrost_translate_swizzle_4(swizzle),
                .nr_channels = MALI_POSITIVE(desc->nr_channels),
                .unk2 = 0x4,
                .unk3 = 0xb,
        };

        if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB)
                fmt.unk2 |= MALI_SFBD_FORMAT_SRGB;

        /* sRGB handled as a dedicated flag */
        enum pipe_format linearized = util_format_linear(surf->format);

        /* If RGB, we're good to go */
        if (util_format_is_unorm8(desc))
                return fmt;

        switch (linearized) {
        case PIPE_FORMAT_B5G6R5_UNORM:
                fmt.unk1 = 0x5;
                fmt.nr_channels = MALI_POSITIVE(2);
                fmt.unk2 = 0x5;
                break;

        case PIPE_FORMAT_A4B4G4R4_UNORM:
        case PIPE_FORMAT_B4G4R4A4_UNORM:
                fmt.unk1 = 0x4;
                fmt.nr_channels = MALI_POSITIVE(1);
                fmt.unk2 = 0x5;
                break;

        default:
                unreachable("Invalid format rendering");
        }

        return fmt;
}

static void
panfrost_sfbd_clear(
        struct panfrost_batch *batch,
        struct mali_single_framebuffer *sfbd)
{
        if (batch->clear & PIPE_CLEAR_COLOR) {
                sfbd->clear_color_1 = batch->clear_color[0][0];
                sfbd->clear_color_2 = batch->clear_color[0][1];
                sfbd->clear_color_3 = batch->clear_color[0][2];
                sfbd->clear_color_4 = batch->clear_color[0][3];
        }

        if (batch->clear & PIPE_CLEAR_DEPTH) {
                sfbd->clear_depth_1 = batch->clear_depth;
                sfbd->clear_depth_2 = batch->clear_depth;
                sfbd->clear_depth_3 = batch->clear_depth;
                sfbd->clear_depth_4 = batch->clear_depth;
        }

        if (batch->clear & PIPE_CLEAR_STENCIL) {
                sfbd->clear_stencil = batch->clear_stencil;
        }

        /* Set flags based on what has been cleared, for the SFBD case */
        /* XXX: What do these flags mean? */
        int clear_flags = 0x101100;

        if (!(batch->clear & ~(PIPE_CLEAR_COLOR | PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL))) {
                /* On a tiler like this, it's fastest to clear all three buffers at once */

                clear_flags |= MALI_CLEAR_FAST;
        } else {
                clear_flags |= MALI_CLEAR_SLOW;

                if (batch->clear & PIPE_CLEAR_STENCIL)
                        clear_flags |= MALI_CLEAR_SLOW_STENCIL;
        }

        sfbd->clear_flags = clear_flags;
}

static void
panfrost_sfbd_set_cbuf(
        struct mali_single_framebuffer *fb,
        struct pipe_surface *surf)
{
        struct panfrost_resource *rsrc = pan_resource(surf->texture);

        unsigned level = surf->u.tex.level;
        unsigned first_layer = surf->u.tex.first_layer;
        assert(surf->u.tex.last_layer == first_layer);
        signed stride = rsrc->slices[level].stride;

        mali_ptr base = panfrost_get_texture_address(rsrc, level, first_layer);

        fb->format = panfrost_sfbd_format(surf);

        fb->framebuffer = base;
        fb->stride = stride;

        if (rsrc->layout == PAN_LINEAR)
                fb->format.block = MALI_BLOCK_LINEAR;
        else if (rsrc->layout == PAN_TILED) {
                fb->format.block = MALI_BLOCK_TILED;
                fb->stride *= 16;
        } else {
                fprintf(stderr, "Invalid render layout\n");
                assert(0);
        }
}

static void
panfrost_sfbd_set_zsbuf(
        struct mali_single_framebuffer *fb,
        struct pipe_surface *surf)
{
        struct panfrost_resource *rsrc = pan_resource(surf->texture);
        struct panfrost_context *ctx = pan_context(surf->context);

        unsigned level = surf->u.tex.level;
        assert(surf->u.tex.first_layer == 0);

        if (rsrc->layout != PAN_TILED)
                unreachable("Invalid render layout.");

        fb->depth_buffer = rsrc->bo->gpu + rsrc->slices[level].offset;
        fb->depth_stride = rsrc->slices[level].stride;

        /* No stencil? Job done. */
        if (!ctx->depth_stencil || !ctx->depth_stencil->stencil[0].enabled)
                return;

        if (panfrost_is_z24s8_variant(surf->format)) {

                /* Stencil data is interleaved with depth */
                fb->stencil_buffer = fb->depth_buffer;
                fb->stencil_stride = fb->depth_stride;
        } else if (surf->format == PIPE_FORMAT_Z32_UNORM ||
                   surf->format == PIPE_FORMAT_Z32_FLOAT) {

                /* No stencil, nothing to do */
        } else if (surf->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) {

                /* Stencil data in separate buffer */
                struct panfrost_resource *stencil = rsrc->separate_stencil;
                struct panfrost_slice stencil_slice = stencil->slices[level];

                fb->stencil_buffer = stencil->bo->gpu + stencil_slice.offset;
                fb->stencil_stride = stencil_slice.stride;
        } else
                unreachable("Unsupported depth/stencil format.");
}


static struct mali_single_framebuffer
panfrost_emit_sfbd(struct panfrost_batch *batch, unsigned vertex_count)
{
        struct panfrost_context *ctx = batch->ctx;
        struct pipe_context *gallium = (struct pipe_context *) ctx;
        struct panfrost_screen *screen = pan_screen(gallium->screen);

        unsigned width = batch->key.width;
        unsigned height = batch->key.height;

        /* TODO: Why do we need to make the stack bigger than other platforms? */
        unsigned shift = panfrost_get_stack_shift(MAX2(batch->stack_size, 512));

        /* TODO: where do we specify the shift? */

        struct mali_single_framebuffer framebuffer = {
                .width = MALI_POSITIVE(width),
                .height = MALI_POSITIVE(height),
                .unknown2 = 0x1f,
                .format = {
                        .unk3 = 0x3,
                },
                .clear_flags = 0x1000,
                .scratchpad = panfrost_batch_get_scratchpad(batch, shift, screen->thread_tls_alloc, screen->core_count)->gpu,
                .tiler = panfrost_emit_midg_tiler(batch, vertex_count),
        };

        return framebuffer;
}

void
panfrost_attach_sfbd(struct panfrost_batch *batch, unsigned vertex_count)
{
        struct mali_single_framebuffer sfbd =
                panfrost_emit_sfbd(batch, vertex_count);

        memcpy(batch->framebuffer.cpu, &sfbd, sizeof(sfbd));
}

/* Creates an SFBD for the FRAGMENT section of the bound framebuffer */

mali_ptr
panfrost_sfbd_fragment(struct panfrost_batch *batch, bool has_draws)
{
        struct mali_single_framebuffer fb = panfrost_emit_sfbd(batch, has_draws);

        panfrost_sfbd_clear(batch, &fb);

        /* SFBD does not support MRT natively; sanity check */
        assert(batch->key.nr_cbufs <= 1);
        if (batch->key.nr_cbufs) {
                struct pipe_surface *surf = batch->key.cbufs[0];
                struct panfrost_resource *rsrc = pan_resource(surf->texture);
                struct panfrost_bo *bo = rsrc->bo;

                panfrost_sfbd_set_cbuf(&fb, surf);

                if (rsrc->checksummed) {
                        unsigned level = surf->u.tex.level;
                        struct panfrost_slice *slice = &rsrc->slices[level];

                        fb.checksum_stride = slice->checksum_stride;
                        fb.checksum = bo->gpu + slice->checksum_offset;
                }
        }

        if (batch->key.zsbuf)
                panfrost_sfbd_set_zsbuf(&fb, batch->key.zsbuf);

        if (batch->requirements & PAN_REQ_MSAA) {
                fb.format.unk1 |= MALI_SFBD_FORMAT_MSAA_A;
                fb.format.unk2 |= MALI_SFBD_FORMAT_MSAA_B;
        }

        return panfrost_upload_transient(batch, &fb, sizeof(fb));
}
