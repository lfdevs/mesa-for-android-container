/*
 * Copyright © 2025 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PVR_MRT_H
#define PVR_MRT_H

#include <stdbool.h>
#include <stdint.h>

#include <vulkan/vulkan.h>

#include "pvr_common.h"
#include "pvr_macros.h"

struct pvr_device;
struct pvr_dynamic_render_info;
struct pvr_cmd_buffer;

/* Specifies the location of render target writes. */
enum usc_mrt_resource_type {
   USC_MRT_RESOURCE_TYPE_INVALID = 0, /* explicitly treat 0 as invalid. */
   USC_MRT_RESOURCE_TYPE_OUTPUT_REG,
   USC_MRT_RESOURCE_TYPE_MEMORY,
};

#define PVR_USC_RENDER_TARGET_MAXIMUM_SIZE_IN_DWORDS (4)

struct usc_mrt_desc {
   /* Size (in bytes) of the intermediate storage required for each pixel in the
    * render target.
    */
   uint32_t intermediate_size;

   /* Mask of the bits from each dword which are read by the PBE. */
   uint32_t valid_mask[PVR_USC_RENDER_TARGET_MAXIMUM_SIZE_IN_DWORDS];

   /* Higher number = higher priority. Used to decide which render targets get
    * allocated dedicated output registers.
    */
   uint32_t priority;
};

struct usc_mrt_resource {
   /* Input description of render target. */
   struct usc_mrt_desc mrt_desc;

   /* Resource type allocated for render target. */
   enum usc_mrt_resource_type type;

   /* Intermediate pixel size (in bytes). */
   uint32_t intermediate_size;

   union {
      /* If type == USC_MRT_RESOURCE_TYPE_OUTPUT_REG. */
      struct {
         /* The output register to use. */
         uint32_t output_reg;

         /* The offset in bytes into the output register. */
         uint32_t offset;
      } reg;

      /* If type == USC_MRT_RESOURCE_TYPE_MEMORY. */
      struct {
         /* The index of the tile buffer to use. */
         uint32_t tile_buffer;

         /* The offset in dwords within the tile buffer. */
         uint32_t offset_dw;
      } mem;
   };
};

struct usc_mrt_setup {
   /* Number of render targets present. */
   uint32_t num_render_targets;

   /* Number of output registers used per-pixel (1, 2 or 4). */
   uint32_t num_output_regs;

   /* Number of tile buffers used. */
   uint32_t num_tile_buffers;

   /* Array of MRT resources allocated for each render target. The number of
    * elements is determined by usc_mrt_setup::num_render_targets.
    */
   struct usc_mrt_resource *mrt_resources;

   /* Don't set up source pos in emit. */
   bool disable_source_pos_override;

   /* Hash unique to this particular setup. */
   uint32_t hash;
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
      uint16_t rt_2d_view_3d_mask;

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

struct pvr_load_op_state {
   uint32_t load_op_count;

   /* Load op array indexed by HW render view (not by the index in the view
    * mask).
    */
   struct pvr_load_op *load_ops;
};

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

#ifdef PVR_PER_ARCH
VkResult PVR_PER_ARCH(mrt_setup_partial_init)(
   struct pvr_device *const device,
   struct usc_mrt_setup *mrt_setup,
   uint32_t num_renger_targets,
   uint32_t num_output_regs,
   uint32_t num_tile_buffers);

#   define pvr_arch_mrt_setup_partial_init PVR_PER_ARCH(mrt_setup_partial_init)

VkResult PVR_PER_ARCH(init_usc_mrt_setup)(
   struct pvr_device *device,
   uint32_t attachment_count,
   const VkFormat attachment_formats[attachment_count],
   struct usc_mrt_setup *setup);

#   define pvr_arch_init_usc_mrt_setup PVR_PER_ARCH(init_usc_mrt_setup)

void PVR_PER_ARCH(destroy_mrt_setup)(const struct pvr_device *device,
                                     struct usc_mrt_setup *setup);

#   define pvr_arch_destroy_mrt_setup PVR_PER_ARCH(destroy_mrt_setup)

void PVR_PER_ARCH(init_mrt_desc)(VkFormat format, struct usc_mrt_desc *desc);

#   define pvr_arch_init_mrt_desc PVR_PER_ARCH(init_mrt_desc)

VkResult PVR_PER_ARCH(pds_unitex_state_program_create_and_upload)(
   struct pvr_device *device,
   const VkAllocationCallbacks *allocator,
   uint32_t texture_kicks,
   uint32_t uniform_kicks,
   struct pvr_pds_upload *const pds_upload_out);

#   define pvr_arch_pds_unitex_state_program_create_and_upload \
      PVR_PER_ARCH(pds_unitex_state_program_create_and_upload)

VkResult
   PVR_PER_ARCH(load_op_shader_generate)(struct pvr_device *device,
                                         const VkAllocationCallbacks *allocator,
                                         struct pvr_load_op *load_op);

#   define pvr_arch_load_op_shader_generate \
      PVR_PER_ARCH(load_op_shader_generate)

VkResult PVR_PER_ARCH(mrt_load_ops_setup)(struct pvr_cmd_buffer *cmd_buffer,
                                          const VkAllocationCallbacks *alloc,
                                          struct pvr_load_op_state **state);

#   define pvr_arch_mrt_load_ops_setup PVR_PER_ARCH(mrt_load_ops_setup)

void PVR_PER_ARCH(mrt_load_op_state_cleanup)(const struct pvr_device *device,
                                             const VkAllocationCallbacks *alloc,
                                             struct pvr_load_op_state *state);

#   define pvr_arch_mrt_load_op_state_cleanup \
      PVR_PER_ARCH(mrt_load_op_state_cleanup)

#endif /* PVR_PER_ARCH */

#endif /* PVR_MRT_H */
