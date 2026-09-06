/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is a MODIFIED version of vaapi-driver/src/bitstream.c from the
 * droidspaces-media-decode project (Apache License 2.0), relicensed under
 * GPL-3.0 for the Mesa termux-va bridge.
 */

/* Shared H.264/HEVC bitstream-writing primitives; see bitstream.h. */

#include <string.h>

#include "bitstream.h"


void dmd_bw_init(struct dmd_bitwriter *bw, unsigned char *buf, size_t cap)
{
    bw->buf = buf;
    bw->cap = cap;
    bw->byte_pos = 0;
    bw->bit_pos = 0;
    bw->overflow = 0;
    if (cap > 0)
        buf[0] = 0;
}

void dmd_bw_put_bits(struct dmd_bitwriter *bw, uint32_t value, int nbits)
{
    if (nbits <= 0 || nbits > 32) {
        bw->overflow = 1;
        return;
    }
    for (int i = nbits - 1; i >= 0; i--) {
        if (bw->byte_pos >= bw->cap) {
            bw->overflow = 1;
            return;
        }
        unsigned int bit = (value >> i) & 1u;
        bw->buf[bw->byte_pos] |= (unsigned char)(bit << (7 - bw->bit_pos));
        bw->bit_pos++;
        if (bw->bit_pos == 8) {
            bw->bit_pos = 0;
            bw->byte_pos++;
            if (bw->byte_pos < bw->cap)
                bw->buf[bw->byte_pos] = 0;
        }
    }
}

void dmd_bw_put_flag(struct dmd_bitwriter *bw, int v)
{
    dmd_bw_put_bits(bw, v ? 1u : 0u, 1);
}

/* ue(v): unsigned Exp-Golomb code, written as (leadingZeros) 1 (info).
 * The binary length of codeNum + 1 determines the number of leading zeros. */
void dmd_bw_put_ue(struct dmd_bitwriter *bw, uint32_t v)
{
    if (v == 0xFFFFFFFFu) { /* v + 1 would wrap around. */
        bw->overflow = 1;
        return;
    }
    uint32_t val = v + 1;
    int nbits = 0;
    while ((val >> nbits) != 0)
        nbits++;
    /* Write nbits - 1 leading zeros followed by the nbits bits of val (the
     * most-significant 1 is the separator).  When v == 0, nbits == 1 and
     * there are no leading zeros; dmd_bw_put_bits(..., 0) would report an
     * overflow because it rejects non-positive bit counts. */
    if (nbits > 1)
        dmd_bw_put_bits(bw, 0, nbits - 1);
    dmd_bw_put_bits(bw, val, nbits);
}

/* se(v): signed Exp-Golomb code.  Map 0, 1, -1, 2, -2, ... to
 * 0, 1, 2, 3, 4, ... respectively. */
void dmd_bw_put_se(struct dmd_bitwriter *bw, int32_t v)
{
    uint32_t code;
    if (v <= 0)
        code = (uint32_t)(-2 * (int64_t)v);
    else
        code = (uint32_t)(2 * (int64_t)v - 1);
    dmd_bw_put_ue(bw, code);
}

/* rbsp_trailing_bits: write one bit followed by zeros to the next byte
 * boundary. */
void dmd_bw_rbsp_trailing(struct dmd_bitwriter *bw)
{
    dmd_bw_put_flag(bw, 1);
    while (bw->bit_pos != 0)
        dmd_bw_put_flag(bw, 0);
}

/* Number of bytes written.  Call after rbsp_trailing, when the stream is
 * byte-aligned. */
size_t dmd_bw_bytes(const struct dmd_bitwriter *bw)
{
    return bw->bit_pos == 0 ? bw->byte_pos : bw->byte_pos + 1;
}

/* ------------------------------------------------- emulation-prevention escaping */

/* Convert RBSP to SODB/EBSP by inserting 03 after every 00 00 0x sequence
 * (x <= 3).  MediaCodec receives complete NAL units in escaped form;
 * otherwise a coincidental 00 00 01 sequence would be parsed as a start
 * code. */
size_t dmd_rbsp_escape(const unsigned char *rbsp, size_t len,
                          unsigned char *out, size_t out_cap)
{
    size_t o = 0;
    int zeros = 0;

    for (size_t i = 0; i < len; i++) {
        if (zeros >= 2 && rbsp[i] <= 0x03) {
            if (o >= out_cap)
                return 0;
            out[o++] = 0x03;
            zeros = 0;
        }
        if (o >= out_cap)
            return 0;
        out[o++] = rbsp[i];
        if (rbsp[i] == 0x00)
            zeros++;
        else
            zeros = 0;
    }
    return o;
}
