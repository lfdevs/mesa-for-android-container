/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_NULL_DEVICE_H
#define RADV_NULL_DEVICE_H

#include <stdbool.h>
#include <stdint.h>
#include "amd_family.h"

/* Hardcode some GPU info that are needed for the driver or for some tools. */
static const struct {
   uint32_t pci_id;
   uint32_t num_render_backends;
   bool has_dedicated_vram;
} pci_ids[] = {
   /* clang-format off */
   [CHIP_TAHITI] = {0x6780, 8, true},
   [CHIP_PITCAIRN] = {0x6800, 8, true},
   [CHIP_VERDE] = {0x6820, 4, true},
   [CHIP_OLAND] = {0x6060, 2, true},
   [CHIP_HAINAN] = {0x6660, 2, true},
   [CHIP_BONAIRE] = {0x6640, 4, true},
   [CHIP_KAVERI] = {0x1304, 2, false},
   [CHIP_KABINI] = {0x9830, 2, false},
   [CHIP_HAWAII] = {0x67A0, 16, true},
   [CHIP_TONGA] = {0x6920, 8, true},
   [CHIP_ICELAND] = {0x6900, 2, true},
   [CHIP_CARRIZO] = {0x9870, 2, false},
   [CHIP_FIJI] = {0x7300, 16, true},
   [CHIP_STONEY] = {0x98E4, 2, false},
   [CHIP_POLARIS10] = {0x67C0, 8, true},
   [CHIP_POLARIS11] = {0x67E0, 4, true},
   [CHIP_POLARIS12] = {0x6980, 4, true},
   [CHIP_VEGAM] = {0x694C, 4, true},
   [CHIP_VEGA10] = {0x6860, 16, true},
   [CHIP_VEGA12] = {0x69A0, 8, true},
   [CHIP_VEGA20] = {0x66A0, 16, true},
   [CHIP_RAVEN] = {0x15DD, 2, false},
   [CHIP_RENOIR] = {0x1636, 2, false},
   [CHIP_MI100] = {0x738C, 2, true},
   [CHIP_NAVI10] = {0x7310, 16, true},
   [CHIP_NAVI12] = {0x7360, 8, true},
   [CHIP_NAVI14] = {0x7340, 8, true},
   [CHIP_NAVI21] = {0x73A0, 16, true},
   [CHIP_VANGOGH] = {0x163F, 8, false},
   [CHIP_NAVI22] = {0x73C0, 8, true},
   [CHIP_NAVI23] = {0x73E0, 8, true},
   [CHIP_NAVI31] = {0x744C, 24, true},
   [CHIP_GFX1201] = {0x7550, 16, true},
   /* clang-format on */
};

bool ac_null_device_create(struct radeon_info *gpu_info, const char *family);

#endif /* RADV_NULL_DEVICE_H */
