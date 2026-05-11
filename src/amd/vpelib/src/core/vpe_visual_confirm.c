/* Copyright 2023 Advanced Micro Devices, Inc.
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

#include "vpe_visual_confirm.h"
#include "common.h"
#include "vpe_priv.h"
#include "color_bg.h"
#include "background.h"
#include "resource.h"

static bool should_generate_visual_confirm(enum vpe_stream_type stream_type)
{
    switch (stream_type) {
    case VPE_STREAM_TYPE_INPUT:
    case VPE_STREAM_TYPE_BG_GEN:
        return true;
    default:
        return false;
        break;
    }
}

static uint16_t get_visual_confirm_segs_count(
    uint32_t max_seg_width, uint32_t target_rect_width, uint32_t target_width_alignment)
{
    // Unlike max_gaps logic in vpe10_calculate_segments, we are pure BG seg, no need to worry
    // stream splitted among one of the segment. so no need to "+1", just round up the calculated
    // number of segments.

    uint16_t seg_cnt = (uint16_t)(max(int_divide_with_ceil(target_rect_width, max_seg_width), 1));
    uint16_t segment_width         = (uint16_t)int_divide_with_ceil(target_rect_width, seg_cnt);
    uint32_t aligned_segment_width = vpe_align_seg(segment_width, target_width_alignment);
    if (aligned_segment_width > max_seg_width) {
        seg_cnt++;
    }

    return seg_cnt;
}

static uint16_t vpe_get_visual_confirm_total_seg_count(
    struct vpe_priv *vpe_priv, uint32_t max_seg_width, const struct vpe_build_param *params)
{
    uint16_t             segs_num                  = 0;
    uint16_t             total_visual_confirm_segs = 0;
    uint16_t             stream_idx;
    struct stream_ctx   *stream_ctx;
    uint32_t             alignment = vpe_get_recout_width_alignment(params);
    struct vpe_cmd_info *cmd_info;
    uint16_t             cmd_idx;

    if (vpe_priv->init.debug.visual_confirm_params.input_format) {
        for (stream_idx = 0; stream_idx < (uint16_t)vpe_priv->num_streams; stream_idx++) {
            stream_ctx = &vpe_priv->stream_ctx[stream_idx];
            if (should_generate_visual_confirm(stream_ctx->stream_type))
                total_visual_confirm_segs += get_visual_confirm_segs_count(
                    max_seg_width, stream_ctx->stream.scaling_info.dst_rect.width, alignment);
        }
    }

    if (vpe_priv->init.debug.visual_confirm_params.output_format) {
        total_visual_confirm_segs +=
            get_visual_confirm_segs_count(max_seg_width, params->target_rect.width, alignment);
    }

    if (vpe_priv->init.debug.visual_confirm_params.pipe_idx) {
        cmd_info = vpe_priv->vpe_cmd_vector->element;
        for (cmd_idx = 0; cmd_idx < (uint16_t)vpe_priv->vpe_cmd_vector->num_elements; cmd_idx++) {
            total_visual_confirm_segs += cmd_info->num_inputs;
            cmd_info++;
        }
    }

    return total_visual_confirm_segs;
}

static void generate_pipe_segments(struct vpe_priv *vpe_priv, const struct vpe_build_param *params,
    struct vpe_rect *current_gap, uint32_t max_seg_width)
{
    uint16_t             cmd_idx, input_idx, seg_cnt;
    struct vpe_cmd_info *cmd_info;
    struct vpe_rect      visual_confirm_rect;
    uint32_t             recout_alignment = vpe_get_recout_width_alignment(params);

    if (vpe_priv->init.debug.visual_confirm_params.pipe_idx &&
        params->target_rect.height > 3 * VISUAL_CONFIRM_HEIGHT) {
        cmd_info = vpe_priv->vpe_cmd_vector->element;
        for (cmd_idx = 0; cmd_idx < (uint16_t)vpe_priv->vpe_cmd_vector->num_elements; cmd_idx++) {
            if (cmd_info->ops == VPE_CMD_OPS_BG_VSCF_INPUT ||
                cmd_info->ops == VPE_CMD_OPS_BG_VSCF_OUTPUT ||
                cmd_info->ops == VPE_CMD_OPS_BG_VSCF_PIPE0 ||
                cmd_info->ops == VPE_CMD_OPS_BG_VSCF_PIPE1) {
                cmd_info++;
                continue;
            }
            for (input_idx = 0; input_idx < cmd_info->num_inputs; input_idx++) {
                visual_confirm_rect        = cmd_info->inputs[input_idx].scaler_data.dst_viewport;
                visual_confirm_rect.height = VISUAL_CONFIRM_HEIGHT;
                visual_confirm_rect.y += 2 * VISUAL_CONFIRM_HEIGHT;
                seg_cnt = get_visual_confirm_segs_count(
                    max_seg_width, visual_confirm_rect.width, recout_alignment);
                vpe_full_bg_gaps(current_gap, &visual_confirm_rect, recout_alignment, seg_cnt);
                if (input_idx == 0) {
                    vpe_priv->resource.create_bg_segments(
                        vpe_priv, current_gap, seg_cnt, VPE_CMD_OPS_BG_VSCF_PIPE0);
                } else if (input_idx == 1) {
                    vpe_priv->resource.create_bg_segments(
                        vpe_priv, current_gap, seg_cnt, VPE_CMD_OPS_BG_VSCF_PIPE1);
                } else {
                    VPE_ASSERT(0);
                }
                current_gap += seg_cnt;
            }
            cmd_info++;
        }
    }
}

struct vpe_color vpe_get_visual_confirm_color(struct vpe_priv *vpe_priv,
    enum vpe_surface_pixel_format format, struct vpe_color_space cs, enum color_space output_cs,
    struct transfer_func *output_tf, enum vpe_surface_pixel_format output_format, bool enable_3dlut)
{

    struct vpe_color visual_confirm_color;
    visual_confirm_color.is_ycbcr = false;
    visual_confirm_color.rgba.a   = 0.0;
    visual_confirm_color.rgba.r   = 0.0;
    visual_confirm_color.rgba.g   = 0.0;
    visual_confirm_color.rgba.b   = 0.0;

    switch (format) {
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ALPHA_THRU_LUMA:
        // YUV420 8bit: Green
        visual_confirm_color.rgba.r = 0.0;
        visual_confirm_color.rgba.g = 1.0;
        visual_confirm_color.rgba.b = 0.0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
        // YUV420 10bit: Yellow (SDR)
        switch (cs.tf) {
        case VPE_TF_G22:
        case VPE_TF_G24:
        case VPE_TF_SRGB:
        case VPE_TF_BT709:
            visual_confirm_color.rgba.r = 1.0;
            visual_confirm_color.rgba.g = 1.0;
            visual_confirm_color.rgba.b = 0.0;
            break;
            // YUV420 10bit 3dlut enable: White (HDR)
        case VPE_TF_PQ:
        case VPE_TF_HLG:
            if (enable_3dlut) {
                visual_confirm_color.rgba.r = 1.0;
                visual_confirm_color.rgba.g = 1.0;
                visual_confirm_color.rgba.b = 1.0;
            } else {
            // YUV420 10bit 3dlut disable: Red (HDR)
                visual_confirm_color.rgba.r = 1.0;
                visual_confirm_color.rgba.g = 0.0;
                visual_confirm_color.rgba.b = 0.0;
            }
            break;
        default:
            break;
        }
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_XRGB8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_XBGR8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBX8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRX8888:
        // RGBA and RGBX 8 bit and variants : Pink
        visual_confirm_color.rgba.r = 1.0;
        visual_confirm_color.rgba.g = 0.5;
        visual_confirm_color.rgba.b = 1.0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA1010102:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA1010102:
        // RGBA 10 bit and variants : Cyan
        visual_confirm_color.rgba.r = 0.0;
        visual_confirm_color.rgba.g = 1.0;
        visual_confirm_color.rgba.b = 1.0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616F:
        // FP16 and variants: Orange
        visual_confirm_color.rgba.r = 1.0;
        visual_confirm_color.rgba.g = 0.65f;
        visual_confirm_color.rgba.b = 0.0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_12bpc_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_12bpc_YCbCr:
        // P016 : Dark Green
        visual_confirm_color.rgba.r = 0.0;
        visual_confirm_color.rgba.g = 0.35f;
        visual_confirm_color.rgba.b = 0.0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA16161616:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616_UNORM:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616_UNORM:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616_SNORM:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616_SNORM:
        // RGB16 and variants: Blue
        visual_confirm_color.rgba.r = 0.0;
        visual_confirm_color.rgba.g = 0.0;
        visual_confirm_color.rgba.b = 1.0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_R8:
        // Monochrome 8 bit: Silver
        visual_confirm_color.rgba.r = 0.753f;
        visual_confirm_color.rgba.g = 0.753f;
        visual_confirm_color.rgba.b = 0.753f;
        break;

    case VPE_SURFACE_PIXEL_FORMAT_GRPH_R16:
        // Monochrome 16 bit: Dim Gray
        visual_confirm_color.rgba.r = 0.412f;
        visual_confirm_color.rgba.g = 0.412f;
        visual_confirm_color.rgba.b = 0.412f;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCbCr:
        // YUY2: Misty Rose
        visual_confirm_color.rgba.r = 0.412f;
        visual_confirm_color.rgba.g = 0.894f;
        visual_confirm_color.rgba.b = 0.882f;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCrYCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCbYCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_CrYCbY:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_CbYCrY:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCbCr:
        // Y210: Salmon
        visual_confirm_color.rgba.r = 0.412f;
        visual_confirm_color.rgba.g = 0.627f;
        visual_confirm_color.rgba.b = 0.478f;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCrYCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCbYCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_CrYCbY:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_CbYCrY:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCbCr:
        // Y216: Maroon
        visual_confirm_color.rgba.r = 0.5;
        visual_confirm_color.rgba.g = 0.0;
        visual_confirm_color.rgba.b = 0.0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrCbYA8888:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_AYCrCb8888:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_YCrCbA8888:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb8888:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA8888:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_AYCbCr8888:
        // AYUV: Aqua Marine
        visual_confirm_color.rgba.r = 0.5;
        visual_confirm_color.rgba.g = 1.0;
        visual_confirm_color.rgba.b = 0.8f;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb2101010:
        // Y410: Dark Cyan
        visual_confirm_color.rgba.r = 0.0;
        visual_confirm_color.rgba.g = 0.5;
        visual_confirm_color.rgba.b = 0.5;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb12121212:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA12121212:
        // Y416: Navy
        visual_confirm_color.rgba.r = 0.0;
        visual_confirm_color.rgba.g = 0.0;
        visual_confirm_color.rgba.b = 0.5;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_8bpc_RGB:
        // Planar RGB8: Lavender
        visual_confirm_color.rgba.r = 0.9f;
        visual_confirm_color.rgba.g = 0.9f;
        visual_confirm_color.rgba.b = 0.98f;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_8bpc_YCbCr:
        // Planar YCbCr8: Chocolate
        visual_confirm_color.rgba.r = 0.824f;
        visual_confirm_color.rgba.g = 0.412f;
        visual_confirm_color.rgba.b = 0.118f;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_RGB:
        // Planar RGB16: Rosy Brown
        visual_confirm_color.rgba.r = 0.737f;
        visual_confirm_color.rgba.g = 0.56f;
        visual_confirm_color.rgba.b = 0.56f;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_YCbCr:
        // Planar YCbCr16: Saddle Brown
        visual_confirm_color.rgba.r = 0.545f;
        visual_confirm_color.rgba.g = 0.271f;
        visual_confirm_color.rgba.b = 0.075f;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBE:
        // RGBE: Olive
        visual_confirm_color.rgba.r = 0.5;
        visual_confirm_color.rgba.g = 0.5;
        visual_confirm_color.rgba.b = 0.0;
        break;
    default:
        break;
    }

    // Due to there will be regamma (ogam), need convert the bg color for visual confirm
    vpe_priv->resource.bg_color_convert(
        output_cs, output_tf, output_format, &visual_confirm_color, NULL, enable_3dlut);

    // Experimental: To make FP16 Linear color looks more visually ok
    if (vpe_is_fp16(output_format)) {
        visual_confirm_color.rgba.r /= 125;
        visual_confirm_color.rgba.g /= 125;
        visual_confirm_color.rgba.b /= 125;
    }

    return visual_confirm_color;
}

enum vpe_status vpe_create_visual_confirm_segs(
    struct vpe_priv *vpe_priv, const struct vpe_build_param *params, uint32_t max_seg_width)
{
    uint16_t           stream_idx;
    struct stream_ctx *stream_ctx;
    struct vpe_rect    visual_confirm_rect;
    struct vpe_rect   *visual_confirm_gaps;
    struct vpe_rect   *current_gap;

    uint16_t total_seg_cnt =
        vpe_get_visual_confirm_total_seg_count(vpe_priv, max_seg_width, params);
    uint16_t seg_cnt = 0;
    uint32_t recout_alignment = vpe_get_recout_width_alignment(params);

    if (!total_seg_cnt)
        return VPE_STATUS_OK;

    visual_confirm_gaps = vpe_zalloc(sizeof(struct vpe_rect) * total_seg_cnt);
    if (!visual_confirm_gaps)
        return VPE_STATUS_NO_MEMORY;

    current_gap = visual_confirm_gaps;

    // Do visual confirm bg generation for input format
    if (vpe_priv->init.debug.visual_confirm_params.input_format &&
        params->target_rect.height > 2 * VISUAL_CONFIRM_HEIGHT) {
        for (stream_idx = 0; stream_idx < (uint16_t)params->num_streams; stream_idx++) {
            stream_ctx          = &vpe_priv->stream_ctx[stream_idx];
            visual_confirm_rect = stream_ctx->stream.scaling_info.dst_rect;
            visual_confirm_rect.y += 0;
            visual_confirm_rect.height = VISUAL_CONFIRM_HEIGHT;
            seg_cnt                    = get_visual_confirm_segs_count(
                max_seg_width, stream_ctx->stream.scaling_info.dst_rect.width, recout_alignment);
            vpe_full_bg_gaps(current_gap, &visual_confirm_rect, recout_alignment, seg_cnt);
            vpe_priv->resource.create_bg_segments(
                vpe_priv, current_gap, seg_cnt, VPE_CMD_OPS_BG_VSCF_INPUT);
            current_gap += seg_cnt;
        }
    }
    // Do visual confirm bg generation for output format
    if (vpe_priv->init.debug.visual_confirm_params.output_format &&
        params->target_rect.height > VISUAL_CONFIRM_HEIGHT) {
        visual_confirm_rect = params->target_rect;
        visual_confirm_rect.y += VISUAL_CONFIRM_HEIGHT;
        visual_confirm_rect.height = VISUAL_CONFIRM_HEIGHT;
        seg_cnt                    = get_visual_confirm_segs_count(
            max_seg_width, params->target_rect.width, recout_alignment);
        vpe_full_bg_gaps(current_gap, &visual_confirm_rect, recout_alignment, seg_cnt);
        vpe_priv->resource.create_bg_segments(
            vpe_priv, current_gap, seg_cnt, VPE_CMD_OPS_BG_VSCF_OUTPUT);
        current_gap += seg_cnt;
    }

    generate_pipe_segments(vpe_priv, params, current_gap, max_seg_width);

    if (visual_confirm_gaps != NULL) {
        vpe_free(visual_confirm_gaps);
        visual_confirm_gaps = NULL;
        current_gap         = NULL;
    }

    return VPE_STATUS_OK;
}
