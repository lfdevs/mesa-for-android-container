/*
 * Copyright 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_SPM_H
#define AC_SPM_H

#include <stdint.h>

#include "ac_perfcounter.h"

#include "util/u_dynarray.h"

struct ac_cmdbuf;

#define AC_SPM_MAX_COUNTER_PER_BLOCK 16
#define AC_SPM_GLOBAL_TIMESTAMP_COUNTERS 4 /* in unit of 16-bit counters*/
#define AC_SPM_NUM_COUNTER_PER_MUXSEL 16 /* 16 16-bit counters per muxsel */
#define AC_SPM_MUXSEL_LINE_SIZE ((AC_SPM_NUM_COUNTER_PER_MUXSEL * 2) / 4) /* in dwords */
#define AC_SPM_NUM_PERF_SEL 4

#define AC_SPM_RING_BASE_ALIGN 32

/* GFX10+ */
enum ac_spm_global_block {
    AC_SPM_GLOBAL_BLOCK_CPG,
    AC_SPM_GLOBAL_BLOCK_CPC,
    AC_SPM_GLOBAL_BLOCK_CPF,
    AC_SPM_GLOBAL_BLOCK_GDS,
    AC_SPM_GLOBAL_BLOCK_GCR,
    AC_SPM_GLOBAL_BLOCK_PH,
    AC_SPM_GLOBAL_BLOCK_GE,
    AC_SPM_GLOBAL_BLOCK_GE1 = AC_SPM_GLOBAL_BLOCK_GE,
    AC_SPM_GLOBAL_BLOCK_GL2A,
    AC_SPM_GLOBAL_BLOCK_GL2C,
    AC_SPM_GLOBAL_BLOCK_SDMA,
    AC_SPM_GLOBAL_BLOCK_GUS,
    AC_SPM_GLOBAL_BLOCK_GCEA,
    AC_SPM_GLOBAL_BLOCK_GCEA_CPWD = AC_SPM_GLOBAL_BLOCK_GCEA,
    AC_SPM_GLOBAL_BLOCK_GCEA_SE = AC_SPM_GLOBAL_BLOCK_GCEA,
    AC_SPM_GLOBAL_BLOCK_CHA,
    AC_SPM_GLOBAL_BLOCK_CHC,
    AC_SPM_GLOBAL_BLOCK_CHCG,
    AC_SPM_GLOBAL_BLOCK_GPUVMATCL2,
    AC_SPM_GLOBAL_BLOCK_GPUVMVML2,
    AC_SPM_GLOBAL_BLOCK_GE2SE, /* Per-SE counters */
    AC_SPM_GLOBAL_BLOCK_GE2DIST,

    /* GFX11+ */
    /* gap */
    AC_SPM_GLOBAL_BLOCK_RSPM = 31,
};

enum ac_spm_se_block {
    AC_SPM_SE_BLOCK_CB,
    AC_SPM_SE_BLOCK_DB,
    AC_SPM_SE_BLOCK_PA,
    AC_SPM_SE_BLOCK_SX,
    AC_SPM_SE_BLOCK_SC,
    AC_SPM_SE_BLOCK_TA,
    AC_SPM_SE_BLOCK_TD,
    AC_SPM_SE_BLOCK_TCP,
    AC_SPM_SE_BLOCK_SPI,
    AC_SPM_SE_BLOCK_SQG,
    AC_SPM_SE_BLOCK_GL1A,
    AC_SPM_SE_BLOCK_RMI,
    AC_SPM_SE_BLOCK_GL1C,
    AC_SPM_SE_BLOCK_GL1CG,

    /* GFX11+ */
    AC_SPM_SE_BLOCK_CBR,
    AC_SPM_SE_BLOCK_DBR,
    AC_SPM_SE_BLOCK_GL1H,
    AC_SPM_SE_BLOCK_SQC,
    AC_SPM_SE_BLOCK_PC,
    AC_SPM_SE_BLOCK_EA,
    AC_SPM_SE_BLOCK_GE,
    AC_SPM_SE_BLOCK_GL2A,
    AC_SPM_SE_BLOCK_GL2C,
    AC_SPM_SE_BLOCK_WGS,
    AC_SPM_SE_BLOCK_GL1XA,
    AC_SPM_SE_BLOCK_GL1XC,
    AC_SPM_SE_BLOCK_UTCL1,
    /* gap */
    AC_SPM_SE_BLOCK_SE_RPM = 31,
};

enum ac_spm_segment_type {
   AC_SPM_SEGMENT_TYPE_SE0,
   AC_SPM_SEGMENT_TYPE_SE1,
   AC_SPM_SEGMENT_TYPE_SE2,
   AC_SPM_SEGMENT_TYPE_SE3,
   AC_SPM_SEGMENT_TYPE_SE4,
   AC_SPM_SEGMENT_TYPE_SE5,
   AC_SPM_SEGMENT_TYPE_GLOBAL,
   AC_SPM_SEGMENT_TYPE_COUNT,
};

enum ac_spm_raw_counter_id {
   AC_SPM_TCP_PERF_SEL_REQ = 0,
   AC_SPM_TCP_PERF_SEL_REQ_MISS,
   AC_SPM_SQC_PERF_SEL_DCACHE_HITS,
   AC_SPM_SQC_PERF_SEL_DCACHE_MISSES,
   AC_SPM_SQC_PERF_SEL_DCACHE_MISSES_DUPLICATE,
   AC_SPM_SQC_PERF_SEL_ICACHE_HITS,
   AC_SPM_SQC_PERF_SEL_ICACHE_MISSES,
   AC_SPM_SQC_PERF_SEL_ICACHE_MISSES_DUPLICATE,
   AC_SPM_GL1C_PERF_SEL_REQ,
   AC_SPM_GL1C_PERF_SEL_REQ_MISS,
   AC_SPM_GL2C_PERF_SEL_REQ,
   AC_SPM_GL2C_PERF_SEL_MISS,
   AC_SPM_CPF_PERF_SEL_STAT_BUSY,
   AC_SPM_SQC_PERF_SEL_LDS_BANK_CONFLICT,
   AC_SPM_GL2C_PERF_SEL_EA_RDREQ_32B,
   AC_SPM_GL2C_PERF_SEL_EA_RDREQ_64B,
   AC_SPM_GL2C_PERF_SEL_EA_RDREQ_96B,
   AC_SPM_GL2C_PERF_SEL_EA_RDREQ_128B,
   AC_SPM_GL2C_PERF_SEL_EA_RDREQ_256B,
   AC_SPM_GL2C_PERF_SEL_EA_WRREQ,
   AC_SPM_GL2C_PERF_SEL_EA_WRREQ_64B,
   AC_SPM_GCEA_PERF_SEL_SARB_DRAM_SIZED_REQUESTS,
   AC_SPM_GCEA_CPWD_PERF_SEL_SARB_DRAM_RD_SIZE_REQ,
   AC_SPM_GCEA_CPWD_PERF_SEL_SARB_DRAM_WR_SIZE_REQ,
   AC_SPM_GCEA_PERF_SEL_SARB_IO_SIZED_REQUESTS,
   AC_SPM_GCEA_SE_PERF_SEL_SARB_IO_RD_SIZE_REQ,
   AC_SPM_GCEA_SE_PERF_SEL_SARB_IO_WR_SIZE_REQ,
   AC_SPM_TA_PERF_SEL_TA_BUSY,
   AC_SPM_TCP_PERF_SEL_TCP_TA_REQ_STALL,
   AC_SPM_TD_PERF_SEL_RAY_TRACING_BVH4_TRI_NODE,
   AC_SPM_TD_PERF_SEL_RAY_TRACING_BVH4_FP16_BOX_NODE,
   AC_SPM_TD_PERF_SEL_RAY_TRACING_BVH4_FP32_BOX_NODE,
   AC_SPM_GL2C_PERF_SEL_EA_WRREQ_STALL,
   AC_SPM_RAW_COUNTER_ID_COUNT,

   /* Runtime base for user-defined raw counter ids loaded from a config
    * file. The actual ids are AC_SPM_RAW_COUNTER_ID_USER_BASE + N, where
    * N is the user counter index. Kept well above the builtin enum so it
    * can never collide.
    */
   AC_SPM_RAW_COUNTER_ID_USER_BASE = 0x10000,
};

enum ac_spm_raw_counter_op {
   AC_SPM_RAW_COUNTER_OP_SUM = 0,
   AC_SPM_RAW_COUNTER_OP_MAX,
   AC_SPM_RAW_COUNTER_OP_AVG,
};

struct ac_spm_counter_descr {
   enum ac_spm_raw_counter_id id;
   enum ac_pc_gpu_block gpu_block;
   uint32_t event_id;
};

struct ac_spm_counter_create_info {
   struct ac_spm_counter_descr *b;
   uint32_t instance;
};

union ac_spm_muxsel {
   struct {
      uint16_t counter      : 6;
      uint16_t block        : 4;
      uint16_t shader_array : 1; /* 0: SA0, 1: SA1 */
      uint16_t instance     : 5;
   } gfx10;

   struct {
      uint16_t counter      : 5;
      uint16_t instance     : 5;
      uint16_t shader_array : 1;
      uint16_t block        : 5;
   } gfx11;
   uint16_t value;
};

struct ac_spm_muxsel_line {
   union ac_spm_muxsel muxsel[AC_SPM_NUM_COUNTER_PER_MUXSEL];
};

struct ac_spm_counter_info {
   /* General info. */
   enum ac_spm_raw_counter_id id;
   enum ac_pc_gpu_block gpu_block;
   uint32_t instance;
   uint32_t event_id;

   /* Muxsel info. */
   enum ac_spm_segment_type segment_type;
   bool is_even;
   union ac_spm_muxsel muxsel;

   /* Output info. */
   uint64_t offset;
};

struct ac_spm_counter_select {
   uint8_t active; /* mask of used 16-bit counters. */
   uint32_t sel0;
   uint32_t sel1;
};

struct ac_spm_block_instance {
   uint32_t grbm_gfx_index;

   uint32_t num_counters;
   struct ac_spm_counter_select counters[AC_SPM_MAX_COUNTER_PER_BLOCK];
};

struct ac_spm_block_select {
   const struct ac_pc_block *b;

   uint32_t num_instances;
   struct ac_spm_block_instance *instances;
};

struct ac_spm {
   /* struct radeon_winsys_bo or struct si_resource */
   void *bo;
   void *ptr;
   uint8_t ptr_granularity;
   uint32_t buffer_size;
   uint16_t sample_interval;

   /* Enabled counters. */
   unsigned num_counters;
   struct ac_spm_counter_info *counters;

   /* Block/counters selection. */
   uint32_t num_block_sel;
   struct ac_spm_block_select *block_sel;

   struct {
      uint32_t num_counters;
      struct ac_spm_counter_select counters[16];
   } sqg[AC_SPM_SEGMENT_TYPE_GLOBAL];

   struct {
      uint32_t grbm_gfx_index;
      uint32_t num_counters;
      struct ac_spm_counter_select counters[16];
   } sq_wgp[AMD_MAX_WGP];

   /* Muxsel lines. */
   unsigned num_muxsel_lines[AC_SPM_SEGMENT_TYPE_COUNT];
   struct ac_spm_muxsel_line *muxsel_lines[AC_SPM_SEGMENT_TYPE_COUNT];
   unsigned max_se_muxsel_lines;

   /* Optional user-supplied counter config (loaded from
    * RADV_SPM_COUNTERS_CONFIG). When non-NULL, ac_spm_get_derived_trace
    * emits user-named counters/groups into the derived SPM chunk. Not
    * owned by ac_spm; the caller is responsible for keeping it alive
    * until ac_destroy_spm() returns and for destroying it afterwards.
    */
   const struct ac_spm_user_config *user_config;
};

struct ac_spm_trace {
   void *ptr;
   uint16_t sample_interval;
   unsigned num_counters;
   struct ac_spm_counter_info *counters;
   uint32_t sample_size_in_bytes;
   uint32_t num_samples;
   /* Mirrors ac_spm::user_config; set by ac_spm_get_trace. */
   const struct ac_spm_user_config *user_config;
};

enum ac_spm_group_id {
   AC_SPM_GROUP_CACHE,
   AC_SPM_GROUP_LDS,
   AC_SPM_GROUP_MEMORY_BYTES,
   AC_SPM_GROUP_MEMORY_PERCENTAGE,
   AC_SPM_GROUP_RT,
   AC_SPM_GROUP_COUNT,
};

enum ac_spm_counter_id {
   AC_SPM_COUNTER_INST_CACHE_HIT,
   AC_SPM_COUNTER_SCALAR_CACHE_HIT,
   AC_SPM_COUNTER_L0_CACHE_HIT,
   AC_SPM_COUNTER_L1_CACHE_HIT, /* < GFX12 */
   AC_SPM_COUNTER_L2_CACHE_HIT,
   AC_SPM_COUNTER_CS_LDS_BANK_CONFLICT,
   AC_SPM_COUNTER_FETCH_SIZE,
   AC_SPM_COUNTER_WRITE_SIZE,
   AC_SPM_COUNTER_LOCAL_VID_MEM_BYTES,
   AC_SPM_COUNTER_PCIE_BYTES,
   AC_SPM_COUNTER_MEM_UNIT_BUSY,
   AC_SPM_COUNTER_MEM_UNIT_STALLED,
   AC_SPM_COUNTER_RAY_BOX_TESTS,
   AC_SPM_COUNTER_RAY_TRI_TESTS,
   AC_SPM_COUNTER_WRITE_UNIT_STALLED, /* GFX12+ */
   AC_SPM_COUNTER_COUNT,
};

enum ac_spm_component_id {
   AC_SPM_COMPONENT_INST_CACHE_REQUEST_COUNT,
   AC_SPM_COMPONENT_INST_CACHE_HIT_COUNT,
   AC_SPM_COMPONENT_INST_CACHE_MISS_COUNT,
   AC_SPM_COMPONENT_SCALAR_CACHE_REQUEST_COUNT,
   AC_SPM_COMPONENT_SCALAR_CACHE_HIT_COUNT,
   AC_SPM_COMPONENT_SCALAR_CACHE_MISS_COUNT,
   AC_SPM_COMPONENT_L0_CACHE_REQUEST_COUNT,
   AC_SPM_COMPONENT_L0_CACHE_HIT_COUNT,
   AC_SPM_COMPONENT_L0_CACHE_MISS_COUNT,
   AC_SPM_COMPONENT_L1_CACHE_REQUEST_COUNT,  /* < GFX12 */
   AC_SPM_COMPONENT_L1_CACHE_HIT_COUNT,      /* < GFX12 */
   AC_SPM_COMPONENT_L1_CACHE_MISS_COUNT,     /* < GFX12 */
   AC_SPM_COMPONENT_L2_CACHE_REQUEST_COUNT,
   AC_SPM_COMPONENT_L2_CACHE_HIT_COUNT,
   AC_SPM_COMPONENT_L2_CACHE_MISS_COUNT,
   AC_SPM_COMPONENT_GPU_BUSY_CYCLES,
   AC_SPM_COMPONENT_CS_LDS_BANK_CONFLICT_CYCLES,
   AC_SPM_COMPONENT_MEM_UNIT_BUSY_CYCLES,
   AC_SPM_COMPONENT_MEM_UNIT_STALLED_CYCLES,
   AC_SPM_COMPONENT_WRITE_UNIT_STALLED_CYCLES, /* GFX12+ */
   AC_SPM_COMPONENT_COUNT,
};

enum ac_spm_usage_type {
   AC_SPM_USAGE_PERCENTAGE = 1,
   AC_SPM_USAGE_CYCLES = 2,
   AC_SPM_USAGE_BYTES = 4,
   AC_SPM_USAGE_ITEMS = 5,
};

#define AC_SPM_MAX_COMPONENTS_PER_COUNTER 3
#define AC_SPM_MAX_COUNTERS_PER_GROUP     16

struct ac_spm_derived_component_descr {
   enum ac_spm_component_id id;
   enum ac_spm_counter_id counter_id;
   const char *name;
   enum ac_spm_usage_type usage;
};

struct ac_spm_derived_counter_descr {
   enum ac_spm_counter_id id;
   enum ac_spm_group_id group_id;
   const char *name;
   const char *desc;
   enum ac_spm_usage_type usage;
   uint32_t num_components;
   struct ac_spm_derived_component_descr *components[AC_SPM_MAX_COMPONENTS_PER_COUNTER];
};

struct ac_spm_derived_group_descr {
   enum ac_spm_group_id id;
   const char *name;
   uint32_t num_counters;
   struct ac_spm_derived_counter_descr *counters[AC_SPM_MAX_COUNTERS_PER_GROUP];
};

struct ac_spm_derived_group {
   const struct ac_spm_derived_group_descr *descr;
   uint32_t counter_ids[AC_SPM_MAX_COUNTERS_PER_GROUP];
};

struct ac_spm_derived_counter {
   const struct ac_spm_derived_counter_descr *descr;
   uint32_t component_ids[AC_SPM_MAX_COMPONENTS_PER_COUNTER];

   struct util_dynarray values;
};

struct ac_spm_derived_component {
   const struct ac_spm_derived_component_descr *descr;

   struct util_dynarray values;
};

/* Maximum number of groups that can appear in a derived trace. The built-in
 * legacy descriptors use exactly AC_SPM_GROUP_COUNT entries; user-defined
 * configs (RADV_SPM_COUNTERS_CONFIG) are allowed to grow this further. Must
 * be >= AC_SPM_GROUP_COUNT.
 */
#define AC_SPM_DERIVED_GROUP_MAX 8
static_assert(AC_SPM_DERIVED_GROUP_MAX >= AC_SPM_GROUP_COUNT,
              "AC_SPM_DERIVED_GROUP_MAX must cover all built-in groups");

/* Maximum number of counters (items) that can appear in a derived trace.
 * Same rationale as AC_SPM_DERIVED_GROUP_MAX. Must be >= AC_SPM_COUNTER_COUNT.
 */
#define AC_SPM_DERIVED_COUNTER_MAX 48
static_assert(AC_SPM_DERIVED_COUNTER_MAX >= AC_SPM_COUNTER_COUNT,
              "AC_SPM_DERIVED_COUNTER_MAX must cover all built-in counters");

struct ac_spm_derived_trace {
   uint32_t num_timestamps;
   uint64_t *timestamps;

   uint32_t num_groups;
   struct ac_spm_derived_group groups[AC_SPM_DERIVED_GROUP_MAX];

   uint32_t num_counters;
   struct ac_spm_derived_counter counters[AC_SPM_DERIVED_COUNTER_MAX];

   uint32_t num_components;
   struct ac_spm_derived_component components[AC_SPM_COMPONENT_COUNT];

   uint32_t sample_interval;
};

struct ac_spm_user_config;

bool ac_init_spm(const struct radeon_info *info,
                 const struct ac_perfcounters *pc,
                 struct ac_spm *spm);
void ac_destroy_spm(struct ac_spm *spm);

bool ac_spm_get_trace(const struct ac_spm *spm, struct ac_spm_trace *trace);

struct ac_spm_derived_trace *
ac_spm_get_derived_trace(const struct radeon_info *info,
                         const struct ac_spm_trace *spm_trace);

void
ac_spm_destroy_derived_trace(struct ac_spm_derived_trace *spm_derived_trace);

void
ac_emit_spm_setup(struct ac_cmdbuf *cs, enum amd_gfx_level gfx_level,
                  enum amd_ip_type ip_type, const struct ac_spm *spm,
                  uint64_t va);

void
ac_emit_spm_start(struct ac_cmdbuf *cs, enum amd_ip_type ip_type,
                  const struct radeon_info *info);

void
ac_emit_spm_stop(struct ac_cmdbuf *cs, enum amd_ip_type ip_type,
                 const struct radeon_info *info);

void
ac_emit_spm_reset(struct ac_cmdbuf *cs);

#endif
