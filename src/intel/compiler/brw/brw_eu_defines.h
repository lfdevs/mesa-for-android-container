/*
 * Copyright © 2006 Intel Corporation
 * SPDX-License-Identifier: MIT
 *
 * Intel funded Tungsten Graphics to develop this 3D driver.
 * File originally authored by: Keith Whitwell <keithw@vmware.com>
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include "util/macros.h"
#include "compiler/gen/gen_enums.h"
#include "dev/intel_device_info.h"

/* The following hunk, up-to "Execution Unit" is used by both the
 * intel/compiler and i965 codebase. */

#define INTEL_MASK(high, low) (((1u<<((high)-(low)+1))-1)<<(low))
/* Using the GNU statement expression extension */
#define SET_FIELD(value, field)                                         \
   ({                                                                   \
      uint32_t fieldval = (uint32_t)(value) << field ## _SHIFT;         \
      assert((fieldval & ~ field ## _MASK) == 0);                       \
      fieldval & field ## _MASK;                                        \
   })

#define SET_BITS(value, high, low)                                      \
   ({                                                                   \
      const uint32_t fieldval = (uint32_t)(value) << (low);             \
      assert((fieldval & ~INTEL_MASK(high, low)) == 0);                 \
      fieldval & INTEL_MASK(high, low);                                 \
   })

#define GET_BITS(data, high, low) ((data & INTEL_MASK((high), (low))) >> (low))
#define GET_FIELD(word, field) (((word)  & field ## _MASK) >> field ## _SHIFT)

# define GFX7_GS_CONTROL_DATA_FORMAT_GSCTL_CUT		0
# define GFX7_GS_CONTROL_DATA_FORMAT_GSCTL_SID		1

/* Execution Unit (EU) defines
 */

#define BRW_ALIGN_1   0
#define BRW_ALIGN_16  1

#define BRW_ADDRESS_DIRECT                        0
#define BRW_ADDRESS_REGISTER_INDIRECT_REGISTER    1

#define BRW_CHANNEL_X     0
#define BRW_CHANNEL_Y     1
#define BRW_CHANNEL_Z     2
#define BRW_CHANNEL_W     3

enum brw_compression {
   BRW_COMPRESSION_NONE       = 0,
   BRW_COMPRESSION_2NDHALF    = 1,
   BRW_COMPRESSION_COMPRESSED = 2,
};

enum ENUM_PACKED brw_conditional_mod {
   BRW_CONDITIONAL_NONE = 0,
   BRW_CONDITIONAL_Z    = 1,
   BRW_CONDITIONAL_NZ   = 2,
   BRW_CONDITIONAL_EQ   = 1,	/* Z */
   BRW_CONDITIONAL_NEQ  = 2,	/* NZ */
   BRW_CONDITIONAL_G    = 3,
   BRW_CONDITIONAL_GE   = 4,
   BRW_CONDITIONAL_L    = 5,
   BRW_CONDITIONAL_LE   = 6,
   BRW_CONDITIONAL_R    = 7,    /* Gen <= 5 */
   BRW_CONDITIONAL_O    = 8,
   BRW_CONDITIONAL_U    = 9,
};

#define BRW_DEBUG_NONE        0
#define BRW_DEBUG_BREAKPOINT  1

enum ENUM_PACKED brw_execution_size {
   BRW_EXECUTE_1  = 0,
   BRW_EXECUTE_2  = 1,
   BRW_EXECUTE_4  = 2,
   BRW_EXECUTE_8  = 3,
   BRW_EXECUTE_16 = 4,
   BRW_EXECUTE_32 = 5,
};

enum ENUM_PACKED brw_horizontal_stride {
   BRW_HORIZONTAL_STRIDE_0 = 0,
   BRW_HORIZONTAL_STRIDE_1 = 1,
   BRW_HORIZONTAL_STRIDE_2 = 2,
   BRW_HORIZONTAL_STRIDE_4 = 3,
};

enum ENUM_PACKED brw_align1_3src_src_horizontal_stride {
   BRW_ALIGN1_3SRC_SRC_HORIZONTAL_STRIDE_0 = 0,
   BRW_ALIGN1_3SRC_SRC_HORIZONTAL_STRIDE_1 = 1,
   BRW_ALIGN1_3SRC_SRC_HORIZONTAL_STRIDE_2 = 2,
   BRW_ALIGN1_3SRC_SRC_HORIZONTAL_STRIDE_4 = 3,
};

enum ENUM_PACKED brw_align1_3src_dst_horizontal_stride {
   BRW_ALIGN1_3SRC_DST_HORIZONTAL_STRIDE_1 = 0,
   BRW_ALIGN1_3SRC_DST_HORIZONTAL_STRIDE_2 = 1,
};

#define BRW_INSTRUCTION_NORMAL    0
#define BRW_INSTRUCTION_SATURATE  1

#define BRW_MASK_ENABLE   0
#define BRW_MASK_DISABLE  1

/** @{
 *
 * Gfx6 has replaced "mask enable/disable" with WECtrl, which is
 * effectively the same but much simpler to think about.  Now, there
 * are two contributors ANDed together to whether channels are
 * executed: The predication on the instruction, and the channel write
 * enable.
 */
/**
 * This is the default value.  It means that a channel's write enable is set
 * if the per-channel IP is pointing at this instruction.
 */
#define BRW_WE_NORMAL		0
/**
 * This is used like BRW_MASK_DISABLE, and causes all channels to have
 * their write enable set.  Note that predication still contributes to
 * whether the channel actually gets written.
 */
#define BRW_WE_ALL		1
/** @} */

enum ENUM_PACKED opcode {
   /* These are the actual hardware instructions. */
   BRW_OPCODE_ILLEGAL,
   BRW_OPCODE_SYNC,
   BRW_OPCODE_MOV,
   BRW_OPCODE_SEL,
   BRW_OPCODE_MOVI,
   BRW_OPCODE_NOT,
   BRW_OPCODE_AND,
   BRW_OPCODE_OR,
   BRW_OPCODE_XOR,
   BRW_OPCODE_BFN,
   BRW_OPCODE_SHR,
   BRW_OPCODE_SHL,
   BRW_OPCODE_SMOV,
   BRW_OPCODE_ASR,
   BRW_OPCODE_ROR,  /**< Gfx11+ */
   BRW_OPCODE_ROL,  /**< Gfx11+ */
   BRW_OPCODE_CMP,
   BRW_OPCODE_CMPN,
   BRW_OPCODE_CSEL,
   BRW_OPCODE_BFREV,
   BRW_OPCODE_BFE,
   BRW_OPCODE_BFI1,
   BRW_OPCODE_BFI2,
   BRW_OPCODE_JMPI,
   BRW_OPCODE_BRD,
   BRW_OPCODE_IF,
   BRW_OPCODE_BRC,
   BRW_OPCODE_ELSE,
   BRW_OPCODE_ENDIF,
   BRW_OPCODE_DO, /**< Used as pseudo opcode, will be moved later. */
   BRW_OPCODE_WHILE,
   BRW_OPCODE_BREAK,
   BRW_OPCODE_CONTINUE,
   BRW_OPCODE_HALT,
   BRW_OPCODE_CALLA,
   BRW_OPCODE_CALL,
   BRW_OPCODE_RET,
   BRW_OPCODE_GOTO,
   BRW_OPCODE_JOIN,
   BRW_OPCODE_WAIT,
   BRW_OPCODE_SEND,
   BRW_OPCODE_SENDC,
   BRW_OPCODE_SENDS,
   BRW_OPCODE_SENDSC,
   BRW_OPCODE_MATH,
   BRW_OPCODE_ADD,
   BRW_OPCODE_MUL,
   BRW_OPCODE_AVG,
   BRW_OPCODE_FRC,
   BRW_OPCODE_RNDU,
   BRW_OPCODE_RNDD,
   BRW_OPCODE_RNDE,
   BRW_OPCODE_RNDZ,
   BRW_OPCODE_MAC,
   BRW_OPCODE_MACL,
   BRW_OPCODE_MACH,
   BRW_OPCODE_LZD,
   BRW_OPCODE_FBH,
   BRW_OPCODE_FBL,
   BRW_OPCODE_CBIT,
   BRW_OPCODE_ADDC,
   BRW_OPCODE_SUBB,
   BRW_OPCODE_ADD3, /* Gen12+ only */
   BRW_OPCODE_DP4,
   BRW_OPCODE_SRND, /* Xe2+ only */
   BRW_OPCODE_DPH,
   BRW_OPCODE_DP3,
   BRW_OPCODE_DP2,
   BRW_OPCODE_DP4A, /**< Gfx12+ */
   BRW_OPCODE_LINE,
   BRW_OPCODE_DPAS,  /**< Gfx12.5+ */
   BRW_OPCODE_PLN, /**< Up until Gfx9 */
   BRW_OPCODE_MAD,
   BRW_OPCODE_LRP,
   BRW_OPCODE_MADM,
   BRW_OPCODE_NOP,

   NUM_BRW_OPCODES,

   /**
    * The position/ordering of the arguments are defined
    * by the enum fb_write_logical_srcs.
    */
   FS_OPCODE_FB_WRITE_LOGICAL = NUM_BRW_OPCODES,

   FS_OPCODE_FB_READ_LOGICAL,

   SHADER_OPCODE_RCP,
   SHADER_OPCODE_RSQ,
   SHADER_OPCODE_SQRT,
   SHADER_OPCODE_EXP2,
   SHADER_OPCODE_LOG2,
   SHADER_OPCODE_POW,
   SHADER_OPCODE_INT_QUOTIENT,
   SHADER_OPCODE_INT_REMAINDER,
   SHADER_OPCODE_SIN,
   SHADER_OPCODE_COS,

   /**
    * A generic "send" opcode.  The first two sources are the message
    * descriptor and extended message descriptor respectively.  The third
    * and optional fourth sources are the message payload
    */
   SHADER_OPCODE_SEND,

   /**
    * A variant of SEND that collects its sources to form an input.
    *
    * Source 0:    Message descriptor ("desc").
    * Source 1:    Message extended descriptor ("ex_desc").
    * Source 2:    Before register allocation must be BAD_FILE,
    *              after that, the ARF scalar register containing
    *              the (physical) numbers of the payload sources.
    * Source 3..n: Payload sources.  For this opcode, they must each
    *              have the size of a physical GRF.
    */
   SHADER_OPCODE_SEND_GATHER,

   /**
    * An "undefined" write which does nothing but indicates to liveness that
    * we don't care about any values in the register which predate this
    * instruction.  Used to prevent partial writes from causing issues with
    * live ranges.
    */
   SHADER_OPCODE_UNDEF,

   /**
    * A sampler instruction, see brw_tex_inst::sampler_opcode enum
    * sampler_opcode.
    */
   SHADER_OPCODE_SAMPLER,

   /**
    * Combines multiple sources of size 1 into a larger virtual GRF.
    * For example, parameters for a send-from-GRF message.  Or, updating
    * channels of a size 4 VGRF used to store vec4s such as texturing results.
    *
    * This will be lowered into MOVs from each source to consecutive offsets
    * of the destination VGRF.
    *
    * src[0] may be BAD_FILE.  If so, the lowering pass skips emitting the MOV,
    * but still reserves the first channel of the destination VGRF.  This can be
    * used to reserve space for, say, a message header set up by the generators.
    */
   SHADER_OPCODE_LOAD_PAYLOAD,

   /**
    * Packs a number of sources into a single value. Unlike LOAD_PAYLOAD, this
    * acts intra-channel, obtaining the final value for each channel by
    * combining the sources values for the same channel, the first source
    * occupying the lowest bits and the last source occupying the highest
    * bits.
    */
   FS_OPCODE_PACK,

   SHADER_OPCODE_RND_MODE,
   SHADER_OPCODE_FLOAT_CONTROL_MODE,

   /**
    * Memory fence messages.
    *
    * Source 0: Must be register g0, used as header.
    * Source 1: Immediate bool to indicate whether control is returned to the
    *           thread only after the fence has been honored.
    */
   SHADER_OPCODE_MEMORY_FENCE,

   /**
    * Scheduling-only fence.
    *
    * Sources can be used to force a stall until the registers in those are
    * available.  This might generate MOVs or SYNC_NOPs (Gfx12+).
    */
   FS_OPCODE_SCHEDULING_FENCE,

   SHADER_OPCODE_SCRATCH_HEADER,

   /**
    * Gfx8+ SIMD8 URB messages.
    */
   SHADER_OPCODE_URB_READ_LOGICAL,
   SHADER_OPCODE_URB_WRITE_LOGICAL,

   /**
    * Return the index of the first enabled live channel and assign it to
    * to the first component of the destination.  Frequently used as input
    * for the BROADCAST pseudo-opcode.
    *
    * Source 0: A value.
    * Source 1: Index from Value to broadcast.
    * Source 2: A size in byte of the value register.
    */
   SHADER_OPCODE_FIND_LIVE_CHANNEL,

   /**
    * Return the index of the last enabled live channel and assign it to
    * the first component of the destination.
    */
   SHADER_OPCODE_FIND_LAST_LIVE_CHANNEL,

   /**
    * Return the current execution mask and assign it to the first component
    * of the destination.
    *
    * \sa opcode::FS_OPCODE_LOAD_LIVE_CHANNELS
    */
   SHADER_OPCODE_LOAD_LIVE_CHANNELS,

   /**
    * Return the current execution mask in the specified flag subregister.
    * Can be CSE'ed more easily than a plain MOV from the ce0 ARF register.
    */
   FS_OPCODE_LOAD_LIVE_CHANNELS,

   /**
    * Pick the channel from its first source register given by the index
    * specified as second source.  Useful for variable indexing of surfaces.
    *
    * Note that because the result of this instruction is by definition
    * uniform and it can always be splatted to multiple channels using a
    * scalar regioning mode, only the first channel of the destination region
    * is guaranteed to be updated, which implies that BROADCAST instructions
    * should usually be marked force_writemask_all.
    */
   SHADER_OPCODE_BROADCAST,

   /* Pick the channel from its first source register given by the index
    * specified as second source.
    *
    * This is similar to the BROADCAST instruction except that it takes a
    * dynamic index and potentially puts a different value in each output
    * channel.
    */
   SHADER_OPCODE_SHUFFLE,

   /* Combine all values in each subset (cluster) of channels using an operation,
    * and broadcast the result to all channels in the subset.
    *
    * Source 0: Value.
    * Source 1: Immediate with brw_reduce_op.
    * Source 2: Immediate with cluster size.
    */
   SHADER_OPCODE_REDUCE,

   /* Combine values of previous channels using an operation.  Inclusive scan
    * will include the value of the channel itself in the channel result.
    *
    * Source 0: Value.
    * Source 1: Immediate with brw_reduce_op.
    */
   SHADER_OPCODE_INCLUSIVE_SCAN,
   SHADER_OPCODE_EXCLUSIVE_SCAN,

   /* Check if any or all values in each subset (cluster) of channels are set,
    * and broadcast the result to all channels in the subset.
    *
    * Source 0: Boolean value.
    * Source 1: Immediate with cluster size.
    */
   SHADER_OPCODE_VOTE_ANY,
   SHADER_OPCODE_VOTE_ALL,

   /* Check if the values of all channels are equal, and broadcast the result
    * to all channels.
    *
    * Source 0: Value.
    */
   SHADER_OPCODE_VOTE_EQUAL,

   /* Produces a mask from the boolean value from all channels, and broadcast
    * the result to all channels.
    *
    * Source 0: Boolean value.
    */
   SHADER_OPCODE_BALLOT,

   /* Select between src0 and src1 based on channel enables.
    *
    * This instruction copies src0 into the enabled channels of the
    * destination and copies src1 into the disabled channels.
    */
   SHADER_OPCODE_SEL_EXEC,

   /* Swap values inside a quad based on the direction.
    *
    * Source 0: Value.
    * Source 1: Immediate with brw_swap_direction.
    */
   SHADER_OPCODE_QUAD_SWAP,

   /* Read value from the first live channel and broadcast the result
    * to all channels.
    *
    * Source 0: Value.
    */
   SHADER_OPCODE_READ_FROM_LIVE_CHANNEL,

   /* Read value from a specified channel and broadcast the result
    * to all channels.
    *
    * Source 0: Value.
    * Source 1: Index of the channel to pick value from.
    */
   SHADER_OPCODE_READ_FROM_CHANNEL,

   /* This turns into an align16 mov from src0 to dst with a swizzle
    * provided as an immediate in src1.
    */
   SHADER_OPCODE_QUAD_SWIZZLE,

   /* Take every Nth element in src0 and broadcast it to the group of N
    * channels in which it lives in the destination.  The offset within the
    * cluster is given by src1 and the cluster size is given by src2.
    */
   SHADER_OPCODE_CLUSTER_BROADCAST,

   SHADER_OPCODE_INTERLOCK,

   /** Target for a HALT
    *
    * All HALT instructions in a shader must target the same jump point and
    * that point is denoted by a HALT_TARGET instruction.
    */
   SHADER_OPCODE_HALT_TARGET,

   FS_OPCODE_DDX_COARSE,
   FS_OPCODE_DDX_FINE,
   /**
    * Compute dFdy(), dFdyCoarse(), or dFdyFine().
    */
   FS_OPCODE_DDY_COARSE,
   FS_OPCODE_DDY_FINE,
   FS_OPCODE_PIXEL_X,
   FS_OPCODE_PIXEL_Y,
   FS_OPCODE_UNIFORM_PULL_CONSTANT_LOAD,
   FS_OPCODE_VARYING_PULL_CONSTANT_LOAD_LOGICAL,
   FS_OPCODE_PACK_HALF_2x16_SPLIT,
   FS_OPCODE_INTERPOLATE_AT_SAMPLE,
   FS_OPCODE_INTERPOLATE_AT_SHARED_OFFSET,
   FS_OPCODE_INTERPOLATE_AT_PER_SLOT_OFFSET,

   SHADER_OPCODE_LOAD_ATTRIBUTE_PAYLOAD,

   /**
    * GLSL barrier()
    */
   SHADER_OPCODE_BARRIER,

   /**
    * Calculate the high 32-bits of a 32x32 multiply.
    */
   SHADER_OPCODE_MULH,

   /** Signed subtraction with saturation. */
   SHADER_OPCODE_ISUB_SAT,

   /** Unsigned subtraction with saturation. */
   SHADER_OPCODE_USUB_SAT,

   /**
    * A MOV that uses VxH indirect addressing.
    *
    * Source 0: A register to start from (HW_REG).
    * Source 1: An indirect offset (in bytes, UD GRF).
    * Source 2: The length of the region that could be accessed (in bytes,
    *           UD immediate).
    */
   SHADER_OPCODE_MOV_INDIRECT,

   /** Fills out a relocatable immediate */
   SHADER_OPCODE_MOV_RELOC_IMM,

   SHADER_OPCODE_BTD_SPAWN_LOGICAL,
   SHADER_OPCODE_BTD_RETIRE_LOGICAL,

   SHADER_OPCODE_READ_ARCH_REG,

   SHADER_OPCODE_LOAD_SUBGROUP_INVOCATION,

   RT_OPCODE_TRACE_RAY_LOGICAL,

   SHADER_OPCODE_MEMORY_LOAD_LOGICAL,
   SHADER_OPCODE_MEMORY_STORE_LOGICAL,
   SHADER_OPCODE_MEMORY_ATOMIC_LOGICAL,

   /* Ends a block moving to the next one.  See brw_cfg for details. */
   SHADER_OPCODE_FLOW,

   /**
    * Load a VGRF to generate an SSA value.
    *
    * Acts as a scheduling barrier.
    */
   SHADER_OPCODE_LOAD_REG,

   SHADER_OPCODE_LSC_FILL,
   SHADER_OPCODE_LSC_SPILL,
};

enum send_srcs {
   /** The 32-bit message descriptor (can be a register) */
   SEND_SRC_DESC,
   /** The 32-bit extended message descriptor (can be a register) */
   SEND_SRC_EX_DESC,
   /** The leading register for the first SEND payload */
   SEND_SRC_PAYLOAD1,
   /** The leading register for the second split-SEND payload */
   SEND_SRC_PAYLOAD2,

   SEND_NUM_SRCS
};

enum send_gather_srcs {
   SEND_GATHER_SRC_DESC,
   SEND_GATHER_SRC_EX_DESC,
   SEND_GATHER_SRC_SCALAR,
   SEND_GATHER_SRC_PAYLOAD
};

enum fb_write_logical_srcs {
   FB_WRITE_LOGICAL_SRC_COLOR0,      /* REQUIRED */
   FB_WRITE_LOGICAL_SRC_COLOR1,      /* for dual source blend messages */
   FB_WRITE_LOGICAL_SRC_SRC0_ALPHA,
   FB_WRITE_LOGICAL_SRC_SRC_DEPTH,   /* gl_FragDepth */
   FB_WRITE_LOGICAL_SRC_SRC_STENCIL, /* gl_FragStencilRefARB */
   FB_WRITE_LOGICAL_SRC_OMASK,       /* Sample Mask (gl_SampleMask) */
   FB_WRITE_LOGICAL_NUM_SRCS
};

enum tex_logical_srcs {
   /** REQUIRED: Texture surface index */
   TEX_LOGICAL_SRC_SURFACE,
   /** Texture sampler index */
   TEX_LOGICAL_SRC_SAMPLER,
   /** Packed offsets */
   TEX_LOGICAL_SRC_PACKED_OFFSETS,
   /** Sampler payloads */
   TEX_LOGICAL_SRC_PAYLOAD0,
   TEX_LOGICAL_SRC_PAYLOAD1,
   TEX_LOGICAL_SRC_PAYLOAD2,
   TEX_LOGICAL_SRC_PAYLOAD3,
   TEX_LOGICAL_SRC_PAYLOAD4,
   TEX_LOGICAL_SRC_PAYLOAD5,
   TEX_LOGICAL_SRC_PAYLOAD6,
   TEX_LOGICAL_SRC_PAYLOAD7,
   TEX_LOGICAL_SRC_PAYLOAD8,
   TEX_LOGICAL_SRC_PAYLOAD9,
   TEX_LOGICAL_SRC_PAYLOAD10,
   TEX_LOGICAL_SRC_PAYLOAD11,
   TEX_LOGICAL_SRC_PAYLOAD12,

   TEX_LOGICAL_NUM_SRCS,
};

enum pull_uniform_constant_srcs {
   /** enum lsc_addr_surface_type (as UD immediate) */
   PULL_UNIFORM_CONSTANT_SRC_BINDING_TYPE,
   /**
    * Where to find the surface state.  Depends on BINDING_TYPE above:
    *
    * - SS: pointer to surface state (relative to surface base address)
    * - BSS: pointer to surface state (relative to bindless surface base)
    * - BTI: binding table index
    */
   PULL_UNIFORM_CONSTANT_SRC_BINDING,
   /** Surface offset */
   PULL_UNIFORM_CONSTANT_SRC_OFFSET,
   /** Pull size */
   PULL_UNIFORM_CONSTANT_SRC_SIZE,

   PULL_UNIFORM_CONSTANT_SRCS,
};

enum pull_varying_constant_srcs {
   /** enum lsc_addr_surface_type (as UD immediate) */
   PULL_VARYING_CONSTANT_SRC_BINDING_TYPE,
   /**
    * Where to find the surface state.  Depends on BINDING_TYPE above:
    *
    * - SS: pointer to surface state (relative to surface base address)
    * - BSS: pointer to surface state (relative to bindless surface base)
    * - BTI: binding table index
    */
   PULL_VARYING_CONSTANT_SRC_BINDING,
   /** Surface offset */
   PULL_VARYING_CONSTANT_SRC_OFFSET,
   /** Pull alignment */
   PULL_VARYING_CONSTANT_SRC_ALIGNMENT,

   PULL_VARYING_CONSTANT_SRCS,
};

enum ENUM_PACKED memory_logical_mode {
   MEMORY_MODE_TYPED,
   MEMORY_MODE_UNTYPED,
   MEMORY_MODE_SHARED_LOCAL,
   MEMORY_MODE_SCRATCH,
   MEMORY_MODE_CONSTANT,
};

enum memory_logical_srcs {
   /**
    * Where to find the surface state.  Depends on brw_mem_inst::binding_type:
    *
    * - SS: pointer to surface state (relative to surface base address)
    * - BSS: pointer to surface state (relative to bindless surface base)
    * - BTI: binding table index
    * - FLAT: This should should be BAD_FILE
    */
   MEMORY_LOGICAL_BINDING,

   /** Coordinate/address/offset for where to access memory */
   MEMORY_LOGICAL_ADDRESS,

   /** Data to write for stores or the first operand for atomics */
   MEMORY_LOGICAL_DATA0,

   /** Second operand for two-source atomics */
   MEMORY_LOGICAL_DATA1,

   MEMORY_LOGICAL_NUM_SRCS
};

enum memory_flags {
   /** Whether this is a transposed (i.e. block) memory access */
   MEMORY_FLAG_TRANSPOSE = 1 << 0,
   /** Whether this operation should fire for helper invocations */
   MEMORY_FLAG_INCLUDE_HELPERS = 1 << 1,
   /** Whether memory access is marked volatile by GLSL/SPIR-V. */
   MEMORY_FLAG_VOLATILE_ACCESS = 1 << 2,
   /** Whether memory access is marked coherent by GLSL/SPIR-V. */
   MEMORY_FLAG_COHERENT_ACCESS = 1 << 3,
   /** Whether this instruction should run serialized with regard to EU
    *  fusion (Gfx12.x only).
    */
   MEMORY_FLAG_FUSED_EU_DISABLE = 1 << 4,
   /** Whether this memory load can be arbitrarily reordered or CSE'd
    *  with other loads.
    */
   MEMORY_FLAG_CAN_REORDER = 1 << 5,
};

enum rt_logical_srcs {
   /** Address of the globals */
   RT_LOGICAL_SRC_GLOBALS,
   /** Trace ray payloads */
   RT_LOGICAL_SRC_PAYLOADS,
   /** Synchronous tracing (ray query) */
   RT_LOGICAL_SRC_SYNCHRONOUS,

   RT_LOGICAL_NUM_SRCS
};

enum urb_logical_srcs {
   URB_LOGICAL_SRC_HANDLE,
   /** Offset in bytes on Xe2+ or OWords on older platforms */
   URB_LOGICAL_SRC_PER_SLOT_OFFSETS,
   URB_LOGICAL_SRC_CHANNEL_MASK,
   /** Data to be written.  BAD_FILE for reads. */
   URB_LOGICAL_SRC_DATA,
   URB_LOGICAL_NUM_SRCS
};

enum interpolator_logical_srcs {
   /** Interpolation offset */
   INTERP_SRC_OFFSET,
   /** Message data  */
   INTERP_SRC_MSG_DESC,
   /** Flag register for dynamic mode */
   INTERP_SRC_DYNAMIC_MODE,
   /** Whether this should use noperspective (0/1 as UD immediate) */
   INTERP_SRC_NOPERSPECTIVE,

   INTERP_NUM_SRCS
};

enum spill_srcs {
   /** Register used for the address in scratch space. */
   SPILL_SRC_PAYLOAD1,

   /** Register to be spilled. */
   SPILL_SRC_PAYLOAD2,

   SPILL_NUM_SRCS
};

enum fill_srcs {
   /** Register used for the address in scratch space. */
   FILL_SRC_PAYLOAD1,

   FILL_NUM_SRCS
};

enum brw_reduce_op {
   BRW_REDUCE_OP_ADD,
   BRW_REDUCE_OP_MUL,
   BRW_REDUCE_OP_MIN,
   BRW_REDUCE_OP_MAX,
   BRW_REDUCE_OP_AND,
   BRW_REDUCE_OP_OR,
   BRW_REDUCE_OP_XOR,
};

enum brw_swap_direction {
   BRW_SWAP_HORIZONTAL,
   BRW_SWAP_VERTICAL,
   BRW_SWAP_DIAGONAL,
};

enum ENUM_PACKED brw_predicate {
   BRW_PREDICATE_NONE                =  0,
   BRW_PREDICATE_NORMAL              =  1,
   BRW_PREDICATE_ALIGN1_ANYV         =  2,
   BRW_PREDICATE_ALIGN1_ALLV         =  3,
   BRW_PREDICATE_ALIGN1_ANY2H        =  4,
   BRW_PREDICATE_ALIGN1_ALL2H        =  5,
   BRW_PREDICATE_ALIGN1_ANY4H        =  6,
   BRW_PREDICATE_ALIGN1_ALL4H        =  7,
   BRW_PREDICATE_ALIGN1_ANY8H        =  8,
   BRW_PREDICATE_ALIGN1_ALL8H        =  9,
   BRW_PREDICATE_ALIGN1_ANY16H       = 10,
   BRW_PREDICATE_ALIGN1_ALL16H       = 11,
   BRW_PREDICATE_ALIGN1_ANY32H       = 12,
   BRW_PREDICATE_ALIGN1_ALL32H       = 13,
   BRW_PREDICATE_ALIGN16_REPLICATE_X =  2,
   BRW_PREDICATE_ALIGN16_REPLICATE_Y =  3,
   BRW_PREDICATE_ALIGN16_REPLICATE_Z =  4,
   BRW_PREDICATE_ALIGN16_REPLICATE_W =  5,
   BRW_PREDICATE_ALIGN16_ANY4H       =  6,
   BRW_PREDICATE_ALIGN16_ALL4H       =  7,
   XE2_PREDICATE_ANY = 2,
   XE2_PREDICATE_ALL = 3
};

enum ENUM_PACKED brw_reg_file {
   BAD_FILE = 0,

   ARF,
   FIXED_GRF,
   IMM,

   ADDRESS,
   VGRF,
   ATTR,
   UNIFORM, /* pushed constant delivered register */
};

/* Align1 support for 3-src instructions. Bit 35 of the instruction
 * word is "Execution Datatype" which controls whether the instruction operates
 * on float or integer types. The register arguments have fields that offer
 * more fine control their respective types.
 */
enum ENUM_PACKED brw_align1_3src_exec_type {
   BRW_ALIGN1_3SRC_EXEC_TYPE_INT   = 0,
   BRW_ALIGN1_3SRC_EXEC_TYPE_FLOAT = 1,
};

#define BRW_ARF_NULL                  0x00
#define BRW_ARF_ADDRESS               0x10
#define BRW_ARF_ACCUMULATOR           0x20
#define BRW_ARF_FLAG                  0x30
#define BRW_ARF_MASK                  0x40
#define BRW_ARF_SCALAR                0x60
#define BRW_ARF_STATE                 0x70
#define BRW_ARF_CONTROL               0x80
#define BRW_ARF_NOTIFICATION_COUNT    0x90
#define BRW_ARF_IP                    0xA0
#define BRW_ARF_TDR                   0xB0
#define BRW_ARF_TIMESTAMP             0xC0

#define BRW_THREAD_NORMAL     0
#define BRW_THREAD_ATOMIC     1
#define BRW_THREAD_SWITCH     2

/* Subregister of the address register used for particular purposes */
enum brw_address_subreg {
   BRW_ADDRESS_SUBREG_INDIRECT_DESC = 0,
   BRW_ADDRESS_SUBREG_INDIRECT_EX_DESC = 2,
   BRW_ADDRESS_SUBREG_INDIRECT_SPILL_DESC = 4,
};

enum ENUM_PACKED brw_vertical_stride {
   BRW_VERTICAL_STRIDE_0               = 0,
   BRW_VERTICAL_STRIDE_1               = 1,
   BRW_VERTICAL_STRIDE_2               = 2,
   BRW_VERTICAL_STRIDE_4               = 3,
   BRW_VERTICAL_STRIDE_8               = 4,
   BRW_VERTICAL_STRIDE_16              = 5,
   BRW_VERTICAL_STRIDE_32              = 6,
   BRW_VERTICAL_STRIDE_ONE_DIMENSIONAL = 0xF,
};

enum ENUM_PACKED brw_align1_3src_vertical_stride {
   BRW_ALIGN1_3SRC_VERTICAL_STRIDE_0 = 0,
   BRW_ALIGN1_3SRC_VERTICAL_STRIDE_1 = 1,
   BRW_ALIGN1_3SRC_VERTICAL_STRIDE_2 = 1,
   BRW_ALIGN1_3SRC_VERTICAL_STRIDE_4 = 2,
   BRW_ALIGN1_3SRC_VERTICAL_STRIDE_8 = 3,
};

enum ENUM_PACKED brw_width {
   BRW_WIDTH_1  = 0,
   BRW_WIDTH_2  = 1,
   BRW_WIDTH_4  = 2,
   BRW_WIDTH_8  = 3,
   BRW_WIDTH_16 = 4,
};


enum gfx12_sub_byte_precision {
   BRW_SUB_BYTE_PRECISION_NONE = 0,

   /** 4 bits. Signedness determined by base type */
   BRW_SUB_BYTE_PRECISION_4BIT = 1,

   /** 2 bits. Signedness determined by base type */
   BRW_SUB_BYTE_PRECISION_2BIT = 2,
};

enum gfx12_systolic_depth {
   BRW_SYSTOLIC_DEPTH_16 = 0,
   BRW_SYSTOLIC_DEPTH_2 = 1,
   BRW_SYSTOLIC_DEPTH_4 = 2,
   BRW_SYSTOLIC_DEPTH_8 = 3,
};

#ifdef __cplusplus
/**
 * Allow bitwise arithmetic of gen_sbid_mode enums.
 */
inline gen_sbid_mode
operator|(gen_sbid_mode x, gen_sbid_mode y)
{
   return gen_sbid_mode(unsigned(x) | unsigned(y));
}

inline gen_sbid_mode
operator&(gen_sbid_mode x, gen_sbid_mode y)
{
   return gen_sbid_mode(unsigned(x) & unsigned(y));
}

inline gen_sbid_mode
operator~(gen_sbid_mode x)
{
   const unsigned range = (unsigned(GEN_SBID_SET) << 1) - 1;
   return gen_sbid_mode(~unsigned(x) & range);
}

inline gen_sbid_mode &
operator|=(gen_sbid_mode &x, gen_sbid_mode y)
{
   return x = x | y;
}

inline gen_sbid_mode &
operator&=(gen_sbid_mode &x, gen_sbid_mode y)
{
   return x = x & y;
}
#endif


enum tgl_sync_function {
   TGL_SYNC_NOP = 0x0,
   TGL_SYNC_ALLRD = 0x2,
   TGL_SYNC_ALLWR = 0x3,
   TGL_SYNC_FENCE = 0xd,
   TGL_SYNC_BAR = 0xe,
   TGL_SYNC_HOST = 0xf
};

#define GFX7_MESSAGE_TARGET_DP_DATA_CACHE     10

#define BRW_SAMPLER_RETURN_FORMAT_FLOAT32     0
#define BRW_SAMPLER_RETURN_FORMAT_UINT32      2
#define BRW_SAMPLER_RETURN_FORMAT_SINT32      3

#define GFX8_SAMPLER_RETURN_FORMAT_32BITS    0
#define GFX8_SAMPLER_RETURN_FORMAT_16BITS    1

#define BRW_DATAPORT_OWORD_BLOCK_OWORDS(n)              \
   ((n) == 1 ? GEN_DATAPORT_OWORD_BLOCK_1_OWORDLOW :    \
    (n) == 2 ? GEN_DATAPORT_OWORD_BLOCK_2_OWORDS :      \
    (n) == 4 ? GEN_DATAPORT_OWORD_BLOCK_4_OWORDS :      \
    (n) == 8 ? GEN_DATAPORT_OWORD_BLOCK_8_OWORDS :      \
    (n) == 16 ? GEN_GFX12_DATAPORT_OWORD_BLOCK_16_OWORDS :  \
    (abort(), ~0))
#define BRW_DATAPORT_OWORD_BLOCK_DWORDS(n)              \
   ((n) == 4 ? GEN_DATAPORT_OWORD_BLOCK_1_OWORDLOW :    \
    (n) == 8 ? GEN_DATAPORT_OWORD_BLOCK_2_OWORDS :      \
    (n) == 16 ? GEN_DATAPORT_OWORD_BLOCK_4_OWORDS :     \
    (n) == 32 ? GEN_DATAPORT_OWORD_BLOCK_8_OWORDS :     \
    (abort(), ~0))

#define BRW_DATAPORT_OWORD_DUAL_BLOCK_1OWORD     0
#define BRW_DATAPORT_OWORD_DUAL_BLOCK_4OWORDS    2

#define BRW_DATAPORT_DWORD_SCATTERED_BLOCK_8DWORDS   2
#define BRW_DATAPORT_DWORD_SCATTERED_BLOCK_16DWORDS  3

/* This one stays the same across generations. */
#define BRW_DATAPORT_READ_MESSAGE_OWORD_BLOCK_READ          0
/* GFX6 */
#define GFX6_DATAPORT_READ_MESSAGE_RENDER_UNORM_READ	    1
#define GFX6_DATAPORT_READ_MESSAGE_OWORD_DUAL_BLOCK_READ     2
#define GFX6_DATAPORT_READ_MESSAGE_MEDIA_BLOCK_READ          4
#define GFX6_DATAPORT_READ_MESSAGE_OWORD_UNALIGN_BLOCK_READ  5
#define GFX6_DATAPORT_READ_MESSAGE_DWORD_SCATTERED_READ      6

#define BRW_DATAPORT_RENDER_TARGET_WRITE_SIMD16_SINGLE_SOURCE                0
#define BRW_DATAPORT_RENDER_TARGET_WRITE_SIMD16_SINGLE_SOURCE_REPLICATED     1
#define BRW_DATAPORT_RENDER_TARGET_WRITE_SIMD8_DUAL_SOURCE_SUBSPAN01         2
#define BRW_DATAPORT_RENDER_TARGET_WRITE_SIMD8_DUAL_SOURCE_SUBSPAN23         3
#define BRW_DATAPORT_RENDER_TARGET_WRITE_SIMD8_SINGLE_SOURCE_SUBSPAN01       4

#define XE2_DATAPORT_RENDER_TARGET_WRITE_SIMD32_SINGLE_SOURCE                1
#define XE2_DATAPORT_RENDER_TARGET_WRITE_SIMD16_DUAL_SOURCE                  2

/* GFX6 */
#define GFX6_DATAPORT_WRITE_MESSAGE_DWORD_ATOMIC_WRITE              7
#define GFX6_DATAPORT_WRITE_MESSAGE_OWORD_BLOCK_WRITE               8
#define GFX6_DATAPORT_WRITE_MESSAGE_OWORD_DUAL_BLOCK_WRITE          9
#define GFX6_DATAPORT_WRITE_MESSAGE_MEDIA_BLOCK_WRITE               10
#define GFX6_DATAPORT_WRITE_MESSAGE_DWORD_SCATTERED_WRITE           11
#define GFX6_DATAPORT_WRITE_MESSAGE_STREAMED_VB_WRITE               13
#define GFX6_DATAPORT_WRITE_MESSAGE_RENDER_TARGET_UNORM_WRITE       14

/* GFX7 */
#define GFX7_DATAPORT_RC_MEDIA_BLOCK_READ                           4
#define GFX7_DATAPORT_RC_TYPED_SURFACE_READ                         5
#define GFX7_DATAPORT_RC_TYPED_ATOMIC_OP                            6
#define GFX7_DATAPORT_RC_MEMORY_FENCE                               7
#define GFX7_DATAPORT_RC_MEDIA_BLOCK_WRITE                          10
#define GFX7_DATAPORT_RC_TYPED_SURFACE_WRITE                        13

#define GFX7_DATAPORT_SCRATCH_READ                            ((1 << 18) | \
                                                               (0 << 17))
#define GFX7_DATAPORT_SCRATCH_WRITE                           ((1 << 18) | \
                                                               (1 << 17))
#define GFX7_DATAPORT_SCRATCH_NUM_REGS_SHIFT                        12


/* HSW */
#define HSW_DATAPORT_DC_PORT0_OWORD_BLOCK_READ                      0
#define HSW_DATAPORT_DC_PORT0_UNALIGNED_OWORD_BLOCK_READ            1
#define HSW_DATAPORT_DC_PORT0_OWORD_DUAL_BLOCK_READ                 2
#define HSW_DATAPORT_DC_PORT0_DWORD_SCATTERED_READ                  3
#define HSW_DATAPORT_DC_PORT0_BYTE_SCATTERED_READ                   4
#define HSW_DATAPORT_DC_PORT0_MEMORY_FENCE                          7
#define HSW_DATAPORT_DC_PORT0_OWORD_BLOCK_WRITE                     8
#define HSW_DATAPORT_DC_PORT0_OWORD_DUAL_BLOCK_WRITE                10
#define HSW_DATAPORT_DC_PORT0_DWORD_SCATTERED_WRITE                 11
#define HSW_DATAPORT_DC_PORT0_BYTE_SCATTERED_WRITE                  12

/* GFX9 */

/* A64 scattered message subtype */
#define GFX8_A64_SCATTERED_SUBTYPE_BYTE                             0
#define GFX8_A64_SCATTERED_SUBTYPE_DWORD                            1
#define GFX8_A64_SCATTERED_SUBTYPE_QWORD                            2
#define GFX8_A64_SCATTERED_SUBTYPE_HWORD                            3


#define BRW_URB_SWIZZLE_NONE          0
#define BRW_URB_SWIZZLE_INTERLEAVE    1
#define BRW_URB_SWIZZLE_TRANSPOSE     2



/* Gfx7 "GS URB Entry Allocation Size" is a U9-1 field, so the maximum gs_size
 * is 2^9, or 512.  It's counted in multiples of 64 bytes.
 *
 * Identical for VS, DS, and HS.
 */
#define GFX7_MAX_GS_URB_ENTRY_SIZE_BYTES                (512*64)
#define GFX7_MAX_DS_URB_ENTRY_SIZE_BYTES                (512*64)
#define GFX7_MAX_HS_URB_ENTRY_SIZE_BYTES                (512*64)
#define GFX7_MAX_VS_URB_ENTRY_SIZE_BYTES                (512*64)

/* GS Thread Payload
 */

/* 3DSTATE_GS "Output Vertex Size" has an effective maximum of 62. It's
 * counted in multiples of 16 bytes.
 */
#define GFX7_MAX_GS_OUTPUT_VERTEX_SIZE_BYTES            (62*16)


/* CR0.0[5:4] Floating-Point Rounding Modes
 *  Skylake PRM, Volume 7 Part 1, "Control Register", page 756
 */

#define BRW_CR0_RND_MODE_MASK     0x30
#define BRW_CR0_RND_MODE_SHIFT    4

enum ENUM_PACKED brw_rnd_mode {
   BRW_RND_MODE_RTNE = 0,  /* Round to Nearest or Even */
   BRW_RND_MODE_RU = 1,    /* Round Up, toward +inf */
   BRW_RND_MODE_RD = 2,    /* Round Down, toward -inf */
   BRW_RND_MODE_RTZ = 3,   /* Round Toward Zero */
   BRW_RND_MODE_UNSPECIFIED,  /* Unspecified rounding mode */
};

#define BRW_CR0_FP64_DENORM_PRESERVE (1 << 6)
#define BRW_CR0_FP32_DENORM_PRESERVE (1 << 7)
#define BRW_CR0_FP16_DENORM_PRESERVE (1 << 10)

#define BRW_CR0_FP_MODE_MASK (BRW_CR0_FP64_DENORM_PRESERVE | \
                              BRW_CR0_FP32_DENORM_PRESERVE | \
                              BRW_CR0_FP16_DENORM_PRESERVE | \
                              BRW_CR0_RND_MODE_MASK)

/* MDC_DS - Data Size Message Descriptor Control Field
 * Skylake PRM, Volume 2d, page 129
 *
 * Specifies the number of Bytes to be read or written per Dword used at
 * byte_scattered read/write and byte_scaled read/write messages.
 */
#define GFX7_BYTE_SCATTERED_DATA_ELEMENT_BYTE     0
#define GFX7_BYTE_SCATTERED_DATA_ELEMENT_WORD     1
#define GFX7_BYTE_SCATTERED_DATA_ELEMENT_DWORD    2

