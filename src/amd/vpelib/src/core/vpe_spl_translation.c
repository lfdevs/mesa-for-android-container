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
#include "vpe_spl_translation.h"
#include "color.h"
#include "vpe_hw_types.h"
#include "common.h"

const struct spl_sharpness_range SHARPNESS_RANGE = {
    0, 1750, 750,  // SDR RGB Min, Max, Mid
    0, 3500, 1500, // SDR YUV Min, Max, Mid
    0, 2750, 1500  // HDR RGB Min, Max, Mid
};

static void spl_rect_to_vpe_rect(struct spl_rect *spl_rect, struct vpe_rect *vpe_rect)
{
    vpe_rect->height = spl_rect->height;
    vpe_rect->width  = spl_rect->width;
    vpe_rect->x      = spl_rect->x;
    vpe_rect->y      = spl_rect->y;
}

static void vpe_rect_to_spl_rect(struct vpe_rect *vpe_rect, struct spl_rect *spl_rect)
{
    spl_rect->height = vpe_rect->height;
    spl_rect->width  = vpe_rect->width;
    spl_rect->x      = vpe_rect->x;
    spl_rect->y      = vpe_rect->y;
}

struct spl_rotation_mirror_map {
    enum spl_rotation_angle rotation;
    bool                    h_mirror;
};

static struct spl_rotation_mirror_map spl_scan_map[] = {
    {SPL_ROTATION_ANGLE_0, false},
    {SPL_ROTATION_ANGLE_270, false},
    {SPL_ROTATION_ANGLE_180, false},
    {SPL_ROTATION_ANGLE_90, false},
    {SPL_ROTATION_ANGLE_0, true},
    {SPL_ROTATION_ANGLE_270, true},
    {SPL_ROTATION_ANGLE_180, true},
    {SPL_ROTATION_ANGLE_90, true},
};

static void get_spl_rotation(
    enum vpe_scan_direction scan_dir, enum spl_rotation_angle *spl_angle, bool *spl_h_mirror)
{
    // VPE and DCN HW rotate in opposite directions.
    // 90 degree rotation in VPE corresponds to 270 degree rotation in DCN,
    // and vice versa.
    *spl_angle    = spl_scan_map[scan_dir].rotation;
    *spl_h_mirror = spl_scan_map[scan_dir].h_mirror;
}

static enum chroma_cositing get_spl_cositing(enum vpe_chroma_cositing cositing)
{
    switch (cositing) {
    case VPE_CHROMA_COSITING_NONE:
        return CHROMA_COSITING_NONE;
    case VPE_CHROMA_COSITING_LEFT:
        return CHROMA_COSITING_LEFT;
    case VPE_CHROMA_COSITING_TOPLEFT:
        return CHROMA_COSITING_TOPLEFT;
    default:
        VPE_ASSERT(false);
        return CHROMA_COSITING_NONE;
    }
}

static enum spl_pixel_format get_spl_format(enum vpe_surface_pixel_format fmt)
{
    // 8/10/16 bit differences in formats does not affect SPL
    switch (fmt) {
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGB565:
        return SPL_PIXEL_FORMAT_RGB565;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_R8:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_R16:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616_UNORM:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616_UNORM:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616_SNORM:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616_SNORM:
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_8bpc_RGB:
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_RGB:
        return SPL_PIXEL_FORMAT_ARGB8888;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA1010102:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA1010102:
        return SPL_PIXEL_FORMAT_ARGB2101010;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_RGB_FLOAT:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBE:
        return SPL_PIXEL_FORMAT_FP16;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
        return SPL_PIXEL_FORMAT_420BPP8;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_12bpc_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_12bpc_YCbCr:
        return SPL_PIXEL_FORMAT_420BPP10;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCrYCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCbYCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_CrYCbY:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_CbYCrY:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCbCr:
        return SPL_PIXEL_FORMAT_422BPP8;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCrYCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCbYCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_CrYCbY:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_CbYCrY:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCrYCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCbYCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_CrYCbY:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_CbYCrY:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCbCr:
        return SPL_PIXEL_FORMAT_422BPP10;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrCbYA8888:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_AYCrCb8888:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_YCrCbA8888:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb8888:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA8888:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_AYCbCr8888:
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_8bpc_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ALPHA_THRU_LUMA:
        return SPL_PIXEL_FORMAT_444BPP8;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb2101010:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA1010102:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb12121212:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA12121212:
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_YCbCr:
        return SPL_PIXEL_FORMAT_444BPP10;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_XRGB8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_XBGR8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBX8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRX8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FIX:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FIX:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FLOAT:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FLOAT:
    case VPE_SURFACE_PIXEL_FORMAT_INVALID:
        return SPL_PIXEL_FORMAT_UNKNOWN;
    default:
        VPE_ASSERT(false);
        return SPL_PIXEL_FORMAT_INVALID;
    }
}

static enum spl_transfer_func_type get_spl_tf_type(enum transfer_func_type tf_type)
{
    switch (tf_type) {
    case TF_TYPE_PREDEFINED:
        return SPL_TF_TYPE_PREDEFINED;
    case TF_TYPE_DISTRIBUTED_POINTS:
        return SPL_TF_TYPE_DISTRIBUTED_POINTS;
    case TF_TYPE_BYPASS:
        return SPL_TF_TYPE_BYPASS;
    case TF_TYPE_HWPWL:
        return SPL_TF_TYPE_HWPWL;
    default:
        VPE_ASSERT(false);
        return SPL_TF_TYPE_PREDEFINED;
    }
}

static enum spl_transfer_func_predefined get_spl_tf(enum color_transfer_func tf)
{
    switch (tf) {
    case TRANSFER_FUNC_SRGB:
        return SPL_TRANSFER_FUNCTION_SRGB;
    case TRANSFER_FUNC_BT709:
        return SPL_TRANSFER_FUNCTION_BT709;
    case TRANSFER_FUNC_BT1886:
        return SPL_TRANSFER_FUNCTION_GAMMA24;
    case TRANSFER_FUNC_PQ2084:
        return SPL_TRANSFER_FUNCTION_PQ;
    case TRANSFER_FUNC_LINEAR:
        return SPL_TRANSFER_FUNCTION_LINEAR;
    case TRANSFER_FUNC_NORMALIZED_PQ:
        return SPL_TRANSFER_FUNCTION_UNITY;
    case TRANSFER_FUNC_HLG:
        return SPL_TRANSFER_FUNCTION_HLG;
    default:
        VPE_ASSERT(false);
        return SPL_TRANSFER_FUNCTION_SRGB;
    }
}

static enum spl_color_space get_spl_cs(enum color_space cs)
{
    switch (cs) {
    case COLOR_SPACE_SRGB:
    case COLOR_SPACE_YCBCR_JFIF:
    case COLOR_SPACE_RGB_JFIF:
        return SPL_COLOR_SPACE_SRGB;
    case COLOR_SPACE_SRGB_LIMITED:
        return SPL_COLOR_SPACE_SRGB_LIMITED;
    case COLOR_SPACE_MSREF_SCRGB:
        return SPL_COLOR_SPACE_MSREF_SCRGB;
    case COLOR_SPACE_YCBCR601:
    case COLOR_SPACE_RGB601:
        return SPL_COLOR_SPACE_YCBCR601;
    case COLOR_SPACE_RGB601_LIMITED:
    case COLOR_SPACE_YCBCR601_LIMITED:
        return SPL_COLOR_SPACE_YCBCR601_LIMITED;
    case COLOR_SPACE_YCBCR709:
        return SPL_COLOR_SPACE_YCBCR709;
    case COLOR_SPACE_YCBCR709_LIMITED:
        return SPL_COLOR_SPACE_YCBCR709_LIMITED;
    case COLOR_SPACE_2020_RGB_FULLRANGE:
        return SPL_COLOR_SPACE_2020_RGB_FULLRANGE;
    case COLOR_SPACE_2020_RGB_LIMITEDRANGE:
        return SPL_COLOR_SPACE_2020_RGB_LIMITEDRANGE;
    case COLOR_SPACE_2020_YCBCR:
    case COLOR_SPACE_2020_YCBCR_LIMITED:
        return SPL_COLOR_SPACE_2020_YCBCR;
    default:
        VPE_ASSERT(false);
        return SPL_COLOR_SPACE_UNKNOWN;
    }
}

static struct fixed31_32 spl_int_frac_to_fixpt(
    uint32_t int_part, uint32_t frac_part, uint32_t shift_frac)
{
    struct fixed31_32 fixed_pt = vpe_fixpt_zero;
    fixed_pt                   = vpe_fixpt_from_int(int_part);
    fixed_pt.value += (frac_part >> shift_frac);
    return fixed_pt;
}

static struct fixed31_32 spl_ratio_to_fixpt(uint32_t ratio_u3d19)
{
    uint32_t          int_part;
    uint32_t          frac_part;
    struct fixed31_32 fixed_pt = vpe_fixpt_zero;
    ratio_u3d19                = ratio_u3d19 >> 5;
    frac_part                  = ratio_u3d19 & 0x7FFFF;
    int_part                   = (ratio_u3d19 >> 19) & 0x7;

    fixed_pt = spl_int_frac_to_fixpt(int_part, frac_part, 0);
    return fixed_pt;
}

static struct fixed31_32 spl_init_to_fixpt(
    uint32_t int_part, uint32_t frac_part, uint32_t shift_frac)
{
    return spl_int_frac_to_fixpt(int_part, frac_part, 5);
}

static void set_clip_rect(
    struct spl_rect *spl_clip_rect, struct vpe_rect dst_rect, struct vpe_rect clipped_dst_rect)
{
    spl_clip_rect->x      = clipped_dst_rect.x - dst_rect.x;
    spl_clip_rect->y      = clipped_dst_rect.y - dst_rect.y;
    spl_clip_rect->width  = clipped_dst_rect.width;
    spl_clip_rect->height = clipped_dst_rect.height;
}

void vpe_spl_scl_to_vpe_scl(struct spl_out *spl_out, struct scaler_data *vpe_scl_data)
{

    // taps
    vpe_scl_data->taps.v_taps   = spl_out->dscl_prog_data->taps.v_taps + 1;
    vpe_scl_data->taps.h_taps   = spl_out->dscl_prog_data->taps.h_taps + 1;
    vpe_scl_data->taps.h_taps_c = spl_out->dscl_prog_data->taps.h_taps_c + 1;
    vpe_scl_data->taps.v_taps_c = spl_out->dscl_prog_data->taps.v_taps_c + 1;
    // viewport
    spl_rect_to_vpe_rect(&spl_out->dscl_prog_data->viewport, &vpe_scl_data->viewport);
    spl_rect_to_vpe_rect(&spl_out->dscl_prog_data->viewport_c, &vpe_scl_data->viewport_c);
    // recout
    spl_rect_to_vpe_rect(&spl_out->dscl_prog_data->recout, &vpe_scl_data->recout);
    // ratios
    vpe_scl_data->ratios.horz = spl_ratio_to_fixpt(spl_out->dscl_prog_data->ratios.h_scale_ratio);
    vpe_scl_data->ratios.vert = spl_ratio_to_fixpt(spl_out->dscl_prog_data->ratios.v_scale_ratio);
    vpe_scl_data->ratios.horz_c =
        spl_ratio_to_fixpt(spl_out->dscl_prog_data->ratios.h_scale_ratio_c);
    vpe_scl_data->ratios.vert_c =
        spl_ratio_to_fixpt(spl_out->dscl_prog_data->ratios.v_scale_ratio_c);
    // inits
    vpe_scl_data->inits.h   = spl_init_to_fixpt(spl_out->dscl_prog_data->init.h_filter_init_int,
          spl_out->dscl_prog_data->init.h_filter_init_frac, 5);
    vpe_scl_data->inits.v   = spl_init_to_fixpt(spl_out->dscl_prog_data->init.v_filter_init_int,
          spl_out->dscl_prog_data->init.v_filter_init_frac, 5);
    vpe_scl_data->inits.h_c = spl_init_to_fixpt(spl_out->dscl_prog_data->init.h_filter_init_int_c,
        spl_out->dscl_prog_data->init.h_filter_init_frac_c, 5);
    vpe_scl_data->inits.v_c = spl_init_to_fixpt(spl_out->dscl_prog_data->init.v_filter_init_int_c,
        spl_out->dscl_prog_data->init.v_filter_init_frac_c, 5);
}

struct vp_scan_direction {
    bool orthogonal_rotation;
    bool flip_horz_scan_dir;
    bool flip_vert_scan_dir;
};

static const struct vp_scan_direction
    vp_scan_direction[VPE_ROTATION_ANGLE_COUNT][2][2] =
        {
            {
                // VPE_ROTATION_ANGLE_0
                [false] =
                    {
                        // h_mirror = false
                        [false] = {false, false, false},
                        [true]  = {false, false, true},
                    },
                [true] =
                    {
                        // h_mirror = true
                        [false] = {false, true, false},
                        [true]  = {false, true, true},
                    },
            },
            {
                // VPE_ROTATION_ANGLE_90
                [false] =
                    {
                        [false] = {true, false, true},
                        [true]  = {true, true, true},
                    },
                [true] =
                    {
                        [false] = {true, false, false},
                        [true]  = {true, true, false},
                    },
            },
            {
                // VPE_ROTATION_ANGLE_180
                [false] =
                    {
                        [false] = {false, true, true},
                        [true]  = {false, true, false},
                    },
                [true] =
                    {
                        [false] = {false, false, true},
                        [true]  = {false, false, false},
                    },
            },
            {
                // VPE_ROTATION_ANGLE_270
                [false] =
                    {
                        [false] = {true, true, false},
                        [true]  = {true, false, false},
                    },
                [true] =
                    {
                        [false] = {true, true, true},
                        [true]  = {true, false, true},
                    },
            },
};

void vpe_get_vp_scan_direction(enum vpe_rotation_angle degree, bool h_mirror, bool v_mirror,
    bool *orthogonal_rotation, bool *flip_horz_scan_dir, bool *flip_vert_scan_dir)
{
    struct vp_scan_direction res = vp_scan_direction[degree][h_mirror][v_mirror];

    *orthogonal_rotation = res.orthogonal_rotation;
    *flip_vert_scan_dir  = res.flip_vert_scan_dir;
    *flip_horz_scan_dir  = res.flip_horz_scan_dir;
}

static void determine_opp_recout_adjust(struct spl_in *spl_input, struct stream_ctx *stream_ctx,
    struct output_ctx *output_ctx, const struct vpe_rect *clipped_src_rect,
    const struct vpe_rect *clipped_dst_rect)
{
    struct vpe_caps            *caps = stream_ctx->vpe_priv->pub.caps;
    struct opp                 *opp  = stream_ctx->vpe_priv->resource.opp[0];
    struct fmt_extra_pixel_info extra_info;
    bool dst_subsampled = vpe_is_subsampled_format(output_ctx->surface.format);

    memset(&spl_input->basic_in.opp_recout_adjust, 0, sizeof(struct spl_opp_adjust));

    opp->funcs->get_fmt_extra_pixel(output_ctx->surface.format,
        stream_ctx->vpe_priv->init.debug.subsampling_quality,
        (enum chroma_cositing)output_ctx->surface.cs.cositing, &extra_info);

    if (dst_subsampled) {
        bool orthogonal, flip_horz, flip_vert;

        struct vpe_scaling_info *scaling_info = &stream_ctx->stream.scaling_info;
        struct vpe_rect          surf_src     = *clipped_src_rect;
        struct fixed31_32        h_ratio, temp;
        int32_t                  offset;

        vpe_get_vp_scan_direction(stream_ctx->stream.rotation, stream_ctx->stream.horizontal_mirror,
            stream_ctx->stream.vertical_mirror, &orthogonal, &flip_horz, &flip_vert);

        if (orthogonal) {
            swap(surf_src.width, surf_src.height);
        }
        h_ratio = vpe_fixpt_from_fraction(surf_src.width, clipped_dst_rect->width);

        // see if the LEFT most needs more for output boundary handling, left needs 2 extra
        temp   = vpe_fixpt_mul_int(h_ratio, extra_info.left_pixels);
        offset = (int32_t)vpe_fixpt_floor(temp);

        // default is REPEAT - destination stream is set manually beforehand
        if (stream_ctx->stream_type != VPE_STREAM_TYPE_DESTINATION) {
            stream_ctx->left  = FMT_SUBSAMPLING_BOUNDARY_REPEAT;
            stream_ctx->right = FMT_SUBSAMPLING_BOUNDARY_REPEAT;
        }

        if (offset != 0) {
            if (orthogonal) {
                if (flip_vert) {
                    // i.e. left is at the bottom, check if it is out of bound
                    if ((clipped_src_rect->y + clipped_src_rect->height + offset) <=
                        (scaling_info->src_rect.y + scaling_info->src_rect.height)) {
                        /* can not directly modify the clipped_src_rect and clipped_dst_rect as it
                         * will break the recout alignment partitioning in spl becoz it assumes
                         * clipped_dst_rect is the same after opp */
                        spl_input->basic_in.opp_recout_adjust.width += extra_info.left_pixels;
                        spl_input->basic_in.opp_recout_adjust.x -= extra_info.left_pixels;
                        stream_ctx->left = FMT_SUBSAMPLING_BOUNDARY_EXTRA;
                    }
                } else {
                    // i.e. left is at the top, check if it is out of bound
                    if ((clipped_src_rect->y - scaling_info->src_rect.y) >= offset) {
                        spl_input->basic_in.opp_recout_adjust.width += extra_info.left_pixels;
                        spl_input->basic_in.opp_recout_adjust.x -= extra_info.left_pixels;
                        stream_ctx->left = FMT_SUBSAMPLING_BOUNDARY_EXTRA;
                    }
                }
            } else {
                if (!flip_horz) {
                    if ((clipped_src_rect->x - scaling_info->src_rect.x) >= offset) {
                        spl_input->basic_in.opp_recout_adjust.width += extra_info.left_pixels;
                        spl_input->basic_in.opp_recout_adjust.x -= extra_info.left_pixels;
                        stream_ctx->left = FMT_SUBSAMPLING_BOUNDARY_EXTRA;
                    }
                } else {
                    // right is left instead
                    if ((clipped_src_rect->x + clipped_src_rect->width + offset) <=
                        (scaling_info->src_rect.x + scaling_info->src_rect.width)) {
                        spl_input->basic_in.opp_recout_adjust.width += extra_info.left_pixels;
                        spl_input->basic_in.opp_recout_adjust.x -= extra_info.left_pixels;
                        stream_ctx->left = FMT_SUBSAMPLING_BOUNDARY_EXTRA;
                    }
                }
            }
        }

        // see if the RIGHT most needs more for output boundary handling, right needs 1 extra
        temp   = vpe_fixpt_mul_int(h_ratio, extra_info.right_pixels);
        offset = vpe_fixpt_floor(temp);

        if (offset != 0) {
            if (orthogonal) {
                if (flip_vert) {
                    // right is at the top
                    if ((clipped_src_rect->y - scaling_info->src_rect.y) >= offset) {
                        spl_input->basic_in.opp_recout_adjust.width += extra_info.right_pixels;
                        stream_ctx->right = FMT_SUBSAMPLING_BOUNDARY_EXTRA;
                    }
                } else {
                    // right is at the bottom
                    if ((clipped_src_rect->y + clipped_src_rect->height + offset) <=
                        (scaling_info->src_rect.y + scaling_info->src_rect.height)) {
                        spl_input->basic_in.opp_recout_adjust.width += extra_info.right_pixels;
                        stream_ctx->left = FMT_SUBSAMPLING_BOUNDARY_EXTRA;
                    }
                }
            } else {
                if (!flip_horz) {
                    if ((clipped_src_rect->x + clipped_src_rect->width + offset) <=
                        (scaling_info->src_rect.x + scaling_info->src_rect.width)) {
                        spl_input->basic_in.opp_recout_adjust.width += extra_info.right_pixels;
                        stream_ctx->right = FMT_SUBSAMPLING_BOUNDARY_EXTRA;
                    }
                } else {
                    // left is right instead
                    if ((clipped_src_rect->x - scaling_info->src_rect.x) >= offset) {
                        spl_input->basic_in.opp_recout_adjust.width += extra_info.right_pixels;
                        stream_ctx->right = FMT_SUBSAMPLING_BOUNDARY_EXTRA;
                    }
                }
            }
        }
    } else {
        stream_ctx->right = FMT_SUBSAMPLING_BOUNDARY_REPEAT;
        stream_ctx->left  = FMT_SUBSAMPLING_BOUNDARY_REPEAT;
    }
}

void vpe_init_spl_in(
    struct spl_in *spl_input, struct stream_ctx *stream_ctx, struct output_ctx *output_ctx)
{
    enum color_space         in_cs, out_cs;
    enum color_transfer_func in_tf, out_tf;

    struct vpe_rect clipped_src_rect;
    struct vpe_rect clipped_dst_rect;

    struct vpe_scaling_info *scaling_info = &stream_ctx->stream.scaling_info;

    vpe_color_get_color_space_and_tf(&stream_ctx->stream.surface_info.cs, &in_cs, &in_tf);
    vpe_color_get_color_space_and_tf(
        &stream_ctx->vpe_priv->output_ctx.surface.cs, &out_cs, &out_tf);

    /** Since the active values just set the mpc_size in SPL and vpelib calculates the mpc_size
     * for each segment after the SPL call for segmentation, the active values does not affect the
     * segmentation. Therefore, zero is set for the initialization to avoid NAN assignement.
     *
     * in vpe, we do not have mpc combine output from multiple input streams
     * as such, we could not specify all output-related rect to use final coordinates,
     * all output related rects are relative to the {0, 0 , dst_width, dst_height} only
     */
    spl_input->h_active = 0;
    spl_input->v_active = 0;

    // BASIC_OUT
    clipped_src_rect = scaling_info->src_rect;
    clipped_dst_rect = scaling_info->dst_rect;

    vpe_clip_stream(&clipped_src_rect, &clipped_dst_rect, &output_ctx->target_rect);

    determine_opp_recout_adjust(
        spl_input, stream_ctx, output_ctx, &clipped_src_rect, &clipped_dst_rect);

    // struct spl_size output_size; // Output Size
    spl_input->basic_out.output_size.width  = scaling_info->dst_rect.width;
    spl_input->basic_out.output_size.height = scaling_info->dst_rect.height;

    // do not have 2-stages scaling concept in usage, set basic_out src_rect = dst_rect
    spl_input->basic_out.src_rect.x      = 0;
    spl_input->basic_out.src_rect.y      = 0;
    spl_input->basic_out.src_rect.width  = scaling_info->dst_rect.width;
    spl_input->basic_out.src_rect.height = scaling_info->dst_rect.height;
    spl_input->basic_out.dst_rect        = spl_input->basic_out.src_rect;

    // int odm_combine_factor;	// deprecated
    spl_input->basic_out.odm_combine_factor = 1;
    spl_input->basic_out.alpha_en           = stream_ctx->per_pixel_alpha;

    // BASIC_IN
    // enum spl_pixel_format format; // Pixel Format
    spl_input->basic_in.format = get_spl_format(stream_ctx->stream.surface_info.format);
    // enum chroma_cositing cositing; /* Chroma Subsampling Offset */
    spl_input->basic_in.cositing = get_spl_cositing(stream_ctx->stream.surface_info.cs.cositing);
    if (stream_ctx->stream.lut_compound.enabled) {
        // In 3DLUT compound case, cs is "custom" with no cositing info
        // It is provided separately via lut_compound.chroma_cositing
        spl_input->basic_in.cositing =
            get_spl_cositing(stream_ctx->stream.lut_compound.upsampled_chroma_input);
    } else if (vpe_is_yuv422(stream_ctx->stream.surface_info.format)) {
        // Force TOPLEFT cositing for 422 as other cositing modes have vertical adjustment
        spl_input->basic_in.cositing = CHROMA_COSITING_TOPLEFT;
    }
    // struct spl_rect src_rect; // Source rect
    vpe_rect_to_spl_rect(&scaling_info->src_rect, &spl_input->basic_in.src_rect);

    // struct spl_rect dst_rect; // Destination Rect
    // spl shall only know the relative location of the output.
    // the final absolute destination rect is adjusted in calculate_dst_viewport_and_active
    spl_input->basic_in.dst_rect.x      = 0;
    spl_input->basic_in.dst_rect.y      = 0;
    spl_input->basic_in.dst_rect.width  = scaling_info->dst_rect.width;
    spl_input->basic_in.dst_rect.height = scaling_info->dst_rect.height;

    // enum spl_rotation_angle rotation;  // Rotation
    // bool horizontal_mirror;  // Horizontal mirror
    get_spl_rotation(vpe_get_scan_direction(stream_ctx->stream.rotation,
                         stream_ctx->stream.horizontal_mirror, stream_ctx->stream.vertical_mirror),
        &spl_input->basic_in.rotation, &spl_input->basic_in.horizontal_mirror);

    // int mpc_num_h_slices; // MPC Horizontal Combine Factor (number of segments/horizintal slices)
    spl_input->basic_in.num_h_slices_recout_width_align.use_recout_width_aligned = false;
    spl_input->basic_in.num_h_slices_recout_width_align.num_slices_recout_width.mpc_num_h_slices =
        stream_ctx->num_segments;
    // enum spl_transfer_func_type tf_type; /* Transfer function type */
    spl_input->basic_in.tf_type = SPL_TF_TYPE_DISTRIBUTED_POINTS;
    // enum spl_transfer_func_predefined tf_predefined_type; /* Transfer function predefined type */
    spl_input->basic_in.tf_predefined_type = get_spl_tf(in_tf);
    // enum spl_color_space color_space;	//	Color Space
    spl_input->basic_in.color_space = get_spl_cs(in_cs);
    // unsigned int max_luminance;	//	Max Luminance
    spl_input->basic_in.max_luminance = 80;
    // struct spl_rect clip_rect; // Clip rect
    set_clip_rect(&spl_input->basic_in.clip_rect, scaling_info->dst_rect, clipped_dst_rect);
    // int odm_slice_index;	// ODM Slice Index using get_odm_split_index
    spl_input->odm_slice_index = 0;
    // struct spl_taps scaling_quality; // Explicit Scaling Quality
    spl_input->scaling_quality.v_taps          = scaling_info->taps.v_taps;
    spl_input->scaling_quality.h_taps          = scaling_info->taps.h_taps;
    spl_input->scaling_quality.v_taps_c        = scaling_info->taps.v_taps_c;
    spl_input->scaling_quality.h_taps_c        = scaling_info->taps.h_taps_c;
    spl_input->scaling_quality.integer_scaling = false;
    spl_input->is_hdr_on                       = vpe_is_HDR(out_tf);
    spl_input->adaptive_sharpness.enable       = scaling_info->adaptive_sharpeness.enable;
    spl_input->adaptive_sharpness.sharpness_level =
        scaling_info->adaptive_sharpeness.sharpness_level;
    spl_input->adaptive_sharpness.sharpness_range = SHARPNESS_RANGE; // to be passed by the caller
    spl_input->sharpen_policy                     = SHARPEN_ALWAYS;
    spl_input->disable_easf                       = !scaling_info->enable_easf;
    spl_input->prefer_easf                        = scaling_info->prefer_easf;

    if (vpe_is_subsampled_format(stream_ctx->stream.surface_info.format)) {
        spl_input->min_viewport_size = 2;
    } else {
        spl_input->min_viewport_size = 1;
    }
}

void vpe_scl_to_dscl_bg(struct scaler_data *scl_data)
{
    // struct spl_rect recout;
    vpe_rect_to_spl_rect(&scl_data->recout, &scl_data->dscl_prog_data.recout);
    // struct mpc_size mpc_size;
    scl_data->dscl_prog_data.mpc_size.width  = scl_data->h_active;
    scl_data->dscl_prog_data.mpc_size.height = scl_data->v_active;

    // struct ratio ratios;
    scl_data->dscl_prog_data.ratios.h_scale_ratio   = vpe_fixpt_u3d19(scl_data->ratios.horz) << 5;
    scl_data->dscl_prog_data.ratios.v_scale_ratio   = vpe_fixpt_u3d19(scl_data->ratios.vert) << 5;
    scl_data->dscl_prog_data.ratios.h_scale_ratio_c = vpe_fixpt_u3d19(scl_data->ratios.horz_c) << 5;
    scl_data->dscl_prog_data.ratios.v_scale_ratio_c = vpe_fixpt_u3d19(scl_data->ratios.vert_c) << 5;

    // struct init init;
    scl_data->dscl_prog_data.init.h_filter_init_frac   = vpe_fixpt_u0d19(scl_data->inits.h) << 5;
    scl_data->dscl_prog_data.init.h_filter_init_int    = vpe_fixpt_floor(scl_data->inits.h);
    scl_data->dscl_prog_data.init.h_filter_init_frac_c = vpe_fixpt_u0d19(scl_data->inits.h_c) << 5;
    scl_data->dscl_prog_data.init.h_filter_init_int_c  = vpe_fixpt_floor(scl_data->inits.h_c);
    scl_data->dscl_prog_data.init.v_filter_init_frac   = vpe_fixpt_u0d19(scl_data->inits.v) << 5;
    scl_data->dscl_prog_data.init.v_filter_init_int    = vpe_fixpt_floor(scl_data->inits.v);
    scl_data->dscl_prog_data.init.v_filter_init_frac_c = vpe_fixpt_u0d19(scl_data->inits.v_c) << 5;
    scl_data->dscl_prog_data.init.v_filter_init_int_c  = vpe_fixpt_floor(scl_data->inits.v_c);

    // struct spl_taps taps;	// TAPS - set based on scl_data.taps
    scl_data->dscl_prog_data.taps.h_taps   = scl_data->taps.h_taps - 1;
    scl_data->dscl_prog_data.taps.v_taps   = scl_data->taps.v_taps - 1;
    scl_data->dscl_prog_data.taps.h_taps_c = scl_data->taps.h_taps_c - 1;
    scl_data->dscl_prog_data.taps.v_taps_c = scl_data->taps.v_taps_c - 1;

    // struct spl_rect viewport;
    vpe_rect_to_spl_rect(&scl_data->viewport, &scl_data->dscl_prog_data.viewport);
    // struct spl_rect viewport_c;
    vpe_rect_to_spl_rect(&scl_data->viewport_c, &scl_data->dscl_prog_data.viewport_c);
}
