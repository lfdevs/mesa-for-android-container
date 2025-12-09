/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef MTL_COMMAND_QUEUE_H
#define MTL_COMMAND_QUEUE_H 1

#include "mtl_types.h"

#include <stdint.h>

mtl_command_queue *mtl_new_command_queue(mtl_device *device,
                                         uint32_t cmd_buffer_count);

mtl_command_buffer *mtl_new_command_buffer(mtl_command_queue *cmd_queue);

#endif /* MTL_COMMAND_QUEUE_H */