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

#include "vk_drm_syncobj.h"

#include <sched.h>
#include <xf86drm.h>

#include "drm-uapi/drm.h"

#include "util/libsync.h"
#include "util/os_time.h"
#include "util/u_sync_provider.h"

#include "vk_device.h"
#include "vk_log.h"
#include "vk_physical_device.h"
#include "vk_util.h"

static struct vk_drm_syncobj *
to_drm_syncobj(struct vk_sync *sync)
{
   assert(vk_sync_type_is_drm_syncobj(sync->type));
   return container_of(sync, struct vk_drm_syncobj, base);
}

static VkResult
vk_drm_syncobj_init(struct vk_device *device,
                    struct vk_sync *sync,
                    uint64_t initial_value)
{
   struct vk_drm_syncobj *sobj = to_drm_syncobj(sync);

   uint32_t flags = 0;
   if (!(sync->flags & VK_SYNC_IS_TIMELINE) && initial_value)
      flags |= DRM_SYNCOBJ_CREATE_SIGNALED;

   int err = device->sync->create(device->sync, flags, &sobj->syncobj);
   if (err < 0) {
      return vk_errorf(device, VK_ERROR_OUT_OF_HOST_MEMORY,
                       "DRM_IOCTL_SYNCOBJ_CREATE failed: %m");
   }

   if ((sync->flags & VK_SYNC_IS_TIMELINE) && initial_value) {
      err = device->sync->timeline_signal(device->sync, &sobj->syncobj,
                                          &initial_value, 1);
      if (err < 0) {
         vk_drm_syncobj_finish(device, sync);
         return vk_errorf(device, VK_ERROR_OUT_OF_HOST_MEMORY,
                          "DRM_IOCTL_SYNCOBJ_CREATE failed: %m");
      }
   }

   return VK_SUCCESS;
}

void
vk_drm_syncobj_finish(struct vk_device *device,
                      struct vk_sync *sync)
{
   struct vk_drm_syncobj *sobj = to_drm_syncobj(sync);

   ASSERTED int err = device->sync->destroy(device->sync, sobj->syncobj);
   assert(err == 0);
}

static VkResult
vk_drm_syncobj_signal(struct vk_device *device,
                      struct vk_sync *sync,
                      uint64_t value)
{
   struct vk_drm_syncobj *sobj = to_drm_syncobj(sync);

   int err;
   if (sync->flags & VK_SYNC_IS_TIMELINE)
      err = device->sync->timeline_signal(device->sync, &sobj->syncobj, &value, 1);
   else
      err = device->sync->signal(device->sync, &sobj->syncobj, 1);
   if (err) {
      return vk_errorf(device, VK_ERROR_UNKNOWN,
                       "DRM_IOCTL_SYNCOBJ_SIGNAL failed: %m");
   }

   return VK_SUCCESS;
}

static VkResult
vk_drm_syncobj_signal_many(struct vk_device *device,
                           uint32_t signal_count,
                           const struct vk_sync_signal *signals)
{
   if (signal_count == 0)
      return VK_SUCCESS;

   STACK_ARRAY(uint32_t, timeline_handles, signal_count);
   STACK_ARRAY(uint32_t, binary_handles, signal_count);
   STACK_ARRAY(uint64_t, timeline_values, signal_count);
   uint32_t timeline_count = 0, binary_count = 0;

   for (uint32_t i = 0; i < signal_count; i++) {
      struct vk_drm_syncobj *signal_sobj = to_drm_syncobj(signals[i].sync);

      if (signal_sobj->base.flags & VK_SYNC_IS_TIMELINE) {
         timeline_handles[timeline_count] = signal_sobj->syncobj;
         timeline_values[timeline_count] = signals[i].signal_value;
         timeline_count++;
      } else {
         binary_handles[binary_count] = signal_sobj->syncobj;
         binary_count++;
      }
   }

   int err = 0;
   if (timeline_count > 0) {
      err =  device->sync->timeline_signal(device->sync, timeline_handles,
                                           timeline_values, timeline_count);
   }
   if (binary_count > 0) {
      err |= device->sync->signal(device->sync, binary_handles,
                                  binary_count);
   }

   STACK_ARRAY_FINISH(timeline_handles);
   STACK_ARRAY_FINISH(binary_handles);
   STACK_ARRAY_FINISH(timeline_values);

   if (err) {
      return vk_errorf(device, VK_ERROR_UNKNOWN,
                       "DRM_IOCTL_SYNCOBJ_SIGNAL failed: %m");
   }

   return VK_SUCCESS;
}

static VkResult
vk_drm_syncobj_get_value(struct vk_device *device,
                         struct vk_sync *sync,
                         uint64_t *value)
{
   struct vk_drm_syncobj *sobj = to_drm_syncobj(sync);

   int err = device->sync->query(device->sync, &sobj->syncobj, value, 1, 0);
   if (err) {
      return vk_errorf(device, VK_ERROR_UNKNOWN,
                       "DRM_IOCTL_SYNCOBJ_QUERY failed: %m");
   }

   return VK_SUCCESS;
}

static VkResult
vk_drm_syncobj_reset(struct vk_device *device,
                     struct vk_sync *sync)
{
   struct vk_drm_syncobj *sobj = to_drm_syncobj(sync);

   int err = device->sync->reset(device->sync, &sobj->syncobj, 1);
   if (err) {
      return vk_errorf(device, VK_ERROR_UNKNOWN,
                       "DRM_IOCTL_SYNCOBJ_RESET failed: %m");
   }

   return VK_SUCCESS;
}

static VkResult
vk_drm_syncobj_reset_many(struct vk_device *device,
                          uint32_t sync_count,
                          struct vk_sync *const *syncs)
{
   if (sync_count == 0)
      return VK_SUCCESS;

   STACK_ARRAY(uint32_t, handles, sync_count);

   for (uint32_t i = 0; i < sync_count; i++)
      handles[i] = to_drm_syncobj(syncs[i])->syncobj;

   int err = device->sync->reset(device->sync, handles, sync_count);

   STACK_ARRAY_FINISH(handles);

   if (err) {
      return vk_errorf(device, VK_ERROR_UNKNOWN,
                       "DRM_IOCTL_SYNCOBJ_RESET failed: %m");
   }

   return VK_SUCCESS;
}

static VkResult
sync_has_sync_file(struct vk_device *device, struct vk_sync *sync)
{
   uint32_t handle = to_drm_syncobj(sync)->syncobj;

   int fd = -1;
   int err = device->sync->export_sync_file(device->sync, handle, &fd);
   if (!err) {
      close(fd);
      return VK_SUCCESS;
   }

   /* On the off chance the sync_file export repeatedly fails for some
    * unexpected reason, we want to ensure this function will return success
    * eventually.  Do a zero-time syncobj wait if the export failed.
    */
   err = device->sync->wait(device->sync, &handle, 1, 0 /* timeout */,
                            DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
                            NULL /* first_signaled */);
   if (!err) {
      return VK_SUCCESS;
   } else if (errno == ETIME) {
      return VK_TIMEOUT;
   } else {
      return vk_errorf(device, VK_ERROR_UNKNOWN,
                       "DRM_IOCTL_SYNCOBJ_WAIT failed: %m");
   }
}

static VkResult
spin_wait_for_sync_file(struct vk_device *device,
                        uint32_t wait_count,
                        const struct vk_sync_wait *waits,
                        enum vk_sync_wait_flags wait_flags,
                        uint64_t abs_timeout_ns)
{
   if (wait_flags & VK_SYNC_WAIT_ANY) {
      while (1) {
         for (uint32_t i = 0; i < wait_count; i++) {
            VkResult result = sync_has_sync_file(device, waits[i].sync);
            if (result != VK_TIMEOUT)
               return result;
         }

         if (os_time_get_nano() >= abs_timeout_ns)
            return VK_TIMEOUT;

         sched_yield();
      }
   } else {
      for (uint32_t i = 0; i < wait_count; i++) {
         while (1) {
            VkResult result = sync_has_sync_file(device, waits[i].sync);
            if (result == VK_SUCCESS)
               break; /* Break out of the inner while */

            if (result != VK_TIMEOUT)
               return result;

            if (os_time_get_nano() >= abs_timeout_ns)
               return VK_TIMEOUT;

            sched_yield();
         }
      }
   }

   return VK_SUCCESS;
}

static VkResult
vk_drm_syncobj_wait_many(struct vk_device *device,
                         uint32_t wait_count,
                         const struct vk_sync_wait *waits,
                         enum vk_sync_wait_flags wait_flags,
                         uint64_t abs_timeout_ns)
{
   if ((wait_flags & VK_SYNC_WAIT_PENDING) &&
       !(waits[0].sync->type->features & VK_SYNC_FEATURE_TIMELINE)) {
      /* Sadly, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE was never implemented
       * for drivers that don't support timelines.  Instead, we have to spin
       * on DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE until it succeeds.
       */
      return spin_wait_for_sync_file(device, wait_count, waits,
                                     wait_flags, abs_timeout_ns);
   }

   /* Syncobj timeouts are signed */
   abs_timeout_ns = MIN2(abs_timeout_ns, (uint64_t)INT64_MAX);

   STACK_ARRAY(uint32_t, handles, wait_count);
   STACK_ARRAY(uint64_t, wait_values, wait_count);

   uint32_t j = 0;
   bool has_timeline = false;
   for (uint32_t i = 0; i < wait_count; i++) {
      /* The syncobj API doesn't like wait values of 0 but it's safe to skip
       * them because a wait for 0 is a no-op.
       */
      if (waits[i].sync->flags & VK_SYNC_IS_TIMELINE) {
         if (waits[i].wait_value == 0)
            continue;

         has_timeline = true;
      }

      handles[j] = to_drm_syncobj(waits[i].sync)->syncobj;
      wait_values[j] = waits[i].wait_value;
      j++;
   }
   assert(j <= wait_count);
   wait_count = j;

   uint32_t syncobj_wait_flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT;
   if (!(wait_flags & VK_SYNC_WAIT_ANY))
      syncobj_wait_flags |= DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL;

   int err;
   if (wait_count == 0) {
      err = 0;
   } else if (wait_flags & VK_SYNC_WAIT_PENDING) {
      /* We always use a timeline wait for WAIT_PENDING, even for binary
       * syncobjs because the non-timeline wait doesn't support
       * DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE.
       */
      err = device->sync->timeline_wait(device->sync, handles, wait_values,
                                        wait_count, abs_timeout_ns,
                                        syncobj_wait_flags |
                                        DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE,
                                        NULL /* first_signaled */);
   } else if (has_timeline) {
      err = device->sync->timeline_wait(device->sync, handles, wait_values,
                                        wait_count, abs_timeout_ns,
                                        syncobj_wait_flags,
                                        NULL /* first_signaled */);
   } else {
      err = device->sync->wait(device->sync, handles,
                               wait_count, abs_timeout_ns,
                               syncobj_wait_flags,
                               NULL /* first_signaled */);
   }

   STACK_ARRAY_FINISH(handles);
   STACK_ARRAY_FINISH(wait_values);

   if (err && errno == ETIME) {
      return VK_TIMEOUT;
   } else if (err) {
      return vk_errorf(device, VK_ERROR_UNKNOWN,
                       "DRM_IOCTL_SYNCOBJ_WAIT failed: %m");
   }

   return VK_SUCCESS;
}

static VkResult
vk_drm_syncobj_import_opaque_fd(struct vk_device *device,
                                struct vk_sync *sync,
                                int fd)
{
   struct vk_drm_syncobj *sobj = to_drm_syncobj(sync);

   uint32_t new_handle;
   int err = device->sync->fd_to_handle(device->sync, fd, &new_handle);
   if (err) {
      return vk_errorf(device, VK_ERROR_UNKNOWN,
                       "DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE failed: %m");
   }

   err = device->sync->destroy(device->sync, sobj->syncobj);
   assert(!err);

   sobj->syncobj = new_handle;

   return VK_SUCCESS;
}

static VkResult
vk_drm_syncobj_export_opaque_fd(struct vk_device *device,
                                struct vk_sync *sync,
                                int *fd)
{
   struct vk_drm_syncobj *sobj = to_drm_syncobj(sync);

   int err = device->sync->handle_to_fd(device->sync, sobj->syncobj, fd);
   if (err) {
      return vk_errorf(device, VK_ERROR_UNKNOWN,
                       "DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD failed: %m");
   }

   return VK_SUCCESS;
}

static VkResult
vk_drm_syncobj_import_sync_file(struct vk_device *device,
                                struct vk_sync *sync,
                                int sync_file)
{
   struct vk_drm_syncobj *sobj = to_drm_syncobj(sync);

   int err = device->sync->import_sync_file(device->sync, sobj->syncobj, sync_file);
   if (err) {
      return vk_errorf(device, VK_ERROR_UNKNOWN,
                       "DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE failed: %m");
   }

   return VK_SUCCESS;
}

static VkResult
vk_drm_syncobj_export_sync_file(struct vk_device *device,
                                struct vk_sync *sync,
                                int *sync_file)
{
   struct vk_drm_syncobj *sobj = to_drm_syncobj(sync);

   int err = device->sync->export_sync_file(device->sync, sobj->syncobj, sync_file);
   if (err) {
      return vk_errorf(device, VK_ERROR_UNKNOWN,
                       "DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD failed: %m");
   }

   return VK_SUCCESS;
}

static VkResult
vk_drm_syncobj_move(struct vk_device *device,
                    struct vk_sync *dst,
                    struct vk_sync *src)
{
   struct vk_drm_syncobj *dst_sobj = to_drm_syncobj(dst);
   struct vk_drm_syncobj *src_sobj = to_drm_syncobj(src);
   VkResult result;

   if (!(dst->flags & VK_SYNC_IS_SHARED) &&
       !(src->flags & VK_SYNC_IS_SHARED)) {
      result = vk_drm_syncobj_reset(device, dst);
      if (unlikely(result != VK_SUCCESS))
         return result;

      SWAP(dst_sobj->syncobj, src_sobj->syncobj);

      return VK_SUCCESS;
   } else {
      int fd;
      result = vk_drm_syncobj_export_sync_file(device, src, &fd);
      if (result != VK_SUCCESS)
         return result;

      result = vk_drm_syncobj_import_sync_file(device, dst, fd);
      if (fd >= 0)
         close(fd);
      if (result != VK_SUCCESS)
         return result;

      return vk_drm_syncobj_reset(device, src);
   }
}

static VkResult
vk_drm_copy_sync_file_payloads(struct vk_device *device,
                               uint32_t wait_count,
                               const struct vk_sync_wait *waits,
                               uint32_t signal_count,
                               const struct vk_sync_signal *signals)
{
   VkResult result = VK_SUCCESS;
   int merged = -1;

   for (uint32_t i = 0; i < wait_count; i++) {
      assert(!(waits[i].sync->flags & VK_SYNC_IS_TIMELINE));
      assert(waits[i].wait_value == 0);

      int wait_fd = -1;
      result = vk_drm_syncobj_export_sync_file(device, waits[i].sync, &wait_fd);
      if (result != VK_SUCCESS)
         goto fail;

      /* -1 means it's already signaled, so nothing to merge. */
      if (wait_fd == -1)
         continue;

      if (merged == -1) {
         merged = wait_fd;
      } else {
         int ret = sync_merge("vk_drm_syncobj", merged, wait_fd);
         close(wait_fd);
         if (ret < 0) {
            result = vk_errorf(device, VK_ERROR_UNKNOWN,
                               "SYNC_IOC_MERGE failed: %m");
            goto fail;
         }
         close(merged);
         merged = ret;
      }
   }

   /* merged == -1 could either mean that we had no waits or it could mean
    * that they were all already complete.  In either case there's nothing to
    * wait on so we can  just signal everything.
    */
   if (merged == -1)
      return vk_drm_syncobj_signal_many(device, signal_count, signals);

   for (uint32_t i = 0; i < signal_count; i++) {
      assert(!(signals[i].sync->flags & VK_SYNC_IS_TIMELINE));
      assert(signals[i].signal_value == 0);

      result = vk_drm_syncobj_import_sync_file(device, signals[i].sync, merged);
      if (result != VK_SUCCESS)
         goto fail;
   }

fail:
   if (merged >= 0)
      close(merged);

   return result;
}

static VkResult
vk_drm_syncobj_transfer_payloads(struct vk_device *device,
                                 uint32_t wait_count,
                                 const struct vk_sync_wait *waits,
                                 uint32_t signal_count,
                                 const struct vk_sync_signal *signals)
{
   if (wait_count == 1) {
      /* If we only have one wait, we can transfer directly into each of the
       * signal syncs.
       */
      struct vk_drm_syncobj *wait_sobj = to_drm_syncobj(waits[0].sync);
      const uint64_t wait_value = waits[0].wait_value;

      for (uint32_t i = 0; i < signal_count; i++) {
         struct vk_drm_syncobj *signal_sobj = to_drm_syncobj(signals[i].sync);
         const uint64_t signal_value = signals[i].signal_value;

         /* It's possible that we're waiting and signaling the same syncobj */
         if (signal_sobj == wait_sobj) {
            if (wait_sobj->base.flags & VK_SYNC_IS_TIMELINE) {
               /* We have to be signaling a higher value */
               assert(signal_value > wait_value);
            } else {
               /* Don't copy into ourself */
               continue;
            }
         }

         int err = device->sync->transfer(device->sync,
                                          signal_sobj->syncobj, signal_value,
                                          wait_sobj->syncobj, wait_value, 0);
         if (err) {
            return vk_errorf(device, VK_ERROR_UNKNOWN,
                             "DRM_IOCTL_SYNCOBJ_TRANSFER failed: %m");
         }
      }

      return VK_SUCCESS;
   } else {
      /* This is the annoying case where we have to do an actual many-to-many
       * transfer.  This requires us to go through an intermediary syncobj.
       *
       * We'll build up tmp_syncobj as a timeline and then transfer from it
       * as a binary.  The behavior of dma_fence_chain in the kernel is that
       * waiting on a whole chain waits on everything.
       */
      uint32_t tmp_syncobj;
      int err = device->sync->create(device->sync, 0, &tmp_syncobj);
      if (err) {
         return vk_errorf(device, VK_ERROR_UNKNOWN,
                          "DRM_IOCTL_SYNCOBJ_CREATE failed: %m");
      }

      for (uint32_t i = 0; i < wait_count; i++) {
         struct vk_drm_syncobj *wait_sobj = to_drm_syncobj(waits[i].sync);
         const uint64_t wait_value = waits[i].wait_value;

         err = device->sync->transfer(device->sync, tmp_syncobj, i + 1,
                                      wait_sobj->syncobj, wait_value, 0);
         if (err) {
            err = device->sync->destroy(device->sync, tmp_syncobj);
            assert(err == 0);
            return vk_errorf(device, VK_ERROR_UNKNOWN,
                             "DRM_IOCTL_SYNCOBJ_TRANSFER failed: %m");
         }
      }

      for (uint32_t i = 0; i < signal_count; i++) {
         struct vk_drm_syncobj *signal_sobj = to_drm_syncobj(signals[i].sync);
         const uint64_t signal_value = signals[i].signal_value;

         int err = device->sync->transfer(device->sync,
                                          signal_sobj->syncobj, signal_value,
                                          tmp_syncobj, 0, 0);
         if (err) {
            err = device->sync->destroy(device->sync, tmp_syncobj);
            assert(err == 0);
            return vk_errorf(device, VK_ERROR_UNKNOWN,
                             "DRM_IOCTL_SYNCOBJ_TRANSFER failed: %m");
         }
      }

      err = device->sync->destroy(device->sync, tmp_syncobj);
      assert(err == 0);

      return VK_SUCCESS;
   }
}

static bool
vk_device_has_timeline_syncobj(struct vk_device *device)
{
   /* This is annoyingly complex but nothing compared to calling an ioctl. */
   for (const struct vk_sync_type *const *t =
        device->physical->supported_sync_types; *t; t++) {
      if (vk_sync_type_is_drm_syncobj(*t) &&
          ((*t)->features & VK_SYNC_FEATURE_TIMELINE))
         return true;
   }
   return false;
}

VkResult
vk_drm_syncobj_copy_payloads(struct vk_device *device,
                             uint32_t wait_count,
                             const struct vk_sync_wait *waits,
                             uint32_t signal_count,
                             const struct vk_sync_signal *signals)
{
   /* First check if there's even anything to signal */
   if (signal_count == 0)
      return VK_SUCCESS;

   /* If there's nothing to wait on, just signal everything */
   if (wait_count == 0)
      return vk_drm_syncobj_signal_many(device, signal_count, signals);

   if (vk_device_has_timeline_syncobj(device)) {
      return vk_drm_syncobj_transfer_payloads(device, wait_count, waits,
                                              signal_count, signals);
   } else {
      return vk_drm_copy_sync_file_payloads(device, wait_count, waits,
                                            signal_count, signals);
   }
}

struct vk_sync_type
vk_drm_syncobj_get_type_from_provider(struct util_sync_provider *sync)
{
   uint32_t syncobj = 0;
   int err = sync->create(sync, DRM_SYNCOBJ_CREATE_SIGNALED, &syncobj);
   if (err < 0)
      return (struct vk_sync_type) { .features = 0 };

   struct vk_sync_type type = {
      .size = sizeof(struct vk_drm_syncobj),
      .features = VK_SYNC_FEATURE_BINARY |
                  VK_SYNC_FEATURE_GPU_WAIT |
                  VK_SYNC_FEATURE_GPU_MULTI_WAIT |
                  VK_SYNC_FEATURE_CPU_RESET |
                  VK_SYNC_FEATURE_CPU_SIGNAL |
                  VK_SYNC_FEATURE_WAIT_PENDING,
      .init = vk_drm_syncobj_init,
      .finish = vk_drm_syncobj_finish,
      .signal = vk_drm_syncobj_signal,
      .signal_many = vk_drm_syncobj_signal_many,
      .reset = vk_drm_syncobj_reset,
      .reset_many = vk_drm_syncobj_reset_many,
      .move = vk_drm_syncobj_move,
      .import_opaque_fd = vk_drm_syncobj_import_opaque_fd,
      .export_opaque_fd = vk_drm_syncobj_export_opaque_fd,
      .import_sync_file = vk_drm_syncobj_import_sync_file,
      .export_sync_file = vk_drm_syncobj_export_sync_file,
   };

   err = sync->wait(sync, &syncobj, 1, 0,
                    DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL,
                    NULL /* first_signaled */);
   if (err == 0) {
      type.wait_many = vk_drm_syncobj_wait_many;
      type.features |= VK_SYNC_FEATURE_CPU_WAIT |
                       VK_SYNC_FEATURE_WAIT_ANY;
   }

   if (sync->timeline_wait) {
      type.get_value = vk_drm_syncobj_get_value;
      type.features |= VK_SYNC_FEATURE_TIMELINE;
   }

   err = sync->destroy(sync, syncobj);
   assert(err == 0);

   return type;
}

struct vk_sync_type
vk_drm_syncobj_get_type(int drm_fd)
{
   struct util_sync_provider *sync = util_sync_provider_drm(drm_fd);
   struct vk_sync_type ret = vk_drm_syncobj_get_type_from_provider(sync);
   sync->finalize(sync);
   return ret;
}
