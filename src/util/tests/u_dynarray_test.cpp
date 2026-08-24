/*
 * Copyright 2026 Jan Meisel
 * SPDX-License-Identifier: MIT
 */

#include <gtest/gtest.h>

#include "util/u_dynarray.h"

TEST(util_dynarray, grow_within_capacity)
{
   uint8_t storage[16];
   struct util_dynarray array;
   util_dynarray_init_from_stack(&array, storage, sizeof(storage));

   void *first = util_dynarray_grow_bytes(&array, 1, 4);
   ASSERT_EQ(first, storage);
   ASSERT_EQ(array.size, 4);

   void *second = util_dynarray_grow_bytes(&array, 1, 4);
   EXPECT_EQ(second, storage + 4);
   EXPECT_EQ(array.data, storage);
   EXPECT_EQ(array.size, 8);
   EXPECT_EQ(array.capacity, sizeof(storage));
   EXPECT_EQ(array.mem_ctx, &util_dynarray_is_data_stack_allocated);

   util_dynarray_fini(&array);
}

TEST(util_dynarray, grow_past_stack_capacity)
{
   uint32_t storage[] = {0x12345678};
   struct util_dynarray array;
   util_dynarray_init_from_stack(&array, storage, sizeof(storage));
   array.size = sizeof(storage);

   uint32_t *next = static_cast<uint32_t *>(
      util_dynarray_grow_bytes(&array, 1, sizeof(uint32_t)));
   ASSERT_NE(next, nullptr);
   EXPECT_NE(array.data, storage);
   EXPECT_EQ(array.mem_ctx, nullptr);
   EXPECT_EQ(array.size, 2 * sizeof(uint32_t));
   EXPECT_GE(array.capacity, array.size);
   EXPECT_EQ(*static_cast<uint32_t *>(array.data), storage[0]);
   EXPECT_EQ(next, static_cast<uint32_t *>(array.data) + 1);

   util_dynarray_fini(&array);
}

TEST(util_dynarray, grow_overflow)
{
   struct util_dynarray array;
   util_dynarray_init(&array, nullptr);

   EXPECT_EQ(util_dynarray_grow_bytes(&array, UINT_MAX, 2), nullptr);
   EXPECT_EQ(array.data, nullptr);
   EXPECT_EQ(array.size, 0);
   EXPECT_EQ(array.capacity, 0);
}
