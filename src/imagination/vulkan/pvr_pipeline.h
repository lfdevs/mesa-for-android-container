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

#ifndef PVR_PIPELINE_H
#define PVR_PIPELINE_H

#include "vk_object.h"
#include "vk_graphics_state.h"

#include "pvr_common.h"
#include "pvr_csb.h"
#include "pvr_pds.h"

struct pvr_suballoc_bo;

struct pvr_pipeline_stage_state {
   uint32_t pds_temps_count;
};

struct pvr_compute_shader_state {
   /* Pointer to a buffer object that contains the shader binary. */
   struct pvr_suballoc_bo *shader_bo;

   /* Buffer object for the coefficient update shader binary. */
   struct pvr_suballoc_bo *coeff_update_shader_bo;
   uint32_t coeff_update_shader_temps;
};

struct pvr_pds_attrib_program {
   struct pvr_pds_info info;
   /* The uploaded PDS program stored here only contains the code segment,
    * meaning the data size will be 0, unlike the data size stored in the
    * 'info' member above.
    */
   struct pvr_pds_upload program;
};

struct pvr_stage_allocation_descriptor_state {
   struct pvr_pds_upload pds_code;
   /* Since we upload the code segment separately from the data segment
    * pds_code->data_size might be 0 whilst
    * pds_info->data_size_in_dwords might be >0 in the case of this struct
    * referring to the code upload.
    */
   struct pvr_pds_info pds_info;

   /* Already setup compile time static consts. */
   struct pvr_suballoc_bo *static_consts;
};

struct pvr_vertex_shader_state {
   /* Pointer to a buffer object that contains the shader binary. */
   struct pvr_suballoc_bo *shader_bo;

   struct pvr_pds_attrib_program
      pds_attrib_programs[PVR_PDS_VERTEX_ATTRIB_PROGRAM_COUNT];

   struct pvr_pipeline_stage_state stage_state;
   /* FIXME: Move this into stage_state? */
   struct pvr_stage_allocation_descriptor_state descriptor_state;
};

struct pvr_fragment_shader_state {
   /* Pointer to a buffer object that contains the shader binary. */
   struct pvr_suballoc_bo *shader_bo;

   struct pvr_pipeline_stage_state stage_state;
   /* FIXME: Move this into stage_state? */
   struct pvr_stage_allocation_descriptor_state descriptor_state;
   enum ROGUE_TA_PASSTYPE pass_type;

   struct pvr_pds_coeff_loading_program pds_coeff_program;
   uint32_t *pds_coeff_program_buffer;

   struct pvr_pds_kickusc_program pds_fragment_program;
   uint32_t *pds_fragment_program_buffer;
};

struct pvr_pipeline {
   struct vk_object_base base;
   enum pvr_pipeline_type type;
   struct vk_pipeline_layout *layout;
   VkPipelineCreateFlags2KHR pipeline_flags;
};

struct pvr_compute_pipeline {
   struct pvr_pipeline base;

   pco_data cs_data;

   struct pvr_compute_shader_state shader_state;
   struct pvr_stage_allocation_descriptor_state descriptor_state;

   struct pvr_pds_upload pds_cs_program;
   struct pvr_pds_info pds_cs_program_info;

   uint32_t *pds_cs_data_section;
   uint32_t base_workgroup_data_patching_offset;
   uint32_t num_workgroups_data_patching_offset;
   uint32_t num_workgroups_indirect_src_patching_offset;
   uint32_t num_workgroups_indirect_src_dma_patching_offset;
};

struct pvr_graphics_pipeline {
   struct pvr_pipeline base;

   struct vk_dynamic_graphics_state dynamic_state;

   /* Derived and other state */
   size_t stage_indices[MESA_SHADER_STAGES];

   pco_data vs_data;
   pco_data fs_data;

   struct {
      struct pvr_vertex_shader_state vertex;
      struct pvr_fragment_shader_state fragment;
   } shader_state;
};

struct pvr_private_compute_pipeline {
   /* Used by pvr_compute_update_kernel_private(). */
   uint32_t pds_code_offset;
   uint32_t pds_data_offset;
   uint32_t pds_data_size_dw;
   uint32_t pds_temps_used;
   uint32_t coeff_regs_count;
   uint32_t unified_store_regs_count;
   VkExtent3D workgroup_size;

   /* Used by pvr_compute_update_shared_private(). */
   uint32_t pds_shared_update_code_offset;
   uint32_t pds_shared_update_data_offset;
   uint32_t pds_shared_update_data_size_dw;

   /* Used by both pvr_compute_update_{kernel,shared}_private(). */
   uint32_t const_shared_regs_count;

   pvr_dev_addr_t const_buffer_addr;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_pipeline,
                               base,
                               VkPipeline,
                               VK_OBJECT_TYPE_PIPELINE)

static inline struct pvr_compute_pipeline *
to_pvr_compute_pipeline(struct pvr_pipeline *pipeline)
{
   assert(pipeline->type == PVR_PIPELINE_TYPE_COMPUTE);
   return container_of(pipeline, struct pvr_compute_pipeline, base);
}

static inline struct pvr_graphics_pipeline *
to_pvr_graphics_pipeline(struct pvr_pipeline *pipeline)
{
   assert(pipeline->type == PVR_PIPELINE_TYPE_GRAPHICS);
   return container_of(pipeline, struct pvr_graphics_pipeline, base);
}

static enum pvr_pipeline_stage_bits
pvr_stage_mask(VkPipelineStageFlags2 stage_mask)
{
   enum pvr_pipeline_stage_bits stages = 0;

   if (stage_mask & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)
      return PVR_PIPELINE_STAGE_ALL_BITS;

   if (stage_mask & (VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT))
      stages |= PVR_PIPELINE_STAGE_ALL_GRAPHICS_BITS;

   if (stage_mask & (VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                     VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                     VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                     VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
                     VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
                     VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT)) {
      stages |= PVR_PIPELINE_STAGE_GEOM_BIT;
   }

   if (stage_mask & (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                     VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                     VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT)) {
      stages |= PVR_PIPELINE_STAGE_FRAG_BIT;
   }

   if (stage_mask & (VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)) {
      stages |= PVR_PIPELINE_STAGE_COMPUTE_BIT;
   }

   if (stage_mask & (VK_PIPELINE_STAGE_TRANSFER_BIT))
      stages |= PVR_PIPELINE_STAGE_TRANSFER_BIT;

   return stages;
}

static inline enum pvr_pipeline_stage_bits
pvr_stage_mask_src(VkPipelineStageFlags2 stage_mask)
{
   /* If the source is bottom of pipe, all stages will need to be waited for. */
   if (stage_mask & VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT)
      return PVR_PIPELINE_STAGE_ALL_BITS;

   return pvr_stage_mask(stage_mask);
}

static inline enum pvr_pipeline_stage_bits
pvr_stage_mask_dst(VkPipelineStageFlags2 stage_mask)
{
   /* If the destination is top of pipe, all stages should be blocked by prior
    * commands.
    */
   if (stage_mask & VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)
      return PVR_PIPELINE_STAGE_ALL_BITS;

   return pvr_stage_mask(stage_mask);
}

size_t pvr_pds_get_max_descriptor_upload_const_map_size_in_bytes(void);

#endif /* PVR_PIPELINE_H */
