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
#include "vpe10_dpp.h"
#include "vpe20_dpp.h"
#include "color.h"
#include "hw_shared.h"
#include "reg_helper.h"

#define CTX_BASE dpp
#define CTX      vpe20_dpp

static struct dpp_funcs vpe20_dpp_funcs = {
    .enable_clocks = vpe20_dpp_enable_clocks,

    // cnv
    .program_cnv            = vpe20_dpp_program_cnv,
    .program_pre_dgam       = vpe10_dpp_cnv_program_pre_dgam,
    .program_cnv_bias_scale = vpe10_dpp_program_cnv_bias_scale,
    .build_keyer_params     = vpe10_dpp_build_keyer_params,
    .program_alpha_keyer    = vpe20_dpp_cnv_program_alpha_keyer,
    .program_crc            = vpe10_dpp_program_crc,

    // cm
    .program_input_transfer_func = vpe20_dpp_program_input_transfer_func,
    .program_gamut_remap         = NULL,
    .program_post_csc   = vpe10_dpp_program_post_csc,
    .set_hdr_multiplier = vpe10_dpp_set_hdr_multiplier,
    .program_histogram  = vpe20_dpp_program_histo,
    // scaler
    .get_optimal_number_of_taps  = vpe10_dpp_get_optimal_number_of_taps,
    .dscl_calc_lb_num_partitions = vpe10_dscl_calc_lb_num_partitions,
    .set_segment_scaler          = vpe20_dpp_set_segment_scaler,
    .dscl_set_scaler_position    = vpe20_dpp_dscl_set_scaler_position,
    .set_frame_scaler            = vpe20_dpp_set_frame_scaler,
    .get_line_buffer_size        = vpe10_get_line_buffer_size,
    .validate_number_of_taps     = vpe10_dpp_validate_number_of_taps,

    .dscl_program_easf   = vpe20_dscl_program_easf,
    .dscl_disable_easf   = vpe20_dscl_disable_easf,
    .dscl_program_isharp = vpe20_dscl_program_isharp,
};

void vpe20_construct_dpp(struct vpe_priv *vpe_priv, struct dpp *dpp)
{
    dpp->vpe_priv = vpe_priv;
    dpp->funcs    = &vpe20_dpp_funcs;
}

/* Not used as we do not have special 2bit LUT currently
 * Can skip for optimize performance and use default val
 */
static void vpe20_dpp_program_alpha_2bit_lut(
    struct dpp *dpp, struct cnv_alpha_2bit_lut *alpha_2bit_lut)
{
    PROGRAM_ENTRY();

    if (alpha_2bit_lut != NULL) {
        REG_SET_2(VPCNVC_ALPHA_2BIT_LUT01, 0, ALPHA_2BIT_LUT0, alpha_2bit_lut->lut0,
            ALPHA_2BIT_LUT1, alpha_2bit_lut->lut1);
        REG_SET_2(VPCNVC_ALPHA_2BIT_LUT23, 0, ALPHA_2BIT_LUT2, alpha_2bit_lut->lut2,
            ALPHA_2BIT_LUT3, alpha_2bit_lut->lut3);
    } else { // restore to default
        REG_SET_DEFAULT(VPCNVC_ALPHA_2BIT_LUT01);
        REG_SET_DEFAULT(VPCNVC_ALPHA_2BIT_LUT23);
    }
}

void vpe20_dpp_enable_clocks(struct dpp *dpp, bool enable)
{
    PROGRAM_ENTRY();

    REG_SET(VPDPP_CONTROL, REG_DEFAULT(VPDPP_CONTROL), VPECLK_G_GATE_DISABLE, enable);
}

void vpe20_dpp_cnv_program_alpha_keyer(struct dpp *dpp, const struct cnv_keyer_params *keyer_params)
{
    PROGRAM_ENTRY();

    if (keyer_params->keyer_en) {
        uint8_t keyer_mode = 0;

        switch (keyer_params->keyer_mode) {
        case VPE_KEYER_MODE_FORCE_00:
            keyer_mode = 0;
            break;
        case VPE_KEYER_MODE_FORCE_FF:
            keyer_mode = 1;
            break;
        case VPE_KEYER_MODE_RANGE_FF:
            keyer_mode = 2;
            break;
        case VPE_KEYER_MODE_RANGE_00:
        default:
            keyer_mode = 3;
            break;
        }

        if (!keyer_params->is_color_key) { // Luma Keying
            REG_SET_3(VPCNVC_COLOR_KEYER_CONTROL, 0, COLOR_KEYER_EN, 0, LUMA_KEYER_EN, 1,
                COLOR_KEYER_MODE, keyer_mode);
            REG_SET_2(VPCNVC_COLOR_KEYER_GREEN, 0, COLOR_KEYER_GREEN_LOW,
                keyer_params->luma_keyer.lower_luma_bound, COLOR_KEYER_GREEN_HIGH,
                keyer_params->luma_keyer.upper_luma_bound);
        } else { // Color Keying
            REG_SET_3(VPCNVC_COLOR_KEYER_CONTROL, 0, COLOR_KEYER_EN, 1, LUMA_KEYER_EN, 0,
                COLOR_KEYER_MODE, keyer_mode);
            REG_SET_2(VPCNVC_COLOR_KEYER_GREEN, 0, COLOR_KEYER_GREEN_LOW,
                keyer_params->color_keyer.color_keyer_green_low, COLOR_KEYER_GREEN_HIGH,
                keyer_params->color_keyer.color_keyer_green_high);
            REG_SET_2(VPCNVC_COLOR_KEYER_BLUE, 0, COLOR_KEYER_BLUE_LOW,
                keyer_params->color_keyer.color_keyer_blue_low, COLOR_KEYER_BLUE_HIGH,
                keyer_params->color_keyer.color_keyer_blue_high);
            REG_SET_2(VPCNVC_COLOR_KEYER_RED, 0, COLOR_KEYER_RED_LOW,
                keyer_params->color_keyer.color_keyer_red_low, COLOR_KEYER_RED_HIGH,
                keyer_params->color_keyer.color_keyer_red_high);
            REG_SET_2(VPCNVC_COLOR_KEYER_ALPHA, 0, COLOR_KEYER_ALPHA_LOW,
                keyer_params->color_keyer.color_keyer_alpha_low, COLOR_KEYER_ALPHA_HIGH,
                keyer_params->color_keyer.color_keyer_alpha_high);
        }
    } else {
        REG_SET_DEFAULT(VPCNVC_COLOR_KEYER_CONTROL);
    }
}

void vpe20_dpp_program_cnv(
    struct dpp *dpp, enum vpe_surface_pixel_format format, enum vpe_expansion_mode mode)
{
    uint32_t alpha_en          = 1;
    uint32_t pixel_format      = 0;
    uint32_t hw_expansion_mode = 0;

    PROGRAM_ENTRY();

    switch (mode) {
    case VPE_EXPANSION_MODE_DYNAMIC:
        hw_expansion_mode = 0;
        break;
    case VPE_EXPANSION_MODE_ZERO:
        hw_expansion_mode = 1;
        break;
    default:
        VPE_ASSERT(0);
        break;
    }

    switch (format) {
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_XRGB8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_XBGR8888:
        pixel_format = 8;
        alpha_en = 0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
        pixel_format = 8;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBX8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRX8888:
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_8bpc_RGB:
        pixel_format = 9;
        alpha_en     = 0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA8888:
        pixel_format = 9;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
        pixel_format = 10;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA1010102:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA1010102:
        pixel_format = 11;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCrCb:
        pixel_format = 64;
        alpha_en     = 0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCbCr:
        pixel_format = 65;
        alpha_en     = 0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCrCb:
        pixel_format = 66;
        alpha_en     = 0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCbCr:
        pixel_format = 67;
        alpha_en     = 0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616: /* use crossbar */
        pixel_format = 20;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_RGB:
        pixel_format = 21;
        alpha_en     = 0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA16161616:
        pixel_format = 21;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_RGB_FLOAT:
        pixel_format = 24;
        alpha_en     = 0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F: /* FP16          */
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F: /* used crossbar */
        pixel_format = 24;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA16161616F: /* FP16          */
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616F: /* used crossbar */
        pixel_format = 25;
        break;

    // VPE 2.0 Supported Modes
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_AYCrCb8888:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_AYCbCr8888: /* used crossbar */
        pixel_format = 12;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_8bpc_YCbCr:
        pixel_format = 13;
        alpha_en     = 0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_YCrCbA8888:
        pixel_format = 13;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb8888:
        pixel_format = 14;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA8888:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrCbYA8888:
        pixel_format = 15;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616_UNORM:
        pixel_format = 26;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616_UNORM:
        pixel_format = 27;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616_SNORM:
        pixel_format = 28;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616_SNORM:
        pixel_format = 29;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_YCbCr:
        pixel_format = 42;
        alpha_en     = 0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb12121212: /* Y416 */
        pixel_format = 44;                              /* 12 bit slice MSB */
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA12121212:
        pixel_format = 46;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_12bpc_YCrCb: /* P016 */
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCrCb: /* P216 */
        pixel_format = 68;
        alpha_en     = 0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_12bpc_YCbCr: /* P016 */
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCbCr: /* P216 */
        pixel_format = 69;
        alpha_en     = 0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ALPHA_THRU_LUMA:
        pixel_format = 70;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCrYCb: /* YUY12         */
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCbYCr: /* used crossbar */
        pixel_format = 72;
        alpha_en     = 0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_CrYCbY: /* YUY12         */
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_CbYCrY: /* used crossbar */
        pixel_format = 74;
        alpha_en     = 0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCrYCb: /* Y210          */
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCbYCr: /* used crossbar */
        pixel_format = 76;
        alpha_en     = 0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_CrYCbY: /* Y210          */
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_CbYCrY: /* used crossbar */
        pixel_format = 78;
        alpha_en     = 0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCrYCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCbYCr: /* used crossbar */
        pixel_format = 80;
        alpha_en     = 0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_CrYCbY: /* Y216          */
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_CbYCrY: // use crossbar
        pixel_format = 82;
        alpha_en     = 0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb2101010: /* Y410 */
        pixel_format = 114;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA1010102: /* Y410 */
        pixel_format = 115;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_R8: // use crossbar
        pixel_format = 120;
        alpha_en     = 0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_R16: // use crossbar
        pixel_format = 125;
        alpha_en     = 0;
        break;
    default:
        break;
    }

    REG_SET(VPCNVC_SURFACE_PIXEL_FORMAT, 0, VPCNVC_SURFACE_PIXEL_FORMAT, pixel_format);
    REG_SET_7(VPCNVC_FORMAT_CONTROL, REG_DEFAULT(VPCNVC_FORMAT_CONTROL), FORMAT_EXPANSION_MODE,
        hw_expansion_mode, FORMAT_CNV16, 0, FORMAT_CONTROL__ALPHA_EN, alpha_en, VPCNVC_BYPASS,
        dpp->vpe_priv->init.debug.vpcnvc_bypass, VPCNVC_BYPASS_MSB_ALIGN, 0, CLAMP_POSITIVE, 0,
        CLAMP_POSITIVE_C, 0);
}

