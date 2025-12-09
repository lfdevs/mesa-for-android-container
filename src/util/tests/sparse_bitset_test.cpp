/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include <gtest/gtest.h>
#include "util/sparse_bitset.h"


TEST(sparse_bitset, tree)
{
   struct u_sparse_bitset set;
   u_sparse_bitset_init(&set, 1048577, NULL);

   u_sparse_bitset_set(&set, 65535);
   u_sparse_bitset_set(&set, 1048576);

   unsigned num_nodes = 0;
   rb_tree_foreach(struct u_sparse_bitset_node, _, &set.tree, node) {
      ++num_nodes;
   }
   EXPECT_EQ(num_nodes, 2);

   u_sparse_bitset_free(&set);
}

TEST(sparse_bitset, set_clear)
{
   struct u_sparse_bitset set;
   u_sparse_bitset_init(&set, 1048577, NULL);

   u_sparse_bitset_set(&set, 65535);
   u_sparse_bitset_set(&set, 1048576);
   u_sparse_bitset_set(&set, 16383);

   EXPECT_EQ(u_sparse_bitset_test(&set, 128), false);
   EXPECT_EQ(u_sparse_bitset_test(&set, 65535), true);
   EXPECT_EQ(u_sparse_bitset_test(&set, 16383), true);

   u_sparse_bitset_clear(&set, 1236749);
   u_sparse_bitset_clear(&set, 65535);

   EXPECT_EQ(u_sparse_bitset_test(&set, 65535), false);

   u_sparse_bitset_free(&set);
}

TEST(sparse_bitset, set_dup)
{
   struct u_sparse_bitset set;
   u_sparse_bitset_init(&set, 1048577, NULL);

   u_sparse_bitset_set(&set, 65535);
   u_sparse_bitset_set(&set, 1048576);

   struct u_sparse_bitset set2;
   u_sparse_bitset_dup(&set2, &set);

   u_sparse_bitset_clear(&set, 65535);

   EXPECT_EQ(u_sparse_bitset_test(&set2, 128), false);
   EXPECT_EQ(u_sparse_bitset_test(&set2, 65535), true);
   EXPECT_EQ(u_sparse_bitset_test(&set2, 1048576), true);

   u_sparse_bitset_free(&set);
   u_sparse_bitset_free(&set2);
}

TEST(sparse_bitset, set_merge)
{
   struct u_sparse_bitset set;
   u_sparse_bitset_init(&set, 1048577, NULL);

   u_sparse_bitset_set(&set, 65535);
   u_sparse_bitset_set(&set, 1048576);

   struct u_sparse_bitset set2;
   u_sparse_bitset_init(&set2, 1048577, NULL);
   u_sparse_bitset_set(&set2, 128);
   u_sparse_bitset_set(&set2, 16383);

   EXPECT_EQ(u_sparse_bitset_merge(&set2, &set), true);

   u_sparse_bitset_clear(&set, 65535);

   EXPECT_EQ(u_sparse_bitset_test(&set2, 128), true);
   EXPECT_EQ(u_sparse_bitset_test(&set2, 16383), true);
   EXPECT_EQ(u_sparse_bitset_test(&set2, 65535), true);
   EXPECT_EQ(u_sparse_bitset_test(&set2, 1048576), true);

   EXPECT_EQ(u_sparse_bitset_merge(&set2, &set), false);

   EXPECT_EQ(u_sparse_bitset_test(&set2, 128), true);
   EXPECT_EQ(u_sparse_bitset_test(&set2, 16383), true);
   EXPECT_EQ(u_sparse_bitset_test(&set2, 65535), true);
   EXPECT_EQ(u_sparse_bitset_test(&set2, 1048576), true);

   u_sparse_bitset_free(&set);
   u_sparse_bitset_free(&set2);
}

TEST(sparse_bitset, set_foreach)
{
   struct u_sparse_bitset set;
   u_sparse_bitset_init(&set, 1048577, NULL);

   u_sparse_bitset_set(&set, 65535);
   u_sparse_bitset_set(&set, 1048576);
   u_sparse_bitset_set(&set, 16383);
   u_sparse_bitset_set(&set, 19);
   u_sparse_bitset_set(&set, 422);
   u_sparse_bitset_set(&set, 65539);

   uint32_t arr[] = {19, 422, 16383, 65535, 65539, 1048576};
   unsigned i = 0;

   U_SPARSE_BITSET_FOREACH_SET(&set, it) {
      EXPECT_LE(i, ARRAY_SIZE(arr));
      EXPECT_EQ(it, arr[i]);
      ++i;
   }

   EXPECT_EQ(i, ARRAY_SIZE(arr));

   u_sparse_bitset_free(&set);
}
