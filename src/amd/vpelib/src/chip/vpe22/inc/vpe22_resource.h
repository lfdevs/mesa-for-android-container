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
#pragma once

#include "resource.h"

#ifdef __cplusplus
extern "C" {
#endif

enum vpe_status vpe22_construct_resource(struct vpe_priv *vpe_priv, struct resource *res);

const struct vpe_caps *vpe22_get_capability(void);

struct cdc_fe *vpe22_cdc_fe_create(struct vpe_priv *vpe_priv, int inst);
struct cdc_be *vpe22_cdc_be_create(struct vpe_priv *vpe_priv, int inst);
struct dpp *vpe22_dpp_create(struct vpe_priv *vpe_priv, int inst);
struct opp *vpe22_opp_create(struct vpe_priv *vpe_priv, int inst);
struct mpc *vpe22_mpc_create(struct vpe_priv *vpe_priv, int inst);

void vpe22_setup_check_funcs(struct vpe_check_support_funcs *funcs);

bool vpe22_check_input_format(enum vpe_surface_pixel_format format);

#ifdef __cplusplus
}
#endif
