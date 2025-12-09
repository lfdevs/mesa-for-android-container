/*
 * Copyright © 2020 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */
#include "ac_null_device.h"
#include "ac_gpu_info.h"
#include "util/u_string.h"

bool
ac_null_device_create(struct radeon_info *gpu_info, const char *family)
{
   unsigned i;

   gpu_info->gfx_level = CLASS_UNKNOWN;
   gpu_info->family = CHIP_UNKNOWN;
   strcpy(gpu_info->marketing_name, "AMD Unknown");

   for (i = CHIP_TAHITI; i < CHIP_LAST; i++) {
      if (!strcasecmp(family, ac_get_family_name(i))) {
         /* Override family and gfx_level. */
         gpu_info->family = i;

         if (gpu_info->family >= CHIP_GFX1200)
            gpu_info->gfx_level = GFX12;
         else if (gpu_info->family >= CHIP_NAVI31)
            gpu_info->gfx_level = GFX11;
         else if (i >= CHIP_NAVI21)
            gpu_info->gfx_level = GFX10_3;
         else if (i >= CHIP_NAVI10)
            gpu_info->gfx_level = GFX10;
         else if (i >= CHIP_VEGA10)
            gpu_info->gfx_level = GFX9;
         else if (i >= CHIP_TONGA)
            gpu_info->gfx_level = GFX8;
         else if (i >= CHIP_BONAIRE)
            gpu_info->gfx_level = GFX7;
         else
            gpu_info->gfx_level = GFX6;
      }
   }

   if (gpu_info->family == CHIP_UNKNOWN)
      return false;

   gpu_info->pci_id = pci_ids[gpu_info->family].pci_id;
   gpu_info->max_se = pci_ids[gpu_info->family].has_dedicated_vram ? 4 : 1;
   gpu_info->num_se = gpu_info->max_se;
   if (gpu_info->gfx_level >= GFX10_3)
      gpu_info->max_waves_per_simd = 16;
   else if (gpu_info->gfx_level >= GFX10)
      gpu_info->max_waves_per_simd = 20;
   else if (gpu_info->family >= CHIP_POLARIS10 && gpu_info->family <= CHIP_VEGAM)
      gpu_info->max_waves_per_simd = 8;
   else
      gpu_info->max_waves_per_simd = 10;

   if (gpu_info->gfx_level >= GFX10)
      gpu_info->num_physical_sgprs_per_simd = 128 * gpu_info->max_waves_per_simd;
   else if (gpu_info->gfx_level >= GFX8)
      gpu_info->num_physical_sgprs_per_simd = 800;
   else
      gpu_info->num_physical_sgprs_per_simd = 512;

   gpu_info->has_timeline_syncobj = true;
   gpu_info->has_vm_always_valid = true;
   gpu_info->has_3d_cube_border_color_mipmap = true;
   gpu_info->has_image_opcodes = true;
   gpu_info->has_attr_ring = gpu_info->gfx_level >= GFX11;
   gpu_info->has_attr_ring_wait_bug = gpu_info->gfx_level == GFX11 || gpu_info->gfx_level == GFX11_5;
   gpu_info->has_ngg_fully_culled_bug = gpu_info->gfx_level == GFX10;
   gpu_info->has_ngg_passthru_no_msg = gpu_info->family >= CHIP_NAVI23;

   if (gpu_info->family == CHIP_NAVI31 || gpu_info->family == CHIP_NAVI32 || gpu_info->gfx_level >= GFX12)
      gpu_info->num_physical_wave64_vgprs_per_simd = 768;
   else if (gpu_info->gfx_level >= GFX10)
      gpu_info->num_physical_wave64_vgprs_per_simd = 512;
   else
      gpu_info->num_physical_wave64_vgprs_per_simd = 256;
   gpu_info->num_simd_per_compute_unit = gpu_info->gfx_level >= GFX10 ? 2 : 4;
   gpu_info->lds_size_per_workgroup = gpu_info->gfx_level >= GFX7 ? 64 * 1024 : 32 * 1024;
   gpu_info->max_render_backends = pci_ids[gpu_info->family].num_render_backends;

   gpu_info->has_dedicated_vram = pci_ids[gpu_info->family].has_dedicated_vram;
   gpu_info->has_packed_math_16bit = gpu_info->gfx_level >= GFX9;

   gpu_info->has_cb_lt16bit_int_clamp_bug = gpu_info->gfx_level <= GFX7 &&
                                            gpu_info->family != CHIP_HAWAII;

   gpu_info->has_image_load_dcc_bug = gpu_info->family == CHIP_NAVI23 || gpu_info->family == CHIP_VANGOGH;

   gpu_info->has_distributed_tess =
      gpu_info->gfx_level >= GFX10 || (gpu_info->gfx_level >= GFX8 && gpu_info->max_se >= 2);

   gpu_info->has_accelerated_dot_product =
      gpu_info->family == CHIP_VEGA20 ||
      (gpu_info->family >= CHIP_MI100 && gpu_info->family != CHIP_NAVI10 && gpu_info->family != CHIP_GFX1013);

   gpu_info->has_image_bvh_intersect_ray = gpu_info->gfx_level >= GFX10_3 || gpu_info->family == CHIP_GFX1013;

   gpu_info->address32_hi = gpu_info->gfx_level >= GFX9 ? 0xffff8000u : 0x0;

   gpu_info->has_rbplus = gpu_info->family == CHIP_STONEY || gpu_info->gfx_level >= GFX9;
   gpu_info->rbplus_allowed =
      gpu_info->has_rbplus &&
      (gpu_info->family == CHIP_STONEY || gpu_info->family == CHIP_VEGA12 || gpu_info->family == CHIP_RAVEN ||
       gpu_info->family == CHIP_RAVEN2 || gpu_info->family == CHIP_RENOIR || gpu_info->gfx_level >= GFX10_3);

   gpu_info->has_gang_submit = true;
   gpu_info->mesh_fast_launch_2 = gpu_info->gfx_level >= GFX11;
   gpu_info->hs_offchip_workgroup_dw_size = gpu_info->family == CHIP_HAWAII ? 4096 : 8192;
   gpu_info->has_ls_vgpr_init_bug = gpu_info->family == CHIP_VEGA10 || gpu_info->family == CHIP_RAVEN;
   gpu_info->has_graphics = true;
   gpu_info->ip[AMD_IP_GFX].num_queues = 1;

   gpu_info->gart_page_size = 4096;
   gpu_info->family_overridden = true;

   return true;
}
