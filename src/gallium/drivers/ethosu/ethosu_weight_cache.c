/*
 * Copyright 2026 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "util/mesa-blake3.h"
#include "util/simple_mtx.h"
#include "ethosu_encode.h"

#define WEIGHT_CACHE_SIZE_LIMIT (64 * 1024 * 1024)

struct weight_cache_key {
   blake3_hash weights_hash;
   uint32_t weights_size;
   uint32_t is_u65;
   uint32_t kernel_depthwise;
   uint32_t kernel_is_signed;
   uint32_t kernel_zero_point;
   uint32_t kernel_height;
   uint32_t kernel_width;
   uint32_t kernel_stride_y;
   uint32_t kernel_stride_x;
   uint32_t kernel_dilation_y;
   uint32_t kernel_dilation_x;
   uint32_t ifm_depth;
   uint32_t ofm_depth;
   uint32_t is_partkernel;
   uint32_t ifm_block_depth;
   uint32_t ofm_block_depth;
   uint32_t ofm_ublock_depth;
};

struct weight_cache_entry {
   struct weight_cache_entry *prev;
   struct weight_cache_entry *next;
   struct weight_cache_key key;
   uint8_t *weights;
   long weights_size;
};

struct weight_cache {
   simple_mtx_t mutex;
   struct weight_cache_entry *head;
   struct weight_cache_entry *tail;
   size_t size;
};

static void
weight_cache_key_init(struct weight_cache_key *key,
                      const struct ethosu_ml_device *device,
                      const struct ethosu_operation *operation,
                      const uint8_t *weights, long weights_size)
{
   memset(key, 0, sizeof(*key));
   _mesa_blake3_compute(weights, weights_size, key->weights_hash);
   key->weights_size = weights_size;
   key->is_u65 = device->is_u65;
   key->kernel_depthwise = operation->kernel.depthwise;
   key->kernel_is_signed = operation->kernel.is_signed;
   key->kernel_zero_point = operation->kernel.zero_point;
   key->kernel_height = operation->kernel.height;
   key->kernel_width = operation->kernel.width;
   key->kernel_stride_y = operation->kernel.stride_y;
   key->kernel_stride_x = operation->kernel.stride_x;
   key->kernel_dilation_y = operation->kernel.dilation_y;
   key->kernel_dilation_x = operation->kernel.dilation_x;
   key->ifm_depth = operation->ifm.shape.depth;
   key->ofm_depth = operation->ofm.shape.depth;
   key->is_partkernel = operation->block_config.is_partkernel;
   key->ifm_block_depth = operation->block_config.ifm_block.depth;
   key->ofm_block_depth = operation->block_config.ofm_block.depth;
   key->ofm_ublock_depth = operation->block_config.ofm_ublock.depth;
}

static void
weight_cache_move_to_front(struct weight_cache *cache,
                           struct weight_cache_entry *entry)
{
   if (cache->head == entry)
      return;

   if (entry->prev)
      entry->prev->next = entry->next;
   if (entry->next)
      entry->next->prev = entry->prev;
   if (cache->tail == entry)
      cache->tail = entry->prev;

   entry->prev = NULL;
   entry->next = cache->head;
   if (cache->head)
      cache->head->prev = entry;
   else
      cache->tail = entry;
   cache->head = entry;
}

static void
weight_cache_remove(struct weight_cache *cache,
                    struct weight_cache_entry *entry)
{
   if (entry->prev)
      entry->prev->next = entry->next;
   else
      cache->head = entry->next;

   if (entry->next)
      entry->next->prev = entry->prev;
   else
      cache->tail = entry->prev;

   cache->size -= entry->weights_size;
   free(entry->weights);
   free(entry);
}

static struct weight_cache *
get_weight_cache(struct ethosu_ml_device *device)
{
   struct weight_cache *cache = device->weight_cache;

   if (cache)
      return cache;

   cache = calloc(1, sizeof(*cache));
   if (cache) {
      simple_mtx_init(&cache->mutex, mtx_plain);
      device->weight_cache = cache;
   }

   return cache;
}

bool
weight_cache_lookup(struct ethosu_ml_device *device,
                    const struct ethosu_operation *operation,
                    const uint8_t *input_weights,
                    long input_weights_size,
                    uint8_t **weights, long *weights_size)
{
   struct weight_cache_key key;
   struct weight_cache *cache = get_weight_cache(device);

   if (!cache)
      return false;

   weight_cache_key_init(&key, device, operation, input_weights,
                         input_weights_size);

   simple_mtx_lock(&cache->mutex);
   for (struct weight_cache_entry *entry = cache->head; entry;
        entry = entry->next) {
      if (memcmp(&key, &entry->key, sizeof(key)))
         continue;

      *weights = malloc(entry->weights_size);
      if (!*weights) {
         simple_mtx_unlock(&cache->mutex);
         return false;
      }

      memcpy(*weights, entry->weights, entry->weights_size);
      *weights_size = entry->weights_size;
      weight_cache_move_to_front(cache, entry);
      simple_mtx_unlock(&cache->mutex);
      return true;
   }

   simple_mtx_unlock(&cache->mutex);
   return false;
}

void
weight_cache_insert(struct ethosu_ml_device *device,
                    const struct ethosu_operation *operation,
                    const uint8_t *input_weights,
                    long input_weights_size,
                    const uint8_t *weights, long weights_size)
{
   struct weight_cache_key key;
   struct weight_cache *cache;
   struct weight_cache_entry *entry;

   if (!weights_size || (size_t)weights_size > WEIGHT_CACHE_SIZE_LIMIT)
      return;

   cache = get_weight_cache(device);
   if (!cache)
      return;

   weight_cache_key_init(&key, device, operation, input_weights,
                         input_weights_size);

   simple_mtx_lock(&cache->mutex);
   for (entry = cache->head; entry; entry = entry->next) {
      if (!memcmp(&key, &entry->key, sizeof(key))) {
         weight_cache_move_to_front(cache, entry);
         simple_mtx_unlock(&cache->mutex);
         return;
      }
   }

   entry = calloc(1, sizeof(*entry));
   if (!entry) {
      simple_mtx_unlock(&cache->mutex);
      return;
   }

   entry->weights = malloc(weights_size);
   if (!entry->weights) {
      free(entry);
      simple_mtx_unlock(&cache->mutex);
      return;
   }

   memcpy(entry->weights, weights, weights_size);
   entry->key = key;
   entry->weights_size = weights_size;

   while (cache->tail && cache->size + weights_size > WEIGHT_CACHE_SIZE_LIMIT)
      weight_cache_remove(cache, cache->tail);

   entry->next = cache->head;
   if (cache->head)
      cache->head->prev = entry;
   else
      cache->tail = entry;
   cache->head = entry;
   cache->size += weights_size;
   simple_mtx_unlock(&cache->mutex);
}

void
ethosu_weight_cache_destroy(struct ethosu_ml_device *device)
{
   struct weight_cache *cache = device->weight_cache;

   if (!cache)
      return;

   while (cache->head)
      weight_cache_remove(cache, cache->head);

   simple_mtx_destroy(&cache->mutex);
   free(cache);
   device->weight_cache = NULL;
}
