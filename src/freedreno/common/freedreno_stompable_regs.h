/*
 * Copyright © 2023 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#ifndef __FREEDRENO_STOMPABLE_REGS_H__
#define __FREEDRENO_STOMPABLE_REGS_H__

#include <stdint.h>

#include "a6xx.xml.h"
#include "adreno_common.xml.h"
#include "adreno_pm4.xml.h"

/* In order to debug issues with usage of stale reg data we need to have
 * a list of regs which we allowed to stomp.
 * The regs we are NOT allowed to stomp are:
 * - Write protected;
 * - Written by kernel but are not write protected;
 * - Some regs that are not written by anyone but do affect the result.
 *
 * In addition, some regs are only emmitted during cmdbuf setup
 * so we have to have additional filter to get a reduced list of regs
 * stompable before each renderpass/blit.
 */

struct fd_stompable_reg_range {
   uint16_t start_reg;
   uint16_t end_reg;
};

static const struct fd_stompable_reg_range
   a6xx_fd_cmdbuf_stompable_reg_ranges[] = {
      {REG_A6XX_VSC_BIN_SIZE, REG_A6XX_VSC_DRAW_STRM_SIZE(31)},
      {REG_A6XX_UCHE_UNKNOWN_0E12, REG_A6XX_UCHE_UNKNOWN_0E12},
      {REG_A6XX_GRAS_CL_CNTL, REG_A6XX_GRAS_LRZ_DEPTH_VIEW},
      {REG_A6XX_GRAS_2D_BLIT_CNTL, REG_A6XX_GRAS_2D_RESOLVE_CNTL_2},
      {REG_A6XX_RB_BIN_CONTROL, REG_A6XX_RB_SAMPLE_LOCATION_1},
      {REG_A6XX_RB_RENDER_CONTROL0, REG_A6XX_RB_UNKNOWN_8811},
      {REG_A6XX_RB_UNKNOWN_8818, REG_A6XX_RB_UNKNOWN_881E},
      {REG_A6XX_RB_MRT(0), REG_A6XX_RB_BLEND_CNTL},
      {REG_A6XX_RB_DEPTH_PLANE_CNTL, REG_A6XX_RB_Z_BOUNDS_MAX},
      {REG_A6XX_RB_STENCIL_CONTROL, REG_A6XX_RB_STENCILWRMASK},
      {REG_A6XX_RB_WINDOW_OFFSET, REG_A6XX_RB_SAMPLE_COUNT_CONTROL},
      {REG_A6XX_RB_LRZ_CNTL, REG_A6XX_RB_LRZ_CNTL},
      {REG_A6XX_RB_Z_CLAMP_MIN, REG_A6XX_RB_Z_CLAMP_MAX},
      {REG_A6XX_RB_UNKNOWN_88D0, REG_A6XX_RB_BLIT_SCISSOR_BR},
      {REG_A6XX_RB_BIN_CONTROL2, REG_A6XX_RB_BLIT_INFO},
      {REG_A6XX_RB_UNKNOWN_88F0, REG_A6XX_RB_UNKNOWN_88F4},
      {REG_A6XX_RB_DEPTH_FLAG_BUFFER_BASE, REG_A6XX_RB_MRT_FLAG_BUFFER(7)},
      {REG_A6XX_RB_SAMPLE_COUNT_ADDR, REG_A6XX_RB_SAMPLE_COUNT_ADDR},
      {REG_A6XX_RB_2D_DST_INFO, REG_A6XX_RB_2D_SRC_SOLID_C3},
      {REG_A6XX_RB_DBG_ECO_CNTL, REG_A6XX_RB_ADDR_MODE_CNTL},
      {REG_A6XX_RB_CCU_CNTL, REG_A6XX_RB_CCU_CNTL},
      {REG_A6XX_VPC_GS_PARAM, REG_A6XX_VPC_POLYGON_MODE},
      {REG_A6XX_VPC_VARYING_INTERP(0), REG_A6XX_VPC_POINT_COORD_INVERT},
      {REG_A6XX_VPC_UNKNOWN_9300, REG_A6XX_VPC_SO_DISABLE},
      {REG_A6XX_VPC_DBG_ECO_CNTL, REG_A6XX_VPC_PERFCTR_VPC_SEL(5)},
      {REG_A6XX_PC_TESS_NUM_VERTEX, REG_A6XX_PC_DGEN_SU_CONSERVATIVE_RAS_CNTL},
      {REG_A6XX_PC_POLYGON_MODE, REG_A6XX_PC_RASTER_CNTL},
      {REG_A6XX_PC_PRIMITIVE_CNTL_0, REG_A6XX_PC_MULTIVIEW_MASK},
      {REG_A6XX_PC_DRAW_INDX_BASE, REG_A6XX_PC_TESSFACTOR_ADDR},
      {REG_A6XX_PC_VSTREAM_CONTROL, REG_A6XX_PC_BIN_DRAW_STRM},
      {REG_A6XX_PC_VISIBILITY_OVERRIDE, REG_A6XX_PC_VISIBILITY_OVERRIDE},
      {REG_A6XX_VFD_CONTROL_0, REG_A6XX_VFD_DEST_CNTL(31)},
      {REG_A6XX_VFD_POWER_CNTL, REG_A6XX_VFD_POWER_CNTL},
      {REG_A6XX_SP_VS_CTRL_REG0, REG_A6XX_SP_VS_PVT_MEM_HW_STACK_OFFSET},
      {REG_A6XX_SP_HS_CTRL_REG0, REG_A6XX_SP_HS_PVT_MEM_HW_STACK_OFFSET},
      {REG_A6XX_SP_DS_CTRL_REG0, REG_A6XX_SP_DS_PVT_MEM_HW_STACK_OFFSET},
      {REG_A6XX_SP_GS_CTRL_REG0, REG_A6XX_SP_GS_PVT_MEM_HW_STACK_OFFSET},
      {REG_A6XX_SP_VS_TEX_SAMP, REG_A6XX_SP_GS_TEX_CONST},
      {REG_A6XX_SP_FS_CTRL_REG0, REG_A6XX_SP_FS_PVT_MEM_HW_STACK_OFFSET},
      {REG_A6XX_SP_CS_CTRL_REG0, REG_A6XX_SP_CS_PVT_MEM_HW_STACK_OFFSET},
      {REG_A6XX_SP_CS_CNTL_0, REG_A6XX_SP_CS_CNTL_1},
      {REG_A6XX_SP_FS_TEX_SAMP, REG_A6XX_SP_CS_TEX_CONST},
      {REG_A6XX_SP_CS_IBO, REG_A6XX_SP_CS_IBO},
      {REG_A6XX_SP_CS_IBO_COUNT, REG_A6XX_SP_CS_IBO_COUNT},
      {REG_A6XX_SP_MODE_CONTROL, REG_A6XX_SP_BINDLESS_BASE(0)},
      {REG_A6XX_SP_IBO, REG_A6XX_SP_IBO_COUNT},
      {REG_A6XX_SP_CHICKEN_BITS, REG_A6XX_SP_FLOAT_CNTL},
      {REG_A6XX_SP_PS_TP_BORDER_COLOR_BASE_ADDR, REG_A6XX_SP_UNKNOWN_B183},
      {REG_A6XX_SP_TP_BORDER_COLOR_BASE_ADDR, REG_A6XX_SP_TP_MODE_CNTL},
      {REG_A6XX_SP_PS_2D_SRC_INFO, REG_A6XX_SP_WINDOW_OFFSET},
      {REG_A6XX_TPL1_DBG_ECO_CNTL, REG_A6XX_TPL1_DBG_ECO_CNTL},
      {REG_A6XX_HLSQ_VS_CNTL, REG_A6XX_HLSQ_GS_CNTL},
      {REG_A6XX_HLSQ_FS_CNTL_0, REG_A6XX_HLSQ_CS_CNTL},
      {REG_A6XX_HLSQ_CS_NDRANGE_0, REG_A6XX_HLSQ_CS_KERNEL_GROUP_Z},
      {REG_A6XX_HLSQ_CS_BINDLESS_BASE(0), REG_A6XX_HLSQ_CS_BINDLESS_BASE(0)},
      {REG_A6XX_HLSQ_CS_UNKNOWN_B9D0, REG_A6XX_HLSQ_CS_UNKNOWN_B9D0},
      {REG_A6XX_HLSQ_FS_CNTL, REG_A6XX_HLSQ_SHARED_CONSTS},
      {REG_A6XX_HLSQ_BINDLESS_BASE(0), REG_A6XX_HLSQ_BINDLESS_BASE(0)},
      {REG_A6XX_HLSQ_UNKNOWN_BE00, REG_A6XX_HLSQ_UNKNOWN_BE01},
};

/* Return true if it is expected that reg is overwritten by a renderpass or
 * not used by anything in a renderpass. So it's safe to stomp the reg
 * beforehand.
 */
static bool
a6xx_fd_reg_rp_stompable(bool turnip, uint16_t reg)
{
   switch (reg) {
   case REG_A6XX_VSC_DRAW_STRM_SIZE_ADDRESS ... REG_A6XX_VSC_DRAW_STRM_SIZE_ADDRESS + 1:
      return !turnip;
   case REG_A6XX_VSC_PRIM_STRM_ADDRESS ... REG_A6XX_VSC_DRAW_STRM_LIMIT:
      return false;
   case REG_A6XX_GRAS_SU_CONSERVATIVE_RAS_CNTL:
      return false;
   case REG_A6XX_GRAS_SAMPLE_CONFIG ... REG_A6XX_GRAS_SAMPLE_LOCATION_1:
      return turnip;
   case REG_A6XX_GRAS_UNKNOWN_80AF:
      return false;
   case REG_A6XX_GRAS_LRZ_DEPTH_VIEW:
      return turnip;
   case REG_A6XX_GRAS_UNKNOWN_8110:
      return false;
   case REG_A6XX_GRAS_DBG_ECO_CNTL ... REG_A6XX_GRAS_PERFCTR_LRZ_SEL(3):
      return false;
   case REG_A6XX_RB_SAMPLE_CONFIG ... REG_A6XX_RB_SAMPLE_LOCATION_1:
      return turnip;
   case REG_A6XX_RB_DITHER_CNTL:
      return !turnip;
   case REG_A6XX_RB_UNKNOWN_8811 ... REG_A6XX_RB_UNKNOWN_881E:
      return false;
   case REG_A6XX_RB_ALPHA_CONTROL:
      return !turnip;
   case REG_A6XX_RB_UNKNOWN_88F0:
      return false;
   case REG_A6XX_RB_SAMPLE_COUNT_ADDR ... REG_A6XX_RB_SAMPLE_COUNT_ADDR + 1:
      return false;
   case REG_A6XX_RB_UNKNOWN_8E01:
      return false;
   case REG_A6XX_RB_DBG_ECO_CNTL ... REG_A6XX_RB_CCU_CNTL:
      return false;
   case REG_A6XX_RB_PERFCTR_RB_SEL(0)... REG_A6XX_RB_UNKNOWN_8E51:
      return false;
   case REG_A6XX_VPC_UNKNOWN_9210 ... REG_A6XX_VPC_UNKNOWN_9211:
      return false;
   case REG_A6XX_VPC_SO(0) ... REG_A6XX_VPC_POINT_COORD_INVERT:
      return false;
   case REG_A6XX_VPC_UNKNOWN_9300:
      return false;
   case REG_A6XX_VPC_DBG_ECO_CNTL ... REG_A6XX_VPC_PERFCTR_VPC_SEL(5):
      return false;
   case REG_A6XX_PC_DRAW_CMD ... REG_A6XX_PC_MARKER:
      return false;
   case REG_A6XX_PC_DBG_ECO_CNTL ... REG_A6XX_PC_ADDR_MODE_CNTL:
      return false;
   case REG_A6XX_PC_TESSFACTOR_ADDR:
      return false;
   case REG_A6XX_VFD_MODE_CNTL:
      return false;
   case REG_A6XX_VFD_ADD_OFFSET:
      return false;
   case REG_A6XX_SP_UNKNOWN_A9A8:
      return false;
   case REG_A6XX_SP_DBG_ECO_CNTL ... REG_A6XX_SP_PERFCTR_SP_SEL(23):
      return false;
   case REG_A6XX_SP_PS_TP_BORDER_COLOR_BASE_ADDR ... REG_A6XX_SP_UNKNOWN_B183:
      return false;
   case REG_A6XX_SP_UNKNOWN_B190 ... REG_A6XX_SP_UNKNOWN_B191:
      return false;
   case REG_A6XX_SP_TP_BORDER_COLOR_BASE_ADDR ... REG_A6XX_SP_TP_SAMPLE_LOCATION_1:
      return false;
   case REG_A6XX_SP_TP_MODE_CNTL:
      return false;
   case REG_A6XX_TPL1_DBG_ECO_CNTL ... REG_A6XX_TPL1_PERFCTR_TP_SEL(11):
      return false;
   case REG_A6XX_HLSQ_UNKNOWN_BE00 ... REG_A6XX_HLSQ_PERFCTR_HLSQ_SEL(5):
      return false;

   /* We should not stomp compute pipeline since they write registers directly
    * into command stream and should survive renderpass.
    */
   case REG_A6XX_SP_CS_CTRL_REG0 ... REG_A6XX_SP_CS_CNTL_1:
      return false;
   case REG_A6XX_HLSQ_CS_CNTL ... REG_A6XX_HLSQ_CS_CNTL_1:
      return false;
   }

   return true;
}

/* Stomping some regs is known to cause issues */
static bool
a6xx_fd_reg_do_not_stomp(bool turnip, uint16_t reg)
{
   switch (reg) {
   /* Faults in
    * dEQP-VK.renderpass.suballocation.formats.r5g6b5_unorm_pack16.clear.clear
    * It seems that PC_CCU_FLUSH_COLOR_TS reads REG_A6XX_RB_DEPTH_PLANE_CNTL.
    */
   case REG_A6XX_RB_DEPTH_PLANE_CNTL:
      return true;
   /* Faults in
    * dEQP-VK.conditional_rendering.draw.condition_host_memory_expect_noop.draw */
   case REG_A6XX_HLSQ_VS_CNTL ... REG_A6XX_HLSQ_GS_CNTL:
      return true;
   case REG_A6XX_HLSQ_FS_CNTL:
      return true;
   /* Faults in
    * dEQP-VK.memory_model.message_passing.ext.u32.coherent.atomic_atomic.atomicrmw.device.payload_local.image.guard_local.image.comp
    * while there is even no fragment shaders.
    */
   case REG_A6XX_SP_FS_OBJ_START ... REG_A6XX_SP_FS_OBJ_START + 1:
      return true;
   }

   return false;
}

#endif /* __FREEDRENO_STOMPABLE_REGS_H__ */