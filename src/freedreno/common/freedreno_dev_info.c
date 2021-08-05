/*
 * Copyright © 2020 Valve Corporation
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
 *
 */

#include "freedreno_dev_info.h"
#include "util/macros.h"

extern const struct fd_dev_id fd_dev_ids[];
extern const unsigned fd_dev_ids_count;

const struct fd_dev_info *
fd_dev_info(uint32_t gpu_id)
{
   for (int i = 0; i < fd_dev_ids_count; i++) {
      if (gpu_id == fd_dev_ids[i].gpu_id) {
         return fd_dev_ids[i].info;
      }
   }
   return NULL;
}

const char *
fd_dev_name(uint32_t gpu_id)
{
   for (int i = 0; i < fd_dev_ids_count; i++) {
      if (gpu_id == fd_dev_ids[i].gpu_id) {
         return fd_dev_ids[i].name;
      }
   }
   return NULL;
}
