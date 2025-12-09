/* Copyright 2022 Advanced Micro Devices, Inc.
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

enum vpe_status vpe10_construct_resource(struct vpe_priv *vpe_priv, struct resource *res);

void vpe10_destroy_resource(struct vpe_priv *vpe_priv, struct resource *res);

enum vpe_status vpe10_set_num_segments(struct vpe_priv *vpe_priv, struct stream_ctx *stream_ctx,
    struct scaler_data *scl_data, struct vpe_rect *src_rect, struct vpe_rect *dst_rect,
    uint32_t *max_seg_width, uint32_t recout_width_alignment);

bool vpe10_get_dcc_compression_output_cap(
    const struct vpe_dcc_surface_param *params, struct vpe_surface_dcc_cap *cap);

bool vpe10_get_dcc_compression_input_cap(
    const struct vpe_dcc_surface_param *params, struct vpe_surface_dcc_cap *cap);

bool vpe10_check_input_format(enum vpe_surface_pixel_format format);

bool vpe10_check_output_format(enum vpe_surface_pixel_format format);

bool vpe10_check_input_color_space(
    enum vpe_surface_pixel_format format, const struct vpe_color_space *vcs);

bool vpe10_check_output_color_space(
    enum vpe_surface_pixel_format format, const struct vpe_color_space *vcs);

bool vpe10_check_h_mirror_support(bool *input_mirror, bool *output_mirror);

enum vpe_status vpe10_check_bg_color_support(struct vpe_priv* vpe_priv, struct vpe_color* bg_color);

void vpe10_bg_color_convert(enum color_space output_cs, struct transfer_func *output_tf,
    enum vpe_surface_pixel_format pixel_format, struct vpe_color *mpc_bg_color,
    struct vpe_color *opp_bg_color, bool enable_3dlut);

uint16_t vpe10_get_bg_stream_idx(struct vpe_priv *vpe_priv);

enum vpe_status vpe10_calculate_segments(
    struct vpe_priv *vpe_priv, const struct vpe_build_param *params);

int32_t vpe10_program_frontend(struct vpe_priv *vpe_priv, uint32_t pipe_idx, uint32_t cmd_idx,
    uint32_t cmd_input_idx, bool seg_only);

int32_t vpe10_program_backend(
    struct vpe_priv *vpe_priv, uint32_t pipe_idx, uint32_t cmd_idx, bool seg_only);

enum vpe_status vpe10_populate_cmd_info(struct vpe_priv *vpe_priv);

void vpe10_calculate_dst_viewport_and_active(
    struct segment_ctx *segment_ctx, uint32_t max_seg_width);

void vpe10_create_stream_ops_config(struct vpe_priv *vpe_priv, uint32_t pipe_idx,
    struct stream_ctx *stream_ctx, struct vpe_cmd_input *cmd_input, enum vpe_cmd_ops ops);

void vpe10_get_bufs_req(struct vpe_priv *vpe_priv, struct vpe_bufs_req *req);

enum vpe_status vpe10_check_mirror_rotation_support(const struct vpe_stream *stream);

enum vpe_status vpe10_update_blnd_gamma(struct vpe_priv *vpe_priv,
    const struct vpe_build_param *param, const struct vpe_stream *stream,
    struct transfer_func *blnd_tf);

enum vpe_status vpe10_update_output_gamma(struct vpe_priv *vpe_priv,
    const struct vpe_build_param *param, struct transfer_func *output_tf, bool geometric_scaling);

struct opp *vpe10_opp_create(struct vpe_priv *vpe_priv, int inst);

struct mpc *vpe10_mpc_create(struct vpe_priv *vpe_priv, int inst);

struct dpp *vpe10_dpp_create(struct vpe_priv *vpe_priv, int inst);

struct cdc_fe *vpe10_cdc_fe_create(struct vpe_priv *vpe_priv, int inst);
struct cdc_be *vpe10_cdc_be_create(struct vpe_priv *vpe_priv, int inst);

bool vpe10_validate_cached_param(struct vpe_priv *vpe_priv, const struct vpe_build_param *param);

void vpe10_setup_check_funcs(struct vpe_check_support_funcs *funcs);

const struct vpe_caps *vpe10_get_capability(void);
#ifdef __cplusplus
}
#endif
