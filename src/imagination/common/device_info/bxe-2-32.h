/*
 * Copyright © 2025 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef BXE_2_32_H
#define BXE_2_32_H

#include <stdbool.h>

#include "pvr_device_info.h"

#define PVR_DEVICE_IDENT_36_V_52_182 \
   .device_id = 0x36052182, .series_name = "B-Series", \
   .public_name = "BXE-2-32", .arch = PVR_DEVICE_ARCH_ROGUE

static const struct pvr_device_features pvr_device_features_36_V_52_182 = {
   .has_astc = true,
   .has_common_store_size_in_dwords = true,
   .has_compute = true,
   .has_compute_overlap = true,
   .has_fbcdc_algorithm = true,
   .has_gpu_multicore_support = true,
   /* .has_gs_rta_support = true, */
   .has_ipf_creq_pf = true,
   .has_isp_max_tiles_in_flight = true,
   .has_isp_samples_per_pixel = true,
   .has_max_instances_per_pds_task = true,
   .has_max_multisample = true,
   .has_max_partitions = true,
   .has_max_usc_tasks = true,
   .has_num_clusters = true,
   .has_num_raster_pipes = true,
   .has_paired_tiles = true,
   .has_pbe2_in_xe = true,
   .has_pbe_filterable_f16 = true,
   .has_pbe_yuv = true,
   .has_pds_ddmadt = true,
   .has_roguexe = true,
   .has_screen_size8K = true,
   .has_simple_internal_parameter_format = true,
   .has_simple_internal_parameter_format_v2 = true,
   .has_simple_parameter_format_version = true,
   .has_slc_cache_line_size_bits = true,
   .has_tile_size_16x16 = true,
   .has_tile_size_x = true,
   .has_tile_size_y = true,
   .has_tpu_border_colour_enhanced = true,
   .has_tpu_extended_integer_lookup = true,
   .has_tpu_image_state_v2 = true,
   .has_tpu_parallel_instances = true,
   .has_unified_store_depth = true,
   .has_usc_f16sop_u8 = true,
   .has_usc_itrsmp = true,
   .has_usc_itrsmp_enhanced = true,
   .has_usc_itr_parallel_instances = true,

   .has_usc_min_output_registers_per_pix = true,
   .has_usc_pixel_partition_mask = true,
   .has_usc_slots = true,
   .has_uvs_banks = true,
   .has_uvs_pba_entries = true,
   .has_uvs_vtx_entries = true,
   .has_vdm_cam_size = true,
   .has_xpu_max_slaves = true,

   .common_store_size_in_dwords = 832U * 4U * 4U,
   .fbcdc_algorithm = 50U,
   .isp_max_tiles_in_flight = 2U,
   .isp_samples_per_pixel = 1U,
   .max_instances_per_pds_task = 32U,
   .max_multisample = 4U,
   .max_partitions = 8U,
   .max_usc_tasks = 72U,
   .num_clusters = 1U,
   .num_raster_pipes = 1U,
   .simple_parameter_format_version = 2U,
   .slc_cache_line_size_bits = 512U,
   .tile_size_x = 16U,
   .tile_size_y = 16U,
   .tpu_parallel_instances = 2U,
   .unified_store_depth = 128U,
   .usc_itr_parallel_instances = 8U,
   .usc_min_output_registers_per_pix = 2U,
   .usc_slots = 32U,
   .uvs_banks = 4U,
   .uvs_pba_entries = 320U,
   .uvs_vtx_entries = 288U,
   .vdm_cam_size = 64U,
   .xpu_max_slaves = 3U,

   .has_s8xe = true,
};

static const struct pvr_device_enhancements
   pvr_device_enhancements_36_29_52_182 = {
      .has_ern35421 = true,
      .has_ern38748 = true,
      .has_ern42307 = true,
      .has_ern45493 = true,
   };

static const struct pvr_device_quirks pvr_device_quirks_36_29_52_182 = {
   .has_brn70165 = true,
   .has_brn72168 = true,
   .has_brn72463 = true,
   .has_brn74056 = true,
};

static const struct pvr_device_info pvr_device_info_36_29_52_182 = {
   .ident = {
      PVR_DEVICE_IDENT_36_V_52_182,
      .b = 36,
      .v = 29,
      .n = 52,
      .c = 182,
   },
   .features = pvr_device_features_36_V_52_182,
   .enhancements = pvr_device_enhancements_36_29_52_182,
   .quirks = pvr_device_quirks_36_29_52_182,
};

#endif /* BXE_2_32_H */
