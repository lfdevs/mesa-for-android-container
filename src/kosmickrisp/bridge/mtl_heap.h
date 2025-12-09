/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef MTL_HEAP_H
#define MTL_HEAP_H 1

#include "mtl_types.h"

#include <inttypes.h>

/* TODO_KOSMICKRISP We should move this struct to the bridge side. */
struct kk_image_layout;

/* Creation */
mtl_heap *mtl_new_heap(mtl_device *device, uint64_t size,
                       enum mtl_resource_options resource_options);

/* Utils */
uint64_t mtl_heap_get_size(mtl_heap *heap);

/* Allocation from heap */
mtl_buffer *mtl_new_buffer_with_length(mtl_heap *heap, uint64_t size_B,
                                       uint64_t offset_B);
mtl_texture *mtl_new_texture_with_descriptor(
   mtl_heap *heap, const struct kk_image_layout *layout, uint64_t offset);

#endif /* MTL_HEAP_H */