/*
 * Copyright 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2024 Valve Corporation
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */
#include "compiler/libcl/libcl_vk.h"

#include "kk_query.h"

static inline global uint64_t *
query_report(global uint64_t *results, global uint16_t *oq_index,
             uint reports_per_query, uint query)
{
   /* For occlusion queries, results[] points to the device global heap. We
    * need to remap indices according to the query pool's allocation.
    */
   uint result_index = oq_index ? oq_index[query] : query;

   return results + (result_index * reports_per_query);
}

/**
 * Goes through a series of consecutive query indices in the given pool,
 * setting all element values to 0 and emitting them as available.
 */
KERNEL(1)
libkk_reset_query(global uint32_t *availability, global uint64_t *results,
                  global uint16_t *oq_index, uint32_t first_query,
                  uint16_t reports_per_query, int set_available)
{
   uint32_t query = first_query + cl_global_id.x;

   uint64_t value = 0;
   if (availability) {
      availability[query] = set_available;
   } else {
      value = set_available ? 0 : UINT64_MAX;
   }

   global uint64_t *report =
      query_report(results, oq_index, reports_per_query, query);

   /* XXX: is this supposed to happen on the begin? */
   for (unsigned j = 0; j < reports_per_query; ++j) {
      report[j] = value;
   }
}

KERNEL(1)
libkk_write_u32_array(global struct libkk_imm_write *write_array)
{
   uint id = cl_global_id.x;
   *(write_array[id].address) = write_array[id].value;
}

KERNEL(1)
libkk_copy_queries(global uint32_t *availability, global uint64_t *results,
                   global uint16_t *oq_index, uint64_t dst_addr,
                   uint64_t dst_stride, uint32_t first_query,
                   VkQueryResultFlagBits flags, uint16_t reports_per_query)
{
   uint index = cl_group_id.x;
   uint64_t dst = dst_addr + (((uint64_t)index) * dst_stride);
   uint32_t query = first_query + index;

   bool available;
   if (availability)
      available = availability[query];
   else
      available = (results[query] != LIBKK_QUERY_UNAVAILABLE);

   if (available || (flags & VK_QUERY_RESULT_PARTIAL_BIT)) {
      /* For occlusion queries, results[] points to the device global heap. We
       * need to remap indices according to the query pool's allocation.
       */
      uint result_index = oq_index ? oq_index[query] : query;
      uint idx = result_index * reports_per_query;

      for (unsigned i = 0; i < reports_per_query; ++i) {
         vk_write_query(dst, i, flags, results[idx + i]);
      }
   }

   if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
      vk_write_query(dst, reports_per_query, flags, available);
   }
}
