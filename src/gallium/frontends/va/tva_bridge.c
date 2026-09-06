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
 * Parts of this file are a MODIFIED version of vaapi-driver/src/decode.c,
 * vaapi-driver/src/profiles.c, and vaapi-driver/src/hevc_bitstream.c from
 * the droidspaces-media-decode project (Apache License, Version 2.0): the
 * codec capability table, the pending pipeline-depth model, the
 * is_param_set() unit classification, and the HEVC parameter-set writer were
 * ported from there and relicensed under GPL-3.0.  The Mesa-side wrappers and
 * the fence/pending machinery are new code written for termux-va.
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
#include <va/va.h>
#include <va/va_dec_av1.h>
#ifndef _WIN32
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "drm-uapi/dma-buf.h"
#include "drm-uapi/drm_fourcc.h"
#ifdef __linux__
#include <linux/dma-heap.h>
#endif
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
#include "av1_bitstream.h"

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
    case PIPE_VIDEO_PROFILE_HEVC_MAIN:
    case PIPE_VIDEO_PROFILE_VP9_PROFILE0:
    case PIPE_VIDEO_PROFILE_AV1_MAIN:
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
    case PIPE_VIDEO_FORMAT_AV1:
        return CODEC_AV1;
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
struct tva_av1_frame;

struct tva_pending {
    bool in_use;
    uint32_t unit_seq;            /* access-unit index of the picture */
    bool ready;                   /* staged frame available */
    bool failed;                  /* session error: fence must not hang */
    bool copied;                  /* staging already written into the target */
    bool drop_on_fence_destroy;   /* target was reused before this output */
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

/* AV1's VA-API descriptor does not carry refresh_frame_flags.  Keep a whole
 * temporal unit in hand until the next descriptor arrives: its reference map
 * then reveals which slots contain the just-finished target, allowing us to
 * reconstruct the exact refresh mask before submitting the unit.  The legacy
 * path submits hidden frames and their displayed frame together; the default
 * hidden-output path intentionally splits them so MediaCodec returns every
 * frame needed by show_existing_frame playback. */
#define TVA_AV1_GROUP_MAX_FRAMES 64
/* Keep surface reuse bounded even when MediaCodec is temporarily stalled. */
#define TVA_FENCE_DESTROY_WAIT_MS 200

struct tva_av1_picture {
    struct pipe_video_buffer *target;
    struct tva_pending *pending;
    VADecPictureParameterBufferAV1 picture;
    struct dmd_av1_tile tiles[256];
    uint8_t *tile_data;
    size_t tile_bytes;
    unsigned tile_count;
    uint8_t refresh_frame_flags;
};

struct tva_av1_frame {
    bool valid;
    struct tva_av1_picture pictures[TVA_AV1_GROUP_MAX_FRAMES];
    unsigned picture_count;
    bool include_sequence;
    bool have_show_frame;
    bool hidden_since_show_frame;
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
    bool strict_pending;         /* optional hard in-flight limit */
    unsigned pend_head;           /* oldest entry */
    unsigned pend_count;

    uint64_t next_unit;           /* index to assign to the next VCL unit (1-based) */
    uint64_t frames_done;         /* staged frames counter (diagnostics) */

    /* synthesized CSD cache: re-sent to the daemon only when it changes */
    uint8_t *csd;
    size_t csd_len;
    bool h264_pps_defaults_valid;
    unsigned h264_pps_l0_default;
    unsigned h264_pps_l1_default;
    bool av1_sequence_sent;
    struct tva_av1_frame av1_pending;

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
        if (c->pend_count < DMD_PIPELINE_DEPTH_MAX && !c->strict_pending)
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
    if (fence) {
        fence->codec = c;
        fence->slot = p;
    }
    c->pend_count++;
    return p;
}

/* AV1 show_existing_frame packets do not enter the VA decode callbacks.  A
 * hidden reference picture therefore has no consumer-visible callback even
 * though its surface must still contain decoded pixels before a later
 * show_existing_frame can display it.  The Qualcomm decoder only returns
 * output buffers for show_frame pictures, so mark hidden pictures as shown
 * in the reconstructed stream and pair those extra output buffers with their
 * original VA surfaces.  DMD_AV1_OUTPUT_HIDDEN=0 restores the legacy path. */
static bool
tva_av1_output_hidden(void)
{
    const char *e = getenv("DMD_AV1_OUTPUT_HIDDEN");
    if (!e || !*e)
        return true;
    return !(!strcmp(e, "0") || !strcmp(e, "false") ||
             !strcmp(e, "off"));
}

static bool
tva_av1_synthetic_show_existing(void)
{
    const char *e = getenv("DMD_AV1_SYNTHETIC_SHOW");
    return e && (!strcmp(e, "1") || !strcmp(e, "true") ||
                 !strcmp(e, "on"));
}

static bool
tva_av1_inline_show_existing(void)
{
    const char *e = getenv("DMD_AV1_INLINE_SHOW");
    return e && (!strcmp(e, "1") || !strcmp(e, "true") ||
                 !strcmp(e, "on"));
}

static bool
tva_av1_strict_pending(void)
{
    const char *e = getenv("DMD_AV1_STRICT_PENDING");
    return e && (!strcmp(e, "1") || !strcmp(e, "true") ||
                 !strcmp(e, "on"));
}

static unsigned
tva_av1_fence_destroy_wait_ms(void)
{
    const char *e = getenv("DMD_AV1_FENCE_WAIT_MS");
    if (!e || !*e)
        return TVA_FENCE_DESTROY_WAIT_MS;

    char *end = NULL;
    unsigned long value = strtoul(e, &end, 10);
    if (end == e || *end || value > 5000)
        return TVA_FENCE_DESTROY_WAIT_MS;
    return (unsigned)value;
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
    return true;
}

#if defined(__linux__)
static bool
tva_drm_render_node_present(void)
{
    DIR *dir = opendir("/dev/dri");
    if (!dir)
        return false;

    bool present = false;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!strncmp(entry->d_name, "renderD", 7)) {
            present = true;
            break;
        }
    }
    closedir(dir);
    return present;
}
#endif

/* Chromium's Vulkan importer currently expects all NV12 planes to refer to
 * one dma-buf object.  The normal Gallium video-buffer allocator creates one
 * object per plane, which is valid for VA-API but cannot be consumed by that
 * importer in a PRoot environment.  It can be overridden explicitly, and is
 * enabled automatically for KGSL-only containers without a DRM render node. */
static bool
tva_contiguous_dmabuf_enabled(void)
{
    const char *e = getenv("DMD_VA_CONTIGUOUS_DMABUF");
    if (!e || !*e)
        e = getenv("TERMUX_VA_CONTIGUOUS_DMABUF");
    if (e && *e)
        return !strcmp(e, "1") || !strcmp(e, "true") || !strcmp(e, "on");

#if defined(__linux__)
    /* PRoot exposes KGSL directly but has no DRM render node.  Chromium's
     * native-pixmap importer accepts only one NV12 dma-buf in that setup, so
     * select the shared-object layout automatically when no override is set. */
    const char *backend = getenv("TERMUX_VA_GPU_BACKEND");
    if ((!backend || !*backend || !strcmp(backend, "auto") ||
         !strcmp(backend, "kgsl")) &&
        access("/dev/kgsl-3d0", R_OK) == 0 &&
        !tva_drm_render_node_present())
        return true;
#endif

    return false;
}

#ifndef _WIN32
#if defined(__linux__)
static int
tva_alloc_dmabuf(size_t size)
{
   if (!size)
      return -1;

   int heap_fd = open("/dev/dma_heap/system", O_RDONLY | O_CLOEXEC);
   if (heap_fd < 0)
      return -1;

   struct dma_heap_allocation_data alloc = {
      .len = size,
      .fd_flags = O_RDWR | O_CLOEXEC,
   };
   int ret = ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc);
   int saved_errno = errno;
   close(heap_fd);
   if (ret < 0) {
      errno = saved_errno;
      return -1;
   }

   return (int)alloc.fd;
}

/* Allocate a linear NV12 buffer backed by one dma-buf and import each plane
 * as a separate Gallium resource with an explicit offset.  The resources
 * intentionally have independent KGSL BO wrappers, but their exported FDs
 * still refer to the same dma-buf file description. */
static struct pipe_video_buffer *
tva_create_contiguous_video_buffer(struct pipe_context *context,
                                   const struct pipe_video_buffer *templat)
{
   struct pipe_screen *screen = context ? context->screen : NULL;
   struct pipe_video_buffer bridge_templ;
   struct pipe_resource *resources[VL_NUM_COMPONENTS] = {0};
   enum pipe_format formats[VL_NUM_COMPONENTS] = {0};
   struct pipe_resource res_templ;
   enum pipe_video_chroma_format chroma;
   const unsigned page_size = 4096;
   unsigned y_stride, y_height, uv_height;
   size_t y_size, uv_size, uv_offset, total_size;
   int dmabuf = -1;
   struct pipe_video_buffer *result = NULL;

   if (!screen || !screen->resource_from_handle ||
       !templat || templat->buffer_format != PIPE_FORMAT_NV12 ||
       templat->interlaced)
      return NULL;

   bridge_templ = *templat;
   bridge_templ.bind |= PIPE_BIND_SHARED | PIPE_BIND_LINEAR;
   bridge_templ.width = align(templat->width, VL_MACROBLOCK_WIDTH);
   bridge_templ.height = align(templat->height, VL_MACROBLOCK_HEIGHT);
   chroma = pipe_format_to_chroma_format(bridge_templ.buffer_format);

   vl_get_video_buffer_formats(screen, bridge_templ.buffer_format, formats);
   if (formats[0] == PIPE_FORMAT_NONE || formats[1] == PIPE_FORMAT_NONE)
      return NULL;

   memset(&res_templ, 0, sizeof(res_templ));
   vl_video_buffer_template(&res_templ, &bridge_templ, formats[0], 1, 1,
                            PIPE_USAGE_DEFAULT, 0, chroma);
   y_stride = align(res_templ.width0 * util_format_get_blocksize(res_templ.format),
                    64);
   y_height = res_templ.height0;

   vl_video_buffer_template(&res_templ, &bridge_templ, formats[1], 1, 1,
                            PIPE_USAGE_DEFAULT, 1, chroma);
   uv_height = res_templ.height0;

   if (!y_stride || !y_height || !uv_height ||
       y_height > SIZE_MAX / y_stride ||
       uv_height > SIZE_MAX / y_stride)
      return NULL;

   y_size = (size_t)y_stride * y_height;
   uv_size = (size_t)y_stride * uv_height;
   uv_offset = align(y_size, page_size);
   if (uv_offset < y_size || uv_size > SIZE_MAX - uv_offset)
      return NULL;
   total_size = align(uv_offset + uv_size, page_size);
   if (total_size < uv_offset + uv_size || total_size > UINT32_MAX)
      return NULL;

   dmabuf = tva_alloc_dmabuf(total_size);
   if (dmabuf < 0)
      return NULL;

   for (unsigned plane = 0; plane < 2; plane++) {
      const enum pipe_format format = formats[plane];
      const size_t offset = plane ? uv_offset : 0;
      int import_fd;

      vl_video_buffer_template(&res_templ, &bridge_templ, format, 1, 1,
                               PIPE_USAGE_DEFAULT, plane, chroma);
      res_templ.bind |= PIPE_BIND_SHARED | PIPE_BIND_LINEAR;

      struct winsys_handle whandle = {
         .type = WINSYS_HANDLE_TYPE_FD,
         .plane = plane,
         .size = (uint64_t)total_size,
         .stride = y_stride,
         .offset = offset,
         .format = format,
         .modifier = DRM_FORMAT_MOD_LINEAR,
      };
      import_fd = dup(dmabuf);
      if (import_fd < 0)
         goto fail;
      whandle.handle = import_fd;
      resources[plane] = screen->resource_from_handle(
         screen, &res_templ, &whandle,
         PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE);
      close(import_fd);
      if (!resources[plane])
         goto fail;
   }

   result = vl_video_buffer_create_ex2(context, &bridge_templ, resources);
   if (!result)
      goto fail;
   result->contiguous_planes = true;
   close(dmabuf);
   if (getenv("DMD_VA_LOG"))
      fprintf(stderr, "tva: contiguous NV12 dmabuf=%d size=%zu y=%ux%u "
              "stride=%u uv_offset=%zu\n", dmabuf, total_size,
              bridge_templ.width, bridge_templ.height, y_stride, uv_offset);
   return result;

fail:
   for (unsigned i = 0; i < ARRAY_SIZE(resources); i++)
      pipe_resource_reference(&resources[i], NULL);
   close(dmabuf);
   return NULL;
}
#endif

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

static bool
tva_reader_copy_enabled(void)
{
   const char *e = getenv("DMD_VA_READER_COPY");
   if (!e || !*e)
      return true;
   return !(!strcmp(e, "0") || !strcmp(e, "false") ||
            !strcmp(e, "off"));
}

/* Copy directly into a linear dma-buf without touching pipe_context.  The
 * reader thread can therefore complete a surface as soon as the daemon
 * output arrives, while the application-thread Gallium path remains available
 * as a fallback for drivers that cannot mmap an exported BO. */
static bool
tva_direct_copy_plane(struct pipe_screen *screen, struct pipe_resource *res,
                      const uint8_t *data, unsigned w, unsigned h,
                      unsigned src_stride)
{
   if (!screen || !screen->resource_get_handle || !res || !data || !w || !h)
      return false;

   const unsigned blocksize = util_format_get_blocksize(res->format);
   if (!blocksize || w > UINT_MAX / blocksize)
      return false;
   const size_t row_bytes = (size_t)w * blocksize;
   if (src_stride < row_bytes)
      return false;

   struct winsys_handle whandle;
   memset(&whandle, 0, sizeof(whandle));
   whandle.type = WINSYS_HANDLE_TYPE_FD;
   if (!screen->resource_get_handle(screen, NULL, res, &whandle,
                                    PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE))
      return false;

   const int fd = whandle.handle;
   const size_t dst_stride = whandle.stride;
   const uint64_t object_size = whandle.size;
   if (fd < 0 || dst_stride < row_bytes || !object_size ||
       (uint64_t)(h - 1) > (UINT64_MAX - row_bytes) / dst_stride ||
       (uint64_t)(h - 1) * dst_stride + row_bytes > object_size) {
      if (fd >= 0)
         close(fd);
      return false;
   }

   long page_size = sysconf(_SC_PAGESIZE);
   if (page_size <= 0)
      page_size = 4096;
   const uint64_t page_mask = (uint64_t)page_size - 1;
   const uint64_t map_offset = whandle.offset & ~page_mask;
   const uint64_t delta = whandle.offset - map_offset;
   const uint64_t need = (uint64_t)(h - 1) * dst_stride + row_bytes;
   if (delta > object_size || need > object_size - delta ||
       delta + need > SIZE_MAX) {
      close(fd);
      return false;
   }

   const size_t map_len = (size_t)(delta + need);
   uint8_t *map = mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED,
                       fd, (off_t)map_offset);
   if (map == MAP_FAILED) {
      close(fd);
      return false;
   }

   bool synced = false;
   struct dma_buf_sync sync = {
      .flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE,
   };
   if (ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync) == 0) {
      synced = true;
   } else if (errno != ENOTTY && errno != EOPNOTSUPP && errno != ENOSYS &&
              getenv("DMD_VA_LOG")) {
      fprintf(stderr, "tva: direct dma-buf sync start failed fd=%d errno=%d\n",
              fd, errno);
   }

   uint8_t *dst = map + delta;
   if (dst_stride == src_stride) {
      memcpy(dst, data, row_bytes * h);
   } else {
      for (unsigned y = 0; y < h; y++)
         memcpy(dst + (size_t)y * dst_stride,
                data + (size_t)y * src_stride, row_bytes);
   }

   if (synced) {
      sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
      if (ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync) < 0 && getenv("DMD_VA_LOG"))
         fprintf(stderr, "tva: direct dma-buf sync end failed fd=%d errno=%d\n",
                 fd, errno);
   }
   munmap(map, map_len);
   close(fd);
   return true;
}

static bool
tva_copy_frame_direct(struct tva_codec *c, struct tva_pending *p,
                      const uint8_t *src, size_t src_size)
{
   if (!c || !c->pipe || !p || !src || !p->resources[0] ||
       !p->resources[1] || !p->frame_width || !p->frame_height ||
       p->stride <= 0 || p->slice_height <= 0 || p->crop_left < 0 ||
       p->crop_top < 0 || p->crop_right < p->crop_left ||
       p->crop_bottom < p->crop_top ||
       p->crop_right >= (int)p->frame_width ||
       p->crop_bottom >= (int)p->frame_height ||
       p->crop_right >= p->stride || p->slice_height < (int)p->frame_height)
      return false;

   const unsigned display_w =
      (unsigned)(p->crop_right - p->crop_left + 1);
   const unsigned display_h =
      (unsigned)(p->crop_bottom - p->crop_top + 1);
   const unsigned w = p->resources[0]->width0 < display_w
                      ? p->resources[0]->width0 : display_w;
   const unsigned h = p->resources[0]->height0 < display_h
                      ? p->resources[0]->height0 : display_h;
   if (!w || !h || p->resources[1]->width0 < (w + 1) / 2 ||
       p->resources[1]->height0 < (h + 1) / 2)
      return false;

   const size_t stride = (size_t)p->stride;
   const unsigned uv_w = (w + 1) / 2;
   const unsigned uv_h = (h + 1) / 2;
   const unsigned y_blocksize =
      util_format_get_blocksize(p->resources[0]->format);
   const unsigned uv_blocksize =
      util_format_get_blocksize(p->resources[1]->format);
   if (!y_blocksize || !uv_blocksize || w > UINT_MAX / y_blocksize ||
       uv_w > UINT_MAX / uv_blocksize || stride < (size_t)w * y_blocksize ||
       stride < (size_t)uv_w * uv_blocksize)
      return false;

   if ((size_t)p->crop_top > SIZE_MAX / stride ||
       (size_t)p->crop_top * stride > SIZE_MAX - (size_t)p->crop_left ||
       (size_t)p->slice_height > SIZE_MAX / stride ||
       (size_t)(p->crop_top / 2) > SIZE_MAX / stride)
      return false;
   const size_t y_offset = (size_t)p->crop_top * stride +
                           (size_t)p->crop_left;
   size_t uv_offset = (size_t)p->slice_height * stride;
   if (uv_offset > SIZE_MAX - (size_t)(p->crop_top / 2) * stride)
      return false;
   uv_offset += (size_t)(p->crop_top / 2) * stride;
   if (uv_offset > SIZE_MAX - (size_t)(p->crop_left & ~1))
      return false;
   uv_offset += (size_t)(p->crop_left & ~1);

   const size_t y_rows = h - 1;
   const size_t uv_rows = uv_h - 1;
   const size_t y_row_bytes = (size_t)w * y_blocksize;
   const size_t uv_row_bytes = (size_t)uv_w * uv_blocksize;
   if (y_rows > SIZE_MAX / stride ||
       y_offset > SIZE_MAX - y_rows * stride ||
       y_offset + y_rows * stride > SIZE_MAX - y_row_bytes ||
       uv_rows > SIZE_MAX / stride ||
       uv_offset > SIZE_MAX - uv_rows * stride ||
       uv_offset + uv_rows * stride > SIZE_MAX - uv_row_bytes)
      return false;
   const size_t y_end = y_offset + y_rows * stride + y_row_bytes;
   const size_t uv_end = uv_offset + uv_rows * stride + uv_row_bytes;
   if (y_end > src_size || uv_end > src_size)
      return false;

   struct pipe_screen *screen = c->pipe->screen;
   if (!tva_direct_copy_plane(screen, p->resources[0], src + y_offset,
                               w, h, (unsigned)p->stride))
      return false;
   if (!tva_direct_copy_plane(screen, p->resources[1], src + uv_offset,
                              uv_w, uv_h, (unsigned)p->stride))
      return false;
   return true;
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
        /* Bridge surfaces are linear and normally preserve the daemon's
         * pitch.  Collapse the row loop to one transfer in that common case;
         * AV1 at 1080p otherwise performs more than 1,600 tiny memcpys per
         * frame (and quickly becomes CPU-bound at 60 fps). */
        if (transfer->stride == stride) {
            memcpy(dst, data, (size_t)row_bytes * h);
        } else {
            for (unsigned y = 0; y < h; y++)
                memcpy(dst + (size_t)y * transfer->stride,
                       data + (size_t)y * stride, row_bytes);
        }
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
    const uint64_t copy_start_ns = os_time_get_nano();
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

    /* CPU mappings write the exported allocation synchronously; there is no
     * Gallium batch to submit in that path.  Keep the information here so the
     * caller can avoid flushing unrelated GPU work after every decoded frame.
     * The GPU-upload path still needs an explicit flush before its dma-buf is
     * handed to the consumer. */
    const bool cpu_copy = tva_cpu_copy_enabled() && pipe->texture_map &&
                          pipe->texture_unmap;

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
    if (!cpu_copy && !tva_flush_copy(pipe))
        return false;
    tva_probe_resource(pipe, p->resources[0], w);
    tva_probe_resource(pipe, p->resources[1], uv_w * 2);
    TVA_TRACE("copy frame complete unit=%u duration=%.3f ms",
              p->unit_seq,
              (double)(os_time_get_nano() - copy_start_ns) / 1000000.0);
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
        p->frame_width = f.width;
        p->frame_height = f.height;
        p->stride = f.stride;
        p->slice_height = f.slice_height;
        p->crop_left = f.crop_left;
        p->crop_top = f.crop_top;
        p->crop_right = f.crop_right;
        p->crop_bottom = f.crop_bottom;

#ifndef _WIN32
        /* A direct dma-buf copy closes the surface-reuse window for AV1
         * hidden references.  It is enabled by default for the validated
         * linear resources; DMD_VA_READER_COPY=0 opts out.  A failed direct
         * map falls back to the staged application-thread copy below. */
        if (tva_reader_copy_enabled() && tva_cpu_copy_enabled() &&
            tva_copy_frame_direct(c, p, f.data, f.size)) {
            p->copied = true;
            p->ready = true;
            c->frames_done++;
            TVA_TRACE("reader direct copy unit=%u result=1", p->unit_seq);
            u_cnd_monotonic_broadcast(&c->pend_cond);
            mtx_unlock(&c->pend_mutex);
            tva_session_release_frame(c->sess, &f);
            continue;
        }
#endif

        p->staging = malloc(f.size ? f.size : 1);
        if (!p->staging) {
            tva_mark_broken_locked(c);
            mtx_unlock(&c->pend_mutex);
            tva_session_release_frame(c->sess, &f);
            break;
        }
        memcpy(p->staging, f.data, f.size);
        p->staging_size = f.size;
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
 * FFmpeg's VAAPI H.264 path does not deliver SPS/PPS as slice-data buffers;
 * the VA frontend exposes only the parsed pipe_h264_sps/pps structures.  The
 * daemon's MediaCodec needs the parameter sets as CSD, so the bridge
 * regenerates them from those parsed structures.  (Same role as upstream
 * vaapi-driver's h264_bitstream.c, ported to the gallium-side data model.)
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

static bool
tva_h264_high_profile(enum pipe_video_profile profile)
{
    return profile == PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH ||
           profile == PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10 ||
           profile == PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH422 ||
           profile == PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH444;
}

/*
 * Build the SPS NALU (with NAL header + emulation prevention) from the
 * frontend-parsed struct.  Returns the RBSP size; the NAL header byte is
 * written first (nal_ref_idc=3, type=7).
 */
static size_t
tva_build_h264_sps(enum pipe_video_profile profile,
                   const struct pipe_h264_sps *sps, unsigned max_refs,
                   unsigned visible_width, unsigned visible_height,
                   uint8_t **out)
{
    struct tva_bw w = {0};

    /* VA-API carries the H.264 profile in the decode context rather than in
     * VAPictureParameterBufferH264.  Preserve that profile in the synthetic
     * SPS; inferring High solely from chroma/depth mislabels ordinary 8-bit
     * 4:2:0 High streams as Main. */
    uint8_t profile_idc;
    switch (profile) {
    case PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE:
    case PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE:
        profile_idc = 66;
        break;
    case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
        profile_idc = 77;
        break;
    case PIPE_VIDEO_PROFILE_MPEG4_AVC_EXTENDED:
        profile_idc = 88;
        break;
    case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
        profile_idc = 100;
        break;
    case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10:
        profile_idc = 110;
        break;
    case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH422:
        profile_idc = 122;
        break;
    case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH444:
        profile_idc = 244;
        break;
    default:
        /* Keep a conservative fallback for callers that pass an unknown
         * profile while still providing high-profile SPS fields when the
         * parsed descriptor requires them. */
        profile_idc = (sps->bit_depth_luma_minus8 ||
                       sps->bit_depth_chroma_minus8 ||
                       sps->chroma_format_idc > 1) ? 100 : 77;
        break;
    }

    tva_bw_put(&w, 8, 0x67);             /* nal_ref_idc=3, type=7 */
    tva_bw_put(&w, 8, profile_idc);
    tva_bw_put(&w, 8, 0);                /* constraint flags + reserved */
    tva_bw_put(&w, 8, sps->level_idc ? sps->level_idc : 40);
    tva_bw_ue(&w, 0);                    /* seq_parameter_set_id */
    if (profile_idc == 100 || profile_idc == 110 ||
        profile_idc == 122 || profile_idc == 244) {
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
tva_build_h264_pps(enum pipe_video_profile profile,
                   const struct pipe_h264_pps *pps,
                   unsigned default_l0, unsigned default_l1,
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
     * caller supplies defaults inferred from the slice parameters (or the
     * decoder DPB size when slice parameters are unavailable). */
    bool high_profile = tva_h264_high_profile(profile);
    tva_bw_ue(&w, pps->num_ref_idx_l0_default_active_minus1 ?
             pps->num_ref_idx_l0_default_active_minus1 : default_l0);
    tva_bw_ue(&w, pps->num_ref_idx_l1_default_active_minus1 ?
             pps->num_ref_idx_l1_default_active_minus1 : default_l1);
    tva_bw_put(&w, 1, pps->weighted_pred_flag);
    tva_bw_put(&w, 2, pps->weighted_bipred_idc);
    tva_bw_se(&w, pps->pic_init_qp_minus26);
    tva_bw_se(&w, pps->pic_init_qs_minus26);
    tva_bw_se(&w, pps->chroma_qp_index_offset);
    tva_bw_put(&w, 1, pps->deblocking_filter_control_present_flag);
    tva_bw_put(&w, 1, pps->constrained_intra_pred_flag);
    tva_bw_put(&w, 1, pps->redundant_pic_cnt_present_flag);
    if (high_profile) {
        tva_bw_put(&w, 1, pps->transform_8x8_mode_flag);
        tva_bw_put(&w, 1, 0);            /* pic_scaling_matrix_present */
        tva_bw_se(&w, pps->second_chroma_qp_index_offset);
    }
    tva_bw_rbsp_trailing(&w);
    *out = w.buf;
    return w.len;
}

/* ---------------------------------- HEVC CSD synthesis (VPS/SPS/PPS) */
/*
 * VA-API gives the frontend parsed HEVC picture parameters, but does not
 * carry the original VPS/SPS/PPS byte stream.  MediaCodec needs those
 * parameter sets before the first VCL unit, so reconstruct the minimal
 * single-layer Annex B headers from the pipe_h265 descriptors.  Slice data
 * itself is forwarded unchanged: VA's slice-data offsets guarantee that the
 * slice header is still present in the buffer.
 *
 * A few syntax elements are not represented by the VA decode descriptor.  In
 * particular, the contents of SPS short-term reference-picture sets cannot be
 * reconstructed from the VA descriptor.  The Qualcomm decoder still requires
 * the declared RPS count to be non-zero, even when every slice carries its own
 * inline RPS, so the bridge emits empty placeholder sets and rejects a slice
 * that instead references an SPS RPS.
 */

static unsigned
tva_h265_profile_idc(enum pipe_video_profile profile)
{
    return profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_10 ? 2 : 1;
}

/* Return a conservative level_idc based on the luma-picture size.  VA-API's
 * HEVC picture descriptor does not expose the stream's original level. */
static unsigned
tva_h265_level_idc(unsigned width, unsigned height)
{
    uint64_t luma = (uint64_t)width * height;
    if (luma <=   36864) return 30;  /* 1.0 */
    if (luma <=  122880) return 60;  /* 2.0 */
    if (luma <=  245760) return 63;  /* 2.1 */
    if (luma <=  552960) return 90;  /* 3.0 */
    if (luma <=  983040) return 93;  /* 3.1 */
    if (luma <= 2228224) return 120; /* 4.0 */
    if (luma <= 8912896) return 150; /* 5.0 */
    return 180;                      /* 6.0 */
}

static void
tva_bw_hevc_header(struct tva_bw *w, unsigned nal_type)
{
    /* forbidden_zero_bit=0, nal_unit_type, nuh_layer_id=0,
     * nuh_temporal_id_plus1=1. */
    tva_bw_put(w, 8, (nal_type & 0x3f) << 1);
    tva_bw_put(w, 8, 1);
}

static void
tva_h265_put_ptl(struct tva_bw *w, enum pipe_video_profile profile,
                 unsigned width, unsigned height)
{
    unsigned profile_idc = tva_h265_profile_idc(profile);

    tva_bw_put(w, 2, 0);                 /* general_profile_space */
    tva_bw_put(w, 1, 0);                 /* general_tier_flag */
    tva_bw_put(w, 5, profile_idc);       /* general_profile_idc */
    for (unsigned i = 0; i < 32; i++)
        tva_bw_put(w, 1, i == profile_idc);
    tva_bw_put(w, 1, 1);                 /* progressive_source */
    tva_bw_put(w, 1, 0);                 /* interlaced_source */
    tva_bw_put(w, 1, 0);                 /* non_packed_constraint */
    tva_bw_put(w, 1, 1);                 /* frame_only_constraint */
    tva_bw_put(w, 22, 0);                /* reserved_zero_43bits (part 1) */
    tva_bw_put(w, 21, 0);                /* reserved_zero_43bits (part 2) */
    tva_bw_put(w, 1, 0);                 /* inbld/reserved_zero_bit */
    tva_bw_put(w, 8, tva_h265_level_idc(width, height));
}

static size_t
tva_build_h265_vps(const struct pipe_h265_sps *sps,
                   enum pipe_video_profile profile, uint8_t **out)
{
    struct tva_bw w = {0};

    tva_bw_hevc_header(&w, 32);
    tva_bw_put(&w, 4, 0);                /* vps_video_parameter_set_id */
    tva_bw_put(&w, 2, 3);                /* base_layer_internal/available */
    tva_bw_put(&w, 6, 0);                /* vps_max_layers_minus1 */
    tva_bw_put(&w, 3, 0);                /* vps_max_sub_layers_minus1 */
    tva_bw_put(&w, 1, 1);                /* vps_temporal_id_nesting_flag */
    tva_bw_put(&w, 16, 0xffff);          /* vps_reserved_0xffff_16bits */
    tva_h265_put_ptl(&w, profile, sps->pic_width_in_luma_samples,
                     sps->pic_height_in_luma_samples);
    tva_bw_put(&w, 1, 0);                /* sub_layer_ordering_info_present */
    tva_bw_ue(&w, sps->sps_max_dec_pic_buffering_minus1);
    tva_bw_ue(&w, 0);                    /* vps_max_num_reorder_pics */
    tva_bw_ue(&w, 0);                    /* vps_max_latency_increase_plus1 */
    tva_bw_put(&w, 6, 0);                /* vps_max_layer_id */
    tva_bw_ue(&w, 0);                    /* vps_num_layer_sets_minus1 */
    tva_bw_put(&w, 1, 0);                /* vps_timing_info_present_flag */
    tva_bw_put(&w, 1, 0);                /* vps_extension_flag */
    tva_bw_rbsp_trailing(&w);
    *out = w.buf;
    return w.len;
}

static size_t
tva_build_h265_sps(const struct pipe_h265_sps *sps,
                   enum pipe_video_profile profile, uint8_t **out)
{
    struct tva_bw w = {0};

    tva_bw_hevc_header(&w, 33);
    tva_bw_put(&w, 4, 0);                /* sps_video_parameter_set_id */
    tva_bw_put(&w, 3, 0);                /* sps_max_sub_layers_minus1 */
    tva_bw_put(&w, 1, 1);                /* sps_temporal_id_nesting_flag */
    tva_h265_put_ptl(&w, profile, sps->pic_width_in_luma_samples,
                     sps->pic_height_in_luma_samples);
    tva_bw_ue(&w, 0);                    /* sps_seq_parameter_set_id */
    tva_bw_ue(&w, sps->chroma_format_idc);
    if (sps->chroma_format_idc == 3)
        tva_bw_put(&w, 1, sps->separate_colour_plane_flag);
    tva_bw_ue(&w, sps->pic_width_in_luma_samples);
    tva_bw_ue(&w, sps->pic_height_in_luma_samples);
    tva_bw_put(&w, 1, 0);                /* conformance_window_flag */
    tva_bw_ue(&w, sps->bit_depth_luma_minus8);
    tva_bw_ue(&w, sps->bit_depth_chroma_minus8);
    tva_bw_ue(&w, sps->log2_max_pic_order_cnt_lsb_minus4);
    tva_bw_put(&w, 1, 0);                /* sub_layer_ordering_info_present */
    tva_bw_ue(&w, sps->sps_max_dec_pic_buffering_minus1);
    tva_bw_ue(&w, 0);                    /* sps_max_num_reorder_pics */
    tva_bw_ue(&w, 0);                    /* sps_max_latency_increase_plus1 */
    tva_bw_ue(&w, sps->log2_min_luma_coding_block_size_minus3);
    tva_bw_ue(&w, sps->log2_diff_max_min_luma_coding_block_size);
    tva_bw_ue(&w, sps->log2_min_transform_block_size_minus2);
    tva_bw_ue(&w, sps->log2_diff_max_min_transform_block_size);
    tva_bw_ue(&w, sps->max_transform_hierarchy_depth_inter);
    tva_bw_ue(&w, sps->max_transform_hierarchy_depth_intra);
    tva_bw_put(&w, 1, sps->scaling_list_enabled_flag);
    if (sps->scaling_list_enabled_flag)
        tva_bw_put(&w, 1, 0);            /* use default scaling lists */
    tva_bw_put(&w, 1, sps->amp_enabled_flag);
    tva_bw_put(&w, 1, sps->sample_adaptive_offset_enabled_flag);
    tva_bw_put(&w, 1, sps->pcm_enabled_flag);
    if (sps->pcm_enabled_flag) {
        tva_bw_put(&w, 4, sps->pcm_sample_bit_depth_luma_minus1);
        tva_bw_put(&w, 4, sps->pcm_sample_bit_depth_chroma_minus1);
        tva_bw_ue(&w, sps->log2_min_pcm_luma_coding_block_size_minus3);
        tva_bw_ue(&w, sps->log2_diff_max_min_luma_coding_block_size);
        tva_bw_put(&w, 1, sps->pcm_loop_filter_disabled_flag);
    }
    /* Keep the count from VA-API.  Its descriptor does not carry the RPS
     * contents, so emit empty non-predicted sets as placeholders.  The tested
     * Qualcomm decoder requires a non-zero count even for inline slice RPS.
     * tva_h265_can_build() rejects streams whose slices reference these
     * placeholders through short_term_ref_pic_set_sps_flag. */
    tva_bw_ue(&w, sps->num_short_term_ref_pic_sets);
    for (unsigned i = 0; i < sps->num_short_term_ref_pic_sets; i++) {
        if (i)
            tva_bw_put(&w, 1, 0);         /* inter_ref_pic_set_prediction */
        tva_bw_ue(&w, 0);                /* num_negative_pics */
        tva_bw_ue(&w, 0);                /* num_positive_pics */
    }
    tva_bw_put(&w, 1, sps->long_term_ref_pics_present_flag);
    if (sps->long_term_ref_pics_present_flag)
        tva_bw_ue(&w, sps->num_long_term_ref_pics_sps);
    tva_bw_put(&w, 1, sps->sps_temporal_mvp_enabled_flag);
    tva_bw_put(&w, 1, sps->strong_intra_smoothing_enabled_flag);
    tva_bw_put(&w, 1, 0);                /* vui_parameters_present_flag */
    tva_bw_put(&w, 1, 0);                /* sps_extension_present_flag */
    tva_bw_rbsp_trailing(&w);
    *out = w.buf;
    return w.len;
}

static size_t
tva_build_h265_pps(const struct pipe_h265_pps *pps, uint8_t **out)
{
    struct tva_bw w = {0};

    if (pps->tiles_enabled_flag &&
        (pps->num_tile_columns_minus1 >= ARRAY_SIZE(pps->column_width_minus1) ||
         pps->num_tile_rows_minus1 >= ARRAY_SIZE(pps->row_height_minus1)))
        return 0;

    tva_bw_hevc_header(&w, 34);
    tva_bw_ue(&w, 0);                    /* pps_pic_parameter_set_id */
    tva_bw_ue(&w, 0);                    /* pps_seq_parameter_set_id */
    tva_bw_put(&w, 1, pps->dependent_slice_segments_enabled_flag);
    tva_bw_put(&w, 1, pps->output_flag_present_flag);
    tva_bw_put(&w, 3, pps->num_extra_slice_header_bits);
    tva_bw_put(&w, 1, pps->sign_data_hiding_enabled_flag);
    tva_bw_put(&w, 1, pps->cabac_init_present_flag);
    tva_bw_ue(&w, pps->num_ref_idx_l0_default_active_minus1);
    tva_bw_ue(&w, pps->num_ref_idx_l1_default_active_minus1);
    tva_bw_se(&w, pps->init_qp_minus26);
    tva_bw_put(&w, 1, pps->constrained_intra_pred_flag);
    tva_bw_put(&w, 1, pps->transform_skip_enabled_flag);
    tva_bw_put(&w, 1, pps->cu_qp_delta_enabled_flag);
    if (pps->cu_qp_delta_enabled_flag)
        tva_bw_ue(&w, pps->diff_cu_qp_delta_depth);
    tva_bw_se(&w, pps->pps_cb_qp_offset);
    tva_bw_se(&w, pps->pps_cr_qp_offset);
    tva_bw_put(&w, 1, pps->pps_slice_chroma_qp_offsets_present_flag);
    tva_bw_put(&w, 1, pps->weighted_pred_flag);
    tva_bw_put(&w, 1, pps->weighted_bipred_flag);
    tva_bw_put(&w, 1, pps->transquant_bypass_enabled_flag);
    tva_bw_put(&w, 1, pps->tiles_enabled_flag);
    tva_bw_put(&w, 1, pps->entropy_coding_sync_enabled_flag);
    if (pps->tiles_enabled_flag) {
        tva_bw_ue(&w, pps->num_tile_columns_minus1);
        tva_bw_ue(&w, pps->num_tile_rows_minus1);
        /* The frontend does not preserve uniform_spacing_flag.  Explicit
         * widths/heights are available, so use the non-uniform form. */
        tva_bw_put(&w, 1, 0);
        for (unsigned i = 0; i < pps->num_tile_columns_minus1; i++)
            tva_bw_ue(&w, pps->column_width_minus1[i]);
        for (unsigned i = 0; i < pps->num_tile_rows_minus1; i++)
            tva_bw_ue(&w, pps->row_height_minus1[i]);
        tva_bw_put(&w, 1, pps->loop_filter_across_tiles_enabled_flag);
    }
    tva_bw_put(&w, 1, pps->pps_loop_filter_across_slices_enabled_flag);

    /* VA-API does not expose deblocking_filter_control_present_flag itself;
     * infer it from the controls which follow it in the bitstream. */
    bool dbf_ctrl = pps->deblocking_filter_override_enabled_flag ||
                    pps->pps_deblocking_filter_disabled_flag ||
                    pps->pps_beta_offset_div2 || pps->pps_tc_offset_div2;
    tva_bw_put(&w, 1, dbf_ctrl);
    if (dbf_ctrl) {
        tva_bw_put(&w, 1, pps->deblocking_filter_override_enabled_flag);
        tva_bw_put(&w, 1, pps->pps_deblocking_filter_disabled_flag);
        if (!pps->pps_deblocking_filter_disabled_flag) {
            tva_bw_se(&w, pps->pps_beta_offset_div2);
            tva_bw_se(&w, pps->pps_tc_offset_div2);
        }
    }
    tva_bw_put(&w, 1, 0);                /* pps_scaling_list_data_present */
    tva_bw_put(&w, 1, pps->lists_modification_present_flag);
    tva_bw_ue(&w, pps->log2_parallel_merge_level_minus2);
    tva_bw_put(&w, 1, pps->slice_segment_header_extension_present_flag);
    tva_bw_put(&w, 1, 0);                /* pps_extension_present_flag */
    tva_bw_rbsp_trailing(&w);
    *out = w.buf;
    return w.len;
}

static bool
tva_h265_can_build(const struct pipe_h265_sps *sps,
                   const struct pipe_h265_picture_desc *pic)
{
    if (!sps || !pic)
        return false;
    /* VA-API exposes only the number of short-term RPS entries, not their
     * contents.  The slice header bit count is non-zero when the current
     * picture carries an inline RPS (st_rps_bits in VAPictureParameterBuffer
     * HEVC).  An IDR picture has no RPS syntax, so it is also safe to seed the
     * synthetic SPS there.  If a later picture references an SPS RPS, the
     * synthetic SPS cannot represent it and must be rejected. */
    if (sps->num_short_term_ref_pic_sets > 0 &&
        !pic->IDRPicFlag && pic->NumShortTermPictureSliceHeaderBits == 0)
        return false;
    /* Long-term SPS entries have the same limitation. */
    if (sps->long_term_ref_pics_present_flag &&
        sps->num_long_term_ref_pics_sps > 0)
        return false;
    return true;
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
 * The Gallium AV1 descriptor is a compact, driver-facing representation of
 * the VA-API picture parameters.  AV1 OBU reconstruction uses the public VA
 * structure because its field names and semantics are defined by libva, so
 * copy the fields needed by the bitstream writer here.  Fields that the
 * Gallium frontend intentionally does not retain (color range and still
 * picture) use the safe defaults for ordinary 8-bit 4:2:0 video.
 */
static bool
tva_av1_to_va_picture(const struct pipe_av1_picture_desc *src,
                      VADecPictureParameterBufferAV1 *dst)
{
    if (!src || !dst)
        return false;

    const __typeof__(src->picture_parameter) *p = &src->picture_parameter;
    memset(dst, 0, sizeof(*dst));

    dst->profile = p->profile;
    dst->order_hint_bits_minus_1 = p->order_hint_bits_minus_1;
    dst->bit_depth_idx = p->bit_depth_idx;
    dst->matrix_coefficients = p->matrix_coefficients;
    dst->seq_info_fields.fields.still_picture = 0;
    dst->seq_info_fields.fields.use_128x128_superblock =
        p->seq_info_fields.use_128x128_superblock;
    dst->seq_info_fields.fields.enable_filter_intra =
        p->seq_info_fields.enable_filter_intra;
    dst->seq_info_fields.fields.enable_intra_edge_filter =
        p->seq_info_fields.enable_intra_edge_filter;
    dst->seq_info_fields.fields.enable_interintra_compound =
        p->seq_info_fields.enable_interintra_compound;
    dst->seq_info_fields.fields.enable_masked_compound =
        p->seq_info_fields.enable_masked_compound;
    dst->seq_info_fields.fields.enable_dual_filter =
        p->seq_info_fields.enable_dual_filter;
    dst->seq_info_fields.fields.enable_order_hint =
        p->seq_info_fields.enable_order_hint;
    dst->seq_info_fields.fields.enable_jnt_comp =
        p->seq_info_fields.enable_jnt_comp;
    dst->seq_info_fields.fields.enable_cdef = p->seq_info_fields.enable_cdef;
    dst->seq_info_fields.fields.mono_chrome = p->seq_info_fields.mono_chrome;
    dst->seq_info_fields.fields.color_range = 0;
    dst->seq_info_fields.fields.subsampling_x =
        p->seq_info_fields.subsampling_x;
    dst->seq_info_fields.fields.subsampling_y =
        p->seq_info_fields.subsampling_y;
    dst->seq_info_fields.fields.film_grain_params_present =
        p->seq_info_fields.film_grain_params_present;

    if (p->frame_width == 0 || p->frame_height == 0)
        return false;
    dst->frame_width_minus1 = p->frame_width - 1;
    dst->frame_height_minus1 = p->frame_height - 1;
    memcpy(dst->ref_frame_idx, p->ref_frame_idx,
           sizeof(dst->ref_frame_idx));
    dst->primary_ref_frame = p->primary_ref_frame;
    dst->order_hint = p->order_hint;

    dst->seg_info.segment_info_fields.bits.enabled =
        p->seg_info.segment_info_fields.enabled;
    dst->seg_info.segment_info_fields.bits.update_map =
        p->seg_info.segment_info_fields.update_map;
    dst->seg_info.segment_info_fields.bits.update_data =
        p->seg_info.segment_info_fields.update_data;
    dst->seg_info.segment_info_fields.bits.temporal_update =
        p->seg_info.segment_info_fields.temporal_update;
    memcpy(dst->seg_info.feature_data, p->seg_info.feature_data,
           sizeof(dst->seg_info.feature_data));
    memcpy(dst->seg_info.feature_mask, p->seg_info.feature_mask,
           sizeof(dst->seg_info.feature_mask));

    dst->tile_cols = p->tile_cols;
    dst->tile_rows = p->tile_rows;
    if (!dst->tile_cols || !dst->tile_rows || dst->tile_cols > 64 ||
        dst->tile_rows > 64 ||
        (uint32_t)dst->tile_cols * dst->tile_rows > 256)
        return false;
    for (unsigned i = 0; i < dst->tile_cols && i < 63; i++) {
        if (!p->width_in_sbs[i])
            return false;
        dst->width_in_sbs_minus_1[i] = p->width_in_sbs[i] - 1;
    }
    for (unsigned i = 0; i < dst->tile_rows && i < 63; i++) {
        if (!p->height_in_sbs[i])
            return false;
        dst->height_in_sbs_minus_1[i] = p->height_in_sbs[i] - 1;
    }
    dst->context_update_tile_id = p->context_update_tile_id;

    dst->pic_info_fields.bits.frame_type = p->pic_info_fields.frame_type;
    dst->pic_info_fields.bits.show_frame = p->pic_info_fields.show_frame;
    dst->pic_info_fields.bits.showable_frame = p->pic_info_fields.showable_frame;
    dst->pic_info_fields.bits.error_resilient_mode =
        p->pic_info_fields.error_resilient_mode;
    dst->pic_info_fields.bits.disable_cdf_update =
        p->pic_info_fields.disable_cdf_update;
    dst->pic_info_fields.bits.allow_screen_content_tools =
        p->pic_info_fields.allow_screen_content_tools;
    dst->pic_info_fields.bits.force_integer_mv =
        p->pic_info_fields.force_integer_mv;
    dst->pic_info_fields.bits.allow_intrabc = p->pic_info_fields.allow_intrabc;
    dst->pic_info_fields.bits.use_superres = p->pic_info_fields.use_superres;
    dst->pic_info_fields.bits.allow_high_precision_mv =
        p->pic_info_fields.allow_high_precision_mv;
    dst->pic_info_fields.bits.is_motion_mode_switchable =
        p->pic_info_fields.is_motion_mode_switchable;
    dst->pic_info_fields.bits.use_ref_frame_mvs =
        p->pic_info_fields.use_ref_frame_mvs;
    dst->pic_info_fields.bits.disable_frame_end_update_cdf =
        p->pic_info_fields.disable_frame_end_update_cdf;
    dst->pic_info_fields.bits.uniform_tile_spacing_flag =
        p->pic_info_fields.uniform_tile_spacing_flag;
    dst->pic_info_fields.bits.allow_warped_motion =
        p->pic_info_fields.allow_warped_motion;
    dst->pic_info_fields.bits.large_scale_tile = p->pic_info_fields.large_scale_tile;

    dst->superres_scale_denominator = p->superres_scale_denominator;
    dst->interp_filter = p->interp_filter;
    memcpy(dst->filter_level, p->filter_level, sizeof(dst->filter_level));
    dst->filter_level_u = p->filter_level_u;
    dst->filter_level_v = p->filter_level_v;
    dst->loop_filter_info_fields.bits.sharpness_level =
        p->loop_filter_info_fields.sharpness_level;
    dst->loop_filter_info_fields.bits.mode_ref_delta_enabled =
        p->loop_filter_info_fields.mode_ref_delta_enabled;
    dst->loop_filter_info_fields.bits.mode_ref_delta_update =
        p->loop_filter_info_fields.mode_ref_delta_update;
    memcpy(dst->ref_deltas, p->ref_deltas, sizeof(dst->ref_deltas));
    memcpy(dst->mode_deltas, p->mode_deltas, sizeof(dst->mode_deltas));

    dst->base_qindex = p->base_qindex;
    dst->y_dc_delta_q = p->y_dc_delta_q;
    dst->u_dc_delta_q = p->u_dc_delta_q;
    dst->u_ac_delta_q = p->u_ac_delta_q;
    dst->v_dc_delta_q = p->v_dc_delta_q;
    dst->v_ac_delta_q = p->v_ac_delta_q;
    dst->qmatrix_fields.bits.using_qmatrix = p->qmatrix_fields.using_qmatrix;
    dst->qmatrix_fields.bits.qm_y = p->qmatrix_fields.qm_y;
    dst->qmatrix_fields.bits.qm_u = p->qmatrix_fields.qm_u;
    dst->qmatrix_fields.bits.qm_v = p->qmatrix_fields.qm_v;

    dst->mode_control_fields.bits.delta_q_present_flag =
        p->mode_control_fields.delta_q_present_flag;
    dst->mode_control_fields.bits.log2_delta_q_res =
        p->mode_control_fields.log2_delta_q_res;
    dst->mode_control_fields.bits.delta_lf_present_flag =
        p->mode_control_fields.delta_lf_present_flag;
    dst->mode_control_fields.bits.log2_delta_lf_res =
        p->mode_control_fields.log2_delta_lf_res;
    dst->mode_control_fields.bits.delta_lf_multi = p->mode_control_fields.delta_lf_multi;
    dst->mode_control_fields.bits.tx_mode = p->mode_control_fields.tx_mode;
    dst->mode_control_fields.bits.reference_select =
        p->mode_control_fields.reference_select;
    dst->mode_control_fields.bits.reduced_tx_set_used =
        p->mode_control_fields.reduced_tx_set_used;
    dst->mode_control_fields.bits.skip_mode_present =
        p->mode_control_fields.skip_mode_present;

    dst->cdef_damping_minus_3 = p->cdef_damping_minus_3;
    dst->cdef_bits = p->cdef_bits;
    memcpy(dst->cdef_y_strengths, p->cdef_y_strengths,
           sizeof(dst->cdef_y_strengths));
    memcpy(dst->cdef_uv_strengths, p->cdef_uv_strengths,
           sizeof(dst->cdef_uv_strengths));
    /* FFmpeg's VA-API backend remaps AV1 restoration values for the VA
     * interface: {NONE, SWITCHABLE, WIENER, SGRPROJ}.  The bitstream syntax
     * uses {NONE, WIENER, SGRPROJ, SWITCHABLE}, so undo that remap here. */
    static const uint8_t restore_remap[4] = { 0, 2, 3, 1 };
    if (p->loop_restoration_fields.yframe_restoration_type > 3 ||
        p->loop_restoration_fields.cbframe_restoration_type > 3 ||
        p->loop_restoration_fields.crframe_restoration_type > 3)
        return false;
    dst->loop_restoration_fields.bits.yframe_restoration_type =
        restore_remap[p->loop_restoration_fields.yframe_restoration_type];
    dst->loop_restoration_fields.bits.cbframe_restoration_type =
        restore_remap[p->loop_restoration_fields.cbframe_restoration_type];
    dst->loop_restoration_fields.bits.crframe_restoration_type =
        restore_remap[p->loop_restoration_fields.crframe_restoration_type];
    dst->loop_restoration_fields.bits.lr_unit_shift =
        p->loop_restoration_fields.lr_unit_shift;
    dst->loop_restoration_fields.bits.lr_uv_shift =
        p->loop_restoration_fields.lr_uv_shift;

    return true;
}

static void
tva_av1_pending_clear(struct tva_av1_frame *frame)
{
    for (unsigned i = 0; i < frame->picture_count; i++)
        free(frame->pictures[i].tile_data);
    memset(frame, 0, sizeof(*frame));
}

/* The next VA picture exposes the reference-frame map after the pending
 * picture.  A target appearing in map slot i means that the pending picture
 * refreshed slot i; multiple matches are preserved for key-like updates. */
static uint8_t
tva_av1_refresh_mask(const struct pipe_av1_picture_desc *next,
                     const struct pipe_video_buffer *target)
{
    if (!next || !target)
        return 0;

    uint8_t mask = 0;
    for (unsigned i = 0; i < 8; i++) {
        if (next->ref[i] == target)
            mask |= (uint8_t)(1u << i);
    }
    return mask;
}

static int
tva_av1_send_pending(struct tva_codec *c)
{
    struct tva_av1_frame *frame = &c->av1_pending;
    if (!frame->valid)
        return 0;

    /* Submit one temporal unit per decoded picture, appending a
     * show_existing_frame OBU immediately after hidden pictures.  Keeping
     * the synthetic presentation next to the frame which refreshes the
     * reference slot prevents a later temporal unit from changing the map
     * before the Qualcomm decoder has emitted the hidden surface. */
    if (tva_av1_inline_show_existing()) {
        if (frame->picture_count > UINT32_MAX - c->next_unit) {
            debug_printf("tva: AV1 picture sequence overflow\n");
            tva_mark_broken(c);
            tva_av1_pending_clear(frame);
            return -1;
        }

        mtx_lock(&c->pend_mutex);
        for (unsigned i = 0; i < frame->picture_count; i++) {
            if (frame->pictures[i].pending)
                frame->pictures[i].pending->unit_seq =
                    (uint32_t)(c->next_unit + i + 1);
        }
        mtx_unlock(&c->pend_mutex);

        for (unsigned i = 0; i < frame->picture_count; i++) {
            struct tva_av1_picture *pic = &frame->pictures[i];
            size_t unit_cap = pic->tile_bytes + 8192u + 64u;
            if (unit_cap < pic->tile_bytes || unit_cap > MAX_FRAME) {
                debug_printf("tva: AV1 picture is too large (%zu bytes)\n",
                             pic->tile_bytes);
                tva_mark_broken(c);
                tva_av1_pending_clear(frame);
                return -1;
            }

            uint8_t *unit = malloc(unit_cap);
            if (!unit) {
                tva_mark_broken(c);
                tva_av1_pending_clear(frame);
                return -1;
            }

            size_t unit_len = 0;
            size_t n = dmd_av1_obu_header(DMD_OBU_TEMPORAL_DELIMITER, 0,
                                          unit, unit_cap);
            if (!n) {
                free(unit);
                tva_mark_broken(c);
                tva_av1_pending_clear(frame);
                return -1;
            }
            unit_len += n;
            if (i == 0 && frame->include_sequence) {
                n = dmd_av1_build_sequence_header(&pic->picture,
                                                  unit + unit_len,
                                                  unit_cap - unit_len);
                if (!n) {
                    free(unit);
                    tva_mark_broken(c);
                    tva_av1_pending_clear(frame);
                    return -1;
                }
                unit_len += n;
            }

            n = dmd_av1_build_frame(&pic->picture, pic->tiles,
                                    (int)pic->tile_count,
                                    pic->refresh_frame_flags,
                                    unit + unit_len, unit_cap - unit_len);
            if (!n) {
                free(unit);
                tva_mark_broken(c);
                tva_av1_pending_clear(frame);
                return -1;
            }
            unit_len += n;

            uint8_t map_idx = 0;
            if (!pic->picture.pic_info_fields.bits.show_frame) {
                const uint8_t mask = pic->refresh_frame_flags;
                map_idx = mask ? (uint8_t)__builtin_ctz(mask) : 0;
                n = dmd_av1_build_show_existing(map_idx,
                                                unit + unit_len,
                                                unit_cap - unit_len);
                if (!n) {
                    free(unit);
                    tva_mark_broken(c);
                    tva_av1_pending_clear(frame);
                    return -1;
                }
                unit_len += n;
            }

            TVA_TRACE("AV1 inline picture=%u unit=%u show=%u map=%u refresh=%02x",
                      i + 1, (unsigned)c->next_unit + 1,
                      pic->picture.pic_info_fields.bits.show_frame,
                      map_idx, pic->refresh_frame_flags);
            if (tva_session_send_unit(c->sess, unit, unit_len) != TVA_OK) {
                debug_printf("tva: AV1 inline send failed: %s\n",
                             tva_session_last_error(c->sess));
                free(unit);
                tva_av1_pending_clear(frame);
                tva_mark_broken(c);
                return -1;
            }
            free(unit);
            c->next_unit++;
        }

        c->av1_sequence_sent |= frame->include_sequence;
        tva_av1_pending_clear(frame);
        return 0;
    }

    /* In hidden-output mode each picture gets its own input unit.  The
     * Qualcomm decoder otherwise emits only one output for a temporal unit,
     * even when all of its frame OBUs carry show_frame=1.  Separate units
     * preserve the AV1 reference chain while giving the daemon one PTS (and
     * therefore one pending surface) per decoded picture. */
    if (tva_av1_output_hidden()) {
        if (frame->picture_count > UINT32_MAX - c->next_unit) {
            debug_printf("tva: AV1 temporal unit sequence overflow\n");
            tva_mark_broken(c);
            tva_av1_pending_clear(frame);
            return -1;
        }

        for (unsigned i = 0; i < frame->picture_count; i++) {
            struct tva_av1_picture *pic = &frame->pictures[i];
            size_t unit_cap = pic->tile_bytes + 8192u +
                              (size_t)pic->tile_count * 4u;
            if (unit_cap < pic->tile_bytes || unit_cap > MAX_FRAME) {
                debug_printf("tva: AV1 temporal unit is too large (%zu bytes)\n",
                             pic->tile_bytes);
                tva_mark_broken(c);
                tva_av1_pending_clear(frame);
                return -1;
            }

            uint8_t *unit = malloc(unit_cap);
            if (!unit) {
                tva_mark_broken(c);
                tva_av1_pending_clear(frame);
                return -1;
            }

            size_t unit_len = 0;
            size_t n = dmd_av1_obu_header(DMD_OBU_TEMPORAL_DELIMITER, 0,
                                          unit, unit_cap);
            if (!n) {
                free(unit);
                tva_mark_broken(c);
                tva_av1_pending_clear(frame);
                return -1;
            }
            unit_len += n;

            if (i == 0 && frame->include_sequence) {
                n = dmd_av1_build_sequence_header(&pic->picture,
                                                  unit + unit_len,
                                                  unit_cap - unit_len);
                if (!n) {
                    free(unit);
                    tva_mark_broken(c);
                    tva_av1_pending_clear(frame);
                    return -1;
                }
                unit_len += n;
            }

            VADecPictureParameterBufferAV1 picture = pic->picture;
            picture.pic_info_fields.bits.show_frame = 1;
            n = dmd_av1_build_frame(&picture, pic->tiles,
                                    (int)pic->tile_count,
                                    pic->refresh_frame_flags,
                                    unit + unit_len, unit_cap - unit_len);
            if (!n) {
                free(unit);
                tva_mark_broken(c);
                tva_av1_pending_clear(frame);
                return -1;
            }
            unit_len += n;

            TVA_TRACE("AV1 unit len=%zu picture=%u/%u source_show=%u "
                      "encoded_show=1 refresh=%02x order=%u type=%u%s",
                      unit_len, i + 1, frame->picture_count,
                      pic->picture.pic_info_fields.bits.show_frame,
                      pic->refresh_frame_flags, pic->picture.order_hint,
                      pic->picture.pic_info_fields.bits.frame_type,
                      i == 0 && frame->include_sequence ? " sequence" : "");
            if (tva_session_send_unit(c->sess, unit, unit_len) != TVA_OK) {
                debug_printf("tva: AV1 send_unit failed: %s\n",
                             tva_session_last_error(c->sess));
                free(unit);
                tva_av1_pending_clear(frame);
                tva_mark_broken(c);
                return -1;
            }
            free(unit);
            c->next_unit++;
        }

        c->av1_sequence_sent |= frame->include_sequence;
        tva_av1_pending_clear(frame);
        return 0;
    }

    size_t tile_bytes = 0;
    size_t overhead = 2048;
    for (unsigned i = 0; i < frame->picture_count; i++) {
        const struct tva_av1_picture *pic = &frame->pictures[i];
        if (pic->tile_bytes > SIZE_MAX - tile_bytes ||
            tile_bytes + pic->tile_bytes > SIZE_MAX - overhead ||
            overhead > SIZE_MAX - 1024 - (size_t)pic->tile_count * 4)
            goto too_large;
        tile_bytes += pic->tile_bytes;
        overhead += 1024 + (size_t)pic->tile_count * 4;
    }
    if (tile_bytes > MAX_FRAME || tile_bytes > SIZE_MAX - overhead ||
        tile_bytes + overhead > MAX_FRAME) {
too_large:
        debug_printf("tva: AV1 temporal unit is too large (%zu bytes)\n",
                     tile_bytes);
        tva_mark_broken(c);
        tva_av1_pending_clear(frame);
        return -1;
    }

    const size_t unit_cap = tile_bytes + overhead;
    uint8_t *unit = malloc(unit_cap);
    if (!unit) {
        tva_mark_broken(c);
        tva_av1_pending_clear(frame);
        return -1;
    }

    size_t unit_len = 0;
    size_t n = dmd_av1_obu_header(DMD_OBU_TEMPORAL_DELIMITER, 0,
                                  unit, unit_cap);
    if (!n)
        goto failed;
    unit_len += n;

    if (frame->include_sequence) {
        n = dmd_av1_build_sequence_header(&frame->pictures[0].picture,
                                          unit + unit_len,
                                          unit_cap - unit_len);
        if (!n)
            goto failed;
        unit_len += n;
    }

    for (unsigned i = 0; i < frame->picture_count; i++) {
        struct tva_av1_picture *pic = &frame->pictures[i];
        n = dmd_av1_build_frame(&pic->picture, pic->tiles,
                                (int)pic->tile_count,
                                pic->refresh_frame_flags,
                                unit + unit_len, unit_cap - unit_len);
        if (!n)
            goto failed;
        unit_len += n;
    }

    TVA_TRACE("AV1 unit len=%zu pictures=%u payload=%zu%s", unit_len,
              frame->picture_count, tile_bytes,
              frame->include_sequence ? " sequence" : "");
    for (unsigned i = 0; i < frame->picture_count; i++)
        TVA_TRACE("AV1 unit picture=%u show=%u refresh=%02x order=%u type=%u",
                  i + 1,
                  frame->pictures[i].picture.pic_info_fields.bits.show_frame,
                  frame->pictures[i].refresh_frame_flags,
                  frame->pictures[i].picture.order_hint,
                  frame->pictures[i].picture.pic_info_fields.bits.frame_type);

    /* A hidden AV1 picture updates the decoder reference map but does not
     * normally produce a MediaCodec output buffer.  Keep the efficient
     * temporal-unit submission above, then ask the decoder to present each
     * refreshed reference with a tiny show_existing_frame unit.  This fills
     * the VA surface assigned to the hidden picture without decoding it a
     * second time or forcing every picture into a separate access unit. */
    unsigned synthetic_hidden = 0;
    if (tva_av1_synthetic_show_existing()) {
        const uint32_t base_seq = (uint32_t)c->next_unit + 1;
        mtx_lock(&c->pend_mutex);
        for (unsigned i = 0; i < frame->picture_count; i++) {
            struct tva_av1_picture *pic = &frame->pictures[i];
            if (!pic->pending)
                continue;
            if (pic->picture.pic_info_fields.bits.show_frame) {
                pic->pending->unit_seq = base_seq;
            } else {
                pic->pending->unit_seq = base_seq + 1 + synthetic_hidden;
                synthetic_hidden++;
            }
        }
        mtx_unlock(&c->pend_mutex);
    }

    if (tva_session_send_unit(c->sess, unit, unit_len) != TVA_OK) {
        debug_printf("tva: AV1 send_unit failed: %s\n",
                     tva_session_last_error(c->sess));
        free(unit);
        tva_av1_pending_clear(frame);
        tva_mark_broken(c);
        return -1;
    }

    free(unit);
    c->av1_sequence_sent |= frame->include_sequence;
    c->next_unit++;

    if (tva_av1_synthetic_show_existing()) {
        for (unsigned i = 0; i < frame->picture_count; i++) {
            struct tva_av1_picture *pic = &frame->pictures[i];
            if (pic->picture.pic_info_fields.bits.show_frame)
                continue;

            uint8_t mask = pic->refresh_frame_flags;
            uint8_t map_idx = mask ? (uint8_t)__builtin_ctz(mask) : 0;
            unsigned char show_unit[32];
            size_t show_hdr = dmd_av1_obu_header(DMD_OBU_TEMPORAL_DELIMITER,
                                                 0, show_unit,
                                                 sizeof(show_unit));
            size_t show_obu = show_hdr
                ? dmd_av1_build_show_existing(map_idx,
                                              show_unit + show_hdr,
                                              sizeof(show_unit) - show_hdr)
                : 0;
            if (!show_hdr || !show_obu ||
                show_hdr + show_obu > sizeof(show_unit)) {
                debug_printf("tva: AV1 show_existing_frame build failed\n");
                tva_av1_pending_clear(frame);
                tva_mark_broken(c);
                return -1;
            }
            const size_t show_len = show_hdr + show_obu;
            TVA_TRACE("AV1 synthetic show_existing unit=%u map=%u refresh=%02x",
                      (unsigned)c->next_unit + 1, map_idx, mask);
            if (tva_session_send_unit(c->sess, show_unit, show_len) != TVA_OK) {
                debug_printf("tva: AV1 show_existing send failed: %s\n",
                             tva_session_last_error(c->sess));
                tva_av1_pending_clear(frame);
                tva_mark_broken(c);
                return -1;
            }
            c->next_unit++;
        }
    }

    tva_av1_pending_clear(frame);
    return 0;

failed:
    free(unit);
    tva_av1_pending_clear(frame);
    tva_mark_broken(c);
    return -1;
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
                if (!c->h264_pps_defaults_valid) {
                    if (h264->slice_parameter.slice_info_present) {
                        /* VA exposes the active list sizes with each slice
                         * parameter.  The PPS defaults themselves are not
                         * part of VAPictureParameterBufferH264, so seed the
                         * synthetic PPS from the first observed values.  The
                         * values are the effective counts reported by the
                         * VA/FFmpeg parser, including defaults used when a
                         * slice omits num_ref_idx_active_override_flag. */
                        c->h264_pps_l0_default =
                            h264->num_ref_idx_l0_active_minus1;
                        c->h264_pps_l1_default =
                            h264->num_ref_idx_l1_active_minus1;
                    } else {
                        unsigned default_refs = c->base.max_references
                                                ? MIN2(c->base.max_references, 16) - 1
                                                : 0;
                        c->h264_pps_l0_default = default_refs;
                        c->h264_pps_l1_default = default_refs;
                    }
                    c->h264_pps_defaults_valid = true;
                } else if (h264->slice_parameter.slice_info_present) {
                    /* Reference-list counts can be zero for an IDR picture
                     * and increase on later P/B pictures.  Non-high profiles
                     * retain the largest values observed so the PPS converges
                     * without emitting a new parameter set for every frame.
                     * High-profile streams may use explicit per-slice list
                     * sizes larger than their PPS defaults; once a non-zero
                     * default has been learned, keep it stable instead of
                     * replacing it with those explicit values. */
                    if (!tva_h264_high_profile(c->base.profile) ||
                        c->h264_pps_l0_default == 0)
                        c->h264_pps_l0_default = MAX2(
                            c->h264_pps_l0_default,
                            (unsigned)h264->num_ref_idx_l0_active_minus1);
                    if (!tva_h264_high_profile(c->base.profile) ||
                        c->h264_pps_l1_default == 0)
                        c->h264_pps_l1_default = MAX2(
                            c->h264_pps_l1_default,
                            (unsigned)h264->num_ref_idx_l1_active_minus1);
                }
                size_t sps_rbsp_len = tva_build_h264_sps(c->base.profile,
                                                         h264->pps->sps,
                                                         c->base.max_references,
                                                         target ? target->width : c->base.width,
                                                         target ? target->height : c->base.height,
                                                         &sps_rbsp);
                size_t pps_rbsp_len = tva_build_h264_pps(c->base.profile,
                                                         h264->pps,
                                                         c->h264_pps_l0_default,
                                                         c->h264_pps_l1_default,
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
                        TVA_TRACE("CSD sent: profile=%d sps=%zu pps=%zu maxrefs=%u defaults=%u/%u",
                                  (int)c->base.profile, sps_len, pps_len,
                                  c->base.max_references,
                                  c->h264_pps_l0_default,
                                  c->h264_pps_l1_default);
                    }
                }
                free(csd);
                free(sps_buf);
                free(pps_buf);
            }
            if (tva_codec_is_broken(c))
                return -1;
        } else if (format == PIPE_VIDEO_FORMAT_HEVC) {
            struct pipe_h265_picture_desc *h265 =
                (struct pipe_h265_picture_desc *)picture;
            if (h265 && h265->pps && h265->pps->sps) {
                const struct pipe_h265_sps *sps = h265->pps->sps;
                static const uint8_t sc[4] = { 0, 0, 0, 1 };
                uint8_t *vps_nalu = NULL, *sps_nalu = NULL;
                uint8_t *pps_nalu = NULL;

                if (!tva_h265_can_build(sps, h265)) {
                    debug_printf("tva: HEVC parameter sets cannot be synthesized "
                                 "for this stream\n");
                    tva_mark_broken(c);
                    return -1;
                }

                size_t vps_nalu_len =
                    tva_build_h265_vps(sps, c->base.profile, &vps_nalu);
                size_t sps_nalu_len =
                    tva_build_h265_sps(sps, c->base.profile, &sps_nalu);
                size_t pps_nalu_len = tva_build_h265_pps(h265->pps, &pps_nalu);
                if (!vps_nalu_len || !sps_nalu_len || !pps_nalu_len) {
                    debug_printf("tva: HEVC CSD synthesis failed\n");
                    free(vps_nalu);
                    free(sps_nalu);
                    free(pps_nalu);
                    tva_mark_broken(c);
                    return -1;
                }

                size_t vps_len = vps_nalu_len + 4;
                size_t sps_len = sps_nalu_len + 4;
                size_t pps_len = pps_nalu_len + 4;
                size_t nlen = vps_len + sps_len + pps_len;
                uint8_t *csd = malloc(nlen);
                if (!csd) {
                    free(vps_nalu);
                    free(sps_nalu);
                    free(pps_nalu);
                    tva_mark_broken(c);
                    return -1;
                }
                memcpy(csd, sc, 4);
                memcpy(csd + 4, vps_nalu, vps_nalu_len);
                memcpy(csd + vps_len, sc, 4);
                memcpy(csd + vps_len + 4, sps_nalu, sps_nalu_len);
                memcpy(csd + vps_len + sps_len, sc, 4);
                memcpy(csd + vps_len + sps_len + 4,
                       pps_nalu, pps_nalu_len);

                if (!c->csd || c->csd_len != nlen ||
                    memcmp(c->csd, csd, nlen)) {
                    free(c->csd);
                    c->csd = csd;
                    c->csd_len = nlen;
                    csd = NULL;

                    int rc = tva_session_send_unit(c->sess,
                                                    c->csd, vps_len);
                    if (rc == TVA_OK)
                        rc = tva_session_send_unit(c->sess,
                                                    c->csd + vps_len, sps_len);
                    if (rc == TVA_OK)
                        rc = tva_session_send_unit(c->sess,
                                                    c->csd + vps_len + sps_len,
                                                    pps_len);
                    if (rc != TVA_OK) {
                        debug_printf("tva: HEVC CSD send failed: %s\n",
                                     tva_session_last_error(c->sess));
                        tva_mark_broken(c);
                    } else {
                        TVA_TRACE("HEVC CSD sent: profile=%d vps=%zu sps=%zu pps=%zu",
                                  (int)c->base.profile, vps_len, sps_len,
                                  pps_len);
                    }
                }
                free(csd);
                free(vps_nalu);
                free(sps_nalu);
                free(pps_nalu);
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
    } else if (format == PIPE_VIDEO_FORMAT_AV1) {
        /* VA-API supplies AV1 tile payloads separately from the structured
         * picture parameters.  Rebuild complete temporal units before
         * passing them to MediaCodec; forwarding c->acc directly is not valid
         * AV1 and starts with tile bytes rather than an OBU header. */
        const struct pipe_av1_picture_desc *av1 =
            (const struct pipe_av1_picture_desc *)picture;
        VADecPictureParameterBufferAV1 va_pic;
        if (!tva_av1_to_va_picture(av1, &va_pic)) {
            debug_printf("tva: AV1 picture parameters are invalid\n");
            c->acc_len = 0;
            tva_mark_broken(c);
            return -1;
        }
        const bool show_frame = va_pic.pic_info_fields.bits.show_frame;
        TVA_TRACE("AV1 VA params: warp=%u tx=%u show=%u refresh=%u",
                  va_pic.pic_info_fields.bits.allow_warped_motion,
                  va_pic.mode_control_fields.bits.reduced_tx_set_used,
                  show_frame,
                  av1->picture_parameter.refresh_frame_flags);

        const uint32_t tile_count =
            (uint32_t)va_pic.tile_cols * (uint32_t)va_pic.tile_rows;
        if (!tile_count || tile_count > 256 ||
            av1->slice_parameter.slice_count != tile_count) {
            debug_printf("tva: AV1 tile count mismatch (params=%u slices=%u)\n",
                         tile_count,
                         av1 ? (unsigned)av1->slice_parameter.slice_count : 0);
            c->acc_len = 0;
            tva_mark_broken(c);
            return -1;
        }

        struct dmd_av1_tile tiles[256];
        size_t tile_bytes = 0;
        for (uint32_t i = 0; i < tile_count; i++) {
            const size_t off = av1->slice_parameter.slice_data_offset[i];
            const size_t len = av1->slice_parameter.slice_data_size[i];
            if (!len || off > c->acc_len || len > c->acc_len - off) {
                debug_printf("tva: AV1 tile %u is outside the slice buffer "
                             "(off=%zu len=%zu buffer=%zu)\n",
                             i, off, len, c->acc_len);
                c->acc_len = 0;
                tva_mark_broken(c);
                return -1;
            }
            if (tile_bytes > SIZE_MAX - len) {
                c->acc_len = 0;
                tva_mark_broken(c);
                return -1;
            }
            tiles[i].data = c->acc + off;
            tiles[i].len = len;
            tile_bytes += len;
        }

        struct tva_av1_frame *frame = &c->av1_pending;

        /* The VA descriptor has no temporal-delimiter marker.  Chromium's
         * AV1 parser presents each temporal unit as either one displayed
         * frame, or a run of hidden reference frames followed by one displayed
         * frame.  Once a displayed frame has been seen without a hidden frame
         * after it, the next descriptor (hidden or displayed) starts the next
         * temporal unit.  This keeps a displayed key frame separate from the
         * hidden references of the following unit. */
        bool boundary = frame->valid && frame->picture_count &&
                        frame->have_show_frame &&
                        !frame->hidden_since_show_frame;
        if (frame->valid && frame->picture_count) {
            struct tva_av1_picture *previous =
                &frame->pictures[frame->picture_count - 1];
            previous->refresh_frame_flags = tva_av1_refresh_mask(av1,
                                                                  previous->target);
        }
        if (boundary) {
            TVA_TRACE("AV1 group flush before show frame: pictures=%u",
                      frame->picture_count);
            if (tva_av1_send_pending(c)) {
                c->acc_len = 0;
                return -1;
            }
            frame = &c->av1_pending;
        }
        if (frame->picture_count >= TVA_AV1_GROUP_MAX_FRAMES) {
            debug_printf("tva: AV1 temporal unit has too many frames (%u)\n",
                         frame->picture_count);
            c->acc_len = 0;
            tva_mark_broken(c);
            return -1;
        }

        const bool key_frame = va_pic.pic_info_fields.bits.frame_type == 0;
        const bool include_sequence = key_frame || !c->av1_sequence_sent;
        uint8_t *tile_data = malloc(tile_bytes);
        if (!tile_data) {
            c->acc_len = 0;
            tva_mark_broken(c);
            return -1;
        }
        size_t tile_offset = 0;
        for (uint32_t i = 0; i < tile_count; i++) {
            memcpy(tile_data + tile_offset, tiles[i].data, tiles[i].len);
            tiles[i].data = tile_data + tile_offset;
            tile_offset += tiles[i].len;
        }

        struct tva_av1_picture *current =
            &frame->pictures[frame->picture_count];
        memset(current, 0, sizeof(*current));
        current->target = target;
        current->picture = va_pic;
        current->tile_data = tile_data;
        current->tile_bytes = tile_bytes;
        current->tile_count = tile_count;
        for (uint32_t i = 0; i < tile_count; i++)
            current->tiles[i] = tiles[i];

        frame->valid = true;
        frame->picture_count++;
        frame->include_sequence |= include_sequence;
        if (show_frame) {
            frame->have_show_frame = true;
            frame->hidden_since_show_frame = false;
        } else {
            frame->hidden_since_show_frame = true;
        }

        /* Hidden AV1 frames normally update the decoder's reference state but
         * do not produce an output buffer.  In hidden-output mode the
         * reconstructed stream marks them as shown, so reserve a fence-less
         * entry for each one and let the reader pair repeated output frames
         * with the entries sharing this temporal-unit sequence number. */
        struct tva_fence *fence = NULL;
        if (show_frame || tva_av1_output_hidden() ||
            tva_av1_synthetic_show_existing() ||
            tva_av1_inline_show_existing()) {
            fence = CALLOC_STRUCT(tva_fence);
            if (!fence) {
                tva_av1_pending_clear(frame);
                c->acc_len = 0;
                tva_mark_broken(c);
                return -1;
            }
            if (c->next_unit >= UINT32_MAX) {
                FREE(fence);
                tva_av1_pending_clear(frame);
                c->acc_len = 0;
                tva_mark_broken(c);
                return -1;
            }
            /* In hidden-output mode pictures are submitted as separate
             * daemon units.  Reserve the sequence number this picture will
             * receive within the accumulated temporal unit; the normal path
             * keeps one sequence number for the whole group. */
            uint64_t pending_seq = c->next_unit + 1;
            if (tva_av1_output_hidden())
                pending_seq = c->next_unit + frame->picture_count;
            else if (tva_av1_synthetic_show_existing() ||
                     tva_av1_inline_show_existing())
                pending_seq = 0; /* assigned when the group is flushed */
            if (pending_seq > UINT32_MAX) {
                FREE(fence);
                tva_av1_pending_clear(frame);
                c->acc_len = 0;
                tva_mark_broken(c);
                return -1;
            }
            mtx_lock(&c->pend_mutex);
            pending = tva_pend_reserve_locked(c, (uint32_t)pending_seq,
                                              target, fence);
            /* Chromium recycles AV1 surfaces as soon as their VA fence is
             * replaced.  A late daemon output must never be copied into that
             * surface, including the ordinary visible-frame path: doing so
             * can overwrite a newer frame and make old images flash back on
             * screen. */
            if (pending)
                pending->drop_on_fence_destroy = true;
            mtx_unlock(&c->pend_mutex);
            if (!pending) {
                FREE(fence);
                tva_av1_pending_clear(frame);
                c->acc_len = 0;
                return -1;
            }
            current->pending = pending;
        }

        TVA_TRACE("AV1 group append picture=%u show=%u tiles=%u payload=%zu%s",
                  frame->picture_count, show_frame, tile_count, tile_bytes,
                  show_frame ? " output" : " hidden");

        if (picture && picture->out_fence &&
            (show_frame || tva_av1_output_hidden() ||
             tva_av1_synthetic_show_existing() ||
             tva_av1_inline_show_existing())) {
            if (*picture->out_fence)
                tva_codec_destroy_fence(codec, *picture->out_fence);
            *picture->out_fence = (struct pipe_fence_handle *)fence;
        } else if (picture && picture->out_fence) {
            /* A hidden frame has no corresponding output in the normal mode.
             * Keep the historical fence-less behaviour there; in
             * hidden-output mode it receives the regular fence above so an
             * export of a later show_existing_frame surface waits for the
             * reference copy. */
            if (*picture->out_fence)
                tva_codec_destroy_fence(codec, *picture->out_fence);
            *picture->out_fence = NULL;
        }
        if (show_frame || tva_av1_output_hidden() ||
            tva_av1_synthetic_show_existing() ||
            tva_av1_inline_show_existing())
            last_vcl = pending && pending->unit_seq ? pending->unit_seq :
                       (uint32_t)(c->next_unit + 1);
    } else {
        /* VP9 (no-start-code codec): one whole frame */
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
    struct tva_codec *c = tva_codec(codec);
    if (!c->av1_pending.valid || tva_codec_is_broken(c))
        return;

    /* No following VA descriptor is available at end of stream.  The final
     * picture does not need to be referenced by a later picture, so its zero
     * mask is sufficient; key frames infer all slots and do not code it. */
    if (c->av1_pending.picture_count)
        c->av1_pending.pictures[c->av1_pending.picture_count - 1]
            .refresh_frame_flags = 0;
    (void)tva_av1_send_pending(c);
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
    /* Chromium destroys a surface fence immediately before reusing the VA
     * surface for another picture.  Do not let a reordered daemon output
     * arrive after that reuse: it would overwrite the newer picture and make
     * an already displayed frame flash back on screen.  Wait for the pending
     * output while the resource is still owned by this entry, then copy a
     * staged frame before detaching the client fence. */
    bool fail = false;
    struct tva_pending *p = fence->slot;
    if (p && p->drop_on_fence_destroy) {
        const unsigned wait_ms = tva_av1_fence_destroy_wait_ms();
        const uint64_t wait_start = os_time_get_nano();
        const uint64_t deadline = os_time_get_nano() +
                                  (uint64_t)wait_ms * 1000000ull;
        /* Keep the entry alive while waiting, but release pend_mutex so the
         * reader can publish the matching frame.  Waiting on pend_cond while
         * holding this mutex would deadlock: the reader needs the same mutex
         * to set p->ready and signal the condition. */
        p->waiters++;
        mtx_unlock(&c->pend_mutex);
        for (;;) {
            mtx_lock(&c->pend_mutex);
            bool done = p->ready || c->broken;
            mtx_unlock(&c->pend_mutex);
            uint64_t now = os_time_get_nano();
            if (done || now >= deadline)
                break;
            uint64_t remaining_us = (deadline - now) / 1000ull;
            os_time_sleep((int64_t)MIN2(remaining_us, 1000ull));
        }
        mtx_lock(&c->pend_mutex);
        const uint64_t wait_elapsed = os_time_get_nano() - wait_start;
        if (!p->ready || wait_elapsed >= 1000000ull)
            TVA_TRACE("recycled AV1 fence wait unit=%u ready=%d wait_ms=%u "
                      "elapsed=%.3f ms", p->unit_seq, p->ready, wait_ms,
                      (double)wait_elapsed / 1000000.0);
        if (!p->ready) {
            TVA_TRACE("recycled AV1 fence timed out unit=%u", p->unit_seq);
            fail = true;
        } else if (!p->failed && !p->copied && p->staging) {
            p->copied = tva_copy_frame(c, p);
            TVA_TRACE("recycled AV1 fence copy unit=%u result=%d",
                      p->unit_seq, p->copied);
            if (!p->copied)
                fail = true;
        }
        p->waiters--;
    }
    tva_detach_fence_locked(fence, fail);
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
    tva_av1_pending_clear(&c->av1_pending);
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
#if defined(__linux__)
    if (tva_contiguous_dmabuf_enabled()) {
        struct pipe_video_buffer *buffer =
            tva_create_contiguous_video_buffer(context, templat);
        if (buffer)
            return buffer;
        if (getenv("DMD_VA_LOG"))
            fprintf(stderr, "tva: contiguous NV12 allocation failed; "
                    "falling back to separate plane resources\n");
    }
#endif

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
    } else if (codec_id == CODEC_AV1 && tva_av1_output_hidden()) {
        /* Hidden-output AV1 produces one daemon result for every reference
         * and displayed frame.  Use the complete SHM pool by default so the
         * sixth pending surface does not add avoidable backpressure. */
        pipeline_depth = SHM_SLOTS;
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
    /* DMD_AV1_STRICT_PENDING=1 keeps AV1 output submissions within the
     * configured depth.  The default soft limit lets the pending cache absorb
     * short decoder bursts while fence recycling still protects old surfaces. */
    c->strict_pending = codec_id == CODEC_AV1 && tva_av1_strict_pending();

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

    TVA_TRACE("codec ready pipeline=%u strict=%d hidden=%d synthetic=%d inline=%d shm=%d",
              c->pipeline_depth, c->strict_pending,
              tva_av1_output_hidden(), tva_av1_synthetic_show_existing(),
              tva_av1_inline_show_existing(), cfg.want_shm);
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
    int opened_fd = -1;
    if (!backend || !*backend || !strcmp(backend, "auto"))
        backend = "auto";

#if defined(__linux__)
    /* An X11 VA display in a PRoot container does not carry a DRM fd.  Open
     * KGSL here so the bridge can still create a GPU screen without a DRM
     * render node; the loader override below selects the KGSL alias. */
    if (fd < 0 && (!strcmp(backend, "auto") || !strcmp(backend, "kgsl"))) {
        opened_fd = open("/dev/kgsl-3d0", O_RDWR | O_CLOEXEC);
        if (opened_fd >= 0) {
            fd = opened_fd;
            if (!strcmp(backend, "auto"))
                backend = "kgsl";
        }
    }
#endif

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
        if (vscreen) {
            if (opened_fd >= 0)
                close(opened_fd);
            return vscreen;
        }
        /* 2. software fallback.  The KGSL alias is intentionally explicit:
         * applications must opt into GPU submission through /dev/kgsl-3d0
         * with TERMUX_VA_GPU_BACKEND=kgsl. */
        fprintf(stderr, "tva: stock drm screen creation failed, using llvmpipe\n");
        struct vl_screen *vscreen_sw = tva_vscreen_sw();
        if (opened_fd >= 0)
            close(opened_fd);
        return vscreen_sw;
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
        {
            if (opened_fd >= 0)
                close(opened_fd);
            return vscreen;
        }
        fprintf(stderr, "tva: kgsl screen creation failed, trying llvmpipe\n");
        struct vl_screen *vscreen_sw = tva_vscreen_sw();
        if (opened_fd >= 0)
            close(opened_fd);
        return vscreen_sw;
    }
    if (!strcmp(backend, "sw")) {
        struct vl_screen *vscreen = tva_vscreen_sw();
        if (opened_fd >= 0)
            close(opened_fd);
        return vscreen;
    }
    if (!strcmp(backend, "drm")) {
        struct vl_screen *vscreen = vl_drm_screen_create(fd, honor_dri_prime);
        if (opened_fd >= 0)
            close(opened_fd);
        return vscreen;
    }

    fprintf(stderr, "tva: unknown TERMUX_VA_GPU_BACKEND '%s', using auto\n", backend);
    if (opened_fd >= 0)
        close(opened_fd);
    return tva_bridge_vscreen_create(fd, honor_dri_prime);
}
