/* Copyright 2024 Advanced Micro Devices, Inc.
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

#include "vpe_priv.h"
#include "reg_helper.h"
#include "vpe10/inc/vpe10_cm_common.h"
#include "vpe20/inc/vpe20_cm_common.h"
#include "vpe10_dpp.h"
#include "vpe20_dpp.h"
#include "custom_float.h"
#include "fixed31_32.h"
#include "conversion.h"

#define CTX      vpe20_dpp
#define CTX_BASE dpp


enum histo_xbar {
    select_R_Cr_Max = 0,
    select_G_Luma,
    select_B_Cb_Min
};

#define HIST_SRC1_SET_MAX  1
#define HIST_SRC2_SET_LUMA 1
#define HIST_SRC3_SET_MIN  1

void vpe20_dpp_program_input_transfer_func(struct dpp *dpp, struct transfer_func *input_tf)
{
    struct pwl_params *params = NULL;

    PROGRAM_ENTRY();

    struct stream_ctx *stream_ctx = &vpe_priv->stream_ctx[vpe_priv->fe_cb_ctx.stream_idx];
    bool               bypass;

    VPE_ASSERT(input_tf);
    // There should always have input_tf
    // Only accept either DISTRIBUTED_POINTS or BYPASS
    // No support for PREDEFINED case
    VPE_ASSERT(input_tf->type == TF_TYPE_DISTRIBUTED_POINTS || input_tf->type == TF_TYPE_BYPASS);

    // VPE always do NL scaling using gamcor, thus skipping dgam (default bypass)
    // dpp->funcs->program_pre_dgam(dpp, tf);
    if (input_tf->type == TF_TYPE_DISTRIBUTED_POINTS) {
        vpe10_cm_helper_translate_curve_to_degamma_hw_format(input_tf, &dpp->degamma_params, input_tf->dirty[dpp->inst]);
        params = &dpp->degamma_params;
    }

    bypass = ((input_tf->type == TF_TYPE_BYPASS) || dpp->vpe_priv->init.debug.bypass_gamcor);

    CONFIG_CACHE(input_tf, stream_ctx, vpe_priv->init.debug.disable_lut_caching, bypass,
        vpe10_dpp_program_gamcor_lut(dpp, params), dpp->inst);
}

static bool calculate_hist_rgb_luma_coeffs(uint32_t * rgbcoff_reg, enum color_space cs)
{
    struct fixed31_32 rgb_luma_coeffs[3]        = { 0 };
    struct custom_float_format const vpefmt     = { 12, 6, false };
    switch (cs) {
        case COLOR_SPACE_RGB601:
        case COLOR_SPACE_RGB601_LIMITED:
            rgb_luma_coeffs[0] = vpe_convfix31_32(vpe_output_full_csc_matrix_fixed[1].regval[4]);
            rgb_luma_coeffs[1] = vpe_convfix31_32(vpe_output_full_csc_matrix_fixed[1].regval[5]);
            rgb_luma_coeffs[2] = vpe_convfix31_32(vpe_output_full_csc_matrix_fixed[1].regval[6]);
            break;
        case COLOR_SPACE_SRGB:
            rgb_luma_coeffs[0] = vpe_convfix31_32(vpe_output_full_csc_matrix_fixed[2].regval[4]);
            rgb_luma_coeffs[1] = vpe_convfix31_32(vpe_output_full_csc_matrix_fixed[2].regval[5]);
            rgb_luma_coeffs[2] = vpe_convfix31_32(vpe_output_full_csc_matrix_fixed[2].regval[6]);
            break;
        case COLOR_SPACE_2020_RGB_FULLRANGE:
        case COLOR_SPACE_2020_RGB_LIMITEDRANGE:
            rgb_luma_coeffs[0] = vpe_convfix31_32(vpe_output_full_csc_matrix_fixed[3].regval[4]);
            rgb_luma_coeffs[1] = vpe_convfix31_32(vpe_output_full_csc_matrix_fixed[3].regval[5]);
            rgb_luma_coeffs[2] = vpe_convfix31_32(vpe_output_full_csc_matrix_fixed[3].regval[6]);
            break;
        default:
            return false;
            break;
    }
    if( (vpe_convert_to_custom_float_format(rgb_luma_coeffs[0], &vpefmt, &rgbcoff_reg[0])) &&
        (vpe_convert_to_custom_float_format(rgb_luma_coeffs[1], &vpefmt, &rgbcoff_reg[1])) &&
        (vpe_convert_to_custom_float_format(rgb_luma_coeffs[2], &vpefmt, &rgbcoff_reg[2]))) {
        return true;
    }
    return false;
}

void vpe20_dpp_program_histo(struct dpp* dpp, struct vpe_histogram_param* hist_param, enum color_space cs)
{
    uint32_t hist_chan_mask = 0;
    uint32_t hist_chan_en   = 0;
    uint32_t reg_xbar[hist_max_channel]     = { 0 };
    uint32_t reg_src_sel[hist_max_channel]  = { 0 };    
    uint32_t rgbcoff_reg[3]                 = { 0 };
    const uint32_t tapPoint = 0;
    enum hist_channels hist_crt_channel;
    bool rgb_luma_transform = false;

    PROGRAM_ENTRY();

    for (hist_crt_channel = hist_channel1; hist_crt_channel < hist_max_channel; hist_crt_channel++) {
        if (hist_param->hist_collection_param[hist_crt_channel].hist_types != VPE_HISTOGRAM_NONE) {
            hist_chan_mask |= 1 << hist_crt_channel;
            hist_chan_en++;
            switch (hist_param->hist_collection_param[hist_crt_channel].hist_types) {
            case VPE_HISTOGRAM_MAX_RGB_YCbCr:
                reg_xbar[hist_crt_channel] = select_R_Cr_Max;
                reg_src_sel[hist_channel1] = HIST_SRC1_SET_MAX;
                break;
            case VPE_HISTOGRAM_G_Y:
                reg_xbar[hist_crt_channel] = select_G_Luma;
                break;
            case VPE_HISTOGRAM_RGB_TRANSFORMED_Y:
                reg_xbar[hist_crt_channel] = select_G_Luma;
                reg_src_sel[hist_channel2] = HIST_SRC2_SET_LUMA;
                rgb_luma_transform = true;
                break;
            case VPE_HISTOGRAM_B_CB:
                reg_xbar[hist_crt_channel] = select_B_Cb_Min;
                break;
            case VPE_HISTOGRAM_MIN_RGB_YCbCr:
                reg_xbar[hist_crt_channel] = select_B_Cb_Min;
                reg_src_sel[hist_channel3] = HIST_SRC3_SET_MIN;
                break;
            default:
                break;
            }
        }
    }

    if ( (rgb_luma_transform) &&
         (calculate_hist_rgb_luma_coeffs(rgbcoff_reg, cs))) {
            REG_SET(VPCM_HIST_COEFA_SRC2, REG_DEFAULT(VPCM_HIST_COEFA_SRC2), VPCM_HIST_COEFA_SRC2, rgbcoff_reg[0]);
            REG_SET(VPCM_HIST_COEFB_SRC2, REG_DEFAULT(VPCM_HIST_COEFB_SRC2), VPCM_HIST_COEFB_SRC2, rgbcoff_reg[1]);
            REG_SET(VPCM_HIST_COEFC_SRC2, REG_DEFAULT(VPCM_HIST_COEFC_SRC2), VPCM_HIST_COEFC_SRC2, rgbcoff_reg[2]);
    }    

    REG_SET_10(VPCM_HIST_CNTL, REG_DEFAULT(VPCM_HIST_CNTL),
        VPCM_HIST_SEL,          (uint32_t)tapPoint,
        VPCM_HIST_CH_EN,        (uint32_t)hist_chan_en,
        VPCM_HIST_CH1_XBAR,     (uint32_t)reg_xbar[hist_channel1],
        VPCM_HIST_CH2_XBAR,     (uint32_t)reg_xbar[hist_channel2],
        VPCM_HIST_CH3_XBAR,     (uint32_t)reg_xbar[hist_channel3],
        VPCM_HIST_FORMAT,       (uint32_t)hist_param->hist_format,
        VPCM_HIST_SRC1_SEL,     (uint32_t)reg_src_sel[hist_channel1],
        VPCM_HIST_SRC2_SEL,     (uint32_t)reg_src_sel[hist_channel2],
        VPCM_HIST_SRC3_SEL,     (uint32_t)reg_src_sel[hist_channel3],
        VPCM_HIST_READ_CHANNEL_MASK, (uint32_t)hist_chan_mask);

}
