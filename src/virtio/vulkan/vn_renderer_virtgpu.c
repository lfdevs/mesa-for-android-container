/*
 * Copyright 2020 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xf86drm.h>

#ifdef MAJOR_IN_MKDEV
#include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMACROS
#include <sys/sysmacros.h>
#endif

#include "virtio/virtio-gpu/venus_hw.h"

#include "drm-uapi/virtgpu_drm.h"
#include "util/os_file.h"
#include "util/sparse_array.h"
#include "util/u_sync_provider.h"

#include "vn_renderer_internal.h"
#include "vn_renderer_sim_syncobj.h"

/* All guest allocations happen via virtgpu dedicated heap. */
#ifndef VIRTGPU_PARAM_GUEST_VRAM
#define VIRTGPU_PARAM_GUEST_VRAM 9
#endif
#ifndef VIRTGPU_BLOB_MEM_GUEST_VRAM
#define VIRTGPU_BLOB_MEM_GUEST_VRAM 0x0004
#endif

#define VIRTGPU_PCI_VENDOR_ID 0x1af4
#define VIRTGPU_PCI_DEVICE_ID 0x1050

struct virtgpu_shmem {
   struct vn_renderer_shmem base;
   uint32_t gem_handle;
};

struct virtgpu_bo {
   struct vn_renderer_bo base;
   uint32_t gem_handle;
   uint32_t blob_flags;
};

struct virtgpu {
   struct vn_renderer base;

   struct vn_instance *instance;

   int fd;

   bool has_primary;
   int primary_major;
   int primary_minor;
   int render_major;
   int render_minor;

   int bustype;
   drmPciBusInfo pci_bus_info;

   uint32_t max_timeline_count;

   struct {
      uint32_t id;
      uint32_t version;
      struct virgl_renderer_capset_venus data;
   } capset;

   uint32_t shmem_blob_mem;
   uint32_t bo_blob_mem;

   /* note that we use gem_handle instead of res_id to index because
    * res_id is monotonically increasing by default (see
    * virtio_gpu_resource_id_get)
    */
   struct util_sparse_array shmem_array;
   struct util_sparse_array bo_array;

   mtx_t dma_buf_import_mutex;

   struct vn_renderer_shmem_cache shmem_cache;

   bool supports_cross_device;

   struct util_sync_provider *sync;
};

static inline int
virtgpu_ioctl(struct virtgpu *gpu, unsigned long request, void *args)
{
   return drmIoctl(gpu->fd, request, args);
}

static uint64_t
virtgpu_ioctl_getparam(struct virtgpu *gpu, uint64_t param)
{
   /* val must be zeroed because kernel only writes the lower 32 bits */
   uint64_t val = 0;
   struct drm_virtgpu_getparam args = {
      .param = param,
      .value = (uintptr_t)&val,
   };

   const int ret = virtgpu_ioctl(gpu, DRM_IOCTL_VIRTGPU_GETPARAM, &args);
   return ret ? 0 : val;
}

static int
virtgpu_ioctl_get_caps(struct virtgpu *gpu,
                       uint32_t id,
                       uint32_t version,
                       void *capset,
                       size_t capset_size)
{
   struct drm_virtgpu_get_caps args = {
      .cap_set_id = id,
      .cap_set_ver = version,
      .addr = (uintptr_t)capset,
      .size = capset_size,
   };

   return virtgpu_ioctl(gpu, DRM_IOCTL_VIRTGPU_GET_CAPS, &args);
}

static int
virtgpu_ioctl_context_init(struct virtgpu *gpu, uint32_t capset_id)
{
   struct drm_virtgpu_context_set_param ctx_set_params[3] = {
      {
         .param = VIRTGPU_CONTEXT_PARAM_CAPSET_ID,
         .value = capset_id,
      },
      {
         .param = VIRTGPU_CONTEXT_PARAM_NUM_RINGS,
         .value = 64,
      },
      {
         .param = VIRTGPU_CONTEXT_PARAM_POLL_RINGS_MASK,
         .value = 0, /* don't generate drm_events on fence signaling */
      },
   };

   struct drm_virtgpu_context_init args = {
      .num_params = ARRAY_SIZE(ctx_set_params),
      .ctx_set_params = (uintptr_t)&ctx_set_params,
   };

   return virtgpu_ioctl(gpu, DRM_IOCTL_VIRTGPU_CONTEXT_INIT, &args);
}

static uint32_t
virtgpu_ioctl_resource_create_blob(struct virtgpu *gpu,
                                   struct vn_renderer_submit_batch *batch,
                                   uint32_t blob_mem,
                                   uint32_t blob_flags,
                                   size_t blob_size,
                                   uint64_t blob_id,
                                   uint32_t *res_id)
{
   struct drm_virtgpu_resource_create_blob args = {
      .blob_mem = blob_mem,
      .blob_flags = blob_flags,
      .size = blob_size,
      .cmd_size = batch ? batch->cs_size : 0,
      .cmd = batch ? (uintptr_t)batch->cs_data : 0,
      .blob_id = blob_id,
   };

   if (virtgpu_ioctl(gpu, DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB, &args)) {
      vn_log(gpu->instance,
             "RESOURCE_CREATE_BLOB failed: type=%u, flags=%u, size=%zu, "
             "id=%" PRIu64 ", err=%s",
             blob_mem, blob_flags, blob_size, blob_id, strerror(errno));
      return 0;
   }

   *res_id = args.res_handle;
   return args.bo_handle;
}

static int
virtgpu_ioctl_resource_info(struct virtgpu *gpu,
                            uint32_t gem_handle,
                            struct drm_virtgpu_resource_info *info)
{
   *info = (struct drm_virtgpu_resource_info){
      .bo_handle = gem_handle,
   };

   const int ret = virtgpu_ioctl(gpu, DRM_IOCTL_VIRTGPU_RESOURCE_INFO, info);
   if (ret) {
      vn_log(gpu->instance, "RESOURCE_INFO failed: handle=%u, err=%s",
             gem_handle, strerror(errno));
      return ret;
   }

   return 0;
}

static void
virtgpu_ioctl_gem_close(struct virtgpu *gpu, uint32_t gem_handle)
{
   struct drm_gem_close args = {
      .handle = gem_handle,
   };

   ASSERTED const int ret = virtgpu_ioctl(gpu, DRM_IOCTL_GEM_CLOSE, &args);
   assert(!ret);
}

static int
virtgpu_ioctl_prime_handle_to_fd(struct virtgpu *gpu,
                                 uint32_t gem_handle,
                                 bool mappable)
{
   struct drm_prime_handle args = {
      .handle = gem_handle,
      .flags = DRM_CLOEXEC | (mappable ? DRM_RDWR : 0),
   };

   const int ret = virtgpu_ioctl(gpu, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args);
   if (ret) {
      vn_log(gpu->instance,
             "PRIME_HANDLE_TO_FD failed: handle=%u, mappable=%d, err=%s",
             gem_handle, mappable, strerror(errno));
      return -1;
   }

   return args.fd;
}

static uint32_t
virtgpu_ioctl_prime_fd_to_handle(struct virtgpu *gpu, int fd)
{
   struct drm_prime_handle args = {
      .fd = fd,
   };

   const int ret = virtgpu_ioctl(gpu, DRM_IOCTL_PRIME_FD_TO_HANDLE, &args);
   if (ret) {
      vn_log(gpu->instance, "PRIME_FD_TO_HANDLE failed: fd=%d, err=%s", fd,
             strerror(errno));
      return 0;
   }

   return args.handle;
}

static void *
virtgpu_ioctl_map(struct virtgpu *gpu,
                  uint32_t gem_handle,
                  size_t size,
                  void *placed_addr)
{
   struct drm_virtgpu_map args = {
      .handle = gem_handle,
   };

   if (virtgpu_ioctl(gpu, DRM_IOCTL_VIRTGPU_MAP, &args)) {
      vn_log(gpu->instance, "MAP failed: handle=%u, err=%s", gem_handle,
             strerror(errno));
      return NULL;
   }

   void *ptr =
      mmap(placed_addr, size, PROT_READ | PROT_WRITE,
           MAP_SHARED | (placed_addr ? MAP_FIXED : 0), gpu->fd, args.offset);
   if (ptr == MAP_FAILED) {
      vn_log(
         gpu->instance,
         "mmap failed: gpu_fd=%d, handle=%u, size=%zu, offset=%llu, err=%s",
         gpu->fd, gem_handle, size, (long long)args.offset, strerror(errno));
      return NULL;
   }

   return ptr;
}

static VkResult
virtgpu_sync_write(struct vn_renderer *renderer,
                   struct vn_renderer_sync *sync,
                   uint64_t val)
{
   struct virtgpu *gpu = (struct virtgpu *)renderer;

   assert(renderer->info.has_timeline_sync);
   int ret =
      gpu->sync->timeline_signal(gpu->sync, &sync->syncobj_handle, &val, 1);

   return ret ? VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_SUCCESS;
}

static VkResult
virtgpu_sync_read(struct vn_renderer *renderer,
                  struct vn_renderer_sync *sync,
                  uint64_t *val)
{
   struct virtgpu *gpu = (struct virtgpu *)renderer;

   int ret = gpu->sync->query(gpu->sync, &sync->syncobj_handle, val, 1, 0);

   return ret ? VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_SUCCESS;
}

static VkResult
virtgpu_sync_reset(struct vn_renderer *renderer,
                   struct vn_renderer_sync *sync)
{
   struct virtgpu *gpu = (struct virtgpu *)renderer;

   int ret = gpu->sync->reset(gpu->sync, &sync->syncobj_handle, 1);
   return ret ? VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_SUCCESS;
}

static int
virtgpu_sync_export_syncobj(struct vn_renderer *renderer,
                            struct vn_renderer_sync *sync,
                            bool sync_file)
{
   struct virtgpu *gpu = (struct virtgpu *)renderer;

   int ret, fd;
   if (sync_file)
      ret = gpu->sync->export_sync_file(gpu->sync, sync->syncobj_handle, &fd);
   else if (gpu->sync->handle_to_fd)
      ret = gpu->sync->handle_to_fd(gpu->sync, sync->syncobj_handle, &fd);
   else
      ret = -1;

   return ret ? -1 : fd;
}

static void
virtgpu_sync_destroy(struct vn_renderer *renderer,
                     struct vn_renderer_sync *sync)
{
   struct virtgpu *gpu = (struct virtgpu *)renderer;

   gpu->sync->destroy(gpu->sync, sync->syncobj_handle);

   free(sync);
}

static VkResult
virtgpu_sync_create_from_syncobj(struct vn_renderer *renderer,
                                 int fd,
                                 bool sync_file,
                                 struct vn_renderer_sync **out_sync)
{
   struct virtgpu *gpu = (struct virtgpu *)renderer;

   uint32_t syncobj_handle;
   if (sync_file) {
      if (gpu->sync->create(gpu->sync, 0, &syncobj_handle))
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      if (gpu->sync->import_sync_file(gpu->sync, syncobj_handle, fd)) {
         gpu->sync->destroy(gpu->sync, syncobj_handle);
         return VK_ERROR_INVALID_EXTERNAL_HANDLE;
      }
   } else {
      if (gpu->sync->fd_to_handle(gpu->sync, fd, &syncobj_handle))
         return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }

   struct vn_renderer_sync *sync = calloc(1, sizeof(*sync));
   if (!sync) {
      gpu->sync->destroy(gpu->sync, syncobj_handle);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   sync->syncobj_handle = syncobj_handle;
   *out_sync = sync;

   return VK_SUCCESS;
}

static VkResult
virtgpu_sync_create(struct vn_renderer *renderer,
                    uint64_t initial_val,
                    struct vn_renderer_sync **out_sync)
{
   struct virtgpu *gpu = (struct virtgpu *)renderer;

   uint32_t syncobj_handle;
   if (renderer->info.has_timeline_sync) {
      if (gpu->sync->create(gpu->sync, 0, &syncobj_handle))
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;

      /* add a signaled fence chain with seqno initial_val */
      if (initial_val && gpu->sync->timeline_signal(
                            gpu->sync, &syncobj_handle, &initial_val, 1)) {
         gpu->sync->destroy(gpu->sync, syncobj_handle);
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;
      }
   } else {
      assert(initial_val <= 1);
      const uint32_t flags = initial_val ? DRM_SYNCOBJ_CREATE_SIGNALED : 0;
      if (gpu->sync->create(gpu->sync, flags, &syncobj_handle))
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   }

   struct vn_renderer_sync *sync = calloc(1, sizeof(*sync));
   if (!sync) {
      gpu->sync->destroy(gpu->sync, syncobj_handle);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   sync->syncobj_handle = syncobj_handle;
   *out_sync = sync;

   return VK_SUCCESS;
}

static void
virtgpu_bo_invalidate(struct vn_renderer *renderer,
                      struct vn_renderer_bo *bo,
                      VkDeviceSize offset,
                      VkDeviceSize size)
{
   /* nop because kernel makes every mapping coherent */
}

static void
virtgpu_bo_flush(struct vn_renderer *renderer,
                 struct vn_renderer_bo *bo,
                 VkDeviceSize offset,
                 VkDeviceSize size)
{
   /* nop because kernel makes every mapping coherent */
}

static void *
virtgpu_bo_map(struct vn_renderer *renderer,
               struct vn_renderer_bo *_bo,
               void *placed_addr)
{
   struct virtgpu *gpu = (struct virtgpu *)renderer;
   struct virtgpu_bo *bo = (struct virtgpu_bo *)_bo;
   const bool mappable = bo->blob_flags & VIRTGPU_BLOB_FLAG_USE_MAPPABLE;

   /* not thread-safe but is fine */
   if (!bo->base.mmap_ptr && mappable) {
      bo->base.mmap_ptr = virtgpu_ioctl_map(gpu, bo->gem_handle,
                                            bo->base.mmap_size, placed_addr);
   }

   return bo->base.mmap_ptr;
}

static int
virtgpu_bo_export_dma_buf(struct vn_renderer *renderer,
                          struct vn_renderer_bo *_bo)
{
   struct virtgpu *gpu = (struct virtgpu *)renderer;
   struct virtgpu_bo *bo = (struct virtgpu_bo *)_bo;
   const bool mappable = bo->blob_flags & VIRTGPU_BLOB_FLAG_USE_MAPPABLE;
   const bool shareable = bo->blob_flags & VIRTGPU_BLOB_FLAG_USE_SHAREABLE;

   return shareable
             ? virtgpu_ioctl_prime_handle_to_fd(gpu, bo->gem_handle, mappable)
             : -1;
}

static bool
virtgpu_bo_destroy(struct vn_renderer *renderer, struct vn_renderer_bo *_bo)
{
   struct virtgpu *gpu = (struct virtgpu *)renderer;
   struct virtgpu_bo *bo = (struct virtgpu_bo *)_bo;

   mtx_lock(&gpu->dma_buf_import_mutex);

   /* Check the refcount again after the import lock is grabbed.  Yes, we use
    * the double-checked locking anti-pattern.
    */
   if (vn_refcount_is_valid(&bo->base.refcount)) {
      mtx_unlock(&gpu->dma_buf_import_mutex);
      return false;
   }

   if (bo->base.mmap_ptr)
      munmap(bo->base.mmap_ptr, bo->base.mmap_size);

   /* Set gem_handle to 0 to indicate that the bo is invalid. Must be set
    * before closing gem handle. Otherwise the same gem handle can be reused
    * by another newly created bo and unexpectedly gotten zero'ed out the
    * tracked gem handle.
    */
   const uint32_t gem_handle = bo->gem_handle;
   bo->gem_handle = 0;
   virtgpu_ioctl_gem_close(gpu, gem_handle);

   mtx_unlock(&gpu->dma_buf_import_mutex);

   return true;
}

static uint32_t
virtgpu_bo_blob_flags(struct virtgpu *gpu,
                      VkMemoryPropertyFlags flags,
                      VkExternalMemoryHandleTypeFlags external_handles)
{
   uint32_t blob_flags = 0;
   if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
      blob_flags |= VIRTGPU_BLOB_FLAG_USE_MAPPABLE;
   if (external_handles)
      blob_flags |= VIRTGPU_BLOB_FLAG_USE_SHAREABLE;
   if (external_handles & VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT) {
      if (gpu->supports_cross_device)
         blob_flags |= VIRTGPU_BLOB_FLAG_USE_CROSS_DEVICE;
   }

   return blob_flags;
}

static VkResult
virtgpu_bo_create_from_dma_buf(struct vn_renderer *renderer,
                               VkDeviceSize size,
                               int fd,
                               VkMemoryPropertyFlags flags,
                               struct vn_renderer_bo **out_bo)
{
   struct virtgpu *gpu = (struct virtgpu *)renderer;
   struct drm_virtgpu_resource_info info;
   uint32_t gem_handle = 0;
   struct virtgpu_bo *bo = NULL;

   mtx_lock(&gpu->dma_buf_import_mutex);

   gem_handle = virtgpu_ioctl_prime_fd_to_handle(gpu, fd);
   if (!gem_handle)
      goto fail;
   bo = util_sparse_array_get(&gpu->bo_array, gem_handle);

   if (virtgpu_ioctl_resource_info(gpu, gem_handle, &info))
      goto fail;

   /* Upon import, blob_flags is not passed to the kernel and is only for
    * internal use. Set it to what works best for us.
    * - blob mem: SHAREABLE + conditional MAPPABLE per VkMemoryPropertyFlags
    * - classic 3d: SHAREABLE only for export and to fail the map
    */
   uint32_t blob_flags = VIRTGPU_BLOB_FLAG_USE_SHAREABLE;
   size_t mmap_size = 0;
   if (info.blob_mem) {
      /* must be VIRTGPU_BLOB_MEM_HOST3D or VIRTGPU_BLOB_MEM_GUEST_VRAM */
      if (info.blob_mem != gpu->bo_blob_mem) {
         vn_log(gpu->instance,
                "dma-buf import failed: info.blob_mem(%u) != "
                "gpu->bo_blob_mem(%u)",
                info.blob_mem, gpu->bo_blob_mem);
         goto fail;
      }

      blob_flags |= virtgpu_bo_blob_flags(gpu, flags, 0);

      /* mmap_size is only used when mappable */
      mmap_size = 0;
      if (blob_flags & VIRTGPU_BLOB_FLAG_USE_MAPPABLE) {
         if (info.size < size) {
            /* If queried blob size is smaller than requested allocation size,
             * we drop the mappable flag to defer the mapping failure till the
             * app attempts to map the imported memory.
             */
            blob_flags &= ~VIRTGPU_BLOB_FLAG_USE_MAPPABLE;
         } else {
            /* Similar to virtgpu_bo_create_from_device_memory, the app can
             * do multiple imports with different sizes for suballocation. So
             * on the initial import, the mapping size has to be initialized
             * with the real size of the backing blob resource.
             */
            mmap_size = info.size;
         }
      }
   }

   /* we check bo->gem_handle instead of bo->refcount because bo->refcount
    * might only be memset to 0 and is not considered initialized in theory
    */
   if (bo->gem_handle == gem_handle) {
      if (bo->base.mmap_size < mmap_size) {
         vn_log(
            gpu->instance,
            "dma-buf import failed: bo->base.mmap_size(%zu) < mmap_size(%zu)",
            bo->base.mmap_size, mmap_size);
         goto fail;
      }
      if (blob_flags & ~bo->blob_flags) {
         vn_log(gpu->instance,
                "dma-buf import failed: blob_flags(%u) & ~bo->blob_flags(%u)",
                blob_flags, bo->blob_flags);
         goto fail;
      }

      /* we can't use vn_renderer_bo_ref as the refcount may drop to 0
       * temporarily before virtgpu_bo_destroy grabs the lock
       */
      vn_refcount_fetch_add_relaxed(&bo->base.refcount, 1);
   } else {
      *bo = (struct virtgpu_bo){
         .base = {
            .refcount = VN_REFCOUNT_INIT(1),
            .res_id = info.res_handle,
            .mmap_size = mmap_size,
         },
         .gem_handle = gem_handle,
         .blob_flags = blob_flags,
      };
   }

   mtx_unlock(&gpu->dma_buf_import_mutex);

   *out_bo = &bo->base;

   return VK_SUCCESS;

fail:
   if (gem_handle && bo->gem_handle != gem_handle)
      virtgpu_ioctl_gem_close(gpu, gem_handle);
   mtx_unlock(&gpu->dma_buf_import_mutex);
   return VK_ERROR_INVALID_EXTERNAL_HANDLE;
}

static VkResult
virtgpu_bo_create_from_device_memory(
   struct vn_renderer *renderer,
   struct vn_renderer_submit_batch *batch,
   VkDeviceSize size,
   vn_object_id mem_id,
   VkMemoryPropertyFlags flags,
   VkExternalMemoryHandleTypeFlags external_handles,
   struct vn_renderer_bo **out_bo)
{
   struct virtgpu *gpu = (struct virtgpu *)renderer;
   const uint32_t blob_flags =
      virtgpu_bo_blob_flags(gpu, flags, external_handles);

   uint32_t res_id;
   uint32_t gem_handle = virtgpu_ioctl_resource_create_blob(
      gpu, batch, gpu->bo_blob_mem, blob_flags, size, mem_id, &res_id);
   if (!gem_handle)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   /* There's a single underlying bo mapping shared by the initial alloc here
    * and the later import of the same. The mapping size has to be initialized
    * with the real size of the created blob resource, since the app can query
    * the exported native handle size for re-import. e.g. lseek dma-buf size
    */
   const uint32_t mappable_and_shareable =
      VIRTGPU_BLOB_FLAG_USE_MAPPABLE | VIRTGPU_BLOB_FLAG_USE_SHAREABLE;
   if ((blob_flags & mappable_and_shareable) == mappable_and_shareable) {
      struct drm_virtgpu_resource_info info;
      if (virtgpu_ioctl_resource_info(gpu, gem_handle, &info)) {
         virtgpu_ioctl_gem_close(gpu, gem_handle);
         return VK_ERROR_INVALID_EXTERNAL_HANDLE;
      }

      assert(info.blob_mem);
      if (info.size < size) {
         virtgpu_ioctl_gem_close(gpu, gem_handle);

         vn_log(gpu->instance,
                "blob mem create failed: info.size(%u) < size(%" PRIu64 ")",
                info.size, size);
         return VK_ERROR_INVALID_EXTERNAL_HANDLE;
      }

      size = info.size;
   }

   struct virtgpu_bo *bo = util_sparse_array_get(&gpu->bo_array, gem_handle);
   *bo = (struct virtgpu_bo){
      .base = {
         .refcount = VN_REFCOUNT_INIT(1),
         .res_id = res_id,
         .mmap_size = size,
      },
      .gem_handle = gem_handle,
      .blob_flags = blob_flags,
   };

   *out_bo = &bo->base;

   return VK_SUCCESS;
}

static void
virtgpu_shmem_destroy_now(struct vn_renderer *renderer,
                          struct vn_renderer_shmem *_shmem)
{
   struct virtgpu *gpu = (struct virtgpu *)renderer;
   struct virtgpu_shmem *shmem = (struct virtgpu_shmem *)_shmem;

   munmap(shmem->base.mmap_ptr, shmem->base.mmap_size);
   virtgpu_ioctl_gem_close(gpu, shmem->gem_handle);
}

static void
virtgpu_shmem_destroy(struct vn_renderer *renderer,
                      struct vn_renderer_shmem *shmem)
{
   struct virtgpu *gpu = (struct virtgpu *)renderer;

   if (vn_renderer_shmem_cache_add(&gpu->shmem_cache, shmem))
      return;

   virtgpu_shmem_destroy_now(&gpu->base, shmem);
}

static struct vn_renderer_shmem *
virtgpu_shmem_create(struct vn_renderer *renderer, size_t size)
{
   struct virtgpu *gpu = (struct virtgpu *)renderer;

   struct vn_renderer_shmem *cached_shmem =
      vn_renderer_shmem_cache_get(&gpu->shmem_cache, size);
   if (cached_shmem) {
      cached_shmem->refcount = VN_REFCOUNT_INIT(1);
      return cached_shmem;
   }

   uint32_t res_id;
   uint32_t gem_handle = virtgpu_ioctl_resource_create_blob(
      gpu, NULL, gpu->shmem_blob_mem, VIRTGPU_BLOB_FLAG_USE_MAPPABLE, size, 0,
      &res_id);
   if (!gem_handle)
      return NULL;

   void *ptr = virtgpu_ioctl_map(gpu, gem_handle, size, NULL);
   if (!ptr) {
      virtgpu_ioctl_gem_close(gpu, gem_handle);
      return NULL;
   }

   struct virtgpu_shmem *shmem =
      util_sparse_array_get(&gpu->shmem_array, gem_handle);
   *shmem = (struct virtgpu_shmem){
      .base = {
         .refcount = VN_REFCOUNT_INIT(1),
         .res_id = res_id,
         .mmap_size = size,
         .mmap_ptr = ptr,
      },
      .gem_handle = gem_handle,
   };

   return &shmem->base;
}

static VkResult
virtgpu_wait(struct vn_renderer *renderer,
             const struct vn_renderer_wait *wait)
{
   struct virtgpu *gpu = (struct virtgpu *)renderer;

   /* always enable wait-before-submit */
   uint32_t flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT;
   if (!wait->wait_any)
      flags |= DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL;

   STACK_ARRAY(uint32_t, syncobj_handles, wait->sync_count);

   for (uint32_t i = 0; i < wait->sync_count; i++)
      syncobj_handles[i] = wait->syncs[i]->syncobj_handle;

   /* syncobj timeout is signed */
   uint64_t abs_timeout_ns = os_time_get_absolute_timeout(wait->timeout);
   abs_timeout_ns = MIN2(abs_timeout_ns, (uint64_t)INT64_MAX);

   int ret;
   if (renderer->info.has_timeline_sync) {
      ret = gpu->sync->timeline_wait(
         gpu->sync, syncobj_handles, (uint64_t *)wait->sync_values,
         wait->sync_count, abs_timeout_ns, flags, NULL);
   } else {
      ret = gpu->sync->wait(gpu->sync, syncobj_handles, wait->sync_count,
                            abs_timeout_ns, flags, NULL);
   }

   STACK_ARRAY_FINISH(syncobj_handles);

   if (ret && errno != ETIME)
      return VK_ERROR_DEVICE_LOST;

   return ret ? VK_TIMEOUT : VK_SUCCESS;
}

static VkResult
virtgpu_submit(struct vn_renderer *renderer,
               const struct vn_renderer_submit_batch *batch)
{
   struct virtgpu *gpu = (struct virtgpu *)renderer;

   STACK_ARRAY(struct drm_virtgpu_execbuffer_syncobj, out_syncobjs,
               batch->sync_count);
   for (uint32_t i = 0; i < batch->sync_count; i++) {
      out_syncobjs[i] = (struct drm_virtgpu_execbuffer_syncobj){
         .handle = batch->syncs[i]->syncobj_handle,
         .point = batch->sync_values[i],
      };
   }

   struct drm_virtgpu_execbuffer args = {
      .flags = VIRTGPU_EXECBUF_RING_IDX,
      .size = batch->cs_size,
      .command = (uintptr_t)batch->cs_data,
      .ring_idx = batch->ring_idx,
   };
   if (renderer->info.has_timeline_sync) {
      args.syncobj_stride = sizeof(struct drm_virtgpu_execbuffer_syncobj);
      args.num_out_syncobjs = batch->sync_count;
      args.out_syncobjs = (uintptr_t)out_syncobjs;
   } else if (batch->sync_count) {
      args.flags |= VIRTGPU_EXECBUF_FENCE_FD_OUT;
   }

   int ret = virtgpu_ioctl(gpu, DRM_IOCTL_VIRTGPU_EXECBUFFER, &args);

   if (!renderer->info.has_timeline_sync && !ret && batch->sync_count) {
      for (uint32_t i = 0; i < batch->sync_count; i++) {
         ret = gpu->sync->import_sync_file(
            gpu->sync, batch->syncs[i]->syncobj_handle, args.fence_fd);
         if (ret)
            break;
      }

      close(args.fence_fd);
   }

   STACK_ARRAY_FINISH(out_syncobjs);

   return ret ? VK_ERROR_DEVICE_LOST : VK_SUCCESS;
}

static void
virtgpu_init_renderer_info(struct virtgpu *gpu)
{
   struct vn_renderer_info *info = &gpu->base.info;

   info->drm.props = (VkPhysicalDeviceDrmPropertiesEXT){
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT,
      .hasPrimary = gpu->has_primary,
      .hasRender = true,
      .primaryMajor = gpu->primary_major,
      .primaryMinor = gpu->primary_minor,
      .renderMajor = gpu->render_major,
      .renderMinor = gpu->render_minor,
   };

   info->pci.vendor_id = VIRTGPU_PCI_VENDOR_ID;
   info->pci.device_id = VIRTGPU_PCI_DEVICE_ID;

   if (gpu->bustype == DRM_BUS_PCI) {
      info->pci.has_bus_info = true;
      info->pci.props = (VkPhysicalDevicePCIBusInfoPropertiesEXT){
         .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT,
         .pciDomain = gpu->pci_bus_info.domain,
         .pciBus = gpu->pci_bus_info.bus,
         .pciDevice = gpu->pci_bus_info.dev,
         .pciFunction = gpu->pci_bus_info.func,
      };
   }

   info->has_dma_buf_import = true;
   info->has_external_sync = true;

   assert(gpu->sync);
   info->has_timeline_sync =
      !VN_PERF(NO_TIMELINE_SYNC) && !!gpu->sync->timeline_signal;

   info->has_implicit_fencing = false;

   const struct virgl_renderer_capset_venus *capset = &gpu->capset.data;
   info->wire_format_version = capset->wire_format_version;
   info->vk_xml_version = capset->vk_xml_version;
   info->vk_ext_command_serialization_spec_version =
      capset->vk_ext_command_serialization_spec_version;
   info->vk_mesa_venus_protocol_spec_version =
      capset->vk_mesa_venus_protocol_spec_version;
   assert(capset->supports_blob_id_0);

   /* ensure vk_extension_mask is large enough to hold all capset masks */
   STATIC_ASSERT(sizeof(info->vk_extension_mask) >=
                 sizeof(capset->vk_extension_mask1));
   memcpy(info->vk_extension_mask, capset->vk_extension_mask1,
          sizeof(capset->vk_extension_mask1));

   assert(capset->allow_vk_wait_syncs);

   assert(capset->supports_multiple_timelines);
   info->max_timeline_count = gpu->max_timeline_count;

   if (gpu->bo_blob_mem == VIRTGPU_BLOB_MEM_GUEST_VRAM)
      info->has_guest_vram = true;

   /* Use guest blob allocations from dedicated heap (Host visible memory) */
   if (gpu->bo_blob_mem == VIRTGPU_BLOB_MEM_HOST3D && capset->use_guest_vram)
      info->has_guest_vram = true;
}

static void
virtgpu_destroy(struct vn_renderer *renderer,
                const VkAllocationCallbacks *alloc)
{
   struct virtgpu *gpu = (struct virtgpu *)renderer;

   vn_renderer_shmem_cache_fini(&gpu->shmem_cache);

   if (gpu->sync)
      gpu->sync->finalize(gpu->sync);

   if (gpu->fd >= 0)
      close(gpu->fd);

   mtx_destroy(&gpu->dma_buf_import_mutex);

   util_sparse_array_finish(&gpu->shmem_array);
   util_sparse_array_finish(&gpu->bo_array);

   vk_free(alloc, gpu);
}

static inline void
virtgpu_init_shmem_blob_mem(ASSERTED struct virtgpu *gpu)
{
   /* VIRTGPU_BLOB_MEM_GUEST allocates from the guest system memory.  They are
    * logically contiguous in the guest but are sglists (iovecs) in the host.
    * That makes them slower to process in the host.  With host process
    * isolation, it also becomes impossible for the host to access sglists
    * directly.
    *
    * While there are ideas (and shipped code in some cases) such as creating
    * udmabufs from sglists, or having a dedicated guest heap, it seems the
    * easiest way is to reuse VIRTGPU_BLOB_MEM_HOST3D.  That is, when the
    * renderer sees a request to export a blob where
    *
    *  - blob_mem is VIRTGPU_BLOB_MEM_HOST3D
    *  - blob_flags is VIRTGPU_BLOB_FLAG_USE_MAPPABLE
    *  - blob_id is 0
    *
    * it allocates a host shmem.
    *
    * supports_blob_id_0 has been enforced by mandated render server config.
    */
   assert(gpu->capset.data.supports_blob_id_0);
   gpu->shmem_blob_mem = VIRTGPU_BLOB_MEM_HOST3D;
}

static inline void
virtgpu_init_sync_provider(struct virtgpu *gpu)
{
   /* Without virtgpu syncobj uAPI support (before 6.6 kernel), fallback to
    * simulated syncobj. Here we rely on util_sync_provider::timeline_signal
    * being conditioned upon DRM_CAP_SYNCOBJ_TIMELINE.
    */
   if (!VN_DEBUG(NO_DRM_SYNCOBJ)) {
      gpu->sync = util_sync_provider_drm(gpu->fd);
      if (!gpu->sync->timeline_signal) {
         gpu->sync->finalize(gpu->sync);
         gpu->sync = NULL;
      }
   }

   if (!gpu->sync)
      gpu->sync = vn_renderer_sim_syncobj_get_sync();
}

static VkResult
virtgpu_init_context(struct virtgpu *gpu)
{
   assert(!gpu->capset.version);
   const int ret = virtgpu_ioctl_context_init(gpu, gpu->capset.id);
   if (ret) {
      if (VN_DEBUG(INIT)) {
         vn_log(gpu->instance, "failed to initialize context: %s",
                strerror(errno));
      }
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   return VK_SUCCESS;
}

static VkResult
virtgpu_init_capset(struct virtgpu *gpu)
{
   gpu->capset.id = VIRTGPU_DRM_CAPSET_VENUS;
   gpu->capset.version = 0;

   const int ret =
      virtgpu_ioctl_get_caps(gpu, gpu->capset.id, gpu->capset.version,
                             &gpu->capset.data, sizeof(gpu->capset.data));
   if (ret) {
      if (VN_DEBUG(INIT)) {
         vn_log(gpu->instance, "failed to get venus v%d capset: %s",
                gpu->capset.version, strerror(errno));
      }
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   if (gpu->capset.data.wire_format_version == 0) {
      if (VN_DEBUG(INIT)) {
         vn_log(gpu->instance, "Unsupported wire format version %u",
                gpu->capset.data.wire_format_version);
      }
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   return VK_SUCCESS;
}

static VkResult
virtgpu_init_params(struct virtgpu *gpu)
{
   const uint64_t required_params[] = {
      VIRTGPU_PARAM_3D_FEATURES,
      VIRTGPU_PARAM_CAPSET_QUERY_FIX,
      VIRTGPU_PARAM_RESOURCE_BLOB,
      VIRTGPU_PARAM_CONTEXT_INIT,
   };
   uint64_t val;
   for (uint32_t i = 0; i < ARRAY_SIZE(required_params); i++) {
      val = virtgpu_ioctl_getparam(gpu, required_params[i]);
      if (!val) {
         if (VN_DEBUG(INIT)) {
            vn_log(gpu->instance, "required kernel param %d is missing",
                   (int)required_params[i]);
         }
         return VK_ERROR_INITIALIZATION_FAILED;
      }
   }

   val = virtgpu_ioctl_getparam(gpu, VIRTGPU_PARAM_HOST_VISIBLE);
   if (val) {
      gpu->bo_blob_mem = VIRTGPU_BLOB_MEM_HOST3D;
   } else {
      val = virtgpu_ioctl_getparam(gpu, VIRTGPU_PARAM_GUEST_VRAM);
      if (val) {
         gpu->bo_blob_mem = VIRTGPU_BLOB_MEM_GUEST_VRAM;
      }
   }

   if (!val) {
      vn_log(gpu->instance,
             "one of required kernel params (%d or %d) is missing",
             (int)VIRTGPU_PARAM_HOST_VISIBLE, (int)VIRTGPU_PARAM_GUEST_VRAM);
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   /* Cross-device feature is optional.  It enables sharing dma-bufs
    * with other virtio devices, like virtio-wl or virtio-video used
    * by ChromeOS VMs.  Qemu doesn't support cross-device sharing.
    */
   val = virtgpu_ioctl_getparam(gpu, VIRTGPU_PARAM_CROSS_DEVICE);
   if (val)
      gpu->supports_cross_device = true;

   /* implied by CONTEXT_INIT uapi */
   gpu->max_timeline_count = 64;

   return VK_SUCCESS;
}

static VkResult
virtgpu_open_device(struct virtgpu *gpu, const drmDevicePtr dev)
{
   bool supported_bus = false;

   switch (dev->bustype) {
   case DRM_BUS_PCI:
      if (dev->deviceinfo.pci->vendor_id == VIRTGPU_PCI_VENDOR_ID &&
          dev->deviceinfo.pci->device_id == VIRTGPU_PCI_DEVICE_ID)
         supported_bus = true;
      break;
   case DRM_BUS_PLATFORM:
      supported_bus = true;
      break;
   default:
      break;
   }

   if (!supported_bus || !(dev->available_nodes & (1 << DRM_NODE_RENDER))) {
      if (VN_DEBUG(INIT)) {
         const char *name = "unknown";
         for (uint32_t i = 0; i < DRM_NODE_MAX; i++) {
            if (dev->available_nodes & (1 << i)) {
               name = dev->nodes[i];
               break;
            }
         }
         vn_log(gpu->instance, "skipping DRM device %s", name);
      }
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   const char *primary_path = dev->nodes[DRM_NODE_PRIMARY];
   const char *node_path = dev->nodes[DRM_NODE_RENDER];

   int fd = open(node_path, O_RDWR | O_CLOEXEC);
   if (fd < 0) {
      if (VN_DEBUG(INIT))
         vn_log(gpu->instance, "failed to open %s", node_path);
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   drmVersionPtr version = drmGetVersion(fd);
   if (!version || strcmp(version->name, "virtio_gpu") ||
       version->version_major != 0) {
      if (VN_DEBUG(INIT)) {
         if (version) {
            vn_log(gpu->instance, "unknown DRM driver %s version %d",
                   version->name, version->version_major);
         } else {
            vn_log(gpu->instance, "failed to get DRM driver version");
         }
      }
      if (version)
         drmFreeVersion(version);
      close(fd);
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   gpu->fd = fd;

   struct stat st;
   if (stat(primary_path, &st) == 0) {
      gpu->has_primary = true;
      gpu->primary_major = major(st.st_rdev);
      gpu->primary_minor = minor(st.st_rdev);
   } else {
      gpu->has_primary = false;
      gpu->primary_major = 0;
      gpu->primary_minor = 0;
   }
   stat(node_path, &st);
   gpu->render_major = major(st.st_rdev);
   gpu->render_minor = minor(st.st_rdev);

   gpu->bustype = dev->bustype;
   if (dev->bustype == DRM_BUS_PCI)
      gpu->pci_bus_info = *dev->businfo.pci;

   drmFreeVersion(version);

   if (VN_DEBUG(INIT))
      vn_log(gpu->instance, "using DRM device %s", node_path);

   return VK_SUCCESS;
}

static VkResult
virtgpu_open(struct virtgpu *gpu)
{
   drmDevicePtr devs[8];
   int count = drmGetDevices2(0, devs, ARRAY_SIZE(devs));
   if (count < 0) {
      if (VN_DEBUG(INIT))
         vn_log(gpu->instance, "failed to enumerate DRM devices");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   VkResult result = VK_ERROR_INITIALIZATION_FAILED;
   for (int i = 0; i < count; i++) {
      result = virtgpu_open_device(gpu, devs[i]);
      if (result == VK_SUCCESS)
         break;
   }

   drmFreeDevices(devs, count);

   return result;
}

static VkResult
virtgpu_init(struct virtgpu *gpu)
{
   util_sparse_array_init(&gpu->shmem_array, sizeof(struct virtgpu_shmem),
                          1024);
   util_sparse_array_init(&gpu->bo_array, sizeof(struct virtgpu_bo), 1024);

   mtx_init(&gpu->dma_buf_import_mutex, mtx_plain);

   VkResult result = virtgpu_open(gpu);
   if (result == VK_SUCCESS)
      result = virtgpu_init_params(gpu);
   if (result == VK_SUCCESS)
      result = virtgpu_init_capset(gpu);
   if (result == VK_SUCCESS)
      result = virtgpu_init_context(gpu);
   if (result != VK_SUCCESS)
      return result;

   virtgpu_init_shmem_blob_mem(gpu);
   virtgpu_init_sync_provider(gpu);

   vn_renderer_shmem_cache_init(&gpu->shmem_cache, &gpu->base,
                                virtgpu_shmem_destroy_now);

   virtgpu_init_renderer_info(gpu);

   gpu->base.ops.destroy = virtgpu_destroy;
   gpu->base.ops.submit = virtgpu_submit;
   gpu->base.ops.wait = virtgpu_wait;

   gpu->base.shmem_ops.create = virtgpu_shmem_create;
   gpu->base.shmem_ops.destroy = virtgpu_shmem_destroy;

   gpu->base.bo_ops.create_from_device_memory =
      virtgpu_bo_create_from_device_memory;
   gpu->base.bo_ops.create_from_dma_buf = virtgpu_bo_create_from_dma_buf;
   gpu->base.bo_ops.destroy = virtgpu_bo_destroy;
   gpu->base.bo_ops.export_dma_buf = virtgpu_bo_export_dma_buf;
   gpu->base.bo_ops.export_sync_file =
      vn_renderer_bo_export_sync_file_internal;
   gpu->base.bo_ops.map = virtgpu_bo_map;
   gpu->base.bo_ops.flush = virtgpu_bo_flush;
   gpu->base.bo_ops.invalidate = virtgpu_bo_invalidate;

   gpu->base.sync_ops.create = virtgpu_sync_create;
   gpu->base.sync_ops.create_from_syncobj = virtgpu_sync_create_from_syncobj;
   gpu->base.sync_ops.destroy = virtgpu_sync_destroy;
   gpu->base.sync_ops.export_syncobj = virtgpu_sync_export_syncobj;
   gpu->base.sync_ops.reset = virtgpu_sync_reset;
   gpu->base.sync_ops.read = virtgpu_sync_read;
   gpu->base.sync_ops.write = virtgpu_sync_write;

   return VK_SUCCESS;
}

VkResult
vn_renderer_create_virtgpu(struct vn_instance *instance,
                           const VkAllocationCallbacks *alloc,
                           struct vn_renderer **renderer)
{
   struct virtgpu *gpu = vk_zalloc(alloc, sizeof(*gpu), VN_DEFAULT_ALIGN,
                                   VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!gpu)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   gpu->instance = instance;
   gpu->fd = -1;

   VkResult result = virtgpu_init(gpu);
   if (result != VK_SUCCESS) {
      virtgpu_destroy(&gpu->base, alloc);
      return result;
   }

   *renderer = &gpu->base;

   if (VN_DEBUG(INIT))
      vn_log(gpu->instance, "virtgpu backend initialized");

   return VK_SUCCESS;
}
