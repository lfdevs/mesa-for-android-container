/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "jay_ir.h"
#include "nir.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JAY_DBG_NOOPT       BITFIELD_BIT(0)
#define JAY_DBG_PRINTDEMAND BITFIELD_BIT(1)
#define JAY_DBG_SPILL       BITFIELD_BIT(2)
#define JAY_DBG_SYNC        BITFIELD_BIT(3)
extern int jay_debug;

bool jay_nir_lower_bool(nir_shader *nir);
bool jay_nir_opt_sel_zero(nir_shader *nir);
bool jay_nir_lower_fsign(nir_shader *nir);

void jay_populate_prog_data(const struct intel_device_info *devinfo,
                            nir_shader *nir,
                            union brw_any_prog_data *prog_data,
                            union brw_any_prog_key *key,
                            unsigned nr_packed_regs);
unsigned jay_process_nir(const struct intel_device_info *devinfo,
                         nir_shader *nir,
                         union brw_any_prog_data *prog_data,
                         union brw_any_prog_key *key);

void jay_compute_liveness(jay_function *f);
void jay_calculate_register_demands(jay_function *f);

void jay_spill(jay_function *func, unsigned limit);
void jay_partition_grf(jay_shader *shader);
void jay_register_allocate(jay_shader *s);
void jay_assign_flags(jay_shader *s);
void jay_assign_accumulators(jay_shader *s);
void jay_repair_ssa(jay_function *func);

const char *jay_file_prefix(enum jay_file file);
void jay_print_type(FILE *f, enum jay_type t);
void jay_print_inst(FILE *f, jay_inst *I);
void jay_print_block(FILE *f, jay_block *block);
void jay_print_func(FILE *fp, jay_function *func);
void jay_print(FILE *f, jay_shader *s);

#ifndef NDEBUG
void jay_validate(jay_shader *s, const char *when);
void jay_validate_ra(jay_function *func);
#else
static inline void
jay_validate(jay_shader *s, const char *when)
{
}

static inline void
jay_validate_ra(jay_function *func)
{
}
#endif

void jay_opt_propagate_forwards(jay_shader *s);
void jay_opt_propagate_backwards(jay_shader *s);
void jay_opt_dead_code(jay_shader *s);
void jay_opt_predicate(jay_shader *s);

void jay_lower_pre_ra(jay_shader *s);
void jay_lower_post_ra(jay_shader *s);
void jay_lower_spill(jay_function *func);
void jay_lower_simd_width(jay_shader *s);
void jay_lower_scoreboard(jay_shader *s);
void jay_insert_fp_mode(jay_shader *shader, uint32_t api, uint32_t float_sizes);

struct jay_shader_bin *jay_to_binary(jay_shader *s,
                                     void *const_data,
                                     size_t const_data_size,
                                     bool debug);

#ifdef __cplusplus
} /* extern C */
#endif
