/* -*- c++ -*- */
/*
 * Copyright © 2010-2016 Intel Corporation
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

#pragma once

#include <assert.h>
#include "brw_reg.h"
#include "compiler/brw_list.h"
#include "brw_sampler.h"

#define MAX_SAMPLER_MESSAGE_SIZE 11

/* The sampler can return a vec5 when sampling with sparse residency. In
 * SIMD32, each component takes up 4 GRFs, so we need to allow up to size-20
 * VGRFs to hold the result.
 */
#define MAX_VGRF_SIZE(devinfo) ((devinfo)->ver >= 20 ? 40 : 20)

struct bblock_t;
struct brw_shader;

enum ENUM_PACKED brw_inst_kind {
   BRW_KIND_BASE,
   BRW_KIND_SEND,
   BRW_KIND_LOGICAL,
   BRW_KIND_TEX,
   BRW_KIND_MEM,
   BRW_KIND_DPAS,
   BRW_KIND_LOAD_PAYLOAD,
   BRW_KIND_URB,
   BRW_KIND_FB_WRITE,
   BRW_KIND_SCRATCH,
};

brw_inst_kind brw_inst_kind_for_opcode(enum opcode opcode);

struct brw_inst : brw_exec_node {
   brw_inst() = delete;
   brw_inst(const brw_inst&) = delete;

   /* Enable usage of placement new. */
   static void* operator new(size_t size, void *ptr) { return ptr; }
   static void operator delete(void *p) {}

   /* Prefer macro here instead of templates to get nicer
    * helper names.
    */
#define KIND_HELPERS(HELPER_NAME, TYPE_NAME, ENUM_NAME)         \
   struct TYPE_NAME *HELPER_NAME() {                            \
      return kind == ENUM_NAME ? (struct TYPE_NAME *)this       \
                               : nullptr;                       \
   }                                                            \
   const struct TYPE_NAME *HELPER_NAME() const {                \
      return kind == ENUM_NAME ? (const struct TYPE_NAME *)this \
                               : nullptr;                       \
   }

   KIND_HELPERS(as_send, brw_send_inst, BRW_KIND_SEND);
   KIND_HELPERS(as_tex, brw_tex_inst, BRW_KIND_TEX);
   KIND_HELPERS(as_mem, brw_mem_inst, BRW_KIND_MEM);
   KIND_HELPERS(as_dpas, brw_dpas_inst, BRW_KIND_DPAS);
   KIND_HELPERS(as_load_payload, brw_load_payload_inst, BRW_KIND_LOAD_PAYLOAD);
   KIND_HELPERS(as_urb, brw_urb_inst, BRW_KIND_URB);
   KIND_HELPERS(as_fb_write, brw_fb_write_inst, BRW_KIND_FB_WRITE);
   KIND_HELPERS(as_scratch, brw_scratch_inst, BRW_KIND_SCRATCH);

#undef KIND_HELPERS

   bool is_send() const;
   bool is_payload(unsigned arg) const;
   bool is_partial_write(unsigned grf_size = REG_SIZE) const;
   unsigned components_read(unsigned i) const;
   unsigned size_read(const struct intel_device_info *devinfo, int arg) const;
   bool can_do_source_mods(const struct intel_device_info *devinfo) const;
   bool can_do_cmod(enum brw_conditional_mod cmod) const;
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

   void remove();

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

   /**
    * Return true if this instruction is using the address register
    * implicitly.
    */
   bool uses_address_register_implicitly() const;

   enum opcode opcode;
   brw_inst_kind kind;

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

   uint8_t sources; /**< Number of brw_reg sources. */

   enum brw_predicate predicate;
   enum brw_conditional_mod conditional_mod; /**< BRW_CONDITIONAL_* */

   uint16_t size_written; /**< Data written to the destination register in bytes. */

   union {
      struct {
         /* Chooses which flag subregister (f0.0 to f3.1) is used for
          * conditional mod and predication.
          */
         uint8_t flag_subreg:3;

         bool predicate_inverse:1;
         bool writes_accumulator:1; /**< instruction implicitly writes accumulator */
         bool force_writemask_all:1;
         bool saturate:1;

         /**
          * The predication mask applied to this instruction is guaranteed to
          * be uniform and a superset of the execution mask of the present block.
          * No currently enabled channel will be disabled by the predicate.
          */
         bool predicate_trivial:1;
         bool eot:1;
         /**
          * Whether the parameters of the SEND instructions are build with
          * NoMask (for A32 messages this covers only the surface handle, for
          * A64 messages this covers the load address).
          *
          * Also used to signal a dummy render target SEND message that is
          * never executed.
          */
         bool has_no_mask_send_params:1;

         uint8_t pad:6;
      };
      uint16_t bits;
   };

   tgl_swsb sched; /**< Scheduling info. */

   bblock_t *block;

   brw_reg dst;
   brw_reg *src;

#ifndef NDEBUG
   /** @{
    * Annotation for the generated IR.
    */
   const char *annotation;
   /** @} */
#endif
};

struct brw_send_inst : brw_inst {
   uint32_t desc;
   uint32_t ex_desc;
   uint32_t offset;

   uint8_t mlen;
   uint8_t ex_mlen;
   uint8_t sfid;

   /** The number of hardware registers used for a message header. */
   uint8_t header_size;

   union {
      struct {
         /**
          * Turns it into a SENDC.
          */
         bool check_tdr:1;

         bool has_side_effects:1;
         bool is_volatile:1;

         /**
          * The surface used for this message is bindless and therefore should
          * go into the address register.
          */
         bool bindless_surface:1;

         /**
          * Serialize the message (Gfx12.x only)
          */
         bool fused_eu_disable:1;

         /**
          * Only for SHADER_OPCODE_SEND, @offset field contains an immediate
          * part of the extended descriptor that must be encoded in the
          * instruction.
          */
         bool ex_desc_imm:1;

         uint8_t pad:2;
      };
      uint8_t send_bits;
   };
};

struct brw_tex_inst : brw_inst {
   enum brw_sampler_opcode sampler_opcode;
   union {
      struct {
         /**
          * Whether the instruction requests the residency data (additional register
          * written).
          */
         bool residency:1;
         /**
          * Serialize the message (Gfx12.x only)
          */
         bool fused_eu_disable:1;
         /**
          * Whether the surface handle is bindless
          */
         bool surface_bindless:1;
         /**
          * Whether the sampler handle is bindless
          */
         bool sampler_bindless:1;
         /**
          * Whether const_offsets holds meaningful values
          */
         bool has_const_offsets:1;
         /**
          * Coord components
          */
         uint8_t coord_components:2;
         /**
          * Gather component
          */
         uint8_t gather_component:2;
         /**
          * Bitfields payload parameters that cannot be optimized by
          * brw_opt_zero_samples()
          */
         uint16_t required_params:13;
      };
      uint32_t bits;
   };

   /**
    * Constant offsets
    */
   int8_t const_offsets[3];
};

struct brw_mem_inst : brw_inst {
   enum lsc_opcode lsc_op;
   enum memory_logical_mode mode;
   enum lsc_addr_surface_type binding_type;
   enum lsc_data_size data_size;

   uint8_t coord_components;
   uint8_t components;
   uint8_t flags;

   /** Required alignment of address in bytes; 0 for natural alignment */
   uint32_t alignment;

   int32_t address_offset;
};

struct brw_dpas_inst : brw_inst {
   /** Systolic depth. */
   uint8_t sdepth;

   /** Repeat count. */
   uint8_t rcount;
};

struct brw_load_payload_inst : brw_inst {
   /** The number of hardware registers used for a message header. */
   uint8_t header_size;
};

struct brw_urb_inst : brw_inst {
   /** Global offset in bytes on Xe2 or OWords on older hardware */
   uint32_t offset;
   uint8_t components;
};

struct brw_fb_write_inst : brw_inst {
   uint8_t components;
   uint8_t target;
   bool null_rt;
   bool last_rt;
};

struct brw_scratch_inst : brw_inst {
   /** Offset in scratch space for the load or store. */
   unsigned offset;

   /**
    * Should a LSC transpose message be used for the fill?
    *
    * Currently this must be false for spills.
    */
   bool use_transpose;
};

/**
 * Make the execution of \p inst dependent on the evaluation of a possibly
 * inverted predicate.
 */
static inline brw_inst *
set_predicate_inv(enum brw_predicate pred, bool inverse,
                  brw_inst *inst)
{
   inst->predicate = pred;
   inst->predicate_inverse = inverse;
   return inst;
}

/**
 * Make the execution of \p inst dependent on the evaluation of a predicate.
 */
static inline brw_inst *
set_predicate(enum brw_predicate pred, brw_inst *inst)
{
   return set_predicate_inv(pred, false, inst);
}

/**
 * Write the result of evaluating the condition given by \p mod to a flag
 * register.
 */
static inline brw_inst *
set_condmod(enum brw_conditional_mod mod, brw_inst *inst)
{
   inst->conditional_mod = mod;
   return inst;
}

/**
 * Clamp the result of \p inst to the saturation range of its destination
 * datatype.
 */
static inline brw_inst *
set_saturate(bool saturate, brw_inst *inst)
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
regs_written(const brw_inst *inst)
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
regs_read(const struct intel_device_info *devinfo, const brw_inst *inst, unsigned i)
{
   if (inst->src[i].file == IMM)
      return 1;

   const unsigned reg_size = inst->src[i].file == UNIFORM ? 4 : REG_SIZE;
   return DIV_ROUND_UP(reg_offset(inst->src[i]) % reg_size +
                       inst->size_read(devinfo, i) -
                       MIN2(inst->size_read(devinfo, i), reg_padding(inst->src[i])),
                       reg_size);
}

enum brw_reg_type get_exec_type(const brw_inst *inst);

static inline unsigned
get_exec_type_size(const brw_inst *inst)
{
   return brw_type_size_bytes(get_exec_type(inst));
}

/**
 * Return whether the instruction isn't an ALU instruction and cannot be
 * assumed to complete in-order.
 */
static inline bool
is_unordered(const intel_device_info *devinfo, const brw_inst *inst)
{
   return inst->is_send() || (devinfo->ver < 20 && inst->is_math()) ||
          inst->opcode == BRW_OPCODE_DPAS ||
          (devinfo->has_64bit_float_via_math_pipe &&
           (get_exec_type(inst) == BRW_TYPE_DF ||
            inst->dst.type == BRW_TYPE_DF));
}

static inline bool
has_bfloat_operands(const brw_inst *inst)
{
   if (brw_type_is_bfloat(inst->dst.type))
      return true;

   for (int i = 0; i < inst->sources; i++) {
      if (brw_type_is_bfloat(inst->src[i].type))
         return true;
   }

   return false;
}

bool has_dst_aligned_region_restriction(const intel_device_info *devinfo,
                                        const brw_inst *inst,
                                        brw_reg_type dst_type);

static inline bool
has_dst_aligned_region_restriction(const intel_device_info *devinfo,
                                   const brw_inst *inst)
{
   return has_dst_aligned_region_restriction(devinfo, inst, inst->dst.type);
}

bool has_subdword_integer_region_restriction(const intel_device_info *devinfo,
                                             const brw_inst *inst,
                                             const brw_reg *srcs, unsigned num_srcs);

static inline bool
has_subdword_integer_region_restriction(const intel_device_info *devinfo,
                                        const brw_inst *inst)
{
   return has_subdword_integer_region_restriction(devinfo, inst,
                                                  inst->src, inst->sources);
}

bool is_identity_payload(const struct intel_device_info *devinfo,
                         brw_reg_file file, const brw_inst *inst);

bool is_multi_copy_payload(const struct intel_device_info *devinfo,
                           const brw_inst *inst);

bool is_coalescing_payload(const struct brw_shader &s, const brw_inst *inst);

bool has_bank_conflict(const struct brw_isa_info *isa, const brw_inst *inst);

/* Return the subset of flag registers that an instruction could
 * potentially read or write based on the execution controls and flag
 * subregister number of the instruction.
 */
static inline unsigned
brw_flag_mask(const brw_inst *inst, unsigned width)
{
   assert(util_is_power_of_two_nonzero(width));
   const unsigned start = (inst->flag_subreg * 16 + inst->group) &
                          ~(width - 1);
   const unsigned end = start + align(inst->exec_size, width);
   return ((1 << DIV_ROUND_UP(end, 8)) - 1) & ~((1 << (start / 8)) - 1);
}

static inline unsigned
brw_bit_mask(unsigned n)
{
   return (n >= CHAR_BIT * sizeof(brw_bit_mask(n)) ? ~0u : (1u << n) - 1);
}

static inline unsigned
brw_flag_mask(const brw_reg &r, unsigned sz)
{
   if (r.file == ARF) {
      const unsigned start = (r.nr - BRW_ARF_FLAG) * 4 + r.subnr;
      const unsigned end = start + sz;
      return brw_bit_mask(end) & ~brw_bit_mask(start);
   } else {
      return 0;
   }
}
