/*
 * Copyright 2025 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include <random>

#include <gtest/gtest.h>

#include "macros.h"
#include "ralloc.h"
#include "range_minimum_query.h"

static uint32_t
rmq_naive(struct range_minimum_query_table *const table,
          uint32_t left_idx, uint32_t right_idx)
{
   uint32_t result = UINT32_MAX;
   for (uint32_t i = left_idx; i < right_idx; i++) {
      result = MIN2(result, table->table[i]);
   }
   return result;
}

TEST(range_minimum_query_test, range_minimum_query_test)
{
   void* context = ralloc_context(nullptr);

   std::mt19937 gen(1337);

   struct range_minimum_query_table table;
   range_minimum_query_table_init(&table);

   for (uint32_t width = 0; width < 256; width++) {
      range_minimum_query_table_resize(&table, context, width);

      std::uniform_int_distribution<> distrib(0, 100);
      for (uint32_t i = 0; i < width; i++) {
         table.table[i] = distrib(gen);
         // printf("%i\n", table.table[i]);
      }

      range_minimum_query_table_preprocess(&table);
      for (uint32_t i = 0; i < width; i++) {
         for (uint32_t j = i + 1; j < width; j++) {
            // printf("%i, %i\n", i, j);
            EXPECT_EQ(range_minimum_query(&table, i, j),
                      rmq_naive(&table, i, j));
         }
      }
   }

   ralloc_free(context);
}
