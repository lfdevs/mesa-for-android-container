/*
 * Copyright 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_SPM_H
#define AC_SPM_H

#include <stdint.h>

#include "ac_perfcounter.h"

#define AC_SPM_MAX_COUNTER_PER_BLOCK 16
#define AC_SPM_GLOBAL_TIMESTAMP_COUNTERS 4 /* in unit of 16-bit counters*/
#define AC_SPM_NUM_COUNTER_PER_MUXSEL 16 /* 16 16-bit counters per muxsel */
#define AC_SPM_MUXSEL_LINE_SIZE ((AC_SPM_NUM_COUNTER_PER_MUXSEL * 2) / 4) /* in dwords */
#define AC_SPM_NUM_PERF_SEL 4

enum ac_spm_segment_type {
   AC_SPM_SEGMENT_TYPE_SE0,
   AC_SPM_SEGMENT_TYPE_SE1,
   AC_SPM_SEGMENT_TYPE_SE2,
   AC_SPM_SEGMENT_TYPE_SE3,
   AC_SPM_SEGMENT_TYPE_GLOBAL,
   AC_SPM_SEGMENT_TYPE_COUNT,
};

struct ac_spm_counter_create_info {
   enum ac_pc_gpu_block gpu_block;
   uint32_t instance;
   uint32_t event_id;
};

struct ac_spm_muxsel {
   uint16_t counter      : 6;
   uint16_t block        : 4;
   uint16_t shader_array : 1; /* 0: SA0, 1: SA1 */
   uint16_t instance     : 5;
};

struct ac_spm_muxsel_line {
   struct ac_spm_muxsel muxsel[AC_SPM_NUM_COUNTER_PER_MUXSEL];
};

struct ac_spm_counter_info {
   /* General info. */
   enum ac_pc_gpu_block gpu_block;
   uint32_t instance;
   uint32_t event_id;

   /* Muxsel info. */
   enum ac_spm_segment_type segment_type;
   bool is_even;
   struct ac_spm_muxsel muxsel;

   /* Output info. */
   uint64_t offset;
};

struct ac_spm_counter_select {
   uint8_t active; /* mask of used 16-bit counters. */
   uint32_t sel0;
   uint32_t sel1;
};

struct ac_spm_block_select {
   const struct ac_pc_block *b;
   uint32_t grbm_gfx_index;

   uint32_t num_counters;
   struct ac_spm_counter_select counters[AC_SPM_MAX_COUNTER_PER_BLOCK];
};

struct ac_spm {
   /* struct radeon_winsys_bo or struct pb_buffer */
   void *bo;
   void *ptr;
   uint32_t buffer_size;
   uint16_t sample_interval;

   /* Enabled counters. */
   unsigned num_counters;
   struct ac_spm_counter_info *counters;

   /* Block/counters selection. */
   uint32_t num_block_sel;
   struct ac_spm_block_select *block_sel;
   uint32_t num_used_sq_block_sel;
   struct ac_spm_block_select sq_block_sel[16];

   /* Muxsel lines. */
   unsigned num_muxsel_lines[AC_SPM_SEGMENT_TYPE_COUNT];
   struct ac_spm_muxsel_line *muxsel_lines[AC_SPM_SEGMENT_TYPE_COUNT];
};

struct ac_spm_trace {
   void *ptr;
   uint16_t sample_interval;
   unsigned num_counters;
   struct ac_spm_counter_info *counters;
   uint32_t sample_size_in_bytes;
   uint32_t num_samples;
};

bool ac_init_spm(const struct radeon_info *info,
                 const struct ac_perfcounters *pc,
                 unsigned num_counters,
                 const struct ac_spm_counter_create_info *counters,
                 struct ac_spm *spm);
void ac_destroy_spm(struct ac_spm *spm);

void ac_spm_get_trace(const struct ac_spm *spm, struct ac_spm_trace *trace);

#endif
