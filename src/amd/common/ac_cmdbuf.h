/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_CMDBUF_H
#define AC_CMDBUF_H

#include <inttypes.h>

#include "ac_pm4.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ac_cmdbuf {
   uint32_t cdw;         /* Number of used dwords. */
   uint32_t max_dw;      /* Maximum number of dwords. */
   uint32_t reserved_dw; /* Number of dwords reserved. */
   uint32_t *buf;        /* The base pointer of the chunk. */
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
#define ac_cmdbuf_set_config_reg_seq(reg, num) __ac_cmdbuf_set_reg_seq(reg, num, 0, SI_CONFIG, PKT3_SET_CONFIG_REG, 0)

#define ac_cmdbuf_set_config_reg(reg, value) __ac_cmdbuf_set_reg(reg, 0, value, SI_CONFIG, PKT3_SET_CONFIG_REG)

/* Packet building helpers for UCONFIG registers. */
#define ac_cmdbuf_set_uconfig_reg_seq(reg, num) __ac_cmdbuf_set_reg_seq(reg, num, 0, CIK_UCONFIG, PKT3_SET_UCONFIG_REG, 0)

#define ac_cmdbuf_set_uconfig_reg(reg, value) __ac_cmdbuf_set_reg(reg, 0, value, CIK_UCONFIG, PKT3_SET_UCONFIG_REG)

/*
 * On GFX10, there is a bug with the ME implementation of its content
 * addressable memory (CAM), that means that it can skip register writes due
 * to not taking correctly into account the fields from the GRBM_GFX_INDEX.
 * With this __filter_cam_workaround bit we can force the write.
 */
#define ac_cmdbuf_set_uconfig_perfctr_reg_seq(gfx_level, ip_type, reg, num)                              \
   do {                                                                                                  \
      const bool __filter_cam_workaround = (gfx_level) >= GFX10 && (ip_type) == AMD_IP_GFX;              \
      __ac_cmdbuf_set_reg_seq(reg, num, 0, CIK_UCONFIG, PKT3_SET_UCONFIG_REG, __filter_cam_workaround);  \
   } while (0)

#define ac_cmdbuf_set_uconfig_perfctr_reg(gfx_level, ip_type, reg, value)  \
   do {                                                                    \
      ac_cmdbuf_set_uconfig_perfctr_reg_seq(gfx_level, ip_type, reg, 1);   \
      ac_cmdbuf_emit(value);                                               \
   } while (0)

/* Packet building helpers for CONTEXT registers. */
#define ac_cmdbuf_set_context_reg_seq(reg, num) __ac_cmdbuf_set_reg_seq(reg, num, 0, SI_CONTEXT, PKT3_SET_CONTEXT_REG, 0)

#define ac_cmdbuf_set_context_reg(reg, value) __ac_cmdbuf_set_reg(reg, 0, value, SI_CONTEXT, PKT3_SET_CONTEXT_REG)

#define ac_cmdbuf_set_context_reg_idx(reg, idx, value) __ac_cmdbuf_set_reg(reg, idx, value, SI_CONTEXT, PKT3_SET_CONTEXT_REG)

/* Packet building helpers for SH registers. */
#define ac_cmdbuf_set_sh_reg_seq(reg, num) __ac_cmdbuf_set_reg_seq(reg, num, 0, SI_SH, PKT3_SET_SH_REG, 0)

#define ac_cmdbuf_set_sh_reg(reg, value) __ac_cmdbuf_set_reg(reg, 0, value, SI_SH, PKT3_SET_SH_REG)

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

#define ac_cmdbuf_set_privileged_config_reg(reg, value)                                      \
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
#define ac_gfx11_begin_packed_context_regs()       \
   struct ac_gfx11_reg_pair __cs_context_regs[50]; \
   unsigned __cs_context_reg_count = 0;

#define ac_gfx11_set_context_reg(reg, value)                                              \
   ac_gfx11_push_reg(reg, value, SI_CONTEXT, __cs_context_regs, __cs_context_reg_count)

#define ac_gfx11_end_packed_context_regs()                                                                  \
   do {                                                                                                     \
      if (__cs_context_reg_count >= 2) {                                                                    \
         /* Align the count to 2 by duplicating the first register. */                                      \
         if (__cs_context_reg_count % 2 == 1) {                                                             \
            ac_gfx11_set_context_reg(SI_CONTEXT_REG_OFFSET + __cs_context_regs[0].reg_offset[0] * 4,        \
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
#define ac_gfx12_begin_context_regs() __ac_gfx12_begin_regs(__cs_context_reg_header)

#define ac_gfx12_set_context_reg(reg, value) ac_gfx12_set_reg(reg, value, SI_CONTEXT_REG_OFFSET)

#define ac_gfx12_end_context_regs() __ac_gfx12_end_regs(__cs_context_reg_header, PKT3_SET_CONTEXT_REG_PAIRS)

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
ac_cmdbuf_flush_vgt_streamout(struct ac_cmdbuf *cs, enum amd_gfx_level gfx_level);

#ifdef __cplusplus
}
#endif

#endif
