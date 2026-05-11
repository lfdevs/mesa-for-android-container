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
#include "vpe_assert.h"
#include "common.h"
#include "vpe_priv.h"
#include "vpe10_mpc.h"
#include "vpe20_mpc.h"
#include "reg_helper.h"
#include "vpe10_cm_common.h"
#include "custom_fp16.h"
#include "conversion.h"

#define CTX_BASE mpc
#define CTX      vpe20_mpc
#define MPC_RMCM_3DLUT_FL_ENABLE  (0x0)
#define MPC_RMCM_3DLUT_FL_DISABLE (0xf)

static struct mpc_funcs mpc_funcs = {
    .program_mpcc_mux            = vpe20_mpc_program_mpcc_mux,
    .program_mpcc_blending       = vpe20_mpc_program_mpcc_blending,
    .program_mpc_bypass_bg_color = vpe10_mpc_program_mpc_bypass_bg_color,
    .power_on_ogam_lut           = vpe10_mpc_power_on_ogam_lut,
    .set_output_csc              = vpe10_mpc_set_output_csc,
    .set_ocsc_default            = vpe10_mpc_set_ocsc_default,
    .program_output_csc          = vpe10_program_output_csc,
    .set_output_gamma            = vpe10_mpc_set_output_gamma,
    .set_gamut_remap             = NULL,
    .set_gamut_remap2            = vpe20_mpc_set_gamut_remap2,
    .power_on_1dlut_shaper_3dlut = vpe20_mpc_power_on_1dlut_shaper_3dlut,
    .program_shaper              = vpe20_mpc_program_shaper,
    .program_3dlut               = vpe20_mpc_program_3dlut,
    .program_3dlut_indirect      = vpe20_mpc_program_3dlut_indirect,
    .program_1dlut               = vpe10_mpc_program_1dlut,
    .program_cm_location         = vpe10_mpc_program_cm_location,
    .set_denorm                  = vpe10_mpc_set_denorm,
    .set_out_float_en            = vpe10_mpc_set_out_float_en,
    .program_mpc_out             = vpe10_mpc_program_mpc_out,
    .set_output_transfer_func    = vpe10_mpc_set_output_transfer_func,
    .set_mpc_shaper_3dlut        = vpe20_mpc_set_mpc_shaper_3dlut,
    .shaper_bypass               = vpe20_mpc_shaper_bypass,
    .set_blend_lut               = vpe10_mpc_set_blend_lut,
    .program_movable_cm          = vpe20_mpc_program_movable_cm,
    .program_crc                 = vpe10_mpc_program_crc,
    .attach_3dlut_to_mpc_inst    = vpe20_attach_3dlut_to_mpc_inst,
    .update_3dlut_fl_bias_scale  = vpe20_update_3dlut_fl_bias_scale,
    .program_mpc_3dlut_fl_config = vpe20_mpc_program_3dlut_fl_config,
    .program_mpc_3dlut_fl        = vpe20_mpc_program_3dlut_fl,
};

void vpe20_construct_mpc(struct vpe_priv *vpe_priv, struct mpc *mpc)
{
    mpc->vpe_priv = vpe_priv;
    mpc->funcs    = &mpc_funcs;
}

void vpe20_mpc_program_mpcc_mux(struct mpc *mpc, enum mpc_mpccid mpcc_idx,
    enum mpc_mux_topsel topsel, enum mpc_mux_botsel botsel, enum mpc_mux_outmux outmux,
    enum mpc_mux_oppid oppid)
{
    PROGRAM_ENTRY();

    /* program mux and MPCC_MODE */
    REG_SET(VPMPCC_TOP_SEL, 0, VPMPCC_TOP_SEL, topsel);
    REG_SET(VPMPCC_BOT_SEL, 0, VPMPCC_BOT_SEL, botsel);
    REG_SET(VPMPC_OUT_MUX, 0, VPMPC_OUT_MUX, outmux);
    REG_SET(VPMPCC_VPOPP_ID, 0, VPMPCC_VPOPP_ID, oppid);
}

void vpe20_mpc_program_mpcc_blending(
    struct mpc *mpc, enum mpc_mpccid mpcc_idx, struct mpcc_blnd_cfg *blnd_cfg)
{
    PROGRAM_ENTRY();
    float                           r_cr, g_y, b_cb;
    int                             bg_r_cr, bg_g_y, bg_b_cb;
    struct vpe_custom_float_format2 fmt;

    REG_SET_5(VPMPCC_CONTROL, REG_DEFAULT(VPMPCC_CONTROL), VPMPCC_MODE, blnd_cfg->blend_mode,
        VPMPCC_ALPHA_BLND_MODE, blnd_cfg->alpha_mode, VPMPCC_ALPHA_MULTIPLIED_MODE,
        blnd_cfg->pre_multiplied_alpha, VPMPCC_BLND_ACTIVE_OVERLAP_ONLY, blnd_cfg->overlap_only,
        VPMPCC_BOT_GAIN_MODE, blnd_cfg->bottom_gain_mode);

    REG_SET_2(VPMPCC_CONTROL2, REG_DEFAULT(VPMPCC_CONTROL2), VPMPCC_GLOBAL_ALPHA,
        blnd_cfg->global_alpha, VPMPCC_GLOBAL_GAIN, blnd_cfg->global_gain);

    REG_SET(VPMPCC_TOP_GAIN, 0, VPMPCC_TOP_GAIN, blnd_cfg->top_gain);
    REG_SET(VPMPCC_BOT_GAIN_INSIDE, 0, VPMPCC_BOT_GAIN_INSIDE, blnd_cfg->bottom_inside_gain);
    REG_SET(VPMPCC_BOT_GAIN_OUTSIDE, 0, VPMPCC_BOT_GAIN_OUTSIDE, blnd_cfg->bottom_outside_gain);

    if (blnd_cfg->bg_color.is_ycbcr) {
        r_cr = blnd_cfg->bg_color.ycbcra.cr;
        g_y  = blnd_cfg->bg_color.ycbcra.y;
        b_cb = blnd_cfg->bg_color.ycbcra.cb;
    } else {
        r_cr = blnd_cfg->bg_color.rgba.r;
        g_y  = blnd_cfg->bg_color.rgba.g;
        b_cb = blnd_cfg->bg_color.rgba.b;
    }

    fmt.flags.Uint      = 0;
    fmt.flags.bits.sign = 1;
    fmt.mantissaBits    = 12;
    fmt.exponentaBits   = 6;

    vpe_convert_to_custom_float_generic((double)r_cr, &fmt, &bg_r_cr);
    vpe_convert_to_custom_float_generic((double)g_y, &fmt, &bg_g_y);
    vpe_convert_to_custom_float_generic((double)b_cb, &fmt, &bg_b_cb);

    // Set background color
    REG_SET(VPMPCC_BG_R_CR, 0, VPMPCC_BG_R_CR, bg_r_cr);
    REG_SET(VPMPCC_BG_G_Y, 0, VPMPCC_BG_G_Y, bg_g_y);
    REG_SET(VPMPCC_BG_B_CB, 0, VPMPCC_BG_B_CB, bg_b_cb);
}

void vpe20_mpc_power_on_1dlut_shaper_3dlut(struct mpc *mpc, bool power_on)
{
    PROGRAM_ENTRY();
    // int max_retries = 10;

    // VPE1 has a single register.
    // VPE2 is split into two that reflects that shaper+3DLUT are in RMCM while (post)1D is stil in MCM
    REG_SET(VPMPCC_MCM_MEM_PWR_CTRL, REG_DEFAULT(VPMPCC_MCM_MEM_PWR_CTRL),
        VPMPCC_MCM_1DLUT_MEM_PWR_DIS, power_on == true ? 1 : 0);
    REG_SET_2(VPMPC_RMCM_MEM_PWR_CTRL, REG_DEFAULT(VPMPC_RMCM_MEM_PWR_CTRL),
        VPMPC_RMCM_SHAPER_MEM_PWR_DIS, power_on == true ? 1 : 0,
        VPMPC_RMCM_3DLUT_MEM_PWR_DIS, power_on == true ? 1 : 0);

    /* wait for memory to fully power up */
    if (power_on && vpe_priv->init.debug.enable_mem_low_power.bits.mpc) {
        // REG_WAIT(VPMPCC_MCM_MEM_PWR_CTRL, VPMPCC_MCM_SHAPER_MEM_PWR_STATE, 0, 1, max_retries);
        //  Use two REG_SET instead of wait for State        
        REG_SET(VPMPCC_MCM_MEM_PWR_CTRL, REG_DEFAULT(VPMPCC_MCM_MEM_PWR_CTRL),
            VPMPCC_MCM_1DLUT_MEM_PWR_DIS, power_on == true ? 1 : 0);
        REG_SET_2(VPMPC_RMCM_MEM_PWR_CTRL, REG_DEFAULT(VPMPC_RMCM_MEM_PWR_CTRL),
            VPMPC_RMCM_SHAPER_MEM_PWR_DIS, power_on == true ? 1 : 0,
            VPMPC_RMCM_3DLUT_MEM_PWR_DIS, power_on == true ? 1 : 0);

        REG_SET(VPMPCC_MCM_MEM_PWR_CTRL, REG_DEFAULT(VPMPCC_MCM_MEM_PWR_CTRL),
            VPMPCC_MCM_1DLUT_MEM_PWR_DIS, power_on == true ? 1 : 0);
        REG_SET_2(VPMPC_RMCM_MEM_PWR_CTRL, REG_DEFAULT(VPMPC_RMCM_MEM_PWR_CTRL),
            VPMPC_RMCM_SHAPER_MEM_PWR_DIS, power_on == true ? 1 : 0,
            VPMPC_RMCM_3DLUT_MEM_PWR_DIS, power_on == true ? 1 : 0);

        // REG_WAIT(VPMPCC_MCM_MEM_PWR_CTRL, VPMPCC_MCM_3DLUT_MEM_PWR_STATE, 0, 1, max_retries);
    }
}

static void vpe20_mpc_configure_shaper_lut(struct mpc *mpc, bool is_ram_a)
{
    PROGRAM_ENTRY();

    REG_SET(VPMPC_RMCM_SHAPER_LUT_INDEX, 0, VPMPC_RMCM_SHAPER_LUT_INDEX, 0);

    REG_SET_2(VPMPC_RMCM_SHAPER_LUT_WRITE_EN_MASK, 0, VPMPC_RMCM_SHAPER_LUT_WRITE_EN_MASK, 7,
        VPMPC_RMCM_SHAPER_LUT_WRITE_SEL, is_ram_a == true ? 0 : 1);
}

static void vpe20_mpc_program_shaper_luta_settings(struct mpc *mpc, const struct pwl_params *params)
{
    PROGRAM_ENTRY();
    const struct gamma_curve *curve;
    uint16_t                  packet_data_size;
    uint16_t                  i;

    REG_SET_2(VPMPC_RMCM_SHAPER_RAMA_START_CNTL_B, 0, VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_START_B,
        params->corner_points[0].blue.custom_float_x,
        VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);
    REG_SET_2(VPMPC_RMCM_SHAPER_RAMA_START_CNTL_G, 0, VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_START_B,
        params->corner_points[0].green.custom_float_x,
        VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);
    REG_SET_2(VPMPC_RMCM_SHAPER_RAMA_START_CNTL_R, 0, VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_START_B,
        params->corner_points[0].red.custom_float_x,
        VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_START_SEGMENT_B, 0);

    REG_SET_2(VPMPC_RMCM_SHAPER_RAMA_END_CNTL_B, 0, VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_END_B,
        params->corner_points[1].blue.custom_float_x, VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_END_BASE_B,
        params->corner_points[1].blue.custom_float_y);
    REG_SET_2(VPMPC_RMCM_SHAPER_RAMA_END_CNTL_G, 0, VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_END_B,
        params->corner_points[1].green.custom_float_x, VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_END_BASE_B,
        params->corner_points[1].green.custom_float_y);
    REG_SET_2(VPMPC_RMCM_SHAPER_RAMA_END_CNTL_R, 0, VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_END_B,
        params->corner_points[1].red.custom_float_x, VPMPC_RMCM_SHAPER_RAMA_EXP_REGION_END_BASE_B,
        params->corner_points[1].red.custom_float_y);

    // Optimized by single VPEP config packet with auto inc

    packet_data_size = (uint16_t)(REG_OFFSET(VPMPC_RMCM_SHAPER_RAMA_REGION_32_33) -
        REG_OFFSET(VPMPC_RMCM_SHAPER_RAMA_REGION_0_1) + 1);

    VPE_ASSERT(packet_data_size <= MAX_CONFIG_PACKET_DATA_SIZE_DWORD);
    packet.bits.INC = 1;      // set the auto increase bit
    packet.bits.VPEP_CONFIG_DATA_SIZE =
        packet_data_size - 1; // number of "continuous" dwords, 1-based
    packet.bits.VPEP_CONFIG_REGISTER_OFFSET = REG_OFFSET(VPMPC_RMCM_SHAPER_RAMA_REGION_0_1);

    config_writer_fill_direct_config_packet_header(config_writer, &packet);

    curve = params->arr_curve_points;

    for (i = 0; i < packet_data_size; i++) {
        config_writer_fill(config_writer,
            REG_FIELD_VALUE(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset) |
            REG_FIELD_VALUE(
                VPMPC_RMCM_SHAPER_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num) |
            REG_FIELD_VALUE(VPMPC_RMCM_SHAPER_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset) |
            REG_FIELD_VALUE(
                VPMPC_RMCM_SHAPER_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num));
        curve += 2;
    }
}

static void vpe20_mpc_program_shaper_lut(
    struct mpc *mpc, const struct pwl_result_data *rgb, uint32_t num)
{
    PROGRAM_ENTRY();
    uint32_t i, red, green, blue;
    uint32_t red_delta, green_delta, blue_delta;
    uint32_t red_value, green_value, blue_value;
    uint16_t packet_data_size;

    // Optimized by single VPEP config packet for same address with multiple write
    packet_data_size = (uint16_t)num * 3; // num writes for each channel in (R, G, B)

    VPE_ASSERT(packet_data_size <= MAX_CONFIG_PACKET_DATA_SIZE_DWORD);
    packet.bits.INC = 0;
    packet.bits.VPEP_CONFIG_DATA_SIZE =
        packet_data_size - 1; // number of "continuous" dwords, 1-based
    packet.bits.VPEP_CONFIG_REGISTER_OFFSET = REG_OFFSET(VPMPC_RMCM_SHAPER_LUT_DATA);

    config_writer_fill_direct_config_packet_header(config_writer, &packet);

    for (i = 0; i < num; i++) {
        red   = rgb[i].red_reg;
        green = rgb[i].green_reg;
        blue  = rgb[i].blue_reg;

        red_delta   = rgb[i].delta_red_reg;
        green_delta = rgb[i].delta_green_reg;
        blue_delta  = rgb[i].delta_blue_reg;

        red_value   = ((red_delta & 0x3ff) << 14) | (red & 0x3fff);
        green_value = ((green_delta & 0x3ff) << 14) | (green & 0x3fff);
        blue_value  = ((blue_delta & 0x3ff) << 14) | (blue & 0x3fff);

        config_writer_fill(config_writer, REG_FIELD_VALUE(VPMPC_RMCM_SHAPER_LUT_DATA, red_value));
        config_writer_fill(config_writer, REG_FIELD_VALUE(VPMPC_RMCM_SHAPER_LUT_DATA, green_value));
        config_writer_fill(config_writer, REG_FIELD_VALUE(VPMPC_RMCM_SHAPER_LUT_DATA, blue_value));
    }
}

void vpe20_mpc_shaper_bypass(struct mpc *mpc, bool bypass)
{
    PROGRAM_ENTRY();

    REG_SET(VPMPC_RMCM_SHAPER_CONTROL, 0, VPMPC_RMCM_SHAPER_LUT_MODE, bypass ? 0 : 1);
}

bool vpe20_mpc_program_shaper(struct mpc *mpc, const struct pwl_params *params)
{
    PROGRAM_ENTRY();

    if (params == NULL) {
        REG_SET(VPMPC_RMCM_SHAPER_CONTROL, 0, VPMPC_RMCM_SHAPER_LUT_MODE, 0);
        return false;
    }

    vpe20_mpc_configure_shaper_lut(mpc, true); // Always use LUT_RAM_A

    // if (vpe_priv->init.debug.enable_mem_low_power.bits.mpc)
    //  should always turn it on
    vpe20_mpc_power_on_1dlut_shaper_3dlut(mpc, true);

    vpe20_mpc_program_shaper_luta_settings(mpc, params);

    vpe20_mpc_program_shaper_lut(mpc, params->rgb_resulted, params->hw_points_num);

    REG_SET(VPMPC_RMCM_SHAPER_CONTROL, 0, VPMPC_RMCM_SHAPER_LUT_MODE, 1);
    REG_SET_DEFAULT(VPMPC_RMCM_SHAPER_SCALE_R);
    REG_SET_DEFAULT(VPMPC_RMCM_SHAPER_SCALE_G_B);

    //? Should we check debug option before turn off shaper? -- should be yes
    if (vpe_priv->init.debug.enable_mem_low_power.bits.mpc)
        vpe20_mpc_power_on_1dlut_shaper_3dlut(mpc, false);

    return true;
}

static void vpe20_mpc_select_3dlut_ram_and_mask(struct mpc *mpc, enum vpe_lut_mode mode,
    bool is_color_channel_12bits, uint32_t ram_selection_mask)
{
    PROGRAM_ENTRY();

    VPE_ASSERT(mode == LUT_RAM_A);

    REG_SET_3(VPMPC_RMCM_3DLUT_READ_WRITE_CONTROL, REG_DEFAULT(VPMPC_RMCM_3DLUT_READ_WRITE_CONTROL),
        VPMPC_RMCM_3DLUT_RAM_SEL, mode == LUT_RAM_A ? 0 : 1, VPMPC_RMCM_3DLUT_30BIT_EN,
        is_color_channel_12bits ? 0 : 1, VPMPC_RMCM_3DLUT_WRITE_EN_MASK, ram_selection_mask);
    REG_SET(VPMPC_RMCM_3DLUT_INDEX, 0, VPMPC_RMCM_3DLUT_INDEX, 0);
}

static void vpe20_mpc_set3dlut_ram12(struct mpc *mpc, const struct vpe_rgb *lut, uint32_t entries)
{
    PROGRAM_ENTRY();
    uint32_t i, red, green, blue, red1, green1, blue1;
    uint16_t MaxLutEntriesPerPacket =
        (MAX_CONFIG_PACKET_DATA_SIZE_DWORD / 3) * 2; // each two entries consumes 3 DWORDs
    uint16_t ActualEntriesInPacket = 0;
    uint16_t ActualPacketSize;

    // Optimized by single VPEP config packet for same address with multiple write

    for (i = 0; i < entries; i += 2) {
        if (i % MaxLutEntriesPerPacket == 0) { // need generate one another new packet
            ActualEntriesInPacket = MaxLutEntriesPerPacket;

            // If single packet is big enough to contain remaining entries
            if ((entries - i) < MaxLutEntriesPerPacket) {
                ActualEntriesInPacket = (uint16_t)(entries - i);
                if ((entries - i) % 2) {
                    // odd entries, round up to even as we need to program in pair
                    ActualEntriesInPacket++;
                }
            }

            ActualPacketSize = ActualEntriesInPacket * 3 / 2;

            VPE_ASSERT(ActualPacketSize <= MAX_CONFIG_PACKET_DATA_SIZE_DWORD);
            packet.bits.INC = 0;
            packet.bits.VPEP_CONFIG_DATA_SIZE =
                ActualPacketSize - 1; // number of "continuous" dwords, 1-based
            packet.bits.VPEP_CONFIG_REGISTER_OFFSET = REG_OFFSET(VPMPC_RMCM_3DLUT_DATA);

            config_writer_fill_direct_config_packet_header(config_writer, &packet);
        }

        red   = lut[i].red << 4;
        green = lut[i].green << 4;
        blue  = lut[i].blue << 4;
        if (i + 1 < entries) {
            red1   = lut[i + 1].red << 4;
            green1 = lut[i + 1].green << 4;
            blue1  = lut[i + 1].blue << 4;
        }
        else {
            // last odd entry, program 0 for extra one that accompany with it.
            red1   = 0;
            green1 = 0;
            blue1  = 0;
        }

        config_writer_fill(config_writer, REG_FIELD_VALUE(VPMPC_RMCM_3DLUT_DATA0, red) |
            REG_FIELD_VALUE(VPMPC_RMCM_3DLUT_DATA1, red1));
        config_writer_fill(config_writer, REG_FIELD_VALUE(VPMPC_RMCM_3DLUT_DATA0, green) |
            REG_FIELD_VALUE(VPMPC_RMCM_3DLUT_DATA1, green1));
        config_writer_fill(config_writer, REG_FIELD_VALUE(VPMPC_RMCM_3DLUT_DATA0, blue) |
            REG_FIELD_VALUE(VPMPC_RMCM_3DLUT_DATA1, blue1));
    }
}

static void vpe20_mpc_set3dlut_ram10(struct mpc *mpc, const struct vpe_rgb *lut, uint32_t entries)
{
    PROGRAM_ENTRY();
    uint32_t i, red, green, blue, value;
    uint16_t MaxLutEntriesPerPacket =
        MAX_CONFIG_PACKET_DATA_SIZE_DWORD; // each entries consumes 1 DWORDs
    uint16_t ActualPacketSize;

    // Optimize to VPEP direct with multiple data
    for (i = 0; i < entries; i++) {
        // Need to revisit about the new config writer handling , DO WE STILL NEED IT?
        // Yes, this is to ensure how many "packets" we need due to each packet have max data size
        // i.e. need to split into diff packets (but might still in one direct config descriptor)
        // The new config writer handles the "descriptor" size exceeded issue.
        // i.e. need to split into diff direct config descriptors.
        if (i % MaxLutEntriesPerPacket == 0) { // need generate one another new packet
            if ((entries - i) <
                MaxLutEntriesPerPacket) // Single packet is big enough to contain remaining entries
                MaxLutEntriesPerPacket = (uint16_t)(entries - i);

            ActualPacketSize = MaxLutEntriesPerPacket;

            VPE_ASSERT(ActualPacketSize <= MAX_CONFIG_PACKET_DATA_SIZE_DWORD);
            packet.bits.INC = 0;
            packet.bits.VPEP_CONFIG_DATA_SIZE =
                ActualPacketSize - 1; // number of "continuous" dwords, 1-based
            packet.bits.VPEP_CONFIG_REGISTER_OFFSET = REG_OFFSET(VPMPC_RMCM_3DLUT_DATA_30BIT);

            config_writer_fill_direct_config_packet_header(config_writer, &packet);
        }

        red   = lut[i].red;
        green = lut[i].green;
        blue  = lut[i].blue;
        // should we shift red 22bit and green 12?
        //  Yes, accroding to spec.
        //  let's do it instead of just shift 10 bits
        value = (red << 22) | (green << 12) | blue << 2;

        config_writer_fill(config_writer, REG_FIELD_VALUE(VPMPC_RMCM_3DLUT_DATA_30BIT, value));
    }
}

static void vpe20_mpc_set_3dlut_mode(
    struct mpc *mpc, enum vpe_lut_mode mode, bool is_lut_size17x17x17)
{
    PROGRAM_ENTRY();
    uint32_t lut_mode;

    if (mode == LUT_BYPASS)
        lut_mode = 0;
    else if (mode == LUT_RAM_A)
        lut_mode = 1;
    else
        lut_mode = 2;

    // 0 = 17x17x17, 1 = 9x9x9, 2 = 33x33x33
    // VPE2 does not support 9x9x9
    REG_SET_2(VPMPC_RMCM_3DLUT_MODE, 0, VPMPC_RMCM_3DLUT_MODE, lut_mode, VPMPC_RMCM_3DLUT_SIZE,
        is_lut_size17x17x17 == true ? 0 : 2);
}

// using direct config to program the 3dlut specified in params
void vpe20_mpc_program_3dlut(struct mpc *mpc, const struct tetrahedral_params *params)
{
    PROGRAM_ENTRY();
    enum vpe_lut_mode     mode;
    bool                  is_17x17x17;
    bool                  is_12bits_color_channel;
    const struct vpe_rgb *lut0;
    const struct vpe_rgb *lut1;
    const struct vpe_rgb *lut2;
    const struct vpe_rgb *lut3;
    uint32_t              lut_size0;
    uint32_t              lut_size;

    if (params == NULL) {
        vpe20_mpc_set_3dlut_mode(mpc, LUT_BYPASS, false);
        return;
    }

    vpe20_mpc_power_on_1dlut_shaper_3dlut(mpc, true);

    // always use LUT_RAM_A except for bypass mode which is not the case here
    mode = LUT_RAM_A;

    is_17x17x17 = (params->lut_dim == LUT_DIM_17);

    is_12bits_color_channel = params->use_12bits;
    if (is_17x17x17) {
        lut0 = params->tetrahedral_17.lut0;
        lut1 = params->tetrahedral_17.lut1;
        lut2 = params->tetrahedral_17.lut2;
        lut3 = params->tetrahedral_17.lut3;
        lut_size0 = sizeof(params->tetrahedral_17.lut0) / sizeof(params->tetrahedral_17.lut0[0]);
        lut_size = sizeof(params->tetrahedral_17.lut1) / sizeof(params->tetrahedral_17.lut1[0]);
    }
    else if (params->lut_dim == LUT_DIM_33) {
        lut0 = params->tetrahedral_33.lut0;
        lut1 = params->tetrahedral_33.lut1;
        lut2 = params->tetrahedral_33.lut2;
        lut3 = params->tetrahedral_33.lut3;
        lut_size0 = sizeof(params->tetrahedral_33.lut0) / sizeof(params->tetrahedral_33.lut0[0]);
        lut_size = sizeof(params->tetrahedral_33.lut1) / sizeof(params->tetrahedral_33.lut1[0]);
    }
    else {
        // 9x9x9 mode not supported on VPE2
        VPE_ASSERT(false);
        vpe20_mpc_set_3dlut_mode(mpc, LUT_BYPASS, false);
        return;
    }

    if (is_12bits_color_channel)
        vpe20_mpc_set3dlut_ram12(mpc, lut0, lut_size0);
    else
        vpe20_mpc_set3dlut_ram10(mpc, lut0, lut_size0);

    if (is_12bits_color_channel)
        vpe20_mpc_set3dlut_ram12(mpc, lut1, lut_size);
    else
        vpe20_mpc_set3dlut_ram10(mpc, lut1, lut_size);

    if (is_12bits_color_channel)
        vpe20_mpc_set3dlut_ram12(mpc, lut2, lut_size);
    else
        vpe20_mpc_set3dlut_ram10(mpc, lut2, lut_size);

    if (is_12bits_color_channel)
        vpe20_mpc_set3dlut_ram12(mpc, lut3, lut_size);
    else
        vpe20_mpc_set3dlut_ram10(mpc, lut3, lut_size);

    vpe20_mpc_set_3dlut_mode(mpc, mode, is_17x17x17);

    if (vpe_priv->init.debug.enable_mem_low_power.bits.mpc)
        vpe20_mpc_power_on_1dlut_shaper_3dlut(mpc, false);

    return;
}

static void vpe20_mpc_set3dlut_ram10_indirect(
    struct mpc *mpc, const uint64_t lut_gpuva, uint32_t entries)
{
    PROGRAM_ENTRY();

    uint32_t data_array_size = entries; // DW size of config data array, actual size
    // Optimized by single VPEP indirect config packet
    // The layout inside the lut buf must be: (each element is 10bit, but LSB[1:0] are always 0)
    // DW0: R0<<22 | G0<<12 | B0 <<2
    // DW0: R1<<22 | G1<<12 | B1 <<2
    //...

    config_writer_set_type(config_writer, CONFIG_TYPE_INDIRECT, mpc->inst);

    // Optimized by single VPEP indirect config packet
    // Fill the 3dLut array pointer
    config_writer_fill_indirect_data_array(config_writer, lut_gpuva, data_array_size);

    // Start from index 0
    config_writer_fill_indirect_destination(
        config_writer, REG_OFFSET(VPMPC_RMCM_3DLUT_INDEX), 0, REG_OFFSET(VPMPC_RMCM_3DLUT_DATA));

    // resume back to direct
    config_writer_set_type(config_writer, CONFIG_TYPE_DIRECT, mpc->inst);
}

static void vpe20_mpc_set3dlut_ram12_indirect(
    struct mpc *mpc, const uint64_t lut_gpuva, uint32_t entries)
{
    PROGRAM_ENTRY();
    // The layout inside the lut buf must be: (each element is 16bit, but LSB[3:0] are always 0)
    // DW0: R1<<16 | R0
    // DW1: G1<<16 | G0
    // DW2: B1<<16 | B0
    // DW3: R3<<16 | R2
    // DW4: G3<<16 | G2
    // DW5: B3<<16 | B2
    //...

    uint32_t data_array_size = (entries / 2 * 3); // DW size of config data array, actual size

    config_writer_set_type(config_writer, CONFIG_TYPE_INDIRECT, mpc->inst);

    // Optimized by single VPEP indirect config packet
    // Fill the 3dLut array pointer
    config_writer_fill_indirect_data_array(config_writer, lut_gpuva, data_array_size);

    // Start from index 0
    config_writer_fill_indirect_destination(
        config_writer, REG_OFFSET(VPMPC_RMCM_3DLUT_INDEX), 0, REG_OFFSET(VPMPC_RMCM_3DLUT_DATA));

    // restore back to direct
    config_writer_set_type(config_writer, CONFIG_TYPE_DIRECT, mpc->inst);
}

// using indirect config to configure the 3DLut
// note that we still need direct config to switch the mask between lut0 - lut3
bool vpe20_mpc_program_3dlut_indirect(struct mpc *mpc,
    struct vpe_buf *lut0_3_buf, // 3d lut buf which contains the data for lut0-lut3
    bool use_tetrahedral_9, bool use_12bits)
{
    PROGRAM_ENTRY();
    enum vpe_lut_mode            mode;
    bool                         is_12bits_color_channel;
    uint64_t                     lut0_gpuva;
    uint64_t                     lut1_gpuva;
    uint64_t                     lut2_gpuva;
    uint64_t                     lut3_gpuva;
    uint32_t                     lut_size0;
    uint32_t                     lut_size;
    // see struct tetrahedral_17x17x17 definition
    const uint32_t tetra17_lut_size = 1228;

    // make sure it is in DIRECT type
    config_writer_set_type(config_writer, CONFIG_TYPE_DIRECT, mpc->inst);

    if (lut0_3_buf == NULL || use_tetrahedral_9) {
        vpe20_mpc_set_3dlut_mode(mpc, LUT_BYPASS, false);
        return false;
    }

    vpe20_mpc_power_on_1dlut_shaper_3dlut(mpc, true);

    // always use LUT_RAM_A except for bypass mode which is not the case here
    mode = LUT_RAM_A;

    is_12bits_color_channel = use_12bits;
    
    lut0_gpuva = lut0_3_buf->gpu_va;
    lut1_gpuva = lut0_3_buf->gpu_va + (uint64_t)(offsetof(struct tetrahedral_17x17x17, lut1));
    lut2_gpuva = lut0_3_buf->gpu_va + (uint64_t)(offsetof(struct tetrahedral_17x17x17, lut2));
    lut3_gpuva = lut0_3_buf->gpu_va + (uint64_t)(offsetof(struct tetrahedral_17x17x17, lut3));
    lut_size0  = tetra17_lut_size + 1; // lut0 has an extra element (vertex (0,0,0))
    lut_size   = tetra17_lut_size;

    if (is_12bits_color_channel)
        vpe20_mpc_set3dlut_ram12_indirect(mpc, lut0_gpuva, lut_size0);
    else
        vpe20_mpc_set3dlut_ram10_indirect(mpc, lut0_gpuva, lut_size0);

    if (is_12bits_color_channel)
        vpe20_mpc_set3dlut_ram12_indirect(mpc, lut1_gpuva, lut_size);
    else
        vpe20_mpc_set3dlut_ram10_indirect(mpc, lut1_gpuva, lut_size);

    if (is_12bits_color_channel)
        vpe20_mpc_set3dlut_ram12_indirect(mpc, lut2_gpuva, lut_size);
    else
        vpe20_mpc_set3dlut_ram10_indirect(mpc, lut2_gpuva, lut_size);

    if (is_12bits_color_channel)
        vpe20_mpc_set3dlut_ram12_indirect(mpc, lut3_gpuva, lut_size);
    else
        vpe20_mpc_set3dlut_ram10_indirect(mpc, lut3_gpuva, lut_size);

    vpe20_mpc_set_3dlut_mode(mpc, mode, !use_tetrahedral_9); // always true here

    if (vpe_priv->init.debug.enable_mem_low_power.bits.mpc)
        vpe20_mpc_power_on_1dlut_shaper_3dlut(mpc, false);

    return true;
}

void vpe20_attach_3dlut_to_mpc_inst(struct mpc *mpc, enum mpc_mpccid mpcc_idx)
{
    PROGRAM_ENTRY();

    REG_SET(VPMPC_RMCM_CNTL, REG_DEFAULT(VPMPC_RMCM_CNTL), VPMPC_RMCM_CNTL, mpcc_idx);
}

void vpe20_mpc_set_mpc_shaper_3dlut(
    struct mpc *mpc, struct transfer_func *func_shaper, struct vpe_3dlut *lut3d_func)
{
    const struct pwl_params *shaper_lut = NULL;
    const struct tetrahedral_params *lut3d_params;

    PROGRAM_ENTRY();
    struct stream_ctx *stream_ctx = &vpe_priv->stream_ctx[vpe_priv->fe_cb_ctx.stream_idx];
    bool               bypass;

    // get the shaper lut params
    if (func_shaper) {
        if (func_shaper->type == TF_TYPE_DISTRIBUTED_POINTS) {
            vpe10_cm_helper_translate_curve_to_hw_format(func_shaper, &mpc->shaper_params, true,
                func_shaper->dirty[mpc->inst]);          // should init shaper_params first
            shaper_lut = &mpc->shaper_params;            // are there shaper prams in dpp instead?
        }
        else if (func_shaper->type == TF_TYPE_HWPWL) {
            shaper_lut = &func_shaper->pwl;
        }
    }

    bypass = (!shaper_lut || (func_shaper && func_shaper->type == TF_TYPE_BYPASS));
    CONFIG_CACHE(func_shaper, stream_ctx, vpe_priv->init.debug.disable_lut_caching, bypass,
        mpc->funcs->program_shaper(mpc, shaper_lut), mpc->inst);

    if (lut3d_func && !lut3d_func->state.bits.is_dma) {
        bypass       = (!lut3d_func || !lut3d_func->state.bits.initialized);
        lut3d_params = (bypass) ? (NULL) : (&lut3d_func->lut_3d);
        CONFIG_CACHE(lut3d_func, stream_ctx, vpe_priv->init.debug.disable_lut_caching, bypass,
            mpc->funcs->program_3dlut(mpc, lut3d_params), mpc->inst);
    }
    return;
}

bool vpe20_mpc_program_movable_cm(struct mpc *mpc, struct transfer_func *func_shaper,
    struct vpe_3dlut *lut3d_func, struct transfer_func *blend_tf, bool afterblend)
{
    struct pwl_params *params = NULL;
    bool               ret    = false;

    /*program shaper and 3dlut and 1dlut in MPC*/
    /*only 1 3dlut inst so only program once*/
    if ((func_shaper || lut3d_func) && (mpc->funcs->set_mpc_shaper_3dlut != NULL))
        mpc->funcs->set_mpc_shaper_3dlut(mpc, func_shaper, lut3d_func);
    mpc->funcs->set_blend_lut(mpc, blend_tf);
    mpc->funcs->program_cm_location(mpc, afterblend);

    return ret;
}

static void vpe20_program_gamut_remap(struct mpc *mpc,
    enum mpcc_gamut_remap_id gamut_remap_block_id, const uint16_t *regval,
    enum gamut_remap_select select)
{
    uint16_t                  selection = 0;
    struct color_matrices_reg gam_regs;
    PROGRAM_ENTRY();

    switch (gamut_remap_block_id) {
    case VPE_MPC_GAMUT_REMAP:

        if (regval == NULL || select == GAMUT_REMAP_BYPASS) {
            REG_SET(VPMPCC_GAMUT_REMAP_MODE, 0, VPMPCC_GAMUT_REMAP_MODE, GAMUT_REMAP_BYPASS);
            return;
        }

        gam_regs.shifts.csc_c11 = REG_FIELD_SHIFT(VPMPCC_GAMUT_REMAP_C11_A);
        gam_regs.masks.csc_c11  = REG_FIELD_MASK(VPMPCC_GAMUT_REMAP_C11_A);
        gam_regs.shifts.csc_c12 = REG_FIELD_SHIFT(VPMPCC_GAMUT_REMAP_C12_A);
        gam_regs.masks.csc_c12  = REG_FIELD_MASK(VPMPCC_GAMUT_REMAP_C12_A);
        gam_regs.csc_c11_c12    = REG_OFFSET(VPMPC_GAMUT_REMAP_C11_C12_A);
        gam_regs.csc_c33_c34    = REG_OFFSET(VPMPC_GAMUT_REMAP_C33_C34_A);

        vpe10_cm_helper_program_color_matrices(config_writer, regval, &gam_regs);

        REG_SET(VPMPCC_GAMUT_REMAP_MODE, 0, VPMPCC_GAMUT_REMAP_MODE, GAMUT_REMAP_COMA_COEFF);

        break;

    case VPE_MPC_RMCM_GAMUT_REMAP:
        if (regval == NULL || select == GAMUT_REMAP_BYPASS) {
            REG_SET(
                VPMPC_RMCM_GAMUT_REMAP_MODE, 0, VPMPC_RMCM_GAMUT_REMAP_MODE, GAMUT_REMAP_BYPASS);
            return;
        }

        gam_regs.shifts.csc_c11 = REG_FIELD_SHIFT(VPMPC_RMCM_GAMUT_REMAP_C11_SETA);
        gam_regs.masks.csc_c11  = REG_FIELD_MASK(VPMPC_RMCM_GAMUT_REMAP_C11_SETA);
        gam_regs.shifts.csc_c12 = REG_FIELD_SHIFT(VPMPC_RMCM_GAMUT_REMAP_C12_SETA);
        gam_regs.masks.csc_c12  = REG_FIELD_MASK(VPMPC_RMCM_GAMUT_REMAP_C12_SETA);
        gam_regs.csc_c11_c12    = REG_OFFSET(VPMPC_RMCM_GAMUT_REMAP_C11_C12_SETA);
        gam_regs.csc_c33_c34    = REG_OFFSET(VPMPC_RMCM_GAMUT_REMAP_C33_C34_SETA);

        vpe10_cm_helper_program_color_matrices(config_writer, regval, &gam_regs);

        REG_SET(
            VPMPC_RMCM_GAMUT_REMAP_MODE, 0, VPMPC_RMCM_GAMUT_REMAP_MODE, GAMUT_REMAP_COMA_COEFF);

        break;

    case VPE_MPC_MCM_FIRST_GAMUT_REMAP:
        if (regval == NULL || select == GAMUT_REMAP_BYPASS) {
            REG_SET(VPMPCC_MCM_FIRST_GAMUT_REMAP_MODE, 0, VPMPCC_MCM_FIRST_GAMUT_REMAP_MODE,
                GAMUT_REMAP_BYPASS);
            return;
        }

        gam_regs.shifts.csc_c11 = REG_FIELD_SHIFT(VPMPCC_MCM_FIRST_GAMUT_REMAP_C11_SETA);
        gam_regs.masks.csc_c11  = REG_FIELD_MASK(VPMPCC_MCM_FIRST_GAMUT_REMAP_C11_SETA);
        gam_regs.shifts.csc_c12 = REG_FIELD_SHIFT(VPMPCC_MCM_FIRST_GAMUT_REMAP_C12_SETA);
        gam_regs.masks.csc_c12  = REG_FIELD_MASK(VPMPCC_MCM_FIRST_GAMUT_REMAP_C12_SETA);
        gam_regs.csc_c11_c12    = REG_OFFSET(VPMPC_MCM_FIRST_GAMUT_REMAP_C11_C12_SETA);
        gam_regs.csc_c33_c34    = REG_OFFSET(VPMPC_MCM_FIRST_GAMUT_REMAP_C33_C34_SETA);

        vpe10_cm_helper_program_color_matrices(config_writer, regval, &gam_regs);

        REG_SET(VPMPCC_MCM_FIRST_GAMUT_REMAP_MODE, 0, VPMPCC_MCM_FIRST_GAMUT_REMAP_MODE,
            GAMUT_REMAP_COMA_COEFF);

        break;

    case VPE_MPC_MCM_SECOND_GAMUT_REMAP:

        if (regval == NULL || select == GAMUT_REMAP_BYPASS) {
            REG_SET(VPMPCC_MCM_SECOND_GAMUT_REMAP_MODE, 0, VPMPCC_MCM_SECOND_GAMUT_REMAP_MODE,
                GAMUT_REMAP_BYPASS);
            return;
        }

        gam_regs.shifts.csc_c11 = REG_FIELD_SHIFT(VPMPCC_MCM_SECOND_GAMUT_REMAP_C11_SETA);
        gam_regs.masks.csc_c11  = REG_FIELD_MASK(VPMPCC_MCM_SECOND_GAMUT_REMAP_C11_SETA);
        gam_regs.shifts.csc_c12 = REG_FIELD_SHIFT(VPMPCC_MCM_SECOND_GAMUT_REMAP_C12_SETA);
        gam_regs.masks.csc_c12  = REG_FIELD_MASK(VPMPCC_MCM_SECOND_GAMUT_REMAP_C12_SETA);
        gam_regs.csc_c11_c12    = REG_OFFSET(VPMPC_MCM_SECOND_GAMUT_REMAP_C11_C12_SETA);
        gam_regs.csc_c33_c34    = REG_OFFSET(VPMPC_MCM_SECOND_GAMUT_REMAP_C33_C34_SETA);

        vpe10_cm_helper_program_color_matrices(config_writer, regval, &gam_regs);

        REG_SET(VPMPCC_MCM_SECOND_GAMUT_REMAP_MODE, 0, VPMPCC_MCM_SECOND_GAMUT_REMAP_MODE,
            GAMUT_REMAP_COMA_COEFF);
        break;

    default:
        VPE_ASSERT(0);
        break;
    }
}

void vpe20_mpc_set_gamut_remap2(struct mpc *mpc, struct colorspace_transform *gamut_remap,
    enum mpcc_gamut_remap_id mpcc_gamut_remap_block_id)
{
    uint16_t arr_reg_val[12] = {0}; // Initialize to zero
    PROGRAM_ENTRY();
    int i = 0;

    enum gamut_remap_select gamutSel = GAMUT_REMAP_COMA_COEFF;

    if (!gamut_remap || !gamut_remap->enable_remap)
        gamutSel = GAMUT_REMAP_BYPASS;
    else {
        conv_convert_float_matrix(arr_reg_val, gamut_remap->matrix, 12);
    }

    vpe20_program_gamut_remap(mpc, mpcc_gamut_remap_block_id,
        (gamutSel == GAMUT_REMAP_COMA_COEFF) ? arr_reg_val : NULL, gamutSel);
}

void vpe20_update_3dlut_fl_bias_scale(struct mpc *mpc, uint16_t bias, uint16_t scale)
{
    PROGRAM_ENTRY();

    REG_SET_2(VPMPC_VPCDC0_3DLUT_FL_BIAS_SCALE, 0, VPCDC0_3DLUT_FL_BIAS, bias,
        VPCDC0_3DLUT_FL_SCALE, scale);
}

void vpe20_mpc_program_3dlut_fl_config(struct mpc *mpc, enum vpe_3dlut_mem_layout layout,
    enum vpe_3dlut_mem_format format, bool enable)
{
    PROGRAM_ENTRY();

    if (enable) {
        REG_SET(VPMPC_RMCM_3DLUT_FAST_LOAD_SELECT, 0, VPMPC_RMCM_3DLUT_FL_SEL,
            MPC_RMCM_3DLUT_FL_ENABLE);
        REG_SET_2(VPMPC_VPCDC0_3DLUT_FL_CONFIG, 0, VPCDC0_3DLUT_FL_MODE, layout,
            VPCDC0_3DLUT_FL_FORMAT, format);
    } else
        REG_SET(VPMPC_RMCM_3DLUT_FAST_LOAD_SELECT, 0, VPMPC_RMCM_3DLUT_FL_SEL,
            MPC_RMCM_3DLUT_FL_DISABLE);
}

void vpe20_mpc_program_3dlut_fl(struct mpc *mpc, enum lut_dimension lut_dimension, bool use_12bit)
{
    PROGRAM_ENTRY();
    enum vpe_lut_mode mode = LUT_RAM_A;

    vpe20_mpc_power_on_1dlut_shaper_3dlut(mpc, true);

    // always use LUT_RAM_A except for bypass mode which is not the case here
    vpe20_mpc_set_3dlut_mode(mpc, mode, lut_dimension == LUT_DIM_17 ? 1 : 0);

    if (vpe_priv->init.debug.enable_mem_low_power.bits.mpc)
        vpe20_mpc_power_on_1dlut_shaper_3dlut(mpc, false);
}
