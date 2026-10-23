/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is a MODIFIED version of vaapi-driver/src/bitstream.h from the
 * droidspaces-media-decode project (Apache License 2.0), relicensed under
 * GPL-3.0 for the Mesa termux-va bridge.
 */

/* Shared H.264/HEVC bitstream-writing primitives.
 *
 * Both codecs use the same syntax-element encodings (u(n)/ue(v)/se(v),
 * rbsp_trailing_bits and emulation prevention), so these helpers are shared
 * instead of duplicating the H.264 implementation for HEVC.
 */
#ifndef DMD_BITSTREAM_H
#define DMD_BITSTREAM_H

#include <stddef.h>
#include <stdint.h>

struct dmd_bitwriter {
    unsigned char *buf;
    size_t cap;
    size_t byte_pos;
    int bit_pos;       /* 0..7, number of bits written in the current byte */
    int overflow;      /* Once set, writes are no-ops; callers check this. */
};

void   dmd_bw_init(struct dmd_bitwriter *bw, unsigned char *buf, size_t cap);
void   dmd_bw_put_bits(struct dmd_bitwriter *bw, uint32_t value, int nbits);
void   dmd_bw_put_flag(struct dmd_bitwriter *bw, int v);
void   dmd_bw_put_ue(struct dmd_bitwriter *bw, uint32_t v);
void   dmd_bw_put_se(struct dmd_bitwriter *bw, int32_t v);
void   dmd_bw_rbsp_trailing(struct dmd_bitwriter *bw);
size_t dmd_bw_bytes(const struct dmd_bitwriter *bw);

/* RBSP -> EBSP: insert 03 after every 00 00 0x sequence (x <= 3).
 * MediaCodec requires the escaped representation.  Return the number of
 * bytes written to out, or 0 when the output buffer is too small. */
size_t dmd_rbsp_escape(const unsigned char *rbsp, size_t len,
                       unsigned char *out, size_t out_cap);

#endif
