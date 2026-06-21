/* Copyright 2025 Advanced Micro Devices, Inc.
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
#include "vpe22_resource.h"
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
#include "vpe22/inc/asic/chip_offset.h"
#include "vpe22/inc/asic/chip_mask.h"
#include "vpe22/inc/asic/chip_shift.h"
#include "vpe22/inc/asic/chip_default.h"
#include "custom_fp16.h"
#include "custom_float.h"
#include "background.h"
#include "vpe_visual_confirm.h"
#include "vpe_spl_translation.h"
#include "SPL/dc_spl.h"

#define LUT_NUM_ENTRIES   (17 * 17 * 17)
#define LUT_ENTRY_SIZE    (2)
#define LUT_NUM_COMPONENT (3)
#define LUT_BUFFER_SIZE   (LUT_NUM_ENTRIES * LUT_ENTRY_SIZE * LUT_NUM_COMPONENT)

#define LUT_FL_SIZE_17X17X17 (4916)
#define LUT_FL_SIZE_33X33X33 (35940)

#define BYTES_PER_ENTRY                    (4)
#define SHAPER_LUT_CHANNELS                (3)
#define SHAPER_LUT_DATA_POINTS_PER_CHANNEL (256)
#define SHAPER_LUT_CONFIG_ENTRIES          (29)
#define SHAPER_LUT_DMA_DATA_SIZE                                                                   \
    (SHAPER_LUT_DATA_POINTS_PER_CHANNEL * SHAPER_LUT_CHANNELS * BYTES_PER_ENTRY)
#define SHAPER_LUT_DMA_CONFIG_SIZE                                                                 \
    (SHAPER_LUT_CONFIG_ENTRIES * (BYTES_PER_ENTRY + SHAPER_LUT_DMA_CONFIG_PADDING))
#define SHAPER_LUT_DMA_DATA_ALIGNMENT   (64)
#define SHAPER_LUT_DMA_CONFIG_ALIGNMENT (64)
#define SHAPER_LUT_DMA_CONFIG_PADDING   (60)
#define LUT_3D_DMA_ALIGNMENT            (256)

#define VPE_DESTINATION_AS_INPUT_STREAM_INDEX 0xff

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
                                .lut_3d_compound = 1,
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
                            .y410            = 1, /**< packed 4:4:4 10-bit */
                            .y416            = 1, /**< packed 4:4:4 16-bit */
                            .p210            = 1, /**< planar 4:2:2 10-bit */
                            .p216            = 1, /**< planar 4:2:2 16-bit */
                            .r8              = 0, /**< single channel RGB 8-bit */
                            .r16             = 0, /**< single channel RGB 16-bit */
                            .rgb8_planar     = 1, /**< planar RGB 8-bit */
                            .rgb16_planar    = 1, /**< planar RGB 16-bit */
                            .yuv8_planar     = 1, /**< planar YUV 8-bit */
                            .yuv16_planar    = 1, /**< planar YUV 16-bit */
                            .fp16_planar     = 1, /**< planar float 16-bit */
                            .rgbe            = 1, /**< shared exponent R9G9B9E5 */
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
                            .y410            = 1, /**< packed 4:4:4 10-bit */
                            .y416            = 1, /**< packed 4:4:4 16-bit */
                            .p210            = 1, /**< planar 4:2:2 10-bit */
                            .p216            = 1, /**< planar 4:2:2 16-bit */
                            .r8              = 0, /**< single channel RGB 8-bit */
                            .r16             = 0, /**< single channel RGB 16-bit */
                            .rgb8_planar     = 1, /**< planar RGB 8-bit */
                            .rgb16_planar    = 1, /**< planar RGB 16-bit */
                            .yuv8_planar     = 1, /**< planar YUV 8-bit */
                            .yuv16_planar    = 1, /**< planar YUV 16-bit */
                            .fp16_planar     = 1, /**< planar float 16-bit */
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
            .easf_support                = 1,
            .input_internal_dcc_support  = 1,
            .output_internal_dcc_support = 0,
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

struct cdc_fe *vpe22_cdc_fe_create(struct vpe_priv *vpe_priv, int inst)
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

struct cdc_be *vpe22_cdc_be_create(struct vpe_priv *vpe_priv, int inst)
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

struct dpp *vpe22_dpp_create(struct vpe_priv *vpe_priv, int inst)
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

struct opp *vpe22_opp_create(struct vpe_priv *vpe_priv, int inst)
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

struct mpc *vpe22_mpc_create(struct vpe_priv *vpe_priv, int inst)
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

enum vpe_status vpe22_construct_resource(struct vpe_priv *vpe_priv, struct resource *res)
{
    struct vpe *vpe = &vpe_priv->pub;
    uint32_t    i;

    vpe->caps = &caps;

    vpe10_construct_vpec(vpe_priv, &res->vpec);

    for (i = 0; i < vpe->caps->resource_caps.num_dpp; i++) { // num pipes = num dpp = num_mpc
        res->cdc_fe[i] = vpe22_cdc_fe_create(vpe_priv, i);
        if (res->cdc_fe[i] == NULL)
            goto err;

        res->dpp[i] = vpe22_dpp_create(vpe_priv, i);
        if (res->dpp[i] == NULL)
            goto err;

        res->mpc[i] = vpe22_mpc_create(vpe_priv, i);
        if (res->mpc[i] == NULL)
            goto err;
    }

    for (i = 0; i < vpe->caps->resource_caps.num_cdc_be; i++) {
        res->cdc_be[i] = vpe22_cdc_be_create(vpe_priv, i);
        if (res->cdc_be[i] == NULL)
            goto err;
    }

    for (i = 0; i < vpe->caps->resource_caps.num_opp; i++) { // num opp = num dpp
        res->opp[i] = vpe22_opp_create(vpe_priv, i);
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
    res->program_frontend                   = NULL;
    res->program_frontend_frame             = vpe20_program_frontend_frame;
    res->program_frontend_segment           = vpe20_program_frontend_segment;
    res->program_stream_op_config           = vpe20_program_stream_ops_config;
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
    res->populate_frod_param                = vpe20_populate_frod_param;
    res->set_lls_pref                       = vpe20_set_lls_pref;
    res->program_fastload     = vpe20_program_3dlut_fl;
    res->pipe_setup           = vpe20_pipe_setup;
    res->mpc_reset            = vpe20_mpc_reset;
    res->calculate_shaper     = vpe10_calculate_shaper;
    res->check_lut3d_compound = vpe20_check_lut3d_compound;

    res->set_dst_cmd_info_scaler        = vpe20_set_dst_cmd_info_scaler;
    res->update_opp_adjust_and_boundary = vpe20_update_opp_adjust_and_boundary;

    return VPE_STATUS_OK;

err:
    vpe20_destroy_resource(vpe_priv, res);
    return VPE_STATUS_ERROR;
}

bool vpe22_check_input_format(enum vpe_surface_pixel_format format)
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

    if (format == VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBE)
        return true;

    if (format == VPE_SURFACE_PIXEL_FORMAT_PLANAR_8bpc_RGB)
        return true;

    return false;
}

const struct vpe_caps *vpe22_get_capability(void)
{
    return &caps;
}

void vpe22_setup_check_funcs(struct vpe_check_support_funcs *funcs)
{
    funcs->check_input_format             = vpe22_check_input_format;
    funcs->check_output_format            = vpe20_check_output_format;
    funcs->check_input_color_space        = vpe10_check_input_color_space;
    funcs->check_output_color_space       = vpe20_check_output_color_space;
}
