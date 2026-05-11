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

#include "cdc.h"
#include "reg_helper.h"
#include "vpe10_cdc_fe.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Some HW registers have been renamed, and even though there are only few exceptions, all have
 * to be copied and set individually. The order is the same as in VPE10 so it's easy to compare,
 * but the only thing that matters is that they both have the same set of vars/registers.
 */
#define CDC_FE_REG_LIST_VPE20(id)                                                                  \
    SRIDFVL1(VPEP_MGCG_CNTL), SRIDFVL1(VPCDC_SOFT_RESET),                                          \
        SRIDFVL_CDC(SURFACE_CONFIG, VPCDC_FE, id), SRIDFVL_CDC(CROSSBAR_CONFIG, VPCDC_FE, id),     \
        SRIDFVL_CDC(VIEWPORT_START_CONFIG, VPCDC_FE, id),                                          \
        SRIDFVL_CDC(VIEWPORT_DIMENSION_CONFIG, VPCDC_FE, id),                                      \
        SRIDFVL_CDC(VIEWPORT_START_C_CONFIG, VPCDC_FE, id),                                        \
        SRIDFVL_CDC(VIEWPORT_DIMENSION_C_CONFIG, VPCDC_FE, id),                                    \
        SRIDFVL1(VPCDC_GLOBAL_SYNC_TRIGGER), SRIDFVL1(VPEP_MEM_GLOBAL_PWR_REQ_CNTL),               \
        SRIDFVL2(MEM_PWR_CNTL, VPFE, id), SRIDFVL2(MEM_PWR_CNTL, VPBE, id),                        \
        SRIDFVL1(VPCDC_3DLUT_FL_CONFIG)

#define CDC_FE_FIELD_LIST_VPE20_COMMON(post_fix)                                                   \
    SFRB(VPDPP0_CLK_GATE_DIS, VPEP_MGCG_CNTL, post_fix),                                           \
        SFRB(VPMPC_CLK_GATE_DIS, VPEP_MGCG_CNTL, post_fix),                                        \
        SFRB(VPOPP_CLK_GATE_DIS, VPEP_MGCG_CNTL, post_fix),                                        \
        SFRB(VPCDC_SOCCLK_G_GATE_DIS, VPEP_MGCG_CNTL, post_fix),                                   \
        SFRB(VPCDC_SOCCLK_R_GATE_DIS, VPEP_MGCG_CNTL, post_fix),                                   \
        SFRB(VPCDC_VPECLK_G_GATE_DIS, VPEP_MGCG_CNTL, post_fix),                                   \
        SFRB(VPCDC_VPECLK_R_GATE_DIS, VPEP_MGCG_CNTL, post_fix),                                   \
        SFRB(VPCDC_SOCCLK_SOFT_RESET, VPCDC_SOFT_RESET, post_fix),                                 \
        SFRB(VPCDC_VPECLK_SOFT_RESET, VPCDC_SOFT_RESET, post_fix),                                 \
        SFRB(SURFACE_PIXEL_FORMAT_FE0, VPCDC_FE0_SURFACE_CONFIG, post_fix),                        \
        SFRB(ROTATION_ANGLE_FE0, VPCDC_FE0_SURFACE_CONFIG, post_fix),                              \
        SFRB(H_MIRROR_EN_FE0, VPCDC_FE0_SURFACE_CONFIG, post_fix),                                 \
        SFRB(PIX_SURFACE_LINEAR_FE0, VPCDC_FE0_SURFACE_CONFIG, post_fix),                          \
        SFRB(CROSSBAR_SRC_ALPHA_FE0, VPCDC_FE0_CROSSBAR_CONFIG, post_fix),                         \
        SFRB(CROSSBAR_SRC_Y_G_FE0, VPCDC_FE0_CROSSBAR_CONFIG, post_fix),                           \
        SFRB(CROSSBAR_SRC_CB_B_FE0, VPCDC_FE0_CROSSBAR_CONFIG, post_fix),                          \
        SFRB(CROSSBAR_SRC_CR_R_FE0, VPCDC_FE0_CROSSBAR_CONFIG, post_fix),                          \
        SFRB(VIEWPORT_X_START_FE0, VPCDC_FE0_VIEWPORT_START_CONFIG, post_fix),                     \
        SFRB(VIEWPORT_Y_START_FE0, VPCDC_FE0_VIEWPORT_START_CONFIG, post_fix),                     \
        SFRB(VIEWPORT_WIDTH_FE0, VPCDC_FE0_VIEWPORT_DIMENSION_CONFIG, post_fix),                   \
        SFRB(VIEWPORT_HEIGHT_FE0, VPCDC_FE0_VIEWPORT_DIMENSION_CONFIG, post_fix),                  \
        SFRB(VIEWPORT_X_START_C_FE0, VPCDC_FE0_VIEWPORT_START_C_CONFIG, post_fix),                 \
        SFRB(VIEWPORT_Y_START_C_FE0, VPCDC_FE0_VIEWPORT_START_C_CONFIG, post_fix),                 \
        SFRB(VIEWPORT_WIDTH_C_FE0, VPCDC_FE0_VIEWPORT_DIMENSION_C_CONFIG, post_fix),               \
        SFRB(VIEWPORT_HEIGHT_C_FE0, VPCDC_FE0_VIEWPORT_DIMENSION_C_CONFIG, post_fix),              \
        SFRB(VPBE_GS_TRIG, VPCDC_GLOBAL_SYNC_TRIGGER, post_fix),                                   \
        SFRB(VPFE_VR_STATUS, VPCDC_VREADY_STATUS, post_fix),                                       \
        SFRB(MEM_GLOBAL_PWR_REQ_DIS, VPEP_MEM_GLOBAL_PWR_REQ_CNTL, post_fix),                      \
        SFRB(VPFE0_MEM_PWR_FORCE, VPFE0_MEM_PWR_CNTL, post_fix),                                   \
        SFRB(VPFE0_MEM_PWR_MODE, VPFE0_MEM_PWR_CNTL, post_fix),                                    \
        SFRB(VPFE0_MEM_PWR_STATE, VPFE0_MEM_PWR_CNTL, post_fix),                                   \
        SFRB(VPFE0_MEM_PWR_DIS, VPFE0_MEM_PWR_CNTL, post_fix),                                     \
        SFRB(VPBE0_MEM_PWR_FORCE, VPBE0_MEM_PWR_CNTL, post_fix),                                   \
        SFRB(VPBE0_MEM_PWR_MODE, VPBE0_MEM_PWR_CNTL, post_fix),                                    \
        SFRB(VPBE0_MEM_PWR_STATE, VPBE0_MEM_PWR_CNTL, post_fix),                                   \
        SFRB(VPBE0_MEM_PWR_DIS, VPBE0_MEM_PWR_CNTL, post_fix),                                     \
        SFRB(VPCDC_3DLUT_FL_CROSSBAR_SRC_G, VPCDC_3DLUT_FL_CONFIG, post_fix),                      \
        SFRB(VPCDC_3DLUT_FL_CROSSBAR_SRC_B, VPCDC_3DLUT_FL_CONFIG, post_fix),                      \
        SFRB(VPCDC_3DLUT_FL_CROSSBAR_SRC_R, VPCDC_3DLUT_FL_CONFIG, post_fix)

#define CDC_FE_FIELD_LIST_VPE20(post_fix)                                                          \
    CDC_FE_FIELD_LIST_VPE20_COMMON(post_fix),                                                      \
        SFRB(VPCDC_3DLUT_FL_MODE, VPCDC_3DLUT_FL_CONFIG, post_fix),                                \
        SFRB(VPCDC_3DLUT_FL_SIZE, VPCDC_3DLUT_FL_CONFIG, post_fix)

#define CDC_FE_REG_VARIABLE_LIST_VPE20_COMMON                                                      \
    CDC_FE_REG_VARIABLE_LIST_VPE10                                                                 \
    reg_id_val VPCDC_3DLUT_FL_CONFIG;

#define CDC_FE_REG_VARIABLE_LIST_VPE20 CDC_FE_REG_VARIABLE_LIST_VPE20_COMMON

#define CDC_FE_FIELD_VARIABLE_LIST_VPE20_COMMON(type)                                              \
    CDC_FE_FIELD_VARIABLE_LIST_VPE10(type)                                                         \
    type VPCDC_3DLUT_FL_CROSSBAR_SRC_G;                                                            \
    type VPCDC_3DLUT_FL_CROSSBAR_SRC_B;                                                            \
    type VPCDC_3DLUT_FL_CROSSBAR_SRC_R;

#define CDC_FE_FIELD_VARIABLE_LIST_VPE20(type)                                                     \
    CDC_FE_FIELD_VARIABLE_LIST_VPE20_COMMON(type)                                                  \
    type VPCDC_3DLUT_FL_MODE;                                                                      \
    type VPCDC_3DLUT_FL_SIZE;

/* Variable list is the same as the one for VPE10 at the moment as it's the same set of registers.
 * Note that adding VPE2 specific variables must be done at the bottom so that casting can work.
 * See PROGRAM_ENTRY(),the order here matters, VPE1 subset must be in the same order in VPE2 list.
 */
struct vpe20_cdc_fe_registers {
    CDC_FE_REG_VARIABLE_LIST_VPE20
};

struct vpe20_cdc_fe_shift {
    CDC_FE_FIELD_VARIABLE_LIST_VPE20(uint8_t)
};

struct vpe20_cdc_fe_mask {
    CDC_FE_FIELD_VARIABLE_LIST_VPE20(uint32_t)
};

struct vpe20_cdc_fe {
    struct cdc_fe                    base; // base class, must be the first field
    struct vpe20_cdc_fe_registers   *regs;
    const struct vpe20_cdc_fe_shift *shift;
    const struct vpe20_cdc_fe_mask  *mask;
};

void vpe20_construct_cdc_fe(struct vpe_priv *vpe_priv, struct cdc_fe *cdc_fe);

void vpe20_cdc_program_surface_config(struct cdc_fe *cdc_fe, enum vpe_surface_pixel_format format,
    enum vpe_rotation_angle rotation, bool horizontal_mirror, enum vpe_swizzle_mode_values swizzle);

void vpe20_cdc_program_crossbar_config(struct cdc_fe *cdc_fe, enum vpe_surface_pixel_format format);

void vpe20_cdc_program_viewport(
    struct cdc_fe *cdc_fe, const struct vpe_rect *viewport, const struct vpe_rect *viewport_c);

void vpe20_program_3dlut_fl_config(
    struct cdc_fe *cdc_fe, enum lut_dimension lut_dimension, struct vpe_3dlut *lut_3d);

#ifdef __cplusplus
}
#endif
