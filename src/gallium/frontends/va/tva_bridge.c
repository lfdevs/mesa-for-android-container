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
 *   vaEndPicture    -> end_frame(...)      -> we send the accumulated Annex B
 *                      access unit as one daemon input unit and register a
 *                      fence for the picture
 *   vaSyncSurface   -> fence_wait(...)     -> we pump frames from the daemon,
 *                      stage the frame matching the picture's unit index,
 *                      and copy the visible (cropped) region into the
 *                      surface's plane resources on the caller's thread
 *
 * Threading model: the reader thread owns socket receives and stages complete
 * frames under pend_mutex.  end_frame runs on the application thread and
 * publishes a pending record before sending output-producing units.  The
 * fence_wait callback remains on the application thread and copies staged
 * planes through pipe_context after the reader signals the record.
 *
 * Unit-index pairing: the daemon tags every VCL input unit with an index
 * (1-based, parameter sets excluded) and carries it back on the matching
 * output frame.  A picture's complete access unit therefore maps to one
 * index; frames with unknown indices fall back to FIFO matching.
 */
#include "tva_bridge.h"

#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "drm-uapi/dma-buf.h"
#include "frontend/drm_driver.h"
#endif

#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_video_codec.h"
#include "pipe/p_video_enums.h"
#include "pipe/p_state.h"

#include "pipe-loader/pipe_loader.h"

#include "c11/threads.h"
#include "util/cnd_monotonic.h"
#include "util/format/u_format.h"
#include "util/os_misc.h"
#include "util/os_time.h"
#include "util/timespec.h"
#include "util/u_debug.h"
#include "util/u_memory.h"
#include "util/u_video.h"
#include "vl/vl_video_buffer.h"
#include "vl/vl_winsys.h"

#include "tva_client.h"
#include "tva_protocol.h"

/* Normal bridge pipeline depth.  In SHM mode it MUST stay <= SHM_SLOTS (8,
 * tva_protocol.h) or the daemon's slot pool stalls; the pending cache below
 * may temporarily grow beyond this threshold while a decoder reorders output,
 * but each SHM slot is released immediately after staging. */
#define DMD_PIPELINE_DEPTH_MAX 32
static unsigned tva_pipeline_depth_default = 6;

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

static bool
tva_dbg(void)
{
    const char *e = getenv("DMD_VA_LOG");
    return e && e[0] == '1';
}

static int tva_dbg_seq;
#define TVA_TRACE(fmt, ...) \
    do { if (tva_dbg()) \
        fprintf(stderr, "tva#%d %s: " fmt "\n", tva_dbg_seq, __func__, ##__VA_ARGS__); \
    } while (0)

/* ------------------------------------------------- screen video hooks */
/*
 * Drivers without a video path (freedreno, llvmpipe) leave
 * get_video_param / is_video_format_supported NULL, which fails the VA
 * frontend's init check.  When the bridge is active it owns video decode,
 * so its capability hooks replace generic 3D-driver hooks that would reject
 * video formats before the bridge receives the request. */

static bool
tva_profile_supported(enum pipe_video_profile profile)
{
    switch (profile) {
    case PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE:
    case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
    case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
    case PIPE_VIDEO_PROFILE_VP9_PROFILE0:
        /* HEVC Main is parsed by the frontend but its CSD (VPS/SPS/PPS) is
         * not synthesized yet, so it is not advertised. */
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

static int
tva_screen_get_video_param(struct pipe_screen *screen,
                           enum pipe_video_profile profile,
                           enum pipe_video_entrypoint entrypoint,
                           enum pipe_video_cap param)
{
    if ((entrypoint == PIPE_VIDEO_ENTRYPOINT_BITSTREAM ||
         entrypoint == PIPE_VIDEO_ENTRYPOINT_UNKNOWN) &&
        param == PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE &&
        (profile == PIPE_VIDEO_PROFILE_UNKNOWN || tva_profile_supported(profile)))
        return 1;

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

    return 0;
}

static bool
tva_screen_is_video_format_supported(struct pipe_screen *screen,
                                     enum pipe_format format,
                                     enum pipe_video_profile profile,
                                     enum pipe_video_entrypoint entrypoint)
{
    if (entrypoint != PIPE_VIDEO_ENTRYPOINT_BITSTREAM ||
        !tva_profile_supported(profile))
        return false;
    if (format != PIPE_FORMAT_NV12)
        return false;
    return vl_video_buffer_is_format_supported(screen, format, profile,
                                               entrypoint);
}

void
tva_bridge_screen_set_video_hooks(struct pipe_screen *screen)
{
    if (!screen)
        return;
    screen->get_video_param = tva_screen_get_video_param;
    screen->is_video_format_supported = tva_screen_is_video_format_supported;
}

/* ------------------------------------------------------- bridge codec */
struct tva_fence;

struct tva_pending {
    bool in_use;
    uint32_t unit_seq;            /* access-unit index of the picture */
    bool ready;                   /* staged frame available */
    bool failed;                  /* session error: fence must not hang */
    bool copied;                  /* staging already written into the target */
    unsigned waiters;              /* fence_wait callers holding this entry */
    struct pipe_resource *resources[2]; /* owned until the entry is reaped */
    uint8_t *staging;
    size_t staging_size;
    uint32_t frame_width;
    uint32_t frame_height;
    int stride;
    int slice_height;
    int crop_left;
    int crop_top;
    int crop_right;
    int crop_bottom;
    struct tva_fence *fence;
};

struct tva_fence {
    struct tva_codec *codec;
    struct tva_pending *slot;
    bool failed;           /* the associated pending entry was abandoned */
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
    struct tva_pending pend[DMD_PIPELINE_DEPTH_MAX];
    unsigned pipeline_depth;
    unsigned pend_head;           /* oldest entry */
    unsigned pend_count;

    uint64_t next_unit;           /* index to assign to the next VCL unit (1-based) */
    uint64_t frames_done;         /* staged frames counter (diagnostics) */

    /* synthesized CSD cache: re-sent to the daemon only when it changes */
    uint8_t *csd;
    size_t csd_len;

    bool broken;                  /* session error, further decodes fail */

    /* reader thread machinery */
    mtx_t pend_mutex;
    struct u_cnd_monotonic pend_cond;
    bool quitting;
    thrd_t reader;
    bool reader_started;
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

/* Caller must hold pend_mutex. */
static void
tva_pend_pop_locked(struct tva_codec *c)
{
    struct tva_pending *p = &c->pend[c->pend_head];
    if (p->fence) {
        p->fence->failed = true;
        p->fence->slot = NULL;
    }
    for (unsigned i = 0; i < ARRAY_SIZE(p->resources); i++)
        pipe_resource_reference(&p->resources[i], NULL);
    free(p->staging);
    memset(p, 0, sizeof(*p));
    c->pend_head = (c->pend_head + 1) % DMD_PIPELINE_DEPTH_MAX;
    c->pend_count--;
}

/* Caller must hold pend_mutex. */
static struct tva_pending *
tva_pend_find_locked(struct tva_codec *c, uint32_t unit_seq)
{
    for (unsigned i = 0; i < c->pend_count; i++) {
        struct tva_pending *p =
            &c->pend[(c->pend_head + i) % DMD_PIPELINE_DEPTH_MAX];
        if (p->in_use && !p->ready && unit_seq && p->unit_seq == unit_seq)
            return p;
    }

    /* A known sequence that is no longer pending was abandoned.  Do not
     * attach its late output to another surface.  Legacy peers without PTS
     * use FIFO matching because there is no other identity to compare. */
    return unit_seq ? NULL : tva_pend_oldest(c);
}

/* Caller must hold pend_mutex. */
static void
tva_fail_pending_locked(struct tva_codec *c)
{
    for (unsigned i = 0; i < c->pend_count; i++) {
        struct tva_pending *p =
            &c->pend[(c->pend_head + i) % DMD_PIPELINE_DEPTH_MAX];
        if (!p->ready) {
            p->ready = true;
            p->failed = true;
        }
    }
}

static bool
tva_codec_is_broken(struct tva_codec *c)
{
    bool broken;
    mtx_lock(&c->pend_mutex);
    broken = c->broken;
    mtx_unlock(&c->pend_mutex);
    return broken;
}

/* Caller must hold pend_mutex. */
static void
tva_mark_broken_locked(struct tva_codec *c)
{
    c->broken = true;
    tva_fail_pending_locked(c);
    u_cnd_monotonic_broadcast(&c->pend_cond);
}

static void
tva_mark_broken(struct tva_codec *c)
{
    mtx_lock(&c->pend_mutex);
    tva_mark_broken_locked(c);
    mtx_unlock(&c->pend_mutex);
}

/* Caller must hold pend_mutex. */
static void
tva_detach_fence_locked(struct tva_fence *fence, bool fail)
{
    if (!fence || !fence->slot)
        return;
    struct tva_pending *p = fence->slot;
    if (p->fence == fence)
        p->fence = NULL;
    fence->slot = NULL;
    if (fail) {
        fence->failed = true;
        if (!p->ready) {
            p->ready = true;
            p->failed = true;
        }
    }
}

static bool
tva_copy_frame(struct tva_codec *c, struct tva_pending *p);

/* Caller must hold pend_mutex.  A pending entry is reclaimable only after its
 * frame has been staged and no fence waiter is still using it.  The copy is
 * deliberately performed here, on the application thread; the reader thread
 * is restricted to socket I/O and staging. */
static int
tva_pend_retire_oldest_locked(struct tva_codec *c)
{
    struct tva_pending *p = tva_pend_oldest(c);
    if (!p || p->waiters || !p->ready)
        return 0;

    if (!p->failed && !p->copied && p->staging) {
        p->copied = tva_copy_frame(c, p);
        TVA_TRACE("retire copy unit=%u result=%d", p->unit_seq, p->copied);
        if (!p->copied)
            p->failed = true;
    }
    if (!p->copied && !p->failed)
        return -1;

    if (p->fence) {
        p->fence->failed = p->failed;
        p->fence->slot = NULL;
        p->fence = NULL;
    }
    tva_pend_pop_locked(c);
    u_cnd_monotonic_broadcast(&c->pend_cond);
    return 1;
}

/* Caller must hold pend_mutex. */
static struct tva_pending *
tva_pend_reserve_locked(struct tva_codec *c, uint32_t unit_seq,
                       struct pipe_video_buffer *target,
                       struct tva_fence *fence)
{
    while (c->pend_count >= c->pipeline_depth) {
        int retired = tva_pend_retire_oldest_locked(c);
        if (retired > 0)
            continue;
        if (retired < 0) {
            tva_mark_broken_locked(c);
            return NULL;
        }

        /* The normal threshold is a scheduling hint, not a reason to drop a
         * frame.  If the decoder is still reordering the oldest output, let
         * the host-side pending cache grow up to its fixed bound. */
        if (c->pend_count < DMD_PIPELINE_DEPTH_MAX)
            break;

        struct tva_pending *oldest = tva_pend_oldest(c);
        if (!oldest) {
            tva_mark_broken_locked(c);
            return NULL;
        }

        /* The cache is full.  Wait for the oldest frame to become available,
         * then retire it on this application thread before reserving space. */
        uint64_t deadline = os_time_get_nano() +
                            (uint64_t)SHM_SLOT_WAIT_MS * 1000000ull;
        while (!oldest->ready || oldest->waiters) {
            if (c->broken) {
                tva_fail_pending_locked(c);
                break;
            }
            uint64_t now = os_time_get_nano();
            if (now >= deadline) {
                tva_mark_broken_locked(c);
                return NULL;
            }
            struct timespec ts;
            timespec_from_nsec(&ts, MIN2(deadline, now + 200000000ull));
            u_cnd_monotonic_timedwait(&c->pend_cond, &c->pend_mutex, &ts);
            oldest = tva_pend_oldest(c);
            if (!oldest)
                break;
        }

        retired = tva_pend_retire_oldest_locked(c);
        if (retired <= 0) {
            tva_mark_broken_locked(c);
            return NULL;
        }
    }

    struct tva_pending *p =
        &c->pend[(c->pend_head + c->pend_count) % DMD_PIPELINE_DEPTH_MAX];
    memset(p, 0, sizeof(*p));
    p->in_use = true;
    p->unit_seq = unit_seq;
    if (target) {
        struct pipe_resource *res[4] = {0};
        target->get_resources(target, res);
        for (unsigned i = 0; i < ARRAY_SIZE(p->resources); i++) {
            if (res[i])
                pipe_resource_reference(&p->resources[i], res[i]);
        }
    }
    p->fence = fence;
    fence->codec = c;
    fence->slot = p;
    c->pend_count++;
    return p;
}

static bool
tva_cpu_copy_enabled(void)
{
    const char *e = getenv("DMD_VA_CPU_COPY");
    if (e && *e && (strcmp(e, "0") == 0 || strcmp(e, "false") == 0 ||
                    strcmp(e, "off") == 0))
        return false;

    /* The Chrome GPU process sanitizes TERMUX_VA_* from its inherited
     * environment, so backend-based autodetection is not reliable here.  The
     * bridge is only used for decoder output resources; use the cache-safe CPU
     * handoff by default and retain DMD_VA_CPU_COPY=0 as an escape hatch. */
    bool enabled = true;
    if (tva_dbg())
        fprintf(stderr, "tva: copy mode env=%s enabled=%d\n",
                e ? e : "<unset>", enabled);
    return enabled;
}

#ifndef _WIN32
/* KGSL exposes the same dma-buf through the VA producer and Chrome's ANGLE
 * consumer, but it does not provide an implicit cross-context GPU dependency
 * for a Gallium texture upload.  Bracket CPU writes with the dma-buf exporter
 * cache hooks so a consumer importing the fd observes completed frame data. */
static int
tva_dmabuf_write_begin(struct pipe_context *pipe, struct pipe_resource *res)
{
   if (!pipe || !pipe->screen || !pipe->screen->resource_get_handle)
      return -1;

   struct winsys_handle whandle;
   memset(&whandle, 0, sizeof(whandle));
   whandle.type = WINSYS_HANDLE_TYPE_FD;
   if (!pipe->screen->resource_get_handle(pipe->screen, pipe, res,
                                          &whandle,
                                          PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE))
      return -1;

   struct dma_buf_sync sync = {
      .flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE,
   };
   if (ioctl(whandle.handle, DMA_BUF_IOCTL_SYNC, &sync) < 0) {
      int err = errno;
      if (err != ENOTTY && err != EOPNOTSUPP && err != ENOSYS &&
          getenv("DMD_VA_LOG"))
         fprintf(stderr, "tva: dma-buf write sync start failed fd=%d errno=%d\n",
                 whandle.handle, err);
      close(whandle.handle);
      return -1;
   }
   return whandle.handle;
}

static void
tva_dmabuf_write_end(int fd)
{
   if (fd < 0)
      return;

   struct dma_buf_sync sync = {
      .flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE,
   };
   if (ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync) < 0 && getenv("DMD_VA_LOG"))
      fprintf(stderr, "tva: dma-buf write sync end failed fd=%d errno=%d\n",
              fd, errno);
   close(fd);
}
#endif

static bool
tva_copy_plane(struct pipe_context *pipe, struct pipe_resource *res,
               const uint8_t *data, unsigned w, unsigned h, unsigned stride)
{
    unsigned blocksize = util_format_get_blocksize(res->format);
    if (!blocksize || w > UINT_MAX / blocksize ||
        stride < w * blocksize)
        return false;

    struct pipe_box box = {
        .x = 0,
        .width = (int)w,
        .y = 0,
        .height = (int)h,
        .z = 0,
        .depth = 1,
    };
    TVA_TRACE("copy plane fmt=%s %ux%u stride=%u src=%02x %02x %02x %02x",
              util_format_short_name(res->format), w, h, stride,
              data[0], data[1], data[2], data[3]);
    /* CPU-mapped uploads are used by default for the cache-safe KGSL handoff.
     * Callers can force the asynchronous GPU upload path with
     * DMD_VA_CPU_COPY=0. */
    if (tva_cpu_copy_enabled() && pipe->texture_map &&
        pipe->texture_unmap) {
        TVA_TRACE("copy path=cpu");
#ifndef _WIN32
        int sync_fd = tva_dmabuf_write_begin(pipe, res);
#else
        int sync_fd = -1;
#endif
        struct pipe_transfer *transfer = NULL;
        uint8_t *dst = pipe->texture_map(pipe, res, 0, PIPE_MAP_WRITE,
                                          &box, &transfer);
        if (!dst || !transfer) {
            if (transfer)
                pipe->texture_unmap(pipe, transfer);
#ifndef _WIN32
            tva_dmabuf_write_end(sync_fd);
#endif
            return false;
        }
        unsigned row_bytes = w * blocksize;
        if (transfer->stride < row_bytes) {
            pipe->texture_unmap(pipe, transfer);
#ifndef _WIN32
            tva_dmabuf_write_end(sync_fd);
#endif
            return false;
        }
        for (unsigned y = 0; y < h; y++)
            memcpy(dst + (size_t)y * transfer->stride,
                   data + (size_t)y * stride, row_bytes);
        pipe->texture_unmap(pipe, transfer);
#ifndef _WIN32
        tva_dmabuf_write_end(sync_fd);
#endif
    } else if (pipe->texture_subdata) {
        TVA_TRACE("copy path=gpu");
        pipe->texture_subdata(pipe, res, 0, PIPE_MAP_WRITE, &box, data,
                              stride, (uintptr_t)stride);
    } else {
        return false;
    }
    return true;
}

/* texture_subdata() is asynchronous when the real context is wrapped by
 * threaded_context.  The bridge fence is the completion point exposed to VA
 * clients, so make the queued resource writes visible before signaling it. */
static bool
tva_flush_copy(struct pipe_context *pipe)
{
    struct pipe_fence_handle *fence = NULL;
    struct pipe_screen *screen;
    bool ready = true;

    if (!pipe || !pipe->flush || !(screen = pipe->screen))
        return false;

    pipe->flush(pipe, &fence, 0);
    if (fence) {
        ready = screen->fence_finish(screen, pipe, fence,
                                     OS_TIMEOUT_INFINITE);
        screen->fence_reference(screen, &fence, NULL);
    }
    TVA_TRACE("copy flush ready=%d", ready);
    return ready;
}

static void
tva_probe_resource(struct pipe_context *pipe, struct pipe_resource *res,
                   unsigned width)
{
    const char *e = getenv("DMD_VA_PROBE");
    if (!e || e[0] != '1' || !pipe || !res || !pipe->texture_map ||
        !pipe->texture_unmap)
        return;

    struct pipe_box box = {
        .x = 0,
        .width = (int)MIN2(width, 8u),
        .y = 0,
        .height = 1,
        .z = 0,
        .depth = 1,
    };
    struct pipe_transfer *transfer = NULL;
    uint8_t *map = pipe->texture_map(pipe, res, 0, PIPE_MAP_READ, &box,
                                      &transfer);
    if (!map || !transfer) {
        fprintf(stderr, "tva: resource probe map failed res=%p\n",
                (void *)res);
        if (transfer)
            pipe->texture_unmap(pipe, transfer);
        return;
    }
    fprintf(stderr, "tva: resource probe res=%p stride=%u bytes=%02x %02x %02x %02x %02x %02x %02x %02x\n",
            (void *)res, transfer->stride, map[0], map[1], map[2], map[3],
            map[4], map[5], map[6], map[7]);
    pipe->texture_unmap(pipe, transfer);
}

static bool
tva_copy_frame(struct tva_codec *c, struct tva_pending *p)
{
    if (!p->staging || !p->resources[0] || !p->resources[1] ||
        !p->frame_width || !p->frame_height || p->stride <= 0 ||
        p->slice_height <= 0 || p->crop_left < 0 || p->crop_top < 0 ||
        p->crop_right < p->crop_left || p->crop_bottom < p->crop_top ||
        p->crop_right >= (int)p->frame_width ||
        p->crop_bottom >= (int)p->frame_height ||
        p->crop_right >= p->stride || p->slice_height < (int)p->frame_height)
        return false;

    unsigned display_w = (unsigned)(p->crop_right - p->crop_left + 1);
    unsigned display_h = (unsigned)(p->crop_bottom - p->crop_top + 1);
    unsigned w = p->resources[0]->width0 < display_w ?
                 p->resources[0]->width0 : display_w;
    unsigned h = p->resources[0]->height0 < display_h ?
                 p->resources[0]->height0 : display_h;
    if (!w || !h || p->resources[1]->width0 < (w + 1) / 2 ||
        p->resources[1]->height0 < (h + 1) / 2)
        return false;

    size_t stride = (size_t)p->stride;
    size_t y_offset, uv_offset, y_end, uv_end;
    size_t y_rows = h - 1;
    size_t uv_rows = (h + 1) / 2 - 1;
    unsigned uv_w = (w + 1) / 2;
    unsigned uv_h = (h + 1) / 2;
    unsigned y_blocksize = util_format_get_blocksize(p->resources[0]->format);
    unsigned uv_blocksize = util_format_get_blocksize(p->resources[1]->format);
    size_t y_row_bytes, uv_row_bytes;
    if (!y_blocksize || !uv_blocksize ||
        w > UINT_MAX / y_blocksize || uv_w > UINT_MAX / uv_blocksize)
        return false;
    y_row_bytes = (size_t)w * y_blocksize;
    uv_row_bytes = (size_t)uv_w * uv_blocksize;
    if (stride < y_row_bytes || stride < uv_row_bytes ||
        p->resources[1]->width0 < uv_w || p->resources[1]->height0 < uv_h)
        return false;
    if ((size_t)p->crop_top > SIZE_MAX / stride ||
        (size_t)p->crop_top * stride > SIZE_MAX - (size_t)p->crop_left)
        return false;
    y_offset = (size_t)p->crop_top * stride + (size_t)p->crop_left;
    if ((size_t)p->slice_height > SIZE_MAX / stride)
        return false;
    uv_offset = (size_t)p->slice_height * stride;
    if ((size_t)(p->crop_top / 2) > SIZE_MAX / stride ||
        uv_offset > SIZE_MAX - (size_t)(p->crop_top / 2) * stride)
        return false;
    uv_offset += (size_t)(p->crop_top / 2) * stride;
    if (uv_offset > SIZE_MAX - (size_t)(p->crop_left & ~1))
        return false;
    uv_offset += (size_t)(p->crop_left & ~1);
    if (y_rows > SIZE_MAX / stride ||
        y_offset > SIZE_MAX - y_rows * stride ||
        y_offset + y_rows * stride > SIZE_MAX - y_row_bytes)
        return false;
    y_end = y_offset + y_rows * stride + y_row_bytes;
    if (uv_rows > SIZE_MAX / stride ||
        uv_offset > SIZE_MAX - uv_rows * stride ||
        uv_offset + uv_rows * stride > SIZE_MAX - uv_row_bytes)
        return false;
    uv_end = uv_offset + uv_rows * stride + uv_row_bytes;
    if (y_end > p->staging_size || uv_end > p->staging_size)
        return false;

    struct pipe_context *pipe = c->pipe;
    if (!pipe)
        return false;

    TVA_TRACE("copy frame unit=%u frame=%ux%u stride=%d slice=%d crop=%d,%d-%d,%d yoff=%zu uvoff=%zu size=%zu",
              p->unit_seq, p->frame_width, p->frame_height, p->stride,
              p->slice_height, p->crop_left, p->crop_top, p->crop_right,
              p->crop_bottom, y_offset, uv_offset, p->staging_size);

    if (!tva_copy_plane(pipe, p->resources[0], p->staging + y_offset,
                        w, h, (unsigned)p->stride))
        return false;
    if (!tva_copy_plane(pipe, p->resources[1], p->staging + uv_offset,
                        uv_w, uv_h, (unsigned)p->stride))
        return false;
    if (!tva_flush_copy(pipe))
        return false;
    tva_probe_resource(pipe, p->resources[0], w);
    tva_probe_resource(pipe, p->resources[1], uv_w * 2);
    return true;
}

/* ---------------------------- reader thread */
/*
 * The reader thread moves frames from the session into pending staging
 * buffers.  It exists because the two socket directions must always flow:
 * the application thread sends AUs inside EndPicture while MediaCodec's
 * input buffers are throttled by output consumption - with a single
 * thread, a blocking send would stall reads and the C2 decoder's input
 * queue would deadlock the pipeline.
 *
 * The reader only touches the session socket, the pending ring (under
 * pend_mutex) and its own staging buffers.  pipe_context stays on the
 * application thread: the surface copy happens in fence_wait.
 */
static int
tva_reader_thread(void *param)
{
    struct tva_codec *c = param;

    for (;;) {
        mtx_lock(&c->pend_mutex);
        bool quitting = c->quitting;
        mtx_unlock(&c->pend_mutex);
        if (quitting)
            break;

        struct tva_frame f;
        int r = tva_session_next_frame(c->sess, &f, 200);
        if (r == TVA_ERR_TIMEOUT)
            continue;
        if (r == TVA_EOS) {
            mtx_lock(&c->pend_mutex);
            tva_mark_broken_locked(c);
            mtx_unlock(&c->pend_mutex);
            break;
        }
        if (r < 0) {
            mtx_lock(&c->pend_mutex);
            tva_mark_broken_locked(c);
            mtx_unlock(&c->pend_mutex);
            break;
        }

        TVA_TRACE("reader frame unit=%u size=%zu slot=%d", f.unit_seq,
                  f.size, f.shm_slot);

        /* Match the frame to a pending picture by unit index.  Unknown
         * indices fall back to the oldest waiting entry for old peers. */
        mtx_lock(&c->pend_mutex);
        struct tva_pending *p = tva_pend_find_locked(c, f.unit_seq);
        if (!p || p->ready) {
            mtx_unlock(&c->pend_mutex);
            tva_session_release_frame(c->sess, &f);
            continue;
        }
        p->staging = malloc(f.size ? f.size : 1);
        if (!p->staging) {
            tva_mark_broken_locked(c);
            mtx_unlock(&c->pend_mutex);
            tva_session_release_frame(c->sess, &f);
            break;
        }
        memcpy(p->staging, f.data, f.size);
        p->staging_size = f.size;
        p->frame_width = f.width;
        p->frame_height = f.height;
        p->stride = f.stride;
        p->slice_height = f.slice_height;
        p->crop_left = f.crop_left;
        p->crop_top = f.crop_top;
        p->crop_right = f.crop_right;
        p->crop_bottom = f.crop_bottom;
        p->ready = true;
        c->frames_done++;
        u_cnd_monotonic_broadcast(&c->pend_cond);
        mtx_unlock(&c->pend_mutex);
        tva_session_release_frame(c->sess, &f);   /* return the slot promptly */
    }
    return 0;
}

/* ---------------------------------- H.264 CSD synthesis (SPS/PPS) */
/*
 * ffmpeg's vaapi h264 does not deliver SPS/PPS as slice data buffers, and
 * the frontend has no H264 header synthesizer (unlike HEVC/VP9).  The
 * daemon's MediaCodec needs the parameter sets as CSD, so the bridge
 * regenerates them from the parsed pipe_h264_sps/pps structures the
 * frontend fills in.  (Same role as upstream vaapi-driver's
 * h264_bitstream.c, ported to the gallium-side data model.)
 */

struct tva_bw {
    uint8_t *buf;
    size_t cap;
    size_t len;          /* bytes flushed to buf (EBSP) */
    uint32_t acc;
    unsigned nbits;
    unsigned zeros;      /* running zero count for emulation prevention */
};

static void
tva_bw_put(struct tva_bw *w, unsigned n, uint32_t v)
{
    for (int i = n - 1; i >= 0; i--) {
        unsigned bit = (v >> i) & 1;
        w->acc = (w->acc << 1) | bit;
        if (++w->nbits == 8) {
            uint8_t byte = (uint8_t)(w->acc & 0xff);
            if (w->len + 4 > w->cap) {
                w->cap = w->cap ? w->cap * 2 : 64;
                w->buf = realloc(w->buf, w->cap);
            }
            /* emulation prevention: 00 00 {0,1,2,3} -> 00 00 03 xx */
            if (w->zeros >= 2 && byte <= 3) {
                w->buf[w->len++] = 3;
                w->zeros = 0;
            }
            w->buf[w->len++] = byte;
            w->zeros = byte == 0 ? w->zeros + 1 : 0;
            w->nbits = 0;
            w->acc = 0;
        }
    }
}

static void
tva_bw_ue(struct tva_bw *w, uint32_t v)
{
    uint32_t val = v + 1;
    unsigned n = 0;
    while ((val >> n) != 1)
        n++;
    tva_bw_put(w, n, 0);
    tva_bw_put(w, n + 1, val);
}

static void
tva_bw_se(struct tva_bw *w, int32_t v)
{
    uint32_t k = v <= 0 ? (uint32_t)(-2 * v) : (uint32_t)(2 * v - 1);
    tva_bw_ue(w, k);
}

static void
tva_bw_rbsp_trailing(struct tva_bw *w)
{
    tva_bw_put(w, 1, 1);
    while (w->nbits)
        tva_bw_put(w, 1, 0);
}

/*
 * Build the SPS NALU (with NAL header + emulation prevention) from the
 * frontend-parsed struct.  Returns the RBSP size; the NAL header byte is
 * written first (nal_ref_idc=3, type=7).
 */
static size_t
tva_build_h264_sps(const struct pipe_h264_sps *sps, unsigned max_refs,
                   unsigned visible_width, unsigned visible_height,
                   uint8_t **out)
{
    struct tva_bw w = {0};

    /* profile_idc does not exist in VA-API; derive it exactly like the
     * upstream driver's derive_profile_idc(): the fields written below are
     * only self-consistent for the profile chosen here. */
    uint8_t profile_idc;
    if (sps->bit_depth_luma_minus8 || sps->bit_depth_chroma_minus8 ||
        sps->chroma_format_idc > 1)
        profile_idc = 100;   /* high: carries chroma/depth fields */
    else
        profile_idc = 77;    /* main: baseline has no CABAC */

    tva_bw_put(&w, 8, 0x67);             /* nal_ref_idc=3, type=7 */
    tva_bw_put(&w, 8, profile_idc);
    tva_bw_put(&w, 8, 0);                /* constraint flags + reserved */
    tva_bw_put(&w, 8, sps->level_idc ? sps->level_idc : 40);
    tva_bw_ue(&w, 0);                    /* seq_parameter_set_id */
    if (profile_idc == 100) {
        tva_bw_ue(&w, sps->chroma_format_idc);
        if (sps->chroma_format_idc == 3)
            tva_bw_put(&w, 1, sps->separate_colour_plane_flag);
        tva_bw_ue(&w, sps->bit_depth_luma_minus8);
        tva_bw_ue(&w, sps->bit_depth_chroma_minus8);
        tva_bw_put(&w, 1, 0);            /* qpprime_y_zero_transform_bypass */
        tva_bw_put(&w, 1, 0);            /* seq_scaling_matrix_present */
    }
    tva_bw_ue(&w, sps->log2_max_frame_num_minus4);
    tva_bw_ue(&w, sps->pic_order_cnt_type);
    if (sps->pic_order_cnt_type == 0)
        tva_bw_ue(&w, sps->log2_max_pic_order_cnt_lsb_minus4);
    else if (sps->pic_order_cnt_type == 1) {
        tva_bw_put(&w, 1, sps->delta_pic_order_always_zero_flag);
        tva_bw_se(&w, sps->offset_for_non_ref_pic);
        tva_bw_se(&w, sps->offset_for_top_to_bottom_field);
        tva_bw_ue(&w, sps->num_ref_frames_in_pic_order_cnt_cycle);
        for (unsigned i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle; i++)
            tva_bw_se(&w, sps->offset_for_ref_frame[i]);
    }
    /* the VA picture param carries the real DPB size; sps->max_num_ref_frames
     * itself is never filled by the frontend */
    tva_bw_ue(&w, max_refs ? max_refs : 1);
    tva_bw_put(&w, 1, sps->gaps_in_frame_num_value_allowed_flag);
    tva_bw_ue(&w, sps->pic_width_in_mbs_minus1);
    tva_bw_ue(&w, sps->pic_height_in_mbs_minus1);
    tva_bw_put(&w, 1, sps->frame_mbs_only_flag);
    if (!sps->frame_mbs_only_flag)
        tva_bw_put(&w, 1, sps->mb_adaptive_frame_field_flag);
    tva_bw_put(&w, 1, sps->direct_8x8_inference_flag);

    /* VA exposes the coded macroblock dimensions, while the video template
     * retains the visible dimensions requested by the application.  Preserve
     * a right/bottom crop when those differ; Qualcomm's decoder otherwise
     * treats the synthetic stream as 816 pixels wide and may reject the
     * 810-pixel output surfaces. */
    unsigned coded_width = (sps->pic_width_in_mbs_minus1 + 1) * 16;
    unsigned coded_height = (sps->pic_height_in_mbs_minus1 + 1) * 16 *
                            (sps->frame_mbs_only_flag ? 1 : 2);
    unsigned crop_unit_x = sps->chroma_format_idc == 0 ? 1 : 2;
    unsigned crop_unit_y = sps->chroma_format_idc == 0 ?
                           (sps->frame_mbs_only_flag ? 1 : 2) :
                           (sps->frame_mbs_only_flag ? 2 : 4);
    unsigned crop_right = 0, crop_bottom = 0;
    if (visible_width && coded_width > visible_width &&
        (coded_width - visible_width) % crop_unit_x == 0)
        crop_right = (coded_width - visible_width) / crop_unit_x;
    if (visible_height && coded_height > visible_height &&
        (coded_height - visible_height) % crop_unit_y == 0)
        crop_bottom = (coded_height - visible_height) / crop_unit_y;
    bool cropped = crop_right || crop_bottom;
    tva_bw_put(&w, 1, cropped);
    if (cropped) {
        tva_bw_ue(&w, 0);                /* frame_crop_left_offset */
        tva_bw_ue(&w, crop_right);
        tva_bw_ue(&w, 0);                /* frame_crop_top_offset */
        tva_bw_ue(&w, crop_bottom);
    }
    /* The VA picture descriptor does not carry the original VUI.  Emit only a
     * minimal parameter block and, importantly, do not invent a bitstream
     * restriction: the source stream may permit reordering (the test stream
     * does), and Qualcomm C2 treats a fabricated max_num_reorder_frames=0 as
     * a different stream, dropping output after its initial reorder window.
     * The remaining VUI flags are left absent because their values are not
     * represented by the VA descriptor. */
    tva_bw_put(&w, 1, 1);                /* vui_parameters_present */
    tva_bw_put(&w, 1, 0);                /* aspect_ratio_info_present */
    tva_bw_put(&w, 1, 0);                /* overscan_info_present */
    tva_bw_put(&w, 1, 0);                /* video_signal_type_present */
    tva_bw_put(&w, 1, 0);                /* chroma_loc_info_present */
    tva_bw_put(&w, 1, 0);                /* timing_info_present */
    tva_bw_put(&w, 1, 0);                /* nal_hrd_parameters_present */
    tva_bw_put(&w, 1, 0);                /* vcl_hrd_parameters_present */
    tva_bw_put(&w, 1, 0);                /* pic_struct_present */
    tva_bw_put(&w, 1, 0);                /* bitstream_restriction_flag */
    tva_bw_rbsp_trailing(&w);
    *out = w.buf;
    return w.len;
}

/* PPS NALU: nal_ref_idc=3, type=8 */
static size_t
tva_build_h264_pps(const struct pipe_h264_pps *pps, unsigned max_refs,
                   uint8_t **out)
{
    struct tva_bw w = {0};

    tva_bw_put(&w, 8, 0x68);
    tva_bw_ue(&w, 0);                    /* pic_parameter_set_id */
    tva_bw_ue(&w, 0);                    /* seq_parameter_set_id */
    tva_bw_put(&w, 1, pps->entropy_coding_mode_flag);
    tva_bw_put(&w, 1, pps->bottom_field_pic_order_in_frame_present_flag);
    tva_bw_ue(&w, pps->num_slice_groups_minus1);
    /* VAPictureParameterBufferH264 has no PPS default-reference fields.  The
     * parser leaves these Gallium fields at zero, but a stream can legally
     * omit num_ref_idx_active_override_flag and rely on defaults (the test
     * stream uses four references).  Use the decoder's DPB size as the safe
     * fallback unless a future frontend supplies explicit defaults. */
    unsigned default_refs = max_refs ? MIN2(max_refs, 16) - 1 : 0;
    tva_bw_ue(&w, pps->num_ref_idx_l0_default_active_minus1 ?
             pps->num_ref_idx_l0_default_active_minus1 : default_refs);
    tva_bw_ue(&w, pps->num_ref_idx_l1_default_active_minus1 ?
             pps->num_ref_idx_l1_default_active_minus1 : default_refs);
    tva_bw_put(&w, 1, pps->weighted_pred_flag);
    tva_bw_put(&w, 2, pps->weighted_bipred_idc);
    tva_bw_se(&w, pps->pic_init_qp_minus26);
    tva_bw_se(&w, pps->pic_init_qs_minus26);
    tva_bw_se(&w, pps->chroma_qp_index_offset);
    tva_bw_put(&w, 1, pps->deblocking_filter_control_present_flag);
    tva_bw_put(&w, 1, pps->constrained_intra_pred_flag);
    tva_bw_put(&w, 1, pps->redundant_pic_cnt_present_flag);
    tva_bw_rbsp_trailing(&w);
    *out = w.buf;
    return w.len;
}

/* ---------------------------- codec vfuncs */
static void
tva_codec_destroy_fence(struct pipe_video_codec *codec,
                        struct pipe_fence_handle *fence_handle);

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
                tva_mark_broken(c);
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

    if (tva_codec_is_broken(c)) {
        TVA_TRACE("end_frame on broken session");
        return -1;
    }
    if (!c->sess)
        return 0;   /* TVA_NO_SESSION dry run */

    if (!c->acc_len)
        return 0;   /* an empty VA picture carries no decode work */

    int codec_id = tva_codec_id(c->base.profile);
    enum pipe_video_format format = u_reduce_video_profile(c->base.profile);
    struct tva_pending *pending = NULL;
    TVA_TRACE("end_frame enter acc=%zu profile=%d format=%d", c->acc_len,
              (int)c->base.profile, (int)format);
    uint32_t last_vcl = 0;

    if (format == PIPE_VIDEO_FORMAT_MPEG4_AVC ||
        format == PIPE_VIDEO_FORMAT_HEVC) {
        /* ffmpeg does not deliver SPS/PPS as slice data; regenerate them
         * from the frontend-parsed structures and send them as CSD units
         * whenever they change.  The daemon excludes parameter sets from
         * the unit index. */
        if (format == PIPE_VIDEO_FORMAT_MPEG4_AVC) {
            struct pipe_h264_picture_desc *h264 =
                (struct pipe_h264_picture_desc *)picture;
            if (h264 && h264->pps && h264->pps->sps) {
                uint8_t *sps_rbsp = NULL, *pps_rbsp = NULL;
                static const uint8_t sc[4] = { 0, 0, 0, 1 };
                size_t sps_rbsp_len = tva_build_h264_sps(h264->pps->sps,
                                                         c->base.max_references,
                                                         target ? target->width : c->base.width,
                                                         target ? target->height : c->base.height,
                                                         &sps_rbsp);
                size_t pps_rbsp_len = tva_build_h264_pps(h264->pps,
                                                         c->base.max_references,
                                                         &pps_rbsp);
                if (!sps_rbsp_len || !pps_rbsp_len) {
                    debug_printf("tva: CSD synthesis failed\n");
                    free(sps_rbsp);
                    free(pps_rbsp);
                    tva_mark_broken(c);
                    return -1;
                }
                size_t sps_len = sps_rbsp_len + 4;
                size_t pps_len = pps_rbsp_len + 4;
                uint8_t *sps_buf = malloc(sps_len);
                uint8_t *pps_buf = malloc(pps_len);
                if (!sps_buf || !pps_buf) {
                    free(sps_buf); free(pps_buf); free(sps_rbsp); free(pps_rbsp);
                    tva_mark_broken(c);
                    return -1;
                }
                memcpy(sps_buf, sc, 4);
                memcpy(sps_buf + 4, sps_rbsp, sps_rbsp_len);
                memcpy(pps_buf, sc, 4);
                memcpy(pps_buf + 4, pps_rbsp, pps_rbsp_len);
                free(sps_rbsp);
                free(pps_rbsp);

                size_t nlen = sps_len + pps_len;
                uint8_t *csd = malloc(nlen);
                if (!csd) {
                    free(sps_buf);
                    free(pps_buf);
                    tva_mark_broken(c);
                    return -1;
                }
                memcpy(csd, sps_buf, sps_len);
                memcpy(csd + sps_len, pps_buf, pps_len);

                if (!c->csd || c->csd_len != nlen ||
                    memcmp(c->csd, csd, nlen)) {
                    free(c->csd);
                    c->csd = csd;
                    c->csd_len = nlen;
                    csd = NULL;
                    int rc1 = tva_session_send_unit(c->sess, sps_buf, sps_len);
                    int rc2 = rc1 == TVA_OK
                              ? tva_session_send_unit(c->sess, pps_buf, pps_len)
                              : rc1;
                    if (rc2 != TVA_OK) {
                        debug_printf("tva: CSD send failed: %s\n",
                                     tva_session_last_error(c->sess));
                        tva_mark_broken(c);
                    } else {
                        TVA_TRACE("CSD sent: sps=%zu pps=%zu maxrefs=%u",
                                  sps_len, pps_len, c->base.max_references);
                    }
                }
                free(csd);
                free(sps_buf);
                free(pps_buf);
            }
            if (tva_codec_is_broken(c))
                return -1;
        }

        /* A VA picture may contain several slice NALUs.  MediaCodec consumes
         * an access unit, not an individual slice: sending each slice as a
         * separate input buffer gives the decoder several different PTS
         * values for one output frame, and the value returned on that frame
         * is implementation dependent (usually the first or last slice).
         * Keep the NALUs together so one daemon unit and one PTS identify the
         * whole picture. */
        bool have_vcl = false;
        size_t scan = 0;
        while (scan < c->acc_len) {
            size_t sc = tva_next_start_code(c->acc, c->acc_len, scan);
            if (sc >= c->acc_len)
                break;
            size_t next = tva_next_start_code(c->acc, c->acc_len, sc + 3);
            if (next > c->acc_len)
                next = c->acc_len;
            size_t end = next;
            if (end < c->acc_len) {
                while (end > sc + 3 && c->acc[end - 1] == 0)
                    end--;
            }
            if (!tva_is_param_set(codec_id, c->acc + sc, end - sc))
                have_vcl = true;
            scan = next;
        }
        if (have_vcl && c->next_unit >= UINT32_MAX) {
            debug_printf("tva: input unit sequence exhausted\n");
            c->acc_len = 0;
            tva_mark_broken(c);
            return -1;
        }
        uint32_t picture_unit = have_vcl ? (uint32_t)c->next_unit + 1
                                         : (uint32_t)c->next_unit;
        if (c->acc_len > MAX_FRAME) {
            debug_printf("tva: picture access unit too large (%zu bytes)\n",
                         c->acc_len);
            c->acc_len = 0;
            tva_mark_broken(c);
            return -1;
        }
        if (have_vcl) {
            if (picture_unit == 0) {
                tva_mark_broken(c);
                return -1;
            }
            struct tva_fence *fence = CALLOC_STRUCT(tva_fence);
            if (!fence) {
                tva_mark_broken(c);
                return -1;
            }
            mtx_lock(&c->pend_mutex);
            pending = tva_pend_reserve_locked(c, picture_unit, target, fence);
            mtx_unlock(&c->pend_mutex);
            if (!pending) {
                FREE(fence);
                c->acc_len = 0;
                return -1;
            }
            if (picture && picture->out_fence) {
                if (*picture->out_fence)
                    tva_codec_destroy_fence(codec, *picture->out_fence);
                *picture->out_fence = (struct pipe_fence_handle *)fence;
            }
        }

        TVA_TRACE("access unit len=%zu vcl=%d unit=%u", c->acc_len,
                  have_vcl, picture_unit);
        int r = tva_session_send_unit(c->sess, c->acc, c->acc_len);
        if (r != TVA_OK) {
            debug_printf("tva: send_unit failed: %s\n",
                         tva_session_last_error(c->sess));
            tva_mark_broken(c);
            return -1;
        }
        if (have_vcl) {
            c->next_unit++;
            last_vcl = (uint32_t)c->next_unit;
        }
    } else {
        /* VP9 (and any future no-start-code codec): one whole frame */
        TVA_TRACE("sending whole frame, len=%zu", c->acc_len);
        struct tva_fence *fence = CALLOC_STRUCT(tva_fence);
        if (!fence)
            return -1;
        mtx_lock(&c->pend_mutex);
        pending = tva_pend_reserve_locked(c, (uint32_t)(c->next_unit + 1),
                                          target, fence);
        mtx_unlock(&c->pend_mutex);
        if (!pending) {
            FREE(fence);
            c->acc_len = 0;
            return -1;
        }
        if (picture && picture->out_fence) {
            if (*picture->out_fence)
                tva_codec_destroy_fence(codec, *picture->out_fence);
            *picture->out_fence = (struct pipe_fence_handle *)fence;
        }
        int r = tva_session_send_unit(c->sess, c->acc, c->acc_len);
        if (r != TVA_OK) {
            debug_printf("tva: send_unit failed: %s\n",
                         tva_session_last_error(c->sess));
            mtx_lock(&c->pend_mutex);
            tva_mark_broken_locked(c);
            mtx_unlock(&c->pend_mutex);
            return -1;
        }
        c->next_unit++;
        last_vcl = (uint32_t)c->next_unit;
        mtx_lock(&c->pend_mutex);
        if (pending)
            pending->unit_seq = last_vcl;
        mtx_unlock(&c->pend_mutex);
    }

    if (!last_vcl) {
        /* picture contained only parameter sets: nothing to wait for */
        c->acc_len = 0;
        return 0;
    }

    c->acc_len = 0;
    unsigned pending_count;
    mtx_lock(&c->pend_mutex);
    pending_count = c->pend_count;
    mtx_unlock(&c->pend_mutex);
    TVA_TRACE("end_frame ok: pic unit=%u pending=%u", last_vcl, pending_count);
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

    if (!fence)
        return 1;

    struct tva_pending *p;

    /* timeout comes in ns from the frontend; VA_TIMEOUT_INFINITE arrives as
     * (uint64_t)-1 and MUST be treated as unbounded - casting it to int64
     * yields -1, which would make the deadline expire immediately and fail
     * every vaSyncSurface.  The reader thread stages frames; this thread
     * only waits on the condition variable and copies the staged frame into
     * the surface (pipe_context stays on the application thread). */
    bool infinite = timeout == UINT64_MAX;
    uint64_t deadline_ns = infinite ? UINT64_MAX :
                           os_time_get_nano() + timeout;
    int ret = 1;

    mtx_lock(&c->pend_mutex);
    if (fence->failed) {
        mtx_unlock(&c->pend_mutex);
        return 0;
    }
    p = fence->slot;
    if (!p) {
        mtx_unlock(&c->pend_mutex);
        return 1;
    }
    p->waiters++;
    while (!p->ready) {
        if (!infinite && timeout == 0) {
            ret = 0;
            break;
        }
        uint64_t now = os_time_get_nano();
        if (!infinite && now >= deadline_ns) {
            ret = 0;
            break;
        }
        uint64_t wake_ns = infinite ? now + 200000000ull :
                           MIN2(deadline_ns, now + 200000000ull);
        struct timespec ts;
        timespec_from_nsec(&ts, wake_ns);
        u_cnd_monotonic_timedwait(&c->pend_cond, &c->pend_mutex, &ts);
    }

    TVA_TRACE("fence unit=%u ready=%d failed=%d copied=%d staging=%p",
              p->unit_seq, p->ready, p->failed, p->copied, (void *)p->staging);

    if (p->ready && !p->failed && !p->copied && p->staging) {
        p->copied = tva_copy_frame(c, p);
        TVA_TRACE("fence copy unit=%u result=%d", p->unit_seq, p->copied);
        if (!p->copied)
            p->failed = true;
    }
    if (p->failed)
        ret = 0;
    p->waiters--;
    u_cnd_monotonic_broadcast(&c->pend_cond);
    mtx_unlock(&c->pend_mutex);
    return ret;
}

static void
tva_codec_destroy_fence(struct pipe_video_codec *codec,
                        struct pipe_fence_handle *fence_handle)
{
    struct tva_codec *c = tva_codec(codec);
    struct tva_fence *fence = (struct tva_fence *)fence_handle;

    if (!fence)
        return;
    mtx_lock(&c->pend_mutex);
    /* Destroying the VA fence only drops the client's wait handle.  The
     * output surface/resource may still be reused while MediaCodec is
     * reordering frames, so keep the pending record alive and copy its staged
     * frame when the matching output arrives.  Marking it failed here drops
     * the frame before the reader can pair it with the pending unit. */
    tva_detach_fence_locked(fence, false);
    u_cnd_monotonic_broadcast(&c->pend_cond);
    mtx_unlock(&c->pend_mutex);
    FREE(fence);
}

static void
tva_codec_destroy(struct pipe_video_codec *codec)
{
    struct tva_codec *c = tva_codec(codec);

    mtx_lock(&c->pend_mutex);
    c->quitting = true;
    c->broken = true;
    tva_fail_pending_locked(c);
    u_cnd_monotonic_broadcast(&c->pend_cond);
    mtx_unlock(&c->pend_mutex);
    tva_session_cancel(c->sess);
    if (c->reader_started)
        thrd_join(c->reader, NULL);
    mtx_lock(&c->pend_mutex);
    while (c->pend_count)
        tva_pend_pop_locked(c);
    mtx_unlock(&c->pend_mutex);
    mtx_destroy(&c->pend_mutex);
    u_cnd_monotonic_destroy(&c->pend_cond);
    TVA_TRACE("codec destroy: %llu units, %llu frames", (unsigned long long)c->next_unit, (unsigned long long)c->frames_done);
    tva_session_destroy(c->sess);
    free(c->csd);
    free(c->acc);
    FREE(c);
}

static struct pipe_video_buffer *
tva_pipe_create_video_buffer(struct pipe_context *context,
                             const struct pipe_video_buffer *templat)
{
    /* Bridge surfaces are CPU-filled and may be exported to Vulkan.  Allocate
     * them as linear shared resources so KGSL does not need an export-time
     * shadow allocation. */
    struct pipe_video_buffer bridge_templ = *templat;
    bridge_templ.bind |= PIPE_BIND_SHARED | PIPE_BIND_LINEAR;
    return vl_video_buffer_create(context, &bridge_templ);
}

static struct pipe_video_buffer *
tva_pipe_create_video_buffer_with_modifiers(
    struct pipe_context *context, const struct pipe_video_buffer *templat,
    const uint64_t *modifiers, unsigned modifiers_count)
{
    /* Explicit modifiers cannot change the bridge's linear CPU-copy layout. */
    (void)modifiers;
    (void)modifiers_count;
    return tva_pipe_create_video_buffer(context, templat);
}

static struct pipe_video_codec *
tva_pipe_create_video_codec(struct pipe_context *context,
                            const struct pipe_video_codec *templat)
{
    if (templat->entrypoint != PIPE_VIDEO_ENTRYPOINT_BITSTREAM ||
        !tva_profile_supported(templat->profile))
        return NULL;   /* no encode / unsupported profiles through the bridge */

    int codec_id = tva_codec_id(templat->profile);
    if (codec_id < 0)
        return NULL;

    unsigned pipeline_depth = tva_pipeline_depth_default;
    const char *d = getenv("TERMUX_VA_PIPELINE_DEPTH");
    if (d && *d) {
        long v = atol(d);
        if (v >= 2 && v <= DMD_PIPELINE_DEPTH_MAX)
            pipeline_depth = (unsigned)v;
    }

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
    if (cfg.want_shm && pipeline_depth > SHM_SLOTS)
        pipeline_depth = SHM_SLOTS;

    struct tva_error err;
    memset(&err, 0, sizeof(err));

    tva_dbg_seq++;
    TVA_TRACE("session create codec=%d %dx%d", codec_id, templat->width, templat->height);
    c->sess = tva_session_create(&cfg, &err);
    if (!c->sess) {
        debug_printf("tva: session create failed: %s\n",
                     err.msg[0] ? err.msg : "unknown error (set DMD_VA_LOG=1)");
        FREE(c);
        return NULL;
    }

    c->pipe = context;
    c->next_unit = 0;
    c->pipeline_depth = pipeline_depth;

    mtx_init(&c->pend_mutex, mtx_plain);
    if (u_cnd_monotonic_init(&c->pend_cond) != thrd_success) {
        tva_session_destroy(c->sess);
        free(c->csd);
        free(c->acc);
        mtx_destroy(&c->pend_mutex);
        FREE(c);
        return NULL;
    }
    if (thrd_create(&c->reader, tva_reader_thread, c) == thrd_success)
        c->reader_started = true;

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

    TVA_TRACE("codec ready");
    return &c->base;
}

/* ------------------------------------------------------ pipe codec hooks */
/*
 * The multimedia context's create_video_codec / create_video_buffer hooks
 * are filled in directly on the real context: drivers without a video path
 * leave them NULL, and the bridge implementations above receive the real
 * context pointer, so no wrapper object is involved anywhere.
 */
void
tva_bridge_pipe_set_codec_hooks(struct pipe_context *pipe)
{
    if (!pipe)
        return;
    pipe->create_video_codec = tva_pipe_create_video_codec;
    pipe->create_video_buffer = tva_pipe_create_video_buffer;
    pipe->create_video_buffer_with_modifiers =
        tva_pipe_create_video_buffer_with_modifiers;
}

/* ------------------------------------------------- vscreen creation */
/*
 * The stock loader path breaks on the kgsl stack in two ways:
 *
 *  1. Xiaomi/DroidSpaces kernels report the display controller's DRM node
 *     as "msm_drm" - no pipe_loader descriptor matches that name, so the
 *     kmsro/zink fallbacks engage and fail (the kgsl stack has no usable
 *     Vulkan device for zink, hence "ZINK: failed to choose pdev").
 *  2. Even on kernels reporting "msm", the msm kmd drives no GPU there -
 *     the Adreno GPU is only reachable through /dev/kgsl-3d0.
 *
 * The fork registers a "kgsl" alias of the freedreno descriptor whose
 * device layer redirects GPU submission to /dev/kgsl-3d0 while keeping the
 * handed fd as the control/identity fd (freedreno_device.c); EGL uses
 * exactly that override via MESA_LOADER_DRIVER_OVERRIDE=kgsl.  The bridge
 * does the same for VA-API by re-pointing the probed device's driver_name.
 */

static void
tva_vscreen_destroy(struct vl_screen *vscreen)
{
    vscreen->pscreen->destroy(vscreen->pscreen);
    pipe_loader_release(&vscreen->dev, 1);
    FREE(vscreen);
}

static struct vl_screen *
tva_vscreen_from_pscreen(struct pipe_screen *pscreen,
                         struct pipe_loader_device *dev)
{
    struct vl_screen *vscreen = CALLOC_STRUCT(vl_screen);
    if (!vscreen) {
        pscreen->destroy(pscreen);
        pipe_loader_release(&dev, 1);
        return NULL;
    }

    vscreen->pscreen = pscreen;
    vscreen->dev = dev;
    vscreen->destroy = tva_vscreen_destroy;
    vscreen->texture_from_drawable = NULL;
    vscreen->get_dirty_area = NULL;
    vscreen->get_timestamp = NULL;
    vscreen->set_next_timestamp = NULL;
    vscreen->get_private = NULL;
    vscreen->set_back_texture_from_output = NULL;
    return vscreen;
}

/* llvmpipe over the null sw winsys: no GPU needed, enough for the CPU
 * frame-copy paths (vainfo, ffmpeg vaMapBuffer). */
static struct vl_screen *
tva_vscreen_sw(void)
{
    struct pipe_loader_device *dev = NULL;
    if (!pipe_loader_sw_probe_null(&dev))
        return NULL;

    struct pipe_screen *pscreen = pipe_loader_create_screen(dev, false);
    if (!pscreen) {
        fprintf(stderr, "tva: llvmpipe screen creation failed\n");
        pipe_loader_release(&dev, 1);
        return NULL;
    }
    fprintf(stderr, "tva: using the llvmpipe software backend\n");
    return tva_vscreen_from_pscreen(pscreen, dev);
}

struct vl_screen *
tva_bridge_vscreen_create(int fd, bool honor_dri_prime)
{
    const char *backend = os_get_option("TERMUX_VA_GPU_BACKEND");
    if (!backend || !*backend || !strcmp(backend, "auto"))
        backend = "auto";
    if (getenv("DMD_VA_LOG"))
        fprintf(stderr, "tva: creating the bridge vscreen, backend='%s'\n", backend);

    if (!strcmp(backend, "auto") || !strcmp(backend, "kgsl")) {
        /* On the kgsl stack the display controller's DRM node drives no GPU;
         * without this the freedreno device layer builds a half-initialised
         * device that fails at the first pipe query.  KGSL cannot export its
         * native allocations as dma-buf, so make shareable video surfaces use
         * the dma-heap import path before the screen is created.  On a real
         * msm GPU render node an absent /dev/kgsl-3d0 simply falls through to
         * the regular msm path. */
        setenv("FD_FORCE_KGSL", "1", 0);
        setenv("FD_KGSL_ENABLE_DMABUF", "1", 0);
    }

    if (!strcmp(backend, "auto")) {
        /* 1. stock selection (correct on normal GPU render nodes).  The kgsl
         * stack fails here by construction: the display node's kernel name
         * matches no descriptor and zink has no Vulkan device. */
        struct vl_screen *vscreen = vl_drm_screen_create(fd, honor_dri_prime);
        if (vscreen)
            return vscreen;
        /* 2. software fallback.  The KGSL alias is intentionally explicit:
         * applications must opt into GPU submission through /dev/kgsl-3d0
         * with TERMUX_VA_GPU_BACKEND=kgsl. */
        fprintf(stderr, "tva: stock drm screen creation failed, using llvmpipe\n");
        return tva_vscreen_sw();
    }
    if (!strcmp(backend, "kgsl")) {
        /* Probe with the loader override so the device resolves to the
         * fork's kgsl freedreno alias, then restore the env. */
        const char *old = getenv("MESA_LOADER_DRIVER_OVERRIDE");
        char *saved = old && *old ? strdup(old) : NULL;
        setenv("MESA_LOADER_DRIVER_OVERRIDE", "kgsl", 1);
        setenv("FD_FORCE_KGSL", "1", 0);
        setenv("FD_KGSL_ENABLE_DMABUF", "1", 0);
        struct vl_screen *vscreen = vl_drm_screen_create(fd, honor_dri_prime);
        if (saved) {
            setenv("MESA_LOADER_DRIVER_OVERRIDE", saved, 1);
            free(saved);
        } else {
            unsetenv("MESA_LOADER_DRIVER_OVERRIDE");
        }
        if (vscreen)
            return vscreen;
        fprintf(stderr, "tva: kgsl screen creation failed, trying llvmpipe\n");
        return tva_vscreen_sw();
    }
    if (!strcmp(backend, "sw"))
        return tva_vscreen_sw();
    if (!strcmp(backend, "drm"))
        return vl_drm_screen_create(fd, honor_dri_prime);

    fprintf(stderr, "tva: unknown TERMUX_VA_GPU_BACKEND '%s', using auto\n", backend);
    return tva_bridge_vscreen_create(fd, honor_dri_prime);
}
