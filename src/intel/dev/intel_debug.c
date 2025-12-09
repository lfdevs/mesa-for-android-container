/*
 * Copyright 2003 VMware, Inc.
 * Copyright © 2006 Intel Corporation
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

/**
 * \file intel_debug.c
 *
 * Support for the INTEL_DEBUG environment variable, along with other
 * miscellaneous debugging code.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dev/intel_debug.h"
#include "util/macros.h"
#include "util/u_debug.h"
#include "util/u_math.h"
#include "c11/threads.h"

BITSET_WORD intel_debug[BITSET_WORDS(INTEL_DEBUG_MAX)] = {0};

struct debug_control_bitset {
   const char *string;
   uint32_t range[2];
};

static const struct debug_control_bitset debug_control[] = {
#define OPT1(name, bit) \
   { .string = name, .range = { bit, bit }, }
#define OPT2(name, start, end) \
   { .string = name, .range = { start, end }, }
   OPT1("tex",               DEBUG_TEXTURE),
   OPT1("blit",              DEBUG_BLIT),
   OPT1("fall",              DEBUG_PERF),
   OPT1("perf",              DEBUG_PERF),
   OPT1("perfmon",           DEBUG_PERFMON),
   OPT1("bat",               DEBUG_BATCH),
   OPT1("buf",               DEBUG_BUFMGR),
   OPT1("fs",                DEBUG_WM),
   OPT1("gs",                DEBUG_GS),
   OPT1("sync",              DEBUG_SYNC),
   OPT1("sf",                DEBUG_SF),
   OPT1("submit",            DEBUG_SUBMIT),
   OPT1("wm",                DEBUG_WM),
   OPT1("urb",               DEBUG_URB),
   OPT1("vs",                DEBUG_VS),
   OPT1("clip",              DEBUG_CLIP),
   OPT1("no16",              DEBUG_NO16),
   OPT1("blorp",             DEBUG_BLORP),
   OPT1("nodualobj",         DEBUG_NO_DUAL_OBJECT_GS),
   OPT1("optimizer",         DEBUG_OPTIMIZER),
   OPT1("mda",               DEBUG_MDA),
   OPT1("ann",               DEBUG_ANNOTATION),
   OPT1("no8",               DEBUG_NO8),
   OPT1("no-oaconfig",       DEBUG_NO_OACONFIG),
   OPT1("no-fill-opt",       DEBUG_NO_FILL_OPT),
   OPT1("spill_fs",          DEBUG_SPILL_FS),
   OPT1("spill_vec4",        DEBUG_SPILL_VEC4),
   OPT1("cs",                DEBUG_CS),
   OPT1("hex",               DEBUG_HEX),
   OPT1("nocompact",         DEBUG_NO_COMPACTION),
   OPT1("hs",                DEBUG_TCS),
   OPT1("tcs",               DEBUG_TCS),
   OPT1("ds",                DEBUG_TES),
   OPT1("tes",               DEBUG_TES),
   OPT1("l3",                DEBUG_L3),
   OPT1("do32",              DEBUG_DO32),
   OPT1("norbc",             DEBUG_NO_CCS),
   OPT1("noccs",             DEBUG_NO_CCS),
   OPT1("noccs-modifier",    DEBUG_NO_CCS_MODIFIER),
   OPT1("nohiz",             DEBUG_NO_HIZ),
   OPT1("color",             DEBUG_COLOR),
   OPT1("reemit",            DEBUG_REEMIT),
   OPT1("soft64",            DEBUG_SOFT64),
   OPT1("bt",                DEBUG_BT),
   OPT1("pc",                DEBUG_PIPE_CONTROL),
   OPT1("nofc",              DEBUG_NO_FAST_CLEAR),
   OPT1("no32",              DEBUG_NO32),
   OPT2("shaders",           DEBUG_VS, DEBUG_RT),
   OPT1("rt",                DEBUG_RT),
   OPT1("rt_notrace",        DEBUG_RT_NO_TRACE),
   OPT1("bvh_blas",          DEBUG_BVH_BLAS),
   OPT1("bvh_tlas",          DEBUG_BVH_TLAS),
   OPT1("bvh_blas_ir_hdr",   DEBUG_BVH_BLAS_IR_HDR),
   OPT1("bvh_tlas_ir_hdr",   DEBUG_BVH_TLAS_IR_HDR),
   OPT1("bvh_blas_ir_as",    DEBUG_BVH_BLAS_IR_AS),
   OPT1("bvh_tlas_ir_as",    DEBUG_BVH_TLAS_IR_AS),
   OPT1("bvh_no_build",      DEBUG_BVH_NO_BUILD),
   OPT1("task",              DEBUG_TASK),
   OPT1("mesh",              DEBUG_MESH),
   OPT1("stall",             DEBUG_STALL),
   OPT1("capture-all",       DEBUG_CAPTURE_ALL),
   OPT1("perf-symbol-names", DEBUG_PERF_SYMBOL_NAMES),
   OPT1("swsb-stall",        DEBUG_SWSB_STALL),
   OPT1("heaps",             DEBUG_HEAPS),
   OPT1("isl",               DEBUG_ISL),
   OPT1("sparse",            DEBUG_SPARSE),
   OPT1("draw_bkp",          DEBUG_DRAW_BKP),
   OPT1("dispatch_bkp",      DEBUG_DISPATCH_BKP),
   OPT1("bat-stats",         DEBUG_BATCH_STATS),
   OPT1("reg-pressure",      DEBUG_REG_PRESSURE),
   OPT1("shader-print",      DEBUG_SHADER_PRINT),
   OPT1("cl-quiet",          DEBUG_CL_QUIET),
   OPT1("no-send-gather",    DEBUG_NO_SEND_GATHER),
   OPT1("no-vrt",            DEBUG_NO_VRT),
   OPT1("shaders-lineno",    DEBUG_SHADERS_LINENO),
   OPT1("show_shader_stage", DEBUG_SHOW_SHADER_STAGE),
   { NULL, }
#undef OPT1
#undef OPT2
};
uint64_t intel_simd = 0;

static const struct debug_control simd_control[] = {
   { "fs8",    DEBUG_FS_SIMD8 },
   { "fs16",   DEBUG_FS_SIMD16 },
   { "fs32",   DEBUG_FS_SIMD32 },
   { "fs2x8",  DEBUG_FS_SIMD2X8 },
   { "fs4x8",  DEBUG_FS_SIMD4X8 },
   { "fs2x16", DEBUG_FS_SIMD2X16 },
   { "cs8",    DEBUG_CS_SIMD8 },
   { "cs16",   DEBUG_CS_SIMD16 },
   { "cs32",   DEBUG_CS_SIMD32 },
   { "ts8",    DEBUG_TS_SIMD8 },
   { "ts16",   DEBUG_TS_SIMD16 },
   { "ts32",   DEBUG_TS_SIMD32 },
   { "ms8",    DEBUG_MS_SIMD8 },
   { "ms16",   DEBUG_MS_SIMD16 },
   { "ms32",   DEBUG_MS_SIMD32 },
   { "rt8",    DEBUG_RT_SIMD8 },
   { "rt16",   DEBUG_RT_SIMD16 },
   { "rt32",   DEBUG_RT_SIMD32 },
   { NULL,     0 }
};

uint64_t
intel_debug_flag_for_shader_stage(mesa_shader_stage stage)
{
   uint64_t flags[] = {
      [MESA_SHADER_VERTEX] = DEBUG_VS,
      [MESA_SHADER_TESS_CTRL] = DEBUG_TCS,
      [MESA_SHADER_TESS_EVAL] = DEBUG_TES,
      [MESA_SHADER_GEOMETRY] = DEBUG_GS,
      [MESA_SHADER_FRAGMENT] = DEBUG_WM,
      [MESA_SHADER_COMPUTE] = DEBUG_CS,
      [MESA_SHADER_KERNEL] = DEBUG_CS,

      [MESA_SHADER_TASK]         = DEBUG_TASK,
      [MESA_SHADER_MESH]         = DEBUG_MESH,

      [MESA_SHADER_RAYGEN]       = DEBUG_RT,
      [MESA_SHADER_ANY_HIT]      = DEBUG_RT,
      [MESA_SHADER_CLOSEST_HIT]  = DEBUG_RT,
      [MESA_SHADER_MISS]         = DEBUG_RT,
      [MESA_SHADER_INTERSECTION] = DEBUG_RT,
      [MESA_SHADER_CALLABLE]     = DEBUG_RT,
   };
   return flags[stage];
}

#define DEBUG_FS_SIMD  (DEBUG_FS_SIMD8  | DEBUG_FS_SIMD16  | \
                        DEBUG_FS_SIMD32)
#define DEBUG_CS_SIMD  (DEBUG_CS_SIMD8  | DEBUG_CS_SIMD16  | DEBUG_CS_SIMD32)
#define DEBUG_TS_SIMD  (DEBUG_TS_SIMD8  | DEBUG_TS_SIMD16  | DEBUG_TS_SIMD32)
#define DEBUG_MS_SIMD  (DEBUG_MS_SIMD8  | DEBUG_MS_SIMD16  | DEBUG_MS_SIMD32)
#define DEBUG_RT_SIMD  (DEBUG_RT_SIMD8  | DEBUG_RT_SIMD16  | DEBUG_RT_SIMD32)

#define DEBUG_SIMD8_ALL \
   (DEBUG_FS_SIMD8  | \
    DEBUG_CS_SIMD8  | \
    DEBUG_TS_SIMD8  | \
    DEBUG_MS_SIMD8  | \
    DEBUG_RT_SIMD8)

#define DEBUG_SIMD16_ALL \
   (DEBUG_FS_SIMD16 | \
    DEBUG_CS_SIMD16 | \
    DEBUG_TS_SIMD16 | \
    DEBUG_MS_SIMD16 | \
    DEBUG_RT_SIMD16)

#define DEBUG_SIMD32_ALL \
   (DEBUG_FS_SIMD32 | \
    DEBUG_CS_SIMD32 | \
    DEBUG_TS_SIMD32 | \
    DEBUG_MS_SIMD32 | \
    DEBUG_RT_SIMD32)

uint64_t intel_debug_batch_frame_start = 0;
uint64_t intel_debug_batch_frame_stop = -1;

uint32_t intel_debug_bkp_before_draw_count = 0;
uint32_t intel_debug_bkp_after_draw_count = 0;
uint32_t intel_shader_dump_filter = 0;

uint32_t intel_debug_bkp_before_dispatch_count = 0;
uint32_t intel_debug_bkp_after_dispatch_count = 0;

static void
parse_debug_bitset(const char *env, const struct debug_control_bitset *tbl)
{
   /* Check if env is NULL or empty */
   if (!env || !*env)
      return;

   char *copy = strdup(env);
   if (!copy)
      return;

   /* Tokenize the string by space or comma */
   for (char *tok = strtok(copy, ", "); tok; tok = strtok(NULL, ", ")) {
      /* Check for negation prefix, useful if user would like to disable certian flags */
      bool negate = (*tok == '~' || *tok == '-');
      if (negate)
         tok++;

      for (unsigned i = 0; tbl[i].string; i++) {
         if (strcasecmp(tok, tbl[i].string) != 0)
            continue;

         for (unsigned bit = tbl[i].range[0]; bit <= tbl[i].range[1]; bit++) {
            if (negate)
               BITSET_CLEAR(intel_debug, bit);
            else
               BITSET_SET(intel_debug, bit);
         }
         break;
      }
   }
   free(copy);
}

static void
process_intel_debug_variable_once(void)
{
   BITSET_ZERO(intel_debug);
   parse_debug_bitset(os_get_option("INTEL_DEBUG"), debug_control);

   intel_simd = parse_debug_string(os_get_option("INTEL_SIMD_DEBUG"), simd_control);
   intel_debug_batch_frame_start =
      debug_get_num_option("INTEL_DEBUG_BATCH_FRAME_START", 0);
   intel_debug_batch_frame_stop =
      debug_get_num_option("INTEL_DEBUG_BATCH_FRAME_STOP", -1);

   intel_debug_bkp_before_draw_count =
      debug_get_num_option("INTEL_DEBUG_BKP_BEFORE_DRAW_COUNT", 0);
   intel_debug_bkp_after_draw_count =
      debug_get_num_option("INTEL_DEBUG_BKP_AFTER_DRAW_COUNT", 0);

   intel_shader_dump_filter =
      debug_get_num_option("INTEL_SHADER_DUMP_FILTER", 0);

   intel_debug_bkp_before_dispatch_count =
      debug_get_num_option("INTEL_DEBUG_BKP_BEFORE_DISPATCH_COUNT", 0);
   intel_debug_bkp_after_dispatch_count =
      debug_get_num_option("INTEL_DEBUG_BKP_AFTER_DISPATCH_COUNT", 0);

   if (!(intel_simd & DEBUG_FS_SIMD))
      intel_simd |=   DEBUG_FS_SIMD;
   if (!(intel_simd & DEBUG_CS_SIMD))
      intel_simd |=   DEBUG_CS_SIMD;
   if (!(intel_simd & DEBUG_TS_SIMD))
      intel_simd |=   DEBUG_TS_SIMD;
   if (!(intel_simd & DEBUG_MS_SIMD))
      intel_simd |=   DEBUG_MS_SIMD;
   if (!(intel_simd & DEBUG_RT_SIMD))
      intel_simd |=   DEBUG_RT_SIMD;

   if (BITSET_TEST(intel_debug, DEBUG_NO8))
      intel_simd &= ~DEBUG_SIMD8_ALL;

   if (BITSET_TEST(intel_debug, DEBUG_NO16))
      intel_simd &= ~DEBUG_SIMD16_ALL;

   if (BITSET_TEST(intel_debug, DEBUG_NO32))
      intel_simd &= ~DEBUG_SIMD32_ALL;

   BITSET_CLEAR(intel_debug, DEBUG_NO8);
   BITSET_CLEAR(intel_debug, DEBUG_NO16);
   BITSET_CLEAR(intel_debug, DEBUG_NO32);
}

void
process_intel_debug_variable(void)
{
   static once_flag process_intel_debug_variable_flag = ONCE_FLAG_INIT;

   call_once(&process_intel_debug_variable_flag,
             process_intel_debug_variable_once);
}
