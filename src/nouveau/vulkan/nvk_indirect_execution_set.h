/*
 * Copyright © 2024 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_INDIRECT_EXECUTION_SET
#define NVK_INDIRECT_EXECUTION_SET 1

#include "nvk_private.h"

struct nvk_physical_device;
struct nvk_shader;
struct nvkmd_mem;

enum nvk_ies_type {
   NVK_IES_TYPE_CS_QMD,
   NVK_IES_TYPE_GFX_SHADER,
   NVK_IES_TYPE_GFX_PIPELINE,
};

PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_ERROR(-Wpadded)
struct nvk_ies_cs_qmd {
   uint32_t qmd[64];
};
PRAGMA_DIAGNOSTIC_POP

static inline uint16_t
nvk_ies_cs_qmd_max_dw_count(struct nvk_physical_device *pdev)
{
   return 64;
}

void nvk_ies_cs_qmd_init(struct nvk_physical_device *pdev,
                         struct nvk_ies_cs_qmd *qmd,
                         struct nvk_shader *shader);

PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_ERROR(-Wpadded)
struct nvk_ies_gfx_shader {
   uint16_t dw_count;
   uint16_t vtgm_dw_count;
   uint32_t push[0];
};
PRAGMA_DIAGNOSTIC_POP
static_assert(sizeof(struct nvk_ies_gfx_shader) == 4,
              "nvk_ies_gfx_shader has no holes");

uint16_t nvk_ies_gfx_shader_max_dw_count(struct nvk_physical_device *pdev,
                                         VkShaderStageFlags stages,
                                         bool last_vtgm);

PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_ERROR(-Wpadded)
struct nvk_ies_gfx_pipeline {
   uint32_t dw_count;
   uint32_t push[0];
};
PRAGMA_DIAGNOSTIC_POP
static_assert(sizeof(struct nvk_ies_gfx_pipeline) == 4,
              "nvk_ies_gfx_pipeline has no holes");

uint16_t nvk_ies_gfx_pipeline_max_dw_count(struct nvk_physical_device *pdev,
                                           VkShaderStageFlags stages);

struct nvk_indirect_execution_set {
   struct vk_object_base base;

   enum nvk_ies_type type;
   uint32_t stride_B;
   uint32_t count;

   struct nvkmd_mem *mem;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(nvk_indirect_execution_set, base,
                               VkIndirectExecutionSetEXT,
                               VK_OBJECT_TYPE_INDIRECT_EXECUTION_SET_EXT);

#endif
