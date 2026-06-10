/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef MTL_DEVICE_H
#define MTL_DEVICE_H 1

#include "mtl_format.h"
#include "mtl_types.h"

#include <stdbool.h>
#include <stdint.h>

/* TODO_KOSMICKRISP Remove */
struct kk_image_layout;

/* Device creation */
mtl_device *mtl_device_create(void);

/* Device operations */
void mtl_start_gpu_capture(mtl_device *mtl_dev_handle, const char *directory);
void mtl_stop_gpu_capture(void);

/* Device feature query */
void mtl_device_get_name(mtl_device *dev, char buffer[256]);
void mtl_device_get_architecture_name(mtl_device *dev, char buffer[256]);
uint64_t mtl_device_get_registry_id(mtl_device *dev);
bool mtl_device_supports_sample_count(mtl_device *dev, uint32_t sample_count);
struct mtl_size mtl_device_max_threads_per_threadgroup(mtl_device *dev);
uint32_t mtl_device_max_threadgroup_memory_length(mtl_device *dev);
uint64_t mtl_device_max_buffer_length(mtl_device *dev);
uint64_t mtl_device_recommended_max_working_set_size(mtl_device *dev);
uint64_t mtl_device_current_allocated_size(mtl_device *dev);

/* Timestamp query */
uint64_t mtl_device_get_gpu_timestamp(mtl_device *dev);

/* Resource queries */
void mtl_heap_buffer_size_and_align_with_length(mtl_device *device,
                                                uint64_t *size_B,
                                                uint64_t *align_B);
uint64_t mtl_minimum_linear_texture_alignment_for_pixel_format(
   mtl_device *device, enum mtl_pixel_format format);
void mtl_heap_texture_size_and_align_with_descriptor(
   mtl_device *device, struct kk_image_layout *layout, uint64_t *size_B,
   uint64_t *align_B);

uint32_t mtl_sparse_tile_size_in_bytes(mtl_device *device);
struct mtl_size mtl_sparse_tile_size(mtl_device *device,
                                     struct kk_image_layout *layout);
struct mtl_size mtl_sparse_tile_count(mtl_device *device,
                                      struct kk_image_layout *layout,
                                      struct mtl_size tile_size);

/* Resource creation */
mtl_buffer *mtl_new_buffer_with_bytes_no_copy(mtl_device *device, void *ptr,
                                              uint64_t size_B);

#endif /* MTL_DEVICE_H */
