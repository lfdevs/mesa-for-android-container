/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "compiler/libcl/libcl_vk.h"
#include "nvk_query.h"

void
nvk_copy_queries(uint64_t pool_addr, uint available_stride,
                 uint reports_start, uint report_count, uint query_stride,
                 uint first_query, uint query_count,
                 uint64_t dst_addr, uint64_t dst_stride, uint flags)
{
   uint i = get_sub_group_local_id() + cl_group_id.x * 32;
   if (i >= query_count)
      return;

   uint query = first_query + i;
   uint64_t available_offs = (uint64_t)query * (uint64_t)available_stride;
   bool available = *(global uint *)(pool_addr + available_offs);
   bool write_results = available || (flags & VK_QUERY_RESULT_PARTIAL_BIT);

   uint64_t report_offs = reports_start + (uint64_t)query * (uint64_t)query_stride;
   global uint64_t *report = (global uint64_t *)(pool_addr + report_offs);

   uint64_t dst_offset = dst_stride * (uint64_t)i;

   for (uint r = 0; r < report_count; ++r) {
      vk_write_query(dst_addr + dst_offset, r, flags, report[r * 2]);
   }

   if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
      vk_write_query(dst_addr + dst_offset, report_count, flags, available);
   }
}
