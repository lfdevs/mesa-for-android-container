/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef MTL_COMMAND_BUFFER_H
#define MTL_COMMAND_BUFFER_H 1

#include "mtl_types.h"

#include <stdint.h>

void mtl_command_allocator_reset(mtl_command_allocator *allocator);

void mtl_begin_command_buffer(mtl_command_buffer *command_buffer,
                              mtl_command_allocator *allocator);
void mtl_end_command_buffer(mtl_command_buffer *command_buffer);

#endif /* MTL_COMMAND_BUFFER_H */
