/*
 * Copyright © 2017 Lima Project
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

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "c11/threads.h"
#include "util/u_hash_table.h"
#include "util/u_pointer.h"
#include "renderonly/renderonly.h"

#include "lima_drm_public.h"

#include "lima/lima_screen.h"

static struct util_hash_table *fd_tab = NULL;
static mtx_t lima_screen_mutex = _MTX_INITIALIZER_NP;

static void
lima_drm_screen_destroy(struct pipe_screen *pscreen)
{
   struct lima_screen *screen = lima_screen(pscreen);
   boolean destroy;
   int fd = screen->fd;

   mtx_lock(&lima_screen_mutex);
   destroy = --screen->refcnt == 0;
   if (destroy)
      util_hash_table_remove(fd_tab, intptr_to_pointer(fd));
   mtx_unlock(&lima_screen_mutex);

   if (destroy) {
      pscreen->destroy = screen->winsys_priv;
      pscreen->destroy(pscreen);
      close(fd);
   }
}

static unsigned hash_fd(void *key)
{
   int fd = pointer_to_intptr(key);
   struct stat stat;

   fstat(fd, &stat);

   return stat.st_dev ^ stat.st_ino ^ stat.st_rdev;
}

static int compare_fd(void *key1, void *key2)
{
   int fd1 = pointer_to_intptr(key1);
   int fd2 = pointer_to_intptr(key2);
   struct stat stat1, stat2;

   fstat(fd1, &stat1);
   fstat(fd2, &stat2);

   return stat1.st_dev != stat2.st_dev ||
          stat1.st_ino != stat2.st_ino ||
          stat1.st_rdev != stat2.st_rdev;
}

struct pipe_screen *
lima_drm_screen_create(int fd)
{
   struct pipe_screen *pscreen = NULL;

   mtx_lock(&lima_screen_mutex);
   if (!fd_tab) {
      fd_tab = util_hash_table_create(hash_fd, compare_fd);
      if (!fd_tab)
         goto unlock;
   }

   pscreen = util_hash_table_get(fd_tab, intptr_to_pointer(fd));
   if (pscreen) {
      lima_screen(pscreen)->refcnt++;
   } else {
      int dup_fd = fcntl(fd, F_DUPFD_CLOEXEC, 3);

      pscreen = lima_screen_create(dup_fd, NULL);
      if (pscreen) {
         util_hash_table_set(fd_tab, intptr_to_pointer(dup_fd), pscreen);

         /* Bit of a hack, to avoid circular linkage dependency,
          * ie. pipe driver having to call in to winsys, we
          * override the pipe drivers screen->destroy():
          */
         lima_screen(pscreen)->winsys_priv = pscreen->destroy;
         pscreen->destroy = lima_drm_screen_destroy;
      }
   }

unlock:
   mtx_unlock(&lima_screen_mutex);
   return pscreen;
}

struct pipe_screen *
lima_drm_screen_create_renderonly(struct renderonly *ro)
{
   return lima_screen_create(fcntl(ro->gpu_fd, F_DUPFD_CLOEXEC, 3), ro);
}
