/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is a MODIFIED version of vaapi-driver/src/av1_bitstream.h from
 * the droidspaces-media-decode project (Apache License 2.0), relicensed
 * under GPL-3.0 for the Mesa termux-va bridge.
 */

/* Reconstruct AV1 OBUs.
 *
 * VA-API passes AV1 decode parameters to the driver as structured fields
 * (VADecPictureParameterBufferAV1), while the bitstream side provides only
 * raw tile payloads with no OBU framing.  The downstream MediaCodec decoder
 * requires a complete AV1 OBU stream.
 *
 * libva documents this explicitly (/usr/include/va/va_dec_av1.h:643-645):
 *   "host decoder is responsible to parse out the per tile information.
 *    And the bit stream in sent to driver in per tile granularity."
 * The same file (:637-638) also notes that VASliceParameterBufferAV1
 * "actually means VATileParameterBufferAV1".
 *
 * Warning: this is different from VP9 and must not be copied from that path.
 * VP9 slice data is already a complete frame (va_dec_vp9.h:274-284), so no
 * reconstruction is needed there.  Applying the same approach to AV1 sends
 * an invalid stream: the observed first byte is 0xd0 (forbidden_bit = 1 and
 * reserved OBU type 10), and the decoder reports "No sequence header
 * available" without decoding a frame.
 *
 * The bit-writing primitives are shared with bitstream.h: AV1 f(n) and the
 * H.264/HEVC u(n) encodings are both fixed-width, MSB-first fields (AV1
 * specification 4.10.2).  The AV1-specific variable-length encodings
 * (leb128, uvlc and le) are implemented here.
 */
#ifndef DMD_AV1_BITSTREAM_H
#define DMD_AV1_BITSTREAM_H

#include <stddef.h>
#include <stdint.h>

#include "bitstream.h"

/* OBU types (AV1 specification 6.2.2, obu_type table). */
enum {
    DMD_OBU_SEQUENCE_HEADER        = 1,
    DMD_OBU_TEMPORAL_DELIMITER     = 2,
    DMD_OBU_FRAME_HEADER           = 3,
    DMD_OBU_TILE_GROUP             = 4,
    DMD_OBU_METADATA               = 5,
    DMD_OBU_FRAME                  = 6,
    DMD_OBU_REDUNDANT_FRAME_HEADER = 7,
    DMD_OBU_TILE_LIST              = 8,
    DMD_OBU_PADDING                = 15,
};

/* Maximum leb128 length (AV1 specification 4.10.5: eight bytes). */
#define DMD_LEB128_MAX 8

/* --------------------------------------------------------- variable-length codes */

/* leb128(v): little-endian groups of seven bits, with the high bit of each
 * byte indicating that another byte follows.  Write to out and return the
 * byte count, or 0 when out_cap is insufficient. */
size_t dmd_av1_leb128(uint64_t v, unsigned char *out, size_t out_cap);

/* Return the leb128 encoded length without writing; used to size obu_size. */
size_t dmd_av1_leb128_len(uint64_t v);

/* uvlc() (AV1 specification 4.10.3): a leading-zero count followed by a
 * mantissa.  Only a few frame-header fields use it; for example,
 * timing_info's num_units_in_display_tick does not, while delta_frame_id
 * does. */
void dmd_av1_put_uvlc(struct dmd_bitwriter *bw, uint32_t v);

/* le(n) (AV1 specification 4.10.4): an n-byte little-endian integer.
 * The bitstream must be byte-aligned when this is called. */
void dmd_av1_put_le(struct dmd_bitwriter *bw, uint64_t v, int nbytes);

/* ns(n) (AV1 specification 4.10.7): non-symmetric binary coding.  Used by
 * tile_info for context_update_tile_id and tile-size derivation. */
void dmd_av1_put_ns(struct dmd_bitwriter *bw, uint32_t v, uint32_t n);

/* su(n) (AV1 specification 4.10.6): signed fixed-width coding, used by
 * global_motion and related fields. */
void dmd_av1_put_su(struct dmd_bitwriter *bw, int32_t v, int nbits);

/* --------------------------------------------------------------- alignment */

/* byte_alignment() (AV1 specification 5.3.5): pad with zeros to a byte
 * boundary.  Unlike H.264 rbsp_trailing_bits, AV1 does not write a stop bit;
 * using the wrong helper makes the decoder interpret padding as syntax. */
void dmd_av1_byte_align(struct dmd_bitwriter *bw);

/* trailing_bits() (AV1 specification 5.3.4): write one bit and then pad with
 * zeros to a byte boundary.  Used at the end of OBU payloads (sequence_header
 * and frame_header, but not tile_group). */
void dmd_av1_trailing_bits(struct dmd_bitwriter *bw);

/* ---------------------------------------------------------------- OBU headers */

/* ------------------------------------------------ sequence-header assembly */

/* Build a complete OBU_SEQUENCE_HEADER from a
 * VADecPictureParameterBufferAV1 (OBU header + payload + trailing_bits).
 *
 * pic is const void * rather than a concrete type: this header deliberately
 * avoids including va_dec_av1.h, so libva dependencies do not spread to
 * callers that only need the bitstream primitives.  The implementation casts
 * it to the concrete type.
 *
 * Write to out and return the total byte count; return 0 for insufficient
 * capacity or invalid parameters. */
size_t dmd_av1_build_sequence_header(const void *pic,
                                     unsigned char *out, size_t out_cap);

/* ---------------------------------------------- OBU_FRAME assembly (3/4 + 4/4) */

/* Tile offset and length used by dmd_av1_build_frame() to assemble a
 * tile_group. */
struct dmd_av1_tile {
    const unsigned char *data;
    size_t               len;
};

/* Build a complete OBU_FRAME (6): frame header + byte_alignment + tile_group.
 *
 * Important: use OBU_FRAME rather than separate FRAME_HEADER (3) and
 * TILE_GROUP (4) OBUs.  dav1d rejects the split form with "Failed to read
 * unit 0 (type 3)", while the combined form succeeds.  Real libaom streams
 * also use OBU_FRAME.
 *
 * The crucial detail is that the frame header inside OBU_FRAME ends with
 * byte_alignment (zero padding), not trailing_bits (a one followed by
 * padding).  In specification 5.10.1 frame_obu(), byte_alignment() follows
 * frame_header_obu(); using trailing_bits shifts the tile_group start and
 * produces the same dav1d error.
 *
 * tiles are supplied in tile-row-major order, and the count must equal
 * tile_cols * tile_rows.  Write to out and return the total byte count; return
 * 0 for insufficient capacity or invalid parameters. */
size_t dmd_av1_build_frame(const void *pic,
                           const struct dmd_av1_tile *tiles, int num_tiles,
                           uint8_t refresh_frame_flags,
                           unsigned char *out, size_t out_cap);

/* ---------------------------------------------------- frame-header assembly */

/* Build a complete OBU_FRAME_HEADER from a VADecPictureParameterBufferAV1
 * (OBU header + payload + trailing_bits).
 *
 * tile_cols and tile_rows come from pic.  tile_size_bytes is fixed at four
 * bytes and must match the tile_size_minus_1 width written in tile_group.
 *
 * Write to out and return the total byte count; return 0 for insufficient
 * capacity or invalid parameters. */
size_t dmd_av1_build_frame_header(const void *pic,
                                  unsigned char *out, size_t out_cap);

/* obu_header() + obu_size (AV1 specification 5.3.1/5.3.2).
 *
 * Bit layout (one byte without an extension):
 *   obu_forbidden_bit    f(1)  must be 0
 *   obu_type             f(4)
 *   obu_extension_flag   f(1)  always 0 (no scalable layer)
 *   obu_has_size_field   f(1)  always 1 (required by low-overhead format)
 *   obu_reserved_1bit    f(1)  must be 0
 *
 * Write to out (one header byte followed by leb128 payload_len) and return the
 * total byte count; return 0 when out_cap is insufficient.  The caller then
 * appends the payload at the returned offset. */
size_t dmd_av1_obu_header(int obu_type, size_t payload_len,
                          unsigned char *out, size_t out_cap);

#endif
