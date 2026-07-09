/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "dev/intel_device_info.h"

#include "gen_enums.h"
#include "gen_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A default value for constants that will be patched at run-time.
 * We pick an arbitrary value that prevents instruction compaction.
 */
#define GEN_UNCOMPACTABLE_PATCH_IMM 0x4a7cc037

unsigned gen_inst_num_sources(const struct intel_device_info *devinfo,
                              const gen_inst *inst);
bool gen_has_uip(gen_opcode op);
bool gen_has_jip(gen_opcode op);

uint32_t gen_swsb_encode(const struct intel_device_info *devinfo,
                         gen_swsb swsb, gen_opcode op);

gen_swsb gen_swsb_decode(const struct intel_device_info *devinfo,
                         bool is_unordered, uint32_t x, gen_opcode op);

/**
 * Return the source index for JIP (Jump Index Pointer) in a branch
 * instruction, or -1 if the instruction has no JIP.
 *
 * For all branch instructions, JIP is always src[0].
 * In the hardware encoding, JIP occupies the Src1 bit region
 * (IMM_LO_32 / BRANCH_JIP at bits [127:96]).
 */
static inline int
gen_inst_jip_src_index(gen_opcode op)
{
   return gen_has_jip(op) ? 0 : -1;
}

/**
 * Return the source index for UIP (Update Index Pointer) in a branch
 * instruction, or -1 if the instruction has no UIP.
 *
 * For all branch instructions with UIP, it is always src[1].
 * In the hardware encoding, UIP occupies the Src0 bit region
 * (IMM_HI_32 / BRANCH_UIP at bits [95:64]).
 */
static inline int
gen_inst_uip_src_index(gen_opcode op)
{
   return gen_has_uip(op) ? 1 : -1;
}
bool gen_has_branch_ctrl(gen_opcode opcode);

static inline bool
gen_type_is_float(gen_reg_type t)
{
   return (t & GEN_TYPE_BASE_MASK) == GEN_TYPE_BASE_FLOAT;
}

static inline bool
gen_type_is_bfloat(gen_reg_type t)
{
   return (t & GEN_TYPE_BASE_MASK) == GEN_TYPE_BASE_BFLOAT;
}

static inline bool
gen_type_is_float_or_bfloat(gen_reg_type t)
{
   return gen_type_is_float(t) || gen_type_is_bfloat(t);
}

static inline bool
gen_type_is_uint(gen_reg_type t)
{
   return (t & GEN_TYPE_BASE_MASK) == GEN_TYPE_BASE_UINT;
}

static inline bool
gen_type_is_sint(gen_reg_type t)
{
   return (t & GEN_TYPE_BASE_MASK) == GEN_TYPE_BASE_SINT;
}

static inline bool
gen_type_is_int(gen_reg_type t)
{
   return gen_type_is_uint(t) || gen_type_is_sint(t);
}

static inline bool
gen_type_is_vector_imm(gen_reg_type t)
{
   return t & GEN_TYPE_VECTOR;
}

static inline unsigned
gen_type_size_bits(gen_reg_type t)
{
   /* [U]V components are 4-bit, but HW unpacks them to 16-bit.
    * Similarly, VF is expanded to 32-bit.
    */
   return 8 << (t & GEN_TYPE_SIZE_MASK);
}

static inline unsigned
gen_type_size_bytes(gen_reg_type t)
{
   return gen_type_size_bits(t) / 8;
}

static inline gen_operand
gen_grf(unsigned nr, unsigned subnr)
{
   gen_operand op = {};
   op.file = GEN_GRF;
   op.region.vstride = 1;
   op.region.width = 1;
   op.nr = nr;
   op.subnr = (uint8_t)subnr;
   return op;
}

static inline gen_operand
gen_arf(unsigned nr, unsigned subnr)
{
   gen_operand op = {};
   op.file = GEN_ARF;
   op.region.width = 1;
   op.nr = nr;
   op.subnr = (uint8_t)subnr;
   return op;
}

static inline gen_operand
gen_null(void)
{
   return gen_arf(GEN_ARF_NULL, 0);
}

static inline gen_operand
gen_address(unsigned subnr)
{
   gen_operand op = gen_arf(GEN_ARF_ADDRESS, subnr * 2);
   op.type = GEN_TYPE_UW;
   return op;
}

static inline gen_operand
gen_accumulator(unsigned nr)
{
   return gen_arf(GEN_ARF_ACCUMULATOR + nr, 0);
}

static inline gen_operand
gen_flag(unsigned subnr)
{
   gen_operand op = gen_arf(GEN_ARF_FLAG + subnr / 2, (subnr % 2) * 2);
   op.type = GEN_TYPE_UW;
   return op;
}

static inline gen_operand
gen_imm(gen_reg_type type, uint64_t imm)
{
   if (gen_type_size_bits(type) == 16 && !gen_type_is_vector_imm(type))
      imm = (imm & 0xffff) | ((imm & 0xffff) << 16);

   gen_operand op = {};
   op.file = GEN_IMM;
   op.type = type;
   op.region.width = 1;
   op.imm = imm;
   return op;
}

static inline gen_operand gen_imm_ub(uint8_t v)  { return gen_imm(GEN_TYPE_UB, v); }
static inline gen_operand gen_imm_b(int8_t v)    { return gen_imm(GEN_TYPE_B,  (uint8_t)v); }
static inline gen_operand gen_imm_uw(uint16_t v) { return gen_imm(GEN_TYPE_UW, v); }
static inline gen_operand gen_imm_w(int16_t v)   { return gen_imm(GEN_TYPE_W,  (uint16_t)v); }
static inline gen_operand gen_imm_ud(uint32_t v) { return gen_imm(GEN_TYPE_UD, v); }
static inline gen_operand gen_imm_d(int32_t v)   { return gen_imm(GEN_TYPE_D,  (uint32_t)v); }
static inline gen_operand gen_imm_uq(uint64_t v) { return gen_imm(GEN_TYPE_UQ, v); }
static inline gen_operand gen_imm_q(int64_t v)   { return gen_imm(GEN_TYPE_Q,  (uint64_t)v); }
static inline gen_operand gen_imm_v(uint32_t v)  { return gen_imm(GEN_TYPE_V,  v); }
static inline gen_operand gen_imm_uv(uint32_t v) { return gen_imm(GEN_TYPE_UV, v); }
static inline gen_operand gen_imm_vf(uint32_t v) { return gen_imm(GEN_TYPE_VF, v); }

static inline gen_operand
gen_retype(gen_operand op, gen_reg_type type)
{
   op.type = type;
   return op;
}

static inline gen_operand
gen_restride(gen_operand op, uint8_t v, uint8_t w, uint8_t h)
{
   op.region = (gen_region) { v, w, h };
   return op;
}

static inline gen_operand
gen_byte_offset(const struct intel_device_info *devinfo,
                gen_operand op, unsigned bytes)
{
   if (op.indirect) {
      op.addr_imm += bytes;
      return op;
   }

   switch (op.file) {
   case GEN_BAD_FILE:
      break;

   case GEN_GRF: {
      const unsigned reg_size = devinfo->grf_size;
      unsigned subnr = op.subnr + bytes;
      op.nr += subnr / reg_size;
      op.subnr = subnr % reg_size;
      break;
   }

   case GEN_ARF: {
      /* The accumulator is as wide as a GRF; other ARFs are 32 bytes. */
      const unsigned reg_size =
         op.nr >= GEN_ARF_ACCUMULATOR && op.nr < GEN_ARF_FLAG
            ? devinfo->grf_size : 32;
      unsigned subnr = op.subnr + bytes;
      op.nr += subnr / reg_size;
      op.subnr = subnr % reg_size;
      break;
   }

   case GEN_IMM:
      assert(bytes == 0);
      break;
   }

   return op;
}

static inline gen_operand
gen_element_offset(const struct intel_device_info *devinfo,
                   gen_operand op, unsigned delta)
{
   return gen_byte_offset(devinfo, op, delta * gen_type_size_bytes(op.type));
}

/*
 * Return the stride between channels of the specified register in
 * byte units, or ~0u if the region cannot be represented with a
 * single one-dimensional stride.
 */
static inline unsigned
gen_byte_stride(gen_operand op)
{
   if (op.file == GEN_ARF && op.nr == GEN_ARF_NULL)
      return 4; /* brw_null_reg() uses type F with <8;8,1> */

   if (op.file == GEN_IMM)
      return 0;

   if (op.region.width == 1) {
      return op.region.vstride * gen_type_size_bytes(op.type);
   } else if (op.region.hstride * op.region.width == op.region.vstride) {
      return op.region.hstride * gen_type_size_bytes(op.type);
   } else {
      return ~0u;
   }
}

static inline gen_operand
gen_subscript(const struct intel_device_info *devinfo,
              gen_operand op, gen_reg_type type, unsigned i)
{
   const unsigned old_size = gen_type_size_bytes(op.type);
   const unsigned new_size = gen_type_size_bytes(type);

   assert((i + 1) * new_size <= old_size);

   if (op.file == GEN_IMM) {
      const unsigned bit_size = gen_type_size_bits(type);
      op.imm >>= i * bit_size;
      op.imm &= BITFIELD64_MASK(bit_size);
      if (bit_size <= 16)
         op.imm |= op.imm << 16;
      op.type = type;
      return op;
   }

   const unsigned ratio = old_size / new_size;
   if (op.region.hstride)
      op.region.hstride *= ratio;
   if (op.region.vstride && op.region.vstride != GEN_VSTRIDE_ONE_DIMENSIONAL)
      op.region.vstride *= ratio;

   op.type = type;
   return gen_byte_offset(devinfo, op, i * new_size);
}

static inline unsigned
gen_swizzle4(unsigned x, unsigned y, unsigned z, unsigned w)
{
   return (x << 0) | (y << 2) | (z << 4) | (w << 6);
}

#ifndef INTEL_MASK
#define INTEL_MASK(high, low) (((1u<<((high)-(low)+1))-1)<<(low))
#define SET_BITS(value, high, low)                                      \
   ({                                                                   \
      const uint32_t fieldval = (uint32_t)(value) << (low);             \
      assert((fieldval & ~INTEL_MASK(high, low)) == 0);                 \
      fieldval & INTEL_MASK(high, low);                                 \
   })

#define GET_BITS(data, high, low) ((data & INTEL_MASK((high), (low))) >> (low))
#endif

static inline bool
lsc_opcode_has_cmask(enum lsc_opcode opcode)
{
   return opcode == LSC_OP_LOAD_CMASK || opcode == LSC_OP_STORE_CMASK ||
          opcode == LSC_OP_LOAD_CMASK_MSRT ||
          opcode == LSC_OP_STORE_CMASK_MSRT;
}

static inline bool
lsc_opcode_has_transpose(enum lsc_opcode opcode)
{
   return opcode == LSC_OP_LOAD || opcode == LSC_OP_STORE;
}

static inline bool
lsc_opcode_is_store(enum lsc_opcode opcode)
{
   return opcode == LSC_OP_STORE ||
          opcode == LSC_OP_STORE_CMASK ||
          opcode == LSC_OP_STORE_CMASK_MSRT;
}

static inline bool
lsc_opcode_is_atomic(enum lsc_opcode opcode)
{
   switch (opcode) {
   case LSC_OP_ATOMIC_INC:
   case LSC_OP_ATOMIC_DEC:
   case LSC_OP_ATOMIC_LOAD:
   case LSC_OP_ATOMIC_STORE:
   case LSC_OP_ATOMIC_ADD:
   case LSC_OP_ATOMIC_SUB:
   case LSC_OP_ATOMIC_MIN:
   case LSC_OP_ATOMIC_MAX:
   case LSC_OP_ATOMIC_UMIN:
   case LSC_OP_ATOMIC_UMAX:
   case LSC_OP_ATOMIC_CMPXCHG:
   case LSC_OP_ATOMIC_FADD:
   case LSC_OP_ATOMIC_FSUB:
   case LSC_OP_ATOMIC_FMIN:
   case LSC_OP_ATOMIC_FMAX:
   case LSC_OP_ATOMIC_FCMPXCHG:
   case LSC_OP_ATOMIC_AND:
   case LSC_OP_ATOMIC_OR:
   case LSC_OP_ATOMIC_XOR:
      return true;
   default:
      return false;
   }
}

static inline bool
lsc_opcode_is_atomic_float(enum lsc_opcode opcode)
{
   switch (opcode) {
   case LSC_OP_ATOMIC_FADD:
   case LSC_OP_ATOMIC_FSUB:
   case LSC_OP_ATOMIC_FMIN:
   case LSC_OP_ATOMIC_FMAX:
   case LSC_OP_ATOMIC_FCMPXCHG:
      return true;
   default:
      return false;
   }
}

static inline unsigned
lsc_op_num_data_values(unsigned _op)
{
   const enum lsc_opcode op = (enum lsc_opcode)_op;

   switch (op) {
   case LSC_OP_ATOMIC_CMPXCHG:
   case LSC_OP_ATOMIC_FCMPXCHG:
      return 2;
   case LSC_OP_ATOMIC_INC:
   case LSC_OP_ATOMIC_DEC:
   case LSC_OP_ATOMIC_LOAD:
   case LSC_OP_LOAD:
   case LSC_OP_LOAD_CMASK:
   case LSC_OP_FENCE:
   case LSC_OP_LOAD_CMASK_MSRT:
      return 0;
   default:
      return 1;
   }
}

static inline uint32_t
lsc_data_size_bytes(enum lsc_data_size data_size)
{
   switch (data_size) {
   case LSC_DATA_SIZE_D8:      return 1;
   case LSC_DATA_SIZE_D16:     return 2;
   case LSC_DATA_SIZE_D32:
   case LSC_DATA_SIZE_D8U32:
   case LSC_DATA_SIZE_D16U32:
   case LSC_DATA_SIZE_D16BF32: return 4;
   case LSC_DATA_SIZE_D64:     return 8;
   default:
      assert(!"Unsupported LSC data size");
      return 0;
   }
}

static inline uint32_t
lsc_addr_size_bytes(enum lsc_addr_size addr_size)
{
   switch (addr_size) {
   case LSC_ADDR_SIZE_A16: return 2;
   case LSC_ADDR_SIZE_A32: return 4;
   case LSC_ADDR_SIZE_A64: return 8;
   default:
      assert(!"Unsupported LSC address size");
      return 0;
   }
}

static inline uint32_t
lsc_vector_length(enum lsc_vect_size vect_size)
{
   switch (vect_size) {
   case LSC_VECT_SIZE_V1:  return 1;
   case LSC_VECT_SIZE_V2:  return 2;
   case LSC_VECT_SIZE_V3:  return 3;
   case LSC_VECT_SIZE_V4:  return 4;
   case LSC_VECT_SIZE_V8:  return 8;
   case LSC_VECT_SIZE_V16: return 16;
   case LSC_VECT_SIZE_V32: return 32;
   case LSC_VECT_SIZE_V64: return 64;
   default:
      assert(!"Unsupported LSC vector size");
      return 0;
   }
}

static inline enum lsc_vect_size
lsc_vect_size(unsigned vect_size)
{
   switch (vect_size) {
   case 1:  return LSC_VECT_SIZE_V1;
   case 2:  return LSC_VECT_SIZE_V2;
   case 3:  return LSC_VECT_SIZE_V3;
   case 4:  return LSC_VECT_SIZE_V4;
   case 8:  return LSC_VECT_SIZE_V8;
   case 16: return LSC_VECT_SIZE_V16;
   case 32: return LSC_VECT_SIZE_V32;
   case 64: return LSC_VECT_SIZE_V64;
   default:
      assert(!"Unsupported LSC vector size");
      return LSC_VECT_SIZE_V1;
   }
}

static inline uint32_t
gen_message_desc_encode(const struct intel_device_info *devinfo,
                        const gen_message_desc *desc)
{
   (void)devinfo;
   return SET_BITS(desc->msg_length, 28, 25) |
          SET_BITS(desc->response_length, 24, 20) |
          SET_BITS(desc->header_present, 19, 19);
}

static inline gen_message_desc
gen_message_desc_decode(const struct intel_device_info *devinfo,
                        uint32_t raw_desc)
{
   (void)devinfo;
   gen_message_desc desc = {};
   desc.msg_length = GET_BITS(raw_desc, 28, 25);
   desc.response_length = GET_BITS(raw_desc, 24, 20);
   desc.header_present = GET_BITS(raw_desc, 19, 19);
   return desc;
}

static inline uint32_t
gen_message_ex_desc(const struct intel_device_info *devinfo,
                    unsigned ex_msg_length)
{
   return devinfo->ver >= 20 ?
      SET_BITS(ex_msg_length, 10, 6) :
      SET_BITS(ex_msg_length, 9, 6);
}

static inline unsigned
gen_message_ex_desc_ex_mlen(const struct intel_device_info *devinfo,
                            uint32_t raw_ex_desc)
{
   return devinfo->ver >= 20 ?
      GET_BITS(raw_ex_desc, 10, 6) :
      GET_BITS(raw_ex_desc, 9, 6);
}

static inline enum lsc_opcode
lsc_msg_desc_opcode(const struct intel_device_info *devinfo, uint32_t desc)
{
   assert(devinfo->has_lsc);
   return (enum lsc_opcode)GET_BITS(desc, 5, 0);
}

static inline enum lsc_addr_size
lsc_msg_desc_addr_size(const struct intel_device_info *devinfo, uint32_t desc)
{
   assert(devinfo->has_lsc);
   return (enum lsc_addr_size)GET_BITS(desc, 8, 7);
}

static inline enum lsc_data_size
lsc_msg_desc_data_size(const struct intel_device_info *devinfo, uint32_t desc)
{
   assert(devinfo->has_lsc);
   return (enum lsc_data_size)GET_BITS(desc, 11, 9);
}

static inline enum lsc_vect_size
lsc_msg_desc_vect_size(const struct intel_device_info *devinfo, uint32_t desc)
{
   assert(devinfo->has_lsc);
   assert(!lsc_opcode_has_cmask(lsc_msg_desc_opcode(devinfo, desc)));
   return (enum lsc_vect_size)GET_BITS(desc, 14, 12);
}

static inline enum lsc_cmask
lsc_msg_desc_cmask(const struct intel_device_info *devinfo, uint32_t desc)
{
   assert(devinfo->has_lsc);
   assert(lsc_opcode_has_cmask(lsc_msg_desc_opcode(devinfo, desc)));
   return (enum lsc_cmask)GET_BITS(desc, 15, 12);
}

static inline bool
lsc_msg_desc_transpose(const struct intel_device_info *devinfo, uint32_t desc)
{
   assert(devinfo->has_lsc);
   return GET_BITS(desc, 15, 15);
}

static inline unsigned
lsc_msg_desc_cache_ctrl(const struct intel_device_info *devinfo, uint32_t desc)
{
   assert(devinfo->has_lsc);
   return devinfo->ver >= 20 ? GET_BITS(desc, 19, 16) : GET_BITS(desc, 19, 17);
}

static inline enum lsc_addr_surface_type
lsc_msg_desc_addr_type(const struct intel_device_info *devinfo, uint32_t desc)
{
   assert(devinfo->has_lsc);
   return (enum lsc_addr_surface_type)GET_BITS(desc, 30, 29);
}

static inline uint32_t
lsc_fence_msg_desc(const struct intel_device_info *devinfo,
                   enum lsc_fence_scope scope,
                   enum lsc_flush_type flush_type,
                   bool route_to_lsc)
{
   assert(devinfo->has_lsc);

   return SET_BITS(LSC_OP_FENCE, 5, 0) |
          SET_BITS(LSC_ADDR_SIZE_A32, 8, 7) |
          SET_BITS(scope, 11, 9) |
          SET_BITS(flush_type, 14, 12) |
          SET_BITS(route_to_lsc, 18, 18) |
          SET_BITS(LSC_ADDR_SURFTYPE_FLAT, 30, 29);
}

static inline enum lsc_fence_scope
lsc_fence_msg_desc_scope(const struct intel_device_info *devinfo, uint32_t desc)
{
   assert(devinfo->has_lsc);
   return (enum lsc_fence_scope)GET_BITS(desc, 11, 9);
}

static inline enum lsc_flush_type
lsc_fence_msg_desc_flush_type(const struct intel_device_info *devinfo,
                              uint32_t desc)
{
   assert(devinfo->has_lsc);
   return (enum lsc_flush_type)GET_BITS(desc, 14, 12);
}

static inline enum lsc_backup_fence_routing
lsc_fence_msg_desc_backup_routing(const struct intel_device_info *devinfo,
                                  uint32_t desc)
{
   assert(devinfo->has_lsc);
   return (enum lsc_backup_fence_routing)GET_BITS(desc, 18, 18);
}

/**
 * Construct a scheduling annotation with a single RegDist dependency.  This
 * synchronizes with the completion of the d-th previous in-order instruction.
 * The index is one-based, zero causes a no-op gen_swsb to be constructed.
 */
static inline gen_swsb
gen_swsb_regdist(unsigned d)
{
   const gen_swsb swsb = { d, (gen_pipe)(d ? GEN_PIPE_ALL : GEN_PIPE_NONE) };
   assert(swsb.regdist == d);
   return swsb;
}

/**
 * Construct a scheduling annotation that synchronizes with the specified SBID
 * token.
 */
static inline gen_swsb
gen_swsb_sbid(gen_sbid_mode mode, unsigned sbid)
{
   const gen_swsb swsb = { 0, GEN_PIPE_NONE, sbid, mode };
   assert(swsb.sbid == sbid);
   return swsb;
}

/**
 * Construct a no-op scheduling annotation.
 */
static inline gen_swsb
gen_swsb_null(void)
{
   return gen_swsb_regdist(0);
}

/**
 * Return a scheduling annotation that allocates the same SBID synchronization
 * token as \p swsb.  In addition it will synchronize against a previous
 * in-order instruction if \p regdist is non-zero.
 */
static inline gen_swsb
gen_swsb_dst_dep(gen_swsb swsb, unsigned regdist)
{
   swsb.regdist = regdist;
   swsb.mode = (gen_sbid_mode)(swsb.mode & GEN_SBID_SET);
   swsb.pipe = regdist ? GEN_PIPE_ALL : GEN_PIPE_NONE;
   return swsb;
}

/**
 * Return a scheduling annotation that synchronizes against the same SBID and
 * RegDist dependencies as \p swsb, but doesn't allocate any SBID token.
 */
static inline gen_swsb
gen_swsb_src_dep(gen_swsb swsb)
{
   swsb.mode = (gen_sbid_mode)(swsb.mode & (GEN_SBID_SRC | GEN_SBID_DST));
   return swsb;
}

static inline int
gen_inst_send_src0_len(const gen_inst *inst)
{
   if (inst->send.desc_is_reg)
      return -1;
   return (inst->send.desc_imm >> 25) & 0xF;
}

/*
 * Destination-register length field (rlen) from the generic message
 * descriptor, in reg_unit-scaled form (i.e. matching what .send mnemonics
 * print as ':N'). Returns -1 when the descriptor is indirect.
 */
static inline int
gen_inst_send_dst_len(const gen_inst *inst)
{
   if (inst->send.desc_is_reg)
      return -1;
   return (inst->send.desc_imm >> 20) & 0x1F;
}

static inline int
gen_inst_send_src1_len(const struct intel_device_info *devinfo,
                       const gen_inst *inst)
{
   if (inst->send.src1_len)
      return inst->send.src1_len;
   if (inst->send.ex_desc_is_reg)
      return -1;
   return gen_message_ex_desc_ex_mlen(devinfo, inst->send.ex_desc_imm);
}

static inline gen_condition
gen_condition_swap_sources(gen_condition mod)
{
   switch (mod) {
   case GEN_CONDITION_GT: return GEN_CONDITION_LT;
   case GEN_CONDITION_LT: return GEN_CONDITION_GT;
   case GEN_CONDITION_GE: return GEN_CONDITION_LE;
   case GEN_CONDITION_LE: return GEN_CONDITION_GE;
   default:               return mod;
   }
}

/* Rewrite the immediate of an already-encoded uncompacted MOV imm
 * instruction in place.  Used by shader relocation handling.
 *
 * See also GEN_UNCOMPACTABLE_PATCH_IMM.
 */
void gen_update_reloc_imm(const struct intel_device_info *devinfo,
                          void *inst, uint32_t value);

#ifdef __cplusplus
} /* extern "C" */
#endif
