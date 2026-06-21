/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_command_queue.h"

mtl_command_queue *
mtl_new_command_queue(mtl_device *device)
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

void
mtl_signal_event(mtl_command_queue *queue, mtl_event *event, uint64_t value)
{
}

void
mtl_wait_for_event(mtl_command_queue *queue, mtl_event *event, uint64_t value)
{
}

void
mtl_command_queue_commit(mtl_command_queue *queue,
                         mtl_command_buffer **command_buffers, uint32_t count)
{
}

void
mtl_command_queue_signal_drawable(mtl_command_queue *queue, void *drawable)
{
}

void
mtl_drawable_present(void *drawable)
{
}
