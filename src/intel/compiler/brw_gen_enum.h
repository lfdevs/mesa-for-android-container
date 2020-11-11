/*
 * Copyright © 2015 Intel Corporation
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

#include "util/macros.h"
#include "dev/gen_device_info.h"

enum gen {
   GEN4  = (1 << 0),
   GEN45 = (1 << 1),
   GEN5  = (1 << 2),
   GEN6  = (1 << 3),
   GEN7  = (1 << 4),
   GEN75 = (1 << 5),
   GEN8  = (1 << 6),
   GEN9  = (1 << 7),
   GEN10 = (1 << 8),
   GEN11 = (1 << 9),
   GEN12 = (1 << 10),
   GEN_ALL = ~0
};

#define GEN_LT(gen) ((gen) - 1)
#define GEN_GE(gen) (~GEN_LT(gen))
#define GEN_LE(gen) (GEN_LT(gen) | (gen))

static enum gen
gen_from_devinfo(const struct gen_device_info *devinfo)
{
   switch (devinfo->gen) {
   case 4: return devinfo->is_g4x ? GEN45 : GEN4;
   case 5: return GEN5;
   case 6: return GEN6;
   case 7: return devinfo->is_haswell ? GEN75 : GEN7;
   case 8: return GEN8;
   case 9: return GEN9;
   case 11: return GEN11;
   case 12: return GEN12;
   default:
      unreachable("not reached");
   }
}
