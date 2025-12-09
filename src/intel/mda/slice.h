/*
 * Copyright 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef MDA_SLICE_H
#define MDA_SLICE_H

#include <stdbool.h>
#include <stdint.h>

#include "util/hash_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Non-owning string slice.  Makes convenient to refer to parts of an existing
 * buffer instead of duplicating into new strings.
 */
typedef struct slice {
   const char *data;
   int len;
} slice;

/* To be used when printf formatting pattern "%.*s". */
#define SLICE_FMT(s) (s).len, (s).data

slice slice_from_cstr(const char *str);

bool slice_is_empty(slice s);
bool slice_equal(slice a, slice b);
bool slice_equal_cstr(slice s, const char *cstr);
bool slice_contains_str(slice s, slice needle);
bool slice_starts_with(slice s, slice prefix);
bool slice_ends_with(slice s, slice suffix);

char *slice_to_cstr(void *mem_ctx, slice s);

slice slice_find_char(slice s, char c);
slice slice_find_str(slice s, slice needle);

slice slice_strip_prefix(slice s, slice prefix);
slice slice_substr_from(slice s, int start);
slice slice_substr_to(slice s, int end);
slice slice_substr(slice s, int start, int end);

typedef struct slice_cut_result {
   slice before;
   slice after;
   bool found;
} slice_cut_result;

slice_cut_result slice_cut(slice s, char c);
slice_cut_result slice_cut_n(slice s, char c, int n);

/* Hash table support.
 *
 * Mesa src/util/hash_table.h has support for keys up to pointer
 * size, so a slice by itself can't be stored directly in the
 * same way a number would.  So the functions below will use
 * pointers to slices, but ensure that when _stored_ as keys,
 * a copy of the slice itself will be made and owned by the
 * hash table.
 *
 * Note that the contents of the slices themselves are not
 * owned by the slices, so also not by the hash table.
 */

struct hash_table *slice_hash_table_create(void *mem_ctx);
struct hash_entry *slice_hash_table_insert(struct hash_table *ht, slice key, void *data);

static inline struct hash_entry *slice_hash_table_search(struct hash_table *ht, slice key) {
   return _mesa_hash_table_search(ht, &key);
}

#ifdef __cplusplus
}
#endif

#endif /* MDA_SLICE_H */
