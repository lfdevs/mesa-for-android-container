/*
 * Copyright © 2023 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <string.h>
#include <xf86drm.h>

#include "util/cache_ops.h"
#include "util/u_memory.h"
#include "util/macros.h"
#include "pan_kmod.h"
#include "pan_kmod_backend.h"

#if defined(HAVE_PAN_KMOD_PANFROST)
extern const struct pan_kmod_ops panfrost_kmod_ops;
#endif
#if defined(HAVE_PAN_KMOD_PANTHOR)
extern const struct pan_kmod_ops panthor_kmod_ops;
#endif
#ifdef HAVE_PANFROST_VDRM
extern const struct pan_kmod_ops panfrost_virtio_gpu_ops;
extern const struct pan_kmod_ops panfrost_vpipe_ops;
#endif /* HAVE_PANFROST_VDRM */
#if defined(HAVE_PAN_KMOD_KBASE)
extern const struct pan_kmod_ops kbase_kmod_ops;
#endif

static const struct backend {
   const char *name;
   const struct pan_kmod_ops *ops;
} backends[] = {
#if defined(HAVE_PAN_KMOD_PANFROST)
   {
      "panfrost",
      &panfrost_kmod_ops,
   },
#endif
#if defined(HAVE_PAN_KMOD_PANTHOR)
   {
      "panthor",
      &panthor_kmod_ops,
   },
#endif
#ifdef HAVE_PANFROST_VDRM
   {
      "panfrost_virtio_gpu",
      &panfrost_virtio_gpu_ops,
   },
   {
      "panfrost_vpipe",
      &panfrost_vpipe_ops,
   },
#endif /* HAVE_PANFROST_VDRM */
#if defined(HAVE_PAN_KMOD_KBASE)
   {
      "kbase",
      &kbase_kmod_ops,
   },
#endif
};

DEBUG_GET_ONCE_OPTION(enabled_backends, "PAN_KMOD_RESTRICT_TO_BACKENDS", "all");

static inline bool
pan_kmod_backend_is_enabled(const char *enabled_backends, const char *backend)
{
   return !strcmp(enabled_backends, "all") ||
          comma_separated_list_contains(enabled_backends, backend);
}

static void *
default_zalloc(const struct pan_kmod_allocator *allocator, size_t size,
               UNUSED bool transient)
{
   return os_calloc(1, size);
}

static void
default_free(const struct pan_kmod_allocator *allocator, void *data)
{
   os_free(data);
}

static const struct pan_kmod_allocator default_allocator = {
   .zalloc = default_zalloc,
   .free = default_free,
};

struct pan_kmod_dev *
pan_kmod_dev_create_with_driver(int fd, uint32_t flags,
                                const char *driver_name,
                                const struct pan_kmod_driver *driver,
                                const struct pan_kmod_allocator *allocator)
{
   if (!allocator)
      allocator = &default_allocator;

   if (!driver_name || !driver_name[0]) {
      mesa_loge("invalid kernel driver name");
      errno = EINVAL;
      return NULL;
   }

   const struct pan_kmod_driver fallback_driver = {
      .version = {
         .major = 0,
         .minor = 0,
      },
   };

   if (!driver)
      driver = &fallback_driver;

   for (unsigned i = 0; i < ARRAY_SIZE(backends); i++) {
      if (!strcmp(backends[i].name, driver_name)) {
         struct pan_kmod_dev *dev =
            backends[i].ops->dev_create(fd, flags, allocator);
         if (dev && driver)
            dev->driver = *driver;
         return dev;
      }
   }

   mesa_loge("kernel driver '%s' is not supported by this build", driver_name);
   errno = ENODEV;
   return NULL;
}

struct pan_kmod_dev *
pan_kmod_dev_create(int fd, uint32_t flags,
                    const struct pan_kmod_allocator *allocator)
{
   const char *selected_backends = debug_get_option_enabled_backends();
   struct pan_kmod_dev *dev = NULL;

   if (!allocator)
      allocator = &default_allocator;

   for (int i = 0; i < ARRAY_SIZE(backends); i++) {
      if (pan_kmod_backend_is_enabled(selected_backends, backends[i].name))
         dev = backends[i].ops->dev_create(fd, flags, allocator);
      else
         mesa_logi("skipping '%s'", backends[i].name);

      if (dev)
         break;
   }
   return dev;
}

void
pan_kmod_dev_destroy(struct pan_kmod_dev *dev)
{
   dev->ops->dev_destroy(dev);
}

struct pan_kmod_bo *
pan_kmod_bo_alloc(struct pan_kmod_dev *dev, struct pan_kmod_vm *exclusive_vm,
                  uint64_t size, uint32_t flags)
{
   struct pan_kmod_bo *bo;

   bo = dev->ops->bo_alloc(dev, exclusive_vm, size, flags);
   if (!bo)
      return NULL;

   /* We intentionally don't take the lock when filling the sparse array,
    * because we just created the BO, and haven't exported it yet, so
    * there's no risk of imports racing with our BO insertion.
    */
   struct pan_kmod_bo **slot =
      util_sparse_array_get(&dev->handle_to_bo.array, bo->handle);

   if (!slot) {
      mesa_loge("failed to allocate slot in the handle_to_bo array");
      bo->dev->ops->bo_free(bo);
      return NULL;
   }

   assert(*slot == NULL);
   *slot = bo;
   return bo;
}

void
pan_kmod_bo_put(struct pan_kmod_bo *bo)
{
   if (!bo)
      return;

   int32_t refcnt = p_atomic_dec_return(&bo->refcnt);

   assert(refcnt >= 0);

   if (refcnt)
      return;

   struct pan_kmod_dev *dev = bo->dev;

   simple_mtx_lock(&dev->handle_to_bo.lock);

   /* If some import took a ref on this BO while we were trying to acquire the
    * lock, skip the destruction.
    */
   if (!p_atomic_read(&bo->refcnt)) {
      struct pan_kmod_bo **slot = (struct pan_kmod_bo **)util_sparse_array_get(
         &dev->handle_to_bo.array, bo->handle);

      assert(slot);
      *slot = NULL;
      bo->dev->ops->bo_free(bo);
   }

   simple_mtx_unlock(&dev->handle_to_bo.lock);
}

struct pan_kmod_bo *
pan_kmod_bo_import(struct pan_kmod_dev *dev, int fd)
{
   struct pan_kmod_bo *bo;

   simple_mtx_lock(&dev->handle_to_bo.lock);
   bo = dev->ops->bo_import(dev, fd);
   if (bo)
      assert(p_atomic_read(&bo->refcnt) > 0);
   simple_mtx_unlock(&dev->handle_to_bo.lock);


   return bo;
}

void
pan_kmod_flush_bo_map_syncs_locked(struct pan_kmod_dev *dev)
{
   ASSERTED int ret = dev->ops->flush_bo_map_syncs(dev);
   assert(!ret);

   util_dynarray_foreach(&dev->pending_bo_syncs.array,
                         struct pan_kmod_deferred_bo_sync, sync)
      sync->bo->has_pending_deferred_syncs = false;

   util_dynarray_clear(&dev->pending_bo_syncs.array);
}

int
pan_kmod_cs_event_signal(struct pan_kmod_dev *dev)
{
   if (!dev->ops->cs_event_signal)
      return 0;

   return dev->ops->cs_event_signal(dev);
}

void
pan_kmod_flush_bo_map_syncs(struct pan_kmod_dev *dev)
{
   if (dev->props.is_io_coherent)
      return;

   /* Barrier to make sure all flush/invalidate requests are effective. */
   if (p_atomic_xchg(&dev->pending_bo_syncs.user_cache_ops_pending, false))
      util_post_flush_inval_fence();

   /* This can be racy, but that's fine, because we expect a future call to
    * pan_kmod_flush_bo_map_syncs() if new ops are being added while we check
    * this value.
    */
   if (!util_dynarray_num_elements(&dev->pending_bo_syncs.array,
                                   struct pan_kmod_deferred_bo_sync))
      return;

   simple_mtx_lock(&dev->pending_bo_syncs.lock);
   pan_kmod_flush_bo_map_syncs_locked(dev);
   simple_mtx_unlock(&dev->pending_bo_syncs.lock);
}

/* Arbitrary limit for now. Pick something bigger or make it configurable if it
 * becomes problematic.
 */
#define MAX_PENDING_SYNC_OPS 4096

void
pan_kmod_queue_bo_map_sync(struct pan_kmod_bo *bo, uint64_t bo_offset,
                           void *cpu_ptr, uint64_t range,
                           enum pan_kmod_bo_sync_type type)
{
   struct pan_kmod_dev *dev = bo->dev;

   /* Nothing to do if the buffer is IO coherent or if the BO is not mapped
    * cacheable.
    */
   if (!(bo->flags & PAN_KMOD_BO_FLAG_WB_MMAP) ||
       (bo->flags & PAN_KMOD_BO_FLAG_IO_COHERENT))
      return;

   /* If we have userspace cache flushing ops, use them instead of trapping
    * through to the kernel.
    */
   if (pan_kmod_can_sync_bo_map_from_userland(dev)) {
      /* Pre-flush needs to be executed before each flush/inval operation, but
       * we can batch the post flush/inval fence. util_pre_flush_fence() being
       * a NOP on aarch64, it's effectively free there, but we keep it here for
       * clarity (not sure we care about Mali on x86 to be honest :D).
       */
      util_pre_flush_fence();

      if (type == PAN_KMOD_BO_SYNC_CPU_CACHE_FLUSH)
         util_flush_range_no_fence(cpu_ptr, range);
      else
         util_flush_inval_range_no_fence(cpu_ptr, range);

      /* The util_pre_flush_inval_fence() is inserted by
       * pan_kmod_flush_bo_map_syncs() to avoid unnecessary serialization when
       * flush/invalidate operations are batched.
       */
      p_atomic_set(&dev->pending_bo_syncs.user_cache_ops_pending, true);
      return;
   }

   simple_mtx_lock(&dev->pending_bo_syncs.lock);

   /* If we reach the limit, flush the pending ops before queuing new ones. */
   if (util_dynarray_num_elements(&dev->pending_bo_syncs.array,
                                  struct pan_kmod_deferred_bo_sync) >=
       MAX_PENDING_SYNC_OPS)
      pan_kmod_flush_bo_map_syncs_locked(dev);

   /* Architectures that use cache_ops_null.c report a zero granularity. */
   uint64_t granularity = util_has_cache_ops() ? util_cache_granularity() : 64;
   uint64_t start = bo_offset & ~(granularity - 1);
   uint64_t end = ALIGN_POT(bo_offset + range, granularity);

   /* A command buffer often dirties several neighboring slices of the same
    * mapped BO before the next submit.  Coalesce those slices here so a
    * kernel-backed cache-sync implementation (notably kbase MEM_SYNC) does
    * not issue one ioctl per descriptor or upload. */
   util_dynarray_foreach(&dev->pending_bo_syncs.array,
                         struct pan_kmod_deferred_bo_sync, sync) {
      uint64_t sync_end = sync->start + sync->size;

      if (sync->bo == bo && sync->type == type &&
          start <= sync_end && sync->start <= end) {
         uint64_t merged_start = MIN2(start, sync->start);
         uint64_t merged_end = MAX2(end, sync_end);

         sync->start = merged_start;
         sync->size = merged_end - merged_start;
         simple_mtx_unlock(&dev->pending_bo_syncs.lock);
         return;
      }
   }

   struct pan_kmod_deferred_bo_sync new_sync = {
      .bo = bo,
      .start = start,
      .size = end - start,
      .type = type,
   };

   bo->has_pending_deferred_syncs = true;
   util_dynarray_append(&dev->pending_bo_syncs.array, new_sync);

   simple_mtx_unlock(&dev->pending_bo_syncs.lock);
}
