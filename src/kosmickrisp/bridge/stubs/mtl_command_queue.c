/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_command_queue.h"

mtl_command_queue *
mtl_new_command_queue(mtl_device *device, uint32_t cmd_buffer_count)
{
   return NULL;
}

mtl_command_buffer *
mtl_new_command_buffer(mtl_command_queue *cmd_queue)
{
   return NULL;
}

void
mtl_command_queue_add_residency_set(mtl_command_queue *cmd_queue,
                                    mtl_residency_set *set)
{
}

void
mtl_command_queue_remove_residency_set(mtl_command_queue *cmd_queue,
                                       mtl_residency_set *set)
{
}
