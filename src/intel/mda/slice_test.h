/*
 * Copyright 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <gtest/gtest.h>
#include <string>
#include <type_traits>
#include <cstdio>

#include "slice.h"

static inline
std::ostream& operator<<(std::ostream& os, const slice& s)
{
   os << "{data=\"";
   if (s.data) {
      os << std::string(s.data, s.len) << "\"";

      bool has_nul = false;
      for (int i = 0; i < s.len; i++) {
         if (s.data[i] == '\0') {
            has_nul = true;
            break;
         }
      }

      if (has_nul) {
         os << " [bytes: ";
         for (int i = 0; i < s.len; i++) {
            if (i > 0) os << " ";
            char buf[3];
            snprintf(buf, sizeof(buf), "%02x", (unsigned char)s.data[i]);
            os << buf;
         }
         os << "]";
      }
   } else {
      os << "(null)\"";
   }
   os << ", len=" << s.len << "}";
   return os;
}

static inline testing::AssertionResult
AssertSliceEmpty(const char* slice_expr, slice s)
{
   if (slice_is_empty(s))
      return testing::AssertionSuccess();

   return testing::AssertionFailure()
      << slice_expr << " is not empty\n"
      << "  " << slice_expr << " = " << s;
}

#define EXPECT_SLICE_EMPTY(slice) EXPECT_PRED_FORMAT1(AssertSliceEmpty, slice)
#define ASSERT_SLICE_EMPTY(slice) ASSERT_PRED_FORMAT1(AssertSliceEmpty, slice)


static inline testing::AssertionResult
AssertSliceNotEmpty(const char* slice_expr, slice s)
{
   if (!slice_is_empty(s))
      return testing::AssertionSuccess();

   return testing::AssertionFailure()
      << slice_expr << " is empty when it should not be\n"
      << "  " << slice_expr << " = " << s;
}

#define EXPECT_SLICE_NOT_EMPTY(slice) EXPECT_PRED_FORMAT1(AssertSliceNotEmpty, slice)
#define ASSERT_SLICE_NOT_EMPTY(slice) ASSERT_PRED_FORMAT1(AssertSliceNotEmpty, slice)


/* Use generics here to be able to compare slice with not only other
 * slices but also regular C strings (including literals).
 */
template<typename T>
testing::AssertionResult AssertSliceEqual(const char* slice_expr,
                                           const char* other_expr,
                                           slice s,
                                           const T& other) {
   /* String literals have type const char[N], not const char*, so we need decay
    * to convert array types to pointer types for uniform handling.  Note each
    * different size N would be have been a different type to handle below.
    */
   using DecayedT = std::decay_t<T>;

   static_assert(std::is_same_v<DecayedT, slice> ||
                 std::is_same_v<DecayedT, const char*> ||
                 std::is_same_v<DecayedT, char*> ||
                 std::is_array_v<T>,
                 "Second argument must be slice, const char*, char*, or string literal");

   if constexpr (std::is_same_v<DecayedT, const char*> ||
                 std::is_same_v<DecayedT, char*> ||
                 std::is_array_v<T>) {
      if (slice_equal_cstr(s, other))
         return testing::AssertionSuccess();

      std::stringstream ss;
      ss << slice_expr << " and " << other_expr << " are not equal\n"
         << "  " << slice_expr << " = " << s << "\n"
         << "  " << other_expr << " = \"";

      if constexpr (std::is_array_v<T>)
         ss << other;
      else
         ss << (other ? other : "(null)");

      ss << "\"";

      return testing::AssertionFailure() << ss.str();

   } else {
      static_assert(std::is_same_v<DecayedT, slice>);

      if (slice_equal(s, other))
         return testing::AssertionSuccess();

      return testing::AssertionFailure()
             << slice_expr << " and " << other_expr << " are not equal\n"
             << "  " << slice_expr << " = " << s << "\n"
             << "  " << other_expr << " = " << other;
   }
}

#define EXPECT_SLICE_EQ(slice, other) EXPECT_PRED_FORMAT2(AssertSliceEqual, slice, other)
#define ASSERT_SLICE_EQ(slice, other) ASSERT_PRED_FORMAT2(AssertSliceEqual, slice, other)

