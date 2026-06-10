/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef ANTI_LAG_LAYER_H
#define ANTI_LAG_LAYER_H

#include "util/simple_mtx.h"
#include "vulkan/vk_layer.h"
#include "vulkan/vulkan_core.h"
#include "ringbuffer.h"

#define MAX_FRAMES  8
#define MAX_QUERIES 256

enum frame_state {
   FRAME_INVALID = 0,
   FRAME_INPUT,   /* Frame is in input stage. */
   FRAME_SUBMIT,  /* All current queueSubmit calls are associated with this frame. */
   FRAME_PRESENT, /* Frame is in present stage and latencies can be evaluated. */
};

typedef struct frame {
   uint64_t frame_time;
   int64_t min_delay;

   enum frame_state state;
} frame;

struct query {
   uint64_t gpu_ts;
   uint64_t submit_ts;
   VkCommandBuffer cmdbuffer;
};

typedef struct queue_context {
   VkQueue queue;
   uint32_t queue_family_idx;
   bool latency_sensitive;
   VkCommandPool cmdPool;
   VkQueryPool queryPool;
   VkSemaphore semaphore;
   uint64_t semaphore_value;
   uint8_t submissions_per_frame[MAX_FRAMES];
   RINGBUFFER_DECLARE(queries, struct query, MAX_QUERIES);
} queue_context;

typedef struct device_context {

   struct DeviceDispatchTable {
#define DECLARE_HOOK(fn) PFN_vk##fn fn
      DECLARE_HOOK(GetDeviceProcAddr);
      DECLARE_HOOK(SetDeviceLoaderData);
      DECLARE_HOOK(DestroyDevice);
      DECLARE_HOOK(QueueSubmit);
      DECLARE_HOOK(QueueSubmit2);
      DECLARE_HOOK(QueueSubmit2KHR);
      DECLARE_HOOK(GetDeviceQueue);
      DECLARE_HOOK(CreateCommandPool);
      DECLARE_HOOK(DestroyCommandPool);
      DECLARE_HOOK(CreateQueryPool);
      DECLARE_HOOK(ResetQueryPool);
      DECLARE_HOOK(DestroyQueryPool);
      DECLARE_HOOK(GetQueryPoolResults);
      DECLARE_HOOK(AllocateCommandBuffers);
      DECLARE_HOOK(FreeCommandBuffers);
      DECLARE_HOOK(BeginCommandBuffer);
      DECLARE_HOOK(EndCommandBuffer);
      DECLARE_HOOK(GetCalibratedTimestampsKHR);
      DECLARE_HOOK(CmdWriteTimestamp);
      DECLARE_HOOK(CreateSemaphore);
      DECLARE_HOOK(DestroySemaphore);
      DECLARE_HOOK(GetSemaphoreCounterValue);
      DECLARE_HOOK(WaitSemaphores);
      DECLARE_HOOK(QueuePresentKHR);
#undef DECLARE_HOOK
   } vtable;

   VkDevice device;
   VkAllocationCallbacks alloc;
   simple_mtx_t mtx;
   bool enabled;

   struct {
      int64_t delta;
      uint64_t recalibrate_when;
      float timestamp_period;
   } calibration;

   RINGBUFFER_DECLARE(frames, frame, MAX_FRAMES);
   frame *active_frame;

   uint64_t avg_frame_time;
   uint64_t delta;
   uint64_t prev_input_begin;
   int64_t queuing_delay;

   unsigned num_queues;
   queue_context queues[];
} device_context;

device_context *get_device_context(const void *object);

void anti_lag_AntiLagUpdateAMD(VkDevice device, const VkAntiLagDataAMD *pData);
VkResult anti_lag_QueueSubmit2KHR(VkQueue queue, uint32_t submitCount,
                                  const VkSubmitInfo2 *pSubmits, VkFence fence);
VkResult anti_lag_QueueSubmit2(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2 *pSubmits,
                               VkFence fence);
VkResult anti_lag_QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits,
                              VkFence fence);
VkResult anti_lag_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo);

VkResult anti_lag_NegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *pVersionStruct);

#endif /* ANTI_LAG_LAYER_H */
