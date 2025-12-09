/*
 * Copyright © 2025 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_mem_stream.h"

#include "nvk_device.h"
#include "vk_sync.h"

struct nvk_mem_stream_chunk {
   struct nvkmd_mem *mem;

   /** Link in nvk_mem_stream::recycle */
   struct list_head link;

   /** Time point at which point this BO will be idle */
   uint64_t idle_time_point;
};

static VkResult
nvk_mem_stream_chunk_create(struct nvk_device *dev,
                            struct nvk_mem_stream_chunk **chunk_out)
{
   struct nvk_mem_stream_chunk *chunk;
   VkResult result;

   chunk = vk_zalloc(&dev->vk.alloc, sizeof(*chunk), 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (chunk == NULL)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = nvkmd_dev_alloc_mapped_mem(dev->nvkmd, &dev->vk.base,
                                       NVK_MEM_STREAM_MAX_ALLOC_SIZE, 0,
                                       NVKMD_MEM_GART, NVKMD_MEM_MAP_WR,
                                       &chunk->mem);
   if (result != VK_SUCCESS) {
      vk_free(&dev->vk.alloc, chunk);
      return result;
   }

   *chunk_out = chunk;

   return VK_SUCCESS;
}

static void
nvk_mem_stream_chunk_destroy(struct nvk_device *dev,
                             struct nvk_mem_stream_chunk *chunk)
{
   nvkmd_mem_unref(chunk->mem);
   vk_free(&dev->vk.alloc, chunk);
}

VkResult
nvk_mem_stream_init(struct nvk_device *dev,
                    struct nvk_mem_stream *stream)
{
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);
   VkResult result;

   const struct vk_sync_type *sync_type = pdev->nvkmd->sync_types[0];
   assert(sync_type->features & VK_SYNC_FEATURE_TIMELINE);

   result = vk_sync_create(&dev->vk, sync_type, VK_SYNC_IS_TIMELINE,
                           0, &stream->sync);
   if (result != VK_SUCCESS)
      return result;

   stream->next_time_point = 1;
   stream->time_point_passed = 0;

   stream->needs_flush = false;

   stream->chunk = NULL;
   stream->chunk_alloc_B = NVK_MEM_STREAM_MAX_ALLOC_SIZE;

   list_inithead(&stream->recycle);

   return VK_SUCCESS;
}

void
nvk_mem_stream_finish(struct nvk_device *dev,
                      struct nvk_mem_stream *stream)
{
   list_for_each_entry_safe(struct nvk_mem_stream_chunk, chunk,
                            &stream->recycle, link)
      nvk_mem_stream_chunk_destroy(dev, chunk);

   if (stream->chunk != NULL)
      nvk_mem_stream_chunk_destroy(dev, stream->chunk);

   vk_sync_destroy(&dev->vk, stream->sync);
}

static VkResult
nvk_mem_stream_get_chunk(struct nvk_device *dev,
                         struct nvk_mem_stream *stream,
                         struct nvk_mem_stream_chunk **chunk_out)
{
   if (!list_is_empty(&stream->recycle)) {
      /* Check to see if something on the recycle list is ready */
      struct nvk_mem_stream_chunk *chunk =
         list_first_entry(&stream->recycle, struct nvk_mem_stream_chunk, link);
      if (stream->time_point_passed >= chunk->idle_time_point) {
         list_del(&chunk->link);
         *chunk_out = chunk;
         return VK_SUCCESS;
      }

      /* Try again with a fresh time point.  Fetching a new time_point_passed
       * after the first check may avoid extra ioctls if things get really hot.
       */
      VkResult result = vk_sync_get_value(&dev->vk, stream->sync,
                                          &stream->time_point_passed);
      if (result != VK_SUCCESS)
         return result;

      if (stream->time_point_passed >= chunk->idle_time_point) {
         list_del(&chunk->link);
         *chunk_out = chunk;
         return VK_SUCCESS;
      }
   }

   return nvk_mem_stream_chunk_create(dev, chunk_out);
}

VkResult
nvk_mem_stream_alloc(struct nvk_device *dev,
                     struct nvk_mem_stream *stream,
                     uint32_t size_B, uint32_t align_B,
                     uint64_t *addr_out, void **map_out)
{
   assert(size_B <= NVK_MEM_STREAM_MAX_ALLOC_SIZE);
   assert(align_B <= NVK_MEM_STREAM_MAX_ALLOC_SIZE);

   if (size_B == 0) {
      *addr_out = 0;
      *map_out = NULL;
      return VK_SUCCESS;
   }

   stream->chunk_alloc_B = align(stream->chunk_alloc_B, align_B);
   assert(stream->chunk_alloc_B <= NVK_MEM_STREAM_MAX_ALLOC_SIZE);

   if (stream->chunk_alloc_B + size_B > NVK_MEM_STREAM_MAX_ALLOC_SIZE) {
      if (stream->chunk != NULL) {
         list_addtail(&stream->chunk->link, &stream->recycle);
         stream->chunk = NULL;
      }

      /* On the off chance that nvk_mem_stream_get_mem() fails, reset to MAX
       * here so that we hit the re-alloc path on the next attempt.
       */
      assert(stream->chunk == NULL);
      stream->chunk_alloc_B = NVK_MEM_STREAM_MAX_ALLOC_SIZE;

      VkResult result = nvk_mem_stream_get_chunk(dev, stream, &stream->chunk);
      if (result != VK_SUCCESS) {
         assert(stream->chunk == NULL);
         return result;
      }

      stream->chunk_alloc_B = 0;
   }

   /* Mark the chunk as not being idle until next_time_point */
   assert(stream->chunk->idle_time_point <= stream->next_time_point);
   stream->chunk->idle_time_point = stream->next_time_point;

   /* The stream needs to be flushed */
   stream->needs_flush = true;

   assert(stream->chunk_alloc_B + size_B <= NVK_MEM_STREAM_MAX_ALLOC_SIZE);
   *addr_out = stream->chunk->mem->va->addr + stream->chunk_alloc_B;
   *map_out = stream->chunk->mem->map + stream->chunk_alloc_B;
   stream->chunk_alloc_B += size_B;

   return VK_SUCCESS;
}

VkResult
nvk_mem_stream_flush(struct nvk_device *dev,
                     struct nvk_mem_stream *stream,
                     struct nvkmd_ctx *ctx,
                     uint64_t *time_point_out)
{
   if (!stream->needs_flush) {
      if (time_point_out != NULL)
         *time_point_out = stream->next_time_point - 1;
      return VK_SUCCESS;
   }

   if (stream->next_time_point == UINT64_MAX)
      abort();

   /* Flush any chunks with idle_time_point == next_time_point, including
    * those on the recycle list.  We can assume that anything with
    * idle_time_point < next_time_point is already flushed by a previous call
    * to nvk_mem_stream_flush().
    */
   if (stream->chunk != NULL) {
      struct nvk_mem_stream_chunk *chunk = stream->chunk;
      assert(chunk->idle_time_point <= stream->next_time_point);
      if (chunk->idle_time_point == stream->next_time_point) {
         nvkmd_mem_sync_map_to_gpu(stream->chunk->mem, 0,
                                   NVK_MEM_STREAM_MAX_ALLOC_SIZE);
      }
   }

   list_for_each_entry_rev(struct nvk_mem_stream_chunk, chunk,
                           &stream->recycle, link) {
      assert(chunk->idle_time_point <= stream->next_time_point);
      if (chunk->idle_time_point < stream->next_time_point)
         break;

      nvkmd_mem_sync_map_to_gpu(chunk->mem, 0, NVK_MEM_STREAM_MAX_ALLOC_SIZE);
   }

   const struct vk_sync_signal signal = {
      .sync = stream->sync,
      .stage_mask = ~0,
      .signal_value = stream->next_time_point,
   };
   VkResult result = nvkmd_ctx_signal(ctx, &dev->vk.base, 1, &signal);
   if (result != VK_SUCCESS)
      return result;

   if (time_point_out != NULL)
      *time_point_out = stream->next_time_point;

   stream->needs_flush = false;
   stream->next_time_point++;

   return VK_SUCCESS;
}

VkResult
nvk_mem_stream_push(struct nvk_device *dev,
                    struct nvk_mem_stream *stream,
                    struct nvkmd_ctx *ctx,
                    const uint32_t *push_data,
                    uint32_t push_dw_count,
                    uint64_t *time_point_out)
{
   VkResult result;

   uint64_t push_addr;
   void *push_map;
   result = nvk_mem_stream_alloc(dev, stream, push_dw_count * 4, 4,
                                 &push_addr, &push_map);
   if (result != VK_SUCCESS)
      return result;

   memcpy(push_map, push_data, push_dw_count * 4);

   const struct nvkmd_ctx_exec exec = {
      .addr = push_addr,
      .size_B = push_dw_count * 4,
   };
   result = nvkmd_ctx_exec(ctx, &dev->vk.base, 1, &exec);
   if (result != VK_SUCCESS)
      return result;

   return nvk_mem_stream_flush(dev, stream, ctx, time_point_out);
}

VkResult
nvk_mem_stream_sync(struct nvk_device *dev,
                    struct nvk_mem_stream *stream,
                    struct nvkmd_ctx *ctx)
{
   uint64_t time_point;
   VkResult result = nvk_mem_stream_flush(dev, stream, ctx, &time_point);
   if (result != VK_SUCCESS)
      return result;

   return vk_sync_wait(&dev->vk, stream->sync, time_point,
                       VK_SYNC_WAIT_COMPLETE, UINT64_MAX);
}
