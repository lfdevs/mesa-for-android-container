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

#include "opp.h"
#include "reg_helper.h"
#include "vpe10_opp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Some HW registers have been renamed, and even though there are only few exceptions, all have
 * to be copied and set individually. The order is the same as in VPE10 so it's easy to compare,
 * but the only thing that matters is that they both have the same set of vars/registers.
 */

#define OPP_REG_LIST_VPE20(id)                                                                     \
    OPP_REG_LIST_VPE10_COMMON(id),                                                                 \
        SRIDFVL1(VPOPP_TOP_CLK_CONTROL),                                                           \
        SRIDFVL(VPFMT_SUBSAMPLER_MEMORY_CONTROL, VPFMT, id),                                       \
        SRIDFVL(VPOPP_PIPE_OUTBG_EXT1, VPOPP_PIPE, id),                                            \
        SRIDFVL(VPOPP_PIPE_OUTBG_EXT2, VPOPP_PIPE, id),                                            \
        SRIDFVL(VPOPP_PIPE_OUTBG_COL1, VPOPP_PIPE, id),                                            \
        SRIDFVL(VPOPP_PIPE_OUTBG_COL2, VPOPP_PIPE, id),                                            \
        SRIDFVL1(VPOPP_CRC_CONTROL),                                                               \
        SRIDFVL1(VPOPP_CRC_RESULT_RG),                                                             \
        SRIDFVL1(VPOPP_CRC_RESULT_BC),                                                             \
        SRIDFVL1(VPOPP_FROD_CONTROL),                                                              \
        SRIDFVL1(VPOPP_FROD_MEM_PWR_CONTROL)


#define OPP_FIELD_LIST_VPE20_COMMON(post_fix)                                                      \
    OPP_FIELD_LIST_VPE10_COMMON(post_fix),                                                         \
        SFRB(VPFMT_PIXEL_ENCODING, VPFMT_CONTROL, post_fix),                                       \
        SFRB(VPFMT_SUBSAMPLE_HTAPS, VPFMT_CONTROL, post_fix),                                      \
        SFRB(VPFMT_SUBSAMPLE_LEFT_EDGE, VPFMT_CONTROL, post_fix),                                  \
        SFRB(VPFMT_SUBSAMPLE_RIGHT_EDGE, VPFMT_CONTROL, post_fix),                                 \
        SFRB(VPFMT_SUBSAMPLE_VTAPS, VPFMT_CONTROL, post_fix),                                      \
        SFRB(VPFMT_SUBSAMPLE_TOP_EDGE, VPFMT_CONTROL, post_fix),                                   \
        SFRB(VPFMT_SUBSAMPLE_BOTTOM_EDGE, VPFMT_CONTROL, post_fix),                                \
        SFRB(VPFMT_SUBSAMPLER_MEM_PWR_FORCE, VPFMT_SUBSAMPLER_MEMORY_CONTROL, post_fix),           \
        SFRB(VPFMT_SUBSAMPLER_MEM_PWR_DIS, VPFMT_SUBSAMPLER_MEMORY_CONTROL, post_fix),             \
        SFRB(VPFMT_SUBSAMPLER_MEM_PWR_STATE, VPFMT_SUBSAMPLER_MEMORY_CONTROL, post_fix),           \
        SFRB(VPFMT_DEFAULT_MEM_LOW_POWER_STATE, VPFMT_SUBSAMPLER_MEMORY_CONTROL, post_fix),        \
        SFRB(OUTBG_EXT_TOP, VPOPP_PIPE_OUTBG_EXT1, post_fix),                                      \
        SFRB(OUTBG_EXT_BOT, VPOPP_PIPE_OUTBG_EXT1, post_fix),                                      \
        SFRB(OUTBG_EXT_LEFT, VPOPP_PIPE_OUTBG_EXT2, post_fix),                                     \
        SFRB(OUTBG_EXT_RIGHT, VPOPP_PIPE_OUTBG_EXT2, post_fix),                                    \
        SFRB(OUTBG_R_CR, VPOPP_PIPE_OUTBG_COL1, post_fix),                                         \
        SFRB(OUTBG_B_CB, VPOPP_PIPE_OUTBG_COL1, post_fix),                                         \
        SFRB(OUTBG_Y, VPOPP_PIPE_OUTBG_COL2, post_fix),                                            \
        SFRB(VPOPP_CRC_EN, VPOPP_CRC_CONTROL, post_fix),                                           \
        SFRB(VPOPP_CRC_CONT_EN, VPOPP_CRC_CONTROL, post_fix),                                      \
        SFRB(VPOPP_CRC_PIXEL_SELECT, VPOPP_CRC_CONTROL, post_fix),                                 \
        SFRB(VPOPP_CRC_SOURCE_SELECT, VPOPP_CRC_CONTROL, post_fix),                                \
        SFRB(VPOPP_CRC_PIPE_SELECT, VPOPP_CRC_CONTROL, post_fix),                                  \
        SFRB(VPOPP_CRC_MASK, VPOPP_CRC_CONTROL, post_fix),                                         \
        SFRB(VPOPP_CRC_ONE_SHOT_PENDING, VPOPP_CRC_CONTROL, post_fix),                             \
        SFRB(VPOPP_CRC_RESULT_R, VPOPP_CRC_RESULT_RG, post_fix),                                   \
        SFRB(VPOPP_CRC_RESULT_G, VPOPP_CRC_RESULT_RG, post_fix),                                   \
        SFRB(VPOPP_CRC_RESULT_B, VPOPP_CRC_RESULT_BC, post_fix),                                   \
        SFRB(VPOPP_CRC_RESULT_C, VPOPP_CRC_RESULT_BC, post_fix),                                   \
        SFRB(FROD_EN, VPOPP_FROD_CONTROL, post_fix),                                               \
        SFRB(FROD_MEM_PWR_FORCE, VPOPP_FROD_MEM_PWR_CONTROL, post_fix),                            \
        SFRB(FROD_MEM_PWR_DIS, VPOPP_FROD_MEM_PWR_CONTROL, post_fix),                              \
        SFRB(FROD_MEM_PWR_STATE, VPOPP_FROD_MEM_PWR_CONTROL, post_fix),                            \
        SFRB(FROD_MEM_DEFAULT_LOW_PWR_STATE, VPOPP_FROD_MEM_PWR_CONTROL, post_fix)

#define OPP_FIELD_LIST_VPE20(post_fix)                                                             \
    OPP_FIELD_LIST_VPE20_COMMON(post_fix),                                                         \
        SFRB(VPFMT_SPATIAL_DITHER_EN, VPFMT_BIT_DEPTH_CONTROL, post_fix),                          \
        SFRB(VPFMT_SPATIAL_DITHER_MODE, VPFMT_BIT_DEPTH_CONTROL, post_fix),                        \
        SFRB(VPFMT_SPATIAL_DITHER_DEPTH, VPFMT_BIT_DEPTH_CONTROL, post_fix),                       \
        SFRB(VPFMT_FRAME_RANDOM_ENABLE, VPFMT_BIT_DEPTH_CONTROL, post_fix),                        \
        SFRB(VPFMT_RGB_RANDOM_ENABLE, VPFMT_BIT_DEPTH_CONTROL, post_fix),                          \
        SFRB(VPFMT_HIGHPASS_RANDOM_ENABLE, VPFMT_BIT_DEPTH_CONTROL, post_fix),                     \
        SFRB(VPFMT_SPATIAL_DITHER_FRAME_COUNTER_MAX, VPFMT_CONTROL, post_fix),                     \
        SFRB(VPFMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP, VPFMT_CONTROL, post_fix),                \
        SFRB(VPFMT_RAND_R_SEED, VPFMT_DITHER_RAND_R_SEED, post_fix),                               \
        SFRB(VPFMT_RAND_G_SEED, VPFMT_DITHER_RAND_G_SEED, post_fix),                               \
        SFRB(VPFMT_RAND_B_SEED, VPFMT_DITHER_RAND_B_SEED, post_fix),                               \
        SFRB(VPOPP_PIPE_ALPHA_SEL, VPOPP_PIPE_CONTROL, post_fix),                                  \
        SFRB(VPOPP_PIPE_ALPHA, VPOPP_PIPE_CONTROL, post_fix)

#define OPP_REG_VARIABLE_LIST_VPE20                                                                \
    OPP_REG_VARIABLE_LIST_VPE10_COMMON                                                             \
    reg_id_val VPFMT_SUBSAMPLER_MEMORY_CONTROL;                                                    \
    reg_id_val VPOPP_PIPE_OUTBG_EXT1;                                                              \
    reg_id_val VPOPP_PIPE_OUTBG_EXT2;                                                              \
    reg_id_val VPOPP_PIPE_OUTBG_COL1;                                                              \
    reg_id_val VPOPP_PIPE_OUTBG_COL2;                                                              \
    reg_id_val VPOPP_CRC_CONTROL;                                                                  \
    reg_id_val VPOPP_CRC_RESULT_RG;                                                                \
    reg_id_val VPOPP_CRC_RESULT_BC;                                                                \
    reg_id_val VPOPP_FROD_CONTROL;                                                                 \
    reg_id_val VPOPP_FROD_MEM_PWR_CONTROL;

#define OPP_FIELD_VARIABLE_LIST_VPE20_COMMON(type)                                                 \
    OPP_FIELD_VARIABLE_LIST_VPE10_COMMON(type)                                                     \
    type VPFMT_PIXEL_ENCODING;                                                                     \
    type VPFMT_SUBSAMPLE_HTAPS;                                                                    \
    type VPFMT_SUBSAMPLE_LEFT_EDGE;                                                                \
    type VPFMT_SUBSAMPLE_RIGHT_EDGE;                                                               \
    type VPFMT_SUBSAMPLE_VTAPS;                                                                    \
    type VPFMT_SUBSAMPLE_TOP_EDGE;                                                                 \
    type VPFMT_SUBSAMPLE_BOTTOM_EDGE;                                                              \
    type VPFMT_SUBSAMPLER_MEM_PWR_FORCE;                                                           \
    type VPFMT_SUBSAMPLER_MEM_PWR_DIS;                                                             \
    type VPFMT_SUBSAMPLER_MEM_PWR_STATE;                                                           \
    type VPFMT_DEFAULT_MEM_LOW_POWER_STATE;                                                        \
    type OUTBG_EXT_TOP;                                                                            \
    type OUTBG_EXT_BOT;                                                                            \
    type OUTBG_EXT_LEFT;                                                                           \
    type OUTBG_EXT_RIGHT;                                                                          \
    type OUTBG_R_CR;                                                                               \
    type OUTBG_B_CB;                                                                               \
    type OUTBG_Y;                                                                                  \
    type VPOPP_CRC_EN;                                                                             \
    type VPOPP_CRC_CONT_EN;                                                                        \
    type VPOPP_CRC_PIXEL_SELECT;                                                                   \
    type VPOPP_CRC_SOURCE_SELECT;                                                                  \
    type VPOPP_CRC_PIPE_SELECT;                                                                    \
    type VPOPP_CRC_MASK;                                                                           \
    type VPOPP_CRC_ONE_SHOT_PENDING;                                                               \
    type VPOPP_CRC_RESULT_R;                                                                       \
    type VPOPP_CRC_RESULT_G;                                                                       \
    type VPOPP_CRC_RESULT_B;                                                                       \
    type VPOPP_CRC_RESULT_C;                                                                       \
    type FROD_EN;                                                                                  \
    type FROD_MEM_PWR_FORCE;                                                                       \
    type FROD_MEM_PWR_DIS;                                                                         \
    type FROD_MEM_PWR_STATE;                                                                       \
    type FROD_MEM_DEFAULT_LOW_PWR_STATE;

#define OPP_FIELD_VARIABLE_LIST_VPE20(type)                                                        \
    OPP_FIELD_VARIABLE_LIST_VPE20_COMMON(type)                                                     \
    type VPFMT_SPATIAL_DITHER_EN;                                                                  \
    type VPFMT_SPATIAL_DITHER_MODE;                                                                \
    type VPFMT_SPATIAL_DITHER_DEPTH;                                                               \
    type VPFMT_FRAME_RANDOM_ENABLE;                                                                \
    type VPFMT_RGB_RANDOM_ENABLE;                                                                  \
    type VPFMT_HIGHPASS_RANDOM_ENABLE;                                                             \
    type VPFMT_SPATIAL_DITHER_FRAME_COUNTER_MAX;                                                   \
    type VPFMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP;                                              \
    type VPFMT_RAND_R_SEED;                                                                        \
    type VPFMT_RAND_G_SEED;                                                                        \
    type VPFMT_RAND_B_SEED;                                                                        \
    type VPOPP_PIPE_ALPHA;                                                                         \
    type VPOPP_PIPE_ALPHA_SEL;

/* Variable list is the same as the one for VPE10 at the moment as it's the same set of registers.
 * Note that adding VPE2 specific variables must be done at the bottom so that casting can work.
 * See PROGRAM_ENTRY(),the order here matters, VPE1 subset must be in the same order in VPE2 list.
 */
struct vpe20_opp_registers {
    OPP_REG_VARIABLE_LIST_VPE20
};

struct vpe20_opp_shift {
    OPP_FIELD_VARIABLE_LIST_VPE20(uint8_t)
};

struct vpe20_opp_mask {
    OPP_FIELD_VARIABLE_LIST_VPE20(uint32_t)
};

struct vpe20_opp {
    struct opp                    base; // base class, must be the first field
    struct vpe20_opp_registers   *regs;
    const struct vpe20_opp_shift *shift;
    const struct vpe20_opp_mask  *mask;
};

void vpe20_construct_opp(struct vpe_priv *vpe_priv, struct opp *opp);

void vpe20_opp_build_fmt_subsample_params(struct opp *opp, enum vpe_surface_pixel_format format,
    enum subsampling_quality subsample_quality, enum chroma_cositing cositing,
    struct fmt_boundary_mode boundary_mode, struct fmt_subsampling_params *subsample_params);

void vpe20_opp_program_fmt_control(struct opp *opp, struct fmt_control_params *fmt_ctrl);

void vpe20_opp_program_bit_depth_reduction(
    struct opp *opp, const struct bit_depth_reduction_params *params);

void vpe20_opp_set_bg(struct opp* opp, struct vpe_rect target_rect, struct vpe_rect dst_rect,
    enum vpe_surface_pixel_format format, struct vpe_color bgcolor);

void vpe20_opp_program_pipe_crc(struct opp *opp, bool enable);

void vpe20_opp_program_frod(struct opp *opp, struct opp_frod_param *frod_param);

void vpe20_opp_get_fmt_extra_pixel(enum vpe_surface_pixel_format format,
    enum subsampling_quality subsample_quality, enum chroma_cositing cositing,
    struct fmt_extra_pixel_info *extra_pixel);

void vpe20_opp_program_pipe_control(struct opp *opp, const struct opp_pipe_control_params *params);

#ifdef __cplusplus
}
#endif
