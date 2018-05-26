/*
 * Copyright © 2014-2017 Broadcom
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <errno.h>
#include <err.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "util/u_hash_table.h"
#include "util/u_memory.h"
#include "util/ralloc.h"

#include "vc5_context.h"
#include "vc5_screen.h"

#ifdef HAVE_VALGRIND
#include <valgrind.h>
#include <memcheck.h>
#define VG(x) x
#else
#define VG(x)
#endif

static bool dump_stats = false;

static void
vc5_bo_cache_free_all(struct vc5_bo_cache *cache);

static void
vc5_bo_dump_stats(struct vc5_screen *screen)
{
        struct vc5_bo_cache *cache = &screen->bo_cache;

        fprintf(stderr, "  BOs allocated:   %d\n", screen->bo_count);
        fprintf(stderr, "  BOs size:        %dkb\n", screen->bo_size / 1024);
        fprintf(stderr, "  BOs cached:      %d\n", cache->bo_count);
        fprintf(stderr, "  BOs cached size: %dkb\n", cache->bo_size / 1024);

        if (!list_empty(&cache->time_list)) {
                struct vc5_bo *first = LIST_ENTRY(struct vc5_bo,
                                                  cache->time_list.next,
                                                  time_list);
                struct vc5_bo *last = LIST_ENTRY(struct vc5_bo,
                                                  cache->time_list.prev,
                                                  time_list);

                fprintf(stderr, "  oldest cache time: %ld\n",
                        (long)first->free_time);
                fprintf(stderr, "  newest cache time: %ld\n",
                        (long)last->free_time);

                struct timespec time;
                clock_gettime(CLOCK_MONOTONIC, &time);
                fprintf(stderr, "  now:               %ld\n",
                        time.tv_sec);
        }
}

static void
vc5_bo_remove_from_cache(struct vc5_bo_cache *cache, struct vc5_bo *bo)
{
        list_del(&bo->time_list);
        list_del(&bo->size_list);
        cache->bo_count--;
        cache->bo_size -= bo->size;
}

static struct vc5_bo *
vc5_bo_from_cache(struct vc5_screen *screen, uint32_t size, const char *name)
{
        struct vc5_bo_cache *cache = &screen->bo_cache;
        uint32_t page_index = size / 4096 - 1;

        if (cache->size_list_size <= page_index)
                return NULL;

        struct vc5_bo *bo = NULL;
        mtx_lock(&cache->lock);
        if (!list_empty(&cache->size_list[page_index])) {
                bo = LIST_ENTRY(struct vc5_bo, cache->size_list[page_index].next,
                                size_list);

                /* Check that the BO has gone idle.  If not, then we want to
                 * allocate something new instead, since we assume that the
                 * user will proceed to CPU map it and fill it with stuff.
                 */
                if (!vc5_bo_wait(bo, 0, NULL)) {
                        mtx_unlock(&cache->lock);
                        return NULL;
                }

                pipe_reference_init(&bo->reference, 1);
                vc5_bo_remove_from_cache(cache, bo);

                bo->name = name;
        }
        mtx_unlock(&cache->lock);
        return bo;
}

struct vc5_bo *
vc5_bo_alloc(struct vc5_screen *screen, uint32_t size, const char *name)
{
        struct vc5_bo *bo;
        int ret;

        size = align(size, 4096);

        bo = vc5_bo_from_cache(screen, size, name);
        if (bo) {
                if (dump_stats) {
                        fprintf(stderr, "Allocated %s %dkb from cache:\n",
                                name, size / 1024);
                        vc5_bo_dump_stats(screen);
                }
                return bo;
        }

        bo = CALLOC_STRUCT(vc5_bo);
        if (!bo)
                return NULL;

        pipe_reference_init(&bo->reference, 1);
        bo->screen = screen;
        bo->size = size;
        bo->name = name;
        bo->private = true;

 retry:
        ;

        bool cleared_and_retried = false;
        struct drm_vc5_create_bo create = {
                .size = size
        };

        ret = vc5_ioctl(screen->fd, DRM_IOCTL_VC5_CREATE_BO, &create);
        bo->handle = create.handle;
        bo->offset = create.offset;

        if (ret != 0) {
                if (!list_empty(&screen->bo_cache.time_list) &&
                    !cleared_and_retried) {
                        cleared_and_retried = true;
                        vc5_bo_cache_free_all(&screen->bo_cache);
                        goto retry;
                }

                free(bo);
                return NULL;
        }

        screen->bo_count++;
        screen->bo_size += bo->size;
        if (dump_stats) {
                fprintf(stderr, "Allocated %s %dkb:\n", name, size / 1024);
                vc5_bo_dump_stats(screen);
        }

        return bo;
}

void
vc5_bo_last_unreference(struct vc5_bo *bo)
{
        struct vc5_screen *screen = bo->screen;

        struct timespec time;
        clock_gettime(CLOCK_MONOTONIC, &time);
        mtx_lock(&screen->bo_cache.lock);
        vc5_bo_last_unreference_locked_timed(bo, time.tv_sec);
        mtx_unlock(&screen->bo_cache.lock);
}

static void
vc5_bo_free(struct vc5_bo *bo)
{
        struct vc5_screen *screen = bo->screen;

        if (bo->map) {
                if (using_vc5_simulator && bo->name &&
                    strcmp(bo->name, "winsys") == 0) {
                        free(bo->map);
                } else {
                        munmap(bo->map, bo->size);
                        VG(VALGRIND_FREELIKE_BLOCK(bo->map, 0));
                }
        }

        struct drm_gem_close c;
        memset(&c, 0, sizeof(c));
        c.handle = bo->handle;
        int ret = vc5_ioctl(screen->fd, DRM_IOCTL_GEM_CLOSE, &c);
        if (ret != 0)
                fprintf(stderr, "close object %d: %s\n", bo->handle, strerror(errno));

        screen->bo_count--;
        screen->bo_size -= bo->size;

        if (dump_stats) {
                fprintf(stderr, "Freed %s%s%dkb:\n",
                        bo->name ? bo->name : "",
                        bo->name ? " " : "",
                        bo->size / 1024);
                vc5_bo_dump_stats(screen);
        }

        free(bo);
}

static void
free_stale_bos(struct vc5_screen *screen, time_t time)
{
        struct vc5_bo_cache *cache = &screen->bo_cache;
        bool freed_any = false;

        list_for_each_entry_safe(struct vc5_bo, bo, &cache->time_list,
                                 time_list) {
                if (dump_stats && !freed_any) {
                        fprintf(stderr, "Freeing stale BOs:\n");
                        vc5_bo_dump_stats(screen);
                        freed_any = true;
                }

                /* If it's more than a second old, free it. */
                if (time - bo->free_time > 2) {
                        vc5_bo_remove_from_cache(cache, bo);
                        vc5_bo_free(bo);
                } else {
                        break;
                }
        }

        if (dump_stats && freed_any) {
                fprintf(stderr, "Freed stale BOs:\n");
                vc5_bo_dump_stats(screen);
        }
}

static void
vc5_bo_cache_free_all(struct vc5_bo_cache *cache)
{
        mtx_lock(&cache->lock);
        list_for_each_entry_safe(struct vc5_bo, bo, &cache->time_list,
                                 time_list) {
                vc5_bo_remove_from_cache(cache, bo);
                vc5_bo_free(bo);
        }
        mtx_unlock(&cache->lock);
}

void
vc5_bo_last_unreference_locked_timed(struct vc5_bo *bo, time_t time)
{
        struct vc5_screen *screen = bo->screen;
        struct vc5_bo_cache *cache = &screen->bo_cache;
        uint32_t page_index = bo->size / 4096 - 1;

        if (!bo->private) {
                vc5_bo_free(bo);
                return;
        }

        if (cache->size_list_size <= page_index) {
                struct list_head *new_list =
                        ralloc_array(screen, struct list_head, page_index + 1);

                /* Move old list contents over (since the array has moved, and
                 * therefore the pointers to the list heads have to change).
                 */
                for (int i = 0; i < cache->size_list_size; i++) {
                        struct list_head *old_head = &cache->size_list[i];
                        if (list_empty(old_head))
                                list_inithead(&new_list[i]);
                        else {
                                new_list[i].next = old_head->next;
                                new_list[i].prev = old_head->prev;
                                new_list[i].next->prev = &new_list[i];
                                new_list[i].prev->next = &new_list[i];
                        }
                }
                for (int i = cache->size_list_size; i < page_index + 1; i++)
                        list_inithead(&new_list[i]);

                cache->size_list = new_list;
                cache->size_list_size = page_index + 1;
        }

        bo->free_time = time;
        list_addtail(&bo->size_list, &cache->size_list[page_index]);
        list_addtail(&bo->time_list, &cache->time_list);
        cache->bo_count++;
        cache->bo_size += bo->size;
        if (dump_stats) {
                fprintf(stderr, "Freed %s %dkb to cache:\n",
                        bo->name, bo->size / 1024);
                vc5_bo_dump_stats(screen);
        }
        bo->name = NULL;

        free_stale_bos(screen, time);
}

static struct vc5_bo *
vc5_bo_open_handle(struct vc5_screen *screen,
                   uint32_t winsys_stride,
                   uint32_t handle, uint32_t size)
{
        struct vc5_bo *bo;

        assert(size);

        mtx_lock(&screen->bo_handles_mutex);

        bo = util_hash_table_get(screen->bo_handles, (void*)(uintptr_t)handle);
        if (bo) {
                pipe_reference(NULL, &bo->reference);
                goto done;
        }

        bo = CALLOC_STRUCT(vc5_bo);
        pipe_reference_init(&bo->reference, 1);
        bo->screen = screen;
        bo->handle = handle;
        bo->size = size;
        bo->name = "winsys";
        bo->private = false;

        struct drm_vc5_get_bo_offset get = {
                .handle = handle,
        };
        int ret = vc5_ioctl(screen->fd, DRM_IOCTL_VC5_GET_BO_OFFSET, &get);
        if (ret) {
                fprintf(stderr, "Failed to get BO offset: %s\n",
                        strerror(errno));
                free(bo);
                return NULL;
        }
        bo->offset = get.offset;

#ifdef USE_VC5_SIMULATOR
        vc5_simulator_open_from_handle(screen->fd, winsys_stride,
                                       bo->handle, bo->size);
        bo->map = malloc(bo->size);
#endif

        util_hash_table_set(screen->bo_handles, (void *)(uintptr_t)handle, bo);

done:
        mtx_unlock(&screen->bo_handles_mutex);
        return bo;
}

struct vc5_bo *
vc5_bo_open_name(struct vc5_screen *screen, uint32_t name,
                 uint32_t winsys_stride)
{
        struct drm_gem_open o = {
                .name = name
        };
        int ret = vc5_ioctl(screen->fd, DRM_IOCTL_GEM_OPEN, &o);
        if (ret) {
                fprintf(stderr, "Failed to open bo %d: %s\n",
                        name, strerror(errno));
                return NULL;
        }

        return vc5_bo_open_handle(screen, winsys_stride, o.handle, o.size);
}

struct vc5_bo *
vc5_bo_open_dmabuf(struct vc5_screen *screen, int fd, uint32_t winsys_stride)
{
        uint32_t handle;
        int ret = drmPrimeFDToHandle(screen->fd, fd, &handle);
        int size;
        if (ret) {
                fprintf(stderr, "Failed to get vc5 handle for dmabuf %d\n", fd);
                return NULL;
        }

        /* Determine the size of the bo we were handed. */
        size = lseek(fd, 0, SEEK_END);
        if (size == -1) {
                fprintf(stderr, "Couldn't get size of dmabuf fd %d.\n", fd);
                return NULL;
        }

        return vc5_bo_open_handle(screen, winsys_stride, handle, size);
}

int
vc5_bo_get_dmabuf(struct vc5_bo *bo)
{
        int fd;
        int ret = drmPrimeHandleToFD(bo->screen->fd, bo->handle,
                                     O_CLOEXEC, &fd);
        if (ret != 0) {
                fprintf(stderr, "Failed to export gem bo %d to dmabuf\n",
                        bo->handle);
                return -1;
        }

        mtx_lock(&bo->screen->bo_handles_mutex);
        bo->private = false;
        util_hash_table_set(bo->screen->bo_handles, (void *)(uintptr_t)bo->handle, bo);
        mtx_unlock(&bo->screen->bo_handles_mutex);

        return fd;
}

bool
vc5_bo_flink(struct vc5_bo *bo, uint32_t *name)
{
        struct drm_gem_flink flink = {
                .handle = bo->handle,
        };
        int ret = vc5_ioctl(bo->screen->fd, DRM_IOCTL_GEM_FLINK, &flink);
        if (ret) {
                fprintf(stderr, "Failed to flink bo %d: %s\n",
                        bo->handle, strerror(errno));
                free(bo);
                return false;
        }

        bo->private = false;
        *name = flink.name;

        return true;
}

static int vc5_wait_bo_ioctl(int fd, uint32_t handle, uint64_t timeout_ns)
{
        struct drm_vc5_wait_bo wait = {
                .handle = handle,
                .timeout_ns = timeout_ns,
        };
        int ret = vc5_ioctl(fd, DRM_IOCTL_VC5_WAIT_BO, &wait);
        if (ret == -1)
                return -errno;
        else
                return 0;

}

bool
vc5_bo_wait(struct vc5_bo *bo, uint64_t timeout_ns, const char *reason)
{
        struct vc5_screen *screen = bo->screen;

        if (unlikely(V3D_DEBUG & V3D_DEBUG_PERF) && timeout_ns && reason) {
                if (vc5_wait_bo_ioctl(screen->fd, bo->handle, 0) == -ETIME) {
                        fprintf(stderr, "Blocking on %s BO for %s\n",
                                bo->name, reason);
                }
        }

        int ret = vc5_wait_bo_ioctl(screen->fd, bo->handle, timeout_ns);
        if (ret) {
                if (ret != -ETIME) {
                        fprintf(stderr, "wait failed: %d\n", ret);
                        abort();
                }

                return false;
        }

        return true;
}

void *
vc5_bo_map_unsynchronized(struct vc5_bo *bo)
{
        uint64_t offset;
        int ret;

        if (bo->map)
                return bo->map;

        struct drm_vc5_mmap_bo map;
        memset(&map, 0, sizeof(map));
        map.handle = bo->handle;
        ret = vc5_ioctl(bo->screen->fd, DRM_IOCTL_VC5_MMAP_BO, &map);
        offset = map.offset;
        if (ret != 0) {
                fprintf(stderr, "map ioctl failure\n");
                abort();
        }

        bo->map = mmap(NULL, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                       bo->screen->fd, offset);
        if (bo->map == MAP_FAILED) {
                fprintf(stderr, "mmap of bo %d (offset 0x%016llx, size %d) failed\n",
                        bo->handle, (long long)offset, bo->size);
                abort();
        }
        VG(VALGRIND_MALLOCLIKE_BLOCK(bo->map, bo->size, 0, false));

        return bo->map;
}

void *
vc5_bo_map(struct vc5_bo *bo)
{
        void *map = vc5_bo_map_unsynchronized(bo);

        bool ok = vc5_bo_wait(bo, PIPE_TIMEOUT_INFINITE, "bo map");
        if (!ok) {
                fprintf(stderr, "BO wait for map failed\n");
                abort();
        }

        return map;
}

void
vc5_bufmgr_destroy(struct pipe_screen *pscreen)
{
        struct vc5_screen *screen = vc5_screen(pscreen);
        struct vc5_bo_cache *cache = &screen->bo_cache;

        vc5_bo_cache_free_all(cache);

        if (dump_stats) {
                fprintf(stderr, "BO stats after screen destroy:\n");
                vc5_bo_dump_stats(screen);
        }
}
