/*
 * Copyright © 2012-2018 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "util/os_file.h"
#include "util/u_process.h"

#include "freedreno_rd_output.h"

#include "freedreno_drmif.h"
#include "freedreno_drm_perfetto.h"
#include "freedreno_priv.h"

struct fd_device *msm_device_new(int fd, drmVersionPtr version);
#ifdef HAVE_FREEDRENO_VIRTIO
struct fd_device *virtio_device_new(int fd, drmVersionPtr version);
#endif
#ifdef HAVE_FREEDRENO_KGSL
struct fd_device *kgsl_device_new(int fd);
#endif

uint64_t os_page_size = 4096;

struct fd_device *
fd_device_new(int fd)
{
   struct fd_device *dev = NULL;
   drmVersionPtr version = NULL;
   bool use_heap = false;
   bool support_use_heap = true;
   /* The fd used for GPU submission. Defaults to the fd we were handed, but on
    * the kgsl stack it is redirected to the kgsl GPU node while the original fd
    * is retained as the device control/identity fd (see below). */
   int gpu_fd = fd;

   os_get_page_size(&os_page_size);

#if HAVE_FREEDRENO_KGSL
   /* On the kgsl stack the Adreno GPU is reachable only through the
    * /dev/kgsl-3d0 char device, never through a DRM render node.  EGL/GBM may
    * hand us the display controller's DRM node (sde-kms renderD128), which
    * drmGetVersion reports as "msm" even though it drives no GPU at all -
    * taking the msm path there yields a half-initialised screen that crashes
    * on the first capability query.  When kgsl is forced (the whole kgsl stack
    * exports FD_FORCE_KGSL=1), ignore the fd we were handed and open the kgsl
    * GPU node directly for rendering.  The fd we were handed is kept as the
    * device control/identity fd (dev->control_fd, returned by fd_device_fd):
    * it still identifies the screen for u_pipe_screen_lookup_or_create()'s
    * fd-keyed cache (which compares file descriptions, so dev->fd MUST stay a
    * dup of the cache key - a freshly opened kgsl fd is a different file
    * description and would break cache eviction -> use-after-free) and is the
    * fd handed to DRI3 clients.  Only GPU submission uses the kgsl fd. */
   if (debug_get_bool_option("FD_FORCE_KGSL", false)) {
      int kgsl_fd = open("/dev/kgsl-3d0", O_RDWR | O_CLOEXEC);
      if (kgsl_fd >= 0) {
         dev = kgsl_device_new(kgsl_fd);
         if (dev) {
            /* Userspace fences are not supported with KGSL */
            support_use_heap = false;
            gpu_fd = kgsl_fd;   /* render on kgsl, keep fd as control_fd */
            goto out;
         }
         close(kgsl_fd);
      }
   }
#endif

#ifdef HAVE_LIBDRM
   /* figure out if we are kgsl or msm drm driver: */
   version = drmGetVersion(fd);
   if (!version)
      DEBUG_MSG("cannot get version: %s", strerror(errno));
#endif

#ifdef HAVE_FREEDRENO_VIRTIO
   if (debug_get_bool_option("FD_FORCE_VTEST", false)) {
      DEBUG_MSG("virtio_gpu vtest device");
      dev = virtio_device_new(-1, version);
   } else
#endif
   if (version && !strcmp(version->name, "msm")) {
      DEBUG_MSG("msm DRM device");
      if (version->version_major != 1) {
         ERROR_MSG("unsupported version: %u.%u.%u", version->version_major,
                   version->version_minor, version->version_patchlevel);
         goto out;
      }

      dev = msm_device_new(fd, version);
#ifdef HAVE_FREEDRENO_VIRTIO
   } else if (version && !strcmp(version->name, "virtio_gpu")) {
      DEBUG_MSG("virtio_gpu DRM device");
      dev = virtio_device_new(fd, version);
      /* Only devices that support a hypervisor are a6xx+, so avoid the
       * extra guest<->host round trips associated with pipe creation:
       */
      use_heap = true;
#endif
#if HAVE_FREEDRENO_KGSL
   } else {
      /* If drm driver not detected assume this is KGSL */
      dev = kgsl_device_new(fd);
      /* Userspace fences are not supported with KGSL */
      support_use_heap = false;
      if (dev)
         goto out;
#endif
   }

#if HAVE_FREEDRENO_KGSL
   /* On the kgsl stack the Adreno GPU is reachable only through the
    * /dev/kgsl-3d0 char device, never through a DRM render node.  When EGL is
    * driven via GBM (e.g. Xwayland glamor) the fd handed to us is the display
    * controller's DRM node (sde-kms renderD128) or some other non-kgsl fd, so
    * neither the msm path nor kgsl-on-this-fd above can produce a device.
    * Open the kgsl GPU node directly as a last resort so native freedreno GL
    * still comes up instead of failing dri2 screen creation. */
   if (!dev) {
      int kgsl_fd = open("/dev/kgsl-3d0", O_RDWR | O_CLOEXEC);
      if (kgsl_fd >= 0) {
         dev = kgsl_device_new(kgsl_fd);
         if (dev) {
            support_use_heap = false;
            /* Render on the kgsl GPU node; keep the fd we were handed as the
             * control/identity fd (see the FD_FORCE_KGSL block above). */
            gpu_fd = kgsl_fd;
         } else {
            close(kgsl_fd);
         }
      }
   }
#endif

   if (!dev) {
      INFO_MSG("unsupported device: %s", version ? version->name : "(none)");
      goto out;
   }

out:
   drmFreeVersion(version);

   if (!dev)
      return NULL;

   fd_drm_perfetto_init();

   fd_rd_dump_env_init();
   fd_rd_output_init(&dev->rd, util_get_process_name());

   p_atomic_set(&dev->refcnt, 1);
   dev->fd = gpu_fd;
   /* control_fd identifies the screen for the fd-keyed screen cache and DRI3;
    * it is a dup of the cache key. It equals fd unless rendering was redirected
    * to a separate kgsl GPU node above, in which case fd != gpu_fd. */
   dev->control_fd = fd;
   dev->handle_table =
      _mesa_hash_table_create(NULL, _mesa_hash_u32, _mesa_key_u32_equal);
   dev->name_table =
      _mesa_hash_table_create(NULL, _mesa_hash_u32, _mesa_key_u32_equal);
   fd_bo_cache_init(&dev->bo_cache, false, "bo");
   fd_bo_cache_init(&dev->ring_cache, true, "ring");

   list_inithead(&dev->deferred_submits);
   simple_mtx_init(&dev->submit_lock, mtx_plain);
   simple_mtx_init(&dev->suballoc_lock, mtx_plain);

   if (!use_heap) {
      struct fd_pipe *pipe = fd_pipe_new(dev, FD_PIPE_3D);

      if (!pipe)
         goto fail;

      /* Userspace fences don't appear to be reliable enough (missing some
       * cache flushes?) on older gens, so limit sub-alloc heaps to a6xx+
       * for now:
       */
      use_heap = fd_dev_gen(&pipe->dev_id) >= 6;

      fd_pipe_del(pipe);
   }

   if (support_use_heap && use_heap) {
      dev->ring_heap = fd_bo_heap_new(dev, RING_FLAGS);
      dev->default_heap = fd_bo_heap_new(dev, 0);
   }

   return dev;

fail:
   fd_device_del(dev);
   return NULL;
}

/* like fd_device_new() but creates it's own private dup() of the fd
 * which is close()d when the device is finalized.
 */
struct fd_device *
fd_device_new_dup(int fd)
{
   int dup_fd = os_dupfd_cloexec(fd);
   struct fd_device *dev = fd_device_new(dup_fd);
   if (dev)
      dev->closefd = 1;
   else
      close(dup_fd);
   return dev;
}

/* Convenience helper to open the drm device and return new fd_device:
 */
struct fd_device *
fd_device_open(void)
{
   int fd;

   fd = drmOpenWithType("msm", NULL, DRM_NODE_RENDER);
#ifdef HAVE_FREEDRENO_VIRTIO
   if (fd < 0)
      fd = drmOpenWithType("virtio_gpu", NULL, DRM_NODE_RENDER);
#endif
   if (fd < 0)
      return NULL;

   return fd_device_new(fd);
}

struct fd_device *
fd_device_ref(struct fd_device *dev)
{
   ref(&dev->refcnt);
   return dev;
}

void
fd_device_purge(struct fd_device *dev)
{
   fd_bo_cache_cleanup(&dev->bo_cache, 0);
   fd_bo_cache_cleanup(&dev->ring_cache, 0);
}

void
fd_device_del(struct fd_device *dev)
{
   if (!unref(&dev->refcnt))
      return;

   fd_rd_output_fini(&dev->rd);

   assert(list_is_empty(&dev->deferred_submits));
   assert(!dev->deferred_submits_fence);

   if (dev->suballoc_bo)
      fd_bo_del(dev->suballoc_bo);

   if (dev->ring_heap)
      fd_bo_heap_destroy(dev->ring_heap);

   if (dev->default_heap)
      fd_bo_heap_destroy(dev->default_heap);

   fd_bo_cache_cleanup(&dev->bo_cache, 0);
   fd_bo_cache_cleanup(&dev->ring_cache, 0);

   /* Needs to be after bo cache cleanup in case backend has a
    * util_vma_heap that it destroys:
    */
   dev->funcs->destroy(dev);

   _mesa_hash_table_destroy(dev->handle_table, NULL);
   _mesa_hash_table_destroy(dev->name_table, NULL);

   if (fd_device_threaded_submit(dev))
      util_queue_destroy(&dev->submit_queue);

   if (dev->closefd) {
      close(dev->fd);
      /* On the kgsl stack control_fd is a distinct fd from the kgsl GPU fd;
       * close it too so it does not leak (guard against double-close when they
       * are the same fd, the common case). */
      if (dev->control_fd >= 0 && dev->control_fd != dev->fd)
         close(dev->control_fd);
   }

   free(dev);
}

int
fd_device_fd(struct fd_device *dev)
{
   /* Return the control/identity fd, not the GPU submission fd. On the kgsl
    * stack these differ: the screen cache and DRI3 must see the fd we were
    * handed (a dup of the cache key), while GPU submission uses dev->fd. */
   return dev->control_fd;
}

enum fd_version
fd_device_version(struct fd_device *dev)
{
   return dev->version;
}

void
fd_device_disable_explicit_sync_heuristic(struct fd_device *dev)
{
   dev->disable_explicit_sync_heuristic = true;
}

DEBUG_GET_ONCE_BOOL_OPTION(libgl, "LIBGL_DEBUG", false)

bool
fd_dbg(void)
{
   return debug_get_option_libgl();
}

uint32_t
fd_get_features(struct fd_device *dev)
{
   return dev->features;
}

bool
fd_has_syncobj(struct fd_device *dev)
{
   uint64_t value;
   if (drmGetCap(dev->fd, DRM_CAP_SYNCOBJ, &value))
      return false;
   return value && dev->version >= FD_VERSION_FENCE_FD;
}
