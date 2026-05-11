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

#include "mpc.h"
#include "reg_helper.h"
#include "vpe10_mpc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RMCM_MPCC_DISCONNECTED 0xf

#define MPC_REG_LIST_VPE20_COMMON(id)                                                              \
    MPC_REG_LIST_VPE10_COMMON(id),                                                                 \
        SRIDFVL(VPMPCC_MCM_FIRST_GAMUT_REMAP_COEF_FORMAT, VPMPCC_MCM, id),                         \
        SRIDFVL(VPMPCC_MCM_FIRST_GAMUT_REMAP_MODE, VPMPCC_MCM, id),                                \
        SRIDFVL(VPMPC_MCM_FIRST_GAMUT_REMAP_C11_C12_SETA, VPMPCC_MCM, id),                         \
        SRIDFVL(VPMPC_MCM_FIRST_GAMUT_REMAP_C13_C14_SETA, VPMPCC_MCM, id),                         \
        SRIDFVL(VPMPC_MCM_FIRST_GAMUT_REMAP_C21_C22_SETA, VPMPCC_MCM, id),                         \
        SRIDFVL(VPMPC_MCM_FIRST_GAMUT_REMAP_C23_C24_SETA, VPMPCC_MCM, id),                         \
        SRIDFVL(VPMPC_MCM_FIRST_GAMUT_REMAP_C31_C32_SETA, VPMPCC_MCM, id),                         \
        SRIDFVL(VPMPC_MCM_FIRST_GAMUT_REMAP_C33_C34_SETA, VPMPCC_MCM, id),                         \
        SRIDFVL(VPMPCC_MCM_SECOND_GAMUT_REMAP_COEF_FORMAT, VPMPCC_MCM, id),                        \
        SRIDFVL(VPMPCC_MCM_SECOND_GAMUT_REMAP_MODE, VPMPCC_MCM, id),                               \
        SRIDFVL(VPMPC_MCM_SECOND_GAMUT_REMAP_C11_C12_SETA, VPMPCC_MCM, id),                        \
        SRIDFVL(VPMPC_MCM_SECOND_GAMUT_REMAP_C13_C14_SETA, VPMPCC_MCM, id),                        \
        SRIDFVL(VPMPC_MCM_SECOND_GAMUT_REMAP_C21_C22_SETA, VPMPCC_MCM, id),                        \
        SRIDFVL(VPMPC_MCM_SECOND_GAMUT_REMAP_C23_C24_SETA, VPMPCC_MCM, id),                        \
        SRIDFVL(VPMPC_MCM_SECOND_GAMUT_REMAP_C31_C32_SETA, VPMPCC_MCM, id),                        \
        SRIDFVL(VPMPC_MCM_SECOND_GAMUT_REMAP_C33_C34_SETA, VPMPCC_MCM, id),                        \
        SRIDFVL(VPMPCC_CONTROL2, VPMPCC, id)

#define MPC_REG_LIST_VPE20(id)                                                                     \
    MPC_REG_LIST_VPE20_COMMON(id), SRIDFVL3(SHAPER_CONTROL, VPMPC_RMCM, id),                       \
        SRIDFVL3(SHAPER_OFFSET_R, VPMPC_RMCM, id), SRIDFVL3(SHAPER_OFFSET_G, VPMPC_RMCM, id),      \
        SRIDFVL3(SHAPER_OFFSET_B, VPMPC_RMCM, id), SRIDFVL3(SHAPER_SCALE_R, VPMPC_RMCM, id),       \
        SRIDFVL3(SHAPER_SCALE_G_B, VPMPC_RMCM, id), SRIDFVL3(SHAPER_LUT_INDEX, VPMPC_RMCM, id),    \
        SRIDFVL3(SHAPER_LUT_DATA, VPMPC_RMCM, id),                                                 \
        SRIDFVL3(SHAPER_LUT_WRITE_EN_MASK, VPMPC_RMCM, id),                                        \
        SRIDFVL3(SHAPER_RAMA_START_CNTL_B, VPMPC_RMCM, id),                                        \
        SRIDFVL3(SHAPER_RAMA_START_CNTL_G, VPMPC_RMCM, id),                                        \
        SRIDFVL3(SHAPER_RAMA_START_CNTL_R, VPMPC_RMCM, id),                                        \
        SRIDFVL3(SHAPER_RAMA_END_CNTL_B, VPMPC_RMCM, id),                                          \
        SRIDFVL3(SHAPER_RAMA_END_CNTL_G, VPMPC_RMCM, id),                                          \
        SRIDFVL3(SHAPER_RAMA_END_CNTL_R, VPMPC_RMCM, id),                                          \
        SRIDFVL3(SHAPER_RAMA_REGION_0_1, VPMPC_RMCM, id),                                          \
        SRIDFVL3(SHAPER_RAMA_REGION_2_3, VPMPC_RMCM, id),                                          \
        SRIDFVL3(SHAPER_RAMA_REGION_4_5, VPMPC_RMCM, id),                                          \
        SRIDFVL3(SHAPER_RAMA_REGION_6_7, VPMPC_RMCM, id),                                          \
        SRIDFVL3(SHAPER_RAMA_REGION_8_9, VPMPC_RMCM, id),                                          \
        SRIDFVL3(SHAPER_RAMA_REGION_10_11, VPMPC_RMCM, id),                                        \
        SRIDFVL3(SHAPER_RAMA_REGION_12_13, VPMPC_RMCM, id),                                        \
        SRIDFVL3(SHAPER_RAMA_REGION_14_15, VPMPC_RMCM, id),                                        \
        SRIDFVL3(SHAPER_RAMA_REGION_16_17, VPMPC_RMCM, id),                                        \
        SRIDFVL3(SHAPER_RAMA_REGION_18_19, VPMPC_RMCM, id),                                        \
        SRIDFVL3(SHAPER_RAMA_REGION_20_21, VPMPC_RMCM, id),                                        \
        SRIDFVL3(SHAPER_RAMA_REGION_22_23, VPMPC_RMCM, id),                                        \
        SRIDFVL3(SHAPER_RAMA_REGION_24_25, VPMPC_RMCM, id),                                        \
        SRIDFVL3(SHAPER_RAMA_REGION_26_27, VPMPC_RMCM, id),                                        \
        SRIDFVL3(SHAPER_RAMA_REGION_28_29, VPMPC_RMCM, id),                                        \
        SRIDFVL3(SHAPER_RAMA_REGION_30_31, VPMPC_RMCM, id),                                        \
        SRIDFVL3(SHAPER_RAMA_REGION_32_33, VPMPC_RMCM, id), SRIDFVL3(3DLUT_MODE, VPMPC_RMCM, id),  \
        SRIDFVL3(3DLUT_INDEX, VPMPC_RMCM, id), SRIDFVL3(3DLUT_DATA, VPMPC_RMCM, id),               \
        SRIDFVL3(3DLUT_DATA_30BIT, VPMPC_RMCM, id),                                                \
        SRIDFVL3(3DLUT_READ_WRITE_CONTROL, VPMPC_RMCM, id),                                        \
        SRIDFVL3(3DLUT_OUT_NORM_FACTOR, VPMPC_RMCM, id),                                           \
        SRIDFVL3(3DLUT_OUT_OFFSET_R, VPMPC_RMCM, id),                                              \
        SRIDFVL3(3DLUT_OUT_OFFSET_G, VPMPC_RMCM, id),                                              \
        SRIDFVL3(3DLUT_OUT_OFFSET_B, VPMPC_RMCM, id),                                              \
        SRIDFVL3(GAMUT_REMAP_COEF_FORMAT, VPMPC_RMCM, id),                                         \
        SRIDFVL3(GAMUT_REMAP_MODE, VPMPC_RMCM, id),                                                \
        SRIDFVL3(GAMUT_REMAP_C11_C12_SETA, VPMPC_RMCM, id),                                        \
        SRIDFVL3(GAMUT_REMAP_C13_C14_SETA, VPMPC_RMCM, id),                                        \
        SRIDFVL3(GAMUT_REMAP_C21_C22_SETA, VPMPC_RMCM, id),                                        \
        SRIDFVL3(GAMUT_REMAP_C23_C24_SETA, VPMPC_RMCM, id),                                        \
        SRIDFVL3(GAMUT_REMAP_C31_C32_SETA, VPMPC_RMCM, id),                                        \
        SRIDFVL3(GAMUT_REMAP_C33_C34_SETA, VPMPC_RMCM, id),                                        \
        SRIDFVL3(MEM_PWR_CTRL, VPMPC_RMCM, id), SRIDFVL3(3DLUT_FAST_LOAD_SELECT, VPMPC_RMCM, id),  \
        SRIDFVL3(CNTL, VPMPC_RMCM, id), SRIDFVL1(VPMPC_VPCDC0_3DLUT_FL_CONFIG),                    \
        SRIDFVL1(VPMPC_VPCDC0_3DLUT_FL_BIAS_SCALE)

#define MPC_FIELD_LIST_VPE20_COMMON(post_fix)                                                      \
    MPC_FIELD_LIST_VPE10_COMMON(post_fix), SFRB(VPMPC_RMCM_CNTL, VPMPC_RMCM_CNTL, post_fix),       \
        SFRB(VPMPC_RMCM_SHAPER_LUT_MODE, VPMPC_RMCM_SHAPER_CONTROL, post_fix),                     \
        SFRB(VPMPC_RMCM_SHAPER_MODE_CURRENT, VPMPC_RMCM_SHAPER_CONTROL, post_fix),                 \
        SFRB(VPMPC_RMCM_SHAPER_SELECT_CURRENT, VPMPC_RMCM_SHAPER_CONTROL, post_fix),               \
        SFRB(VPMPC_RMCM_SHAPER_OFFSET_R, VPMPC_RMCM_SHAPER_OFFSET_R, post_fix),                    \
        SFRB(VPMPC_RMCM_SHAPER_OFFSET_G, VPMPC_RMCM_SHAPER_OFFSET_G, post_fix),                    \
        SFRB(VPMPC_RMCM_SHAPER_OFFSET_B, VPMPC_RMCM_SHAPER_OFFSET_B, post_fix),                    \
        SFRB(VPMPC_RMCM_SHAPER_SCALE_R, VPMPC_RMCM_SHAPER_SCALE_R, post_fix),                      \
        SFRB(VPMPC_RMCM_SHAPER_LUT_INDEX, VPMPC_RMCM_SHAPER_LUT_INDEX, post_fix),                  \
        SFRB(VPMPC_RMCM_SHAPER_LUT_DATA, VPMPC_RMCM_SHAPER_LUT_DATA, post_fix),                    \
        SFRB(VPMPC_RMCM_SHAPER_LUT_WRITE_EN_MASK, VPMPC_RMCM_SHAPER_LUT_WRITE_EN_MASK, post_fix),  \
        SFRB(VPMPC_RMCM_SHAPER_LUT_WRITE_SEL, VPMPC_RMCM_SHAPER_LUT_WRITE_EN_MASK, post_fix),      \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_START_B, VPMPC_RMCM_SHAPER_RAMA_START_CNTL_B,       \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B,                                    \
            VPMPC_RMCM_SHAPER_RAMA_START_CNTL_B, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_START_G, VPMPC_RMCM_SHAPER_RAMA_START_CNTL_G,       \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_G,                                    \
            VPMPC_RMCM_SHAPER_RAMA_START_CNTL_G, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_START_R, VPMPC_RMCM_SHAPER_RAMA_START_CNTL_R,       \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_R,                                    \
            VPMPC_RMCM_SHAPER_RAMA_START_CNTL_R, post_fix),                                        \
        SFRB(                                                                                      \
            VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_END_B, VPMPC_RMCM_SHAPER_RAMA_END_CNTL_B, post_fix), \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_END_BASE_B, VPMPC_RMCM_SHAPER_RAMA_END_CNTL_B,      \
            post_fix),                                                                             \
        SFRB(                                                                                      \
            VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_END_G, VPMPC_RMCM_SHAPER_RAMA_END_CNTL_G, post_fix), \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_END_BASE_G, VPMPC_RMCM_SHAPER_RAMA_END_CNTL_G,      \
            post_fix),                                                                             \
        SFRB(                                                                                      \
            VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_END_R, VPMPC_RMCM_SHAPER_RAMA_END_CNTL_R, post_fix), \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_END_BASE_R, VPMPC_RMCM_SHAPER_RAMA_END_CNTL_R,      \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_0_1,     \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, VPMPC_RMCM_SHAPER_RAMA_REGION_0_1,   \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_0_1,     \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, VPMPC_RMCM_SHAPER_RAMA_REGION_0_1,   \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION2_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_2_3,     \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION2_NUM_SEGMENTS, VPMPC_RMCM_SHAPER_RAMA_REGION_2_3,   \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION3_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_2_3,     \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION3_NUM_SEGMENTS, VPMPC_RMCM_SHAPER_RAMA_REGION_2_3,   \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION4_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_4_5,     \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION4_NUM_SEGMENTS, VPMPC_RMCM_SHAPER_RAMA_REGION_4_5,   \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION5_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_4_5,     \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION5_NUM_SEGMENTS, VPMPC_RMCM_SHAPER_RAMA_REGION_4_5,   \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION6_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_6_7,     \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION6_NUM_SEGMENTS, VPMPC_RMCM_SHAPER_RAMA_REGION_6_7,   \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION7_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_6_7,     \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION7_NUM_SEGMENTS, VPMPC_RMCM_SHAPER_RAMA_REGION_6_7,   \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION8_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_8_9,     \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION8_NUM_SEGMENTS, VPMPC_RMCM_SHAPER_RAMA_REGION_8_9,   \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION9_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_8_9,     \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION9_NUM_SEGMENTS, VPMPC_RMCM_SHAPER_RAMA_REGION_8_9,   \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION10_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_10_11,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION10_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_10_11, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION11_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_10_11,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION11_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_10_11, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION12_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_12_13,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION12_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_12_13, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION13_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_12_13,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION13_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_12_13, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION14_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_14_15,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION14_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_14_15, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION15_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_14_15,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION15_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_14_15, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION16_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_16_17,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION16_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_16_17, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION17_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_16_17,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION17_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_16_17, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION18_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_18_19,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION18_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_18_19, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION19_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_18_19,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION19_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_18_19, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION20_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_20_21,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION20_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_20_21, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION21_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_20_21,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION21_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_20_21, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION22_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_22_23,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION22_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_22_23, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION23_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_22_23,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION23_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_22_23, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION24_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_24_25,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION24_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_24_25, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION25_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_24_25,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION25_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_24_25, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION26_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_26_27,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION26_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_26_27, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION27_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_26_27,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION27_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_26_27, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION28_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_28_29,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION28_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_28_29, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION29_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_28_29,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION29_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_28_29, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION30_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_30_31,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION30_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_30_31, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION31_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_30_31,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION31_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_30_31, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION32_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_32_33,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION32_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_32_33, post_fix),                                        \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION33_LUT_OFFSET, VPMPC_RMCM_SHAPER_RAMA_REGION_32_33,  \
            post_fix),                                                                             \
        SFRB(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION33_NUM_SEGMENTS,                                     \
            VPMPC_RMCM_SHAPER_RAMA_REGION_32_33, post_fix),                                        \
        SFRB(VPMPC_RMCM_3DLUT_MODE, VPMPC_RMCM_3DLUT_MODE, post_fix),                              \
        SFRB(VPMPC_RMCM_3DLUT_SIZE, VPMPC_RMCM_3DLUT_MODE, post_fix),                              \
        SFRB(VPMPC_RMCM_3DLUT_MODE_CURRENT, VPMPC_RMCM_3DLUT_MODE, post_fix),                      \
        SFRB(VPMPC_RMCM_3DLUT_SELECT_CURRENT, VPMPC_RMCM_3DLUT_MODE, post_fix),                    \
        SFRB(VPMPC_RMCM_3DLUT_INDEX, VPMPC_RMCM_3DLUT_INDEX, post_fix),                            \
        SFRB(VPMPC_RMCM_3DLUT_DATA0, VPMPC_RMCM_3DLUT_DATA, post_fix),                             \
        SFRB(VPMPC_RMCM_3DLUT_DATA1, VPMPC_RMCM_3DLUT_DATA, post_fix),                             \
        SFRB(VPMPC_RMCM_3DLUT_DATA_30BIT, VPMPC_RMCM_3DLUT_DATA_30BIT, post_fix),                  \
        SFRB(VPMPC_RMCM_3DLUT_WRITE_EN_MASK, VPMPC_RMCM_3DLUT_READ_WRITE_CONTROL, post_fix),       \
        SFRB(VPMPC_RMCM_3DLUT_RAM_SEL, VPMPC_RMCM_3DLUT_READ_WRITE_CONTROL, post_fix),             \
        SFRB(VPMPC_RMCM_3DLUT_30BIT_EN, VPMPC_RMCM_3DLUT_READ_WRITE_CONTROL, post_fix),            \
        SFRB(VPMPC_RMCM_3DLUT_READ_SEL, VPMPC_RMCM_3DLUT_READ_WRITE_CONTROL, post_fix),            \
        SFRB(VPMPC_RMCM_3DLUT_OUT_NORM_FACTOR, VPMPC_RMCM_3DLUT_OUT_NORM_FACTOR, post_fix),        \
        SFRB(VPMPC_RMCM_3DLUT_OUT_OFFSET_R, VPMPC_RMCM_3DLUT_OUT_OFFSET_R, post_fix),              \
        SFRB(VPMPC_RMCM_3DLUT_OUT_SCALE_R, VPMPC_RMCM_3DLUT_OUT_OFFSET_R, post_fix),               \
        SFRB(VPMPC_RMCM_3DLUT_OUT_OFFSET_G, VPMPC_RMCM_3DLUT_OUT_OFFSET_G, post_fix),              \
        SFRB(VPMPC_RMCM_3DLUT_OUT_SCALE_G, VPMPC_RMCM_3DLUT_OUT_OFFSET_G, post_fix),               \
        SFRB(VPMPC_RMCM_3DLUT_OUT_OFFSET_B, VPMPC_RMCM_3DLUT_OUT_OFFSET_B, post_fix),              \
        SFRB(VPMPC_RMCM_3DLUT_OUT_SCALE_B, VPMPC_RMCM_3DLUT_OUT_OFFSET_B, post_fix),               \
        SFRB(VPMPC_RMCM_SHAPER_MEM_PWR_FORCE, VPMPC_RMCM_MEM_PWR_CTRL, post_fix),                  \
        SFRB(VPMPC_RMCM_SHAPER_MEM_PWR_DIS, VPMPC_RMCM_MEM_PWR_CTRL, post_fix),                    \
        SFRB(VPMPC_RMCM_SHAPER_MEM_LOW_PWR_MODE, VPMPC_RMCM_MEM_PWR_CTRL, post_fix),               \
        SFRB(VPMPC_RMCM_3DLUT_MEM_PWR_FORCE, VPMPC_RMCM_MEM_PWR_CTRL, post_fix),                   \
        SFRB(VPMPC_RMCM_3DLUT_MEM_PWR_DIS, VPMPC_RMCM_MEM_PWR_CTRL, post_fix),                     \
        SFRB(VPMPC_RMCM_3DLUT_MEM_LOW_PWR_MODE, VPMPC_RMCM_MEM_PWR_CTRL, post_fix),                \
        SFRB(VPMPC_RMCM_SHAPER_MEM_PWR_STATE, VPMPC_RMCM_MEM_PWR_CTRL, post_fix),                  \
        SFRB(VPMPC_RMCM_3DLUT_MEM_PWR_STATE, VPMPC_RMCM_MEM_PWR_CTRL, post_fix),                   \
        SFRB(VPMPC_RMCM_GAMUT_REMAP_COEF_FORMAT, VPMPC_RMCM_GAMUT_REMAP_COEF_FORMAT, post_fix),    \
        SFRB(VPMPC_RMCM_GAMUT_REMAP_MODE, VPMPC_RMCM_GAMUT_REMAP_MODE, post_fix),                  \
        SFRB(VPMPC_RMCM_GAMUT_REMAP_MODE_CURRENT, VPMPC_RMCM_GAMUT_REMAP_MODE, post_fix),          \
        SFRB(VPMPC_RMCM_GAMUT_REMAP_C11_SETA, VPMPC_RMCM_GAMUT_REMAP_C11_C12_SETA, post_fix),      \
        SFRB(VPMPC_RMCM_GAMUT_REMAP_C12_SETA, VPMPC_RMCM_GAMUT_REMAP_C11_C12_SETA, post_fix),      \
        SFRB(VPMPC_RMCM_GAMUT_REMAP_C13_SETA, VPMPC_RMCM_GAMUT_REMAP_C13_C14_SETA, post_fix),      \
        SFRB(VPMPC_RMCM_GAMUT_REMAP_C14_SETA, VPMPC_RMCM_GAMUT_REMAP_C13_C14_SETA, post_fix),      \
        SFRB(VPMPC_RMCM_GAMUT_REMAP_C21_SETA, VPMPC_RMCM_GAMUT_REMAP_C21_C22_SETA, post_fix),      \
        SFRB(VPMPC_RMCM_GAMUT_REMAP_C22_SETA, VPMPC_RMCM_GAMUT_REMAP_C21_C22_SETA, post_fix),      \
        SFRB(VPMPC_RMCM_GAMUT_REMAP_C23_SETA, VPMPC_RMCM_GAMUT_REMAP_C23_C24_SETA, post_fix),      \
        SFRB(VPMPC_RMCM_GAMUT_REMAP_C24_SETA, VPMPC_RMCM_GAMUT_REMAP_C23_C24_SETA, post_fix),      \
        SFRB(VPMPC_RMCM_GAMUT_REMAP_C31_SETA, VPMPC_RMCM_GAMUT_REMAP_C31_C32_SETA, post_fix),      \
        SFRB(VPMPC_RMCM_GAMUT_REMAP_C32_SETA, VPMPC_RMCM_GAMUT_REMAP_C31_C32_SETA, post_fix),      \
        SFRB(VPMPC_RMCM_GAMUT_REMAP_C33_SETA, VPMPC_RMCM_GAMUT_REMAP_C33_C34_SETA, post_fix),      \
        SFRB(VPMPC_RMCM_GAMUT_REMAP_C34_SETA, VPMPC_RMCM_GAMUT_REMAP_C33_C34_SETA, post_fix),      \
        SFRB(VPMPCC_MCM_FIRST_GAMUT_REMAP_COEF_FORMAT, VPMPCC_MCM_FIRST_GAMUT_REMAP_COEF_FORMAT,   \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_FIRST_GAMUT_REMAP_MODE, VPMPCC_MCM_FIRST_GAMUT_REMAP_MODE, post_fix),      \
        SFRB(VPMPCC_MCM_FIRST_GAMUT_REMAP_MODE_CURRENT, VPMPCC_MCM_FIRST_GAMUT_REMAP_MODE,         \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_FIRST_GAMUT_REMAP_C11_SETA, VPMPC_MCM_FIRST_GAMUT_REMAP_C11_C12_SETA,      \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_FIRST_GAMUT_REMAP_C12_SETA, VPMPC_MCM_FIRST_GAMUT_REMAP_C11_C12_SETA,      \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_FIRST_GAMUT_REMAP_C13_SETA, VPMPC_MCM_FIRST_GAMUT_REMAP_C13_C14_SETA,      \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_FIRST_GAMUT_REMAP_C14_SETA, VPMPC_MCM_FIRST_GAMUT_REMAP_C13_C14_SETA,      \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_FIRST_GAMUT_REMAP_C21_SETA, VPMPC_MCM_FIRST_GAMUT_REMAP_C21_C22_SETA,      \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_FIRST_GAMUT_REMAP_C22_SETA, VPMPC_MCM_FIRST_GAMUT_REMAP_C21_C22_SETA,      \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_FIRST_GAMUT_REMAP_C23_SETA, VPMPC_MCM_FIRST_GAMUT_REMAP_C23_C24_SETA,      \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_FIRST_GAMUT_REMAP_C24_SETA, VPMPC_MCM_FIRST_GAMUT_REMAP_C23_C24_SETA,      \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_FIRST_GAMUT_REMAP_C31_SETA, VPMPC_MCM_FIRST_GAMUT_REMAP_C31_C32_SETA,      \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_FIRST_GAMUT_REMAP_C32_SETA, VPMPC_MCM_FIRST_GAMUT_REMAP_C31_C32_SETA,      \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_FIRST_GAMUT_REMAP_C33_SETA, VPMPC_MCM_FIRST_GAMUT_REMAP_C33_C34_SETA,      \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_FIRST_GAMUT_REMAP_C34_SETA, VPMPC_MCM_FIRST_GAMUT_REMAP_C33_C34_SETA,      \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_SECOND_GAMUT_REMAP_COEF_FORMAT, VPMPCC_MCM_SECOND_GAMUT_REMAP_COEF_FORMAT, \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_SECOND_GAMUT_REMAP_MODE, VPMPCC_MCM_SECOND_GAMUT_REMAP_MODE, post_fix),    \
        SFRB(VPMPCC_MCM_SECOND_GAMUT_REMAP_MODE_CURRENT, VPMPCC_MCM_SECOND_GAMUT_REMAP_MODE,       \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_SECOND_GAMUT_REMAP_C11_SETA, VPMPC_MCM_SECOND_GAMUT_REMAP_C11_C12_SETA,    \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_SECOND_GAMUT_REMAP_C12_SETA, VPMPC_MCM_SECOND_GAMUT_REMAP_C11_C12_SETA,    \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_SECOND_GAMUT_REMAP_C13_SETA, VPMPC_MCM_SECOND_GAMUT_REMAP_C13_C14_SETA,    \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_SECOND_GAMUT_REMAP_C14_SETA, VPMPC_MCM_SECOND_GAMUT_REMAP_C13_C14_SETA,    \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_SECOND_GAMUT_REMAP_C21_SETA, VPMPC_MCM_SECOND_GAMUT_REMAP_C21_C22_SETA,    \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_SECOND_GAMUT_REMAP_C22_SETA, VPMPC_MCM_SECOND_GAMUT_REMAP_C21_C22_SETA,    \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_SECOND_GAMUT_REMAP_C23_SETA, VPMPC_MCM_SECOND_GAMUT_REMAP_C23_C24_SETA,    \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_SECOND_GAMUT_REMAP_C24_SETA, VPMPC_MCM_SECOND_GAMUT_REMAP_C23_C24_SETA,    \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_SECOND_GAMUT_REMAP_C31_SETA, VPMPC_MCM_SECOND_GAMUT_REMAP_C31_C32_SETA,    \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_SECOND_GAMUT_REMAP_C32_SETA, VPMPC_MCM_SECOND_GAMUT_REMAP_C31_C32_SETA,    \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_SECOND_GAMUT_REMAP_C33_SETA, VPMPC_MCM_SECOND_GAMUT_REMAP_C33_C34_SETA,    \
            post_fix),                                                                             \
        SFRB(VPMPCC_MCM_SECOND_GAMUT_REMAP_C34_SETA, VPMPC_MCM_SECOND_GAMUT_REMAP_C33_C34_SETA,    \
            post_fix),                                                                             \
        SFRB(VPMPCC_GLOBAL_ALPHA, VPMPCC_CONTROL2, post_fix),                                      \
        SFRB(VPMPCC_GLOBAL_GAIN, VPMPCC_CONTROL2, post_fix)

#define MPC_FIELD_LIST_VPE20(post_fix)                                                             \
    MPC_FIELD_LIST_VPE20_COMMON(post_fix),                                                         \
        SFRB(VPMPC_RMCM_SHAPER_SCALE_G, VPMPC_RMCM_SHAPER_SCALE_G_B, post_fix),                    \
        SFRB(VPMPC_RMCM_SHAPER_SCALE_B, VPMPC_RMCM_SHAPER_SCALE_G_B, post_fix),                    \
        SFRB(VPMPC_RMCM_3DLUT_FL_SEL, VPMPC_RMCM_3DLUT_FAST_LOAD_SELECT, post_fix),                \
        SFRB(VPCDC0_3DLUT_FL_MODE, VPMPC_VPCDC0_3DLUT_FL_CONFIG, post_fix),                        \
        SFRB(VPCDC0_3DLUT_FL_FORMAT, VPMPC_VPCDC0_3DLUT_FL_CONFIG, post_fix),                      \
        SFRB(VPCDC0_3DLUT_FL_BIAS, VPMPC_VPCDC0_3DLUT_FL_BIAS_SCALE, post_fix),                    \
        SFRB(VPCDC0_3DLUT_FL_SCALE, VPMPC_VPCDC0_3DLUT_FL_BIAS_SCALE, post_fix)

#define MPC_REG_VARIABLE_LIST_VPE20_COMMON                                                         \
    MPC_REG_VARIABLE_LIST_VPE10_COMMON                                                             \
    reg_id_val VPMPC_RMCM_CNTL;                                                                    \
    reg_id_val VPMPC_RMCM_SHAPER_CONTROL;                                                          \
    reg_id_val VPMPC_RMCM_SHAPER_OFFSET_R;                                                         \
    reg_id_val VPMPC_RMCM_SHAPER_OFFSET_G;                                                         \
    reg_id_val VPMPC_RMCM_SHAPER_OFFSET_B;                                                         \
    reg_id_val VPMPC_RMCM_SHAPER_SCALE_R;                                                          \
    reg_id_val VPMPC_RMCM_SHAPER_LUT_INDEX;                                                        \
    reg_id_val VPMPC_RMCM_SHAPER_LUT_DATA;                                                         \
    reg_id_val VPMPC_RMCM_SHAPER_LUT_WRITE_EN_MASK;                                                \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_START_CNTL_B;                                                \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_START_CNTL_G;                                                \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_START_CNTL_R;                                                \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_END_CNTL_B;                                                  \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_END_CNTL_G;                                                  \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_END_CNTL_R;                                                  \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_REGION_0_1;                                                  \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_REGION_2_3;                                                  \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_REGION_4_5;                                                  \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_REGION_6_7;                                                  \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_REGION_8_9;                                                  \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_REGION_10_11;                                                \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_REGION_12_13;                                                \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_REGION_14_15;                                                \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_REGION_16_17;                                                \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_REGION_18_19;                                                \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_REGION_20_21;                                                \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_REGION_22_23;                                                \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_REGION_24_25;                                                \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_REGION_26_27;                                                \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_REGION_28_29;                                                \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_REGION_30_31;                                                \
    reg_id_val VPMPC_RMCM_SHAPER_RAMA_REGION_32_33;                                                \
    reg_id_val VPMPC_RMCM_3DLUT_MODE;                                                              \
    reg_id_val VPMPC_RMCM_3DLUT_INDEX;                                                             \
    reg_id_val VPMPC_RMCM_3DLUT_DATA;                                                              \
    reg_id_val VPMPC_RMCM_3DLUT_DATA_30BIT;                                                        \
    reg_id_val VPMPC_RMCM_3DLUT_READ_WRITE_CONTROL;                                                \
    reg_id_val VPMPC_RMCM_3DLUT_OUT_NORM_FACTOR;                                                   \
    reg_id_val VPMPC_RMCM_3DLUT_OUT_OFFSET_R;                                                      \
    reg_id_val VPMPC_RMCM_3DLUT_OUT_OFFSET_G;                                                      \
    reg_id_val VPMPC_RMCM_3DLUT_OUT_OFFSET_B;                                                      \
    reg_id_val VPMPC_RMCM_GAMUT_REMAP_COEF_FORMAT;                                                 \
    reg_id_val VPMPC_RMCM_GAMUT_REMAP_MODE;                                                        \
    reg_id_val VPMPC_RMCM_GAMUT_REMAP_C11_C12_SETA;                                                \
    reg_id_val VPMPC_RMCM_GAMUT_REMAP_C13_C14_SETA;                                                \
    reg_id_val VPMPC_RMCM_GAMUT_REMAP_C21_C22_SETA;                                                \
    reg_id_val VPMPC_RMCM_GAMUT_REMAP_C23_C24_SETA;                                                \
    reg_id_val VPMPC_RMCM_GAMUT_REMAP_C31_C32_SETA;                                                \
    reg_id_val VPMPC_RMCM_GAMUT_REMAP_C33_C34_SETA;                                                \
    reg_id_val VPMPCC_MCM_FIRST_GAMUT_REMAP_COEF_FORMAT;                                           \
    reg_id_val VPMPCC_MCM_FIRST_GAMUT_REMAP_MODE;                                                  \
    reg_id_val VPMPC_MCM_FIRST_GAMUT_REMAP_C11_C12_SETA;                                           \
    reg_id_val VPMPC_MCM_FIRST_GAMUT_REMAP_C13_C14_SETA;                                           \
    reg_id_val VPMPC_MCM_FIRST_GAMUT_REMAP_C21_C22_SETA;                                           \
    reg_id_val VPMPC_MCM_FIRST_GAMUT_REMAP_C23_C24_SETA;                                           \
    reg_id_val VPMPC_MCM_FIRST_GAMUT_REMAP_C31_C32_SETA;                                           \
    reg_id_val VPMPC_MCM_FIRST_GAMUT_REMAP_C33_C34_SETA;                                           \
    reg_id_val VPMPCC_MCM_SECOND_GAMUT_REMAP_COEF_FORMAT;                                          \
    reg_id_val VPMPCC_MCM_SECOND_GAMUT_REMAP_MODE;                                                 \
    reg_id_val VPMPC_MCM_SECOND_GAMUT_REMAP_C11_C12_SETA;                                          \
    reg_id_val VPMPC_MCM_SECOND_GAMUT_REMAP_C13_C14_SETA;                                          \
    reg_id_val VPMPC_MCM_SECOND_GAMUT_REMAP_C21_C22_SETA;                                          \
    reg_id_val VPMPC_MCM_SECOND_GAMUT_REMAP_C23_C24_SETA;                                          \
    reg_id_val VPMPC_MCM_SECOND_GAMUT_REMAP_C31_C32_SETA;                                          \
    reg_id_val VPMPC_MCM_SECOND_GAMUT_REMAP_C33_C34_SETA;                                          \
    reg_id_val VPMPC_RMCM_MEM_PWR_CTRL;                                                            \
    reg_id_val VPMPCC_CONTROL2;                                                                    \
    reg_id_val VPCM_HIST_INDEX;

#define MPC_REG_VARIABLE_LIST_VPE20                                                                \
    MPC_REG_VARIABLE_LIST_VPE20_COMMON                                                             \
    reg_id_val VPMPC_RMCM_SHAPER_SCALE_G_B;                                                        \
    reg_id_val VPMPC_RMCM_3DLUT_FAST_LOAD_SELECT;                                                  \
    reg_id_val VPMPC_VPCDC0_3DLUT_FL_CONFIG;                                                       \
    reg_id_val VPMPC_VPCDC0_3DLUT_FL_BIAS_SCALE;

#define MPC_FIELD_VARIABLE_LIST_VPE20_COMMON(type)                                                 \
    MPC_FIELD_VARIABLE_LIST_VPE10_COMMON(type)                                                     \
    type VPMPC_RMCM_CNTL;                                                                          \
    type VPMPC_RMCM_SHAPER_LUT_MODE;                                                               \
    type VPMPC_RMCM_SHAPER_MODE_CURRENT;                                                           \
    type VPMPC_RMCM_SHAPER_SELECT_CURRENT;                                                         \
    type VPMPC_RMCM_SHAPER_OFFSET_R;                                                               \
    type VPMPC_RMCM_SHAPER_OFFSET_G;                                                               \
    type VPMPC_RMCM_SHAPER_OFFSET_B;                                                               \
    type VPMPC_RMCM_SHAPER_SCALE_R;                                                                \
    type VPMPC_RMCM_SHAPER_LUT_INDEX;                                                              \
    type VPMPC_RMCM_SHAPER_LUT_DATA;                                                               \
    type VPMPC_RMCM_SHAPER_LUT_WRITE_EN_MASK;                                                      \
    type VPMPC_RMCM_SHAPER_LUT_WRITE_SEL;                                                          \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_START_B;                                                \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B;                                        \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_START_G;                                                \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_G;                                        \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_START_R;                                                \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_R;                                        \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_END_B;                                                  \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_END_BASE_B;                                             \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_END_G;                                                  \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_END_BASE_G;                                             \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_END_R;                                                  \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_END_BASE_R;                                             \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET;                                            \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS;                                          \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET;                                            \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS;                                          \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION2_LUT_OFFSET;                                            \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION2_NUM_SEGMENTS;                                          \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION3_LUT_OFFSET;                                            \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION3_NUM_SEGMENTS;                                          \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION4_LUT_OFFSET;                                            \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION4_NUM_SEGMENTS;                                          \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION5_LUT_OFFSET;                                            \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION5_NUM_SEGMENTS;                                          \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION6_LUT_OFFSET;                                            \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION6_NUM_SEGMENTS;                                          \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION7_LUT_OFFSET;                                            \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION7_NUM_SEGMENTS;                                          \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION8_LUT_OFFSET;                                            \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION8_NUM_SEGMENTS;                                          \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION9_LUT_OFFSET;                                            \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION9_NUM_SEGMENTS;                                          \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION10_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION10_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION11_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION11_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION12_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION12_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION13_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION13_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION14_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION14_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION15_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION15_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION16_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION16_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION17_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION17_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION18_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION18_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION19_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION19_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION20_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION20_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION21_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION21_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION22_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION22_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION23_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION23_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION24_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION24_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION25_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION25_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION26_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION26_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION27_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION27_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION28_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION28_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION29_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION29_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION30_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION30_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION31_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION31_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION32_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION32_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION33_LUT_OFFSET;                                           \
    type VPMPC_RMCM_SHAPER_RAMA_EXP_REGION33_NUM_SEGMENTS;                                         \
    type VPMPC_RMCM_3DLUT_MODE;                                                                    \
    type VPMPC_RMCM_3DLUT_SIZE;                                                                    \
    type VPMPC_RMCM_3DLUT_MODE_CURRENT;                                                            \
    type VPMPC_RMCM_3DLUT_SELECT_CURRENT;                                                          \
    type VPMPC_RMCM_3DLUT_INDEX;                                                                   \
    type VPMPC_RMCM_3DLUT_DATA0;                                                                   \
    type VPMPC_RMCM_3DLUT_DATA1;                                                                   \
    type VPMPC_RMCM_3DLUT_DATA_30BIT;                                                              \
    type VPMPC_RMCM_3DLUT_WRITE_EN_MASK;                                                           \
    type VPMPC_RMCM_3DLUT_RAM_SEL;                                                                 \
    type VPMPC_RMCM_3DLUT_30BIT_EN;                                                                \
    type VPMPC_RMCM_3DLUT_READ_SEL;                                                                \
    type VPMPC_RMCM_3DLUT_OUT_NORM_FACTOR;                                                         \
    type VPMPC_RMCM_3DLUT_OUT_OFFSET_R;                                                            \
    type VPMPC_RMCM_3DLUT_OUT_SCALE_R;                                                             \
    type VPMPC_RMCM_3DLUT_OUT_OFFSET_G;                                                            \
    type VPMPC_RMCM_3DLUT_OUT_SCALE_G;                                                             \
    type VPMPC_RMCM_3DLUT_OUT_OFFSET_B;                                                            \
    type VPMPC_RMCM_3DLUT_OUT_SCALE_B;                                                             \
    type VPMPC_RMCM_GAMUT_REMAP_COEF_FORMAT;                                                       \
    type VPMPC_RMCM_GAMUT_REMAP_MODE;                                                              \
    type VPMPC_RMCM_GAMUT_REMAP_MODE_CURRENT;                                                      \
    type VPMPC_RMCM_GAMUT_REMAP_C11_SETA;                                                          \
    type VPMPC_RMCM_GAMUT_REMAP_C12_SETA;                                                          \
    type VPMPC_RMCM_GAMUT_REMAP_C13_SETA;                                                          \
    type VPMPC_RMCM_GAMUT_REMAP_C14_SETA;                                                          \
    type VPMPC_RMCM_GAMUT_REMAP_C21_SETA;                                                          \
    type VPMPC_RMCM_GAMUT_REMAP_C22_SETA;                                                          \
    type VPMPC_RMCM_GAMUT_REMAP_C23_SETA;                                                          \
    type VPMPC_RMCM_GAMUT_REMAP_C24_SETA;                                                          \
    type VPMPC_RMCM_GAMUT_REMAP_C31_SETA;                                                          \
    type VPMPC_RMCM_GAMUT_REMAP_C32_SETA;                                                          \
    type VPMPC_RMCM_GAMUT_REMAP_C33_SETA;                                                          \
    type VPMPC_RMCM_GAMUT_REMAP_C34_SETA;                                                          \
    type VPMPCC_MCM_FIRST_GAMUT_REMAP_COEF_FORMAT;                                                 \
    type VPMPCC_MCM_FIRST_GAMUT_REMAP_MODE;                                                        \
    type VPMPCC_MCM_FIRST_GAMUT_REMAP_MODE_CURRENT;                                                \
    type VPMPCC_MCM_FIRST_GAMUT_REMAP_C11_SETA;                                                    \
    type VPMPCC_MCM_FIRST_GAMUT_REMAP_C12_SETA;                                                    \
    type VPMPCC_MCM_FIRST_GAMUT_REMAP_C13_SETA;                                                    \
    type VPMPCC_MCM_FIRST_GAMUT_REMAP_C14_SETA;                                                    \
    type VPMPCC_MCM_FIRST_GAMUT_REMAP_C21_SETA;                                                    \
    type VPMPCC_MCM_FIRST_GAMUT_REMAP_C22_SETA;                                                    \
    type VPMPCC_MCM_FIRST_GAMUT_REMAP_C23_SETA;                                                    \
    type VPMPCC_MCM_FIRST_GAMUT_REMAP_C24_SETA;                                                    \
    type VPMPCC_MCM_FIRST_GAMUT_REMAP_C31_SETA;                                                    \
    type VPMPCC_MCM_FIRST_GAMUT_REMAP_C32_SETA;                                                    \
    type VPMPCC_MCM_FIRST_GAMUT_REMAP_C33_SETA;                                                    \
    type VPMPCC_MCM_FIRST_GAMUT_REMAP_C34_SETA;                                                    \
    type VPMPCC_MCM_SECOND_GAMUT_REMAP_COEF_FORMAT;                                                \
    type VPMPCC_MCM_SECOND_GAMUT_REMAP_MODE;                                                       \
    type VPMPCC_MCM_SECOND_GAMUT_REMAP_MODE_CURRENT;                                               \
    type VPMPCC_MCM_SECOND_GAMUT_REMAP_C11_SETA;                                                   \
    type VPMPCC_MCM_SECOND_GAMUT_REMAP_C12_SETA;                                                   \
    type VPMPCC_MCM_SECOND_GAMUT_REMAP_C13_SETA;                                                   \
    type VPMPCC_MCM_SECOND_GAMUT_REMAP_C14_SETA;                                                   \
    type VPMPCC_MCM_SECOND_GAMUT_REMAP_C21_SETA;                                                   \
    type VPMPCC_MCM_SECOND_GAMUT_REMAP_C22_SETA;                                                   \
    type VPMPCC_MCM_SECOND_GAMUT_REMAP_C23_SETA;                                                   \
    type VPMPCC_MCM_SECOND_GAMUT_REMAP_C24_SETA;                                                   \
    type VPMPCC_MCM_SECOND_GAMUT_REMAP_C31_SETA;                                                   \
    type VPMPCC_MCM_SECOND_GAMUT_REMAP_C32_SETA;                                                   \
    type VPMPCC_MCM_SECOND_GAMUT_REMAP_C33_SETA;                                                   \
    type VPMPCC_MCM_SECOND_GAMUT_REMAP_C34_SETA;                                                   \
    type VPMPC_RMCM_SHAPER_MEM_PWR_STATE;                                                          \
    type VPMPC_RMCM_3DLUT_MEM_PWR_STATE;                                                           \
    type VPMPC_RMCM_SHAPER_MEM_PWR_FORCE;                                                          \
    type VPMPC_RMCM_SHAPER_MEM_PWR_DIS;                                                            \
    type VPMPC_RMCM_SHAPER_MEM_LOW_PWR_MODE;                                                       \
    type VPMPC_RMCM_3DLUT_MEM_PWR_FORCE;                                                           \
    type VPMPC_RMCM_3DLUT_MEM_PWR_DIS;                                                             \
    type VPMPC_RMCM_3DLUT_MEM_LOW_PWR_MODE;

#define MPC_FIELD_VARIABLE_LIST_VPE20(type)                                                        \
    MPC_FIELD_VARIABLE_LIST_VPE20_COMMON(type)                                                     \
    type VPMPC_RMCM_SHAPER_SCALE_G;                                                                \
    type VPMPC_RMCM_SHAPER_SCALE_B;                                                                \
    type VPMPC_RMCM_3DLUT_FL_SEL;                                                                  \
    type VPCDC0_3DLUT_FL_MODE;                                                                     \
    type VPCDC0_3DLUT_FL_FORMAT;                                                                   \
    type VPCDC0_3DLUT_FL_BIAS;                                                                     \
    type VPCDC0_3DLUT_FL_SCALE;

struct vpe20_mpc_registers {
    MPC_REG_VARIABLE_LIST_VPE20
};

struct vpe20_mpc_shift {
    MPC_FIELD_VARIABLE_LIST_VPE20(uint8_t)
};

struct vpe20_mpc_mask {
    MPC_FIELD_VARIABLE_LIST_VPE20(uint32_t)
};

struct vpe20_mpc {
    struct mpc                    base;
    struct vpe20_mpc_registers   *regs;
    const struct vpe20_mpc_shift *shift;
    const struct vpe20_mpc_mask  *mask;
};

void vpe20_construct_mpc(struct vpe_priv *vpe_priv, struct mpc *mpc);

void vpe20_mpc_program_mpcc_mux(struct mpc *mpc, enum mpc_mpccid mpcc_idx,
    enum mpc_mux_topsel topsel, enum mpc_mux_botsel botsel, enum mpc_mux_outmux outmux,
    enum mpc_mux_oppid oppid);

void vpe20_mpc_program_mpcc_blending(
    struct mpc *mpc, enum mpc_mpccid mpcc_idx, struct mpcc_blnd_cfg *blnd_cfg);

void vpe20_mpc_power_on_1dlut_shaper_3dlut(struct mpc *mpc, bool power_on);

void vpe20_mpc_shaper_bypass(struct mpc *mpc, bool bypass);

bool vpe20_mpc_program_shaper(struct mpc *mpc, const struct pwl_params *params);

// using direct config to program the 3dlut specified in params
void vpe20_mpc_program_3dlut(struct mpc *mpc, const struct tetrahedral_params *params);

void vpe20_mpc_set_mpc_shaper_3dlut(
    struct mpc *mpc, struct transfer_func *func_shaper, struct vpe_3dlut *lut3d_func);

// using indirect config to configure the 3DLut
// note that we still need direct config to switch the mask between lut0 - lut3
bool vpe20_mpc_program_3dlut_indirect(struct mpc *mpc,
    struct vpe_buf *lut0_3_buf, // 3d lut buf which contains the data for lut0-lut3
    bool use_tetrahedral_17, bool use_12bits);

void vpe20_attach_3dlut_to_mpc_inst(struct mpc *mpc, enum mpc_mpccid mpcc_idx);

bool vpe20_mpc_program_movable_cm(struct mpc *mpc, struct transfer_func *func_shaper,
    struct vpe_3dlut *lut3d_func, struct transfer_func *blend_tf, bool afterblend);

void vpe20_mpc_set_gamut_remap2(struct mpc *mpc, struct colorspace_transform *gamut_remap,
    enum mpcc_gamut_remap_id mpcc_gamut_remap_block_id);

void vpe20_update_3dlut_fl_bias_scale(struct mpc *mpc, uint16_t bias, uint16_t scale);

void vpe20_mpc_program_3dlut_fl_config(struct mpc *mpc, enum vpe_3dlut_mem_layout layout,
    enum vpe_3dlut_mem_format format, bool enable);

void vpe20_mpc_program_3dlut_fl(struct mpc *mpc, enum lut_dimension lut_dimension, bool use_12bit);

#ifdef __cplusplus
}
#endif
