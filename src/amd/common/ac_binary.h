/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_BINARY_H
#define AC_BINARY_H

#include <stdbool.h>
#include <stddef.h>

#include "amd_family.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ac_compiler_info;

struct ac_shader_config {
   unsigned num_sgprs;
   unsigned num_vgprs;
   unsigned num_shared_vgprs; /* GFX10: number of VGPRs shared between half-waves */
   unsigned spilled_sgprs;
   unsigned spilled_vgprs;
   unsigned lds_size; /* in bytes */
   unsigned spi_ps_input_ena;
   unsigned spi_ps_input_addr;
   unsigned float_mode;
   unsigned scratch_bytes_per_wave;
   bool wgp_mode;
   unsigned rsrc1;
   unsigned rsrc2;
   unsigned rsrc3;
};

void ac_parse_llvm_binary_config(const char *data, size_t nbytes, unsigned wave_size,
                                 const struct ac_compiler_info *compiler_info,
                                 struct ac_shader_config *conf);

unsigned ac_align_shader_binary_for_prefetch(enum amd_gfx_level gfx_level,
                                             unsigned prefetch_distance,
                                             unsigned size);

unsigned ac_get_instr_prefetch_size(enum amd_gfx_level gfx_level,
                                    unsigned prefetch_distance,
                                    unsigned size);

#ifdef __cplusplus
}
#endif

#endif /* AC_BINARY_H */
