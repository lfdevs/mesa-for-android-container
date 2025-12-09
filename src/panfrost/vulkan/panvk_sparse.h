/*
 * Copyright © 2025 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_SPARSE_H
#define PANVK_SPARSE_H

#include "vk_device.h"

VkResult panvk_map_to_blackhole(struct panvk_device *device,
                                uint64_t address, uint64_t size);

struct pan_kmod_bo *panvk_get_blackhole(struct panvk_device *device);

#endif
