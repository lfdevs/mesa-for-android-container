/* -*- c++ -*- */
/*
 * Copyright © 2021 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "brw_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

/* brw_reg_allocate.cpp */
void brw_alloc_reg_sets(struct brw_compiler *compiler);

/* brw_disasm.c */
extern const char *const conditional_modifier[16];
extern const char *const pred_ctrl_align16[16];

const unsigned *brw_compile_vs(const struct brw_compiler *compiler,
                               struct brw_compile_vs_params *params);
const unsigned *brw_compile_tcs(const struct brw_compiler *compiler,
                                struct brw_compile_tcs_params *params);
const unsigned *brw_compile_tes(const struct brw_compiler *compiler,
                                struct brw_compile_tes_params *params);
const unsigned *brw_compile_gs(const struct brw_compiler *compiler,
                               struct brw_compile_gs_params *params);
const unsigned *brw_compile_task(const struct brw_compiler *compiler,
                                 struct brw_compile_task_params *params);
const unsigned *brw_compile_mesh(const struct brw_compiler *compiler,
                                 struct brw_compile_mesh_params *params);
const unsigned *brw_compile_fs(const struct brw_compiler *compiler,
                               struct brw_compile_fs_params *params);
const unsigned *brw_compile_cs(const struct brw_compiler *compiler,
                               struct brw_compile_cs_params *params);
const unsigned *brw_compile_bs(const struct brw_compiler *compiler,
                               struct brw_compile_bs_params *params);

typedef struct brw_pass_tracker {
   nir_shader *nir;
   unsigned dispatch_width;

   const struct brw_compiler *compiler;

   const struct brw_base_prog_key *key;

   bool progress;

   /* Filled with the last line that made progress.
    * Used to perform early break in loops.
    * See BRW_NIR_LOOP_PASS macros below.
    */
   unsigned long opt_line;

   /* Tracking information for the debug archiver. */
   unsigned pass_num;
   debug_archiver *archiver;
} brw_pass_tracker;

#ifndef NDEBUG
void
brw_pass_tracker_archive(brw_pass_tracker *pt, const char *pass_name);
#else
static inline void
brw_pass_tracker_archive(brw_pass_tracker *pt, const char *pass_name)
{}
#endif

/* To be used in conjunction to BRW_NIR_LOOP_* macros. */
static inline void
pass_tracker_new_loop(brw_pass_tracker *pt)
{
   pt->opt_line = 0;
}

/* To be used in conjunction to BRW_NIR_LOOP_* macros. */
static inline void
pass_tracker_new_iteration(brw_pass_tracker *pt)
{
   pt->progress = false;
}

#define BRW_NIR_SNAPSHOT(name) do {                        \
   pt->pass_num++;                                         \
   brw_pass_tracker_archive(pt, name);                     \
} while (false);

#define BRW_NIR_PASS(pass, ...) ({                         \
   pt->pass_num++;                                         \
   bool this_progress = false;                             \
   NIR_PASS(this_progress, pt->nir, pass, ##__VA_ARGS__);  \
   if (this_progress) {                                    \
      pt->progress = true;                                 \
      if (unlikely(pt->archiver))                          \
         brw_pass_tracker_archive(pt, #pass);              \
   }                                                       \
   this_progress;                                          \
})

#define BRW_NIR_LOOP_PASS(pass, ...) ({                    \
   const unsigned long this_line = __LINE__;               \
   if (pt->opt_line == this_line) {                        \
      pt->pass_num++;                                      \
      break;                                               \
   }                                                       \
   bool this_progress = BRW_NIR_PASS(pass, ##__VA_ARGS__); \
   if (this_progress)                                      \
      pt->opt_line = this_line;                            \
   this_progress;                                          \
})

#define BRW_NIR_LOOP_PASS_NOT_IDEMPOTENT(pass, ...) ({     \
   bool this_progress = BRW_NIR_PASS(pass, ##__VA_ARGS__); \
   if (this_progress)                                      \
      pt->opt_line = 0;                                    \
   this_progress;                                          \
})

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include <variant>

unsigned brw_required_dispatch_width(const struct shader_info *info);

static constexpr int SIMD_COUNT = 3;

struct brw_simd_selection_state {
   const struct intel_device_info *devinfo;

   std::variant<struct brw_cs_prog_data *,
                struct brw_bs_prog_data *> prog_data;

   unsigned required_width;

   const char *error[SIMD_COUNT];

   bool compiled[SIMD_COUNT];
   bool spilled[SIMD_COUNT];
   bool beyond_threshold[SIMD_COUNT];
};

inline int brw_simd_first_compiled(const brw_simd_selection_state &state)
{
   for (int i = 0; i < SIMD_COUNT; i++) {
      if (state.compiled[i])
         return i;
   }
   return -1;
}

inline bool brw_simd_any_compiled(const brw_simd_selection_state &state)
{
   return brw_simd_first_compiled(state) >= 0;
}

unsigned brw_geometry_stage_dispatch_width(const struct intel_device_info *devinfo);

bool brw_simd_should_compile(brw_simd_selection_state &state, unsigned simd);

void brw_simd_mark_compiled(brw_simd_selection_state &state, unsigned simd, bool spilled);

int brw_simd_select(const brw_simd_selection_state &state);

int brw_simd_select_for_workgroup_size(const struct intel_device_info *devinfo,
                                       const struct brw_cs_prog_data *prog_data,
                                       const unsigned *sizes);

bool brw_should_print_shader(const nir_shader *shader, uint64_t debug_flag, uint32_t source_hash);

void brw_prog_data_init(struct brw_stage_prog_data *prog_data,
                        const struct brw_compile_params *params);

#endif // __cplusplus
