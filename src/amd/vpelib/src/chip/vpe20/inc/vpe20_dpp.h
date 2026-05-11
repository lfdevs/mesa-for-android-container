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
#pragma once

#include "dpp.h"
#include "vpe10_dpp.h"
#include "transform.h"
#include "reg_helper.h"
#include "vpe_types.h"
#include "color_table.h"

#ifdef __cplusplus
extern "C" {
#endif

// Used to resolve corner case
#define DPP_SFRB(field_name, reg_name, post_fix) .field_name = reg_name##_##field_name##post_fix

#define DPP_REG_LIST_VPE20_COMMON(id)                                                              \
    DPP_REG_LIST_VPE10_COMMON(id), SRIDFVL(VPCNVC_ALPHA_2BIT_LUT01, VPCNVC_CFG, id),               \
        SRIDFVL(VPCNVC_ALPHA_2BIT_LUT23, VPCNVC_CFG, id), SRIDFVL(VPDSCL_SC_MODE, VPDSCL, id),     \
        SRIDFVL(VPDSCL_SC_MATRIX_C0C1, VPDSCL, id), SRIDFVL(VPDSCL_SC_MATRIX_C2C3, VPDSCL, id),    \
        SRIDFVL(VPDSCL_EASF_H_MODE, VPDSCL, id), SRIDFVL(VPDSCL_EASF_V_MODE, VPDSCL, id),          \
        SRIDFVL(VPDSCL_EASF_H_BF_CNTL, VPDSCL, id), SRIDFVL(VPDSCL_EASF_V_BF_CNTL, VPDSCL, id),    \
        SRIDFVL(VPDSCL_EASF_H_RINGEST_EVENTAP_GAIN, VPDSCL, id),                                   \
        SRIDFVL(VPDSCL_EASF_H_RINGEST_EVENTAP_REDUCE, VPDSCL, id),                                 \
        SRIDFVL(VPDSCL_EASF_V_RINGEST_EVENTAP_GAIN, VPDSCL, id),                                   \
        SRIDFVL(VPDSCL_EASF_V_RINGEST_EVENTAP_REDUCE, VPDSCL, id),                                 \
        SRIDFVL(VPDSCL_EASF_H_BF_FINAL_MAX_MIN, VPDSCL, id),                                       \
        SRIDFVL(VPDSCL_EASF_V_BF_FINAL_MAX_MIN, VPDSCL, id),                                       \
        SRIDFVL(VPDSCL_EASF_H_BF1_PWL_SEG0, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_H_BF1_PWL_SEG1, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_H_BF1_PWL_SEG2, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_H_BF1_PWL_SEG3, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_H_BF1_PWL_SEG4, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_H_BF1_PWL_SEG5, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_H_BF1_PWL_SEG6, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_H_BF1_PWL_SEG7, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_H_BF3_PWL_SEG0, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_H_BF3_PWL_SEG1, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_H_BF3_PWL_SEG2, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_H_BF3_PWL_SEG3, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_H_BF3_PWL_SEG4, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_H_BF3_PWL_SEG5, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_V_BF1_PWL_SEG0, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_V_BF1_PWL_SEG1, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_V_BF1_PWL_SEG2, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_V_BF1_PWL_SEG3, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_V_BF1_PWL_SEG4, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_V_BF1_PWL_SEG5, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_V_BF1_PWL_SEG6, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_V_BF1_PWL_SEG7, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_V_BF3_PWL_SEG0, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_V_BF3_PWL_SEG1, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_V_BF3_PWL_SEG2, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_V_BF3_PWL_SEG3, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_V_BF3_PWL_SEG4, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_V_BF3_PWL_SEG5, VPDSCL, id),                                           \
        SRIDFVL(VPDSCL_EASF_RINGEST_FORCE, VPDSCL, id),                                            \
        SRIDFVL(VPDSCL_EASF_V_RINGEST_3TAP_CNTL1, VPDSCL, id),                                     \
        SRIDFVL(VPDSCL_EASF_V_RINGEST_3TAP_CNTL2, VPDSCL, id),                                     \
        SRIDFVL(VPDSCL_EASF_V_RINGEST_3TAP_CNTL3, VPDSCL, id), SRIDFVL(VPISHARP_MODE, VPDSCL, id), \
        SRIDFVL(VPISHARP_DELTA_CTRL, VPDSCL, id), SRIDFVL(VPISHARP_DELTA_INDEX, VPDSCL, id),       \
        SRIDFVL(VPISHARP_DELTA_DATA, VPDSCL, id), SRIDFVL(VPISHARP_LBA_PWL_SEG0, VPDSCL, id),      \
        SRIDFVL(VPISHARP_LBA_PWL_SEG1, VPDSCL, id), SRIDFVL(VPISHARP_LBA_PWL_SEG2, VPDSCL, id),    \
        SRIDFVL(VPISHARP_LBA_PWL_SEG3, VPDSCL, id), SRIDFVL(VPISHARP_LBA_PWL_SEG4, VPDSCL, id),    \
        SRIDFVL(VPISHARP_LBA_PWL_SEG5, VPDSCL, id),                                                \
        SRIDFVL(VPISHARP_DELTA_LUT_MEM_PWR_CTRL, VPDSCL, id),                                      \
        SRIDFVL(VPISHARP_NLDELTA_SOFT_CLIP, VPDSCL, id),                                           \
        SRIDFVL(VPISHARP_NOISEDET_THRESHOLD, VPDSCL, id),                                          \
        SRIDFVL(VPISHARP_NOISE_GAIN_PWL, VPDSCL, id), SRIDFVL(VPCM_HIST_CNTL, VPCM, id),           \
        SRIDFVL(VPCM_HIST_SCALE_SRC1, VPCM, id), SRIDFVL(VPCM_HIST_SCALE_SRC3, VPCM, id),          \
        SRIDFVL(VPCM_HIST_BIAS_SRC1, VPCM, id), SRIDFVL(VPCM_HIST_BIAS_SRC2, VPCM, id),            \
        SRIDFVL(VPCM_HIST_BIAS_SRC3, VPCM, id), SRIDFVL(VPCM_HIST_COEFA_SRC2, VPCM, id),           \
        SRIDFVL(VPCM_HIST_COEFB_SRC2, VPCM, id), SRIDFVL(VPCM_HIST_COEFC_SRC2, VPCM, id)

#define DPP_REG_LIST_VPE20(id)                                                                     \
    DPP_REG_LIST_VPE20_COMMON(id), SRIDFVL(VPDSCL_VERT_FILTER_INIT_BOT, VPDSCL, id),               \
        SRIDFVL(VPDSCL_VERT_FILTER_INIT_BOT_C, VPDSCL, id),                                        \
        SRIDFVL(VPCNVC_PRE_DEGAM, VPCNVC_CFG, id)

#define DPP_FIELD_LIST_VPE20_COMMON(post_fix)                                                      \
    DPP_FIELD_LIST_VPE10_COMMON(post_fix),                                                         \
        SFRB(ALPHA_2BIT_LUT0, VPCNVC_ALPHA_2BIT_LUT01, post_fix),                                  \
        SFRB(ALPHA_2BIT_LUT1, VPCNVC_ALPHA_2BIT_LUT01, post_fix),                                  \
        SFRB(ALPHA_2BIT_LUT2, VPCNVC_ALPHA_2BIT_LUT23, post_fix),                                  \
        SFRB(ALPHA_2BIT_LUT3, VPCNVC_ALPHA_2BIT_LUT23, post_fix),                                  \
        SFRB(SCL_SC_MATRIX_MODE, VPDSCL_SC_MODE, post_fix),                                        \
        SFRB(SCL_SC_MATRIX_C0, VPDSCL_SC_MATRIX_C0C1, post_fix),                                   \
        SFRB(SCL_SC_MATRIX_C1, VPDSCL_SC_MATRIX_C0C1, post_fix),                                   \
        SFRB(SCL_SC_MATRIX_C2, VPDSCL_SC_MATRIX_C2C3, post_fix),                                   \
        SFRB(SCL_SC_MATRIX_C3, VPDSCL_SC_MATRIX_C2C3, post_fix),                                   \
        SFRB(SCL_SC_LTONL_EN, VPDSCL_SC_MODE, post_fix),                                           \
        SFRB(SCL_EASF_H_EN, VPDSCL_EASF_H_MODE, post_fix),                                         \
        SFRB(SCL_EASF_H_RINGEST_FORCE_EN, VPDSCL_EASF_H_MODE, post_fix),                           \
        SFRB(SCL_EASF_H_2TAP_SHARP_FACTOR, VPDSCL_EASF_H_MODE, post_fix),                          \
        SFRB(SCL_EASF_V_EN, VPDSCL_EASF_V_MODE, post_fix),                                         \
        SFRB(SCL_EASF_V_RINGEST_FORCE_EN, VPDSCL_EASF_V_MODE, post_fix),                           \
        SFRB(SCL_EASF_V_2TAP_SHARP_FACTOR, VPDSCL_EASF_V_MODE, post_fix),                          \
        SFRB(SCL_EASF_H_BF1_EN, VPDSCL_EASF_H_BF_CNTL, post_fix),                                  \
        SFRB(SCL_EASF_H_BF2_MODE, VPDSCL_EASF_H_BF_CNTL, post_fix),                                \
        SFRB(SCL_EASF_H_BF3_MODE, VPDSCL_EASF_H_BF_CNTL, post_fix),                                \
        SFRB(SCL_EASF_H_BF2_FLAT1_GAIN, VPDSCL_EASF_H_BF_CNTL, post_fix),                          \
        SFRB(SCL_EASF_H_BF2_FLAT2_GAIN, VPDSCL_EASF_H_BF_CNTL, post_fix),                          \
        SFRB(SCL_EASF_H_BF2_ROC_GAIN, VPDSCL_EASF_H_BF_CNTL, post_fix),                            \
        SFRB(SCL_EASF_V_BF1_EN, VPDSCL_EASF_V_BF_CNTL, post_fix),                                  \
        SFRB(SCL_EASF_V_BF2_MODE, VPDSCL_EASF_V_BF_CNTL, post_fix),                                \
        SFRB(SCL_EASF_V_BF3_MODE, VPDSCL_EASF_V_BF_CNTL, post_fix),                                \
        SFRB(SCL_EASF_V_BF2_FLAT1_GAIN, VPDSCL_EASF_V_BF_CNTL, post_fix),                          \
        SFRB(SCL_EASF_V_BF2_FLAT2_GAIN, VPDSCL_EASF_V_BF_CNTL, post_fix),                          \
        SFRB(SCL_EASF_V_BF2_ROC_GAIN, VPDSCL_EASF_V_BF_CNTL, post_fix),                            \
        SFRB(SCL_EASF_H_RINGEST_EVENTAP_GAIN1, VPDSCL_EASF_H_RINGEST_EVENTAP_GAIN, post_fix),      \
        SFRB(SCL_EASF_H_RINGEST_EVENTAP_GAIN2, VPDSCL_EASF_H_RINGEST_EVENTAP_GAIN, post_fix),      \
        SFRB(SCL_EASF_H_RINGEST_EVENTAP_REDUCEG1, VPDSCL_EASF_H_RINGEST_EVENTAP_REDUCE, post_fix), \
        SFRB(SCL_EASF_H_RINGEST_EVENTAP_REDUCEG2, VPDSCL_EASF_H_RINGEST_EVENTAP_REDUCE, post_fix), \
        SFRB(SCL_EASF_V_RINGEST_EVENTAP_GAIN1, VPDSCL_EASF_V_RINGEST_EVENTAP_GAIN, post_fix),      \
        SFRB(SCL_EASF_V_RINGEST_EVENTAP_GAIN2, VPDSCL_EASF_V_RINGEST_EVENTAP_GAIN, post_fix),      \
        SFRB(SCL_EASF_V_RINGEST_EVENTAP_REDUCEG1, VPDSCL_EASF_V_RINGEST_EVENTAP_REDUCE, post_fix), \
        SFRB(SCL_EASF_V_RINGEST_EVENTAP_REDUCEG2, VPDSCL_EASF_V_RINGEST_EVENTAP_REDUCE, post_fix), \
        SFRB(SCL_EASF_H_BF_MAXA, VPDSCL_EASF_H_BF_FINAL_MAX_MIN, post_fix),                        \
        SFRB(SCL_EASF_H_BF_MAXB, VPDSCL_EASF_H_BF_FINAL_MAX_MIN, post_fix),                        \
        SFRB(SCL_EASF_H_BF_MINA, VPDSCL_EASF_H_BF_FINAL_MAX_MIN, post_fix),                        \
        SFRB(SCL_EASF_H_BF_MINB, VPDSCL_EASF_H_BF_FINAL_MAX_MIN, post_fix),                        \
        SFRB(SCL_EASF_H_BF1_PWL_IN_SEG0, VPDSCL_EASF_H_BF1_PWL_SEG0, post_fix),                    \
        SFRB(SCL_EASF_H_BF1_PWL_BASE_SEG0, VPDSCL_EASF_H_BF1_PWL_SEG0, post_fix),                  \
        SFRB(SCL_EASF_H_BF1_PWL_SLOPE_SEG0, VPDSCL_EASF_H_BF1_PWL_SEG0, post_fix),                 \
        SFRB(SCL_EASF_H_BF1_PWL_IN_SEG1, VPDSCL_EASF_H_BF1_PWL_SEG1, post_fix),                    \
        SFRB(SCL_EASF_H_BF1_PWL_BASE_SEG1, VPDSCL_EASF_H_BF1_PWL_SEG1, post_fix),                  \
        SFRB(SCL_EASF_H_BF1_PWL_SLOPE_SEG1, VPDSCL_EASF_H_BF1_PWL_SEG1, post_fix),                 \
        SFRB(SCL_EASF_H_BF1_PWL_IN_SEG2, VPDSCL_EASF_H_BF1_PWL_SEG2, post_fix),                    \
        SFRB(SCL_EASF_H_BF1_PWL_BASE_SEG2, VPDSCL_EASF_H_BF1_PWL_SEG2, post_fix),                  \
        SFRB(SCL_EASF_H_BF1_PWL_SLOPE_SEG2, VPDSCL_EASF_H_BF1_PWL_SEG2, post_fix),                 \
        SFRB(SCL_EASF_H_BF1_PWL_IN_SEG3, VPDSCL_EASF_H_BF1_PWL_SEG3, post_fix),                    \
        SFRB(SCL_EASF_H_BF1_PWL_BASE_SEG3, VPDSCL_EASF_H_BF1_PWL_SEG3, post_fix),                  \
        SFRB(SCL_EASF_H_BF1_PWL_SLOPE_SEG3, VPDSCL_EASF_H_BF1_PWL_SEG3, post_fix),                 \
        SFRB(SCL_EASF_H_BF1_PWL_IN_SEG4, VPDSCL_EASF_H_BF1_PWL_SEG4, post_fix),                    \
        SFRB(SCL_EASF_H_BF1_PWL_BASE_SEG4, VPDSCL_EASF_H_BF1_PWL_SEG4, post_fix),                  \
        SFRB(SCL_EASF_H_BF1_PWL_SLOPE_SEG4, VPDSCL_EASF_H_BF1_PWL_SEG4, post_fix),                 \
        SFRB(SCL_EASF_H_BF1_PWL_IN_SEG5, VPDSCL_EASF_H_BF1_PWL_SEG5, post_fix),                    \
        SFRB(SCL_EASF_H_BF1_PWL_BASE_SEG5, VPDSCL_EASF_H_BF1_PWL_SEG5, post_fix),                  \
        SFRB(SCL_EASF_H_BF1_PWL_SLOPE_SEG5, VPDSCL_EASF_H_BF1_PWL_SEG5, post_fix),                 \
        SFRB(SCL_EASF_H_BF1_PWL_IN_SEG6, VPDSCL_EASF_H_BF1_PWL_SEG6, post_fix),                    \
        SFRB(SCL_EASF_H_BF1_PWL_BASE_SEG6, VPDSCL_EASF_H_BF1_PWL_SEG6, post_fix),                  \
        SFRB(SCL_EASF_H_BF1_PWL_SLOPE_SEG6, VPDSCL_EASF_H_BF1_PWL_SEG6, post_fix),                 \
        SFRB(SCL_EASF_H_BF1_PWL_IN_SEG7, VPDSCL_EASF_H_BF1_PWL_SEG7, post_fix),                    \
        SFRB(SCL_EASF_H_BF1_PWL_BASE_SEG7, VPDSCL_EASF_H_BF1_PWL_SEG7, post_fix),                  \
        SFRB(SCL_EASF_H_BF3_PWL_IN_SEG0, VPDSCL_EASF_H_BF3_PWL_SEG0, post_fix),                    \
        SFRB(SCL_EASF_H_BF3_PWL_BASE_SEG0, VPDSCL_EASF_H_BF3_PWL_SEG0, post_fix),                  \
        SFRB(SCL_EASF_H_BF3_PWL_SLOPE_SEG0, VPDSCL_EASF_H_BF3_PWL_SEG0, post_fix),                 \
        SFRB(SCL_EASF_H_BF3_PWL_IN_SEG1, VPDSCL_EASF_H_BF3_PWL_SEG1, post_fix),                    \
        SFRB(SCL_EASF_H_BF3_PWL_BASE_SEG1, VPDSCL_EASF_H_BF3_PWL_SEG1, post_fix),                  \
        SFRB(SCL_EASF_H_BF3_PWL_SLOPE_SEG1, VPDSCL_EASF_H_BF3_PWL_SEG1, post_fix),                 \
        SFRB(SCL_EASF_H_BF3_PWL_IN_SEG2, VPDSCL_EASF_H_BF3_PWL_SEG2, post_fix),                    \
        SFRB(SCL_EASF_H_BF3_PWL_BASE_SEG2, VPDSCL_EASF_H_BF3_PWL_SEG2, post_fix),                  \
        SFRB(SCL_EASF_H_BF3_PWL_SLOPE_SEG2, VPDSCL_EASF_H_BF3_PWL_SEG2, post_fix),                 \
        SFRB(SCL_EASF_H_BF3_PWL_IN_SEG3, VPDSCL_EASF_H_BF3_PWL_SEG3, post_fix),                    \
        SFRB(SCL_EASF_H_BF3_PWL_BASE_SEG3, VPDSCL_EASF_H_BF3_PWL_SEG3, post_fix),                  \
        SFRB(SCL_EASF_H_BF3_PWL_SLOPE_SEG3, VPDSCL_EASF_H_BF3_PWL_SEG3, post_fix),                 \
        SFRB(SCL_EASF_H_BF3_PWL_IN_SEG4, VPDSCL_EASF_H_BF3_PWL_SEG4, post_fix),                    \
        SFRB(SCL_EASF_H_BF3_PWL_BASE_SEG4, VPDSCL_EASF_H_BF3_PWL_SEG4, post_fix),                  \
        SFRB(SCL_EASF_H_BF3_PWL_SLOPE_SEG4, VPDSCL_EASF_H_BF3_PWL_SEG4, post_fix),                 \
        SFRB(SCL_EASF_H_BF3_PWL_IN_SEG5, VPDSCL_EASF_H_BF3_PWL_SEG5, post_fix),                    \
        SFRB(SCL_EASF_H_BF3_PWL_BASE_SEG5, VPDSCL_EASF_H_BF3_PWL_SEG5, post_fix),                  \
        SFRB(SCL_EASF_V_BF1_PWL_IN_SEG0, VPDSCL_EASF_V_BF1_PWL_SEG0, post_fix),                    \
        SFRB(SCL_EASF_V_BF1_PWL_BASE_SEG0, VPDSCL_EASF_V_BF1_PWL_SEG0, post_fix),                  \
        SFRB(SCL_EASF_V_BF1_PWL_SLOPE_SEG0, VPDSCL_EASF_V_BF1_PWL_SEG0, post_fix),                 \
        SFRB(SCL_EASF_V_BF1_PWL_IN_SEG1, VPDSCL_EASF_V_BF1_PWL_SEG1, post_fix),                    \
        SFRB(SCL_EASF_V_BF1_PWL_BASE_SEG1, VPDSCL_EASF_V_BF1_PWL_SEG1, post_fix),                  \
        SFRB(SCL_EASF_V_BF1_PWL_SLOPE_SEG1, VPDSCL_EASF_V_BF1_PWL_SEG1, post_fix),                 \
        SFRB(SCL_EASF_V_BF1_PWL_IN_SEG2, VPDSCL_EASF_V_BF1_PWL_SEG2, post_fix),                    \
        SFRB(SCL_EASF_V_BF1_PWL_BASE_SEG2, VPDSCL_EASF_V_BF1_PWL_SEG2, post_fix),                  \
        SFRB(SCL_EASF_V_BF1_PWL_SLOPE_SEG2, VPDSCL_EASF_V_BF1_PWL_SEG2, post_fix),                 \
        SFRB(SCL_EASF_V_BF1_PWL_IN_SEG3, VPDSCL_EASF_V_BF1_PWL_SEG3, post_fix),                    \
        SFRB(SCL_EASF_V_BF1_PWL_BASE_SEG3, VPDSCL_EASF_V_BF1_PWL_SEG3, post_fix),                  \
        SFRB(SCL_EASF_V_BF1_PWL_SLOPE_SEG3, VPDSCL_EASF_V_BF1_PWL_SEG3, post_fix),                 \
        SFRB(SCL_EASF_V_BF1_PWL_IN_SEG4, VPDSCL_EASF_V_BF1_PWL_SEG4, post_fix),                    \
        SFRB(SCL_EASF_V_BF1_PWL_BASE_SEG4, VPDSCL_EASF_V_BF1_PWL_SEG4, post_fix),                  \
        SFRB(SCL_EASF_V_BF1_PWL_SLOPE_SEG4, VPDSCL_EASF_V_BF1_PWL_SEG4, post_fix),                 \
        SFRB(SCL_EASF_V_BF1_PWL_IN_SEG5, VPDSCL_EASF_V_BF1_PWL_SEG5, post_fix),                    \
        SFRB(SCL_EASF_V_BF1_PWL_BASE_SEG5, VPDSCL_EASF_V_BF1_PWL_SEG5, post_fix),                  \
        SFRB(SCL_EASF_V_BF1_PWL_SLOPE_SEG5, VPDSCL_EASF_V_BF1_PWL_SEG5, post_fix),                 \
        SFRB(SCL_EASF_V_BF1_PWL_IN_SEG6, VPDSCL_EASF_V_BF1_PWL_SEG6, post_fix),                    \
        SFRB(SCL_EASF_V_BF1_PWL_BASE_SEG6, VPDSCL_EASF_V_BF1_PWL_SEG6, post_fix),                  \
        SFRB(SCL_EASF_V_BF1_PWL_SLOPE_SEG6, VPDSCL_EASF_V_BF1_PWL_SEG6, post_fix),                 \
        SFRB(SCL_EASF_V_BF1_PWL_IN_SEG7, VPDSCL_EASF_V_BF1_PWL_SEG7, post_fix),                    \
        SFRB(SCL_EASF_V_BF1_PWL_BASE_SEG7, VPDSCL_EASF_V_BF1_PWL_SEG7, post_fix),                  \
        SFRB(SCL_EASF_V_BF3_PWL_IN_SEG0, VPDSCL_EASF_V_BF3_PWL_SEG0, post_fix),                    \
        SFRB(SCL_EASF_V_BF3_PWL_BASE_SEG0, VPDSCL_EASF_V_BF3_PWL_SEG0, post_fix),                  \
        SFRB(SCL_EASF_V_BF3_PWL_SLOPE_SEG0, VPDSCL_EASF_V_BF3_PWL_SEG0, post_fix),                 \
        SFRB(SCL_EASF_V_BF3_PWL_IN_SEG1, VPDSCL_EASF_V_BF3_PWL_SEG1, post_fix),                    \
        SFRB(SCL_EASF_V_BF3_PWL_BASE_SEG1, VPDSCL_EASF_V_BF3_PWL_SEG1, post_fix),                  \
        SFRB(SCL_EASF_V_BF3_PWL_SLOPE_SEG1, VPDSCL_EASF_V_BF3_PWL_SEG1, post_fix),                 \
        SFRB(SCL_EASF_V_BF3_PWL_IN_SEG2, VPDSCL_EASF_V_BF3_PWL_SEG2, post_fix),                    \
        SFRB(SCL_EASF_V_BF3_PWL_BASE_SEG2, VPDSCL_EASF_V_BF3_PWL_SEG2, post_fix),                  \
        SFRB(SCL_EASF_V_BF3_PWL_SLOPE_SEG2, VPDSCL_EASF_V_BF3_PWL_SEG2, post_fix),                 \
        SFRB(SCL_EASF_V_BF3_PWL_IN_SEG3, VPDSCL_EASF_V_BF3_PWL_SEG3, post_fix),                    \
        SFRB(SCL_EASF_V_BF3_PWL_BASE_SEG3, VPDSCL_EASF_V_BF3_PWL_SEG3, post_fix),                  \
        SFRB(SCL_EASF_V_BF3_PWL_SLOPE_SEG3, VPDSCL_EASF_V_BF3_PWL_SEG3, post_fix),                 \
        SFRB(SCL_EASF_V_BF3_PWL_IN_SEG4, VPDSCL_EASF_V_BF3_PWL_SEG4, post_fix),                    \
        SFRB(SCL_EASF_V_BF3_PWL_BASE_SEG4, VPDSCL_EASF_V_BF3_PWL_SEG4, post_fix),                  \
        SFRB(SCL_EASF_V_BF3_PWL_SLOPE_SEG4, VPDSCL_EASF_V_BF3_PWL_SEG4, post_fix),                 \
        SFRB(SCL_EASF_V_BF3_PWL_IN_SEG5, VPDSCL_EASF_V_BF3_PWL_SEG5, post_fix),                    \
        SFRB(SCL_EASF_V_BF3_PWL_BASE_SEG5, VPDSCL_EASF_V_BF3_PWL_SEG5, post_fix),                  \
        SFRB(SCL_EASF_H_RINGEST_FORCE, VPDSCL_EASF_RINGEST_FORCE, post_fix),                       \
        SFRB(SCL_EASF_V_RINGEST_FORCE, VPDSCL_EASF_RINGEST_FORCE, post_fix),                       \
        SFRB(SCL_EASF_V_RINGEST_3TAP_DNTILT_UPTILT, VPDSCL_EASF_V_RINGEST_3TAP_CNTL1, post_fix),   \
        SFRB(SCL_EASF_V_RINGEST_3TAP_UPTILT_MAXVAL, VPDSCL_EASF_V_RINGEST_3TAP_CNTL1, post_fix),   \
        SFRB(SCL_EASF_V_RINGEST_3TAP_DNTILT_SLOPE, VPDSCL_EASF_V_RINGEST_3TAP_CNTL2, post_fix),    \
        SFRB(SCL_EASF_V_RINGEST_3TAP_UPTILT1_SLOPE, VPDSCL_EASF_V_RINGEST_3TAP_CNTL2, post_fix),   \
        SFRB(SCL_EASF_V_RINGEST_3TAP_UPTILT2_SLOPE, VPDSCL_EASF_V_RINGEST_3TAP_CNTL3, post_fix),   \
        SFRB(SCL_EASF_V_RINGEST_3TAP_UPTILT2_OFFSET, VPDSCL_EASF_V_RINGEST_3TAP_CNTL3, post_fix),  \
        SFRB(ISHARP_EN, VPISHARP_MODE, post_fix),                                                  \
        SFRB(ISHARP_NOISEDET_EN, VPISHARP_MODE, post_fix),                                         \
        SFRB(ISHARP_NOISEDET_MODE, VPISHARP_MODE, post_fix),                                       \
        SFRB(ISHARP_LBA_MODE, VPISHARP_MODE, post_fix),                                            \
        SFRB(ISHARP_DELTA_LUT_SELECT_CURRENT, VPISHARP_MODE, post_fix),                            \
        SFRB(ISHARP_FMT_MODE, VPISHARP_MODE, post_fix),                                            \
        SFRB(ISHARP_FMT_NORM, VPISHARP_MODE, post_fix),                                            \
        SFRB(ISHARP_DELTA_LUT_HOST_SELECT, VPISHARP_DELTA_CTRL, post_fix),                         \
        SFRB(ISHARP_DELTA_INDEX, VPISHARP_DELTA_INDEX, post_fix),                                  \
        SFRB(ISHARP_DELTA_DATA, VPISHARP_DELTA_DATA, post_fix),                                    \
        SFRB(ISHARP_LBA_PWL_IN_SEG0, VPISHARP_LBA_PWL_SEG0, post_fix),                             \
        SFRB(ISHARP_LBA_PWL_BASE_SEG0, VPISHARP_LBA_PWL_SEG0, post_fix),                           \
        SFRB(ISHARP_LBA_PWL_SLOPE_SEG0, VPISHARP_LBA_PWL_SEG0, post_fix),                          \
        SFRB(ISHARP_LBA_PWL_IN_SEG1, VPISHARP_LBA_PWL_SEG1, post_fix),                             \
        SFRB(ISHARP_LBA_PWL_BASE_SEG1, VPISHARP_LBA_PWL_SEG1, post_fix),                           \
        SFRB(ISHARP_LBA_PWL_SLOPE_SEG1, VPISHARP_LBA_PWL_SEG1, post_fix),                          \
        SFRB(ISHARP_LBA_PWL_IN_SEG2, VPISHARP_LBA_PWL_SEG2, post_fix),                             \
        SFRB(ISHARP_LBA_PWL_BASE_SEG2, VPISHARP_LBA_PWL_SEG2, post_fix),                           \
        SFRB(ISHARP_LBA_PWL_SLOPE_SEG2, VPISHARP_LBA_PWL_SEG2, post_fix),                          \
        SFRB(ISHARP_LBA_PWL_IN_SEG3, VPISHARP_LBA_PWL_SEG3, post_fix),                             \
        SFRB(ISHARP_LBA_PWL_BASE_SEG3, VPISHARP_LBA_PWL_SEG3, post_fix),                           \
        SFRB(ISHARP_LBA_PWL_SLOPE_SEG3, VPISHARP_LBA_PWL_SEG3, post_fix),                          \
        SFRB(ISHARP_LBA_PWL_IN_SEG4, VPISHARP_LBA_PWL_SEG4, post_fix),                             \
        SFRB(ISHARP_LBA_PWL_BASE_SEG4, VPISHARP_LBA_PWL_SEG4, post_fix),                           \
        SFRB(ISHARP_LBA_PWL_SLOPE_SEG4, VPISHARP_LBA_PWL_SEG4, post_fix),                          \
        SFRB(ISHARP_LBA_PWL_IN_SEG5, VPISHARP_LBA_PWL_SEG5, post_fix),                             \
        SFRB(ISHARP_LBA_PWL_BASE_SEG5, VPISHARP_LBA_PWL_SEG5, post_fix),                           \
        SFRB(ISHARP_DELTA_LUT_MEM_PWR_FORCE, VPISHARP_DELTA_LUT_MEM_PWR_CTRL, post_fix),           \
        SFRB(ISHARP_DELTA_LUT_MEM_PWR_DIS, VPISHARP_DELTA_LUT_MEM_PWR_CTRL, post_fix),             \
        SFRB(ISHARP_DELTA_LUT_MEM_PWR_STATE, VPISHARP_DELTA_LUT_MEM_PWR_CTRL, post_fix),           \
        SFRB(ISHARP_NLDELTA_SCLIP_EN_P, VPISHARP_NLDELTA_SOFT_CLIP, post_fix),                     \
        SFRB(ISHARP_NLDELTA_SCLIP_PIVOT_P, VPISHARP_NLDELTA_SOFT_CLIP, post_fix),                  \
        SFRB(ISHARP_NLDELTA_SCLIP_SLOPE_P, VPISHARP_NLDELTA_SOFT_CLIP, post_fix),                  \
        SFRB(ISHARP_NLDELTA_SCLIP_EN_N, VPISHARP_NLDELTA_SOFT_CLIP, post_fix),                     \
        SFRB(ISHARP_NLDELTA_SCLIP_PIVOT_N, VPISHARP_NLDELTA_SOFT_CLIP, post_fix),                  \
        SFRB(ISHARP_NLDELTA_SCLIP_SLOPE_N, VPISHARP_NLDELTA_SOFT_CLIP, post_fix),                  \
        SFRB(ISHARP_NOISEDET_UTHRE, VPISHARP_NOISEDET_THRESHOLD, post_fix),                        \
        SFRB(ISHARP_NOISEDET_DTHRE, VPISHARP_NOISEDET_THRESHOLD, post_fix),                        \
        SFRB(ISHARP_NOISEDET_PWL_START_IN, VPISHARP_NOISE_GAIN_PWL, post_fix),                     \
        SFRB(ISHARP_NOISEDET_PWL_END_IN, VPISHARP_NOISE_GAIN_PWL, post_fix),                       \
        SFRB(ISHARP_NOISEDET_PWL_SLOPE, VPISHARP_NOISE_GAIN_PWL, post_fix),                        \
        SFRB(LUMA_KEYER_EN, VPCNVC_COLOR_KEYER_CONTROL, post_fix),                                 \
        SFRB(VPCM_HIST_CH_EN, VPCM_HIST_CNTL, post_fix),                                           \
        SFRB(VPCM_HIST_SRC1_SEL, VPCM_HIST_CNTL, post_fix),                                        \
        SFRB(VPCM_HIST_SRC2_SEL, VPCM_HIST_CNTL, post_fix),                                        \
        SFRB(VPCM_HIST_SRC3_SEL, VPCM_HIST_CNTL, post_fix),                                        \
        SFRB(VPCM_HIST_SEL, VPCM_HIST_CNTL, post_fix),                                             \
        SFRB(VPCM_HIST_CH1_XBAR, VPCM_HIST_CNTL, post_fix),                                        \
        SFRB(VPCM_HIST_CH2_XBAR, VPCM_HIST_CNTL, post_fix),                                        \
        SFRB(VPCM_HIST_CH3_XBAR, VPCM_HIST_CNTL, post_fix),                                        \
        SFRB(VPCM_HIST_FORMAT, VPCM_HIST_CNTL, post_fix),                                          \
        SFRB(VPCM_HIST_READ_CHANNEL_MASK, VPCM_HIST_CNTL, post_fix),                               \
        SFRB(VPCM_HIST_SCALE_SRC1, VPCM_HIST_SCALE_SRC1, post_fix),                                \
        SFRB(VPCM_HIST_SCALE_SRC3, VPCM_HIST_SCALE_SRC3, post_fix),                                \
        SFRB(VPCM_HIST_BIAS_SRC1, VPCM_HIST_BIAS_SRC1, post_fix),                                  \
        SFRB(VPCM_HIST_BIAS_SRC2, VPCM_HIST_BIAS_SRC2, post_fix),                                  \
        SFRB(VPCM_HIST_BIAS_SRC3, VPCM_HIST_BIAS_SRC3, post_fix),                                  \
        SFRB(VPCM_HIST_COEFA_SRC2, VPCM_HIST_COEFA_SRC2, post_fix),                                \
        SFRB(VPCM_HIST_COEFB_SRC2, VPCM_HIST_COEFB_SRC2, post_fix),                                \
        SFRB(VPCM_HIST_COEFC_SRC2, VPCM_HIST_COEFC_SRC2, post_fix)
#define DPP_FIELD_LIST_VPE20(post_fix)                                                             \
    DPP_FIELD_LIST_VPE20_COMMON(post_fix),                                                         \
        SFRB(VPCM_GAMCOR_LUT_CONFIG_MODE, VPCM_GAMCOR_LUT_CONTROL, post_fix),                      \
        SFRB(SCL_V_INIT_FRAC_BOT, VPDSCL_VERT_FILTER_INIT_BOT, post_fix),                          \
        SFRB(SCL_V_INIT_INT_BOT, VPDSCL_VERT_FILTER_INIT_BOT, post_fix),                           \
        SFRB(SCL_V_INIT_FRAC_BOT_C, VPDSCL_VERT_FILTER_INIT_BOT_C, post_fix),                      \
        SFRB(SCL_V_INIT_INT_BOT_C, VPDSCL_VERT_FILTER_INIT_BOT_C, post_fix),                       \
        SFRB(PRE_DEGAM_MODE, VPCNVC_PRE_DEGAM, post_fix),                                          \
        SFRB(PRE_DEGAM_SELECT, VPCNVC_PRE_DEGAM, post_fix)

#define DPP_REG_VARIABLE_LIST_VPE20_COMMON                                                         \
    DPP_REG_VARIABLE_LIST_VPE10_COMMON                                                             \
    reg_id_val VPCNVC_ALPHA_2BIT_LUT01;                                                            \
    reg_id_val VPCNVC_ALPHA_2BIT_LUT23;                                                            \
    reg_id_val VPDSCL_SC_MODE;                                                                     \
    reg_id_val VPDSCL_SC_MATRIX_C0C1;                                                              \
    reg_id_val VPDSCL_SC_MATRIX_C2C3;                                                              \
    reg_id_val VPDSCL_EASF_H_MODE;                                                                 \
    reg_id_val VPDSCL_EASF_V_MODE;                                                                 \
    reg_id_val VPDSCL_EASF_H_BF_CNTL;                                                              \
    reg_id_val VPDSCL_EASF_V_BF_CNTL;                                                              \
    reg_id_val VPDSCL_EASF_H_RINGEST_EVENTAP_GAIN;                                                 \
    reg_id_val VPDSCL_EASF_H_RINGEST_EVENTAP_REDUCE;                                               \
    reg_id_val VPDSCL_EASF_V_RINGEST_EVENTAP_GAIN;                                                 \
    reg_id_val VPDSCL_EASF_V_RINGEST_EVENTAP_REDUCE;                                               \
    reg_id_val VPDSCL_EASF_H_BF_FINAL_MAX_MIN;                                                     \
    reg_id_val VPDSCL_EASF_V_BF_FINAL_MAX_MIN;                                                     \
    reg_id_val VPDSCL_EASF_H_BF1_PWL_SEG0;                                                         \
    reg_id_val VPDSCL_EASF_H_BF1_PWL_SEG1;                                                         \
    reg_id_val VPDSCL_EASF_H_BF1_PWL_SEG2;                                                         \
    reg_id_val VPDSCL_EASF_H_BF1_PWL_SEG3;                                                         \
    reg_id_val VPDSCL_EASF_H_BF1_PWL_SEG4;                                                         \
    reg_id_val VPDSCL_EASF_H_BF1_PWL_SEG5;                                                         \
    reg_id_val VPDSCL_EASF_H_BF1_PWL_SEG6;                                                         \
    reg_id_val VPDSCL_EASF_H_BF1_PWL_SEG7;                                                         \
    reg_id_val VPDSCL_EASF_H_BF3_PWL_SEG0;                                                         \
    reg_id_val VPDSCL_EASF_H_BF3_PWL_SEG1;                                                         \
    reg_id_val VPDSCL_EASF_H_BF3_PWL_SEG2;                                                         \
    reg_id_val VPDSCL_EASF_H_BF3_PWL_SEG3;                                                         \
    reg_id_val VPDSCL_EASF_H_BF3_PWL_SEG4;                                                         \
    reg_id_val VPDSCL_EASF_H_BF3_PWL_SEG5;                                                         \
    reg_id_val VPDSCL_EASF_V_BF1_PWL_SEG0;                                                         \
    reg_id_val VPDSCL_EASF_V_BF1_PWL_SEG1;                                                         \
    reg_id_val VPDSCL_EASF_V_BF1_PWL_SEG2;                                                         \
    reg_id_val VPDSCL_EASF_V_BF1_PWL_SEG3;                                                         \
    reg_id_val VPDSCL_EASF_V_BF1_PWL_SEG4;                                                         \
    reg_id_val VPDSCL_EASF_V_BF1_PWL_SEG5;                                                         \
    reg_id_val VPDSCL_EASF_V_BF1_PWL_SEG6;                                                         \
    reg_id_val VPDSCL_EASF_V_BF1_PWL_SEG7;                                                         \
    reg_id_val VPDSCL_EASF_V_BF3_PWL_SEG0;                                                         \
    reg_id_val VPDSCL_EASF_V_BF3_PWL_SEG1;                                                         \
    reg_id_val VPDSCL_EASF_V_BF3_PWL_SEG2;                                                         \
    reg_id_val VPDSCL_EASF_V_BF3_PWL_SEG3;                                                         \
    reg_id_val VPDSCL_EASF_V_BF3_PWL_SEG4;                                                         \
    reg_id_val VPDSCL_EASF_V_BF3_PWL_SEG5;                                                         \
    reg_id_val VPDSCL_EASF_RINGEST_FORCE;                                                          \
    reg_id_val VPDSCL_EASF_V_RINGEST_3TAP_CNTL1;                                                   \
    reg_id_val VPDSCL_EASF_V_RINGEST_3TAP_CNTL2;                                                   \
    reg_id_val VPDSCL_EASF_V_RINGEST_3TAP_CNTL3;                                                   \
    reg_id_val VPISHARP_MODE;                                                                      \
    reg_id_val VPISHARP_DELTA_CTRL;                                                                \
    reg_id_val VPISHARP_DELTA_INDEX;                                                               \
    reg_id_val VPISHARP_DELTA_DATA;                                                                \
    reg_id_val VPISHARP_LBA_PWL_SEG0;                                                              \
    reg_id_val VPISHARP_LBA_PWL_SEG1;                                                              \
    reg_id_val VPISHARP_LBA_PWL_SEG2;                                                              \
    reg_id_val VPISHARP_LBA_PWL_SEG3;                                                              \
    reg_id_val VPISHARP_LBA_PWL_SEG4;                                                              \
    reg_id_val VPISHARP_LBA_PWL_SEG5;                                                              \
    reg_id_val VPISHARP_DELTA_LUT_MEM_PWR_CTRL;                                                    \
    reg_id_val VPISHARP_NLDELTA_SOFT_CLIP;                                                         \
    reg_id_val VPISHARP_NOISEDET_THRESHOLD;                                                        \
    reg_id_val VPISHARP_NOISE_GAIN_PWL;                                                            \
    reg_id_val VPCM_HIST_CNTL;                                                                     \
    reg_id_val VPCM_HIST_SCALE_SRC1;                                                               \
    reg_id_val VPCM_HIST_SCALE_SRC3;                                                               \
    reg_id_val VPCM_HIST_BIAS_SRC1;                                                                \
    reg_id_val VPCM_HIST_BIAS_SRC2;                                                                \
    reg_id_val VPCM_HIST_BIAS_SRC3;                                                                \
    reg_id_val VPCM_HIST_COEFA_SRC2;                                                               \
    reg_id_val VPCM_HIST_COEFB_SRC2;                                                               \
    reg_id_val VPCM_HIST_COEFC_SRC2;

#define DPP_REG_VARIABLE_LIST_VPE20                                                                \
    DPP_REG_VARIABLE_LIST_VPE20_COMMON                                                             \
    reg_id_val VPDSCL_VERT_FILTER_INIT_BOT;                                                        \
    reg_id_val VPDSCL_VERT_FILTER_INIT_BOT_C;                                                      \
    reg_id_val VPCNVC_PRE_DEGAM;

#define DPP_FIELD_VARIABLE_LIST_VPE20_COMMON(type)                                                 \
    DPP_FIELD_VARIABLE_LIST_VPE10_COMMON(type)                                                     \
    type SCL_SC_MATRIX_MODE;                                                                       \
    type SCL_SC_LTONL_EN;                                                                          \
    type SCL_SC_MATRIX_C0;                                                                         \
    type SCL_SC_MATRIX_C1;                                                                         \
    type SCL_SC_MATRIX_C2;                                                                         \
    type SCL_SC_MATRIX_C3;                                                                         \
    type SCL_EASF_H_EN;                                                                            \
    type SCL_EASF_H_RINGEST_FORCE_EN;                                                              \
    type SCL_EASF_H_2TAP_SHARP_FACTOR;                                                             \
    type SCL_EASF_V_EN;                                                                            \
    type SCL_EASF_V_RINGEST_FORCE_EN;                                                              \
    type SCL_EASF_V_2TAP_SHARP_FACTOR;                                                             \
    type SCL_EASF_H_BF1_EN;                                                                        \
    type SCL_EASF_H_BF2_MODE;                                                                      \
    type SCL_EASF_H_BF3_MODE;                                                                      \
    type SCL_EASF_H_BF2_FLAT1_GAIN;                                                                \
    type SCL_EASF_H_BF2_FLAT2_GAIN;                                                                \
    type SCL_EASF_H_BF2_ROC_GAIN;                                                                  \
    type SCL_EASF_V_BF1_EN;                                                                        \
    type SCL_EASF_V_BF2_MODE;                                                                      \
    type SCL_EASF_V_BF3_MODE;                                                                      \
    type SCL_EASF_V_BF2_FLAT1_GAIN;                                                                \
    type SCL_EASF_V_BF2_FLAT2_GAIN;                                                                \
    type SCL_EASF_V_BF2_ROC_GAIN;                                                                  \
    type SCL_EASF_H_RINGEST_EVENTAP_GAIN1;                                                         \
    type SCL_EASF_H_RINGEST_EVENTAP_GAIN2;                                                         \
    type SCL_EASF_H_RINGEST_EVENTAP_REDUCEG1;                                                      \
    type SCL_EASF_H_RINGEST_EVENTAP_REDUCEG2;                                                      \
    type SCL_EASF_V_RINGEST_EVENTAP_GAIN1;                                                         \
    type SCL_EASF_V_RINGEST_EVENTAP_GAIN2;                                                         \
    type SCL_EASF_V_RINGEST_EVENTAP_REDUCEG1;                                                      \
    type SCL_EASF_V_RINGEST_EVENTAP_REDUCEG2;                                                      \
    type SCL_EASF_H_BF_MAXA;                                                                       \
    type SCL_EASF_H_BF_MAXB;                                                                       \
    type SCL_EASF_H_BF_MINA;                                                                       \
    type SCL_EASF_H_BF_MINB;                                                                       \
    type SCL_EASF_V_BF_MAXA;                                                                       \
    type SCL_EASF_V_BF_MAXB;                                                                       \
    type SCL_EASF_V_BF_MINA;                                                                       \
    type SCL_EASF_V_BF_MINB;                                                                       \
    type SCL_EASF_H_BF1_PWL_IN_SEG0;                                                               \
    type SCL_EASF_H_BF1_PWL_BASE_SEG0;                                                             \
    type SCL_EASF_H_BF1_PWL_SLOPE_SEG0;                                                            \
    type SCL_EASF_H_BF1_PWL_IN_SEG1;                                                               \
    type SCL_EASF_H_BF1_PWL_BASE_SEG1;                                                             \
    type SCL_EASF_H_BF1_PWL_SLOPE_SEG1;                                                            \
    type SCL_EASF_H_BF1_PWL_IN_SEG2;                                                               \
    type SCL_EASF_H_BF1_PWL_BASE_SEG2;                                                             \
    type SCL_EASF_H_BF1_PWL_SLOPE_SEG2;                                                            \
    type SCL_EASF_H_BF1_PWL_IN_SEG3;                                                               \
    type SCL_EASF_H_BF1_PWL_BASE_SEG3;                                                             \
    type SCL_EASF_H_BF1_PWL_SLOPE_SEG3;                                                            \
    type SCL_EASF_H_BF1_PWL_IN_SEG4;                                                               \
    type SCL_EASF_H_BF1_PWL_BASE_SEG4;                                                             \
    type SCL_EASF_H_BF1_PWL_SLOPE_SEG4;                                                            \
    type SCL_EASF_H_BF1_PWL_IN_SEG5;                                                               \
    type SCL_EASF_H_BF1_PWL_BASE_SEG5;                                                             \
    type SCL_EASF_H_BF1_PWL_SLOPE_SEG5;                                                            \
    type SCL_EASF_H_BF1_PWL_IN_SEG6;                                                               \
    type SCL_EASF_H_BF1_PWL_BASE_SEG6;                                                             \
    type SCL_EASF_H_BF1_PWL_SLOPE_SEG6;                                                            \
    type SCL_EASF_H_BF1_PWL_IN_SEG7;                                                               \
    type SCL_EASF_H_BF1_PWL_BASE_SEG7;                                                             \
    type SCL_EASF_H_BF3_PWL_IN_SEG0;                                                               \
    type SCL_EASF_H_BF3_PWL_BASE_SEG0;                                                             \
    type SCL_EASF_H_BF3_PWL_SLOPE_SEG0;                                                            \
    type SCL_EASF_H_BF3_PWL_IN_SEG1;                                                               \
    type SCL_EASF_H_BF3_PWL_BASE_SEG1;                                                             \
    type SCL_EASF_H_BF3_PWL_SLOPE_SEG1;                                                            \
    type SCL_EASF_H_BF3_PWL_IN_SEG2;                                                               \
    type SCL_EASF_H_BF3_PWL_BASE_SEG2;                                                             \
    type SCL_EASF_H_BF3_PWL_SLOPE_SEG2;                                                            \
    type SCL_EASF_H_BF3_PWL_IN_SEG3;                                                               \
    type SCL_EASF_H_BF3_PWL_BASE_SEG3;                                                             \
    type SCL_EASF_H_BF3_PWL_SLOPE_SEG3;                                                            \
    type SCL_EASF_H_BF3_PWL_IN_SEG4;                                                               \
    type SCL_EASF_H_BF3_PWL_BASE_SEG4;                                                             \
    type SCL_EASF_H_BF3_PWL_SLOPE_SEG4;                                                            \
    type SCL_EASF_H_BF3_PWL_IN_SEG5;                                                               \
    type SCL_EASF_H_BF3_PWL_BASE_SEG5;                                                             \
    type SCL_EASF_V_BF1_PWL_IN_SEG0;                                                               \
    type SCL_EASF_V_BF1_PWL_BASE_SEG0;                                                             \
    type SCL_EASF_V_BF1_PWL_SLOPE_SEG0;                                                            \
    type SCL_EASF_V_BF1_PWL_IN_SEG1;                                                               \
    type SCL_EASF_V_BF1_PWL_BASE_SEG1;                                                             \
    type SCL_EASF_V_BF1_PWL_SLOPE_SEG1;                                                            \
    type SCL_EASF_V_BF1_PWL_IN_SEG2;                                                               \
    type SCL_EASF_V_BF1_PWL_BASE_SEG2;                                                             \
    type SCL_EASF_V_BF1_PWL_SLOPE_SEG2;                                                            \
    type SCL_EASF_V_BF1_PWL_IN_SEG3;                                                               \
    type SCL_EASF_V_BF1_PWL_BASE_SEG3;                                                             \
    type SCL_EASF_V_BF1_PWL_SLOPE_SEG3;                                                            \
    type SCL_EASF_V_BF1_PWL_IN_SEG4;                                                               \
    type SCL_EASF_V_BF1_PWL_BASE_SEG4;                                                             \
    type SCL_EASF_V_BF1_PWL_SLOPE_SEG4;                                                            \
    type SCL_EASF_V_BF1_PWL_IN_SEG5;                                                               \
    type SCL_EASF_V_BF1_PWL_BASE_SEG5;                                                             \
    type SCL_EASF_V_BF1_PWL_SLOPE_SEG5;                                                            \
    type SCL_EASF_V_BF1_PWL_IN_SEG6;                                                               \
    type SCL_EASF_V_BF1_PWL_BASE_SEG6;                                                             \
    type SCL_EASF_V_BF1_PWL_SLOPE_SEG6;                                                            \
    type SCL_EASF_V_BF1_PWL_IN_SEG7;                                                               \
    type SCL_EASF_V_BF1_PWL_BASE_SEG7;                                                             \
    type SCL_EASF_V_BF3_PWL_IN_SEG0;                                                               \
    type SCL_EASF_V_BF3_PWL_BASE_SEG0;                                                             \
    type SCL_EASF_V_BF3_PWL_SLOPE_SEG0;                                                            \
    type SCL_EASF_V_BF3_PWL_IN_SEG1;                                                               \
    type SCL_EASF_V_BF3_PWL_BASE_SEG1;                                                             \
    type SCL_EASF_V_BF3_PWL_SLOPE_SEG1;                                                            \
    type SCL_EASF_V_BF3_PWL_IN_SEG2;                                                               \
    type SCL_EASF_V_BF3_PWL_BASE_SEG2;                                                             \
    type SCL_EASF_V_BF3_PWL_SLOPE_SEG2;                                                            \
    type SCL_EASF_V_BF3_PWL_IN_SEG3;                                                               \
    type SCL_EASF_V_BF3_PWL_BASE_SEG3;                                                             \
    type SCL_EASF_V_BF3_PWL_SLOPE_SEG3;                                                            \
    type SCL_EASF_V_BF3_PWL_IN_SEG4;                                                               \
    type SCL_EASF_V_BF3_PWL_BASE_SEG4;                                                             \
    type SCL_EASF_V_BF3_PWL_SLOPE_SEG4;                                                            \
    type SCL_EASF_V_BF3_PWL_IN_SEG5;                                                               \
    type SCL_EASF_V_BF3_PWL_BASE_SEG5;                                                             \
    type SCL_EASF_H_RINGEST_FORCE;                                                                 \
    type SCL_EASF_V_RINGEST_FORCE;                                                                 \
    type SCL_EASF_V_RINGEST_3TAP_DNTILT_UPTILT;                                                    \
    type SCL_EASF_V_RINGEST_3TAP_UPTILT_MAXVAL;                                                    \
    type SCL_EASF_V_RINGEST_3TAP_DNTILT_SLOPE;                                                     \
    type SCL_EASF_V_RINGEST_3TAP_UPTILT1_SLOPE;                                                    \
    type SCL_EASF_V_RINGEST_3TAP_UPTILT2_SLOPE;                                                    \
    type SCL_EASF_V_RINGEST_3TAP_UPTILT2_OFFSET;                                                   \
    type ISHARP_EN;                                                                                \
    type ISHARP_NOISEDET_EN;                                                                       \
    type ISHARP_NOISEDET_MODE;                                                                     \
    type ISHARP_LBA_MODE;                                                                          \
    type ISHARP_DELTA_LUT_SELECT;                                                                  \
    type ISHARP_FMT_MODE;                                                                          \
    type ISHARP_FMT_NORM;                                                                          \
    type ISHARP_DELTA_LUT_SELECT_CURRENT;                                                          \
    type ISHARP_DELTA_LUT_HOST_SELECT;                                                             \
    type ISHARP_DELTA_INDEX;                                                                       \
    type ISHARP_DELTA_DATA;                                                                        \
    type ISHARP_LBA_PWL_IN_SEG0;                                                                   \
    type ISHARP_LBA_PWL_BASE_SEG0;                                                                 \
    type ISHARP_LBA_PWL_SLOPE_SEG0;                                                                \
    type ISHARP_LBA_PWL_IN_SEG1;                                                                   \
    type ISHARP_LBA_PWL_BASE_SEG1;                                                                 \
    type ISHARP_LBA_PWL_SLOPE_SEG1;                                                                \
    type ISHARP_LBA_PWL_IN_SEG2;                                                                   \
    type ISHARP_LBA_PWL_BASE_SEG2;                                                                 \
    type ISHARP_LBA_PWL_SLOPE_SEG2;                                                                \
    type ISHARP_LBA_PWL_IN_SEG3;                                                                   \
    type ISHARP_LBA_PWL_BASE_SEG3;                                                                 \
    type ISHARP_LBA_PWL_SLOPE_SEG3;                                                                \
    type ISHARP_LBA_PWL_IN_SEG4;                                                                   \
    type ISHARP_LBA_PWL_BASE_SEG4;                                                                 \
    type ISHARP_LBA_PWL_SLOPE_SEG4;                                                                \
    type ISHARP_LBA_PWL_IN_SEG5;                                                                   \
    type ISHARP_LBA_PWL_BASE_SEG5;                                                                 \
    type ISHARP_DELTA_LUT_MEM_PWR_FORCE;                                                           \
    type ISHARP_DELTA_LUT_MEM_PWR_DIS;                                                             \
    type ISHARP_DELTA_LUT_MEM_PWR_STATE;                                                           \
    type ISHARP_NLDELTA_SCLIP_EN_P;                                                                \
    type ISHARP_NLDELTA_SCLIP_PIVOT_P;                                                             \
    type ISHARP_NLDELTA_SCLIP_SLOPE_P;                                                             \
    type ISHARP_NLDELTA_SCLIP_EN_N;                                                                \
    type ISHARP_NLDELTA_SCLIP_PIVOT_N;                                                             \
    type ISHARP_NLDELTA_SCLIP_SLOPE_N;                                                             \
    type ISHARP_NOISEDET_UTHRE;                                                                    \
    type ISHARP_NOISEDET_DTHRE;                                                                    \
    type ISHARP_NOISEDET_PWL_START_IN;                                                             \
    type ISHARP_NOISEDET_PWL_END_IN;                                                               \
    type ISHARP_NOISEDET_PWL_SLOPE;                                                                \
    type LUMA_KEYER_EN;                                                                            \
    type VPCM_HIST_SEL;                                                                            \
    type VPCM_HIST_CH_EN;                                                                          \
    type VPCM_HIST_SRC1_SEL;                                                                       \
    type VPCM_HIST_SRC2_SEL;                                                                       \
    type VPCM_HIST_SRC3_SEL;                                                                       \
    type VPCM_HIST_CH1_XBAR;                                                                       \
    type VPCM_HIST_CH2_XBAR;                                                                       \
    type VPCM_HIST_CH3_XBAR;                                                                       \
    type VPCM_HIST_FORMAT;                                                                         \
    type VPCM_HIST_READ_CHANNEL_MASK;                                                              \
    type VPCM_HIST_SCALE_SRC1;                                                                     \
    type VPCM_HIST_SCALE_SRC3;                                                                     \
    type VPCM_HIST_BIAS_SRC1;                                                                      \
    type VPCM_HIST_BIAS_SRC2;                                                                      \
    type VPCM_HIST_BIAS_SRC3;                                                                      \
    type VPCM_HIST_COEFA_SRC2;                                                                     \
    type VPCM_HIST_COEFB_SRC2;                                                                     \
    type VPCM_HIST_COEFC_SRC2;                                                                     \
    type VPCNVC_FORMAT_CROSSBAR_R;                                                                 \
    type VPCNVC_FORMAT_CROSSBAR_G;                                                                 \
    type VPCNVC_FORMAT_CROSSBAR_B;

#define DPP_FIELD_VARIABLE_LIST_VPE20(type)                                                        \
    DPP_FIELD_VARIABLE_LIST_VPE20_COMMON(type)                                                     \
    type VPCM_GAMCOR_LUT_CONFIG_MODE;                                                              \
    type SCL_V_INIT_FRAC_BOT;                                                                      \
    type SCL_V_INIT_INT_BOT;                                                                       \
    type SCL_V_INIT_FRAC_BOT_C;                                                                    \
    type SCL_V_INIT_INT_BOT_C;                                                                     \
    type PRE_DEGAM_MODE;                                                                           \
    type PRE_DEGAM_SELECT;

struct vpe20_dpp_registers {
    DPP_REG_VARIABLE_LIST_VPE20
};

struct vpe20_dpp_shift {
    DPP_FIELD_VARIABLE_LIST_VPE20(uint8_t)
};

struct vpe20_dpp_mask {
    DPP_FIELD_VARIABLE_LIST_VPE20(uint32_t)
};

struct vpe20_dpp {
    struct dpp                    base; // base class, must be the 1st field
    struct vpe20_dpp_registers   *regs;
    const struct vpe20_dpp_shift *shift;
    const struct vpe20_dpp_mask  *mask;
};

enum vpe10_dscl_mode_sel vpe20_dpp_dscl_get_dscl_mode(const struct scaler_data *data);

void vpe20_construct_dpp(struct vpe_priv *vpe_priv, struct dpp *dpp);

void vpe20_dpp_set_segment_scaler(struct dpp *dpp, const struct scaler_data *scl_data);

void vpe20_dpp_dscl_set_scaler_position(struct dpp *dpp, const struct scaler_data *scl_data);

void vpe20_dpp_set_frame_scaler(struct dpp *dpp, const struct scaler_data *scl_data);

void vpe20_dpp_program_input_transfer_func(struct dpp *dpp, struct transfer_func *input_tf);

void vpe20_dscl_program_easf(struct dpp *dpp_base, const struct scaler_data *scl_data);

void vpe20_dscl_disable_easf(struct dpp *dpp, const struct scaler_data *scl_data);

void vpe20_dscl_program_isharp(struct dpp *dpp, const struct scaler_data *scl_data);

void vpe20_dpp_enable_clocks(struct dpp *dpp, bool enable);

void vpe20_dpp_cnv_program_alpha_keyer(
    struct dpp *dpp, const struct cnv_keyer_params *keyer_params);
void vpe20_dpp_program_cnv(
    struct dpp *dpp, enum vpe_surface_pixel_format format, enum vpe_expansion_mode mode);

void vpe20_dpp_program_histo(struct dpp* dpp, struct vpe_histogram_param* hist_para, enum color_space csm);
#ifdef __cplusplus
}
#endif
