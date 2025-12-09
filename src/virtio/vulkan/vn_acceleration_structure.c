/*
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "vn_acceleration_structure.h"

#include "venus-protocol/vn_protocol_driver_acceleration_structure.h"

#include "vn_device.h"

VkResult
vn_CreateAccelerationStructureKHR(
   VkDevice device,
   const VkAccelerationStructureCreateInfoKHR *pCreateInfo,
   const VkAllocationCallbacks *pAllocator,
   VkAccelerationStructureKHR *pAccelerationStructure)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.vk.alloc;

   struct vn_acceleration_structure *accel =
      vk_zalloc(alloc, sizeof(*accel), VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!accel)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&accel->base,
                       VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, &dev->base);

   VkAccelerationStructureKHR accel_handle =
      vn_acceleration_structure_to_handle(accel);
   vn_async_vkCreateAccelerationStructureKHR(
      dev->primary_ring, device, pCreateInfo, NULL, &accel_handle);

   *pAccelerationStructure = accel_handle;

   return VK_SUCCESS;
}

void
vn_DestroyAccelerationStructureKHR(
   VkDevice device,
   VkAccelerationStructureKHR accelerationStructure,
   const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_acceleration_structure *accel =
      vn_acceleration_structure_from_handle(accelerationStructure);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.vk.alloc;

   if (!accel)
      return;

   vn_async_vkDestroyAccelerationStructureKHR(dev->primary_ring, device,
                                              accelerationStructure, NULL);

   vn_object_base_fini(&accel->base);
   vk_free(alloc, accel);
}

void
vn_GetAccelerationStructureBuildSizesKHR(
   VkDevice device,
   VkAccelerationStructureBuildTypeKHR buildType,
   const VkAccelerationStructureBuildGeometryInfoKHR *pBuildInfo,
   const uint32_t *pMaxPrimitiveCounts,
   VkAccelerationStructureBuildSizesInfoKHR *pSizeInfo)
{
   struct vn_device *dev = vn_device_from_handle(device);

   vn_call_vkGetAccelerationStructureBuildSizesKHR(
      dev->primary_ring, device, buildType, pBuildInfo, pMaxPrimitiveCounts,
      pSizeInfo);
}

VkDeviceAddress
vn_GetAccelerationStructureDeviceAddressKHR(
   VkDevice device, const VkAccelerationStructureDeviceAddressInfoKHR *pInfo)
{
   struct vn_device *dev = vn_device_from_handle(device);

   return vn_call_vkGetAccelerationStructureDeviceAddressKHR(
      dev->primary_ring, device, pInfo);
}

void
vn_GetDeviceAccelerationStructureCompatibilityKHR(
   VkDevice device,
   const VkAccelerationStructureVersionInfoKHR *pVersionInfo,
   VkAccelerationStructureCompatibilityKHR *pCompatibility)
{
   struct vn_device *dev = vn_device_from_handle(device);

   /* TODO per device cache */
   vn_call_vkGetDeviceAccelerationStructureCompatibilityKHR(
      dev->primary_ring, device, pVersionInfo, pCompatibility);
}

VkResult
vn_BuildAccelerationStructuresKHR(
   VkDevice device,
   VkDeferredOperationKHR deferredOperation,
   uint32_t infoCount,
   const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
   const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
{
   struct vn_device *dev = vn_device_from_handle(device);
   UNREACHABLE("Unimplemented");
   return vn_error(dev->instance, VK_ERROR_FEATURE_NOT_PRESENT);
}

VkResult
vn_CopyAccelerationStructureKHR(
   VkDevice device,
   VkDeferredOperationKHR deferredOperation,
   const VkCopyAccelerationStructureInfoKHR *pInfo)
{
   struct vn_device *dev = vn_device_from_handle(device);
   UNREACHABLE("Unimplemented");
   return vn_error(dev->instance, VK_ERROR_FEATURE_NOT_PRESENT);
}

VkResult
vn_CopyAccelerationStructureToMemoryKHR(
   VkDevice device,
   VkDeferredOperationKHR deferredOperation,
   const VkCopyAccelerationStructureToMemoryInfoKHR *pInfo)
{
   struct vn_device *dev = vn_device_from_handle(device);
   UNREACHABLE("Unimplemented");
   return vn_error(dev->instance, VK_ERROR_FEATURE_NOT_PRESENT);
}

VkResult
vn_CopyMemoryToAccelerationStructureKHR(
   VkDevice device,
   VkDeferredOperationKHR deferredOperation,
   const VkCopyMemoryToAccelerationStructureInfoKHR *pInfo)
{
   struct vn_device *dev = vn_device_from_handle(device);
   UNREACHABLE("Unimplemented");
   return vn_error(dev->instance, VK_ERROR_FEATURE_NOT_PRESENT);
}

VkResult
vn_WriteAccelerationStructuresPropertiesKHR(
   VkDevice device,
   uint32_t accelerationStructureCount,
   const VkAccelerationStructureKHR *pAccelerationStructures,
   VkQueryType queryType,
   size_t dataSize,
   void *pData,
   size_t stride)
{
   struct vn_device *dev = vn_device_from_handle(device);
   UNREACHABLE("Unimplemented");
   return vn_error(dev->instance, VK_ERROR_FEATURE_NOT_PRESENT);
}
