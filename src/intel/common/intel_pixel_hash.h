/*
 * Copyright © 2021 Intel Corporation
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
#ifndef INTEL_PIXEL_HASH_H
#define INTEL_PIXEL_HASH_H

/**
 * Compute an \p n x \p m pixel hashing table usable as slice, subslice or
 * pixel pipe hashing table.  The resulting table is the cyclic repetition of
 * a fixed pattern with periodicity equal to \p period.
 *
 * If \p index is specified to be equal to \p period, a 2-way hashing table
 * will be generated such that indices 0 and 1 are returned for the following
 * fractions of entries respectively:
 *
 *   p_0 = ceil(period / 2) / period
 *   p_1 = floor(period / 2) / period
 *
 * If \p index is even and less than \p period, a 3-way hashing table will be
 * generated such that indices 0, 1 and 2 are returned for the following
 * fractions of entries:
 *
 *   p_0 = (ceil(period / 2) - 1) / period
 *   p_1 = floor(period / 2) / period
 *   p_2 = 1 / period
 *
 * The equations above apply if \p flip is equal to 0, if it is equal to 1 p_0
 * and p_1 will be swapped for the result.  Note that in the context of pixel
 * pipe hashing this can be always 0 on Gfx12 platforms, since the hardware
 * transparently remaps logical indices found on the table to physical pixel
 * pipe indices from the highest to lowest EU count.
 */
UNUSED static void
intel_compute_pixel_hash_table_3way(unsigned n, unsigned m,
                                    unsigned period, unsigned index, bool flip,
                                    uint32_t *p)
{
   for (unsigned i = 0; i < n; i++) {
      for (unsigned j = 0; j < m; j++) {
         const unsigned k = (i + j) % period;
         p[j + m * i] = (k == index ? 2 : (k & 1) ^ flip);
      }
   }
}

/**
 * Compute an \p n x \p m pixel hashing table usable as slice,
 * subslice or pixel pipe hashing table.  This generalizes the
 * previous 3-way hash table function to an arbitrary number of ways
 * given by the number of bits set in the \p mask argument, but
 * doesn't allow the specification of different frequencies for
 * different table indices.
 */
UNUSED static void
intel_compute_pixel_hash_table_nway(unsigned n, unsigned m, uint32_t mask,
                                    uint32_t *p)
{
   /* Construct a table mapping consecutive indices to the physical
    * indices given by the bits set on the mask argument.
    */
   unsigned phys_ids[sizeof(mask) * CHAR_BIT];
   unsigned num_ids = 0;

   u_foreach_bit(i, mask)
      phys_ids[num_ids++] = i;

   assert(num_ids > 0);

   /* Compute a permutation of the above indices that assigns indices
    * as far as possible to adjacent entries.  This permutation is
    * designed to be equivalent to the bit reversal of each index in
    * cases where num_ids is a power of two, but doesn't actually
    * require it to be a power of two in order to satisfy the required
    * properties (which is necessary to handle configurations with
    * arbitrary non-power of two fusing).  By construction, flipping
    * bit l of its input will lead to a change in its result of the
    * order of num_ids/2^(l+1) (see variable t below).  The
    * bijectivity of this permutation can be verified easily by
    * induction.
    */
   const unsigned bits = util_logbase2_ceil(num_ids);
   unsigned swz[ARRAY_SIZE(phys_ids)];

   for (unsigned k = 0; k < num_ids; k++) {
      unsigned t = num_ids;
      unsigned s = 0;

      for (unsigned l = 0; l < bits; l++) {
         if (k & (1u << l)) {
            s += (t + 1) >> 1;
            t >>= 1;
         } else {
            t = (t + 1) >> 1;
         }
      }

      swz[k] = s;
   }

   /* Initialize the table with the cyclic repetition of a
    * num_ids-periodic pattern.
    *
    * Note that the swz permutation only affects the ordering of rows.
    * This is intentional in order to minimize the size of the
    * contiguous area that needs to be rendered in parallel in order
    * to utilize the whole GPU: A rendering rectangle of width W will
    * need to be at least H blocks high, where H is bounded by
    * 2^ceil(log2(num_ids/W)) thanks to the above definition of the swz
    * permutation.
    */
   for (unsigned i = 0; i < n; i++) {
      const unsigned k = i % num_ids;
      assert(swz[k] < num_ids);
      for (unsigned j = 0; j < m; j++) {
         p[j + m * i] = phys_ids[(j + swz[k]) % num_ids];
      }
   }
}

#endif
