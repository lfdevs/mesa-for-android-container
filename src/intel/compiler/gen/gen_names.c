/*
 * Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "gen_names.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "dev/intel_device_info.h"
#include "util/macros.h"

#define LOOKUP_BY_VALUE(names, value) \
   ((value) < ARRAY_SIZE(names) ? (names)[value] : NULL)

static bool
string_matches(const char *name, const char *str, int size)
{
   return size >= 0 && strlen(name) == (size_t)size &&
          memcmp(name, str, size) == 0;
}

static int
lookup_by_name(const char *const *names, unsigned num_names,
               const char *str, int size)
{
   for (unsigned i = 0; i < num_names; i++) {
      if (names[i] && string_matches(names[i], str, size))
         return i;
   }

   return -1;
}

#define LOOKUP_BY_NAME(names, str, size)                       \
   lookup_by_name((names), ARRAY_SIZE(names), (str), (size))

#define DEFINE_TO_STRING(to_string, type, names)               \
const char *                                                   \
to_string(type value)                                          \
{                                                              \
   return LOOKUP_BY_VALUE(names, value);                       \
}

#define DEFINE_FROM_STRING(from_string, type, names)           \
type                                                           \
from_string(const char *str, int size, bool *valid)            \
{                                                              \
   assert(valid);                                              \
   const int v = LOOKUP_BY_NAME(names, str, size);             \
   *valid = v >= 0;                                            \
   return *valid ? (type)v : (type)0;                          \
}

static const char *const gen_reg_type_names[] = {
   [GEN_TYPE_UB] = "ub",
   [GEN_TYPE_B]  = "b",
   [GEN_TYPE_UW] = "uw",
   [GEN_TYPE_W]  = "w",
   [GEN_TYPE_UD] = "ud",
   [GEN_TYPE_D]  = "d",
   [GEN_TYPE_UQ] = "uq",
   [GEN_TYPE_Q]  = "q",
   [GEN_TYPE_HF] = "hf",
   [GEN_TYPE_BF] = "bf",
   [GEN_TYPE_F]  = "f",
   [GEN_TYPE_DF] = "df",
   [GEN_TYPE_V]  = "v",
   [GEN_TYPE_VF] = "vf",
   [GEN_TYPE_UV] = "uv",
};

DEFINE_TO_STRING(gen_reg_type_to_string, enum gen_reg_type, gen_reg_type_names)
DEFINE_FROM_STRING(gen_reg_type_from_string, enum gen_reg_type, gen_reg_type_names)


static const char *const gen_arf_names[] = {
   [GEN_ARF_NULL]               = "null",
   [GEN_ARF_ADDRESS]            = "a",
   [GEN_ARF_ACCUMULATOR]        = "acc",
   [GEN_ARF_FLAG]               = "f",
   [GEN_ARF_MASK]               = "mask",
   [GEN_ARF_SCALAR]             = "s",
   [GEN_ARF_STATE]              = "sr",
   [GEN_ARF_CONTROL]            = "cr",
   [GEN_ARF_NOTIFICATION_COUNT] = "n",
   [GEN_ARF_IP]                 = "ip",
   [GEN_ARF_TDR]                = "tdr",
   [GEN_ARF_TIMESTAMP]          = "tm",
};

DEFINE_TO_STRING(gen_arf_to_string, unsigned, gen_arf_names)


static const char *const gen_condition_names[] = {
   [GEN_CONDITION_NONE] = "",
   [GEN_CONDITION_EQ]   = "eq",
   [GEN_CONDITION_NE]   = "ne",
   [GEN_CONDITION_GT]   = "gt",
   [GEN_CONDITION_GE]   = "ge",
   [GEN_CONDITION_LT]   = "lt",
   [GEN_CONDITION_LE]   = "le",
   [GEN_CONDITION_OV]   = "ov",
   [GEN_CONDITION_UN]   = "un",
};

DEFINE_TO_STRING(gen_condition_to_string, enum gen_condition, gen_condition_names)
DEFINE_FROM_STRING(gen_condition_from_string, enum gen_condition, gen_condition_names)


static const char *const gen_predicate_align16_names[] = {
   [GEN_PREDICATE_NONE]             = "",
   [GEN_PREDICATE_NORMAL]           = "",
   [GEN_PREDICATE_A16_REPLICATE_X]  = "x",
   [GEN_PREDICATE_A16_REPLICATE_Y]  = "y",
   [GEN_PREDICATE_A16_REPLICATE_Z]  = "z",
   [GEN_PREDICATE_A16_REPLICATE_W]  = "w",
   [GEN_PREDICATE_A16_ANY4H]        = "any4h",
   [GEN_PREDICATE_A16_ALL4H]        = "all4h",
};

static const char *const gen_predicate_align1_names[] = {
   [GEN_PREDICATE_NONE]   = "",
   [GEN_PREDICATE_NORMAL] = "",
   [GEN_PREDICATE_ANYV]   = "anyv",
   [GEN_PREDICATE_ALLV]   = "allv",
   [GEN_PREDICATE_ANY2H]  = "any2h",
   [GEN_PREDICATE_ALL2H]  = "all2h",
   [GEN_PREDICATE_ANY4H]  = "any4h",
   [GEN_PREDICATE_ALL4H]  = "all4h",
   [GEN_PREDICATE_ANY8H]  = "any8h",
   [GEN_PREDICATE_ALL8H]  = "all8h",
   [GEN_PREDICATE_ANY16H] = "any16h",
   [GEN_PREDICATE_ALL16H] = "all16h",
   [GEN_PREDICATE_ANY32H] = "any32h",
   [GEN_PREDICATE_ALL32H] = "all32h",
};

static const char *const xe2_predicate_names[] = {
   [GEN_PREDICATE_NONE]    = "",
   [GEN_PREDICATE_NORMAL]  = "",
   [GEN_PREDICATE_XE2_ANY] = "any",
   [GEN_PREDICATE_XE2_ALL] = "all",
};

const char *
gen_predicate_to_string(const struct intel_device_info *devinfo,
                        bool align16, gen_predicate pred)
{
   if (devinfo->ver >= 20)
      return LOOKUP_BY_VALUE(xe2_predicate_names, pred);

   return align16 ? LOOKUP_BY_VALUE(gen_predicate_align16_names, pred) :
                    LOOKUP_BY_VALUE(gen_predicate_align1_names, pred);
}

gen_predicate
gen_predicate_from_string(const struct intel_device_info *devinfo,
                          bool align16, const char *str, int size,
                          bool *valid)
{
   assert(valid);

   int pred;
   if (devinfo->ver >= 20) {
      pred = LOOKUP_BY_NAME(xe2_predicate_names, str, size);
   } else {
      pred = align16 ? LOOKUP_BY_NAME(gen_predicate_align16_names, str, size) :
                       LOOKUP_BY_NAME(gen_predicate_align1_names, str, size);
      if (pred < 0 && !align16 && string_matches("any", str, size))
         pred = GEN_PREDICATE_ANYV;
      if (pred < 0 && !align16 && string_matches("all", str, size))
         pred = GEN_PREDICATE_ALLV;
   }

   *valid = pred >= 0;
   return *valid ? (gen_predicate)pred : (gen_predicate)0;
}


static const char *const gen_math_function_names[] = {
   [GEN_MATH_INV]               = "inv",
   [GEN_MATH_LOG]               = "log",
   [GEN_MATH_EXP]               = "exp",
   [GEN_MATH_SQRT]              = "sqt",
   [GEN_MATH_RSQ]               = "rsq",
   [GEN_MATH_SIN]               = "sin",
   [GEN_MATH_COS]               = "cos",
   [GEN_MATH_FDIV]              = "fdiv",
   [GEN_MATH_POW]               = "pow",
   [GEN_MATH_INT_DIV_BOTH]      = "intdiv_qr",
   [GEN_MATH_INT_DIV_QUOTIENT]  = "intdiv_q",
   [GEN_MATH_INT_DIV_REMAINDER] = "intdiv_r",
   [GEN_MATH_INVM]              = "invm",
   [GEN_MATH_RSQRTM]            = "rsqrtm",
};

DEFINE_TO_STRING(gen_math_function_to_string, gen_math, gen_math_function_names)
DEFINE_FROM_STRING(gen_math_function_from_string, gen_math, gen_math_function_names)


static const char *const gen_sync_function_names[] = {
   [GEN_SYNC_NOP] = "nop",
   [GEN_SYNC_ALLRD] = "allrd",
   [GEN_SYNC_ALLWR] = "allwr",
   [GEN_SYNC_FENCE] = "fence",
   [GEN_SYNC_BAR] = "bar",
   [GEN_SYNC_HOST] = "host",
};

DEFINE_TO_STRING(gen_sync_function_to_string, gen_sync_func, gen_sync_function_names)
DEFINE_FROM_STRING(gen_sync_function_from_string, gen_sync_func, gen_sync_function_names)


static const char *const gen_pipe_names[] = {
   [GEN_PIPE_NONE]   = "",
   [GEN_PIPE_FLOAT]  = "F",
   [GEN_PIPE_INT]    = "I",
   [GEN_PIPE_LONG]   = "L",
   [GEN_PIPE_MATH]   = "M",
   [GEN_PIPE_SCALAR] = "S",
   [GEN_PIPE_ALL]    = "A",
};

DEFINE_TO_STRING(gen_pipe_to_string, gen_pipe, gen_pipe_names)
DEFINE_FROM_STRING(gen_pipe_from_string, gen_pipe, gen_pipe_names)


static const char *const gen_sfid_names[] = {
   [GEN_SFID_NULL]                  = "null",
   [GEN_SFID_SAMPLER]               = "smpl",
   [GEN_SFID_MESSAGE_GATEWAY]       = "gtwy",
   [GEN_SFID_HDC2]                  = "hdc2",
   [GEN_SFID_RENDER_CACHE]          = "render",
   [GEN_SFID_URB]                   = "urb",
   [GEN_SFID_RAY_TRACE_ACCELERATOR] = "rtaccel",
   [GEN_SFID_HDC_READ_ONLY]         = "hdc_ro",
   [GEN_SFID_HDC0]                  = "hdc0",
   [GEN_SFID_PIXEL_INTERPOLATOR]    = "pi",
   [GEN_SFID_HDC1]                  = "hdc1",
   [GEN_SFID_SLM]                   = "slm",
   [GEN_SFID_TGM]                   = "tgm",
   [GEN_SFID_UGM]                   = "ugm",
};

const char *
gen_sfid_to_string(const struct intel_device_info *devinfo, gen_sfid sfid)
{
   if (sfid == GEN_SFID_THREAD_SPAWNER)
      return devinfo->verx10 <= 120 ? "ts" : "btd";

   return LOOKUP_BY_VALUE(gen_sfid_names, sfid);
}

gen_sfid
gen_sfid_from_string(const struct intel_device_info *devinfo,
                     const char *str, int size, bool *valid)
{
   assert(valid);

   if (string_matches("ts", str, size)) {
      *valid = devinfo->verx10 <= 120;
      return GEN_SFID_THREAD_SPAWNER;
   }
   if (string_matches("btd", str, size)) {
      *valid = devinfo->verx10 >= 125;
      return GEN_SFID_BINDLESS_THREAD_DISPATCH;
   }

   const int sfid = LOOKUP_BY_NAME(gen_sfid_names, str, size);
   *valid = sfid >= 0;
   return *valid ? (gen_sfid)sfid : (gen_sfid)0;
}


static const char *const gen_lsc_opcode_names[] = {
   [LSC_OP_LOAD]             = "load",
   [LSC_OP_LOAD_CMASK]       = "load_cmask",
   [LSC_OP_STORE]            = "store",
   [LSC_OP_STORE_CMASK]      = "store_cmask",
   [LSC_OP_ATOMIC_INC]       = "atomic_inc",
   [LSC_OP_ATOMIC_DEC]       = "atomic_dec",
   [LSC_OP_ATOMIC_LOAD]      = "atomic_load",
   [LSC_OP_ATOMIC_STORE]     = "atomic_store",
   [LSC_OP_ATOMIC_ADD]       = "atomic_add",
   [LSC_OP_ATOMIC_SUB]       = "atomic_sub",
   [LSC_OP_ATOMIC_MIN]       = "atomic_min",
   [LSC_OP_ATOMIC_MAX]       = "atomic_max",
   [LSC_OP_ATOMIC_UMIN]      = "atomic_umin",
   [LSC_OP_ATOMIC_UMAX]      = "atomic_umax",
   [LSC_OP_ATOMIC_CMPXCHG]   = "atomic_cmpxchg",
   [LSC_OP_ATOMIC_FADD]      = "atomic_fadd",
   [LSC_OP_ATOMIC_FSUB]      = "atomic_fsub",
   [LSC_OP_ATOMIC_FMIN]      = "atomic_fmin",
   [LSC_OP_ATOMIC_FMAX]      = "atomic_fmax",
   [LSC_OP_ATOMIC_FCMPXCHG]  = "atomic_fcmpxchg",
   [LSC_OP_ATOMIC_AND]       = "atomic_and",
   [LSC_OP_ATOMIC_OR]        = "atomic_or",
   [LSC_OP_ATOMIC_XOR]       = "atomic_xor",
   [LSC_OP_FENCE]            = "fence",
   [LSC_OP_LOAD_CMASK_MSRT]  = "load_cmask_msrt",
   [LSC_OP_STORE_CMASK_MSRT] = "store_cmask_msrt",
};

DEFINE_TO_STRING(gen_lsc_opcode_to_string, enum lsc_opcode, gen_lsc_opcode_names)
DEFINE_FROM_STRING(gen_lsc_opcode_from_string, enum lsc_opcode, gen_lsc_opcode_names)


static const char *const gen_lsc_addr_size_names[] = {
   [LSC_ADDR_SIZE_A16] = "a16",
   [LSC_ADDR_SIZE_A32] = "a32",
   [LSC_ADDR_SIZE_A64] = "a64",
};

DEFINE_TO_STRING(gen_lsc_addr_size_to_string, enum lsc_addr_size, gen_lsc_addr_size_names)
DEFINE_FROM_STRING(gen_lsc_addr_size_from_string, enum lsc_addr_size, gen_lsc_addr_size_names)


static const char *const gen_lsc_data_size_names[] = {
   [LSC_DATA_SIZE_D8]      = "d8",
   [LSC_DATA_SIZE_D16]     = "d16",
   [LSC_DATA_SIZE_D32]     = "d32",
   [LSC_DATA_SIZE_D64]     = "d64",
   [LSC_DATA_SIZE_D8U32]   = "d8u32",
   [LSC_DATA_SIZE_D16U32]  = "d16u32",
   [LSC_DATA_SIZE_D16BF32] = "d16bf32",
};

DEFINE_TO_STRING(gen_lsc_data_size_to_string, enum lsc_data_size, gen_lsc_data_size_names)
DEFINE_FROM_STRING(gen_lsc_data_size_from_string, enum lsc_data_size, gen_lsc_data_size_names)


static const char *const gen_lsc_cmask_names[] = {
   [LSC_CMASK_X]    = "x",
   [LSC_CMASK_Y]    = "y",
   [LSC_CMASK_XY]   = "xy",
   [LSC_CMASK_Z]    = "z",
   [LSC_CMASK_XZ]   = "xz",
   [LSC_CMASK_YZ]   = "yz",
   [LSC_CMASK_XYZ]  = "xyz",
   [LSC_CMASK_W]    = "w",
   [LSC_CMASK_XW]   = "xw",
   [LSC_CMASK_YW]   = "yw",
   [LSC_CMASK_XYW]  = "xyw",
   [LSC_CMASK_ZW]   = "zw",
   [LSC_CMASK_XZW]  = "xzw",
   [LSC_CMASK_YZW]  = "yzw",
   [LSC_CMASK_XYZW] = "xyzw",
};

DEFINE_TO_STRING(gen_lsc_cmask_to_string, enum lsc_cmask, gen_lsc_cmask_names)
DEFINE_FROM_STRING(gen_lsc_cmask_from_string, enum lsc_cmask, gen_lsc_cmask_names)


static const char *const gen_lsc_fence_scope_names[] = {
   [LSC_FENCE_THREADGROUP]    = "threadgroup",
   [LSC_FENCE_LOCAL]          = "local",
   [LSC_FENCE_TILE]           = "tile",
   [LSC_FENCE_GPU]            = "gpu",
   [LSC_FENCE_ALL_GPU]        = "all_gpu",
   [LSC_FENCE_SYSTEM_RELEASE] = "system_release",
   [LSC_FENCE_SYSTEM_ACQUIRE] = "system_acquire",
};

DEFINE_TO_STRING(gen_lsc_fence_scope_to_string, enum lsc_fence_scope, gen_lsc_fence_scope_names)
DEFINE_FROM_STRING(gen_lsc_fence_scope_from_string, enum lsc_fence_scope, gen_lsc_fence_scope_names)


static const char *const gen_lsc_flush_type_names[] = {
   [LSC_FLUSH_TYPE_NONE]       = "none",
   [LSC_FLUSH_TYPE_EVICT]      = "evict",
   [LSC_FLUSH_TYPE_INVALIDATE] = "invalidate",
   [LSC_FLUSH_TYPE_DISCARD]    = "discard",
   [LSC_FLUSH_TYPE_CLEAN]      = "clean",
   [LSC_FLUSH_TYPE_L3ONLY]     = "l3only",
   [LSC_FLUSH_TYPE_NONE_6]     = "none_6",
};

DEFINE_TO_STRING(gen_lsc_flush_type_to_string, enum lsc_flush_type, gen_lsc_flush_type_names)
DEFINE_FROM_STRING(gen_lsc_flush_type_from_string, enum lsc_flush_type, gen_lsc_flush_type_names)


static const char *const xe2_lsc_cache_load_names[] = {
   [XE2_LSC_CACHE_LOAD_L1UC_L3UC]   = "uc.uc",
   [XE2_LSC_CACHE_LOAD_L1UC_L3C]    = "uc.ca",
   [XE2_LSC_CACHE_LOAD_L1UC_L3CC]   = "uc.cc",
   [XE2_LSC_CACHE_LOAD_L1C_L3UC]    = "ca.uc",
   [XE2_LSC_CACHE_LOAD_L1C_L3C]     = "ca.ca",
   [XE2_LSC_CACHE_LOAD_L1C_L3CC]    = "ca.cc",
   [XE2_LSC_CACHE_LOAD_L1S_L3UC]    = "st.uc",
   [XE2_LSC_CACHE_LOAD_L1S_L3C]     = "st.ca",
   [XE2_LSC_CACHE_LOAD_L1IAR_L3IAR] = "ri.ri",
};

static const char *const lsc_cache_load_names[] = {
   [LSC_CACHE_LOAD_L1UC_L3UC] = "uc.uc",
   [LSC_CACHE_LOAD_L1UC_L3C]  = "uc.ca",
   [LSC_CACHE_LOAD_L1C_L3UC]  = "ca.uc",
   [LSC_CACHE_LOAD_L1C_L3C]   = "ca.ca",
   [LSC_CACHE_LOAD_L1S_L3UC]  = "st.uc",
   [LSC_CACHE_LOAD_L1S_L3C]   = "st.ca",
   [LSC_CACHE_LOAD_L1IAR_L3C] = "ri.ca",
};

static const char *const xe2_lsc_cache_store_names[] = {
   [XE2_LSC_CACHE_STORE_L1UC_L3UC] = "uc.uc",
   [XE2_LSC_CACHE_STORE_L1UC_L3WB] = "uc.wb",
   [XE2_LSC_CACHE_STORE_L1WT_L3UC] = "wt.uc",
   [XE2_LSC_CACHE_STORE_L1WT_L3WB] = "wt.wb",
   [XE2_LSC_CACHE_STORE_L1S_L3UC]  = "st.uc",
   [XE2_LSC_CACHE_STORE_L1S_L3WB]  = "st.wb",
   [XE2_LSC_CACHE_STORE_L1WB_L3WB] = "wb.wb",
};

static const char *const lsc_cache_store_names[] = {
   [LSC_CACHE_STORE_L1UC_L3UC] = "uc.uc",
   [LSC_CACHE_STORE_L1UC_L3WB] = "uc.wb",
   [LSC_CACHE_STORE_L1WT_L3UC] = "wt.uc",
   [LSC_CACHE_STORE_L1WT_L3WB] = "wt.wb",
   [LSC_CACHE_STORE_L1S_L3UC]  = "st.uc",
   [LSC_CACHE_STORE_L1S_L3WB]  = "st.wb",
   [LSC_CACHE_STORE_L1WB_L3WB] = "wb.wb",
};

static bool
lsc_opcode_uses_load_cache(enum lsc_opcode opcode)
{
   return opcode == LSC_OP_LOAD ||
          opcode == LSC_OP_LOAD_CMASK ||
          opcode == LSC_OP_LOAD_CMASK_MSRT;
}

const char *
gen_lsc_cache_ctrl_to_string(const struct intel_device_info *devinfo,
                             enum lsc_opcode op, unsigned cache_ctrl)
{
   if (cache_ctrl == 0)
      return "";

   const bool xe2 = devinfo->ver >= 20;
   const bool load = lsc_opcode_uses_load_cache(op);

   if (load) {
      return xe2 ? LOOKUP_BY_VALUE(xe2_lsc_cache_load_names, cache_ctrl)
                 : LOOKUP_BY_VALUE(lsc_cache_load_names, cache_ctrl);
   } else {
      return xe2 ? LOOKUP_BY_VALUE(xe2_lsc_cache_store_names, cache_ctrl)
                 : LOOKUP_BY_VALUE(lsc_cache_store_names, cache_ctrl);
   }
}

unsigned
gen_lsc_cache_ctrl_from_string(const struct intel_device_info *devinfo,
                               enum lsc_opcode op,
                               const char *str, int size, bool *valid)
{
   assert(valid);

   const bool xe2 = devinfo->ver >= 20;
   const bool load = lsc_opcode_uses_load_cache(op);
   const int ctrl = load ?
      (xe2 ? LOOKUP_BY_NAME(xe2_lsc_cache_load_names, str, size) :
             LOOKUP_BY_NAME(lsc_cache_load_names, str, size)) :
      (xe2 ? LOOKUP_BY_NAME(xe2_lsc_cache_store_names, str, size) :
             LOOKUP_BY_NAME(lsc_cache_store_names, str, size));

   *valid = ctrl >= 0;
   return *valid ? (unsigned)ctrl : 0;
}


static const char *const gen_sampler_msg_type_names[] = {
   [GEN_SAMPLER_MESSAGE_SAMPLE]                     = "sample",
   [GEN_SAMPLER_MESSAGE_SAMPLE_BIAS]                = "sample_b",
   [GEN_SAMPLER_MESSAGE_SAMPLE_LOD]                 = "sample_l",
   [GEN_SAMPLER_MESSAGE_SAMPLE_COMPARE]             = "sample_c",
   [GEN_SAMPLER_MESSAGE_SAMPLE_DERIVS]              = "sample_d",
   [GEN_SAMPLER_MESSAGE_SAMPLE_BIAS_COMPARE]        = "sample_b_c",
   [GEN_SAMPLER_MESSAGE_SAMPLE_LOD_COMPARE]         = "sample_l_c",
   [GEN_SAMPLER_MESSAGE_SAMPLE_LD]                  = "ld",
   [GEN_SAMPLER_MESSAGE_SAMPLE_GATHER4]             = "gather4",
   [GEN_SAMPLER_MESSAGE_LOD]                        = "lod",
   [GEN_SAMPLER_MESSAGE_SAMPLE_RESINFO]             = "resinfo",
   [GEN_SAMPLER_MESSAGE_SAMPLE_SAMPLEINFO]          = "sampleinfo",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_L]       = "gather4_l",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_B]       = "gather4_b",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_I]       = "gather4_i",
   [GEN_SAMPLER_MESSAGE_SAMPLE_GATHER4_C]           = "gather4_c",
   [GEN_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO]          = "gather4_po",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_COMPARE_MLOD]    = "sample_c_mlod",
   [GEN_SAMPLER_MESSAGE_SAMPLE_DERIV_COMPARE]       = "sample_d_c",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_I_C]     = "gather4_i_c",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_L_C]     = "gather4_l_c",
   [GEN_SAMPLER_MESSAGE_SAMPLE_LZ]                  = "sample_lz",
   [GEN_SAMPLER_MESSAGE_SAMPLE_C_LZ]                = "sample_c_lz",
   [GEN_SAMPLER_MESSAGE_SAMPLE_LD_LZ]               = "ld_lz",
   [GEN_SAMPLER_MESSAGE_SAMPLE_LD2DMS_W]            = "ld2dms_w",
   [GEN_SAMPLER_MESSAGE_SAMPLE_LD_MCS]              = "ld_mcs",
   [GEN_SAMPLER_MESSAGE_SAMPLE_LD2DMS]              = "ld2dms",
   [GEN_SAMPLER_MESSAGE_SAMPLE_LD2DSS]              = "ld2dss",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO]              = "sample_po",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_BIAS]         = "sample_po_b",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_LOD]          = "sample_po_l",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_COMPARE]      = "sample_po_c",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_DERIVS]       = "sample_po_d",
   [GEN_XE3_SAMPLER_MESSAGE_SAMPLE_PO_BIAS_COMPARE] = "sample_po_b_c",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_LOD_COMPARE]  = "sample_po_l_c",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_L]    = "gather4_po_l",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_B]    = "gather4_po_b",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_I]    = "gather4_po_i",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_C]    = "gather4_po_c",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_D_C]          = "sample_po_d_c",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_I_C]  = "gather4_po_i_c",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_L_C]  = "gather4_po_l_c",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_LZ]           = "sample_po_lz",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_C_LZ]         = "sample_po_c_lz",
};

const char *
gen_sampler_msg_type_to_string(const struct intel_device_info *devinfo,
                               unsigned msg_type)
{
   /* Value 18 aliases GATHER4_PO_C pre-Xe2 and MLOD on Xe2+. */
   if (msg_type == GEN_SAMPLER_MESSAGE_SAMPLE_GATHER4_PO_C)
      return devinfo->ver >= 20 ? "sample_mlod" : "gather4_po_c";

   return LOOKUP_BY_VALUE(gen_sampler_msg_type_names, msg_type);
}

/* TODO: This was initially based on the unambiguous cases from brw_sampler.c,
 * see if we can consolidate some of this between here and brw_sampler.c code.
 * Note some cases might be incomplete, sometimes brw would've made one
 * decision on the params but others are possible based on other factors.
 */

static const char *const gen_sampler_params[] = {
   [GEN_SAMPLER_MESSAGE_SAMPLE]                 = "u,v,r,ai",
   [GEN_SAMPLER_MESSAGE_SAMPLE_COMPARE]         = "ref,u,v,r,ai",
   [GEN_SAMPLER_MESSAGE_SAMPLE_LD]              = "u,v,lod,r",
   [GEN_SAMPLER_MESSAGE_SAMPLE_GATHER4]         = "u,v,r,ai",
   [GEN_SAMPLER_MESSAGE_LOD]                    = "u,v,r,ai",
   [GEN_SAMPLER_MESSAGE_SAMPLE_RESINFO]         = "lod",
   [GEN_SAMPLER_MESSAGE_SAMPLE_GATHER4_C]       = "ref,u,v,r,ai",
   [GEN_SAMPLER_MESSAGE_SAMPLE_LZ]              = "u,v,r,ai",
   [GEN_SAMPLER_MESSAGE_SAMPLE_C_LZ]            = "ref,u,v,r,ai",
   [GEN_SAMPLER_MESSAGE_SAMPLE_LD_LZ]           = "u,v,r",
   [GEN_SAMPLER_MESSAGE_SAMPLE_LD_MCS]          = "u,v,r,lod",
   [GEN_SAMPLER_MESSAGE_SAMPLE_LD2DMS]          = "si,mcs,u,v,r,lod",
   [GEN_SAMPLER_MESSAGE_SAMPLE_LD2DSS]          = "ssi,u,v,r,lod",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_I]   = "u,v,r,ai",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_I_C] = "ref,u,v,r,ai",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_B]   = "bias,u,v,r,ai",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_L]   = "lod,u,v,r,ai",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_GATHER4_L_C] = "ref,lod,u,v,r,ai",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_DERIVS]   = "u,dudx,dudy,v,dvdx,dvdy,offuvr4_r,mlod",
   [GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO_D_C]      = "ref,u,dudx,dudy,v,dvdx,dvdy,offuv4_r",
};

const char *
gen_sampler_params_to_string(const struct intel_device_info *devinfo, unsigned msg_type)
{
   if (msg_type == GEN_SAMPLER_MESSAGE_SAMPLE_LD2DMS_W)
      return devinfo->verx10 >= 125 ? "si,mcs0,mcs1,mcs2,mcs3,u,v,r,lod"
                                    : "si,mcsl,mcsh,u,v,r,lod";

   if (msg_type == GEN_SAMPLER_MESSAGE_SAMPLE_DERIVS)
      return devinfo->verx10 >= 125 ? "u,dudx,dudy,v,dvdx,dvdy,r,mlod"
                                    : "u,dudx,dudy,v,dvdx,dvdy,r,drdx,drdy,ai,mlod";

   if (msg_type == GEN_SAMPLER_MESSAGE_SAMPLE_DERIV_COMPARE)
      return devinfo->ver >= 20 ? NULL
                                : "ref,u,dudx,dudy,v,dvdx,dvdy,r,drdx,drdy,ai";

   return LOOKUP_BY_VALUE(gen_sampler_params, msg_type);
}

static const char *const gen_urb_opcode_names[] = {
   [GEN_URB_OPCODE_ATOMIC_MOV]   = "atomic_mov",
   [GEN_URB_OPCODE_ATOMIC_INC]   = "atomic_inc",
   [GEN_URB_OPCODE_ATOMIC_ADD]   = "atomic_add",
   [GEN_URB_OPCODE_SIMD8_WRITE]  = "simd8_write",
   [GEN_URB_OPCODE_SIMD8_READ]   = "simd8_read",
   [GEN_GFX125_URB_OPCODE_FENCE] = "fence",
};

DEFINE_TO_STRING(gen_urb_opcode_to_string, unsigned, gen_urb_opcode_names)


static const char *const gen_hdc1_surface_simd_mode_names[] = {
   [GEN_HDC1_SURFACE_SIMD_MODE_SIMD4X2] = "simd4x2",
   [GEN_HDC1_SURFACE_SIMD_MODE_SIMD16]  = "simd16",
   [GEN_HDC1_SURFACE_SIMD_MODE_SIMD8]   = "simd8",
};

DEFINE_TO_STRING(gen_hdc1_surface_simd_mode_to_string, unsigned, gen_hdc1_surface_simd_mode_names)


static const char *const gen_hdc1_aop_names[] = {
   [GEN_AOP_AND]    = "and",
   [GEN_AOP_OR]     = "or",
   [GEN_AOP_XOR]    = "xor",
   [GEN_AOP_MOV]    = "mov",
   [GEN_AOP_INC]    = "inc",
   [GEN_AOP_DEC]    = "dec",
   [GEN_AOP_ADD]    = "add",
   [GEN_AOP_SUB]    = "sub",
   [GEN_AOP_REVSUB] = "revsub",
   [GEN_AOP_IMAX]   = "imax",
   [GEN_AOP_IMIN]   = "imin",
   [GEN_AOP_UMAX]   = "umax",
   [GEN_AOP_UMIN]   = "umin",
   [GEN_AOP_CMPWR]  = "cmpwr",
   [GEN_AOP_PREDEC] = "predec",
};

DEFINE_TO_STRING(gen_hdc1_aop_to_string, unsigned, gen_hdc1_aop_names)


static const char *const gen_hdc1_float_aop_names[] = {
   [GEN_AOP_FMAX]   = "fmax",
   [GEN_AOP_FMIN]   = "fmin",
   [GEN_AOP_FCMPWR] = "fcmpwr",
   [GEN_AOP_FADD]   = "fadd",
};

DEFINE_TO_STRING(gen_hdc1_float_aop_to_string, unsigned, gen_hdc1_float_aop_names)


static const char *const gen_hdc1_owords_names[] = {
   [GEN_DATAPORT_OWORD_BLOCK_1_OWORDLOW]      = "owords1_low",
   [GEN_DATAPORT_OWORD_BLOCK_1_OWORDHIGH]     = "owords1_high",
   [GEN_DATAPORT_OWORD_BLOCK_2_OWORDS]        = "owords2",
   [GEN_DATAPORT_OWORD_BLOCK_4_OWORDS]        = "owords4",
   [GEN_DATAPORT_OWORD_BLOCK_8_OWORDS]        = "owords8",
   [GEN_GFX12_DATAPORT_OWORD_BLOCK_16_OWORDS] = "owords16",
};

DEFINE_TO_STRING(gen_hdc1_owords_to_string, unsigned, gen_hdc1_owords_names)


static const char *const gen_rt_write_subtype_names[] = {
   [GEN_RT_WRITE_SUBTYPE_SIMD16]             = "simd16",
   [GEN_RT_WRITE_SUBTYPE_SIMD16_REPDATA]     = "simd16_repdata",
   [GEN_RT_WRITE_SUBTYPE_SIMD8_DUALSRC_LOW]  = "simd8_dualsrc_low",
   [GEN_RT_WRITE_SUBTYPE_SIMD8_DUALSRC_HIGH] = "simd8_dualsrc_high",
   [GEN_RT_WRITE_SUBTYPE_SIMD8]              = "simd8",
   [GEN_RT_WRITE_SUBTYPE_SIMD8_IMAGEWRITE]   = "simd8_imagewrite",
   [GEN_RT_WRITE_SUBTYPE_SIMD16_REPDATA_7]   = "simd16_repdata_7",
};

static const char *const xe2_rt_write_subtype_names[] = {
   [GEN_XE2_RT_WRITE_SUBTYPE_SIMD16]         = "simd16",
   [GEN_XE2_RT_WRITE_SUBTYPE_SIMD32]         = "simd32",
   [GEN_XE2_RT_WRITE_SUBTYPE_SIMD16_DUALSRC] = "simd16_dualsrc",
};

const char *
gen_rt_write_subtype_to_string(const struct intel_device_info *devinfo,
                               unsigned subtype)
{
   return devinfo->ver >= 20 ?
      LOOKUP_BY_VALUE(xe2_rt_write_subtype_names, subtype) :
      LOOKUP_BY_VALUE(gen_rt_write_subtype_names, subtype);
}
