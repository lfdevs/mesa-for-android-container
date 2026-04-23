/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_gpu_info.h"
#include "ac_perfcounter.h"
#include "ac_spm.h"

#include "util/u_memory.h"
#include "util/macros.h"

/* cik_CB */
static unsigned cik_CB_select0[] = {
   R_037004_CB_PERFCOUNTER0_SELECT,
   R_03700C_CB_PERFCOUNTER1_SELECT,
   R_037010_CB_PERFCOUNTER2_SELECT,
   R_037014_CB_PERFCOUNTER3_SELECT,
};
static unsigned cik_CB_select1[] = {
   R_037008_CB_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base cik_CB = {
   .gpu_block = CB,
   .name = "CB",
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_INSTANCE_GROUPS,

   .select0 = cik_CB_select0,
   .select1 = cik_CB_select1,
   .counter0_lo = R_035018_CB_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_SE_BLOCK_CB,
};

/* cik_CPC */
static unsigned cik_CPC_select0[] = {
   R_036024_CPC_PERFCOUNTER0_SELECT,
   R_03600C_CPC_PERFCOUNTER1_SELECT,
};
static unsigned cik_CPC_select1[] = {
   R_036010_CPC_PERFCOUNTER0_SELECT1,
};
static unsigned cik_CPC_counters[] = {
   R_034018_CPC_PERFCOUNTER0_LO,
   R_034010_CPC_PERFCOUNTER1_LO,
};
static struct ac_pc_block_base cik_CPC = {
   .gpu_block = CPC,
   .name = "CPC",
   .num_counters = 2,

   .select0 = cik_CPC_select0,
   .select1 = cik_CPC_select1,
   .counters = cik_CPC_counters,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_CPC,
};

/* cik_CPF */
static unsigned cik_CPF_select0[] = {
   R_03601C_CPF_PERFCOUNTER0_SELECT,
   R_036014_CPF_PERFCOUNTER1_SELECT,
};
static unsigned cik_CPF_select1[] = {
   R_036018_CPF_PERFCOUNTER0_SELECT1,
};
static unsigned cik_CPF_counters[] = {
   R_034028_CPF_PERFCOUNTER0_LO,
   R_034020_CPF_PERFCOUNTER1_LO,
};
static struct ac_pc_block_base cik_CPF = {
   .gpu_block = CPF,
   .name = "CPF",
   .num_counters = 2,

   .select0 = cik_CPF_select0,
   .select1 = cik_CPF_select1,
   .counters = cik_CPF_counters,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_CPF,
};

/* cik_CPG */
static unsigned cik_CPG_select0[] = {
   R_036008_CPG_PERFCOUNTER0_SELECT,
   R_036000_CPG_PERFCOUNTER1_SELECT,
};
static unsigned cik_CPG_select1[] = {
   R_036004_CPG_PERFCOUNTER0_SELECT1
};
static unsigned cik_CPG_counters[] = {
   R_034008_CPG_PERFCOUNTER0_LO,
   R_034000_CPG_PERFCOUNTER1_LO,
};
static struct ac_pc_block_base cik_CPG = {
   .gpu_block = CPG,
   .name = "CPG",
   .num_counters = 2,

   .select0 = cik_CPG_select0,
   .select1 = cik_CPG_select1,
   .counters = cik_CPG_counters,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_CPG,
};

/* cik_DB */
static unsigned cik_DB_select0[] = {
   R_037100_DB_PERFCOUNTER0_SELECT,
   R_037108_DB_PERFCOUNTER1_SELECT,
   R_037110_DB_PERFCOUNTER2_SELECT,
   R_037118_DB_PERFCOUNTER3_SELECT,
};
static unsigned cik_DB_select1[] = {
   R_037104_DB_PERFCOUNTER0_SELECT1,
   R_03710C_DB_PERFCOUNTER1_SELECT1,
};
static struct ac_pc_block_base cik_DB = {
   .gpu_block = DB,
   .name = "DB",
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_INSTANCE_GROUPS,

   .select0 = cik_DB_select0,
   .select1 = cik_DB_select1,
   .counter0_lo = R_035100_DB_PERFCOUNTER0_LO,

   .num_spm_modules = 2,
   .num_spm_wires = 3,
};

/* cik_GDS */
static unsigned cik_GDS_select0[] = {
   R_036A00_GDS_PERFCOUNTER0_SELECT,
   R_036A04_GDS_PERFCOUNTER1_SELECT,
   R_036A08_GDS_PERFCOUNTER2_SELECT,
   R_036A0C_GDS_PERFCOUNTER3_SELECT,
};
static unsigned cik_GDS_select1[] = {
   R_036A10_GDS_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base cik_GDS = {
   .gpu_block = GDS,
   .name = "GDS",
   .num_counters = 4,

   .select0 = cik_GDS_select0,
   .select1 = cik_GDS_select1,
   .counter0_lo = R_034A00_GDS_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_GDS,
};

/* cik_GRBM */
static unsigned cik_GRBM_select0[] = {
   R_036100_GRBM_PERFCOUNTER0_SELECT,
   R_036104_GRBM_PERFCOUNTER1_SELECT,
};
static unsigned cik_GRBM_counters[] = {
   R_034100_GRBM_PERFCOUNTER0_LO,
   R_03410C_GRBM_PERFCOUNTER1_LO,
};
static struct ac_pc_block_base cik_GRBM = {
   .gpu_block = GRBM,
   .name = "GRBM",
   .num_counters = 2,

   .select0 = cik_GRBM_select0,
   .counters = cik_GRBM_counters,
};

/* cik_GRBMSE */
static unsigned cik_GRBMSE_select0[] = {
   R_036108_GRBM_SE0_PERFCOUNTER_SELECT,
   R_03610C_GRBM_SE1_PERFCOUNTER_SELECT,
   R_036110_GRBM_SE2_PERFCOUNTER_SELECT,
   R_036114_GRBM_SE3_PERFCOUNTER_SELECT,
};
static struct ac_pc_block_base cik_GRBMSE = {
   .gpu_block = GRBMSE,
   .name = "GRBMSE",
   .num_counters = 4,

   .select0 = cik_GRBMSE_select0,
   .counter0_lo = R_034114_GRBM_SE0_PERFCOUNTER_LO,
};

/* cik_IA */
static unsigned cik_IA_select0[] = {
   R_036210_IA_PERFCOUNTER0_SELECT,
   R_036214_IA_PERFCOUNTER1_SELECT,
   R_036218_IA_PERFCOUNTER2_SELECT,
   R_03621C_IA_PERFCOUNTER3_SELECT,
};
static unsigned cik_IA_select1[] = {
   R_036220_IA_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base cik_IA = {
   .gpu_block = IA,
   .name = "IA",
   .num_counters = 4,

   .select0 = cik_IA_select0,
   .select1 = cik_IA_select1,
   .counter0_lo = R_034220_IA_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
};

/* cik_PA_SC */
static unsigned cik_PA_SC_select0[] = {
   R_036500_PA_SC_PERFCOUNTER0_SELECT,
   R_036508_PA_SC_PERFCOUNTER1_SELECT,
   R_03650C_PA_SC_PERFCOUNTER2_SELECT,
   R_036510_PA_SC_PERFCOUNTER3_SELECT,
   R_036514_PA_SC_PERFCOUNTER4_SELECT,
   R_036518_PA_SC_PERFCOUNTER5_SELECT,
   R_03651C_PA_SC_PERFCOUNTER6_SELECT,
   R_036520_PA_SC_PERFCOUNTER7_SELECT,
};
static unsigned cik_PA_SC_select1[] = {
   R_036504_PA_SC_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base cik_PA_SC = {
   .gpu_block = PA_SC,
   .name = "PA_SC",
   .num_counters = 8,
   .flags = AC_PC_BLOCK_SE,

   .select0 = cik_PA_SC_select0,
   .select1 = cik_PA_SC_select1,
   .counter0_lo = R_034500_PA_SC_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_SE_BLOCK_SC,
};

/* cik_PA_SU */
static unsigned cik_PA_SU_select0[] = {
   R_036400_PA_SU_PERFCOUNTER0_SELECT,
   R_036408_PA_SU_PERFCOUNTER1_SELECT,
   R_036410_PA_SU_PERFCOUNTER2_SELECT,
   R_036414_PA_SU_PERFCOUNTER3_SELECT,
};
static unsigned cik_PA_SU_select1[] = {
   R_036404_PA_SU_PERFCOUNTER0_SELECT1,
   R_03640C_PA_SU_PERFCOUNTER1_SELECT1,
};
/* According to docs, PA_SU counters are only 48 bits wide. */
static struct ac_pc_block_base cik_PA_SU = {
   .gpu_block = PA_SU,
   .name = "PA_SU",
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE,

   .select0 = cik_PA_SU_select0,
   .select1 = cik_PA_SU_select1,
   .counter0_lo = R_034400_PA_SU_PERFCOUNTER0_LO,

   .num_spm_modules = 2,
   .num_spm_wires = 3,
};

/* cik_SPI */
static unsigned cik_SPI_select0[] = {
   R_036600_SPI_PERFCOUNTER0_SELECT,
   R_036604_SPI_PERFCOUNTER1_SELECT,
   R_036608_SPI_PERFCOUNTER2_SELECT,
   R_03660C_SPI_PERFCOUNTER3_SELECT,
   R_036620_SPI_PERFCOUNTER4_SELECT,
   R_036624_SPI_PERFCOUNTER5_SELECT,
};
static unsigned cik_SPI_select1[] = {
   R_036610_SPI_PERFCOUNTER0_SELECT1,
   R_036614_SPI_PERFCOUNTER1_SELECT1,
   R_036618_SPI_PERFCOUNTER2_SELECT1,
   R_03661C_SPI_PERFCOUNTER3_SELECT1
};
static struct ac_pc_block_base cik_SPI = {
   .gpu_block = SPI,
   .name = "SPI",
   .num_counters = 6,
   .flags = AC_PC_BLOCK_SE,

   .select0 = cik_SPI_select0,
   .select1 = cik_SPI_select1,
   .counter0_lo = R_034604_SPI_PERFCOUNTER0_LO,

   .num_spm_modules = 4,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_SE_BLOCK_SPI,
};

/* cik_SQ */
static unsigned cik_SQ_select0[] = {
   R_036700_SQ_PERFCOUNTER0_SELECT,
   R_036704_SQ_PERFCOUNTER1_SELECT,
   R_036708_SQ_PERFCOUNTER2_SELECT,
   R_03670C_SQ_PERFCOUNTER3_SELECT,
   R_036710_SQ_PERFCOUNTER4_SELECT,
   R_036714_SQ_PERFCOUNTER5_SELECT,
   R_036718_SQ_PERFCOUNTER6_SELECT,
   R_03671C_SQ_PERFCOUNTER7_SELECT,
   R_036720_SQ_PERFCOUNTER8_SELECT,
   R_036724_SQ_PERFCOUNTER9_SELECT,
   R_036728_SQ_PERFCOUNTER10_SELECT,
   R_03672C_SQ_PERFCOUNTER11_SELECT,
   R_036730_SQ_PERFCOUNTER12_SELECT,
   R_036734_SQ_PERFCOUNTER13_SELECT,
   R_036738_SQ_PERFCOUNTER14_SELECT,
   R_03673C_SQ_PERFCOUNTER15_SELECT,
};
static struct ac_pc_block_base cik_SQ = {
   .gpu_block = SQ,
   .name = "SQ",
   .num_counters = 16,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_SHADER,

   .select0 = cik_SQ_select0,
   .select_or = S_036700_SQC_BANK_MASK(15) | S_036700_SQC_CLIENT_MASK(15) | S_036700_SIMD_MASK(15),
   .counter0_lo = R_034700_SQ_PERFCOUNTER0_LO,

   .num_spm_wires = 8,
};

/* cik_SX */
static unsigned cik_SX_select0[] = {
   R_036900_SX_PERFCOUNTER0_SELECT,
   R_036904_SX_PERFCOUNTER1_SELECT,
   R_036908_SX_PERFCOUNTER2_SELECT,
   R_03690C_SX_PERFCOUNTER3_SELECT,
};
static unsigned cik_SX_select1[] = {
   R_036910_SX_PERFCOUNTER0_SELECT1,
   R_036914_SX_PERFCOUNTER1_SELECT1,
};
static struct ac_pc_block_base cik_SX = {
   .gpu_block = SX,
   .name = "SX",
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE,

   .select0 = cik_SX_select0,
   .select1 = cik_SX_select1,
   .counter0_lo = R_034900_SX_PERFCOUNTER0_LO,

   .num_spm_modules = 2,
   .num_spm_wires = 4,
   .spm_block_select = AC_SPM_SE_BLOCK_SX,
};

/* cik_TA */
static unsigned cik_TA_select0[] = {
   R_036B00_TA_PERFCOUNTER0_SELECT,
   R_036B08_TA_PERFCOUNTER1_SELECT,
};
static unsigned cik_TA_select1[] = {
   R_036B04_TA_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base cik_TA = {
   .gpu_block = TA,
   .name = "TA",
   .num_counters = 2,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_INSTANCE_GROUPS | AC_PC_BLOCK_SHADER_WINDOWED,

   .select0 = cik_TA_select0,
   .select1 = cik_TA_select1,
   .counter0_lo = R_034B00_TA_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_SE_BLOCK_TA,
};

/* cik_TD */
static unsigned cik_TD_select0[] = {
   R_036C00_TD_PERFCOUNTER0_SELECT,
   R_036C08_TD_PERFCOUNTER1_SELECT,
};
static unsigned cik_TD_select1[] = {
   R_036C04_TD_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base cik_TD = {
   .gpu_block = TD,
   .name = "TD",
   .num_counters = 2,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_INSTANCE_GROUPS | AC_PC_BLOCK_SHADER_WINDOWED,

   .select0 = cik_TD_select0,
   .select1 = cik_TD_select1,
   .counter0_lo = R_034C00_TD_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_SE_BLOCK_TD,
};

/* cik_TCA */
static unsigned cik_TCA_select0[] = {
   R_036E40_TCA_PERFCOUNTER0_SELECT,
   R_036E48_TCA_PERFCOUNTER1_SELECT,
   R_036E50_TCA_PERFCOUNTER2_SELECT,
   R_036E54_TCA_PERFCOUNTER3_SELECT,
};
static unsigned cik_TCA_select1[] = {
   R_036E44_TCA_PERFCOUNTER0_SELECT1,
   R_036E4C_TCA_PERFCOUNTER1_SELECT1,
};
static struct ac_pc_block_base cik_TCA = {
   .gpu_block = TCA,
   .name = "TCA",
   .num_counters = 4,
   .flags = AC_PC_BLOCK_INSTANCE_GROUPS,

   .select0 = cik_TCA_select0,
   .select1 = cik_TCA_select1,
   .counter0_lo = R_034E40_TCA_PERFCOUNTER0_LO,

   .num_spm_modules = 2,
   .num_spm_wires = 4,
};

/* cik_TCC */
static unsigned cik_TCC_select0[] = {
   R_036E00_TCC_PERFCOUNTER0_SELECT,
   R_036E08_TCC_PERFCOUNTER1_SELECT,
   R_036E10_TCC_PERFCOUNTER2_SELECT,
   R_036E14_TCC_PERFCOUNTER3_SELECT,
};
static unsigned cik_TCC_select1[] = {
   R_036E04_TCC_PERFCOUNTER0_SELECT1,
   R_036E0C_TCC_PERFCOUNTER1_SELECT1,
};
static struct ac_pc_block_base cik_TCC = {
   .gpu_block = TCC,
   .name = "TCC",
   .num_counters = 4,
   .flags = AC_PC_BLOCK_INSTANCE_GROUPS,

   .select0 = cik_TCC_select0,
   .select1 = cik_TCC_select1,
   .counter0_lo = R_034E00_TCC_PERFCOUNTER0_LO,

   .num_spm_modules = 2,
   .num_spm_wires = 4,
};

/* cik_TCP */
static unsigned cik_TCP_select0[] = {
   R_036D00_TCP_PERFCOUNTER0_SELECT,
   R_036D08_TCP_PERFCOUNTER1_SELECT,
   R_036D10_TCP_PERFCOUNTER2_SELECT,
   R_036D14_TCP_PERFCOUNTER3_SELECT,
};
static unsigned cik_TCP_select1[] = {
   R_036D04_TCP_PERFCOUNTER0_SELECT1,
   R_036D0C_TCP_PERFCOUNTER1_SELECT1,
};
static struct ac_pc_block_base cik_TCP = {
   .gpu_block = TCP,
   .name = "TCP",
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_INSTANCE_GROUPS | AC_PC_BLOCK_SHADER_WINDOWED,

   .select0 = cik_TCP_select0,
   .select1 = cik_TCP_select1,
   .counter0_lo = R_034D00_TCP_PERFCOUNTER0_LO,

   .num_spm_modules = 2,
   .num_spm_wires = 3,
};

/* cik_VGT */
static unsigned cik_VGT_select0[] = {
   R_036230_VGT_PERFCOUNTER0_SELECT,
   R_036234_VGT_PERFCOUNTER1_SELECT,
   R_036238_VGT_PERFCOUNTER2_SELECT,
   R_03623C_VGT_PERFCOUNTER3_SELECT,
};
static unsigned cik_VGT_select1[] = {
   R_036240_VGT_PERFCOUNTER0_SELECT1,
   R_036244_VGT_PERFCOUNTER1_SELECT1,
};
static struct ac_pc_block_base cik_VGT = {
   .gpu_block = VGT,
   .name = "VGT",
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE,

   .select0 = cik_VGT_select0,
   .select1 = cik_VGT_select1,
   .counter0_lo = R_034240_VGT_PERFCOUNTER0_LO,

   .num_spm_modules = 2,
   .num_spm_wires = 3,
};

/* cik_WD */
static unsigned cik_WD_select0[] = {
   R_036200_WD_PERFCOUNTER0_SELECT,
   R_036204_WD_PERFCOUNTER1_SELECT,
   R_036208_WD_PERFCOUNTER2_SELECT,
   R_03620C_WD_PERFCOUNTER3_SELECT,
};
static struct ac_pc_block_base cik_WD = {
   .gpu_block = WD,
   .name = "WD",
   .num_counters = 4,

   .select0 = cik_WD_select0,
   .counter0_lo = R_034200_WD_PERFCOUNTER0_LO,
};

/* cik_MC */
static struct ac_pc_block_base cik_MC = {
   .gpu_block = MC,
   .name = "MC",
   .num_counters = 4,
};

/* cik_SRBM */
static struct ac_pc_block_base cik_SRBM = {
   .gpu_block = SRBM,
   .name = "SRBM",
   .num_counters = 2,
};

/* Both the number of instances and selectors varies between chips of the same
 * class. We only differentiate by class here and simply expose the maximum
 * number over all chips in a class.
 *
 * Unfortunately, GPUPerfStudio uses the order of performance counter groups
 * blindly once it believes it has identified the hardware, so the order of
 * blocks here matters.
 */
static struct ac_pc_block_gfxdescr groups_CIK[] = {
   {&cik_CB, 226},     {&cik_CPF, 17},    {&cik_DB, 257},  {&cik_GRBM, 34},   {&cik_GRBMSE, 15},
   {&cik_PA_SU, 153},  {&cik_PA_SC, 395}, {&cik_SPI, 186}, {&cik_SQ, 252},    {&cik_SX, 32},
   {&cik_TA, 111},     {&cik_TCA, 39, 2}, {&cik_TCC, 160}, {&cik_TD, 55},     {&cik_TCP, 154},
   {&cik_GDS, 121},    {&cik_VGT, 140},   {&cik_IA, 22},   {&cik_MC, 22},     {&cik_SRBM, 19},
   {&cik_WD, 22},      {&cik_CPG, 46},    {&cik_CPC, 22},

};

static struct ac_pc_block_gfxdescr groups_VI[] = {
   {&cik_CB, 405},     {&cik_CPF, 19},    {&cik_DB, 257},  {&cik_GRBM, 34},   {&cik_GRBMSE, 15},
   {&cik_PA_SU, 154},  {&cik_PA_SC, 397}, {&cik_SPI, 197}, {&cik_SQ, 273},    {&cik_SX, 34},
   {&cik_TA, 119},     {&cik_TCA, 35, 2}, {&cik_TCC, 192}, {&cik_TD, 55},     {&cik_TCP, 180},
   {&cik_GDS, 121},    {&cik_VGT, 147},   {&cik_IA, 24},   {&cik_MC, 22},     {&cik_SRBM, 27},
   {&cik_WD, 37},      {&cik_CPG, 48},    {&cik_CPC, 24},

};

static struct ac_pc_block_gfxdescr groups_gfx9[] = {
   {&cik_CB, 438},     {&cik_CPF, 32},    {&cik_DB, 328},  {&cik_GRBM, 38},   {&cik_GRBMSE, 16},
   {&cik_PA_SU, 292},  {&cik_PA_SC, 491}, {&cik_SPI, 196}, {&cik_SQ, 374},    {&cik_SX, 208},
   {&cik_TA, 119},     {&cik_TCA, 35, 2}, {&cik_TCC, 256}, {&cik_TD, 57},     {&cik_TCP, 85},
   {&cik_GDS, 121},    {&cik_VGT, 148},   {&cik_IA, 32},   {&cik_WD, 58},     {&cik_CPG, 59},
   {&cik_CPC, 35},
};

struct ac_pc_block *ac_lookup_counter(const struct ac_perfcounters *pc,
                                      unsigned index, unsigned *base_gid,
                                      unsigned *sub_index)
{
   struct ac_pc_block *block = pc->blocks;
   unsigned bid;

   *base_gid = 0;
   for (bid = 0; bid < pc->num_blocks; ++bid, ++block) {
      unsigned total = block->num_groups * block->b->selectors;

      if (index < total) {
         *sub_index = index;
         return block;
      }

      index -= total;
      *base_gid += block->num_groups;
   }

   return NULL;
}

struct ac_pc_block *ac_lookup_group(const struct ac_perfcounters *pc,
                                    unsigned *index)
{
   unsigned bid;
   struct ac_pc_block *block = pc->blocks;

   for (bid = 0; bid < pc->num_blocks; ++bid, ++block) {
      if (*index < block->num_groups)
         return block;
      *index -= block->num_groups;
   }

   return NULL;
}

bool ac_init_block_names(const struct radeon_info *info,
                         const struct ac_perfcounters *pc,
                         struct ac_pc_block *block)
{
   bool per_instance_groups = ac_pc_block_has_per_instance_groups(pc, block);
   bool per_se_groups = ac_pc_block_has_per_se_groups(pc, block);
   unsigned i, j, k;
   unsigned groups_shader = 1, groups_se = 1, groups_instance = 1;
   unsigned namelen;
   char *groupname;
   char *p;

   if (per_instance_groups)
      groups_instance = block->num_scoped_instances;
   if (per_se_groups)
      groups_se = info->max_se;
   if (block->b->b->flags & AC_PC_BLOCK_SHADER)
      groups_shader = ARRAY_SIZE(ac_pc_shader_type_bits);

   namelen = strlen(block->b->b->name);
   block->group_name_stride = namelen + 1;
   if (block->b->b->flags & AC_PC_BLOCK_SHADER)
      block->group_name_stride += 3;
   if (per_se_groups) {
      assert(groups_se <= 10);
      block->group_name_stride += 1;

      if (per_instance_groups)
         block->group_name_stride += 1;
   }
   if (per_instance_groups) {
      assert(groups_instance <= 100);
      block->group_name_stride += 2;
   }

   block->group_names = MALLOC((size_t)block->num_groups * block->group_name_stride);
   if (!block->group_names)
      return false;

   groupname = block->group_names;
   for (i = 0; i < groups_shader; ++i) {
      const char *shader_suffix = ac_pc_shader_type_suffixes[i];
      unsigned shaderlen = strlen(shader_suffix);
      for (j = 0; j < groups_se; ++j) {
         for (k = 0; k < groups_instance; ++k) {
            strcpy(groupname, block->b->b->name);
            p = groupname + namelen;

            if (block->b->b->flags & AC_PC_BLOCK_SHADER) {
               strcpy(p, shader_suffix);
               p += shaderlen;
            }

            if (per_se_groups) {
               p += sprintf(p, "%d", j);
               if (per_instance_groups)
                  *p++ = '_';
            }

            if (per_instance_groups)
               p += sprintf(p, "%d", k);

            groupname += block->group_name_stride;
         }
      }
   }

   block->selector_name_stride = block->group_name_stride + 5;
   block->selector_names =
      MALLOC((size_t)block->num_groups * block->b->selectors * block->selector_name_stride);
   if (!block->selector_names)
      return false;

   groupname = block->group_names;
   p = block->selector_names;
   for (i = 0; i < block->num_groups; ++i) {
      for (j = 0; j < block->b->selectors; ++j) {
         sprintf(p, "%s_%04d", groupname, j);
         p += block->selector_name_stride;
      }
      groupname += block->group_name_stride;
   }

   return true;
}

bool ac_init_perfcounters(const struct radeon_info *info,
                          bool separate_se,
                          bool separate_instance,
                          struct ac_perfcounters *pc)
{
   const struct ac_pc_block_gfxdescr *blocks;
   unsigned num_blocks;

   switch (info->gfx_level) {
   case GFX7:
      blocks = groups_CIK;
      num_blocks = ARRAY_SIZE(groups_CIK);
      break;
   case GFX8:
      blocks = groups_VI;
      num_blocks = ARRAY_SIZE(groups_VI);
      break;
   case GFX9:
      blocks = groups_gfx9;
      num_blocks = ARRAY_SIZE(groups_gfx9);
      break;
   case GFX10:
      blocks = ac_gfx10_get_perfcounters(&num_blocks);
      break;
   case GFX10_3:
      blocks = ac_gfx103_get_perfcounters(&num_blocks);
      break;
   case GFX11:
   case GFX11_5:
   case GFX11_7:
      blocks = ac_gfx11_get_perfcounters(&num_blocks);
      break;
   case GFX12:
      blocks = ac_gfx12_get_perfcounters(&num_blocks);
      break;
   case GFX6:
   default:
      return false; /* not implemented */
   }

   pc->separate_se = separate_se;
   pc->separate_instance = separate_instance;

   pc->blocks = CALLOC(num_blocks, sizeof(struct ac_pc_block));
   if (!pc->blocks)
      return false;
   pc->num_blocks = num_blocks;

   for (unsigned i = 0; i < num_blocks; i++) {
      struct ac_pc_block *block = &pc->blocks[i];

      block->b = &blocks[i];
      block->num_scoped_instances = MAX2(1, block->b->instances);

      if (info->gfx_level >= GFX10) {
         /* TODO: Generalize this to older generations. */
         const uint32_t num_rb_per_se = info->num_rb / info->num_se;
         const uint32_t rb_per_sa = num_rb_per_se / info->max_sa_per_se;

         /* Determine the number of 16-bit/32-bit SPM counters. */
         if (!block->b->b->num_16bit_spm_counters &&
             !block->b->b->num_32bit_spm_counters) {
            block->b->b->num_16bit_spm_counters = block->b->b->num_spm_modules * 4;
            block->b->b->num_32bit_spm_counters = block->b->b->num_spm_modules * 2;
         }

         switch (block->b->b->gpu_block) {
         case CB:
         case DB:
            block->num_scoped_instances = rb_per_sa;
            break;
         case GL2C:
         case GCEA:
         case GCEA_SE:
            block->num_scoped_instances = info->num_tcc_blocks;
            break;
         case GL2A:
            if (info->family == CHIP_NAVI21 ||
                info->family == CHIP_NAVI31) {
               block->num_scoped_instances = 4;
            } else if (info->family == CHIP_NAVI14 ||
                       info->family == CHIP_NAVI32 ||
                       info->family == CHIP_NAVI33) {
               block->num_scoped_instances = 2;
            }
            break;
         case TA:
         case TD:
         case TCP:
            block->num_scoped_instances = MAX2(1, info->max_good_cu_per_sa);
            break;
         case DMA:
            block->num_scoped_instances = MIN2(2, info->ip[AMD_IP_SDMA].num_queues);
            break;
         case RMI:
            block->num_scoped_instances = rb_per_sa * 2;
            break;
         default:
            break;
         }

         switch (block->b->b->distribution) {
         case AC_PC_PER_SHADER_ARRAY:
            block->num_instances = block->num_scoped_instances * info->num_se * info->max_sa_per_se;
            break;
         case AC_PC_PER_SHADER_ENGINE:
            block->num_instances = block->num_scoped_instances * info->num_se;
            break;
         case AC_PC_GLOBAL_BLOCK:
            block->num_instances = block->num_scoped_instances;
            break;
         default:
            UNREACHABLE("Invalid perf block distribution mode.");
         }
      } else {
         if (!strcmp(block->b->b->name, "CB") ||
             !strcmp(block->b->b->name, "DB") ||
             !strcmp(block->b->b->name, "RMI"))
            block->num_scoped_instances = info->max_se;
         else if (!strcmp(block->b->b->name, "TCC"))
            block->num_scoped_instances = info->max_tcc_blocks;
         else if (!strcmp(block->b->b->name, "IA"))
            block->num_scoped_instances = MAX2(1, info->max_se / 2);
         else if (!strcmp(block->b->b->name, "TA") ||
                  !strcmp(block->b->b->name, "TCP") ||
                  !strcmp(block->b->b->name, "TD")) {
            block->num_scoped_instances = MAX2(1, info->max_good_cu_per_sa);
         }

         if (info->gfx_level >= GFX10) {
            if (!strcmp(block->b->b->name, "TCP")) {
               block->num_instances = MAX2(1, info->num_cu_per_sh) * info->num_se * info->max_sa_per_se;
            } else if (!strcmp(block->b->b->name, "SQ")) {
               block->num_instances = block->num_scoped_instances * info->num_se;
            } else if (!strcmp(block->b->b->name, "GL1C") ||
                       !strcmp(block->b->b->name, "SQ_WGP")) {
               block->num_instances = block->num_scoped_instances * info->num_se * info->max_sa_per_se;
            } else if (!strcmp(block->b->b->name, "GL2C") ||
                       !strcmp(block->b->b->name, "GCEA")) {
               block->num_scoped_instances = block->num_instances = info->num_tcc_blocks;
            } else if (!strcmp(block->b->b->name, "CPF")) {
               block->num_scoped_instances = block->num_instances = 1;
            } else if (!strcmp(block->b->b->name, "TA") ||
                       !strcmp(block->b->b->name, "TD")) {
               block->num_instances = block->num_scoped_instances;
            }
         }
      }

      if (ac_pc_block_has_per_instance_groups(pc, block)) {
         block->num_groups = block->num_scoped_instances;
      } else {
         block->num_groups = 1;
      }

      if (ac_pc_block_has_per_se_groups(pc, block))
         block->num_groups *= info->max_se;
      if (block->b->b->flags & AC_PC_BLOCK_SHADER)
         block->num_groups *= ARRAY_SIZE(ac_pc_shader_type_bits);

      pc->num_groups += block->num_groups;
   }

   return true;
}

void ac_destroy_perfcounters(struct ac_perfcounters *pc)
{
   if (!pc)
      return;

   for (unsigned i = 0; i < pc->num_blocks; ++i) {
      FREE(pc->blocks[i].group_names);
      FREE(pc->blocks[i].selector_names);
   }
   FREE(pc->blocks);
}

struct ac_pc_block *ac_pc_get_block(const struct ac_perfcounters *pc,
                                    enum ac_pc_gpu_block gpu_block)
{
   for (unsigned i = 0; i < pc->num_blocks; i++) {
      struct ac_pc_block *block = &pc->blocks[i];
      if (block->b->b->gpu_block == gpu_block) {
         return block;
      }
   }
   return NULL;
}
