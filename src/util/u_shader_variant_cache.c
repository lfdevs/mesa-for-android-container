/*
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "util/u_shader_variant_cache.h"

#include "util/hash_table.h"
#include "util/u_atomic.h"
#include "util/u_memory.h"

#include <assert.h>
#include <string.h>

static bool
key_equal(const struct util_shader_variant_cache_options *options,
          const void *a, const void *b, size_t key_size)
{
   return options->equal ? options->equal(a, b)
                         : memcmp(a, b, key_size) == 0;
}

static struct util_shader_variant *
find_locked(const struct util_shader_variant_cache_options *options,
            struct util_shader_variant_list *list,
            uint32_t hash,
            const void *key,
            size_t key_size)
{
   struct util_shader_variant *v;

   LIST_FOR_EACH_ENTRY(v, &list->variants, link) {
      if (v->key_hash != hash)
         continue;

      if (key_equal(options, v->key, key, key_size))
         return v;
   }

   return NULL;
}

static void
destroy_variant(const struct util_shader_variant_cache_options *options,
                struct util_shader_variant *v)
{
   FREE(v->key);
   options->destroy(v);
}

void
util_shader_variant_list_init(struct util_shader_variant_list *list)
{
   simple_mtx_init(&list->lock, mtx_plain);
   list_inithead(&list->variants);
   list->count = 0;
}

static bool
variant_ref_dec(struct util_shader_variant *v)
{
   return p_atomic_dec_zero(&v->reference.count);
}

static void
variant_ref_inc(struct util_shader_variant *v)
{
   ASSERTED int32_t count = p_atomic_inc_return(&v->reference.count);
   assert(count > 1); /* must already be referenced */
}

void
util_shader_variant_reference(const struct util_shader_variant_cache_options *options,
                              struct util_shader_variant **dst,
                              struct util_shader_variant *src)
{
   struct util_shader_variant *old = *dst;

   if (src)
      variant_ref_inc(src);

   if (old && variant_ref_dec(old))
      destroy_variant(options, old);

   *dst = src;
}

void
util_shader_variant_list_destroy(const struct util_shader_variant_cache_options *options,
                                 struct util_shader_variant_list *list)
{
   simple_mtx_lock(&list->lock);

   struct list_head victims;
   list_inithead(&victims);

   struct util_shader_variant *v, *next;
   LIST_FOR_EACH_ENTRY_SAFE(v, next, &list->variants, link) {
      list_del(&v->link);
      list->count--;

      if (variant_ref_dec(v))
         list_addtail(&v->link, &victims);
      /* else: pinned by a peer. Variant survives off-list as a
       * standalone object, the last util_shader_variant_reference drop
       * will free it.
       */
   }

   simple_mtx_unlock(&list->lock);

   LIST_FOR_EACH_ENTRY_SAFE(v, next, &victims, link) {
      destroy_variant(options, v);
   }

   simple_mtx_destroy(&list->lock);
}

/* Shared workhorse. When pin is true, the returned variant's refcount is
 * bumped to (cache + 1) under list->lock so the caller can install it into
 * a pin slot atomically vs. eviction. When pin is false, the bare cache ref
 * is returned - safe only with cap == 0 (no eviction).
 */
static struct util_shader_variant *
get_internal(const struct util_shader_variant_cache_options *options,
             struct util_shader_variant_list *list,
             void *cso,
             const void *key,
             size_t key_size,
             bool pin,
             bool *was_cache_miss)
{
   assert((options->hash == NULL) == (options->equal == NULL));

   if (was_cache_miss)
      *was_cache_miss = false;

   const uint32_t hash = options->hash
      ? options->hash(key)
      : _mesa_hash_data(key, key_size);

   simple_mtx_lock(&list->lock);
   struct util_shader_variant *v =
      find_locked(options, list, hash, key, key_size);
   if (v) {
      if (pin)
         variant_ref_inc(v);

      /* MRU-on-hit: only relevant when eviction is enabled. Skip the
       * link writes if already at the head.
       */
      if (options->cap && list->variants.next != &v->link)
         list_move_to(&v->link, &list->variants);
   }
   simple_mtx_unlock(&list->lock);
   if (v)
      return v;

   /* Allocate the key copy before calling compile so a later OOM
    * never leaves a compiled variant half-initialized.
    */
   void *key_copy = mem_dup(key, key_size);
   if (!key_copy)
      return NULL;

   /* Compile outside the lock. */
   struct util_shader_variant *new_v =
      options->compile(options->user_data, cso, key);
   if (!new_v) {
      FREE(key_copy);
      return NULL;
   }

   new_v->key_hash = hash;
   new_v->key = key_copy;
   new_v->reference.count = pin ? 2 : 1;

   simple_mtx_lock(&list->lock);
   struct util_shader_variant *dup =
      find_locked(options, list, hash, key, key_size);
   if (dup) {
      /* Lost the race. Throw our compile away, return the winner. */
      if (pin)
         variant_ref_inc(dup);

      simple_mtx_unlock(&list->lock);
      destroy_variant(options, new_v);
      return dup;
   }

   list_add(&new_v->link, &list->variants);
   list->count++;

   /* Soft cap: walk LRU end, drop first cache-only variant. If all are
    * pinned, eviction is a no-op and the list grows past cap.
    */
   const unsigned cap = options->cap;
   struct util_shader_variant *victim = NULL;

   if (cap && list->count > cap) {
      list_for_each_entry_rev(struct util_shader_variant, cand,
                              &list->variants, link) {
         if (p_atomic_read(&cand->reference.count) != 1)
            continue;

         list_del(&cand->link);
         list->count--;
         victim = cand;
         break;
      }
   }
   simple_mtx_unlock(&list->lock);

   if (victim)
      destroy_variant(options, victim);

   if (was_cache_miss)
      *was_cache_miss = true;

   return new_v;
}

struct util_shader_variant *
util_shader_variant_get(const struct util_shader_variant_cache_options *options,
                        struct util_shader_variant_list *list,
                        void *cso,
                        const void *key,
                        size_t key_size,
                        bool *was_cache_miss)
{
   assert(options->cap == 0); /* see header */

   return get_internal(options, list, cso, key, key_size, false, was_cache_miss);
}

struct util_shader_variant *
util_shader_variant_get_pinned(const struct util_shader_variant_cache_options *options,
                               struct util_shader_variant_list *list,
                               void *cso,
                               const void *key,
                               size_t key_size,
                               struct util_shader_variant **pin,
                               bool *was_cache_miss)
{
   /* No key-based fast path on the pin slot here: pin slots outlive
    * shader binds and keys carry no shader identity, so a key match
    * against *pin can alias a different shader's variant. The lookup
    * must always go through the per-shader list.
    */
   struct util_shader_variant *cur = *pin;

   struct util_shader_variant *v =
      get_internal(options, list, cso, key, key_size, true, was_cache_miss);
   if (!v)
      return NULL; /* old pin retained, caller can still unbind it */

   *pin = v;

   if (cur && variant_ref_dec(cur))
      destroy_variant(options, cur);

   return v;
}
