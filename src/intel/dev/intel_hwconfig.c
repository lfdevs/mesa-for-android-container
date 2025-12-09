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

#include <stdio.h>
#include <stdlib.h>

#include "intel_device_info.h"
#include "intel_hwconfig.h"
#include "intel_hwconfig_types.h"
#include "intel/common/intel_gem.h"
#include "i915/intel_device_info.h"
#include "xe/intel_device_info.h"

#include "util/log.h"

struct hwconfig {
   uint32_t key;
   uint32_t len;
   uint32_t val[];
};

static char *
key_to_name(uint32_t key)
{
#define HANDLE(key_name) case key_name: return #key_name
   switch (key) {
      HANDLE(INTEL_HWCONFIG_MAX_SLICES_SUPPORTED);
      HANDLE(INTEL_HWCONFIG_MAX_DUAL_SUBSLICES_SUPPORTED);
      HANDLE(INTEL_HWCONFIG_MAX_NUM_EU_PER_DSS);
      HANDLE(INTEL_HWCONFIG_NUM_PIXEL_PIPES);
      HANDLE(INTEL_HWCONFIG_DEPRECATED_MAX_NUM_GEOMETRY_PIPES);
      HANDLE(INTEL_HWCONFIG_DEPRECATED_L3_CACHE_SIZE_IN_KB);
      HANDLE(INTEL_HWCONFIG_DEPRECATED_L3_BANK_COUNT);
      HANDLE(INTEL_HWCONFIG_L3_CACHE_WAYS_SIZE_IN_BYTES);
      HANDLE(INTEL_HWCONFIG_L3_CACHE_WAYS_PER_SECTOR);
      HANDLE(INTEL_HWCONFIG_MAX_MEMORY_CHANNELS);
      HANDLE(INTEL_HWCONFIG_MEMORY_TYPE);
      HANDLE(INTEL_HWCONFIG_CACHE_TYPES);
      HANDLE(INTEL_HWCONFIG_LOCAL_MEMORY_PAGE_SIZES_SUPPORTED);
      HANDLE(INTEL_HWCONFIG_DEPRECATED_SLM_SIZE_IN_KB);
      HANDLE(INTEL_HWCONFIG_NUM_THREADS_PER_EU);
      HANDLE(INTEL_HWCONFIG_TOTAL_VS_THREADS);
      HANDLE(INTEL_HWCONFIG_TOTAL_GS_THREADS);
      HANDLE(INTEL_HWCONFIG_TOTAL_HS_THREADS);
      HANDLE(INTEL_HWCONFIG_TOTAL_DS_THREADS);
      HANDLE(INTEL_HWCONFIG_TOTAL_VS_THREADS_POCS);
      HANDLE(INTEL_HWCONFIG_TOTAL_PS_THREADS);
      HANDLE(INTEL_HWCONFIG_DEPRECATED_MAX_FILL_RATE);
      HANDLE(INTEL_HWCONFIG_MAX_RCS);
      HANDLE(INTEL_HWCONFIG_MAX_CCS);
      HANDLE(INTEL_HWCONFIG_MAX_VCS);
      HANDLE(INTEL_HWCONFIG_MAX_VECS);
      HANDLE(INTEL_HWCONFIG_MAX_COPY_CS);
      HANDLE(INTEL_HWCONFIG_DEPRECATED_URB_SIZE_IN_KB);
      HANDLE(INTEL_HWCONFIG_MIN_VS_URB_ENTRIES);
      HANDLE(INTEL_HWCONFIG_MAX_VS_URB_ENTRIES);
      HANDLE(INTEL_HWCONFIG_MIN_PCS_URB_ENTRIES);
      HANDLE(INTEL_HWCONFIG_MAX_PCS_URB_ENTRIES);
      HANDLE(INTEL_HWCONFIG_MIN_HS_URB_ENTRIES);
      HANDLE(INTEL_HWCONFIG_MAX_HS_URB_ENTRIES);
      HANDLE(INTEL_HWCONFIG_MIN_GS_URB_ENTRIES);
      HANDLE(INTEL_HWCONFIG_MAX_GS_URB_ENTRIES);
      HANDLE(INTEL_HWCONFIG_MIN_DS_URB_ENTRIES);
      HANDLE(INTEL_HWCONFIG_MAX_DS_URB_ENTRIES);
      HANDLE(INTEL_HWCONFIG_PUSH_CONSTANT_URB_RESERVED_SIZE);
      HANDLE(INTEL_HWCONFIG_POCS_PUSH_CONSTANT_URB_RESERVED_SIZE);
      HANDLE(INTEL_HWCONFIG_URB_REGION_ALIGNMENT_SIZE_IN_BYTES);
      HANDLE(INTEL_HWCONFIG_URB_ALLOCATION_SIZE_UNITS_IN_BYTES);
      HANDLE(INTEL_HWCONFIG_MAX_URB_SIZE_CCS_IN_BYTES);
      HANDLE(INTEL_HWCONFIG_VS_MIN_DEREF_BLOCK_SIZE_HANDLE_COUNT);
      HANDLE(INTEL_HWCONFIG_DS_MIN_DEREF_BLOCK_SIZE_HANDLE_COUNT);
      HANDLE(INTEL_HWCONFIG_NUM_RT_STACKS_PER_DSS);
      HANDLE(INTEL_HWCONFIG_MAX_URB_STARTING_ADDRESS);
      HANDLE(INTEL_HWCONFIG_MIN_CS_URB_ENTRIES);
      HANDLE(INTEL_HWCONFIG_MAX_CS_URB_ENTRIES);
      HANDLE(INTEL_HWCONFIG_L3_ALLOC_PER_BANK_URB);
      HANDLE(INTEL_HWCONFIG_L3_ALLOC_PER_BANK_REST);
      HANDLE(INTEL_HWCONFIG_L3_ALLOC_PER_BANK_DC);
      HANDLE(INTEL_HWCONFIG_L3_ALLOC_PER_BANK_RO);
      HANDLE(INTEL_HWCONFIG_L3_ALLOC_PER_BANK_Z);
      HANDLE(INTEL_HWCONFIG_L3_ALLOC_PER_BANK_COLOR);
      HANDLE(INTEL_HWCONFIG_L3_ALLOC_PER_BANK_UNIFIED_TILE_CACHE);
      HANDLE(INTEL_HWCONFIG_L3_ALLOC_PER_BANK_COMMAND_BUFFER);
      HANDLE(INTEL_HWCONFIG_L3_ALLOC_PER_BANK_RW);
      HANDLE(INTEL_HWCONFIG_MAX_NUM_L3_CONFIGS);
      HANDLE(INTEL_HWCONFIG_BINDLESS_SURFACE_OFFSET_BIT_COUNT);
      HANDLE(INTEL_HWCONFIG_RESERVED_CCS_WAYS);
      HANDLE(INTEL_HWCONFIG_CSR_SIZE_IN_MB);
      HANDLE(INTEL_HWCONFIG_GEOMETRY_PIPES_PER_SLICE);
      HANDLE(INTEL_HWCONFIG_L3_BANK_SIZE_IN_KB);
      HANDLE(INTEL_HWCONFIG_SLM_SIZE_PER_DSS);
      HANDLE(INTEL_HWCONFIG_MAX_PIXEL_FILL_RATE_PER_SLICE);
      HANDLE(INTEL_HWCONFIG_MAX_PIXEL_FILL_RATE_PER_DSS);
      HANDLE(INTEL_HWCONFIG_URB_SIZE_PER_SLICE_IN_KB);
      HANDLE(INTEL_HWCONFIG_URB_SIZE_PER_L3_BANK_COUNT_IN_KB);
      HANDLE(INTEL_HWCONFIG_MAX_SUBSLICE);
      HANDLE(INTEL_HWCONFIG_MAX_EU_PER_SUBSLICE);
      HANDLE(INTEL_HWCONFIG_RAMBO_L3_BANK_SIZE_IN_KB);
      HANDLE(INTEL_HWCONFIG_SLM_SIZE_PER_SS_IN_KB);
      HANDLE(INTEL_HWCONFIG_NUM_HBM_STACKS_PER_TILE);
      HANDLE(INTEL_HWCONFIG_NUM_CHANNELS_PER_HBM_STACK);
      HANDLE(INTEL_HWCONFIG_HBM_CHANNEL_WIDTH_IN_BYTES);
      HANDLE(INTEL_HWCONFIG_MIN_TASK_URB_ENTRIES);
      HANDLE(INTEL_HWCONFIG_MAX_TASK_URB_ENTRIES);
      HANDLE(INTEL_HWCONFIG_MIN_MESH_URB_ENTRIES);
      HANDLE(INTEL_HWCONFIG_MAX_MESH_URB_ENTRIES);
      HANDLE(INTEL_HWCONFIG_MAX_GSC);
      HANDLE(INTEL_HWCONFIG_SYNC_NUM_RT_STACKS_PER_DSS);
      HANDLE(INTEL_HWCONFIG_NUM_XECU);
#undef HANDLE
   }
   return "UNKNOWN_INTEL_HWCONFIG";
}

typedef void (*hwconfig_item_cb)(struct intel_device_info *devinfo,
                                 const struct hwconfig *item);

static void
process_hwconfig_table(struct intel_device_info *devinfo,
                       const struct hwconfig *hwconfig,
                       int32_t hwconfig_len,
                       hwconfig_item_cb item_callback_func)
{
   assert(hwconfig);
   assert(hwconfig_len % 4 == 0);
   const struct hwconfig *current = hwconfig;
   const struct hwconfig *end =
      (struct hwconfig*)(((uint32_t*)hwconfig) + (hwconfig_len / 4));
   while (current < end) {
      assert(current + 1 < end);
      struct hwconfig *next =
         (struct hwconfig*)((uint32_t*)current + 2 + current->len);
      assert(next <= end);
      item_callback_func(devinfo, current);
      current = next;
   }
   assert(current == end);
}

bool
intel_hwconfig_is_required(const struct intel_device_info *devinfo)
{
   /* returns is true when the platform should apply hwconfig values */
   return devinfo->verx10 >= 125;
}

static bool
hwconfig_ignore_difference(const struct intel_device_info *devinfo,
                           const uint32_t key, uint32_t value)
{
   if (key == INTEL_HWCONFIG_TOTAL_GS_THREADS && value == 336 &&
       intel_needs_workaround(devinfo, 18040209780))
      return true;

   return false;
}

static inline void
hwconfig_item_warning(const struct intel_device_info *devinfo,
                      const char *devinfo_name, uint32_t devinfo_val,
                      const uint32_t hwconfig_key, uint32_t hwconfig_val)
{
   if (devinfo_val != hwconfig_val &&
       !hwconfig_ignore_difference(devinfo, hwconfig_key, hwconfig_val)) {
      printf("   %s (%u) != devinfo->%s (%u)\n", key_to_name(hwconfig_key),
             hwconfig_val, devinfo_name, devinfo_val);
   }
}

static inline bool
should_apply_hwconfig_item(uint16_t always_apply_verx10,
                           const struct intel_device_info *devinfo,
                           uint32_t devinfo_val)
{
   assert(intel_hwconfig_is_required(devinfo));
   if ((devinfo->verx10 >= always_apply_verx10 || devinfo_val == 0))
      return true;

   return false;
}

/* If apply_hwconfig(devinfo) is true, then we apply the
 * hwconfig value to the devinfo field, ``F``.
 *
 * For debug builds, if apply_hwconfig() is false, we will compare the
 * hwconfig value, ``V``, with the current value in ``F`` and log a warning
 * message if they differ. This should help to make sure the values in our
 * devinfo structures match what hwconfig is specified.
 *
 * If ``devinfo->verx10 >= CVER``, then the hwconfig value is always be used.
 * If ``devinfo->verx10 < CVER``, the hwconfig value is only used if
 * devinfo->F is 0.
 */
#define DEVINFO_HWCONFIG_KV(CVER, F, K, V)                              \
   do {                                                                 \
      if (check_only)                                                   \
         hwconfig_item_warning(devinfo, #F, devinfo->F, (K), (V));      \
      else if (should_apply_hwconfig_item((CVER), devinfo, devinfo->F)) \
         devinfo->F = (V);                                              \
   } while (0)

#define DEVINFO_HWCONFIG(CVER, F, I)                                    \
   DEVINFO_HWCONFIG_KV((CVER), F, (I)->key, (I)->val[0])

#define CALC_TOPOLOGY_LAYOUT_VERX10 300

static void
process_hwconfig_item(struct intel_device_info *devinfo,
                      const struct hwconfig *item,
                      const bool check_only)
{
   switch (item->key) {
   case INTEL_HWCONFIG_MAX_SLICES_SUPPORTED:
      /* if we are not applying hwconfig to max_slices and max_subslices_per_slice
       * it should be skipped at all, otherwise the upper limit values set in
       * xe_compute_topology() will cause hwconfig mismatch warnings in
       * some SKUs.
       */
      if (devinfo->verx10 < CALC_TOPOLOGY_LAYOUT_VERX10)
         break;

      DEVINFO_HWCONFIG(CALC_TOPOLOGY_LAYOUT_VERX10, max_slices, item);
      break;
   case INTEL_HWCONFIG_MAX_DUAL_SUBSLICES_SUPPORTED: /* available in Gfx 12.5 */
   case INTEL_HWCONFIG_MAX_SUBSLICE: /* available in Gfx 20+ */
      if (devinfo->verx10 < CALC_TOPOLOGY_LAYOUT_VERX10)
         break;

      /* This one is special because it depends on max_slices that is not
       * guarantee to be processed before this one
       */
      if (check_only) {
         hwconfig_item_warning(devinfo, "max_subslices_per_slice",
                               devinfo->max_subslices_per_slice, item->key,
                               item->val[0] / devinfo->max_slices);
      } else {
         /* it will be later adjusted in late_apply_hwconfig() */
         DEVINFO_HWCONFIG(CALC_TOPOLOGY_LAYOUT_VERX10,
                          max_subslices_per_slice, item);
      }
      break;
   case INTEL_HWCONFIG_MAX_NUM_EU_PER_DSS:
      DEVINFO_HWCONFIG(125, max_eus_per_subslice, item);
      break;
   case INTEL_HWCONFIG_NUM_THREADS_PER_EU:
      DEVINFO_HWCONFIG(125, num_thread_per_eu, item);
      break;
   case INTEL_HWCONFIG_TOTAL_VS_THREADS:
      DEVINFO_HWCONFIG(125, max_vs_threads, item);
      break;
   case INTEL_HWCONFIG_TOTAL_GS_THREADS:
      DEVINFO_HWCONFIG(125, max_gs_threads, item);
      break;
   case INTEL_HWCONFIG_TOTAL_HS_THREADS:
      DEVINFO_HWCONFIG(125, max_tcs_threads, item);
      break;
   case INTEL_HWCONFIG_TOTAL_DS_THREADS:
      DEVINFO_HWCONFIG(125, max_tes_threads, item);
      break;
   case INTEL_HWCONFIG_TOTAL_VS_THREADS_POCS:
      break; /* ignore */
   case INTEL_HWCONFIG_TOTAL_PS_THREADS: {
      unsigned threads = item->val[0];
      if (devinfo->ver == 12)
         threads /= 2;
      DEVINFO_HWCONFIG_KV(125, max_threads_per_psd, item->key, threads);
      break;
   }
   case INTEL_HWCONFIG_URB_SIZE_PER_SLICE_IN_KB:
      DEVINFO_HWCONFIG(125, urb.size, item);
      break;
   case INTEL_HWCONFIG_MAX_VS_URB_ENTRIES:
      DEVINFO_HWCONFIG(200, urb.max_entries[MESA_SHADER_VERTEX], item);
      break;
   case INTEL_HWCONFIG_MAX_HS_URB_ENTRIES:
      DEVINFO_HWCONFIG(200, urb.max_entries[MESA_SHADER_TESS_CTRL], item);
      break;
   case INTEL_HWCONFIG_MAX_GS_URB_ENTRIES:
      DEVINFO_HWCONFIG(200, urb.max_entries[MESA_SHADER_GEOMETRY], item);
      break;
   case INTEL_HWCONFIG_MAX_DS_URB_ENTRIES:
      DEVINFO_HWCONFIG(200, urb.max_entries[MESA_SHADER_TESS_EVAL], item);
      break;
   case INTEL_HWCONFIG_NUM_PIXEL_PIPES:
      DEVINFO_HWCONFIG_KV(200, num_color_pipes, item->key, item->val[0]);
      break;
   case INTEL_HWCONFIG_GEOMETRY_PIPES_PER_SLICE:
      DEVINFO_HWCONFIG_KV(200, num_geom_pipes, item->key, item->val[0]);
      break;
   default:
      break; /* ignore */
   }
}

static void
apply_hwconfig_item(struct intel_device_info *devinfo,
                    const struct hwconfig *item)
{
   process_hwconfig_item(devinfo, item, false);
}

static void
late_apply_hwconfig(struct intel_device_info *devinfo)
{
   if (devinfo->verx10 >= CALC_TOPOLOGY_LAYOUT_VERX10) {
      assert((devinfo->max_subslices_per_slice % devinfo->max_slices) == 0);
      devinfo->max_subslices_per_slice /= devinfo->max_slices;
   }

   /* Calculate total pipes count from stored values */
   if (devinfo->verx10 >= 200) {
      devinfo->num_color_pipes *= devinfo->max_slices;
      devinfo->num_depth_pipes =  devinfo->num_color_pipes;
      devinfo->num_geom_pipes  *= devinfo->max_slices;
   }
}

bool
intel_hwconfig_process_table(struct intel_device_info *devinfo,
                             void *data, int32_t len)
{
   if (intel_hwconfig_is_required(devinfo)) {
      process_hwconfig_table(devinfo, data, len, apply_hwconfig_item);
      late_apply_hwconfig(devinfo);
   }

   return true;
}

static void
print_hwconfig_item(struct intel_device_info *devinfo,
                    const struct hwconfig *item)
{
   printf("%s: ", key_to_name(item->key));
   for (int i = 0; i < item->len; i++)
      printf(i ? ", 0x%x (%d)" : "0x%x (%d)", item->val[i],
              item->val[i]);
   printf("\n");
}

static void
intel_print_hwconfig_table(const struct hwconfig *hwconfig,
                           int32_t hwconfig_len)
{
   process_hwconfig_table(NULL, hwconfig, hwconfig_len, print_hwconfig_item);
}

static struct hwconfig *
intel_get_hwconfig_table(int fd, struct intel_device_info *devinfo,
                         int32_t *hwconfig_len)
{
   switch (devinfo->kmd_type) {
   case INTEL_KMD_TYPE_I915:
      return intel_device_info_i915_query_hwconfig(fd, hwconfig_len);
   case INTEL_KMD_TYPE_XE:
      return intel_device_info_xe_query_hwconfig(fd, hwconfig_len);
   default:
      UNREACHABLE("unknown kmd type");
      return NULL;
   }
}

void
intel_get_and_print_hwconfig_table(int fd, struct intel_device_info *devinfo)
{
   struct hwconfig *hwconfig;
   int32_t hwconfig_len = 0;

   hwconfig = intel_get_hwconfig_table(fd, devinfo, &hwconfig_len);
   if (hwconfig) {
      intel_print_hwconfig_table(hwconfig, hwconfig_len);
      free(hwconfig);
   }
}

UNUSED static void
check_hwconfig_item(struct intel_device_info *devinfo,
                    const struct hwconfig *item)
{
   process_hwconfig_item(devinfo, item, true);
}

void
intel_check_hwconfig_items(int fd, struct intel_device_info *devinfo)
{
   struct hwconfig *data;
   int32_t len = 0;

   data = intel_get_hwconfig_table(fd, devinfo, &len);
   if (data) {
      process_hwconfig_table(devinfo, data, len, check_hwconfig_item);
      free(data);
   }
}
