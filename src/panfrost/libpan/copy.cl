/*
 * Copyright 2026 Google LLC
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */
#include "compiler/libcl/libcl.h"

#if PAN_ARCH >= 6
KERNEL(32)
panlib_fill(global uint32_t *address, uint32_t value)
{
   address[cl_global_id.x] = value;
}

KERNEL(32)
panlib_fill_uint4(global uint4 *address, uint a, uint b, uint c, uint d)
{
   address[cl_global_id.x] = (uint4)(a, b, c, d);
}

KERNEL(1)
panlib_fill_scalar(global uint32_t *address, uint32_t value)
{
   address[cl_global_id.x] = value;
}

KERNEL(1)
panlib_fill_uint4_scalar(global uint4 *address, uint a, uint b, uint c, uint d)
{
   address[cl_global_id.x] = (uint4)(a, b, c, d);
}
#endif
