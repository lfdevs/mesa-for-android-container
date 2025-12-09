/*
 * Copyright © 2020 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_SHADOWED_REGS
#define AC_SHADOWED_REGS

#include "ac_gpu_info.h"

struct ac_reg_range {
   unsigned offset;
   unsigned size;
};

enum ac_reg_range_type
{
   SI_REG_RANGE_UCONFIG,
   SI_REG_RANGE_CONTEXT,
   SI_REG_RANGE_SH,
   SI_REG_RANGE_CS_SH,
   SI_NUM_REG_RANGES,
};

#ifdef __cplusplus
extern "C" {
#endif

void ac_get_reg_ranges(enum amd_gfx_level gfx_level, enum radeon_family family,
                       enum ac_reg_range_type type, unsigned *num_ranges,
                       const struct ac_reg_range **ranges);
struct ac_pm4_state *ac_emulate_clear_state(const struct radeon_info *info);
void ac_print_nonshadowed_regs(enum amd_gfx_level gfx_level, enum radeon_family family);
void ac_build_load_reg(const struct radeon_info *info, struct ac_pm4_state *pm4,
                       enum ac_reg_range_type type, uint64_t gpu_address);

struct ac_pm4_state *ac_create_shadowing_ib_preamble(const struct radeon_info *info,
                                                     uint64_t gpu_address,
                                                     bool dpbb_allowed);
#ifdef __cplusplus
}
#endif


#endif
