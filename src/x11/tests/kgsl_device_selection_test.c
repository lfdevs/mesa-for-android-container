/* SPDX-License-Identifier: MIT */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "x11_dri3.h"

/* Hardware selection test only: no X server, allocation, GPU submission or
 * display changes. A deliberately invalid display provides an errored XCB
 * connection, so server-fd paths must fail instead of silently opening KGSL.
 */
static const struct {
   const char *name;
   const char *driver;
   const char *bridge;
   const char *renderonly;
   bool native;
} cases[] = {
   {"ordinary KGSL", "kgsl", NULL, NULL, true},
   {"disabled HDMI options", "kgsl", "0", "0", true},
   {"HDMI renderonly", "kgsl", NULL, "1", false},
   {"HDMI renderonly boolean", "kgsl", NULL, "true", false},
   {"HDMI bridge", "kgsl", "1", NULL, true},
   {"HDMI bridge boolean", "kgsl", "true", NULL, true},
   {"other driver", "llvmpipe", NULL, NULL, false},
   {"no override", NULL, NULL, NULL, false},
};

static int
set_option(const char *name, const char *value)
{
   return value ? setenv(name, value, 1) : unsetenv(name);
}

int
main(void)
{
   struct stat kgsl_stat;
   int reference = open("/dev/kgsl-3d0", O_RDWR | O_CLOEXEC);
   if (reference < 0) {
      fprintf(stderr, "SKIP: /dev/kgsl-3d0 is not accessible\n");
      return 77;
   }
   int ret = fstat(reference, &kgsl_stat);
   close(reference);
   if (ret < 0 || !S_ISCHR(kgsl_stat.st_mode))
      return 1;

   xcb_connection_t *conn = xcb_connect(":", NULL);
   if (xcb_connection_has_error(conn) != XCB_CONN_CLOSED_PARSE_ERR) {
      fprintf(stderr, "Expected an invalid-display XCB connection\n");
      xcb_disconnect(conn);
      return 1;
   }

   int result = 0;
   for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      if (set_option("MESA_LOADER_DRIVER_OVERRIDE", cases[i].driver) ||
          set_option("MESA_KGSL_X11_SHM_BRIDGE", cases[i].bridge) ||
          set_option("FD_KGSL_RENDERONLY", cases[i].renderonly)) {
         perror("set device-selection options");
         result = 1;
         break;
      }

      int fd = x11_dri3_open(conn, 0, 0);
      bool passed = fd < 0 && !cases[i].native;
      if (fd >= 0) {
         struct stat selected;
         int flags = fcntl(fd, F_GETFD);
         passed = cases[i].native && fstat(fd, &selected) == 0 &&
                  S_ISCHR(selected.st_mode) &&
                  selected.st_rdev == kgsl_stat.st_rdev &&
                  flags >= 0 && (flags & FD_CLOEXEC);
         close(fd);
      }
      printf("%s: %s\n", passed ? "PASS" : "FAIL", cases[i].name);
      if (!passed)
         result = 1;
   }

   xcb_disconnect(conn);
   return result;
}
