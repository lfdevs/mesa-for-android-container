/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "dev/intel_device_info.h"
#include "isl/isl.h"
#include "isl/isl_priv.h"

/* An assert that works regardless of NDEBUG. */
#define t_assert(cond) \
   do { \
      if (!(cond)) { \
         fprintf(stderr, "%s:%d: assertion failed\n", __FILE__, __LINE__); \
         abort(); \
      } \
   } while (0)

/* The bare minimum amount of data that should be in the surface */
static uint64_t
surf_data_size(const struct isl_surf *surf, int level)
{
   return (isl_format_get_layout(surf->format)->bpb / 8) *
      isl_minify(surf->logical_level0_px.w, level) *
      isl_minify(surf->logical_level0_px.h, level);
}

static void
test_2d_r8g8b8a8_unorm_16x16x1_levels01_array01_samples01_noaux(const char *platform,
                                                                uint32_t tiling_flags)
{
   struct intel_device_info devinfo;
   t_assert(intel_get_device_info_from_pci_id(
               intel_device_name_to_pci_device_id(platform), &devinfo));

   struct isl_device dev;
   isl_device_init(&dev, &devinfo);

   struct isl_surf surf;
   bool ok =
      isl_surf_init(&dev, &surf,
                    .dim = ISL_SURF_DIM_2D,
                    .format = ISL_FORMAT_R8G8B8A8_UNORM,
                    .width = 16,
                    .height = 16,
                    .depth = 1,
                    .levels = 1,
                    .array_len = 1,
                    .samples = 1,
                    .usage = ISL_SURF_USAGE_TEXTURE_BIT |
                             ISL_SURF_USAGE_DISABLE_AUX_BIT,
                    .tiling_flags = tiling_flags);
   t_assert(ok);

   uint64_t start_tile_B, end_tile_B;
   isl_surf_get_image_range_B_tile(&surf, 0, 0, 0, &start_tile_B, &end_tile_B);

   t_assert(end_tile_B - start_tile_B <= surf.size_B);
   t_assert(end_tile_B - start_tile_B >= surf_data_size(&surf, 0));
}

static void
test_2d_r8g8b8a8_unorm_256x256x1_levels05_array01_samples01_noaux(const char *platform,
                                                                  uint32_t tiling_flags)
{
   struct intel_device_info devinfo;
   t_assert(intel_get_device_info_from_pci_id(
               intel_device_name_to_pci_device_id(platform), &devinfo));

   struct isl_device dev;
   isl_device_init(&dev, &devinfo);

   struct isl_surf surf;
   bool ok =
      isl_surf_init(&dev, &surf,
                    .dim = ISL_SURF_DIM_2D,
                    .format = ISL_FORMAT_R8G8B8A8_UNORM,
                    .width = 256,
                    .height = 256,
                    .depth = 1,
                    .levels = 5,
                    .array_len = 1,
                    .samples = 1,
                    .usage = ISL_SURF_USAGE_TEXTURE_BIT |
                             ISL_SURF_USAGE_DISABLE_AUX_BIT,
                    .tiling_flags = tiling_flags);
   t_assert(ok);

   struct isl_surf sub_surf;
   uint32_t x_offset_sa, y_offset_sa;
   uint64_t offset_B;
   isl_surf_get_image_surf(&dev, &surf,
                           4, 0, 0,
                           &sub_surf,
                           &offset_B,
                           &x_offset_sa,
                           &y_offset_sa);

   uint64_t start_tile_B, end_tile_B;
   isl_surf_get_image_range_B_tile(&surf, 4, 0, 0, &start_tile_B, &end_tile_B);

   t_assert(end_tile_B - start_tile_B <= surf.size_B);
   t_assert(end_tile_B - start_tile_B >= surf_data_size(&surf, 4));
}

static void
test_2d_astc_ldr_4x4_u8srgb_unorm_256x256x1_levels05_array01_samples01_noaux(const char *platform,
                                                                             uint32_t tiling_flags)
{
   struct intel_device_info devinfo;
   t_assert(intel_get_device_info_from_pci_id(
               intel_device_name_to_pci_device_id(platform), &devinfo));

   struct isl_device dev;
   isl_device_init(&dev, &devinfo);

   struct isl_surf surf;
   bool ok =
      isl_surf_init(&dev, &surf,
                    .dim = ISL_SURF_DIM_2D,
                    .format = ISL_FORMAT_R8G8B8A8_UNORM,
                    .width = 256,
                    .height = 256,
                    .depth = 1,
                    .levels = 5,
                    .array_len = 1,
                    .samples = 1,
                    .usage = ISL_SURF_USAGE_TEXTURE_BIT |
                             ISL_SURF_USAGE_DISABLE_AUX_BIT,
                    .tiling_flags = tiling_flags);
   t_assert(ok);

   struct isl_surf sub_surf;
   uint32_t x_offset_sa, y_offset_sa;
   uint64_t offset_B;
   isl_surf_get_image_surf(&dev, &surf,
                           4, 0, 0,
                           &sub_surf,
                           &offset_B,
                           &x_offset_sa,
                           &y_offset_sa);

   uint64_t start_tile_B, end_tile_B;
   isl_surf_get_image_range_B_tile(&surf, 4, 0, 0, &start_tile_B, &end_tile_B);

   t_assert(end_tile_B - start_tile_B <= surf.size_B);
   t_assert(end_tile_B - start_tile_B >= surf_data_size(&surf, 4));
}

int main(void)
{
   test_2d_r8g8b8a8_unorm_16x16x1_levels01_array01_samples01_noaux("dg2", ISL_TILING_4_BIT);
   test_2d_r8g8b8a8_unorm_256x256x1_levels05_array01_samples01_noaux("dg2", ISL_TILING_4_BIT);
   test_2d_r8g8b8a8_unorm_16x16x1_levels01_array01_samples01_noaux("icl", ISL_TILING_Y0_BIT);
   test_2d_r8g8b8a8_unorm_256x256x1_levels05_array01_samples01_noaux("icl", ISL_TILING_Y0_BIT);
   test_2d_r8g8b8a8_unorm_16x16x1_levels01_array01_samples01_noaux("tgl", ISL_TILING_LINEAR_BIT);
   test_2d_r8g8b8a8_unorm_256x256x1_levels05_array01_samples01_noaux("tgl", ISL_TILING_LINEAR_BIT);
   test_2d_astc_ldr_4x4_u8srgb_unorm_256x256x1_levels05_array01_samples01_noaux("skl", ISL_TILING_Y0_BIT);
   test_2d_astc_ldr_4x4_u8srgb_unorm_256x256x1_levels05_array01_samples01_noaux("skl", ISL_TILING_LINEAR_BIT);
}
