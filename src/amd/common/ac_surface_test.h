/*
 * Copyright © 2021 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_SURFACE_TEST_COMMON_H
#define AC_SURFACE_TEST_COMMON_H

#include "ac_gpu_info.h"
#include "amdgfxregs.h"

#include "amdgpu_devices.h"
#include "ac_linux_drm.h"

struct ac_surface_fake_device {
   const char *name;
   const char *device_name;
   int banks_or_pkrs;
   int pipes;
   int se;
   int rb_per_se;
};

static struct ac_surface_fake_device ac_surface_fake_devices[] = {
   {"polaris12", "polaris12"},
   {"vega10", "vega10", 4, 2, 2, 2},
   {"vega10_diff_bank", "vega10", 3, 2, 2, 2},
   {"vega10_diff_rb", "vega10", 4, 2, 2, 0},
   {"vega10_diff_pipe", "vega10", 4, 0, 2, 2},
   {"vega10_diff_se", "vega10", 4, 2, 1, 2},
   {"vega20", "vega20", 4, 2, 2, 2},
   {"raven", "raven", 0, 2, 0, 1},
   {"raven2", "raven2", 3, 1, 0, 1},
   /* Just test a bunch of different numbers. (packers, pipes) */
   {"navi10", "navi10", 0, 4},
   {"navi10_diff_pipe", "navi10", 0, 3},
   {"navi10_diff_pkr", "navi10", 1, 4},
   {"navi21", "navi21", 4, 4},
   {"navi21_8pkr", "navi21", 3, 4},
   {"navi22", "navi21", 3, 3},
   {"navi24", "navi21", 2, 2},
   {"vangogh", "vangogh", 1, 2},
   {"vangogh_1pkr", "vangogh", 0, 2},
   {"raphael", "vangogh", 0, 1},
   {"navi31", "navi31", 5, 5},
   {"navi33", "navi33", 3, 3},
   {"phoenix", "phoenix", 2, 2},
   {"phoenix_2pkr", "phoenix", 1, 2},
   {"phoenix2", "phoenix", 0, 2},
   {"phoenix2_2pipe", "phoenix", 0, 1},
   {"gfx12", "gfx1201", 4, 4},
};

static const struct amdgpu_device *find_amdgpu_device(const char *name)
{
   for (int i = 0;i < num_amdgpu_devices; i++) {
      if (strcmp(amdgpu_devices[i].name, name) == 0)
         return &amdgpu_devices[i];
   }
   assert(false);
   return NULL;
}

static void get_radeon_info(struct radeon_info *info, const struct ac_surface_fake_device *hw)
{
   const struct amdgpu_device *dev = find_amdgpu_device(hw->device_name);
   struct amdgpu_gpu_info gpu_info = { 0 };

   /* Emulate ac_drm_read_mm_registers to read relevant fields. */
   gpu_info.gb_addr_cfg = dev->mmr_regs[2];
   if (dev->dev.family < AMDGPU_FAMILY_AI) {
      for (int i = 0; i < 32; i++) {
         for (int j = 0; j < dev->mmr_reg_count; j++) {
            const uint32_t *triple = &dev->mmr_regs[j * 3];

            if (triple[0] == 0x2644 + i)
               gpu_info.gb_tile_mode[i] = triple[2];
         }
      }
   }
   info->kernel_has_modifiers = 1;

   ac_fill_hw_ip_info(info, &dev->dev, AMD_IP_GFX, &dev->hw_ip_gfx);
   ac_fill_hw_ip_info(info, &dev->dev, AMD_IP_COMPUTE, &dev->hw_ip_compute);

   ac_identify_chip(info, &dev->dev);

   ac_fill_memory_info(info, &dev->dev, &dev->mem);
   ac_fill_hw_info(info, &dev->dev);

   ac_fill_tiling_info(info, &gpu_info);
   ac_fill_feature_info(info, &dev->dev);
   ac_fill_bug_info(info);
   ac_fill_tess_info(info);
   ac_fill_compiler_info(info, &dev->dev, false);

   switch(info->gfx_level) {
   case GFX9:
      info->gb_addr_config = (info->gb_addr_config &
                             C_0098F8_NUM_PIPES &
                             C_0098F8_NUM_BANKS &
                             C_0098F8_NUM_SHADER_ENGINES_GFX9 &
                             C_0098F8_NUM_RB_PER_SE) |
                             S_0098F8_NUM_PIPES(hw->pipes) |
                             S_0098F8_NUM_BANKS(hw->banks_or_pkrs) |
                             S_0098F8_NUM_SHADER_ENGINES_GFX9(hw->se) |
                             S_0098F8_NUM_RB_PER_SE(hw->rb_per_se);
      break;
   case GFX10:
   case GFX10_3:
   case GFX11:
   case GFX12:
      info->gb_addr_config = (info->gb_addr_config &
                             C_0098F8_NUM_PIPES &
                             C_0098F8_NUM_PKRS) |
                             S_0098F8_NUM_PIPES(hw->pipes) |
                             S_0098F8_NUM_PKRS(hw->banks_or_pkrs);
      /* 1 packer implies 1 RB except gfx10 where the field is ignored. */
      info->max_render_backends = info->gfx_level == GFX10 || hw->banks_or_pkrs ? 2 : 1;
      break;
   default:
      break;
   }
}

#endif
