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
#include "vpe20_dpp.h"

#define CTX      vpe20_dpp
#define CTX_BASE dpp

#define LB_MAX_PARTITION  12
#define NUM_PHASES        64
#define NUM_ISHARP_LEVELS 32

enum vpe10_dscl_mode_sel vpe20_dpp_dscl_get_dscl_mode(const struct scaler_data *data)
{
    return (enum vpe10_dscl_mode_sel)data->dscl_prog_data.dscl_mode;
}

/**
 * vpe20_dscl_program_easf - Program EASF
 *
 * This is the primary function to program EASF
 *
 */
void vpe20_dscl_program_easf(struct dpp *dpp, const struct scaler_data *scl_data)
{
    PROGRAM_ENTRY();

    REG_SET_2(VPDSCL_SC_MODE, REG_DEFAULT(VPDSCL_SC_MODE),
        SCL_SC_MATRIX_MODE, scl_data->dscl_prog_data.easf_matrix_mode,
        SCL_SC_LTONL_EN, scl_data->dscl_prog_data.easf_ltonl_en);
    /* DSCL_EASF_V_MODE */
    REG_SET_3(VPDSCL_EASF_V_MODE, REG_DEFAULT(VPDSCL_EASF_V_MODE),
        SCL_EASF_V_EN, scl_data->dscl_prog_data.easf_v_en,
        SCL_EASF_V_2TAP_SHARP_FACTOR, scl_data->dscl_prog_data.easf_v_sharp_factor,
        SCL_EASF_V_RINGEST_FORCE_EN, scl_data->dscl_prog_data.easf_v_ring);
    REG_SET_6(VPDSCL_EASF_V_BF_CNTL, REG_DEFAULT(VPDSCL_EASF_V_BF_CNTL),
        SCL_EASF_V_BF1_EN, scl_data->dscl_prog_data.easf_v_bf1_en,
        SCL_EASF_V_BF2_MODE, scl_data->dscl_prog_data.easf_v_bf2_mode,
        SCL_EASF_V_BF3_MODE, scl_data->dscl_prog_data.easf_v_bf3_mode,
        SCL_EASF_V_BF2_FLAT1_GAIN, scl_data->dscl_prog_data.easf_v_bf2_flat1_gain,
        SCL_EASF_V_BF2_FLAT2_GAIN, scl_data->dscl_prog_data.easf_v_bf2_flat2_gain,
        SCL_EASF_V_BF2_ROC_GAIN, scl_data->dscl_prog_data.easf_v_bf2_roc_gain);
    REG_SET_2(VPDSCL_EASF_V_RINGEST_3TAP_CNTL1, REG_DEFAULT(VPDSCL_EASF_V_RINGEST_3TAP_CNTL1),
        SCL_EASF_V_RINGEST_3TAP_DNTILT_UPTILT, scl_data->dscl_prog_data.easf_v_ringest_3tap_dntilt_uptilt,
        SCL_EASF_V_RINGEST_3TAP_UPTILT_MAXVAL, scl_data->dscl_prog_data.easf_v_ringest_3tap_uptilt_max);
    REG_SET_2(VPDSCL_EASF_V_RINGEST_3TAP_CNTL2, REG_DEFAULT(VPDSCL_EASF_V_RINGEST_3TAP_CNTL2),
        SCL_EASF_V_RINGEST_3TAP_DNTILT_SLOPE, scl_data->dscl_prog_data.easf_v_ringest_3tap_dntilt_slope,
        SCL_EASF_V_RINGEST_3TAP_UPTILT1_SLOPE, scl_data->dscl_prog_data.easf_v_ringest_3tap_uptilt1_slope);
    REG_SET_2(VPDSCL_EASF_V_RINGEST_3TAP_CNTL3, REG_DEFAULT(VPDSCL_EASF_V_RINGEST_3TAP_CNTL3),
        SCL_EASF_V_RINGEST_3TAP_UPTILT2_SLOPE, scl_data->dscl_prog_data.easf_v_ringest_3tap_uptilt2_slope,
        SCL_EASF_V_RINGEST_3TAP_UPTILT2_OFFSET, scl_data->dscl_prog_data.easf_v_ringest_3tap_uptilt2_offset);
    REG_SET_2(VPDSCL_EASF_V_RINGEST_EVENTAP_REDUCE, REG_DEFAULT(VPDSCL_EASF_V_RINGEST_EVENTAP_REDUCE),
        SCL_EASF_V_RINGEST_EVENTAP_REDUCEG1, scl_data->dscl_prog_data.easf_v_ringest_eventap_reduceg1,
        SCL_EASF_V_RINGEST_EVENTAP_REDUCEG2, scl_data->dscl_prog_data.easf_v_ringest_eventap_reduceg2);
    REG_SET_2(VPDSCL_EASF_V_RINGEST_EVENTAP_GAIN, REG_DEFAULT(VPDSCL_EASF_V_RINGEST_EVENTAP_GAIN),
        SCL_EASF_V_RINGEST_EVENTAP_GAIN1, scl_data->dscl_prog_data.easf_v_ringest_eventap_gain1,
        SCL_EASF_V_RINGEST_EVENTAP_GAIN2, scl_data->dscl_prog_data.easf_v_ringest_eventap_gain2);
    REG_SET_4(VPDSCL_EASF_V_BF_FINAL_MAX_MIN, REG_DEFAULT(VPDSCL_EASF_V_BF_FINAL_MAX_MIN),
        SCL_EASF_V_BF_MAXA, scl_data->dscl_prog_data.easf_v_bf_maxa,
        SCL_EASF_V_BF_MAXB, scl_data->dscl_prog_data.easf_v_bf_maxb,
        SCL_EASF_V_BF_MINA, scl_data->dscl_prog_data.easf_v_bf_mina,
        SCL_EASF_V_BF_MINB, scl_data->dscl_prog_data.easf_v_bf_minb);
    REG_SET_3(VPDSCL_EASF_V_BF1_PWL_SEG0, REG_DEFAULT(VPDSCL_EASF_V_BF1_PWL_SEG0),
        SCL_EASF_V_BF1_PWL_IN_SEG0, scl_data->dscl_prog_data.easf_v_bf1_pwl_in_seg0,
        SCL_EASF_V_BF1_PWL_BASE_SEG0, scl_data->dscl_prog_data.easf_v_bf1_pwl_base_seg0,
        SCL_EASF_V_BF1_PWL_SLOPE_SEG0, scl_data->dscl_prog_data.easf_v_bf1_pwl_slope_seg0);
    REG_SET_3(VPDSCL_EASF_V_BF1_PWL_SEG1, REG_DEFAULT(VPDSCL_EASF_V_BF1_PWL_SEG1),
        SCL_EASF_V_BF1_PWL_IN_SEG1, scl_data->dscl_prog_data.easf_v_bf1_pwl_in_seg1,
        SCL_EASF_V_BF1_PWL_BASE_SEG1, scl_data->dscl_prog_data.easf_v_bf1_pwl_base_seg1,
        SCL_EASF_V_BF1_PWL_SLOPE_SEG1, scl_data->dscl_prog_data.easf_v_bf1_pwl_slope_seg1);
    REG_SET_3(VPDSCL_EASF_V_BF1_PWL_SEG2, REG_DEFAULT(VPDSCL_EASF_V_BF1_PWL_SEG2),
        SCL_EASF_V_BF1_PWL_IN_SEG2, scl_data->dscl_prog_data.easf_v_bf1_pwl_in_seg2,
        SCL_EASF_V_BF1_PWL_BASE_SEG2, scl_data->dscl_prog_data.easf_v_bf1_pwl_base_seg2,
        SCL_EASF_V_BF1_PWL_SLOPE_SEG2, scl_data->dscl_prog_data.easf_v_bf1_pwl_slope_seg2);
    REG_SET_3(VPDSCL_EASF_V_BF1_PWL_SEG3, REG_DEFAULT(VPDSCL_EASF_V_BF1_PWL_SEG3),
        SCL_EASF_V_BF1_PWL_IN_SEG3, scl_data->dscl_prog_data.easf_v_bf1_pwl_in_seg3,
        SCL_EASF_V_BF1_PWL_BASE_SEG3, scl_data->dscl_prog_data.easf_v_bf1_pwl_base_seg3,
        SCL_EASF_V_BF1_PWL_SLOPE_SEG3, scl_data->dscl_prog_data.easf_v_bf1_pwl_slope_seg3);
    REG_SET_3(VPDSCL_EASF_V_BF1_PWL_SEG4, REG_DEFAULT(VPDSCL_EASF_V_BF1_PWL_SEG4),
        SCL_EASF_V_BF1_PWL_IN_SEG4, scl_data->dscl_prog_data.easf_v_bf1_pwl_in_seg4,
        SCL_EASF_V_BF1_PWL_BASE_SEG4, scl_data->dscl_prog_data.easf_v_bf1_pwl_base_seg4,
        SCL_EASF_V_BF1_PWL_SLOPE_SEG4, scl_data->dscl_prog_data.easf_v_bf1_pwl_slope_seg4);
    REG_SET_3(VPDSCL_EASF_V_BF1_PWL_SEG5, REG_DEFAULT(VPDSCL_EASF_V_BF1_PWL_SEG5),
        SCL_EASF_V_BF1_PWL_IN_SEG5, scl_data->dscl_prog_data.easf_v_bf1_pwl_in_seg5,
        SCL_EASF_V_BF1_PWL_BASE_SEG5, scl_data->dscl_prog_data.easf_v_bf1_pwl_base_seg5,
        SCL_EASF_V_BF1_PWL_SLOPE_SEG5, scl_data->dscl_prog_data.easf_v_bf1_pwl_slope_seg5);
    REG_SET_3(VPDSCL_EASF_V_BF1_PWL_SEG6, REG_DEFAULT(VPDSCL_EASF_V_BF1_PWL_SEG6),
        SCL_EASF_V_BF1_PWL_IN_SEG6, scl_data->dscl_prog_data.easf_v_bf1_pwl_in_seg6,
        SCL_EASF_V_BF1_PWL_BASE_SEG6, scl_data->dscl_prog_data.easf_v_bf1_pwl_base_seg6,
        SCL_EASF_V_BF1_PWL_SLOPE_SEG6, scl_data->dscl_prog_data.easf_v_bf1_pwl_slope_seg6);
    REG_SET_2(VPDSCL_EASF_V_BF1_PWL_SEG7, REG_DEFAULT(VPDSCL_EASF_V_BF1_PWL_SEG7),
        SCL_EASF_V_BF1_PWL_IN_SEG7, scl_data->dscl_prog_data.easf_v_bf1_pwl_in_seg7,
        SCL_EASF_V_BF1_PWL_BASE_SEG7, scl_data->dscl_prog_data.easf_v_bf1_pwl_base_seg7);
    REG_SET_3(VPDSCL_EASF_V_BF3_PWL_SEG0, REG_DEFAULT(VPDSCL_EASF_V_BF3_PWL_SEG0),
        SCL_EASF_V_BF3_PWL_IN_SEG0, scl_data->dscl_prog_data.easf_v_bf3_pwl_in_set0,
        SCL_EASF_V_BF3_PWL_BASE_SEG0, scl_data->dscl_prog_data.easf_v_bf3_pwl_base_set0,
        SCL_EASF_V_BF3_PWL_SLOPE_SEG0, scl_data->dscl_prog_data.easf_v_bf3_pwl_slope_set0);
    REG_SET_3(VPDSCL_EASF_V_BF3_PWL_SEG1, REG_DEFAULT(VPDSCL_EASF_V_BF3_PWL_SEG1),
        SCL_EASF_V_BF3_PWL_IN_SEG1, scl_data->dscl_prog_data.easf_v_bf3_pwl_in_set1,
        SCL_EASF_V_BF3_PWL_BASE_SEG1, scl_data->dscl_prog_data.easf_v_bf3_pwl_base_set1,
        SCL_EASF_V_BF3_PWL_SLOPE_SEG1, scl_data->dscl_prog_data.easf_v_bf3_pwl_slope_set1);
    REG_SET_3(VPDSCL_EASF_V_BF3_PWL_SEG2, REG_DEFAULT(VPDSCL_EASF_V_BF3_PWL_SEG2),
        SCL_EASF_V_BF3_PWL_IN_SEG2, scl_data->dscl_prog_data.easf_v_bf3_pwl_in_set2,
        SCL_EASF_V_BF3_PWL_BASE_SEG2, scl_data->dscl_prog_data.easf_v_bf3_pwl_base_set2,
        SCL_EASF_V_BF3_PWL_SLOPE_SEG2, scl_data->dscl_prog_data.easf_v_bf3_pwl_slope_set2);
    REG_SET_3(VPDSCL_EASF_V_BF3_PWL_SEG3, REG_DEFAULT(VPDSCL_EASF_V_BF3_PWL_SEG3),
        SCL_EASF_V_BF3_PWL_IN_SEG3, scl_data->dscl_prog_data.easf_v_bf3_pwl_in_set3,
        SCL_EASF_V_BF3_PWL_BASE_SEG3, scl_data->dscl_prog_data.easf_v_bf3_pwl_base_set3,
        SCL_EASF_V_BF3_PWL_SLOPE_SEG3, scl_data->dscl_prog_data.easf_v_bf3_pwl_slope_set3);
    REG_SET_3(VPDSCL_EASF_V_BF3_PWL_SEG4, REG_DEFAULT(VPDSCL_EASF_V_BF3_PWL_SEG4),
        SCL_EASF_V_BF3_PWL_IN_SEG4, scl_data->dscl_prog_data.easf_v_bf3_pwl_in_set4,
        SCL_EASF_V_BF3_PWL_BASE_SEG4, scl_data->dscl_prog_data.easf_v_bf3_pwl_base_set4,
        SCL_EASF_V_BF3_PWL_SLOPE_SEG4, scl_data->dscl_prog_data.easf_v_bf3_pwl_slope_set4);
    REG_SET_2(VPDSCL_EASF_V_BF3_PWL_SEG5, REG_DEFAULT(VPDSCL_EASF_V_BF3_PWL_SEG5),
        SCL_EASF_V_BF3_PWL_IN_SEG5, scl_data->dscl_prog_data.easf_v_bf3_pwl_in_set5,
        SCL_EASF_V_BF3_PWL_BASE_SEG5, scl_data->dscl_prog_data.easf_v_bf3_pwl_base_set5);
    /* DSCL_EASF_H_MODE */
    REG_SET_3(VPDSCL_EASF_H_MODE, REG_DEFAULT(VPDSCL_EASF_H_MODE),
        SCL_EASF_H_EN, scl_data->dscl_prog_data.easf_h_en,
        SCL_EASF_H_2TAP_SHARP_FACTOR, scl_data->dscl_prog_data.easf_h_sharp_factor,
        SCL_EASF_H_RINGEST_FORCE_EN, scl_data->dscl_prog_data.easf_h_ring);
    REG_SET_6(VPDSCL_EASF_H_BF_CNTL, REG_DEFAULT(VPDSCL_EASF_H_BF_CNTL),
        SCL_EASF_H_BF1_EN, scl_data->dscl_prog_data.easf_h_bf1_en,
        SCL_EASF_H_BF2_MODE, scl_data->dscl_prog_data.easf_h_bf2_mode,
        SCL_EASF_H_BF3_MODE, scl_data->dscl_prog_data.easf_h_bf3_mode,
        SCL_EASF_H_BF2_FLAT1_GAIN, scl_data->dscl_prog_data.easf_h_bf2_flat1_gain,
        SCL_EASF_H_BF2_FLAT2_GAIN, scl_data->dscl_prog_data.easf_h_bf2_flat2_gain,
        SCL_EASF_H_BF2_ROC_GAIN, scl_data->dscl_prog_data.easf_h_bf2_roc_gain);
    REG_SET_2(VPDSCL_EASF_H_RINGEST_EVENTAP_REDUCE, REG_DEFAULT(VPDSCL_EASF_H_RINGEST_EVENTAP_REDUCE),
        SCL_EASF_H_RINGEST_EVENTAP_REDUCEG1, scl_data->dscl_prog_data.easf_h_ringest_eventap_reduceg1,
        SCL_EASF_H_RINGEST_EVENTAP_REDUCEG2, scl_data->dscl_prog_data.easf_h_ringest_eventap_reduceg2);
    REG_SET_2(VPDSCL_EASF_H_RINGEST_EVENTAP_GAIN, REG_DEFAULT(VPDSCL_EASF_H_RINGEST_EVENTAP_GAIN),
        SCL_EASF_H_RINGEST_EVENTAP_GAIN1, scl_data->dscl_prog_data.easf_h_ringest_eventap_gain1,
        SCL_EASF_H_RINGEST_EVENTAP_GAIN2, scl_data->dscl_prog_data.easf_h_ringest_eventap_gain2);
    REG_SET_4(VPDSCL_EASF_H_BF_FINAL_MAX_MIN, REG_DEFAULT(VPDSCL_EASF_H_BF_FINAL_MAX_MIN),
        SCL_EASF_H_BF_MAXA, scl_data->dscl_prog_data.easf_h_bf_maxa,
        SCL_EASF_H_BF_MAXB, scl_data->dscl_prog_data.easf_h_bf_maxb,
        SCL_EASF_H_BF_MINA, scl_data->dscl_prog_data.easf_h_bf_mina,
        SCL_EASF_H_BF_MINB, scl_data->dscl_prog_data.easf_h_bf_minb);
    REG_SET_3(VPDSCL_EASF_H_BF1_PWL_SEG0, REG_DEFAULT(VPDSCL_EASF_H_BF1_PWL_SEG0),
        SCL_EASF_H_BF1_PWL_IN_SEG0, scl_data->dscl_prog_data.easf_h_bf1_pwl_in_seg0,
        SCL_EASF_H_BF1_PWL_BASE_SEG0, scl_data->dscl_prog_data.easf_h_bf1_pwl_base_seg0,
        SCL_EASF_H_BF1_PWL_SLOPE_SEG0, scl_data->dscl_prog_data.easf_h_bf1_pwl_slope_seg0);
    REG_SET_3(VPDSCL_EASF_H_BF1_PWL_SEG1, REG_DEFAULT(VPDSCL_EASF_H_BF1_PWL_SEG1),
        SCL_EASF_H_BF1_PWL_IN_SEG1, scl_data->dscl_prog_data.easf_h_bf1_pwl_in_seg1,
        SCL_EASF_H_BF1_PWL_BASE_SEG1, scl_data->dscl_prog_data.easf_h_bf1_pwl_base_seg1,
        SCL_EASF_H_BF1_PWL_SLOPE_SEG1, scl_data->dscl_prog_data.easf_h_bf1_pwl_slope_seg1);
    REG_SET_3(VPDSCL_EASF_H_BF1_PWL_SEG2, REG_DEFAULT(VPDSCL_EASF_H_BF1_PWL_SEG2),
        SCL_EASF_H_BF1_PWL_IN_SEG2, scl_data->dscl_prog_data.easf_h_bf1_pwl_in_seg2,
        SCL_EASF_H_BF1_PWL_BASE_SEG2, scl_data->dscl_prog_data.easf_h_bf1_pwl_base_seg2,
        SCL_EASF_H_BF1_PWL_SLOPE_SEG2, scl_data->dscl_prog_data.easf_h_bf1_pwl_slope_seg2);
    REG_SET_3(VPDSCL_EASF_H_BF1_PWL_SEG3, REG_DEFAULT(VPDSCL_EASF_H_BF1_PWL_SEG3),
        SCL_EASF_H_BF1_PWL_IN_SEG3, scl_data->dscl_prog_data.easf_h_bf1_pwl_in_seg3,
        SCL_EASF_H_BF1_PWL_BASE_SEG3, scl_data->dscl_prog_data.easf_h_bf1_pwl_base_seg3,
        SCL_EASF_H_BF1_PWL_SLOPE_SEG3, scl_data->dscl_prog_data.easf_h_bf1_pwl_slope_seg3);
    REG_SET_3(VPDSCL_EASF_H_BF1_PWL_SEG4, REG_DEFAULT(VPDSCL_EASF_H_BF1_PWL_SEG4),
        SCL_EASF_H_BF1_PWL_IN_SEG4, scl_data->dscl_prog_data.easf_h_bf1_pwl_in_seg4,
        SCL_EASF_H_BF1_PWL_BASE_SEG4, scl_data->dscl_prog_data.easf_h_bf1_pwl_base_seg4,
        SCL_EASF_H_BF1_PWL_SLOPE_SEG4, scl_data->dscl_prog_data.easf_h_bf1_pwl_slope_seg4);
    REG_SET_3(VPDSCL_EASF_H_BF1_PWL_SEG5, REG_DEFAULT(VPDSCL_EASF_H_BF1_PWL_SEG5),
        SCL_EASF_H_BF1_PWL_IN_SEG5, scl_data->dscl_prog_data.easf_h_bf1_pwl_in_seg5,
        SCL_EASF_H_BF1_PWL_BASE_SEG5, scl_data->dscl_prog_data.easf_h_bf1_pwl_base_seg5,
        SCL_EASF_H_BF1_PWL_SLOPE_SEG5, scl_data->dscl_prog_data.easf_h_bf1_pwl_slope_seg5);
    REG_SET_3(VPDSCL_EASF_H_BF1_PWL_SEG6, REG_DEFAULT(VPDSCL_EASF_H_BF1_PWL_SEG6),
        SCL_EASF_H_BF1_PWL_IN_SEG6, scl_data->dscl_prog_data.easf_h_bf1_pwl_in_seg6,
        SCL_EASF_H_BF1_PWL_BASE_SEG6, scl_data->dscl_prog_data.easf_h_bf1_pwl_base_seg6,
        SCL_EASF_H_BF1_PWL_SLOPE_SEG6, scl_data->dscl_prog_data.easf_h_bf1_pwl_slope_seg6);
    REG_SET_2(VPDSCL_EASF_H_BF1_PWL_SEG7, REG_DEFAULT(VPDSCL_EASF_H_BF1_PWL_SEG7),
        SCL_EASF_H_BF1_PWL_IN_SEG7, scl_data->dscl_prog_data.easf_h_bf1_pwl_in_seg7,
        SCL_EASF_H_BF1_PWL_BASE_SEG7, scl_data->dscl_prog_data.easf_h_bf1_pwl_base_seg7);
    REG_SET_3(VPDSCL_EASF_H_BF3_PWL_SEG0, REG_DEFAULT(VPDSCL_EASF_H_BF3_PWL_SEG0),
        SCL_EASF_H_BF3_PWL_IN_SEG0, scl_data->dscl_prog_data.easf_h_bf3_pwl_in_set0,
        SCL_EASF_H_BF3_PWL_BASE_SEG0, scl_data->dscl_prog_data.easf_h_bf3_pwl_base_set0,
        SCL_EASF_H_BF3_PWL_SLOPE_SEG0, scl_data->dscl_prog_data.easf_h_bf3_pwl_slope_set0);
    REG_SET_3(VPDSCL_EASF_H_BF3_PWL_SEG1, REG_DEFAULT(VPDSCL_EASF_H_BF3_PWL_SEG1),
        SCL_EASF_H_BF3_PWL_IN_SEG1, scl_data->dscl_prog_data.easf_h_bf3_pwl_in_set1,
        SCL_EASF_H_BF3_PWL_BASE_SEG1, scl_data->dscl_prog_data.easf_h_bf3_pwl_base_set1,
        SCL_EASF_H_BF3_PWL_SLOPE_SEG1, scl_data->dscl_prog_data.easf_h_bf3_pwl_slope_set1);
    REG_SET_3(VPDSCL_EASF_H_BF3_PWL_SEG2, REG_DEFAULT(VPDSCL_EASF_H_BF3_PWL_SEG2),
        SCL_EASF_H_BF3_PWL_IN_SEG2, scl_data->dscl_prog_data.easf_h_bf3_pwl_in_set2,
        SCL_EASF_H_BF3_PWL_BASE_SEG2, scl_data->dscl_prog_data.easf_h_bf3_pwl_base_set2,
        SCL_EASF_H_BF3_PWL_SLOPE_SEG2, scl_data->dscl_prog_data.easf_h_bf3_pwl_slope_set2);
    REG_SET_3(VPDSCL_EASF_H_BF3_PWL_SEG3, REG_DEFAULT(VPDSCL_EASF_H_BF3_PWL_SEG3),
        SCL_EASF_H_BF3_PWL_IN_SEG3, scl_data->dscl_prog_data.easf_h_bf3_pwl_in_set3,
        SCL_EASF_H_BF3_PWL_BASE_SEG3, scl_data->dscl_prog_data.easf_h_bf3_pwl_base_set3,
        SCL_EASF_H_BF3_PWL_SLOPE_SEG3, scl_data->dscl_prog_data.easf_h_bf3_pwl_slope_set3);
    REG_SET_3(VPDSCL_EASF_H_BF3_PWL_SEG4, REG_DEFAULT(VPDSCL_EASF_H_BF3_PWL_SEG4),
        SCL_EASF_H_BF3_PWL_IN_SEG4, scl_data->dscl_prog_data.easf_h_bf3_pwl_in_set4,
        SCL_EASF_H_BF3_PWL_BASE_SEG4, scl_data->dscl_prog_data.easf_h_bf3_pwl_base_set4,
        SCL_EASF_H_BF3_PWL_SLOPE_SEG4, scl_data->dscl_prog_data.easf_h_bf3_pwl_slope_set4);
    REG_SET_2(VPDSCL_EASF_H_BF3_PWL_SEG5, REG_DEFAULT(VPDSCL_EASF_H_BF3_PWL_SEG5),
        SCL_EASF_H_BF3_PWL_IN_SEG5, scl_data->dscl_prog_data.easf_h_bf3_pwl_in_set5,
        SCL_EASF_H_BF3_PWL_BASE_SEG5, scl_data->dscl_prog_data.easf_h_bf3_pwl_base_set5);
    /* DSCL_EASF_SC_MATRIX_C0C1, DSCL_EASF_SC_MATRIX_C2C3 */
    REG_SET_2(VPDSCL_SC_MATRIX_C0C1, REG_DEFAULT(VPDSCL_SC_MATRIX_C0C1),
        SCL_SC_MATRIX_C0, scl_data->dscl_prog_data.easf_matrix_c0,
        SCL_SC_MATRIX_C1, scl_data->dscl_prog_data.easf_matrix_c1);
    REG_SET_2(VPDSCL_SC_MATRIX_C2C3, REG_DEFAULT(VPDSCL_SC_MATRIX_C2C3),
        SCL_SC_MATRIX_C2, scl_data->dscl_prog_data.easf_matrix_c2,
        SCL_SC_MATRIX_C3, scl_data->dscl_prog_data.easf_matrix_c3);
}

/**
 * vpe20_dscl_disable_easf - Disable EASF when no scaling (1:1)
 *
 */
void vpe20_dscl_disable_easf(struct dpp *dpp, const struct scaler_data *scl_data)
{
    PROGRAM_ENTRY();

    /* DSCL_EASF_V_MODE */
    REG_SET(VPDSCL_EASF_V_MODE, REG_DEFAULT(VPDSCL_EASF_V_MODE),
        SCL_EASF_V_EN, scl_data->dscl_prog_data.easf_v_en);
    /* DSCL_EASF_H_MODE */
    REG_SET(VPDSCL_EASF_H_MODE, REG_DEFAULT(VPDSCL_EASF_H_MODE),
        SCL_EASF_H_EN, scl_data->dscl_prog_data.easf_h_en);
    /*Set the color conversion matrices even when the scaler is not active*/
    REG_SET_2(VPDSCL_SC_MODE, REG_DEFAULT(VPDSCL_SC_MODE), SCL_SC_MATRIX_MODE,
        scl_data->dscl_prog_data.easf_matrix_mode, SCL_SC_LTONL_EN,
        scl_data->dscl_prog_data.easf_ltonl_en);
    REG_SET_2(VPDSCL_SC_MATRIX_C0C1, REG_DEFAULT(VPDSCL_SC_MATRIX_C0C1), SCL_SC_MATRIX_C0,
        scl_data->dscl_prog_data.easf_matrix_c0, SCL_SC_MATRIX_C1,
        scl_data->dscl_prog_data.easf_matrix_c1);
    REG_SET_2(VPDSCL_SC_MATRIX_C2C3, REG_DEFAULT(VPDSCL_SC_MATRIX_C2C3), SCL_SC_MATRIX_C2,
        scl_data->dscl_prog_data.easf_matrix_c2, SCL_SC_MATRIX_C3,
        scl_data->dscl_prog_data.easf_matrix_c3);
}

static void dpp2_dscl_set_isharp_filter(struct dpp *dpp, const uint32_t *filter)
{
    PROGRAM_ENTRY();

    uint32_t level = 0;
    uint32_t filter_data;
    if (filter == NULL)
        return;

    REG_SET_3(VPISHARP_DELTA_LUT_MEM_PWR_CTRL, REG_DEFAULT(VPISHARP_DELTA_LUT_MEM_PWR_CTRL),
        ISHARP_DELTA_LUT_MEM_PWR_FORCE, 0, ISHARP_DELTA_LUT_MEM_PWR_DIS, 1,
        ISHARP_DELTA_LUT_MEM_PWR_STATE, 0);

    REG_SET(VPISHARP_DELTA_CTRL, REG_DEFAULT(VPISHARP_DELTA_CTRL),
        ISHARP_DELTA_LUT_HOST_SELECT, 0);
    REG_SET(VPISHARP_DELTA_INDEX, 0,
        ISHARP_DELTA_INDEX, level);

    for (level = 0; level < NUM_ISHARP_LEVELS; level++) {
        filter_data = filter[level];
        REG_SET(VPISHARP_DELTA_DATA, REG_DEFAULT(VPISHARP_DELTA_DATA),
            ISHARP_DELTA_DATA, filter_data);
    }

    REG_SET_3(VPISHARP_DELTA_LUT_MEM_PWR_CTRL, REG_DEFAULT(VPISHARP_DELTA_LUT_MEM_PWR_CTRL),
        ISHARP_DELTA_LUT_MEM_PWR_FORCE, 0, ISHARP_DELTA_LUT_MEM_PWR_DIS, 0,
        ISHARP_DELTA_LUT_MEM_PWR_STATE, 0);
}

/**
  * vpe20_dscl_program_isharp - Program isharp
  *
  * This is the primary function to program isharp
  *
  */
void vpe20_dscl_program_isharp(struct dpp *dpp, const struct scaler_data *scl_data)
{
    PROGRAM_ENTRY();

    /* ISHARP_MDOE */
    REG_SET_6(VPISHARP_MODE, REG_DEFAULT(VPISHARP_MODE),
        ISHARP_EN, scl_data->dscl_prog_data.isharp_en,
        ISHARP_NOISEDET_EN, scl_data->dscl_prog_data.isharp_noise_det.enable,
        ISHARP_NOISEDET_MODE, scl_data->dscl_prog_data.isharp_noise_det.mode,
        ISHARP_LBA_MODE, scl_data->dscl_prog_data.isharp_lba.mode,
        ISHARP_FMT_MODE, scl_data->dscl_prog_data.isharp_fmt.mode,
        ISHARP_FMT_NORM, scl_data->dscl_prog_data.isharp_fmt.norm);

    if (scl_data->dscl_prog_data.isharp_en == 0)
        return;

    /* ISHARP_NOISEDET_THRESHOLD */
    REG_SET_2(VPISHARP_NOISEDET_THRESHOLD, REG_DEFAULT(VPISHARP_NOISEDET_THRESHOLD),
        ISHARP_NOISEDET_UTHRE, scl_data->dscl_prog_data.isharp_noise_det.uthreshold,
        ISHARP_NOISEDET_DTHRE, scl_data->dscl_prog_data.isharp_noise_det.dthreshold);
    /* ISHARP_NOISE_GAIN_PWL */
    REG_SET_3(VPISHARP_NOISE_GAIN_PWL, REG_DEFAULT(VPISHARP_NOISE_GAIN_PWL),
        ISHARP_NOISEDET_PWL_START_IN, scl_data->dscl_prog_data.isharp_noise_det.pwl_start_in,
        ISHARP_NOISEDET_PWL_END_IN, scl_data->dscl_prog_data.isharp_noise_det.pwl_end_in,
        ISHARP_NOISEDET_PWL_SLOPE, scl_data->dscl_prog_data.isharp_noise_det.pwl_slope);
    /* ISHARP_LBA: IN_SEG, BASE_SEG, SLOPE_SEG */
    REG_SET_3(VPISHARP_LBA_PWL_SEG0, REG_DEFAULT(VPISHARP_LBA_PWL_SEG0),
        ISHARP_LBA_PWL_IN_SEG0, scl_data->dscl_prog_data.isharp_lba.in_seg[0],
        ISHARP_LBA_PWL_BASE_SEG0, scl_data->dscl_prog_data.isharp_lba.base_seg[0],
        ISHARP_LBA_PWL_SLOPE_SEG0, scl_data->dscl_prog_data.isharp_lba.slope_seg[0]);
    REG_SET_3(VPISHARP_LBA_PWL_SEG1, REG_DEFAULT(VPISHARP_LBA_PWL_SEG1),
        ISHARP_LBA_PWL_IN_SEG1, scl_data->dscl_prog_data.isharp_lba.in_seg[1],
        ISHARP_LBA_PWL_BASE_SEG1, scl_data->dscl_prog_data.isharp_lba.base_seg[1],
        ISHARP_LBA_PWL_SLOPE_SEG1, scl_data->dscl_prog_data.isharp_lba.slope_seg[1]);
    REG_SET_3(VPISHARP_LBA_PWL_SEG2, REG_DEFAULT(VPISHARP_LBA_PWL_SEG2),
        ISHARP_LBA_PWL_IN_SEG2, scl_data->dscl_prog_data.isharp_lba.in_seg[2],
        ISHARP_LBA_PWL_BASE_SEG2, scl_data->dscl_prog_data.isharp_lba.base_seg[2],
        ISHARP_LBA_PWL_SLOPE_SEG2, scl_data->dscl_prog_data.isharp_lba.slope_seg[2]);
    REG_SET_3(VPISHARP_LBA_PWL_SEG3, REG_DEFAULT(VPISHARP_LBA_PWL_SEG3),
        ISHARP_LBA_PWL_IN_SEG3, scl_data->dscl_prog_data.isharp_lba.in_seg[3],
        ISHARP_LBA_PWL_BASE_SEG3, scl_data->dscl_prog_data.isharp_lba.base_seg[3],
        ISHARP_LBA_PWL_SLOPE_SEG3, scl_data->dscl_prog_data.isharp_lba.slope_seg[3]);
    REG_SET_3(VPISHARP_LBA_PWL_SEG4, REG_DEFAULT(VPISHARP_LBA_PWL_SEG4),
        ISHARP_LBA_PWL_IN_SEG4, scl_data->dscl_prog_data.isharp_lba.in_seg[4],
        ISHARP_LBA_PWL_BASE_SEG4, scl_data->dscl_prog_data.isharp_lba.base_seg[4],
        ISHARP_LBA_PWL_SLOPE_SEG4, scl_data->dscl_prog_data.isharp_lba.slope_seg[4]);
    REG_SET_2(VPISHARP_LBA_PWL_SEG5, REG_DEFAULT(VPISHARP_LBA_PWL_SEG5),
        ISHARP_LBA_PWL_IN_SEG5, scl_data->dscl_prog_data.isharp_lba.in_seg[5],
        ISHARP_LBA_PWL_BASE_SEG5, scl_data->dscl_prog_data.isharp_lba.base_seg[5]);

    /* ISHARP_DELTA_LUT */
    dpp2_dscl_set_isharp_filter(dpp, scl_data->dscl_prog_data.isharp_delta);

    /* ISHARP_NLDELTA_SOFT_CLIP */
    REG_SET_6(VPISHARP_NLDELTA_SOFT_CLIP, REG_DEFAULT(VPISHARP_NLDELTA_SOFT_CLIP),
        ISHARP_NLDELTA_SCLIP_EN_P, scl_data->dscl_prog_data.isharp_nldelta_sclip.enable_p,
        ISHARP_NLDELTA_SCLIP_PIVOT_P, scl_data->dscl_prog_data.isharp_nldelta_sclip.pivot_p,
        ISHARP_NLDELTA_SCLIP_SLOPE_P, scl_data->dscl_prog_data.isharp_nldelta_sclip.slope_p,
        ISHARP_NLDELTA_SCLIP_EN_N, scl_data->dscl_prog_data.isharp_nldelta_sclip.enable_n,
        ISHARP_NLDELTA_SCLIP_PIVOT_N, scl_data->dscl_prog_data.isharp_nldelta_sclip.pivot_n,
        ISHARP_NLDELTA_SCLIP_SLOPE_N, scl_data->dscl_prog_data.isharp_nldelta_sclip.slope_n);

    /* Blur and Scale Coefficients - SCL_COEF_RAM_TAP_SELECT */
    if (scl_data->dscl_prog_data.isharp_en) {
        if (scl_data->dscl_prog_data.filter_blur_scale_v) {
            vpe10_dpp_dscl_set_scaler_filter(dpp, scl_data->taps.v_taps,
                SCL_COEF_VERTICAL_BLUR_SCALE, scl_data->dscl_prog_data.filter_blur_scale_v);
        }
        if (scl_data->dscl_prog_data.filter_blur_scale_h) {
            vpe10_dpp_dscl_set_scaler_filter(dpp, scl_data->taps.h_taps,
                SCL_COEF_HORIZONTAL_BLUR_SCALE, scl_data->dscl_prog_data.filter_blur_scale_h);
        }
    }
}

static void dpp2_set_recout(struct dpp *dpp, const struct vpe_rect *recout)
{
    PROGRAM_ENTRY();

    REG_SET_2(VPDSCL_RECOUT_START, 0, RECOUT_START_X, recout->x, RECOUT_START_Y, recout->y);

    REG_SET_2(VPDSCL_RECOUT_SIZE, 0, RECOUT_WIDTH, recout->width, RECOUT_HEIGHT, recout->height);
}

static void dpp2_power_on_dscl(struct dpp *dpp, bool power_on)
{
    PROGRAM_ENTRY();

    if (dpp->vpe_priv->init.debug.enable_mem_low_power.bits.dscl) {
        if (power_on) {
            REG_SET_2(VPDSCL_MEM_PWR_CTRL, REG_DEFAULT(VPDSCL_MEM_PWR_CTRL), LUT_MEM_PWR_DIS, 0,
                LUT_MEM_PWR_FORCE, 0);

            // introduce a delay by dummy set
            REG_SET_2(VPDSCL_MEM_PWR_CTRL, REG_DEFAULT(VPDSCL_MEM_PWR_CTRL), LUT_MEM_PWR_DIS, 0,
                LUT_MEM_PWR_FORCE, 0);

            REG_SET_2(VPDSCL_MEM_PWR_CTRL, REG_DEFAULT(VPDSCL_MEM_PWR_CTRL), LUT_MEM_PWR_DIS, 0,
                LUT_MEM_PWR_FORCE, 0);
        } else {
            REG_SET_2(VPDSCL_MEM_PWR_CTRL, REG_DEFAULT(VPDSCL_MEM_PWR_CTRL), LUT_MEM_PWR_DIS, 0,
                LUT_MEM_PWR_FORCE, 3);
        }
    } else {
        if (power_on) {
            REG_SET_2(VPDSCL_MEM_PWR_CTRL, REG_DEFAULT(VPDSCL_MEM_PWR_CTRL), LUT_MEM_PWR_DIS, 1,
                LUT_MEM_PWR_FORCE, 0);
        } else {
            REG_SET_2(VPDSCL_MEM_PWR_CTRL, REG_DEFAULT(VPDSCL_MEM_PWR_CTRL), LUT_MEM_PWR_DIS, 0,
                LUT_MEM_PWR_FORCE, 0);
        }
    }
}

static void dpp2_dscl_set_scale_ratio(struct dpp *dpp, const struct scaler_data *data)
{
    PROGRAM_ENTRY();

    REG_SET(VPDSCL_HORZ_FILTER_SCALE_RATIO, 0,
        SCL_H_SCALE_RATIO, data->dscl_prog_data.ratios.h_scale_ratio);

    REG_SET(VPDSCL_VERT_FILTER_SCALE_RATIO, 0,
        SCL_V_SCALE_RATIO, data->dscl_prog_data.ratios.v_scale_ratio);

    REG_SET(VPDSCL_HORZ_FILTER_SCALE_RATIO_C, 0,
        SCL_H_SCALE_RATIO_C, data->dscl_prog_data.ratios.h_scale_ratio_c);

    REG_SET(VPDSCL_VERT_FILTER_SCALE_RATIO_C, 0,
        SCL_V_SCALE_RATIO_C, data->dscl_prog_data.ratios.v_scale_ratio_c);
}

void vpe20_dpp_dscl_set_scaler_position(struct dpp *dpp, const struct scaler_data *data)
{
    PROGRAM_ENTRY();

    REG_SET_2(VPDSCL_HORZ_FILTER_INIT, 0,
        SCL_H_INIT_FRAC, data->dscl_prog_data.init.h_filter_init_frac,
        SCL_H_INIT_INT, data->dscl_prog_data.init.h_filter_init_int);

    REG_SET_2(VPDSCL_HORZ_FILTER_INIT_C, 0,
        SCL_H_INIT_FRAC_C, data->dscl_prog_data.init.h_filter_init_frac_c,
        SCL_H_INIT_INT_C, data->dscl_prog_data.init.h_filter_init_int_c);

    REG_SET_2(VPDSCL_VERT_FILTER_INIT, 0,
        SCL_V_INIT_FRAC, data->dscl_prog_data.init.v_filter_init_frac,
        SCL_V_INIT_INT, data->dscl_prog_data.init.v_filter_init_int);

    REG_SET_2(VPDSCL_VERT_FILTER_INIT_BOT, 0,
        SCL_V_INIT_FRAC_BOT, data->dscl_prog_data.init.v_filter_init_bot_frac,
        SCL_V_INIT_INT_BOT, data->dscl_prog_data.init.v_filter_init_bot_int);

    REG_SET_2(VPDSCL_VERT_FILTER_INIT_C, 0,
        SCL_V_INIT_FRAC_C, data->dscl_prog_data.init.v_filter_init_frac_c,
        SCL_V_INIT_INT_C, data->dscl_prog_data.init.v_filter_init_int_c);

    REG_SET_2(VPDSCL_VERT_FILTER_INIT_BOT_C, 0,
        SCL_V_INIT_FRAC_BOT_C, data->dscl_prog_data.init.v_filter_init_bot_frac_c,
        SCL_V_INIT_INT_BOT_C, data->dscl_prog_data.init.v_filter_init_bot_int_c);
}

static void dpp2_dscl_set_scl_filter_and_dscl_mode(struct dpp *dpp,
    const struct scaler_data *scl_data, enum vpe10_dscl_mode_sel scl_mode, bool chroma_coef_mode)
{
    PROGRAM_ENTRY();

    const uint16_t *filter_h   = NULL;
    const uint16_t *filter_v   = NULL;
    const uint16_t *filter_h_c = NULL;
    const uint16_t *filter_v_c = NULL;

    if (scl_mode != DSCL_MODE_DSCL_BYPASS) {
        filter_h   = scl_data->dscl_prog_data.filter_h;
        filter_v   = scl_data->dscl_prog_data.filter_v;
        filter_h_c = scl_data->dscl_prog_data.filter_h_c;
        filter_v_c = scl_data->dscl_prog_data.filter_v_c;

        if (filter_h) {
            vpe10_dpp_dscl_set_scaler_filter(
                dpp, scl_data->taps.h_taps, SCL_COEF_LUMA_HORZ_FILTER, filter_h);
        }

        if (filter_v) {
            vpe10_dpp_dscl_set_scaler_filter(
                dpp, scl_data->taps.v_taps, SCL_COEF_LUMA_VERT_FILTER, filter_v);
        }

        if (chroma_coef_mode) {
            if (filter_h_c) {
                vpe10_dpp_dscl_set_scaler_filter(
                    dpp, scl_data->taps.h_taps_c, SCL_COEF_CHROMA_HORZ_FILTER, filter_h_c);
            }
            if (filter_v_c) {
                vpe10_dpp_dscl_set_scaler_filter(
                    dpp, scl_data->taps.v_taps_c, SCL_COEF_CHROMA_VERT_FILTER, filter_v_c);
            }
        }
    }

    REG_SET_2(VPDSCL_MODE, 0, VPDSCL_MODE, scl_mode, SCL_CHROMA_COEF_MODE, chroma_coef_mode);
}


static void dpp2_dscl_set_taps(struct dpp *dpp, const struct scaler_data *scl_data)
{
    PROGRAM_ENTRY();

    /* HTaps/VTaps */
    REG_SET_4(VPDSCL_TAP_CONTROL, REG_DEFAULT(VPDSCL_TAP_CONTROL), SCL_V_NUM_TAPS,
        scl_data->taps.v_taps - 1, SCL_H_NUM_TAPS, scl_data->taps.h_taps - 1, SCL_V_NUM_TAPS_C,
        scl_data->taps.v_taps_c - 1, SCL_H_NUM_TAPS_C, scl_data->taps.h_taps_c - 1);
}

void vpe20_dpp_set_segment_scaler(struct dpp *dpp, const struct scaler_data *scl_data)
{
    PROGRAM_ENTRY();

    const struct vpe_rect *rect     =
        (struct vpe_rect *)&scl_data->dscl_prog_data.recout;
    enum scl_mode         dscl_mode =
        (enum scl_mode) scl_data->dscl_prog_data.dscl_mode;

    uint32_t mpc_width  = scl_data->dscl_prog_data.mpc_size.width;
    uint32_t mpc_height = scl_data->dscl_prog_data.mpc_size.height;

    if (dpp->vpe_priv->init.debug.opp_background_gen) {
        // We set the x and y of the DPP rect out to 0, since OPP does the top and left extend
        struct vpe_rect new_rect;
        new_rect.y      = 0;
        new_rect.width  = rect->width;
        new_rect.x      = 0;
        new_rect.height = rect->height;

        dpp2_set_recout(dpp, &new_rect);
    } else {
        dpp2_set_recout(dpp, rect);
    }

    REG_SET_2(VPMPC_SIZE, REG_DEFAULT(VPMPC_SIZE), VPMPC_WIDTH, mpc_width,
        VPMPC_HEIGHT, mpc_height);

    if (dscl_mode == SCL_MODE_DSCL_BYPASS)
        return;

    dpp->funcs->dscl_set_scaler_position(dpp, scl_data);
}

/**
 * vpe20_dpp_set_frame_scaler - program scaler from dscl_prog_data
 *
 * This is the primary function to program scaler and line buffer in manual
 * scaling mode. To execute the required operations for manual scale, we need
 * to disable AutoCal first.
 */
void vpe20_dpp_set_frame_scaler(struct dpp *dpp, const struct scaler_data *scl_data)
{
    PROGRAM_ENTRY();

    enum vpe10_dscl_mode_sel dscl_mode = vpe20_dpp_dscl_get_dscl_mode(scl_data);
    bool                     ycbcr     = vpe10_dpp_dscl_is_ycbcr(scl_data->format);

    if (dscl_mode == DSCL_MODE_DSCL_BYPASS) {
        dpp2_dscl_set_scl_filter_and_dscl_mode(dpp, scl_data, dscl_mode, ycbcr);
        vpe20_dscl_program_isharp(dpp, scl_data);
        vpe20_dscl_disable_easf(dpp, scl_data);
        vpe10_dpp_power_on_dscl(dpp, false);
    } else {
        vpe10_dpp_power_on_dscl(dpp, true);
        vpe10_dpp_dscl_set_lb(dpp, &scl_data->lb_params, LB_MEMORY_CONFIG_0);
        dpp2_dscl_set_scale_ratio(dpp, scl_data);
        dpp2_dscl_set_taps(dpp, scl_data);
        dpp2_dscl_set_scl_filter_and_dscl_mode(dpp, scl_data, dscl_mode, ycbcr);
        vpe20_dscl_program_isharp(dpp, scl_data);

        if (dscl_mode == DSCL_MODE_SCALING_444_BYPASS ||
            (!scl_data->dscl_prog_data.easf_v_en && !scl_data->dscl_prog_data.easf_h_en)) {
            vpe20_dscl_disable_easf(dpp, scl_data);
        } else {
            vpe20_dscl_program_easf(dpp, scl_data);
        }
    }
}
