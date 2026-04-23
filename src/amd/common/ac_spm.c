/*
 * Copyright 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_cmdbuf.h"
#include "ac_cmdbuf_cp.h"
#include "ac_spm.h"

#include "util/bitscan.h"
#include "util/compiler.h"
#include "util/u_memory.h"
#include "ac_perfcounter.h"

/* SPM counters definition. */
/* GFX10+ */
static struct ac_spm_counter_descr gfx10_tcp_perf_sel_req =
   {AC_SPM_TCP_PERF_SEL_REQ, TCP, 0x9};
static struct ac_spm_counter_descr gfx10_tcp_perf_sel_req_miss =
   {AC_SPM_TCP_PERF_SEL_REQ_MISS, TCP, 0x12};
static struct ac_spm_counter_descr gfx10_sqc_perf_sel_dcache_hits =
   {AC_SPM_SQC_PERF_SEL_DCACHE_HITS, SQ, 0x14f};
static struct ac_spm_counter_descr gfx10_sqc_perf_sel_dcache_misses =
   {AC_SPM_SQC_PERF_SEL_DCACHE_MISSES, SQ, 0x150};
static struct ac_spm_counter_descr gfx10_sqc_perf_sel_dcache_misses_duplicate =
   {AC_SPM_SQC_PERF_SEL_DCACHE_MISSES_DUPLICATE, SQ, 0x151};
static struct ac_spm_counter_descr gfx10_sqc_perf_sel_icache_hits =
   {AC_SPM_SQC_PERF_SEL_ICACHE_HITS, SQ, 0x12c};
static struct ac_spm_counter_descr gfx10_sqc_perf_sel_icache_misses =
   {AC_SPM_SQC_PERF_SEL_ICACHE_MISSES, SQ, 0x12d};
static struct ac_spm_counter_descr gfx10_sqc_perf_sel_icache_misses_duplicate =
   {AC_SPM_SQC_PERF_SEL_ICACHE_MISSES_DUPLICATE, SQ, 0x12e};
static struct ac_spm_counter_descr gfx10_gl1c_perf_sel_req =
   {AC_SPM_GL1C_PERF_SEL_REQ, GL1C, 0xe};
static struct ac_spm_counter_descr gfx10_gl1c_perf_sel_req_miss =
   {AC_SPM_GL1C_PERF_SEL_REQ_MISS, GL1C, 0x12};
static struct ac_spm_counter_descr gfx10_gl2c_perf_sel_req =
   {AC_SPM_GL2C_PERF_SEL_REQ, GL2C, 0x3};
static struct ac_spm_counter_descr gfx10_gl2c_perf_sel_miss =
   {AC_SPM_GL2C_PERF_SEL_MISS, GL2C, 0x23};
static struct ac_spm_counter_descr gfx10_cpf_perf_sel_stat_busy =
   {AC_SPM_CPF_PERF_SEL_STAT_BUSY, CPF, 0x18};
static struct ac_spm_counter_descr gfx10_sqc_perf_sel_lds_bank_conflict =
   {AC_SPM_SQC_PERF_SEL_LDS_BANK_CONFLICT, SQ, 0x11d};
static struct ac_spm_counter_descr gfx10_gl2c_perf_sel_ea_rdreq_32b =
   {AC_SPM_GL2C_PERF_SEL_EA_RDREQ_32B, GL2C, 0x59};
static struct ac_spm_counter_descr gfx10_gl2c_perf_sel_ea_rdreq_64b =
   {AC_SPM_GL2C_PERF_SEL_EA_RDREQ_64B, GL2C, 0x5a};
static struct ac_spm_counter_descr gfx10_gl2c_perf_sel_ea_rdreq_96b =
   {AC_SPM_GL2C_PERF_SEL_EA_RDREQ_96B, GL2C, 0x5b};
static struct ac_spm_counter_descr gfx10_gl2c_perf_sel_ea_rdreq_128b =
   {AC_SPM_GL2C_PERF_SEL_EA_RDREQ_128B, GL2C, 0x5c};
static struct ac_spm_counter_descr gfx10_gl2c_perf_sel_ea_wrreq =
   {AC_SPM_GL2C_PERF_SEL_EA_WRREQ, GL2C, 0x4b};
static struct ac_spm_counter_descr gfx10_gl2c_perf_sel_ea_wrreq_64b =
   {AC_SPM_GL2C_PERF_SEL_EA_WRREQ_64B, GL2C, 0x4c};
static struct ac_spm_counter_descr gfx10_gcea_perf_sel_sarb_dram_sized_requests =
   {AC_SPM_GCEA_PERF_SEL_SARB_DRAM_SIZED_REQUESTS, GCEA, 0x37};
static struct ac_spm_counter_descr gfx10_gcea_perf_sel_sarb_io_sized_requests =
   {AC_SPM_GCEA_PERF_SEL_SARB_IO_SIZED_REQUESTS, GCEA, 0x39};
static struct ac_spm_counter_descr gfx10_ta_perf_sel_ta_busy =
   {AC_SPM_TA_PERF_SEL_TA_BUSY, TA, 0xf};
static struct ac_spm_counter_descr gfx10_tcp_perf_sel_tcp_ta_req_stall =
   {AC_SPM_TCP_PERF_SEL_TCP_TA_REQ_STALL, TCP, 0x24};

static struct ac_spm_counter_create_info gfx10_spm_counters[] = {
   {&gfx10_tcp_perf_sel_req},
   {&gfx10_tcp_perf_sel_req_miss},
   {&gfx10_sqc_perf_sel_dcache_hits},
   {&gfx10_sqc_perf_sel_dcache_misses},
   {&gfx10_sqc_perf_sel_dcache_misses_duplicate},
   {&gfx10_sqc_perf_sel_icache_hits},
   {&gfx10_sqc_perf_sel_icache_misses},
   {&gfx10_sqc_perf_sel_icache_misses_duplicate},
   {&gfx10_gl1c_perf_sel_req},
   {&gfx10_gl1c_perf_sel_req_miss},
   {&gfx10_gl2c_perf_sel_req},
   {&gfx10_gl2c_perf_sel_miss},
   {&gfx10_cpf_perf_sel_stat_busy},
   {&gfx10_sqc_perf_sel_lds_bank_conflict},
   {&gfx10_gl2c_perf_sel_ea_rdreq_32b},
   {&gfx10_gl2c_perf_sel_ea_rdreq_64b},
   {&gfx10_gl2c_perf_sel_ea_rdreq_96b},
   {&gfx10_gl2c_perf_sel_ea_rdreq_128b},
   {&gfx10_gl2c_perf_sel_ea_wrreq},
   {&gfx10_gl2c_perf_sel_ea_wrreq_64b},
   {&gfx10_gcea_perf_sel_sarb_dram_sized_requests},
   {&gfx10_gcea_perf_sel_sarb_io_sized_requests},
   {&gfx10_ta_perf_sel_ta_busy},
   {&gfx10_tcp_perf_sel_tcp_ta_req_stall},
};

/* GFX10.3+ */
static struct ac_spm_counter_descr gfx103_gl2c_perf_sel_miss =
   {AC_SPM_GL2C_PERF_SEL_MISS, GL2C, 0x2b};
static struct ac_spm_counter_descr gfx103_gl2c_perf_sel_ea_rdreq_32b =
   {AC_SPM_GL2C_PERF_SEL_EA_RDREQ_32B, GL2C, 0x63};
static struct ac_spm_counter_descr gfx103_gl2c_perf_sel_ea_rdreq_64b =
   {AC_SPM_GL2C_PERF_SEL_EA_RDREQ_64B, GL2C, 0x64};
static struct ac_spm_counter_descr gfx103_gl2c_perf_sel_ea_rdreq_96b =
   {AC_SPM_GL2C_PERF_SEL_EA_RDREQ_96B, GL2C, 0x65};
static struct ac_spm_counter_descr gfx103_gl2c_perf_sel_ea_rdreq_128b =
   {AC_SPM_GL2C_PERF_SEL_EA_RDREQ_128B, GL2C, 0x66};
static struct ac_spm_counter_descr gfx103_gl2c_perf_sel_ea_wrreq =
   {AC_SPM_GL2C_PERF_SEL_EA_WRREQ, GL2C, 0x53};
static struct ac_spm_counter_descr gfx103_gl2c_perf_sel_ea_wrreq_64b =
   {AC_SPM_GL2C_PERF_SEL_EA_WRREQ_64B, GL2C, 0x55};
static struct ac_spm_counter_descr gfx103_td_perf_sel_ray_tracing_bvh4_tri_node =
   {AC_SPM_TD_PERF_SEL_RAY_TRACING_BVH4_TRI_NODE, TD, 0x76};
static struct ac_spm_counter_descr gfx103_td_perf_sel_ray_tracing_bvh4_fp16_box_node =
   {AC_SPM_TD_PERF_SEL_RAY_TRACING_BVH4_FP16_BOX_NODE, TD, 0x74};
static struct ac_spm_counter_descr gfx103_td_perf_sel_ray_tracing_bvh4_fp32_box_node =
   {AC_SPM_TD_PERF_SEL_RAY_TRACING_BVH4_FP32_BOX_NODE, TD, 0x75};

static struct ac_spm_counter_create_info gfx103_spm_counters[] = {
   {&gfx10_tcp_perf_sel_req},
   {&gfx10_tcp_perf_sel_req_miss},
   {&gfx10_sqc_perf_sel_dcache_hits},
   {&gfx10_sqc_perf_sel_dcache_misses},
   {&gfx10_sqc_perf_sel_dcache_misses_duplicate},
   {&gfx10_sqc_perf_sel_icache_hits},
   {&gfx10_sqc_perf_sel_icache_misses},
   {&gfx10_sqc_perf_sel_icache_misses_duplicate},
   {&gfx10_gl1c_perf_sel_req},
   {&gfx10_gl1c_perf_sel_req_miss},
   {&gfx10_gl2c_perf_sel_req},
   {&gfx103_gl2c_perf_sel_miss},
   {&gfx10_cpf_perf_sel_stat_busy},
   {&gfx10_sqc_perf_sel_lds_bank_conflict},
   {&gfx103_gl2c_perf_sel_ea_rdreq_32b},
   {&gfx103_gl2c_perf_sel_ea_rdreq_64b},
   {&gfx103_gl2c_perf_sel_ea_rdreq_96b},
   {&gfx103_gl2c_perf_sel_ea_rdreq_128b},
   {&gfx103_gl2c_perf_sel_ea_wrreq},
   {&gfx103_gl2c_perf_sel_ea_wrreq_64b},
   {&gfx10_gcea_perf_sel_sarb_dram_sized_requests},
   {&gfx10_gcea_perf_sel_sarb_io_sized_requests},
   {&gfx10_ta_perf_sel_ta_busy},
   {&gfx10_tcp_perf_sel_tcp_ta_req_stall},
   {&gfx103_td_perf_sel_ray_tracing_bvh4_tri_node},
   {&gfx103_td_perf_sel_ray_tracing_bvh4_fp16_box_node},
   {&gfx103_td_perf_sel_ray_tracing_bvh4_fp32_box_node},
};

/* GFX11+ */
static struct ac_spm_counter_descr gfx11_tcp_perf_sel_req_miss =
   {AC_SPM_TCP_PERF_SEL_REQ_MISS, TCP, 0x11};
static struct ac_spm_counter_descr gfx11_sqc_perf_sel_dcache_hits =
   {AC_SPM_SQC_PERF_SEL_DCACHE_HITS, SQ_WGP, 0x126};
static struct ac_spm_counter_descr gfx11_sqc_perf_sel_dcache_misses =
   {AC_SPM_SQC_PERF_SEL_DCACHE_MISSES, SQ_WGP, 0x127};
static struct ac_spm_counter_descr gfx11_sqc_perf_sel_dcache_misses_duplicate =
   {AC_SPM_SQC_PERF_SEL_DCACHE_MISSES_DUPLICATE, SQ_WGP, 0x128};
static struct ac_spm_counter_descr gfx11_sqc_perf_sel_icache_hits =
   {AC_SPM_SQC_PERF_SEL_ICACHE_HITS, SQ_WGP, 0x10e};
static struct ac_spm_counter_descr gfx11_sqc_perf_sel_icache_misses =
   {AC_SPM_SQC_PERF_SEL_ICACHE_MISSES, SQ_WGP, 0x10f};
static struct ac_spm_counter_descr gfx11_sqc_perf_sel_icache_misses_duplicate =
   {AC_SPM_SQC_PERF_SEL_ICACHE_MISSES_DUPLICATE, SQ_WGP, 0x110};
static struct ac_spm_counter_descr gfx11_sqc_perf_sel_lds_bank_conflict =
   {AC_SPM_SQC_PERF_SEL_LDS_BANK_CONFLICT, SQ_WGP, 0x100};
static struct ac_spm_counter_descr gfx11_tcp_perf_sel_tcp_ta_req_stall =
   {AC_SPM_TCP_PERF_SEL_TCP_TA_REQ_STALL, TCP, 0x27};

static struct ac_spm_counter_create_info gfx11_spm_counters[] = {
   {&gfx10_tcp_perf_sel_req},
   {&gfx11_tcp_perf_sel_req_miss},
   {&gfx11_sqc_perf_sel_dcache_hits},
   {&gfx11_sqc_perf_sel_dcache_misses},
   {&gfx11_sqc_perf_sel_dcache_misses_duplicate},
   {&gfx11_sqc_perf_sel_icache_hits},
   {&gfx11_sqc_perf_sel_icache_misses},
   {&gfx11_sqc_perf_sel_icache_misses_duplicate},
   {&gfx10_gl1c_perf_sel_req},
   {&gfx10_gl1c_perf_sel_req_miss},
   {&gfx10_gl2c_perf_sel_req},
   {&gfx103_gl2c_perf_sel_miss},
   {&gfx10_cpf_perf_sel_stat_busy},
   {&gfx11_sqc_perf_sel_lds_bank_conflict},
   {&gfx103_gl2c_perf_sel_ea_rdreq_32b},
   {&gfx103_gl2c_perf_sel_ea_rdreq_64b},
   {&gfx103_gl2c_perf_sel_ea_rdreq_96b},
   {&gfx103_gl2c_perf_sel_ea_rdreq_128b},
   {&gfx103_gl2c_perf_sel_ea_wrreq},
   {&gfx103_gl2c_perf_sel_ea_wrreq_64b},
   {&gfx10_gcea_perf_sel_sarb_dram_sized_requests},
   {&gfx10_gcea_perf_sel_sarb_io_sized_requests},
   {&gfx10_ta_perf_sel_ta_busy},
   {&gfx11_tcp_perf_sel_tcp_ta_req_stall},
   {&gfx103_td_perf_sel_ray_tracing_bvh4_tri_node},
   {&gfx103_td_perf_sel_ray_tracing_bvh4_fp16_box_node},
   {&gfx103_td_perf_sel_ray_tracing_bvh4_fp32_box_node},
};

/* GFX12+ */
static struct ac_spm_counter_descr gfx12_sqc_perf_sel_dcache_hits =
   {AC_SPM_SQC_PERF_SEL_DCACHE_HITS, SQ_WGP, 0x146};
static struct ac_spm_counter_descr gfx12_sqc_perf_sel_dcache_misses =
   {AC_SPM_SQC_PERF_SEL_DCACHE_MISSES, SQ_WGP, 0x147};
static struct ac_spm_counter_descr gfx12_sqc_perf_sel_dcache_misses_duplicate =
   {AC_SPM_SQC_PERF_SEL_DCACHE_MISSES_DUPLICATE, SQ_WGP, 0x148};
static struct ac_spm_counter_descr gfx12_sqc_perf_sel_icache_hits =
   {AC_SPM_SQC_PERF_SEL_ICACHE_HITS, SQ_WGP, 0x12e};
static struct ac_spm_counter_descr gfx12_sqc_perf_sel_icache_misses =
   {AC_SPM_SQC_PERF_SEL_ICACHE_MISSES, SQ_WGP, 0x12f};
static struct ac_spm_counter_descr gfx12_sqc_perf_sel_icache_misses_duplicate =
   {AC_SPM_SQC_PERF_SEL_ICACHE_MISSES_DUPLICATE, SQ_WGP, 0x130};
static struct ac_spm_counter_descr gfx12_gl2c_perf_sel_miss =
   {AC_SPM_GL2C_PERF_SEL_MISS, GL2C, 0x2a};
static struct ac_spm_counter_descr gfx12_sqc_perf_sel_lds_bank_conflict =
   {AC_SPM_SQC_PERF_SEL_LDS_BANK_CONFLICT, SQ_WGP, 0x120};
static struct ac_spm_counter_descr gfx12_gl2c_perf_sel_ea_rdreq_32b =
   {AC_SPM_GL2C_PERF_SEL_EA_RDREQ_32B, GL2C, 0x92};
static struct ac_spm_counter_descr gfx12_gl2c_perf_sel_ea_rdreq_64b =
   {AC_SPM_GL2C_PERF_SEL_EA_RDREQ_64B, GL2C, 0x93};
static struct ac_spm_counter_descr gfx12_gl2c_perf_sel_ea_rdreq_128b =
   {AC_SPM_GL2C_PERF_SEL_EA_RDREQ_128B, GL2C, 0x94};
static struct ac_spm_counter_descr gfx12_gl2c_perf_sel_ea_rdreq_256b =
   {AC_SPM_GL2C_PERF_SEL_EA_RDREQ_256B, GL2C, 0x95};
static struct ac_spm_counter_descr gfx12_gl2c_perf_sel_ea_wrreq =
   {AC_SPM_GL2C_PERF_SEL_EA_WRREQ, GL2C, 0x6c};
static struct ac_spm_counter_descr gfx12_gl2c_perf_sel_ea_wrreq_64b =
   {AC_SPM_GL2C_PERF_SEL_EA_WRREQ_64B, GL2C, 0x72};
static struct ac_spm_counter_descr gfx12_gcea_cpwd_perf_sel_sarb_dram_rd_size_req =
   {AC_SPM_GCEA_CPWD_PERF_SEL_SARB_DRAM_RD_SIZE_REQ, GCEA_CPWD, 0x3};
static struct ac_spm_counter_descr gfx12_gcea_cpwd_perf_sel_sarb_dram_wr_size_req =
   {AC_SPM_GCEA_CPWD_PERF_SEL_SARB_DRAM_WR_SIZE_REQ, GCEA_CPWD, 0x4};
static struct ac_spm_counter_descr gfx12_gcea_se_perf_sel_sarb_io_rd_size_req =
   {AC_SPM_GCEA_SE_PERF_SEL_SARB_IO_RD_SIZE_REQ, GCEA_SE, 0x1};
static struct ac_spm_counter_descr gfx12_gcea_se_perf_sel_sarb_io_wr_size_req =
   {AC_SPM_GCEA_SE_PERF_SEL_SARB_IO_WR_SIZE_REQ, GCEA_SE, 0x2};
static struct ac_spm_counter_descr gfx12_gl2c_perf_sel_ea_wrreq_stall =
   {AC_SPM_GL2C_PERF_SEL_EA_WRREQ_STALL, GL2C, 0x7a};

static struct ac_spm_counter_create_info gfx12_spm_counters[] = {
   {&gfx10_tcp_perf_sel_req},
   {&gfx11_tcp_perf_sel_req_miss},
   {&gfx12_sqc_perf_sel_dcache_hits},
   {&gfx12_sqc_perf_sel_dcache_misses},
   {&gfx12_sqc_perf_sel_dcache_misses_duplicate},
   {&gfx12_sqc_perf_sel_icache_hits},
   {&gfx12_sqc_perf_sel_icache_misses},
   {&gfx12_sqc_perf_sel_icache_misses_duplicate},
   {&gfx10_gl2c_perf_sel_req},
   {&gfx12_gl2c_perf_sel_miss},
   {&gfx10_cpf_perf_sel_stat_busy},
   {&gfx12_sqc_perf_sel_lds_bank_conflict},
   {&gfx12_gl2c_perf_sel_ea_rdreq_32b},
   {&gfx12_gl2c_perf_sel_ea_rdreq_64b},
   {&gfx12_gl2c_perf_sel_ea_rdreq_128b},
   {&gfx12_gl2c_perf_sel_ea_rdreq_256b},
   {&gfx12_gl2c_perf_sel_ea_wrreq},
   {&gfx12_gl2c_perf_sel_ea_wrreq_64b},
   {&gfx12_gcea_cpwd_perf_sel_sarb_dram_rd_size_req},
   {&gfx12_gcea_cpwd_perf_sel_sarb_dram_wr_size_req},
   {&gfx12_gcea_se_perf_sel_sarb_io_rd_size_req},
   {&gfx12_gcea_se_perf_sel_sarb_io_wr_size_req},
   {&gfx10_ta_perf_sel_ta_busy},
   {&gfx11_tcp_perf_sel_tcp_ta_req_stall},
   {&gfx12_gl2c_perf_sel_ea_wrreq_stall},
   {&gfx103_td_perf_sel_ray_tracing_bvh4_tri_node},
   {&gfx103_td_perf_sel_ray_tracing_bvh4_fp16_box_node},
   {&gfx103_td_perf_sel_ray_tracing_bvh4_fp32_box_node},
};

static struct ac_spm_block_select *
ac_spm_get_block_select(struct ac_spm *spm, const struct ac_pc_block *block)
{
   struct ac_spm_block_select *block_sel, *new_block_sel;
   uint32_t num_block_sel;

   for (uint32_t i = 0; i < spm->num_block_sel; i++) {
      if (spm->block_sel[i].b->b->b->gpu_block == block->b->b->gpu_block)
         return &spm->block_sel[i];
   }

   /* Allocate a new select block if it doesn't already exist. */
   num_block_sel = spm->num_block_sel + 1;
   block_sel = realloc(spm->block_sel, num_block_sel * sizeof(*block_sel));
   if (!block_sel)
      return NULL;

   spm->num_block_sel = num_block_sel;
   spm->block_sel = block_sel;

   /* Initialize the new select block. */
   new_block_sel = &spm->block_sel[spm->num_block_sel - 1];
   memset(new_block_sel, 0, sizeof(*new_block_sel));

   new_block_sel->b = block;
   new_block_sel->instances =
      calloc(block->num_instances, sizeof(*new_block_sel->instances));
   if (!new_block_sel->instances)
      return NULL;
   new_block_sel->num_instances = block->num_instances;

   for (unsigned i = 0; i < new_block_sel->num_instances; i++)
      new_block_sel->instances[i].num_counters = block->b->b->num_spm_modules;

   return new_block_sel;
}

struct ac_spm_instance_mapping {
   uint32_t se_index;         /* SE index or 0 if global */
   uint32_t sa_index;         /* SA index or 0 if global or per-SE */
   uint32_t instance_index;
};

static bool
ac_spm_init_instance_mapping(const struct radeon_info *info,
                             const struct ac_pc_block *block,
                             const struct ac_spm_counter_info *counter,
                             struct ac_spm_instance_mapping *mapping)
{
   uint32_t instance_index = 0, se_index = 0, sa_index = 0;

   switch (block->b->b->distribution) {
   case AC_PC_GLOBAL_BLOCK:
      /* Global blocks have a one-to-one instance mapping. */
      instance_index = counter->instance;
      break;
   case AC_PC_PER_SHADER_ENGINE:
      /* We want the SE index to be the outer index and the local instance to
       * be the inner index.
       */
      se_index = counter->instance / block->num_scoped_instances;
      instance_index = counter->instance % block->num_scoped_instances;
      break;
   case AC_PC_PER_SHADER_ARRAY:
      /* From the outermost to the innermost, the internal indices are in the
       * order: SE, SA, local instance.
       */
      se_index = (counter->instance / block->num_scoped_instances) / info->max_sa_per_se;
      sa_index = (counter->instance / block->num_scoped_instances) % info->max_sa_per_se;
      instance_index = counter->instance % block->num_scoped_instances;
      break;
   default:
      UNREACHABLE("Invalid perf block distribution mode.");
   }

   if (se_index >= info->num_se ||
       sa_index >= info->max_sa_per_se ||
       instance_index >= block->num_scoped_instances)
      return false;

   mapping->se_index = se_index;
   mapping->sa_index = sa_index;
   mapping->instance_index = instance_index;

   return true;
}

static void
ac_spm_init_muxsel(const struct radeon_info *info,
                   const struct ac_pc_block *block,
                   const struct ac_spm_instance_mapping *mapping,
                   struct ac_spm_counter_info *counter,
                   uint32_t spm_wire)
{
   const uint16_t counter_idx = 2 * spm_wire + (counter->is_even ? 0 : 1);
   union ac_spm_muxsel *muxsel = &counter->muxsel;

   if (info->gfx_level >= GFX11) {
      muxsel->gfx11.counter = counter_idx;
      muxsel->gfx11.block = block->b->b->spm_block_select;
      muxsel->gfx11.shader_array = mapping->sa_index;
      muxsel->gfx11.instance = mapping->instance_index;
   } else {
      muxsel->gfx10.counter = counter_idx;
      muxsel->gfx10.block = block->b->b->spm_block_select;
      muxsel->gfx10.shader_array = mapping->sa_index;
      muxsel->gfx10.instance = mapping->instance_index;
   }
}

static uint32_t
ac_spm_init_grbm_gfx_index(const struct ac_pc_block *block,
                           const struct ac_spm_instance_mapping *mapping)
{
   uint32_t instance = mapping->instance_index;
   uint32_t grbm_gfx_index = 0;

   grbm_gfx_index |= S_030800_SE_INDEX(mapping->se_index) |
                     S_030800_SH_INDEX(mapping->sa_index);

   switch (block->b->b->distribution) {
   case AC_PC_GLOBAL_BLOCK:
      /* Global block writes should broadcast to SEs and SAs. */
      grbm_gfx_index |= S_030800_SE_BROADCAST_WRITES(1);
      FALLTHROUGH;
   case AC_PC_PER_SHADER_ENGINE:
      /* Per-SE block writes should broadcast to SAs. */
      grbm_gfx_index |= S_030800_SH_BROADCAST_WRITES(1);
      break;
   case AC_PC_PER_SHADER_ARRAY:
      /* Other blocks shouldn't broadcast. */
      break;
   default:
      UNREACHABLE("Invalid perf block distribution mode.");
   }

   if (block->b->b->gpu_block == SQ_WGP) {
      union {
         struct {
            uint32_t block_index : 2; /* Block index withing WGP */
            uint32_t wgp_index : 3;
            uint32_t is_below_spi : 1; /* 0: lower WGP numbers, 1: higher WGP numbers */
            uint32_t reserved : 26;
         };

         uint32_t value;
      } instance_index = {0};

      const uint32_t num_wgp_above_spi = 4;
      const bool is_below_spi = mapping->instance_index >= num_wgp_above_spi;

      instance_index.wgp_index =
         is_below_spi ? (mapping->instance_index - num_wgp_above_spi) : mapping->instance_index;
      instance_index.is_below_spi = is_below_spi;

      instance = instance_index.value;
   }

   grbm_gfx_index |= S_030800_INSTANCE_INDEX(instance);

   return grbm_gfx_index;
}

static bool
ac_spm_map_counter(struct ac_spm *spm, struct ac_spm_block_select *block_sel,
                   struct ac_spm_counter_info *counter,
                   const struct ac_spm_instance_mapping *mapping,
                   uint32_t *spm_wire)
{
   uint32_t instance = counter->instance;

   if (block_sel->b->b->b->gpu_block == SQ_WGP) {
      if (!spm->sq_wgp[instance].grbm_gfx_index) {
         spm->sq_wgp[instance].grbm_gfx_index =
            ac_spm_init_grbm_gfx_index(block_sel->b, mapping);
      }

      for (unsigned i = 0; i < ARRAY_SIZE(spm->sq_wgp[instance].counters); i++) {
         struct ac_spm_counter_select *cntr_sel = &spm->sq_wgp[instance].counters[i];

         if (i < spm->sq_wgp[instance].num_counters)
            continue;

         cntr_sel->sel0 |= S_036700_PERF_SEL(counter->event_id) |
                           S_036700_SPM_MODE(1) | /* 16-bit clamp */
                           S_036700_PERF_MODE(0);

         /* Each SQ_WQP modules (GFX11+) share one 32-bit accumulator/wire
          * per pair of selects.
          */
         cntr_sel->active |= 1 << (i % 2);
         *spm_wire = i / 2;

         if (cntr_sel->active & 0x1)
            counter->is_even = true;

         spm->sq_wgp[instance].num_counters++;
         return true;
      }
   } else if (block_sel->b->b->b->gpu_block == SQ) {
      for (unsigned i = 0; i < ARRAY_SIZE(spm->sqg[instance].counters); i++) {
         struct ac_spm_counter_select *cntr_sel = &spm->sqg[instance].counters[i];

         if (i < spm->sqg[instance].num_counters)
            continue;

         /* SQ doesn't support 16-bit counters. */
         cntr_sel->sel0 |= S_036700_PERF_SEL(counter->event_id) |
                           S_036700_SPM_MODE(3) | /* 32-bit clamp */
                           S_036700_PERF_MODE(0);
         cntr_sel->active |= 0x3;

         /* 32-bits counter are always even. */
         counter->is_even = true;

         /* One wire per SQ module. */
         *spm_wire = i;

         spm->sqg[instance].num_counters++;
         return true;
      }
   } else {
      /* Generic blocks. */
      struct ac_spm_block_instance *block_instance =
         &block_sel->instances[instance];

      if (!block_instance->grbm_gfx_index) {
         block_instance->grbm_gfx_index =
            ac_spm_init_grbm_gfx_index(block_sel->b, mapping);
      }

      for (unsigned i = 0; i < block_instance->num_counters; i++) {
         struct ac_spm_counter_select *cntr_sel = &block_instance->counters[i];
         int index = ffs(~cntr_sel->active) - 1;

         switch (index) {
         case 0: /* use S_037004_PERF_SEL */
            cntr_sel->sel0 |= S_037004_PERF_SEL(counter->event_id) |
                              S_037004_CNTR_MODE(1) | /* 16-bit clamp */
                              S_037004_PERF_MODE(0); /* accum */
            break;
         case 1: /* use S_037004_PERF_SEL1 */
            cntr_sel->sel0 |= S_037004_PERF_SEL1(counter->event_id) |
                              S_037004_PERF_MODE1(0);
            break;
         case 2: /* use S_037004_PERF_SEL2 */
            cntr_sel->sel1 |= S_037008_PERF_SEL2(counter->event_id) |
                              S_037008_PERF_MODE2(0);
            break;
         case 3: /* use S_037004_PERF_SEL3 */
            cntr_sel->sel1 |= S_037008_PERF_SEL3(counter->event_id) |
                              S_037008_PERF_MODE3(0);
            break;
         default:
            /*  Try to program the new counter slot. */
            continue;
         }

         /* Mark this 16-bit counter as used. */
         cntr_sel->active |= 1 << index;

         /* Determine if the counter is even or odd. */
         counter->is_even = !(index % 2);

         /* Determine the SPM wire (one wire holds two 16-bit counters). */
         *spm_wire = !!(index >= 2);

         return true;
      }
   }

   return false;
}

static bool
ac_spm_add_counter(const struct radeon_info *info,
                   const struct ac_perfcounters *pc,
                   struct ac_spm *spm,
                   const struct ac_spm_counter_create_info *counter_info)
{
   struct ac_spm_instance_mapping instance_mapping = {0};
   struct ac_spm_counter_info *counter;
   struct ac_spm_block_select *block_sel;
   struct ac_pc_block *block;
   uint32_t spm_wire;

   /* Check if the GPU block is valid. */
   block = ac_pc_get_block(pc, counter_info->b->gpu_block);
   if (!block) {
      fprintf(stderr, "ac/spm: Invalid GPU block.\n");
      return false;
   }

   /* Check if the number of instances is valid. */
   if (counter_info->instance > block->num_instances - 1) {
      fprintf(stderr, "ac/spm: Invalid instance ID.\n");
      return false;
   }

   /* Check if the event ID is valid. */
   if (counter_info->b->event_id > block->b->selectors) {
      fprintf(stderr, "ac/spm: Invalid event ID.\n");
      return false;
   }

   counter = &spm->counters[spm->num_counters];
   spm->num_counters++;

   counter->id = counter_info->b->id;
   counter->gpu_block = counter_info->b->gpu_block;
   counter->event_id = counter_info->b->event_id;
   counter->instance = counter_info->instance;

   /* Get the select block used to configure the counter. */
   block_sel = ac_spm_get_block_select(spm, block);
   if (!block_sel)
      return false;

   /* Initialize instance mapping for the counter. */
   if (!ac_spm_init_instance_mapping(info, block, counter, &instance_mapping)) {
      fprintf(stderr, "ac/spm: Failed to initialize instance mapping.\n");
      return false;
   }

   /* Map the counter to the select block. */
   if (!ac_spm_map_counter(spm, block_sel, counter, &instance_mapping, &spm_wire)) {
      fprintf(stderr, "ac/spm: No free slots available!\n");
      return false;
   }

   /* Determine the counter segment type. */
   if (block->b->b->flags & AC_PC_BLOCK_SE) {
      counter->segment_type = instance_mapping.se_index;
   } else {
      counter->segment_type = AC_SPM_SEGMENT_TYPE_GLOBAL;
   }

   /* Configure the muxsel for SPM. */
   ac_spm_init_muxsel(info, block, &instance_mapping, counter, spm_wire);

   return true;
}

static void
ac_spm_fill_muxsel_ram(const struct radeon_info *info,
                       struct ac_spm *spm,
                       enum ac_spm_segment_type segment_type,
                       uint32_t offset)
{
   struct ac_spm_muxsel_line *mappings = spm->muxsel_lines[segment_type];
   uint32_t even_counter_idx = 0, even_line_idx = 0;
   uint32_t odd_counter_idx = 0, odd_line_idx = 1;

   /* Add the global timestamps first. */
   if (segment_type == AC_SPM_SEGMENT_TYPE_GLOBAL) {
      if (info->gfx_level >= GFX11) {
         mappings[even_line_idx].muxsel[even_counter_idx++].value = 0xf840;
         mappings[even_line_idx].muxsel[even_counter_idx++].value = 0xf841;
         mappings[even_line_idx].muxsel[even_counter_idx++].value = 0xf842;
         mappings[even_line_idx].muxsel[even_counter_idx++].value = 0xf843;
      } else {
         for (unsigned i = 0; i < 4; i++) {
            mappings[even_line_idx].muxsel[even_counter_idx++].value = 0xf0f0;
         }
      }
   }

   for (unsigned i = 0; i < spm->num_counters; i++) {
      struct ac_spm_counter_info *counter = &spm->counters[i];

      if (counter->segment_type != segment_type)
         continue;

      if (counter->is_even) {
         counter->offset =
            (offset + even_line_idx) * AC_SPM_NUM_COUNTER_PER_MUXSEL + even_counter_idx;

         mappings[even_line_idx].muxsel[even_counter_idx] = spm->counters[i].muxsel;
         if (++even_counter_idx == AC_SPM_NUM_COUNTER_PER_MUXSEL) {
            even_counter_idx = 0;
            even_line_idx += 2;
         }
      } else {
         counter->offset =
            (offset + odd_line_idx) * AC_SPM_NUM_COUNTER_PER_MUXSEL + odd_counter_idx;

         mappings[odd_line_idx].muxsel[odd_counter_idx] = spm->counters[i].muxsel;
         if (++odd_counter_idx == AC_SPM_NUM_COUNTER_PER_MUXSEL) {
            odd_counter_idx = 0;
            odd_line_idx += 2;
         }
      }
   }
}

bool ac_init_spm(const struct radeon_info *info,
                 const struct ac_perfcounters *pc,
                 struct ac_spm *spm)
{
   const struct ac_spm_counter_create_info *create_info;
   unsigned create_info_count;
   unsigned num_counters = 0;

   switch (info->gfx_level) {
   case GFX10:
      create_info_count = ARRAY_SIZE(gfx10_spm_counters);
      create_info = gfx10_spm_counters;
      break;
   case GFX10_3:
      create_info_count = ARRAY_SIZE(gfx103_spm_counters);
      create_info = gfx103_spm_counters;
      break;
   case GFX11:
   case GFX11_5:
   case GFX11_7:
      create_info_count = ARRAY_SIZE(gfx11_spm_counters);
      create_info = gfx11_spm_counters;
      break;
   case GFX12:
      create_info_count = ARRAY_SIZE(gfx12_spm_counters);
      create_info = gfx12_spm_counters;
      break;
   default:
      fprintf(stderr, "radv: Failed to initialize SPM because SPM counters aren't implemented.\n");
      return false; /* not implemented */
   }

   /* Count the total number of counters. */
   for (unsigned i = 0; i < create_info_count; i++) {
      const struct ac_pc_block *block = ac_pc_get_block(pc, create_info[i].b->gpu_block);

      if (!block) {
         fprintf(stderr, "ac/spm: Unknown group.\n");
         return false;
      }

      num_counters += block->num_instances;
   }

   spm->counters = CALLOC(num_counters, sizeof(*spm->counters));
   if (!spm->counters)
      return false;

   for (unsigned i = 0; i < create_info_count; i++) {
      const struct ac_pc_block *block = ac_pc_get_block(pc, create_info[i].b->gpu_block);
      struct ac_spm_counter_create_info counter = create_info[i];

      assert(block->num_instances > 0);

      for (unsigned j = 0; j < block->num_instances; j++) {
         counter.instance = j;

         if (!ac_spm_add_counter(info, pc, spm, &counter)) {
            fprintf(stderr, "ac/spm: Failed to add SPM counter (%d).\n", i);
            return false;
         }
      }
   }

   /* Determine the segment size and create a muxsel ram for every segment. */
   for (unsigned s = 0; s < AC_SPM_SEGMENT_TYPE_COUNT; s++) {
      unsigned num_even_counters = 0, num_odd_counters = 0;

      if (s == AC_SPM_SEGMENT_TYPE_GLOBAL) {
         /* The global segment always start with a 64-bit timestamp. */
         num_even_counters += AC_SPM_GLOBAL_TIMESTAMP_COUNTERS;
      }

      /* Count the number of even/odd counters for this segment. */
      for (unsigned c = 0; c < spm->num_counters; c++) {
         struct ac_spm_counter_info *counter = &spm->counters[c];

         if (counter->segment_type != s)
            continue;

         if (counter->is_even) {
            num_even_counters++;
         } else {
            num_odd_counters++;
         }
      }

      /* Compute the number of lines. */
      unsigned even_lines =
         DIV_ROUND_UP(num_even_counters, AC_SPM_NUM_COUNTER_PER_MUXSEL);
      unsigned odd_lines =
         DIV_ROUND_UP(num_odd_counters, AC_SPM_NUM_COUNTER_PER_MUXSEL);
      unsigned num_lines = (even_lines > odd_lines) ? (2 * even_lines - 1) : (2 * odd_lines);

      spm->muxsel_lines[s] = CALLOC(num_lines, sizeof(*spm->muxsel_lines[s]));
      if (!spm->muxsel_lines[s])
         return false;
      spm->num_muxsel_lines[s] = num_lines;
   }

   /* Compute the maximum number of muxsel lines among all SEs. On GFX11,
    * there is only one SE segment size value and the highest value is used.
    */
   for (unsigned s = 0; s < AC_SPM_SEGMENT_TYPE_GLOBAL; s++) {
      spm->max_se_muxsel_lines =
         MAX2(spm->num_muxsel_lines[s], spm->max_se_muxsel_lines);
   }

   /* RLC uses the following order: Global, SE0, SE1, SE2, SE3, SE4, SE5. */
   ac_spm_fill_muxsel_ram(info, spm, AC_SPM_SEGMENT_TYPE_GLOBAL, 0);

   const uint32_t num_global_lines = spm->num_muxsel_lines[AC_SPM_SEGMENT_TYPE_GLOBAL];

   if (info->gfx_level >= GFX11) {
      /* On GFX11, RLC uses one segment size for every single SE. */
      for (unsigned i = 0; i < info->num_se; i++) {
         assert(i < AC_SPM_SEGMENT_TYPE_GLOBAL);
         uint32_t offset = num_global_lines + i * spm->max_se_muxsel_lines;

         ac_spm_fill_muxsel_ram(info, spm, i, offset);
      }
   } else {
      uint32_t offset = num_global_lines;

      for (unsigned i = 0; i < info->num_se; i++) {
         assert(i < AC_SPM_SEGMENT_TYPE_GLOBAL);

         ac_spm_fill_muxsel_ram(info, spm, i, offset);

         offset += spm->num_muxsel_lines[i];
      }
   }

   /* Configure the sample interval to default to 4096 clk. */
   spm->sample_interval = 4096;

   /* On GFX11-11.7, the data size written by the hw is in units of segment. */
   spm->ptr_granularity =
      (info->gfx_level >= GFX11 && info->gfx_level < GFX12) ? 32 : 1;

   return true;
}

void ac_destroy_spm(struct ac_spm *spm)
{
   for (unsigned s = 0; s < AC_SPM_SEGMENT_TYPE_COUNT; s++) {
      FREE(spm->muxsel_lines[s]);
   }

   for (unsigned i = 0; i < spm->num_block_sel; i++) {
      FREE(spm->block_sel[i].instances);
   }

   FREE(spm->block_sel);
   FREE(spm->counters);
}

static uint32_t ac_spm_get_sample_size(const struct ac_spm *spm)
{
   uint32_t sample_size = 0; /* in bytes */

   for (unsigned s = 0; s < AC_SPM_SEGMENT_TYPE_COUNT; s++) {
      sample_size += spm->num_muxsel_lines[s] * AC_SPM_MUXSEL_LINE_SIZE * 4;
   }

   return sample_size;
}

static bool ac_spm_get_num_samples(const struct ac_spm *spm, uint32_t *num_samples)
{
   uint32_t sample_size = ac_spm_get_sample_size(spm);
   uint32_t *ptr = (uint32_t *)spm->ptr;
   uint32_t data_size, num_lines_written;

   /* Get the data size (in bytes) written by the hw to the ring buffer. */
   data_size = ptr[0] * spm->ptr_granularity;

   /* Compute the number of 256 bits (16 * 16-bits counters) lines written. */
   num_lines_written = data_size / (2 * AC_SPM_NUM_COUNTER_PER_MUXSEL);

   /* Check for overflow. */
   if (num_lines_written % (sample_size / 32)) {
      /* Buffer is too small and it needs to be resized. */
      return false;
   }

   *num_samples = num_lines_written / (sample_size / 32);
   return true;
}

bool ac_spm_get_trace(const struct ac_spm *spm, struct ac_spm_trace *trace)
{
   memset(trace, 0, sizeof(*trace));

   trace->ptr = spm->ptr;
   trace->sample_interval = spm->sample_interval;
   trace->num_counters = spm->num_counters;
   trace->counters = spm->counters;
   trace->sample_size_in_bytes = ac_spm_get_sample_size(spm);

   return ac_spm_get_num_samples(spm, &trace->num_samples);
}

/* SPM components. */
/* Instruction cache components. */
static struct ac_spm_derived_component_descr gfx10_inst_cache_request_count_comp = {
   .id = AC_SPM_COMPONENT_INST_CACHE_REQUEST_COUNT,
   .counter_id = AC_SPM_COUNTER_INST_CACHE_HIT,
   .name = "Requests",
   .usage = AC_SPM_USAGE_ITEMS,
};

static struct ac_spm_derived_component_descr gfx10_inst_cache_hit_count_comp = {
   .id = AC_SPM_COMPONENT_INST_CACHE_HIT_COUNT,
   .counter_id = AC_SPM_COUNTER_INST_CACHE_HIT,
   .name = "Hits",
   .usage = AC_SPM_USAGE_ITEMS,
};

static struct ac_spm_derived_component_descr gfx10_inst_cache_miss_count_comp = {
   .id = AC_SPM_COMPONENT_INST_CACHE_MISS_COUNT,
   .counter_id = AC_SPM_COUNTER_INST_CACHE_HIT,
   .name = "Misses",
   .usage = AC_SPM_USAGE_ITEMS,
};

/* Scalar cache components. */
static struct ac_spm_derived_component_descr gfx10_scalar_cache_request_count_comp = {
   .id = AC_SPM_COMPONENT_SCALAR_CACHE_REQUEST_COUNT,
   .counter_id = AC_SPM_COUNTER_SCALAR_CACHE_HIT,
   .name = "Requests",
   .usage = AC_SPM_USAGE_ITEMS,
};

static struct ac_spm_derived_component_descr gfx10_scalar_cache_hit_count_comp = {
   .id = AC_SPM_COMPONENT_SCALAR_CACHE_HIT_COUNT,
   .counter_id = AC_SPM_COUNTER_SCALAR_CACHE_HIT,
   .name = "Hits",
   .usage = AC_SPM_USAGE_ITEMS,
};

static struct ac_spm_derived_component_descr gfx10_scalar_cache_miss_count_comp = {
   .id = AC_SPM_COMPONENT_SCALAR_CACHE_MISS_COUNT,
   .counter_id = AC_SPM_COUNTER_SCALAR_CACHE_HIT,
   .name = "Misses",
   .usage = AC_SPM_USAGE_ITEMS,
};

/* L0 cache components. */
static struct ac_spm_derived_component_descr gfx10_l0_cache_request_count_comp = {
   .id = AC_SPM_COMPONENT_L0_CACHE_REQUEST_COUNT,
   .counter_id = AC_SPM_COUNTER_L0_CACHE_HIT,
   .name = "Requests",
   .usage = AC_SPM_USAGE_ITEMS,
};

static struct ac_spm_derived_component_descr gfx10_l0_cache_hit_count_comp = {
   .id = AC_SPM_COMPONENT_L0_CACHE_HIT_COUNT,
   .counter_id = AC_SPM_COUNTER_L0_CACHE_HIT,
   .name = "Hits",
   .usage = AC_SPM_USAGE_ITEMS,
};

static struct ac_spm_derived_component_descr gfx10_l0_cache_miss_count_comp = {
   .id = AC_SPM_COMPONENT_L0_CACHE_MISS_COUNT,
   .counter_id = AC_SPM_COUNTER_L0_CACHE_HIT,
   .name = "Misses",
   .usage = AC_SPM_USAGE_ITEMS,
};

/* L1 cache components. */
static struct ac_spm_derived_component_descr gfx10_l1_cache_request_count_comp = {
   .id = AC_SPM_COMPONENT_L1_CACHE_REQUEST_COUNT,
   .counter_id = AC_SPM_COUNTER_L1_CACHE_HIT,
   .name = "Requests",
   .usage = AC_SPM_USAGE_ITEMS,
};

static struct ac_spm_derived_component_descr gfx10_l1_cache_hit_count_comp = {
   .id = AC_SPM_COMPONENT_L1_CACHE_HIT_COUNT,
   .counter_id = AC_SPM_COUNTER_L1_CACHE_HIT,
   .name = "Hits",
   .usage = AC_SPM_USAGE_ITEMS,
};

static struct ac_spm_derived_component_descr gfx10_l1_cache_miss_count_comp = {
   .id = AC_SPM_COMPONENT_L1_CACHE_MISS_COUNT,
   .counter_id = AC_SPM_COUNTER_L1_CACHE_HIT,
   .name = "Misses",
   .usage = AC_SPM_USAGE_ITEMS,
};

/* L2 cache components. */
static struct ac_spm_derived_component_descr gfx10_l2_cache_request_count_comp = {
   .id = AC_SPM_COMPONENT_L2_CACHE_REQUEST_COUNT,
   .counter_id = AC_SPM_COUNTER_L2_CACHE_HIT,
   .name = "Requests",
   .usage = AC_SPM_USAGE_ITEMS,
};

static struct ac_spm_derived_component_descr gfx10_l2_cache_hit_count_comp = {
   .id = AC_SPM_COMPONENT_L2_CACHE_HIT_COUNT,
   .counter_id = AC_SPM_COUNTER_L2_CACHE_HIT,
   .name = "Hits",
   .usage = AC_SPM_USAGE_ITEMS,
};

static struct ac_spm_derived_component_descr gfx10_l2_cache_miss_count_comp = {
   .id = AC_SPM_COMPONENT_L2_CACHE_MISS_COUNT,
   .counter_id = AC_SPM_COUNTER_L2_CACHE_HIT,
   .name = "Misses",
   .usage = AC_SPM_USAGE_ITEMS,
};

static struct ac_spm_derived_component_descr gfx10_gpu_busy_cycles_comp = {
   .id = AC_SPM_COMPONENT_GPU_BUSY_CYCLES,
   .counter_id = AC_SPM_COUNTER_CS_LDS_BANK_CONFLICT,
   .name = "Gpu Busy Cycles",
   .usage = AC_SPM_USAGE_CYCLES,
};

static struct ac_spm_derived_component_descr gfx10_cs_lds_bank_conflict_cycles_comp = {
   .id = AC_SPM_COMPONENT_CS_LDS_BANK_CONFLICT_CYCLES,
   .counter_id = AC_SPM_COUNTER_CS_LDS_BANK_CONFLICT,
   .name = "LDS Busy Cycles",
   .usage = AC_SPM_USAGE_CYCLES,
};

static struct ac_spm_derived_component_descr gfx10_mem_unit_busy_cycles_comp = {
   .id = AC_SPM_COMPONENT_MEM_UNIT_BUSY_CYCLES,
   .counter_id = AC_SPM_COUNTER_MEM_UNIT_BUSY,
   .name = "Memory unit busy cycles",
   .usage = AC_SPM_USAGE_CYCLES,
};

static struct ac_spm_derived_component_descr gfx10_mem_unit_stalled_cycles_comp = {
   .id = AC_SPM_COMPONENT_MEM_UNIT_STALLED_CYCLES,
   .counter_id = AC_SPM_COUNTER_MEM_UNIT_STALLED,
   .name = "Memory unit stalled cycles",
   .usage = AC_SPM_USAGE_CYCLES,
};

static struct ac_spm_derived_component_descr gfx12_write_unit_stalled_cycles_comp = {
   .id = AC_SPM_COMPONENT_WRITE_UNIT_STALLED_CYCLES,
   .counter_id = AC_SPM_COUNTER_WRITE_UNIT_STALLED,
   .name = "Write unit stalled cycles",
   .usage = AC_SPM_USAGE_CYCLES,
};

/* SPM counters. */
static struct ac_spm_derived_counter_descr gfx10_inst_cache_hit_counter = {
   .id = AC_SPM_COUNTER_INST_CACHE_HIT,
   .group_id = AC_SPM_GROUP_CACHE,
   .name = "Instruction cache hit",
   .desc = "The percentage of read requests made that hit the data in the "
           "Instruction cache. The Instruction cache supplies shader code to an "
           "executing shader. Each request is 64 bytes in size. Value range: 0% "
           "(no hit) to 100% (optimal).",
   .usage = AC_SPM_USAGE_PERCENTAGE,
   .num_components = 3,
   .components = {
      &gfx10_inst_cache_request_count_comp,
      &gfx10_inst_cache_hit_count_comp,
      &gfx10_inst_cache_miss_count_comp,
   },
};

static struct ac_spm_derived_counter_descr gfx10_scalar_cache_hit_counter = {
   .id = AC_SPM_COUNTER_SCALAR_CACHE_HIT,
   .group_id = AC_SPM_GROUP_CACHE,
   .name = "Scalar cache hit",
   .desc = "The percentage of read requests made from executing shader code "
           "that hit the data in the Scalar cache. The Scalar cache contains data "
           "that does not vary in each thread across the wavefront. Each request is "
           "64 bytes in size. Value range: 0% (no hit) to 100% (optimal).",
   .usage = AC_SPM_USAGE_PERCENTAGE,
   .num_components = 3,
   .components = {
      &gfx10_scalar_cache_request_count_comp,
      &gfx10_scalar_cache_hit_count_comp,
      &gfx10_scalar_cache_miss_count_comp,
   },
};

static struct ac_spm_derived_counter_descr gfx10_l0_cache_hit_counter = {
   .id = AC_SPM_COUNTER_L0_CACHE_HIT,
   .group_id = AC_SPM_GROUP_CACHE,
   .name = "L0 cache hit",
   .desc = "The percentage of read requests that hit the data in the L0 cache. "
           "The L0 cache contains vector data, which is data that may vary in each "
           "thread across the wavefront. Each request is 128 bytes in size. Value "
           "range: 0% (no hit) to 100% (optimal).",
   .usage = AC_SPM_USAGE_PERCENTAGE,
   .num_components = 3,
   .components = {
      &gfx10_l0_cache_request_count_comp,
      &gfx10_l0_cache_hit_count_comp,
      &gfx10_l0_cache_miss_count_comp,
   },
};

static struct ac_spm_derived_counter_descr gfx10_l1_cache_hit_counter = {
   .id = AC_SPM_COUNTER_L1_CACHE_HIT,
   .group_id = AC_SPM_GROUP_CACHE,
   .name = "L1 cache hit",
   .desc = "The percentage of read or write requests that hit the data in the "
           "L1 cache. The L1 cache is shared across all WGPs in a single shader "
           "engine. Each request is 128 bytes in size. Value range: 0% (no hit) to "
           "100% (optimal).",
   .usage = AC_SPM_USAGE_PERCENTAGE,
   .num_components = 3,
   .components = {
      &gfx10_l1_cache_request_count_comp,
      &gfx10_l1_cache_hit_count_comp,
      &gfx10_l1_cache_miss_count_comp,
   },
};

static struct ac_spm_derived_counter_descr gfx10_l2_cache_hit_counter = {
   .id = AC_SPM_COUNTER_L2_CACHE_HIT,
   .group_id = AC_SPM_GROUP_CACHE,
   .name = "L2 cache hit",
   .desc = "The percentage of read or write requests that hit the data in the "
           "L2 cache. The L2 cache is shared by many blocks across the GPU, "
           "including the Command Processor, Geometry Engine, all WGPs, all Render "
           "Backends, and others. Each request is 128 bytes in size. Value range: 0% "
           "(no hit) to 100% (optimal).",
   .usage = AC_SPM_USAGE_PERCENTAGE,
   .num_components = 3,
   .components = {
      &gfx10_l2_cache_request_count_comp,
      &gfx10_l2_cache_hit_count_comp,
      &gfx10_l2_cache_miss_count_comp,
   },
};

static struct ac_spm_derived_counter_descr gfx10_cs_lds_bank_conflict_counter = {
   .id = AC_SPM_COUNTER_CS_LDS_BANK_CONFLICT,
   .group_id = AC_SPM_GROUP_LDS,
   .name = "LDS Bank Conflict",
   .desc = "The percentage of GPUTime LDS is stalled by bank conflicts. Value "
           "range: 0% (optimal) to 100% (bad).",
   .usage = AC_SPM_USAGE_PERCENTAGE,
   .num_components = 2,
   .components = {
      &gfx10_gpu_busy_cycles_comp,
      &gfx10_cs_lds_bank_conflict_cycles_comp,
   },
};

static struct ac_spm_derived_counter_descr gfx10_fetch_size_counter = {
   .id = AC_SPM_COUNTER_FETCH_SIZE,
   .group_id = AC_SPM_GROUP_MEMORY_BYTES,
   .name = "Fetch size",
   .desc = "The total bytes fetched from the video memory. This is measured "
           "with all extra fetches and any cache or memory effects taken into "
           "account.",
   .usage = AC_SPM_USAGE_BYTES,
   .num_components = 0,
};

static struct ac_spm_derived_counter_descr gfx10_write_size_counter = {
   .id = AC_SPM_COUNTER_WRITE_SIZE,
   .group_id = AC_SPM_GROUP_MEMORY_BYTES,
   .name = "Write size",
   .desc = "The total bytes written to the video memory. This is measured with "
           "all extra fetches and any cache or memory effects taken into account.",
   .usage = AC_SPM_USAGE_BYTES,
   .num_components = 0,
};

static struct ac_spm_derived_counter_descr gfx10_local_vid_mem_bytes_counter = {
   .id = AC_SPM_COUNTER_LOCAL_VID_MEM_BYTES,
   .group_id = AC_SPM_GROUP_MEMORY_BYTES,
   .name = "Local video memory bytes",
   .desc = "Number of bytes read from or written to the Infinity Cache (if "
           "available) or local video memory",
   .usage = AC_SPM_USAGE_BYTES,
   .num_components = 0,
};

static struct ac_spm_derived_counter_descr gfx10_pcie_bytes_counter = {
   .id = AC_SPM_COUNTER_PCIE_BYTES,
   .group_id = AC_SPM_GROUP_MEMORY_BYTES,
   .name = "PCIe bytes",
   .desc = "Number of bytes sent and received over the PCIe bus",
   .usage = AC_SPM_USAGE_BYTES,
   .num_components = 0,
};

static struct ac_spm_derived_counter_descr gfx10_mem_unit_busy_counter = {
   .id = AC_SPM_COUNTER_MEM_UNIT_BUSY,
   .group_id = AC_SPM_GROUP_MEMORY_PERCENTAGE,
   .name = "Memory unity busy",
   .desc = "The percentage of GPUTime the memory unit is active. The result "
           "includes the stall time (MemUnitStalled). This is measured with all "
           "extra fetches and writes and any cache or memory effects taken into "
           "account. Value range: 0% to 100% (fetch-bound).",
   .usage = AC_SPM_USAGE_PERCENTAGE,
   .num_components = 2,
   .components = {
      &gfx10_gpu_busy_cycles_comp,
      &gfx10_mem_unit_busy_cycles_comp,
   },
};

static struct ac_spm_derived_counter_descr gfx10_mem_unit_stalled_counter = {
   .id = AC_SPM_COUNTER_MEM_UNIT_STALLED,
   .group_id = AC_SPM_GROUP_MEMORY_PERCENTAGE,
   .name = "Memory unit stalled",
   .desc = "The percentage of GPUTime the memory unit is stalled. Try reducing "
           "the number or size of fetches and writes if possible. Value range: 0% "
           "(optimal) to 100% (bad).",
   .usage = AC_SPM_USAGE_PERCENTAGE,
   .num_components = 2,
   .components = {
      &gfx10_gpu_busy_cycles_comp,
      &gfx10_mem_unit_stalled_cycles_comp,
   },
};

static struct ac_spm_derived_counter_descr gfx12_write_unit_stalled_counter = {
   .id = AC_SPM_COUNTER_WRITE_UNIT_STALLED,
   .group_id = AC_SPM_GROUP_MEMORY_PERCENTAGE,
   .name = "WriteUnitStalled",
   .desc = "The percentage of GPUTime the Write unit is stalled. Value range: "
           "0% to 100% (bad).",
   .usage = AC_SPM_USAGE_PERCENTAGE,
   .num_components = 2,
   .components = {
      &gfx10_gpu_busy_cycles_comp,
      &gfx12_write_unit_stalled_cycles_comp,
   },
};

static struct ac_spm_derived_counter_descr gfx103_ray_box_tests_counter = {
   .id = AC_SPM_COUNTER_RAY_BOX_TESTS,
   .group_id = AC_SPM_GROUP_RT,
   .name = "Ray-box tests",
   .desc = "The number of ray box intersection tests.",
   .usage = AC_SPM_USAGE_ITEMS,
   .num_components = 0,
};

static struct ac_spm_derived_counter_descr gfx103_ray_tri_tests_counter = {
   .id = AC_SPM_COUNTER_RAY_TRI_TESTS,
   .group_id = AC_SPM_GROUP_RT,
   .name = "Ray-triangle tests",
   .desc = "iThe number of ray triangle intersection tests",
   .usage = AC_SPM_USAGE_ITEMS,
   .num_components = 0,
};

/* SPM groups. */
/* GFX10+ */
static struct ac_spm_derived_group_descr gfx10_cache_group = {
   .id = AC_SPM_GROUP_CACHE,
   .name = "Cache",
   .num_counters = 5,
   .counters = {
      &gfx10_inst_cache_hit_counter,
      &gfx10_scalar_cache_hit_counter,
      &gfx10_l0_cache_hit_counter,
      &gfx10_l1_cache_hit_counter,
      &gfx10_l2_cache_hit_counter,
   },
};

static struct ac_spm_derived_group_descr gfx10_lds_group = {
   .id = AC_SPM_GROUP_LDS,
   .name = "LDS",
   .num_counters = 1,
   .counters = {
      &gfx10_cs_lds_bank_conflict_counter,
   },
};

static struct ac_spm_derived_group_descr gfx10_memory_bytes_group = {
   .id = AC_SPM_GROUP_MEMORY_BYTES,
   .name = "Memory (bytes)",
   .num_counters = 4,
   .counters = {
      &gfx10_fetch_size_counter,
      &gfx10_write_size_counter,
      &gfx10_local_vid_mem_bytes_counter,
      &gfx10_pcie_bytes_counter,
   },
};

static struct ac_spm_derived_group_descr gfx10_memory_percentage_group = {
   .id = AC_SPM_GROUP_MEMORY_PERCENTAGE,
   .name = "Memory (%)",
   .num_counters = 2,
   .counters = {
      &gfx10_mem_unit_busy_counter,
      &gfx10_mem_unit_stalled_counter,
   },
};

static struct ac_spm_derived_group_descr gfx103_rt_group = {
   .id = AC_SPM_GROUP_RT,
   .name = "Ray tracing",
   .num_counters = 2,
   .counters = {
      &gfx103_ray_box_tests_counter,
      &gfx103_ray_tri_tests_counter,
   },
};

/* GFX12+ */
static struct ac_spm_derived_group_descr gfx12_cache_group = {
   .id = AC_SPM_GROUP_CACHE,
   .name = "Cache",
   .num_counters = 4,
   .counters = {
      &gfx10_inst_cache_hit_counter,
      &gfx10_scalar_cache_hit_counter,
      &gfx10_l0_cache_hit_counter,
      &gfx10_l2_cache_hit_counter,
   },
};

static struct ac_spm_derived_group_descr gfx12_memory_percentage_group = {
   .id = AC_SPM_GROUP_MEMORY_PERCENTAGE,
   .name = "Memory (%)",
   .num_counters = 3,
   .counters = {
      &gfx10_mem_unit_busy_counter,
      &gfx10_mem_unit_stalled_counter,
      &gfx12_write_unit_stalled_counter,
   },
};

static struct ac_spm_derived_counter *
ac_spm_get_counter_by_id(struct ac_spm_derived_trace *spm_derived_trace,
                         enum ac_spm_counter_id counter_id)
{
   for (uint32_t i = 0; i < spm_derived_trace->num_counters; i++) {
      struct ac_spm_derived_counter *counter = &spm_derived_trace->counters[i];

      if (counter->descr->id == counter_id)
         return counter;
   }

   return NULL;
}

static struct ac_spm_derived_component *
ac_spm_get_component_by_id(struct ac_spm_derived_trace *spm_derived_trace,
                           enum ac_spm_component_id component_id)
{
   for (uint32_t i = 0; i < spm_derived_trace->num_components; i++) {
      struct ac_spm_derived_component *component = &spm_derived_trace->components[i];

      if (component->descr->id == component_id)
         return component;
   }

   return NULL;
}

static int
ac_spm_get_global_component_id(struct ac_spm_derived_trace *spm_derived_trace,
                               enum ac_spm_component_id component_id)
{
   for (uint32_t i = 0; i < spm_derived_trace->num_components; i++) {
      struct ac_spm_derived_component *component = &spm_derived_trace->components[i];

      if (component->descr->id == component_id)
         return i;
   }

   return -1;
}

static void
ac_spm_add_group(struct ac_spm_derived_trace *spm_derived_trace,
                 const struct ac_spm_derived_group_descr *group_descr)
{
   struct ac_spm_derived_group *group =
      &spm_derived_trace->groups[spm_derived_trace->num_groups];

   assert(spm_derived_trace->num_groups < AC_SPM_GROUP_COUNT);
   group->descr = group_descr;

   for (uint32_t i = 0; i < group_descr->num_counters; i++) {
      const struct ac_spm_derived_counter_descr *counter_descr =
         group_descr->counters[i];
      struct ac_spm_derived_counter *counter =
         &spm_derived_trace->counters[spm_derived_trace->num_counters];

      assert(spm_derived_trace->num_counters < AC_SPM_COUNTER_COUNT);
      counter->descr = counter_descr;

      for (uint32_t j = 0; j < counter_descr->num_components; j++) {
         /* A component can be used by different counters, re-use the same ID. */
         const int component_id =
            ac_spm_get_global_component_id(spm_derived_trace,
                                           counter_descr->components[j]->id);

         if (component_id != -1) {
            counter->component_ids[j] = component_id;
         } else {
            struct ac_spm_derived_component *component =
               &spm_derived_trace->components[spm_derived_trace->num_components];

            assert(spm_derived_trace->num_components < AC_SPM_COMPONENT_COUNT);
            component->descr = counter_descr->components[j];

            counter->component_ids[j] = spm_derived_trace->num_components;
            spm_derived_trace->num_components++;
         }
      }

      group->counter_ids[i] = spm_derived_trace->num_counters;
      spm_derived_trace->num_counters++;
   }

   spm_derived_trace->num_groups++;
}

static enum ac_spm_raw_counter_op
ac_spm_get_raw_counter_op(enum ac_spm_raw_counter_id id)
{
   switch (id) {
   case AC_SPM_TCP_PERF_SEL_REQ:
   case AC_SPM_TCP_PERF_SEL_REQ_MISS:
   case AC_SPM_SQC_PERF_SEL_DCACHE_HITS:
   case AC_SPM_SQC_PERF_SEL_DCACHE_MISSES:
   case AC_SPM_SQC_PERF_SEL_DCACHE_MISSES_DUPLICATE:
   case AC_SPM_SQC_PERF_SEL_ICACHE_HITS:
   case AC_SPM_SQC_PERF_SEL_ICACHE_MISSES:
   case AC_SPM_SQC_PERF_SEL_ICACHE_MISSES_DUPLICATE:
   case AC_SPM_GL1C_PERF_SEL_REQ:
   case AC_SPM_GL1C_PERF_SEL_REQ_MISS:
   case AC_SPM_GL2C_PERF_SEL_REQ:
   case AC_SPM_GL2C_PERF_SEL_MISS:
   case AC_SPM_CPF_PERF_SEL_STAT_BUSY:
   case AC_SPM_SQC_PERF_SEL_LDS_BANK_CONFLICT:
   case AC_SPM_GL2C_PERF_SEL_EA_RDREQ_32B:
   case AC_SPM_GL2C_PERF_SEL_EA_RDREQ_64B:
   case AC_SPM_GL2C_PERF_SEL_EA_RDREQ_96B:
   case AC_SPM_GL2C_PERF_SEL_EA_RDREQ_128B:
   case AC_SPM_GL2C_PERF_SEL_EA_RDREQ_256B:
   case AC_SPM_GL2C_PERF_SEL_EA_WRREQ:
   case AC_SPM_GL2C_PERF_SEL_EA_WRREQ_64B:
   case AC_SPM_GCEA_PERF_SEL_SARB_DRAM_SIZED_REQUESTS:
   case AC_SPM_GCEA_CPWD_PERF_SEL_SARB_DRAM_RD_SIZE_REQ:
   case AC_SPM_GCEA_CPWD_PERF_SEL_SARB_DRAM_WR_SIZE_REQ:
   case AC_SPM_GCEA_PERF_SEL_SARB_IO_SIZED_REQUESTS:
   case AC_SPM_GCEA_SE_PERF_SEL_SARB_IO_RD_SIZE_REQ:
   case AC_SPM_GCEA_SE_PERF_SEL_SARB_IO_WR_SIZE_REQ:
   case AC_SPM_TD_PERF_SEL_RAY_TRACING_BVH4_TRI_NODE:
   case AC_SPM_TD_PERF_SEL_RAY_TRACING_BVH4_FP16_BOX_NODE:
   case AC_SPM_TD_PERF_SEL_RAY_TRACING_BVH4_FP32_BOX_NODE:
      return AC_SPM_RAW_COUNTER_OP_SUM;
   case AC_SPM_TA_PERF_SEL_TA_BUSY:
   case AC_SPM_TCP_PERF_SEL_TCP_TA_REQ_STALL:
   case AC_SPM_GL2C_PERF_SEL_EA_WRREQ_STALL:
      return AC_SPM_RAW_COUNTER_OP_MAX;
   default:
      UNREACHABLE("Invalid SPM raw counter ID.");
   }
}

struct ac_spm_derived_trace *
ac_spm_get_derived_trace(const struct radeon_info *info,
                         const struct ac_spm_trace *spm_trace)
{
   uint32_t sample_size_in_bytes = spm_trace->sample_size_in_bytes;
   uint8_t *spm_data_ptr = (uint8_t *)spm_trace->ptr;
   struct ac_spm_derived_trace *spm_derived_trace;

   spm_derived_trace = calloc(1, sizeof(*spm_derived_trace));
   if (!spm_derived_trace)
      return NULL;

   /* Add groups to the trace. */
   if (info->gfx_level >= GFX12) {
      ac_spm_add_group(spm_derived_trace, &gfx12_cache_group);
   } else {
      ac_spm_add_group(spm_derived_trace, &gfx10_cache_group);
   }
   ac_spm_add_group(spm_derived_trace, &gfx10_lds_group);
   ac_spm_add_group(spm_derived_trace, &gfx10_memory_bytes_group);
   if (info->gfx_level >= GFX12) {
      ac_spm_add_group(spm_derived_trace, &gfx12_memory_percentage_group);
   } else {
      ac_spm_add_group(spm_derived_trace, &gfx10_memory_percentage_group);
   }
   if (info->gfx_level >= GFX10_3)
      ac_spm_add_group(spm_derived_trace, &gfx103_rt_group);

   spm_derived_trace->timestamps = malloc(spm_trace->num_samples * sizeof(uint64_t));
   if (!spm_derived_trace->timestamps) {
      free(spm_derived_trace);
      return NULL;
   }

   /* Skip the reserved 32 bytes of data at beginning. */
   spm_data_ptr += 32;

   /* Collect timestamps. */
   uint64_t sample_size_in_qwords = sample_size_in_bytes / sizeof(uint64_t);
   uint64_t *timestamp_ptr = (uint64_t *)spm_data_ptr;

   for (uint32_t i = 0; i < spm_trace->num_samples; i++) {
      uint64_t index = i * sample_size_in_qwords;
      uint64_t timestamp = timestamp_ptr[index];

      spm_derived_trace->timestamps[i] = timestamp;
   }

   /* Collect raw counter values. */
   uint64_t *raw_counter_values[AC_SPM_RAW_COUNTER_ID_COUNT];
   for (uint32_t i = 0; i < AC_SPM_RAW_COUNTER_ID_COUNT; i++) {
      raw_counter_values[i] = calloc(spm_trace->num_samples, sizeof(uint64_t));
   }

   const uint32_t sample_size_in_hwords = sample_size_in_bytes / sizeof(uint16_t);
   const uint16_t *counter_values_ptr = (uint16_t *)spm_data_ptr;

   for (uint32_t c = 0; c < spm_trace->num_counters; c++) {
      const uint64_t offset = spm_trace->counters[c].offset;
      const uint32_t id = spm_trace->counters[c].id;
      const enum ac_spm_raw_counter_op op = ac_spm_get_raw_counter_op(id);

      for (uint32_t s = 0; s < spm_trace->num_samples; s++) {
         const uint64_t index = offset + (s * sample_size_in_hwords);
         const uint16_t value = counter_values_ptr[index];

         switch (op) {
         case AC_SPM_RAW_COUNTER_OP_SUM:
            raw_counter_values[id][s] += value;
            break;
         case AC_SPM_RAW_COUNTER_OP_MAX:
            raw_counter_values[id][s] = MAX2(raw_counter_values[id][s], value);
            break;
         default:
            UNREACHABLE("Invalid SPM raw counter OP.\n");
         }
      }
   }

#define GET_COMPONENT(n) \
   struct ac_spm_derived_component *_##n = \
      ac_spm_get_component_by_id(spm_derived_trace, AC_SPM_COMPONENT_##n);
#define GET_COUNTER(n) \
   struct ac_spm_derived_counter *_##n = \
      ac_spm_get_counter_by_id(spm_derived_trace, AC_SPM_COUNTER_##n);

   GET_COUNTER(INST_CACHE_HIT);
   GET_COUNTER(SCALAR_CACHE_HIT);
   GET_COUNTER(L0_CACHE_HIT);
   GET_COUNTER(L1_CACHE_HIT);
   GET_COUNTER(L2_CACHE_HIT);
   GET_COUNTER(CS_LDS_BANK_CONFLICT);
   GET_COUNTER(FETCH_SIZE);
   GET_COUNTER(WRITE_SIZE);
   GET_COUNTER(LOCAL_VID_MEM_BYTES);
   GET_COUNTER(PCIE_BYTES);
   GET_COUNTER(MEM_UNIT_BUSY);
   GET_COUNTER(MEM_UNIT_STALLED);
   GET_COUNTER(WRITE_UNIT_STALLED);
   GET_COUNTER(RAY_BOX_TESTS);
   GET_COUNTER(RAY_TRI_TESTS);

   GET_COMPONENT(INST_CACHE_REQUEST_COUNT);
   GET_COMPONENT(INST_CACHE_HIT_COUNT);
   GET_COMPONENT(INST_CACHE_MISS_COUNT);
   GET_COMPONENT(SCALAR_CACHE_REQUEST_COUNT);
   GET_COMPONENT(SCALAR_CACHE_HIT_COUNT);
   GET_COMPONENT(SCALAR_CACHE_MISS_COUNT);
   GET_COMPONENT(L0_CACHE_REQUEST_COUNT);
   GET_COMPONENT(L0_CACHE_HIT_COUNT);
   GET_COMPONENT(L0_CACHE_MISS_COUNT);
   GET_COMPONENT(L1_CACHE_REQUEST_COUNT);
   GET_COMPONENT(L1_CACHE_HIT_COUNT);
   GET_COMPONENT(L1_CACHE_MISS_COUNT);
   GET_COMPONENT(L2_CACHE_REQUEST_COUNT);
   GET_COMPONENT(L2_CACHE_HIT_COUNT);
   GET_COMPONENT(L2_CACHE_MISS_COUNT);
   GET_COMPONENT(GPU_BUSY_CYCLES);
   GET_COMPONENT(CS_LDS_BANK_CONFLICT_CYCLES);
   GET_COMPONENT(MEM_UNIT_BUSY_CYCLES);
   GET_COMPONENT(MEM_UNIT_STALLED_CYCLES);
   GET_COMPONENT(WRITE_UNIT_STALLED_CYCLES);

#undef GET_COMPONENT
#undef GET_COUNTER

#define ADD(id, value) \
   util_dynarray_append(&_##id->values, (double)(value));

#define OP_RAW(n) \
   raw_counter_values[AC_SPM_##n][s]
#define OP_SUM2(a, b) \
   raw_counter_values[AC_SPM_##a][s] + \
   raw_counter_values[AC_SPM_##b][s]
#define OP_SUM3(a, b, c) \
   raw_counter_values[AC_SPM_##a][s] + \
   raw_counter_values[AC_SPM_##b][s] + \
   raw_counter_values[AC_SPM_##c][s]
#define OP_SUB2(a, b) \
   raw_counter_values[AC_SPM_##a][s] - \
   raw_counter_values[AC_SPM_##b][s]

   const uint32_t num_simds = info->num_cu * info->compiler_info.num_simd_per_compute_unit;

   for (uint32_t s = 0; s < spm_trace->num_samples; s++) {
      /* Cache group. */
      /* Instruction cache. */
      const double inst_cache_request_count =
         OP_SUM3(SQC_PERF_SEL_ICACHE_HITS, SQC_PERF_SEL_ICACHE_MISSES, SQC_PERF_SEL_ICACHE_MISSES_DUPLICATE);
      const double inst_cache_hit_count =
         OP_RAW(SQC_PERF_SEL_ICACHE_HITS);
      const double inst_cache_miss_count =
         OP_SUM2(SQC_PERF_SEL_ICACHE_MISSES, SQC_PERF_SEL_ICACHE_MISSES_DUPLICATE);
      const double inst_cache_hit =
         inst_cache_request_count ? (inst_cache_hit_count / inst_cache_request_count) * 100.0f : 0.0f;

      ADD(INST_CACHE_REQUEST_COUNT, inst_cache_request_count);
      ADD(INST_CACHE_HIT_COUNT, inst_cache_hit_count);
      ADD(INST_CACHE_MISS_COUNT, inst_cache_miss_count);
      ADD(INST_CACHE_HIT, inst_cache_hit);

      /* Scalar cache. */
      const double scalar_cache_request_count =
         OP_SUM3(SQC_PERF_SEL_DCACHE_HITS, SQC_PERF_SEL_DCACHE_MISSES, SQC_PERF_SEL_DCACHE_MISSES_DUPLICATE);
      const double scalar_cache_hit_count =
         OP_RAW(SQC_PERF_SEL_DCACHE_HITS);
      const double scalar_cache_miss_count =
         OP_SUM2(SQC_PERF_SEL_DCACHE_MISSES, SQC_PERF_SEL_DCACHE_MISSES_DUPLICATE);
      const double scalar_cache_hit =
         scalar_cache_request_count ? (scalar_cache_hit_count / scalar_cache_request_count) * 100.0f : 0.0f;

      ADD(SCALAR_CACHE_REQUEST_COUNT, scalar_cache_request_count);
      ADD(SCALAR_CACHE_HIT_COUNT, scalar_cache_hit_count);
      ADD(SCALAR_CACHE_MISS_COUNT, scalar_cache_miss_count);
      ADD(SCALAR_CACHE_HIT, scalar_cache_hit);

      /* L0 cache. */
      const double l0_cache_request_count = OP_RAW(TCP_PERF_SEL_REQ);
      const double l0_cache_hit_count = OP_SUB2(TCP_PERF_SEL_REQ, TCP_PERF_SEL_REQ_MISS);
      const double l0_cache_miss_count = OP_RAW(TCP_PERF_SEL_REQ_MISS);
      const double l0_cache_hit =
         l0_cache_request_count ? (l0_cache_hit_count / l0_cache_request_count) * 100.0f : 0.0f;

      ADD(L0_CACHE_REQUEST_COUNT, l0_cache_request_count);
      ADD(L0_CACHE_HIT_COUNT, l0_cache_hit_count);
      ADD(L0_CACHE_MISS_COUNT, l0_cache_miss_count);
      ADD(L0_CACHE_HIT, l0_cache_hit);

      if (info->gfx_level < GFX12) {
         /* L1 cache. */
         const double l1_cache_request_count = OP_RAW(GL1C_PERF_SEL_REQ);
         const double l1_cache_hit_count = OP_SUB2(GL1C_PERF_SEL_REQ, GL1C_PERF_SEL_REQ_MISS);
         const double l1_cache_miss_count = OP_RAW(GL1C_PERF_SEL_REQ_MISS);
         const double l1_cache_hit =
            l1_cache_request_count ? (l1_cache_hit_count / l1_cache_request_count) * 100.0f : 0.0f;

         ADD(L1_CACHE_REQUEST_COUNT, l1_cache_request_count);
         ADD(L1_CACHE_HIT_COUNT, l1_cache_hit_count);
         ADD(L1_CACHE_MISS_COUNT, l1_cache_miss_count);
         ADD(L1_CACHE_HIT, l1_cache_hit);
      }

      /* L2 cache. */
      const double l2_cache_request_count = OP_RAW(GL2C_PERF_SEL_REQ);
      const double l2_cache_hit_count = OP_SUB2(GL2C_PERF_SEL_REQ, GL2C_PERF_SEL_MISS);
      const double l2_cache_miss_count = OP_RAW(GL2C_PERF_SEL_MISS);
      const double l2_cache_hit =
         l2_cache_request_count ? (l2_cache_hit_count / l2_cache_request_count) * 100.0f : 0.0f;

      ADD(L2_CACHE_REQUEST_COUNT, l2_cache_request_count);
      ADD(L2_CACHE_HIT_COUNT, l2_cache_hit_count);
      ADD(L2_CACHE_MISS_COUNT, l2_cache_miss_count);
      ADD(L2_CACHE_HIT, l2_cache_hit);

      /* LDS group */
      /* CS LDS Bank Conflict. */
      const double gpu_busy_cycles = OP_RAW(CPF_PERF_SEL_STAT_BUSY);
      const double cs_lds_bank_conflict_cycles = OP_RAW(SQC_PERF_SEL_LDS_BANK_CONFLICT) / (double)num_simds;
      const double cs_lds_bank_conflict =
         gpu_busy_cycles ? (cs_lds_bank_conflict_cycles / gpu_busy_cycles) * 100.0f : 0.0f;

      ADD(GPU_BUSY_CYCLES, gpu_busy_cycles);
      ADD(CS_LDS_BANK_CONFLICT_CYCLES, cs_lds_bank_conflict_cycles);
      ADD(CS_LDS_BANK_CONFLICT, cs_lds_bank_conflict);

      /* Memmory (bytes) group. */
      /* Fetch size. */
      double fetch_size;

      if (info->gfx_level >= GFX12) {
         fetch_size = OP_RAW(GL2C_PERF_SEL_EA_RDREQ_32B) * 32 +
                      OP_RAW(GL2C_PERF_SEL_EA_RDREQ_64B) * 64 +
                      OP_RAW(GL2C_PERF_SEL_EA_RDREQ_128B) * 128 +
                      OP_RAW(GL2C_PERF_SEL_EA_RDREQ_256B) * 256;
      } else {
         fetch_size = OP_RAW(GL2C_PERF_SEL_EA_RDREQ_32B) * 32 +
                      OP_RAW(GL2C_PERF_SEL_EA_RDREQ_64B) * 64 +
                      OP_RAW(GL2C_PERF_SEL_EA_RDREQ_96B) * 96 +
                      OP_RAW(GL2C_PERF_SEL_EA_RDREQ_128B) * 128;
      }

      ADD(FETCH_SIZE, fetch_size);

      /* Write size. */
      const double write_size = (OP_RAW(GL2C_PERF_SEL_EA_WRREQ) * 32 +
                                 OP_RAW(GL2C_PERF_SEL_EA_WRREQ_64B) * 64) -
                                (OP_RAW(GL2C_PERF_SEL_EA_WRREQ_64B) * 32);

      ADD(WRITE_SIZE, write_size);

      /* Local video mem bytes. */
      double local_vid_mem_bytes;

      if (info->gfx_level >= GFX12) {
         local_vid_mem_bytes = (OP_RAW(GCEA_CPWD_PERF_SEL_SARB_DRAM_RD_SIZE_REQ) +
                                OP_RAW(GCEA_CPWD_PERF_SEL_SARB_DRAM_WR_SIZE_REQ)) * 32;
      } else {
         local_vid_mem_bytes = OP_RAW(GCEA_PERF_SEL_SARB_DRAM_SIZED_REQUESTS) * 32;
      }

      ADD(LOCAL_VID_MEM_BYTES, local_vid_mem_bytes);

      /* PCIe bytes. */
      double pcie_bytes;

      if (info->gfx_level >= GFX12) {
         pcie_bytes = (OP_RAW(GCEA_SE_PERF_SEL_SARB_IO_RD_SIZE_REQ) +
                       OP_RAW(GCEA_SE_PERF_SEL_SARB_IO_WR_SIZE_REQ)) * 32;
      } else {
         pcie_bytes = OP_RAW(GCEA_PERF_SEL_SARB_IO_SIZED_REQUESTS) * 32;
      }

      ADD(PCIE_BYTES, pcie_bytes);

      /* Memory (percentage) group. */
      /* Memory unit busy. */
      const double mem_unit_busy_cycles = OP_RAW(TA_PERF_SEL_TA_BUSY);
      const double mem_unit_busy =
         gpu_busy_cycles ? (mem_unit_busy_cycles / gpu_busy_cycles) * 100.0f : 0.0f;

      ADD(MEM_UNIT_BUSY_CYCLES, mem_unit_busy_cycles);
      ADD(MEM_UNIT_BUSY, mem_unit_busy);

      /* Memory unit stalled. */
      const double mem_unit_stalled_cycles = OP_RAW(TCP_PERF_SEL_TCP_TA_REQ_STALL);
      const double mem_unit_stalled =
         gpu_busy_cycles ? (mem_unit_stalled_cycles / gpu_busy_cycles) * 100.0f : 0.f;

      ADD(MEM_UNIT_STALLED_CYCLES, mem_unit_stalled_cycles);
      ADD(MEM_UNIT_STALLED, mem_unit_stalled);

      if (info->gfx_level >= GFX12) {
         /* Write unit stalled. */
         const double write_unit_stalled_cycles = OP_RAW(GL2C_PERF_SEL_EA_WRREQ_STALL);
         const double write_unit_stalled =
            gpu_busy_cycles ? (write_unit_stalled_cycles / gpu_busy_cycles) * 100.0f : 0.0f;

         ADD(WRITE_UNIT_STALLED_CYCLES, write_unit_stalled_cycles);
         ADD(WRITE_UNIT_STALLED, write_unit_stalled);
      }

      if (info->gfx_level >= GFX10_3) {
         /* Raytracing group. */
         /* Ray box tests. */
         const double ray_box_tests = OP_RAW(TD_PERF_SEL_RAY_TRACING_BVH4_FP16_BOX_NODE) +
                                      OP_RAW(TD_PERF_SEL_RAY_TRACING_BVH4_FP32_BOX_NODE);

         ADD(RAY_BOX_TESTS, ray_box_tests);

         /* Ray triangle tests. */
         const double ray_tri_tests = OP_RAW(TD_PERF_SEL_RAY_TRACING_BVH4_TRI_NODE);

         ADD(RAY_TRI_TESTS, ray_tri_tests);
      }
   }

#undef ADD
#undef OP_RAW
#undef OP_SUM2
#undef OP_SUM3
#undef OP_SUB2

   spm_derived_trace->num_timestamps = spm_trace->num_samples;
   spm_derived_trace->sample_interval = spm_trace->sample_interval;

   for (uint32_t i = 0; i < AC_SPM_RAW_COUNTER_ID_COUNT; i++)
      free(raw_counter_values[i]);

   return spm_derived_trace;
}

void
ac_spm_destroy_derived_trace(struct ac_spm_derived_trace *spm_derived_trace)
{
   for (uint32_t i = 0; i < spm_derived_trace->num_components; i++) {
      struct ac_spm_derived_component *component = &spm_derived_trace->components[i];
      util_dynarray_fini(&component->values);
   }

   for (uint32_t i = 0; i < spm_derived_trace->num_counters; i++) {
      struct ac_spm_derived_counter *counter = &spm_derived_trace->counters[i];
      util_dynarray_fini(&counter->values);
   }

   free(spm_derived_trace);
}

static void
ac_emit_spm_muxsel(struct ac_cmdbuf *cs, enum amd_gfx_level gfx_level,
                   enum amd_ip_type ip_type, const struct ac_spm *spm)
{
   /* Upload each muxsel ram to the RLC. */
   for (unsigned s = 0; s < AC_SPM_SEGMENT_TYPE_COUNT; s++) {
      unsigned rlc_muxsel_addr, rlc_muxsel_data;
      unsigned grbm_gfx_index = S_030800_SH_BROADCAST_WRITES(1) |
                                S_030800_INSTANCE_BROADCAST_WRITES(1);

      if (!spm->num_muxsel_lines[s])
         continue;

      if (s == AC_SPM_SEGMENT_TYPE_GLOBAL) {
         grbm_gfx_index |= S_030800_SE_BROADCAST_WRITES(1);

         rlc_muxsel_addr = gfx_level >= GFX11 ? R_037220_RLC_SPM_GLOBAL_MUXSEL_ADDR
                                              : R_037224_RLC_SPM_GLOBAL_MUXSEL_ADDR;
         rlc_muxsel_data = gfx_level >= GFX11 ? R_037224_RLC_SPM_GLOBAL_MUXSEL_DATA
                                              : R_037228_RLC_SPM_GLOBAL_MUXSEL_DATA;
      } else {
         grbm_gfx_index |= S_030800_SE_INDEX(s);

         rlc_muxsel_addr = gfx_level >= GFX11 ? R_037228_RLC_SPM_SE_MUXSEL_ADDR
                                              : R_03721C_RLC_SPM_SE_MUXSEL_ADDR;
         rlc_muxsel_data = gfx_level >= GFX11 ? R_03722C_RLC_SPM_SE_MUXSEL_DATA
                                              : R_037220_RLC_SPM_SE_MUXSEL_DATA;
      }

      ac_cmdbuf_begin(cs);

      ac_cmdbuf_set_ucfg_reg(R_030800_GRBM_GFX_INDEX, grbm_gfx_index);

      for (unsigned l = 0; l < spm->num_muxsel_lines[s]; l++) {
         uint32_t *data = (uint32_t *)spm->muxsel_lines[s][l].muxsel;

         /* Select MUXSEL_ADDR to point to the next muxsel. */
         ac_cmdbuf_set_ucfg_perfctr_reg(gfx_level, ip_type, rlc_muxsel_addr,
                                        l * AC_SPM_MUXSEL_LINE_SIZE);

         /* Write the muxsel line configuration with MUXSEL_DATA. */
         ac_cmdbuf_emit(PKT3(PKT3_WRITE_DATA, 2 + AC_SPM_MUXSEL_LINE_SIZE, 0));
         ac_cmdbuf_emit(S_371_DST_SEL(V_371_MEM_MAPPED_REGISTER) |
                        S_371_WR_CONFIRM(V_371_WAIT_FOR_WRITE_CONFIRMATION) |
                        S_371_ENGINE_SEL(V_371_MICRO_ENGINE) |
                        S_371_ADDR_INCR(V_371_DO_NOT_INCREMENT_ADDRESS));
         ac_cmdbuf_emit(rlc_muxsel_data >> 2);
         ac_cmdbuf_emit(0);
         ac_cmdbuf_emit_array(data, AC_SPM_MUXSEL_LINE_SIZE);
      }

      ac_cmdbuf_end();
   }
}

static void
ac_emit_spm_counters(struct ac_cmdbuf *cs, enum amd_gfx_level gfx_level,
                     enum amd_ip_type ip_type,
                     const struct ac_spm *spm)
{
   if (gfx_level >= GFX11) {
      for (uint32_t instance = 0; instance < ARRAY_SIZE(spm->sq_wgp); instance++) {
         uint32_t num_counters = spm->sq_wgp[instance].num_counters;

         if (!num_counters)
            continue;

         ac_cmdbuf_begin(cs);
         ac_cmdbuf_set_ucfg_reg(R_030800_GRBM_GFX_INDEX, spm->sq_wgp[instance].grbm_gfx_index);

         for (uint32_t b = 0; b < num_counters; b++) {
            const struct ac_spm_counter_select *cntr_sel = &spm->sq_wgp[instance].counters[b];
            uint32_t reg_base = R_036700_SQ_PERFCOUNTER0_SELECT;

            ac_cmdbuf_set_ucfg_perfctr_reg_seq(gfx_level, ip_type,
                                               reg_base + b * 4, 1);
            ac_cmdbuf_emit(cntr_sel->sel0);
         }

         ac_cmdbuf_end();
      }
   }

   for (uint32_t instance = 0; instance < ARRAY_SIZE(spm->sqg); instance++) {
      uint32_t num_counters = spm->sqg[instance].num_counters;

      if (!num_counters)
         continue;

      ac_cmdbuf_begin(cs);
      ac_cmdbuf_set_ucfg_reg(R_030800_GRBM_GFX_INDEX, S_030800_SH_BROADCAST_WRITES(1) |
                                                      S_030800_INSTANCE_BROADCAST_WRITES(1) |
                                                      S_030800_SE_INDEX(instance));

      for (uint32_t b = 0; b < num_counters; b++) {
         const struct ac_spm_counter_select *cntr_sel = &spm->sqg[instance].counters[b];
         uint32_t reg_base = R_036700_SQ_PERFCOUNTER0_SELECT;

         ac_cmdbuf_set_ucfg_perfctr_reg_seq(gfx_level, ip_type,
                                            reg_base + b * 4, 1);
         ac_cmdbuf_emit(cntr_sel->sel0 | S_036700_SQC_BANK_MASK(0xf)); /* SQC_BANK_MASK only gfx10 */
      }

      ac_cmdbuf_end();
   }

   for (uint32_t b = 0; b < spm->num_block_sel; b++) {
      struct ac_spm_block_select *block_sel = &spm->block_sel[b];
      struct ac_pc_block_base *regs = block_sel->b->b->b;

      for (unsigned i = 0; i < block_sel->num_instances; i++) {
         struct ac_spm_block_instance *block_instance = &block_sel->instances[i];

         ac_cmdbuf_begin(cs);
         ac_cmdbuf_set_ucfg_reg(R_030800_GRBM_GFX_INDEX, block_instance->grbm_gfx_index);

         for (unsigned c = 0; c < block_instance->num_counters; c++) {
            const struct ac_spm_counter_select *cntr_sel = &block_instance->counters[c];

            if (!cntr_sel->active)
               continue;

            ac_cmdbuf_set_ucfg_perfctr_reg_seq(gfx_level, ip_type, regs->select0[c], 1);
            ac_cmdbuf_emit(cntr_sel->sel0);

            ac_cmdbuf_set_ucfg_perfctr_reg_seq(gfx_level, ip_type, regs->select1[c], 1);
            ac_cmdbuf_emit(cntr_sel->sel1);
         }

         ac_cmdbuf_end();
      }
   }

   /* Restore global broadcasting. */
   ac_cmdbuf_begin(cs);
   ac_cmdbuf_set_ucfg_reg(R_030800_GRBM_GFX_INDEX, S_030800_SE_BROADCAST_WRITES(1) |
                                                   S_030800_SH_BROADCAST_WRITES(1) |
                                                   S_030800_INSTANCE_BROADCAST_WRITES(1));
   ac_cmdbuf_end();
}

void
ac_emit_spm_setup(struct ac_cmdbuf *cs, enum amd_gfx_level gfx_level,
                  enum amd_ip_type ip_type, const struct ac_spm *spm,
                  uint64_t va)
{
   /* It's required that the ring VA and the size are correctly aligned. */
   assert(!(va & (AC_SPM_RING_BASE_ALIGN - 1)));
   assert(!(spm->buffer_size & (AC_SPM_RING_BASE_ALIGN - 1)));
   assert(spm->sample_interval >= 32);

   ac_cmdbuf_begin(cs);

   /* Configure the SPM ring buffer. */
   ac_cmdbuf_set_ucfg_reg(R_037200_RLC_SPM_PERFMON_CNTL,
                          S_037200_PERFMON_RING_MODE(0) | /* no stall and no interrupt on overflow */
                          S_037200_PERFMON_SAMPLE_INTERVAL(spm->sample_interval)); /* in sclk */
   ac_cmdbuf_set_ucfg_reg(R_037204_RLC_SPM_PERFMON_RING_BASE_LO, va);
   ac_cmdbuf_set_ucfg_reg(R_037208_RLC_SPM_PERFMON_RING_BASE_HI,
                          S_037208_RING_BASE_HI(va >> 32));
   ac_cmdbuf_set_ucfg_reg(R_03720C_RLC_SPM_PERFMON_RING_SIZE, spm->buffer_size);

   /* Configure the muxsel. */
   uint32_t total_muxsel_lines = 0;
   for (unsigned s = 0; s < AC_SPM_SEGMENT_TYPE_COUNT; s++) {
      total_muxsel_lines += spm->num_muxsel_lines[s];
   }

   ac_cmdbuf_set_ucfg_reg(R_03726C_RLC_SPM_ACCUM_MODE, 0);

   if (gfx_level >= GFX11) {
      ac_cmdbuf_set_ucfg_reg(R_03721C_RLC_SPM_PERFMON_SEGMENT_SIZE,
                             S_03721C_TOTAL_NUM_SEGMENT(total_muxsel_lines) |
                             S_03721C_GLOBAL_NUM_SEGMENT(spm->num_muxsel_lines[AC_SPM_SEGMENT_TYPE_GLOBAL]) |
                             S_03721C_SE_NUM_SEGMENT(spm->max_se_muxsel_lines));

      ac_cmdbuf_set_ucfg_reg(R_037210_RLC_SPM_RING_WRPTR, 0);
   } else {
      ac_cmdbuf_set_ucfg_reg(R_037210_RLC_SPM_PERFMON_SEGMENT_SIZE, 0);
      ac_cmdbuf_set_ucfg_reg(R_03727C_RLC_SPM_PERFMON_SE3TO0_SEGMENT_SIZE,
                             S_03727C_SE0_NUM_LINE(spm->num_muxsel_lines[AC_SPM_SEGMENT_TYPE_SE0]) |
                             S_03727C_SE1_NUM_LINE(spm->num_muxsel_lines[AC_SPM_SEGMENT_TYPE_SE1]) |
                             S_03727C_SE2_NUM_LINE(spm->num_muxsel_lines[AC_SPM_SEGMENT_TYPE_SE2]) |
                             S_03727C_SE3_NUM_LINE(spm->num_muxsel_lines[AC_SPM_SEGMENT_TYPE_SE3]));
      ac_cmdbuf_set_ucfg_reg(R_037280_RLC_SPM_PERFMON_GLB_SEGMENT_SIZE,
                             S_037280_PERFMON_SEGMENT_SIZE(total_muxsel_lines) |
                             S_037280_GLOBAL_NUM_LINE(spm->num_muxsel_lines[AC_SPM_SEGMENT_TYPE_GLOBAL]));
   }

   ac_cmdbuf_end();

   /* Upload each muxsel ram to the RLC. */
   ac_emit_spm_muxsel(cs, gfx_level, ip_type, spm);

   /* Select SPM counters. */
   ac_emit_spm_counters(cs, gfx_level, ip_type, spm);
}

void
ac_emit_spm_start(struct ac_cmdbuf *cs, enum amd_ip_type ip_type,
                  const struct radeon_info *info)
{
   /* Start SPM counters. */
   ac_cmdbuf_begin(cs);
   ac_cmdbuf_set_ucfg_reg(R_036020_CP_PERFMON_CNTL,
                          S_036020_PERFMON_STATE(V_036020_CP_PERFMON_STATE_DISABLE_AND_RESET) |
                          S_036020_SPM_PERFMON_STATE(V_036020_STRM_PERFMON_STATE_START_COUNTING));
   ac_cmdbuf_end();

   /* Start windowed performance counters. */
   ac_emit_cp_update_windowed_counters(cs, info, ip_type, true);
}

void
ac_emit_spm_stop(struct ac_cmdbuf *cs, enum amd_ip_type ip_type,
                 const struct radeon_info *info)
{
   /* Stop windowed performance counters. */
   ac_emit_cp_update_windowed_counters(cs, info, ip_type, false);

   /* Stop SPM counters. */
   ac_cmdbuf_begin(cs);
   ac_cmdbuf_set_ucfg_reg(R_036020_CP_PERFMON_CNTL,
                          S_036020_PERFMON_STATE(V_036020_CP_PERFMON_STATE_DISABLE_AND_RESET) |
                          S_036020_SPM_PERFMON_STATE(info->never_stop_sq_perf_counters ?
                             V_036020_STRM_PERFMON_STATE_START_COUNTING :
                             V_036020_STRM_PERFMON_STATE_STOP_COUNTING));
   ac_cmdbuf_end();
}

void
ac_emit_spm_reset(struct ac_cmdbuf *cs)
{
   ac_cmdbuf_begin(cs);
   ac_cmdbuf_set_ucfg_reg(R_036020_CP_PERFMON_CNTL,
                          S_036020_PERFMON_STATE(V_036020_CP_PERFMON_STATE_DISABLE_AND_RESET) |
                          S_036020_SPM_PERFMON_STATE(V_036020_STRM_PERFMON_STATE_DISABLE_AND_RESET));
   ac_cmdbuf_end();
}
