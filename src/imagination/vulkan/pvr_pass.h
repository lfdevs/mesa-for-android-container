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

   VkAttachmentLoadOp stencil_load_op;

   VkAttachmentStoreOp stencil_store_op;

   VkFormat vk_format;
   uint32_t sample_count;
   VkImageLayout initial_layout;

   /* Derived and other state. */
   VkImageAspectFlags aspects;

   /* Can this surface be resolved by the PBE. */
   bool is_pbe_downscalable;

   uint32_t index;
};

struct pvr_render_input_attachment {
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
   struct pvr_render_input_attachment *input_attachments;

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

/* Max render targets for the clears loads state in load op.
 * To account for resolve attachments, double the color attachments.
 */
#define PVR_LOAD_OP_CLEARS_LOADS_MAX_RTS (PVR_MAX_COLOR_ATTACHMENTS * 2)

struct pvr_load_op {
   bool is_hw_object;

   struct pvr_suballoc_bo *usc_frag_prog_bo;
   uint32_t const_shareds_count;
   uint32_t shareds_count;
   uint32_t num_tile_buffers;

   struct pvr_pds_upload pds_frag_prog;

   struct pvr_pds_upload pds_tex_state_prog;
   uint32_t temps_count;

   union {
      const struct pvr_renderpass_hwsetup_render *hw_render;
      const struct pvr_render_subpass *subpass;
   };

   /* TODO: We might not need to keep all of this around. Some stuff might just
    * be for the compiler to ingest which we can then discard.
    */
   struct {
      uint16_t rt_clear_mask;
      uint16_t rt_load_mask;

      uint16_t unresolved_msaa_mask;

      /* The format to write to the output regs. */
      VkFormat dest_vk_format[PVR_LOAD_OP_CLEARS_LOADS_MAX_RTS];

#define PVR_NO_DEPTH_CLEAR_TO_REG (-1)
      /* If >= 0, write a depth clear value to the specified pixel output. */
      int32_t depth_clear_to_reg;

      const struct usc_mrt_setup *mrt_setup;
   } clears_loads_state;

   uint32_t view_indices[PVR_MAX_MULTIVIEW];

   uint32_t view_count;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_render_pass,
                               base,
                               VkRenderPass,
                               VK_OBJECT_TYPE_RENDER_PASS)

#define CHECK_MASK_SIZE(_struct_type, _field_name, _nr_bits)               \
   static_assert(sizeof(((struct _struct_type *)NULL)->_field_name) * 8 >= \
                    _nr_bits,                                              \
                 #_field_name " mask of struct " #_struct_type " too small")

CHECK_MASK_SIZE(pvr_load_op,
                clears_loads_state.rt_clear_mask,
                PVR_LOAD_OP_CLEARS_LOADS_MAX_RTS);
CHECK_MASK_SIZE(pvr_load_op,
                clears_loads_state.rt_load_mask,
                PVR_LOAD_OP_CLEARS_LOADS_MAX_RTS);
CHECK_MASK_SIZE(pvr_load_op,
                clears_loads_state.unresolved_msaa_mask,
                PVR_LOAD_OP_CLEARS_LOADS_MAX_RTS);

#undef CHECK_MASK_SIZE

VkResult pvr_pds_unitex_state_program_create_and_upload(
   struct pvr_device *device,
   const VkAllocationCallbacks *allocator,
   uint32_t texture_kicks,
   uint32_t uniform_kicks,
   struct pvr_pds_upload *const pds_upload_out);

#endif /* PVR_PASS */
