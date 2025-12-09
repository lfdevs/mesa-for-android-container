/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * based in part on radv driver which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PVR_BUFFER_H
#define PVR_BUFFER_H

#include "vk_buffer.h"
#include "vk_buffer_view.h"

#include "pvr_common.h"

struct pvr_buffer {
   struct vk_buffer vk;

   /* Derived and other state */
   uint32_t alignment;
   /* vma this buffer is bound to */
   struct pvr_winsys_vma *vma;
   /* Device address the buffer is mapped to in device virtual address space */
   pvr_dev_addr_t dev_addr;
};

#define PVR_BUFFER_VIEW_WIDTH 8192U

struct pvr_buffer_view {
   struct vk_buffer_view vk;

   uint32_t num_rows;

   /* Prepacked Texture dword 0 and 1. It will be copied to the descriptor
    * during pvr_UpdateDescriptorSets().
    */
   struct pvr_image_descriptor image_state;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_buffer,
                               vk.base,
                               VkBuffer,
                               VK_OBJECT_TYPE_BUFFER)

VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_buffer_view,
                               vk.base,
                               VkBufferView,
                               VK_OBJECT_TYPE_BUFFER_VIEW)

#endif /* PVR_BUFFER_H */
