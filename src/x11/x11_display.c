/*
 * Copyright © 2025 Collabora, Ltd.
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
#include "x11_display.h"

#include "util/macros.h"
#include "util/log.h"

#include <stdio.h>
#include <X11/Xlibint.h>

#ifdef HAVE_SYS_SHM_H
#include <xcb/shm.h>
#include <sys/shm.h>
#endif

bool
x11_xlib_display_is_thread_safe(Display *dpy)
{
   static bool warned = false;

   /* 'lock_fns' is the XLockDisplay function pointer of the X11 display 'dpy'.
    * It will be NULL if XInitThreads wasn't called.
    */
   if (likely(dpy->lock_fns != NULL))
      return true;

   if (unlikely(!warned)) {
      fprintf(stderr, "Xlib is not thread-safe.  This should never be the "
                      "case starting with XLib 1.8.  Either upgrade XLib "
                      "or call XInitThreads() from your app.\n");
      warned = true;
   }

   return false;
}

#ifdef HAVE_SYS_SHM_H
bool
x11_xcb_display_supports_xshm(xcb_connection_t *con, int *xshm_opcode)
{
   xcb_generic_error_t *error;
   xcb_query_extension_cookie_t shm_cookie;
   xcb_query_extension_reply_t *shm_reply;
   bool has_mit_shm;
   int shmid;
   void *shm_addr = (void *) -1;
   xcb_shm_seg_t shm_seg;
   bool ret = true;
   int xshm_opcode_val = -1;

   shm_cookie = xcb_query_extension(con, 7, "MIT-SHM");
   shm_reply = xcb_query_extension_reply(con, shm_cookie, NULL);

   has_mit_shm = shm_reply && shm_reply->present;
   if (has_mit_shm)
      xshm_opcode_val = shm_reply->major_opcode;
   free(shm_reply);
   if (!has_mit_shm)
      return false;

   /* Check if we're a remote client by attempting to detach segment 0.
    * Remote clients will get BadRequest, local clients get BadValue,
    * since 'info' has an invalid segment name.
    */
   if ((error = xcb_request_check(con,
                                  xcb_shm_detach_checked(con, 0)))) {
      bool is_remote = error->error_code == BadRequest;
      free(error);
      if (is_remote)
         return false;
    }

   /* We're a local client. Now verify we can actually attach SHM.
    * In some container setups, the X server reports MIT-SHM support
    * but the client lacks permission to attach shared memory.
    */
   shmid = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600);
   if (shmid < 0) {
      mesa_logd("shared memory allocation error: %s", strerror(errno));
      return false;
   }

   shm_addr = shmat(shmid, NULL, 0);
   if (shm_addr == (void *) -1) {
      mesa_logd("shmat failed: %s", strerror(errno));
      ret = false;
      goto check_xshm_cleanup;
   }

   shm_seg = xcb_generate_id(con);
   error = xcb_request_check(con,
                             xcb_shm_attach_checked(con,
                                                    shm_seg, shmid, 0));
   if (error) {
      mesa_logd("Failed to attach to x11 shm with error: %u", error->error_code);
      free(error);
      ret = false;
      goto check_xshm_cleanup;
   }

   /* Only detach if it was attached successfully */
   xcb_shm_detach(con, shm_seg);

   /* Only set xshm_opcode if attach was successful */
   if (xshm_opcode)
      *xshm_opcode = xshm_opcode_val;

check_xshm_cleanup:
   if (shm_addr != (void *) -1)
      shmdt(shm_addr);
   shmctl(shmid, IPC_RMID, NULL);

   return ret;
}
#endif
