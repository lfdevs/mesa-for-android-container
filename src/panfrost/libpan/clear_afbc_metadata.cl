/*
 * Copyright 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */
#include "compiler/libcl/libcl.h"
#include "compiler/libcl/libcl_vk.h"
#include "genxml/gen_macros.h"
#include "lib/pan_encoder.h"

#if PAN_ARCH >= 6
KERNEL(1)
panlib_clear_afbc_metadata(global uint8_t *p,
                           uint32_t layer_or_slice_stride)
{
    uint32_t item = get_global_id(0);
    uint32_t layer_or_slice = get_global_id(1);

    uint4 *q = p + layer_or_slice * layer_or_slice_stride;
    q[item] = 0;
}
#endif
