/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * tva_bridge.c - the termux-va bridge: forwards VA-API decode work from the
 * Mesa VA frontend over a Unix socket to the termux-va daemon running in
 * Termux, which decodes with Android MediaCodec in hardware and returns
 * NV12 frames.
 *
 * Copyright (C) 2026 lfdevs
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * ******************************************************************************
 * MODIFICATION NOTICE (GPL-3.0 section 5)
 *
 * Parts of this file are a MODIFIED version of vaapi-driver/src/decode.c and
 * vaapi-driver/src/profiles.c from the droidspaces-media-decode project
 * (Apache License, Version 2.0): the codec capability table, the pending
 * pipeline-depth model and the is_param_set() unit classification were
 * ported from there and relicensed under GPL-3.0.  The Mesa-side wrappers
 * and the fence/pending machinery are new code written for termux-va.
 * ******************************************************************************
 *
 * Architecture (Mesa 26.x VA frontend, new video API):
 *
 *   vaRenderPicture -> frontend parses VA buffers, prepends H.264/HEVC
 *                      start codes to slice data, then calls
 *                      decode_bitstream(...)  -> we ACCUMULATE the bytes
 *   vaEndPicture    -> end_frame(...)      -> we split the accumulation into
 *                      Annex B units (one NALU per daemon length prefix),
 *                      send them, and register a fence for the picture
 *   vaSyncSurface   -> fence_wait(...)     -> we pump frames from the daemon,
 *                      stage the frame matching the picture's unit index,
 *                      and copy the visible (cropped) region into the
 *                      surface's plane resources on the caller's thread
 *
 * Threading model: everything runs on the application thread, exactly like
 * the upstream pseudo-driver - sends happen inside end_frame (bounded by
 * the session io timeout), receives happen inside end_frame/fence_wait.
 * No bridge-owned threads touch pipe_context, keeping it single-thread-safe.
 *
 * Unit-index pairing: the daemon tags every VCL input unit with an index
 * (1-based, parameter sets excluded) and carries it back on the matching
 * output frame.  A picture maps to the index of its LAST VCL unit
 * (MediaCodec stamps the completing input buffer's PTS onto the output
 * frame); frames with unknown indices fall back to FIFO matching.
 */
#include "tva_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_video_codec.h"
#include "pipe/p_video_enums.h"
#include "pipe/p_state.h"

#include "util/os_misc.h"
#include "util/os_time.h"
#include "util/u_debug.h"
#include "util/u_memory.h"
#include "util/u_video.h"
#include "vl/vl_video_buffer.h"
#include "vl/vl_winsys.h"

#include "tva_client.h"
#include "tva_protocol.h"

/* Bridge-side pipeline depth.  MUST stay <= SHM_SLOTS (8, tva_protocol.h):
 * the daemon's slot pool would otherwise stall.  Same coupling as the
 * upstream driver's DMD_PIPELINE_DEPTH. */
#define DMD_PIPELINE_DEPTH 6

/* Deadline for end_frame waiting for a free pending slot.  Matches the
 * daemon's slot wait (SHM_SLOT_WAIT_MS). */
#define TVA_PENDING_WAIT_MS SHM_SLOT_WAIT_MS

/* ----------------------------------------------------------- activation */
bool tva_bridge_active(void)
{
    const char *force = os_get_option("TERMUX_VA_BRIDGE");
    if (force && *force) {
        return !(strcmp(force, "0") == 0 || strcmp(force, "false") == 0 ||
                 strcmp(force, "off") == 0);
    }

    const char *ep = os_get_option("TERMUX_VA_SOCKET");
    if (ep && *ep)
        return true;
    ep = os_get_option("TERMUX_VA_SOCKET_DIR");
    if (ep && *ep)
        return true;

    /* Auto-detect: the daemon's default endpoint seen through the shared tmp */
    char buf[300];
    const char *def = tva_default_endpoint(buf, sizeof(buf));
    struct stat st;
    return def && stat(def, &st) == 0 && S_ISSOCK(st.st_mode);
}

/* --------------------------------------------------- capability helpers */
static bool
tva_profile_supported(enum pipe_video_profile profile)
{
    switch (profile) {
    case PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE:
    case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
    case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
    case PIPE_VIDEO_PROFILE_HEVC_MAIN:
    case PIPE_VIDEO_PROFILE_VP9_PROFILE0:
        return true;
    default:
        return false;
    }
}

static int
tva_codec_id(enum pipe_video_profile profile)
{
    switch (u_reduce_video_profile(profile)) {
    case PIPE_VIDEO_FORMAT_MPEG4_AVC:
        return CODEC_H264;
    case PIPE_VIDEO_FORMAT_HEVC:
        return CODEC_HEVC;
    case PIPE_VIDEO_FORMAT_VP9:
        return CODEC_VP9;
    default:
        return -1;
    }
}

/* --------------------------------------------------------- wrapped screen */
struct tva_screen {
    struct pipe_screen base;      /* memcpy of the real screen, overridden */
    struct pipe_screen *real;
};

static struct tva_screen *
tva_screen(struct pipe_screen *screen)
{
    return (struct tva_screen *)screen;
}

static int
tva_screen_get_video_param(struct pipe_screen *screen,
                           enum pipe_video_profile profile,
                           enum pipe_video_entrypoint entrypoint,
                           enum pipe_video_cap param)
{
    struct tva_screen *s = tva_screen(screen);

    if (entrypoint == PIPE_VIDEO_ENTRYPOINT_BITSTREAM &&
        tva_profile_supported(profile)) {
        switch (param) {
        case PIPE_VIDEO_CAP_SUPPORTED:
            return 1;
        case PIPE_VIDEO_CAP_MIN_WIDTH:
        case PIPE_VIDEO_CAP_MIN_HEIGHT:
            return 96;
        case PIPE_VIDEO_CAP_MAX_WIDTH:
            return 8192;
        case PIPE_VIDEO_CAP_MAX_HEIGHT:
            return 4320;
        case PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE:
            return 1;
        case PIPE_VIDEO_CAP_SKIP_CLEAR_SURFACE:
            /* surfaces start uninitialized; every decode target is fully
             * written by the first frame copy */
            return 1;
        case PIPE_VIDEO_CAP_SUPPORTS_CONTIGUOUS_PLANES_MAP:
            /* plane resources are separate textures; vaDeriveImage is not
             * available (CPU consumers fall back to vaGetImage) */
            return 0;
        default:
            return 0;
        }
    }

    return s->real->get_video_param(s->real, profile, entrypoint, param);
}

static bool
tva_screen_is_video_format_supported(struct pipe_screen *screen,
                                     enum pipe_format format,
                                     enum pipe_video_profile profile,
                                     enum pipe_video_entrypoint entrypoint)
{
    struct tva_screen *s = tva_screen(screen);

    if (entrypoint == PIPE_VIDEO_ENTRYPOINT_BITSTREAM &&
        tva_profile_supported(profile)) {
        if (format != PIPE_FORMAT_NV12)
            return false;
        return vl_video_buffer_is_format_supported(screen, format, profile,
                                                   entrypoint);
    }

    return s->real->is_video_format_supported(s->real, format, profile,
                                              entrypoint);
}

static void
tva_screen_destroy(struct pipe_screen *screen)
{
    struct tva_screen *s = tva_screen(screen);
    s->real->destroy(s->real);
    FREE(s);
}

static struct pipe_screen *
tva_wrap_screen(struct pipe_screen *real)
{
    struct tva_screen *s = CALLOC_STRUCT(tva_screen);
    if (!s)
        return real;   /* degrade: run unwrapped rather than fail init */

    memcpy(&s->base, real, sizeof(s->base));
    s->real = real;
    s->base.destroy = tva_screen_destroy;
    s->base.get_video_param = tva_screen_get_video_param;
    s->base.is_video_format_supported = tva_screen_is_video_format_supported;
    return &s->base;
}

/* ----------------------------------------------------------- wrapped pipe */
struct tva_pipe {
    struct pipe_context base;     /* memcpy of the real context, overridden */
    struct pipe_context *real;
};

static struct tva_pipe *
tva_pipe(struct pipe_context *context)
{
    return (struct tva_pipe *)context;
}

/* ------------------------------------------------------- bridge codec */
struct tva_fence;

struct tva_pending {
    bool in_use;
    uint32_t unit_seq;            /* last VCL unit index of the picture */
    bool ready;                   /* staged frame available */
    bool failed;                  /* session error: fence must not hang */
    bool copied;                  /* staging already written into the target */
    struct pipe_video_buffer *target;   /* borrowed from end_frame */
    uint8_t *staging;
    size_t staging_size;
    struct tva_fence *fence;
};

struct tva_fence {
    struct tva_codec *codec;
    struct tva_pending *slot;
};

struct tva_codec {
    struct pipe_video_codec base;

    struct pipe_context *pipe;    /* real context, for resource writes */

    struct tva_session *sess;

    /* Bitstream accumulation for the picture being assembled
     * (decode_bitstream copies VA buffers here; the frontend unmaps them
     * after RenderPicture returns). */
    uint8_t *acc;
    size_t acc_len;
    size_t acc_cap;

    /* Pending ring, FIFO order */
    struct tva_pending pend[DMD_PIPELINE_DEPTH];
    unsigned pend_head;           /* oldest entry */
    unsigned pend_count;

    uint64_t next_unit;           /* index to assign to the next VCL unit (1-based) */

    bool broken;                  /* session error, further decodes fail */
};

static struct tva_codec *
tva_codec(struct pipe_video_codec *codec)
{
    return (struct tva_codec *)codec;
}

/* ---------------------------- NALU classification (upstream port) */
/*
 * H.264 NAL unit type.  Returns -1 when undeterminable.  Ported from
 * upstream src/decode-daemon.c: the start code (3 or 4 bytes) must be
 * skipped before reading the header byte.
 */
static int
tva_nalu_type(const uint8_t *b, size_t len)
{
    size_t off = 0;
    if (len >= 4 && b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 1) off = 4;
    else if (len >= 3 && b[0] == 0 && b[1] == 0 && b[2] == 1) off = 3;
    if (off == 0 || off >= len) return -1;
    return b[off] & 0x1f;
}

/*
 * Whether the unit is a parameter set.  The daemon does NOT count parameter
 * sets in the unit index (vcl_in only advances for VCL units), so the
 * bridge must apply the same classification to assign indices correctly.
 * Ported from upstream src/decode-daemon.c is_param_set().
 */
static bool
tva_is_param_set(int codec_id, const uint8_t *b, size_t len)
{
    if (codec_id == CODEC_H264) {
        int t = tva_nalu_type(b, len);
        return (t == 7 || t == 8);
    }
    if (codec_id == CODEC_HEVC) {
        size_t off = 0;
        if (len >= 4 && b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 1) off = 4;
        else if (len >= 3 && b[0] == 0 && b[1] == 0 && b[2] == 1) off = 3;
        if (off == 0 || off >= len) return false;
        int t = (b[off] >> 1) & 0x3f;
        return (t == 32 || t == 33 || t == 34);
    }
    return false;
}

/* Locate the next 3-byte start code at or after `from` (the tail of a
 * 4-byte code also matches); returns len when none. */
static size_t
tva_next_start_code(const uint8_t *d, size_t len, size_t from)
{
    for (size_t i = from; i + 2 < len; i++)
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1)
            return i;
    return len;
}

/* ---------------------------- pending ring */
static struct tva_pending *
tva_pend_oldest(struct tva_codec *c)
{
    if (!c->pend_count)
        return NULL;
    return &c->pend[c->pend_head];
}

static void
tva_pend_pop(struct tva_codec *c)
{
    struct tva_pending *p = &c->pend[c->pend_head];
    free(p->staging);
    memset(p, 0, sizeof(*p));
    c->pend_head = (c->pend_head + 1) % DMD_PIPELINE_DEPTH;
    c->pend_count--;
}

/* Frames with a unit index nobody waits for (e.g. the completing-input
 * heuristic guessed wrong) are matched FIFO to the oldest pending picture. */
static struct tva_pending *
tva_pend_find(struct tva_codec *c, uint32_t unit_seq)
{
    for (unsigned i = 0; i < c->pend_count; i++) {
        struct tva_pending *p =
            &c->pend[(c->pend_head + i) % DMD_PIPELINE_DEPTH];
        if (p->in_use && !p->ready && unit_seq != 0 && p->unit_seq == unit_seq)
            return p;
    }
    return tva_pend_oldest(c);
}

/* ---------------------------- frame pump */
/*
 * Read frames from the daemon into the pending ring's staging buffers.
 * With block_ms > 0, waits up to that long for the first byte of each
 * frame; with 0, only drains what is already available.  Returns the
 * number of frames staged, or -1 on a session error.
 */
static int
tva_pump(struct tva_codec *c, int block_ms)
{
    if (!c->sess || c->broken)
        return -1;

    int staged = 0;
    for (;;) {
        struct tva_frame f;
        int r = tva_session_next_frame(c->sess, &f, block_ms);
        if (r == TVA_ERR_TIMEOUT)
            return staged;
        if (r == TVA_EOS) {
            /* the daemon closed; no more frames will come.  Flag all
             * waiters so fence_wait cannot hang. */
            for (unsigned i = 0; i < c->pend_count; i++) {
                struct tva_pending *p =
                    &c->pend[(c->pend_head + i) % DMD_PIPELINE_DEPTH];
                if (!p->ready) {
                    p->ready = true;
                    p->failed = true;
                }
            }
            return staged;
        }
        if (r < 0) {
            c->broken = true;
            for (unsigned i = 0; i < c->pend_count; i++) {
                struct tva_pending *p =
                    &c->pend[(c->pend_head + i) % DMD_PIPELINE_DEPTH];
                if (!p->ready) {
                    p->ready = true;
                    p->failed = true;
                }
            }
            return -1;
        }

        struct tva_pending *p = tva_pend_find(c, f.unit_seq);
        if (!p || p->ready) {
            /* frame nobody waits for (stale) - drop it */
            tva_session_release_frame(c->sess, &f);
            continue;
        }

        p->staging = malloc(f.size ? f.size : 1);
        if (!p->staging) {
            tva_session_release_frame(c->sess, &f);
            c->broken = true;
            return -1;
        }
        memcpy(p->staging, f.data, f.size);
        p->staging_size = f.size;
        p->ready = true;
        tva_session_release_frame(c->sess, &f);   /* return the slot promptly */
        staged++;
    }
}

/*
 * Copy one plane of a staged frame into a surface resource, honoring the
 * decoder's row stride.  Uses texture_subdata when the driver provides it,
 * otherwise falls back to a mapped transfer.
 */
static void
tva_copy_plane(struct pipe_context *pipe, struct pipe_resource *res,
               const uint8_t *data, unsigned w, unsigned h, unsigned stride)
{
    struct pipe_box box = {0, 0, 0, (int)w, (int)h, 1};

    if (pipe->texture_subdata) {
        pipe->texture_subdata(pipe, res, 0, PIPE_MAP_WRITE, &box, data,
                              stride, (uintptr_t)stride);
        return;
    }

    struct pipe_transfer *transfer = NULL;
    void *map = pipe->texture_map(pipe, res, 0, PIPE_MAP_WRITE, &box, &transfer);
    if (!map)
        return;
    for (unsigned row = 0; row < h; row++)
        memcpy((uint8_t *)map + (size_t)row * transfer->stride,
               data + (size_t)row * stride, w);
    pipe->texture_unmap(pipe, transfer);
}

/*
 * Copy a staged decoder frame into the target surface's plane resources.
 * Runs on the caller's (application) thread under the frontend's context
 * mutex.  The decoder buffer is the padded geometry (stride, slice_height,
 * closed-interval crop); the surface holds the visible w x h area.
 */
static void
tva_copy_frame(struct tva_codec *c, struct tva_pending *p)
{
    const struct tva_format *fmt = tva_session_format(c->sess);
    struct pipe_resource *res[4] = {0};

    if (!fmt || !fmt->valid) {
        debug_printf("tva: no format block received, cannot copy frame\n");
        return;
    }

    p->target->get_resources(p->target, res);
    if (!res[0] || !res[1])
        return;

    int disp_w = tva_format_display_width(fmt);
    int disp_h = tva_format_display_height(fmt);
    unsigned w = res[0]->width0;
    unsigned h = res[0]->height0;
    /* the surface holds the visible area; clamp against the crop rect */
    if (disp_w > 0 && (unsigned)disp_w < w)
        w = (unsigned)disp_w;
    if (disp_h > 0 && (unsigned)disp_h < h)
        h = (unsigned)disp_h;

    /* Y plane: crop_top*stride + crop_left is the first visible byte */
    const uint8_t *y = p->staging + (size_t)fmt->crop_top * fmt->stride
                     + fmt->crop_left;
    tva_copy_plane(c->pipe, res[0], y, w, h, (unsigned)fmt->stride);

    /* UV plane starts at stride*slice_height; half the crop offsets */
    const uint8_t *uv = p->staging + (size_t)fmt->stride * fmt->slice_height
                      + (size_t)(fmt->crop_top / 2) * fmt->stride
                      + (fmt->crop_left & ~1);
    tva_copy_plane(c->pipe, res[1], uv, (w + 1) / 2, (h + 1) / 2,
                   (unsigned)fmt->stride);
}

/* ---------------------------- codec vfuncs */
static void
tva_codec_begin_frame(struct pipe_video_codec *codec,
                      struct pipe_video_buffer *target,
                      struct pipe_picture_desc *picture)
{
    /* begin_frame is emitted once before the first slice data by the
     * frontend; the bridge delimits pictures via end_frame instead. */
    (void)codec; (void)target; (void)picture;
}

static void
tva_codec_decode_bitstream(struct pipe_video_codec *codec,
                           struct pipe_video_buffer *target,
                           struct pipe_picture_desc *picture,
                           unsigned num_buffers,
                           const void *const *buffers,
                           const unsigned *sizes)
{
    struct tva_codec *c = tva_codec(codec);

    (void)target;
    (void)picture;

    /* The frontend unmaps the VA buffers after RenderPicture returns, so
     * the data must be copied now.  The buffers already carry H.264/HEVC
     * start codes (the frontend prepends them when missing). */
    for (unsigned i = 0; i < num_buffers; i++) {
        if (c->acc_len + sizes[i] > c->acc_cap) {
            size_t nc = c->acc_cap ? c->acc_cap : 256 * 1024;
            while (nc < c->acc_len + sizes[i])
                nc += nc / 2;
            uint8_t *na = realloc(c->acc, nc);
            if (!na) {
                c->broken = true;
                return;
            }
            c->acc = na;
            c->acc_cap = nc;
        }
        memcpy(c->acc + c->acc_len, buffers[i], sizes[i]);
        c->acc_len += sizes[i];
    }
}

/*
 * Send the accumulated picture to the daemon and register the fence.
 * Returns 0 on success, non-zero to make EndPicture report
 * VA_STATUS_ERROR_OPERATION_FAILED.
 */
static int
tva_codec_end_frame(struct pipe_video_codec *codec,
                    struct pipe_video_buffer *target,
                    struct pipe_picture_desc *picture)
{
    struct tva_codec *c = tva_codec(codec);
    (void)picture;

    if (c->broken)
        return -1;

    /* Wait for room in the pending ring (backpressure, matching the
     * upstream driver's pipeline depth).  Bounded by the daemon's own slot
     * wait so a stuck daemon fails the picture instead of hanging the app. */
    int64_t deadline_ns = (int64_t)os_time_get_nano()
                        + (int64_t)TVA_PENDING_WAIT_MS * 1000000;
    while (c->pend_count >= DMD_PIPELINE_DEPTH) {
        int64_t left_ns = deadline_ns - (int64_t)os_time_get_nano();
        if (left_ns <= 0) {
            debug_printf("tva: pending ring stayed full for %d ms\n",
                         (int)TVA_PENDING_WAIT_MS);
            return -1;
        }
        tva_pump(c, (int)(left_ns / 1000000) + 1);
        if (tva_pend_oldest(c) && tva_pend_oldest(c)->ready)
            tva_pend_pop(c);
    }

    int codec_id = tva_codec_id(c->base.profile);
    enum pipe_video_format format = u_reduce_video_profile(c->base.profile);
    uint32_t last_vcl = 0;

    if (format == PIPE_VIDEO_FORMAT_MPEG4_AVC ||
        format == PIPE_VIDEO_FORMAT_HEVC) {
        /* Split the accumulation into Annex B units: exactly one NALU per
         * daemon length prefix, each KEEPING its start code.  Zeros before
         * a following start code (4-byte-code padding) are stripped; the
         * tail of the last NALU is kept verbatim (cabac_zero_words are
         * legal trailing data). */
        size_t pos = 0;
        while (pos < c->acc_len) {
            size_t sc = tva_next_start_code(c->acc, c->acc_len, pos);
            if (sc >= c->acc_len)
                break;   /* trailing bytes without a start code: dropped */
            size_t next = tva_next_start_code(c->acc, c->acc_len, sc + 3);
            if (next > c->acc_len)
                next = c->acc_len;
            size_t end = next;
            if (end < c->acc_len) {
                while (end > sc + 3 && c->acc[end - 1] == 0)
                    end--;
            }

            bool param = tva_is_param_set(codec_id, c->acc + sc, end - sc);
            int r = tva_session_send_unit(c->sess, c->acc + sc, end - sc);
            if (r != TVA_OK) {
                debug_printf("tva: send_unit failed: %s\n",
                             tva_session_last_error(c->sess));
                c->broken = true;
                return -1;
            }
            if (!param) {
                c->next_unit++;
                last_vcl = (uint32_t)c->next_unit;
            }
            pos = next;
        }
    } else {
        /* VP9 (and any future no-start-code codec): one whole frame */
        int r = tva_session_send_unit(c->sess, c->acc, c->acc_len);
        if (r != TVA_OK) {
            debug_printf("tva: send_unit failed: %s\n",
                         tva_session_last_error(c->sess));
            c->broken = true;
            return -1;
        }
        c->next_unit++;
        last_vcl = (uint32_t)c->next_unit;
    }

    if (!last_vcl) {
        /* picture contained only parameter sets: nothing to wait for */
        c->acc_len = 0;
        return 0;
    }

    struct tva_fence *fence = CALLOC_STRUCT(tva_fence);
    if (!fence) {
        c->broken = true;
        return -1;
    }

    struct tva_pending *p =
        &c->pend[(c->pend_head + c->pend_count) % DMD_PIPELINE_DEPTH];
    memset(p, 0, sizeof(*p));
    p->in_use = true;
    p->unit_seq = last_vcl;
    p->target = target;          /* borrowed; valid until the fence is reaped */
    fence->codec = c;
    fence->slot = p;
    p->fence = fence;
    c->pend_count++;

    c->acc_len = 0;

    /* Opportunistically collect whatever already came back */
    tva_pump(c, 0);
    return 0;
}

static void
tva_codec_flush(struct pipe_video_codec *codec)
{
    /* no command buffer to flush */
    (void)codec;
}

/*
 * Wait for the frame behind `fence` and write it into the surface.  Returns
 * 1 when the fence is (or became) signaled, 0 on timeout.  timeout is in
 * nanoseconds, as passed by the VA frontend's _vlVaSyncSurface.
 */
static int
tva_codec_fence_wait(struct pipe_video_codec *codec,
                     struct pipe_fence_handle *fence_handle,
                     uint64_t timeout)
{
    struct tva_codec *c = tva_codec(codec);
    struct tva_fence *fence = (struct tva_fence *)fence_handle;

    if (!fence || !fence->slot)
        return 1;

    struct tva_pending *p = fence->slot;

    int64_t deadline_ns = (int64_t)os_time_get_nano() + (int64_t)timeout;

    while (!p->ready) {
        int64_t left_ns = deadline_ns - (int64_t)os_time_get_nano();
        if (left_ns <= 0)
            return 0;
        int block_ms = (int)(left_ns / 1000000);
        if (block_ms > 1000)
            block_ms = 1000;   /* keep draining in bounded steps */
        if (tva_pump(c, block_ms) < 0)
            break;
    }

    if (p->ready && !p->failed && !p->copied && p->staging) {
        tva_copy_frame(c, p);
        p->copied = true;
    }
    return 1;
}

static void
tva_codec_destroy_fence(struct pipe_video_codec *codec,
                        struct pipe_fence_handle *fence_handle)
{
    struct tva_codec *c = tva_codec(codec);
    struct tva_fence *fence = (struct tva_fence *)fence_handle;

    if (!fence)
        return;
    if (fence->slot)
        fence->slot->fence = NULL;
    (void)c;
    FREE(fence);
}

static void
tva_codec_destroy(struct pipe_video_codec *codec)
{
    struct tva_codec *c = tva_codec(codec);

    /* Reap the ring; unreaped fences keep dangling slot pointers, which is
     * fine because destroy_fence only clears them and the slots here are
     * being freed anyway. */
    while (c->pend_count)
        tva_pend_pop(c);
    tva_session_destroy(c->sess);
    free(c->acc);
    FREE(c);
}

static void
tva_pipe_destroy(struct pipe_context *context)
{
    struct tva_pipe *tp = tva_pipe(context);
    tp->real->destroy(tp->real);
    FREE(tp);
}

static struct pipe_video_buffer *
tva_pipe_create_video_buffer(struct pipe_context *context,
                             const struct pipe_video_buffer *templat)
{
    /* The underlying drivers have no video path; the generic vl helper
     * allocates linear planar NV12 resources, which is all the bridge
     * needs (CPU copies + sampling). */
    return vl_video_buffer_create(tva_pipe(context)->real, templat);
}

static struct pipe_video_buffer *
tva_pipe_create_video_buffer_with_modifiers(
    struct pipe_context *context, const struct pipe_video_buffer *templat,
    const uint64_t *modifiers, unsigned modifiers_count)
{
    /* Modifier negotiation is meaningless for CPU-staged decode; the
     * buffers are linear either way.  (Revisit if dmabuf export of bridge
     * surfaces is wired up.) */
    (void)modifiers;
    (void)modifiers_count;
    return vl_video_buffer_create(tva_pipe(context)->real, templat);
}

static struct pipe_video_codec *
tva_pipe_create_video_codec(struct pipe_context *context,
                            const struct pipe_video_codec *templat)
{
    struct tva_pipe *tp = tva_pipe(context);

    if (templat->entrypoint != PIPE_VIDEO_ENTRYPOINT_BITSTREAM ||
        !tva_profile_supported(templat->profile))
        return NULL;   /* no encode / unsupported profiles through the bridge */

    int codec_id = tva_codec_id(templat->profile);
    if (codec_id < 0)
        return NULL;

    struct tva_codec *c = CALLOC_STRUCT(tva_codec);
    if (!c)
        return NULL;

    struct tva_session_config cfg;
    tva_session_config_defaults(&cfg);
    cfg.codec = codec_id;
    cfg.width = templat->width;
    cfg.height = templat->height;
    /* SHM zero-copy on by default; DMD_WANT_SHM=0 disables (legacy name
     * kept on purpose) */
    const char *shm = getenv("DMD_WANT_SHM");
    cfg.want_shm = !(shm && !strcmp(shm, "0"));

    struct tva_error err;
    memset(&err, 0, sizeof(err));

    c->sess = tva_session_create(&cfg, &err);
    if (!c->sess) {
        debug_printf("tva: session create failed: %s\n",
                     err.msg[0] ? err.msg : "unknown error (set DMD_VA_LOG=1)");
        FREE(c);
        return NULL;
    }

    c->pipe = tp->real;
    c->next_unit = 0;

    c->base.context = context;
    c->base.profile = templat->profile;
    c->base.level = templat->level;
    c->base.entrypoint = templat->entrypoint;
    c->base.width = templat->width;
    c->base.height = templat->height;
    c->base.max_references = templat->max_references;
    c->base.destroy = tva_codec_destroy;
    c->base.begin_frame = tva_codec_begin_frame;
    c->base.decode_macroblock = NULL;
    c->base.decode_bitstream = tva_codec_decode_bitstream;
    c->base.end_frame = tva_codec_end_frame;
    c->base.flush = tva_codec_flush;
    c->base.get_feedback = NULL;
    c->base.fence_wait = tva_codec_fence_wait;
    c->base.destroy_fence = tva_codec_destroy_fence;

    return &c->base;
}

static void
tva_wrap_pipe(struct pipe_context *real, struct pipe_screen *wrapped_screen,
              struct pipe_context **out_pipe)
{
    struct tva_pipe *tp = CALLOC_STRUCT(tva_pipe);
    if (!tp)
        return;   /* degrade: run unwrapped rather than fail init */

    memcpy(&tp->base, real, sizeof(tp->base));
    tp->real = real;
    tp->base.screen = wrapped_screen;
    tp->base.destroy = tva_pipe_destroy;
    tp->base.create_video_codec = tva_pipe_create_video_codec;
    tp->base.create_video_buffer = tva_pipe_create_video_buffer;
    tp->base.create_video_buffer_with_modifiers =
        tva_pipe_create_video_buffer_with_modifiers;
    *out_pipe = &tp->base;
}

void
tva_bridge_wrap_driver(struct vl_screen *vscreen, struct pipe_context **pipe)
{
    if (!vscreen || !pipe || !*pipe)
        return;

    struct pipe_screen *wrapped = tva_wrap_screen((*pipe)->screen);
    if (wrapped == (*pipe)->screen)
        return;   /* allocation failed: stay unwrapped */

    struct pipe_context *wrapped_pipe = NULL;
    tva_wrap_pipe(*pipe, wrapped, &wrapped_pipe);
    if (!wrapped_pipe)
        return;

    *pipe = wrapped_pipe;
    vscreen->pscreen = wrapped;
}
