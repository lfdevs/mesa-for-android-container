/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_CS_H
#define RADV_CS_H

#include <assert.h>
#include <stdint.h>

#include "radv_cmd_buffer.h"
#include "radv_radeon_winsys.h"
#include "radv_sdma.h"
#include "sid.h"

#include "ac_cmdbuf_cp.h"
#include "ac_cmdbuf_sdma.h"

static inline unsigned
radeon_check_space(struct radeon_winsys *ws, struct ac_cmdbuf *cs, unsigned needed)
{
   assert(cs->cdw <= cs->reserved_dw);
   if (cs->max_dw - cs->cdw < needed)
      ws->cs_grow(cs, needed);
   cs->reserved_dw = MAX2(cs->reserved_dw, cs->cdw + needed);
   return cs->cdw + needed;
}

#define radeon_begin(cs)                                                                                               \
   struct radv_cmd_stream *__rcs = (cs);                                                                               \
   ac_cmdbuf_begin(__rcs->b);

#define radeon_end() ac_cmdbuf_end()

#define radeon_emit(value) ac_cmdbuf_emit(value)

#define radeon_emit_array(values, num) ac_cmdbuf_emit_array(values, num)

/* Packet building helpers for CONFIG registers. */
#define radeon_set_config_reg_seq(reg, num) ac_cmdbuf_set_cfg_reg_seq(reg, num)

#define radeon_set_config_reg(reg, value) ac_cmdbuf_set_cfg_reg(reg, value)

/* Packet building helpers for CONTEXT registers. */
#define radeon_set_context_reg_seq(reg, num) ac_cmdbuf_set_ctx_reg_seq(reg, num)

#define radeon_set_context_reg(reg, value) ac_cmdbuf_set_ctx_reg(reg, value)

#define radeon_set_context_reg_idx(reg, idx, value) ac_cmdbuf_set_ctx_reg_idx(reg, idx, value)

#define radeon_opt_set_context_reg(reg, reg_enum, value)                                                               \
   do {                                                                                                                \
      struct ac_tracked_regs *__tracked_regs = &__rcs->tracked_regs;                                                   \
      ac_cmdbuf_opt_set_ctx_reg(__tracked_regs, reg, reg_enum, value);                                                 \
   } while (0)

#define radeon_opt_set_context_reg2(reg, reg_enum, v1, v2)                                                             \
   do {                                                                                                                \
      struct ac_tracked_regs *__tracked_regs = &__rcs->tracked_regs;                                                   \
      ac_cmdbuf_opt_set_ctx_reg2(__tracked_regs, reg, reg_enum, v1, v2);                                               \
   } while (0)

#define radeon_opt_set_context_reg3(reg, reg_enum, v1, v2, v3)                                                         \
   do {                                                                                                                \
      struct ac_tracked_regs *__tracked_regs = &__rcs->tracked_regs;                                                   \
      ac_cmdbuf_opt_set_ctx_reg3(__tracked_regs, reg, reg_enum, v1, v2, v3);                                           \
   } while (0)

#define radeon_opt_set_context_reg4(reg, reg_enum, v1, v2, v3, v4)                                                     \
   do {                                                                                                                \
      struct ac_tracked_regs *__tracked_regs = &__rcs->tracked_regs;                                                   \
      ac_cmdbuf_opt_set_ctx_reg4(__tracked_regs, reg, reg_enum, v1, v2, v3, v4);                                       \
   } while (0)

#define radeon_opt_set_context_regn(reg, values, saved_values, num)                                                    \
   ac_cmdbuf_opt_set_ctx_regn(reg, values, saved_values, num)

/* Packet building helpers for SH registers. */
#define radeon_set_sh_reg_seq(reg, num) ac_cmdbuf_set_sh_reg_seq(reg, num)

#define radeon_set_sh_reg(reg, value) ac_cmdbuf_set_sh_reg(reg, value)

#define radeon_set_sh_reg_idx(info, reg, idx, value) ac_cmdbuf_set_sh_reg_idx(info, reg, idx, value)

/* Packet building helpers for UCONFIG registers. */
#define radeon_set_uconfig_reg_seq(reg, num) ac_cmdbuf_set_ucfg_reg_seq(reg, num)

#define radeon_set_uconfig_reg(reg, value) ac_cmdbuf_set_ucfg_reg(reg, value)

#define radeon_set_uconfig_reg_idx(info, reg, idx, value) ac_cmdbuf_set_ucfg_reg_idx(info, reg, idx, value)

#define radeon_set_uconfig_perfctr_reg_seq(gfx_level, ip_type, reg, num)                                               \
   ac_cmdbuf_set_ucfg_perfctr_reg_seq(gfx_level, ip_type, reg, num)

#define radeon_set_uconfig_perfctr_reg(gfx_level, ip_type, reg, value)                                                 \
   ac_cmdbuf_set_ucfg_perfctr_reg(gfx_level, ip_type, reg, value)

#define radeon_set_privileged_config_reg(reg, value) ac_cmdbuf_set_privileged_cfg_reg(reg, value)

#define radeon_event_write_predicate(event_type, predicate) ac_cmdbuf_event_write_predicate(event_type, predicate)

#define radeon_event_write(event_type) ac_cmdbuf_event_write(event_type)

#define radeon_emit_32bit_pointer(sh_offset, va, info) ac_cmdbuf_emit_32bit_pointer(sh_offset, va, info)

#define radeon_emit_64bit_pointer(sh_offset, va) ac_cmdbuf_emit_64bit_pointer(sh_offset, va)

/* GFX12 packet building helpers for PAIRS packets. */
#define gfx12_begin_context_regs() ac_gfx12_begin_ctx_regs()

#define gfx12_set_context_reg(reg, value) ac_gfx12_set_ctx_reg(reg, value)

#define gfx12_opt_set_context_reg(reg, reg_enum, value)                                                                \
   do {                                                                                                                \
      struct ac_tracked_regs *__tracked_regs = &__rcs->tracked_regs;                                                   \
      ac_gfx12_opt_set_reg(__tracked_regs, reg, reg_enum, value, SI_CONTEXT_REG_OFFSET);                               \
   } while (0)

#define gfx12_opt_set_context_reg2(reg, reg_enum, v1, v2)                                                              \
   do {                                                                                                                \
      struct ac_tracked_regs *__tracked_regs = &__rcs->tracked_regs;                                                   \
      ac_gfx12_opt_set_reg2(__tracked_regs, reg, reg_enum, v1, v2, SI_CONTEXT_REG_OFFSET);                             \
   } while (0)

#define gfx12_end_context_regs() ac_gfx12_end_ctx_regs()

/* GFX12 packet building helpers for buffered registers. */
#define gfx12_push_sh_reg(reg, value) ac_gfx12_push_sh_reg(__rcs->buffered_sh_regs, reg, value)

#define gfx12_push_32bit_pointer(sh_offset, va, info)                                                                  \
   ac_gfx12_push_32bit_pointer(__rcs->buffered_sh_regs, sh_offset, va, info)

#define gfx12_push_64bit_pointer(sh_offset, va) ac_gfx12_push_64bit_pointer(__rcs->buffered_sh_regs, sh_offset, va)

/* GFX11 packet building helpers for PAIRS packets. */
#define gfx11_begin_packed_context_regs() ac_gfx11_begin_packed_ctx_regs()
#define gfx11_set_context_reg(reg, value) ac_gfx11_set_ctx_reg(reg, value)
#define gfx11_end_packed_context_regs()   ac_gfx11_end_packed_ctx_regs()

#define gfx11_opt_set_context_reg(reg, reg_enum, value)                                                                \
   do {                                                                                                                \
      struct ac_tracked_regs *__tracked_regs = &__rcs->tracked_regs;                                                   \
      ac_gfx11_opt_set_ctx_reg(__tracked_regs, reg, reg_enum, value);                                                  \
   } while (0)

#define gfx11_opt_set_context_reg2(reg, reg_enum, v1, v2)                                                              \
   do {                                                                                                                \
      struct ac_tracked_regs *__tracked_regs = &__rcs->tracked_regs;                                                   \
      ac_gfx11_opt_set_ctx_reg2(__tracked_regs, reg, reg_enum, v1, v2);                                                \
   } while (0)

ALWAYS_INLINE static void
radv_gfx12_emit_buffered_regs(const struct radv_device *device, struct radv_cmd_stream *cs)
{
   const uint32_t reg_count = cs->buffered_sh_regs.num;

   if (!reg_count)
      return;

   radeon_check_space(device->ws, cs->b, 1 + reg_count * 2);

   radeon_begin(cs);
   radeon_emit(PKT3(PKT3_SET_SH_REG_PAIRS, reg_count * 2 - 1, 0) |
               PKT3_RESET_FILTER_CAM_S(cs->hw_ip == AMD_IP_GFX));
   radeon_emit_array(cs->buffered_sh_regs.gfx12.regs, reg_count * 2);
   radeon_end();

   cs->buffered_sh_regs.num = 0;
}

ALWAYS_INLINE static void
radv_cp_wait_mem(struct radv_cmd_stream *cs, const uint32_t op, const uint64_t va, const uint32_t ref,
                 const uint32_t mask)
{
   assert(op == WAIT_REG_MEM_EQUAL || op == WAIT_REG_MEM_NOT_EQUAL || op == WAIT_REG_MEM_GREATER_OR_EQUAL);

   if (cs->hw_ip == AMD_IP_GFX || cs->hw_ip == AMD_IP_COMPUTE) {
      ac_emit_cp_wait_mem(cs->b, va, ref, mask, op);
   } else if (cs->hw_ip == AMD_IP_SDMA) {
      ac_emit_sdma_wait_mem(cs->b, op, va, ref, mask);
   } else {
      UNREACHABLE("unsupported queue family");
   }
}

ALWAYS_INLINE static unsigned
radv_cs_write_data_head(const struct radv_device *device, struct radv_cmd_stream *cs, const unsigned engine_sel,
                        const uint64_t va, const unsigned count, const bool predicating)
{
   /* Return the correct cdw at the end of the packet so the caller can assert it. */
   const unsigned cdw_end = radeon_check_space(device->ws, cs->b, 4 + count);

   if (cs->hw_ip == AMD_IP_COMPUTE || cs->hw_ip == AMD_IP_GFX) {
      ac_emit_cp_write_data_head(cs->b, engine_sel, V_371_MEMORY, va, count, predicating);
   } else if (cs->hw_ip == AMD_IP_SDMA) {
      ac_emit_sdma_write_data_head(cs->b, va, count);
   } else {
      UNREACHABLE("unsupported queue family");
   }

   return cdw_end;
}

ALWAYS_INLINE static void
radv_cs_write_data(const struct radv_device *device, struct radv_cmd_stream *cs, const unsigned engine_sel,
                   const uint64_t va, const unsigned count, const uint32_t *dwords, const bool predicating)
{
   ASSERTED const unsigned cdw_end = radv_cs_write_data_head(device, cs, engine_sel, va, count, predicating);

   radeon_begin(cs);
   radeon_emit_array(dwords, count);
   radeon_end();
   assert(cs->b->cdw == cdw_end);
}

void radv_cs_emit_write_event_eop(struct radv_cmd_stream *cs, enum amd_gfx_level gfx_level, unsigned event,
                                  unsigned event_flags, unsigned dst_sel, unsigned int_sel, unsigned data_sel,
                                  uint64_t va, uint32_t new_fence, uint64_t gfx9_eop_bug_va);

void radv_cs_emit_cache_flush(struct radeon_winsys *ws, struct radv_cmd_stream *cs, enum amd_gfx_level gfx_level,
                              uint32_t *flush_cnt, uint64_t flush_va, enum radv_cmd_flush_bits flush_bits,
                              enum rgp_flush_bits *sqtt_flush_bits, uint64_t gfx9_eop_bug_va);

VkResult radv_create_cmd_stream(const struct radv_device *device, const enum amd_ip_type ip_type,
                                const bool is_secondary, struct radv_cmd_stream **cs_out);

void radv_init_cmd_stream(const struct radv_device *device, struct radv_cmd_stream *cs, const enum amd_ip_type ip_type);

void radv_reset_cmd_stream(const struct radv_device *device, struct radv_cmd_stream *cs);

VkResult radv_finalize_cmd_stream(const struct radv_device *device, struct radv_cmd_stream *cs);

void radv_destroy_cmd_stream(const struct radv_device *device, struct radv_cmd_stream *cs);

#endif /* RADV_CS_H */
