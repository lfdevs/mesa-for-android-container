/*
 * Copyright © Microsoft Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "dzn_private.h"

#include "vk_alloc.h"
#include "vk_debug_report.h"
#include "vk_util.h"

#include "util/macros.h"
#include "util/os_time.h"

static VkResult
dzn_sync_init(struct vk_device *device,
              struct vk_sync *sync,
              uint64_t initial_value)
{
   dzn_sync *dsync = container_of(sync, dzn_sync, vk);
   dzn_device *ddev = container_of(device, dzn_device, vk);

   assert(!(sync->flags & VK_SYNC_IS_SHAREABLE));

   if (FAILED(ddev->dev->CreateFence(initial_value, D3D12_FENCE_FLAG_NONE,
                                     IID_PPV_ARGS(&dsync->fence))))
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   return VK_SUCCESS;
}

static void
dzn_sync_finish(struct vk_device *device,
                struct vk_sync *sync)
{
   dzn_sync *dsync = container_of(sync, dzn_sync, vk);

   dsync->fence->Release();
}

static VkResult
dzn_sync_signal(struct vk_device *device,
                struct vk_sync *sync,
                uint64_t value)
{
   dzn_sync *dsync = container_of(sync, dzn_sync, vk);

   if (!(sync->flags & VK_SYNC_IS_TIMELINE))
      value = 1;

   if (FAILED(dsync->fence->Signal(value)))
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   return VK_SUCCESS;
}

static VkResult
dzn_sync_get_value(struct vk_device *device,
                   struct vk_sync *sync,
                   uint64_t *value)
{
   dzn_sync *dsync = container_of(sync, dzn_sync, vk);

   *value = dsync->fence->GetCompletedValue();
   return VK_SUCCESS;
}

static VkResult
dzn_sync_reset(struct vk_device *device,
               struct vk_sync *sync)
{
   dzn_sync *dsync = container_of(sync, dzn_sync, vk);

   if (FAILED(dsync->fence->Signal(0)))
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   return VK_SUCCESS;
}

static VkResult
dzn_sync_move(struct vk_device *device,
              struct vk_sync *dst,
              struct vk_sync *src)
{
   dzn_device *ddev = container_of(device, dzn_device, vk);
   dzn_sync *ddst = container_of(dst, dzn_sync, vk);
   dzn_sync *dsrc = container_of(src, dzn_sync, vk);
   ID3D12Fence *new_fence;

   if (FAILED(ddev->dev->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                     IID_PPV_ARGS(&new_fence))))
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   ddst->fence->Release();
   ddst->fence = dsrc->fence;
   dsrc->fence = new_fence;
   return VK_SUCCESS;
}

static VkResult
dzn_sync_wait(struct vk_device *device,
              uint32_t wait_count,
              const struct vk_sync_wait *waits,
              enum vk_sync_wait_flags wait_flags,
              uint64_t abs_timeout_ns)
{
   dzn_device *ddev = container_of(device, dzn_device, vk);

   HANDLE event = CreateEventA(NULL, FALSE, FALSE, NULL);
   if (event == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   STACK_ARRAY(ID3D12Fence *, fences, wait_count);
   STACK_ARRAY(uint64_t, values, wait_count);

   for (uint32_t i = 0; i < wait_count; i++) {
      dzn_sync *sync = container_of(waits[i].sync, dzn_sync, vk);

      fences[i] = sync->fence;
      values[i] = (sync->vk.flags & VK_SYNC_IS_TIMELINE) ? waits[i].wait_value : 1;
   }

   D3D12_MULTIPLE_FENCE_WAIT_FLAGS flags =
      (wait_flags & VK_SYNC_WAIT_ANY) ?
      D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY :
      D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL;

   if (FAILED(ddev->dev->SetEventOnMultipleFenceCompletion(fences, values,
                                                           wait_count, flags,
                                                           event))) {
      STACK_ARRAY_FINISH(fences);
      STACK_ARRAY_FINISH(values);
      CloseHandle(event);
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   DWORD timeout_ms;

   if (abs_timeout_ns == OS_TIMEOUT_INFINITE) {
      timeout_ms = INFINITE;
   } else {
      uint64_t cur_time = os_time_get_nano();
      uint64_t rel_timeout_ns =
         abs_timeout_ns > cur_time ? abs_timeout_ns - cur_time : 0;

      timeout_ms = (rel_timeout_ns / 1000000) + (rel_timeout_ns % 1000000 ? 1 : 0);
   }

   DWORD res =
      WaitForSingleObject(event, timeout_ms);

   CloseHandle(event);

   STACK_ARRAY_FINISH(fences);
   STACK_ARRAY_FINISH(values);

   if (res == WAIT_TIMEOUT)
      return VK_TIMEOUT;
   else if (res != WAIT_OBJECT_0)
      return vk_error(device, VK_ERROR_UNKNOWN);

   return VK_SUCCESS;
}

const struct vk_sync_type dzn_sync_type = {
   .size = sizeof(dzn_sync),
   .features = (enum vk_sync_features)
      (VK_SYNC_FEATURE_BINARY |
       VK_SYNC_FEATURE_TIMELINE |
       VK_SYNC_FEATURE_GPU_WAIT |
       VK_SYNC_FEATURE_GPU_MULTI_WAIT |
       VK_SYNC_FEATURE_CPU_WAIT |
       VK_SYNC_FEATURE_CPU_RESET |
       VK_SYNC_FEATURE_CPU_SIGNAL |
       VK_SYNC_FEATURE_WAIT_ANY |
       VK_SYNC_FEATURE_WAIT_BEFORE_SIGNAL),

   .init = dzn_sync_init,
   .finish = dzn_sync_finish,
   .signal = dzn_sync_signal,
   .get_value = dzn_sync_get_value,
   .reset = dzn_sync_reset,
   .move = dzn_sync_move,
   .wait_many = dzn_sync_wait,
};
