/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "libintel_shaders.h"

/* Memcpy data using multiple lanes. */
void genX(libanv_memcpy)(global void *dst_base,
                         global void *src_base,
                         uint num_dwords,
                         uint dword_offset)
{
   global void *dst = dst_base + 4 * dword_offset;
   global void *src = src_base + 4 * dword_offset;

   if (dword_offset + 4 <= num_dwords) {
      *(global uint4 *)(dst) = *(global uint4 *)(src);
   } else if (dword_offset + 3 <= num_dwords) {
      *(global uint3 *)(dst) = *(global uint3 *)(src);
   } else if (dword_offset + 2 <= num_dwords) {
      *(global uint2 *)(dst) = *(global uint2 *)(src);
   } else if (dword_offset + 1 <= num_dwords) {
      *(global uint *)(dst) = *(global uint *)(src);
   }
}

/* Copy size from src_ptr to dst_ptr for using a single lane with size
 * multiple of 4.
 */
void genX(copy_data)(global void *dst_ptr,
                     global void *src_ptr,
                     uint32_t size)
{
   for (uint32_t offset = 0; offset < size; offset += 16) {
      if (offset + 16 <= size) {
         *(global uint4 *)(dst_ptr + offset) = *(global uint4 *)(src_ptr + offset);
      } else if (offset + 12 <= size) {
         *(global uint3 *)(dst_ptr + offset) = *(global uint3 *)(src_ptr + offset);
      } else if (offset + 8 <= size) {
         *(global uint2 *)(dst_ptr + offset) = *(global uint2 *)(src_ptr + offset);
      } else if (offset + 4 <= size) {
         *(global uint *)(dst_ptr + offset) = *(global uint *)(src_ptr + offset);
      }
   }
}
