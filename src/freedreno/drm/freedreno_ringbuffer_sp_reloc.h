/*
 * Copyright © 2021 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifdef X
#undef X
#endif

#if PTRSZ == 32
#define X(n) n##_32
#else
#define X(n) n##_64
#endif

static void X(emit_reloc_common)(struct fd_ringbuffer *ring, uint64_t iova)
{
#if PTRSZ == 64
   uint64_t *p64 = (uint64_t *)ring->cur;
   *p64 = iova;
   ring->cur += 2;
#else
   (*ring->cur++) = (uint32_t)iova;
#endif
}

static void X(fd_ringbuffer_sp_emit_reloc_nonobj)(struct fd_ringbuffer *ring,
                                                  const struct fd_reloc *reloc)
{
   X(emit_reloc_common)(ring, reloc->iova);
   fd_ringbuffer_sp_attach_bo_nonobj(ring, reloc->bo);
}

static void X(fd_ringbuffer_sp_emit_reloc_obj)(struct fd_ringbuffer *ring,
                                               const struct fd_reloc *reloc)
{
   X(emit_reloc_common)(ring, reloc->iova);
   fd_ringbuffer_sp_attach_bo_obj(ring, reloc->bo);
}

static uint32_t X(fd_ringbuffer_sp_emit_reloc_ring_nonobj)(
   struct fd_ringbuffer *ring, struct fd_ringbuffer *target, uint32_t cmd_idx)
{
   uint64_t iova;
   uint32_t size;

   size = fd_ringbuffer_sp_attach_ring_nonobj(ring, target, cmd_idx, &iova);
   X(emit_reloc_common)(ring, iova);

   return size;
}

static uint32_t X(fd_ringbuffer_sp_emit_reloc_ring_obj)(
   struct fd_ringbuffer *ring, struct fd_ringbuffer *target, uint32_t cmd_idx)
{
   uint64_t iova;
   uint32_t size;

   size = fd_ringbuffer_sp_attach_ring_obj(ring, target, cmd_idx, &iova);
   X(emit_reloc_common)(ring, iova);

   return size;
}
