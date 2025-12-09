/*
 * Copyright 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <assert.h>
#include <stdint.h>
#include "util/macros.h"

/*
 * Represents a boolean lookup table in sum-of-minterms form. These are
 * natural encodings, matching the Intel BFN and Apple BITOP instructions.
 */
typedef uint8_t util_lut2;
typedef uint8_t util_lut3;

#if !defined(_MSC_VER)
/*
 * Build a lookup table from a boolean expression. Bitwise operations are
 * supported: &, |, ^, and ~. Note ~ must be used, not !.
 *
 * The implementation uses a GNU statement-expression with the appropriate
 * masks, such that the AND of all three masks (with arbitrary complements)
 * equals the single bit for the corresponding min-term. This matches how Intel
 * describes BFN in the bspec, but it obscures the meaning.
 *
 * Casting to uint8_t masks the out-of-bounds bits in ~a & ~b & ~c.
 *
 * Example: UTIL_LUT3((a & b) | (~a & c))
 */
#define UTIL_LUT3(expr_involving_a_b_c)                                        \
   ({                                                                          \
      UNUSED const uint8_t a = 0xAA, b = 0xCC, c = 0xF0;                       \
      (util_lut3)(expr_involving_a_b_c);                                       \
   })

#define UTIL_LUT2(expr_involving_a_b)                                          \
   (util_lut2) UTIL_LUT3((expr_involving_a_b) & ~c)

/*
 * Return a lookup table with source s inverted. We exchange the minterms for
 * "source a is true" and "source a is false".
 */
static inline util_lut3
util_lut3_invert_source(util_lut3 l, unsigned s)
{
   uint8_t masks[] = {UTIL_LUT3(a), UTIL_LUT3(b), UTIL_LUT3(c)};
   assert(s < ARRAY_SIZE(masks));

   uint8_t mask = masks[s];
   uint8_t shift = __builtin_ctz(mask);
   uint8_t true_bits = l & mask;
   uint8_t false_bits = l & ~mask;
   return (false_bits << shift) | (true_bits >> shift);
}

static inline util_lut2
util_lut2_invert_source(util_lut2 l, unsigned s)
{
   return (util_lut2)(util_lut3_invert_source((util_lut3)l, s) & 0xf);
}
#endif

/*
 * Helpers to invert a LUT. This is easy: invert all the min-terms.
 */
static inline util_lut2
util_lut2_invert(util_lut2 l)
{
   return l ^ 0xf;
}

static inline util_lut3
util_lut3_invert(util_lut3 l)
{
   return l ^ 0xff;
}

/*
 * Return a lookup table equivalent to the input but with sources a & b swapped.
 * To implement, we swap the corresponding minterms.
 */
static inline util_lut2
util_lut2_swap_sources(util_lut2 l)
{
   return util_bit_swap(l, 1, 2);
}

static inline util_lut3
util_lut3_swap_sources(util_lut3 l, unsigned a, unsigned b)
{
   if (a == 0 && b == 1) {
      return util_bit_swap(util_bit_swap(l, 1, 2), 5, 6);
   } else if (a == 0 && b == 2) {
      return util_bit_swap(util_bit_swap(l, 1, 4), 3, 6);
   } else if (a == 1 && b == 2) {
      return util_bit_swap(util_bit_swap(l, 2, 4), 3, 5);
   }

   UNREACHABLE("invalid source selection");
}


/* Finding minimal string forms of LUTs is tricky, so we precompute. */
extern const char *util_lut3_to_str[256];
