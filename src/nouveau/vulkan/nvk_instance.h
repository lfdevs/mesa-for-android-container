/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_INSTANCE_H
#define NVK_INSTANCE_H 1

#include "nvk_private.h"

#include "nvk_debug.h"
#include "nvk_drirc.h"
#include "vk_instance.h"

struct nvk_instance {
   struct vk_instance vk;

   enum nvk_debug debug_flags;
   enum nvk_experimental experimental_flags;

   struct nvk_drirc drirc;

   uint8_t driver_build_sha[BLAKE3_KEY_LEN];
};

VK_DEFINE_HANDLE_CASTS(nvk_instance, vk.base, VkInstance, VK_OBJECT_TYPE_INSTANCE)

#endif
