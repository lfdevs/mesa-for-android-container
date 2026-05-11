/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_perfcounter.h"
#include "ac_spm.h"

#include "util/macros.h"

/* gfx10_CB */
static unsigned gfx10_CB_select0[] = {
   R_037004_CB_PERFCOUNTER0_SELECT,
   R_03700C_CB_PERFCOUNTER1_SELECT,
   R_037010_CB_PERFCOUNTER2_SELECT,
   R_037014_CB_PERFCOUNTER3_SELECT,
};
static unsigned gfx10_CB_select1[] = {
   R_037008_CB_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base gfx10_CB = {
   .gpu_block = CB,
   .name = "CB",
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_INSTANCE_GROUPS,

   .select0 = gfx10_CB_select0,
   .select1 = gfx10_CB_select1,
   .counter0_lo = R_035018_CB_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_SE_BLOCK_CB,
};

/* gfx10_CPC */
static unsigned gfx10_CPC_select0[] = {
   R_036024_CPC_PERFCOUNTER0_SELECT,
   R_03600C_CPC_PERFCOUNTER1_SELECT,
};
static unsigned gfx10_CPC_select1[] = {
   R_036010_CPC_PERFCOUNTER0_SELECT1,
};
static unsigned gfx10_CPC_counters[] = {
   R_034018_CPC_PERFCOUNTER0_LO,
   R_034010_CPC_PERFCOUNTER1_LO,
};
static struct ac_pc_block_base gfx10_CPC = {
   .gpu_block = CPC,
   .name = "CPC",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 2,

   .select0 = gfx10_CPC_select0,
   .select1 = gfx10_CPC_select1,
   .counters = gfx10_CPC_counters,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_CPC,
};

/* gfx10_CPF */
static unsigned gfx10_CPF_select0[] = {
   R_03601C_CPF_PERFCOUNTER0_SELECT,
   R_036014_CPF_PERFCOUNTER1_SELECT,
};
static unsigned gfx10_CPF_select1[] = {
   R_036018_CPF_PERFCOUNTER0_SELECT1,
};
static unsigned gfx10_CPF_counters[] = {
   R_034028_CPF_PERFCOUNTER0_LO,
   R_034020_CPF_PERFCOUNTER1_LO,
};
static struct ac_pc_block_base gfx10_CPF = {
   .gpu_block = CPF,
   .name = "CPF",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 2,

   .select0 = gfx10_CPF_select0,
   .select1 = gfx10_CPF_select1,
   .counters = gfx10_CPF_counters,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_CPF,
};

/* gfx10_CPG */
static unsigned gfx10_CPG_select0[] = {
   R_036008_CPG_PERFCOUNTER0_SELECT,
   R_036000_CPG_PERFCOUNTER1_SELECT,
};
static unsigned gfx10_CPG_select1[] = {
   R_036004_CPG_PERFCOUNTER0_SELECT1
};
static unsigned gfx10_CPG_counters[] = {
   R_034008_CPG_PERFCOUNTER0_LO,
   R_034000_CPG_PERFCOUNTER1_LO,
};
static struct ac_pc_block_base gfx10_CPG = {
   .gpu_block = CPG,
   .name = "CPG",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 2,

   .select0 = gfx10_CPG_select0,
   .select1 = gfx10_CPG_select1,
   .counters = gfx10_CPG_counters,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_CPG,
};

/* gfx10_GDS */
static unsigned gfx10_GDS_select0[] = {
   R_036A00_GDS_PERFCOUNTER0_SELECT,
   R_036A04_GDS_PERFCOUNTER1_SELECT,
   R_036A08_GDS_PERFCOUNTER2_SELECT,
   R_036A0C_GDS_PERFCOUNTER3_SELECT,
};
static unsigned gfx10_GDS_select1[] = {
   R_036A10_GDS_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base gfx10_GDS = {
   .gpu_block = GDS,
   .name = "GDS",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 4,

   .select0 = gfx10_GDS_select0,
   .select1 = gfx10_GDS_select1,
   .counter0_lo = R_034A00_GDS_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_GDS,
};

/* gfx10_GRBM */
static unsigned gfx10_GRBM_select0[] = {
   R_036100_GRBM_PERFCOUNTER0_SELECT,
   R_036104_GRBM_PERFCOUNTER1_SELECT,
};
static unsigned gfx10_GRBM_counters[] = {
   R_034100_GRBM_PERFCOUNTER0_LO,
   R_03410C_GRBM_PERFCOUNTER1_LO,
};
static struct ac_pc_block_base gfx10_GRBM = {
   .gpu_block = GRBM,
   .name = "GRBM",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 2,

   .select0 = gfx10_GRBM_select0,
   .counters = gfx10_GRBM_counters,
};

/* gfx10_GRBMSE */
static unsigned gfx10_GRBMSE_select0[] = {
   R_036108_GRBM_SE0_PERFCOUNTER_SELECT,
   R_03610C_GRBM_SE1_PERFCOUNTER_SELECT,
   R_036110_GRBM_SE2_PERFCOUNTER_SELECT,
   R_036114_GRBM_SE3_PERFCOUNTER_SELECT,
};
static struct ac_pc_block_base gfx10_GRBMSE = {
   .gpu_block = GRBMSE,
   .name = "GRBMSE",
   .distribution = AC_PC_PER_SHADER_ENGINE,
   .num_counters = 4,

   .select0 = gfx10_GRBMSE_select0,
   .counter0_lo = R_034114_GRBM_SE0_PERFCOUNTER_LO,
};

/* gfx10_PA_SC */
static unsigned gfx10_PA_SC_select0[] = {
   R_036500_PA_SC_PERFCOUNTER0_SELECT,
   R_036508_PA_SC_PERFCOUNTER1_SELECT,
   R_03650C_PA_SC_PERFCOUNTER2_SELECT,
   R_036510_PA_SC_PERFCOUNTER3_SELECT,
   R_036514_PA_SC_PERFCOUNTER4_SELECT,
   R_036518_PA_SC_PERFCOUNTER5_SELECT,
   R_03651C_PA_SC_PERFCOUNTER6_SELECT,
   R_036520_PA_SC_PERFCOUNTER7_SELECT,
};
static unsigned gfx10_PA_SC_select1[] = {
   R_036504_PA_SC_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base gfx10_PA_SC = {
   .gpu_block = PA_SC,
   .name = "PA_SC",
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .num_counters = 8,
   .flags = AC_PC_BLOCK_SE,

   .select0 = gfx10_PA_SC_select0,
   .select1 = gfx10_PA_SC_select1,
   .counter0_lo = R_034500_PA_SC_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_SE_BLOCK_SC,
};

/* gfx10_SPI */
static unsigned gfx10_SPI_select0[] = {
   R_036600_SPI_PERFCOUNTER0_SELECT,
   R_036604_SPI_PERFCOUNTER1_SELECT,
   R_036608_SPI_PERFCOUNTER2_SELECT,
   R_03660C_SPI_PERFCOUNTER3_SELECT,
   R_036620_SPI_PERFCOUNTER4_SELECT,
   R_036624_SPI_PERFCOUNTER5_SELECT,
};
static unsigned gfx10_SPI_select1[] = {
   R_036610_SPI_PERFCOUNTER0_SELECT1,
   R_036614_SPI_PERFCOUNTER1_SELECT1,
   R_036618_SPI_PERFCOUNTER2_SELECT1,
   R_03661C_SPI_PERFCOUNTER3_SELECT1
};
static struct ac_pc_block_base gfx10_SPI = {
   .gpu_block = SPI,
   .name = "SPI",
   .distribution = AC_PC_PER_SHADER_ENGINE,
   .num_counters = 6,
   .flags = AC_PC_BLOCK_SE,

   .select0 = gfx10_SPI_select0,
   .select1 = gfx10_SPI_select1,
   .counter0_lo = R_034604_SPI_PERFCOUNTER0_LO,

   .num_spm_modules = 4,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_SE_BLOCK_SPI,
};

/* gfx10_SX */
static unsigned gfx10_SX_select0[] = {
   R_036900_SX_PERFCOUNTER0_SELECT,
   R_036904_SX_PERFCOUNTER1_SELECT,
   R_036908_SX_PERFCOUNTER2_SELECT,
   R_03690C_SX_PERFCOUNTER3_SELECT,
};
static unsigned gfx10_SX_select1[] = {
   R_036910_SX_PERFCOUNTER0_SELECT1,
   R_036914_SX_PERFCOUNTER1_SELECT1,
};
static struct ac_pc_block_base gfx10_SX = {
   .gpu_block = SX,
   .name = "SX",
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE,

   .select0 = gfx10_SX_select0,
   .select1 = gfx10_SX_select1,
   .counter0_lo = R_034900_SX_PERFCOUNTER0_LO,

   .num_spm_modules = 2,
   .num_spm_wires = 4,
   .spm_block_select = AC_SPM_SE_BLOCK_SX,
};

/* gfx10_TA */
static unsigned gfx10_TA_select0[] = {
   R_036B00_TA_PERFCOUNTER0_SELECT,
   R_036B08_TA_PERFCOUNTER1_SELECT,
};
static unsigned gfx10_TA_select1[] = {
   R_036B04_TA_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base gfx10_TA = {
   .gpu_block = TA,
   .name = "TA",
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .num_counters = 2,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_INSTANCE_GROUPS | AC_PC_BLOCK_SHADER_WINDOWED,

   .select0 = gfx10_TA_select0,
   .select1 = gfx10_TA_select1,
   .counter0_lo = R_034B00_TA_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_SE_BLOCK_TA,
};

/* gfx10_TD */
static unsigned gfx10_TD_select0[] = {
   R_036C00_TD_PERFCOUNTER0_SELECT,
   R_036C08_TD_PERFCOUNTER1_SELECT,
};
static unsigned gfx10_TD_select1[] = {
   R_036C04_TD_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base gfx10_TD = {
   .gpu_block = TD,
   .name = "TD",
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .num_counters = 2,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_INSTANCE_GROUPS | AC_PC_BLOCK_SHADER_WINDOWED,

   .select0 = gfx10_TD_select0,
   .select1 = gfx10_TD_select1,
   .counter0_lo = R_034C00_TD_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_SE_BLOCK_TD,
};

/* gfx10_CHA */
static unsigned gfx10_CHA_select0[] = {
   R_037780_CHA_PERFCOUNTER0_SELECT,
   R_037788_CHA_PERFCOUNTER1_SELECT,
   R_03778C_CHA_PERFCOUNTER2_SELECT,
   R_037790_CHA_PERFCOUNTER3_SELECT,
};
static unsigned gfx10_CHA_select1[] = {
   R_037784_CHA_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base gfx10_CHA = {
   .gpu_block = CHA,
   .name = "CHA",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 4,

   .select0 = gfx10_CHA_select0,
   .select1 = gfx10_CHA_select1,
   .counter0_lo = R_035800_CHA_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_CHA,
};

/* gfx10_CHCG */
static unsigned gfx10_CHCG_select0[] = {
   R_036F18_CHCG_PERFCOUNTER0_SELECT,
   R_036F20_CHCG_PERFCOUNTER1_SELECT,
   R_036F24_CHCG_PERFCOUNTER2_SELECT,
   R_036F28_CHCG_PERFCOUNTER3_SELECT,
};
static unsigned gfx10_CHCG_select1[] = {
   R_036F1C_CHCG_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base gfx10_CHCG = {
   .gpu_block = CHCG,
   .name = "CHCG",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 4,

   .select0 = gfx10_CHCG_select0,
   .select1 = gfx10_CHCG_select1,
   .counter0_lo = R_034F20_CHCG_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_CHCG,
};

/* gfx10_CHC */
static unsigned gfx10_CHC_select0[] = {
   R_036F00_CHC_PERFCOUNTER0_SELECT,
   R_036F08_CHC_PERFCOUNTER1_SELECT,
   R_036F0C_CHC_PERFCOUNTER2_SELECT,
   R_036F10_CHC_PERFCOUNTER3_SELECT,
};
static unsigned gfx10_CHC_select1[] = {
   R_036F04_CHC_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base gfx10_CHC = {
   .gpu_block = CHC,
   .name = "CHC",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 4,

   .select0 = gfx10_CHC_select0,
   .select1 = gfx10_CHC_select1,
   .counter0_lo = R_034F00_CHC_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_CHC,
};

/* gfx10_DB */
static unsigned gfx10_DB_select0[] = {
   R_037100_DB_PERFCOUNTER0_SELECT,
   R_037108_DB_PERFCOUNTER1_SELECT,
   R_037110_DB_PERFCOUNTER2_SELECT,
   R_037118_DB_PERFCOUNTER3_SELECT,
};
static unsigned gfx10_DB_select1[] = {
   R_037104_DB_PERFCOUNTER0_SELECT1,
   R_03710C_DB_PERFCOUNTER1_SELECT1,
};
static struct ac_pc_block_base gfx10_DB = {
   .gpu_block = DB,
   .name = "DB",
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_INSTANCE_GROUPS,

   .select0 = gfx10_DB_select0,
   .select1 = gfx10_DB_select1,
   .counter0_lo = R_035100_DB_PERFCOUNTER0_LO,

   .num_spm_modules = 2,
   .num_spm_wires = 4,
   .spm_block_select = AC_SPM_SE_BLOCK_DB,
};

/* gfx10_GCR */
static unsigned gfx10_GCR_select0[] = {
   R_037580_GCR_PERFCOUNTER0_SELECT,
   R_037588_GCR_PERFCOUNTER1_SELECT,
};
static unsigned gfx10_GCR_select1[] = {
   R_037584_GCR_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base gfx10_GCR = {
   .gpu_block = GCR,
   .name = "GCR",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 2,

   .select0 = gfx10_GCR_select0,
   .select1 = gfx10_GCR_select1,
   .counter0_lo = R_035480_GCR_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_GCR,
};

/* gfx10_GE */
static unsigned gfx10_GE_select0[] = {
   R_036200_GE_PERFCOUNTER0_SELECT,
   R_036208_GE_PERFCOUNTER1_SELECT,
   R_036210_GE_PERFCOUNTER2_SELECT,
   R_036218_GE_PERFCOUNTER3_SELECT,
   R_036220_GE_PERFCOUNTER4_SELECT,
   R_036228_GE_PERFCOUNTER5_SELECT,
   R_036230_GE_PERFCOUNTER6_SELECT,
   R_036238_GE_PERFCOUNTER7_SELECT,
   R_036240_GE_PERFCOUNTER8_SELECT,
   R_036248_GE_PERFCOUNTER9_SELECT,
   R_036250_GE_PERFCOUNTER10_SELECT,
   R_036258_GE_PERFCOUNTER11_SELECT,
};
static unsigned gfx10_GE_select1[] = {
   R_036204_GE_PERFCOUNTER0_SELECT1,
   R_03620C_GE_PERFCOUNTER1_SELECT1,
   R_036214_GE_PERFCOUNTER2_SELECT1,
   R_03621C_GE_PERFCOUNTER3_SELECT1,
};
static struct ac_pc_block_base gfx10_GE = {
   .gpu_block = GE,
   .name = "GE",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 12,

   .select0 = gfx10_GE_select0,
   .select1 = gfx10_GE_select1,
   .counter0_lo = R_034200_GE_PERFCOUNTER0_LO,

   .num_spm_modules = 4,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_GE,
};

/* gfx10_GL1A */
static unsigned gfx10_GL1A_select0[] = {
   R_037700_GL1A_PERFCOUNTER0_SELECT,
   R_037708_GL1A_PERFCOUNTER1_SELECT,
   R_03770C_GL1A_PERFCOUNTER2_SELECT,
   R_037710_GL1A_PERFCOUNTER3_SELECT,
};
static unsigned gfx10_GL1A_select1[] = {
   R_037704_GL1A_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base gfx10_GL1A = {
   .gpu_block = GL1A,
   .name = "GL1A",
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_SHADER_WINDOWED,

   .select0 = gfx10_GL1A_select0,
   .select1 = gfx10_GL1A_select1,
   .counter0_lo = R_035700_GL1A_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_SE_BLOCK_GL1A,
};

/* gfx10_GL1C */
static unsigned gfx10_GL1C_select0[] = {
   R_036E80_GL1C_PERFCOUNTER0_SELECT,
   R_036E88_GL1C_PERFCOUNTER1_SELECT,
   R_036E8C_GL1C_PERFCOUNTER2_SELECT,
   R_036E90_GL1C_PERFCOUNTER3_SELECT,
};
static unsigned gfx10_GL1C_select1[] = {
   R_036E84_GL1C_PERFCOUNTER0_SELECT1,
};
static struct ac_pc_block_base gfx10_GL1C = {
   .gpu_block = GL1C,
   .name = "GL1C",
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_SHADER_WINDOWED,

   .select0 = gfx10_GL1C_select0,
   .select1 = gfx10_GL1C_select1,
   .counter0_lo = R_034E80_GL1C_PERFCOUNTER0_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_SE_BLOCK_GL1C,
};

/* gfx10_GL2A */
static unsigned gfx10_GL2A_select0[] = {
   R_036E40_GL2A_PERFCOUNTER0_SELECT,
   R_036E48_GL2A_PERFCOUNTER1_SELECT,
   R_036E50_GL2A_PERFCOUNTER2_SELECT,
   R_036E54_GL2A_PERFCOUNTER3_SELECT,
};
static unsigned gfx10_GL2A_select1[] = {
   R_036E44_GL2A_PERFCOUNTER0_SELECT1,
   R_036E4C_GL2A_PERFCOUNTER1_SELECT1,
};
static struct ac_pc_block_base gfx10_GL2A = {
   .gpu_block = GL2A,
   .name = "GL2A",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 4,

   .select0 = gfx10_GL2A_select0,
   .select1 = gfx10_GL2A_select1,
   .counter0_lo = R_034E40_GL2A_PERFCOUNTER0_LO,

   .num_spm_modules = 2,
   .num_spm_wires = 4,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_GL2A,
};

/* gfx10_GL2C */
static unsigned gfx10_GL2C_select0[] = {
   R_036E00_GL2C_PERFCOUNTER0_SELECT,
   R_036E08_GL2C_PERFCOUNTER1_SELECT,
   R_036E10_GL2C_PERFCOUNTER2_SELECT,
   R_036E14_GL2C_PERFCOUNTER3_SELECT,
};
static unsigned gfx10_GL2C_select1[] = {
   R_036E04_GL2C_PERFCOUNTER0_SELECT1,
   R_036E0C_GL2C_PERFCOUNTER1_SELECT1,
};
static struct ac_pc_block_base gfx10_GL2C = {
   .gpu_block = GL2C,
   .name = "GL2C",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 4,

   .select0 = gfx10_GL2C_select0,
   .select1 = gfx10_GL2C_select1,
   .counter0_lo = R_034E00_GL2C_PERFCOUNTER0_LO,

   .num_spm_modules = 2,
   .num_spm_wires = 4,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_GL2C,
};

/* gfx10_PA_PH */
static unsigned gfx10_PA_PH_select0[] = {
   R_037600_PA_PH_PERFCOUNTER0_SELECT,
   R_037608_PA_PH_PERFCOUNTER1_SELECT,
   R_03760C_PA_PH_PERFCOUNTER2_SELECT,
   R_037610_PA_PH_PERFCOUNTER3_SELECT,
   R_037614_PA_PH_PERFCOUNTER4_SELECT,
   R_037618_PA_PH_PERFCOUNTER5_SELECT,
   R_03761C_PA_PH_PERFCOUNTER6_SELECT,
   R_037620_PA_PH_PERFCOUNTER7_SELECT,
};
static unsigned gfx10_PA_PH_select1[] = {
   R_037604_PA_PH_PERFCOUNTER0_SELECT1,
   R_037640_PA_PH_PERFCOUNTER1_SELECT1,
   R_037644_PA_PH_PERFCOUNTER2_SELECT1,
   R_037648_PA_PH_PERFCOUNTER3_SELECT1,
};
static struct ac_pc_block_base gfx10_PA_PH = {
   .gpu_block = PA_PH,
   .name = "PA_PH",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 8,
   .flags = AC_PC_BLOCK_SE,

   .select0 = gfx10_PA_PH_select0,
   .select1 = gfx10_PA_PH_select1,
   .counter0_lo = R_035600_PA_PH_PERFCOUNTER0_LO,

   .num_spm_modules = 4,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_PH,
};

/* gfx10_PA_SU */
static unsigned gfx10_PA_SU_select0[] = {
   R_036400_PA_SU_PERFCOUNTER0_SELECT,
   R_036408_PA_SU_PERFCOUNTER1_SELECT,
   R_036410_PA_SU_PERFCOUNTER2_SELECT,
   R_036418_PA_SU_PERFCOUNTER3_SELECT,
};
static unsigned gfx10_PA_SU_select1[] = {
   R_036404_PA_SU_PERFCOUNTER0_SELECT1,
   R_03640C_PA_SU_PERFCOUNTER1_SELECT1,
   R_036414_PA_SU_PERFCOUNTER2_SELECT1,
   R_03641C_PA_SU_PERFCOUNTER3_SELECT1,
};
static struct ac_pc_block_base gfx10_PA_SU = {
   .gpu_block = PA_SU,
   .name = "PA_SU",
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE,

   .select0 = gfx10_PA_SU_select0,
   .select1 = gfx10_PA_SU_select1,
   .counter0_lo = R_034400_PA_SU_PERFCOUNTER0_LO,

   .num_spm_modules = 4,
   .num_spm_wires = 8,
   .spm_block_select = AC_SPM_SE_BLOCK_PA,
};

/* gfx10_RLC */
static unsigned gfx10_RLC_select0[] = {
   R_037304_RLC_PERFCOUNTER0_SELECT,
   R_037308_RLC_PERFCOUNTER1_SELECT,
};
static struct ac_pc_block_base gfx10_RLC = {
   .gpu_block = RLC,
   .name = "RLC",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 2,

   .select0 = gfx10_RLC_select0,
   .counter0_lo = R_035200_RLC_PERFCOUNTER0_LO,
};

/* gfx10_RMI */
static unsigned gfx10_RMI_select0[] = {
   R_037400_RMI_PERFCOUNTER0_SELECT,
   R_037408_RMI_PERFCOUNTER1_SELECT,
   R_03740C_RMI_PERFCOUNTER2_SELECT,
   R_037414_RMI_PERFCOUNTER3_SELECT,
};
static unsigned gfx10_RMI_select1[] = {
   R_037404_RMI_PERFCOUNTER0_SELECT1,
   R_037410_RMI_PERFCOUNTER2_SELECT1,
};
static struct ac_pc_block_base gfx10_RMI = {
   .gpu_block = RMI,
   .name = "RMI",
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_INSTANCE_GROUPS,

   .select0 = gfx10_RMI_select0,
   .select1 = gfx10_RMI_select1,
   .counter0_lo = R_035300_RMI_PERFCOUNTER0_LO,

   .num_spm_modules = 2,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_SE_BLOCK_RMI,
};

/* gfx10_SQ */
static unsigned gfx10_SQ_select0[] = {
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
static struct ac_pc_block_base gfx10_SQ = {
   .gpu_block = SQ,
   .name = "SQ",
   .distribution = AC_PC_PER_SHADER_ENGINE,
   .num_counters = 16,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_SHADER,

   .select0 = gfx10_SQ_select0,
   .select_or = S_036700_SQC_BANK_MASK(15),
   .counter0_lo = R_034700_SQ_PERFCOUNTER0_LO,

   .num_16bit_spm_counters = 16,
   .num_32bit_spm_counters = 16,
   .num_spm_wires = 16,
   .spm_block_select = AC_SPM_SE_BLOCK_SQG,
};

/* gfx10_TCP */
static unsigned gfx10_TCP_select0[] = {
   R_036D00_TCP_PERFCOUNTER0_SELECT,
   R_036D08_TCP_PERFCOUNTER1_SELECT,
   R_036D10_TCP_PERFCOUNTER2_SELECT,
   R_036D14_TCP_PERFCOUNTER3_SELECT,
};
static unsigned gfx10_TCP_select1[] = {
   R_036D04_TCP_PERFCOUNTER0_SELECT1,
   R_036D0C_TCP_PERFCOUNTER1_SELECT1,
};
static struct ac_pc_block_base gfx10_TCP = {
   .gpu_block = TCP,
   .name = "TCP",
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .num_counters = 4,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_INSTANCE_GROUPS | AC_PC_BLOCK_SHADER_WINDOWED,

   .select0 = gfx10_TCP_select0,
   .select1 = gfx10_TCP_select1,
   .counter0_lo = R_034D00_TCP_PERFCOUNTER0_LO,

   .num_spm_modules = 2,
   .num_spm_wires = 4,
   .spm_block_select = AC_SPM_SE_BLOCK_TCP,
};

/* gfx10_UTCL1 */
static unsigned gfx10_UTCL1_select0[] = {
   R_03758C_UTCL1_PERFCOUNTER0_SELECT,
   R_037590_UTCL1_PERFCOUNTER1_SELECT,
};
static struct ac_pc_block_base gfx10_UTCL1 = {
   .gpu_block = UTCL1,
   .name = "UTCL1",
   .distribution = AC_PC_PER_SHADER_ARRAY,
   .num_counters = 2,
   .flags = AC_PC_BLOCK_SE | AC_PC_BLOCK_SHADER_WINDOWED,

   .select0 = gfx10_UTCL1_select0,
   .counter0_lo = R_035470_UTCL1_PERFCOUNTER0_LO,
};

/* gfx10_GCEA */
static unsigned gfx10_GCEA_select0[] = {
   R_036800_GCEA_PERFCOUNTER2_SELECT,
};

static unsigned gfx10_GCEA_select1[] = {
   R_036804_GCEA_PERFCOUNTER2_SELECT1,
};
static struct ac_pc_block_base gfx10_GCEA = {
   .gpu_block = GCEA,
   .name = "GCEA",
   .distribution = AC_PC_GLOBAL_BLOCK,
   .num_counters = 1,

   .select0 = gfx10_GCEA_select0,
   .select1 = gfx10_GCEA_select1,
   .counter0_lo = R_034980_GCEA_PERFCOUNTER2_LO,

   .num_spm_modules = 1,
   .num_spm_wires = 2,
   .spm_block_select = AC_SPM_GLOBAL_BLOCK_GCEA,
};

static struct ac_pc_block_gfxdescr groups_gfx10[] = {
   {&gfx10_CB, 452},
   {&gfx10_CHA, 44},
   {&gfx10_CHCG, 34},
   {&gfx10_CHC, 34, 4},
   {&gfx10_CPC, 46},
   {&gfx10_CPF, 39},
   {&gfx10_CPG, 81},
   {&gfx10_DB, 369},
   {&gfx10_GCR, 93},
   {&gfx10_GDS, 120},
   {&gfx10_GE, 314},
   {&gfx10_GL1A, 35},
   {&gfx10_GL1C, 63, 4},
   {&gfx10_GL2A, 90},
   {&gfx10_GL2C, 234},
   {&gfx10_GRBM, 46},
   {&gfx10_GRBMSE, 18},
   {&gfx10_PA_PH, 959},
   {&gfx10_PA_SC, 551, 2},
   {&gfx10_PA_SU, 265},
   {&gfx10_RLC, 6},
   {&gfx10_RMI, 257},
   {&gfx10_SPI, 328},
   {&gfx10_SQ, 511},
   {&gfx10_SX, 224},
   {&gfx10_TA, 225},
   {&gfx10_TCP, 76},
   {&gfx10_TD, 60},
   {&gfx10_UTCL1, 14},
   {&gfx10_GCEA, 88},
};

const struct ac_pc_block_gfxdescr *
ac_gfx10_get_perfcounters(uint32_t *num_blocks)
{
   *num_blocks = ARRAY_SIZE(groups_gfx10);
   return groups_gfx10;
}
