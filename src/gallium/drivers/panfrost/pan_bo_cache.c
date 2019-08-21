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
#include <xf86drm.h>
#include "drm-uapi/panfrost_drm.h"

#include "pan_screen.h"
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

struct panfrost_bo *
panfrost_bo_cache_fetch(
                struct panfrost_screen *screen,
                size_t size, uint32_t flags)
{
        struct list_head *bucket = pan_bucket(screen, size);

        /* Iterate the bucket looking for something suitable */
        list_for_each_entry_safe(struct panfrost_bo, entry, bucket, link) {
                if (entry->size >= size &&
                    entry->flags == flags) {
                        int ret;
                        struct drm_panfrost_madvise madv;

                        /* This one works, splice it out of the cache */
                        list_del(&entry->link);

                        madv.handle = entry->gem_handle;
                        madv.madv = PANFROST_MADV_WILLNEED;
                        madv.retained = 0;

                        ret = drmIoctl(screen->fd, DRM_IOCTL_PANFROST_MADVISE, &madv);
                        if (!ret && !madv.retained) {
                                panfrost_drm_release_bo(screen, entry, false);
                                continue;
                        }
                        /* Let's go! */
                        return entry;
                }
        }

        /* We didn't find anything */
        return NULL;
}

/* Tries to add a BO to the cache. Returns if it was
 * successful */

bool
panfrost_bo_cache_put(
                struct panfrost_screen *screen,
                struct panfrost_bo *bo)
{
        struct list_head *bucket = pan_bucket(screen, bo->size);
        struct drm_panfrost_madvise madv;

        madv.handle = bo->gem_handle;
        madv.madv = PANFROST_MADV_DONTNEED;
	madv.retained = 0;

        drmIoctl(screen->fd, DRM_IOCTL_PANFROST_MADVISE, &madv);

        /* Add us to the bucket */
        list_addtail(&bo->link, bucket);

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
        for (unsigned i = 0; i < ARRAY_SIZE(screen->bo_cache); ++i) {
                struct list_head *bucket = &screen->bo_cache[i];

                list_for_each_entry_safe(struct panfrost_bo, entry, bucket, link) {
                        list_del(&entry->link);
                        panfrost_drm_release_bo(screen, entry, false);
                }
        }

        return;
}

