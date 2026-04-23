/*
 * Copyright © 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef TU_ACCELERATION_STRUCT_H
#define TU_ACCELERATION_STRUCT_H

#include "vk_acceleration_structure.h"
#include "vulkan/vulkan_core.h"

struct tu_device;

VkResult tu_init_null_accel_struct(struct tu_device *device);

extern const vk_acceleration_structure_build_ops tu_as_build_ops;

#endif
