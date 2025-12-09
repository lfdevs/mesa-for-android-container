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

#ifndef PVR_FRAMEBUFFER_H
#define PVR_FRAMEBUFFER_H

struct pvr_render_target {
   struct pvr_rt_dataset *rt_dataset[PVR_MAX_MULTIVIEW];

   pthread_mutex_t mutex;

   uint32_t valid_mask;
};

struct pvr_framebuffer {
   struct vk_object_base base;

   /* Saved information from pCreateInfo. */
   uint32_t width;
   uint32_t height;
   uint32_t layers;

   uint32_t attachment_count;
   struct pvr_image_view **attachments;

   /* Derived and other state. */
   struct pvr_suballoc_bo *ppp_state_bo;
   /* PPP state size in dwords. */
   size_t ppp_state_size;

   uint32_t render_targets_count;
   struct pvr_render_target *render_targets;

   struct pvr_spm_scratch_buffer *scratch_buffer;

   uint32_t render_count;
   struct pvr_spm_eot_state *spm_eot_state_per_render;
   struct pvr_spm_bgobj_state *spm_bgobj_state_per_render;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_framebuffer,
                               base,
                               VkFramebuffer,
                               VK_OBJECT_TYPE_FRAMEBUFFER)

#endif /* PVR_FRAMEBUFFER_H */
