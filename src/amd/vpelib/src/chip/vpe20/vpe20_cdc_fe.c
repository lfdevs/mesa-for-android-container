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

#include "common.h"
#include "vpe_priv.h"
#include "vpe20_cdc_fe.h"
#include "vpe20_resource.h"

#define CTX_BASE cdc_fe
#define CTX      vpe20_cdc_fe

enum mux_sel {
    MUX_SEL_ALPHA = 0,
    MUX_SEL_Y_G = 1,
    MUX_SEL_CB_B = 2,
    MUX_SEL_CR_R = 3
};

static struct cdc_fe_funcs cdc_fe_func = {
    .program_surface_config  = vpe20_cdc_program_surface_config,
    .program_crossbar_config = vpe20_cdc_program_crossbar_config,
    .program_3dlut_fl_config = vpe20_program_3dlut_fl_config,
    .program_viewport        = vpe20_cdc_program_viewport,
};

void vpe20_construct_cdc_fe(struct vpe_priv *vpe_priv, struct cdc_fe *cdc_fe)
{
    cdc_fe->vpe_priv = vpe_priv;
    cdc_fe->funcs    = &cdc_fe_func;
}

void vpe20_cdc_program_surface_config(struct cdc_fe *cdc_fe, enum vpe_surface_pixel_format format,
    enum vpe_rotation_angle rotation, bool horizontal_mirror, enum vpe_swizzle_mode_values swizzle)
{
    uint32_t surface_linear = 0;
    uint32_t rotation_angle = 0;
    uint32_t surf_format    = 8;

    PROGRAM_ENTRY();

    /* Program rotation angle and horz mirror - no mirror */
    if (rotation == VPE_ROTATION_ANGLE_0)
        rotation_angle = 0;
    else if (rotation == VPE_ROTATION_ANGLE_90)
        rotation_angle = 1;
    else if (rotation == VPE_ROTATION_ANGLE_180)
        rotation_angle = 2;
    else if (rotation == VPE_ROTATION_ANGLE_270)
        rotation_angle = 3;

    if (swizzle == VPE_SW_LINEAR)
        surface_linear = 1;
    else
        surface_linear = 0;

    surf_format = vpe20_get_hw_surface_format(format);

    REG_SET_4(VPCDC_FE0_SURFACE_CONFIG, 0, SURFACE_PIXEL_FORMAT_FE0, surf_format,
        ROTATION_ANGLE_FE0, rotation_angle, H_MIRROR_EN_FE0, (unsigned)horizontal_mirror,
        PIX_SURFACE_LINEAR_FE0, surface_linear);
}

void vpe20_cdc_program_crossbar_config(struct cdc_fe *cdc_fe, enum vpe_surface_pixel_format format)
{
    uint32_t alpha_bar = (uint32_t)MUX_SEL_ALPHA;
    uint32_t green_bar = (uint32_t)MUX_SEL_Y_G;
    uint32_t red_bar = (uint32_t)MUX_SEL_CR_R;
    uint32_t blue_bar = (uint32_t)MUX_SEL_CB_B;

    PROGRAM_ENTRY();

    if (format == VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR8888 ||
        format == VPE_SURFACE_PIXEL_FORMAT_GRPH_XBGR8888 ||
        format == VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010 ||
        format == VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F ||
        format == VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA8888 ||
        format == VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRX8888 ||
        format == VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA1010102 ||
        format == VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616F ||
        format == VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCbYCr ||
        format == VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_CbYCrY ||
        format == VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCbYCr ||
        format == VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_CbYCrY ||
        format == VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCbYCr ||
        format == VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_CbYCrY ||
        format == VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616 ||
        format == VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616_UNORM ||
        format == VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616_SNORM ||
        format == VPE_SURFACE_PIXEL_FORMAT_VIDEO_AYCbCr8888) {
        red_bar = MUX_SEL_CB_B;
        blue_bar = MUX_SEL_CR_R;
    }

    if (format == VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrCbYA8888) {
        blue_bar  = MUX_SEL_Y_G;
        green_bar = MUX_SEL_CB_B;
    }
    REG_SET_4(VPCDC_FE0_CROSSBAR_CONFIG, 0, CROSSBAR_SRC_ALPHA_FE0, alpha_bar,
        CROSSBAR_SRC_CR_R_FE0, red_bar, CROSSBAR_SRC_Y_G_FE0, green_bar, CROSSBAR_SRC_CB_B_FE0,
        blue_bar);
}

void vpe20_cdc_program_viewport(
    struct cdc_fe *cdc_fe, const struct vpe_rect *viewport, const struct vpe_rect *viewport_c)
{

    PROGRAM_ENTRY();

    REG_SET_2(VPCDC_FE0_VIEWPORT_START_CONFIG, 0, VIEWPORT_X_START_FE0, viewport->x,
        VIEWPORT_Y_START_FE0, viewport->y);

    REG_SET_2(VPCDC_FE0_VIEWPORT_DIMENSION_CONFIG, 0, VIEWPORT_WIDTH_FE0, viewport->width,
        VIEWPORT_HEIGHT_FE0, viewport->height);

    REG_SET_2(VPCDC_FE0_VIEWPORT_START_C_CONFIG, 0, VIEWPORT_X_START_C_FE0, viewport_c->x,
        VIEWPORT_Y_START_C_FE0, viewport_c->y);

    REG_SET_2(VPCDC_FE0_VIEWPORT_DIMENSION_C_CONFIG, 0, VIEWPORT_WIDTH_C_FE0, viewport_c->width,
        VIEWPORT_HEIGHT_C_FE0, viewport_c->height);
}

void vpe20_program_3dlut_fl_config(
    struct cdc_fe *cdc_fe, enum lut_dimension lut_dimension, struct vpe_3dlut *lut_3d)
{
    PROGRAM_ENTRY();

    REG_SET_5(VPCDC_3DLUT_FL_CONFIG, 0,
        VPCDC_3DLUT_FL_CROSSBAR_SRC_G, lut_3d->dma_params.crossbar_g,
        VPCDC_3DLUT_FL_CROSSBAR_SRC_B, lut_3d->dma_params.crossbar_b,
        VPCDC_3DLUT_FL_CROSSBAR_SRC_R, lut_3d->dma_params.crossbar_r,
        VPCDC_3DLUT_FL_MODE, lut_3d->dma_params.layout,
        VPCDC_3DLUT_FL_SIZE, lut_dimension == LUT_DIM_33 ? 1 : 0);
}
