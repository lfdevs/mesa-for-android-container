/*
 * Copyright 2019 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors (Collabora):
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <xf86drm.h>
#include <pthread.h>
#include "drm-uapi/panfrost_drm.h"

#include "pan_bo.h"
#include "pan_screen.h"
#include "pan_util.h"
#include "pandecode/decode.h"

#include "os/os_mman.h"

#include "util/u_inlines.h"
#include "util/u_math.h"

/* This file implements a userspace BO cache. Allocating and freeing
 * GPU-visible buffers is very expensive, and even the extra kernel roundtrips
 * adds more work than we would like at this point. So caching BOs in userspace
 * solves both of these problems and does not require kernel updates.
 *
 * Cached BOs are sorted into a bucket based on rounding their size down to the
 * nearest power-of-two. Each bucket contains a linked list of free panfrost_bo
 * objects. Putting a BO into the cache is accomplished by adding it to the
 * corresponding bucket. Getting a BO from the cache consists of finding the
 * appropriate bucket and sorting. A cache eviction is a kernel-level free of a
 * BO and removing it from the bucket. We special case evicting all BOs from
 * the cache, since that's what helpful in practice and avoids extra logic
 * around the linked list.
 */

static struct panfrost_bo *
panfrost_bo_alloc(struct panfrost_screen *screen, size_t size,
                  uint32_t flags)
{
        struct drm_panfrost_create_bo create_bo = { .size = size };
        struct panfrost_bo *bo;
        int ret;

        if (screen->kernel_version->version_major > 1 ||
            screen->kernel_version->version_minor >= 1) {
                if (flags & PAN_BO_GROWABLE)
                        create_bo.flags |= PANFROST_BO_HEAP;
                if (!(flags & PAN_BO_EXECUTE))
                        create_bo.flags |= PANFROST_BO_NOEXEC;
        }

        ret = drmIoctl(screen->fd, DRM_IOCTL_PANFROST_CREATE_BO, &create_bo);
        if (ret) {
                fprintf(stderr, "DRM_IOCTL_PANFROST_CREATE_BO failed: %m\n");
                return NULL;
        }

        bo = rzalloc(screen, struct panfrost_bo);
        assert(bo);
        bo->size = create_bo.size;
        bo->gpu = create_bo.offset;
        bo->gem_handle = create_bo.handle;
        bo->flags = flags;
        bo->screen = screen;
        return bo;
}

static void
panfrost_bo_free(struct panfrost_bo *bo)
{
        struct drm_gem_close gem_close = { .handle = bo->gem_handle };
        int ret;

        ret = drmIoctl(bo->screen->fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
        if (ret) {
                fprintf(stderr, "DRM_IOCTL_GEM_CLOSE failed: %m\n");
                assert(0);
        }

        ralloc_free(bo);
}

/* Returns true if the BO is ready, false otherwise.
 * access_type is encoding the type of access one wants to ensure is done.
 * Say you want to make sure all writers are done writing, you should pass
 * PAN_BO_ACCESS_WRITE.
 * If you want to wait for all users, you should pass PAN_BO_ACCESS_RW.
 * PAN_BO_ACCESS_READ would work too as waiting for readers implies
 * waiting for writers as well, but we want to make things explicit and waiting
 * only for readers is impossible.
 */
bool
panfrost_bo_wait(struct panfrost_bo *bo, int64_t timeout_ns,
                 uint32_t access_type)
{
        struct drm_panfrost_wait_bo req = {
                .handle = bo->gem_handle,
		.timeout_ns = timeout_ns,
        };
        int ret;

        assert(access_type == PAN_BO_ACCESS_WRITE ||
               access_type == PAN_BO_ACCESS_RW);

        /* If the BO has been exported or imported we can't rely on the cached
         * state, we need to call the WAIT_BO ioctl.
         */
        if (!(bo->flags & (PAN_BO_IMPORTED | PAN_BO_EXPORTED))) {
                /* If ->gpu_access is 0, the BO is idle, no need to wait. */
                if (!bo->gpu_access)
                        return true;

                /* If the caller only wants to wait for writers and no
                 * writes are pending, we don't have to wait.
                 */
                if (access_type == PAN_BO_ACCESS_WRITE &&
                    !(bo->gpu_access & PAN_BO_ACCESS_WRITE))
                        return true;
        }

        /* The ioctl returns >= 0 value when the BO we are waiting for is ready
         * -1 otherwise.
         */
        ret = drmIoctl(bo->screen->fd, DRM_IOCTL_PANFROST_WAIT_BO, &req);
        if (ret != -1) {
                /* Set gpu_access to 0 so that the next call to bo_wait()
                 * doesn't have to call the WAIT_BO ioctl.
                 */
                bo->gpu_access = 0;
                return true;
        }

        /* If errno is not ETIMEDOUT or EBUSY that means the handle we passed
         * is invalid, which shouldn't happen here.
         */
        assert(errno == ETIMEDOUT || errno == EBUSY);
        return false;
}

/* Helper to calculate the bucket index of a BO */

static unsigned
pan_bucket_index(unsigned size)
{
        /* Round down to POT to compute a bucket index */

        unsigned bucket_index = util_logbase2(size);

        /* Clamp the bucket index; all huge allocations will be
         * sorted into the largest bucket */

        bucket_index = MIN2(bucket_index, MAX_BO_CACHE_BUCKET);

        /* The minimum bucket size must equal the minimum allocation
         * size; the maximum we clamped */

        assert(bucket_index >= MIN_BO_CACHE_BUCKET);
        assert(bucket_index <= MAX_BO_CACHE_BUCKET);

        /* Reindex from 0 */
        return (bucket_index - MIN_BO_CACHE_BUCKET);
}

static struct list_head *
pan_bucket(struct panfrost_screen *screen, unsigned size)
{
        return &screen->bo_cache[pan_bucket_index(size)];
}

/* Tries to fetch a BO of sufficient size with the appropriate flags from the
 * BO cache. If it succeeds, it returns that BO and removes the BO from the
 * cache. If it fails, it returns NULL signaling the caller to allocate a new
 * BO. */

static struct panfrost_bo *
panfrost_bo_cache_fetch(struct panfrost_screen *screen,
                        size_t size, uint32_t flags, bool dontwait)
{
        pthread_mutex_lock(&screen->bo_cache_lock);
        struct list_head *bucket = pan_bucket(screen, size);
        struct panfrost_bo *bo = NULL;

        /* Iterate the bucket looking for something suitable */
        list_for_each_entry_safe(struct panfrost_bo, entry, bucket, link) {
                if (entry->size < size || entry->flags != flags)
                        continue;

                if (!panfrost_bo_wait(entry, dontwait ? 0 : INT64_MAX,
                                      PAN_BO_ACCESS_RW))
                        continue;

                struct drm_panfrost_madvise madv = {
                        .handle = entry->gem_handle,
                        .madv = PANFROST_MADV_WILLNEED,
                };
                int ret;

                /* This one works, splice it out of the cache */
                list_del(&entry->link);

                ret = drmIoctl(screen->fd, DRM_IOCTL_PANFROST_MADVISE, &madv);
                if (!ret && !madv.retained) {
                        panfrost_bo_free(entry);
                        continue;
                }
                /* Let's go! */
                bo = entry;
                break;
        }
        pthread_mutex_unlock(&screen->bo_cache_lock);

        return bo;
}

/* Tries to add a BO to the cache. Returns if it was
 * successful */

static bool
panfrost_bo_cache_put(struct panfrost_bo *bo)
{
        struct panfrost_screen *screen = bo->screen;

        if (bo->flags & PAN_BO_DONT_REUSE)
                return false;

        pthread_mutex_lock(&screen->bo_cache_lock);
        struct list_head *bucket = pan_bucket(screen, bo->size);
        struct drm_panfrost_madvise madv;

        madv.handle = bo->gem_handle;
        madv.madv = PANFROST_MADV_DONTNEED;
	madv.retained = 0;

        drmIoctl(screen->fd, DRM_IOCTL_PANFROST_MADVISE, &madv);

        /* Add us to the bucket */
        list_addtail(&bo->link, bucket);
        pthread_mutex_unlock(&screen->bo_cache_lock);

        return true;
}

/* Evicts all BOs from the cache. Called during context
 * destroy or during low-memory situations (to free up
 * memory that may be unused by us just sitting in our
 * cache, but still reserved from the perspective of the
 * OS) */

void
panfrost_bo_cache_evict_all(
                struct panfrost_screen *screen)
{
        pthread_mutex_lock(&screen->bo_cache_lock);
        for (unsigned i = 0; i < ARRAY_SIZE(screen->bo_cache); ++i) {
                struct list_head *bucket = &screen->bo_cache[i];

                list_for_each_entry_safe(struct panfrost_bo, entry, bucket, link) {
                        list_del(&entry->link);
                        panfrost_bo_free(entry);
                }
        }
        pthread_mutex_unlock(&screen->bo_cache_lock);
}

void
panfrost_bo_mmap(struct panfrost_bo *bo)
{
        struct drm_panfrost_mmap_bo mmap_bo = { .handle = bo->gem_handle };
        int ret;

        if (bo->cpu)
                return;

        ret = drmIoctl(bo->screen->fd, DRM_IOCTL_PANFROST_MMAP_BO, &mmap_bo);
        if (ret) {
                fprintf(stderr, "DRM_IOCTL_PANFROST_MMAP_BO failed: %m\n");
                assert(0);
        }

        bo->cpu = os_mmap(NULL, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                          bo->screen->fd, mmap_bo.offset);
        if (bo->cpu == MAP_FAILED) {
                fprintf(stderr, "mmap failed: %p %m\n", bo->cpu);
                assert(0);
        }

        /* Record the mmap if we're tracing */
        if (pan_debug & PAN_DBG_TRACE)
                pandecode_inject_mmap(bo->gpu, bo->cpu, bo->size, NULL);
}

static void
panfrost_bo_munmap(struct panfrost_bo *bo)
{
        if (!bo->cpu)
                return;

        if (os_munmap((void *) (uintptr_t)bo->cpu, bo->size)) {
                perror("munmap");
                abort();
        }

        bo->cpu = NULL;
}

struct panfrost_bo *
panfrost_bo_create(struct panfrost_screen *screen, size_t size,
                   uint32_t flags)
{
        struct panfrost_bo *bo;

        /* Kernel will fail (confusingly) with EPERM otherwise */
        assert(size > 0);

        /* To maximize BO cache usage, don't allocate tiny BOs */
        size = MAX2(size, 4096);

        /* GROWABLE BOs cannot be mmapped */
        if (flags & PAN_BO_GROWABLE)
                assert(flags & PAN_BO_INVISIBLE);

        /* Before creating a BO, we first want to check the cache but without
         * waiting for BO readiness (BOs in the cache can still be referenced
         * by jobs that are not finished yet).
         * If the cached allocation fails we fall back on fresh BO allocation,
         * and if that fails too, we try one more time to allocate from the
         * cache, but this time we accept to wait.
         */
        bo = panfrost_bo_cache_fetch(screen, size, flags, true);
        if (!bo)
                bo = panfrost_bo_alloc(screen, size, flags);
        if (!bo)
                bo = panfrost_bo_cache_fetch(screen, size, flags, false);

        if (!bo)
                fprintf(stderr, "BO creation failed\n");

        assert(bo);

        /* Only mmap now if we know we need to. For CPU-invisible buffers, we
         * never map since we don't care about their contents; they're purely
         * for GPU-internal use. But we do trace them anyway. */

        if (!(flags & (PAN_BO_INVISIBLE | PAN_BO_DELAY_MMAP)))
                panfrost_bo_mmap(bo);
        else if (flags & PAN_BO_INVISIBLE) {
                if (pan_debug & PAN_DBG_TRACE)
                        pandecode_inject_mmap(bo->gpu, NULL, bo->size, NULL);
        }

        pipe_reference_init(&bo->reference, 1);

        pthread_mutex_lock(&screen->active_bos_lock);
        _mesa_set_add(bo->screen->active_bos, bo);
        pthread_mutex_unlock(&screen->active_bos_lock);

        return bo;
}

void
panfrost_bo_reference(struct panfrost_bo *bo)
{
        if (bo)
                pipe_reference(NULL, &bo->reference);
}

void
panfrost_bo_unreference(struct panfrost_bo *bo)
{
        if (!bo)
                return;

        if (!pipe_reference(&bo->reference, NULL))
                return;

        struct panfrost_screen *screen = bo->screen;

        pthread_mutex_lock(&screen->active_bos_lock);
        /* Someone might have imported this BO while we were waiting for the
         * lock, let's make sure it's still not referenced before freeing it.
         */
        if (!pipe_is_referenced(&bo->reference)) {
                _mesa_set_remove_key(bo->screen->active_bos, bo);

                /* When the reference count goes to zero, we need to cleanup */
                panfrost_bo_munmap(bo);

                /* Rather than freeing the BO now, we'll cache the BO for later
                 * allocations if we're allowed to.
                 */
                if (!panfrost_bo_cache_put(bo))
                        panfrost_bo_free(bo);
        }
        pthread_mutex_unlock(&screen->active_bos_lock);
}

struct panfrost_bo *
panfrost_bo_import(struct panfrost_screen *screen, int fd)
{
        struct panfrost_bo *bo, *newbo = rzalloc(screen, struct panfrost_bo);
        struct drm_panfrost_get_bo_offset get_bo_offset = {0,};
        struct set_entry *entry;
        ASSERTED int ret;
        unsigned gem_handle;

        newbo->screen = screen;

        ret = drmPrimeFDToHandle(screen->fd, fd, &gem_handle);
        assert(!ret);

        newbo->gem_handle = gem_handle;

        pthread_mutex_lock(&screen->active_bos_lock);
        entry = _mesa_set_search_or_add(screen->active_bos, newbo);
        assert(entry);
        bo = (struct panfrost_bo *)entry->key;
        if (newbo == bo) {
                get_bo_offset.handle = gem_handle;
                ret = drmIoctl(screen->fd, DRM_IOCTL_PANFROST_GET_BO_OFFSET, &get_bo_offset);
                assert(!ret);

                newbo->gpu = (mali_ptr) get_bo_offset.offset;
                newbo->size = lseek(fd, 0, SEEK_END);
                newbo->flags |= PAN_BO_DONT_REUSE | PAN_BO_IMPORTED;
                assert(newbo->size > 0);
                pipe_reference_init(&newbo->reference, 1);
                // TODO map and unmap on demand?
                panfrost_bo_mmap(newbo);
        } else {
                ralloc_free(newbo);
                /* !pipe_is_referenced(&bo->reference) can happen if the BO
                 * was being released but panfrost_bo_import() acquired the
                 * lock before panfrost_bo_unreference(). In that case, refcnt
                 * is 0 and we can't use panfrost_bo_reference() directly, we
                 * have to re-initialize it with pipe_reference_init().
                 * Note that panfrost_bo_unreference() checks
                 * pipe_is_referenced() value just after acquiring the lock to
                 * make sure the object is not freed if panfrost_bo_import()
                 * acquired it in the meantime.
                 */
                if (!pipe_is_referenced(&bo->reference))
                        pipe_reference_init(&newbo->reference, 1);
                else
                        panfrost_bo_reference(bo);
                assert(bo->cpu);
        }
        pthread_mutex_unlock(&screen->active_bos_lock);

        return bo;
}

int
panfrost_bo_export(struct panfrost_bo *bo)
{
        struct drm_prime_handle args = {
                .handle = bo->gem_handle,
                .flags = DRM_CLOEXEC,
        };

        int ret = drmIoctl(bo->screen->fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args);
        if (ret == -1)
                return -1;

        bo->flags |= PAN_BO_DONT_REUSE | PAN_BO_EXPORTED;
        return args.fd;
}

