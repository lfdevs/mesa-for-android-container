/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_SHADER_H
#define NVK_SHADER_H 1

#include "nvk_private.h"
#include "nvk_device_memory.h"

#include "nir.h"
#include "nouveau_bo.h"

struct nvk_device;
struct nvk_physical_device;
struct nvk_pipeline_compilation_ctx;
struct vk_pipeline_cache;
struct vk_pipeline_layout;
struct vk_pipeline_robustness_state;
struct vk_shader_module;

#define GF100_SHADER_HEADER_SIZE (20 * 4)
#define TU102_SHADER_HEADER_SIZE (32 * 4)
#define NVC0_MAX_SHADER_HEADER_SIZE TU102_SHADER_HEADER_SIZE

struct nvk_fs_key {
   bool msaa;
   bool force_per_sample;
   bool zs_self_dep;
};

struct nvk_transform_feedback_state {
   uint32_t stride[4];
   uint8_t stream[4];
   uint8_t varying_count[4];
   uint8_t varying_index[4][128];
};

struct nvk_shader {
   gl_shader_stage stage;

   uint8_t *code_ptr;
   uint32_t code_size;

   uint32_t upload_size;
   uint64_t upload_addr;
   uint32_t upload_padding;

   uint8_t num_gprs;
   uint8_t num_barriers;
   uint32_t slm_size;

   uint32_t hdr[NVC0_MAX_SHADER_HEADER_SIZE/4];
   uint32_t flags[2];

   struct {
      uint32_t clip_mode; /* clip/cull selection */
      uint8_t clip_enable; /* mask of defined clip planes */
      uint8_t cull_enable; /* mask of defined cull distances */
      uint8_t edgeflag; /* attribute index of edgeflag input */
      bool need_vertex_id;
      bool need_draw_parameters;
      bool layer_viewport_relative; /* also applies go gp and tp */
   } vs;

   struct {
      uint8_t early_z;
      uint8_t colors;
      uint8_t color_interp[2];
      bool sample_mask_in;
      bool uses_sample_shading;
      bool force_persample_interp;
      bool flatshade;
      bool reads_framebuffer;
      bool post_depth_coverage;
      bool msaa;
   } fs;

   struct {
      uint32_t domain_type; /* ~0 if params defined by the other stage */
      uint32_t spacing;
      uint32_t output_prims;
   } tp;

   struct {
      uint32_t smem_size; /* shared memory (TGSI LOCAL resource) size */
      uint32_t block_size[3];
   } cp;

   struct nvk_transform_feedback_state *xfb;
};

static inline uint64_t
nvk_shader_address(const struct nvk_shader *shader)
{
   return shader->upload_addr + shader->upload_padding;
}

uint64_t
nvk_physical_device_compiler_flags(const struct nvk_physical_device *pdev);

const nir_shader_compiler_options *
nvk_physical_device_nir_options(const struct nvk_physical_device *pdev,
                                gl_shader_stage stage);

static inline nir_address_format
nvk_buffer_addr_format(VkPipelineRobustnessBufferBehaviorEXT robustness)
{
   switch (robustness) {
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT:
      return nir_address_format_64bit_global_32bit_offset;
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT:
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT:
      return nir_address_format_64bit_bounded_global;
   default:
      unreachable("Invalid robust buffer access behavior");
   }
}

struct spirv_to_nir_options
nvk_physical_device_spirv_options(const struct nvk_physical_device *pdev,
                                  const struct vk_pipeline_robustness_state *rs);

bool
nvk_nir_lower_descriptors(nir_shader *nir,
                          const struct vk_pipeline_robustness_state *rs,
                          const struct vk_pipeline_layout *layout);

VkResult
nvk_shader_stage_to_nir(struct nvk_device *dev,
                        const VkPipelineShaderStageCreateInfo *sinfo,
                        const struct vk_pipeline_robustness_state *rstate,
                        struct vk_pipeline_cache *cache,
                        void *mem_ctx, struct nir_shader **nir_out);

void
nvk_lower_nir(struct nvk_device *dev, nir_shader *nir,
              const struct vk_pipeline_robustness_state *rs,
              bool is_multiview,
              const struct vk_pipeline_layout *layout);

VkResult
nvk_compile_nir(struct nvk_physical_device *dev, nir_shader *nir,
                const struct nvk_fs_key *fs_key,
                struct nvk_shader *shader);

VkResult
nvk_shader_upload(struct nvk_device *dev, struct nvk_shader *shader);

void
nvk_shader_finish(struct nvk_device *dev, struct nvk_shader *shader);
#endif
