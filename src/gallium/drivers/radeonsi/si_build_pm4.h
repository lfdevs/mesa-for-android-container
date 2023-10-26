/*
 * Copyright 2013 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * This file contains helpers for writing commands to commands streams.
 */

#ifndef SI_BUILD_PM4_H
#define SI_BUILD_PM4_H

#include "si_pipe.h"
#include "sid.h"

#define radeon_begin(cs) struct radeon_cmdbuf *__cs = (cs); \
                         unsigned __cs_num = __cs->current.cdw; \
                         UNUSED unsigned __cs_num_initial = __cs_num; \
                         uint32_t *__cs_buf = __cs->current.buf

#define radeon_begin_again(cs) do { \
   assert(__cs == NULL); \
   __cs = (cs); \
   __cs_num = __cs->current.cdw; \
   __cs_num_initial = __cs_num; \
   __cs_buf = __cs->current.buf; \
} while (0)

#define radeon_end() do { \
   __cs->current.cdw = __cs_num; \
   assert(__cs->current.cdw <= __cs->current.max_dw); \
   __cs = NULL; \
} while (0)

#define radeon_emit(value)  __cs_buf[__cs_num++] = (value)
#define radeon_packets_added()  (__cs_num != __cs_num_initial)

#define radeon_end_update_context_roll(sctx) do { \
   radeon_end(); \
   if (radeon_packets_added()) \
      (sctx)->context_roll = true; \
} while (0)

#define radeon_emit_array(values, num) do { \
   unsigned __n = (num); \
   memcpy(__cs_buf + __cs_num, (values), __n * 4); \
   __cs_num += __n; \
} while (0)

/* Instead of writing into the command buffer, return the pointer to the command buffer and
 * assume that the caller will fill the specified number of elements.
 */
#define radeon_emit_array_get_ptr(num, ptr) do { \
   *(ptr) = __cs_buf + __cs_num; \
   __cs_num += (num); \
} while (0)

#define radeon_set_config_reg_seq(reg, num) do { \
   assert((reg) < SI_CONTEXT_REG_OFFSET); \
   radeon_emit(PKT3(PKT3_SET_CONFIG_REG, num, 0)); \
   radeon_emit(((reg) - SI_CONFIG_REG_OFFSET) >> 2); \
} while (0)

#define radeon_set_config_reg(reg, value) do { \
   radeon_set_config_reg_seq(reg, 1); \
   radeon_emit(value); \
} while (0)

#define radeon_set_context_reg_seq(reg, num) do { \
   assert((reg) >= SI_CONTEXT_REG_OFFSET); \
   radeon_emit(PKT3(PKT3_SET_CONTEXT_REG, num, 0)); \
   radeon_emit(((reg) - SI_CONTEXT_REG_OFFSET) >> 2); \
} while (0)

#define radeon_set_context_reg(reg, value) do { \
   radeon_set_context_reg_seq(reg, 1); \
   radeon_emit(value); \
} while (0)

#define radeon_set_context_reg_seq_array(reg, num, values) do { \
   radeon_set_context_reg_seq(reg, num); \
   radeon_emit_array(values, num); \
} while (0)

#define radeon_set_context_reg_idx(reg, idx, value) do { \
   assert((reg) >= SI_CONTEXT_REG_OFFSET); \
   radeon_emit(PKT3(PKT3_SET_CONTEXT_REG, 1, 0)); \
   radeon_emit(((reg) - SI_CONTEXT_REG_OFFSET) >> 2 | ((idx) << 28)); \
   radeon_emit(value); \
} while (0)

#define radeon_set_sh_reg_seq(reg, num) do { \
   assert((reg) >= SI_SH_REG_OFFSET && (reg) < SI_SH_REG_END); \
   radeon_emit(PKT3(PKT3_SET_SH_REG, num, 0)); \
   radeon_emit(((reg) - SI_SH_REG_OFFSET) >> 2); \
} while (0)

#define radeon_set_sh_reg_idx_seq(sctx, reg, idx, num) do { \
   assert((reg) >= SI_SH_REG_OFFSET && (reg) < SI_SH_REG_END); \
   if ((sctx)->screen->info.uses_kernel_cu_mask) { \
      assert((sctx)->gfx_level >= GFX10); \
      radeon_emit(PKT3(PKT3_SET_SH_REG_INDEX, num, 0)); \
      radeon_emit((((reg) - SI_SH_REG_OFFSET) >> 2) | ((idx) << 28)); \
   } else { \
      radeon_emit(PKT3(PKT3_SET_SH_REG, num, 0)); \
      radeon_emit(((reg) - SI_SH_REG_OFFSET) >> 2); \
   } \
} while (0)

#define radeon_set_sh_reg(reg, value) do { \
   radeon_set_sh_reg_seq(reg, 1); \
   radeon_emit(value); \
} while (0)

#define radeon_set_sh_reg_idx(sctx, reg, idx, value) do { \
   radeon_set_sh_reg_idx_seq(sctx, reg, idx, 1); \
   radeon_emit(value); \
} while (0)

#define radeon_push_gfx_sh_reg(reg, value) do { \
   unsigned __i = sctx->num_buffered_gfx_sh_regs++; \
   assert(__i / 2 < ARRAY_SIZE(sctx->buffered_gfx_sh_regs)); \
   sctx->buffered_gfx_sh_regs[__i / 2].reg_offset[__i % 2] = ((reg) - SI_SH_REG_OFFSET) >> 2; \
   sctx->buffered_gfx_sh_regs[__i / 2].reg_value[__i % 2] = value; \
} while (0)

#define radeon_push_compute_sh_reg(reg, value) do { \
   unsigned __i = sctx->num_buffered_compute_sh_regs++; \
   assert(__i / 2 < ARRAY_SIZE(sctx->buffered_compute_sh_regs)); \
   sctx->buffered_compute_sh_regs[__i / 2].reg_offset[__i % 2] = ((reg) - SI_SH_REG_OFFSET) >> 2; \
   sctx->buffered_compute_sh_regs[__i / 2].reg_value[__i % 2] = value; \
} while (0)

#define radeon_set_or_push_gfx_sh_reg(reg, value) do { \
   if (HAS_PAIRS) { \
      radeon_push_gfx_sh_reg(reg, value); \
   } else { \
      radeon_set_sh_reg_seq(reg, 1); \
      radeon_emit(value); \
   } \
} while (0)

#define radeon_opt_push_gfx_sh_reg(offset, reg, val) do { \
   unsigned __value = val; \
   unsigned __reg = reg; \
   if (((sctx->tracked_regs.other_reg_saved_mask >> (__reg)) & 0x1) != 0x1 || \
       sctx->tracked_regs.other_reg_value[__reg] != __value) { \
      radeon_push_gfx_sh_reg(offset, __value); \
      sctx->tracked_regs.other_reg_saved_mask |= BITFIELD_BIT(__reg); \
      sctx->tracked_regs.other_reg_value[__reg] = __value; \
   } \
} while (0)

#define radeon_opt_push_compute_sh_reg(offset, reg, val) do { \
   unsigned __value = val; \
   if (((sctx->tracked_regs.other_reg_saved_mask >> (reg)) & 0x1) != 0x1 || \
       sctx->tracked_regs.other_reg_value[reg] != __value) { \
      radeon_push_compute_sh_reg(offset, __value); \
      sctx->tracked_regs.other_reg_saved_mask |= BITFIELD_BIT(reg); \
      sctx->tracked_regs.other_reg_value[reg] = __value; \
   } \
} while (0)

#define radeon_set_uconfig_reg_seq(reg, num, perfctr) do { \
   assert((reg) >= CIK_UCONFIG_REG_OFFSET && (reg) < CIK_UCONFIG_REG_END); \
   radeon_emit(PKT3(PKT3_SET_UCONFIG_REG, num, perfctr)); \
   radeon_emit(((reg) - CIK_UCONFIG_REG_OFFSET) >> 2); \
} while (0)

#define radeon_set_uconfig_reg(reg, value) do { \
   radeon_set_uconfig_reg_seq(reg, 1, false); \
   radeon_emit(value); \
} while (0)

#define radeon_set_uconfig_reg_perfctr(reg, value) do { \
   radeon_set_uconfig_reg_seq(reg, 1, true); \
   radeon_emit(value); \
} while (0)

#define radeon_set_uconfig_reg_idx(screen, gfx_level, reg, idx, value) do { \
   assert((reg) >= CIK_UCONFIG_REG_OFFSET && (reg) < CIK_UCONFIG_REG_END); \
   assert((idx) != 0); \
   unsigned __opcode = PKT3_SET_UCONFIG_REG_INDEX; \
   if ((gfx_level) < GFX9 || \
       ((gfx_level) == GFX9 && (screen)->info.me_fw_version < 26)) \
      __opcode = PKT3_SET_UCONFIG_REG; \
   radeon_emit(PKT3(__opcode, 1, 0)); \
   radeon_emit(((reg) - CIK_UCONFIG_REG_OFFSET) >> 2 | ((idx) << 28)); \
   radeon_emit(value); \
} while (0)

/* Emit PKT3_SET_CONTEXT_REG if the register value is different. */
#define radeon_opt_set_context_reg(sctx, offset, reg, val) do { \
   unsigned __value = val; \
   if (((sctx->tracked_regs.context_reg_saved_mask >> (reg)) & 0x1) != 0x1 || \
       sctx->tracked_regs.context_reg_value[reg] != __value) { \
      radeon_set_context_reg(offset, __value); \
      sctx->tracked_regs.context_reg_saved_mask |= 0x1ull << (reg); \
      sctx->tracked_regs.context_reg_value[reg] = __value; \
   } \
} while (0)

#define radeon_opt_set_context_reg_idx(sctx, offset, reg, idx, val) do { \
   unsigned __value = val; \
   if (((sctx->tracked_regs.context_reg_saved_mask >> (reg)) & 0x1) != 0x1 || \
       sctx->tracked_regs.context_reg_value[reg] != __value) { \
      radeon_set_context_reg_idx(offset, idx, __value); \
      sctx->tracked_regs.context_reg_saved_mask |= 0x1ull << (reg); \
      sctx->tracked_regs.context_reg_value[reg] = __value; \
   } \
} while (0)

/**
 * Set 2 consecutive registers if any registers value is different.
 * @param offset        starting register offset
 * @param val1          is written to first register
 * @param val2          is written to second register
 */
#define radeon_opt_set_context_reg2(sctx, offset, reg, val1, val2) do { \
   unsigned __value1 = (val1), __value2 = (val2); \
   if (((sctx->tracked_regs.context_reg_saved_mask >> (reg)) & 0x3) != 0x3 || \
       sctx->tracked_regs.context_reg_value[reg] != __value1 || \
       sctx->tracked_regs.context_reg_value[(reg) + 1] != __value2) { \
      radeon_set_context_reg_seq(offset, 2); \
      radeon_emit(__value1); \
      radeon_emit(__value2); \
      sctx->tracked_regs.context_reg_value[reg] = __value1; \
      sctx->tracked_regs.context_reg_value[(reg) + 1] = __value2; \
      sctx->tracked_regs.context_reg_saved_mask |= 0x3ull << (reg); \
   } \
} while (0)

/**
 * Set 3 consecutive registers if any registers value is different.
 */
#define radeon_opt_set_context_reg3(sctx, offset, reg, val1, val2, val3) do { \
   unsigned __value1 = (val1), __value2 = (val2), __value3 = (val3); \
   if (((sctx->tracked_regs.context_reg_saved_mask >> (reg)) & 0x7) != 0x7 || \
       sctx->tracked_regs.context_reg_value[reg] != __value1 || \
       sctx->tracked_regs.context_reg_value[(reg) + 1] != __value2 || \
       sctx->tracked_regs.context_reg_value[(reg) + 2] != __value3) { \
      radeon_set_context_reg_seq(offset, 3); \
      radeon_emit(__value1); \
      radeon_emit(__value2); \
      radeon_emit(__value3); \
      sctx->tracked_regs.context_reg_value[reg] = __value1; \
      sctx->tracked_regs.context_reg_value[(reg) + 1] = __value2; \
      sctx->tracked_regs.context_reg_value[(reg) + 2] = __value3; \
      sctx->tracked_regs.context_reg_saved_mask |= 0x7ull << (reg); \
   } \
} while (0)

/**
 * Set 4 consecutive registers if any registers value is different.
 */
#define radeon_opt_set_context_reg4(sctx, offset, reg, val1, val2, val3, val4) do { \
   unsigned __value1 = (val1), __value2 = (val2), __value3 = (val3), __value4 = (val4); \
   if (((sctx->tracked_regs.context_reg_saved_mask >> (reg)) & 0xf) != 0xf || \
       sctx->tracked_regs.context_reg_value[reg] != __value1 || \
       sctx->tracked_regs.context_reg_value[(reg) + 1] != __value2 || \
       sctx->tracked_regs.context_reg_value[(reg) + 2] != __value3 || \
       sctx->tracked_regs.context_reg_value[(reg) + 3] != __value4) { \
      radeon_set_context_reg_seq(offset, 4); \
      radeon_emit(__value1); \
      radeon_emit(__value2); \
      radeon_emit(__value3); \
      radeon_emit(__value4); \
      sctx->tracked_regs.context_reg_value[reg] = __value1; \
      sctx->tracked_regs.context_reg_value[(reg) + 1] = __value2; \
      sctx->tracked_regs.context_reg_value[(reg) + 2] = __value3; \
      sctx->tracked_regs.context_reg_value[(reg) + 3] = __value4; \
      sctx->tracked_regs.context_reg_saved_mask |= 0xfull << (reg); \
   } \
} while (0)

/**
 * Set 5 consecutive registers if any register value is different.
 */
#define radeon_opt_set_context_reg5(sctx, offset, reg, val0, val1, val2, val3, val4) do { \
   unsigned __value0 = (val0), __value1 = (val1), __value2 = (val2), __value3 = (val3), __value4 = (val4); \
   if (((sctx->tracked_regs.context_reg_saved_mask >> (reg)) & 0x1f) != 0x1f || \
       sctx->tracked_regs.context_reg_value[(reg) + 0] != __value0 || \
       sctx->tracked_regs.context_reg_value[(reg) + 1] != __value1 || \
       sctx->tracked_regs.context_reg_value[(reg) + 2] != __value2 || \
       sctx->tracked_regs.context_reg_value[(reg) + 3] != __value3 || \
       sctx->tracked_regs.context_reg_value[(reg) + 4] != __value4) { \
      radeon_set_context_reg_seq(offset, 5); \
      radeon_emit(__value0); \
      radeon_emit(__value1); \
      radeon_emit(__value2); \
      radeon_emit(__value3); \
      radeon_emit(__value4); \
      sctx->tracked_regs.context_reg_value[(reg) + 0] = __value0; \
      sctx->tracked_regs.context_reg_value[(reg) + 1] = __value1; \
      sctx->tracked_regs.context_reg_value[(reg) + 2] = __value2; \
      sctx->tracked_regs.context_reg_value[(reg) + 3] = __value3; \
      sctx->tracked_regs.context_reg_value[(reg) + 4] = __value4; \
      sctx->tracked_regs.context_reg_saved_mask |= 0x1full << (reg); \
   } \
} while (0)

/**
 * Set consecutive registers if any registers value is different.
 */
#define radeon_opt_set_context_regn(sctx, offset, value, saved_val, num) do { \
   if (memcmp(value, saved_val, sizeof(uint32_t) * (num))) { \
      radeon_set_context_reg_seq(offset, num); \
      radeon_emit_array(value, num); \
      memcpy(saved_val, value, sizeof(uint32_t) * (num)); \
   } \
} while (0)

#define radeon_opt_set_sh_reg(sctx, offset, reg, val) do { \
   unsigned __value = val; \
   if (((sctx->tracked_regs.other_reg_saved_mask >> (reg)) & 0x1) != 0x1 || \
       sctx->tracked_regs.other_reg_value[reg] != __value) { \
      radeon_set_sh_reg(offset, __value); \
      sctx->tracked_regs.other_reg_saved_mask |= BITFIELD_BIT(reg); \
      sctx->tracked_regs.other_reg_value[reg] = __value; \
   } \
} while (0)

/**
 * Set 2 consecutive registers if any register value is different.
 */
#define radeon_opt_set_sh_reg2(sctx, offset, reg, val1, val2) do { \
   unsigned __value1 = (val1), __value2 = (val2); \
   if (((sctx->tracked_regs.other_reg_saved_mask >> (reg)) & 0x3) != 0x3 || \
       sctx->tracked_regs.other_reg_value[reg] != __value1 || \
       sctx->tracked_regs.other_reg_value[(reg) + 1] != __value2) { \
      radeon_set_sh_reg_seq(offset, 2); \
      radeon_emit(__value1); \
      radeon_emit(__value2); \
      sctx->tracked_regs.other_reg_value[reg] = __value1; \
      sctx->tracked_regs.other_reg_value[(reg) + 1] = __value2; \
      sctx->tracked_regs.other_reg_saved_mask |= 0x3ull << (reg); \
   } \
} while (0)

/**
 * Set 3 consecutive registers if any register value is different.
 */
#define radeon_opt_set_sh_reg3(sctx, offset, reg, val1, val2, val3) do { \
   unsigned __value1 = (val1), __value2 = (val2), __value3 = (val3); \
   if (((sctx->tracked_regs.other_reg_saved_mask >> (reg)) & 0x7) != 0x7 || \
       sctx->tracked_regs.other_reg_value[reg] != __value1 || \
       sctx->tracked_regs.other_reg_value[(reg) + 1] != __value2 || \
       sctx->tracked_regs.other_reg_value[(reg) + 2] != __value3) { \
      radeon_set_sh_reg_seq(offset, 3); \
      radeon_emit(__value1); \
      radeon_emit(__value2); \
      radeon_emit(__value3); \
      sctx->tracked_regs.other_reg_value[reg] = __value1; \
      sctx->tracked_regs.other_reg_value[(reg) + 1] = __value2; \
      sctx->tracked_regs.other_reg_value[(reg) + 2] = __value3; \
      sctx->tracked_regs.other_reg_saved_mask |= 0x7ull << (reg); \
   } \
} while (0)

#define radeon_opt_set_sh_reg_idx(sctx, offset, reg, idx, val) do { \
   unsigned __value = val; \
   if (((sctx->tracked_regs.other_reg_saved_mask >> (reg)) & 0x1) != 0x1 || \
       sctx->tracked_regs.other_reg_value[reg] != __value) { \
      radeon_set_sh_reg_idx(sctx, offset, idx, __value); \
      sctx->tracked_regs.other_reg_saved_mask |= BITFIELD_BIT(reg); \
      sctx->tracked_regs.other_reg_value[reg] = __value; \
   } \
} while (0)

#define radeon_opt_set_uconfig_reg(sctx, offset, reg, val) do { \
   unsigned __value = val; \
   if (((sctx->tracked_regs.other_reg_saved_mask >> (reg)) & 0x1) != 0x1 || \
       sctx->tracked_regs.other_reg_value[reg] != __value) { \
      radeon_set_uconfig_reg(offset, __value); \
      sctx->tracked_regs.other_reg_saved_mask |= 0x1ull << (reg); \
      sctx->tracked_regs.other_reg_value[reg] = __value; \
   } \
} while (0)

#define radeon_opt_set_uconfig_reg_idx(sctx, gfx_level, offset, reg, idx, val) do { \
   unsigned __value = val; \
   if (((sctx->tracked_regs.other_reg_saved_mask >> (reg)) & 0x1) != 0x1 || \
       sctx->tracked_regs.other_reg_value[reg] != __value) { \
      radeon_set_uconfig_reg_idx((sctx)->screen, gfx_level, offset, idx, __value); \
      sctx->tracked_regs.other_reg_saved_mask |= 0x1ull << (reg); \
      sctx->tracked_regs.other_reg_value[reg] = __value; \
   } \
} while (0)

#define radeon_set_privileged_config_reg(reg, value) do { \
   assert((reg) < CIK_UCONFIG_REG_OFFSET); \
   radeon_emit(PKT3(PKT3_COPY_DATA, 4, 0)); \
   radeon_emit(COPY_DATA_SRC_SEL(COPY_DATA_IMM) | \
               COPY_DATA_DST_SEL(COPY_DATA_PERF)); \
   radeon_emit(value); \
   radeon_emit(0); /* unused */ \
   radeon_emit((reg) >> 2); \
   radeon_emit(0); /* unused */ \
} while (0)

#define radeon_emit_32bit_pointer(sscreen, va) do { \
   radeon_emit(va); \
   assert((va) == 0 || ((va) >> 32) == sscreen->info.address32_hi); \
} while (0)

#define radeon_emit_one_32bit_pointer(sctx, desc, sh_base) do { \
   unsigned sh_offset = (sh_base) + (desc)->shader_userdata_offset; \
   radeon_set_sh_reg_seq(sh_offset, 1); \
   radeon_emit_32bit_pointer(sctx->screen, (desc)->gpu_address); \
} while (0)

/* This should be evaluated at compile time if all parameters are constants. */
static ALWAYS_INLINE unsigned
si_get_user_data_base(enum amd_gfx_level gfx_level, enum si_has_tess has_tess,
                      enum si_has_gs has_gs, enum si_has_ngg ngg,
                      enum pipe_shader_type shader)
{
   switch (shader) {
   case PIPE_SHADER_VERTEX:
      /* VS can be bound as VS, ES, LS, or GS. */
      if (has_tess) {
         if (gfx_level >= GFX10) {
            return R_00B430_SPI_SHADER_USER_DATA_HS_0;
         } else if (gfx_level == GFX9) {
            return R_00B430_SPI_SHADER_USER_DATA_LS_0;
         } else {
            return R_00B530_SPI_SHADER_USER_DATA_LS_0;
         }
      } else if (gfx_level >= GFX10) {
         if (ngg || has_gs) {
            return R_00B230_SPI_SHADER_USER_DATA_GS_0;
         } else {
            return R_00B130_SPI_SHADER_USER_DATA_VS_0;
         }
      } else if (has_gs) {
         return R_00B330_SPI_SHADER_USER_DATA_ES_0;
      } else {
         return R_00B130_SPI_SHADER_USER_DATA_VS_0;
      }

   case PIPE_SHADER_TESS_CTRL:
      if (gfx_level == GFX9) {
         return R_00B430_SPI_SHADER_USER_DATA_LS_0;
      } else {
         return R_00B430_SPI_SHADER_USER_DATA_HS_0;
      }

   case PIPE_SHADER_TESS_EVAL:
      /* TES can be bound as ES, VS, or not bound. */
      if (has_tess) {
         if (gfx_level >= GFX10) {
            if (ngg || has_gs) {
               return R_00B230_SPI_SHADER_USER_DATA_GS_0;
            } else {
               return R_00B130_SPI_SHADER_USER_DATA_VS_0;
            }
         } else if (has_gs) {
            return R_00B330_SPI_SHADER_USER_DATA_ES_0;
         } else {
            return R_00B130_SPI_SHADER_USER_DATA_VS_0;
         }
      } else {
         return 0;
      }

   case PIPE_SHADER_GEOMETRY:
      if (gfx_level == GFX9) {
         return R_00B330_SPI_SHADER_USER_DATA_ES_0;
      } else {
         return R_00B230_SPI_SHADER_USER_DATA_GS_0;
      }

   default:
      assert(0);
      return 0;
   }
}

#endif
