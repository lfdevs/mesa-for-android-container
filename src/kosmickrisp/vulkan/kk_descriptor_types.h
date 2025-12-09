/*
 * Copyright © 2024 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */
#ifndef KK_DESCRIPTOR_TYPES
#define KK_DESCRIPTOR_TYPES 1

#include "kk_private.h"

/* TODO_KOSMICKRISP Reduce size to 32 bytes by moving border to a heap. */
struct kk_sampled_image_descriptor {
   uint64_t image_gpu_resource_id;
   uint16_t sampler_index;
   uint16_t lod_bias_fp16;
   uint16_t lod_min_fp16;
   uint16_t lod_max_fp16;
   uint32_t has_border;
   uint32_t pad_to_64_bits;
   uint32_t border[4];
   uint64_t pad_to_power_2[3];
};

static_assert(sizeof(struct kk_sampled_image_descriptor) == 64,
              "kk_sampled_image_descriptor has no holes");

struct kk_storage_image_descriptor {
   uint64_t image_gpu_resource_id;
};

static_assert(sizeof(struct kk_storage_image_descriptor) == 8,
              "kk_storage_image_descriptor has no holes");

/* This has to match nir_address_format_64bit_bounded_global */
struct kk_buffer_address {
   uint64_t base_addr;
   uint32_t size;
   uint32_t zero; /* Must be zero! */
};

static_assert(sizeof(struct kk_buffer_address) == 16,
              "kk_buffer_address has no holes");

#endif /* KK_DESCRIPTOR_TYPES */
