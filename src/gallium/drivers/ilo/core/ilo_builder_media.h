/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2014 LunarG, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#ifndef ILO_BUILDER_MEDIA_H
#define ILO_BUILDER_MEDIA_H

#include "genhw/genhw.h"
#include "intel_winsys.h"

#include "ilo_core.h"
#include "ilo_dev.h"
#include "ilo_state_compute.h"
#include "ilo_builder.h"

static inline void
gen6_MEDIA_VFE_STATE(struct ilo_builder *builder,
                     const struct ilo_state_compute *compute)
{
   const uint8_t cmd_len = 8;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 6, 7.5);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(MEDIA, MEDIA_VFE_STATE) | (cmd_len - 2);
   /* see compute_set_gen6_MEDIA_VFE_STATE() */
   dw[1] = compute->vfe[0];
   dw[2] = compute->vfe[1];
   dw[3] = 0;
   dw[4] = compute->vfe[2];
   dw[5] = 0;
   dw[6] = 0;
   dw[7] = 0;
}

static inline void
gen6_MEDIA_CURBE_LOAD(struct ilo_builder *builder,
                      uint32_t offset, unsigned size)
{
   const uint8_t cmd_len = 4;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 7, 7.5);

   assert(offset % 32 == 0 && size % 32 == 0);
   /* GPU hangs if size is zero */
   assert(size);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(MEDIA, MEDIA_CURBE_LOAD) | (cmd_len - 2);
   dw[1] = 0;
   dw[2] = size;
   dw[3] = offset;
}

static inline void
gen6_MEDIA_INTERFACE_DESCRIPTOR_LOAD(struct ilo_builder *builder,
                                     uint32_t offset, unsigned size)
{
   const uint8_t cmd_len = 4;
   const unsigned idrt_alloc =
      ((ilo_dev_gen(builder->dev) >= ILO_GEN(7.5)) ? 64 : 32) * 32;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 7, 7.5);

   assert(offset % 32 == 0 && size % 32 == 0);
   assert(size && size <= idrt_alloc);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(MEDIA, MEDIA_INTERFACE_DESCRIPTOR_LOAD) |
           (cmd_len - 2);
   dw[1] = 0;
   dw[2] = size;
   dw[3] = offset;
}

static inline void
gen6_MEDIA_STATE_FLUSH(struct ilo_builder *builder)
{
   const uint8_t cmd_len = 2;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 7, 7.5);

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN6_RENDER_CMD(MEDIA, MEDIA_STATE_FLUSH) | (cmd_len - 2);
   dw[1] = 0;
}

static inline void
gen7_GPGPU_WALKER(struct ilo_builder *builder,
                  const unsigned thread_group_offset[3],
                  const unsigned thread_group_dim[3],
                  unsigned thread_group_size,
                  unsigned simd_size)
{
   const uint8_t cmd_len = 11;
   uint32_t right_execmask, bottom_execmask;
   unsigned thread_count;
   uint32_t *dw;

   ILO_DEV_ASSERT(builder->dev, 7, 7.5);

   assert(simd_size == 16 || simd_size == 8);

   thread_count = (thread_group_size + simd_size - 1) / simd_size;
   assert(thread_count <= 64);

   right_execmask = thread_group_size % simd_size;
   if (right_execmask)
      right_execmask = (1 << right_execmask) - 1;
   else
      right_execmask = (1 << simd_size) - 1;

   bottom_execmask = 0xffffffff;

   ilo_builder_batch_pointer(builder, cmd_len, &dw);

   dw[0] = GEN7_RENDER_CMD(MEDIA, GPGPU_WALKER) | (cmd_len - 2);
   dw[1] = 0; /* always first IDRT */

   dw[2] = (thread_count - 1) << GEN7_GPGPU_DW2_THREAD_MAX_X__SHIFT;
   if (simd_size == 16)
      dw[2] |= GEN7_GPGPU_DW2_SIMD_SIZE_SIMD16;
   else
      dw[2] |= GEN7_GPGPU_DW2_SIMD_SIZE_SIMD8;

   dw[3] = thread_group_offset[0];
   dw[4] = thread_group_dim[0];
   dw[5] = thread_group_offset[1];
   dw[6] = thread_group_dim[1];
   dw[7] = thread_group_offset[2];
   dw[8] = thread_group_dim[2];

   dw[9] = right_execmask;
   dw[10] = bottom_execmask;
}

static inline uint32_t
gen6_INTERFACE_DESCRIPTOR_DATA(struct ilo_builder *builder,
                               const struct ilo_state_compute *compute,
                               const uint32_t *kernel_offsets,
                               const uint32_t *sampler_offsets,
                               const uint32_t *binding_table_offsets)
{
   /*
    * From the Sandy Bridge PRM, volume 2 part 2, page 34:
    *
    *     "(Interface Descriptor Total Length) This field must have the same
    *      alignment as the Interface Descriptor Data Start Address.
    *
    *      It must be DQWord (32-byte) aligned..."
    *
    * From the Sandy Bridge PRM, volume 2 part 2, page 35:
    *
    *     "(Interface Descriptor Data Start Address) Specifies the 32-byte
    *      aligned address of the Interface Descriptor data."
    */
   const int state_align = 32;
   const int state_len = (32 / 4) * compute->idrt_count;
   uint32_t state_offset, *dw;
   int i;

   ILO_DEV_ASSERT(builder->dev, 6, 7.5);

   state_offset = ilo_builder_dynamic_pointer(builder,
         ILO_BUILDER_ITEM_INTERFACE_DESCRIPTOR, state_align, state_len, &dw);

   for (i = 0; i < compute->idrt_count; i++) {
      /* see compute_set_gen6_INTERFACE_DESCRIPTOR_DATA() */
      dw[0] = compute->idrt[i][0] + kernel_offsets[i];
      dw[1] = 0;
      dw[2] = compute->idrt[i][1] |
              sampler_offsets[i];
      dw[3] = compute->idrt[i][2] |
              binding_table_offsets[i];
      dw[4] = compute->idrt[i][3];
      dw[5] = compute->idrt[i][4];
      dw[6] = compute->idrt[i][5];
      dw[7] = 0;

      dw += 8;
   }

   return state_offset;
}

#endif /* ILO_BUILDER_MEDIA_H */
