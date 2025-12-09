/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_heap.h"

/* Creation */
mtl_heap *
mtl_new_heap(mtl_device *device, uint64_t size,
             enum mtl_resource_options resource_options)
{
   return NULL;
}

/* Utils */
uint64_t
mtl_heap_get_size(mtl_heap *heap)
{
   return 0u;
}

/* Allocation from heap */
mtl_buffer *
mtl_new_buffer_with_length(mtl_heap *heap, uint64_t size_B, uint64_t offset_B)
{
   return NULL;
}

mtl_texture *
mtl_new_texture_with_descriptor(mtl_heap *heap,
                                const struct kk_image_layout *layout,
                                uint64_t offset)
{
   return NULL;
}
