/*
 * Copyright © 2025 Imagination Technologies Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "libcl.h"
#include "compiler/libcl/libcl_vk.h"

KERNEL(1)
vs_nop_common(void)
{
   return;
}

KERNEL(1)
fs_nop_common(void)
{
   return;
}

KERNEL(1)
cs_nop_common(void)
{
   return;
}

/* gl_Position = vec4(in.xyz, 1.0f); */
KERNEL(1)
vs_passthrough_common(void)
{
   nir_uvsw_write_pco(0, nir_load_vtxin_pco(3, 0));
   nir_uvsw_write_pco(3, 1.0f);
}

/* gl_Position = vec4(in.xyz, 1.0f); rta_out = in.w; */
KERNEL(1)
vs_passthrough_rta_common(void)
{
   vs_passthrough_common();
   nir_uvsw_write_pco(4, nir_load_vtxin_pco(1, 3));
}

/* TODO: uint index = cl_global_id.x;
 * instead of this function once things
 * are properly hooked up.
*/
static inline uint
query_calc_global_id(void)
{
   uint local_invoc_index = nir_load_vtxin_pco(1, 0);
   local_invoc_index &= get_local_size(0) - 1;
   uint wg_id = nir_load_coeff_pco(1, 0);
   return nir_imad(wg_id, get_local_size(0), local_invoc_index);
}

/* TODO: support parameter passing. */
/* TODO: switch to common implementation. */
KERNEL(32)
cs_query_availability_common(void)
{
   uint index_count = nir_load_preamble(1, PVR_QUERY_AVAILABILITY_DATA_INDEX_COUNT, 0);

   uint index_base_addr_lo = nir_load_preamble(1, PVR_QUERY_AVAILABILITY_DATA_INDEX_BO_LO, 0);
   uint index_base_addr_hi = nir_load_preamble(1, PVR_QUERY_AVAILABILITY_DATA_INDEX_BO_HI, 0);

   uint avail_base_addr_lo = nir_load_preamble(1, PVR_QUERY_AVAILABILITY_DATA_BO_LO, 0);
   uint avail_base_addr_hi = nir_load_preamble(1, PVR_QUERY_AVAILABILITY_DATA_BO_HI, 0);

   uint index = query_calc_global_id();

   if (index < index_count) {
      uint2 index_addr = nir_uadd64_32(index_base_addr_lo, index_base_addr_hi, index * sizeof(uint32_t));
      uint offset = nir_dma_ld_pco(1, index_addr);

      uint2 avail_addr = nir_uadd64_32(avail_base_addr_lo, avail_base_addr_hi, offset * sizeof(uint32_t));

      nir_dma_st_pco(false, avail_addr, ~0U);
   }
}

KERNEL(32)
cs_query_copy_common(void)
{
   uint index_count = nir_load_preamble(1, PVR_QUERY_COPY_DATA_INDEX_COUNT, 0);

   uint dest_base_addr_lo = nir_load_preamble(1, PVR_QUERY_COPY_DATA_DEST_BO_LO, 0);
   uint dest_base_addr_hi = nir_load_preamble(1, PVR_QUERY_COPY_DATA_DEST_BO_HI, 0);

   uint avail_base_addr_lo = nir_load_preamble(1, PVR_QUERY_COPY_DATA_AVAILABILITY_BO_LO, 0);
   uint avail_base_addr_hi = nir_load_preamble(1, PVR_QUERY_COPY_DATA_AVAILABILITY_BO_HI, 0);

   uint result_base_addr_lo = nir_load_preamble(1, PVR_QUERY_COPY_DATA_RESULT_BO_LO, 0);
   uint result_base_addr_hi = nir_load_preamble(1, PVR_QUERY_COPY_DATA_RESULT_BO_HI, 0);

   uint dest_stride = nir_load_preamble(1, PVR_QUERY_COPY_DATA_DEST_STRIDE, 0);

   uint flags = nir_load_preamble(1, PVR_QUERY_COPY_DATA_FLAGS, 0);

   uint index = query_calc_global_id();

   if (index < index_count) {
      uint2 avail_addr = nir_uadd64_32(avail_base_addr_lo, avail_base_addr_hi, index * sizeof(uint32_t));
      uint available = nir_dma_ld_pco(1, avail_addr);

      uint2 dest_addr = nir_umad64_32(dest_stride, index, dest_base_addr_lo, dest_base_addr_hi);

      if (available || (flags & VK_QUERY_RESULT_PARTIAL_BIT)) {
         uint2 result_addr = nir_uadd64_32(result_base_addr_lo, result_base_addr_hi, index * sizeof(uint32_t));
         uint result = nir_dma_ld_pco(1, result_addr);

         /* TODO: for 64/32-bit writes, just prep the 64-bit one and set the burst-length variably. */
         if (flags & VK_QUERY_RESULT_64_BIT) {
            /* TODO: check if data should be (result, 0) or (0, result) */
            nir_dma_st_pco(false, dest_addr, result, 0);
         } else {
            nir_dma_st_pco(false, dest_addr, result);
         }
      }

      if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
         if (flags & VK_QUERY_RESULT_64_BIT) {
            dest_addr = nir_uadd64_32(dest_addr.x, dest_addr.y, sizeof(uint64_t));
            /* TODO: check if data should be (available, 0) or (0, available) */
            nir_dma_st_pco(false, dest_addr, available, 0);
         } else {
            dest_addr = nir_uadd64_32(dest_addr.x, dest_addr.y, sizeof(uint32_t));
            nir_dma_st_pco(false, dest_addr, available);
         }
      }
   }
}

KERNEL(32)
cs_query_reset_common(void)
{
   uint index_count = nir_load_preamble(1, PVR_QUERY_RESET_DATA_INDEX_COUNT, 0);

   uint result_base_addr_lo = nir_load_preamble(1, PVR_QUERY_RESET_DATA_RESULT_BO_LO, 0);
   uint result_base_addr_hi = nir_load_preamble(1, PVR_QUERY_RESET_DATA_RESULT_BO_HI, 0);

   uint avail_base_addr_lo = nir_load_preamble(1, PVR_QUERY_RESET_DATA_AVAILABILITY_BO_LO, 0);
   uint avail_base_addr_hi = nir_load_preamble(1, PVR_QUERY_RESET_DATA_AVAILABILITY_BO_HI, 0);

   uint index = query_calc_global_id();

   if (index < index_count) {
      uint2 result_addr = nir_uadd64_32(result_base_addr_lo, result_base_addr_hi, index * sizeof(uint32_t));
      nir_dma_st_pco(false, result_addr, 0);

      uint2 avail_addr = nir_uadd64_32(avail_base_addr_lo, avail_base_addr_hi, index * sizeof(uint32_t));
      nir_dma_st_pco(false, avail_addr, 0);
   }
}

static inline void
load_store_sr(uint block_size_dw,
                     bool store,
                     bool use_loaded_size,
                     bool use_temp_storage)
{
   /* static_assert(!use_temp_storage || (!store && block_size_dw == 16), "Invalid arguments."); */

   uint size_addr_lo = nir_load_vtxin_pco(1, PVR_LOAD_STORE_SR_DATA_SIZE_ADDR_LO);
   uint size_addr_hi = nir_load_vtxin_pco(1, PVR_LOAD_STORE_SR_DATA_SIZE_ADDR_HI);
   uint2 size_addr = (uint2)(size_addr_lo, size_addr_hi);

   /* Load the allocation size in 16 dword blocks. */
   uint alloc_size_16dw = !store && use_loaded_size ?
                             nir_dma_ld_pco(1, size_addr) :
                             nir_load_shared_reg_alloc_size_pco();

   /* Store the allocation size in 16 dword blocks. */
   if (store)
      nir_dma_st_pco(true, size_addr, alloc_size_16dw);

   if (alloc_size_16dw) {
      uint store_addr_lo = nir_load_vtxin_pco(1, PVR_LOAD_STORE_SR_DATA_STORE_ADDR_LO);
      uint store_addr_hi = nir_load_vtxin_pco(1, PVR_LOAD_STORE_SR_DATA_STORE_ADDR_HI);
      uint2 store_addr = (uint2)(store_addr_lo, store_addr_hi);

      /* TODO: if multicore, offset store_addr accordingly. */

      uint size_remaining_dw = alloc_size_16dw * 16;
      uint offset = 0;

      while (size_remaining_dw) {
         uint burst_len = size_remaining_dw;
         if (size_remaining_dw > block_size_dw)
            burst_len = block_size_dw == 1024 ? 0 : block_size_dw;

         if (store) {
            nir_dma_st_shregs_pco(store_addr, burst_len, offset, true);
         } else if (use_temp_storage) {
            uint16 temp = nir_dma_ld_pco(16, store_addr);
            for (unsigned u = 0; u < 16; ++u)
               nir_store_preamble_dynamic(temp[u], offset + u, 0);
         } else {
            nir_dma_ld_shregs_pco(store_addr, burst_len, offset);
         }

         offset += block_size_dw;
         size_remaining_dw -= block_size_dw;
         store_addr = nir_uadd64_32(store_addr.x, store_addr.y, block_size_dw * sizeof(uint32_t));
      }
   }
}

KERNEL(1)
cs_store_sr_1024_common(void)
{
   load_store_sr(1024, true, false, false);
}

KERNEL(1)
cs_load_sr_256_common(void)
{
   load_store_sr(256, false, true, false);
}

KERNEL(1)
cs_load_sr_16_common(void)
{
   load_store_sr(16, false, false, true);
}

KERNEL(1)
cs_idfwdf_common(void)
{
   /* Do a memory store and a texture read to fence any loads/texture writes from previous kernels */

   uint addr_lo = nir_load_preamble(1, PVR_IDFWDF_DATA_ADDR_LO, 0);
   uint addr_hi = nir_load_preamble(1, PVR_IDFWDF_DATA_ADDR_HI, 0);
   uint2 addr = (uint2)(addr_lo, addr_hi);

   nir_dma_st_pco(true, addr, 0U);

   uint16 smp_data = (uint16)(0);
   uint4 tex_state = nir_load_preamble(4, PVR_IDFWDF_DATA_TEX, 0);
   uint4 smp_state = nir_load_preamble(4, PVR_IDFWDF_DATA_SMP, 0);

   /* TODO: improve flag/range emission. */
   #define FLAGS 0xc9 /* 2d pplod fcnorm replace */
   #define RANGE 3    /* data comps: x, y, lod */

   #pragma unroll
   for (unsigned y = 0; y < PVR_IDFWDF_TEX_HEIGHT; ++y) {
      #pragma unroll
      for (unsigned x = 0; x < PVR_IDFWDF_TEX_WIDTH; ++x) {
         smp_data.xy = (uint2)(x, y);
         nir_smp_pco(smp_data, tex_state, smp_state, FLAGS, RANGE);
      }
   }
}
