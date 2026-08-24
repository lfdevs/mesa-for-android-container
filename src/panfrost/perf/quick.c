/*
 * Copyright © 2021 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <xf86drm.h>
#include <inttypes.h>
#include <stdio.h>
#include <lib/kmod/pan_kmod.h>
#include <lib/pan_props.h>
#include "pan_perf.h"

int
main(void)
{
   int fd = drmOpenWithType("panfrost", NULL, DRM_NODE_RENDER);

   if (fd < 0) {
      fprintf(stderr, "No panfrost device\n");
      exit(1);
   }

   struct pan_perf *perf = pan_perf_create(fd);
   if (!perf)
      return -1;

   /* 100ms sampling period. */
   int ret = pan_perf_enable(perf, 100000000ull);

   if (ret < 0) {
      fprintf(stderr, "failed to enable counters (%d)\n", ret);
      fprintf(
         stderr,
         "try `# echo Y > /sys/module/panfrost/parameters/unstable_ioctls`\n");

      exit(1);
   }

   sleep(1);

   pan_perf_dump(perf);

   for (const struct mali_perf_counter *ctr = perf->info->counters; ctr->name;
        ctr++) {
      int64_t val = pan_perf_counter_read_sum(perf, ctr);

      printf("%s: %" PRId64 "\n", ctr->name, val);
   }

   if (pan_perf_disable(perf) < 0) {
      fprintf(stderr, "failed to disable counters\n");
      exit(1);
   }

   pan_perf_destroy(perf);
   return 0;
}
