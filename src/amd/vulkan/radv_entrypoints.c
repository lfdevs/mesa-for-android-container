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

/* This file generated from radv_entrypoints_gen.py, don't edit directly. */

#include "radv_private.h"

struct radv_entrypoint {
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
    "vkCmdDrawIndirectCountAMD\0"
    "vkCmdDrawIndexedIndirectCountAMD\0"
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
    "vkBindBufferMemory2KHR\0"
    "vkBindImageMemory2KHR\0"
    "vkCreateDescriptorUpdateTemplateKHR\0"
    "vkDestroyDescriptorUpdateTemplateKHR\0"
    "vkUpdateDescriptorSetWithTemplateKHR\0"
    "vkCmdPushDescriptorSetWithTemplateKHR\0"
    "vkGetBufferMemoryRequirements2KHR\0"
    "vkGetImageMemoryRequirements2KHR\0"
    "vkGetImageSparseMemoryRequirements2KHR\0"
;

static const struct radv_entrypoint entrypoints[] = {
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
    [153] = { 3401, 0xe5ad0a50 }, /* vkCmdDrawIndirectCountAMD */
    [154] = { 3427, 0xc86e9287 }, /* vkCmdDrawIndexedIndirectCountAMD */
    [155] = { 3460, 0x6a9a3636 }, /* vkGetPhysicalDeviceFeatures2KHR */
    [156] = { 3492, 0xcd15838c }, /* vkGetPhysicalDeviceProperties2KHR */
    [157] = { 3526, 0x9099cbbb }, /* vkGetPhysicalDeviceFormatProperties2KHR */
    [158] = { 3566, 0x102ff7ea }, /* vkGetPhysicalDeviceImageFormatProperties2KHR */
    [159] = { 3611, 0x5ceb2bed }, /* vkGetPhysicalDeviceQueueFamilyProperties2KHR */
    [160] = { 3656, 0xc8c3da3d }, /* vkGetPhysicalDeviceMemoryProperties2KHR */
    [161] = { 3696, 0x8746ed72 }, /* vkGetPhysicalDeviceSparseImageFormatProperties2KHR */
    [162] = { 3747, 0xf17232a1 }, /* vkCmdPushDescriptorSetKHR */
    [163] = { 3773, 0x51177c8d }, /* vkTrimCommandPoolKHR */
    [164] = { 3794, 0xee68b389 }, /* vkGetPhysicalDeviceExternalBufferPropertiesKHR */
    [165] = { 3841, 0x503c14c5 }, /* vkGetMemoryFdKHR */
    [166] = { 3858, 0xb028a792 }, /* vkGetMemoryFdPropertiesKHR */
    [167] = { 3885, 0x984c3fa7 }, /* vkGetPhysicalDeviceExternalSemaphorePropertiesKHR */
    [168] = { 3935, 0x3e0e9884 }, /* vkGetSemaphoreFdKHR */
    [169] = { 3955, 0x36337c05 }, /* vkImportSemaphoreFdKHR */
    [170] = { 3978, 0x6878d3ce }, /* vkBindBufferMemory2KHR */
    [171] = { 4001, 0xf18729ad }, /* vkBindImageMemory2KHR */
    [172] = { 4023, 0x5189488a }, /* vkCreateDescriptorUpdateTemplateKHR */
    [173] = { 4059, 0xaa83901e }, /* vkDestroyDescriptorUpdateTemplateKHR */
    [174] = { 4096, 0x214ad230 }, /* vkUpdateDescriptorSetWithTemplateKHR */
    [175] = { 4133, 0x3d528981 }, /* vkCmdPushDescriptorSetWithTemplateKHR */
    [176] = { 4171, 0x78dbe98d }, /* vkGetBufferMemoryRequirements2KHR */
    [177] = { 4205, 0x8de28366 }, /* vkGetImageMemoryRequirements2KHR */
    [178] = { 4238, 0x3df40f5e }, /* vkGetImageSparseMemoryRequirements2KHR */
};

/* Weak aliases for all potential implementations. These will resolve to
 * NULL if they're not defined, which lets the resolve_entrypoint() function
 * either pick the correct entry point.
 */

    VkResult radv_CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) __attribute__ ((weak));
    void radv_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult radv_EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) __attribute__ ((weak));
    PFN_vkVoidFunction radv_GetDeviceProcAddr(VkDevice device, const char* pName) __attribute__ ((weak));
    PFN_vkVoidFunction radv_GetInstanceProcAddr(VkInstance instance, const char* pName) __attribute__ ((weak));
    void radv_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) __attribute__ ((weak));
    void radv_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties) __attribute__ ((weak));
    void radv_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties) __attribute__ ((weak));
    void radv_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures) __attribute__ ((weak));
    void radv_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties) __attribute__ ((weak));
    VkResult radv_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* pImageFormatProperties) __attribute__ ((weak));
    VkResult radv_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) __attribute__ ((weak));
    void radv_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult radv_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
    VkResult radv_EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
    VkResult radv_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
    VkResult radv_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
    void radv_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue) __attribute__ ((weak));
    VkResult radv_QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence) __attribute__ ((weak));
    VkResult radv_QueueWaitIdle(VkQueue queue) __attribute__ ((weak));
    VkResult radv_DeviceWaitIdle(VkDevice device) __attribute__ ((weak));
    VkResult radv_AllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory) __attribute__ ((weak));
    void radv_FreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult radv_MapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData) __attribute__ ((weak));
    void radv_UnmapMemory(VkDevice device, VkDeviceMemory memory) __attribute__ ((weak));
    VkResult radv_FlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) __attribute__ ((weak));
    VkResult radv_InvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) __attribute__ ((weak));
    void radv_GetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory, VkDeviceSize* pCommittedMemoryInBytes) __attribute__ ((weak));
    void radv_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
    VkResult radv_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
    void radv_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
    VkResult radv_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
    void radv_GetImageSparseMemoryRequirements(VkDevice device, VkImage image, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements* pSparseMemoryRequirements) __attribute__ ((weak));
    void radv_GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling, uint32_t* pPropertyCount, VkSparseImageFormatProperties* pProperties) __attribute__ ((weak));
    VkResult radv_QueueBindSparse(VkQueue queue, uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence) __attribute__ ((weak));
    VkResult radv_CreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence) __attribute__ ((weak));
    void radv_DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult radv_ResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences) __attribute__ ((weak));
    VkResult radv_GetFenceStatus(VkDevice device, VkFence fence) __attribute__ ((weak));
    VkResult radv_WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout) __attribute__ ((weak));
    VkResult radv_CreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore) __attribute__ ((weak));
    void radv_DestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult radv_CreateEvent(VkDevice device, const VkEventCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkEvent* pEvent) __attribute__ ((weak));
    void radv_DestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult radv_GetEventStatus(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult radv_SetEvent(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult radv_ResetEvent(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult radv_CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool) __attribute__ ((weak));
    void radv_DestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult radv_GetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* pData, VkDeviceSize stride, VkQueryResultFlags flags) __attribute__ ((weak));
    VkResult radv_CreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer) __attribute__ ((weak));
    void radv_DestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult radv_CreateBufferView(VkDevice device, const VkBufferViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBufferView* pView) __attribute__ ((weak));
    void radv_DestroyBufferView(VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult radv_CreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage) __attribute__ ((weak));
    void radv_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void radv_GetImageSubresourceLayout(VkDevice device, VkImage image, const VkImageSubresource* pSubresource, VkSubresourceLayout* pLayout) __attribute__ ((weak));
    VkResult radv_CreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView) __attribute__ ((weak));
    void radv_DestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult radv_CreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule) __attribute__ ((weak));
    void radv_DestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult radv_CreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineCache* pPipelineCache) __attribute__ ((weak));
    void radv_DestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult radv_GetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, size_t* pDataSize, void* pData) __attribute__ ((weak));
    VkResult radv_MergePipelineCaches(VkDevice device, VkPipelineCache dstCache, uint32_t srcCacheCount, const VkPipelineCache* pSrcCaches) __attribute__ ((weak));
    VkResult radv_CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) __attribute__ ((weak));
    VkResult radv_CreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) __attribute__ ((weak));
    void radv_DestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult radv_CreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout) __attribute__ ((weak));
    void radv_DestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult radv_CreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSampler* pSampler) __attribute__ ((weak));
    void radv_DestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult radv_CreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout) __attribute__ ((weak));
    void radv_DestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult radv_CreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool) __attribute__ ((weak));
    void radv_DestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult radv_ResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags) __attribute__ ((weak));
    VkResult radv_AllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets) __attribute__ ((weak));
    VkResult radv_FreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets) __attribute__ ((weak));
    void radv_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies) __attribute__ ((weak));
    VkResult radv_CreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFramebuffer* pFramebuffer) __attribute__ ((weak));
    void radv_DestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult radv_CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass) __attribute__ ((weak));
    void radv_DestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void radv_GetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass, VkExtent2D* pGranularity) __attribute__ ((weak));
    VkResult radv_CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool) __attribute__ ((weak));
    void radv_DestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult radv_ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags) __attribute__ ((weak));
    VkResult radv_AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    void radv_FreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    VkResult radv_BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo) __attribute__ ((weak));
    VkResult radv_EndCommandBuffer(VkCommandBuffer commandBuffer) __attribute__ ((weak));
    VkResult radv_ResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags) __attribute__ ((weak));
    void radv_CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) __attribute__ ((weak));
    void radv_CmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports) __attribute__ ((weak));
    void radv_CmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors) __attribute__ ((weak));
    void radv_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth) __attribute__ ((weak));
    void radv_CmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor) __attribute__ ((weak));
    void radv_CmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4]) __attribute__ ((weak));
    void radv_CmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds) __attribute__ ((weak));
    void radv_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t compareMask) __attribute__ ((weak));
    void radv_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t writeMask) __attribute__ ((weak));
    void radv_CmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference) __attribute__ ((weak));
    void radv_CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets) __attribute__ ((weak));
    void radv_CmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) __attribute__ ((weak));
    void radv_CmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets) __attribute__ ((weak));
    void radv_CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) __attribute__ ((weak));
    void radv_CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) __attribute__ ((weak));
    void radv_CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) __attribute__ ((weak));
    void radv_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) __attribute__ ((weak));
    void radv_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) __attribute__ ((weak));
    void radv_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset) __attribute__ ((weak));
    void radv_CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions) __attribute__ ((weak));
    void radv_CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions) __attribute__ ((weak));
    void radv_CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter) __attribute__ ((weak));
    void radv_CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
    void radv_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
    void radv_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* pData) __attribute__ ((weak));
    void radv_CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data) __attribute__ ((weak));
    void radv_CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) __attribute__ ((weak));
    void radv_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) __attribute__ ((weak));
    void radv_CmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment* pAttachments, uint32_t rectCount, const VkClearRect* pRects) __attribute__ ((weak));
    void radv_CmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageResolve* pRegions) __attribute__ ((weak));
    void radv_CmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) __attribute__ ((weak));
    void radv_CmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) __attribute__ ((weak));
    void radv_CmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) __attribute__ ((weak));
    void radv_CmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) __attribute__ ((weak));
    void radv_CmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags) __attribute__ ((weak));
    void radv_CmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query) __attribute__ ((weak));
    void radv_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) __attribute__ ((weak));
    void radv_CmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query) __attribute__ ((weak));
    void radv_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags) __attribute__ ((weak));
    void radv_CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues) __attribute__ ((weak));
    void radv_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents) __attribute__ ((weak));
    void radv_CmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents) __attribute__ ((weak));
    void radv_CmdEndRenderPass(VkCommandBuffer commandBuffer) __attribute__ ((weak));
    void radv_CmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    void radv_DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult radv_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32* pSupported) __attribute__ ((weak));
    VkResult radv_GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult radv_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats) __attribute__ ((weak));
    VkResult radv_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes) __attribute__ ((weak));
    VkResult radv_CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) __attribute__ ((weak));
    void radv_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult radv_GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages) __attribute__ ((weak));
    VkResult radv_AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex) __attribute__ ((weak));
    VkResult radv_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) __attribute__ ((weak));
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    VkResult radv_CreateWaylandSurfaceKHR(VkInstance instance, const VkWaylandSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    VkBool32 radv_GetPhysicalDeviceWaylandPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, struct wl_display* display) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    VkResult radv_CreateXlibSurfaceKHR(VkInstance instance, const VkXlibSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    VkBool32 radv_GetPhysicalDeviceXlibPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, Display* dpy, VisualID visualID) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    VkResult radv_CreateXcbSurfaceKHR(VkInstance instance, const VkXcbSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    VkBool32 radv_GetPhysicalDeviceXcbPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, xcb_connection_t* connection, xcb_visualid_t visual_id) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XCB_KHR
    void radv_CmdDrawIndirectCountAMD(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride) __attribute__ ((weak));
    void radv_CmdDrawIndexedIndirectCountAMD(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride) __attribute__ ((weak));
    void radv_GetPhysicalDeviceFeatures2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2KHR* pFeatures) __attribute__ ((weak));
    void radv_GetPhysicalDeviceProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2KHR* pProperties) __attribute__ ((weak));
    void radv_GetPhysicalDeviceFormatProperties2KHR(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2KHR* pFormatProperties) __attribute__ ((weak));
    VkResult radv_GetPhysicalDeviceImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2KHR* pImageFormatInfo, VkImageFormatProperties2KHR* pImageFormatProperties) __attribute__ ((weak));
    void radv_GetPhysicalDeviceQueueFamilyProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2KHR* pQueueFamilyProperties) __attribute__ ((weak));
    void radv_GetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2KHR* pMemoryProperties) __attribute__ ((weak));
    void radv_GetPhysicalDeviceSparseImageFormatProperties2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2KHR* pFormatInfo, uint32_t* pPropertyCount, VkSparseImageFormatProperties2KHR* pProperties) __attribute__ ((weak));
    void radv_CmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites) __attribute__ ((weak));
    void radv_TrimCommandPoolKHR(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlagsKHR flags) __attribute__ ((weak));
    void radv_GetPhysicalDeviceExternalBufferPropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfoKHR* pExternalBufferInfo, VkExternalBufferPropertiesKHR* pExternalBufferProperties) __attribute__ ((weak));
    VkResult radv_GetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult radv_GetMemoryFdPropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBitsKHR handleType, int fd, VkMemoryFdPropertiesKHR* pMemoryFdProperties) __attribute__ ((weak));
    void radv_GetPhysicalDeviceExternalSemaphorePropertiesKHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfoKHR* pExternalSemaphoreInfo, VkExternalSemaphorePropertiesKHR* pExternalSemaphoreProperties) __attribute__ ((weak));
    VkResult radv_GetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult radv_ImportSemaphoreFdKHR(VkDevice device, const VkImportSemaphoreFdInfoKHR* pImportSemaphoreFdInfo) __attribute__ ((weak));
    VkResult radv_BindBufferMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfoKHR* pBindInfos) __attribute__ ((weak));
    VkResult radv_BindImageMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfoKHR* pBindInfos) __attribute__ ((weak));
    VkResult radv_CreateDescriptorUpdateTemplateKHR(VkDevice device, const VkDescriptorUpdateTemplateCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplateKHR* pDescriptorUpdateTemplate) __attribute__ ((weak));
    void radv_DestroyDescriptorUpdateTemplateKHR(VkDevice device, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void radv_UpdateDescriptorSetWithTemplateKHR(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, const void* pData) __attribute__ ((weak));
    void radv_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplateKHR descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) __attribute__ ((weak));
    void radv_GetBufferMemoryRequirements2KHR(VkDevice device, const VkBufferMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void radv_GetImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2KHR* pInfo, VkMemoryRequirements2KHR* pMemoryRequirements) __attribute__ ((weak));
    void radv_GetImageSparseMemoryRequirements2KHR(VkDevice device, const VkImageSparseMemoryRequirementsInfo2KHR* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2KHR* pSparseMemoryRequirements) __attribute__ ((weak));

  const struct radv_dispatch_table radv_layer = {
    .CreateInstance = radv_CreateInstance,
    .DestroyInstance = radv_DestroyInstance,
    .EnumeratePhysicalDevices = radv_EnumeratePhysicalDevices,
    .GetDeviceProcAddr = radv_GetDeviceProcAddr,
    .GetInstanceProcAddr = radv_GetInstanceProcAddr,
    .GetPhysicalDeviceProperties = radv_GetPhysicalDeviceProperties,
    .GetPhysicalDeviceQueueFamilyProperties = radv_GetPhysicalDeviceQueueFamilyProperties,
    .GetPhysicalDeviceMemoryProperties = radv_GetPhysicalDeviceMemoryProperties,
    .GetPhysicalDeviceFeatures = radv_GetPhysicalDeviceFeatures,
    .GetPhysicalDeviceFormatProperties = radv_GetPhysicalDeviceFormatProperties,
    .GetPhysicalDeviceImageFormatProperties = radv_GetPhysicalDeviceImageFormatProperties,
    .CreateDevice = radv_CreateDevice,
    .DestroyDevice = radv_DestroyDevice,
    .EnumerateInstanceLayerProperties = radv_EnumerateInstanceLayerProperties,
    .EnumerateInstanceExtensionProperties = radv_EnumerateInstanceExtensionProperties,
    .EnumerateDeviceLayerProperties = radv_EnumerateDeviceLayerProperties,
    .EnumerateDeviceExtensionProperties = radv_EnumerateDeviceExtensionProperties,
    .GetDeviceQueue = radv_GetDeviceQueue,
    .QueueSubmit = radv_QueueSubmit,
    .QueueWaitIdle = radv_QueueWaitIdle,
    .DeviceWaitIdle = radv_DeviceWaitIdle,
    .AllocateMemory = radv_AllocateMemory,
    .FreeMemory = radv_FreeMemory,
    .MapMemory = radv_MapMemory,
    .UnmapMemory = radv_UnmapMemory,
    .FlushMappedMemoryRanges = radv_FlushMappedMemoryRanges,
    .InvalidateMappedMemoryRanges = radv_InvalidateMappedMemoryRanges,
    .GetDeviceMemoryCommitment = radv_GetDeviceMemoryCommitment,
    .GetBufferMemoryRequirements = radv_GetBufferMemoryRequirements,
    .BindBufferMemory = radv_BindBufferMemory,
    .GetImageMemoryRequirements = radv_GetImageMemoryRequirements,
    .BindImageMemory = radv_BindImageMemory,
    .GetImageSparseMemoryRequirements = radv_GetImageSparseMemoryRequirements,
    .GetPhysicalDeviceSparseImageFormatProperties = radv_GetPhysicalDeviceSparseImageFormatProperties,
    .QueueBindSparse = radv_QueueBindSparse,
    .CreateFence = radv_CreateFence,
    .DestroyFence = radv_DestroyFence,
    .ResetFences = radv_ResetFences,
    .GetFenceStatus = radv_GetFenceStatus,
    .WaitForFences = radv_WaitForFences,
    .CreateSemaphore = radv_CreateSemaphore,
    .DestroySemaphore = radv_DestroySemaphore,
    .CreateEvent = radv_CreateEvent,
    .DestroyEvent = radv_DestroyEvent,
    .GetEventStatus = radv_GetEventStatus,
    .SetEvent = radv_SetEvent,
    .ResetEvent = radv_ResetEvent,
    .CreateQueryPool = radv_CreateQueryPool,
    .DestroyQueryPool = radv_DestroyQueryPool,
    .GetQueryPoolResults = radv_GetQueryPoolResults,
    .CreateBuffer = radv_CreateBuffer,
    .DestroyBuffer = radv_DestroyBuffer,
    .CreateBufferView = radv_CreateBufferView,
    .DestroyBufferView = radv_DestroyBufferView,
    .CreateImage = radv_CreateImage,
    .DestroyImage = radv_DestroyImage,
    .GetImageSubresourceLayout = radv_GetImageSubresourceLayout,
    .CreateImageView = radv_CreateImageView,
    .DestroyImageView = radv_DestroyImageView,
    .CreateShaderModule = radv_CreateShaderModule,
    .DestroyShaderModule = radv_DestroyShaderModule,
    .CreatePipelineCache = radv_CreatePipelineCache,
    .DestroyPipelineCache = radv_DestroyPipelineCache,
    .GetPipelineCacheData = radv_GetPipelineCacheData,
    .MergePipelineCaches = radv_MergePipelineCaches,
    .CreateGraphicsPipelines = radv_CreateGraphicsPipelines,
    .CreateComputePipelines = radv_CreateComputePipelines,
    .DestroyPipeline = radv_DestroyPipeline,
    .CreatePipelineLayout = radv_CreatePipelineLayout,
    .DestroyPipelineLayout = radv_DestroyPipelineLayout,
    .CreateSampler = radv_CreateSampler,
    .DestroySampler = radv_DestroySampler,
    .CreateDescriptorSetLayout = radv_CreateDescriptorSetLayout,
    .DestroyDescriptorSetLayout = radv_DestroyDescriptorSetLayout,
    .CreateDescriptorPool = radv_CreateDescriptorPool,
    .DestroyDescriptorPool = radv_DestroyDescriptorPool,
    .ResetDescriptorPool = radv_ResetDescriptorPool,
    .AllocateDescriptorSets = radv_AllocateDescriptorSets,
    .FreeDescriptorSets = radv_FreeDescriptorSets,
    .UpdateDescriptorSets = radv_UpdateDescriptorSets,
    .CreateFramebuffer = radv_CreateFramebuffer,
    .DestroyFramebuffer = radv_DestroyFramebuffer,
    .CreateRenderPass = radv_CreateRenderPass,
    .DestroyRenderPass = radv_DestroyRenderPass,
    .GetRenderAreaGranularity = radv_GetRenderAreaGranularity,
    .CreateCommandPool = radv_CreateCommandPool,
    .DestroyCommandPool = radv_DestroyCommandPool,
    .ResetCommandPool = radv_ResetCommandPool,
    .AllocateCommandBuffers = radv_AllocateCommandBuffers,
    .FreeCommandBuffers = radv_FreeCommandBuffers,
    .BeginCommandBuffer = radv_BeginCommandBuffer,
    .EndCommandBuffer = radv_EndCommandBuffer,
    .ResetCommandBuffer = radv_ResetCommandBuffer,
    .CmdBindPipeline = radv_CmdBindPipeline,
    .CmdSetViewport = radv_CmdSetViewport,
    .CmdSetScissor = radv_CmdSetScissor,
    .CmdSetLineWidth = radv_CmdSetLineWidth,
    .CmdSetDepthBias = radv_CmdSetDepthBias,
    .CmdSetBlendConstants = radv_CmdSetBlendConstants,
    .CmdSetDepthBounds = radv_CmdSetDepthBounds,
    .CmdSetStencilCompareMask = radv_CmdSetStencilCompareMask,
    .CmdSetStencilWriteMask = radv_CmdSetStencilWriteMask,
    .CmdSetStencilReference = radv_CmdSetStencilReference,
    .CmdBindDescriptorSets = radv_CmdBindDescriptorSets,
    .CmdBindIndexBuffer = radv_CmdBindIndexBuffer,
    .CmdBindVertexBuffers = radv_CmdBindVertexBuffers,
    .CmdDraw = radv_CmdDraw,
    .CmdDrawIndexed = radv_CmdDrawIndexed,
    .CmdDrawIndirect = radv_CmdDrawIndirect,
    .CmdDrawIndexedIndirect = radv_CmdDrawIndexedIndirect,
    .CmdDispatch = radv_CmdDispatch,
    .CmdDispatchIndirect = radv_CmdDispatchIndirect,
    .CmdCopyBuffer = radv_CmdCopyBuffer,
    .CmdCopyImage = radv_CmdCopyImage,
    .CmdBlitImage = radv_CmdBlitImage,
    .CmdCopyBufferToImage = radv_CmdCopyBufferToImage,
    .CmdCopyImageToBuffer = radv_CmdCopyImageToBuffer,
    .CmdUpdateBuffer = radv_CmdUpdateBuffer,
    .CmdFillBuffer = radv_CmdFillBuffer,
    .CmdClearColorImage = radv_CmdClearColorImage,
    .CmdClearDepthStencilImage = radv_CmdClearDepthStencilImage,
    .CmdClearAttachments = radv_CmdClearAttachments,
    .CmdResolveImage = radv_CmdResolveImage,
    .CmdSetEvent = radv_CmdSetEvent,
    .CmdResetEvent = radv_CmdResetEvent,
    .CmdWaitEvents = radv_CmdWaitEvents,
    .CmdPipelineBarrier = radv_CmdPipelineBarrier,
    .CmdBeginQuery = radv_CmdBeginQuery,
    .CmdEndQuery = radv_CmdEndQuery,
    .CmdResetQueryPool = radv_CmdResetQueryPool,
    .CmdWriteTimestamp = radv_CmdWriteTimestamp,
    .CmdCopyQueryPoolResults = radv_CmdCopyQueryPoolResults,
    .CmdPushConstants = radv_CmdPushConstants,
    .CmdBeginRenderPass = radv_CmdBeginRenderPass,
    .CmdNextSubpass = radv_CmdNextSubpass,
    .CmdEndRenderPass = radv_CmdEndRenderPass,
    .CmdExecuteCommands = radv_CmdExecuteCommands,
    .DestroySurfaceKHR = radv_DestroySurfaceKHR,
    .GetPhysicalDeviceSurfaceSupportKHR = radv_GetPhysicalDeviceSurfaceSupportKHR,
    .GetPhysicalDeviceSurfaceCapabilitiesKHR = radv_GetPhysicalDeviceSurfaceCapabilitiesKHR,
    .GetPhysicalDeviceSurfaceFormatsKHR = radv_GetPhysicalDeviceSurfaceFormatsKHR,
    .GetPhysicalDeviceSurfacePresentModesKHR = radv_GetPhysicalDeviceSurfacePresentModesKHR,
    .CreateSwapchainKHR = radv_CreateSwapchainKHR,
    .DestroySwapchainKHR = radv_DestroySwapchainKHR,
    .GetSwapchainImagesKHR = radv_GetSwapchainImagesKHR,
    .AcquireNextImageKHR = radv_AcquireNextImageKHR,
    .QueuePresentKHR = radv_QueuePresentKHR,
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .CreateWaylandSurfaceKHR = radv_CreateWaylandSurfaceKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .GetPhysicalDeviceWaylandPresentationSupportKHR = radv_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .CreateXlibSurfaceKHR = radv_CreateXlibSurfaceKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .GetPhysicalDeviceXlibPresentationSupportKHR = radv_GetPhysicalDeviceXlibPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .CreateXcbSurfaceKHR = radv_CreateXcbSurfaceKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .GetPhysicalDeviceXcbPresentationSupportKHR = radv_GetPhysicalDeviceXcbPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
    .CmdDrawIndirectCountAMD = radv_CmdDrawIndirectCountAMD,
    .CmdDrawIndexedIndirectCountAMD = radv_CmdDrawIndexedIndirectCountAMD,
    .GetPhysicalDeviceFeatures2KHR = radv_GetPhysicalDeviceFeatures2KHR,
    .GetPhysicalDeviceProperties2KHR = radv_GetPhysicalDeviceProperties2KHR,
    .GetPhysicalDeviceFormatProperties2KHR = radv_GetPhysicalDeviceFormatProperties2KHR,
    .GetPhysicalDeviceImageFormatProperties2KHR = radv_GetPhysicalDeviceImageFormatProperties2KHR,
    .GetPhysicalDeviceQueueFamilyProperties2KHR = radv_GetPhysicalDeviceQueueFamilyProperties2KHR,
    .GetPhysicalDeviceMemoryProperties2KHR = radv_GetPhysicalDeviceMemoryProperties2KHR,
    .GetPhysicalDeviceSparseImageFormatProperties2KHR = radv_GetPhysicalDeviceSparseImageFormatProperties2KHR,
    .CmdPushDescriptorSetKHR = radv_CmdPushDescriptorSetKHR,
    .TrimCommandPoolKHR = radv_TrimCommandPoolKHR,
    .GetPhysicalDeviceExternalBufferPropertiesKHR = radv_GetPhysicalDeviceExternalBufferPropertiesKHR,
    .GetMemoryFdKHR = radv_GetMemoryFdKHR,
    .GetMemoryFdPropertiesKHR = radv_GetMemoryFdPropertiesKHR,
    .GetPhysicalDeviceExternalSemaphorePropertiesKHR = radv_GetPhysicalDeviceExternalSemaphorePropertiesKHR,
    .GetSemaphoreFdKHR = radv_GetSemaphoreFdKHR,
    .ImportSemaphoreFdKHR = radv_ImportSemaphoreFdKHR,
    .BindBufferMemory2KHR = radv_BindBufferMemory2KHR,
    .BindImageMemory2KHR = radv_BindImageMemory2KHR,
    .CreateDescriptorUpdateTemplateKHR = radv_CreateDescriptorUpdateTemplateKHR,
    .DestroyDescriptorUpdateTemplateKHR = radv_DestroyDescriptorUpdateTemplateKHR,
    .UpdateDescriptorSetWithTemplateKHR = radv_UpdateDescriptorSetWithTemplateKHR,
    .CmdPushDescriptorSetWithTemplateKHR = radv_CmdPushDescriptorSetWithTemplateKHR,
    .GetBufferMemoryRequirements2KHR = radv_GetBufferMemoryRequirements2KHR,
    .GetImageMemoryRequirements2KHR = radv_GetImageMemoryRequirements2KHR,
    .GetImageSparseMemoryRequirements2KHR = radv_GetImageSparseMemoryRequirements2KHR,
  };

static void * __attribute__ ((noinline))
radv_resolve_entrypoint(uint32_t index)
{
   return radv_layer.entrypoints[index];
}

/* Hash table stats:
 * size 256 entries
 * collisions entries:
 *     0     115
 *     1     32
 *     2     12
 *     3     9
 *     4     4
 *     5     1
 *     6     3
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
      0x00a6,
      0x002b,
      0x0040,
      0x0061,
      0x0049,
      0x0022,
      0x0056,
      none,
      none,
      none,
      0x00a4,
      none,
      none,
      0x00a5,
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
      0x00ad,
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
      0x009f,
      0x004d,
      0x0090,
      0x0024,
      0x00a0,
      0x005e,
      0x000b,
      0x0088,
      0x0091,
      none,
      0x00ae,
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
      0x0099,
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
      0x009b,
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
      0x00b2,
      0x00a1,
      0x003e,
      0x0048,
      0x007b,
      0x0055,
      0x00a9,
      none,
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
      0x00b1,
      0x005d,
      0x008a,
      0x0003,
      0x008f,
      none,
      0x0063,
      0x0006,
      none,
      0x0093,
      0x00a3,
      none,
      none,
      none,
      0x0059,
      0x0026,
      none,
      0x003c,
      none,
      0x0037,
      0x00a8,
      0x0009,
      0x0038,
      0x0011,
      none,
      0x0072,
      0x0016,
      none,
      0x003d,
      0x00b0,
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
      0x00ac,
      0x004e,
      0x0095,
      0x0031,
      0x00a2,
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
      0x009a,
      none,
      0x0036,
      none,
      0x0050,
      0x009c,
      0x007d,
      none,
      0x008c,
      0x0005,
      0x001a,
      0x000c,
      0x0098,
      0x00a7,
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
      0x009d,
      0x0064,
      0x0085,
      none,
      none,
      0x00ab,
      0x000f,
      0x007e,
      none,
      0x009e,
      0x0017,
      0x0012,
      0x0010,
      none,
      0x0021,
      0x008b,
      0x0079,
      0x0001,
      0x00af,
      0x00aa,
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
radv_lookup_entrypoint(const char *name)
{
   static const uint32_t prime_factor = 5024183;
   static const uint32_t prime_step = 19;
   const struct radv_entrypoint *e;
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

   return radv_resolve_entrypoint(i);
}