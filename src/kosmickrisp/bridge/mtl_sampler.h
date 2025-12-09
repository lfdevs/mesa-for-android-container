/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef MTL_SAMPLER_H
#define MTL_SAMPLER_H 1

#include "mtl_types.h"

#include <inttypes.h>
#include <stdbool.h>

/* Sampler descriptor */
mtl_sampler_descriptor *mtl_new_sampler_descriptor(void);

/* Sampler descriptor utils */
void mtl_sampler_descriptor_set_normalized_coordinates(
   mtl_sampler_descriptor *descriptor, bool normalized_coordinates);
void mtl_sampler_descriptor_set_address_mode(
   mtl_sampler_descriptor *descriptor,
   enum mtl_sampler_address_mode address_mode_u,
   enum mtl_sampler_address_mode address_mode_v,
   enum mtl_sampler_address_mode address_mode_w);
void
mtl_sampler_descriptor_set_border_color(mtl_sampler_descriptor *descriptor,
                                        enum mtl_sampler_border_color color);
void
mtl_sampler_descriptor_set_filters(mtl_sampler_descriptor *descriptor,
                                   enum mtl_sampler_min_mag_filter min_filter,
                                   enum mtl_sampler_min_mag_filter mag_filter,
                                   enum mtl_sampler_mip_filter mip_filter);
void mtl_sampler_descriptor_set_lod_clamp(mtl_sampler_descriptor *descriptor,
                                          float min, float max);
void
mtl_sampler_descriptor_set_max_anisotropy(mtl_sampler_descriptor *descriptor,
                                          uint64_t max);
void
mtl_sampler_descriptor_set_compare_function(mtl_sampler_descriptor *descriptor,
                                            enum mtl_compare_function function);

/* Sampler */
mtl_sampler *mtl_new_sampler(mtl_device *device,
                             mtl_sampler_descriptor *descriptor);

/* Sampler utils */
uint64_t mtl_sampler_get_gpu_resource_id(mtl_sampler *sampler);

#endif /* MTL_SAMPLER_H */