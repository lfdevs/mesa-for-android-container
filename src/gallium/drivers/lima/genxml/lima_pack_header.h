/*
 * Copyright 2016 Intel Corporation
 * Copyright 2016 Broadcom
 * Copyright 2020 Collabora, Ltd.
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2025 Lima Project
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <inttypes.h>
#include <stdio.h>
#include "util/bitpack_helpers.h"
#include "util/half_float.h"
#define FILE_TYPE FILE
#define CONSTANT_ const
#define GLOBAL_

#define __gen_unpack_float(x, y, z) uif(__gen_unpack_uint(x, y, z))
#define __gen_unpack_half(x, y, z)                                             \
   _mesa_half_to_float(__gen_unpack_uint(x, y, z))

static inline uint64_t
__gen_unpack_uint(CONSTANT_ uint32_t *restrict cl, uint32_t start, uint32_t end)
{
   uint64_t val = 0;
   const int width = end - start + 1;
   const uint64_t mask =
      (width == 64) ? ~((uint64_t)0) : ((uint64_t)1 << width) - 1;

   for (unsigned word = start / 32; word < (end / 32) + 1; word++) {
      val |= ((uint64_t)cl[word]) << ((word - start / 32) * 32);
   }

   return (val >> (start % 32)) & mask;
}

static inline uint64_t
__gen_pack_unorm16(float f, uint32_t start, uint32_t end)
{
   return util_bitpack_uint(float_to_ushort(f), start, end);
}

static inline float
__gen_unpack_unorm16(CONSTANT_ uint32_t *restrict cl, uint32_t start, uint32_t end)
{
   return ushort_to_float(__gen_unpack_uint(cl, start, end));
}

static inline uint64_t
__gen_pack_unorm8(float f, uint32_t start, uint32_t end)
{
   return util_bitpack_uint(float_to_ubyte(f), start, end);
}

static inline float
__gen_unpack_unorm8(CONSTANT_ uint32_t *restrict cl, uint32_t start, uint32_t end)
{
   return ubyte_to_float(__gen_unpack_uint(cl, start, end));
}

static inline uint64_t
__gen_unpack_sint(CONSTANT_ uint32_t *restrict cl, uint32_t start, uint32_t end)
{
   int size = end - start + 1;
   int64_t val = __gen_unpack_uint(cl, start, end);

   return util_sign_extend(val, size);
}

static inline float
__gen_unpack_ulod(const uint32_t *restrict cl, uint32_t start, uint32_t end)
{
   uint32_t u = __gen_unpack_uint(cl, start, end);

   return ((float)u) / 16.0;
}

static inline float
__gen_unpack_slod(const uint32_t *restrict cl, uint32_t start, uint32_t end)
{
   int32_t u = __gen_unpack_sint(cl, start, end);

   return ((float)u) / 16.0;
}

static inline uint64_t
__gen_to_groups(uint32_t value, uint32_t group_size, uint32_t length)
{
   /* Zero is not representable, clamp to minimum */
   if (value == 0)
      return 1;

   /* Round up to the nearest number of groups */
   uint32_t groups = DIV_ROUND_UP(value, group_size);

   /* The 0 encoding means "all" */
   if (groups == ((uint64_t)1) << length)
      return 0;

   /* Otherwise it's encoded as the identity */
   assert(groups < (1u << length) && "out of bounds");
   assert(groups >= 1 && "exhaustive");
   return groups;
}

static inline uint64_t
__gen_from_groups(uint32_t value, uint32_t group_size, uint32_t length)
{
   return group_size * (value ? value : (1 << length));
}

#define lima_pack(dst, T, name)                                                \
   for (struct LIMA_##T name = {LIMA_##T##_header},                            \
                       *_loop_count = (GLOBAL_ void *)((uintptr_t)0);          \
        (uintptr_t)_loop_count < 1; (                                          \
           {                                                                   \
              LIMA_##T##_pack((GLOBAL_ uint32_t *)(dst), &name);               \
              _loop_count = (GLOBAL_ void *)(((uintptr_t)_loop_count) + 1);    \
           }))

#define lima_unpack(fp, src, T, name)                                          \
   struct LIMA_##T name;                                                       \
   LIMA_##T##_unpack(fp, (CONSTANT_ uint8_t *)(src), &name)

#define lima_print(fp, T, var, indent) LIMA_##T##_print(fp, &(var), indent)

static inline void
lima_merge_helper(uint32_t *dst, const uint32_t *src, size_t bytes)
{
   assert((bytes & 3) == 0);

   for (unsigned i = 0; i < (bytes / 4); ++i)
      dst[i] |= src[i];
}

#define lima_merge(packed1, packed2, type)                                      \
   lima_merge_helper((packed1).opaque, (packed2).opaque, LIMA_##type##_LENGTH)

#if defined(NDEBUG)
#define lima_genxml_validate_bounds(a, b, c)
#define lima_genxml_validate_mask(a, b, c, d, e) true
#define lima_genxml_validate_exact(a, b, c, d)   true
#else
static inline void
lima_genxml_validate_bounds(const char *name, uint64_t value, uint64_t bound)
{
   if (unlikely(value >= bound)) {
      fprintf(stderr, "%s out-of-bounds, got 0x%" PRIx64 ", max %" PRIx64 "\n",
              name, value, bound);

      UNREACHABLE("Out-of-bounds pack");
   }
}

static inline bool
lima_genxml_validate_mask(FILE *fp, const char *name, const void *cl_,
                         uint32_t index, uint32_t bad_mask)
{
   const uint32_t *cl = (const uint32_t *)cl_;
   uint32_t bad = cl[index] & bad_mask;

   if (bad && fp != NULL) {
      fprintf(
         fp,
         "XXX: Unknown field of %s unpacked at word %u got %X, bad mask %X\n",
         name, index, cl[index], bad);
   }

   return bad == 0;
}

static bool
lima_genxml_validate_exact(FILE *fp, const char *name, uint64_t value,
                          uint64_t exact)
{
   if (value != exact && fp != NULL) {
      fprintf(fp, "XXX: Expected %s to equal %" PRIx64 " but got %" PRIx64 "\n",
              name, value, exact);
   }

   return value == exact;
}

#endif

/* Everything after this is autogenerated from XML. Do not hand edit. */
