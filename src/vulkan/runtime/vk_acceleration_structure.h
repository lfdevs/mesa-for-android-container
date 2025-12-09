/*
 * Copyright © 2021 Bas Nieuwenhuizen
 * Copyright © 2023 Valve Corporation
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

#ifndef VK_ACCELERATION_STRUCTURE_H
#define VK_ACCELERATION_STRUCTURE_H

#include "vk_buffer.h"
#include "vk_meta.h"
#include "vk_object.h"
#include "radix_sort/radix_sort_vk.h"

#include "bvh/vk_bvh.h"

#ifdef __cplusplus
extern "C" {
#endif

enum vk_acceleration_structure_build_step {
   VK_ACCELERATION_STRUCTURE_BUILD_STEP_TOP,
   VK_ACCELERATION_STRUCTURE_BUILD_STEP_BUILD_LEAVES,
   VK_ACCELERATION_STRUCTURE_BUILD_STEP_MORTON_GENERATE,
   VK_ACCELERATION_STRUCTURE_BUILD_STEP_MORTON_SORT,
   VK_ACCELERATION_STRUCTURE_BUILD_STEP_LBVH_BUILD_INTERNAL,
   VK_ACCELERATION_STRUCTURE_BUILD_STEP_PLOC_BUILD_INTERNAL,
   VK_ACCELERATION_STRUCTURE_BUILD_STEP_ENCODE,
   VK_ACCELERATION_STRUCTURE_BUILD_STEP_UPDATE,
};

struct vk_acceleration_structure_build_marker {
   enum vk_acceleration_structure_build_step step;
   union {
      struct {
         uint32_t blas_count;
         uint32_t tlas_count;
      } top; /* Used for VK_ACCELERATION_STRUCTURE_BUILD_STEP_TOP */
      struct {
         uint32_t pass;
         uint32_t key;
         uint32_t leaf_node_count;
         uint32_t internal_node_count;
      } encode; /* Used for VK_ACCELERATION_STRUCTURE_BUILD_STEP_ENCODE, VK_ACCELERATION_STRUCTURE_BUILD_STEP_UPDATE */
   };
};

struct vk_acceleration_structure {
   struct vk_object_base base;

   struct vk_buffer *buffer;

   uint64_t offset;
   uint64_t size;
};

static inline VkDeviceAddress
vk_acceleration_structure_get_va(const struct vk_acceleration_structure *accel_struct)
{
   assert(accel_struct->buffer != NULL);
   return vk_buffer_address(accel_struct->buffer, accel_struct->offset);
}

VK_DEFINE_NONDISP_HANDLE_CASTS(vk_acceleration_structure, base, VkAccelerationStructureKHR,
                               VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR)

#define MAX_ENCODE_PASSES 4
#define MAX_UPDATE_PASSES 2

enum vk_internal_build_type {
   VK_INTERNAL_BUILD_TYPE_LBVH,
   VK_INTERNAL_BUILD_TYPE_PLOC,
   VK_INTERNAL_BUILD_TYPE_UPDATE,
};

struct vk_build_config {
   enum vk_internal_build_type internal_type;
   bool updateable;
   uint32_t encode_key[MAX_ENCODE_PASSES];
   uint32_t update_key[MAX_ENCODE_PASSES];
};

struct vk_scratch_layout {
   uint32_t size;
   uint32_t update_size;

   uint32_t header_offset;

   /* Used for BUILD only. */

   uint32_t sort_buffer_offset[2];
   uint32_t sort_internal_offset;

   uint32_t ploc_prefix_sum_partition_offset;
   uint32_t lbvh_node_offset;

   uint32_t ir_offset;
   uint32_t internal_node_offset;
};

struct vk_acceleration_structure_build_state {
   const VkAccelerationStructureBuildGeometryInfoKHR *build_info;
   const VkAccelerationStructureBuildRangeInfoKHR *build_range_infos;
   uint32_t leaf_node_count;
   struct vk_scratch_layout scratch;
   struct vk_build_config config;
};

struct vk_acceleration_structure_build_ops {
   void (*begin_debug_marker)(VkCommandBuffer commandBuffer,
                              struct vk_acceleration_structure_build_marker *marker);
   void (*end_debug_marker)(VkCommandBuffer commandBuffer,
                            struct vk_acceleration_structure_build_marker *marker);

   void (*get_build_config)(VkDevice device, struct vk_acceleration_structure_build_state *state);

   VkDeviceSize (*get_as_size)(VkDevice device, const struct vk_acceleration_structure_build_state *state);
   VkDeviceSize (*get_encode_scratch_size)(VkDevice device, const struct vk_acceleration_structure_build_state *state);
   VkDeviceSize (*get_update_scratch_size)(VkDevice device, const struct vk_acceleration_structure_build_state *state);

   VkResult (*encode_bind_pipeline[MAX_ENCODE_PASSES])(VkCommandBuffer cmd_buffer, const struct vk_acceleration_structure_build_state *state);
   void (*encode_as[MAX_ENCODE_PASSES])(VkCommandBuffer cmd_buffer, const struct vk_acceleration_structure_build_state *state);

   void (*init_update_scratch)(VkCommandBuffer cmd_buffer, const struct vk_acceleration_structure_build_state *state);
   void (*update_bind_pipeline[MAX_ENCODE_PASSES])(VkCommandBuffer cmd_buffer, const struct vk_acceleration_structure_build_state *state,
                                                   bool flushed_cp_after_init_update_scratch, bool flushed_compute_after_init_update_scratch);
   void (*update_as[MAX_ENCODE_PASSES])(VkCommandBuffer cmd_buffer, const struct vk_acceleration_structure_build_state *state);

   const uint32_t *leaf_spirv_override;
   size_t leaf_spirv_override_size;
};

struct vk_acceleration_structure_build_args {
   uint32_t subgroup_size;
   uint32_t bvh_bounds_offset;
   uint32_t root_flags_offset;
   bool propagate_cull_flags;
   bool emit_markers;
   const radix_sort_vk_t *radix_sort;
};

VkResult vk_get_bvh_build_pipeline_layout(struct vk_device *device, struct vk_meta_device *meta,
                                          unsigned push_constant_size, VkPipelineLayout *layout);

VkResult vk_get_bvh_build_pipeline_spv(struct vk_device *device, struct vk_meta_device *meta,
                                       enum vk_meta_object_key_type type, const uint32_t *spv,
                                       uint32_t spv_size, unsigned push_constant_size,
                                       const struct vk_acceleration_structure_build_args *args,
                                       uint32_t flags, VkPipeline *pipeline,
                                       bool unaligned_dispatch);

void vk_cmd_build_acceleration_structures(VkCommandBuffer cmdbuf,
                                          struct vk_device *device,
                                          struct vk_meta_device *meta,
                                          uint32_t info_count,
                                          const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
                                          const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos,
                                          const struct vk_acceleration_structure_build_args *args);

void vk_get_as_build_sizes(VkDevice _device, VkAccelerationStructureBuildTypeKHR buildType,
                           const VkAccelerationStructureBuildGeometryInfoKHR *pBuildInfo,
                           const uint32_t *pMaxPrimitiveCounts,
                           VkAccelerationStructureBuildSizesInfoKHR *pSizeInfo,
                           const struct vk_acceleration_structure_build_args *args);

bool vk_acceleration_struct_vtx_format_supported(VkFormat format);

static inline VkGeometryTypeKHR
vk_get_as_geometry_type(const VkAccelerationStructureBuildGeometryInfoKHR *build_info)
{
   if (build_info->geometryCount) {
      if (build_info->pGeometries)
         return build_info->pGeometries[0].geometryType;
      else
         return build_info->ppGeometries[0]->geometryType;
   }

   /* If there are no geometries, the geometry type shouldn't matter, but
    * return something.
    */
   return VK_GEOMETRY_TYPE_TRIANGLES_KHR;
}

struct vk_bvh_geometry_data
vk_fill_geometry_data(VkAccelerationStructureTypeKHR type, uint32_t first_id, uint32_t geom_index,
                      const VkAccelerationStructureGeometryKHR *geometry,
                      const VkAccelerationStructureBuildRangeInfoKHR *build_range_info);

void vk_accel_struct_cmd_begin_debug_marker(VkCommandBuffer commandBuffer,
                                            struct vk_acceleration_structure_build_marker *marker);

void vk_accel_struct_cmd_end_debug_marker(VkCommandBuffer commandBuffer,
                                          struct vk_acceleration_structure_build_marker *marker);

#ifdef __cplusplus
}
#endif

#endif
