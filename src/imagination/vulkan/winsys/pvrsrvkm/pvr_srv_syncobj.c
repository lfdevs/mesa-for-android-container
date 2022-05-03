/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include "pvr_private.h"
#include "pvr_srv.h"
#include "pvr_srv_syncobj.h"
#include "pvr_winsys.h"
#include "util/libsync.h"
#include "util/macros.h"
#include "util/timespec.h"
#include "vk_alloc.h"
#include "vk_log.h"

VkResult
pvr_srv_winsys_syncobj_create(struct pvr_winsys *ws,
                              bool signaled,
                              struct pvr_winsys_syncobj **const syncobj_out)
{
   struct pvr_srv_winsys *srv_ws = to_pvr_srv_winsys(ws);
   struct pvr_srv_winsys_syncobj *srv_syncobj;

   srv_syncobj = vk_alloc(srv_ws->alloc,
                          sizeof(*srv_syncobj),
                          8,
                          VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!srv_syncobj)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   srv_syncobj->base.ws = ws;
   srv_syncobj->signaled = signaled;
   srv_syncobj->fd = -1;

   *syncobj_out = &srv_syncobj->base;

   return VK_SUCCESS;
}

void pvr_srv_winsys_syncobj_destroy(struct pvr_winsys_syncobj *syncobj)
{
   struct pvr_srv_winsys_syncobj *srv_syncobj;
   struct pvr_srv_winsys *srv_ws;

   assert(syncobj);

   srv_ws = to_pvr_srv_winsys(syncobj->ws);
   srv_syncobj = to_pvr_srv_winsys_syncobj(syncobj);

   if (srv_syncobj->fd != -1)
      close(srv_syncobj->fd);

   vk_free(srv_ws->alloc, srv_syncobj);
}

/* Note: function closes the fd. */
static void pvr_set_syncobj_state(struct pvr_srv_winsys_syncobj *srv_syncobj,
                                  bool signaled)
{
   if (srv_syncobj->fd != -1) {
      close(srv_syncobj->fd);
      srv_syncobj->fd = -1;
   }

   srv_syncobj->signaled = signaled;
}

void pvr_srv_set_syncobj_payload(struct pvr_winsys_syncobj *syncobj,
                                 int payload)
{
   struct pvr_srv_winsys_syncobj *srv_syncobj =
      to_pvr_srv_winsys_syncobj(syncobj);

   if (srv_syncobj->fd != -1)
      close(srv_syncobj->fd);

   srv_syncobj->fd = payload;
   /* FIXME: Is this valid? */
   srv_syncobj->signaled = (payload == -1);
}

VkResult
pvr_srv_winsys_syncobjs_reset(struct pvr_winsys *ws,
                              struct pvr_winsys_syncobj **const syncobjs,
                              uint32_t count)
{
   for (uint32_t i = 0; i < count; i++) {
      struct pvr_srv_winsys_syncobj *srv_syncobj;

      if (!syncobjs[i])
         continue;

      srv_syncobj = to_pvr_srv_winsys_syncobj(syncobjs[i]);
      pvr_set_syncobj_state(srv_syncobj, false);
   }

   return VK_SUCCESS;
}

VkResult
pvr_srv_winsys_syncobjs_signal(struct pvr_winsys *ws,
                               struct pvr_winsys_syncobj **const syncobjs,
                               uint32_t count)
{
   for (uint32_t i = 0; i < count; i++) {
      struct pvr_srv_winsys_syncobj *srv_syncobj;

      if (!syncobjs[i])
         continue;

      srv_syncobj = to_pvr_srv_winsys_syncobj(syncobjs[i]);
      pvr_set_syncobj_state(srv_syncobj, true);
   }

   return VK_SUCCESS;
}

/* Careful, timeout might overflow. */
static inline void pvr_start_timeout(struct timespec *timeout,
                                     uint64_t timeout_ns)
{
   clock_gettime(CLOCK_MONOTONIC, timeout);
   timespec_add_nsec(timeout, timeout, timeout_ns);
}

/* Careful, a negative value might be returned. */
static inline struct timespec
pvr_get_remaining_time(const struct timespec *timeout)
{
   struct timespec time;

   clock_gettime(CLOCK_MONOTONIC, &time);
   timespec_sub(&time, timeout, &time);

   return time;
}

/* timeout == 0 -> Get status without waiting.
 * timeout == ~0 -> Wait infinitely
 * else wait for the given timeout in nanoseconds. */
VkResult
pvr_srv_winsys_syncobjs_wait(struct pvr_winsys *ws,
                             struct pvr_winsys_syncobj **const syncobjs,
                             uint32_t count,
                             bool wait_all,
                             uint64_t timeout)
{
   const struct pvr_srv_winsys *srv_ws = to_pvr_srv_winsys(ws);
   uint32_t unsignaled_count = 0U;
   struct timespec end_time;
   struct pollfd *poll_fds;
   VkResult result;
   int ppoll_ret;

   if (timeout != 0U && timeout != ~0U) {
      /* We don't worry about overflow since ppoll() returns EINVAL on
       * negative timeout.
       */
      pvr_start_timeout(&end_time, timeout);
   }

   poll_fds = vk_alloc(srv_ws->alloc,
                       sizeof(*poll_fds) * count,
                       8,
                       VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!poll_fds)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   for (uint32_t i = 0; i < count; i++) {
      struct pvr_srv_winsys_syncobj *srv_syncobj =
         to_pvr_srv_winsys_syncobj(syncobjs[i]);

      /* -1 in case if fence is signaled or uninitialized, ppoll will skip the
       * fence.
       */
      if (!srv_syncobj || srv_syncobj->signaled || srv_syncobj->fd == -1) {
         poll_fds[i].fd = -1;
      } else {
         poll_fds[i].fd = srv_syncobj->fd;
         unsignaled_count++;
      }

      poll_fds[i].events = POLLIN;
      poll_fds[i].revents = 0U;
   }

   if (unsignaled_count == 0U) {
      result = VK_SUCCESS;
      goto end_wait_for_fences;
   }

   /* TODO: Implement device loss handling like anvil: reporting the loss
    * save the reported status, maybe abort() on env flag, etc.
    */

   do {
      if (timeout == ~0U) {
         ppoll_ret = ppoll(poll_fds, count, NULL, NULL);
      } else {
         struct timespec remaining_time;

         if (timeout == 0U) {
            remaining_time = (struct timespec){ 0UL, 0UL };
         } else {
            /* ppoll() returns EINVAL on negative timeout. Nothing to worry.
             */
            remaining_time = pvr_get_remaining_time(&end_time);
         }

         ppoll_ret = ppoll(poll_fds, count, &remaining_time, NULL);
      }

      if (ppoll_ret > 0U) {
         /* ppoll_ret contains the amount of structs updated by poll(). */
         unsignaled_count -= ppoll_ret;

         /* ppoll_ret > 0 is for early loop termination. */
         for (uint32_t i = 0; ppoll_ret > 0 && i < count; i++) {
            struct pvr_srv_winsys_syncobj *srv_syncobj;

            if (poll_fds[i].revents == 0)
               continue;

            if (poll_fds[i].revents & (POLLNVAL | POLLERR)) {
               result = vk_error(NULL, VK_ERROR_DEVICE_LOST);
               goto end_wait_for_fences;
            }

            srv_syncobj = to_pvr_srv_winsys_syncobj(syncobjs[i]);
            pvr_set_syncobj_state(srv_syncobj, true);

            if (!wait_all) {
               result = VK_SUCCESS;
               goto end_wait_for_fences;
            }

            /* -1 makes ppoll ignore it and set revents to 0. */
            poll_fds[i].fd = -1;
            ppoll_ret--;
         }

         /* For zero timeout, just return even if we still have unsignaled
          * fences.
          */
         if (timeout == 0U && unsignaled_count != 0U) {
            result = VK_TIMEOUT;
            goto end_wait_for_fences;
         }
      } else if (ppoll_ret == 0) {
         result = VK_TIMEOUT;
         goto end_wait_for_fences;
      }

      /* Careful as we might have decremented ppoll_ret to 0. */
   } while ((ppoll_ret != -1 && unsignaled_count != 0) ||
            (ppoll_ret == -1 && (errno == EINTR || errno == EAGAIN)));

   /* We assume device loss in case of an unknown error or invalid fd. */
   if (ppoll_ret != -1)
      result = VK_SUCCESS;
   else if (errno == EINVAL)
      result = VK_TIMEOUT;
   else if (errno == ENOMEM)
      result = vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);
   else
      result = vk_error(NULL, VK_ERROR_DEVICE_LOST);

end_wait_for_fences:
   vk_free(srv_ws->alloc, poll_fds);

   return result;
}

VkResult pvr_srv_winsys_syncobjs_merge(struct pvr_winsys_syncobj *src,
                                       struct pvr_winsys_syncobj *target,
                                       struct pvr_winsys_syncobj **syncobj_out)
{
   struct pvr_srv_winsys_syncobj *srv_target =
      to_pvr_srv_winsys_syncobj(target);
   struct pvr_srv_winsys_syncobj *srv_src = to_pvr_srv_winsys_syncobj(src);
   struct pvr_srv_winsys_syncobj *srv_output;
   struct pvr_winsys_syncobj *output = NULL;
   VkResult result;

   if (!srv_src || srv_src->fd == -1) {
      *syncobj_out = target;
      return VK_SUCCESS;
   }

   result = pvr_srv_winsys_syncobj_create(src->ws, false, &output);
   if (result != VK_SUCCESS)
      return result;

   srv_output = to_pvr_srv_winsys_syncobj(output);

   if (!srv_target || srv_target->fd == -1) {
      int fd = dup(srv_src->fd);
      if (fd < 0) {
         result = vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);
         goto err_syncobj_destroy;
      }

      pvr_srv_set_syncobj_payload(output, fd);
      if (target)
         pvr_srv_winsys_syncobj_destroy(target);
      *syncobj_out = output;
      return VK_SUCCESS;
   }

   srv_output->fd = sync_merge("", srv_src->fd, srv_target->fd);
   if (srv_output->fd < 0) {
      result = vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_syncobj_destroy;
   }

   pvr_srv_winsys_syncobj_destroy(target);

   *syncobj_out = output;

   return VK_SUCCESS;

err_syncobj_destroy:
   pvr_srv_winsys_syncobj_destroy(output);

   return result;
}
