/*
 * Copyright 2026 Google LLC
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */
#include "compiler/libcl/libcl.h"
#include "libpan_copy.h"

#if PAN_ARCH >= 6
KERNEL(32)
panlib_fill(global uint32_t *address, uint32_t value)
{
   address[cl_global_id.x] = value;
}

KERNEL(32)
panlib_fill_uint4(global uint4 *address, uint a, uint b, uint c, uint d)
{
   address[cl_global_id.x] = (uint4)(a, b, c, d);
}

KERNEL(1)
panlib_fill_scalar(global uint32_t *address, uint32_t value)
{
   address[cl_global_id.x] = value;
}

KERNEL(1)
panlib_fill_uint4_scalar(global uint4 *address, uint a, uint b, uint c, uint d)
{
   address[cl_global_id.x] = (uint4)(a, b, c, d);
}

/* Shared body for the direct and indirect buffer->buffer copy kernels. Copies
 * the bulk of the buffer with 16-byte (uint4) chunks and the remaining up-to-3
 * words with 4-byte accesses. Each thread strides over the buffer so a capped
 * workgroup count still covers the whole copy.
 *
 * src, dst and size must be 4-byte aligned, a trailing partial word would be
 * dropped. The uint4 accesses inherit that 4-byte alignment, they are not
 * 16-byte aligned. The backend splits them where the hardware needs a wider
 * alignment.
 */
static inline void
panlib_copy_mem_body(uintptr_t dst, uintptr_t src, uint64_t size)
{
   uint64_t nthreads = (uint64_t)cl_num_groups.x * PANLIB_COPY_MEM_WG_SIZE;

   uint64_t chunks = size / PANLIB_COPY_MEM_CHUNK_SIZE;
   for (uint64_t i = cl_global_id.x; i < chunks; i += nthreads) {
      uint64_t o = i * PANLIB_COPY_MEM_CHUNK_SIZE;
      uint4 data = vload4(0, (global uint32_t *)(src + o));
      vstore4(data, 0, (global uint32_t *)(dst + o));
   }

   /* Remaining 1-3 words, one 32-bit word per thread. */
   uint64_t tail_base = chunks * PANLIB_COPY_MEM_CHUNK_SIZE;
   uint64_t words = (size - tail_base) / 4;
   for (uint64_t i = cl_global_id.x; i < words; i += nthreads) {
      uint64_t o = tail_base + i * 4;
      *(global uint32_t *)(dst + o) = *(global uint32_t *)(src + o);
   }
}

KERNEL(PANLIB_COPY_MEM_WG_SIZE)
panlib_copy_mem(global uchar *dst, global const uchar *src, uint64_t size)
{
   panlib_copy_mem_body((uintptr_t)dst, (uintptr_t)src, size);
}

#if PAN_ARCH >= 10
/* The workgroup count is derived from the copy size on the command stream
 * and may be capped, so each thread loops until the whole size is covered.
 */
KERNEL(PANLIB_COPY_MEM_WG_SIZE)
panlib_copy_mem_indirect(global const uint32_t *cmd)
{
   /* VkCopyMemoryIndirectCommandKHR is only guaranteed to be 4-byte
    * aligned, read it as 32-bit words.
    */
   uintptr_t src = upsample(cmd[1], cmd[0]);
   uintptr_t dst = upsample(cmd[3], cmd[2]);
   uint64_t size = upsample(cmd[5], cmd[4]);

   panlib_copy_mem_body(dst, src, size);
}
#endif
#endif
