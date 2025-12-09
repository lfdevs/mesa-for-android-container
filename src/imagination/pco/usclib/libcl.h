/*
 * Copyright © 2025 Imagination Technologies Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PCO_LIBCL_H
#define PCO_LIBCL_H

#include "common/pvr_iface.h"
#include "compiler/libcl/libcl.h"
#include "compiler/shader_enums.h"
#include "pco/pco_common.h"
#include "pp_macros.h"

#define OVERLOADABLE __attribute__((overloadable))

void nir_mutex_pco(enum pco_mutex_id mutex_id, enum pco_mutex_op mutex_op);
uint32_t nir_load_instance_num_pco(void);

uint32_t nir_load_ssbo(uint2 buffer_index,
                       uint offset,
                       enum gl_access_qualifier access,
                       uint align_mul,
                       uint align_offset,
                       uint offset_shift);

void nir_store_ssbo(uint32_t value,
                    uint2 block_index,
                    uint offset,
                    uint write_mask,
                    enum gl_access_qualifier access,
                    uint align_mul,
                    uint align_offset,
                    uint offset_shift);

uint32_t
nir_load_shared(uint offset, uint base, uint access, uint align_mul, uint align_offset);

void nir_store_shared(uint32_t value,
                      uint offset,
                      uint base,
                      uint access,
                      uint write_mask,
                      uint align_mul,
                      uint align_offset);

void OVERLOADABLE nir_uvsw_write_pco(uint offset, uint data);
void OVERLOADABLE nir_uvsw_write_pco(uint offset, uint2 data);
void OVERLOADABLE nir_uvsw_write_pco(uint offset, uint3 data);
void OVERLOADABLE nir_uvsw_write_pco(uint offset, uint4 data);

void OVERLOADABLE nir_uvsw_write_pco(uint offset, float data);
void OVERLOADABLE nir_uvsw_write_pco(uint offset, float2 data);
void OVERLOADABLE nir_uvsw_write_pco(uint offset, float3 data);
void OVERLOADABLE nir_uvsw_write_pco(uint offset, float4 data);

uint nir_load_vtxin_pco__1(uint offset);
uint2 nir_load_vtxin_pco__2(uint offset);
uint3 nir_load_vtxin_pco__3(uint offset);
uint4 nir_load_vtxin_pco__4(uint offset);

#define nir_load_vtxin_pco(n, ...) CAT2(nir_load_vtxin_pco__, n)(__VA_ARGS__)

uint nir_load_coeff_pco__1(uint offset);
uint2 nir_load_coeff_pco__2(uint offset);
uint3 nir_load_coeff_pco__3(uint offset);
uint4 nir_load_coeff_pco__4(uint offset);

#define nir_load_coeff_pco(n, ...) CAT2(nir_load_coeff_pco__, n)(__VA_ARGS__)

uint nir_load_preamble__1(uint base, uint preamble_class);
uint4 nir_load_preamble__4(uint base, uint preamble_class);

#define nir_load_preamble(n, ...) CAT2(nir_load_preamble__, n)(__VA_ARGS__)

void nir_store_preamble_dynamic(uint data, uint offset, uint preamble_class);

uint nir_dma_ld_pco__1(uint2 addr);
uint2 nir_dma_ld_pco__2(uint2 addr);
uint3 nir_dma_ld_pco__3(uint2 addr);
uint4 nir_dma_ld_pco__4(uint2 addr);
uint16 nir_dma_ld_pco__16(uint2 addr);

#define nir_dma_ld_pco(n, ...) CAT2(nir_dma_ld_pco__, n)(__VA_ARGS__)

void nir_dma_st_pco__1(uint3 addr_data, uint flags);
void nir_dma_st_pco__2(uint4 addr_data, uint flags);

#define SELECT_ARGS_ST(flags, addr, ...) \
   ((CAT2(uint, NUM_ARGS_PLUS_2(__VA_ARGS__)))(addr, __VA_ARGS__), flags)

/* clang-format off */
#define nir_dma_st_pco(flags, addr, ...) SELECT_NAME(nir_dma_st_pco, __, __VA_ARGS__)SELECT_ARGS_ST(flags, addr, __VA_ARGS__)
/* clang-format on */

void nir_dma_st_shregs_pco(uint2 addr,
                           uint burst_len,
                           uint shreg_offset,
                           uint flags);

void nir_dma_ld_shregs_pco(uint2 addr, uint burst_len, uint shreg_offset);

void nir_dma_idf_pco(uint2 addr);

uint2 nir_uadd64_32(uint lo, uint hi, uint offset);
uint nir_imad(uint a, uint b, uint c);
uint2 nir_umad64_32(uint a, uint b, uint lo, uint hi);

uint nir_load_shared_reg_alloc_size_pco(void);

uint nir_smp_pco(uint16 data,
                 uint4 tex_state,
                 uint4 smp_state,
                 uint smp_flags,
                 uint range);

uint nir_umax(uint a, uint b);
#endif /* PCO_LIBCL_H */
