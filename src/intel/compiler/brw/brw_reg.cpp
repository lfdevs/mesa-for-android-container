/*
 * Copyright © 2010 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>

#include "brw_reg.h"
#include "util/macros.h"

bool
brw_reg_saturate_immediate(brw_reg *reg)
{
   union {
      unsigned ud;
      int d;
      float f;
      double df;
   } imm, sat_imm = { 0 };

   const unsigned size = brw_type_size_bytes(reg->type);

   /* We want to either do a 32-bit or 64-bit data copy, the type is otherwise
    * irrelevant, so just check the size of the type and copy from/to an
    * appropriately sized field.
    */
   if (size < 8)
      imm.ud = reg->ud;
   else
      imm.df = reg->df;

   switch (reg->type) {
   case BRW_TYPE_UD:
   case BRW_TYPE_D:
   case BRW_TYPE_UW:
   case BRW_TYPE_W:
   case BRW_TYPE_UQ:
   case BRW_TYPE_Q:
      /* Nothing to do. */
      return false;
   case BRW_TYPE_F:
      sat_imm.f = SATURATE(imm.f);
      break;
   case BRW_TYPE_DF:
      sat_imm.df = SATURATE(imm.df);
      break;
   case BRW_TYPE_UB:
   case BRW_TYPE_B:
      UNREACHABLE("no UB/B immediates");
   case BRW_TYPE_V:
   case BRW_TYPE_UV:
   case BRW_TYPE_VF:
      UNREACHABLE("unimplemented: saturate vector immediate");
   case BRW_TYPE_HF:
      UNREACHABLE("unimplemented: saturate HF immediate");
   default:
      UNREACHABLE("invalid type");
   }

   if (size < 8) {
      if (imm.ud != sat_imm.ud) {
         reg->ud = sat_imm.ud;
         return true;
      }
   } else {
      if (imm.df != sat_imm.df) {
         reg->df = sat_imm.df;
         return true;
      }
   }
   return false;
}

bool
brw_reg_negate_immediate(brw_reg *reg)
{
   switch (reg->type) {
   case BRW_TYPE_D:
   case BRW_TYPE_UD:
      reg->d = -reg->d;
      return true;
   case BRW_TYPE_W:
   case BRW_TYPE_UW: {
      uint16_t value = -(int16_t)reg->ud;
      reg->ud = value | (uint32_t)value << 16;
      return true;
   }
   case BRW_TYPE_F:
      reg->f = -reg->f;
      return true;
   case BRW_TYPE_VF:
      reg->ud ^= 0x80808080;
      return true;
   case BRW_TYPE_DF:
      reg->df = -reg->df;
      return true;
   case BRW_TYPE_UQ:
   case BRW_TYPE_Q:
      reg->d64 = -reg->d64;
      return true;
   case BRW_TYPE_UB:
   case BRW_TYPE_B:
      UNREACHABLE("no UB/B immediates");
   case BRW_TYPE_UV:
   case BRW_TYPE_V:
      UNREACHABLE("unimplemented: negate UV/V immediate");
   case BRW_TYPE_HF:
      reg->ud ^= 0x80008000;
      return true;
   default:
      UNREACHABLE("invalid type");
   }

   return false;
}

bool
brw_reg_abs_immediate(brw_reg *reg)
{
   switch (reg->type) {
   case BRW_TYPE_D:
      reg->d = abs(reg->d);
      return true;
   case BRW_TYPE_W: {
      uint16_t value = abs((int16_t)reg->ud);
      reg->ud = value | (uint32_t)value << 16;
      return true;
   }
   case BRW_TYPE_F:
      reg->f = fabsf(reg->f);
      return true;
   case BRW_TYPE_DF:
      reg->df = fabs(reg->df);
      return true;
   case BRW_TYPE_VF:
      reg->ud &= ~0x80808080;
      return true;
   case BRW_TYPE_Q:
      reg->d64 = imaxabs(reg->d64);
      return true;
   case BRW_TYPE_UB:
   case BRW_TYPE_B:
      UNREACHABLE("no UB/B immediates");
   case BRW_TYPE_UQ:
   case BRW_TYPE_UD:
   case BRW_TYPE_UW:
   case BRW_TYPE_UV:
      /* Presumably the absolute value modifier on an unsigned source is a
       * nop, but it would be nice to confirm.
       */
      UNREACHABLE("unimplemented: abs unsigned immediate");
   case BRW_TYPE_V:
      UNREACHABLE("unimplemented: abs V immediate");
   case BRW_TYPE_HF:
      reg->ud &= ~0x80008000;
      return true;
   default:
      UNREACHABLE("invalid type");
   }

   return false;
}

bool
brw_reg::is_zero() const
{
   if (file != IMM)
      return false;

   assert(brw_type_size_bytes(type) > 1);

   switch (type) {
   case BRW_TYPE_HF:
      assert((d & 0xffff) == ((d >> 16) & 0xffff));
      return (d & 0xffff) == 0 || (d & 0xffff) == 0x8000;
   case BRW_TYPE_F:
      return f == 0;
   case BRW_TYPE_DF:
      return df == 0;
   case BRW_TYPE_W:
   case BRW_TYPE_UW:
      assert((d & 0xffff) == ((d >> 16) & 0xffff));
      return (d & 0xffff) == 0;
   case BRW_TYPE_D:
   case BRW_TYPE_UD:
      return d == 0;
   case BRW_TYPE_UQ:
   case BRW_TYPE_Q:
      return u64 == 0;
   default:
      return false;
   }
}

bool
brw_reg::is_one() const
{
   if (file != IMM)
      return false;

   assert(brw_type_size_bytes(type) > 1);

   switch (type) {
   case BRW_TYPE_HF:
      assert((d & 0xffff) == ((d >> 16) & 0xffff));
      return (d & 0xffff) == 0x3c00;
   case BRW_TYPE_F:
      return f == 1.0f;
   case BRW_TYPE_DF:
      return df == 1.0;
   case BRW_TYPE_W:
   case BRW_TYPE_UW:
      assert((d & 0xffff) == ((d >> 16) & 0xffff));
      return (d & 0xffff) == 1;
   case BRW_TYPE_D:
   case BRW_TYPE_UD:
      return d == 1;
   case BRW_TYPE_UQ:
   case BRW_TYPE_Q:
      return u64 == 1;
   default:
      return false;
   }
}

bool
brw_reg::is_negative_one() const
{
   if (file != IMM)
      return false;

   assert(brw_type_size_bytes(type) > 1);

   switch (type) {
   case BRW_TYPE_HF:
      assert((d & 0xffff) == ((d >> 16) & 0xffff));
      return (d & 0xffff) == 0xbc00;
   case BRW_TYPE_F:
      return f == -1.0;
   case BRW_TYPE_DF:
      return df == -1.0;
   case BRW_TYPE_W:
      assert((d & 0xffff) == ((d >> 16) & 0xffff));
      return (d & 0xffff) == 0xffff;
   case BRW_TYPE_D:
      return d == -1;
   case BRW_TYPE_Q:
      return d64 == -1;
   default:
      return false;
   }
}

bool
brw_reg::is_address() const
{
   return file == ADDRESS;
}

unsigned
brw_reg::address_slot(unsigned byte_offset) const
{
   assert(is_address());
   return (reg_offset(*this) + byte_offset) / 2;
}

bool
brw_reg::equals(const brw_reg &r) const
{
   return brw_regs_equal(this, &r);
}

bool
brw_reg::negative_equals(const brw_reg &r) const
{
   return brw_regs_negative_equal(this, &r);
}

bool
brw_reg::is_contiguous() const
{
   switch (file) {
   case ADDRESS:
   case ARF:
   case FIXED_GRF:
      return hstride == BRW_HORIZONTAL_STRIDE_1 &&
             vstride == width + hstride;
   case VGRF:
   case ATTR:
      return stride == 1;
   case UNIFORM:
   case IMM:
   case BAD_FILE:
      return true;
   }

   UNREACHABLE("Invalid register file");
}

unsigned
brw_reg::component_size(unsigned width) const
{
   if (file == ADDRESS || file == ARF || file == FIXED_GRF) {
      const unsigned w = MIN2(width, 1u << this->width);
      const unsigned h = width >> this->width;
      const unsigned vs = vstride ? 1 << (vstride - 1) : 0;
      const unsigned hs = hstride ? 1 << (hstride - 1) : 0;
      assert(w > 0);
      /* Note this rounds up to next horizontal stride to be consistent with
       * the VGRF case below.
       */
      return ((MAX2(1, h) - 1) * vs + MAX2(w * hs, 1)) * brw_type_size_bytes(type);
   } else {
      return MAX2(width * stride, 1) * brw_type_size_bytes(type);
   }
}
