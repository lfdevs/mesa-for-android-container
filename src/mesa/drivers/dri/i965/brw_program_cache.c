/*
 Copyright (C) Intel Corp.  2006.  All Rights Reserved.
 Intel funded Tungsten Graphics to
 develop this 3D driver.

 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the
 "Software"), to deal in the Software without restriction, including
 without limitation the rights to use, copy, modify, merge, publish,
 distribute, sublicense, and/or sell copies of the Software, and to
 permit persons to whom the Software is furnished to do so, subject to
 the following conditions:

 The above copyright notice and this permission notice (including the
 next paragraph) shall be included in all copies or substantial
 portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 **********************************************************************/
 /*
  * Authors:
  *   Keith Whitwell <keithw@vmware.com>
  */

/** @file brw_program_cache.c
 *
 * This file implements a simple program cache for 965.  The consumers can
 *  query the hash table of programs using a cache_id and program key, and
 * receive the corresponding program buffer object (plus associated auxiliary
 *  data) in return.  Objects in the cache may not have relocations
 * (pointers to other BOs) in them.
 *
 * The inner workings are a simple hash table based on a CRC of the
 * key data.
 *
 * Replacement is not implemented.  Instead, when the cache gets too
 * big we throw out all of the cache data and let it get regenerated.
 */

#include "main/imports.h"
#include "intel_batchbuffer.h"
#include "brw_state.h"
#include "brw_vs.h"
#include "brw_wm.h"
#include "brw_gs.h"
#include "brw_cs.h"
#include "brw_program.h"

#define FILE_DEBUG_FLAG DEBUG_STATE

struct brw_cache_item {
   /**
    * Effectively part of the key, cache_id identifies what kind of state
    * buffer is involved, and also which dirty flag should set.
    */
   enum brw_cache_id cache_id;

   /** 32-bit hash of the key data */
   GLuint hash;

   /** for variable-sized keys */
   GLuint key_size;
   GLuint aux_size;
   const void *key;

   uint32_t offset;
   uint32_t size;

   struct brw_cache_item *next;
};

static unsigned
get_program_string_id(enum brw_cache_id cache_id, const void *key)
{
   switch (cache_id) {
   case BRW_CACHE_VS_PROG:
      return ((struct brw_vs_prog_key *) key)->program_string_id;
   case BRW_CACHE_TCS_PROG:
      return ((struct brw_tcs_prog_key *) key)->program_string_id;
   case BRW_CACHE_TES_PROG:
      return ((struct brw_tes_prog_key *) key)->program_string_id;
   case BRW_CACHE_GS_PROG:
      return ((struct brw_gs_prog_key *) key)->program_string_id;
   case BRW_CACHE_CS_PROG:
      return ((struct brw_cs_prog_key *) key)->program_string_id;
   case BRW_CACHE_FS_PROG:
      return ((struct brw_wm_prog_key *) key)->program_string_id;
   default:
      unreachable("no program string id for this kind of program");
   }
}

static GLuint
hash_key(struct brw_cache_item *item)
{
   GLuint *ikey = (GLuint *)item->key;
   GLuint hash = item->cache_id, i;

   assert(item->key_size % 4 == 0);

   /* I'm sure this can be improved on:
    */
   for (i = 0; i < item->key_size/4; i++) {
      hash ^= ikey[i];
      hash = (hash << 5) | (hash >> 27);
   }

   return hash;
}

static int
brw_cache_item_equals(const struct brw_cache_item *a,
                      const struct brw_cache_item *b)
{
   return a->cache_id == b->cache_id &&
      a->hash == b->hash &&
      a->key_size == b->key_size &&
      (memcmp(a->key, b->key, a->key_size) == 0);
}

static struct brw_cache_item *
search_cache(struct brw_cache *cache, GLuint hash,
             struct brw_cache_item *lookup)
{
   struct brw_cache_item *c;

#if 0
   int bucketcount = 0;

   for (c = cache->items[hash % cache->size]; c; c = c->next)
      bucketcount++;

   fprintf(stderr, "bucket %d/%d = %d/%d items\n", hash % cache->size,
           cache->size, bucketcount, cache->n_items);
#endif

   for (c = cache->items[hash % cache->size]; c; c = c->next) {
      if (brw_cache_item_equals(lookup, c))
         return c;
   }

   return NULL;
}


static void
rehash(struct brw_cache *cache)
{
   struct brw_cache_item **items;
   struct brw_cache_item *c, *next;
   GLuint size, i;

   size = cache->size * 3;
   items = calloc(size, sizeof(*items));

   for (i = 0; i < cache->size; i++)
      for (c = cache->items[i]; c; c = next) {
         next = c->next;
         c->next = items[c->hash % size];
         items[c->hash % size] = c;
      }

   free(cache->items);
   cache->items = items;
   cache->size = size;
}


/**
 * Returns the buffer object matching cache_id and key, or NULL.
 */
bool
brw_search_cache(struct brw_cache *cache,
                 enum brw_cache_id cache_id,
                 const void *key, GLuint key_size,
                 uint32_t *inout_offset, void *inout_aux)
{
   struct brw_context *brw = cache->brw;
   struct brw_cache_item *item;
   struct brw_cache_item lookup;
   GLuint hash;

   lookup.cache_id = cache_id;
   lookup.key = key;
   lookup.key_size = key_size;
   hash = hash_key(&lookup);
   lookup.hash = hash;

   item = search_cache(cache, hash, &lookup);

   if (item == NULL)
      return false;

   void *aux = ((char *) item->key) + item->key_size;

   if (item->offset != *inout_offset || aux != *((void **) inout_aux)) {
      brw->ctx.NewDriverState |= (1 << cache_id);
      *inout_offset = item->offset;
      *((void **) inout_aux) = aux;
   }

   return true;
}

static void
brw_cache_new_bo(struct brw_cache *cache, uint32_t new_size)
{
   struct brw_context *brw = cache->brw;
   drm_intel_bo *new_bo;

   new_bo = drm_intel_bo_alloc(brw->bufmgr, "program cache", new_size, 64);
   if (brw->has_llc)
      drm_intel_gem_bo_map_unsynchronized(new_bo);

   /* Copy any existing data that needs to be saved. */
   if (cache->next_offset != 0) {
      if (brw->has_llc) {
         memcpy(new_bo->virtual, cache->bo->virtual, cache->next_offset);
      } else {
         drm_intel_bo_map(cache->bo, false);
         drm_intel_bo_subdata(new_bo, 0, cache->next_offset,
                              cache->bo->virtual);
         drm_intel_bo_unmap(cache->bo);
      }
   }

   if (brw->has_llc)
      drm_intel_bo_unmap(cache->bo);
   drm_intel_bo_unreference(cache->bo);
   cache->bo = new_bo;
   cache->bo_used_by_gpu = false;

   /* Since we have a new BO in place, we need to signal the units
    * that depend on it (state base address on gen5+, or unit state before).
    */
   brw->ctx.NewDriverState |= BRW_NEW_PROGRAM_CACHE;
   brw->batch.state_base_address_emitted = false;
}

/**
 * Attempts to find an item in the cache with identical data.
 */
static const struct brw_cache_item *
brw_lookup_prog(const struct brw_cache *cache,
                enum brw_cache_id cache_id,
                const void *data, unsigned data_size)
{
   const struct brw_context *brw = cache->brw;
   unsigned i;
   const struct brw_cache_item *item;

   for (i = 0; i < cache->size; i++) {
      for (item = cache->items[i]; item; item = item->next) {
         int ret;

         if (item->cache_id != cache_id || item->size != data_size)
            continue;

         if (!brw->has_llc)
            drm_intel_bo_map(cache->bo, false);
         ret = memcmp(cache->bo->virtual + item->offset, data, item->size);
         if (!brw->has_llc)
            drm_intel_bo_unmap(cache->bo);
         if (ret)
            continue;

         return item;
      }
   }

   return NULL;
}

static uint32_t
brw_alloc_item_data(struct brw_cache *cache, uint32_t size)
{
   uint32_t offset;
   struct brw_context *brw = cache->brw;

   /* Allocate space in the cache BO for our new program. */
   if (cache->next_offset + size > cache->bo->size) {
      uint32_t new_size = cache->bo->size * 2;

      while (cache->next_offset + size > new_size)
         new_size *= 2;

      brw_cache_new_bo(cache, new_size);
   }

   /* If we would block on writing to an in-use program BO, just
    * recreate it.
    */
   if (!brw->has_llc && cache->bo_used_by_gpu) {
      perf_debug("Copying busy program cache buffer.\n");
      brw_cache_new_bo(cache, cache->bo->size);
   }

   offset = cache->next_offset;

   /* Programs are always 64-byte aligned, so set up the next one now */
   cache->next_offset = ALIGN(offset + size, 64);

   return offset;
}

const void *
brw_find_previous_compile(struct brw_cache *cache,
                          enum brw_cache_id cache_id,
                          unsigned program_string_id)
{
   for (unsigned i = 0; i < cache->size; i++) {
      for (struct brw_cache_item *c = cache->items[i]; c; c = c->next) {
         if (c->cache_id == cache_id &&
             get_program_string_id(cache_id, c->key) == program_string_id) {
            return c->key;
         }
      }
   }

   return NULL;
}

void
brw_upload_cache(struct brw_cache *cache,
                 enum brw_cache_id cache_id,
                 const void *key,
                 GLuint key_size,
                 const void *data,
                 GLuint data_size,
                 const void *aux,
                 GLuint aux_size,
                 uint32_t *out_offset,
                 void *out_aux)
{
   struct brw_context *brw = cache->brw;
   struct brw_cache_item *item = CALLOC_STRUCT(brw_cache_item);
   const struct brw_cache_item *matching_data =
      brw_lookup_prog(cache, cache_id, data, data_size);
   GLuint hash;
   void *tmp;

   item->cache_id = cache_id;
   item->size = data_size;
   item->key = key;
   item->key_size = key_size;
   item->aux_size = aux_size;
   hash = hash_key(item);
   item->hash = hash;

   /* If we can find a matching prog in the cache already, then reuse the
    * existing stuff without creating new copy into the underlying buffer
    * object. This is notably useful for programs generating shaders at
    * runtime, where multiple shaders may compile to the same thing in our
    * backend.
    */
   if (matching_data) {
      item->offset = matching_data->offset;
   } else {
      item->offset = brw_alloc_item_data(cache, data_size);

      /* Copy data to the buffer */
      if (brw->has_llc) {
         memcpy((char *)cache->bo->virtual + item->offset, data, data_size);
      } else {
         drm_intel_bo_subdata(cache->bo, item->offset, data_size, data);
      }
   }

   /* Set up the memory containing the key and aux_data */
   tmp = malloc(key_size + aux_size);

   memcpy(tmp, key, key_size);
   memcpy(tmp + key_size, aux, aux_size);

   item->key = tmp;

   if (cache->n_items > cache->size * 1.5f)
      rehash(cache);

   hash %= cache->size;
   item->next = cache->items[hash];
   cache->items[hash] = item;
   cache->n_items++;

   *out_offset = item->offset;
   *(void **)out_aux = (void *)((char *)item->key + item->key_size);
   cache->brw->ctx.NewDriverState |= 1 << cache_id;
}

void
brw_init_caches(struct brw_context *brw)
{
   struct brw_cache *cache = &brw->cache;

   cache->brw = brw;

   cache->size = 7;
   cache->n_items = 0;
   cache->items =
      calloc(cache->size, sizeof(struct brw_cache_item *));

   cache->bo = drm_intel_bo_alloc(brw->bufmgr, "program cache",  4096, 64);
   if (brw->has_llc)
      drm_intel_gem_bo_map_unsynchronized(cache->bo);
}

static void
brw_clear_cache(struct brw_context *brw, struct brw_cache *cache)
{
   struct brw_cache_item *c, *next;
   GLuint i;

   DBG("%s\n", __func__);

   for (i = 0; i < cache->size; i++) {
      for (c = cache->items[i]; c; c = next) {
         next = c->next;
         if (c->cache_id == BRW_CACHE_VS_PROG ||
             c->cache_id == BRW_CACHE_TCS_PROG ||
             c->cache_id == BRW_CACHE_TES_PROG ||
             c->cache_id == BRW_CACHE_GS_PROG ||
             c->cache_id == BRW_CACHE_FS_PROG ||
             c->cache_id == BRW_CACHE_CS_PROG) {
            const void *item_aux = c->key + c->key_size;
            brw_stage_prog_data_free(item_aux);
         }
         free((void *)c->key);
         free(c);
      }
      cache->items[i] = NULL;
   }

   cache->n_items = 0;

   /* Start putting programs into the start of the BO again, since
    * we'll never find the old results.
    */
   cache->next_offset = 0;

   /* We need to make sure that the programs get regenerated, since
    * any offsets leftover in brw_context will no longer be valid.
    */
   brw->NewGLState = ~0;
   brw->ctx.NewDriverState = ~0ull;
   brw->state.pipelines[BRW_RENDER_PIPELINE].mesa = ~0;
   brw->state.pipelines[BRW_RENDER_PIPELINE].brw = ~0ull;
   brw->state.pipelines[BRW_COMPUTE_PIPELINE].mesa = ~0;
   brw->state.pipelines[BRW_COMPUTE_PIPELINE].brw = ~0ull;

   /* Also, NULL out any stale program pointers. */
   brw->vs.base.prog_data = NULL;
   brw->tcs.base.prog_data = NULL;
   brw->tes.base.prog_data = NULL;
   brw->gs.base.prog_data = NULL;
   brw->wm.base.prog_data = NULL;
   brw->cs.base.prog_data = NULL;

   intel_batchbuffer_flush(brw);
}

void
brw_program_cache_check_size(struct brw_context *brw)
{
   /* un-tuned guess.  Each object is generally a page, so 2000 of them is 8 MB of
    * state cache.
    */
   if (brw->cache.n_items > 2000) {
      perf_debug("Exceeded state cache size limit.  Clearing the set "
                 "of compiled programs, which will trigger recompiles\n");
      brw_clear_cache(brw, &brw->cache);
   }
}


static void
brw_destroy_cache(struct brw_context *brw, struct brw_cache *cache)
{

   DBG("%s\n", __func__);

   if (brw->has_llc)
      drm_intel_bo_unmap(cache->bo);
   drm_intel_bo_unreference(cache->bo);
   cache->bo = NULL;
   brw_clear_cache(brw, cache);
   free(cache->items);
   cache->items = NULL;
   cache->size = 0;
}


void
brw_destroy_caches(struct brw_context *brw)
{
   brw_destroy_cache(brw, &brw->cache);
}

static const char *
cache_name(enum brw_cache_id cache_id)
{
   switch (cache_id) {
   case BRW_CACHE_VS_PROG:
      return "VS kernel";
   case BRW_CACHE_TCS_PROG:
      return "TCS kernel";
   case BRW_CACHE_TES_PROG:
      return "TES kernel";
   case BRW_CACHE_FF_GS_PROG:
      return "Fixed-function GS kernel";
   case BRW_CACHE_GS_PROG:
      return "GS kernel";
   case BRW_CACHE_CLIP_PROG:
      return "CLIP kernel";
   case BRW_CACHE_SF_PROG:
      return "SF kernel";
   case BRW_CACHE_FS_PROG:
      return "FS kernel";
   case BRW_CACHE_CS_PROG:
      return "CS kernel";
   default:
      return "unknown";
   }
}

void
brw_print_program_cache(struct brw_context *brw)
{
   const struct brw_cache *cache = &brw->cache;
   struct brw_cache_item *item;

   if (!brw->has_llc)
      drm_intel_bo_map(cache->bo, false);

   for (unsigned i = 0; i < cache->size; i++) {
      for (item = cache->items[i]; item; item = item->next) {
         fprintf(stderr, "%s:\n", cache_name(i));
         brw_disassemble(&brw->screen->devinfo, cache->bo->virtual,
                         item->offset, item->size, stderr);
      }
   }

   if (!brw->has_llc)
      drm_intel_bo_unmap(cache->bo);
}
