/*
 * Copyright © 2025 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef GE8300_H
#define GE8300_H

#include <stdbool.h>

#include "pvr_device_info.h"

#define PVR_DEVICE_IDENT_22_V_54_30 \
   .device_id = 0x22054030, .series_name = "Rogue", \
   .public_name = "GE8300", .arch = PVR_DEVICE_ARCH_ROGUE

static const struct pvr_device_features pvr_device_features_22_V_54_30 = {
   .has_astc = true,
   .has_common_store_size_in_dwords = true,
   .has_compute = true,
   .has_compute_overlap = true,
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
   .has_pbe2_in_xe = true,
   .has_pbe_filterable_f16 = true,
   .has_pbe_yuv = true,
   .has_roguexe = true,
   .has_simple_internal_parameter_format = true,
   .has_simple_internal_parameter_format_v1 = true,
   .has_simple_parameter_format_version = true,
   .has_slc_cache_line_size_bits = true,
   .has_tile_size_16x16 = true,
   .has_tile_size_x = true,
   .has_tile_size_y = true,
   .has_tpu_parallel_instances = true,
   .has_unified_store_depth = true,
   .has_usc_f16sop_u8 = true,
   .has_usc_itrsmp = true,
   .has_usc_itrsmp_enhanced = true,
   .has_usc_itr_parallel_instances = true,
   .has_usc_min_output_registers_per_pix = true,
   .has_usc_slots = true,
   .has_uvs_banks = true,
   .has_uvs_pba_entries = true,
   .has_uvs_vtx_entries = true,
   .has_vdm_cam_size = true,

   .common_store_size_in_dwords = 1024U * 4U * 4U,
   .isp_max_tiles_in_flight = 4U,
   .isp_samples_per_pixel = 1U,
   .max_instances_per_pds_task = 32U,
   .max_multisample = 4U,
   .max_partitions = 10U,
   .max_usc_tasks = 104U,
   .num_clusters = 1U,
   .num_raster_pipes = 1U,
   .simple_parameter_format_version = 1U,
   .slc_cache_line_size_bits = 512U,
   .tile_size_x = 16U,
   .tile_size_y = 16U,
   .tpu_parallel_instances = 4U,
   .unified_store_depth = 208U,
   .usc_itr_parallel_instances = 16U,
   .usc_min_output_registers_per_pix = 2U,
   .usc_slots = 64U,
   .uvs_banks = 4U,
   .uvs_pba_entries = 320U,
   .uvs_vtx_entries = 288U,
   .vdm_cam_size = 64U,

   .has_s8xe = true,
};

static const struct pvr_device_enhancements
   pvr_device_enhancements_22_67_54_30 = {
      .has_ern35421 = true,
      .has_ern38748 = true,
      .has_ern42307 = true,
   };

static const struct pvr_device_quirks pvr_device_quirks_22_67_54_30 = {
   .has_brn49927 = true,
   .has_brn66011 = true,
   .has_brn70165 = true,
   .has_brn74056 = true,
};

static const struct pvr_device_info pvr_device_info_22_67_54_30 = {
   .ident = {
      PVR_DEVICE_IDENT_22_V_54_30,
      .b = 22,
      .v = 67,
      .n = 54,
      .c = 30,
   },
   .features = pvr_device_features_22_V_54_30,
   .enhancements = pvr_device_enhancements_22_67_54_30,
   .quirks = pvr_device_quirks_22_67_54_30,
};

static const struct pvr_device_enhancements
   pvr_device_enhancements_22_68_54_30 = {
      .has_ern35421 = true,
      .has_ern38748 = true,
      .has_ern42307 = true,
   };

static const struct pvr_device_quirks pvr_device_quirks_22_68_54_30 = {
   .has_brn49927 = true,
   .has_brn66011 = true,
   .has_brn70165 = true,
   .has_brn74056 = true,
};

static const struct pvr_device_info pvr_device_info_22_68_54_30 = {
   .ident = {
      PVR_DEVICE_IDENT_22_V_54_30,
      .b = 22,
      .v = 68,
      .n = 54,
      .c = 30,
   },
   .features = pvr_device_features_22_V_54_30,
   .enhancements = pvr_device_enhancements_22_68_54_30,
   .quirks = pvr_device_quirks_22_68_54_30,
};

#define PVR_DEVICE_IDENT_22_V_54_38 \
   .device_id = 0x22054038, .series_name = "Rogue", .public_name = "GE8300"

static const struct pvr_device_features pvr_device_features_22_V_54_38 = {
   .has_astc = true,
   .has_common_store_size_in_dwords = true,
   .has_compute = true,
   .has_compute_overlap = true,
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
   .has_pbe2_in_xe = true,
   .has_pbe_filterable_f16 = true,
   .has_pbe_yuv = true,
   .has_roguexe = true,
   .has_simple_internal_parameter_format = true,
   .has_simple_internal_parameter_format_v1 = true,
   .has_simple_parameter_format_version = true,
   .has_slc_cache_line_size_bits = true,
   .has_tile_size_16x16 = true,
   .has_tile_size_x = true,
   .has_tile_size_y = true,
   .has_tpu_parallel_instances = true,
   .has_unified_store_depth = true,
   .has_usc_f16sop_u8 = true,
   .has_usc_itrsmp = true,
   .has_usc_itrsmp_enhanced = true,
   .has_usc_min_output_registers_per_pix = true,
   .has_usc_slots = true,
   .has_uvs_banks = true,
   .has_uvs_pba_entries = true,
   .has_uvs_vtx_entries = true,
   .has_vdm_cam_size = true,

   .common_store_size_in_dwords = 1024U * 4U * 4U,
   .isp_max_tiles_in_flight = 4U,
   .isp_samples_per_pixel = 1U,
   .max_instances_per_pds_task = 32U,
   .max_multisample = 4U,
   .max_partitions = 10U,
   .max_usc_tasks = 104U,
   .num_clusters = 1U,
   .num_raster_pipes = 1U,
   .simple_parameter_format_version = 1U,
   .slc_cache_line_size_bits = 512U,
   .tile_size_x = 16U,
   .tile_size_y = 16U,
   .tpu_parallel_instances = 4U,
   .unified_store_depth = 208U,
   .usc_min_output_registers_per_pix = 2U,
   .usc_slots = 64U,
   .uvs_banks = 4U,
   .uvs_pba_entries = 320U,
   .uvs_vtx_entries = 288U,
   .vdm_cam_size = 64U,

   .has_s8xe = true,
   .has_usc_itr_parallel_instances = true,

   .usc_itr_parallel_instances = 16U,
};

static const struct pvr_device_enhancements
   pvr_device_enhancements_22_102_54_38 = {
      .has_ern35421 = true,
      .has_ern38748 = true,
      .has_ern42307 = true,
   };

static const struct pvr_device_quirks pvr_device_quirks_22_102_54_38 = {
   .has_brn49927 = true,
   .has_brn66011 = true,
   .has_brn70165 = true,
   .has_brn74056 = true,
};

static const struct pvr_device_info pvr_device_info_22_102_54_38 = {
   .ident = {
      PVR_DEVICE_IDENT_22_V_54_38,
      .b = 22,
      .v = 102,
      .n = 54,
      .c = 38,
   },
   .features = pvr_device_features_22_V_54_38,
   .enhancements = pvr_device_enhancements_22_102_54_38,
   .quirks = pvr_device_quirks_22_102_54_38,
};

#endif /* GE8300_H */
