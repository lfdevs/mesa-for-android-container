/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>

#include "vk_alloc.h"
#include "vk_log.h"

#include "panvk_device.h"
#include "panvk_priv_bo.h"

#include "kmod/pan_kmod.h"
#include "pan_props.h"

#include "genxml/decode.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "util/os_misc.h"
#include "util/os_time.h"
#include "util/u_debug.h"
#include "util/u_math.h"

VkResult
panvk_priv_bo_create(struct panvk_device *dev, uint64_t size, uint32_t flags,
                     VkSystemAllocationScope scope, struct panvk_priv_bo **out)
{
   return panvk_priv_bo_create_kind(dev, size, flags, scope, out,
                                    PANVK_MEM_MISC);
}

struct panvk_mem_stats panvk_mem_stats;

static void
panvk_mem_account(enum panvk_mem_kind kind, int64_t delta)
{
   uint64_t v = p_atomic_add_return(&panvk_mem_stats.bytes[kind], delta);

   if (delta > 0) {
      p_atomic_inc(&panvk_mem_stats.count[kind]);
      uint64_t peak = p_atomic_read_relaxed(&panvk_mem_stats.peak[kind]);
      while (v > peak &&
             !__atomic_compare_exchange_n(&panvk_mem_stats.peak[kind], &peak,
                                          v, true, __ATOMIC_RELAXED,
                                          __ATOMIC_RELAXED))
         ;
   } else {
      p_atomic_dec(&panvk_mem_stats.count[kind]);
   }
}

VkResult
panvk_priv_bo_create_pool(struct panvk_pool *pool, uint64_t size,
                          uint32_t flags, VkSystemAllocationScope scope,
                          struct panvk_priv_bo **out)
{
   const char *l = pool->props.label ? pool->props.label : "";
   enum panvk_mem_kind kind = PANVK_MEM_OTHER_POOL;

   if (!strcmp(l, "Command buffer CS pool"))
      kind = PANVK_MEM_CMD_CS;
   else if (!strcmp(l, "Command buffer descriptor pool"))
      kind = PANVK_MEM_CMD_DESC;
   else if (!strcmp(l, "TLS pool"))
      kind = PANVK_MEM_CMD_TLS;
   else if (!strncmp(l, "Device RW", 9))
      kind = PANVK_MEM_DEV_POOL;
   else if (!strncmp(l, "Device executable", 17))
      kind = PANVK_MEM_SHADER;

   return panvk_priv_bo_create_kind(pool->dev, size, flags, scope, out, kind);
}

VkResult
panvk_priv_bo_create_kind(struct panvk_device *dev, uint64_t size,
                          uint32_t flags, VkSystemAllocationScope scope,
                          struct panvk_priv_bo **out, enum panvk_mem_kind kind)
{
   VkResult result;
   int ret;
   struct panvk_priv_bo *priv_bo =
      vk_zalloc(&dev->vk.alloc, sizeof(*priv_bo), 8, scope);

   if (!priv_bo)
      return panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct pan_kmod_bo *bo =
      pan_kmod_bo_alloc(dev->kmod.dev, dev->kmod.vm, size, flags);
   if (!bo) {
      result = panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      goto err_free_priv_bo;
   }

   priv_bo->bo = bo;
   priv_bo->dev = dev;

   if (!(flags & PAN_KMOD_BO_FLAG_NO_MMAP)) {
      priv_bo->addr.host =
         pan_kmod_bo_mmap(bo, PROT_READ | PROT_WRITE, MAP_SHARED, NULL);
      if (priv_bo->addr.host == MAP_FAILED) {
         result = panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
         goto err_put_bo;
      }
   }

   struct pan_kmod_vm_op op = {
      .type = PAN_KMOD_VM_OP_TYPE_MAP,
      .va = {
         .start = PAN_KMOD_VM_MAP_AUTO_VA,
         .size = pan_kmod_bo_size(bo),
      },
      .map = {
         .bo = priv_bo->bo,
         .bo_offset = 0,
      },
   };

   if (!(dev->kmod.vm->flags & PAN_KMOD_VM_FLAG_AUTO_VA)) {
      enum panvk_va_heap_id heap = flags & PAN_KMOD_BO_FLAG_EXECUTABLE
                                      ? PANVK_EXEC_VA_HEAP
                                      : PANVK_NO_EXEC_VA_HEAP;
      op.va.start =
         panvk_as_alloc(dev, heap, op.va.size,
                        pan_choose_gpu_va_alignment(dev->kmod.vm, op.va.size));
      if (!op.va.start) {
         result = panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         goto err_munmap_bo;
      }
   }

   ret = pan_kmod_vm_bind(dev->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE, &op, 1);
   if (ret) {
      result = panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      goto err_return_va;
   }

   priv_bo->addr.dev = op.va.start;

   panvk_address_binding_report(dev, NULL, priv_bo->addr.dev,
                                pan_kmod_bo_size(priv_bo->bo),
                                VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT);

   if (dev->debug.decode_ctx) {
      pandecode_inject_mmap(dev->debug.decode_ctx, priv_bo->addr.dev,
                            priv_bo->addr.host, pan_kmod_bo_size(priv_bo->bo),
                            NULL);
   }

   p_atomic_set(&priv_bo->refcnt, 1);
   priv_bo->mem_kind = kind;
   panvk_mem_account(kind, pan_kmod_bo_size(bo));

   *out = priv_bo;
   return VK_SUCCESS;

err_return_va:
   if (!(dev->kmod.vm->flags & PAN_KMOD_VM_FLAG_AUTO_VA)) {
      panvk_as_free(dev, op.va.start, op.va.size);
   }

err_munmap_bo:
   if (priv_bo->addr.host) {
      ret = pan_kmod_bo_munmap(bo, priv_bo->addr.host, pan_kmod_bo_size(bo));
      assert(!ret);
   }

err_put_bo:
   pan_kmod_bo_put(bo);

err_free_priv_bo:
   vk_free(&dev->vk.alloc, priv_bo);
   return result;
}

void
panvk_priv_bo_flush(struct panvk_priv_bo *priv_bo, size_t offset, size_t size)
{
   assert(priv_bo->addr.host != NULL);
   pan_kmod_queue_bo_map_sync(priv_bo->bo, offset, priv_bo->addr.host + offset,
                              size, PAN_KMOD_BO_SYNC_CPU_CACHE_FLUSH);
}

void
panvk_priv_bo_invalidate(struct panvk_priv_bo *priv_bo, size_t offset,
                         size_t size)
{
   assert(priv_bo->addr.host != NULL);
   pan_kmod_queue_bo_map_sync(priv_bo->bo, offset, priv_bo->addr.host + offset,
                              size,
                              PAN_KMOD_BO_SYNC_CPU_CACHE_FLUSH_AND_INVALIDATE);
}

static void
panvk_priv_bo_destroy(struct panvk_priv_bo *priv_bo)
{
   struct panvk_device *dev = priv_bo->dev;

   panvk_mem_account(priv_bo->mem_kind, -(int64_t)pan_kmod_bo_size(priv_bo->bo));
   panvk_address_binding_report(dev, NULL, priv_bo->addr.dev,
                                pan_kmod_bo_size(priv_bo->bo),
                                VK_DEVICE_ADDRESS_BINDING_TYPE_UNBIND_EXT);

   if (dev->debug.decode_ctx) {
      pandecode_inject_free(dev->debug.decode_ctx, priv_bo->addr.dev,
                            pan_kmod_bo_size(priv_bo->bo));
   }

   struct pan_kmod_vm_op op = {
      .type = PAN_KMOD_VM_OP_TYPE_UNMAP,
      .va = {
         .start = priv_bo->addr.dev,
         .size = pan_kmod_bo_size(priv_bo->bo),
      },
   };
   ASSERTED int ret =
      pan_kmod_vm_bind(dev->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE, &op, 1);
   assert(!ret);

   if (!(dev->kmod.vm->flags & PAN_KMOD_VM_FLAG_AUTO_VA)) {
      panvk_as_free(dev, op.va.start, op.va.size);
   }

   if (priv_bo->addr.host) {
      ret = pan_kmod_bo_munmap(priv_bo->bo, priv_bo->addr.host,
                               pan_kmod_bo_size(priv_bo->bo));
      assert(!ret);
   }

   /* csf-v11 230: a kbase alias region is a SAME_VA region; munmap()
    * tears it down (and drops its reference on the BO pages). */
   if (priv_bo->alias_size)
      munmap((void *)(uintptr_t)priv_bo->addr.dev, priv_bo->alias_size);

   pan_kmod_bo_put(priv_bo->bo);
   vk_free(&dev->vk.alloc, priv_bo);
}

void
panvk_priv_bo_unref(struct panvk_priv_bo *priv_bo)
{
   if (!priv_bo || p_atomic_dec_return(&priv_bo->refcnt))
      return;

   panvk_priv_bo_destroy(priv_bo);
}

/* ---- csf-v11 223: PANVK_DEBUG=mem memory report ----
 *
 * logd is stopped on some phones and games run in a Wine prefix, so the
 * report goes to a file: PANVK_MEM_LOG=<path> (appended; a "%p" in the path
 * becomes the pid), else MESA_LOG (mesa_logi, so MESA_LOG_FILE also works).
 * PANVK_DEBUG=mem without PANVK_MEM_LOG logs through mesa_logi. One line at
 * most every PANVK_MEM_LOG_MS (default 10000) ms, from the queue submit
 * path, and one at queue destroy. Fields are MiB unless noted.
 */
static const struct debug_control panvk_mem_debug_options[] = {
   {"mem", 1},
   {NULL, 0},
};

static int panvk_mem_report_on = -1;
static uint64_t panvk_mem_report_last_ns, panvk_mem_report_start_ns;
static simple_mtx_t panvk_mem_report_lock = SIMPLE_MTX_INITIALIZER;
static FILE *panvk_mem_report_fp;

bool
panvk_mem_report_enabled(void)
{
   int on = p_atomic_read_relaxed(&panvk_mem_report_on);

   if (on < 0) {
      on = (parse_debug_string(os_get_option("PANVK_DEBUG"),
                               panvk_mem_debug_options) & 1) ||
           os_get_option("PANVK_MEM_LOG");
      p_atomic_set(&panvk_mem_report_on, on);
   }
   return on;
}

static FILE *
panvk_mem_report_file(void)
{
   static bool tried;
   const char *path = os_get_option("PANVK_MEM_LOG");

   if (tried || !path || !path[0])
      return panvk_mem_report_fp;
   tried = true;

   char buf[1024];
   const char *p = strstr(path, "%p");
   if (p) {
      snprintf(buf, sizeof(buf), "%.*s%d%s", (int)(p - path), path,
               (int)getpid(), p + 2);
      path = buf;
   }
   panvk_mem_report_fp = fopen(path, "a");
   if (!panvk_mem_report_fp)
      mesa_logw("panvk: PANVK_MEM_LOG=%s: cannot open, using mesa_log", path);
   else
      setvbuf(panvk_mem_report_fp, NULL, _IOLBF, 0);
   return panvk_mem_report_fp;
}

/* VmRSS, RssAnon, RssFile, VmSwap in kB from /proc/self/status. */
static void
panvk_mem_proc_status(uint64_t kb[4])
{
   static const char *keys[4] = {"VmRSS:", "RssAnon:", "RssFile:", "VmSwap:"};
   char line[256];
   FILE *f = fopen("/proc/self/status", "r");

   kb[0] = kb[1] = kb[2] = kb[3] = 0;
   if (!f)
      return;
   while (fgets(line, sizeof(line), f)) {
      for (unsigned i = 0; i < 4; i++) {
         size_t n = strlen(keys[i]);
         if (!strncmp(line, keys[i], n))
            kb[i] = strtoull(line + n, NULL, 10);
      }
   }
   fclose(f);
}

static uint64_t
panvk_priv_mem_size(struct panvk_priv_mem mem)
{
   struct panvk_priv_bo *bo = panvk_priv_mem_bo(mem);
   return bo ? pan_kmod_bo_size(bo->bo) : 0;
}

#define MIB(x) ((double)(x) / (1024.0 * 1024.0))

void
panvk_mem_report(struct panvk_device *dev, bool final)
{
   if (!panvk_mem_report_enabled())
      return;

   const uint64_t now = os_time_get_nano();
   static int64_t interval_ms = -1;
   if (interval_ms < 0)
      interval_ms = MAX2(debug_get_num_option("PANVK_MEM_LOG_MS", 10000), 100);

   if (!final) {
      uint64_t last = p_atomic_read_relaxed(&panvk_mem_report_last_ns);
      if (last && now - last < (uint64_t)interval_ms * 1000000ull)
         return;
   }
   simple_mtx_lock(&panvk_mem_report_lock);
   if (!panvk_mem_report_start_ns)
      panvk_mem_report_start_ns = now;
   p_atomic_set(&panvk_mem_report_last_ns, now);

   const struct panvk_mem_stats *s = &panvk_mem_stats;
   const struct panvk_physical_device *pdev =
      to_panvk_physical_device(dev->vk.physical);
   uint64_t b[PANVK_MEM_KIND_COUNT], priv = 0;
   for (unsigned i = 0; i < PANVK_MEM_KIND_COUNT; i++) {
      b[i] = p_atomic_read_relaxed(&s->bytes[i]);
      priv += b[i];
   }
   /* gpu_prerast arenas (csf-v11 205) are counted as misc BOs. */
   const uint64_t prerast = panvk_priv_mem_size(dev->gpu_prerast_arena) +
                            panvk_priv_mem_size(dev->gpu_prerast_tess_arena) +
                            panvk_priv_mem_size(dev->gpu_prerast_tess_arena2);
   const uint64_t devmem = p_atomic_read_relaxed(&pdev->memory.heap_used);
   const uint64_t heap_live = p_atomic_read_relaxed(&s->heap_ctx_live);
   const uint64_t heap_init = p_atomic_read_relaxed(&s->heap_init_B);
   uint64_t avail = 0, kb[4];
   os_get_available_system_memory(&avail);
   panvk_mem_proc_status(kb);

   char line[1536];
   snprintf(
      line, sizeof(line),
      "panvk-mem%s t=%.1fs devmem=%.1f priv=%.1f "
      "dev_pool=%.1f shader=%.1f cmd_cs=%.1f/%" PRIu64 " cmd_desc=%.1f/%" PRIu64
      " cmd_tls=%.1f dev_tls=%.1f desc_pool=%.1f/%" PRIu64
      " bc_raw=%.1f/%" PRIu64 " utrace=%.1f other_pool=%.1f misc=%.1f"
      " prerast=%.1f | peaks cmd=%.1f desc_pool=%.1f bc_raw=%.1f dev_tls=%.1f"
      " | heap live=%" PRIu64 " peak=%" PRIu64 " init=%.1f renew=%" PRIu64
      " term_sync=%" PRIu64 " termq_max=%" PRIu64
      " | bc resident=%.1f spilled=%.1f needed=%d spills=%" PRIu64
      " remats=%" PRIu64 " pins=%" PRIu64 " lazy_skips=%" PRIu64
      " stale=%" PRIu64 "/%" PRIu64
      " | trim bos=%" PRIu64 " %.1f tls_freed=%.1f"
      " | slab_cache=%.1f/%" PRIu64 " hits=%" PRIu64 " freed=%" PRIu64
      " %.1f evictable=%.1f/%" PRIu64 " reused=%" PRIu64 " lost=%" PRIu64
      " | rss=%.1f anon=%.1f file=%.1f swap=%.1f avail=%.1f",
      final ? "-final" : "", (now - panvk_mem_report_start_ns) / 1e9,
      MIB(devmem), MIB(priv), MIB(b[PANVK_MEM_DEV_POOL]),
      MIB(b[PANVK_MEM_SHADER]), MIB(b[PANVK_MEM_CMD_CS]),
      p_atomic_read_relaxed(&s->count[PANVK_MEM_CMD_CS]),
      MIB(b[PANVK_MEM_CMD_DESC]),
      p_atomic_read_relaxed(&s->count[PANVK_MEM_CMD_DESC]),
      MIB(b[PANVK_MEM_CMD_TLS]), MIB(b[PANVK_MEM_DEV_TLS]),
      MIB(b[PANVK_MEM_DESC_POOL]),
      p_atomic_read_relaxed(&s->count[PANVK_MEM_DESC_POOL]),
      MIB(b[PANVK_MEM_BC_RAW]),
      p_atomic_read_relaxed(&s->count[PANVK_MEM_BC_RAW]),
      MIB(b[PANVK_MEM_UTRACE]), MIB(b[PANVK_MEM_OTHER_POOL]),
      MIB(b[PANVK_MEM_MISC] > prerast ? b[PANVK_MEM_MISC] - prerast : 0),
      MIB(prerast),
      MIB(p_atomic_read_relaxed(&s->peak[PANVK_MEM_CMD_CS]) +
          p_atomic_read_relaxed(&s->peak[PANVK_MEM_CMD_DESC]) +
          p_atomic_read_relaxed(&s->peak[PANVK_MEM_CMD_TLS])),
      MIB(p_atomic_read_relaxed(&s->peak[PANVK_MEM_DESC_POOL])),
      MIB(p_atomic_read_relaxed(&s->peak[PANVK_MEM_BC_RAW])),
      MIB(p_atomic_read_relaxed(&s->peak[PANVK_MEM_DEV_TLS])), heap_live,
      p_atomic_read_relaxed(&s->heap_ctx_peak), MIB(heap_init),
      p_atomic_read_relaxed(&s->heap_renewals),
      p_atomic_read_relaxed(&s->heap_term_sync),
      p_atomic_read_relaxed(&s->heap_term_queued_max),
      MIB(dev->bc_raw.resident_B), MIB(dev->bc_raw.spilled_B),
      p_atomic_read_relaxed(&dev->bc_raw.needed),
      p_atomic_read_relaxed(&s->bc_spills), p_atomic_read_relaxed(&s->bc_remats),
      p_atomic_read_relaxed(&s->bc_pins),
      p_atomic_read_relaxed(&s->bc_lazy_skips),
      p_atomic_read_relaxed(&s->bc_stale_marks),
      p_atomic_read_relaxed(&s->bc_stale_reads),
      p_atomic_read_relaxed(&s->cmdpool_trim_bos),
      MIB(p_atomic_read_relaxed(&s->cmdpool_trim_B)),
      MIB(p_atomic_read_relaxed(&s->dev_tls_freed_B)),
      MIB(p_atomic_read_relaxed(&s->slab_cache_B)),
      p_atomic_read_relaxed(&s->slab_cache_count),
      p_atomic_read_relaxed(&s->slab_cache_hits),
      p_atomic_read_relaxed(&s->slab_cache_freed_bos),
      MIB(p_atomic_read_relaxed(&s->slab_cache_freed_B)),
      MIB(p_atomic_read_relaxed(&s->slab_evict_B)),
      p_atomic_read_relaxed(&s->slab_evict_count),
      p_atomic_read_relaxed(&s->slab_evict_reused),
      p_atomic_read_relaxed(&s->slab_evict_lost), kb[0] / 1024.0,
      kb[1] / 1024.0, kb[2] / 1024.0, kb[3] / 1024.0, MIB(avail));

   FILE *fp = panvk_mem_report_file();
   if (fp)
      fprintf(fp, "%s\n", line);
   else
      mesa_logi("%s", line);
   simple_mtx_unlock(&panvk_mem_report_lock);
}
