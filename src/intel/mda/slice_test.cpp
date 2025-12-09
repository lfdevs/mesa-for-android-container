/*
 * Copyright 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <gtest/gtest.h>

#include "slice.h"
#include "slice_test.h"

TEST(Slice, Cut)
{
   slice s = slice_from_cstr("hello:world");

   slice_cut_result result = slice_cut(s, ':');
   ASSERT_TRUE(result.found);
   ASSERT_SLICE_EQ(result.before, "hello");
   ASSERT_SLICE_EQ(result.after, "world");

   slice s2 = slice_from_cstr("no separator");
   slice_cut_result result2 = slice_cut(s2, ':');
   ASSERT_FALSE(result2.found);
   ASSERT_SLICE_EQ(result2.before, s2);
   ASSERT_SLICE_EMPTY(result2.after);
}

TEST(Slice, CutN)
{
   slice s = slice_from_cstr("a:b:c:d");

   slice_cut_result result1 = slice_cut_n(s, ':', 2);
   ASSERT_TRUE(result1.found);
   ASSERT_SLICE_EQ(result1.before, "a:b");
   ASSERT_SLICE_EQ(result1.after, "c:d");

   slice_cut_result result2 = slice_cut_n(s, ':', 1);
   ASSERT_TRUE(result2.found);
   ASSERT_SLICE_EQ(result2.before, "a");
   ASSERT_SLICE_EQ(result2.after, "b:c:d");

   slice_cut_result result3 = slice_cut_n(s, ':', 5);
   ASSERT_FALSE(result3.found);
   ASSERT_SLICE_EQ(result3.before, s);
   ASSERT_SLICE_EMPTY(result3.after);

   slice_cut_result result4 = slice_cut_n(s, ':', 0);
   ASSERT_FALSE(result4.found);
   slice_cut_result result5 = slice_cut_n(s, ':', -1);
   ASSERT_FALSE(result5.found);
}

TEST(Slice, HashTable)
{
   struct hash_table *ht = slice_hash_table_create(NULL);

   const char *strings[] = {
      "NIR-CS/v1", "NIR-CS/v2", "BRW-CS/v1", "BRW-CS/v2",
      "ASM-CS/v1", "ASM-CS/v2", "NIR-FS/v1", "BRW-FS/v1"
   };
   int values[] = {1, 2, 3, 4, 5, 6, 7, 8};

   for (int i = 0; i < 8; i++) {
      slice key = slice_from_cstr(strings[i]);
      slice_hash_table_insert(ht, key, &values[i]);
   }

   ASSERT_EQ(_mesa_hash_table_num_entries(ht), 8u);

   for (int i = 0; i < 8; i++) {
      slice key = slice_from_cstr(strings[i]);
      struct hash_entry *found = slice_hash_table_search(ht, key);
      ASSERT_NE(found, nullptr);
      ASSERT_EQ(*(int*)found->data, values[i]);
   }

   const int index = 2;
   slice same_content = slice_from_cstr(strings[index]);
   struct hash_entry *found = slice_hash_table_search(ht, same_content);
   ASSERT_NE(found, nullptr);
   ASSERT_EQ(*(int*)found->data, values[index]);

   _mesa_hash_table_destroy(ht, NULL);
}
