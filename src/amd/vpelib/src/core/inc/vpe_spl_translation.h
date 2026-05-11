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

#include "vpe_types.h"
#include "transform.h"
#include "vpe_priv.h"
#include "SPL/dc_spl_types.h"
#include "SPL/dc_spl.h"

#ifdef __cplusplus
extern "C" {
#endif

void vpe_spl_scl_to_vpe_scl(struct spl_out *spl_out, struct scaler_data *vpe_scl_data);

void vpe_init_spl_in(
    struct spl_in *spl_input, struct stream_ctx *stream_ctx, struct output_ctx *output_ctx);

void vpe_scl_to_dscl_bg(struct scaler_data *scl_data);

void vpe_get_vp_scan_direction(enum vpe_rotation_angle degree, bool h_mirror, bool v_mirror,
    bool *orthogonal_rotation, bool *flip_horz_scan_dir, bool *flip_vert_scan_dir);

#ifdef __cplusplus
}
#endif
