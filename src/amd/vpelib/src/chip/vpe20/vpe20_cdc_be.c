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
#include "vpe20_cdc_be.h"

#define CTX_BASE cdc_be
#define CTX      vpe20_cdc_be

enum mux_sel {
    MUX_SEL_ALPHA = 0,
    MUX_SEL_Y_G   = 1,
    MUX_SEL_CB_B  = 2,
    MUX_SEL_CR_R  = 3
};

static struct cdc_be_funcs cdc_func = {
    .program_cdc_control = vpe20_cdc_program_control,
    .program_global_sync = vpe20_cdc_program_global_sync,
    .program_p2b_config  = vpe20_cdc_program_p2b_config,
};

void vpe20_cdc_program_control(struct cdc_be *cdc_be, uint8_t enable_frod, uint32_t hist_dsets[])
{
    PROGRAM_ENTRY();
    REG_SET_3(VPCDC_CONTROL, 0, VPCDC_FROD_EN, enable_frod, VPCDC_HISTOGRAM0_EN, hist_dsets[0],
        VPCDC_HISTOGRAM1_EN, hist_dsets[1]);
}

void vpe20_cdc_program_global_sync(
    struct cdc_be *cdc_be, uint32_t vupdate_offset, uint32_t vupdate_width, uint32_t vready_offset)
{
    PROGRAM_ENTRY();

    REG_SET_3(VPCDC_BE0_GLOBAL_SYNC_CONFIG, 0, BE0_VUPDATE_OFFSET, vupdate_offset,
        BE0_VUPDATE_WIDTH, vupdate_width, BE0_VREADY_OFFSET, vready_offset);
}
void vpe20_cdc_program_p2b_config(struct cdc_be *cdc_be, enum vpe_surface_pixel_format format,
    enum vpe_swizzle_mode_values swizzle, const struct vpe_rect *viewport,
    const struct vpe_rect *viewport_c)
{
    uint32_t bar_sel0       = (uint32_t)MUX_SEL_CB_B;
    uint32_t bar_sel1       = (uint32_t)MUX_SEL_Y_G;
    uint32_t bar_sel2       = (uint32_t)MUX_SEL_CR_R;
    uint32_t bar_sel3       = (uint32_t)MUX_SEL_ALPHA;
    uint32_t p2b_format_sel = 0;
    uint32_t tile_mode      = swizzle == VPE_SW_LINEAR ? 0 : 1;
    uint32_t x_start_plane0 = 0;
    uint32_t x_start_plane1 = 0;

    PROGRAM_ENTRY();

    if (viewport != NULL) {
        x_start_plane0 = viewport->x;
    }

    if (viewport_c != NULL) {
        x_start_plane1 = viewport_c->x;
    }

    // Conversion to the element coordinate of x_start_plane0 is only required for packed 422
    // formats as only these formats' pixel sizes and element sizes differ among supported formats
    if (vpe_is_yuv422(format) && vpe_is_yuv_packed(format)) {
        x_start_plane0 = x_start_plane0 / 2;
    }

    switch (tile_mode) {
    case VPE_SW_LINEAR:
        tile_mode = 0;
        x_start_plane0 = x_start_plane0 % (32 / (uint32_t)vpe_get_element_size_in_bytes(format, 0));
        x_start_plane1 = x_start_plane1 % (32 / (uint32_t)vpe_get_element_size_in_bytes(format, 1));
        // Pre-divided by 2
        break;
    default:
        tile_mode = 1;
        x_start_plane0 = x_start_plane0 % 4;
        x_start_plane1 = x_start_plane1 % 4;
    }

    switch (format) {
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBX8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRX8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_XRGB8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_XBGR8888:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrCbYA8888:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA8888:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_AYCrCb8888:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_YCrCbA8888:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb8888:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_AYCbCr8888:
        p2b_format_sel = 8;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb2101010:
        p2b_format_sel = 10;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA1010102:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA1010102:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA1010102:
        p2b_format_sel = 11;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
        p2b_format_sel = 20;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA16161616:
        p2b_format_sel = 21;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616_UNORM:
        p2b_format_sel = 26;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616_UNORM:
        p2b_format_sel = 27;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616_SNORM:
        p2b_format_sel = 28;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616_SNORM:
        p2b_format_sel = 29;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA12121212:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb12121212:
        p2b_format_sel = 24;
        break;
        // Planar YUV formats
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCrCb:
        p2b_format_sel = 0x40;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCbCr:
        p2b_format_sel = 0x41;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCrCb:
        p2b_format_sel = 0x42;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCbCr:
        p2b_format_sel = 0x43;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_12bpc_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCrCb:
        p2b_format_sel = 0x44;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_12bpc_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCbCr:
        p2b_format_sel = 0x45;
        break;
        // Packed YUV formats
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCrYCb:
        p2b_format_sel = 0x48;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCbYCr:
        p2b_format_sel = 0x49;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_CrYCbY:
        p2b_format_sel = 0x4a;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_CbYCrY:
        p2b_format_sel = 0x4b;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCrYCb:
        p2b_format_sel = 0x4c;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCbYCr:
        p2b_format_sel = 0x4d;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_CrYCbY:
        p2b_format_sel = 0x4e;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_CbYCrY:
        p2b_format_sel = 0x4f;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCrYCb:
        p2b_format_sel = 0x50;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCbYCr:
        p2b_format_sel = 0x51;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_CrYCbY:
        p2b_format_sel = 0x52;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_CbYCrY:
        p2b_format_sel = 0x53;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FIX:
        p2b_format_sel = 0x70;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FIX:
        p2b_format_sel = 0x71;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FLOAT:
        p2b_format_sel = 0x76;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FLOAT:
        p2b_format_sel = 0x77;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_R8:
        p2b_format_sel = 0x78;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_R16:
        p2b_format_sel = 0x7D;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_8bpc_RGB:
        p2b_format_sel = 0x109;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_8bpc_YCbCr:
        p2b_format_sel = 0x10D;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_RGB:
        p2b_format_sel = 0x115;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_RGB_FLOAT:
        p2b_format_sel = 0x118;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_YCbCr:
        p2b_format_sel = 0x12A;
        break;
    default:
        VPE_ASSERT(0);
        break;
    }

    switch (format) {
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_8bpc_RGB:
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_8bpc_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_RGB:
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_RGB_FLOAT:
        bar_sel3 = (uint32_t)MUX_SEL_CR_R;
        bar_sel2 = (uint32_t)MUX_SEL_CB_B;
        bar_sel1 = (uint32_t)MUX_SEL_Y_G;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_12bpc_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCrCb:
        bar_sel3 = (uint32_t)MUX_SEL_CR_R;
        bar_sel2 = (uint32_t)MUX_SEL_CB_B;
        bar_sel1 = (uint32_t)MUX_SEL_Y_G;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_12bpc_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCbCr:
        bar_sel3 = (uint32_t)MUX_SEL_CB_B;
        bar_sel2 = (uint32_t)MUX_SEL_CR_R;
        bar_sel1 = (uint32_t)MUX_SEL_Y_G;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCrYCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCrYCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCrYCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_CrYCbY:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_CrYCbY:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_CrYCbY:
        bar_sel3 = (uint32_t)MUX_SEL_CR_R;
        bar_sel2 = (uint32_t)MUX_SEL_CB_B;
        bar_sel1 = (uint32_t)MUX_SEL_Y_G;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCbYCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCbYCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCbYCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_CbYCrY:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_CbYCrY:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_CbYCrY:
        bar_sel3 = (uint32_t)MUX_SEL_CB_B;
        bar_sel2 = (uint32_t)MUX_SEL_CR_R;
        bar_sel1 = (uint32_t)MUX_SEL_Y_G;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBX8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA1010102:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA16161616:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA1010102:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA12121212:
        bar_sel3 = (uint32_t)MUX_SEL_CR_R;
        bar_sel2 = (uint32_t)MUX_SEL_Y_G;
        bar_sel1 = (uint32_t)MUX_SEL_CB_B;
        bar_sel0 = (uint32_t)MUX_SEL_ALPHA;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_XBGR8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616_UNORM:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616_SNORM:
        bar_sel3 = (uint32_t)MUX_SEL_ALPHA;
        bar_sel2 = (uint32_t)MUX_SEL_CB_B;
        bar_sel1 = (uint32_t)MUX_SEL_Y_G;
        bar_sel0 = (uint32_t)MUX_SEL_CR_R;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRX8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA1010102:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616_UNORM:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616_SNORM:
        bar_sel3 = (uint32_t)MUX_SEL_CB_B;
        bar_sel2 = (uint32_t)MUX_SEL_Y_G;
        bar_sel1 = (uint32_t)MUX_SEL_CR_R;
        bar_sel0 = (uint32_t)MUX_SEL_ALPHA;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
        bar_sel3 = (uint32_t)MUX_SEL_ALPHA;
        bar_sel2 = (uint32_t)MUX_SEL_CR_R;
        bar_sel1 = (uint32_t)MUX_SEL_Y_G;
        bar_sel0 = (uint32_t)MUX_SEL_CB_B;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
        bar_sel3 = (uint32_t)MUX_SEL_ALPHA;
        bar_sel2 = (uint32_t)MUX_SEL_CB_B;
        bar_sel1 = (uint32_t)MUX_SEL_Y_G;
        bar_sel0 = (uint32_t)MUX_SEL_CR_R;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA8888:
        bar_sel3 = (uint32_t)MUX_SEL_CR_R;
        bar_sel2 = (uint32_t)MUX_SEL_Y_G;
        bar_sel1 = (uint32_t)MUX_SEL_CB_B;
        bar_sel0 = (uint32_t)MUX_SEL_ALPHA;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrCbYA8888:
        bar_sel3 = (uint32_t)MUX_SEL_CR_R;
        bar_sel2 = (uint32_t)MUX_SEL_CB_B;
        bar_sel1 = (uint32_t)MUX_SEL_Y_G;
        bar_sel0 = (uint32_t)MUX_SEL_ALPHA;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_AYCrCb8888:
        bar_sel2 = (uint32_t)MUX_SEL_Y_G;
        bar_sel1 = (uint32_t)MUX_SEL_CR_R;
        bar_sel0 = (uint32_t)MUX_SEL_CB_B;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_YCrCbA8888:
        bar_sel3 = (uint32_t)MUX_SEL_Y_G;
        bar_sel2 = (uint32_t)MUX_SEL_CR_R;
        bar_sel1 = (uint32_t)MUX_SEL_CB_B;
        bar_sel0 = (uint32_t)MUX_SEL_ALPHA;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb8888:
        bar_sel2 = (uint32_t)MUX_SEL_CR_R;
        bar_sel1 = (uint32_t)MUX_SEL_Y_G;
        bar_sel0 = (uint32_t)MUX_SEL_CB_B;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_AYCbCr8888:
        bar_sel2 = (uint32_t)MUX_SEL_Y_G;
        bar_sel1 = (uint32_t)MUX_SEL_CB_B;
        bar_sel0 = (uint32_t)MUX_SEL_CR_R;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_R8:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_R16:
        bar_sel3 = (uint32_t)MUX_SEL_Y_G;
        bar_sel2 = (uint32_t)MUX_SEL_CB_B;
        bar_sel1 = (uint32_t)MUX_SEL_CR_R;
        bar_sel0 = (uint32_t)MUX_SEL_ALPHA;
        break;
    default:
        break;
    }

    REG_SET_8(VPCDC_BE0_P2B_CONFIG, 0, VPCDC_BE0_P2B_XBAR_SEL0, bar_sel0, VPCDC_BE0_P2B_XBAR_SEL1,
        bar_sel1, VPCDC_BE0_P2B_XBAR_SEL2, bar_sel2, VPCDC_BE0_P2B_XBAR_SEL3, bar_sel3,
        VPCDC_BE0_P2B_FORMAT_SEL, p2b_format_sel, VPCDC_BE0_P2B_TILED, tile_mode,
        VPCDC_BE0_P2B_X_START_PLANE0, x_start_plane0, VPCDC_BE0_P2B_X_START_PLANE1, x_start_plane1);
}

void vpe20_construct_cdc_be(struct vpe_priv *vpe_priv, struct cdc_be *cdc_be)
{
    cdc_be->vpe_priv = vpe_priv;
    cdc_be->funcs    = &cdc_func;
}

