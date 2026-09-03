/*
 * Copyright © 2013 Keith Packard
 * Copyright © 2015 Boyan Ding
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include "x11_dri3.h"

#include <stdbool.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/dri3.h>
#include <xcb/present.h>
#include <xcb/xfixes.h>

/** x11_dri3_open
 *
 * Wrapper around xcb_dri3_open
 */
int
x11_dri3_open(xcb_connection_t *conn,
                 xcb_window_t root,
                 uint32_t provider)
{
   xcb_dri3_open_cookie_t       cookie;
   xcb_dri3_open_reply_t        *reply;
   xcb_xfixes_query_version_cookie_t fixes_cookie;
   xcb_xfixes_query_version_reply_t *fixes_reply;
   int                          fd;
   const xcb_query_extension_reply_t *extension;

   xcb_prefetch_extension_data(conn, &xcb_dri3_id);
   extension = xcb_get_extension_data(conn, &xcb_dri3_id);
   if (!(extension && extension->present))
      return -1;

   cookie = xcb_dri3_open(conn,
                          root,
                          provider);

   reply = xcb_dri3_open_reply(conn, cookie, NULL);

   if (!reply || reply->nfd != 1) {
      free(reply);
      return -1;
   }

   fd = xcb_dri3_open_reply_fds(conn, reply)[0];
   free(reply);
   fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);

   /* let the server know our xfixes level */
   fixes_cookie = xcb_xfixes_query_version(conn,
                                           XCB_XFIXES_MAJOR_VERSION,
                                           XCB_XFIXES_MINOR_VERSION);
   fixes_reply = xcb_xfixes_query_version_reply(conn, fixes_cookie, NULL);
   if (fixes_reply->major_version < 2) {
      close(fd);
      fd = -1;
   }
   free(fixes_reply);

   return fd;
}

/* Only request versions of these protocols which we actually support. */
#define DRI3_SUPPORTED_MAJOR 1
#define PRESENT_SUPPORTED_MAJOR 1

#ifdef HAVE_X11_DRM
#define DRI3_SUPPORTED_MINOR 2
#define PRESENT_SUPPORTED_MINOR 2
#else
#define PRESENT_SUPPORTED_MINOR 0
#define DRI3_SUPPORTED_MINOR 0
#endif

bool
x11_dri3_has_multibuffer(xcb_connection_t *c)
{
   xcb_dri3_query_version_cookie_t      dri3_cookie;
   xcb_dri3_query_version_reply_t       *dri3_reply;
   xcb_present_query_version_cookie_t   present_cookie;
   xcb_present_query_version_reply_t    *present_reply;
   xcb_generic_error_t                  *error;
   const xcb_query_extension_reply_t    *extension;

   xcb_prefetch_extension_data(c, &xcb_dri3_id);
   xcb_prefetch_extension_data(c, &xcb_present_id);

   extension = xcb_get_extension_data(c, &xcb_dri3_id);
   if (!(extension && extension->present))
      return false;

   extension = xcb_get_extension_data(c, &xcb_present_id);
   if (!(extension && extension->present))
      return false;

   dri3_cookie = xcb_dri3_query_version(c,
                                        DRI3_SUPPORTED_MAJOR,
                                        DRI3_SUPPORTED_MINOR);
   present_cookie = xcb_present_query_version(c,
                                              PRESENT_SUPPORTED_MAJOR,
                                              PRESENT_SUPPORTED_MINOR);

   dri3_reply = xcb_dri3_query_version_reply(c, dri3_cookie, &error);
   if (!dri3_reply) {
      free(error);
      return false;
   }

   int dri3Major = dri3_reply->major_version;
   int dri3Minor = dri3_reply->minor_version;
   free(dri3_reply);

   present_reply = xcb_present_query_version_reply(c, present_cookie, &error);
   if (!present_reply) {
      free(error);
      return false;
   }
   int presentMajor = present_reply->major_version;
   int presentMinor = present_reply->minor_version;
   free(present_reply);

   return (presentMajor > 1 || (presentMajor == 1 && presentMinor >= 2)) &&
      (dri3Major > 1 || (dri3Major == 1 && dri3Minor >= 2));
}
