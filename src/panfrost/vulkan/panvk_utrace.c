/*
 * Copyright 2024 Google LLC
 * Copyright 2025 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_utrace.h"

#include "kmod/pan_kmod.h"
#include "util/log.h"
#include "util/timespec.h"
#include "panvk_device.h"
#include "panvk_physical_device.h"
#include "panvk_priv_bo.h"
#include "vk_sync.h"

static struct panvk_device *
to_dev(struct u_trace_context *utctx)
{
   return container_of(utctx, struct panvk_device, utrace.utctx);
}

void *
panvk_utrace_create_buffer(struct u_trace_context *utctx, uint64_t size_B)
{
   struct panvk_device *dev = to_dev(utctx);

   /* This memory is also used to write CSF commands, therefore we align to a
    * cache line. */
   const uint64_t alignment = 0x40;

   simple_mtx_lock(&dev->utrace.copy_buf_heap_lock);
   const uint64_t addr_dev =
      util_vma_heap_alloc(&dev->utrace.copy_buf_heap, size_B, alignment);
   simple_mtx_unlock(&dev->utrace.copy_buf_heap_lock);

   if (!addr_dev) {
      mesa_loge("Couldn't allocate utrace buffer (size = 0x%" PRIx64 ")."
                "Provide larger PANVK_UTRACE_CLONE_MEM_SIZE (current = 0x%" PRIx64 ")",
                size_B, dev->utrace.copy_buf_heap_bo->bo->size);
      return NULL;
   }

   struct panvk_utrace_buf *container = malloc(sizeof(struct panvk_utrace_buf));
   void *addr_host = dev->utrace.copy_buf_heap_bo->addr.host + addr_dev -
                     dev->utrace.copy_buf_heap_bo->addr.dev;

   *container = (struct panvk_utrace_buf){
      .host = addr_host,
      .dev = addr_dev,
      .size = size_B,
   };

   memset(addr_host, 0, size_B);

   return container;
}

void
panvk_utrace_delete_buffer(struct u_trace_context *utctx, void *buffer)
{
   struct panvk_device *dev = to_dev(utctx);
   struct panvk_utrace_buf *buf = buffer;

   simple_mtx_lock(&dev->utrace.copy_buf_heap_lock);
   util_vma_heap_free(&dev->utrace.copy_buf_heap, buf->dev, buf->size);
   simple_mtx_unlock(&dev->utrace.copy_buf_heap_lock);

   free(buffer);
}

uint64_t
panvk_utrace_read_ts(struct u_trace_context *utctx, void *timestamps,
                     uint64_t offset_B, uint32_t flags, void *flush_data)
{
   struct panvk_device *dev = to_dev(utctx);
   const struct panvk_physical_device *pdev =
      to_panvk_physical_device(dev->vk.physical);
   const struct pan_kmod_dev_props *props = &pdev->kmod.props;
   const struct panvk_utrace_buf *buf = timestamps;
   struct panvk_utrace_flush_data *data = flush_data;

   assert(props->timestamp_frequency);

   /* wait for the submit */
   if (data->sync) {
      if (vk_sync_wait(&dev->vk, data->sync, data->wait_value,
                       VK_SYNC_WAIT_COMPLETE, UINT64_MAX) != VK_SUCCESS)
         mesa_logw("failed to wait for utrace timestamps");

      data->sync = NULL;
      data->wait_value = 0;
   }

   const uint64_t *ts_ptr = buf->host + offset_B;
   uint64_t ts = *ts_ptr;
   if (ts != U_TRACE_NO_TIMESTAMP)
      ts = (ts * NSEC_PER_SEC) / props->timestamp_frequency;

   return ts;
}

const void *
panvk_utrace_get_data(struct u_trace_context *utctx, void *buffer,
                      uint64_t offset_B, uint32_t size_B)
{
   const struct panvk_utrace_buf *buf = buffer;
   return buf->host + offset_B;
}

void
panvk_utrace_delete_flush_data(struct u_trace_context *utctx, void *flush_data)
{
   struct panvk_utrace_flush_data *data = flush_data;

   if (data->clone_cs_root) {
      panvk_utrace_delete_buffer(utctx, data->clone_cs_root);
      data->clone_cs_root = NULL;
   }

   if (data->free_self)
      free(data);
}
