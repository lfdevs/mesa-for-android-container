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

struct string_map_entry {
   uint32_t name;
   uint32_t hash;
   uint32_t num;
};

/* We use a big string constant to avoid lots of relocations from the entry
 * point table to lots of little strings. The entries in the entry point table
 * store the index into this big string.
 */

static const char strings[] =
    "vkAcquireImageANDROID\0"
    "vkAcquireNextImage2KHR\0"
    "vkAcquireNextImageKHR\0"
    "vkAcquireXlibDisplayEXT\0"
    "vkAllocateCommandBuffers\0"
    "vkAllocateDescriptorSets\0"
    "vkAllocateMemory\0"
    "vkBeginCommandBuffer\0"
    "vkBindBufferMemory\0"
    "vkBindBufferMemory2\0"
    "vkBindBufferMemory2KHR\0"
    "vkBindImageMemory\0"
    "vkBindImageMemory2\0"
    "vkBindImageMemory2KHR\0"
    "vkCmdBeginConditionalRenderingEXT\0"
    "vkCmdBeginQuery\0"
    "vkCmdBeginRenderPass\0"
    "vkCmdBeginRenderPass2KHR\0"
    "vkCmdBindDescriptorSets\0"
    "vkCmdBindIndexBuffer\0"
    "vkCmdBindPipeline\0"
    "vkCmdBindVertexBuffers\0"
    "vkCmdBlitImage\0"
    "vkCmdClearAttachments\0"
    "vkCmdClearColorImage\0"
    "vkCmdClearDepthStencilImage\0"
    "vkCmdCopyBuffer\0"
    "vkCmdCopyBufferToImage\0"
    "vkCmdCopyImage\0"
    "vkCmdCopyImageToBuffer\0"
    "vkCmdCopyQueryPoolResults\0"
    "vkCmdDispatch\0"
    "vkCmdDispatchBase\0"
    "vkCmdDispatchBaseKHR\0"
    "vkCmdDispatchIndirect\0"
    "vkCmdDraw\0"
    "vkCmdDrawIndexed\0"
    "vkCmdDrawIndexedIndirect\0"
    "vkCmdDrawIndexedIndirectCountAMD\0"
    "vkCmdDrawIndexedIndirectCountKHR\0"
    "vkCmdDrawIndirect\0"
    "vkCmdDrawIndirectCountAMD\0"
    "vkCmdDrawIndirectCountKHR\0"
    "vkCmdEndConditionalRenderingEXT\0"
    "vkCmdEndQuery\0"
    "vkCmdEndRenderPass\0"
    "vkCmdEndRenderPass2KHR\0"
    "vkCmdExecuteCommands\0"
    "vkCmdFillBuffer\0"
    "vkCmdNextSubpass\0"
    "vkCmdNextSubpass2KHR\0"
    "vkCmdPipelineBarrier\0"
    "vkCmdPushConstants\0"
    "vkCmdPushDescriptorSetKHR\0"
    "vkCmdPushDescriptorSetWithTemplateKHR\0"
    "vkCmdResetEvent\0"
    "vkCmdResetQueryPool\0"
    "vkCmdResolveImage\0"
    "vkCmdSetBlendConstants\0"
    "vkCmdSetDepthBias\0"
    "vkCmdSetDepthBounds\0"
    "vkCmdSetDeviceMask\0"
    "vkCmdSetDeviceMaskKHR\0"
    "vkCmdSetDiscardRectangleEXT\0"
    "vkCmdSetEvent\0"
    "vkCmdSetLineWidth\0"
    "vkCmdSetScissor\0"
    "vkCmdSetStencilCompareMask\0"
    "vkCmdSetStencilReference\0"
    "vkCmdSetStencilWriteMask\0"
    "vkCmdSetViewport\0"
    "vkCmdUpdateBuffer\0"
    "vkCmdWaitEvents\0"
    "vkCmdWriteTimestamp\0"
    "vkCreateBuffer\0"
    "vkCreateBufferView\0"
    "vkCreateCommandPool\0"
    "vkCreateComputePipelines\0"
    "vkCreateDebugReportCallbackEXT\0"
    "vkCreateDescriptorPool\0"
    "vkCreateDescriptorSetLayout\0"
    "vkCreateDescriptorUpdateTemplate\0"
    "vkCreateDescriptorUpdateTemplateKHR\0"
    "vkCreateDevice\0"
    "vkCreateDisplayModeKHR\0"
    "vkCreateDisplayPlaneSurfaceKHR\0"
    "vkCreateEvent\0"
    "vkCreateFence\0"
    "vkCreateFramebuffer\0"
    "vkCreateGraphicsPipelines\0"
    "vkCreateImage\0"
    "vkCreateImageView\0"
    "vkCreateInstance\0"
    "vkCreatePipelineCache\0"
    "vkCreatePipelineLayout\0"
    "vkCreateQueryPool\0"
    "vkCreateRenderPass\0"
    "vkCreateRenderPass2KHR\0"
    "vkCreateSampler\0"
    "vkCreateSamplerYcbcrConversion\0"
    "vkCreateSemaphore\0"
    "vkCreateShaderModule\0"
    "vkCreateSwapchainKHR\0"
    "vkCreateWaylandSurfaceKHR\0"
    "vkCreateXcbSurfaceKHR\0"
    "vkCreateXlibSurfaceKHR\0"
    "vkDebugReportMessageEXT\0"
    "vkDestroyBuffer\0"
    "vkDestroyBufferView\0"
    "vkDestroyCommandPool\0"
    "vkDestroyDebugReportCallbackEXT\0"
    "vkDestroyDescriptorPool\0"
    "vkDestroyDescriptorSetLayout\0"
    "vkDestroyDescriptorUpdateTemplate\0"
    "vkDestroyDescriptorUpdateTemplateKHR\0"
    "vkDestroyDevice\0"
    "vkDestroyEvent\0"
    "vkDestroyFence\0"
    "vkDestroyFramebuffer\0"
    "vkDestroyImage\0"
    "vkDestroyImageView\0"
    "vkDestroyInstance\0"
    "vkDestroyPipeline\0"
    "vkDestroyPipelineCache\0"
    "vkDestroyPipelineLayout\0"
    "vkDestroyQueryPool\0"
    "vkDestroyRenderPass\0"
    "vkDestroySampler\0"
    "vkDestroySamplerYcbcrConversion\0"
    "vkDestroySemaphore\0"
    "vkDestroyShaderModule\0"
    "vkDestroySurfaceKHR\0"
    "vkDestroySwapchainKHR\0"
    "vkDeviceWaitIdle\0"
    "vkDisplayPowerControlEXT\0"
    "vkEndCommandBuffer\0"
    "vkEnumerateDeviceExtensionProperties\0"
    "vkEnumerateDeviceLayerProperties\0"
    "vkEnumerateInstanceExtensionProperties\0"
    "vkEnumerateInstanceLayerProperties\0"
    "vkEnumerateInstanceVersion\0"
    "vkEnumeratePhysicalDeviceGroups\0"
    "vkEnumeratePhysicalDeviceGroupsKHR\0"
    "vkEnumeratePhysicalDevices\0"
    "vkFlushMappedMemoryRanges\0"
    "vkFreeCommandBuffers\0"
    "vkFreeDescriptorSets\0"
    "vkFreeMemory\0"
    "vkGetBufferMemoryRequirements\0"
    "vkGetBufferMemoryRequirements2\0"
    "vkGetBufferMemoryRequirements2KHR\0"
    "vkGetDescriptorSetLayoutSupport\0"
    "vkGetDescriptorSetLayoutSupportKHR\0"
    "vkGetDeviceGroupPeerMemoryFeatures\0"
    "vkGetDeviceGroupPeerMemoryFeaturesKHR\0"
    "vkGetDeviceGroupPresentCapabilitiesKHR\0"
    "vkGetDeviceGroupSurfacePresentModesKHR\0"
    "vkGetDeviceMemoryCommitment\0"
    "vkGetDeviceProcAddr\0"
    "vkGetDeviceQueue\0"
    "vkGetDeviceQueue2\0"
    "vkGetDisplayModeProperties2KHR\0"
    "vkGetDisplayModePropertiesKHR\0"
    "vkGetDisplayPlaneCapabilities2KHR\0"
    "vkGetDisplayPlaneCapabilitiesKHR\0"
    "vkGetDisplayPlaneSupportedDisplaysKHR\0"
    "vkGetEventStatus\0"
    "vkGetFenceFdKHR\0"
    "vkGetFenceStatus\0"
    "vkGetImageMemoryRequirements\0"
    "vkGetImageMemoryRequirements2\0"
    "vkGetImageMemoryRequirements2KHR\0"
    "vkGetImageSparseMemoryRequirements\0"
    "vkGetImageSparseMemoryRequirements2\0"
    "vkGetImageSparseMemoryRequirements2KHR\0"
    "vkGetImageSubresourceLayout\0"
    "vkGetInstanceProcAddr\0"
    "vkGetMemoryFdKHR\0"
    "vkGetMemoryFdPropertiesKHR\0"
    "vkGetMemoryHostPointerPropertiesEXT\0"
    "vkGetPhysicalDeviceDisplayPlaneProperties2KHR\0"
    "vkGetPhysicalDeviceDisplayPlanePropertiesKHR\0"
    "vkGetPhysicalDeviceDisplayProperties2KHR\0"
    "vkGetPhysicalDeviceDisplayPropertiesKHR\0"
    "vkGetPhysicalDeviceExternalBufferProperties\0"
    "vkGetPhysicalDeviceExternalBufferPropertiesKHR\0"
    "vkGetPhysicalDeviceExternalFenceProperties\0"
    "vkGetPhysicalDeviceExternalFencePropertiesKHR\0"
    "vkGetPhysicalDeviceExternalSemaphoreProperties\0"
    "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR\0"
    "vkGetPhysicalDeviceFeatures\0"
    "vkGetPhysicalDeviceFeatures2\0"
    "vkGetPhysicalDeviceFeatures2KHR\0"
    "vkGetPhysicalDeviceFormatProperties\0"
    "vkGetPhysicalDeviceFormatProperties2\0"
    "vkGetPhysicalDeviceFormatProperties2KHR\0"
    "vkGetPhysicalDeviceImageFormatProperties\0"
    "vkGetPhysicalDeviceImageFormatProperties2\0"
    "vkGetPhysicalDeviceImageFormatProperties2KHR\0"
    "vkGetPhysicalDeviceMemoryProperties\0"
    "vkGetPhysicalDeviceMemoryProperties2\0"
    "vkGetPhysicalDeviceMemoryProperties2KHR\0"
    "vkGetPhysicalDevicePresentRectanglesKHR\0"
    "vkGetPhysicalDeviceProperties\0"
    "vkGetPhysicalDeviceProperties2\0"
    "vkGetPhysicalDeviceProperties2KHR\0"
    "vkGetPhysicalDeviceQueueFamilyProperties\0"
    "vkGetPhysicalDeviceQueueFamilyProperties2\0"
    "vkGetPhysicalDeviceQueueFamilyProperties2KHR\0"
    "vkGetPhysicalDeviceSparseImageFormatProperties\0"
    "vkGetPhysicalDeviceSparseImageFormatProperties2\0"
    "vkGetPhysicalDeviceSparseImageFormatProperties2KHR\0"
    "vkGetPhysicalDeviceSurfaceCapabilities2EXT\0"
    "vkGetPhysicalDeviceSurfaceCapabilities2KHR\0"
    "vkGetPhysicalDeviceSurfaceCapabilitiesKHR\0"
    "vkGetPhysicalDeviceSurfaceFormats2KHR\0"
    "vkGetPhysicalDeviceSurfaceFormatsKHR\0"
    "vkGetPhysicalDeviceSurfacePresentModesKHR\0"
    "vkGetPhysicalDeviceSurfaceSupportKHR\0"
    "vkGetPhysicalDeviceWaylandPresentationSupportKHR\0"
    "vkGetPhysicalDeviceXcbPresentationSupportKHR\0"
    "vkGetPhysicalDeviceXlibPresentationSupportKHR\0"
    "vkGetPipelineCacheData\0"
    "vkGetQueryPoolResults\0"
    "vkGetRandROutputDisplayEXT\0"
    "vkGetRenderAreaGranularity\0"
    "vkGetSemaphoreFdKHR\0"
    "vkGetShaderInfoAMD\0"
    "vkGetSwapchainCounterEXT\0"
    "vkGetSwapchainGrallocUsageANDROID\0"
    "vkGetSwapchainImagesKHR\0"
    "vkImportFenceFdKHR\0"
    "vkImportSemaphoreFdKHR\0"
    "vkInvalidateMappedMemoryRanges\0"
    "vkMapMemory\0"
    "vkMergePipelineCaches\0"
    "vkQueueBindSparse\0"
    "vkQueuePresentKHR\0"
    "vkQueueSignalReleaseImageANDROID\0"
    "vkQueueSubmit\0"
    "vkQueueWaitIdle\0"
    "vkRegisterDeviceEventEXT\0"
    "vkRegisterDisplayEventEXT\0"
    "vkReleaseDisplayEXT\0"
    "vkResetCommandBuffer\0"
    "vkResetCommandPool\0"
    "vkResetDescriptorPool\0"
    "vkResetEvent\0"
    "vkResetFences\0"
    "vkSetEvent\0"
    "vkTrimCommandPool\0"
    "vkTrimCommandPoolKHR\0"
    "vkUnmapMemory\0"
    "vkUpdateDescriptorSetWithTemplate\0"
    "vkUpdateDescriptorSetWithTemplateKHR\0"
    "vkUpdateDescriptorSets\0"
    "vkWaitForFences\0"
;

static const struct string_map_entry string_map_entries[] = {
    { 0, 0x6bf780dd, 247 }, /* vkAcquireImageANDROID */
    { 22, 0x82860572, 217 }, /* vkAcquireNextImage2KHR */
    { 45, 0xc3fedb2e, 155 }, /* vkAcquireNextImageKHR */
    { 67, 0x60df100d, 198 }, /* vkAcquireXlibDisplayEXT */
    { 91, 0x8c0c811a, 89 }, /* vkAllocateCommandBuffers */
    { 116, 0x4c449d3a, 78 }, /* vkAllocateDescriptorSets */
    { 141, 0x522b85d3, 22 }, /* vkAllocateMemory */
    { 158, 0xc54f7327, 91 }, /* vkBeginCommandBuffer */
    { 179, 0x6bcbdcb, 30 }, /* vkBindBufferMemory */
    { 198, 0xc27aaf4f, 209 }, /* vkBindBufferMemory2 */
    { 218, 0x6878d3ce, 210 }, /* vkBindBufferMemory2KHR */
    { 241, 0x5caaae4a, 32 }, /* vkBindImageMemory */
    { 259, 0xa9097118, 211 }, /* vkBindImageMemory2 */
    { 278, 0xf18729ad, 212 }, /* vkBindImageMemory2KHR */
    { 300, 0xe561c19f, 130 }, /* vkCmdBeginConditionalRenderingEXT */
    { 334, 0xf5064ea4, 128 }, /* vkCmdBeginQuery */
    { 350, 0xcb7a58e3, 136 }, /* vkCmdBeginRenderPass */
    { 371, 0x8b6b4de6, 252 }, /* vkCmdBeginRenderPass2KHR */
    { 396, 0x28c7a5da, 104 }, /* vkCmdBindDescriptorSets */
    { 420, 0x4c22d870, 105 }, /* vkCmdBindIndexBuffer */
    { 441, 0x3af9fd84, 94 }, /* vkCmdBindPipeline */
    { 459, 0xa9c83f1d, 106 }, /* vkCmdBindVertexBuffers */
    { 482, 0x331ebf89, 115 }, /* vkCmdBlitImage */
    { 497, 0x93cb5cb8, 122 }, /* vkCmdClearAttachments */
    { 519, 0xb4bc8d08, 120 }, /* vkCmdClearColorImage */
    { 540, 0x4f88e4ba, 121 }, /* vkCmdClearDepthStencilImage */
    { 568, 0xc939a0da, 113 }, /* vkCmdCopyBuffer */
    { 584, 0x929847e, 116 }, /* vkCmdCopyBufferToImage */
    { 607, 0x278effa9, 114 }, /* vkCmdCopyImage */
    { 622, 0x68cddbac, 117 }, /* vkCmdCopyImageToBuffer */
    { 645, 0xdee8c6d4, 134 }, /* vkCmdCopyQueryPoolResults */
    { 671, 0xbd58e867, 111 }, /* vkCmdDispatch */
    { 685, 0xfb767220, 218 }, /* vkCmdDispatchBase */
    { 703, 0x402403e5, 219 }, /* vkCmdDispatchBaseKHR */
    { 724, 0xd6353005, 112 }, /* vkCmdDispatchIndirect */
    { 746, 0x9912c1a1, 107 }, /* vkCmdDraw */
    { 756, 0xbe5a8058, 108 }, /* vkCmdDrawIndexed */
    { 773, 0x94e7ed36, 110 }, /* vkCmdDrawIndexedIndirect */
    { 798, 0xc86e9287, 167 }, /* vkCmdDrawIndexedIndirectCountAMD */
    { 831, 0xda9e8a2c, 256 }, /* vkCmdDrawIndexedIndirectCountKHR */
    { 864, 0xe9ac41bf, 109 }, /* vkCmdDrawIndirect */
    { 882, 0xe5ad0a50, 166 }, /* vkCmdDrawIndirectCountAMD */
    { 908, 0xf7dd01f5, 255 }, /* vkCmdDrawIndirectCountKHR */
    { 934, 0x18c8217d, 131 }, /* vkCmdEndConditionalRenderingEXT */
    { 966, 0xd556fd22, 129 }, /* vkCmdEndQuery */
    { 980, 0xdcdb0235, 138 }, /* vkCmdEndRenderPass */
    { 999, 0x57eebe78, 254 }, /* vkCmdEndRenderPass2KHR */
    { 1022, 0x9eaabe40, 139 }, /* vkCmdExecuteCommands */
    { 1043, 0x5bdd2ae0, 119 }, /* vkCmdFillBuffer */
    { 1059, 0x2eeec2f9, 137 }, /* vkCmdNextSubpass */
    { 1076, 0x25b621bc, 253 }, /* vkCmdNextSubpass2KHR */
    { 1097, 0x97fccfe8, 127 }, /* vkCmdPipelineBarrier */
    { 1118, 0xb1c6b468, 135 }, /* vkCmdPushConstants */
    { 1137, 0xf17232a1, 182 }, /* vkCmdPushDescriptorSetKHR */
    { 1163, 0x3d528981, 227 }, /* vkCmdPushDescriptorSetWithTemplateKHR */
    { 1201, 0x4fccce28, 125 }, /* vkCmdResetEvent */
    { 1217, 0x2f614082, 132 }, /* vkCmdResetQueryPool */
    { 1237, 0x671bb594, 123 }, /* vkCmdResolveImage */
    { 1255, 0x1c989dfb, 99 }, /* vkCmdSetBlendConstants */
    { 1278, 0x30f14d07, 98 }, /* vkCmdSetDepthBias */
    { 1296, 0x7b3a8a63, 100 }, /* vkCmdSetDepthBounds */
    { 1316, 0xaecdae87, 213 }, /* vkCmdSetDeviceMask */
    { 1335, 0xfbb79356, 214 }, /* vkCmdSetDeviceMaskKHR */
    { 1357, 0x64df188b, 228 }, /* vkCmdSetDiscardRectangleEXT */
    { 1385, 0xe257f075, 124 }, /* vkCmdSetEvent */
    { 1399, 0x32282165, 97 }, /* vkCmdSetLineWidth */
    { 1417, 0x48f28c7f, 96 }, /* vkCmdSetScissor */
    { 1433, 0xa8f534e2, 101 }, /* vkCmdSetStencilCompareMask */
    { 1460, 0x83e2b024, 103 }, /* vkCmdSetStencilReference */
    { 1485, 0xe7c4b134, 102 }, /* vkCmdSetStencilWriteMask */
    { 1510, 0x53d6c2b, 95 }, /* vkCmdSetViewport */
    { 1527, 0xd2986b5e, 118 }, /* vkCmdUpdateBuffer */
    { 1545, 0x3b9346b3, 126 }, /* vkCmdWaitEvents */
    { 1561, 0xec4d324c, 133 }, /* vkCmdWriteTimestamp */
    { 1581, 0x7d4282b9, 51 }, /* vkCreateBuffer */
    { 1596, 0x925bd256, 53 }, /* vkCreateBufferView */
    { 1615, 0x820fe476, 86 }, /* vkCreateCommandPool */
    { 1635, 0xf70c85eb, 67 }, /* vkCreateComputePipelines */
    { 1660, 0x987ef56, 163 }, /* vkCreateDebugReportCallbackEXT */
    { 1691, 0xfb95a8a4, 75 }, /* vkCreateDescriptorPool */
    { 1714, 0x3c14cc74, 73 }, /* vkCreateDescriptorSetLayout */
    { 1742, 0xad3ce733, 221 }, /* vkCreateDescriptorUpdateTemplate */
    { 1775, 0x5189488a, 222 }, /* vkCreateDescriptorUpdateTemplateKHR */
    { 1811, 0x85ed23f, 11 }, /* vkCreateDevice */
    { 1826, 0xcc0bde41, 144 }, /* vkCreateDisplayModeKHR */
    { 1849, 0x7ac4dacb, 146 }, /* vkCreateDisplayPlaneSurfaceKHR */
    { 1880, 0xe7188731, 43 }, /* vkCreateEvent */
    { 1894, 0x958af968, 36 }, /* vkCreateFence */
    { 1908, 0x887a38c4, 81 }, /* vkCreateFramebuffer */
    { 1928, 0x4b59f96d, 66 }, /* vkCreateGraphicsPipelines */
    { 1954, 0x652128c2, 55 }, /* vkCreateImage */
    { 1968, 0xdce077ff, 58 }, /* vkCreateImageView */
    { 1986, 0x38a581a6, 0 }, /* vkCreateInstance */
    { 2003, 0xcbf6489f, 62 }, /* vkCreatePipelineCache */
    { 2025, 0x451ef1ed, 69 }, /* vkCreatePipelineLayout */
    { 2048, 0x5edcd92b, 48 }, /* vkCreateQueryPool */
    { 2066, 0x109a9c18, 83 }, /* vkCreateRenderPass */
    { 2085, 0xfa16043b, 251 }, /* vkCreateRenderPass2KHR */
    { 2108, 0x13cf03f, 71 }, /* vkCreateSampler */
    { 2124, 0xe6a58c26, 241 }, /* vkCreateSamplerYcbcrConversion */
    { 2155, 0xf2065e5b, 41 }, /* vkCreateSemaphore */
    { 2173, 0xa0d3cea2, 60 }, /* vkCreateShaderModule */
    { 2194, 0xcdefcaa8, 152 }, /* vkCreateSwapchainKHR */
    { 2215, 0x2b2a4b79, 157 }, /* vkCreateWaylandSurfaceKHR */
    { 2241, 0xc5e5b106, 161 }, /* vkCreateXcbSurfaceKHR */
    { 2263, 0xa693bc66, 159 }, /* vkCreateXlibSurfaceKHR */
    { 2286, 0xa4e75334, 165 }, /* vkDebugReportMessageEXT */
    { 2310, 0x94a07a45, 52 }, /* vkDestroyBuffer */
    { 2326, 0x98b27962, 54 }, /* vkDestroyBufferView */
    { 2346, 0xd5d83a0a, 87 }, /* vkDestroyCommandPool */
    { 2367, 0x43d4c4e2, 164 }, /* vkDestroyDebugReportCallbackEXT */
    { 2399, 0x47bdaf30, 76 }, /* vkDestroyDescriptorPool */
    { 2423, 0xa4227b08, 74 }, /* vkDestroyDescriptorSetLayout */
    { 2452, 0xbb2cbe7f, 223 }, /* vkDestroyDescriptorUpdateTemplate */
    { 2486, 0xaa83901e, 224 }, /* vkDestroyDescriptorUpdateTemplateKHR */
    { 2523, 0x1fbcc9cb, 12 }, /* vkDestroyDevice */
    { 2539, 0x4df27c05, 44 }, /* vkDestroyEvent */
    { 2554, 0xfc64ee3c, 37 }, /* vkDestroyFence */
    { 2569, 0xdc428e58, 82 }, /* vkDestroyFramebuffer */
    { 2590, 0xcbfb1d96, 56 }, /* vkDestroyImage */
    { 2605, 0xb5853953, 59 }, /* vkDestroyImageView */
    { 2624, 0x9bd21af2, 1 }, /* vkDestroyInstance */
    { 2642, 0x6aac68af, 68 }, /* vkDestroyPipeline */
    { 2660, 0x4112a673, 63 }, /* vkDestroyPipelineCache */
    { 2683, 0x9146f879, 70 }, /* vkDestroyPipelineLayout */
    { 2707, 0x37819a7f, 49 }, /* vkDestroyQueryPool */
    { 2726, 0x16f14324, 84 }, /* vkDestroyRenderPass */
    { 2746, 0x3b645153, 72 }, /* vkDestroySampler */
    { 2763, 0x20f261b2, 242 }, /* vkDestroySamplerYcbcrConversion */
    { 2795, 0xcaab1faf, 42 }, /* vkDestroySemaphore */
    { 2814, 0x2d77af6e, 61 }, /* vkDestroyShaderModule */
    { 2836, 0xf204ce7d, 147 }, /* vkDestroySurfaceKHR */
    { 2856, 0x5a93ab74, 153 }, /* vkDestroySwapchainKHR */
    { 2878, 0xd46c5f24, 21 }, /* vkDeviceWaitIdle */
    { 2895, 0xdbb064, 200 }, /* vkDisplayPowerControlEXT */
    { 2920, 0xaffb5725, 92 }, /* vkEndCommandBuffer */
    { 2939, 0x5fd13eed, 17 }, /* vkEnumerateDeviceExtensionProperties */
    { 2976, 0x2f8566e7, 16 }, /* vkEnumerateDeviceLayerProperties */
    { 3009, 0xeb27627e, 15 }, /* vkEnumerateInstanceExtensionProperties */
    { 3048, 0x81f69d8, 14 }, /* vkEnumerateInstanceLayerProperties */
    { 3083, 0xd0481e5c, 13 }, /* vkEnumerateInstanceVersion */
    { 3110, 0x270514f0, 205 }, /* vkEnumeratePhysicalDeviceGroups */
    { 3142, 0x549ce595, 206 }, /* vkEnumeratePhysicalDeviceGroupsKHR */
    { 3177, 0x5787c327, 2 }, /* vkEnumeratePhysicalDevices */
    { 3204, 0xff52f051, 26 }, /* vkFlushMappedMemoryRanges */
    { 3230, 0xb9db2b91, 90 }, /* vkFreeCommandBuffers */
    { 3251, 0x7a1347b1, 79 }, /* vkFreeDescriptorSets */
    { 3272, 0x8f6f838a, 23 }, /* vkFreeMemory */
    { 3285, 0xab98422a, 29 }, /* vkGetBufferMemoryRequirements */
    { 3315, 0xd1fd0638, 235 }, /* vkGetBufferMemoryRequirements2 */
    { 3346, 0x78dbe98d, 236 }, /* vkGetBufferMemoryRequirements2KHR */
    { 3380, 0xfeac9573, 244 }, /* vkGetDescriptorSetLayoutSupport */
    { 3412, 0xd7e44a, 245 }, /* vkGetDescriptorSetLayoutSupportKHR */
    { 3447, 0x2e218c10, 207 }, /* vkGetDeviceGroupPeerMemoryFeatures */
    { 3482, 0xa3809375, 208 }, /* vkGetDeviceGroupPeerMemoryFeaturesKHR */
    { 3520, 0xf72c87d4, 215 }, /* vkGetDeviceGroupPresentCapabilitiesKHR */
    { 3559, 0x6b9448c3, 216 }, /* vkGetDeviceGroupSurfacePresentModesKHR */
    { 3598, 0x46e38db5, 28 }, /* vkGetDeviceMemoryCommitment */
    { 3626, 0xba013486, 3 }, /* vkGetDeviceProcAddr */
    { 3646, 0xcc920d9a, 18 }, /* vkGetDeviceQueue */
    { 3663, 0xb11a6348, 243 }, /* vkGetDeviceQueue2 */
    { 3681, 0x3e613e42, 233 }, /* vkGetDisplayModeProperties2KHR */
    { 3712, 0x36b8a8de, 143 }, /* vkGetDisplayModePropertiesKHR */
    { 3742, 0xff1655a4, 234 }, /* vkGetDisplayPlaneCapabilities2KHR */
    { 3776, 0x4b60d48c, 145 }, /* vkGetDisplayPlaneCapabilitiesKHR */
    { 3809, 0xabef4889, 142 }, /* vkGetDisplayPlaneSupportedDisplaysKHR */
    { 3847, 0x96d834b, 45 }, /* vkGetEventStatus */
    { 3864, 0x69a5d6af, 195 }, /* vkGetFenceFdKHR */
    { 3880, 0x5f391892, 39 }, /* vkGetFenceStatus */
    { 3897, 0x916f1e63, 31 }, /* vkGetImageMemoryRequirements */
    { 3926, 0x56e213f7, 237 }, /* vkGetImageMemoryRequirements2 */
    { 3956, 0x8de28366, 238 }, /* vkGetImageMemoryRequirements2KHR */
    { 3989, 0x15855f5b, 33 }, /* vkGetImageSparseMemoryRequirements */
    { 4024, 0xbd4e3d3f, 239 }, /* vkGetImageSparseMemoryRequirements2 */
    { 4060, 0x3df40f5e, 240 }, /* vkGetImageSparseMemoryRequirements2KHR */
    { 4099, 0x9163b686, 57 }, /* vkGetImageSubresourceLayout */
    { 4127, 0x3d2ae9ad, 4 }, /* vkGetInstanceProcAddr */
    { 4149, 0x503c14c5, 187 }, /* vkGetMemoryFdKHR */
    { 4166, 0xb028a792, 188 }, /* vkGetMemoryFdPropertiesKHR */
    { 4193, 0x7030ee5b, 250 }, /* vkGetMemoryHostPointerPropertiesEXT */
    { 4229, 0xb7bc4386, 232 }, /* vkGetPhysicalDeviceDisplayPlaneProperties2KHR */
    { 4275, 0xb9b8ddba, 141 }, /* vkGetPhysicalDeviceDisplayPlanePropertiesKHR */
    { 4320, 0x540c0372, 231 }, /* vkGetPhysicalDeviceDisplayProperties2KHR */
    { 4361, 0xfa0cd2e, 140 }, /* vkGetPhysicalDeviceDisplayPropertiesKHR */
    { 4401, 0x944476dc, 185 }, /* vkGetPhysicalDeviceExternalBufferProperties */
    { 4445, 0xee68b389, 186 }, /* vkGetPhysicalDeviceExternalBufferPropertiesKHR */
    { 4492, 0x3bc965eb, 193 }, /* vkGetPhysicalDeviceExternalFenceProperties */
    { 4535, 0x99b35492, 194 }, /* vkGetPhysicalDeviceExternalFencePropertiesKHR */
    { 4581, 0xcf251b0e, 189 }, /* vkGetPhysicalDeviceExternalSemaphoreProperties */
    { 4628, 0x984c3fa7, 190 }, /* vkGetPhysicalDeviceExternalSemaphorePropertiesKHR */
    { 4678, 0x113e2f33, 8 }, /* vkGetPhysicalDeviceFeatures */
    { 4706, 0x63c068a7, 168 }, /* vkGetPhysicalDeviceFeatures2 */
    { 4735, 0x6a9a3636, 169 }, /* vkGetPhysicalDeviceFeatures2KHR */
    { 4767, 0x3e54b398, 9 }, /* vkGetPhysicalDeviceFormatProperties */
    { 4803, 0xca3bb9da, 172 }, /* vkGetPhysicalDeviceFormatProperties2 */
    { 4840, 0x9099cbbb, 173 }, /* vkGetPhysicalDeviceFormatProperties2KHR */
    { 4880, 0xdd36a867, 10 }, /* vkGetPhysicalDeviceImageFormatProperties */
    { 4921, 0x35d260d3, 174 }, /* vkGetPhysicalDeviceImageFormatProperties2 */
    { 4963, 0x102ff7ea, 175 }, /* vkGetPhysicalDeviceImageFormatProperties2KHR */
    { 5008, 0xa90da4da, 7 }, /* vkGetPhysicalDeviceMemoryProperties */
    { 5044, 0xcb4cc208, 178 }, /* vkGetPhysicalDeviceMemoryProperties2 */
    { 5081, 0xc8c3da3d, 179 }, /* vkGetPhysicalDeviceMemoryProperties2KHR */
    { 5121, 0x100341b4, 220 }, /* vkGetPhysicalDevicePresentRectanglesKHR */
    { 5161, 0x52fe22c9, 5 }, /* vkGetPhysicalDeviceProperties */
    { 5191, 0x6c4d8ee1, 170 }, /* vkGetPhysicalDeviceProperties2 */
    { 5222, 0xcd15838c, 171 }, /* vkGetPhysicalDeviceProperties2KHR */
    { 5256, 0x4e5fc88a, 6 }, /* vkGetPhysicalDeviceQueueFamilyProperties */
    { 5297, 0xcad374d8, 176 }, /* vkGetPhysicalDeviceQueueFamilyProperties2 */
    { 5339, 0x5ceb2bed, 177 }, /* vkGetPhysicalDeviceQueueFamilyProperties2KHR */
    { 5384, 0x272ef8ef, 34 }, /* vkGetPhysicalDeviceSparseImageFormatProperties */
    { 5431, 0xebddba0b, 180 }, /* vkGetPhysicalDeviceSparseImageFormatProperties2 */
    { 5479, 0x8746ed72, 181 }, /* vkGetPhysicalDeviceSparseImageFormatProperties2KHR */
    { 5530, 0x5a5fba04, 204 }, /* vkGetPhysicalDeviceSurfaceCapabilities2EXT */
    { 5573, 0x9497e378, 229 }, /* vkGetPhysicalDeviceSurfaceCapabilities2KHR */
    { 5616, 0x77890558, 149 }, /* vkGetPhysicalDeviceSurfaceCapabilitiesKHR */
    { 5658, 0xd00b7188, 230 }, /* vkGetPhysicalDeviceSurfaceFormats2KHR */
    { 5696, 0xe32227c8, 150 }, /* vkGetPhysicalDeviceSurfaceFormatsKHR */
    { 5733, 0x31c3cbd1, 151 }, /* vkGetPhysicalDeviceSurfacePresentModesKHR */
    { 5775, 0x1a687885, 148 }, /* vkGetPhysicalDeviceSurfaceSupportKHR */
    { 5812, 0x84e085ac, 158 }, /* vkGetPhysicalDeviceWaylandPresentationSupportKHR */
    { 5861, 0x41782cb9, 162 }, /* vkGetPhysicalDeviceXcbPresentationSupportKHR */
    { 5906, 0x34a063ab, 160 }, /* vkGetPhysicalDeviceXlibPresentationSupportKHR */
    { 5952, 0x2092a349, 64 }, /* vkGetPipelineCacheData */
    { 5975, 0xbf3f2cb3, 50 }, /* vkGetQueryPoolResults */
    { 5997, 0xb87cdd6c, 199 }, /* vkGetRandROutputDisplayEXT */
    { 6024, 0xa9820d22, 85 }, /* vkGetRenderAreaGranularity */
    { 6051, 0x3e0e9884, 191 }, /* vkGetSemaphoreFdKHR */
    { 6071, 0x5330743c, 249 }, /* vkGetShaderInfoAMD */
    { 6090, 0xa4aeb5a, 203 }, /* vkGetSwapchainCounterEXT */
    { 6115, 0x4979c9a3, 246 }, /* vkGetSwapchainGrallocUsageANDROID */
    { 6149, 0x57695f28, 154 }, /* vkGetSwapchainImagesKHR */
    { 6173, 0x51df0390, 196 }, /* vkImportFenceFdKHR */
    { 6192, 0x36337c05, 192 }, /* vkImportSemaphoreFdKHR */
    { 6215, 0x1e115cca, 27 }, /* vkInvalidateMappedMemoryRanges */
    { 6246, 0xcb977bd8, 24 }, /* vkMapMemory */
    { 6258, 0xc3499606, 65 }, /* vkMergePipelineCaches */
    { 6280, 0xc3628a09, 35 }, /* vkQueueBindSparse */
    { 6298, 0xfc5fb6ce, 156 }, /* vkQueuePresentKHR */
    { 6316, 0xa0313eef, 248 }, /* vkQueueSignalReleaseImageANDROID */
    { 6349, 0xfa4713ec, 19 }, /* vkQueueSubmit */
    { 6363, 0x6f8fc2a5, 20 }, /* vkQueueWaitIdle */
    { 6379, 0x26cc78f5, 201 }, /* vkRegisterDeviceEventEXT */
    { 6404, 0x4a0bd849, 202 }, /* vkRegisterDisplayEventEXT */
    { 6430, 0x4207f4f1, 197 }, /* vkReleaseDisplayEXT */
    { 6450, 0x847dc731, 93 }, /* vkResetCommandBuffer */
    { 6471, 0x6da9f7fd, 88 }, /* vkResetCommandPool */
    { 6490, 0x9bd85f5, 77 }, /* vkResetDescriptorPool */
    { 6512, 0x6d373ba8, 47 }, /* vkResetEvent */
    { 6525, 0x684781dc, 38 }, /* vkResetFences */
    { 6539, 0x592ae5f5, 46 }, /* vkSetEvent */
    { 6550, 0xfef2fb38, 183 }, /* vkTrimCommandPool */
    { 6568, 0x51177c8d, 184 }, /* vkTrimCommandPoolKHR */
    { 6589, 0x1a1a0e2f, 25 }, /* vkUnmapMemory */
    { 6603, 0x5349c9d, 225 }, /* vkUpdateDescriptorSetWithTemplate */
    { 6637, 0x214ad230, 226 }, /* vkUpdateDescriptorSetWithTemplateKHR */
    { 6674, 0xbfd090ae, 80 }, /* vkUpdateDescriptorSets */
    { 6697, 0x19d64c81, 40 }, /* vkWaitForFences */
};

/* Hash table stats:
 * size 257 entries
 * collisions entries:
 *     0      203
 *     1      33
 *     2      9
 *     3      7
 *     4      0
 *     5      2
 *     6      3
 *     7      0
 *     8      0
 *     9+     0
 */

#define none 0xffff
static const uint16_t string_map[512] = {
    0x00c2,
    none,
    none,
    none,
    0x00d4,
    0x0022,
    0x00eb,
    none,
    0x00c8,
    0x00ec,
    0x006d,
    0x00d2,
    none,
    0x0003,
    none,
    none,
    0x0099,
    0x00ea,
    none,
    0x00d0,
    none,
    none,
    none,
    none,
    0x0060,
    none,
    none,
    0x00f6,
    none,
    none,
    0x0072,
    none,
    0x0020,
    none,
    none,
    none,
    0x0044,
    none,
    0x0063,
    none,
    0x0037,
    none,
    0x0094,
    0x0046,
    0x0027,
    none,
    0x00f9,
    0x00fc,
    0x00fe,
    none,
    none,
    none,
    none,
    0x002d,
    0x00c0,
    none,
    0x0095,
    none,
    none,
    0x0061,
    0x0075,
    0x00c9,
    0x0074,
    0x0053,
    0x002f,
    0x0054,
    0x00a1,
    none,
    none,
    0x006b,
    none,
    none,
    none,
    0x00f2,
    0x000b,
    none,
    0x0049,
    none,
    none,
    0x00e3,
    0x0029,
    0x0090,
    0x0062,
    none,
    none,
    none,
    0x004b,
    none,
    0x0024,
    none,
    none,
    0x0064,
    0x008c,
    0x0098,
    none,
    none,
    none,
    none,
    none,
    0x003c,
    0x0086,
    none,
    0x0069,
    0x001f,
    0x0034,
    none,
    none,
    0x0076,
    none,
    none,
    0x00b3,
    none,
    0x0013,
    none,
    none,
    0x007b,
    0x0050,
    0x0040,
    0x004c,
    0x00e8,
    0x002e,
    0x007c,
    0x00c4,
    none,
    none,
    0x0083,
    0x001b,
    0x0042,
    none,
    0x0100,
    0x0038,
    none,
    0x00e2,
    0x00da,
    0x009e,
    0x0026,
    none,
    0x00a5,
    0x0052,
    0x003f,
    0x00a4,
    0x00fb,
    none,
    none,
    none,
    0x008a,
    0x0071,
    none,
    none,
    none,
    none,
    none,
    none,
    0x00af,
    0x003d,
    none,
    0x00a9,
    0x00ce,
    none,
    0x005d,
    none,
    0x0035,
    0x0065,
    none,
    0x000f,
    0x007d,
    none,
    0x00bf,
    0x0066,
    none,
    none,
    none,
    none,
    none,
    0x00ff,
    0x007a,
    0x00fd,
    none,
    none,
    0x0048,
    none,
    none,
    none,
    0x004f,
    0x0017,
    0x004a,
    0x0019,
    none,
    none,
    none,
    none,
    none,
    none,
    none,
    0x005a,
    0x009c,
    0x0058,
    0x00b1,
    0x00df,
    none,
    none,
    0x00cb,
    0x00e9,
    0x0055,
    0x00dc,
    none,
    0x00ed,
    none,
    none,
    none,
    none,
    0x00c5,
    0x001e,
    0x00a7,
    none,
    none,
    0x00cf,
    none,
    0x001a,
    none,
    0x00b8,
    0x0000,
    0x00a2,
    none,
    0x0030,
    0x00cc,
    0x0043,
    0x0010,
    none,
    none,
    none,
    0x0089,
    none,
    none,
    none,
    none,
    none,
    0x0088,
    none,
    0x00d1,
    0x008d,
    0x00a8,
    0x0079,
    none,
    none,
    0x006e,
    none,
    none,
    none,
    0x0031,
    none,
    none,
    none,
    none,
    none,
    none,
    0x00c7,
    none,
    0x00ee,
    none,
    0x00bb,
    none,
    0x0068,
    0x003b,
    0x0018,
    none,
    none,
    none,
    none,
    none,
    0x00bc,
    none,
    none,
    none,
    none,
    none,
    none,
    none,
    none,
    0x00f0,
    0x000c,
    none,
    0x0004,
    0x0070,
    none,
    0x0015,
    none,
    none,
    none,
    none,
    0x002c,
    none,
    0x007e,
    0x0087,
    none,
    0x0007,
    0x00e6,
    none,
    0x00f3,
    0x005f,
    none,
    none,
    0x0002,
    none,
    0x006f,
    0x0056,
    none,
    0x0051,
    0x0045,
    0x00e1,
    0x0025,
    0x0085,
    0x00fa,
    none,
    0x0005,
    none,
    none,
    none,
    none,
    0x00ad,
    none,
    0x00b7,
    none,
    none,
    0x00f4,
    none,
    0x00be,
    0x006a,
    0x00a0,
    0x00de,
    none,
    0x00a6,
    none,
    0x008f,
    none,
    0x0009,
    none,
    none,
    none,
    0x0078,
    0x00f1,
    none,
    0x003e,
    none,
    0x00d6,
    none,
    0x00e4,
    0x00ac,
    none,
    none,
    0x0047,
    none,
    none,
    none,
    0x006c,
    none,
    none,
    0x0041,
    0x007f,
    none,
    0x0057,
    0x004e,
    none,
    none,
    0x00e0,
    0x0059,
    0x0082,
    none,
    none,
    0x00ae,
    0x0001,
    0x0097,
    0x0084,
    0x009a,
    none,
    none,
    0x00d5,
    0x0067,
    none,
    none,
    none,
    0x002b,
    none,
    none,
    none,
    0x0036,
    none,
    none,
    0x0014,
    0x00b6,
    0x00b4,
    none,
    0x00d7,
    0x0016,
    0x0093,
    none,
    0x00ab,
    0x0096,
    none,
    none,
    0x00e7,
    0x0091,
    0x00b2,
    none,
    0x0039,
    0x008e,
    0x0077,
    none,
    0x00c1,
    none,
    0x009f,
    none,
    0x00b9,
    none,
    none,
    0x000e,
    none,
    0x0023,
    none,
    0x00e5,
    0x00a3,
    none,
    0x005c,
    0x00bd,
    0x00f7,
    0x001c,
    none,
    0x00d3,
    0x001d,
    0x000d,
    none,
    0x0081,
    none,
    0x0092,
    0x0080,
    none,
    0x00ca,
    0x009d,
    none,
    none,
    none,
    none,
    0x00b5,
    0x00c3,
    0x0032,
    none,
    0x00dd,
    0x0028,
    0x00b0,
    none,
    none,
    none,
    none,
    0x00cd,
    none,
    none,
    0x00d8,
    none,
    none,
    0x0008,
    none,
    none,
    0x000a,
    none,
    none,
    0x00d9,
    0x00db,
    0x0006,
    0x009b,
    none,
    none,
    none,
    0x008b,
    none,
    0x0012,
    none,
    0x00f8,
    none,
    0x0073,
    none,
    none,
    none,
    none,
    none,
    none,
    0x0021,
    0x0011,
    none,
    0x0033,
    none,
    0x00c6,
    0x004d,
    0x00ef,
    0x005e,
    none,
    none,
    none,
    none,
    none,
    none,
    none,
    0x002a,
    none,
    0x00aa,
    none,
    none,
    none,
    0x003a,
    none,
    0x00f5,
    0x00ba,
    0x005b,
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
    VkResult radv_EnumerateInstanceVersion(uint32_t* pApiVersion) __attribute__ ((weak));
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
    void radv_CmdBeginConditionalRenderingEXT(VkCommandBuffer commandBuffer, const VkConditionalRenderingBeginInfoEXT* pConditionalRenderingBegin) __attribute__ ((weak));
    void radv_CmdEndConditionalRenderingEXT(VkCommandBuffer commandBuffer) __attribute__ ((weak));
    void radv_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) __attribute__ ((weak));
    void radv_CmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query) __attribute__ ((weak));
    void radv_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags) __attribute__ ((weak));
    void radv_CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues) __attribute__ ((weak));
    void radv_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents) __attribute__ ((weak));
    void radv_CmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents) __attribute__ ((weak));
    void radv_CmdEndRenderPass(VkCommandBuffer commandBuffer) __attribute__ ((weak));
    void radv_CmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    VkResult radv_GetPhysicalDeviceDisplayPropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayPropertiesKHR* pProperties) __attribute__ ((weak));
    VkResult radv_GetPhysicalDeviceDisplayPlanePropertiesKHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayPlanePropertiesKHR* pProperties) __attribute__ ((weak));
    VkResult radv_GetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice physicalDevice, uint32_t planeIndex, uint32_t* pDisplayCount, VkDisplayKHR* pDisplays) __attribute__ ((weak));
    VkResult radv_GetDisplayModePropertiesKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, uint32_t* pPropertyCount, VkDisplayModePropertiesKHR* pProperties) __attribute__ ((weak));
    VkResult radv_CreateDisplayModeKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, const VkDisplayModeCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDisplayModeKHR* pMode) __attribute__ ((weak));
    VkResult radv_GetDisplayPlaneCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkDisplayModeKHR mode, uint32_t planeIndex, VkDisplayPlaneCapabilitiesKHR* pCapabilities) __attribute__ ((weak));
    VkResult radv_CreateDisplayPlaneSurfaceKHR(VkInstance instance, const VkDisplaySurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
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
    VkResult radv_CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback) __attribute__ ((weak));
    void radv_DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void radv_DebugReportMessageEXT(VkInstance instance, VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage) __attribute__ ((weak));
    void radv_CmdDrawIndirectCountAMD(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride) __attribute__ ((weak));
    void radv_CmdDrawIndexedIndirectCountAMD(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride) __attribute__ ((weak));
    void radv_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures) __attribute__ ((weak));
          void radv_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties) __attribute__ ((weak));
          void radv_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2* pFormatProperties) __attribute__ ((weak));
          VkResult radv_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo, VkImageFormatProperties2* pImageFormatProperties) __attribute__ ((weak));
          void radv_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2* pQueueFamilyProperties) __attribute__ ((weak));
          void radv_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties) __attribute__ ((weak));
          void radv_GetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo, uint32_t* pPropertyCount, VkSparseImageFormatProperties2* pProperties) __attribute__ ((weak));
          void radv_CmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites) __attribute__ ((weak));
    void radv_TrimCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlags flags) __attribute__ ((weak));
          void radv_GetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo* pExternalBufferInfo, VkExternalBufferProperties* pExternalBufferProperties) __attribute__ ((weak));
          VkResult radv_GetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult radv_GetMemoryFdPropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, int fd, VkMemoryFdPropertiesKHR* pMemoryFdProperties) __attribute__ ((weak));
    void radv_GetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo, VkExternalSemaphoreProperties* pExternalSemaphoreProperties) __attribute__ ((weak));
          VkResult radv_GetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult radv_ImportSemaphoreFdKHR(VkDevice device, const VkImportSemaphoreFdInfoKHR* pImportSemaphoreFdInfo) __attribute__ ((weak));
    void radv_GetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo, VkExternalFenceProperties* pExternalFenceProperties) __attribute__ ((weak));
          VkResult radv_GetFenceFdKHR(VkDevice device, const VkFenceGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult radv_ImportFenceFdKHR(VkDevice device, const VkImportFenceFdInfoKHR* pImportFenceFdInfo) __attribute__ ((weak));
    VkResult radv_ReleaseDisplayEXT(VkPhysicalDevice physicalDevice, VkDisplayKHR display) __attribute__ ((weak));
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
    VkResult radv_AcquireXlibDisplayEXT(VkPhysicalDevice physicalDevice, Display* dpy, VkDisplayKHR display) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
    VkResult radv_GetRandROutputDisplayEXT(VkPhysicalDevice physicalDevice, Display* dpy, RROutput rrOutput, VkDisplayKHR* pDisplay) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT
    VkResult radv_DisplayPowerControlEXT(VkDevice device, VkDisplayKHR display, const VkDisplayPowerInfoEXT* pDisplayPowerInfo) __attribute__ ((weak));
    VkResult radv_RegisterDeviceEventEXT(VkDevice device, const VkDeviceEventInfoEXT* pDeviceEventInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence) __attribute__ ((weak));
    VkResult radv_RegisterDisplayEventEXT(VkDevice device, VkDisplayKHR display, const VkDisplayEventInfoEXT* pDisplayEventInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence) __attribute__ ((weak));
    VkResult radv_GetSwapchainCounterEXT(VkDevice device, VkSwapchainKHR swapchain, VkSurfaceCounterFlagBitsEXT counter, uint64_t* pCounterValue) __attribute__ ((weak));
    VkResult radv_GetPhysicalDeviceSurfaceCapabilities2EXT(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilities2EXT* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult radv_EnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t* pPhysicalDeviceGroupCount, VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties) __attribute__ ((weak));
          void radv_GetDeviceGroupPeerMemoryFeatures(VkDevice device, uint32_t heapIndex, uint32_t localDeviceIndex, uint32_t remoteDeviceIndex, VkPeerMemoryFeatureFlags* pPeerMemoryFeatures) __attribute__ ((weak));
          VkResult radv_BindBufferMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos) __attribute__ ((weak));
          VkResult radv_BindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos) __attribute__ ((weak));
          void radv_CmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask) __attribute__ ((weak));
          VkResult radv_GetDeviceGroupPresentCapabilitiesKHR(VkDevice device, VkDeviceGroupPresentCapabilitiesKHR* pDeviceGroupPresentCapabilities) __attribute__ ((weak));
    VkResult radv_GetDeviceGroupSurfacePresentModesKHR(VkDevice device, VkSurfaceKHR surface, VkDeviceGroupPresentModeFlagsKHR* pModes) __attribute__ ((weak));
    VkResult radv_AcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex) __attribute__ ((weak));
    void radv_CmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) __attribute__ ((weak));
          VkResult radv_GetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pRectCount, VkRect2D* pRects) __attribute__ ((weak));
    VkResult radv_CreateDescriptorUpdateTemplate(VkDevice device, const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate) __attribute__ ((weak));
          void radv_DestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
          void radv_UpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void* pData) __attribute__ ((weak));
          void radv_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplate descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) __attribute__ ((weak));
    void radv_CmdSetDiscardRectangleEXT(VkCommandBuffer commandBuffer, uint32_t firstDiscardRectangle, uint32_t discardRectangleCount, const VkRect2D* pDiscardRectangles) __attribute__ ((weak));
    VkResult radv_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult radv_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats) __attribute__ ((weak));
    VkResult radv_GetPhysicalDeviceDisplayProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayProperties2KHR* pProperties) __attribute__ ((weak));
    VkResult radv_GetPhysicalDeviceDisplayPlaneProperties2KHR(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkDisplayPlaneProperties2KHR* pProperties) __attribute__ ((weak));
    VkResult radv_GetDisplayModeProperties2KHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display, uint32_t* pPropertyCount, VkDisplayModeProperties2KHR* pProperties) __attribute__ ((weak));
    VkResult radv_GetDisplayPlaneCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkDisplayPlaneInfo2KHR* pDisplayPlaneInfo, VkDisplayPlaneCapabilities2KHR* pCapabilities) __attribute__ ((weak));
    void radv_GetBufferMemoryRequirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) __attribute__ ((weak));
          void radv_GetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) __attribute__ ((weak));
          void radv_GetImageSparseMemoryRequirements2(VkDevice device, const VkImageSparseMemoryRequirementsInfo2* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2* pSparseMemoryRequirements) __attribute__ ((weak));
          VkResult radv_CreateSamplerYcbcrConversion(VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion) __attribute__ ((weak));
    void radv_DestroySamplerYcbcrConversion(VkDevice device, VkSamplerYcbcrConversion ycbcrConversion, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void radv_GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* pQueueInfo, VkQueue* pQueue) __attribute__ ((weak));
    void radv_GetDescriptorSetLayoutSupport(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, VkDescriptorSetLayoutSupport* pSupport) __attribute__ ((weak));
      #ifdef VK_USE_PLATFORM_ANDROID_KHR
    VkResult radv_GetSwapchainGrallocUsageANDROID(VkDevice device, VkFormat format, VkImageUsageFlags imageUsage, int* grallocUsage) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_ANDROID_KHR
#ifdef VK_USE_PLATFORM_ANDROID_KHR
    VkResult radv_AcquireImageANDROID(VkDevice device, VkImage image, int nativeFenceFd, VkSemaphore semaphore, VkFence fence) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_ANDROID_KHR
#ifdef VK_USE_PLATFORM_ANDROID_KHR
    VkResult radv_QueueSignalReleaseImageANDROID(VkQueue queue, uint32_t waitSemaphoreCount, const VkSemaphore* pWaitSemaphores, VkImage image, int* pNativeFenceFd) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_ANDROID_KHR
    VkResult radv_GetShaderInfoAMD(VkDevice device, VkPipeline pipeline, VkShaderStageFlagBits shaderStage, VkShaderInfoTypeAMD infoType, size_t* pInfoSize, void* pInfo) __attribute__ ((weak));
    VkResult radv_GetMemoryHostPointerPropertiesEXT(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, const void* pHostPointer, VkMemoryHostPointerPropertiesEXT* pMemoryHostPointerProperties) __attribute__ ((weak));
    VkResult radv_CreateRenderPass2KHR(VkDevice device, const VkRenderPassCreateInfo2KHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass) __attribute__ ((weak));
    void radv_CmdBeginRenderPass2KHR(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo*      pRenderPassBegin, const VkSubpassBeginInfoKHR*      pSubpassBeginInfo) __attribute__ ((weak));
    void radv_CmdNextSubpass2KHR(VkCommandBuffer commandBuffer, const VkSubpassBeginInfoKHR*      pSubpassBeginInfo, const VkSubpassEndInfoKHR*        pSubpassEndInfo) __attribute__ ((weak));
    void radv_CmdEndRenderPass2KHR(VkCommandBuffer commandBuffer, const VkSubpassEndInfoKHR*        pSubpassEndInfo) __attribute__ ((weak));
    void radv_CmdDrawIndirectCountKHR(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride) __attribute__ ((weak));
    void radv_CmdDrawIndexedIndirectCountKHR(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount, uint32_t stride) __attribute__ ((weak));

  const struct radv_dispatch_table radv_layer = {
    .vkCreateInstance = radv_CreateInstance,
    .vkDestroyInstance = radv_DestroyInstance,
    .vkEnumeratePhysicalDevices = radv_EnumeratePhysicalDevices,
    .vkGetDeviceProcAddr = radv_GetDeviceProcAddr,
    .vkGetInstanceProcAddr = radv_GetInstanceProcAddr,
    .vkGetPhysicalDeviceProperties = radv_GetPhysicalDeviceProperties,
    .vkGetPhysicalDeviceQueueFamilyProperties = radv_GetPhysicalDeviceQueueFamilyProperties,
    .vkGetPhysicalDeviceMemoryProperties = radv_GetPhysicalDeviceMemoryProperties,
    .vkGetPhysicalDeviceFeatures = radv_GetPhysicalDeviceFeatures,
    .vkGetPhysicalDeviceFormatProperties = radv_GetPhysicalDeviceFormatProperties,
    .vkGetPhysicalDeviceImageFormatProperties = radv_GetPhysicalDeviceImageFormatProperties,
    .vkCreateDevice = radv_CreateDevice,
    .vkDestroyDevice = radv_DestroyDevice,
    .vkEnumerateInstanceVersion = radv_EnumerateInstanceVersion,
    .vkEnumerateInstanceLayerProperties = radv_EnumerateInstanceLayerProperties,
    .vkEnumerateInstanceExtensionProperties = radv_EnumerateInstanceExtensionProperties,
    .vkEnumerateDeviceLayerProperties = radv_EnumerateDeviceLayerProperties,
    .vkEnumerateDeviceExtensionProperties = radv_EnumerateDeviceExtensionProperties,
    .vkGetDeviceQueue = radv_GetDeviceQueue,
    .vkQueueSubmit = radv_QueueSubmit,
    .vkQueueWaitIdle = radv_QueueWaitIdle,
    .vkDeviceWaitIdle = radv_DeviceWaitIdle,
    .vkAllocateMemory = radv_AllocateMemory,
    .vkFreeMemory = radv_FreeMemory,
    .vkMapMemory = radv_MapMemory,
    .vkUnmapMemory = radv_UnmapMemory,
    .vkFlushMappedMemoryRanges = radv_FlushMappedMemoryRanges,
    .vkInvalidateMappedMemoryRanges = radv_InvalidateMappedMemoryRanges,
    .vkGetDeviceMemoryCommitment = radv_GetDeviceMemoryCommitment,
    .vkGetBufferMemoryRequirements = radv_GetBufferMemoryRequirements,
    .vkBindBufferMemory = radv_BindBufferMemory,
    .vkGetImageMemoryRequirements = radv_GetImageMemoryRequirements,
    .vkBindImageMemory = radv_BindImageMemory,
    .vkGetImageSparseMemoryRequirements = radv_GetImageSparseMemoryRequirements,
    .vkGetPhysicalDeviceSparseImageFormatProperties = radv_GetPhysicalDeviceSparseImageFormatProperties,
    .vkQueueBindSparse = radv_QueueBindSparse,
    .vkCreateFence = radv_CreateFence,
    .vkDestroyFence = radv_DestroyFence,
    .vkResetFences = radv_ResetFences,
    .vkGetFenceStatus = radv_GetFenceStatus,
    .vkWaitForFences = radv_WaitForFences,
    .vkCreateSemaphore = radv_CreateSemaphore,
    .vkDestroySemaphore = radv_DestroySemaphore,
    .vkCreateEvent = radv_CreateEvent,
    .vkDestroyEvent = radv_DestroyEvent,
    .vkGetEventStatus = radv_GetEventStatus,
    .vkSetEvent = radv_SetEvent,
    .vkResetEvent = radv_ResetEvent,
    .vkCreateQueryPool = radv_CreateQueryPool,
    .vkDestroyQueryPool = radv_DestroyQueryPool,
    .vkGetQueryPoolResults = radv_GetQueryPoolResults,
    .vkCreateBuffer = radv_CreateBuffer,
    .vkDestroyBuffer = radv_DestroyBuffer,
    .vkCreateBufferView = radv_CreateBufferView,
    .vkDestroyBufferView = radv_DestroyBufferView,
    .vkCreateImage = radv_CreateImage,
    .vkDestroyImage = radv_DestroyImage,
    .vkGetImageSubresourceLayout = radv_GetImageSubresourceLayout,
    .vkCreateImageView = radv_CreateImageView,
    .vkDestroyImageView = radv_DestroyImageView,
    .vkCreateShaderModule = radv_CreateShaderModule,
    .vkDestroyShaderModule = radv_DestroyShaderModule,
    .vkCreatePipelineCache = radv_CreatePipelineCache,
    .vkDestroyPipelineCache = radv_DestroyPipelineCache,
    .vkGetPipelineCacheData = radv_GetPipelineCacheData,
    .vkMergePipelineCaches = radv_MergePipelineCaches,
    .vkCreateGraphicsPipelines = radv_CreateGraphicsPipelines,
    .vkCreateComputePipelines = radv_CreateComputePipelines,
    .vkDestroyPipeline = radv_DestroyPipeline,
    .vkCreatePipelineLayout = radv_CreatePipelineLayout,
    .vkDestroyPipelineLayout = radv_DestroyPipelineLayout,
    .vkCreateSampler = radv_CreateSampler,
    .vkDestroySampler = radv_DestroySampler,
    .vkCreateDescriptorSetLayout = radv_CreateDescriptorSetLayout,
    .vkDestroyDescriptorSetLayout = radv_DestroyDescriptorSetLayout,
    .vkCreateDescriptorPool = radv_CreateDescriptorPool,
    .vkDestroyDescriptorPool = radv_DestroyDescriptorPool,
    .vkResetDescriptorPool = radv_ResetDescriptorPool,
    .vkAllocateDescriptorSets = radv_AllocateDescriptorSets,
    .vkFreeDescriptorSets = radv_FreeDescriptorSets,
    .vkUpdateDescriptorSets = radv_UpdateDescriptorSets,
    .vkCreateFramebuffer = radv_CreateFramebuffer,
    .vkDestroyFramebuffer = radv_DestroyFramebuffer,
    .vkCreateRenderPass = radv_CreateRenderPass,
    .vkDestroyRenderPass = radv_DestroyRenderPass,
    .vkGetRenderAreaGranularity = radv_GetRenderAreaGranularity,
    .vkCreateCommandPool = radv_CreateCommandPool,
    .vkDestroyCommandPool = radv_DestroyCommandPool,
    .vkResetCommandPool = radv_ResetCommandPool,
    .vkAllocateCommandBuffers = radv_AllocateCommandBuffers,
    .vkFreeCommandBuffers = radv_FreeCommandBuffers,
    .vkBeginCommandBuffer = radv_BeginCommandBuffer,
    .vkEndCommandBuffer = radv_EndCommandBuffer,
    .vkResetCommandBuffer = radv_ResetCommandBuffer,
    .vkCmdBindPipeline = radv_CmdBindPipeline,
    .vkCmdSetViewport = radv_CmdSetViewport,
    .vkCmdSetScissor = radv_CmdSetScissor,
    .vkCmdSetLineWidth = radv_CmdSetLineWidth,
    .vkCmdSetDepthBias = radv_CmdSetDepthBias,
    .vkCmdSetBlendConstants = radv_CmdSetBlendConstants,
    .vkCmdSetDepthBounds = radv_CmdSetDepthBounds,
    .vkCmdSetStencilCompareMask = radv_CmdSetStencilCompareMask,
    .vkCmdSetStencilWriteMask = radv_CmdSetStencilWriteMask,
    .vkCmdSetStencilReference = radv_CmdSetStencilReference,
    .vkCmdBindDescriptorSets = radv_CmdBindDescriptorSets,
    .vkCmdBindIndexBuffer = radv_CmdBindIndexBuffer,
    .vkCmdBindVertexBuffers = radv_CmdBindVertexBuffers,
    .vkCmdDraw = radv_CmdDraw,
    .vkCmdDrawIndexed = radv_CmdDrawIndexed,
    .vkCmdDrawIndirect = radv_CmdDrawIndirect,
    .vkCmdDrawIndexedIndirect = radv_CmdDrawIndexedIndirect,
    .vkCmdDispatch = radv_CmdDispatch,
    .vkCmdDispatchIndirect = radv_CmdDispatchIndirect,
    .vkCmdCopyBuffer = radv_CmdCopyBuffer,
    .vkCmdCopyImage = radv_CmdCopyImage,
    .vkCmdBlitImage = radv_CmdBlitImage,
    .vkCmdCopyBufferToImage = radv_CmdCopyBufferToImage,
    .vkCmdCopyImageToBuffer = radv_CmdCopyImageToBuffer,
    .vkCmdUpdateBuffer = radv_CmdUpdateBuffer,
    .vkCmdFillBuffer = radv_CmdFillBuffer,
    .vkCmdClearColorImage = radv_CmdClearColorImage,
    .vkCmdClearDepthStencilImage = radv_CmdClearDepthStencilImage,
    .vkCmdClearAttachments = radv_CmdClearAttachments,
    .vkCmdResolveImage = radv_CmdResolveImage,
    .vkCmdSetEvent = radv_CmdSetEvent,
    .vkCmdResetEvent = radv_CmdResetEvent,
    .vkCmdWaitEvents = radv_CmdWaitEvents,
    .vkCmdPipelineBarrier = radv_CmdPipelineBarrier,
    .vkCmdBeginQuery = radv_CmdBeginQuery,
    .vkCmdEndQuery = radv_CmdEndQuery,
    .vkCmdBeginConditionalRenderingEXT = radv_CmdBeginConditionalRenderingEXT,
    .vkCmdEndConditionalRenderingEXT = radv_CmdEndConditionalRenderingEXT,
    .vkCmdResetQueryPool = radv_CmdResetQueryPool,
    .vkCmdWriteTimestamp = radv_CmdWriteTimestamp,
    .vkCmdCopyQueryPoolResults = radv_CmdCopyQueryPoolResults,
    .vkCmdPushConstants = radv_CmdPushConstants,
    .vkCmdBeginRenderPass = radv_CmdBeginRenderPass,
    .vkCmdNextSubpass = radv_CmdNextSubpass,
    .vkCmdEndRenderPass = radv_CmdEndRenderPass,
    .vkCmdExecuteCommands = radv_CmdExecuteCommands,
    .vkGetPhysicalDeviceDisplayPropertiesKHR = radv_GetPhysicalDeviceDisplayPropertiesKHR,
    .vkGetPhysicalDeviceDisplayPlanePropertiesKHR = radv_GetPhysicalDeviceDisplayPlanePropertiesKHR,
    .vkGetDisplayPlaneSupportedDisplaysKHR = radv_GetDisplayPlaneSupportedDisplaysKHR,
    .vkGetDisplayModePropertiesKHR = radv_GetDisplayModePropertiesKHR,
    .vkCreateDisplayModeKHR = radv_CreateDisplayModeKHR,
    .vkGetDisplayPlaneCapabilitiesKHR = radv_GetDisplayPlaneCapabilitiesKHR,
    .vkCreateDisplayPlaneSurfaceKHR = radv_CreateDisplayPlaneSurfaceKHR,
    .vkDestroySurfaceKHR = radv_DestroySurfaceKHR,
    .vkGetPhysicalDeviceSurfaceSupportKHR = radv_GetPhysicalDeviceSurfaceSupportKHR,
    .vkGetPhysicalDeviceSurfaceCapabilitiesKHR = radv_GetPhysicalDeviceSurfaceCapabilitiesKHR,
    .vkGetPhysicalDeviceSurfaceFormatsKHR = radv_GetPhysicalDeviceSurfaceFormatsKHR,
    .vkGetPhysicalDeviceSurfacePresentModesKHR = radv_GetPhysicalDeviceSurfacePresentModesKHR,
    .vkCreateSwapchainKHR = radv_CreateSwapchainKHR,
    .vkDestroySwapchainKHR = radv_DestroySwapchainKHR,
    .vkGetSwapchainImagesKHR = radv_GetSwapchainImagesKHR,
    .vkAcquireNextImageKHR = radv_AcquireNextImageKHR,
    .vkQueuePresentKHR = radv_QueuePresentKHR,
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .vkCreateWaylandSurfaceKHR = radv_CreateWaylandSurfaceKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .vkGetPhysicalDeviceWaylandPresentationSupportKHR = radv_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .vkCreateXlibSurfaceKHR = radv_CreateXlibSurfaceKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .vkGetPhysicalDeviceXlibPresentationSupportKHR = radv_GetPhysicalDeviceXlibPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .vkCreateXcbSurfaceKHR = radv_CreateXcbSurfaceKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .vkGetPhysicalDeviceXcbPresentationSupportKHR = radv_GetPhysicalDeviceXcbPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
    .vkCreateDebugReportCallbackEXT = radv_CreateDebugReportCallbackEXT,
    .vkDestroyDebugReportCallbackEXT = radv_DestroyDebugReportCallbackEXT,
    .vkDebugReportMessageEXT = radv_DebugReportMessageEXT,
    .vkCmdDrawIndirectCountAMD = radv_CmdDrawIndirectCountAMD,
    .vkCmdDrawIndexedIndirectCountAMD = radv_CmdDrawIndexedIndirectCountAMD,
    .vkGetPhysicalDeviceFeatures2 = radv_GetPhysicalDeviceFeatures2,
    .vkGetPhysicalDeviceFeatures2KHR = radv_GetPhysicalDeviceFeatures2,
    .vkGetPhysicalDeviceProperties2 = radv_GetPhysicalDeviceProperties2,
    .vkGetPhysicalDeviceProperties2KHR = radv_GetPhysicalDeviceProperties2,
    .vkGetPhysicalDeviceFormatProperties2 = radv_GetPhysicalDeviceFormatProperties2,
    .vkGetPhysicalDeviceFormatProperties2KHR = radv_GetPhysicalDeviceFormatProperties2,
    .vkGetPhysicalDeviceImageFormatProperties2 = radv_GetPhysicalDeviceImageFormatProperties2,
    .vkGetPhysicalDeviceImageFormatProperties2KHR = radv_GetPhysicalDeviceImageFormatProperties2,
    .vkGetPhysicalDeviceQueueFamilyProperties2 = radv_GetPhysicalDeviceQueueFamilyProperties2,
    .vkGetPhysicalDeviceQueueFamilyProperties2KHR = radv_GetPhysicalDeviceQueueFamilyProperties2,
    .vkGetPhysicalDeviceMemoryProperties2 = radv_GetPhysicalDeviceMemoryProperties2,
    .vkGetPhysicalDeviceMemoryProperties2KHR = radv_GetPhysicalDeviceMemoryProperties2,
    .vkGetPhysicalDeviceSparseImageFormatProperties2 = radv_GetPhysicalDeviceSparseImageFormatProperties2,
    .vkGetPhysicalDeviceSparseImageFormatProperties2KHR = radv_GetPhysicalDeviceSparseImageFormatProperties2,
    .vkCmdPushDescriptorSetKHR = radv_CmdPushDescriptorSetKHR,
    .vkTrimCommandPool = radv_TrimCommandPool,
    .vkTrimCommandPoolKHR = radv_TrimCommandPool,
    .vkGetPhysicalDeviceExternalBufferProperties = radv_GetPhysicalDeviceExternalBufferProperties,
    .vkGetPhysicalDeviceExternalBufferPropertiesKHR = radv_GetPhysicalDeviceExternalBufferProperties,
    .vkGetMemoryFdKHR = radv_GetMemoryFdKHR,
    .vkGetMemoryFdPropertiesKHR = radv_GetMemoryFdPropertiesKHR,
    .vkGetPhysicalDeviceExternalSemaphoreProperties = radv_GetPhysicalDeviceExternalSemaphoreProperties,
    .vkGetPhysicalDeviceExternalSemaphorePropertiesKHR = radv_GetPhysicalDeviceExternalSemaphoreProperties,
    .vkGetSemaphoreFdKHR = radv_GetSemaphoreFdKHR,
    .vkImportSemaphoreFdKHR = radv_ImportSemaphoreFdKHR,
    .vkGetPhysicalDeviceExternalFenceProperties = radv_GetPhysicalDeviceExternalFenceProperties,
    .vkGetPhysicalDeviceExternalFencePropertiesKHR = radv_GetPhysicalDeviceExternalFenceProperties,
    .vkGetFenceFdKHR = radv_GetFenceFdKHR,
    .vkImportFenceFdKHR = radv_ImportFenceFdKHR,
    .vkReleaseDisplayEXT = radv_ReleaseDisplayEXT,
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
    .vkAcquireXlibDisplayEXT = radv_AcquireXlibDisplayEXT,
#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
    .vkGetRandROutputDisplayEXT = radv_GetRandROutputDisplayEXT,
#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT
    .vkDisplayPowerControlEXT = radv_DisplayPowerControlEXT,
    .vkRegisterDeviceEventEXT = radv_RegisterDeviceEventEXT,
    .vkRegisterDisplayEventEXT = radv_RegisterDisplayEventEXT,
    .vkGetSwapchainCounterEXT = radv_GetSwapchainCounterEXT,
    .vkGetPhysicalDeviceSurfaceCapabilities2EXT = radv_GetPhysicalDeviceSurfaceCapabilities2EXT,
    .vkEnumeratePhysicalDeviceGroups = radv_EnumeratePhysicalDeviceGroups,
    .vkEnumeratePhysicalDeviceGroupsKHR = radv_EnumeratePhysicalDeviceGroups,
    .vkGetDeviceGroupPeerMemoryFeatures = radv_GetDeviceGroupPeerMemoryFeatures,
    .vkGetDeviceGroupPeerMemoryFeaturesKHR = radv_GetDeviceGroupPeerMemoryFeatures,
    .vkBindBufferMemory2 = radv_BindBufferMemory2,
    .vkBindBufferMemory2KHR = radv_BindBufferMemory2,
    .vkBindImageMemory2 = radv_BindImageMemory2,
    .vkBindImageMemory2KHR = radv_BindImageMemory2,
    .vkCmdSetDeviceMask = radv_CmdSetDeviceMask,
    .vkCmdSetDeviceMaskKHR = radv_CmdSetDeviceMask,
    .vkGetDeviceGroupPresentCapabilitiesKHR = radv_GetDeviceGroupPresentCapabilitiesKHR,
    .vkGetDeviceGroupSurfacePresentModesKHR = radv_GetDeviceGroupSurfacePresentModesKHR,
    .vkAcquireNextImage2KHR = radv_AcquireNextImage2KHR,
    .vkCmdDispatchBase = radv_CmdDispatchBase,
    .vkCmdDispatchBaseKHR = radv_CmdDispatchBase,
    .vkGetPhysicalDevicePresentRectanglesKHR = radv_GetPhysicalDevicePresentRectanglesKHR,
    .vkCreateDescriptorUpdateTemplate = radv_CreateDescriptorUpdateTemplate,
    .vkCreateDescriptorUpdateTemplateKHR = radv_CreateDescriptorUpdateTemplate,
    .vkDestroyDescriptorUpdateTemplate = radv_DestroyDescriptorUpdateTemplate,
    .vkDestroyDescriptorUpdateTemplateKHR = radv_DestroyDescriptorUpdateTemplate,
    .vkUpdateDescriptorSetWithTemplate = radv_UpdateDescriptorSetWithTemplate,
    .vkUpdateDescriptorSetWithTemplateKHR = radv_UpdateDescriptorSetWithTemplate,
    .vkCmdPushDescriptorSetWithTemplateKHR = radv_CmdPushDescriptorSetWithTemplateKHR,
    .vkCmdSetDiscardRectangleEXT = radv_CmdSetDiscardRectangleEXT,
    .vkGetPhysicalDeviceSurfaceCapabilities2KHR = radv_GetPhysicalDeviceSurfaceCapabilities2KHR,
    .vkGetPhysicalDeviceSurfaceFormats2KHR = radv_GetPhysicalDeviceSurfaceFormats2KHR,
    .vkGetPhysicalDeviceDisplayProperties2KHR = radv_GetPhysicalDeviceDisplayProperties2KHR,
    .vkGetPhysicalDeviceDisplayPlaneProperties2KHR = radv_GetPhysicalDeviceDisplayPlaneProperties2KHR,
    .vkGetDisplayModeProperties2KHR = radv_GetDisplayModeProperties2KHR,
    .vkGetDisplayPlaneCapabilities2KHR = radv_GetDisplayPlaneCapabilities2KHR,
    .vkGetBufferMemoryRequirements2 = radv_GetBufferMemoryRequirements2,
    .vkGetBufferMemoryRequirements2KHR = radv_GetBufferMemoryRequirements2,
    .vkGetImageMemoryRequirements2 = radv_GetImageMemoryRequirements2,
    .vkGetImageMemoryRequirements2KHR = radv_GetImageMemoryRequirements2,
    .vkGetImageSparseMemoryRequirements2 = radv_GetImageSparseMemoryRequirements2,
    .vkGetImageSparseMemoryRequirements2KHR = radv_GetImageSparseMemoryRequirements2,
    .vkCreateSamplerYcbcrConversion = radv_CreateSamplerYcbcrConversion,
    .vkDestroySamplerYcbcrConversion = radv_DestroySamplerYcbcrConversion,
    .vkGetDeviceQueue2 = radv_GetDeviceQueue2,
    .vkGetDescriptorSetLayoutSupport = radv_GetDescriptorSetLayoutSupport,
    .vkGetDescriptorSetLayoutSupportKHR = radv_GetDescriptorSetLayoutSupport,
#ifdef VK_USE_PLATFORM_ANDROID_KHR
    .vkGetSwapchainGrallocUsageANDROID = radv_GetSwapchainGrallocUsageANDROID,
#endif // VK_USE_PLATFORM_ANDROID_KHR
#ifdef VK_USE_PLATFORM_ANDROID_KHR
    .vkAcquireImageANDROID = radv_AcquireImageANDROID,
#endif // VK_USE_PLATFORM_ANDROID_KHR
#ifdef VK_USE_PLATFORM_ANDROID_KHR
    .vkQueueSignalReleaseImageANDROID = radv_QueueSignalReleaseImageANDROID,
#endif // VK_USE_PLATFORM_ANDROID_KHR
    .vkGetShaderInfoAMD = radv_GetShaderInfoAMD,
    .vkGetMemoryHostPointerPropertiesEXT = radv_GetMemoryHostPointerPropertiesEXT,
    .vkCreateRenderPass2KHR = radv_CreateRenderPass2KHR,
    .vkCmdBeginRenderPass2KHR = radv_CmdBeginRenderPass2KHR,
    .vkCmdNextSubpass2KHR = radv_CmdNextSubpass2KHR,
    .vkCmdEndRenderPass2KHR = radv_CmdEndRenderPass2KHR,
    .vkCmdDrawIndirectCountKHR = radv_CmdDrawIndirectCountKHR,
    .vkCmdDrawIndexedIndirectCountKHR = radv_CmdDrawIndexedIndirectCountKHR,
  };

static void * __attribute__ ((noinline))
radv_resolve_entrypoint(uint32_t index)
{
   return radv_layer.entrypoints[index];
}

/** Return true if the core version or extension in which the given entrypoint
 * is defined is enabled.
 *
 * If instance is NULL, we only allow the 3 commands explicitly allowed by the vk
 * spec.
 *
 * If device is NULL, all device extensions are considered enabled.
 */
static bool
radv_entrypoint_is_enabled(int index, uint32_t core_version,
                          const struct radv_instance_extension_table *instance,
                          const struct radv_device_extension_table *device)
{
   switch (index) {
   case 0:
      if (device) return false;
      return !device;
   case 1:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 2:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 3:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 4:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 5:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 6:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 7:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 8:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 9:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 10:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 11:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 12:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 13:
      if (device) return false;
      return !device;
   case 14:
      if (device) return false;
      return !device;
   case 15:
      if (device) return false;
      return !device;
   case 16:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 17:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 18:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 19:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 20:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 21:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 22:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 23:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 24:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 25:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 26:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 27:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 28:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 29:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 30:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 31:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 32:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 33:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 34:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 35:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 36:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 37:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 38:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 39:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 40:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 41:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 42:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 43:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 44:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 45:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 46:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 47:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 48:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 49:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 50:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 51:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 52:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 53:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 54:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 55:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 56:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 57:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 58:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 59:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 60:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 61:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 62:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 63:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 64:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 65:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 66:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 67:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 68:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 69:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 70:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 71:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 72:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 73:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 74:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 75:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 76:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 77:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 78:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 79:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 80:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 81:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 82:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 83:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 84:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 85:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 86:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 87:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 88:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 89:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 90:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 91:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 92:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 93:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 94:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 95:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 96:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 97:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 98:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 99:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 100:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 101:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 102:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 103:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 104:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 105:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 106:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 107:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 108:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 109:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 110:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 111:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 112:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 113:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 114:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 115:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 116:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 117:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 118:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 119:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 120:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 121:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 122:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 123:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 124:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 125:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 126:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 127:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 128:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 129:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 130:
      if (instance && (!device || device->EXT_conditional_rendering)) return true;
      return false;
   case 131:
      if (instance && (!device || device->EXT_conditional_rendering)) return true;
      return false;
   case 132:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 133:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 134:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 135:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 136:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 137:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 138:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 139:
      return instance && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 140:
      if (device) return false;
      if (instance && instance->KHR_display) return true;
      return false;
   case 141:
      if (device) return false;
      if (instance && instance->KHR_display) return true;
      return false;
   case 142:
      if (device) return false;
      if (instance && instance->KHR_display) return true;
      return false;
   case 143:
      if (device) return false;
      if (instance && instance->KHR_display) return true;
      return false;
   case 144:
      if (device) return false;
      if (instance && instance->KHR_display) return true;
      return false;
   case 145:
      if (device) return false;
      if (instance && instance->KHR_display) return true;
      return false;
   case 146:
      if (device) return false;
      if (instance && instance->KHR_display) return true;
      return false;
   case 147:
      if (device) return false;
      if (instance && instance->KHR_surface) return true;
      return false;
   case 148:
      if (device) return false;
      if (instance && instance->KHR_surface) return true;
      return false;
   case 149:
      if (device) return false;
      if (instance && instance->KHR_surface) return true;
      return false;
   case 150:
      if (device) return false;
      if (instance && instance->KHR_surface) return true;
      return false;
   case 151:
      if (device) return false;
      if (instance && instance->KHR_surface) return true;
      return false;
   case 152:
      if (instance && (!device || device->KHR_swapchain)) return true;
      return false;
   case 153:
      if (instance && (!device || device->KHR_swapchain)) return true;
      return false;
   case 154:
      if (instance && (!device || device->KHR_swapchain)) return true;
      return false;
   case 155:
      if (instance && (!device || device->KHR_swapchain)) return true;
      return false;
   case 156:
      if (instance && (!device || device->KHR_swapchain)) return true;
      return false;
   case 157:
      if (device) return false;
      if (instance && instance->KHR_wayland_surface) return true;
      return false;
   case 158:
      if (device) return false;
      if (instance && instance->KHR_wayland_surface) return true;
      return false;
   case 159:
      if (device) return false;
      if (instance && instance->KHR_xlib_surface) return true;
      return false;
   case 160:
      if (device) return false;
      if (instance && instance->KHR_xlib_surface) return true;
      return false;
   case 161:
      if (device) return false;
      if (instance && instance->KHR_xcb_surface) return true;
      return false;
   case 162:
      if (device) return false;
      if (instance && instance->KHR_xcb_surface) return true;
      return false;
   case 163:
      if (device) return false;
      if (instance && instance->EXT_debug_report) return true;
      return false;
   case 164:
      if (device) return false;
      if (instance && instance->EXT_debug_report) return true;
      return false;
   case 165:
      if (device) return false;
      if (instance && instance->EXT_debug_report) return true;
      return false;
   case 166:
      if (instance && (!device || device->AMD_draw_indirect_count)) return true;
      return false;
   case 167:
      if (instance && (!device || device->AMD_draw_indirect_count)) return true;
      return false;
   case 168:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 169:
      if (device) return false;
      if (instance && instance->KHR_get_physical_device_properties2) return true;
      return false;
   case 170:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 171:
      if (device) return false;
      if (instance && instance->KHR_get_physical_device_properties2) return true;
      return false;
   case 172:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 173:
      if (device) return false;
      if (instance && instance->KHR_get_physical_device_properties2) return true;
      return false;
   case 174:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 175:
      if (device) return false;
      if (instance && instance->KHR_get_physical_device_properties2) return true;
      return false;
   case 176:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 177:
      if (device) return false;
      if (instance && instance->KHR_get_physical_device_properties2) return true;
      return false;
   case 178:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 179:
      if (device) return false;
      if (instance && instance->KHR_get_physical_device_properties2) return true;
      return false;
   case 180:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 181:
      if (device) return false;
      if (instance && instance->KHR_get_physical_device_properties2) return true;
      return false;
   case 182:
      if (instance && (!device || device->KHR_push_descriptor)) return true;
      return false;
   case 183:
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 184:
      if (instance && (!device || device->KHR_maintenance1)) return true;
      return false;
   case 185:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 186:
      if (device) return false;
      if (instance && instance->KHR_external_memory_capabilities) return true;
      return false;
   case 187:
      if (instance && (!device || device->KHR_external_memory_fd)) return true;
      return false;
   case 188:
      if (instance && (!device || device->KHR_external_memory_fd)) return true;
      return false;
   case 189:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 190:
      if (device) return false;
      if (instance && instance->KHR_external_semaphore_capabilities) return true;
      return false;
   case 191:
      if (instance && (!device || device->KHR_external_semaphore_fd)) return true;
      return false;
   case 192:
      if (instance && (!device || device->KHR_external_semaphore_fd)) return true;
      return false;
   case 193:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 194:
      if (device) return false;
      if (instance && instance->KHR_external_fence_capabilities) return true;
      return false;
   case 195:
      if (instance && (!device || device->KHR_external_fence_fd)) return true;
      return false;
   case 196:
      if (instance && (!device || device->KHR_external_fence_fd)) return true;
      return false;
   case 197:
      if (device) return false;
      if (instance && instance->EXT_direct_mode_display) return true;
      return false;
   case 198:
      if (device) return false;
      if (instance && instance->EXT_acquire_xlib_display) return true;
      return false;
   case 199:
      if (device) return false;
      if (instance && instance->EXT_acquire_xlib_display) return true;
      return false;
   case 200:
      if (instance && (!device || device->EXT_display_control)) return true;
      return false;
   case 201:
      if (instance && (!device || device->EXT_display_control)) return true;
      return false;
   case 202:
      if (instance && (!device || device->EXT_display_control)) return true;
      return false;
   case 203:
      if (instance && (!device || device->EXT_display_control)) return true;
      return false;
   case 204:
      if (device) return false;
      if (instance && instance->EXT_display_surface_counter) return true;
      return false;
   case 205:
      if (device) return false;
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 206:
      if (device) return false;
      if (instance && instance->KHR_device_group_creation) return true;
      return false;
   case 207:
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 208:
      if (instance && (!device || device->KHR_device_group)) return true;
      return false;
   case 209:
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 210:
      if (instance && (!device || device->KHR_bind_memory2)) return true;
      return false;
   case 211:
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 212:
      if (instance && (!device || device->KHR_bind_memory2)) return true;
      return false;
   case 213:
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 214:
      if (instance && (!device || device->KHR_device_group)) return true;
      return false;
   case 215:
      if (instance && (!device || device->KHR_swapchain)) return true;
      if (instance && (!device || device->KHR_device_group)) return true;
      return false;
   case 216:
      if (instance && (!device || device->KHR_swapchain)) return true;
      if (instance && (!device || device->KHR_device_group)) return true;
      return false;
   case 217:
      if (instance && (!device || device->KHR_swapchain)) return true;
      if (instance && (!device || device->KHR_device_group)) return true;
      return false;
   case 218:
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 219:
      if (instance && (!device || device->KHR_device_group)) return true;
      return false;
   case 220:
      if (device) return false;
      if (instance && (!device || device->KHR_swapchain)) return true;
      if (instance && (!device || device->KHR_device_group)) return true;
      return false;
   case 221:
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 222:
      if (instance && (!device || device->KHR_descriptor_update_template)) return true;
      return false;
   case 223:
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 224:
      if (instance && (!device || device->KHR_descriptor_update_template)) return true;
      return false;
   case 225:
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 226:
      if (instance && (!device || device->KHR_descriptor_update_template)) return true;
      return false;
   case 227:
      if (instance && (!device || device->KHR_push_descriptor)) return true;
      if (instance && (!device || device->KHR_descriptor_update_template)) return true;
      return false;
   case 228:
      if (instance && (!device || device->EXT_discard_rectangles)) return true;
      return false;
   case 229:
      if (device) return false;
      if (instance && instance->KHR_get_surface_capabilities2) return true;
      return false;
   case 230:
      if (device) return false;
      if (instance && instance->KHR_get_surface_capabilities2) return true;
      return false;
   case 231:
      if (device) return false;
      if (instance && instance->KHR_get_display_properties2) return true;
      return false;
   case 232:
      if (device) return false;
      if (instance && instance->KHR_get_display_properties2) return true;
      return false;
   case 233:
      if (device) return false;
      if (instance && instance->KHR_get_display_properties2) return true;
      return false;
   case 234:
      if (device) return false;
      if (instance && instance->KHR_get_display_properties2) return true;
      return false;
   case 235:
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 236:
      if (instance && (!device || device->KHR_get_memory_requirements2)) return true;
      return false;
   case 237:
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 238:
      if (instance && (!device || device->KHR_get_memory_requirements2)) return true;
      return false;
   case 239:
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 240:
      if (instance && (!device || device->KHR_get_memory_requirements2)) return true;
      return false;
   case 241:
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 242:
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 243:
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 244:
      return instance && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 245:
      if (instance && (!device || device->KHR_maintenance3)) return true;
      return false;
   case 246:
      if (instance && (!device || device->ANDROID_native_buffer)) return true;
      return false;
   case 247:
      if (instance && (!device || device->ANDROID_native_buffer)) return true;
      return false;
   case 248:
      if (instance && (!device || device->ANDROID_native_buffer)) return true;
      return false;
   case 249:
      if (instance && (!device || device->AMD_shader_info)) return true;
      return false;
   case 250:
      if (instance && (!device || device->EXT_external_memory_host)) return true;
      return false;
   case 251:
      if (instance && (!device || device->KHR_create_renderpass2)) return true;
      return false;
   case 252:
      if (instance && (!device || device->KHR_create_renderpass2)) return true;
      return false;
   case 253:
      if (instance && (!device || device->KHR_create_renderpass2)) return true;
      return false;
   case 254:
      if (instance && (!device || device->KHR_create_renderpass2)) return true;
      return false;
   case 255:
      if (instance && (!device || device->KHR_draw_indirect_count)) return true;
      return false;
   case 256:
      if (instance && (!device || device->KHR_draw_indirect_count)) return true;
      return false;
   default:
      return false;
   }
}

static int
radv_lookup_entrypoint(const char *name)
{
   static const uint32_t prime_factor = 5024183;
   static const uint32_t prime_step = 19;
   const struct string_map_entry *e;
   uint32_t hash, h;
   uint16_t i;
   const char *p;

   hash = 0;
   for (p = name; *p; p++)
       hash = hash * prime_factor + *p;

   h = hash;
   while (1) {
       i = string_map[h & 511];
       if (i == none)
          return -1;
       e = &string_map_entries[i];
       if (e->hash == hash && strcmp(name, strings + e->name) == 0)
           return e->num;
       h += prime_step;
   }

   return -1;
}

void *
radv_lookup_entrypoint_unchecked(const char *name)
{
   int index = radv_lookup_entrypoint(name);
   if (index < 0)
      return NULL;
   return radv_resolve_entrypoint(index);
}

void *
radv_lookup_entrypoint_checked(const char *name,
                               uint32_t core_version,
                               const struct radv_instance_extension_table *instance,
                               const struct radv_device_extension_table *device)
{
   int index = radv_lookup_entrypoint(name);
   if (index < 0 || !radv_entrypoint_is_enabled(index, core_version, instance, device))
      return NULL;
   return radv_resolve_entrypoint(index);
}