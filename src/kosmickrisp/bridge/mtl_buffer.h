/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef MTL_BUFFER_H
#define MTL_BUFFER_H 1

#include "mtl_types.h"

#include <inttypes.h>

struct kk_image_layout;

/* Utils */
uint64_t mtl_buffer_get_length(mtl_buffer *buffer);
uint64_t mtl_buffer_get_gpu_address(mtl_buffer *buffer);
/* Gets CPU address */
void *mtl_get_contents(mtl_buffer *buffer);

/* Allocation from buffer */
mtl_texture *mtl_new_texture_with_descriptor_linear(
   mtl_buffer *buffer, const struct kk_image_layout *layout, uint64_t offset);

#endif /* MTL_BUFFER_H */