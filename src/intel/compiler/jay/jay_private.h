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
#define JAY_DBG_NOACC       BITFIELD_BIT(4)
#define JAY_DBG_NOSCHED     BITFIELD_BIT(5)
#define JAY_DBG_STRICT      BITFIELD_BIT(6)
extern int jay_debug;

bool jay_nir_lower_bool(nir_shader *nir);
bool jay_nir_opt_sel_zero(nir_shader *nir);
bool jay_nir_lower_fsign(nir_shader *nir);
bool jay_nir_lower_bfloat_math(nir_shader *nir);

void jay_populate_prog_data(const struct intel_device_info *devinfo,
                            nir_shader *nir,
                            union brw_any_prog_data *prog_data,
                            union brw_any_prog_key *key,
                            unsigned nr_packed_regs);
unsigned jay_process_nir(const struct intel_device_info *devinfo,
                         nir_shader *nir,
                         union brw_any_prog_data *prog_data,
                         union brw_any_prog_key *key,
                         debug_archiver *archiver,
                         bool *track_helpers);

void jay_compute_liveness(jay_function *f);
void jay_calculate_register_demands(jay_function *f);

void jay_spill(jay_function *func, enum jay_file file, unsigned limit);
void jay_partition_grf(jay_shader *shader);
void jay_print_partition(struct jay_partition *p);
void jay_register_allocate(jay_shader *s);
void jay_assign_flags(jay_shader *s);
void jay_assign_accumulators(jay_shader *s);

const char *jay_file_prefix(enum jay_file file);
void jay_print_type(FILE *f, enum jay_type t);
void jay_print_inst(FILE *f, jay_inst *I);
void jay_print_block(FILE *f, jay_block *block);
void jay_print_func(FILE *fp, jay_function *func);
void jay_print(FILE *f, jay_shader *s);

#ifndef NDEBUG
void jay_archive(jay_shader *s, const char *name, unsigned idx);
void jay_validate(jay_shader *s, const char *when);
void jay_validate_ra(jay_function *func);
#else
static inline void
jay_archive(jay_shader *s, const char *name, unsigned idx)
{
}

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

void jay_schedule_pressure(jay_shader *s);

void jay_lower_pre_ra(jay_shader *s);
void jay_lower_post_ra(jay_shader *s);
void jay_lower_helpers(jay_shader *s);
void jay_lower_spill(jay_function *func);
void jay_lower_simd_width(jay_shader *s);
void jay_lower_scoreboard(jay_shader *s);
void jay_lower_scoreboard_trivial(jay_shader *s);
void
jay_lower_post_sched(jay_shader *shader, uint32_t api, uint32_t float_sizes);

gen_pipe jay_inferred_sync_pipe(const struct intel_device_info *devinfo,
                                const jay_inst *I);
gen_pipe jay_inst_exec_pipe(const struct intel_device_info *devinfo,
                            jay_inst *I);

unsigned jay_latency(jay_shader *s, jay_inst *I);
unsigned jay_estimate_cycles(jay_function *f);

struct jay_shader_bin *jay_to_binary(jay_shader *s,
                                     void *const_data,
                                     size_t const_data_size,
                                     bool debug);

/*
 * Check whether the Early EOT feature is possibly enabled. This feature was
 * removed in Xe3+. It exists on Xe2+ and fulsim enables it but real hardware
 * under xe.ko does not, so we gate on strict mode there. Pre-Xe2, it is always
 * enabled right now.
 */
static inline bool
jay_has_early_eot(jay_shader *s)
{
   return (s->devinfo->ver == 20 && (jay_debug & JAY_DBG_STRICT)) ||
          (s->devinfo->ver < 20);
}

static inline bool
jay_is_early_eot_send(jay_shader *s, const jay_inst *I)
{
   return I->op == JAY_OPCODE_SEND && jay_send_eot(I) && jay_has_early_eot(s);
}

#ifdef __cplusplus
} /* extern C */
#endif
