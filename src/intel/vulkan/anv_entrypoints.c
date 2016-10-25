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

/* DO NOT EDIT! This is a generated file. */

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
   "vkGetPhysicalDeviceFeatures\0"
   "vkGetPhysicalDeviceFormatProperties\0"
   "vkGetPhysicalDeviceImageFormatProperties\0"
   "vkGetPhysicalDeviceProperties\0"
   "vkGetPhysicalDeviceQueueFamilyProperties\0"
   "vkGetPhysicalDeviceMemoryProperties\0"
   "vkGetInstanceProcAddr\0"
   "vkGetDeviceProcAddr\0"
   "vkCreateDevice\0"
   "vkDestroyDevice\0"
   "vkEnumerateInstanceExtensionProperties\0"
   "vkEnumerateDeviceExtensionProperties\0"
   "vkEnumerateInstanceLayerProperties\0"
   "vkEnumerateDeviceLayerProperties\0"
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
   "vkBindBufferMemory\0"
   "vkBindImageMemory\0"
   "vkGetBufferMemoryRequirements\0"
   "vkGetImageMemoryRequirements\0"
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
   "vkGetPhysicalDeviceDisplayPropertiesKHR\0"
   "vkGetPhysicalDeviceDisplayPlanePropertiesKHR\0"
   "vkGetDisplayPlaneSupportedDisplaysKHR\0"
   "vkGetDisplayModePropertiesKHR\0"
   "vkCreateDisplayModeKHR\0"
   "vkGetDisplayPlaneCapabilitiesKHR\0"
   "vkCreateDisplayPlaneSurfaceKHR\0"
   "vkCreateSharedSwapchainsKHR\0"
   "vkCreateXlibSurfaceKHR\0"
   "vkGetPhysicalDeviceXlibPresentationSupportKHR\0"
   "vkCreateXcbSurfaceKHR\0"
   "vkGetPhysicalDeviceXcbPresentationSupportKHR\0"
   "vkCreateWaylandSurfaceKHR\0"
   "vkGetPhysicalDeviceWaylandPresentationSupportKHR\0"
   "vkCreateMirSurfaceKHR\0"
   "vkGetPhysicalDeviceMirPresentationSupportKHR\0"
   "vkCreateAndroidSurfaceKHR\0"
   "vkCreateWin32SurfaceKHR\0"
   "vkGetPhysicalDeviceWin32PresentationSupportKHR\0"
   "vkCreateDebugReportCallbackEXT\0"
   "vkDestroyDebugReportCallbackEXT\0"
   "vkDebugReportMessageEXT\0"
   "vkCreateDmaBufImageINTEL\0"
   ;

static const struct anv_entrypoint entrypoints[] = {
   {     0, 0x38a581a6 },
   {    17, 0x9bd21af2 },
   {    35, 0x5787c327 },
   {    62, 0x113e2f33 },
   {    90, 0x3e54b398 },
   {   126, 0xdd36a867 },
   {   167, 0x52fe22c9 },
   {   197, 0x4e5fc88a },
   {   238, 0xa90da4da },
   {   274, 0x3d2ae9ad },
   {   296, 0xba013486 },
   {   316, 0x085ed23f },
   {   331, 0x1fbcc9cb },
   {   347, 0xeb27627e },
   {   386, 0x5fd13eed },
   {   423, 0x081f69d8 },
   {   458, 0x2f8566e7 },
   {   491, 0xcc920d9a },
   {   508, 0xfa4713ec },
   {   522, 0x6f8fc2a5 },
   {   538, 0xd46c5f24 },
   {   555, 0x522b85d3 },
   {   572, 0x8f6f838a },
   {   585, 0xcb977bd8 },
   {   597, 0x1a1a0e2f },
   {   611, 0xff52f051 },
   {   637, 0x1e115cca },
   {   668, 0x46e38db5 },
   {   696, 0x06bcbdcb },
   {   715, 0x5caaae4a },
   {   733, 0xab98422a },
   {   763, 0x916f1e63 },
   {   792, 0x15855f5b },
   {   827, 0x272ef8ef },
   {   874, 0xc3628a09 },
   {   892, 0x958af968 },
   {   906, 0xfc64ee3c },
   {   921, 0x684781dc },
   {   935, 0x5f391892 },
   {   952, 0x19d64c81 },
   {   968, 0xf2065e5b },
   {   986, 0xcaab1faf },
   {  1005, 0xe7188731 },
   {  1019, 0x4df27c05 },
   {  1034, 0x096d834b },
   {  1051, 0x592ae5f5 },
   {  1062, 0x6d373ba8 },
   {  1075, 0x5edcd92b },
   {  1093, 0x37819a7f },
   {  1112, 0xbf3f2cb3 },
   {  1134, 0x7d4282b9 },
   {  1149, 0x94a07a45 },
   {  1165, 0x925bd256 },
   {  1184, 0x98b27962 },
   {  1204, 0x652128c2 },
   {  1218, 0xcbfb1d96 },
   {  1233, 0x9163b686 },
   {  1261, 0xdce077ff },
   {  1279, 0xb5853953 },
   {  1298, 0xa0d3cea2 },
   {  1319, 0x2d77af6e },
   {  1341, 0xcbf6489f },
   {  1363, 0x4112a673 },
   {  1386, 0x2092a349 },
   {  1409, 0xc3499606 },
   {  1431, 0x4b59f96d },
   {  1457, 0xf70c85eb },
   {  1482, 0x6aac68af },
   {  1500, 0x451ef1ed },
   {  1523, 0x9146f879 },
   {  1547, 0x013cf03f },
   {  1563, 0x3b645153 },
   {  1580, 0x3c14cc74 },
   {  1608, 0xa4227b08 },
   {  1637, 0xfb95a8a4 },
   {  1660, 0x47bdaf30 },
   {  1684, 0x09bd85f5 },
   {  1706, 0x4c449d3a },
   {  1731, 0x7a1347b1 },
   {  1752, 0xbfd090ae },
   {  1775, 0x887a38c4 },
   {  1795, 0xdc428e58 },
   {  1816, 0x109a9c18 },
   {  1835, 0x16f14324 },
   {  1855, 0xa9820d22 },
   {  1882, 0x820fe476 },
   {  1902, 0xd5d83a0a },
   {  1923, 0x6da9f7fd },
   {  1942, 0x8c0c811a },
   {  1967, 0xb9db2b91 },
   {  1988, 0xc54f7327 },
   {  2009, 0xaffb5725 },
   {  2028, 0x847dc731 },
   {  2049, 0x3af9fd84 },
   {  2067, 0x053d6c2b },
   {  2084, 0x48f28c7f },
   {  2100, 0x32282165 },
   {  2118, 0x30f14d07 },
   {  2136, 0x1c989dfb },
   {  2159, 0x7b3a8a63 },
   {  2179, 0xa8f534e2 },
   {  2206, 0xe7c4b134 },
   {  2231, 0x83e2b024 },
   {  2256, 0x28c7a5da },
   {  2280, 0x4c22d870 },
   {  2301, 0xa9c83f1d },
   {  2324, 0x9912c1a1 },
   {  2334, 0xbe5a8058 },
   {  2351, 0xe9ac41bf },
   {  2369, 0x94e7ed36 },
   {  2394, 0xbd58e867 },
   {  2408, 0xd6353005 },
   {  2430, 0xc939a0da },
   {  2446, 0x278effa9 },
   {  2461, 0x331ebf89 },
   {  2476, 0x0929847e },
   {  2499, 0x68cddbac },
   {  2522, 0xd2986b5e },
   {  2540, 0x5bdd2ae0 },
   {  2556, 0xb4bc8d08 },
   {  2577, 0x4f88e4ba },
   {  2605, 0x93cb5cb8 },
   {  2627, 0x671bb594 },
   {  2645, 0xe257f075 },
   {  2659, 0x4fccce28 },
   {  2675, 0x3b9346b3 },
   {  2691, 0x97fccfe8 },
   {  2712, 0xf5064ea4 },
   {  2728, 0xd556fd22 },
   {  2742, 0x2f614082 },
   {  2762, 0xec4d324c },
   {  2782, 0xdee8c6d4 },
   {  2808, 0xb1c6b468 },
   {  2827, 0xcb7a58e3 },
   {  2848, 0x2eeec2f9 },
   {  2865, 0xdcdb0235 },
   {  2884, 0x9eaabe40 },
   {  2905, 0xf204ce7d },
   {  2925, 0x1a687885 },
   {  2962, 0x77890558 },
   {  3004, 0xe32227c8 },
   {  3041, 0x31c3cbd1 },
   {  3083, 0xcdefcaa8 },
   {  3104, 0x5a93ab74 },
   {  3126, 0x57695f28 },
   {  3150, 0xc3fedb2e },
   {  3172, 0xfc5fb6ce },
   {  3190, 0x0fa0cd2e },
   {  3230, 0xb9b8ddba },
   {  3275, 0xabef4889 },
   {  3313, 0x36b8a8de },
   {  3343, 0xcc0bde41 },
   {  3366, 0x4b60d48c },
   {  3399, 0x7ac4dacb },
   {  3430, 0x47655c4a },
   {  3458, 0xa693bc66 },
   {  3481, 0x34a063ab },
   {  3527, 0xc5e5b106 },
   {  3549, 0x41782cb9 },
   {  3594, 0x2b2a4b79 },
   {  3620, 0x84e085ac },
   {  3669, 0x2ce93a55 },
   {  3691, 0xcf1e6028 },
   {  3736, 0x03667f4e },
   {  3762, 0xfa2ba1e2 },
   {  3786, 0x80e72505 },
   {  3833, 0x0987ef56 },
   {  3864, 0x43d4c4e2 },
   {  3896, 0xa4e75334 },
   {  3920, 0x6392dfa7 },
};



/* Weak aliases for all potential implementations. These will resolve to
 * NULL if they're not defined, which lets the resolve_entrypoint() function
 * either pick the correct entry point.
 */

VkResult anv_CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) __attribute__ ((weak));
void anv_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
VkResult anv_EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) __attribute__ ((weak));
void anv_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures) __attribute__ ((weak));
void anv_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties) __attribute__ ((weak));
VkResult anv_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* pImageFormatProperties) __attribute__ ((weak));
void anv_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) __attribute__ ((weak));
void anv_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties) __attribute__ ((weak));
void anv_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties) __attribute__ ((weak));
PFN_vkVoidFunction anv_GetInstanceProcAddr(VkInstance instance, const char* pName) __attribute__ ((weak));
PFN_vkVoidFunction anv_GetDeviceProcAddr(VkDevice device, const char* pName) __attribute__ ((weak));
VkResult anv_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) __attribute__ ((weak));
void anv_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
VkResult anv_EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
VkResult anv_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
VkResult anv_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
VkResult anv_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
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
VkResult anv_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
VkResult anv_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
void anv_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
void anv_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
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
void anv_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y, uint32_t z) __attribute__ ((weak));
void anv_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset) __attribute__ ((weak));
void anv_CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions) __attribute__ ((weak));
void anv_CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions) __attribute__ ((weak));
void anv_CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter) __attribute__ ((weak));
void anv_CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
void anv_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
void anv_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const uint32_t* pData) __attribute__ ((weak));
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
VkResult anv_GetPhysicalDeviceDisplayPropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayPropertiesKHR* pProperties) __attribute__ ((weak));
VkResult anv_GetPhysicalDeviceDisplayPlanePropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayPlanePropertiesKHR* pProperties) __attribute__ ((weak));
VkResult anv_GetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice physicalDevice, uint32_t planeIndex, uint32_t* pDisplayCount, VkDisplayKHR* pDisplays) __attribute__ ((weak));
VkResult anv_GetDisplayModePropertiesKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, uint32_t* pPropertyCount, VkDisplayModePropertiesKHR* pProperties) __attribute__ ((weak));
VkResult anv_CreateDisplayModeKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, const VkDisplayModeCreateInfoKHR*pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDisplayModeKHR* pMode) __attribute__ ((weak));
VkResult anv_GetDisplayPlaneCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkDisplayModeKHR mode, uint32_t planeIndex, VkDisplayPlaneCapabilitiesKHR* pCapabilities) __attribute__ ((weak));
VkResult anv_CreateDisplayPlaneSurfaceKHR(VkInstance instance, const VkDisplaySurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
VkResult anv_CreateSharedSwapchainsKHR(VkDevice device, uint32_t swapchainCount, const VkSwapchainCreateInfoKHR* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchains) __attribute__ ((weak));
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
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
VkResult anv_CreateWaylandSurfaceKHR(VkInstance instance, const VkWaylandSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
VkBool32 anv_GetPhysicalDeviceWaylandPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, struct wl_display* display) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
VkResult anv_CreateMirSurfaceKHR(VkInstance instance, const VkMirSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
VkBool32 anv_GetPhysicalDeviceMirPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, MirConnection* connection) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_ANDROID_KHR
VkResult anv_CreateAndroidSurfaceKHR(VkInstance instance, const VkAndroidSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_ANDROID_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
VkResult anv_CreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
VkBool32 anv_GetPhysicalDeviceWin32PresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WIN32_KHR
VkResult anv_CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback) __attribute__ ((weak));
void anv_DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
void anv_DebugReportMessageEXT(VkInstance instance, VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage) __attribute__ ((weak));
VkResult anv_CreateDmaBufImageINTEL(VkDevice device, const VkDmaBufImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMem, VkImage* pImage) __attribute__ ((weak));

const struct anv_dispatch_table anv_layer = {
   .CreateInstance = anv_CreateInstance,
   .DestroyInstance = anv_DestroyInstance,
   .EnumeratePhysicalDevices = anv_EnumeratePhysicalDevices,
   .GetPhysicalDeviceFeatures = anv_GetPhysicalDeviceFeatures,
   .GetPhysicalDeviceFormatProperties = anv_GetPhysicalDeviceFormatProperties,
   .GetPhysicalDeviceImageFormatProperties = anv_GetPhysicalDeviceImageFormatProperties,
   .GetPhysicalDeviceProperties = anv_GetPhysicalDeviceProperties,
   .GetPhysicalDeviceQueueFamilyProperties = anv_GetPhysicalDeviceQueueFamilyProperties,
   .GetPhysicalDeviceMemoryProperties = anv_GetPhysicalDeviceMemoryProperties,
   .GetInstanceProcAddr = anv_GetInstanceProcAddr,
   .GetDeviceProcAddr = anv_GetDeviceProcAddr,
   .CreateDevice = anv_CreateDevice,
   .DestroyDevice = anv_DestroyDevice,
   .EnumerateInstanceExtensionProperties = anv_EnumerateInstanceExtensionProperties,
   .EnumerateDeviceExtensionProperties = anv_EnumerateDeviceExtensionProperties,
   .EnumerateInstanceLayerProperties = anv_EnumerateInstanceLayerProperties,
   .EnumerateDeviceLayerProperties = anv_EnumerateDeviceLayerProperties,
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
   .BindBufferMemory = anv_BindBufferMemory,
   .BindImageMemory = anv_BindImageMemory,
   .GetBufferMemoryRequirements = anv_GetBufferMemoryRequirements,
   .GetImageMemoryRequirements = anv_GetImageMemoryRequirements,
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
   .GetPhysicalDeviceDisplayPropertiesKHR = anv_GetPhysicalDeviceDisplayPropertiesKHR,
   .GetPhysicalDeviceDisplayPlanePropertiesKHR = anv_GetPhysicalDeviceDisplayPlanePropertiesKHR,
   .GetDisplayPlaneSupportedDisplaysKHR = anv_GetDisplayPlaneSupportedDisplaysKHR,
   .GetDisplayModePropertiesKHR = anv_GetDisplayModePropertiesKHR,
   .CreateDisplayModeKHR = anv_CreateDisplayModeKHR,
   .GetDisplayPlaneCapabilitiesKHR = anv_GetDisplayPlaneCapabilitiesKHR,
   .CreateDisplayPlaneSurfaceKHR = anv_CreateDisplayPlaneSurfaceKHR,
   .CreateSharedSwapchainsKHR = anv_CreateSharedSwapchainsKHR,
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
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .CreateWaylandSurfaceKHR = anv_CreateWaylandSurfaceKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .GetPhysicalDeviceWaylandPresentationSupportKHR = anv_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
   .CreateMirSurfaceKHR = anv_CreateMirSurfaceKHR,
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
   .GetPhysicalDeviceMirPresentationSupportKHR = anv_GetPhysicalDeviceMirPresentationSupportKHR,
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_ANDROID_KHR
   .CreateAndroidSurfaceKHR = anv_CreateAndroidSurfaceKHR,
#endif // VK_USE_PLATFORM_ANDROID_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
   .CreateWin32SurfaceKHR = anv_CreateWin32SurfaceKHR,
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
   .GetPhysicalDeviceWin32PresentationSupportKHR = anv_GetPhysicalDeviceWin32PresentationSupportKHR,
#endif // VK_USE_PLATFORM_WIN32_KHR
   .CreateDebugReportCallbackEXT = anv_CreateDebugReportCallbackEXT,
   .DestroyDebugReportCallbackEXT = anv_DestroyDebugReportCallbackEXT,
   .DebugReportMessageEXT = anv_DebugReportMessageEXT,
   .CreateDmaBufImageINTEL = anv_CreateDmaBufImageINTEL,
};

VkResult gen7_CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) __attribute__ ((weak));
void gen7_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
VkResult gen7_EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) __attribute__ ((weak));
void gen7_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures) __attribute__ ((weak));
void gen7_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties) __attribute__ ((weak));
VkResult gen7_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* pImageFormatProperties) __attribute__ ((weak));
void gen7_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) __attribute__ ((weak));
void gen7_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties) __attribute__ ((weak));
void gen7_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties) __attribute__ ((weak));
PFN_vkVoidFunction gen7_GetInstanceProcAddr(VkInstance instance, const char* pName) __attribute__ ((weak));
PFN_vkVoidFunction gen7_GetDeviceProcAddr(VkDevice device, const char* pName) __attribute__ ((weak));
VkResult gen7_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) __attribute__ ((weak));
void gen7_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
VkResult gen7_EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
VkResult gen7_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
VkResult gen7_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
VkResult gen7_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
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
VkResult gen7_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
VkResult gen7_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
void gen7_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
void gen7_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
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
void gen7_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y, uint32_t z) __attribute__ ((weak));
void gen7_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset) __attribute__ ((weak));
void gen7_CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions) __attribute__ ((weak));
void gen7_CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions) __attribute__ ((weak));
void gen7_CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter) __attribute__ ((weak));
void gen7_CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
void gen7_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
void gen7_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const uint32_t* pData) __attribute__ ((weak));
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
VkResult gen7_GetPhysicalDeviceDisplayPropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayPropertiesKHR* pProperties) __attribute__ ((weak));
VkResult gen7_GetPhysicalDeviceDisplayPlanePropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayPlanePropertiesKHR* pProperties) __attribute__ ((weak));
VkResult gen7_GetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice physicalDevice, uint32_t planeIndex, uint32_t* pDisplayCount, VkDisplayKHR* pDisplays) __attribute__ ((weak));
VkResult gen7_GetDisplayModePropertiesKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, uint32_t* pPropertyCount, VkDisplayModePropertiesKHR* pProperties) __attribute__ ((weak));
VkResult gen7_CreateDisplayModeKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, const VkDisplayModeCreateInfoKHR*pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDisplayModeKHR* pMode) __attribute__ ((weak));
VkResult gen7_GetDisplayPlaneCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkDisplayModeKHR mode, uint32_t planeIndex, VkDisplayPlaneCapabilitiesKHR* pCapabilities) __attribute__ ((weak));
VkResult gen7_CreateDisplayPlaneSurfaceKHR(VkInstance instance, const VkDisplaySurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
VkResult gen7_CreateSharedSwapchainsKHR(VkDevice device, uint32_t swapchainCount, const VkSwapchainCreateInfoKHR* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchains) __attribute__ ((weak));
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
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
VkResult gen7_CreateWaylandSurfaceKHR(VkInstance instance, const VkWaylandSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
VkBool32 gen7_GetPhysicalDeviceWaylandPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, struct wl_display* display) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
VkResult gen7_CreateMirSurfaceKHR(VkInstance instance, const VkMirSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
VkBool32 gen7_GetPhysicalDeviceMirPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, MirConnection* connection) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_ANDROID_KHR
VkResult gen7_CreateAndroidSurfaceKHR(VkInstance instance, const VkAndroidSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_ANDROID_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
VkResult gen7_CreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
VkBool32 gen7_GetPhysicalDeviceWin32PresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WIN32_KHR
VkResult gen7_CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback) __attribute__ ((weak));
void gen7_DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
void gen7_DebugReportMessageEXT(VkInstance instance, VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage) __attribute__ ((weak));
VkResult gen7_CreateDmaBufImageINTEL(VkDevice device, const VkDmaBufImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMem, VkImage* pImage) __attribute__ ((weak));

const struct anv_dispatch_table gen7_layer = {
   .CreateInstance = gen7_CreateInstance,
   .DestroyInstance = gen7_DestroyInstance,
   .EnumeratePhysicalDevices = gen7_EnumeratePhysicalDevices,
   .GetPhysicalDeviceFeatures = gen7_GetPhysicalDeviceFeatures,
   .GetPhysicalDeviceFormatProperties = gen7_GetPhysicalDeviceFormatProperties,
   .GetPhysicalDeviceImageFormatProperties = gen7_GetPhysicalDeviceImageFormatProperties,
   .GetPhysicalDeviceProperties = gen7_GetPhysicalDeviceProperties,
   .GetPhysicalDeviceQueueFamilyProperties = gen7_GetPhysicalDeviceQueueFamilyProperties,
   .GetPhysicalDeviceMemoryProperties = gen7_GetPhysicalDeviceMemoryProperties,
   .GetInstanceProcAddr = gen7_GetInstanceProcAddr,
   .GetDeviceProcAddr = gen7_GetDeviceProcAddr,
   .CreateDevice = gen7_CreateDevice,
   .DestroyDevice = gen7_DestroyDevice,
   .EnumerateInstanceExtensionProperties = gen7_EnumerateInstanceExtensionProperties,
   .EnumerateDeviceExtensionProperties = gen7_EnumerateDeviceExtensionProperties,
   .EnumerateInstanceLayerProperties = gen7_EnumerateInstanceLayerProperties,
   .EnumerateDeviceLayerProperties = gen7_EnumerateDeviceLayerProperties,
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
   .BindBufferMemory = gen7_BindBufferMemory,
   .BindImageMemory = gen7_BindImageMemory,
   .GetBufferMemoryRequirements = gen7_GetBufferMemoryRequirements,
   .GetImageMemoryRequirements = gen7_GetImageMemoryRequirements,
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
   .GetPhysicalDeviceDisplayPropertiesKHR = gen7_GetPhysicalDeviceDisplayPropertiesKHR,
   .GetPhysicalDeviceDisplayPlanePropertiesKHR = gen7_GetPhysicalDeviceDisplayPlanePropertiesKHR,
   .GetDisplayPlaneSupportedDisplaysKHR = gen7_GetDisplayPlaneSupportedDisplaysKHR,
   .GetDisplayModePropertiesKHR = gen7_GetDisplayModePropertiesKHR,
   .CreateDisplayModeKHR = gen7_CreateDisplayModeKHR,
   .GetDisplayPlaneCapabilitiesKHR = gen7_GetDisplayPlaneCapabilitiesKHR,
   .CreateDisplayPlaneSurfaceKHR = gen7_CreateDisplayPlaneSurfaceKHR,
   .CreateSharedSwapchainsKHR = gen7_CreateSharedSwapchainsKHR,
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
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .CreateWaylandSurfaceKHR = gen7_CreateWaylandSurfaceKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .GetPhysicalDeviceWaylandPresentationSupportKHR = gen7_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
   .CreateMirSurfaceKHR = gen7_CreateMirSurfaceKHR,
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
   .GetPhysicalDeviceMirPresentationSupportKHR = gen7_GetPhysicalDeviceMirPresentationSupportKHR,
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_ANDROID_KHR
   .CreateAndroidSurfaceKHR = gen7_CreateAndroidSurfaceKHR,
#endif // VK_USE_PLATFORM_ANDROID_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
   .CreateWin32SurfaceKHR = gen7_CreateWin32SurfaceKHR,
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
   .GetPhysicalDeviceWin32PresentationSupportKHR = gen7_GetPhysicalDeviceWin32PresentationSupportKHR,
#endif // VK_USE_PLATFORM_WIN32_KHR
   .CreateDebugReportCallbackEXT = gen7_CreateDebugReportCallbackEXT,
   .DestroyDebugReportCallbackEXT = gen7_DestroyDebugReportCallbackEXT,
   .DebugReportMessageEXT = gen7_DebugReportMessageEXT,
   .CreateDmaBufImageINTEL = gen7_CreateDmaBufImageINTEL,
};

VkResult gen75_CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) __attribute__ ((weak));
void gen75_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
VkResult gen75_EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) __attribute__ ((weak));
void gen75_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures) __attribute__ ((weak));
void gen75_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties) __attribute__ ((weak));
VkResult gen75_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* pImageFormatProperties) __attribute__ ((weak));
void gen75_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) __attribute__ ((weak));
void gen75_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties) __attribute__ ((weak));
void gen75_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties) __attribute__ ((weak));
PFN_vkVoidFunction gen75_GetInstanceProcAddr(VkInstance instance, const char* pName) __attribute__ ((weak));
PFN_vkVoidFunction gen75_GetDeviceProcAddr(VkDevice device, const char* pName) __attribute__ ((weak));
VkResult gen75_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) __attribute__ ((weak));
void gen75_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
VkResult gen75_EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
VkResult gen75_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
VkResult gen75_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
VkResult gen75_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
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
VkResult gen75_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
VkResult gen75_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
void gen75_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
void gen75_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
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
void gen75_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y, uint32_t z) __attribute__ ((weak));
void gen75_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset) __attribute__ ((weak));
void gen75_CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions) __attribute__ ((weak));
void gen75_CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions) __attribute__ ((weak));
void gen75_CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter) __attribute__ ((weak));
void gen75_CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
void gen75_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
void gen75_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const uint32_t* pData) __attribute__ ((weak));
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
VkResult gen75_GetPhysicalDeviceDisplayPropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayPropertiesKHR* pProperties) __attribute__ ((weak));
VkResult gen75_GetPhysicalDeviceDisplayPlanePropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayPlanePropertiesKHR* pProperties) __attribute__ ((weak));
VkResult gen75_GetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice physicalDevice, uint32_t planeIndex, uint32_t* pDisplayCount, VkDisplayKHR* pDisplays) __attribute__ ((weak));
VkResult gen75_GetDisplayModePropertiesKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, uint32_t* pPropertyCount, VkDisplayModePropertiesKHR* pProperties) __attribute__ ((weak));
VkResult gen75_CreateDisplayModeKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, const VkDisplayModeCreateInfoKHR*pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDisplayModeKHR* pMode) __attribute__ ((weak));
VkResult gen75_GetDisplayPlaneCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkDisplayModeKHR mode, uint32_t planeIndex, VkDisplayPlaneCapabilitiesKHR* pCapabilities) __attribute__ ((weak));
VkResult gen75_CreateDisplayPlaneSurfaceKHR(VkInstance instance, const VkDisplaySurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
VkResult gen75_CreateSharedSwapchainsKHR(VkDevice device, uint32_t swapchainCount, const VkSwapchainCreateInfoKHR* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchains) __attribute__ ((weak));
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
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
VkResult gen75_CreateWaylandSurfaceKHR(VkInstance instance, const VkWaylandSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
VkBool32 gen75_GetPhysicalDeviceWaylandPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, struct wl_display* display) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
VkResult gen75_CreateMirSurfaceKHR(VkInstance instance, const VkMirSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
VkBool32 gen75_GetPhysicalDeviceMirPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, MirConnection* connection) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_ANDROID_KHR
VkResult gen75_CreateAndroidSurfaceKHR(VkInstance instance, const VkAndroidSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_ANDROID_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
VkResult gen75_CreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
VkBool32 gen75_GetPhysicalDeviceWin32PresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WIN32_KHR
VkResult gen75_CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback) __attribute__ ((weak));
void gen75_DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
void gen75_DebugReportMessageEXT(VkInstance instance, VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage) __attribute__ ((weak));
VkResult gen75_CreateDmaBufImageINTEL(VkDevice device, const VkDmaBufImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMem, VkImage* pImage) __attribute__ ((weak));

const struct anv_dispatch_table gen75_layer = {
   .CreateInstance = gen75_CreateInstance,
   .DestroyInstance = gen75_DestroyInstance,
   .EnumeratePhysicalDevices = gen75_EnumeratePhysicalDevices,
   .GetPhysicalDeviceFeatures = gen75_GetPhysicalDeviceFeatures,
   .GetPhysicalDeviceFormatProperties = gen75_GetPhysicalDeviceFormatProperties,
   .GetPhysicalDeviceImageFormatProperties = gen75_GetPhysicalDeviceImageFormatProperties,
   .GetPhysicalDeviceProperties = gen75_GetPhysicalDeviceProperties,
   .GetPhysicalDeviceQueueFamilyProperties = gen75_GetPhysicalDeviceQueueFamilyProperties,
   .GetPhysicalDeviceMemoryProperties = gen75_GetPhysicalDeviceMemoryProperties,
   .GetInstanceProcAddr = gen75_GetInstanceProcAddr,
   .GetDeviceProcAddr = gen75_GetDeviceProcAddr,
   .CreateDevice = gen75_CreateDevice,
   .DestroyDevice = gen75_DestroyDevice,
   .EnumerateInstanceExtensionProperties = gen75_EnumerateInstanceExtensionProperties,
   .EnumerateDeviceExtensionProperties = gen75_EnumerateDeviceExtensionProperties,
   .EnumerateInstanceLayerProperties = gen75_EnumerateInstanceLayerProperties,
   .EnumerateDeviceLayerProperties = gen75_EnumerateDeviceLayerProperties,
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
   .BindBufferMemory = gen75_BindBufferMemory,
   .BindImageMemory = gen75_BindImageMemory,
   .GetBufferMemoryRequirements = gen75_GetBufferMemoryRequirements,
   .GetImageMemoryRequirements = gen75_GetImageMemoryRequirements,
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
   .GetPhysicalDeviceDisplayPropertiesKHR = gen75_GetPhysicalDeviceDisplayPropertiesKHR,
   .GetPhysicalDeviceDisplayPlanePropertiesKHR = gen75_GetPhysicalDeviceDisplayPlanePropertiesKHR,
   .GetDisplayPlaneSupportedDisplaysKHR = gen75_GetDisplayPlaneSupportedDisplaysKHR,
   .GetDisplayModePropertiesKHR = gen75_GetDisplayModePropertiesKHR,
   .CreateDisplayModeKHR = gen75_CreateDisplayModeKHR,
   .GetDisplayPlaneCapabilitiesKHR = gen75_GetDisplayPlaneCapabilitiesKHR,
   .CreateDisplayPlaneSurfaceKHR = gen75_CreateDisplayPlaneSurfaceKHR,
   .CreateSharedSwapchainsKHR = gen75_CreateSharedSwapchainsKHR,
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
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .CreateWaylandSurfaceKHR = gen75_CreateWaylandSurfaceKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .GetPhysicalDeviceWaylandPresentationSupportKHR = gen75_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
   .CreateMirSurfaceKHR = gen75_CreateMirSurfaceKHR,
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
   .GetPhysicalDeviceMirPresentationSupportKHR = gen75_GetPhysicalDeviceMirPresentationSupportKHR,
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_ANDROID_KHR
   .CreateAndroidSurfaceKHR = gen75_CreateAndroidSurfaceKHR,
#endif // VK_USE_PLATFORM_ANDROID_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
   .CreateWin32SurfaceKHR = gen75_CreateWin32SurfaceKHR,
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
   .GetPhysicalDeviceWin32PresentationSupportKHR = gen75_GetPhysicalDeviceWin32PresentationSupportKHR,
#endif // VK_USE_PLATFORM_WIN32_KHR
   .CreateDebugReportCallbackEXT = gen75_CreateDebugReportCallbackEXT,
   .DestroyDebugReportCallbackEXT = gen75_DestroyDebugReportCallbackEXT,
   .DebugReportMessageEXT = gen75_DebugReportMessageEXT,
   .CreateDmaBufImageINTEL = gen75_CreateDmaBufImageINTEL,
};

VkResult gen8_CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) __attribute__ ((weak));
void gen8_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
VkResult gen8_EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) __attribute__ ((weak));
void gen8_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures) __attribute__ ((weak));
void gen8_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties) __attribute__ ((weak));
VkResult gen8_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* pImageFormatProperties) __attribute__ ((weak));
void gen8_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) __attribute__ ((weak));
void gen8_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties) __attribute__ ((weak));
void gen8_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties) __attribute__ ((weak));
PFN_vkVoidFunction gen8_GetInstanceProcAddr(VkInstance instance, const char* pName) __attribute__ ((weak));
PFN_vkVoidFunction gen8_GetDeviceProcAddr(VkDevice device, const char* pName) __attribute__ ((weak));
VkResult gen8_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) __attribute__ ((weak));
void gen8_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
VkResult gen8_EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
VkResult gen8_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
VkResult gen8_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
VkResult gen8_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
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
VkResult gen8_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
VkResult gen8_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
void gen8_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
void gen8_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
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
void gen8_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y, uint32_t z) __attribute__ ((weak));
void gen8_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset) __attribute__ ((weak));
void gen8_CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions) __attribute__ ((weak));
void gen8_CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions) __attribute__ ((weak));
void gen8_CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter) __attribute__ ((weak));
void gen8_CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
void gen8_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
void gen8_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const uint32_t* pData) __attribute__ ((weak));
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
VkResult gen8_GetPhysicalDeviceDisplayPropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayPropertiesKHR* pProperties) __attribute__ ((weak));
VkResult gen8_GetPhysicalDeviceDisplayPlanePropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayPlanePropertiesKHR* pProperties) __attribute__ ((weak));
VkResult gen8_GetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice physicalDevice, uint32_t planeIndex, uint32_t* pDisplayCount, VkDisplayKHR* pDisplays) __attribute__ ((weak));
VkResult gen8_GetDisplayModePropertiesKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, uint32_t* pPropertyCount, VkDisplayModePropertiesKHR* pProperties) __attribute__ ((weak));
VkResult gen8_CreateDisplayModeKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, const VkDisplayModeCreateInfoKHR*pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDisplayModeKHR* pMode) __attribute__ ((weak));
VkResult gen8_GetDisplayPlaneCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkDisplayModeKHR mode, uint32_t planeIndex, VkDisplayPlaneCapabilitiesKHR* pCapabilities) __attribute__ ((weak));
VkResult gen8_CreateDisplayPlaneSurfaceKHR(VkInstance instance, const VkDisplaySurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
VkResult gen8_CreateSharedSwapchainsKHR(VkDevice device, uint32_t swapchainCount, const VkSwapchainCreateInfoKHR* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchains) __attribute__ ((weak));
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
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
VkResult gen8_CreateWaylandSurfaceKHR(VkInstance instance, const VkWaylandSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
VkBool32 gen8_GetPhysicalDeviceWaylandPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, struct wl_display* display) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
VkResult gen8_CreateMirSurfaceKHR(VkInstance instance, const VkMirSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
VkBool32 gen8_GetPhysicalDeviceMirPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, MirConnection* connection) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_ANDROID_KHR
VkResult gen8_CreateAndroidSurfaceKHR(VkInstance instance, const VkAndroidSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_ANDROID_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
VkResult gen8_CreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
VkBool32 gen8_GetPhysicalDeviceWin32PresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WIN32_KHR
VkResult gen8_CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback) __attribute__ ((weak));
void gen8_DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
void gen8_DebugReportMessageEXT(VkInstance instance, VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage) __attribute__ ((weak));
VkResult gen8_CreateDmaBufImageINTEL(VkDevice device, const VkDmaBufImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMem, VkImage* pImage) __attribute__ ((weak));

const struct anv_dispatch_table gen8_layer = {
   .CreateInstance = gen8_CreateInstance,
   .DestroyInstance = gen8_DestroyInstance,
   .EnumeratePhysicalDevices = gen8_EnumeratePhysicalDevices,
   .GetPhysicalDeviceFeatures = gen8_GetPhysicalDeviceFeatures,
   .GetPhysicalDeviceFormatProperties = gen8_GetPhysicalDeviceFormatProperties,
   .GetPhysicalDeviceImageFormatProperties = gen8_GetPhysicalDeviceImageFormatProperties,
   .GetPhysicalDeviceProperties = gen8_GetPhysicalDeviceProperties,
   .GetPhysicalDeviceQueueFamilyProperties = gen8_GetPhysicalDeviceQueueFamilyProperties,
   .GetPhysicalDeviceMemoryProperties = gen8_GetPhysicalDeviceMemoryProperties,
   .GetInstanceProcAddr = gen8_GetInstanceProcAddr,
   .GetDeviceProcAddr = gen8_GetDeviceProcAddr,
   .CreateDevice = gen8_CreateDevice,
   .DestroyDevice = gen8_DestroyDevice,
   .EnumerateInstanceExtensionProperties = gen8_EnumerateInstanceExtensionProperties,
   .EnumerateDeviceExtensionProperties = gen8_EnumerateDeviceExtensionProperties,
   .EnumerateInstanceLayerProperties = gen8_EnumerateInstanceLayerProperties,
   .EnumerateDeviceLayerProperties = gen8_EnumerateDeviceLayerProperties,
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
   .BindBufferMemory = gen8_BindBufferMemory,
   .BindImageMemory = gen8_BindImageMemory,
   .GetBufferMemoryRequirements = gen8_GetBufferMemoryRequirements,
   .GetImageMemoryRequirements = gen8_GetImageMemoryRequirements,
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
   .GetPhysicalDeviceDisplayPropertiesKHR = gen8_GetPhysicalDeviceDisplayPropertiesKHR,
   .GetPhysicalDeviceDisplayPlanePropertiesKHR = gen8_GetPhysicalDeviceDisplayPlanePropertiesKHR,
   .GetDisplayPlaneSupportedDisplaysKHR = gen8_GetDisplayPlaneSupportedDisplaysKHR,
   .GetDisplayModePropertiesKHR = gen8_GetDisplayModePropertiesKHR,
   .CreateDisplayModeKHR = gen8_CreateDisplayModeKHR,
   .GetDisplayPlaneCapabilitiesKHR = gen8_GetDisplayPlaneCapabilitiesKHR,
   .CreateDisplayPlaneSurfaceKHR = gen8_CreateDisplayPlaneSurfaceKHR,
   .CreateSharedSwapchainsKHR = gen8_CreateSharedSwapchainsKHR,
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
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .CreateWaylandSurfaceKHR = gen8_CreateWaylandSurfaceKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .GetPhysicalDeviceWaylandPresentationSupportKHR = gen8_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
   .CreateMirSurfaceKHR = gen8_CreateMirSurfaceKHR,
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
   .GetPhysicalDeviceMirPresentationSupportKHR = gen8_GetPhysicalDeviceMirPresentationSupportKHR,
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_ANDROID_KHR
   .CreateAndroidSurfaceKHR = gen8_CreateAndroidSurfaceKHR,
#endif // VK_USE_PLATFORM_ANDROID_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
   .CreateWin32SurfaceKHR = gen8_CreateWin32SurfaceKHR,
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
   .GetPhysicalDeviceWin32PresentationSupportKHR = gen8_GetPhysicalDeviceWin32PresentationSupportKHR,
#endif // VK_USE_PLATFORM_WIN32_KHR
   .CreateDebugReportCallbackEXT = gen8_CreateDebugReportCallbackEXT,
   .DestroyDebugReportCallbackEXT = gen8_DestroyDebugReportCallbackEXT,
   .DebugReportMessageEXT = gen8_DebugReportMessageEXT,
   .CreateDmaBufImageINTEL = gen8_CreateDmaBufImageINTEL,
};

VkResult gen9_CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) __attribute__ ((weak));
void gen9_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
VkResult gen9_EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) __attribute__ ((weak));
void gen9_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures) __attribute__ ((weak));
void gen9_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties) __attribute__ ((weak));
VkResult gen9_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* pImageFormatProperties) __attribute__ ((weak));
void gen9_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) __attribute__ ((weak));
void gen9_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties) __attribute__ ((weak));
void gen9_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties) __attribute__ ((weak));
PFN_vkVoidFunction gen9_GetInstanceProcAddr(VkInstance instance, const char* pName) __attribute__ ((weak));
PFN_vkVoidFunction gen9_GetDeviceProcAddr(VkDevice device, const char* pName) __attribute__ ((weak));
VkResult gen9_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) __attribute__ ((weak));
void gen9_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
VkResult gen9_EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
VkResult gen9_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
VkResult gen9_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
VkResult gen9_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
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
VkResult gen9_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
VkResult gen9_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
void gen9_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
void gen9_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
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
void gen9_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y, uint32_t z) __attribute__ ((weak));
void gen9_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset) __attribute__ ((weak));
void gen9_CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions) __attribute__ ((weak));
void gen9_CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions) __attribute__ ((weak));
void gen9_CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter) __attribute__ ((weak));
void gen9_CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
void gen9_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
void gen9_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const uint32_t* pData) __attribute__ ((weak));
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
VkResult gen9_GetPhysicalDeviceDisplayPropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayPropertiesKHR* pProperties) __attribute__ ((weak));
VkResult gen9_GetPhysicalDeviceDisplayPlanePropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayPlanePropertiesKHR* pProperties) __attribute__ ((weak));
VkResult gen9_GetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice physicalDevice, uint32_t planeIndex, uint32_t* pDisplayCount, VkDisplayKHR* pDisplays) __attribute__ ((weak));
VkResult gen9_GetDisplayModePropertiesKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, uint32_t* pPropertyCount, VkDisplayModePropertiesKHR* pProperties) __attribute__ ((weak));
VkResult gen9_CreateDisplayModeKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, const VkDisplayModeCreateInfoKHR*pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDisplayModeKHR* pMode) __attribute__ ((weak));
VkResult gen9_GetDisplayPlaneCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkDisplayModeKHR mode, uint32_t planeIndex, VkDisplayPlaneCapabilitiesKHR* pCapabilities) __attribute__ ((weak));
VkResult gen9_CreateDisplayPlaneSurfaceKHR(VkInstance instance, const VkDisplaySurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
VkResult gen9_CreateSharedSwapchainsKHR(VkDevice device, uint32_t swapchainCount, const VkSwapchainCreateInfoKHR* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchains) __attribute__ ((weak));
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
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
VkResult gen9_CreateWaylandSurfaceKHR(VkInstance instance, const VkWaylandSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
VkBool32 gen9_GetPhysicalDeviceWaylandPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, struct wl_display* display) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
VkResult gen9_CreateMirSurfaceKHR(VkInstance instance, const VkMirSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
VkBool32 gen9_GetPhysicalDeviceMirPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, MirConnection* connection) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_ANDROID_KHR
VkResult gen9_CreateAndroidSurfaceKHR(VkInstance instance, const VkAndroidSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_ANDROID_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
VkResult gen9_CreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
VkBool32 gen9_GetPhysicalDeviceWin32PresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WIN32_KHR
VkResult gen9_CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback) __attribute__ ((weak));
void gen9_DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
void gen9_DebugReportMessageEXT(VkInstance instance, VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage) __attribute__ ((weak));
VkResult gen9_CreateDmaBufImageINTEL(VkDevice device, const VkDmaBufImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMem, VkImage* pImage) __attribute__ ((weak));

const struct anv_dispatch_table gen9_layer = {
   .CreateInstance = gen9_CreateInstance,
   .DestroyInstance = gen9_DestroyInstance,
   .EnumeratePhysicalDevices = gen9_EnumeratePhysicalDevices,
   .GetPhysicalDeviceFeatures = gen9_GetPhysicalDeviceFeatures,
   .GetPhysicalDeviceFormatProperties = gen9_GetPhysicalDeviceFormatProperties,
   .GetPhysicalDeviceImageFormatProperties = gen9_GetPhysicalDeviceImageFormatProperties,
   .GetPhysicalDeviceProperties = gen9_GetPhysicalDeviceProperties,
   .GetPhysicalDeviceQueueFamilyProperties = gen9_GetPhysicalDeviceQueueFamilyProperties,
   .GetPhysicalDeviceMemoryProperties = gen9_GetPhysicalDeviceMemoryProperties,
   .GetInstanceProcAddr = gen9_GetInstanceProcAddr,
   .GetDeviceProcAddr = gen9_GetDeviceProcAddr,
   .CreateDevice = gen9_CreateDevice,
   .DestroyDevice = gen9_DestroyDevice,
   .EnumerateInstanceExtensionProperties = gen9_EnumerateInstanceExtensionProperties,
   .EnumerateDeviceExtensionProperties = gen9_EnumerateDeviceExtensionProperties,
   .EnumerateInstanceLayerProperties = gen9_EnumerateInstanceLayerProperties,
   .EnumerateDeviceLayerProperties = gen9_EnumerateDeviceLayerProperties,
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
   .BindBufferMemory = gen9_BindBufferMemory,
   .BindImageMemory = gen9_BindImageMemory,
   .GetBufferMemoryRequirements = gen9_GetBufferMemoryRequirements,
   .GetImageMemoryRequirements = gen9_GetImageMemoryRequirements,
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
   .GetPhysicalDeviceDisplayPropertiesKHR = gen9_GetPhysicalDeviceDisplayPropertiesKHR,
   .GetPhysicalDeviceDisplayPlanePropertiesKHR = gen9_GetPhysicalDeviceDisplayPlanePropertiesKHR,
   .GetDisplayPlaneSupportedDisplaysKHR = gen9_GetDisplayPlaneSupportedDisplaysKHR,
   .GetDisplayModePropertiesKHR = gen9_GetDisplayModePropertiesKHR,
   .CreateDisplayModeKHR = gen9_CreateDisplayModeKHR,
   .GetDisplayPlaneCapabilitiesKHR = gen9_GetDisplayPlaneCapabilitiesKHR,
   .CreateDisplayPlaneSurfaceKHR = gen9_CreateDisplayPlaneSurfaceKHR,
   .CreateSharedSwapchainsKHR = gen9_CreateSharedSwapchainsKHR,
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
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .CreateWaylandSurfaceKHR = gen9_CreateWaylandSurfaceKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .GetPhysicalDeviceWaylandPresentationSupportKHR = gen9_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
   .CreateMirSurfaceKHR = gen9_CreateMirSurfaceKHR,
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
   .GetPhysicalDeviceMirPresentationSupportKHR = gen9_GetPhysicalDeviceMirPresentationSupportKHR,
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_ANDROID_KHR
   .CreateAndroidSurfaceKHR = gen9_CreateAndroidSurfaceKHR,
#endif // VK_USE_PLATFORM_ANDROID_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
   .CreateWin32SurfaceKHR = gen9_CreateWin32SurfaceKHR,
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
   .GetPhysicalDeviceWin32PresentationSupportKHR = gen9_GetPhysicalDeviceWin32PresentationSupportKHR,
#endif // VK_USE_PLATFORM_WIN32_KHR
   .CreateDebugReportCallbackEXT = gen9_CreateDebugReportCallbackEXT,
   .DestroyDebugReportCallbackEXT = gen9_DestroyDebugReportCallbackEXT,
   .DebugReportMessageEXT = gen9_DebugReportMessageEXT,
   .CreateDmaBufImageINTEL = gen9_CreateDmaBufImageINTEL,
};


static void * __attribute__ ((noinline))
anv_resolve_entrypoint(const struct gen_device_info *devinfo, uint32_t index)
{
   if (devinfo == NULL) {
      return anv_layer.entrypoints[index];
   }

   switch (devinfo->gen) {
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
 * collisions  entries
 *      0       111
 *      1        30
 *      2        10
 *      3         7
 *      4         6
 *      5         1
 *      6         1
 *      7         1
 *      8         1
 *      9+        2
 */

#define none 0xffff

static const uint16_t map[] = {
    0x0044,   none,   none,   none, 0x0096, 0x002b, 0x0040, 0x0061,
    0x0049, 0x0022, 0x0056,   none,   none,   none, 0x0095,   none,
      none,   none,   none, 0x0067,   none,   none,   none, 0x0099,
    0x0052, 0x009d, 0x0058, 0x004c,   none, 0x0069,   none,   none,
      none,   none, 0x0054,   none, 0x0014, 0x005b, 0x0070, 0x0002,
    0x007c,   none, 0x001e, 0x002f,   none,   none, 0x0077, 0x0018,
    0x004b, 0x002a,   none, 0x0003, 0x0065, 0x0080, 0x006d, 0x0053,
      none,   none, 0x004d, 0x0090, 0x0024,   none, 0x005e, 0x000b,
    0x0088, 0x0091,   none,   none, 0x005c, 0x0033,   none, 0x00a8,
    0x0087, 0x003f, 0x001d, 0x002c, 0x0082, 0x005a, 0x00a2,   none,
      none, 0x0019, 0x0046, 0x003a, 0x0093, 0x00a1, 0x0034,   none,
    0x0051,   none,   none, 0x0020,   none, 0x0066, 0x0075,   none,
      none, 0x00a3, 0x0035, 0x001f, 0x006f, 0x0060, 0x0047, 0x0005,
    0x0023, 0x00a6,   none, 0x006b,   none, 0x0041, 0x0028,   none,
    0x0068,   none,   none, 0x003e, 0x0048, 0x007b, 0x0055, 0x00a5,
      none, 0x0045, 0x006e, 0x0084,   none, 0x0089, 0x000d, 0x0030,
      none, 0x0027, 0x0081, 0x009a, 0x005d, 0x008a, 0x000a, 0x008f,
      none, 0x0063, 0x0007,   none, 0x0098, 0x0097,   none,   none,
      none, 0x0059, 0x0026,   none, 0x003c,   none, 0x0037,   none,
    0x0004, 0x0038, 0x0011,   none, 0x0072, 0x0016,   none, 0x003d,
    0x00a4, 0x006a, 0x003b,   none, 0x004a, 0x0013, 0x0000, 0x007a,
    0x002e, 0x0071,   none, 0x009c, 0x0074, 0x0009, 0x004f, 0x0029,
      none, 0x004e, 0x009b, 0x0031,   none, 0x001b,   none, 0x0073,
    0x005f, 0x0032, 0x0078, 0x008e,   none,   none,   none, 0x006c,
      none,   none, 0x0036,   none, 0x0050, 0x009f, 0x007d,   none,
    0x008c, 0x0006, 0x001a, 0x000c, 0x009e, 0x0094, 0x0092,   none,
      none, 0x008d, 0x00a0, 0x0015, 0x0083, 0x0043,   none,   none,
    0x000f, 0x00a7, 0x0008,   none, 0x0025, 0x007f, 0x001c,   none,
    0x0076,   none, 0x0064, 0x0085,   none,   none,   none, 0x0010,
    0x007e,   none,   none, 0x0017, 0x0012, 0x000e,   none, 0x0021,
    0x008b, 0x0079, 0x0001, 0x00a9,   none, 0x002d,   none,   none,
      none, 0x0086,   none, 0x0062,   none, 0x0057, 0x0042, 0x0039,
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

