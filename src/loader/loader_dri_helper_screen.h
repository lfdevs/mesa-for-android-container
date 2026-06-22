/*
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

#ifndef LOADER_DRI_HELPER_SCREEN_H
#define LOADER_DRI_HELPER_SCREEN_H

#ifdef HAVE_X11_PLATFORM
#include <xcb/xcb.h>
#include <xcb/dri3.h>
#include <xcb/present.h>

struct loader_crtc_info {
   xcb_randr_crtc_t id;
   xcb_timestamp_t timestamp;

   int16_t x, y;
   uint16_t width, height;

   unsigned refresh_numerator;
   unsigned refresh_denominator;
};

struct loader_screen_resources {
   mtx_t mtx;

   xcb_connection_t *conn;
   xcb_screen_t *screen;

   xcb_timestamp_t config_timestamp;

   /* Number of CRTCs with an active mode set */
   unsigned num_crtcs;
   struct loader_crtc_info *crtcs;
};

void
loader_init_screen_resources(struct loader_screen_resources *res,
                             xcb_connection_t *conn,
                             xcb_screen_t *screen);
bool
loader_update_screen_resources(struct loader_screen_resources *res);

void
loader_destroy_screen_resources(struct loader_screen_resources *res);

#endif

static inline int
box_intersection_area(int16_t a_x, int16_t a_y, int16_t a_width,
                      int16_t a_height, int16_t b_x, int16_t b_y,
                      int16_t b_width, int16_t b_height)
{
   int w = MIN2(a_x + a_width, b_x + b_width) - MAX2(a_x, b_x);
   int h = MIN2(a_y + a_height, b_y + b_height) - MAX2(a_y, b_y);

   return (w < 0 || h < 0) ? 0 : w * h;
}

#endif
