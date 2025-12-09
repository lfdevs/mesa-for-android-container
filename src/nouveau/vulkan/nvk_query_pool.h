/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_QUERY_POOL_H
#define NVK_QUERY_POOL_H 1

#include "nvk_private.h"

#include "vk_query_pool.h"

struct nvkmd_mem;

enum nvk_query_pool_layout {
   /** Stores the availables and query reports as separate arrays.
    *
    * This uses less memory and is optimized for being able to memset a pile
    * of availables in one go.  In this layout, the query reports start at
    * reports_start and are every query_stride.
    */
   NVK_QUERY_POOL_LAYOUT_SEPARATE,

   /** Interleaves availables and reports interleaved in aligned chunks
    *
    * This uses more memory but ensures that each query is aligned to a CPU
    * cache line boundary for save non-coherent access.  In this layout, the
    * available is the first 4 bytes of the query and the reports start at
    * byte 16.
    */
   NVK_QUERY_POOL_LAYOUT_ALIGNED_INTERLEAVED,
};

struct nvk_query_pool {
   struct vk_query_pool vk;

   enum nvk_query_pool_layout layout;

   uint32_t reports_start;
   uint32_t query_stride;

   struct nvkmd_mem *mem;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(nvk_query_pool, vk.base, VkQueryPool,
                               VK_OBJECT_TYPE_QUERY_POOL)

#endif /* NVK_QUERY_POOL_H */
