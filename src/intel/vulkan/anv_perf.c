/*
 * Copyright © 2018 Intel Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "anv_private.h"

#include "perf/gen_perf.h"
#include "perf/gen_perf_mdapi.h"

struct gen_perf_config *
anv_get_perf(const struct gen_device_info *devinfo, int fd)
{
   struct gen_perf_config *perf = gen_perf_new(NULL);

   gen_perf_init_metrics(perf, devinfo, fd);

   /* We need DRM_I915_PERF_PROP_HOLD_PREEMPTION support, only available in
    * perf revision 2.
    */
   if (anv_gem_get_param(fd, I915_PARAM_PERF_REVISION) < 3)
      goto err;

   return perf;

 err:
   ralloc_free(perf);
   return NULL;
}

void
anv_device_perf_init(struct anv_device *device)
{
   device->perf_fd = -1;
}

static int
anv_device_perf_open(struct anv_device *device, uint64_t metric_id)
{
   uint64_t properties[DRM_I915_PERF_PROP_MAX * 2];
   struct drm_i915_perf_open_param param;
   int p = 0, stream_fd;

   properties[p++] = DRM_I915_PERF_PROP_SAMPLE_OA;
   properties[p++] = true;

   properties[p++] = DRM_I915_PERF_PROP_OA_METRICS_SET;
   properties[p++] = metric_id;

   properties[p++] = DRM_I915_PERF_PROP_OA_FORMAT;
   properties[p++] = device->info.gen >= 8 ?
      I915_OA_FORMAT_A32u40_A4u32_B8_C8 :
      I915_OA_FORMAT_A45_B8_C8;

   properties[p++] = DRM_I915_PERF_PROP_OA_EXPONENT;
   properties[p++] = 31; /* slowest sampling period */

   properties[p++] = DRM_I915_PERF_PROP_CTX_HANDLE;
   properties[p++] = device->context_id;

   properties[p++] = DRM_I915_PERF_PROP_HOLD_PREEMPTION;
   properties[p++] = true;

   memset(&param, 0, sizeof(param));
   param.flags = 0;
   param.flags |= I915_PERF_FLAG_FD_CLOEXEC | I915_PERF_FLAG_FD_NONBLOCK;
   param.properties_ptr = (uintptr_t)properties;
   param.num_properties = p / 2;

   stream_fd = gen_ioctl(device->fd, DRM_IOCTL_I915_PERF_OPEN, &param);
   return stream_fd;
}

VkResult anv_InitializePerformanceApiINTEL(
    VkDevice                                    _device,
    const VkInitializePerformanceApiInfoINTEL*  pInitializeInfo)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   const struct anv_physical_device *pdevice = &device->instance->physicalDevice;

   if (!pdevice->perf)
      return VK_ERROR_EXTENSION_NOT_PRESENT;

   /* Not much to do here */
   return VK_SUCCESS;
}

VkResult anv_GetPerformanceParameterINTEL(
    VkDevice                                    _device,
    VkPerformanceParameterTypeINTEL             parameter,
    VkPerformanceValueINTEL*                    pValue)
{
      ANV_FROM_HANDLE(anv_device, device, _device);
      const struct anv_physical_device *pdevice = &device->instance->physicalDevice;

      if (!pdevice->perf)
         return VK_ERROR_EXTENSION_NOT_PRESENT;

      VkResult result = VK_SUCCESS;
      switch (parameter) {
      case VK_PERFORMANCE_PARAMETER_TYPE_HW_COUNTERS_SUPPORTED_INTEL:
         pValue->type = VK_PERFORMANCE_VALUE_TYPE_BOOL_INTEL;
         pValue->data.valueBool = VK_TRUE;
         break;

      case VK_PERFORMANCE_PARAMETER_TYPE_STREAM_MARKER_VALID_BITS_INTEL:
         pValue->type = VK_PERFORMANCE_VALUE_TYPE_UINT32_INTEL;
         pValue->data.value32 = 25;
         break;

      default:
         result = VK_ERROR_FEATURE_NOT_PRESENT;
         break;
      }

      return result;
}

VkResult anv_CmdSetPerformanceMarkerINTEL(
    VkCommandBuffer                             commandBuffer,
    const VkPerformanceMarkerInfoINTEL*         pMarkerInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->intel_perf_marker = pMarkerInfo->marker;

   return VK_SUCCESS;
}

VkResult anv_AcquirePerformanceConfigurationINTEL(
    VkDevice                                    _device,
    const VkPerformanceConfigurationAcquireInfoINTEL* pAcquireInfo,
    VkPerformanceConfigurationINTEL*            pConfiguration)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   const struct anv_physical_device *pdevice = &device->instance->physicalDevice;

   struct gen_perf_registers *perf_config =
      gen_perf_load_configuration(pdevice->perf, device->fd,
                                  GEN_PERF_QUERY_GUID_MDAPI);
   if (!perf_config)
      return VK_INCOMPLETE;

   int ret = gen_perf_store_configuration(pdevice->perf, device->fd,
                                          perf_config, NULL /* guid */);
   if (ret < 0) {
      ralloc_free(perf_config);
      return VK_INCOMPLETE;
   }

   *pConfiguration = (VkPerformanceConfigurationINTEL) (uint64_t) ret;

   return VK_SUCCESS;
}

VkResult anv_ReleasePerformanceConfigurationINTEL(
    VkDevice                                    _device,
    VkPerformanceConfigurationINTEL             _configuration)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   uint64_t config = (uint64_t) _configuration;

   gen_ioctl(device->fd, DRM_IOCTL_I915_PERF_REMOVE_CONFIG, &config);

   return VK_SUCCESS;
}

VkResult anv_QueueSetPerformanceConfigurationINTEL(
    VkQueue                                     _queue,
    VkPerformanceConfigurationINTEL             _configuration)
{
   ANV_FROM_HANDLE(anv_queue, queue, _queue);
   struct anv_device *device = queue->device;
   uint64_t configuration = (uint64_t) _configuration;

   if (device->perf_fd < 0) {
      device->perf_fd = anv_device_perf_open(device, configuration);
      if (device->perf_fd < 0)
         return VK_ERROR_INITIALIZATION_FAILED;
   } else {
      int ret = gen_ioctl(device->perf_fd, I915_PERF_IOCTL_CONFIG,
                          (void *)(uintptr_t) _configuration);
      if (ret < 0)
         return anv_device_set_lost(device, "i915-perf config failed: %m");
   }

   return VK_SUCCESS;
}

void anv_UninitializePerformanceApiINTEL(
    VkDevice                                    _device)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   if (device->perf_fd >= 0) {
      close(device->perf_fd);
      device->perf_fd = -1;
   }
}
