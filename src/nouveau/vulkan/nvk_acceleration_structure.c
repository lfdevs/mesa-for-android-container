/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#include "nvk_entrypoints.h"

VKAPI_ATTR VkResult VKAPI_CALL
nvk_GetAccelerationStructureOpaqueCaptureDescriptorDataEXT(
    VkDevice device,
    const VkAccelerationStructureCaptureDescriptorDataInfoEXT *pInfo,
    void *pData)
{
   return VK_SUCCESS;
}
