/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef MTL_COMMAND_BUFFER_H
#define MTL_COMMAND_BUFFER_H 1

#include "mtl_types.h"

#include <stdint.h>

void mtl_encode_signal_event(mtl_command_buffer *cmd_buf_handle,
                             mtl_event *event_handle, uint64_t value);

void mtl_encode_wait_for_event(mtl_command_buffer *cmd_buf_handle,
                               mtl_event *event_handle, uint64_t value);

void mtl_add_completed_handler(mtl_command_buffer *cmd,
                               void (*callback)(void *data), void *data);

void mtl_command_buffer_commit(mtl_command_buffer *cmd_buf);

void mtl_present_drawable(mtl_command_buffer *cmd_buf, void *drawable);

#endif /* MTL_COMMAND_BUFFER_H */
