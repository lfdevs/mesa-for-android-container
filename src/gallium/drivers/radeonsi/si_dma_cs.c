/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "si_pipe.h"
#include "sid.h"

static void si_dma_emit_wait_idle(struct si_context *sctx)
{
   struct radeon_cmdbuf *cs = sctx->sdma_cs;

   /* NOP waits for idle. */
   if (sctx->chip_class >= GFX7)
      radeon_emit(cs, 0x00000000); /* NOP */
   else
      radeon_emit(cs, 0xf0000000); /* NOP */
}

void si_dma_emit_timestamp(struct si_context *sctx, struct si_resource *dst, uint64_t offset)
{
   struct radeon_cmdbuf *cs = sctx->sdma_cs;
   uint64_t va = dst->gpu_address + offset;

   if (sctx->chip_class == GFX6) {
      unreachable("SI DMA doesn't support the timestamp packet.");
      return;
   }

   /* Mark the buffer range of destination as valid (initialized),
    * so that transfer_map knows it should wait for the GPU when mapping
    * that range. */
   util_range_add(&dst->b.b, &dst->valid_buffer_range, offset, offset + 8);

   assert(va % 8 == 0);

   si_need_dma_space(sctx, 4, dst, NULL);
   si_dma_emit_wait_idle(sctx);

   radeon_emit(
      cs, CIK_SDMA_PACKET(CIK_SDMA_OPCODE_TIMESTAMP, SDMA_TS_SUB_OPCODE_GET_GLOBAL_TIMESTAMP, 0));
   radeon_emit(cs, va);
   radeon_emit(cs, va >> 32);
}

void si_sdma_clear_buffer(struct si_context *sctx, struct pipe_resource *dst, uint64_t offset,
                          uint64_t size, unsigned clear_value)
{
   struct radeon_cmdbuf *cs = sctx->sdma_cs;
   unsigned i, ncopy, csize;
   struct si_resource *sdst = si_resource(dst);

   assert(offset % 4 == 0);
   assert(size);
   assert(size % 4 == 0);

   if (!cs || dst->flags & PIPE_RESOURCE_FLAG_SPARSE ||
       sctx->screen->debug_flags & DBG(NO_SDMA_CLEARS) ||
       unlikely(radeon_uses_secure_bos(sctx->ws))) {
      sctx->b.clear_buffer(&sctx->b, dst, offset, size, &clear_value, 4);
      return;
   }

   /* Mark the buffer range of destination as valid (initialized),
    * so that transfer_map knows it should wait for the GPU when mapping
    * that range. */
   util_range_add(dst, &sdst->valid_buffer_range, offset, offset + size);

   offset += sdst->gpu_address;

   if (sctx->chip_class == GFX6) {
      /* the same maximum size as for copying */
      ncopy = DIV_ROUND_UP(size, SI_DMA_COPY_MAX_DWORD_ALIGNED_SIZE);
      si_need_dma_space(sctx, ncopy * 4, sdst, NULL);

      for (i = 0; i < ncopy; i++) {
         csize = MIN2(size, SI_DMA_COPY_MAX_DWORD_ALIGNED_SIZE);
         radeon_emit(cs, SI_DMA_PACKET(SI_DMA_PACKET_CONSTANT_FILL, 0, csize / 4));
         radeon_emit(cs, offset);
         radeon_emit(cs, clear_value);
         radeon_emit(cs, (offset >> 32) << 16);
         offset += csize;
         size -= csize;
      }
      return;
   }

   /* The following code is for CI and later. */
   /* the same maximum size as for copying */
   unsigned max_size_per_packet = sctx->chip_class >= GFX10_3 ?
                                     GFX103_SDMA_COPY_MAX_SIZE :
                                     CIK_SDMA_COPY_MAX_SIZE;
   ncopy = DIV_ROUND_UP(size, max_size_per_packet);
   si_need_dma_space(sctx, ncopy * 5, sdst, NULL);

   for (i = 0; i < ncopy; i++) {
      csize = MIN2(size, max_size_per_packet);
      radeon_emit(cs, CIK_SDMA_PACKET(CIK_SDMA_PACKET_CONSTANT_FILL, 0, 0x8000 /* dword copy */));
      radeon_emit(cs, offset);
      radeon_emit(cs, offset >> 32);
      radeon_emit(cs, clear_value);
      /* dw count */
      radeon_emit(cs, (sctx->chip_class >= GFX9 ? csize - 1 : csize) & 0xfffffffc);
      offset += csize;
      size -= csize;
   }
}

void si_sdma_copy_buffer(struct si_context *sctx, struct pipe_resource *dst,
                         struct pipe_resource *src, uint64_t dst_offset, uint64_t src_offset,
                         uint64_t size)
{
   struct radeon_cmdbuf *cs = sctx->sdma_cs;
   unsigned i, ncopy, csize;
   struct si_resource *sdst = si_resource(dst);
   struct si_resource *ssrc = si_resource(src);

   if (!cs || dst->flags & PIPE_RESOURCE_FLAG_SPARSE || src->flags & PIPE_RESOURCE_FLAG_SPARSE ||
       (ssrc->flags & RADEON_FLAG_ENCRYPTED) != (sdst->flags & RADEON_FLAG_ENCRYPTED)) {
      si_copy_buffer(sctx, dst, src, dst_offset, src_offset, size);
      return;
   }

   /* Mark the buffer range of destination as valid (initialized),
    * so that transfer_map knows it should wait for the GPU when mapping
    * that range. */
   util_range_add(dst, &sdst->valid_buffer_range, dst_offset, dst_offset + size);

   dst_offset += sdst->gpu_address;
   src_offset += ssrc->gpu_address;

   if (sctx->chip_class == GFX6) {
      unsigned max_size, sub_cmd, shift;

      /* see whether we should use the dword-aligned or byte-aligned copy */
      if (!(dst_offset % 4) && !(src_offset % 4) && !(size % 4)) {
         sub_cmd = SI_DMA_COPY_DWORD_ALIGNED;
         shift = 2;
         max_size = SI_DMA_COPY_MAX_DWORD_ALIGNED_SIZE;
      } else {
         sub_cmd = SI_DMA_COPY_BYTE_ALIGNED;
         shift = 0;
         max_size = SI_DMA_COPY_MAX_BYTE_ALIGNED_SIZE;
      }

      ncopy = DIV_ROUND_UP(size, max_size);
      si_need_dma_space(sctx, ncopy * 5, sdst, ssrc);

      for (i = 0; i < ncopy; i++) {
         csize = MIN2(size, max_size);
         radeon_emit(cs, SI_DMA_PACKET(SI_DMA_PACKET_COPY, sub_cmd, csize >> shift));
         radeon_emit(cs, dst_offset);
         radeon_emit(cs, src_offset);
         radeon_emit(cs, (dst_offset >> 32UL) & 0xff);
         radeon_emit(cs, (src_offset >> 32UL) & 0xff);
         dst_offset += csize;
         src_offset += csize;
         size -= csize;
      }
      return;
   }

   /* The following code is for CI and later. */
   unsigned max_size_per_packet = sctx->chip_class >= GFX10_3 ?
                                     GFX103_SDMA_COPY_MAX_SIZE :
                                     CIK_SDMA_COPY_MAX_SIZE;
   unsigned align = ~0u;
   ncopy = DIV_ROUND_UP(size, max_size_per_packet);

   /* Align copy size to dw if src/dst address are dw aligned */
   if ((src_offset & 0x3) == 0 && (dst_offset & 0x3) == 0 && size > 4 && (size & 3) != 0) {
      align = ~0x3u;
      ncopy++;
   }

   si_need_dma_space(sctx, ncopy * 7, sdst, ssrc);

   for (i = 0; i < ncopy; i++) {
      csize = size >= 4 ? MIN2(size & align, max_size_per_packet) : size;
      radeon_emit(cs, CIK_SDMA_PACKET(CIK_SDMA_OPCODE_COPY, CIK_SDMA_COPY_SUB_OPCODE_LINEAR,
                                      (sctx->ws->cs_is_secure(cs) ? 1u : 0) << 2));
      radeon_emit(cs, sctx->chip_class >= GFX9 ? csize - 1 : csize);
      radeon_emit(cs, 0); /* src/dst endian swap */
      radeon_emit(cs, src_offset);
      radeon_emit(cs, src_offset >> 32);
      radeon_emit(cs, dst_offset);
      radeon_emit(cs, dst_offset >> 32);
      dst_offset += csize;
      src_offset += csize;
      size -= csize;
   }
}

void si_need_dma_space(struct si_context *ctx, unsigned num_dw, struct si_resource *dst,
                       struct si_resource *src)
{
   struct radeon_winsys *ws = ctx->ws;
   uint64_t vram = ctx->sdma_cs->used_vram;
   uint64_t gtt = ctx->sdma_cs->used_gart;

   if (dst) {
      vram += dst->vram_usage;
      gtt += dst->gart_usage;
   }
   if (src) {
      vram += src->vram_usage;
      gtt += src->gart_usage;
   }

   /* Flush the GFX IB if DMA depends on it. */
   if (!ctx->sdma_uploads_in_progress && radeon_emitted(ctx->gfx_cs, ctx->initial_gfx_cs_size) &&
       ((dst && ws->cs_is_buffer_referenced(ctx->gfx_cs, dst->buf, RADEON_USAGE_READWRITE)) ||
        (src && ws->cs_is_buffer_referenced(ctx->gfx_cs, src->buf, RADEON_USAGE_WRITE))))
      si_flush_gfx_cs(ctx, RADEON_FLUSH_ASYNC_START_NEXT_GFX_IB_NOW, NULL);

   bool use_secure_cmd = false;
   if (unlikely(radeon_uses_secure_bos(ctx->ws))) {
      if (src && src->flags & RADEON_FLAG_ENCRYPTED) {
         assert(!dst || (dst->flags & RADEON_FLAG_ENCRYPTED));
         use_secure_cmd = true;
      } else if (dst && (dst->flags & RADEON_FLAG_ENCRYPTED)) {
         use_secure_cmd = true;
      }
   }

   /* Flush if there's not enough space, or if the memory usage per IB
    * is too large.
    *
    * IBs using too little memory are limited by the IB submission overhead.
    * IBs using too much memory are limited by the kernel/TTM overhead.
    * Too long IBs create CPU-GPU pipeline bubbles and add latency.
    *
    * This heuristic makes sure that DMA requests are executed
    * very soon after the call is made and lowers memory usage.
    * It improves texture upload performance by keeping the DMA
    * engine busy while uploads are being submitted.
    */
   num_dw++; /* for emit_wait_idle below */
   if (!ctx->sdma_uploads_in_progress &&
       (use_secure_cmd != ctx->ws->cs_is_secure(ctx->sdma_cs) ||
        !ws->cs_check_space(ctx->sdma_cs, num_dw, false) ||
        ctx->sdma_cs->used_vram + ctx->sdma_cs->used_gart > 64 * 1024 * 1024 ||
        !radeon_cs_memory_below_limit(ctx->screen, ctx->sdma_cs, vram, gtt))) {
      si_flush_dma_cs(ctx, PIPE_FLUSH_ASYNC | RADEON_FLUSH_TOGGLE_SECURE_SUBMISSION, NULL);
      assert(ctx->ws->cs_is_secure(ctx->sdma_cs) == use_secure_cmd);
      assert((num_dw + ctx->sdma_cs->current.cdw) <= ctx->sdma_cs->current.max_dw);
   }

   /* Wait for idle if either buffer has been used in the IB before to
    * prevent read-after-write hazards.
    */
   if ((dst && ws->cs_is_buffer_referenced(ctx->sdma_cs, dst->buf, RADEON_USAGE_READWRITE)) ||
       (src && ws->cs_is_buffer_referenced(ctx->sdma_cs, src->buf, RADEON_USAGE_WRITE)))
      si_dma_emit_wait_idle(ctx);

   unsigned sync = ctx->sdma_uploads_in_progress ? 0 : RADEON_USAGE_SYNCHRONIZED;
   if (dst) {
      ws->cs_add_buffer(ctx->sdma_cs, dst->buf, RADEON_USAGE_WRITE | sync, dst->domains, 0);
   }
   if (src) {
      ws->cs_add_buffer(ctx->sdma_cs, src->buf, RADEON_USAGE_READ | sync, src->domains, 0);
   }

   /* this function is called before all DMA calls, so increment this. */
   ctx->num_dma_calls++;
}

void si_flush_dma_cs(struct si_context *ctx, unsigned flags, struct pipe_fence_handle **fence)
{
   struct radeon_cmdbuf *cs = ctx->sdma_cs;
   struct radeon_saved_cs saved;
   bool check_vm = (ctx->screen->debug_flags & DBG(CHECK_VM)) != 0;

   if (!radeon_emitted(cs, 0) &&
       !(flags & RADEON_FLUSH_TOGGLE_SECURE_SUBMISSION)) {
      if (fence)
         ctx->ws->fence_reference(fence, ctx->last_sdma_fence);
      return;
   }

   if (check_vm)
      si_save_cs(ctx->ws, cs, &saved, true);

   if (ctx->is_noop)
      flags |= RADEON_FLUSH_NOOP;

   ctx->ws->cs_flush(cs, flags, &ctx->last_sdma_fence);
   if (fence)
      ctx->ws->fence_reference(fence, ctx->last_sdma_fence);

   if (check_vm) {
      /* Use conservative timeout 800ms, after which we won't wait any
       * longer and assume the GPU is hung.
       */
      ctx->ws->fence_wait(ctx->ws, ctx->last_sdma_fence, 800 * 1000 * 1000);

      si_check_vm_faults(ctx, &saved, RING_DMA);
      si_clear_saved_cs(&saved);
   }
}

void si_screen_clear_buffer(struct si_screen *sscreen, struct pipe_resource *dst, uint64_t offset,
                            uint64_t size, unsigned value)
{
   struct si_context *ctx = (struct si_context *)sscreen->aux_context;

   simple_mtx_lock(&sscreen->aux_context_lock);
   si_sdma_clear_buffer(ctx, dst, offset, size, value);
   sscreen->aux_context->flush(sscreen->aux_context, NULL, 0);
   simple_mtx_unlock(&sscreen->aux_context_lock);
}
