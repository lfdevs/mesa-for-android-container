/*
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

/*
 * Generic per-shader cache of compiled shader variants.
 *
 * A "variant" is a compiled specialization of a shader, keyed on
 * runtime spec state (e.g. bound vertex formats, rasterizer clip flags).
 * Keys are opaque bytes; variants are opaque pointers; the compile step
 * is a driver callback.
 *
 * Embedding:
 *  - util_shader_variant_cache_options: one per cache scope, embedded by
 *    the caller at whatever scope should share variants. The list always
 *    stays per shader.
 *  - util_shader_variant_list: one per shader that holds variants.
 *  - Driver variants embed struct util_shader_variant and map back to
 *    the containing struct with container_of().
 *
 * Lifetime: variants are refcounted. The cache holds one ref per variant
 * on the list. util_shader_variant_get returns a borrowed pointer with
 * no extra ref - the variant stays alive as long as the list does.
 *
 * Per-list cap drives LRU eviction inside _get. An evictable variant is
 * one whose refcount is exactly 1 (held by the cache only). To keep a
 * variant alive across cap-driven eviction, callers grab a ref via
 * util_shader_variant_reference into a pin slot. Pinned variants
 * (refcount > 1) are skipped by eviction.
 *
 * util_shader_variant_list_destroy drops the cache's ref on every variant
 * in the list. Variants whose only ref was the cache get destroyed inline.
 * Variants pinned by a peer survive off-list as standalone heap objects
 * and are destroyed by the last util_shader_variant_reference drop.
 *
 * Custom key hash and equality: drivers with a hand-rolled fast-path
 * comparison can supply a hash + equal callback pair in the options. The
 * util uses them in place of _mesa_hash_data and memcmp. Both must be set
 * together or both NULL — the lookup path asserts otherwise, since a
 * mismatched hash would drop matches the custom equality accepts.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "util/list.h"
#include "util/simple_mtx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Refcount on util_shader_variant. Plain atomic refcount; the field is
 * touched only via util_shader_variant_reference and the cache internals.
 */
struct util_shader_variant_ref {
   int32_t count;   /* atomic */
};

struct util_shader_variant {
   struct util_shader_variant_ref reference;
   struct list_head link;
   /* internal: written by the cache, do not touch from drivers */
   uint32_t key_hash;
   void *key;
};

struct util_shader_variant_list {
   struct list_head variants;
   /* internal */
   simple_mtx_t lock;
   unsigned count;
};

typedef struct util_shader_variant *
(*util_shader_variant_compile_cb)(void *user_data,
                                  void *cso,
                                  const void *key);

typedef void
(*util_shader_variant_destroy_cb)(struct util_shader_variant *variant);

typedef uint32_t (*util_shader_variant_hash_cb)(const void *key);

typedef bool (*util_shader_variant_equal_cb)(const void *a, const void *b);

struct util_shader_variant_cache_options {
   util_shader_variant_compile_cb compile;   /* required */
   util_shader_variant_destroy_cb destroy;   /* required */
   util_shader_variant_hash_cb hash;
   util_shader_variant_equal_cb equal;
   void *user_data;
   /* Per-list cap. 0 = unbounded. Applies to every list this cache governs:
    * eviction inside _get walks the LRU end of the list and drops the first
    * variant with refcount == 1 (cache-only) when count exceeds cap.
    */
   unsigned cap;
};

void
util_shader_variant_list_init(struct util_shader_variant_list *list);

void
util_shader_variant_list_destroy(const struct util_shader_variant_cache_options *options,
                                 struct util_shader_variant_list *list);

/* Look up or compile a variant. If was_cache_miss is non-NULL, it is
 * set to true if this call compiled a fresh variant and false if an
 * existing variant was found (or returned via dedup-on-publish after a
 * concurrent peer compiled the same key).
 *
 * The returned pointer is borrowed - the cache keeps the variant alive
 * on the list for the lifetime of the owning shader. Only valid when
 * options->cap == 0 (no eviction); asserted. For cap > 0 caches,
 * use util_shader_variant_get_pinned instead.
 */
struct util_shader_variant *
util_shader_variant_get(const struct util_shader_variant_cache_options *options,
                        struct util_shader_variant_list *list,
                        void *cso,
                        const void *key,
                        size_t key_size,
                        bool *was_cache_miss);

/* Atomic look-up-or-compile + pin install. Equivalent in effect to
 *   base = util_shader_variant_get(...);
 *   util_shader_variant_reference(options, pin, base);
 * but the lookup and the ref bump happen under the same list lock, so a
 * concurrent _get_pinned cannot evict our variant in the window between
 * the lookup and the pin. Use this whenever options->cap > 0.
 *
 * On failure returns NULL and leaves *pin unchanged (caller can still
 * unbind the previous variant later).
 */
struct util_shader_variant *
util_shader_variant_get_pinned(const struct util_shader_variant_cache_options *options,
                               struct util_shader_variant_list *list,
                               void *cso,
                               const void *key,
                               size_t key_size,
                               struct util_shader_variant **pin,
                               bool *was_cache_miss);

/* Bind/unbind helper, mirroring pipe_resource_reference semantics.
 * Drops *dst's ref, bumps src's ref, stores src in *dst. When the last
 * ref drops, frees the variant via options->destroy.
 */
void
util_shader_variant_reference(const struct util_shader_variant_cache_options *options,
                              struct util_shader_variant **dst,
                              struct util_shader_variant *src);

#ifdef __cplusplus
}
#endif
