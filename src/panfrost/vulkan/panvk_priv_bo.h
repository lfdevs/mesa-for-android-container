/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_PRIV_BO_H
#define PANVK_PRIV_BO_H

#include <vulkan/vulkan_core.h>

#include "util/list.h"
#include "util/u_atomic.h"

struct panvk_kmod_bo;
struct panvk_pool;

/* csf-v11 223: driver memory accounting (PANVK_DEBUG=mem). Each private BO
 * is counted under one kind; the counters are process-wide (one VkDevice per
 * game process in practice) and always kept, the report is opt-in. */
enum panvk_mem_kind {
   PANVK_MEM_MISC = 0,
   PANVK_MEM_DEV_POOL,  /* device rw / rw_nc pools */
   PANVK_MEM_SHADER,    /* device exec pool */
   PANVK_MEM_CMD_CS,
   PANVK_MEM_CMD_DESC,
   PANVK_MEM_CMD_TLS,   /* per-command-buffer TLS pool */
   PANVK_MEM_DEV_TLS,   /* device-wide TLS (csf-v11 097) */
   PANVK_MEM_DESC_POOL,
   PANVK_MEM_BC_RAW,    /* BC raw stores (csf-v11 201) */
   PANVK_MEM_UTRACE,
   PANVK_MEM_OTHER_POOL, /* other panvk_pool slabs (JM varying, GS) */
   PANVK_MEM_KIND_COUNT,
};

struct panvk_mem_stats {
   uint64_t bytes[PANVK_MEM_KIND_COUNT];
   uint64_t peak[PANVK_MEM_KIND_COUNT];
   uint64_t count[PANVK_MEM_KIND_COUNT];
   /* kbase tiler heaps (csf/panvk_vX_gpu_queue.c): contexts alive (each
    * commits its initial chunks; growth is not visible to user space),
    * bytes committed at HEAP_INIT, renewals, TERMs done on the submitting
    * thread because the heap thread was behind. */
   uint64_t heap_ctx_live, heap_ctx_peak, heap_init_B, heap_renewals,
      heap_term_sync, heap_term_queued_max;
   /* BC raw store events (panvk_image.c, panvk_vX_cmd_meta.c). */
   uint64_t bc_spills, bc_remats, bc_pins, bc_lazy_skips, bc_stale_marks,
      bc_stale_reads;
   /* Command pool auto-trim (panvk_cmd_pool.c): BOs and bytes freed. */
   uint64_t cmdpool_trim_bos, cmdpool_trim_B;
   /* Device TLS BOs freed early (csf-v11 097 grow-only list). */
   uint64_t dev_tls_freed_B;
   /* Device slab caches (panvk_mempool.c): slabs and bytes idle in them
    * (counted in their kind too), slabs they handed out, slabs and bytes
    * their window trim freed. */
   uint64_t slab_cache_count, slab_cache_B, slab_cache_hits,
      slab_cache_freed_bos, slab_cache_freed_B;
   /* PANVK_KBASE_EVICTABLE: cached slabs marked evictable (and their
    * bytes), taken back, and freed because they could not be taken back. */
   uint64_t slab_evict_count, slab_evict_B, slab_evict_reused,
      slab_evict_lost;
};

extern struct panvk_mem_stats panvk_mem_stats;

static inline void
panvk_mem_stat_add(uint64_t *v, uint64_t d)
{
   p_atomic_add(v, d);
}

/* Used for internal object allocation. */
struct panvk_priv_bo {
   struct list_head node;
   uint64_t refcnt;
   struct panvk_device *dev;
   struct pan_kmod_bo *bo;
   struct {
      uint64_t dev;
      void *host;
   } addr;
   /* enum panvk_mem_kind (csf-v11 223). */
   uint8_t mem_kind;
   /* Window of its slab cache in which the BO was marked evictable. */
   uint32_t cache_window;
   /* csf-v11 230: when non-zero, addr.dev is a kbase MEM_ALIAS region of
    * this many bytes over the BO (sparse device TLS), unmapped on destroy. */
   uint64_t alias_size;
};

VkResult panvk_priv_bo_create(struct panvk_device *dev, uint64_t size,
                              uint32_t flags, VkSystemAllocationScope scope,
                              struct panvk_priv_bo **out);

/* csf-v11 223: same, counted under kind. */
VkResult panvk_priv_bo_create_kind(struct panvk_device *dev, uint64_t size,
                                   uint32_t flags,
                                   VkSystemAllocationScope scope,
                                   struct panvk_priv_bo **out,
                                   enum panvk_mem_kind kind);

/* csf-v11 223: a panvk_pool slab, counted by the pool's label. */
VkResult panvk_priv_bo_create_pool(struct panvk_pool *pool, uint64_t size,
                                   uint32_t flags,
                                   VkSystemAllocationScope scope,
                                   struct panvk_priv_bo **out);

/* csf-v11 223: PANVK_DEBUG=mem / PANVK_MEM_LOG. Writes one summary line at
 * most every PANVK_MEM_LOG_MS (default 10000) ms; final forces a line. */
bool panvk_mem_report_enabled(void);
void panvk_mem_report(struct panvk_device *dev, bool final);

static inline struct panvk_priv_bo *
panvk_priv_bo_ref(struct panvk_priv_bo *bo)
{
   assert(p_atomic_read(&bo->refcnt) > 0);
   p_atomic_inc(&bo->refcnt);
   return bo;
}

void panvk_priv_bo_flush(struct panvk_priv_bo *bo, size_t offset, size_t size);
void panvk_priv_bo_invalidate(struct panvk_priv_bo *bo, size_t offset, size_t size);

void panvk_priv_bo_unref(struct panvk_priv_bo *bo);

#endif
