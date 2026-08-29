/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * tva_protocol.h - Wire protocol constants shared by the termux-va daemon
 *                  (Termux side) and the Mesa termux-va bridge (container
 *                  side, mesa-for-android-container branch
 *                  test/add-va-bridge, file src/gallium/frontends/va/tva_protocol.h).
 *
 * This file is the single source of truth for everything that travels on
 * the wire or must agree on both ends of the Unix socket. The two copies
 * MUST be byte-identical; scripts/check-mirror.sh enforces that with cmp(1)
 * (same discipline as anland-termux AGENTS.md).
 *
 * Copyright (C) 2026 lfdevs
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * This file is based on droidspaces-media-decode (src/decode-daemon.c and
 * vaapi-driver/src/dmd_client.{c,h}), which is licensed under the Apache
 * License, Version 2.0.  It has been MODIFIED for the termux-va project and
 * relicensed under the GNU General Public License version 3:
 *   - protocol constants extracted from the daemon/client inline defines
 *     into this shared header (values unchanged, wire format unchanged),
 *   - default socket name changed from decode.sock to termux-va.sock,
 *   - XFER_TCP renamed to XFER_INLINE (wire value 0 unchanged; the transport
 *     is always a path-based Unix socket in termux-va, TCP was removed).
 *
 * Protocol compatibility statement: the wire format stays byte-compatible
 * with droidspaces-media-decode protocol v3 (HELLO_MAGIC 0x444D4400) so the
 * upstream regression tools (tools/test_decode.py, dmd-probe) can be reused
 * unchanged.
 */
#ifndef TVA_PROTOCOL_H
#define TVA_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ paths */

/* Fixed socket file name inside the socket directory.  Both the daemon
 * (directory mode) and the Mesa bridge (default endpoint) join this name
 * onto the socket directory. */
#define TVA_SOCK_NAME "termux-va.sock"

/* Name of the socket directory relative to the shared tmp directory.
 * Termux default:  $TMPDIR/termux-va/termux-va.sock
 * Container view:  /tmp/termux-va/termux-va.sock   (proot --shared-tmp) */
#define TVA_SOCKET_DIR_NAME "termux-va"

/* --------------------------------------------------------------- handshake */

/*
 * Magic of the 24-byte big-endian handshake request
 *   [u32 magic][u32 version][u32 codec][u32 width][u32 height][u32 xfer]
 *
 * Kept identical to upstream droidspaces-media-decode ("DMD\0") so the
 * upstream protocol v3 regression tools work unchanged.  A legal NALU
 * length can never equal this value (1145389568 >> MAX_FRAME), which is
 * how the two are told apart... but note the handshake is REQUIRED here:
 * a first word that is not the magic is rejected (status=4).
 */
#define HELLO_MAGIC   0x444D4400u

/*
 * Current protocol version.
 *   v2: shared-memory transport negotiation added
 *   v3: response may carry the endpoint dev/ino extension (bit31 of the
 *       name-length word), used by the client to verify it connected to
 *       the live socket inode (bind-mount safety).
 *
 * Version negotiation: the daemon accepts versions 2..HELLO_VERSION and
 * takes the minimum; clients must do the same.
 */
#define HELLO_VERSION 3
#define TVA_VERSION_MIN 2

/*
 * Codec identifiers.  This is part of the wire protocol (handshake word 3).
 * Values may only be APPENDED, never reordered or reused.
 */
typedef enum {
    CODEC_H264 = 0,
    CODEC_HEVC = 1,
    CODEC_VP9  = 2,
    CODEC_VP8  = 3,
    CODEC_AV1  = 4,   /* accepted by the daemon; the Mesa bridge never
                       * requests it (not implemented upstream either) */
    CODEC_MAX
} CodecId;

/*
 * Frame return transport requested in the handshake and granted in the
 * response ("actual xfer" word).
 *
 *   0 = inline : frame data is sent over the control socket itself
 *   1 = SHM    : frame data is written into a memfd slot pool; the socket
 *                only carries a 24-byte control message naming the slot
 *
 * TVA: upstream calls value 0 "XFER_TCP" because upstream also had a TCP
 * control transport.  termux-va is Unix-socket-only, so the constant is
 * renamed XFER_INLINE; the WIRE VALUE 0 IS UNCHANGED and must stay 0.
 */
typedef enum {
    XFER_INLINE = 0,
    XFER_SHM    = 1
} XferMode;

/* Handshake status codes (response word 1):
 *   0 accepted / 1 version / 2 codec / 3 resolution out of range /
 *   4 handshake missing.  Error responses are always a bare 12 bytes. */

/* ------------------------------------------------------------------ limits */

/* Hard cap of one input unit (4-byte length prefix + data). */
#define MAX_FRAME (8 * 1024 * 1024)

/* Max concurrent decode sessions the daemon accepts (hardware supports 16). */
#define MAX_CLIENTS 8

/* ------------------------------------------------- format descriptor block */

/* Number of u32 words in the 32-byte format descriptor body:
 *   [buf_w][buf_h][stride][slice_height][crop_l][crop_t][crop_r][crop_b] */
#define FMTDESC_WORDS 8

/* frame_size values that mean "this is a control message, not a frame". */
#define FMTDESC_SENTINEL   0xFFFFFFFFu   /* followed by the 32-byte format block */
#define SHMFRAME_SENTINEL  0xFFFFFFFEu   /* followed by [slot][len][pts] in one
                                           * 24-byte SHM control message */

/* Capability flag in format-block header word 2: every frame header carries
 * a 4th field = input unit index (round-tripped through MediaCodec PTS). */
#define CAP_FRAME_PTS 0x00000001u

/*
 * Input unit index -> presentationTimeUs multiplier.
 *
 * The decoder quantizes PTS to milliseconds; feeding the raw unit index
 * (1us steps) collapses everything to 0.  x1000 keeps indices unique after
 * quantization.  The client divides by this to recover the index.
 */
#define PTS_UNIT_SCALE 1000

/* ------------------------------------------------------- shared memory pool */

/* Pool layout: [control area SHM_CTRL_BYTES][slot 0]..[slot SHM_SLOTS-1].
 * Each slot has a u32 state word in the control area: daemon sets 1
 * (release) after writing, client resets 0 (acquire) after consuming. */
#define SHM_SLOTS      8
#define SHM_CTRL_BYTES 4096

/*
 * How long the daemon spins waiting for a free slot (milliseconds).
 * MUST stay well above the bridge-side per-call frame timeout (5s) or the
 * daemon kills sessions the client would have completed.  15s = 3x.
 */
#define SHM_SLOT_WAIT_MS 15000

#ifdef __cplusplus
}
#endif

#endif /* TVA_PROTOCOL_H */
