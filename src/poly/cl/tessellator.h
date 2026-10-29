/*
    Copyright (c) Microsoft Corporation

    Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
*/

#ifndef __OPENCL_VERSION__
#error "Tessellator should only be imported by OpenCL C code"
#endif

#include "poly/geometry.h"
#include "poly/tessellator.h"
#include "util/u_math.h"

#pragma once

#define POLY_TESS_MIN_ISOLINE_DENSITY_TESSELLATION_FACTOR 1.0f
#define POLY_TESS_MAX_ISOLINE_DENSITY_TESSELLATION_FACTOR 64.0f

typedef unsigned int FXP; // fixed point number

enum {
   U = 0, // points on a tri patch
   V = 1,
};

enum {
   Ueq0 = 0, // edges on a tri patch
   Veq0 = 1,
   Weq0 = 2,
};

enum {
   Ueq1 = 2, // edges on a quad patch: Ueq0, Veq0, Ueq1, Veq1
   Veq1 = 3,
};

#define QUAD_AXES  2
#define QUAD_EDGES 4
#define TRI_EDGES  3

// The interior can just use a simpler stitch.
typedef enum DIAGONALS {
   DIAGONALS_INSIDE_TO_OUTSIDE,
   DIAGONALS_INSIDE_TO_OUTSIDE_EXCEPT_MIDDLE,
   DIAGONALS_MIRRORED
} DIAGONALS;

typedef struct TESS_FACTOR_CONTEXT {
   FXP fxpInvNumSegmentsOnFloorTessFactor;
   FXP fxpInvNumSegmentsOnCeilTessFactor;
   FXP fxpHalfTessFactorFraction;
   int numHalfTessFactorPoints;
   int splitPointOnFloorHalfTessFactor;
} TESS_FACTOR_CONTEXT;

struct INDEX_PATCH_CONTEXT {
   int insidePointIndexDeltaToRealValue;
   int insidePointIndexBadValue;
   int insidePointIndexReplacementValue;
   int outsidePointIndexPatchBase;
   int outsidePointIndexDeltaToRealValue;
   int outsidePointIndexBadValue;
   int outsidePointIndexReplacementValue;
};

struct INDEX_PATCH_CONTEXT2 {
   int baseIndexToInvert;
   int indexInversionEndPoint;
   int cornerCaseBadValue;
   int cornerCaseReplacementValue;
};

struct CHWTessellator {
   enum poly_tess_mode mode;
   uint index_bias;
   /* POLY_TESS_CCW_INLINE: write triangles counter-clockwise. */
   bool swap_winding;

   // array where we will store u/v's for the points we generate
   global struct poly_tess_point *Point;

   // array where we will store index topology
   global void *Index;

   // A second index patch we have to do handles the leftover strip of quads in
   // the middle of an odd quad patch after finishing all the concentric rings.
   // This also handles the leftover strip of points in the middle of an even
   // quad patch, when stitching the row of triangles up the left side (V major
   // quad) or bottom (U major quad) of the inner ring
   bool bUsingPatchedIndices;
   bool bUsingPatchedIndices2;
   struct INDEX_PATCH_CONTEXT IndexPatchCtx;
   struct INDEX_PATCH_CONTEXT2 IndexPatchCtx2;
};

/* The per-edge and per-axis arrays below stay in registers only while every
 * index is a constant. The edge and ring loops hold nested loops, so they are
 * not unrolled, and one array[edge] there puts the whole array in thread
 * local storage, read back from memory in the innermost generation loops.
 * These read an element through selects instead.
 */
#define TESS_SEL2(a, i, f) ((i) ? (a)[1] f : (a)[0] f)
#define TESS_SEL3(a, i, f)                                                     \
   ((i) == 0 ? (a)[0] f : (i) == 1 ? (a)[1] f : (a)[2] f)
#define TESS_SEL4(a, i, f)                                                     \
   ((i) < 2 ? ((i) == 0 ? (a)[0] f : (a)[1] f)                                 \
            : ((i) == 2 ? (a)[2] f : (a)[3] f))

#define FXP_INTEGER_BITS  15
#define FXP_FRACTION_BITS 16
#define FXP_FRACTION_MASK 0x0000ffff
#define FXP_INTEGER_MASK  0x7fff0000
#define FXP_ONE           (1 << FXP_FRACTION_BITS)
#define FXP_ONE_THIRD     0x00005555
#define FXP_TWO_THIRDS    0x0000aaaa
#define FXP_ONE_HALF      0x00008000

static inline global float *
tess_factors(constant struct poly_tess_params *p, uint patch)
{
   return p->tcs_buffer + (patch * p->tcs_stride_el);
}

/*
 * Generate an indexed draw for a patch with the computed number of indices.
 * This allocates heap memory for the index buffer, returning the allocated
 * memory.
 */
static inline global void *
poly_draw(constant struct poly_tess_params *p, enum poly_tess_mode mode,
          bool lines, uint patch, uint count)
{
   if (mode == POLY_TESS_MODE_COUNT) {
      p->counts[patch] = count;
   }

   if (mode == POLY_TESS_MODE_WITH_COUNTS) {
      /* The index buffer is already allocated, get a pointer inside it.
       * p->counts has had an inclusive prefix sum hence the subtraction.
       */
      uint offset_el = p->counts[sub_sat(patch, 1u)];
      if (patch == 0)
         offset_el = 0;

      return &p->index_buffer[offset_el];
   }

   return NULL;
}

static inline void
poly_draw_points(private struct CHWTessellator *ctx,
                 constant struct poly_tess_params *p, uint patch, uint count)
{
   /* For points mode with a single draw, we need to generate a trivial index
    * buffer to stuff in the patch ID in the right place.
    */
   global uint32_t *indices = poly_draw(p, ctx->mode, false, patch, count);

   if (ctx->mode == POLY_TESS_MODE_COUNT)
      return;

   for (int i = 0; i < count; ++i) {
      indices[i] = ctx->index_bias + i;
   }
}

static inline void
poly_draw_empty(constant struct poly_tess_params *p, enum poly_tess_mode mode,
                uint patch)
{
   if (mode == POLY_TESS_MODE_COUNT) {
      p->counts[patch] = 0;
   }
}

/*
 * Allocate heap memory for domain points for a patch. The allocation
 * is recorded in the coord_allocs[] array, which is in elements.
 */
static inline global struct poly_tess_point *
poly_heap_alloc_points(constant struct poly_tess_params *p, uint patch,
                       uint count)
{
   /* If we're recording statistics, increment now. The statistic is for
    * tessellation evaluation shader invocations, which is equal to the number
    * of domain points generated.
    */
   if (p->statistic) {
      atomic_fetch_add((volatile atomic_uint *)(p->statistic), count);
   }

   uint32_t elsize_B = sizeof(struct poly_tess_point);
   uint32_t alloc_B = poly_heap_alloc_offs(p->heap, elsize_B * count, 16);
   uint32_t alloc_el = alloc_B / elsize_B;

   p->coord_allocs[patch] = alloc_el;
   return (global struct poly_tess_point *)(((uintptr_t)p->heap->base) +
                                            alloc_B);
}

// Microsoft D3D11 Fixed Function Tessellator Reference - May 7, 2012
// amar.patel@microsoft.com

#define POLY_TESS_MIN_ODD_TESSELLATION_FACTOR  1
#define POLY_TESS_MAX_ODD_TESSELLATION_FACTOR  63
#define POLY_TESS_MIN_EVEN_TESSELLATION_FACTOR 2
#define POLY_TESS_MAX_EVEN_TESSELLATION_FACTOR 64

// 2^(-16), min positive fixed point fraction
#define EPSILON 0.0000152587890625f
#define MIN_ODD_TESSFACTOR_PLUS_HALF_EPSILON                                   \
   (POLY_TESS_MIN_ODD_TESSELLATION_FACTOR + EPSILON / 2)

static inline float
clamp_factor(float factor, enum poly_tess_partitioning partitioning, float maxf)
{
   float lower = (partitioning == POLY_TESS_PARTITIONING_FRACTIONAL_EVEN)
                    ? POLY_TESS_MIN_EVEN_TESSELLATION_FACTOR
                    : POLY_TESS_MIN_ODD_TESSELLATION_FACTOR;

   float upper = (partitioning == POLY_TESS_PARTITIONING_FRACTIONAL_ODD)
                    ? POLY_TESS_MAX_ODD_TESSELLATION_FACTOR
                    : POLY_TESS_MAX_EVEN_TESSELLATION_FACTOR;

   // If any TessFactor will end up > 1 after floatToFixed conversion later,
   // then force the inside TessFactors to be > 1 so there is a picture frame.
   if (partitioning == POLY_TESS_PARTITIONING_FRACTIONAL_ODD &&
       maxf > MIN_ODD_TESSFACTOR_PLUS_HALF_EPSILON) {

      lower = POLY_TESS_MIN_ODD_TESSELLATION_FACTOR + EPSILON;
   }

   factor = clamp(factor, lower, upper);

   if (partitioning == POLY_TESS_PARTITIONING_INTEGER) {
      factor = ceil(factor);
   }

   return factor;
}

static inline FXP
floatToFixed(const float input)
{
   return mad(input, FXP_ONE, 0.5f);
}

static inline bool
isOdd(const float input)
{
   return ((int)input) & 1;
}

static inline FXP
fxpCeil(const FXP input)
{
   if (input & FXP_FRACTION_MASK) {
      return (input & FXP_INTEGER_MASK) + FXP_ONE;
   }
   return input;
}

static inline FXP
fxpFloor(const FXP input)
{
   return (input & FXP_INTEGER_MASK);
}

/* Single exit: an early return leaves the inlined return value variable
 * unwritten on some paths, and once the ifs are flattened to selects that
 * stale value is carried around every enclosing loop (one register each).
 */
static inline int
PatchIndexValue(private struct CHWTessellator *ctx, int index)
{
   int r = index;

   if (ctx->bUsingPatchedIndices) {
      // assumed remapped outide indices are > remapped inside vertices
      if (index >= ctx->IndexPatchCtx.outsidePointIndexPatchBase) {
         r = index == ctx->IndexPatchCtx.outsidePointIndexBadValue
                ? ctx->IndexPatchCtx.outsidePointIndexReplacementValue
                : index + ctx->IndexPatchCtx.outsidePointIndexDeltaToRealValue;
      } else {
         r = index == ctx->IndexPatchCtx.insidePointIndexBadValue
                ? ctx->IndexPatchCtx.insidePointIndexReplacementValue
                : index + ctx->IndexPatchCtx.insidePointIndexDeltaToRealValue;
      }
   } else if (ctx->bUsingPatchedIndices2) {
      if (index == ctx->IndexPatchCtx2.cornerCaseBadValue) {
         r = ctx->IndexPatchCtx2.cornerCaseReplacementValue;
      } else if (index >= ctx->IndexPatchCtx2.baseIndexToInvert) {
         r = ctx->IndexPatchCtx2.indexInversionEndPoint - index;
      }
   }

   return r;
}

/* End of a stitch: the patch contexts are only read while a flag is set, but
 * the compiler cannot see that, so clear them too: nothing then stays live
 * from one edge to the next.
 */
static inline void
ResetIndexPatch(private struct CHWTessellator *ctx)
{
   ctx->bUsingPatchedIndices = false;
   ctx->bUsingPatchedIndices2 = false;
   ctx->IndexPatchCtx.insidePointIndexDeltaToRealValue = 0;
   ctx->IndexPatchCtx.insidePointIndexBadValue = 0;
   ctx->IndexPatchCtx.insidePointIndexReplacementValue = 0;
   ctx->IndexPatchCtx.outsidePointIndexPatchBase = 0;
   ctx->IndexPatchCtx.outsidePointIndexDeltaToRealValue = 0;
   ctx->IndexPatchCtx.outsidePointIndexBadValue = 0;
   ctx->IndexPatchCtx.outsidePointIndexReplacementValue = 0;
   ctx->IndexPatchCtx2.baseIndexToInvert = 0;
   ctx->IndexPatchCtx2.indexInversionEndPoint = 0;
   ctx->IndexPatchCtx2.cornerCaseBadValue = 0;
   ctx->IndexPatchCtx2.cornerCaseReplacementValue = 0;
}

/* Run a ring stitch with the index patching mode as a constant: the copy for
 * the unpatched edges (most of them) then carries none of the patch state
 * through its triangle loops.
 */
#define TESS_STITCH(ctx, call)                                                 \
   do {                                                                        \
      if ((ctx).bUsingPatchedIndices) {                                        \
         (ctx).bUsingPatchedIndices = true;                                    \
         (ctx).bUsingPatchedIndices2 = false;                                  \
         call;                                                                 \
      } else if ((ctx).bUsingPatchedIndices2) {                                \
         (ctx).bUsingPatchedIndices = false;                                   \
         (ctx).bUsingPatchedIndices2 = true;                                   \
         call;                                                                 \
      } else {                                                                 \
         (ctx).bUsingPatchedIndices = false;                                   \
         (ctx).bUsingPatchedIndices2 = false;                                  \
         call;                                                                 \
      }                                                                        \
   } while (0)

static inline void
DefinePoint(global struct poly_tess_point *out, FXP fxpU, FXP fxpV)
{
   out->u = fxpU;
   out->v = fxpV;
}

static inline void
DefineIndex(private struct CHWTessellator *ctx, int index,
            int indexStorageOffset)
{
   global uint32_t *indices = (global uint32_t *)ctx->Index;
   indices[indexStorageOffset] = ctx->index_bias + PatchIndexValue(ctx, index);
}

static inline void
DefineTriangle(private struct CHWTessellator *ctx, int index0, int index1,
               int index2, int indexStorageBaseOffset)
{
   index0 = PatchIndexValue(ctx, index0);
   index1 = PatchIndexValue(ctx, index1);
   index2 = PatchIndexValue(ctx, index2);

   if (ctx->swap_winding) {
      int tmp = index1;
      index1 = index2;
      index2 = tmp;
   }

   vstore3(ctx->index_bias + (uint3)(index0, index1, index2), 0,
           (global uint *)ctx->Index + indexStorageBaseOffset);
}

static inline uint32_t
RemoveMSB(uint32_t val)
{
   uint32_t bit = val ? (1 << (31 - clz(val))) : 0;
   return val & ~bit;
}

static inline int
NumPointsForTessFactor(bool odd, FXP fxpTessFactor)
{
   // Add epsilon for rounding and add 1 for odd
   FXP f = fxpTessFactor + (odd ? (FXP_ONE + 1) : 1);
   int r = fxpCeil(f / 2) >> (FXP_FRACTION_BITS - 1);
   return odd ? r : r + 1;
}

static inline void
ComputeTessFactorCtx(bool odd, FXP fxpTessFactor,
                     private TESS_FACTOR_CONTEXT *TessFactorCtx)
{
   // fxpHalfTessFactor == 1/2 if TessFactor is 1,
   // but we're pretending we are even.
   FXP fxpHalfTessFactor = (fxpTessFactor + 1 /*round*/) / 2;
   if (odd || (fxpHalfTessFactor == FXP_ONE_HALF)) {
      fxpHalfTessFactor += FXP_ONE_HALF;
   }
   FXP fxpFloorHalfTessFactor = fxpFloor(fxpHalfTessFactor);
   FXP fxpCeilHalfTessFactor = fxpCeil(fxpHalfTessFactor);
   TessFactorCtx->fxpHalfTessFactorFraction = fxpHalfTessFactor - fxpFloorHalfTessFactor;
   TessFactorCtx->numHalfTessFactorPoints =
      (fxpCeilHalfTessFactor >> FXP_FRACTION_BITS); // for EVEN, we don't include the point always
                                                    // fixed at the midpoint of the TessFactor
   if (fxpCeilHalfTessFactor == fxpFloorHalfTessFactor) {
      TessFactorCtx->splitPointOnFloorHalfTessFactor =
         /*pick value to cause this to be ignored*/ TessFactorCtx->numHalfTessFactorPoints + 1;
   } else if (odd) {
      if (fxpFloorHalfTessFactor == FXP_ONE) {
         TessFactorCtx->splitPointOnFloorHalfTessFactor = 0;
      } else {
         TessFactorCtx->splitPointOnFloorHalfTessFactor =
            (RemoveMSB((fxpFloorHalfTessFactor >> FXP_FRACTION_BITS) - 1) << 1) + 1;
      }
   } else {
      TessFactorCtx->splitPointOnFloorHalfTessFactor =
         (RemoveMSB(fxpFloorHalfTessFactor >> FXP_FRACTION_BITS) << 1) + 1;
   }
   int numFloorSegments = (fxpFloorHalfTessFactor * 2) >> FXP_FRACTION_BITS;
   int numCeilSegments = (fxpCeilHalfTessFactor * 2) >> FXP_FRACTION_BITS;
   if (odd) {
      numFloorSegments -= 1;
      numCeilSegments -= 1;
   }
   TessFactorCtx->fxpInvNumSegmentsOnFloorTessFactor =
      floatToFixed(1.0f / (float)numFloorSegments);
   TessFactorCtx->fxpInvNumSegmentsOnCeilTessFactor =
      floatToFixed(1.0f / (float)numCeilSegments);
}

/* The patch functions recompute TESS_FACTOR_CONTEXTs where they are used
 * instead of keeping one per edge and axis live through the whole patch:
 * that state is what made the tessellator spill.
 */
static inline int
TessFactorNumHalfPoints(bool odd, FXP fxpTessFactor)
{
   TESS_FACTOR_CONTEXT c;
   ComputeTessFactorCtx(odd, fxpTessFactor, &c);
   return c.numHalfTessFactorPoints;
}

static inline FXP
PlacePointIn1D(private const TESS_FACTOR_CONTEXT *TessFactorCtx, bool odd,
               int point)
{
   bool bFlip = point >= TessFactorCtx->numHalfTessFactorPoints;

   if (bFlip) {
      point = (TessFactorCtx->numHalfTessFactorPoints << 1) - point - odd;
   }

   // special casing middle since 16 bit fixed math below can't reproduce 0.5
   // exactly (single exit, see PatchIndexValue())
   bool middle = point == TessFactorCtx->numHalfTessFactorPoints;

   unsigned int indexOnCeilHalfTessFactor = point;
   unsigned int indexOnFloorHalfTessFactor = indexOnCeilHalfTessFactor;
   if (point > TessFactorCtx->splitPointOnFloorHalfTessFactor) {
      indexOnFloorHalfTessFactor -= 1;
   }
   // For the fixed point multiplies below, we know the results are <= 16 bits
   // because the locations on the halfTessFactor are <= half the number of
   // segments for the total TessFactor. So a number divided by a number that
   // is at least twice as big will give a result no bigger than 0.5 (which in
   // fixed point is 16 bits in our case)
   FXP fxpLocationOnFloorHalfTessFactor =
      indexOnFloorHalfTessFactor * TessFactorCtx->fxpInvNumSegmentsOnFloorTessFactor;
   FXP fxpLocationOnCeilHalfTessFactor =
      indexOnCeilHalfTessFactor * TessFactorCtx->fxpInvNumSegmentsOnCeilTessFactor;

   // Since we know the numbers calculated above are <= fixed point 0.5, and the
   // equation below is just lerping between two values <= fixed point 0.5
   // (0x00008000), then we know that the final result before shifting by 16 bits
   // is no larger than 0x80000000.  Once we shift that down by 16, we get the
   // result of lerping 2 numbers <= 0.5, which is obviously at most 0.5
   // (0x00008000)
   FXP fxpLocation =
      fxpLocationOnFloorHalfTessFactor * (FXP_ONE - TessFactorCtx->fxpHalfTessFactorFraction) +
      fxpLocationOnCeilHalfTessFactor * (TessFactorCtx->fxpHalfTessFactorFraction);
   fxpLocation = (fxpLocation + FXP_ONE_HALF /*round*/) >> FXP_FRACTION_BITS; // get back to n.16
   if (bFlip) {
      fxpLocation = FXP_ONE - fxpLocation;
   }
   return middle ? FXP_ONE_HALF : fxpLocation;
}

static inline void
StitchRegular(private struct CHWTessellator *ctx, bool bTrapezoid,
              DIAGONALS diagonals, int baseIndexOffset, int numInsideEdgePoints,
              int insideEdgePointBaseOffset, int outsideEdgePointBaseOffset)
{
   int insidePoint = insideEdgePointBaseOffset;
   int outsidePoint = outsideEdgePointBaseOffset;
   if (bTrapezoid) {
      DefineTriangle(ctx, outsidePoint, outsidePoint + 1, insidePoint,
                     baseIndexOffset);
      baseIndexOffset += 3;
      outsidePoint++;
   }
   int p;
   switch (diagonals) {
   case DIAGONALS_INSIDE_TO_OUTSIDE:
      // Diagonals pointing from inside edge forward towards outside edge
      for (p = 0; p < numInsideEdgePoints - 1; p++) {
         DefineTriangle(ctx, insidePoint, outsidePoint, outsidePoint + 1,
                        baseIndexOffset);
         baseIndexOffset += 3;

         DefineTriangle(ctx, insidePoint, outsidePoint + 1, insidePoint + 1,
                        baseIndexOffset);
         baseIndexOffset += 3;
         insidePoint++;
         outsidePoint++;
      }
      break;
   case DIAGONALS_INSIDE_TO_OUTSIDE_EXCEPT_MIDDLE: // Assumes ODD tessellation
      // Diagonals pointing from outside edge forward towards inside edge

      // First half
      for (p = 0; p < numInsideEdgePoints / 2 - 1; p++) {
         DefineTriangle(ctx, outsidePoint, outsidePoint + 1, insidePoint,
                        baseIndexOffset);
         baseIndexOffset += 3;
         DefineTriangle(ctx, insidePoint, outsidePoint + 1, insidePoint + 1,
                        baseIndexOffset);
         baseIndexOffset += 3;
         insidePoint++;
         outsidePoint++;
      }

      // Middle
      DefineTriangle(ctx, outsidePoint, insidePoint + 1, insidePoint,
                     baseIndexOffset);
      baseIndexOffset += 3;
      DefineTriangle(ctx, outsidePoint, outsidePoint + 1, insidePoint + 1,
                     baseIndexOffset);
      baseIndexOffset += 3;
      insidePoint++;
      outsidePoint++;
      p += 2;

      // Second half
      for (; p < numInsideEdgePoints; p++) {
         DefineTriangle(ctx, outsidePoint, outsidePoint + 1, insidePoint,
                        baseIndexOffset);
         baseIndexOffset += 3;
         DefineTriangle(ctx, insidePoint, outsidePoint + 1, insidePoint + 1,
                        baseIndexOffset);
         baseIndexOffset += 3;
         insidePoint++;
         outsidePoint++;
      }
      break;
   case DIAGONALS_MIRRORED:
      // First half, diagonals pointing from outside of outside edge to inside of
      // inside edge
      for (p = 0; p < numInsideEdgePoints / 2; p++) {
         DefineTriangle(ctx, outsidePoint, insidePoint + 1, insidePoint,
                        baseIndexOffset);
         baseIndexOffset += 3;
         DefineTriangle(ctx, outsidePoint, outsidePoint + 1, insidePoint + 1,
                        baseIndexOffset);
         baseIndexOffset += 3;
         insidePoint++;
         outsidePoint++;
      }
      // Second half, diagonals pointing from inside of inside edge to outside of
      // outside edge
      for (; p < numInsideEdgePoints - 1; p++) {
         DefineTriangle(ctx, insidePoint, outsidePoint, outsidePoint + 1,
                        baseIndexOffset);
         baseIndexOffset += 3;
         DefineTriangle(ctx, insidePoint, outsidePoint + 1, insidePoint + 1,
                        baseIndexOffset);
         baseIndexOffset += 3;
         insidePoint++;
         outsidePoint++;
      }
      break;
   }
   if (bTrapezoid) {
      DefineTriangle(ctx, outsidePoint, outsidePoint + 1, insidePoint,
                     baseIndexOffset);
      baseIndexOffset += 3;
   }
}

// loop_start and loop_end give optimal loop bounds for
// the stitching algorithm further below, for any given halfTssFactor. There
// is probably a better way to encode this...
//
// Return the FIRST entry in finalPointPositionTable awhich is less than
// halfTessFactor, except entry 0 and 1 which are set up to skip the loop.
static inline int
loop_start(int N)
{
   if (N < 2)
      return 1;
   else if (N == 2)
      return 17;
   else if (N < 5)
      return 9;
   else if (N < 9)
      return 5;
   else if (N < 17)
      return 3;
   else
      return 2;
}

// Return the LAST entry in finalPointPositionTable[] which is less than
// halfTessFactor, except entry 0 and 1 which are set up to skip the loop.
static int
loop_end(int N)
{
   if (N < 2)
      return 0;
   else if (N < 4)
      return 17;
   else if (N < 8)
      return 25;
   else if (N < 16)
      return 29;
   else if (N < 32)
      return 31;
   else
      return 32;
}

// Tables to assist in the stitching of 2 rows of points having arbitrary
// TessFactors. The stitching order is governed by Ruler Function vertex
// split ordering (see external documentation).
//
// The contents of the finalPointPositionTable are where vertex i [0..33]
// ends up on the half-edge at the max tessellation amount given
// ruler-function split order. Recall the other half of an edge is mirrored,
// so we only need to deal with one half. This table is used to decide when
// to advance a point on the interior or exterior. It supports odd TessFactor
// up to 65 and even TessFactor up to 64.

/* TODO: Is this actually faster than a LUT? */
static inline uint32_t
finalPointPositionTable(uint32_t x)
{
   /* Single exit, see PatchIndexValue(). */
   uint32_t shift;
   if ((x & 1) == 0) {
      shift = 1;
   } else if ((x & 3) == 3) {
      shift = 2;
   } else if ((x & 7) == 5) {
      shift = 3;
   } else if (x != 17) {
      shift = 4;
   } else {
      shift = 5;
   }

   // SWAR vectorized right-shift of (0x20, x)
   // We're calculating `min(0xf, 0x20 >> shift) + (x >> shift)`.
   uint32_t items_to_shift = x | (0x20 << 16);
   uint32_t shifted = items_to_shift >> shift;

   uint32_t bias = min(0xfu, shifted >> 16);
   return x == 0 ? 0 : x == 1 ? 0x20 : bias + (shifted & 0xffff);
}

static inline void
StitchTransition(private struct CHWTessellator *ctx, int baseIndexOffset,
                 int insideEdgePointBaseOffset,
                 int insideNumHalfTessFactorPoints,
                 bool insideEdgeTessFactorOdd, int outsideEdgePointBaseOffset,
                 int outsideNumHalfTessFactorPoints, bool outsideTessFactorOdd)
{
   if (insideEdgeTessFactorOdd) {
      insideNumHalfTessFactorPoints -= 1;
   }
   if (outsideTessFactorOdd) {
      outsideNumHalfTessFactorPoints -= 1;
   }
   // Walk first half
   int outsidePoint = outsideEdgePointBaseOffset;
   int insidePoint = insideEdgePointBaseOffset;

   // iStart,iEnd are a small optimization so the loop below doesn't have to go
   // from 0 up to 31
   int iStart = min(loop_start(insideNumHalfTessFactorPoints),
                    loop_start(outsideNumHalfTessFactorPoints));
   int iEnd = loop_end(
      max(insideNumHalfTessFactorPoints, outsideNumHalfTessFactorPoints));

   // since we don't start the loop at 0 below, we need a special case.
   if (0 < outsideNumHalfTessFactorPoints) {
      // Advance outside
      DefineTriangle(ctx, outsidePoint, outsidePoint + 1, insidePoint,
                     baseIndexOffset);
      baseIndexOffset += 3;
      outsidePoint++;
   }

   for (int i = iStart; i <= iEnd; i++) {
      int bound = finalPointPositionTable(i);

      if (bound < insideNumHalfTessFactorPoints) {
         // Advance inside
         DefineTriangle(ctx, insidePoint, outsidePoint, insidePoint + 1,
                        baseIndexOffset);
         baseIndexOffset += 3;
         insidePoint++;
      }
      if (bound < outsideNumHalfTessFactorPoints) {
         // Advance outside
         DefineTriangle(ctx, outsidePoint, outsidePoint + 1, insidePoint,
                        baseIndexOffset);
         baseIndexOffset += 3;
         outsidePoint++;
      }
   }

   if ((insideEdgeTessFactorOdd != outsideTessFactorOdd) ||
       insideEdgeTessFactorOdd) {
      if (insideEdgeTessFactorOdd == outsideTessFactorOdd) {
         // Quad in the middle
         DefineTriangle(ctx, insidePoint, outsidePoint, insidePoint + 1,
                        baseIndexOffset);
         baseIndexOffset += 3;
         DefineTriangle(ctx, insidePoint + 1, outsidePoint, outsidePoint + 1,
                        baseIndexOffset);
         baseIndexOffset += 3;
         insidePoint++;
         outsidePoint++;
      } else if (!insideEdgeTessFactorOdd) {
         // Triangle pointing inside
         DefineTriangle(ctx, insidePoint, outsidePoint, outsidePoint + 1,
                        baseIndexOffset);
         baseIndexOffset += 3;
         outsidePoint++;
      } else {
         // Triangle pointing outside
         DefineTriangle(ctx, insidePoint, outsidePoint, insidePoint + 1,
                        baseIndexOffset);
         baseIndexOffset += 3;
         insidePoint++;
      }
   }

   // Walk second half.
   for (int i = iEnd; i >= iStart; i--) {
      int bound = finalPointPositionTable(i);

      if (bound < outsideNumHalfTessFactorPoints) {
         // Advance outside
         DefineTriangle(ctx, outsidePoint, outsidePoint + 1, insidePoint,
                        baseIndexOffset);
         baseIndexOffset += 3;
         outsidePoint++;
      }
      if (bound < insideNumHalfTessFactorPoints) {
         // Advance inside
         DefineTriangle(ctx, insidePoint, outsidePoint, insidePoint + 1,
                        baseIndexOffset);
         baseIndexOffset += 3;
         insidePoint++;
      }
   }
   // Below case is not needed if we didn't optimize loop above and made it run
   // from 31 down to 0.
   if (0 < outsideNumHalfTessFactorPoints) {
      DefineTriangle(ctx, outsidePoint, outsidePoint + 1, insidePoint,
                     baseIndexOffset);
      baseIndexOffset += 3;
      outsidePoint++;
   }
}

static inline void
poly_tess_isoline_process(constant struct poly_tess_params *p, uint32_t patch,
                          enum poly_tess_mode mode)
{
   enum poly_tess_partitioning partitioning = p->partitioning;

   bool lineDensityOdd;
   bool lineDetailOdd;
   TESS_FACTOR_CONTEXT lineDensityTessFactorCtx;
   TESS_FACTOR_CONTEXT lineDetailTessFactorCtx;

   global float *factors = tess_factors(p, patch);
   float TessFactor_V_LineDensity = factors[0];
   float TessFactor_U_LineDetail = factors[1];

   // Is the patch culled? NaN will pass.
   if (!(TessFactor_V_LineDensity > 0) || !(TessFactor_U_LineDetail > 0)) {
      poly_draw_empty(p, mode, patch);
      return;
   }

   // Clamp edge TessFactors
   TessFactor_V_LineDensity =
      clamp(TessFactor_V_LineDensity,
            POLY_TESS_MIN_ISOLINE_DENSITY_TESSELLATION_FACTOR,
            POLY_TESS_MAX_ISOLINE_DENSITY_TESSELLATION_FACTOR);
   TessFactor_U_LineDetail =
      clamp_factor(TessFactor_U_LineDetail, partitioning, 0);

   // Process tessFactors
   if (partitioning == POLY_TESS_PARTITIONING_INTEGER) {
      lineDetailOdd = isOdd(TessFactor_U_LineDetail);
   } else {
      lineDetailOdd = (partitioning == POLY_TESS_PARTITIONING_FRACTIONAL_ODD);
   }

   FXP fxpTessFactor_U_LineDetail = floatToFixed(TessFactor_U_LineDetail);

   ComputeTessFactorCtx(lineDetailOdd, fxpTessFactor_U_LineDetail,
                        &lineDetailTessFactorCtx);
   int numPointsPerLine =
      NumPointsForTessFactor(lineDetailOdd, fxpTessFactor_U_LineDetail);

   TessFactor_V_LineDensity = ceil(TessFactor_V_LineDensity);
   lineDensityOdd = isOdd(TessFactor_V_LineDensity);
   FXP fxpTessFactor_V_LineDensity = floatToFixed(TessFactor_V_LineDensity);
   ComputeTessFactorCtx(lineDensityOdd, fxpTessFactor_V_LineDensity,
                        &lineDensityTessFactorCtx);

   // don't draw last line at V == 1.
   int numLines =
      NumPointsForTessFactor(lineDensityOdd, fxpTessFactor_V_LineDensity) - 1;

   /* Points */
   uint num_points = numPointsPerLine * numLines;
   if (mode != POLY_TESS_MODE_COUNT) {
      global struct poly_tess_point *points =
         poly_heap_alloc_points(p, patch, num_points);

      for (int line = 0, pointOffset = 0; line < numLines; line++) {
         FXP fxpV =
            PlacePointIn1D(&lineDensityTessFactorCtx, lineDensityOdd, line);

         for (int point = 0; point < numPointsPerLine; point++) {
            FXP fxpU =
               PlacePointIn1D(&lineDetailTessFactorCtx, lineDetailOdd, point);

            DefinePoint(&points[pointOffset++], fxpU, fxpV);
         }
      }
   }

   struct CHWTessellator ctx = {
      .mode = mode,
      .index_bias = patch * POLY_TES_PATCH_ID_STRIDE,
   };

   /* Connectivity */
   if (!p->points_mode) {
      uint num_indices = numLines * (numPointsPerLine - 1) * 2;
      ctx.Index = poly_draw(p, mode, true, patch, num_indices);

      if (mode == POLY_TESS_MODE_COUNT)
         return;

      for (int line = 0, pointOffset = 0, indexOffset = 0; line < numLines;
           line++) {
         pointOffset++;

         for (int point = 1; point < numPointsPerLine; point++) {
            DefineIndex(&ctx, pointOffset - 1, indexOffset++);
            DefineIndex(&ctx, pointOffset, indexOffset++);
            pointOffset++;
         }
      }
   } else {
      poly_draw_points(&ctx, p, patch, num_points);
   }
}

static inline void
poly_tess_tri_process(constant struct poly_tess_params *p, uint32_t patch,
                      enum poly_tess_mode mode)
{
   enum poly_tess_partitioning partitioning = p->partitioning;

   global float *factors = tess_factors(p, patch);
   float tessFactor_Ueq0 = factors[0];
   float tessFactor_Veq0 = factors[1];
   float tessFactor_Weq0 = factors[2];
   float insideTessFactor_f = factors[4];

   struct CHWTessellator ctx = {
      .mode = mode,
      .index_bias = patch * POLY_TES_PATCH_ID_STRIDE,
      .swap_winding = (p->ccw & POLY_TESS_CCW_INLINE) != 0,
   };

   // Is the patch culled? NaN will pass.
   if (!(tessFactor_Ueq0 > 0) || !(tessFactor_Veq0 > 0) ||
       !(tessFactor_Weq0 > 0)) {

      poly_draw_empty(p, mode, patch);

      return;
   }

   FXP outsideTessFactor[TRI_EDGES];
   FXP insideTessFactor;
   bool outsideTessFactorOdd[TRI_EDGES];
   bool insideTessFactorOdd;
   // Stuff below is just specific to the traversal order
   // this code happens to use to generate points/lines
   int numPointsForOutsideEdge[TRI_EDGES];
   int numPointsForInsideTessFactor;
   int insideEdgePointBaseOffset;

   // Clamp TessFactors
   tessFactor_Ueq0 = clamp_factor(tessFactor_Ueq0, partitioning, 0);
   tessFactor_Veq0 = clamp_factor(tessFactor_Veq0, partitioning, 0);
   tessFactor_Weq0 = clamp_factor(tessFactor_Weq0, partitioning, 0);

   float maxf = max(max(tessFactor_Ueq0, tessFactor_Veq0), tessFactor_Weq0);
   insideTessFactor_f = clamp_factor(insideTessFactor_f, partitioning, maxf);
   // Note the above clamps map NaN to the lower bound

   // Process tessFactors
   float outsideTessFactor_f[TRI_EDGES] = {tessFactor_Ueq0, tessFactor_Veq0,
                                           tessFactor_Weq0};
   if (partitioning == POLY_TESS_PARTITIONING_INTEGER) {
      for (int edge = 0; edge < TRI_EDGES; edge++) {
         outsideTessFactorOdd[edge] = isOdd(outsideTessFactor_f[edge]);
      }
      insideTessFactorOdd =
         isOdd(insideTessFactor_f) && (1.0f != insideTessFactor_f);
   } else {
      bool odd = (partitioning == POLY_TESS_PARTITIONING_FRACTIONAL_ODD);

      for (int edge = 0; edge < TRI_EDGES; edge++) {
         outsideTessFactorOdd[edge] = odd;
      }
      insideTessFactorOdd = odd;
   }

   /* The edge odd flags as bits: one register through the loops. */
   uint outsideOddBits = outsideTessFactorOdd[0] |
                         (outsideTessFactorOdd[1] << 1) |
                         (outsideTessFactorOdd[2] << 2);

   // Save fixed point TessFactors
   for (int edge = 0; edge < TRI_EDGES; edge++) {
      outsideTessFactor[edge] = floatToFixed(outsideTessFactor_f[edge]);
   }
   insideTessFactor = floatToFixed(insideTessFactor_f);

   if (partitioning != POLY_TESS_PARTITIONING_FRACTIONAL_EVEN) {
      // Special case if all TessFactors are 1
      if ((FXP_ONE == insideTessFactor) &&
          (FXP_ONE == outsideTessFactor[Ueq0]) &&
          (FXP_ONE == outsideTessFactor[Veq0]) &&
          (FXP_ONE == outsideTessFactor[Weq0])) {

         /* Just do minimum tess factor */
         if (mode == POLY_TESS_MODE_COUNT) {
            p->counts[patch] = 3;
            return;
         }

         global struct poly_tess_point *points =
            poly_heap_alloc_points(p, patch, 3);

         DefinePoint(&points[0], 0,
                     FXP_ONE);          // V=1 (beginning of Ueq0 edge VW)
         DefinePoint(&points[1], 0, 0); // W=1 (beginning of Veq0 edge WU)
         DefinePoint(&points[2], FXP_ONE,
                     0); // U=1 (beginning of Weq0 edge UV)

         if (!p->points_mode) {
            ctx.Index = poly_draw(p, mode, false, patch, 3);

            DefineTriangle(&ctx, 0, 1, 2,
                           /*indexStorageBaseOffset*/ 0);
         } else {
            poly_draw_points(&ctx, p, patch, 3);
         }

         return;
      }
   }

   // Per-TessFactor metadata (TESS_FACTOR_CONTEXT) is computed where it is
   // used, see TessFactorNumHalfPoints().

   // Compute some initial data.
   int NumPoints = 0;

   // outside edge offsets and storage
   for (int edge = 0; edge < TRI_EDGES; edge++) {
      numPointsForOutsideEdge[edge] = NumPointsForTessFactor(
         outsideTessFactorOdd[edge], outsideTessFactor[edge]);
      NumPoints += numPointsForOutsideEdge[edge];
   }
   NumPoints -= 3;

   // inside edge offsets
   numPointsForInsideTessFactor =
      NumPointsForTessFactor(insideTessFactorOdd, insideTessFactor);
   {
      int pointCountMin = insideTessFactorOdd ? 4 : 3;
      // max() allows degenerate transition regions when inside TessFactor == 1
      numPointsForInsideTessFactor =
         max(pointCountMin, numPointsForInsideTessFactor);
   }

   insideEdgePointBaseOffset = NumPoints;

   // inside storage, including interior edges above
   {
      int interiorRings = (numPointsForInsideTessFactor >> 1) - 1;
      int even = insideTessFactorOdd ? 0 : 1;
      NumPoints += TRI_EDGES * (interiorRings * (interiorRings + even)) + even;
   }

   /* GENERATE POINTS */
   if (mode != POLY_TESS_MODE_COUNT) {
      ctx.Point = poly_heap_alloc_points(p, patch, NumPoints);

      // Generate exterior ring edge points, clockwise starting from point V
      // (VW, the U==0 edge)
      int pointOffset = 0;
      for (int edge = 0; edge < TRI_EDGES; edge++) {
         int odd = edge & 0x1;
         bool edgeOdd = ((outsideOddBits >> edge) & 1);
         FXP edgeFactor = TESS_SEL3(outsideTessFactor, edge, );
         int endPoint = NumPointsForTessFactor(edgeOdd, edgeFactor) - 1;
         TESS_FACTOR_CONTEXT edgeCtx;
         ComputeTessFactorCtx(edgeOdd, edgeFactor, &edgeCtx);
         // don't include end, since next edge starts with it.
         for (int p = 0; p < endPoint; p++, pointOffset++) {
            // whether to reverse point order given we are defining V or U (W
            // implicit): edge0, VW, has V decreasing, so reverse 1D points
            // below edge1, WU, has U increasing, so don't reverse 1D points
            // below edge2, UV, has U decreasing, so reverse 1D points below
            int q = odd ? p : endPoint - p;

            FXP fxpParam = PlacePointIn1D(&edgeCtx, edgeOdd, q);
            DefinePoint(&ctx.Point[pointOffset], (edge == 0) ? 0 : fxpParam,
                        (edge == 0)   ? fxpParam
                        : (edge == 2) ? FXP_ONE - fxpParam
                                      : 0);
         }
      }

      // Generate interior ring points, clockwise spiralling in
      TESS_FACTOR_CONTEXT insideTessFactorCtx;
      ComputeTessFactorCtx(insideTessFactorOdd, insideTessFactor,
                           &insideTessFactorCtx);
      int numRings = (numPointsForInsideTessFactor >> 1);
      for (int ring = 1; ring < numRings; ring++) {
         int startPoint = ring;
         int endPoint = numPointsForInsideTessFactor - 1 - startPoint;

         int perpendicularAxisPoint = startPoint;
         FXP fxpPerpParam = PlacePointIn1D(
            &insideTessFactorCtx, insideTessFactorOdd, perpendicularAxisPoint);

         // Map location to the right size in
         // barycentric space. We know this fixed
         // point math won't over/underflow
         fxpPerpParam *= FXP_TWO_THIRDS;
         fxpPerpParam = (fxpPerpParam + FXP_ONE_HALF /*round*/) >>
                        FXP_FRACTION_BITS; // get back to n.16

         for (int edge = 0; edge < TRI_EDGES; edge++) {
            int odd = edge & 0x1;

            // don't include end: next edge starts with it.
            for (int p = startPoint; p < endPoint; p++, pointOffset++) {
               // whether to reverse point given we are defining V or U (W
               // implicit): edge0, VW, has V decreasing, so reverse 1D points
               // below edge1, WU, has U increasing, so don't reverse 1D points
               // below edge2, UV, has U decreasing, so reverse 1D points below
               int q = odd ? p : endPoint - (p - startPoint);

               FXP fxpParam =
                  PlacePointIn1D(&insideTessFactorCtx, insideTessFactorOdd, q);
               // edge0 VW, has perpendicular parameter U constant
               // edge1 WU, has perpendicular parameter V constant
               // edge2 UV, has perpendicular parameter W constant
               // reciprocal is the rate of change of edge-parallel parameters
               // as they are pushed into the triangle
               const unsigned int deriv = 2;

               // we know this fixed point math won't over/underflow
               FXP tmp = fxpParam - (fxpPerpParam + 1 /*round*/) / deriv;

               DefinePoint(&ctx.Point[pointOffset],
                           edge > 0 ? tmp : fxpPerpParam,
                           edge == 0   ? tmp
                           : edge == 1 ? fxpPerpParam
                                       : FXP_ONE - tmp - fxpPerpParam);
            }
         }
      }
      if (!insideTessFactorOdd) {
         // Last point is the point at the center.
         DefinePoint(&ctx.Point[pointOffset], FXP_ONE_THIRD, FXP_ONE_THIRD);
      }
   }

   if (p->points_mode) {
      poly_draw_points(&ctx, p, patch, NumPoints);
      return;
   }

   {
      // Generate primitives for all the concentric rings, one side at a time
      // for each ring +1 is so even tess includes the center point, which we
      // want to now
      int numRings = ((numPointsForInsideTessFactor + 1) >> 1);

      int NumIndices = 0;
      {
         int OuterPoints = numPointsForOutsideEdge[0] +
                           numPointsForOutsideEdge[1] +
                           numPointsForOutsideEdge[2];

         int numRings18 = numRings * 18;
         NumIndices = ((numRings18 - 27) * numPointsForInsideTessFactor) +
                      (3 * OuterPoints) - (numRings18 * (numRings - 1)) +
                      (insideTessFactorOdd ? 3 : 0);
      }

      // Generate the draw and allocate the index buffer now that we know the size
      ctx.Index = poly_draw(p, mode, false, patch, NumIndices);

      if (mode == POLY_TESS_MODE_COUNT)
         return;

      int insideOffset = insideEdgePointBaseOffset;
      int outsideEdgePointBaseOffset = 0;

      NumIndices = 0;
      for (int ring = 1; ring < numRings; ring++) {
         int numPointsForInsideEdge = numPointsForInsideTessFactor - 2 * ring;
         int edge0InsidePointBaseOffset = insideOffset;
         int edge0OutsidePointBaseOffset = outsideEdgePointBaseOffset;
         for (int edge = 0; edge < TRI_EDGES; edge++) {
            bool edgeOdd = ((outsideOddBits >> edge) & 1);
            FXP edgeFactor = TESS_SEL3(outsideTessFactor, edge, );
            int outsidePoints = ring == 1
                                   ? NumPointsForTessFactor(edgeOdd, edgeFactor)
                                   : (numPointsForInsideEdge + 2);

            int numTriangles = numPointsForInsideEdge + outsidePoints - 2;

            int insideBaseOffset;
            int outsideBaseOffset;
            if (edge == 2) {
               ctx.IndexPatchCtx.insidePointIndexDeltaToRealValue =
                  insideOffset;
               ctx.IndexPatchCtx.insidePointIndexBadValue =
                  numPointsForInsideEdge - 1;
               ctx.IndexPatchCtx.insidePointIndexReplacementValue =
                  edge0InsidePointBaseOffset;
               ctx.IndexPatchCtx.outsidePointIndexPatchBase =
                  ctx.IndexPatchCtx.insidePointIndexBadValue +
                  1; // past inside patched index range
               ctx.IndexPatchCtx.outsidePointIndexDeltaToRealValue =
                  outsideEdgePointBaseOffset -
                  ctx.IndexPatchCtx.outsidePointIndexPatchBase;
               ctx.IndexPatchCtx.outsidePointIndexBadValue =
                  ctx.IndexPatchCtx.outsidePointIndexPatchBase + outsidePoints -
                  1;
               ctx.IndexPatchCtx.outsidePointIndexReplacementValue =
                  edge0OutsidePointBaseOffset;
               ctx.bUsingPatchedIndices = true;
               insideBaseOffset = 0;
               outsideBaseOffset = ctx.IndexPatchCtx.outsidePointIndexPatchBase;
            } else {
               insideBaseOffset = insideOffset;
               outsideBaseOffset = outsideEdgePointBaseOffset;
            }
            TESS_STITCH(ctx, if (ring == 1) {
               StitchTransition(
                  &ctx, /*baseIndexOffset: */ NumIndices, insideBaseOffset,
                  TessFactorNumHalfPoints(insideTessFactorOdd,
                                          insideTessFactor),
                  insideTessFactorOdd, outsideBaseOffset,
                  TessFactorNumHalfPoints(edgeOdd, edgeFactor), edgeOdd);
            } else {
               StitchRegular(&ctx, /*bTrapezoid*/ true, DIAGONALS_MIRRORED,
                             /*baseIndexOffset: */ NumIndices,
                             numPointsForInsideEdge, insideBaseOffset,
                             outsideBaseOffset);
            });
            ResetIndexPatch(&ctx);
            NumIndices += numTriangles * 3;
            outsideEdgePointBaseOffset += outsidePoints - 1;
            insideOffset += numPointsForInsideEdge - 1;
         }
      }
      if (insideTessFactorOdd) {
         // Triangulate center (a single triangle)
         DefineTriangle(&ctx, outsideEdgePointBaseOffset,
                        outsideEdgePointBaseOffset + 1,
                        outsideEdgePointBaseOffset + 2, NumIndices);
         NumIndices += 3;
      }
   }
}

static inline void
poly_tess_quad_process(constant struct poly_tess_params *p, uint32_t patch,
                       enum poly_tess_mode mode)
{
   enum poly_tess_partitioning partitioning = p->partitioning;
   global float *factors = tess_factors(p, patch);

   float tessFactor_Ueq0 = factors[0];
   float tessFactor_Veq0 = factors[1];
   float tessFactor_Ueq1 = factors[2];
   float tessFactor_Veq1 = factors[3];

   float insideTessFactor_U = factors[4];
   float insideTessFactor_V = factors[5];

   struct CHWTessellator ctx = {
      .mode = mode,
      .index_bias = patch * POLY_TES_PATCH_ID_STRIDE,
      .swap_winding = (p->ccw & POLY_TESS_CCW_INLINE) != 0,
   };

   // Is the patch culled?
   if (!(tessFactor_Ueq0 > 0) || // NaN will pass
       !(tessFactor_Veq0 > 0) || !(tessFactor_Ueq1 > 0) ||
       !(tessFactor_Veq1 > 0)) {
      poly_draw_empty(p, mode, patch);
      return;
   }

   FXP outsideTessFactor[QUAD_EDGES];
   FXP insideTessFactor[QUAD_AXES];
   bool outsideTessFactorOdd[QUAD_EDGES];
   bool insideTessFactorOdd[QUAD_AXES];
   // Stuff below is just specific to the traversal order
   // this code happens to use to generate points/lines
   int numPointsForOutsideEdge[QUAD_EDGES];
   int numPointsForInsideTessFactor[QUAD_AXES];
   int insideEdgePointBaseOffset;

   // Clamp edge TessFactors
   tessFactor_Ueq0 = clamp_factor(tessFactor_Ueq0, partitioning, 0);
   tessFactor_Veq0 = clamp_factor(tessFactor_Veq0, partitioning, 0);
   tessFactor_Ueq1 = clamp_factor(tessFactor_Ueq1, partitioning, 0);
   tessFactor_Veq1 = clamp_factor(tessFactor_Veq1, partitioning, 0);

   float maxf = max(max(max(tessFactor_Ueq0, tessFactor_Veq0),
                        max(tessFactor_Ueq1, tessFactor_Veq1)),
                    max(insideTessFactor_U, insideTessFactor_V));

   insideTessFactor_U = clamp_factor(insideTessFactor_U, partitioning, maxf);
   insideTessFactor_V = clamp_factor(insideTessFactor_V, partitioning, maxf);
   // Note the above clamps map NaN to lowerBound

   // Process tessFactors
   float outsideTessFactor_f[QUAD_EDGES] = {tessFactor_Ueq0, tessFactor_Veq0,
                                            tessFactor_Ueq1, tessFactor_Veq1};
   float insideTessFactor_f[QUAD_AXES] = {insideTessFactor_U,
                                          insideTessFactor_V};
   if (partitioning == POLY_TESS_PARTITIONING_INTEGER) {
      for (int edge = 0; edge < QUAD_EDGES; edge++) {
         outsideTessFactorOdd[edge] = isOdd(outsideTessFactor_f[edge]);
      }
      for (int axis = 0; axis < QUAD_AXES; axis++) {
         insideTessFactorOdd[axis] = isOdd(insideTessFactor_f[axis]) &&
                                     (1.0f != insideTessFactor_f[axis]);
      }
   } else {
      bool odd = (partitioning == POLY_TESS_PARTITIONING_FRACTIONAL_ODD);

      for (int edge = 0; edge < QUAD_EDGES; edge++) {
         outsideTessFactorOdd[edge] = odd;
      }
      insideTessFactorOdd[U] = insideTessFactorOdd[V] = odd;
   }

   /* The odd flags as bits (edges 0-3, axes 4-5): one register through the
    * loops instead of six (see TESS_SEL*). */
   uint oddBits = outsideTessFactorOdd[0] | (outsideTessFactorOdd[1] << 1) |
                  (outsideTessFactorOdd[2] << 2) |
                  (outsideTessFactorOdd[3] << 3) |
                  (insideTessFactorOdd[U] << 4) | (insideTessFactorOdd[V] << 5);

   // Save fixed point TessFactors
   for (int edge = 0; edge < QUAD_EDGES; edge++) {
      outsideTessFactor[edge] = floatToFixed(outsideTessFactor_f[edge]);
   }
   for (int axis = 0; axis < QUAD_AXES; axis++) {
      insideTessFactor[axis] = floatToFixed(insideTessFactor_f[axis]);
   }

   if (partitioning != POLY_TESS_PARTITIONING_FRACTIONAL_EVEN) {
      // Special case if all TessFactors are 1
      if ((FXP_ONE == insideTessFactor[U]) &&
          (FXP_ONE == insideTessFactor[V]) &&
          (FXP_ONE == outsideTessFactor[Ueq0]) &&
          (FXP_ONE == outsideTessFactor[Veq0]) &&
          (FXP_ONE == outsideTessFactor[Ueq1]) &&
          (FXP_ONE == outsideTessFactor[Veq1])) {

         /* Just do minimum tess factor */
         if (!p->points_mode) {
            ctx.Index = poly_draw(p, mode, false, patch, 6);
            if (mode == POLY_TESS_MODE_COUNT)
               return;

            DefineTriangle(&ctx, 0, 1, 3, /*indexStorageOffset*/ 0);
            DefineTriangle(&ctx, 1, 2, 3, /*indexStorageOffset*/ 3);
         } else {
            poly_draw_points(&ctx, p, patch, 4);
            if (mode == POLY_TESS_MODE_COUNT)
               return;
         }

         global struct poly_tess_point *points =
            poly_heap_alloc_points(p, patch, 4);

         DefinePoint(&points[0], 0, 0);
         DefinePoint(&points[1], FXP_ONE, 0);
         DefinePoint(&points[2], FXP_ONE, FXP_ONE);
         DefinePoint(&points[3], 0, FXP_ONE);
         return;
      }
   }

   // TessFactor-specific metadata (TESS_FACTOR_CONTEXT) is computed where it
   // is used, see TessFactorNumHalfPoints().

   int NumPoints = 0;

   // outside edge offsets and storage
   for (int edge = 0; edge < QUAD_EDGES; edge++) {
      numPointsForOutsideEdge[edge] = NumPointsForTessFactor(
         outsideTessFactorOdd[edge], outsideTessFactor[edge]);
      NumPoints += numPointsForOutsideEdge[edge];
   }
   NumPoints -= 4;

   // inside edge offsets
   for (int axis = 0; axis < QUAD_AXES; axis++) {
      numPointsForInsideTessFactor[axis] = NumPointsForTessFactor(
         insideTessFactorOdd[axis], insideTessFactor[axis]);
      int pointCountMin = insideTessFactorOdd[axis] ? 4 : 3;
      // max() allows degenerate transition regions when inside TessFactor == 1
      numPointsForInsideTessFactor[axis] =
         max(pointCountMin, numPointsForInsideTessFactor[axis]);
   }

   insideEdgePointBaseOffset = NumPoints;

   // inside storage, including interior edges above
   int numInteriorPoints = (numPointsForInsideTessFactor[U] - 2) *
                           (numPointsForInsideTessFactor[V] - 2);
   NumPoints += numInteriorPoints;

   if (mode != POLY_TESS_MODE_COUNT) {
      ctx.Point = poly_heap_alloc_points(p, patch, NumPoints);

      // Generate exterior ring edge points, clockwise from top-left
      int pointOffset = 0;
      for (int edge = 0; edge < QUAD_EDGES; edge++) {
         int odd = edge & 0x1;
         // don't include end, since next edge starts with it.
         bool edgeOdd = ((oddBits >> edge) & 1);
         FXP edgeFactor = TESS_SEL4(outsideTessFactor, edge, );
         int endPoint = NumPointsForTessFactor(edgeOdd, edgeFactor) - 1;
         TESS_FACTOR_CONTEXT edgeCtx;
         ComputeTessFactorCtx(edgeOdd, edgeFactor, &edgeCtx);
         for (int p = 0; p < endPoint; p++, pointOffset++) {
            int q =
               ((edge == 1) || (edge == 2)) ? p : endPoint - p; // reverse order
            FXP fxpParam = PlacePointIn1D(&edgeCtx, edgeOdd, q);

            FXP u = odd ? fxpParam : ((edge == 2) ? FXP_ONE : 0);
            FXP v = odd ? ((edge == 3) ? FXP_ONE : 0) : fxpParam;
            DefinePoint(&ctx.Point[pointOffset], u, v);
         }
      }

      // Generate interior ring points, clockwise from (U==0,V==1) (bottom-left)
      // spiralling toward center
      int minNumPointsForTessFactor =
         min(numPointsForInsideTessFactor[U], numPointsForInsideTessFactor[V]);
      // note for even tess we aren't counting center point here.
      int numRings = (minNumPointsForTessFactor >> 1);

      for (int ring = 1; ring < numRings; ring++) {
         int startPoint = ring;
         int endPoint[QUAD_AXES] = {
            numPointsForInsideTessFactor[U] - 1 - startPoint,
            numPointsForInsideTessFactor[V] - 1 - startPoint,
         };

         for (int edge = 0; edge < QUAD_EDGES; edge++) {
            int odd0 = edge & 0x1, odd1 = (edge + 1) & 0x1;
            bool perpOdd = ((oddBits >> (4 + odd0)) & 1);
            bool edgeOdd = ((oddBits >> (4 + odd1)) & 1);
            TESS_FACTOR_CONTEXT perpCtx, edgeCtx;
            ComputeTessFactorCtx(perpOdd, TESS_SEL2(insideTessFactor, odd0, ),
                                 &perpCtx);
            ComputeTessFactorCtx(edgeOdd, TESS_SEL2(insideTessFactor, odd1, ),
                                 &edgeCtx);
            int perpendicularAxisPoint =
               (edge < 2) ? startPoint : TESS_SEL2(endPoint, odd0, );
            FXP fxpPerpParam =
               PlacePointIn1D(&perpCtx, perpOdd, perpendicularAxisPoint);
            int edgeEnd = TESS_SEL2(endPoint, odd1, );

            for (int p = startPoint; p < edgeEnd; p++,
                     pointOffset++) // don't include end: next edge starts with
                                    // it.
            {
               bool odd_ = odd1;
               int q = ((edge == 1) || (edge == 2))
                          ? p
                          : edgeEnd - (p - startPoint);
               FXP fxpParam = PlacePointIn1D(&edgeCtx, edgeOdd, q);
               DefinePoint(&ctx.Point[pointOffset],
                           odd_ ? fxpPerpParam : fxpParam,
                           odd_ ? fxpParam : fxpPerpParam);
            }
         }
      }
      // For even tessellation, the inner "ring" is degenerate - a row of points
      if ((numPointsForInsideTessFactor[U] > numPointsForInsideTessFactor[V]) &&
          !((oddBits >> 5) & 1)) {
         int startPoint = numRings;
         int endPoint = numPointsForInsideTessFactor[U] - 1 - startPoint;
         TESS_FACTOR_CONTEXT ctxU;
         ComputeTessFactorCtx(((oddBits >> 4) & 1), insideTessFactor[U],
                              &ctxU);
         for (int p = startPoint; p <= endPoint; p++, pointOffset++) {
            FXP fxpParam = PlacePointIn1D(&ctxU, ((oddBits >> 4) & 1), p);
            DefinePoint(&ctx.Point[pointOffset], fxpParam, FXP_ONE_HALF);
         }
      } else if ((numPointsForInsideTessFactor[V] >=
                  numPointsForInsideTessFactor[U]) &&
                 !((oddBits >> 4) & 1)) {
         int startPoint = numRings;
         int endPoint = numPointsForInsideTessFactor[V] - 1 - startPoint;
         TESS_FACTOR_CONTEXT ctxV;
         ComputeTessFactorCtx(((oddBits >> 5) & 1), insideTessFactor[V],
                              &ctxV);
         for (int p = endPoint; p >= startPoint; p--, pointOffset++) {
            FXP fxpParam = PlacePointIn1D(&ctxV, ((oddBits >> 5) & 1), p);
            DefinePoint(&ctx.Point[pointOffset], FXP_ONE_HALF, fxpParam);
         }
      }
   }

   if (p->points_mode) {
      poly_draw_points(&ctx, p, patch, NumPoints);
      return;
   }

   /* CONNECTIVITY */
   {
      // Generate primitives for all the concentric rings, one side at a time
      // for each ring. +1 is so even tess includes the center point
      int numPointRowsToCenter[QUAD_AXES] = {
         (numPointsForInsideTessFactor[U] + 1) >> 1,
         (numPointsForInsideTessFactor[V] + 1) >> 1,
      };

      int numRings = min(numPointRowsToCenter[U], numPointRowsToCenter[V]);

      /* Calculate # of indices so we can allocate */
      {
         /* Handle main case */
         int OuterPoints =
            numPointsForOutsideEdge[0] + numPointsForOutsideEdge[1] +
            numPointsForOutsideEdge[2] + numPointsForOutsideEdge[3];

         int InnerPoints =
            numPointsForInsideTessFactor[U] + numPointsForInsideTessFactor[V];

         int NumIndices = (OuterPoints * 3) + (12 * numRings * InnerPoints) -
                          (InnerPoints * 18) - (24 * numRings * (numRings - 1));

         /* Determine major/minor axes */
         bool U_major =
            (numPointsForInsideTessFactor[U] > numPointsForInsideTessFactor[V]);
         unsigned M = U_major ? U : V;
         unsigned m = U_major ? V : U;

         /* Handle degenerate ring */
         if (((oddBits >> (4 + m)) & 1)) {
            NumIndices +=
               12 * ((TESS_SEL2(numPointsForInsideTessFactor, M, ) >> 1) -
                     (TESS_SEL2(numPointsForInsideTessFactor, m, ) >> 1));
            NumIndices += (((oddBits >> (4 + M)) & 1) ? 6 : 12);
         }

         // Generate the draw and allocate the index buffer with the size
         ctx.Index = poly_draw(p, mode, false, patch, NumIndices);
      }

      if (mode == POLY_TESS_MODE_COUNT)
         return;

      int degeneratePointRing[QUAD_AXES] = {
         // Even partitioning causes degenerate row of points,
         // which results in exceptions to the point ordering conventions
         // when travelling around the rings counterclockwise.
         !((oddBits >> 5) & 1) ? numPointRowsToCenter[V] - 1 : -1,
         !((oddBits >> 4) & 1) ? numPointRowsToCenter[U] - 1 : -1,
      };

      int insideEdgePointBaseOffset_ = insideEdgePointBaseOffset;
      int outsideEdgePointBaseOffset = 0;

      int NumIndices = 0;

      for (int ring = 1; ring < numRings; ring++) {
         int numPointsForInsideEdge[QUAD_AXES] = {
            numPointsForInsideTessFactor[U] - 2 * ring,
            numPointsForInsideTessFactor[V] - 2 * ring};

         int edge0InsidePointBaseOffset = insideEdgePointBaseOffset_;
         int edge0OutsidePointBaseOffset = outsideEdgePointBaseOffset;

         for (int edge = 0; edge < QUAD_EDGES; edge++) {
            int odd = (edge + 1) & 0x1;
            int insideEdgePoints = TESS_SEL2(numPointsForInsideEdge, odd, );
            bool edgeOdd = ((oddBits >> edge) & 1);
            FXP edgeFactor = TESS_SEL4(outsideTessFactor, edge, );
            /* The outside row of a ring is the inside row of the one before. */
            int outsideEdgePoints = ring == 1
                                       ? NumPointsForTessFactor(edgeOdd, edgeFactor)
                                       : insideEdgePoints + 2;
            bool degenerateRing = ring == TESS_SEL2(degeneratePointRing, odd, );

            int numTriangles = insideEdgePoints + outsideEdgePoints - 2;
            int insideBaseOffset;
            int outsideBaseOffset;

            // We need to patch the indexing so Stitch() can think it sees 2
            // sequentially increasing rows of points, even though we have
            // wrapped around to the end of the inner and outer ring's points,
            // so the last point is really the first point for the ring. We make
            // it so that when Stitch() calls AddIndex(), that function will do
            // any necessary index adjustment.
            if (edge == 3) {
               if (degenerateRing) {
                  ctx.IndexPatchCtx2.baseIndexToInvert =
                     insideEdgePointBaseOffset_ + 1;
                  ctx.IndexPatchCtx2.cornerCaseBadValue =
                     outsideEdgePointBaseOffset + outsideEdgePoints - 1;
                  ctx.IndexPatchCtx2.cornerCaseReplacementValue =
                     edge0OutsidePointBaseOffset;
                  ctx.IndexPatchCtx2.indexInversionEndPoint =
                     (ctx.IndexPatchCtx2.baseIndexToInvert << 1) - 1;
                  insideBaseOffset = ctx.IndexPatchCtx2.baseIndexToInvert;
                  outsideBaseOffset = outsideEdgePointBaseOffset;
                  ctx.bUsingPatchedIndices2 = true;
               } else {
                  ctx.IndexPatchCtx.insidePointIndexDeltaToRealValue =
                     insideEdgePointBaseOffset_;
                  ctx.IndexPatchCtx.insidePointIndexBadValue =
                     insideEdgePoints - 1;
                  ctx.IndexPatchCtx.insidePointIndexReplacementValue =
                     edge0InsidePointBaseOffset;
                  ctx.IndexPatchCtx.outsidePointIndexPatchBase =
                     ctx.IndexPatchCtx.insidePointIndexBadValue +
                     1; // past inside patched index range
                  ctx.IndexPatchCtx.outsidePointIndexDeltaToRealValue =
                     outsideEdgePointBaseOffset -
                     ctx.IndexPatchCtx.outsidePointIndexPatchBase;
                  ctx.IndexPatchCtx.outsidePointIndexBadValue =
                     ctx.IndexPatchCtx.outsidePointIndexPatchBase +
                     outsideEdgePoints - 1;
                  ctx.IndexPatchCtx.outsidePointIndexReplacementValue =
                     edge0OutsidePointBaseOffset;

                  insideBaseOffset = 0;
                  outsideBaseOffset =
                     ctx.IndexPatchCtx.outsidePointIndexPatchBase;
                  ctx.bUsingPatchedIndices = true;
               }
            } else if ((edge == 2) && degenerateRing) {
               ctx.IndexPatchCtx2.baseIndexToInvert =
                  insideEdgePointBaseOffset_;
               ctx.IndexPatchCtx2.cornerCaseBadValue = -1;         // unused
               ctx.IndexPatchCtx2.cornerCaseReplacementValue = -1; // unused
               ctx.IndexPatchCtx2.indexInversionEndPoint =
                  ctx.IndexPatchCtx2.baseIndexToInvert << 1;
               insideBaseOffset = ctx.IndexPatchCtx2.baseIndexToInvert;
               outsideBaseOffset = outsideEdgePointBaseOffset;
               ctx.bUsingPatchedIndices2 = true;
            } else {
               insideBaseOffset = insideEdgePointBaseOffset_;
               outsideBaseOffset = outsideEdgePointBaseOffset;
            }
            TESS_STITCH(ctx, if (ring == 1) {
               StitchTransition(
                  &ctx, /*baseIndexOffset: */ NumIndices, insideBaseOffset,
                  TessFactorNumHalfPoints(((oddBits >> (4 + odd)) & 1),
                                          TESS_SEL2(insideTessFactor, odd, )),
                  ((oddBits >> (4 + odd)) & 1), outsideBaseOffset,
                  TessFactorNumHalfPoints(edgeOdd, edgeFactor), edgeOdd);
            } else {
               StitchRegular(&ctx, /*bTrapezoid*/ true, DIAGONALS_MIRRORED,
                             /*baseIndexOffset: */ NumIndices,
                             insideEdgePoints, insideBaseOffset,
                             outsideBaseOffset);
            });
            ResetIndexPatch(&ctx);
            NumIndices += numTriangles * 3;
            outsideEdgePointBaseOffset += outsideEdgePoints - 1;
            if ((edge == 2) && degenerateRing) {
               insideEdgePointBaseOffset_ -= insideEdgePoints - 1;
            } else {
               insideEdgePointBaseOffset_ += insideEdgePoints - 1;
            }
         }
      }

      // Triangulate center - a row of quads if odd
      // This triangulation may be producing diagonals that are asymmetric about
      // the center of the patch in this region.
      if ((numPointsForInsideTessFactor[U] > numPointsForInsideTessFactor[V]) &&
          ((oddBits >> 5) & 1)) {
         ctx.bUsingPatchedIndices2 = true;
         int stripNumQuads = (((numPointsForInsideTessFactor[U] >> 1) -
                               (numPointsForInsideTessFactor[V] >> 1))
                              << 1) +
                             (((oddBits >> 4) & 1) ? 1 : 2);
         ctx.IndexPatchCtx2.baseIndexToInvert =
            outsideEdgePointBaseOffset + stripNumQuads + 2;
         ctx.IndexPatchCtx2.cornerCaseBadValue =
            ctx.IndexPatchCtx2.baseIndexToInvert;
         ctx.IndexPatchCtx2.cornerCaseReplacementValue =
            outsideEdgePointBaseOffset;
         ctx.IndexPatchCtx2.indexInversionEndPoint =
            ctx.IndexPatchCtx2.baseIndexToInvert +
            ctx.IndexPatchCtx2.baseIndexToInvert + stripNumQuads;
         StitchRegular(
            &ctx, /*bTrapezoid*/ false, DIAGONALS_INSIDE_TO_OUTSIDE,
            /*baseIndexOffset: */ NumIndices,
            /*numInsideEdgePoints:*/ stripNumQuads + 1,
            /*insideEdgePointBaseOffset*/ ctx.IndexPatchCtx2.baseIndexToInvert,
            outsideEdgePointBaseOffset + 1);
         ctx.bUsingPatchedIndices2 = false;
         NumIndices += stripNumQuads * 6;
      } else if ((numPointsForInsideTessFactor[V] >=
                  numPointsForInsideTessFactor[U]) &&
                 ((oddBits >> 4) & 1)) {
         ctx.bUsingPatchedIndices2 = true;
         int stripNumQuads = (((numPointsForInsideTessFactor[V] >> 1) -
                               (numPointsForInsideTessFactor[U] >> 1))
                              << 1) +
                             (((oddBits >> 5) & 1) ? 1 : 2);
         ctx.IndexPatchCtx2.baseIndexToInvert =
            outsideEdgePointBaseOffset + stripNumQuads + 1;
         ctx.IndexPatchCtx2.cornerCaseBadValue = -1; // unused
         ctx.IndexPatchCtx2.indexInversionEndPoint =
            ctx.IndexPatchCtx2.baseIndexToInvert +
            ctx.IndexPatchCtx2.baseIndexToInvert + stripNumQuads;
         DIAGONALS diag = ((oddBits >> 5) & 1)
                             ? DIAGONALS_INSIDE_TO_OUTSIDE_EXCEPT_MIDDLE
                             : DIAGONALS_INSIDE_TO_OUTSIDE;
         StitchRegular(
            &ctx, /*bTrapezoid*/ false, diag,
            /*baseIndexOffset: */ NumIndices,
            /*numInsideEdgePoints:*/ stripNumQuads + 1,
            /*insideEdgePointBaseOffset*/ ctx.IndexPatchCtx2.baseIndexToInvert,
            outsideEdgePointBaseOffset);
         ctx.bUsingPatchedIndices2 = false;
         NumIndices += stripNumQuads * 6;
      }
   }
}
