/*
 * Copyright © 2025 Collabora Ltd
 * SPDX-License-Identifier: MIT
 */

/*
 * This file contains a variety of mesa-internal extension structs and
 * enumerants.  These are not exposed to apps but are instead used for the
 * runtime components (including meta and WSI) to communicate additional
 * information to drivers beyond what is provided through the Vulkan spec
 * itself.  Care should be taken when adding anything here to avoid
 * conflicting with existing Vulkan enums if at all possible.
 */

#ifndef VK_INTERNAL_EXTS_H
#define VK_INTERNAL_EXTS_H

#include <vulkan/vulkan_core.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VK_PRIMITIVE_TOPOLOGY_META_RECT_LIST_MESA (VkPrimitiveTopology)11
#define VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA \
   (VkImageViewCreateFlagBits)0x80000000


/* This is always chained to VkImageCreateInfo when a wsi image is created.
 * It indicates that the image can be transitioned to/from
 * VK_IMAGE_LAYOUT_PRESENT_SRC_KHR.
 */
struct wsi_image_create_info {
    VkStructureType sType;
    const void *pNext;
    bool scanout;

    /* if true, the image is a blit source */
    bool blit_src;
};

struct wsi_memory_allocate_info {
    VkStructureType sType;
    const void *pNext;
    /**
     * If set, then the driver needs to do implicit synchronization on this BO.
     *
     * For DRM drivers, this flag will only get set before linux 6.0, at which
     * point DMA_BUF_IOCTL_IMPORT_SYNC_FILE was added.
     */
    bool implicit_sync;

    /**
     * If set, then the WSI will attach implicit synchronization's fences to
     * (and reference implicit sync fences from) the dma-buf for this memory, to
     * meet the requirement for implicit synchronization with the window system.
     * This does mean that the kernel BO must be able to participate in implicit
     * sync still when used as part of a non-explicit-synced submit (e.g. by a
     * GL driver in the X server).
     */
    bool dma_buf_sync_file;
};

/* To be chained into VkSurfaceCapabilities2KHR */
struct wsi_surface_supported_counters {
   VkStructureType sType;
   const void *pNext;

   VkSurfaceCounterFlagsEXT supported_surface_counters;

};

/* This is guaranteed to not collide with anything because it's in the
 * VK_KHR_swapchain namespace but not actually used by the extension.
 */
#define VK_STRUCTURE_TYPE_WSI_IMAGE_CREATE_INFO_MESA \
   (VkStructureType)1000001002
#define VK_STRUCTURE_TYPE_WSI_MEMORY_ALLOCATE_INFO_MESA \
   (VkStructureType)1000001003
#define VK_STRUCTURE_TYPE_WSI_SURFACE_SUPPORTED_COUNTERS_MESA \
   (VkStructureType)1000001005

#define VK_STRUCTURE_TYPE_WSI_IMAGE_CREATE_INFO_MESA_cast \
   struct wsi_image_create_info
#define VK_STRUCTURE_TYPE_WSI_MEMORY_ALLOCATE_INFO_MESA_cast \
   struct wsi_memory_allocate_info
#define VK_STRUCTURE_TYPE_WSI_SURFACE_SUPPORTED_COUNTERS_MESA_cast \
   struct wsi_surface_supported_counters

/**
 * Pseudo-extension struct that may be chained into VkRenderingAttachmentInfo
 * to indicate an initial layout for the attachment.  This is only allowed if
 * all of the following conditions are met:
 *
 *    1. VkRenderingAttachmentInfo::loadOp == LOAD_OP_CLEAR
 *
 *    2. VkRenderingInfo::renderArea is the entire image view LOD
 *
 *    3. For 3D image attachments, VkRenderingInfo::viewMask == 0 AND
 *       VkRenderingInfo::layerCount references the entire bound image view
 *       OR VkRenderingInfo::viewMask is dense (no holes) and references the
 *       entire bound image view.  (2D and 2D array images have no such
 *       requirement.)
 *
 * If this struct is included in the pNext chain of a
 * VkRenderingAttachmentInfo, the driver is responsible for transitioning the
 * bound region of the image from
 * VkRenderingAttachmentInitialLayoutInfoMESA::initialLayout to
 * VkRenderingAttachmentInfo::imageLayout prior to rendering.
 */
typedef struct VkRenderingAttachmentInitialLayoutInfoMESA {
    VkStructureType    sType;
    const void*        pNext;

    /** Initial layout of the attachment */
    VkImageLayout      initialLayout;
} VkRenderingAttachmentInitialLayoutInfoMESA;

#define VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INITIAL_LAYOUT_INFO_MESA \
   (VkStructureType)1000044901
#define VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INITIAL_LAYOUT_INFO_MESA_cast \
   VkRenderingAttachmentInitialLayoutInfoMESA


struct nir_shader;

typedef struct VkPipelineShaderStageNirCreateInfoMESA {
   VkStructureType sType;
   const void *pNext;
   struct nir_shader *nir;
} VkPipelineShaderStageNirCreateInfoMESA;

#define VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_NIR_CREATE_INFO_MESA \
   (VkStructureType)1000290001

#define VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_NIR_CREATE_INFO_MESA_cast \
   VkPipelineShaderStageNirCreateInfoMESA


static const VkPipelineCreateFlagBits2
   VK_PIPELINE_CREATE_2_UNALIGNED_DISPATCH_BIT_MESA = 0x20000000000ull;


#define VK_SHADER_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_MESA 0x1000
#define VK_SHADER_CREATE_UNALIGNED_DISPATCH_BIT_MESA               0x2000

#ifdef __cplusplus
}
#endif

#endif /* VK_INTERNAL_EXTS_H */
