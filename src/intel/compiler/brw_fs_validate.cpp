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

/** @file brw_fs_validate.cpp
 *
 * Implements a pass that validates various invariants of the IR.  The current
 * pass only validates that GRF's uses are sane.  More can be added later.
 */

#include "brw_fs.h"
#include "brw_cfg.h"

#define fsv_assert_eq(first, second)                                    \
   {                                                                    \
      unsigned f = (first);                                             \
      unsigned s = (second);                                            \
      if (f != s) {                                                     \
         fprintf(stderr, "ASSERT: Scalar %s validation failed!\n", stage_abbrev); \
         dump_instruction(inst, stderr);                                \
         fprintf(stderr, "%s:%d: A == B failed\n", __FILE__, __LINE__); \
         fprintf(stderr, "  A = %s = %u\n", #first, f);                 \
         fprintf(stderr, "  B = %s = %u\n", #second, s);                \
         abort();                                                       \
      }                                                                 \
   }

#define fsv_assert_lte(first, second)                                   \
   {                                                                    \
      unsigned f = (first);                                             \
      unsigned s = (second);                                            \
      if (f > s) {                                                      \
         fprintf(stderr, "ASSERT: Scalar %s validation failed!\n", stage_abbrev); \
         dump_instruction(inst, stderr);                                \
         fprintf(stderr, "%s:%d: A <= B failed\n", __FILE__, __LINE__); \
         fprintf(stderr, "  A = %s = %u\n", #first, f);                 \
         fprintf(stderr, "  B = %s = %u\n", #second, s);                \
         abort();                                                       \
      }                                                                 \
   }

void
fs_visitor::validate()
{
#ifndef NDEBUG
   foreach_block_and_inst (block, fs_inst, inst, cfg) {
      if (inst->opcode == SHADER_OPCODE_URB_WRITE_LOGICAL) {
         const unsigned header_size = 1 +
            unsigned(inst->src[URB_LOGICAL_SRC_PER_SLOT_OFFSETS].file != BAD_FILE) +
            unsigned(inst->src[URB_LOGICAL_SRC_CHANNEL_MASK].file != BAD_FILE);

         unsigned data_size = 0;
         for (unsigned i = header_size, j = 0; i < inst->mlen; i++, j++) {
            fsv_assert_eq(type_sz(offset(inst->src[URB_LOGICAL_SRC_DATA], bld, j).type), 4);
            data_size++;
         }

         fsv_assert_eq(header_size + data_size, inst->mlen);
      }

      if (inst->dst.file == VGRF) {
         fsv_assert_lte(inst->dst.offset / REG_SIZE + regs_written(inst),
                        alloc.sizes[inst->dst.nr]);
      }

      for (unsigned i = 0; i < inst->sources; i++) {
         if (inst->src[i].file == VGRF) {
            fsv_assert_lte(inst->src[i].offset / REG_SIZE + regs_read(inst, i),
                           alloc.sizes[inst->src[i].nr]);
         }
      }
   }
#endif
}
