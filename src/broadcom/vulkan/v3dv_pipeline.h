/*
 * Copyright © 2026 Raspberry Pi Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#ifndef V3DV_PIPELINE_H
#define V3DV_PIPELINE_H

#include "v3dv_common.h"
#include "v3dv_limits.h"
#include "v3dv_bo.h"
#include "v3dv_descriptor_set.h"
#include "vk_pipeline.h"
#include "vk_graphics_state.h"
#include "compiler/shader_enums.h"
#include "compiler/v3d_compiler.h"
#include "util/mesa-blake3.h"
#include "util/u_dynarray.h"
#include "util/hash_table.h"

struct v3dv_cmd_buffer;
struct v3dv_device;
struct v3dv_render_pass;
struct v3dv_subpass;
struct vk_shader_module;
struct nir_shader;
struct nir_shader_compiler_options;

struct v3dv_pipeline_key {
   uint8_t topology;
   uint8_t logicop_func;
   bool msaa;
   bool sample_alpha_to_coverage;
   bool sample_alpha_to_one;
   bool software_blend;
   uint8_t cbufs;
   struct {
      enum pipe_format format;
      uint8_t swizzle[4];
   } color_fmt[V3D_MAX_DRAW_BUFFERS];
   struct {
           enum pipe_blend_func rgb_func;
           enum pipe_blendfactor rgb_src_factor;
           enum pipe_blendfactor rgb_dst_factor;
           enum pipe_blend_func alpha_func;
           enum pipe_blendfactor alpha_src_factor;
           enum pipe_blendfactor alpha_dst_factor;
   } blend[V3D_MAX_DRAW_BUFFERS];
   uint8_t f32_color_rb;
   uint8_t norm_16;
   uint8_t snorm;
   uint32_t va_swap_rb_mask;
   bool has_multiview;
   bool line_smooth;
};

struct v3dv_pipeline_cache_stats {
   uint32_t miss;
   uint32_t hit;
   uint32_t count;
   uint32_t on_disk_hit;
};

/* Equivalent to mesa_shader_stage, but including the coordinate shaders
 *
 * FIXME: perhaps move to common
 */
enum broadcom_shader_stage {
   BROADCOM_SHADER_VERTEX,
   BROADCOM_SHADER_VERTEX_BIN,
   BROADCOM_SHADER_GEOMETRY,
   BROADCOM_SHADER_GEOMETRY_BIN,
   BROADCOM_SHADER_FRAGMENT,
   BROADCOM_SHADER_COMPUTE,
};

#define BROADCOM_SHADER_STAGES (BROADCOM_SHADER_COMPUTE + 1)

/* Assumes that coordinate shaders will be custom-handled by the caller */
static inline enum broadcom_shader_stage
mesa_shader_stage_to_broadcom(mesa_shader_stage stage)
{
   switch (stage) {
   case MESA_SHADER_VERTEX:
      return BROADCOM_SHADER_VERTEX;
   case MESA_SHADER_GEOMETRY:
      return BROADCOM_SHADER_GEOMETRY;
   case MESA_SHADER_FRAGMENT:
      return BROADCOM_SHADER_FRAGMENT;
   case MESA_SHADER_COMPUTE:
      return BROADCOM_SHADER_COMPUTE;
   default:
      UNREACHABLE("Unknown gl shader stage");
   }
}

static inline mesa_shader_stage
broadcom_shader_stage_to_gl(enum broadcom_shader_stage stage)
{
   switch (stage) {
   case BROADCOM_SHADER_VERTEX:
   case BROADCOM_SHADER_VERTEX_BIN:
      return MESA_SHADER_VERTEX;
   case BROADCOM_SHADER_GEOMETRY:
   case BROADCOM_SHADER_GEOMETRY_BIN:
      return MESA_SHADER_GEOMETRY;
   case BROADCOM_SHADER_FRAGMENT:
      return MESA_SHADER_FRAGMENT;
   case BROADCOM_SHADER_COMPUTE:
      return MESA_SHADER_COMPUTE;
   default:
      UNREACHABLE("Unknown broadcom shader stage");
   }
}

static inline bool
broadcom_shader_stage_is_binning(enum broadcom_shader_stage stage)
{
   switch (stage) {
   case BROADCOM_SHADER_VERTEX_BIN:
   case BROADCOM_SHADER_GEOMETRY_BIN:
      return true;
   default:
      return false;
   }
}

static inline bool
broadcom_shader_stage_is_render_with_binning(enum broadcom_shader_stage stage)
{
   switch (stage) {
   case BROADCOM_SHADER_VERTEX:
   case BROADCOM_SHADER_GEOMETRY:
      return true;
   default:
      return false;
   }
}

static inline enum broadcom_shader_stage
broadcom_binning_shader_stage_for_render_stage(enum broadcom_shader_stage stage)
{
   switch (stage) {
   case BROADCOM_SHADER_VERTEX:
      return BROADCOM_SHADER_VERTEX_BIN;
   case BROADCOM_SHADER_GEOMETRY:
      return BROADCOM_SHADER_GEOMETRY_BIN;
   default:
      UNREACHABLE("Invalid shader stage");
   }
}

static inline const char *
broadcom_shader_stage_name(enum broadcom_shader_stage stage)
{
   switch(stage) {
   case BROADCOM_SHADER_VERTEX_BIN:
      return "MESA_SHADER_VERTEX_BIN";
   case BROADCOM_SHADER_GEOMETRY_BIN:
      return "MESA_SHADER_GEOMETRY_BIN";
   default:
      return mesa_shader_stage_name(broadcom_shader_stage_to_gl(stage));
   }
}

struct v3dv_pipeline_cache {
   struct vk_object_base base;

   struct v3dv_device *device;
   mtx_t mutex;

   struct hash_table *nir_cache;
   struct v3dv_pipeline_cache_stats nir_stats;

   struct hash_table *cache;
   struct v3dv_pipeline_cache_stats stats;

   /* For VK_EXT_pipeline_creation_cache_control. */
   bool externally_synchronized;
};

enum v3dv_ez_state {
   V3D_EZ_UNDECIDED = 0,
   V3D_EZ_GT_GE,
   V3D_EZ_LT_LE,
   V3D_EZ_DISABLED,
};

struct v3dv_shader_variant {
   enum broadcom_shader_stage stage;

   union {
      struct v3d_prog_data *base;
      struct v3d_vs_prog_data *vs;
      struct v3d_gs_prog_data *gs;
      struct v3d_fs_prog_data *fs;
      struct v3d_compute_prog_data *cs;
   } prog_data;

   /* We explicitly save the prog_data_size as it would make easier to
    * serialize
    */
   uint32_t prog_data_size;

   /* The assembly for this variant will be uploaded to a BO shared with all
    * other shader stages in that pipeline. This is the offset in that BO.
    */
   uint32_t assembly_offset;

   /* Note: don't assume qpu_insts to be always NULL or not-NULL. In general
    * we will try to free it as soon as we upload it to the shared bo while we
    * compile the different stages. But we can decide to keep it around based
    * on some pipeline creation flags, like
    * VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT.
    */
   uint64_t *qpu_insts;
   uint32_t qpu_insts_size;
};

/*
 * Per-stage info for each stage, useful so shader_module_compile_to_nir and
 * other methods doesn't have so many parameters.
 *
 * FIXME: for the case of the coordinate shader and the vertex shader, module,
 * entrypoint, spec_info and nir are the same. There are also info only
 * relevant to some stages. But seemed too much a hassle to create a new
 * struct only to handle that. Revisit if such kind of info starts to grow.
 */
struct v3dv_pipeline_stage {
   struct v3dv_pipeline *pipeline;

   enum broadcom_shader_stage stage;

   const struct vk_shader_module *module;
   const char *entrypoint;
   const VkSpecializationInfo *spec_info;
   const VkShaderModuleCreateInfo *module_info;

   nir_shader *nir;

   /* The following is the combined hash of module+entrypoint+spec_info+nir */
   unsigned char shader_blake3[BLAKE3_KEY_LEN];

   /** A name for this program, so you can track it in shader-db output. */
   uint32_t program_id;

   VkPipelineCreationFeedback feedback;

   struct vk_pipeline_robustness_state robustness;
};

struct v3dv_viewport_state {
   float translate[MAX_VIEWPORTS][3];
   float scale[MAX_VIEWPORTS][3];
};

enum v3dv_cmd_dirty_bits {
   V3DV_CMD_DIRTY_PIPELINE                  = 1 << 0,
   V3DV_CMD_DIRTY_COMPUTE_PIPELINE          = 1 << 1,
   V3DV_CMD_DIRTY_VERTEX_BUFFER             = 1 << 2,
   V3DV_CMD_DIRTY_INDEX_BUFFER              = 1 << 3,
   V3DV_CMD_DIRTY_DESCRIPTOR_SETS           = 1 << 4,
   V3DV_CMD_DIRTY_COMPUTE_DESCRIPTOR_SETS   = 1 << 5,
   V3DV_CMD_DIRTY_PUSH_CONSTANTS            = 1 << 6,
   V3DV_CMD_DIRTY_PUSH_CONSTANTS_UBO        = 1 << 7,
   V3DV_CMD_DIRTY_OCCLUSION_QUERY           = 1 << 8,
   V3DV_CMD_DIRTY_VIEW_INDEX                = 1 << 9,
   V3DV_CMD_DIRTY_DRAW_ID                   = 1 << 10,
   V3DV_CMD_DIRTY_ALL                       = (1 << 10) - 1,
};

struct v3dv_dynamic_state {
   struct v3dv_viewport_state viewport;
   uint32_t color_write_enable;
};

void v3dv_viewport_compute_xform(const VkViewport *viewport,
                                 float scale[3],
                                 float translate[3]);

struct v3dv_descriptor_maps {
   struct v3dv_descriptor_map ubo_map;
   struct v3dv_descriptor_map ssbo_map;
   struct v3dv_descriptor_map sampler_map;
   struct v3dv_descriptor_map texture_map;
};

/* The structure represents data shared between different objects, like the
 * pipeline and the pipeline cache, so we ref count it to know when it should
 * be freed.
 */
struct v3dv_pipeline_shared_data {
   uint32_t ref_cnt;

   unsigned char blake3_key[BLAKE3_KEY_LEN];

   struct v3dv_descriptor_maps *maps[BROADCOM_SHADER_STAGES];
   struct v3dv_shader_variant *variants[BROADCOM_SHADER_STAGES];

   struct v3dv_bo *assembly_bo;

   /* Owning VkPipelineCache, used for VK_EXT_device_memory_report
    * attribution of assembly_bo. Pipeline cache is the correct owner (rather
    * than whichever pipeline first populated this entry) since shared_data is
    * refcounted and shared across any pipeline that hits this blake3_key.
    */
   VkObjectType owner_type;
   uint64_t owner_handle;
};

struct v3dv_pipeline_executable_data {
   enum broadcom_shader_stage stage;
   char *nir_str;
   char *qpu_str;
};

struct v3dv_pipeline {
   struct vk_object_base base;

   struct v3dv_device *device;

   VkShaderStageFlags active_stages;
   VkPipelineCreateFlagBits2KHR flags;

   struct v3dv_render_pass *pass;
   struct v3dv_subpass *subpass;

   struct v3dv_pipeline_stage *stages[BROADCOM_SHADER_STAGES];

   /* For VK_KHR_dynamic_rendering */
   struct vk_render_pass_state rendering_info;
   struct vk_multiview_state multiview_info;

   /* Flags for whether optional pipeline stages are present, for convenience */
   bool has_gs;

   /* Whether any stage in this pipeline uses VK_KHR_buffer_device_address */
   bool uses_buffer_device_address;

   /* Spilling memory requirements */
   struct {
      struct v3dv_bo *bo;
      uint32_t size_per_thread;
   } spill;

   struct vk_dynamic_graphics_state dynamic_graphics_state;
   struct v3dv_dynamic_state dynamic;

   struct v3dv_pipeline_layout *layout;

   enum v3dv_ez_state ez_state;

   /* If ez_state is V3D_EZ_DISABLED, if the reason for disabling is that the
    * pipeline selects an incompatible depth test function.
    */
   bool incompatible_ez_test;

   bool rasterization_enabled;
   bool msaa;
   bool sample_rate_shading;
   uint32_t sample_mask;

   bool negative_one_to_one;

   /* Indexed by vertex binding. */
   struct v3dv_pipeline_vertex_binding {
      uint32_t instance_divisor;
   } vb[MAX_VBS];
   uint32_t vb_count;

   /* Note that a lot of info from VkVertexInputAttributeDescription is
    * already prepacked, so here we are only storing those that need recheck
    * later. The array must be indexed by driver location, since that is the
    * order in which we need to emit the attributes.
    */
   struct v3dv_pipeline_vertex_attrib {
      uint32_t binding;
      uint32_t offset;
      VkFormat vk_format;
   } va[MAX_VERTEX_ATTRIBS];
   uint32_t va_count;

   enum mesa_prim topology;

   bool line_smooth;

   struct v3dv_pipeline_shared_data *shared_data;

   /* It is the combined stages blake3, layout blake3, plus the pipeline key. */
   unsigned char blake3[BLAKE3_KEY_LEN];

   /* In general we can reuse v3dv_device->default_attribute_float, so note
    * that the following can be NULL. In 7.x this is not used, so it will be
    * always NULL.
    *
    * FIXME: the content of this BO will be small, so it could be improved to
    * be uploaded to a common BO. But as in most cases it will be NULL, it is
    * not a priority.
    */
   struct v3dv_bo *default_attribute_values;

   struct vpm_config vpm_cfg;
   struct vpm_config vpm_cfg_bin;

   /* If the pipeline should emit any of the stencil configuration packets */
   bool emit_stencil_cfg[2];

   /* Blend state */
   struct {
      /* In some cases, such as when dual source blend factors are in use, we
       * fall back to software blend lowering.
       */
      bool use_software;

      /* Per-RT bit mask with blend enables. */
      uint8_t enables;
      /* Per-RT prepacked blend config packets */
      uint8_t cfg[V3D_MAX_DRAW_BUFFERS][V3DV_BLEND_CFG_LENGTH];
      /* Flag indicating whether the blend factors in use require
       * color constants.
       */
      bool needs_color_constants;
      /* Mask with enabled color channels for each RT (4 bits per RT) */
      uint32_t color_write_masks;
   } blend;

   struct {
      void *mem_ctx;
      struct util_dynarray data; /* Array of v3dv_pipeline_executable_data */
   } executables;

   /* Packets prepacked during pipeline creation
    */
   uint8_t cfg_bits[V3DV_CFG_BITS_LENGTH];
   uint8_t shader_state_record[V3DV_GL_SHADER_STATE_RECORD_LENGTH];
   uint8_t vcm_cache_size[V3DV_VCM_CACHE_SIZE_LENGTH];
   uint8_t vertex_attrs[V3DV_GL_SHADER_STATE_ATTRIBUTE_RECORD_LENGTH *
                        MAX_VERTEX_ATTRIBS];
   uint8_t stencil_cfg[2][V3DV_STENCIL_CFG_LENGTH];
};

struct v3dv_pipeline_layout {
   struct vk_object_base base;

   struct {
      struct v3dv_descriptor_set_layout *layout;
      uint32_t dynamic_offset_start;
   } set[MAX_SETS];

   uint32_t num_sets;

   /* Shader stages that are declared to use descriptors from this layout */
   uint32_t shader_stages;

   uint32_t dynamic_offset_count;
   uint32_t push_constant_size;

   /* Pipeline layouts can be destroyed after creating pipelines since
    * maintenance4.
    */
   uint32_t ref_cnt;

   unsigned char blake3[BLAKE3_KEY_LEN];
};

void
v3dv_pipeline_layout_destroy(struct v3dv_device *device,
                             struct v3dv_pipeline_layout *layout,
                             const VkAllocationCallbacks *alloc);

static inline void
v3dv_pipeline_layout_ref(struct v3dv_pipeline_layout *layout)
{
   assert(layout && layout->ref_cnt >= 1);
   p_atomic_inc(&layout->ref_cnt);
}

static inline void
v3dv_pipeline_layout_unref(struct v3dv_device *device,
                           struct v3dv_pipeline_layout *layout,
                           const VkAllocationCallbacks *alloc)
{
   assert(layout && layout->ref_cnt >= 1);
   if (p_atomic_dec_zero(&layout->ref_cnt))
      v3dv_pipeline_layout_destroy(device, layout, alloc);
}

static inline VkPipelineBindPoint
v3dv_pipeline_get_binding_point(struct v3dv_pipeline *pipeline)
{
   assert(pipeline->active_stages == VK_SHADER_STAGE_COMPUTE_BIT ||
          !(pipeline->active_stages & VK_SHADER_STAGE_COMPUTE_BIT));
   return pipeline->active_stages == VK_SHADER_STAGE_COMPUTE_BIT ?
      VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
}

const nir_shader_compiler_options *
v3dv_pipeline_get_nir_options(const struct v3d_device_info *devinfo);

struct v3dv_cl_reloc v3dv_write_uniforms(struct v3dv_cmd_buffer *cmd_buffer,
                                         struct v3dv_pipeline *pipeline,
                                         struct v3dv_shader_variant *variant);

struct v3dv_cl_reloc v3dv_write_uniforms_wg_offsets(struct v3dv_cmd_buffer *cmd_buffer,
                                                    struct v3dv_pipeline *pipeline,
                                                    struct v3dv_shader_variant *variant,
                                                    uint32_t **wg_count_offsets);

struct v3dv_shader_variant *
v3dv_get_shader_variant(struct v3dv_pipeline_stage *p_stage,
                        struct v3dv_pipeline_cache *cache,
                        struct v3d_key *key,
                        size_t key_size,
                        const VkAllocationCallbacks *pAllocator,
                        VkResult *out_vk_result);

struct v3dv_shader_variant *
v3dv_shader_variant_create(struct v3dv_device *device,
                           enum broadcom_shader_stage stage,
                           struct v3d_prog_data *prog_data,
                           uint32_t prog_data_size,
                           uint32_t assembly_offset,
                           uint64_t *qpu_insts,
                           uint32_t qpu_insts_size,
                           VkResult *out_vk_result);

void
v3dv_shader_variant_destroy(struct v3dv_device *device,
                            struct v3dv_shader_variant *variant);

static inline void
v3dv_pipeline_shared_data_ref(struct v3dv_pipeline_shared_data *shared_data)
{
   assert(shared_data && shared_data->ref_cnt >= 1);
   p_atomic_inc(&shared_data->ref_cnt);
}

void
v3dv_pipeline_shared_data_destroy(struct v3dv_device *device,
                                  struct v3dv_pipeline_shared_data *shared_data);

static inline void
v3dv_pipeline_shared_data_unref(struct v3dv_device *device,
                                struct v3dv_pipeline_shared_data *shared_data)
{
   assert(shared_data && shared_data->ref_cnt >= 1);
   if (p_atomic_dec_zero(&shared_data->ref_cnt))
      v3dv_pipeline_shared_data_destroy(device, shared_data);
}

void v3dv_pipeline_cache_init(struct v3dv_pipeline_cache *cache,
                              struct v3dv_device *device,
                              VkPipelineCacheCreateFlags,
                              bool cache_enabled);

void v3dv_pipeline_cache_finish(struct v3dv_pipeline_cache *cache);

void v3dv_pipeline_cache_upload_nir(struct v3dv_pipeline *pipeline,
                                    struct v3dv_pipeline_cache *cache,
                                    nir_shader *nir,
                                    unsigned char blake3_key[BLAKE3_KEY_LEN]);

nir_shader* v3dv_pipeline_cache_search_for_nir(struct v3dv_pipeline *pipeline,
                                               struct v3dv_pipeline_cache *cache,
                                               const nir_shader_compiler_options *nir_options,
                                               unsigned char blake3_key[BLAKE3_KEY_LEN]);

struct v3dv_pipeline_shared_data *
v3dv_pipeline_cache_search_for_pipeline(struct v3dv_pipeline_cache *cache,
                                        unsigned char blake3_key[BLAKE3_KEY_LEN],
                                        bool *cache_hit);

void
v3dv_pipeline_cache_upload_pipeline(struct v3dv_pipeline *pipeline,
                                    struct v3dv_pipeline_cache *cache);

VkResult
v3dv_create_compute_pipeline_from_nir(struct v3dv_device *device,
                                      nir_shader *nir,
                                      VkPipelineLayout pipeline_layout,
                                      VkPipeline *pipeline);

float
v3dv_get_aa_line_width(struct v3dv_pipeline *pipeline,
                       struct v3dv_cmd_buffer *buffer);


void
v3dv_compute_ez_state(struct vk_dynamic_graphics_state *dyn,
                      struct v3dv_pipeline *pipeline,
                      enum v3dv_ez_state *ez_state,
                      bool *incompatible_ez_test);

uint32_t v3dv_pipeline_primitive(VkPrimitiveTopology vk_prim);

VK_DEFINE_NONDISP_HANDLE_CASTS(v3dv_pipeline, base, VkPipeline,
                               VK_OBJECT_TYPE_PIPELINE)
VK_DEFINE_NONDISP_HANDLE_CASTS(v3dv_pipeline_cache, base, VkPipelineCache,
                               VK_OBJECT_TYPE_PIPELINE_CACHE)
VK_DEFINE_NONDISP_HANDLE_CASTS(v3dv_pipeline_layout, base, VkPipelineLayout,
                               VK_OBJECT_TYPE_PIPELINE_LAYOUT)

#endif /* V3DV_PIPELINE_H */
