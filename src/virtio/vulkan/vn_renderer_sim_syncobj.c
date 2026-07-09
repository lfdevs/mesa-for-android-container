/*
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "vn_renderer_sim_syncobj.h"

#include <errno.h>
#include <poll.h>
#include <unistd.h>

#include "drm-uapi/drm.h"
#include "util/hash_table.h"
#include "util/os_file.h"
#include "util/u_idalloc.h"
#include "util/u_sync_provider.h"

struct sim_sync_provider {
   struct util_sync_provider base;

   mtx_t mutex;
   struct hash_table *syncobjs;
   struct util_idalloc ida;
};

struct sim_syncobj {
   mtx_t mutex;
   int pending_fd;
   bool signaled;
};

static int
sim_syncobj_create(struct util_sync_provider *p,
                   uint32_t flags,
                   uint32_t *handle)
{
   struct sim_sync_provider *sim = (struct sim_sync_provider *)p;

   struct sim_syncobj *syncobj = calloc(1, sizeof(*syncobj));
   if (!syncobj)
      return -ENOMEM;

   mtx_init(&syncobj->mutex, mtx_plain);
   syncobj->pending_fd = -1;
   syncobj->signaled = !!(flags & DRM_SYNCOBJ_CREATE_SIGNALED);

   mtx_lock(&sim->mutex);
   *handle = util_idalloc_alloc(&sim->ida) + 1;
   _mesa_hash_table_insert(sim->syncobjs, (const void *)(uintptr_t)(*handle),
                           syncobj);
   mtx_unlock(&sim->mutex);

   return 0;
}

static int
sim_syncobj_destroy(struct util_sync_provider *p, uint32_t handle)
{
   struct sim_sync_provider *sim = (struct sim_sync_provider *)p;

   struct sim_syncobj *syncobj = NULL;

   mtx_lock(&sim->mutex);
   struct hash_entry *entry =
      _mesa_hash_table_search(sim->syncobjs, (const void *)(uintptr_t)handle);
   if (entry) {
      syncobj = entry->data;
      _mesa_hash_table_remove(sim->syncobjs, entry);
      util_idalloc_free(&sim->ida, handle - 1);
   }
   mtx_unlock(&sim->mutex);

   if (syncobj) {
      if (syncobj->pending_fd >= 0)
         close(syncobj->pending_fd);
      mtx_destroy(&syncobj->mutex);
      free(syncobj);
   }

   return 0;
}

static bool
sim_syncobj_poll(int fd, int poll_timeout)
{
   struct pollfd pollfd = {
      .fd = fd,
      .events = POLLIN,
   };
   int ret;
   do {
      ret = poll(&pollfd, 1, poll_timeout);
   } while (ret == -1 && (errno == EINTR || errno == EAGAIN));

   return ret > 0 && (pollfd.revents & POLLIN);
}

static void
sim_syncobj_update_locked(struct sim_syncobj *syncobj, int poll_timeout)
{
   if (syncobj->pending_fd >= 0) {
      if (sim_syncobj_poll(syncobj->pending_fd, poll_timeout)) {
         close(syncobj->pending_fd);
         syncobj->pending_fd = -1;
         syncobj->signaled = true;
      }
   }
}

static struct sim_syncobj *
sim_syncobj_lookup(struct sim_sync_provider *sim, uint32_t handle)
{
   struct sim_syncobj *syncobj = NULL;

   mtx_lock(&sim->mutex);
   struct hash_entry *entry =
      _mesa_hash_table_search(sim->syncobjs, (const void *)(uintptr_t)handle);
   if (entry)
      syncobj = entry->data;
   mtx_unlock(&sim->mutex);

   return syncobj;
}

static int
sim_syncobj_reset(struct util_sync_provider *p,
                  const uint32_t *handles,
                  uint32_t num_handles)
{
   struct sim_sync_provider *sim = (struct sim_sync_provider *)p;

   for (uint32_t i = 0; i < num_handles; i++) {
      struct sim_syncobj *syncobj = sim_syncobj_lookup(sim, handles[i]);
      if (!syncobj)
         return -1;

      mtx_lock(&syncobj->mutex);
      syncobj->signaled = false;

      if (syncobj->pending_fd >= 0) {
         close(syncobj->pending_fd);
         syncobj->pending_fd = -1;
      }
      mtx_unlock(&syncobj->mutex);
   }

   return 0;
}

static int
sim_syncobj_query(struct util_sync_provider *p,
                  uint32_t *handles,
                  uint64_t *points,
                  uint32_t num_handles,
                  uint32_t flags)
{
   struct sim_sync_provider *sim = (struct sim_sync_provider *)p;

   for (uint32_t i = 0; i < num_handles; i++) {
      struct sim_syncobj *syncobj = sim_syncobj_lookup(sim, handles[i]);
      if (!syncobj)
         return -1;

      mtx_lock(&syncobj->mutex);
      sim_syncobj_update_locked(syncobj, 0);
      points[i] = syncobj->signaled;
      mtx_unlock(&syncobj->mutex);
   }

   return 0;
}

static int
timeout_to_poll_timeout(int64_t timeout)
{
   const int64_t ns_per_ms = 1000000;
   const int64_t ms = (timeout + ns_per_ms - 1) / ns_per_ms;
   if (!ms && timeout)
      return -1;
   return ms <= INT_MAX ? ms : -1;
}

static int
sim_syncobj_wait(struct util_sync_provider *p,
                 uint32_t *handles,
                 unsigned num_handles,
                 int64_t timeout_nsec,
                 unsigned flags,
                 uint32_t *first_signaled)
{
   struct sim_sync_provider *sim = (struct sim_sync_provider *)p;

   const int poll_timeout = timeout_to_poll_timeout(timeout_nsec);
   const bool wait_all = !!(flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL);

   /* TODO poll all fds at the same time */
   for (uint32_t i = 0; i < num_handles; i++) {
      struct sim_syncobj *syncobj = sim_syncobj_lookup(sim, handles[i]);
      if (!syncobj)
         return -1;

      mtx_lock(&syncobj->mutex);
      if (!syncobj->signaled)
         sim_syncobj_update_locked(syncobj, poll_timeout);

      if (!syncobj->signaled) {
         if (!wait_all && i < num_handles - 1 && syncobj->pending_fd < 0) {
            mtx_unlock(&syncobj->mutex);
            continue;
         }
         mtx_unlock(&syncobj->mutex);
         return -ETIME;
      }
      mtx_unlock(&syncobj->mutex);

      if (!wait_all) {
         if (first_signaled)
            *first_signaled = i;
         break;
      }

      /* TODO adjust poll_timeout */
   }

   return 0;
}

static int
sim_syncobj_export_sync_file(struct util_sync_provider *p,
                             uint32_t handle,
                             int *out_sync_file_fd)
{
   struct sim_sync_provider *sim = (struct sim_sync_provider *)p;

   struct sim_syncobj *syncobj = sim_syncobj_lookup(sim, handle);
   if (!syncobj)
      return -1;

   int fd = -1;
   mtx_lock(&syncobj->mutex);
   if (syncobj->pending_fd >= 0)
      fd = os_dupfd_cloexec(syncobj->pending_fd);
   mtx_unlock(&syncobj->mutex);

   *out_sync_file_fd = fd;

   return 0;
}

static int
sim_syncobj_import_sync_file(struct util_sync_provider *p,
                             uint32_t handle,
                             int sync_file_fd)
{
   struct sim_sync_provider *sim = (struct sim_sync_provider *)p;

   struct sim_syncobj *syncobj = sim_syncobj_lookup(sim, handle);
   if (!syncobj)
      return 0;

   int pending_fd = os_dupfd_cloexec(sync_file_fd);
   if (pending_fd < 0)
      return -errno;

   mtx_lock(&syncobj->mutex);
   if (syncobj->pending_fd >= 0)
      close(syncobj->pending_fd);
   syncobj->pending_fd = pending_fd;
   syncobj->signaled = false;
   mtx_unlock(&syncobj->mutex);

   return 0;
}

static void
sim_syncobj_finalize(struct util_sync_provider *p)
{
   free(p);
}

struct util_sync_provider *
vn_renderer_sim_syncobj_get_sync(void)
{
   struct sim_sync_provider *s = calloc(1, sizeof(*s));
   if (!s)
      return NULL;

   s->syncobjs = _mesa_pointer_hash_table_create(NULL);
   if (!s->syncobjs) {
      free(s);
      return NULL;
   }

   mtx_init(&s->mutex, mtx_plain);
   util_idalloc_init(&s->ida, 32);

   s->base = (struct util_sync_provider){
      .create = sim_syncobj_create,
      .destroy = sim_syncobj_destroy,
      .handle_to_fd = NULL,
      .fd_to_handle = NULL,
      .import_sync_file = sim_syncobj_import_sync_file,
      .export_sync_file = sim_syncobj_export_sync_file,
      .wait = sim_syncobj_wait,
      .reset = sim_syncobj_reset,
      .signal = NULL,
      .timeline_signal = NULL,
      .timeline_wait = NULL,
      .query = sim_syncobj_query,
      .transfer = NULL,
      .finalize = sim_syncobj_finalize,
      .clone = NULL,
   };

   return &s->base;
}
