/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_CMDBUF_CP_H
#define AC_CMDBUF_CP_H

#include <inttypes.h>
#include <stdbool.h>

#include "amd_family.h"

#include "util/macros.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ac_cmdbuf;
struct radeon_info;

enum ac_cp_indirect_buffer_flags {
   AC_CP_INDIRECT_BUFFER_CHAIN = 1u << 0,
   AC_CP_INDIRECT_BUFFER_VALID = 1u << 1,
};

void
ac_emit_cp_indirect_buffer(struct ac_cmdbuf *cs, uint64_t va, uint32_t cdw,
                           enum ac_cp_indirect_buffer_flags flags,
                           bool predicate);

void
ac_emit_cp_cond_exec(struct ac_cmdbuf *cs, enum amd_gfx_level gfx_level,
                     uint64_t va, uint32_t count);

void
ac_emit_cp_write_data_head(struct ac_cmdbuf *cs, uint32_t engine_sel,
                           uint32_t dst_sel, uint64_t va, uint32_t size,
                           bool predicate);

void
ac_emit_cp_write_data(struct ac_cmdbuf *cs, uint32_t engine_sel,
                      uint32_t dst_sel, uint64_t va, uint32_t size,
                      const uint32_t *data, bool predicate);

void
ac_emit_cp_write_data_imm(struct ac_cmdbuf *cs, unsigned engine_sel,
                          uint64_t va, uint32_t value);

void
ac_emit_cp_wait_mem(struct ac_cmdbuf *cs, uint64_t va, uint32_t ref,
                    uint32_t mask, unsigned flags);

void
ac_emit_cp_acquire_mem_pws(struct ac_cmdbuf *cs, ASSERTED enum amd_gfx_level gfx_level,
                           ASSERTED enum amd_ip_type ip_type, uint32_t event_type,
                           uint32_t stage_sel, uint32_t count,
                           uint32_t gcr_cntl);

void
ac_emit_cp_release_mem_pws(struct ac_cmdbuf *cs, ASSERTED enum amd_gfx_level gfx_level,
                           ASSERTED enum amd_ip_type ip_type, uint32_t event_type,
                           uint32_t gcr_cntl);

enum ac_cp_copy_data_flags {
   AC_CP_COPY_DATA_WR_CONFIRM = 1u << 0,
   AC_CP_COPY_DATA_COUNT_SEL = 1u << 1, /* 64 bits */
   AC_CP_COPY_DATA_ENGINE_PFP = 1u << 2,
};

void
ac_emit_cp_copy_data(struct ac_cmdbuf *cs, uint32_t src_sel, uint32_t dst_sel,
                     uint64_t src_va, uint64_t dst_va,
                     enum ac_cp_copy_data_flags flags, bool predicate);

void
ac_emit_cp_pfp_sync_me(struct ac_cmdbuf *cs, bool predicate);

void
ac_emit_cp_set_predication(struct ac_cmdbuf *cs, enum amd_gfx_level gfx_level,
                           uint64_t va, uint32_t op);

void
ac_emit_cp_gfx11_ge_rings(struct ac_cmdbuf *cs, const struct radeon_info *info,
                          uint64_t attr_ring_va, bool enable_gfx12_partial_hiz_wa);

void
ac_emit_cp_tess_rings(struct ac_cmdbuf *cs, const struct radeon_info *info,
                      uint64_t va);

void
ac_emit_cp_gfx_scratch(struct ac_cmdbuf *cs, enum amd_gfx_level gfx_level,
                       uint64_t va, uint32_t size);

void
ac_emit_cp_acquire_mem(struct ac_cmdbuf *cs, enum amd_gfx_level gfx_level,
                       enum amd_ip_type ip_type, uint32_t engine,
                       uint32_t gcr_cntl);

void
ac_emit_cp_release_mem(struct ac_cmdbuf *cs, enum amd_gfx_level gfx_level,
                       enum amd_ip_type ip_type, uint32_t event,
                       uint32_t event_flags, uint32_t dst_sel,
                       uint32_t int_sel, uint32_t data_sel, uint64_t va,
                       uint32_t new_fence, uint64_t eop_bug_va);

void
ac_emit_cp_atomic_mem(struct ac_cmdbuf *cs, uint32_t atomic_op,
                      uint32_t atomic_cmd, uint64_t va, uint64_t data,
                      uint64_t compare_data);

void
ac_emit_cp_nop(struct ac_cmdbuf *cs, uint32_t value);

void
ac_emit_cp_load_context_reg_index(struct ac_cmdbuf *cs, uint32_t reg,
                                  uint32_t reg_count, uint64_t va,
                                  bool predicate);

void
ac_emit_cp_inhibit_clockgating(struct ac_cmdbuf *cs, enum amd_gfx_level gfx_level,
                               bool inhibit);

void
ac_emit_cp_spi_config_cntl(struct ac_cmdbuf *cs, enum amd_gfx_level gfx_level,
                           bool enable);

#ifdef __cplusplus
}
#endif

#endif
