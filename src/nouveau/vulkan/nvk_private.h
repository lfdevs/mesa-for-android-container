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
#define NVK_MAX_PUSH_SIZE 128
#define NVK_MAX_DYNAMIC_BUFFERS 64
#define NVK_MAX_RTS 8
#define NVK_MAX_SAMPLES 8
#define NVK_MIN_SSBO_ALIGNMENT 16
#define NVK_MIN_TEXEL_BUFFER_ALIGNMENT 16
#define NVK_MIN_UBO_ALIGNMENT 64
#define NVK_MAX_VIEWPORTS 16
#define NVK_MAX_DESCRIPTOR_SIZE 16
#define NVK_MAX_PUSH_DESCRIPTORS 32
#define NVK_MAX_DESCRIPTOR_SET_SIZE (1u << 30)
#define NVK_MAX_DESCRIPTORS (1 << 20)
#define NVK_PUSH_DESCRIPTOR_SET_SIZE \
   (NVK_MAX_PUSH_DESCRIPTORS * NVK_MAX_DESCRIPTOR_SIZE)
#define NVK_SSBO_BOUNDS_CHECK_ALIGNMENT 4
#define NVK_MAX_MULTIVIEW_VIEW_COUNT 32

#define NVK_SPARSE_ADDR_SPACE_SIZE (1ull << 39)
#define NVK_MAX_BUFFER_SIZE (1ull << 31)
#define NVK_MAX_SHARED_SIZE (48 * 1024)

/* Max size of a bound cbuf */
#define NVK_MAX_CBUF_SIZE (1u << 16)

struct nvk_addr_range {
   uint64_t addr;
   uint64_t range;
};

#endif
