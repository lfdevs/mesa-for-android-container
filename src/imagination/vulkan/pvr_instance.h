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

#include <stdint.h>

#include "util/mesa-sha1.h"

struct pvr_instance {
   struct vk_instance vk;

   uint32_t active_device_count;

   uint8_t driver_build_sha[SHA1_DIGEST_LENGTH];
};

VK_DEFINE_HANDLE_CASTS(pvr_instance,
                       vk.base,
                       VkInstance,
                       VK_OBJECT_TYPE_INSTANCE)

#endif /* PVR_INSTANCE_H */
