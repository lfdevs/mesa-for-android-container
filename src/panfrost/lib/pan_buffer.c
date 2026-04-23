/*
 * Copyright (C) 2026 Arm Ltd.
 *
 * Derived from pan_texture.c which is:
 * Copyright (C) 2008 VMware, Inc.
 * Copyright (C) 2014 Broadcom
 * Copyright (C) 2018-2019 Alyssa Rosenzweig
 * Copyright (C) 2019-2020 Collabora, Ltd.
 * Copyright (C) 2024 Arm Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pan_buffer.h"
#include "pan_desc.h"
#include "pan_format.h"
#include "pan_texture.h"
#include "pan_util.h"

#if PAN_ARCH >= 9

void
GENX(pan_buffer_texture_emit)(const struct pan_buffer_view *bview,
                              struct mali_buffer_packed *out)
{
   unsigned stride = util_format_get_blocksize(bview->format);
   struct MALI_INTERNAL_CONVERSION conv = {
      .memory_format = GENX(pan_format_from_pipe_format)(bview->format)->hw,
      .raw = false,
   };

   uint64_t buffer_size = bview->width_el * stride;

   pan_pack(out, BUFFER, cfg) {
      cfg.type = MALI_DESCRIPTOR_TYPE_BUFFER;
      cfg.buffer_type = MALI_BUFFER_TYPE_STRUCTURE;

#if PAN_ARCH >= 11
      cfg.size = buffer_size & BITFIELD_MASK(32);
      cfg.size_hi = buffer_size >> 32;
#else
      cfg.size = buffer_size;
#endif

      cfg.address = bview->base;
      cfg.stride = stride;
      cfg.conversion = conv;
   }
}

#elif PAN_ARCH >= 6

void
GENX(pan_buffer_texture_emit)(const struct pan_buffer_view *bview,
                              struct mali_attribute_buffer_packed *out_buf,
                              struct mali_attribute_packed *out_attrib)
{
   unsigned stride = util_format_get_blocksize(bview->format);
   uint32_t hw_fmt = GENX(pan_format_from_pipe_format)(bview->format)->hw;

   /* The base address has shr(6) applied so we need to use the offset of the
    * attribute to get the last 6 bits.
    */
   uint64_t base = bview->base & ~0x3f;
   uint32_t offset = bview->base & 0x3f;

   pan_pack(out_buf, ATTRIBUTE_BUFFER, cfg) {
      cfg.type = MALI_ATTRIBUTE_TYPE_1D;
      cfg.pointer = base;
      cfg.stride = stride;
      /* The ATTRIBUTE_BUFFER.size specifies the size of the buffer starting
       * at ATTRIBUTE_BUFFER.pointer and and ATTRIBUTE.offset specifies an
       * offset into that window. If we're going to use it to offset the base
       * of the buffer, we need to increase the size as well.
       */
      cfg.size = bview->width_el * stride + offset;
   }

   pan_pack(out_attrib, ATTRIBUTE, cfg) {
      cfg.format = hw_fmt;
      cfg.offset = offset;
      cfg.offset_enable = offset != 0;
   }
}

#else

void
GENX(pan_buffer_texture_emit)(const struct pan_buffer_view *bview,
                              struct mali_texture_packed *out,
                              const struct pan_ptr *payload)
{
   uint32_t mali_format = GENX(pan_format_from_pipe_format)(bview->format)->hw;
   static const unsigned char rgba_swizzle[4] = {
      PIPE_SWIZZLE_X,
      PIPE_SWIZZLE_Y,
      PIPE_SWIZZLE_Z,
      PIPE_SWIZZLE_W,
   };

   pan_cast_and_pack(payload->cpu, SURFACE_WITH_STRIDE, cfg) {
      cfg.pointer = bview->base;
      cfg.row_stride = 0;
      cfg.surface_stride = 0;
   }

   pan_pack(out, TEXTURE, cfg) {
      cfg.dimension = MALI_TEXTURE_DIMENSION_1D;
      cfg.format = mali_format;
      cfg.width = bview->width_el;
      cfg.height = 1;
      cfg.sample_count = 1;
      cfg.swizzle = pan_translate_swizzle_4(rgba_swizzle);
      cfg.texel_ordering = MALI_TEXTURE_LAYOUT_LINEAR;
      cfg.levels = 1;
      cfg.array_size = 1;
   }
}

#endif
