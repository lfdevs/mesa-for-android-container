/*
 * Copyright 2024 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_UTRACE_H
#define PANVK_UTRACE_H

#include "util/perf/u_trace.h"

#include "panvk_macros.h"
#include "panvk_mempool.h"

struct panvk_device;
struct vk_sync;

struct panvk_utrace_flush_data {
   uint32_t subqueue;

   struct vk_sync *sync;
   uint64_t wait_value;

   struct panvk_utrace_buf *clone_cs_root;
   bool free_self;
};

struct panvk_utrace_buf {
   uint64_t dev;
   uint64_t size;
   void *host;
};

void *panvk_utrace_create_buffer(struct u_trace_context *utctx,
                                 uint64_t size_B);

void panvk_utrace_delete_buffer(struct u_trace_context *utctx, void *buffer);

uint64_t panvk_utrace_read_ts(struct u_trace_context *utctx, void *timestamps,
                              uint64_t offset_B, uint32_t flags, void *flush_data);

const void *panvk_utrace_get_data(struct u_trace_context *utctx, void *buffer,
                                  uint64_t offset_B, uint32_t size_B);

void panvk_utrace_delete_flush_data(struct u_trace_context *utctx,
                                    void *flush_data);

#ifdef PAN_ARCH

#if PAN_ARCH >= 10

struct panvk_cmd_buffer;

struct panvk_utrace_cs_info {
   struct panvk_cmd_buffer *cmdbuf;
   struct cs_async_op *ts_async_op;
   bool capture_data_wait_for_ts;
};

/* Special value indicating that an indirect capture should be of registers
 * rather than an address. */
#define PANVK_UTRACE_CAPTURE_REGISTERS 0x1

VkResult
panvk_per_arch(utrace_context_init)(struct panvk_device *dev);

void panvk_per_arch(utrace_context_fini)(struct panvk_device *dev);

void panvk_per_arch(utrace_copy_buffer)(struct u_trace_context *utctx,
                                        void *cmdstream, void *ts_from,
                                        uint64_t from_offset, void *ts_to,
                                        uint64_t to_offset, uint64_t size_B);

struct cs_builder;
struct cs_buffer;

void panvk_per_arch(utrace_clone_init_builder)(struct cs_builder *b,
                                               struct panvk_device *dev,
                                               const struct cs_buffer *cs_root);
void panvk_per_arch(utrace_clone_finish_builder)(struct cs_builder *b);

#else /* PAN_ARCH >= 10 */

static inline VkResult
panvk_per_arch(utrace_context_init)(struct panvk_device *dev)
{
   return VK_SUCCESS;
}

static inline void
panvk_per_arch(utrace_context_fini)(struct panvk_device *dev)
{
}

#endif /* PAN_ARCH >= 10 */

#endif /* PAN_ARCH */

#endif /* PANVK_UTRACE_H */
