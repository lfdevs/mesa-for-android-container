/*
 * Copyright 2021 Alyssa Rosenzweig
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
#
#include <stdint.h>
#include "agx_state.h"
#include "magic.h"

/* The structures managed in this file appear to be software defined (either in
 * the macOS kernel driver or in the AGX firmware) */

/* Odd pattern */
static uint64_t
demo_unk6(struct agx_pool *pool)
{
   struct agx_ptr ptr = agx_pool_alloc_aligned(pool, 0x4000 * sizeof(uint64_t), 64);
   uint64_t *buf = ptr.cpu;
   memset(buf, 0, sizeof(*buf));

   for (unsigned i = 1; i < 0x3ff; ++i)
      buf[i] = (i + 1);

   return ptr.gpu;
}

static uint64_t
demo_zero(struct agx_pool *pool, unsigned count)
{
   struct agx_ptr ptr = agx_pool_alloc_aligned(pool, count, 64);
   memset(ptr.cpu, 0, count);
   return ptr.gpu;
}

static size_t
asahi_size_resource(struct pipe_resource *prsrc, unsigned level)
{
   struct agx_resource *rsrc = agx_resource(prsrc);
   size_t size = rsrc->slices[level].size;

   if (rsrc->separate_stencil)
      size += asahi_size_resource(&rsrc->separate_stencil->base, level);

   return size;
}

static size_t
asahi_size_surface(struct pipe_surface *surf)
{
   return asahi_size_resource(surf->texture, surf->u.tex.level);
}

static size_t
asahi_size_attachments(struct pipe_framebuffer_state *framebuffer)
{
   size_t sum = 0;

   for (unsigned i = 0; i < framebuffer->nr_cbufs; ++i)
      sum += asahi_size_surface(framebuffer->cbufs[i]);

   if (framebuffer->zsbuf)
      sum += asahi_size_surface(framebuffer->zsbuf);

   return sum;
}

static enum agx_iogpu_attachment_type
asahi_classify_attachment(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);

   if (util_format_has_depth(desc))
      return AGX_IOGPU_ATTACHMENT_TYPE_DEPTH;
   else if (util_format_has_stencil(desc))
      return AGX_IOGPU_ATTACHMENT_TYPE_STENCIL;
   else
      return AGX_IOGPU_ATTACHMENT_TYPE_COLOUR;
}

static void
asahi_pack_iogpu_attachment(void *out, struct agx_resource *rsrc,
                            struct pipe_surface *surf,
                            unsigned total_size)
{
   /* We don't support layered rendering yet */
   assert(surf->u.tex.first_layer == surf->u.tex.last_layer);

   agx_pack(out, IOGPU_ATTACHMENT, cfg) {
      cfg.type = asahi_classify_attachment(rsrc->base.format);

      cfg.address = agx_map_texture_gpu(rsrc, surf->u.tex.level,
                                              surf->u.tex.first_layer);

      cfg.size = rsrc->slices[surf->u.tex.level].size;

      cfg.percent = (100 * cfg.size) / total_size;
   }
}

static unsigned
asahi_pack_iogpu_attachments(void *out, struct pipe_framebuffer_state *framebuffer)
{
   unsigned total_attachment_size = asahi_size_attachments(framebuffer);
   struct agx_iogpu_attachment_packed *attachments = out;
   unsigned nr = 0;

   for (unsigned i = 0; i < framebuffer->nr_cbufs; ++i) {
      asahi_pack_iogpu_attachment(attachments + (nr++),
                                  agx_resource(framebuffer->cbufs[i]->texture),
                                  framebuffer->cbufs[i],
                                  total_attachment_size);
   }

   if (framebuffer->zsbuf) {
         struct agx_resource *rsrc = agx_resource(framebuffer->zsbuf->texture);

         asahi_pack_iogpu_attachment(attachments + (nr++),
                                     rsrc, framebuffer->zsbuf,
                                     total_attachment_size);

         if (rsrc->separate_stencil) {
            asahi_pack_iogpu_attachment(attachments + (nr++),
                                        rsrc->separate_stencil,
                                        framebuffer->zsbuf,
                                        total_attachment_size);
         }
   }

   return nr;
}

unsigned
demo_cmdbuf(uint64_t *buf, size_t size,
            struct agx_pool *pool,
            struct pipe_framebuffer_state *framebuffer,
            uint64_t encoder_ptr,
            uint64_t encoder_id,
            uint64_t scissor_ptr,
            uint32_t pipeline_null,
            uint32_t pipeline_clear,
            uint32_t pipeline_store,
            bool clear_pipeline_textures)
{
   uint32_t *map = (uint32_t *) buf;
   memset(map, 0, 518 * 4);

   map[54] = 0x6b0003;
   map[55] = 0x3a0012;
   map[56] = 1;

   /* Unknown address at word 110 */

   map[112] = 1;
   map[114] = 0x1c;
   map[118] = 0xffffffff;
   map[119] = 0xffffffff;
   map[120] = 0xffffffff;

   uint64_t unk_buffer = demo_zero(pool, 0x1000);
   uint64_t unk_buffer_2 = demo_zero(pool, 0x8000);

   agx_pack(map + 160, IOGPU_INTERNAL_PIPELINES, cfg) {
      cfg.clear_pipeline_bind = 0xffff8002 | (clear_pipeline_textures ? 0x210 : 0);
      cfg.clear_pipeline = pipeline_clear;
      cfg.store_pipeline_bind = 0x12;
      cfg.store_pipeline = pipeline_store;
      cfg.scissor_array = scissor_ptr;
      cfg.unknown_buffer = unk_buffer;
   }

   agx_pack(map + 228, IOGPU_AUX_FRAMEBUFFER, cfg) {
      cfg.width = framebuffer->width;
      cfg.height = framebuffer->height;
      cfg.z16_unorm_attachment = false;
      cfg.pointer = unk_buffer_2;
   }

   agx_pack(map + 292, IOGPU_CLEAR_Z_S, cfg) {
      cfg.depth_clear_value = fui(1.0); // 32-bit float
      cfg.stencil_clear_value = 0;
      cfg.z16_unorm_attachment = false;
   }

   map[312] = 0xffff8212;
   map[314] = pipeline_null | 0x4;
   map[320] = 0x12;
   map[322] = pipeline_store | 0x4;

   agx_pack(map + 356, IOGPU_MISC, cfg) {
      cfg.encoder_id = encoder_id;
      cfg.unknown_buffer = demo_unk6(pool);
      cfg.width = framebuffer->width;
      cfg.height = framebuffer->height;
   }

   unsigned offset_unk = (484 * 4);
   unsigned offset_attachments = (496 * 4);

   unsigned nr_attachments =
      asahi_pack_iogpu_attachments(map + (offset_attachments / 4) + 4,
                                   framebuffer);

   map[(offset_attachments / 4) + 3] = nr_attachments;

   unsigned total_size = offset_attachments + (AGX_IOGPU_ATTACHMENT_LENGTH * nr_attachments) + 16;

   agx_pack(map, IOGPU_HEADER, cfg) {
      cfg.total_size = total_size;
      cfg.attachment_offset = offset_attachments;
      cfg.attachment_length = nr_attachments * AGX_IOGPU_ATTACHMENT_LENGTH;
      cfg.unknown_offset = offset_unk;
      cfg.encoder = encoder_ptr;

      cfg.deflake_1 = demo_zero(pool, 0x540);
      cfg.deflake_2 = demo_zero(pool, 0x280);
   }

   return total_size;
}

static struct agx_map_header
demo_map_header(uint64_t cmdbuf_id, uint64_t encoder_id, unsigned cmdbuf_size, unsigned count)
{
   return (struct agx_map_header) {
      .cmdbuf_id = cmdbuf_id,
      .unk2 = 0x1,
      .unk3 = 0x528, // 1320
      .encoder_id = encoder_id,
      .unk6 = 0x0,
      .cmdbuf_size = cmdbuf_size,

      /* +1 for the sentinel ending */
      .nr_entries = count,
      .nr_handles = count,
   };
}

void
demo_mem_map(void *map, size_t size, unsigned *handles, unsigned count,
             uint64_t cmdbuf_id, uint64_t encoder_id, unsigned cmdbuf_size)
{
   struct agx_map_header *header = map;
   struct agx_map_entry *entries = (struct agx_map_entry *) (((uint8_t *) map) + sizeof(*header));
   struct agx_map_entry *end = (struct agx_map_entry *) (((uint8_t *) map) + size);

   /* Header precedes the entry */
   *header = demo_map_header(cmdbuf_id, encoder_id, cmdbuf_size, count);

   /* Add an entry for each BO mapped */
   for (unsigned i = 0; i < count; ++i) {
	   assert((entries + i) < end);
      entries[i] = (struct agx_map_entry) {
         .indices = {handles[i]},
         .unkAAA = 0x20,
         .unkBBB = 0x1,
         .unka = 0x1ffff,
      };
   }
}
