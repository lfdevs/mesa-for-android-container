/* -*- c++ -*- */
/*
 * Copyright © 2010-2015 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef BRW_IR_FS_H
#define BRW_IR_FS_H

#include "brw_ir.h"
#include "brw_ir_allocator.h"

struct fs_inst : public exec_node {
private:
   fs_inst &operator=(const fs_inst &);

   void init(enum opcode opcode, uint8_t exec_width, const brw_reg &dst,
             const brw_reg *src, unsigned sources);

public:
   DECLARE_RALLOC_CXX_OPERATORS(fs_inst)

   fs_inst();
   fs_inst(enum opcode opcode, uint8_t exec_size);
   fs_inst(enum opcode opcode, uint8_t exec_size, const brw_reg &dst);
   fs_inst(enum opcode opcode, uint8_t exec_size, const brw_reg &dst,
           const brw_reg &src0);
   fs_inst(enum opcode opcode, uint8_t exec_size, const brw_reg &dst,
           const brw_reg &src0, const brw_reg &src1);
   fs_inst(enum opcode opcode, uint8_t exec_size, const brw_reg &dst,
           const brw_reg &src0, const brw_reg &src1, const brw_reg &src2);
   fs_inst(enum opcode opcode, uint8_t exec_size, const brw_reg &dst,
           const brw_reg src[], unsigned sources);
   fs_inst(const fs_inst &that);
   ~fs_inst();

   void resize_sources(uint8_t num_sources);

   bool is_send_from_grf() const;
   bool is_payload(unsigned arg) const;
   bool is_partial_write() const;
   unsigned components_read(unsigned i) const;
   unsigned size_read(int arg) const;
   bool can_do_source_mods(const struct intel_device_info *devinfo) const;
   bool can_do_cmod() const;
   bool can_change_types() const;
   bool has_source_and_destination_hazard() const;

   bool is_3src(const struct brw_compiler *compiler) const;
   bool is_math() const;
   bool is_control_flow_begin() const;
   bool is_control_flow_end() const;
   bool is_control_flow() const;
   bool is_commutative() const;
   bool is_raw_move() const;
   bool can_do_saturate() const;
   bool reads_accumulator_implicitly() const;
   bool writes_accumulator_implicitly(const struct intel_device_info *devinfo) const;

   /**
    * Instructions that use indirect addressing have additional register
    * regioning restrictions.
    */
   bool uses_indirect_addressing() const;

   void remove(bblock_t *block, bool defer_later_block_ip_updates = false);
   void insert_after(bblock_t *block, fs_inst *inst);
   void insert_before(bblock_t *block, fs_inst *inst);

   /**
    * True if the instruction has side effects other than writing to
    * its destination registers.  You are expected not to reorder or
    * optimize these out unless you know what you are doing.
    */
   bool has_side_effects() const;

   /**
    * True if the instruction might be affected by side effects of other
    * instructions.
    */
   bool is_volatile() const;

   /**
    * Return whether \p arg is a control source of a virtual instruction which
    * shouldn't contribute to the execution type and usual regioning
    * restriction calculations of arithmetic instructions.
    */
   bool is_control_source(unsigned arg) const;

   /**
    * Return the subset of flag registers read by the instruction as a bitset
    * with byte granularity.
    */
   unsigned flags_read(const intel_device_info *devinfo) const;

   /**
    * Return the subset of flag registers updated by the instruction (either
    * partially or fully) as a bitset with byte granularity.
    */
   unsigned flags_written(const intel_device_info *devinfo) const;

   /**
    * Return true if this instruction is a sampler message gathering residency
    * data.
    */
   bool has_sampler_residency() const;

   uint8_t sources; /**< Number of brw_reg sources. */

   /**
    * Execution size of the instruction.  This is used by the generator to
    * generate the correct binary for the given instruction.  Current valid
    * values are 1, 4, 8, 16, 32.
    */
   uint8_t exec_size;

   /**
    * Channel group from the hardware execution and predication mask that
    * should be applied to the instruction.  The subset of channel enable
    * signals (calculated from the EU control flow and predication state)
    * given by [group, group + exec_size) will be used to mask GRF writes and
    * any other side effects of the instruction.
    */
   uint8_t group;

   uint8_t mlen; /**< SEND message length */
   uint8_t ex_mlen; /**< SENDS extended message length */
   uint8_t sfid; /**< SFID for SEND instructions */
   /** The number of hardware registers used for a message header. */
   uint8_t header_size;
   uint8_t target; /**< MRT target. */
   uint32_t desc; /**< SEND[S] message descriptor immediate */
   uint32_t ex_desc; /**< SEND[S] extended message descriptor immediate */

   uint32_t offset; /**< spill/unspill offset or texture offset bitfield */
   unsigned size_written; /**< Data written to the destination register in bytes. */

   enum opcode opcode; /* BRW_OPCODE_* or FS_OPCODE_* */
   enum brw_conditional_mod conditional_mod; /**< BRW_CONDITIONAL_* */
   enum brw_predicate predicate;

   tgl_swsb sched; /**< Scheduling info. */

   union {
      struct {
         /* Chooses which flag subregister (f0.0 to f3.1) is used for
          * conditional mod and predication.
          */
         unsigned flag_subreg:3;

         /**
          * Systolic depth used by DPAS instruction.
          */
         unsigned sdepth:4;

         /**
          * Repeat count used by DPAS instruction.
          */
         unsigned rcount:4;

         unsigned pad:2;

         bool predicate_inverse:1;
         bool writes_accumulator:1; /**< instruction implicitly writes accumulator */
         bool force_writemask_all:1;
         bool no_dd_clear:1;
         bool no_dd_check:1;
         bool saturate:1;
         bool shadow_compare:1;
         bool check_tdr:1; /**< Only valid for SEND; turns it into a SENDC */
         bool send_has_side_effects:1; /**< Only valid for SHADER_OPCODE_SEND */
         bool send_is_volatile:1; /**< Only valid for SHADER_OPCODE_SEND */
         bool send_ex_desc_scratch:1; /**< Only valid for SHADER_OPCODE_SEND, use
                                       *   the scratch surface offset to build
                                       *   extended descriptor
                                       */
         bool send_ex_bso:1; /**< Only for SHADER_OPCODE_SEND, use extended
                              *   bindless surface offset (26bits instead of
                              *   20bits)
                              */
         /**
          * The predication mask applied to this instruction is guaranteed to
          * be uniform and a superset of the execution mask of the present block.
          * No currently enabled channel will be disabled by the predicate.
          */
         bool predicate_trivial:1;
         bool eot:1;
         bool last_rt:1;
         bool pi_noperspective:1;   /**< Pixel interpolator noperspective flag */
         bool keep_payload_trailing_zeros:1;
         /**
          * Whether the parameters of the SEND instructions are build with
          * NoMask (for A32 messages this covers only the surface handle, for
          * A64 messages this covers the load address).
          */
         bool has_no_mask_send_params:1;
      };
      uint32_t bits;
   };

   brw_reg dst;
   brw_reg *src;
   brw_reg builtin_src[4];

#ifndef NDEBUG
   /** @{
    * Annotation for the generated IR.
    */
   const char *annotation;
   /** @} */
#endif
};

/**
 * Make the execution of \p inst dependent on the evaluation of a possibly
 * inverted predicate.
 */
static inline fs_inst *
set_predicate_inv(enum brw_predicate pred, bool inverse,
                  fs_inst *inst)
{
   inst->predicate = pred;
   inst->predicate_inverse = inverse;
   return inst;
}

/**
 * Make the execution of \p inst dependent on the evaluation of a predicate.
 */
static inline fs_inst *
set_predicate(enum brw_predicate pred, fs_inst *inst)
{
   return set_predicate_inv(pred, false, inst);
}

/**
 * Write the result of evaluating the condition given by \p mod to a flag
 * register.
 */
static inline fs_inst *
set_condmod(enum brw_conditional_mod mod, fs_inst *inst)
{
   inst->conditional_mod = mod;
   return inst;
}

/**
 * Clamp the result of \p inst to the saturation range of its destination
 * datatype.
 */
static inline fs_inst *
set_saturate(bool saturate, fs_inst *inst)
{
   inst->saturate = saturate;
   return inst;
}

/**
 * Return the number of dataflow registers written by the instruction (either
 * fully or partially) counted from 'floor(reg_offset(inst->dst) /
 * register_size)'.  The somewhat arbitrary register size unit is 4B for the
 * UNIFORM and IMM files and 32B for all other files.
 */
inline unsigned
regs_written(const fs_inst *inst)
{
   assert(inst->dst.file != UNIFORM && inst->dst.file != IMM);
   return DIV_ROUND_UP(reg_offset(inst->dst) % REG_SIZE +
                       inst->size_written -
                       MIN2(inst->size_written, reg_padding(inst->dst)),
                       REG_SIZE);
}

/**
 * Return the number of dataflow registers read by the instruction (either
 * fully or partially) counted from 'floor(reg_offset(inst->src[i]) /
 * register_size)'.  The somewhat arbitrary register size unit is 4B for the
 * UNIFORM files and 32B for all other files.
 */
inline unsigned
regs_read(const fs_inst *inst, unsigned i)
{
   if (inst->src[i].file == IMM)
      return 1;

   const unsigned reg_size = inst->src[i].file == UNIFORM ? 4 : REG_SIZE;
   return DIV_ROUND_UP(reg_offset(inst->src[i]) % reg_size +
                       inst->size_read(i) -
                       MIN2(inst->size_read(i), reg_padding(inst->src[i])),
                       reg_size);
}

static inline enum brw_reg_type
get_exec_type(const fs_inst *inst)
{
   brw_reg_type exec_type = BRW_TYPE_B;

   for (int i = 0; i < inst->sources; i++) {
      if (inst->src[i].file != BAD_FILE &&
          !inst->is_control_source(i)) {
         const brw_reg_type t = get_exec_type(inst->src[i].type);
         if (brw_type_size_bytes(t) > brw_type_size_bytes(exec_type))
            exec_type = t;
         else if (brw_type_size_bytes(t) == brw_type_size_bytes(exec_type) &&
                  brw_type_is_float(t))
            exec_type = t;
      }
   }

   if (exec_type == BRW_TYPE_B)
      exec_type = inst->dst.type;

   assert(exec_type != BRW_TYPE_B);

   /* Promotion of the execution type to 32-bit for conversions from or to
    * half-float seems to be consistent with the following text from the
    * Cherryview PRM Vol. 7, "Execution Data Type":
    *
    * "When single precision and half precision floats are mixed between
    *  source operands or between source and destination operand [..] single
    *  precision float is the execution datatype."
    *
    * and from "Register Region Restrictions":
    *
    * "Conversion between Integer and HF (Half Float) must be DWord aligned
    *  and strided by a DWord on the destination."
    */
   if (brw_type_size_bytes(exec_type) == 2 &&
       inst->dst.type != exec_type) {
      if (exec_type == BRW_TYPE_HF)
         exec_type = BRW_TYPE_F;
      else if (inst->dst.type == BRW_TYPE_HF)
         exec_type = BRW_TYPE_D;
   }

   return exec_type;
}

static inline unsigned
get_exec_type_size(const fs_inst *inst)
{
   return brw_type_size_bytes(get_exec_type(inst));
}

static inline bool
is_send(const fs_inst *inst)
{
   return inst->mlen || inst->is_send_from_grf();
}

/**
 * Return whether the instruction isn't an ALU instruction and cannot be
 * assumed to complete in-order.
 */
static inline bool
is_unordered(const intel_device_info *devinfo, const fs_inst *inst)
{
   return is_send(inst) || (devinfo->ver < 20 && inst->is_math()) ||
          inst->opcode == BRW_OPCODE_DPAS ||
          (devinfo->has_64bit_float_via_math_pipe &&
           (get_exec_type(inst) == BRW_TYPE_DF ||
            inst->dst.type == BRW_TYPE_DF));
}

/**
 * Return whether the following regioning restriction applies to the specified
 * instruction.  From the Cherryview PRM Vol 7. "Register Region
 * Restrictions":
 *
 * "When source or destination datatype is 64b or operation is integer DWord
 *  multiply, regioning in Align1 must follow these rules:
 *
 *  1. Source and Destination horizontal stride must be aligned to the same qword.
 *  2. Regioning must ensure Src.Vstride = Src.Width * Src.Hstride.
 *  3. Source and Destination offset must be the same, except the case of
 *     scalar source."
 */
static inline bool
has_dst_aligned_region_restriction(const intel_device_info *devinfo,
                                   const fs_inst *inst,
                                   brw_reg_type dst_type)
{
   const brw_reg_type exec_type = get_exec_type(inst);
   /* Even though the hardware spec claims that "integer DWord multiply"
    * operations are restricted, empirical evidence and the behavior of the
    * simulator suggest that only 32x32-bit integer multiplication is
    * restricted.
    */
   const bool is_dword_multiply = !brw_type_is_float(exec_type) &&
      ((inst->opcode == BRW_OPCODE_MUL &&
        MIN2(brw_type_size_bytes(inst->src[0].type), brw_type_size_bytes(inst->src[1].type)) >= 4) ||
       (inst->opcode == BRW_OPCODE_MAD &&
        MIN2(brw_type_size_bytes(inst->src[1].type), brw_type_size_bytes(inst->src[2].type)) >= 4));

   if (brw_type_size_bytes(dst_type) > 4 || brw_type_size_bytes(exec_type) > 4 ||
       (brw_type_size_bytes(exec_type) == 4 && is_dword_multiply))
      return intel_device_info_is_9lp(devinfo) || devinfo->verx10 >= 125;

   else if (brw_type_is_float(dst_type))
      return devinfo->verx10 >= 125;

   else
      return false;
}

static inline bool
has_dst_aligned_region_restriction(const intel_device_info *devinfo,
                                   const fs_inst *inst)
{
   return has_dst_aligned_region_restriction(devinfo, inst, inst->dst.type);
}

/**
 * Return true if the instruction can be potentially affected by the Xe2+
 * regioning restrictions that apply to integer types smaller than a dword.
 * The restriction isn't quoted here due to its length, see BSpec #56640 for
 * details.
 */
static inline bool
has_subdword_integer_region_restriction(const intel_device_info *devinfo,
                                        const fs_inst *inst,
                                        const brw_reg *srcs, unsigned num_srcs)
{
   if (devinfo->ver >= 20 &&
       brw_type_is_int(inst->dst.type) &&
       MAX2(byte_stride(inst->dst),
            brw_type_size_bytes(inst->dst.type)) < 4) {
      for (unsigned i = 0; i < num_srcs; i++) {
         if (brw_type_is_int(srcs[i].type) &&
             ((brw_type_size_bytes(srcs[i].type) < 4 &&
               byte_stride(srcs[i]) >= 4) ||
              (MAX2(byte_stride(inst->dst),
                   brw_type_size_bytes(inst->dst.type)) == 1 &&
               brw_type_size_bytes(srcs[i].type) == 1 &&
               byte_stride(srcs[i]) >= 2)))
            return true;
      }
   }

   return false;
}

static inline bool
has_subdword_integer_region_restriction(const intel_device_info *devinfo,
                                        const fs_inst *inst)
{
   return has_subdword_integer_region_restriction(devinfo, inst,
                                                  inst->src, inst->sources);
}

/**
 * Return whether the LOAD_PAYLOAD instruction is a plain copy of bits from
 * the specified register file into a VGRF.
 *
 * This implies identity register regions without any source-destination
 * overlap, but otherwise has no implications on the location of sources and
 * destination in the register file: Gathering any number of portions from
 * multiple virtual registers in any order is allowed.
 */
inline bool
is_copy_payload(brw_reg_file file, const fs_inst *inst)
{
   if (inst->opcode != SHADER_OPCODE_LOAD_PAYLOAD ||
       inst->is_partial_write() || inst->saturate ||
       inst->dst.file != VGRF)
      return false;

   for (unsigned i = 0; i < inst->sources; i++) {
      if (inst->src[i].file != file ||
          inst->src[i].abs || inst->src[i].negate)
         return false;

      if (!inst->src[i].is_contiguous())
         return false;

      if (regions_overlap(inst->dst, inst->size_written,
                          inst->src[i], inst->size_read(i)))
         return false;
   }

   return true;
}

/**
 * Like is_copy_payload(), but the instruction is required to copy a single
 * contiguous block of registers from the given register file into the
 * destination without any reordering.
 */
inline bool
is_identity_payload(brw_reg_file file, const fs_inst *inst) {
   if (is_copy_payload(file, inst)) {
      brw_reg reg = inst->src[0];

      for (unsigned i = 0; i < inst->sources; i++) {
         reg.type = inst->src[i].type;
         if (!inst->src[i].equals(reg))
            return false;

         reg = byte_offset(reg, inst->size_read(i));
      }

      return true;
   } else {
      return false;
   }
}

/**
 * Like is_copy_payload(), but the instruction is required to source data from
 * at least two disjoint VGRFs.
 *
 * This doesn't necessarily rule out the elimination of this instruction
 * through register coalescing, but due to limitations of the register
 * coalesce pass it might be impossible to do so directly until a later stage,
 * when the LOAD_PAYLOAD instruction is unrolled into a sequence of MOV
 * instructions.
 */
inline bool
is_multi_copy_payload(const fs_inst *inst) {
   if (is_copy_payload(VGRF, inst)) {
      for (unsigned i = 0; i < inst->sources; i++) {
            if (inst->src[i].nr != inst->src[0].nr)
               return true;
      }
   }

   return false;
}

/**
 * Like is_identity_payload(), but the instruction is required to copy the
 * whole contents of a single VGRF into the destination.
 *
 * This means that there is a good chance that the instruction will be
 * eliminated through register coalescing, but it's neither a necessary nor a
 * sufficient condition for that to happen -- E.g. consider the case where
 * source and destination registers diverge due to other instructions in the
 * program overwriting part of their contents, which isn't something we can
 * predict up front based on a cheap strictly local test of the copy
 * instruction.
 */
inline bool
is_coalescing_payload(const brw::simple_allocator &alloc, const fs_inst *inst)
{
   return is_identity_payload(VGRF, inst) &&
          inst->src[0].offset == 0 &&
          alloc.sizes[inst->src[0].nr] * REG_SIZE == inst->size_written;
}

bool
has_bank_conflict(const struct brw_isa_info *isa, const fs_inst *inst);

/* Return the subset of flag registers that an instruction could
 * potentially read or write based on the execution controls and flag
 * subregister number of the instruction.
 */
static inline unsigned
brw_fs_flag_mask(const fs_inst *inst, unsigned width)
{
   assert(util_is_power_of_two_nonzero(width));
   const unsigned start = (inst->flag_subreg * 16 + inst->group) &
                          ~(width - 1);
  const unsigned end = start + ALIGN(inst->exec_size, width);
   return ((1 << DIV_ROUND_UP(end, 8)) - 1) & ~((1 << (start / 8)) - 1);
}

static inline unsigned
brw_fs_bit_mask(unsigned n)
{
   return (n >= CHAR_BIT * sizeof(brw_fs_bit_mask(n)) ? ~0u : (1u << n) - 1);
}

static inline unsigned
brw_fs_flag_mask(const brw_reg &r, unsigned sz)
{
   if (r.file == ARF) {
      const unsigned start = (r.nr - BRW_ARF_FLAG) * 4 + r.subnr;
      const unsigned end = start + sz;
      return brw_fs_bit_mask(end) & ~brw_fs_bit_mask(start);
   } else {
      return 0;
   }
}

#endif
