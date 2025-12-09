/*
 * Copyright © 2022 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_PRIVATE_H
#define NVK_PRIVATE_H 1

#include <assert.h>

#include "vk_log.h"
#include "vk_util.h"

#define NVK_MAX_SETS 32
#define NVK_MAX_PUSH_SIZE 256
#define NVK_MAX_DYNAMIC_BUFFERS 64
#define NVK_MAX_RTS 8
#define NVK_MAX_SAMPLES 8
#define NVK_MIN_SSBO_ALIGNMENT 16
#define NVK_MIN_TEXEL_BUFFER_ALIGNMENT 16
#define NVK_MIN_UBO_ALIGNMENT 64
#define NVK_MAX_VIEWPORTS 16
// This constant tracks the biggest descriptor size, it must be conservative
// From Maxwell we only use much smaller descriptors.
#define NVK_MAX_DESCRIPTOR_SIZE 32
#define NVK_MAX_PUSH_DESCRIPTORS 32
#define NVK_MAX_DESCRIPTOR_SET_SIZE (1u << 30)
#define NVK_MAX_INLINE_UNIFORM_BLOCK_SIZE (1u << 16)
#define NVK_MAX_DESCRIPTORS (1 << 20)
#define NVK_PUSH_DESCRIPTOR_SET_SIZE \
   (NVK_MAX_PUSH_DESCRIPTORS * NVK_MAX_DESCRIPTOR_SIZE)
#define NVK_SSBO_BOUNDS_CHECK_ALIGNMENT 4
#define NVK_MAX_MULTIVIEW_VIEW_COUNT 32
#define NVK_MAX_DISCARD_RECTANGLES 8

#define NVK_SPARSE_ADDR_SPACE_SIZE (1ull << 39)
#define NVK_MAX_BUFFER_SIZE (1ull << 31)
#define NVK_MAX_SHARED_SIZE (48 * 1024)

/* Max size of a bound cbuf */
#define NVK_MAX_CBUF_SIZE (1u << 16)

#define NVK_MAX_IMAGE_PLANES 3
#define NVK_MAX_SAMPLER_PLANES 2

/* Device Generated Commands */
#define NVK_DGC_ALIGN 0x100

struct nvk_addr_range {
   uint64_t addr;
   uint64_t range;
};

#endif
