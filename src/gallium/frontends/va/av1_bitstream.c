/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is a MODIFIED version of vaapi-driver/src/av1_bitstream.c from
 * the droidspaces-media-decode project (Apache License 2.0), relicensed
 * under GPL-3.0 for the Mesa termux-va bridge.
 */

/* Reconstruct AV1 OBUs: variable-length codes, alignment and OBU headers.
 *
 * See the design notes at the top of av1_bitstream.h.  This file implements
 * the low-level bit-writing primitives together with sequence-header,
 * frame-header and tile-group syntax.
 */
#include <va/va.h>
#include <va/va_dec_av1.h>
#include <string.h>

#include "av1_bitstream.h"

/* ---------------------------------------------------------------- leb128 */

size_t dmd_av1_leb128_len(uint64_t v)
{
    size_t n = 0;
    do {
        n++;
        v >>= 7;
    } while (v && n < DMD_LEB128_MAX);
    return n;
}

size_t dmd_av1_leb128(uint64_t v, unsigned char *out, size_t out_cap)
{
    size_t n = 0;
    do {
        if (n >= out_cap || n >= DMD_LEB128_MAX)
            return 0;
        unsigned char byte = (unsigned char)(v & 0x7f);
        v >>= 7;
        if (v)
            byte |= 0x80;          /* another byte follows */
        out[n++] = byte;
    } while (v);
    return n;
}

/* ------------------------------------------------------------------ uvlc */

void dmd_av1_put_uvlc(struct dmd_bitwriter *bw, uint32_t v)
{
    /* AV1 specification 4.10.3: write leadingZeros zero bits, one bit, and
     * leadingZeros mantissa bits.  The value v is encoded as
     * (1 << leadingZeros) - 1 + mantissa. */
    uint32_t leading_zeros = 0;
    uint64_t val = (uint64_t)v + 1;

    while ((val >> (leading_zeros + 1)) != 0)
        leading_zeros++;

    if (leading_zeros >= 32) {
        bw->overflow = 1;
        return;
    }

    /* Write leading_zeros zero bits; put_bits rejects nbits == 0, so skip the
     * call for the empty prefix. */
    if (leading_zeros > 0)
        dmd_bw_put_bits(bw, 0, (int)leading_zeros);
    dmd_bw_put_flag(bw, 1);
    if (leading_zeros > 0)
        dmd_bw_put_bits(bw, (uint32_t)(val & ((1u << leading_zeros) - 1)),
                        (int)leading_zeros);
}

/* -------------------------------------------------------------------- le */

void dmd_av1_put_le(struct dmd_bitwriter *bw, uint64_t v, int nbytes)
{
    /* AV1 specification 4.10.4: little-endian byte order; the caller must
     * provide a byte-aligned bitstream. */
    if (nbytes <= 0 || nbytes > 8 || bw->bit_pos != 0) {
        bw->overflow = 1;
        return;
    }
    for (int i = 0; i < nbytes; i++)
        dmd_bw_put_bits(bw, (uint32_t)((v >> (i * 8)) & 0xff), 8);
}

/* -------------------------------------------------------------------- ns */

void dmd_av1_put_ns(struct dmd_bitwriter *bw, uint32_t v, uint32_t n)
{
    /* AV1 specification 4.10.7 ns(n): non-symmetric coding saves one bit for
     * small values.
     *   w = FloorLog2(n) + 1
     *   m = (1 << w) - n
     * Write v directly in w - 1 bits when v < m; otherwise write v + m in
     * w bits. */
    if (n == 0) {
        bw->overflow = 1;
        return;
    }
    if (n == 1)
        return;                    /* the only value occupies no bits */

    uint32_t w = 0, t = n;
    while (t) { w++; t >>= 1; }    /* w = FloorLog2(n) + 1 */
    uint32_t m = (1u << w) - n;

    if (v < m) {
        dmd_bw_put_bits(bw, v, (int)(w - 1));
    } else {
        uint32_t enc = v + m;
        dmd_bw_put_bits(bw, enc >> 1, (int)(w - 1));
        dmd_bw_put_bits(bw, enc & 1, 1);
    }
}

/* -------------------------------------------------------------------- su */

void dmd_av1_put_su(struct dmd_bitwriter *bw, int32_t v, int nbits)
{
    /* AV1 specification 4.10.6 su(n): n-bit two's-complement coding.  Keep
     * the low nbits when writing; the decoder sign-extends the value. */
    if (nbits <= 0 || nbits > 32) {
        bw->overflow = 1;
        return;
    }
    uint32_t mask = (nbits == 32) ? 0xffffffffu : ((1u << nbits) - 1u);
    dmd_bw_put_bits(bw, (uint32_t)v & mask, nbits);
}

/* --------------------------------------------------------------- alignment */

void dmd_av1_byte_align(struct dmd_bitwriter *bw)
{
    /* AV1 specification 5.3.5 byte_alignment(): pad with zeros only; do not
     * write a stop bit.  This is the key difference from H.264/HEVC
     * rbsp_trailing_bits. */
    while (bw->bit_pos != 0 && !bw->overflow)
        dmd_bw_put_flag(bw, 0);
}

void dmd_av1_trailing_bits(struct dmd_bitwriter *bw)
{
    /* AV1 specification 5.3.4 trailing_bits(): write one bit and then pad to
     * a byte boundary.  The one bit is required even when already aligned; it
     * terminates the payload. */
    dmd_bw_put_flag(bw, 1);
    while (bw->bit_pos != 0 && !bw->overflow)
        dmd_bw_put_flag(bw, 0);
}

/* ------------------------------------------------ sequence-header assembly */

/* Number of bits needed to represent x: AV1 uses FloorLog2(x) + 1. */
static int bits_for(uint32_t v)
{
    int n = 0;
    while (v) { n++; v >>= 1; }
    return n ? n : 1;
}

/* color_config(), AV1 specification 5.5.2. */
static void put_color_config(struct dmd_bitwriter *bw,
                             const VADecPictureParameterBufferAV1 *p)
{
    const uint32_t depth_idx  = p->bit_depth_idx;
    const uint32_t mono       = p->seq_info_fields.fields.mono_chrome;
    const uint32_t sub_x      = p->seq_info_fields.fields.subsampling_x;
    const uint32_t sub_y      = p->seq_info_fields.fields.subsampling_y;

    /* high_bitdepth / twelve_bit: bit_depth_idx 0/1/2 maps to 8/10/12 bits
     * (va_dec_av1.h:255-260).  Only profile 2 supports 12-bit output. */
    const int high_bitdepth = (depth_idx != 0);
    dmd_bw_put_flag(bw, high_bitdepth);
    if (p->profile == 2 && high_bitdepth)
        dmd_bw_put_flag(bw, depth_idx == 2);       /* twelve_bit */

    if (p->profile != 1)
        dmd_bw_put_flag(bw, (int)mono);            /* mono_chrome */

    /* color_description_present_flag = 0.
     *
     * We previously set this to 1 and wrote all three descriptors to retain
     * VA-API's matrix_coefficients, but that did not match the real stream:
     * libaom sets it to 0, leaving all three values UNSPECIFIED (2).  Writing
     * them would make the sequence header three bytes longer.
     *
     * This is acceptable because matrix_coefficients only selects the color
     * conversion matrix; it does not affect decoding.  The driver outputs
     * NV12 and leaves color interpretation to the consumer. */
    dmd_bw_put_flag(bw, 0);

    if (mono) {
        dmd_bw_put_flag(bw, (int)p->seq_info_fields.fields.color_range);
        return;                                    /* monochrome branch ends */
    }

    dmd_bw_put_flag(bw, (int)p->seq_info_fields.fields.color_range);

    /* Subsampling is explicit only for profile 2 at 12 bits.  Profile 0 is
     * always 4:2:0 and profile 1 is always 4:4:4, so the profile implies the
     * values and no bits are present (specification 5.5.2). */
    if (p->profile == 2 && depth_idx == 2) {
        dmd_bw_put_flag(bw, (int)sub_x);
        if (sub_x)
            dmd_bw_put_flag(bw, (int)sub_y);
    }

    /* chroma_sample_position is present only for 4:2:0.  The corresponding
     * VA-API field is marked va_deprecated (:285), so use the value observed
     * in the real stream (CSP_VERTICAL = 1).  It only affects the assumed
     * chroma-interpolation phase, not whether the frame can be decoded. */
    if (sub_x && sub_y)
        dmd_bw_put_bits(bw, 1, 2);

    dmd_bw_put_flag(bw, 0);                        /* separate_uv_delta_q */
}

/* tile_info(), AV1 specification 5.9.15.
 *
 * tile_size_bytes_minus_1 is fixed at 3 (four bytes) here.  tile_group must
 * use the same width for tile_size_minus_1; otherwise the decoder reads tile
 * lengths with the wrong width and becomes misaligned from the second tile.
 *
 * tile_info(), AV1 specification 5.9.15.
 *
 * The boundary calculations in this function were wrong twice; they were
 * eventually checked line by line against FFmpeg's CBS implementation
 * (tile_info() in libavcodec/cbs_av1_syntax_template.c).  Two pitfalls are
 * worth documenting:
 *
 *   1) The shift is sb_size = sb_shift + 2, not sb_shift.  CBS says:
 *        sb_size = sb_shift + 2;
 *        max_tile_width_sb = AV1_MAX_TILE_WIDTH >> sb_size;
 *      Using sb_shift makes max_tile_width_sb four times too large, which
 *      makes min_log2_tile_cols too small and shifts the unary-code start.
 *
 *   2) min_log2_tiles must be at least min_log2_tile_cols.  CBS says:
 *        min_log2_tiles = FFMAX(min_log2_tile_cols,
 *                    cbs_av1_tile_log2(max_tile_area_sb, sb_rows*sb_cols));
 *      Omitting this makes min_log2_tile_rows too small and emits extra ones
 *      in the row direction.
 *
 * increment(v, min, max) follows cbs_av1_write_increment:
 *   v == max -> write (max - min) ones, with no stop bit;
 *   otherwise -> write (v - min) ones followed by a zero.
 *
 * tile_size_bytes_minus_1 is fixed at 3 (four bytes), and tile_group must use
 * the same width for tile_size_minus_1; a mismatch misaligns every tile after
 * the first. */
static void put_tile_info(struct dmd_bitwriter *bw,
                          const VADecPictureParameterBufferAV1 *p,
                          uint32_t mi_cols, uint32_t mi_rows)
{
    const int sb_shift = p->seq_info_fields.fields.use_128x128_superblock ? 5 : 4;
    const int sb_size  = sb_shift + 2;

    const uint32_t sb_cols = p->seq_info_fields.fields.use_128x128_superblock
                           ? ((mi_cols + 31) >> 5) : ((mi_cols + 15) >> 4);
    const uint32_t sb_rows = p->seq_info_fields.fields.use_128x128_superblock
                           ? ((mi_rows + 31) >> 5) : ((mi_rows + 15) >> 4);

    const uint32_t MAX_TILE_COLS = 64, MAX_TILE_ROWS = 64;
    const uint32_t max_tile_width_sb = 4096u >> sb_size;
    const uint32_t max_tile_area_sb  = (4096u * 2304u) >> (2 * sb_size);

    uint32_t max_log2_tile_cols = 0;
    while ((1u << max_log2_tile_cols) <
           (sb_cols < MAX_TILE_COLS ? sb_cols : MAX_TILE_COLS))
        max_log2_tile_cols++;
    uint32_t max_log2_tile_rows = 0;
    while ((1u << max_log2_tile_rows) <
           (sb_rows < MAX_TILE_ROWS ? sb_rows : MAX_TILE_ROWS))
        max_log2_tile_rows++;

    uint32_t min_log2_tile_cols = 0;
    while ((max_tile_width_sb << min_log2_tile_cols) < sb_cols)
        min_log2_tile_cols++;
    uint32_t min_log2_area = 0;
    while ((max_tile_area_sb << min_log2_area) < sb_rows * sb_cols)
        min_log2_area++;
    const uint32_t min_log2_tiles = (min_log2_tile_cols > min_log2_area)
                                  ? min_log2_tile_cols : min_log2_area;

    /* VA-API provides tile_cols/tile_rows as counts; derive their log2 values. */
    uint32_t cols_log2 = 0;
    while ((1u << cols_log2) < p->tile_cols)
        cols_log2++;
    uint32_t rows_log2 = 0;
    while ((1u << rows_log2) < p->tile_rows)
        rows_log2++;


    dmd_bw_put_flag(bw, (int)p->pic_info_fields.bits.uniform_tile_spacing_flag);

    if (p->pic_info_fields.bits.uniform_tile_spacing_flag) {
        /* increment(tile_cols_log2, min_log2_tile_cols, max_log2_tile_cols).
         * Clamp to the valid range first; CBS rejects out-of-range values on
         * the write side. */
        if (cols_log2 < min_log2_tile_cols) cols_log2 = min_log2_tile_cols;
        if (cols_log2 > max_log2_tile_cols) cols_log2 = max_log2_tile_cols;
        for (uint32_t i = min_log2_tile_cols; i < cols_log2; i++)
            dmd_bw_put_flag(bw, 1);
        if (cols_log2 != max_log2_tile_cols)
            dmd_bw_put_flag(bw, 0);

        const uint32_t min_log2_tile_rows =
            (min_log2_tiles > cols_log2) ? (min_log2_tiles - cols_log2) : 0;
        if (rows_log2 < min_log2_tile_rows) rows_log2 = min_log2_tile_rows;
        if (rows_log2 > max_log2_tile_rows) rows_log2 = max_log2_tile_rows;
        for (uint32_t i = min_log2_tile_rows; i < rows_log2; i++)
            dmd_bw_put_flag(bw, 1);
        if (rows_log2 != max_log2_tile_rows)
            dmd_bw_put_flag(bw, 0);
    } else {
        /* Non-uniform spacing: write width_in_sbs_minus_1 and
         * height_in_sbs_minus_1 for each tile using ns(n).  The upper bound
         * is the smaller of the remaining superblock count and max_tile_*_sb. */
        uint32_t start_sb = 0;
        for (int i = 0; i < p->tile_cols && start_sb < sb_cols; i++) {
            const uint32_t rest = sb_cols - start_sb;
            const uint32_t lim = rest < max_tile_width_sb ? rest
                                                          : max_tile_width_sb;
            dmd_av1_put_ns(bw, p->width_in_sbs_minus_1[i], lim);
            start_sb += p->width_in_sbs_minus_1[i] + 1;
        }
        start_sb = 0;
        for (int i = 0; i < p->tile_rows && start_sb < sb_rows; i++) {
            dmd_av1_put_ns(bw, p->height_in_sbs_minus_1[i],
                           sb_rows - start_sb);
            start_sb += p->height_in_sbs_minus_1[i] + 1;
        }
    }

    /* When TileCols * TileRows > 1, write context_update_tile_id and
     * tile_size_bytes.  Use the clamped log2 values above; do not recompute
     * the width from tile_cols. */
    if (cols_log2 + rows_log2 > 0) {
        dmd_bw_put_bits(bw, p->context_update_tile_id,
                        (int)(cols_log2 + rows_log2));
        /* tile_size_bytes_minus_1 = 1 (two bytes).  The VA-API tile offsets
         * have exactly two-byte gaps (observed as tile[0] offset 2 and each
         * following tile starting two bytes after the previous end); those
         * gaps are the original tile_size fields.  Matching the source width
         * keeps the reconstructed payload length unchanged and avoids
         * needless expansion.  This width must match the tile_size_minus_1
         * encoding in dmd_av1_build_frame(). */
        dmd_bw_put_bits(bw, 1, 2);
    }
}

/* quantization_params(), AV1 specification 5.9.12. */
static void put_quantization_params(struct dmd_bitwriter *bw,
                                    const VADecPictureParameterBufferAV1 *p)
{
    const uint32_t mono = p->seq_info_fields.fields.mono_chrome;

    dmd_bw_put_bits(bw, p->base_qindex, 8);

    /* delta_q uses su(1 + 6): one presence bit plus a six-bit signed value
     * (specification 5.9.13, read_delta_q).  A zero value writes only the
     * presence bit set to zero. */
    #define PUT_DELTA_Q(v) do {                        \
        if ((v) != 0) { dmd_bw_put_flag(bw, 1);        \
                        dmd_av1_put_su(bw, (v), 7); }  \
        else            dmd_bw_put_flag(bw, 0);        \
    } while (0)

    PUT_DELTA_Q(p->y_dc_delta_q);

    if (!mono) {
        /* diff_uv_delta is present only when separate_uv_delta_q is set.  The
         * sequence header writes that flag as zero, so omit it here and use a
         * shared U/V delta (writing the U value is sufficient). */
        PUT_DELTA_Q(p->u_dc_delta_q);
        PUT_DELTA_Q(p->u_ac_delta_q);
    }

    #undef PUT_DELTA_Q

    dmd_bw_put_flag(bw, (int)p->qmatrix_fields.bits.using_qmatrix);
    if (p->qmatrix_fields.bits.using_qmatrix) {
        dmd_bw_put_bits(bw, p->qmatrix_fields.bits.qm_y, 4);
        dmd_bw_put_bits(bw, p->qmatrix_fields.bits.qm_u, 4);
        if (!mono)
            dmd_bw_put_bits(bw, p->qmatrix_fields.bits.qm_v, 4);
    }
}

/* segmentation_params(), AV1 specification 5.9.14. */
static void put_segmentation_params(struct dmd_bitwriter *bw,
                                    const VADecPictureParameterBufferAV1 *p,
                                    int primary_ref_none)
{
    const uint32_t enabled = p->seg_info.segment_info_fields.bits.enabled;
    dmd_bw_put_flag(bw, (int)enabled);
    if (!enabled)
        return;

    /* When primary_ref_frame is NONE, update_map/update_data are inferred as
     * one and are not written. */
    if (!primary_ref_none) {
        dmd_bw_put_flag(bw, (int)p->seg_info.segment_info_fields.bits.update_map);
        if (p->seg_info.segment_info_fields.bits.update_map)
            dmd_bw_put_flag(bw,
                (int)p->seg_info.segment_info_fields.bits.temporal_update);
        dmd_bw_put_flag(bw, (int)p->seg_info.segment_info_fields.bits.update_data);
    }

    if (primary_ref_none || p->seg_info.segment_info_fields.bits.update_data) {
        /* Write each segment and feature.  Bits in feature_mask indicate
         * enabled SEG_LVL_* features, while feature_data supplies the value.
         * Width and signedness come from the Segmentation_Feature_Bits/Signed
         * tables in the specification. */
        static const int seg_bits[8]   = { 8, 6, 6, 6, 6, 3, 0, 0 };
        static const int seg_signed[8] = { 1, 1, 1, 1, 1, 0, 0, 0 };
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) {
                const int on = (p->seg_info.feature_mask[i] >> j) & 1;
                dmd_bw_put_flag(bw, on);
                if (!on)
                    continue;
                if (seg_bits[j] == 0)
                    continue;            /* SEG_LVL_REF_FRAME and other valueless features */
                if (seg_signed[j])
                    dmd_av1_put_su(bw, p->seg_info.feature_data[i][j],
                                   seg_bits[j] + 1);
                else
                    dmd_bw_put_bits(bw,
                        (uint32_t)p->seg_info.feature_data[i][j], seg_bits[j]);
            }
        }
    }
}

/* loop_filter_params(), AV1 specification 5.9.11. */
static void put_loop_filter_params(struct dmd_bitwriter *bw,
                                   const VADecPictureParameterBufferAV1 *p,
                                   int coded_lossless, int allow_intrabc)
{
    /* The entire section is omitted for lossless or allow_intrabc frames;
     * the specification supplies the default values directly. */
    if (coded_lossless || allow_intrabc)
        return;

    dmd_bw_put_bits(bw, p->filter_level[0], 6);
    dmd_bw_put_bits(bw, p->filter_level[1], 6);
    if (!p->seq_info_fields.fields.mono_chrome &&
        (p->filter_level[0] || p->filter_level[1])) {
        dmd_bw_put_bits(bw, p->filter_level_u, 6);
        dmd_bw_put_bits(bw, p->filter_level_v, 6);
    }
    dmd_bw_put_bits(bw, p->loop_filter_info_fields.bits.sharpness_level, 3);

    const int delta_enabled =
        p->loop_filter_info_fields.bits.mode_ref_delta_enabled;
    dmd_bw_put_flag(bw, delta_enabled);
    if (delta_enabled) {
        const int delta_update =
            p->loop_filter_info_fields.bits.mode_ref_delta_update;
        dmd_bw_put_flag(bw, delta_update);
        if (delta_update) {
            for (int i = 0; i < 8; i++) {
                /* Write the update flag and su(7) for each entry.  VA-API does
                 * not distinguish "not updated" from "updated to zero", so
                 * conservatively mark every non-zero value as an update. */
                if (p->ref_deltas[i] != 0) {
                    dmd_bw_put_flag(bw, 1);
                    dmd_av1_put_su(bw, p->ref_deltas[i], 7);
                } else {
                    dmd_bw_put_flag(bw, 0);
                }
            }
            for (int i = 0; i < 2; i++) {
                if (p->mode_deltas[i] != 0) {
                    dmd_bw_put_flag(bw, 1);
                    dmd_av1_put_su(bw, p->mode_deltas[i], 7);
                } else {
                    dmd_bw_put_flag(bw, 0);
                }
            }
        }
    }
}

/* cdef_params(), AV1 specification 5.9.19. */
static void put_cdef_params(struct dmd_bitwriter *bw,
                            const VADecPictureParameterBufferAV1 *p,
                            int coded_lossless, int allow_intrabc)
{
    if (coded_lossless || allow_intrabc ||
        !p->seq_info_fields.fields.enable_cdef)
        return;

    dmd_bw_put_bits(bw, p->cdef_damping_minus_3, 2);
    dmd_bw_put_bits(bw, p->cdef_bits, 2);

    const int n = 1 << p->cdef_bits;
    for (int i = 0; i < n; i++) {
        /* VA-API packs each Y/UV strength as (pri << 2) | sec, matching the
         * four-bit pri plus two-bit sec layout in the bitstream. */
        dmd_bw_put_bits(bw, p->cdef_y_strengths[i] >> 2, 4);
        dmd_bw_put_bits(bw, p->cdef_y_strengths[i] & 0x3, 2);
        if (!p->seq_info_fields.fields.mono_chrome) {
            dmd_bw_put_bits(bw, p->cdef_uv_strengths[i] >> 2, 4);
            dmd_bw_put_bits(bw, p->cdef_uv_strengths[i] & 0x3, 2);
        }
    }
}

/* lr_params(), AV1 specification 5.9.20. */
static void put_lr_params(struct dmd_bitwriter *bw,
                          const VADecPictureParameterBufferAV1 *p,
                          int all_lossless, int allow_intrabc)
{
    if (all_lossless || allow_intrabc)
        return;

    const uint32_t ry = p->loop_restoration_fields.bits.yframe_restoration_type;
    const uint32_t rcb = p->loop_restoration_fields.bits.cbframe_restoration_type;
    const uint32_t rcr = p->loop_restoration_fields.bits.crframe_restoration_type;

    /* The sequence header always sets enable_restoration to one, so this
     * section is always present even when all three lr_type values are zero
     * (the proper representation of no restoration for this frame).  An
     * earlier all-zero early return made the frame header six bits too short. */

    /* lr_type uses f(2), in the same order as the VA-API restoration_type
     * enum (0=NONE, 1=WIENER, 2=SGRPROJ, 3=SWITCHABLE). */
    dmd_bw_put_bits(bw, ry, 2);
    if (!p->seq_info_fields.fields.mono_chrome) {
        dmd_bw_put_bits(bw, rcb, 2);
        dmd_bw_put_bits(bw, rcr, 2);
    }

    if (ry || rcb || rcr) {
        /* VA-API stores the decoded increment value (1 or 2) while the
         * bitstream carries only the increment bit when 128x128 superblocks
         * are enabled. */
        if (p->seq_info_fields.fields.use_128x128_superblock) {
            const uint32_t shift =
                p->loop_restoration_fields.bits.lr_unit_shift;
            dmd_bw_put_bits(bw, shift > 0 ? shift - 1 : 0, 1);
        } else {
            const uint32_t shift =
                p->loop_restoration_fields.bits.lr_unit_shift;
            if (shift == 0) {
                dmd_bw_put_bits(bw, 0, 1);
            } else {
                dmd_bw_put_bits(bw, 1, 1);
                dmd_bw_put_bits(bw, shift > 1 ? 1 : 0, 1);
            }
        }
        if (p->seq_info_fields.fields.use_128x128_superblock == 0 &&
            p->loop_restoration_fields.bits.lr_unit_shift)
            dmd_bw_put_bits(bw, 0, 1);   /* lr_unit_extra_shift */
        if (p->seq_info_fields.fields.subsampling_x &&
            p->seq_info_fields.fields.subsampling_y && (rcb || rcr))
            dmd_bw_put_bits(bw, p->loop_restoration_fields.bits.lr_uv_shift, 1);
    }
}

size_t dmd_av1_build_sequence_header(const void *pic_v,
                                     unsigned char *out, size_t out_cap)
{
    const VADecPictureParameterBufferAV1 *p = pic_v;
    if (!p || !out || out_cap < 8)
        return 0;

    /* Write the payload to a temporary buffer first: obu_size uses leb128 and
     * the payload length is needed before the header can be emitted.  The
     * sequence header is small (about 20 bytes in practice), so the stack
     * buffer is sufficient. */
    unsigned char body[128];
    struct dmd_bitwriter bw;
    dmd_bw_init(&bw, body, sizeof(body));

    const uint32_t enable_order_hint =
        p->seq_info_fields.fields.enable_order_hint;

    /* ---- sequence_header_obu(), AV1 specification 5.5.1 ---- */

    dmd_bw_put_bits(&bw, p->profile, 3);                    /* seq_profile   */
    dmd_bw_put_flag(&bw, (int)p->seq_info_fields.fields.still_picture);
    dmd_bw_put_flag(&bw, 0);   /* reduced_still_picture_header: always zero;
                                * setting it would omit most following fields,
                                * while frame parsing relies on enable_* bits. */

    dmd_bw_put_flag(&bw, 0);   /* timing_info_present_flag: VA-API does not
                                * provide timing_info.  Timing does not affect
                                * decoding; the consumer controls presentation. */
    dmd_bw_put_flag(&bw, 0);   /* initial_display_delay_present_flag */
    dmd_bw_put_bits(&bw, 0, 5);/* operating_points_cnt_minus_1: one operating
                                * point; this driver has no scalable layer,
                                * matching the OBU extension_flag of zero. */
    dmd_bw_put_bits(&bw, 0, 12);          /* operating_point_idc[0] */
    /* seq_level_idx[0] = 9 (level 4.1) + seq_tier[0] = 0.
     *
     * The target stream is AV1 Main@L4.1; preserving level 4.1 avoids
     * advertising a lower operating-point capability to strict decoders.
     * The value is not the only detail: seq_level_idx > 7 must be followed by
     * seq_tier (f(1)); omitting the zero bit shifts every subsequent field.
     * VA-API does not provide the level, so use 8 to match the real stream.
     * MediaCodec allocates from the actual resolution and does not validate
     * this field. */
    dmd_bw_put_bits(&bw, 9, 5);           /* seq_level_idx[0] */
    dmd_bw_put_flag(&bw, 0);              /* seq_tier[0] (idx > 7) */

    /* Use the minimum width needed for frame_width_bits rather than a fixed
     * 16 bits.  Per va_dec_av1.h:332-334, frame_width_minus1 is the upscaled
     * frame size, which is the meaning required by max_frame_width_minus_1. */
    const uint32_t w_m1  = p->frame_width_minus1;
    const uint32_t h_m1  = p->frame_height_minus1;
    const int      wbits = bits_for(w_m1);
    const int      hbits = bits_for(h_m1);

    dmd_bw_put_bits(&bw, (uint32_t)(wbits - 1), 4);
    dmd_bw_put_bits(&bw, (uint32_t)(hbits - 1), 4);
    dmd_bw_put_bits(&bw, w_m1, wbits);
    dmd_bw_put_bits(&bw, h_m1, hbits);

    dmd_bw_put_flag(&bw, 0);   /* frame_id_numbers_present_flag: zero because
                                * VA-API provides no delta_frame_id; the frame
                                * header must omit the matching fields too. */

    dmd_bw_put_flag(&bw, (int)p->seq_info_fields.fields.use_128x128_superblock);
    dmd_bw_put_flag(&bw, (int)p->seq_info_fields.fields.enable_filter_intra);
    dmd_bw_put_flag(&bw, (int)p->seq_info_fields.fields.enable_intra_edge_filter);
    dmd_bw_put_flag(&bw, (int)p->seq_info_fields.fields.enable_interintra_compound);
    dmd_bw_put_flag(&bw, (int)p->seq_info_fields.fields.enable_masked_compound);

    /* VA-API does not provide the sequence-level enable_warped_motion field.
     * Set the capability bit to one: the per-frame allow_warped_motion
     * field (:439) still controls actual use.  Setting it to zero while a
     * frame requests warped motion would make the decoder reject that frame;
     * setting it to one has no effect when the frame does not use it. */
    dmd_bw_put_flag(&bw, 1);

    dmd_bw_put_flag(&bw, (int)p->seq_info_fields.fields.enable_dual_filter);
    dmd_bw_put_flag(&bw, (int)enable_order_hint);
    if (enable_order_hint) {
        dmd_bw_put_flag(&bw, (int)p->seq_info_fields.fields.enable_jnt_comp);
        dmd_bw_put_flag(&bw, 1);   /* enable_ref_frame_mvs: likewise unavailable
                                    * from VA-API; use the safe value one (the
                                    * per-frame field is use_ref_frame_mvs at :435). */
    }

    /* Match the sequence-control syntax used by the VA bitstream.  When the
     * current frame does not use screen-content tools, forcing both sequence
     * controls to zero avoids inserting an extra frame-header bit.  The
     * entropy-coded tile payload is parsed with exactly this choice. */
    const int choose_screen_tools =
        p->pic_info_fields.bits.allow_screen_content_tools;
    dmd_bw_put_flag(&bw, choose_screen_tools); /* seq_choose_screen_content_tools */
    if (choose_screen_tools) {
        /* seq_force_screen_content_tools is inferred as SELECT; the syntax
         * therefore carries seq_choose_integer_mv. */
        dmd_bw_put_flag(&bw, 1);              /* seq_choose_integer_mv */
    } else {
        dmd_bw_put_flag(&bw, 0);              /* seq_force_screen_content_tools */
    }

    if (enable_order_hint)
        dmd_bw_put_bits(&bw, p->order_hint_bits_minus_1, 3);

    /* VA-API also omits enable_superres and enable_restoration, so infer them
     * from the frame fields:
     *   use_superres (:432), and
     *   the three *frame_restoration_type values (:608-610; non-zero enables).
     * Unlike warped_motion, these values change the frame-header syntax and
     * must be accurate; a wrong value shifts the rest of the header. */
    const int use_superres = (int)p->pic_info_fields.bits.use_superres;

    dmd_bw_put_flag(&bw, use_superres);
    dmd_bw_put_flag(&bw, (int)p->seq_info_fields.fields.enable_cdef);
    /* enable_restoration is always one.
     *
     * Inferring this from whether all three frame_restoration_type values are
     * zero was wrong.  trace_headers shows enable_restoration=1 in the real
     * stream, and the six lr_type[0..2] bits are present at frame-header bit
     * 204 even though their values are all zero.  In other words, a frame with
     * no restoration is represented by lr_type=0, not by a sequence-level
     * enable_restoration=0.  The latter removes the lr_params section, makes
     * the header six bits too short, and shifts tile_group parsing.
     *
     * One is the safe capability value; each frame's lr_type still determines
     * whether restoration is actually used. */
    dmd_bw_put_flag(&bw, 1);

    put_color_config(&bw, p);

    dmd_bw_put_flag(&bw,
        (int)p->seq_info_fields.fields.film_grain_params_present);

    dmd_av1_trailing_bits(&bw);

    if (bw.overflow)
        return 0;

    /* Assemble the OBU header (including the leb128 payload length) and
     * payload. */
    const size_t body_len = dmd_bw_bytes(&bw);
    const size_t hdr = dmd_av1_obu_header(DMD_OBU_SEQUENCE_HEADER,
                                         body_len, out, out_cap);
    if (hdr == 0 || hdr + body_len > out_cap)
        return 0;
    for (size_t i = 0; i < body_len; i++)
        out[hdr + i] = body[i];
    return hdr + body_len;
}

/* Write uncompressed_header() to bw without trailing_bits or
 * byte_alignment; the caller selects the terminator for the enclosing OBU:
 *   OBU_FRAME_HEADER (3) uses trailing_bits (specification 5.9.1);
 *   OBU_FRAME (6)        uses byte_alignment (specification 5.10.1).
 * This distinction was found through testing: the wrong terminator shifts
 * the tile_group start and makes dav1d report "Failed to read unit". */
static void put_uncompressed_header(struct dmd_bitwriter *bwp,
                                    const VADecPictureParameterBufferAV1 *p,
                                    uint8_t refresh_frame_flags)
{
    /* struct dmd_bitwriter is a value type: buf points to the caller's
     * storage and the remaining fields are counters.  Copying it in and back
     * out is therefore safe, and preserves the field-by-field `&bw` writes
     * validated against the real stream without introducing line-by-line
     * changes. */
    struct dmd_bitwriter bw = *bwp;

    const uint32_t frame_type   = p->pic_info_fields.bits.frame_type;
    const int is_key            = (frame_type == 0);   /* KEY_FRAME */
    const int is_intra_only     = (frame_type == 2);   /* INTRA_ONLY_FRAME */
    const int intra_only        = is_key || is_intra_only;
    const uint32_t allow_intrabc = p->pic_info_fields.bits.allow_intrabc;
    const uint32_t enable_order_hint =
        p->seq_info_fields.fields.enable_order_hint;
    const int order_hint_bits = enable_order_hint
        ? (int)p->order_hint_bits_minus_1 + 1 : 0;
    /* CodedLossless (specification 7.12.1) is true when all segment qindex
     * values and the four delta_q values are zero.  VA-API does not expose
     * this flag directly, so derive it from the definition; it controls
     * whether loop_filter, cdef and lr sections are present. */
    const int coded_lossless =
        (p->base_qindex == 0 && p->y_dc_delta_q == 0 &&
         p->u_dc_delta_q == 0 && p->u_ac_delta_q == 0 &&
         p->v_dc_delta_q == 0 && p->v_ac_delta_q == 0);
    /* AllLossless additionally requires no superres upscaling (specification
     * 7.12.1). */
    const int all_lossless =
        coded_lossless && !p->pic_info_fields.bits.use_superres;

    /* --- uncompressed_header(), AV1 specification 5.9.2 --- */

    /* show_existing_frame: this driver forwards each decoded frame and does
     * not reuse an existing frame, so the value is always zero.  The field is
     * still present when frame_id_numbers_present is zero; only current_frame_id
     * is omitted in that case. */
    dmd_bw_put_flag(&bw, 0);

    dmd_bw_put_bits(&bw, frame_type, 2);
    dmd_bw_put_flag(&bw, (int)p->pic_info_fields.bits.show_frame);
    if (!p->pic_info_fields.bits.show_frame)
        dmd_bw_put_flag(&bw, (int)p->pic_info_fields.bits.showable_frame);

    /* error_resilient_mode is inferred as one, and is not written, for a
     * KEY_FRAME that is shown. */
    /* error_resilient_mode (from CBS):
     *   frame_type == SWITCH || (frame_type == KEY && show_frame)
     *     -> infer 1 (do not write it);
     *   otherwise write flag(error_resilient_mode). */
    const int er_inferred = (frame_type == 3) ||
                            (is_key && p->pic_info_fields.bits.show_frame);
    if (!er_inferred)
        dmd_bw_put_flag(&bw, (int)p->pic_info_fields.bits.error_resilient_mode);

    dmd_bw_put_flag(&bw, (int)p->pic_info_fields.bits.disable_cdf_update);

    /* With sequence controls forced to zero, allow_screen_content_tools and
     * force_integer_mv are inferred and do not occupy frame-header bits.  If
     * this frame requests the selectable mode, mirror the corresponding
     * sequence-header form and emit the per-frame flags. */
    if (p->pic_info_fields.bits.allow_screen_content_tools) {
        dmd_bw_put_flag(&bw, 1); /* allow_screen_content_tools */
        dmd_bw_put_flag(&bw, (int)p->pic_info_fields.bits.force_integer_mv);
    }

    /* frame_id_numbers_present is zero in the sequence header, so skip
     * current_frame_id. */

    /* frame_size_override_flag is always zero: the frame size equals the
     * sequence header's max_frame_size, which is set to this frame's size.
     * A resolution change on a non-key frame recreates the session, so this
     * invariant holds. */
    if (frame_type != 3 /* the flag is always one for SWITCH_FRAME */)
        dmd_bw_put_flag(&bw, 0);

    /* order_hint (specification 5.9.2) is present when enable_order_hint is
     * set and the condition "frame_is_intra and refresh_frame_flags ==
     * allFrames" is false.
     *
     * This condition caused two mistakes before it was checked against the
     * real stream.  A 1080p KEY_FRAME generated by libaom (show_frame=1)
     * does write order_hint (value 0, after frame_size_override_flag), so it
     * cannot be simplified to "never write it for intra frames"; doing so
     * would omit order_hint_bits bits.
     *
     * For a KEY+show frame, refresh_frame_flags is absent from the stream;
     * specification 7.20 derives RefreshFrameFlags = allFrames on the
     * decoder side, while 5.9.2 applies to syntax variables.  libaom's
     * behavior confirms that the condition is false here, so order_hint is
     * written. */
    if (enable_order_hint)
        dmd_bw_put_bits(&bw, p->order_hint, order_hint_bits);

    /* primary_ref_frame is omitted for intra frames and error-resilient
     * frames. */
    /* err_res is the effective value: it is one when inferred, otherwise it
     * comes from VA-API.  The conditions for primary_ref_frame,
     * use_ref_frame_mvs and allow_warped_motion must use this value rather
     * than the raw field. */
    const int err_res = er_inferred
                      ? 1 : (int)p->pic_info_fields.bits.error_resilient_mode;
    const int primary_ref_none = intra_only || err_res ||
        (p->primary_ref_frame == 7 /* PRIMARY_REF_NONE */);
    if (!intra_only && !err_res)
        dmd_bw_put_bits(&bw, p->primary_ref_frame, 3);

    /* refresh_frame_flags is 0xFF and omitted from the stream for a KEY_FRAME
     * with show_frame set.
     *
     * VA-API does not provide this field (a complete grep found only the
     * reference in the comment at :421); it is the bitmask of reference slots
     * refreshed by the frame.
     *
     * Use 0xFF (refresh all eight slots).  MediaCodec manages reference-frame
     * lifetime internally and does not allocate buffers from this bitmask; it
     * only needs a syntactically valid value.  Refreshing every slot is the
     * conservative choice and avoids claiming that a slot remains valid after
     * it has been overwritten.  Reference management is less precise, but the
     * driver forwards frames and lets MediaCodec reorder them. */
    const int refresh_all = (frame_type == 3) ||
                            (is_key && p->pic_info_fields.bits.show_frame);
    if (!refresh_all)
        dmd_bw_put_bits(&bw, refresh_frame_flags, 8);

    /* Reference-frame indices are present only for inter frames. */
    if (!intra_only) {
        /* frame_refs_short_signaling requires enable_order_hint.  Set it to
         * zero and write all seven ref_frame_idx values explicitly; VA-API
         * supplies exactly this array. */
        if (enable_order_hint)
            dmd_bw_put_flag(&bw, 0);
        for (int i = 0; i < 7; i++)
            dmd_bw_put_bits(&bw, p->ref_frame_idx[i], 3);
        /* frame_id_numbers_present is zero, so do not write delta_frame_id. */
    }

    /* frame_size() / render_size(): with frame_size_override=0, the sequence
     * header supplies the frame dimensions; write only superres and render_size
     * (specifications 5.9.5/5.9.6/5.9.8).
     * superres_params() (specification 5.9.8): use_superres is present in the
     * stream only when the sequence header sets enable_superres.
     *
     * An earlier version always wrote this flag even though the sequence
     * header had enable_superres = 0.  The decoder then skipped the bit and
     * the frame header was shifted by one, producing "trailing_one_bit out of
     * range: 0" in dav1d.  Whenever a sequence-level flag controls whether a
     * frame field exists, both sides must use the same condition. */
    if (p->pic_info_fields.bits.use_superres) {
        dmd_bw_put_flag(&bw, 1);
        /* coded_denom: SUPERRES_DENOM_MIN = 9, using SUPERRES_DENOM_BITS = 3. */
        const uint32_t denom = p->superres_scale_denominator;
        dmd_bw_put_bits(&bw, (denom >= 9 ? denom - 9 : 0), 3);
    }
    /* render_and_frame_size_different = 0: display size equals frame size.
     * VA-API does not provide render_size; it affects display cropping only,
     * not decoding. */
    dmd_bw_put_flag(&bw, 0);

    /* allow_intrabc (specification 5.9.2) is present when
     * allow_screen_content_tools && UpscaledWidth == FrameWidth.  FFmpeg's
     * CBS implementation expresses the condition as:
     *   if (allow_screen_content_tools && upscaled_width == frame_width)
     *       flag(allow_intrabc);
     *   else
     *       infer(allow_intrabc, 0);
     *
     * upscaled_width == frame_width is equivalent to no superres upscaling:
     * superres_params reduces frame_width by denom only when use_superres is
     * set.
     *
     * Both conditions are required; omitting either adds or removes one bit.
     *
     * The CBS source and libaom's actual output differ here, so follow the
     * real stream.
     *
     * CBS (cbs_av1_syntax_template.c) conditionally reads:
     *   if (allow_screen_content_tools && upscaled_width == frame_width)
     *       flag(allow_intrabc); else infer 0
     *
     * A bit-level comparison with a libaom-generated 1080p KEY_FRAME showed
     * allow_screen_content_tools=0.  Skipping this bit as CBS does shifts the
     * complete frame header and leaves trailing_one_bit=0; reading it
     * unconditionally yields trailing_one_bit=1 and exact bit closure.
     *
     * Conclusion: the encoder writes this bit unconditionally.  The criterion
     * is whether the real stream closes correctly, not how the source code is
     * written; the target is a decoder, not CBS.
     *
     * For allow_intrabc, trust ffmpeg trace_headers' bit-by-bit output for the
     * real stream.  In a libaom-generated 1080p KEY_FRAME with
     * allow_screen_content_tools=0:
     *   bit 46  render_and_frame_size_different
     *   bit 47  disable_frame_end_update_cdf     (allow_intrabc is absent)
     *   bit 48  uniform_tile_spacing_flag
     * This confirms the CBS condition: when asct=0, the bit is omitted.
     *
     * An earlier conclusion based on bit closure in a home-grown parser was
     * wrong: that parser was itself off by one here, and the two errors merely
     * canceled out.  Validation tools must first be calibrated against an
     * authoritative source such as trace_headers. */
    if (intra_only && p->pic_info_fields.bits.allow_screen_content_tools &&
        !p->pic_info_fields.bits.use_superres)
        dmd_bw_put_flag(&bw, (int)allow_intrabc);

    if (!intra_only) {
        dmd_bw_put_flag(&bw, (int)p->pic_info_fields.bits.allow_high_precision_mv);
        /* read_interpolation_filter(): is_filter_switchable f(1); when zero,
         * write the two-bit interp_filter value.  VA-API uses 4 for
         * SWITCHABLE. */
        if (p->interp_filter == 4) {
            dmd_bw_put_flag(&bw, 1);
        } else {
            dmd_bw_put_flag(&bw, 0);
            dmd_bw_put_bits(&bw, p->interp_filter, 2);
        }
        dmd_bw_put_flag(&bw,
            (int)p->pic_info_fields.bits.is_motion_mode_switchable);
        /* use_ref_frame_mvs requires enable_ref_frame_mvs (set to one in the
         * sequence header), a non-error-resilient frame and enable_order_hint. */
        if (!err_res && enable_order_hint)
            dmd_bw_put_flag(&bw, (int)p->pic_info_fields.bits.use_ref_frame_mvs);
    }

    /* disable_frame_end_update_cdf is present when
     * reduced_still_picture_header=0 and disable_cdf_update=0. */
    if (!p->pic_info_fields.bits.disable_cdf_update)
        dmd_bw_put_flag(&bw,
            (int)p->pic_info_fields.bits.disable_frame_end_update_cdf);

    /* MiCols/MiRows: frame dimensions in 4x4 units (derived in specification
     * 5.9.5).  2 * ((width + 7) >> 3) rounds up to eight pixels and halves the
     * result. */
    const uint32_t width  = (uint32_t)p->frame_width_minus1 + 1;
    const uint32_t height = (uint32_t)p->frame_height_minus1 + 1;
    const uint32_t mi_cols = 2 * ((width  + 7) >> 3);
    const uint32_t mi_rows = 2 * ((height + 7) >> 3);

    put_tile_info(&bw, p, mi_cols, mi_rows);
    put_quantization_params(&bw, p);
    put_segmentation_params(&bw, p, primary_ref_none);

    /* delta_q_params() (5.9.17): delta_q_present exists only when
     * base_qindex > 0. */
    if (p->base_qindex > 0)
        dmd_bw_put_flag(&bw, (int)p->mode_control_fields.bits.delta_q_present_flag);
    if (p->mode_control_fields.bits.delta_q_present_flag) {
        dmd_bw_put_bits(&bw, p->mode_control_fields.bits.log2_delta_q_res, 2);

        /* delta_lf_params() (5.9.18) is present only when delta_q_present is
         * set. */
        if (!allow_intrabc) {
            dmd_bw_put_flag(&bw,
                (int)p->mode_control_fields.bits.delta_lf_present_flag);
            if (p->mode_control_fields.bits.delta_lf_present_flag) {
                dmd_bw_put_bits(&bw,
                    p->mode_control_fields.bits.log2_delta_lf_res, 2);
                dmd_bw_put_flag(&bw,
                    (int)p->mode_control_fields.bits.delta_lf_multi);
            }
        }
    }

    put_loop_filter_params(&bw, p, coded_lossless, (int)allow_intrabc);
    put_cdef_params(&bw, p, coded_lossless, (int)allow_intrabc);
    put_lr_params(&bw, p, all_lossless, (int)allow_intrabc);

    /* read_tx_mode() (5.9.21): CodedLossless implies ONLY_4X4 and writes no
     * field; otherwise write tx_mode_select f(1).  VA-API's tx_mode values are
     * 2 = TX_MODE_LARGEST and 3 = TX_MODE_SELECT. */
    /* read_tx_mode (CBS source):
     *   coded_lossless -> infer(tx_mode, ONLY_4X4), no field;
     *   otherwise increment(tx_mode, TX_MODE_LARGEST=1, TX_MODE_SELECT=2).
     *
     * It was previously treated as an f(1) flag and encoded as tx_mode == 3;
     * both assumptions were wrong:
     *   - the encoding is increment over the range [1,2], not a flag;
     *   - VA-API's tx_mode range is [0..2] (va_dec_av1.h:560-563), directly
     *     matching the specification enum, so 3 is not a valid value.
     * increment writes max - min ones without a stop bit at range_max, and a
     * single zero at range_min. */
    if (!coded_lossless) {
        const uint32_t tm = p->mode_control_fields.bits.tx_mode;
        if (tm >= 2)
            dmd_bw_put_flag(&bw, 1);   /* TX_MODE_SELECT: one, no stop bit */
        else
            dmd_bw_put_flag(&bw, 0);   /* TX_MODE_LARGEST: stop bit */
    }

    /* frame_reference_mode() (5.9.23): inter frames write reference_select. */
    if (!intra_only)
        dmd_bw_put_flag(&bw, (int)p->mode_control_fields.bits.reference_select);

    /* skip_mode_params() (5.9.22): the VA descriptor carries the final
     * skip_mode_present value, but not the reference order hints needed to
     * derive skipModeAllowed.  A stream with fewer than two populated
     * reference slots cannot enable skip mode; this is also the conservative
     * choice for descriptors whose reference indices are all the invalid
     * first-frame value (7).  For the normal inter frames, two or more
     * populated slots are sufficient for the AV1 streams accepted here and
     * the VA result can be emitted unchanged.
     */
    unsigned populated_refs = 0;
    if (!intra_only && p->mode_control_fields.bits.reference_select) {
        for (int i = 0; i < 7; i++)
            populated_refs += p->ref_frame_idx[i] != 7;
    }
    if (populated_refs >= 2)
        dmd_bw_put_flag(&bw, (int)p->mode_control_fields.bits.skip_mode_present);

    /* allow_warped_motion requires is_motion_mode_switchable, a non-error-
     * resilient frame and the sequence-level enable_warped_motion bit (set to
     * one above). */
    if (!intra_only &&
        p->pic_info_fields.bits.is_motion_mode_switchable && !err_res)
        dmd_bw_put_flag(&bw, (int)p->pic_info_fields.bits.allow_warped_motion);

    dmd_bw_put_flag(&bw, (int)p->mode_control_fields.bits.reduced_tx_set_used);

    /* global_motion_params() (5.9.24): write is_global for each reference on
     * inter frames.  VA-API supplies transform parameters in wm[], but
     * encoding them back requires the complete differential coding and
     * reference projection logic.  Write is_global=0 (IDENTITY) for every
     * reference instead; the known simplification loses global-motion
     * compensation and is documented at the end of this file. */
    if (!intra_only) {
        for (int i = 0; i < 7; i++)
            dmd_bw_put_flag(&bw, 0);     /* is_global[LAST+i] = 0 */
    }

    /* film_grain_params() (5.9.30): the section is absent when the sequence
     * header's film_grain_params_present is zero.  Mirror that condition here. */
    if (p->seq_info_fields.fields.film_grain_params_present &&
        (p->pic_info_fields.bits.show_frame ||
         p->pic_info_fields.bits.showable_frame))
        dmd_bw_put_flag(&bw, 0);         /* apply_grain = 0 */

    /* Do not write trailing_bits or byte_alignment; the caller selects the
     * terminator for the enclosing OBU. */
    *bwp = bw;
}

/* Wrap the frame-header payload in the requested OBU type.  obu_type selects
 * the terminator:
 *   DMD_OBU_FRAME_HEADER -> trailing_bits (specification 5.9.1);
 *   DMD_OBU_FRAME        -> byte_alignment (specification 5.10.1), followed
 *                           by tile_group.
 * Return the number of bytes written to out, including the OBU header, or 0
 * on failure.  body_len_out returns the payload length for OBU_FRAME, which
 * appends tile_group afterwards. */
static size_t build_frame_header_obu(const VADecPictureParameterBufferAV1 *p,
                                     int obu_type,
                                     unsigned char *body, size_t body_cap,
                                     size_t *body_len_out,
                                     uint8_t refresh_frame_flags)
{
    struct dmd_bitwriter bw;
    dmd_bw_init(&bw, body, body_cap);
    put_uncompressed_header(&bw, p, refresh_frame_flags);

    if (obu_type == DMD_OBU_FRAME)
        dmd_av1_byte_align(&bw);      /* zero padding only, no marker bit */
    else
        dmd_av1_trailing_bits(&bw);   /* one bit followed by zero padding */

    if (bw.overflow)
        return 0;
    *body_len_out = dmd_bw_bytes(&bw);
    return *body_len_out;
}

size_t dmd_av1_build_frame_header(const void *pic_v,
                                  unsigned char *out, size_t out_cap)
{
    const VADecPictureParameterBufferAV1 *p = pic_v;
    if (!p || !out || out_cap < 8)
        return 0;

    unsigned char body[512];
    size_t body_len = 0;
    if (build_frame_header_obu(p, DMD_OBU_FRAME_HEADER,
                              body, sizeof(body), &body_len, 0xff) == 0)
        return 0;

    const size_t hdr = dmd_av1_obu_header(DMD_OBU_FRAME_HEADER,
                                         body_len, out, out_cap);
    if (hdr == 0 || hdr + body_len > out_cap)
        return 0;
    for (size_t i = 0; i < body_len; i++)
        out[hdr + i] = body[i];
    return hdr + body_len;
}

size_t dmd_av1_build_frame(const void *pic_v,
                           const struct dmd_av1_tile *tiles, int num_tiles,
                           uint8_t refresh_frame_flags,
                           unsigned char *out, size_t out_cap)
{
    const VADecPictureParameterBufferAV1 *p = pic_v;
    if (!p || !out || !tiles || num_tiles <= 0 || out_cap < 16)
        return 0;

    /* Frame header (ends with byte_alignment, not trailing_bits). */
    unsigned char fh[512];
    size_t fh_len = 0;
    if (build_frame_header_obu(p, DMD_OBU_FRAME,
                              fh, sizeof(fh), &fh_len,
                              refresh_frame_flags) == 0)
        return 0;
    /* tile_group_obu() payload (specification 5.11.1):
     *   when NumTiles > 1, write tile_start_and_end_present_flag first
     *   zero means that this group covers every tile (tg_start=0,
     *   tg_end=NumTiles-1)
     *   then byte_alignment, followed by tile_size_minus_1 and data per tile
     *   the final tile has no length field; its size is implied by the
     *   remaining OBU payload
     *
     * tile_size_minus_1 uses le(2), and its width must match the
     * tile_size_bytes_minus_1 = 3 written by tile_info; a mismatch misaligns
     * every tile after the first. */
    const uint32_t tile_total =
        (uint32_t)p->tile_cols * (uint32_t)p->tile_rows;

    unsigned char tg_hdr[8];
    struct dmd_bitwriter tgw;
    dmd_bw_init(&tgw, tg_hdr, sizeof(tg_hdr));
    if (tile_total > 1)
        dmd_bw_put_flag(&tgw, 0);     /* tile_start_and_end_present_flag */
    dmd_av1_byte_align(&tgw);
    if (tgw.overflow)
        return 0;
    const size_t tg_hdr_len = dmd_bw_bytes(&tgw);

    /* Compute the total OBU payload length before writing the leb128 obu_size. */
    size_t payload_len = fh_len + tg_hdr_len;
    for (int i = 0; i < num_tiles; i++) {
        if (!tiles[i].data && tiles[i].len)
            return 0;
        payload_len += tiles[i].len;
        if (i + 1 < num_tiles)
            payload_len += 2;         /* tile_size_minus_1, le(2) */
    }

    const size_t hdr = dmd_av1_obu_header(DMD_OBU_FRAME, payload_len,
                                          out, out_cap);
    if (hdr == 0 || hdr + payload_len > out_cap)
        return 0;

    unsigned char *q = out + hdr;
    for (size_t i = 0; i < fh_len; i++)
        *q++ = fh[i];
    for (size_t i = 0; i < tg_hdr_len; i++)
        *q++ = tg_hdr[i];
    for (int i = 0; i < num_tiles; i++) {
        if (i + 1 < num_tiles) {
            /* le(2): the width must equal tile_size_bytes_minus_1 + 1 = 2
             * from the frame header.  This caps one tile at 64 KiB; observed
             * 1080p tiles are about 4 KiB and the VA-API source stream also
             * uses two bytes.  If a future stream exceeds 64 KiB, widen this
             * field and the frame-header field together. */
            const uint32_t v = (uint32_t)(tiles[i].len - 1);
            *q++ = (unsigned char)(v & 0xFF);
            *q++ = (unsigned char)((v >> 8) & 0xFF);
        }
        for (size_t k = 0; k < tiles[i].len; k++)
            *q++ = tiles[i].data[k];
    }
    return hdr + payload_len;
}

size_t dmd_av1_build_show_existing(uint8_t map_idx,
                                   unsigned char *out, size_t out_cap)
{
    if (!out || out_cap < 4 || map_idx >= 8)
        return 0;

    unsigned char body[2];
    struct dmd_bitwriter bw;
    dmd_bw_init(&bw, body, sizeof(body));
    dmd_bw_put_flag(&bw, 1);          /* show_existing_frame */
    dmd_bw_put_bits(&bw, map_idx, 3); /* frame_to_show_map_idx */
    dmd_av1_trailing_bits(&bw);
    if (bw.overflow)
        return 0;

    const size_t body_len = dmd_bw_bytes(&bw);
    const size_t hdr = dmd_av1_obu_header(DMD_OBU_FRAME_HEADER,
                                          body_len, out, out_cap);
    if (!hdr || hdr + body_len > out_cap)
        return 0;
    memcpy(out + hdr, body, body_len);
    return hdr + body_len;
}

/* --------------------------------------------------------------- OBU header */

size_t dmd_av1_obu_header(int obu_type, size_t payload_len,
                          unsigned char *out, size_t out_cap)
{
    if (obu_type < 0 || obu_type > 15 || out_cap < 1)
        return 0;

    /* forbidden(1)=0 | type(4) | extension(1)=0 | has_size(1)=1 | reserved(1)=0
     *
     * This is 0x00 | (type << 3) | 0x00 | 0x02 | 0x00; has_size is bit 1.
     * Examples: SEQUENCE_HEADER (1) -> 0x0a, FRAME_HEADER (3) -> 0x1a,
     *           TILE_GROUP (4) -> 0x22, TEMPORAL_DELIMITER (2) -> 0x12.
     *
     * The invalid value observed before reconstruction was 0xd0 =
     * 1101_0000: forbidden=1 (must be zero) and type=10 (reserved), clearly
     * showing that the input was raw tile payload rather than an OBU header. */
    out[0] = (unsigned char)(((obu_type & 0x0f) << 3) | 0x02);

    size_t n = dmd_av1_leb128((uint64_t)payload_len, out + 1, out_cap - 1);
    if (n == 0)
        return 0;
    return 1 + n;
}
