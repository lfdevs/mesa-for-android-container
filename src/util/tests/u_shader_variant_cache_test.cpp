/*
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include <gtest/gtest.h>

#include "util/u_shader_variant_cache.h"
#include "util/u_memory.h"

#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

struct test_ctx;

struct test_variant {
   struct util_shader_variant base;
   struct test_ctx *tctx;
   int tag;
};

struct test_ctx {
   std::atomic<int> compile_calls{0};
   std::atomic<int> destroy_calls{0};
   std::atomic<int> next_tag{1};
   /* If non-zero, compile returns NULL when compile_calls hits this value. */
   int fail_after = 0;
};

static struct util_shader_variant *
test_compile(void *user_data, UNUSED void *cso, UNUSED const void *key)
{
   struct test_ctx *tctx = (struct test_ctx *)user_data;
   int n = ++tctx->compile_calls;

   if (tctx->fail_after && n >= tctx->fail_after)
      return NULL;

   struct test_variant *v = CALLOC_STRUCT(test_variant);
   if (!v)
      return NULL;

   v->tctx = tctx;
   v->tag = tctx->next_tag.fetch_add(1);

   return &v->base;
}

static void
test_destroy(struct util_shader_variant *variant)
{
   struct test_variant *v = container_of(variant, struct test_variant, base);

   v->tctx->destroy_calls++;
   FREE(v);
}

static std::vector<uint32_t>
list_keys(struct util_shader_variant_list *list)
{
   std::vector<uint32_t> keys;

   list_for_each_entry(struct util_shader_variant, v, &list->variants, link)
      keys.push_back(*(const uint32_t *)v->key);

   return keys;
}

TEST(u_shader_variant_cache, list_init_destroy_empty)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;

   util_shader_variant_list_init(&list);

   EXPECT_TRUE(list_is_empty(&list.variants));

   util_shader_variant_list_destroy(&opts, &list);

   /* An empty list destroyed should not invoke any destroy callbacks. */
   EXPECT_EQ(tctx.destroy_calls.load(), 0);
}

TEST(u_shader_variant_cache, get_compiles_on_miss)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;

   util_shader_variant_list_init(&list);

   uint32_t key = 0x1234;
   struct util_shader_variant *v =
      util_shader_variant_get(&opts, &list, /*cso*/ nullptr,
                              &key, sizeof(key), nullptr);
   ASSERT_NE(v, nullptr);
   EXPECT_EQ(tctx.compile_calls.load(), 1);
   EXPECT_EQ(list_length(&list.variants), 1);

   util_shader_variant_list_destroy(&opts, &list);
   EXPECT_EQ(tctx.destroy_calls.load(), 1);
}

TEST(u_shader_variant_cache, get_returns_borrowed_ref)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;
   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;

   util_shader_variant_list_init(&list);

   uint32_t key = 0xC0DE;
   struct util_shader_variant *v =
      util_shader_variant_get(&opts, &list, nullptr,
                              &key, sizeof(key), nullptr);
   ASSERT_NE(v, nullptr);
   /* The cache holds the only ref. The returned pointer is borrowed. */
   EXPECT_EQ(v->reference.count, 1);

   util_shader_variant_list_destroy(&opts, &list);
}

TEST(u_shader_variant_cache, get_hit_second_lookup)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;

   util_shader_variant_list_init(&list);

   uint32_t key = 0xABCD;
   struct util_shader_variant *a =
      util_shader_variant_get(&opts, &list, nullptr,
                              &key, sizeof(key), nullptr);
   ASSERT_NE(a, nullptr);

   struct util_shader_variant *b =
      util_shader_variant_get(&opts, &list, nullptr,
                              &key, sizeof(key), nullptr);
   EXPECT_EQ(a, b);
   EXPECT_EQ(tctx.compile_calls.load(), 1);
   EXPECT_EQ(list_length(&list.variants), 1);

   util_shader_variant_list_destroy(&opts, &list);
   EXPECT_EQ(tctx.destroy_calls.load(), 1);
}

TEST(u_shader_variant_cache, was_cache_miss_reporting)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;

   util_shader_variant_list_init(&list);

   uint32_t k1 = 1, k2 = 2;
   bool miss = false;

   /* First lookup compiles. */
   struct util_shader_variant *a =
      util_shader_variant_get(&opts, &list, nullptr,
                              &k1, sizeof(k1), &miss);
   ASSERT_NE(a, nullptr);
   EXPECT_TRUE(miss);

   /* Second lookup of the same key hits. */
   struct util_shader_variant *b =
      util_shader_variant_get(&opts, &list, nullptr,
                              &k1, sizeof(k1), &miss);
   EXPECT_EQ(b, a);
   EXPECT_FALSE(miss);

   /* A different key misses again. */
   struct util_shader_variant *c =
      util_shader_variant_get(&opts, &list, nullptr,
                              &k2, sizeof(k2), &miss);
   ASSERT_NE(c, nullptr);
   EXPECT_NE(c, a);
   EXPECT_TRUE(miss);

   util_shader_variant_list_destroy(&opts, &list);
}

TEST(u_shader_variant_cache, multiple_variants_one_list)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;

   util_shader_variant_list_init(&list);

   uint32_t keys[3] = { 1, 2, 3 };
   struct util_shader_variant *vs[3];

   for (int i = 0; i < 3; ++i) {
      vs[i] = util_shader_variant_get(&opts, &list, nullptr,
                                      &keys[i], sizeof(keys[i]), nullptr);
      ASSERT_NE(vs[i], nullptr);
   }

   EXPECT_EQ(tctx.compile_calls.load(), 3);
   EXPECT_EQ(list_length(&list.variants), 3);

   EXPECT_NE(vs[0], vs[1]);
   EXPECT_NE(vs[1], vs[2]);
   EXPECT_NE(vs[0], vs[2]);

   util_shader_variant_list_destroy(&opts, &list);
   EXPECT_EQ(tctx.destroy_calls.load(), 3);
}

TEST(u_shader_variant_cache, compile_null_is_surfaced)
{
   struct test_ctx tctx;
   tctx.fail_after = 1;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;

   util_shader_variant_list_init(&list);

   uint32_t k = 99;
   struct util_shader_variant *v =
      util_shader_variant_get(&opts, &list, nullptr, &k, sizeof(k), nullptr);
   EXPECT_EQ(v, nullptr);
   EXPECT_EQ(tctx.compile_calls.load(), 1);
   EXPECT_EQ(tctx.destroy_calls.load(), 0);
   EXPECT_EQ(list_length(&list.variants), 0);

   util_shader_variant_list_destroy(&opts, &list);
}

TEST(u_shader_variant_cache, concurrent_get)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;

   util_shader_variant_list_init(&list);

   constexpr int num_threads = 8;
   constexpr int num_iters   = 20000;
   constexpr int num_keys    = 32;

   std::atomic<int> misses{0};

   auto worker = [&]() {
      for (int i = 0; i < num_iters; ++i) {
         uint32_t k = (uint32_t)(i % num_keys);
         bool miss = false;
         struct util_shader_variant *v =
            util_shader_variant_get(&opts, &list, nullptr,
                                    &k, sizeof(k), &miss);
         ASSERT_NE(v, nullptr);
         if (miss)
            misses++;
      }
   };

   std::vector<std::thread> threads;

   for (int i = 0; i < num_threads; ++i)
      threads.emplace_back(worker);

   for (auto &t : threads)
      t.join();

   EXPECT_EQ(list_length(&list.variants), num_keys);

   /* Only the publish winner of each key reports a miss; peers that
    * compiled but lost the race (dedup-on-publish) report a hit.
    */
   EXPECT_EQ(misses.load(), num_keys);

   /* Race-on-publish loses a few compiles to dedup. The losing
    * compiles get destroyed inline, so destroy_calls so far equals
    * (compiles - num_keys).
    */
   int compiles = tctx.compile_calls.load();
   int destroys_so_far = tctx.destroy_calls.load();
   EXPECT_EQ(destroys_so_far, compiles - num_keys);

   util_shader_variant_list_destroy(&opts, &list);
   EXPECT_EQ(tctx.destroy_calls.load(), compiles);
}

static uint32_t test_hash_first_byte(const void *key)
{
   return *(const uint8_t *)key;
}

static bool test_equal_first_byte(const void *a, const void *b)
{
   return *(const uint8_t *)a == *(const uint8_t *)b;
}

TEST(u_shader_variant_cache, custom_callbacks_hit)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.hash = test_hash_first_byte;
   opts.equal = test_equal_first_byte;
   opts.user_data = &tctx;

   util_shader_variant_list_init(&list);

   uint8_t key[4] = { 0x42, 1, 2, 3 };
   struct util_shader_variant *a =
      util_shader_variant_get(&opts, &list, nullptr,
                              key, sizeof(key), nullptr);
   ASSERT_NE(a, nullptr);

   /* Same first byte -> custom equal returns true -> cache hit. */
   uint8_t key2[4] = { 0x42, 9, 9, 9 };
   struct util_shader_variant *b =
      util_shader_variant_get(&opts, &list, nullptr,
                              key2, sizeof(key2), nullptr);
   EXPECT_EQ(a, b);

   util_shader_variant_list_destroy(&opts, &list);
}

TEST(u_shader_variant_cache, custom_callbacks_miss)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.hash = test_hash_first_byte;
   opts.equal = test_equal_first_byte;
   opts.user_data = &tctx;

   util_shader_variant_list_init(&list);

   uint8_t key1[4] = { 0x42, 1, 2, 3 };
   uint8_t key2[4] = { 0x99, 4, 5, 6 };
   struct util_shader_variant *a =
      util_shader_variant_get(&opts, &list, nullptr,
                              key1, sizeof(key1), nullptr);
   struct util_shader_variant *b =
      util_shader_variant_get(&opts, &list, nullptr,
                              key2, sizeof(key2), nullptr);
   EXPECT_NE(a, b);
   EXPECT_EQ(list_length(&list.variants), 2);

   util_shader_variant_list_destroy(&opts, &list);
}

TEST(u_shader_variant_cache, cap_evicts_lru)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;
   opts.cap = 2;

   util_shader_variant_list_init(&list);

   auto insert = [&](uint32_t key) {
      struct util_shader_variant *transient = NULL;
      struct util_shader_variant *v =
         util_shader_variant_get_pinned(&opts, &list, nullptr,
                                        &key, sizeof(key), &transient, nullptr);
      ASSERT_NE(v, nullptr);
      util_shader_variant_reference(&opts, &transient, nullptr);
   };

   insert(1);
   insert(2);
   EXPECT_EQ(list.count, 2u);
   EXPECT_EQ(tctx.destroy_calls.load(), 0);
   /* MRU (head) to LRU (tail): key 2 was inserted last. */
   EXPECT_EQ(list_keys(&list), (std::vector<uint32_t>{2, 1}));

   /* Insert a third - list is over cap, oldest (key=1) evicted. */
   insert(3);
   EXPECT_EQ(list.count, 2u);
   EXPECT_EQ(tctx.destroy_calls.load(), 1);
   /* key 1 is gone; 3 (newest) is MRU, 2 is now the LRU end. */
   EXPECT_EQ(list_keys(&list), (std::vector<uint32_t>{3, 2}));

   util_shader_variant_list_destroy(&opts, &list);
   EXPECT_EQ(tctx.destroy_calls.load(), 3);
}

TEST(u_shader_variant_cache, eviction_skips_pinned)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;
   opts.cap = 2;

   util_shader_variant_list_init(&list);

   /* Pin k1 in the long-lived pin slot. */
   uint32_t k1 = 1;
   struct util_shader_variant *pin = NULL;
   struct util_shader_variant *base_k1 =
      util_shader_variant_get_pinned(&opts, &list, nullptr,
                                     &k1, sizeof(k1), &pin, nullptr);
   ASSERT_NE(base_k1, nullptr);

   /* k2 inserted and immediately released - the only unpinned variant. */
   uint32_t k2 = 2;
   struct util_shader_variant *transient = NULL;
   struct util_shader_variant *v2 =
      util_shader_variant_get_pinned(&opts, &list, nullptr,
                                     &k2, sizeof(k2), &transient, nullptr);
   ASSERT_NE(v2, nullptr);
   util_shader_variant_reference(&opts, &transient, nullptr);

   /* k3 overflows. Eviction sees k1 at the tail (pinned, skip) and
    * picks k2 (unpinned).
    */
   uint32_t k3 = 3;
   struct util_shader_variant *v3 =
      util_shader_variant_get_pinned(&opts, &list, nullptr,
                                     &k3, sizeof(k3), &transient, nullptr);
   ASSERT_NE(v3, nullptr);
   util_shader_variant_reference(&opts, &transient, nullptr);

   EXPECT_EQ(list.count, 2u);
   EXPECT_EQ(tctx.destroy_calls.load(), 1);  /* only k2 evicted */
   /* k2 is gone; k3 (newest) is MRU, pinned k1 survived at the LRU end. */
   EXPECT_EQ(list_keys(&list), (std::vector<uint32_t>{3, 1}));

   util_shader_variant_reference(&opts, &pin, nullptr);
   util_shader_variant_list_destroy(&opts, &list);
   EXPECT_EQ(tctx.destroy_calls.load(), 3);
}

TEST(u_shader_variant_cache, mru_on_hit_reorders_lru)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;
   opts.cap = 2;

   util_shader_variant_list_init(&list);

   uint32_t k1 = 1, k2 = 2, k3 = 3;
   struct util_shader_variant *transient = NULL;

   /* Fill: list is [k2, k1] with k1 at the LRU end. */
   struct util_shader_variant *v1 =
      util_shader_variant_get_pinned(&opts, &list, nullptr,
                                     &k1, sizeof(k1), &transient, nullptr);
   ASSERT_NE(v1, nullptr);
   const struct util_shader_variant *v1_addr = v1;
   util_shader_variant_reference(&opts, &transient, nullptr);

   struct util_shader_variant *v2 =
      util_shader_variant_get_pinned(&opts, &list, nullptr,
                                     &k2, sizeof(k2), &transient, nullptr);
   ASSERT_NE(v2, nullptr);
   util_shader_variant_reference(&opts, &transient, nullptr);
   /* Filled: [k2, k1], k1 at the LRU end. */
   EXPECT_EQ(list_keys(&list), (std::vector<uint32_t>{2, 1}));

   /* Hit on k1 promotes it to head: [k1, k2], k2 now at tail. */
   struct util_shader_variant *v1_again =
      util_shader_variant_get_pinned(&opts, &list, nullptr,
                                     &k1, sizeof(k1), &transient, nullptr);
   EXPECT_EQ(v1_again, v1_addr);
   util_shader_variant_reference(&opts, &transient, nullptr);
   EXPECT_EQ(list_keys(&list), (std::vector<uint32_t>{1, 2}));

   /* Insert k3 - overflows, k2 (now LRU) gets evicted, k1 survives. */
   struct util_shader_variant *v3 =
      util_shader_variant_get_pinned(&opts, &list, nullptr,
                                     &k3, sizeof(k3), &transient, nullptr);
   ASSERT_NE(v3, nullptr);
   util_shader_variant_reference(&opts, &transient, nullptr);
   EXPECT_EQ(tctx.destroy_calls.load(), 1);
   /* k2 evicted; promoted k1 survived. */
   EXPECT_EQ(list_keys(&list), (std::vector<uint32_t>{3, 1}));

   util_shader_variant_list_destroy(&opts, &list);
   EXPECT_EQ(tctx.destroy_calls.load(), 3);
}

TEST(u_shader_variant_cache, orphan_survives_list_destroy)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;

   util_shader_variant_list_init(&list);

   uint32_t key = 0x10ABE1;
   struct util_shader_variant *base =
      util_shader_variant_get(&opts, &list, nullptr,
                              &key, sizeof(key), nullptr);
   ASSERT_NE(base, nullptr);

   /* Pin via _reference (refcount = cache + pin). _list_destroy drops
    * the cache ref but the variant survives off-list while pin holds it.
    */
   struct util_shader_variant *pin = NULL;
   util_shader_variant_reference(&opts, &pin, base);

   util_shader_variant_list_destroy(&opts, &list);
   EXPECT_EQ(tctx.destroy_calls.load(), 0);

   /* Last drop fires the destructor. */
   util_shader_variant_reference(&opts, &pin, nullptr);
   EXPECT_EQ(tctx.destroy_calls.load(), 1);

}

/* Models the draw_llvm middle-end pattern: a single variant stays pinned
 * across many subsequent inserts that trigger eviction. The pinned variant
 * must survive every eviction even after large numbers of new inserts.
 */
TEST(u_shader_variant_cache, pinned_survives_sustained_eviction)
{
   constexpr unsigned cap = 4;
   constexpr unsigned churn_inserts = 50;

   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;
   opts.cap = cap;

   util_shader_variant_list_init(&list);

   /* Pin a "current" variant, mirroring draw_llvm's pin-slot pattern. */
   uint32_t pinned_key = 0xC0DE;
   struct util_shader_variant *pin = NULL;
   struct util_shader_variant *pinned_base =
      util_shader_variant_get_pinned(&opts, &list, nullptr,
                                     &pinned_key, sizeof(pinned_key),
                                     &pin, nullptr);
   ASSERT_NE(pinned_base, nullptr);
   const struct util_shader_variant *pinned_addr = pinned_base;

   /* Insert many fresh keys, triggering sustained eviction. */
   struct util_shader_variant *transient = NULL;
   for (unsigned i = 0; i < churn_inserts; ++i) {
      uint32_t k = 0x1000 + i;
      struct util_shader_variant *v =
         util_shader_variant_get_pinned(&opts, &list, nullptr,
                                        &k, sizeof(k), &transient, nullptr);
      ASSERT_NE(v, nullptr);
   }
   util_shader_variant_reference(&opts, &transient, nullptr);

   /* The pinned variant must still be in the list (eviction skipped it). */
   EXPECT_EQ(list.count, cap);
   struct util_shader_variant *probe_pin = NULL;
   struct util_shader_variant *probe =
      util_shader_variant_get_pinned(&opts, &list, nullptr,
                                     &pinned_key, sizeof(pinned_key),
                                     &probe_pin, nullptr);
   EXPECT_EQ(probe, pinned_addr);
   util_shader_variant_reference(&opts, &probe_pin, nullptr);

   /* Cleanup. */
   util_shader_variant_reference(&opts, &pin, nullptr);
   util_shader_variant_list_destroy(&opts, &list);
}

/* Stress _get_pinned from multiple threads sharing a cap-bounded cache.
 * Each worker churns its own set of keys through its own pin slot. The
 * atomic get-plus-pin must not let any worker's freshly-fetched variant be
 * evicted before its pin install. Pre-fix this would race on cap > 0
 * caches where eviction walks past the head to find an unpinned variant.
 */
TEST(u_shader_variant_cache, concurrent_get_pinned)
{
   constexpr int num_threads = 8;
   constexpr int num_iters = 6500;
   constexpr int num_keys_per_thread = 4;
   constexpr unsigned cap = 4;

   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;
   opts.cap = cap;

   util_shader_variant_list_init(&list);

   auto worker = [&](int tid) {
      struct util_shader_variant *pin = NULL;
      for (int i = 0; i < num_iters; ++i) {
         uint32_t k = (uint32_t)((tid << 16) | (i % num_keys_per_thread));
         struct util_shader_variant *v =
            util_shader_variant_get_pinned(&opts, &list, nullptr,
                                           &k, sizeof(k), &pin, nullptr);
         ASSERT_NE(v, nullptr);
         EXPECT_EQ(pin, v);
      }
      util_shader_variant_reference(&opts, &pin, nullptr);
   };

   std::vector<std::thread> threads;
   for (int i = 0; i < num_threads; ++i)
      threads.emplace_back(worker, i);

   for (auto &t : threads)
      t.join();

   int compiles = tctx.compile_calls.load();
   int destroys_so_far = tctx.destroy_calls.load();

   /* Every dropped compile + every eviction has fired destroy already; the
    * remainder is what's still on the list when we tear it down.
    */
   util_shader_variant_list_destroy(&opts, &list);
   EXPECT_EQ(tctx.destroy_calls.load(), compiles);
   EXPECT_GT(tctx.destroy_calls.load(), destroys_so_far);

}

TEST(u_shader_variant_cache, get_pinned_same_key_reuses_variant)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.user_data = &tctx;
   opts.cap = 4;

   util_shader_variant_list_init(&list);

   uint32_t k1 = 11, k2 = 22;
   struct util_shader_variant *pin = NULL;

   /* First call: pin empty, miss, compiles. */
   struct util_shader_variant *a =
      util_shader_variant_get_pinned(&opts, &list, nullptr,
                                     &k1, sizeof(k1), &pin, nullptr);
   ASSERT_NE(a, nullptr);
   EXPECT_EQ(tctx.compile_calls.load(), 1);

   /* Second call same key: list lookup hits, compiles nothing. */
   struct util_shader_variant *b =
      util_shader_variant_get_pinned(&opts, &list, nullptr,
                                     &k1, sizeof(k1), &pin, nullptr);
   EXPECT_EQ(b, a);
   EXPECT_EQ(tctx.compile_calls.load(), 1);

   /* Different key: cache miss, compiles. */
   struct util_shader_variant *c =
      util_shader_variant_get_pinned(&opts, &list, nullptr,
                                     &k2, sizeof(k2), &pin, nullptr);
   ASSERT_NE(c, nullptr);
   EXPECT_NE(c, a);
   EXPECT_EQ(tctx.compile_calls.load(), 2);

   util_shader_variant_reference(&opts, &pin, nullptr);
   util_shader_variant_list_destroy(&opts, &list);
}

/* The pairing invariant is checked with assert(), which is a no-op under
 * NDEBUG, so these death tests only apply to debug builds.
 */
#ifndef NDEBUG
TEST(u_shader_variant_cache_death, pair_invariant_hash_only)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.hash = test_hash_first_byte;
   opts.user_data = &tctx;

   util_shader_variant_list_init(&list);

   uint32_t k = 1;
   ASSERT_DEATH(util_shader_variant_get(&opts, &list, nullptr,
                                        &k, sizeof(k), nullptr), "");
}

TEST(u_shader_variant_cache_death, pair_invariant_equal_only)
{
   struct test_ctx tctx;
   struct util_shader_variant_list list;

   struct util_shader_variant_cache_options opts = {};
   opts.compile = test_compile;
   opts.destroy = test_destroy;
   opts.equal = test_equal_first_byte;
   opts.user_data = &tctx;

   util_shader_variant_list_init(&list);

   uint32_t k = 1;
   ASSERT_DEATH(util_shader_variant_get(&opts, &list, nullptr,
                                        &k, sizeof(k), nullptr), "");
}
#endif
