/*
 * Copyright © 2025 Imagination Technologies Ltd.
 * 
 * SPDX-License-Identifier: MIT
 */

#include <gtest/gtest.h>
#include "util/macros.h"
#include "util/list.h"

struct test_node {
   struct list_head link;
};

/* node count must be even or some tests may try deleting the head */
#define NODE_COUNT (8)

struct test_ctx {
   struct list_head list;
   struct test_node nodes[NODE_COUNT];
};

static void init_test_ctx(struct test_ctx *ctx) {
   list_inithead(&ctx->list);
   for (unsigned i = 0; i < ARRAY_SIZE(ctx->nodes); i++) {
      list_addtail(&ctx->nodes[i].link, &ctx->list);
   }
}

#define LIST_LAST_EQ_TEST(name, iterator, node, stmt, index) \
   TEST(list, name) { \
      struct test_ctx ctx = {}; \
      init_test_ctx(&ctx); \
      struct test_node *__last_node = NULL; \
      iterator (struct test_node, node, &ctx.list, link) { \
         __last_node = node; \
         stmt; \
      } \
      EXPECT_EQ(__last_node, &ctx.nodes[index]); \
   }

#define LIST_DEATH_TEST(name, iterator, node, stmt, msg) \
   TEST(list, name) { \
      struct test_ctx ctx = {}; \
      init_test_ctx(&ctx); \
      EXPECT_DEATH({ \
         iterator (struct test_node, node, &ctx.list, link) { \
            stmt; \
         } \
      }, msg); \
   }

LIST_LAST_EQ_TEST(del_node_safe, list_for_each_entry_safe, node, {
   list_del(&node->link);
}, NODE_COUNT - 1)

LIST_LAST_EQ_TEST(delinit_node_safe, list_for_each_entry_safe, node, {
   list_delinit(&node->link);
}, NODE_COUNT - 1)

LIST_LAST_EQ_TEST(del_next, list_for_each_entry, node, {
   list_del(node->link.next);
}, NODE_COUNT - 2)

LIST_LAST_EQ_TEST(delinit_next, list_for_each_entry, node, {
   list_delinit(node->link.next);
}, NODE_COUNT - 2)

LIST_LAST_EQ_TEST(del_node_safe_rev, list_for_each_entry_safe_rev, node, {
   list_del(&node->link);
}, 0)

LIST_LAST_EQ_TEST(delinit_node_safe_rev, list_for_each_entry_safe_rev, node, {
   list_delinit(&node->link);
}, 0)

LIST_LAST_EQ_TEST(del_prev_rev, list_for_each_entry_rev, node, {
   list_del(node->link.prev);
}, 1)

LIST_LAST_EQ_TEST(delinit_prev_rev, list_for_each_entry_rev, node, {
   list_delinit(node->link.prev);
}, 1)

#ifndef NDEBUG 
LIST_DEATH_TEST(del_node, list_for_each_entry, node, {
   list_del(&node->link);
}, "use _safe iterator")

LIST_DEATH_TEST(delinit_node, list_for_each_entry, node, {
   list_delinit(&node->link);
}, "use _safe iterator")

LIST_DEATH_TEST(del_next_safe, list_for_each_entry_safe, node, {
   list_del(node->link.next);
}, "use non _safe iterator")

LIST_DEATH_TEST(delinit_next_safe, list_for_each_entry_safe, node, {
   list_delinit(node->link.next);
}, "use non _safe iterator")

LIST_DEATH_TEST(del_node_rev, list_for_each_entry_rev, node, {
   list_del(&node->link);
}, "use _safe iterator")

LIST_DEATH_TEST(delinit_node_rev, list_for_each_entry_rev, node, {
   list_delinit(&node->link);
}, "use _safe iterator")

LIST_DEATH_TEST(del_prev_safe_rev, list_for_each_entry_safe_rev, node, {
   list_del(node->link.prev);
}, "use non _safe iterator")

LIST_DEATH_TEST(delinit_prev_safe_rev, list_for_each_entry_safe_rev, node, {
   list_delinit(node->link.prev);
}, "use non _safe iterator")
#endif
