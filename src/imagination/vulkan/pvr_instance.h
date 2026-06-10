/*
 * Copyright © 2022 Imagination Technologies Ltd.
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

#ifndef PVR_INSTANCE_H
#define PVR_INSTANCE_H

#include "vk_instance.h"
#include "util/xmlconfig.h"

#include <stdint.h>

#include "util/mesa-blake3.h"

struct pvr_instance {
   struct vk_instance vk;

   uint32_t active_device_count;

   struct driOptionCache dri_options;
   struct driOptionCache available_dri_options;

   uint8_t driver_build_sha[BLAKE3_KEY_LEN];
   uint32_t force_vk_vendor;
   float heap_memory_percent;
};

VK_DEFINE_HANDLE_CASTS(pvr_instance,
                       vk.base,
                       VkInstance,
                       VK_OBJECT_TYPE_INSTANCE)

#endif /* PVR_INSTANCE_H */
