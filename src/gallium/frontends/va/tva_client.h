/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * tva_client.h - termux-va daemon client library (used by the Mesa
 *                termux-va VA bridge).
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
 * This file is a MODIFIED version of vaapi-driver/src/dmd_client.h from the
 * droidspaces-media-decode project (Apache License, Version 2.0), ported to
 * the termux-va project and relicensed under GPL-3.0.  Modifications:
 *   - the transport is a path-based Unix socket only (the TCP fallback and
 *     the TCP framing mode were removed; wire values unchanged),
 *   - protocol constants come from tva_protocol.h (the cmp-verified mirror
 *     of the daemon's copy),
 *   - symbols renamed dmd_* -> tva_*,
 *   - endpoint resolution defaults to /tmp/termux-va/termux-va.sock
 *     (the shared-tmp view of the Termux daemon's $TMPDIR/termux-va/),
 *     overridable through TERMUX_VA_SOCKET / TERMUX_VA_SOCKET_DIR.
 * ******************************************************************************
 *
 * This library wraps "being a client of the Android decode daemon" into an
 * opaque session handle.  It is compiled into libgallium and dlopen'ed into
 * Firefox / ffmpeg / Chrome processes, so it keeps the hard constraints of
 * the upstream library it derives from:
 *
 *   - never exit()/abort()/assert(): every error is reported via return code
 *   - never write stdout; logs go to stderr, silent unless DMD_VA_LOG=1
 *   - no global mutable state: all state lives in the session struct,
 *     multiple sessions may be open concurrently in one process
 *   - every blocking operation has a timeout (poll + non-blocking fds)
 *   - sends use MSG_NOSIGNAL: a closed peer cannot SIGPIPE the host
 *   - all fds are CLOEXEC: the host may fork/exec without leaking fds
 *   - depends on libc only (plus Mesa's os_get_option for the endpoint)
 */
#ifndef TVA_CLIENT_H
#define TVA_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#include "tva_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------- return codes */
/*
 * 0 success, positive non-error stream states, negative errors.
 * dmd-style legacy names are kept for the error codes the rest of the
 * protocol tooling refers to.
 */
enum {
    TVA_OK            =  0,
    TVA_EOS           =  1,   /* peer closed and no more frames will come */

    TVA_ERR_INVAL     = -1,
    TVA_ERR_NOMEM     = -2,
    TVA_ERR_CONNECT   = -3,   /* connect failed or timed out */
    TVA_ERR_REJECTED  = -4,   /* daemon rejected the handshake, see error hs_status */
    TVA_ERR_IO        = -5,   /* socket read/write failed */
    TVA_ERR_TIMEOUT   = -6,   /* wait timed out */
    TVA_ERR_PROTOCOL  = -7,   /* bytes not conforming to the protocol */
    TVA_ERR_STATE     = -8,   /* session state does not allow the operation */
    TVA_ERR_TOOBIG    = -9,   /* unit/frame beyond the protocol cap */
    TVA_ERR_ENDPOINT_MISMATCH = -10,
    TVA_ERR_CANCELLED  = -11  /* tva_session_cancel() interrupted the session */
    /* The (dev,ino) the client stat'ed differs from what the daemon
     * reported.  Typical cause: a single socket FILE (not a directory) was
     * bind-mounted and the daemon restarted, changing the inode.  Not
     * retryable, not downgradeable - fail loudly. */
};

/* Uplink unit cap (MAX_FRAME; the daemon rejects anything larger). */
#define TVA_MAX_UNIT_BYTES (8u * 1024u * 1024u)
/* Rational cap of a downlink frame.  MUST be far above TVA_MAX_UNIT_BYTES:
 * a single 4K NV12 frame is 12441600 bytes; the 8MB cap only bounds uplink. */
#define TVA_MAX_FRAME_BYTES (64u * 1024u * 1024u)

/* Default connect / io timeouts (ms). */
#define TVA_DEF_CONNECT_MS 2000
#define TVA_DEF_IO_MS      5000
/* Window to pick up the memfd.  The daemon waits 3s; 1.5s here keeps both
 * sides' views consistent. */
#define TVA_SHM_ATTACH_MS  1500

/* ------------------------------------------------------------ configuration */
struct tva_session_config {
    /* Path of the Unix socket to connect to.  NULL = use the default
     * resolution (tva_default_endpoint()). */
    const char *sock_path;
    int      codec;             /* CODEC_* from tva_protocol.h */
    int      width;             /* 96..8192 */
    int      height;            /* 96..4320 */
    int      want_shm;          /* non-0 = request SHM (daemon may downgrade) */
    int      connect_timeout_ms;/* <=0 = TVA_DEF_CONNECT_MS */
    int      io_timeout_ms;     /* <=0 = TVA_DEF_IO_MS */
};

/* Fill cfg with defaults (H.264, inline, default timeouts). */
void tva_session_config_defaults(struct tva_session_config *cfg);

/*
 * Resolve the default endpoint the same way the daemon does, in container
 * view:
 *   1. TERMUX_VA_SOCKET        (full socket file path)
 *   2. TERMUX_VA_SOCKET_DIR    (directory + TVA_SOCK_NAME)
 *   3. $TMPDIR/termux-va/termux-va.sock   (TMPDIR==/data/local/tmp ignored)
 *   4. /tmp/termux-va/termux-va.sock
 * When several candidates exist, the first that exists as a socket wins;
 * otherwise the last candidate is returned so connect() reports the error.
 * Returns a pointer into `buf`.
 */
const char *tva_default_endpoint(char *buf, size_t bufsz);

/* ------------------------------------------------------------ error detail */
struct tva_error {
    int  code;              /* TVA_ERR_* */
    int  handshake_status;  /* code==TVA_ERR_REJECTED: daemon status
                             * 1=version 2=codec 3=resolution 4=missing hello */
    char msg[192];          /* always NUL-terminated */
};

/* ------------------------------------------------------------ format */
/*
 * Geometry of the decoder output buffer, from the daemon's format block.
 *
 * buf_width/buf_height are the PADDED buffer dimensions (Qualcomm Venus
 * aligns width to 128 and height to 32; 1080p decodes into 1920x1088); the
 * visible area is the crop rect, which is a CLOSED interval:
 *   display width  = crop_right - crop_left + 1
 *   display height = crop_bottom - crop_top + 1
 * The UV plane starts at stride * slice_height.
 */
struct tva_format {
    int buf_width;
    int buf_height;
    int stride;
    int slice_height;
    int crop_left;
    int crop_top;
    int crop_right;
    int crop_bottom;
    int valid;          /* 0 = no format block received yet */
    int changes;        /* count of format blocks, >1 = mid-stream change */
};

int tva_format_display_width(const struct tva_format *fmt);
int tva_format_display_height(const struct tva_format *fmt);

/* ------------------------------------------------------------ frame */
/*
 * One NV12 frame.  Ownership of data always stays with the library:
 *   SHM mode  -> points into the shared memory slot; release resets the
 *                slot state word, handing it back to the daemon
 *   inline    -> points into the session receive buffer; release just marks
 *                it reusable
 * data is invalid after release.  Forgetting release in SHM mode makes the
 * daemon judge the client stuck (~15s slot timeout) - release is mandatory.
 */
struct tva_frame {
    uint8_t *data;
    size_t   size;

    uint32_t width;      /* buffer dimensions as announced in the header */
    uint32_t height;

    /* Input unit index (1-based) this frame belongs to; 0 = not provided.
     * Used to pair frames with submissions without knowing the decoder's
     * output order. */
    uint32_t unit_seq;

    /* Geometry snapshot from the latest format block, self-consistent
     * with this frame. */
    int stride;
    int slice_height;
    int crop_left;
    int crop_top;
    int crop_right;
    int crop_bottom;

    int      shm_slot;   /* >=0 = SHM slot; -1 = inline receive buffer */
    uint64_t seq;        /* session frame counter, starting at 0 */
};

/* ------------------------------------------------------------ session */
struct tva_session;

/*
 * Create a session: connect to the daemon, handshake, and (if requested)
 * take over the shared memory pool.  Returns NULL on failure; err (optional)
 * receives the reason.  No fd or mapping leaks on failure.
 */
struct tva_session *tva_session_create(const struct tva_session_config *cfg,
                                       struct tva_error *err);

/*
 * Interrupt a session's blocking receive/send operations without freeing it.
 * The session remains owned by the caller and must still be destroyed after
 * any worker using it has joined.
 */
void tva_session_cancel(struct tva_session *s);

/* Destroy the session: close, unmap, free.  NULL is a no-op. */
void tva_session_destroy(struct tva_session *s);

/*
 * Send one data unit.
 *   H.264/HEVC: one or more NALUs WITH their Annex B start codes (3 or 4
 *               bytes each) - the daemon locates the first nal_unit_header
 *               through the leading start code.  A complete access unit may
 *               contain multiple slice NALUs.
 *   VP8/VP9:    one whole frame WITHOUT start codes
 * The library never adds start codes itself (adding them wrongly corrupts
 * the stream silently); H.264/HEVC units without a start code are rejected
 * with TVA_ERR_PROTOCOL.  Returns TVA_OK / TVA_ERR_*.
 */
int tva_session_send_unit(struct tva_session *s, const void *data, size_t len);

/*
 * Take back a frame.  SHM mode returns the slot; inline mode releases the
 * receive buffer.  Unconditional calls are safe (NULL or released is a
 * no-op).
 */
/*
 * Reversible drain: send a zero-length unit so the daemon flushes the
 * decoder and emits whatever it holds; the session stays usable.  The
 * reference chain is destroyed by the flush, so frames after a drain
 * decode against broken references until the next IDR - send it only when
 * waiting is provably futile.
 */
int tva_session_drain(struct tva_session *s);

/* Take back a frame.  SHM mode returns the slot; inline mode releases the
 * receive buffer.  Unconditional calls are safe (NULL or released is a
 * no-op). */
int tva_session_release_frame(struct tva_session *s, struct tva_frame *f);

/*
 * Fetch the next frame.  timeout_ms < 0 uses the configured io timeout.
 * Returns TVA_OK (frame fetched), TVA_EOS, TVA_ERR_TIMEOUT (no frame right
 * now, session still usable) or an error.  Format blocks are consumed
 * internally and update tva_session_format().
 */
int tva_session_next_frame(struct tva_session *s, struct tva_frame *out,
                           int timeout_ms);

/* Latest format block; valid for the session lifetime. */
const struct tva_format *tva_session_format(const struct tva_session *s);

/* Human-readable reason of the last error; never NULL. */
const char *tva_session_last_error(const struct tva_session *s);

/* Latest error code (TVA_ERR_*), 0 when none. */
int tva_session_last_error_code(const struct tva_session *s);

/* Effective transport: TVA_XFER_INLINE / TVA_XFER_SHM.  Requesting SHM may
 * still be silently downgraded, so verify with this. */
int tva_session_xfer_mode(const struct tva_session *s);

/* Statistics: units sent / frames received. */
uint64_t tva_session_units_sent(const struct tva_session *s);
uint64_t tva_session_frames_received(const struct tva_session *s);

#ifdef __cplusplus
}
#endif

#endif /* TVA_CLIENT_H */
