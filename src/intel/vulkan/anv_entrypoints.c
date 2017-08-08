/*
 * Copyright © 2015 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/* This file generated from anv_entrypoints_gen.py, don't edit directly. */

#include "anv_private.h"

struct anv_entrypoint {
   uint32_t name;
   uint32_t hash;
};

/* We use a big string constant to avoid lots of reloctions from the entry
 * point table to lots of little strings. The entries in the entry point table
 * store the index into this big string.
 */

static const char strings[] =
    "vkCreateInstance\0"
    "vkDestroyInstance\0"
    "vkEnumeratePhysicalDevices\0"
    "vkGetDeviceProcAddr\0"
    "vkGetInstanceProcAddr\0"
    "vkGetPhysicalDeviceProperties\0"
    "vkGetPhysicalDeviceQueueFamilyProperties\0"
    "vkGetPhysicalDeviceMemoryProperties\0"
    "vkGetPhysicalDeviceFeatures\0"
    "vkGetPhysicalDeviceFormatProperties\0"
    "vkGetPhysicalDeviceImageFormatProperties\0"
    "vkCreateDevice\0"
    "vkDestroyDevice\0"
    "vkEnumerateInstanceLayerProperties\0"
    "vkEnumerateInstanceExtensionProperties\0"
    "vkEnumerateDeviceLayerProperties\0"
    "vkEnumerateDeviceExtensionProperties\0"
    "vkGetDeviceQueue\0"
    "vkQueueSubmit\0"
    "vkQueueWaitIdle\0"
    "vkDeviceWaitIdle\0"
    "vkAllocateMemory\0"
    "vkFreeMemory\0"
    "vkMapMemory\0"
    "vkUnmapMemory\0"
    "vkFlushMappedMemoryRanges\0"
    "vkInvalidateMappedMemoryRanges\0"
    "vkGetDeviceMemoryCommitment\0"
    "vkGetBufferMemoryRequirements\0"
    "vkBindBufferMemory\0"
    "vkGetImageMemoryRequirements\0"
    "vkBindImageMemory\0"
    "vkGetImageSparseMemoryRequirements\0"
    "vkGetPhysicalDeviceSparseImageFormatProperties\0"
    "vkQueueBindSparse\0"
    "vkCreateFence\0"
    "vkDestroyFence\0"
    "vkResetFences\0"
    "vkGetFenceStatus\0"
    "vkWaitForFences\0"
    "vkCreateSemaphore\0"
    "vkDestroySemaphore\0"
    "vkCreateEvent\0"
    "vkDestroyEvent\0"
    "vkGetEventStatus\0"
    "vkSetEvent\0"
    "vkResetEvent\0"
    "vkCreateQueryPool\0"
    "vkDestroyQueryPool\0"
    "vkGetQueryPoolResults\0"
    "vkCreateBuffer\0"
    "vkDestroyBuffer\0"
    "vkCreateBufferView\0"
    "vkDestroyBufferView\0"
    "vkCreateImage\0"
    "vkDestroyImage\0"
    "vkGetImageSubresourceLayout\0"
    "vkCreateImageView\0"
    "vkDestroyImageView\0"
    "vkCreateShaderModule\0"
    "vkDestroyShaderModule\0"
    "vkCreatePipelineCache\0"
    "vkDestroyPipelineCache\0"
    "vkGetPipelineCacheData\0"
    "vkMergePipelineCaches\0"
    "vkCreateGraphicsPipelines\0"
    "vkCreateComputePipelines\0"
    "vkDestroyPipeline\0"
    "vkCreatePipelineLayout\0"
    "vkDestroyPipelineLayout\0"
    "vkCreateSampler\0"
    "vkDestroySampler\0"
    "vkCreateDescriptorSetLayout\0"
    "vkDestroyDescriptorSetLayout\0"
    "vkCreateDescriptorPool\0"
    "vkDestroyDescriptorPool\0"
    "vkResetDescriptorPool\0"
    "vkAllocateDescriptorSets\0"
    "vkFreeDescriptorSets\0"
    "vkUpdateDescriptorSets\0"
    "vkCreateFramebuffer\0"
    "vkDestroyFramebuffer\0"
    "vkCreateRenderPass\0"
    "vkDestroyRenderPass\0"
    "vkGetRenderAreaGranularity\0"
    "vkCreateCommandPool\0"
    "vkDestroyCommandPool\0"
    "vkResetCommandPool\0"
    "vkAllocateCommandBuffers\0"
    "vkFreeCommandBuffers\0"
    "vkBeginCommandBuffer\0"
    "vkEndCommandBuffer\0"
    "vkResetCommandBuffer\0"
    "vkCmdBindPipeline\0"
    "vkCmdSetViewport\0"
    "vkCmdSetScissor\0"
    "vkCmdSetLineWidth\0"
    "vkCmdSetDepthBias\0"
    "vkCmdSetBlendConstants\0"
    "vkCmdSetDepthBounds\0"
    "vkCmdSetStencilCompareMask\0"
    "vkCmdSetStencilWriteMask\0"
    "vkCmdSetStencilReference\0"
    "vkCmdBindDescriptorSets\0"
    "vkCmdBindIndexBuffer\0"
    "vkCmdBindVertexBuffers\0"
    "vkCmdDraw\0"
    "vkCmdDrawIndexed\0"
    "vkCmdDrawIndirect\0"
    "vkCmdDrawIndexedIndirect\0"
    "vkCmdDispatch\0"
    "vkCmdDispatchIndirect\0"
    "vkCmdCopyBuffer\0"
    "vkCmdCopyImage\0"
    "vkCmdBlitImage\0"
    "vkCmdCopyBufferToImage\0"
    "vkCmdCopyImageToBuffer\0"
    "vkCmdUpdateBuffer\0"
    "vkCmdFillBuffer\0"
    "vkCmdClearColorImage\0"
    "vkCmdClearDepthStencilImage\0"
    "vkCmdClearAttachments\0"
    "vkCmdResolveImage\0"
    "vkCmdSetEvent\0"
    "vkCmdResetEvent\0"
    "vkCmdWaitEvents\0"
    "vkCmdPipelineBarrier\0"
    "vkCmdBeginQuery\0"
    "vkCmdEndQuery\0"
    "vkCmdResetQueryPool\0"
    "vkCmdWriteTimestamp\0"
    "vkCmdCopyQueryPoolResults\0"
    "vkCmdPushConstants\0"
    "vkCmdBeginRenderPass\0"
    "vkCmdNextSubpass\0"
    "vkCmdEndRenderPass\0"
    "vkCmdExecuteCommands\0"
    "vkDestroySurfaceKHR\0"
    "vkGetPhysicalDeviceSurfaceSupportKHR\0"
    "vkGetPhysicalDeviceSurfaceCapabilitiesKHR\0"
    "vkGetPhysicalDeviceSurfaceFormatsKHR\0"
    "vkGetPhysicalDeviceSurfacePresentModesKHR\0"
    "vkCreateSwapchainKHR\0"
    "vkDestroySwapchainKHR\0"
    "vkGetSwapchainImagesKHR\0"
    "vkAcquireNextImageKHR\0"
    "vkQueuePresentKHR\0"
    "vkCreateWaylandSurfaceKHR\0"
    "vkGetPhysicalDeviceWaylandPresentationSupportKHR\0"
    "vkCreateXlibSurfaceKHR\0"
    "vkGetPhysicalDeviceXlibPresentationSupportKHR\0"
    "vkCreateXcbSurfaceKHR\0"
    "vkGetPhysicalDeviceXcbPresentationSupportKHR\0"
    "vkGetPhysicalDeviceFeatures2KHR\0"
    "vkGetPhysicalDeviceProperties2KHR\0"
    "vkGetPhysicalDeviceFormatProperties2KHR\0"
    "vkGetPhysicalDeviceImageFormatProperties2KHR\0"
    "vkGetPhysicalDeviceQueueFamilyProperties2KHR\0"
    "vkGetPhysicalDeviceMemoryProperties2KHR\0"
    "vkGetPhysicalDeviceSparseImageFormatProperties2KHR\0"
    "vkCmdPushDescriptorSetKHR\0"
    "vkTrimCommandPoolKHR\0"
    "vkGetPhysicalDeviceExternalBufferPropertiesKHR\0"
    "vkGetMemoryFdKHR\0"
    "vkGetMemoryFdPropertiesKHR\0"
    "vkCreateDescriptorUpdateTemplateKHR\0"
    "vkDestroyDescriptorUpdateTemplateKHR\0"
    "vkUpdateDescriptorSetWithTemplateKHR\0"
    "vkCmdPushDescriptorSetWithTemplateKHR\0"
    "vkGetPhysicalDeviceSurfaceCapabilities2KHR\0"
    "vkGetPhysicalDeviceSurfaceFormats2KHR\0"
    "vkGetBufferMemoryRequirements2KHR\0"
    "vkGetImageMemoryRequirements2KHR\0"
    "vkGetImageSparseMemoryRequirements2KHR\0"
    "vkCreateDmaBufImageINTEL\0"
;

static const struct anv_entrypoint entrypoints[] = {
    { 0, 0x38a581a6 },
    { 17, 0x9bd21af2 },
    { 35, 0x5787c327 },
    { 62, 0xba013486 },
    { 82, 0x3d2ae9ad },
    { 104, 0x52fe22c9 },
    { 134, 0x4e5fc88a },
    { 175, 0xa90da4da },
    { 211, 0x113e2f33 },
    { 239, 0x3e54b398 },
    { 275, 0xdd36a867 },
    { 316, 0x85ed23f },
    { 331, 0x1fbcc9cb },
    { 347, 0x81f69d8 },
    { 382, 0xeb27627e },
    { 421, 0x2f8566e7 },
    { 454, 0x5fd13eed },
    { 491, 0xcc920d9a },
    { 508, 0xfa4713ec },
    { 522, 0x6f8fc2a5 },
    { 538, 0xd46c5f24 },
    { 555, 0x522b85d3 },
    { 572, 0x8f6f838a },
    { 585, 0xcb977bd8 },
    { 597, 0x1a1a0e2f },
    { 611, 0xff52f051 },
    { 637, 0x1e115cca },
    { 668, 0x46e38db5 },
    { 696, 0xab98422a },
    { 726, 0x6bcbdcb },
    { 745, 0x916f1e63 },
    { 774, 0x5caaae4a },
    { 792, 0x15855f5b },
    { 827, 0x272ef8ef },
    { 874, 0xc3628a09 },
    { 892, 0x958af968 },
    { 906, 0xfc64ee3c },
    { 921, 0x684781dc },
    { 935, 0x5f391892 },
    { 952, 0x19d64c81 },
    { 968, 0xf2065e5b },
    { 986, 0xcaab1faf },
    { 1005, 0xe7188731 },
    { 1019, 0x4df27c05 },
    { 1034, 0x96d834b },
    { 1051, 0x592ae5f5 },
    { 1062, 0x6d373ba8 },
    { 1075, 0x5edcd92b },
    { 1093, 0x37819a7f },
    { 1112, 0xbf3f2cb3 },
    { 1134, 0x7d4282b9 },
    { 1149, 0x94a07a45 },
    { 1165, 0x925bd256 },
    { 1184, 0x98b27962 },
    { 1204, 0x652128c2 },
    { 1218, 0xcbfb1d96 },
    { 1233, 0x9163b686 },
    { 1261, 0xdce077ff },
    { 1279, 0xb5853953 },
    { 1298, 0xa0d3cea2 },
    { 1319, 0x2d77af6e },
    { 1341, 0xcbf6489f },
    { 1363, 0x4112a673 },
    { 1386, 0x2092a349 },
    { 1409, 0xc3499606 },
    { 1431, 0x4b59f96d },
    { 1457, 0xf70c85eb },
    { 1482, 0x6aac68af },
    { 1500, 0x451ef1ed },
    { 1523, 0x9146f879 },
    { 1547, 0x13cf03f },
    { 1563, 0x3b645153 },
    { 1580, 0x3c14cc74 },
    { 1608, 0xa4227b08 },
    { 1637, 0xfb95a8a4 },
    { 1660, 0x47bdaf30 },
    { 1684, 0x9bd85f5 },
    { 1706, 0x4c449d3a },
    { 1731, 0x7a1347b1 },
    { 1752, 0xbfd090ae },
    { 1775, 0x887a38c4 },
    { 1795, 0xdc428e58 },
    { 1816, 0x109a9c18 },
    { 1835, 0x16f14324 },
    { 1855, 0xa9820d22 },
    { 1882, 0x820fe476 },
    { 1902, 0xd5d83a0a },
    { 1923, 0x6da9f7fd },
    { 1942, 0x8c0c811a },
    { 1967, 0xb9db2b91 },
    { 1988, 0xc54f7327 },
    { 2009, 0xaffb5725 },
    { 2028, 0x847dc731 },
    { 2049, 0x3af9fd84 },
    { 2067, 0x53d6c2b },
    { 2084, 0x48f28c7f },
    { 2100, 0x32282165 },
    { 2118, 0x30f14d07 },
    { 2136, 0x1c989dfb },
    { 2159, 0x7b3a8a63 },
    { 2179, 0xa8f534e2 },
    { 2206, 0xe7c4b134 },
    { 2231, 0x83e2b024 },
    { 2256, 0x28c7a5da },
    { 2280, 0x4c22d870 },
    { 2301, 0xa9c83f1d },
    { 2324, 0x9912c1a1 },
    { 2334, 0xbe5a8058 },
    { 2351, 0xe9ac41bf },
    { 2369, 0x94e7ed36 },
    { 2394, 0xbd58e867 },
    { 2408, 0xd6353005 },
    { 2430, 0xc939a0da },
    { 2446, 0x278effa9 },
    { 2461, 0x331ebf89 },
    { 2476, 0x929847e },
    { 2499, 0x68cddbac },
    { 2522, 0xd2986b5e },
    { 2540, 0x5bdd2ae0 },
    { 2556, 0xb4bc8d08 },
    { 2577, 0x4f88e4ba },
    { 2605, 0x93cb5cb8 },
    { 2627, 0x671bb594 },
    { 2645, 0xe257f075 },
    { 2659, 0x4fccce28 },
    { 2675, 0x3b9346b3 },
    { 2691, 0x97fccfe8 },
    { 2712, 0xf5064ea4 },
    { 2728, 0xd556fd22 },
    { 2742, 0x2f614082 },
    { 2762, 0xec4d324c },
    { 2782, 0xdee8c6d4 },
    { 2808, 0xb1c6b468 },
    { 2827, 0xcb7a58e3 },
    { 2848, 0x2eeec2f9 },
    { 2865, 0xdcdb0235 },
    { 2884, 0x9eaabe40 },
    { 2905, 0xf204ce7d },
    { 2925, 0x1a687885 },
    { 2962, 0x77890558 },
    { 3004, 0xe32227c8 },
    { 3041, 0x31c3cbd1 },
    { 3083, 0xcdefcaa8 },
    { 3104, 0x5a93ab74 },
    { 3126, 0x57695f28 },
    { 3150, 0xc3fedb2e },
    { 3172, 0xfc5fb6ce },
    { 3190, 0x2b2a4b79 },
    { 3216, 0x84e085ac },
    { 3265, 0xa693bc66 },
    { 3288, 0x34a063ab },
    { 3334, 0xc5e5b106 },
    { 3356, 0x41782cb9 },
    { 3401, 0x6a9a3636 },
    { 3433, 0xcd15838c },
    { 3467, 0x9099cbbb },
    { 3507, 0x102ff7ea },
    { 3552, 0x5ceb2bed },
    { 3597, 0xc8c3da3d },
    { 3637, 0x8746ed72 },
    { 3688, 0xf17232a1 },
    { 3714, 0x51177c8d },
    { 3735, 0xee68b389 },
    { 3782, 0x503c14c5 },
    { 3799, 0xb028a792 },
    { 3826, 0x5189488a },
    { 3862, 0xaa83901e },
    { 3899, 0x214ad230 },
    { 3936, 0x3d528981 },
    { 3974, 0x9497e378 },
    { 4017, 0xd00b7188 },
    { 4055, 0x78dbe98d },
    { 4089, 0x8de28366 },
    { 4122, 0x3df40f5e },
    { 4161, 0x6392dfa7 },
};

/* Weak aliases for all potential implementations. These will resolve to
 * NULL if they're not defined, which lets the resolve_entrypoint() function
 * either pick the correct entry point.
 */

    VkResult anv_CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) __attribute__ ((weak));
    void anv_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult anv_EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) __attribute__ ((weak));
    PFN_vkVoidFunction anv_GetDeviceProcAddr(VkDevice device, const char* pName) __attribute__ ((weak));
    PFN_vkVoidFunction anv_GetInstanceProcAddr(VkInstance instance, const char* pName) __attribute__ ((weak));
    void anv_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) __attribute__ ((weak));
    void anv_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties) __attribute__ ((weak));
    void anv_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties) __attribute__ ((weak));
    void anv_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures) __attribute__ ((weak));
    void anv_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties) __attribute__ ((weak));
    VkResult anv_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* pImageFormatProperties) __attribute__ ((weak));
    VkResult anv_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) __attribute__ ((weak));
    void anv_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult anv_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
    VkResult anv_EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
    VkResult anv_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
    VkResult anv_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
    void anv_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue) __attribute__ ((weak));
    VkResult anv_QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence) __attribute__ ((weak));
    VkResult anv_QueueWaitIdle(VkQueue queue) __attribute__ ((weak));
    VkResult anv_DeviceWaitIdle(VkDevice device) __attribute__ ((weak));
    VkResult anv_AllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory) __attribute__ ((weak));
    void anv_FreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult anv_MapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData) __attribute__ ((weak));
    void anv_UnmapMemory(VkDevice device, VkDeviceMemory memory) __attribute__ ((weak));
    VkResult anv_FlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) __attribute__ ((weak));
    VkResult anv_InvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) __attribute__ ((weak));
    void anv_GetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory, VkDeviceSize* pCommittedMemoryInBytes) __attribute__ ((weak));
    void anv_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
    VkResult anv_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
    void anv_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
    VkResult anv_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
    void anv_GetImageSparseMemoryRequirements(VkDevice device, VkImage image, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements* pSparseMemoryRequirements) __attribute__ ((weak));
    void anv_GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling, uint32_t* pPropertyCount, VkSparseImageFormatProperties* pProperties) __attribute__ ((weak));
    VkResult anv_QueueBindSparse(VkQueue queue, uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence) __attribute__ ((weak));
    VkResult anv_CreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence) __attribute__ ((weak));
    void anv_DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult anv_ResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences) __attribute__ ((weak));
    VkResult anv_GetFenceStatus(VkDevice device, VkFence fence) __attribute__ ((weak));
    VkResult anv_WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout) __attribute__ ((weak));
    VkResult anv_CreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore) __attribute__ ((weak));
    void anv_DestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult anv_CreateEvent(VkDevice device, const VkEventCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkEvent* pEvent) __attribute__ ((weak));
    void anv_DestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult anv_GetEventStatus(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult anv_SetEvent(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult anv_ResetEvent(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult anv_CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool) __attribute__ ((weak));
    void anv_DestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult anv_GetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* pData, VkDeviceSize stride, VkQueryResultFlags flags) __attribute__ ((weak));
    VkResult anv_CreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer) __attribute__ ((weak));
    void anv_DestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult anv_CreateBufferView(VkDevice device, const VkBufferViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBufferView* pView) __attribute__ ((weak));
    void anv_DestroyBufferView(VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult anv_CreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage) __attribute__ ((weak));
    void anv_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void anv_GetImageSubresourceLayout(VkDevice device, VkImage image, const VkImageSubresource* pSubresource, VkSubresourceLayout* pLayout) __attribute__ ((weak));
    VkResult anv_CreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView) __attribute__ ((weak));
    void anv_DestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult anv_CreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule) __attribute__ ((weak));
    void anv_DestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult anv_CreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineCache* pPipelineCache) __attribute__ ((weak));
    void anv_DestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult anv_GetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, size_t* pDataSize, void* pData) __attribute__ ((weak));
    VkResult anv_MergePipelineCaches(VkDevice device, VkPipelineCache dstCache, uint32_t srcCacheCount, const VkPipelineCache* pSrcCaches) __attribute__ ((weak));
    VkResult anv_CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) __attribute__ ((weak));
    VkResult anv_CreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) __attribute__ ((weak));
    void anv_DestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult anv_CreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout) __attribute__ ((weak));
    void anv_DestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult anv_CreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSampler* pSampler) __attribute__ ((weak));
    void anv_DestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult anv_CreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout) __attribute__ ((weak));
    void anv_DestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult anv_CreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool) __attribute__ ((weak));
    void anv_DestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult anv_ResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags) __attribute__ ((weak));
    VkResult anv_AllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets) __attribute__ ((weak));
    VkResult anv_FreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets) __attribute__ ((weak));
    void anv_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies) __attribute__ ((weak));
    VkResult anv_CreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFramebuffer* pFramebuffer) __attribute__ ((weak));
    void anv_DestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult anv_CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass) __attribute__ ((weak));
    void anv_DestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void anv_GetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass, VkExtent2D* pGranularity) __attribute__ ((weak));
    VkResult anv_CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool) __attribute__ ((weak));
    void anv_DestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult anv_ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags) __attribute__ ((weak));
    VkResult anv_AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    void anv_FreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    VkResult anv_BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo) __attribute__ ((weak));
    VkResult anv_EndCommandBuffer(VkCommandBuffer commandBuffer) __attribute__ ((weak));
    VkResult anv_ResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags) __attribute__ ((weak));
    void anv_CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) __attribute__ ((weak));
    void anv_CmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports) __attribute__ ((weak));
    void anv_CmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors) __attribute__ ((weak));
    void anv_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth) __attribute__ ((weak));
    void anv_CmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor) __attribute__ ((weak));
    void anv_CmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4]) __attribute__ ((weak));
    void anv_CmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds) __attribute__ ((weak));
    void anv_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t compareMask) __attribute__ ((weak));
    void anv_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t writeMask) __attribute__ ((weak));
    void anv_CmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference) __attribute__ ((weak));
    void anv_CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets) __attribute__ ((weak));
    void anv_CmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) __attribute__ ((weak));
    void anv_CmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets) __attribute__ ((weak));
    void anv_CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) __attribute__ ((weak));
    void anv_CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) __attribute__ ((weak));
    void anv_CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) __attribute__ ((weak));
    void anv_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) __attribute__ ((weak));
    void anv_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) __attribute__ ((weak));
    void anv_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset) __attribute__ ((weak));
    void anv_CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions) __attribute__ ((weak));
    void anv_CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions) __attribute__ ((weak));
    void anv_CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter) __attribute__ ((weak));
    void anv_CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
    void anv_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
    void anv_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* pData) __attribute__ ((weak));
    void anv_CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data) __attribute__ ((weak));
    void anv_CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) __attribute__ ((weak));
    void anv_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) __attribute__ ((weak));
    void anv_CmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment* pAttachments, uint32_t rectCount, const VkClearRect* pRects) __attribute__ ((weak));
    void anv_CmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageResolve* pRegions) __attribute__ ((weak));
    void anv_CmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) __attribute__ ((weak));
    void anv_CmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) __attribute__ ((weak));
    void anv_CmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) __attribute__ ((weak));
    void anv_CmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) __attribute__ ((weak));
    void anv_CmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags) __attribute__ ((weak));
    void anv_CmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query) __attribute__ ((weak));
    void anv_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) __attribute__ ((weak));
    void anv_CmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query) __attribute__ ((weak));
    void anv_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags) __attribute__ ((weak));
    void anv_CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues) __attribute__ ((weak));
    void anv_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents) __attribute__ ((weak));
    void anv_CmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents) __attribute__ ((weak));
    void anv_CmdEndRenderPass(VkCommandBuffer commandBuffer) __attribute__ ((weak));
    void anv_CmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    void anv_DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult anv_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32* pSupported) __attribute__ ((weak));
    VkResult anv_GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult anv_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats) __attribute__ ((weak));
    VkResult anv_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes) __attribute__ ((weak));
    VkResult anv_CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) __attribute__ ((weak));
    void anv_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult anv_GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages) __attribute__ ((weak));
    VkResult anv_AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex) __attribute__ ((weak));
    VkResult anv_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) __attribute__ ((weak));
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    VkResult anv_CreateWaylandSurfaceKHR(VkInstance instance, const VkWaylandSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    VkBool32 anv_GetPhysicalDeviceWaylandPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, struct wl_display* display) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    VkResult anv_CreateXlibSurfaceKHR(VkInstance instance, const VkXlibSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    VkBool32 anv_GetPhysicalDeviceXlibPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, Display* dpy, VisualID visualID) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    VkResult anv_CreateXcbSurfaceKHR(VkInstance instance, const VkXcbSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    VkBool32 anv_GetPhysicalDeviceXcbPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, xcb_connection_t* connection, xcb_visualid_t visual_id) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XCB_KHR
    void anv_GetPhysicalDeviceFeatures2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2KHR* pFeatures) __attribute__ ((weak));
    void anv_GetPhysicalDeviceProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2KHR* pProperties) __attribute__ ((weak));
    void anv_GetPhysicalDeviceFormatProperties2KHR(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2KHR* pFormatProperties) __attribute__ ((weak));
    VkResult anv_GetPhysicalDeviceImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2KHR* pImageFormatInfo, VkImageFormatProperties2KHR* pImageFormatProperties) __attribute__ ((weak));
    void anv_GetPhysicalDeviceQueueFamilyProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2KHR* pQueueFamilyProperties) __attribute__ ((weak));
    void anv_GetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2KHR* pMemoryProperties) __attribute__ ((weak));
    void anv_GetPhysicalDeviceSparseImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2KHR* pFormatInfo, uint32_t* pPropertyCount, VkSparseImageFormatProperties2KHR* pProperties) __attribute__ ((weak));
    void anv_CmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites) __attribute__ ((weak));
    void anv_TrimCommandPoolKHR(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlagsKHR flags) __attribute__ ((weak));
    void anv_GetPhysicalDeviceExternalBufferPropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfoKHR* pExternalBufferInfo, VkExternalBufferPropertiesKHR* pExternalBufferProperties) __attribute__ ((weak));
    VkResult anv_GetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult anv_GetMemoryFdPropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBitsKHR handleType, int fd, VkMemoryFdPropertiesKHR* pMemoryFdProperties) __attribute__ ((weak));
    VkResult anv_CreateDescriptorUpdateTemplateKHR(VkDevice device, const VkDescriptorUpdateTemplateCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplateKHR* pDescriptorUpdateTemplate) __attribute__ ((weak));
    void anv_DestroyDescriptorUpdateTemplateKHR(VkDevice device, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void anv_UpdateDescriptorSetWithTemplateKHR(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const void* pData) __attribute__ ((weak));
    void anv_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) __attribute__ ((weak));
    VkResult anv_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult anv_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats) __attribute__ ((weak));
    void anv_GetBufferMemoryRequirements2KHR(VkDevice device, const VkBufferMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void anv_GetImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void anv_GetImageSparseMemoryRequirements2KHR(VkDevice device, const VkImageSparseMemoryRequirementsInfo2KHR* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2KHR* pSparseMemoryRequirements) __attribute__ ((weak));
    VkResult anv_CreateDmaBufImageINTEL(VkDevice device, const VkDmaBufImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,VkDeviceMemory* pMem,VkImage* pImage) __attribute__ ((weak));

  const struct anv_dispatch_table anv_layer = {
    .CreateInstance = anv_CreateInstance,
    .DestroyInstance = anv_DestroyInstance,
    .EnumeratePhysicalDevices = anv_EnumeratePhysicalDevices,
    .GetDeviceProcAddr = anv_GetDeviceProcAddr,
    .GetInstanceProcAddr = anv_GetInstanceProcAddr,
    .GetPhysicalDeviceProperties = anv_GetPhysicalDeviceProperties,
    .GetPhysicalDeviceQueueFamilyProperties = anv_GetPhysicalDeviceQueueFamilyProperties,
    .GetPhysicalDeviceMemoryProperties = anv_GetPhysicalDeviceMemoryProperties,
    .GetPhysicalDeviceFeatures = anv_GetPhysicalDeviceFeatures,
    .GetPhysicalDeviceFormatProperties = anv_GetPhysicalDeviceFormatProperties,
    .GetPhysicalDeviceImageFormatProperties = anv_GetPhysicalDeviceImageFormatProperties,
    .CreateDevice = anv_CreateDevice,
    .DestroyDevice = anv_DestroyDevice,
    .EnumerateInstanceLayerProperties = anv_EnumerateInstanceLayerProperties,
    .EnumerateInstanceExtensionProperties = anv_EnumerateInstanceExtensionProperties,
    .EnumerateDeviceLayerProperties = anv_EnumerateDeviceLayerProperties,
    .EnumerateDeviceExtensionProperties = anv_EnumerateDeviceExtensionProperties,
    .GetDeviceQueue = anv_GetDeviceQueue,
    .QueueSubmit = anv_QueueSubmit,
    .QueueWaitIdle = anv_QueueWaitIdle,
    .DeviceWaitIdle = anv_DeviceWaitIdle,
    .AllocateMemory = anv_AllocateMemory,
    .FreeMemory = anv_FreeMemory,
    .MapMemory = anv_MapMemory,
    .UnmapMemory = anv_UnmapMemory,
    .FlushMappedMemoryRanges = anv_FlushMappedMemoryRanges,
    .InvalidateMappedMemoryRanges = anv_InvalidateMappedMemoryRanges,
    .GetDeviceMemoryCommitment = anv_GetDeviceMemoryCommitment,
    .GetBufferMemoryRequirements = anv_GetBufferMemoryRequirements,
    .BindBufferMemory = anv_BindBufferMemory,
    .GetImageMemoryRequirements = anv_GetImageMemoryRequirements,
    .BindImageMemory = anv_BindImageMemory,
    .GetImageSparseMemoryRequirements = anv_GetImageSparseMemoryRequirements,
    .GetPhysicalDeviceSparseImageFormatProperties = anv_GetPhysicalDeviceSparseImageFormatProperties,
    .QueueBindSparse = anv_QueueBindSparse,
    .CreateFence = anv_CreateFence,
    .DestroyFence = anv_DestroyFence,
    .ResetFences = anv_ResetFences,
    .GetFenceStatus = anv_GetFenceStatus,
    .WaitForFences = anv_WaitForFences,
    .CreateSemaphore = anv_CreateSemaphore,
    .DestroySemaphore = anv_DestroySemaphore,
    .CreateEvent = anv_CreateEvent,
    .DestroyEvent = anv_DestroyEvent,
    .GetEventStatus = anv_GetEventStatus,
    .SetEvent = anv_SetEvent,
    .ResetEvent = anv_ResetEvent,
    .CreateQueryPool = anv_CreateQueryPool,
    .DestroyQueryPool = anv_DestroyQueryPool,
    .GetQueryPoolResults = anv_GetQueryPoolResults,
    .CreateBuffer = anv_CreateBuffer,
    .DestroyBuffer = anv_DestroyBuffer,
    .CreateBufferView = anv_CreateBufferView,
    .DestroyBufferView = anv_DestroyBufferView,
    .CreateImage = anv_CreateImage,
    .DestroyImage = anv_DestroyImage,
    .GetImageSubresourceLayout = anv_GetImageSubresourceLayout,
    .CreateImageView = anv_CreateImageView,
    .DestroyImageView = anv_DestroyImageView,
    .CreateShaderModule = anv_CreateShaderModule,
    .DestroyShaderModule = anv_DestroyShaderModule,
    .CreatePipelineCache = anv_CreatePipelineCache,
    .DestroyPipelineCache = anv_DestroyPipelineCache,
    .GetPipelineCacheData = anv_GetPipelineCacheData,
    .MergePipelineCaches = anv_MergePipelineCaches,
    .CreateGraphicsPipelines = anv_CreateGraphicsPipelines,
    .CreateComputePipelines = anv_CreateComputePipelines,
    .DestroyPipeline = anv_DestroyPipeline,
    .CreatePipelineLayout = anv_CreatePipelineLayout,
    .DestroyPipelineLayout = anv_DestroyPipelineLayout,
    .CreateSampler = anv_CreateSampler,
    .DestroySampler = anv_DestroySampler,
    .CreateDescriptorSetLayout = anv_CreateDescriptorSetLayout,
    .DestroyDescriptorSetLayout = anv_DestroyDescriptorSetLayout,
    .CreateDescriptorPool = anv_CreateDescriptorPool,
    .DestroyDescriptorPool = anv_DestroyDescriptorPool,
    .ResetDescriptorPool = anv_ResetDescriptorPool,
    .AllocateDescriptorSets = anv_AllocateDescriptorSets,
    .FreeDescriptorSets = anv_FreeDescriptorSets,
    .UpdateDescriptorSets = anv_UpdateDescriptorSets,
    .CreateFramebuffer = anv_CreateFramebuffer,
    .DestroyFramebuffer = anv_DestroyFramebuffer,
    .CreateRenderPass = anv_CreateRenderPass,
    .DestroyRenderPass = anv_DestroyRenderPass,
    .GetRenderAreaGranularity = anv_GetRenderAreaGranularity,
    .CreateCommandPool = anv_CreateCommandPool,
    .DestroyCommandPool = anv_DestroyCommandPool,
    .ResetCommandPool = anv_ResetCommandPool,
    .AllocateCommandBuffers = anv_AllocateCommandBuffers,
    .FreeCommandBuffers = anv_FreeCommandBuffers,
    .BeginCommandBuffer = anv_BeginCommandBuffer,
    .EndCommandBuffer = anv_EndCommandBuffer,
    .ResetCommandBuffer = anv_ResetCommandBuffer,
    .CmdBindPipeline = anv_CmdBindPipeline,
    .CmdSetViewport = anv_CmdSetViewport,
    .CmdSetScissor = anv_CmdSetScissor,
    .CmdSetLineWidth = anv_CmdSetLineWidth,
    .CmdSetDepthBias = anv_CmdSetDepthBias,
    .CmdSetBlendConstants = anv_CmdSetBlendConstants,
    .CmdSetDepthBounds = anv_CmdSetDepthBounds,
    .CmdSetStencilCompareMask = anv_CmdSetStencilCompareMask,
    .CmdSetStencilWriteMask = anv_CmdSetStencilWriteMask,
    .CmdSetStencilReference = anv_CmdSetStencilReference,
    .CmdBindDescriptorSets = anv_CmdBindDescriptorSets,
    .CmdBindIndexBuffer = anv_CmdBindIndexBuffer,
    .CmdBindVertexBuffers = anv_CmdBindVertexBuffers,
    .CmdDraw = anv_CmdDraw,
    .CmdDrawIndexed = anv_CmdDrawIndexed,
    .CmdDrawIndirect = anv_CmdDrawIndirect,
    .CmdDrawIndexedIndirect = anv_CmdDrawIndexedIndirect,
    .CmdDispatch = anv_CmdDispatch,
    .CmdDispatchIndirect = anv_CmdDispatchIndirect,
    .CmdCopyBuffer = anv_CmdCopyBuffer,
    .CmdCopyImage = anv_CmdCopyImage,
    .CmdBlitImage = anv_CmdBlitImage,
    .CmdCopyBufferToImage = anv_CmdCopyBufferToImage,
    .CmdCopyImageToBuffer = anv_CmdCopyImageToBuffer,
    .CmdUpdateBuffer = anv_CmdUpdateBuffer,
    .CmdFillBuffer = anv_CmdFillBuffer,
    .CmdClearColorImage = anv_CmdClearColorImage,
    .CmdClearDepthStencilImage = anv_CmdClearDepthStencilImage,
    .CmdClearAttachments = anv_CmdClearAttachments,
    .CmdResolveImage = anv_CmdResolveImage,
    .CmdSetEvent = anv_CmdSetEvent,
    .CmdResetEvent = anv_CmdResetEvent,
    .CmdWaitEvents = anv_CmdWaitEvents,
    .CmdPipelineBarrier = anv_CmdPipelineBarrier,
    .CmdBeginQuery = anv_CmdBeginQuery,
    .CmdEndQuery = anv_CmdEndQuery,
    .CmdResetQueryPool = anv_CmdResetQueryPool,
    .CmdWriteTimestamp = anv_CmdWriteTimestamp,
    .CmdCopyQueryPoolResults = anv_CmdCopyQueryPoolResults,
    .CmdPushConstants = anv_CmdPushConstants,
    .CmdBeginRenderPass = anv_CmdBeginRenderPass,
    .CmdNextSubpass = anv_CmdNextSubpass,
    .CmdEndRenderPass = anv_CmdEndRenderPass,
    .CmdExecuteCommands = anv_CmdExecuteCommands,
    .DestroySurfaceKHR = anv_DestroySurfaceKHR,
    .GetPhysicalDeviceSurfaceSupportKHR = anv_GetPhysicalDeviceSurfaceSupportKHR,
    .GetPhysicalDeviceSurfaceCapabilitiesKHR = anv_GetPhysicalDeviceSurfaceCapabilitiesKHR,
    .GetPhysicalDeviceSurfaceFormatsKHR = anv_GetPhysicalDeviceSurfaceFormatsKHR,
    .GetPhysicalDeviceSurfacePresentModesKHR = anv_GetPhysicalDeviceSurfacePresentModesKHR,
    .CreateSwapchainKHR = anv_CreateSwapchainKHR,
    .DestroySwapchainKHR = anv_DestroySwapchainKHR,
    .GetSwapchainImagesKHR = anv_GetSwapchainImagesKHR,
    .AcquireNextImageKHR = anv_AcquireNextImageKHR,
    .QueuePresentKHR = anv_QueuePresentKHR,
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .CreateWaylandSurfaceKHR = anv_CreateWaylandSurfaceKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .GetPhysicalDeviceWaylandPresentationSupportKHR = anv_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .CreateXlibSurfaceKHR = anv_CreateXlibSurfaceKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .GetPhysicalDeviceXlibPresentationSupportKHR = anv_GetPhysicalDeviceXlibPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .CreateXcbSurfaceKHR = anv_CreateXcbSurfaceKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .GetPhysicalDeviceXcbPresentationSupportKHR = anv_GetPhysicalDeviceXcbPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
    .GetPhysicalDeviceFeatures2KHR = anv_GetPhysicalDeviceFeatures2KHR,
    .GetPhysicalDeviceProperties2KHR = anv_GetPhysicalDeviceProperties2KHR,
    .GetPhysicalDeviceFormatProperties2KHR = anv_GetPhysicalDeviceFormatProperties2KHR,
    .GetPhysicalDeviceImageFormatProperties2KHR = anv_GetPhysicalDeviceImageFormatProperties2KHR,
    .GetPhysicalDeviceQueueFamilyProperties2KHR = anv_GetPhysicalDeviceQueueFamilyProperties2KHR,
    .GetPhysicalDeviceMemoryProperties2KHR = anv_GetPhysicalDeviceMemoryProperties2KHR,
    .GetPhysicalDeviceSparseImageFormatProperties2KHR = anv_GetPhysicalDeviceSparseImageFormatProperties2KHR,
    .CmdPushDescriptorSetKHR = anv_CmdPushDescriptorSetKHR,
    .TrimCommandPoolKHR = anv_TrimCommandPoolKHR,
    .GetPhysicalDeviceExternalBufferPropertiesKHR = anv_GetPhysicalDeviceExternalBufferPropertiesKHR,
    .GetMemoryFdKHR = anv_GetMemoryFdKHR,
    .GetMemoryFdPropertiesKHR = anv_GetMemoryFdPropertiesKHR,
    .CreateDescriptorUpdateTemplateKHR = anv_CreateDescriptorUpdateTemplateKHR,
    .DestroyDescriptorUpdateTemplateKHR = anv_DestroyDescriptorUpdateTemplateKHR,
    .UpdateDescriptorSetWithTemplateKHR = anv_UpdateDescriptorSetWithTemplateKHR,
    .CmdPushDescriptorSetWithTemplateKHR = anv_CmdPushDescriptorSetWithTemplateKHR,
    .GetPhysicalDeviceSurfaceCapabilities2KHR = anv_GetPhysicalDeviceSurfaceCapabilities2KHR,
    .GetPhysicalDeviceSurfaceFormats2KHR = anv_GetPhysicalDeviceSurfaceFormats2KHR,
    .GetBufferMemoryRequirements2KHR = anv_GetBufferMemoryRequirements2KHR,
    .GetImageMemoryRequirements2KHR = anv_GetImageMemoryRequirements2KHR,
    .GetImageSparseMemoryRequirements2KHR = anv_GetImageSparseMemoryRequirements2KHR,
    .CreateDmaBufImageINTEL = anv_CreateDmaBufImageINTEL,
  };
    VkResult gen7_CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) __attribute__ ((weak));
    void gen7_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen7_EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) __attribute__ ((weak));
    PFN_vkVoidFunction gen7_GetDeviceProcAddr(VkDevice device, const char* pName) __attribute__ ((weak));
    PFN_vkVoidFunction gen7_GetInstanceProcAddr(VkInstance instance, const char* pName) __attribute__ ((weak));
    void gen7_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) __attribute__ ((weak));
    void gen7_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties) __attribute__ ((weak));
    void gen7_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties) __attribute__ ((weak));
    void gen7_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures) __attribute__ ((weak));
    void gen7_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties) __attribute__ ((weak));
    VkResult gen7_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* pImageFormatProperties) __attribute__ ((weak));
    VkResult gen7_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) __attribute__ ((weak));
    void gen7_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen7_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
    VkResult gen7_EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
    VkResult gen7_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
    VkResult gen7_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
    void gen7_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue) __attribute__ ((weak));
    VkResult gen7_QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence) __attribute__ ((weak));
    VkResult gen7_QueueWaitIdle(VkQueue queue) __attribute__ ((weak));
    VkResult gen7_DeviceWaitIdle(VkDevice device) __attribute__ ((weak));
    VkResult gen7_AllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory) __attribute__ ((weak));
    void gen7_FreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen7_MapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData) __attribute__ ((weak));
    void gen7_UnmapMemory(VkDevice device, VkDeviceMemory memory) __attribute__ ((weak));
    VkResult gen7_FlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) __attribute__ ((weak));
    VkResult gen7_InvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) __attribute__ ((weak));
    void gen7_GetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory, VkDeviceSize* pCommittedMemoryInBytes) __attribute__ ((weak));
    void gen7_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
    VkResult gen7_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
    void gen7_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
    VkResult gen7_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
    void gen7_GetImageSparseMemoryRequirements(VkDevice device, VkImage image, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements* pSparseMemoryRequirements) __attribute__ ((weak));
    void gen7_GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling, uint32_t* pPropertyCount, VkSparseImageFormatProperties* pProperties) __attribute__ ((weak));
    VkResult gen7_QueueBindSparse(VkQueue queue, uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence) __attribute__ ((weak));
    VkResult gen7_CreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence) __attribute__ ((weak));
    void gen7_DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen7_ResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences) __attribute__ ((weak));
    VkResult gen7_GetFenceStatus(VkDevice device, VkFence fence) __attribute__ ((weak));
    VkResult gen7_WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout) __attribute__ ((weak));
    VkResult gen7_CreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore) __attribute__ ((weak));
    void gen7_DestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen7_CreateEvent(VkDevice device, const VkEventCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkEvent* pEvent) __attribute__ ((weak));
    void gen7_DestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen7_GetEventStatus(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult gen7_SetEvent(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult gen7_ResetEvent(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult gen7_CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool) __attribute__ ((weak));
    void gen7_DestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen7_GetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* pData, VkDeviceSize stride, VkQueryResultFlags flags) __attribute__ ((weak));
    VkResult gen7_CreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer) __attribute__ ((weak));
    void gen7_DestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen7_CreateBufferView(VkDevice device, const VkBufferViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBufferView* pView) __attribute__ ((weak));
    void gen7_DestroyBufferView(VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen7_CreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage) __attribute__ ((weak));
    void gen7_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen7_GetImageSubresourceLayout(VkDevice device, VkImage image, const VkImageSubresource* pSubresource, VkSubresourceLayout* pLayout) __attribute__ ((weak));
    VkResult gen7_CreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView) __attribute__ ((weak));
    void gen7_DestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen7_CreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule) __attribute__ ((weak));
    void gen7_DestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen7_CreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineCache* pPipelineCache) __attribute__ ((weak));
    void gen7_DestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen7_GetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, size_t* pDataSize, void* pData) __attribute__ ((weak));
    VkResult gen7_MergePipelineCaches(VkDevice device, VkPipelineCache dstCache, uint32_t srcCacheCount, const VkPipelineCache* pSrcCaches) __attribute__ ((weak));
    VkResult gen7_CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) __attribute__ ((weak));
    VkResult gen7_CreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) __attribute__ ((weak));
    void gen7_DestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen7_CreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout) __attribute__ ((weak));
    void gen7_DestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen7_CreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSampler* pSampler) __attribute__ ((weak));
    void gen7_DestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen7_CreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout) __attribute__ ((weak));
    void gen7_DestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen7_CreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool) __attribute__ ((weak));
    void gen7_DestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen7_ResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags) __attribute__ ((weak));
    VkResult gen7_AllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets) __attribute__ ((weak));
    VkResult gen7_FreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets) __attribute__ ((weak));
    void gen7_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies) __attribute__ ((weak));
    VkResult gen7_CreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFramebuffer* pFramebuffer) __attribute__ ((weak));
    void gen7_DestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen7_CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass) __attribute__ ((weak));
    void gen7_DestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen7_GetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass, VkExtent2D* pGranularity) __attribute__ ((weak));
    VkResult gen7_CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool) __attribute__ ((weak));
    void gen7_DestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen7_ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags) __attribute__ ((weak));
    VkResult gen7_AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    void gen7_FreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    VkResult gen7_BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo) __attribute__ ((weak));
    VkResult gen7_EndCommandBuffer(VkCommandBuffer commandBuffer) __attribute__ ((weak));
    VkResult gen7_ResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags) __attribute__ ((weak));
    void gen7_CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) __attribute__ ((weak));
    void gen7_CmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports) __attribute__ ((weak));
    void gen7_CmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors) __attribute__ ((weak));
    void gen7_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth) __attribute__ ((weak));
    void gen7_CmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor) __attribute__ ((weak));
    void gen7_CmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4]) __attribute__ ((weak));
    void gen7_CmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds) __attribute__ ((weak));
    void gen7_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t compareMask) __attribute__ ((weak));
    void gen7_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t writeMask) __attribute__ ((weak));
    void gen7_CmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference) __attribute__ ((weak));
    void gen7_CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets) __attribute__ ((weak));
    void gen7_CmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) __attribute__ ((weak));
    void gen7_CmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets) __attribute__ ((weak));
    void gen7_CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) __attribute__ ((weak));
    void gen7_CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) __attribute__ ((weak));
    void gen7_CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) __attribute__ ((weak));
    void gen7_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) __attribute__ ((weak));
    void gen7_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) __attribute__ ((weak));
    void gen7_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset) __attribute__ ((weak));
    void gen7_CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions) __attribute__ ((weak));
    void gen7_CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions) __attribute__ ((weak));
    void gen7_CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter) __attribute__ ((weak));
    void gen7_CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
    void gen7_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
    void gen7_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* pData) __attribute__ ((weak));
    void gen7_CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data) __attribute__ ((weak));
    void gen7_CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) __attribute__ ((weak));
    void gen7_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) __attribute__ ((weak));
    void gen7_CmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment* pAttachments, uint32_t rectCount, const VkClearRect* pRects) __attribute__ ((weak));
    void gen7_CmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageResolve* pRegions) __attribute__ ((weak));
    void gen7_CmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) __attribute__ ((weak));
    void gen7_CmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) __attribute__ ((weak));
    void gen7_CmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) __attribute__ ((weak));
    void gen7_CmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) __attribute__ ((weak));
    void gen7_CmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags) __attribute__ ((weak));
    void gen7_CmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query) __attribute__ ((weak));
    void gen7_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) __attribute__ ((weak));
    void gen7_CmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query) __attribute__ ((weak));
    void gen7_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags) __attribute__ ((weak));
    void gen7_CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues) __attribute__ ((weak));
    void gen7_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents) __attribute__ ((weak));
    void gen7_CmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents) __attribute__ ((weak));
    void gen7_CmdEndRenderPass(VkCommandBuffer commandBuffer) __attribute__ ((weak));
    void gen7_CmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    void gen7_DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen7_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32* pSupported) __attribute__ ((weak));
    VkResult gen7_GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen7_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats) __attribute__ ((weak));
    VkResult gen7_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes) __attribute__ ((weak));
    VkResult gen7_CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) __attribute__ ((weak));
    void gen7_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen7_GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages) __attribute__ ((weak));
    VkResult gen7_AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex) __attribute__ ((weak));
    VkResult gen7_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) __attribute__ ((weak));
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    VkResult gen7_CreateWaylandSurfaceKHR(VkInstance instance, const VkWaylandSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    VkBool32 gen7_GetPhysicalDeviceWaylandPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, struct wl_display* display) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    VkResult gen7_CreateXlibSurfaceKHR(VkInstance instance, const VkXlibSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    VkBool32 gen7_GetPhysicalDeviceXlibPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, Display* dpy, VisualID visualID) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    VkResult gen7_CreateXcbSurfaceKHR(VkInstance instance, const VkXcbSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    VkBool32 gen7_GetPhysicalDeviceXcbPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, xcb_connection_t* connection, xcb_visualid_t visual_id) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XCB_KHR
    void gen7_GetPhysicalDeviceFeatures2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2KHR* pFeatures) __attribute__ ((weak));
    void gen7_GetPhysicalDeviceProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2KHR* pProperties) __attribute__ ((weak));
    void gen7_GetPhysicalDeviceFormatProperties2KHR(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2KHR* pFormatProperties) __attribute__ ((weak));
    VkResult gen7_GetPhysicalDeviceImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2KHR* pImageFormatInfo, VkImageFormatProperties2KHR* pImageFormatProperties) __attribute__ ((weak));
    void gen7_GetPhysicalDeviceQueueFamilyProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2KHR* pQueueFamilyProperties) __attribute__ ((weak));
    void gen7_GetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2KHR* pMemoryProperties) __attribute__ ((weak));
    void gen7_GetPhysicalDeviceSparseImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2KHR* pFormatInfo, uint32_t* pPropertyCount, VkSparseImageFormatProperties2KHR* pProperties) __attribute__ ((weak));
    void gen7_CmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites) __attribute__ ((weak));
    void gen7_TrimCommandPoolKHR(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlagsKHR flags) __attribute__ ((weak));
    void gen7_GetPhysicalDeviceExternalBufferPropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfoKHR* pExternalBufferInfo, VkExternalBufferPropertiesKHR* pExternalBufferProperties) __attribute__ ((weak));
    VkResult gen7_GetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen7_GetMemoryFdPropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBitsKHR handleType, int fd, VkMemoryFdPropertiesKHR* pMemoryFdProperties) __attribute__ ((weak));
    VkResult gen7_CreateDescriptorUpdateTemplateKHR(VkDevice device, const VkDescriptorUpdateTemplateCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplateKHR* pDescriptorUpdateTemplate) __attribute__ ((weak));
    void gen7_DestroyDescriptorUpdateTemplateKHR(VkDevice device, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen7_UpdateDescriptorSetWithTemplateKHR(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const void* pData) __attribute__ ((weak));
    void gen7_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) __attribute__ ((weak));
    VkResult gen7_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen7_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats) __attribute__ ((weak));
    void gen7_GetBufferMemoryRequirements2KHR(VkDevice device, const VkBufferMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void gen7_GetImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void gen7_GetImageSparseMemoryRequirements2KHR(VkDevice device, const VkImageSparseMemoryRequirementsInfo2KHR* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2KHR* pSparseMemoryRequirements) __attribute__ ((weak));
    VkResult gen7_CreateDmaBufImageINTEL(VkDevice device, const VkDmaBufImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,VkDeviceMemory* pMem,VkImage* pImage) __attribute__ ((weak));

  const struct anv_dispatch_table gen7_layer = {
    .CreateInstance = gen7_CreateInstance,
    .DestroyInstance = gen7_DestroyInstance,
    .EnumeratePhysicalDevices = gen7_EnumeratePhysicalDevices,
    .GetDeviceProcAddr = gen7_GetDeviceProcAddr,
    .GetInstanceProcAddr = gen7_GetInstanceProcAddr,
    .GetPhysicalDeviceProperties = gen7_GetPhysicalDeviceProperties,
    .GetPhysicalDeviceQueueFamilyProperties = gen7_GetPhysicalDeviceQueueFamilyProperties,
    .GetPhysicalDeviceMemoryProperties = gen7_GetPhysicalDeviceMemoryProperties,
    .GetPhysicalDeviceFeatures = gen7_GetPhysicalDeviceFeatures,
    .GetPhysicalDeviceFormatProperties = gen7_GetPhysicalDeviceFormatProperties,
    .GetPhysicalDeviceImageFormatProperties = gen7_GetPhysicalDeviceImageFormatProperties,
    .CreateDevice = gen7_CreateDevice,
    .DestroyDevice = gen7_DestroyDevice,
    .EnumerateInstanceLayerProperties = gen7_EnumerateInstanceLayerProperties,
    .EnumerateInstanceExtensionProperties = gen7_EnumerateInstanceExtensionProperties,
    .EnumerateDeviceLayerProperties = gen7_EnumerateDeviceLayerProperties,
    .EnumerateDeviceExtensionProperties = gen7_EnumerateDeviceExtensionProperties,
    .GetDeviceQueue = gen7_GetDeviceQueue,
    .QueueSubmit = gen7_QueueSubmit,
    .QueueWaitIdle = gen7_QueueWaitIdle,
    .DeviceWaitIdle = gen7_DeviceWaitIdle,
    .AllocateMemory = gen7_AllocateMemory,
    .FreeMemory = gen7_FreeMemory,
    .MapMemory = gen7_MapMemory,
    .UnmapMemory = gen7_UnmapMemory,
    .FlushMappedMemoryRanges = gen7_FlushMappedMemoryRanges,
    .InvalidateMappedMemoryRanges = gen7_InvalidateMappedMemoryRanges,
    .GetDeviceMemoryCommitment = gen7_GetDeviceMemoryCommitment,
    .GetBufferMemoryRequirements = gen7_GetBufferMemoryRequirements,
    .BindBufferMemory = gen7_BindBufferMemory,
    .GetImageMemoryRequirements = gen7_GetImageMemoryRequirements,
    .BindImageMemory = gen7_BindImageMemory,
    .GetImageSparseMemoryRequirements = gen7_GetImageSparseMemoryRequirements,
    .GetPhysicalDeviceSparseImageFormatProperties = gen7_GetPhysicalDeviceSparseImageFormatProperties,
    .QueueBindSparse = gen7_QueueBindSparse,
    .CreateFence = gen7_CreateFence,
    .DestroyFence = gen7_DestroyFence,
    .ResetFences = gen7_ResetFences,
    .GetFenceStatus = gen7_GetFenceStatus,
    .WaitForFences = gen7_WaitForFences,
    .CreateSemaphore = gen7_CreateSemaphore,
    .DestroySemaphore = gen7_DestroySemaphore,
    .CreateEvent = gen7_CreateEvent,
    .DestroyEvent = gen7_DestroyEvent,
    .GetEventStatus = gen7_GetEventStatus,
    .SetEvent = gen7_SetEvent,
    .ResetEvent = gen7_ResetEvent,
    .CreateQueryPool = gen7_CreateQueryPool,
    .DestroyQueryPool = gen7_DestroyQueryPool,
    .GetQueryPoolResults = gen7_GetQueryPoolResults,
    .CreateBuffer = gen7_CreateBuffer,
    .DestroyBuffer = gen7_DestroyBuffer,
    .CreateBufferView = gen7_CreateBufferView,
    .DestroyBufferView = gen7_DestroyBufferView,
    .CreateImage = gen7_CreateImage,
    .DestroyImage = gen7_DestroyImage,
    .GetImageSubresourceLayout = gen7_GetImageSubresourceLayout,
    .CreateImageView = gen7_CreateImageView,
    .DestroyImageView = gen7_DestroyImageView,
    .CreateShaderModule = gen7_CreateShaderModule,
    .DestroyShaderModule = gen7_DestroyShaderModule,
    .CreatePipelineCache = gen7_CreatePipelineCache,
    .DestroyPipelineCache = gen7_DestroyPipelineCache,
    .GetPipelineCacheData = gen7_GetPipelineCacheData,
    .MergePipelineCaches = gen7_MergePipelineCaches,
    .CreateGraphicsPipelines = gen7_CreateGraphicsPipelines,
    .CreateComputePipelines = gen7_CreateComputePipelines,
    .DestroyPipeline = gen7_DestroyPipeline,
    .CreatePipelineLayout = gen7_CreatePipelineLayout,
    .DestroyPipelineLayout = gen7_DestroyPipelineLayout,
    .CreateSampler = gen7_CreateSampler,
    .DestroySampler = gen7_DestroySampler,
    .CreateDescriptorSetLayout = gen7_CreateDescriptorSetLayout,
    .DestroyDescriptorSetLayout = gen7_DestroyDescriptorSetLayout,
    .CreateDescriptorPool = gen7_CreateDescriptorPool,
    .DestroyDescriptorPool = gen7_DestroyDescriptorPool,
    .ResetDescriptorPool = gen7_ResetDescriptorPool,
    .AllocateDescriptorSets = gen7_AllocateDescriptorSets,
    .FreeDescriptorSets = gen7_FreeDescriptorSets,
    .UpdateDescriptorSets = gen7_UpdateDescriptorSets,
    .CreateFramebuffer = gen7_CreateFramebuffer,
    .DestroyFramebuffer = gen7_DestroyFramebuffer,
    .CreateRenderPass = gen7_CreateRenderPass,
    .DestroyRenderPass = gen7_DestroyRenderPass,
    .GetRenderAreaGranularity = gen7_GetRenderAreaGranularity,
    .CreateCommandPool = gen7_CreateCommandPool,
    .DestroyCommandPool = gen7_DestroyCommandPool,
    .ResetCommandPool = gen7_ResetCommandPool,
    .AllocateCommandBuffers = gen7_AllocateCommandBuffers,
    .FreeCommandBuffers = gen7_FreeCommandBuffers,
    .BeginCommandBuffer = gen7_BeginCommandBuffer,
    .EndCommandBuffer = gen7_EndCommandBuffer,
    .ResetCommandBuffer = gen7_ResetCommandBuffer,
    .CmdBindPipeline = gen7_CmdBindPipeline,
    .CmdSetViewport = gen7_CmdSetViewport,
    .CmdSetScissor = gen7_CmdSetScissor,
    .CmdSetLineWidth = gen7_CmdSetLineWidth,
    .CmdSetDepthBias = gen7_CmdSetDepthBias,
    .CmdSetBlendConstants = gen7_CmdSetBlendConstants,
    .CmdSetDepthBounds = gen7_CmdSetDepthBounds,
    .CmdSetStencilCompareMask = gen7_CmdSetStencilCompareMask,
    .CmdSetStencilWriteMask = gen7_CmdSetStencilWriteMask,
    .CmdSetStencilReference = gen7_CmdSetStencilReference,
    .CmdBindDescriptorSets = gen7_CmdBindDescriptorSets,
    .CmdBindIndexBuffer = gen7_CmdBindIndexBuffer,
    .CmdBindVertexBuffers = gen7_CmdBindVertexBuffers,
    .CmdDraw = gen7_CmdDraw,
    .CmdDrawIndexed = gen7_CmdDrawIndexed,
    .CmdDrawIndirect = gen7_CmdDrawIndirect,
    .CmdDrawIndexedIndirect = gen7_CmdDrawIndexedIndirect,
    .CmdDispatch = gen7_CmdDispatch,
    .CmdDispatchIndirect = gen7_CmdDispatchIndirect,
    .CmdCopyBuffer = gen7_CmdCopyBuffer,
    .CmdCopyImage = gen7_CmdCopyImage,
    .CmdBlitImage = gen7_CmdBlitImage,
    .CmdCopyBufferToImage = gen7_CmdCopyBufferToImage,
    .CmdCopyImageToBuffer = gen7_CmdCopyImageToBuffer,
    .CmdUpdateBuffer = gen7_CmdUpdateBuffer,
    .CmdFillBuffer = gen7_CmdFillBuffer,
    .CmdClearColorImage = gen7_CmdClearColorImage,
    .CmdClearDepthStencilImage = gen7_CmdClearDepthStencilImage,
    .CmdClearAttachments = gen7_CmdClearAttachments,
    .CmdResolveImage = gen7_CmdResolveImage,
    .CmdSetEvent = gen7_CmdSetEvent,
    .CmdResetEvent = gen7_CmdResetEvent,
    .CmdWaitEvents = gen7_CmdWaitEvents,
    .CmdPipelineBarrier = gen7_CmdPipelineBarrier,
    .CmdBeginQuery = gen7_CmdBeginQuery,
    .CmdEndQuery = gen7_CmdEndQuery,
    .CmdResetQueryPool = gen7_CmdResetQueryPool,
    .CmdWriteTimestamp = gen7_CmdWriteTimestamp,
    .CmdCopyQueryPoolResults = gen7_CmdCopyQueryPoolResults,
    .CmdPushConstants = gen7_CmdPushConstants,
    .CmdBeginRenderPass = gen7_CmdBeginRenderPass,
    .CmdNextSubpass = gen7_CmdNextSubpass,
    .CmdEndRenderPass = gen7_CmdEndRenderPass,
    .CmdExecuteCommands = gen7_CmdExecuteCommands,
    .DestroySurfaceKHR = gen7_DestroySurfaceKHR,
    .GetPhysicalDeviceSurfaceSupportKHR = gen7_GetPhysicalDeviceSurfaceSupportKHR,
    .GetPhysicalDeviceSurfaceCapabilitiesKHR = gen7_GetPhysicalDeviceSurfaceCapabilitiesKHR,
    .GetPhysicalDeviceSurfaceFormatsKHR = gen7_GetPhysicalDeviceSurfaceFormatsKHR,
    .GetPhysicalDeviceSurfacePresentModesKHR = gen7_GetPhysicalDeviceSurfacePresentModesKHR,
    .CreateSwapchainKHR = gen7_CreateSwapchainKHR,
    .DestroySwapchainKHR = gen7_DestroySwapchainKHR,
    .GetSwapchainImagesKHR = gen7_GetSwapchainImagesKHR,
    .AcquireNextImageKHR = gen7_AcquireNextImageKHR,
    .QueuePresentKHR = gen7_QueuePresentKHR,
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .CreateWaylandSurfaceKHR = gen7_CreateWaylandSurfaceKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .GetPhysicalDeviceWaylandPresentationSupportKHR = gen7_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .CreateXlibSurfaceKHR = gen7_CreateXlibSurfaceKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .GetPhysicalDeviceXlibPresentationSupportKHR = gen7_GetPhysicalDeviceXlibPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .CreateXcbSurfaceKHR = gen7_CreateXcbSurfaceKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .GetPhysicalDeviceXcbPresentationSupportKHR = gen7_GetPhysicalDeviceXcbPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
    .GetPhysicalDeviceFeatures2KHR = gen7_GetPhysicalDeviceFeatures2KHR,
    .GetPhysicalDeviceProperties2KHR = gen7_GetPhysicalDeviceProperties2KHR,
    .GetPhysicalDeviceFormatProperties2KHR = gen7_GetPhysicalDeviceFormatProperties2KHR,
    .GetPhysicalDeviceImageFormatProperties2KHR = gen7_GetPhysicalDeviceImageFormatProperties2KHR,
    .GetPhysicalDeviceQueueFamilyProperties2KHR = gen7_GetPhysicalDeviceQueueFamilyProperties2KHR,
    .GetPhysicalDeviceMemoryProperties2KHR = gen7_GetPhysicalDeviceMemoryProperties2KHR,
    .GetPhysicalDeviceSparseImageFormatProperties2KHR = gen7_GetPhysicalDeviceSparseImageFormatProperties2KHR,
    .CmdPushDescriptorSetKHR = gen7_CmdPushDescriptorSetKHR,
    .TrimCommandPoolKHR = gen7_TrimCommandPoolKHR,
    .GetPhysicalDeviceExternalBufferPropertiesKHR = gen7_GetPhysicalDeviceExternalBufferPropertiesKHR,
    .GetMemoryFdKHR = gen7_GetMemoryFdKHR,
    .GetMemoryFdPropertiesKHR = gen7_GetMemoryFdPropertiesKHR,
    .CreateDescriptorUpdateTemplateKHR = gen7_CreateDescriptorUpdateTemplateKHR,
    .DestroyDescriptorUpdateTemplateKHR = gen7_DestroyDescriptorUpdateTemplateKHR,
    .UpdateDescriptorSetWithTemplateKHR = gen7_UpdateDescriptorSetWithTemplateKHR,
    .CmdPushDescriptorSetWithTemplateKHR = gen7_CmdPushDescriptorSetWithTemplateKHR,
    .GetPhysicalDeviceSurfaceCapabilities2KHR = gen7_GetPhysicalDeviceSurfaceCapabilities2KHR,
    .GetPhysicalDeviceSurfaceFormats2KHR = gen7_GetPhysicalDeviceSurfaceFormats2KHR,
    .GetBufferMemoryRequirements2KHR = gen7_GetBufferMemoryRequirements2KHR,
    .GetImageMemoryRequirements2KHR = gen7_GetImageMemoryRequirements2KHR,
    .GetImageSparseMemoryRequirements2KHR = gen7_GetImageSparseMemoryRequirements2KHR,
    .CreateDmaBufImageINTEL = gen7_CreateDmaBufImageINTEL,
  };
    VkResult gen75_CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) __attribute__ ((weak));
    void gen75_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen75_EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) __attribute__ ((weak));
    PFN_vkVoidFunction gen75_GetDeviceProcAddr(VkDevice device, const char* pName) __attribute__ ((weak));
    PFN_vkVoidFunction gen75_GetInstanceProcAddr(VkInstance instance, const char* pName) __attribute__ ((weak));
    void gen75_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) __attribute__ ((weak));
    void gen75_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties) __attribute__ ((weak));
    void gen75_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties) __attribute__ ((weak));
    void gen75_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures) __attribute__ ((weak));
    void gen75_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties) __attribute__ ((weak));
    VkResult gen75_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* pImageFormatProperties) __attribute__ ((weak));
    VkResult gen75_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) __attribute__ ((weak));
    void gen75_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen75_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
    VkResult gen75_EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
    VkResult gen75_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
    VkResult gen75_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
    void gen75_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue) __attribute__ ((weak));
    VkResult gen75_QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence) __attribute__ ((weak));
    VkResult gen75_QueueWaitIdle(VkQueue queue) __attribute__ ((weak));
    VkResult gen75_DeviceWaitIdle(VkDevice device) __attribute__ ((weak));
    VkResult gen75_AllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory) __attribute__ ((weak));
    void gen75_FreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen75_MapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData) __attribute__ ((weak));
    void gen75_UnmapMemory(VkDevice device, VkDeviceMemory memory) __attribute__ ((weak));
    VkResult gen75_FlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) __attribute__ ((weak));
    VkResult gen75_InvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) __attribute__ ((weak));
    void gen75_GetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory, VkDeviceSize* pCommittedMemoryInBytes) __attribute__ ((weak));
    void gen75_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
    VkResult gen75_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
    void gen75_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
    VkResult gen75_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
    void gen75_GetImageSparseMemoryRequirements(VkDevice device, VkImage image, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements* pSparseMemoryRequirements) __attribute__ ((weak));
    void gen75_GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling, uint32_t* pPropertyCount, VkSparseImageFormatProperties* pProperties) __attribute__ ((weak));
    VkResult gen75_QueueBindSparse(VkQueue queue, uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence) __attribute__ ((weak));
    VkResult gen75_CreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence) __attribute__ ((weak));
    void gen75_DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen75_ResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences) __attribute__ ((weak));
    VkResult gen75_GetFenceStatus(VkDevice device, VkFence fence) __attribute__ ((weak));
    VkResult gen75_WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout) __attribute__ ((weak));
    VkResult gen75_CreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore) __attribute__ ((weak));
    void gen75_DestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen75_CreateEvent(VkDevice device, const VkEventCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkEvent* pEvent) __attribute__ ((weak));
    void gen75_DestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen75_GetEventStatus(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult gen75_SetEvent(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult gen75_ResetEvent(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult gen75_CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool) __attribute__ ((weak));
    void gen75_DestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen75_GetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* pData, VkDeviceSize stride, VkQueryResultFlags flags) __attribute__ ((weak));
    VkResult gen75_CreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer) __attribute__ ((weak));
    void gen75_DestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen75_CreateBufferView(VkDevice device, const VkBufferViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBufferView* pView) __attribute__ ((weak));
    void gen75_DestroyBufferView(VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen75_CreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage) __attribute__ ((weak));
    void gen75_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen75_GetImageSubresourceLayout(VkDevice device, VkImage image, const VkImageSubresource* pSubresource, VkSubresourceLayout* pLayout) __attribute__ ((weak));
    VkResult gen75_CreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView) __attribute__ ((weak));
    void gen75_DestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen75_CreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule) __attribute__ ((weak));
    void gen75_DestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen75_CreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineCache* pPipelineCache) __attribute__ ((weak));
    void gen75_DestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen75_GetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, size_t* pDataSize, void* pData) __attribute__ ((weak));
    VkResult gen75_MergePipelineCaches(VkDevice device, VkPipelineCache dstCache, uint32_t srcCacheCount, const VkPipelineCache* pSrcCaches) __attribute__ ((weak));
    VkResult gen75_CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) __attribute__ ((weak));
    VkResult gen75_CreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) __attribute__ ((weak));
    void gen75_DestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen75_CreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout) __attribute__ ((weak));
    void gen75_DestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen75_CreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSampler* pSampler) __attribute__ ((weak));
    void gen75_DestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen75_CreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout) __attribute__ ((weak));
    void gen75_DestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen75_CreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool) __attribute__ ((weak));
    void gen75_DestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen75_ResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags) __attribute__ ((weak));
    VkResult gen75_AllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets) __attribute__ ((weak));
    VkResult gen75_FreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets) __attribute__ ((weak));
    void gen75_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies) __attribute__ ((weak));
    VkResult gen75_CreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFramebuffer* pFramebuffer) __attribute__ ((weak));
    void gen75_DestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen75_CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass) __attribute__ ((weak));
    void gen75_DestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen75_GetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass, VkExtent2D* pGranularity) __attribute__ ((weak));
    VkResult gen75_CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool) __attribute__ ((weak));
    void gen75_DestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen75_ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags) __attribute__ ((weak));
    VkResult gen75_AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    void gen75_FreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    VkResult gen75_BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo) __attribute__ ((weak));
    VkResult gen75_EndCommandBuffer(VkCommandBuffer commandBuffer) __attribute__ ((weak));
    VkResult gen75_ResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags) __attribute__ ((weak));
    void gen75_CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) __attribute__ ((weak));
    void gen75_CmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports) __attribute__ ((weak));
    void gen75_CmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors) __attribute__ ((weak));
    void gen75_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth) __attribute__ ((weak));
    void gen75_CmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor) __attribute__ ((weak));
    void gen75_CmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4]) __attribute__ ((weak));
    void gen75_CmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds) __attribute__ ((weak));
    void gen75_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t compareMask) __attribute__ ((weak));
    void gen75_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t writeMask) __attribute__ ((weak));
    void gen75_CmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference) __attribute__ ((weak));
    void gen75_CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets) __attribute__ ((weak));
    void gen75_CmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) __attribute__ ((weak));
    void gen75_CmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets) __attribute__ ((weak));
    void gen75_CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) __attribute__ ((weak));
    void gen75_CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) __attribute__ ((weak));
    void gen75_CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) __attribute__ ((weak));
    void gen75_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) __attribute__ ((weak));
    void gen75_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) __attribute__ ((weak));
    void gen75_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset) __attribute__ ((weak));
    void gen75_CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions) __attribute__ ((weak));
    void gen75_CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions) __attribute__ ((weak));
    void gen75_CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter) __attribute__ ((weak));
    void gen75_CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
    void gen75_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
    void gen75_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* pData) __attribute__ ((weak));
    void gen75_CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data) __attribute__ ((weak));
    void gen75_CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) __attribute__ ((weak));
    void gen75_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) __attribute__ ((weak));
    void gen75_CmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment* pAttachments, uint32_t rectCount, const VkClearRect* pRects) __attribute__ ((weak));
    void gen75_CmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageResolve* pRegions) __attribute__ ((weak));
    void gen75_CmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) __attribute__ ((weak));
    void gen75_CmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) __attribute__ ((weak));
    void gen75_CmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) __attribute__ ((weak));
    void gen75_CmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) __attribute__ ((weak));
    void gen75_CmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags) __attribute__ ((weak));
    void gen75_CmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query) __attribute__ ((weak));
    void gen75_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) __attribute__ ((weak));
    void gen75_CmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query) __attribute__ ((weak));
    void gen75_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags) __attribute__ ((weak));
    void gen75_CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues) __attribute__ ((weak));
    void gen75_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents) __attribute__ ((weak));
    void gen75_CmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents) __attribute__ ((weak));
    void gen75_CmdEndRenderPass(VkCommandBuffer commandBuffer) __attribute__ ((weak));
    void gen75_CmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    void gen75_DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen75_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32* pSupported) __attribute__ ((weak));
    VkResult gen75_GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen75_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats) __attribute__ ((weak));
    VkResult gen75_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes) __attribute__ ((weak));
    VkResult gen75_CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) __attribute__ ((weak));
    void gen75_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen75_GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages) __attribute__ ((weak));
    VkResult gen75_AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex) __attribute__ ((weak));
    VkResult gen75_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) __attribute__ ((weak));
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    VkResult gen75_CreateWaylandSurfaceKHR(VkInstance instance, const VkWaylandSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    VkBool32 gen75_GetPhysicalDeviceWaylandPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, struct wl_display* display) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    VkResult gen75_CreateXlibSurfaceKHR(VkInstance instance, const VkXlibSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    VkBool32 gen75_GetPhysicalDeviceXlibPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, Display* dpy, VisualID visualID) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    VkResult gen75_CreateXcbSurfaceKHR(VkInstance instance, const VkXcbSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    VkBool32 gen75_GetPhysicalDeviceXcbPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, xcb_connection_t* connection, xcb_visualid_t visual_id) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XCB_KHR
    void gen75_GetPhysicalDeviceFeatures2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2KHR* pFeatures) __attribute__ ((weak));
    void gen75_GetPhysicalDeviceProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2KHR* pProperties) __attribute__ ((weak));
    void gen75_GetPhysicalDeviceFormatProperties2KHR(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2KHR* pFormatProperties) __attribute__ ((weak));
    VkResult gen75_GetPhysicalDeviceImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2KHR* pImageFormatInfo, VkImageFormatProperties2KHR* pImageFormatProperties) __attribute__ ((weak));
    void gen75_GetPhysicalDeviceQueueFamilyProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2KHR* pQueueFamilyProperties) __attribute__ ((weak));
    void gen75_GetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2KHR* pMemoryProperties) __attribute__ ((weak));
    void gen75_GetPhysicalDeviceSparseImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2KHR* pFormatInfo, uint32_t* pPropertyCount, VkSparseImageFormatProperties2KHR* pProperties) __attribute__ ((weak));
    void gen75_CmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites) __attribute__ ((weak));
    void gen75_TrimCommandPoolKHR(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlagsKHR flags) __attribute__ ((weak));
    void gen75_GetPhysicalDeviceExternalBufferPropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfoKHR* pExternalBufferInfo, VkExternalBufferPropertiesKHR* pExternalBufferProperties) __attribute__ ((weak));
    VkResult gen75_GetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen75_GetMemoryFdPropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBitsKHR handleType, int fd, VkMemoryFdPropertiesKHR* pMemoryFdProperties) __attribute__ ((weak));
    VkResult gen75_CreateDescriptorUpdateTemplateKHR(VkDevice device, const VkDescriptorUpdateTemplateCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplateKHR* pDescriptorUpdateTemplate) __attribute__ ((weak));
    void gen75_DestroyDescriptorUpdateTemplateKHR(VkDevice device, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen75_UpdateDescriptorSetWithTemplateKHR(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const void* pData) __attribute__ ((weak));
    void gen75_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) __attribute__ ((weak));
    VkResult gen75_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen75_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats) __attribute__ ((weak));
    void gen75_GetBufferMemoryRequirements2KHR(VkDevice device, const VkBufferMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void gen75_GetImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void gen75_GetImageSparseMemoryRequirements2KHR(VkDevice device, const VkImageSparseMemoryRequirementsInfo2KHR* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2KHR* pSparseMemoryRequirements) __attribute__ ((weak));
    VkResult gen75_CreateDmaBufImageINTEL(VkDevice device, const VkDmaBufImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,VkDeviceMemory* pMem,VkImage* pImage) __attribute__ ((weak));

  const struct anv_dispatch_table gen75_layer = {
    .CreateInstance = gen75_CreateInstance,
    .DestroyInstance = gen75_DestroyInstance,
    .EnumeratePhysicalDevices = gen75_EnumeratePhysicalDevices,
    .GetDeviceProcAddr = gen75_GetDeviceProcAddr,
    .GetInstanceProcAddr = gen75_GetInstanceProcAddr,
    .GetPhysicalDeviceProperties = gen75_GetPhysicalDeviceProperties,
    .GetPhysicalDeviceQueueFamilyProperties = gen75_GetPhysicalDeviceQueueFamilyProperties,
    .GetPhysicalDeviceMemoryProperties = gen75_GetPhysicalDeviceMemoryProperties,
    .GetPhysicalDeviceFeatures = gen75_GetPhysicalDeviceFeatures,
    .GetPhysicalDeviceFormatProperties = gen75_GetPhysicalDeviceFormatProperties,
    .GetPhysicalDeviceImageFormatProperties = gen75_GetPhysicalDeviceImageFormatProperties,
    .CreateDevice = gen75_CreateDevice,
    .DestroyDevice = gen75_DestroyDevice,
    .EnumerateInstanceLayerProperties = gen75_EnumerateInstanceLayerProperties,
    .EnumerateInstanceExtensionProperties = gen75_EnumerateInstanceExtensionProperties,
    .EnumerateDeviceLayerProperties = gen75_EnumerateDeviceLayerProperties,
    .EnumerateDeviceExtensionProperties = gen75_EnumerateDeviceExtensionProperties,
    .GetDeviceQueue = gen75_GetDeviceQueue,
    .QueueSubmit = gen75_QueueSubmit,
    .QueueWaitIdle = gen75_QueueWaitIdle,
    .DeviceWaitIdle = gen75_DeviceWaitIdle,
    .AllocateMemory = gen75_AllocateMemory,
    .FreeMemory = gen75_FreeMemory,
    .MapMemory = gen75_MapMemory,
    .UnmapMemory = gen75_UnmapMemory,
    .FlushMappedMemoryRanges = gen75_FlushMappedMemoryRanges,
    .InvalidateMappedMemoryRanges = gen75_InvalidateMappedMemoryRanges,
    .GetDeviceMemoryCommitment = gen75_GetDeviceMemoryCommitment,
    .GetBufferMemoryRequirements = gen75_GetBufferMemoryRequirements,
    .BindBufferMemory = gen75_BindBufferMemory,
    .GetImageMemoryRequirements = gen75_GetImageMemoryRequirements,
    .BindImageMemory = gen75_BindImageMemory,
    .GetImageSparseMemoryRequirements = gen75_GetImageSparseMemoryRequirements,
    .GetPhysicalDeviceSparseImageFormatProperties = gen75_GetPhysicalDeviceSparseImageFormatProperties,
    .QueueBindSparse = gen75_QueueBindSparse,
    .CreateFence = gen75_CreateFence,
    .DestroyFence = gen75_DestroyFence,
    .ResetFences = gen75_ResetFences,
    .GetFenceStatus = gen75_GetFenceStatus,
    .WaitForFences = gen75_WaitForFences,
    .CreateSemaphore = gen75_CreateSemaphore,
    .DestroySemaphore = gen75_DestroySemaphore,
    .CreateEvent = gen75_CreateEvent,
    .DestroyEvent = gen75_DestroyEvent,
    .GetEventStatus = gen75_GetEventStatus,
    .SetEvent = gen75_SetEvent,
    .ResetEvent = gen75_ResetEvent,
    .CreateQueryPool = gen75_CreateQueryPool,
    .DestroyQueryPool = gen75_DestroyQueryPool,
    .GetQueryPoolResults = gen75_GetQueryPoolResults,
    .CreateBuffer = gen75_CreateBuffer,
    .DestroyBuffer = gen75_DestroyBuffer,
    .CreateBufferView = gen75_CreateBufferView,
    .DestroyBufferView = gen75_DestroyBufferView,
    .CreateImage = gen75_CreateImage,
    .DestroyImage = gen75_DestroyImage,
    .GetImageSubresourceLayout = gen75_GetImageSubresourceLayout,
    .CreateImageView = gen75_CreateImageView,
    .DestroyImageView = gen75_DestroyImageView,
    .CreateShaderModule = gen75_CreateShaderModule,
    .DestroyShaderModule = gen75_DestroyShaderModule,
    .CreatePipelineCache = gen75_CreatePipelineCache,
    .DestroyPipelineCache = gen75_DestroyPipelineCache,
    .GetPipelineCacheData = gen75_GetPipelineCacheData,
    .MergePipelineCaches = gen75_MergePipelineCaches,
    .CreateGraphicsPipelines = gen75_CreateGraphicsPipelines,
    .CreateComputePipelines = gen75_CreateComputePipelines,
    .DestroyPipeline = gen75_DestroyPipeline,
    .CreatePipelineLayout = gen75_CreatePipelineLayout,
    .DestroyPipelineLayout = gen75_DestroyPipelineLayout,
    .CreateSampler = gen75_CreateSampler,
    .DestroySampler = gen75_DestroySampler,
    .CreateDescriptorSetLayout = gen75_CreateDescriptorSetLayout,
    .DestroyDescriptorSetLayout = gen75_DestroyDescriptorSetLayout,
    .CreateDescriptorPool = gen75_CreateDescriptorPool,
    .DestroyDescriptorPool = gen75_DestroyDescriptorPool,
    .ResetDescriptorPool = gen75_ResetDescriptorPool,
    .AllocateDescriptorSets = gen75_AllocateDescriptorSets,
    .FreeDescriptorSets = gen75_FreeDescriptorSets,
    .UpdateDescriptorSets = gen75_UpdateDescriptorSets,
    .CreateFramebuffer = gen75_CreateFramebuffer,
    .DestroyFramebuffer = gen75_DestroyFramebuffer,
    .CreateRenderPass = gen75_CreateRenderPass,
    .DestroyRenderPass = gen75_DestroyRenderPass,
    .GetRenderAreaGranularity = gen75_GetRenderAreaGranularity,
    .CreateCommandPool = gen75_CreateCommandPool,
    .DestroyCommandPool = gen75_DestroyCommandPool,
    .ResetCommandPool = gen75_ResetCommandPool,
    .AllocateCommandBuffers = gen75_AllocateCommandBuffers,
    .FreeCommandBuffers = gen75_FreeCommandBuffers,
    .BeginCommandBuffer = gen75_BeginCommandBuffer,
    .EndCommandBuffer = gen75_EndCommandBuffer,
    .ResetCommandBuffer = gen75_ResetCommandBuffer,
    .CmdBindPipeline = gen75_CmdBindPipeline,
    .CmdSetViewport = gen75_CmdSetViewport,
    .CmdSetScissor = gen75_CmdSetScissor,
    .CmdSetLineWidth = gen75_CmdSetLineWidth,
    .CmdSetDepthBias = gen75_CmdSetDepthBias,
    .CmdSetBlendConstants = gen75_CmdSetBlendConstants,
    .CmdSetDepthBounds = gen75_CmdSetDepthBounds,
    .CmdSetStencilCompareMask = gen75_CmdSetStencilCompareMask,
    .CmdSetStencilWriteMask = gen75_CmdSetStencilWriteMask,
    .CmdSetStencilReference = gen75_CmdSetStencilReference,
    .CmdBindDescriptorSets = gen75_CmdBindDescriptorSets,
    .CmdBindIndexBuffer = gen75_CmdBindIndexBuffer,
    .CmdBindVertexBuffers = gen75_CmdBindVertexBuffers,
    .CmdDraw = gen75_CmdDraw,
    .CmdDrawIndexed = gen75_CmdDrawIndexed,
    .CmdDrawIndirect = gen75_CmdDrawIndirect,
    .CmdDrawIndexedIndirect = gen75_CmdDrawIndexedIndirect,
    .CmdDispatch = gen75_CmdDispatch,
    .CmdDispatchIndirect = gen75_CmdDispatchIndirect,
    .CmdCopyBuffer = gen75_CmdCopyBuffer,
    .CmdCopyImage = gen75_CmdCopyImage,
    .CmdBlitImage = gen75_CmdBlitImage,
    .CmdCopyBufferToImage = gen75_CmdCopyBufferToImage,
    .CmdCopyImageToBuffer = gen75_CmdCopyImageToBuffer,
    .CmdUpdateBuffer = gen75_CmdUpdateBuffer,
    .CmdFillBuffer = gen75_CmdFillBuffer,
    .CmdClearColorImage = gen75_CmdClearColorImage,
    .CmdClearDepthStencilImage = gen75_CmdClearDepthStencilImage,
    .CmdClearAttachments = gen75_CmdClearAttachments,
    .CmdResolveImage = gen75_CmdResolveImage,
    .CmdSetEvent = gen75_CmdSetEvent,
    .CmdResetEvent = gen75_CmdResetEvent,
    .CmdWaitEvents = gen75_CmdWaitEvents,
    .CmdPipelineBarrier = gen75_CmdPipelineBarrier,
    .CmdBeginQuery = gen75_CmdBeginQuery,
    .CmdEndQuery = gen75_CmdEndQuery,
    .CmdResetQueryPool = gen75_CmdResetQueryPool,
    .CmdWriteTimestamp = gen75_CmdWriteTimestamp,
    .CmdCopyQueryPoolResults = gen75_CmdCopyQueryPoolResults,
    .CmdPushConstants = gen75_CmdPushConstants,
    .CmdBeginRenderPass = gen75_CmdBeginRenderPass,
    .CmdNextSubpass = gen75_CmdNextSubpass,
    .CmdEndRenderPass = gen75_CmdEndRenderPass,
    .CmdExecuteCommands = gen75_CmdExecuteCommands,
    .DestroySurfaceKHR = gen75_DestroySurfaceKHR,
    .GetPhysicalDeviceSurfaceSupportKHR = gen75_GetPhysicalDeviceSurfaceSupportKHR,
    .GetPhysicalDeviceSurfaceCapabilitiesKHR = gen75_GetPhysicalDeviceSurfaceCapabilitiesKHR,
    .GetPhysicalDeviceSurfaceFormatsKHR = gen75_GetPhysicalDeviceSurfaceFormatsKHR,
    .GetPhysicalDeviceSurfacePresentModesKHR = gen75_GetPhysicalDeviceSurfacePresentModesKHR,
    .CreateSwapchainKHR = gen75_CreateSwapchainKHR,
    .DestroySwapchainKHR = gen75_DestroySwapchainKHR,
    .GetSwapchainImagesKHR = gen75_GetSwapchainImagesKHR,
    .AcquireNextImageKHR = gen75_AcquireNextImageKHR,
    .QueuePresentKHR = gen75_QueuePresentKHR,
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .CreateWaylandSurfaceKHR = gen75_CreateWaylandSurfaceKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .GetPhysicalDeviceWaylandPresentationSupportKHR = gen75_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .CreateXlibSurfaceKHR = gen75_CreateXlibSurfaceKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .GetPhysicalDeviceXlibPresentationSupportKHR = gen75_GetPhysicalDeviceXlibPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .CreateXcbSurfaceKHR = gen75_CreateXcbSurfaceKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .GetPhysicalDeviceXcbPresentationSupportKHR = gen75_GetPhysicalDeviceXcbPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
    .GetPhysicalDeviceFeatures2KHR = gen75_GetPhysicalDeviceFeatures2KHR,
    .GetPhysicalDeviceProperties2KHR = gen75_GetPhysicalDeviceProperties2KHR,
    .GetPhysicalDeviceFormatProperties2KHR = gen75_GetPhysicalDeviceFormatProperties2KHR,
    .GetPhysicalDeviceImageFormatProperties2KHR = gen75_GetPhysicalDeviceImageFormatProperties2KHR,
    .GetPhysicalDeviceQueueFamilyProperties2KHR = gen75_GetPhysicalDeviceQueueFamilyProperties2KHR,
    .GetPhysicalDeviceMemoryProperties2KHR = gen75_GetPhysicalDeviceMemoryProperties2KHR,
    .GetPhysicalDeviceSparseImageFormatProperties2KHR = gen75_GetPhysicalDeviceSparseImageFormatProperties2KHR,
    .CmdPushDescriptorSetKHR = gen75_CmdPushDescriptorSetKHR,
    .TrimCommandPoolKHR = gen75_TrimCommandPoolKHR,
    .GetPhysicalDeviceExternalBufferPropertiesKHR = gen75_GetPhysicalDeviceExternalBufferPropertiesKHR,
    .GetMemoryFdKHR = gen75_GetMemoryFdKHR,
    .GetMemoryFdPropertiesKHR = gen75_GetMemoryFdPropertiesKHR,
    .CreateDescriptorUpdateTemplateKHR = gen75_CreateDescriptorUpdateTemplateKHR,
    .DestroyDescriptorUpdateTemplateKHR = gen75_DestroyDescriptorUpdateTemplateKHR,
    .UpdateDescriptorSetWithTemplateKHR = gen75_UpdateDescriptorSetWithTemplateKHR,
    .CmdPushDescriptorSetWithTemplateKHR = gen75_CmdPushDescriptorSetWithTemplateKHR,
    .GetPhysicalDeviceSurfaceCapabilities2KHR = gen75_GetPhysicalDeviceSurfaceCapabilities2KHR,
    .GetPhysicalDeviceSurfaceFormats2KHR = gen75_GetPhysicalDeviceSurfaceFormats2KHR,
    .GetBufferMemoryRequirements2KHR = gen75_GetBufferMemoryRequirements2KHR,
    .GetImageMemoryRequirements2KHR = gen75_GetImageMemoryRequirements2KHR,
    .GetImageSparseMemoryRequirements2KHR = gen75_GetImageSparseMemoryRequirements2KHR,
    .CreateDmaBufImageINTEL = gen75_CreateDmaBufImageINTEL,
  };
    VkResult gen8_CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) __attribute__ ((weak));
    void gen8_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen8_EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) __attribute__ ((weak));
    PFN_vkVoidFunction gen8_GetDeviceProcAddr(VkDevice device, const char* pName) __attribute__ ((weak));
    PFN_vkVoidFunction gen8_GetInstanceProcAddr(VkInstance instance, const char* pName) __attribute__ ((weak));
    void gen8_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) __attribute__ ((weak));
    void gen8_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties) __attribute__ ((weak));
    void gen8_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties) __attribute__ ((weak));
    void gen8_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures) __attribute__ ((weak));
    void gen8_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties) __attribute__ ((weak));
    VkResult gen8_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* pImageFormatProperties) __attribute__ ((weak));
    VkResult gen8_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) __attribute__ ((weak));
    void gen8_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen8_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
    VkResult gen8_EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
    VkResult gen8_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
    VkResult gen8_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
    void gen8_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue) __attribute__ ((weak));
    VkResult gen8_QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence) __attribute__ ((weak));
    VkResult gen8_QueueWaitIdle(VkQueue queue) __attribute__ ((weak));
    VkResult gen8_DeviceWaitIdle(VkDevice device) __attribute__ ((weak));
    VkResult gen8_AllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory) __attribute__ ((weak));
    void gen8_FreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen8_MapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData) __attribute__ ((weak));
    void gen8_UnmapMemory(VkDevice device, VkDeviceMemory memory) __attribute__ ((weak));
    VkResult gen8_FlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) __attribute__ ((weak));
    VkResult gen8_InvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) __attribute__ ((weak));
    void gen8_GetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory, VkDeviceSize* pCommittedMemoryInBytes) __attribute__ ((weak));
    void gen8_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
    VkResult gen8_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
    void gen8_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
    VkResult gen8_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
    void gen8_GetImageSparseMemoryRequirements(VkDevice device, VkImage image, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements* pSparseMemoryRequirements) __attribute__ ((weak));
    void gen8_GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling, uint32_t* pPropertyCount, VkSparseImageFormatProperties* pProperties) __attribute__ ((weak));
    VkResult gen8_QueueBindSparse(VkQueue queue, uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence) __attribute__ ((weak));
    VkResult gen8_CreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence) __attribute__ ((weak));
    void gen8_DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen8_ResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences) __attribute__ ((weak));
    VkResult gen8_GetFenceStatus(VkDevice device, VkFence fence) __attribute__ ((weak));
    VkResult gen8_WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout) __attribute__ ((weak));
    VkResult gen8_CreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore) __attribute__ ((weak));
    void gen8_DestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen8_CreateEvent(VkDevice device, const VkEventCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkEvent* pEvent) __attribute__ ((weak));
    void gen8_DestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen8_GetEventStatus(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult gen8_SetEvent(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult gen8_ResetEvent(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult gen8_CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool) __attribute__ ((weak));
    void gen8_DestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen8_GetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* pData, VkDeviceSize stride, VkQueryResultFlags flags) __attribute__ ((weak));
    VkResult gen8_CreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer) __attribute__ ((weak));
    void gen8_DestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen8_CreateBufferView(VkDevice device, const VkBufferViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBufferView* pView) __attribute__ ((weak));
    void gen8_DestroyBufferView(VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen8_CreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage) __attribute__ ((weak));
    void gen8_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen8_GetImageSubresourceLayout(VkDevice device, VkImage image, const VkImageSubresource* pSubresource, VkSubresourceLayout* pLayout) __attribute__ ((weak));
    VkResult gen8_CreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView) __attribute__ ((weak));
    void gen8_DestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen8_CreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule) __attribute__ ((weak));
    void gen8_DestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen8_CreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineCache* pPipelineCache) __attribute__ ((weak));
    void gen8_DestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen8_GetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, size_t* pDataSize, void* pData) __attribute__ ((weak));
    VkResult gen8_MergePipelineCaches(VkDevice device, VkPipelineCache dstCache, uint32_t srcCacheCount, const VkPipelineCache* pSrcCaches) __attribute__ ((weak));
    VkResult gen8_CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) __attribute__ ((weak));
    VkResult gen8_CreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) __attribute__ ((weak));
    void gen8_DestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen8_CreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout) __attribute__ ((weak));
    void gen8_DestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen8_CreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSampler* pSampler) __attribute__ ((weak));
    void gen8_DestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen8_CreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout) __attribute__ ((weak));
    void gen8_DestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen8_CreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool) __attribute__ ((weak));
    void gen8_DestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen8_ResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags) __attribute__ ((weak));
    VkResult gen8_AllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets) __attribute__ ((weak));
    VkResult gen8_FreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets) __attribute__ ((weak));
    void gen8_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies) __attribute__ ((weak));
    VkResult gen8_CreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFramebuffer* pFramebuffer) __attribute__ ((weak));
    void gen8_DestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen8_CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass) __attribute__ ((weak));
    void gen8_DestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen8_GetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass, VkExtent2D* pGranularity) __attribute__ ((weak));
    VkResult gen8_CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool) __attribute__ ((weak));
    void gen8_DestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen8_ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags) __attribute__ ((weak));
    VkResult gen8_AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    void gen8_FreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    VkResult gen8_BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo) __attribute__ ((weak));
    VkResult gen8_EndCommandBuffer(VkCommandBuffer commandBuffer) __attribute__ ((weak));
    VkResult gen8_ResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags) __attribute__ ((weak));
    void gen8_CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) __attribute__ ((weak));
    void gen8_CmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports) __attribute__ ((weak));
    void gen8_CmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors) __attribute__ ((weak));
    void gen8_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth) __attribute__ ((weak));
    void gen8_CmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor) __attribute__ ((weak));
    void gen8_CmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4]) __attribute__ ((weak));
    void gen8_CmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds) __attribute__ ((weak));
    void gen8_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t compareMask) __attribute__ ((weak));
    void gen8_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t writeMask) __attribute__ ((weak));
    void gen8_CmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference) __attribute__ ((weak));
    void gen8_CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets) __attribute__ ((weak));
    void gen8_CmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) __attribute__ ((weak));
    void gen8_CmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets) __attribute__ ((weak));
    void gen8_CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) __attribute__ ((weak));
    void gen8_CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) __attribute__ ((weak));
    void gen8_CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) __attribute__ ((weak));
    void gen8_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) __attribute__ ((weak));
    void gen8_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) __attribute__ ((weak));
    void gen8_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset) __attribute__ ((weak));
    void gen8_CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions) __attribute__ ((weak));
    void gen8_CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions) __attribute__ ((weak));
    void gen8_CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter) __attribute__ ((weak));
    void gen8_CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
    void gen8_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
    void gen8_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* pData) __attribute__ ((weak));
    void gen8_CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data) __attribute__ ((weak));
    void gen8_CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) __attribute__ ((weak));
    void gen8_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) __attribute__ ((weak));
    void gen8_CmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment* pAttachments, uint32_t rectCount, const VkClearRect* pRects) __attribute__ ((weak));
    void gen8_CmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageResolve* pRegions) __attribute__ ((weak));
    void gen8_CmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) __attribute__ ((weak));
    void gen8_CmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) __attribute__ ((weak));
    void gen8_CmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) __attribute__ ((weak));
    void gen8_CmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) __attribute__ ((weak));
    void gen8_CmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags) __attribute__ ((weak));
    void gen8_CmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query) __attribute__ ((weak));
    void gen8_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) __attribute__ ((weak));
    void gen8_CmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query) __attribute__ ((weak));
    void gen8_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags) __attribute__ ((weak));
    void gen8_CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues) __attribute__ ((weak));
    void gen8_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents) __attribute__ ((weak));
    void gen8_CmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents) __attribute__ ((weak));
    void gen8_CmdEndRenderPass(VkCommandBuffer commandBuffer) __attribute__ ((weak));
    void gen8_CmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    void gen8_DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen8_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32* pSupported) __attribute__ ((weak));
    VkResult gen8_GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen8_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats) __attribute__ ((weak));
    VkResult gen8_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes) __attribute__ ((weak));
    VkResult gen8_CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) __attribute__ ((weak));
    void gen8_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen8_GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages) __attribute__ ((weak));
    VkResult gen8_AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex) __attribute__ ((weak));
    VkResult gen8_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) __attribute__ ((weak));
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    VkResult gen8_CreateWaylandSurfaceKHR(VkInstance instance, const VkWaylandSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    VkBool32 gen8_GetPhysicalDeviceWaylandPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, struct wl_display* display) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    VkResult gen8_CreateXlibSurfaceKHR(VkInstance instance, const VkXlibSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    VkBool32 gen8_GetPhysicalDeviceXlibPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, Display* dpy, VisualID visualID) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    VkResult gen8_CreateXcbSurfaceKHR(VkInstance instance, const VkXcbSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    VkBool32 gen8_GetPhysicalDeviceXcbPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, xcb_connection_t* connection, xcb_visualid_t visual_id) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XCB_KHR
    void gen8_GetPhysicalDeviceFeatures2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2KHR* pFeatures) __attribute__ ((weak));
    void gen8_GetPhysicalDeviceProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2KHR* pProperties) __attribute__ ((weak));
    void gen8_GetPhysicalDeviceFormatProperties2KHR(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2KHR* pFormatProperties) __attribute__ ((weak));
    VkResult gen8_GetPhysicalDeviceImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2KHR* pImageFormatInfo, VkImageFormatProperties2KHR* pImageFormatProperties) __attribute__ ((weak));
    void gen8_GetPhysicalDeviceQueueFamilyProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2KHR* pQueueFamilyProperties) __attribute__ ((weak));
    void gen8_GetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2KHR* pMemoryProperties) __attribute__ ((weak));
    void gen8_GetPhysicalDeviceSparseImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2KHR* pFormatInfo, uint32_t* pPropertyCount, VkSparseImageFormatProperties2KHR* pProperties) __attribute__ ((weak));
    void gen8_CmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites) __attribute__ ((weak));
    void gen8_TrimCommandPoolKHR(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlagsKHR flags) __attribute__ ((weak));
    void gen8_GetPhysicalDeviceExternalBufferPropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfoKHR* pExternalBufferInfo, VkExternalBufferPropertiesKHR* pExternalBufferProperties) __attribute__ ((weak));
    VkResult gen8_GetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen8_GetMemoryFdPropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBitsKHR handleType, int fd, VkMemoryFdPropertiesKHR* pMemoryFdProperties) __attribute__ ((weak));
    VkResult gen8_CreateDescriptorUpdateTemplateKHR(VkDevice device, const VkDescriptorUpdateTemplateCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplateKHR* pDescriptorUpdateTemplate) __attribute__ ((weak));
    void gen8_DestroyDescriptorUpdateTemplateKHR(VkDevice device, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen8_UpdateDescriptorSetWithTemplateKHR(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const void* pData) __attribute__ ((weak));
    void gen8_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) __attribute__ ((weak));
    VkResult gen8_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen8_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats) __attribute__ ((weak));
    void gen8_GetBufferMemoryRequirements2KHR(VkDevice device, const VkBufferMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void gen8_GetImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void gen8_GetImageSparseMemoryRequirements2KHR(VkDevice device, const VkImageSparseMemoryRequirementsInfo2KHR* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2KHR* pSparseMemoryRequirements) __attribute__ ((weak));
    VkResult gen8_CreateDmaBufImageINTEL(VkDevice device, const VkDmaBufImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,VkDeviceMemory* pMem,VkImage* pImage) __attribute__ ((weak));

  const struct anv_dispatch_table gen8_layer = {
    .CreateInstance = gen8_CreateInstance,
    .DestroyInstance = gen8_DestroyInstance,
    .EnumeratePhysicalDevices = gen8_EnumeratePhysicalDevices,
    .GetDeviceProcAddr = gen8_GetDeviceProcAddr,
    .GetInstanceProcAddr = gen8_GetInstanceProcAddr,
    .GetPhysicalDeviceProperties = gen8_GetPhysicalDeviceProperties,
    .GetPhysicalDeviceQueueFamilyProperties = gen8_GetPhysicalDeviceQueueFamilyProperties,
    .GetPhysicalDeviceMemoryProperties = gen8_GetPhysicalDeviceMemoryProperties,
    .GetPhysicalDeviceFeatures = gen8_GetPhysicalDeviceFeatures,
    .GetPhysicalDeviceFormatProperties = gen8_GetPhysicalDeviceFormatProperties,
    .GetPhysicalDeviceImageFormatProperties = gen8_GetPhysicalDeviceImageFormatProperties,
    .CreateDevice = gen8_CreateDevice,
    .DestroyDevice = gen8_DestroyDevice,
    .EnumerateInstanceLayerProperties = gen8_EnumerateInstanceLayerProperties,
    .EnumerateInstanceExtensionProperties = gen8_EnumerateInstanceExtensionProperties,
    .EnumerateDeviceLayerProperties = gen8_EnumerateDeviceLayerProperties,
    .EnumerateDeviceExtensionProperties = gen8_EnumerateDeviceExtensionProperties,
    .GetDeviceQueue = gen8_GetDeviceQueue,
    .QueueSubmit = gen8_QueueSubmit,
    .QueueWaitIdle = gen8_QueueWaitIdle,
    .DeviceWaitIdle = gen8_DeviceWaitIdle,
    .AllocateMemory = gen8_AllocateMemory,
    .FreeMemory = gen8_FreeMemory,
    .MapMemory = gen8_MapMemory,
    .UnmapMemory = gen8_UnmapMemory,
    .FlushMappedMemoryRanges = gen8_FlushMappedMemoryRanges,
    .InvalidateMappedMemoryRanges = gen8_InvalidateMappedMemoryRanges,
    .GetDeviceMemoryCommitment = gen8_GetDeviceMemoryCommitment,
    .GetBufferMemoryRequirements = gen8_GetBufferMemoryRequirements,
    .BindBufferMemory = gen8_BindBufferMemory,
    .GetImageMemoryRequirements = gen8_GetImageMemoryRequirements,
    .BindImageMemory = gen8_BindImageMemory,
    .GetImageSparseMemoryRequirements = gen8_GetImageSparseMemoryRequirements,
    .GetPhysicalDeviceSparseImageFormatProperties = gen8_GetPhysicalDeviceSparseImageFormatProperties,
    .QueueBindSparse = gen8_QueueBindSparse,
    .CreateFence = gen8_CreateFence,
    .DestroyFence = gen8_DestroyFence,
    .ResetFences = gen8_ResetFences,
    .GetFenceStatus = gen8_GetFenceStatus,
    .WaitForFences = gen8_WaitForFences,
    .CreateSemaphore = gen8_CreateSemaphore,
    .DestroySemaphore = gen8_DestroySemaphore,
    .CreateEvent = gen8_CreateEvent,
    .DestroyEvent = gen8_DestroyEvent,
    .GetEventStatus = gen8_GetEventStatus,
    .SetEvent = gen8_SetEvent,
    .ResetEvent = gen8_ResetEvent,
    .CreateQueryPool = gen8_CreateQueryPool,
    .DestroyQueryPool = gen8_DestroyQueryPool,
    .GetQueryPoolResults = gen8_GetQueryPoolResults,
    .CreateBuffer = gen8_CreateBuffer,
    .DestroyBuffer = gen8_DestroyBuffer,
    .CreateBufferView = gen8_CreateBufferView,
    .DestroyBufferView = gen8_DestroyBufferView,
    .CreateImage = gen8_CreateImage,
    .DestroyImage = gen8_DestroyImage,
    .GetImageSubresourceLayout = gen8_GetImageSubresourceLayout,
    .CreateImageView = gen8_CreateImageView,
    .DestroyImageView = gen8_DestroyImageView,
    .CreateShaderModule = gen8_CreateShaderModule,
    .DestroyShaderModule = gen8_DestroyShaderModule,
    .CreatePipelineCache = gen8_CreatePipelineCache,
    .DestroyPipelineCache = gen8_DestroyPipelineCache,
    .GetPipelineCacheData = gen8_GetPipelineCacheData,
    .MergePipelineCaches = gen8_MergePipelineCaches,
    .CreateGraphicsPipelines = gen8_CreateGraphicsPipelines,
    .CreateComputePipelines = gen8_CreateComputePipelines,
    .DestroyPipeline = gen8_DestroyPipeline,
    .CreatePipelineLayout = gen8_CreatePipelineLayout,
    .DestroyPipelineLayout = gen8_DestroyPipelineLayout,
    .CreateSampler = gen8_CreateSampler,
    .DestroySampler = gen8_DestroySampler,
    .CreateDescriptorSetLayout = gen8_CreateDescriptorSetLayout,
    .DestroyDescriptorSetLayout = gen8_DestroyDescriptorSetLayout,
    .CreateDescriptorPool = gen8_CreateDescriptorPool,
    .DestroyDescriptorPool = gen8_DestroyDescriptorPool,
    .ResetDescriptorPool = gen8_ResetDescriptorPool,
    .AllocateDescriptorSets = gen8_AllocateDescriptorSets,
    .FreeDescriptorSets = gen8_FreeDescriptorSets,
    .UpdateDescriptorSets = gen8_UpdateDescriptorSets,
    .CreateFramebuffer = gen8_CreateFramebuffer,
    .DestroyFramebuffer = gen8_DestroyFramebuffer,
    .CreateRenderPass = gen8_CreateRenderPass,
    .DestroyRenderPass = gen8_DestroyRenderPass,
    .GetRenderAreaGranularity = gen8_GetRenderAreaGranularity,
    .CreateCommandPool = gen8_CreateCommandPool,
    .DestroyCommandPool = gen8_DestroyCommandPool,
    .ResetCommandPool = gen8_ResetCommandPool,
    .AllocateCommandBuffers = gen8_AllocateCommandBuffers,
    .FreeCommandBuffers = gen8_FreeCommandBuffers,
    .BeginCommandBuffer = gen8_BeginCommandBuffer,
    .EndCommandBuffer = gen8_EndCommandBuffer,
    .ResetCommandBuffer = gen8_ResetCommandBuffer,
    .CmdBindPipeline = gen8_CmdBindPipeline,
    .CmdSetViewport = gen8_CmdSetViewport,
    .CmdSetScissor = gen8_CmdSetScissor,
    .CmdSetLineWidth = gen8_CmdSetLineWidth,
    .CmdSetDepthBias = gen8_CmdSetDepthBias,
    .CmdSetBlendConstants = gen8_CmdSetBlendConstants,
    .CmdSetDepthBounds = gen8_CmdSetDepthBounds,
    .CmdSetStencilCompareMask = gen8_CmdSetStencilCompareMask,
    .CmdSetStencilWriteMask = gen8_CmdSetStencilWriteMask,
    .CmdSetStencilReference = gen8_CmdSetStencilReference,
    .CmdBindDescriptorSets = gen8_CmdBindDescriptorSets,
    .CmdBindIndexBuffer = gen8_CmdBindIndexBuffer,
    .CmdBindVertexBuffers = gen8_CmdBindVertexBuffers,
    .CmdDraw = gen8_CmdDraw,
    .CmdDrawIndexed = gen8_CmdDrawIndexed,
    .CmdDrawIndirect = gen8_CmdDrawIndirect,
    .CmdDrawIndexedIndirect = gen8_CmdDrawIndexedIndirect,
    .CmdDispatch = gen8_CmdDispatch,
    .CmdDispatchIndirect = gen8_CmdDispatchIndirect,
    .CmdCopyBuffer = gen8_CmdCopyBuffer,
    .CmdCopyImage = gen8_CmdCopyImage,
    .CmdBlitImage = gen8_CmdBlitImage,
    .CmdCopyBufferToImage = gen8_CmdCopyBufferToImage,
    .CmdCopyImageToBuffer = gen8_CmdCopyImageToBuffer,
    .CmdUpdateBuffer = gen8_CmdUpdateBuffer,
    .CmdFillBuffer = gen8_CmdFillBuffer,
    .CmdClearColorImage = gen8_CmdClearColorImage,
    .CmdClearDepthStencilImage = gen8_CmdClearDepthStencilImage,
    .CmdClearAttachments = gen8_CmdClearAttachments,
    .CmdResolveImage = gen8_CmdResolveImage,
    .CmdSetEvent = gen8_CmdSetEvent,
    .CmdResetEvent = gen8_CmdResetEvent,
    .CmdWaitEvents = gen8_CmdWaitEvents,
    .CmdPipelineBarrier = gen8_CmdPipelineBarrier,
    .CmdBeginQuery = gen8_CmdBeginQuery,
    .CmdEndQuery = gen8_CmdEndQuery,
    .CmdResetQueryPool = gen8_CmdResetQueryPool,
    .CmdWriteTimestamp = gen8_CmdWriteTimestamp,
    .CmdCopyQueryPoolResults = gen8_CmdCopyQueryPoolResults,
    .CmdPushConstants = gen8_CmdPushConstants,
    .CmdBeginRenderPass = gen8_CmdBeginRenderPass,
    .CmdNextSubpass = gen8_CmdNextSubpass,
    .CmdEndRenderPass = gen8_CmdEndRenderPass,
    .CmdExecuteCommands = gen8_CmdExecuteCommands,
    .DestroySurfaceKHR = gen8_DestroySurfaceKHR,
    .GetPhysicalDeviceSurfaceSupportKHR = gen8_GetPhysicalDeviceSurfaceSupportKHR,
    .GetPhysicalDeviceSurfaceCapabilitiesKHR = gen8_GetPhysicalDeviceSurfaceCapabilitiesKHR,
    .GetPhysicalDeviceSurfaceFormatsKHR = gen8_GetPhysicalDeviceSurfaceFormatsKHR,
    .GetPhysicalDeviceSurfacePresentModesKHR = gen8_GetPhysicalDeviceSurfacePresentModesKHR,
    .CreateSwapchainKHR = gen8_CreateSwapchainKHR,
    .DestroySwapchainKHR = gen8_DestroySwapchainKHR,
    .GetSwapchainImagesKHR = gen8_GetSwapchainImagesKHR,
    .AcquireNextImageKHR = gen8_AcquireNextImageKHR,
    .QueuePresentKHR = gen8_QueuePresentKHR,
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .CreateWaylandSurfaceKHR = gen8_CreateWaylandSurfaceKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .GetPhysicalDeviceWaylandPresentationSupportKHR = gen8_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .CreateXlibSurfaceKHR = gen8_CreateXlibSurfaceKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .GetPhysicalDeviceXlibPresentationSupportKHR = gen8_GetPhysicalDeviceXlibPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .CreateXcbSurfaceKHR = gen8_CreateXcbSurfaceKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .GetPhysicalDeviceXcbPresentationSupportKHR = gen8_GetPhysicalDeviceXcbPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
    .GetPhysicalDeviceFeatures2KHR = gen8_GetPhysicalDeviceFeatures2KHR,
    .GetPhysicalDeviceProperties2KHR = gen8_GetPhysicalDeviceProperties2KHR,
    .GetPhysicalDeviceFormatProperties2KHR = gen8_GetPhysicalDeviceFormatProperties2KHR,
    .GetPhysicalDeviceImageFormatProperties2KHR = gen8_GetPhysicalDeviceImageFormatProperties2KHR,
    .GetPhysicalDeviceQueueFamilyProperties2KHR = gen8_GetPhysicalDeviceQueueFamilyProperties2KHR,
    .GetPhysicalDeviceMemoryProperties2KHR = gen8_GetPhysicalDeviceMemoryProperties2KHR,
    .GetPhysicalDeviceSparseImageFormatProperties2KHR = gen8_GetPhysicalDeviceSparseImageFormatProperties2KHR,
    .CmdPushDescriptorSetKHR = gen8_CmdPushDescriptorSetKHR,
    .TrimCommandPoolKHR = gen8_TrimCommandPoolKHR,
    .GetPhysicalDeviceExternalBufferPropertiesKHR = gen8_GetPhysicalDeviceExternalBufferPropertiesKHR,
    .GetMemoryFdKHR = gen8_GetMemoryFdKHR,
    .GetMemoryFdPropertiesKHR = gen8_GetMemoryFdPropertiesKHR,
    .CreateDescriptorUpdateTemplateKHR = gen8_CreateDescriptorUpdateTemplateKHR,
    .DestroyDescriptorUpdateTemplateKHR = gen8_DestroyDescriptorUpdateTemplateKHR,
    .UpdateDescriptorSetWithTemplateKHR = gen8_UpdateDescriptorSetWithTemplateKHR,
    .CmdPushDescriptorSetWithTemplateKHR = gen8_CmdPushDescriptorSetWithTemplateKHR,
    .GetPhysicalDeviceSurfaceCapabilities2KHR = gen8_GetPhysicalDeviceSurfaceCapabilities2KHR,
    .GetPhysicalDeviceSurfaceFormats2KHR = gen8_GetPhysicalDeviceSurfaceFormats2KHR,
    .GetBufferMemoryRequirements2KHR = gen8_GetBufferMemoryRequirements2KHR,
    .GetImageMemoryRequirements2KHR = gen8_GetImageMemoryRequirements2KHR,
    .GetImageSparseMemoryRequirements2KHR = gen8_GetImageSparseMemoryRequirements2KHR,
    .CreateDmaBufImageINTEL = gen8_CreateDmaBufImageINTEL,
  };
    VkResult gen9_CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) __attribute__ ((weak));
    void gen9_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen9_EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) __attribute__ ((weak));
    PFN_vkVoidFunction gen9_GetDeviceProcAddr(VkDevice device, const char* pName) __attribute__ ((weak));
    PFN_vkVoidFunction gen9_GetInstanceProcAddr(VkInstance instance, const char* pName) __attribute__ ((weak));
    void gen9_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) __attribute__ ((weak));
    void gen9_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties) __attribute__ ((weak));
    void gen9_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties) __attribute__ ((weak));
    void gen9_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures) __attribute__ ((weak));
    void gen9_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties) __attribute__ ((weak));
    VkResult gen9_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* pImageFormatProperties) __attribute__ ((weak));
    VkResult gen9_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) __attribute__ ((weak));
    void gen9_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen9_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
    VkResult gen9_EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
    VkResult gen9_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
    VkResult gen9_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
    void gen9_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue) __attribute__ ((weak));
    VkResult gen9_QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence) __attribute__ ((weak));
    VkResult gen9_QueueWaitIdle(VkQueue queue) __attribute__ ((weak));
    VkResult gen9_DeviceWaitIdle(VkDevice device) __attribute__ ((weak));
    VkResult gen9_AllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory) __attribute__ ((weak));
    void gen9_FreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen9_MapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData) __attribute__ ((weak));
    void gen9_UnmapMemory(VkDevice device, VkDeviceMemory memory) __attribute__ ((weak));
    VkResult gen9_FlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) __attribute__ ((weak));
    VkResult gen9_InvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) __attribute__ ((weak));
    void gen9_GetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory, VkDeviceSize* pCommittedMemoryInBytes) __attribute__ ((weak));
    void gen9_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
    VkResult gen9_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
    void gen9_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
    VkResult gen9_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
    void gen9_GetImageSparseMemoryRequirements(VkDevice device, VkImage image, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements* pSparseMemoryRequirements) __attribute__ ((weak));
    void gen9_GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling, uint32_t* pPropertyCount, VkSparseImageFormatProperties* pProperties) __attribute__ ((weak));
    VkResult gen9_QueueBindSparse(VkQueue queue, uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence) __attribute__ ((weak));
    VkResult gen9_CreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence) __attribute__ ((weak));
    void gen9_DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen9_ResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences) __attribute__ ((weak));
    VkResult gen9_GetFenceStatus(VkDevice device, VkFence fence) __attribute__ ((weak));
    VkResult gen9_WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout) __attribute__ ((weak));
    VkResult gen9_CreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore) __attribute__ ((weak));
    void gen9_DestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen9_CreateEvent(VkDevice device, const VkEventCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkEvent* pEvent) __attribute__ ((weak));
    void gen9_DestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen9_GetEventStatus(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult gen9_SetEvent(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult gen9_ResetEvent(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult gen9_CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool) __attribute__ ((weak));
    void gen9_DestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen9_GetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* pData, VkDeviceSize stride, VkQueryResultFlags flags) __attribute__ ((weak));
    VkResult gen9_CreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer) __attribute__ ((weak));
    void gen9_DestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen9_CreateBufferView(VkDevice device, const VkBufferViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBufferView* pView) __attribute__ ((weak));
    void gen9_DestroyBufferView(VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen9_CreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage) __attribute__ ((weak));
    void gen9_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen9_GetImageSubresourceLayout(VkDevice device, VkImage image, const VkImageSubresource* pSubresource, VkSubresourceLayout* pLayout) __attribute__ ((weak));
    VkResult gen9_CreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView) __attribute__ ((weak));
    void gen9_DestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen9_CreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule) __attribute__ ((weak));
    void gen9_DestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen9_CreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineCache* pPipelineCache) __attribute__ ((weak));
    void gen9_DestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen9_GetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, size_t* pDataSize, void* pData) __attribute__ ((weak));
    VkResult gen9_MergePipelineCaches(VkDevice device, VkPipelineCache dstCache, uint32_t srcCacheCount, const VkPipelineCache* pSrcCaches) __attribute__ ((weak));
    VkResult gen9_CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) __attribute__ ((weak));
    VkResult gen9_CreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) __attribute__ ((weak));
    void gen9_DestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen9_CreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout) __attribute__ ((weak));
    void gen9_DestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen9_CreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSampler* pSampler) __attribute__ ((weak));
    void gen9_DestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen9_CreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout) __attribute__ ((weak));
    void gen9_DestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen9_CreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool) __attribute__ ((weak));
    void gen9_DestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen9_ResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags) __attribute__ ((weak));
    VkResult gen9_AllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets) __attribute__ ((weak));
    VkResult gen9_FreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets) __attribute__ ((weak));
    void gen9_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies) __attribute__ ((weak));
    VkResult gen9_CreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFramebuffer* pFramebuffer) __attribute__ ((weak));
    void gen9_DestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen9_CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass) __attribute__ ((weak));
    void gen9_DestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen9_GetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass, VkExtent2D* pGranularity) __attribute__ ((weak));
    VkResult gen9_CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool) __attribute__ ((weak));
    void gen9_DestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen9_ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags) __attribute__ ((weak));
    VkResult gen9_AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    void gen9_FreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    VkResult gen9_BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo) __attribute__ ((weak));
    VkResult gen9_EndCommandBuffer(VkCommandBuffer commandBuffer) __attribute__ ((weak));
    VkResult gen9_ResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags) __attribute__ ((weak));
    void gen9_CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) __attribute__ ((weak));
    void gen9_CmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports) __attribute__ ((weak));
    void gen9_CmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors) __attribute__ ((weak));
    void gen9_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth) __attribute__ ((weak));
    void gen9_CmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor) __attribute__ ((weak));
    void gen9_CmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4]) __attribute__ ((weak));
    void gen9_CmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds) __attribute__ ((weak));
    void gen9_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t compareMask) __attribute__ ((weak));
    void gen9_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t writeMask) __attribute__ ((weak));
    void gen9_CmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference) __attribute__ ((weak));
    void gen9_CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets) __attribute__ ((weak));
    void gen9_CmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) __attribute__ ((weak));
    void gen9_CmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets) __attribute__ ((weak));
    void gen9_CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) __attribute__ ((weak));
    void gen9_CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) __attribute__ ((weak));
    void gen9_CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) __attribute__ ((weak));
    void gen9_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) __attribute__ ((weak));
    void gen9_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) __attribute__ ((weak));
    void gen9_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset) __attribute__ ((weak));
    void gen9_CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions) __attribute__ ((weak));
    void gen9_CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions) __attribute__ ((weak));
    void gen9_CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter) __attribute__ ((weak));
    void gen9_CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
    void gen9_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
    void gen9_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* pData) __attribute__ ((weak));
    void gen9_CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data) __attribute__ ((weak));
    void gen9_CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) __attribute__ ((weak));
    void gen9_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) __attribute__ ((weak));
    void gen9_CmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment* pAttachments, uint32_t rectCount, const VkClearRect* pRects) __attribute__ ((weak));
    void gen9_CmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageResolve* pRegions) __attribute__ ((weak));
    void gen9_CmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) __attribute__ ((weak));
    void gen9_CmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) __attribute__ ((weak));
    void gen9_CmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) __attribute__ ((weak));
    void gen9_CmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) __attribute__ ((weak));
    void gen9_CmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags) __attribute__ ((weak));
    void gen9_CmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query) __attribute__ ((weak));
    void gen9_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) __attribute__ ((weak));
    void gen9_CmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query) __attribute__ ((weak));
    void gen9_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags) __attribute__ ((weak));
    void gen9_CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues) __attribute__ ((weak));
    void gen9_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents) __attribute__ ((weak));
    void gen9_CmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents) __attribute__ ((weak));
    void gen9_CmdEndRenderPass(VkCommandBuffer commandBuffer) __attribute__ ((weak));
    void gen9_CmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    void gen9_DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen9_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32* pSupported) __attribute__ ((weak));
    VkResult gen9_GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen9_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats) __attribute__ ((weak));
    VkResult gen9_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes) __attribute__ ((weak));
    VkResult gen9_CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) __attribute__ ((weak));
    void gen9_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen9_GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages) __attribute__ ((weak));
    VkResult gen9_AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex) __attribute__ ((weak));
    VkResult gen9_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) __attribute__ ((weak));
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    VkResult gen9_CreateWaylandSurfaceKHR(VkInstance instance, const VkWaylandSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    VkBool32 gen9_GetPhysicalDeviceWaylandPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, struct wl_display* display) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    VkResult gen9_CreateXlibSurfaceKHR(VkInstance instance, const VkXlibSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    VkBool32 gen9_GetPhysicalDeviceXlibPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, Display* dpy, VisualID visualID) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    VkResult gen9_CreateXcbSurfaceKHR(VkInstance instance, const VkXcbSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    VkBool32 gen9_GetPhysicalDeviceXcbPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, xcb_connection_t* connection, xcb_visualid_t visual_id) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XCB_KHR
    void gen9_GetPhysicalDeviceFeatures2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2KHR* pFeatures) __attribute__ ((weak));
    void gen9_GetPhysicalDeviceProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2KHR* pProperties) __attribute__ ((weak));
    void gen9_GetPhysicalDeviceFormatProperties2KHR(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2KHR* pFormatProperties) __attribute__ ((weak));
    VkResult gen9_GetPhysicalDeviceImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2KHR* pImageFormatInfo, VkImageFormatProperties2KHR* pImageFormatProperties) __attribute__ ((weak));
    void gen9_GetPhysicalDeviceQueueFamilyProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2KHR* pQueueFamilyProperties) __attribute__ ((weak));
    void gen9_GetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2KHR* pMemoryProperties) __attribute__ ((weak));
    void gen9_GetPhysicalDeviceSparseImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2KHR* pFormatInfo, uint32_t* pPropertyCount, VkSparseImageFormatProperties2KHR* pProperties) __attribute__ ((weak));
    void gen9_CmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites) __attribute__ ((weak));
    void gen9_TrimCommandPoolKHR(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlagsKHR flags) __attribute__ ((weak));
    void gen9_GetPhysicalDeviceExternalBufferPropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfoKHR* pExternalBufferInfo, VkExternalBufferPropertiesKHR* pExternalBufferProperties) __attribute__ ((weak));
    VkResult gen9_GetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen9_GetMemoryFdPropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBitsKHR handleType, int fd, VkMemoryFdPropertiesKHR* pMemoryFdProperties) __attribute__ ((weak));
    VkResult gen9_CreateDescriptorUpdateTemplateKHR(VkDevice device, const VkDescriptorUpdateTemplateCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplateKHR* pDescriptorUpdateTemplate) __attribute__ ((weak));
    void gen9_DestroyDescriptorUpdateTemplateKHR(VkDevice device, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen9_UpdateDescriptorSetWithTemplateKHR(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const void* pData) __attribute__ ((weak));
    void gen9_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) __attribute__ ((weak));
    VkResult gen9_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen9_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats) __attribute__ ((weak));
    void gen9_GetBufferMemoryRequirements2KHR(VkDevice device, const VkBufferMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void gen9_GetImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void gen9_GetImageSparseMemoryRequirements2KHR(VkDevice device, const VkImageSparseMemoryRequirementsInfo2KHR* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2KHR* pSparseMemoryRequirements) __attribute__ ((weak));
    VkResult gen9_CreateDmaBufImageINTEL(VkDevice device, const VkDmaBufImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,VkDeviceMemory* pMem,VkImage* pImage) __attribute__ ((weak));

  const struct anv_dispatch_table gen9_layer = {
    .CreateInstance = gen9_CreateInstance,
    .DestroyInstance = gen9_DestroyInstance,
    .EnumeratePhysicalDevices = gen9_EnumeratePhysicalDevices,
    .GetDeviceProcAddr = gen9_GetDeviceProcAddr,
    .GetInstanceProcAddr = gen9_GetInstanceProcAddr,
    .GetPhysicalDeviceProperties = gen9_GetPhysicalDeviceProperties,
    .GetPhysicalDeviceQueueFamilyProperties = gen9_GetPhysicalDeviceQueueFamilyProperties,
    .GetPhysicalDeviceMemoryProperties = gen9_GetPhysicalDeviceMemoryProperties,
    .GetPhysicalDeviceFeatures = gen9_GetPhysicalDeviceFeatures,
    .GetPhysicalDeviceFormatProperties = gen9_GetPhysicalDeviceFormatProperties,
    .GetPhysicalDeviceImageFormatProperties = gen9_GetPhysicalDeviceImageFormatProperties,
    .CreateDevice = gen9_CreateDevice,
    .DestroyDevice = gen9_DestroyDevice,
    .EnumerateInstanceLayerProperties = gen9_EnumerateInstanceLayerProperties,
    .EnumerateInstanceExtensionProperties = gen9_EnumerateInstanceExtensionProperties,
    .EnumerateDeviceLayerProperties = gen9_EnumerateDeviceLayerProperties,
    .EnumerateDeviceExtensionProperties = gen9_EnumerateDeviceExtensionProperties,
    .GetDeviceQueue = gen9_GetDeviceQueue,
    .QueueSubmit = gen9_QueueSubmit,
    .QueueWaitIdle = gen9_QueueWaitIdle,
    .DeviceWaitIdle = gen9_DeviceWaitIdle,
    .AllocateMemory = gen9_AllocateMemory,
    .FreeMemory = gen9_FreeMemory,
    .MapMemory = gen9_MapMemory,
    .UnmapMemory = gen9_UnmapMemory,
    .FlushMappedMemoryRanges = gen9_FlushMappedMemoryRanges,
    .InvalidateMappedMemoryRanges = gen9_InvalidateMappedMemoryRanges,
    .GetDeviceMemoryCommitment = gen9_GetDeviceMemoryCommitment,
    .GetBufferMemoryRequirements = gen9_GetBufferMemoryRequirements,
    .BindBufferMemory = gen9_BindBufferMemory,
    .GetImageMemoryRequirements = gen9_GetImageMemoryRequirements,
    .BindImageMemory = gen9_BindImageMemory,
    .GetImageSparseMemoryRequirements = gen9_GetImageSparseMemoryRequirements,
    .GetPhysicalDeviceSparseImageFormatProperties = gen9_GetPhysicalDeviceSparseImageFormatProperties,
    .QueueBindSparse = gen9_QueueBindSparse,
    .CreateFence = gen9_CreateFence,
    .DestroyFence = gen9_DestroyFence,
    .ResetFences = gen9_ResetFences,
    .GetFenceStatus = gen9_GetFenceStatus,
    .WaitForFences = gen9_WaitForFences,
    .CreateSemaphore = gen9_CreateSemaphore,
    .DestroySemaphore = gen9_DestroySemaphore,
    .CreateEvent = gen9_CreateEvent,
    .DestroyEvent = gen9_DestroyEvent,
    .GetEventStatus = gen9_GetEventStatus,
    .SetEvent = gen9_SetEvent,
    .ResetEvent = gen9_ResetEvent,
    .CreateQueryPool = gen9_CreateQueryPool,
    .DestroyQueryPool = gen9_DestroyQueryPool,
    .GetQueryPoolResults = gen9_GetQueryPoolResults,
    .CreateBuffer = gen9_CreateBuffer,
    .DestroyBuffer = gen9_DestroyBuffer,
    .CreateBufferView = gen9_CreateBufferView,
    .DestroyBufferView = gen9_DestroyBufferView,
    .CreateImage = gen9_CreateImage,
    .DestroyImage = gen9_DestroyImage,
    .GetImageSubresourceLayout = gen9_GetImageSubresourceLayout,
    .CreateImageView = gen9_CreateImageView,
    .DestroyImageView = gen9_DestroyImageView,
    .CreateShaderModule = gen9_CreateShaderModule,
    .DestroyShaderModule = gen9_DestroyShaderModule,
    .CreatePipelineCache = gen9_CreatePipelineCache,
    .DestroyPipelineCache = gen9_DestroyPipelineCache,
    .GetPipelineCacheData = gen9_GetPipelineCacheData,
    .MergePipelineCaches = gen9_MergePipelineCaches,
    .CreateGraphicsPipelines = gen9_CreateGraphicsPipelines,
    .CreateComputePipelines = gen9_CreateComputePipelines,
    .DestroyPipeline = gen9_DestroyPipeline,
    .CreatePipelineLayout = gen9_CreatePipelineLayout,
    .DestroyPipelineLayout = gen9_DestroyPipelineLayout,
    .CreateSampler = gen9_CreateSampler,
    .DestroySampler = gen9_DestroySampler,
    .CreateDescriptorSetLayout = gen9_CreateDescriptorSetLayout,
    .DestroyDescriptorSetLayout = gen9_DestroyDescriptorSetLayout,
    .CreateDescriptorPool = gen9_CreateDescriptorPool,
    .DestroyDescriptorPool = gen9_DestroyDescriptorPool,
    .ResetDescriptorPool = gen9_ResetDescriptorPool,
    .AllocateDescriptorSets = gen9_AllocateDescriptorSets,
    .FreeDescriptorSets = gen9_FreeDescriptorSets,
    .UpdateDescriptorSets = gen9_UpdateDescriptorSets,
    .CreateFramebuffer = gen9_CreateFramebuffer,
    .DestroyFramebuffer = gen9_DestroyFramebuffer,
    .CreateRenderPass = gen9_CreateRenderPass,
    .DestroyRenderPass = gen9_DestroyRenderPass,
    .GetRenderAreaGranularity = gen9_GetRenderAreaGranularity,
    .CreateCommandPool = gen9_CreateCommandPool,
    .DestroyCommandPool = gen9_DestroyCommandPool,
    .ResetCommandPool = gen9_ResetCommandPool,
    .AllocateCommandBuffers = gen9_AllocateCommandBuffers,
    .FreeCommandBuffers = gen9_FreeCommandBuffers,
    .BeginCommandBuffer = gen9_BeginCommandBuffer,
    .EndCommandBuffer = gen9_EndCommandBuffer,
    .ResetCommandBuffer = gen9_ResetCommandBuffer,
    .CmdBindPipeline = gen9_CmdBindPipeline,
    .CmdSetViewport = gen9_CmdSetViewport,
    .CmdSetScissor = gen9_CmdSetScissor,
    .CmdSetLineWidth = gen9_CmdSetLineWidth,
    .CmdSetDepthBias = gen9_CmdSetDepthBias,
    .CmdSetBlendConstants = gen9_CmdSetBlendConstants,
    .CmdSetDepthBounds = gen9_CmdSetDepthBounds,
    .CmdSetStencilCompareMask = gen9_CmdSetStencilCompareMask,
    .CmdSetStencilWriteMask = gen9_CmdSetStencilWriteMask,
    .CmdSetStencilReference = gen9_CmdSetStencilReference,
    .CmdBindDescriptorSets = gen9_CmdBindDescriptorSets,
    .CmdBindIndexBuffer = gen9_CmdBindIndexBuffer,
    .CmdBindVertexBuffers = gen9_CmdBindVertexBuffers,
    .CmdDraw = gen9_CmdDraw,
    .CmdDrawIndexed = gen9_CmdDrawIndexed,
    .CmdDrawIndirect = gen9_CmdDrawIndirect,
    .CmdDrawIndexedIndirect = gen9_CmdDrawIndexedIndirect,
    .CmdDispatch = gen9_CmdDispatch,
    .CmdDispatchIndirect = gen9_CmdDispatchIndirect,
    .CmdCopyBuffer = gen9_CmdCopyBuffer,
    .CmdCopyImage = gen9_CmdCopyImage,
    .CmdBlitImage = gen9_CmdBlitImage,
    .CmdCopyBufferToImage = gen9_CmdCopyBufferToImage,
    .CmdCopyImageToBuffer = gen9_CmdCopyImageToBuffer,
    .CmdUpdateBuffer = gen9_CmdUpdateBuffer,
    .CmdFillBuffer = gen9_CmdFillBuffer,
    .CmdClearColorImage = gen9_CmdClearColorImage,
    .CmdClearDepthStencilImage = gen9_CmdClearDepthStencilImage,
    .CmdClearAttachments = gen9_CmdClearAttachments,
    .CmdResolveImage = gen9_CmdResolveImage,
    .CmdSetEvent = gen9_CmdSetEvent,
    .CmdResetEvent = gen9_CmdResetEvent,
    .CmdWaitEvents = gen9_CmdWaitEvents,
    .CmdPipelineBarrier = gen9_CmdPipelineBarrier,
    .CmdBeginQuery = gen9_CmdBeginQuery,
    .CmdEndQuery = gen9_CmdEndQuery,
    .CmdResetQueryPool = gen9_CmdResetQueryPool,
    .CmdWriteTimestamp = gen9_CmdWriteTimestamp,
    .CmdCopyQueryPoolResults = gen9_CmdCopyQueryPoolResults,
    .CmdPushConstants = gen9_CmdPushConstants,
    .CmdBeginRenderPass = gen9_CmdBeginRenderPass,
    .CmdNextSubpass = gen9_CmdNextSubpass,
    .CmdEndRenderPass = gen9_CmdEndRenderPass,
    .CmdExecuteCommands = gen9_CmdExecuteCommands,
    .DestroySurfaceKHR = gen9_DestroySurfaceKHR,
    .GetPhysicalDeviceSurfaceSupportKHR = gen9_GetPhysicalDeviceSurfaceSupportKHR,
    .GetPhysicalDeviceSurfaceCapabilitiesKHR = gen9_GetPhysicalDeviceSurfaceCapabilitiesKHR,
    .GetPhysicalDeviceSurfaceFormatsKHR = gen9_GetPhysicalDeviceSurfaceFormatsKHR,
    .GetPhysicalDeviceSurfacePresentModesKHR = gen9_GetPhysicalDeviceSurfacePresentModesKHR,
    .CreateSwapchainKHR = gen9_CreateSwapchainKHR,
    .DestroySwapchainKHR = gen9_DestroySwapchainKHR,
    .GetSwapchainImagesKHR = gen9_GetSwapchainImagesKHR,
    .AcquireNextImageKHR = gen9_AcquireNextImageKHR,
    .QueuePresentKHR = gen9_QueuePresentKHR,
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .CreateWaylandSurfaceKHR = gen9_CreateWaylandSurfaceKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .GetPhysicalDeviceWaylandPresentationSupportKHR = gen9_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .CreateXlibSurfaceKHR = gen9_CreateXlibSurfaceKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .GetPhysicalDeviceXlibPresentationSupportKHR = gen9_GetPhysicalDeviceXlibPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .CreateXcbSurfaceKHR = gen9_CreateXcbSurfaceKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .GetPhysicalDeviceXcbPresentationSupportKHR = gen9_GetPhysicalDeviceXcbPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
    .GetPhysicalDeviceFeatures2KHR = gen9_GetPhysicalDeviceFeatures2KHR,
    .GetPhysicalDeviceProperties2KHR = gen9_GetPhysicalDeviceProperties2KHR,
    .GetPhysicalDeviceFormatProperties2KHR = gen9_GetPhysicalDeviceFormatProperties2KHR,
    .GetPhysicalDeviceImageFormatProperties2KHR = gen9_GetPhysicalDeviceImageFormatProperties2KHR,
    .GetPhysicalDeviceQueueFamilyProperties2KHR = gen9_GetPhysicalDeviceQueueFamilyProperties2KHR,
    .GetPhysicalDeviceMemoryProperties2KHR = gen9_GetPhysicalDeviceMemoryProperties2KHR,
    .GetPhysicalDeviceSparseImageFormatProperties2KHR = gen9_GetPhysicalDeviceSparseImageFormatProperties2KHR,
    .CmdPushDescriptorSetKHR = gen9_CmdPushDescriptorSetKHR,
    .TrimCommandPoolKHR = gen9_TrimCommandPoolKHR,
    .GetPhysicalDeviceExternalBufferPropertiesKHR = gen9_GetPhysicalDeviceExternalBufferPropertiesKHR,
    .GetMemoryFdKHR = gen9_GetMemoryFdKHR,
    .GetMemoryFdPropertiesKHR = gen9_GetMemoryFdPropertiesKHR,
    .CreateDescriptorUpdateTemplateKHR = gen9_CreateDescriptorUpdateTemplateKHR,
    .DestroyDescriptorUpdateTemplateKHR = gen9_DestroyDescriptorUpdateTemplateKHR,
    .UpdateDescriptorSetWithTemplateKHR = gen9_UpdateDescriptorSetWithTemplateKHR,
    .CmdPushDescriptorSetWithTemplateKHR = gen9_CmdPushDescriptorSetWithTemplateKHR,
    .GetPhysicalDeviceSurfaceCapabilities2KHR = gen9_GetPhysicalDeviceSurfaceCapabilities2KHR,
    .GetPhysicalDeviceSurfaceFormats2KHR = gen9_GetPhysicalDeviceSurfaceFormats2KHR,
    .GetBufferMemoryRequirements2KHR = gen9_GetBufferMemoryRequirements2KHR,
    .GetImageMemoryRequirements2KHR = gen9_GetImageMemoryRequirements2KHR,
    .GetImageSparseMemoryRequirements2KHR = gen9_GetImageSparseMemoryRequirements2KHR,
    .CreateDmaBufImageINTEL = gen9_CreateDmaBufImageINTEL,
  };
    VkResult gen10_CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) __attribute__ ((weak));
    void gen10_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen10_EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) __attribute__ ((weak));
    PFN_vkVoidFunction gen10_GetDeviceProcAddr(VkDevice device, const char* pName) __attribute__ ((weak));
    PFN_vkVoidFunction gen10_GetInstanceProcAddr(VkInstance instance, const char* pName) __attribute__ ((weak));
    void gen10_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) __attribute__ ((weak));
    void gen10_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties) __attribute__ ((weak));
    void gen10_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties) __attribute__ ((weak));
    void gen10_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures) __attribute__ ((weak));
    void gen10_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties) __attribute__ ((weak));
    VkResult gen10_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* pImageFormatProperties) __attribute__ ((weak));
    VkResult gen10_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) __attribute__ ((weak));
    void gen10_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen10_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
    VkResult gen10_EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
    VkResult gen10_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
    VkResult gen10_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
    void gen10_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue) __attribute__ ((weak));
    VkResult gen10_QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence) __attribute__ ((weak));
    VkResult gen10_QueueWaitIdle(VkQueue queue) __attribute__ ((weak));
    VkResult gen10_DeviceWaitIdle(VkDevice device) __attribute__ ((weak));
    VkResult gen10_AllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory) __attribute__ ((weak));
    void gen10_FreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen10_MapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData) __attribute__ ((weak));
    void gen10_UnmapMemory(VkDevice device, VkDeviceMemory memory) __attribute__ ((weak));
    VkResult gen10_FlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) __attribute__ ((weak));
    VkResult gen10_InvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) __attribute__ ((weak));
    void gen10_GetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory, VkDeviceSize* pCommittedMemoryInBytes) __attribute__ ((weak));
    void gen10_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
    VkResult gen10_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
    void gen10_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
    VkResult gen10_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
    void gen10_GetImageSparseMemoryRequirements(VkDevice device, VkImage image, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements* pSparseMemoryRequirements) __attribute__ ((weak));
    void gen10_GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling, uint32_t* pPropertyCount, VkSparseImageFormatProperties* pProperties) __attribute__ ((weak));
    VkResult gen10_QueueBindSparse(VkQueue queue, uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence) __attribute__ ((weak));
    VkResult gen10_CreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence) __attribute__ ((weak));
    void gen10_DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen10_ResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences) __attribute__ ((weak));
    VkResult gen10_GetFenceStatus(VkDevice device, VkFence fence) __attribute__ ((weak));
    VkResult gen10_WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout) __attribute__ ((weak));
    VkResult gen10_CreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore) __attribute__ ((weak));
    void gen10_DestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen10_CreateEvent(VkDevice device, const VkEventCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkEvent* pEvent) __attribute__ ((weak));
    void gen10_DestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen10_GetEventStatus(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult gen10_SetEvent(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult gen10_ResetEvent(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult gen10_CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool) __attribute__ ((weak));
    void gen10_DestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen10_GetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* pData, VkDeviceSize stride, VkQueryResultFlags flags) __attribute__ ((weak));
    VkResult gen10_CreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer) __attribute__ ((weak));
    void gen10_DestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen10_CreateBufferView(VkDevice device, const VkBufferViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBufferView* pView) __attribute__ ((weak));
    void gen10_DestroyBufferView(VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen10_CreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage) __attribute__ ((weak));
    void gen10_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen10_GetImageSubresourceLayout(VkDevice device, VkImage image, const VkImageSubresource* pSubresource, VkSubresourceLayout* pLayout) __attribute__ ((weak));
    VkResult gen10_CreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView) __attribute__ ((weak));
    void gen10_DestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen10_CreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule) __attribute__ ((weak));
    void gen10_DestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen10_CreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineCache* pPipelineCache) __attribute__ ((weak));
    void gen10_DestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen10_GetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, size_t* pDataSize, void* pData) __attribute__ ((weak));
    VkResult gen10_MergePipelineCaches(VkDevice device, VkPipelineCache dstCache, uint32_t srcCacheCount, const VkPipelineCache* pSrcCaches) __attribute__ ((weak));
    VkResult gen10_CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) __attribute__ ((weak));
    VkResult gen10_CreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) __attribute__ ((weak));
    void gen10_DestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen10_CreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout) __attribute__ ((weak));
    void gen10_DestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen10_CreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSampler* pSampler) __attribute__ ((weak));
    void gen10_DestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen10_CreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout) __attribute__ ((weak));
    void gen10_DestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen10_CreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool) __attribute__ ((weak));
    void gen10_DestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen10_ResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags) __attribute__ ((weak));
    VkResult gen10_AllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets) __attribute__ ((weak));
    VkResult gen10_FreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets) __attribute__ ((weak));
    void gen10_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies) __attribute__ ((weak));
    VkResult gen10_CreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFramebuffer* pFramebuffer) __attribute__ ((weak));
    void gen10_DestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen10_CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass) __attribute__ ((weak));
    void gen10_DestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen10_GetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass, VkExtent2D* pGranularity) __attribute__ ((weak));
    VkResult gen10_CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool) __attribute__ ((weak));
    void gen10_DestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen10_ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags) __attribute__ ((weak));
    VkResult gen10_AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    void gen10_FreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    VkResult gen10_BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo) __attribute__ ((weak));
    VkResult gen10_EndCommandBuffer(VkCommandBuffer commandBuffer) __attribute__ ((weak));
    VkResult gen10_ResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags) __attribute__ ((weak));
    void gen10_CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) __attribute__ ((weak));
    void gen10_CmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports) __attribute__ ((weak));
    void gen10_CmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors) __attribute__ ((weak));
    void gen10_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth) __attribute__ ((weak));
    void gen10_CmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor) __attribute__ ((weak));
    void gen10_CmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4]) __attribute__ ((weak));
    void gen10_CmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds) __attribute__ ((weak));
    void gen10_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t compareMask) __attribute__ ((weak));
    void gen10_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t writeMask) __attribute__ ((weak));
    void gen10_CmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference) __attribute__ ((weak));
    void gen10_CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets) __attribute__ ((weak));
    void gen10_CmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) __attribute__ ((weak));
    void gen10_CmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets) __attribute__ ((weak));
    void gen10_CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) __attribute__ ((weak));
    void gen10_CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) __attribute__ ((weak));
    void gen10_CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) __attribute__ ((weak));
    void gen10_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) __attribute__ ((weak));
    void gen10_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) __attribute__ ((weak));
    void gen10_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset) __attribute__ ((weak));
    void gen10_CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions) __attribute__ ((weak));
    void gen10_CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions) __attribute__ ((weak));
    void gen10_CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter) __attribute__ ((weak));
    void gen10_CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
    void gen10_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
    void gen10_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* pData) __attribute__ ((weak));
    void gen10_CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data) __attribute__ ((weak));
    void gen10_CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) __attribute__ ((weak));
    void gen10_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) __attribute__ ((weak));
    void gen10_CmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment* pAttachments, uint32_t rectCount, const VkClearRect* pRects) __attribute__ ((weak));
    void gen10_CmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageResolve* pRegions) __attribute__ ((weak));
    void gen10_CmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) __attribute__ ((weak));
    void gen10_CmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) __attribute__ ((weak));
    void gen10_CmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) __attribute__ ((weak));
    void gen10_CmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) __attribute__ ((weak));
    void gen10_CmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags) __attribute__ ((weak));
    void gen10_CmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query) __attribute__ ((weak));
    void gen10_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) __attribute__ ((weak));
    void gen10_CmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query) __attribute__ ((weak));
    void gen10_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags) __attribute__ ((weak));
    void gen10_CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues) __attribute__ ((weak));
    void gen10_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents) __attribute__ ((weak));
    void gen10_CmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents) __attribute__ ((weak));
    void gen10_CmdEndRenderPass(VkCommandBuffer commandBuffer) __attribute__ ((weak));
    void gen10_CmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    void gen10_DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen10_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32* pSupported) __attribute__ ((weak));
    VkResult gen10_GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen10_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats) __attribute__ ((weak));
    VkResult gen10_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes) __attribute__ ((weak));
    VkResult gen10_CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) __attribute__ ((weak));
    void gen10_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen10_GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages) __attribute__ ((weak));
    VkResult gen10_AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex) __attribute__ ((weak));
    VkResult gen10_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) __attribute__ ((weak));
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    VkResult gen10_CreateWaylandSurfaceKHR(VkInstance instance, const VkWaylandSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    VkBool32 gen10_GetPhysicalDeviceWaylandPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, struct wl_display* display) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    VkResult gen10_CreateXlibSurfaceKHR(VkInstance instance, const VkXlibSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    VkBool32 gen10_GetPhysicalDeviceXlibPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, Display* dpy, VisualID visualID) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    VkResult gen10_CreateXcbSurfaceKHR(VkInstance instance, const VkXcbSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    VkBool32 gen10_GetPhysicalDeviceXcbPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, xcb_connection_t* connection, xcb_visualid_t visual_id) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XCB_KHR
    void gen10_GetPhysicalDeviceFeatures2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2KHR* pFeatures) __attribute__ ((weak));
    void gen10_GetPhysicalDeviceProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2KHR* pProperties) __attribute__ ((weak));
    void gen10_GetPhysicalDeviceFormatProperties2KHR(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2KHR* pFormatProperties) __attribute__ ((weak));
    VkResult gen10_GetPhysicalDeviceImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2KHR* pImageFormatInfo, VkImageFormatProperties2KHR* pImageFormatProperties) __attribute__ ((weak));
    void gen10_GetPhysicalDeviceQueueFamilyProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2KHR* pQueueFamilyProperties) __attribute__ ((weak));
    void gen10_GetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2KHR* pMemoryProperties) __attribute__ ((weak));
    void gen10_GetPhysicalDeviceSparseImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2KHR* pFormatInfo, uint32_t* pPropertyCount, VkSparseImageFormatProperties2KHR* pProperties) __attribute__ ((weak));
    void gen10_CmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites) __attribute__ ((weak));
    void gen10_TrimCommandPoolKHR(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlagsKHR flags) __attribute__ ((weak));
    void gen10_GetPhysicalDeviceExternalBufferPropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfoKHR* pExternalBufferInfo, VkExternalBufferPropertiesKHR* pExternalBufferProperties) __attribute__ ((weak));
    VkResult gen10_GetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen10_GetMemoryFdPropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBitsKHR handleType, int fd, VkMemoryFdPropertiesKHR* pMemoryFdProperties) __attribute__ ((weak));
    VkResult gen10_CreateDescriptorUpdateTemplateKHR(VkDevice device, const VkDescriptorUpdateTemplateCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplateKHR* pDescriptorUpdateTemplate) __attribute__ ((weak));
    void gen10_DestroyDescriptorUpdateTemplateKHR(VkDevice device, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen10_UpdateDescriptorSetWithTemplateKHR(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const void* pData) __attribute__ ((weak));
    void gen10_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) __attribute__ ((weak));
    VkResult gen10_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen10_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats) __attribute__ ((weak));
    void gen10_GetBufferMemoryRequirements2KHR(VkDevice device, const VkBufferMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void gen10_GetImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void gen10_GetImageSparseMemoryRequirements2KHR(VkDevice device, const VkImageSparseMemoryRequirementsInfo2KHR* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2KHR* pSparseMemoryRequirements) __attribute__ ((weak));
    VkResult gen10_CreateDmaBufImageINTEL(VkDevice device, const VkDmaBufImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,VkDeviceMemory* pMem,VkImage* pImage) __attribute__ ((weak));

  const struct anv_dispatch_table gen10_layer = {
    .CreateInstance = gen10_CreateInstance,
    .DestroyInstance = gen10_DestroyInstance,
    .EnumeratePhysicalDevices = gen10_EnumeratePhysicalDevices,
    .GetDeviceProcAddr = gen10_GetDeviceProcAddr,
    .GetInstanceProcAddr = gen10_GetInstanceProcAddr,
    .GetPhysicalDeviceProperties = gen10_GetPhysicalDeviceProperties,
    .GetPhysicalDeviceQueueFamilyProperties = gen10_GetPhysicalDeviceQueueFamilyProperties,
    .GetPhysicalDeviceMemoryProperties = gen10_GetPhysicalDeviceMemoryProperties,
    .GetPhysicalDeviceFeatures = gen10_GetPhysicalDeviceFeatures,
    .GetPhysicalDeviceFormatProperties = gen10_GetPhysicalDeviceFormatProperties,
    .GetPhysicalDeviceImageFormatProperties = gen10_GetPhysicalDeviceImageFormatProperties,
    .CreateDevice = gen10_CreateDevice,
    .DestroyDevice = gen10_DestroyDevice,
    .EnumerateInstanceLayerProperties = gen10_EnumerateInstanceLayerProperties,
    .EnumerateInstanceExtensionProperties = gen10_EnumerateInstanceExtensionProperties,
    .EnumerateDeviceLayerProperties = gen10_EnumerateDeviceLayerProperties,
    .EnumerateDeviceExtensionProperties = gen10_EnumerateDeviceExtensionProperties,
    .GetDeviceQueue = gen10_GetDeviceQueue,
    .QueueSubmit = gen10_QueueSubmit,
    .QueueWaitIdle = gen10_QueueWaitIdle,
    .DeviceWaitIdle = gen10_DeviceWaitIdle,
    .AllocateMemory = gen10_AllocateMemory,
    .FreeMemory = gen10_FreeMemory,
    .MapMemory = gen10_MapMemory,
    .UnmapMemory = gen10_UnmapMemory,
    .FlushMappedMemoryRanges = gen10_FlushMappedMemoryRanges,
    .InvalidateMappedMemoryRanges = gen10_InvalidateMappedMemoryRanges,
    .GetDeviceMemoryCommitment = gen10_GetDeviceMemoryCommitment,
    .GetBufferMemoryRequirements = gen10_GetBufferMemoryRequirements,
    .BindBufferMemory = gen10_BindBufferMemory,
    .GetImageMemoryRequirements = gen10_GetImageMemoryRequirements,
    .BindImageMemory = gen10_BindImageMemory,
    .GetImageSparseMemoryRequirements = gen10_GetImageSparseMemoryRequirements,
    .GetPhysicalDeviceSparseImageFormatProperties = gen10_GetPhysicalDeviceSparseImageFormatProperties,
    .QueueBindSparse = gen10_QueueBindSparse,
    .CreateFence = gen10_CreateFence,
    .DestroyFence = gen10_DestroyFence,
    .ResetFences = gen10_ResetFences,
    .GetFenceStatus = gen10_GetFenceStatus,
    .WaitForFences = gen10_WaitForFences,
    .CreateSemaphore = gen10_CreateSemaphore,
    .DestroySemaphore = gen10_DestroySemaphore,
    .CreateEvent = gen10_CreateEvent,
    .DestroyEvent = gen10_DestroyEvent,
    .GetEventStatus = gen10_GetEventStatus,
    .SetEvent = gen10_SetEvent,
    .ResetEvent = gen10_ResetEvent,
    .CreateQueryPool = gen10_CreateQueryPool,
    .DestroyQueryPool = gen10_DestroyQueryPool,
    .GetQueryPoolResults = gen10_GetQueryPoolResults,
    .CreateBuffer = gen10_CreateBuffer,
    .DestroyBuffer = gen10_DestroyBuffer,
    .CreateBufferView = gen10_CreateBufferView,
    .DestroyBufferView = gen10_DestroyBufferView,
    .CreateImage = gen10_CreateImage,
    .DestroyImage = gen10_DestroyImage,
    .GetImageSubresourceLayout = gen10_GetImageSubresourceLayout,
    .CreateImageView = gen10_CreateImageView,
    .DestroyImageView = gen10_DestroyImageView,
    .CreateShaderModule = gen10_CreateShaderModule,
    .DestroyShaderModule = gen10_DestroyShaderModule,
    .CreatePipelineCache = gen10_CreatePipelineCache,
    .DestroyPipelineCache = gen10_DestroyPipelineCache,
    .GetPipelineCacheData = gen10_GetPipelineCacheData,
    .MergePipelineCaches = gen10_MergePipelineCaches,
    .CreateGraphicsPipelines = gen10_CreateGraphicsPipelines,
    .CreateComputePipelines = gen10_CreateComputePipelines,
    .DestroyPipeline = gen10_DestroyPipeline,
    .CreatePipelineLayout = gen10_CreatePipelineLayout,
    .DestroyPipelineLayout = gen10_DestroyPipelineLayout,
    .CreateSampler = gen10_CreateSampler,
    .DestroySampler = gen10_DestroySampler,
    .CreateDescriptorSetLayout = gen10_CreateDescriptorSetLayout,
    .DestroyDescriptorSetLayout = gen10_DestroyDescriptorSetLayout,
    .CreateDescriptorPool = gen10_CreateDescriptorPool,
    .DestroyDescriptorPool = gen10_DestroyDescriptorPool,
    .ResetDescriptorPool = gen10_ResetDescriptorPool,
    .AllocateDescriptorSets = gen10_AllocateDescriptorSets,
    .FreeDescriptorSets = gen10_FreeDescriptorSets,
    .UpdateDescriptorSets = gen10_UpdateDescriptorSets,
    .CreateFramebuffer = gen10_CreateFramebuffer,
    .DestroyFramebuffer = gen10_DestroyFramebuffer,
    .CreateRenderPass = gen10_CreateRenderPass,
    .DestroyRenderPass = gen10_DestroyRenderPass,
    .GetRenderAreaGranularity = gen10_GetRenderAreaGranularity,
    .CreateCommandPool = gen10_CreateCommandPool,
    .DestroyCommandPool = gen10_DestroyCommandPool,
    .ResetCommandPool = gen10_ResetCommandPool,
    .AllocateCommandBuffers = gen10_AllocateCommandBuffers,
    .FreeCommandBuffers = gen10_FreeCommandBuffers,
    .BeginCommandBuffer = gen10_BeginCommandBuffer,
    .EndCommandBuffer = gen10_EndCommandBuffer,
    .ResetCommandBuffer = gen10_ResetCommandBuffer,
    .CmdBindPipeline = gen10_CmdBindPipeline,
    .CmdSetViewport = gen10_CmdSetViewport,
    .CmdSetScissor = gen10_CmdSetScissor,
    .CmdSetLineWidth = gen10_CmdSetLineWidth,
    .CmdSetDepthBias = gen10_CmdSetDepthBias,
    .CmdSetBlendConstants = gen10_CmdSetBlendConstants,
    .CmdSetDepthBounds = gen10_CmdSetDepthBounds,
    .CmdSetStencilCompareMask = gen10_CmdSetStencilCompareMask,
    .CmdSetStencilWriteMask = gen10_CmdSetStencilWriteMask,
    .CmdSetStencilReference = gen10_CmdSetStencilReference,
    .CmdBindDescriptorSets = gen10_CmdBindDescriptorSets,
    .CmdBindIndexBuffer = gen10_CmdBindIndexBuffer,
    .CmdBindVertexBuffers = gen10_CmdBindVertexBuffers,
    .CmdDraw = gen10_CmdDraw,
    .CmdDrawIndexed = gen10_CmdDrawIndexed,
    .CmdDrawIndirect = gen10_CmdDrawIndirect,
    .CmdDrawIndexedIndirect = gen10_CmdDrawIndexedIndirect,
    .CmdDispatch = gen10_CmdDispatch,
    .CmdDispatchIndirect = gen10_CmdDispatchIndirect,
    .CmdCopyBuffer = gen10_CmdCopyBuffer,
    .CmdCopyImage = gen10_CmdCopyImage,
    .CmdBlitImage = gen10_CmdBlitImage,
    .CmdCopyBufferToImage = gen10_CmdCopyBufferToImage,
    .CmdCopyImageToBuffer = gen10_CmdCopyImageToBuffer,
    .CmdUpdateBuffer = gen10_CmdUpdateBuffer,
    .CmdFillBuffer = gen10_CmdFillBuffer,
    .CmdClearColorImage = gen10_CmdClearColorImage,
    .CmdClearDepthStencilImage = gen10_CmdClearDepthStencilImage,
    .CmdClearAttachments = gen10_CmdClearAttachments,
    .CmdResolveImage = gen10_CmdResolveImage,
    .CmdSetEvent = gen10_CmdSetEvent,
    .CmdResetEvent = gen10_CmdResetEvent,
    .CmdWaitEvents = gen10_CmdWaitEvents,
    .CmdPipelineBarrier = gen10_CmdPipelineBarrier,
    .CmdBeginQuery = gen10_CmdBeginQuery,
    .CmdEndQuery = gen10_CmdEndQuery,
    .CmdResetQueryPool = gen10_CmdResetQueryPool,
    .CmdWriteTimestamp = gen10_CmdWriteTimestamp,
    .CmdCopyQueryPoolResults = gen10_CmdCopyQueryPoolResults,
    .CmdPushConstants = gen10_CmdPushConstants,
    .CmdBeginRenderPass = gen10_CmdBeginRenderPass,
    .CmdNextSubpass = gen10_CmdNextSubpass,
    .CmdEndRenderPass = gen10_CmdEndRenderPass,
    .CmdExecuteCommands = gen10_CmdExecuteCommands,
    .DestroySurfaceKHR = gen10_DestroySurfaceKHR,
    .GetPhysicalDeviceSurfaceSupportKHR = gen10_GetPhysicalDeviceSurfaceSupportKHR,
    .GetPhysicalDeviceSurfaceCapabilitiesKHR = gen10_GetPhysicalDeviceSurfaceCapabilitiesKHR,
    .GetPhysicalDeviceSurfaceFormatsKHR = gen10_GetPhysicalDeviceSurfaceFormatsKHR,
    .GetPhysicalDeviceSurfacePresentModesKHR = gen10_GetPhysicalDeviceSurfacePresentModesKHR,
    .CreateSwapchainKHR = gen10_CreateSwapchainKHR,
    .DestroySwapchainKHR = gen10_DestroySwapchainKHR,
    .GetSwapchainImagesKHR = gen10_GetSwapchainImagesKHR,
    .AcquireNextImageKHR = gen10_AcquireNextImageKHR,
    .QueuePresentKHR = gen10_QueuePresentKHR,
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .CreateWaylandSurfaceKHR = gen10_CreateWaylandSurfaceKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .GetPhysicalDeviceWaylandPresentationSupportKHR = gen10_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .CreateXlibSurfaceKHR = gen10_CreateXlibSurfaceKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .GetPhysicalDeviceXlibPresentationSupportKHR = gen10_GetPhysicalDeviceXlibPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .CreateXcbSurfaceKHR = gen10_CreateXcbSurfaceKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .GetPhysicalDeviceXcbPresentationSupportKHR = gen10_GetPhysicalDeviceXcbPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
    .GetPhysicalDeviceFeatures2KHR = gen10_GetPhysicalDeviceFeatures2KHR,
    .GetPhysicalDeviceProperties2KHR = gen10_GetPhysicalDeviceProperties2KHR,
    .GetPhysicalDeviceFormatProperties2KHR = gen10_GetPhysicalDeviceFormatProperties2KHR,
    .GetPhysicalDeviceImageFormatProperties2KHR = gen10_GetPhysicalDeviceImageFormatProperties2KHR,
    .GetPhysicalDeviceQueueFamilyProperties2KHR = gen10_GetPhysicalDeviceQueueFamilyProperties2KHR,
    .GetPhysicalDeviceMemoryProperties2KHR = gen10_GetPhysicalDeviceMemoryProperties2KHR,
    .GetPhysicalDeviceSparseImageFormatProperties2KHR = gen10_GetPhysicalDeviceSparseImageFormatProperties2KHR,
    .CmdPushDescriptorSetKHR = gen10_CmdPushDescriptorSetKHR,
    .TrimCommandPoolKHR = gen10_TrimCommandPoolKHR,
    .GetPhysicalDeviceExternalBufferPropertiesKHR = gen10_GetPhysicalDeviceExternalBufferPropertiesKHR,
    .GetMemoryFdKHR = gen10_GetMemoryFdKHR,
    .GetMemoryFdPropertiesKHR = gen10_GetMemoryFdPropertiesKHR,
    .CreateDescriptorUpdateTemplateKHR = gen10_CreateDescriptorUpdateTemplateKHR,
    .DestroyDescriptorUpdateTemplateKHR = gen10_DestroyDescriptorUpdateTemplateKHR,
    .UpdateDescriptorSetWithTemplateKHR = gen10_UpdateDescriptorSetWithTemplateKHR,
    .CmdPushDescriptorSetWithTemplateKHR = gen10_CmdPushDescriptorSetWithTemplateKHR,
    .GetPhysicalDeviceSurfaceCapabilities2KHR = gen10_GetPhysicalDeviceSurfaceCapabilities2KHR,
    .GetPhysicalDeviceSurfaceFormats2KHR = gen10_GetPhysicalDeviceSurfaceFormats2KHR,
    .GetBufferMemoryRequirements2KHR = gen10_GetBufferMemoryRequirements2KHR,
    .GetImageMemoryRequirements2KHR = gen10_GetImageMemoryRequirements2KHR,
    .GetImageSparseMemoryRequirements2KHR = gen10_GetImageSparseMemoryRequirements2KHR,
    .CreateDmaBufImageINTEL = gen10_CreateDmaBufImageINTEL,
  };

static void * __attribute__ ((noinline))
anv_resolve_entrypoint(const struct gen_device_info *devinfo, uint32_t index)
{
   if (devinfo == NULL) {
      return anv_layer.entrypoints[index];
   }

   switch (devinfo->gen) {
   case 10:
      if (gen10_layer.entrypoints[index])
         return gen10_layer.entrypoints[index];
      /* fall through */
   case 9:
      if (gen9_layer.entrypoints[index])
         return gen9_layer.entrypoints[index];
      /* fall through */
   case 8:
      if (gen8_layer.entrypoints[index])
         return gen8_layer.entrypoints[index];
      /* fall through */
   case 7:
      if (devinfo->is_haswell && gen75_layer.entrypoints[index])
         return gen75_layer.entrypoints[index];

      if (gen7_layer.entrypoints[index])
         return gen7_layer.entrypoints[index];
      /* fall through */
   case 0:
      return anv_layer.entrypoints[index];
   default:
      unreachable("unsupported gen\n");
   }
}

/* Hash table stats:
 * size 256 entries
 * collisions entries:
 *     0     116
 *     1     31
 *     2     10
 *     3     7
 *     4     6
 *     5     1
 *     6     1
 *     7     1
 *     8     1
 *     9+     1
 */

#define none 0xffff
static const uint16_t map[] = {
      0x0044,
      none,
      none,
      none,
      0x00a4,
      0x002b,
      0x0040,
      0x0061,
      0x0049,
      0x0022,
      0x0056,
      none,
      none,
      none,
      0x00a2,
      none,
      none,
      0x00a3,
      none,
      0x0067,
      none,
      none,
      none,
      none,
      0x0052,
      0x0097,
      0x0058,
      0x004c,
      none,
      0x0069,
      0x00a6,
      none,
      none,
      none,
      0x0054,
      none,
      0x0014,
      0x005b,
      0x0070,
      0x0002,
      0x007c,
      none,
      0x001c,
      0x002f,
      none,
      none,
      0x0077,
      0x0018,
      0x004b,
      0x002a,
      none,
      0x0008,
      0x0065,
      0x0080,
      0x006d,
      0x0053,
      none,
      0x009d,
      0x004d,
      0x0090,
      0x0024,
      0x009e,
      0x005e,
      0x000b,
      0x0088,
      0x0091,
      none,
      0x00a7,
      0x005c,
      0x0033,
      none,
      none,
      0x0087,
      0x003f,
      0x001f,
      0x002c,
      0x0082,
      0x005a,
      none,
      none,
      none,
      0x0019,
      0x0046,
      0x003a,
      none,
      none,
      0x0034,
      none,
      0x0051,
      none,
      none,
      0x0020,
      0x0099,
      0x0066,
      0x0075,
      none,
      none,
      none,
      0x0035,
      0x001e,
      0x006f,
      0x0060,
      0x0047,
      0x000a,
      0x0023,
      none,
      none,
      0x006b,
      none,
      0x0041,
      0x0028,
      none,
      0x0068,
      0x00ad,
      0x009f,
      0x003e,
      0x0048,
      0x007b,
      0x0055,
      none,
      0x00a9,
      0x0045,
      0x006e,
      0x0084,
      none,
      0x0089,
      0x000e,
      0x0030,
      none,
      0x0027,
      0x0081,
      0x00ac,
      0x005d,
      0x008a,
      0x0003,
      0x008f,
      0x00aa,
      0x0063,
      0x0006,
      none,
      0x0093,
      0x00a1,
      none,
      none,
      none,
      0x0059,
      0x0026,
      none,
      0x003c,
      none,
      0x0037,
      none,
      0x0009,
      0x0038,
      0x0011,
      none,
      0x0072,
      0x0016,
      none,
      0x003d,
      0x00ab,
      0x006a,
      0x003b,
      none,
      0x004a,
      0x0013,
      0x0000,
      0x007a,
      0x002e,
      0x0071,
      none,
      0x0096,
      0x0074,
      0x0004,
      0x004f,
      0x0029,
      0x00a5,
      0x004e,
      0x0095,
      0x0031,
      0x00a0,
      0x001b,
      none,
      0x0073,
      0x005f,
      0x0032,
      0x0078,
      0x008e,
      none,
      none,
      none,
      0x006c,
      none,
      none,
      0x0036,
      none,
      0x0050,
      0x009a,
      0x007d,
      none,
      0x008c,
      0x0005,
      0x001a,
      0x000c,
      0x0098,
      0x00a8,
      0x0092,
      none,
      none,
      0x008d,
      0x0094,
      0x0015,
      0x0083,
      0x0043,
      none,
      none,
      0x000d,
      none,
      0x0007,
      none,
      0x0025,
      0x007f,
      0x001d,
      none,
      0x0076,
      0x009b,
      0x0064,
      0x0085,
      none,
      none,
      none,
      0x000f,
      0x007e,
      none,
      0x009c,
      0x0017,
      0x0012,
      0x0010,
      none,
      0x0021,
      0x008b,
      0x0079,
      0x0001,
      0x00ae,
      none,
      0x002d,
      none,
      none,
      none,
      0x0086,
      none,
      0x0062,
      none,
      0x0057,
      0x0042,
      0x0039,
};

void *
anv_lookup_entrypoint(const struct gen_device_info *devinfo, const char *name)
{
   static const uint32_t prime_factor = 5024183;
   static const uint32_t prime_step = 19;
   const struct anv_entrypoint *e;
   uint32_t hash, h, i;
   const char *p;

   hash = 0;
   for (p = name; *p; p++)
      hash = hash * prime_factor + *p;

   h = hash;
   do {
      i = map[h & 255];
      if (i == none)
         return NULL;
      e = &entrypoints[i];
      h += prime_step;
   } while (e->hash != hash);

   if (strcmp(name, strings + e->name) != 0)
      return NULL;

   return anv_resolve_entrypoint(devinfo, i);
}