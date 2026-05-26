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

#include "kosmickrisp/bridge/mtl_format.h"

#include "vk_pipeline_cache.h"

#include "vk_shader.h"

struct kk_cmd_buffer;

struct kk_shader_info {
   mesa_shader_stage stage;
   bool uses_per_draw_data;
   union {
      /* Vertex shader is the pipeline, store all relevant data here. */
      struct {
         /* Required for serialization. */
         char *frag_msl_code;
         char *frag_entrypoint_name;

         /* Data needed to start render pass and bind pipeline. */
         uint32_t attribs_read;
         uint32_t sample_count;

         /* Required for fragment shader cull distance discards. */
         uint8_t num_cull_distances;

         /* Data needed for serialization. */
         enum mtl_primitive_topology_class topology;
         enum mtl_pixel_format rt_formats[MAX_DRAW_BUFFERS];
         enum mtl_pixel_format d_format;
         enum mtl_pixel_format s_format;
         uint32_t view_mask;
         VkCompareOp depth_compare_op;
         struct {
            uint8_t depth_fail;
            uint8_t fail;
            uint8_t pass;
            uint8_t compare;
            uint8_t compare_mask;
            uint8_t write_mask;
         } stencil_front, stencil_back;
         uint8_t color_attachment_count;
         bool has_ms;
         bool has_alpha_to_coverage_enabled;
         bool has_alpha_to_one_enabled;
         bool has_ds;
         bool has_depth_write;
         bool has_stencil_test;
      } vs;

      struct {
         struct mtl_size local_size;
      } cs;
   };
};

/* Metal handles for binding. */
struct kk_pipeline_handles {
   union {
      struct {
         mtl_render_pipeline_state *handle;
         mtl_depth_stencil_state *ds_handle;
      } gfx;
      mtl_compute_pipeline_state *cs;
   };
};

struct kk_shader {
   struct vk_shader vk;

   struct kk_pipeline_handles pipeline;
   struct kk_shader_info info;
   const char *entrypoint_name;
   const char *msl_code;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(kk_shader, vk.base, VkShaderEXT,
                               VK_OBJECT_TYPE_SHADER_EXT);

extern const struct vk_device_shader_ops kk_device_shader_ops;

static inline nir_address_format
kk_buffer_addr_format(VkPipelineRobustnessBufferBehaviorEXT robustness)
{
   switch (robustness) {
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED:
      return nir_address_format_64bit_global_32bit_offset;
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS:
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2:
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

bool kk_nir_lower_null_images(nir_shader *nir);

bool kk_nir_lower_textures(nir_shader *nir);

bool kk_nir_lower_vs_multiview(nir_shader *nir, uint32_t view_mask);
bool kk_nir_lower_fs_multiview(nir_shader *nir, uint32_t view_mask);

VkResult kk_compile_nir_shader(struct kk_device *dev, nir_shader *nir,
                               const VkAllocationCallbacks *alloc,
                               struct kk_shader **shader_out);

void kk_cmd_bind_compute_shader(struct kk_cmd_buffer *cmd,
                                struct kk_shader *shader);

#endif /* KK_SHADER_H */
