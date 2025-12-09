/*
 * Copyright © 2021 Intel Corporation
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

#include "vk_sync_timeline.h"

#include <inttypes.h>

#include "util/os_time.h"
#include "util/timespec.h"

#include "vk_alloc.h"
#include "vk_device.h"
#include "vk_log.h"

static struct vk_sync_timeline *
to_vk_sync_timeline(struct vk_sync *sync)
{
   assert(sync->type->init == vk_sync_timeline_init);

   return container_of(sync, struct vk_sync_timeline, sync);
}

static struct vk_sync_timeline_state *
to_vk_sync_timeline_state(struct vk_sync *sync)
{
   struct vk_sync_timeline *timeline = to_vk_sync_timeline(sync);
   return timeline->state;
}

static void
vk_sync_timeline_type_validate(const struct vk_sync_timeline_type *ttype)
{
   ASSERTED const enum vk_sync_features req_features =
      VK_SYNC_FEATURE_BINARY |
      VK_SYNC_FEATURE_GPU_WAIT |
      VK_SYNC_FEATURE_GPU_MULTI_WAIT |
      VK_SYNC_FEATURE_CPU_WAIT |
      VK_SYNC_FEATURE_CPU_RESET;

   assert(!(req_features & ~ttype->point_sync_type->features));
}

static void
vk_sync_timeline_state_ref(struct vk_sync_timeline_state *state)
{
   p_atomic_inc(&state->refcount);
}

static void
vk_sync_timeline_state_unref(struct vk_device *device,
                             struct vk_sync_timeline_state *state)
{
   if (p_atomic_dec_return(&state->refcount))
      return;

   list_for_each_entry_safe(struct vk_sync_timeline_point, point,
                            &state->free_points, link) {
      list_del(&point->link);
      vk_sync_finish(device, &point->sync);
      vk_free(&device->alloc, point);
   }

   list_for_each_entry_safe(struct vk_sync_timeline_point, point,
                            &state->pending_points, link) {
      list_del(&point->link);
      vk_sync_finish(device, &point->sync);
      vk_free(&device->alloc, point);
   }

   u_cnd_monotonic_destroy(&state->cond);
   mtx_destroy(&state->mutex);
   vk_free(&device->alloc, state);
}

VkResult
vk_sync_timeline_init(struct vk_device *device,
                      struct vk_sync *sync,
                      uint64_t initial_value)
{
   struct vk_sync_timeline *timeline = to_vk_sync_timeline(sync);
   struct vk_sync_timeline_state *state;
   int ret;

   ASSERTED const struct vk_sync_timeline_type *ttype =
      container_of(timeline->sync.type, struct vk_sync_timeline_type, sync);
   vk_sync_timeline_type_validate(ttype);

   state = vk_zalloc(&device->alloc, sizeof(*state), 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!state)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   ret = mtx_init(&state->mutex, mtx_plain);
   if (ret != thrd_success) {
      vk_free(&device->alloc, state);
      return vk_errorf(device, VK_ERROR_UNKNOWN, "mtx_init failed");
   }

   ret = u_cnd_monotonic_init(&state->cond);
   if (ret != thrd_success) {
      mtx_destroy(&state->mutex);
      vk_free(&device->alloc, state);
      return vk_errorf(device, VK_ERROR_UNKNOWN, "cnd_init failed");
   }

   state->highest_past = state->highest_pending = initial_value;
   list_inithead(&state->pending_points);
   list_inithead(&state->free_points);

   p_atomic_set(&state->refcount, 1);

   timeline->state = state;

   return VK_SUCCESS;
}

static VkResult
vk_sync_timeline_gc_locked(struct vk_device *device,
                           struct vk_sync_timeline_state *state,
                           bool drain);

static void
vk_sync_timeline_finish(struct vk_device *device,
                        struct vk_sync *sync)
{
   struct vk_sync_timeline_state *state = to_vk_sync_timeline_state(sync);

   /* We need to garbage collect to get rid of any pending points so that the
    * vk_sync_timeline_state_unref() at the end drops the final reference
    * held by the vk_sync_timeline. It's up to the client to ensure that
    * there are no vk_sync in-flight when this is called so this should get
    * rid of all pending time points. The only time point references left are
    * those held by waits or vk_queue_submit().
    */
   mtx_lock(&state->mutex);
   vk_sync_timeline_gc_locked(device, state, true);
   assert(list_is_empty(&state->pending_points));
   mtx_unlock(&state->mutex);

   vk_sync_timeline_state_unref(device, state);
}

static struct vk_sync_timeline_point *
vk_sync_timeline_first_point(struct vk_sync_timeline_state *state)
{
   struct vk_sync_timeline_point *point =
      list_first_entry(&state->pending_points,
                       struct vk_sync_timeline_point, link);

   assert(point->value <= state->highest_pending);
   assert(point->value > state->highest_past);

   return point;
}

static VkResult
vk_sync_timeline_alloc_point_locked(struct vk_device *device,
                                    struct vk_sync_timeline *timeline,
                                    uint64_t value,
                                    struct vk_sync_timeline_point **point_out)
{
   struct vk_sync_timeline_state *state = timeline->state;
   struct vk_sync_timeline_point *point;
   VkResult result;

   result = vk_sync_timeline_gc_locked(device, state, false);
   if (unlikely(result != VK_SUCCESS))
      return result;

   if (list_is_empty(&state->free_points)) {
      const struct vk_sync_timeline_type *ttype =
         container_of(timeline->sync.type, struct vk_sync_timeline_type, sync);
      const struct vk_sync_type *point_sync_type = ttype->point_sync_type;

      size_t size = offsetof(struct vk_sync_timeline_point, sync) +
                    point_sync_type->size;

      point = vk_zalloc(&device->alloc, size, 8,
                        VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
      if (!point)
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

      point->timeline_state = state;

      result = vk_sync_init(device, &point->sync, point_sync_type,
                            0 /* flags */, 0 /* initial_value */);
      if (unlikely(result != VK_SUCCESS)) {
         vk_free(&device->alloc, point);
         return result;
      }
   } else {
      point = list_first_entry(&state->free_points,
                               struct vk_sync_timeline_point, link);

      if (point->sync.type->reset) {
         result = vk_sync_reset(device, &point->sync);
         if (unlikely(result != VK_SUCCESS))
            return result;
      }

      list_del(&point->link);
   }

   point->value = value;

   assert(point->refcount == 0);
   point->refcount++;

   *point_out = point;

   vk_sync_timeline_state_ref(state);

   return VK_SUCCESS;
}

VkResult
vk_sync_timeline_alloc_point(struct vk_device *device,
                             struct vk_sync_timeline *timeline,
                             uint64_t value,
                             struct vk_sync_timeline_point **point_out)
{
   VkResult result;

   mtx_lock(&timeline->state->mutex);
   result = vk_sync_timeline_alloc_point_locked(device, timeline, value, point_out);
   mtx_unlock(&timeline->state->mutex);

   return result;
}

static void
vk_sync_timeline_ref_point_locked(struct vk_sync_timeline_point *point)
{
   point->refcount++;
}

/* Returns true if this was the last reference to point.
 *
 * DO NOT call this helper directly. You should call vk_sync_timeline_unref_point_locked()
 * or vk_sync_timeline_point_unref() instead.
 */
static bool
vk_sync_timeline_unref_point_no_unref_state_locked(struct vk_sync_timeline_point *point)
{
   struct vk_sync_timeline_state *state = point->timeline_state;

   assert(point->refcount > 0);
   point->refcount--;

   if (point->refcount > 0)
      return false;

   assert(point->refcount == 0);

   /* The pending list also takes a reference so this can't be pending */
   assert(!point->pending);
   list_add(&point->link, &state->free_points);

   return true;
}

static void
vk_sync_timeline_unref_point_locked(struct vk_device *device,
                                    struct vk_sync_timeline_state *state,
                                    struct vk_sync_timeline_point *point)
{
   /* The caller needs to have its own reference to the state, not just the
    * one implicit in point, because it's also holding the lock.
    */
   assert(p_atomic_read(&state->refcount) > 1);

   if (vk_sync_timeline_unref_point_no_unref_state_locked(point))
      vk_sync_timeline_state_unref(device, state);
}

void
vk_sync_timeline_point_unref(struct vk_device *device,
                             struct vk_sync_timeline_point *point)
{
   struct vk_sync_timeline_state *state = point->timeline_state;

   mtx_lock(&state->mutex);
   const bool last_ref =
      vk_sync_timeline_unref_point_no_unref_state_locked(point);
   mtx_unlock(&state->mutex);

   /* Drop the state reference outside the mutex so we don't free the state
    * and then try to unlock the mutex.
    */
   if (last_ref)
      vk_sync_timeline_state_unref(device, state);
}

static void
vk_sync_timeline_complete_point_locked(struct vk_device *device,
                                       struct vk_sync_timeline_state *state,
                                       struct vk_sync_timeline_point *point)
{
   if (!point->pending)
      return;

   assert(state->highest_past < point->value);
   state->highest_past = point->value;

   point->pending = false;
   list_del(&point->link);

   /* Drop the pending reference */
   vk_sync_timeline_unref_point_locked(device, state, point);
}

static VkResult
vk_sync_timeline_gc_locked(struct vk_device *device,
                           struct vk_sync_timeline_state *state,
                           bool drain)
{
   list_for_each_entry_safe(struct vk_sync_timeline_point, point,
                            &state->pending_points, link) {
      /* state->highest_pending is only incremented once submission has
       * happened. If this point has a greater serial, it means the point
       * hasn't been submitted yet.
       */
      if (point->value > state->highest_pending)
         return VK_SUCCESS;

      /* If someone is waiting on this time point, consider it busy and don't
       * try to recycle it. There's a slim possibility that it's no longer
       * busy by the time we look at it but we would be recycling it out from
       * under a waiter and that can lead to weird races.
       *
       * We walk the list in-order so if this time point is still busy so is
       * every following time point
       */
      assert(point->refcount > 0);
      if (point->refcount > 1 && !drain)
         return VK_SUCCESS;

      /* Garbage collect any signaled point. */
      VkResult result = vk_sync_wait(device, &point->sync, 0,
                                     VK_SYNC_WAIT_COMPLETE,
                                     0 /* abs_timeout_ns */);
      if (result == VK_TIMEOUT) {
         /* We walk the list in-order so if this time point is still busy so
          * is every following time point
          */
         return VK_SUCCESS;
      } else if (result != VK_SUCCESS) {
         return result;
      }

      vk_sync_timeline_complete_point_locked(device, state, point);
   }

   return VK_SUCCESS;
}

VkResult
vk_sync_timeline_point_install(struct vk_device *device,
                               struct vk_sync_timeline_point *point)
{
   struct vk_sync_timeline_state *state = point->timeline_state;

   mtx_lock(&state->mutex);

   assert(point->value > state->highest_pending);
   state->highest_pending = point->value;

   /* Adding to the pending list implicitly takes a reference but also this
    * function is documented to consume the reference to point so we don't
    * need to do anything to the reference count here.
    */
   point->pending = true;
   list_addtail(&point->link, &state->pending_points);

   int ret = u_cnd_monotonic_broadcast(&state->cond);

   mtx_unlock(&state->mutex);

   if (ret == thrd_error)
      return vk_errorf(device, VK_ERROR_UNKNOWN, "cnd_broadcast failed");

   return VK_SUCCESS;
}

static VkResult
vk_sync_timeline_get_point_locked(struct vk_device *device,
                                  struct vk_sync_timeline_state *state,
                                  uint64_t wait_value,
                                  struct vk_sync_timeline_point **point_out)
{
   if (state->highest_past >= wait_value) {
      /* Nothing to wait on */
      *point_out = NULL;
      return VK_SUCCESS;
   }

   list_for_each_entry(struct vk_sync_timeline_point, point,
                       &state->pending_points, link) {
      if (point->value >= wait_value) {
         vk_sync_timeline_ref_point_locked(point);
         *point_out = point;
         return VK_SUCCESS;
      }
   }

   return VK_NOT_READY;
}

VkResult
vk_sync_timeline_get_point(struct vk_device *device,
                           struct vk_sync_timeline *timeline,
                           uint64_t wait_value,
                           struct vk_sync_timeline_point **point_out)
{
   struct vk_sync_timeline_state *state = timeline->state;

   mtx_lock(&state->mutex);
   VkResult result = vk_sync_timeline_get_point_locked(device, state,
                                                       wait_value, point_out);
   mtx_unlock(&state->mutex);

   return result;
}

static VkResult
vk_sync_timeline_signal_locked(struct vk_device *device,
                               struct vk_sync_timeline_state *state,
                               uint64_t value)
{
   VkResult result = vk_sync_timeline_gc_locked(device, state, true);
   if (unlikely(result != VK_SUCCESS))
      return result;

   if (unlikely(value <= state->highest_past)) {
      return vk_device_set_lost(device, "Timeline values must only ever "
                                        "strictly increase.");
   }

   assert(list_is_empty(&state->pending_points));
   assert(state->highest_pending == state->highest_past);
   state->highest_pending = state->highest_past = value;

   int ret = u_cnd_monotonic_broadcast(&state->cond);
   if (ret == thrd_error)
      return vk_errorf(device, VK_ERROR_UNKNOWN, "cnd_broadcast failed");

   return VK_SUCCESS;
}

static VkResult
vk_sync_timeline_signal(struct vk_device *device,
                        struct vk_sync *sync,
                        uint64_t value)
{
   struct vk_sync_timeline_state *state = to_vk_sync_timeline_state(sync);

   mtx_lock(&state->mutex);
   VkResult result = vk_sync_timeline_signal_locked(device, state, value);
   mtx_unlock(&state->mutex);

   return result;
}

static VkResult
vk_sync_timeline_get_value(struct vk_device *device,
                           struct vk_sync *sync,
                           uint64_t *value)
{
   struct vk_sync_timeline_state *state = to_vk_sync_timeline_state(sync);

   mtx_lock(&state->mutex);
   VkResult result = vk_sync_timeline_gc_locked(device, state, true);
   mtx_unlock(&state->mutex);

   if (result != VK_SUCCESS)
      return result;

   *value = state->highest_past;

   return VK_SUCCESS;
}

static VkResult
vk_sync_timeline_wait_locked(struct vk_device *device,
                             struct vk_sync_timeline_state *state,
                             uint64_t wait_value,
                             enum vk_sync_wait_flags wait_flags,
                             uint64_t abs_timeout_ns)
{
   struct timespec abs_timeout_ts;
   timespec_from_nsec(&abs_timeout_ts, abs_timeout_ns);

   /* Wait on the queue_submit condition variable until the timeline has a
    * time point pending that's at least as high as wait_value.
    */
   while (state->highest_pending < wait_value) {
      int ret = u_cnd_monotonic_timedwait(&state->cond, &state->mutex,
                                          &abs_timeout_ts);
      if (ret == thrd_timedout)
         return VK_TIMEOUT;

      if (ret != thrd_success)
         return vk_errorf(device, VK_ERROR_UNKNOWN, "cnd_timedwait failed");
   }

   if (wait_flags & VK_SYNC_WAIT_PENDING)
      return VK_SUCCESS;

   VkResult result = vk_sync_timeline_gc_locked(device, state, false);
   if (result != VK_SUCCESS)
      return result;

   while (state->highest_past < wait_value) {
      struct vk_sync_timeline_point *point = vk_sync_timeline_first_point(state);

      /* Drop the lock while we wait. */
      vk_sync_timeline_ref_point_locked(point);
      mtx_unlock(&state->mutex);

      result = vk_sync_wait(device, &point->sync, 0,
                            VK_SYNC_WAIT_COMPLETE,
                            abs_timeout_ns);

      /* Pick the mutex back up */
      mtx_lock(&state->mutex);
      vk_sync_timeline_unref_point_locked(device, state, point);

      /* This covers both VK_TIMEOUT and VK_ERROR_DEVICE_LOST */
      if (result != VK_SUCCESS)
         return result;

      vk_sync_timeline_complete_point_locked(device, state, point);
   }

   return VK_SUCCESS;
}

static VkResult
vk_sync_timeline_wait(struct vk_device *device,
                      struct vk_sync *sync,
                      uint64_t wait_value,
                      enum vk_sync_wait_flags wait_flags,
                      uint64_t abs_timeout_ns)
{
   struct vk_sync_timeline_state *state = to_vk_sync_timeline_state(sync);
   if (wait_value == 0)
      return VK_SUCCESS;

   mtx_lock(&state->mutex);
   VkResult result = vk_sync_timeline_wait_locked(device, state,
                                                  wait_value, wait_flags,
                                                  abs_timeout_ns);
   mtx_unlock(&state->mutex);

   return result;
}

struct vk_sync_timeline_type
vk_sync_timeline_get_type(const struct vk_sync_type *point_sync_type)
{
   return (struct vk_sync_timeline_type) {
      .sync = {
         .size = sizeof(struct vk_sync_timeline),
         .features = VK_SYNC_FEATURE_TIMELINE |
                     VK_SYNC_FEATURE_GPU_WAIT |
                     VK_SYNC_FEATURE_CPU_WAIT |
                     VK_SYNC_FEATURE_CPU_SIGNAL |
                     VK_SYNC_FEATURE_WAIT_ANY |
                     VK_SYNC_FEATURE_WAIT_PENDING,
         .init = vk_sync_timeline_init,
         .finish = vk_sync_timeline_finish,
         .signal = vk_sync_timeline_signal,
         .get_value = vk_sync_timeline_get_value,
         .wait = vk_sync_timeline_wait,
      },
      .point_sync_type = point_sync_type,
   };
}
