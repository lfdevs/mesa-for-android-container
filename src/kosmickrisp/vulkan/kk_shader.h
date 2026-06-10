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

struct kk_tess_info {
   enum tess_primitive_mode mode : 8;
   enum gl_tess_spacing spacing  : 8;
   bool points;
   bool ccw;
};
static_assert(sizeof(struct kk_tess_info) == 4, "packed");

static struct kk_tess_info
kk_tess_info_merge(struct kk_tess_info a, struct kk_tess_info b)
{
   static_assert(TESS_PRIMITIVE_UNSPECIFIED == 0, "zero state");
   static_assert(TESS_SPACING_UNSPECIFIED == 0, "zero state");

   /* Just merge by OR'ing the raw bits */
   uint32_t x, y;
   memcpy(&x, &a, sizeof(x));
   memcpy(&y, &b, sizeof(y));

   x |= y;

   struct kk_tess_info out;
   memcpy(&out, &x, sizeof(out));
   return out;
}

struct kk_shader_info {
   mesa_shader_stage stage;
   bool uses_per_draw_data;

   /* Required for fragment shader cull distance discards. */
   uint8_t num_cull_distances;

   union {
      /* Vertex shader is the pipeline, store all relevant data here. */
      struct {
         /* Required to know which extra states does the pipeline contain for
          * serialization. */
         uint32_t additional_stages_bits;

         uint32_t tess_local_thread_size;

         /* Data needed to start render pass and bind pipeline. */
         uint32_t attribs_read;
         uint32_t sample_count;
         uint64_t outputs_written;

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
         uint64_t tcs_per_vertex_outputs;
         uint32_t tcs_output_stride;
         uint8_t tcs_output_patch_size;
         uint8_t tcs_nr_patch_outputs;

         struct kk_tess_info info;
      } tess;

      struct {
         struct mtl_size local_size;
      } cs;
   };
};

/* Metal handles for binding. */
struct kk_pipeline_handles {
   union {
      struct {
         mtl_render_pipeline_state *render;
         /* Vertex, tess ctrl and tess eval at most needed before pre-render. */
         mtl_compute_pipeline_state *pre_render[3];
         mtl_depth_stencil_state *ds_handle;
         uint32_t pre_render_count;
      } gfx;
      mtl_compute_pipeline_state *cs;
   };
};

struct kk_shader {
   struct vk_shader vk;

   struct kk_pipeline_handles pipeline;
   struct kk_shader_info info;
   char *msl_shaders[MESA_SHADER_STAGES];
   char *entrypoint_names[MESA_SHADER_STAGES];
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

bool kk_nir_lower_poly(struct nir_shader *nir);
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
