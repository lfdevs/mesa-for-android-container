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

struct string_map_entry {
   uint32_t name;
   uint32_t hash;
   uint32_t num;
};

/* We use a big string constant to avoid lots of reloctions from the entry
 * point table to lots of little strings. The entries in the entry point table
 * store the index into this big string.
 */

static const char strings[] =
    "vkAcquireNextImage2KHR\0"
    "vkAcquireNextImageKHR\0"
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
    "vkCmdBeginQuery\0"
    "vkCmdBeginRenderPass\0"
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
    "vkCmdDrawIndirect\0"
    "vkCmdEndQuery\0"
    "vkCmdEndRenderPass\0"
    "vkCmdExecuteCommands\0"
    "vkCmdFillBuffer\0"
    "vkCmdNextSubpass\0"
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
    "vkCreateDmaBufImageINTEL\0"
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
    "vkCreateSampler\0"
    "vkCreateSamplerYcbcrConversion\0"
    "vkCreateSamplerYcbcrConversionKHR\0"
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
    "vkDestroySamplerYcbcrConversionKHR\0"
    "vkDestroySemaphore\0"
    "vkDestroyShaderModule\0"
    "vkDestroySurfaceKHR\0"
    "vkDestroySwapchainKHR\0"
    "vkDeviceWaitIdle\0"
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
    "vkGetRenderAreaGranularity\0"
    "vkGetSemaphoreFdKHR\0"
    "vkGetSwapchainImagesKHR\0"
    "vkImportFenceFdKHR\0"
    "vkImportSemaphoreFdKHR\0"
    "vkInvalidateMappedMemoryRanges\0"
    "vkMapMemory\0"
    "vkMergePipelineCaches\0"
    "vkQueueBindSparse\0"
    "vkQueuePresentKHR\0"
    "vkQueueSubmit\0"
    "vkQueueWaitIdle\0"
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
    { 0, 0x82860572, 198 }, /* vkAcquireNextImage2KHR */
    { 23, 0xc3fedb2e, 146 }, /* vkAcquireNextImageKHR */
    { 45, 0x8c0c811a, 89 }, /* vkAllocateCommandBuffers */
    { 70, 0x4c449d3a, 78 }, /* vkAllocateDescriptorSets */
    { 95, 0x522b85d3, 22 }, /* vkAllocateMemory */
    { 112, 0xc54f7327, 91 }, /* vkBeginCommandBuffer */
    { 133, 0x6bcbdcb, 30 }, /* vkBindBufferMemory */
    { 152, 0xc27aaf4f, 190 }, /* vkBindBufferMemory2 */
    { 172, 0x6878d3ce, 191 }, /* vkBindBufferMemory2KHR */
    { 195, 0x5caaae4a, 32 }, /* vkBindImageMemory */
    { 213, 0xa9097118, 192 }, /* vkBindImageMemory2 */
    { 232, 0xf18729ad, 193 }, /* vkBindImageMemory2KHR */
    { 254, 0xf5064ea4, 128 }, /* vkCmdBeginQuery */
    { 270, 0xcb7a58e3, 134 }, /* vkCmdBeginRenderPass */
    { 291, 0x28c7a5da, 104 }, /* vkCmdBindDescriptorSets */
    { 315, 0x4c22d870, 105 }, /* vkCmdBindIndexBuffer */
    { 336, 0x3af9fd84, 94 }, /* vkCmdBindPipeline */
    { 354, 0xa9c83f1d, 106 }, /* vkCmdBindVertexBuffers */
    { 377, 0x331ebf89, 115 }, /* vkCmdBlitImage */
    { 392, 0x93cb5cb8, 122 }, /* vkCmdClearAttachments */
    { 414, 0xb4bc8d08, 120 }, /* vkCmdClearColorImage */
    { 435, 0x4f88e4ba, 121 }, /* vkCmdClearDepthStencilImage */
    { 463, 0xc939a0da, 113 }, /* vkCmdCopyBuffer */
    { 479, 0x929847e, 116 }, /* vkCmdCopyBufferToImage */
    { 502, 0x278effa9, 114 }, /* vkCmdCopyImage */
    { 517, 0x68cddbac, 117 }, /* vkCmdCopyImageToBuffer */
    { 540, 0xdee8c6d4, 132 }, /* vkCmdCopyQueryPoolResults */
    { 566, 0xbd58e867, 111 }, /* vkCmdDispatch */
    { 580, 0xfb767220, 199 }, /* vkCmdDispatchBase */
    { 598, 0x402403e5, 200 }, /* vkCmdDispatchBaseKHR */
    { 619, 0xd6353005, 112 }, /* vkCmdDispatchIndirect */
    { 641, 0x9912c1a1, 107 }, /* vkCmdDraw */
    { 651, 0xbe5a8058, 108 }, /* vkCmdDrawIndexed */
    { 668, 0x94e7ed36, 110 }, /* vkCmdDrawIndexedIndirect */
    { 693, 0xe9ac41bf, 109 }, /* vkCmdDrawIndirect */
    { 711, 0xd556fd22, 129 }, /* vkCmdEndQuery */
    { 725, 0xdcdb0235, 136 }, /* vkCmdEndRenderPass */
    { 744, 0x9eaabe40, 137 }, /* vkCmdExecuteCommands */
    { 765, 0x5bdd2ae0, 119 }, /* vkCmdFillBuffer */
    { 781, 0x2eeec2f9, 135 }, /* vkCmdNextSubpass */
    { 798, 0x97fccfe8, 127 }, /* vkCmdPipelineBarrier */
    { 819, 0xb1c6b468, 133 }, /* vkCmdPushConstants */
    { 838, 0xf17232a1, 171 }, /* vkCmdPushDescriptorSetKHR */
    { 864, 0x3d528981, 208 }, /* vkCmdPushDescriptorSetWithTemplateKHR */
    { 902, 0x4fccce28, 125 }, /* vkCmdResetEvent */
    { 918, 0x2f614082, 130 }, /* vkCmdResetQueryPool */
    { 938, 0x671bb594, 123 }, /* vkCmdResolveImage */
    { 956, 0x1c989dfb, 99 }, /* vkCmdSetBlendConstants */
    { 979, 0x30f14d07, 98 }, /* vkCmdSetDepthBias */
    { 997, 0x7b3a8a63, 100 }, /* vkCmdSetDepthBounds */
    { 1017, 0xaecdae87, 194 }, /* vkCmdSetDeviceMask */
    { 1036, 0xfbb79356, 195 }, /* vkCmdSetDeviceMaskKHR */
    { 1058, 0xe257f075, 124 }, /* vkCmdSetEvent */
    { 1072, 0x32282165, 97 }, /* vkCmdSetLineWidth */
    { 1090, 0x48f28c7f, 96 }, /* vkCmdSetScissor */
    { 1106, 0xa8f534e2, 101 }, /* vkCmdSetStencilCompareMask */
    { 1133, 0x83e2b024, 103 }, /* vkCmdSetStencilReference */
    { 1158, 0xe7c4b134, 102 }, /* vkCmdSetStencilWriteMask */
    { 1183, 0x53d6c2b, 95 }, /* vkCmdSetViewport */
    { 1200, 0xd2986b5e, 118 }, /* vkCmdUpdateBuffer */
    { 1218, 0x3b9346b3, 126 }, /* vkCmdWaitEvents */
    { 1234, 0xec4d324c, 131 }, /* vkCmdWriteTimestamp */
    { 1254, 0x7d4282b9, 51 }, /* vkCreateBuffer */
    { 1269, 0x925bd256, 53 }, /* vkCreateBufferView */
    { 1288, 0x820fe476, 86 }, /* vkCreateCommandPool */
    { 1308, 0xf70c85eb, 67 }, /* vkCreateComputePipelines */
    { 1333, 0x987ef56, 154 }, /* vkCreateDebugReportCallbackEXT */
    { 1364, 0xfb95a8a4, 75 }, /* vkCreateDescriptorPool */
    { 1387, 0x3c14cc74, 73 }, /* vkCreateDescriptorSetLayout */
    { 1415, 0xad3ce733, 202 }, /* vkCreateDescriptorUpdateTemplate */
    { 1448, 0x5189488a, 203 }, /* vkCreateDescriptorUpdateTemplateKHR */
    { 1484, 0x85ed23f, 11 }, /* vkCreateDevice */
    { 1499, 0x6392dfa7, 224 }, /* vkCreateDmaBufImageINTEL */
    { 1524, 0xe7188731, 43 }, /* vkCreateEvent */
    { 1538, 0x958af968, 36 }, /* vkCreateFence */
    { 1552, 0x887a38c4, 81 }, /* vkCreateFramebuffer */
    { 1572, 0x4b59f96d, 66 }, /* vkCreateGraphicsPipelines */
    { 1598, 0x652128c2, 55 }, /* vkCreateImage */
    { 1612, 0xdce077ff, 58 }, /* vkCreateImageView */
    { 1630, 0x38a581a6, 0 }, /* vkCreateInstance */
    { 1647, 0xcbf6489f, 62 }, /* vkCreatePipelineCache */
    { 1669, 0x451ef1ed, 69 }, /* vkCreatePipelineLayout */
    { 1692, 0x5edcd92b, 48 }, /* vkCreateQueryPool */
    { 1710, 0x109a9c18, 83 }, /* vkCreateRenderPass */
    { 1729, 0x13cf03f, 71 }, /* vkCreateSampler */
    { 1745, 0xe6a58c26, 217 }, /* vkCreateSamplerYcbcrConversion */
    { 1776, 0x7482104f, 218 }, /* vkCreateSamplerYcbcrConversionKHR */
    { 1810, 0xf2065e5b, 41 }, /* vkCreateSemaphore */
    { 1828, 0xa0d3cea2, 60 }, /* vkCreateShaderModule */
    { 1849, 0xcdefcaa8, 143 }, /* vkCreateSwapchainKHR */
    { 1870, 0x2b2a4b79, 148 }, /* vkCreateWaylandSurfaceKHR */
    { 1896, 0xc5e5b106, 152 }, /* vkCreateXcbSurfaceKHR */
    { 1918, 0xa693bc66, 150 }, /* vkCreateXlibSurfaceKHR */
    { 1941, 0xa4e75334, 156 }, /* vkDebugReportMessageEXT */
    { 1965, 0x94a07a45, 52 }, /* vkDestroyBuffer */
    { 1981, 0x98b27962, 54 }, /* vkDestroyBufferView */
    { 2001, 0xd5d83a0a, 87 }, /* vkDestroyCommandPool */
    { 2022, 0x43d4c4e2, 155 }, /* vkDestroyDebugReportCallbackEXT */
    { 2054, 0x47bdaf30, 76 }, /* vkDestroyDescriptorPool */
    { 2078, 0xa4227b08, 74 }, /* vkDestroyDescriptorSetLayout */
    { 2107, 0xbb2cbe7f, 204 }, /* vkDestroyDescriptorUpdateTemplate */
    { 2141, 0xaa83901e, 205 }, /* vkDestroyDescriptorUpdateTemplateKHR */
    { 2178, 0x1fbcc9cb, 12 }, /* vkDestroyDevice */
    { 2194, 0x4df27c05, 44 }, /* vkDestroyEvent */
    { 2209, 0xfc64ee3c, 37 }, /* vkDestroyFence */
    { 2224, 0xdc428e58, 82 }, /* vkDestroyFramebuffer */
    { 2245, 0xcbfb1d96, 56 }, /* vkDestroyImage */
    { 2260, 0xb5853953, 59 }, /* vkDestroyImageView */
    { 2279, 0x9bd21af2, 1 }, /* vkDestroyInstance */
    { 2297, 0x6aac68af, 68 }, /* vkDestroyPipeline */
    { 2315, 0x4112a673, 63 }, /* vkDestroyPipelineCache */
    { 2338, 0x9146f879, 70 }, /* vkDestroyPipelineLayout */
    { 2362, 0x37819a7f, 49 }, /* vkDestroyQueryPool */
    { 2381, 0x16f14324, 84 }, /* vkDestroyRenderPass */
    { 2401, 0x3b645153, 72 }, /* vkDestroySampler */
    { 2418, 0x20f261b2, 219 }, /* vkDestroySamplerYcbcrConversion */
    { 2450, 0xaaa623a3, 220 }, /* vkDestroySamplerYcbcrConversionKHR */
    { 2485, 0xcaab1faf, 42 }, /* vkDestroySemaphore */
    { 2504, 0x2d77af6e, 61 }, /* vkDestroyShaderModule */
    { 2526, 0xf204ce7d, 138 }, /* vkDestroySurfaceKHR */
    { 2546, 0x5a93ab74, 144 }, /* vkDestroySwapchainKHR */
    { 2568, 0xd46c5f24, 21 }, /* vkDeviceWaitIdle */
    { 2585, 0xaffb5725, 92 }, /* vkEndCommandBuffer */
    { 2604, 0x5fd13eed, 17 }, /* vkEnumerateDeviceExtensionProperties */
    { 2641, 0x2f8566e7, 16 }, /* vkEnumerateDeviceLayerProperties */
    { 2674, 0xeb27627e, 15 }, /* vkEnumerateInstanceExtensionProperties */
    { 2713, 0x81f69d8, 14 }, /* vkEnumerateInstanceLayerProperties */
    { 2748, 0xd0481e5c, 13 }, /* vkEnumerateInstanceVersion */
    { 2775, 0x270514f0, 186 }, /* vkEnumeratePhysicalDeviceGroups */
    { 2807, 0x549ce595, 187 }, /* vkEnumeratePhysicalDeviceGroupsKHR */
    { 2842, 0x5787c327, 2 }, /* vkEnumeratePhysicalDevices */
    { 2869, 0xff52f051, 26 }, /* vkFlushMappedMemoryRanges */
    { 2895, 0xb9db2b91, 90 }, /* vkFreeCommandBuffers */
    { 2916, 0x7a1347b1, 79 }, /* vkFreeDescriptorSets */
    { 2937, 0x8f6f838a, 23 }, /* vkFreeMemory */
    { 2950, 0xab98422a, 29 }, /* vkGetBufferMemoryRequirements */
    { 2980, 0xd1fd0638, 211 }, /* vkGetBufferMemoryRequirements2 */
    { 3011, 0x78dbe98d, 212 }, /* vkGetBufferMemoryRequirements2KHR */
    { 3045, 0xfeac9573, 222 }, /* vkGetDescriptorSetLayoutSupport */
    { 3077, 0xd7e44a, 223 }, /* vkGetDescriptorSetLayoutSupportKHR */
    { 3112, 0x2e218c10, 188 }, /* vkGetDeviceGroupPeerMemoryFeatures */
    { 3147, 0xa3809375, 189 }, /* vkGetDeviceGroupPeerMemoryFeaturesKHR */
    { 3185, 0xf72c87d4, 196 }, /* vkGetDeviceGroupPresentCapabilitiesKHR */
    { 3224, 0x6b9448c3, 197 }, /* vkGetDeviceGroupSurfacePresentModesKHR */
    { 3263, 0x46e38db5, 28 }, /* vkGetDeviceMemoryCommitment */
    { 3291, 0xba013486, 3 }, /* vkGetDeviceProcAddr */
    { 3311, 0xcc920d9a, 18 }, /* vkGetDeviceQueue */
    { 3328, 0xb11a6348, 221 }, /* vkGetDeviceQueue2 */
    { 3346, 0x96d834b, 45 }, /* vkGetEventStatus */
    { 3363, 0x69a5d6af, 184 }, /* vkGetFenceFdKHR */
    { 3379, 0x5f391892, 39 }, /* vkGetFenceStatus */
    { 3396, 0x916f1e63, 31 }, /* vkGetImageMemoryRequirements */
    { 3425, 0x56e213f7, 213 }, /* vkGetImageMemoryRequirements2 */
    { 3455, 0x8de28366, 214 }, /* vkGetImageMemoryRequirements2KHR */
    { 3488, 0x15855f5b, 33 }, /* vkGetImageSparseMemoryRequirements */
    { 3523, 0xbd4e3d3f, 215 }, /* vkGetImageSparseMemoryRequirements2 */
    { 3559, 0x3df40f5e, 216 }, /* vkGetImageSparseMemoryRequirements2KHR */
    { 3598, 0x9163b686, 57 }, /* vkGetImageSubresourceLayout */
    { 3626, 0x3d2ae9ad, 4 }, /* vkGetInstanceProcAddr */
    { 3648, 0x503c14c5, 176 }, /* vkGetMemoryFdKHR */
    { 3665, 0xb028a792, 177 }, /* vkGetMemoryFdPropertiesKHR */
    { 3692, 0x944476dc, 174 }, /* vkGetPhysicalDeviceExternalBufferProperties */
    { 3736, 0xee68b389, 175 }, /* vkGetPhysicalDeviceExternalBufferPropertiesKHR */
    { 3783, 0x3bc965eb, 182 }, /* vkGetPhysicalDeviceExternalFenceProperties */
    { 3826, 0x99b35492, 183 }, /* vkGetPhysicalDeviceExternalFencePropertiesKHR */
    { 3872, 0xcf251b0e, 178 }, /* vkGetPhysicalDeviceExternalSemaphoreProperties */
    { 3919, 0x984c3fa7, 179 }, /* vkGetPhysicalDeviceExternalSemaphorePropertiesKHR */
    { 3969, 0x113e2f33, 8 }, /* vkGetPhysicalDeviceFeatures */
    { 3997, 0x63c068a7, 157 }, /* vkGetPhysicalDeviceFeatures2 */
    { 4026, 0x6a9a3636, 158 }, /* vkGetPhysicalDeviceFeatures2KHR */
    { 4058, 0x3e54b398, 9 }, /* vkGetPhysicalDeviceFormatProperties */
    { 4094, 0xca3bb9da, 161 }, /* vkGetPhysicalDeviceFormatProperties2 */
    { 4131, 0x9099cbbb, 162 }, /* vkGetPhysicalDeviceFormatProperties2KHR */
    { 4171, 0xdd36a867, 10 }, /* vkGetPhysicalDeviceImageFormatProperties */
    { 4212, 0x35d260d3, 163 }, /* vkGetPhysicalDeviceImageFormatProperties2 */
    { 4254, 0x102ff7ea, 164 }, /* vkGetPhysicalDeviceImageFormatProperties2KHR */
    { 4299, 0xa90da4da, 7 }, /* vkGetPhysicalDeviceMemoryProperties */
    { 4335, 0xcb4cc208, 167 }, /* vkGetPhysicalDeviceMemoryProperties2 */
    { 4372, 0xc8c3da3d, 168 }, /* vkGetPhysicalDeviceMemoryProperties2KHR */
    { 4412, 0x100341b4, 201 }, /* vkGetPhysicalDevicePresentRectanglesKHR */
    { 4452, 0x52fe22c9, 5 }, /* vkGetPhysicalDeviceProperties */
    { 4482, 0x6c4d8ee1, 159 }, /* vkGetPhysicalDeviceProperties2 */
    { 4513, 0xcd15838c, 160 }, /* vkGetPhysicalDeviceProperties2KHR */
    { 4547, 0x4e5fc88a, 6 }, /* vkGetPhysicalDeviceQueueFamilyProperties */
    { 4588, 0xcad374d8, 165 }, /* vkGetPhysicalDeviceQueueFamilyProperties2 */
    { 4630, 0x5ceb2bed, 166 }, /* vkGetPhysicalDeviceQueueFamilyProperties2KHR */
    { 4675, 0x272ef8ef, 34 }, /* vkGetPhysicalDeviceSparseImageFormatProperties */
    { 4722, 0xebddba0b, 169 }, /* vkGetPhysicalDeviceSparseImageFormatProperties2 */
    { 4770, 0x8746ed72, 170 }, /* vkGetPhysicalDeviceSparseImageFormatProperties2KHR */
    { 4821, 0x9497e378, 209 }, /* vkGetPhysicalDeviceSurfaceCapabilities2KHR */
    { 4864, 0x77890558, 140 }, /* vkGetPhysicalDeviceSurfaceCapabilitiesKHR */
    { 4906, 0xd00b7188, 210 }, /* vkGetPhysicalDeviceSurfaceFormats2KHR */
    { 4944, 0xe32227c8, 141 }, /* vkGetPhysicalDeviceSurfaceFormatsKHR */
    { 4981, 0x31c3cbd1, 142 }, /* vkGetPhysicalDeviceSurfacePresentModesKHR */
    { 5023, 0x1a687885, 139 }, /* vkGetPhysicalDeviceSurfaceSupportKHR */
    { 5060, 0x84e085ac, 149 }, /* vkGetPhysicalDeviceWaylandPresentationSupportKHR */
    { 5109, 0x41782cb9, 153 }, /* vkGetPhysicalDeviceXcbPresentationSupportKHR */
    { 5154, 0x34a063ab, 151 }, /* vkGetPhysicalDeviceXlibPresentationSupportKHR */
    { 5200, 0x2092a349, 64 }, /* vkGetPipelineCacheData */
    { 5223, 0xbf3f2cb3, 50 }, /* vkGetQueryPoolResults */
    { 5245, 0xa9820d22, 85 }, /* vkGetRenderAreaGranularity */
    { 5272, 0x3e0e9884, 180 }, /* vkGetSemaphoreFdKHR */
    { 5292, 0x57695f28, 145 }, /* vkGetSwapchainImagesKHR */
    { 5316, 0x51df0390, 185 }, /* vkImportFenceFdKHR */
    { 5335, 0x36337c05, 181 }, /* vkImportSemaphoreFdKHR */
    { 5358, 0x1e115cca, 27 }, /* vkInvalidateMappedMemoryRanges */
    { 5389, 0xcb977bd8, 24 }, /* vkMapMemory */
    { 5401, 0xc3499606, 65 }, /* vkMergePipelineCaches */
    { 5423, 0xc3628a09, 35 }, /* vkQueueBindSparse */
    { 5441, 0xfc5fb6ce, 147 }, /* vkQueuePresentKHR */
    { 5459, 0xfa4713ec, 19 }, /* vkQueueSubmit */
    { 5473, 0x6f8fc2a5, 20 }, /* vkQueueWaitIdle */
    { 5489, 0x847dc731, 93 }, /* vkResetCommandBuffer */
    { 5510, 0x6da9f7fd, 88 }, /* vkResetCommandPool */
    { 5529, 0x9bd85f5, 77 }, /* vkResetDescriptorPool */
    { 5551, 0x6d373ba8, 47 }, /* vkResetEvent */
    { 5564, 0x684781dc, 38 }, /* vkResetFences */
    { 5578, 0x592ae5f5, 46 }, /* vkSetEvent */
    { 5589, 0xfef2fb38, 172 }, /* vkTrimCommandPool */
    { 5607, 0x51177c8d, 173 }, /* vkTrimCommandPoolKHR */
    { 5628, 0x1a1a0e2f, 25 }, /* vkUnmapMemory */
    { 5642, 0x5349c9d, 206 }, /* vkUpdateDescriptorSetWithTemplate */
    { 5676, 0x214ad230, 207 }, /* vkUpdateDescriptorSetWithTemplateKHR */
    { 5713, 0xbfd090ae, 80 }, /* vkUpdateDescriptorSets */
    { 5736, 0x19d64c81, 40 }, /* vkWaitForFences */
};

/* Hash table stats:
 * size 225 entries
 * collisions entries:
 *     0      180
 *     1      29
 *     2      10
 *     3      3
 *     4      2
 *     5      1
 *     6      0
 *     7      0
 *     8      0
 *     9+     0
 */

#define none 0xffff
static const uint16_t string_map[512] = {
    0x00ab,
    none,
    none,
    none,
    none,
    0x001e,
    0x00cf,
    none,
    0x00b1,
    0x00d0,
    0x0060,
    0x00bb,
    none,
    none,
    none,
    none,
    0x008c,
    0x00ce,
    none,
    0x00b9,
    none,
    none,
    none,
    none,
    0x0053,
    none,
    none,
    0x00d9,
    none,
    none,
    0x0065,
    none,
    0x001c,
    none,
    none,
    none,
    0x0038,
    none,
    0x0055,
    none,
    0x002c,
    none,
    0x0087,
    0x003a,
    none,
    none,
    none,
    0x00dc,
    0x00de,
    none,
    none,
    none,
    none,
    0x0024,
    0x00a9,
    none,
    0x0088,
    none,
    none,
    none,
    0x0068,
    0x00b2,
    0x0067,
    0x0047,
    0x0025,
    none,
    none,
    none,
    none,
    0x005e,
    none,
    none,
    none,
    none,
    0x0009,
    none,
    0x003d,
    none,
    none,
    0x0056,
    none,
    0x0083,
    0x0054,
    none,
    none,
    none,
    0x003f,
    none,
    0x0020,
    none,
    none,
    0x0057,
    0x007f,
    0x008b,
    none,
    none,
    none,
    none,
    none,
    0x0031,
    0x00cc,
    none,
    0x005c,
    0x001b,
    0x0029,
    none,
    none,
    0x0069,
    none,
    none,
    none,
    none,
    0x000f,
    none,
    none,
    0x006e,
    0x0044,
    0x0034,
    0x0040,
    none,
    none,
    0x006f,
    0x00ad,
    none,
    none,
    0x0077,
    0x0017,
    0x0036,
    none,
    0x00e0,
    0x002d,
    none,
    0x00c9,
    0x00c2,
    0x0091,
    0x0032,
    none,
    0x0097,
    0x0046,
    none,
    none,
    0x00db,
    none,
    none,
    none,
    0x007d,
    0x0064,
    none,
    none,
    none,
    none,
    none,
    none,
    0x009d,
    none,
    none,
    none,
    0x00b7,
    none,
    0x0050,
    none,
    0x002a,
    0x0058,
    none,
    0x000c,
    0x0070,
    none,
    0x00a8,
    0x0059,
    none,
    none,
    none,
    none,
    none,
    0x00df,
    0x006d,
    0x00dd,
    none,
    none,
    0x003c,
    none,
    none,
    none,
    0x0043,
    0x0013,
    0x003e,
    0x0015,
    none,
    none,
    none,
    none,
    none,
    none,
    none,
    0x004d,
    0x008f,
    0x004b,
    0x009f,
    0x00c7,
    none,
    none,
    0x00b4,
    0x00cd,
    0x0096,
    0x00c4,
    none,
    0x00d1,
    none,
    none,
    none,
    none,
    0x00ae,
    0x001a,
    0x0095,
    none,
    none,
    0x00b8,
    none,
    0x0016,
    none,
    0x00a1,
    none,
    0x00a4,
    none,
    0x0026,
    0x00b5,
    0x0037,
    0x000d,
    none,
    none,
    none,
    0x007c,
    none,
    none,
    none,
    none,
    none,
    0x007b,
    none,
    0x00ba,
    0x0080,
    0x00d3,
    0x006c,
    none,
    none,
    0x0061,
    none,
    none,
    none,
    0x0027,
    none,
    none,
    none,
    none,
    none,
    none,
    0x00b0,
    none,
    none,
    none,
    none,
    none,
    0x005b,
    0x0030,
    0x0014,
    none,
    none,
    none,
    none,
    none,
    0x00a5,
    none,
    none,
    none,
    none,
    none,
    none,
    none,
    none,
    none,
    0x000a,
    none,
    0x0002,
    0x0063,
    none,
    0x0011,
    none,
    none,
    none,
    none,
    0x0023,
    none,
    0x0071,
    0x007a,
    none,
    0x0005,
    0x00ca,
    none,
    none,
    0x0052,
    none,
    none,
    0x0001,
    none,
    0x0062,
    0x0049,
    none,
    0x0045,
    0x0039,
    0x00c8,
    0x0021,
    0x0079,
    0x00da,
    none,
    0x0003,
    none,
    none,
    none,
    none,
    0x009b,
    none,
    none,
    none,
    none,
    0x00d4,
    none,
    0x00a7,
    0x005d,
    0x0093,
    0x00c6,
    none,
    0x0094,
    none,
    0x0082,
    none,
    0x0007,
    none,
    none,
    none,
    0x006b,
    none,
    none,
    0x0033,
    none,
    0x00be,
    none,
    none,
    0x009a,
    none,
    none,
    0x003b,
    none,
    none,
    none,
    0x005f,
    none,
    none,
    0x0035,
    0x0072,
    none,
    0x004a,
    0x0042,
    none,
    none,
    none,
    0x004c,
    0x0076,
    none,
    none,
    0x009c,
    0x0000,
    0x008a,
    0x0078,
    0x008d,
    none,
    none,
    0x00bd,
    0x005a,
    none,
    none,
    none,
    none,
    none,
    none,
    none,
    0x002b,
    none,
    none,
    0x0010,
    0x00bc,
    none,
    none,
    0x00bf,
    0x0012,
    0x0086,
    none,
    0x0099,
    0x0089,
    none,
    none,
    0x00cb,
    0x0084,
    0x00a0,
    none,
    0x002e,
    0x0081,
    0x006a,
    none,
    0x00aa,
    none,
    0x0092,
    none,
    0x00a2,
    none,
    none,
    0x00b6,
    none,
    0x001f,
    none,
    0x0074,
    none,
    none,
    0x004f,
    0x0048,
    0x00d7,
    0x0018,
    none,
    0x00c5,
    0x0019,
    0x000b,
    none,
    0x0075,
    none,
    0x0085,
    0x0073,
    none,
    0x00b3,
    0x0090,
    none,
    none,
    none,
    none,
    0x00a6,
    0x00ac,
    none,
    none,
    none,
    0x0022,
    0x009e,
    none,
    none,
    none,
    none,
    none,
    none,
    none,
    0x00c0,
    none,
    none,
    0x0006,
    none,
    none,
    0x0008,
    none,
    none,
    0x00c1,
    0x00c3,
    0x0004,
    0x008e,
    none,
    none,
    none,
    0x007e,
    none,
    0x000e,
    none,
    0x00d8,
    none,
    0x0066,
    none,
    none,
    none,
    none,
    none,
    none,
    0x001d,
    none,
    none,
    0x0028,
    none,
    0x00af,
    0x0041,
    0x00d2,
    0x0051,
    none,
    none,
    none,
    none,
    none,
    none,
    none,
    0x00d6,
    none,
    0x0098,
    none,
    none,
    none,
    0x002f,
    none,
    0x00d5,
    0x00a3,
    0x004e,
};

static int
string_map_lookup(const char *str)
{
    static const uint32_t prime_factor = 5024183;
    static const uint32_t prime_step = 19;
    const struct string_map_entry *e;
    uint32_t hash, h;
    uint16_t i;
    const char *p;

    hash = 0;
    for (p = str; *p; p++)
        hash = hash * prime_factor + *p;

    h = hash;
    while (1) {
        i = string_map[h & 511];
        if (i == none)
           return -1;
        e = &string_map_entries[i];
        if (e->hash == hash && strcmp(str, strings + e->name) == 0)
            return e->num;
        h += prime_step;
    }

    return -1;
}

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
    VkResult anv_EnumerateInstanceVersion(uint32_t* pApiVersion) __attribute__ ((weak));
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
    void anv_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures) __attribute__ ((weak));
          void anv_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties) __attribute__ ((weak));
          void anv_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2* pFormatProperties) __attribute__ ((weak));
          VkResult anv_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo, VkImageFormatProperties2* pImageFormatProperties) __attribute__ ((weak));
          void anv_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2* pQueueFamilyProperties) __attribute__ ((weak));
          void anv_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties) __attribute__ ((weak));
          void anv_GetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo, uint32_t* pPropertyCount, VkSparseImageFormatProperties2* pProperties) __attribute__ ((weak));
          void anv_CmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites) __attribute__ ((weak));
    void anv_TrimCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlags flags) __attribute__ ((weak));
          void anv_GetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo* pExternalBufferInfo, VkExternalBufferProperties* pExternalBufferProperties) __attribute__ ((weak));
          VkResult anv_GetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult anv_GetMemoryFdPropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, int fd, VkMemoryFdPropertiesKHR* pMemoryFdProperties) __attribute__ ((weak));
    void anv_GetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo, VkExternalSemaphoreProperties* pExternalSemaphoreProperties) __attribute__ ((weak));
          VkResult anv_GetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult anv_ImportSemaphoreFdKHR(VkDevice device, const VkImportSemaphoreFdInfoKHR* pImportSemaphoreFdInfo) __attribute__ ((weak));
    void anv_GetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo, VkExternalFenceProperties* pExternalFenceProperties) __attribute__ ((weak));
          VkResult anv_GetFenceFdKHR(VkDevice device, const VkFenceGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult anv_ImportFenceFdKHR(VkDevice device, const VkImportFenceFdInfoKHR* pImportFenceFdInfo) __attribute__ ((weak));
    VkResult anv_EnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t* pPhysicalDeviceGroupCount, VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties) __attribute__ ((weak));
          void anv_GetDeviceGroupPeerMemoryFeatures(VkDevice device, uint32_t heapIndex, uint32_t localDeviceIndex, uint32_t remoteDeviceIndex, VkPeerMemoryFeatureFlags* pPeerMemoryFeatures) __attribute__ ((weak));
          VkResult anv_BindBufferMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos) __attribute__ ((weak));
          VkResult anv_BindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos) __attribute__ ((weak));
          void anv_CmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask) __attribute__ ((weak));
          VkResult anv_GetDeviceGroupPresentCapabilitiesKHR(VkDevice device, VkDeviceGroupPresentCapabilitiesKHR* pDeviceGroupPresentCapabilities) __attribute__ ((weak));
    VkResult anv_GetDeviceGroupSurfacePresentModesKHR(VkDevice device, VkSurfaceKHR surface, VkDeviceGroupPresentModeFlagsKHR* pModes) __attribute__ ((weak));
    VkResult anv_AcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex) __attribute__ ((weak));
    void anv_CmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) __attribute__ ((weak));
          VkResult anv_GetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pRectCount, VkRect2D* pRects) __attribute__ ((weak));
    VkResult anv_CreateDescriptorUpdateTemplate(VkDevice device, const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate) __attribute__ ((weak));
          void anv_DestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
          void anv_UpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void* pData) __attribute__ ((weak));
          void anv_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplate descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) __attribute__ ((weak));
    VkResult anv_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult anv_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats) __attribute__ ((weak));
    void anv_GetBufferMemoryRequirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) __attribute__ ((weak));
          void anv_GetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) __attribute__ ((weak));
          void anv_GetImageSparseMemoryRequirements2(VkDevice device, const VkImageSparseMemoryRequirementsInfo2* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2* pSparseMemoryRequirements) __attribute__ ((weak));
          VkResult anv_CreateSamplerYcbcrConversion(VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion) __attribute__ ((weak));
          void anv_DestroySamplerYcbcrConversion(VkDevice device, VkSamplerYcbcrConversion ycbcrConversion, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
          void anv_GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* pQueueInfo, VkQueue* pQueue) __attribute__ ((weak));
    void anv_GetDescriptorSetLayoutSupport(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, VkDescriptorSetLayoutSupport* pSupport) __attribute__ ((weak));
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
    .vkEnumerateInstanceVersion = anv_EnumerateInstanceVersion,
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
    .vkGetPhysicalDeviceFeatures2 = anv_GetPhysicalDeviceFeatures2,
    .vkGetPhysicalDeviceFeatures2KHR = anv_GetPhysicalDeviceFeatures2,
    .vkGetPhysicalDeviceProperties2 = anv_GetPhysicalDeviceProperties2,
    .vkGetPhysicalDeviceProperties2KHR = anv_GetPhysicalDeviceProperties2,
    .vkGetPhysicalDeviceFormatProperties2 = anv_GetPhysicalDeviceFormatProperties2,
    .vkGetPhysicalDeviceFormatProperties2KHR = anv_GetPhysicalDeviceFormatProperties2,
    .vkGetPhysicalDeviceImageFormatProperties2 = anv_GetPhysicalDeviceImageFormatProperties2,
    .vkGetPhysicalDeviceImageFormatProperties2KHR = anv_GetPhysicalDeviceImageFormatProperties2,
    .vkGetPhysicalDeviceQueueFamilyProperties2 = anv_GetPhysicalDeviceQueueFamilyProperties2,
    .vkGetPhysicalDeviceQueueFamilyProperties2KHR = anv_GetPhysicalDeviceQueueFamilyProperties2,
    .vkGetPhysicalDeviceMemoryProperties2 = anv_GetPhysicalDeviceMemoryProperties2,
    .vkGetPhysicalDeviceMemoryProperties2KHR = anv_GetPhysicalDeviceMemoryProperties2,
    .vkGetPhysicalDeviceSparseImageFormatProperties2 = anv_GetPhysicalDeviceSparseImageFormatProperties2,
    .vkGetPhysicalDeviceSparseImageFormatProperties2KHR = anv_GetPhysicalDeviceSparseImageFormatProperties2,
    .vkCmdPushDescriptorSetKHR = anv_CmdPushDescriptorSetKHR,
    .vkTrimCommandPool = anv_TrimCommandPool,
    .vkTrimCommandPoolKHR = anv_TrimCommandPool,
    .vkGetPhysicalDeviceExternalBufferProperties = anv_GetPhysicalDeviceExternalBufferProperties,
    .vkGetPhysicalDeviceExternalBufferPropertiesKHR = anv_GetPhysicalDeviceExternalBufferProperties,
    .vkGetMemoryFdKHR = anv_GetMemoryFdKHR,
    .vkGetMemoryFdPropertiesKHR = anv_GetMemoryFdPropertiesKHR,
    .vkGetPhysicalDeviceExternalSemaphoreProperties = anv_GetPhysicalDeviceExternalSemaphoreProperties,
    .vkGetPhysicalDeviceExternalSemaphorePropertiesKHR = anv_GetPhysicalDeviceExternalSemaphoreProperties,
    .vkGetSemaphoreFdKHR = anv_GetSemaphoreFdKHR,
    .vkImportSemaphoreFdKHR = anv_ImportSemaphoreFdKHR,
    .vkGetPhysicalDeviceExternalFenceProperties = anv_GetPhysicalDeviceExternalFenceProperties,
    .vkGetPhysicalDeviceExternalFencePropertiesKHR = anv_GetPhysicalDeviceExternalFenceProperties,
    .vkGetFenceFdKHR = anv_GetFenceFdKHR,
    .vkImportFenceFdKHR = anv_ImportFenceFdKHR,
    .vkEnumeratePhysicalDeviceGroups = anv_EnumeratePhysicalDeviceGroups,
    .vkEnumeratePhysicalDeviceGroupsKHR = anv_EnumeratePhysicalDeviceGroups,
    .vkGetDeviceGroupPeerMemoryFeatures = anv_GetDeviceGroupPeerMemoryFeatures,
    .vkGetDeviceGroupPeerMemoryFeaturesKHR = anv_GetDeviceGroupPeerMemoryFeatures,
    .vkBindBufferMemory2 = anv_BindBufferMemory2,
    .vkBindBufferMemory2KHR = anv_BindBufferMemory2,
    .vkBindImageMemory2 = anv_BindImageMemory2,
    .vkBindImageMemory2KHR = anv_BindImageMemory2,
    .vkCmdSetDeviceMask = anv_CmdSetDeviceMask,
    .vkCmdSetDeviceMaskKHR = anv_CmdSetDeviceMask,
    .vkGetDeviceGroupPresentCapabilitiesKHR = anv_GetDeviceGroupPresentCapabilitiesKHR,
    .vkGetDeviceGroupSurfacePresentModesKHR = anv_GetDeviceGroupSurfacePresentModesKHR,
    .vkAcquireNextImage2KHR = anv_AcquireNextImage2KHR,
    .vkCmdDispatchBase = anv_CmdDispatchBase,
    .vkCmdDispatchBaseKHR = anv_CmdDispatchBase,
    .vkGetPhysicalDevicePresentRectanglesKHR = anv_GetPhysicalDevicePresentRectanglesKHR,
    .vkCreateDescriptorUpdateTemplate = anv_CreateDescriptorUpdateTemplate,
    .vkCreateDescriptorUpdateTemplateKHR = anv_CreateDescriptorUpdateTemplate,
    .vkDestroyDescriptorUpdateTemplate = anv_DestroyDescriptorUpdateTemplate,
    .vkDestroyDescriptorUpdateTemplateKHR = anv_DestroyDescriptorUpdateTemplate,
    .vkUpdateDescriptorSetWithTemplate = anv_UpdateDescriptorSetWithTemplate,
    .vkUpdateDescriptorSetWithTemplateKHR = anv_UpdateDescriptorSetWithTemplate,
    .vkCmdPushDescriptorSetWithTemplateKHR = anv_CmdPushDescriptorSetWithTemplateKHR,
    .vkGetPhysicalDeviceSurfaceCapabilities2KHR = anv_GetPhysicalDeviceSurfaceCapabilities2KHR,
    .vkGetPhysicalDeviceSurfaceFormats2KHR = anv_GetPhysicalDeviceSurfaceFormats2KHR,
    .vkGetBufferMemoryRequirements2 = anv_GetBufferMemoryRequirements2,
    .vkGetBufferMemoryRequirements2KHR = anv_GetBufferMemoryRequirements2,
    .vkGetImageMemoryRequirements2 = anv_GetImageMemoryRequirements2,
    .vkGetImageMemoryRequirements2KHR = anv_GetImageMemoryRequirements2,
    .vkGetImageSparseMemoryRequirements2 = anv_GetImageSparseMemoryRequirements2,
    .vkGetImageSparseMemoryRequirements2KHR = anv_GetImageSparseMemoryRequirements2,
    .vkCreateSamplerYcbcrConversion = anv_CreateSamplerYcbcrConversion,
    .vkCreateSamplerYcbcrConversionKHR = anv_CreateSamplerYcbcrConversion,
    .vkDestroySamplerYcbcrConversion = anv_DestroySamplerYcbcrConversion,
    .vkDestroySamplerYcbcrConversionKHR = anv_DestroySamplerYcbcrConversion,
    .vkGetDeviceQueue2 = anv_GetDeviceQueue2,
    .vkGetDescriptorSetLayoutSupport = anv_GetDescriptorSetLayoutSupport,
    .vkGetDescriptorSetLayoutSupportKHR = anv_GetDescriptorSetLayoutSupport,
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
    VkResult gen7_EnumerateInstanceVersion(uint32_t* pApiVersion) __attribute__ ((weak));
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
    void gen7_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures) __attribute__ ((weak));
          void gen7_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties) __attribute__ ((weak));
          void gen7_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2* pFormatProperties) __attribute__ ((weak));
          VkResult gen7_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo, VkImageFormatProperties2* pImageFormatProperties) __attribute__ ((weak));
          void gen7_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2* pQueueFamilyProperties) __attribute__ ((weak));
          void gen7_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties) __attribute__ ((weak));
          void gen7_GetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo, uint32_t* pPropertyCount, VkSparseImageFormatProperties2* pProperties) __attribute__ ((weak));
          void gen7_CmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites) __attribute__ ((weak));
    void gen7_TrimCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlags flags) __attribute__ ((weak));
          void gen7_GetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo* pExternalBufferInfo, VkExternalBufferProperties* pExternalBufferProperties) __attribute__ ((weak));
          VkResult gen7_GetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen7_GetMemoryFdPropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, int fd, VkMemoryFdPropertiesKHR* pMemoryFdProperties) __attribute__ ((weak));
    void gen7_GetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo, VkExternalSemaphoreProperties* pExternalSemaphoreProperties) __attribute__ ((weak));
          VkResult gen7_GetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen7_ImportSemaphoreFdKHR(VkDevice device, const VkImportSemaphoreFdInfoKHR* pImportSemaphoreFdInfo) __attribute__ ((weak));
    void gen7_GetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo, VkExternalFenceProperties* pExternalFenceProperties) __attribute__ ((weak));
          VkResult gen7_GetFenceFdKHR(VkDevice device, const VkFenceGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen7_ImportFenceFdKHR(VkDevice device, const VkImportFenceFdInfoKHR* pImportFenceFdInfo) __attribute__ ((weak));
    VkResult gen7_EnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t* pPhysicalDeviceGroupCount, VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties) __attribute__ ((weak));
          void gen7_GetDeviceGroupPeerMemoryFeatures(VkDevice device, uint32_t heapIndex, uint32_t localDeviceIndex, uint32_t remoteDeviceIndex, VkPeerMemoryFeatureFlags* pPeerMemoryFeatures) __attribute__ ((weak));
          VkResult gen7_BindBufferMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos) __attribute__ ((weak));
          VkResult gen7_BindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos) __attribute__ ((weak));
          void gen7_CmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask) __attribute__ ((weak));
          VkResult gen7_GetDeviceGroupPresentCapabilitiesKHR(VkDevice device, VkDeviceGroupPresentCapabilitiesKHR* pDeviceGroupPresentCapabilities) __attribute__ ((weak));
    VkResult gen7_GetDeviceGroupSurfacePresentModesKHR(VkDevice device, VkSurfaceKHR surface, VkDeviceGroupPresentModeFlagsKHR* pModes) __attribute__ ((weak));
    VkResult gen7_AcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex) __attribute__ ((weak));
    void gen7_CmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) __attribute__ ((weak));
          VkResult gen7_GetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pRectCount, VkRect2D* pRects) __attribute__ ((weak));
    VkResult gen7_CreateDescriptorUpdateTemplate(VkDevice device, const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate) __attribute__ ((weak));
          void gen7_DestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
          void gen7_UpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void* pData) __attribute__ ((weak));
          void gen7_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplate descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) __attribute__ ((weak));
    VkResult gen7_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen7_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats) __attribute__ ((weak));
    void gen7_GetBufferMemoryRequirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) __attribute__ ((weak));
          void gen7_GetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) __attribute__ ((weak));
          void gen7_GetImageSparseMemoryRequirements2(VkDevice device, const VkImageSparseMemoryRequirementsInfo2* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2* pSparseMemoryRequirements) __attribute__ ((weak));
          VkResult gen7_CreateSamplerYcbcrConversion(VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion) __attribute__ ((weak));
          void gen7_DestroySamplerYcbcrConversion(VkDevice device, VkSamplerYcbcrConversion ycbcrConversion, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
          void gen7_GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* pQueueInfo, VkQueue* pQueue) __attribute__ ((weak));
    void gen7_GetDescriptorSetLayoutSupport(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, VkDescriptorSetLayoutSupport* pSupport) __attribute__ ((weak));
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
    .vkEnumerateInstanceVersion = gen7_EnumerateInstanceVersion,
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
    .vkGetPhysicalDeviceFeatures2 = gen7_GetPhysicalDeviceFeatures2,
    .vkGetPhysicalDeviceFeatures2KHR = gen7_GetPhysicalDeviceFeatures2,
    .vkGetPhysicalDeviceProperties2 = gen7_GetPhysicalDeviceProperties2,
    .vkGetPhysicalDeviceProperties2KHR = gen7_GetPhysicalDeviceProperties2,
    .vkGetPhysicalDeviceFormatProperties2 = gen7_GetPhysicalDeviceFormatProperties2,
    .vkGetPhysicalDeviceFormatProperties2KHR = gen7_GetPhysicalDeviceFormatProperties2,
    .vkGetPhysicalDeviceImageFormatProperties2 = gen7_GetPhysicalDeviceImageFormatProperties2,
    .vkGetPhysicalDeviceImageFormatProperties2KHR = gen7_GetPhysicalDeviceImageFormatProperties2,
    .vkGetPhysicalDeviceQueueFamilyProperties2 = gen7_GetPhysicalDeviceQueueFamilyProperties2,
    .vkGetPhysicalDeviceQueueFamilyProperties2KHR = gen7_GetPhysicalDeviceQueueFamilyProperties2,
    .vkGetPhysicalDeviceMemoryProperties2 = gen7_GetPhysicalDeviceMemoryProperties2,
    .vkGetPhysicalDeviceMemoryProperties2KHR = gen7_GetPhysicalDeviceMemoryProperties2,
    .vkGetPhysicalDeviceSparseImageFormatProperties2 = gen7_GetPhysicalDeviceSparseImageFormatProperties2,
    .vkGetPhysicalDeviceSparseImageFormatProperties2KHR = gen7_GetPhysicalDeviceSparseImageFormatProperties2,
    .vkCmdPushDescriptorSetKHR = gen7_CmdPushDescriptorSetKHR,
    .vkTrimCommandPool = gen7_TrimCommandPool,
    .vkTrimCommandPoolKHR = gen7_TrimCommandPool,
    .vkGetPhysicalDeviceExternalBufferProperties = gen7_GetPhysicalDeviceExternalBufferProperties,
    .vkGetPhysicalDeviceExternalBufferPropertiesKHR = gen7_GetPhysicalDeviceExternalBufferProperties,
    .vkGetMemoryFdKHR = gen7_GetMemoryFdKHR,
    .vkGetMemoryFdPropertiesKHR = gen7_GetMemoryFdPropertiesKHR,
    .vkGetPhysicalDeviceExternalSemaphoreProperties = gen7_GetPhysicalDeviceExternalSemaphoreProperties,
    .vkGetPhysicalDeviceExternalSemaphorePropertiesKHR = gen7_GetPhysicalDeviceExternalSemaphoreProperties,
    .vkGetSemaphoreFdKHR = gen7_GetSemaphoreFdKHR,
    .vkImportSemaphoreFdKHR = gen7_ImportSemaphoreFdKHR,
    .vkGetPhysicalDeviceExternalFenceProperties = gen7_GetPhysicalDeviceExternalFenceProperties,
    .vkGetPhysicalDeviceExternalFencePropertiesKHR = gen7_GetPhysicalDeviceExternalFenceProperties,
    .vkGetFenceFdKHR = gen7_GetFenceFdKHR,
    .vkImportFenceFdKHR = gen7_ImportFenceFdKHR,
    .vkEnumeratePhysicalDeviceGroups = gen7_EnumeratePhysicalDeviceGroups,
    .vkEnumeratePhysicalDeviceGroupsKHR = gen7_EnumeratePhysicalDeviceGroups,
    .vkGetDeviceGroupPeerMemoryFeatures = gen7_GetDeviceGroupPeerMemoryFeatures,
    .vkGetDeviceGroupPeerMemoryFeaturesKHR = gen7_GetDeviceGroupPeerMemoryFeatures,
    .vkBindBufferMemory2 = gen7_BindBufferMemory2,
    .vkBindBufferMemory2KHR = gen7_BindBufferMemory2,
    .vkBindImageMemory2 = gen7_BindImageMemory2,
    .vkBindImageMemory2KHR = gen7_BindImageMemory2,
    .vkCmdSetDeviceMask = gen7_CmdSetDeviceMask,
    .vkCmdSetDeviceMaskKHR = gen7_CmdSetDeviceMask,
    .vkGetDeviceGroupPresentCapabilitiesKHR = gen7_GetDeviceGroupPresentCapabilitiesKHR,
    .vkGetDeviceGroupSurfacePresentModesKHR = gen7_GetDeviceGroupSurfacePresentModesKHR,
    .vkAcquireNextImage2KHR = gen7_AcquireNextImage2KHR,
    .vkCmdDispatchBase = gen7_CmdDispatchBase,
    .vkCmdDispatchBaseKHR = gen7_CmdDispatchBase,
    .vkGetPhysicalDevicePresentRectanglesKHR = gen7_GetPhysicalDevicePresentRectanglesKHR,
    .vkCreateDescriptorUpdateTemplate = gen7_CreateDescriptorUpdateTemplate,
    .vkCreateDescriptorUpdateTemplateKHR = gen7_CreateDescriptorUpdateTemplate,
    .vkDestroyDescriptorUpdateTemplate = gen7_DestroyDescriptorUpdateTemplate,
    .vkDestroyDescriptorUpdateTemplateKHR = gen7_DestroyDescriptorUpdateTemplate,
    .vkUpdateDescriptorSetWithTemplate = gen7_UpdateDescriptorSetWithTemplate,
    .vkUpdateDescriptorSetWithTemplateKHR = gen7_UpdateDescriptorSetWithTemplate,
    .vkCmdPushDescriptorSetWithTemplateKHR = gen7_CmdPushDescriptorSetWithTemplateKHR,
    .vkGetPhysicalDeviceSurfaceCapabilities2KHR = gen7_GetPhysicalDeviceSurfaceCapabilities2KHR,
    .vkGetPhysicalDeviceSurfaceFormats2KHR = gen7_GetPhysicalDeviceSurfaceFormats2KHR,
    .vkGetBufferMemoryRequirements2 = gen7_GetBufferMemoryRequirements2,
    .vkGetBufferMemoryRequirements2KHR = gen7_GetBufferMemoryRequirements2,
    .vkGetImageMemoryRequirements2 = gen7_GetImageMemoryRequirements2,
    .vkGetImageMemoryRequirements2KHR = gen7_GetImageMemoryRequirements2,
    .vkGetImageSparseMemoryRequirements2 = gen7_GetImageSparseMemoryRequirements2,
    .vkGetImageSparseMemoryRequirements2KHR = gen7_GetImageSparseMemoryRequirements2,
    .vkCreateSamplerYcbcrConversion = gen7_CreateSamplerYcbcrConversion,
    .vkCreateSamplerYcbcrConversionKHR = gen7_CreateSamplerYcbcrConversion,
    .vkDestroySamplerYcbcrConversion = gen7_DestroySamplerYcbcrConversion,
    .vkDestroySamplerYcbcrConversionKHR = gen7_DestroySamplerYcbcrConversion,
    .vkGetDeviceQueue2 = gen7_GetDeviceQueue2,
    .vkGetDescriptorSetLayoutSupport = gen7_GetDescriptorSetLayoutSupport,
    .vkGetDescriptorSetLayoutSupportKHR = gen7_GetDescriptorSetLayoutSupport,
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
    VkResult gen75_EnumerateInstanceVersion(uint32_t* pApiVersion) __attribute__ ((weak));
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
    void gen75_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures) __attribute__ ((weak));
          void gen75_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties) __attribute__ ((weak));
          void gen75_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2* pFormatProperties) __attribute__ ((weak));
          VkResult gen75_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo, VkImageFormatProperties2* pImageFormatProperties) __attribute__ ((weak));
          void gen75_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2* pQueueFamilyProperties) __attribute__ ((weak));
          void gen75_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties) __attribute__ ((weak));
          void gen75_GetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo, uint32_t* pPropertyCount, VkSparseImageFormatProperties2* pProperties) __attribute__ ((weak));
          void gen75_CmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites) __attribute__ ((weak));
    void gen75_TrimCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlags flags) __attribute__ ((weak));
          void gen75_GetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo* pExternalBufferInfo, VkExternalBufferProperties* pExternalBufferProperties) __attribute__ ((weak));
          VkResult gen75_GetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen75_GetMemoryFdPropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, int fd, VkMemoryFdPropertiesKHR* pMemoryFdProperties) __attribute__ ((weak));
    void gen75_GetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo, VkExternalSemaphoreProperties* pExternalSemaphoreProperties) __attribute__ ((weak));
          VkResult gen75_GetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen75_ImportSemaphoreFdKHR(VkDevice device, const VkImportSemaphoreFdInfoKHR* pImportSemaphoreFdInfo) __attribute__ ((weak));
    void gen75_GetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo, VkExternalFenceProperties* pExternalFenceProperties) __attribute__ ((weak));
          VkResult gen75_GetFenceFdKHR(VkDevice device, const VkFenceGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen75_ImportFenceFdKHR(VkDevice device, const VkImportFenceFdInfoKHR* pImportFenceFdInfo) __attribute__ ((weak));
    VkResult gen75_EnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t* pPhysicalDeviceGroupCount, VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties) __attribute__ ((weak));
          void gen75_GetDeviceGroupPeerMemoryFeatures(VkDevice device, uint32_t heapIndex, uint32_t localDeviceIndex, uint32_t remoteDeviceIndex, VkPeerMemoryFeatureFlags* pPeerMemoryFeatures) __attribute__ ((weak));
          VkResult gen75_BindBufferMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos) __attribute__ ((weak));
          VkResult gen75_BindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos) __attribute__ ((weak));
          void gen75_CmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask) __attribute__ ((weak));
          VkResult gen75_GetDeviceGroupPresentCapabilitiesKHR(VkDevice device, VkDeviceGroupPresentCapabilitiesKHR* pDeviceGroupPresentCapabilities) __attribute__ ((weak));
    VkResult gen75_GetDeviceGroupSurfacePresentModesKHR(VkDevice device, VkSurfaceKHR surface, VkDeviceGroupPresentModeFlagsKHR* pModes) __attribute__ ((weak));
    VkResult gen75_AcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex) __attribute__ ((weak));
    void gen75_CmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) __attribute__ ((weak));
          VkResult gen75_GetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pRectCount, VkRect2D* pRects) __attribute__ ((weak));
    VkResult gen75_CreateDescriptorUpdateTemplate(VkDevice device, const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate) __attribute__ ((weak));
          void gen75_DestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
          void gen75_UpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void* pData) __attribute__ ((weak));
          void gen75_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplate descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) __attribute__ ((weak));
    VkResult gen75_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen75_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats) __attribute__ ((weak));
    void gen75_GetBufferMemoryRequirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) __attribute__ ((weak));
          void gen75_GetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) __attribute__ ((weak));
          void gen75_GetImageSparseMemoryRequirements2(VkDevice device, const VkImageSparseMemoryRequirementsInfo2* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2* pSparseMemoryRequirements) __attribute__ ((weak));
          VkResult gen75_CreateSamplerYcbcrConversion(VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion) __attribute__ ((weak));
          void gen75_DestroySamplerYcbcrConversion(VkDevice device, VkSamplerYcbcrConversion ycbcrConversion, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
          void gen75_GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* pQueueInfo, VkQueue* pQueue) __attribute__ ((weak));
    void gen75_GetDescriptorSetLayoutSupport(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, VkDescriptorSetLayoutSupport* pSupport) __attribute__ ((weak));
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
    .vkEnumerateInstanceVersion = gen75_EnumerateInstanceVersion,
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
    .vkGetPhysicalDeviceFeatures2 = gen75_GetPhysicalDeviceFeatures2,
    .vkGetPhysicalDeviceFeatures2KHR = gen75_GetPhysicalDeviceFeatures2,
    .vkGetPhysicalDeviceProperties2 = gen75_GetPhysicalDeviceProperties2,
    .vkGetPhysicalDeviceProperties2KHR = gen75_GetPhysicalDeviceProperties2,
    .vkGetPhysicalDeviceFormatProperties2 = gen75_GetPhysicalDeviceFormatProperties2,
    .vkGetPhysicalDeviceFormatProperties2KHR = gen75_GetPhysicalDeviceFormatProperties2,
    .vkGetPhysicalDeviceImageFormatProperties2 = gen75_GetPhysicalDeviceImageFormatProperties2,
    .vkGetPhysicalDeviceImageFormatProperties2KHR = gen75_GetPhysicalDeviceImageFormatProperties2,
    .vkGetPhysicalDeviceQueueFamilyProperties2 = gen75_GetPhysicalDeviceQueueFamilyProperties2,
    .vkGetPhysicalDeviceQueueFamilyProperties2KHR = gen75_GetPhysicalDeviceQueueFamilyProperties2,
    .vkGetPhysicalDeviceMemoryProperties2 = gen75_GetPhysicalDeviceMemoryProperties2,
    .vkGetPhysicalDeviceMemoryProperties2KHR = gen75_GetPhysicalDeviceMemoryProperties2,
    .vkGetPhysicalDeviceSparseImageFormatProperties2 = gen75_GetPhysicalDeviceSparseImageFormatProperties2,
    .vkGetPhysicalDeviceSparseImageFormatProperties2KHR = gen75_GetPhysicalDeviceSparseImageFormatProperties2,
    .vkCmdPushDescriptorSetKHR = gen75_CmdPushDescriptorSetKHR,
    .vkTrimCommandPool = gen75_TrimCommandPool,
    .vkTrimCommandPoolKHR = gen75_TrimCommandPool,
    .vkGetPhysicalDeviceExternalBufferProperties = gen75_GetPhysicalDeviceExternalBufferProperties,
    .vkGetPhysicalDeviceExternalBufferPropertiesKHR = gen75_GetPhysicalDeviceExternalBufferProperties,
    .vkGetMemoryFdKHR = gen75_GetMemoryFdKHR,
    .vkGetMemoryFdPropertiesKHR = gen75_GetMemoryFdPropertiesKHR,
    .vkGetPhysicalDeviceExternalSemaphoreProperties = gen75_GetPhysicalDeviceExternalSemaphoreProperties,
    .vkGetPhysicalDeviceExternalSemaphorePropertiesKHR = gen75_GetPhysicalDeviceExternalSemaphoreProperties,
    .vkGetSemaphoreFdKHR = gen75_GetSemaphoreFdKHR,
    .vkImportSemaphoreFdKHR = gen75_ImportSemaphoreFdKHR,
    .vkGetPhysicalDeviceExternalFenceProperties = gen75_GetPhysicalDeviceExternalFenceProperties,
    .vkGetPhysicalDeviceExternalFencePropertiesKHR = gen75_GetPhysicalDeviceExternalFenceProperties,
    .vkGetFenceFdKHR = gen75_GetFenceFdKHR,
    .vkImportFenceFdKHR = gen75_ImportFenceFdKHR,
    .vkEnumeratePhysicalDeviceGroups = gen75_EnumeratePhysicalDeviceGroups,
    .vkEnumeratePhysicalDeviceGroupsKHR = gen75_EnumeratePhysicalDeviceGroups,
    .vkGetDeviceGroupPeerMemoryFeatures = gen75_GetDeviceGroupPeerMemoryFeatures,
    .vkGetDeviceGroupPeerMemoryFeaturesKHR = gen75_GetDeviceGroupPeerMemoryFeatures,
    .vkBindBufferMemory2 = gen75_BindBufferMemory2,
    .vkBindBufferMemory2KHR = gen75_BindBufferMemory2,
    .vkBindImageMemory2 = gen75_BindImageMemory2,
    .vkBindImageMemory2KHR = gen75_BindImageMemory2,
    .vkCmdSetDeviceMask = gen75_CmdSetDeviceMask,
    .vkCmdSetDeviceMaskKHR = gen75_CmdSetDeviceMask,
    .vkGetDeviceGroupPresentCapabilitiesKHR = gen75_GetDeviceGroupPresentCapabilitiesKHR,
    .vkGetDeviceGroupSurfacePresentModesKHR = gen75_GetDeviceGroupSurfacePresentModesKHR,
    .vkAcquireNextImage2KHR = gen75_AcquireNextImage2KHR,
    .vkCmdDispatchBase = gen75_CmdDispatchBase,
    .vkCmdDispatchBaseKHR = gen75_CmdDispatchBase,
    .vkGetPhysicalDevicePresentRectanglesKHR = gen75_GetPhysicalDevicePresentRectanglesKHR,
    .vkCreateDescriptorUpdateTemplate = gen75_CreateDescriptorUpdateTemplate,
    .vkCreateDescriptorUpdateTemplateKHR = gen75_CreateDescriptorUpdateTemplate,
    .vkDestroyDescriptorUpdateTemplate = gen75_DestroyDescriptorUpdateTemplate,
    .vkDestroyDescriptorUpdateTemplateKHR = gen75_DestroyDescriptorUpdateTemplate,
    .vkUpdateDescriptorSetWithTemplate = gen75_UpdateDescriptorSetWithTemplate,
    .vkUpdateDescriptorSetWithTemplateKHR = gen75_UpdateDescriptorSetWithTemplate,
    .vkCmdPushDescriptorSetWithTemplateKHR = gen75_CmdPushDescriptorSetWithTemplateKHR,
    .vkGetPhysicalDeviceSurfaceCapabilities2KHR = gen75_GetPhysicalDeviceSurfaceCapabilities2KHR,
    .vkGetPhysicalDeviceSurfaceFormats2KHR = gen75_GetPhysicalDeviceSurfaceFormats2KHR,
    .vkGetBufferMemoryRequirements2 = gen75_GetBufferMemoryRequirements2,
    .vkGetBufferMemoryRequirements2KHR = gen75_GetBufferMemoryRequirements2,
    .vkGetImageMemoryRequirements2 = gen75_GetImageMemoryRequirements2,
    .vkGetImageMemoryRequirements2KHR = gen75_GetImageMemoryRequirements2,
    .vkGetImageSparseMemoryRequirements2 = gen75_GetImageSparseMemoryRequirements2,
    .vkGetImageSparseMemoryRequirements2KHR = gen75_GetImageSparseMemoryRequirements2,
    .vkCreateSamplerYcbcrConversion = gen75_CreateSamplerYcbcrConversion,
    .vkCreateSamplerYcbcrConversionKHR = gen75_CreateSamplerYcbcrConversion,
    .vkDestroySamplerYcbcrConversion = gen75_DestroySamplerYcbcrConversion,
    .vkDestroySamplerYcbcrConversionKHR = gen75_DestroySamplerYcbcrConversion,
    .vkGetDeviceQueue2 = gen75_GetDeviceQueue2,
    .vkGetDescriptorSetLayoutSupport = gen75_GetDescriptorSetLayoutSupport,
    .vkGetDescriptorSetLayoutSupportKHR = gen75_GetDescriptorSetLayoutSupport,
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
    VkResult gen8_EnumerateInstanceVersion(uint32_t* pApiVersion) __attribute__ ((weak));
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
    void gen8_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures) __attribute__ ((weak));
          void gen8_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties) __attribute__ ((weak));
          void gen8_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2* pFormatProperties) __attribute__ ((weak));
          VkResult gen8_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo, VkImageFormatProperties2* pImageFormatProperties) __attribute__ ((weak));
          void gen8_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2* pQueueFamilyProperties) __attribute__ ((weak));
          void gen8_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties) __attribute__ ((weak));
          void gen8_GetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo, uint32_t* pPropertyCount, VkSparseImageFormatProperties2* pProperties) __attribute__ ((weak));
          void gen8_CmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites) __attribute__ ((weak));
    void gen8_TrimCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlags flags) __attribute__ ((weak));
          void gen8_GetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo* pExternalBufferInfo, VkExternalBufferProperties* pExternalBufferProperties) __attribute__ ((weak));
          VkResult gen8_GetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen8_GetMemoryFdPropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, int fd, VkMemoryFdPropertiesKHR* pMemoryFdProperties) __attribute__ ((weak));
    void gen8_GetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo, VkExternalSemaphoreProperties* pExternalSemaphoreProperties) __attribute__ ((weak));
          VkResult gen8_GetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen8_ImportSemaphoreFdKHR(VkDevice device, const VkImportSemaphoreFdInfoKHR* pImportSemaphoreFdInfo) __attribute__ ((weak));
    void gen8_GetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo, VkExternalFenceProperties* pExternalFenceProperties) __attribute__ ((weak));
          VkResult gen8_GetFenceFdKHR(VkDevice device, const VkFenceGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen8_ImportFenceFdKHR(VkDevice device, const VkImportFenceFdInfoKHR* pImportFenceFdInfo) __attribute__ ((weak));
    VkResult gen8_EnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t* pPhysicalDeviceGroupCount, VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties) __attribute__ ((weak));
          void gen8_GetDeviceGroupPeerMemoryFeatures(VkDevice device, uint32_t heapIndex, uint32_t localDeviceIndex, uint32_t remoteDeviceIndex, VkPeerMemoryFeatureFlags* pPeerMemoryFeatures) __attribute__ ((weak));
          VkResult gen8_BindBufferMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos) __attribute__ ((weak));
          VkResult gen8_BindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos) __attribute__ ((weak));
          void gen8_CmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask) __attribute__ ((weak));
          VkResult gen8_GetDeviceGroupPresentCapabilitiesKHR(VkDevice device, VkDeviceGroupPresentCapabilitiesKHR* pDeviceGroupPresentCapabilities) __attribute__ ((weak));
    VkResult gen8_GetDeviceGroupSurfacePresentModesKHR(VkDevice device, VkSurfaceKHR surface, VkDeviceGroupPresentModeFlagsKHR* pModes) __attribute__ ((weak));
    VkResult gen8_AcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex) __attribute__ ((weak));
    void gen8_CmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) __attribute__ ((weak));
          VkResult gen8_GetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pRectCount, VkRect2D* pRects) __attribute__ ((weak));
    VkResult gen8_CreateDescriptorUpdateTemplate(VkDevice device, const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate) __attribute__ ((weak));
          void gen8_DestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
          void gen8_UpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void* pData) __attribute__ ((weak));
          void gen8_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplate descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) __attribute__ ((weak));
    VkResult gen8_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen8_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats) __attribute__ ((weak));
    void gen8_GetBufferMemoryRequirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) __attribute__ ((weak));
          void gen8_GetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) __attribute__ ((weak));
          void gen8_GetImageSparseMemoryRequirements2(VkDevice device, const VkImageSparseMemoryRequirementsInfo2* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2* pSparseMemoryRequirements) __attribute__ ((weak));
          VkResult gen8_CreateSamplerYcbcrConversion(VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion) __attribute__ ((weak));
          void gen8_DestroySamplerYcbcrConversion(VkDevice device, VkSamplerYcbcrConversion ycbcrConversion, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
          void gen8_GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* pQueueInfo, VkQueue* pQueue) __attribute__ ((weak));
    void gen8_GetDescriptorSetLayoutSupport(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, VkDescriptorSetLayoutSupport* pSupport) __attribute__ ((weak));
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
    .vkEnumerateInstanceVersion = gen8_EnumerateInstanceVersion,
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
    .vkGetPhysicalDeviceFeatures2 = gen8_GetPhysicalDeviceFeatures2,
    .vkGetPhysicalDeviceFeatures2KHR = gen8_GetPhysicalDeviceFeatures2,
    .vkGetPhysicalDeviceProperties2 = gen8_GetPhysicalDeviceProperties2,
    .vkGetPhysicalDeviceProperties2KHR = gen8_GetPhysicalDeviceProperties2,
    .vkGetPhysicalDeviceFormatProperties2 = gen8_GetPhysicalDeviceFormatProperties2,
    .vkGetPhysicalDeviceFormatProperties2KHR = gen8_GetPhysicalDeviceFormatProperties2,
    .vkGetPhysicalDeviceImageFormatProperties2 = gen8_GetPhysicalDeviceImageFormatProperties2,
    .vkGetPhysicalDeviceImageFormatProperties2KHR = gen8_GetPhysicalDeviceImageFormatProperties2,
    .vkGetPhysicalDeviceQueueFamilyProperties2 = gen8_GetPhysicalDeviceQueueFamilyProperties2,
    .vkGetPhysicalDeviceQueueFamilyProperties2KHR = gen8_GetPhysicalDeviceQueueFamilyProperties2,
    .vkGetPhysicalDeviceMemoryProperties2 = gen8_GetPhysicalDeviceMemoryProperties2,
    .vkGetPhysicalDeviceMemoryProperties2KHR = gen8_GetPhysicalDeviceMemoryProperties2,
    .vkGetPhysicalDeviceSparseImageFormatProperties2 = gen8_GetPhysicalDeviceSparseImageFormatProperties2,
    .vkGetPhysicalDeviceSparseImageFormatProperties2KHR = gen8_GetPhysicalDeviceSparseImageFormatProperties2,
    .vkCmdPushDescriptorSetKHR = gen8_CmdPushDescriptorSetKHR,
    .vkTrimCommandPool = gen8_TrimCommandPool,
    .vkTrimCommandPoolKHR = gen8_TrimCommandPool,
    .vkGetPhysicalDeviceExternalBufferProperties = gen8_GetPhysicalDeviceExternalBufferProperties,
    .vkGetPhysicalDeviceExternalBufferPropertiesKHR = gen8_GetPhysicalDeviceExternalBufferProperties,
    .vkGetMemoryFdKHR = gen8_GetMemoryFdKHR,
    .vkGetMemoryFdPropertiesKHR = gen8_GetMemoryFdPropertiesKHR,
    .vkGetPhysicalDeviceExternalSemaphoreProperties = gen8_GetPhysicalDeviceExternalSemaphoreProperties,
    .vkGetPhysicalDeviceExternalSemaphorePropertiesKHR = gen8_GetPhysicalDeviceExternalSemaphoreProperties,
    .vkGetSemaphoreFdKHR = gen8_GetSemaphoreFdKHR,
    .vkImportSemaphoreFdKHR = gen8_ImportSemaphoreFdKHR,
    .vkGetPhysicalDeviceExternalFenceProperties = gen8_GetPhysicalDeviceExternalFenceProperties,
    .vkGetPhysicalDeviceExternalFencePropertiesKHR = gen8_GetPhysicalDeviceExternalFenceProperties,
    .vkGetFenceFdKHR = gen8_GetFenceFdKHR,
    .vkImportFenceFdKHR = gen8_ImportFenceFdKHR,
    .vkEnumeratePhysicalDeviceGroups = gen8_EnumeratePhysicalDeviceGroups,
    .vkEnumeratePhysicalDeviceGroupsKHR = gen8_EnumeratePhysicalDeviceGroups,
    .vkGetDeviceGroupPeerMemoryFeatures = gen8_GetDeviceGroupPeerMemoryFeatures,
    .vkGetDeviceGroupPeerMemoryFeaturesKHR = gen8_GetDeviceGroupPeerMemoryFeatures,
    .vkBindBufferMemory2 = gen8_BindBufferMemory2,
    .vkBindBufferMemory2KHR = gen8_BindBufferMemory2,
    .vkBindImageMemory2 = gen8_BindImageMemory2,
    .vkBindImageMemory2KHR = gen8_BindImageMemory2,
    .vkCmdSetDeviceMask = gen8_CmdSetDeviceMask,
    .vkCmdSetDeviceMaskKHR = gen8_CmdSetDeviceMask,
    .vkGetDeviceGroupPresentCapabilitiesKHR = gen8_GetDeviceGroupPresentCapabilitiesKHR,
    .vkGetDeviceGroupSurfacePresentModesKHR = gen8_GetDeviceGroupSurfacePresentModesKHR,
    .vkAcquireNextImage2KHR = gen8_AcquireNextImage2KHR,
    .vkCmdDispatchBase = gen8_CmdDispatchBase,
    .vkCmdDispatchBaseKHR = gen8_CmdDispatchBase,
    .vkGetPhysicalDevicePresentRectanglesKHR = gen8_GetPhysicalDevicePresentRectanglesKHR,
    .vkCreateDescriptorUpdateTemplate = gen8_CreateDescriptorUpdateTemplate,
    .vkCreateDescriptorUpdateTemplateKHR = gen8_CreateDescriptorUpdateTemplate,
    .vkDestroyDescriptorUpdateTemplate = gen8_DestroyDescriptorUpdateTemplate,
    .vkDestroyDescriptorUpdateTemplateKHR = gen8_DestroyDescriptorUpdateTemplate,
    .vkUpdateDescriptorSetWithTemplate = gen8_UpdateDescriptorSetWithTemplate,
    .vkUpdateDescriptorSetWithTemplateKHR = gen8_UpdateDescriptorSetWithTemplate,
    .vkCmdPushDescriptorSetWithTemplateKHR = gen8_CmdPushDescriptorSetWithTemplateKHR,
    .vkGetPhysicalDeviceSurfaceCapabilities2KHR = gen8_GetPhysicalDeviceSurfaceCapabilities2KHR,
    .vkGetPhysicalDeviceSurfaceFormats2KHR = gen8_GetPhysicalDeviceSurfaceFormats2KHR,
    .vkGetBufferMemoryRequirements2 = gen8_GetBufferMemoryRequirements2,
    .vkGetBufferMemoryRequirements2KHR = gen8_GetBufferMemoryRequirements2,
    .vkGetImageMemoryRequirements2 = gen8_GetImageMemoryRequirements2,
    .vkGetImageMemoryRequirements2KHR = gen8_GetImageMemoryRequirements2,
    .vkGetImageSparseMemoryRequirements2 = gen8_GetImageSparseMemoryRequirements2,
    .vkGetImageSparseMemoryRequirements2KHR = gen8_GetImageSparseMemoryRequirements2,
    .vkCreateSamplerYcbcrConversion = gen8_CreateSamplerYcbcrConversion,
    .vkCreateSamplerYcbcrConversionKHR = gen8_CreateSamplerYcbcrConversion,
    .vkDestroySamplerYcbcrConversion = gen8_DestroySamplerYcbcrConversion,
    .vkDestroySamplerYcbcrConversionKHR = gen8_DestroySamplerYcbcrConversion,
    .vkGetDeviceQueue2 = gen8_GetDeviceQueue2,
    .vkGetDescriptorSetLayoutSupport = gen8_GetDescriptorSetLayoutSupport,
    .vkGetDescriptorSetLayoutSupportKHR = gen8_GetDescriptorSetLayoutSupport,
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
    VkResult gen9_EnumerateInstanceVersion(uint32_t* pApiVersion) __attribute__ ((weak));
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
    void gen9_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures) __attribute__ ((weak));
          void gen9_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties) __attribute__ ((weak));
          void gen9_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2* pFormatProperties) __attribute__ ((weak));
          VkResult gen9_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo, VkImageFormatProperties2* pImageFormatProperties) __attribute__ ((weak));
          void gen9_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2* pQueueFamilyProperties) __attribute__ ((weak));
          void gen9_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties) __attribute__ ((weak));
          void gen9_GetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo, uint32_t* pPropertyCount, VkSparseImageFormatProperties2* pProperties) __attribute__ ((weak));
          void gen9_CmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites) __attribute__ ((weak));
    void gen9_TrimCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlags flags) __attribute__ ((weak));
          void gen9_GetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo* pExternalBufferInfo, VkExternalBufferProperties* pExternalBufferProperties) __attribute__ ((weak));
          VkResult gen9_GetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen9_GetMemoryFdPropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, int fd, VkMemoryFdPropertiesKHR* pMemoryFdProperties) __attribute__ ((weak));
    void gen9_GetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo, VkExternalSemaphoreProperties* pExternalSemaphoreProperties) __attribute__ ((weak));
          VkResult gen9_GetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen9_ImportSemaphoreFdKHR(VkDevice device, const VkImportSemaphoreFdInfoKHR* pImportSemaphoreFdInfo) __attribute__ ((weak));
    void gen9_GetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo, VkExternalFenceProperties* pExternalFenceProperties) __attribute__ ((weak));
          VkResult gen9_GetFenceFdKHR(VkDevice device, const VkFenceGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen9_ImportFenceFdKHR(VkDevice device, const VkImportFenceFdInfoKHR* pImportFenceFdInfo) __attribute__ ((weak));
    VkResult gen9_EnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t* pPhysicalDeviceGroupCount, VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties) __attribute__ ((weak));
          void gen9_GetDeviceGroupPeerMemoryFeatures(VkDevice device, uint32_t heapIndex, uint32_t localDeviceIndex, uint32_t remoteDeviceIndex, VkPeerMemoryFeatureFlags* pPeerMemoryFeatures) __attribute__ ((weak));
          VkResult gen9_BindBufferMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos) __attribute__ ((weak));
          VkResult gen9_BindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos) __attribute__ ((weak));
          void gen9_CmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask) __attribute__ ((weak));
          VkResult gen9_GetDeviceGroupPresentCapabilitiesKHR(VkDevice device, VkDeviceGroupPresentCapabilitiesKHR* pDeviceGroupPresentCapabilities) __attribute__ ((weak));
    VkResult gen9_GetDeviceGroupSurfacePresentModesKHR(VkDevice device, VkSurfaceKHR surface, VkDeviceGroupPresentModeFlagsKHR* pModes) __attribute__ ((weak));
    VkResult gen9_AcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex) __attribute__ ((weak));
    void gen9_CmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) __attribute__ ((weak));
          VkResult gen9_GetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pRectCount, VkRect2D* pRects) __attribute__ ((weak));
    VkResult gen9_CreateDescriptorUpdateTemplate(VkDevice device, const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate) __attribute__ ((weak));
          void gen9_DestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
          void gen9_UpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void* pData) __attribute__ ((weak));
          void gen9_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplate descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) __attribute__ ((weak));
    VkResult gen9_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen9_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats) __attribute__ ((weak));
    void gen9_GetBufferMemoryRequirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) __attribute__ ((weak));
          void gen9_GetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) __attribute__ ((weak));
          void gen9_GetImageSparseMemoryRequirements2(VkDevice device, const VkImageSparseMemoryRequirementsInfo2* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2* pSparseMemoryRequirements) __attribute__ ((weak));
          VkResult gen9_CreateSamplerYcbcrConversion(VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion) __attribute__ ((weak));
          void gen9_DestroySamplerYcbcrConversion(VkDevice device, VkSamplerYcbcrConversion ycbcrConversion, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
          void gen9_GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* pQueueInfo, VkQueue* pQueue) __attribute__ ((weak));
    void gen9_GetDescriptorSetLayoutSupport(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, VkDescriptorSetLayoutSupport* pSupport) __attribute__ ((weak));
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
    .vkEnumerateInstanceVersion = gen9_EnumerateInstanceVersion,
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
    .vkGetPhysicalDeviceFeatures2 = gen9_GetPhysicalDeviceFeatures2,
    .vkGetPhysicalDeviceFeatures2KHR = gen9_GetPhysicalDeviceFeatures2,
    .vkGetPhysicalDeviceProperties2 = gen9_GetPhysicalDeviceProperties2,
    .vkGetPhysicalDeviceProperties2KHR = gen9_GetPhysicalDeviceProperties2,
    .vkGetPhysicalDeviceFormatProperties2 = gen9_GetPhysicalDeviceFormatProperties2,
    .vkGetPhysicalDeviceFormatProperties2KHR = gen9_GetPhysicalDeviceFormatProperties2,
    .vkGetPhysicalDeviceImageFormatProperties2 = gen9_GetPhysicalDeviceImageFormatProperties2,
    .vkGetPhysicalDeviceImageFormatProperties2KHR = gen9_GetPhysicalDeviceImageFormatProperties2,
    .vkGetPhysicalDeviceQueueFamilyProperties2 = gen9_GetPhysicalDeviceQueueFamilyProperties2,
    .vkGetPhysicalDeviceQueueFamilyProperties2KHR = gen9_GetPhysicalDeviceQueueFamilyProperties2,
    .vkGetPhysicalDeviceMemoryProperties2 = gen9_GetPhysicalDeviceMemoryProperties2,
    .vkGetPhysicalDeviceMemoryProperties2KHR = gen9_GetPhysicalDeviceMemoryProperties2,
    .vkGetPhysicalDeviceSparseImageFormatProperties2 = gen9_GetPhysicalDeviceSparseImageFormatProperties2,
    .vkGetPhysicalDeviceSparseImageFormatProperties2KHR = gen9_GetPhysicalDeviceSparseImageFormatProperties2,
    .vkCmdPushDescriptorSetKHR = gen9_CmdPushDescriptorSetKHR,
    .vkTrimCommandPool = gen9_TrimCommandPool,
    .vkTrimCommandPoolKHR = gen9_TrimCommandPool,
    .vkGetPhysicalDeviceExternalBufferProperties = gen9_GetPhysicalDeviceExternalBufferProperties,
    .vkGetPhysicalDeviceExternalBufferPropertiesKHR = gen9_GetPhysicalDeviceExternalBufferProperties,
    .vkGetMemoryFdKHR = gen9_GetMemoryFdKHR,
    .vkGetMemoryFdPropertiesKHR = gen9_GetMemoryFdPropertiesKHR,
    .vkGetPhysicalDeviceExternalSemaphoreProperties = gen9_GetPhysicalDeviceExternalSemaphoreProperties,
    .vkGetPhysicalDeviceExternalSemaphorePropertiesKHR = gen9_GetPhysicalDeviceExternalSemaphoreProperties,
    .vkGetSemaphoreFdKHR = gen9_GetSemaphoreFdKHR,
    .vkImportSemaphoreFdKHR = gen9_ImportSemaphoreFdKHR,
    .vkGetPhysicalDeviceExternalFenceProperties = gen9_GetPhysicalDeviceExternalFenceProperties,
    .vkGetPhysicalDeviceExternalFencePropertiesKHR = gen9_GetPhysicalDeviceExternalFenceProperties,
    .vkGetFenceFdKHR = gen9_GetFenceFdKHR,
    .vkImportFenceFdKHR = gen9_ImportFenceFdKHR,
    .vkEnumeratePhysicalDeviceGroups = gen9_EnumeratePhysicalDeviceGroups,
    .vkEnumeratePhysicalDeviceGroupsKHR = gen9_EnumeratePhysicalDeviceGroups,
    .vkGetDeviceGroupPeerMemoryFeatures = gen9_GetDeviceGroupPeerMemoryFeatures,
    .vkGetDeviceGroupPeerMemoryFeaturesKHR = gen9_GetDeviceGroupPeerMemoryFeatures,
    .vkBindBufferMemory2 = gen9_BindBufferMemory2,
    .vkBindBufferMemory2KHR = gen9_BindBufferMemory2,
    .vkBindImageMemory2 = gen9_BindImageMemory2,
    .vkBindImageMemory2KHR = gen9_BindImageMemory2,
    .vkCmdSetDeviceMask = gen9_CmdSetDeviceMask,
    .vkCmdSetDeviceMaskKHR = gen9_CmdSetDeviceMask,
    .vkGetDeviceGroupPresentCapabilitiesKHR = gen9_GetDeviceGroupPresentCapabilitiesKHR,
    .vkGetDeviceGroupSurfacePresentModesKHR = gen9_GetDeviceGroupSurfacePresentModesKHR,
    .vkAcquireNextImage2KHR = gen9_AcquireNextImage2KHR,
    .vkCmdDispatchBase = gen9_CmdDispatchBase,
    .vkCmdDispatchBaseKHR = gen9_CmdDispatchBase,
    .vkGetPhysicalDevicePresentRectanglesKHR = gen9_GetPhysicalDevicePresentRectanglesKHR,
    .vkCreateDescriptorUpdateTemplate = gen9_CreateDescriptorUpdateTemplate,
    .vkCreateDescriptorUpdateTemplateKHR = gen9_CreateDescriptorUpdateTemplate,
    .vkDestroyDescriptorUpdateTemplate = gen9_DestroyDescriptorUpdateTemplate,
    .vkDestroyDescriptorUpdateTemplateKHR = gen9_DestroyDescriptorUpdateTemplate,
    .vkUpdateDescriptorSetWithTemplate = gen9_UpdateDescriptorSetWithTemplate,
    .vkUpdateDescriptorSetWithTemplateKHR = gen9_UpdateDescriptorSetWithTemplate,
    .vkCmdPushDescriptorSetWithTemplateKHR = gen9_CmdPushDescriptorSetWithTemplateKHR,
    .vkGetPhysicalDeviceSurfaceCapabilities2KHR = gen9_GetPhysicalDeviceSurfaceCapabilities2KHR,
    .vkGetPhysicalDeviceSurfaceFormats2KHR = gen9_GetPhysicalDeviceSurfaceFormats2KHR,
    .vkGetBufferMemoryRequirements2 = gen9_GetBufferMemoryRequirements2,
    .vkGetBufferMemoryRequirements2KHR = gen9_GetBufferMemoryRequirements2,
    .vkGetImageMemoryRequirements2 = gen9_GetImageMemoryRequirements2,
    .vkGetImageMemoryRequirements2KHR = gen9_GetImageMemoryRequirements2,
    .vkGetImageSparseMemoryRequirements2 = gen9_GetImageSparseMemoryRequirements2,
    .vkGetImageSparseMemoryRequirements2KHR = gen9_GetImageSparseMemoryRequirements2,
    .vkCreateSamplerYcbcrConversion = gen9_CreateSamplerYcbcrConversion,
    .vkCreateSamplerYcbcrConversionKHR = gen9_CreateSamplerYcbcrConversion,
    .vkDestroySamplerYcbcrConversion = gen9_DestroySamplerYcbcrConversion,
    .vkDestroySamplerYcbcrConversionKHR = gen9_DestroySamplerYcbcrConversion,
    .vkGetDeviceQueue2 = gen9_GetDeviceQueue2,
    .vkGetDescriptorSetLayoutSupport = gen9_GetDescriptorSetLayoutSupport,
    .vkGetDescriptorSetLayoutSupportKHR = gen9_GetDescriptorSetLayoutSupport,
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
    VkResult gen10_EnumerateInstanceVersion(uint32_t* pApiVersion) __attribute__ ((weak));
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
    void gen10_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures) __attribute__ ((weak));
          void gen10_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties) __attribute__ ((weak));
          void gen10_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2* pFormatProperties) __attribute__ ((weak));
          VkResult gen10_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo, VkImageFormatProperties2* pImageFormatProperties) __attribute__ ((weak));
          void gen10_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2* pQueueFamilyProperties) __attribute__ ((weak));
          void gen10_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties) __attribute__ ((weak));
          void gen10_GetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo, uint32_t* pPropertyCount, VkSparseImageFormatProperties2* pProperties) __attribute__ ((weak));
          void gen10_CmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites) __attribute__ ((weak));
    void gen10_TrimCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlags flags) __attribute__ ((weak));
          void gen10_GetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo* pExternalBufferInfo, VkExternalBufferProperties* pExternalBufferProperties) __attribute__ ((weak));
          VkResult gen10_GetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen10_GetMemoryFdPropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, int fd, VkMemoryFdPropertiesKHR* pMemoryFdProperties) __attribute__ ((weak));
    void gen10_GetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo, VkExternalSemaphoreProperties* pExternalSemaphoreProperties) __attribute__ ((weak));
          VkResult gen10_GetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen10_ImportSemaphoreFdKHR(VkDevice device, const VkImportSemaphoreFdInfoKHR* pImportSemaphoreFdInfo) __attribute__ ((weak));
    void gen10_GetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo, VkExternalFenceProperties* pExternalFenceProperties) __attribute__ ((weak));
          VkResult gen10_GetFenceFdKHR(VkDevice device, const VkFenceGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen10_ImportFenceFdKHR(VkDevice device, const VkImportFenceFdInfoKHR* pImportFenceFdInfo) __attribute__ ((weak));
    VkResult gen10_EnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t* pPhysicalDeviceGroupCount, VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties) __attribute__ ((weak));
          void gen10_GetDeviceGroupPeerMemoryFeatures(VkDevice device, uint32_t heapIndex, uint32_t localDeviceIndex, uint32_t remoteDeviceIndex, VkPeerMemoryFeatureFlags* pPeerMemoryFeatures) __attribute__ ((weak));
          VkResult gen10_BindBufferMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos) __attribute__ ((weak));
          VkResult gen10_BindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos) __attribute__ ((weak));
          void gen10_CmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask) __attribute__ ((weak));
          VkResult gen10_GetDeviceGroupPresentCapabilitiesKHR(VkDevice device, VkDeviceGroupPresentCapabilitiesKHR* pDeviceGroupPresentCapabilities) __attribute__ ((weak));
    VkResult gen10_GetDeviceGroupSurfacePresentModesKHR(VkDevice device, VkSurfaceKHR surface, VkDeviceGroupPresentModeFlagsKHR* pModes) __attribute__ ((weak));
    VkResult gen10_AcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex) __attribute__ ((weak));
    void gen10_CmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) __attribute__ ((weak));
          VkResult gen10_GetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pRectCount, VkRect2D* pRects) __attribute__ ((weak));
    VkResult gen10_CreateDescriptorUpdateTemplate(VkDevice device, const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate) __attribute__ ((weak));
          void gen10_DestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
          void gen10_UpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void* pData) __attribute__ ((weak));
          void gen10_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplate descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) __attribute__ ((weak));
    VkResult gen10_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen10_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats) __attribute__ ((weak));
    void gen10_GetBufferMemoryRequirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) __attribute__ ((weak));
          void gen10_GetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) __attribute__ ((weak));
          void gen10_GetImageSparseMemoryRequirements2(VkDevice device, const VkImageSparseMemoryRequirementsInfo2* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2* pSparseMemoryRequirements) __attribute__ ((weak));
          VkResult gen10_CreateSamplerYcbcrConversion(VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion) __attribute__ ((weak));
          void gen10_DestroySamplerYcbcrConversion(VkDevice device, VkSamplerYcbcrConversion ycbcrConversion, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
          void gen10_GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* pQueueInfo, VkQueue* pQueue) __attribute__ ((weak));
    void gen10_GetDescriptorSetLayoutSupport(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, VkDescriptorSetLayoutSupport* pSupport) __attribute__ ((weak));
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
    .vkEnumerateInstanceVersion = gen10_EnumerateInstanceVersion,
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
    .vkGetPhysicalDeviceFeatures2 = gen10_GetPhysicalDeviceFeatures2,
    .vkGetPhysicalDeviceFeatures2KHR = gen10_GetPhysicalDeviceFeatures2,
    .vkGetPhysicalDeviceProperties2 = gen10_GetPhysicalDeviceProperties2,
    .vkGetPhysicalDeviceProperties2KHR = gen10_GetPhysicalDeviceProperties2,
    .vkGetPhysicalDeviceFormatProperties2 = gen10_GetPhysicalDeviceFormatProperties2,
    .vkGetPhysicalDeviceFormatProperties2KHR = gen10_GetPhysicalDeviceFormatProperties2,
    .vkGetPhysicalDeviceImageFormatProperties2 = gen10_GetPhysicalDeviceImageFormatProperties2,
    .vkGetPhysicalDeviceImageFormatProperties2KHR = gen10_GetPhysicalDeviceImageFormatProperties2,
    .vkGetPhysicalDeviceQueueFamilyProperties2 = gen10_GetPhysicalDeviceQueueFamilyProperties2,
    .vkGetPhysicalDeviceQueueFamilyProperties2KHR = gen10_GetPhysicalDeviceQueueFamilyProperties2,
    .vkGetPhysicalDeviceMemoryProperties2 = gen10_GetPhysicalDeviceMemoryProperties2,
    .vkGetPhysicalDeviceMemoryProperties2KHR = gen10_GetPhysicalDeviceMemoryProperties2,
    .vkGetPhysicalDeviceSparseImageFormatProperties2 = gen10_GetPhysicalDeviceSparseImageFormatProperties2,
    .vkGetPhysicalDeviceSparseImageFormatProperties2KHR = gen10_GetPhysicalDeviceSparseImageFormatProperties2,
    .vkCmdPushDescriptorSetKHR = gen10_CmdPushDescriptorSetKHR,
    .vkTrimCommandPool = gen10_TrimCommandPool,
    .vkTrimCommandPoolKHR = gen10_TrimCommandPool,
    .vkGetPhysicalDeviceExternalBufferProperties = gen10_GetPhysicalDeviceExternalBufferProperties,
    .vkGetPhysicalDeviceExternalBufferPropertiesKHR = gen10_GetPhysicalDeviceExternalBufferProperties,
    .vkGetMemoryFdKHR = gen10_GetMemoryFdKHR,
    .vkGetMemoryFdPropertiesKHR = gen10_GetMemoryFdPropertiesKHR,
    .vkGetPhysicalDeviceExternalSemaphoreProperties = gen10_GetPhysicalDeviceExternalSemaphoreProperties,
    .vkGetPhysicalDeviceExternalSemaphorePropertiesKHR = gen10_GetPhysicalDeviceExternalSemaphoreProperties,
    .vkGetSemaphoreFdKHR = gen10_GetSemaphoreFdKHR,
    .vkImportSemaphoreFdKHR = gen10_ImportSemaphoreFdKHR,
    .vkGetPhysicalDeviceExternalFenceProperties = gen10_GetPhysicalDeviceExternalFenceProperties,
    .vkGetPhysicalDeviceExternalFencePropertiesKHR = gen10_GetPhysicalDeviceExternalFenceProperties,
    .vkGetFenceFdKHR = gen10_GetFenceFdKHR,
    .vkImportFenceFdKHR = gen10_ImportFenceFdKHR,
    .vkEnumeratePhysicalDeviceGroups = gen10_EnumeratePhysicalDeviceGroups,
    .vkEnumeratePhysicalDeviceGroupsKHR = gen10_EnumeratePhysicalDeviceGroups,
    .vkGetDeviceGroupPeerMemoryFeatures = gen10_GetDeviceGroupPeerMemoryFeatures,
    .vkGetDeviceGroupPeerMemoryFeaturesKHR = gen10_GetDeviceGroupPeerMemoryFeatures,
    .vkBindBufferMemory2 = gen10_BindBufferMemory2,
    .vkBindBufferMemory2KHR = gen10_BindBufferMemory2,
    .vkBindImageMemory2 = gen10_BindImageMemory2,
    .vkBindImageMemory2KHR = gen10_BindImageMemory2,
    .vkCmdSetDeviceMask = gen10_CmdSetDeviceMask,
    .vkCmdSetDeviceMaskKHR = gen10_CmdSetDeviceMask,
    .vkGetDeviceGroupPresentCapabilitiesKHR = gen10_GetDeviceGroupPresentCapabilitiesKHR,
    .vkGetDeviceGroupSurfacePresentModesKHR = gen10_GetDeviceGroupSurfacePresentModesKHR,
    .vkAcquireNextImage2KHR = gen10_AcquireNextImage2KHR,
    .vkCmdDispatchBase = gen10_CmdDispatchBase,
    .vkCmdDispatchBaseKHR = gen10_CmdDispatchBase,
    .vkGetPhysicalDevicePresentRectanglesKHR = gen10_GetPhysicalDevicePresentRectanglesKHR,
    .vkCreateDescriptorUpdateTemplate = gen10_CreateDescriptorUpdateTemplate,
    .vkCreateDescriptorUpdateTemplateKHR = gen10_CreateDescriptorUpdateTemplate,
    .vkDestroyDescriptorUpdateTemplate = gen10_DestroyDescriptorUpdateTemplate,
    .vkDestroyDescriptorUpdateTemplateKHR = gen10_DestroyDescriptorUpdateTemplate,
    .vkUpdateDescriptorSetWithTemplate = gen10_UpdateDescriptorSetWithTemplate,
    .vkUpdateDescriptorSetWithTemplateKHR = gen10_UpdateDescriptorSetWithTemplate,
    .vkCmdPushDescriptorSetWithTemplateKHR = gen10_CmdPushDescriptorSetWithTemplateKHR,
    .vkGetPhysicalDeviceSurfaceCapabilities2KHR = gen10_GetPhysicalDeviceSurfaceCapabilities2KHR,
    .vkGetPhysicalDeviceSurfaceFormats2KHR = gen10_GetPhysicalDeviceSurfaceFormats2KHR,
    .vkGetBufferMemoryRequirements2 = gen10_GetBufferMemoryRequirements2,
    .vkGetBufferMemoryRequirements2KHR = gen10_GetBufferMemoryRequirements2,
    .vkGetImageMemoryRequirements2 = gen10_GetImageMemoryRequirements2,
    .vkGetImageMemoryRequirements2KHR = gen10_GetImageMemoryRequirements2,
    .vkGetImageSparseMemoryRequirements2 = gen10_GetImageSparseMemoryRequirements2,
    .vkGetImageSparseMemoryRequirements2KHR = gen10_GetImageSparseMemoryRequirements2,
    .vkCreateSamplerYcbcrConversion = gen10_CreateSamplerYcbcrConversion,
    .vkCreateSamplerYcbcrConversionKHR = gen10_CreateSamplerYcbcrConversion,
    .vkDestroySamplerYcbcrConversion = gen10_DestroySamplerYcbcrConversion,
    .vkDestroySamplerYcbcrConversionKHR = gen10_DestroySamplerYcbcrConversion,
    .vkGetDeviceQueue2 = gen10_GetDeviceQueue2,
    .vkGetDescriptorSetLayoutSupport = gen10_GetDescriptorSetLayoutSupport,
    .vkGetDescriptorSetLayoutSupportKHR = gen10_GetDescriptorSetLayoutSupport,
    .vkCreateDmaBufImageINTEL = gen10_CreateDmaBufImageINTEL,
  };
    VkResult gen11_CreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) __attribute__ ((weak));
    void gen11_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen11_EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) __attribute__ ((weak));
    PFN_vkVoidFunction gen11_GetDeviceProcAddr(VkDevice device, const char* pName) __attribute__ ((weak));
    PFN_vkVoidFunction gen11_GetInstanceProcAddr(VkInstance instance, const char* pName) __attribute__ ((weak));
    void gen11_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) __attribute__ ((weak));
    void gen11_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties) __attribute__ ((weak));
    void gen11_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties) __attribute__ ((weak));
    void gen11_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures) __attribute__ ((weak));
    void gen11_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties) __attribute__ ((weak));
    VkResult gen11_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* pImageFormatProperties) __attribute__ ((weak));
    VkResult gen11_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) __attribute__ ((weak));
    void gen11_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen11_EnumerateInstanceVersion(uint32_t* pApiVersion) __attribute__ ((weak));
    VkResult gen11_EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
    VkResult gen11_EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
    VkResult gen11_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) __attribute__ ((weak));
    VkResult gen11_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) __attribute__ ((weak));
    void gen11_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue) __attribute__ ((weak));
    VkResult gen11_QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence) __attribute__ ((weak));
    VkResult gen11_QueueWaitIdle(VkQueue queue) __attribute__ ((weak));
    VkResult gen11_DeviceWaitIdle(VkDevice device) __attribute__ ((weak));
    VkResult gen11_AllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory) __attribute__ ((weak));
    void gen11_FreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen11_MapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData) __attribute__ ((weak));
    void gen11_UnmapMemory(VkDevice device, VkDeviceMemory memory) __attribute__ ((weak));
    VkResult gen11_FlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) __attribute__ ((weak));
    VkResult gen11_InvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount, const VkMappedMemoryRange* pMemoryRanges) __attribute__ ((weak));
    void gen11_GetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory, VkDeviceSize* pCommittedMemoryInBytes) __attribute__ ((weak));
    void gen11_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
    VkResult gen11_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
    void gen11_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements) __attribute__ ((weak));
    VkResult gen11_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) __attribute__ ((weak));
    void gen11_GetImageSparseMemoryRequirements(VkDevice device, VkImage image, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements* pSparseMemoryRequirements) __attribute__ ((weak));
    void gen11_GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling, uint32_t* pPropertyCount, VkSparseImageFormatProperties* pProperties) __attribute__ ((weak));
    VkResult gen11_QueueBindSparse(VkQueue queue, uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence) __attribute__ ((weak));
    VkResult gen11_CreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFence* pFence) __attribute__ ((weak));
    void gen11_DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen11_ResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences) __attribute__ ((weak));
    VkResult gen11_GetFenceStatus(VkDevice device, VkFence fence) __attribute__ ((weak));
    VkResult gen11_WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout) __attribute__ ((weak));
    VkResult gen11_CreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore) __attribute__ ((weak));
    void gen11_DestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen11_CreateEvent(VkDevice device, const VkEventCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkEvent* pEvent) __attribute__ ((weak));
    void gen11_DestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen11_GetEventStatus(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult gen11_SetEvent(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult gen11_ResetEvent(VkDevice device, VkEvent event) __attribute__ ((weak));
    VkResult gen11_CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool) __attribute__ ((weak));
    void gen11_DestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen11_GetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* pData, VkDeviceSize stride, VkQueryResultFlags flags) __attribute__ ((weak));
    VkResult gen11_CreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer) __attribute__ ((weak));
    void gen11_DestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen11_CreateBufferView(VkDevice device, const VkBufferViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBufferView* pView) __attribute__ ((weak));
    void gen11_DestroyBufferView(VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen11_CreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage) __attribute__ ((weak));
    void gen11_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen11_GetImageSubresourceLayout(VkDevice device, VkImage image, const VkImageSubresource* pSubresource, VkSubresourceLayout* pLayout) __attribute__ ((weak));
    VkResult gen11_CreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView) __attribute__ ((weak));
    void gen11_DestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen11_CreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule) __attribute__ ((weak));
    void gen11_DestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen11_CreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineCache* pPipelineCache) __attribute__ ((weak));
    void gen11_DestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen11_GetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, size_t* pDataSize, void* pData) __attribute__ ((weak));
    VkResult gen11_MergePipelineCaches(VkDevice device, VkPipelineCache dstCache, uint32_t srcCacheCount, const VkPipelineCache* pSrcCaches) __attribute__ ((weak));
    VkResult gen11_CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) __attribute__ ((weak));
    VkResult gen11_CreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) __attribute__ ((weak));
    void gen11_DestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen11_CreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout) __attribute__ ((weak));
    void gen11_DestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen11_CreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSampler* pSampler) __attribute__ ((weak));
    void gen11_DestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen11_CreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout) __attribute__ ((weak));
    void gen11_DestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen11_CreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool) __attribute__ ((weak));
    void gen11_DestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen11_ResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags) __attribute__ ((weak));
    VkResult gen11_AllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets) __attribute__ ((weak));
    VkResult gen11_FreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets) __attribute__ ((weak));
    void gen11_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies) __attribute__ ((weak));
    VkResult gen11_CreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFramebuffer* pFramebuffer) __attribute__ ((weak));
    void gen11_DestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen11_CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass) __attribute__ ((weak));
    void gen11_DestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen11_GetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass, VkExtent2D* pGranularity) __attribute__ ((weak));
    VkResult gen11_CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool) __attribute__ ((weak));
    void gen11_DestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen11_ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags) __attribute__ ((weak));
    VkResult gen11_AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    void gen11_FreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    VkResult gen11_BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo) __attribute__ ((weak));
    VkResult gen11_EndCommandBuffer(VkCommandBuffer commandBuffer) __attribute__ ((weak));
    VkResult gen11_ResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags) __attribute__ ((weak));
    void gen11_CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) __attribute__ ((weak));
    void gen11_CmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports) __attribute__ ((weak));
    void gen11_CmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors) __attribute__ ((weak));
    void gen11_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth) __attribute__ ((weak));
    void gen11_CmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor, float depthBiasClamp, float depthBiasSlopeFactor) __attribute__ ((weak));
    void gen11_CmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4]) __attribute__ ((weak));
    void gen11_CmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds) __attribute__ ((weak));
    void gen11_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t compareMask) __attribute__ ((weak));
    void gen11_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t writeMask) __attribute__ ((weak));
    void gen11_CmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask, uint32_t reference) __attribute__ ((weak));
    void gen11_CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets) __attribute__ ((weak));
    void gen11_CmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) __attribute__ ((weak));
    void gen11_CmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets) __attribute__ ((weak));
    void gen11_CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) __attribute__ ((weak));
    void gen11_CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) __attribute__ ((weak));
    void gen11_CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) __attribute__ ((weak));
    void gen11_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) __attribute__ ((weak));
    void gen11_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) __attribute__ ((weak));
    void gen11_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset) __attribute__ ((weak));
    void gen11_CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions) __attribute__ ((weak));
    void gen11_CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions) __attribute__ ((weak));
    void gen11_CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter) __attribute__ ((weak));
    void gen11_CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
    void gen11_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions) __attribute__ ((weak));
    void gen11_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize, const void* pData) __attribute__ ((weak));
    void gen11_CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data) __attribute__ ((weak));
    void gen11_CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearColorValue* pColor, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) __attribute__ ((weak));
    void gen11_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout, const VkClearDepthStencilValue* pDepthStencil, uint32_t rangeCount, const VkImageSubresourceRange* pRanges) __attribute__ ((weak));
    void gen11_CmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const VkClearAttachment* pAttachments, uint32_t rectCount, const VkClearRect* pRects) __attribute__ ((weak));
    void gen11_CmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageResolve* pRegions) __attribute__ ((weak));
    void gen11_CmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) __attribute__ ((weak));
    void gen11_CmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) __attribute__ ((weak));
    void gen11_CmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) __attribute__ ((weak));
    void gen11_CmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) __attribute__ ((weak));
    void gen11_CmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags) __attribute__ ((weak));
    void gen11_CmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query) __attribute__ ((weak));
    void gen11_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount) __attribute__ ((weak));
    void gen11_CmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query) __attribute__ ((weak));
    void gen11_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags) __attribute__ ((weak));
    void gen11_CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues) __attribute__ ((weak));
    void gen11_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents) __attribute__ ((weak));
    void gen11_CmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents) __attribute__ ((weak));
    void gen11_CmdEndRenderPass(VkCommandBuffer commandBuffer) __attribute__ ((weak));
    void gen11_CmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) __attribute__ ((weak));
    void gen11_DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen11_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32* pSupported) __attribute__ ((weak));
    VkResult gen11_GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen11_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats) __attribute__ ((weak));
    VkResult gen11_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes) __attribute__ ((weak));
    VkResult gen11_CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) __attribute__ ((weak));
    void gen11_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    VkResult gen11_GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages) __attribute__ ((weak));
    VkResult gen11_AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex) __attribute__ ((weak));
    VkResult gen11_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) __attribute__ ((weak));
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    VkResult gen11_CreateWaylandSurfaceKHR(VkInstance instance, const VkWaylandSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    VkBool32 gen11_GetPhysicalDeviceWaylandPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, struct wl_display* display) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    VkResult gen11_CreateXlibSurfaceKHR(VkInstance instance, const VkXlibSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    VkBool32 gen11_GetPhysicalDeviceXlibPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, Display* dpy, VisualID visualID) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    VkResult gen11_CreateXcbSurfaceKHR(VkInstance instance, const VkXcbSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    VkBool32 gen11_GetPhysicalDeviceXcbPresentationSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, xcb_connection_t* connection, xcb_visualid_t visual_id) __attribute__ ((weak));
#endif // VK_USE_PLATFORM_XCB_KHR
    VkResult gen11_CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback) __attribute__ ((weak));
    void gen11_DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
    void gen11_DebugReportMessageEXT(VkInstance instance, VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage) __attribute__ ((weak));
    void gen11_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures) __attribute__ ((weak));
          void gen11_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties) __attribute__ ((weak));
          void gen11_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties2* pFormatProperties) __attribute__ ((weak));
          VkResult gen11_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo, VkImageFormatProperties2* pImageFormatProperties) __attribute__ ((weak));
          void gen11_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties2* pQueueFamilyProperties) __attribute__ ((weak));
          void gen11_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2* pMemoryProperties) __attribute__ ((weak));
          void gen11_GetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo, uint32_t* pPropertyCount, VkSparseImageFormatProperties2* pProperties) __attribute__ ((weak));
          void gen11_CmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites) __attribute__ ((weak));
    void gen11_TrimCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlags flags) __attribute__ ((weak));
          void gen11_GetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo* pExternalBufferInfo, VkExternalBufferProperties* pExternalBufferProperties) __attribute__ ((weak));
          VkResult gen11_GetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen11_GetMemoryFdPropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, int fd, VkMemoryFdPropertiesKHR* pMemoryFdProperties) __attribute__ ((weak));
    void gen11_GetPhysicalDeviceExternalSemaphoreProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfo* pExternalSemaphoreInfo, VkExternalSemaphoreProperties* pExternalSemaphoreProperties) __attribute__ ((weak));
          VkResult gen11_GetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen11_ImportSemaphoreFdKHR(VkDevice device, const VkImportSemaphoreFdInfoKHR* pImportSemaphoreFdInfo) __attribute__ ((weak));
    void gen11_GetPhysicalDeviceExternalFenceProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfo* pExternalFenceInfo, VkExternalFenceProperties* pExternalFenceProperties) __attribute__ ((weak));
          VkResult gen11_GetFenceFdKHR(VkDevice device, const VkFenceGetFdInfoKHR* pGetFdInfo, int* pFd) __attribute__ ((weak));
    VkResult gen11_ImportFenceFdKHR(VkDevice device, const VkImportFenceFdInfoKHR* pImportFenceFdInfo) __attribute__ ((weak));
    VkResult gen11_EnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t* pPhysicalDeviceGroupCount, VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties) __attribute__ ((weak));
          void gen11_GetDeviceGroupPeerMemoryFeatures(VkDevice device, uint32_t heapIndex, uint32_t localDeviceIndex, uint32_t remoteDeviceIndex, VkPeerMemoryFeatureFlags* pPeerMemoryFeatures) __attribute__ ((weak));
          VkResult gen11_BindBufferMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos) __attribute__ ((weak));
          VkResult gen11_BindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos) __attribute__ ((weak));
          void gen11_CmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask) __attribute__ ((weak));
          VkResult gen11_GetDeviceGroupPresentCapabilitiesKHR(VkDevice device, VkDeviceGroupPresentCapabilitiesKHR* pDeviceGroupPresentCapabilities) __attribute__ ((weak));
    VkResult gen11_GetDeviceGroupSurfacePresentModesKHR(VkDevice device, VkSurfaceKHR surface, VkDeviceGroupPresentModeFlagsKHR* pModes) __attribute__ ((weak));
    VkResult gen11_AcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex) __attribute__ ((weak));
    void gen11_CmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) __attribute__ ((weak));
          VkResult gen11_GetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pRectCount, VkRect2D* pRects) __attribute__ ((weak));
    VkResult gen11_CreateDescriptorUpdateTemplate(VkDevice device, const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate) __attribute__ ((weak));
          void gen11_DestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
          void gen11_UpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void* pData) __attribute__ ((weak));
          void gen11_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplate descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData) __attribute__ ((weak));
    VkResult gen11_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, VkSurfaceCapabilities2KHR* pSurfaceCapabilities) __attribute__ ((weak));
    VkResult gen11_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo, uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats) __attribute__ ((weak));
    void gen11_GetBufferMemoryRequirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) __attribute__ ((weak));
          void gen11_GetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) __attribute__ ((weak));
          void gen11_GetImageSparseMemoryRequirements2(VkDevice device, const VkImageSparseMemoryRequirementsInfo2* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2* pSparseMemoryRequirements) __attribute__ ((weak));
          VkResult gen11_CreateSamplerYcbcrConversion(VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion) __attribute__ ((weak));
          void gen11_DestroySamplerYcbcrConversion(VkDevice device, VkSamplerYcbcrConversion ycbcrConversion, const VkAllocationCallbacks* pAllocator) __attribute__ ((weak));
          void gen11_GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* pQueueInfo, VkQueue* pQueue) __attribute__ ((weak));
    void gen11_GetDescriptorSetLayoutSupport(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, VkDescriptorSetLayoutSupport* pSupport) __attribute__ ((weak));
          VkResult gen11_CreateDmaBufImageINTEL(VkDevice device, const VkDmaBufImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMem, VkImage* pImage) __attribute__ ((weak));

  const struct anv_dispatch_table gen11_dispatch_table = {
    .vkCreateInstance = gen11_CreateInstance,
    .vkDestroyInstance = gen11_DestroyInstance,
    .vkEnumeratePhysicalDevices = gen11_EnumeratePhysicalDevices,
    .vkGetDeviceProcAddr = gen11_GetDeviceProcAddr,
    .vkGetInstanceProcAddr = gen11_GetInstanceProcAddr,
    .vkGetPhysicalDeviceProperties = gen11_GetPhysicalDeviceProperties,
    .vkGetPhysicalDeviceQueueFamilyProperties = gen11_GetPhysicalDeviceQueueFamilyProperties,
    .vkGetPhysicalDeviceMemoryProperties = gen11_GetPhysicalDeviceMemoryProperties,
    .vkGetPhysicalDeviceFeatures = gen11_GetPhysicalDeviceFeatures,
    .vkGetPhysicalDeviceFormatProperties = gen11_GetPhysicalDeviceFormatProperties,
    .vkGetPhysicalDeviceImageFormatProperties = gen11_GetPhysicalDeviceImageFormatProperties,
    .vkCreateDevice = gen11_CreateDevice,
    .vkDestroyDevice = gen11_DestroyDevice,
    .vkEnumerateInstanceVersion = gen11_EnumerateInstanceVersion,
    .vkEnumerateInstanceLayerProperties = gen11_EnumerateInstanceLayerProperties,
    .vkEnumerateInstanceExtensionProperties = gen11_EnumerateInstanceExtensionProperties,
    .vkEnumerateDeviceLayerProperties = gen11_EnumerateDeviceLayerProperties,
    .vkEnumerateDeviceExtensionProperties = gen11_EnumerateDeviceExtensionProperties,
    .vkGetDeviceQueue = gen11_GetDeviceQueue,
    .vkQueueSubmit = gen11_QueueSubmit,
    .vkQueueWaitIdle = gen11_QueueWaitIdle,
    .vkDeviceWaitIdle = gen11_DeviceWaitIdle,
    .vkAllocateMemory = gen11_AllocateMemory,
    .vkFreeMemory = gen11_FreeMemory,
    .vkMapMemory = gen11_MapMemory,
    .vkUnmapMemory = gen11_UnmapMemory,
    .vkFlushMappedMemoryRanges = gen11_FlushMappedMemoryRanges,
    .vkInvalidateMappedMemoryRanges = gen11_InvalidateMappedMemoryRanges,
    .vkGetDeviceMemoryCommitment = gen11_GetDeviceMemoryCommitment,
    .vkGetBufferMemoryRequirements = gen11_GetBufferMemoryRequirements,
    .vkBindBufferMemory = gen11_BindBufferMemory,
    .vkGetImageMemoryRequirements = gen11_GetImageMemoryRequirements,
    .vkBindImageMemory = gen11_BindImageMemory,
    .vkGetImageSparseMemoryRequirements = gen11_GetImageSparseMemoryRequirements,
    .vkGetPhysicalDeviceSparseImageFormatProperties = gen11_GetPhysicalDeviceSparseImageFormatProperties,
    .vkQueueBindSparse = gen11_QueueBindSparse,
    .vkCreateFence = gen11_CreateFence,
    .vkDestroyFence = gen11_DestroyFence,
    .vkResetFences = gen11_ResetFences,
    .vkGetFenceStatus = gen11_GetFenceStatus,
    .vkWaitForFences = gen11_WaitForFences,
    .vkCreateSemaphore = gen11_CreateSemaphore,
    .vkDestroySemaphore = gen11_DestroySemaphore,
    .vkCreateEvent = gen11_CreateEvent,
    .vkDestroyEvent = gen11_DestroyEvent,
    .vkGetEventStatus = gen11_GetEventStatus,
    .vkSetEvent = gen11_SetEvent,
    .vkResetEvent = gen11_ResetEvent,
    .vkCreateQueryPool = gen11_CreateQueryPool,
    .vkDestroyQueryPool = gen11_DestroyQueryPool,
    .vkGetQueryPoolResults = gen11_GetQueryPoolResults,
    .vkCreateBuffer = gen11_CreateBuffer,
    .vkDestroyBuffer = gen11_DestroyBuffer,
    .vkCreateBufferView = gen11_CreateBufferView,
    .vkDestroyBufferView = gen11_DestroyBufferView,
    .vkCreateImage = gen11_CreateImage,
    .vkDestroyImage = gen11_DestroyImage,
    .vkGetImageSubresourceLayout = gen11_GetImageSubresourceLayout,
    .vkCreateImageView = gen11_CreateImageView,
    .vkDestroyImageView = gen11_DestroyImageView,
    .vkCreateShaderModule = gen11_CreateShaderModule,
    .vkDestroyShaderModule = gen11_DestroyShaderModule,
    .vkCreatePipelineCache = gen11_CreatePipelineCache,
    .vkDestroyPipelineCache = gen11_DestroyPipelineCache,
    .vkGetPipelineCacheData = gen11_GetPipelineCacheData,
    .vkMergePipelineCaches = gen11_MergePipelineCaches,
    .vkCreateGraphicsPipelines = gen11_CreateGraphicsPipelines,
    .vkCreateComputePipelines = gen11_CreateComputePipelines,
    .vkDestroyPipeline = gen11_DestroyPipeline,
    .vkCreatePipelineLayout = gen11_CreatePipelineLayout,
    .vkDestroyPipelineLayout = gen11_DestroyPipelineLayout,
    .vkCreateSampler = gen11_CreateSampler,
    .vkDestroySampler = gen11_DestroySampler,
    .vkCreateDescriptorSetLayout = gen11_CreateDescriptorSetLayout,
    .vkDestroyDescriptorSetLayout = gen11_DestroyDescriptorSetLayout,
    .vkCreateDescriptorPool = gen11_CreateDescriptorPool,
    .vkDestroyDescriptorPool = gen11_DestroyDescriptorPool,
    .vkResetDescriptorPool = gen11_ResetDescriptorPool,
    .vkAllocateDescriptorSets = gen11_AllocateDescriptorSets,
    .vkFreeDescriptorSets = gen11_FreeDescriptorSets,
    .vkUpdateDescriptorSets = gen11_UpdateDescriptorSets,
    .vkCreateFramebuffer = gen11_CreateFramebuffer,
    .vkDestroyFramebuffer = gen11_DestroyFramebuffer,
    .vkCreateRenderPass = gen11_CreateRenderPass,
    .vkDestroyRenderPass = gen11_DestroyRenderPass,
    .vkGetRenderAreaGranularity = gen11_GetRenderAreaGranularity,
    .vkCreateCommandPool = gen11_CreateCommandPool,
    .vkDestroyCommandPool = gen11_DestroyCommandPool,
    .vkResetCommandPool = gen11_ResetCommandPool,
    .vkAllocateCommandBuffers = gen11_AllocateCommandBuffers,
    .vkFreeCommandBuffers = gen11_FreeCommandBuffers,
    .vkBeginCommandBuffer = gen11_BeginCommandBuffer,
    .vkEndCommandBuffer = gen11_EndCommandBuffer,
    .vkResetCommandBuffer = gen11_ResetCommandBuffer,
    .vkCmdBindPipeline = gen11_CmdBindPipeline,
    .vkCmdSetViewport = gen11_CmdSetViewport,
    .vkCmdSetScissor = gen11_CmdSetScissor,
    .vkCmdSetLineWidth = gen11_CmdSetLineWidth,
    .vkCmdSetDepthBias = gen11_CmdSetDepthBias,
    .vkCmdSetBlendConstants = gen11_CmdSetBlendConstants,
    .vkCmdSetDepthBounds = gen11_CmdSetDepthBounds,
    .vkCmdSetStencilCompareMask = gen11_CmdSetStencilCompareMask,
    .vkCmdSetStencilWriteMask = gen11_CmdSetStencilWriteMask,
    .vkCmdSetStencilReference = gen11_CmdSetStencilReference,
    .vkCmdBindDescriptorSets = gen11_CmdBindDescriptorSets,
    .vkCmdBindIndexBuffer = gen11_CmdBindIndexBuffer,
    .vkCmdBindVertexBuffers = gen11_CmdBindVertexBuffers,
    .vkCmdDraw = gen11_CmdDraw,
    .vkCmdDrawIndexed = gen11_CmdDrawIndexed,
    .vkCmdDrawIndirect = gen11_CmdDrawIndirect,
    .vkCmdDrawIndexedIndirect = gen11_CmdDrawIndexedIndirect,
    .vkCmdDispatch = gen11_CmdDispatch,
    .vkCmdDispatchIndirect = gen11_CmdDispatchIndirect,
    .vkCmdCopyBuffer = gen11_CmdCopyBuffer,
    .vkCmdCopyImage = gen11_CmdCopyImage,
    .vkCmdBlitImage = gen11_CmdBlitImage,
    .vkCmdCopyBufferToImage = gen11_CmdCopyBufferToImage,
    .vkCmdCopyImageToBuffer = gen11_CmdCopyImageToBuffer,
    .vkCmdUpdateBuffer = gen11_CmdUpdateBuffer,
    .vkCmdFillBuffer = gen11_CmdFillBuffer,
    .vkCmdClearColorImage = gen11_CmdClearColorImage,
    .vkCmdClearDepthStencilImage = gen11_CmdClearDepthStencilImage,
    .vkCmdClearAttachments = gen11_CmdClearAttachments,
    .vkCmdResolveImage = gen11_CmdResolveImage,
    .vkCmdSetEvent = gen11_CmdSetEvent,
    .vkCmdResetEvent = gen11_CmdResetEvent,
    .vkCmdWaitEvents = gen11_CmdWaitEvents,
    .vkCmdPipelineBarrier = gen11_CmdPipelineBarrier,
    .vkCmdBeginQuery = gen11_CmdBeginQuery,
    .vkCmdEndQuery = gen11_CmdEndQuery,
    .vkCmdResetQueryPool = gen11_CmdResetQueryPool,
    .vkCmdWriteTimestamp = gen11_CmdWriteTimestamp,
    .vkCmdCopyQueryPoolResults = gen11_CmdCopyQueryPoolResults,
    .vkCmdPushConstants = gen11_CmdPushConstants,
    .vkCmdBeginRenderPass = gen11_CmdBeginRenderPass,
    .vkCmdNextSubpass = gen11_CmdNextSubpass,
    .vkCmdEndRenderPass = gen11_CmdEndRenderPass,
    .vkCmdExecuteCommands = gen11_CmdExecuteCommands,
    .vkDestroySurfaceKHR = gen11_DestroySurfaceKHR,
    .vkGetPhysicalDeviceSurfaceSupportKHR = gen11_GetPhysicalDeviceSurfaceSupportKHR,
    .vkGetPhysicalDeviceSurfaceCapabilitiesKHR = gen11_GetPhysicalDeviceSurfaceCapabilitiesKHR,
    .vkGetPhysicalDeviceSurfaceFormatsKHR = gen11_GetPhysicalDeviceSurfaceFormatsKHR,
    .vkGetPhysicalDeviceSurfacePresentModesKHR = gen11_GetPhysicalDeviceSurfacePresentModesKHR,
    .vkCreateSwapchainKHR = gen11_CreateSwapchainKHR,
    .vkDestroySwapchainKHR = gen11_DestroySwapchainKHR,
    .vkGetSwapchainImagesKHR = gen11_GetSwapchainImagesKHR,
    .vkAcquireNextImageKHR = gen11_AcquireNextImageKHR,
    .vkQueuePresentKHR = gen11_QueuePresentKHR,
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .vkCreateWaylandSurfaceKHR = gen11_CreateWaylandSurfaceKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .vkGetPhysicalDeviceWaylandPresentationSupportKHR = gen11_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .vkCreateXlibSurfaceKHR = gen11_CreateXlibSurfaceKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .vkGetPhysicalDeviceXlibPresentationSupportKHR = gen11_GetPhysicalDeviceXlibPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .vkCreateXcbSurfaceKHR = gen11_CreateXcbSurfaceKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .vkGetPhysicalDeviceXcbPresentationSupportKHR = gen11_GetPhysicalDeviceXcbPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
    .vkCreateDebugReportCallbackEXT = gen11_CreateDebugReportCallbackEXT,
    .vkDestroyDebugReportCallbackEXT = gen11_DestroyDebugReportCallbackEXT,
    .vkDebugReportMessageEXT = gen11_DebugReportMessageEXT,
    .vkGetPhysicalDeviceFeatures2 = gen11_GetPhysicalDeviceFeatures2,
    .vkGetPhysicalDeviceFeatures2KHR = gen11_GetPhysicalDeviceFeatures2,
    .vkGetPhysicalDeviceProperties2 = gen11_GetPhysicalDeviceProperties2,
    .vkGetPhysicalDeviceProperties2KHR = gen11_GetPhysicalDeviceProperties2,
    .vkGetPhysicalDeviceFormatProperties2 = gen11_GetPhysicalDeviceFormatProperties2,
    .vkGetPhysicalDeviceFormatProperties2KHR = gen11_GetPhysicalDeviceFormatProperties2,
    .vkGetPhysicalDeviceImageFormatProperties2 = gen11_GetPhysicalDeviceImageFormatProperties2,
    .vkGetPhysicalDeviceImageFormatProperties2KHR = gen11_GetPhysicalDeviceImageFormatProperties2,
    .vkGetPhysicalDeviceQueueFamilyProperties2 = gen11_GetPhysicalDeviceQueueFamilyProperties2,
    .vkGetPhysicalDeviceQueueFamilyProperties2KHR = gen11_GetPhysicalDeviceQueueFamilyProperties2,
    .vkGetPhysicalDeviceMemoryProperties2 = gen11_GetPhysicalDeviceMemoryProperties2,
    .vkGetPhysicalDeviceMemoryProperties2KHR = gen11_GetPhysicalDeviceMemoryProperties2,
    .vkGetPhysicalDeviceSparseImageFormatProperties2 = gen11_GetPhysicalDeviceSparseImageFormatProperties2,
    .vkGetPhysicalDeviceSparseImageFormatProperties2KHR = gen11_GetPhysicalDeviceSparseImageFormatProperties2,
    .vkCmdPushDescriptorSetKHR = gen11_CmdPushDescriptorSetKHR,
    .vkTrimCommandPool = gen11_TrimCommandPool,
    .vkTrimCommandPoolKHR = gen11_TrimCommandPool,
    .vkGetPhysicalDeviceExternalBufferProperties = gen11_GetPhysicalDeviceExternalBufferProperties,
    .vkGetPhysicalDeviceExternalBufferPropertiesKHR = gen11_GetPhysicalDeviceExternalBufferProperties,
    .vkGetMemoryFdKHR = gen11_GetMemoryFdKHR,
    .vkGetMemoryFdPropertiesKHR = gen11_GetMemoryFdPropertiesKHR,
    .vkGetPhysicalDeviceExternalSemaphoreProperties = gen11_GetPhysicalDeviceExternalSemaphoreProperties,
    .vkGetPhysicalDeviceExternalSemaphorePropertiesKHR = gen11_GetPhysicalDeviceExternalSemaphoreProperties,
    .vkGetSemaphoreFdKHR = gen11_GetSemaphoreFdKHR,
    .vkImportSemaphoreFdKHR = gen11_ImportSemaphoreFdKHR,
    .vkGetPhysicalDeviceExternalFenceProperties = gen11_GetPhysicalDeviceExternalFenceProperties,
    .vkGetPhysicalDeviceExternalFencePropertiesKHR = gen11_GetPhysicalDeviceExternalFenceProperties,
    .vkGetFenceFdKHR = gen11_GetFenceFdKHR,
    .vkImportFenceFdKHR = gen11_ImportFenceFdKHR,
    .vkEnumeratePhysicalDeviceGroups = gen11_EnumeratePhysicalDeviceGroups,
    .vkEnumeratePhysicalDeviceGroupsKHR = gen11_EnumeratePhysicalDeviceGroups,
    .vkGetDeviceGroupPeerMemoryFeatures = gen11_GetDeviceGroupPeerMemoryFeatures,
    .vkGetDeviceGroupPeerMemoryFeaturesKHR = gen11_GetDeviceGroupPeerMemoryFeatures,
    .vkBindBufferMemory2 = gen11_BindBufferMemory2,
    .vkBindBufferMemory2KHR = gen11_BindBufferMemory2,
    .vkBindImageMemory2 = gen11_BindImageMemory2,
    .vkBindImageMemory2KHR = gen11_BindImageMemory2,
    .vkCmdSetDeviceMask = gen11_CmdSetDeviceMask,
    .vkCmdSetDeviceMaskKHR = gen11_CmdSetDeviceMask,
    .vkGetDeviceGroupPresentCapabilitiesKHR = gen11_GetDeviceGroupPresentCapabilitiesKHR,
    .vkGetDeviceGroupSurfacePresentModesKHR = gen11_GetDeviceGroupSurfacePresentModesKHR,
    .vkAcquireNextImage2KHR = gen11_AcquireNextImage2KHR,
    .vkCmdDispatchBase = gen11_CmdDispatchBase,
    .vkCmdDispatchBaseKHR = gen11_CmdDispatchBase,
    .vkGetPhysicalDevicePresentRectanglesKHR = gen11_GetPhysicalDevicePresentRectanglesKHR,
    .vkCreateDescriptorUpdateTemplate = gen11_CreateDescriptorUpdateTemplate,
    .vkCreateDescriptorUpdateTemplateKHR = gen11_CreateDescriptorUpdateTemplate,
    .vkDestroyDescriptorUpdateTemplate = gen11_DestroyDescriptorUpdateTemplate,
    .vkDestroyDescriptorUpdateTemplateKHR = gen11_DestroyDescriptorUpdateTemplate,
    .vkUpdateDescriptorSetWithTemplate = gen11_UpdateDescriptorSetWithTemplate,
    .vkUpdateDescriptorSetWithTemplateKHR = gen11_UpdateDescriptorSetWithTemplate,
    .vkCmdPushDescriptorSetWithTemplateKHR = gen11_CmdPushDescriptorSetWithTemplateKHR,
    .vkGetPhysicalDeviceSurfaceCapabilities2KHR = gen11_GetPhysicalDeviceSurfaceCapabilities2KHR,
    .vkGetPhysicalDeviceSurfaceFormats2KHR = gen11_GetPhysicalDeviceSurfaceFormats2KHR,
    .vkGetBufferMemoryRequirements2 = gen11_GetBufferMemoryRequirements2,
    .vkGetBufferMemoryRequirements2KHR = gen11_GetBufferMemoryRequirements2,
    .vkGetImageMemoryRequirements2 = gen11_GetImageMemoryRequirements2,
    .vkGetImageMemoryRequirements2KHR = gen11_GetImageMemoryRequirements2,
    .vkGetImageSparseMemoryRequirements2 = gen11_GetImageSparseMemoryRequirements2,
    .vkGetImageSparseMemoryRequirements2KHR = gen11_GetImageSparseMemoryRequirements2,
    .vkCreateSamplerYcbcrConversion = gen11_CreateSamplerYcbcrConversion,
    .vkCreateSamplerYcbcrConversionKHR = gen11_CreateSamplerYcbcrConversion,
    .vkDestroySamplerYcbcrConversion = gen11_DestroySamplerYcbcrConversion,
    .vkDestroySamplerYcbcrConversionKHR = gen11_DestroySamplerYcbcrConversion,
    .vkGetDeviceQueue2 = gen11_GetDeviceQueue2,
    .vkGetDescriptorSetLayoutSupport = gen11_GetDescriptorSetLayoutSupport,
    .vkGetDescriptorSetLayoutSupportKHR = gen11_GetDescriptorSetLayoutSupport,
    .vkCreateDmaBufImageINTEL = gen11_CreateDmaBufImageINTEL,
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
  anv_tramp_QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence)
  {
      ANV_FROM_HANDLE(anv_queue, anv_queue, queue);
      return anv_queue->device->dispatch.vkQueueSubmit(queue, submitCount, pSubmits, fence);
  }
  static VkResult
  anv_tramp_QueueWaitIdle(VkQueue queue)
  {
      ANV_FROM_HANDLE(anv_queue, anv_queue, queue);
      return anv_queue->device->dispatch.vkQueueWaitIdle(queue);
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
  anv_tramp_QueueBindSparse(VkQueue queue, uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence)
  {
      ANV_FROM_HANDLE(anv_queue, anv_queue, queue);
      return anv_queue->device->dispatch.vkQueueBindSparse(queue, bindInfoCount, pBindInfo, fence);
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
  static VkResult
  anv_tramp_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
  {
      ANV_FROM_HANDLE(anv_queue, anv_queue, queue);
      return anv_queue->device->dispatch.vkQueuePresentKHR(queue, pPresentInfo);
  }
                                                                                              static void
  anv_tramp_CmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdPushDescriptorSetKHR(commandBuffer, pipelineBindPoint, layout, set, descriptorWriteCount, pDescriptorWrites);
  }
  static void
  anv_tramp_TrimCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlags flags)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkTrimCommandPool(device, commandPool, flags);
  }
              static VkResult
  anv_tramp_GetMemoryFdKHR(VkDevice device, const VkMemoryGetFdInfoKHR* pGetFdInfo, int* pFd)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetMemoryFdKHR(device, pGetFdInfo, pFd);
  }
  static VkResult
  anv_tramp_GetMemoryFdPropertiesKHR(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, int fd, VkMemoryFdPropertiesKHR* pMemoryFdProperties)
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
          static void
  anv_tramp_GetDeviceGroupPeerMemoryFeatures(VkDevice device, uint32_t heapIndex, uint32_t localDeviceIndex, uint32_t remoteDeviceIndex, VkPeerMemoryFeatureFlags* pPeerMemoryFeatures)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetDeviceGroupPeerMemoryFeatures(device, heapIndex, localDeviceIndex, remoteDeviceIndex, pPeerMemoryFeatures);
  }
      static VkResult
  anv_tramp_BindBufferMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkBindBufferMemory2(device, bindInfoCount, pBindInfos);
  }
      static VkResult
  anv_tramp_BindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkBindImageMemory2(device, bindInfoCount, pBindInfos);
  }
      static void
  anv_tramp_CmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdSetDeviceMask(commandBuffer, deviceMask);
  }
      static VkResult
  anv_tramp_GetDeviceGroupPresentCapabilitiesKHR(VkDevice device, VkDeviceGroupPresentCapabilitiesKHR* pDeviceGroupPresentCapabilities)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetDeviceGroupPresentCapabilitiesKHR(device, pDeviceGroupPresentCapabilities);
  }
  static VkResult
  anv_tramp_GetDeviceGroupSurfacePresentModesKHR(VkDevice device, VkSurfaceKHR surface, VkDeviceGroupPresentModeFlagsKHR* pModes)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetDeviceGroupSurfacePresentModesKHR(device, surface, pModes);
  }
  static VkResult
  anv_tramp_AcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkAcquireNextImage2KHR(device, pAcquireInfo, pImageIndex);
  }
  static void
  anv_tramp_CmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdDispatchBase(commandBuffer, baseGroupX, baseGroupY, baseGroupZ, groupCountX, groupCountY, groupCountZ);
  }
          static VkResult
  anv_tramp_CreateDescriptorUpdateTemplate(VkDevice device, const VkDescriptorUpdateTemplateCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorUpdateTemplate* pDescriptorUpdateTemplate)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateDescriptorUpdateTemplate(device, pCreateInfo, pAllocator, pDescriptorUpdateTemplate);
  }
      static void
  anv_tramp_DestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroyDescriptorUpdateTemplate(device, descriptorUpdateTemplate, pAllocator);
  }
      static void
  anv_tramp_UpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void* pData)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkUpdateDescriptorSetWithTemplate(device, descriptorSet, descriptorUpdateTemplate, pData);
  }
      static void
  anv_tramp_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer, VkDescriptorUpdateTemplate descriptorUpdateTemplate, VkPipelineLayout layout, uint32_t set, const void* pData)
  {
      ANV_FROM_HANDLE(anv_cmd_buffer, anv_cmd_buffer, commandBuffer);
      return anv_cmd_buffer->device->dispatch.vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, descriptorUpdateTemplate, layout, set, pData);
  }
          static void
  anv_tramp_GetBufferMemoryRequirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetBufferMemoryRequirements2(device, pInfo, pMemoryRequirements);
  }
      static void
  anv_tramp_GetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetImageMemoryRequirements2(device, pInfo, pMemoryRequirements);
  }
      static void
  anv_tramp_GetImageSparseMemoryRequirements2(VkDevice device, const VkImageSparseMemoryRequirementsInfo2* pInfo, uint32_t* pSparseMemoryRequirementCount, VkSparseImageMemoryRequirements2* pSparseMemoryRequirements)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetImageSparseMemoryRequirements2(device, pInfo, pSparseMemoryRequirementCount, pSparseMemoryRequirements);
  }
      static VkResult
  anv_tramp_CreateSamplerYcbcrConversion(VkDevice device, const VkSamplerYcbcrConversionCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSamplerYcbcrConversion* pYcbcrConversion)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkCreateSamplerYcbcrConversion(device, pCreateInfo, pAllocator, pYcbcrConversion);
  }
      static void
  anv_tramp_DestroySamplerYcbcrConversion(VkDevice device, VkSamplerYcbcrConversion ycbcrConversion, const VkAllocationCallbacks* pAllocator)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkDestroySamplerYcbcrConversion(device, ycbcrConversion, pAllocator);
  }
      static void
  anv_tramp_GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* pQueueInfo, VkQueue* pQueue)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetDeviceQueue2(device, pQueueInfo, pQueue);
  }
  static void
  anv_tramp_GetDescriptorSetLayoutSupport(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, VkDescriptorSetLayoutSupport* pSupport)
  {
      ANV_FROM_HANDLE(anv_device, anv_device, device);
      return anv_device->dispatch.vkGetDescriptorSetLayoutSupport(device, pCreateInfo, pSupport);
  }
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
    .vkQueueSubmit = anv_tramp_QueueSubmit,
    .vkQueueWaitIdle = anv_tramp_QueueWaitIdle,
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
        .vkQueueBindSparse = anv_tramp_QueueBindSparse,
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
    .vkQueuePresentKHR = anv_tramp_QueuePresentKHR,
                                                                                                .vkCmdPushDescriptorSetKHR = anv_tramp_CmdPushDescriptorSetKHR,
    .vkTrimCommandPool = anv_tramp_TrimCommandPool,
    .vkTrimCommandPoolKHR = anv_tramp_TrimCommandPool,
            .vkGetMemoryFdKHR = anv_tramp_GetMemoryFdKHR,
    .vkGetMemoryFdPropertiesKHR = anv_tramp_GetMemoryFdPropertiesKHR,
            .vkGetSemaphoreFdKHR = anv_tramp_GetSemaphoreFdKHR,
    .vkImportSemaphoreFdKHR = anv_tramp_ImportSemaphoreFdKHR,
            .vkGetFenceFdKHR = anv_tramp_GetFenceFdKHR,
    .vkImportFenceFdKHR = anv_tramp_ImportFenceFdKHR,
            .vkGetDeviceGroupPeerMemoryFeatures = anv_tramp_GetDeviceGroupPeerMemoryFeatures,
    .vkGetDeviceGroupPeerMemoryFeaturesKHR = anv_tramp_GetDeviceGroupPeerMemoryFeatures,
    .vkBindBufferMemory2 = anv_tramp_BindBufferMemory2,
    .vkBindBufferMemory2KHR = anv_tramp_BindBufferMemory2,
    .vkBindImageMemory2 = anv_tramp_BindImageMemory2,
    .vkBindImageMemory2KHR = anv_tramp_BindImageMemory2,
    .vkCmdSetDeviceMask = anv_tramp_CmdSetDeviceMask,
    .vkCmdSetDeviceMaskKHR = anv_tramp_CmdSetDeviceMask,
    .vkGetDeviceGroupPresentCapabilitiesKHR = anv_tramp_GetDeviceGroupPresentCapabilitiesKHR,
    .vkGetDeviceGroupSurfacePresentModesKHR = anv_tramp_GetDeviceGroupSurfacePresentModesKHR,
    .vkAcquireNextImage2KHR = anv_tramp_AcquireNextImage2KHR,
    .vkCmdDispatchBase = anv_tramp_CmdDispatchBase,
    .vkCmdDispatchBaseKHR = anv_tramp_CmdDispatchBase,
        .vkCreateDescriptorUpdateTemplate = anv_tramp_CreateDescriptorUpdateTemplate,
    .vkCreateDescriptorUpdateTemplateKHR = anv_tramp_CreateDescriptorUpdateTemplate,
    .vkDestroyDescriptorUpdateTemplate = anv_tramp_DestroyDescriptorUpdateTemplate,
    .vkDestroyDescriptorUpdateTemplateKHR = anv_tramp_DestroyDescriptorUpdateTemplate,
    .vkUpdateDescriptorSetWithTemplate = anv_tramp_UpdateDescriptorSetWithTemplate,
    .vkUpdateDescriptorSetWithTemplateKHR = anv_tramp_UpdateDescriptorSetWithTemplate,
    .vkCmdPushDescriptorSetWithTemplateKHR = anv_tramp_CmdPushDescriptorSetWithTemplateKHR,
            .vkGetBufferMemoryRequirements2 = anv_tramp_GetBufferMemoryRequirements2,
    .vkGetBufferMemoryRequirements2KHR = anv_tramp_GetBufferMemoryRequirements2,
    .vkGetImageMemoryRequirements2 = anv_tramp_GetImageMemoryRequirements2,
    .vkGetImageMemoryRequirements2KHR = anv_tramp_GetImageMemoryRequirements2,
    .vkGetImageSparseMemoryRequirements2 = anv_tramp_GetImageSparseMemoryRequirements2,
    .vkGetImageSparseMemoryRequirements2KHR = anv_tramp_GetImageSparseMemoryRequirements2,
    .vkCreateSamplerYcbcrConversion = anv_tramp_CreateSamplerYcbcrConversion,
    .vkCreateSamplerYcbcrConversionKHR = anv_tramp_CreateSamplerYcbcrConversion,
    .vkDestroySamplerYcbcrConversion = anv_tramp_DestroySamplerYcbcrConversion,
    .vkDestroySamplerYcbcrConversionKHR = anv_tramp_DestroySamplerYcbcrConversion,
    .vkGetDeviceQueue2 = anv_tramp_GetDeviceQueue2,
    .vkGetDescriptorSetLayoutSupport = anv_tramp_GetDescriptorSetLayoutSupport,
    .vkGetDescriptorSetLayoutSupportKHR = anv_tramp_GetDescriptorSetLayoutSupport,
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
      /* vkCreateInstance */
         return !device && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 1:
      /* vkDestroyInstance */
         return !device && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 2:
      /* vkEnumeratePhysicalDevices */
         return !device && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 3:
      /* vkGetDeviceProcAddr */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 4:
      /* vkGetInstanceProcAddr */
         return !device && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 5:
      /* vkGetPhysicalDeviceProperties */
         return !device && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 6:
      /* vkGetPhysicalDeviceQueueFamilyProperties */
         return !device && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 7:
      /* vkGetPhysicalDeviceMemoryProperties */
         return !device && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 8:
      /* vkGetPhysicalDeviceFeatures */
         return !device && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 9:
      /* vkGetPhysicalDeviceFormatProperties */
         return !device && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 10:
      /* vkGetPhysicalDeviceImageFormatProperties */
         return !device && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 11:
      /* vkCreateDevice */
         return !device && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 12:
      /* vkDestroyDevice */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 13:
      /* vkEnumerateInstanceVersion */
         return !device && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 14:
      /* vkEnumerateInstanceLayerProperties */
         return !device && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 15:
      /* vkEnumerateInstanceExtensionProperties */
         return !device && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 16:
      /* vkEnumerateDeviceLayerProperties */
         return !device && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 17:
      /* vkEnumerateDeviceExtensionProperties */
         return !device && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 18:
      /* vkGetDeviceQueue */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 19:
      /* vkQueueSubmit */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 20:
      /* vkQueueWaitIdle */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 21:
      /* vkDeviceWaitIdle */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 22:
      /* vkAllocateMemory */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 23:
      /* vkFreeMemory */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 24:
      /* vkMapMemory */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 25:
      /* vkUnmapMemory */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 26:
      /* vkFlushMappedMemoryRanges */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 27:
      /* vkInvalidateMappedMemoryRanges */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 28:
      /* vkGetDeviceMemoryCommitment */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 29:
      /* vkGetBufferMemoryRequirements */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 30:
      /* vkBindBufferMemory */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 31:
      /* vkGetImageMemoryRequirements */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 32:
      /* vkBindImageMemory */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 33:
      /* vkGetImageSparseMemoryRequirements */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 34:
      /* vkGetPhysicalDeviceSparseImageFormatProperties */
         return !device && VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 35:
      /* vkQueueBindSparse */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 36:
      /* vkCreateFence */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 37:
      /* vkDestroyFence */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 38:
      /* vkResetFences */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 39:
      /* vkGetFenceStatus */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 40:
      /* vkWaitForFences */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 41:
      /* vkCreateSemaphore */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 42:
      /* vkDestroySemaphore */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 43:
      /* vkCreateEvent */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 44:
      /* vkDestroyEvent */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 45:
      /* vkGetEventStatus */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 46:
      /* vkSetEvent */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 47:
      /* vkResetEvent */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 48:
      /* vkCreateQueryPool */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 49:
      /* vkDestroyQueryPool */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 50:
      /* vkGetQueryPoolResults */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 51:
      /* vkCreateBuffer */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 52:
      /* vkDestroyBuffer */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 53:
      /* vkCreateBufferView */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 54:
      /* vkDestroyBufferView */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 55:
      /* vkCreateImage */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 56:
      /* vkDestroyImage */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 57:
      /* vkGetImageSubresourceLayout */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 58:
      /* vkCreateImageView */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 59:
      /* vkDestroyImageView */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 60:
      /* vkCreateShaderModule */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 61:
      /* vkDestroyShaderModule */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 62:
      /* vkCreatePipelineCache */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 63:
      /* vkDestroyPipelineCache */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 64:
      /* vkGetPipelineCacheData */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 65:
      /* vkMergePipelineCaches */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 66:
      /* vkCreateGraphicsPipelines */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 67:
      /* vkCreateComputePipelines */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 68:
      /* vkDestroyPipeline */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 69:
      /* vkCreatePipelineLayout */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 70:
      /* vkDestroyPipelineLayout */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 71:
      /* vkCreateSampler */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 72:
      /* vkDestroySampler */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 73:
      /* vkCreateDescriptorSetLayout */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 74:
      /* vkDestroyDescriptorSetLayout */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 75:
      /* vkCreateDescriptorPool */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 76:
      /* vkDestroyDescriptorPool */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 77:
      /* vkResetDescriptorPool */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 78:
      /* vkAllocateDescriptorSets */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 79:
      /* vkFreeDescriptorSets */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 80:
      /* vkUpdateDescriptorSets */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 81:
      /* vkCreateFramebuffer */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 82:
      /* vkDestroyFramebuffer */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 83:
      /* vkCreateRenderPass */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 84:
      /* vkDestroyRenderPass */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 85:
      /* vkGetRenderAreaGranularity */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 86:
      /* vkCreateCommandPool */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 87:
      /* vkDestroyCommandPool */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 88:
      /* vkResetCommandPool */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 89:
      /* vkAllocateCommandBuffers */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 90:
      /* vkFreeCommandBuffers */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 91:
      /* vkBeginCommandBuffer */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 92:
      /* vkEndCommandBuffer */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 93:
      /* vkResetCommandBuffer */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 94:
      /* vkCmdBindPipeline */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 95:
      /* vkCmdSetViewport */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 96:
      /* vkCmdSetScissor */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 97:
      /* vkCmdSetLineWidth */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 98:
      /* vkCmdSetDepthBias */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 99:
      /* vkCmdSetBlendConstants */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 100:
      /* vkCmdSetDepthBounds */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 101:
      /* vkCmdSetStencilCompareMask */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 102:
      /* vkCmdSetStencilWriteMask */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 103:
      /* vkCmdSetStencilReference */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 104:
      /* vkCmdBindDescriptorSets */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 105:
      /* vkCmdBindIndexBuffer */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 106:
      /* vkCmdBindVertexBuffers */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 107:
      /* vkCmdDraw */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 108:
      /* vkCmdDrawIndexed */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 109:
      /* vkCmdDrawIndirect */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 110:
      /* vkCmdDrawIndexedIndirect */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 111:
      /* vkCmdDispatch */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 112:
      /* vkCmdDispatchIndirect */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 113:
      /* vkCmdCopyBuffer */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 114:
      /* vkCmdCopyImage */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 115:
      /* vkCmdBlitImage */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 116:
      /* vkCmdCopyBufferToImage */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 117:
      /* vkCmdCopyImageToBuffer */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 118:
      /* vkCmdUpdateBuffer */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 119:
      /* vkCmdFillBuffer */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 120:
      /* vkCmdClearColorImage */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 121:
      /* vkCmdClearDepthStencilImage */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 122:
      /* vkCmdClearAttachments */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 123:
      /* vkCmdResolveImage */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 124:
      /* vkCmdSetEvent */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 125:
      /* vkCmdResetEvent */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 126:
      /* vkCmdWaitEvents */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 127:
      /* vkCmdPipelineBarrier */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 128:
      /* vkCmdBeginQuery */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 129:
      /* vkCmdEndQuery */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 130:
      /* vkCmdResetQueryPool */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 131:
      /* vkCmdWriteTimestamp */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 132:
      /* vkCmdCopyQueryPoolResults */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 133:
      /* vkCmdPushConstants */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 134:
      /* vkCmdBeginRenderPass */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 135:
      /* vkCmdNextSubpass */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 136:
      /* vkCmdEndRenderPass */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 137:
      /* vkCmdExecuteCommands */
         return VK_MAKE_VERSION(1, 0, 0) <= core_version;
   case 138:
      /* vkDestroySurfaceKHR */
      if (!device && instance->KHR_surface) return true;
      return false;
   case 139:
      /* vkGetPhysicalDeviceSurfaceSupportKHR */
      if (!device && instance->KHR_surface) return true;
      return false;
   case 140:
      /* vkGetPhysicalDeviceSurfaceCapabilitiesKHR */
      if (!device && instance->KHR_surface) return true;
      return false;
   case 141:
      /* vkGetPhysicalDeviceSurfaceFormatsKHR */
      if (!device && instance->KHR_surface) return true;
      return false;
   case 142:
      /* vkGetPhysicalDeviceSurfacePresentModesKHR */
      if (!device && instance->KHR_surface) return true;
      return false;
   case 143:
      /* vkCreateSwapchainKHR */
      if (!device || device->KHR_swapchain) return true;
      return false;
   case 144:
      /* vkDestroySwapchainKHR */
      if (!device || device->KHR_swapchain) return true;
      return false;
   case 145:
      /* vkGetSwapchainImagesKHR */
      if (!device || device->KHR_swapchain) return true;
      return false;
   case 146:
      /* vkAcquireNextImageKHR */
      if (!device || device->KHR_swapchain) return true;
      return false;
   case 147:
      /* vkQueuePresentKHR */
      if (!device || device->KHR_swapchain) return true;
      return false;
   case 148:
      /* vkCreateWaylandSurfaceKHR */
      if (!device && instance->KHR_wayland_surface) return true;
      return false;
   case 149:
      /* vkGetPhysicalDeviceWaylandPresentationSupportKHR */
      if (!device && instance->KHR_wayland_surface) return true;
      return false;
   case 150:
      /* vkCreateXlibSurfaceKHR */
      if (!device && instance->KHR_xlib_surface) return true;
      return false;
   case 151:
      /* vkGetPhysicalDeviceXlibPresentationSupportKHR */
      if (!device && instance->KHR_xlib_surface) return true;
      return false;
   case 152:
      /* vkCreateXcbSurfaceKHR */
      if (!device && instance->KHR_xcb_surface) return true;
      return false;
   case 153:
      /* vkGetPhysicalDeviceXcbPresentationSupportKHR */
      if (!device && instance->KHR_xcb_surface) return true;
      return false;
   case 154:
      /* vkCreateDebugReportCallbackEXT */
      if (!device && instance->EXT_debug_report) return true;
      return false;
   case 155:
      /* vkDestroyDebugReportCallbackEXT */
      if (!device && instance->EXT_debug_report) return true;
      return false;
   case 156:
      /* vkDebugReportMessageEXT */
      if (!device && instance->EXT_debug_report) return true;
      return false;
   case 157:
      /* vkGetPhysicalDeviceFeatures2 */
         return !device && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 158:
      /* vkGetPhysicalDeviceFeatures2KHR */
      if (!device && instance->KHR_get_physical_device_properties2) return true;
      return false;
   case 159:
      /* vkGetPhysicalDeviceProperties2 */
         return !device && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 160:
      /* vkGetPhysicalDeviceProperties2KHR */
      if (!device && instance->KHR_get_physical_device_properties2) return true;
      return false;
   case 161:
      /* vkGetPhysicalDeviceFormatProperties2 */
         return !device && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 162:
      /* vkGetPhysicalDeviceFormatProperties2KHR */
      if (!device && instance->KHR_get_physical_device_properties2) return true;
      return false;
   case 163:
      /* vkGetPhysicalDeviceImageFormatProperties2 */
         return !device && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 164:
      /* vkGetPhysicalDeviceImageFormatProperties2KHR */
      if (!device && instance->KHR_get_physical_device_properties2) return true;
      return false;
   case 165:
      /* vkGetPhysicalDeviceQueueFamilyProperties2 */
         return !device && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 166:
      /* vkGetPhysicalDeviceQueueFamilyProperties2KHR */
      if (!device && instance->KHR_get_physical_device_properties2) return true;
      return false;
   case 167:
      /* vkGetPhysicalDeviceMemoryProperties2 */
         return !device && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 168:
      /* vkGetPhysicalDeviceMemoryProperties2KHR */
      if (!device && instance->KHR_get_physical_device_properties2) return true;
      return false;
   case 169:
      /* vkGetPhysicalDeviceSparseImageFormatProperties2 */
         return !device && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 170:
      /* vkGetPhysicalDeviceSparseImageFormatProperties2KHR */
      if (!device && instance->KHR_get_physical_device_properties2) return true;
      return false;
   case 171:
      /* vkCmdPushDescriptorSetKHR */
      if (!device || device->KHR_push_descriptor) return true;
      return false;
   case 172:
      /* vkTrimCommandPool */
         return VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 173:
      /* vkTrimCommandPoolKHR */
      if (!device || device->KHR_maintenance1) return true;
      return false;
   case 174:
      /* vkGetPhysicalDeviceExternalBufferProperties */
         return !device && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 175:
      /* vkGetPhysicalDeviceExternalBufferPropertiesKHR */
      if (!device && instance->KHR_external_memory_capabilities) return true;
      return false;
   case 176:
      /* vkGetMemoryFdKHR */
      if (!device || device->KHR_external_memory_fd) return true;
      return false;
   case 177:
      /* vkGetMemoryFdPropertiesKHR */
      if (!device || device->KHR_external_memory_fd) return true;
      return false;
   case 178:
      /* vkGetPhysicalDeviceExternalSemaphoreProperties */
         return !device && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 179:
      /* vkGetPhysicalDeviceExternalSemaphorePropertiesKHR */
      if (!device && instance->KHR_external_semaphore_capabilities) return true;
      return false;
   case 180:
      /* vkGetSemaphoreFdKHR */
      if (!device || device->KHR_external_semaphore_fd) return true;
      return false;
   case 181:
      /* vkImportSemaphoreFdKHR */
      if (!device || device->KHR_external_semaphore_fd) return true;
      return false;
   case 182:
      /* vkGetPhysicalDeviceExternalFenceProperties */
         return !device && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 183:
      /* vkGetPhysicalDeviceExternalFencePropertiesKHR */
      if (!device && instance->KHR_external_fence_capabilities) return true;
      return false;
   case 184:
      /* vkGetFenceFdKHR */
      if (!device || device->KHR_external_fence_fd) return true;
      return false;
   case 185:
      /* vkImportFenceFdKHR */
      if (!device || device->KHR_external_fence_fd) return true;
      return false;
   case 186:
      /* vkEnumeratePhysicalDeviceGroups */
         return !device && VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 187:
      /* vkEnumeratePhysicalDeviceGroupsKHR */
      if (!device && instance->KHR_device_group_creation) return true;
      return false;
   case 188:
      /* vkGetDeviceGroupPeerMemoryFeatures */
         return VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 189:
      /* vkGetDeviceGroupPeerMemoryFeaturesKHR */
      if (!device || device->KHR_device_group) return true;
      return false;
   case 190:
      /* vkBindBufferMemory2 */
         return VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 191:
      /* vkBindBufferMemory2KHR */
      if (!device || device->KHR_bind_memory2) return true;
      return false;
   case 192:
      /* vkBindImageMemory2 */
         return VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 193:
      /* vkBindImageMemory2KHR */
      if (!device || device->KHR_bind_memory2) return true;
      return false;
   case 194:
      /* vkCmdSetDeviceMask */
         return VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 195:
      /* vkCmdSetDeviceMaskKHR */
      if (!device || device->KHR_device_group) return true;
      return false;
   case 196:
      /* vkGetDeviceGroupPresentCapabilitiesKHR */
      if (!device || device->KHR_swapchain) return true;
      if (!device || device->KHR_device_group) return true;
      return false;
   case 197:
      /* vkGetDeviceGroupSurfacePresentModesKHR */
      if (!device || device->KHR_swapchain) return true;
      if (!device || device->KHR_device_group) return true;
      return false;
   case 198:
      /* vkAcquireNextImage2KHR */
      if (!device || device->KHR_swapchain) return true;
      if (!device || device->KHR_device_group) return true;
      return false;
   case 199:
      /* vkCmdDispatchBase */
         return VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 200:
      /* vkCmdDispatchBaseKHR */
      if (!device || device->KHR_device_group) return true;
      return false;
   case 201:
      /* vkGetPhysicalDevicePresentRectanglesKHR */
      if (!device || device->KHR_swapchain) return true;
      if (!device || device->KHR_device_group) return true;
      return false;
   case 202:
      /* vkCreateDescriptorUpdateTemplate */
         return VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 203:
      /* vkCreateDescriptorUpdateTemplateKHR */
      if (!device || device->KHR_descriptor_update_template) return true;
      return false;
   case 204:
      /* vkDestroyDescriptorUpdateTemplate */
         return VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 205:
      /* vkDestroyDescriptorUpdateTemplateKHR */
      if (!device || device->KHR_descriptor_update_template) return true;
      return false;
   case 206:
      /* vkUpdateDescriptorSetWithTemplate */
         return VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 207:
      /* vkUpdateDescriptorSetWithTemplateKHR */
      if (!device || device->KHR_descriptor_update_template) return true;
      return false;
   case 208:
      /* vkCmdPushDescriptorSetWithTemplateKHR */
      if (!device || device->KHR_push_descriptor) return true;
      if (!device || device->KHR_descriptor_update_template) return true;
      return false;
   case 209:
      /* vkGetPhysicalDeviceSurfaceCapabilities2KHR */
      if (!device && instance->KHR_get_surface_capabilities2) return true;
      return false;
   case 210:
      /* vkGetPhysicalDeviceSurfaceFormats2KHR */
      if (!device && instance->KHR_get_surface_capabilities2) return true;
      return false;
   case 211:
      /* vkGetBufferMemoryRequirements2 */
         return VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 212:
      /* vkGetBufferMemoryRequirements2KHR */
      if (!device || device->KHR_get_memory_requirements2) return true;
      return false;
   case 213:
      /* vkGetImageMemoryRequirements2 */
         return VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 214:
      /* vkGetImageMemoryRequirements2KHR */
      if (!device || device->KHR_get_memory_requirements2) return true;
      return false;
   case 215:
      /* vkGetImageSparseMemoryRequirements2 */
         return VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 216:
      /* vkGetImageSparseMemoryRequirements2KHR */
      if (!device || device->KHR_get_memory_requirements2) return true;
      return false;
   case 217:
      /* vkCreateSamplerYcbcrConversion */
         return VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 218:
      /* vkCreateSamplerYcbcrConversionKHR */
      if (!device || device->KHR_sampler_ycbcr_conversion) return true;
      return false;
   case 219:
      /* vkDestroySamplerYcbcrConversion */
         return VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 220:
      /* vkDestroySamplerYcbcrConversionKHR */
      if (!device || device->KHR_sampler_ycbcr_conversion) return true;
      return false;
   case 221:
      /* vkGetDeviceQueue2 */
         return VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 222:
      /* vkGetDescriptorSetLayoutSupport */
         return VK_MAKE_VERSION(1, 1, 0) <= core_version;
   case 223:
      /* vkGetDescriptorSetLayoutSupportKHR */
      if (!device || device->KHR_maintenance3) return true;
      return false;
   case 224:
      /* vkCreateDmaBufImageINTEL */
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
   case 11:
      genX_table = &gen11_dispatch_table;
      break;
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

int
anv_get_entrypoint_index(const char *name)
{
   return string_map_lookup(name);
}

void *
anv_lookup_entrypoint(const struct gen_device_info *devinfo, const char *name)
{
   int idx = anv_get_entrypoint_index(name);
   if (idx < 0)
      return NULL;
   return anv_resolve_entrypoint(devinfo, idx);
}