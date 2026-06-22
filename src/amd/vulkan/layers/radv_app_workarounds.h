/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_APP_WORKAROUNDS_H
#define RADV_APP_WORKAROUNDS_H

#include <vulkan/vulkan.h>

VKAPI_ATTR VkResult VKAPI_CALL metro_exodus_GetSemaphoreCounterValue(VkDevice _device, VkSemaphore _semaphore,
                                                                     uint64_t *pValue);

VKAPI_ATTR VkResult VKAPI_CALL no_mans_sky_CreateImageView(VkDevice _device, const VkImageViewCreateInfo *pCreateInfo,
                                                           const VkAllocationCallbacks *pAllocator, VkImageView *pView);

VKAPI_ATTR VkResult VKAPI_CALL quantic_dream_UnmapMemory2(VkDevice _device, const VkMemoryUnmapInfo *pMemoryUnmapInfo);

VKAPI_ATTR void VKAPI_CALL rage2_CmdBeginRenderPass(VkCommandBuffer commandBuffer,
                                                    const VkRenderPassBeginInfo *pRenderPassBegin,
                                                    VkSubpassContents contents);

VKAPI_ATTR void VKAPI_CALL strange_brigade_CmdPipelineBarrier2(VkCommandBuffer commandBuffer,
                                                               const VkDependencyInfo *pDependencyInfo);

VKAPI_ATTR void VKAPI_CALL gfxbench5_CmdPipelineBarrier2(VkCommandBuffer commandBuffer,
                                                         const VkDependencyInfo *pDependencyInfo);

#endif /* RADV_APP_WORKAROUNDS_H */
