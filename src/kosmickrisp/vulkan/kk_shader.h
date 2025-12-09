/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_SHADER_H
#define KK_SHADER_H 1

#include "kk_device_memory.h"
#include "kk_private.h"

#include "vk_pipeline_cache.h"

#include "vk_shader.h"

struct kk_shader_info {
   mesa_shader_stage stage;
   union {
      struct {
         uint32_t attribs_read;
      } vs;

      struct {
         struct mtl_size local_size;
      } cs;
   };
};

struct kk_shader {
   struct vk_shader vk;
   const char *entrypoint_name;
   const char *msl_code;

   struct kk_shader_info info;

   /* Pipeline resources. Only stored in compute or vertex shaders */
   struct {
      union {
         struct {
            mtl_render_pipeline_state *handle;
            mtl_depth_stencil_state *mtl_depth_stencil_state_handle;
            enum mtl_primitive_type primitive_type;
         } gfx;
         mtl_compute_pipeline_state *cs;
      };
   } pipeline;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(kk_shader, vk.base, VkShaderEXT,
                               VK_OBJECT_TYPE_SHADER_EXT);

extern const struct vk_device_shader_ops kk_device_shader_ops;

static inline nir_address_format
kk_buffer_addr_format(VkPipelineRobustnessBufferBehaviorEXT robustness)
{
   switch (robustness) {
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT:
      return nir_address_format_64bit_global_32bit_offset;
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT:
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT:
      return nir_address_format_64bit_bounded_global;
   default:
      UNREACHABLE("Invalid robust buffer access behavior");
   }
}

bool
kk_nir_lower_descriptors(nir_shader *nir,
                         const struct vk_pipeline_robustness_state *rs,
                         uint32_t set_layout_count,
                         struct vk_descriptor_set_layout *const *set_layouts);

bool kk_nir_lower_textures(nir_shader *nir);

bool kk_nir_lower_vs_multiview(nir_shader *nir, uint32_t view_mask);
bool kk_nir_lower_fs_multiview(nir_shader *nir, uint32_t view_mask);

VkResult kk_compile_nir_shader(struct kk_device *dev, nir_shader *nir,
                               const VkAllocationCallbacks *alloc,
                               struct kk_shader **shader_out);

#endif /* KK_SHADER_H */
