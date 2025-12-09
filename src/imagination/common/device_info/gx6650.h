/*
 * Copyright © 2025 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef GX6650_H
#define GX6650_H

#include <stdbool.h>

#include "pvr_device_info.h"

#define PVR_DEVICE_IDENT_4_V_6_62 \
   .device_id = 0x6650, .series_name = "Rogue", \
   .public_name = "GX6650", .arch = PVR_DEVICE_ARCH_ROGUE

static const struct pvr_device_features pvr_device_features_4_V_6_62 = {
   .has_astc = true,
   .has_cluster_grouping = true,
   .has_common_store_size_in_dwords = true,
   .has_compute = true,
   .has_compute_morton_capable = true,
   .has_compute_overlap = true,
   .has_eight_output_registers = true,
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
   .has_pbe_yuv = true,
   .has_slc_cache_line_size_bits = true,
   .has_slc_mcu_cache_controls = true,
   .has_tf_bicubic_filter = true,
   .has_tile_size_x = true,
   .has_tile_size_y = true,
   .has_tpu_array_textures = true,
   .has_tpu_extended_integer_lookup = true,
   .has_tpu_image_state_v2 = true,
   .has_tpu_parallel_instances = true,
   .has_unified_store_depth = true,
   .has_usc_f16sop_u8 = true,
   .has_usc_itrsmp = true,
   .has_usc_itr_parallel_instances = true,
   .has_usc_min_output_registers_per_pix = true,
   .has_usc_slots = true,
   .has_uvs_banks = true,
   .has_uvs_pba_entries = true,
   .has_uvs_vtx_entries = true,
   .has_vdm_cam_size = true,
   .has_xt_top_infrastructure = true,
   .has_zls_subtile = true,

   .common_store_size_in_dwords = 1280U * 4U * 4U,
   .fbcdc_algorithm = 2U,
   .isp_max_tiles_in_flight = 8U,
   .isp_samples_per_pixel = 2U,
   .max_instances_per_pds_task = 32U,
   .max_multisample = 8U,
   .max_partitions = 8U,
   .max_usc_tasks = 56U,
   .num_clusters = 6U,
   .num_raster_pipes = 2U,
   .slc_cache_line_size_bits = 512U,
   .tile_size_x = 32U,
   .tile_size_y = 32U,
   .tpu_parallel_instances = 4U,
   .unified_store_depth = 256U,
   .usc_itr_parallel_instances = 8U,
   .usc_min_output_registers_per_pix = 2U,
   .usc_slots = 32U,
   .uvs_banks = 8U,
   .uvs_pba_entries = 320U,
   .uvs_vtx_entries = 288U,
   .vdm_cam_size = 256U,

   .has_requires_fb_cdc_zls_setup = true,
};

static const struct pvr_device_enhancements pvr_device_enhancements_4_46_6_62 = {
   .has_ern35421 = true,
   .has_ern38020 = true,
   .has_ern38748 = true,
   .has_ern42064 = true,
   .has_ern42307 = true,
   .has_ern45493 = true,
};

static const struct pvr_device_quirks pvr_device_quirks_4_46_6_62 = {
   .has_brn44079 = true,
   .has_brn49927 = true,
   .has_brn51025 = true,
   .has_brn51210 = true,
   .has_brn51764 = true,
   .has_brn52354 = true,
   .has_brn52942 = true,
   .has_brn58839 = true,
   .has_brn62269 = true,
   .has_brn66011 = true,
   .has_brn70165 = true,
   .has_brn74056 = true,
};

static const struct pvr_device_info pvr_device_info_4_46_6_62 = {
   .ident = {
      PVR_DEVICE_IDENT_4_V_6_62,
      .b = 4,
      .v = 46,
      .n = 6,
      .c = 62,
   },
   .features = pvr_device_features_4_V_6_62,
   .enhancements = pvr_device_enhancements_4_46_6_62,
   .quirks = pvr_device_quirks_4_46_6_62,
};

#endif /* GX6650_H */
