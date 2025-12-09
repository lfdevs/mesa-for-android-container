/*
 * Copyright © 2025 Imagination Technologies Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef __pvr_address_type
#define __pvr_address_type uint64_t
#define __pvr_get_address(addr) addr
#define __pvr_make_address(addr) addr
#endif /* __pvr_address_type */

#include "csbgen/rogue/cdm.h"
#include "hwdef/rogue_hw_defs.h"
#include "libcl.h"

uint32_t
usclib_emu_ssbo_atomic_comp_swap(uint2 ssbo_buffer, uint ssbo_offset, uint compare, uint data)
{
   uint32_t result;

   nir_mutex_pco(PCO_MUTEX_ID_ATOMIC_EMU, PCO_MUTEX_OP_LOCK);
   for (uint u = 0; u < ROGUE_MAX_INSTANCES_PER_TASK; ++u) {
      if (u == nir_load_instance_num_pco()) {
         uint32_t pre_val = nir_load_ssbo(ssbo_buffer, ssbo_offset, ACCESS_COHERENT, 4, 0, 0);
         result = pre_val;

         uint32_t post_val = (pre_val == compare) ? data : pre_val;
         nir_store_ssbo(post_val, ssbo_buffer, ssbo_offset, 0x1, ACCESS_COHERENT, 4, 0, 0);
      }
   }
   nir_mutex_pco(PCO_MUTEX_ID_ATOMIC_EMU, PCO_MUTEX_OP_RELEASE);

   return result;
}

uint32_t
usclib_emu_global_atomic_comp_swap(uint32_t addr_lo, uint32_t addr_hi, uint compare, uint data)
{
   uint32_t result;

   nir_mutex_pco(PCO_MUTEX_ID_ATOMIC_EMU, PCO_MUTEX_OP_LOCK);
   for (uint u = 0; u < ROGUE_MAX_INSTANCES_PER_TASK; ++u) {
      if (u == nir_load_instance_num_pco()) {
         uint2 addr = (uint2)(addr_lo, addr_hi);
         uint32_t pre_val = nir_dma_ld_pco(1, addr);
         result = pre_val;

         uint32_t post_val = (pre_val == compare) ? data : pre_val;
         nir_dma_st_pco(false, addr, post_val);
      }
   }
   nir_mutex_pco(PCO_MUTEX_ID_ATOMIC_EMU, PCO_MUTEX_OP_RELEASE);

   return result;
}

void
usclib_barrier(uint num_slots, uint counter_offset)
{
   #define load_barrier_counter() nir_load_shared(counter_offset, 0, 0, 4, 0)
   #define store_barrier_counter(value) nir_store_shared(value, counter_offset, 0, 0, 0x1, 4, 0)

   bool is_inst_zero = !nir_load_instance_num_pco();

   nir_mutex_pco(PCO_MUTEX_ID_BARRIER, PCO_MUTEX_OP_LOCK);

   if (is_inst_zero)
      store_barrier_counter(load_barrier_counter() + 1);

   bool all_slots_done = load_barrier_counter() == num_slots;
   if (all_slots_done) {
      if (is_inst_zero)
         store_barrier_counter(0);
   } else {
      do {
         nir_mutex_pco(PCO_MUTEX_ID_BARRIER, PCO_MUTEX_OP_RELEASE_SLEEP);
         nir_mutex_pco(PCO_MUTEX_ID_BARRIER, PCO_MUTEX_OP_LOCK);
      } while (load_barrier_counter() != 0);
   }

   nir_mutex_pco(PCO_MUTEX_ID_BARRIER, PCO_MUTEX_OP_RELEASE_WAKEUP);
}

void
usclib_zero_init_wg_mem(uint count)
{
   for (unsigned u = 0; u < count; ++u)
      nir_store_shared(0, u * sizeof(uint32_t), 0, 0, 0x1, 4, 0);
}
