/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#ifndef TU_PIPELINE_H
#define TU_PIPELINE_H

#include "tu_common.h"

#include "tu_cs.h"
#include "tu_descriptor_set.h"
#include "tu_shader.h"
#include "tu_suballoc.h"

enum tu_dynamic_state
{
   /* re-use VK_DYNAMIC_STATE_ enums for non-extended dynamic states */
   TU_DYNAMIC_STATE_SAMPLE_LOCATIONS = VK_DYNAMIC_STATE_STENCIL_REFERENCE + 1,
   TU_DYNAMIC_STATE_SAMPLE_LOCATIONS_ENABLE,
   TU_DYNAMIC_STATE_DS,
   TU_DYNAMIC_STATE_VB_STRIDE,
   TU_DYNAMIC_STATE_PC_RASTER_CNTL,
   TU_DYNAMIC_STATE_BLEND,
   TU_DYNAMIC_STATE_VERTEX_INPUT,
   TU_DYNAMIC_STATE_PATCH_CONTROL_POINTS,
   TU_DYNAMIC_STATE_COUNT,
   /* re-use the line width enum as it uses GRAS_SU_CNTL: */
   TU_DYNAMIC_STATE_RAST = VK_DYNAMIC_STATE_LINE_WIDTH,
};

struct cache_entry;

struct tu_lrz_pipeline
{
   uint32_t lrz_status;

   struct {
      bool has_kill;
      bool force_early_z;
      bool early_fragment_tests;
   } fs;

   bool force_late_z;
   bool blend_valid;
};

struct tu_bandwidth
{
   uint32_t color_bandwidth_per_sample;
   uint32_t depth_cpp_per_sample;
   uint32_t stencil_cpp_per_sample;
   bool valid;
};

struct tu_compiled_shaders
{
   struct vk_pipeline_cache_object base;

   struct tu_const_state const_state[MESA_SHADER_STAGES];
   uint8_t active_desc_sets;

   struct ir3_shader_variant *variants[MESA_SHADER_STAGES];

   struct ir3_shader_variant *safe_const_variants[MESA_SHADER_STAGES];
};

struct tu_nir_shaders
{
   struct vk_pipeline_cache_object base;

   /* This is optional, and is only filled out when a library pipeline is
    * compiled with RETAIN_LINK_TIME_OPTIMIZATION_INFO.
    */
   nir_shader *nir[MESA_SHADER_STAGES];
};

extern const struct vk_pipeline_cache_object_ops tu_shaders_ops;
extern const struct vk_pipeline_cache_object_ops tu_nir_shaders_ops;

static bool inline
tu6_shared_constants_enable(const struct tu_pipeline_layout *layout,
                            const struct ir3_compiler *compiler)
{
   return layout->push_constant_size > 0 &&
          layout->push_constant_size <= (compiler->shared_consts_size * 16);
}

struct tu_program_descriptor_linkage
{
   struct ir3_const_state const_state;

   uint32_t constlen;

   struct tu_const_state tu_const_state;
};

struct tu_pipeline_executable {
   gl_shader_stage stage;

   struct ir3_info stats;
   bool is_binning;

   char *nir_from_spirv;
   char *nir_final;
   char *disasm;
};

enum tu_pipeline_type {
   TU_PIPELINE_GRAPHICS,
   TU_PIPELINE_GRAPHICS_LIB,
   TU_PIPELINE_COMPUTE,
};

struct tu_pipeline
{
   struct vk_object_base base;
   enum tu_pipeline_type type;

   struct tu_cs cs;
   struct tu_suballoc_bo bo;

   /* Separate BO for private memory since it should GPU writable */
   struct tu_bo *pvtmem_bo;

   VkShaderStageFlags active_stages;
   uint32_t active_desc_sets;

   /* mask of enabled dynamic states
    * if BIT(i) is set, pipeline->dynamic_state[i] is used
    */
   uint32_t set_state_mask;
   struct tu_draw_state dynamic_state[TU_DYNAMIC_STATE_COUNT];

   struct {
      unsigned patch_type;
   } tess;

   struct {
      bool raster_order_attachment_access;
   } ds;

   /* Misc. info from the fragment output interface state that is used
    * elsewhere.
    */
   struct {
      bool raster_order_attachment_access;
   } output;

   /* In other words - framebuffer fetch support */
   struct {
      /* If the pipeline sets SINGLE_PRIM_MODE for sysmem. */
      bool sysmem_single_prim_mode;
      struct tu_draw_state state_sysmem, state_gmem;
   } prim_order;

   /* draw states for the pipeline */
   struct tu_draw_state load_state;

   struct tu_push_constant_range shared_consts;

   struct
   {
      struct tu_draw_state config_state;
      struct tu_draw_state state;
      struct tu_draw_state binning_state;

      struct tu_program_descriptor_linkage link[MESA_SHADER_STAGES];

      uint32_t vs_param_stride;
      uint32_t hs_param_stride;
      uint32_t hs_param_dwords;
      uint32_t hs_vertices_out;

      bool per_view_viewport;
      bool per_samp;

      enum a6xx_tess_output tess_output_upper_left, tess_output_lower_left;
      enum a6xx_tess_spacing tess_spacing;
   } program;

   struct tu_lrz_pipeline lrz;
   struct tu_bandwidth bandwidth;

   void *executables_mem_ctx;
   /* tu_pipeline_executable */
   struct util_dynarray executables;
};

struct tu_graphics_lib_pipeline {
   struct tu_pipeline base;

   VkGraphicsPipelineLibraryFlagsEXT state;

   struct vk_graphics_pipeline_state graphics_state;

   /* For vk_graphics_pipeline_state */
   void *state_data;

   /* compiled_shaders only contains variants compiled by this pipeline, and
    * it owns them, so when it is freed they disappear.  Similarly,
    * nir_shaders owns the link-time NIR. shaders points to the shaders from
    * this pipeline and all libraries included in it, for convenience.
    */
   struct tu_compiled_shaders *compiled_shaders;
   struct tu_nir_shaders *nir_shaders;
   struct {
      nir_shader *nir;
      struct tu_shader_key key;
      struct tu_const_state const_state;
      struct ir3_shader_variant *variant, *safe_const_variant;
   } shaders[MESA_SHADER_FRAGMENT + 1];

   struct ir3_shader_key ir3_key;

   /* Used to stitch together an overall layout for the final pipeline. */
   struct tu_descriptor_set_layout *layouts[MAX_SETS];
   unsigned num_sets;
   unsigned push_constant_size;
   bool independent_sets;
};

struct tu_graphics_pipeline {
   struct tu_pipeline base;

   struct vk_dynamic_graphics_state dynamic_state;
   bool feedback_loop_color, feedback_loop_ds;
   bool feedback_loop_may_involve_textures;
   bool has_fdm;
};

struct tu_compute_pipeline {
   struct tu_pipeline base;

   uint32_t local_size[3];
   uint32_t subgroup_size;
   uint32_t instrlen;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(tu_pipeline, base, VkPipeline,
                               VK_OBJECT_TYPE_PIPELINE)

#define TU_DECL_PIPELINE_DOWNCAST(pipe_type, pipe_enum)              \
   static inline struct tu_##pipe_type##_pipeline *                  \
   tu_pipeline_to_##pipe_type(struct tu_pipeline *pipeline)          \
   {                                                                 \
      assert(pipeline->type == pipe_enum);                           \
      return (struct tu_##pipe_type##_pipeline *) pipeline;          \
   }

TU_DECL_PIPELINE_DOWNCAST(graphics, TU_PIPELINE_GRAPHICS)
TU_DECL_PIPELINE_DOWNCAST(graphics_lib, TU_PIPELINE_GRAPHICS_LIB)
TU_DECL_PIPELINE_DOWNCAST(compute, TU_PIPELINE_COMPUTE)

VkOffset2D tu_fdm_per_bin_offset(VkExtent2D frag_area, VkRect2D bin);

uint32_t tu_emit_draw_state(struct tu_cmd_buffer *cmd);

struct tu_pvtmem_config {
   uint64_t iova;
   uint32_t per_fiber_size;
   uint32_t per_sp_size;
   bool per_wave;
};

void
tu6_emit_xs_config(struct tu_cs *cs,
                   gl_shader_stage stage,
                   const struct ir3_shader_variant *xs);

void
tu6_emit_xs(struct tu_cs *cs,
            gl_shader_stage stage,
            const struct ir3_shader_variant *xs,
            const struct tu_pvtmem_config *pvtmem,
            uint64_t binary_iova);

void
tu6_emit_vpc(struct tu_cs *cs,
             const struct ir3_shader_variant *vs,
             const struct ir3_shader_variant *hs,
             const struct ir3_shader_variant *ds,
             const struct ir3_shader_variant *gs,
             const struct ir3_shader_variant *fs);

void
tu6_emit_fs_inputs(struct tu_cs *cs, const struct ir3_shader_variant *fs);

void
tu_fill_render_pass_state(struct vk_render_pass_state *rp,
                          const struct tu_render_pass *pass,
                          const struct tu_subpass *subpass);

#endif /* TU_PIPELINE_H */
