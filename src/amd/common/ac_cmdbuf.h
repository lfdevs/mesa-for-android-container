/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_CMDBUF_H
#define AC_CMDBUF_H

#include <inttypes.h>

#include "ac_pm4.h"

#include "util/bitset.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ac_cmdbuf {
   uint32_t cdw;         /* Number of used dwords. */
   uint32_t max_dw;      /* Maximum number of dwords. */
   uint32_t reserved_dw; /* Number of dwords reserved. */
   uint32_t *buf;        /* The base pointer of the chunk. */

   bool context_roll;
};

/* The structure layout is identical to a pair of registers in SET_*_REG_PAIRS_PACKED. */
struct ac_gfx11_reg_pair {
   union {
      /* A pair of register offsets. */
      struct {
         uint16_t reg_offset[2];
      };
      /* The same pair of register offsets as a dword. */
      uint32_t reg_offsets;
   };
   /* A pair of register values for the register offsets above. */
   uint32_t reg_value[2];
};

/* A pair of values for SET_*_REG_PAIRS. */
struct ac_gfx12_reg {
   uint32_t reg_offset;
   uint32_t reg_value;
};

/* GFX11+: Buffered SH registers for SET_SH_REG_PAIRS_*. */
struct ac_buffered_sh_regs {
   uint32_t num;
   union {
      struct {
         struct ac_gfx11_reg_pair regs[32];
      } gfx11;

      struct {
         struct ac_gfx12_reg regs[256];
      } gfx12;
   };
};

/* Tracked registers. */
enum ac_tracked_reg
{
   /* CONTEXT registers. */
   /* 2 consecutive registers (GFX6-11), or separate registers (GFX12) */
   AC_TRACKED_DB_RENDER_CONTROL,
   AC_TRACKED_DB_COUNT_CONTROL,

   AC_TRACKED_DB_DEPTH_CONTROL,
   AC_TRACKED_DB_STENCIL_CONTROL,
   /* 2 consecutive registers */
   AC_TRACKED_DB_DEPTH_BOUNDS_MIN,
   AC_TRACKED_DB_DEPTH_BOUNDS_MAX,

   AC_TRACKED_SPI_INTERP_CONTROL_0,
   AC_TRACKED_PA_SU_POINT_SIZE,
   AC_TRACKED_PA_SU_POINT_MINMAX,
   AC_TRACKED_PA_SU_LINE_CNTL,
   AC_TRACKED_PA_SC_MODE_CNTL_0,
   AC_TRACKED_PA_SU_SC_MODE_CNTL,
   AC_TRACKED_PA_SC_EDGERULE,

   /* 6 consecutive registers */
   AC_TRACKED_PA_SU_POLY_OFFSET_DB_FMT_CNTL,
   AC_TRACKED_PA_SU_POLY_OFFSET_CLAMP,
   AC_TRACKED_PA_SU_POLY_OFFSET_FRONT_SCALE,
   AC_TRACKED_PA_SU_POLY_OFFSET_FRONT_OFFSET,
   AC_TRACKED_PA_SU_POLY_OFFSET_BACK_SCALE,
   AC_TRACKED_PA_SU_POLY_OFFSET_BACK_OFFSET,

   /* 2 consecutive registers */
   AC_TRACKED_PA_SC_LINE_CNTL,
   AC_TRACKED_PA_SC_AA_CONFIG,

   /* 5 consecutive registers (GFX6-11) */
   AC_TRACKED_PA_SU_VTX_CNTL,
   /* 4 consecutive registers (GFX12) */
   AC_TRACKED_PA_CL_GB_VERT_CLIP_ADJ,
   AC_TRACKED_PA_CL_GB_VERT_DISC_ADJ,
   AC_TRACKED_PA_CL_GB_HORZ_CLIP_ADJ,
   AC_TRACKED_PA_CL_GB_HORZ_DISC_ADJ,

   /* 2 consecutive registers */
   AC_TRACKED_SPI_SHADER_IDX_FORMAT,
   AC_TRACKED_SPI_SHADER_POS_FORMAT,

   /* 5 consecutive registers (GFX12), or 2 consecutive registers (GFX6-11) */
   AC_TRACKED_SPI_SHADER_Z_FORMAT,
   AC_TRACKED_SPI_SHADER_COL_FORMAT,

   /* 2 consecutive registers. */
   AC_TRACKED_SPI_PS_INPUT_ENA,
   AC_TRACKED_SPI_PS_INPUT_ADDR,

   AC_TRACKED_DB_EQAA,
   AC_TRACKED_DB_RENDER_OVERRIDE2,
   AC_TRACKED_DB_SHADER_CONTROL,
   AC_TRACKED_DB_STENCIL_REF,
   AC_TRACKED_DB_ALPHA_TO_MASK,
   AC_TRACKED_CB_COLOR_CONTROL,
   AC_TRACKED_CB_SHADER_MASK,
   AC_TRACKED_CB_TARGET_MASK,
   AC_TRACKED_PA_CL_CLIP_CNTL,
   AC_TRACKED_PA_CL_VS_OUT_CNTL,
   AC_TRACKED_PA_CL_VTE_CNTL,
   AC_TRACKED_PA_CL_VRS_CNTL,
   AC_TRACKED_PA_SC_CLIPRECT_RULE,
   AC_TRACKED_PA_SC_LINE_STIPPLE,
   AC_TRACKED_PA_SC_MODE_CNTL_1,
   AC_TRACKED_PA_SU_HARDWARE_SCREEN_OFFSET,
   AC_TRACKED_PA_SC_SAMPLE_PROPERTIES,
   AC_TRACKED_PA_SC_CONSERVATIVE_RASTERIZATION_CNTL,
   AC_TRACKED_SPI_PS_IN_CONTROL,
   AC_TRACKED_VGT_GS_INSTANCE_CNT,
   AC_TRACKED_VGT_GS_MAX_VERT_OUT,
   AC_TRACKED_VGT_SHADER_STAGES_EN,
   AC_TRACKED_VGT_LS_HS_CONFIG,
   AC_TRACKED_VGT_TF_PARAM,
   AC_TRACKED_VGT_DRAW_PAYLOAD_CNTL,
   AC_TRACKED_VGT_MULTI_PRIM_IB_RESET_INDX,
   AC_TRACKED_PA_SU_SMALL_PRIM_FILTER_CNTL,  /* GFX8-9 (only with has_small_prim_filter_sample_loc_bug) */
   AC_TRACKED_PA_SC_BINNER_CNTL_0,           /* GFX9+ */
   AC_TRACKED_GE_MAX_OUTPUT_PER_SUBGROUP,    /* GFX10+ - the SMALL_PRIM_FILTER slot above can be reused */
   AC_TRACKED_GE_NGG_SUBGRP_CNTL,            /* GFX10+ */
   AC_TRACKED_PA_CL_NGG_CNTL,                /* GFX10+ */
   AC_TRACKED_DB_PA_SC_VRS_OVERRIDE_CNTL,    /* GFX10.3+ */

   /* 3 consecutive registers */
   AC_TRACKED_SX_PS_DOWNCONVERT,             /* GFX8+ */
   AC_TRACKED_SX_BLEND_OPT_EPSILON,          /* GFX8+ */
   AC_TRACKED_SX_BLEND_OPT_CONTROL,          /* GFX8+ */

   /* The slots below can be reused by other generations. */
   AC_TRACKED_VGT_ESGS_RING_ITEMSIZE,        /* GFX6-8 (GFX9+ can reuse this slot) */
   AC_TRACKED_VGT_REUSE_OFF,                 /* GFX6-8,10.3 */
   AC_TRACKED_IA_MULTI_VGT_PARAM,            /* GFX6-8 (GFX9+ can reuse this slot) */

   AC_TRACKED_VGT_GS_MAX_PRIMS_PER_SUBGROUP, /* GFX9 - the slots above can be reused */
   AC_TRACKED_VGT_GS_ONCHIP_CNTL,            /* GFX9-10 - the slots above can be reused */

   AC_TRACKED_VGT_GSVS_RING_ITEMSIZE,        /* GFX6-10 (GFX11+ can reuse this slot) */
   AC_TRACKED_VGT_GS_MODE,                   /* GFX6-10 (GFX11+ can reuse this slot) */
   AC_TRACKED_VGT_VERTEX_REUSE_BLOCK_CNTL,   /* GFX6-10 (GFX11+ can reuse this slot) */
   AC_TRACKED_VGT_GS_OUT_PRIM_TYPE,          /* GFX6-10 (GFX11+ can reuse this slot) */

   /* 3 consecutive registers */
   AC_TRACKED_VGT_GSVS_RING_OFFSET_1,        /* GFX6-10 (GFX11+ can reuse this slot) */
   AC_TRACKED_VGT_GSVS_RING_OFFSET_2,        /* GFX6-10 (GFX11+ can reuse this slot) */
   AC_TRACKED_VGT_GSVS_RING_OFFSET_3,        /* GFX6-10 (GFX11+ can reuse this slot) */

   /* 4 consecutive registers */
   AC_TRACKED_VGT_GS_VERT_ITEMSIZE,          /* GFX6-10 (GFX11+ can reuse this slot) */
   AC_TRACKED_VGT_GS_VERT_ITEMSIZE_1,        /* GFX6-10 (GFX11+ can reuse this slot) */
   AC_TRACKED_VGT_GS_VERT_ITEMSIZE_2,        /* GFX6-10 (GFX11+ can reuse this slot) */
   AC_TRACKED_VGT_GS_VERT_ITEMSIZE_3,        /* GFX6-10 (GFX11+ can reuse this slot) */

   AC_TRACKED_SPI_VS_OUT_CONFIG,             /* GFX6-11 */
   AC_TRACKED_DB_RENDER_OVERRIDE = AC_TRACKED_SPI_VS_OUT_CONFIG, /* GFX12+ (slot reused) */
   AC_TRACKED_VGT_PRIMITIVEID_EN,            /* GFX6-11 */
   AC_TRACKED_CB_DCC_CONTROL,                /* GFX8-11 */
   AC_TRACKED_DB_STENCIL_READ_MASK,          /* GFX12+ */
   AC_TRACKED_DB_STENCIL_WRITE_MASK,         /* GFX12+ */
   AC_TRACKED_PA_SC_SHADER_CONTROL,          /* GFX9-10.3 */
   AC_TRACKED_PA_SC_HISZ_CONTROL = AC_TRACKED_PA_SC_SHADER_CONTROL, /* GFX12+ (slot reused)*/
   AC_TRACKED_PA_SC_LINE_STIPPLE_RESET,      /* GFX12+ */

   /* 2 consecutive registers */
   AC_TRACKED_DB_STENCILREFMASK,    /* GFX6-11.5 */
   AC_TRACKED_DB_STENCILREFMASK_BF, /* GFX6-11.5 */

   /* 2 consecutive registers */
   AC_TRACKED_PA_SC_AA_MASK_X0Y0_X1Y0,
   AC_TRACKED_PA_SC_AA_MASK_X0Y1_X1Y1,

   AC_TRACKED_UNUSED0, /* To force alignment */

   AC_NUM_TRACKED_CONTEXT_REGS,
   AC_FIRST_TRACKED_OTHER_REG = AC_NUM_TRACKED_CONTEXT_REGS,

   /* SH and UCONFIG registers. */
   AC_TRACKED_GE_PC_ALLOC = AC_FIRST_TRACKED_OTHER_REG, /* GFX10-11 */
   AC_TRACKED_SPI_SHADER_PGM_RSRC3_GS,       /* GFX7-11 */
   AC_TRACKED_SPI_SHADER_PGM_RSRC4_GS,       /* GFX10+ */
   AC_TRACKED_VGT_GS_OUT_PRIM_TYPE_UCONFIG,  /* GFX11+ */
   AC_TRACKED_SPI_SHADER_GS_OUT_CONFIG_PS,   /* GFX12+ */
   AC_TRACKED_VGT_PRIMITIVEID_EN_UCONFIG,    /* GFX12+ */

   AC_TRACKED_IA_MULTI_VGT_PARAM_UCONFIG,    /* GFX9 only */
   AC_TRACKED_GE_CNTL = AC_TRACKED_IA_MULTI_VGT_PARAM_UCONFIG, /* GFX10+ */

   AC_TRACKED_SPI_SHADER_PGM_RSRC2_HS,       /* GFX9+ (not tracked on previous chips) */
   AC_TRACKED_SPI_SHADER_USER_DATA_PS__ALPHA_REF,

   /* 3 consecutive registers. */
   AC_TRACKED_SPI_SHADER_USER_DATA_HS__TCS_OFFCHIP_LAYOUT,
   AC_TRACKED_SPI_SHADER_USER_DATA_HS__TCS_OFFCHIP_ADDR,
   AC_TRACKED_SPI_SHADER_USER_DATA_HS__VS_STATE_BITS,    /* GFX6-8 */

   AC_TRACKED_SPI_SHADER_USER_DATA_LS__BASE_VERTEX,
   AC_TRACKED_SPI_SHADER_USER_DATA_LS__DRAWID,
   AC_TRACKED_SPI_SHADER_USER_DATA_LS__START_INSTANCE,

   AC_TRACKED_SPI_SHADER_USER_DATA_ES__BASE_VERTEX,
   AC_TRACKED_SPI_SHADER_USER_DATA_ES__DRAWID,
   AC_TRACKED_SPI_SHADER_USER_DATA_ES__START_INSTANCE,

   AC_TRACKED_SPI_SHADER_USER_DATA_VS__BASE_VERTEX,      /* GFX6-10 */
   AC_TRACKED_SPI_SHADER_USER_DATA_VS__DRAWID,           /* GFX6-10 */
   AC_TRACKED_SPI_SHADER_USER_DATA_VS__START_INSTANCE,   /* GFX6-10 */

   AC_TRACKED_COMPUTE_RESOURCE_LIMITS,
   AC_TRACKED_COMPUTE_DISPATCH_INTERLEAVE,   /* GFX12+ (not tracked on previous chips) */
   AC_TRACKED_COMPUTE_NUM_THREAD_X,
   AC_TRACKED_COMPUTE_NUM_THREAD_Y,
   AC_TRACKED_COMPUTE_NUM_THREAD_Z,
   AC_TRACKED_COMPUTE_TMPRING_SIZE,
   AC_TRACKED_COMPUTE_PGM_RSRC3,             /* GFX11+ */

   /* 2 consecutive registers. */
   AC_TRACKED_COMPUTE_PGM_RSRC1,
   AC_TRACKED_COMPUTE_PGM_RSRC2,

   /* 3 consecutive registers. */
   AC_TRACKED_SPI_SHADER_GS_MESHLET_DIM,       /* GFX11+ */
   AC_TRACKED_SPI_SHADER_GS_MESHLET_EXP_ALLOC, /* GFX11+ */
   AC_TRACKED_SPI_SHADER_GS_MESHLET_CTRL,      /* GFX12+ */

   /* 2 consecutive registers. */
   AC_TRACKED_COMPUTE_DISPATCH_SCRATCH_BASE_LO, /* GFX11+ */
   AC_TRACKED_COMPUTE_DISPATCH_SCRATCH_BASE_HI, /* GFX11+ */

   /* This spot is only for new SH and UCONFIG register enums.
    * Context register enums should be before AC_NUM_TRACKED_CONTEXT_REGS.
    */

   AC_NUM_ALL_TRACKED_REGS,
};

struct ac_tracked_regs {
   BITSET_DECLARE(reg_saved_mask, AC_NUM_ALL_TRACKED_REGS);
   uint32_t reg_value[AC_NUM_ALL_TRACKED_REGS];
   uint32_t spi_ps_input_cntl[32];
   uint32_t cb_blend_control[8];
   uint32_t sx_mrt_blend_opt[8];
};

#define ac_cmdbuf_begin(cs) struct ac_cmdbuf *__cs = (cs);                        \
                            uint32_t __cs_num = __cs->cdw;                        \
                            UNUSED uint32_t __cs_num_initial = __cs_num;          \
                            UNUSED uint32_t __cs_reserved_dw = __cs->reserved_dw; \
                            UNUSED uint32_t *__cs_buf = __cs->buf

#define ac_cmdbuf_begin_again(cs) do { \
   assert(__cs == NULL);               \
   __cs = (cs);                        \
   __cs_num = __cs->cdw;               \
   __cs_num_initial = __cs_num;        \
   __cs_buf = __cs->buf;               \
} while (0)

#define ac_cmdbuf_emit(value)                                   \
   do {                                                         \
      assert(!__cs_reserved_dw || __cs_num < __cs_reserved_dw); \
      __cs_buf[__cs_num++] = (value);                           \
   } while (0)

#define ac_cmdbuf_packets_added()  (__cs_num != __cs_num_initial)

#define ac_cmdbuf_emit_array(values, num) do {                       \
   unsigned __n = (num);                                             \
   assert(!__cs_reserved_dw || __cs_num + __n <= __cs_reserved_dw);  \
   memcpy(__cs_buf + __cs_num, (values), __n * 4);                   \
   __cs_num += __n;                                                  \
} while (0)

#define ac_cmdbuf_end() do {           \
   __cs->cdw = __cs_num;               \
   assert(__cs->cdw <= __cs->max_dw);  \
   __cs = NULL;                        \
} while (0)

/* Packet building helpers. Don't use directly. */
#define __ac_cmdbuf_set_reg_seq(reg, num, idx, prefix_name, packet, reset_filter_cam)     \
   do {                                                                                   \
      assert((reg) >= prefix_name##_REG_OFFSET && (reg) < prefix_name##_REG_END);         \
      ac_cmdbuf_emit(PKT3(packet, num, 0) | PKT3_RESET_FILTER_CAM_S(reset_filter_cam));   \
      ac_cmdbuf_emit((((reg) - prefix_name##_REG_OFFSET) >> 2) | ((idx) << 28));          \
   } while (0)

#define __ac_cmdbuf_set_reg(reg, idx, value, prefix_name, packet)                         \
   do {                                                                                   \
      __ac_cmdbuf_set_reg_seq(reg, 1, idx, prefix_name, packet, 0);                       \
      ac_cmdbuf_emit(value);                                                              \
   } while (0)

/* Packet building helpers for CONFIG registers. */
#define ac_cmdbuf_set_cfg_reg_seq(reg, num) __ac_cmdbuf_set_reg_seq(reg, num, 0, SI_CONFIG, PKT3_SET_CONFIG_REG, 0)

#define ac_cmdbuf_set_cfg_reg(reg, value) __ac_cmdbuf_set_reg(reg, 0, value, SI_CONFIG, PKT3_SET_CONFIG_REG)

/* Packet building helpers for UCONFIG registers. */
#define ac_cmdbuf_set_ucfg_reg_seq(reg, num) __ac_cmdbuf_set_reg_seq(reg, num, 0, CIK_UCONFIG, PKT3_SET_UCONFIG_REG, 0)

#define ac_cmdbuf_set_ucfg_reg(reg, value) __ac_cmdbuf_set_reg(reg, 0, value, CIK_UCONFIG, PKT3_SET_UCONFIG_REG)

#define ac_cmdbuf_set_ucfg_reg_idx(info, reg, idx, value)                                          \
   do {                                                                                            \
      assert((idx));                                                                               \
      unsigned __opcode = PKT3_SET_UCONFIG_REG_INDEX;                                              \
      if ((info)->gfx_level < GFX9 || ((info)->gfx_level == GFX9 && (info)->me_fw_version < 26))   \
         __opcode = PKT3_SET_UCONFIG_REG;                                                          \
      __ac_cmdbuf_set_reg(reg, idx, value, CIK_UCONFIG, __opcode);                                 \
   } while (0)
/*
 * On GFX10, there is a bug with the ME implementation of its content
 * addressable memory (CAM), that means that it can skip register writes due
 * to not taking correctly into account the fields from the GRBM_GFX_INDEX.
 * With this __filter_cam_workaround bit we can force the write.
 */
#define ac_cmdbuf_set_ucfg_perfctr_reg_seq(gfx_level, ip_type, reg, num)                                 \
   do {                                                                                                  \
      const bool __filter_cam_workaround = (gfx_level) >= GFX10 && (ip_type) == AMD_IP_GFX;              \
      __ac_cmdbuf_set_reg_seq(reg, num, 0, CIK_UCONFIG, PKT3_SET_UCONFIG_REG, __filter_cam_workaround);  \
   } while (0)

#define ac_cmdbuf_set_ucfg_perfctr_reg(gfx_level, ip_type, reg, value)  \
   do {                                                                 \
      ac_cmdbuf_set_ucfg_perfctr_reg_seq(gfx_level, ip_type, reg, 1);   \
      ac_cmdbuf_emit(value);                                            \
   } while (0)

/* Packet building helpers for CONTEXT registers. */
#define ac_cmdbuf_set_ctx_reg_seq(reg, num) __ac_cmdbuf_set_reg_seq(reg, num, 0, SI_CONTEXT, PKT3_SET_CONTEXT_REG, 0)

#define ac_cmdbuf_set_ctx_reg(reg, value) __ac_cmdbuf_set_reg(reg, 0, value, SI_CONTEXT, PKT3_SET_CONTEXT_REG)

#define ac_cmdbuf_set_ctx_reg_idx(reg, idx, value) __ac_cmdbuf_set_reg(reg, idx, value, SI_CONTEXT, PKT3_SET_CONTEXT_REG)

/* Packet building helpers for SH registers. */
#define ac_cmdbuf_set_sh_reg_seq(reg, num) __ac_cmdbuf_set_reg_seq(reg, num, 0, SI_SH, PKT3_SET_SH_REG, 0)

#define ac_cmdbuf_set_sh_reg(reg, value) __ac_cmdbuf_set_reg(reg, 0, value, SI_SH, PKT3_SET_SH_REG)

#define ac_cmdbuf_set_sh_reg_idx(info, reg, idx, value)        \
   do {                                                        \
      assert((idx));                                           \
      unsigned __opcode = PKT3_SET_SH_REG_INDEX;               \
      if ((info)->gfx_level < GFX10)                           \
         __opcode = PKT3_SET_SH_REG;                           \
      __ac_cmdbuf_set_reg(reg, idx, value, SI_SH, __opcode);   \
   } while (0)

#define ac_cmdbuf_emit_32bit_pointer(sh_offset, va, info)         \
   do {                                                           \
      assert((va) == 0 || ((va) >> 32) == (info)->address32_hi);  \
      ac_cmdbuf_set_sh_reg(sh_offset, va);                        \
   } while (0)

#define ac_cmdbuf_emit_64bit_pointer(sh_offset, va)   \
   do {                                               \
      ac_cmdbuf_set_sh_reg_seq(sh_offset, 2);         \
      ac_cmdbuf_emit(va);                             \
      ac_cmdbuf_emit(va >> 32);                       \
   } while (0)

#define ac_cmdbuf_event_write_predicate(event_type, predicate)                               \
   do {                                                                                      \
      unsigned __event_type = (event_type);                                                  \
      ac_cmdbuf_emit(PKT3(PKT3_EVENT_WRITE, 0, predicate));                                  \
      ac_cmdbuf_emit(EVENT_TYPE(__event_type) |                                              \
                     EVENT_INDEX(__event_type == V_028A90_VS_PARTIAL_FLUSH ||                \
                                 __event_type == V_028A90_PS_PARTIAL_FLUSH ||                \
                                 __event_type == V_028A90_CS_PARTIAL_FLUSH ? 4 :             \
                                 __event_type == V_028A90_PIXEL_PIPE_STAT_CONTROL ? 1 : 0)); \
   } while (0)

#define ac_cmdbuf_event_write(event_type) ac_cmdbuf_event_write_predicate(event_type, false)

#define ac_cmdbuf_set_privileged_cfg_reg(reg, value)                                         \
   do {                                                                                      \
      assert((reg) < CIK_UCONFIG_REG_OFFSET);                                                \
      ac_cmdbuf_emit(PKT3(PKT3_COPY_DATA, 4, 0));                                            \
      ac_cmdbuf_emit(COPY_DATA_SRC_SEL(COPY_DATA_IMM) | COPY_DATA_DST_SEL(COPY_DATA_PERF));  \
      ac_cmdbuf_emit(value);                                                                 \
      ac_cmdbuf_emit(0); /* unused */                                                        \
      ac_cmdbuf_emit((reg) >> 2);                                                            \
      ac_cmdbuf_emit(0); /* unused */                                                        \
   } while (0)

/* GFX11 generic packet building helpers for buffered SH registers. Don't use these directly. */
#define ac_gfx11_push_reg(reg, value, prefix_name, buffer, reg_count)                  \
   do {                                                                                \
      unsigned __i = (reg_count)++;                                                    \
      assert((reg) >= prefix_name##_REG_OFFSET && (reg) < prefix_name##_REG_END);      \
      assert(__i / 2 < ARRAY_SIZE(buffer));                                            \
      buffer[__i / 2].reg_offset[__i % 2] = ((reg) - prefix_name##_REG_OFFSET) >> 2;   \
      buffer[__i / 2].reg_value[__i % 2] = value;                                      \
   } while (0)

/* GFX11 packet building helpers for SET_CONTEXT_REG_PAIRS_PACKED.
 * Registers are buffered on the stack and then copied to the command buffer at the end.
 */
#define ac_gfx11_begin_packed_ctx_regs()           \
   struct ac_gfx11_reg_pair __cs_context_regs[50]; \
   unsigned __cs_context_reg_count = 0;

#define ac_gfx11_set_ctx_reg(reg, value)                                               \
   ac_gfx11_push_reg(reg, value, SI_CONTEXT, __cs_context_regs, __cs_context_reg_count)

#define ac_gfx11_end_packed_ctx_regs()                                                                      \
   do {                                                                                                     \
      if (__cs_context_reg_count >= 2) {                                                                    \
         /* Align the count to 2 by duplicating the first register. */                                      \
         if (__cs_context_reg_count % 2 == 1) {                                                             \
            ac_gfx11_set_ctx_reg(SI_CONTEXT_REG_OFFSET + __cs_context_regs[0].reg_offset[0] * 4,            \
                                     __cs_context_regs[0].reg_value[0]);                                    \
         }                                                                                                  \
         assert(__cs_context_reg_count % 2 == 0);                                                           \
         unsigned __num_dw = (__cs_context_reg_count / 2) * 3;                                              \
         ac_cmdbuf_emit(PKT3(PKT3_SET_CONTEXT_REG_PAIRS_PACKED, __num_dw, 0) | PKT3_RESET_FILTER_CAM_S(1)); \
         ac_cmdbuf_emit(__cs_context_reg_count);                                                            \
         ac_cmdbuf_emit_array(__cs_context_regs, __num_dw);                                                 \
      } else if (__cs_context_reg_count == 1) {                                                             \
         ac_cmdbuf_emit(PKT3(PKT3_SET_CONTEXT_REG, 1, 0));                                                  \
         ac_cmdbuf_emit(__cs_context_regs[0].reg_offset[0]);                                                \
         ac_cmdbuf_emit(__cs_context_regs[0].reg_value[0]);                                                 \
      }                                                                                                     \
   } while (0)

/* GFX12 generic packet building helpers for PAIRS packets. Don't use these directly. */

/* Reserved 1 DWORD to emit the packet header when the sequence ends. */
#define __ac_gfx12_begin_regs(header) uint32_t header = __cs_num++

/* Set a register unconditionally. */
#define ac_gfx12_set_reg(reg, value, base_offset)   \
   do {                                             \
      ac_cmdbuf_emit(((reg) - (base_offset)) >> 2); \
      ac_cmdbuf_emit(value);                        \
   } while (0)

/* End the sequence and emit the packet header. */
#define __ac_gfx12_end_regs(header, packet)                                               \
   do {                                                                                   \
      if ((header) + 1 == __cs_num) {                                                     \
         __cs_num--; /* no registers have been set, back off */                           \
      } else {                                                                            \
         unsigned __dw_count = __cs_num - (header) - 2;                                   \
         __cs_buf[(header)] = PKT3((packet), __dw_count, 0) | PKT3_RESET_FILTER_CAM_S(1); \
      }                                                                                   \
   } while (0)

/* GFX12 packet building helpers for PAIRS packets. */
#define ac_gfx12_begin_ctx_regs() __ac_gfx12_begin_regs(__cs_context_reg_header)

#define ac_gfx12_set_ctx_reg(reg, value) ac_gfx12_set_reg(reg, value, SI_CONTEXT_REG_OFFSET)

#define ac_gfx12_end_ctx_regs() __ac_gfx12_end_regs(__cs_context_reg_header, PKT3_SET_CONTEXT_REG_PAIRS)

/* GFX12 generic packet building helpers for buffered registers. */
#define __ac_gfx12_push_reg(buf_regs, reg, value, base_offset)             \
   do {                                                                    \
      unsigned __i = buf_regs.num++;                                       \
      assert(__i < ARRAY_SIZE(buf_regs.gfx12.regs));                       \
      buf_regs.gfx12.regs[__i].reg_offset = ((reg) - (base_offset)) >> 2;  \
      buf_regs.gfx12.regs[__i].reg_value = value;                          \
   } while (0)

#define ac_gfx12_push_sh_reg(buf_regs, reg, value) \
   __ac_gfx12_push_reg(buf_regs, reg, value, SI_SH_REG_OFFSET)

#define ac_gfx12_push_32bit_pointer(buf_regs, sh_offset, va, info)   \
   do {                                                              \
      assert((va) == 0 || ((va) >> 32) == (info)->address32_hi);     \
      ac_gfx12_push_sh_reg(buf_regs, sh_offset, va);                 \
   } while (0)

#define ac_gfx12_push_64bit_pointer(buf_regs, sh_offset, va)         \
   do {                                                              \
      ac_gfx12_push_sh_reg(buf_regs, sh_offset, va);                 \
      ac_gfx12_push_sh_reg(buf_regs, sh_offset + 4, va >> 32);       \
   } while (0)

/* Tracked registers. */
#define ac_cmdbuf_opt_set_ctx_reg(tracked_regs, reg, reg_enum, value)      \
   do {                                                                    \
      const uint32_t __value = (value);                                    \
      if (!BITSET_TEST(tracked_regs->reg_saved_mask, (reg_enum)) ||        \
          tracked_regs->reg_value[(reg_enum)] != __value) {                \
         ac_cmdbuf_set_ctx_reg(reg, __value);                              \
         BITSET_SET(tracked_regs->reg_saved_mask, (reg_enum));             \
         tracked_regs->reg_value[(reg_enum)] = __value;                    \
         __cs->context_roll = true;                                        \
      }                                                                    \
   } while (0)

#define ac_cmdbuf_opt_set_ctx_reg2(tracked_regs, reg, reg_enum, v1, v2)                                     \
   do {                                                                                                     \
      static_assert(BITSET_BITWORD(reg_enum) == BITSET_BITWORD(reg_enum + 1),                               \
                    "bit range crosses dword boundary");                                                    \
      const uint32_t __v1 = (v1);                                                                           \
      const uint32_t __v2 = (v2);                                                                           \
      if (!BITSET_TEST_RANGE_INSIDE_WORD(tracked_regs->reg_saved_mask, (reg_enum), (reg_enum) + 1, 0x3) ||  \
          tracked_regs->reg_value[(reg_enum)] != __v1 || tracked_regs->reg_value[(reg_enum) + 1] != __v2) { \
         ac_cmdbuf_set_ctx_reg_seq(reg, 2);                                                                 \
         ac_cmdbuf_emit(__v1);                                                                              \
         ac_cmdbuf_emit(__v2);                                                                              \
         BITSET_SET_RANGE_INSIDE_WORD(tracked_regs->reg_saved_mask, (reg_enum), (reg_enum) + 1);            \
         tracked_regs->reg_value[(reg_enum)] = __v1;                                                        \
         tracked_regs->reg_value[(reg_enum) + 1] = __v2;                                                    \
         __cs->context_roll = true;                                                                         \
      }                                                                                                     \
   } while (0)

#define ac_cmdbuf_opt_set_ctx_reg3(tracked_regs, reg, reg_enum, v1, v2, v3)                                 \
   do {                                                                                                     \
      static_assert(BITSET_BITWORD(reg_enum) == BITSET_BITWORD(reg_enum + 2),                               \
                    "bit range crosses dword boundary");                                                    \
      const uint32_t __v1 = (v1);                                                                           \
      const uint32_t __v2 = (v2);                                                                           \
      const uint32_t __v3 = (v3);                                                                           \
      if (!BITSET_TEST_RANGE_INSIDE_WORD(tracked_regs->reg_saved_mask, (reg_enum), (reg_enum) + 2, 0x7) ||  \
          tracked_regs->reg_value[(reg_enum)] != __v1 || tracked_regs->reg_value[(reg_enum) + 1] != __v2 || \
          tracked_regs->reg_value[(reg_enum) + 2] != __v3) {                                                \
         ac_cmdbuf_set_ctx_reg_seq(reg, 3);                                                                 \
         ac_cmdbuf_emit(__v1);                                                                              \
         ac_cmdbuf_emit(__v2);                                                                              \
         ac_cmdbuf_emit(__v3);                                                                              \
         BITSET_SET_RANGE_INSIDE_WORD(__tracked_regs->reg_saved_mask, (reg_enum), (reg_enum) + 2);          \
         tracked_regs->reg_value[(reg_enum)] = __v1;                                                        \
         tracked_regs->reg_value[(reg_enum) + 1] = __v2;                                                    \
         tracked_regs->reg_value[(reg_enum) + 2] = __v3;                                                    \
         __cs->context_roll = true;                                                                         \
      }                                                                                                     \
   } while (0)

#define ac_cmdbuf_opt_set_ctx_reg4(tracked_regs, reg, reg_enum, v1, v2, v3, v4)                                   \
   do {                                                                                                           \
      static_assert(BITSET_BITWORD((reg_enum)) == BITSET_BITWORD((reg_enum) + 3),                                 \
                    "bit range crosses dword boundary");                                                          \
      const uint32_t __v1 = (v1);                                                                                 \
      const uint32_t __v2 = (v2);                                                                                 \
      const uint32_t __v3 = (v3);                                                                                 \
      const uint32_t __v4 = (v4);                                                                                 \
      if (!BITSET_TEST_RANGE_INSIDE_WORD(tracked_regs->reg_saved_mask, (reg_enum), (reg_enum) + 3, 0xf) ||        \
          tracked_regs->reg_value[(reg_enum)] != __v1 || tracked_regs->reg_value[(reg_enum) + 1] != __v2 ||       \
          tracked_regs->reg_value[(reg_enum) + 2] != __v3 || tracked_regs->reg_value[(reg_enum) + 3] != __v4) {   \
         ac_cmdbuf_set_ctx_reg_seq(reg, 4);                                                                       \
         ac_cmdbuf_emit(__v1);                                                                                    \
         ac_cmdbuf_emit(__v2);                                                                                    \
         ac_cmdbuf_emit(__v3);                                                                                    \
         ac_cmdbuf_emit(__v4);                                                                                    \
         BITSET_SET_RANGE_INSIDE_WORD(tracked_regs->reg_saved_mask, (reg_enum), (reg_enum) + 3);                  \
         tracked_regs->reg_value[(reg_enum)] = __v1;                                                              \
         tracked_regs->reg_value[(reg_enum) + 1] = __v2;                                                          \
         tracked_regs->reg_value[(reg_enum) + 2] = __v3;                                                          \
         tracked_regs->reg_value[(reg_enum) + 3] = __v4;                                                          \
         __cs->context_roll = true;                                                                               \
      }                                                                                                           \
   } while (0)

#define ac_cmdbuf_opt_set_ctx_regn(reg, values, saved_values, num)      \
   do {                                                                 \
      if (memcmp(values, saved_values, sizeof(uint32_t) * (num))) {     \
         ac_cmdbuf_set_ctx_reg_seq(reg, num);                           \
         ac_cmdbuf_emit_array(values, num);                             \
         memcpy(saved_values, values, sizeof(uint32_t) * (num));        \
         __cs->context_roll = true;                                     \
      }                                                                 \
   } while (0)

/* GFX11 */
#define ac_gfx11_opt_push_reg(tracked_regs, reg, reg_enum, value, prefix_name, buffer, reg_count)           \
   do {                                                                                                     \
      const uint32_t __value = (value);                                                                     \
      if (!BITSET_TEST(tracked_regs->reg_saved_mask, (reg_enum)) ||                                         \
          tracked_regs->reg_value[(reg_enum)] != __value) {                                                 \
         ac_gfx11_push_reg((reg), __value, prefix_name, buffer, reg_count);                                 \
         BITSET_SET(tracked_regs->reg_saved_mask, (reg_enum));                                              \
         tracked_regs->reg_value[(reg_enum)] = __value;                                                     \
      }                                                                                                     \
   } while (0)

#define ac_gfx11_opt_push_reg2(tracked_regs, reg, reg_enum, v1, v2, prefix_name, buffer, reg_count)         \
   do {                                                                                                     \
      static_assert(BITSET_BITWORD(reg_enum) == BITSET_BITWORD(reg_enum + 1),                               \
                    "bit range crosses dword boundary");                                                    \
      const uint32_t __v1 = (v1);                                                                           \
      const uint32_t __v2 = (v2);                                                                           \
      if (!BITSET_TEST_RANGE_INSIDE_WORD(tracked_regs->reg_saved_mask, (reg_enum), (reg_enum) + 1, 0x3) ||  \
          tracked_regs->reg_value[(reg_enum)] != __v1 || tracked_regs->reg_value[(reg_enum) + 1] != __v2) { \
         ac_gfx11_push_reg((reg), __v1, prefix_name, buffer, reg_count);                                    \
         ac_gfx11_push_reg((reg) + 4, __v2, prefix_name, buffer, reg_count);                                \
         BITSET_SET_RANGE_INSIDE_WORD(tracked_regs->reg_saved_mask, (reg_enum), (reg_enum) + 1);            \
         tracked_regs->reg_value[(reg_enum)] = __v1;                                                        \
         tracked_regs->reg_value[(reg_enum) + 1] = __v2;                                                    \
      }                                                                                                     \
   } while (0)

#define ac_gfx11_opt_push_reg4(tracked_regs, reg, reg_enum, v1, v2, v3, v4, prefix_name, buffer, reg_count)       \
   do {                                                                                                           \
      static_assert(BITSET_BITWORD((reg_enum)) == BITSET_BITWORD((reg_enum) + 3),                                 \
                    "bit range crosses dword boundary");                                                          \
      const uint32_t __v1 = (v1);                                                                                 \
      const uint32_t __v2 = (v2);                                                                                 \
      const uint32_t __v3 = (v3);                                                                                 \
      const uint32_t __v4 = (v4);                                                                                 \
      if (!BITSET_TEST_RANGE_INSIDE_WORD(tracked_regs->reg_saved_mask, (reg_enum), (reg_enum) + 3, 0xf) ||        \
          tracked_regs->reg_value[(reg_enum)] != __v1 ||                                                          \
          tracked_regs->reg_value[(reg_enum) + 1] != __v2 ||                                                      \
          tracked_regs->reg_value[(reg_enum) + 2] != __v3 ||                                                      \
          tracked_regs->reg_value[(reg_enum) + 3] != __v4) {                                                      \
         ac_gfx11_push_reg((reg), __v1, prefix_name, buffer, reg_count);                                          \
         ac_gfx11_push_reg((reg) + 4, __v2, prefix_name, buffer, reg_count);                                      \
         ac_gfx11_push_reg((reg) + 8, __v3, prefix_name, buffer, reg_count);                                      \
         ac_gfx11_push_reg((reg) + 12, __v4, prefix_name, buffer, reg_count);                                     \
         BITSET_SET_RANGE_INSIDE_WORD(tracked_regs->reg_saved_mask,                                               \
                                      (reg_enum), (reg_enum) + 3);                                                \
         tracked_regs->reg_value[(reg_enum)] = __v1;                                                              \
         tracked_regs->reg_value[(reg_enum) + 1] = __v2;                                                          \
         tracked_regs->reg_value[(reg_enum) + 2] = __v3;                                                          \
         tracked_regs->reg_value[(reg_enum) + 3] = __v4;                                                          \
      }                                                                                                           \
   } while (0)

#define ac_gfx11_opt_set_ctx_reg(tracked_regs, reg, reg_enum, value)                                             \
   ac_gfx11_opt_push_reg(tracked_regs, reg, reg_enum, value, SI_CONTEXT, __cs_context_regs, __cs_context_reg_count)

#define ac_gfx11_opt_set_ctx_reg2(tracked_regs, reg, reg_enum, v1, v2)                                           \
   ac_gfx11_opt_push_reg2(tracked_regs, reg, reg_enum, v1, v2, SI_CONTEXT, __cs_context_regs, __cs_context_reg_count)

/* GFX12 */
#define ac_gfx12_opt_set_reg(tracked_regs, reg, reg_enum, value, base_offset)                                     \
   do {                                                                                                           \
      const uint32_t __value = (value);                                                                           \
      if (!BITSET_TEST(tracked_regs->reg_saved_mask, (reg_enum)) ||                                               \
          tracked_regs->reg_value[(reg_enum)] != __value) {                                                       \
         ac_gfx12_set_reg((reg), __value, base_offset);                                                           \
         BITSET_SET(tracked_regs->reg_saved_mask, (reg_enum));                                                    \
         tracked_regs->reg_value[(reg_enum)] = __value;                                                           \
      }                                                                                                           \
   } while (0)

#define ac_gfx12_opt_set_reg2(tracked_regs, reg, reg_enum, v1, v2, base_offset)                                   \
   do {                                                                                                           \
      static_assert(BITSET_BITWORD(reg_enum) == BITSET_BITWORD(reg_enum + 1),                                     \
                    "bit range crosses dword boundary");                                                          \
      const uint32_t __v1 = (v1);                                                                                 \
      const uint32_t __v2 = (v2);                                                                                 \
      if (!BITSET_TEST_RANGE_INSIDE_WORD(tracked_regs->reg_saved_mask, (reg_enum), (reg_enum) + 1, 0x3) ||        \
          tracked_regs->reg_value[(reg_enum)] != __v1 || tracked_regs->reg_value[(reg_enum) + 1] != __v2) {       \
         ac_gfx12_set_reg((reg), __v1, base_offset);                                                              \
         ac_gfx12_set_reg((reg) + 4, __v2, base_offset);                                                          \
         BITSET_SET_RANGE_INSIDE_WORD(tracked_regs->reg_saved_mask, (reg_enum), (reg_enum) + 1);                  \
         tracked_regs->reg_value[(reg_enum)] = __v1;                                                              \
         tracked_regs->reg_value[(reg_enum) + 1] = __v2;                                                          \
      }                                                                                                           \
   } while (0)

#define ac_gfx12_opt_set_reg4(tracked_regs, reg, reg_enum, v1, v2, v3, v4, base_offset)                           \
   do {                                                                                                           \
      static_assert(BITSET_BITWORD((reg_enum)) == BITSET_BITWORD((reg_enum) + 3),                                 \
                    "bit range crosses dword boundary");                                                          \
      const uint32_t __v1 = (v1);                                                                                 \
      const uint32_t __v2 = (v2);                                                                                 \
      const uint32_t __v3 = (v3);                                                                                 \
      const uint32_t __v4 = (v4);                                                                                 \
      if (!BITSET_TEST_RANGE_INSIDE_WORD(tracked_regs->reg_saved_mask,                                            \
                                         (reg_enum), (reg_enum) + 3, 0xf) ||                                      \
          tracked_regs->reg_value[(reg_enum)] != __v1 ||                                                          \
          tracked_regs->reg_value[(reg_enum) + 1] != __v2 ||                                                      \
          tracked_regs->reg_value[(reg_enum) + 2] != __v3 ||                                                      \
          tracked_regs->reg_value[(reg_enum) + 3] != __v4) {                                                      \
         ac_gfx12_set_reg((reg), __v1, (base_offset));                                                            \
         ac_gfx12_set_reg((reg) + 4, __v2, (base_offset));                                                        \
         ac_gfx12_set_reg((reg) + 8, __v3, (base_offset));                                                        \
         ac_gfx12_set_reg((reg) + 12, __v4, (base_offset));                                                       \
         BITSET_SET_RANGE_INSIDE_WORD(tracked_regs->reg_saved_mask,                                               \
                                      (reg_enum), (reg_enum) + 3);                                                \
         tracked_regs->reg_value[(reg_enum)] = __v1;                                                              \
         tracked_regs->reg_value[(reg_enum) + 1] = __v2;                                                          \
         tracked_regs->reg_value[(reg_enum) + 2] = __v3;                                                          \
         tracked_regs->reg_value[(reg_enum) + 3] = __v4;                                                          \
      }                                                                                                           \
   } while (0)

struct ac_preamble_state {
   uint64_t border_color_va;

   struct {
      bool cache_cb_gl2;
      bool cache_db_gl2;
   } gfx10;

   struct {
      uint32_t compute_dispatch_interleave;
   } gfx11;
};

void
ac_init_compute_preamble_state(const struct ac_preamble_state *state,
                               struct ac_pm4_state *pm4);

void
ac_init_graphics_preamble_state(const struct ac_preamble_state *state,
                                struct ac_pm4_state *pm4);

void
ac_init_tracked_regs(struct ac_tracked_regs *tracked_regs,
                     const struct radeon_info *info, bool init_to_clear_state);

void
ac_set_tracked_regs_to_clear_state(struct ac_tracked_regs *tracked_regs,
                                   const struct radeon_info *info);

void
ac_cmdbuf_flush_vgt_streamout(struct ac_cmdbuf *cs, enum amd_gfx_level gfx_level);

#ifdef __cplusplus
}
#endif

#endif
