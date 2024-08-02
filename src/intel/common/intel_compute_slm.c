/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "intel_compute_slm.h"

#include <assert.h>

#include "util/macros.h"
#include "util/u_math.h"

struct slm_encode {
  uint32_t encode;
  uint32_t size_in_kb;
};

static inline struct slm_encode *
slm_encode_lookup(struct slm_encode *table, unsigned int table_len, uint32_t bytes)
{
   const uint32_t kbytes = DIV_ROUND_UP(bytes, 1024);
   unsigned int i;

   assert(kbytes <= table[table_len - 1].size_in_kb);
   for (i = 0; i < table_len; i++) {
      if (table[i].size_in_kb >= kbytes)
         return &table[i];
   }

   return &table[table_len - 1];
}

static struct slm_encode xe2_slm_allocation_size_table[] = {
  { .encode = 0x0, .size_in_kb = 0, },
  { .encode = 0x1, .size_in_kb = 1, },
  { .encode = 0x2, .size_in_kb = 2, },
  { .encode = 0x3, .size_in_kb = 4, },
  { .encode = 0x4, .size_in_kb = 8, },
  { .encode = 0x5, .size_in_kb = 16, },
  { .encode = 0x8, .size_in_kb = 24, },
  { .encode = 0x6, .size_in_kb = 32, },
  { .encode = 0x9, .size_in_kb = 48, },
  { .encode = 0x7, .size_in_kb = 64, },
  { .encode = 0xA, .size_in_kb = 96, },
  { .encode = 0xB, .size_in_kb = 128, },
  { .encode = 0xC, .size_in_kb = 192, },
  { .encode = 0xD, .size_in_kb = 256, },
  { .encode = 0xE, .size_in_kb = 384, },
};

/* Shared Local Memory Size is specified as powers of two,
 * and also have a Gen-dependent minimum value if not zero.
 */
uint32_t
intel_compute_slm_calculate_size(unsigned gen, uint32_t bytes)
{
   if (gen >= 20) {
      struct slm_encode *slm_encode;

      slm_encode = slm_encode_lookup(xe2_slm_allocation_size_table,
                                     ARRAY_SIZE(xe2_slm_allocation_size_table),
                                     bytes);
      return slm_encode->size_in_kb * 1024;
   }

   assert(bytes <= 64 * 1024);
   if (bytes > 0)
      return MAX2(util_next_power_of_two(bytes), gen >= 9 ? 1024 : 4096);
   else
      return 0;
}

uint32_t
intel_compute_slm_encode_size(unsigned gen, uint32_t bytes)
{
   uint32_t slm_size;

   if (bytes == 0)
      return 0;

   if (gen >= 20) {
      struct slm_encode *slm_encode;

      slm_encode = slm_encode_lookup(xe2_slm_allocation_size_table,
                                     ARRAY_SIZE(xe2_slm_allocation_size_table),
                                     bytes);
      return slm_encode->encode;
   }

   /* Shared Local Memory is specified as powers of two, and encoded in
    * INTERFACE_DESCRIPTOR_DATA with the following representations:
    *
    * Size   | 0 kB | 1 kB | 2 kB | 4 kB | 8 kB | 16 kB | 32 kB | 64 kB |
    * -------------------------------------------------------------------
    * Gfx7-8 |    0 | none | none |    1 |    2 |     4 |     8 |    16 |
    * -------------------------------------------------------------------
    * Gfx9+  |    0 |    1 |    2 |    3 |    4 |     5 |     6 |     7 |
    */

   slm_size = intel_compute_slm_calculate_size(gen, bytes);
   assert(util_is_power_of_two_nonzero(slm_size));

   if (gen >= 9) {
      /* Turn an exponent of 10 (1024 kB) into 1. */
      assert(slm_size >= 1024);
      slm_size = ffs(slm_size) - 10;
   } else {
      assert(slm_size >= 4096);
      /* Convert to the pre-Gfx9 representation. */
      slm_size = slm_size / 4096;
   }

   return slm_size;
}

/* encode = 0 sets to largest SLM size supported in subslice */
static struct slm_encode preferred_slm_allocation_size_table[] = {
   { .encode = 0x8, .size_in_kb = 0, },
   { .encode = 0x9, .size_in_kb = 16, },
   { .encode = 0xa, .size_in_kb = 32, },
   { .encode = 0xb, .size_in_kb = 64, },
   { .encode = 0xc, .size_in_kb = 96, },
   { .encode = 0xd, .size_in_kb = 128, },
};

static struct slm_encode xe2_preferred_slm_allocation_size_table[] = {
  { .encode = 0x0, .size_in_kb = 0, },
  { .encode = 0x1, .size_in_kb = 16, },
  { .encode = 0x2, .size_in_kb = 32, },
  { .encode = 0x3, .size_in_kb = 64, },
  { .encode = 0x4, .size_in_kb = 96, },
  { .encode = 0x5, .size_in_kb = 128, },
  { .encode = 0x6, .size_in_kb = 160, },
  { .encode = 0x7, .size_in_kb = 192, },
  { .encode = 0x8, .size_in_kb = 224, },
  { .encode = 0x9, .size_in_kb = 256, },
  { .encode = 0xA, .size_in_kb = 384, },
};

static uint32_t
intel_compute_preferred_slm_encode_size(unsigned gen, uint32_t bytes)
{
   struct slm_encode *table;
   unsigned int table_len;

   if (gen >= 20) {
      table = xe2_preferred_slm_allocation_size_table;
      table_len = ARRAY_SIZE(xe2_preferred_slm_allocation_size_table);
   } else {
      table = preferred_slm_allocation_size_table;
      table_len = ARRAY_SIZE(preferred_slm_allocation_size_table);
   }

   return slm_encode_lookup(table, table_len, bytes)->encode;
}

/**
 * Compute a shared local memory size to be allocated for each sub-slice.
 * It estimate how many workgroups will run concurrently per sub-slice and
 * multiply that per each workgroup SLM size.
 */
uint32_t
intel_compute_preferred_slm_calc_encode_size(const struct intel_device_info *devinfo,
                                             const uint32_t slm_size_per_workgroup,
                                             const uint32_t invocations_per_workgroup,
                                             const uint8_t cs_simd)
{
   const uint32_t max_preferred_slm_size = intel_device_info_get_max_preferred_slm_size(devinfo);
   const uint32_t invocations_per_ss = intel_device_info_get_eu_count_first_subslice(devinfo) *
                                       devinfo->num_thread_per_eu * cs_simd;
   uint32_t preferred_slm_size;

   if (slm_size_per_workgroup) {
      uint32_t workgroups_per_ss = invocations_per_ss / invocations_per_workgroup;

      preferred_slm_size = workgroups_per_ss * slm_size_per_workgroup;
      preferred_slm_size = MIN2(preferred_slm_size, max_preferred_slm_size);
   } else {
      preferred_slm_size = 0;
   }

   assert(preferred_slm_size >= slm_size_per_workgroup);
   return intel_compute_preferred_slm_encode_size(devinfo->ver, preferred_slm_size);
}
