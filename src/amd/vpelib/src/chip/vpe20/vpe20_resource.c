/* Copyright 2024-2025 Advanced Micro Devices, Inc.
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
#include <string.h>
#include "vpe_priv.h"
#include "common.h"
#include "vpe20_cmd_builder.h"
#include "vpe20_vpe_desc_writer.h"
#include "vpe20_plane_desc_writer.h"
#include "vpe20_config_writer.h"
#include "vpe20_resource.h"
#include "vpe10_resource.h"
#include "vpe10_vpec.h"
#include "vpe10_cdc_fe.h"
#include "vpe10_cdc_be.h"
#include "vpe20_cdc_fe.h"
#include "vpe20_cdc_be.h"
#include "vpe10_dpp.h"
#include "vpe20_dpp.h"
#include "vpe10_mpc.h"
#include "vpe20_mpc.h"
#include "vpe10_opp.h"
#include "vpe20_opp.h"
#include "vpe10_background.h"
#include "vpe20/inc/asic/chip_offset.h"
#include "vpe20/inc/asic/chip_mask.h"
#include "vpe20/inc/asic/chip_shift.h"
#include "vpe20/inc/asic/chip_default.h"
#include "custom_fp16.h"
#include "custom_float.h"
#include "background.h"
#include "vpe_visual_confirm.h"
#include "vpe_spl_translation.h"
#include "SPL/dc_spl.h"
#include "multi_pipe_segmentation.h"

#define LUT_NUM_ENTRIES   (17 * 17 * 17)
#define LUT_ENTRY_SIZE    (2)
#define LUT_NUM_COMPONENT (3)
#define LUT_BUFFER_SIZE   (LUT_NUM_ENTRIES * LUT_ENTRY_SIZE * LUT_NUM_COMPONENT)

#define LUT_FL_SIZE_17X17X17 (4916)
#define LUT_FL_SIZE_33X33X33 (35940)

#define BYTES_PER_ENTRY                    (4)
#define SHAPER_LUT_CHANNELS                (3)
#define SHAPER_LUT_DATA_POINTS_PER_CHANNEL (256)
#define SHAPER_LUT_CONFIG_ENTRIES          (28)
#define SHAPER_LUT_DMA_DATA_SIZE                                                                   \
    (SHAPER_LUT_DATA_POINTS_PER_CHANNEL * SHAPER_LUT_CHANNELS * BYTES_PER_ENTRY)
#define SHAPER_LUT_DMA_CONFIG_SIZE                                                                 \
    (SHAPER_LUT_CONFIG_ENTRIES * (BYTES_PER_ENTRY + SHAPER_LUT_DMA_CONFIG_PADDING))
#define SHAPER_LUT_DMA_DATA_ALIGNMENT   (64)
#define SHAPER_LUT_DMA_CONFIG_ALIGNMENT (64)
#define SHAPER_LUT_DMA_CONFIG_PADDING   (60)
#define LUT_3D_DMA_ALIGNMENT            (256)

// 0.92f is the max studio range value in floating point
#define BG_COLOR_STUDIO_PQ_MAX_THRESHOLD 0.92f

// set field/register/bitfield name
#define SFRB(field_name, reg_name, post_fix) .field_name = reg_name##__##field_name##post_fix

// #ifdef SOC_BRINGUP

#define SRIDFVL(reg_name, block, id)                                                               \
    .reg_name = {mm##block##id##_##reg_name, mm##block##id##_##reg_name##_##DEFAULT,               \
        mm##block##id##_##reg_name##_##DEFAULT, false}

#define SRIDFVL1(reg_name)                                                                         \
    .reg_name = {mm##reg_name, mm##reg_name##_##DEFAULT, mm##reg_name##_##DEFAULT, false}

#define SRIDFVL2(reg_name, block, id)                                                              \
    .block##_##reg_name = {mm##block##id##_##reg_name, mm##block##id##_##reg_name##_##DEFAULT,     \
        mm##block##id##_##reg_name##_##DEFAULT, false}

#define SRIDFVL3(reg_name, block, id)                                                              \
    .block##_##reg_name = {mm##block##_##reg_name, mm##block##_##reg_name##_##DEFAULT,             \
        mm##block##_##reg_name##_##DEFAULT, false}

#define SRIDFVL_CDC(reg_name, block, id)                                                           \
    .block##0##_##reg_name = {mm##block##id##_##reg_name, mm##block##id##_##reg_name##_##DEFAULT,  \
        mm##block##id##_##reg_name##_##DEFAULT, false}

/***************** CDC FE registers ****************/
#define cdc_fe_regs(id) [id] = {CDC_FE_REG_LIST_VPE20(id)}

static struct vpe20_cdc_fe_registers cdc_fe_regs[] = {cdc_fe_regs(0), cdc_fe_regs(1)};

static const struct vpe20_cdc_fe_shift cdc_fe_shift = {CDC_FE_FIELD_LIST_VPE20(__SHIFT)};

static const struct vpe20_cdc_fe_mask cdc_fe_mask = {CDC_FE_FIELD_LIST_VPE20(_MASK)};

/***************** CDC BE registers ****************/
#define cdc_be_regs(id) [id] = {CDC_BE_REG_LIST_VPE20(id)}
static struct vpe20_cdc_be_registers cdc_be_regs[] = {
    cdc_be_regs(0), cdc_be_regs(1), cdc_be_regs(2), cdc_be_regs(3)};

static const struct vpe20_cdc_be_shift cdc_be_shift = {CDC_BE_FIELD_LIST_VPE20(__SHIFT)};

static const struct vpe20_cdc_be_mask cdc_be_mask = {CDC_BE_FIELD_LIST_VPE20(_MASK)};

/***************** DPP registers ****************/
#define dpp_regs(id) [id] = {DPP_REG_LIST_VPE20(id)}

static struct vpe20_dpp_registers dpp_regs[] = {dpp_regs(0), dpp_regs(1)};

static const struct vpe20_dpp_shift dpp_shift = {DPP_FIELD_LIST_VPE20(__SHIFT)};

static const struct vpe20_dpp_mask dpp_mask = {DPP_FIELD_LIST_VPE20(_MASK)};

/***************** OPP registers ****************/
#define opp_regs(id) [id] = {OPP_REG_LIST_VPE20(id)}

static struct vpe20_opp_registers opp_regs[] = {opp_regs(0), opp_regs(1)};

static const struct vpe20_opp_shift opp_shift = {OPP_FIELD_LIST_VPE20(__SHIFT)};

static const struct vpe20_opp_mask opp_mask = {OPP_FIELD_LIST_VPE20(_MASK)};

/***************** MPC registers ****************/
#define mpc_regs(id) [id] = {MPC_REG_LIST_VPE20(id)}

static struct vpe20_mpc_registers mpc_regs[] = {mpc_regs(0), mpc_regs(1)};

static const struct vpe20_mpc_shift mpc_shift = {MPC_FIELD_LIST_VPE20(__SHIFT)};

static const struct vpe20_mpc_mask mpc_mask = {MPC_FIELD_LIST_VPE20(_MASK)};

static struct vpe_caps
    caps =
        {
            .max_input_size =
                {
                    .width  = 16384,
                    .height = 16384,
                },
            .max_output_size =
                {
                    .width  = 16384,
                    .height = 16384,
                },
            .min_input_size =
                {
                    .width  = 1,
                    .height = 1,
                },
            .min_output_size =
                {
                    .width  = 1,
                    .height = 1,
                },
            .lut_size               = LUT_BUFFER_SIZE,
            .rotation_support       = 1,
            .h_mirror_support       = 1,
            .v_mirror_support       = 1,
            .is_apu                 = 1,
            .bg_color_check_support = 0,

            .prefer_external_scaler_coef = 0,

            .resource_caps =
                {
                    .num_dpp       = 2,
                    .num_opp       = 2,
                    .num_mpc_3dlut = 1,
                    .num_queue     = 8,
                    .num_cdc_be    = 4,
                },
            .color_caps = {.dpp =
                               {
                                   .pre_csc    = 1,
                                   .luma_key   = 1,
                                   .color_key  = 1,
                                   .dgam_ram   = 0,
                                   .post_csc   = 1,
                                   .gamma_corr = 1,
                                   .hw_3dlut   = 1,
                                   .ogam_ram   = 1, /**< programmable gam in output -> gamma_corr */
                                   .ocsc       = 0,
                                   .dgam_rom_caps =
                                       {
                                           .srgb     = 1,
                                           .bt2020   = 1,
                                           .gamma2_2 = 1,
                                           .pq       = 1,
                                           .hlg      = 1,
                                       },
                               },
                .mpc =
                    {
                        .gamut_remap         = 1,
                        .ogam_ram            = 1,
                        .ocsc                = 1,
                        .shared_3d_lut       = 1,
                        .global_alpha        = 1,
                        .top_bottom_blending = 1,
                        .dma_3d_lut          = 1,
                        .yuv_linear_blend    = 0,
                        .lut_dim_caps =
                            {
                                .dim_9  = 0,
                                .dim_17 = 1,
                                .dim_33 = 1,
                            },
                        .fast_load_caps =
                            {
                                .lut_3d_17 = 0,
                                .lut_3d_33 = 1,
                            },
                        .lut_caps =
                            {
                                .lut_shaper_caps =
                                    {
                                        .dma_data             = 0,
                                        .dma_config           = 0,
                                        .non_monotonic        = 0,
                                        .data_alignment       = SHAPER_LUT_DMA_DATA_ALIGNMENT,
                                        .config_alignment     = SHAPER_LUT_DMA_CONFIG_ALIGNMENT,
                                        .config_padding       = SHAPER_LUT_DMA_CONFIG_PADDING,
                                        .data_size            = SHAPER_LUT_DMA_DATA_SIZE,
                                        .config_size          = SHAPER_LUT_DMA_CONFIG_SIZE,
                                        .data_pts_per_channel = SHAPER_LUT_DATA_POINTS_PER_CHANNEL,
                                    },
                                .lut_3dlut_caps =
                                    {
                                        .data_dim_9  = 0,
                                        .data_dim_17 = 1,
                                        .data_dim_33 = 1,
                                        .dma_dim_9   = 0,
                                        .dma_dim_17  = 0,
                                        .dma_dim_33  = 1,
                                        .alignment   = LUT_3D_DMA_ALIGNMENT,
                                    },
                                .lut_3d_compound = 0,
                            },
                    }},
            .plane_caps =
                {
                    .per_pixel_alpha = 1,
                    .input_pixel_format_support =
                        {
                            .argb_packed_32b = 1,
                            .argb_packed_64b = 1,
                            .nv12            = 1,
                            .fp16            = 1,
                            .p010            = 1, /**< planar 4:2:0 10-bit */
                            .p016            = 1, /**< planar 4:2:0 16-bit */
                            .ayuv            = 1, /**< packed 4:4:4 */
                            .yuy2            = 1, /**< packed 4:2:2 */
                            .y210            = 1, /**< packed 4:2:2 10-bit */
                            .y216            = 1, /**< packed 4:2:2 16-bit */
                            .p210            = 1, /**< planar 4:2:2 10-bit */
                            .p216            = 1, /**< planar 4:2:2 16-bit */
                            .rgb8_planar     = 1, /**< planar RGB 8-bit */
                            .rgb16_planar    = 1, /**< planar RGB 16-bit */
                            .yuv8_planar     = 1, /**< planar YUV 16-bit */
                            .yuv16_planar    = 1, /**< planar YUV 16-bit */
                            .fp16_planar     = 1, /**< planar RGB 8-bit */
                            .rgbe            = 0, /**< shared exponent R9G9B9E5 */
                            .rgb111110_fix   = 0, /**< fixed R11G11B10 */
                            .rgb111110_float = 0, /**< float R11G11B10 */
                        },
                    .output_pixel_format_support =
                        {
                            .argb_packed_32b = 1,
                            .argb_packed_64b = 1,
                            .nv12            = 1,
                            .fp16            = 1,
                            .p010            = 1, /**< planar 4:2:0 10-bit */
                            .p016            = 1, /**< planar 4:2:0 16-bit */
                            .ayuv            = 1, /**< packed 4:4:4 */
                            .yuy2            = 1, /**< packed 4:2:2 */
                            .y210            = 1, /**< packed 4:2:2 10-bit */
                            .y216            = 1, /**< packed 4:2:2 16-bit */
                            .p210            = 1, /**< planar 4:2:2 10-bit */
                            .p216            = 1, /**< planar 4:2:2 16-bit */
                            .rgb8_planar     = 1, /**< planar RGB 8-bit */
                            .rgb16_planar    = 1, /**< planar RGB 16-bit */
                            .yuv8_planar     = 1, /**< planar YUV 16-bit */
                            .yuv16_planar    = 1, /**< planar YUV 16-bit */
                            .fp16_planar     = 1, /**< planar RGB 8-bit */
                            .rgbe            = 0, /**< shared exponent R9G9B9E5 */
                            .rgb111110_fix   = 0, /**< fixed R11G11B10 */
                            .rgb111110_float = 0, /**< float R11G11B10 */
                        },
                    .max_upscale_factor = 64000,

                    // limit to 4:1 downscaling ratio: 1000/4 = 250
                    .max_downscale_factor = 250,

                    .pitch_alignment          = 256,
                    .addr_alignment           = 256,
                    .max_viewport_width       = 1024,
                    .max_viewport_width_64bpp = 540,
                },
            .isharp_caps =
                {
                    .support = true,
                    .range =
                        {
                            .min  = 0,
                            .max  = 10,
                            .step = 1,
                        },
                },
            .easf_support           = 1,
            .input_dcc_support      = 1,
            .input_internal_dcc     = 1,
            .output_dcc_support     = 0,
            .output_internal_dcc    = 0,
            .histogram_support      = 1,
            .frod_support           = 1,
            .alpha_blending_support = 1,
            .alpha_fill_caps =
                {
                    .opaque        = 1,
                    .bg_color      = 1,
                    .destination   = 0,
                    .source_stream = 0,
                },
};

uint32_t vpe20_get_max_seg_width(struct output_ctx *output_ctx,
    enum vpe_surface_pixel_format format, enum vpe_scan_direction scan)
{
    if ((vpe_get_element_size_in_bytes(format, 0) == (uint8_t)8) &&
        ((scan == VPE_SCAN_PATTERN_90_DEGREE) || (scan == VPE_SCAN_PATTERN_270_DEGREE) ||
            (scan == VPE_SCAN_PATTERN_90_DEGREE_V_MIRROR) ||
            (scan == VPE_SCAN_PATTERN_270_DEGREE_V_MIRROR))) {
        return caps.plane_caps.max_viewport_width_64bpp;
    }

    return caps.plane_caps.max_viewport_width;
}

static bool is_two_pass_blend_stream(struct stream_ctx *stream_ctx)
{
    return (stream_ctx->stream_type == VPE_STREAM_TYPE_BKGR_ALPHA || stream_ctx->mps_ctx != NULL ||
               stream_ctx->stream.blend_info.blending == false ||
               vpe_should_generate_cmd_info(stream_ctx) == false)
               ? false
               : true;
}

void vpe20_update_opp_adjust_and_boundary(struct stream_ctx *stream_ctx, uint16_t seg_idx,
    bool dst_subsampled, const struct vpe_rect *src_rect, const struct vpe_rect *dst_rect,
    struct output_ctx *output_ctx, struct spl_opp_adjust *opp_recout_adjust)
{
    struct segment_ctx *segment_ctx = &stream_ctx->segment_ctx[seg_idx];

    memset(opp_recout_adjust, 0, sizeof(struct spl_opp_adjust));

    if (dst_subsampled) {
        struct fmt_extra_pixel_info extra_info;

        struct vpe_rect *unclipped_dst_rect = &stream_ctx->stream.scaling_info.dst_rect;
        struct vpe_rect *target_rect        = &output_ctx->target_rect;

        struct opp *opp = stream_ctx->vpe_priv->resource.opp[0];
        bool        extra_left_available =
            !(is_two_pass_blend_stream(stream_ctx) && (target_rect->x > unclipped_dst_rect->x));
        bool extra_right_available = !(is_two_pass_blend_stream(stream_ctx) &&
                                       ((target_rect->x + target_rect->width) <
                                           (unclipped_dst_rect->x + unclipped_dst_rect->width)));

        opp->funcs->get_fmt_extra_pixel(output_ctx->surface.format,
            stream_ctx->vpe_priv->init.debug.subsampling_quality,
            (enum chroma_cositing)output_ctx->surface.cs.cositing, &extra_info);

        if (seg_idx == 0) {
            segment_ctx->boundary_mode.left =
                extra_left_available ? stream_ctx->left : FMT_SUBSAMPLING_BOUNDARY_REPEAT;
            if (segment_ctx->boundary_mode.left == FMT_SUBSAMPLING_BOUNDARY_EXTRA) {
                opp_recout_adjust->x -= extra_info.left_pixels;
                opp_recout_adjust->width += extra_info.left_pixels;
            }
        } else {
            opp_recout_adjust->x -= extra_info.left_pixels;
            opp_recout_adjust->width += extra_info.left_pixels;
            segment_ctx->boundary_mode.left = FMT_SUBSAMPLING_BOUNDARY_EXTRA;
        }

        // see if the RIGHT most needs more for output boundary handling, right needs 1 extra

        if (seg_idx == stream_ctx->num_segments - 1) {
            segment_ctx->boundary_mode.right =
                extra_right_available ? stream_ctx->right : FMT_SUBSAMPLING_BOUNDARY_REPEAT;
            if (segment_ctx->boundary_mode.right == FMT_SUBSAMPLING_BOUNDARY_EXTRA) {
                opp_recout_adjust->width += extra_info.right_pixels;
            }
        } else {
            segment_ctx->boundary_mode.right = FMT_SUBSAMPLING_BOUNDARY_EXTRA;
            opp_recout_adjust->width += extra_info.right_pixels;
        }
    } else {
        segment_ctx->boundary_mode.left  = FMT_SUBSAMPLING_BOUNDARY_REPEAT;
        segment_ctx->boundary_mode.right = FMT_SUBSAMPLING_BOUNDARY_REPEAT;
    }

    segment_ctx->boundary_mode.top    = FMT_SUBSAMPLING_BOUNDARY_REPEAT;
    segment_ctx->boundary_mode.bottom = FMT_SUBSAMPLING_BOUNDARY_REPEAT;
    segment_ctx->opp_recout_adjust    = *opp_recout_adjust;
}

static void vpe20_spl_calc_lb_num_partitions(bool alpha_en, const struct spl_scaler_data *scl_data,
    enum lb_memory_config lb_config, int *num_part_y, int *num_part_c)
{
    int memory_line_size_y, memory_line_size_c, memory_line_size_a, lb_memory_size,
        lb_memory_size_c, lb_memory_size_a, num_partitions_a;

    uint32_t line_size   = scl_data->viewport.width < scl_data->recout.width
                               ? scl_data->viewport.width
                               : scl_data->recout.width;
    uint32_t line_size_c = scl_data->viewport_c.width < scl_data->recout.width
                               ? scl_data->viewport_c.width
                               : scl_data->recout.width;

    if (line_size == 0)
        line_size = 1;

    if (line_size_c == 0)
        line_size_c = 1;

    memory_line_size_y = (line_size + 5) / 6;   /* +5 to ceil */
    memory_line_size_c = (line_size_c + 5) / 6; /* +5 to ceil */
    memory_line_size_a = (line_size + 5) / 6;   /* +5 to ceil */

    // only has 1-piece lb config in vpe1
    lb_memory_size   = 696;
    lb_memory_size_c = 696;
    lb_memory_size_a = 696;

    *num_part_y      = lb_memory_size / memory_line_size_y;
    *num_part_c      = lb_memory_size_c / memory_line_size_c;
    num_partitions_a = lb_memory_size_a / memory_line_size_a;

    if (alpha_en && (num_partitions_a < *num_part_y))
        *num_part_y = num_partitions_a;

    if (*num_part_y > 12)
        *num_part_y = 12;
    if (*num_part_c > 12)
        *num_part_c = 12;
}

static void vpe20_spl_calc_lb_num_partitions_init(bool alpha_en,
    const struct spl_scaler_data *scl_data, enum lb_memory_config lb_config, int *num_part_y,
    int *num_part_c)
{
    if (num_part_y)
        *num_part_y = 12;
    if (num_part_c)
        *num_part_c = 12;
}

void vpe20_update_recout_dst_viewport(struct scaler_data *data,
    enum vpe_surface_pixel_format format, struct spl_opp_adjust *opp_adjust,
    bool opp_background_gen)
{
    uint32_t               vpc_h_div      = (vpe_is_yuv420(format) || vpe_is_yuv422(format)) ? 2 : 1;
    uint32_t               vpc_v_div      = vpe_is_yuv420(format) ? 2 : 1;
    struct vpe_rect       *dst_viewport   = &data->dst_viewport;
    struct vpe_rect       *dst_viewport_c = &data->dst_viewport_c;
    struct dscl_prog_data *dscl_prog_data = &data->dscl_prog_data;


    dst_viewport_c->x      = dst_viewport->x / (int32_t)vpc_h_div;
    dst_viewport_c->y      = dst_viewport->y / (int32_t)vpc_v_div;
    dst_viewport_c->width  = dst_viewport->width / vpc_h_div;
    dst_viewport_c->height = dst_viewport->height / vpc_v_div;

    // [h/v]_active
    if (opp_background_gen) {
        data->h_active                  = data->recout.width;
        data->v_active                  = data->recout.height;
        dscl_prog_data->mpc_size.width  = data->recout.width;
        dscl_prog_data->mpc_size.height = data->recout.height;
    } else {
        dscl_prog_data->mpc_size.width  = dst_viewport->width;
        dscl_prog_data->mpc_size.height = dst_viewport->height;

        if (opp_adjust != NULL) {
            dscl_prog_data->mpc_size.width += opp_adjust->width;
            dscl_prog_data->mpc_size.height += opp_adjust->height;
        }
    }

    // recout
    dscl_prog_data->recout.x      = data->recout.x;
    dscl_prog_data->recout.y      = data->recout.y;
    dscl_prog_data->recout.width  = data->recout.width;
    dscl_prog_data->recout.height = data->recout.height;
}

static void vpe20_build_mpcc_mux_params(struct vpe_priv *vpe_priv, enum vpe_cmd_ops ops,
    uint32_t pipe_idx, uint16_t cmd_num_input, enum mpc_mux_topsel *topsel,
    enum mpc_mux_botsel *botsel, enum mpc_mux_outmux *outmux, enum mpc_mux_oppid *oppid,
    enum mpcc_blend_mode *blend_mode)
{
    *topsel = pipe_idx;
    *oppid  = MPC_MUX_OPPID_OPP0;

    switch (ops) {
    case VPE_CMD_OPS_BLENDING:
        *botsel     = pipe_idx + 1;
        *outmux     = (pipe_idx == 0) ? MPC_MUX_OUTMUX_MPCC0 : MPC_MUX_OUTMUX_DISABLE;
        *blend_mode = MPCC_BLEND_MODE_TOP_BOT_BLENDING;
        // Need to disable botsel based on the last input of each cmd_inputs.
        if (pipe_idx == (uint32_t)(cmd_num_input - 1)) {
            *botsel = MPC_MUX_BOTSEL_DISABLE;

            *blend_mode = MPCC_BLEND_MODE_TOP_LAYER_ONLY;
        }

        // num dpp == num mpcc
        if (pipe_idx == (uint32_t)(vpe_priv->pub.caps->resource_caps.num_dpp - 1)) {
            *botsel     = MPC_MUX_BOTSEL_DISABLE;
            *blend_mode = MPCC_BLEND_MODE_TOP_LAYER_ONLY;
        }
        break;
    case VPE_CMD_OPS_ALPHA_THROUGH_LUMA:
        if (pipe_idx == 0) { // pipe 0 - User input plane to have bg removed
            *botsel     = 1;
            *outmux     = MPC_MUX_OUTMUX_MPCC0;
            *blend_mode = MPCC_BLEND_MODE_TOP_BOT_BLENDING;
        } else { // pipe 1 - Alpha Luma plane
            *botsel     = MPC_MUX_BOTSEL_DISABLE;
            *outmux     = MPC_MUX_OUTMUX_DISABLE;
            *blend_mode = MPCC_BLEND_MODE_TOP_LAYER_PASSTHROUGH;
        }
        break;
    default:
        // single pipe compositing / performance mode
        *outmux     = pipe_idx;
        *botsel     = MPC_MUX_BOTSEL_DISABLE;
        *oppid      = pipe_idx;
        *blend_mode = MPCC_BLEND_MODE_TOP_LAYER_ONLY;
    }

    if (vpe_priv->init.debug.mpc_bypass)
        *blend_mode = MPCC_BLEND_MODE_BYPASS;
}

void vpe20_update_src_viewport(struct scaler_data *data, enum vpe_surface_pixel_format format)
{
    uint32_t               vpc_h_div  = (vpe_is_yuv420(format) || vpe_is_yuv422(format)) ? 2 : 1;
    uint32_t               vpc_v_div  = vpe_is_yuv420(format) ? 2 : 1;
    struct vpe_rect       *viewport   = &data->viewport;
    struct vpe_rect       *viewport_c = &data->viewport_c;
    struct dscl_prog_data *dscl_prog_data = &data->dscl_prog_data;

    viewport_c->x      = viewport->x / (int32_t)vpc_h_div;
    viewport_c->y      = viewport->y / (int32_t)vpc_v_div;
    viewport_c->width  = viewport->width / vpc_h_div;
    viewport_c->height = viewport->height / vpc_v_div;

    dscl_prog_data->viewport.x = viewport->x;
    dscl_prog_data->viewport.y = viewport->y;

    dscl_prog_data->viewport.width    = viewport->width;
    dscl_prog_data->viewport.height   = viewport->height;
    dscl_prog_data->viewport_c.width  = viewport_c->width;
    dscl_prog_data->viewport_c.height = viewport_c->height;
}

// Note - No additional adjustments are made for rotation here as YUV422 does not support rotation
static void adjust_packed_422_scaler_params(struct spl_out *spl_output, bool is_h_mirror)
{
    uint32_t hinit_int      = spl_output->dscl_prog_data->init.h_filter_init_int;
    uint32_t hinit_c_int    = spl_output->dscl_prog_data->init.h_filter_init_int_c;
    int      luma_x_start   = spl_output->dscl_prog_data->viewport.x;
    int      luma_x_end     = luma_x_start + spl_output->dscl_prog_data->viewport.width;
    int      chroma_x_start = spl_output->dscl_prog_data->viewport_c.x;
    int      chroma_x_end   = chroma_x_start + spl_output->dscl_prog_data->viewport_c.width;

    int luma_x_start_aligned   = luma_x_start & ~1;
    int luma_x_end_aligned     = (luma_x_end + 1) & ~1;
    int chroma_x_start_aligned = chroma_x_start * 2;
    int chroma_x_end_aligned   = chroma_x_end * 2;
    int x_start_aligned = (luma_x_start_aligned < chroma_x_start_aligned) ? luma_x_start_aligned
                                                                          : chroma_x_start_aligned;
    int x_end_aligned =
        (luma_x_end_aligned > chroma_x_end_aligned) ? luma_x_end_aligned : chroma_x_end_aligned;

    if (is_h_mirror == false) {
        if (luma_x_start > x_start_aligned) {
            uint32_t drop = luma_x_start - x_start_aligned;
            hinit_int += drop;
        }

        if (chroma_x_start > (x_start_aligned / 2)) {
            uint32_t drop = chroma_x_start - (x_start_aligned / 2);
            hinit_c_int += drop;
        }
    } else {
        if (x_end_aligned > luma_x_end) {
            uint32_t drop = x_end_aligned - luma_x_end;
            hinit_int += drop;
        }

        if ((x_end_aligned / 2) > (chroma_x_end)) {
            uint32_t drop = (x_end_aligned / 2) - chroma_x_end;
            hinit_c_int += drop;
        }
    }

    spl_output->dscl_prog_data->init.h_filter_init_int   = hinit_int;
    spl_output->dscl_prog_data->init.h_filter_init_int_c = hinit_c_int;
    spl_output->dscl_prog_data->viewport.x               = x_start_aligned;
    spl_output->dscl_prog_data->viewport.width           = x_end_aligned - x_start_aligned;
    spl_output->dscl_prog_data->viewport_c.x             = x_start_aligned / 2;
    spl_output->dscl_prog_data->viewport_c.width         = (x_end_aligned - x_start_aligned) / 2;
}

static void set_dst_cmd_boundary_mode_and_opp_adjust(struct vpe_priv *vpe_priv,
    struct vpe_rect dst_viewport, struct fmt_boundary_mode *output_boundary_mode,
    struct spl_opp_adjust *output_opp_adjust)
{
    struct fmt_extra_pixel_info extra_pixel;

    struct opp        *opp        = vpe_priv->resource.opp[0];
    struct output_ctx *output_ctx = &vpe_priv->output_ctx;

    opp->funcs->get_fmt_extra_pixel(output_ctx->surface.format,
        vpe_priv->init.debug.subsampling_quality,
        (enum chroma_cositing)output_ctx->surface.cs.cositing, &extra_pixel);

    // Ensure both streams have same seam size, and if not set to BOUNDARY_REPEAT
    if (output_boundary_mode->left == FMT_SUBSAMPLING_BOUNDARY_EXTRA) {
        bool do_streams_have_same_seam_size =
            (dst_viewport.x >= output_ctx->target_rect.x) &&
            (extra_pixel.left_pixels == (uint32_t)(abs(output_opp_adjust->x)));

        if (!do_streams_have_same_seam_size) {
            output_boundary_mode->left = FMT_SUBSAMPLING_BOUNDARY_REPEAT;
            output_opp_adjust->width += output_opp_adjust->x;
            output_opp_adjust->x = 0;
        }
    }

    if (output_boundary_mode->top == FMT_SUBSAMPLING_BOUNDARY_EXTRA) {
        bool do_streams_have_same_seam_size =
            (dst_viewport.y >= output_ctx->target_rect.y) &&
            (extra_pixel.top_pixels == (uint32_t)(abs(output_opp_adjust->y)));

        if (!do_streams_have_same_seam_size) {
            output_boundary_mode->top = FMT_SUBSAMPLING_BOUNDARY_REPEAT;
            output_opp_adjust->height += output_opp_adjust->y;
            output_opp_adjust->y = 0;
        }
    }

    if (output_boundary_mode->right == FMT_SUBSAMPLING_BOUNDARY_EXTRA) {
        uint32_t right_seam = (uint32_t)(output_opp_adjust->width + output_opp_adjust->x);
        bool     do_streams_have_same_seam_size =
            (dst_viewport.x + dst_viewport.width <=
                output_ctx->target_rect.x + output_ctx->target_rect.width) &&
            (extra_pixel.right_pixels == right_seam);

        if (!do_streams_have_same_seam_size) {
            output_boundary_mode->right = FMT_SUBSAMPLING_BOUNDARY_REPEAT;
            output_opp_adjust->width -= right_seam;
        }
    }

    if (output_boundary_mode->bottom == FMT_SUBSAMPLING_BOUNDARY_EXTRA) {
        uint32_t bottom_seam = (uint32_t)(output_opp_adjust->height + output_opp_adjust->y);
        bool     do_streams_have_same_seam_size =
            (dst_viewport.y + dst_viewport.height <=
                output_ctx->target_rect.y + output_ctx->target_rect.height) &&
            (extra_pixel.bottom_pixels == bottom_seam);

        if (!do_streams_have_same_seam_size) {
            output_boundary_mode->bottom = FMT_SUBSAMPLING_BOUNDARY_REPEAT;
            output_opp_adjust->height -= bottom_seam;
        }
    }
}

static void update_pipe_ctrl_param(struct opp_pipe_control_params *param,
    struct output_ctx *output_ctx, struct vpe_surface_info *surface_info,
    struct vpe_cmd_info *cmd_info)
{
    uint16_t alpha_16;
    bool     opp_dig_bypass = false;

    if (cmd_info->ops == VPE_CMD_OPS_ALPHA_THROUGH_LUMA) {
        param->alpha         = 0;
        param->bypass_enable = true;
        param->alpha_select  = 1;
    } else {
        if (vpe_is_fp16(surface_info->format)) {
            if (output_ctx->alpha_mode == VPE_ALPHA_BGCOLOR)
                vpe_convert_from_float_to_fp16((double)output_ctx->mpc_bg_color.rgba.a, &alpha_16);
            else
                vpe_convert_from_float_to_fp16(1.0, &alpha_16);

            opp_dig_bypass = true;
        } else {
            if (output_ctx->alpha_mode == VPE_ALPHA_BGCOLOR)
                alpha_16 = (uint16_t)(output_ctx->mpc_bg_color.rgba.a * 0xffff);
            else
                alpha_16 = 0xffff;
        }

        param->alpha         = alpha_16;
        param->bypass_enable = opp_dig_bypass;
        param->alpha_select  = 0;
    }
}

bool vpe20_set_dst_cmd_info_scaler(struct stream_ctx *dst_stream_ctx,
    struct scaler_data *dst_scaler_data, struct vpe_rect recout, struct vpe_rect dst_viewport,
    struct fmt_boundary_mode *boundary_mode, struct spl_opp_adjust *opp_adjust)
{
    struct vpe_priv         *vpe_priv     = dst_stream_ctx->vpe_priv;
    struct spl_in           *spl_input    = &dst_stream_ctx->spl_input;
    struct spl_out          *spl_output   = &dst_stream_ctx->spl_output;
    struct vpe_scaling_info *scaling_info = &dst_stream_ctx->stream.scaling_info;
    struct output_ctx       *output_ctx   = &vpe_priv->output_ctx;

    dst_scaler_data->format = output_ctx->surface.format;

    // Set the vpe_scaling_info for the destination stream

    scaling_info->src_rect = recout;
    scaling_info->dst_rect = recout;

    scaling_info->src_rect.x += dst_viewport.x + opp_adjust->x;
    scaling_info->src_rect.y += dst_viewport.y;
    scaling_info->dst_rect.x += dst_viewport.x + opp_adjust->x;
    scaling_info->dst_rect.y += dst_viewport.y;

    // Set SPL input / output params in preparation for spl_calculate_scaler_params

    spl_output->dscl_prog_data                      = &dst_scaler_data->dscl_prog_data;
    spl_input->callbacks.spl_calc_lb_num_partitions = vpe20_spl_calc_lb_num_partitions;
    spl_input->basic_out.max_downscale_src_width    = 0; // Set to zero to bypass SPL sanity checks
    spl_input->basic_in.mpc_h_slice_index           = 0;

    // In two pass stuations, we need to check LLS support for dst stream as well
    vpe_priv->resource.set_lls_pref(
        vpe_priv, spl_input, dst_stream_ctx->tf, dst_stream_ctx->stream.surface_info.format);

    dst_stream_ctx->num_segments = 1;
    dst_stream_ctx->left         = boundary_mode->left;
    dst_stream_ctx->right        = boundary_mode->right;

    vpe_init_spl_in(spl_input, dst_stream_ctx, output_ctx);

    if (!SPL_NAMESPACE(spl_calculate_scaler_params(spl_input, spl_output)))
        return false;

    if (vpe_is_yuv422(output_ctx->surface.format) && vpe_is_yuv_packed(output_ctx->surface.format))
        adjust_packed_422_scaler_params(spl_output, false);

    vpe_spl_scl_to_vpe_scl(spl_output, dst_scaler_data);

    // Set src/dst viewport and recout info

    dst_scaler_data->viewport.x          = recout.x + dst_viewport.x + opp_adjust->x;
    dst_scaler_data->viewport.y          = recout.y + dst_viewport.y + opp_adjust->y;
    dst_scaler_data->viewport.width      = dst_scaler_data->recout.width;
    dst_scaler_data->viewport.height     = dst_scaler_data->recout.height;
    dst_scaler_data->dst_viewport.x      = recout.x + dst_viewport.x;
    dst_scaler_data->dst_viewport.y      = recout.y + dst_viewport.y;
    dst_scaler_data->dst_viewport.width  = dst_viewport.width;
    dst_scaler_data->dst_viewport.height = dst_viewport.height;

    if (vpe_priv->init.debug.opp_background_gen == 1) {
        dst_scaler_data->recout.y = 0;
        dst_scaler_data->recout.x = 0;
    } else {
        dst_scaler_data->recout.y = recout.y;
        dst_scaler_data->recout.x = recout.x;
    }

    // Calculate extra pixel info for subsampling

    set_dst_cmd_boundary_mode_and_opp_adjust(vpe_priv, dst_viewport, boundary_mode, opp_adjust);

    vpe20_update_recout_dst_viewport(dst_scaler_data, dst_stream_ctx->stream.surface_info.format,
        opp_adjust, dst_stream_ctx->vpe_priv->init.debug.opp_background_gen == 1);
    vpe20_update_src_viewport(dst_scaler_data, dst_stream_ctx->stream.surface_info.format);

    return true;
}

static bool should_stream_generate_background(struct stream_ctx *stream_ctx)
{
    uint32_t stream_idx_to_generate_background;

    if (stream_ctx->vpe_priv->stream_ctx[0].stream.flags.is_alpha_plane != false)
        // If our first op is BGKR, then the BKGR background stream will do gen background
        stream_idx_to_generate_background = VPE_BKGR_STREAM_BACKGROUND_OFFSET;
    else
        // Otherwise, first stream will always generate background
        stream_idx_to_generate_background = 0;

    return stream_idx_to_generate_background == (uint32_t)stream_ctx->stream_idx;
}

enum vpe_status vpe20_fill_performance_mode_cmd_info(
    struct vpe_priv *vpe_priv, uint16_t stream_idx, uint16_t avail_pipe_count)
{
    struct stream_ctx  *stream_ctx  = &vpe_priv->stream_ctx[stream_idx];
    uint16_t            segment_idx = 0;
    uint16_t            i;
    struct vpe_cmd_info cmd_info   = {0};
    enum lut3d_type     lut3d_type = vpe_get_stream_lut3d_type(stream_ctx);

    while (segment_idx < stream_ctx->num_segments) {
        uint16_t cmd_pipe_count     = min(avail_pipe_count, stream_ctx->num_segments - segment_idx);
        cmd_info.num_inputs         = cmd_pipe_count;
        cmd_info.num_outputs        = cmd_pipe_count;
        cmd_info.ops                = VPE_CMD_OPS_COMPOSITING;
        cmd_info.lut3d_type         = lut3d_type;
        cmd_info.insert_start_csync = false;
        cmd_info.insert_end_csync   = false;
        cmd_info.cd                 = (uint16_t)int_divide_with_ceil(stream_ctx->num_segments - segment_idx, avail_pipe_count) - 1;

        for (i = 0; i < cmd_pipe_count; i++) {
            cmd_info.inputs[i].stream_idx = stream_idx;
            memcpy(&(cmd_info.inputs[i].scaler_data),
                &(stream_ctx->segment_ctx[segment_idx + i].scaler_data),
                sizeof(struct scaler_data));

            cmd_info.outputs[i].dst_viewport =
                stream_ctx->segment_ctx[segment_idx + i].scaler_data.dst_viewport;
            cmd_info.outputs[i].dst_viewport_c =
                stream_ctx->segment_ctx[segment_idx + i].scaler_data.dst_viewport_c;
            cmd_info.outputs[i].boundary_mode =
                stream_ctx->segment_ctx[segment_idx + i].boundary_mode;
            cmd_info.outputs[i].opp_recout_adjust =
                stream_ctx->segment_ctx[segment_idx + i].opp_recout_adjust;
        }
        segment_idx += cmd_pipe_count;
        vpe_vector_push(vpe_priv->vpe_cmd_vector, &cmd_info);

    }

    return VPE_STATUS_OK;
}

enum vpe_status vpe20_fill_blending_cmd_info(
    struct vpe_priv *vpe_priv, uint16_t top_stream_idx, uint16_t bot_stream_idx)
{
    struct fmt_boundary_mode *boundary_mode;
    struct segment_ctx       *segment_ctx;
    uint16_t                  dest_cmd_info_idx, input_cmd_info_idx;

    struct stream_ctx  *top_stream_ctx   = NULL;
    struct stream_ctx  *bot_stream_ctx   = NULL;
    struct stream_ctx  *input_stream_ctx = NULL;
    struct stream_ctx  *dest_stream_ctx  = NULL;
    struct vpe_cmd_info cmd_info         = {0};
    struct vpe_rect     dst_viewport     = {0};
    struct vpe_rect     dst_viewport_c   = {0};
    struct vpe_rect     recout           = {0};
    uint16_t            segment_idx      = 0;
    enum lut3d_type     lut3d_type       = LUT3D_TYPE_NONE;
    uint16_t            num_segments     = 0;
    enum vpe_status     status           = VPE_STATUS_OK;
    struct spl_opp_adjust *opp_adjust;

    if ((top_stream_idx == VPE_DESTINATION_AS_INPUT_STREAM_INDEX) ||
        (bot_stream_idx == VPE_DESTINATION_AS_INPUT_STREAM_INDEX)) {
        dest_stream_ctx = vpe_get_virtual_stream(vpe_priv, VPE_STREAM_TYPE_DESTINATION);
        if (dest_stream_ctx == NULL)
            status = VPE_STATUS_ERROR;
    } else {
        status = VPE_STATUS_ERROR; // at least one stream must be destination as input
    }

    if (top_stream_idx != VPE_DESTINATION_AS_INPUT_STREAM_INDEX) {
        top_stream_ctx     = &vpe_priv->stream_ctx[top_stream_idx];
        bot_stream_ctx     = dest_stream_ctx;
        input_stream_ctx   = top_stream_ctx;
        dest_cmd_info_idx  = 1;
        input_cmd_info_idx = 0;
    } else if (bot_stream_idx != VPE_DESTINATION_AS_INPUT_STREAM_INDEX) {
        top_stream_ctx     = dest_stream_ctx;
        bot_stream_ctx     = &vpe_priv->stream_ctx[bot_stream_idx];
        input_stream_ctx   = bot_stream_ctx;
        dest_cmd_info_idx  = 0;
        input_cmd_info_idx = 1;
    } else {
        dest_cmd_info_idx  = 0; // need to set these for compilation
        input_cmd_info_idx = 0;
        status             = VPE_STATUS_ERROR;
    }

    if ((input_stream_ctx != NULL) && (status == VPE_STATUS_OK)) {
        lut3d_type   = vpe_get_stream_lut3d_type(input_stream_ctx);
        num_segments = input_stream_ctx->num_segments;
    } else {
        status = VPE_STATUS_ERROR;
    }

    if ((top_stream_ctx == NULL) || (bot_stream_ctx == NULL))
        status = VPE_STATUS_ERROR;

    if (status == VPE_STATUS_OK) {
        for (segment_idx = 0; segment_idx < num_segments; segment_idx++) {

            cmd_info.num_inputs         = 2;
            cmd_info.ops                = VPE_CMD_OPS_BLENDING;
            cmd_info.lut3d_type         = lut3d_type;
            cmd_info.insert_start_csync = false;
            cmd_info.insert_end_csync   = false;
            cmd_info.cd                 = (uint16_t)(num_segments - segment_idx - 1);

            cmd_info.inputs[0].stream_idx = (uint16_t)top_stream_ctx->stream_idx;
            cmd_info.inputs[1].stream_idx = (uint16_t)bot_stream_ctx->stream_idx;

            if (input_stream_ctx == NULL) {
                status = VPE_STATUS_ERROR;
                break;
            }

            // Build scaler data for destination as input stream

            segment_ctx    = &input_stream_ctx->segment_ctx[segment_idx];
            boundary_mode  = &segment_ctx->boundary_mode;
            dst_viewport   = segment_ctx->scaler_data.dst_viewport;
            dst_viewport_c = segment_ctx->scaler_data.dst_viewport_c;
            opp_adjust = &segment_ctx->opp_recout_adjust;

            if (!vpe_priv->resource.set_dst_cmd_info_scaler(dest_stream_ctx,
                    &cmd_info.inputs[dest_cmd_info_idx].scaler_data,
                    segment_ctx->scaler_data.recout, dst_viewport, boundary_mode, opp_adjust)) {
                status = VPE_STATUS_SCALER_NOT_SET;
                break;
            }

            cmd_info.inputs[dest_cmd_info_idx].scaler_data.lb_params.alpha_en =
                dest_stream_ctx->per_pixel_alpha;

            if (status != VPE_STATUS_OK)
                break;

            memcpy(&(cmd_info.inputs[input_cmd_info_idx].scaler_data), &segment_ctx->scaler_data,
                sizeof(struct scaler_data));

            // Validate that recout + MPC sizes line up (recout) so blending works

            VPE_ASSERT((cmd_info.inputs[0].scaler_data.recout.width ==
                           cmd_info.inputs[1].scaler_data.recout.width) &&
                       (cmd_info.inputs[0].scaler_data.recout.height ==
                           cmd_info.inputs[1].scaler_data.recout.height));

            VPE_ASSERT((cmd_info.inputs[0].scaler_data.dscl_prog_data.mpc_size.width ==
                           cmd_info.inputs[1].scaler_data.dscl_prog_data.mpc_size.width) &&
                       (cmd_info.inputs[0].scaler_data.dscl_prog_data.mpc_size.height ==
                           cmd_info.inputs[1].scaler_data.dscl_prog_data.mpc_size.height));

            // Program cmd output valus

            cmd_info.num_outputs               = 1;
            cmd_info.outputs[0].dst_viewport   = dst_viewport;
            cmd_info.outputs[0].dst_viewport_c = dst_viewport_c;
            cmd_info.outputs[0].boundary_mode  = *boundary_mode;
            cmd_info.outputs[0].opp_recout_adjust = *opp_adjust;
            if (vpe_priv->output_ctx.frod_param.enable_frod) {
                cmd_info.num_outputs = FROD_NUM_OUTPUTS;
                cmd_info.frod_param.enable_frod = true;
            }

            vpe_vector_push(vpe_priv->vpe_cmd_vector, &cmd_info);
        }
    }

    return status;
}

enum vpe_status vpe20_fill_alpha_through_luma_cmd_info(
    struct vpe_priv *vpe_priv, uint16_t alpha_stream_idx)
{
    struct stream_ctx *alpha_stream =
        &vpe_priv->stream_ctx[alpha_stream_idx + VPE_BKGR_STREAM_ALPHA_OFFSET];
    struct stream_ctx *video_stream =
        &vpe_priv->stream_ctx[alpha_stream_idx + VPE_BKGR_STREAM_VIDEO_OFFSET];
    struct vpe_cmd_info cmd_info     = {0};
    uint16_t            segment_idx  = 0;
    enum lut3d_type     lut3d_type   = vpe_get_stream_lut3d_type(video_stream);

    for (segment_idx = 0; segment_idx < video_stream->num_segments; segment_idx++) {

        cmd_info.num_inputs           = 2;
        cmd_info.inputs[0].stream_idx = alpha_stream_idx + VPE_BKGR_STREAM_ALPHA_OFFSET;
        cmd_info.inputs[1].stream_idx = alpha_stream_idx + VPE_BKGR_STREAM_VIDEO_OFFSET;
        cmd_info.cd                   = (uint16_t)(video_stream->num_segments - segment_idx - 1);

        // For alpha luma stream, NV12 stream treated as one plane, so same chroma viewport as luma
        // viewport
        alpha_stream->segment_ctx[segment_idx].scaler_data.dst_viewport_c =
            alpha_stream->segment_ctx[segment_idx].scaler_data.dst_viewport;

        memcpy(&(cmd_info.inputs[1].scaler_data),
            &(video_stream->segment_ctx[segment_idx].scaler_data), sizeof(struct scaler_data));
        memcpy(&(cmd_info.inputs[0].scaler_data),
            &(alpha_stream->segment_ctx[segment_idx].scaler_data), sizeof(struct scaler_data));

        // Ensure MPC sizes line up (recout) so blending works
        VPE_ASSERT((cmd_info.inputs[0].scaler_data.recout.width ==
                       cmd_info.inputs[1].scaler_data.recout.width) &&
                   (cmd_info.inputs[0].scaler_data.recout.height ==
                       cmd_info.inputs[1].scaler_data.recout.height));

        cmd_info.num_outputs = 1;
        cmd_info.outputs[0].dst_viewport =
            alpha_stream->segment_ctx[segment_idx].scaler_data.dst_viewport;
        cmd_info.outputs[0].dst_viewport_c =
            alpha_stream->segment_ctx[segment_idx].scaler_data.dst_viewport_c;
        cmd_info.outputs[0].boundary_mode = alpha_stream->segment_ctx[segment_idx].boundary_mode;
        cmd_info.outputs[0].opp_recout_adjust =
            alpha_stream->segment_ctx[segment_idx].opp_recout_adjust;
        cmd_info.ops                = VPE_CMD_OPS_ALPHA_THROUGH_LUMA;
        cmd_info.lut3d_type         = lut3d_type;
        cmd_info.insert_start_csync = false;
        cmd_info.insert_end_csync   = false;

        vpe_vector_push(vpe_priv->vpe_cmd_vector, &cmd_info);
    }

    return VPE_STATUS_OK;
}

enum vpe_status vpe20_fill_non_performance_mode_cmd_info(
    struct vpe_priv *vpe_priv, uint16_t stream_idx)
{
    struct stream_ctx        *stream_ctx = &vpe_priv->stream_ctx[stream_idx];
    uint16_t                  segment_idx;
    struct vpe_cmd_info       cmd_info = {0};
    struct fmt_boundary_mode *boundary_mode;
    enum lut3d_type           lut3d_type = vpe_get_stream_lut3d_type(stream_ctx);

    for (segment_idx = 0; segment_idx < stream_ctx->num_segments; segment_idx++) {
        boundary_mode                  = &stream_ctx->segment_ctx[segment_idx].boundary_mode;
        cmd_info.inputs[0].stream_idx  = stream_idx;
        cmd_info.cd                    = (uint16_t)(stream_ctx->num_segments - segment_idx - 1);
        cmd_info.inputs[0].scaler_data = stream_ctx->segment_ctx[segment_idx].scaler_data;
        cmd_info.num_outputs           = 1;
        if (vpe_priv->output_ctx.frod_param.enable_frod) {
            cmd_info.num_outputs = FROD_NUM_OUTPUTS;
            cmd_info.frod_param.enable_frod = true;
        }
        cmd_info.outputs[0].dst_viewport =
            stream_ctx->segment_ctx[segment_idx].scaler_data.dst_viewport;
        cmd_info.outputs[0].dst_viewport_c =
            stream_ctx->segment_ctx[segment_idx].scaler_data.dst_viewport_c;
        cmd_info.outputs[0].boundary_mode = stream_ctx->segment_ctx[segment_idx].boundary_mode;
        cmd_info.outputs[0].opp_recout_adjust =
            stream_ctx->segment_ctx[segment_idx].opp_recout_adjust;
        cmd_info.num_inputs           = 1;
        cmd_info.ops                  = VPE_CMD_OPS_COMPOSITING;
        cmd_info.lut3d_type           = lut3d_type;
        cmd_info.insert_start_csync   = false;
        cmd_info.insert_end_csync     = false;

        vpe_vector_push(vpe_priv->vpe_cmd_vector, &cmd_info);
    }

    return VPE_STATUS_OK;
}

static struct vpe_rect generate_opp_dst_rect(uint32_t pipe_idx, struct vpe_cmd_info *cmd_info)
{
    uint32_t            input_idx;
    struct vpe_rect     opp_dst_rect = {0};
    struct scaler_data *s_data;

    // Determine which front-end input we should be mapping the OPP background generation to
    if (cmd_info->frod_param.enable_frod) {
        // For FROD, only 2 cases are 1-pipe normal operation, and blending. For both of these
        // we pick pipe 0 (refer to comments below)
        input_idx = 0;
    } else {
        if (cmd_info->num_inputs == cmd_info->num_outputs) {
            // perf mode and normal 1 pipe case: each pipe should generate its own background
            input_idx = pipe_idx;
        } else if (cmd_info->num_inputs > 1 && cmd_info->num_outputs == 1) { // blending case
            // If blending we only want to generate bg around the final output, which will be the
            // top-most input (aka input 0)
            input_idx = 0;
        } else {
            VPE_ASSERT(false); // Should not ever hit this
            input_idx = 0;
        }
    }

    if (input_idx >= MAX_INPUT_PIPE) { // Need this for compilation
        VPE_ASSERT(false);
        input_idx = 0;
    }

    s_data = &cmd_info->inputs[input_idx].scaler_data;

    opp_dst_rect.x      = s_data->dst_viewport.x + s_data->recout.x;
    opp_dst_rect.y      = s_data->dst_viewport.y + s_data->recout.y;

    // BG segments will set w/h to MIN_VIEWPORT_SIZE, but mpc output is 0. Need opp rect to match
    if (cmd_info->ops == VPE_CMD_OPS_BG) {
        opp_dst_rect.width  = 0;
        opp_dst_rect.height = 0;
    } else {
        opp_dst_rect.width  = s_data->recout.width;
        opp_dst_rect.height = s_data->recout.height;
    }
    /* Note:
     *  After spl, recout.x is non zero for 2nd segment onwards.
     *  However, calculate_dst_viewport_and_active() will adjust the x/y back to 0.
     *  it is due to each segment is an independent job to VPE and
     *  VPE is only seeing this segment's output starting from 0,0 position.
     *  so recout.x should reset to 0, if it is non-0, meaning that
     *  background generation is needed.
     *  Here we have to drop the extra pixels needed by OPP FMT subsampling,
     *  after OPP FMT, those pixels are dropped before going to OPP OUTBG block,
     *  thus adjustment to width & height are needed here.
     *  x, y are not needed as recout.x is reset and dst_viewport.x/y already adjusted
     *  in calculate_dst_viewport_and_active()
     */
    opp_dst_rect.width -= cmd_info->outputs[pipe_idx].opp_recout_adjust.width;
    opp_dst_rect.height -= cmd_info->outputs[pipe_idx].opp_recout_adjust.height;
    return opp_dst_rect;
}

static bool init_scaler_data(struct stream_ctx *stream_ctx, struct spl_in *spl_input,
    struct spl_out *spl_output, struct scaler_data *scl_data, struct output_ctx *output_ctx,
    uint32_t max_seg_width)
{
    struct vpe_priv              *vpe_priv     = stream_ctx->vpe_priv;
    enum vpe_surface_pixel_format pixel_format = stream_ctx->stream.surface_info.format;
    struct dscl_prog_data dscl_prog_data;

    if (vpe_rec_is_equal(output_ctx->surface.plane_size.surface_size, output_ctx->target_rect) &&
        vpe_rec_is_equal(stream_ctx->stream.scaling_info.dst_rect, output_ctx->target_rect)) {
        spl_input->is_fullscreen = true;
    } else {
        spl_input->is_fullscreen = false;
    }

    vpe_priv->resource.set_lls_pref(vpe_priv, spl_input, stream_ctx->tf, pixel_format);

    /* To get the number of taps, SPL does not need to check if the input/output frame fits into
     * the line buffer since it uses the whole source/destination sizes for getting the number of
     * taps. After calculating thenumber of taps VPE calculates the maximum viewport size that fits
     * into the line buffer based on the calculated number of vertical taps.
     *
     * Initially, set the partition function to return maximum number of lines the line buffer can
     * fit; otherwise it would fail with frames with src/dst width more than 4176 pixels, e.g. 8K.
     * After getting the number of taps, set the partition function to calculate the correct number
     * of partitions for each segment.
     */
    spl_input->callbacks.spl_calc_lb_num_partitions = vpe20_spl_calc_lb_num_partitions_init;
    spl_input->basic_out.max_downscale_src_width    = 0;  // Set to zero to bypass SPL sanity checks
    spl_input->basic_out.always_scale =
        (vpe_is_yuv422(pixel_format) && vpe_is_yuv_packed(pixel_format));
    spl_input->basic_in.mpc_h_slice_index           = 0;
    spl_output->dscl_prog_data                      = &dscl_prog_data;
    stream_ctx->num_segments                        = 1;

    vpe_init_spl_in(spl_input, stream_ctx, output_ctx);

    if (!SPL_NAMESPACE(spl_get_number_of_taps(spl_input, spl_output)))
        return false;

    spl_input->callbacks.spl_calc_lb_num_partitions = vpe20_spl_calc_lb_num_partitions;

    scl_data->taps.h_taps   = spl_output->dscl_prog_data->taps.h_taps + 1;
    scl_data->taps.v_taps   = spl_output->dscl_prog_data->taps.v_taps + 1;
    scl_data->taps.h_taps_c = spl_output->dscl_prog_data->taps.h_taps_c + 1;
    scl_data->taps.v_taps_c = spl_output->dscl_prog_data->taps.h_taps_c + 1;

    return true;
}

struct cdc_fe *vpe20_cdc_fe_create(struct vpe_priv *vpe_priv, int inst)
{
    struct vpe20_cdc_fe *vpe_cdc_fe = vpe_zalloc(sizeof(struct vpe20_cdc_fe));

    if (!vpe_cdc_fe)
        return NULL;

    vpe20_construct_cdc_fe(vpe_priv, &vpe_cdc_fe->base);

    vpe_cdc_fe->base.inst = inst;
    vpe_cdc_fe->regs      = &cdc_fe_regs[inst];
    vpe_cdc_fe->mask      = &cdc_fe_mask;
    vpe_cdc_fe->shift     = &cdc_fe_shift;

    return &vpe_cdc_fe->base;
}

struct cdc_be *vpe20_cdc_be_create(struct vpe_priv *vpe_priv, int inst)
{
    struct vpe20_cdc_be *vpe_cdc_be = vpe_zalloc(sizeof(struct vpe20_cdc_be));

    if (!vpe_cdc_be)
        return NULL;

    vpe20_construct_cdc_be(vpe_priv, &vpe_cdc_be->base);

    vpe_cdc_be->base.inst = inst;
    vpe_cdc_be->regs      = &cdc_be_regs[inst];
    vpe_cdc_be->mask      = &cdc_be_mask;
    vpe_cdc_be->shift     = &cdc_be_shift;

    return &vpe_cdc_be->base;
}

struct dpp *vpe20_dpp_create(struct vpe_priv *vpe_priv, int inst)
{
    struct vpe20_dpp *vpe_dpp = vpe_zalloc(sizeof(struct vpe20_dpp));

    if (!vpe_dpp)
        return NULL;

    vpe20_construct_dpp(vpe_priv, &vpe_dpp->base);

    vpe_dpp->base.inst = inst;
    vpe_dpp->regs      = &dpp_regs[inst];
    vpe_dpp->mask      = &dpp_mask;
    vpe_dpp->shift     = &dpp_shift;

    return &vpe_dpp->base;
}

struct opp *vpe20_opp_create(struct vpe_priv *vpe_priv, int inst)
{
    struct vpe20_opp *vpe_opp = vpe_zalloc(sizeof(struct vpe20_opp));

    if (!vpe_opp)
        return NULL;

    vpe20_construct_opp(vpe_priv, &vpe_opp->base);

    vpe_opp->base.inst = inst;
    vpe_opp->regs      = &opp_regs[inst];
    vpe_opp->mask      = &opp_mask;
    vpe_opp->shift     = &opp_shift;

    return &vpe_opp->base;
}

struct mpc *vpe20_mpc_create(struct vpe_priv *vpe_priv, int inst)
{
    struct vpe20_mpc *vpe_mpc = vpe_zalloc(sizeof(struct vpe20_mpc));

    if (!vpe_mpc)
        return NULL;

    vpe20_construct_mpc(vpe_priv, &vpe_mpc->base);

    vpe_mpc->base.inst = inst;
    vpe_mpc->regs      = &mpc_regs[inst];
    vpe_mpc->mask      = &mpc_mask;
    vpe_mpc->shift     = &mpc_shift;

    return &vpe_mpc->base;
}

enum vpe_status vpe20_construct_resource(struct vpe_priv *vpe_priv, struct resource *res)
{
    struct vpe *vpe = &vpe_priv->pub;
    uint32_t    i;

    vpe->caps      = &caps;

    vpe10_construct_vpec(vpe_priv, &res->vpec);

    for (i = 0; i < vpe->caps->resource_caps.num_dpp; i++) { // num pipes = num dpp = num_mpc
        res->cdc_fe[i] = vpe20_cdc_fe_create(vpe_priv, i);
        if (res->cdc_fe[i] == NULL)
            goto err;

        res->dpp[i] = vpe20_dpp_create(vpe_priv, i);
        if (res->dpp[i] == NULL)
            goto err;

        res->mpc[i] = vpe20_mpc_create(vpe_priv, i);
        if (res->mpc[i] == NULL)
            goto err;
    }

    for (i = 0; i < vpe->caps->resource_caps.num_cdc_be; i++) {
        res->cdc_be[i] = vpe20_cdc_be_create(vpe_priv, i);
        if (res->cdc_be[i] == NULL)
            goto err;
    }

    for (i = 0; i < vpe->caps->resource_caps.num_opp; i++) { // num opp = num dpp
        res->opp[i] = vpe20_opp_create(vpe_priv, i);
        if (res->opp[i] == NULL)
            goto err;
    }

    vpe20_construct_cmd_builder(vpe_priv, &res->cmd_builder);
    vpe20_construct_vpe_desc_writer(&vpe_priv->vpe_desc_writer);
    vpe20_construct_plane_desc_writer(&vpe_priv->plane_desc_writer);
    vpe20_config_writer_init(&vpe_priv->config_writer);

    vpe_priv->num_pipe = 2;

    res->internal_hdr_normalization = 1;

    // Many of the below will need VPE20 versions.
    res->check_h_mirror_support             = vpe20_check_h_mirror_support;
    res->calculate_segments                 = vpe20_calculate_segments;
    res->get_max_seg_width                  = vpe20_get_max_seg_width;
    res->set_num_segments                   = vpe20_set_num_segments;
    res->split_bg_gap                       = vpe10_split_bg_gap;
    res->calculate_dst_viewport_and_active  = vpe20_calculate_dst_viewport_and_active;
    res->get_bg_stream_idx                  = vpe20_get_bg_stream_idx;
    res->find_bg_gaps                       = vpe_find_bg_gaps;
    res->create_bg_segments                 = vpe20_create_bg_segments;
    res->populate_cmd_info                  = vpe20_populate_cmd_info;
    res->program_frontend                   = vpe20_program_frontend;
    res->program_backend                    = vpe20_program_backend;
    res->get_bufs_req                       = vpe20_get_bufs_req;
    res->check_bg_color_support             = vpe20_check_bg_color_support;
    res->bg_color_convert                   = vpe20_bg_color_convert;
    res->check_mirror_rotation_support      = vpe20_check_mirror_rotation_support;
    res->update_blnd_gamma                  = vpe20_update_blnd_gamma;
    res->update_output_gamma                = vpe20_update_output_gamma;
    res->validate_cached_param              = vpe20_validate_cached_param;
    res->fill_alpha_through_luma_cmd_info   = vpe20_fill_alpha_through_luma_cmd_info;
    res->fill_non_performance_mode_cmd_info = vpe20_fill_non_performance_mode_cmd_info;
    res->fill_performance_mode_cmd_info     = vpe20_fill_performance_mode_cmd_info;
    res->fill_blending_cmd_info             = vpe20_fill_blending_cmd_info;
    res->get_num_pipes_available            = vpe20_get_num_pipes_available;
    res->set_frod_output_viewport           = vpe20_set_frod_output_viewport;
    res->check_alpha_fill_support           = vpe10_check_alpha_fill_support;
    res->reset_pipes                        = vpe20_reset_pipes;
    res->populate_frod_param                = vpe20_populate_frod_param;
    res->check_lut3d_compound               = vpe20_check_lut3d_compound;
    res->set_lls_pref                       = vpe20_set_lls_pref;
    res->program_fastload = vpe20_program_3dlut_fl;
    res->calculate_shaper = vpe10_calculate_shaper;
    res->set_dst_cmd_info_scaler        = vpe20_set_dst_cmd_info_scaler;
    res->update_opp_adjust_and_boundary = vpe20_update_opp_adjust_and_boundary;

    return VPE_STATUS_OK;

err:
    vpe20_destroy_resource(vpe_priv, res);
    return VPE_STATUS_ERROR;
}

void vpe20_calculate_dst_viewport_and_active(
    struct segment_ctx *segment_ctx, uint32_t max_seg_width)
{
    struct scaler_data       *data          = &segment_ctx->scaler_data;
    struct stream_ctx        *stream_ctx    = segment_ctx->stream_ctx;
    struct vpe_priv          *vpe_priv      = stream_ctx->vpe_priv;
    struct vpe_rect          *dst_rect      = &stream_ctx->stream.scaling_info.dst_rect;
    struct vpe_rect          *target_rect   = &vpe_priv->output_ctx.target_rect;
    struct fmt_boundary_mode *boundary_mode = &segment_ctx->boundary_mode;

    data->dst_viewport.x     = data->recout.x + dst_rect->x;
    data->dst_viewport.width = data->recout.width;

    // dst viewport is used by vpec, which see no boundary extra pixels as opp drops them
    // remove extra pixels here if exists
    if ((boundary_mode->left == FMT_SUBSAMPLING_BOUNDARY_EXTRA) ||
        (boundary_mode->right == FMT_SUBSAMPLING_BOUNDARY_EXTRA)
    ) {
        data->dst_viewport.x -= segment_ctx->opp_recout_adjust.x;
        data->dst_viewport.width -= segment_ctx->opp_recout_adjust.width;
    }

    // 1st stream will cover the background
    // extends the v_active to cover the full target_rect's height
    if (should_stream_generate_background(segment_ctx->stream_ctx)) {
        data->recout.x       = 0;
        data->recout.y       = dst_rect->y >= target_rect->y ? dst_rect->y - target_rect->y : 0;
        data->dst_viewport.y = target_rect->y;
        data->dst_viewport.height = target_rect->height;

        if (!stream_ctx->flip_horizonal_output) {
            /* first segment :
             * if the dst_viewport.width is not 1024,
             * and we need background on the left, extend the active to cover as much as it can
             */
            if (segment_ctx->segment_idx == 0) {
                uint32_t remain_gap = min(max_seg_width - data->dst_viewport.width,
                    (uint32_t)(data->dst_viewport.x - target_rect->x));
                data->recout.x      = (int32_t)remain_gap;

                data->dst_viewport.x -= (int32_t)remain_gap;
                data->dst_viewport.width += remain_gap;
            }
            // last segment
            if (segment_ctx->segment_idx == stream_ctx->num_segments - 1) {
                uint32_t remain_gap = min(max_seg_width - data->dst_viewport.width,
                    (uint32_t)((target_rect->x + (int32_t)target_rect->width) -
                        (data->dst_viewport.x + (int32_t)data->dst_viewport.width)));

                data->dst_viewport.width += remain_gap;
            }
        }
    } else {
        data->dst_viewport.y      = data->recout.y + dst_rect->y;
        data->dst_viewport.height = data->recout.height;
        data->recout.y            = 0;
        data->recout.x            = 0;
        if ((boundary_mode->top == FMT_SUBSAMPLING_BOUNDARY_EXTRA) ||
            (boundary_mode->bottom == FMT_SUBSAMPLING_BOUNDARY_EXTRA)) {
            data->dst_viewport.y -= segment_ctx->opp_recout_adjust.y;
            data->dst_viewport.height -= segment_ctx->opp_recout_adjust.height;
        }
    }

    vpe20_update_recout_dst_viewport(data, vpe_priv->output_ctx.surface.format,
        &segment_ctx->opp_recout_adjust, (vpe_priv->init.debug.opp_background_gen == 1));
}

bool vpe20_validate_cached_param(struct vpe_priv *vpe_priv, const struct vpe_build_param *param)
{
    uint32_t           i;
    struct output_ctx *output_ctx;

    if (vpe_priv->num_input_streams != param->num_streams &&
        !(vpe_priv->init.debug.bg_color_fill_only == true && vpe_priv->num_streams == 1))
        return false;

    for (i = 0; i < vpe_priv->num_input_streams; i++) {
        struct vpe_stream stream = param->streams[i];

        if (memcmp(&vpe_priv->stream_ctx[i].stream, &stream, sizeof(struct vpe_stream)))
            return false;
    }

    output_ctx = &vpe_priv->output_ctx;
    if (output_ctx->alpha_mode != param->alpha_mode)
        return false;

    if (memcmp(&output_ctx->mpc_bg_color, &param->bg_color, sizeof(struct vpe_color)))
        return false;

    if (memcmp(&output_ctx->opp_bg_color, &param->bg_color, sizeof(struct vpe_color)))
        return false;

    if (memcmp(&output_ctx->target_rect, &param->target_rect, sizeof(struct vpe_rect)))
        return false;

    if (memcmp(&output_ctx->surface, &param->dst_surface, sizeof(struct vpe_surface_info)))
        return false;

    return true;
}

bool vpe20_check_h_mirror_support(bool* input_mirror, bool* output_mirror)
{
    *input_mirror = true;
    *output_mirror = false;
    return true;
}

// To determine where to program OPP alpha.
//    - Per segment in background replacement mode
//    - Once per stream in non-background replacement mode
static bool is_background_replacement_ops(struct vpe_priv *vpe_priv, uint32_t cmd_idx)
{
    struct vpe_cmd_info *cmd_info = vpe_vector_get(vpe_priv->vpe_cmd_vector, cmd_idx);
    VPE_ASSERT(cmd_info);
    if (!cmd_info)
        return false;

    if (cmd_info->ops == VPE_CMD_OPS_ALPHA_THROUGH_LUMA) {
        return true;
    }
    if (cmd_idx != 0) {
        struct vpe_cmd_info *last_cmd_info = vpe_vector_get(vpe_priv->vpe_cmd_vector, cmd_idx - 1);
        VPE_ASSERT(last_cmd_info);
        if (!last_cmd_info)
            return false;
        if (last_cmd_info->ops == VPE_CMD_OPS_ALPHA_THROUGH_LUMA)
            return true;
    }
    return false;
}

static bool should_program_backend_config(struct vpe_priv *vpe_priv, struct vpe_cmd_info *cmd_info,
    uint32_t pipe_idx, uint32_t max_opp_pipes)
{
    /* this function only control skip programming or not.
     * i.e. programming is the same.
     * if fundamental programming is different,
     * it should fall into stream op specific programming instead.
     */
    bool res = false;

    if (!cmd_info->frod_param.enable_frod) {
        if (vpe_priv->stream_ctx[cmd_info->inputs[0].stream_idx].mps_parent_stream != NULL) {
            res = true;
        } else
            if (cmd_info->ops == VPE_CMD_OPS_BLENDING) {
            // only pipe0 has blended output
            if (pipe_idx == 0) {
                res = true;
            }
        } else if (pipe_idx < max_opp_pipes) { // this is more of a sanity check only
            // performance mode, need to program both pipes
            res = true;
        }
    } else { // FROD case
        // frod only pipe0 can output data
        res = (pipe_idx == 0);
    }
    return res;
}

int32_t vpe20_program_backend(
    struct vpe_priv *vpe_priv, uint32_t pipe_idx, uint32_t cmd_idx, bool seg_only)
{
    struct output_ctx       *output_ctx   = &vpe_priv->output_ctx;
    struct vpe_surface_info *surface_info = &vpe_priv->output_ctx.surface;
    struct stream_ctx       *stream_ctx   = vpe_priv->stream_ctx;
    struct cdc_be           *cdc_be       = vpe_priv->resource.cdc_be[pipe_idx];
    struct vpe_cmd_info     *cmd_info     = vpe_vector_get(vpe_priv->vpe_cmd_vector, cmd_idx);
    struct opp              *opp          = vpe_priv->resource.opp[pipe_idx];
    struct mpc              *mpc          = vpe_priv->resource.mpc[pipe_idx];

    struct bit_depth_reduction_params         fmt_bit_depth;
    struct clamping_and_pixel_encoding_params clamp_param;
    enum color_depth                          display_color_depth;
    struct opp_pipe_control_params            pipe_ctrl_param;
    bool                                      opp_dig_bypass     = false;
    struct fmt_subsampling_params             subsampling_params = {0};
    struct fmt_control_params                 fmt_ctrl           = {0};

    VPE_ASSERT(cmd_info);
    if (!cmd_info)
        return -1;

    vpe_priv->be_cb_ctx.vpe_priv = vpe_priv;
    config_writer_set_callback(
        &vpe_priv->config_writer, &vpe_priv->be_cb_ctx, vpe_backend_config_callback);

    config_writer_set_type(&vpe_priv->config_writer, CONFIG_TYPE_DIRECT, pipe_idx);

    if (!seg_only) {
        /* start back-end programming that can be shared among segments */
        if (stream_ctx->mps_parent_stream == NULL)
            vpe_priv->be_cb_ctx.share = true;

        cdc_be->funcs->program_global_sync(cdc_be, VPE20_CDC_VUPDATE_OFFSET_DEFAULT,
            VPE20_CDC_VUPDATE_WIDTH_DEFAULT, VPE20_CDC_VREADY_OFFSET_DEFAULT);

        if (should_program_backend_config(
                vpe_priv, cmd_info, pipe_idx, vpe_priv->pub.caps->resource_caps.num_opp)) {
            mpc->funcs->set_output_transfer_func(mpc, output_ctx);
            mpc->funcs->program_mpc_out(mpc, surface_info->format);
            mpc->funcs->program_output_csc(mpc, output_ctx->surface.format, output_ctx->cs, NULL);

            display_color_depth = vpe_get_color_depth(surface_info->format);

            // disable dynamic expansion for now as no use case
            opp->funcs->set_dyn_expansion(opp, false, display_color_depth);

            if (!is_background_replacement_ops(vpe_priv, cmd_idx)) {
                update_pipe_ctrl_param(&pipe_ctrl_param, output_ctx, surface_info, cmd_info);
                opp->funcs->program_pipe_control(opp, &pipe_ctrl_param);
            }

            if (vpe_priv->init.debug.opp_pipe_crc_ctrl)
                opp->funcs->program_pipe_crc(opp, true);
        }

        config_writer_complete(&vpe_priv->config_writer);
    }

    // Segment Specific programming
    vpe_priv->be_cb_ctx.share = false;

    // Segment specific programming that should be skipped for FROD.
    //  In the blending case there is only one output so the entire back end programming is skipped
    if (should_program_backend_config(
            vpe_priv, cmd_info, pipe_idx, vpe_priv->pub.caps->resource_caps.num_opp)) {
        if (!(cmd_info->frod_param.enable_frod && pipe_idx != 0) &&
            vpe_priv->init.debug.opp_background_gen) {
            struct vpe_rect opp_dst_rect = generate_opp_dst_rect(pipe_idx, cmd_info);

            opp->funcs->set_bg(opp, cmd_info->outputs[pipe_idx].dst_viewport, opp_dst_rect,
                surface_info->format, output_ctx->opp_bg_color);
        }

        vpe_build_clamping_params(opp, &clamp_param);
        vpe_resource_build_bit_depth_reduction_params(opp, &fmt_bit_depth);
        // output cositing and boundary mode
        opp->funcs->build_fmt_subsample_params(opp, surface_info->format,
            vpe_priv->init.debug.subsampling_quality,
            (enum chroma_cositing)surface_info->cs.cositing,
            cmd_info->outputs[pipe_idx].boundary_mode, &fmt_ctrl.subsampling_params);

        opp->funcs->program_fmt(opp, &fmt_bit_depth, &fmt_ctrl, &clamp_param);

        if (is_background_replacement_ops(vpe_priv, cmd_idx)) {
            update_pipe_ctrl_param(&pipe_ctrl_param, output_ctx, surface_info, cmd_info);
            opp->funcs->program_pipe_control(opp, &pipe_ctrl_param);
        }
    }

    if (pipe_idx == 0) {
        // frod control has to be programmed here
        // As visual confirm should not have frod/histogram enabled vs the main output may have it
        // enabled.
        cdc_be->funcs->program_cdc_control(
            cdc_be, cmd_info->frod_param.enable_frod, cmd_info->histo_dsets);
        opp->funcs->program_frod(opp, &cmd_info->frod_param);
    }

    cdc_be->funcs->program_p2b_config(cdc_be, surface_info->format, surface_info->swizzle,
        &cmd_info->outputs[pipe_idx].dst_viewport, &cmd_info->outputs[pipe_idx].dst_viewport_c);

    config_writer_complete(&vpe_priv->config_writer);

    return 0;
}

enum vpe_status vpe20_check_bg_color_support(struct vpe_priv* vpe_priv, struct vpe_color* bg_color)
{
    enum vpe_status status = VPE_STATUS_OK;
    struct vpe_color_space *p_cs   = &vpe_priv->output_ctx.surface.cs;

    // Check if output is studio format and RGB values are >= 0.92, return BG color out of range
    if ((p_cs->tf == VPE_TF_PQ) && (p_cs->range == VPE_COLOR_RANGE_STUDIO) &&
        (bg_color->rgba.r > BG_COLOR_STUDIO_PQ_MAX_THRESHOLD ||
            bg_color->rgba.g > BG_COLOR_STUDIO_PQ_MAX_THRESHOLD ||
            bg_color->rgba.b > BG_COLOR_STUDIO_PQ_MAX_THRESHOLD)) {
        return VPE_STATUS_BG_COLOR_OUT_OF_RANGE;
    }

    return status;
}

// To understand the logic for background color conversion,
// please refer to vpe_update_output_gamma_sequence in color.c
void vpe20_bg_color_convert(enum color_space output_cs, struct transfer_func *output_tf,
    enum vpe_surface_pixel_format pixel_format, struct vpe_color *mpc_bg_color,
    struct vpe_color *opp_bg_color, bool enable_3dlut)
{
    // inverse OCSC studio/format conversion and convert from bg input format to in-pipe format
    if (vpe_is_limited_cs(output_cs) || mpc_bg_color->is_ycbcr)
        vpe_bg_format_and_limited_conversion(output_cs, pixel_format, mpc_bg_color);

    if (opp_bg_color != NULL) {
        if ((opp_bg_color->is_ycbcr && !vpe_is_yuv_cs(output_cs)) ||
            (!opp_bg_color->is_ycbcr && vpe_is_yuv_cs(output_cs))) {
            vpe_bg_color_space_conversion(output_cs, opp_bg_color);
        }
    }
    if (output_tf->type != TF_TYPE_BYPASS) {
        // inverse degam
        if (output_tf->tf == TRANSFER_FUNC_PQ2084)
            vpe_bg_degam(output_tf, mpc_bg_color);
        // inverse gamut remap
        if (enable_3dlut && output_cs != COLOR_SPACE_MSREF_SCRGB)
            vpe_bg_inverse_gamut_remap(output_cs, output_tf, mpc_bg_color);
    }
    // for TF_TYPE_BYPASS, bg color should be programmed to mpc as linear
}

void vpe20_destroy_resource(struct vpe_priv *vpe_priv, struct resource *res)
{
    uint32_t i = 0;

    for (i = 0; i < vpe_priv->num_pipe; i++) {
        if (res->cdc_fe[i] != NULL) {
            vpe_free(container_of(res->cdc_fe[i], struct vpe20_cdc_fe, base));
            res->cdc_fe[i] = NULL;
        }

        if (res->dpp[i] != NULL) {
            vpe_free(container_of(res->dpp[i], struct vpe20_dpp, base));
            res->dpp[i] = NULL;
        }

        if (res->mpc[i] != NULL) {
            vpe_free(container_of(res->mpc[i], struct vpe20_mpc, base));
            res->mpc[i] = NULL;
        }
    }

    for (i = 0; i < vpe_priv->pub.caps->resource_caps.num_cdc_be; i++) {
        vpe_free(container_of(res->cdc_be[i], struct vpe20_cdc_be, base));
        res->cdc_be[i] = NULL;
    }

    for (i = 0; i < vpe_priv->pub.caps->resource_caps.num_opp; i++) {
        if (res->opp[i] != NULL) {
            vpe_free(container_of(res->opp[i], struct vpe20_opp, base));
            res->opp[i] = NULL;
        }
    }
}

void vpe20_create_stream_ops_config(struct vpe_priv *vpe_priv, uint32_t pipe_idx,
    uint32_t cmd_input_idx, struct stream_ctx *stream_ctx, struct vpe_cmd_info *cmd_info)
{
    /* put all hw programming that can be shared according to the cmd type within a stream here */
    struct mpcc_blnd_cfg blndcfg  = {0};

    struct dpp          *dpp      = vpe_priv->resource.dpp[pipe_idx];
    struct mpc          *mpc      = vpe_priv->resource.mpc[pipe_idx];
    enum vpe_cmd_type    cmd_type = VPE_CMD_TYPE_COUNT;
    struct vpe_vector   *config_vector;
    struct vpe_cmd_input *cmd_input = &cmd_info->inputs[cmd_input_idx];

    // MPCC programming
    enum mpc_mpccid      mpccid = pipe_idx;
    enum mpc_mux_topsel  topsel;
    enum mpc_mux_outmux  outmux;
    enum mpc_mux_botsel  botsel;
    enum mpc_mux_oppid   oppid;
    enum mpcc_blend_mode blend_mode;

    vpe_priv->fe_cb_ctx.stream_op_sharing = true;
    vpe_priv->fe_cb_ctx.stream_sharing    = false;

    switch (cmd_info->ops) {
    case VPE_CMD_OPS_BG:
        cmd_type = VPE_CMD_TYPE_BG;
        break;
    case VPE_CMD_OPS_COMPOSITING:
        cmd_type = VPE_CMD_TYPE_COMPOSITING;
        break;
    case VPE_CMD_OPS_BLENDING:
        cmd_type = VPE_CMD_TYPE_BLENDING;
        break;
    case VPE_CMD_OPS_BG_VSCF_INPUT:
        cmd_type = VPE_CMD_TYPE_BG_VSCF_INPUT;
        break;
    case VPE_CMD_OPS_BG_VSCF_OUTPUT:
        cmd_type = VPE_CMD_TYPE_BG_VSCF_OUTPUT;
        break;
    case VPE_CMD_OPS_BG_VSCF_PIPE0:
        cmd_type = VPE_CMD_TYPE_BG_VSCF_PIPE0;
        break;
    case VPE_CMD_OPS_BG_VSCF_PIPE1:
        cmd_type = VPE_CMD_TYPE_BG_VSCF_PIPE1;
        break;
    case VPE_CMD_OPS_ALPHA_THROUGH_LUMA:
        cmd_type = VPE_CMD_TYPE_ALPHA_THROUGH_LUMA;
        break;
    default:
        return;
        break;
    }

    // return if already generated
    config_vector = stream_ctx->stream_op_configs[pipe_idx][cmd_type];

    // mps blend can have any stream generate BG, so blend cfg must be programmed every time
    if (config_vector->num_elements && stream_ctx->mps_parent_stream == NULL)
        return;

    vpe_priv->fe_cb_ctx.cmd_type = cmd_type;

    // out mux depends on cmd type (blend vs composition)
    vpe20_build_mpcc_mux_params(vpe_priv, cmd_info->ops, pipe_idx, cmd_info->num_inputs, &topsel,
        &botsel, &outmux, &oppid, &blend_mode);

    mpc->funcs->program_mpcc_mux(mpc, mpccid, topsel, botsel, outmux, oppid);

    dpp->funcs->set_frame_scaler(dpp, &cmd_input->scaler_data);

    if (cmd_info->ops == VPE_CMD_OPS_BG_VSCF_INPUT) {
        blndcfg.bg_color = vpe_get_visual_confirm_color(vpe_priv,
            stream_ctx->stream.surface_info.format, stream_ctx->stream.surface_info.cs,
            vpe_priv->output_ctx.cs, vpe_priv->output_ctx.output_tf,
            vpe_priv->output_ctx.surface.format,
            (stream_ctx->stream.tm_params.UID != 0 || stream_ctx->stream.tm_params.enable_3dlut));
    } else if (cmd_info->ops == VPE_CMD_OPS_BG_VSCF_OUTPUT) {
        blndcfg.bg_color =
            vpe_get_visual_confirm_color(vpe_priv, vpe_priv->output_ctx.surface.format,
                vpe_priv->output_ctx.surface.cs, vpe_priv->output_ctx.cs,
                vpe_priv->output_ctx.output_tf, vpe_priv->output_ctx.surface.format,
                false); // 3DLUT should only affect input visual confirm
    } else if (cmd_info->ops == VPE_CMD_OPS_BG_VSCF_PIPE0) {
        blndcfg.bg_color.is_ycbcr = false;
        blndcfg.bg_color.rgba.r   = 1.0f;
        blndcfg.bg_color.rgba.g   = 1.0f;
        blndcfg.bg_color.rgba.b   = 0.0f;
        blndcfg.bg_color.rgba.a   = 0.0f;
    } else if (cmd_info->ops == VPE_CMD_OPS_BG_VSCF_PIPE1) {
        blndcfg.bg_color.is_ycbcr = false;
        blndcfg.bg_color.rgba.r   = 0.0f;
        blndcfg.bg_color.rgba.g   = 1.0f;
        blndcfg.bg_color.rgba.b   = 1.0f;
        blndcfg.bg_color.rgba.a   = 0.0f;
    } else {
        blndcfg.bg_color = vpe_priv->output_ctx.mpc_bg_color;
    }
    blndcfg.global_gain          = 0xfff;
    blndcfg.pre_multiplied_alpha = false;

    if (cmd_type == VPE_CMD_TYPE_ALPHA_THROUGH_LUMA) {
        if (pipe_idx == 0) { // Alpha plane goes through pipe 1 and blending happens here
            blndcfg.alpha_mode   = MPCC_ALPHA_BLEND_MODE_ALPHA_THROUGH_LUMA;
            blndcfg.global_alpha = 0xfff;
        } else {
            blndcfg.alpha_mode           = MPCC_ALPHA_BLEND_MODE_GLOBAL_ALPHA;
            blndcfg.global_alpha         = 0xfff;
            blndcfg.pre_multiplied_alpha = 1;
        }
    } else if (stream_ctx->stream.blend_info.blending ||
               (stream_ctx->stream_type == VPE_STREAM_TYPE_DESTINATION &&
                   pipe_idx == 0)) { // Top stream as destination means bg replace
        if (stream_ctx->per_pixel_alpha) {
            blndcfg.alpha_mode = MPCC_ALPHA_BLEND_MODE_PER_PIXEL_ALPHA_COMBINED_GLOBAL_GAIN;

            blndcfg.pre_multiplied_alpha = stream_ctx->stream.blend_info.pre_multiplied_alpha;
            if (stream_ctx->stream.blend_info.global_alpha) {
                blndcfg.global_gain =
                    (uint16_t)(stream_ctx->stream.blend_info.global_alpha_value * 0xfff);
            }
        } else {
            blndcfg.alpha_mode = MPCC_ALPHA_BLEND_MODE_GLOBAL_ALPHA;
            if (stream_ctx->stream.blend_info.global_alpha == true) {
                VPE_ASSERT(stream_ctx->stream.blend_info.global_alpha_value <= 1.0f);
                blndcfg.global_alpha =
                    (uint16_t)(stream_ctx->stream.blend_info.global_alpha_value * 0xfff);
            } else {
                // Global alpha not enabled, make top layer opaque
                blndcfg.global_alpha = 0xfff;
            }
        }
    } else {
        blndcfg.alpha_mode   = MPCC_ALPHA_BLEND_MODE_GLOBAL_ALPHA;
        blndcfg.global_alpha = 0xfff;
    }

    if (cmd_type == VPE_CMD_TYPE_BG || cmd_type == VPE_CMD_TYPE_BG_VSCF_INPUT ||
        cmd_type == VPE_CMD_TYPE_BG_VSCF_OUTPUT ||
        (stream_ctx->mps_parent_stream != NULL &&
            cmd_input->scaler_data.recout.width == VPE_MIN_VIEWPORT_SIZE &&
            cmd_input->scaler_data.recout.height == VPE_MIN_VIEWPORT_SIZE)) {
        // for bg commands, make top layer transparent
        // as global alpha only works when global alpha mode, set global alpha mode as well
        blndcfg.global_alpha = 0;
        blndcfg.global_gain  = 0xfff;
        blndcfg.alpha_mode   = MPCC_ALPHA_BLEND_MODE_GLOBAL_ALPHA;
    }

    blndcfg.overlap_only     = false;
    blndcfg.bottom_gain_mode = 0;

    switch (vpe_priv->init.debug.bg_bit_depth) {
    case 8:
        blndcfg.background_color_bpc = 0;
        break;
    case 9:
        blndcfg.background_color_bpc = 1;
        break;
    case 10:
        blndcfg.background_color_bpc = 2;
        break;
    case 11:
        blndcfg.background_color_bpc = 3;
        break;
    case 12:
    default:
        blndcfg.background_color_bpc = 4; // 12 bit. DAL's choice;
        break;
    }

    blndcfg.top_gain            = 0x1f000;
    blndcfg.bottom_inside_gain  = 0x1f000;
    blndcfg.bottom_outside_gain = 0x1f000;
    blndcfg.blend_mode          = blend_mode;

    mpc->funcs->program_mpcc_blending(mpc, pipe_idx, &blndcfg);

    config_writer_complete(&vpe_priv->config_writer);
}

void vpe20_set_lls_pref(struct vpe_priv *vpe_priv, struct spl_in *spl_input,
    enum color_transfer_func tf, enum vpe_surface_pixel_format pixel_format)
{
    if (tf == TRANSFER_FUNC_LINEAR) {
        spl_input->lls_pref = LLS_PREF_YES;
    } else {
        spl_input->lls_pref = LLS_PREF_NO;
    }
}

void vpe20_program_3dlut_fl(struct vpe_priv *vpe_priv, uint32_t cmd_idx)
{
    struct vpe_cmd_info *cmd_info = vpe_vector_get(vpe_priv->vpe_cmd_vector, cmd_idx);
    VPE_ASSERT(cmd_info);
    if (!cmd_info)
        return;

    uint32_t num_3dluts =
        min(vpe_priv->pub.caps->resource_caps.num_mpc_3dlut, cmd_info->num_inputs);
    uint32_t used_3dluts = 0;
    uint32_t pipe_idx    = 0;

    config_writer_set_callback(
        &vpe_priv->config_writer, &vpe_priv->fe_cb_ctx, vpe_frontend_config_callback);

    vpe_priv->fe_cb_ctx.stream_sharing    = false;
    vpe_priv->fe_cb_ctx.stream_op_sharing = false;

    // Program CDC & mpc for 3DLUT FL
    for (pipe_idx = 0; pipe_idx < cmd_info->num_inputs; pipe_idx++) {

        struct stream_ctx *stream_ctx =
            &vpe_priv->stream_ctx[cmd_info->inputs[pipe_idx].stream_idx];
        struct cdc_fe           *cdc_fe       = vpe_priv->resource.cdc_fe[pipe_idx];
        struct mpc              *mpc          = vpe_priv->resource.mpc[pipe_idx];
        struct vpe_surface_info *surface_info = &stream_ctx->stream.surface_info;
        struct vpe_cmd_input    *cmd_input    = &cmd_info->inputs[pipe_idx];
        uint16_t                 lut3d_bias   = 0x0;
        uint16_t                 lut3d_scale  = 0x3C00;

        vpe_priv->fe_cb_ctx.stream_idx = cmd_input->stream_idx;
        vpe_priv->fe_cb_ctx.vpe_priv   = vpe_priv;

        config_writer_set_type(&vpe_priv->config_writer, CONFIG_TYPE_DIRECT, pipe_idx);

        if ((stream_ctx->stream.tm_params.UID != 0 || stream_ctx->stream.tm_params.enable_3dlut) &&
            (stream_ctx->stream.tm_params.lut_type > VPE_LUT_TYPE_CPU) &&
            stream_ctx->lut3d_func->state.bits.is_dma) { // FL enabled

            VPE_ASSERT(used_3dluts < num_3dluts);

            /* Fast Load Programming. Always force LUT_DIM_33 */
            used_3dluts++;
            cdc_fe->funcs->program_3dlut_fl_config(cdc_fe, LUT_DIM_33, stream_ctx->lut3d_func);

            vpe_convert_from_float_to_fp16(stream_ctx->stream.dma_info.lut3d.bias, &lut3d_bias);
            vpe_convert_from_float_to_fp16(stream_ctx->stream.dma_info.lut3d.scale, &lut3d_scale);
            mpc->funcs->update_3dlut_fl_bias_scale(mpc, lut3d_bias, lut3d_scale);

            if (mpc->funcs->program_mpc_3dlut_fl_config) {
                mpc->funcs->program_mpc_3dlut_fl_config(mpc,
                    stream_ctx->lut3d_func->dma_params.layout,
                    stream_ctx->lut3d_func->dma_params.format, true);
            }

            mpc->funcs->program_mpc_3dlut_fl(
                mpc, LUT_DIM_33, stream_ctx->lut3d_func->lut_3d.use_12bits);

            config_writer_complete(&vpe_priv->config_writer);

            // Start 3dlut Config
            config_writer_set_type(&vpe_priv->config_writer, CONFIG_TYPE_3DLUT_FL, pipe_idx);

            config_writer_fill_3dlut_fl_addr(&vpe_priv->config_writer,
                (uint64_t)stream_ctx->stream.dma_info.lut3d.data,
                stream_ctx->lut3d_func->dma_params.addr_mode,
                stream_ctx->stream.dma_info.lut3d.mem_align, LUT_FL_SIZE_33X33X33, false,
                stream_ctx->stream.dma_info.lut3d.tmz);
        } else {
            if (mpc->funcs->program_mpc_3dlut_fl_config != NULL) {
                mpc->funcs->program_mpc_3dlut_fl_config(mpc, VPE_3DLUT_MEM_LAYOUT_DISABLE,
                    VPE_3DLUT_MEM_FORMAT_16161616_UNORM_12MSB, false);
                config_writer_complete(&vpe_priv->config_writer);
            }
        }
    }
}

int32_t vpe20_program_frontend(struct vpe_priv* vpe_priv, uint32_t pipe_idx, uint32_t cmd_idx,
    uint32_t cmd_input_idx, bool seg_only)
{
    struct vpe_cmd_info *cmd_info = vpe_vector_get(vpe_priv->vpe_cmd_vector, cmd_idx);
    VPE_ASSERT(cmd_info);
    if (!cmd_info)
        return -1;

    struct vpe_cmd_input      *cmd_input    = &cmd_info->inputs[cmd_input_idx];
    struct stream_ctx         *stream_ctx   = &vpe_priv->stream_ctx[cmd_input->stream_idx];
    struct output_ctx         *output_ctx   = &vpe_priv->output_ctx;
    struct vpe_surface_info   *surface_info = &stream_ctx->stream.surface_info;
    struct cdc_fe             *cdc_fe       = vpe_priv->resource.cdc_fe[pipe_idx];
    struct dpp                *dpp          = vpe_priv->resource.dpp[pipe_idx];
    struct mpc                *mpc          = vpe_priv->resource.mpc[pipe_idx];
    enum input_csc_select      select       = INPUT_CSC_SELECT_BYPASS;
    uint32_t                   hw_mult      = 0;
    struct custom_float_format fmt;
    struct cnv_keyer_params    keyer_params;
    enum lut3d_type            lut3d_type        = vpe_get_stream_lut3d_type(stream_ctx);
    bool                       is_enabled_precsc = false;

    enum mpc_mpccid      mpccid = pipe_idx;
    enum mpc_mux_topsel  topsel;
    enum mpc_mux_outmux  outmux;
    enum mpc_mux_botsel  botsel;
    enum mpc_mux_oppid   oppid;
    enum mpcc_blend_mode blend_mode;

    vpe_priv->fe_cb_ctx.stream_idx = cmd_input->stream_idx;
    vpe_priv->fe_cb_ctx.vpe_priv = vpe_priv;

    config_writer_set_callback(
        &vpe_priv->config_writer, &vpe_priv->fe_cb_ctx, vpe_frontend_config_callback);

    config_writer_set_type(&vpe_priv->config_writer, CONFIG_TYPE_DIRECT, pipe_idx);

    vpe20_build_mpcc_mux_params(vpe_priv, cmd_info->ops, pipe_idx, cmd_info->num_inputs, &topsel,
        &botsel, &outmux, &oppid, &blend_mode);

    if (!seg_only) {
        /* start front-end programming that can be shared among segments */
        vpe_priv->fe_cb_ctx.stream_sharing = true;

        config_writer_set_type(&vpe_priv->config_writer, CONFIG_TYPE_DIRECT, pipe_idx);

        cdc_fe->funcs->program_surface_config(cdc_fe, surface_info->format,
            stream_ctx->stream.rotation, stream_ctx->stream.horizontal_mirror,
            surface_info->swizzle);
        cdc_fe->funcs->program_crossbar_config(cdc_fe, surface_info->format);

        dpp->funcs->program_cnv(dpp, surface_info->format, vpe_priv->expansion_mode);
        dpp->funcs->build_keyer_params(dpp, stream_ctx, &keyer_params);
        dpp->funcs->program_alpha_keyer(dpp, &keyer_params);

        if (stream_ctx->bias_scale)
            dpp->funcs->program_cnv_bias_scale(dpp, stream_ctx->bias_scale);

        /* If input adjustment exists, program the ICSC with those values. */
        if (stream_ctx->input_cs) {
            if (!is_enabled_precsc)
                select = INPUT_CSC_SELECT_ICSC;
            dpp->funcs->program_post_csc(dpp, stream_ctx->cs, select, stream_ctx->input_cs);
        } else {
            dpp->funcs->program_post_csc(dpp, stream_ctx->cs, select, NULL);
        }
        dpp->funcs->program_input_transfer_func(dpp, stream_ctx->input_tf);

        // RMCM LOCATION MUST BE SET BEFORE PROGRAMMING RMCM COMPONENTS
        // program shaper, 3dlut and 1dlut in MPC for stream before blend
        if (stream_ctx->enable_3dlut) {
            mpc->funcs->attach_3dlut_to_mpc_inst(mpc, pipe_idx);
        }

        if (stream_ctx->stream.hist_params.hist_dsets > 0)
        {
            dpp->funcs->program_histogram(dpp, &stream_ctx->stream.hist_params, stream_ctx->cs);
        }

        // top mux has to be set first before mpc programming
        mpc->funcs->program_mpcc_mux(mpc, mpccid, topsel, botsel, outmux, oppid);
        /** VPE2.0 Gamut Remaps
         *  4 gamut remaps in the pipe available.
         *  1 in RMCM before 3dlut + Shaper. Only 1 RMCM shared for all pipes
         *  2 in MCM (Gamut-First -> BlndGamma -> Gamut Second). Each pipe has an MCM.
         *  1 post blend. Each pipe has one.
         */
        struct colorspace_transform *gamut_matrix_mcm1 = stream_ctx->gamut_remap;
        struct colorspace_transform *gamut_matrix_rmcm = NULL;
        struct vpe_3dlut            *lut3d_func        = NULL;
        struct transfer_func        *func_shaper       = NULL;

        if (stream_ctx->stream.tm_params.enable_3dlut) {
            // RMCM Programming. Only Programmed Once.
            func_shaper       = stream_ctx->in_shaper_func;
            lut3d_func        = stream_ctx->lut3d_func;
            gamut_matrix_rmcm = stream_ctx->gamut_remap;
            gamut_matrix_mcm1 = NULL;

            mpc->funcs->set_gamut_remap2(mpc, gamut_matrix_rmcm, VPE_MPC_RMCM_GAMUT_REMAP);
        }

        // Always Pre-Blend. RMCM (RMCM_GAMUT + 3dLUT + Shaper)
        mpc->funcs->program_movable_cm(mpc, func_shaper, lut3d_func, stream_ctx->blend_tf, false);

        // Program if RMCM is not used
        mpc->funcs->set_gamut_remap2(mpc, gamut_matrix_mcm1, VPE_MPC_MCM_FIRST_GAMUT_REMAP);

        // Always Program Pre-Blend Gamut
        mpc->funcs->set_gamut_remap2(mpc, output_ctx->gamut_remap, VPE_MPC_MCM_SECOND_GAMUT_REMAP);

        // Always Bypass Post-Blend Gamut Remap
        mpc->funcs->set_gamut_remap2(mpc, NULL, VPE_MPC_GAMUT_REMAP);

        // program hdr_mult
        fmt.exponenta_bits = 6;
        fmt.mantissa_bits = 12;
        fmt.sign = true;
        if (stream_ctx->stream.tm_params.UID || stream_ctx->stream.tm_params.enable_3dlut) {
            if (!vpe_convert_to_custom_float_format(
                    stream_ctx->lut3d_func->hdr_multiplier, &fmt, &hw_mult)) {
                VPE_ASSERT(0);
            }
        } else {
            if (!vpe_convert_to_custom_float_format(stream_ctx->white_point_gain, &fmt, &hw_mult)) {
                VPE_ASSERT(0);
            }
        }
        dpp->funcs->set_hdr_multiplier(dpp, hw_mult);

        if (vpe_priv->init.debug.dpp_crc_ctrl)
            dpp->funcs->program_crc(dpp, true);

        if (vpe_priv->init.debug.mpc_crc_ctrl)
            mpc->funcs->program_crc(mpc, true);

        config_writer_complete(&vpe_priv->config_writer);
        // put other hw programming for stream specific that can be shared here
    } else if (stream_ctx->mps_parent_stream != NULL) {
        vpe_priv->fe_cb_ctx.stream_sharing = false;
        mpc->funcs->program_mpcc_mux(mpc, mpccid, topsel, botsel, outmux, oppid);

        config_writer_complete(&vpe_priv->config_writer);
    }

    vpe20_create_stream_ops_config(vpe_priv, pipe_idx, cmd_input_idx, stream_ctx, cmd_info);

    /* start segment specific programming */
    vpe_priv->fe_cb_ctx.stream_sharing    = false;
    vpe_priv->fe_cb_ctx.stream_op_sharing = false;
    vpe_priv->fe_cb_ctx.cmd_type          = VPE_CMD_TYPE_COMPOSITING;

    // Due to MPS algorithm, you may have two streams in a single build command,
    // where one of the streams requires tone mapping and the pipe processing that
    // stream has changed since the previous command. Thus there is a need for per
    // segment RMCM programming.
    // RMCM LOCATION MUST BE SET BEFORE PROGRAMMING RMCM COMPONENTS
    // program shaper, 3dlut and 1dlut in MPC for stream before blend
    // if !seg_only, this would be programmed before
    if (seg_only) {
        if (stream_ctx->enable_3dlut) {
            mpc->funcs->attach_3dlut_to_mpc_inst(mpc, pipe_idx);
            mpc->funcs->shaper_bypass(mpc, false);
        }
    }


    cdc_fe->funcs->program_viewport(
        cdc_fe, &cmd_input->scaler_data.viewport, &cmd_input->scaler_data.viewport_c);

    dpp->funcs->set_segment_scaler(dpp, &cmd_input->scaler_data);

    if (cmd_info->num_inputs > 1) {
        if (pipe_idx < (uint32_t)(cmd_info->num_inputs - 1)) {
            // Need to enable next pipes dpp clocks before starting programming, so enable at
            // end of previous (current) pipe

            // This if statement required to avoid warning compilation error
            if (pipe_idx + 1 < MAX_INPUT_PIPE)
                vpe_priv->resource.dpp[pipe_idx + 1]->funcs->enable_clocks(
                    vpe_priv->resource.dpp[pipe_idx + 1], true);
        }
        if (pipe_idx != 0) {
            // After finishing the pipe programming, we can disable the clock of the current pipe.
            dpp->funcs->enable_clocks(dpp, false);
        }
    }

    config_writer_complete(&vpe_priv->config_writer);

    return 0;
}

enum vpe_status vpe20_populate_cmd_info(struct vpe_priv *vpe_priv)
{
    uint16_t            stream_idx;
    struct stream_ctx  *stream_ctx;
    enum vpe_status     status;
    uint16_t            avail_pipe_count;

    for (stream_idx = 0; stream_idx < (uint16_t)vpe_priv->num_streams; stream_idx++) {
        stream_ctx = &vpe_priv->stream_ctx[stream_idx];
        avail_pipe_count =
            (uint16_t)vpe_priv->resource.get_num_pipes_available(vpe_priv, stream_ctx);
        if (!vpe_should_generate_cmd_info(stream_ctx))
            continue;

        if (stream_ctx->mps_parent_stream != NULL) {
            status = vpe_fill_mps_blend_cmd_info(vpe_priv, stream_ctx->mps_ctx);
            if (status != VPE_STATUS_OK) {
                return status;
            }
        } else if (stream_ctx->stream_type == VPE_STREAM_TYPE_BKGR_ALPHA) {
            // first pass to generate top layer with alpha
            status = vpe_priv->resource.fill_alpha_through_luma_cmd_info(vpe_priv, stream_idx);
            if (status != VPE_STATUS_OK) {
                return status;
            }
            // second pass to blend with new background stream (always 2 after alpha stream)
            status = vpe_priv->resource.fill_blending_cmd_info(vpe_priv,
                VPE_DESTINATION_AS_INPUT_STREAM_INDEX,
                stream_idx + VPE_BKGR_STREAM_BACKGROUND_OFFSET);
            if (status != VPE_STATUS_OK) {
                return status;
            }
            stream_idx += 2; // skip next two streams - bkgr video and background
        }
        else if (avail_pipe_count > 1) {
            status = vpe_priv->resource.fill_performance_mode_cmd_info(
                vpe_priv, stream_idx, avail_pipe_count);
            if (status != VPE_STATUS_OK) {
                return status;
            }
        } else if (stream_idx > 0 && stream_ctx->stream.blend_info.blending) {
            status = vpe_priv->resource.fill_blending_cmd_info(
                vpe_priv, stream_idx, VPE_DESTINATION_AS_INPUT_STREAM_INDEX);
            if (status != VPE_STATUS_OK) {
                return status;
            }
        } else {
            status = vpe_priv->resource.fill_non_performance_mode_cmd_info(vpe_priv, stream_idx);
            if (status != VPE_STATUS_OK) {
                return status;
            }
        }
    }

    return VPE_STATUS_OK;
}

bool vpe20_check_input_format(enum vpe_surface_pixel_format format)
{
    if (vpe_is_32bit_packed_rgb(format))
        return true;

    if (vpe_is_yuv420(format))
        return true;

    if (vpe_is_yuv422(format))
        return true;

    if (vpe_is_yuv444(format))
        return true;

    if (vpe_is_fp16(format))
        return true;

    if (vpe_is_rgb16(format))
        return true;

    if (format == VPE_SURFACE_PIXEL_FORMAT_PLANAR_8bpc_RGB)
        return true;

    return false;
}

bool vpe20_check_output_format(enum vpe_surface_pixel_format format)
{
    if (vpe_is_32bit_packed_rgb(format))
        return true;
    if (vpe_is_fp16(format))
        return true;
    if (vpe_is_yuv420(format) || vpe_is_yuv422(format))
        return true;
    if (vpe_is_rgb16(format))
        return true;
    if (vpe_is_yuv444(format))
        return true;
    if (format == VPE_SURFACE_PIXEL_FORMAT_PLANAR_8bpc_RGB)
        return true;
    return false;
}

bool vpe20_check_output_color_space(
    enum vpe_surface_pixel_format format, const struct vpe_color_space *vcs)
{
    enum color_space         cs;
    enum color_transfer_func tf;

    vpe_color_get_color_space_and_tf(vcs, &cs, &tf);
    if (cs == COLOR_SPACE_UNKNOWN || tf == TRANSFER_FUNC_UNKNOWN)
        return false;

    if (vpe_is_fp16(format) && tf != TRANSFER_FUNC_LINEAR)
        return false;

    return true;
}

enum vpe_status vpe20_set_num_segments(struct vpe_priv *vpe_priv, struct stream_ctx *stream_ctx,
    struct scaler_data *scl_data, struct vpe_rect *src_rect, struct vpe_rect *dst_rect,
    uint32_t *max_seg_width, uint32_t recout_width_alignment)
{
    uint16_t        num_segs;
    uint32_t        aligned_width;
    double          ratio;
    uint16_t        free_pipes;
    struct dpp     *dpp              = vpe_priv->resource.dpp[0];
    const uint32_t  max_lb_size      = dpp->funcs->get_line_buffer_size();
    struct vpe_rect local_src_rect   = *src_rect;
    uint16_t        avail_pipe_count =
        (uint16_t)vpe_priv->resource.get_num_pipes_available(vpe_priv, stream_ctx);
    bool            use_aligned      = (recout_width_alignment != VPE_NO_ALIGNMENT);
    enum vpe_status res              = VPE_STATUS_OK;

    *max_seg_width = min(*max_seg_width, max_lb_size / scl_data->taps.v_taps);

    // The src_rect is segmented horizontally during orthogonal rotation
    // Swap the src_rect's width and height so the src_rect's height can be examined
    // for segmentation in vpe_get_num_segments
    if (stream_ctx->stream.rotation == VPE_ROTATION_ANGLE_90 ||
        stream_ctx->stream.rotation == VPE_ROTATION_ANGLE_270) {
        swap(local_src_rect.width, local_src_rect.height);
    }

    ratio = (double)local_src_rect.width / dst_rect->width;

    if (stream_ctx->mps_parent_stream == NULL) {
        num_segs = vpe_get_num_segments(vpe_priv, &local_src_rect, dst_rect, *max_seg_width);

        if (use_aligned && num_segs > 1) {
            aligned_width = dst_rect->width / num_segs;
            aligned_width = vpe_align_seg(aligned_width, recout_width_alignment);
            if (aligned_width * num_segs != dst_rect->width) {

                // Increase the number of segments if the aligned width is greater than the maximum
                // allowed viewport size. Only happens if the split size is too large.
                if (aligned_width > *max_seg_width ||
                    (uint32_t)(aligned_width * ratio) > *max_seg_width)
                    num_segs++;
            }
        }

        free_pipes = num_segs % avail_pipe_count;
        if (avail_pipe_count > 1 && (free_pipes != 0)) {
            free_pipes = avail_pipe_count - free_pipes;
            // only use the remaining pipes for the segmentation if the segment size is greater
            // or equal to the minimum viewport size when adding the extra segments
            if (local_src_rect.width / (num_segs + free_pipes) >= VPE_MIN_VIEWPORT_SIZE &&
                dst_rect->width / (num_segs + free_pipes) >= VPE_MIN_VIEWPORT_SIZE) {

                num_segs += free_pipes;
            }
        }
        res = vpe_alloc_segment_ctx(vpe_priv, stream_ctx, num_segs);

        if (res == VPE_STATUS_OK) {
            stream_ctx->num_segments = num_segs;
        }
    } else { // mps blend
        num_segs =
            vpe_mps_get_num_segs(vpe_priv, stream_ctx, max_seg_width, recout_width_alignment);

        if (num_segs == 0)
            res = VPE_STATUS_ERROR;

        if (res == VPE_STATUS_OK)
            res = vpe_alloc_segment_ctx(vpe_priv, stream_ctx, num_segs);

        stream_ctx->num_segments = num_segs;
    }

    return res;
}

uint32_t vpe20_get_hw_surface_format(enum vpe_surface_pixel_format format)
{
    uint32_t surf_format = 8;
    switch (format) {
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
        surf_format = 1;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGB565:
        surf_format = 3;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_XRGB8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_XBGR8888:
        surf_format = 8;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBX8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRX8888:
        surf_format = 9;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
        surf_format = 10;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA1010102:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA1010102:
        surf_format = 11;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_AYCrCb8888:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_AYCbCr8888: // use crossbar
        surf_format = 12;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_YCrCbA8888:
        surf_format = 13;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb8888:
        surf_format = 14;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA8888:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrCbYA8888:
        surf_format = 15;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616: /* use crossbar */
        surf_format = 20;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA16161616:
        surf_format = 21;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F: /* use crossbar */
        surf_format = 24;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616F:
        surf_format = 25;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616_UNORM:
        surf_format = 26;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616_UNORM:
        surf_format = 27;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616_SNORM:
        surf_format = 28;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616_SNORM:
        surf_format = 29;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb12121212:
        surf_format = 44; // 12 bit slice MSB
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA12121212:
        surf_format = 46; // 12 bit slice MSB
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCbCr:
        surf_format = 65;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCrCb:
        surf_format = 64;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCbCr:
        surf_format = 67;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCrCb:
        surf_format = 66;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_12bpc_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCrCb:
        surf_format = 68;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_12bpc_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCbCr:
        surf_format = 69;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ALPHA_THRU_LUMA:
        surf_format = 70;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FIX:
        surf_format = 112;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FIX:
        surf_format = 113;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb2101010:
        surf_format = 114;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA1010102: // use crossbar
        surf_format = 115;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGB111110_FLOAT:
        surf_format = 118;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGR101111_FLOAT:
        surf_format = 119;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCrYCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCbYCr: // use crossbar
        surf_format = 72;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_CrYCbY:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_CbYCrY:
        surf_format = 74;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCrYCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCbYCr:
        surf_format = 76;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_CrYCbY:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_CbYCrY:
        surf_format = 78;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCrYCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCbYCr:
        surf_format = 80;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_CrYCbY:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_CbYCrY:
        surf_format = 82;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_R8:
        surf_format = 120;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_R16:
        surf_format = 125;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_8bpc_RGB:
        surf_format = 265;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_8bpc_YCbCr:
        surf_format = 269;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_RGB:
        surf_format = 277;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_RGB_FLOAT:
        surf_format = 280;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_YCbCr:
        surf_format = 298;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBE:
    default:
        VPE_ASSERT("Invalid pixel format");
        break;
    }
    return surf_format;
}

bool vpe20_get_dcc_compression_output_cap(
    const struct vpe_dcc_surface_param *params, struct vpe_surface_dcc_cap *cap)
{
    cap->capable = false;
    return cap->capable;
}

bool vpe20_get_dcc_compression_input_cap(
    const struct vpe_dcc_surface_param *params, struct vpe_surface_dcc_cap *cap)
{
    if (!vpe_is_dual_plane_format(params->format) && !vpe_is_planar_format(params->format)) {
        cap->capable = true;
        cap->is_internal_dcc = true;
    } else {
        cap->capable = false;
        cap->is_internal_dcc = false;
    }
    return cap->capable;
}

//(BYTES_IN_DWORD * (HEADER_DWORD + (CONFIG_DWORD * NUM_CONFIG_PER_PIPE * NUM_PIPE))
// WORST_CASE_ALIGNMENT PER CONFIG IS 60 BYTES
#define VPE20_GENERAL_VPE_DESC_SIZE                288   // 4 * (4 + (2 * MAX_NUM_SAVED_CONFIG * 2))
#define VPE20_GENERAL_EMB_USAGE_FRAME_SHARED       6400  // 4876(max recorded) + round up margin
#define VPE20_GENERAL_EMB_USAGE_3DLUT_FRAME_SHARED 40960 // currently max 35192 is recorded
#define VPE20_GENERAL_EMB_USAGE_BG_SHARED          4000
#define VPE20_GENERAL_EMB_USAGE_SEG_NON_SHARED     6400 // 3820 (max recorded) + round up margin

void vpe20_get_bufs_req(struct vpe_priv *vpe_priv, struct vpe_bufs_req *req)
{
    uint32_t             i;
    struct vpe_cmd_info *cmd_info;
    uint32_t             stream_idx                 = 0xFFFFFFFF;
    uint64_t             emb_req                    = 0;
    bool                 have_visual_confirm_input  = false;
    bool                 have_visual_confirm_output = false;

    req->cmd_buf_size = 0;
    req->emb_buf_size = 0;

    for (i = 0; i < vpe_priv->vpe_cmd_vector->num_elements; i++) {
        uint32_t per_pipe_size = 0;

        cmd_info = vpe_vector_get(vpe_priv->vpe_cmd_vector, i);
        VPE_ASSERT(cmd_info);
        if (!cmd_info)
            continue;

        // each cmd consumes one VPE descriptor
        req->cmd_buf_size += VPE20_GENERAL_VPE_DESC_SIZE;

        // if a command represents the first segment of a stream,
        // total amount of config sizes is added, but for other segments
        // just the segment specific config size is added
        switch (cmd_info->ops) {
        case VPE_CMD_OPS_COMPOSITING:
        case VPE_CMD_OPS_BLENDING:
        case VPE_CMD_OPS_ALPHA_THROUGH_LUMA:
            // embedded buffer only bigger size if DIRECT CONFIG is used
            if (stream_idx != cmd_info->inputs[0].stream_idx) {

                per_pipe_size += (cmd_info->lut3d_type == LUT3D_TYPE_CPU)
                                     ? VPE20_GENERAL_EMB_USAGE_3DLUT_FRAME_SHARED
                                     : VPE20_GENERAL_EMB_USAGE_FRAME_SHARED;
                per_pipe_size += VPE20_GENERAL_EMB_USAGE_SEG_NON_SHARED;

                stream_idx = cmd_info->inputs[0].stream_idx;
            } else {
                per_pipe_size += VPE20_GENERAL_EMB_USAGE_SEG_NON_SHARED;
            }

            emb_req += (per_pipe_size * cmd_info->num_inputs);
            break;

        case VPE_CMD_OPS_BG:
            emb_req += (i > 0) ? VPE20_GENERAL_EMB_USAGE_SEG_NON_SHARED
                               : VPE20_GENERAL_EMB_USAGE_BG_SHARED;
            break;
        case VPE_CMD_OPS_BG_VSCF_INPUT:
            emb_req += have_visual_confirm_input ? VPE20_GENERAL_EMB_USAGE_SEG_NON_SHARED
                                                 : VPE20_GENERAL_EMB_USAGE_BG_SHARED;
            have_visual_confirm_input = true;
            break;
        case VPE_CMD_OPS_BG_VSCF_OUTPUT:
            emb_req += have_visual_confirm_output ? VPE20_GENERAL_EMB_USAGE_SEG_NON_SHARED
                                                  : VPE20_GENERAL_EMB_USAGE_BG_SHARED;
            have_visual_confirm_output = true;
            break;
        case VPE_CMD_OPS_BG_VSCF_PIPE0:
        case VPE_CMD_OPS_BG_VSCF_PIPE1:
            emb_req += VPE20_GENERAL_EMB_USAGE_SEG_NON_SHARED + VPE20_GENERAL_EMB_USAGE_BG_SHARED;
            break;
        default:
            VPE_ASSERT(0);
            break;
        }

        req->emb_buf_size += emb_req;
    }

    req->cmd_buf_size += VPE_PREDICATION_CMD_SIZE;
}

enum vpe_status vpe20_check_mirror_rotation_support(const struct vpe_stream *stream)
{
    enum vpe_swizzle_mode_values swizzle_mode;
    enum vpe_scan_direction      scan_dir;

    VPE_ASSERT(stream != NULL);

    swizzle_mode = stream->surface_info.swizzle;
    scan_dir     = vpe_get_scan_direction(
        stream->rotation, stream->horizontal_mirror, stream->vertical_mirror);

    if (swizzle_mode == VPE_SW_LINEAR) {

        if (!vpe_supported_linear_scan_pattern(scan_dir))
            return VPE_STATUS_ROTATION_NOT_SUPPORTED;

        if (stream->rotation == VPE_ROTATION_ANGLE_90 || stream->rotation == VPE_ROTATION_ANGLE_270)
            return VPE_STATUS_ROTATION_NOT_SUPPORTED;

        if (stream->vertical_mirror)
            return VPE_STATUS_MIRROR_NOT_SUPPORTED;
    }

    if (vpe_is_yuv_packed(stream->surface_info.format) &&
        vpe_is_yuv422(stream->surface_info.format)) {

        if (stream->rotation != VPE_ROTATION_ANGLE_0)
            return VPE_STATUS_ROTATION_NOT_SUPPORTED;

        if (stream->vertical_mirror)
            return VPE_STATUS_MIRROR_NOT_SUPPORTED;
    }

    return VPE_STATUS_OK;
}

/* This function generates software points for the blnd gam programming block.
   The logic for the blndgam/ogam programming sequence is a function of:
   1. Output Range (Studio Full)
   2. 3DLUT usage
   3. Output format (HDR SDR)

   SDR Out
      TM Case
         BLNDGAM : NL -> NL*S + B
         OGAM    : Bypass
      Non TM Case
         BLNDGAM : L -> NL*S + B
         OGAM    : Bypass
   HDR Out
      TM Case
         BLNDGAM : NL -> L
         OGAM    : L -> NL
      Non TM Case
         BLNDGAM : Bypass
         OGAM    : L -> NL

*/
enum vpe_status vpe20_update_blnd_gamma(struct vpe_priv *vpe_priv,
    const struct vpe_build_param *param, const struct vpe_stream *stream,
    struct transfer_func *blnd_tf)
{
    struct output_ctx       *output_ctx;
    struct vpe_color_space   tm_out_cs;
    struct fixed31_32        x_scale       = vpe_fixpt_one;
    struct fixed31_32        y_scale       = vpe_fixpt_one;
    struct fixed31_32        y_bias        = vpe_fixpt_zero;
    bool                     can_bypass    = false;
    bool                     lut3d_enabled = false;
    enum color_space         cs            = COLOR_SPACE_2020_RGB_FULLRANGE;
    enum color_transfer_func tf            = TRANSFER_FUNC_LINEAR;
    enum vpe_status          status        = VPE_STATUS_OK;
    const struct vpe_tonemap_params *tm_params     = &stream->tm_params;

    output_ctx = &vpe_priv->output_ctx;
    lut3d_enabled = tm_params->UID != 0 || tm_params->enable_3dlut;

    if (stream->flags.geometric_scaling) {
        vpe_color_update_degamma_tf(vpe_priv, tf, x_scale, y_scale, y_bias, true, blnd_tf);
    } else {
        // If SDR out -> Blend should be NL
        if (!vpe_is_HDR(output_ctx->tf)) {
            if (lut3d_enabled) {
                tf = TRANSFER_FUNC_LINEAR;
            } else {
                tf = output_ctx->tf;
            }
            vpe_color_update_regamma_tf(
                vpe_priv, tf, x_scale, y_scale, y_bias, can_bypass, blnd_tf);
        } else {

            if (lut3d_enabled) {
                vpe_color_build_tm_cs(tm_params, &param->dst_surface, &tm_out_cs);
                vpe_color_get_color_space_and_tf(&tm_out_cs, &cs, &tf);
            } else {
                can_bypass = true;
            }

            vpe_color_update_degamma_tf(vpe_priv, tf, x_scale, y_scale, y_bias, can_bypass, blnd_tf);
        }
    }
    return status;
}

/* This function generates software points for the ogam gamma programming block.
   The logic for the blndgam/ogam programming sequence is a function of:
   1. 3DLUT usage
   2. Output format (HDR SDR)
   SDR Out
      TM Case
         BLNDGAM : Bypass
         OGAM    : Bypass
      Non TM Case
         BLNDGAM : L -> NL
         OGAM    : Bypass
   Full range HDR Out
      TM Case
         BLNDGAM : NL -> L
         OGAM    : L -> NL
      Non TM Case
         BLNDGAM : Bypass
         OGAM    : L -> NL
*/
enum vpe_status vpe20_update_output_gamma(struct vpe_priv *vpe_priv,
    const struct vpe_build_param *param, struct transfer_func *output_tf, bool geometric_scaling)
{
    bool               can_bypass = false;
    struct output_ctx *output_ctx = &vpe_priv->output_ctx;
    enum vpe_status    status     = VPE_STATUS_OK;
    struct fixed31_32  y_scale    = vpe_fixpt_one;

    if (vpe_is_fp16(param->dst_surface.format)) {
        y_scale = vpe_fixpt_mul_int(y_scale, CCCS_NORM);
    }

    if (!geometric_scaling && vpe_is_HDR(output_ctx->tf))
        can_bypass = false;
    else
        can_bypass = true;

    vpe_color_update_regamma_tf(
        vpe_priv, output_ctx->tf, vpe_fixpt_one, y_scale, vpe_fixpt_zero, can_bypass, output_tf);

    return status;
}

static bool needs_segmentation(enum vpe_stream_type type)
{
    switch (type) {
    case VPE_STREAM_TYPE_INPUT:
    case VPE_STREAM_TYPE_BKGR_VIDEO:
    case VPE_STREAM_TYPE_BKGR_ALPHA:
    case VPE_STREAM_TYPE_BKGR_BACKGROUND:
        return true;
    default:
        return false;
    }
}

static void update_spl_recout_width_align(struct basic_in *basic_in, uint32_t num_segs,
    struct vpe_rect *dst_rect, uint32_t recout_width_alignment)
{
    // If the alignment is needed, use the alignment value to calculate the number of segments
    if (recout_width_alignment != VPE_NO_ALIGNMENT) {
        uint32_t aligned_width;
        aligned_width = (dst_rect->width + num_segs - 1) / num_segs;
        aligned_width = vpe_align_seg(aligned_width, recout_width_alignment);

        basic_in->num_h_slices_recout_width_align.use_recout_width_aligned = true;
        basic_in->num_h_slices_recout_width_align.num_slices_recout_width.mpc_recout_width_align =
            aligned_width;
    } else {
        basic_in->num_h_slices_recout_width_align.use_recout_width_aligned = false;
        basic_in->num_h_slices_recout_width_align.num_slices_recout_width.mpc_num_h_slices =
            num_segs;
    }
}

uint16_t vpe20_get_bg_stream_idx(struct vpe_priv *vpe_priv)
{
    // For BGR we insert background in later stream
    if (vpe_priv->stream_ctx[0].stream.flags.is_alpha_plane == false)
        return 0;
    else
        return VPE_BKGR_STREAM_BACKGROUND_OFFSET;
}

static bool rect_contained_in_rect(struct vpe_rect inside_rect, struct vpe_rect containing_rect)
{
    return !(inside_rect.x < containing_rect.x || inside_rect.y < containing_rect.y ||
             inside_rect.x + inside_rect.width > containing_rect.x + containing_rect.width ||
             inside_rect.y + inside_rect.height > containing_rect.y + containing_rect.height);
}

static enum vpe_status segment_stream(struct vpe_priv *vpe_priv, struct stream_ctx *stream_ctx,
    struct vpe_rect target_rect, bool dst_subsampled, bool enable_frod, uint32_t recout_alignment)
{
    uint16_t                      seg_idx;
    struct segment_ctx *segment_ctx;
    struct scaler_data  scl_data;
    struct vpe_rect     src_rect;
    struct vpe_rect     dst_rect;
    enum vpe_surface_pixel_format pixel_format;
    struct spl_in  *spl_input;
    struct spl_out *spl_output;
    uint32_t        max_seg_width;
    struct dpp     *dpp              = vpe_priv->resource.dpp[0];
    enum vpe_status res              = VPE_STATUS_OK;
    bool            skip_program_scl = false;

    if (!needs_segmentation(stream_ctx->stream_type))
        return res;

    spl_input  = &stream_ctx->spl_input;
    spl_output = &stream_ctx->spl_output;
    src_rect         = stream_ctx->stream.scaling_info.src_rect;
    dst_rect         = stream_ctx->stream.scaling_info.dst_rect;
    stream_ctx->scan = vpe_get_scan_direction(stream_ctx->stream.rotation,
        stream_ctx->stream.horizontal_mirror, stream_ctx->stream.vertical_mirror);
    pixel_format     = stream_ctx->stream.surface_info.format;
    max_seg_width    = vpe_priv->resource.get_max_seg_width(
        &vpe_priv->output_ctx, stream_ctx->stream.surface_info.format, stream_ctx->scan);

    if (dst_rect.width == 0 && dst_rect.height == 0) {
        stream_ctx->num_segments = 0;
        return VPE_STATUS_OK;
    }

    if (!vpe_is_valid_vp(&src_rect, &dst_rect))
        return VPE_STATUS_VIEWPORT_SIZE_NOT_SUPPORTED;
    if (enable_frod) {
        if ((src_rect.width < (VPE_MIN_VIEWPORT_SIZE * MAX_FROD_VIEWPORT_DIVIDER)) ||
            (src_rect.height < (VPE_MIN_VIEWPORT_SIZE * MAX_FROD_VIEWPORT_DIVIDER)) ||
            (dst_rect.width < (VPE_MIN_VIEWPORT_SIZE * MAX_FROD_VIEWPORT_DIVIDER)) ||
            (dst_rect.height < (VPE_MIN_VIEWPORT_SIZE * MAX_FROD_VIEWPORT_DIVIDER))) {
            return VPE_STATUS_VIEWPORT_SIZE_NOT_SUPPORTED;
        }
    }
    vpe_clip_stream(&src_rect, &dst_rect, &target_rect);

    if (vpe_is_zero_rect(&src_rect) || vpe_is_zero_rect(&dst_rect)) {
        vpe_log("calculate_segments: after clipping, src or dst rect contains no area. Skip "
                "this stream.\n");
        stream_ctx->num_segments = 0;
        return res;
    }

    if (!vpe_is_valid_vp(&src_rect, &dst_rect))
        return VPE_STATUS_VIEWPORT_SIZE_NOT_SUPPORTED;

    if (!vpe_is_scaling_factor_supported(
            vpe_priv, &src_rect, &dst_rect, stream_ctx->stream.rotation))
        return VPE_STATUS_SCALING_RATIO_NOT_SUPPORTED;

    // init scaler data to get the number of taps for calculating the number of segments and
    // maximum viewport
    if (!init_scaler_data(
            stream_ctx, spl_input, spl_output, &scl_data, &vpe_priv->output_ctx, max_seg_width))
        return VPE_STATUS_SCALING_RATIO_NOT_SUPPORTED;
    if (stream_ctx->mps_parent_stream == NULL && stream_ctx->stream_idx == 0 &&
        vpe_priv->num_input_streams > 1) {
        struct stream_ctx *mps_stream_ctx[2] = {&vpe_priv->stream_ctx[0], &vpe_priv->stream_ctx[1]};

        if (vpe_is_mps_possible(vpe_priv, mps_stream_ctx, 2, recout_alignment))
            if (vpe_init_mps_ctx(vpe_priv, mps_stream_ctx, 2) != VPE_STATUS_OK)
                return VPE_STATUS_ERROR;
    }

    res = vpe_priv->resource.set_num_segments(vpe_priv, stream_ctx, &scl_data,
        &stream_ctx->stream.scaling_info.src_rect, &stream_ctx->stream.scaling_info.dst_rect,
        &max_seg_width, recout_alignment);
    if (res != VPE_STATUS_OK)
        return res;
    if (stream_ctx->mps_parent_stream == NULL)
        update_spl_recout_width_align(
            &spl_input->basic_in, stream_ctx->num_segments, &dst_rect, recout_alignment);
    else
        update_spl_recout_width_align(
            &spl_input->basic_in, stream_ctx->num_segments, &dst_rect, VPE_NO_ALIGNMENT);
    for (seg_idx = 0; seg_idx < stream_ctx->num_segments; seg_idx++) {

        segment_ctx                                 = &stream_ctx->segment_ctx[seg_idx];
        segment_ctx->segment_idx                    = seg_idx;
        segment_ctx->stream_ctx                     = stream_ctx;
        segment_ctx->scaler_data.format             = stream_ctx->stream.surface_info.format;
        segment_ctx->scaler_data.lb_params.alpha_en = stream_ctx->per_pixel_alpha;

        // SPL Calculation
        spl_output->dscl_prog_data            = &segment_ctx->scaler_data.dscl_prog_data;
        spl_input->basic_in.mpc_h_slice_index = seg_idx;
        vpe_priv->resource.update_opp_adjust_and_boundary(stream_ctx, seg_idx, dst_subsampled,
            &src_rect, &dst_rect, &vpe_priv->output_ctx, &spl_input->basic_in.opp_recout_adjust);

        if (stream_ctx->mps_parent_stream == NULL) {
            spl_input->basic_in.custom_width = 0;
            spl_input->basic_in.custom_x     = 0;
        } else {
            // For mps we need to pass custom start_x and width in SPL in
            struct vpe_mps_ctx *mps_ctx = stream_ctx->mps_parent_stream->mps_ctx;
            for (int i = 0; i < mps_ctx->num_streams; i++) {
                if (mps_ctx->stream_idx[i] == stream_ctx->stream_idx) {
                    VPE_ASSERT(
                        mps_ctx->segment_widths[i]->num_elements == stream_ctx->num_segments);

                    int32_t rect_x   = stream_ctx->stream.scaling_info.dst_rect.x;
                    int32_t target_x = vpe_priv->output_ctx.target_rect.x;

                    int start_x = rect_x > target_x ? 0 : target_x - rect_x;
                    int width   = 0;
                    for (int j = 0; j <= seg_idx; j++) {
                        start_x += width;
                        width = *(uint32_t *)vpe_vector_get(mps_ctx->segment_widths[i], j);
                    }

                    spl_input->basic_in.custom_width = width;
                    spl_input->basic_in.custom_x     = start_x;
                    break;
                }
            }
        }
        if (!SPL_NAMESPACE(spl_calculate_scaler_params(spl_input, spl_output)))
            return VPE_STATUS_SCALER_NOT_SET;

        if ((!skip_program_scl) && vpe_is_yuv422(pixel_format) && vpe_is_yuv_packed(pixel_format))
            adjust_packed_422_scaler_params(spl_output, stream_ctx->stream.horizontal_mirror);

        vpe_spl_scl_to_vpe_scl(spl_output, &segment_ctx->scaler_data);

        // Update vpe values based on SPL_out
        vpe_priv->resource.calculate_dst_viewport_and_active(segment_ctx, max_seg_width);
    }
    return res;
}

enum vpe_status vpe20_calculate_segments(
    struct vpe_priv *vpe_priv, const struct vpe_build_param *params)
{
    struct vpe_rect *gaps;
    uint16_t         gaps_cnt, max_gaps;
    uint16_t         stream_idx;
    uint32_t         max_seg_width    = vpe_priv->pub.caps->plane_caps.max_viewport_width;
    bool             dst_subsampled   = vpe_is_subsampled_format(params->dst_surface.format);
    uint32_t         recout_alignment = vpe_get_recout_width_alignment(params);
    enum vpe_status  res              = VPE_STATUS_OK;

    for (stream_idx = 0; stream_idx < (uint16_t)params->num_streams; stream_idx++) {
        res = segment_stream(vpe_priv, &vpe_priv->stream_ctx[stream_idx], params->target_rect,
            dst_subsampled, params->frod_param.enable_frod, recout_alignment);

        if (res != VPE_STATUS_OK)
            break;
    }

    /* If the stream width is less than max_seg_width - 1024, and it
    * lies inside a max_seg_width window of the background, vpe needs
    * an extra bg segment to store that.
       1    2  3  4   5
    |....|....|.**.|....|
    |....|....|.**.|....|
    |....|....|.**.|....|

     (*: stream
      .: background
      |: 1k separator)

    */

    if (res == VPE_STATUS_OK) {
        // mps does its own bg generation
        if (vpe_priv->stream_ctx[0].mps_parent_stream == NULL) {
            // Background doesn't need rotation, so we can use max_viewport_width
            max_seg_width = vpe_priv->pub.caps->plane_caps.max_viewport_width;
            max_gaps =
                (uint16_t)(max((int_divide_with_ceil(params->target_rect.width, max_seg_width)),
                               1) +
                           1);

            gaps = vpe_zalloc(sizeof(struct vpe_rect) * max_gaps);
            if (!gaps)
                return VPE_STATUS_NO_MEMORY;

            gaps_cnt = vpe_priv->resource.find_bg_gaps(
                vpe_priv, &(params->target_rect), gaps, recout_alignment, max_gaps);
            if (gaps_cnt > 0)
                vpe_priv->resource.create_bg_segments(vpe_priv, gaps, gaps_cnt, VPE_CMD_OPS_BG);

            if (gaps != NULL) {
                vpe_free(gaps);
                gaps = NULL;
            }
        }

        vpe_handle_output_h_mirror(vpe_priv);

        res = vpe_priv->resource.populate_cmd_info(vpe_priv);
    }

    if (res == VPE_STATUS_OK)
        res = vpe_create_visual_confirm_segs(vpe_priv, params, max_seg_width);

    return res;
}

void vpe20_fill_bg_cmd_scaler_data(
    struct stream_ctx *stream_ctx, struct vpe_rect *dst_viewport, struct scaler_data *scaler_data)
{
    struct vpe_priv *vpe_priv  = stream_ctx->vpe_priv;
    int32_t          vp_x      = stream_ctx->stream.scaling_info.src_rect.x;
    int32_t          vp_y      = stream_ctx->stream.scaling_info.src_rect.y;
    uint16_t         src_h_div = vpe_is_yuv420(stream_ctx->stream.surface_info.format) ? 2 : 1;
    uint16_t         src_v_div = vpe_is_yuv420(stream_ctx->stream.surface_info.format) ? 2 : 1;
    uint16_t         dst_h_div = vpe_is_yuv420(vpe_priv->output_ctx.surface.format) ? 2 : 1;
    uint16_t         dst_v_div = vpe_is_yuv420(vpe_priv->output_ctx.surface.format) ? 2 : 1;

    if (vpe_is_yuv422(stream_ctx->stream.surface_info.format))
        src_h_div = 2;
    if (vpe_is_yuv422(vpe_priv->output_ctx.surface.format))
        dst_h_div = 2;

    /* format */
    scaler_data->format             = stream_ctx->stream.surface_info.format;
    scaler_data->lb_params.alpha_en = stream_ctx->per_pixel_alpha;

    /* recout */

    scaler_data->recout.x      = 0;
    scaler_data->recout.y      = 0;
    scaler_data->recout.height = VPE_MIN_VIEWPORT_SIZE;
    scaler_data->recout.width  = VPE_MIN_VIEWPORT_SIZE;

    /* ratios */
    scaler_data->ratios.horz = vpe_fixpt_one;
    scaler_data->ratios.vert = vpe_fixpt_one;

    if (vpe_is_yuv420(scaler_data->format)) {
        scaler_data->ratios.horz_c = vpe_fixpt_from_fraction(1, 2);
        scaler_data->ratios.vert_c = vpe_fixpt_from_fraction(1, 2);
    }
    else if (vpe_is_yuv422(scaler_data->format)) {
        scaler_data->ratios.horz_c = vpe_fixpt_from_fraction(1, 2);
        scaler_data->ratios.vert_c = vpe_fixpt_one;
    }
    else {
        scaler_data->ratios.horz_c = vpe_fixpt_one;
        scaler_data->ratios.vert_c = vpe_fixpt_one;
    }

    /* Active region */
    scaler_data->h_active = dst_viewport->width;
    scaler_data->v_active = dst_viewport->height;

    /* viewport */

    scaler_data->viewport.x      = vp_x;
    scaler_data->viewport.y      = vp_y;
    scaler_data->viewport.width  = VPE_MIN_VIEWPORT_SIZE;
    scaler_data->viewport.height = VPE_MIN_VIEWPORT_SIZE;

    scaler_data->viewport_c.x      = scaler_data->viewport.x / src_h_div;
    scaler_data->viewport_c.y      = scaler_data->viewport.y / src_v_div;
    scaler_data->viewport_c.width  = scaler_data->viewport.width / src_h_div;
    scaler_data->viewport_c.height = scaler_data->viewport.height / src_v_div;

    /* destination viewport */
    scaler_data->dst_viewport = *dst_viewport;

    scaler_data->dst_viewport_c.x      = scaler_data->dst_viewport.x / dst_h_div;
    scaler_data->dst_viewport_c.y      = scaler_data->dst_viewport.y / dst_v_div;
    scaler_data->dst_viewport_c.width  = scaler_data->dst_viewport.width / dst_h_div;
    scaler_data->dst_viewport_c.height = scaler_data->dst_viewport.height / dst_v_div;

    /* taps and inits */
    scaler_data->taps.h_taps = scaler_data->taps.v_taps = 1;

    if (scaler_data->ratios.horz_c.value == vpe_fixpt_one.value)
        scaler_data->taps.h_taps_c = 1;
    else
        scaler_data->taps.h_taps_c = 2;

    if (scaler_data->ratios.vert_c.value == vpe_fixpt_one.value)
        scaler_data->taps.v_taps_c = 1;
    else
        scaler_data->taps.v_taps_c = 2;

    scaler_data->inits.h = vpe_fixpt_div_int(
        vpe_fixpt_add_int(scaler_data->ratios.horz, (int)(scaler_data->taps.h_taps + 1)), 2);
    scaler_data->inits.v = vpe_fixpt_div_int(
        vpe_fixpt_add_int(scaler_data->ratios.vert, (int)(scaler_data->taps.v_taps + 1)), 2);
    scaler_data->inits.h_c = vpe_fixpt_div_int(
        vpe_fixpt_add_int(scaler_data->ratios.horz_c, (int)(scaler_data->taps.h_taps_c + 1)), 2);
    scaler_data->inits.v_c = vpe_fixpt_div_int(
        vpe_fixpt_add_int(scaler_data->ratios.vert_c, (int)(scaler_data->taps.v_taps_c + 1)), 2);

    /** Translate scaling data to dscl_prog_data.The dscl mode sets in here since the hardcoded
     * info did not go through the regular scaler process.
     */
    scaler_data->dscl_prog_data.dscl_mode = vpe10_dpp_dscl_get_dscl_mode(scaler_data);
    vpe_scl_to_dscl_bg(scaler_data);

    if (vpe_priv->init.debug.opp_background_gen == 1) {
        // spl sets bg segments mpc_size to dst_viewport, but bg is generated in OPP so set MPC 0
        scaler_data->dscl_prog_data.mpc_size.width  = 0;
        scaler_data->dscl_prog_data.mpc_size.height = 0;
    }
}

void vpe20_create_bg_segments(
    struct vpe_priv *vpe_priv, struct vpe_rect *gaps, uint16_t gaps_cnt, enum vpe_cmd_ops ops)
{
    uint16_t            gap_index;
    uint16_t            bg_index    = vpe_priv->resource.get_bg_stream_idx(vpe_priv);
    struct vpe_cmd_info cmd_info    = {0};
    struct scaler_data *scaler_data = &(cmd_info.inputs[bg_index].scaler_data);
    struct stream_ctx  *stream_ctx  = &(vpe_priv->stream_ctx[bg_index]);

    for (gap_index = 0; gap_index < gaps_cnt; gap_index++) {

        VPE_ASSERT(gaps_cnt - gap_index - 1 <= (uint16_t)0xF);

        // generate the scaler data for this bg gap
        vpe20_fill_bg_cmd_scaler_data(stream_ctx, &gaps[gap_index], scaler_data);

        // background takes stream_idx 0 as its input
        cmd_info.inputs[0].stream_idx      = 0;
        cmd_info.num_outputs               = 1;
        cmd_info.outputs[0].dst_viewport   = scaler_data->dst_viewport;
        cmd_info.outputs[0].dst_viewport_c = scaler_data->dst_viewport_c;

        // make sure frod/histogram are disabled for bg segments
        cmd_info.frod_param.enable_frod = 0;
        memset(&cmd_info.histo_dsets, 0, sizeof(cmd_info.histo_dsets));

        cmd_info.num_inputs = 1;
        cmd_info.ops        = ops;
        cmd_info.cd         = (uint16_t)(gaps_cnt - gap_index - 1);
        cmd_info.lut3d_type = LUT3D_TYPE_NONE; // currently only support frontend tm
        vpe_vector_push(vpe_priv->vpe_cmd_vector, &cmd_info);
    }
}

uint32_t vpe20_get_num_pipes_available(struct vpe_priv *vpe_priv, struct stream_ctx *stream_ctx)
{
    uint32_t pipe_count = 1;

    if (vpe_priv->init.debug.disable_performance_mode)
        return pipe_count;

    if (vpe_priv->output_ctx.frod_param.enable_frod)
        return pipe_count;

    if (stream_ctx->stream_type == VPE_STREAM_TYPE_BKGR_ALPHA ||
        stream_ctx->stream_type == VPE_STREAM_TYPE_BKGR_VIDEO)
        return pipe_count;

    if (stream_ctx->stream.blend_info.blending && stream_ctx->stream_idx != 0)
        return pipe_count;
    /* if 3D LUT is enabled and 3D LUT mpc does not equal to number of pipes */
    if (stream_ctx->stream.tm_params.enable_3dlut || stream_ctx->stream.tm_params.UID != 0) {
        pipe_count = vpe_priv->pub.caps->resource_caps.num_mpc_3dlut;
    } else {
        pipe_count = vpe_priv->pub.caps->resource_caps.num_dpp;
    }

    return pipe_count;
}

void vpe20_set_frod_output_viewport(struct vpe_cmd_output *dst_output,
    struct vpe_cmd_output *src_output, uint32_t viewport_divider,
    enum vpe_surface_pixel_format format)
{
    if ((viewport_divider > 0) && (dst_output != NULL) && (src_output != NULL)) {
        dst_output->dst_viewport.x =
            (int32_t)int_divide_with_ceil(src_output->dst_viewport.x, viewport_divider);
        dst_output->dst_viewport.y =
            (int32_t)int_divide_with_ceil(src_output->dst_viewport.y, viewport_divider);
        dst_output->dst_viewport.width =
            (int32_t)int_divide_with_ceil(src_output->dst_viewport.width, viewport_divider);
        dst_output->dst_viewport.height =
            (int32_t)int_divide_with_ceil(src_output->dst_viewport.height, viewport_divider);
        dst_output->dst_viewport_c.x =
            (int32_t)int_divide_with_ceil(src_output->dst_viewport_c.x, viewport_divider);
        dst_output->dst_viewport_c.y =
            (int32_t)int_divide_with_ceil(src_output->dst_viewport_c.y, viewport_divider);
        dst_output->dst_viewport_c.width =
            (int32_t)int_divide_with_ceil(src_output->dst_viewport_c.width, viewport_divider);
        dst_output->dst_viewport_c.height =
            (int32_t)int_divide_with_ceil(src_output->dst_viewport_c.height, viewport_divider);
        if (vpe_is_yuv422(format)) {
            if (dst_output->dst_viewport.width & 1) {
                dst_output->dst_viewport.width++;
            }
        } else if (vpe_is_yuv(format) && vpe_is_dual_plane_format(format)) {
            if (dst_output->dst_viewport.width & 1) {
                dst_output->dst_viewport.width++;
                dst_output->dst_viewport_c.width = dst_output->dst_viewport.width / 2;
            }
            if (dst_output->dst_viewport.height & 1) {
                dst_output->dst_viewport.height++;
                dst_output->dst_viewport_c.height = dst_output->dst_viewport.height / 2;
            }
        }
    }
}

/*
 * Because we only touch the back end of the output pipes associated with the current command,
 * and the number of output pipes can vary between commands, there is a need to reset certain
 * registers in the back end that may have been leftover from a previous job. Specfically the
 * OPP BG Gen registers need to be reset because this block can generate its own signal. This
 * is not the case for the other back end blocks, so we don't need to reset them.
 */
void vpe20_reset_pipes(struct vpe_priv *vpe_priv)
{
    struct mpc      *mpc;
    struct opp      *opp;
    struct vpe_rect  zero_dim_rect = {0, 0, 0, 0};
    struct vpe_color bg_color      = {.is_ycbcr = false, .rgba = {0, 0, 0}};

    // Reset necessary frontend registers
    vpe_priv->fe_cb_ctx.vpe_priv          = vpe_priv;
    vpe_priv->fe_cb_ctx.stream_sharing    = false;
    vpe_priv->fe_cb_ctx.stream_op_sharing = false;
    config_writer_set_callback(
        &vpe_priv->config_writer, &vpe_priv->fe_cb_ctx, vpe_frontend_config_callback);

    for (uint32_t rmcm_idx = 0; rmcm_idx < vpe_priv->pub.caps->resource_caps.num_mpc_3dlut;
         rmcm_idx++) {
        config_writer_set_type(&vpe_priv->config_writer, CONFIG_TYPE_DIRECT, rmcm_idx);
        mpc = vpe_priv->resource.mpc[rmcm_idx];
        mpc->funcs->attach_3dlut_to_mpc_inst(mpc, RMCM_MPCC_DISCONNECTED);
        mpc->funcs->shaper_bypass(mpc, true);
        mpc->funcs->program_3dlut(mpc, NULL);
    }

    config_writer_complete(&vpe_priv->config_writer);

    // Reset necessary backend registers
    vpe_priv->be_cb_ctx.vpe_priv = vpe_priv;
    vpe_priv->be_cb_ctx.share    = false;
    config_writer_set_callback(
        &vpe_priv->config_writer, &vpe_priv->be_cb_ctx, vpe_backend_config_callback);

    for (uint32_t pipe_idx = 0; pipe_idx < vpe_priv->pub.caps->resource_caps.num_opp; pipe_idx++) {
        config_writer_set_type(&vpe_priv->config_writer, CONFIG_TYPE_DIRECT, pipe_idx);
        opp = vpe_priv->resource.opp[pipe_idx];
        opp->funcs->set_bg(
            opp, zero_dim_rect, zero_dim_rect, VPE_SURFACE_PIXEL_FORMAT_INVALID, bg_color);

        mpc = vpe_priv->resource.mpc[pipe_idx];
        mpc->funcs->program_mpcc_mux(mpc, pipe_idx, MPC_MUX_TOPSEL_DISABLE, MPC_MUX_BOTSEL_DISABLE,
            MPC_MUX_OUTMUX_DISABLE, MPC_MUX_OPPID_DISABLE);
    }

    config_writer_complete(&vpe_priv->config_writer);
}

enum vpe_status vpe20_populate_frod_param(
    struct vpe_priv *vpe_priv, const struct vpe_build_param *param)
{
    struct output_ctx *output_ctx = &vpe_priv->output_ctx;
    enum vpe_status    status     = VPE_STATUS_OK;

    // FROD can't support negative numbers and FP16 can go negative
    if ((status == VPE_STATUS_OK) && vpe_is_fp16(output_ctx->surface.format)) {
        status = VPE_STATUS_FROD_NOT_SUPPORTED;
    }

    if (status == VPE_STATUS_OK) {
        for (uint32_t i = 0; i < VPE_FROD_MAX_STAGE; i++) {
            memcpy(&output_ctx->frod_surface[i], &param->frod_surface[i],
                sizeof(struct vpe_surface_info));
        }
        output_ctx->frod_param.enable_frod = param->frod_param.enable_frod;
    }

    return status;
}

const struct vpe_caps *vpe20_get_capability(void)
{
    return &caps;
}

void vpe20_setup_check_funcs(struct vpe_check_support_funcs *funcs)
{
    funcs->check_input_format             = vpe20_check_input_format;
    funcs->check_output_format            = vpe20_check_output_format;
    funcs->check_input_color_space        = vpe10_check_input_color_space;
    funcs->check_output_color_space       = vpe20_check_output_color_space;
    funcs->get_dcc_compression_input_cap  = vpe20_get_dcc_compression_input_cap;
    funcs->get_dcc_compression_output_cap = vpe20_get_dcc_compression_output_cap;
}

enum vpe_status vpe20_check_lut3d_compound(
    const struct vpe_stream *stream, const struct vpe_build_param *param)
{
    enum vpe_status status = VPE_STATUS_OK;

    /* 3DLUT compound not enabled is trivially supported */
    if (stream->lut_compound.enabled == false) {
        status = VPE_STATUS_OK;
    } else {
        if (stream->surface_info.cs.primaries != VPE_PRIMARIES_CUSTOM) {
            status = VPE_STATUS_LUT_COMPOUND_NOT_SUPPORTED;
        } else if (stream->surface_info.cs.tf != VPE_TF_CUSTOM) {
            status = VPE_STATUS_LUT_COMPOUND_NOT_SUPPORTED;
        } else if (stream->tm_params.lut_out_tf == VPE_TF_G10) {
            status = VPE_STATUS_LUT_COMPOUND_NOT_SUPPORTED;
        } else if (vpe_is_fp16(stream->dma_info.lut3d.format)) {
            status = VPE_STATUS_LUT_COMPOUND_NOT_SUPPORTED;
        } else if (param->dst_surface.cs.encoding != VPE_PIXEL_ENCODING_RGB) {
            enum color_space         cs;
            enum color_transfer_func tf;
            vpe_color_get_color_space_and_tf(&param->dst_surface.cs, &cs, &tf);

            // SDR output with blending and YUV not supported
            if (!vpe_is_HDR(tf) && (stream->blend_info.blending)) {
                status = VPE_STATUS_LUT_COMPOUND_NOT_SUPPORTED;
            }
        }
    }

    return status;
}
