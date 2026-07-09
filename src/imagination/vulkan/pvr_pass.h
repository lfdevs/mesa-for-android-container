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

#ifndef PVR_PASS_H
#define PVR_PASS_H

#include "vk_object.h"

#include "pvr_common.h"

struct pvr_device;

struct pvr_render_pass_attachment {
   /* Saved information from pCreateInfo. */
   VkAttachmentLoadOp load_op;

   VkAttachmentStoreOp store_op;

   VkResolveModeFlagBits resolve_mode;

   VkAttachmentLoadOp stencil_load_op;

   VkAttachmentStoreOp stencil_store_op;

   VkResolveModeFlagBits stencil_resolve_mode;

   VkFormat vk_format;
   uint32_t sample_count;

   /* Derived and other state. */
   VkImageAspectFlags aspects;

   /* Can this surface be resolved by the PBE. */
   bool is_pbe_downscalable;
   bool is_depth;
   bool is_stencil;
   bool need_eot;

   uint32_t resolve_target;
   uint32_t index;
};

struct pvr_render_attachment {
   uint32_t attachment_idx;
   VkImageAspectFlags aspect_mask;
};

struct pvr_render_subpass {
   /* Saved information from pCreateInfo. */
   /* The number of samples per color attachment (or depth attachment if
    * z-only).
    */
   /* FIXME: rename to 'samples' to match struct pvr_image */
   uint32_t sample_count;

   uint32_t color_count;
   uint32_t *color_attachments;
   uint32_t *resolve_attachments;

   uint32_t input_count;
   struct pvr_render_attachment *input_attachments;

   uint32_t preserve_count;
   struct pvr_render_attachment *preserve_attachments;

   uint32_t depth_stencil_attachment;

   uint32_t depth_stencil_resolve_attachment;
   VkResolveModeFlagBits depth_resolve_mode;
   VkResolveModeFlagBits stencil_resolve_mode;

   /*  Derived and other state. */
   uint32_t dep_count;
   uint32_t *dep_list;

   /* Array with dep_count elements. flush_on_dep[x] is true if this subpass
    * and the subpass dep_list[x] can't be in the same hardware render.
    */
   bool *flush_on_dep;

   uint32_t index;

   uint32_t isp_userpass;

   VkPipelineBindPoint pipeline_bind_point;

   /* View mask for multiview. */
   uint32_t view_mask;
};

struct pvr_render_pass {
   struct vk_object_base base;

   /* Saved information from pCreateInfo. */
   uint32_t attachment_count;

   struct pvr_render_pass_attachment *attachments;

   uint32_t subpass_count;

   struct pvr_render_subpass *subpasses;

   struct pvr_renderpass_hwsetup *hw_setup;

   /*  Derived and other state. */
   /* FIXME: rename to 'max_samples' as we use 'samples' elsewhere */
   uint32_t max_sample_count;

   /* The maximum number of tile buffers to use in any subpass. */
   uint32_t max_tilebuffer_count;

   /* VkSubpassDescription2::viewMask or 1 when non-multiview
    *
    * To determine whether multiview is enabled, check
    * pvr_render_pass::multiview_enabled.
    */
   bool multiview_enabled;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_render_pass,
                               base,
                               VkRenderPass,
                               VK_OBJECT_TYPE_RENDER_PASS)

#endif /* PVR_PASS */
