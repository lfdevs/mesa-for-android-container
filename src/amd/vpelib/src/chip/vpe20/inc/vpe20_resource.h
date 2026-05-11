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

#include "resource.h"

#ifdef __cplusplus
extern "C" {
#endif

enum vpe_status vpe20_construct_resource(struct vpe_priv *vpe_priv, struct resource *res);

void vpe20_update_recout_dst_viewport(struct scaler_data *data,
    enum vpe_surface_pixel_format format, struct spl_opp_adjust *opp_adjust,
    bool opp_background_gen);

void vpe20_update_src_viewport(struct scaler_data *data, enum vpe_surface_pixel_format format);

void vpe20_calculate_dst_viewport_and_active(
    struct segment_ctx* segment_ctx, uint32_t max_seg_width);

bool vpe20_check_h_mirror_support(bool* input_mirror, bool* output_mirror);

void vpe20_destroy_resource(struct vpe_priv *vpe_priv, struct resource *res);

bool vpe20_check_input_format(enum vpe_surface_pixel_format format);

bool vpe20_check_output_format(enum vpe_surface_pixel_format format);

bool vpe20_check_output_color_space(
    enum vpe_surface_pixel_format format, const struct vpe_color_space *vcs);

void vpe20_set_lls_pref(struct vpe_priv *vpe_priv, struct spl_in *spl_input,
    enum color_transfer_func tf, enum vpe_surface_pixel_format pixel_format);

enum vpe_status vpe20_check_bg_color_support(struct vpe_priv* vpe_priv, struct vpe_color* bg_color);

void vpe20_bg_color_convert(enum color_space output_cs, struct transfer_func *output_tf,
    enum vpe_surface_pixel_format pixel_format, struct vpe_color *mpc_bg_color,
    struct vpe_color *opp_bg_color, bool enable_3dlut);

int32_t vpe20_program_backend(struct vpe_priv* vpe_priv, uint32_t pipe_idx, uint32_t cmd_idx, bool seg_only);

uint16_t vpe20_get_bg_stream_idx(struct vpe_priv *vpe_priv);

uint32_t vpe20_get_hw_surface_format(enum vpe_surface_pixel_format format);

enum vpe_status vpe20_calculate_segments(
    struct vpe_priv *vpe_priv, const struct vpe_build_param *params);

int32_t vpe20_program_frontend(struct vpe_priv* vpe_priv, uint32_t pipe_idx, uint32_t cmd_idx,
    uint32_t cmd_input_idx, bool seg_only);

bool vpe20_get_dcc_compression_output_cap(
    const struct vpe_dcc_surface_param *params, struct vpe_surface_dcc_cap *cap);

bool vpe20_get_dcc_compression_input_cap(
    const struct vpe_dcc_surface_param *params, struct vpe_surface_dcc_cap *cap);

void vpe20_create_stream_ops_config(struct vpe_priv *vpe_priv, uint32_t pipe_idx,
    uint32_t cmd_input_idx, struct stream_ctx *stream_ctx, struct vpe_cmd_info *cmd_info);

enum vpe_status vpe20_populate_cmd_info(struct vpe_priv *vpe_priv);

enum vpe_status vpe20_set_num_segments(struct vpe_priv *vpe_priv, struct stream_ctx *stream_ctx,
    struct scaler_data *scl_data, struct vpe_rect *src_rect, struct vpe_rect *dst_rect,
    uint32_t *max_seg_width, uint32_t recout_width_alignment);

void vpe20_get_bufs_req(struct vpe_priv *vpe_priv, struct vpe_bufs_req *req);

enum vpe_status vpe20_check_mirror_rotation_support(const struct vpe_stream *stream);

enum vpe_status vpe20_update_blnd_gamma(struct vpe_priv *vpe_priv,
    const struct vpe_build_param *param, const struct vpe_stream *stream,
    struct transfer_func *blnd_tf);

enum vpe_status vpe20_update_output_gamma(struct vpe_priv *vpe_priv,
    const struct vpe_build_param *param, struct transfer_func *output_tf, bool geometric_scaling);

struct cdc_fe *vpe20_cdc_fe_create(struct vpe_priv *vpe_priv, int inst);
struct cdc_be *vpe20_cdc_be_create(struct vpe_priv *vpe_priv, int inst);
struct dpp    *vpe20_dpp_create(struct vpe_priv *vpe_priv, int inst);
struct opp    *vpe20_opp_create(struct vpe_priv *vpe_priv, int inst);
struct mpc    *vpe20_mpc_create(struct vpe_priv *vpe_priv, int inst);

void vpe20_fill_bg_cmd_scaler_data(
    struct stream_ctx *stream_ctx, struct vpe_rect *dst_viewport, struct scaler_data *scaler_data);

void vpe20_create_bg_segments(
    struct vpe_priv *vpe_priv, struct vpe_rect *gaps, uint16_t gaps_cnt, enum vpe_cmd_ops ops);

uint32_t vpe20_get_max_seg_width(struct output_ctx *output_ctx,
    enum vpe_surface_pixel_format format, enum vpe_scan_direction scan);

enum vpe_status vpe20_fill_alpha_through_luma_cmd_info(
    struct vpe_priv *vpe_priv, uint16_t alpha_stream_idx);

enum vpe_status vpe20_fill_non_performance_mode_cmd_info(
    struct vpe_priv *vpe_priv, uint16_t stream_idx);

enum vpe_status vpe20_fill_performance_mode_cmd_info(
    struct vpe_priv *vpe_priv, uint16_t stream_idx, uint16_t avail_pipe_count);

enum vpe_status vpe20_fill_blending_cmd_info(
    struct vpe_priv *vpe_priv, uint16_t top_stream_idx, uint16_t bot_stream_idx);

uint32_t vpe20_get_num_pipes_available(struct vpe_priv *vpe_priv, struct stream_ctx *stream_ctx);

void vpe20_set_frod_output_viewport(struct vpe_cmd_output *dst_output,
    struct vpe_cmd_output *src_output, uint32_t viewport_divider,
    enum vpe_surface_pixel_format format);

void vpe20_reset_pipes(struct vpe_priv *vpe_priv);

enum vpe_status vpe20_populate_frod_param(
    struct vpe_priv *vpe_priv, const struct vpe_build_param *param);

void vpe20_update_opp_adjust_and_boundary(struct stream_ctx *stream_ctx, uint16_t seg_idx,
    bool dst_subsampled, const struct vpe_rect *src_rect, const struct vpe_rect *dst_rect,
    struct output_ctx *output_ctx, struct spl_opp_adjust *opp_recout_adjust);

bool vpe20_set_dst_cmd_info_scaler(struct stream_ctx *dst_stream_ctx,
    struct scaler_data *dst_scaler_data, struct vpe_rect recout, struct vpe_rect dst_viewport,
    struct fmt_boundary_mode *boundary_mode, struct spl_opp_adjust *opp_adjust);

bool vpe20_validate_cached_param(struct vpe_priv *vpe_priv, const struct vpe_build_param *param);

void vpe20_program_3dlut_fl(struct vpe_priv *vpe_priv, uint32_t cmd_idx);

const struct vpe_caps *vpe20_get_capability(void);

void vpe20_setup_check_funcs(struct vpe_check_support_funcs *funcs);

enum vpe_status vpe20_check_lut3d_compound(
    const struct vpe_stream *stream, const struct vpe_build_param *param);

#ifdef __cplusplus
}
#endif
