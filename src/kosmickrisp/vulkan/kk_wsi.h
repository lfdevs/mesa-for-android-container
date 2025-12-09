/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_WSI_H
#define KK_WSI_H 1

#include "kk_physical_device.h"

VkResult kk_init_wsi(struct kk_physical_device *pdev);
void kk_finish_wsi(struct kk_physical_device *pdev);

#endif /* KK_WSI_H */
