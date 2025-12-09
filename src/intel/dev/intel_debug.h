/*
 * Copyright 2003 VMware, Inc.
 * Copyright © 2007 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef INTEL_DEBUG_H
#define INTEL_DEBUG_H

#include <stdint.h>
#include "compiler/shader_enums.h"
#include "util/bitset.h"
#include "util/macros.h"

#ifdef __cplusplus
extern "C" {
#endif
/**
 * \file intel_debug.h
 *
 * Basic INTEL_DEBUG environment variable handling.  This file defines the
 * list of debugging flags, as well as some macros for handling them.
 */

enum intel_debug_flag {
   DEBUG_TEXTURE = 0,
   DEBUG_BLIT,
   DEBUG_PERF,
   DEBUG_PERFMON,
   DEBUG_BATCH,
   DEBUG_BUFMGR,
   DEBUG_SYNC,
   DEBUG_SF,
   DEBUG_SUBMIT,
   DEBUG_URB,
   DEBUG_CLIP,
   DEBUG_STALL,
   DEBUG_BLORP,
   DEBUG_NO_DUAL_OBJECT_GS,
   DEBUG_OPTIMIZER,
   DEBUG_MDA,
   DEBUG_ANNOTATION,
   DEBUG_NO_OACONFIG,
   DEBUG_NO_FILL_OPT,
   DEBUG_SPILL_FS,
   DEBUG_SPILL_VEC4,
   DEBUG_HEX,
   DEBUG_NO_COMPACTION,
   DEBUG_L3,
   DEBUG_NO_CCS,
   DEBUG_NO_HIZ,
   DEBUG_NO_CCS_MODIFIER,
   DEBUG_COLOR,
   DEBUG_REEMIT,
   DEBUG_SOFT64,
   DEBUG_BT,
   DEBUG_PIPE_CONTROL,
   DEBUG_NO_FAST_CLEAR,
   DEBUG_CAPTURE_ALL,
   DEBUG_PERF_SYMBOL_NAMES,
   DEBUG_SWSB_STALL,
   DEBUG_HEAPS,
   DEBUG_ISL,
   DEBUG_SPARSE,
   DEBUG_DRAW_BKP,
   DEBUG_DISPATCH_BKP,
   DEBUG_BATCH_STATS,
   DEBUG_REG_PRESSURE,
   DEBUG_SHADER_PRINT,
   DEBUG_CL_QUIET,
   DEBUG_BVH_BLAS,
   DEBUG_BVH_TLAS,
   DEBUG_BVH_BLAS_IR_HDR,
   DEBUG_BVH_TLAS_IR_HDR,
   DEBUG_BVH_BLAS_IR_AS,
   DEBUG_BVH_TLAS_IR_AS,
   DEBUG_BVH_NO_BUILD,
   DEBUG_NO_SEND_GATHER,
   DEBUG_NO_VRT,
   DEBUG_RT_NO_TRACE,
   DEBUG_SHADERS_LINENO,
   DEBUG_SHOW_SHADER_STAGE,
   /* Keep the stages grouped */
   DEBUG_VS,
   DEBUG_TCS,
   DEBUG_TES,
   DEBUG_GS,
   DEBUG_WM,
   DEBUG_TASK,
   DEBUG_MESH,
   DEBUG_CS,
   DEBUG_RT,
   DEBUG_NO8,

   DEBUG_NO16,
   DEBUG_NO32,
   DEBUG_DO32,

   /* Must be the last entry */
   INTEL_DEBUG_MAX,
};

extern BITSET_WORD intel_debug[BITSET_WORDS(INTEL_DEBUG_MAX)];


/* Check if a debug flag is enabled by testing its bit position */
#define INTEL_DEBUG(flag) unlikely(BITSET_TEST(intel_debug, (flag)))

/* These flags are not compatible with the disk shader cache */
#define DEBUG_DISK_CACHE_DISABLE_MASK 0

/* Flags to determine what bvh to dump out */
#define INTEL_DEBUG_BVH_ANY (unlikely(INTEL_DEBUG(DEBUG_BVH_BLAS) ||    \
                                      INTEL_DEBUG(DEBUG_BVH_TLAS) ||    \
                                      INTEL_DEBUG(DEBUG_BVH_BLAS_IR_HDR) || \
                                      INTEL_DEBUG(DEBUG_BVH_TLAS_IR_HDR) || \
                                      INTEL_DEBUG(DEBUG_BVH_BLAS_IR_AS) || \
                                      INTEL_DEBUG(DEBUG_BVH_TLAS_IR_AS)))

extern uint64_t intel_simd;
extern uint32_t intel_debug_bkp_before_draw_count;
extern uint32_t intel_debug_bkp_after_draw_count;
extern uint32_t intel_debug_bkp_before_dispatch_count;
extern uint32_t intel_debug_bkp_after_dispatch_count;
extern uint64_t intel_debug_batch_frame_start;
extern uint64_t intel_debug_batch_frame_stop;
extern uint32_t intel_shader_dump_filter;

#define INTEL_SIMD(type, size)        (!!(intel_simd & (DEBUG_ ## type ## _SIMD ## size)))

/* VS, TCS, TES and GS stages are dispatched in one size */
#define DEBUG_FS_SIMD8    (1ull << 0)
#define DEBUG_FS_SIMD16   (1ull << 1)
#define DEBUG_FS_SIMD32   (1ull << 2)
#define DEBUG_FS_SIMD2X8  (1ull << 3)
#define DEBUG_FS_SIMD4X8  (1ull << 4)
#define DEBUG_FS_SIMD2X16 (1ull << 5)

#define DEBUG_CS_SIMD8    (1ull << 6)
#define DEBUG_CS_SIMD16   (1ull << 7)
#define DEBUG_CS_SIMD32   (1ull << 8)

#define DEBUG_TS_SIMD8    (1ull << 9)
#define DEBUG_TS_SIMD16   (1ull << 10)
#define DEBUG_TS_SIMD32   (1ull << 11)

#define DEBUG_MS_SIMD8    (1ull << 12)
#define DEBUG_MS_SIMD16   (1ull << 13)
#define DEBUG_MS_SIMD32   (1ull << 14)

#define DEBUG_RT_SIMD8    (1ull << 15)
#define DEBUG_RT_SIMD16   (1ull << 16)
#define DEBUG_RT_SIMD32   (1ull << 17)

#define SIMD_DISK_CACHE_MASK ((1ull << 18) - 1)

#ifdef HAVE_ANDROID_PLATFORM
#define LOG_TAG "INTEL-MESA"
#if ANDROID_API_LEVEL >= 26
#include <log/log.h>
#else
#include <cutils/log.h>
#endif /* use log/log.h start from android 8 major version */
#ifndef ALOGW
#define ALOGW LOGW
#endif
#define dbg_printf(...)	ALOGW(__VA_ARGS__)
#else
#define dbg_printf(...)	fprintf(stderr, __VA_ARGS__)
#endif /* HAVE_ANDROID_PLATFORM */

#define DBG(...) do {                  \
   if (INTEL_DEBUG(FILE_DEBUG_FLAG))   \
      dbg_printf(__VA_ARGS__);         \
} while(0)

extern uint64_t intel_debug_flag_for_shader_stage(mesa_shader_stage stage);

extern void process_intel_debug_variable(void);

#ifdef __cplusplus
}
#endif

#endif /* INTEL_DEBUG_H */
