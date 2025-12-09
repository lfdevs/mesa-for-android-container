/*
 * Copyright © 2023 Imagination Technologies Ltd.
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * based in part on radv driver which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PVR_WSI_H
#define PVR_WSI_H

#include <vulkan/vulkan_core.h>

struct pvr_physical_device;

VkResult pvr_wsi_init(struct pvr_physical_device *pdevice);
void pvr_wsi_finish(struct pvr_physical_device *pdevice);

#endif /* PVR_WSI_H */
