/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_SPM_CONFIG_H
#define AC_SPM_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "ac_spm.h"

struct ac_perfcounters;

/* Holds one user-defined SPM counter parsed from a config file.
 *
 * Each user counter expands to one or more raw counters in
 * ac_spm_user_config::create_infos[] (one per hardware instance). All raw
 * counters of the same user counter share the same id
 * (AC_SPM_RAW_COUNTER_ID_USER_BASE + counter_index) so they can be
 * aggregated together when building the derived trace.
 *
 * Each user counter is also auto-promoted to one pass-through derived
 * counter saved into the DERIVED_SPM_DB chunk; its per-sample value is
 * the aggregated raw value of the source counter.
 */
struct ac_spm_user_counter {
   /* Name from the config (left of '='). Owned. */
   char *name;

   /* Aggregation across instances for this counter. */
   enum ac_spm_raw_counter_op op;

   /* Index into ac_spm_user_config::groups. */
   uint32_t group_index;

   /* Range in ac_spm_user_config::create_infos[]. */
   uint32_t first_create_info;
   uint32_t num_create_infos;

   /* Descriptor used to populate the DERIVED_SPM_DB chunk. */
   struct ac_spm_derived_counter_descr derived_descr;
};

/* One [NAME] section. */
struct ac_spm_user_group {
   /* Display name shown in RGP. Owned. */
   char *name;

   /* Range in ac_spm_user_config::counters[] (HW counters declared in
    * this group). Each of those counters is also a pass-through derived
    * counter in the DERIVED_SPM_DB chunk.
    */
   uint32_t first_counter;
   uint32_t num_counters;

   /* Descriptor used to populate the DERIVED_SPM_DB chunk. */
   struct ac_spm_derived_group_descr derived_descr;
};

struct ac_spm_user_config {
   /* Backing arrays consumed by ac_init_spm(). One entry per (counter ×
    * instance) pair.
    */
   struct ac_spm_counter_descr *descrs;
   struct ac_spm_counter_create_info *create_infos;
   uint32_t num_create_infos;

   /* Per-counter metadata used by the derived trace builder. */
   struct ac_spm_user_counter *counters;
   uint32_t num_counters;

   /* Per-group metadata. */
   struct ac_spm_user_group *groups;
   uint32_t num_groups;
};

/* Loads SPM counters from a config file.
 *
 * File format (line-based):
 *
 *   [Cache]                                ; start a group named 'Cache'
 *   NAME=BLOCK,EVENT_ID,INSTANCE,OP        ; HW counters declared inside
 *                                          ;   the current group
 *   ...
 *
 * Rules:
 * - BLOCK is the textual ac_pc_gpu_block name (e.g. SQ_WGP, GL2C).
 * - EVENT_ID is decimal or hex (with 0x prefix).
 * - INSTANCE is a decimal integer or the keyword ALL (expands to every
 *   hardware instance of the block).
 * - OP is one of: sum, max, avg. 'avg' first sums the per-instance
 *   values like 'sum' and then divides by the number of instances the
 *   line expanded to.
 * - Group display names may be wrapped in double or single quotes,
 *   which allows characters like '=' or ']' to appear in the name
 *   (e.g. ["Memory (%)"]).
 * - Each HW counter is auto-promoted to a pass-through derived item
 *   shown in RGP under its group.
 *
 * Comments: '#' to end-of-line and C-style block comments
 * (including multi-line).
 *
 * Returns true on success and writes a newly-allocated config to *out.
 * Returns false on any error (prints a message to stderr) and leaves
 * *out unchanged.
 */
bool ac_spm_user_config_load(const char *path,
                             const struct ac_perfcounters *pc,
                             struct ac_spm_user_config **out);

void ac_spm_user_config_destroy(struct ac_spm_user_config *config);

#endif /* AC_SPM_CONFIG_H */
