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

#include "vk_acceleration_structure.h"

#include "vk_alloc.h"
#include "vk_common_entrypoints.h"
#include "vk_device.h"
#include "vk_command_buffer.h"
#include "vk_log.h"
#include "vk_pipeline.h"
#include "vk_meta.h"
#include "vk_shader.h"

#include "bvh/vk_build_interface.h"
#include "bvh/vk_bvh.h"

#include "util/u_string.h"
#include "util/bitset.h"

static const uint32_t leaf_spv[] = {
#include "bvh/leaf.spv.h"
};

static const uint32_t morton_spv[] = {
#include "bvh/morton.spv.h"
};

static const uint32_t lbvh_main_spv[] = {
#include "bvh/lbvh_main.spv.h"
};

static const uint32_t lbvh_generate_ir_spv[] = {
#include "bvh/lbvh_generate_ir.spv.h"
};

static const uint32_t ploc_spv[] = {
#include "bvh/ploc_internal.spv.h"
};

static const uint32_t hploc_spv[] = {
#include "bvh/hploc_internal.spv.h"
};

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_CreateAccelerationStructureKHR(VkDevice _device,
                                         const VkAccelerationStructureCreateInfoKHR *pCreateInfo,
                                         const VkAllocationCallbacks *pAllocator,
                                         VkAccelerationStructureKHR *pAccelerationStructure)
{
   VK_FROM_HANDLE(vk_device, device, _device);
   VK_FROM_HANDLE(vk_buffer, buffer, pCreateInfo->buffer);

   struct vk_acceleration_structure *accel_struct = vk_object_zalloc(
      device, pAllocator, sizeof(struct vk_acceleration_structure),
      VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR);

   if (!accel_struct)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   accel_struct->buffer = buffer;
   accel_struct->offset = pCreateInfo->offset;
   accel_struct->size = pCreateInfo->size;

   if (pCreateInfo->deviceAddress &&
       vk_acceleration_structure_get_va(accel_struct) != pCreateInfo->deviceAddress)
      return vk_error(device, VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS);

   *pAccelerationStructure = vk_acceleration_structure_to_handle(accel_struct);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_CreateAccelerationStructure2KHR(VkDevice _device,
                                          const VkAccelerationStructureCreateInfo2KHR *pCreateInfo,
                                          const VkAllocationCallbacks *pAllocator,
                                          VkAccelerationStructureKHR *pAccelerationStructure)
{
   VK_FROM_HANDLE(vk_device, device, _device);

   struct vk_acceleration_structure *accel_struct = vk_object_zalloc(
      device, pAllocator, sizeof(struct vk_acceleration_structure),
      VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR);

   if (!accel_struct)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   accel_struct->addr = pCreateInfo->addressRange.address;
   accel_struct->size = pCreateInfo->addressRange.size;

   *pAccelerationStructure = vk_acceleration_structure_to_handle(accel_struct);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_common_DestroyAccelerationStructureKHR(VkDevice _device,
                                     VkAccelerationStructureKHR accelerationStructure,
                                     const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(vk_device, device, _device);
   VK_FROM_HANDLE(vk_acceleration_structure, accel_struct, accelerationStructure);

   if (!accel_struct)
      return;

   vk_object_free(device, pAllocator, accel_struct);
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL
vk_common_GetAccelerationStructureDeviceAddressKHR(
   VkDevice _device, const VkAccelerationStructureDeviceAddressInfoKHR *pInfo)
{
   VK_FROM_HANDLE(vk_acceleration_structure, accel_struct, pInfo->accelerationStructure);
   return vk_acceleration_structure_get_va(accel_struct);
}

static void
vk_acceleration_structure_build_state_init(struct vk_acceleration_structure_build_state *state,
                                           struct vk_device *device, uint32_t leaf_count,
                                           const VkAccelerationStructureBuildGeometryInfoKHR *build_info,
                                           const struct vk_acceleration_structure_build_args *args)
{
   state->build_info = build_info;
   state->leaf_node_count = leaf_count;
   state->internal_node_count = MAX2(leaf_count, 2) - 1;

   if (leaf_count <= 4)
      state->config.internal_type = VK_INTERNAL_BUILD_TYPE_LBVH;
   else if (build_info->type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR)
      state->config.internal_type = VK_INTERNAL_BUILD_TYPE_PLOC;
   else if (!(build_info->flags & VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR) &&
            !(build_info->flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR))
      state->config.internal_type = VK_INTERNAL_BUILD_TYPE_PLOC;
   else
      state->config.internal_type = VK_INTERNAL_BUILD_TYPE_LBVH;

   if (build_info->mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR &&
       build_info->type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR &&
       device->as_build_ops->update_as[0])
      state->config.internal_type = VK_INTERNAL_BUILD_TYPE_UPDATE;

   if ((build_info->flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR) &&
       build_info->type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR &&
       device->as_build_ops->update_as[0])
      state->config.updateable = true;

   if (device->as_build_ops->get_build_config)
      device->as_build_ops->get_build_config(vk_device_to_handle(device), state);

   radix_sort_vk_memory_requirements_t requirements = {
      0,
   };
   radix_sort_vk_get_memory_requirements(state->config.u64_keys ? args->radix_sort_96 : args->radix_sort_64, leaf_count,
                                         &requirements);

   uint32_t ir_leaf_size;
   switch (vk_get_as_geometry_type(build_info)) {
   case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
      ir_leaf_size = sizeof(struct vk_ir_triangle_node);
      break;
   case VK_GEOMETRY_TYPE_AABBS_KHR:
      ir_leaf_size = sizeof(struct vk_ir_aabb_node);
      break;
   case VK_GEOMETRY_TYPE_INSTANCES_KHR:
      ir_leaf_size = sizeof(struct vk_ir_instance_node);
      break;
   default:
      UNREACHABLE("Unknown VkGeometryTypeKHR");
   }

   uint32_t offset = 0;

   uint32_t ploc_scratch_space = 0;
   uint32_t hploc_scratch_space = 0;
   uint32_t lbvh_node_space = 0;

   if (state->config.internal_type == VK_INTERNAL_BUILD_TYPE_PLOC)
      ploc_scratch_space = DIV_ROUND_UP(leaf_count, PLOC_WORKGROUP_SIZE) * sizeof(struct ploc_prefix_scan_partition);
   else if (state->config.internal_type == VK_INTERNAL_BUILD_TYPE_HPLOC)
      hploc_scratch_space = sizeof(uint32_t) * state->internal_node_count;
   else
      lbvh_node_space = sizeof(struct lbvh_node_info) * state->internal_node_count;

   uint32_t encode_scratch_size = 0;
   if (device->as_build_ops->get_encode_scratch_size)
      encode_scratch_size = device->as_build_ops->get_encode_scratch_size(vk_device_to_handle(device), state);

   state->scratch.header_offset = offset;
   offset += sizeof(struct vk_ir_header);

   /* The encode passes should not need node sorting state. Reuse the space reserved for node sorting. */
   uint32_t encode_scratch_end = offset + encode_scratch_size;

   state->scratch.sort_buffer_offset[0] = offset;
   offset += requirements.keyvals_size;

   state->scratch.sort_buffer_offset[1] = offset;
   offset += requirements.keyvals_size;

   state->scratch.sort_internal_offset = offset;
   /* Internal sorting data is not needed when PLOC/LBVH are invoked,
    * save space by aliasing them */
   state->scratch.ploc_prefix_sum_partition_offset = offset;
   state->scratch.lbvh_node_offset = offset;
   offset += MAX3(requirements.internal_size, ploc_scratch_space, lbvh_node_space);

   state->scratch.hploc_ranges_offset = offset;
   offset += hploc_scratch_space;

   /* Make sure encode scratch space does not overlap the BVH. */
   offset = MAX2(offset, encode_scratch_end);

   state->scratch.ir_offset = offset;
   offset += ir_leaf_size * leaf_count;

   state->scratch.internal_node_offset = offset;
   offset += sizeof(struct vk_ir_box_node) * state->internal_node_count;

   state->scratch.size = offset;

   if (build_info->type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR &&
       device->as_build_ops->update_as[0]) {
      state->scratch.update_size = device->as_build_ops->get_update_scratch_size(vk_device_to_handle(device), state);
   } else {
      state->scratch.update_size = offset;
   }
}

struct bvh_batch_state {
   bool any_updateable;
   bool any_non_updateable;
   bool any_ploc;
   bool any_hploc;
   bool any_lbvh;
   bool any_update;
};

struct vk_bvh_build_pipeline_layout_key {
   enum vk_meta_object_key_type type;
   uint32_t size;
};

struct vk_bvh_build_pipeline_key {
   enum vk_meta_object_key_type type;
   uint32_t flags;
};

VkResult
vk_get_bvh_build_pipeline_layout(struct vk_device *device, struct vk_meta_device *meta,
                                 unsigned push_constant_size, VkPipelineLayout *layout)
{
   struct vk_bvh_build_pipeline_layout_key key = {
      .type = VK_META_OBJECT_KEY_BVH_PIPELINE_LAYOUT,
      .size = push_constant_size,
   };

   VkPushConstantRange push_constant_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .size = push_constant_size,
   };

   return vk_meta_get_pipeline_layout(
      device, meta, NULL, &push_constant_range, &key, sizeof(key), layout);
}

VkResult
vk_get_bvh_build_pipeline_spv(struct vk_device *device, struct vk_meta_device *meta,
                              enum vk_meta_object_key_type type, const uint32_t *spv,
                              uint32_t spv_size, unsigned push_constant_size,
                              const struct vk_acceleration_structure_build_args *args,
                              uint32_t flags, VkPipeline *pipeline,
                              bool unaligned_dispatch)
{
   VkPipelineLayout layout;
   VkResult result = vk_get_bvh_build_pipeline_layout(device, meta, push_constant_size, &layout);
   if (result != VK_SUCCESS)
      return result;

   struct vk_bvh_build_pipeline_key key = {
      .type = type,
      .flags = flags,
   };

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(meta, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline = pipeline_from_cache;
      return VK_SUCCESS;
   }

   VkShaderModuleCreateInfo module_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .codeSize = spv_size,
      .pCode = spv,
   };

   VkSpecializationMapEntry spec_map[4] = {
      {
         .constantID = SUBGROUP_SIZE_ID,
         .offset = 0,
         .size = sizeof(args->subgroup_size),
      },
      {
         .constantID = BVH_BOUNDS_OFFSET_ID,
         .offset = sizeof(args->subgroup_size),
         .size = sizeof(args->bvh_bounds_offset),
      },
      {
         .constantID = BUILD_FLAGS_ID,
         .offset = sizeof(args->subgroup_size) + sizeof(args->bvh_bounds_offset),
         .size = sizeof(flags),
      },
      {
         .constantID = ROOT_FLAGS_OFFSET_ID,
         .offset = sizeof(args->subgroup_size) +
                   sizeof(args->bvh_bounds_offset),
         .size = sizeof(args->root_flags_offset),
      }
   };

   uint32_t spec_constants[4] = {
      args->subgroup_size,
      args->bvh_bounds_offset,
      flags,
      args->root_flags_offset,
   };

   VkSpecializationInfo spec_info = {
      .mapEntryCount = ARRAY_SIZE(spec_map),
      .pMapEntries = spec_map,
      .dataSize = sizeof(spec_constants),
      .pData = spec_constants,
   };

   VkPipelineShaderStageRequiredSubgroupSizeCreateInfoEXT rssci = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO_EXT,
      .pNext = &module_info,
      .requiredSubgroupSize = args->subgroup_size,
   };

   uint32_t shader_flags = VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT;

   VkPipelineShaderStageCreateInfo shader_stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .pNext = &rssci,
      .flags = shader_flags,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .pName = "main",
      .pSpecializationInfo = &spec_info,
   };

   VkPipelineCreateFlags2CreateInfo pipeline_flags_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO,
      .flags = unaligned_dispatch ? VK_PIPELINE_CREATE_2_UNALIGNED_DISPATCH_BIT_MESA : 0,
   };

   VkComputePipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .pNext = &pipeline_flags_info,
      .stage = shader_stage,
      .flags = 0,
      .layout = layout,
   };

   return vk_meta_create_compute_pipeline(device, meta, &pipeline_info,
                                          &key, sizeof(key), pipeline);
}

static uint32_t
pack_geometry_id_and_flags(uint32_t geometry_id, uint32_t flags)
{
   uint32_t geometry_id_and_flags = geometry_id;
   if (flags & VK_GEOMETRY_OPAQUE_BIT_KHR)
      geometry_id_and_flags |= VK_GEOMETRY_OPAQUE;

   return geometry_id_and_flags;
}

struct vk_bvh_geometry_data
vk_fill_geometry_data(VkAccelerationStructureTypeKHR type, uint32_t first_id, uint32_t geom_index,
                      const VkAccelerationStructureGeometryKHR *geometry,
                      const VkAccelerationStructureBuildRangeInfoKHR *build_range_info)
{
   struct vk_bvh_geometry_data data = {
      .first_id = first_id,
      .geometry_id = pack_geometry_id_and_flags(geom_index, geometry->flags),
      .geometry_type = geometry->geometryType,
   };

   switch (geometry->geometryType) {
   case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
      assert(type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR);

      data.data = geometry->geometry.triangles.vertexData.deviceAddress +
                  build_range_info->firstVertex * geometry->geometry.triangles.vertexStride;
      data.indices = geometry->geometry.triangles.indexData.deviceAddress;

      if (geometry->geometry.triangles.indexType == VK_INDEX_TYPE_NONE_KHR)
         data.data += build_range_info->primitiveOffset;
      else
         data.indices += build_range_info->primitiveOffset;

      data.transform = geometry->geometry.triangles.transformData.deviceAddress;
      if (data.transform)
         data.transform += build_range_info->transformOffset;

      data.stride = geometry->geometry.triangles.vertexStride;
      data.vertex_format = geometry->geometry.triangles.vertexFormat;
      data.index_format = geometry->geometry.triangles.indexType;
      break;
   case VK_GEOMETRY_TYPE_AABBS_KHR:
      assert(type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR);

      data.data = geometry->geometry.aabbs.data.deviceAddress + build_range_info->primitiveOffset;
      data.stride = geometry->geometry.aabbs.stride;
      break;
   case VK_GEOMETRY_TYPE_INSTANCES_KHR:
      assert(type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR);

      data.data = geometry->geometry.instances.data.deviceAddress + build_range_info->primitiveOffset;

      if (geometry->geometry.instances.arrayOfPointers)
         data.stride = 8;
      else
         data.stride = sizeof(VkAccelerationStructureInstanceKHR);
      break;
   default:
      UNREACHABLE("Unknown geometryType");
   }

   return data;
}

void
vk_accel_struct_cmd_begin_debug_marker(VkCommandBuffer commandBuffer,
                                       struct vk_acceleration_structure_build_marker *marker)
{
   VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, commandBuffer);
   struct vk_device *device = cmd_buffer->base.device;

   char name[256];
   switch (marker->step) {
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_TOP:
      snprintf(name, sizeof(name), "vkCmdBuildAccelerationStructuresKHR(blas_count=%u, tlas_count=%u)",
               marker->top.blas_count, marker->top.tlas_count);
      break;
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_BUILD_LEAVES:
      snprintf(name, sizeof(name), "build_leaves");
      break;
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_MORTON_GENERATE:
      snprintf(name, sizeof(name), "morton_generate");
      break;
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_MORTON_SORT:
      snprintf(name, sizeof(name), "morton_sort");
      break;
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_LBVH_BUILD_INTERNAL:
      snprintf(name, sizeof(name), "lbvh_build_internal");
      break;
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_PLOC_BUILD_INTERNAL:
      snprintf(name, sizeof(name), "ploc_build_internal");
      break;
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_HPLOC_BUILD_INTERNAL:
      snprintf(name, sizeof(name), "hploc_build_internal");
      break;
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_ENCODE:
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_UPDATE: {
      const char *type = marker->step == VK_ACCELERATION_STRUCTURE_BUILD_STEP_ENCODE ? "encode" : "update";
      snprintf(name, sizeof(name), "%s(pass=%u, key=0x%x, leaf_node_count=%u, internal_node_count=%u)",
               type, marker->encode.pass, marker->encode.key, marker->encode.leaf_node_count,
               marker->encode.internal_node_count);
      break;
   }
   default:
      UNREACHABLE("Invalid build step");
   }

   VkDebugMarkerMarkerInfoEXT marker_info = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT,
      .pMarkerName = name,
   };

   device->dispatch_table.CmdDebugMarkerBeginEXT(commandBuffer, &marker_info);
}

void
vk_accel_struct_cmd_end_debug_marker(VkCommandBuffer commandBuffer,
                                     struct vk_acceleration_structure_build_marker *marker)
{
   VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, commandBuffer);
   struct vk_device *device = cmd_buffer->base.device;

   device->dispatch_table.CmdDebugMarkerEndEXT(commandBuffer);
}

#define VK_BUILD_LEAVES_FLAGS (VK_BUILD_FLAG_ALWAYS_ACTIVE | VK_BUILD_FLAG_PROPAGATE_CULL_FLAGS | VK_BUILD_FLAG_64BIT_KEYS)

static VkResult
build_leaves(VkCommandBuffer commandBuffer, struct vk_device *device,
             struct vk_meta_device *meta,
             const struct vk_acceleration_structure_build_args *args,
             struct vk_acceleration_structure_build_state *states,
             uint32_t build_count, uint32_t build_flags)
{
   VkPipeline pipeline;
   VkPipelineLayout layout;

   /* Many apps are broken and will make inactive primitives active when
    * updating, even though this is disallowed by the spec.  To handle this,
    * we use a different variant for updateable acceleration structures when
    * the driver implements an update pass. This passes through inactive leaf
    * nodes as if they were active, with an empty bounding box. It's then the
    * driver or HW's responsibility to filter out inactive nodes.
    */
   const uint32_t *spirv = leaf_spv;
   size_t spirv_size = sizeof(leaf_spv);

   if (device->as_build_ops->leaf_spirv_override) {
      spirv = device->as_build_ops->leaf_spirv_override;
      spirv_size = device->as_build_ops->leaf_spirv_override_size;
   }

   VkResult result = vk_get_bvh_build_pipeline_spv(device, meta, VK_META_OBJECT_KEY_LEAF,
                                                   spirv, spirv_size, sizeof(struct leaf_args),
                                                   args, build_flags,
                                                   &pipeline,
                                                   true /* unaligned_dispatch */);
   if (result != VK_SUCCESS)
      return result;

   result = vk_get_bvh_build_pipeline_layout(device, meta, sizeof(struct leaf_args), &layout);
   if (result != VK_SUCCESS)
      return result;

   if (args->emit_markers) {
      struct vk_acceleration_structure_build_marker marker = {
         .step = VK_ACCELERATION_STRUCTURE_BUILD_STEP_BUILD_LEAVES,
      };
      device->as_build_ops->begin_debug_marker(commandBuffer, &marker);
   }

   const struct vk_device_dispatch_table *disp = &device->dispatch_table;
   disp->CmdBindPipeline(
      commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   for (uint32_t i = 0; i < build_count; ++i) {
      if (states[i].config.internal_type == VK_INTERNAL_BUILD_TYPE_UPDATE)
         continue;
      if ((states[i].build_flags & VK_BUILD_LEAVES_FLAGS) != build_flags)
         continue;

      const VkAccelerationStructureBuildGeometryInfoKHR *build_info = states[i].build_info;

      uint64_t scratch_addr = build_info->scratchData.deviceAddress;
      struct leaf_args leaf_consts = {
         .bvh = scratch_addr + states[i].scratch.ir_offset,
         .header = scratch_addr + states[i].scratch.header_offset,
         .ids = scratch_addr + states[i].scratch.sort_buffer_offset[0],
      };

      for (unsigned j = 0; j < build_info->geometryCount; ++j) {
         const VkAccelerationStructureGeometryKHR *geom =
            build_info->pGeometries ? &build_info->pGeometries[j] : build_info->ppGeometries[j];

         const VkAccelerationStructureBuildRangeInfoKHR *build_range_info = &states[i].build_range_infos[j];

         if (build_range_info->primitiveCount == 0)
            continue;

         leaf_consts.geom_data = vk_fill_geometry_data(build_info->type, states[i].leaf_node_count, j, geom, build_range_info);

         disp->CmdPushConstants(commandBuffer, layout,
                                VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(leaf_consts), &leaf_consts);
         device->cmd_dispatch_unaligned(commandBuffer, build_range_info->primitiveCount, 1, 1);

         states[i].leaf_node_count += build_range_info->primitiveCount;
      }
   }

   if (args->emit_markers) {
      struct vk_acceleration_structure_build_marker marker = {
         .step = VK_ACCELERATION_STRUCTURE_BUILD_STEP_BUILD_LEAVES,
      };
      device->as_build_ops->end_debug_marker(commandBuffer, &marker);
   }

   return VK_SUCCESS;
}

#define VK_MORTON_GENERATE_FLAGS (VK_BUILD_FLAG_64BIT_KEYS)

static VkResult
morton_generate(VkCommandBuffer commandBuffer, struct vk_device *device,
                struct vk_meta_device *meta,
                const struct vk_acceleration_structure_build_args *args,
                struct vk_acceleration_structure_build_state *states,
                uint32_t build_count, uint32_t build_flags)
{
   VkPipeline pipeline;
   VkPipelineLayout layout;

   VkResult result = vk_get_bvh_build_pipeline_spv(device, meta, VK_META_OBJECT_KEY_MORTON,
                                                   morton_spv, sizeof(morton_spv),
                                                   sizeof(struct morton_args), args,
                                                   build_flags, &pipeline,
                                                   true /* unaligned_dispatch */);
   if (result != VK_SUCCESS)
      return result;

   result = vk_get_bvh_build_pipeline_layout(device, meta, sizeof(struct morton_args), &layout);
   if (result != VK_SUCCESS)
      return result;

   if (args->emit_markers) {
      struct vk_acceleration_structure_build_marker marker = {
         .step = VK_ACCELERATION_STRUCTURE_BUILD_STEP_MORTON_GENERATE,
      };
      device->as_build_ops->begin_debug_marker(commandBuffer, &marker);
   }

   const struct vk_device_dispatch_table *disp = &device->dispatch_table;
   disp->CmdBindPipeline(
      commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   for (uint32_t i = 0; i < build_count; ++i) {
      if (states[i].config.internal_type == VK_INTERNAL_BUILD_TYPE_UPDATE)
         continue;
      if ((states[i].build_flags & VK_MORTON_GENERATE_FLAGS) != build_flags)
         continue;

      uint64_t scratch_addr = states[i].build_info->scratchData.deviceAddress;
      const struct morton_args consts = {
         .bvh = scratch_addr + states[i].scratch.ir_offset,
         .header = scratch_addr + states[i].scratch.header_offset,
         .ids = scratch_addr + states[i].scratch.sort_buffer_offset[0],
      };

      disp->CmdPushConstants(commandBuffer, layout,
                             VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(consts), &consts);
      device->cmd_dispatch_unaligned(commandBuffer, states[i].leaf_node_count, 1, 1);
   }

   if (args->emit_markers) {
      struct vk_acceleration_structure_build_marker marker = {
         .step = VK_ACCELERATION_STRUCTURE_BUILD_STEP_MORTON_GENERATE,
      };
      device->as_build_ops->end_debug_marker(commandBuffer, &marker);
   }

   return VK_SUCCESS;
}

#define VK_MORTON_SORT_FLAGS (VK_BUILD_FLAG_64BIT_KEYS)

static VkResult
morton_sort(VkCommandBuffer commandBuffer, struct vk_device *device,
             struct vk_meta_device *meta,
             const struct vk_acceleration_structure_build_args *args,
             struct vk_acceleration_structure_build_state *states,
             uint32_t build_count, uint32_t build_flags)
{
   const struct vk_device_dispatch_table *disp = &device->dispatch_table;

   if (args->emit_markers) {
      struct vk_acceleration_structure_build_marker marker = {
         .step = VK_ACCELERATION_STRUCTURE_BUILD_STEP_MORTON_SORT,
      };
      device->as_build_ops->begin_debug_marker(commandBuffer, &marker);
   }

   /* Copyright 2019 The Fuchsia Authors. */
   uint32_t key_bits = 24;
   const radix_sort_vk_t *rs = args->radix_sort_64;
   if (build_flags & VK_BUILD_FLAG_64BIT_KEYS) {
      key_bits = 64;
      rs = args->radix_sort_96;
   }

   /*
    * OVERVIEW
    *
    *   1. Pad the keyvals in `scatter_even`.
    *   2. Zero the `histograms` and `partitions`.
    *      --- BARRIER ---
    *   3. HISTOGRAM is dispatched before PREFIX.
    *      --- BARRIER ---
    *   4. PREFIX is dispatched before the first SCATTER.
    *      --- BARRIER ---
    *   5. One or more SCATTER dispatches.
    *
    * Note that the `partitions` buffer can be zeroed anytime before the first
    * scatter.
    */

   /* How many passes? */
   uint32_t keyval_bytes = rs->config.keyval_dwords * (uint32_t)sizeof(uint32_t);
   uint32_t passes = (key_bits + RS_RADIX_LOG2 - 1) / RS_RADIX_LOG2;

   for (uint32_t i = 0; i < build_count; ++i) {
      if (states[i].leaf_node_count)
         states[i].scratch_offset = states[i].scratch.sort_buffer_offset[passes & 1];
      else
         states[i].scratch_offset = states[i].scratch.sort_buffer_offset[0];
   }

   /*
    * PAD KEYVALS AND ZERO HISTOGRAM/PARTITIONS
    *
    * Pad fractional blocks with max-valued keyvals.
    *
    * Zero the histograms and partitions buffer.
    *
    * This assumes the partitions follow the histograms.
    */

   /* FIXME(allanmac): Consider precomputing some of these values and hang them off `rs`. */

   /* How many scatter blocks? */
   uint32_t scatter_wg_size = 1 << rs->config.scatter.workgroup_size_log2;
   uint32_t scatter_block_kvs = scatter_wg_size * rs->config.scatter.block_rows;

   /*
    * How many histogram blocks?
    *
    * Note that it's OK to have more max-valued digits counted by the histogram
    * than sorted by the scatters because the sort is stable.
    */
   uint32_t histo_wg_size = 1 << rs->config.histogram.workgroup_size_log2;
   uint32_t histo_block_kvs = histo_wg_size * rs->config.histogram.block_rows;

   uint32_t pass_idx = (keyval_bytes - passes);

   for (uint32_t i = 0; i < build_count; ++i) {
      if (!states[i].leaf_node_count)
         continue;
      if (states[i].config.internal_type == VK_INTERNAL_BUILD_TYPE_UPDATE)
         continue;
      if ((states[i].build_flags & VK_MORTON_SORT_FLAGS) != build_flags)
         continue;

      uint64_t scratch_addr = states[i].build_info->scratchData.deviceAddress;
      uint64_t keyvals_even_addr = scratch_addr + states[i].scratch.sort_buffer_offset[0];
      uint64_t internal_addr = scratch_addr + states[i].scratch.sort_internal_offset;

      states[i].scatter_blocks = (states[i].leaf_node_count + scatter_block_kvs - 1) / scatter_block_kvs;
      states[i].count_ru_scatter = states[i].scatter_blocks * scatter_block_kvs;

      states[i].histo_blocks = (states[i].count_ru_scatter + histo_block_kvs - 1) / histo_block_kvs;
      states[i].count_ru_histo = states[i].histo_blocks * histo_block_kvs;

      /* Fill with max values */
      if (states[i].count_ru_histo > states[i].leaf_node_count) {
         device->cmd_fill_buffer_addr(commandBuffer, keyvals_even_addr +
                                      states[i].leaf_node_count * keyval_bytes,
                                      (states[i].count_ru_histo - states[i].leaf_node_count) * keyval_bytes,
                                      0xFFFFFFFF);
      }

      /*
       * Zero histograms and invalidate partitions.
       *
       * Note that the partition invalidation only needs to be performed once
       * because the even/odd scatter dispatches rely on the the previous pass to
       * leave the partitions in an invalid state.
       *
       * Note that the last workgroup doesn't read/write a partition so it doesn't
       * need to be initialized.
       */
      uint32_t histo_partition_count = passes + states[i].scatter_blocks - 1;

      uint32_t fill_base = pass_idx * (RS_RADIX_SIZE * sizeof(uint32_t));

      device->cmd_fill_buffer_addr(commandBuffer,
                                   internal_addr + rs->internal.histograms.offset + fill_base,
                                   histo_partition_count * (RS_RADIX_SIZE * sizeof(uint32_t)) + keyval_bytes * sizeof(uint32_t), 0);
   }

   /*
    * Pipeline: HISTOGRAM
    *
    * TODO(allanmac): All subgroups should try to process approximately the same
    * number of blocks in order to minimize tail effects.  This was implemented
    * and reverted but should be reimplemented and benchmarked later.
    */
   vk_barrier_transfer_w_to_compute_r(commandBuffer);

   disp->CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                         rs->pipelines.named.histogram);

   for (uint32_t i = 0; i < build_count; ++i) {
      if (!states[i].leaf_node_count)
         continue;
      if (states[i].config.internal_type == VK_INTERNAL_BUILD_TYPE_UPDATE)
         continue;
      if ((states[i].build_flags & VK_MORTON_SORT_FLAGS) != build_flags)
         continue;

      uint64_t scratch_addr = states[i].build_info->scratchData.deviceAddress;
      uint64_t keyvals_even_addr = scratch_addr + states[i].scratch.sort_buffer_offset[0];
      uint64_t internal_addr = scratch_addr + states[i].scratch.sort_internal_offset;

      /* Dispatch histogram */
      struct rs_push_histogram push_histogram = {
         .devaddr_histograms = internal_addr + rs->internal.histograms.offset,
         .devaddr_keyvals = keyvals_even_addr,
         .passes = passes,
      };

      disp->CmdPushConstants(commandBuffer, rs->pipeline_layouts.named.histogram, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                             sizeof(push_histogram), &push_histogram);

      disp->CmdDispatch(commandBuffer, states[i].histo_blocks, 1, 1);
   }

   /*
    * Pipeline: PREFIX
    *
    * Launch one workgroup per pass.
    */
   vk_barrier_compute_w_to_compute_r(commandBuffer);

   disp->CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                         rs->pipelines.named.prefix);

   for (uint32_t i = 0; i < build_count; ++i) {
      if (!states[i].leaf_node_count)
         continue;
      if (states[i].config.internal_type == VK_INTERNAL_BUILD_TYPE_UPDATE)
         continue;
      if ((states[i].build_flags & VK_MORTON_SORT_FLAGS) != build_flags)
         continue;

      uint64_t internal_addr = states[i].build_info->scratchData.deviceAddress +
                               states[i].scratch.sort_internal_offset;

      struct rs_push_prefix push_prefix = {
         .devaddr_histograms = internal_addr + rs->internal.histograms.offset,
      };

      disp->CmdPushConstants(commandBuffer, rs->pipeline_layouts.named.prefix, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                             sizeof(push_prefix), &push_prefix);

      disp->CmdDispatch(commandBuffer, passes, 1, 1);
   }

   /* Pipeline: SCATTER */
   vk_barrier_compute_w_to_compute_r(commandBuffer);

   uint32_t histogram_offset = pass_idx * (RS_RADIX_SIZE * sizeof(uint32_t));

   for (uint32_t i = 0; i < build_count; i++) {
      uint64_t scratch_addr = states[i].build_info->scratchData.deviceAddress;
      uint64_t keyvals_even_addr = scratch_addr + states[i].scratch.sort_buffer_offset[0];
      uint64_t keyvals_odd_addr = scratch_addr + states[i].scratch.sort_buffer_offset[1];
      uint64_t internal_addr = scratch_addr + states[i].scratch.sort_internal_offset;

      states[i].push_scatter = (struct rs_push_scatter){
         .devaddr_keyvals_even = keyvals_even_addr,
         .devaddr_keyvals_odd = keyvals_odd_addr,
         .devaddr_partitions = internal_addr + rs->internal.partitions.offset,
         .devaddr_histograms = internal_addr + rs->internal.histograms.offset + histogram_offset,
      };
   }

   bool is_even = true;

   while (true) {
      uint32_t pass_dword = pass_idx / 4;

      /* Bind new pipeline */
      VkPipeline p =
         is_even ? rs->pipelines.named.scatter[pass_dword].even : rs->pipelines.named.scatter[pass_dword].odd;
      disp->CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, p);

      /* Update push constants that changed */
      VkPipelineLayout pl = is_even ? rs->pipeline_layouts.named.scatter[pass_dword].even
                                    : rs->pipeline_layouts.named.scatter[pass_dword].odd;

      for (uint32_t i = 0; i < build_count; i++) {
         if (!states[i].leaf_node_count)
            continue;
         if (states[i].config.internal_type == VK_INTERNAL_BUILD_TYPE_UPDATE)
            continue;
         if ((states[i].build_flags & VK_MORTON_SORT_FLAGS) != build_flags)
            continue;

         states[i].push_scatter.pass_offset = (pass_idx & 3) * RS_RADIX_LOG2;

         disp->CmdPushConstants(commandBuffer, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(struct rs_push_scatter),
                                &states[i].push_scatter);

         disp->CmdDispatch(commandBuffer, states[i].scatter_blocks, 1, 1);

         states[i].push_scatter.devaddr_histograms += (RS_RADIX_SIZE * sizeof(uint32_t));
      }

      /* Continue? */
      if (++pass_idx >= keyval_bytes)
         break;

      vk_barrier_compute_w_to_compute_r(commandBuffer);

      is_even ^= true;
   }

   if (args->emit_markers) {
      struct vk_acceleration_structure_build_marker marker = {
         .step = VK_ACCELERATION_STRUCTURE_BUILD_STEP_MORTON_SORT,
      };
      device->as_build_ops->end_debug_marker(commandBuffer, &marker);
   }

   return VK_SUCCESS;
}

#define VK_LBVH_BUILD_INTERNAL_FLAGS (VK_BUILD_FLAG_PROPAGATE_CULL_FLAGS | VK_BUILD_FLAG_64BIT_KEYS)

static VkResult
lbvh_build_internal(VkCommandBuffer commandBuffer, struct vk_device *device,
                    struct vk_meta_device *meta,
                    const struct vk_acceleration_structure_build_args *args,
                    struct vk_acceleration_structure_build_state *states,
                    uint32_t build_count, uint32_t build_flags)
{
   VkPipeline pipeline;
   VkPipelineLayout layout;

   VkResult result = vk_get_bvh_build_pipeline_spv(device, meta, VK_META_OBJECT_KEY_LBVH_MAIN,
                                                   lbvh_main_spv, sizeof(lbvh_main_spv),
                                                   sizeof(struct lbvh_main_args), args, build_flags,
                                                   &pipeline,
                                                   true /* unaligned_dispatch */);
   if (result != VK_SUCCESS)
      return result;

   result = vk_get_bvh_build_pipeline_layout(device, meta, sizeof(struct lbvh_main_args), &layout);
   if (result != VK_SUCCESS)
      return result;

   if (args->emit_markers) {
      struct vk_acceleration_structure_build_marker marker = {
         .step = VK_ACCELERATION_STRUCTURE_BUILD_STEP_LBVH_BUILD_INTERNAL,
      };
      device->as_build_ops->begin_debug_marker(commandBuffer, &marker);
   }

   const struct vk_device_dispatch_table *disp = &device->dispatch_table;
   disp->CmdBindPipeline(
      commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   for (uint32_t i = 0; i < build_count; ++i) {
      if (states[i].config.internal_type != VK_INTERNAL_BUILD_TYPE_LBVH)
         continue;
      if ((states[i].build_flags & VK_LBVH_BUILD_INTERNAL_FLAGS) != build_flags)
         continue;

      uint32_t src_scratch_offset = states[i].scratch_offset;

      uint64_t scratch_addr = states[i].build_info->scratchData.deviceAddress;
      const struct lbvh_main_args consts = {
         .bvh = scratch_addr + states[i].scratch.ir_offset,
         .src_ids = scratch_addr + src_scratch_offset,
         .node_info = scratch_addr + states[i].scratch.lbvh_node_offset,
         .header = scratch_addr + states[i].scratch.header_offset,
         .internal_node_base = states[i].scratch.internal_node_offset - states[i].scratch.ir_offset,
      };

      disp->CmdPushConstants(commandBuffer, layout,
                             VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(consts), &consts);
      device->cmd_dispatch_unaligned(commandBuffer, states[i].internal_node_count, 1, 1);
   }

   vk_barrier_compute_w_to_compute_r(commandBuffer);

   result = vk_get_bvh_build_pipeline_spv(device, meta, VK_META_OBJECT_KEY_LBVH_GENERATE_IR,
                                          lbvh_generate_ir_spv, sizeof(lbvh_generate_ir_spv),
                                          sizeof(struct lbvh_generate_ir_args), args, build_flags,
                                          &pipeline, true /* unaligned_dispatch */);
   if (result != VK_SUCCESS)
      return result;

   result = vk_get_bvh_build_pipeline_layout(device, meta, sizeof(struct lbvh_generate_ir_args), &layout);
   if (result != VK_SUCCESS)
      return result;

   disp->CmdBindPipeline(
      commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   for (uint32_t i = 0; i < build_count; ++i) {
      if (states[i].config.internal_type != VK_INTERNAL_BUILD_TYPE_LBVH)
         continue;
      if ((states[i].build_flags & VK_LBVH_BUILD_INTERNAL_FLAGS) != build_flags)
         continue;

      uint64_t scratch_addr = states[i].build_info->scratchData.deviceAddress;
      const struct lbvh_generate_ir_args consts = {
         .bvh = scratch_addr + states[i].scratch.ir_offset,
         .node_info = scratch_addr + states[i].scratch.lbvh_node_offset,
         .header = scratch_addr + states[i].scratch.header_offset,
         .internal_node_base = states[i].scratch.internal_node_offset - states[i].scratch.ir_offset,
      };

      disp->CmdPushConstants(commandBuffer, layout,
                             VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(consts), &consts);
      device->cmd_dispatch_unaligned(commandBuffer, states[i].internal_node_count, 1, 1);
   }

   if (args->emit_markers) {
      struct vk_acceleration_structure_build_marker marker = {
         .step = VK_ACCELERATION_STRUCTURE_BUILD_STEP_LBVH_BUILD_INTERNAL,
      };
      device->as_build_ops->end_debug_marker(commandBuffer, &marker);
   }

   return VK_SUCCESS;
}

#define VK_PLOC_BUILD_INTERNAL_FLAGS (VK_BUILD_FLAG_PROPAGATE_CULL_FLAGS | VK_BUILD_FLAG_64BIT_KEYS)

static VkResult
ploc_build_internal(VkCommandBuffer commandBuffer, struct vk_device *device,
                    struct vk_meta_device *meta,
                    const struct vk_acceleration_structure_build_args *args,
                    struct vk_acceleration_structure_build_state *states,
                    uint32_t build_count, uint32_t build_flags)
{
   VkPipeline pipeline;
   VkPipelineLayout layout;

   VkResult result = vk_get_bvh_build_pipeline_spv(device, meta, VK_META_OBJECT_KEY_PLOC, ploc_spv,
                                                   sizeof(ploc_spv), sizeof(struct ploc_args),
                                                   args, build_flags, &pipeline,
                                                   false /* unaligned_dispatch */);
   if (result != VK_SUCCESS)
      return result;

   result = vk_get_bvh_build_pipeline_layout(device, meta, sizeof(struct ploc_args), &layout);
   if (result != VK_SUCCESS)
      return result;

   if (args->emit_markers) {
      struct vk_acceleration_structure_build_marker marker = {
         .step = VK_ACCELERATION_STRUCTURE_BUILD_STEP_PLOC_BUILD_INTERNAL,
      };
      device->as_build_ops->begin_debug_marker(commandBuffer, &marker);
   }

   const struct vk_device_dispatch_table *disp = &device->dispatch_table;
   disp->CmdBindPipeline(
      commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   for (uint32_t i = 0; i < build_count; ++i) {
      if (states[i].config.internal_type != VK_INTERNAL_BUILD_TYPE_PLOC)
         continue;
      if ((states[i].build_flags & VK_PLOC_BUILD_INTERNAL_FLAGS) != build_flags)
         continue;

      uint32_t src_scratch_offset = states[i].scratch_offset;
      uint32_t dst_scratch_offset = (src_scratch_offset == states[i].scratch.sort_buffer_offset[0])
                                       ? states[i].scratch.sort_buffer_offset[1]
                                       : states[i].scratch.sort_buffer_offset[0];

      uint64_t scratch_addr = states[i].build_info->scratchData.deviceAddress;
      const struct ploc_args consts = {
         .bvh = scratch_addr + states[i].scratch.ir_offset,
         .header = scratch_addr + states[i].scratch.header_offset,
         .ids_0 = scratch_addr + src_scratch_offset,
         .ids_1 = scratch_addr + dst_scratch_offset,
         .prefix_scan_partitions = scratch_addr + states[i].scratch.ploc_prefix_sum_partition_offset,
         .internal_node_offset = states[i].scratch.internal_node_offset - states[i].scratch.ir_offset,
      };

      disp->CmdPushConstants(commandBuffer, layout,
                             VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(consts), &consts);
      disp->CmdDispatch(commandBuffer, MAX2(DIV_ROUND_UP(states[i].leaf_node_count, PLOC_WORKGROUP_SIZE), 1), 1, 1);
   }

   if (args->emit_markers) {
      struct vk_acceleration_structure_build_marker marker = {
         .step = VK_ACCELERATION_STRUCTURE_BUILD_STEP_PLOC_BUILD_INTERNAL,
      };
      device->as_build_ops->end_debug_marker(commandBuffer, &marker);
   }

   return VK_SUCCESS;
}

#define VK_HPLOC_BUILD_INTERNAL_FLAGS (VK_BUILD_FLAG_PROPAGATE_CULL_FLAGS | VK_BUILD_FLAG_64BIT_KEYS)

static VkResult
hploc_build_internal(VkCommandBuffer commandBuffer, struct vk_device *device,
                     struct vk_meta_device *meta,
                     const struct vk_acceleration_structure_build_args *args,
                     struct vk_acceleration_structure_build_state *states,
                     uint32_t build_count, uint32_t build_flags)
{
   VkPipeline pipeline;
   VkPipelineLayout layout;

   VkResult result = vk_get_bvh_build_pipeline_spv(device, meta, VK_META_OBJECT_KEY_HPLOC, hploc_spv,
                                                   sizeof(hploc_spv), sizeof(struct hploc_args),
                                                   args, build_flags, &pipeline,
                                                   false /* unaligned_dispatch */);
   if (result != VK_SUCCESS)
      return result;

   result = vk_get_bvh_build_pipeline_layout(device, meta, sizeof(struct hploc_args), &layout);
   if (result != VK_SUCCESS)
      return result;

   if (args->emit_markers) {
      struct vk_acceleration_structure_build_marker marker = {
         .step = VK_ACCELERATION_STRUCTURE_BUILD_STEP_HPLOC_BUILD_INTERNAL,
      };
      device->as_build_ops->begin_debug_marker(commandBuffer, &marker);
   }

   const struct vk_device_dispatch_table *disp = &device->dispatch_table;
   disp->CmdBindPipeline(
      commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   for (uint32_t i = 0; i < build_count; ++i) {
      if (states[i].config.internal_type != VK_INTERNAL_BUILD_TYPE_HPLOC)
         continue;
      if ((states[i].build_flags & VK_HPLOC_BUILD_INTERNAL_FLAGS) != build_flags)
         continue;

      assert(args->subgroup_size <= 64);

      uint64_t scratch_addr = states[i].build_info->scratchData.deviceAddress;
      const struct hploc_args consts = {
         .header = scratch_addr + states[i].scratch.header_offset,
         .bvh = scratch_addr + states[i].scratch.ir_offset,
         .ranges = scratch_addr + states[i].scratch.hploc_ranges_offset,
         .ids = scratch_addr + states[i].scratch_offset,
         .internal_node_base = states[i].scratch.internal_node_offset - states[i].scratch.ir_offset,
      };

      disp->CmdPushConstants(commandBuffer, layout,
                             VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(consts), &consts);
      disp->CmdDispatch(commandBuffer, MAX2(DIV_ROUND_UP(states[i].leaf_node_count, args->subgroup_size), 1), 1, 1);
   }

   if (args->emit_markers) {
      struct vk_acceleration_structure_build_marker marker = {
         .step = VK_ACCELERATION_STRUCTURE_BUILD_STEP_HPLOC_BUILD_INTERNAL,
      };
      device->as_build_ops->end_debug_marker(commandBuffer, &marker);
   }

   return VK_SUCCESS;
}

typedef VkResult (*vk_build_stage_cb)(VkCommandBuffer commandBuffer, struct vk_device *device,
                                      struct vk_meta_device *meta,
                                      const struct vk_acceleration_structure_build_args *args,
                                      struct vk_acceleration_structure_build_state *states,
                                      uint32_t build_count, uint32_t build_flags);

static VkResult
vk_build_stage(vk_build_stage_cb cb, VkCommandBuffer commandBuffer, struct vk_device *device,
               struct vk_meta_device *meta, const struct vk_acceleration_structure_build_args *args,
               struct vk_acceleration_structure_build_state *states, uint32_t build_count,
               uint32_t build_flags_mask)
{
   BITSET_DECLARE(flag_combinations, 1u << VK_BUILD_FLAG_COUNT);
   BITSET_ZERO(flag_combinations);
   for (uint32_t i = 0; i < build_count; i++)
      BITSET_SET(flag_combinations, states[i].build_flags & build_flags_mask);

   uint32_t build_flags;
   BITSET_FOREACH_SET(build_flags, flag_combinations, 1u << VK_BUILD_FLAG_COUNT) {
      VkResult result = cb(commandBuffer, device, meta, args, states, build_count, build_flags);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

void
vk_cmd_build_acceleration_structures(VkCommandBuffer commandBuffer,
                                     struct vk_device *device,
                                     struct vk_meta_device *meta,
                                     uint32_t infoCount,
                                     const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
                                     const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos,
                                     const struct vk_acceleration_structure_build_args *args)
{
   VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, commandBuffer);
   const struct vk_acceleration_structure_build_ops *ops = device->as_build_ops;

   struct bvh_batch_state batch_state = {0};

   struct vk_acceleration_structure_build_state *states = calloc(infoCount, sizeof(struct vk_acceleration_structure_build_state));

   struct vk_acceleration_structure_build_marker top_marker = {
      .step = VK_ACCELERATION_STRUCTURE_BUILD_STEP_TOP,
   };
   if (args->emit_markers) {
      for (uint32_t i = 0; i < infoCount; ++i) {
         switch (pInfos[i].type) {
         case VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR:
            top_marker.top.tlas_count++;
            break;
         case VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR:
            top_marker.top.blas_count++;
            break;
         default:
            break;
         }
      }
      ops->begin_debug_marker(commandBuffer, &top_marker);
   }

   for (uint32_t i = 0; i < infoCount; ++i) {
      uint32_t leaf_node_count = 0;
      for (uint32_t j = 0; j < pInfos[i].geometryCount; ++j) {
         leaf_node_count += ppBuildRangeInfos[i][j].primitiveCount;
      }

      vk_acceleration_structure_build_state_init(&states[i], cmd_buffer->base.device, leaf_node_count,
                                                 pInfos + i, args);

      states[i].build_range_infos = ppBuildRangeInfos[i];
      /* The leaf node dispatch code uses leaf_node_count as a base index. */
      states[i].leaf_node_count = 0;

      if (states[i].config.updateable)
         batch_state.any_updateable = true;
      else
         batch_state.any_non_updateable = true;

      if (states[i].config.internal_type == VK_INTERNAL_BUILD_TYPE_PLOC) {
         batch_state.any_ploc = true;
      } else if (states[i].config.internal_type == VK_INTERNAL_BUILD_TYPE_HPLOC) {
         batch_state.any_hploc = true;
      } else if (states[i].config.internal_type == VK_INTERNAL_BUILD_TYPE_LBVH) {
         batch_state.any_lbvh = true;
      } else if (states[i].config.internal_type == VK_INTERNAL_BUILD_TYPE_UPDATE) {
         batch_state.any_update = true;
         /* For updates, the leaf node pass never runs, so set leaf_node_count here. */
         states[i].leaf_node_count = leaf_node_count;
      } else {
         UNREACHABLE("Unknown internal_build_type");
      }

      if (states[i].config.updateable)
         states[i].build_flags |= VK_BUILD_FLAG_ALWAYS_ACTIVE;
      if (args->propagate_cull_flags)
         states[i].build_flags |= VK_BUILD_FLAG_PROPAGATE_CULL_FLAGS;
      if (states[i].config.u64_keys)
         states[i].build_flags |= VK_BUILD_FLAG_64BIT_KEYS;

      if (states[i].config.internal_type != VK_INTERNAL_BUILD_TYPE_UPDATE) {
         /* The internal node count is updated in lbvh_build_internal for LBVH
          * and from the PLOC shader for PLOC. */
         struct vk_ir_header header = {
            .min_bounds = {0x7fffffff, 0x7fffffff, 0x7fffffff},
            .max_bounds = {0x80000000, 0x80000000, 0x80000000},
            .dispatch_size_y = 1,
            .dispatch_size_z = 1,
            .sync_data =
               {
                  .current_phase_end_counter = TASK_INDEX_INVALID,
                  /* Will be updated by the first PLOC shader invocation */
                  .task_counts = {TASK_INDEX_INVALID, TASK_INDEX_INVALID},
               },
         };

         device->write_buffer_cp(commandBuffer, pInfos[i].scratchData.deviceAddress + states[i].scratch.header_offset,
                                 &header, sizeof(header));
      }
   }

   if (batch_state.any_update)
      ops->init_update_scratch(commandBuffer, states, infoCount);

   bool flushed_compute_after_init_update_scratch = false;
   bool flushed_cp_after_init_update_scratch = true;

   /* Wait for the write_buffer_cp to land before using in compute shaders */
   device->flush_buffer_write_cp(commandBuffer);
   device->dispatch_table.CmdPipelineBarrier(commandBuffer,
                                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                             0, /* dependencyFlags */
                                             1,
                                             &(VkMemoryBarrier) {
                                                .srcAccessMask = 0,
                                                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                                             }, 0, NULL, 0, NULL);

   if (batch_state.any_lbvh || batch_state.any_ploc || batch_state.any_hploc) {
      VkResult result = vk_build_stage(build_leaves, commandBuffer, device, meta, args, states, infoCount,
                                       VK_BUILD_LEAVES_FLAGS);
      if (result != VK_SUCCESS) {
         free(states);
         vk_command_buffer_set_error(cmd_buffer, result);
         return;
      }

      if (batch_state.any_hploc) {
         for (uint32_t i = 0; i < infoCount; ++i) {
            if (states[i].config.internal_type == VK_INTERNAL_BUILD_TYPE_HPLOC) {
               device->cmd_fill_buffer_addr(commandBuffer, pInfos[i].scratchData.deviceAddress + states[i].scratch.hploc_ranges_offset,
                                            sizeof(uint32_t) * states[i].internal_node_count, 0xffffffff);
            }
         }
         vk_barrier_transfer_w_to_compute_r(commandBuffer);
      }

      vk_barrier_compute_w_to_compute_r(commandBuffer);

      result = vk_build_stage(morton_generate, commandBuffer, device, meta, args, states, infoCount, VK_MORTON_GENERATE_FLAGS);
      if (result != VK_SUCCESS) {
         free(states);
         vk_command_buffer_set_error(cmd_buffer, result);
         return;
      }

      vk_barrier_compute_w_to_compute_r(commandBuffer);

      vk_build_stage(morton_sort, commandBuffer, device, meta, args, states, infoCount, VK_MORTON_SORT_FLAGS);

      vk_barrier_compute_w_to_compute_r(commandBuffer);

      if (batch_state.any_lbvh) {
         result = vk_build_stage(lbvh_build_internal, commandBuffer, device, meta, args, states, infoCount,
                                 VK_LBVH_BUILD_INTERNAL_FLAGS);
         if (result != VK_SUCCESS) {
            free(states);
            vk_command_buffer_set_error(cmd_buffer, result);
            return;
         }
      }

      if (batch_state.any_ploc) {
         result = vk_build_stage(ploc_build_internal, commandBuffer, device, meta, args, states, infoCount,
                                 VK_PLOC_BUILD_INTERNAL_FLAGS);
         if (result != VK_SUCCESS) {
            vk_command_buffer_set_error(cmd_buffer, result);
            return;
         }
      }

      if (batch_state.any_hploc) {
         result = vk_build_stage(hploc_build_internal, commandBuffer, device, meta, args, states, infoCount,
                                 VK_HPLOC_BUILD_INTERNAL_FLAGS);
         if (result != VK_SUCCESS) {
            vk_command_buffer_set_error(cmd_buffer, result);
            return;
         }
      }

      vk_barrier_compute_w_to_compute_r(commandBuffer);
      vk_barrier_compute_w_to_indirect_compute_r(commandBuffer);
      flushed_compute_after_init_update_scratch = true;
   }

   struct vk_acceleration_structure_build_marker encode_marker;
   bool inside_encode_marker = false;

   for (unsigned pass = 0; pass < ARRAY_SIZE(ops->encode_as); pass++) {
      if (!ops->encode_as[pass] && !ops->update_as[pass])
         break;

      bool progress;
      do {
         progress = false;

         bool update;
         uint32_t encode_key = 0;
         uint32_t update_key = 0;
         for (uint32_t i = 0; i < infoCount; ++i) {
            if (states[i].last_encode_pass == pass + 1)
               continue;

            if (!progress) {
               update = (states[i].config.internal_type ==
                         VK_INTERNAL_BUILD_TYPE_UPDATE);
               if (update && !ops->update_as[pass])
                  continue;
               if (!update && !ops->encode_as[pass])
                  continue;
               encode_key = states[i].config.encode_key[pass];
               update_key = states[i].config.update_key[pass];
               progress = true;

               if (args->emit_markers) {
                  if (inside_encode_marker)
                     device->as_build_ops->end_debug_marker(commandBuffer, &encode_marker);

                  memset(&encode_marker, 0, sizeof(encode_marker));
                  encode_marker.step = update ? VK_ACCELERATION_STRUCTURE_BUILD_STEP_UPDATE
                                              : VK_ACCELERATION_STRUCTURE_BUILD_STEP_ENCODE;
                  encode_marker.encode.pass = pass;
                  encode_marker.encode.key = update ? update_key : encode_key;

                  for (uint32_t j = 0; j < infoCount; j++) {
                     if (update != (states[j].config.internal_type ==
                                    VK_INTERNAL_BUILD_TYPE_UPDATE) ||
                         encode_key != states[j].config.encode_key[pass] ||
                         update_key != states[j].config.update_key[pass])
                        continue;

                     encode_marker.encode.leaf_node_count += states[j].leaf_node_count;
                     encode_marker.encode.internal_node_count += states[i].internal_node_count;
                  }

                  device->as_build_ops->begin_debug_marker(commandBuffer, &encode_marker);

                  inside_encode_marker = true;
               }

               if (update) {
                  ops->update_prepare[pass](commandBuffer, &states[i],
                                            flushed_cp_after_init_update_scratch,
                                            flushed_compute_after_init_update_scratch);
               } else {
                  ops->encode_prepare[pass](commandBuffer, &states[i]);
               }
            } else {
               if (update != (states[i].config.internal_type ==
                              VK_INTERNAL_BUILD_TYPE_UPDATE) ||
                   encode_key != states[i].config.encode_key[pass] ||
                   update_key != states[i].config.update_key[pass])
                  continue;
            }

            if (update)
               ops->update_as[pass](commandBuffer, &states[i]);
            else
               ops->encode_as[pass](commandBuffer, &states[i]);

            states[i].last_encode_pass = pass + 1;
         }
      } while (progress);
   }

   if (inside_encode_marker)
      device->as_build_ops->end_debug_marker(commandBuffer, &encode_marker);

   if (args->emit_markers)
      device->as_build_ops->end_debug_marker(commandBuffer, &top_marker);

   free(states);
}

void
vk_get_as_build_sizes(VkDevice _device, VkAccelerationStructureBuildTypeKHR buildType,
                      const VkAccelerationStructureBuildGeometryInfoKHR *pBuildInfo,
                      const uint32_t *pMaxPrimitiveCounts,
                      VkAccelerationStructureBuildSizesInfoKHR *pSizeInfo,
                      const struct vk_acceleration_structure_build_args *args)
{
   VK_FROM_HANDLE(vk_device, device, _device);

   uint32_t leaf_count = 0;
   for (uint32_t i = 0; i < pBuildInfo->geometryCount; i++)
      leaf_count += pMaxPrimitiveCounts[i];

   struct vk_acceleration_structure_build_state state = { 0 };
   vk_acceleration_structure_build_state_init(&state, device, leaf_count, pBuildInfo, args);

   pSizeInfo->accelerationStructureSize = device->as_build_ops->get_as_size(_device, &state);
   pSizeInfo->updateScratchSize = state.scratch.update_size;
   pSizeInfo->buildScratchSize = state.scratch.size;
}

/* Return true if the common framework supports using this format for loading
 * vertices. Must match the formats handled by load_vertices() on the GPU.
 */
bool
vk_acceleration_struct_vtx_format_supported(VkFormat format)
{
   switch (format) {
   case VK_FORMAT_R32G32_SFLOAT:
   case VK_FORMAT_R32G32B32_SFLOAT:
   case VK_FORMAT_R32G32B32A32_SFLOAT:
   case VK_FORMAT_R16G16_SFLOAT:
   case VK_FORMAT_R16G16B16_SFLOAT:
   case VK_FORMAT_R16G16B16A16_SFLOAT:
   case VK_FORMAT_R16G16_SNORM:
   case VK_FORMAT_R16G16_UNORM:
   case VK_FORMAT_R16G16B16A16_SNORM:
   case VK_FORMAT_R16G16B16A16_UNORM:
   case VK_FORMAT_R8G8_SNORM:
   case VK_FORMAT_R8G8_UNORM:
   case VK_FORMAT_R8G8B8A8_SNORM:
   case VK_FORMAT_R8G8B8A8_UNORM:
   case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
      return true;
   default:
      return false;
   }
}

/* Stubs of optional functions for drivers that don't implment them. */

VKAPI_ATTR void VKAPI_CALL
vk_common_CmdBuildAccelerationStructuresIndirectKHR(VkCommandBuffer commandBuffer,
                                                    uint32_t infoCount,
                                                    const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
                                                    const VkDeviceAddress *pIndirectDeviceAddresses,
                                                    const uint32_t *pIndirectStrides,
                                                    const uint32_t *const *ppMaxPrimitiveCounts)
{
   UNREACHABLE("Unimplemented");
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_WriteAccelerationStructuresPropertiesKHR(VkDevice _device, uint32_t accelerationStructureCount,
                                                   const VkAccelerationStructureKHR *pAccelerationStructures,
                                                   VkQueryType queryType,
                                                   size_t dataSize,
                                                   void *pData,
                                                   size_t stride)
{
   VK_FROM_HANDLE(vk_device, device, _device);
   UNREACHABLE("Unimplemented");
   return vk_error(device, VK_ERROR_FEATURE_NOT_PRESENT);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_BuildAccelerationStructuresKHR(VkDevice _device,
                                         VkDeferredOperationKHR deferredOperation,
                                         uint32_t infoCount,
                                         const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
                                         const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
{
   VK_FROM_HANDLE(vk_device, device, _device);
   UNREACHABLE("Unimplemented");
   return vk_error(device, VK_ERROR_FEATURE_NOT_PRESENT);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_CopyAccelerationStructureKHR(VkDevice _device,
                                       VkDeferredOperationKHR deferredOperation,
                                       const VkCopyAccelerationStructureInfoKHR *pInfo)
{
   VK_FROM_HANDLE(vk_device, device, _device);
   UNREACHABLE("Unimplemented");
   return vk_error(device, VK_ERROR_FEATURE_NOT_PRESENT);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_CopyMemoryToAccelerationStructureKHR(VkDevice _device,
                                               VkDeferredOperationKHR deferredOperation,
                                               const VkCopyMemoryToAccelerationStructureInfoKHR *pInfo)
{
   VK_FROM_HANDLE(vk_device, device, _device);
   UNREACHABLE("Unimplemented");
   return vk_error(device, VK_ERROR_FEATURE_NOT_PRESENT);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_CopyAccelerationStructureToMemoryKHR(VkDevice _device,
                                               VkDeferredOperationKHR deferredOperation,
                                               const VkCopyAccelerationStructureToMemoryInfoKHR *pInfo)
{
   VK_FROM_HANDLE(vk_device, device, _device);
   UNREACHABLE("Unimplemented");
   return vk_error(device, VK_ERROR_FEATURE_NOT_PRESENT);
}
