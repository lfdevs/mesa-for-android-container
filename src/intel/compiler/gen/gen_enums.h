/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "util/macros.h"
#include "gen_opcodes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ENUM_PACKED gen_file {
   GEN_BAD_FILE,
   GEN_GRF,
   GEN_ARF,
   GEN_IMM,
} gen_file;

/**
 * Enum for register/value types.
 *
 * Bits 1:0 is the size of the type as a U2 'n' where size = 8 * 2^n.
 * Bits 3:4 is set to identify base type: unsigned integer, signed integer,
 * regular floating point and bfloat.
 * Bit 5 is set for vector immediates.
 */
typedef enum PACKED gen_reg_type {
   /* Unsigned integer types: 8, 16, 32, and 64-bit. */
   GEN_TYPE_UB = 0b00000,
   GEN_TYPE_UW = 0b00001,
   GEN_TYPE_UD = 0b00010,
   GEN_TYPE_UQ = 0b00011,

   /* Signed integer types: 8, 16, 32, and 64-bit. */
   GEN_TYPE_B  = 0b00100,
   GEN_TYPE_W  = 0b00101,
   GEN_TYPE_D  = 0b00110,
   GEN_TYPE_Q  = 0b00111,

   /** Floating point types: 8, 16 (half), 32, and 64-bit (double). */
   GEN_TYPE_HF8 = 0b01000,
   GEN_TYPE_HF  = 0b01001,
   GEN_TYPE_F   = 0b01010,
   GEN_TYPE_DF  = 0b01011,

   /** Floating point types (bfloat variants): 8 and 16-bit. */
   GEN_TYPE_BF8 = 0b01100,
   GEN_TYPE_BF  = 0b01101,

   /** Vector immediate types. */
   GEN_TYPE_UV = 0b10001,
   GEN_TYPE_V  = 0b10101,
   GEN_TYPE_VF = 0b11010,

   GEN_TYPE_SIZE_MASK   = 0b00011, /* type is (8 << x) bits */
   GEN_TYPE_BASE_MASK   = 0b01100, /* base types expressed in these bits */
   GEN_TYPE_BASE_UINT   = 0b00000, /* unsigned types have no base bits set */
   GEN_TYPE_BASE_SINT   = 0b00100, /* type has a signed integer base type */
   GEN_TYPE_BASE_FLOAT  = 0b01000, /* type has a floating point base type */
   GEN_TYPE_BASE_BFLOAT = 0b01100, /* type has a floating point (bfloat variant) base type */
   GEN_TYPE_VECTOR      = 0b10000, /* type is a vector immediate */

   GEN_TYPE_INVALID    = 0b11111,
   GEN_TYPE_LAST       = GEN_TYPE_INVALID,
} gen_reg_type;

typedef enum ENUM_PACKED gen_condition {
   GEN_CONDITION_NONE = 0,
   GEN_CONDITION_ZE   = 1,
   GEN_CONDITION_NZ   = 2,
   GEN_CONDITION_EQ   = GEN_CONDITION_ZE,
   GEN_CONDITION_NE   = GEN_CONDITION_NZ,
   GEN_CONDITION_GT   = 3,
   GEN_CONDITION_GE   = 4,
   GEN_CONDITION_LT   = 5,
   GEN_CONDITION_LE   = 6,
   /* 7 is reserved. */
   GEN_CONDITION_OV   = 8,
   GEN_CONDITION_UN   = 9,
} gen_condition;

typedef enum ENUM_PACKED gen_predicate {
   GEN_PREDICATE_NONE    = 0,
   GEN_PREDICATE_NORMAL  = 1,

   GEN_PREDICATE_ANYV    = 2,
   GEN_PREDICATE_ALLV    = 3,
   GEN_PREDICATE_ANY2H   = 4,
   GEN_PREDICATE_ALL2H   = 5,
   GEN_PREDICATE_ANY4H   = 6,
   GEN_PREDICATE_ALL4H   = 7,
   GEN_PREDICATE_ANY8H   = 8,
   GEN_PREDICATE_ALL8H   = 9,
   GEN_PREDICATE_ANY16H  = 10,
   GEN_PREDICATE_ALL16H  = 11,
   GEN_PREDICATE_ANY32H  = 12,
   GEN_PREDICATE_ALL32H  = 13,

   GEN_PREDICATE_XE2_ANY = 2,
   GEN_PREDICATE_XE2_ALL = 3,

   /* Pre-Xe only. */
   GEN_PREDICATE_A16_REPLICATE_X = 2,
   GEN_PREDICATE_A16_REPLICATE_Y = 3,
   GEN_PREDICATE_A16_REPLICATE_Z = 4,
   GEN_PREDICATE_A16_REPLICATE_W = 5,
   GEN_PREDICATE_A16_ANY4H       = 6,
   GEN_PREDICATE_A16_ALL4H       = 7,
} gen_predicate;

typedef enum ENUM_PACKED gen_math {
   /* 0 is reserved. */
   GEN_MATH_INV               = 1,
   GEN_MATH_LOG               = 2,
   GEN_MATH_EXP               = 3,
   GEN_MATH_SQRT              = 4,
   GEN_MATH_RSQ               = 5,
   GEN_MATH_SIN               = 6,
   GEN_MATH_COS               = 7,
   /* 8 is reserved. */
   GEN_MATH_FDIV              = 9,
   GEN_MATH_POW               = 10,
   GEN_MATH_INT_DIV_BOTH      = 11,
   GEN_MATH_INT_DIV_QUOTIENT  = 12,
   GEN_MATH_INT_DIV_REMAINDER = 13,
   GEN_MATH_INVM              = 14,
   GEN_MATH_RSQRTM            = 15,
} gen_math;

typedef enum ENUM_PACKED gen_sync_func {
   GEN_SYNC_NOP   = 0x0,
   GEN_SYNC_ALLRD = 0x2,
   GEN_SYNC_ALLWR = 0x3,
   GEN_SYNC_FENCE = 0xd,
   GEN_SYNC_BAR   = 0xe,
   GEN_SYNC_HOST  = 0xf,
} gen_sync_func;

enum {
   GEN_ARF_NULL               = 0x00,
   GEN_ARF_ADDRESS            = 0x10,
   GEN_ARF_ACCUMULATOR        = 0x20,
   GEN_ARF_FLAG               = 0x30,
   GEN_ARF_MASK               = 0x40,
   GEN_ARF_SCALAR             = 0x60,
   GEN_ARF_STATE              = 0x70,
   GEN_ARF_CONTROL            = 0x80,
   GEN_ARF_NOTIFICATION_COUNT = 0x90,
   GEN_ARF_IP                 = 0xA0,
   GEN_ARF_TDR                = 0xB0,
   GEN_ARF_TIMESTAMP          = 0xC0,
};

enum {
   GEN_VSTRIDE_ONE_DIMENSIONAL = 0xFF,
};

enum {
   GEN_THREAD_NORMAL = 0,
   GEN_THREAD_ATOMIC = 1,
   GEN_THREAD_SWITCH = 2,
};

/**
 * Xe SWSB RegDist synchronization pipeline.
 *
 * On Xe all instructions that use the RegDist synchronization mechanism are
 * considered to be executed as a single in-order pipeline, therefore only the
 * XE_PIPE_FLOAT pipeline is applicable.  On XeHP+ platforms there are two
 * additional asynchronous ALU pipelines (which still execute instructions
 * in-order and use the RegDist synchronization mechanism).  XE_PIPE_NONE
 * doesn't provide any RegDist pipeline synchronization information and allows
 * the hardware to infer the pipeline based on the source types of the
 * instruction.  XE_PIPE_ALL can be used when synchronization with all ALU
 * pipelines is intended.
 *
 * Xe3 adds XE_PIPE_SCALAR for a very specific use case (writing immediates
 * to scalar register).
 */
typedef enum gen_pipe {
   GEN_PIPE_NONE = 0,
   GEN_PIPE_FLOAT,
   GEN_PIPE_INT,
   GEN_PIPE_LONG,
   GEN_PIPE_MATH,
   GEN_PIPE_SCALAR,
   GEN_PIPE_ALL
} gen_pipe;

/**
 * Xe SWSB SBID synchronization mode.
 *
 * This is represented as a bitmask including any required SBID token
 * synchronization modes, used to synchronize out-of-order instructions.  Only
 * the strongest mode of the mask will be provided to the hardware in the SWSB
 * field of an actual hardware instruction.
 */
typedef enum gen_sbid_mode {
   GEN_SBID_NULL = 0,
   GEN_SBID_SRC  = 1,
   GEN_SBID_DST  = 2,
   GEN_SBID_SET  = 4,
} gen_sbid_mode;

/**
 * Shared Function ID - which unit a SEND message targets.
 *
 * See the Tigerlake and Alchemist PRMs, Volume 2b: Command Reference:
 * Enumerations, in the table under "SFID":
 */
typedef enum ENUM_PACKED gen_sfid {
   GEN_SFID_NULL                     = 0,
   GEN_SFID_SAMPLER                  = 2,
   GEN_SFID_MESSAGE_GATEWAY          = 3,
   GEN_SFID_HDC2                     = 4,  /* Legacy Data Port 2 */
   GEN_SFID_RENDER_CACHE             = 5,
   GEN_SFID_URB                      = 6,
   GEN_SFID_THREAD_SPAWNER           = 7,  /* Gfx12.0 and earlier only */
   GEN_SFID_BINDLESS_THREAD_DISPATCH = 7,
   GEN_SFID_RAY_TRACE_ACCELERATOR    = 8,
   GEN_SFID_HDC_READ_ONLY            = 9,  /* Read Only/Constant Data Cache */
   GEN_SFID_HDC0                     = 10, /* Legacy Data Port 0 */
   GEN_SFID_PIXEL_INTERPOLATOR       = 11,
   GEN_SFID_HDC1                     = 12, /* Legacy Data Port 1 */

   GEN_SFID_TGM                      = 13, /* LSC: Typed Global Memory */
   GEN_SFID_SLM                      = 14, /* LSC: Shared Local Memory */
   GEN_SFID_UGM                      = 15, /* LSC: Untyped Global Memory */
} gen_sfid;

/* Starting with Xe-HPG, the old dataport was massively reworked.
 * The new thing, called Load/Store Cache or LSC, has a significantly improved
 * interface.  Instead of bespoke messages for every case, there's basically
 * one or two messages with different bits to control things like address
 * size, how much data is read/written, etc.
 */
enum ENUM_PACKED lsc_opcode {
   LSC_OP_LOAD            = 0,
   LSC_OP_LOAD_CMASK      = 2,
   LSC_OP_STORE           = 4,
   LSC_OP_STORE_CMASK     = 6,
   LSC_OP_ATOMIC_INC      = 8,
   LSC_OP_ATOMIC_DEC      = 9,
   LSC_OP_ATOMIC_LOAD     = 10,
   LSC_OP_ATOMIC_STORE    = 11,
   LSC_OP_ATOMIC_ADD      = 12,
   LSC_OP_ATOMIC_SUB      = 13,
   LSC_OP_ATOMIC_MIN      = 14,
   LSC_OP_ATOMIC_MAX      = 15,
   LSC_OP_ATOMIC_UMIN     = 16,
   LSC_OP_ATOMIC_UMAX     = 17,
   LSC_OP_ATOMIC_CMPXCHG  = 18,
   LSC_OP_ATOMIC_FADD     = 19,
   LSC_OP_ATOMIC_FSUB     = 20,
   LSC_OP_ATOMIC_FMIN     = 21,
   LSC_OP_ATOMIC_FMAX     = 22,
   LSC_OP_ATOMIC_FCMPXCHG = 23,
   LSC_OP_ATOMIC_AND      = 24,
   LSC_OP_ATOMIC_OR       = 25,
   LSC_OP_ATOMIC_XOR      = 26,
   LSC_OP_FENCE           = 31,
   LSC_OP_LOAD_CMASK_MSRT     = 49,
   LSC_OP_STORE_CMASK_MSRT    = 50
};

/*
 * Specifies the size of the dataport address payload in registers.
 */
enum ENUM_PACKED lsc_addr_reg_size {
   LSC_ADDR_REG_SIZE_1  = 1,
   LSC_ADDR_REG_SIZE_2  = 2,
   LSC_ADDR_REG_SIZE_3  = 3,
   LSC_ADDR_REG_SIZE_4  = 4,
   LSC_ADDR_REG_SIZE_6  = 6,
   LSC_ADDR_REG_SIZE_8  = 8,
};

/*
 * Specifies the size of the address payload item in a dataport message.
 */
enum ENUM_PACKED lsc_addr_size {
  LSC_ADDR_SIZE_A16 = 1,    /* 16-bit address offset */
  LSC_ADDR_SIZE_A32 = 2,    /* 32-bit address offset */
  LSC_ADDR_SIZE_A64 = 3,    /* 64-bit address offset */
};

/*
 * Specifies the type of the address payload item in a dataport message. The
 * address type specifies how the dataport message decodes the Extended
 * Descriptor for the surface attributes and address calculation.
 */
enum ENUM_PACKED lsc_addr_surface_type {
   LSC_ADDR_SURFTYPE_FLAT = 0, /* Flat */
   LSC_ADDR_SURFTYPE_BSS = 1,  /* Bindless surface state */
   LSC_ADDR_SURFTYPE_SS = 2,   /* Surface state */
   LSC_ADDR_SURFTYPE_BTI = 3,  /* Binding table index */
};

/*
 * Specifies the dataport message override to the default L1 and L3 memory
 * cache policies. Dataport L1 cache policies are uncached (UC), cached (C),
 * cache streaming (S) and invalidate-after-read (IAR). Dataport L3 cache
 * policies are uncached (UC) and cached (C).
 */
enum lsc_cache_load {
   /* No override. Use the non-pipelined state or surface state cache settings
    * for L1 and L3.
    */
   LSC_CACHE_LOAD_L1STATE_L3MOCS = 0,
   /* Override to L1 uncached and L3 uncached */
   LSC_CACHE_LOAD_L1UC_L3UC      = 1,
   /* Override to L1 uncached and L3 cached */
   LSC_CACHE_LOAD_L1UC_L3C       = 2,
   /* Override to L1 cached and L3 uncached */
   LSC_CACHE_LOAD_L1C_L3UC       = 3,
   /* Override to cache at both L1 and L3 */
   LSC_CACHE_LOAD_L1C_L3C        = 4,
   /* Override to L1 streaming load and L3 uncached */
   LSC_CACHE_LOAD_L1S_L3UC       = 5,
   /* Override to L1 streaming load and L3 cached */
   LSC_CACHE_LOAD_L1S_L3C        = 6,
   /* For load messages, override to L1 invalidate-after-read, and L3 cached. */
   LSC_CACHE_LOAD_L1IAR_L3C      = 7,
};

/*
 * Specifies the dataport message override to the default L1 and L3 memory
 * cache policies. Dataport L1 cache policies are uncached (UC), cached (C),
 * streaming (S) and invalidate-after-read (IAR). Dataport L3 cache policies
 * are uncached (UC), cached (C), cached-as-a-constand (CC) and
 * invalidate-after-read (IAR).
 */
enum PACKED xe2_lsc_cache_load {
   /* No override. Use the non-pipelined or surface state cache settings for L1
    * and L3.
    */
   XE2_LSC_CACHE_LOAD_L1STATE_L3MOCS = 0,
   /* Override to L1 uncached and L3 uncached */
   XE2_LSC_CACHE_LOAD_L1UC_L3UC = 2,
   /* Override to L1 uncached and L3 cached */
   XE2_LSC_CACHE_LOAD_L1UC_L3C = 4,
   /* Override to L1 uncached and L3 cached as a constant */
   XE2_LSC_CACHE_LOAD_L1UC_L3CC = 5,
   /* Override to L1 cached and L3 uncached */
   XE2_LSC_CACHE_LOAD_L1C_L3UC = 6,
   /* Override to L1 cached and L3 cached */
   XE2_LSC_CACHE_LOAD_L1C_L3C = 8,
   /* Override to L1 cached and L3 cached as a constant */
   XE2_LSC_CACHE_LOAD_L1C_L3CC = 9,
   /* Override to L1 cached as streaming load and L3 uncached */
   XE2_LSC_CACHE_LOAD_L1S_L3UC = 10,
   /* Override to L1 cached as streaming load and L3 cached */
   XE2_LSC_CACHE_LOAD_L1S_L3C = 12,
   /* Override to L1 and L3 invalidate after read */
   XE2_LSC_CACHE_LOAD_L1IAR_L3IAR = 14,

};

/*
 * Specifies the dataport message override to the default L1 and L3 memory
 * cache policies. Dataport L1 cache policies are uncached (UC), write-through
 * (WT), write-back (WB) and streaming (S). Dataport L3 cache policies are
 * uncached (UC) and cached (WB).
 */
enum ENUM_PACKED lsc_cache_store {
   /* No override. Use the non-pipelined or surface state cache settings for L1
    * and L3.
    */
   LSC_CACHE_STORE_L1STATE_L3MOCS = 0,
   /* Override to L1 uncached and L3 uncached */
   LSC_CACHE_STORE_L1UC_L3UC = 1,
   /* Override to L1 uncached and L3 cached */
   LSC_CACHE_STORE_L1UC_L3WB = 2,
   /* Override to L1 write-through and L3 uncached */
   LSC_CACHE_STORE_L1WT_L3UC = 3,
   /* Override to L1 write-through and L3 cached */
   LSC_CACHE_STORE_L1WT_L3WB = 4,
   /* Override to L1 streaming and L3 uncached */
   LSC_CACHE_STORE_L1S_L3UC = 5,
   /* Override to L1 streaming and L3 cached */
   LSC_CACHE_STORE_L1S_L3WB = 6,
   /* Override to L1 write-back, and L3 cached */
   LSC_CACHE_STORE_L1WB_L3WB = 7,

};

/*
 * Specifies the dataport message override to the default L1 and L3 memory
 * cache policies. Dataport L1 cache policies are uncached (UC), write-through
 * (WT), write-back (WB) and streaming (S). Dataport L3 cache policies are
 * uncached (UC) and cached (WB).
 */
enum PACKED xe2_lsc_cache_store {
   /* No override. Use the non-pipelined or surface state cache settings for L1
    * and L3.
    */
   XE2_LSC_CACHE_STORE_L1STATE_L3MOCS = 0,
   /* Override to L1 uncached and L3 uncached */
   XE2_LSC_CACHE_STORE_L1UC_L3UC = 2,
   /* Override to L1 uncached and L3 cached */
   XE2_LSC_CACHE_STORE_L1UC_L3WB = 4,
   /* From BSpec: 71167 for L1WT_L3UC and L1WT_L3WB:
    * "L1 will be uncached rather than write-through."
    */
   /* Override to L1 write-through and L3 uncached */
   XE2_LSC_CACHE_STORE_L1WT_L3UC = 6,
   /* Override to L1 write-through and L3 cached */
   XE2_LSC_CACHE_STORE_L1WT_L3WB = 8,
   /* Override to L1 streaming and L3 uncached */
   XE2_LSC_CACHE_STORE_L1S_L3UC = 10,
   /* Override to L1 streaming and L3 cached */
   XE2_LSC_CACHE_STORE_L1S_L3WB = 12,
   /* Override to L1 write-back and L3 cached */
   XE2_LSC_CACHE_STORE_L1WB_L3WB = 14,

};

#define LSC_CACHE(devinfo, l_or_s, cc)                                  \
   ((devinfo)->ver < 20 ? (unsigned)LSC_CACHE_ ## l_or_s ## _ ## cc :   \
                          (unsigned)XE2_LSC_CACHE_ ## l_or_s ## _ ## cc)

/*
 * Specifies which components of the data payload 4-element vector (X,Y,Z,W) is
 * packed into the register payload.
 */
enum ENUM_PACKED lsc_cmask {
   LSC_CMASK_X = 0x1,
   LSC_CMASK_Y = 0x2,
   LSC_CMASK_XY = 0x3,
   LSC_CMASK_Z = 0x4,
   LSC_CMASK_XZ = 0x5,
   LSC_CMASK_YZ = 0x6,
   LSC_CMASK_XYZ = 0x7,
   LSC_CMASK_W = 0x8,
   LSC_CMASK_XW = 0x9,
   LSC_CMASK_YW = 0xa,
   LSC_CMASK_XYW = 0xb,
   LSC_CMASK_ZW = 0xc,
   LSC_CMASK_XZW = 0xd,
   LSC_CMASK_YZW = 0xe,
   LSC_CMASK_XYZW = 0xf,
};

/*
 * Specifies the size of the data payload item in a dataport message.
 */
enum ENUM_PACKED lsc_data_size {
   /* 8-bit scalar data value in memory, packed into a 8-bit data value in
    * register.
    */
   LSC_DATA_SIZE_D8 = 0,
   /* 16-bit scalar data value in memory, packed into a 16-bit data value in
    * register.
    */
   LSC_DATA_SIZE_D16 = 1,
   /* 32-bit scalar data value in memory, packed into 32-bit data value in
    * register.
    */
   LSC_DATA_SIZE_D32 = 2,
   /* 64-bit scalar data value in memory, packed into 64-bit data value in
    * register.
    */
   LSC_DATA_SIZE_D64 = 3,
   /* 8-bit scalar data value in memory, packed into 32-bit unsigned data value
    * in register.
    */
   LSC_DATA_SIZE_D8U32 = 4,
   /* 16-bit scalar data value in memory, packed into 32-bit unsigned data
    * value in register.
    */
   LSC_DATA_SIZE_D16U32 = 5,
   /* 16-bit scalar BigFloat data value in memory, packed into 32-bit float
    * value in register.
    */
   LSC_DATA_SIZE_D16BF32 = 6,
};

/*
 *  Enum specifies the scope of the fence.
 */
enum ENUM_PACKED lsc_fence_scope {
   /* Wait until all previous memory transactions from this thread are observed
    * within the local thread-group.
    */
   LSC_FENCE_THREADGROUP = 0,
   /* Wait until all previous memory transactions from this thread are observed
    * within the local sub-slice.
    */
   LSC_FENCE_LOCAL = 1,
   /* Wait until all previous memory transactions from this thread are observed
    * in the local tile.
    */
   LSC_FENCE_TILE = 2,
   /* Wait until all previous memory transactions from this thread are observed
    * in the local GPU.
    */
   LSC_FENCE_GPU = 3,
   /* Wait until all previous memory transactions from this thread are observed
    * across all GPUs in the system.
    */
   LSC_FENCE_ALL_GPU = 4,
   /* Wait until all previous memory transactions from this thread are observed
    * at the "system" level.
    */
   LSC_FENCE_SYSTEM_RELEASE = 5,
   /* For GPUs that do not follow PCIe Write ordering for downstream writes
    * targeting device memory, a fence message with scope=System_Acquire will
    * commit to device memory all downstream and peer writes that have reached
    * the device.
    */
   LSC_FENCE_SYSTEM_ACQUIRE = 6,
};

/*
 * Specifies the type of cache flush operation to perform after a fence is
 * complete.
 */
enum ENUM_PACKED lsc_flush_type {
   LSC_FLUSH_TYPE_NONE = 0,
   /*
    * For a R/W cache, evict dirty lines (M to I state) and invalidate clean
    * lines. For a RO cache, invalidate clean lines.
    */
   LSC_FLUSH_TYPE_EVICT = 1,
   /*
    * For both R/W and RO cache, invalidate clean lines in the cache.
    */
   LSC_FLUSH_TYPE_INVALIDATE = 2,
   /*
    * For a R/W cache, invalidate dirty lines (M to I state), without
    * write-back to next level. This opcode does nothing for a RO cache.
    */
   LSC_FLUSH_TYPE_DISCARD = 3,
   /*
    * For a R/W cache, write-back dirty lines to the next level, but kept in
    * the cache as "clean" (M to V state). This opcode does nothing for a RO
    * cache.
    */
   LSC_FLUSH_TYPE_CLEAN = 4,
   /*
    * Flush "RW" section of the L3 cache, but leave L1 and L2 caches untouched.
    */
   LSC_FLUSH_TYPE_L3ONLY = 5,
   /*
    * HW maps this flush type internally to NONE.
    */
   LSC_FLUSH_TYPE_NONE_6 = 6,

};

enum ENUM_PACKED lsc_backup_fence_routing {
   /* Normal routing: UGM fence is routed to UGM pipeline. */
   LSC_NORMAL_ROUTING,
   /* Route UGM fence to LSC unit. */
   LSC_ROUTE_TO_LSC,
};

/*
 * Specifies the size of the vector in a dataport message.
 */
enum ENUM_PACKED lsc_vect_size {
   LSC_VECT_SIZE_V1 = 0,    /* vector length 1 */
   LSC_VECT_SIZE_V2 = 1,    /* vector length 2 */
   LSC_VECT_SIZE_V3 = 2,    /* Vector length 3 */
   LSC_VECT_SIZE_V4 = 3,    /* Vector length 4 */
   LSC_VECT_SIZE_V8 = 4,    /* Vector length 8 */
   LSC_VECT_SIZE_V16 = 5,   /* Vector length 16 */
   LSC_VECT_SIZE_V32 = 6,   /* Vector length 32 */
   LSC_VECT_SIZE_V64 = 7,   /* Vector length 64 */
};

#define LSC_ONE_ADDR_REG   1

/* Sampler message types (desc[16:12], plus desc[31] on Xe2+). */
enum {
   GEN_SAMPLER_MESSAGE_SAMPLE              = 0,
   GEN_SAMPLER_MESSAGE_SAMPLE_BIAS         = 1,
   GEN_SAMPLER_MESSAGE_SAMPLE_LOD          = 2,
   GEN_SAMPLER_MESSAGE_SAMPLE_COMPARE      = 3,
   GEN_SAMPLER_MESSAGE_SAMPLE_DERIVS       = 4,
   GEN_SAMPLER_MESSAGE_SAMPLE_BIAS_COMPARE = 5,
   GEN_SAMPLER_MESSAGE_SAMPLE_LOD_COMPARE  = 6,
   GEN_SAMPLER_MESSAGE_SAMPLE_LD           = 7,
   GEN_SAMPLER_MESSAGE_SAMPLE_GATHER4      = 8,
   GEN_SAMPLER_MESSAGE_LOD                 = 9,
   GEN_SAMPLER_MESSAGE_SAMPLE_RESINFO      = 10,
   GEN_SAMPLER_MESSAGE_SAMPLE_SAMPLEINFO   = 11,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_L     = 13,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_B     = 14,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_I     = 15,
   GEN_SAMPLER_MESSAGE_SAMPLE_GATHER4_C    = 16,
   GEN_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO   = 17,
   GEN_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_C = 18,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_MLOD          = 18,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_COMPARE_MLOD  = 19,
   GEN_SAMPLER_MESSAGE_SAMPLE_DERIV_COMPARE = 20,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_I_C   = 21,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_L_C   = 23,
   GEN_SAMPLER_MESSAGE_SAMPLE_LZ           = 24,
   GEN_SAMPLER_MESSAGE_SAMPLE_C_LZ         = 25,
   GEN_SAMPLER_MESSAGE_SAMPLE_LD_LZ        = 26,
   GEN_SAMPLER_MESSAGE_SAMPLE_LD2DMS_W     = 28,
   GEN_SAMPLER_MESSAGE_SAMPLE_LD_MCS       = 29,
   GEN_SAMPLER_MESSAGE_SAMPLE_LD2DMS       = 30,
   GEN_SAMPLER_MESSAGE_SAMPLE_LD2DSS       = 31,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO                     = 32,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_BIAS                = 33,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_LOD                 = 34,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_COMPARE             = 35,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_DERIVS              = 36,
   GEN_XE3_SAMPLER_MESSAGE_SAMPLE_PO_BIAS_COMPARE        = 37,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_LOD_COMPARE         = 38,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO             = 40,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_L           = 45,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_B           = 46,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_I           = 47,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_C           = 48,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_D_C                 = 52,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_I_C         = 53,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_L_C         = 55,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_LZ                  = 56,
   GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_C_LZ                = 57,
};

/* Sampler SIMD modes (desc[18:17] | desc[29] << 2). */
enum {
   GEN_SAMPLER_SIMD_MODE_SIMD8D          = 0,
   GEN_SAMPLER_SIMD_MODE_SIMD8           = 1,
   GEN_SAMPLER_SIMD_MODE_SIMD16          = 2,
   GEN_SAMPLER_SIMD_MODE_SIMD32_64       = 3,
   GEN_GFX11_SAMPLER_SIMD_MODE_SIMD8H    = 5,
   GEN_GFX11_SAMPLER_SIMD_MODE_SIMD16H   = 6,
   GEN_XE2_SAMPLER_SIMD_MODE_SIMD16      = 1,
   GEN_XE2_SAMPLER_SIMD_MODE_SIMD32      = 2,
   GEN_XE2_SAMPLER_SIMD_MODE_SIMD16H     = 5,
   GEN_XE2_SAMPLER_SIMD_MODE_SIMD32H     = 6,
};

enum {
   GEN_BTI_STATELESS_IA_COHERENT  = 255,
   GEN_BTI_SLM                    = 254,
   GEN_BTI_STATELESS_NON_COHERENT = 253,
   GEN_BTI_BINDLESS               = 252,
};

/* HDC1 (dataport DC port1) message types (desc[18:14]). */
enum {
   GEN_DATAPORT_DC_PORT1_UNTYPED_SURFACE_READ              = 0x01,
   GEN_DATAPORT_DC_PORT1_UNTYPED_ATOMIC_OP                 = 0x02,
   GEN_DATAPORT_DC_PORT1_UNTYPED_ATOMIC_OP_SIMD4X2         = 0x03,
   GEN_DATAPORT_DC_PORT1_MEDIA_BLOCK_READ                  = 0x04,
   GEN_DATAPORT_DC_PORT1_TYPED_SURFACE_READ                = 0x05,
   GEN_DATAPORT_DC_PORT1_TYPED_ATOMIC_OP                   = 0x06,
   GEN_DATAPORT_DC_PORT1_TYPED_ATOMIC_OP_SIMD4X2           = 0x07,
   GEN_DATAPORT_DC_PORT1_UNTYPED_SURFACE_WRITE             = 0x09,
   GEN_DATAPORT_DC_PORT1_MEDIA_BLOCK_WRITE                 = 0x0a,
   GEN_DATAPORT_DC_PORT1_ATOMIC_COUNTER_OP                 = 0x0b,
   GEN_DATAPORT_DC_PORT1_ATOMIC_COUNTER_OP_SIMD4X2         = 0x0c,
   GEN_DATAPORT_DC_PORT1_TYPED_SURFACE_WRITE               = 0x0d,
   GEN_DATAPORT_DC_PORT1_A64_SCATTERED_READ               = 0x10,
   GEN_DATAPORT_DC_PORT1_A64_UNTYPED_SURFACE_READ         = 0x11,
   GEN_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_OP            = 0x12,
   GEN_GFX12_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_HALF_INT_OP  = 0x13,
   GEN_DATAPORT_DC_PORT1_A64_OWORD_BLOCK_READ             = 0x14,
   GEN_DATAPORT_DC_PORT1_A64_OWORD_BLOCK_WRITE            = 0x15,
   GEN_DATAPORT_DC_PORT1_A64_UNTYPED_SURFACE_WRITE        = 0x19,
   GEN_DATAPORT_DC_PORT1_A64_SCATTERED_WRITE              = 0x1a,
   GEN_DATAPORT_DC_PORT1_UNTYPED_ATOMIC_FLOAT_OP          = 0x1b,
   GEN_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_FLOAT_OP      = 0x1d,
   GEN_GFX12_DATAPORT_DC_PORT1_A64_UNTYPED_ATOMIC_HALF_FLOAT_OP = 0x1e,
};

/* HDC0 (dataport DC) message types (desc[18:14]). */
enum {
   GEN_DATAPORT_DC_OWORD_BLOCK_READ            = 0,
   GEN_DATAPORT_DC_UNALIGNED_OWORD_BLOCK_READ  = 1,
   GEN_DATAPORT_DC_OWORD_DUAL_BLOCK_READ       = 2,
   GEN_DATAPORT_DC_DWORD_SCATTERED_READ        = 3,
   GEN_DATAPORT_DC_BYTE_SCATTERED_READ         = 4,
   GEN_DATAPORT_DC_UNTYPED_SURFACE_READ        = 5,
   GEN_DATAPORT_DC_UNTYPED_ATOMIC_OP           = 6,
   GEN_DATAPORT_DC_MEMORY_FENCE                = 7,
   GEN_DATAPORT_DC_OWORD_BLOCK_WRITE           = 8,
   GEN_DATAPORT_DC_OWORD_DUAL_BLOCK_WRITE      = 10,
   GEN_DATAPORT_DC_DWORD_SCATTERED_WRITE       = 11,
   GEN_DATAPORT_DC_BYTE_SCATTERED_WRITE        = 12,
   GEN_DATAPORT_DC_UNTYPED_SURFACE_WRITE       = 13,
};

/* HDC1 surface SIMD modes (msg_ctrl[5:4] / desc[13:12] on
 * typed/untyped surface messages).
 */
enum {
   GEN_HDC1_SURFACE_SIMD_MODE_SIMD4X2 = 0,
   GEN_HDC1_SURFACE_SIMD_MODE_SIMD16  = 1,
   GEN_HDC1_SURFACE_SIMD_MODE_SIMD8   = 2,
};

/* Render-cache message types (desc[17:14]). */
enum {
   GEN_DATAPORT_WRITE_MESSAGE_RENDER_TARGET_WRITE  = 12,
   GEN_DATAPORT_RC_RENDER_TARGET_READ              = 13,
};

/* Render-cache RT write subtypes (msg_ctrl[2:0] / desc[10:8]). */
enum {
   GEN_RT_WRITE_SUBTYPE_SIMD16             = 0,
   GEN_RT_WRITE_SUBTYPE_SIMD16_REPDATA     = 1,
   GEN_RT_WRITE_SUBTYPE_SIMD8_DUALSRC_LOW  = 2,
   GEN_RT_WRITE_SUBTYPE_SIMD8_DUALSRC_HIGH = 3,
   GEN_RT_WRITE_SUBTYPE_SIMD8              = 4,
   GEN_RT_WRITE_SUBTYPE_SIMD8_IMAGEWRITE   = 5,
   GEN_RT_WRITE_SUBTYPE_SIMD16_REPDATA_7   = 7,

   GEN_XE2_RT_WRITE_SUBTYPE_SIMD16         = 0,
   GEN_XE2_RT_WRITE_SUBTYPE_SIMD32         = 1,
   GEN_XE2_RT_WRITE_SUBTYPE_SIMD16_DUALSRC = 2,
};

/* Atomic op codes (msg_ctrl[3:0] of HDC untyped_atomic / similar). */
enum {
   GEN_AOP_AND     = 1,
   GEN_AOP_OR      = 2,
   GEN_AOP_XOR     = 3,
   GEN_AOP_MOV     = 4,
   GEN_AOP_INC     = 5,
   GEN_AOP_DEC     = 6,
   GEN_AOP_ADD     = 7,
   GEN_AOP_SUB     = 8,
   GEN_AOP_REVSUB  = 9,
   GEN_AOP_IMAX    = 10,
   GEN_AOP_IMIN    = 11,
   GEN_AOP_UMAX    = 12,
   GEN_AOP_UMIN    = 13,
   GEN_AOP_CMPWR   = 14,
   GEN_AOP_PREDEC  = 15,

   /* Float atomic op codes (msg_ctrl[3:0] of HDC untyped_atomic_float). */
   GEN_AOP_FMAX    = 1,
   GEN_AOP_FMIN    = 2,
   GEN_AOP_FCMPWR  = 3,
   GEN_AOP_FADD    = 4,
};

/* Dataport OWORD-block selectors (msg_ctrl[2:0] on oword block rw). */
enum {
   GEN_DATAPORT_OWORD_BLOCK_1_OWORDLOW   = 0,
   GEN_DATAPORT_OWORD_BLOCK_1_OWORDHIGH  = 1,
   GEN_DATAPORT_OWORD_BLOCK_2_OWORDS     = 2,
   GEN_DATAPORT_OWORD_BLOCK_4_OWORDS     = 3,
   GEN_DATAPORT_OWORD_BLOCK_8_OWORDS     = 4,
   GEN_GFX12_DATAPORT_OWORD_BLOCK_16_OWORDS  = 5,
};

/* Classic URB opcodes (desc[3:0]; pre-Xe2 only). */
enum gen_urb_opcode {
   GEN_URB_OPCODE_ATOMIC_MOV  = 4,
   GEN_URB_OPCODE_ATOMIC_INC  = 5,
   GEN_URB_OPCODE_ATOMIC_ADD  = 6,
   GEN_URB_OPCODE_SIMD8_WRITE = 7,
   GEN_URB_OPCODE_SIMD8_READ  = 8,
   GEN_GFX125_URB_OPCODE_FENCE     = 9,
};

/* Message Gateway subfuncs (desc[2:0]). */
enum {
   GEN_MESSAGE_GATEWAY_SFID_OPEN_GATEWAY         = 0,
   GEN_MESSAGE_GATEWAY_SFID_CLOSE_GATEWAY        = 1,
   GEN_MESSAGE_GATEWAY_SFID_FORWARD_MSG          = 2,
   GEN_MESSAGE_GATEWAY_SFID_GET_TIMESTAMP        = 3,
   GEN_MESSAGE_GATEWAY_SFID_BARRIER_MSG          = 4,
   GEN_MESSAGE_GATEWAY_SFID_UPDATE_GATEWAY_STATE = 5,
   GEN_MESSAGE_GATEWAY_SFID_MMIO_READ_WRITE      = 6,
};

/* Pixel-interpolator msg_type (desc[13:12]). */
enum {
   GEN_PIXEL_INTERPOLATOR_LOC_SHARED_OFFSET   = 0,
   GEN_PIXEL_INTERPOLATOR_LOC_SAMPLE          = 1,
   GEN_PIXEL_INTERPOLATOR_LOC_CENTROID        = 2,
   GEN_PIXEL_INTERPOLATOR_LOC_PER_SLOT_OFFSET = 3,
};

/* Bindless thread dispatch (gfx12.5+) message types (desc[17:14]). */
enum {
   GEN_RT_BTD_MESSAGE_SPAWN  = 1,
};

/* Ray trace accelerator trace-ray control (desc[9:8]). */
enum {
   GEN_RT_TRACE_RAY_INITIAL   = 0,
   GEN_RT_TRACE_RAY_INSTANCE  = 1,
   GEN_RT_TRACE_RAY_COMMIT    = 2,
   GEN_RT_TRACE_RAY_CONTINUE  = 3,
};

/* Bindless thread dispatch shader types. */
enum {
   GEN_RT_BTD_SHADER_TYPE_ANY_HIT        = 0,
   GEN_RT_BTD_SHADER_TYPE_CLOSEST_HIT    = 1,
   GEN_RT_BTD_SHADER_TYPE_MISS           = 2,
   GEN_RT_BTD_SHADER_TYPE_INTERSECTION   = 3,
};

#ifdef __cplusplus
} /* extern "C" */
#endif

