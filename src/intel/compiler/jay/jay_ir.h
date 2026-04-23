/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "compiler/brw/brw_compiler.h"
#include "compiler/brw/brw_eu.h"
#include "compiler/brw/brw_eu_defines.h"
#include "compiler/shader_enums.h"
#include "util/bitset.h"
#include "util/list.h"
#include "util/macros.h"
#include "util/ralloc.h"
#include "util/sparse_bitset.h"
#include "util/u_dynarray.h"
#include "util/u_math.h"
#include "jay_opcodes.h"

/* TODO: switch to brw_conditional_mod */
enum PACKED jay_conditional_mod {
   JAY_CONDITIONAL_EQ = 1,  /**< Equal to zero */
   JAY_CONDITIONAL_NE = 2,  /**< Not equal to zero */
   JAY_CONDITIONAL_GT = 3,  /**< Greater than zero */
   JAY_CONDITIONAL_LT = 5,  /**< Less than zero */
   JAY_CONDITIONAL_GE = 4,  /**< Greater than or equal to zero */
   JAY_CONDITIONAL_LE = 6,  /**< Less than or equal to zero */
   JAY_CONDITIONAL_OV = 8,  /**< Overflow has occurred */
   JAY_CONDITIONAL_NAN = 9, /**< Result is NaN */
};

static inline enum jay_conditional_mod
jay_conditional_mod_swap_sources(enum jay_conditional_mod mod)
{
   /* clang-format off */
   switch (mod) {
   case JAY_CONDITIONAL_GT: return JAY_CONDITIONAL_LT;
   case JAY_CONDITIONAL_LT: return JAY_CONDITIONAL_GT;
   case JAY_CONDITIONAL_GE: return JAY_CONDITIONAL_LE;
   case JAY_CONDITIONAL_LE: return JAY_CONDITIONAL_GE;
   default:                 return mod;
   }
   /* clang-format on */
}

enum PACKED jay_arf {
   JAY_ARF_NULL = 0,
   JAY_ARF_MASK = BRW_ARF_MASK,
   JAY_ARF_CONTROL = BRW_ARF_CONTROL,
   JAY_ARF_TIMESTAMP = BRW_ARF_TIMESTAMP,
};

enum PACKED jay_file {
   /** Non-uniform general purpose registers: 32-bits per SIMT lane. */
   GPR,

   /** Uniform general purpose registers: 32-bit uniform values */
   UGPR,

   /** Memory registers representing spilled values: 32-bits per SIMT lane. */
   MEM,

   /** Memory registers representing spilled values: 32-bits uniform values */
   UMEM,

   /** Non-uniform flags (predicates): 1-bit per SIMT lane */
   FLAG,

   /** Uniform flags (predicates): 1-bit uniform value */
   UFLAG,

   /** Address registers */
   J_ADDRESS,

   /* Non-SSA files below: */

   /** Accumulators: 32-bits per SIMT lane */
   ACCUM,

   /** Uniform accumulators: 32-bit uniform value */
   UACCUM,

   /** Architecture registers: direct access scalar */
   J_ARF,

   /** Inputs within Jay unit tests */
   TEST_FILE,

   /* Immediate value */
   J_IMM,

   JAY_FILE_LAST = J_IMM,
   JAY_NUM_SSA_FILES = J_ADDRESS + 1,

   /* Set of files that the main RA (and not eg flag RA) allocates. */
   JAY_NUM_RA_FILES = UMEM + 1,
   JAY_NUM_GRF_FILES = UGPR + 1,
};
static_assert(JAY_FILE_LAST <= 0b1111, "must fit in 4 bits (see jay_def)");

#define jay_foreach_ssa_file(file)                                             \
   for (enum jay_file file = 0; file < JAY_NUM_SSA_FILES; ++file)

/* Value stuffed into the index field of instructions post-RA that are not
 * null (0) but do not have an associated SSA index (as they are post-RA).
 */
#define JAY_SENTINEL (0xffffffffu)

/* Maximum number of words in an jay_def */
#define JAY_MAX_DEF_LENGTH (128)

/* Maximum number of sources/destinations other than for phis */
#define JAY_MAX_SRCS                 (16)
#define JAY_MAX_DESTS                (2)
#define JAY_MAX_OPERANDS             (JAY_MAX_SRCS + JAY_MAX_DESTS)
#define JAY_MAX_FLAGS                (8)
#define JAY_MAX_SAMPLER_MESSAGE_SIZE (11)
#define JAY_NUM_LAST_USE_BITS        (32)
#define JAY_NUM_PHYS_GRF             (128)
#define JAY_NUM_UGPR                 (1024)
#define JAY_REG_BITS                 (17)

/*
 * An jay_def represents a contiguous array of registers or a 32-bit immediate.
 * It is used for sources or (in restricted form) for destinations.
 */
typedef struct jay_def {
   /* Mode-dependent payload.
    *
    * File = IMMEDIATE: Immediate.
    * Collect = false: Base SSA index.
    * Collect = true: Pointer to SSA indices.
    *
    * SSA indices must be unique even across register files, so that we can
    * easily track them all in e.g. a bitfield without needing to have
    * separate data structures for each file.
    *
    * Each index represents a single 32-bit (or 1-bit if a predicate) value in
    * the specified register file. 64-bit or vec4 values use multiple indices.
    *
    * Index 0 is reserved as the null value.
    */
   uint32_t _payload;

   /* After register allocation, the register assigned to this def.
    *
    * Also used for additional pointer bits for collect pre-RA, which is why
    * this is as large as it is. Could be shrunk with more pointer compression.
    */
   unsigned reg:JAY_REG_BITS;

   /* (Post-RA) only, access only the top half of the indexed 32-bit register */
   bool hi:1;

   /** The associated file (must be < JAY_NUM_SSA_FILES for SSA) */
   enum jay_file file:4;

   /* Represents either a negation or a bitwise inversion (depending on the
    * instruction type.)
    */
   bool negate:1;

   /* Represents absolute value (on floating point sources) */
   bool abs:1;

   /* Number of values minus 1 */
   unsigned num_values_m1:7;

   /* If true, collects many discontiguous SSA indices into a single def.
    * Requires file = GPR or file = UGPR. Cannot be used post-RA.
    *
    * Canonical form is required: the indices pointed to by the payload must NOT
    * be contiguous. Also, the payload is not owned by the def: the def may be
    * cheaply copied around, but mutating the payload requires copy-on-write and
    * maintaining the canonical form.
    */
   bool collect:1;
} jay_def;
static_assert(sizeof(jay_def) == 8, "packed");

/*
 * Construct an jay_def representing a bare register with no associated SSA
 * index, for use post-RA only.
 */
static inline jay_def
jay_bare_reg(enum jay_file file, uint16_t reg)
{
   return (jay_def) { ._payload = JAY_SENTINEL, .reg = reg, .file = file };
}

/*
 * Set the register for a def (called by RA only). This drops the collect
 * indices since we do not have space to encode both simultaneously.
 */
static inline void
jay_set_reg(jay_def *d, unsigned r)
{
   if (d->collect) {
      d->collect = false;
      d->_payload = JAY_SENTINEL;
   }

   d->reg = r;
}

static inline uint32_t
jay_base_index(jay_def d)
{
   assert(d.file != J_IMM && !d.collect);
   return d._payload;
}

/**
 * True if the value is null.
 */
static inline bool
jay_is_null(jay_def d)
{
   return d._payload == 0 && d.file != J_IMM;
}

static inline bool
jay_is_imm(jay_def d)
{
   return d.file == J_IMM;
}

/**
 * True if the def is a 1-bit flag regardless of whether it is uniform.
 */
static inline bool
jay_is_flag(jay_def d)
{
   return d.file == FLAG || d.file == UFLAG;
}

/**
 * Return the number of SSA indices referenced by an jay_def.
 */
static inline unsigned
jay_num_values(jay_def d)
{
   return jay_is_imm(d) || jay_is_null(d) ? 0 : (d.num_values_m1 + 1);
}

/**
 * True if the def is an SSA def (and not, say, an arch register).
 */
static inline bool
jay_is_ssa(jay_def d)
{
   return d.file < JAY_NUM_SSA_FILES;
}

#define jay_foreach_comp(def, c)                                               \
   for (unsigned c = 0; c < jay_num_values(def); ++c)

#define jay_foreach_comp_rev(def, c)                                           \
   for (signed c = jay_num_values(def) - 1; c >= 0; --c)

/*
 * Alias for jay_base_index for use with scalar defs.
 */
static inline uint32_t
jay_index(jay_def d)
{
   assert(jay_num_values(d) == 1);
   return jay_base_index(d);
}

/**
 * Return a reference to the array of indices of a collect source.
 */
static inline uint32_t *
_jay_collect_indices(jay_def d)
{
   assert(d.collect);

   /* reg has upper bits of the pointer */
   uint64_t payload = (((uint64_t) d.reg) << 32) | d._payload;
   return (uint32_t *) (uintptr_t) payload;
}

/**
 * Return the n'th channel of an SSA def.
 *
 * Note: this is specifically read-only. To mutate, use jay_set_channel.
 */
static inline uint32_t
jay_channel(jay_def d, unsigned c)
{
   assert(d.file != J_IMM);
   assert(c <= d.num_values_m1);

   if (likely(!d.collect)) {
      return jay_base_index(d) + c;
   } else {
      return _jay_collect_indices(d)[c];
   }
}

/**
 * Build a contiguous jay_def.
 */
static inline jay_def
jay_contiguous_def(enum jay_file file, uint32_t index, unsigned count)
{
   assert(count > 0 && count <= (1 << 7) && "max def width");

   return (jay_def) {
      ._payload = index,
      .file = file,
      .num_values_m1 = count - 1,
   };
}

/*
 * Replaces a source, preserving the negate/abs if present.
 */
static inline void
jay_replace_src(jay_def *old, jay_def replacement)
{
   replacement.negate = old->negate;
   replacement.abs = old->abs;
   *old = replacement;
}

static inline jay_def
jay_scalar(enum jay_file file, uint32_t index)
{
   return jay_contiguous_def(file, index, 1);
}

static inline jay_def
jay_null()
{
   return jay_scalar(J_ARF, 0);
}

/**
 * Return a contiguous subrange inside an SSA def.
 */
static inline jay_def
jay_extract_range(jay_def def, unsigned chan, unsigned count)
{
   assert(!jay_is_imm(def));
   assert((count == 1 || !def.collect) && "slicing collects unsupported");
   assert(chan + count <= jay_num_values(def));

   uint32_t base = jay_channel(def, chan);
   jay_replace_src(&def, jay_contiguous_def(def.file, base, count));
   return def;
}

/**
 * Return a scalar SSA def equal to a single channel from an SSA def.
 */
static inline jay_def
jay_extract(jay_def def, unsigned chan)
{
   return jay_extract_range(def, chan, 1);
}

/**
 * Like jay_extract but working on bare registers. This could be unified to
 * preserve indices and such but meh.
 */
static inline jay_def
jay_extract_post_ra(jay_def def, unsigned chan)
{
   return jay_bare_reg(def.file, def.reg + chan);
}

/**
 * Construct an immediate source from a raw 32-bit data pattern.
 */
static inline jay_def
jay_imm(uint32_t imm)
{
   return (jay_def) { ._payload = imm, .file = J_IMM };
}

/**
 * True if both jay_defs are equivalent up to source modifiers.
 */
static inline bool
jay_defs_equivalent(jay_def a, jay_def b)
{
   if (a.file != b.file ||
       a.num_values_m1 != b.num_values_m1 ||
       a.collect != b.collect)
      return false;

   if (likely(!a.collect)) {
      /* Contiguous or immediate */
      return a._payload == b._payload && a.reg == b.reg;
   } else {
      /* Collect. Component-wise compare. */
      return !memcmp(_jay_collect_indices(a), _jay_collect_indices(b),
                     sizeof(uint32_t) * jay_num_values(a));
   }
}

/**
 * True if both registers are equal (for use post-RA).
 */
static inline bool
jay_regs_equal(jay_def a, jay_def b)
{
   return a.file == b.file &&
          a.num_values_m1 == b.num_values_m1 &&
          a.reg == b.reg;
}

/**
 * Return a reference to the execution mask (mask0) architecture register.
 */
static inline jay_def
jay_exec_mask(void)
{
   return jay_scalar(J_ARF, JAY_ARF_MASK);
}

/**
 * Return a reference to the control (cr0) architecture register.
 */
static inline jay_def
jay_control(void)
{
   return jay_scalar(J_ARF, JAY_ARF_CONTROL);
}

/**
 * Construct an immediate from a floating point constant.
 */
static inline jay_def
jay_imm_f(float imm)
{
   return jay_imm(fui(imm));
}

/**
 * Return the negation of a source.
 */
static inline jay_def
jay_negate(jay_def src)
{
   src.negate = !src.negate;
   return src;
}

/**
 * Return the absolute value of a source.
 */
static inline jay_def
jay_abs(jay_def src)
{
   src.negate = false;
   src.abs = true;
   return src;
}

/**
 * Returns true if the given source reads the same value in all lanes.
 */
static inline bool
jay_is_uniform(jay_def d)
{
   return d.file == UGPR ||
          d.file == UFLAG ||
          d.file == UACCUM ||
          jay_is_imm(d);
}

/**
 * Returns true if the given definition represents a spilled variable.
 */
static inline bool
jay_is_mem(jay_def x)
{
   return x.file == MEM || x.file == UMEM;
}

static inline uint32_t
jay_as_uint(jay_def src)
{
   assert(jay_is_imm(src));
   return src._payload;
}

static inline bool
jay_is_zero(jay_def src)
{
   return jay_is_imm(src) && jay_as_uint(src) == 0;
}

/* Chosen so that sized type is the unsized type OR the number bits */
#define JAY_TYPE_BASE_MASK (128 | 2 | 4)

enum PACKED jay_type {
   JAY_TYPE_UNTYPED = 0,
   JAY_TYPE_U = 2,
   JAY_TYPE_S = 4,
   JAY_TYPE_F = 6,
   JAY_TYPE_BF = 128,

   /** Unsigned integers */
   JAY_TYPE_U64 = JAY_TYPE_U | 64,
   JAY_TYPE_U32 = JAY_TYPE_U | 32,
   JAY_TYPE_U16 = JAY_TYPE_U | 16,
   JAY_TYPE_U8 = JAY_TYPE_U | 8,
   JAY_TYPE_U1 = JAY_TYPE_U | 1,

   /** Signed integers */
   JAY_TYPE_S64 = JAY_TYPE_S | 64,
   JAY_TYPE_S32 = JAY_TYPE_S | 32,
   JAY_TYPE_S16 = JAY_TYPE_S | 16,
   JAY_TYPE_S8 = JAY_TYPE_S | 8,
   JAY_TYPE_S1 = JAY_TYPE_S | 1,

   /** IEEE floating point */
   JAY_TYPE_F64 = JAY_TYPE_F | 64,
   JAY_TYPE_F32 = JAY_TYPE_F | 32,
   JAY_TYPE_F16 = JAY_TYPE_F | 16,

   /** Other floating point variants */
   JAY_TYPE_BF16 = JAY_TYPE_BF | 16,
};
static_assert(sizeof(enum jay_type) == 1);

static inline enum jay_type
jay_type(enum jay_type base, unsigned bits)
{
   /* Normalize booleans */
   if (bits == 1) {
      base = JAY_TYPE_U;
   }

   return (enum jay_type)(base | bits);
}

static inline enum jay_type
jay_base_type(enum jay_type t)
{
   return (enum jay_type)(t & JAY_TYPE_BASE_MASK);
}

static inline unsigned
jay_type_size_bits(enum jay_type t)
{
   return t & ~JAY_TYPE_BASE_MASK;
}

static inline enum jay_type
jay_type_rebase(enum jay_type t, enum jay_type new_base)
{
   return jay_type(new_base, jay_type_size_bits(t));
}

static inline enum jay_type
jay_type_resize(enum jay_type t, unsigned bits)
{
   return jay_type(jay_base_type(t), bits);
}

/**
 * Returns the number of 32-bit values needed to hold a type t.
 */
static inline unsigned
jay_type_vector_length(enum jay_type t)
{
   return jay_type_size_bits(t) == 64 ? 2 : 1;
}

static inline bool
jay_type_is_any_float(enum jay_type t)
{
   return jay_base_type(t) == JAY_TYPE_F || jay_base_type(t) == JAY_TYPE_BF;
}

enum jay_predication : uint8_t {
   /** No predication. */
   JAY_NOT_PREDICATED = 0,

   /**
    * Predicated with no default value. Used post-RA and for instructions that
    * do not write a destination.
    */
   JAY_PREDICATED = 1,

   /** Predicated with 1 default value. Used pre-RA. */
   JAY_PREDICATED_DEFAULT = 2,
};

/**
 * Representation of a shader instruction in the Jay IR.
 */
typedef struct jay_inst {
   struct list_head link;

   /**
    * Metadata calculated by liveness analysis: bit i is set if the i'th
    * non-null SSA index read by the instruction is killed by that read.
    */
   BITSET_DECLARE(last_use, JAY_NUM_LAST_USE_BITS);

   enum jay_opcode op;
   enum jay_type type; /**< execution type of the instruction */

   /** Software scoreboarding dependencies (for non-SYNC instructions) */
   struct tgl_swsb dep;

   /** Number of sources */
   uint8_t num_srcs;

   /**
    * Indicates an instruction reading only uniform sources but writing a FLAG
    * and no GPR/UGPR that expects the flag to replicate for all SIMD lanes.
    * This is okay in our data model but cannot be inferred from the files, so
    * we have a secondary bit to express this.
    */
   bool broadcast_flag:1;
   bool saturate      :1;

   /**
    * In a SIMD split instruction, whether the regdist dependency is replicated
    * to each physical instruction. If false, only the first instruction waits.
    *
    * If decrement_dep is also set, the regdist is decremented by the macro
    * length for each instruction (modelling cross-pipe dependencies).
    */
   bool replicate_dep:1;
   bool decrement_dep:1;
   unsigned padding  :12;

   enum jay_predication predication;
   enum jay_conditional_mod conditional_mod;

   jay_def cond_flag; /**< conditional flag */
   jay_def dst;

   jay_def src[];
} jay_inst;

static_assert(sizeof(jay_inst) == 32 + (sizeof(uintptr_t) * 2), "packed");

/*
 * Return the number of instruction set defined sources, ignoring implicit
 * predication and accumulator sources.
 */
static inline unsigned
jay_num_isa_srcs(const jay_inst *I)
{
   return I->num_srcs - I->predication - (I->op == JAY_OPCODE_SEL);
}

static inline bool
jay_uses_flag(const jay_inst *I)
{
   return I->predication ||
          !jay_is_null(I->cond_flag) ||
          I->op == JAY_OPCODE_SEL;
}

static inline void
jay_remove_instruction(jay_inst *inst)
{
   list_del(&inst->link);
}

static inline bool
jay_has_src_mods(jay_inst *I, unsigned s)
{
   return jay_opcode_infos[I->op].src_mods & BITFIELD_BIT(s);
}

static inline bool
jay_inst_has_default(jay_inst *I)
{
   return I->predication >= JAY_PREDICATED_DEFAULT;
}

static inline jay_def *
jay_inst_get_predicate(jay_inst *I)
{
   assert(I->predication);
   return &I->src[I->num_srcs - I->predication];
}

static inline jay_def *
jay_inst_get_default(jay_inst *I)
{
   assert(jay_inst_has_default(I));
   return &I->src[I->num_srcs - 1];
}

/* Must be included late since it depends on jay_inst but the rest of this file
 * depends on the inline functions it defines.
 */
#include "jay_extra_info.h"

static inline enum jay_type
jay_src_type(const jay_inst *I, unsigned s)
{
   /* Predicates */
   if (s == (unsigned) (I->num_srcs - I->predication) ||
       (I->op == JAY_OPCODE_SEL && s == 2) ||
       (I->op == JAY_OPCODE_PHI_SRC && jay_is_flag(I->src[s])))
      return JAY_TYPE_U1;

   /* Conversions have an explicit source type, use that. */
   if (I->op == JAY_OPCODE_CVT)
      return jay_cvt_src_type(I);

   /* 16-bit operand */
   if (I->op == JAY_OPCODE_MUL_32X16 && s == 1)
      return jay_type_resize(I->type, jay_type_size_bits(I->type) / 2);

   if (I->op == JAY_OPCODE_SEND) {
      if (s < 2)
         return JAY_TYPE_U32;
      else if (s < 4)
         return s == 3 ? jay_send_type_1(I) : jay_send_type_0(I);
   }

   if (I->op == JAY_OPCODE_CAST_CANONICAL_TO_FLAG)
      return JAY_TYPE_U32;

   /* Shifts are always small even with 64-bit destinations */
   if ((I->op == JAY_OPCODE_SHL ||
        I->op == JAY_OPCODE_SHR ||
        I->op == JAY_OPCODE_ASR) &&
       s == 1)
      return JAY_TYPE_U16;

   /* TODO: Do we want to allow zero-extension generally? */
   if (I->op == JAY_OPCODE_AND_U32_U16)
      return JAY_TYPE_U16;

   /* Mixed-signedness integer dot product opcode */
   if (I->op == JAY_OPCODE_DP4A_SU && s == 2)
      return JAY_TYPE_U32;

   /* Shuffle lane index distinct from data type */
   if (I->op == JAY_OPCODE_SHUFFLE && s == 1)
      return JAY_TYPE_U32;

   /* Other instructions inherit the destination type. */
   return I->type;
}

enum jay_stride {
   JAY_STRIDE_2 = 0,
   JAY_STRIDE_4,
   JAY_STRIDE_8,
   JAY_NUM_STRIDES,
};

static inline unsigned
jay_stride_to_bits(enum jay_stride s)
{
   assert(s <= JAY_STRIDE_8);
   return 16 << s;
}

#define JAY_PARTITION_BLOCKS (3)

struct jay_register_block {
   uint16_t start, len;
};

struct jay_partition {
   /** Consecutive ranges of GRFs in GPR/UGPRs. */
   struct jay_register_block blocks[JAY_NUM_GRF_FILES][JAY_PARTITION_BLOCKS];

   /** Number of GPR/UGPRs per GRF, times 16. For example, 16 encodes SIMD16
    * 32-bit GPRs on Xe2 (1 GRF = 1 GPR). 256 encodes UGPRs (1 GRF = 16 UGPRs).
    * 8 encodes SIMD32 32-bit GPRs on Xe2 (2 GRF = 1 GPR).
    */
   unsigned units_x16[JAY_NUM_GRF_FILES];

   /** Base GPR for each stride. The file is partitioned (4, 8, 2, 4=EOT). */
   unsigned base8, base2, base_eot;

   /** Region of the UGPR partition suitable for large UGPR vectors */
   struct jay_register_block large_ugpr_block;
};

static inline enum jay_stride
jay_gpr_to_stride(const struct jay_partition *p, unsigned reg)
{
   return (reg < p->base8 || reg >= p->base_eot) ? JAY_STRIDE_4 :
          reg >= p->base2                        ? JAY_STRIDE_2 :
                                                   JAY_STRIDE_8;
}

/**
 * Representation of a shader in the Jay IR.
 */
typedef struct jay_shader {
   mesa_shader_stage stage;
   struct list_head functions;
   const struct intel_device_info *devinfo;
   union brw_any_prog_data *prog_data;
   unsigned spills, fills;
   unsigned scratch_size;
   unsigned push_grfs;

   /**
    * Ralloc linear context. Since we don't typically free as we go,
    * most allocations should go through this context for efficiency.
    */
   struct linear_ctx *lin_ctx;

   /* Dispatch width of the current compile: 8, 16, or 32. */
   unsigned dispatch_width;

   /**
    * Number of GPR/UGPRs used across all functions in the shader. This is the
    * limit that must be allocated for the shader.
    */
   unsigned num_regs[JAY_NUM_RA_FILES];

   /**
    * Register file partition chosen for the whole shader.
    */
   struct jay_partition partition;

   /** Current compilation phase (for printing & validation) */
   bool post_ra;
} jay_shader;

static inline jay_shader *
jay_new_shader(void *memctx, mesa_shader_stage stage)
{
   jay_shader *s = rzalloc(NULL, jay_shader);
   s->stage = stage;
   s->lin_ctx = linear_context(s);
   list_inithead(&s->functions);
   return s;
}

static inline unsigned
jay_ugpr_per_grf(jay_shader *s)
{
   unsigned B_per_unit = 32 /* see reg_unit */;
   unsigned B_per_ugpr = 4;

   return reg_unit(s->devinfo) * (B_per_unit / B_per_ugpr);
}

static inline unsigned
jay_grf_per_gpr(jay_shader *s)
{
   assert(reg_unit(s->devinfo) == 1 || reg_unit(s->devinfo) == 2);
   return reg_unit(s->devinfo) == 2 ? (s->dispatch_width / 16) :
                                      (s->dispatch_width / 8);
}

static inline unsigned
jay_phys_flag_per_virt(jay_shader *s)
{
   /* TODO: Check if this holds on older platforms */
   return jay_grf_per_gpr(s);
}

/*
 * Returns whether an instruction will lower to a SEND post-RA: either a SEND or
 * a spill/fill that has not yet been lowered.
 */
static inline bool
jay_is_send_like(const jay_inst *I)
{
   if (I->op == JAY_OPCODE_MOV)
      return jay_is_mem(I->dst) || jay_is_mem(I->src[0]);
   else
      return I->op == JAY_OPCODE_SEND;
}

/*
 * Returns whether an instruction contains cross-lane access.
 */
static inline bool
jay_is_shuffle_like(const jay_inst *I)
{
   return I->op == JAY_OPCODE_SHUFFLE ||
          I->op == JAY_OPCODE_QUAD_SWIZZLE ||
          I->op == JAY_OPCODE_BROADCAST_IMM;
}

/*
 * Return the required alignment for the register assigned to a given source.
 */
static inline unsigned
jay_src_alignment(jay_shader *shader, const jay_inst *I, unsigned s)
{
   /* SENDs operate on entire GRFs at a time, so align UGPRs to GRFs. This
    * includes UGPR->UMEM moves which lower to SENDs.
    */
   if ((I->op == JAY_OPCODE_SEND && I->src[s].file == UGPR) ||
       (I->dst.file == UMEM)) {
      return jay_ugpr_per_grf(shader);
   }

   /* If the destination is 64-bit, we need the sources to be aligned. Along
    * with a suitable partitioning, this ensures only the aligned low half of
    * a strided register is used, preventing invalid assembly like:
    *
    *    mov.s64 g40, g42.1<2>:s32
    *
    * ..which would violate the rule:
    *
    *    Register Regioning patterns where register data bit location of the LSB
    *    of the channels are changed between source and destination are not
    *    supported except for broadcast of a scalar.
    */
   return jay_type_vector_length(I->type);
}

/*
 * Return the required alignment for the register assigned to a destination.
 */
static inline unsigned
jay_dst_alignment(jay_shader *shader, const jay_inst *I)
{
   /* SENDs write entire GRFs, so align UGPRs to GRFs. Similarly for any
    * instructions involving accumulators:
    *
    *    Register Regioning patterns where register data bit locations are
    *    changed between source and destination are not supported when an
    *    accumulator is used as an implicit source or an explicit source in an
    *    instruction. (TODO)
    */
   if (I->dst.file == UGPR &&
       (I->op == JAY_OPCODE_SEND ||
        (I->op == JAY_OPCODE_MOV && I->src[0].file == UMEM) ||
        I->op == JAY_OPCODE_MUL_32)) {

      return jay_ugpr_per_grf(shader);
   }

   /* If any source is 64-bit, align the destination to 64-bit too. As above. */
   return jay_type_vector_length(jay_src_type(I, 0));
}

static inline bool
jay_inst_is_uniform(const jay_inst *I)
{
   if (I->op == JAY_OPCODE_SEND)
      return jay_send_uniform(I);

   return jay_is_uniform(I->dst) ||
          (I->dst.file == J_ADDRESS && jay_is_uniform(I->src[0])) ||
          I->cond_flag.file == UFLAG ||
          I->op == JAY_OPCODE_SYNC ||
          I->dst.file == FLAG ||
          (I->dst.file == J_ARF && !jay_is_null(I->dst));
}

unsigned jay_simd_split(const jay_shader *s, const jay_inst *I);

static inline unsigned
jay_simd_width_logical(const jay_shader *s, const jay_inst *I)
{
   unsigned base = jay_inst_is_uniform(I) ? 1 : s->dispatch_width;

   /* Handle vectors-of-UGPR operations with special care for 64-bit */
   unsigned vec_per_channel = jay_type_vector_length(I->type);
   unsigned dst_size = jay_num_values(I->dst);
   assert(util_is_aligned(dst_size, vec_per_channel));

   if (base == 1 && dst_size > vec_per_channel && I->op != JAY_OPCODE_SEND) {
      assert(util_is_power_of_two_nonzero(dst_size) && vec_per_channel == 1);
      base = dst_size;
   }

   return base;
}

static inline unsigned
jay_simd_width_physical(jay_shader *s, const jay_inst *I)
{
   return jay_simd_width_logical(s, I) >> jay_simd_split(s, I);
}

/*
 * Returns the number of physical instructions emitted for each logical
 * instruction not accounting for SIMD split. That is, the number of
 * instructions that macros will expand to in jay_to_binary or 1 for non-macros.
 */
static inline unsigned
jay_macro_length(const jay_inst *I)
{
   bool macro = (I->op == JAY_OPCODE_MUL_32 ||
                 I->op == JAY_OPCODE_SHUFFLE ||
                 I->op == JAY_OPCODE_LOOP_ONCE);
   return macro ? 2 : 1;
}

static inline bool
jay_is_no_mask(const jay_inst *I)
{
   return jay_inst_is_uniform(I) ||
          I->broadcast_flag ||
          I->op == JAY_OPCODE_QUAD_SWIZZLE ||
          I->op == JAY_OPCODE_DESWIZZLE_EVEN ||
          I->op == JAY_OPCODE_DESWIZZLE_ODD ||
          I->op == JAY_OPCODE_OFFSET_PACKED_PIXEL_COORDS ||
          I->op == JAY_OPCODE_LANE_ID_8 ||
          I->op == JAY_OPCODE_LANE_ID_EXPAND;
}

/**
 * Representation of an (implemented) function in the Jay IR. This corresponds
 * to nir_function_impl in NIR.
 */
typedef struct jay_function {
   struct list_head link;

   /* Parent pointer for convenience */
   struct jay_shader *shader;

   /* Set of SSA indices of defs that are dead immediately after being written
    * (because they are never read but cannot be DCE'd).
    */
   BITSET_WORD *dead_defs;

   /* Register demand metadata calculated & used in RA */
   unsigned demand[JAY_NUM_SSA_FILES];

   unsigned num_blocks;
   struct list_head blocks;
   bool is_entrypoint;

   uint32_t ssa_alloc;
} jay_function;

static inline jay_function *
jay_new_function(jay_shader *s)
{
   jay_function *f = rzalloc(s, jay_function);
   list_inithead(&f->blocks);

   f->shader = s;
   f->ssa_alloc = 1; /* skip null */

   list_add(&f->link, &s->functions);
   return f;
}

static inline jay_function *
jay_shader_get_entrypoint(jay_shader *s)
{
   /* TODO: Multifunction shaders */
   assert(list_is_singular(&s->functions));
   return list_first_entry(&s->functions, jay_function, link);
}

static inline unsigned
jay_num_regs(jay_shader *shader, enum jay_file file)
{
   assert(file < JAY_NUM_SSA_FILES);

   if (file < JAY_NUM_RA_FILES)
      return shader->num_regs[file];
   else if (file == FLAG)
      return shader->dispatch_width == 32 ? 4 : 8;
   else if (file == UFLAG)
      return 0;
   else
      return 1 /* TODO: We don't have address or accumulator RA yet */;
}

static inline enum jay_stride
jay_def_stride(const jay_shader *shader, jay_def x)
{
   assert(x.file == GPR);
   return jay_gpr_to_stride(&shader->partition, x.reg);
}

/* Represents an allocated register number with file in the top 3 bits. */
typedef uint16_t jay_reg;

/** Represents a set of registers that may be clobbered for lowering swaps */
struct jay_temp_regs {
   jay_reg gpr, gpr2, ugpr, ugpr2;
};

/**
 * A basic block representation
 */
typedef struct jay_block {
   struct list_head link;
   struct list_head instructions;

   /** Control flow graph */
   struct jay_block *successors[2];
   struct util_dynarray predecessors;

   /** Index of the block in source order */
   unsigned index;

   /** Liveness analysis results */
   struct u_sparse_bitset live_in;
   struct u_sparse_bitset live_out;

   /**
    * After register allocation but before going out-of-SSA, registers that
    * are free at the logical end of the block (before phi_src). These will
    * be clobbered by the out-of-SSA pass.
    */
   struct jay_temp_regs temps_out;

   /**
    * Is this block a loop header?  If not, all of its predecessors precede it
    * in source order.
    */
   bool loop_header;

   /** True if all non-exited lanes execute this block together */
   bool uniform;

   /** Pretty printing based on original structured control flow */
   uint8_t indent;
} jay_block;

static inline jay_block *
jay_new_block(jay_function *f)
{
   jay_block *block = rzalloc(f, jay_block);

   util_dynarray_init(&block->predecessors, block);
   list_inithead(&block->instructions);

   block->index = f->num_blocks++;
   return block;
}

static inline bool
jay_op_is_control_flow(enum jay_opcode op)
{
   return op >= JAY_OPCODE_BRD && op <= JAY_OPCODE_LOOP_ONCE;
}

/**
 * Returns the control flow instruction at the end of a block or NULL.
 */
static inline jay_inst *
jay_block_ending_jump(jay_block *block)
{
   jay_inst *last = list_is_empty(&block->instructions) ?
                       NULL :
                       list_last_entry(&block->instructions, jay_inst, link);
   return last && jay_op_is_control_flow(last->op) ? last : NULL;
}

static inline unsigned
jay_num_predecessors(jay_block *block)
{
   return util_dynarray_num_elements(&block->predecessors, jay_block *);
}

static inline unsigned
jay_num_successors(jay_block *block)
{
   static_assert(ARRAY_SIZE(block->successors) == 2);
   return !!block->successors[0] + !!block->successors[1];
}

static inline jay_block *
jay_first_predecessor(jay_block *block)
{
   if (jay_num_predecessors(block) == 0)
      return NULL;

   return *util_dynarray_element(&block->predecessors, struct jay_block *, 0);
}

/* Block worklist helpers */

#define jay_worklist_push_head(w, block) u_worklist_push_head(w, block, index)
#define jay_worklist_push_tail(w, block) u_worklist_push_tail(w, block, index)
#define jay_worklist_peek_head(w)        u_worklist_peek_head(w, jay_block, index)
#define jay_worklist_pop_head(w)         u_worklist_pop_head(w, jay_block, index)
#define jay_worklist_peek_tail(w)        u_worklist_peek_tail(w, jay_block, index)
#define jay_worklist_pop_tail(w)         u_worklist_pop_tail(w, jay_block, index)

/* Iterators */

#define jay_foreach_function(s, v)                                             \
   list_for_each_entry(jay_function, v, &s->functions, link)

#define jay_foreach_block(f, v)                                                \
   list_for_each_entry(jay_block, v, &f->blocks, link)

#define jay_foreach_block_safe(f, v)                                           \
   list_for_each_entry_safe(jay_block, v, &f->blocks, link)

#define jay_foreach_block_rev(f, v)                                            \
   list_for_each_entry_rev(jay_block, v, &f->blocks, link)

#define jay_foreach_block_from(f, from, v)                                     \
   list_for_each_entry_from(jay_block, v, from, &f->blocks, link)

#define jay_foreach_block_from_rev(f, from, v)                                 \
   list_for_each_entry_from_rev(jay_block, v, from, &f->blocks, link)

#define jay_foreach_inst_in_block(block, v)                                    \
   list_for_each_entry(jay_inst, v, &(block)->instructions, link)

#define jay_foreach_inst_in_block_rev(block, v)                                \
   list_for_each_entry_rev(jay_inst, v, &(block)->instructions, link)

#define jay_foreach_inst_in_block_safe(block, v)                               \
   list_for_each_entry_safe(jay_inst, v, &(block)->instructions, link)

#define jay_foreach_inst_in_block_safe_rev(block, v)                           \
   list_for_each_entry_safe_rev(jay_inst, v, &(block)->instructions, link)

#define jay_foreach_inst_in_block_from(block, v, from)                         \
   list_for_each_entry_from(jay_inst, v, from, &(block)->instructions, link)

#define jay_foreach_inst_in_block_from_rev(block, v, from)                     \
   list_for_each_entry_from_rev(jay_inst, v, from, &(block)->instructions, link)

#define jay_foreach_inst_in_func(func, block, v)                               \
   jay_foreach_block(func, block)                                              \
      jay_foreach_inst_in_block(block, v)

#define jay_foreach_inst_in_func_rev(func, block, v)                           \
   jay_foreach_block_rev(func, block)                                          \
      jay_foreach_inst_in_block_rev(block, v)

#define jay_foreach_inst_in_func_safe(func, block, v)                          \
   jay_foreach_block(func, block)                                              \
      jay_foreach_inst_in_block_safe(block, v)

#define jay_foreach_inst_in_func_safe_rev(func, block, v)                      \
   jay_foreach_block_rev(func, block)                                          \
      jay_foreach_inst_in_block_safe_rev(block, v)

#define jay_foreach_inst_in_shader(s, func, inst)                              \
   jay_foreach_function(s, func)                                               \
      jay_foreach_inst_in_func(func, v_block, inst)

#define jay_foreach_inst_in_shader_safe(s, func, inst)                         \
   jay_foreach_function(s, func)                                               \
      jay_foreach_inst_in_func_safe(func, v_block, inst)

#define jay_foreach_successor(blk, v)                                          \
   jay_block *v;                                                               \
   jay_block **_v;                                                             \
   for (_v = (jay_block **) &blk->successors[0], v = *_v;                      \
        v != NULL && _v < (jay_block **) &blk->successors[2]; _v++, v = *_v)

#define jay_foreach_predecessor(blk, v)                                        \
   util_dynarray_foreach(&blk->predecessors, jay_block *, v)

#define jay_foreach_src(inst, s) for (unsigned s = 0; s < inst->num_srcs; ++s)

#define jay_foreach_src_rev(inst, s)                                           \
   for (signed s = inst->num_srcs - 1; s >= 0; --s)

#define jay_foreach_ssa_src(I, s)                                              \
   jay_foreach_src(I, s)                                                       \
      if (jay_is_ssa(I->src[s]) && !jay_is_null(I->src[s]))

#define jay_foreach_ssa_src_rev(I, s)                                          \
   jay_foreach_src_rev(I, s)                                                   \
      if (jay_is_ssa(I->src[s]) && !jay_is_null(I->src[s]))

#define jay_foreach_index(def, c, idx)                                         \
   jay_foreach_comp(def, c)                                                    \
      for (uint32_t idx = jay_channel(def, c); idx != 0; idx = 0)

#define jay_foreach_index_rev(def, c, idx)                                     \
   jay_foreach_comp_rev(def, c)                                                \
      for (uint32_t idx = jay_channel(def, c); idx != 0; idx = 0)

#define jay_foreach_src_index(I, s, c, i)                                      \
   jay_foreach_ssa_src(I, s)                                                   \
      jay_foreach_index(I->src[s], c, i)

#define jay_foreach_src_index_rev(I, s, c, i)                                  \
   jay_foreach_ssa_src_rev(I, s)                                               \
      jay_foreach_index_rev(I->src[s], c, i)

#define jay_foreach_dst(I, d)                                                  \
   for (unsigned _d = 0; _d < 2; ++_d)                                         \
      for (jay_def d = (_d ? I->cond_flag : I->dst); !jay_is_null(d);          \
           d = jay_null())

#define jay_foreach_dst_index(I, d, i)                                         \
   jay_foreach_dst(I, d)                                                       \
      jay_foreach_index(d, _c, i)

/*
 * Phi iterators take advantage of the known position of phis in the block.
 */
#define jay_foreach_phi_src_in_block(block, phi)                               \
   jay_foreach_inst_in_block_safe_rev(block, phi)                              \
      if (jay_op_is_control_flow(phi->op))                                     \
         continue;                                                             \
      else if (phi->op != JAY_OPCODE_PHI_SRC)                                  \
         break;                                                                \
      else

#define jay_foreach_phi_dst_in_block(block, phi)                               \
   jay_foreach_inst_in_block(block, phi)                                       \
      if (phi->op != JAY_OPCODE_PHI_DST)                                       \
         break;                                                                \
      else

#define jay_foreach_preload(func, preload)                                     \
   jay_foreach_inst_in_block_safe(jay_first_block(func), preload)              \
      if (I->op != JAY_OPCODE_PRELOAD)                                         \
         break;                                                                \
      else

static inline jay_block *
jay_first_block(jay_function *f)
{
   assert(!list_is_empty(&f->blocks));
   jay_block *first_block = list_first_entry(&f->blocks, jay_block, link);
   assert(first_block->index == 0);
   return first_block;
}

static inline jay_inst *
jay_first_inst(jay_block *block)
{
   if (list_is_empty(&block->instructions))
      return NULL;
   else
      return list_first_entry(&block->instructions, jay_inst, link);
}

static inline jay_block *
jay_last_block(jay_function *f)
{
   if (list_is_empty(&f->blocks))
      return NULL;
   else
      return list_last_entry(&f->blocks, jay_block, link);
}

static inline jay_inst *
jay_last_inst(jay_block *block)
{
   if (list_is_empty(&block->instructions))
      return NULL;
   else
      return list_last_entry(&block->instructions, jay_inst, link);
}

static inline jay_block *
jay_next_block(jay_block *block)
{
   return list_first_entry(&(block->link), jay_block, link);
}

static inline void
jay_block_add_successor(jay_block *block, jay_block *succ)
{
   unsigned i = block->successors[0] ? 1 : 0;

   assert(succ && block->successors[0] != succ && block->successors[1] != succ);
   assert(block->successors[i] == NULL && "at most 2 successors");

   block->successors[i] = succ;
   util_dynarray_append(&(succ->predecessors), block);
}

static inline unsigned
jay_source_last_use_bit(const jay_def *srcs, unsigned src_idx)
{
   assert(jay_is_ssa(srcs[src_idx]) && "precondition");
   unsigned i = 0;

   for (unsigned s = 0; s < src_idx; ++s) {
      jay_foreach_index(srcs[s], c, idx) {
         i++;
      }
   }

   return i;
}

#define jay_foreach_killed(I, s, c)                                            \
   for (unsigned _kill_idx = 0; _kill_idx == 0; _kill_idx = 1)                 \
      jay_foreach_src_index(I, s, c, idx)                                      \
         for (unsigned _k = _kill_idx++; _k != ~0; _k = ~0)                    \
            if (BITSET_TEST(I->last_use, _k))

/* Helper to run a pass */
#define JAY_PASS(shader, pass, ...)                                            \
   do {                                                                        \
      pass(shader, ##__VA_ARGS__);                                             \
      jay_validate(shader, #pass);                                             \
   } while (0)

#define JAY_DEFINE_FUNCTION_PASS(name, per_func)                               \
   void name(jay_shader *s)                                                    \
   {                                                                           \
      jay_foreach_function(s, f) {                                             \
         per_func(f);                                                          \
      }                                                                        \
   }
