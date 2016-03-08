/*
 * Copyright 2013 Nouveau Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Christoph Bumiller, Samuel Pitoiset
 */

#include "nvc0/nvc0_context.h"
#include "nvc0/nvc0_compute.h"

int
nvc0_screen_compute_setup(struct nvc0_screen *screen,
                          struct nouveau_pushbuf *push)
{
   struct nouveau_object *chan = screen->base.channel;
   struct nouveau_device *dev = screen->base.device;
   uint32_t obj_class;
   int ret;
   int i;

   switch (dev->chipset & ~0xf) {
   case 0xc0:
   case 0xd0:
      /* In theory, GF110+ should also support NVC8_COMPUTE_CLASS but,
       * in practice, a ILLEGAL_CLASS dmesg fail appears when using it. */
      obj_class = NVC0_COMPUTE_CLASS;
      break;
   default:
      NOUVEAU_ERR("unsupported chipset: NV%02x\n", dev->chipset);
      return -1;
   }

   ret = nouveau_object_new(chan, 0xbeef90c0, obj_class, NULL, 0,
                            &screen->compute);
   if (ret) {
      NOUVEAU_ERR("Failed to allocate compute object: %d\n", ret);
      return ret;
   }

   ret = nouveau_bo_new(dev, NV_VRAM_DOMAIN(&screen->base), 0, 1 << 12, NULL,
                        &screen->parm);
   if (ret)
      return ret;

   BEGIN_NVC0(push, SUBC_COMPUTE(NV01_SUBCHAN_OBJECT), 1);
   PUSH_DATA (push, screen->compute->oclass);

   /* hardware limit */
   BEGIN_NVC0(push, NVC0_COMPUTE(MP_LIMIT), 1);
   PUSH_DATA (push, screen->mp_count);
   BEGIN_NVC0(push, NVC0_COMPUTE(CALL_LIMIT_LOG), 1);
   PUSH_DATA (push, 0xf);

   BEGIN_NVC0(push, SUBC_COMPUTE(0x02a0), 1);
   PUSH_DATA (push, 0x8000);

   /* global memory setup */
   BEGIN_NVC0(push, SUBC_COMPUTE(0x02c4), 1);
   PUSH_DATA (push, 0);
   BEGIN_NIC0(push, NVC0_COMPUTE(GLOBAL_BASE), 0x100);
   for (i = 0; i <= 0xff; i++)
      PUSH_DATA (push, (0xc << 28) | (i << 16) | i);
   BEGIN_NVC0(push, SUBC_COMPUTE(0x02c4), 1);
   PUSH_DATA (push, 1);

   /* local memory and cstack setup */
   BEGIN_NVC0(push, NVC0_COMPUTE(TEMP_ADDRESS_HIGH), 2);
   PUSH_DATAh(push, screen->tls->offset);
   PUSH_DATA (push, screen->tls->offset);
   BEGIN_NVC0(push, NVC0_COMPUTE(TEMP_SIZE_HIGH), 2);
   PUSH_DATAh(push, screen->tls->size);
   PUSH_DATA (push, screen->tls->size);
   BEGIN_NVC0(push, NVC0_COMPUTE(WARP_TEMP_ALLOC), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_COMPUTE(LOCAL_BASE), 1);
   PUSH_DATA (push, 0xff << 24);

   /* shared memory setup */
   BEGIN_NVC0(push, NVC0_COMPUTE(CACHE_SPLIT), 1);
   PUSH_DATA (push, NVC0_COMPUTE_CACHE_SPLIT_48K_SHARED_16K_L1);
   BEGIN_NVC0(push, NVC0_COMPUTE(SHARED_BASE), 1);
   PUSH_DATA (push, 0xfe << 24);
   BEGIN_NVC0(push, NVC0_COMPUTE(SHARED_SIZE), 1);
   PUSH_DATA (push, 0);

   /* code segment setup */
   BEGIN_NVC0(push, NVC0_COMPUTE(CODE_ADDRESS_HIGH), 2);
   PUSH_DATAh(push, screen->text->offset);
   PUSH_DATA (push, screen->text->offset);

   /* textures */
   BEGIN_NVC0(push, NVC0_COMPUTE(TIC_ADDRESS_HIGH), 3);
   PUSH_DATAh(push, screen->txc->offset);
   PUSH_DATA (push, screen->txc->offset);
   PUSH_DATA (push, NVC0_TIC_MAX_ENTRIES - 1);

   /* samplers */
   BEGIN_NVC0(push, NVC0_COMPUTE(TSC_ADDRESS_HIGH), 3);
   PUSH_DATAh(push, screen->txc->offset + 65536);
   PUSH_DATA (push, screen->txc->offset + 65536);
   PUSH_DATA (push, NVC0_TSC_MAX_ENTRIES - 1);

   return 0;
}

bool
nvc0_compute_validate_program(struct nvc0_context *nvc0)
{
   struct nvc0_program *prog = nvc0->compprog;

   if (prog->mem)
      return true;

   if (!prog->translated) {
      prog->translated = nvc0_program_translate(
         prog, nvc0->screen->base.device->chipset, &nvc0->base.debug);
      if (!prog->translated)
         return false;
   }
   if (unlikely(!prog->code_size))
      return false;

   if (likely(prog->code_size)) {
      if (nvc0_program_upload_code(nvc0, prog)) {
         struct nouveau_pushbuf *push = nvc0->base.pushbuf;
         BEGIN_NVC0(push, NVC0_COMPUTE(FLUSH), 1);
         PUSH_DATA (push, NVC0_COMPUTE_FLUSH_CODE);
         return true;
      }
   }
   return false;
}

static void
nvc0_compute_validate_samplers(struct nvc0_context *nvc0)
{
   bool need_flush = nvc0_validate_tsc(nvc0, 5);
   if (need_flush) {
      BEGIN_NVC0(nvc0->base.pushbuf, NVC0_COMPUTE(TSC_FLUSH), 1);
      PUSH_DATA (nvc0->base.pushbuf, 0);
   }
}

static void
nvc0_compute_validate_textures(struct nvc0_context *nvc0)
{
   bool need_flush = nvc0_validate_tic(nvc0, 5);
   if (need_flush) {
      BEGIN_NVC0(nvc0->base.pushbuf, NVC0_COMPUTE(TIC_FLUSH), 1);
      PUSH_DATA (nvc0->base.pushbuf, 0);
   }
}

static void
nvc0_compute_validate_constbufs(struct nvc0_context *nvc0)
{
   struct nouveau_pushbuf *push = nvc0->base.pushbuf;
   const int s = 5;

   while (nvc0->constbuf_dirty[s]) {
      int i = ffs(nvc0->constbuf_dirty[s]) - 1;
      nvc0->constbuf_dirty[s] &= ~(1 << i);

      if (nvc0->constbuf[s][i].user) {
         struct nouveau_bo *bo = nvc0->screen->uniform_bo;
         const unsigned base = s << 16;
         const unsigned size = nvc0->constbuf[s][0].size;
         assert(i == 0); /* we really only want OpenGL uniforms here */
         assert(nvc0->constbuf[s][0].u.data);

         if (nvc0->state.uniform_buffer_bound[s] < size) {
            nvc0->state.uniform_buffer_bound[s] = align(size, 0x100);

            BEGIN_NVC0(push, NVC0_COMPUTE(CB_SIZE), 3);
            PUSH_DATA (push, nvc0->state.uniform_buffer_bound[s]);
            PUSH_DATAh(push, bo->offset + base);
            PUSH_DATA (push, bo->offset + base);
            BEGIN_NVC0(push, NVC0_COMPUTE(CB_BIND), 1);
            PUSH_DATA (push, (0 << 8) | 1);
         }
         nvc0_cb_bo_push(&nvc0->base, bo, NV_VRAM_DOMAIN(&nvc0->screen->base),
                         base, nvc0->state.uniform_buffer_bound[s],
                         0, (size + 3) / 4,
                         nvc0->constbuf[s][0].u.data);
      } else {
         struct nv04_resource *res =
            nv04_resource(nvc0->constbuf[s][i].u.buf);
         if (res) {
            BEGIN_NVC0(push, NVC0_COMPUTE(CB_SIZE), 3);
            PUSH_DATA (push, nvc0->constbuf[s][i].size);
            PUSH_DATAh(push, res->address + nvc0->constbuf[s][i].offset);
            PUSH_DATA (push, res->address + nvc0->constbuf[s][i].offset);
            BEGIN_NVC0(push, NVC0_COMPUTE(CB_BIND), 1);
            PUSH_DATA (push, (i << 8) | 1);

            BCTX_REFN(nvc0->bufctx_cp, CP_CB(i), res, RD);

            res->cb_bindings[s] |= 1 << i;
         } else {
            BEGIN_NVC0(push, NVC0_COMPUTE(CB_BIND), 1);
            PUSH_DATA (push, (i << 8) | 0);
         }
         if (i == 0)
            nvc0->state.uniform_buffer_bound[s] = 0;
      }
   }

   BEGIN_NVC0(push, NVC0_COMPUTE(FLUSH), 1);
   PUSH_DATA (push, NVC0_COMPUTE_FLUSH_CB);
}

static void
nvc0_compute_validate_driverconst(struct nvc0_context *nvc0)
{
   struct nouveau_pushbuf *push = nvc0->base.pushbuf;
   struct nvc0_screen *screen = nvc0->screen;

   BEGIN_NVC0(push, NVC0_COMPUTE(CB_SIZE), 3);
   PUSH_DATA (push, 1024);
   PUSH_DATAh(push, screen->uniform_bo->offset + (6 << 16) + (5 << 10));
   PUSH_DATA (push, screen->uniform_bo->offset + (6 << 16) + (5 << 10));
   BEGIN_NVC0(push, NVC0_COMPUTE(CB_BIND), 1);
   PUSH_DATA (push, (15 << 8) | 1);

   nvc0->dirty |= NVC0_NEW_DRIVERCONST;
}

static void
nvc0_compute_validate_buffers(struct nvc0_context *nvc0)
{
   struct nouveau_pushbuf *push = nvc0->base.pushbuf;
   const int s = 5;
   int i;

   BEGIN_NVC0(push, NVC0_COMPUTE(CB_SIZE), 3);
   PUSH_DATA (push, 1024);
   PUSH_DATAh(push, nvc0->screen->uniform_bo->offset + (6 << 16) + (s << 10));
   PUSH_DATA (push, nvc0->screen->uniform_bo->offset + (6 << 16) + (s << 10));
   BEGIN_1IC0(push, NVC0_COMPUTE(CB_POS), 1 + 4 * NVC0_MAX_BUFFERS);
   PUSH_DATA (push, 512);

   for (i = 0; i < NVC0_MAX_BUFFERS; i++) {
      if (nvc0->buffers[s][i].buffer) {
         struct nv04_resource *res =
            nv04_resource(nvc0->buffers[s][i].buffer);
         PUSH_DATA (push, res->address + nvc0->buffers[s][i].buffer_offset);
         PUSH_DATAh(push, res->address + nvc0->buffers[s][i].buffer_offset);
         PUSH_DATA (push, nvc0->buffers[s][i].buffer_size);
         PUSH_DATA (push, 0);
         BCTX_REFN(nvc0->bufctx_cp, CP_BUF, res, RDWR);
      } else {
         PUSH_DATA (push, 0);
         PUSH_DATA (push, 0);
         PUSH_DATA (push, 0);
         PUSH_DATA (push, 0);
      }
   }
}

static bool
nvc0_compute_state_validate(struct nvc0_context *nvc0)
{
   if (!nvc0_compute_validate_program(nvc0))
      return false;
   if (nvc0->dirty_cp & NVC0_NEW_CP_CONSTBUF)
      nvc0_compute_validate_constbufs(nvc0);
   if (nvc0->dirty_cp & NVC0_NEW_CP_DRIVERCONST)
      nvc0_compute_validate_driverconst(nvc0);
   if (nvc0->dirty_cp & NVC0_NEW_CP_BUFFERS)
      nvc0_compute_validate_buffers(nvc0);
   if (nvc0->dirty_cp & NVC0_NEW_CP_TEXTURES)
      nvc0_compute_validate_textures(nvc0);
   if (nvc0->dirty_cp & NVC0_NEW_CP_SAMPLERS)
      nvc0_compute_validate_samplers(nvc0);

   /* TODO: surfaces, global memory buffers */

   nvc0_bufctx_fence(nvc0, nvc0->bufctx_cp, false);

   nouveau_pushbuf_bufctx(nvc0->base.pushbuf, nvc0->bufctx_cp);
   if (unlikely(nouveau_pushbuf_validate(nvc0->base.pushbuf)))
      return false;
   if (unlikely(nvc0->state.flushed))
      nvc0_bufctx_fence(nvc0, nvc0->bufctx_cp, true);

   return true;

}

static void
nvc0_compute_upload_input(struct nvc0_context *nvc0, const void *input)
{
   struct nouveau_pushbuf *push = nvc0->base.pushbuf;
   struct nvc0_screen *screen = nvc0->screen;
   struct nvc0_program *cp = nvc0->compprog;

   if (cp->parm_size) {
      BEGIN_NVC0(push, NVC0_COMPUTE(CB_SIZE), 3);
      PUSH_DATA (push, align(cp->parm_size, 0x100));
      PUSH_DATAh(push, screen->parm->offset);
      PUSH_DATA (push, screen->parm->offset);
      BEGIN_NVC0(push, NVC0_COMPUTE(CB_BIND), 1);
      PUSH_DATA (push, (0 << 8) | 1);
      /* NOTE: size is limited to 4 KiB, which is < NV04_PFIFO_MAX_PACKET_LEN */
      BEGIN_1IC0(push, NVC0_COMPUTE(CB_POS), 1 + cp->parm_size / 4);
      PUSH_DATA (push, 0);
      PUSH_DATAp(push, input, cp->parm_size / 4);

      BEGIN_NVC0(push, NVC0_COMPUTE(FLUSH), 1);
      PUSH_DATA (push, NVC0_COMPUTE_FLUSH_CB);
   }
}

void
nvc0_launch_grid(struct pipe_context *pipe, const struct pipe_grid_info *info)
{
   struct nvc0_context *nvc0 = nvc0_context(pipe);
   struct nouveau_pushbuf *push = nvc0->base.pushbuf;
   struct nvc0_program *cp = nvc0->compprog;
   unsigned s;
   int ret;

   ret = !nvc0_compute_state_validate(nvc0);
   if (ret) {
      NOUVEAU_ERR("Failed to launch grid !\n");
      return;
   }

   nvc0_compute_upload_input(nvc0, info->input);

   BEGIN_NVC0(push, NVC0_COMPUTE(CP_START_ID), 1);
   PUSH_DATA (push, nvc0_program_symbol_offset(cp, info->pc));

   BEGIN_NVC0(push, NVC0_COMPUTE(LOCAL_POS_ALLOC), 3);
   PUSH_DATA (push, align(cp->cp.lmem_size, 0x10));
   PUSH_DATA (push, 0);
   PUSH_DATA (push, 0x800); /* WARP_CSTACK_SIZE */

   BEGIN_NVC0(push, NVC0_COMPUTE(SHARED_SIZE), 3);
   PUSH_DATA (push, align(cp->cp.smem_size, 0x100));
   PUSH_DATA (push, info->block[0] * info->block[1] * info->block[2]);
   PUSH_DATA (push, cp->num_barriers);
   BEGIN_NVC0(push, NVC0_COMPUTE(CP_GPR_ALLOC), 1);
   PUSH_DATA (push, cp->num_gprs);

   /* launch preliminary setup */
   BEGIN_NVC0(push, NVC0_COMPUTE(GRIDID), 1);
   PUSH_DATA (push, 0x1);
   BEGIN_NVC0(push, SUBC_COMPUTE(0x036c), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVC0_COMPUTE(FLUSH), 1);
   PUSH_DATA (push, NVC0_COMPUTE_FLUSH_GLOBAL | NVC0_COMPUTE_FLUSH_UNK8);

   /* block setup */
   BEGIN_NVC0(push, NVC0_COMPUTE(BLOCKDIM_YX), 2);
   PUSH_DATA (push, (info->block[1] << 16) | info->block[0]);
   PUSH_DATA (push, info->block[2]);

   if (unlikely(info->indirect)) {
      struct nv04_resource *res = nv04_resource(info->indirect);
      uint32_t offset = res->offset + info->indirect_offset;
      unsigned macro = NVC0_COMPUTE_MACRO_LAUNCH_GRID_INDIRECT;

      nouveau_pushbuf_space(push, 16, 0, 1);
      PUSH_REFN(push, res->bo, NOUVEAU_BO_RD | res->domain);
      PUSH_DATA(push, NVC0_FIFO_PKHDR_1I(1, macro, 3));
      nouveau_pushbuf_data(push, res->bo, offset,
                           NVC0_IB_ENTRY_1_NO_PREFETCH | 3 * 4);
   } else {
      /* grid setup */
      BEGIN_NVC0(push, NVC0_COMPUTE(GRIDDIM_YX), 2);
      PUSH_DATA (push, (info->grid[1] << 16) | info->grid[0]);
      PUSH_DATA (push, info->grid[2]);

      /* kernel launching */
      BEGIN_NVC0(push, NVC0_COMPUTE(COMPUTE_BEGIN), 1);
      PUSH_DATA (push, 0);
      BEGIN_NVC0(push, SUBC_COMPUTE(0x0a08), 1);
      PUSH_DATA (push, 0);
      BEGIN_NVC0(push, NVC0_COMPUTE(LAUNCH), 1);
      PUSH_DATA (push, 0x1000);
      BEGIN_NVC0(push, NVC0_COMPUTE(COMPUTE_END), 1);
      PUSH_DATA (push, 0);
      BEGIN_NVC0(push, SUBC_COMPUTE(0x0360), 1);
      PUSH_DATA (push, 0x1);
   }

   /* Invalidate all 3D constbufs because they are aliased with COMPUTE. */
   nvc0->dirty |= NVC0_NEW_CONSTBUF;
   for (s = 0; s < 5; s++) {
      nvc0->constbuf_dirty[s] |= nvc0->constbuf_valid[s];
      nvc0->state.uniform_buffer_bound[s] = 0;
   }
}
