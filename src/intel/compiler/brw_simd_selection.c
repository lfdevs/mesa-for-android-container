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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "brw_private.h"
#include "compiler/shader_info.h"
#include "intel/dev/intel_debug.h"
#include "intel/dev/intel_device_info.h"
#include "util/ralloc.h"

unsigned
brw_required_dispatch_width(const struct shader_info *info)
{
   if ((int)info->subgroup_size >= (int)SUBGROUP_SIZE_REQUIRE_8) {
      assert(gl_shader_stage_uses_workgroup(info->stage));
      /* These enum values are expressly chosen to be equal to the subgroup
       * size that they require.
       */
      return (unsigned)info->subgroup_size;
   } else {
      return 0;
   }
}

static inline bool
test_bit(unsigned mask, unsigned bit) {
   return mask & (1u << bit);
}

bool
brw_simd_should_compile(void *mem_ctx,
                        unsigned simd,
                        const struct intel_device_info *devinfo,
                        struct brw_cs_prog_data *prog_data,
                        unsigned required,
                        const char **error)

{
   assert(!test_bit(prog_data->prog_mask, simd));
   assert(error);

   const unsigned width = 8u << simd;

   /* For shaders with variable size workgroup, in most cases we can compile
    * all the variants (exceptions are bindless dispatch & ray queries), since
    * the choice will happen only at dispatch time.
    */
   const bool workgroup_size_variable = prog_data->local_size[0] == 0;

   if (!workgroup_size_variable) {
      if (test_bit(prog_data->prog_spilled, simd)) {
         *error = ralloc_asprintf(
            mem_ctx, "SIMD%u skipped because would spill", width);
         return false;
      }

      const unsigned workgroup_size = prog_data->local_size[0] *
                                      prog_data->local_size[1] *
                                      prog_data->local_size[2];

      unsigned max_threads = devinfo->max_cs_workgroup_threads;

      if (required && required != width) {
         *error = ralloc_asprintf(
            mem_ctx, "SIMD%u skipped because required dispatch width is %u",
            width, required);
         return false;
      }

      if (simd > 0 && test_bit(prog_data->prog_mask, simd - 1) &&
          workgroup_size <= (width / 2)) {
         *error = ralloc_asprintf(
            mem_ctx, "SIMD%u skipped because workgroup size %u already fits in SIMD%u",
            width, workgroup_size, width / 2);
         return false;
      }

      if (DIV_ROUND_UP(workgroup_size, width) > max_threads) {
         *error = ralloc_asprintf(
            mem_ctx, "SIMD%u can't fit all %u invocations in %u threads",
            width, workgroup_size, max_threads);
         return false;
      }

      /* The SIMD32 is only enabled for cases it is needed unless forced.
       *
       * TODO: Use performance_analysis and drop this rule.
       */
      if (width == 32) {
         if (!INTEL_DEBUG(DEBUG_DO32) && prog_data->prog_mask) {
            *error = ralloc_strdup(
               mem_ctx, "SIMD32 skipped because not required");
            return false;
         }
      }
   }

   if (width == 32 && prog_data->base.ray_queries > 0) {
      *error = ralloc_asprintf(
         mem_ctx, "SIMD%u skipped because of ray queries",
         width);
      return false;
   }

   if (width == 32 && prog_data->uses_btd_stack_ids) {
      *error = ralloc_asprintf(
         mem_ctx, "SIMD%u skipped because of bindless shader calls",
         width);
      return false;
   }

   const bool env_skip[3] = {
      INTEL_DEBUG(DEBUG_NO8),
      INTEL_DEBUG(DEBUG_NO16),
      INTEL_DEBUG(DEBUG_NO32),
   };

   if (unlikely(env_skip[simd])) {
      *error = ralloc_asprintf(
         mem_ctx, "SIMD%u skipped because INTEL_DEBUG=no%u",
         width, width);
      return false;
   }

   return true;
}

void
brw_simd_mark_compiled(unsigned simd, struct brw_cs_prog_data *prog_data, bool spilled)
{
   assert(!test_bit(prog_data->prog_mask, simd));

   prog_data->prog_mask |= 1u << simd;

   /* If a SIMD spilled, all the larger ones would spill too. */
   if (spilled) {
      for (unsigned i = simd; i < 3; i++)
         prog_data->prog_spilled |= 1u << i;
   }
}

int
brw_simd_select(const struct brw_cs_prog_data *prog_data)
{
   assert((prog_data->prog_mask & ~0x7u) == 0);
   const unsigned not_spilled_mask =
      prog_data->prog_mask & ~prog_data->prog_spilled;

   /* Util functions index bits from 1 instead of 0, adjust before return. */

   if (not_spilled_mask)
      return util_last_bit(not_spilled_mask) - 1;
   else if (prog_data->prog_mask)
      return ffs(prog_data->prog_mask) - 1;
   else
      return -1;
}

int
brw_simd_select_for_workgroup_size(const struct intel_device_info *devinfo,
                                   const struct brw_cs_prog_data *prog_data,
                                   const unsigned *sizes)
{
   assert(sizes);

   if (prog_data->local_size[0] == sizes[0] &&
       prog_data->local_size[1] == sizes[1] &&
       prog_data->local_size[2] == sizes[2])
      return brw_simd_select(prog_data);

   void *mem_ctx = ralloc_context(NULL);

   struct brw_cs_prog_data cloned = *prog_data;
   for (unsigned i = 0; i < 3; i++)
      cloned.local_size[i] = sizes[i];

   cloned.prog_mask = 0;
   cloned.prog_spilled = 0;

   const char *error[3] = {0};

   for (unsigned simd = 0; simd < 3; simd++) {
      /* We are not recompiling, so use original results of prog_mask and
       * prog_spilled as they will already contain all possible compilations.
       */
      if (brw_simd_should_compile(mem_ctx, simd, devinfo, &cloned,
                                  0 /* required_dispatch_width */, &error[simd]) &&
          test_bit(prog_data->prog_mask, simd)) {
         brw_simd_mark_compiled(simd, &cloned, test_bit(prog_data->prog_spilled, simd));
      }
   }

   ralloc_free(mem_ctx);

   return brw_simd_select(&cloned);
}
