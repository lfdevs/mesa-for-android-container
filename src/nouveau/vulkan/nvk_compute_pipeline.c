/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_private.h"
#include "nvk_device.h"
#include "nvk_physical_device.h"
#include "nvk_pipeline.h"
#include "nvk_shader.h"

#include "vk_nir.h"
#include "vk_pipeline.h"
#include "vk_pipeline_layout.h"

#include "nouveau_bo.h"
#include "nouveau_context.h"

#include "compiler/spirv/nir_spirv.h"

#include "drf.h"
#include "cla0c0.h"
#include "cla0c0qmd.h"
#include "clc0c0.h"
#include "clc0c0qmd.h"
#include "clc3c0.h"
#include "clc3c0qmd.h"
#include "clc6c0.h"
#include "clc6c0qmd.h"
#define NVA0C0_QMDV00_06_VAL_SET(p,a...) NVVAL_MW_SET((p), NVA0C0, QMDV00_06, ##a)
#define NVA0C0_QMDV00_06_DEF_SET(p,a...) NVDEF_MW_SET((p), NVA0C0, QMDV00_06, ##a)
#define NVC0C0_QMDV02_01_VAL_SET(p,a...) NVVAL_MW_SET((p), NVC0C0, QMDV02_01, ##a)
#define NVC0C0_QMDV02_01_DEF_SET(p,a...) NVDEF_MW_SET((p), NVC0C0, QMDV02_01, ##a)
#define NVC3C0_QMDV02_02_VAL_SET(p,a...) NVVAL_MW_SET((p), NVC3C0, QMDV02_02, ##a)
#define NVC3C0_QMDV02_02_DEF_SET(p,a...) NVDEF_MW_SET((p), NVC3C0, QMDV02_02, ##a)
#define NVC6C0_QMDV03_00_VAL_SET(p,a...) NVVAL_MW_SET((p), NVC6C0, QMDV03_00, ##a)
#define NVC6C0_QMDV03_00_DEF_SET(p,a...) NVDEF_MW_SET((p), NVC6C0, QMDV03_00, ##a)

#define QMD_DEF_SET(qmd, class_id, version_major, version_minor, a...) \
   NVDEF_MW_SET((qmd), NV##class_id, QMDV##version_major##_##version_minor, ##a)
#define QMD_VAL_SET(qmd, class_id, version_major, version_minor, a...) \
   NVVAL_MW_SET((qmd), NV##class_id, QMDV##version_major##_##version_minor, ##a)

static int
gv100_sm_config_smem_size(uint32_t size)
{
   if      (size > 64 * 1024) size = 96 * 1024;
   else if (size > 32 * 1024) size = 64 * 1024;
   else if (size > 16 * 1024) size = 32 * 1024;
   else if (size >  8 * 1024) size = 16 * 1024;
   else                       size =  8 * 1024;
   return (size / 4096) + 1;
}

#define base_compute_setup_launch_desc_template(qmd, shader, class_id, version_major, version_minor)   \
do {                                                                                                   \
   QMD_DEF_SET(qmd, class_id, version_major, version_minor, API_VISIBLE_CALL_LIMIT, NO_CHECK);         \
   QMD_VAL_SET(qmd, class_id, version_major, version_minor, BARRIER_COUNT, shader->num_barriers);      \
   QMD_VAL_SET(qmd, class_id, version_major, version_minor, CTA_THREAD_DIMENSION0,                     \
                                                            shader->cp.block_size[0]);                 \
   QMD_VAL_SET(qmd, class_id, version_major, version_minor, CTA_THREAD_DIMENSION1,                     \
                                                            shader->cp.block_size[1]);                 \
   QMD_VAL_SET(qmd, class_id, version_major, version_minor, CTA_THREAD_DIMENSION2,                     \
                                                            shader->cp.block_size[2]);                 \
   QMD_VAL_SET(qmd, class_id, version_major, version_minor, QMD_MAJOR_VERSION, version_major);         \
   QMD_VAL_SET(qmd, class_id, version_major, version_minor, QMD_VERSION, version_minor);               \
   QMD_DEF_SET(qmd, class_id, version_major, version_minor, SAMPLER_INDEX, INDEPENDENTLY);             \
   QMD_VAL_SET(qmd, class_id, version_major, version_minor, SHADER_LOCAL_MEMORY_HIGH_SIZE, 0);         \
   QMD_VAL_SET(qmd, class_id, version_major, version_minor, SHADER_LOCAL_MEMORY_LOW_SIZE,              \
                                                            align(shader->slm_size, 0x10));            \
   QMD_VAL_SET(qmd, class_id, version_major, version_minor, SHARED_MEMORY_SIZE,                        \
                                                            align(shader->cp.smem_size, 0x100));       \
} while (0)

static void
nva0c0_compute_setup_launch_desc_template(uint32_t *qmd,
                                          struct nvk_shader *shader)
{
   base_compute_setup_launch_desc_template(qmd, shader, A0C0, 00, 06);

   NVA0C0_QMDV00_06_DEF_SET(qmd, INVALIDATE_TEXTURE_DATA_CACHE, TRUE);
   NVA0C0_QMDV00_06_DEF_SET(qmd, INVALIDATE_TEXTURE_HEADER_CACHE, TRUE);
   NVA0C0_QMDV00_06_DEF_SET(qmd, INVALIDATE_TEXTURE_SAMPLER_CACHE, TRUE);
   NVA0C0_QMDV00_06_DEF_SET(qmd, INVALIDATE_SHADER_CONSTANT_CACHE, TRUE);
   NVA0C0_QMDV00_06_DEF_SET(qmd, INVALIDATE_SHADER_DATA_CACHE, TRUE);

   if (shader->cp.smem_size <= (16 << 10))
      NVA0C0_QMDV00_06_DEF_SET(qmd, L1_CONFIGURATION, DIRECTLY_ADDRESSABLE_MEMORY_SIZE_16KB);
   else if (shader->cp.smem_size <= (32 << 10))
      NVA0C0_QMDV00_06_DEF_SET(qmd, L1_CONFIGURATION, DIRECTLY_ADDRESSABLE_MEMORY_SIZE_32KB);
   else if (shader->cp.smem_size <= (48 << 10))
      NVA0C0_QMDV00_06_DEF_SET(qmd, L1_CONFIGURATION, DIRECTLY_ADDRESSABLE_MEMORY_SIZE_48KB);
   else
      unreachable("Invalid shared memory size");

   uint64_t addr = nvk_shader_address(shader);
   assert(addr < 0xffffffff);
   NVA0C0_QMDV00_06_VAL_SET(qmd, PROGRAM_OFFSET, addr);
   NVA0C0_QMDV00_06_VAL_SET(qmd, REGISTER_COUNT, shader->num_gprs);
   NVA0C0_QMDV00_06_VAL_SET(qmd, SASS_VERSION, 0x30);
}

static void
nvc0c0_compute_setup_launch_desc_template(uint32_t *qmd,
                                          struct nvk_shader *shader)
{
   base_compute_setup_launch_desc_template(qmd, shader, C0C0, 02, 01);

   uint64_t addr = nvk_shader_address(shader);
   assert(addr < 0xffffffff);

   NVC0C0_QMDV02_01_VAL_SET(qmd, SM_GLOBAL_CACHING_ENABLE, 1);
   NVC0C0_QMDV02_01_VAL_SET(qmd, PROGRAM_OFFSET, addr);
   NVC0C0_QMDV02_01_VAL_SET(qmd, REGISTER_COUNT, shader->num_gprs);
}

static void
nvc3c0_compute_setup_launch_desc_template(uint32_t *qmd,
                                          struct nvk_shader *shader)
{
   base_compute_setup_launch_desc_template(qmd, shader, C3C0, 02, 02);

   NVC3C0_QMDV02_02_VAL_SET(qmd, SM_GLOBAL_CACHING_ENABLE, 1);
   /* those are all QMD 2.2+ */
   NVC3C0_QMDV02_02_VAL_SET(qmd, MIN_SM_CONFIG_SHARED_MEM_SIZE,
                            gv100_sm_config_smem_size(8 * 1024));
   NVC3C0_QMDV02_02_VAL_SET(qmd, MAX_SM_CONFIG_SHARED_MEM_SIZE,
                            gv100_sm_config_smem_size(96 * 1024));
   NVC3C0_QMDV02_02_VAL_SET(qmd, TARGET_SM_CONFIG_SHARED_MEM_SIZE,
                            gv100_sm_config_smem_size(shader->cp.smem_size));

   NVC3C0_QMDV02_02_VAL_SET(qmd, REGISTER_COUNT_V, shader->num_gprs);

   uint64_t addr = nvk_shader_address(shader);
   NVC3C0_QMDV02_02_VAL_SET(qmd, PROGRAM_ADDRESS_LOWER, addr & 0xffffffff);
   NVC3C0_QMDV02_02_VAL_SET(qmd, PROGRAM_ADDRESS_UPPER, addr >> 32);
}

static void
nvc6c0_compute_setup_launch_desc_template(uint32_t *qmd,
                                          struct nvk_shader *shader)
{
   base_compute_setup_launch_desc_template(qmd, shader, C6C0, 03, 00);

   NVC6C0_QMDV03_00_VAL_SET(qmd, SM_GLOBAL_CACHING_ENABLE, 1);
   /* those are all QMD 2.2+ */
   NVC6C0_QMDV03_00_VAL_SET(qmd, MIN_SM_CONFIG_SHARED_MEM_SIZE,
                            gv100_sm_config_smem_size(8 * 1024));
   NVC6C0_QMDV03_00_VAL_SET(qmd, MAX_SM_CONFIG_SHARED_MEM_SIZE,
                            gv100_sm_config_smem_size(96 * 1024));
   NVC6C0_QMDV03_00_VAL_SET(qmd, TARGET_SM_CONFIG_SHARED_MEM_SIZE,
                            gv100_sm_config_smem_size(shader->cp.smem_size));

   NVC6C0_QMDV03_00_VAL_SET(qmd, REGISTER_COUNT_V, shader->num_gprs);

   uint64_t addr = nvk_shader_address(shader);
   NVC6C0_QMDV03_00_VAL_SET(qmd, PROGRAM_ADDRESS_LOWER, addr & 0xffffffff);
   NVC6C0_QMDV03_00_VAL_SET(qmd, PROGRAM_ADDRESS_UPPER, addr >> 32);
}

VkResult
nvk_compute_pipeline_create(struct nvk_device *dev,
                            struct vk_pipeline_cache *cache,
                            const VkComputePipelineCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkPipeline *pPipeline)
{
   VK_FROM_HANDLE(vk_pipeline_layout, pipeline_layout, pCreateInfo->layout);
   struct nvk_physical_device *pdev = nvk_device_physical(dev);
   struct nvk_compute_pipeline *pipeline;
   VkResult result;

   pipeline = (void *)nvk_pipeline_zalloc(dev, NVK_PIPELINE_COMPUTE,
                                          sizeof(*pipeline), pAllocator);
   if (pipeline == NULL)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   assert(pCreateInfo->stage.stage == VK_SHADER_STAGE_COMPUTE_BIT);

   struct vk_pipeline_robustness_state robustness;
   vk_pipeline_robustness_state_fill(&dev->vk, &robustness,
                                     pCreateInfo->pNext,
                                     pCreateInfo->stage.pNext);

   nir_shader *nir;
   result = nvk_shader_stage_to_nir(dev, &pCreateInfo->stage, &robustness,
                                    cache, NULL, &nir);
   if (result != VK_SUCCESS)
      goto fail;

   nvk_lower_nir(dev, nir, &robustness, false, pipeline_layout);

   result = nvk_compile_nir(pdev, nir, NULL,
                            &pipeline->base.shaders[MESA_SHADER_COMPUTE]);
   ralloc_free(nir);
   if (result != VK_SUCCESS)
      goto fail;

   result = nvk_shader_upload(dev,
                              &pipeline->base.shaders[MESA_SHADER_COMPUTE]);
   if (result != VK_SUCCESS)
      goto fail;

   struct nvk_shader *shader = &pipeline->base.shaders[MESA_SHADER_COMPUTE];
   if (pdev->info.cls_compute >= AMPERE_COMPUTE_A)
      nvc6c0_compute_setup_launch_desc_template(pipeline->qmd_template, shader);
   else if (pdev->info.cls_compute >= VOLTA_COMPUTE_A)
      nvc3c0_compute_setup_launch_desc_template(pipeline->qmd_template, shader);
   else if (pdev->info.cls_compute >= PASCAL_COMPUTE_A)
      nvc0c0_compute_setup_launch_desc_template(pipeline->qmd_template, shader);
   else if (pdev->info.cls_compute >= KEPLER_COMPUTE_A)
      nva0c0_compute_setup_launch_desc_template(pipeline->qmd_template, shader);
   else
      unreachable("Fermi and older not supported!");

   *pPipeline = nvk_pipeline_to_handle(&pipeline->base);
   return VK_SUCCESS;

fail:
   nvk_pipeline_free(dev, &pipeline->base, pAllocator);
   return result;
}
