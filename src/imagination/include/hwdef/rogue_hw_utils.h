/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* This file is based on rgxdefs.h and should only contain function-like macros
 * and inline functions. Any object-like macros should instead appear in
 * rogue_hw_defs.h.
 */

#ifndef ROGUE_HW_UTILS_H
#define ROGUE_HW_UTILS_H

#include <stdint.h>

#include "pvr_winsys.h"

#define __pvr_address_type pvr_dev_addr_t
#define __pvr_get_address(pvr_dev_addr) (pvr_dev_addr).addr

#include "csbgen/rogue_cdm.h"
#include "csbgen/rogue_lls.h"

#undef __pvr_get_address
#undef __pvr_address_type

#include "rogue_hw_defs.h"
#include "pvr_device_info.h"
#include "util/compiler.h"
#include "util/macros.h"

static inline void
rogue_get_isp_samples_per_tile_xy(const struct pvr_device_info *dev_info,
                                  uint32_t samples,
                                  uint32_t *const x_out,
                                  uint32_t *const y_out)
{
   const uint32_t tile_size_x =
      PVR_GET_FEATURE_VALUE(dev_info, tile_size_x, 0U);
   const uint32_t tile_size_y =
      PVR_GET_FEATURE_VALUE(dev_info, tile_size_y, 0U);
   const uint32_t samples_per_pixel =
      PVR_GET_FEATURE_VALUE(dev_info, isp_samples_per_pixel, 0U);

#if !defined(NDEBUG)
   switch (samples_per_pixel) {
   case 1:
   case 2:
   case 4:
      break;
   default:
      assert(!"Unsupported ISP samples per pixel");
   }
#endif

   *x_out = tile_size_x;
   *y_out = tile_size_y;

   switch (samples) {
   case 1:
      break;
   case 2:
      if (samples_per_pixel == 2 || samples_per_pixel == 4)
         *y_out *= 2;

      break;
   case 4:
      if (samples_per_pixel == 2 || samples_per_pixel == 4)
         *x_out *= 2;

      if (samples_per_pixel == 2)
         *y_out *= 2;

      break;
   case 8:
      *y_out *= 2;
      break;
   default:
      assert(!"Unsupported number of samples");
   }
}

static inline uint64_t
rogue_get_min_free_list_size(const struct pvr_device_info *dev_info)
{
   uint64_t min_num_pages;

   if (PVR_HAS_FEATURE(dev_info, roguexe)) {
      if (PVR_HAS_QUIRK(dev_info, 66011))
         min_num_pages = 40U;
      else
         min_num_pages = 25U;
   } else {
      min_num_pages = 50U;
   }

   return min_num_pages << ROGUE_BIF_PM_PHYSICAL_PAGE_SHIFT;
}

static inline uint32_t
rogue_get_max_num_vdm_pds_tasks(const struct pvr_device_info *dev_info)
{
   /* Default value based on the minimum value found in all existing cores. */
   uint32_t max_usc_tasks = PVR_GET_FEATURE_VALUE(dev_info, max_usc_tasks, 24U);

   /* FIXME: Where does the 9 come from? */
   return max_usc_tasks - 9;
}

static inline uint32_t
rogue_get_max_output_regs_per_pixel(const struct pvr_device_info *dev_info)
{
   if (PVR_HAS_FEATURE(dev_info, eight_output_registers))
      return 8U;

   return 4U;
}

static inline void
rogue_get_num_macrotiles_xy(const struct pvr_device_info *dev_info,
                            uint32_t *const x_out,
                            uint32_t *const y_out)
{
   uint32_t version;

   if (PVR_FEATURE_VALUE(dev_info, simple_parameter_format_version, &version))
      version = 0;

   if (!PVR_HAS_FEATURE(dev_info, simple_internal_parameter_format) ||
       version == 2) {
      *x_out = 4;
      *y_out = 4;
   } else {
      *x_out = 1;
      *y_out = 1;
   }
}

static inline uint32_t
rogue_get_macrotile_array_size(const struct pvr_device_info *dev_info)
{
   uint32_t num_macrotiles_x;
   uint32_t num_macrotiles_y;

   if (PVR_HAS_FEATURE(dev_info, simple_internal_parameter_format))
      return 0;

   rogue_get_num_macrotiles_xy(dev_info, &num_macrotiles_x, &num_macrotiles_y);

   return num_macrotiles_x * num_macrotiles_y * 8U;
}

/* To get the number of required Bernado/Phantom(s), divide the number of
 * clusters by 4 and round up.
 */
static inline uint32_t
rogue_get_num_phantoms(const struct pvr_device_info *dev_info)
{
   return DIV_ROUND_UP(PVR_GET_FEATURE_VALUE(dev_info, num_clusters, 1U), 4U);
}

/* Region header size in bytes. */
static inline uint32_t
rogue_get_region_header_size(const struct pvr_device_info *dev_info)
{
   uint32_t version;

   if (PVR_FEATURE_VALUE(dev_info, simple_parameter_format_version, &version))
      version = 0;

   if (PVR_HAS_FEATURE(dev_info, simple_internal_parameter_format) &&
       version == 2) {
      return 6;
   }

   return 5;
}

/* Return the total reserved size of partition in dwords. */
static inline uint32_t
rogue_get_total_reserved_partition_size(const struct pvr_device_info *dev_info)
{
   uint32_t tile_size_x = PVR_GET_FEATURE_VALUE(dev_info, tile_size_x, 0);
   uint32_t tile_size_y = PVR_GET_FEATURE_VALUE(dev_info, tile_size_y, 0);
   uint32_t max_partitions = PVR_GET_FEATURE_VALUE(dev_info, max_partitions, 0);

   if (tile_size_x == 16 && tile_size_y == 16) {
      return tile_size_x * tile_size_y * max_partitions *
             PVR_GET_FEATURE_VALUE(dev_info,
                                   usc_min_output_registers_per_pix,
                                   0);
   }

   return max_partitions * 1024U;
}

static inline uint32_t
rogue_get_render_size_max(const struct pvr_device_info *dev_info)
{
   if (PVR_HAS_FEATURE(dev_info, simple_internal_parameter_format))
      if (!PVR_HAS_FEATURE(dev_info, screen_size8K))
         return 4096U;

   return 8192U;
}

#define rogue_get_render_size_max_x(dev_info) \
   rogue_get_render_size_max(dev_info)

#define rogue_get_render_size_max_y(dev_info) \
   rogue_get_render_size_max(dev_info)

static inline uint32_t
rogue_get_slc_cache_line_size(const struct pvr_device_info *dev_info)
{
   return PVR_GET_FEATURE_VALUE(dev_info, slc_cache_line_size_bits, 8U) / 8U;
}

static inline uint32_t pvr_get_max_user_vertex_output_components(
   const struct pvr_device_info *dev_info)
{
   const uint32_t uvs_pba_entries =
      PVR_GET_FEATURE_VALUE(dev_info, uvs_pba_entries, 0U);
   const uint32_t uvs_banks = PVR_GET_FEATURE_VALUE(dev_info, uvs_banks, 0U);

   if (uvs_banks <= 8U && uvs_pba_entries == 160U)
      return 64U;

   return 128U;
}

static inline uint32_t
rogue_get_reserved_shared_size(const struct pvr_device_info *dev_info)
{
   uint32_t common_store_size_in_dwords =
      PVR_GET_FEATURE_VALUE(dev_info,
                            common_store_size_in_dwords,
                            512U * 4U * 4U);
   uint32_t reserved_shared_size =
      common_store_size_in_dwords - (256U * 4U) -
      rogue_get_total_reserved_partition_size(dev_info);

   if (PVR_HAS_QUIRK(dev_info, 44079)) {
      uint32_t common_store_split_point = (768U * 4U * 4U);

      return MIN2(common_store_split_point - (256U * 4U), reserved_shared_size);
   }

   return reserved_shared_size;
}

static inline uint32_t
rogue_max_compute_shared_registers(const struct pvr_device_info *dev_info)
{
   if (PVR_HAS_FEATURE(dev_info, compute))
      return 2U * 1024U;

   return 0U;
}

static inline uint32_t
rogue_get_max_coeffs(const struct pvr_device_info *dev_info)
{
   uint32_t max_coeff_additional_portion = ROGUE_MAX_VERTEX_SHARED_REGISTERS;
   uint32_t pending_allocation_shared_regs = 2U * 1024U;
   uint32_t pending_allocation_coeff_regs = 0U;
   uint32_t num_phantoms = rogue_get_num_phantoms(dev_info);
   uint32_t tiles_in_flight =
      PVR_GET_FEATURE_VALUE(dev_info, isp_max_tiles_in_flight, 0);
   uint32_t max_coeff_pixel_portion =
      DIV_ROUND_UP(tiles_in_flight, num_phantoms);

   max_coeff_pixel_portion *= ROGUE_MAX_PIXEL_SHARED_REGISTERS;

   /* Compute tasks on cores with BRN48492 and without compute overlap may lock
    * up without two additional lines of coeffs.
    */
   if (PVR_HAS_QUIRK(dev_info, 48492) &&
       !PVR_HAS_FEATURE(dev_info, compute_overlap)) {
      pending_allocation_coeff_regs = 2U * 1024U;
   }

   if (PVR_HAS_ERN(dev_info, 38748))
      pending_allocation_shared_regs = 0U;

   if (PVR_HAS_ERN(dev_info, 38020)) {
      max_coeff_additional_portion +=
         rogue_max_compute_shared_registers(dev_info);
   }

   return rogue_get_reserved_shared_size(dev_info) +
          pending_allocation_coeff_regs -
          (max_coeff_pixel_portion + max_coeff_additional_portion +
           pending_allocation_shared_regs);
}

static inline uint32_t
rogue_get_cdm_context_resume_buffer_size(const struct pvr_device_info *dev_info)
{
   if (PVR_HAS_FEATURE(dev_info, gpu_multicore_support)) {
      const uint32_t max_num_cores =
         PVR_GET_FEATURE_VALUE(dev_info, xpu_max_slaves, 0U) + 1U;
      const uint32_t cache_line_size = rogue_get_slc_cache_line_size(dev_info);
      const uint32_t cdm_context_resume_buffer_stride =
         ALIGN_POT(ROGUE_LLS_CDM_CONTEXT_RESUME_BUFFER_SIZE, cache_line_size);

      return cdm_context_resume_buffer_stride * max_num_cores;
   }

   return ROGUE_LLS_CDM_CONTEXT_RESUME_BUFFER_SIZE;
}

static inline uint32_t rogue_get_cdm_context_resume_buffer_alignment(
   const struct pvr_device_info *dev_info)
{
   if (PVR_HAS_FEATURE(dev_info, gpu_multicore_support))
      return rogue_get_slc_cache_line_size(dev_info);

   return ROGUE_LLS_CDM_CONTEXT_RESUME_BUFFER_ALIGNMENT;
}

static inline uint32_t
rogue_get_cdm_max_local_mem_size_regs(const struct pvr_device_info *dev_info)
{
   uint32_t available_coeffs_in_dwords = rogue_get_max_coeffs(dev_info);

   if (PVR_HAS_QUIRK(dev_info, 48492) && PVR_HAS_FEATURE(dev_info, roguexe) &&
       !PVR_HAS_FEATURE(dev_info, compute_overlap)) {
      /* Driver must not use the 2 reserved lines. */
      available_coeffs_in_dwords -= ROGUE_CSRM_LINE_SIZE_IN_DWORDS * 2;
   }

   /* The maximum amount of local memory available to a kernel is the minimum
    * of the total number of coefficient registers available and the max common
    * store allocation size which can be made by the CDM.
    *
    * If any coeff lines are reserved for tessellation or pixel then we need to
    * subtract those too.
    */
   return MIN2(available_coeffs_in_dwords,
               ROGUE_MAX_PER_KERNEL_LOCAL_MEM_SIZE_REGS);
}

static inline uint32_t
rogue_get_compute_max_work_group_size(const struct pvr_device_info *dev_info)
{
   /* The number of tasks which can be executed per USC - Limited to 16U by the
    * CDM.
    */
   const uint32_t max_tasks_per_usc = 16U;

   if (!PVR_HAS_ERN(dev_info, 35421)) {
      /* Barriers on work-groups > 32 instances aren't supported. */
      return ROGUE_MAX_INSTANCES_PER_TASK;
   }

   return ROGUE_MAX_INSTANCES_PER_TASK * max_tasks_per_usc;
}

#endif /* ROGUE_HW_UTILS_H */
