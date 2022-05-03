/*
 * Copyright (C) 2021 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "va_compiler.h"
#include "valhall.h"
#include "valhall_enums.h"
#include "bi_builder.h"

/* This file contains the final passes of the compiler. Running after
 * scheduling and RA, the IR is now finalized, so we need to emit it to actual
 * bits on the wire (as well as fixup branches)
 */

/*
 * Validate that two adjacent 32-bit sources form an aligned 64-bit register
 * pair. This is a compiler invariant, required on Valhall but not on Bifrost.
 */
static void
va_validate_register_pair(const bi_instr *I, unsigned s)
{
   ASSERTED bi_index lo = I->src[s], hi = I->src[s + 1];

   assert(lo.type == hi.type);

   if (lo.type == BI_INDEX_REGISTER) {
      assert(hi.value & 1);
      assert(hi.value == lo.value + 1);
   } else if (lo.type == BI_INDEX_FAU && lo.value & BIR_FAU_IMMEDIATE) {
      /* Small constants are zero extended, so the top word encode zero */
      assert(hi.value == (BIR_FAU_IMMEDIATE | 0));
   } else {
      assert(hi.offset & 1);
      assert(hi.offset == lo.offset + 1);
   }
}

static unsigned
va_pack_reg(bi_index idx)
{
   assert(idx.type == BI_INDEX_REGISTER);
   assert(idx.value < 64);
   return idx.value;
}

static unsigned
va_pack_fau_special(enum bir_fau fau)
{
   switch (fau) {
   case BIR_FAU_ATEST_PARAM:     return VA_FAU_SPECIAL_PAGE_0_ATEST_DATUM;
   case BIR_FAU_TLS_PTR:         return VA_FAU_SPECIAL_PAGE_1_THREAD_LOCAL_POINTER;
   case BIR_FAU_WLS_PTR:         return VA_FAU_SPECIAL_PAGE_1_WORKGROUP_LOCAL_POINTER;
   case BIR_FAU_LANE_ID:         return VA_FAU_SPECIAL_PAGE_3_LANE_ID;
   case BIR_FAU_PROGRAM_COUNTER: return VA_FAU_SPECIAL_PAGE_3_PROGRAM_COUNTER;

   case BIR_FAU_BLEND_0...(BIR_FAU_BLEND_0 + 7):
      return VA_FAU_SPECIAL_PAGE_0_BLEND_DESCRIPTOR_0 + (fau - BIR_FAU_BLEND_0);

   default:
      unreachable("Unknown FAU value");
   }
}

/*
 * Encode a 64-bit FAU source. The offset is ignored, so this function can be
 * used to encode a 32-bit FAU source by or'ing in the appropriate offset.
 */
static unsigned
va_pack_fau_64(bi_index idx)
{
   assert(idx.type == BI_INDEX_FAU);

   unsigned val = (idx.value & BITFIELD_MASK(5));

   if (idx.value & BIR_FAU_IMMEDIATE)
      return (0x3 << 6) | (val << 1);
   else if (idx.value & BIR_FAU_UNIFORM)
      return (0x2 << 6) | (val << 1);
   else
      return (0x7 << 5) | (va_pack_fau_special(idx.value) << 1);
}

static unsigned
va_pack_src(bi_index idx)
{
   if (idx.type == BI_INDEX_REGISTER) {
      unsigned value = va_pack_reg(idx);
      if (idx.discard) value |= (1 << 6);
      return value;
   } else if (idx.type == BI_INDEX_FAU) {
      assert(idx.offset <= 1);
      return va_pack_fau_64(idx) | idx.offset;
   }

   unreachable("Invalid type");
}

static unsigned
va_pack_wrmask(enum bi_swizzle swz)
{
   switch (swz) {
   case BI_SWIZZLE_H00: return 0x1;
   case BI_SWIZZLE_H11: return 0x2;
   case BI_SWIZZLE_H01: return 0x3;
   default: unreachable("Invalid write mask");
   }
}

static enum va_atomic_operation
va_pack_atom_opc(enum bi_atom_opc opc)
{
   switch (opc) {
   case BI_ATOM_OPC_AADD:  return VA_ATOMIC_OPERATION_AADD;
   case BI_ATOM_OPC_ASMIN: return VA_ATOMIC_OPERATION_ASMIN;
   case BI_ATOM_OPC_ASMAX: return VA_ATOMIC_OPERATION_ASMAX;
   case BI_ATOM_OPC_AUMIN: return VA_ATOMIC_OPERATION_AUMIN;
   case BI_ATOM_OPC_AUMAX: return VA_ATOMIC_OPERATION_AUMAX;
   case BI_ATOM_OPC_AAND:  return VA_ATOMIC_OPERATION_AAND;
   case BI_ATOM_OPC_AOR:   return VA_ATOMIC_OPERATION_AOR;
   case BI_ATOM_OPC_AXOR:  return VA_ATOMIC_OPERATION_AXOR;
   case BI_ATOM_OPC_ACMPXCHG:
   case BI_ATOM_OPC_AXCHG: return VA_ATOMIC_OPERATION_AXCHG;
   default: unreachable("Invalid atom_opc");
   }
}

static enum va_atomic_operation_with_1
va_pack_atom_opc_1(enum bi_atom_opc opc)
{
   switch (opc) {
   case BI_ATOM_OPC_AINC:     return VA_ATOMIC_OPERATION_WITH_1_AINC;
   case BI_ATOM_OPC_ADEC:     return VA_ATOMIC_OPERATION_WITH_1_ADEC;
   case BI_ATOM_OPC_AUMAX1:   return VA_ATOMIC_OPERATION_WITH_1_AUMAX1;
   case BI_ATOM_OPC_ASMAX1:   return VA_ATOMIC_OPERATION_WITH_1_ASMAX1;
   case BI_ATOM_OPC_AOR1:     return VA_ATOMIC_OPERATION_WITH_1_AOR1;
   default: unreachable("Invalid atom_opc");
   }
}

static unsigned
va_pack_dest(bi_index index)
{
   return va_pack_reg(index) | (va_pack_wrmask(index.swizzle) << 6);
}

static enum va_widen
va_pack_widen_f32(enum bi_swizzle swz)
{
   switch (swz) {
   case BI_SWIZZLE_H01: return VA_WIDEN_NONE;
   case BI_SWIZZLE_H00: return VA_WIDEN_H0;
   case BI_SWIZZLE_H11: return VA_WIDEN_H1;
   default: unreachable("Invalid widen");
   }
}

static enum va_swizzles_16_bit
va_pack_swizzle_f16(enum bi_swizzle swz)
{
   switch (swz) {
   case BI_SWIZZLE_H00: return VA_SWIZZLES_16_BIT_H00;
   case BI_SWIZZLE_H10: return VA_SWIZZLES_16_BIT_H10;
   case BI_SWIZZLE_H01: return VA_SWIZZLES_16_BIT_H01;
   case BI_SWIZZLE_H11: return VA_SWIZZLES_16_BIT_H11;
   default: unreachable("Invalid swizzle");
   }
}

static unsigned
va_pack_widen(enum bi_swizzle swz, enum va_size size)
{
   if (size == VA_SIZE_8) {
      switch (swz) {
      case BI_SWIZZLE_H01:    return VA_SWIZZLES_8_BIT_B0123;
      case BI_SWIZZLE_H00:    return VA_SWIZZLES_8_BIT_B0101;
      case BI_SWIZZLE_H11:    return VA_SWIZZLES_8_BIT_B2323;
      case BI_SWIZZLE_B0000:  return VA_SWIZZLES_8_BIT_B0000;
      case BI_SWIZZLE_B1111:  return VA_SWIZZLES_8_BIT_B1111;
      case BI_SWIZZLE_B2222:  return VA_SWIZZLES_8_BIT_B2222;
      case BI_SWIZZLE_B3333:  return VA_SWIZZLES_8_BIT_B3333;
      default: unreachable("Exotic swizzles not yet handled");
      }
   } else if (size == VA_SIZE_16) {
      switch (swz) {
      case BI_SWIZZLE_H00:    return VA_SWIZZLES_16_BIT_H00;
      case BI_SWIZZLE_H10:    return VA_SWIZZLES_16_BIT_H10;
      case BI_SWIZZLE_H01:    return VA_SWIZZLES_16_BIT_H01;
      case BI_SWIZZLE_H11:    return VA_SWIZZLES_16_BIT_H11;
      case BI_SWIZZLE_B0000:  return VA_SWIZZLES_16_BIT_B00;
      case BI_SWIZZLE_B1111:  return VA_SWIZZLES_16_BIT_B11;
      case BI_SWIZZLE_B2222:  return VA_SWIZZLES_16_BIT_B22;
      case BI_SWIZZLE_B3333:  return VA_SWIZZLES_16_BIT_B33;
      default: unreachable("Exotic swizzles not yet handled");
      }
   } else if (size == VA_SIZE_32) {
      switch (swz) {
      case BI_SWIZZLE_H01:    return VA_SWIZZLES_32_BIT_NONE;
      case BI_SWIZZLE_H00:    return VA_SWIZZLES_32_BIT_H0;
      case BI_SWIZZLE_H11:    return VA_SWIZZLES_32_BIT_H1;
      case BI_SWIZZLE_B0000:  return VA_SWIZZLES_32_BIT_B0;
      case BI_SWIZZLE_B1111:  return VA_SWIZZLES_32_BIT_B1;
      case BI_SWIZZLE_B2222:  return VA_SWIZZLES_32_BIT_B2;
      case BI_SWIZZLE_B3333:  return VA_SWIZZLES_32_BIT_B3;
      default: unreachable("Invalid swizzle");
      }
   } else {
      unreachable("TODO: other type sizes");
   }
}

static enum va_half_swizzles_8_bit
va_pack_halfswizzle(enum bi_swizzle swz)
{
   switch (swz) {
   case BI_SWIZZLE_B0000: return VA_HALF_SWIZZLES_8_BIT_B00;
   case BI_SWIZZLE_B1111: return VA_HALF_SWIZZLES_8_BIT_B11;
   case BI_SWIZZLE_B2222: return VA_HALF_SWIZZLES_8_BIT_B22;
   case BI_SWIZZLE_B3333: return VA_HALF_SWIZZLES_8_BIT_B33;
   case BI_SWIZZLE_B0011: return VA_HALF_SWIZZLES_8_BIT_B01;
   case BI_SWIZZLE_B2233: return VA_HALF_SWIZZLES_8_BIT_B23;
   case BI_SWIZZLE_B0022: return VA_HALF_SWIZZLES_8_BIT_B02;
   default: unreachable("todo: more halfswizzles");
   }
}

static enum va_lanes_8_bit
va_pack_shift_lanes(enum bi_swizzle swz)
{
   switch (swz) {
   case BI_SWIZZLE_H01:    return VA_LANES_8_BIT_B02;
   case BI_SWIZZLE_B0000:  return VA_LANES_8_BIT_B00;
   default: unreachable("todo: more shifts");
   }
}

static enum va_combine
va_pack_combine(enum bi_swizzle swz)
{
   switch (swz) {
   case BI_SWIZZLE_H01: return VA_COMBINE_NONE;
   case BI_SWIZZLE_H00: return VA_COMBINE_H0;
   case BI_SWIZZLE_H11: return VA_COMBINE_H1;
   default: unreachable("Invalid branch lane");
   }
}

static enum va_source_format
va_pack_source_format(const bi_instr *I)
{
   switch (I->register_format) {
   case BI_REGISTER_FORMAT_AUTO:
   case BI_REGISTER_FORMAT_S32:
   case BI_REGISTER_FORMAT_U32: return VA_SOURCE_FORMAT_SRC_FLAT32;
   case BI_REGISTER_FORMAT_F32: return VA_SOURCE_FORMAT_SRC_F32;
   case BI_REGISTER_FORMAT_F16: return VA_SOURCE_FORMAT_SRC_F16;
   default: unreachable("unhandled register format");
   }
}

static uint64_t
va_pack_alu(const bi_instr *I)
{
   struct va_opcode_info info = valhall_opcodes[I->op];
   uint64_t hex = 0;

   switch (I->op) {
   /* Add FREXP flags */
   case BI_OPCODE_FREXPE_F32:
   case BI_OPCODE_FREXPE_V2F16:
   case BI_OPCODE_FREXPM_F32:
   case BI_OPCODE_FREXPM_V2F16:
      if (I->sqrt) hex |= 1ull << 24;
      if (I->log) hex |= 1ull << 25;
      break;

   /* Add mux type */
   case BI_OPCODE_MUX_I32:
   case BI_OPCODE_MUX_V2I16:
   case BI_OPCODE_MUX_V4I8:
      hex |= (uint64_t) I->mux << 32;
      break;

   /* Add .eq flag */
   case BI_OPCODE_BRANCHZ_I16:
   case BI_OPCODE_BRANCHZI:
      assert(I->cmpf == BI_CMPF_EQ || I->cmpf == BI_CMPF_NE);

      if (I->cmpf == BI_CMPF_EQ) hex |= (1ull << 36);

      if (I->op == BI_OPCODE_BRANCHZI)
         hex |= (0x1ull << 40); /* Absolute */
      else
         hex |= ((uint64_t) I->branch_offset & BITFIELD_MASK(27)) << 8;

      break;

   /* Add arithmetic flag */
   case BI_OPCODE_RSHIFT_AND_I32:
   case BI_OPCODE_RSHIFT_AND_V2I16:
   case BI_OPCODE_RSHIFT_AND_V4I8:
   case BI_OPCODE_RSHIFT_OR_I32:
   case BI_OPCODE_RSHIFT_OR_V2I16:
   case BI_OPCODE_RSHIFT_OR_V4I8:
   case BI_OPCODE_RSHIFT_XOR_I32:
   case BI_OPCODE_RSHIFT_XOR_V2I16:
   case BI_OPCODE_RSHIFT_XOR_V4I8:
      hex |= (uint64_t) I->arithmetic << 34;
      break;

   case BI_OPCODE_LEA_BUF_IMM:
      /* Buffer table index */
      hex |= 0xD << 8;
      break;

   case BI_OPCODE_LEA_ATTR_IMM:
      hex |= ((uint64_t) I->table) << 16;
      hex |= ((uint64_t) I->attribute_index) << 20;
      break;

   case BI_OPCODE_IADD_IMM_I32:
   case BI_OPCODE_IADD_IMM_V2I16:
   case BI_OPCODE_IADD_IMM_V4I8:
   case BI_OPCODE_FADD_IMM_F32:
   case BI_OPCODE_FADD_IMM_V2F16:
      hex |= ((uint64_t) I->index) << 8;
      break;

   case BI_OPCODE_CLPER_I32:
      hex |= ((uint64_t) I->inactive_result) << 22;
      hex |= ((uint64_t) I->lane_op) << 32;
      hex |= ((uint64_t) I->subgroup) << 36;
      break;

   case BI_OPCODE_LD_VAR:
   case BI_OPCODE_LD_VAR_FLAT:
   case BI_OPCODE_LD_VAR_IMM:
   case BI_OPCODE_LD_VAR_FLAT_IMM:
   case BI_OPCODE_LD_VAR_BUF_F16:
   case BI_OPCODE_LD_VAR_BUF_F32:
   case BI_OPCODE_LD_VAR_BUF_IMM_F16:
   case BI_OPCODE_LD_VAR_BUF_IMM_F32:
   case BI_OPCODE_LD_VAR_SPECIAL:
      if (I->op == BI_OPCODE_LD_VAR_SPECIAL)
         hex |= ((uint64_t) I->varying_name) << 12; /* instead of index */
      else if (I->op == BI_OPCODE_LD_VAR_BUF_IMM_F16 ||
               I->op == BI_OPCODE_LD_VAR_BUF_IMM_F32) {
         hex |= ((uint64_t) I->index) << 16;
      } else if (I->op == BI_OPCODE_LD_VAR_IMM ||
                 I->op == BI_OPCODE_LD_VAR_FLAT_IMM) {
         hex |= ((uint64_t) I->table) << 8;
         hex |= ((uint64_t) I->index) << 12;
      }

      hex |= ((uint64_t) va_pack_source_format(I)) << 24;
      hex |= ((uint64_t) I->update) << 36;
      hex |= ((uint64_t) I->sample) << 38;
      break;

   case BI_OPCODE_LD_ATTR_IMM:
      hex |= ((uint64_t) I->table) << 16;
      hex |= ((uint64_t) I->attribute_index) << 20;
      break;

   case BI_OPCODE_LD_TEX_IMM:
   case BI_OPCODE_LEA_TEX_IMM:
      hex |= ((uint64_t) I->table) << 16;
      hex |= ((uint64_t) I->texture_index) << 20;
      break;

   case BI_OPCODE_ZS_EMIT:
      if (I->stencil) hex |= (1 << 24);
      if (I->z) hex |= (1 << 25);
      break;

   default:
      break;
   }

   /* FMA_RSCALE.f32 special modes treated as extra opcodes */
   if (I->op == BI_OPCODE_FMA_RSCALE_F32) {
      assert(I->special < 4);
      hex |= ((uint64_t) I->special) << 48;
   }

   /* Add the normal destination or a placeholder.  Staging destinations are
    * added elsewhere, as they require special handling for control fields.
    */
   if (info.has_dest && info.nr_staging_dests == 0) {
      hex |= (uint64_t) va_pack_dest(I->dest[0]) << 40;
   } else if (info.nr_staging_dests == 0 && info.nr_staging_srcs == 0) {
      assert(bi_is_null(I->dest[0]));
      hex |= 0xC0ull << 40; /* Placeholder */
   }

   bool swap12 = va_swap_12(I->op);

   /* First src is staging if we read, skip it when packing sources */
   unsigned src_offset = bi_opcode_props[I->op].sr_read ? 1 : 0;

   for (unsigned i = 0; i < info.nr_srcs; ++i) {
      unsigned logical_i = (swap12 && i == 1) ? 2 : (swap12 && i == 2) ? 1 : i;

      struct va_src_info src_info = info.srcs[i];
      enum va_size size = src_info.size;

      bi_index src = I->src[logical_i + src_offset];
      hex |= (uint64_t) va_pack_src(src) << (8 * i);

      if (src_info.notted) {
         if (src.neg) hex |= (1ull << 35);
      } else if (src_info.absneg) {
         unsigned neg_offs = 32 + 2 + ((2 - i) * 2);
         unsigned abs_offs = 33 + 2 + ((2 - i) * 2);

         if (src.neg) hex |= 1ull << neg_offs;
         if (src.abs) hex |= 1ull << abs_offs;
      } else {
         assert(!src.neg && "Unexpected negate");
         assert(!src.abs && "Unexpected absolute value");
      }

      if (src_info.swizzle) {
         unsigned offs = 24 + ((2 - i) * 2);
         unsigned S = src.swizzle;
         assert(size == VA_SIZE_16 || size == VA_SIZE_32);

         uint64_t v = (size == VA_SIZE_32 ? va_pack_widen_f32(S) : va_pack_swizzle_f16(S));
         hex |= v << offs;
      } else if (src_info.widen) {
         unsigned offs = (i == 1) ? 26 : 36;
         hex |= (uint64_t) va_pack_widen(src.swizzle, src_info.size) << offs;
      } else if (src_info.lane) {
         unsigned offs = 28;
         assert(i == 0 && "todo: MKVEC");
         if (src_info.size == VA_SIZE_16) {
            hex |= (src.swizzle == BI_SWIZZLE_H11 ? 1 : 0) << offs;
         } else if (I->op == BI_OPCODE_BRANCHZ_I16) {
            hex |= ((uint64_t) va_pack_combine(src.swizzle) << 37);
         } else {
            assert(src_info.size == VA_SIZE_8);
            unsigned comp = src.swizzle - BI_SWIZZLE_B0000;
            assert(comp < 4);
            hex |= (uint64_t) comp << offs;
         }
      } else if (src_info.lanes) {
         assert(src_info.size == VA_SIZE_8);
         assert(i == 1);
         hex |= (uint64_t) va_pack_shift_lanes(src.swizzle) << 26;
      } else if (src_info.combine) {
         /* Treat as swizzle, subgroup ops not yet supported */
         assert(src_info.size == VA_SIZE_32);
         assert(i == 0);
         hex |= (uint64_t) va_pack_widen_f32(src.swizzle) << 37;
      } else if (src_info.halfswizzle) {
         assert(src_info.size == VA_SIZE_8);
         assert(i == 0);
         hex |= (uint64_t) va_pack_halfswizzle(src.swizzle) << 36;
      } else {
         assert(src.swizzle == BI_SWIZZLE_H01 && "Unexpected swizzle");
      }
   }

   if (info.clamp) hex |= (uint64_t) I->clamp << 32;
   if (info.round_mode) hex |= (uint64_t) I->round << 30;
   if (info.condition) hex |= (uint64_t) I->cmpf << 32;
   if (info.result_type) hex |= (uint64_t) I->result_type << 30;

   return hex;
}

static uint64_t
va_pack_byte_offset(const bi_instr *I)
{
   int16_t offset = I->byte_offset;
   assert(offset == I->byte_offset && "offset overflow");

   uint16_t offset_as_u16 = offset;
   return ((uint64_t) offset_as_u16) << 8;
}

static uint64_t
va_pack_byte_offset_8(const bi_instr *I)
{
   uint8_t offset = I->byte_offset;
   assert(offset == I->byte_offset && "offset overflow");

   return ((uint64_t) offset) << 8;
}

static uint64_t
va_pack_load(const bi_instr *I, bool buffer_descriptor)
{
   const uint8_t load_lane_identity[8] = {
      VA_LOAD_LANE_8_BIT_B0,
      VA_LOAD_LANE_16_BIT_H0,
      VA_LOAD_LANE_24_BIT_IDENTITY,
      VA_LOAD_LANE_32_BIT_W0,
      VA_LOAD_LANE_48_BIT_IDENTITY,
      VA_LOAD_LANE_64_BIT_IDENTITY,
      VA_LOAD_LANE_96_BIT_IDENTITY,
      VA_LOAD_LANE_128_BIT_IDENTITY,
   };

   unsigned memory_size = (valhall_opcodes[I->op].exact >> 27) & 0x7;
   uint64_t hex = (uint64_t) load_lane_identity[memory_size] << 36;

   // unsigned
   hex |= (1ull << 39);

   if (!buffer_descriptor)
      hex |= va_pack_byte_offset(I);

   hex |= (uint64_t) va_pack_src(I->src[0]) << 0;

   if (buffer_descriptor)
      hex |= (uint64_t) va_pack_src(I->src[1]) << 8;

   return hex;
}

static uint64_t
va_pack_memory_access(const bi_instr *I)
{
   switch (I->seg) {
   case BI_SEG_TL:   return VA_MEMORY_ACCESS_FORCE;
   case BI_SEG_POS:  return VA_MEMORY_ACCESS_ISTREAM;
   case BI_SEG_VARY: return VA_MEMORY_ACCESS_ESTREAM;
   default:          return VA_MEMORY_ACCESS_NONE;
   }
}

static uint64_t
va_pack_store(const bi_instr *I)
{
   uint64_t hex = va_pack_memory_access(I) << 24;

   va_validate_register_pair(I, 1);
   hex |= (uint64_t) va_pack_src(I->src[1]) << 0;

   hex |= va_pack_byte_offset(I);

   return hex;
}

static enum va_lod_mode
va_pack_lod_mode(enum bi_va_lod_mode mode)
{
   switch (mode) {
   case BI_VA_LOD_MODE_ZERO_LOD:       return VA_LOD_MODE_ZERO;
   case BI_VA_LOD_MODE_COMPUTED_LOD:   return VA_LOD_MODE_COMPUTED;
   case BI_VA_LOD_MODE_EXPLICIT:       return VA_LOD_MODE_EXPLICIT;
   case BI_VA_LOD_MODE_COMPUTED_BIAS:  return VA_LOD_MODE_COMPUTED_BIAS;
   case BI_VA_LOD_MODE_GRDESC:         return VA_LOD_MODE_GRDESC;
   }

   unreachable("Invalid LOD mode");
}

static enum va_register_type
va_pack_register_type(enum bi_register_format regfmt)
{
   switch (regfmt) {
   case BI_REGISTER_FORMAT_F16:
   case BI_REGISTER_FORMAT_F32:
      return VA_REGISTER_TYPE_F;

   case BI_REGISTER_FORMAT_U16:
   case BI_REGISTER_FORMAT_U32:
      return VA_REGISTER_TYPE_U;

   case BI_REGISTER_FORMAT_S16:
   case BI_REGISTER_FORMAT_S32:
      return VA_REGISTER_TYPE_S;

   default:
      unreachable("Invalid register format");
   }
}

static enum va_register_format
va_pack_register_format(const bi_instr *I)
{
   switch (I->register_format) {
   case BI_REGISTER_FORMAT_AUTO: return VA_REGISTER_FORMAT_AUTO;
   case BI_REGISTER_FORMAT_F32:  return VA_REGISTER_FORMAT_F32;
   case BI_REGISTER_FORMAT_F16:  return VA_REGISTER_FORMAT_F16;
   case BI_REGISTER_FORMAT_S32:  return VA_REGISTER_FORMAT_S32;
   case BI_REGISTER_FORMAT_S16:  return VA_REGISTER_FORMAT_S16;
   case BI_REGISTER_FORMAT_U32:  return VA_REGISTER_FORMAT_U32;
   case BI_REGISTER_FORMAT_U16:  return VA_REGISTER_FORMAT_U16;
   default: unreachable("unhandled register format");
   }
}

uint64_t
va_pack_instr(const bi_instr *I)
{
   struct va_opcode_info info = valhall_opcodes[I->op];

   uint64_t hex = info.exact | (((uint64_t) I->flow) << 59);
   hex |= ((uint64_t) va_select_fau_page(I)) << 57;

   if (info.slot) {
      uint64_t slot = (I->op == BI_OPCODE_BARRIER) ? 7 : 0;
      hex |= (slot << 30);
   }

   if (info.sr_count) {
      bool read = bi_opcode_props[I->op].sr_read;
      bi_index sr = read ? I->src[0] : I->dest[0];

      unsigned count = read ?
         bi_count_read_registers(I, 0) :
         bi_count_write_registers(I, 0);

      hex |= ((uint64_t) count << 33);
      hex |= (uint64_t) va_pack_reg(sr) << 40;
      hex |= ((uint64_t) info.sr_control << 46);
   }

   if (info.sr_write_count) {
      hex |= ((uint64_t) bi_count_write_registers(I, 0) - 1) << 36;
      hex |= ((uint64_t) va_pack_reg(I->dest[0])) << 16;
   }

   if (info.vecsize)
      hex |= ((uint64_t) I->vecsize << 28);

   if (info.register_format)
      hex |= ((uint64_t) va_pack_register_format(I)) << 24;

   switch (I->op) {
   case BI_OPCODE_LOAD_I8:
   case BI_OPCODE_LOAD_I16:
   case BI_OPCODE_LOAD_I24:
   case BI_OPCODE_LOAD_I32:
   case BI_OPCODE_LOAD_I48:
   case BI_OPCODE_LOAD_I64:
   case BI_OPCODE_LOAD_I96:
   case BI_OPCODE_LOAD_I128:
      hex |= va_pack_load(I, false);
      break;

   case BI_OPCODE_LD_BUFFER_I8:
   case BI_OPCODE_LD_BUFFER_I16:
   case BI_OPCODE_LD_BUFFER_I24:
   case BI_OPCODE_LD_BUFFER_I32:
   case BI_OPCODE_LD_BUFFER_I48:
   case BI_OPCODE_LD_BUFFER_I64:
   case BI_OPCODE_LD_BUFFER_I96:
   case BI_OPCODE_LD_BUFFER_I128:
      hex |= va_pack_load(I, true);
      break;

   case BI_OPCODE_STORE_I8:
   case BI_OPCODE_STORE_I16:
   case BI_OPCODE_STORE_I24:
   case BI_OPCODE_STORE_I32:
   case BI_OPCODE_STORE_I48:
   case BI_OPCODE_STORE_I64:
   case BI_OPCODE_STORE_I96:
   case BI_OPCODE_STORE_I128:
      hex |= va_pack_store(I);
      break;

   case BI_OPCODE_ATOM1_RETURN_I32:
      /* Permit omitting the destination for plain ATOM1 */
      if (!bi_count_write_registers(I, 0)) {
         hex |= (0x40ull << 40); // fake read
      }

      /* 64-bit source */
      va_validate_register_pair(I, 0);
      hex |= (uint64_t) va_pack_src(I->src[0]) << 0;
      hex |= va_pack_byte_offset_8(I);
      hex |= ((uint64_t) va_pack_atom_opc_1(I->atom_opc)) << 22;
      break;

   case BI_OPCODE_ATOM_I32:
   case BI_OPCODE_ATOM_RETURN_I32:
      /* 64-bit source */
      va_validate_register_pair(I, 1);
      hex |= (uint64_t) va_pack_src(I->src[1]) << 0;
      hex |= va_pack_byte_offset_8(I);
      hex |= ((uint64_t) va_pack_atom_opc(I->atom_opc)) << 22;

      if (I->op == BI_OPCODE_ATOM_RETURN_I32)
         hex |= (0xc0ull << 40); // flags

      if (I->atom_opc == BI_ATOM_OPC_ACMPXCHG)
         hex |= (1 << 26); /* .compare */

      break;

   case BI_OPCODE_ST_CVT:
      /* Staging read */
      hex |= va_pack_store(I);

      /* Conversion descriptor */
      hex |= (uint64_t) va_pack_src(I->src[3]) << 16;
      break;

   case BI_OPCODE_BLEND:
   {
      /* Source 0 - Blend descriptor (64-bit) */
      hex |= ((uint64_t) va_pack_src(I->src[2])) << 0;

      /* Vaidate that it is a 64-bit register pair */
      assert(I->src[3].type == I->src[2].type);

      if (I->src[2].type == BI_INDEX_REGISTER) {
         assert(I->src[3].value & 1);
         assert(I->src[3].value == I->src[2].value + 1);
      } else {
         assert(I->src[3].offset & 1);
         assert(I->src[3].offset == I->src[2].offset + 1);
      }

      /* Target */
      assert((I->branch_offset & 0x7) == 0);
      hex |= ((I->branch_offset >> 3) << 8);

      /* Source 2 - coverage mask */
      hex |= ((uint64_t) va_pack_reg(I->src[1])) << 16;

      /* Vector size */
      unsigned vecsize = 4;
      hex |= ((uint64_t) (vecsize - 1) << 28);

      break;
   }

   case BI_OPCODE_TEX_SINGLE:
   case BI_OPCODE_TEX_FETCH:
   case BI_OPCODE_TEX_GATHER:
   {
      /* Image to read from */
      hex |= ((uint64_t) va_pack_src(I->src[1])) << 0;

      assert(!(I->op == BI_OPCODE_TEX_FETCH && I->shadow));

      if (I->array_enable) hex |= (1ull << 10);
      if (I->texel_offset) hex |= (1ull << 11);
      if (I->shadow) hex |= (1ull << 12);
      if (I->skip) hex |= (1ull << 39);
      if (!bi_is_regfmt_16(I->register_format)) hex |= (1ull << 46);

      if (I->op == BI_OPCODE_TEX_SINGLE) {
         assert(I->va_lod_mode < 8);
         hex |= ((uint64_t) va_pack_lod_mode(I->va_lod_mode)) << 13;
      }

      if (I->op == BI_OPCODE_TEX_GATHER) {
         if (I->integer_coordinates) hex |= (1 << 13);
         hex |= ((uint64_t) I->fetch_component) << 14;
      }

      hex |= (VA_WRITE_MASK_RGBA << 22);
      hex |= ((uint64_t) va_pack_register_type(I->register_format)) << 26;
      hex |= ((uint64_t) I->dimension) << 28;

      break;
   }

   default:
      if (!info.exact && I->op != BI_OPCODE_NOP) {
         bi_print_instr(I, stderr);
         fflush(stderr);
         unreachable("Opcode not packable on Valhall");
      }

      hex |= va_pack_alu(I);
      break;
   }

   return hex;
}

static bool
va_last_in_block(bi_block *block, bi_instr *I)
{
   return (I->link.next == &block->instructions);
}

static bool
va_should_return(bi_block *block, bi_instr *I)
{
   /* Don't return within a block */
   if (!va_last_in_block(block, I))
      return false;

   /* Don't return if we're succeeded by instructions */
   for (unsigned i = 0; i < ARRAY_SIZE(block->successors); ++i) {
      bi_block *succ = block->successors[i];

      if (succ && !bi_is_terminal_block(succ))
         return false;
   }

   return true;
}

static unsigned
va_instructions_in_block(bi_block *block)
{
   unsigned offset = 0;

   bi_foreach_instr_in_block(block, _) {
      offset++;
   }

   return offset;
}

/* Calculate branch_offset from a branch_target for a direct relative branch */

static void
va_lower_branch_target(bi_context *ctx, bi_block *start, bi_instr *I)
{
   /* Precondition: unlowered relative branch */
   bi_block *target = I->branch_target;
   assert(target != NULL);

   /* Signed since we might jump backwards */
   signed offset = 0;

   /* Determine if the target block is strictly greater in source order */
   bool forwards = target->name > start->name;

   if (forwards) {
      /* We have to jump through this block */
      bi_foreach_instr_in_block_from(start, _, I) {
         offset++;
      }

      /* We then need to jump over every following block until the target */
      bi_foreach_block_from(ctx, start, blk) {
         /* End just before the target */
         if (blk == target)
            break;

         /* Count other blocks */
         if (blk != start)
            offset += va_instructions_in_block(blk);
      }
   } else {
      /* Jump through the beginning of this block */
      bi_foreach_instr_in_block_from_rev(start, ins, I) {
         if (ins != I)
            offset--;
      }

      /* Jump over preceding blocks up to and including the target to get to
       * the beginning of the target */
      bi_foreach_block_from_rev(ctx, start, blk) {
         if (blk == start)
            continue;

         offset -= va_instructions_in_block(blk);

         /* End just after the target */
         if (blk == target)
            break;
      }
   }

   /* Offset is relative to the next instruction, so bias */
   offset--;

   /* Update the instruction */
   I->branch_offset = offset;
}

/*
 * Late lowering to insert blend shader calls after BLEND instructions. Required
 * to support blend shaders, so this pass may be omitted if it is known that
 * blend shaders are never used.
 *
 * This lowering runs late because it introduces control flow changes without
 * modifying the control flow graph. It hardcodes registers, meaning running
 * after RA makes sense. Finally, it hardcodes a manually sized instruction
 * sequence, requiring it to run after scheduling.
 *
 * As it is Valhall specific, running it as a pre-pack lowering is sensible.
 */
static void
va_lower_blend(bi_context *ctx)
{
   bool last_blend = true;

   /* Link register (ABI between fragment and blend shaders) */
   bi_index lr = bi_register(48);

   /* Program counter for *next* instruction */
   bi_index pc = bi_fau(BIR_FAU_PROGRAM_COUNTER, false);

   bi_foreach_instr_global_rev(ctx, I) {
      if (I->op != BI_OPCODE_BLEND)
         continue;

      bi_builder b = bi_init_builder(ctx, bi_after_instr(I));

      unsigned prolog_length = 2 * 8;

      if (last_blend)
         bi_iadd_imm_i32_to(&b, lr, va_zero_lut(), 0);
      else
         bi_iadd_imm_i32_to(&b, lr, pc, prolog_length - 8);

      bi_branchzi(&b, va_zero_lut(), I->src[3], BI_CMPF_EQ);

      /* For fixed function: skip the prologue, or return */
      if (last_blend)
         I->flow = 0x7 | 0x8; /* .return */
      else
         I->branch_offset = prolog_length;

      /* Iterate backwards makes the last BLEND easy to identify */
      last_blend = false;
   }
}

/*
 * Add a flow control modifier to an instruction. There may be an existing flow
 * control modifier; if so, we need to add a NOP with the extra flow control
 * _after_ this instruction
 */
static void
va_add_flow(bi_context *ctx, bi_instr *I, enum va_flow flow)
{
   if (I->flow != VA_FLOW_NONE) {
      bi_builder b = bi_init_builder(ctx, bi_after_instr(I));
      I = bi_nop(&b);
   }

   I->flow = flow;
}

/*
 * Add flow control modifiers to the program. This is a stop gap until we have a
 * proper scheduler. For now, this should be conformant while doing little
 * optimization of message waits.
 */
static void
va_lower_flow_control(bi_context *ctx)
{
   bi_foreach_block(ctx, block) {
      bool block_reconverges = bi_reconverge_branches(block);

      bi_foreach_instr_in_block_safe(block, I) {
         /* If this instruction returns, there is nothing left to do. */
         if (va_should_return(block, I)) {
            I->flow = VA_FLOW_END;
            continue;
         }

         /* We may need to wait */
         if (I->op == BI_OPCODE_BARRIER)
            va_add_flow(ctx, I, VA_FLOW_WAIT);
         else if (valhall_opcodes[I->op].nr_staging_dests > 0 || I->op == BI_OPCODE_BLEND)
            va_add_flow(ctx, I, VA_FLOW_WAIT0);

         /* Lastly, we may need to reconverge. If we need reconvergence, it
          * has to be on the last instruction of the block. If we have to
          * generate a NOP for that reconverge, we need that to be last. So
          * this ordering is careful.
          */
         if (va_last_in_block(block, I) && block_reconverges)
            va_add_flow(ctx, I, VA_FLOW_RECONVERGE);


      }
   }
}

void
bi_pack_valhall(bi_context *ctx, struct util_dynarray *emission)
{
   unsigned orig_size = emission->size;

   va_validate(stderr, ctx);

   /* Late lowering */
   if (ctx->stage == MESA_SHADER_FRAGMENT && !ctx->inputs->is_blend)
      va_lower_blend(ctx);

   va_lower_flow_control(ctx);

   bi_foreach_block(ctx, block) {
      bi_foreach_instr_in_block(block, I) {
         if (I->op == BI_OPCODE_BRANCHZ_I16)
            va_lower_branch_target(ctx, block, I);

         uint64_t hex = va_pack_instr(I);
         util_dynarray_append(emission, uint64_t, hex);
      }
   }

   /* Pad with zeroes, but keep empty programs empty so they may be omitted
    * altogether. Failing to do this would result in a program containing only
    * zeroes, which is invalid and will raise an encoding fault.
    *
    * Pad an extra 16 byte (one instruction) to separate primary and secondary
    * shader disassembles. This is not strictly necessary, but it's a good
    * practice. 128 bytes is the optimal program alignment on Trym, so pad
    * secondary shaders up to 128 bytes. This may help the instruction cache.
    */
   if (orig_size != emission->size) {
      unsigned aligned = ALIGN_POT(emission->size + 16, 128);
      unsigned count = aligned - emission->size;

      memset(util_dynarray_grow(emission, uint8_t, count), 0, count);
   }
}
