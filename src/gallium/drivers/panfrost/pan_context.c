/*
 * © Copyright 2018 Alyssa Rosenzweig
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

#include <sys/poll.h>
#include <errno.h>

#include "pan_context.h"
#include "pan_swizzle.h"
#include "pan_format.h"

#include "util/macros.h"
#include "util/u_format.h"
#include "util/u_inlines.h"
#include "util/u_upload_mgr.h"
#include "util/u_memory.h"
#include "util/u_vbuf.h"
#include "util/half_float.h"
#include "util/u_helpers.h"
#include "util/u_format.h"
#include "indices/u_primconvert.h"
#include "tgsi/tgsi_parse.h"
#include "util/u_math.h"

#include "pan_screen.h"
#include "pan_blending.h"
#include "pan_blend_shaders.h"
#include "pan_util.h"
#include "pan_wallpaper.h"

static int performance_counter_number = 0;
extern const char *pan_counters_base;

/* Do not actually send anything to the GPU; merely generate the cmdstream as fast as possible. Disables framebuffer writes */
//#define DRY_RUN

/* Can a given format support AFBC? Not all can. */

static bool
panfrost_can_afbc(enum pipe_format format)
{
        const struct util_format_description *desc =
                util_format_description(format);

        if (util_format_is_rgba8_variant(desc))
                return true;

        /* TODO: AFBC of other formats */

        return false;
}

/* AFBC is enabled on a per-resource basis (AFBC enabling is theoretically
 * indepdent between color buffers and depth/stencil). To enable, we allocate
 * the AFBC metadata buffer and mark that it is enabled. We do -not- actually
 * edit the fragment job here. This routine should be called ONCE per
 * AFBC-compressed buffer, rather than on every frame. */

static void
panfrost_enable_afbc(struct panfrost_context *ctx, struct panfrost_resource *rsrc, bool ds)
{
        if (ctx->require_sfbd) {
                DBG("AFBC not supported yet on SFBD\n");
                assert(0);
        }

        struct pipe_context *gallium = (struct pipe_context *) ctx;
        struct panfrost_screen *screen = pan_screen(gallium->screen);
       /* AFBC metadata is 16 bytes per tile */
        int tile_w = (rsrc->base.width0 + (MALI_TILE_LENGTH - 1)) >> MALI_TILE_SHIFT;
        int tile_h = (rsrc->base.height0 + (MALI_TILE_LENGTH - 1)) >> MALI_TILE_SHIFT;
        int bytes_per_pixel = util_format_get_blocksize(rsrc->base.format);
        int stride = bytes_per_pixel * ALIGN(rsrc->base.width0, 16);

        stride *= 2;  /* TODO: Should this be carried over? */
        int main_size = stride * rsrc->base.height0;
        rsrc->bo->afbc_metadata_size = tile_w * tile_h * 16;

        /* Allocate the AFBC slab itself, large enough to hold the above */
        screen->driver->allocate_slab(screen, &rsrc->bo->afbc_slab,
                               (rsrc->bo->afbc_metadata_size + main_size + 4095) / 4096,
                               true, 0, 0, 0);

        rsrc->bo->layout = PAN_AFBC;

        /* Compressed textured reads use a tagged pointer to the metadata */

        rsrc->bo->gpu = rsrc->bo->afbc_slab.gpu | (ds ? 0 : 1);
        rsrc->bo->cpu = rsrc->bo->afbc_slab.cpu;
        rsrc->bo->gem_handle = rsrc->bo->afbc_slab.gem_handle;
}

static void
panfrost_enable_checksum(struct panfrost_context *ctx, struct panfrost_resource *rsrc)
{
        struct pipe_context *gallium = (struct pipe_context *) ctx;
        struct panfrost_screen *screen = pan_screen(gallium->screen);
        int tile_w = (rsrc->base.width0 + (MALI_TILE_LENGTH - 1)) >> MALI_TILE_SHIFT;
        int tile_h = (rsrc->base.height0 + (MALI_TILE_LENGTH - 1)) >> MALI_TILE_SHIFT;

        /* 8 byte checksum per tile */
        rsrc->bo->checksum_stride = tile_w * 8;
        int pages = (((rsrc->bo->checksum_stride * tile_h) + 4095) / 4096);
        screen->driver->allocate_slab(screen, &rsrc->bo->checksum_slab, pages, false, 0, 0, 0);

        rsrc->bo->has_checksum = true;
}

/* Framebuffer descriptor */

static void
panfrost_set_framebuffer_resolution(struct mali_single_framebuffer *fb, int w, int h)
{
        fb->width = MALI_POSITIVE(w);
        fb->height = MALI_POSITIVE(h);

        /* No idea why this is needed, but it's how resolution_check is
         * calculated.  It's not clear to us yet why the hardware wants this.
         * The formula itself was discovered mostly by manual bruteforce and
         * aggressive algebraic simplification. */

        fb->resolution_check = ((w + h) / 3) << 4;
}

struct mali_single_framebuffer
panfrost_emit_sfbd(struct panfrost_context *ctx)
{
        struct mali_single_framebuffer framebuffer = {
                .unknown2 = 0x1f,
                .format = 0x30000000,
                .clear_flags = 0x1000,
                .unknown_address_0 = ctx->scratchpad.gpu,
                .unknown_address_1 = ctx->misc_0.gpu,
                .unknown_address_2 = ctx->misc_0.gpu + 40960,
                .tiler_flags = 0xf0,
                .tiler_heap_free = ctx->tiler_heap.gpu,
                .tiler_heap_end = ctx->tiler_heap.gpu + ctx->tiler_heap.size,
        };

        panfrost_set_framebuffer_resolution(&framebuffer, ctx->pipe_framebuffer.width, ctx->pipe_framebuffer.height);

        return framebuffer;
}

struct bifrost_framebuffer
panfrost_emit_mfbd(struct panfrost_context *ctx)
{
        struct bifrost_framebuffer framebuffer = {
                /* It is not yet clear what tiler_meta means or how it's
                 * calculated, but we can tell the lower 32-bits are a
                 * (monotonically increasing?) function of tile count and
                 * geometry complexity; I suspect it defines a memory size of
                 * some kind? for the tiler. It's really unclear at the
                 * moment... but to add to the confusion, the hardware is happy
                 * enough to accept a zero in this field, so we don't even have
                 * to worry about it right now.
                 *
                 * The byte (just after the 32-bit mark) is much more
                 * interesting. The higher nibble I've only ever seen as 0xF,
                 * but the lower one I've seen as 0x0 or 0xF, and it's not
                 * obvious what the difference is. But what -is- obvious is
                 * that when the lower nibble is zero, performance is severely
                 * degraded compared to when the lower nibble is set.
                 * Evidently, that nibble enables some sort of fast path,
                 * perhaps relating to caching or tile flush? Regardless, at
                 * this point there's no clear reason not to set it, aside from
                 * substantially increased memory requirements (of the misc_0
                 * buffer) */

                .tiler_meta = ((uint64_t) 0xff << 32) | 0x0,

                .width1 = MALI_POSITIVE(ctx->pipe_framebuffer.width),
                .height1 = MALI_POSITIVE(ctx->pipe_framebuffer.height),
                .width2 = MALI_POSITIVE(ctx->pipe_framebuffer.width),
                .height2 = MALI_POSITIVE(ctx->pipe_framebuffer.height),

                .unk1 = 0x1080,

                /* TODO: MRT */
                .rt_count_1 = MALI_POSITIVE(1),
                .rt_count_2 = 4,

                .unknown2 = 0x1f,

                /* Corresponds to unknown_address_X of SFBD */
                .scratchpad = ctx->scratchpad.gpu,
                .tiler_scratch_start  = ctx->misc_0.gpu,

                /* The constant added here is, like the lower word of
                 * tiler_meta, (loosely) another product of framebuffer size
                 * and geometry complexity. It must be sufficiently large for
                 * the tiler_meta fast path to work; if it's too small, there
                 * will be DATA_INVALID_FAULTs. Conversely, it must be less
                 * than the total size of misc_0, or else there's no room. It's
                 * possible this constant configures a partition between two
                 * parts of misc_0? We haven't investigated the functionality,
                 * as these buffers are internally used by the hardware
                 * (presumably by the tiler) but not seemingly touched by the driver
                 */

                .tiler_scratch_middle = ctx->misc_0.gpu + 0xf0000,

                .tiler_heap_start = ctx->tiler_heap.gpu,
                .tiler_heap_end = ctx->tiler_heap.gpu + ctx->tiler_heap.size,
        };

        return framebuffer;
}

/* Are we currently rendering to the screen (rather than an FBO)? */

bool
panfrost_is_scanout(struct panfrost_context *ctx)
{
        /* If there is no color buffer, it's an FBO */
        if (!ctx->pipe_framebuffer.nr_cbufs)
                return false;

        /* If we're too early that no framebuffer was sent, it's scanout */
        if (!ctx->pipe_framebuffer.cbufs[0])
                return true;

        return ctx->pipe_framebuffer.cbufs[0]->texture->bind & PIPE_BIND_DISPLAY_TARGET ||
               ctx->pipe_framebuffer.cbufs[0]->texture->bind & PIPE_BIND_SCANOUT ||
               ctx->pipe_framebuffer.cbufs[0]->texture->bind & PIPE_BIND_SHARED;
}

static uint32_t
pan_pack_color(const union pipe_color_union *color, enum pipe_format format)
{
        /* Alpha magicked to 1.0 if there is no alpha */

        bool has_alpha = util_format_has_alpha(format);
        float clear_alpha = has_alpha ? color->f[3] : 1.0f;

        /* Packed color depends on the framebuffer format */

        const struct util_format_description *desc =
                util_format_description(format);

        if (util_format_is_rgba8_variant(desc)) {
                return (float_to_ubyte(clear_alpha) << 24) |
                       (float_to_ubyte(color->f[2]) << 16) |
                       (float_to_ubyte(color->f[1]) <<  8) |
                       (float_to_ubyte(color->f[0]) <<  0);
        } else if (format == PIPE_FORMAT_B5G6R5_UNORM) {
                /* First, we convert the components to R5, G6, B5 separately */
                unsigned r5 = CLAMP(color->f[0], 0.0, 1.0) * 31.0;
                unsigned g6 = CLAMP(color->f[1], 0.0, 1.0) * 63.0;
                unsigned b5 = CLAMP(color->f[2], 0.0, 1.0) * 31.0;

                /* Then we pack into a sparse u32. TODO: Why these shifts? */
                return (b5 << 25) | (g6 << 14) | (r5 << 5);
        } else {
                /* Unknown format */
                assert(0);
        }

        return 0;
}

static void
panfrost_clear(
        struct pipe_context *pipe,
        unsigned buffers,
        const union pipe_color_union *color,
        double depth, unsigned stencil)
{
        struct panfrost_context *ctx = pan_context(pipe);
        struct panfrost_job *job = panfrost_get_job_for_fbo(ctx);

        if (buffers & PIPE_CLEAR_COLOR) {
                enum pipe_format format = ctx->pipe_framebuffer.cbufs[0]->format;
                job->clear_color = pan_pack_color(color, format);
        }

        if (buffers & PIPE_CLEAR_DEPTH) {
                job->clear_depth = depth;
        }

        if (buffers & PIPE_CLEAR_STENCIL) {
                job->clear_stencil = stencil;
        }

        job->clear |= buffers;
}

static mali_ptr
panfrost_attach_vt_mfbd(struct panfrost_context *ctx)
{
        /* MFBD needs a sequential semi-render target upload, but what exactly this is, is beyond me for now */
        struct bifrost_render_target rts_list[] = {
                {
                        .chunknown = {
                                .unk = 0x30005,
                        },
                        .framebuffer = ctx->misc_0.gpu,
                        .zero2 = 0x3,
                },
        };

        /* Allocate memory for the three components */
        int size = 1024 + sizeof(ctx->vt_framebuffer_mfbd) + sizeof(rts_list);
        struct panfrost_transfer transfer = panfrost_allocate_transient(ctx, size);

        /* Opaque 1024-block */
        rts_list[0].chunknown.pointer = transfer.gpu;

        memcpy(transfer.cpu + 1024, &ctx->vt_framebuffer_mfbd, sizeof(ctx->vt_framebuffer_mfbd));
        memcpy(transfer.cpu + 1024 + sizeof(ctx->vt_framebuffer_mfbd), rts_list, sizeof(rts_list));

        return (transfer.gpu + 1024) | MALI_MFBD;
}

static mali_ptr
panfrost_attach_vt_sfbd(struct panfrost_context *ctx)
{
        return panfrost_upload_transient(ctx, &ctx->vt_framebuffer_sfbd, sizeof(ctx->vt_framebuffer_sfbd)) | MALI_SFBD;
}

static void
panfrost_attach_vt_framebuffer(struct panfrost_context *ctx)
{
        mali_ptr framebuffer = ctx->require_sfbd ?
                panfrost_attach_vt_sfbd(ctx) :
                panfrost_attach_vt_mfbd(ctx);

        ctx->payload_vertex.postfix.framebuffer = framebuffer;
        ctx->payload_tiler.postfix.framebuffer = framebuffer;
}

/* Reset per-frame context, called on context initialisation as well as after
 * flushing a frame */

static void
panfrost_invalidate_frame(struct panfrost_context *ctx)
{
        unsigned transient_count = ctx->transient_pools[ctx->cmdstream_i].entry_index*ctx->transient_pools[0].entry_size + ctx->transient_pools[ctx->cmdstream_i].entry_offset;
	DBG("Uploaded transient %d bytes\n", transient_count);

        /* Rotate cmdstream */
        if ((++ctx->cmdstream_i) == (sizeof(ctx->transient_pools) / sizeof(ctx->transient_pools[0])))
                ctx->cmdstream_i = 0;

        if (ctx->require_sfbd)
                ctx->vt_framebuffer_sfbd = panfrost_emit_sfbd(ctx);
        else
                ctx->vt_framebuffer_mfbd = panfrost_emit_mfbd(ctx);

        /* Reset varyings allocated */
        ctx->varying_height = 0;

        /* The transient cmdstream is dirty every frame; the only bits worth preserving
         * (textures, shaders, etc) are in other buffers anyways */

        ctx->transient_pools[ctx->cmdstream_i].entry_index = 0;
        ctx->transient_pools[ctx->cmdstream_i].entry_offset = 0;

        /* Regenerate payloads */
        panfrost_attach_vt_framebuffer(ctx);

        if (ctx->rasterizer)
                ctx->dirty |= PAN_DIRTY_RASTERIZER;

        /* XXX */
        ctx->dirty |= PAN_DIRTY_SAMPLERS | PAN_DIRTY_TEXTURES;
}

/* In practice, every field of these payloads should be configurable
 * arbitrarily, which means these functions are basically catch-all's for
 * as-of-yet unwavering unknowns */

static void
panfrost_emit_vertex_payload(struct panfrost_context *ctx)
{
        struct midgard_payload_vertex_tiler payload = {
                .prefix = {
                        .workgroups_z_shift = 32,
                        .workgroups_x_shift_2 = 0x2,
                        .workgroups_x_shift_3 = 0x5,
                },
		.gl_enables = 0x4 | (ctx->is_t6xx ? 0 : 0x2),
        };

        memcpy(&ctx->payload_vertex, &payload, sizeof(payload));
}

static void
panfrost_emit_tiler_payload(struct panfrost_context *ctx)
{
        struct midgard_payload_vertex_tiler payload = {
                .prefix = {
                        .workgroups_z_shift = 32,
                        .workgroups_x_shift_2 = 0x2,
                        .workgroups_x_shift_3 = 0x6,

                        .zero1 = 0xffff, /* Why is this only seen on test-quad-textured? */
                },
        };

        memcpy(&ctx->payload_tiler, &payload, sizeof(payload));
}

static unsigned
translate_tex_wrap(enum pipe_tex_wrap w)
{
        switch (w) {
        case PIPE_TEX_WRAP_REPEAT:
                return MALI_WRAP_REPEAT;

        case PIPE_TEX_WRAP_CLAMP_TO_EDGE:
                return MALI_WRAP_CLAMP_TO_EDGE;

        case PIPE_TEX_WRAP_CLAMP_TO_BORDER:
                return MALI_WRAP_CLAMP_TO_BORDER;

        case PIPE_TEX_WRAP_MIRROR_REPEAT:
                return MALI_WRAP_MIRRORED_REPEAT;

        default:
                assert(0);
                return 0;
        }
}

static unsigned
translate_tex_filter(enum pipe_tex_filter f)
{
        switch (f) {
        case PIPE_TEX_FILTER_NEAREST:
                return MALI_NEAREST;

        case PIPE_TEX_FILTER_LINEAR:
                return MALI_LINEAR;

        default:
                assert(0);
                return 0;
        }
}

static unsigned
translate_mip_filter(enum pipe_tex_mipfilter f)
{
        return (f == PIPE_TEX_MIPFILTER_LINEAR) ? MALI_MIP_LINEAR : 0;
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
        }

        assert (0);
        return 0; /* Unreachable */
}

static unsigned
panfrost_translate_alt_compare_func(enum pipe_compare_func in)
{
        switch (in) {
        case PIPE_FUNC_NEVER:
                return MALI_ALT_FUNC_NEVER;

        case PIPE_FUNC_LESS:
                return MALI_ALT_FUNC_LESS;

        case PIPE_FUNC_EQUAL:
                return MALI_ALT_FUNC_EQUAL;

        case PIPE_FUNC_LEQUAL:
                return MALI_ALT_FUNC_LEQUAL;

        case PIPE_FUNC_GREATER:
                return MALI_ALT_FUNC_GREATER;

        case PIPE_FUNC_NOTEQUAL:
                return MALI_ALT_FUNC_NOTEQUAL;

        case PIPE_FUNC_GEQUAL:
                return MALI_ALT_FUNC_GEQUAL;

        case PIPE_FUNC_ALWAYS:
                return MALI_ALT_FUNC_ALWAYS;
        }

        assert (0);
        return 0; /* Unreachable */
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
        }

        assert (0);
        return 0; /* Unreachable */
}

static void
panfrost_make_stencil_state(const struct pipe_stencil_state *in, struct mali_stencil_test *out)
{
        out->ref = 0; /* Gallium gets it from elsewhere */

        out->mask = in->valuemask;
        out->func = panfrost_translate_compare_func(in->func);
        out->sfail = panfrost_translate_stencil_op(in->fail_op);
        out->dpfail = panfrost_translate_stencil_op(in->zfail_op);
        out->dppass = panfrost_translate_stencil_op(in->zpass_op);
}

static void
panfrost_default_shader_backend(struct panfrost_context *ctx)
{
        struct mali_shader_meta shader = {
                .alpha_coverage = ~MALI_ALPHA_COVERAGE(0.000000),

                .unknown2_3 = MALI_DEPTH_FUNC(MALI_FUNC_ALWAYS) | 0x3010,
                .unknown2_4 = MALI_NO_MSAA | 0x4e0,
        };

	if (ctx->is_t6xx) {
                shader.unknown2_4 |= 0x10;
	}

        struct pipe_stencil_state default_stencil = {
                .enabled = 0,
                .func = PIPE_FUNC_ALWAYS,
                .fail_op = MALI_STENCIL_KEEP,
                .zfail_op = MALI_STENCIL_KEEP,
                .zpass_op = MALI_STENCIL_KEEP,
                .writemask = 0xFF,
                .valuemask = 0xFF
        };

        panfrost_make_stencil_state(&default_stencil, &shader.stencil_front);
        shader.stencil_mask_front = default_stencil.writemask;

        panfrost_make_stencil_state(&default_stencil, &shader.stencil_back);
        shader.stencil_mask_back = default_stencil.writemask;

        if (default_stencil.enabled)
                shader.unknown2_4 |= MALI_STENCIL_TEST;

        memcpy(&ctx->fragment_shader_core, &shader, sizeof(shader));
}

/* Generates a vertex/tiler job. This is, in some sense, the heart of the
 * graphics command stream. It should be called once per draw, accordding to
 * presentations. Set is_tiler for "tiler" jobs (fragment shader jobs, but in
 * Mali parlance, "fragment" refers to framebuffer writeout). Clear it for
 * vertex jobs. */

struct panfrost_transfer
panfrost_vertex_tiler_job(struct panfrost_context *ctx, bool is_tiler, bool is_elided_tiler)
{
        /* Each draw call corresponds to two jobs, and we want to offset to leave room for the set-value job */
        int draw_job_index = 1 + (2 * ctx->draw_count);

        struct mali_job_descriptor_header job = {
                .job_type = is_tiler ? JOB_TYPE_TILER : JOB_TYPE_VERTEX,
                .job_index = draw_job_index + (is_tiler ? 1 : 0),
#ifdef __LP64__
                .job_descriptor_size = 1,
#endif
        };

        /* Only non-elided tiler jobs have dependencies which are known at this point */

        if (is_tiler && !is_elided_tiler) {
                /* Tiler jobs depend on vertex jobs */

                job.job_dependency_index_1 = draw_job_index;

                /* Tiler jobs also depend on the previous tiler job */

                if (ctx->draw_count)
                        job.job_dependency_index_2 = draw_job_index - 1;
        }

        struct midgard_payload_vertex_tiler *payload = is_tiler ? &ctx->payload_tiler : &ctx->payload_vertex;

        /* There's some padding hacks on 32-bit */

#ifdef __LP64__
        int offset = 0;
#else
        int offset = 4;
#endif
        struct panfrost_transfer transfer = panfrost_allocate_transient(ctx, sizeof(job) + sizeof(*payload));
        memcpy(transfer.cpu, &job, sizeof(job));
        memcpy(transfer.cpu + sizeof(job) - offset, payload, sizeof(*payload));
        return transfer;
}

/* Generates a set value job. It's unclear what exactly this does, why it's
 * necessary, and when to call it. */

static void
panfrost_set_value_job(struct panfrost_context *ctx)
{
        struct mali_job_descriptor_header job = {
                .job_type = JOB_TYPE_SET_VALUE,
                .job_descriptor_size = 1,
                .job_index = 1 + (2 * ctx->draw_count),
        };

        struct mali_payload_set_value payload = {
                .out = ctx->misc_0.gpu,
                .unknown = 0x3,
        };

        struct panfrost_transfer transfer = panfrost_allocate_transient(ctx, sizeof(job) + sizeof(payload));
        memcpy(transfer.cpu, &job, sizeof(job));
        memcpy(transfer.cpu + sizeof(job), &payload, sizeof(payload));
        
        ctx->u_set_value_job = (struct mali_job_descriptor_header *) transfer.cpu;
        ctx->set_value_job = transfer.gpu;
}

static mali_ptr
panfrost_emit_varyings(
                struct panfrost_context *ctx,
                union mali_attr *slot,
                unsigned stride,
                unsigned count)
{
        mali_ptr varying_address = ctx->varying_mem.gpu + ctx->varying_height;

        /* Fill out the descriptor */
        slot->elements = varying_address | MALI_ATTR_LINEAR;
        slot->stride = stride;
        slot->size = stride * count;

        ctx->varying_height += ALIGN(slot->size, 64);
        assert(ctx->varying_height < ctx->varying_mem.size);

        return varying_address;
}

static void
panfrost_emit_point_coord(union mali_attr *slot)
{
        slot->elements = MALI_VARYING_POINT_COORD | MALI_ATTR_LINEAR;
        slot->stride = slot->size = 0;
}

static void
panfrost_emit_varying_descriptor(
                struct panfrost_context *ctx,
                unsigned invocation_count)
{
        /* Load the shaders */

        struct panfrost_shader_state *vs = &ctx->vs->variants[ctx->vs->active_variant];
        struct panfrost_shader_state *fs = &ctx->fs->variants[ctx->fs->active_variant];

        /* Allocate the varying descriptor */

        size_t vs_size = sizeof(struct mali_attr_meta) * vs->tripipe->varying_count;
        size_t fs_size = sizeof(struct mali_attr_meta) * fs->tripipe->varying_count;

        struct panfrost_transfer trans = panfrost_allocate_transient(ctx,
                        vs_size + fs_size);

        memcpy(trans.cpu, vs->varyings, vs_size);
        memcpy(trans.cpu + vs_size, fs->varyings, fs_size);

        ctx->payload_vertex.postfix.varying_meta = trans.gpu;
        ctx->payload_tiler.postfix.varying_meta = trans.gpu + vs_size;

        /* Buffer indices must be in this order per our convention */
        union mali_attr varyings[PIPE_MAX_ATTRIBS];
        unsigned idx = 0;

        /* General varyings -- use the VS's, since those are more likely to be
         * accurate on desktop */

        panfrost_emit_varyings(ctx, &varyings[idx++],
                        vs->general_varying_stride, invocation_count);

        /* fp32 vec4 gl_Position */
        ctx->payload_tiler.postfix.position_varying =
                panfrost_emit_varyings(ctx, &varyings[idx++],
                                sizeof(float) * 4, invocation_count);


        if (vs->writes_point_size || fs->reads_point_coord) {
                /* fp16 vec1 gl_PointSize */
                ctx->payload_tiler.primitive_size.pointer =
                        panfrost_emit_varyings(ctx, &varyings[idx++],
                                        2, invocation_count);
        }

        if (fs->reads_point_coord) {
                /* Special descriptor */
                panfrost_emit_point_coord(&varyings[idx++]);
        }

        mali_ptr varyings_p = panfrost_upload_transient(ctx, &varyings, idx * sizeof(union mali_attr));
        ctx->payload_vertex.postfix.varyings = varyings_p;
        ctx->payload_tiler.postfix.varyings = varyings_p;
}

static mali_ptr
panfrost_vertex_buffer_address(struct panfrost_context *ctx, unsigned i)
{
        struct pipe_vertex_buffer *buf = &ctx->vertex_buffers[i];
        struct panfrost_resource *rsrc = (struct panfrost_resource *) (buf->buffer.resource);

        return rsrc->bo->gpu + buf->buffer_offset;
}

/* Emits attributes and varying descriptors, which should be called every draw,
 * excepting some obscure circumstances */

static void
panfrost_emit_vertex_data(struct panfrost_context *ctx, struct panfrost_job *job)
{
        /* Staged mali_attr, and index into them. i =/= k, depending on the
         * vertex buffer mask */
        union mali_attr attrs[PIPE_MAX_ATTRIBS];
        unsigned k = 0;

        unsigned invocation_count = MALI_NEGATIVE(ctx->payload_tiler.prefix.invocation_count);

        for (int i = 0; i < ARRAY_SIZE(ctx->vertex_buffers); ++i) {
                if (!(ctx->vb_mask & (1 << i))) continue;

                struct pipe_vertex_buffer *buf = &ctx->vertex_buffers[i];
                struct panfrost_resource *rsrc = (struct panfrost_resource *) (buf->buffer.resource);

                if (!rsrc) continue;

                /* Align to 64 bytes by masking off the lower bits. This
                 * will be adjusted back when we fixup the src_offset in
                 * mali_attr_meta */

                mali_ptr addr = panfrost_vertex_buffer_address(ctx, i) & ~63;

                /* Offset vertex count by draw_start to make sure we upload enough */
                attrs[k].stride = buf->stride;
                attrs[k].size = rsrc->base.width0;

                panfrost_job_add_bo(job, rsrc->bo);
                attrs[k].elements = addr | MALI_ATTR_LINEAR;

                ++k;
        }

        ctx->payload_vertex.postfix.attributes = panfrost_upload_transient(ctx, attrs, k * sizeof(union mali_attr));

        panfrost_emit_varying_descriptor(ctx, invocation_count);
}

static bool
panfrost_writes_point_size(struct panfrost_context *ctx)
{
        assert(ctx->vs);
        struct panfrost_shader_state *vs = &ctx->vs->variants[ctx->vs->active_variant];

        return vs->writes_point_size && ctx->payload_tiler.prefix.draw_mode == MALI_POINTS;
}

/* Stage the attribute descriptors so we can adjust src_offset
 * to let BOs align nicely */

static void
panfrost_stage_attributes(struct panfrost_context *ctx)
{
        struct panfrost_vertex_state *so = ctx->vertex;

        size_t sz = sizeof(struct mali_attr_meta) * so->num_elements;
        struct panfrost_transfer transfer = panfrost_allocate_transient(ctx, sz);
        struct mali_attr_meta *target = (struct mali_attr_meta *) transfer.cpu;

        /* Copy as-is for the first pass */
        memcpy(target, so->hw, sz);

        /* Fixup offsets for the second pass. Recall that the hardware
         * calculates attribute addresses as:
         *
         *      addr = base + (stride * vtx) + src_offset;
         *
         * However, on Mali, base must be aligned to 64-bytes, so we
         * instead let:
         *
         *      base' = base & ~63 = base - (base & 63)
         * 
         * To compensate when using base' (see emit_vertex_data), we have
         * to adjust src_offset by the masked off piece:
         *
         *      addr' = base' + (stride * vtx) + (src_offset + (base & 63))
         *            = base - (base & 63) + (stride * vtx) + src_offset + (base & 63)
         *            = base + (stride * vtx) + src_offset
         *            = addr;
         *
         * QED.
         */

        for (unsigned i = 0; i < so->num_elements; ++i) {
                unsigned vbi = so->pipe[i].vertex_buffer_index;
                mali_ptr addr = panfrost_vertex_buffer_address(ctx, vbi);

                /* Adjust by the masked off bits of the offset */
                target[i].src_offset += (addr & 63);
        }

        ctx->payload_vertex.postfix.attribute_meta = transfer.gpu;
}

/* Go through dirty flags and actualise them in the cmdstream. */

void
panfrost_emit_for_draw(struct panfrost_context *ctx, bool with_vertex_data)
{
        struct panfrost_job *job = panfrost_get_job_for_fbo(ctx);

        if (with_vertex_data) {
                panfrost_emit_vertex_data(ctx, job);
        }

        bool msaa = ctx->rasterizer->base.multisample;

        if (ctx->dirty & PAN_DIRTY_RASTERIZER) {
                ctx->payload_tiler.gl_enables = ctx->rasterizer->tiler_gl_enables;

                /* TODO: Sample size */
                SET_BIT(ctx->fragment_shader_core.unknown2_3, MALI_HAS_MSAA, msaa);
                SET_BIT(ctx->fragment_shader_core.unknown2_4, MALI_NO_MSAA, !msaa);
        }

        /* Enable job requirements at draw-time */

        if (msaa)
                job->requirements |= PAN_REQ_MSAA;

        if (ctx->depth_stencil->depth.writemask)
                job->requirements |= PAN_REQ_DEPTH_WRITE;

        if (ctx->occlusion_query) {
                ctx->payload_tiler.gl_enables |= MALI_OCCLUSION_QUERY | MALI_OCCLUSION_PRECISE;
                ctx->payload_tiler.postfix.occlusion_counter = ctx->occlusion_query->transfer.gpu;
        }

        if (ctx->dirty & PAN_DIRTY_VS) {
                assert(ctx->vs);

                struct panfrost_shader_state *vs = &ctx->vs->variants[ctx->vs->active_variant];

                /* Late shader descriptor assignments */

                vs->tripipe->texture_count = ctx->sampler_view_count[PIPE_SHADER_VERTEX];
                vs->tripipe->sampler_count = ctx->sampler_count[PIPE_SHADER_VERTEX];

                /* Who knows */
                vs->tripipe->midgard1.unknown1 = 0x2201;

                ctx->payload_vertex.postfix._shader_upper = vs->tripipe_gpu >> 4;
        }

        if (ctx->dirty & (PAN_DIRTY_RASTERIZER | PAN_DIRTY_VS)) {
                /* Check if we need to link the gl_PointSize varying */
                if (!panfrost_writes_point_size(ctx)) {
                        /* If the size is constant, write it out. Otherwise,
                         * don't touch primitive_size (since we would clobber
                         * the pointer there) */

                        ctx->payload_tiler.primitive_size.constant = ctx->rasterizer->base.line_width;
                }
        }

        /* TODO: Maybe dirty track FS, maybe not. For now, it's transient. */
        if (ctx->fs)
                ctx->dirty |= PAN_DIRTY_FS;

        if (ctx->dirty & PAN_DIRTY_FS) {
                assert(ctx->fs);
                struct panfrost_shader_state *variant = &ctx->fs->variants[ctx->fs->active_variant];

#define COPY(name) ctx->fragment_shader_core.name = variant->tripipe->name

                COPY(shader);
                COPY(attribute_count);
                COPY(varying_count);
                COPY(midgard1.uniform_count);
                COPY(midgard1.work_count);
                COPY(midgard1.unknown2);

#undef COPY
                /* If there is a blend shader, work registers are shared */

                if (ctx->blend->has_blend_shader)
                        ctx->fragment_shader_core.midgard1.work_count = /*MAX2(ctx->fragment_shader_core.midgard1.work_count, ctx->blend->blend_work_count)*/16;

                /* Set late due to depending on render state */
                /* The one at the end seems to mean "1 UBO" */
                ctx->fragment_shader_core.midgard1.unknown1 = MALI_NO_ALPHA_TO_COVERAGE | 0x200 | 0x2201;

                /* Assign texture/sample count right before upload */
                ctx->fragment_shader_core.texture_count = ctx->sampler_view_count[PIPE_SHADER_FRAGMENT];
                ctx->fragment_shader_core.sampler_count = ctx->sampler_count[PIPE_SHADER_FRAGMENT];

                /* Assign the stencil refs late */
                ctx->fragment_shader_core.stencil_front.ref = ctx->stencil_ref.ref_value[0];
                ctx->fragment_shader_core.stencil_back.ref = ctx->stencil_ref.ref_value[1];

                /* CAN_DISCARD should be set if the fragment shader possibly
                 * contains a 'discard' instruction. It is likely this is
                 * related to optimizations related to forward-pixel kill, as
                 * per "Mali Performance 3: Is EGL_BUFFER_PRESERVED a good
                 * thing?" by Peter Harris
                 */

                if (variant->can_discard) {
                        ctx->fragment_shader_core.unknown2_3 |= MALI_CAN_DISCARD;
                        ctx->fragment_shader_core.midgard1.unknown1 &= ~MALI_NO_ALPHA_TO_COVERAGE;
                        ctx->fragment_shader_core.midgard1.unknown1 |= 0x4000;
                        ctx->fragment_shader_core.midgard1.unknown1 = 0x4200;
                }

		/* Check if we're using the default blend descriptor (fast path) */

		bool no_blending =
			!ctx->blend->has_blend_shader &&
			(ctx->blend->equation.rgb_mode == 0x122) &&
			(ctx->blend->equation.alpha_mode == 0x122) &&
			(ctx->blend->equation.color_mask == 0xf);

                /* Even on MFBD, the shader descriptor gets blend shaders. It's
                 * *also* copied to the blend_meta appended (by convention),
                 * but this is the field actually read by the hardware. (Or
                 * maybe both are read...?) */

                if (ctx->blend->has_blend_shader) {
                        ctx->fragment_shader_core.blend_shader = ctx->blend->blend_shader;
                }

                if (ctx->require_sfbd) {
                        /* When only a single render target platform is used, the blend
                         * information is inside the shader meta itself. We
                         * additionally need to signal CAN_DISCARD for nontrivial blend
                         * modes (so we're able to read back the destination buffer) */

                        if (!ctx->blend->has_blend_shader) {
                                memcpy(&ctx->fragment_shader_core.blend_equation, &ctx->blend->equation, sizeof(ctx->blend->equation));
                        }

                        if (!no_blending) {
                                ctx->fragment_shader_core.unknown2_3 |= MALI_CAN_DISCARD;
                        }
                }

                size_t size = sizeof(struct mali_shader_meta) + sizeof(struct mali_blend_meta);
                struct panfrost_transfer transfer = panfrost_allocate_transient(ctx, size);
                memcpy(transfer.cpu, &ctx->fragment_shader_core, sizeof(struct mali_shader_meta));

                ctx->payload_tiler.postfix._shader_upper = (transfer.gpu) >> 4;

                if (!ctx->require_sfbd) {
                        /* Additional blend descriptor tacked on for jobs using MFBD */

                        unsigned blend_count = 0;

                        if (ctx->blend->has_blend_shader) {
                                /* For a blend shader, the bottom nibble corresponds to
                                 * the number of work registers used, which signals the
                                 * -existence- of a blend shader */

                                assert(ctx->blend->blend_work_count >= 2);
                                blend_count |= MIN2(ctx->blend->blend_work_count, 3);
                        } else {
                                /* Otherwise, the bottom bit simply specifies if
                                 * blending (anything other than REPLACE) is enabled */


                                if (!no_blending)
                                        blend_count |= 0x1;
                        }

                        /* Second blend equation is always a simple replace */

                        uint64_t replace_magic = 0xf0122122;
                        struct mali_blend_equation replace_mode;
                        memcpy(&replace_mode, &replace_magic, sizeof(replace_mode));

                        struct mali_blend_meta blend_meta[] = {
                                {
                                        .unk1 = 0x200 | blend_count,
                                        .blend_equation_1 = ctx->blend->equation,
                                        .blend_equation_2 = replace_mode
                                },
                        };

                        if (ctx->blend->has_blend_shader) {
                                blend_meta[0].blend_shader = ctx->blend->blend_shader;
                        }

                        memcpy(transfer.cpu + sizeof(struct mali_shader_meta), blend_meta, sizeof(blend_meta));
                }
        }

        /* We stage to transient, so always dirty.. */
        panfrost_stage_attributes(ctx);

        if (ctx->dirty & PAN_DIRTY_SAMPLERS) {
                /* Upload samplers back to back, no padding */

                for (int t = 0; t <= PIPE_SHADER_FRAGMENT; ++t) {
                        if (!ctx->sampler_count[t]) continue;

                        struct panfrost_transfer transfer = panfrost_allocate_transient(ctx, sizeof(struct mali_sampler_descriptor) * ctx->sampler_count[t]);
                        struct mali_sampler_descriptor *desc = (struct mali_sampler_descriptor *) transfer.cpu;

                        for (int i = 0; i < ctx->sampler_count[t]; ++i) {
                                desc[i] = ctx->samplers[t][i]->hw;
                        }

                        if (t == PIPE_SHADER_FRAGMENT)
                                ctx->payload_tiler.postfix.sampler_descriptor = transfer.gpu;
                        else if (t == PIPE_SHADER_VERTEX)
                                ctx->payload_vertex.postfix.sampler_descriptor = transfer.gpu;
                        else
                                assert(0);
                }
        }

        if (ctx->dirty & PAN_DIRTY_TEXTURES) {
                for (int t = 0; t <= PIPE_SHADER_FRAGMENT; ++t) {
                        /* Shortcircuit */
                        if (!ctx->sampler_view_count[t]) continue;

                        uint64_t trampolines[PIPE_MAX_SHADER_SAMPLER_VIEWS];

                        for (int i = 0; i < ctx->sampler_view_count[t]; ++i) {
                                if (!ctx->sampler_views[t][i])
                                        continue;

                                struct pipe_resource *tex_rsrc = ctx->sampler_views[t][i]->base.texture;
                                struct panfrost_resource *rsrc = (struct panfrost_resource *) tex_rsrc;

                                /* Inject the addresses in, interleaving cube
                                 * faces and mip levels appropriately. */

                                for (int l = 0; l <= tex_rsrc->last_level; ++l) {
                                        for (int f = 0; f < tex_rsrc->array_size; ++f) {
                                                unsigned idx = (l * tex_rsrc->array_size) + f;

                                                ctx->sampler_views[t][i]->hw.swizzled_bitmaps[idx] =
                                                        rsrc->bo->gpu +
                                                        rsrc->bo->slices[l].offset +
                                                        f * rsrc->bo->cubemap_stride;
                                        }
                                }

                                trampolines[i] = panfrost_upload_transient(ctx, &ctx->sampler_views[t][i]->hw, sizeof(struct mali_texture_descriptor));
                        }

                        mali_ptr trampoline = panfrost_upload_transient(ctx, trampolines, sizeof(uint64_t) * ctx->sampler_view_count[t]);

                        if (t == PIPE_SHADER_FRAGMENT)
                                ctx->payload_tiler.postfix.texture_trampoline = trampoline;
                        else if (t == PIPE_SHADER_VERTEX)
                                ctx->payload_vertex.postfix.texture_trampoline = trampoline;
                        else
                                assert(0);
                }
        }

        const struct pipe_viewport_state *vp = &ctx->pipe_viewport;

        /* For flipped-Y buffers (signaled by negative scale), the translate is
         * flipped as well */

        bool invert_y = vp->scale[1] < 0.0;
        float translate_y = vp->translate[1];

        if (invert_y)
                translate_y = ctx->pipe_framebuffer.height - translate_y;

        for (int i = 0; i <= PIPE_SHADER_FRAGMENT; ++i) {
                struct panfrost_constant_buffer *buf = &ctx->constant_buffer[i];

                struct panfrost_shader_state *vs = &ctx->vs->variants[ctx->vs->active_variant];
                struct panfrost_shader_state *fs = &ctx->fs->variants[ctx->fs->active_variant];
                struct panfrost_shader_state *ss = (i == PIPE_SHADER_FRAGMENT) ? fs : vs;

                /* Allocate room for the sysval and the uniforms */
                size_t sys_size = sizeof(float) * 4 * ss->sysval_count;
                size_t size = sys_size + buf->size;
                struct panfrost_transfer transfer = panfrost_allocate_transient(ctx, size);

                /* Upload sysvals requested by the shader */
                float *uniforms = (float *) transfer.cpu;
                for (unsigned i = 0; i < ss->sysval_count; ++i) {
                        int sysval = ss->sysval[i];

                        if (sysval == PAN_SYSVAL_VIEWPORT_SCALE) {
                                uniforms[4*i + 0] = vp->scale[0];
                                uniforms[4*i + 1] = fabsf(vp->scale[1]);
                                uniforms[4*i + 2] = vp->scale[2];
                        } else if (sysval == PAN_SYSVAL_VIEWPORT_OFFSET) {
                                uniforms[4*i + 0] = vp->translate[0];
                                uniforms[4*i + 1] = translate_y;
                                uniforms[4*i + 2] = vp->translate[2];
                        } else {
                                assert(0);
                        }
                }

                /* Upload uniforms */
                memcpy(transfer.cpu + sys_size, buf->buffer, buf->size);

                int uniform_count = 0;

                struct mali_vertex_tiler_postfix *postfix;

                switch (i) {
                case PIPE_SHADER_VERTEX:
                        uniform_count = ctx->vs->variants[ctx->vs->active_variant].uniform_count;
                        postfix = &ctx->payload_vertex.postfix;
                        break;

                case PIPE_SHADER_FRAGMENT:
                        uniform_count = ctx->fs->variants[ctx->fs->active_variant].uniform_count;
                        postfix = &ctx->payload_tiler.postfix;
                        break;

                default:
                        DBG("Unknown shader stage %d in uniform upload\n", i);
                        assert(0);
                }

                /* Also attach the same buffer as a UBO for extended access */

                struct mali_uniform_buffer_meta uniform_buffers[] = {
                        {
                                .size = MALI_POSITIVE((2 + uniform_count)),
                                .ptr = transfer.gpu >> 2,
                        },
                };

                mali_ptr ubufs = panfrost_upload_transient(ctx, uniform_buffers, sizeof(uniform_buffers));
                postfix->uniforms = transfer.gpu;
                postfix->uniform_buffers = ubufs;

                buf->dirty = 0;
        }

        /* TODO: Upload the viewport somewhere more appropriate */

        /* Clip bounds are encoded as floats. The viewport itself is encoded as
         * (somewhat) asymmetric ints. */
        const struct pipe_scissor_state *ss = &ctx->scissor;

        struct mali_viewport view = {
                /* By default, do no viewport clipping, i.e. clip to (-inf,
                 * inf) in each direction. Clipping to the viewport in theory
                 * should work, but in practice causes issues when we're not
                 * explicitly trying to scissor */

                .clip_minx = -inff,
                .clip_miny = -inff,
                .clip_maxx = inff,
                .clip_maxy = inff,

                .clip_minz = 0.0,
                .clip_maxz = 1.0,
        };

        /* Always scissor to the viewport by default. */
        view.viewport0[0] = (int) (vp->translate[0] - vp->scale[0]);
        view.viewport1[0] = MALI_POSITIVE((int) (vp->translate[0] + vp->scale[0]));

        view.viewport0[1] = (int) (translate_y - fabs(vp->scale[1]));
        view.viewport1[1] = MALI_POSITIVE((int) (translate_y + fabs(vp->scale[1])));

        if (ss && ctx->rasterizer && ctx->rasterizer->base.scissor) {
                /* Invert scissor if needed */
                unsigned miny = invert_y ?
                        ctx->pipe_framebuffer.height - ss->maxy : ss->miny;

                unsigned maxy = invert_y ?
                        ctx->pipe_framebuffer.height - ss->miny : ss->maxy;

                /* Set the actual scissor */
                view.viewport0[0] = ss->minx;
                view.viewport0[1] = miny;
                view.viewport1[0] = MALI_POSITIVE(ss->maxx);
                view.viewport1[1] = MALI_POSITIVE(maxy);
        } 

        ctx->payload_tiler.postfix.viewport =
                panfrost_upload_transient(ctx,
                                &view,
                                sizeof(struct mali_viewport));

        ctx->dirty = 0;
}

/* Corresponds to exactly one draw, but does not submit anything */

static void
panfrost_queue_draw(struct panfrost_context *ctx)
{
        /* TODO: Expand the array? */
        if (ctx->draw_count >= MAX_DRAW_CALLS) {
                DBG("Job buffer overflow, ignoring draw\n");
                assert(0);
        }

        /* Handle dirty flags now */
        panfrost_emit_for_draw(ctx, true);

        struct panfrost_transfer vertex = panfrost_vertex_tiler_job(ctx, false, false);
        struct panfrost_transfer tiler = panfrost_vertex_tiler_job(ctx, true, false);

        ctx->u_vertex_jobs[ctx->vertex_job_count] = (struct mali_job_descriptor_header *) vertex.cpu;
        ctx->vertex_jobs[ctx->vertex_job_count++] = vertex.gpu;

        ctx->u_tiler_jobs[ctx->tiler_job_count] = (struct mali_job_descriptor_header *) tiler.cpu;
        ctx->tiler_jobs[ctx->tiler_job_count++] = tiler.gpu;

        ctx->draw_count++;
}

/* At the end of the frame, the vertex and tiler jobs are linked together and
 * then the fragment job is plonked at the end. Set value job is first for
 * unknown reasons. */

static void
panfrost_link_job_pair(struct mali_job_descriptor_header *first, mali_ptr next)
{
        if (first->job_descriptor_size)
                first->next_job_64 = (u64) (uintptr_t) next;
        else
                first->next_job_32 = (u32) (uintptr_t) next;
}

static void
panfrost_link_jobs(struct panfrost_context *ctx)
{
        if (ctx->draw_count) {
                /* Generate the set_value_job */
                panfrost_set_value_job(ctx);

                /* Have the first vertex job depend on the set value job */
                ctx->u_vertex_jobs[0]->job_dependency_index_1 = ctx->u_set_value_job->job_index;

                /* SV -> V */
                panfrost_link_job_pair(ctx->u_set_value_job, ctx->vertex_jobs[0]);
        }

        /* V -> V/T ; T -> T/null */
        for (int i = 0; i < ctx->vertex_job_count; ++i) {
                bool isLast = (i + 1) == ctx->vertex_job_count;

                panfrost_link_job_pair(ctx->u_vertex_jobs[i], isLast ? ctx->tiler_jobs[0] : ctx->vertex_jobs[i + 1]);
        }

        /* T -> T/null */
        for (int i = 0; i < ctx->tiler_job_count; ++i) {
                bool isLast = (i + 1) == ctx->tiler_job_count;
                panfrost_link_job_pair(ctx->u_tiler_jobs[i], isLast ? 0 : ctx->tiler_jobs[i + 1]);
        }
}

/* The entire frame is in memory -- send it off to the kernel! */

static void
panfrost_submit_frame(struct panfrost_context *ctx, bool flush_immediate,
		      struct pipe_fence_handle **fence,
                      struct panfrost_job *job)
{
        struct pipe_context *gallium = (struct pipe_context *) ctx;
        struct panfrost_screen *screen = pan_screen(gallium->screen);

        /* Edge case if screen is cleared and nothing else */
        bool has_draws = ctx->draw_count > 0;

        /* Workaround a bizarre lockup (a hardware errata?) */
        if (!has_draws)
                flush_immediate = true;

        /* A number of jobs are batched -- this must be linked and cleared */
        panfrost_link_jobs(ctx);

        ctx->draw_count = 0;
        ctx->vertex_job_count = 0;
        ctx->tiler_job_count = 0;

#ifndef DRY_RUN
        
        bool is_scanout = panfrost_is_scanout(ctx);
        screen->driver->submit_vs_fs_job(ctx, has_draws, is_scanout);

        /* If visual, we can stall a frame */

        if (!flush_immediate)
                screen->driver->force_flush_fragment(ctx, fence);

        screen->last_fragment_flushed = false;
        screen->last_job = job;

        /* If readback, flush now (hurts the pipelined performance) */
        if (flush_immediate)
                screen->driver->force_flush_fragment(ctx, fence);

        if (screen->driver->dump_counters && pan_counters_base) {
                screen->driver->dump_counters(screen);

                char filename[128];
                snprintf(filename, sizeof(filename), "%s/frame%d.mdgprf", pan_counters_base, ++performance_counter_number);
                FILE *fp = fopen(filename, "wb");
                fwrite(screen->perf_counters.cpu,  4096, sizeof(uint32_t), fp);
                fclose(fp);
        }

#endif
}

void
panfrost_flush(
        struct pipe_context *pipe,
        struct pipe_fence_handle **fence,
        unsigned flags)
{
        struct panfrost_context *ctx = pan_context(pipe);
        struct panfrost_job *job = panfrost_get_job_for_fbo(ctx);

        /* Nothing to do! */
        if (!ctx->draw_count && !job->clear) return;

        /* Whether to stall the pipeline for immediately correct results */
        bool flush_immediate = flags & PIPE_FLUSH_END_OF_FRAME;

        /* Submit the frame itself */
        panfrost_submit_frame(ctx, flush_immediate, fence, job);

        /* Prepare for the next frame */
        panfrost_invalidate_frame(ctx);
}

#define DEFINE_CASE(c) case PIPE_PRIM_##c: return MALI_##c;

static int
g2m_draw_mode(enum pipe_prim_type mode)
{
        switch (mode) {
                DEFINE_CASE(POINTS);
                DEFINE_CASE(LINES);
                DEFINE_CASE(LINE_LOOP);
                DEFINE_CASE(LINE_STRIP);
                DEFINE_CASE(TRIANGLES);
                DEFINE_CASE(TRIANGLE_STRIP);
                DEFINE_CASE(TRIANGLE_FAN);
                DEFINE_CASE(QUADS);
                DEFINE_CASE(QUAD_STRIP);
                DEFINE_CASE(POLYGON);

        default:
                DBG("Illegal draw mode %d\n", mode);
                assert(0);
                return MALI_LINE_LOOP;
        }
}

#undef DEFINE_CASE

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
                DBG("Unknown index size %d\n", size);
                assert(0);
                return 0;
        }
}

/* Gets a GPU address for the associated index buffer. Only gauranteed to be
 * good for the duration of the draw (transient), could last longer */

static mali_ptr
panfrost_get_index_buffer_mapped(struct panfrost_context *ctx, const struct pipe_draw_info *info)
{
        struct panfrost_resource *rsrc = (struct panfrost_resource *) (info->index.resource);

        off_t offset = info->start * info->index_size;

        if (!info->has_user_indices) {
                /* Only resources can be directly mapped */
                return rsrc->bo->gpu + offset;
        } else {
                /* Otherwise, we need to upload to transient memory */
                const uint8_t *ibuf8 = (const uint8_t *) info->index.user;
                return panfrost_upload_transient(ctx, ibuf8 + offset, info->count * info->index_size);
        }
}

static void
panfrost_draw_vbo(
        struct pipe_context *pipe,
        const struct pipe_draw_info *info)
{
        struct panfrost_context *ctx = pan_context(pipe);

        ctx->payload_vertex.draw_start = info->start;
        ctx->payload_tiler.draw_start = info->start;

        int mode = info->mode;

        /* Fallback for unsupported modes */

        if (!(ctx->draw_modes & (1 << mode))) {
                if (mode == PIPE_PRIM_QUADS && info->count == 4 && ctx->rasterizer && !ctx->rasterizer->base.flatshade) {
                        mode = PIPE_PRIM_TRIANGLE_FAN;
                } else {
                        if (info->count < 4) {
                                /* Degenerate case? */
                                return;
                        }

                        util_primconvert_save_rasterizer_state(ctx->primconvert, &ctx->rasterizer->base);
                        util_primconvert_draw_vbo(ctx->primconvert, info);
                        return;
                }
        }

        /* Now that we have a guaranteed terminating path, find the job.
         * Assignment commented out to prevent unused warning */

        /* struct panfrost_job *job = */ panfrost_get_job_for_fbo(ctx);

        ctx->payload_tiler.prefix.draw_mode = g2m_draw_mode(mode);

        ctx->vertex_count = info->count;

        /* For non-indexed draws, they're the same */
        unsigned invocation_count = ctx->vertex_count;

        unsigned draw_flags = 0;

        /* The draw flags interpret how primitive size is interpreted */

        if (panfrost_writes_point_size(ctx))
                draw_flags |= MALI_DRAW_VARYING_SIZE;

        /* For higher amounts of vertices (greater than what fits in a 16-bit
         * short), the other value is needed, otherwise there will be bizarre
         * rendering artefacts. It's not clear what these values mean yet. */

        draw_flags |= (mode == PIPE_PRIM_POINTS || ctx->vertex_count > 65535) ? 0x3000 : 0x18000;

        if (info->index_size) {
                /* Calculate the min/max index used so we can figure out how
                 * many times to invoke the vertex shader */

                /* Fetch / calculate index bounds */
                unsigned min_index = 0, max_index = 0;

                if (info->max_index == ~0u) {
                        u_vbuf_get_minmax_index(pipe, info, &min_index, &max_index);
                } else {
                        min_index = info->min_index;
                        max_index = info->max_index;
                }

                /* Use the corresponding values */
                invocation_count = max_index - min_index + 1;
                ctx->payload_vertex.draw_start = min_index;
                ctx->payload_tiler.draw_start = min_index;

                ctx->payload_tiler.prefix.negative_start = -min_index;
                ctx->payload_tiler.prefix.index_count = MALI_POSITIVE(info->count);

                //assert(!info->restart_index); /* TODO: Research */
                assert(!info->index_bias);

                draw_flags |= panfrost_translate_index_size(info->index_size);
                ctx->payload_tiler.prefix.indices = panfrost_get_index_buffer_mapped(ctx, info);
        } else {
                /* Index count == vertex count, if no indexing is applied, as
                 * if it is internally indexed in the expected order */

                ctx->payload_tiler.prefix.negative_start = 0;
                ctx->payload_tiler.prefix.index_count = MALI_POSITIVE(ctx->vertex_count);

                /* Reverse index state */
                ctx->payload_tiler.prefix.indices = (uintptr_t) NULL;
        }

        ctx->payload_vertex.prefix.invocation_count = MALI_POSITIVE(invocation_count);
        ctx->payload_tiler.prefix.invocation_count = MALI_POSITIVE(invocation_count);
        ctx->payload_tiler.prefix.unknown_draw = draw_flags;

        /* Fire off the draw itself */
        panfrost_queue_draw(ctx);
}

/* CSO state */

static void
panfrost_generic_cso_delete(struct pipe_context *pctx, void *hwcso)
{
        free(hwcso);
}

static void *
panfrost_create_rasterizer_state(
        struct pipe_context *pctx,
        const struct pipe_rasterizer_state *cso)
{
        struct panfrost_context *ctx = pan_context(pctx);
        struct panfrost_rasterizer *so = CALLOC_STRUCT(panfrost_rasterizer);

        so->base = *cso;

        /* Bitmask, unknown meaning of the start value */
        so->tiler_gl_enables = ctx->is_t6xx ? 0x105 : 0x7;

        so->tiler_gl_enables |= MALI_FRONT_FACE(
                                        cso->front_ccw ? MALI_CCW : MALI_CW);

        if (cso->cull_face & PIPE_FACE_FRONT)
                so->tiler_gl_enables |= MALI_CULL_FACE_FRONT;

        if (cso->cull_face & PIPE_FACE_BACK)
                so->tiler_gl_enables |= MALI_CULL_FACE_BACK;

        return so;
}

static void
panfrost_bind_rasterizer_state(
        struct pipe_context *pctx,
        void *hwcso)
{
        struct panfrost_context *ctx = pan_context(pctx);

        /* TODO: Why can't rasterizer be NULL ever? Other drivers are fine.. */
        if (!hwcso)
                return;

        ctx->rasterizer = hwcso;
        ctx->dirty |= PAN_DIRTY_RASTERIZER;
}

static void *
panfrost_create_vertex_elements_state(
        struct pipe_context *pctx,
        unsigned num_elements,
        const struct pipe_vertex_element *elements)
{
        struct panfrost_vertex_state *so = CALLOC_STRUCT(panfrost_vertex_state);

        so->num_elements = num_elements;
        memcpy(so->pipe, elements, sizeof(*elements) * num_elements);

        /* XXX: What the cornball? This is totally, 100%, unapologetically
         * nonsense. And yet it somehow fixes a regression in -bshadow
         * (previously, we allocated the descriptor here... a newer commit
         * removed that allocation, and then memory corruption led to
         * shader_meta getting overwritten in bad ways and then the whole test
         * case falling apart . TODO: LOOK INTO PLEASE XXX XXX BAD XXX XXX XXX
         */
        panfrost_allocate_chunk(pan_context(pctx), 0, HEAP_DESCRIPTOR);

        for (int i = 0; i < num_elements; ++i) {
                so->hw[i].index = elements[i].vertex_buffer_index;

                enum pipe_format fmt = elements[i].src_format;
                const struct util_format_description *desc = util_format_description(fmt);
                so->hw[i].unknown1 = 0x2;
                so->hw[i].swizzle = panfrost_get_default_swizzle(desc->nr_channels);

                so->hw[i].format = panfrost_find_format(desc);

                /* The field itself should probably be shifted over */
                so->hw[i].src_offset = elements[i].src_offset;
        }

        return so;
}

static void
panfrost_bind_vertex_elements_state(
        struct pipe_context *pctx,
        void *hwcso)
{
        struct panfrost_context *ctx = pan_context(pctx);

        ctx->vertex = hwcso;
        ctx->dirty |= PAN_DIRTY_VERTEX;
}

static void
panfrost_delete_vertex_elements_state(struct pipe_context *pctx, void *hwcso)
{
        struct panfrost_vertex_state *so = (struct panfrost_vertex_state *) hwcso;
        unsigned bytes = sizeof(struct mali_attr_meta) * so->num_elements;
        DBG("Vertex elements delete leaks descriptor (%d bytes)\n", bytes);
        free(hwcso);
}

static void *
panfrost_create_shader_state(
        struct pipe_context *pctx,
        const struct pipe_shader_state *cso)
{
        struct panfrost_shader_variants *so = CALLOC_STRUCT(panfrost_shader_variants);
        so->base = *cso;

        /* Token deep copy to prevent memory corruption */

        if (cso->type == PIPE_SHADER_IR_TGSI)
                so->base.tokens = tgsi_dup_tokens(so->base.tokens);

        return so;
}

static void
panfrost_delete_shader_state(
        struct pipe_context *pctx,
        void *so)
{
        struct panfrost_shader_variants *cso = (struct panfrost_shader_variants *) so;

        if (cso->base.type == PIPE_SHADER_IR_TGSI) {
                DBG("Deleting TGSI shader leaks duplicated tokens\n");
        }

        unsigned leak = cso->variant_count * sizeof(struct mali_shader_meta);
        DBG("Deleting shader state leaks descriptors (%d bytes), and shader bytecode\n", leak);

        free(so);
}

static void *
panfrost_create_sampler_state(
        struct pipe_context *pctx,
        const struct pipe_sampler_state *cso)
{
        struct panfrost_sampler_state *so = CALLOC_STRUCT(panfrost_sampler_state);
        so->base = *cso;

        /* sampler_state corresponds to mali_sampler_descriptor, which we can generate entirely here */

        struct mali_sampler_descriptor sampler_descriptor = {
                .filter_mode = MALI_TEX_MIN(translate_tex_filter(cso->min_img_filter))
                | MALI_TEX_MAG(translate_tex_filter(cso->mag_img_filter))
                | translate_mip_filter(cso->min_mip_filter)
                | 0x20,

                .wrap_s = translate_tex_wrap(cso->wrap_s),
                .wrap_t = translate_tex_wrap(cso->wrap_t),
                .wrap_r = translate_tex_wrap(cso->wrap_r),
                .compare_func = panfrost_translate_alt_compare_func(cso->compare_func),
                .border_color = {
                        cso->border_color.f[0],
                        cso->border_color.f[1],
                        cso->border_color.f[2],
                        cso->border_color.f[3]
                },
                .min_lod = FIXED_16(cso->min_lod),
                .max_lod = FIXED_16(cso->max_lod),
                .unknown2 = 1,
        };

        so->hw = sampler_descriptor;

        return so;
}

static void
panfrost_bind_sampler_states(
        struct pipe_context *pctx,
        enum pipe_shader_type shader,
        unsigned start_slot, unsigned num_sampler,
        void **sampler)
{
        assert(start_slot == 0);

        struct panfrost_context *ctx = pan_context(pctx);

        /* XXX: Should upload, not just copy? */
        ctx->sampler_count[shader] = num_sampler;
        memcpy(ctx->samplers[shader], sampler, num_sampler * sizeof (void *));

        ctx->dirty |= PAN_DIRTY_SAMPLERS;
}

static bool
panfrost_variant_matches(struct panfrost_context *ctx, struct panfrost_shader_state *variant)
{
        struct pipe_alpha_state *alpha = &ctx->depth_stencil->alpha;

        if (alpha->enabled || variant->alpha_state.enabled) {
                /* Make sure enable state is at least the same */
                if (alpha->enabled != variant->alpha_state.enabled) {
                        return false;
                }

                /* Check that the contents of the test are the same */
                bool same_func = alpha->func == variant->alpha_state.func;
                bool same_ref = alpha->ref_value == variant->alpha_state.ref_value;

                if (!(same_func && same_ref)) {
                        return false;
                }
        }
        /* Otherwise, we're good to go */
        return true;
}

static void
panfrost_bind_fs_state(
        struct pipe_context *pctx,
        void *hwcso)
{
        struct panfrost_context *ctx = pan_context(pctx);

        ctx->fs = hwcso;

        if (hwcso) {
                /* Match the appropriate variant */

                signed variant = -1;

                struct panfrost_shader_variants *variants = (struct panfrost_shader_variants *) hwcso;

                for (unsigned i = 0; i < variants->variant_count; ++i) {
                        if (panfrost_variant_matches(ctx, &variants->variants[i])) {
                                variant = i;
                                break;
                        }
                }

                if (variant == -1) {
                        /* No variant matched, so create a new one */
                        variant = variants->variant_count++;
                        assert(variants->variant_count < MAX_SHADER_VARIANTS);

                        variants->variants[variant].base = hwcso;
                        variants->variants[variant].alpha_state = ctx->depth_stencil->alpha;

                        /* Allocate the mapped descriptor ahead-of-time. TODO: Use for FS as well as VS */
                        struct panfrost_context *ctx = pan_context(pctx);
                        struct panfrost_transfer transfer = panfrost_allocate_chunk(ctx, sizeof(struct mali_shader_meta), HEAP_DESCRIPTOR);

                        variants->variants[variant].tripipe = (struct mali_shader_meta *) transfer.cpu;
                        variants->variants[variant].tripipe_gpu = transfer.gpu;

                }

                /* Select this variant */
                variants->active_variant = variant;

                struct panfrost_shader_state *shader_state = &variants->variants[variant];
                assert(panfrost_variant_matches(ctx, shader_state));

                /* Now we have a variant selected, so compile and go */

                if (!shader_state->compiled) {
                        panfrost_shader_compile(ctx, shader_state->tripipe, NULL, JOB_TYPE_TILER, shader_state);
                        shader_state->compiled = true;
                }
        }

        ctx->dirty |= PAN_DIRTY_FS;
}

static void
panfrost_bind_vs_state(
        struct pipe_context *pctx,
        void *hwcso)
{
        struct panfrost_context *ctx = pan_context(pctx);

        ctx->vs = hwcso;

        if (hwcso) {
                if (!ctx->vs->variants[0].compiled) {
                        ctx->vs->variants[0].base = hwcso;

                        /* TODO DRY from above */
                        struct panfrost_transfer transfer = panfrost_allocate_chunk(ctx, sizeof(struct mali_shader_meta), HEAP_DESCRIPTOR);
                        ctx->vs->variants[0].tripipe = (struct mali_shader_meta *) transfer.cpu;
                        ctx->vs->variants[0].tripipe_gpu = transfer.gpu;

                        panfrost_shader_compile(ctx, ctx->vs->variants[0].tripipe, NULL, JOB_TYPE_VERTEX, &ctx->vs->variants[0]);
                        ctx->vs->variants[0].compiled = true;
                }
        }

        ctx->dirty |= PAN_DIRTY_VS;
}

static void
panfrost_set_vertex_buffers(
        struct pipe_context *pctx,
        unsigned start_slot,
        unsigned num_buffers,
        const struct pipe_vertex_buffer *buffers)
{
        struct panfrost_context *ctx = pan_context(pctx);

        util_set_vertex_buffers_mask(ctx->vertex_buffers, &ctx->vb_mask, buffers, start_slot, num_buffers);
}

static void
panfrost_set_constant_buffer(
        struct pipe_context *pctx,
        enum pipe_shader_type shader, uint index,
        const struct pipe_constant_buffer *buf)
{
        struct panfrost_context *ctx = pan_context(pctx);
        struct panfrost_constant_buffer *pbuf = &ctx->constant_buffer[shader];

        size_t sz = buf ? buf->buffer_size : 0;

        /* Free previous buffer */

        pbuf->dirty = true;
        pbuf->size = sz;

        if (pbuf->buffer) {
                free(pbuf->buffer);
                pbuf->buffer = NULL;
        }

        /* If unbinding, we're done */

        if (!buf)
                return;

        /* Multiple constant buffers not yet supported */
        assert(index == 0);

        const uint8_t *cpu;

        struct panfrost_resource *rsrc = (struct panfrost_resource *) (buf->buffer);

        if (rsrc) {
                cpu = rsrc->bo->cpu;
        } else if (buf->user_buffer) {
                cpu = buf->user_buffer;
        } else {
                DBG("No constant buffer?\n");
                return;
        }

        /* Copy the constant buffer into the driver context for later upload */

        pbuf->buffer = malloc(sz);
        memcpy(pbuf->buffer, cpu + buf->buffer_offset, sz);
}

static void
panfrost_set_stencil_ref(
        struct pipe_context *pctx,
        const struct pipe_stencil_ref *ref)
{
        struct panfrost_context *ctx = pan_context(pctx);
        ctx->stencil_ref = *ref;

        /* Shader core dirty */
        ctx->dirty |= PAN_DIRTY_FS;
}

static struct pipe_sampler_view *
panfrost_create_sampler_view(
        struct pipe_context *pctx,
        struct pipe_resource *texture,
        const struct pipe_sampler_view *template)
{
        struct panfrost_sampler_view *so = CALLOC_STRUCT(panfrost_sampler_view);
        int bytes_per_pixel = util_format_get_blocksize(texture->format);

        pipe_reference(NULL, &texture->reference);

        struct panfrost_resource *prsrc = (struct panfrost_resource *) texture;

        so->base = *template;
        so->base.texture = texture;
        so->base.reference.count = 1;
        so->base.context = pctx;

        /* sampler_views correspond to texture descriptors, minus the texture
         * (data) itself. So, we serialise the descriptor here and cache it for
         * later. */

        /* Make sure it's something with which we're familiar */
        assert(bytes_per_pixel >= 1 && bytes_per_pixel <= 4);

        /* TODO: Detect from format better */
        const struct util_format_description *desc = util_format_description(prsrc->base.format);

        unsigned char user_swizzle[4] = {
                template->swizzle_r,
                template->swizzle_g,
                template->swizzle_b,
                template->swizzle_a
        };

        enum mali_format format = panfrost_find_format(desc);

        bool is_depth = desc->format == PIPE_FORMAT_Z32_UNORM;

        unsigned usage2_layout = 0x10;

        switch (prsrc->bo->layout) {
                case PAN_AFBC:
                        usage2_layout |= 0x8 | 0x4;
                        break;
                case PAN_TILED:
                        usage2_layout |= 0x1;
                        break;
                case PAN_LINEAR:
                        usage2_layout |= is_depth ? 0x1 : 0x2;
                        break;
                default:
                        assert(0);
                        break;
        }

        struct mali_texture_descriptor texture_descriptor = {
                .width = MALI_POSITIVE(texture->width0),
                .height = MALI_POSITIVE(texture->height0),
                .depth = MALI_POSITIVE(texture->depth0),

                /* TODO: Decode */
                .format = {
                        .swizzle = panfrost_translate_swizzle_4(desc->swizzle),
                        .format = format,

                        .usage1 = 0x0,
                        .is_not_cubemap = texture->target != PIPE_TEXTURE_CUBE,

                        .usage2 = usage2_layout
                },

                .swizzle = panfrost_translate_swizzle_4(user_swizzle)
        };

        /* TODO: Other base levels require adjusting dimensions / level numbers / etc */
        assert (template->u.tex.first_level == 0);

        /* Disable mipmapping for now to avoid regressions while automipmapping
         * is being implemented. TODO: Remove me once automipmaps work */

        //texture_descriptor.nr_mipmap_levels = template->u.tex.last_level - template->u.tex.first_level;
        texture_descriptor.nr_mipmap_levels = 0;

        so->hw = texture_descriptor;

        return (struct pipe_sampler_view *) so;
}

static void
panfrost_set_sampler_views(
        struct pipe_context *pctx,
        enum pipe_shader_type shader,
        unsigned start_slot, unsigned num_views,
        struct pipe_sampler_view **views)
{
        struct panfrost_context *ctx = pan_context(pctx);

        assert(start_slot == 0);

        ctx->sampler_view_count[shader] = num_views;
        memcpy(ctx->sampler_views[shader], views, num_views * sizeof (void *));

        ctx->dirty |= PAN_DIRTY_TEXTURES;
}

static void
panfrost_sampler_view_destroy(
        struct pipe_context *pctx,
        struct pipe_sampler_view *views)
{
        //struct panfrost_context *ctx = pan_context(pctx);

        /* TODO */

        free(views);
}

static void
panfrost_set_framebuffer_state(struct pipe_context *pctx,
                               const struct pipe_framebuffer_state *fb)
{
        struct panfrost_context *ctx = pan_context(pctx);

        /* Flush when switching away from an FBO */

        if (!panfrost_is_scanout(ctx)) {
                panfrost_flush(pctx, NULL, 0);
        }

        ctx->pipe_framebuffer.nr_cbufs = fb->nr_cbufs;
        ctx->pipe_framebuffer.samples = fb->samples;
        ctx->pipe_framebuffer.layers = fb->layers;
        ctx->pipe_framebuffer.width = fb->width;
        ctx->pipe_framebuffer.height = fb->height;

        for (int i = 0; i < PIPE_MAX_COLOR_BUFS; i++) {
                struct pipe_surface *cb = i < fb->nr_cbufs ? fb->cbufs[i] : NULL;

                /* check if changing cbuf */
                if (ctx->pipe_framebuffer.cbufs[i] == cb) continue;

                if (cb && (i != 0)) {
                        DBG("XXX: Multiple render targets not supported before t7xx!\n");
                        assert(0);
                }

                /* assign new */
                pipe_surface_reference(&ctx->pipe_framebuffer.cbufs[i], cb);

                if (!cb)
                        continue;

                if (ctx->require_sfbd)
                        ctx->vt_framebuffer_sfbd = panfrost_emit_sfbd(ctx);
                else
                        ctx->vt_framebuffer_mfbd = panfrost_emit_mfbd(ctx);

                panfrost_attach_vt_framebuffer(ctx);

                struct panfrost_resource *tex = ((struct panfrost_resource *) ctx->pipe_framebuffer.cbufs[i]->texture);
                enum pipe_format format = ctx->pipe_framebuffer.cbufs[i]->format;
                bool is_scanout = panfrost_is_scanout(ctx);

                if (!is_scanout && tex->bo->layout != PAN_AFBC && panfrost_can_afbc(format)) {
                        /* The blob is aggressive about enabling AFBC. As such,
                         * it's pretty much necessary to use it here, since we
                         * have no traces of non-compressed FBO. */

                        panfrost_enable_afbc(ctx, tex, false);
                }

                if (!is_scanout && !tex->bo->has_checksum) {
                        /* Enable transaction elimination if we can */
                        panfrost_enable_checksum(ctx, tex);
                }
        }

        {
                struct pipe_surface *zb = fb->zsbuf;

                if (ctx->pipe_framebuffer.zsbuf != zb) {
                        pipe_surface_reference(&ctx->pipe_framebuffer.zsbuf, zb);

                        if (zb) {
                                /* FBO has depth */

                                if (ctx->require_sfbd)
                                        ctx->vt_framebuffer_sfbd = panfrost_emit_sfbd(ctx);
                                else
                                        ctx->vt_framebuffer_mfbd = panfrost_emit_mfbd(ctx);

                                panfrost_attach_vt_framebuffer(ctx);

                                /* Keep the depth FBO linear */
                        }
                }
        }
}

static void *
panfrost_create_blend_state(struct pipe_context *pipe,
                            const struct pipe_blend_state *blend)
{
        struct panfrost_context *ctx = pan_context(pipe);
        struct panfrost_blend_state *so = CALLOC_STRUCT(panfrost_blend_state);
        so->base = *blend;

        /* TODO: The following features are not yet implemented */
        assert(!blend->logicop_enable);
        assert(!blend->alpha_to_coverage);
        assert(!blend->alpha_to_one);

        /* Compile the blend state, first as fixed-function if we can */

        if (panfrost_make_fixed_blend_mode(&blend->rt[0], &so->equation, blend->rt[0].colormask, &ctx->blend_color))
                return so;

        /* If we can't, compile a blend shader instead */

        panfrost_make_blend_shader(ctx, so, &ctx->blend_color);

        return so;
}

static void
panfrost_bind_blend_state(struct pipe_context *pipe,
                          void *cso)
{
        struct panfrost_context *ctx = pan_context(pipe);
        struct pipe_blend_state *blend = (struct pipe_blend_state *) cso;
        struct panfrost_blend_state *pblend = (struct panfrost_blend_state *) cso;
        ctx->blend = pblend;

        if (!blend)
                return;

        SET_BIT(ctx->fragment_shader_core.unknown2_4, MALI_NO_DITHER, !blend->dither);

        /* TODO: Attach color */

        /* Shader itself is not dirty, but the shader core is */
        ctx->dirty |= PAN_DIRTY_FS;
}

static void
panfrost_delete_blend_state(struct pipe_context *pipe,
                            void *blend)
{
        struct panfrost_blend_state *so = (struct panfrost_blend_state *) blend;

        if (so->has_blend_shader) {
                DBG("Deleting blend state leak blend shaders bytecode\n");
        }

        free(blend);
}

static void
panfrost_set_blend_color(struct pipe_context *pipe,
                         const struct pipe_blend_color *blend_color)
{
        struct panfrost_context *ctx = pan_context(pipe);

        /* If blend_color is we're unbinding, so ctx->blend_color is now undefined -> nothing to do */

        if (blend_color) {
                ctx->blend_color = *blend_color;

                /* The blend mode depends on the blend constant color, due to the
                 * fixed/programmable split. So, we're forced to regenerate the blend
                 * equation */

                /* TODO: Attach color */
        }
}

static void *
panfrost_create_depth_stencil_state(struct pipe_context *pipe,
                                    const struct pipe_depth_stencil_alpha_state *depth_stencil)
{
        return mem_dup(depth_stencil, sizeof(*depth_stencil));
}

static void
panfrost_bind_depth_stencil_state(struct pipe_context *pipe,
                                  void *cso)
{
        struct panfrost_context *ctx = pan_context(pipe);
        struct pipe_depth_stencil_alpha_state *depth_stencil = cso;
        ctx->depth_stencil = depth_stencil;

        if (!depth_stencil)
                return;

        /* Alpha does not exist in the hardware (it's not in ES3), so it's
         * emulated in the fragment shader */

        if (depth_stencil->alpha.enabled) {
                /* We need to trigger a new shader (maybe) */
                ctx->base.bind_fs_state(&ctx->base, ctx->fs);
        }

        /* Stencil state */
        SET_BIT(ctx->fragment_shader_core.unknown2_4, MALI_STENCIL_TEST, depth_stencil->stencil[0].enabled); /* XXX: which one? */

        panfrost_make_stencil_state(&depth_stencil->stencil[0], &ctx->fragment_shader_core.stencil_front);
        ctx->fragment_shader_core.stencil_mask_front = depth_stencil->stencil[0].writemask;

        panfrost_make_stencil_state(&depth_stencil->stencil[1], &ctx->fragment_shader_core.stencil_back);
        ctx->fragment_shader_core.stencil_mask_back = depth_stencil->stencil[1].writemask;

        /* Depth state (TODO: Refactor) */
        SET_BIT(ctx->fragment_shader_core.unknown2_3, MALI_DEPTH_TEST, depth_stencil->depth.enabled);

        int func = depth_stencil->depth.enabled ? depth_stencil->depth.func : PIPE_FUNC_ALWAYS;

        ctx->fragment_shader_core.unknown2_3 &= ~MALI_DEPTH_FUNC_MASK;
        ctx->fragment_shader_core.unknown2_3 |= MALI_DEPTH_FUNC(panfrost_translate_compare_func(func));

        /* Bounds test not implemented */
        assert(!depth_stencil->depth.bounds_test);

        ctx->dirty |= PAN_DIRTY_FS;
}

static void
panfrost_delete_depth_stencil_state(struct pipe_context *pipe, void *depth)
{
        free( depth );
}

static void
panfrost_set_sample_mask(struct pipe_context *pipe,
                         unsigned sample_mask)
{
}

static void
panfrost_set_clip_state(struct pipe_context *pipe,
                        const struct pipe_clip_state *clip)
{
        //struct panfrost_context *panfrost = pan_context(pipe);
}

static void
panfrost_set_viewport_states(struct pipe_context *pipe,
                             unsigned start_slot,
                             unsigned num_viewports,
                             const struct pipe_viewport_state *viewports)
{
        struct panfrost_context *ctx = pan_context(pipe);

        assert(start_slot == 0);
        assert(num_viewports == 1);

        ctx->pipe_viewport = *viewports;

#if 0
        /* TODO: What if not centered? */
        float w = abs(viewports->scale[0]) * 2.0;
        float h = abs(viewports->scale[1]) * 2.0;

        ctx->viewport.viewport1[0] = MALI_POSITIVE((int) w);
        ctx->viewport.viewport1[1] = MALI_POSITIVE((int) h);
#endif
}

static void
panfrost_set_scissor_states(struct pipe_context *pipe,
                            unsigned start_slot,
                            unsigned num_scissors,
                            const struct pipe_scissor_state *scissors)
{
        struct panfrost_context *ctx = pan_context(pipe);

        assert(start_slot == 0);
        assert(num_scissors == 1);

        ctx->scissor = *scissors;
}

static void
panfrost_set_polygon_stipple(struct pipe_context *pipe,
                             const struct pipe_poly_stipple *stipple)
{
        //struct panfrost_context *panfrost = pan_context(pipe);
}

static void
panfrost_set_active_query_state(struct pipe_context *pipe,
                                boolean enable)
{
        //struct panfrost_context *panfrost = pan_context(pipe);
}

static void
panfrost_destroy(struct pipe_context *pipe)
{
        struct panfrost_context *panfrost = pan_context(pipe);
        struct panfrost_screen *screen = pan_screen(pipe->screen);

        if (panfrost->blitter)
                util_blitter_destroy(panfrost->blitter);

        screen->driver->free_slab(screen, &panfrost->scratchpad);
        screen->driver->free_slab(screen, &panfrost->varying_mem);
        screen->driver->free_slab(screen, &panfrost->shaders);
        screen->driver->free_slab(screen, &panfrost->tiler_heap);
        screen->driver->free_slab(screen, &panfrost->misc_0);
}

static struct pipe_query *
panfrost_create_query(struct pipe_context *pipe, 
		      unsigned type,
		      unsigned index)
{
        struct panfrost_query *q = CALLOC_STRUCT(panfrost_query);

        q->type = type;
        q->index = index;

        return (struct pipe_query *) q;
}

static void
panfrost_destroy_query(struct pipe_context *pipe, struct pipe_query *q)
{
        FREE(q);
}

static boolean
panfrost_begin_query(struct pipe_context *pipe, struct pipe_query *q)
{
        struct panfrost_context *ctx = pan_context(pipe);
        struct panfrost_query *query = (struct panfrost_query *) q;

        switch (query->type) {
                case PIPE_QUERY_OCCLUSION_COUNTER:
                case PIPE_QUERY_OCCLUSION_PREDICATE:
                case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
                {
                        /* Allocate a word for the query results to be stored */
                        query->transfer = panfrost_allocate_chunk(ctx, sizeof(unsigned), HEAP_DESCRIPTOR);

                        ctx->occlusion_query = query;

                        break;
                }

                default:
                        DBG("Skipping query %d\n", query->type);
                        break;
        }

        return true;
}

static bool
panfrost_end_query(struct pipe_context *pipe, struct pipe_query *q)
{
        struct panfrost_context *ctx = pan_context(pipe);
        ctx->occlusion_query = NULL;
        return true;
}

static boolean
panfrost_get_query_result(struct pipe_context *pipe, 
                          struct pipe_query *q,
                          boolean wait,
                          union pipe_query_result *vresult)
{
        /* STUB */
        struct panfrost_query *query = (struct panfrost_query *) q;

        /* We need to flush out the jobs to actually run the counter, TODO
         * check wait, TODO wallpaper after if needed */

        panfrost_flush(pipe, NULL, PIPE_FLUSH_END_OF_FRAME);

        switch (query->type) {
                case PIPE_QUERY_OCCLUSION_COUNTER:
                case PIPE_QUERY_OCCLUSION_PREDICATE:
                case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE: {
                        /* Read back the query results */
                        unsigned *result = (unsigned *) query->transfer.cpu;
                        unsigned passed = *result;

                        if (query->type == PIPE_QUERY_OCCLUSION_COUNTER) {
                                vresult->u64 = passed;
                        } else {
                                vresult->b = !!passed;
                        }

                        break;
                }
                default:
                        DBG("Skipped query get %d\n", query->type);
                        break;
        }

        return true;
}

static struct pipe_stream_output_target *
panfrost_create_stream_output_target(struct pipe_context *pctx,
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
panfrost_stream_output_target_destroy(struct pipe_context *pctx,
                                 struct pipe_stream_output_target *target)
{
        pipe_resource_reference(&target->buffer, NULL);
        free(target);
}

static void
panfrost_set_stream_output_targets(struct pipe_context *pctx,
                              unsigned num_targets,
                              struct pipe_stream_output_target **targets,
                              const unsigned *offsets)
{
        /* STUB */
}

static void
panfrost_setup_hardware(struct panfrost_context *ctx)
{
        struct pipe_context *gallium = (struct pipe_context *) ctx;
        struct panfrost_screen *screen = pan_screen(gallium->screen);

        for (int i = 0; i < ARRAY_SIZE(ctx->transient_pools); ++i) {
                /* Allocate the beginning of the transient pool */
                int entry_size = (1 << 22); /* 4MB */

                ctx->transient_pools[i].entry_size = entry_size;
                ctx->transient_pools[i].entry_count = 1;

                ctx->transient_pools[i].entries[0] = (struct panfrost_memory_entry *) pb_slab_alloc(&screen->slabs, entry_size, HEAP_TRANSIENT);
        }

        screen->driver->allocate_slab(screen, &ctx->scratchpad, 64, false, 0, 0, 0);
        screen->driver->allocate_slab(screen, &ctx->varying_mem, 16384, false, PAN_ALLOCATE_INVISIBLE | PAN_ALLOCATE_COHERENT_LOCAL, 0, 0);
        screen->driver->allocate_slab(screen, &ctx->shaders, 4096, true, PAN_ALLOCATE_EXECUTE, 0, 0);
        screen->driver->allocate_slab(screen, &ctx->tiler_heap, 32768, false, PAN_ALLOCATE_INVISIBLE | PAN_ALLOCATE_GROWABLE, 1, 128);
        screen->driver->allocate_slab(screen, &ctx->misc_0, 128*128, false, PAN_ALLOCATE_INVISIBLE | PAN_ALLOCATE_GROWABLE, 1, 128);

}

/* New context creation, which also does hardware initialisation since I don't
 * know the better way to structure this :smirk: */

struct pipe_context *
panfrost_create_context(struct pipe_screen *screen, void *priv, unsigned flags)
{
        struct panfrost_context *ctx = CALLOC_STRUCT(panfrost_context);
        struct panfrost_screen *pscreen = pan_screen(screen);
        memset(ctx, 0, sizeof(*ctx));
        struct pipe_context *gallium = (struct pipe_context *) ctx;
        unsigned gpu_id;

        gpu_id = pscreen->driver->query_gpu_version(pscreen);

        ctx->is_t6xx = gpu_id <= 0x0750; /* For now, this flag means T760 or less */
        ctx->require_sfbd = gpu_id < 0x0750; /* T760 is the first to support MFBD */

        gallium->screen = screen;

        gallium->destroy = panfrost_destroy;

        gallium->set_framebuffer_state = panfrost_set_framebuffer_state;

        gallium->flush = panfrost_flush;
        gallium->clear = panfrost_clear;
        gallium->draw_vbo = panfrost_draw_vbo;

        gallium->set_vertex_buffers = panfrost_set_vertex_buffers;
        gallium->set_constant_buffer = panfrost_set_constant_buffer;

        gallium->set_stencil_ref = panfrost_set_stencil_ref;

        gallium->create_sampler_view = panfrost_create_sampler_view;
        gallium->set_sampler_views = panfrost_set_sampler_views;
        gallium->sampler_view_destroy = panfrost_sampler_view_destroy;

        gallium->create_rasterizer_state = panfrost_create_rasterizer_state;
        gallium->bind_rasterizer_state = panfrost_bind_rasterizer_state;
        gallium->delete_rasterizer_state = panfrost_generic_cso_delete;

        gallium->create_vertex_elements_state = panfrost_create_vertex_elements_state;
        gallium->bind_vertex_elements_state = panfrost_bind_vertex_elements_state;
        gallium->delete_vertex_elements_state = panfrost_delete_vertex_elements_state;

        gallium->create_fs_state = panfrost_create_shader_state;
        gallium->delete_fs_state = panfrost_delete_shader_state;
        gallium->bind_fs_state = panfrost_bind_fs_state;

        gallium->create_vs_state = panfrost_create_shader_state;
        gallium->delete_vs_state = panfrost_delete_shader_state;
        gallium->bind_vs_state = panfrost_bind_vs_state;

        gallium->create_sampler_state = panfrost_create_sampler_state;
        gallium->delete_sampler_state = panfrost_generic_cso_delete;
        gallium->bind_sampler_states = panfrost_bind_sampler_states;

        gallium->create_blend_state = panfrost_create_blend_state;
        gallium->bind_blend_state   = panfrost_bind_blend_state;
        gallium->delete_blend_state = panfrost_delete_blend_state;

        gallium->set_blend_color = panfrost_set_blend_color;

        gallium->create_depth_stencil_alpha_state = panfrost_create_depth_stencil_state;
        gallium->bind_depth_stencil_alpha_state   = panfrost_bind_depth_stencil_state;
        gallium->delete_depth_stencil_alpha_state = panfrost_delete_depth_stencil_state;

        gallium->set_sample_mask = panfrost_set_sample_mask;

        gallium->set_clip_state = panfrost_set_clip_state;
        gallium->set_viewport_states = panfrost_set_viewport_states;
        gallium->set_scissor_states = panfrost_set_scissor_states;
        gallium->set_polygon_stipple = panfrost_set_polygon_stipple;
        gallium->set_active_query_state = panfrost_set_active_query_state;

        gallium->create_query = panfrost_create_query;
        gallium->destroy_query = panfrost_destroy_query;
        gallium->begin_query = panfrost_begin_query;
        gallium->end_query = panfrost_end_query;
        gallium->get_query_result = panfrost_get_query_result;

        gallium->create_stream_output_target = panfrost_create_stream_output_target;
        gallium->stream_output_target_destroy = panfrost_stream_output_target_destroy;
        gallium->set_stream_output_targets = panfrost_set_stream_output_targets;

        panfrost_resource_context_init(gallium);

        pscreen->driver->init_context(ctx);

        panfrost_setup_hardware(ctx);

        /* XXX: leaks */
        gallium->stream_uploader = u_upload_create_default(gallium);
        gallium->const_uploader = gallium->stream_uploader;
        assert(gallium->stream_uploader);

        /* Midgard supports ES modes, plus QUADS/QUAD_STRIPS/POLYGON */
        ctx->draw_modes = (1 << (PIPE_PRIM_POLYGON + 1)) - 1;

        ctx->primconvert = util_primconvert_create(gallium, ctx->draw_modes);

        ctx->blitter = util_blitter_create(gallium);
        assert(ctx->blitter);

        /* Prepare for render! */

        panfrost_job_init(ctx);
        panfrost_emit_vertex_payload(ctx);
        panfrost_emit_tiler_payload(ctx);
        panfrost_invalidate_frame(ctx);
        panfrost_default_shader_backend(ctx);
        panfrost_generate_space_filler_indices();

        return gallium;
}
