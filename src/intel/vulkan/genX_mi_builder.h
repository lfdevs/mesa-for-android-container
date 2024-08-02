/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "genxml/gen_macros.h"
#include "genxml/genX_pack.h"

/* We reserve :
 *    - GPR 13 for STATE_BASE_ADDRESS bindless surface base address
 *    - GPR 14 for perf queries
 *    - GPR 15 for conditional rendering
 */
#define MI_BUILDER_NUM_ALLOC_GPRS 13
#define MI_BUILDER_CAN_WRITE_BATCH true
/* Don't do any write check by default, we manually set it where it matters.
 */
#define MI_BUILDER_DEFAULT_WRITE_CHECK false
#define __gen_get_batch_dwords anv_batch_emit_dwords
#define __gen_address_offset anv_address_add
#define __gen_get_batch_address(b, a) anv_batch_address(b, a)
#define __gen_get_write_fencing_status(b) (&(b)->write_fence_status)
#include "common/mi_builder.h"
