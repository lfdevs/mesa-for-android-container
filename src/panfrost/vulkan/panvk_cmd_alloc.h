/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_CMD_ALLOC_H
#define PANVK_CMD_ALLOC_H

#include "panvk_cmd_buffer.h"
#include "panvk_macros.h"
#include "panvk_mempool.h"

static inline struct pan_ptr
panvk_cmd_alloc_from_pool(struct panvk_cmd_buffer *cmdbuf,
                          struct panvk_pool *pool,
                          struct panvk_pool_alloc_info info)
{
   if (!info.size)
      return (struct pan_ptr){0};

   struct pan_ptr ptr =
      pan_pool_alloc_aligned(&pool->base, info.size, info.alignment);

   if (!ptr.gpu) {
      VkResult error =
         panvk_catch_indirect_alloc_failure(VK_ERROR_OUT_OF_DEVICE_MEMORY);
      vk_command_buffer_set_error(&cmdbuf->vk, error);
   }

   return ptr;
}

#define panvk_cmd_alloc_dev_mem(__cmdbuf, __poolnm, __sz, __alignment)         \
   panvk_cmd_alloc_from_pool(__cmdbuf, &(__cmdbuf)->__poolnm##_pool,           \
                             (struct panvk_pool_alloc_info){                   \
                                .size = __sz,                                  \
                                .alignment = __alignment,                      \
                             })

/* jm-v7 0145: TLS and WLS regions must not cross a 4 GiB boundary: the GPU
 * adds the per-thread/per-workgroup offset to the low 32 bits of the base
 * only (kbase: BASE_MEM_GPU_VA_SAME_4GB_PAGE, "required for TLS memory";
 * pan_emit_tls() asserts it for WLS). A crossing region writes 4 GiB below
 * its base (Mali-G1-Ultra tessellation: translation fault at 0x5f0005d040,
 * pool BOs straddling 0x6000000000). A crossing allocation is skipped: the
 * next one starts past the boundary, or in a new pool BO. */
static inline uint64_t
panvk_cmd_alloc_tls_mem(struct panvk_cmd_buffer *cmdbuf, uint64_t size)
{
   for (unsigned i = 0; i < 4; i++) {
      uint64_t va = panvk_cmd_alloc_dev_mem(cmdbuf, tls, size, 4096).gpu;
      if (!va || (va >> 32) == ((va + size - 1) >> 32))
         return va;
   }

   vk_command_buffer_set_error(&cmdbuf->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   return 0;
}

static inline struct pan_ptr
panvk_cmd_upload_to_pool(struct panvk_cmd_buffer *cmdbuf,
                         struct panvk_pool *pool,
                         const void *data,
                         struct panvk_pool_alloc_info info)
{
   struct pan_ptr ptr = panvk_cmd_alloc_from_pool(cmdbuf, pool, info);
   if (ptr.gpu)
      memcpy(ptr.cpu, data, info.size);
   return ptr;
}

#define panvk_cmd_upload_dev_mem(__cmdbuf, __poolnm, __data, __sz, __alignment)\
   panvk_cmd_upload_to_pool(__cmdbuf, &(__cmdbuf)->__poolnm##_pool, (__data),  \
                            (struct panvk_pool_alloc_info){                    \
                               .size = __sz,                                   \
                               .alignment = __alignment,                       \
                            })

#define panvk_cmd_alloc_desc_aggregate(__cmdbuf, ...)                          \
   panvk_cmd_alloc_from_pool(                                                  \
      __cmdbuf, &(__cmdbuf)->desc_pool,                                        \
      panvk_pool_descs_to_alloc_info(PAN_DESC_AGGREGATE(__VA_ARGS__)))

#define panvk_cmd_alloc_desc(__cmdbuf, __desc)                                 \
   panvk_cmd_alloc_desc_aggregate(__cmdbuf, PAN_DESC(__desc))

#define panvk_cmd_alloc_desc_array(__cmdbuf, __count, __desc)                  \
   panvk_cmd_alloc_desc_aggregate(__cmdbuf, PAN_DESC_ARRAY(__count, __desc))

#endif
