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
#include "vpe10_cdc_be.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VPE20_CDC_VUPDATE_OFFSET_DEFAULT (20)
#define VPE20_CDC_VUPDATE_WIDTH_DEFAULT  (60)
#define VPE20_CDC_VREADY_OFFSET_DEFAULT  (150)

/* Some HW registers have been renamed, and even though there are only few exceptions, all have
 * to be copied and set individually. The order is the same as in VPE10 so it's easy to compare,
 * but the only thing that matters is that they both have the same set of vars/registers.
 */
#define CDC_BE_REG_LIST_VPE20(id)                                                                  \
    SRIDFVL_CDC(P2B_CONFIG, VPCDC_BE, id),                                                         \
    SRIDFVL_CDC(GLOBAL_SYNC_CONFIG, VPCDC_BE, id),                                                 \
    SRIDFVL1(VPCDC_CONTROL)

#define CDC_BE_FIELD_LIST_VPE20(post_fix)                                                          \
    SFRB(VPCDC_BE0_P2B_XBAR_SEL0, VPCDC_BE0_P2B_CONFIG, post_fix),                                 \
        SFRB(VPCDC_BE0_P2B_XBAR_SEL1, VPCDC_BE0_P2B_CONFIG, post_fix),                             \
        SFRB(VPCDC_BE0_P2B_XBAR_SEL2, VPCDC_BE0_P2B_CONFIG, post_fix),                             \
        SFRB(VPCDC_BE0_P2B_XBAR_SEL3, VPCDC_BE0_P2B_CONFIG, post_fix),                             \
        SFRB(VPCDC_BE0_P2B_FORMAT_SEL, VPCDC_BE0_P2B_CONFIG, post_fix),                            \
        SFRB(VPCDC_BE0_P2B_TILED, VPCDC_BE0_P2B_CONFIG, post_fix),                                 \
        SFRB(VPCDC_BE0_P2B_X_START_PLANE0, VPCDC_BE0_P2B_CONFIG, post_fix),                        \
        SFRB(VPCDC_BE0_P2B_X_START_PLANE1, VPCDC_BE0_P2B_CONFIG, post_fix),                        \
        SFRB(BE0_VUPDATE_OFFSET, VPCDC_BE0_GLOBAL_SYNC_CONFIG, post_fix),                          \
        SFRB(BE0_VUPDATE_WIDTH, VPCDC_BE0_GLOBAL_SYNC_CONFIG, post_fix),                           \
        SFRB(BE0_VREADY_OFFSET, VPCDC_BE0_GLOBAL_SYNC_CONFIG, post_fix),                           \
        SFRB(VPCDC_FROD_EN, VPCDC_CONTROL, post_fix),                                              \
        SFRB(VPCDC_HISTOGRAM0_EN, VPCDC_CONTROL, post_fix),                                        \
        SFRB(VPCDC_HISTOGRAM1_EN, VPCDC_CONTROL, post_fix)


#define CDC_BE_REG_VARIABLE_LIST_VPE20                                                             \
    CDC_BE_REG_VARIABLE_LIST_VPE10                                                                 \
    reg_id_val VPCDC_CONTROL;


#define CDC_BE_FIELD_VARIABLE_LIST_VPE20(type)                                                     \
    CDC_BE_FIELD_VARIABLE_LIST_VPE10(type)                                                         \
    type VPCDC_BE0_P2B_TILED;                                                                      \
    type VPCDC_BE0_P2B_X_START_PLANE0;                                                             \
    type VPCDC_BE0_P2B_X_START_PLANE1;                                                             \
    type VPCDC_FROD_EN;                                                                            \
    type VPCDC_HISTOGRAM0_EN;                                                                      \
    type VPCDC_HISTOGRAM1_EN;


/* Variable list is the same as the one for VPE10 at the moment as it's the same set of registers.
 * Note that adding VPE2 specific variables must be done at the bottom so that casting can work.
 * See PROGRAM_ENTRY(),the order here matters, VPE1 subset must be in the same order in VPE2 list.
 */
struct vpe20_cdc_be_registers {
    CDC_BE_REG_VARIABLE_LIST_VPE20
};

struct vpe20_cdc_be_shift {
    CDC_BE_FIELD_VARIABLE_LIST_VPE20(uint8_t)
};

struct vpe20_cdc_be_mask {
    CDC_BE_FIELD_VARIABLE_LIST_VPE20(uint32_t)
};

struct vpe20_cdc_be {
    struct cdc_be                    base; // base class, must be the first field
    struct vpe20_cdc_be_registers   *regs;
    const struct vpe20_cdc_be_shift *shift;
    const struct vpe20_cdc_be_mask  *mask;
};

void vpe20_construct_cdc_be(struct vpe_priv *vpe_priv, struct cdc_be *cdc_be);

void vpe20_cdc_program_global_sync(
    struct cdc_be *cdc_be, uint32_t vupdate_offset, uint32_t vupdate_width, uint32_t vready_offset);

void vpe20_cdc_program_p2b_config(struct cdc_be *cdc_be, enum vpe_surface_pixel_format format,
    enum vpe_swizzle_mode_values swizzle, const struct vpe_rect *viewport,
    const struct vpe_rect *viewport_c);

void vpe20_cdc_program_control(struct cdc_be *cdc_be, uint8_t enable_frod, uint32_t hist_dsets[]);

void vpe20_cdc_program_histo(struct cdc_be *cdc_be);
#ifdef __cplusplus
}
#endif
