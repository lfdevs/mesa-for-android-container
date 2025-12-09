/*
 * Copyright © 2024 Collabora, Ltd.
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

#ifndef VK_SHADER_H
#define VK_SHADER_H

#include "compiler/spirv/nir_spirv.h"
#include "vk_internal_exts.h"
#include "vk_limits.h"
#include "vk_pipeline_cache.h"

#include "util/mesa-blake3.h"

#ifdef __cplusplus
extern "C" {
#endif

struct blob;
struct nir_shader;
struct vk_command_buffer;
struct vk_device;
struct vk_descriptor_set_layout;
struct vk_dynamic_graphics_state;
struct vk_graphics_pipeline_state;
struct vk_features;
struct vk_physical_device;
struct vk_pipeline;
struct vk_pipeline_robustness_state;

bool vk_validate_shader_binaries(void);

int vk_shader_cmp_graphics_stages(mesa_shader_stage a, mesa_shader_stage b);
int vk_shader_cmp_rt_stages(mesa_shader_stage a, mesa_shader_stage b);

#define MESA_VK_PIPELINE_RAY_TRACING_FLAGS ( \
   VK_PIPELINE_CREATE_2_RAY_TRACING_SKIP_BUILT_IN_PRIMITIVES_BIT_KHR | \
   VK_PIPELINE_CREATE_2_RAY_TRACING_ALLOW_SPHERES_AND_LINEAR_SWEPT_SPHERES_BIT_NV | \
   VK_PIPELINE_CREATE_2_RAY_TRACING_SKIP_TRIANGLES_BIT_KHR | \
   VK_PIPELINE_CREATE_2_RAY_TRACING_SKIP_AABBS_BIT_KHR | \
   VK_PIPELINE_CREATE_2_RAY_TRACING_NO_NULL_ANY_HIT_SHADERS_BIT_KHR | \
   VK_PIPELINE_CREATE_2_RAY_TRACING_NO_NULL_CLOSEST_HIT_SHADERS_BIT_KHR | \
   VK_PIPELINE_CREATE_2_RAY_TRACING_NO_NULL_MISS_SHADERS_BIT_KHR | \
   VK_PIPELINE_CREATE_2_RAY_TRACING_NO_NULL_INTERSECTION_SHADERS_BIT_KHR | \
   VK_PIPELINE_CREATE_2_RAY_TRACING_SHADER_GROUP_HANDLE_CAPTURE_REPLAY_BIT_KHR | \
   VK_PIPELINE_CREATE_2_RAY_TRACING_ALLOW_MOTION_BIT_NV | \
   VK_PIPELINE_CREATE_2_RAY_TRACING_OPACITY_MICROMAP_BIT_EXT | \
   VK_PIPELINE_CREATE_2_RAY_TRACING_DISPLACEMENT_MICROMAP_BIT_NV | \
   VK_PIPELINE_CREATE_2_DISALLOW_OPACITY_MICROMAP_BIT_ARM)

struct vk_shader_compile_info {
   mesa_shader_stage stage;
   VkShaderCreateFlagsEXT flags;
   /* RT flags only includes :
    *    - VK_PIPELINE_CREATE_2_RAY_TRACING_SKIP_BUILT_IN_PRIMITIVES_BIT_KHR
    *    - VK_PIPELINE_CREATE_2_RAY_TRACING_ALLOW_SPHERES_AND_LINEAR_SWEPT_SPHERES_BIT_NV
    *    - VK_PIPELINE_CREATE_2_RAY_TRACING_SKIP_TRIANGLES_BIT_KHR
    *    - VK_PIPELINE_CREATE_2_RAY_TRACING_SKIP_AABBS_BIT_KHR
    *    - VK_PIPELINE_CREATE_2_RAY_TRACING_NO_NULL_ANY_HIT_SHADERS_BIT_KHR
    *    - VK_PIPELINE_CREATE_2_RAY_TRACING_NO_NULL_CLOSEST_HIT_SHADERS_BIT_KHR
    *    - VK_PIPELINE_CREATE_2_RAY_TRACING_NO_NULL_MISS_SHADERS_BIT_KHR
    *    - VK_PIPELINE_CREATE_2_RAY_TRACING_NO_NULL_INTERSECTION_SHADERS_BIT_KHR
    *    - VK_PIPELINE_CREATE_2_RAY_TRACING_SHADER_GROUP_HANDLE_CAPTURE_REPLAY_BIT_KHR
    *    - VK_PIPELINE_CREATE_2_RAY_TRACING_ALLOW_MOTION_BIT_NV
    *    - VK_PIPELINE_CREATE_2_RAY_TRACING_OPACITY_MICROMAP_BIT_EXT
    *    - VK_PIPELINE_CREATE_2_RAY_TRACING_DISPLACEMENT_MICROMAP_BIT_NV
    *    - VK_PIPELINE_CREATE_2_DISALLOW_OPACITY_MICROMAP_BIT_ARM
   */
   VkPipelineCreateFlags2KHR rt_flags;
   VkShaderStageFlags next_stage_mask;
   struct nir_shader *nir;

   const struct vk_pipeline_robustness_state *robustness;

   uint32_t set_layout_count;
   struct vk_descriptor_set_layout * const *set_layouts;

   uint32_t push_constant_range_count;
   const VkPushConstantRange *push_constant_ranges;
};

struct vk_shader_ops;

struct vk_shader {
   struct vk_object_base base;

   const struct vk_shader_ops *ops;

   mesa_shader_stage stage;

   /* Stack size for RT shaders */
   VkDeviceSize stack_size;

   /* Scratch size used by the shader */
   VkDeviceSize scratch_size;

   /* Number of ray queries used by the shader (useful to avoid going through
    * all of the shaders in the RT pipelines)
    */
   uint32_t ray_queries;

   /* Used for the generic VkPipeline implementation */
   struct {
      struct vk_pipeline_cache_object cache_obj;
      blake3_hash cache_key;
   } pipeline;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(vk_shader, base, VkShaderEXT,
                               VK_OBJECT_TYPE_SHADER_EXT);

struct vk_shader_ops {
   /** Destroy a vk_shader_object */
   void (*destroy)(struct vk_device *device,
                   struct vk_shader *shader,
                   const VkAllocationCallbacks* pAllocator);

   /** Serialize a vk_shader_object to a blob
    *
    * This function shouldn't need to do any validation of the blob data
    * beyond basic sanity checking.  The common implementation of
    * vkGetShaderBinaryEXT verifies the blobUUID and version of input data as
    * well as a size and checksum to ensure integrity.  This callback is only
    * invoked after validation of the input binary data.
    */
   bool (*serialize)(struct vk_device *device,
                     const struct vk_shader *shader,
                     struct blob *blob);

   /** Returns executable properties for this shader
    *
    * This is equivalent to vkGetPipelineExecutableProperties(), only for a
    * single vk_shader.
    */
   VkResult (*get_executable_properties)(struct vk_device *device,
                                         const struct vk_shader *shader,
                                         uint32_t *executable_count,
                                         VkPipelineExecutablePropertiesKHR *properties);

   /** Returns executable statistics for this shader
    *
    * This is equivalent to vkGetPipelineExecutableStatistics(), only for a
    * single vk_shader.
    */
   VkResult (*get_executable_statistics)(struct vk_device *device,
                                         const struct vk_shader *shader,
                                         uint32_t executable_index,
                                         uint32_t *statistic_count,
                                         VkPipelineExecutableStatisticKHR *statistics);

   /** Returns executable internal representations for this shader
    *
    * This is equivalent to vkGetPipelineExecutableInternalRepresentations(),
    * only for a single vk_shader.
    */
   VkResult (*get_executable_internal_representations)(
      struct vk_device *device,
      const struct vk_shader *shader,
      uint32_t executable_index,
      uint32_t *internal_representation_count,
      VkPipelineExecutableInternalRepresentationKHR *internal_representations);
};

void *vk_shader_zalloc(struct vk_device *device,
                       const struct vk_shader_ops *ops,
                       mesa_shader_stage stage,
                       const VkAllocationCallbacks *alloc,
                       size_t size);
void *vk_shader_multizalloc(struct vk_device *device,
                            struct vk_multialloc *ma,
                            const struct vk_shader_ops *ops,
                            mesa_shader_stage stage,
                            const VkAllocationCallbacks *alloc);
void vk_shader_free(struct vk_device *device,
                    const VkAllocationCallbacks *alloc,
                    struct vk_shader *shader);

VkResult vk_compile_shaders(struct vk_device *device,
                            uint32_t shader_count,
                            struct vk_shader_compile_info *infos,
                            const struct vk_graphics_pipeline_state *state,
                            const struct vk_features *enabled_features,
                            const VkAllocationCallbacks* pAllocator,
                            struct vk_shader **shaders_out);

static inline void
vk_shader_destroy(struct vk_device *device,
                  struct vk_shader *shader,
                  const VkAllocationCallbacks *alloc)
{
   shader->ops->destroy(device, shader, alloc);
}

struct vk_device_shader_ops {
   /** Retrieves a NIR compiler options struct
    *
    * NIR compiler options are only allowed to vary based on physical device,
    * stage, and robustness state.
    */
   const struct nir_shader_compiler_options *(*get_nir_options)(
      struct vk_physical_device *device,
      mesa_shader_stage stage,
      const struct vk_pipeline_robustness_state *rs);

   /** Retrieves a SPIR-V options struct
    *
    * SPIR-V options are only allowed to vary based on physical device, stage,
    * and robustness state.
    */
   struct spirv_to_nir_options (*get_spirv_options)(
      struct vk_physical_device *device,
      mesa_shader_stage stage,
      const struct vk_pipeline_robustness_state *rs);

   /** Preprocesses a NIR shader
    *
    * This callback is optional.
    *
    * If non-NULL, this callback is invoked after the SPIR-V is parsed into
    * NIR and before it is handed to compile().  The driver should do as much
    * generic optimization and lowering as it can here.  Importantly, the
    * preprocess step only knows about the NIR input and the physical device,
    * not any enabled device features or pipeline state.  This allows us to
    * potentially cache this shader and re-use it across pipelines.
    */
   void (*preprocess_nir)(
      struct vk_physical_device *device,
      nir_shader *nir,
      const struct vk_pipeline_robustness_state *rs);

   /** Return shader stages that should be linked together in a group
    *
    * The driver should return 0 if it does not require any linking, otherwise
    * it should return the stages that need to be linked together. The return
    * value should have more than one shader stage and be included in the
    * stages given as parameter.
     */
   VkShaderStageFlags (*get_rt_group_linking)(struct vk_physical_device *device,
                                              VkShaderStageFlags stages);

   /** Hash a vk_graphics_state object and a vk_features object.
    *
    * This callback hashes whatever bits of vk_graphics_pipeline_state might
    * be used to compile a shader in one of the given stages.
    *
    * state may be null, indicating that all state is dynamic. enabled_features
    * is always non-NULL.
    */
   void (*hash_state)(struct vk_physical_device *device,
                      const struct vk_graphics_pipeline_state *state,
                      const struct vk_features *enabled_features,
                      VkShaderStageFlags stages,
                      blake3_hash blake3_out);

   /** Compile (and potentially link) a set of shaders
    *
    * Unlike vkCreateShadersEXT, this callback will only ever be called with
    * multiple shaders if VK_SHADER_CREATE_LINK_STAGE_BIT_EXT is set on all of
    * them.  We also guarantee that the shaders occur in the call in Vulkan
    * pipeline stage order as dictated by vk_shader_cmp_graphics_stages().
    *
    * If state/enabled_features is NULL, the driver must conservatively assume
    * all state is dynamic / all features are enabled respectively.
    *
    * This callback consumes all input NIR shaders, regardless of whether or
    * not it was successful.
    */
   VkResult (*compile)(struct vk_device *device,
                       uint32_t shader_count,
                       struct vk_shader_compile_info *infos,
                       const struct vk_graphics_pipeline_state *state,
                       const struct vk_features *enabled_features,
                       const VkAllocationCallbacks* pAllocator,
                       struct vk_shader **shaders_out);

   /** Create a vk_shader from a binary blob */
   VkResult (*deserialize)(struct vk_device *device,
                           struct blob_reader *blob,
                           uint32_t binary_version,
                           const VkAllocationCallbacks* pAllocator,
                           struct vk_shader **shader_out);


   /** Writes a HW shader record from a shader group */
   void (*write_rt_shader_group)(struct vk_device *device,
                                 VkRayTracingShaderGroupTypeKHR type,
                                 const struct vk_shader **shaders,
                                 uint32_t shader_count,
                                 void *output);

   /** Writes a group replay handle for a shader group */
   void (*write_rt_shader_group_replay_handle)(struct vk_device *device,
                                               const struct vk_shader **shaders,
                                               uint32_t shader_count,
                                               void *output);

   /** Bind a set of shaders
    *
    * This is roughly equivalent to vkCmdBindShadersEXT()
    */
   void (*cmd_bind_shaders)(struct vk_command_buffer *cmd_buffer,
                            uint32_t stage_count,
                            const mesa_shader_stage *stages,
                            struct vk_shader ** const shaders);

   /** Sets dynamic state */
   void (*cmd_set_dynamic_graphics_state)(struct vk_command_buffer *cmd_buffer,
                                          const struct vk_dynamic_graphics_state *state);

   /** Sets scratch size & ray query count for RT pipelines */
   void (*cmd_set_rt_state)(struct vk_command_buffer *cmd_buffer,
                            VkDeviceSize scratch_size,
                            uint32_t ray_queries,
                            const uint8_t *dynamic_descriptor_offsets);

   /** Sets stack size for RT pipelines */
   void (*cmd_set_stack_size)(struct vk_command_buffer *cmd_buffer,
                              VkDeviceSize stack_size);
};

extern const struct vk_pipeline_robustness_state vk_robustness_disabled;

#ifdef __cplusplus
}
#endif

#endif /* VK_SHADER_H */
