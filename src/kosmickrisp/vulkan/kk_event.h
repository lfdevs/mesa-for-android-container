/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_EVENT_H
#define KK_EVENT_H 1

#include "kk_private.h"

#include "vk_object.h"

struct kk_bo;

struct kk_event {
   struct vk_object_base base;
   struct kk_bo *bo;

   uint64_t addr;
   uint64_t *status;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(kk_event, base, VkEvent, VK_OBJECT_TYPE_EVENT)

#endif /* KK_EVENT_H */
