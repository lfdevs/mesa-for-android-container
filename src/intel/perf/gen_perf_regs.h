/*
 * Copyright © 2019 Intel Corporation
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

#ifndef GEN_PERF_REGS_H
#define GEN_PERF_REGS_H

#define INTEL_MASK(high, low) (((1u<<((high)-(low)+1))-1)<<(low))

/* GT core frequency counters */
#define GEN7_RPSTAT1                       0xA01C
#define  GEN7_RPSTAT1_CURR_GT_FREQ_SHIFT   7
#define  GEN7_RPSTAT1_CURR_GT_FREQ_MASK    INTEL_MASK(13, 7)
#define  GEN7_RPSTAT1_PREV_GT_FREQ_SHIFT   0
#define  GEN7_RPSTAT1_PREV_GT_FREQ_MASK    INTEL_MASK(6, 0)

#define GEN9_RPSTAT0                       0xA01C
#define  GEN9_RPSTAT0_CURR_GT_FREQ_SHIFT   23
#define  GEN9_RPSTAT0_CURR_GT_FREQ_MASK    INTEL_MASK(31, 23)
#define  GEN9_RPSTAT0_PREV_GT_FREQ_SHIFT   0
#define  GEN9_RPSTAT0_PREV_GT_FREQ_MASK    INTEL_MASK(8, 0)

/* Pipeline statistic counters */
#define IA_VERTICES_COUNT          0x2310
#define IA_PRIMITIVES_COUNT        0x2318
#define VS_INVOCATION_COUNT        0x2320
#define HS_INVOCATION_COUNT        0x2300
#define DS_INVOCATION_COUNT        0x2308
#define GS_INVOCATION_COUNT        0x2328
#define GS_PRIMITIVES_COUNT        0x2330
#define CL_INVOCATION_COUNT        0x2338
#define CL_PRIMITIVES_COUNT        0x2340
#define PS_INVOCATION_COUNT        0x2348
#define CS_INVOCATION_COUNT        0x2290
#define PS_DEPTH_COUNT             0x2350

/* Stream-out counters */
#define GEN6_SO_PRIM_STORAGE_NEEDED     0x2280
#define GEN7_SO_PRIM_STORAGE_NEEDED(n)  (0x5240 + (n) * 8)
#define GEN6_SO_NUM_PRIMS_WRITTEN       0x2288
#define GEN7_SO_NUM_PRIMS_WRITTEN(n)    (0x5200 + (n) * 8)

#endif /* GEN_PERF_REGS_H */
