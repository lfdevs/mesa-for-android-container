/* Copyright 2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include <string.h>
#include "common.h"
#include "vpe_priv.h"
#include "vpe20_opp.h"
#include "hw_shared.h"
#include "reg_helper.h"
#include "custom_fp16.h"

#define CTX_BASE opp
#define CTX      vpe20_opp

static struct opp_funcs opp20_funcs = {
    .program_pipe_control        = vpe20_opp_program_pipe_control,
    .program_pipe_crc            = vpe20_opp_program_pipe_crc,
    .set_clamping                = vpe10_opp_set_clamping,
    .program_bit_depth_reduction = vpe20_opp_program_bit_depth_reduction,
    .set_dyn_expansion           = vpe10_opp_set_dyn_expansion,
    .program_fmt                 = vpe10_opp_program_fmt,
    .program_fmt_control         = vpe20_opp_program_fmt_control,
    .build_fmt_subsample_params  = vpe20_opp_build_fmt_subsample_params,
    .set_bg                      = vpe20_opp_set_bg,
    .program_frod                = vpe20_opp_program_frod,
    .get_fmt_extra_pixel         = vpe20_opp_get_fmt_extra_pixel,
};

enum fmt_pixel {
    PIXEL_ENCODING_RGB444_YCBCR444 = 0,
    PIXEL_ENCODING_YCBCR422 = 1,
    PIXEL_ENCODING_YCBCR420 = 2
};

enum fmt_taps {
    TAPS2 = 0,
    TAPS3 = 1,
    TAPS4 = 2,
    TAPS5 = 3
};

void vpe20_construct_opp(struct vpe_priv *vpe_priv, struct opp *opp)
{
    opp->vpe_priv = vpe_priv;
    opp->funcs    = &opp20_funcs;
}

void vpe20_opp_program_pipe_control(struct opp *opp, const struct opp_pipe_control_params *params)
{
    PROGRAM_ENTRY();
    REG_SET_3(VPOPP_PIPE_CONTROL, REG_DEFAULT(VPOPP_PIPE_CONTROL), VPOPP_PIPE_ALPHA, params->alpha,
        VPOPP_PIPE_DIGITAL_BYPASS_EN, params->bypass_enable, VPOPP_PIPE_ALPHA_SEL,
        params->alpha_select);
}

void vpe20_opp_program_pipe_crc(struct opp *opp, bool enable)
{
    PROGRAM_ENTRY();
    // REG_SET(VPOPP_PIPE_CRC_CONTROL, REG_DEFAULT(VPOPP_PIPE_CRC_CONTROL), VPOPP_PIPE_CRC_EN,
    // enable);
}

static void get_fmt_chroma_taps(enum vpe_surface_pixel_format format,
    enum subsampling_quality subsample_quality, enum chroma_cositing cositing,
    struct chroma_taps *ctaps)
{
    memset(ctaps, 0, sizeof(*ctaps));

    if (vpe_is_yuv420(format)) {
        ctaps->v_taps_c = 2;
        ctaps->h_taps_c = 2;
        switch (cositing) {
        case CHROMA_COSITING_LEFT:
            ctaps->h_taps_c = 3;
            break;
        case CHROMA_COSITING_TOPLEFT:
            ctaps->v_taps_c = 3;
            ctaps->h_taps_c = 3;
            break;
        default:
            break;
        }
        if (subsample_quality == SUBSAMPLING_QUALITY_HIGH) {
            ctaps->v_taps_c += 2;
            ctaps->h_taps_c += 2;
        }
    } else if (vpe_is_yuv422(format)) {
        if (subsample_quality == SUBSAMPLING_QUALITY_HIGH)
            ctaps->h_taps_c = 5;
        else
            ctaps->h_taps_c = 3;
    }
}

void vpe20_opp_build_fmt_subsample_params(struct opp *opp, enum vpe_surface_pixel_format format,
    enum subsampling_quality subsample_quality, enum chroma_cositing cositing,
    struct fmt_boundary_mode boundary_mode, struct fmt_subsampling_params *subsample_params)
{
    PROGRAM_ENTRY();

    struct chroma_taps ctaps;

    uint32_t pixel_encoding = PIXEL_ENCODING_RGB444_YCBCR444;
    uint32_t bit_reduction_bypass = 0;
    uint32_t vtaps = 0;
    uint32_t htaps = 0;

    if (!subsample_params)
        return;

    get_fmt_chroma_taps(format, subsample_quality, cositing, &ctaps);

    if (vpe_is_yuv420(format)) {
        pixel_encoding = PIXEL_ENCODING_YCBCR420;
        bit_reduction_bypass = 1;
    } else if (vpe_is_yuv422(format)) {
        pixel_encoding = PIXEL_ENCODING_YCBCR422;
        bit_reduction_bypass = 1;
    } else {
        pixel_encoding = PIXEL_ENCODING_RGB444_YCBCR444;
    }

    switch (ctaps.h_taps_c) {
    case 2:
        htaps = TAPS2;
        break;
    case 3:
        htaps = TAPS3;
        break;
    case 4:
        htaps = TAPS4;
        break;
    case 5:
        htaps = TAPS5;
        break;
    default:
        break;
    }

    switch (ctaps.v_taps_c) {
    case 2:
        vtaps = TAPS2;
        break;
    case 3:
        vtaps = TAPS3;
        break;
    case 4:
        vtaps = TAPS4;
        break;
    case 5:
        vtaps = TAPS5;
        break;
    default:
        break;
    }

    subsample_params->pixel_encoding       = pixel_encoding;
    subsample_params->bit_reduction_bypass = bit_reduction_bypass;
    subsample_params->htaps                = htaps;
    subsample_params->vtaps                = vtaps;
    subsample_params->boundary_mode        = boundary_mode;
}

void vpe20_opp_program_fmt_control(struct opp *opp, struct fmt_control_params *fmt_ctrl)
{
    PROGRAM_ENTRY();

    REG_SET_10(VPFMT_CONTROL, REG_DEFAULT(VPFMT_CONTROL), VPFMT_SPATIAL_DITHER_FRAME_COUNTER_MAX,
        fmt_ctrl->fmt_spatial_dither_frame_counter_max, VPFMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP,
        fmt_ctrl->fmt_spatial_dither_frame_counter_bit_swap, VPFMT_PIXEL_ENCODING,
        fmt_ctrl->subsampling_params.pixel_encoding, VPFMT_CBCR_BIT_REDUCTION_BYPASS,
        fmt_ctrl->subsampling_params.bit_reduction_bypass, VPFMT_SUBSAMPLE_HTAPS,
        fmt_ctrl->subsampling_params.htaps, VPFMT_SUBSAMPLE_VTAPS,
        fmt_ctrl->subsampling_params.vtaps, VPFMT_SUBSAMPLE_BOTTOM_EDGE,
        fmt_ctrl->subsampling_params.boundary_mode.bottom, VPFMT_SUBSAMPLE_TOP_EDGE,
        fmt_ctrl->subsampling_params.boundary_mode.top, VPFMT_SUBSAMPLE_RIGHT_EDGE,
        fmt_ctrl->subsampling_params.boundary_mode.right, VPFMT_SUBSAMPLE_LEFT_EDGE,
        fmt_ctrl->subsampling_params.boundary_mode.left);
}

void vpe20_opp_set_bg(struct opp* opp, struct vpe_rect target_rect, struct vpe_rect dst_rect,
    enum vpe_surface_pixel_format format, struct vpe_color bgcolor)
{
    PROGRAM_ENTRY();

    uint32_t top_lines, bot_lines, left_lines, right_lines;
    uint16_t r_cr, g_y, b_cb;
    top_lines   = dst_rect.y >= target_rect.y ? dst_rect.y - target_rect.y : 0;
    bot_lines   = (target_rect.y + target_rect.height) >= (dst_rect.y + dst_rect.height)
                      ? (target_rect.y + target_rect.height) - (dst_rect.y + dst_rect.height)
                      : 0;
    left_lines  = dst_rect.x >= target_rect.x ? dst_rect.x - target_rect.x : 0;
    right_lines = (target_rect.x + target_rect.width) >= (dst_rect.x + dst_rect.width)
                      ? (target_rect.x + target_rect.width) - (dst_rect.x + dst_rect.width)
                      : 0;

    if (vpe_is_fp16(format)) {
        vpe_convert_from_float_to_fp16(bgcolor.rgba.r, &r_cr);
        vpe_convert_from_float_to_fp16(bgcolor.rgba.g, &g_y);
        vpe_convert_from_float_to_fp16(bgcolor.rgba.b, &b_cb);
    } else if (bgcolor.is_ycbcr) {
        g_y = (uint16_t)(bgcolor.ycbcra.y * 0xffff);
        r_cr = (uint16_t)(bgcolor.ycbcra.cr * 0xffff);
        b_cb = (uint16_t)(bgcolor.ycbcra.cb * 0xffff);
    } else {
        r_cr = (uint16_t)(bgcolor.rgba.r * 0xffff);
        g_y = (uint16_t)(bgcolor.rgba.g * 0xffff);
        b_cb = (uint16_t)(bgcolor.rgba.b * 0xffff);
    }

    if (vpe_is_yuv420(format)) {
        if (top_lines % 2 != 0)
            top_lines += 1;
        if (bot_lines % 2 != 0)
            bot_lines += 1;
        if (left_lines % 2 != 0)
            left_lines += 1;
        if (right_lines % 2 != 0)
            right_lines += 1;
    } else if (vpe_is_yuv422(format)) {
        if (left_lines % 2 != 0)
            left_lines += 1;
        if (right_lines % 2 != 0)
            right_lines += 1;
    }

    REG_SET_2(VPOPP_PIPE_OUTBG_EXT1, 0, OUTBG_EXT_TOP, top_lines, OUTBG_EXT_BOT, bot_lines);
    REG_SET_2(VPOPP_PIPE_OUTBG_EXT2, 0, OUTBG_EXT_LEFT, left_lines, OUTBG_EXT_RIGHT, right_lines);

    REG_SET_2(VPOPP_PIPE_OUTBG_COL1, 0, OUTBG_R_CR, r_cr, OUTBG_B_CB, b_cb);
    REG_SET(VPOPP_PIPE_OUTBG_COL2, 0, OUTBG_Y, g_y);
}

void vpe20_opp_program_frod(struct opp *opp, struct opp_frod_param *frod_param)
{
    PROGRAM_ENTRY();
    REG_SET(VPOPP_FROD_CONTROL, 0, FROD_EN, frod_param->enable_frod);
}

void vpe20_opp_get_fmt_extra_pixel(enum vpe_surface_pixel_format format,
    enum subsampling_quality subsample_quality, enum chroma_cositing cositing,
    struct fmt_extra_pixel_info *extra_pixel)
{
    (void)format;
    (void)subsample_quality;
    (void)cositing;

    extra_pixel->left_pixels   = 2;
    extra_pixel->right_pixels  = 1;
    extra_pixel->top_pixels    = 2;
    extra_pixel->bottom_pixels = 1;
}

void vpe20_opp_program_bit_depth_reduction(
    struct opp *opp, const struct bit_depth_reduction_params *params)
{
    PROGRAM_ENTRY();

    if (params->flags.SPATIAL_DITHER_ENABLED == 0) {
        /*Disable spatial (random) dithering*/
        REG_SET_9(VPFMT_BIT_DEPTH_CONTROL, REG_DEFAULT(VPFMT_BIT_DEPTH_CONTROL), VPFMT_TRUNCATE_EN,
            params->flags.TRUNCATE_ENABLED, VPFMT_TRUNCATE_DEPTH, params->flags.TRUNCATE_DEPTH,
            VPFMT_TRUNCATE_MODE, params->flags.TRUNCATE_MODE, VPFMT_SPATIAL_DITHER_EN, 0,
            VPFMT_SPATIAL_DITHER_MODE, 0, VPFMT_SPATIAL_DITHER_DEPTH, 0,
            VPFMT_HIGHPASS_RANDOM_ENABLE, 0, VPFMT_FRAME_RANDOM_ENABLE, 0, VPFMT_RGB_RANDOM_ENABLE,
            0);

        return;
    }

    /* Set seed for random values for
     * spatial dithering for R,G,B channels
     */
    REG_SET(VPFMT_DITHER_RAND_R_SEED, 0, VPFMT_RAND_R_SEED, params->r_seed_value);

    REG_SET(VPFMT_DITHER_RAND_G_SEED, 0, VPFMT_RAND_G_SEED, params->g_seed_value);

    REG_SET(VPFMT_DITHER_RAND_B_SEED, 0, VPFMT_RAND_B_SEED, params->b_seed_value);

    /* FMT_OFFSET_R_Cr  31:16 0x0 Setting the zero
     * offset for the R/Cr channel, lower 4LSB
     * is forced to zeros. Typically set to 0
     * RGB and 0x80000 YCbCr.
     */
    /* FMT_OFFSET_G_Y   31:16 0x0 Setting the zero
     * offset for the G/Y  channel, lower 4LSB is
     * forced to zeros. Typically set to 0 RGB
     * and 0x80000 YCbCr.
     */
    /* FMT_OFFSET_B_Cb  31:16 0x0 Setting the zero
     * offset for the B/Cb channel, lower 4LSB is
     * forced to zeros. Typically set to 0 RGB and
     * 0x80000 YCbCr.
     */

    REG_SET_9(VPFMT_BIT_DEPTH_CONTROL, REG_DEFAULT(VPFMT_BIT_DEPTH_CONTROL), VPFMT_TRUNCATE_EN,
        params->flags.TRUNCATE_ENABLED, VPFMT_TRUNCATE_DEPTH, params->flags.TRUNCATE_DEPTH,
        VPFMT_TRUNCATE_MODE, params->flags.TRUNCATE_MODE,
        /*Enable spatial dithering*/
        VPFMT_SPATIAL_DITHER_EN, params->flags.SPATIAL_DITHER_ENABLED,
        /* Set spatial dithering mode
         * (default is Seed patterrn AAAA...)
         */
        VPFMT_SPATIAL_DITHER_MODE, params->flags.SPATIAL_DITHER_MODE,
        /*Set spatial dithering bit depth*/
        VPFMT_SPATIAL_DITHER_DEPTH, params->flags.SPATIAL_DITHER_DEPTH,
        /*Disable High pass filter*/
        VPFMT_HIGHPASS_RANDOM_ENABLE, params->flags.HIGHPASS_RANDOM,
        /*Reset only at startup*/
        VPFMT_FRAME_RANDOM_ENABLE, params->flags.FRAME_RANDOM,
        /*Set RGB data dithered with x^28+x^3+1*/
        VPFMT_RGB_RANDOM_ENABLE, params->flags.RGB_RANDOM);
}

