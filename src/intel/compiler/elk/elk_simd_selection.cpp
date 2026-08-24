/*
 * Copyright © 2021 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "elk_private.h"
#include "compiler/shader_info.h"
#include "intel/dev/intel_debug.h"
#include "intel/dev/intel_device_info.h"
#include "util/ralloc.h"

unsigned
elk_required_dispatch_width(const struct shader_info *info)
{
   if (info->min_subgroup_size == info->max_subgroup_size) {
      return info->max_subgroup_size;
   } else {
      return 0;
   }
}

static inline bool
test_bit(unsigned mask, unsigned bit) {
   return mask & (1u << bit);
}

namespace {

struct elk_cs_prog_data *
get_cs_prog_data(elk_simd_selection_state &state)
{
   if (std::holds_alternative<struct elk_cs_prog_data *>(state.prog_data))
      return std::get<struct elk_cs_prog_data *>(state.prog_data);
   else
      return nullptr;
}

}

/**
 * Return true if INTEL_SIMD_DEBUG allows the given SIMD mode.
 */
static bool
simd_debug_allowed(unsigned simd)
{
   assert(simd <= 2);
   return intel_simd & (DEBUG_CS_SIMD8 << simd);
}

/**
 * Return true if INTEL_SIMD_DEBUG force-enables the given SIMD mode.
 */
static bool
simd_debug_forced(unsigned simd)
{
   return (intel_simd_overridden & (1 << MESA_SHADER_COMPUTE)) &&
          simd_debug_allowed(simd);
}

bool
elk_simd_should_compile(elk_simd_selection_state &state, unsigned simd)
{
   assert(simd < SIMD_COUNT);
   assert(!state.compiled[simd]);

   const auto cs_prog_data = get_cs_prog_data(state);
   const unsigned width = 8u << simd;

   /* For shaders with variable size workgroup, in most cases we can compile
    * all the variants (exceptions are bindless dispatch & ray queries), since
    * the choice will happen only at dispatch time.
    */
   const bool workgroup_size_variable = cs_prog_data && cs_prog_data->local_size[0] == 0;

   if (!workgroup_size_variable) {
      if (state.spilled[simd]) {
         state.error[simd] = "Would spill";
         return false;
      }

      if (state.required_width && state.required_width != width) {
         state.error[simd] = "Different than required dispatch width";
         return false;
      }

      if (cs_prog_data) {
         const unsigned workgroup_size = cs_prog_data->local_size[0] *
                                         cs_prog_data->local_size[1] *
                                         cs_prog_data->local_size[2];

         unsigned max_threads = state.devinfo->max_cs_workgroup_threads;

         if (simd > 0 && state.compiled[simd - 1] &&
            workgroup_size <= (width / 2)) {
            state.error[simd] = "Workgroup size already fits in smaller SIMD";
            return false;
         }

         if (DIV_ROUND_UP(workgroup_size, width) > max_threads) {
            state.error[simd] = "Would need more than max_threads to fit all invocations";
            return false;
         }
      }

      /* The SIMD32 is only enabled for cases it is needed unless forced.
       *
       * TODO: Use performance_analysis and drop this rule.
       */
      if (width == 32) {
         if (!simd_debug_forced(2) &&
             (state.compiled[0] || state.compiled[1])) {
            state.error[simd] = "SIMD32 not required (use INTEL_SIMD_DEBUG=cs32 to force)";
            return false;
         }
      }
   }

   if (unlikely(!simd_debug_allowed(simd))) {
      state.error[simd] = "Disabled by INTEL_SIMD_DEBUG environment variable";
      return false;
   }

   return true;
}

void
elk_simd_mark_compiled(elk_simd_selection_state &state, unsigned simd, bool spilled)
{
   assert(simd < SIMD_COUNT);
   assert(!state.compiled[simd]);

   auto cs_prog_data = get_cs_prog_data(state);

   state.compiled[simd] = true;
   if (cs_prog_data)
      cs_prog_data->prog_mask |= 1u << simd;

   /* If a SIMD spilled, all the larger ones would spill too. */
   if (spilled) {
      for (unsigned i = simd; i < SIMD_COUNT; i++) {
         state.spilled[i] = true;
         if (cs_prog_data)
            cs_prog_data->prog_spilled |= 1u << i;
      }
   }
}

int
elk_simd_select(const struct elk_simd_selection_state &state)
{
   for (int i = SIMD_COUNT - 1; i >= 0; i--) {
      if (state.compiled[i] && !state.spilled[i])
         return i;
   }
   for (int i = SIMD_COUNT - 1; i >= 0; i--) {
      if (state.compiled[i])
         return i;
   }
   return -1;
}

int
elk_simd_select_for_workgroup_size(const struct intel_device_info *devinfo,
                                   const struct elk_cs_prog_data *prog_data,
                                   const unsigned *sizes)
{
   if (!sizes || (prog_data->local_size[0] == sizes[0] &&
                  prog_data->local_size[1] == sizes[1] &&
                  prog_data->local_size[2] == sizes[2])) {
      elk_simd_selection_state simd_state{
         .prog_data = const_cast<struct elk_cs_prog_data *>(prog_data),
      };

      /* Propagate the prog_data information back to the simd_state,
       * so we can use select() directly.
       */
      for (int i = 0; i < SIMD_COUNT; i++) {
         simd_state.compiled[i] = test_bit(prog_data->prog_mask, i);
         simd_state.spilled[i] = test_bit(prog_data->prog_spilled, i);
      }

      return elk_simd_select(simd_state);
   }

   struct elk_cs_prog_data cloned = *prog_data;
   for (unsigned i = 0; i < 3; i++)
      cloned.local_size[i] = sizes[i];

   cloned.prog_mask = 0;
   cloned.prog_spilled = 0;

   elk_simd_selection_state simd_state{
      .devinfo = devinfo,
      .prog_data = &cloned,
   };

   for (unsigned simd = 0; simd < SIMD_COUNT; simd++) {
      /* We are not recompiling, so use original results of prog_mask and
       * prog_spilled as they will already contain all possible compilations.
       */
      if (elk_simd_should_compile(simd_state, simd) &&
          test_bit(prog_data->prog_mask, simd)) {
         elk_simd_mark_compiled(simd_state, simd, test_bit(prog_data->prog_spilled, simd));
      }
   }

   return elk_simd_select(simd_state);
}
