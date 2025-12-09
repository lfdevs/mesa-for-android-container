/*
 * Copyright 2024 Collabora Ltd.
 * Copyright 2025 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */
#include "compiler/libcl/libcl.h"
#include "compiler/libcl/libcl_vk.h"
#include "genxml/gen_macros.h"

#include "vulkan/panvk_cmd_ts.h"

#if (PAN_ARCH >= 10)
static inline uint64_t
compute_timestamp_query_result(global uint64_t *report_addr,
                               uint32_t report_count, uint32_t op,
                               uint32_t sq_mask, bool *available)
{
   uint64_t result = 0;

   /* Compute min/max and keep track of which streams had non-zero results. */
   switch (op) {
   case PANVK_QUERY_TS_OP_MIN: {
      result = ULONG_MAX;
      for (uint32_t i = 0; i < report_count - 1; ++i) {
         uint64_t r = report_addr[i];
         if (r != 0 && (sq_mask & (1 << i))) {
            result = min(result, report_addr[i]);
            sq_mask &= ~(1 << i);
         }
      }
      if (result == ULONG_MAX)
         result = 0;
      break;
   }
   case PANVK_QUERY_TS_OP_MAX: {
      for (uint32_t i = 0; i < report_count - 1; ++i) {
         uint64_t r = report_addr[i];
         if (r != 0 && (sq_mask & (1 << i))) {
            result = max(result, report_addr[i]);
            sq_mask &= ~(1 << i);
         }
      }
      break;
   }
   default:
      UNREACHABLE("Invalid timestamp op");
      break;
   }

   /* The result is available if all subqueues have written their value. */
   *available = sq_mask == 0;

   return result;
}

KERNEL(1)
panlib_copy_ts_query_result(uint64_t pool_addr, global uint32_t *available_addr,
                            uint32_t query_stride, uint32_t first_query,
                            uint32_t query_count, uint64_t dst_addr,
                            uint64_t dst_stride, uint32_t query_type,
                            uint32_t flags, uint32_t report_count)
{
   uint32_t i = cl_global_id.x;

   if (i >= query_count)
      return;

   uintptr_t dst = dst_addr + ((uint64_t)i * dst_stride);

   uint32_t query = first_query + i;
   global uint64_t *report_addr =
      (global uint64_t *)(pool_addr + ((uint64_t)query * query_stride));

   uint64_t result = 0;

   /* The last report is always metadata. */
   uint64_t info = report_addr[report_count - 1];
   uint32_t op = panvk_timestamp_info_get_op(info);
   uint32_t sq_mask = panvk_timestamp_info_get_sq_mask(info);

   bool available = false;
   /* If no subqueue should write a result, the query is uninitialized. */
   if (sq_mask != 0)
      result = compute_timestamp_query_result(report_addr, report_count, op,
                                              sq_mask, &available);

   if ((flags & VK_QUERY_RESULT_PARTIAL_BIT) || available)
      vk_write_query(dst, 0, flags, result);

   if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
      vk_write_query(dst, 1, flags, available);
}
#endif

#if (PAN_ARCH >= 6 && PAN_ARCH < 10)
static inline void
write_occlusion_query_result(uintptr_t dst_addr, int32_t idx, uint32_t flags,
                             global uint64_t *report_addr,
                             uint32_t report_count)
{
   uint64_t value = 0;

   for (uint32_t i = 0; i < report_count; i++)
      value += report_addr[i];

   vk_write_query(dst_addr, idx, flags, value);
}

KERNEL(1)
panlib_copy_query_result(uint64_t pool_addr, global uint32_t *available_addr,
                         uint32_t query_stride, uint32_t first_query,
                         uint32_t query_count, uint64_t dst_addr,
                         uint64_t dst_stride, uint32_t query_type,
                         uint32_t flags, uint32_t report_count)
{
   uint32_t i = cl_global_id.x;

   if (i >= query_count)
      return;

   uint32_t query = first_query + i;
   uintptr_t dst = dst_addr + ((uint64_t)i * dst_stride);
   global uint64_t *report_addr =
      (global uint64_t *)(pool_addr + ((uint64_t)query * query_stride));

   bool available = available_addr[query];

   if ((flags & VK_QUERY_RESULT_PARTIAL_BIT) || available) {
      switch (query_type) {
      case VK_QUERY_TYPE_OCCLUSION:
         write_occlusion_query_result(dst, 0, flags, report_addr, report_count);
         break;
      default:
         UNREACHABLE("Unsupported query type");
         break;
      }
   }

   if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
      vk_write_query(dst, 1, flags, available);
   }
}

KERNEL(1)
panlib_clear_query_result(uint64_t pool_addr, global uint32_t *available_addr,
                          uint32_t query_stride, uint32_t first_query,
                          uint32_t query_count, uint32_t report_count,
                          uint32_t availaible_value)
{
   uint32_t i = cl_global_id.x;

   if (i >= query_count)
      return;

   uint32_t query = first_query + i;
   global uint64_t *report_addr =
      (global uint64_t *)(pool_addr + ((uint64_t)query * query_stride));

   available_addr[query] = availaible_value;

   for (uint32_t i = 0; i < report_count; i++)
      report_addr[i] = 0;
}

#endif
