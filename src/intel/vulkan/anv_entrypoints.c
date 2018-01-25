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
    "vkCreateDebugReportCallbackEXT\0"
    "vkDestroyDebugReportCallbackEXT\0"
    "vkDebugReportMessageEXT\0"
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
    "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR\0"
    "vkGetSemaphoreFdKHR\0"
    "vkImportSemaphoreFdKHR\0"
    "vkGetPhysicalDeviceExternalFencePropertiesKHR\0"
    "vkGetFenceFdKHR\0"
    "vkImportFenceFdKHR\0"
    "vkBindBufferMemory2KHR\0"
    "vkBindImageMemory2KHR\0"
    "vkCreateDescriptorUpdateTemplateKHR\0"
    "vkDestroyDescriptorUpdateTemplateKHR\0"
    "vkUpdateDescriptorSetWithTemplateKHR\0"
    "vkCmdPushDescriptorSetWithTemplateKHR\0"
    "vkGetPhysicalDeviceSurfaceCapabilities2KHR\0"
    "vkGetPhysicalDeviceSurfaceFormats2KHR\0"
    "vkGetBufferMemoryRequirements2KHR\0"
    "vkGetImageMemoryRequirements2KHR\0"
    "vkGetImageSparseMemoryRequirements2KHR\0"
    "vkCreateSamplerYcbcrConversionKHR\0"
    "vkDestroySamplerYcbcrConversionKHR\0"
    "vkGetSwapchainGrallocUsageANDROID\0"
    "vkAcquireImageANDROID\0"
    "vkQueueSignalReleaseImageANDROID\0"
    "vkCreateDmaBufImageINTEL\0"
;

static const struct anv_entrypoint entrypoints[] = {
    [0] = { 0, 0x38a581a6 }, /* vkCreateInstance */
    [1] = { 17, 0x9bd21af2 }, /* vkDestroyInstance */
    [2] = { 35, 0x5787c327 }, /* vkEnumeratePhysicalDevices */
    [3] = { 62, 0xba013486 }, /* vkGetDeviceProcAddr */
    [4] = { 82, 0x3d2ae9ad }, /* vkGetInstanceProcAddr */
    [5] = { 104, 0x52fe22c9 }, /* vkGetPhysicalDeviceProperties */
    [6] = { 134, 0x4e5fc88a }, /* vkGetPhysicalDeviceQueueFamilyProperties */
    [7] = { 175, 0xa90da4da }, /* vkGetPhysicalDeviceMemoryProperties */
    [8] = { 211, 0x113e2f33 }, /* vkGetPhysicalDeviceFeatures */
    [9] = { 239, 0x3e54b398 }, /* vkGetPhysicalDeviceFormatProperties */
    [10] = { 275, 0xdd36a867 }, /* vkGetPhysicalDeviceImageFormatProperties */
    [11] = { 316, 0x85ed23f }, /* vkCreateDevice */
    [12] = { 331, 0x1fbcc9cb }, /* vkDestroyDevice */
    [13] = { 347, 0x81f69d8 }, /* vkEnumerateInstanceLayerProperties */
    [14] = { 382, 0xeb27627e }, /* vkEnumerateInstanceExtensionProperties */
    [15] = { 421, 0x2f8566e7 }, /* vkEnumerateDeviceLayerProperties */
    [16] = { 454, 0x5fd13eed }, /* vkEnumerateDeviceExtensionProperties */
    [17] = { 491, 0xcc920d9a }, /* vkGetDeviceQueue */
    [18] = { 508, 0xfa4713ec }, /* vkQueueSubmit */
    [19] = { 522, 0x6f8fc2a5 }, /* vkQueueWaitIdle */
    [20] = { 538, 0xd46c5f24 }, /* vkDeviceWaitIdle */
    [21] = { 555, 0x522b85d3 }, /* vkAllocateMemory */
    [22] = { 572, 0x8f6f838a }, /* vkFreeMemory */
    [23] = { 585, 0xcb977bd8 }, /* vkMapMemory */
    [24] = { 597, 0x1a1a0e2f }, /* vkUnmapMemory */
    [25] = { 611, 0xff52f051 }, /* vkFlushMappedMemoryRanges */
    [26] = { 637, 0x1e115cca }, /* vkInvalidateMappedMemoryRanges */
    [27] = { 668, 0x46e38db5 }, /* vkGetDeviceMemoryCommitment */
    [28] = { 696, 0xab98422a }, /* vkGetBufferMemoryRequirements */
    [29] = { 726, 0x6bcbdcb }, /* vkBindBufferMemory */
    [30] = { 745, 0x916f1e63 }, /* vkGetImageMemoryRequirements */
    [31] = { 774, 0x5caaae4a }, /* vkBindImageMemory */
    [32] = { 792, 0x15855f5b }, /* vkGetImageSparseMemoryRequirements */
    [33] = { 827, 0x272ef8ef }, /* vkGetPhysicalDeviceSparseImageFormatProperties */
    [34] = { 874, 0xc3628a09 }, /* vkQueueBindSparse */
    [35] = { 892, 0x958af968 }, /* vkCreateFence */
    [36] = { 906, 0xfc64ee3c }, /* vkDestroyFence */
    [37] = { 921, 0x684781dc }, /* vkResetFences */
    [38] = { 935, 0x5f391892 }, /* vkGetFenceStatus */
    [39] = { 952, 0x19d64c81 }, /* vkWaitForFences */
    [40] = { 968, 0xf2065e5b }, /* vkCreateSemaphore */
    [41] = { 986, 0xcaab1faf }, /* vkDestroySemaphore */
    [42] = { 1005, 0xe7188731 }, /* vkCreateEvent */
    [43] = { 1019, 0x4df27c05 }, /* vkDestroyEvent */
    [44] = { 1034, 0x96d834b }, /* vkGetEventStatus */
    [45] = { 1051, 0x592ae5f5 }, /* vkSetEvent */
    [46] = { 1062, 0x6d373ba8 }, /* vkResetEvent */
    [47] = { 1075, 0x5edcd92b }, /* vkCreateQueryPool */
    [48] = { 1093, 0x37819a7f }, /* vkDestroyQueryPool */
    [49] = { 1112, 0xbf3f2cb3 }, /* vkGetQueryPoolResults */
    [50] = { 1134, 0x7d4282b9 }, /* vkCreateBuffer */
    [51] = { 1149, 0x94a07a45 }, /* vkDestroyBuffer */
    [52] = { 1165, 0x925bd256 }, /* vkCreateBufferView */
    [53] = { 1184, 0x98b27962 }, /* vkDestroyBufferView */
    [54] = { 1204, 0x652128c2 }, /* vkCreateImage */
    [55] = { 1218, 0xcbfb1d96 }, /* vkDestroyImage */
    [56] = { 1233, 0x9163b686 }, /* vkGetImageSubresourceLayout */
    [57] = { 1261, 0xdce077ff }, /* vkCreateImageView */
    [58] = { 1279, 0xb5853953 }, /* vkDestroyImageView */
    [59] = { 1298, 0xa0d3cea2 }, /* vkCreateShaderModule */
    [60] = { 1319, 0x2d77af6e }, /* vkDestroyShaderModule */
    [61] = { 1341, 0xcbf6489f }, /* vkCreatePipelineCache */
    [62] = { 1363, 0x4112a673 }, /* vkDestroyPipelineCache */
    [63] = { 1386, 0x2092a349 }, /* vkGetPipelineCacheData */
    [64] = { 1409, 0xc3499606 }, /* vkMergePipelineCaches */
    [65] = { 1431, 0x4b59f96d }, /* vkCreateGraphicsPipelines */
    [66] = { 1457, 0xf70c85eb }, /* vkCreateComputePipelines */
    [67] = { 1482, 0x6aac68af }, /* vkDestroyPipeline */
    [68] = { 1500, 0x451ef1ed }, /* vkCreatePipelineLayout */
    [69] = { 1523, 0x9146f879 }, /* vkDestroyPipelineLayout */
    [70] = { 1547, 0x13cf03f }, /* vkCreateSampler */
    [71] = { 1563, 0x3b645153 }, /* vkDestroySampler */
    [72] = { 1580, 0x3c14cc74 }, /* vkCreateDescriptorSetLayout */
    [73] = { 1608, 0xa4227b08 }, /* vkDestroyDescriptorSetLayout */
    [74] = { 1637, 0xfb95a8a4 }, /* vkCreateDescriptorPool */
    [75] = { 1660, 0x47bdaf30 }, /* vkDestroyDescriptorPool */
    [76] = { 1684, 0x9bd85f5 }, /* vkResetDescriptorPool */
    [77] = { 1706, 0x4c449d3a }, /* vkAllocateDescriptorSets */
    [78] = { 1731, 0x7a1347b1 }, /* vkFreeDescriptorSets */
    [79] = { 1752, 0xbfd090ae }, /* vkUpdateDescriptorSets */
    [80] = { 1775, 0x887a38c4 }, /* vkCreateFramebuffer */
    [81] = { 1795, 0xdc428e58 }, /* vkDestroyFramebuffer */
    [82] = { 1816, 0x109a9c18 }, /* vkCreateRenderPass */
    [83] = { 1835, 0x16f14324 }, /* vkDestroyRenderPass */
    [84] = { 1855, 0xa9820d22 }, /* vkGetRenderAreaGranularity */
    [85] = { 1882, 0x820fe476 }, /* vkCreateCommandPool */
    [86] = { 1902, 0xd5d83a0a }, /* vkDestroyCommandPool */
    [87] = { 1923, 0x6da9f7fd }, /* vkResetCommandPool */
    [88] = { 1942, 0x8c0c811a }, /* vkAllocateCommandBuffers */
    [89] = { 1967, 0xb9db2b91 }, /* vkFreeCommandBuffers */
    [90] = { 1988, 0xc54f7327 }, /* vkBeginCommandBuffer */
    [91] = { 2009, 0xaffb5725 }, /* vkEndCommandBuffer */
    [92] = { 2028, 0x847dc731 }, /* vkResetCommandBuffer */
    [93] = { 2049, 0x3af9fd84 }, /* vkCmdBindPipeline */
    [94] = { 2067, 0x53d6c2b }, /* vkCmdSetViewport */
    [95] = { 2084, 0x48f28c7f }, /* vkCmdSetScissor */
    [96] = { 2100, 0x32282165 }, /* vkCmdSetLineWidth */
    [97] = { 2118, 0x30f14d07 }, /* vkCmdSetDepthBias */
    [98] = { 2136, 0x1c989dfb }, /* vkCmdSetBlendConstants */
    [99] = { 2159, 0x7b3a8a63 }, /* vkCmdSetDepthBounds */
    [100] = { 2179, 0xa8f534e2 }, /* vkCmdSetStencilCompareMask */
    [101] = { 2206, 0xe7c4b134 }, /* vkCmdSetStencilWriteMask */
    [102] = { 2231, 0x83e2b024 }, /* vkCmdSetStencilReference */
    [103] = { 2256, 0x28c7a5da }, /* vkCmdBindDescriptorSets */
    [104] = { 2280, 0x4c22d870 }, /* vkCmdBindIndexBuffer */
    [105] = { 2301, 0xa9c83f1d }, /* vkCmdBindVertexBuffers */
    [106] = { 2324, 0x9912c1a1 }, /* vkCmdDraw */
    [107] = { 2334, 0xbe5a8058 }, /* vkCmdDrawIndexed */
    [108] = { 2351, 0xe9ac41bf }, /* vkCmdDrawIndirect */
    [109] = { 2369, 0x94e7ed36 }, /* vkCmdDrawIndexedIndirect */
    [110] = { 2394, 0xbd58e867 }, /* vkCmdDispatch */
    [111] = { 2408, 0xd6353005 }, /* vkCmdDispatchIndirect */
    [112] = { 2430, 0xc939a0da }, /* vkCmdCopyBuffer */
    [113] = { 2446, 0x278effa9 }, /* vkCmdCopyImage */
    [114] = { 2461, 0x331ebf89 }, /* vkCmdBlitImage */
    [115] = { 2476, 0x929847e }, /* vkCmdCopyBufferToImage */
    [116] = { 2499, 0x68cddbac }, /* vkCmdCopyImageToBuffer */
    [117] = { 2522, 0xd2986b5e }, /* vkCmdUpdateBuffer */
    [118] = { 2540, 0x5bdd2ae0 }, /* vkCmdFillBuffer */
    [119] = { 2556, 0xb4bc8d08 }, /* vkCmdClearColorImage */
    [120] = { 2577, 0x4f88e4ba }, /* vkCmdClearDepthStencilImage */
    [121] = { 2605, 0x93cb5cb8 }, /* vkCmdClearAttachments */
    [122] = { 2627, 0x671bb594 }, /* vkCmdResolveImage */
    [123] = { 2645, 0xe257f075 }, /* vkCmdSetEvent */
    [124] = { 2659, 0x4fccce28 }, /* vkCmdResetEvent */
    [125] = { 2675, 0x3b9346b3 }, /* vkCmdWaitEvents */
    [126] = { 2691, 0x97fccfe8 }, /* vkCmdPipelineBarrier */
    [127] = { 2712, 0xf5064ea4 }, /* vkCmdBeginQuery */
    [128] = { 2728, 0xd556fd22 }, /* vkCmdEndQuery */
    [129] = { 2742, 0x2f614082 }, /* vkCmdResetQueryPool */
    [130] = { 2762, 0xec4d324c }, /* vkCmdWriteTimestamp */
    [131] = { 2782, 0xdee8c6d4 }, /* vkCmdCopyQueryPoolResults */
    [132] = { 2808, 0xb1c6b468 }, /* vkCmdPushConstants */
    [133] = { 2827, 0xcb7a58e3 }, /* vkCmdBeginRenderPass */
    [134] = { 2848, 0x2eeec2f9 }, /* vkCmdNextSubpass */
    [135] = { 2865, 0xdcdb0235 }, /* vkCmdEndRenderPass */
    [136] = { 2884, 0x9eaabe40 }, /* vkCmdExecuteCommands */
    [137] = { 2905, 0xf204ce7d }, /* vkDestroySurfaceKHR */
    [138] = { 2925, 0x1a687885 }, /* vkGetPhysicalDeviceSurfaceSupportKHR */
    [139] = { 2962, 0x77890558 }, /* vkGetPhysicalDeviceSurfaceCapabilitiesKHR */
    [140] = { 3004, 0xe32227c8 }, /* vkGetPhysicalDeviceSurfaceFormatsKHR */
    [141] = { 3041, 0x31c3cbd1 }, /* vkGetPhysicalDeviceSurfacePresentModesKHR */
    [142] = { 3083, 0xcdefcaa8 }, /* vkCreateSwapchainKHR */
    [143] = { 3104, 0x5a93ab74 }, /* vkDestroySwapchainKHR */
    [144] = { 3126, 0x57695f28 }, /* vkGetSwapchainImagesKHR */
    [145] = { 3150, 0xc3fedb2e }, /* vkAcquireNextImageKHR */
    [146] = { 3172, 0xfc5fb6ce }, /* vkQueuePresentKHR */
    [147] = { 3190, 0x2b2a4b79 }, /* vkCreateWaylandSurfaceKHR */
    [148] = { 3216, 0x84e085ac }, /* vkGetPhysicalDeviceWaylandPresentationSupportKHR */
    [149] = { 3265, 0xa693bc66 }, /* vkCreateXlibSurfaceKHR */
    [150] = { 3288, 0x34a063ab }, /* vkGetPhysicalDeviceXlibPresentationSupportKHR */
    [151] = { 3334, 0xc5e5b106 }, /* vkCreateXcbSurfaceKHR */
    [152] = { 3356, 0x41782cb9 }, /* vkGetPhysicalDeviceXcbPresentationSupportKHR */
    [153] = { 3401, 0x987ef56 }, /* vkCreateDebugReportCallbackEXT */
    [154] = { 3432, 0x43d4c4e2 }, /* vkDestroyDebugReportCallbackEXT */
    [155] = { 3464, 0xa4e75334 }, /* vkDebugReportMessageEXT */
    [156] = { 3488, 0x6a9a3636 }, /* vkGetPhysicalDeviceFeatures2KHR */
    [157] = { 3520, 0xcd15838c }, /* vkGetPhysicalDeviceProperties2KHR */
    [158] = { 3554, 0x9099cbbb }, /* vkGetPhysicalDeviceFormatProperties2KHR */
    [159] = { 3594, 0x102ff7ea }, /* vkGetPhysicalDeviceImageFormatProperties2KHR */
    [160] = { 3639, 0x5ceb2bed }, /* vkGetPhysicalDeviceQueueFamilyProperties2KHR */
    [161] = { 3684, 0xc8c3da3d }, /* vkGetPhysicalDeviceMemoryProperties2KHR */
    [162] = { 3724, 0x8746ed72 }, /* vkGetPhysicalDeviceSparseImageFormatProperties2KHR */
    [163] = { 3775, 0xf17232a1 }, /* vkCmdPushDescriptorSetKHR */
    [164] = { 3801, 0x51177c8d }, /* vkTrimCommandPoolKHR */
    [165] = { 3822, 0xee68b389 }, /* vkGetPhysicalDeviceExternalBufferPropertiesKHR */
    [166] = { 3869, 0x503c14c5 }, /* vkGetMemoryFdKHR */
    [167] = { 3886, 0xb028a792 }, /* vkGetMemoryFdPropertiesKHR */
    [168] = { 3913, 0x984c3fa7 }, /* vkGetPhysicalDeviceExternalSemaphorePropertiesKHR */
    [169] = { 3963, 0x3e0e9884 }, /* vkGetSemaphoreFdKHR */
    [170] = { 3983, 0x36337c05 }, /* vkImportSemaphoreFdKHR */
    [171] = { 4006, 0x99b35492 }, /* vkGetPhysicalDeviceExternalFencePropertiesKHR */
    [172] = { 4052, 0x69a5d6af }, /* vkGetFenceFdKHR */
    [173] = { 4068, 0x51df0390 }, /* vkImportFenceFdKHR */
    [174] = { 4087, 0x6878d3ce }, /* vkBindBufferMemory2KHR */
    [175] = { 4110, 0xf18729ad }, /* vkBindImageMemory2KHR */
    [176] = { 4132, 0x5189488a }, /* vkCreateDescriptorUpdateTemplateKHR */
    [177] = { 4168, 0xaa83901e }, /* vkDestroyDescriptorUpdateTemplateKHR */
    [178] = { 4205, 0x214ad230 }, /* vkUpdateDescriptorSetWithTemplateKHR */
    [179] = { 4242, 0x3d528981 }, /* vkCmdPushDescriptorSetWithTemplateKHR */
    [180] = { 4280, 0x9497e378 }, /* vkGetPhysicalDeviceSurfaceCapabilities2KHR */
    [181] = { 4323, 0xd00b7188 }, /* vkGetPhysicalDeviceSurfaceFormats2KHR */
    [182] = { 4361, 0x78dbe98d }, /* vkGetBufferMemoryRequirements2KHR */
    [183] = { 4395, 0x8de28366 }, /* vkGetImageMemoryRequirements2KHR */
    [184] = { 4428, 0x3df40f5e }, /* vkGetImageSparseMemoryRequirements2KHR */
    [185] = { 4467, 0x7482104f }, /* vkCreateSamplerYcbcrConversionKHR */
    [186] = { 4501, 0xaaa623a3 }, /* vkDestroySamplerYcbcrConversionKHR */
    [187] = { 4536, 0x4979c9a3 }, /* vkGetSwapchainGrallocUsageANDROID */
    [188] = { 4570, 0x6bf780dd }, /* vkAcquireImageANDROID */
    [189] = { 4592, 0xa0313eef }, /* vkQueueSignalReleaseImageANDROID */
    [190] = { 4625, 0x6392dfa7 }, /* vkCreateDmaBufImageINTEL */
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
    VkResult anv_CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback) __attribute__ ((weak));
    void anv_DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void anv_DebugReportMessageEXT(VkInstance instance, VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage) __attribute__ ((weak));
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
    void anv_GetPhysicalDeviceExternalSemaphorePropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfoKHR* pExternalSemaphoreInfo, VkExternalSemaphorePropertiesKHR* pExternalSemaphoreProperties) __attribute__ ((weak));
    VkResult anv_GetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult anv_ImportSemaphoreFdKHR(VkDevice device, const VkImportSemaphoreFdInfoKHR* pImportSemaphoreFdInfo) __attribute__ ((weak));
    void anv_GetPhysicalDeviceExternalFencePropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfoKHR* pExternalFenceInfo, VkExternalFencePropertiesKHR* pExternalFenceProperties) __attribute__ ((weak));
    VkResult anv_GetFenceFdKHR(VkDevice device, const VkFenceGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult anv_ImportFenceFdKHR(VkDevice device, const VkImportFenceFdInfoKHR* pImportFenceFdInfo) __attribute__ ((weak));
    VkResult anv_BindBufferMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfoKHR* pBindInfos) __attribute__ ((weak));
    VkResult anv_BindImageMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfoKHR* pBindInfos) __attribute__ ((weak));
    VkResult anv_CreateDescriptorUpdateTemplateKHR(VkDevice device, const VkDescriptorUpdateTemplateCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplateKHR* pDescriptorUpdateTemplate) __attribute__ ((weak));
    void anv_DestroyDescriptorUpdateTemplateKHR(VkDevice device, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void anv_UpdateDescriptorSetWithTemplateKHR(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const void* pData) __attribute__ ((weak));
    void anv_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) __attribute__ ((weak));
    VkResult anv_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult anv_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats) __attribute__ ((weak));
    void anv_GetBufferMemoryRequirements2KHR(VkDevice device, const VkBufferMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void anv_GetImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void anv_GetImageSparseMemoryRequirements2KHR(VkDevice device, const VkImageSparseMemoryRequirementsInfo2KHR* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2KHR* pSparseMemoryRequirements) __attribute__ ((weak));
    VkResult anv_CreateSamplerYcbcrConversionKHR(VkDevice device, const VkSamplerYcbcrConversionCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversionKHR* pYcbcrConversion) __attribute__ ((weak));
    void anv_DestroySamplerYcbcrConversionKHR(VkDevice device, VkSamplerYcbcrConversionKHR ycbcrConversion, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
#ifdef ANDROID
    VkResult anv_GetSwapchainGrallocUsageANDROID(VkDevice device, VkFormat format, VkImageUsageFlags imageUsage, int* grallocUsage) __attribute__ ((weak));
#endif // ANDROID
#ifdef ANDROID
    VkResult anv_AcquireImageANDROID(VkDevice device, VkImage image, int nativeFenceFd, VkSemaphore semaphore, VkFence fence) __attribute__ ((weak));
#endif // ANDROID
#ifdef ANDROID
    VkResult anv_QueueSignalReleaseImageANDROID(VkQueue queue, uint32_t waitSemaphoreCount, const VkSemaphore* pWaitSemaphores, VkImage image, int* pNativeFenceFd) __attribute__ ((weak));
#endif // ANDROID
    VkResult anv_CreateDmaBufImageINTEL(VkDevice device, const VkDmaBufImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMem, VkImage* pImage) __attribute__ ((weak));

  const struct anv_dispatch_table anv_dispatch_table = {
    .vkCreateInstance = anv_CreateInstance,
    .vkDestroyInstance = anv_DestroyInstance,
    .vkEnumeratePhysicalDevices = anv_EnumeratePhysicalDevices,
    .vkGetDeviceProcAddr = anv_GetDeviceProcAddr,
    .vkGetInstanceProcAddr = anv_GetInstanceProcAddr,
    .vkGetPhysicalDeviceProperties = anv_GetPhysicalDeviceProperties,
    .vkGetPhysicalDeviceQueueFamilyProperties = anv_GetPhysicalDeviceQueueFamilyProperties,
    .vkGetPhysicalDeviceMemoryProperties = anv_GetPhysicalDeviceMemoryProperties,
    .vkGetPhysicalDeviceFeatures = anv_GetPhysicalDeviceFeatures,
    .vkGetPhysicalDeviceFormatProperties = anv_GetPhysicalDeviceFormatProperties,
    .vkGetPhysicalDeviceImageFormatProperties = anv_GetPhysicalDeviceImageFormatProperties,
    .vkCreateDevice = anv_CreateDevice,
    .vkDestroyDevice = anv_DestroyDevice,
    .vkEnumerateInstanceLayerProperties = anv_EnumerateInstanceLayerProperties,
    .vkEnumerateInstanceExtensionProperties = anv_EnumerateInstanceExtensionProperties,
    .vkEnumerateDeviceLayerProperties = anv_EnumerateDeviceLayerProperties,
    .vkEnumerateDeviceExtensionProperties = anv_EnumerateDeviceExtensionProperties,
    .vkGetDeviceQueue = anv_GetDeviceQueue,
    .vkQueueSubmit = anv_QueueSubmit,
    .vkQueueWaitIdle = anv_QueueWaitIdle,
    .vkDeviceWaitIdle = anv_DeviceWaitIdle,
    .vkAllocateMemory = anv_AllocateMemory,
    .vkFreeMemory = anv_FreeMemory,
    .vkMapMemory = anv_MapMemory,
    .vkUnmapMemory = anv_UnmapMemory,
    .vkFlushMappedMemoryRanges = anv_FlushMappedMemoryRanges,
    .vkInvalidateMappedMemoryRanges = anv_InvalidateMappedMemoryRanges,
    .vkGetDeviceMemoryCommitment = anv_GetDeviceMemoryCommitment,
    .vkGetBufferMemoryRequirements = anv_GetBufferMemoryRequirements,
    .vkBindBufferMemory = anv_BindBufferMemory,
    .vkGetImageMemoryRequirements = anv_GetImageMemoryRequirements,
    .vkBindImageMemory = anv_BindImageMemory,
    .vkGetImageSparseMemoryRequirements = anv_GetImageSparseMemoryRequirements,
    .vkGetPhysicalDeviceSparseImageFormatProperties = anv_GetPhysicalDeviceSparseImageFormatProperties,
    .vkQueueBindSparse = anv_QueueBindSparse,
    .vkCreateFence = anv_CreateFence,
    .vkDestroyFence = anv_DestroyFence,
    .vkResetFences = anv_ResetFences,
    .vkGetFenceStatus = anv_GetFenceStatus,
    .vkWaitForFences = anv_WaitForFences,
    .vkCreateSemaphore = anv_CreateSemaphore,
    .vkDestroySemaphore = anv_DestroySemaphore,
    .vkCreateEvent = anv_CreateEvent,
    .vkDestroyEvent = anv_DestroyEvent,
    .vkGetEventStatus = anv_GetEventStatus,
    .vkSetEvent = anv_SetEvent,
    .vkResetEvent = anv_ResetEvent,
    .vkCreateQueryPool = anv_CreateQueryPool,
    .vkDestroyQueryPool = anv_DestroyQueryPool,
    .vkGetQueryPoolResults = anv_GetQueryPoolResults,
    .vkCreateBuffer = anv_CreateBuffer,
    .vkDestroyBuffer = anv_DestroyBuffer,
    .vkCreateBufferView = anv_CreateBufferView,
    .vkDestroyBufferView = anv_DestroyBufferView,
    .vkCreateImage = anv_CreateImage,
    .vkDestroyImage = anv_DestroyImage,
    .vkGetImageSubresourceLayout = anv_GetImageSubresourceLayout,
    .vkCreateImageView = anv_CreateImageView,
    .vkDestroyImageView = anv_DestroyImageView,
    .vkCreateShaderModule = anv_CreateShaderModule,
    .vkDestroyShaderModule = anv_DestroyShaderModule,
    .vkCreatePipelineCache = anv_CreatePipelineCache,
    .vkDestroyPipelineCache = anv_DestroyPipelineCache,
    .vkGetPipelineCacheData = anv_GetPipelineCacheData,
    .vkMergePipelineCaches = anv_MergePipelineCaches,
    .vkCreateGraphicsPipelines = anv_CreateGraphicsPipelines,
    .vkCreateComputePipelines = anv_CreateComputePipelines,
    .vkDestroyPipeline = anv_DestroyPipeline,
    .vkCreatePipelineLayout = anv_CreatePipelineLayout,
    .vkDestroyPipelineLayout = anv_DestroyPipelineLayout,
    .vkCreateSampler = anv_CreateSampler,
    .vkDestroySampler = anv_DestroySampler,
    .vkCreateDescriptorSetLayout = anv_CreateDescriptorSetLayout,
    .vkDestroyDescriptorSetLayout = anv_DestroyDescriptorSetLayout,
    .vkCreateDescriptorPool = anv_CreateDescriptorPool,
    .vkDestroyDescriptorPool = anv_DestroyDescriptorPool,
    .vkResetDescriptorPool = anv_ResetDescriptorPool,
    .vkAllocateDescriptorSets = anv_AllocateDescriptorSets,
    .vkFreeDescriptorSets = anv_FreeDescriptorSets,
    .vkUpdateDescriptorSets = anv_UpdateDescriptorSets,
    .vkCreateFramebuffer = anv_CreateFramebuffer,
    .vkDestroyFramebuffer = anv_DestroyFramebuffer,
    .vkCreateRenderPass = anv_CreateRenderPass,
    .vkDestroyRenderPass = anv_DestroyRenderPass,
    .vkGetRenderAreaGranularity = anv_GetRenderAreaGranularity,
    .vkCreateCommandPool = anv_CreateCommandPool,
    .vkDestroyCommandPool = anv_DestroyCommandPool,
    .vkResetCommandPool = anv_ResetCommandPool,
    .vkAllocateCommandBuffers = anv_AllocateCommandBuffers,
    .vkFreeCommandBuffers = anv_FreeCommandBuffers,
    .vkBeginCommandBuffer = anv_BeginCommandBuffer,
    .vkEndCommandBuffer = anv_EndCommandBuffer,
    .vkResetCommandBuffer = anv_ResetCommandBuffer,
    .vkCmdBindPipeline = anv_CmdBindPipeline,
    .vkCmdSetViewport = anv_CmdSetViewport,
    .vkCmdSetScissor = anv_CmdSetScissor,
    .vkCmdSetLineWidth = anv_CmdSetLineWidth,
    .vkCmdSetDepthBias = anv_CmdSetDepthBias,
    .vkCmdSetBlendConstants = anv_CmdSetBlendConstants,
    .vkCmdSetDepthBounds = anv_CmdSetDepthBounds,
    .vkCmdSetStencilCompareMask = anv_CmdSetStencilCompareMask,
    .vkCmdSetStencilWriteMask = anv_CmdSetStencilWriteMask,
    .vkCmdSetStencilReference = anv_CmdSetStencilReference,
    .vkCmdBindDescriptorSets = anv_CmdBindDescriptorSets,
    .vkCmdBindIndexBuffer = anv_CmdBindIndexBuffer,
    .vkCmdBindVertexBuffers = anv_CmdBindVertexBuffers,
    .vkCmdDraw = anv_CmdDraw,
    .vkCmdDrawIndexed = anv_CmdDrawIndexed,
    .vkCmdDrawIndirect = anv_CmdDrawIndirect,
    .vkCmdDrawIndexedIndirect = anv_CmdDrawIndexedIndirect,
    .vkCmdDispatch = anv_CmdDispatch,
    .vkCmdDispatchIndirect = anv_CmdDispatchIndirect,
    .vkCmdCopyBuffer = anv_CmdCopyBuffer,
    .vkCmdCopyImage = anv_CmdCopyImage,
    .vkCmdBlitImage = anv_CmdBlitImage,
    .vkCmdCopyBufferToImage = anv_CmdCopyBufferToImage,
    .vkCmdCopyImageToBuffer = anv_CmdCopyImageToBuffer,
    .vkCmdUpdateBuffer = anv_CmdUpdateBuffer,
    .vkCmdFillBuffer = anv_CmdFillBuffer,
    .vkCmdClearColorImage = anv_CmdClearColorImage,
    .vkCmdClearDepthStencilImage = anv_CmdClearDepthStencilImage,
    .vkCmdClearAttachments = anv_CmdClearAttachments,
    .vkCmdResolveImage = anv_CmdResolveImage,
    .vkCmdSetEvent = anv_CmdSetEvent,
    .vkCmdResetEvent = anv_CmdResetEvent,
    .vkCmdWaitEvents = anv_CmdWaitEvents,
    .vkCmdPipelineBarrier = anv_CmdPipelineBarrier,
    .vkCmdBeginQuery = anv_CmdBeginQuery,
    .vkCmdEndQuery = anv_CmdEndQuery,
    .vkCmdResetQueryPool = anv_CmdResetQueryPool,
    .vkCmdWriteTimestamp = anv_CmdWriteTimestamp,
    .vkCmdCopyQueryPoolResults = anv_CmdCopyQueryPoolResults,
    .vkCmdPushConstants = anv_CmdPushConstants,
    .vkCmdBeginRenderPass = anv_CmdBeginRenderPass,
    .vkCmdNextSubpass = anv_CmdNextSubpass,
    .vkCmdEndRenderPass = anv_CmdEndRenderPass,
    .vkCmdExecuteCommands = anv_CmdExecuteCommands,
    .vkDestroySurfaceKHR = anv_DestroySurfaceKHR,
    .vkGetPhysicalDeviceSurfaceSupportKHR = anv_GetPhysicalDeviceSurfaceSupportKHR,
    .vkGetPhysicalDeviceSurfaceCapabilitiesKHR = anv_GetPhysicalDeviceSurfaceCapabilitiesKHR,
    .vkGetPhysicalDeviceSurfaceFormatsKHR = anv_GetPhysicalDeviceSurfaceFormatsKHR,
    .vkGetPhysicalDeviceSurfacePresentModesKHR = anv_GetPhysicalDeviceSurfacePresentModesKHR,
    .vkCreateSwapchainKHR = anv_CreateSwapchainKHR,
    .vkDestroySwapchainKHR = anv_DestroySwapchainKHR,
    .vkGetSwapchainImagesKHR = anv_GetSwapchainImagesKHR,
    .vkAcquireNextImageKHR = anv_AcquireNextImageKHR,
    .vkQueuePresentKHR = anv_QueuePresentKHR,
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .vkCreateWaylandSurfaceKHR = anv_CreateWaylandSurfaceKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .vkGetPhysicalDeviceWaylandPresentationSupportKHR = anv_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .vkCreateXlibSurfaceKHR = anv_CreateXlibSurfaceKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .vkGetPhysicalDeviceXlibPresentationSupportKHR = anv_GetPhysicalDeviceXlibPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .vkCreateXcbSurfaceKHR = anv_CreateXcbSurfaceKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .vkGetPhysicalDeviceXcbPresentationSupportKHR = anv_GetPhysicalDeviceXcbPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
    .vkCreateDebugReportCallbackEXT = anv_CreateDebugReportCallbackEXT,
    .vkDestroyDebugReportCallbackEXT = anv_DestroyDebugReportCallbackEXT,
    .vkDebugReportMessageEXT = anv_DebugReportMessageEXT,
    .vkGetPhysicalDeviceFeatures2KHR = anv_GetPhysicalDeviceFeatures2KHR,
    .vkGetPhysicalDeviceProperties2KHR = anv_GetPhysicalDeviceProperties2KHR,
    .vkGetPhysicalDeviceFormatProperties2KHR = anv_GetPhysicalDeviceFormatProperties2KHR,
    .vkGetPhysicalDeviceImageFormatProperties2KHR = anv_GetPhysicalDeviceImageFormatProperties2KHR,
    .vkGetPhysicalDeviceQueueFamilyProperties2KHR = anv_GetPhysicalDeviceQueueFamilyProperties2KHR,
    .vkGetPhysicalDeviceMemoryProperties2KHR = anv_GetPhysicalDeviceMemoryProperties2KHR,
    .vkGetPhysicalDeviceSparseImageFormatProperties2KHR = anv_GetPhysicalDeviceSparseImageFormatProperties2KHR,
    .vkCmdPushDescriptorSetKHR = anv_CmdPushDescriptorSetKHR,
    .vkTrimCommandPoolKHR = anv_TrimCommandPoolKHR,
    .vkGetPhysicalDeviceExternalBufferPropertiesKHR = anv_GetPhysicalDeviceExternalBufferPropertiesKHR,
    .vkGetMemoryFdKHR = anv_GetMemoryFdKHR,
    .vkGetMemoryFdPropertiesKHR = anv_GetMemoryFdPropertiesKHR,
    .vkGetPhysicalDeviceExternalSemaphorePropertiesKHR = anv_GetPhysicalDeviceExternalSemaphorePropertiesKHR,
    .vkGetSemaphoreFdKHR = anv_GetSemaphoreFdKHR,
    .vkImportSemaphoreFdKHR = anv_ImportSemaphoreFdKHR,
    .vkGetPhysicalDeviceExternalFencePropertiesKHR = anv_GetPhysicalDeviceExternalFencePropertiesKHR,
    .vkGetFenceFdKHR = anv_GetFenceFdKHR,
    .vkImportFenceFdKHR = anv_ImportFenceFdKHR,
    .vkBindBufferMemory2KHR = anv_BindBufferMemory2KHR,
    .vkBindImageMemory2KHR = anv_BindImageMemory2KHR,
    .vkCreateDescriptorUpdateTemplateKHR = anv_CreateDescriptorUpdateTemplateKHR,
    .vkDestroyDescriptorUpdateTemplateKHR = anv_DestroyDescriptorUpdateTemplateKHR,
    .vkUpdateDescriptorSetWithTemplateKHR = anv_UpdateDescriptorSetWithTemplateKHR,
    .vkCmdPushDescriptorSetWithTemplateKHR = anv_CmdPushDescriptorSetWithTemplateKHR,
    .vkGetPhysicalDeviceSurfaceCapabilities2KHR = anv_GetPhysicalDeviceSurfaceCapabilities2KHR,
    .vkGetPhysicalDeviceSurfaceFormats2KHR = anv_GetPhysicalDeviceSurfaceFormats2KHR,
    .vkGetBufferMemoryRequirements2KHR = anv_GetBufferMemoryRequirements2KHR,
    .vkGetImageMemoryRequirements2KHR = anv_GetImageMemoryRequirements2KHR,
    .vkGetImageSparseMemoryRequirements2KHR = anv_GetImageSparseMemoryRequirements2KHR,
    .vkCreateSamplerYcbcrConversionKHR = anv_CreateSamplerYcbcrConversionKHR,
    .vkDestroySamplerYcbcrConversionKHR = anv_DestroySamplerYcbcrConversionKHR,
#ifdef ANDROID
    .vkGetSwapchainGrallocUsageANDROID = anv_GetSwapchainGrallocUsageANDROID,
#endif // ANDROID
#ifdef ANDROID
    .vkAcquireImageANDROID = anv_AcquireImageANDROID,
#endif // ANDROID
#ifdef ANDROID
    .vkQueueSignalReleaseImageANDROID = anv_QueueSignalReleaseImageANDROID,
#endif // ANDROID
    .vkCreateDmaBufImageINTEL = anv_CreateDmaBufImageINTEL,
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
    VkResult gen7_CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback) __attribute__ ((weak));
    void gen7_DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen7_DebugReportMessageEXT(VkInstance instance, VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage) __attribute__ ((weak));
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
    void gen7_GetPhysicalDeviceExternalSemaphorePropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfoKHR* pExternalSemaphoreInfo, VkExternalSemaphorePropertiesKHR* pExternalSemaphoreProperties) __attribute__ ((weak));
    VkResult gen7_GetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen7_ImportSemaphoreFdKHR(VkDevice device, const VkImportSemaphoreFdInfoKHR* pImportSemaphoreFdInfo) __attribute__ ((weak));
    void gen7_GetPhysicalDeviceExternalFencePropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfoKHR* pExternalFenceInfo, VkExternalFencePropertiesKHR* pExternalFenceProperties) __attribute__ ((weak));
    VkResult gen7_GetFenceFdKHR(VkDevice device, const VkFenceGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen7_ImportFenceFdKHR(VkDevice device, const VkImportFenceFdInfoKHR* pImportFenceFdInfo) __attribute__ ((weak));
    VkResult gen7_BindBufferMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfoKHR* pBindInfos) __attribute__ ((weak));
    VkResult gen7_BindImageMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfoKHR* pBindInfos) __attribute__ ((weak));
    VkResult gen7_CreateDescriptorUpdateTemplateKHR(VkDevice device, const VkDescriptorUpdateTemplateCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplateKHR* pDescriptorUpdateTemplate) __attribute__ ((weak));
    void gen7_DestroyDescriptorUpdateTemplateKHR(VkDevice device, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen7_UpdateDescriptorSetWithTemplateKHR(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const void* pData) __attribute__ ((weak));
    void gen7_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) __attribute__ ((weak));
    VkResult gen7_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen7_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats) __attribute__ ((weak));
    void gen7_GetBufferMemoryRequirements2KHR(VkDevice device, const VkBufferMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void gen7_GetImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void gen7_GetImageSparseMemoryRequirements2KHR(VkDevice device, const VkImageSparseMemoryRequirementsInfo2KHR* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2KHR* pSparseMemoryRequirements) __attribute__ ((weak));
    VkResult gen7_CreateSamplerYcbcrConversionKHR(VkDevice device, const VkSamplerYcbcrConversionCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversionKHR* pYcbcrConversion) __attribute__ ((weak));
    void gen7_DestroySamplerYcbcrConversionKHR(VkDevice device, VkSamplerYcbcrConversionKHR ycbcrConversion, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
#ifdef ANDROID
    VkResult gen7_GetSwapchainGrallocUsageANDROID(VkDevice device, VkFormat format, VkImageUsageFlags imageUsage, int* grallocUsage) __attribute__ ((weak));
#endif // ANDROID
#ifdef ANDROID
    VkResult gen7_AcquireImageANDROID(VkDevice device, VkImage image, int nativeFenceFd, VkSemaphore semaphore, VkFence fence) __attribute__ ((weak));
#endif // ANDROID
#ifdef ANDROID
    VkResult gen7_QueueSignalReleaseImageANDROID(VkQueue queue, uint32_t waitSemaphoreCount, const VkSemaphore* pWaitSemaphores, VkImage image, int* pNativeFenceFd) __attribute__ ((weak));
#endif // ANDROID
    VkResult gen7_CreateDmaBufImageINTEL(VkDevice device, const VkDmaBufImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMem, VkImage* pImage) __attribute__ ((weak));

  const struct anv_dispatch_table gen7_dispatch_table = {
    .vkCreateInstance = gen7_CreateInstance,
    .vkDestroyInstance = gen7_DestroyInstance,
    .vkEnumeratePhysicalDevices = gen7_EnumeratePhysicalDevices,
    .vkGetDeviceProcAddr = gen7_GetDeviceProcAddr,
    .vkGetInstanceProcAddr = gen7_GetInstanceProcAddr,
    .vkGetPhysicalDeviceProperties = gen7_GetPhysicalDeviceProperties,
    .vkGetPhysicalDeviceQueueFamilyProperties = gen7_GetPhysicalDeviceQueueFamilyProperties,
    .vkGetPhysicalDeviceMemoryProperties = gen7_GetPhysicalDeviceMemoryProperties,
    .vkGetPhysicalDeviceFeatures = gen7_GetPhysicalDeviceFeatures,
    .vkGetPhysicalDeviceFormatProperties = gen7_GetPhysicalDeviceFormatProperties,
    .vkGetPhysicalDeviceImageFormatProperties = gen7_GetPhysicalDeviceImageFormatProperties,
    .vkCreateDevice = gen7_CreateDevice,
    .vkDestroyDevice = gen7_DestroyDevice,
    .vkEnumerateInstanceLayerProperties = gen7_EnumerateInstanceLayerProperties,
    .vkEnumerateInstanceExtensionProperties = gen7_EnumerateInstanceExtensionProperties,
    .vkEnumerateDeviceLayerProperties = gen7_EnumerateDeviceLayerProperties,
    .vkEnumerateDeviceExtensionProperties = gen7_EnumerateDeviceExtensionProperties,
    .vkGetDeviceQueue = gen7_GetDeviceQueue,
    .vkQueueSubmit = gen7_QueueSubmit,
    .vkQueueWaitIdle = gen7_QueueWaitIdle,
    .vkDeviceWaitIdle = gen7_DeviceWaitIdle,
    .vkAllocateMemory = gen7_AllocateMemory,
    .vkFreeMemory = gen7_FreeMemory,
    .vkMapMemory = gen7_MapMemory,
    .vkUnmapMemory = gen7_UnmapMemory,
    .vkFlushMappedMemoryRanges = gen7_FlushMappedMemoryRanges,
    .vkInvalidateMappedMemoryRanges = gen7_InvalidateMappedMemoryRanges,
    .vkGetDeviceMemoryCommitment = gen7_GetDeviceMemoryCommitment,
    .vkGetBufferMemoryRequirements = gen7_GetBufferMemoryRequirements,
    .vkBindBufferMemory = gen7_BindBufferMemory,
    .vkGetImageMemoryRequirements = gen7_GetImageMemoryRequirements,
    .vkBindImageMemory = gen7_BindImageMemory,
    .vkGetImageSparseMemoryRequirements = gen7_GetImageSparseMemoryRequirements,
    .vkGetPhysicalDeviceSparseImageFormatProperties = gen7_GetPhysicalDeviceSparseImageFormatProperties,
    .vkQueueBindSparse = gen7_QueueBindSparse,
    .vkCreateFence = gen7_CreateFence,
    .vkDestroyFence = gen7_DestroyFence,
    .vkResetFences = gen7_ResetFences,
    .vkGetFenceStatus = gen7_GetFenceStatus,
    .vkWaitForFences = gen7_WaitForFences,
    .vkCreateSemaphore = gen7_CreateSemaphore,
    .vkDestroySemaphore = gen7_DestroySemaphore,
    .vkCreateEvent = gen7_CreateEvent,
    .vkDestroyEvent = gen7_DestroyEvent,
    .vkGetEventStatus = gen7_GetEventStatus,
    .vkSetEvent = gen7_SetEvent,
    .vkResetEvent = gen7_ResetEvent,
    .vkCreateQueryPool = gen7_CreateQueryPool,
    .vkDestroyQueryPool = gen7_DestroyQueryPool,
    .vkGetQueryPoolResults = gen7_GetQueryPoolResults,
    .vkCreateBuffer = gen7_CreateBuffer,
    .vkDestroyBuffer = gen7_DestroyBuffer,
    .vkCreateBufferView = gen7_CreateBufferView,
    .vkDestroyBufferView = gen7_DestroyBufferView,
    .vkCreateImage = gen7_CreateImage,
    .vkDestroyImage = gen7_DestroyImage,
    .vkGetImageSubresourceLayout = gen7_GetImageSubresourceLayout,
    .vkCreateImageView = gen7_CreateImageView,
    .vkDestroyImageView = gen7_DestroyImageView,
    .vkCreateShaderModule = gen7_CreateShaderModule,
    .vkDestroyShaderModule = gen7_DestroyShaderModule,
    .vkCreatePipelineCache = gen7_CreatePipelineCache,
    .vkDestroyPipelineCache = gen7_DestroyPipelineCache,
    .vkGetPipelineCacheData = gen7_GetPipelineCacheData,
    .vkMergePipelineCaches = gen7_MergePipelineCaches,
    .vkCreateGraphicsPipelines = gen7_CreateGraphicsPipelines,
    .vkCreateComputePipelines = gen7_CreateComputePipelines,
    .vkDestroyPipeline = gen7_DestroyPipeline,
    .vkCreatePipelineLayout = gen7_CreatePipelineLayout,
    .vkDestroyPipelineLayout = gen7_DestroyPipelineLayout,
    .vkCreateSampler = gen7_CreateSampler,
    .vkDestroySampler = gen7_DestroySampler,
    .vkCreateDescriptorSetLayout = gen7_CreateDescriptorSetLayout,
    .vkDestroyDescriptorSetLayout = gen7_DestroyDescriptorSetLayout,
    .vkCreateDescriptorPool = gen7_CreateDescriptorPool,
    .vkDestroyDescriptorPool = gen7_DestroyDescriptorPool,
    .vkResetDescriptorPool = gen7_ResetDescriptorPool,
    .vkAllocateDescriptorSets = gen7_AllocateDescriptorSets,
    .vkFreeDescriptorSets = gen7_FreeDescriptorSets,
    .vkUpdateDescriptorSets = gen7_UpdateDescriptorSets,
    .vkCreateFramebuffer = gen7_CreateFramebuffer,
    .vkDestroyFramebuffer = gen7_DestroyFramebuffer,
    .vkCreateRenderPass = gen7_CreateRenderPass,
    .vkDestroyRenderPass = gen7_DestroyRenderPass,
    .vkGetRenderAreaGranularity = gen7_GetRenderAreaGranularity,
    .vkCreateCommandPool = gen7_CreateCommandPool,
    .vkDestroyCommandPool = gen7_DestroyCommandPool,
    .vkResetCommandPool = gen7_ResetCommandPool,
    .vkAllocateCommandBuffers = gen7_AllocateCommandBuffers,
    .vkFreeCommandBuffers = gen7_FreeCommandBuffers,
    .vkBeginCommandBuffer = gen7_BeginCommandBuffer,
    .vkEndCommandBuffer = gen7_EndCommandBuffer,
    .vkResetCommandBuffer = gen7_ResetCommandBuffer,
    .vkCmdBindPipeline = gen7_CmdBindPipeline,
    .vkCmdSetViewport = gen7_CmdSetViewport,
    .vkCmdSetScissor = gen7_CmdSetScissor,
    .vkCmdSetLineWidth = gen7_CmdSetLineWidth,
    .vkCmdSetDepthBias = gen7_CmdSetDepthBias,
    .vkCmdSetBlendConstants = gen7_CmdSetBlendConstants,
    .vkCmdSetDepthBounds = gen7_CmdSetDepthBounds,
    .vkCmdSetStencilCompareMask = gen7_CmdSetStencilCompareMask,
    .vkCmdSetStencilWriteMask = gen7_CmdSetStencilWriteMask,
    .vkCmdSetStencilReference = gen7_CmdSetStencilReference,
    .vkCmdBindDescriptorSets = gen7_CmdBindDescriptorSets,
    .vkCmdBindIndexBuffer = gen7_CmdBindIndexBuffer,
    .vkCmdBindVertexBuffers = gen7_CmdBindVertexBuffers,
    .vkCmdDraw = gen7_CmdDraw,
    .vkCmdDrawIndexed = gen7_CmdDrawIndexed,
    .vkCmdDrawIndirect = gen7_CmdDrawIndirect,
    .vkCmdDrawIndexedIndirect = gen7_CmdDrawIndexedIndirect,
    .vkCmdDispatch = gen7_CmdDispatch,
    .vkCmdDispatchIndirect = gen7_CmdDispatchIndirect,
    .vkCmdCopyBuffer = gen7_CmdCopyBuffer,
    .vkCmdCopyImage = gen7_CmdCopyImage,
    .vkCmdBlitImage = gen7_CmdBlitImage,
    .vkCmdCopyBufferToImage = gen7_CmdCopyBufferToImage,
    .vkCmdCopyImageToBuffer = gen7_CmdCopyImageToBuffer,
    .vkCmdUpdateBuffer = gen7_CmdUpdateBuffer,
    .vkCmdFillBuffer = gen7_CmdFillBuffer,
    .vkCmdClearColorImage = gen7_CmdClearColorImage,
    .vkCmdClearDepthStencilImage = gen7_CmdClearDepthStencilImage,
    .vkCmdClearAttachments = gen7_CmdClearAttachments,
    .vkCmdResolveImage = gen7_CmdResolveImage,
    .vkCmdSetEvent = gen7_CmdSetEvent,
    .vkCmdResetEvent = gen7_CmdResetEvent,
    .vkCmdWaitEvents = gen7_CmdWaitEvents,
    .vkCmdPipelineBarrier = gen7_CmdPipelineBarrier,
    .vkCmdBeginQuery = gen7_CmdBeginQuery,
    .vkCmdEndQuery = gen7_CmdEndQuery,
    .vkCmdResetQueryPool = gen7_CmdResetQueryPool,
    .vkCmdWriteTimestamp = gen7_CmdWriteTimestamp,
    .vkCmdCopyQueryPoolResults = gen7_CmdCopyQueryPoolResults,
    .vkCmdPushConstants = gen7_CmdPushConstants,
    .vkCmdBeginRenderPass = gen7_CmdBeginRenderPass,
    .vkCmdNextSubpass = gen7_CmdNextSubpass,
    .vkCmdEndRenderPass = gen7_CmdEndRenderPass,
    .vkCmdExecuteCommands = gen7_CmdExecuteCommands,
    .vkDestroySurfaceKHR = gen7_DestroySurfaceKHR,
    .vkGetPhysicalDeviceSurfaceSupportKHR = gen7_GetPhysicalDeviceSurfaceSupportKHR,
    .vkGetPhysicalDeviceSurfaceCapabilitiesKHR = gen7_GetPhysicalDeviceSurfaceCapabilitiesKHR,
    .vkGetPhysicalDeviceSurfaceFormatsKHR = gen7_GetPhysicalDeviceSurfaceFormatsKHR,
    .vkGetPhysicalDeviceSurfacePresentModesKHR = gen7_GetPhysicalDeviceSurfacePresentModesKHR,
    .vkCreateSwapchainKHR = gen7_CreateSwapchainKHR,
    .vkDestroySwapchainKHR = gen7_DestroySwapchainKHR,
    .vkGetSwapchainImagesKHR = gen7_GetSwapchainImagesKHR,
    .vkAcquireNextImageKHR = gen7_AcquireNextImageKHR,
    .vkQueuePresentKHR = gen7_QueuePresentKHR,
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .vkCreateWaylandSurfaceKHR = gen7_CreateWaylandSurfaceKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .vkGetPhysicalDeviceWaylandPresentationSupportKHR = gen7_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .vkCreateXlibSurfaceKHR = gen7_CreateXlibSurfaceKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .vkGetPhysicalDeviceXlibPresentationSupportKHR = gen7_GetPhysicalDeviceXlibPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .vkCreateXcbSurfaceKHR = gen7_CreateXcbSurfaceKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .vkGetPhysicalDeviceXcbPresentationSupportKHR = gen7_GetPhysicalDeviceXcbPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
    .vkCreateDebugReportCallbackEXT = gen7_CreateDebugReportCallbackEXT,
    .vkDestroyDebugReportCallbackEXT = gen7_DestroyDebugReportCallbackEXT,
    .vkDebugReportMessageEXT = gen7_DebugReportMessageEXT,
    .vkGetPhysicalDeviceFeatures2KHR = gen7_GetPhysicalDeviceFeatures2KHR,
    .vkGetPhysicalDeviceProperties2KHR = gen7_GetPhysicalDeviceProperties2KHR,
    .vkGetPhysicalDeviceFormatProperties2KHR = gen7_GetPhysicalDeviceFormatProperties2KHR,
    .vkGetPhysicalDeviceImageFormatProperties2KHR = gen7_GetPhysicalDeviceImageFormatProperties2KHR,
    .vkGetPhysicalDeviceQueueFamilyProperties2KHR = gen7_GetPhysicalDeviceQueueFamilyProperties2KHR,
    .vkGetPhysicalDeviceMemoryProperties2KHR = gen7_GetPhysicalDeviceMemoryProperties2KHR,
    .vkGetPhysicalDeviceSparseImageFormatProperties2KHR = gen7_GetPhysicalDeviceSparseImageFormatProperties2KHR,
    .vkCmdPushDescriptorSetKHR = gen7_CmdPushDescriptorSetKHR,
    .vkTrimCommandPoolKHR = gen7_TrimCommandPoolKHR,
    .vkGetPhysicalDeviceExternalBufferPropertiesKHR = gen7_GetPhysicalDeviceExternalBufferPropertiesKHR,
    .vkGetMemoryFdKHR = gen7_GetMemoryFdKHR,
    .vkGetMemoryFdPropertiesKHR = gen7_GetMemoryFdPropertiesKHR,
    .vkGetPhysicalDeviceExternalSemaphorePropertiesKHR = gen7_GetPhysicalDeviceExternalSemaphorePropertiesKHR,
    .vkGetSemaphoreFdKHR = gen7_GetSemaphoreFdKHR,
    .vkImportSemaphoreFdKHR = gen7_ImportSemaphoreFdKHR,
    .vkGetPhysicalDeviceExternalFencePropertiesKHR = gen7_GetPhysicalDeviceExternalFencePropertiesKHR,
    .vkGetFenceFdKHR = gen7_GetFenceFdKHR,
    .vkImportFenceFdKHR = gen7_ImportFenceFdKHR,
    .vkBindBufferMemory2KHR = gen7_BindBufferMemory2KHR,
    .vkBindImageMemory2KHR = gen7_BindImageMemory2KHR,
    .vkCreateDescriptorUpdateTemplateKHR = gen7_CreateDescriptorUpdateTemplateKHR,
    .vkDestroyDescriptorUpdateTemplateKHR = gen7_DestroyDescriptorUpdateTemplateKHR,
    .vkUpdateDescriptorSetWithTemplateKHR = gen7_UpdateDescriptorSetWithTemplateKHR,
    .vkCmdPushDescriptorSetWithTemplateKHR = gen7_CmdPushDescriptorSetWithTemplateKHR,
    .vkGetPhysicalDeviceSurfaceCapabilities2KHR = gen7_GetPhysicalDeviceSurfaceCapabilities2KHR,
    .vkGetPhysicalDeviceSurfaceFormats2KHR = gen7_GetPhysicalDeviceSurfaceFormats2KHR,
    .vkGetBufferMemoryRequirements2KHR = gen7_GetBufferMemoryRequirements2KHR,
    .vkGetImageMemoryRequirements2KHR = gen7_GetImageMemoryRequirements2KHR,
    .vkGetImageSparseMemoryRequirements2KHR = gen7_GetImageSparseMemoryRequirements2KHR,
    .vkCreateSamplerYcbcrConversionKHR = gen7_CreateSamplerYcbcrConversionKHR,
    .vkDestroySamplerYcbcrConversionKHR = gen7_DestroySamplerYcbcrConversionKHR,
#ifdef ANDROID
    .vkGetSwapchainGrallocUsageANDROID = gen7_GetSwapchainGrallocUsageANDROID,
#endif // ANDROID
#ifdef ANDROID
    .vkAcquireImageANDROID = gen7_AcquireImageANDROID,
#endif // ANDROID
#ifdef ANDROID
    .vkQueueSignalReleaseImageANDROID = gen7_QueueSignalReleaseImageANDROID,
#endif // ANDROID
    .vkCreateDmaBufImageINTEL = gen7_CreateDmaBufImageINTEL,
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
    VkResult gen75_CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback) __attribute__ ((weak));
    void gen75_DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen75_DebugReportMessageEXT(VkInstance instance, VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage) __attribute__ ((weak));
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
    void gen75_GetPhysicalDeviceExternalSemaphorePropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfoKHR* pExternalSemaphoreInfo, VkExternalSemaphorePropertiesKHR* pExternalSemaphoreProperties) __attribute__ ((weak));
    VkResult gen75_GetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen75_ImportSemaphoreFdKHR(VkDevice device, const VkImportSemaphoreFdInfoKHR* pImportSemaphoreFdInfo) __attribute__ ((weak));
    void gen75_GetPhysicalDeviceExternalFencePropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfoKHR* pExternalFenceInfo, VkExternalFencePropertiesKHR* pExternalFenceProperties) __attribute__ ((weak));
    VkResult gen75_GetFenceFdKHR(VkDevice device, const VkFenceGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen75_ImportFenceFdKHR(VkDevice device, const VkImportFenceFdInfoKHR* pImportFenceFdInfo) __attribute__ ((weak));
    VkResult gen75_BindBufferMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfoKHR* pBindInfos) __attribute__ ((weak));
    VkResult gen75_BindImageMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfoKHR* pBindInfos) __attribute__ ((weak));
    VkResult gen75_CreateDescriptorUpdateTemplateKHR(VkDevice device, const VkDescriptorUpdateTemplateCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplateKHR* pDescriptorUpdateTemplate) __attribute__ ((weak));
    void gen75_DestroyDescriptorUpdateTemplateKHR(VkDevice device, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen75_UpdateDescriptorSetWithTemplateKHR(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const void* pData) __attribute__ ((weak));
    void gen75_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) __attribute__ ((weak));
    VkResult gen75_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen75_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats) __attribute__ ((weak));
    void gen75_GetBufferMemoryRequirements2KHR(VkDevice device, const VkBufferMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void gen75_GetImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void gen75_GetImageSparseMemoryRequirements2KHR(VkDevice device, const VkImageSparseMemoryRequirementsInfo2KHR* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2KHR* pSparseMemoryRequirements) __attribute__ ((weak));
    VkResult gen75_CreateSamplerYcbcrConversionKHR(VkDevice device, const VkSamplerYcbcrConversionCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversionKHR* pYcbcrConversion) __attribute__ ((weak));
    void gen75_DestroySamplerYcbcrConversionKHR(VkDevice device, VkSamplerYcbcrConversionKHR ycbcrConversion, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
#ifdef ANDROID
    VkResult gen75_GetSwapchainGrallocUsageANDROID(VkDevice device, VkFormat format, VkImageUsageFlags imageUsage, int* grallocUsage) __attribute__ ((weak));
#endif // ANDROID
#ifdef ANDROID
    VkResult gen75_AcquireImageANDROID(VkDevice device, VkImage image, int nativeFenceFd, VkSemaphore semaphore, VkFence fence) __attribute__ ((weak));
#endif // ANDROID
#ifdef ANDROID
    VkResult gen75_QueueSignalReleaseImageANDROID(VkQueue queue, uint32_t waitSemaphoreCount, const VkSemaphore* pWaitSemaphores, VkImage image, int* pNativeFenceFd) __attribute__ ((weak));
#endif // ANDROID
    VkResult gen75_CreateDmaBufImageINTEL(VkDevice device, const VkDmaBufImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMem, VkImage* pImage) __attribute__ ((weak));

  const struct anv_dispatch_table gen75_dispatch_table = {
    .vkCreateInstance = gen75_CreateInstance,
    .vkDestroyInstance = gen75_DestroyInstance,
    .vkEnumeratePhysicalDevices = gen75_EnumeratePhysicalDevices,
    .vkGetDeviceProcAddr = gen75_GetDeviceProcAddr,
    .vkGetInstanceProcAddr = gen75_GetInstanceProcAddr,
    .vkGetPhysicalDeviceProperties = gen75_GetPhysicalDeviceProperties,
    .vkGetPhysicalDeviceQueueFamilyProperties = gen75_GetPhysicalDeviceQueueFamilyProperties,
    .vkGetPhysicalDeviceMemoryProperties = gen75_GetPhysicalDeviceMemoryProperties,
    .vkGetPhysicalDeviceFeatures = gen75_GetPhysicalDeviceFeatures,
    .vkGetPhysicalDeviceFormatProperties = gen75_GetPhysicalDeviceFormatProperties,
    .vkGetPhysicalDeviceImageFormatProperties = gen75_GetPhysicalDeviceImageFormatProperties,
    .vkCreateDevice = gen75_CreateDevice,
    .vkDestroyDevice = gen75_DestroyDevice,
    .vkEnumerateInstanceLayerProperties = gen75_EnumerateInstanceLayerProperties,
    .vkEnumerateInstanceExtensionProperties = gen75_EnumerateInstanceExtensionProperties,
    .vkEnumerateDeviceLayerProperties = gen75_EnumerateDeviceLayerProperties,
    .vkEnumerateDeviceExtensionProperties = gen75_EnumerateDeviceExtensionProperties,
    .vkGetDeviceQueue = gen75_GetDeviceQueue,
    .vkQueueSubmit = gen75_QueueSubmit,
    .vkQueueWaitIdle = gen75_QueueWaitIdle,
    .vkDeviceWaitIdle = gen75_DeviceWaitIdle,
    .vkAllocateMemory = gen75_AllocateMemory,
    .vkFreeMemory = gen75_FreeMemory,
    .vkMapMemory = gen75_MapMemory,
    .vkUnmapMemory = gen75_UnmapMemory,
    .vkFlushMappedMemoryRanges = gen75_FlushMappedMemoryRanges,
    .vkInvalidateMappedMemoryRanges = gen75_InvalidateMappedMemoryRanges,
    .vkGetDeviceMemoryCommitment = gen75_GetDeviceMemoryCommitment,
    .vkGetBufferMemoryRequirements = gen75_GetBufferMemoryRequirements,
    .vkBindBufferMemory = gen75_BindBufferMemory,
    .vkGetImageMemoryRequirements = gen75_GetImageMemoryRequirements,
    .vkBindImageMemory = gen75_BindImageMemory,
    .vkGetImageSparseMemoryRequirements = gen75_GetImageSparseMemoryRequirements,
    .vkGetPhysicalDeviceSparseImageFormatProperties = gen75_GetPhysicalDeviceSparseImageFormatProperties,
    .vkQueueBindSparse = gen75_QueueBindSparse,
    .vkCreateFence = gen75_CreateFence,
    .vkDestroyFence = gen75_DestroyFence,
    .vkResetFences = gen75_ResetFences,
    .vkGetFenceStatus = gen75_GetFenceStatus,
    .vkWaitForFences = gen75_WaitForFences,
    .vkCreateSemaphore = gen75_CreateSemaphore,
    .vkDestroySemaphore = gen75_DestroySemaphore,
    .vkCreateEvent = gen75_CreateEvent,
    .vkDestroyEvent = gen75_DestroyEvent,
    .vkGetEventStatus = gen75_GetEventStatus,
    .vkSetEvent = gen75_SetEvent,
    .vkResetEvent = gen75_ResetEvent,
    .vkCreateQueryPool = gen75_CreateQueryPool,
    .vkDestroyQueryPool = gen75_DestroyQueryPool,
    .vkGetQueryPoolResults = gen75_GetQueryPoolResults,
    .vkCreateBuffer = gen75_CreateBuffer,
    .vkDestroyBuffer = gen75_DestroyBuffer,
    .vkCreateBufferView = gen75_CreateBufferView,
    .vkDestroyBufferView = gen75_DestroyBufferView,
    .vkCreateImage = gen75_CreateImage,
    .vkDestroyImage = gen75_DestroyImage,
    .vkGetImageSubresourceLayout = gen75_GetImageSubresourceLayout,
    .vkCreateImageView = gen75_CreateImageView,
    .vkDestroyImageView = gen75_DestroyImageView,
    .vkCreateShaderModule = gen75_CreateShaderModule,
    .vkDestroyShaderModule = gen75_DestroyShaderModule,
    .vkCreatePipelineCache = gen75_CreatePipelineCache,
    .vkDestroyPipelineCache = gen75_DestroyPipelineCache,
    .vkGetPipelineCacheData = gen75_GetPipelineCacheData,
    .vkMergePipelineCaches = gen75_MergePipelineCaches,
    .vkCreateGraphicsPipelines = gen75_CreateGraphicsPipelines,
    .vkCreateComputePipelines = gen75_CreateComputePipelines,
    .vkDestroyPipeline = gen75_DestroyPipeline,
    .vkCreatePipelineLayout = gen75_CreatePipelineLayout,
    .vkDestroyPipelineLayout = gen75_DestroyPipelineLayout,
    .vkCreateSampler = gen75_CreateSampler,
    .vkDestroySampler = gen75_DestroySampler,
    .vkCreateDescriptorSetLayout = gen75_CreateDescriptorSetLayout,
    .vkDestroyDescriptorSetLayout = gen75_DestroyDescriptorSetLayout,
    .vkCreateDescriptorPool = gen75_CreateDescriptorPool,
    .vkDestroyDescriptorPool = gen75_DestroyDescriptorPool,
    .vkResetDescriptorPool = gen75_ResetDescriptorPool,
    .vkAllocateDescriptorSets = gen75_AllocateDescriptorSets,
    .vkFreeDescriptorSets = gen75_FreeDescriptorSets,
    .vkUpdateDescriptorSets = gen75_UpdateDescriptorSets,
    .vkCreateFramebuffer = gen75_CreateFramebuffer,
    .vkDestroyFramebuffer = gen75_DestroyFramebuffer,
    .vkCreateRenderPass = gen75_CreateRenderPass,
    .vkDestroyRenderPass = gen75_DestroyRenderPass,
    .vkGetRenderAreaGranularity = gen75_GetRenderAreaGranularity,
    .vkCreateCommandPool = gen75_CreateCommandPool,
    .vkDestroyCommandPool = gen75_DestroyCommandPool,
    .vkResetCommandPool = gen75_ResetCommandPool,
    .vkAllocateCommandBuffers = gen75_AllocateCommandBuffers,
    .vkFreeCommandBuffers = gen75_FreeCommandBuffers,
    .vkBeginCommandBuffer = gen75_BeginCommandBuffer,
    .vkEndCommandBuffer = gen75_EndCommandBuffer,
    .vkResetCommandBuffer = gen75_ResetCommandBuffer,
    .vkCmdBindPipeline = gen75_CmdBindPipeline,
    .vkCmdSetViewport = gen75_CmdSetViewport,
    .vkCmdSetScissor = gen75_CmdSetScissor,
    .vkCmdSetLineWidth = gen75_CmdSetLineWidth,
    .vkCmdSetDepthBias = gen75_CmdSetDepthBias,
    .vkCmdSetBlendConstants = gen75_CmdSetBlendConstants,
    .vkCmdSetDepthBounds = gen75_CmdSetDepthBounds,
    .vkCmdSetStencilCompareMask = gen75_CmdSetStencilCompareMask,
    .vkCmdSetStencilWriteMask = gen75_CmdSetStencilWriteMask,
    .vkCmdSetStencilReference = gen75_CmdSetStencilReference,
    .vkCmdBindDescriptorSets = gen75_CmdBindDescriptorSets,
    .vkCmdBindIndexBuffer = gen75_CmdBindIndexBuffer,
    .vkCmdBindVertexBuffers = gen75_CmdBindVertexBuffers,
    .vkCmdDraw = gen75_CmdDraw,
    .vkCmdDrawIndexed = gen75_CmdDrawIndexed,
    .vkCmdDrawIndirect = gen75_CmdDrawIndirect,
    .vkCmdDrawIndexedIndirect = gen75_CmdDrawIndexedIndirect,
    .vkCmdDispatch = gen75_CmdDispatch,
    .vkCmdDispatchIndirect = gen75_CmdDispatchIndirect,
    .vkCmdCopyBuffer = gen75_CmdCopyBuffer,
    .vkCmdCopyImage = gen75_CmdCopyImage,
    .vkCmdBlitImage = gen75_CmdBlitImage,
    .vkCmdCopyBufferToImage = gen75_CmdCopyBufferToImage,
    .vkCmdCopyImageToBuffer = gen75_CmdCopyImageToBuffer,
    .vkCmdUpdateBuffer = gen75_CmdUpdateBuffer,
    .vkCmdFillBuffer = gen75_CmdFillBuffer,
    .vkCmdClearColorImage = gen75_CmdClearColorImage,
    .vkCmdClearDepthStencilImage = gen75_CmdClearDepthStencilImage,
    .vkCmdClearAttachments = gen75_CmdClearAttachments,
    .vkCmdResolveImage = gen75_CmdResolveImage,
    .vkCmdSetEvent = gen75_CmdSetEvent,
    .vkCmdResetEvent = gen75_CmdResetEvent,
    .vkCmdWaitEvents = gen75_CmdWaitEvents,
    .vkCmdPipelineBarrier = gen75_CmdPipelineBarrier,
    .vkCmdBeginQuery = gen75_CmdBeginQuery,
    .vkCmdEndQuery = gen75_CmdEndQuery,
    .vkCmdResetQueryPool = gen75_CmdResetQueryPool,
    .vkCmdWriteTimestamp = gen75_CmdWriteTimestamp,
    .vkCmdCopyQueryPoolResults = gen75_CmdCopyQueryPoolResults,
    .vkCmdPushConstants = gen75_CmdPushConstants,
    .vkCmdBeginRenderPass = gen75_CmdBeginRenderPass,
    .vkCmdNextSubpass = gen75_CmdNextSubpass,
    .vkCmdEndRenderPass = gen75_CmdEndRenderPass,
    .vkCmdExecuteCommands = gen75_CmdExecuteCommands,
    .vkDestroySurfaceKHR = gen75_DestroySurfaceKHR,
    .vkGetPhysicalDeviceSurfaceSupportKHR = gen75_GetPhysicalDeviceSurfaceSupportKHR,
    .vkGetPhysicalDeviceSurfaceCapabilitiesKHR = gen75_GetPhysicalDeviceSurfaceCapabilitiesKHR,
    .vkGetPhysicalDeviceSurfaceFormatsKHR = gen75_GetPhysicalDeviceSurfaceFormatsKHR,
    .vkGetPhysicalDeviceSurfacePresentModesKHR = gen75_GetPhysicalDeviceSurfacePresentModesKHR,
    .vkCreateSwapchainKHR = gen75_CreateSwapchainKHR,
    .vkDestroySwapchainKHR = gen75_DestroySwapchainKHR,
    .vkGetSwapchainImagesKHR = gen75_GetSwapchainImagesKHR,
    .vkAcquireNextImageKHR = gen75_AcquireNextImageKHR,
    .vkQueuePresentKHR = gen75_QueuePresentKHR,
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .vkCreateWaylandSurfaceKHR = gen75_CreateWaylandSurfaceKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .vkGetPhysicalDeviceWaylandPresentationSupportKHR = gen75_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .vkCreateXlibSurfaceKHR = gen75_CreateXlibSurfaceKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .vkGetPhysicalDeviceXlibPresentationSupportKHR = gen75_GetPhysicalDeviceXlibPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .vkCreateXcbSurfaceKHR = gen75_CreateXcbSurfaceKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .vkGetPhysicalDeviceXcbPresentationSupportKHR = gen75_GetPhysicalDeviceXcbPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
    .vkCreateDebugReportCallbackEXT = gen75_CreateDebugReportCallbackEXT,
    .vkDestroyDebugReportCallbackEXT = gen75_DestroyDebugReportCallbackEXT,
    .vkDebugReportMessageEXT = gen75_DebugReportMessageEXT,
    .vkGetPhysicalDeviceFeatures2KHR = gen75_GetPhysicalDeviceFeatures2KHR,
    .vkGetPhysicalDeviceProperties2KHR = gen75_GetPhysicalDeviceProperties2KHR,
    .vkGetPhysicalDeviceFormatProperties2KHR = gen75_GetPhysicalDeviceFormatProperties2KHR,
    .vkGetPhysicalDeviceImageFormatProperties2KHR = gen75_GetPhysicalDeviceImageFormatProperties2KHR,
    .vkGetPhysicalDeviceQueueFamilyProperties2KHR = gen75_GetPhysicalDeviceQueueFamilyProperties2KHR,
    .vkGetPhysicalDeviceMemoryProperties2KHR = gen75_GetPhysicalDeviceMemoryProperties2KHR,
    .vkGetPhysicalDeviceSparseImageFormatProperties2KHR = gen75_GetPhysicalDeviceSparseImageFormatProperties2KHR,
    .vkCmdPushDescriptorSetKHR = gen75_CmdPushDescriptorSetKHR,
    .vkTrimCommandPoolKHR = gen75_TrimCommandPoolKHR,
    .vkGetPhysicalDeviceExternalBufferPropertiesKHR = gen75_GetPhysicalDeviceExternalBufferPropertiesKHR,
    .vkGetMemoryFdKHR = gen75_GetMemoryFdKHR,
    .vkGetMemoryFdPropertiesKHR = gen75_GetMemoryFdPropertiesKHR,
    .vkGetPhysicalDeviceExternalSemaphorePropertiesKHR = gen75_GetPhysicalDeviceExternalSemaphorePropertiesKHR,
    .vkGetSemaphoreFdKHR = gen75_GetSemaphoreFdKHR,
    .vkImportSemaphoreFdKHR = gen75_ImportSemaphoreFdKHR,
    .vkGetPhysicalDeviceExternalFencePropertiesKHR = gen75_GetPhysicalDeviceExternalFencePropertiesKHR,
    .vkGetFenceFdKHR = gen75_GetFenceFdKHR,
    .vkImportFenceFdKHR = gen75_ImportFenceFdKHR,
    .vkBindBufferMemory2KHR = gen75_BindBufferMemory2KHR,
    .vkBindImageMemory2KHR = gen75_BindImageMemory2KHR,
    .vkCreateDescriptorUpdateTemplateKHR = gen75_CreateDescriptorUpdateTemplateKHR,
    .vkDestroyDescriptorUpdateTemplateKHR = gen75_DestroyDescriptorUpdateTemplateKHR,
    .vkUpdateDescriptorSetWithTemplateKHR = gen75_UpdateDescriptorSetWithTemplateKHR,
    .vkCmdPushDescriptorSetWithTemplateKHR = gen75_CmdPushDescriptorSetWithTemplateKHR,
    .vkGetPhysicalDeviceSurfaceCapabilities2KHR = gen75_GetPhysicalDeviceSurfaceCapabilities2KHR,
    .vkGetPhysicalDeviceSurfaceFormats2KHR = gen75_GetPhysicalDeviceSurfaceFormats2KHR,
    .vkGetBufferMemoryRequirements2KHR = gen75_GetBufferMemoryRequirements2KHR,
    .vkGetImageMemoryRequirements2KHR = gen75_GetImageMemoryRequirements2KHR,
    .vkGetImageSparseMemoryRequirements2KHR = gen75_GetImageSparseMemoryRequirements2KHR,
    .vkCreateSamplerYcbcrConversionKHR = gen75_CreateSamplerYcbcrConversionKHR,
    .vkDestroySamplerYcbcrConversionKHR = gen75_DestroySamplerYcbcrConversionKHR,
#ifdef ANDROID
    .vkGetSwapchainGrallocUsageANDROID = gen75_GetSwapchainGrallocUsageANDROID,
#endif // ANDROID
#ifdef ANDROID
    .vkAcquireImageANDROID = gen75_AcquireImageANDROID,
#endif // ANDROID
#ifdef ANDROID
    .vkQueueSignalReleaseImageANDROID = gen75_QueueSignalReleaseImageANDROID,
#endif // ANDROID
    .vkCreateDmaBufImageINTEL = gen75_CreateDmaBufImageINTEL,
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
    VkResult gen8_CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback) __attribute__ ((weak));
    void gen8_DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen8_DebugReportMessageEXT(VkInstance instance, VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage) __attribute__ ((weak));
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
    void gen8_GetPhysicalDeviceExternalSemaphorePropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfoKHR* pExternalSemaphoreInfo, VkExternalSemaphorePropertiesKHR* pExternalSemaphoreProperties) __attribute__ ((weak));
    VkResult gen8_GetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen8_ImportSemaphoreFdKHR(VkDevice device, const VkImportSemaphoreFdInfoKHR* pImportSemaphoreFdInfo) __attribute__ ((weak));
    void gen8_GetPhysicalDeviceExternalFencePropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfoKHR* pExternalFenceInfo, VkExternalFencePropertiesKHR* pExternalFenceProperties) __attribute__ ((weak));
    VkResult gen8_GetFenceFdKHR(VkDevice device, const VkFenceGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen8_ImportFenceFdKHR(VkDevice device, const VkImportFenceFdInfoKHR* pImportFenceFdInfo) __attribute__ ((weak));
    VkResult gen8_BindBufferMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfoKHR* pBindInfos) __attribute__ ((weak));
    VkResult gen8_BindImageMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfoKHR* pBindInfos) __attribute__ ((weak));
    VkResult gen8_CreateDescriptorUpdateTemplateKHR(VkDevice device, const VkDescriptorUpdateTemplateCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplateKHR* pDescriptorUpdateTemplate) __attribute__ ((weak));
    void gen8_DestroyDescriptorUpdateTemplateKHR(VkDevice device, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen8_UpdateDescriptorSetWithTemplateKHR(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const void* pData) __attribute__ ((weak));
    void gen8_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) __attribute__ ((weak));
    VkResult gen8_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen8_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats) __attribute__ ((weak));
    void gen8_GetBufferMemoryRequirements2KHR(VkDevice device, const VkBufferMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void gen8_GetImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void gen8_GetImageSparseMemoryRequirements2KHR(VkDevice device, const VkImageSparseMemoryRequirementsInfo2KHR* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2KHR* pSparseMemoryRequirements) __attribute__ ((weak));
    VkResult gen8_CreateSamplerYcbcrConversionKHR(VkDevice device, const VkSamplerYcbcrConversionCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversionKHR* pYcbcrConversion) __attribute__ ((weak));
    void gen8_DestroySamplerYcbcrConversionKHR(VkDevice device, VkSamplerYcbcrConversionKHR ycbcrConversion, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
#ifdef ANDROID
    VkResult gen8_GetSwapchainGrallocUsageANDROID(VkDevice device, VkFormat format, VkImageUsageFlags imageUsage, int* grallocUsage) __attribute__ ((weak));
#endif // ANDROID
#ifdef ANDROID
    VkResult gen8_AcquireImageANDROID(VkDevice device, VkImage image, int nativeFenceFd, VkSemaphore semaphore, VkFence fence) __attribute__ ((weak));
#endif // ANDROID
#ifdef ANDROID
    VkResult gen8_QueueSignalReleaseImageANDROID(VkQueue queue, uint32_t waitSemaphoreCount, const VkSemaphore* pWaitSemaphores, VkImage image, int* pNativeFenceFd) __attribute__ ((weak));
#endif // ANDROID
    VkResult gen8_CreateDmaBufImageINTEL(VkDevice device, const VkDmaBufImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMem, VkImage* pImage) __attribute__ ((weak));

  const struct anv_dispatch_table gen8_dispatch_table = {
    .vkCreateInstance = gen8_CreateInstance,
    .vkDestroyInstance = gen8_DestroyInstance,
    .vkEnumeratePhysicalDevices = gen8_EnumeratePhysicalDevices,
    .vkGetDeviceProcAddr = gen8_GetDeviceProcAddr,
    .vkGetInstanceProcAddr = gen8_GetInstanceProcAddr,
    .vkGetPhysicalDeviceProperties = gen8_GetPhysicalDeviceProperties,
    .vkGetPhysicalDeviceQueueFamilyProperties = gen8_GetPhysicalDeviceQueueFamilyProperties,
    .vkGetPhysicalDeviceMemoryProperties = gen8_GetPhysicalDeviceMemoryProperties,
    .vkGetPhysicalDeviceFeatures = gen8_GetPhysicalDeviceFeatures,
    .vkGetPhysicalDeviceFormatProperties = gen8_GetPhysicalDeviceFormatProperties,
    .vkGetPhysicalDeviceImageFormatProperties = gen8_GetPhysicalDeviceImageFormatProperties,
    .vkCreateDevice = gen8_CreateDevice,
    .vkDestroyDevice = gen8_DestroyDevice,
    .vkEnumerateInstanceLayerProperties = gen8_EnumerateInstanceLayerProperties,
    .vkEnumerateInstanceExtensionProperties = gen8_EnumerateInstanceExtensionProperties,
    .vkEnumerateDeviceLayerProperties = gen8_EnumerateDeviceLayerProperties,
    .vkEnumerateDeviceExtensionProperties = gen8_EnumerateDeviceExtensionProperties,
    .vkGetDeviceQueue = gen8_GetDeviceQueue,
    .vkQueueSubmit = gen8_QueueSubmit,
    .vkQueueWaitIdle = gen8_QueueWaitIdle,
    .vkDeviceWaitIdle = gen8_DeviceWaitIdle,
    .vkAllocateMemory = gen8_AllocateMemory,
    .vkFreeMemory = gen8_FreeMemory,
    .vkMapMemory = gen8_MapMemory,
    .vkUnmapMemory = gen8_UnmapMemory,
    .vkFlushMappedMemoryRanges = gen8_FlushMappedMemoryRanges,
    .vkInvalidateMappedMemoryRanges = gen8_InvalidateMappedMemoryRanges,
    .vkGetDeviceMemoryCommitment = gen8_GetDeviceMemoryCommitment,
    .vkGetBufferMemoryRequirements = gen8_GetBufferMemoryRequirements,
    .vkBindBufferMemory = gen8_BindBufferMemory,
    .vkGetImageMemoryRequirements = gen8_GetImageMemoryRequirements,
    .vkBindImageMemory = gen8_BindImageMemory,
    .vkGetImageSparseMemoryRequirements = gen8_GetImageSparseMemoryRequirements,
    .vkGetPhysicalDeviceSparseImageFormatProperties = gen8_GetPhysicalDeviceSparseImageFormatProperties,
    .vkQueueBindSparse = gen8_QueueBindSparse,
    .vkCreateFence = gen8_CreateFence,
    .vkDestroyFence = gen8_DestroyFence,
    .vkResetFences = gen8_ResetFences,
    .vkGetFenceStatus = gen8_GetFenceStatus,
    .vkWaitForFences = gen8_WaitForFences,
    .vkCreateSemaphore = gen8_CreateSemaphore,
    .vkDestroySemaphore = gen8_DestroySemaphore,
    .vkCreateEvent = gen8_CreateEvent,
    .vkDestroyEvent = gen8_DestroyEvent,
    .vkGetEventStatus = gen8_GetEventStatus,
    .vkSetEvent = gen8_SetEvent,
    .vkResetEvent = gen8_ResetEvent,
    .vkCreateQueryPool = gen8_CreateQueryPool,
    .vkDestroyQueryPool = gen8_DestroyQueryPool,
    .vkGetQueryPoolResults = gen8_GetQueryPoolResults,
    .vkCreateBuffer = gen8_CreateBuffer,
    .vkDestroyBuffer = gen8_DestroyBuffer,
    .vkCreateBufferView = gen8_CreateBufferView,
    .vkDestroyBufferView = gen8_DestroyBufferView,
    .vkCreateImage = gen8_CreateImage,
    .vkDestroyImage = gen8_DestroyImage,
    .vkGetImageSubresourceLayout = gen8_GetImageSubresourceLayout,
    .vkCreateImageView = gen8_CreateImageView,
    .vkDestroyImageView = gen8_DestroyImageView,
    .vkCreateShaderModule = gen8_CreateShaderModule,
    .vkDestroyShaderModule = gen8_DestroyShaderModule,
    .vkCreatePipelineCache = gen8_CreatePipelineCache,
    .vkDestroyPipelineCache = gen8_DestroyPipelineCache,
    .vkGetPipelineCacheData = gen8_GetPipelineCacheData,
    .vkMergePipelineCaches = gen8_MergePipelineCaches,
    .vkCreateGraphicsPipelines = gen8_CreateGraphicsPipelines,
    .vkCreateComputePipelines = gen8_CreateComputePipelines,
    .vkDestroyPipeline = gen8_DestroyPipeline,
    .vkCreatePipelineLayout = gen8_CreatePipelineLayout,
    .vkDestroyPipelineLayout = gen8_DestroyPipelineLayout,
    .vkCreateSampler = gen8_CreateSampler,
    .vkDestroySampler = gen8_DestroySampler,
    .vkCreateDescriptorSetLayout = gen8_CreateDescriptorSetLayout,
    .vkDestroyDescriptorSetLayout = gen8_DestroyDescriptorSetLayout,
    .vkCreateDescriptorPool = gen8_CreateDescriptorPool,
    .vkDestroyDescriptorPool = gen8_DestroyDescriptorPool,
    .vkResetDescriptorPool = gen8_ResetDescriptorPool,
    .vkAllocateDescriptorSets = gen8_AllocateDescriptorSets,
    .vkFreeDescriptorSets = gen8_FreeDescriptorSets,
    .vkUpdateDescriptorSets = gen8_UpdateDescriptorSets,
    .vkCreateFramebuffer = gen8_CreateFramebuffer,
    .vkDestroyFramebuffer = gen8_DestroyFramebuffer,
    .vkCreateRenderPass = gen8_CreateRenderPass,
    .vkDestroyRenderPass = gen8_DestroyRenderPass,
    .vkGetRenderAreaGranularity = gen8_GetRenderAreaGranularity,
    .vkCreateCommandPool = gen8_CreateCommandPool,
    .vkDestroyCommandPool = gen8_DestroyCommandPool,
    .vkResetCommandPool = gen8_ResetCommandPool,
    .vkAllocateCommandBuffers = gen8_AllocateCommandBuffers,
    .vkFreeCommandBuffers = gen8_FreeCommandBuffers,
    .vkBeginCommandBuffer = gen8_BeginCommandBuffer,
    .vkEndCommandBuffer = gen8_EndCommandBuffer,
    .vkResetCommandBuffer = gen8_ResetCommandBuffer,
    .vkCmdBindPipeline = gen8_CmdBindPipeline,
    .vkCmdSetViewport = gen8_CmdSetViewport,
    .vkCmdSetScissor = gen8_CmdSetScissor,
    .vkCmdSetLineWidth = gen8_CmdSetLineWidth,
    .vkCmdSetDepthBias = gen8_CmdSetDepthBias,
    .vkCmdSetBlendConstants = gen8_CmdSetBlendConstants,
    .vkCmdSetDepthBounds = gen8_CmdSetDepthBounds,
    .vkCmdSetStencilCompareMask = gen8_CmdSetStencilCompareMask,
    .vkCmdSetStencilWriteMask = gen8_CmdSetStencilWriteMask,
    .vkCmdSetStencilReference = gen8_CmdSetStencilReference,
    .vkCmdBindDescriptorSets = gen8_CmdBindDescriptorSets,
    .vkCmdBindIndexBuffer = gen8_CmdBindIndexBuffer,
    .vkCmdBindVertexBuffers = gen8_CmdBindVertexBuffers,
    .vkCmdDraw = gen8_CmdDraw,
    .vkCmdDrawIndexed = gen8_CmdDrawIndexed,
    .vkCmdDrawIndirect = gen8_CmdDrawIndirect,
    .vkCmdDrawIndexedIndirect = gen8_CmdDrawIndexedIndirect,
    .vkCmdDispatch = gen8_CmdDispatch,
    .vkCmdDispatchIndirect = gen8_CmdDispatchIndirect,
    .vkCmdCopyBuffer = gen8_CmdCopyBuffer,
    .vkCmdCopyImage = gen8_CmdCopyImage,
    .vkCmdBlitImage = gen8_CmdBlitImage,
    .vkCmdCopyBufferToImage = gen8_CmdCopyBufferToImage,
    .vkCmdCopyImageToBuffer = gen8_CmdCopyImageToBuffer,
    .vkCmdUpdateBuffer = gen8_CmdUpdateBuffer,
    .vkCmdFillBuffer = gen8_CmdFillBuffer,
    .vkCmdClearColorImage = gen8_CmdClearColorImage,
    .vkCmdClearDepthStencilImage = gen8_CmdClearDepthStencilImage,
    .vkCmdClearAttachments = gen8_CmdClearAttachments,
    .vkCmdResolveImage = gen8_CmdResolveImage,
    .vkCmdSetEvent = gen8_CmdSetEvent,
    .vkCmdResetEvent = gen8_CmdResetEvent,
    .vkCmdWaitEvents = gen8_CmdWaitEvents,
    .vkCmdPipelineBarrier = gen8_CmdPipelineBarrier,
    .vkCmdBeginQuery = gen8_CmdBeginQuery,
    .vkCmdEndQuery = gen8_CmdEndQuery,
    .vkCmdResetQueryPool = gen8_CmdResetQueryPool,
    .vkCmdWriteTimestamp = gen8_CmdWriteTimestamp,
    .vkCmdCopyQueryPoolResults = gen8_CmdCopyQueryPoolResults,
    .vkCmdPushConstants = gen8_CmdPushConstants,
    .vkCmdBeginRenderPass = gen8_CmdBeginRenderPass,
    .vkCmdNextSubpass = gen8_CmdNextSubpass,
    .vkCmdEndRenderPass = gen8_CmdEndRenderPass,
    .vkCmdExecuteCommands = gen8_CmdExecuteCommands,
    .vkDestroySurfaceKHR = gen8_DestroySurfaceKHR,
    .vkGetPhysicalDeviceSurfaceSupportKHR = gen8_GetPhysicalDeviceSurfaceSupportKHR,
    .vkGetPhysicalDeviceSurfaceCapabilitiesKHR = gen8_GetPhysicalDeviceSurfaceCapabilitiesKHR,
    .vkGetPhysicalDeviceSurfaceFormatsKHR = gen8_GetPhysicalDeviceSurfaceFormatsKHR,
    .vkGetPhysicalDeviceSurfacePresentModesKHR = gen8_GetPhysicalDeviceSurfacePresentModesKHR,
    .vkCreateSwapchainKHR = gen8_CreateSwapchainKHR,
    .vkDestroySwapchainKHR = gen8_DestroySwapchainKHR,
    .vkGetSwapchainImagesKHR = gen8_GetSwapchainImagesKHR,
    .vkAcquireNextImageKHR = gen8_AcquireNextImageKHR,
    .vkQueuePresentKHR = gen8_QueuePresentKHR,
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .vkCreateWaylandSurfaceKHR = gen8_CreateWaylandSurfaceKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .vkGetPhysicalDeviceWaylandPresentationSupportKHR = gen8_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .vkCreateXlibSurfaceKHR = gen8_CreateXlibSurfaceKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .vkGetPhysicalDeviceXlibPresentationSupportKHR = gen8_GetPhysicalDeviceXlibPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .vkCreateXcbSurfaceKHR = gen8_CreateXcbSurfaceKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .vkGetPhysicalDeviceXcbPresentationSupportKHR = gen8_GetPhysicalDeviceXcbPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
    .vkCreateDebugReportCallbackEXT = gen8_CreateDebugReportCallbackEXT,
    .vkDestroyDebugReportCallbackEXT = gen8_DestroyDebugReportCallbackEXT,
    .vkDebugReportMessageEXT = gen8_DebugReportMessageEXT,
    .vkGetPhysicalDeviceFeatures2KHR = gen8_GetPhysicalDeviceFeatures2KHR,
    .vkGetPhysicalDeviceProperties2KHR = gen8_GetPhysicalDeviceProperties2KHR,
    .vkGetPhysicalDeviceFormatProperties2KHR = gen8_GetPhysicalDeviceFormatProperties2KHR,
    .vkGetPhysicalDeviceImageFormatProperties2KHR = gen8_GetPhysicalDeviceImageFormatProperties2KHR,
    .vkGetPhysicalDeviceQueueFamilyProperties2KHR = gen8_GetPhysicalDeviceQueueFamilyProperties2KHR,
    .vkGetPhysicalDeviceMemoryProperties2KHR = gen8_GetPhysicalDeviceMemoryProperties2KHR,
    .vkGetPhysicalDeviceSparseImageFormatProperties2KHR = gen8_GetPhysicalDeviceSparseImageFormatProperties2KHR,
    .vkCmdPushDescriptorSetKHR = gen8_CmdPushDescriptorSetKHR,
    .vkTrimCommandPoolKHR = gen8_TrimCommandPoolKHR,
    .vkGetPhysicalDeviceExternalBufferPropertiesKHR = gen8_GetPhysicalDeviceExternalBufferPropertiesKHR,
    .vkGetMemoryFdKHR = gen8_GetMemoryFdKHR,
    .vkGetMemoryFdPropertiesKHR = gen8_GetMemoryFdPropertiesKHR,
    .vkGetPhysicalDeviceExternalSemaphorePropertiesKHR = gen8_GetPhysicalDeviceExternalSemaphorePropertiesKHR,
    .vkGetSemaphoreFdKHR = gen8_GetSemaphoreFdKHR,
    .vkImportSemaphoreFdKHR = gen8_ImportSemaphoreFdKHR,
    .vkGetPhysicalDeviceExternalFencePropertiesKHR = gen8_GetPhysicalDeviceExternalFencePropertiesKHR,
    .vkGetFenceFdKHR = gen8_GetFenceFdKHR,
    .vkImportFenceFdKHR = gen8_ImportFenceFdKHR,
    .vkBindBufferMemory2KHR = gen8_BindBufferMemory2KHR,
    .vkBindImageMemory2KHR = gen8_BindImageMemory2KHR,
    .vkCreateDescriptorUpdateTemplateKHR = gen8_CreateDescriptorUpdateTemplateKHR,
    .vkDestroyDescriptorUpdateTemplateKHR = gen8_DestroyDescriptorUpdateTemplateKHR,
    .vkUpdateDescriptorSetWithTemplateKHR = gen8_UpdateDescriptorSetWithTemplateKHR,
    .vkCmdPushDescriptorSetWithTemplateKHR = gen8_CmdPushDescriptorSetWithTemplateKHR,
    .vkGetPhysicalDeviceSurfaceCapabilities2KHR = gen8_GetPhysicalDeviceSurfaceCapabilities2KHR,
    .vkGetPhysicalDeviceSurfaceFormats2KHR = gen8_GetPhysicalDeviceSurfaceFormats2KHR,
    .vkGetBufferMemoryRequirements2KHR = gen8_GetBufferMemoryRequirements2KHR,
    .vkGetImageMemoryRequirements2KHR = gen8_GetImageMemoryRequirements2KHR,
    .vkGetImageSparseMemoryRequirements2KHR = gen8_GetImageSparseMemoryRequirements2KHR,
    .vkCreateSamplerYcbcrConversionKHR = gen8_CreateSamplerYcbcrConversionKHR,
    .vkDestroySamplerYcbcrConversionKHR = gen8_DestroySamplerYcbcrConversionKHR,
#ifdef ANDROID
    .vkGetSwapchainGrallocUsageANDROID = gen8_GetSwapchainGrallocUsageANDROID,
#endif // ANDROID
#ifdef ANDROID
    .vkAcquireImageANDROID = gen8_AcquireImageANDROID,
#endif // ANDROID
#ifdef ANDROID
    .vkQueueSignalReleaseImageANDROID = gen8_QueueSignalReleaseImageANDROID,
#endif // ANDROID
    .vkCreateDmaBufImageINTEL = gen8_CreateDmaBufImageINTEL,
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
    VkResult gen9_CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback) __attribute__ ((weak));
    void gen9_DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen9_DebugReportMessageEXT(VkInstance instance, VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage) __attribute__ ((weak));
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
    void gen9_GetPhysicalDeviceExternalSemaphorePropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfoKHR* pExternalSemaphoreInfo, VkExternalSemaphorePropertiesKHR* pExternalSemaphoreProperties) __attribute__ ((weak));
    VkResult gen9_GetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen9_ImportSemaphoreFdKHR(VkDevice device, const VkImportSemaphoreFdInfoKHR* pImportSemaphoreFdInfo) __attribute__ ((weak));
    void gen9_GetPhysicalDeviceExternalFencePropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfoKHR* pExternalFenceInfo, VkExternalFencePropertiesKHR* pExternalFenceProperties) __attribute__ ((weak));
    VkResult gen9_GetFenceFdKHR(VkDevice device, const VkFenceGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen9_ImportFenceFdKHR(VkDevice device, const VkImportFenceFdInfoKHR* pImportFenceFdInfo) __attribute__ ((weak));
    VkResult gen9_BindBufferMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfoKHR* pBindInfos) __attribute__ ((weak));
    VkResult gen9_BindImageMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfoKHR* pBindInfos) __attribute__ ((weak));
    VkResult gen9_CreateDescriptorUpdateTemplateKHR(VkDevice device, const VkDescriptorUpdateTemplateCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplateKHR* pDescriptorUpdateTemplate) __attribute__ ((weak));
    void gen9_DestroyDescriptorUpdateTemplateKHR(VkDevice device, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen9_UpdateDescriptorSetWithTemplateKHR(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const void* pData) __attribute__ ((weak));
    void gen9_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) __attribute__ ((weak));
    VkResult gen9_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen9_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats) __attribute__ ((weak));
    void gen9_GetBufferMemoryRequirements2KHR(VkDevice device, const VkBufferMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void gen9_GetImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void gen9_GetImageSparseMemoryRequirements2KHR(VkDevice device, const VkImageSparseMemoryRequirementsInfo2KHR* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2KHR* pSparseMemoryRequirements) __attribute__ ((weak));
    VkResult gen9_CreateSamplerYcbcrConversionKHR(VkDevice device, const VkSamplerYcbcrConversionCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversionKHR* pYcbcrConversion) __attribute__ ((weak));
    void gen9_DestroySamplerYcbcrConversionKHR(VkDevice device, VkSamplerYcbcrConversionKHR ycbcrConversion, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
#ifdef ANDROID
    VkResult gen9_GetSwapchainGrallocUsageANDROID(VkDevice device, VkFormat format, VkImageUsageFlags imageUsage, int* grallocUsage) __attribute__ ((weak));
#endif // ANDROID
#ifdef ANDROID
    VkResult gen9_AcquireImageANDROID(VkDevice device, VkImage image, int nativeFenceFd, VkSemaphore semaphore, VkFence fence) __attribute__ ((weak));
#endif // ANDROID
#ifdef ANDROID
    VkResult gen9_QueueSignalReleaseImageANDROID(VkQueue queue, uint32_t waitSemaphoreCount, const VkSemaphore* pWaitSemaphores, VkImage image, int* pNativeFenceFd) __attribute__ ((weak));
#endif // ANDROID
    VkResult gen9_CreateDmaBufImageINTEL(VkDevice device, const VkDmaBufImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMem, VkImage* pImage) __attribute__ ((weak));

  const struct anv_dispatch_table gen9_dispatch_table = {
    .vkCreateInstance = gen9_CreateInstance,
    .vkDestroyInstance = gen9_DestroyInstance,
    .vkEnumeratePhysicalDevices = gen9_EnumeratePhysicalDevices,
    .vkGetDeviceProcAddr = gen9_GetDeviceProcAddr,
    .vkGetInstanceProcAddr = gen9_GetInstanceProcAddr,
    .vkGetPhysicalDeviceProperties = gen9_GetPhysicalDeviceProperties,
    .vkGetPhysicalDeviceQueueFamilyProperties = gen9_GetPhysicalDeviceQueueFamilyProperties,
    .vkGetPhysicalDeviceMemoryProperties = gen9_GetPhysicalDeviceMemoryProperties,
    .vkGetPhysicalDeviceFeatures = gen9_GetPhysicalDeviceFeatures,
    .vkGetPhysicalDeviceFormatProperties = gen9_GetPhysicalDeviceFormatProperties,
    .vkGetPhysicalDeviceImageFormatProperties = gen9_GetPhysicalDeviceImageFormatProperties,
    .vkCreateDevice = gen9_CreateDevice,
    .vkDestroyDevice = gen9_DestroyDevice,
    .vkEnumerateInstanceLayerProperties = gen9_EnumerateInstanceLayerProperties,
    .vkEnumerateInstanceExtensionProperties = gen9_EnumerateInstanceExtensionProperties,
    .vkEnumerateDeviceLayerProperties = gen9_EnumerateDeviceLayerProperties,
    .vkEnumerateDeviceExtensionProperties = gen9_EnumerateDeviceExtensionProperties,
    .vkGetDeviceQueue = gen9_GetDeviceQueue,
    .vkQueueSubmit = gen9_QueueSubmit,
    .vkQueueWaitIdle = gen9_QueueWaitIdle,
    .vkDeviceWaitIdle = gen9_DeviceWaitIdle,
    .vkAllocateMemory = gen9_AllocateMemory,
    .vkFreeMemory = gen9_FreeMemory,
    .vkMapMemory = gen9_MapMemory,
    .vkUnmapMemory = gen9_UnmapMemory,
    .vkFlushMappedMemoryRanges = gen9_FlushMappedMemoryRanges,
    .vkInvalidateMappedMemoryRanges = gen9_InvalidateMappedMemoryRanges,
    .vkGetDeviceMemoryCommitment = gen9_GetDeviceMemoryCommitment,
    .vkGetBufferMemoryRequirements = gen9_GetBufferMemoryRequirements,
    .vkBindBufferMemory = gen9_BindBufferMemory,
    .vkGetImageMemoryRequirements = gen9_GetImageMemoryRequirements,
    .vkBindImageMemory = gen9_BindImageMemory,
    .vkGetImageSparseMemoryRequirements = gen9_GetImageSparseMemoryRequirements,
    .vkGetPhysicalDeviceSparseImageFormatProperties = gen9_GetPhysicalDeviceSparseImageFormatProperties,
    .vkQueueBindSparse = gen9_QueueBindSparse,
    .vkCreateFence = gen9_CreateFence,
    .vkDestroyFence = gen9_DestroyFence,
    .vkResetFences = gen9_ResetFences,
    .vkGetFenceStatus = gen9_GetFenceStatus,
    .vkWaitForFences = gen9_WaitForFences,
    .vkCreateSemaphore = gen9_CreateSemaphore,
    .vkDestroySemaphore = gen9_DestroySemaphore,
    .vkCreateEvent = gen9_CreateEvent,
    .vkDestroyEvent = gen9_DestroyEvent,
    .vkGetEventStatus = gen9_GetEventStatus,
    .vkSetEvent = gen9_SetEvent,
    .vkResetEvent = gen9_ResetEvent,
    .vkCreateQueryPool = gen9_CreateQueryPool,
    .vkDestroyQueryPool = gen9_DestroyQueryPool,
    .vkGetQueryPoolResults = gen9_GetQueryPoolResults,
    .vkCreateBuffer = gen9_CreateBuffer,
    .vkDestroyBuffer = gen9_DestroyBuffer,
    .vkCreateBufferView = gen9_CreateBufferView,
    .vkDestroyBufferView = gen9_DestroyBufferView,
    .vkCreateImage = gen9_CreateImage,
    .vkDestroyImage = gen9_DestroyImage,
    .vkGetImageSubresourceLayout = gen9_GetImageSubresourceLayout,
    .vkCreateImageView = gen9_CreateImageView,
    .vkDestroyImageView = gen9_DestroyImageView,
    .vkCreateShaderModule = gen9_CreateShaderModule,
    .vkDestroyShaderModule = gen9_DestroyShaderModule,
    .vkCreatePipelineCache = gen9_CreatePipelineCache,
    .vkDestroyPipelineCache = gen9_DestroyPipelineCache,
    .vkGetPipelineCacheData = gen9_GetPipelineCacheData,
    .vkMergePipelineCaches = gen9_MergePipelineCaches,
    .vkCreateGraphicsPipelines = gen9_CreateGraphicsPipelines,
    .vkCreateComputePipelines = gen9_CreateComputePipelines,
    .vkDestroyPipeline = gen9_DestroyPipeline,
    .vkCreatePipelineLayout = gen9_CreatePipelineLayout,
    .vkDestroyPipelineLayout = gen9_DestroyPipelineLayout,
    .vkCreateSampler = gen9_CreateSampler,
    .vkDestroySampler = gen9_DestroySampler,
    .vkCreateDescriptorSetLayout = gen9_CreateDescriptorSetLayout,
    .vkDestroyDescriptorSetLayout = gen9_DestroyDescriptorSetLayout,
    .vkCreateDescriptorPool = gen9_CreateDescriptorPool,
    .vkDestroyDescriptorPool = gen9_DestroyDescriptorPool,
    .vkResetDescriptorPool = gen9_ResetDescriptorPool,
    .vkAllocateDescriptorSets = gen9_AllocateDescriptorSets,
    .vkFreeDescriptorSets = gen9_FreeDescriptorSets,
    .vkUpdateDescriptorSets = gen9_UpdateDescriptorSets,
    .vkCreateFramebuffer = gen9_CreateFramebuffer,
    .vkDestroyFramebuffer = gen9_DestroyFramebuffer,
    .vkCreateRenderPass = gen9_CreateRenderPass,
    .vkDestroyRenderPass = gen9_DestroyRenderPass,
    .vkGetRenderAreaGranularity = gen9_GetRenderAreaGranularity,
    .vkCreateCommandPool = gen9_CreateCommandPool,
    .vkDestroyCommandPool = gen9_DestroyCommandPool,
    .vkResetCommandPool = gen9_ResetCommandPool,
    .vkAllocateCommandBuffers = gen9_AllocateCommandBuffers,
    .vkFreeCommandBuffers = gen9_FreeCommandBuffers,
    .vkBeginCommandBuffer = gen9_BeginCommandBuffer,
    .vkEndCommandBuffer = gen9_EndCommandBuffer,
    .vkResetCommandBuffer = gen9_ResetCommandBuffer,
    .vkCmdBindPipeline = gen9_CmdBindPipeline,
    .vkCmdSetViewport = gen9_CmdSetViewport,
    .vkCmdSetScissor = gen9_CmdSetScissor,
    .vkCmdSetLineWidth = gen9_CmdSetLineWidth,
    .vkCmdSetDepthBias = gen9_CmdSetDepthBias,
    .vkCmdSetBlendConstants = gen9_CmdSetBlendConstants,
    .vkCmdSetDepthBounds = gen9_CmdSetDepthBounds,
    .vkCmdSetStencilCompareMask = gen9_CmdSetStencilCompareMask,
    .vkCmdSetStencilWriteMask = gen9_CmdSetStencilWriteMask,
    .vkCmdSetStencilReference = gen9_CmdSetStencilReference,
    .vkCmdBindDescriptorSets = gen9_CmdBindDescriptorSets,
    .vkCmdBindIndexBuffer = gen9_CmdBindIndexBuffer,
    .vkCmdBindVertexBuffers = gen9_CmdBindVertexBuffers,
    .vkCmdDraw = gen9_CmdDraw,
    .vkCmdDrawIndexed = gen9_CmdDrawIndexed,
    .vkCmdDrawIndirect = gen9_CmdDrawIndirect,
    .vkCmdDrawIndexedIndirect = gen9_CmdDrawIndexedIndirect,
    .vkCmdDispatch = gen9_CmdDispatch,
    .vkCmdDispatchIndirect = gen9_CmdDispatchIndirect,
    .vkCmdCopyBuffer = gen9_CmdCopyBuffer,
    .vkCmdCopyImage = gen9_CmdCopyImage,
    .vkCmdBlitImage = gen9_CmdBlitImage,
    .vkCmdCopyBufferToImage = gen9_CmdCopyBufferToImage,
    .vkCmdCopyImageToBuffer = gen9_CmdCopyImageToBuffer,
    .vkCmdUpdateBuffer = gen9_CmdUpdateBuffer,
    .vkCmdFillBuffer = gen9_CmdFillBuffer,
    .vkCmdClearColorImage = gen9_CmdClearColorImage,
    .vkCmdClearDepthStencilImage = gen9_CmdClearDepthStencilImage,
    .vkCmdClearAttachments = gen9_CmdClearAttachments,
    .vkCmdResolveImage = gen9_CmdResolveImage,
    .vkCmdSetEvent = gen9_CmdSetEvent,
    .vkCmdResetEvent = gen9_CmdResetEvent,
    .vkCmdWaitEvents = gen9_CmdWaitEvents,
    .vkCmdPipelineBarrier = gen9_CmdPipelineBarrier,
    .vkCmdBeginQuery = gen9_CmdBeginQuery,
    .vkCmdEndQuery = gen9_CmdEndQuery,
    .vkCmdResetQueryPool = gen9_CmdResetQueryPool,
    .vkCmdWriteTimestamp = gen9_CmdWriteTimestamp,
    .vkCmdCopyQueryPoolResults = gen9_CmdCopyQueryPoolResults,
    .vkCmdPushConstants = gen9_CmdPushConstants,
    .vkCmdBeginRenderPass = gen9_CmdBeginRenderPass,
    .vkCmdNextSubpass = gen9_CmdNextSubpass,
    .vkCmdEndRenderPass = gen9_CmdEndRenderPass,
    .vkCmdExecuteCommands = gen9_CmdExecuteCommands,
    .vkDestroySurfaceKHR = gen9_DestroySurfaceKHR,
    .vkGetPhysicalDeviceSurfaceSupportKHR = gen9_GetPhysicalDeviceSurfaceSupportKHR,
    .vkGetPhysicalDeviceSurfaceCapabilitiesKHR = gen9_GetPhysicalDeviceSurfaceCapabilitiesKHR,
    .vkGetPhysicalDeviceSurfaceFormatsKHR = gen9_GetPhysicalDeviceSurfaceFormatsKHR,
    .vkGetPhysicalDeviceSurfacePresentModesKHR = gen9_GetPhysicalDeviceSurfacePresentModesKHR,
    .vkCreateSwapchainKHR = gen9_CreateSwapchainKHR,
    .vkDestroySwapchainKHR = gen9_DestroySwapchainKHR,
    .vkGetSwapchainImagesKHR = gen9_GetSwapchainImagesKHR,
    .vkAcquireNextImageKHR = gen9_AcquireNextImageKHR,
    .vkQueuePresentKHR = gen9_QueuePresentKHR,
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .vkCreateWaylandSurfaceKHR = gen9_CreateWaylandSurfaceKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .vkGetPhysicalDeviceWaylandPresentationSupportKHR = gen9_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .vkCreateXlibSurfaceKHR = gen9_CreateXlibSurfaceKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .vkGetPhysicalDeviceXlibPresentationSupportKHR = gen9_GetPhysicalDeviceXlibPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .vkCreateXcbSurfaceKHR = gen9_CreateXcbSurfaceKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .vkGetPhysicalDeviceXcbPresentationSupportKHR = gen9_GetPhysicalDeviceXcbPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
    .vkCreateDebugReportCallbackEXT = gen9_CreateDebugReportCallbackEXT,
    .vkDestroyDebugReportCallbackEXT = gen9_DestroyDebugReportCallbackEXT,
    .vkDebugReportMessageEXT = gen9_DebugReportMessageEXT,
    .vkGetPhysicalDeviceFeatures2KHR = gen9_GetPhysicalDeviceFeatures2KHR,
    .vkGetPhysicalDeviceProperties2KHR = gen9_GetPhysicalDeviceProperties2KHR,
    .vkGetPhysicalDeviceFormatProperties2KHR = gen9_GetPhysicalDeviceFormatProperties2KHR,
    .vkGetPhysicalDeviceImageFormatProperties2KHR = gen9_GetPhysicalDeviceImageFormatProperties2KHR,
    .vkGetPhysicalDeviceQueueFamilyProperties2KHR = gen9_GetPhysicalDeviceQueueFamilyProperties2KHR,
    .vkGetPhysicalDeviceMemoryProperties2KHR = gen9_GetPhysicalDeviceMemoryProperties2KHR,
    .vkGetPhysicalDeviceSparseImageFormatProperties2KHR = gen9_GetPhysicalDeviceSparseImageFormatProperties2KHR,
    .vkCmdPushDescriptorSetKHR = gen9_CmdPushDescriptorSetKHR,
    .vkTrimCommandPoolKHR = gen9_TrimCommandPoolKHR,
    .vkGetPhysicalDeviceExternalBufferPropertiesKHR = gen9_GetPhysicalDeviceExternalBufferPropertiesKHR,
    .vkGetMemoryFdKHR = gen9_GetMemoryFdKHR,
    .vkGetMemoryFdPropertiesKHR = gen9_GetMemoryFdPropertiesKHR,
    .vkGetPhysicalDeviceExternalSemaphorePropertiesKHR = gen9_GetPhysicalDeviceExternalSemaphorePropertiesKHR,
    .vkGetSemaphoreFdKHR = gen9_GetSemaphoreFdKHR,
    .vkImportSemaphoreFdKHR = gen9_ImportSemaphoreFdKHR,
    .vkGetPhysicalDeviceExternalFencePropertiesKHR = gen9_GetPhysicalDeviceExternalFencePropertiesKHR,
    .vkGetFenceFdKHR = gen9_GetFenceFdKHR,
    .vkImportFenceFdKHR = gen9_ImportFenceFdKHR,
    .vkBindBufferMemory2KHR = gen9_BindBufferMemory2KHR,
    .vkBindImageMemory2KHR = gen9_BindImageMemory2KHR,
    .vkCreateDescriptorUpdateTemplateKHR = gen9_CreateDescriptorUpdateTemplateKHR,
    .vkDestroyDescriptorUpdateTemplateKHR = gen9_DestroyDescriptorUpdateTemplateKHR,
    .vkUpdateDescriptorSetWithTemplateKHR = gen9_UpdateDescriptorSetWithTemplateKHR,
    .vkCmdPushDescriptorSetWithTemplateKHR = gen9_CmdPushDescriptorSetWithTemplateKHR,
    .vkGetPhysicalDeviceSurfaceCapabilities2KHR = gen9_GetPhysicalDeviceSurfaceCapabilities2KHR,
    .vkGetPhysicalDeviceSurfaceFormats2KHR = gen9_GetPhysicalDeviceSurfaceFormats2KHR,
    .vkGetBufferMemoryRequirements2KHR = gen9_GetBufferMemoryRequirements2KHR,
    .vkGetImageMemoryRequirements2KHR = gen9_GetImageMemoryRequirements2KHR,
    .vkGetImageSparseMemoryRequirements2KHR = gen9_GetImageSparseMemoryRequirements2KHR,
    .vkCreateSamplerYcbcrConversionKHR = gen9_CreateSamplerYcbcrConversionKHR,
    .vkDestroySamplerYcbcrConversionKHR = gen9_DestroySamplerYcbcrConversionKHR,
#ifdef ANDROID
    .vkGetSwapchainGrallocUsageANDROID = gen9_GetSwapchainGrallocUsageANDROID,
#endif // ANDROID
#ifdef ANDROID
    .vkAcquireImageANDROID = gen9_AcquireImageANDROID,
#endif // ANDROID
#ifdef ANDROID
    .vkQueueSignalReleaseImageANDROID = gen9_QueueSignalReleaseImageANDROID,
#endif // ANDROID
    .vkCreateDmaBufImageINTEL = gen9_CreateDmaBufImageINTEL,
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
    VkResult gen10_CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback) __attribute__ ((weak));
    void gen10_DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen10_DebugReportMessageEXT(VkInstance instance, VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage) __attribute__ ((weak));
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
    void gen10_GetPhysicalDeviceExternalSemaphorePropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfoKHR* pExternalSemaphoreInfo, VkExternalSemaphorePropertiesKHR* pExternalSemaphoreProperties) __attribute__ ((weak));
    VkResult gen10_GetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen10_ImportSemaphoreFdKHR(VkDevice device, const VkImportSemaphoreFdInfoKHR* pImportSemaphoreFdInfo) __attribute__ ((weak));
    void gen10_GetPhysicalDeviceExternalFencePropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfoKHR* pExternalFenceInfo, VkExternalFencePropertiesKHR* pExternalFenceProperties) __attribute__ ((weak));
    VkResult gen10_GetFenceFdKHR(VkDevice device, const VkFenceGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen10_ImportFenceFdKHR(VkDevice device, const VkImportFenceFdInfoKHR* pImportFenceFdInfo) __attribute__ ((weak));
    VkResult gen10_BindBufferMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfoKHR* pBindInfos) __attribute__ ((weak));
    VkResult gen10_BindImageMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfoKHR* pBindInfos) __attribute__ ((weak));
    VkResult gen10_CreateDescriptorUpdateTemplateKHR(VkDevice device, const VkDescriptorUpdateTemplateCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplateKHR* pDescriptorUpdateTemplate) __attribute__ ((weak));
    void gen10_DestroyDescriptorUpdateTemplateKHR(VkDevice device, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen10_UpdateDescriptorSetWithTemplateKHR(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const void* pData) __attribute__ ((weak));
    void gen10_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) __attribute__ ((weak));
    VkResult gen10_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen10_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats) __attribute__ ((weak));
    void gen10_GetBufferMemoryRequirements2KHR(VkDevice device, const VkBufferMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void gen10_GetImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void gen10_GetImageSparseMemoryRequirements2KHR(VkDevice device, const VkImageSparseMemoryRequirementsInfo2KHR* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2KHR* pSparseMemoryRequirements) __attribute__ ((weak));
    VkResult gen10_CreateSamplerYcbcrConversionKHR(VkDevice device, const VkSamplerYcbcrConversionCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversionKHR* pYcbcrConversion) __attribute__ ((weak));
    void gen10_DestroySamplerYcbcrConversionKHR(VkDevice device, VkSamplerYcbcrConversionKHR ycbcrConversion, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
#ifdef ANDROID
    VkResult gen10_GetSwapchainGrallocUsageANDROID(VkDevice device, VkFormat format, VkImageUsageFlags imageUsage, int* grallocUsage) __attribute__ ((weak));
#endif // ANDROID
#ifdef ANDROID
    VkResult gen10_AcquireImageANDROID(VkDevice device, VkImage image, int nativeFenceFd, VkSemaphore semaphore, VkFence fence) __attribute__ ((weak));
#endif // ANDROID
#ifdef ANDROID
    VkResult gen10_QueueSignalReleaseImageANDROID(VkQueue queue, uint32_t waitSemaphoreCount, const VkSemaphore* pWaitSemaphores, VkImage image, int* pNativeFenceFd) __attribute__ ((weak));
#endif // ANDROID
    VkResult gen10_CreateDmaBufImageINTEL(VkDevice device, const VkDmaBufImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMem, VkImage* pImage) __attribute__ ((weak));

  const struct anv_dispatch_table gen10_dispatch_table = {
    .vkCreateInstance = gen10_CreateInstance,
    .vkDestroyInstance = gen10_DestroyInstance,
    .vkEnumeratePhysicalDevices = gen10_EnumeratePhysicalDevices,
    .vkGetDeviceProcAddr = gen10_GetDeviceProcAddr,
    .vkGetInstanceProcAddr = gen10_GetInstanceProcAddr,
    .vkGetPhysicalDeviceProperties = gen10_GetPhysicalDeviceProperties,
    .vkGetPhysicalDeviceQueueFamilyProperties = gen10_GetPhysicalDeviceQueueFamilyProperties,
    .vkGetPhysicalDeviceMemoryProperties = gen10_GetPhysicalDeviceMemoryProperties,
    .vkGetPhysicalDeviceFeatures = gen10_GetPhysicalDeviceFeatures,
    .vkGetPhysicalDeviceFormatProperties = gen10_GetPhysicalDeviceFormatProperties,
    .vkGetPhysicalDeviceImageFormatProperties = gen10_GetPhysicalDeviceImageFormatProperties,
    .vkCreateDevice = gen10_CreateDevice,
    .vkDestroyDevice = gen10_DestroyDevice,
    .vkEnumerateInstanceLayerProperties = gen10_EnumerateInstanceLayerProperties,
    .vkEnumerateInstanceExtensionProperties = gen10_EnumerateInstanceExtensionProperties,
    .vkEnumerateDeviceLayerProperties = gen10_EnumerateDeviceLayerProperties,
    .vkEnumerateDeviceExtensionProperties = gen10_EnumerateDeviceExtensionProperties,
    .vkGetDeviceQueue = gen10_GetDeviceQueue,
    .vkQueueSubmit = gen10_QueueSubmit,
    .vkQueueWaitIdle = gen10_QueueWaitIdle,
    .vkDeviceWaitIdle = gen10_DeviceWaitIdle,
    .vkAllocateMemory = gen10_AllocateMemory,
    .vkFreeMemory = gen10_FreeMemory,
    .vkMapMemory = gen10_MapMemory,
    .vkUnmapMemory = gen10_UnmapMemory,
    .vkFlushMappedMemoryRanges = gen10_FlushMappedMemoryRanges,
    .vkInvalidateMappedMemoryRanges = gen10_InvalidateMappedMemoryRanges,
    .vkGetDeviceMemoryCommitment = gen10_GetDeviceMemoryCommitment,
    .vkGetBufferMemoryRequirements = gen10_GetBufferMemoryRequirements,
    .vkBindBufferMemory = gen10_BindBufferMemory,
    .vkGetImageMemoryRequirements = gen10_GetImageMemoryRequirements,
    .vkBindImageMemory = gen10_BindImageMemory,
    .vkGetImageSparseMemoryRequirements = gen10_GetImageSparseMemoryRequirements,
    .vkGetPhysicalDeviceSparseImageFormatProperties = gen10_GetPhysicalDeviceSparseImageFormatProperties,
    .vkQueueBindSparse = gen10_QueueBindSparse,
    .vkCreateFence = gen10_CreateFence,
    .vkDestroyFence = gen10_DestroyFence,
    .vkResetFences = gen10_ResetFences,
    .vkGetFenceStatus = gen10_GetFenceStatus,
    .vkWaitForFences = gen10_WaitForFences,
    .vkCreateSemaphore = gen10_CreateSemaphore,
    .vkDestroySemaphore = gen10_DestroySemaphore,
    .vkCreateEvent = gen10_CreateEvent,
    .vkDestroyEvent = gen10_DestroyEvent,
    .vkGetEventStatus = gen10_GetEventStatus,
    .vkSetEvent = gen10_SetEvent,
    .vkResetEvent = gen10_ResetEvent,
    .vkCreateQueryPool = gen10_CreateQueryPool,
    .vkDestroyQueryPool = gen10_DestroyQueryPool,
    .vkGetQueryPoolResults = gen10_GetQueryPoolResults,
    .vkCreateBuffer = gen10_CreateBuffer,
    .vkDestroyBuffer = gen10_DestroyBuffer,
    .vkCreateBufferView = gen10_CreateBufferView,
    .vkDestroyBufferView = gen10_DestroyBufferView,
    .vkCreateImage = gen10_CreateImage,
    .vkDestroyImage = gen10_DestroyImage,
    .vkGetImageSubresourceLayout = gen10_GetImageSubresourceLayout,
    .vkCreateImageView = gen10_CreateImageView,
    .vkDestroyImageView = gen10_DestroyImageView,
    .vkCreateShaderModule = gen10_CreateShaderModule,
    .vkDestroyShaderModule = gen10_DestroyShaderModule,
    .vkCreatePipelineCache = gen10_CreatePipelineCache,
    .vkDestroyPipelineCache = gen10_DestroyPipelineCache,
    .vkGetPipelineCacheData = gen10_GetPipelineCacheData,
    .vkMergePipelineCaches = gen10_MergePipelineCaches,
    .vkCreateGraphicsPipelines = gen10_CreateGraphicsPipelines,
    .vkCreateComputePipelines = gen10_CreateComputePipelines,
    .vkDestroyPipeline = gen10_DestroyPipeline,
    .vkCreatePipelineLayout = gen10_CreatePipelineLayout,
    .vkDestroyPipelineLayout = gen10_DestroyPipelineLayout,
    .vkCreateSampler = gen10_CreateSampler,
    .vkDestroySampler = gen10_DestroySampler,
    .vkCreateDescriptorSetLayout = gen10_CreateDescriptorSetLayout,
    .vkDestroyDescriptorSetLayout = gen10_DestroyDescriptorSetLayout,
    .vkCreateDescriptorPool = gen10_CreateDescriptorPool,
    .vkDestroyDescriptorPool = gen10_DestroyDescriptorPool,
    .vkResetDescriptorPool = gen10_ResetDescriptorPool,
    .vkAllocateDescriptorSets = gen10_AllocateDescriptorSets,
    .vkFreeDescriptorSets = gen10_FreeDescriptorSets,
    .vkUpdateDescriptorSets = gen10_UpdateDescriptorSets,
    .vkCreateFramebuffer = gen10_CreateFramebuffer,
    .vkDestroyFramebuffer = gen10_DestroyFramebuffer,
    .vkCreateRenderPass = gen10_CreateRenderPass,
    .vkDestroyRenderPass = gen10_DestroyRenderPass,
    .vkGetRenderAreaGranularity = gen10_GetRenderAreaGranularity,
    .vkCreateCommandPool = gen10_CreateCommandPool,
    .vkDestroyCommandPool = gen10_DestroyCommandPool,
    .vkResetCommandPool = gen10_ResetCommandPool,
    .vkAllocateCommandBuffers = gen10_AllocateCommandBuffers,
    .vkFreeCommandBuffers = gen10_FreeCommandBuffers,
    .vkBeginCommandBuffer = gen10_BeginCommandBuffer,
    .vkEndCommandBuffer = gen10_EndCommandBuffer,
    .vkResetCommandBuffer = gen10_ResetCommandBuffer,
    .vkCmdBindPipeline = gen10_CmdBindPipeline,
    .vkCmdSetViewport = gen10_CmdSetViewport,
    .vkCmdSetScissor = gen10_CmdSetScissor,
    .vkCmdSetLineWidth = gen10_CmdSetLineWidth,
    .vkCmdSetDepthBias = gen10_CmdSetDepthBias,
    .vkCmdSetBlendConstants = gen10_CmdSetBlendConstants,
    .vkCmdSetDepthBounds = gen10_CmdSetDepthBounds,
    .vkCmdSetStencilCompareMask = gen10_CmdSetStencilCompareMask,
    .vkCmdSetStencilWriteMask = gen10_CmdSetStencilWriteMask,
    .vkCmdSetStencilReference = gen10_CmdSetStencilReference,
    .vkCmdBindDescriptorSets = gen10_CmdBindDescriptorSets,
    .vkCmdBindIndexBuffer = gen10_CmdBindIndexBuffer,
    .vkCmdBindVertexBuffers = gen10_CmdBindVertexBuffers,
    .vkCmdDraw = gen10_CmdDraw,
    .vkCmdDrawIndexed = gen10_CmdDrawIndexed,
    .vkCmdDrawIndirect = gen10_CmdDrawIndirect,
    .vkCmdDrawIndexedIndirect = gen10_CmdDrawIndexedIndirect,
    .vkCmdDispatch = gen10_CmdDispatch,
    .vkCmdDispatchIndirect = gen10_CmdDispatchIndirect,
    .vkCmdCopyBuffer = gen10_CmdCopyBuffer,
    .vkCmdCopyImage = gen10_CmdCopyImage,
    .vkCmdBlitImage = gen10_CmdBlitImage,
    .vkCmdCopyBufferToImage = gen10_CmdCopyBufferToImage,
    .vkCmdCopyImageToBuffer = gen10_CmdCopyImageToBuffer,
    .vkCmdUpdateBuffer = gen10_CmdUpdateBuffer,
    .vkCmdFillBuffer = gen10_CmdFillBuffer,
    .vkCmdClearColorImage = gen10_CmdClearColorImage,
    .vkCmdClearDepthStencilImage = gen10_CmdClearDepthStencilImage,
    .vkCmdClearAttachments = gen10_CmdClearAttachments,
    .vkCmdResolveImage = gen10_CmdResolveImage,
    .vkCmdSetEvent = gen10_CmdSetEvent,
    .vkCmdResetEvent = gen10_CmdResetEvent,
    .vkCmdWaitEvents = gen10_CmdWaitEvents,
    .vkCmdPipelineBarrier = gen10_CmdPipelineBarrier,
    .vkCmdBeginQuery = gen10_CmdBeginQuery,
    .vkCmdEndQuery = gen10_CmdEndQuery,
    .vkCmdResetQueryPool = gen10_CmdResetQueryPool,
    .vkCmdWriteTimestamp = gen10_CmdWriteTimestamp,
    .vkCmdCopyQueryPoolResults = gen10_CmdCopyQueryPoolResults,
    .vkCmdPushConstants = gen10_CmdPushConstants,
    .vkCmdBeginRenderPass = gen10_CmdBeginRenderPass,
    .vkCmdNextSubpass = gen10_CmdNextSubpass,
    .vkCmdEndRenderPass = gen10_CmdEndRenderPass,
    .vkCmdExecuteCommands = gen10_CmdExecuteCommands,
    .vkDestroySurfaceKHR = gen10_DestroySurfaceKHR,
    .vkGetPhysicalDeviceSurfaceSupportKHR = gen10_GetPhysicalDeviceSurfaceSupportKHR,
    .vkGetPhysicalDeviceSurfaceCapabilitiesKHR = gen10_GetPhysicalDeviceSurfaceCapabilitiesKHR,
    .vkGetPhysicalDeviceSurfaceFormatsKHR = gen10_GetPhysicalDeviceSurfaceFormatsKHR,
    .vkGetPhysicalDeviceSurfacePresentModesKHR = gen10_GetPhysicalDeviceSurfacePresentModesKHR,
    .vkCreateSwapchainKHR = gen10_CreateSwapchainKHR,
    .vkDestroySwapchainKHR = gen10_DestroySwapchainKHR,
    .vkGetSwapchainImagesKHR = gen10_GetSwapchainImagesKHR,
    .vkAcquireNextImageKHR = gen10_AcquireNextImageKHR,
    .vkQueuePresentKHR = gen10_QueuePresentKHR,
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .vkCreateWaylandSurfaceKHR = gen10_CreateWaylandSurfaceKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .vkGetPhysicalDeviceWaylandPresentationSupportKHR = gen10_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .vkCreateXlibSurfaceKHR = gen10_CreateXlibSurfaceKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .vkGetPhysicalDeviceXlibPresentationSupportKHR = gen10_GetPhysicalDeviceXlibPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .vkCreateXcbSurfaceKHR = gen10_CreateXcbSurfaceKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .vkGetPhysicalDeviceXcbPresentationSupportKHR = gen10_GetPhysicalDeviceXcbPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
    .vkCreateDebugReportCallbackEXT = gen10_CreateDebugReportCallbackEXT,
    .vkDestroyDebugReportCallbackEXT = gen10_DestroyDebugReportCallbackEXT,
    .vkDebugReportMessageEXT = gen10_DebugReportMessageEXT,
    .vkGetPhysicalDeviceFeatures2KHR = gen10_GetPhysicalDeviceFeatures2KHR,
    .vkGetPhysicalDeviceProperties2KHR = gen10_GetPhysicalDeviceProperties2KHR,
    .vkGetPhysicalDeviceFormatProperties2KHR = gen10_GetPhysicalDeviceFormatProperties2KHR,
    .vkGetPhysicalDeviceImageFormatProperties2KHR = gen10_GetPhysicalDeviceImageFormatProperties2KHR,
    .vkGetPhysicalDeviceQueueFamilyProperties2KHR = gen10_GetPhysicalDeviceQueueFamilyProperties2KHR,
    .vkGetPhysicalDeviceMemoryProperties2KHR = gen10_GetPhysicalDeviceMemoryProperties2KHR,
    .vkGetPhysicalDeviceSparseImageFormatProperties2KHR = gen10_GetPhysicalDeviceSparseImageFormatProperties2KHR,
    .vkCmdPushDescriptorSetKHR = gen10_CmdPushDescriptorSetKHR,
    .vkTrimCommandPoolKHR = gen10_TrimCommandPoolKHR,
    .vkGetPhysicalDeviceExternalBufferPropertiesKHR = gen10_GetPhysicalDeviceExternalBufferPropertiesKHR,
    .vkGetMemoryFdKHR = gen10_GetMemoryFdKHR,
    .vkGetMemoryFdPropertiesKHR = gen10_GetMemoryFdPropertiesKHR,
    .vkGetPhysicalDeviceExternalSemaphorePropertiesKHR = gen10_GetPhysicalDeviceExternalSemaphorePropertiesKHR,
    .vkGetSemaphoreFdKHR = gen10_GetSemaphoreFdKHR,
    .vkImportSemaphoreFdKHR = gen10_ImportSemaphoreFdKHR,
    .vkGetPhysicalDeviceExternalFencePropertiesKHR = gen10_GetPhysicalDeviceExternalFencePropertiesKHR,
    .vkGetFenceFdKHR = gen10_GetFenceFdKHR,
    .vkImportFenceFdKHR = gen10_ImportFenceFdKHR,
    .vkBindBufferMemory2KHR = gen10_BindBufferMemory2KHR,
    .vkBindImageMemory2KHR = gen10_BindImageMemory2KHR,
    .vkCreateDescriptorUpdateTemplateKHR = gen10_CreateDescriptorUpdateTemplateKHR,
    .vkDestroyDescriptorUpdateTemplateKHR = gen10_DestroyDescriptorUpdateTemplateKHR,
    .vkUpdateDescriptorSetWithTemplateKHR = gen10_UpdateDescriptorSetWithTemplateKHR,
    .vkCmdPushDescriptorSetWithTemplateKHR = gen10_CmdPushDescriptorSetWithTemplateKHR,
    .vkGetPhysicalDeviceSurfaceCapabilities2KHR = gen10_GetPhysicalDeviceSurfaceCapabilities2KHR,
    .vkGetPhysicalDeviceSurfaceFormats2KHR = gen10_GetPhysicalDeviceSurfaceFormats2KHR,
    .vkGetBufferMemoryRequirements2KHR = gen10_GetBufferMemoryRequirements2KHR,
    .vkGetImageMemoryRequirements2KHR = gen10_GetImageMemoryRequirements2KHR,
    .vkGetImageSparseMemoryRequirements2KHR = gen10_GetImageSparseMemoryRequirements2KHR,
    .vkCreateSamplerYcbcrConversionKHR = gen10_CreateSamplerYcbcrConversionKHR,
    .vkDestroySamplerYcbcrConversionKHR = gen10_DestroySamplerYcbcrConversionKHR,
#ifdef ANDROID
    .vkGetSwapchainGrallocUsageANDROID = gen10_GetSwapchainGrallocUsageANDROID,
#endif // ANDROID
#ifdef ANDROID
    .vkAcquireImageANDROID = gen10_AcquireImageANDROID,
#endif // ANDROID
#ifdef ANDROID
    .vkQueueSignalReleaseImageANDROID = gen10_QueueSignalReleaseImageANDROID,
#endif // ANDROID
    .vkCreateDmaBufImageINTEL = gen10_CreateDmaBufImageINTEL,
  };


/** Trampoline entrypoints for all device functions */

              static PFN_vkVoidFunction
  anv_tramp_GetDeviceProcAddr(VkDevice device, const char* pName)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetDeviceProcAddr(device, pName);
  }
                                  static void
  anv_tramp_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroyDevice(device, pAllocator);
  }
                  static void
  anv_tramp_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
  }
          static VkResult
  anv_tramp_DeviceWaitIdle(VkDevice device)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDeviceWaitIdle(device);
  }
  static VkResult
  anv_tramp_AllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkAllocateMemory(device, pAllocateInfo, pAllocator, pMemory);
  }
  static void
  anv_tramp_FreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkFreeMemory(device, memory, pAllocator);
  }
  static VkResult
  anv_tramp_MapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkMapMemory(device, memory, offset, size, flags, ppData);
  }
  static void
  anv_tramp_UnmapMemory(VkDevice device, VkDeviceMemory memory)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkUnmapMemory(device, memory);
  }
  static VkResult
  anv_tramp_FlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkFlushMappedMemoryRanges(device, memoryRangeCount, pMemoryRanges);
  }
  static VkResult
  anv_tramp_InvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkInvalidateMappedMemoryRanges(device, memoryRangeCount, pMemoryRanges);
  }
  static void
  anv_tramp_GetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory, VkDeviceSize* pCommittedMemoryInBytes)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetDeviceMemoryCommitment(device, memory, pCommittedMemoryInBytes);
  }
  static void
  anv_tramp_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetBufferMemoryRequirements(device, buffer, pMemoryRequirements);
  }
  static VkResult
  anv_tramp_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkBindBufferMemory(device, buffer, memory, memoryOffset);
  }
  static void
  anv_tramp_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetImageMemoryRequirements(device, image, pMemoryRequirements);
  }
  static VkResult
  anv_tramp_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkBindImageMemory(device, image, memory, memoryOffset);
  }
  static void
  anv_tramp_GetImageSparseMemoryRequirements(VkDevice device, VkImage image, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements* pSparseMemoryRequirements)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetImageSparseMemoryRequirements(device, image, pSparseMemoryRequirementCount, pSparseMemoryRequirements);
  }
          static VkResult
  anv_tramp_CreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateFence(device, pCreateInfo, pAllocator, pFence);
  }
  static void
  anv_tramp_DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroyFence(device, fence, pAllocator);
  }
  static VkResult
  anv_tramp_ResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkResetFences(device, fenceCount, pFences);
  }
  static VkResult
  anv_tramp_GetFenceStatus(VkDevice device, VkFence fence)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetFenceStatus(device, fence);
  }
  static VkResult
  anv_tramp_WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkWaitForFences(device, fenceCount, pFences, waitAll, timeout);
  }
  static VkResult
  anv_tramp_CreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateSemaphore(device, pCreateInfo, pAllocator, pSemaphore);
  }
  static void
  anv_tramp_DestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroySemaphore(device, semaphore, pAllocator);
  }
  static VkResult
  anv_tramp_CreateEvent(VkDevice device, const VkEventCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkEvent* pEvent)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateEvent(device, pCreateInfo, pAllocator, pEvent);
  }
  static void
  anv_tramp_DestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroyEvent(device, event, pAllocator);
  }
  static VkResult
  anv_tramp_GetEventStatus(VkDevice device, VkEvent event)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetEventStatus(device, event);
  }
  static VkResult
  anv_tramp_SetEvent(VkDevice device, VkEvent event)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkSetEvent(device, event);
  }
  static VkResult
  anv_tramp_ResetEvent(VkDevice device, VkEvent event)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkResetEvent(device, event);
  }
  static VkResult
  anv_tramp_CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateQueryPool(device, pCreateInfo, pAllocator, pQueryPool);
  }
  static void
  anv_tramp_DestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroyQueryPool(device, queryPool, pAllocator);
  }
  static VkResult
  anv_tramp_GetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* pData, VkDeviceSize stride, VkQueryResultFlags flags)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetQueryPoolResults(device, queryPool, firstQuery, queryCount, dataSize, pData, stride, flags);
  }
  static VkResult
  anv_tramp_CreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateBuffer(device, pCreateInfo, pAllocator, pBuffer);
  }
  static void
  anv_tramp_DestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroyBuffer(device, buffer, pAllocator);
  }
  static VkResult
  anv_tramp_CreateBufferView(VkDevice device, const VkBufferViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBufferView* pView)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateBufferView(device, pCreateInfo, pAllocator, pView);
  }
  static void
  anv_tramp_DestroyBufferView(VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroyBufferView(device, bufferView, pAllocator);
  }
  static VkResult
  anv_tramp_CreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateImage(device, pCreateInfo, pAllocator, pImage);
  }
  static void
  anv_tramp_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroyImage(device, image, pAllocator);
  }
  static void
  anv_tramp_GetImageSubresourceLayout(VkDevice device, VkImage image, const VkImageSubresource* pSubresource, VkSubresourceLayout* pLayout)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetImageSubresourceLayout(device, image, pSubresource, pLayout);
  }
  static VkResult
  anv_tramp_CreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateImageView(device, pCreateInfo, pAllocator, pView);
  }
  static void
  anv_tramp_DestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroyImageView(device, imageView, pAllocator);
  }
  static VkResult
  anv_tramp_CreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateShaderModule(device, pCreateInfo, pAllocator, pShaderModule);
  }
  static void
  anv_tramp_DestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroyShaderModule(device, shaderModule, pAllocator);
  }
  static VkResult
  anv_tramp_CreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineCache* pPipelineCache)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreatePipelineCache(device, pCreateInfo, pAllocator, pPipelineCache);
  }
  static void
  anv_tramp_DestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroyPipelineCache(device, pipelineCache, pAllocator);
  }
  static VkResult
  anv_tramp_GetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, size_t* pDataSize, void* pData)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetPipelineCacheData(device, pipelineCache, pDataSize, pData);
  }
  static VkResult
  anv_tramp_MergePipelineCaches(VkDevice device, VkPipelineCache dstCache, uint32_t srcCacheCount, const VkPipelineCache* pSrcCaches)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkMergePipelineCaches(device, dstCache, srcCacheCount, pSrcCaches);
  }
  static VkResult
  anv_tramp_CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
  }
  static VkResult
  anv_tramp_CreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateComputePipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
  }
  static void
  anv_tramp_DestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroyPipeline(device, pipeline, pAllocator);
  }
  static VkResult
  anv_tramp_CreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreatePipelineLayout(device, pCreateInfo, pAllocator, pPipelineLayout);
  }
  static void
  anv_tramp_DestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroyPipelineLayout(device, pipelineLayout, pAllocator);
  }
  static VkResult
  anv_tramp_CreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSampler* pSampler)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateSampler(device, pCreateInfo, pAllocator, pSampler);
  }
  static void
  anv_tramp_DestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroySampler(device, sampler, pAllocator);
  }
  static VkResult
  anv_tramp_CreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateDescriptorSetLayout(device, pCreateInfo, pAllocator, pSetLayout);
  }
  static void
  anv_tramp_DestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroyDescriptorSetLayout(device, descriptorSetLayout, pAllocator);
  }
  static VkResult
  anv_tramp_CreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateDescriptorPool(device, pCreateInfo, pAllocator, pDescriptorPool);
  }
  static void
  anv_tramp_DestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroyDescriptorPool(device, descriptorPool, pAllocator);
  }
  static VkResult
  anv_tramp_ResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkResetDescriptorPool(device, descriptorPool, flags);
  }
  static VkResult
  anv_tramp_AllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkAllocateDescriptorSets(device, pAllocateInfo, pDescriptorSets);
  }
  static VkResult
  anv_tramp_FreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkFreeDescriptorSets(device, descriptorPool, descriptorSetCount, pDescriptorSets);
  }
  static void
  anv_tramp_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkUpdateDescriptorSets(device, descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies);
  }
  static VkResult
  anv_tramp_CreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFramebuffer* pFramebuffer)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateFramebuffer(device, pCreateInfo, pAllocator, pFramebuffer);
  }
  static void
  anv_tramp_DestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroyFramebuffer(device, framebuffer, pAllocator);
  }
  static VkResult
  anv_tramp_CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
  }
  static void
  anv_tramp_DestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroyRenderPass(device, renderPass, pAllocator);
  }
  static void
  anv_tramp_GetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass, VkExtent2D* pGranularity)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetRenderAreaGranularity(device, renderPass, pGranularity);
  }
  static VkResult
  anv_tramp_CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateCommandPool(device, pCreateInfo, pAllocator, pCommandPool);
  }
  static void
  anv_tramp_DestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroyCommandPool(device, commandPool, pAllocator);
  }
  static VkResult
  anv_tramp_ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkResetCommandPool(device, commandPool, flags);
  }
  static VkResult
  anv_tramp_AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkAllocateCommandBuffers(device, pAllocateInfo, pCommandBuffers);
  }
  static void
  anv_tramp_FreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkFreeCommandBuffers(device, commandPool, commandBufferCount, pCommandBuffers);
  }
  static VkResult
  anv_tramp_BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkBeginCommandBuffer(commandBuffer, pBeginInfo);
  }
  static VkResult
  anv_tramp_EndCommandBuffer(VkCommandBuffer commandBuffer)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkEndCommandBuffer(commandBuffer);
  }
  static VkResult
  anv_tramp_ResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkResetCommandBuffer(commandBuffer, flags);
  }
  static void
  anv_tramp_CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
  }
  static void
  anv_tramp_CmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdSetViewport(commandBuffer, firstViewport, viewportCount, pViewports);
  }
  static void
  anv_tramp_CmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdSetScissor(commandBuffer, firstScissor, scissorCount, pScissors);
  }
  static void
  anv_tramp_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdSetLineWidth(commandBuffer, lineWidth);
  }
  static void
  anv_tramp_CmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdSetDepthBias(commandBuffer, depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor);
  }
  static void
  anv_tramp_CmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4])
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdSetBlendConstants(commandBuffer, blendConstants);
  }
  static void
  anv_tramp_CmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdSetDepthBounds(commandBuffer, minDepthBounds, maxDepthBounds);
  }
  static void
  anv_tramp_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t compareMask)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdSetStencilCompareMask(commandBuffer, faceMask, compareMask);
  }
  static void
  anv_tramp_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t writeMask)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdSetStencilWriteMask(commandBuffer, faceMask, writeMask);
  }
  static void
  anv_tramp_CmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdSetStencilReference(commandBuffer, faceMask, reference);
  }
  static void
  anv_tramp_CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdBindDescriptorSets(commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
  }
  static void
  anv_tramp_CmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdBindIndexBuffer(commandBuffer, buffer, offset, indexType);
  }
  static void
  anv_tramp_CmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdBindVertexBuffers(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets);
  }
  static void
  anv_tramp_CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
  }
  static void
  anv_tramp_CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
  }
  static void
  anv_tramp_CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdDrawIndirect(commandBuffer, buffer, offset, drawCount, stride);
  }
  static void
  anv_tramp_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdDrawIndexedIndirect(commandBuffer, buffer, offset, drawCount, stride);
  }
  static void
  anv_tramp_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
  }
  static void
  anv_tramp_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdDispatchIndirect(commandBuffer, buffer, offset);
  }
  static void
  anv_tramp_CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, regionCount, pRegions);
  }
  static void
  anv_tramp_CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdCopyImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
  }
  static void
  anv_tramp_CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdBlitImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions, filter);
  }
  static void
  anv_tramp_CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdCopyBufferToImage(commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
  }
  static void
  anv_tramp_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdCopyImageToBuffer(commandBuffer, srcImage, srcImageLayout, dstBuffer, regionCount, pRegions);
  }
  static void
  anv_tramp_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* pData)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdUpdateBuffer(commandBuffer, dstBuffer, dstOffset, dataSize, pData);
  }
  static void
  anv_tramp_CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdFillBuffer(commandBuffer, dstBuffer, dstOffset, size, data);
  }
  static void
  anv_tramp_CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, uint32_t rangeCount, const VkImageSubresourceRange* pRanges)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdClearColorImage(commandBuffer, image, imageLayout, pColor, rangeCount, pRanges);
  }
  static void
  anv_tramp_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange* pRanges)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdClearDepthStencilImage(commandBuffer, image, imageLayout, pDepthStencil, rangeCount, pRanges);
  }
  static void
  anv_tramp_CmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment* pAttachments, uint32_t rectCount, const VkClearRect* pRects)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdClearAttachments(commandBuffer, attachmentCount, pAttachments, rectCount, pRects);
  }
  static void
  anv_tramp_CmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageResolve* pRegions)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdResolveImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
  }
  static void
  anv_tramp_CmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdSetEvent(commandBuffer, event, stageMask);
  }
  static void
  anv_tramp_CmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdResetEvent(commandBuffer, event, stageMask);
  }
  static void
  anv_tramp_CmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdWaitEvents(commandBuffer, eventCount, pEvents, srcStageMask, dstStageMask, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
  }
  static void
  anv_tramp_CmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
  }
  static void
  anv_tramp_CmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdBeginQuery(commandBuffer, queryPool, query, flags);
  }
  static void
  anv_tramp_CmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdEndQuery(commandBuffer, queryPool, query);
  }
  static void
  anv_tramp_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdResetQueryPool(commandBuffer, queryPool, firstQuery, queryCount);
  }
  static void
  anv_tramp_CmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdWriteTimestamp(commandBuffer, pipelineStage, queryPool, query);
  }
  static void
  anv_tramp_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdCopyQueryPoolResults(commandBuffer, queryPool, firstQuery, queryCount, dstBuffer, dstOffset, stride, flags);
  }
  static void
  anv_tramp_CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdPushConstants(commandBuffer, layout, stageFlags, offset, size, pValues);
  }
  static void
  anv_tramp_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
  }
  static void
  anv_tramp_CmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdNextSubpass(commandBuffer, contents);
  }
  static void
  anv_tramp_CmdEndRenderPass(VkCommandBuffer commandBuffer)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdEndRenderPass(commandBuffer);
  }
  static void
  anv_tramp_CmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdExecuteCommands(commandBuffer, commandBufferCount, pCommandBuffers);
  }
                      static VkResult
  anv_tramp_CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
  }
  static void
  anv_tramp_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroySwapchainKHR(device, swapchain, pAllocator);
  }
  static VkResult
  anv_tramp_GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetSwapchainImagesKHR(device, swapchain, pSwapchainImageCount, pSwapchainImages);
  }
  static VkResult
  anv_tramp_AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);
  }
                                                                      static void
  anv_tramp_CmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdPushDescriptorSetKHR(commandBuffer, pipelineBindPoint, layout, set, descriptorWriteCount, pDescriptorWrites);
  }
  static void
  anv_tramp_TrimCommandPoolKHR(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlagsKHR flags)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkTrimCommandPoolKHR(device, commandPool, flags);
  }
      static VkResult
  anv_tramp_GetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetMemoryFdKHR(device, pGetFdInfo, pFd);
  }
  static VkResult
  anv_tramp_GetMemoryFdPropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBitsKHR handleType, int fd, VkMemoryFdPropertiesKHR* pMemoryFdProperties)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetMemoryFdPropertiesKHR(device, handleType, fd, pMemoryFdProperties);
  }
      static VkResult
  anv_tramp_GetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR* pGetFdInfo, int* pFd)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetSemaphoreFdKHR(device, pGetFdInfo, pFd);
  }
  static VkResult
  anv_tramp_ImportSemaphoreFdKHR(VkDevice device, const VkImportSemaphoreFdInfoKHR* pImportSemaphoreFdInfo)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkImportSemaphoreFdKHR(device, pImportSemaphoreFdInfo);
  }
      static VkResult
  anv_tramp_GetFenceFdKHR(VkDevice device, const VkFenceGetFdInfoKHR* pGetFdInfo, int* pFd)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetFenceFdKHR(device, pGetFdInfo, pFd);
  }
  static VkResult
  anv_tramp_ImportFenceFdKHR(VkDevice device, const VkImportFenceFdInfoKHR* pImportFenceFdInfo)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkImportFenceFdKHR(device, pImportFenceFdInfo);
  }
  static VkResult
  anv_tramp_BindBufferMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfoKHR* pBindInfos)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkBindBufferMemory2KHR(device, bindInfoCount, pBindInfos);
  }
  static VkResult
  anv_tramp_BindImageMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfoKHR* pBindInfos)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkBindImageMemory2KHR(device, bindInfoCount, pBindInfos);
  }
  static VkResult
  anv_tramp_CreateDescriptorUpdateTemplateKHR(VkDevice device, const VkDescriptorUpdateTemplateCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplateKHR* pDescriptorUpdateTemplate)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateDescriptorUpdateTemplateKHR(device, pCreateInfo, pAllocator, pDescriptorUpdateTemplate);
  }
  static void
  anv_tramp_DestroyDescriptorUpdateTemplateKHR(VkDevice device, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroyDescriptorUpdateTemplateKHR(device, descriptorUpdateTemplate, pAllocator);
  }
  static void
  anv_tramp_UpdateDescriptorSetWithTemplateKHR(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const void* pData)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkUpdateDescriptorSetWithTemplateKHR(device, descriptorSet, descriptorUpdateTemplate, pData);
  }
  static void
  anv_tramp_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, descriptorUpdateTemplate, layout, set, pData);
  }
          static void
  anv_tramp_GetBufferMemoryRequirements2KHR(VkDevice device, const VkBufferMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetBufferMemoryRequirements2KHR(device, pInfo, pMemoryRequirements);
  }
  static void
  anv_tramp_GetImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetImageMemoryRequirements2KHR(device, pInfo, pMemoryRequirements);
  }
  static void
  anv_tramp_GetImageSparseMemoryRequirements2KHR(VkDevice device, const VkImageSparseMemoryRequirementsInfo2KHR* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2KHR* pSparseMemoryRequirements)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetImageSparseMemoryRequirements2KHR(device, pInfo, pSparseMemoryRequirementCount, pSparseMemoryRequirements);
  }
  static VkResult
  anv_tramp_CreateSamplerYcbcrConversionKHR(VkDevice device, const VkSamplerYcbcrConversionCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversionKHR* pYcbcrConversion)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateSamplerYcbcrConversionKHR(device, pCreateInfo, pAllocator, pYcbcrConversion);
  }
  static void
  anv_tramp_DestroySamplerYcbcrConversionKHR(VkDevice device, VkSamplerYcbcrConversionKHR ycbcrConversion, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroySamplerYcbcrConversionKHR(device, ycbcrConversion, pAllocator);
  }
#ifdef ANDROID
  static VkResult
  anv_tramp_GetSwapchainGrallocUsageANDROID(VkDevice device, VkFormat format, VkImageUsageFlags imageUsage, int* grallocUsage)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetSwapchainGrallocUsageANDROID(device, format, imageUsage, grallocUsage);
  }
#endif // ANDROID
#ifdef ANDROID
  static VkResult
  anv_tramp_AcquireImageANDROID(VkDevice device, VkImage image, int nativeFenceFd, VkSemaphore semaphore, VkFence fence)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkAcquireImageANDROID(device, image, nativeFenceFd, semaphore, fence);
  }
#endif // ANDROID
      static VkResult
  anv_tramp_CreateDmaBufImageINTEL(VkDevice device, const VkDmaBufImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMem, VkImage* pImage)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateDmaBufImageINTEL(device, pCreateInfo, pAllocator, pMem, pImage);
  }

const struct anv_dispatch_table anv_tramp_dispatch_table = {
                .vkGetDeviceProcAddr = anv_tramp_GetDeviceProcAddr,
                                    .vkDestroyDevice = anv_tramp_DestroyDevice,
                    .vkGetDeviceQueue = anv_tramp_GetDeviceQueue,
            .vkDeviceWaitIdle = anv_tramp_DeviceWaitIdle,
    .vkAllocateMemory = anv_tramp_AllocateMemory,
    .vkFreeMemory = anv_tramp_FreeMemory,
    .vkMapMemory = anv_tramp_MapMemory,
    .vkUnmapMemory = anv_tramp_UnmapMemory,
    .vkFlushMappedMemoryRanges = anv_tramp_FlushMappedMemoryRanges,
    .vkInvalidateMappedMemoryRanges = anv_tramp_InvalidateMappedMemoryRanges,
    .vkGetDeviceMemoryCommitment = anv_tramp_GetDeviceMemoryCommitment,
    .vkGetBufferMemoryRequirements = anv_tramp_GetBufferMemoryRequirements,
    .vkBindBufferMemory = anv_tramp_BindBufferMemory,
    .vkGetImageMemoryRequirements = anv_tramp_GetImageMemoryRequirements,
    .vkBindImageMemory = anv_tramp_BindImageMemory,
    .vkGetImageSparseMemoryRequirements = anv_tramp_GetImageSparseMemoryRequirements,
            .vkCreateFence = anv_tramp_CreateFence,
    .vkDestroyFence = anv_tramp_DestroyFence,
    .vkResetFences = anv_tramp_ResetFences,
    .vkGetFenceStatus = anv_tramp_GetFenceStatus,
    .vkWaitForFences = anv_tramp_WaitForFences,
    .vkCreateSemaphore = anv_tramp_CreateSemaphore,
    .vkDestroySemaphore = anv_tramp_DestroySemaphore,
    .vkCreateEvent = anv_tramp_CreateEvent,
    .vkDestroyEvent = anv_tramp_DestroyEvent,
    .vkGetEventStatus = anv_tramp_GetEventStatus,
    .vkSetEvent = anv_tramp_SetEvent,
    .vkResetEvent = anv_tramp_ResetEvent,
    .vkCreateQueryPool = anv_tramp_CreateQueryPool,
    .vkDestroyQueryPool = anv_tramp_DestroyQueryPool,
    .vkGetQueryPoolResults = anv_tramp_GetQueryPoolResults,
    .vkCreateBuffer = anv_tramp_CreateBuffer,
    .vkDestroyBuffer = anv_tramp_DestroyBuffer,
    .vkCreateBufferView = anv_tramp_CreateBufferView,
    .vkDestroyBufferView = anv_tramp_DestroyBufferView,
    .vkCreateImage = anv_tramp_CreateImage,
    .vkDestroyImage = anv_tramp_DestroyImage,
    .vkGetImageSubresourceLayout = anv_tramp_GetImageSubresourceLayout,
    .vkCreateImageView = anv_tramp_CreateImageView,
    .vkDestroyImageView = anv_tramp_DestroyImageView,
    .vkCreateShaderModule = anv_tramp_CreateShaderModule,
    .vkDestroyShaderModule = anv_tramp_DestroyShaderModule,
    .vkCreatePipelineCache = anv_tramp_CreatePipelineCache,
    .vkDestroyPipelineCache = anv_tramp_DestroyPipelineCache,
    .vkGetPipelineCacheData = anv_tramp_GetPipelineCacheData,
    .vkMergePipelineCaches = anv_tramp_MergePipelineCaches,
    .vkCreateGraphicsPipelines = anv_tramp_CreateGraphicsPipelines,
    .vkCreateComputePipelines = anv_tramp_CreateComputePipelines,
    .vkDestroyPipeline = anv_tramp_DestroyPipeline,
    .vkCreatePipelineLayout = anv_tramp_CreatePipelineLayout,
    .vkDestroyPipelineLayout = anv_tramp_DestroyPipelineLayout,
    .vkCreateSampler = anv_tramp_CreateSampler,
    .vkDestroySampler = anv_tramp_DestroySampler,
    .vkCreateDescriptorSetLayout = anv_tramp_CreateDescriptorSetLayout,
    .vkDestroyDescriptorSetLayout = anv_tramp_DestroyDescriptorSetLayout,
    .vkCreateDescriptorPool = anv_tramp_CreateDescriptorPool,
    .vkDestroyDescriptorPool = anv_tramp_DestroyDescriptorPool,
    .vkResetDescriptorPool = anv_tramp_ResetDescriptorPool,
    .vkAllocateDescriptorSets = anv_tramp_AllocateDescriptorSets,
    .vkFreeDescriptorSets = anv_tramp_FreeDescriptorSets,
    .vkUpdateDescriptorSets = anv_tramp_UpdateDescriptorSets,
    .vkCreateFramebuffer = anv_tramp_CreateFramebuffer,
    .vkDestroyFramebuffer = anv_tramp_DestroyFramebuffer,
    .vkCreateRenderPass = anv_tramp_CreateRenderPass,
    .vkDestroyRenderPass = anv_tramp_DestroyRenderPass,
    .vkGetRenderAreaGranularity = anv_tramp_GetRenderAreaGranularity,
    .vkCreateCommandPool = anv_tramp_CreateCommandPool,
    .vkDestroyCommandPool = anv_tramp_DestroyCommandPool,
    .vkResetCommandPool = anv_tramp_ResetCommandPool,
    .vkAllocateCommandBuffers = anv_tramp_AllocateCommandBuffers,
    .vkFreeCommandBuffers = anv_tramp_FreeCommandBuffers,
    .vkBeginCommandBuffer = anv_tramp_BeginCommandBuffer,
    .vkEndCommandBuffer = anv_tramp_EndCommandBuffer,
    .vkResetCommandBuffer = anv_tramp_ResetCommandBuffer,
    .vkCmdBindPipeline = anv_tramp_CmdBindPipeline,
    .vkCmdSetViewport = anv_tramp_CmdSetViewport,
    .vkCmdSetScissor = anv_tramp_CmdSetScissor,
    .vkCmdSetLineWidth = anv_tramp_CmdSetLineWidth,
    .vkCmdSetDepthBias = anv_tramp_CmdSetDepthBias,
    .vkCmdSetBlendConstants = anv_tramp_CmdSetBlendConstants,
    .vkCmdSetDepthBounds = anv_tramp_CmdSetDepthBounds,
    .vkCmdSetStencilCompareMask = anv_tramp_CmdSetStencilCompareMask,
    .vkCmdSetStencilWriteMask = anv_tramp_CmdSetStencilWriteMask,
    .vkCmdSetStencilReference = anv_tramp_CmdSetStencilReference,
    .vkCmdBindDescriptorSets = anv_tramp_CmdBindDescriptorSets,
    .vkCmdBindIndexBuffer = anv_tramp_CmdBindIndexBuffer,
    .vkCmdBindVertexBuffers = anv_tramp_CmdBindVertexBuffers,
    .vkCmdDraw = anv_tramp_CmdDraw,
    .vkCmdDrawIndexed = anv_tramp_CmdDrawIndexed,
    .vkCmdDrawIndirect = anv_tramp_CmdDrawIndirect,
    .vkCmdDrawIndexedIndirect = anv_tramp_CmdDrawIndexedIndirect,
    .vkCmdDispatch = anv_tramp_CmdDispatch,
    .vkCmdDispatchIndirect = anv_tramp_CmdDispatchIndirect,
    .vkCmdCopyBuffer = anv_tramp_CmdCopyBuffer,
    .vkCmdCopyImage = anv_tramp_CmdCopyImage,
    .vkCmdBlitImage = anv_tramp_CmdBlitImage,
    .vkCmdCopyBufferToImage = anv_tramp_CmdCopyBufferToImage,
    .vkCmdCopyImageToBuffer = anv_tramp_CmdCopyImageToBuffer,
    .vkCmdUpdateBuffer = anv_tramp_CmdUpdateBuffer,
    .vkCmdFillBuffer = anv_tramp_CmdFillBuffer,
    .vkCmdClearColorImage = anv_tramp_CmdClearColorImage,
    .vkCmdClearDepthStencilImage = anv_tramp_CmdClearDepthStencilImage,
    .vkCmdClearAttachments = anv_tramp_CmdClearAttachments,
    .vkCmdResolveImage = anv_tramp_CmdResolveImage,
    .vkCmdSetEvent = anv_tramp_CmdSetEvent,
    .vkCmdResetEvent = anv_tramp_CmdResetEvent,
    .vkCmdWaitEvents = anv_tramp_CmdWaitEvents,
    .vkCmdPipelineBarrier = anv_tramp_CmdPipelineBarrier,
    .vkCmdBeginQuery = anv_tramp_CmdBeginQuery,
    .vkCmdEndQuery = anv_tramp_CmdEndQuery,
    .vkCmdResetQueryPool = anv_tramp_CmdResetQueryPool,
    .vkCmdWriteTimestamp = anv_tramp_CmdWriteTimestamp,
    .vkCmdCopyQueryPoolResults = anv_tramp_CmdCopyQueryPoolResults,
    .vkCmdPushConstants = anv_tramp_CmdPushConstants,
    .vkCmdBeginRenderPass = anv_tramp_CmdBeginRenderPass,
    .vkCmdNextSubpass = anv_tramp_CmdNextSubpass,
    .vkCmdEndRenderPass = anv_tramp_CmdEndRenderPass,
    .vkCmdExecuteCommands = anv_tramp_CmdExecuteCommands,
                        .vkCreateSwapchainKHR = anv_tramp_CreateSwapchainKHR,
    .vkDestroySwapchainKHR = anv_tramp_DestroySwapchainKHR,
    .vkGetSwapchainImagesKHR = anv_tramp_GetSwapchainImagesKHR,
    .vkAcquireNextImageKHR = anv_tramp_AcquireNextImageKHR,
                                                                        .vkCmdPushDescriptorSetKHR = anv_tramp_CmdPushDescriptorSetKHR,
    .vkTrimCommandPoolKHR = anv_tramp_TrimCommandPoolKHR,
        .vkGetMemoryFdKHR = anv_tramp_GetMemoryFdKHR,
    .vkGetMemoryFdPropertiesKHR = anv_tramp_GetMemoryFdPropertiesKHR,
        .vkGetSemaphoreFdKHR = anv_tramp_GetSemaphoreFdKHR,
    .vkImportSemaphoreFdKHR = anv_tramp_ImportSemaphoreFdKHR,
        .vkGetFenceFdKHR = anv_tramp_GetFenceFdKHR,
    .vkImportFenceFdKHR = anv_tramp_ImportFenceFdKHR,
    .vkBindBufferMemory2KHR = anv_tramp_BindBufferMemory2KHR,
    .vkBindImageMemory2KHR = anv_tramp_BindImageMemory2KHR,
    .vkCreateDescriptorUpdateTemplateKHR = anv_tramp_CreateDescriptorUpdateTemplateKHR,
    .vkDestroyDescriptorUpdateTemplateKHR = anv_tramp_DestroyDescriptorUpdateTemplateKHR,
    .vkUpdateDescriptorSetWithTemplateKHR = anv_tramp_UpdateDescriptorSetWithTemplateKHR,
    .vkCmdPushDescriptorSetWithTemplateKHR = anv_tramp_CmdPushDescriptorSetWithTemplateKHR,
            .vkGetBufferMemoryRequirements2KHR = anv_tramp_GetBufferMemoryRequirements2KHR,
    .vkGetImageMemoryRequirements2KHR = anv_tramp_GetImageMemoryRequirements2KHR,
    .vkGetImageSparseMemoryRequirements2KHR = anv_tramp_GetImageSparseMemoryRequirements2KHR,
    .vkCreateSamplerYcbcrConversionKHR = anv_tramp_CreateSamplerYcbcrConversionKHR,
    .vkDestroySamplerYcbcrConversionKHR = anv_tramp_DestroySamplerYcbcrConversionKHR,
#ifdef ANDROID
    .vkGetSwapchainGrallocUsageANDROID = anv_tramp_GetSwapchainGrallocUsageANDROID,
#endif // ANDROID
#ifdef ANDROID
    .vkAcquireImageANDROID = anv_tramp_AcquireImageANDROID,
#endif // ANDROID
        .vkCreateDmaBufImageINTEL = anv_tramp_CreateDmaBufImageINTEL,
};


/** Return true if the core version or extension in which the given entrypoint
 * is defined is enabled.
 *
 * If device is NULL, all device extensions are considered enabled.
 */
bool
anv_entrypoint_is_enabled(int index, uint32_t core_version,
                          const struct anv_instance_extension_table *instance,
                          const struct anv_device_extension_table *device)
{
   switch (index) {
   case 0:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 1:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 2:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 3:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 4:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 5:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 6:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 7:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 8:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 9:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 10:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 11:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 12:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 13:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 14:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 15:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 16:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 17:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 18:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 19:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 20:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 21:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 22:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 23:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 24:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 25:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 26:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 27:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 28:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 29:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 30:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 31:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 32:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 33:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 34:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 35:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 36:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 37:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 38:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 39:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 40:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 41:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 42:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 43:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 44:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 45:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 46:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 47:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 48:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 49:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 50:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 51:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 52:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 53:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 54:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 55:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 56:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 57:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 58:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 59:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 60:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 61:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 62:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 63:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 64:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 65:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 66:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 67:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 68:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 69:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 70:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 71:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 72:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 73:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 74:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 75:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 76:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 77:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 78:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 79:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 80:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 81:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 82:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 83:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 84:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 85:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 86:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 87:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 88:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 89:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 90:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 91:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 92:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 93:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 94:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 95:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 96:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 97:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 98:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 99:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 100:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 101:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 102:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 103:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 104:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 105:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 106:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 107:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 108:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 109:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 110:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 111:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 112:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 113:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 114:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 115:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 116:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 117:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 118:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 119:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 120:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 121:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 122:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 123:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 124:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 125:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 126:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 127:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 128:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 129:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 130:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 131:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 132:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 133:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 134:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 135:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 136:
      return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 137:
      return instance->KHR_surface;
   case 138:
      return instance->KHR_surface;
   case 139:
      return instance->KHR_surface;
   case 140:
      return instance->KHR_surface;
   case 141:
      return instance->KHR_surface;
   case 142:
      return !device || device->KHR_swapchain;
   case 143:
      return !device || device->KHR_swapchain;
   case 144:
      return !device || device->KHR_swapchain;
   case 145:
      return !device || device->KHR_swapchain;
   case 146:
      return !device || device->KHR_swapchain;
   case 147:
      return instance->KHR_wayland_surface;
   case 148:
      return instance->KHR_wayland_surface;
   case 149:
      return instance->KHR_xlib_surface;
   case 150:
      return instance->KHR_xlib_surface;
   case 151:
      return instance->KHR_xcb_surface;
   case 152:
      return instance->KHR_xcb_surface;
   case 153:
      return instance->EXT_debug_report;
   case 154:
      return instance->EXT_debug_report;
   case 155:
      return instance->EXT_debug_report;
   case 156:
      return instance->KHR_get_physical_device_properties2;
   case 157:
      return instance->KHR_get_physical_device_properties2;
   case 158:
      return instance->KHR_get_physical_device_properties2;
   case 159:
      return instance->KHR_get_physical_device_properties2;
   case 160:
      return instance->KHR_get_physical_device_properties2;
   case 161:
      return instance->KHR_get_physical_device_properties2;
   case 162:
      return instance->KHR_get_physical_device_properties2;
   case 163:
      return !device || device->KHR_push_descriptor;
   case 164:
      return !device || device->KHR_maintenance1;
   case 165:
      return instance->KHR_external_memory_capabilities;
   case 166:
      return !device || device->KHR_external_memory_fd;
   case 167:
      return !device || device->KHR_external_memory_fd;
   case 168:
      return instance->KHR_external_semaphore_capabilities;
   case 169:
      return !device || device->KHR_external_semaphore_fd;
   case 170:
      return !device || device->KHR_external_semaphore_fd;
   case 171:
      return instance->KHR_external_fence_capabilities;
   case 172:
      return !device || device->KHR_external_fence_fd;
   case 173:
      return !device || device->KHR_external_fence_fd;
   case 174:
      return !device || device->KHR_bind_memory2;
   case 175:
      return !device || device->KHR_bind_memory2;
   case 176:
      return !device || device->KHR_descriptor_update_template;
   case 177:
      return !device || device->KHR_descriptor_update_template;
   case 178:
      return !device || device->KHR_descriptor_update_template;
   case 179:
      return !device || device->KHR_descriptor_update_template;
   case 180:
      return instance->KHR_get_surface_capabilities2;
   case 181:
      return instance->KHR_get_surface_capabilities2;
   case 182:
      return !device || device->KHR_get_memory_requirements2;
   case 183:
      return !device || device->KHR_get_memory_requirements2;
   case 184:
      return !device || device->KHR_get_memory_requirements2;
   case 185:
      return !device || device->KHR_sampler_ycbcr_conversion;
   case 186:
      return !device || device->KHR_sampler_ycbcr_conversion;
   case 187:
      return !device || device->ANDROID_native_buffer;
   case 188:
      return !device || device->ANDROID_native_buffer;
   case 189:
      return !device || device->ANDROID_native_buffer;
   case 190:
      return true;
   default:
      return false;
   }
}

static void * __attribute__ ((noinline))
anv_resolve_entrypoint(const struct gen_device_info *devinfo, uint32_t index)
{
   if (devinfo == NULL) {
      return anv_dispatch_table.entrypoints[index];
   }

   const struct anv_dispatch_table *genX_table;
   switch (devinfo->gen) {
   case 10:
      genX_table = &gen10_dispatch_table;
      break;
   case 9:
      genX_table = &gen9_dispatch_table;
      break;
   case 8:
      genX_table = &gen8_dispatch_table;
      break;
   case 7:
      if (devinfo->is_haswell)
         genX_table = &gen75_dispatch_table;
      else
         genX_table = &gen7_dispatch_table;
      break;
   default:
      unreachable("unsupported gen\n");
   }

   if (genX_table->entrypoints[index])
      return genX_table->entrypoints[index];
   else
      return anv_dispatch_table.entrypoints[index];
}

/* Hash table stats:
 * size 256 entries
 * collisions entries:
 *     0     119
 *     1     37
 *     2     13
 *     3     7
 *     4     4
 *     5     1
 *     6     5
 *     7     3
 *     8     1
 *     9+     1
 */

#define none 0xffff
static const uint16_t map[] = {
      0x0044,
      none,
      0x00bd,
      0x00bc,
      0x00a7,
      0x002b,
      0x0040,
      0x0061,
      0x0049,
      0x0022,
      0x0056,
      none,
      none,
      none,
      0x00a5,
      none,
      none,
      0x00a6,
      none,
      0x0067,
      none,
      none,
      none,
      0x00ab,
      0x0052,
      0x0097,
      0x0058,
      0x004c,
      none,
      0x0069,
      0x00b1,
      none,
      none,
      0x00ac,
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
      0x00be,
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
      0x00a0,
      0x004d,
      0x0090,
      0x0024,
      0x00a1,
      0x005e,
      0x000b,
      0x0088,
      0x0091,
      none,
      0x00b2,
      0x005c,
      0x0033,
      none,
      0x009b,
      0x0087,
      0x003f,
      0x001f,
      0x002c,
      0x0082,
      0x005a,
      none,
      0x00b9,
      none,
      0x0019,
      0x0046,
      0x003a,
      0x009a,
      none,
      0x0034,
      none,
      0x0051,
      none,
      none,
      0x0020,
      0x009c,
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
      0x0099,
      none,
      0x006b,
      none,
      0x0041,
      0x0028,
      none,
      0x0068,
      0x00b8,
      0x00a2,
      0x003e,
      0x0048,
      0x007b,
      0x0055,
      0x00aa,
      0x00b4,
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
      0x00b7,
      0x005d,
      0x008a,
      0x0003,
      0x008f,
      0x00b5,
      0x0063,
      0x0006,
      none,
      0x0093,
      0x00a4,
      none,
      none,
      0x00ad,
      0x0059,
      0x0026,
      none,
      0x003c,
      none,
      0x0037,
      0x00a9,
      0x0009,
      0x0038,
      0x0011,
      none,
      0x0072,
      0x0016,
      none,
      0x003d,
      0x00b6,
      0x006a,
      0x003b,
      0x00ba,
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
      0x00b0,
      0x004e,
      0x0095,
      0x0031,
      0x00a3,
      0x001b,
      0x00bb,
      0x0073,
      0x005f,
      0x0032,
      0x0078,
      0x008e,
      none,
      none,
      none,
      0x006c,
      0x00af,
      none,
      0x0036,
      none,
      0x0050,
      0x009d,
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
      0x009e,
      0x0064,
      0x0085,
      none,
      none,
      none,
      0x000f,
      0x007e,
      none,
      0x009f,
      0x0017,
      0x0012,
      0x0010,
      none,
      0x0021,
      0x008b,
      0x0079,
      0x0001,
      0x00b3,
      0x00ae,
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

int
anv_get_entrypoint_index(const char *name)
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
         return -1;
      e = &entrypoints[i];
      h += prime_step;
   } while (e->hash != hash);

   if (strcmp(name, strings + e->name) != 0)
      return -1;

   return i;
}

void *
anv_lookup_entrypoint(const struct gen_device_info *devinfo, const char *name)
{
   int idx = anv_get_entrypoint_index(name);
   if (idx < 0)
      return NULL;
   return anv_resolve_entrypoint(devinfo, idx);
}