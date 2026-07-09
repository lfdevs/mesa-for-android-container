/*
** Copyright 2025 The Android Open Source Project
** Copyright 2015-2023 The Khronos Group Inc.
**
** SPDX-License-Identifier: Apache-2.0
*/

/*
** This header is generated from the Khronos Vulkan XML API Registry.
**
*/

#pragma once
#ifdef VK_GFXSTREAM_STRUCTURE_TYPE_EXT
#include "vulkan_gfxstream_structure_type.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif


// VK_GOOGLE_gfxstream is a preprocessor guard. Do not pass it to API calls.
#define VK_GOOGLE_gfxstream 1
#define VK_GOOGLE_GFXSTREAM_SPEC_VERSION  0
#define VK_GOOGLE_GFXSTREAM_NUMBER        386
#define VK_GOOGLE_GFXSTREAM_EXTENSION_NAME "VK_GOOGLE_gfxstream"
typedef struct VkImportColorBufferGOOGLE {
    VkStructureType    sType;
    void*              pNext;
    uint32_t           colorBuffer;
} VkImportColorBufferGOOGLE;

typedef struct VkImportBufferGOOGLE {
    VkStructureType    sType;
    void*              pNext;
    uint32_t           buffer;
} VkImportBufferGOOGLE;

typedef struct VkCreateBlobGOOGLE {
    VkStructureType    sType;
    void*              pNext;
    uint32_t           blobMem;
    uint32_t           blobFlags;
    uint64_t           blobId;
} VkCreateBlobGOOGLE;

typedef struct VkDebugMetadataGuestProcessNameGOOGLE {
    VkStructureType    sType;
    const void*        pNext;
    const char*        pName;
} VkDebugMetadataGuestProcessNameGOOGLE;

typedef struct VkDebugMetadataGuestProcessIdGOOGLE {
    VkStructureType    sType;
    const void*        pNext;
    uint64_t           id;
} VkDebugMetadataGuestProcessIdGOOGLE;

typedef struct VkDebugMetadataGuestThreadNameGOOGLE {
    VkStructureType    sType;
    const void*        pNext;
    const char*        pName;
} VkDebugMetadataGuestThreadNameGOOGLE;

typedef struct VkDebugMetadataGuestThreadIdGOOGLE {
    VkStructureType    sType;
    const void*        pNext;
    uint64_t           id;
} VkDebugMetadataGuestThreadIdGOOGLE;

typedef struct VkDebugMetadataGOOGLE {
    VkStructureType    sType;
    const void*        pNext;
} VkDebugMetadataGOOGLE;

typedef VkResult (VKAPI_PTR *PFN_vkMapMemoryIntoAddressSpaceGOOGLE)(VkDevice device, VkDeviceMemory memory, uint64_t* pAddress);
typedef void (VKAPI_PTR *PFN_vkUpdateDescriptorSetWithTemplateSizedGOOGLE)(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplate descriptorUpdateTemplate, uint32_t imageInfoCount, uint32_t bufferInfoCount, uint32_t bufferViewCount, const uint32_t* pImageInfoEntryIndices, const uint32_t* pBufferInfoEntryIndices, const uint32_t* pBufferViewEntryIndices, const VkDescriptorImageInfo* pImageInfos, const VkDescriptorBufferInfo* pBufferInfos, const VkBufferView* pBufferViews);
typedef void (VKAPI_PTR *PFN_vkBeginCommandBufferAsyncGOOGLE)(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo);
typedef void (VKAPI_PTR *PFN_vkEndCommandBufferAsyncGOOGLE)(VkCommandBuffer commandBuffer);
typedef void (VKAPI_PTR *PFN_vkResetCommandBufferAsyncGOOGLE)(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags);
typedef void (VKAPI_PTR *PFN_vkCommandBufferHostSyncGOOGLE)(VkCommandBuffer commandBuffer, uint32_t needHostSync, uint32_t sequenceNumber);
typedef VkResult (VKAPI_PTR *PFN_vkCreateImageWithRequirementsGOOGLE)(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage, VkMemoryRequirements* pMemoryRequirements);
typedef VkResult (VKAPI_PTR *PFN_vkCreateBufferWithRequirementsGOOGLE)(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer, VkMemoryRequirements* pMemoryRequirements);
typedef VkResult (VKAPI_PTR *PFN_vkGetMemoryHostAddressInfoGOOGLE)(VkDevice device, VkDeviceMemory memory, uint64_t* pAddress, uint64_t* pSize, uint64_t* pHostmemId);
typedef VkResult (VKAPI_PTR *PFN_vkFreeMemorySyncGOOGLE)(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator);
typedef void (VKAPI_PTR *PFN_vkQueueHostSyncGOOGLE)(VkQueue queue, uint32_t needHostSync, uint32_t sequenceNumber);
typedef void (VKAPI_PTR *PFN_vkQueueSubmitAsyncGOOGLE)(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence);
typedef void (VKAPI_PTR *PFN_vkQueueWaitIdleAsyncGOOGLE)(VkQueue queue);
typedef void (VKAPI_PTR *PFN_vkQueueBindSparseAsyncGOOGLE)(VkQueue queue, uint32_t bindInfoCount, const VkBindSparseInfo* pBindInfo, VkFence fence);
typedef void (VKAPI_PTR *PFN_vkGetLinearImageLayoutGOOGLE)(VkDevice device, VkFormat format, VkDeviceSize* pOffset, VkDeviceSize* pRowPitchAlignment);
typedef void (VKAPI_PTR *PFN_vkGetLinearImageLayout2GOOGLE)(VkDevice device, const VkImageCreateInfo* pCreateInfo, VkDeviceSize* pOffset, VkDeviceSize* pRowPitchAlignment);
typedef void (VKAPI_PTR *PFN_vkQueueFlushCommandsGOOGLE)(VkQueue queue, VkCommandBuffer commandBuffer, VkDeviceSize dataSize, const void* pData);
typedef void (VKAPI_PTR *PFN_vkQueueCommitDescriptorSetUpdatesGOOGLE)(VkQueue queue, uint32_t descriptorPoolCount, const VkDescriptorPool* pDescriptorPools, uint32_t descriptorSetCount, const VkDescriptorSetLayout* pSetLayouts, const uint64_t* pDescriptorSetPoolIds, const uint32_t* pDescriptorSetWhichPool, const uint32_t* pDescriptorSetPendingAllocation, const uint32_t* pDescriptorWriteStartingIndices, uint32_t pendingDescriptorWriteCount, const VkWriteDescriptorSet* pPendingDescriptorWrites);
typedef void (VKAPI_PTR *PFN_vkCollectDescriptorPoolIdsGOOGLE)(VkDevice device, VkDescriptorPool descriptorPool, uint32_t* pPoolIdCount, uint64_t* pPoolIds);
typedef void (VKAPI_PTR *PFN_vkQueueSignalReleaseImageANDROIDAsyncGOOGLE)(VkQueue queue, uint32_t waitSemaphoreCount, const VkSemaphore* pWaitSemaphores, VkImage image);
typedef void (VKAPI_PTR *PFN_vkQueueFlushCommandsFromAuxMemoryGOOGLE)(VkQueue queue, VkCommandBuffer commandBuffer, VkDeviceMemory deviceMemory, VkDeviceSize dataOffset, VkDeviceSize dataSize);
typedef VkResult (VKAPI_PTR *PFN_vkGetBlobGOOGLE)(VkDevice device, VkDeviceMemory memory);
typedef void (VKAPI_PTR *PFN_vkUpdateDescriptorSetWithTemplateSized2GOOGLE)(VkDevice device, VkDescriptorSet descriptorSet, VkDescriptorUpdateTemplate descriptorUpdateTemplate, uint32_t imageInfoCount, uint32_t bufferInfoCount, uint32_t bufferViewCount, uint32_t inlineUniformBlockCount, const uint32_t* pImageInfoEntryIndices, const uint32_t* pBufferInfoEntryIndices, const uint32_t* pBufferViewEntryIndices, const VkDescriptorImageInfo* pImageInfos, const VkDescriptorBufferInfo* pBufferInfos, const VkBufferView* pBufferViews, const uint8_t* pInlineUniformBlockData);
typedef void (VKAPI_PTR *PFN_vkQueueSubmitAsync2GOOGLE)(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2* pSubmits, VkFence fence);
typedef VkResult (VKAPI_PTR *PFN_vkGetSemaphoreGOOGLE)(VkDevice device, VkSemaphore semaphore, uint64_t syncId);
typedef void (VKAPI_PTR *PFN_vkTraceAsyncGOOGLE)(uint64_t id);
typedef void (VKAPI_PTR *PFN_vkSetDebugMetadataAsyncGOOGLE)(const VkDebugMetadataGOOGLE* pDebugMetadata);

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemoryIntoAddressSpaceGOOGLE(
    VkDevice                                    device,
    VkDeviceMemory                              memory,
    uint64_t*                                   pAddress);

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplateSizedGOOGLE(
    VkDevice                                    device,
    VkDescriptorSet                             descriptorSet,
    VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
    uint32_t                                    imageInfoCount,
    uint32_t                                    bufferInfoCount,
    uint32_t                                    bufferViewCount,
    const uint32_t*                             pImageInfoEntryIndices,
    const uint32_t*                             pBufferInfoEntryIndices,
    const uint32_t*                             pBufferViewEntryIndices,
    const VkDescriptorImageInfo*                pImageInfos,
    const VkDescriptorBufferInfo*               pBufferInfos,
    const VkBufferView*                         pBufferViews);

VKAPI_ATTR void VKAPI_CALL vkBeginCommandBufferAsyncGOOGLE(
    VkCommandBuffer                             commandBuffer,
    const VkCommandBufferBeginInfo*             pBeginInfo);

VKAPI_ATTR void VKAPI_CALL vkEndCommandBufferAsyncGOOGLE(
    VkCommandBuffer                             commandBuffer);

VKAPI_ATTR void VKAPI_CALL vkResetCommandBufferAsyncGOOGLE(
    VkCommandBuffer                             commandBuffer,
    VkCommandBufferResetFlags                   flags);

VKAPI_ATTR void VKAPI_CALL vkCommandBufferHostSyncGOOGLE(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    needHostSync,
    uint32_t                                    sequenceNumber);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageWithRequirementsGOOGLE(
    VkDevice                                    device,
    const VkImageCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImage*                                    pImage,
    VkMemoryRequirements*                       pMemoryRequirements);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBufferWithRequirementsGOOGLE(
    VkDevice                                    device,
    const VkBufferCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkBuffer*                                   pBuffer,
    VkMemoryRequirements*                       pMemoryRequirements);

VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryHostAddressInfoGOOGLE(
    VkDevice                                    device,
    VkDeviceMemory                              memory,
    uint64_t*                                   pAddress,
    uint64_t*                                   pSize,
    uint64_t*                                   pHostmemId);

VKAPI_ATTR VkResult VKAPI_CALL vkFreeMemorySyncGOOGLE(
    VkDevice                                    device,
    VkDeviceMemory                              memory,
    const VkAllocationCallbacks*                pAllocator);

VKAPI_ATTR void VKAPI_CALL vkQueueHostSyncGOOGLE(
    VkQueue                                     queue,
    uint32_t                                    needHostSync,
    uint32_t                                    sequenceNumber);

VKAPI_ATTR void VKAPI_CALL vkQueueSubmitAsyncGOOGLE(
    VkQueue                                     queue,
    uint32_t                                    submitCount,
    const VkSubmitInfo*                         pSubmits,
    VkFence                                     fence);

VKAPI_ATTR void VKAPI_CALL vkQueueWaitIdleAsyncGOOGLE(
    VkQueue                                     queue);

VKAPI_ATTR void VKAPI_CALL vkQueueBindSparseAsyncGOOGLE(
    VkQueue                                     queue,
    uint32_t                                    bindInfoCount,
    const VkBindSparseInfo*                     pBindInfo,
    VkFence                                     fence);

VKAPI_ATTR void VKAPI_CALL vkGetLinearImageLayoutGOOGLE(
    VkDevice                                    device,
    VkFormat                                    format,
    VkDeviceSize*                               pOffset,
    VkDeviceSize*                               pRowPitchAlignment);

VKAPI_ATTR void VKAPI_CALL vkGetLinearImageLayout2GOOGLE(
    VkDevice                                    device,
    const VkImageCreateInfo*                    pCreateInfo,
    VkDeviceSize*                               pOffset,
    VkDeviceSize*                               pRowPitchAlignment);

VKAPI_ATTR void VKAPI_CALL vkQueueFlushCommandsGOOGLE(
    VkQueue                                     queue,
    VkCommandBuffer                             commandBuffer,
    VkDeviceSize                                dataSize,
    const void*                                 pData);

VKAPI_ATTR void VKAPI_CALL vkQueueCommitDescriptorSetUpdatesGOOGLE(
    VkQueue                                     queue,
    uint32_t                                    descriptorPoolCount,
    const VkDescriptorPool*                     pDescriptorPools,
    uint32_t                                    descriptorSetCount,
    const VkDescriptorSetLayout*                pSetLayouts,
    const uint64_t*                             pDescriptorSetPoolIds,
    const uint32_t*                             pDescriptorSetWhichPool,
    const uint32_t*                             pDescriptorSetPendingAllocation,
    const uint32_t*                             pDescriptorWriteStartingIndices,
    uint32_t                                    pendingDescriptorWriteCount,
    const VkWriteDescriptorSet*                 pPendingDescriptorWrites);

VKAPI_ATTR void VKAPI_CALL vkCollectDescriptorPoolIdsGOOGLE(
    VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    uint32_t*                                   pPoolIdCount,
    uint64_t*                                   pPoolIds);

VKAPI_ATTR void VKAPI_CALL vkQueueSignalReleaseImageANDROIDAsyncGOOGLE(
    VkQueue                                     queue,
    uint32_t                                    waitSemaphoreCount,
    const VkSemaphore*                          pWaitSemaphores,
    VkImage                                     image);

VKAPI_ATTR void VKAPI_CALL vkQueueFlushCommandsFromAuxMemoryGOOGLE(
    VkQueue                                     queue,
    VkCommandBuffer                             commandBuffer,
    VkDeviceMemory                              deviceMemory,
    VkDeviceSize                                dataOffset,
    VkDeviceSize                                dataSize);

VKAPI_ATTR VkResult VKAPI_CALL vkGetBlobGOOGLE(
    VkDevice                                    device,
    VkDeviceMemory                              memory);

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplateSized2GOOGLE(
    VkDevice                                    device,
    VkDescriptorSet                             descriptorSet,
    VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
    uint32_t                                    imageInfoCount,
    uint32_t                                    bufferInfoCount,
    uint32_t                                    bufferViewCount,
    uint32_t                                    inlineUniformBlockCount,
    const uint32_t*                             pImageInfoEntryIndices,
    const uint32_t*                             pBufferInfoEntryIndices,
    const uint32_t*                             pBufferViewEntryIndices,
    const VkDescriptorImageInfo*                pImageInfos,
    const VkDescriptorBufferInfo*               pBufferInfos,
    const VkBufferView*                         pBufferViews,
    const uint8_t*                              pInlineUniformBlockData);

VKAPI_ATTR void VKAPI_CALL vkQueueSubmitAsync2GOOGLE(
    VkQueue                                     queue,
    uint32_t                                    submitCount,
    const VkSubmitInfo2*                        pSubmits,
    VkFence                                     fence);

VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreGOOGLE(
    VkDevice                                    device,
    VkSemaphore                                 semaphore,
    uint64_t                                    syncId);

VKAPI_ATTR void VKAPI_CALL vkTraceAsyncGOOGLE(
    uint64_t                                    id);

VKAPI_ATTR void VKAPI_CALL vkSetDebugMetadataAsyncGOOGLE(
    const VkDebugMetadataGOOGLE*                pDebugMetadata);

#ifdef __cplusplus
}
#endif
