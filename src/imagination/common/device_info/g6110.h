/*
 * Copyright © 2025 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef G6110_H
#define G6110_H

#include <stdbool.h>

#include "pvr_device_info.h"

#define PVR_DEVICE_IDENT_5_V_1_46 \
   .device_id = 0x6110, .series_name = "Rogue", \
   .public_name = "GX6110", .arch = PVR_DEVICE_ARCH_ROGUE

static const struct pvr_device_features pvr_device_features_5_V_1_46 = {
   .has_common_store_size_in_dwords = true,
   .has_compute = true,
   .has_fbcdc_algorithm = true,
   /* .has_gs_rta_support = true, */
   .has_isp_max_tiles_in_flight = true,
   .has_isp_samples_per_pixel = true,
   .has_max_instances_per_pds_task = true,
   .has_max_multisample = true,
   .has_max_partitions = true,
   .has_max_usc_tasks = true,
   .has_num_clusters = true,
   .has_num_raster_pipes = true,
   .has_pbe_filterable_f16 = true,
   .has_roguexe = true,
   .has_slc_cache_line_size_bits = true,
   .has_tile_size_x = true,
   .has_tile_size_y = true,
   .has_tpu_parallel_instances = true,
   .has_unified_store_depth = true,
   .has_usc_itr_parallel_instances = true,
   .has_usc_min_output_registers_per_pix = true,
   .has_usc_slots = true,
   .has_uvs_banks = true,
   .has_uvs_pba_entries = true,
   .has_uvs_vtx_entries = true,
   .has_vdm_cam_size = true,

   .common_store_size_in_dwords = 1344U * 4U * 4U,
   .fbcdc_algorithm = 2U,
   .isp_max_tiles_in_flight = 1U,
   .isp_samples_per_pixel = 2U,
   .max_instances_per_pds_task = 32U,
   .max_multisample = 4U,
   .max_partitions = 12U,
   .max_usc_tasks = 96U,
   .num_clusters = 1U,
   .num_raster_pipes = 1U,
   .slc_cache_line_size_bits = 512U,
   .tile_size_x = 32U,
   .tile_size_y = 32U,
   .tpu_parallel_instances = 4U,
   .unified_store_depth = 208U,
   .usc_itr_parallel_instances = 16U,
   .usc_min_output_registers_per_pix = 2U,
   .usc_slots = 56U,
   .uvs_banks = 8U,
   .uvs_pba_entries = 160U,
   .uvs_vtx_entries = 144U,
   .vdm_cam_size = 64U,

   .has_requires_fb_cdc_zls_setup = true,
};

static const struct pvr_device_enhancements pvr_device_enhancements_5_9_1_46 = {
   .has_ern35421 = true,
};

static const struct pvr_device_quirks pvr_device_quirks_5_9_1_46 = {
   .has_brn44079 = true,
   .has_brn47727 = true,
   .has_brn48492 = true,
   .has_brn48545 = true,
   .has_brn49032 = true,
   .has_brn49927 = true,
   .has_brn51025 = true,
   .has_brn51210 = true,
   .has_brn52942 = true,
   .has_brn58839 = true,
   .has_brn62269 = true,
   .has_brn66011 = true,
   .has_brn70165 = true,
   .has_brn74056 = true,
};

static const struct pvr_device_info pvr_device_info_5_9_1_46 = {
   .ident = {
      PVR_DEVICE_IDENT_5_V_1_46,
      .b = 5,
      .v = 9,
      .n = 1,
      .c = 46,
   },
   .features = pvr_device_features_5_V_1_46,
   .enhancements = pvr_device_enhancements_5_9_1_46,
   .quirks = pvr_device_quirks_5_9_1_46,
};

#endif /* G6110_H */
