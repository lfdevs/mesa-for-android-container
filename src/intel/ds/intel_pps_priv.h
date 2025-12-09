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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef INTEL_PPS_PRIV_H
#define INTEL_PPS_PRIV_H

#include <string.h>

#include "util/hash_table.h"

/* Common clock_id name for all perfetto producers */
static inline uint32_t
intel_pps_clock_id(uint32_t gpu_id)
{
   /* See the "Sequence-scoped clocks" section of
    * https://perfetto.dev/docs/concepts/clock-sync
    *
    * intel_pps_clock_id can be called from:
    * - intel_ds_device_init
    *   - anv, hasvk and iris all pass minor
    * - IntelDriver::init_perfcnt
    *   - pps passes DrmDevice::gpu_num, which is the index into the devices
    *     returned from drmGetDevices2 capped to 64 devices
    *
    * So (gpu_id & 0x3F) is valid for all above, except for render minors
    * dynamically allocated >= 192 which we can fallback to use global gpu
    * clock but with pid additionally hashed in.
    */
   if (gpu_id < 192)
      return 64 + (gpu_id & 0x3F);

   char buf[64];
   snprintf(buf, sizeof(buf),
            "org.freedesktop.mesa.intel.gpu%u.pid%d", gpu_id, (int)getpid());

   return _mesa_hash_string(buf) | 0x80000000;
}

#endif /* INTEL_PPS_PRIV_H */
