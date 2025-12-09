/*
 * Copyright © 2021 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#include "meta/radv_meta.h"
#include "radv_cs.h"
#include "radv_entrypoints.h"

#include "radix_sort/common/vk/barrier.h"
#include "radix_sort/radix_sort_u64.h"

#include "bvh/build_interface.h"
#include "bvh/bvh.h"

#include "vk_acceleration_structure.h"
#include "vk_common_entrypoints.h"

static const uint32_t copy_blas_addrs_gfx12_spv[] = {
#include "bvh/copy_blas_addrs_gfx12.spv.h"
};

static const uint32_t copy_spv[] = {
#include "bvh/copy.spv.h"
};

static const uint32_t encode_spv[] = {
#include "bvh/encode.spv.h"
};

static const uint32_t encode_gfx12_spv[] = {
#include "bvh/encode_gfx12.spv.h"
};

static const uint32_t encode_triangles_gfx12_spv[] = {
#include "bvh/encode_triangles_gfx12.spv.h"
};

static const uint32_t header_spv[] = {
#include "bvh/header.spv.h"
};

static const uint32_t update_spv[] = {
#include "bvh/update.spv.h"
};

static const uint32_t update_gfx12_spv[] = {
#include "bvh/update_gfx12.spv.h"
};

static const uint32_t leaf_spv[] = {
#include "bvh/radv_leaf.spv.h"
};

#define RADV_OFFSET_UNUSED 0xffffffff

struct acceleration_structure_layout {
   uint32_t geometry_info_offset;
   uint32_t leaf_node_offsets_offset;
   uint32_t bvh_offset;
   uint32_t leaf_nodes_offset;
   uint32_t internal_nodes_offset;
   uint32_t size;
};

struct update_scratch_layout {
   uint32_t geometry_data_offset;
   uint32_t bounds_offsets;
   uint32_t internal_ready_count_offset;
   uint32_t size;
};

enum radv_encode_key_bits {
   RADV_ENCODE_KEY_WRITE_LEAF_NODE_OFFSETS = (1 << 0),
   RADV_ENCODE_KEY_PAIR_COMPRESS_GFX12 = (1 << 1),
   RADV_ENCODE_KEY_BATCH_COMPRESS_GFX12 = (1 << 2),
};

static void
radv_get_acceleration_structure_layout(struct radv_device *device,
                                       const struct vk_acceleration_structure_build_state *state,
                                       struct acceleration_structure_layout *accel_struct)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   uint32_t internal_node_max_child_count = radv_use_bvh8(pdev) ? 8 : 4;
   /* There are no internal nodes with only one child node except the root node which does't matter here. */
   uint32_t last_internal_node_min_child_count = 2;
   /* With pair compression on GFX12, internal nodes with two triangles are always collapsed so they don't exist. the
    * minimum child count therefore has to be 3.
    */
   if (state->config.encode_key[0] & (RADV_ENCODE_KEY_PAIR_COMPRESS_GFX12 | RADV_ENCODE_KEY_BATCH_COMPRESS_GFX12))
      last_internal_node_min_child_count = 3;

   /* See CalcAccelStructInternalNodeCount (gpurt). */
   uint32_t internal_count = (state->leaf_node_count * internal_node_max_child_count) /
                                (last_internal_node_min_child_count * (internal_node_max_child_count - 1)) +
                             1;

   VkGeometryTypeKHR geometry_type = vk_get_as_geometry_type(state->build_info);

   uint32_t bvh_leaf_size;
   if (radv_use_bvh8(pdev)) {
      switch (geometry_type) {
      case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
         bvh_leaf_size = sizeof(struct radv_gfx12_primitive_node);
         break;
      case VK_GEOMETRY_TYPE_AABBS_KHR:
         bvh_leaf_size = sizeof(struct radv_gfx12_primitive_node);
         break;
      case VK_GEOMETRY_TYPE_INSTANCES_KHR:
         bvh_leaf_size = sizeof(struct radv_gfx12_instance_node) + sizeof(struct radv_gfx12_instance_node_user_data);
         break;
      default:
         UNREACHABLE("Unknown VkGeometryTypeKHR");
      }
   } else {
      switch (geometry_type) {
      case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
         bvh_leaf_size = sizeof(struct radv_bvh_triangle_node);
         break;
      case VK_GEOMETRY_TYPE_AABBS_KHR:
         bvh_leaf_size = sizeof(struct radv_bvh_aabb_node);
         break;
      case VK_GEOMETRY_TYPE_INSTANCES_KHR:
         bvh_leaf_size = sizeof(struct radv_bvh_instance_node);
         break;
      default:
         UNREACHABLE("Unknown VkGeometryTypeKHR");
      }
   }

   uint32_t internal_node_size =
      radv_use_bvh8(pdev) ? sizeof(struct radv_gfx12_box_node) : sizeof(struct radv_bvh_box32_node);

   uint32_t hw_leaf_node_count = state->leaf_node_count;
   if (state->config.encode_key[0] & RADV_ENCODE_KEY_BATCH_COMPRESS_GFX12)
      hw_leaf_node_count = DIV_ROUND_UP(hw_leaf_node_count, 2);

   uint64_t bvh_size = bvh_leaf_size * hw_leaf_node_count + internal_node_size * internal_count;
   uint32_t offset = 0;
   offset += sizeof(struct radv_accel_struct_header);

   if (device->rra_trace.accel_structs) {
      accel_struct->geometry_info_offset = offset;
      offset += sizeof(struct radv_accel_struct_geometry_info) * state->build_info->geometryCount;
   } else {
      accel_struct->geometry_info_offset = RADV_OFFSET_UNUSED;
   }

   /* On GFX12, we need additional space for leaf node offsets since they do not have the same
    * order as the application provided data.
    */
   accel_struct->leaf_node_offsets_offset = offset;
   if (state->config.encode_key[0] & RADV_ENCODE_KEY_WRITE_LEAF_NODE_OFFSETS)
      offset += state->leaf_node_count * 4;

   /* Parent links, which have to go directly before bvh_offset as we index them using negative
    * offsets from there. */
   if (!radv_use_bvh8(pdev))
      offset += bvh_size / 64 * 4;

   /* The BVH and hence bvh_offset needs 64 byte alignment for RT nodes. */
   offset = align(offset, 64);
   accel_struct->bvh_offset = offset;

   /* root node */
   offset += internal_node_size;

   if (!(state->config.encode_key[0] & RADV_ENCODE_KEY_BATCH_COMPRESS_GFX12)) {
      accel_struct->leaf_nodes_offset = offset;
      offset += bvh_leaf_size * state->leaf_node_count;
   }

   accel_struct->internal_nodes_offset = offset;
   /* Factor out the root node. */
   offset += internal_node_size * (internal_count - 1);
   if (state->config.encode_key[0] & RADV_ENCODE_KEY_BATCH_COMPRESS_GFX12)
      offset += bvh_leaf_size * state->leaf_node_count;

   accel_struct->size = offset;
}

static void
radv_get_update_scratch_layout(struct radv_device *device, const struct vk_acceleration_structure_build_state *state,
                               struct update_scratch_layout *scratch)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   uint32_t internal_count = MAX2(state->leaf_node_count, 2) - 1;

   uint32_t offset = 0;

   if (radv_use_bvh8(pdev)) {
      scratch->geometry_data_offset = offset;
      offset += sizeof(struct vk_bvh_geometry_data) * state->build_info->geometryCount;

      scratch->bounds_offsets = offset;
      offset += sizeof(vk_aabb) * internal_count;
   } else {
      scratch->bounds_offsets = offset;
      offset += sizeof(vk_aabb) * state->leaf_node_count;
   }

   scratch->internal_ready_count_offset = offset;
   offset += sizeof(uint32_t) * internal_count;

   scratch->size = offset;
}

VKAPI_ATTR void VKAPI_CALL
radv_GetAccelerationStructureBuildSizesKHR(VkDevice _device, VkAccelerationStructureBuildTypeKHR buildType,
                                           const VkAccelerationStructureBuildGeometryInfoKHR *pBuildInfo,
                                           const uint32_t *pMaxPrimitiveCounts,
                                           VkAccelerationStructureBuildSizesInfoKHR *pSizeInfo)
{
   VK_FROM_HANDLE(radv_device, device, _device);

   STATIC_ASSERT(sizeof(struct radv_bvh_triangle_node) == 64);
   STATIC_ASSERT(sizeof(struct radv_bvh_aabb_node) == 64);
   STATIC_ASSERT(sizeof(struct radv_bvh_instance_node) == 128);
   STATIC_ASSERT(sizeof(struct radv_bvh_box16_node) == 64);
   STATIC_ASSERT(sizeof(struct radv_bvh_box32_node) == 128);
   STATIC_ASSERT(sizeof(struct radv_gfx12_box_node) == RADV_GFX12_BVH_NODE_SIZE);
   STATIC_ASSERT(sizeof(struct radv_gfx12_primitive_node) == RADV_GFX12_BVH_NODE_SIZE);
   STATIC_ASSERT(sizeof(struct radv_gfx12_instance_node) == RADV_GFX12_BVH_NODE_SIZE);
   STATIC_ASSERT(sizeof(struct radv_gfx12_instance_node_user_data) == RADV_GFX12_BVH_NODE_SIZE);

   if (radv_device_init_accel_struct_build_state(device) != VK_SUCCESS)
      return;

   vk_get_as_build_sizes(_device, buildType, pBuildInfo, pMaxPrimitiveCounts, pSizeInfo,
                         &device->meta_state.accel_struct_build.build_args);
}

void
radv_device_finish_accel_struct_build_state(struct radv_device *device)
{
   VkDevice _device = radv_device_to_handle(device);
   struct radv_meta_state *state = &device->meta_state;

   if (state->accel_struct_build.radix_sort)
      radix_sort_vk_destroy(state->accel_struct_build.radix_sort, _device, &state->alloc);
}

static VkDeviceSize
radv_get_as_size(VkDevice _device, const struct vk_acceleration_structure_build_state *state)
{
   VK_FROM_HANDLE(radv_device, device, _device);

   struct acceleration_structure_layout accel_struct;
   radv_get_acceleration_structure_layout(device, state, &accel_struct);
   return accel_struct.size;
}

static uint32_t
radv_get_triangle_batches_size(const struct vk_acceleration_structure_build_state *state)
{
   return state->leaf_node_count * sizeof(struct radv_triangle_encode_task);
}

static VkDeviceSize
radv_get_encode_scratch_size(VkDevice _device, const struct vk_acceleration_structure_build_state *state)
{
   if (state->config.encode_key[2] & RADV_ENCODE_KEY_BATCH_COMPRESS_GFX12) {
      uint32_t retry_batch_indices_size = state->leaf_node_count * sizeof(uint32_t);
      return radv_get_triangle_batches_size(state) + retry_batch_indices_size;
   }

   return 0;
}

static VkDeviceSize
radv_get_update_scratch_size(VkDevice _device, const struct vk_acceleration_structure_build_state *state)
{
   VK_FROM_HANDLE(radv_device, device, _device);

   struct update_scratch_layout scratch;
   radv_get_update_scratch_layout(device, state, &scratch);
   return scratch.size;
}

static void
radv_get_build_config(VkDevice _device, struct vk_acceleration_structure_build_state *state)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   struct radv_physical_device *pdev = radv_device_physical(device);

   uint32_t encode_key = 0;
   if (radv_use_bvh8(pdev)) {
      /*
       * Leaf nodes are not written in the order provided by the application when BVH8 encoding is used.
       * The proper order leaf nodes is used...
       *    1. When fetching vertex positions for triangles.
       *    2. When applying/writing BLAS pointers during TLAS deserialization/serialization. The type
       *       compared to VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR to handle the
       *       VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR case when the application queries the
       *       acceleration structure size.
       */
      if ((state->build_info->flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_KHR) ||
          state->build_info->type != VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR)
         encode_key |= RADV_ENCODE_KEY_WRITE_LEAF_NODE_OFFSETS;

      VkGeometryTypeKHR geometry_type = vk_get_as_geometry_type(state->build_info);
      if (!(state->build_info->flags & (VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR |
                                        VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_KHR)) &&
          geometry_type == VK_GEOMETRY_TYPE_TRIANGLES_KHR)
         encode_key |= RADV_ENCODE_KEY_BATCH_COMPRESS_GFX12;
   }

   state->config.encode_key[0] = encode_key;
   state->config.encode_key[1] = encode_key;
   state->config.encode_key[2] = encode_key;

   uint32_t update_key = 0;
   if (state->build_info->srcAccelerationStructure == state->build_info->dstAccelerationStructure)
      update_key |= RADV_BUILD_FLAG_UPDATE_IN_PLACE;

   if (state->build_info->geometryCount == 1)
      update_key |= RADV_BUILD_FLAG_UPDATE_SINGLE_GEOMETRY;

   if (device->meta_state.accel_struct_build.build_args.propagate_cull_flags)
      update_key |= VK_BUILD_FLAG_PROPAGATE_CULL_FLAGS;

   state->config.update_key[0] = update_key;
}

static void
radv_bvh_build_bind_pipeline(VkCommandBuffer commandBuffer, enum radv_meta_object_key_type type, const uint32_t *spirv,
                             uint32_t spirv_size, uint32_t push_constants_size, uint32_t flags)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   VkPipeline pipeline;
   VkResult result = vk_get_bvh_build_pipeline_spv(
      &device->vk, &device->meta_state.device, (enum vk_meta_object_key_type)type, spirv, spirv_size,
      push_constants_size, &device->meta_state.accel_struct_build.build_args, flags, &pipeline, false);

   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   device->vk.dispatch_table.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
}

static void
radv_bvh_build_set_args(VkCommandBuffer commandBuffer, const void *args, uint32_t size)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   VkPipelineLayout layout;
   vk_get_bvh_build_pipeline_layout(&device->vk, &device->meta_state.device, size, &layout);

   const VkPushConstantsInfoKHR pc_info = {
      .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO_KHR,
      .layout = layout,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .offset = 0,
      .size = size,
      .pValues = args,
   };

   radv_CmdPushConstants2(commandBuffer, &pc_info);
}

static uint32_t
radv_build_flags(VkCommandBuffer commandBuffer, uint32_t key)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   uint32_t flags = 0;

   if (radv_use_bvh8(pdev))
      flags |= RADV_BUILD_FLAG_BVH8;
   if (!radv_emulate_rt(pdev)) {
      /* gfx11 box intersection tests can return garbage with infs and non-standard box sorting */
      if (pdev->info.gfx_level == GFX11)
         flags |= RADV_BUILD_FLAG_NO_INFS;
      if (pdev->info.gfx_level >= GFX11)
         flags |= VK_BUILD_FLAG_PROPAGATE_CULL_FLAGS;
   }
   if (key & RADV_ENCODE_KEY_WRITE_LEAF_NODE_OFFSETS)
      flags |= RADV_BUILD_FLAG_WRITE_LEAF_NODE_OFFSETS;
   if (key & RADV_ENCODE_KEY_PAIR_COMPRESS_GFX12)
      flags |= RADV_BUILD_FLAG_PAIR_COMPRESS_TRIANGLES;
   if (key & RADV_ENCODE_KEY_BATCH_COMPRESS_GFX12)
      flags |= RADV_BUILD_FLAG_BATCH_COMPRESS_TRIANGLES;

   return flags;
}

static VkResult
radv_encode_bind_pipeline(VkCommandBuffer commandBuffer, const struct vk_acceleration_structure_build_state *state)
{
   radv_bvh_build_bind_pipeline(commandBuffer, RADV_META_OBJECT_KEY_BVH_ENCODE, encode_spv, sizeof(encode_spv),
                                sizeof(struct encode_args),
                                radv_build_flags(commandBuffer, state->config.encode_key[0]));

   return VK_SUCCESS;
}

static VkResult
radv_encode_bind_pipeline_gfx12(VkCommandBuffer commandBuffer,
                                const struct vk_acceleration_structure_build_state *state)
{
   radv_bvh_build_bind_pipeline(commandBuffer, RADV_META_OBJECT_KEY_BVH_ENCODE, encode_gfx12_spv,
                                sizeof(encode_gfx12_spv), sizeof(struct encode_gfx12_args),
                                radv_build_flags(commandBuffer, state->config.encode_key[0]));

   return VK_SUCCESS;
}

static void
radv_encode_as(VkCommandBuffer commandBuffer, const struct vk_acceleration_structure_build_state *state)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, dst, state->build_info->dstAccelerationStructure);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   struct acceleration_structure_layout layout;
   radv_get_acceleration_structure_layout(device, state, &layout);

   uint64_t intermediate_header_addr = state->build_info->scratchData.deviceAddress + state->scratch.header_offset;
   uint64_t intermediate_bvh_addr = state->build_info->scratchData.deviceAddress + state->scratch.ir_offset;

   uint32_t dst_offset = layout.internal_nodes_offset - layout.bvh_offset;
   radv_update_memory_cp(cmd_buffer, intermediate_header_addr + offsetof(struct vk_ir_header, dst_node_offset),
                         &dst_offset, sizeof(uint32_t));
   if (radv_device_physical(device)->info.cp_sdma_ge_use_system_memory_scope)
      cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_INV_L2;

   const struct encode_args args = {
      .intermediate_bvh = intermediate_bvh_addr,
      .output_bvh = vk_acceleration_structure_get_va(dst) + layout.bvh_offset,
      .header = intermediate_header_addr,
      .output_bvh_offset = layout.bvh_offset,
      .leaf_node_count = state->leaf_node_count,
      .geometry_type = vk_get_as_geometry_type(state->build_info),
   };
   radv_bvh_build_set_args(commandBuffer, &args, sizeof(args));

   struct radv_dispatch_info dispatch = {
      .unaligned = true,
      .ordered = true,
      .blocks = {MAX2(state->leaf_node_count, 1), 1, 1},
   };

   radv_compute_dispatch(cmd_buffer, &dispatch);
}

static void
radv_encode_as_gfx12(VkCommandBuffer commandBuffer, const struct vk_acceleration_structure_build_state *state)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, dst, state->build_info->dstAccelerationStructure);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   struct acceleration_structure_layout layout;
   radv_get_acceleration_structure_layout(device, state, &layout);

   uint64_t intermediate_header_addr = state->build_info->scratchData.deviceAddress + state->scratch.header_offset;
   uint64_t intermediate_bvh_addr = state->build_info->scratchData.deviceAddress + state->scratch.ir_offset;

   struct vk_ir_header header = {
      .sync_data =
         {
            .current_phase_end_counter = TASK_INDEX_INVALID,
            /* Will be updated by the first PLOC shader invocation */
            .task_counts = {TASK_INDEX_INVALID, TASK_INDEX_INVALID},
         },
      .dst_node_offset = layout.internal_nodes_offset - layout.bvh_offset,
      .dst_leaf_node_offset = layout.leaf_nodes_offset - layout.bvh_offset,
      .driver_internal[RADV_IR_HEADER_ENCODE_TRIANGLES_INVOCATIONS_X] = 0,
      .driver_internal[RADV_IR_HEADER_ENCODE_TRIANGLES_INVOCATIONS_Y] = 1,
      .driver_internal[RADV_IR_HEADER_ENCODE_TRIANGLES_INVOCATIONS_Z] = 1,
      .driver_internal[RADV_IR_HEADER_ENCODE_TRIANGLES_RETRY_INVOCATIONS_X] = 0,
      .driver_internal[RADV_IR_HEADER_ENCODE_TRIANGLES_RETRY_INVOCATIONS_Y] = 1,
      .driver_internal[RADV_IR_HEADER_ENCODE_TRIANGLES_RETRY_INVOCATIONS_Z] = 1,
   };

   uint32_t header_update_size =
      offsetof(struct vk_ir_header, driver_internal) - offsetof(struct vk_ir_header, sync_data);
   if (state->config.encode_key[2] & RADV_ENCODE_KEY_BATCH_COMPRESS_GFX12)
      header_update_size = sizeof(struct vk_ir_header) - offsetof(struct vk_ir_header, sync_data);

   const uint8_t *update_data = ((const uint8_t *)&header + offsetof(struct vk_ir_header, sync_data));
   radv_update_memory_cp(cmd_buffer, intermediate_header_addr + offsetof(struct vk_ir_header, sync_data), update_data,
                         header_update_size);
   if (radv_device_physical(device)->info.cp_sdma_ge_use_system_memory_scope)
      cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_INV_L2;

   const struct encode_gfx12_args args = {
      .intermediate_bvh = intermediate_bvh_addr,
      .output_base = vk_acceleration_structure_get_va(dst),
      .header = intermediate_header_addr,
      .output_bvh_offset = layout.bvh_offset,
      .leaf_node_offsets_offset = layout.leaf_node_offsets_offset,
      .leaf_node_count = state->leaf_node_count,
      .geometry_type = vk_get_as_geometry_type(state->build_info),
   };
   radv_bvh_build_set_args(commandBuffer, &args, sizeof(args));

   uint32_t internal_count = MAX2(state->leaf_node_count, 2) - 1;

   struct radv_dispatch_info dispatch = {
      .ordered = true,
      .blocks = {DIV_ROUND_UP(internal_count * 8, 64), 1, 1},
   };

   radv_compute_dispatch(cmd_buffer, &dispatch);
}

static VkResult
radv_encode_triangles_bind_pipeline_gfx12(VkCommandBuffer commandBuffer,
                                          const struct vk_acceleration_structure_build_state *state)
{
   bool compress_triangles = state->config.encode_key[2] & RADV_ENCODE_KEY_BATCH_COMPRESS_GFX12;
   if (!compress_triangles)
      return VK_SUCCESS;

   /* Wait for internal encoding to finish. */
   vk_barrier_compute_w_to_compute_r(commandBuffer);

   radv_bvh_build_bind_pipeline(commandBuffer, RADV_META_OBJECT_KEY_BVH_ENCODE_TRIANGLES_GFX12,
                                encode_triangles_gfx12_spv, sizeof(encode_triangles_gfx12_spv),
                                sizeof(struct encode_triangles_gfx12_args), 0);

   return VK_SUCCESS;
}

static void
radv_encode_triangles_gfx12(VkCommandBuffer commandBuffer, const struct vk_acceleration_structure_build_state *state)
{
   bool compress_triangles = state->config.encode_key[2] & RADV_ENCODE_KEY_BATCH_COMPRESS_GFX12;
   if (!compress_triangles)
      return;

   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, dst, state->build_info->dstAccelerationStructure);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   uint64_t intermediate_header_addr = state->build_info->scratchData.deviceAddress + state->scratch.header_offset;
   uint64_t intermediate_bvh_addr = state->build_info->scratchData.deviceAddress + state->scratch.ir_offset;

   struct acceleration_structure_layout layout;
   radv_get_acceleration_structure_layout(device, state, &layout);

   const struct encode_triangles_gfx12_args args = {
      .intermediate_bvh = intermediate_bvh_addr,
      .output_base = vk_acceleration_structure_get_va(dst),
      .header = intermediate_header_addr,
      .output_bvh_offset = layout.bvh_offset,
      .leaf_node_offsets_offset = layout.leaf_node_offsets_offset,
      .batches_size = radv_get_triangle_batches_size(state),
   };
   radv_bvh_build_set_args(commandBuffer, &args, sizeof(args));

   struct radv_dispatch_info dispatch = {
      .unaligned = true,
      .indirect_va = intermediate_header_addr +
                     offsetof(struct vk_ir_header, driver_internal[RADV_IR_HEADER_ENCODE_TRIANGLES_INVOCATIONS_X]),
   };

   radv_compute_dispatch(cmd_buffer, &dispatch);
}

static VkResult
radv_encode_triangles_retry_bind_pipeline_gfx12(VkCommandBuffer commandBuffer,
                                                const struct vk_acceleration_structure_build_state *state)
{
   bool compress_triangles = state->config.encode_key[2] & RADV_ENCODE_KEY_BATCH_COMPRESS_GFX12;
   if (!compress_triangles)
      return VK_SUCCESS;

   /* Wait for the first triangle compression pass to finish. */
   vk_barrier_compute_w_to_compute_r(commandBuffer);
   vk_barrier_compute_w_to_indirect_compute_r(commandBuffer);

   radv_bvh_build_bind_pipeline(commandBuffer, RADV_META_OBJECT_KEY_BVH_ENCODE_TRIANGLES_GFX12,
                                encode_triangles_gfx12_spv, sizeof(encode_triangles_gfx12_spv),
                                sizeof(struct encode_triangles_gfx12_args),
                                RADV_BUILD_FLAG_BATCH_COMPRESS_TRIANGLES_RETRY);

   return VK_SUCCESS;
}

static void
radv_encode_triangles_retry_gfx12(VkCommandBuffer commandBuffer,
                                  const struct vk_acceleration_structure_build_state *state)
{
   bool compress_triangles = state->config.encode_key[2] & RADV_ENCODE_KEY_BATCH_COMPRESS_GFX12;
   if (!compress_triangles)
      return;

   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, dst, state->build_info->dstAccelerationStructure);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   uint64_t intermediate_header_addr = state->build_info->scratchData.deviceAddress + state->scratch.header_offset;
   uint64_t intermediate_bvh_addr = state->build_info->scratchData.deviceAddress + state->scratch.ir_offset;

   struct acceleration_structure_layout layout;
   radv_get_acceleration_structure_layout(device, state, &layout);

   const struct encode_triangles_gfx12_args args = {
      .intermediate_bvh = intermediate_bvh_addr,
      .output_base = vk_acceleration_structure_get_va(dst),
      .header = intermediate_header_addr,
      .output_bvh_offset = layout.bvh_offset,
      .leaf_node_offsets_offset = layout.leaf_node_offsets_offset,
      .batches_size = radv_get_triangle_batches_size(state),
   };
   radv_bvh_build_set_args(commandBuffer, &args, sizeof(args));

   struct radv_dispatch_info dispatch = {
      .unaligned = true,
      .indirect_va =
         intermediate_header_addr +
         offsetof(struct vk_ir_header, driver_internal[RADV_IR_HEADER_ENCODE_TRIANGLES_RETRY_INVOCATIONS_X]),
   };

   radv_compute_dispatch(cmd_buffer, &dispatch);
}

static VkResult
radv_init_header_bind_pipeline(VkCommandBuffer commandBuffer, const struct vk_acceleration_structure_build_state *state)
{
   /* Wait for encoding to finish. */
   vk_barrier_compute_w_to_compute_r(commandBuffer);

   radv_bvh_build_bind_pipeline(commandBuffer, RADV_META_OBJECT_KEY_BVH_HEADER, header_spv, sizeof(header_spv),
                                sizeof(struct header_args), 0);

   return VK_SUCCESS;
}

static void
radv_init_header(VkCommandBuffer commandBuffer, const struct vk_acceleration_structure_build_state *state)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, dst, state->build_info->dstAccelerationStructure);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   uint64_t intermediate_header_addr = state->build_info->scratchData.deviceAddress + state->scratch.header_offset;

   size_t base = offsetof(struct radv_accel_struct_header, compacted_size);

   uint64_t instance_count =
      state->build_info->type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR ? state->leaf_node_count : 0;

   struct acceleration_structure_layout layout;
   radv_get_acceleration_structure_layout(device, state, &layout);

   base = offsetof(struct radv_accel_struct_header, geometry_type);

   struct header_args args = {
      .src = intermediate_header_addr,
      .dst = vk_acceleration_structure_get_va(dst),
      .bvh_offset = layout.bvh_offset,
      .internal_nodes_offset = layout.internal_nodes_offset - layout.bvh_offset,
      .instance_count = instance_count,
   };
   radv_bvh_build_set_args(commandBuffer, &args, sizeof(args));

   radv_unaligned_dispatch(cmd_buffer, 1, 1, 1);

   struct radv_accel_struct_header header;

   header.instance_offset = layout.bvh_offset + sizeof(struct radv_bvh_box32_node);
   header.instance_count = instance_count;
   header.leaf_node_offsets_offset = layout.leaf_node_offsets_offset;
   header.compacted_size = layout.size;

   header.copy_dispatch_size[0] = DIV_ROUND_UP(header.compacted_size, 16 * 64);
   header.copy_dispatch_size[1] = 1;
   header.copy_dispatch_size[2] = 1;

   header.serialization_size =
      header.compacted_size +
      align(sizeof(struct radv_accel_struct_serialization_header) + sizeof(uint64_t) * header.instance_count, 128);

   header.size = header.serialization_size - sizeof(struct radv_accel_struct_serialization_header) -
                 sizeof(uint64_t) * header.instance_count;

   header.build_flags = state->build_info->flags;
   header.geometry_type = vk_get_as_geometry_type(state->build_info);
   header.geometry_count = state->build_info->geometryCount;

   radv_update_memory_cp(cmd_buffer, vk_acceleration_structure_get_va(dst) + base, (const char *)&header + base,
                         sizeof(header) - base);

   if (layout.geometry_info_offset != RADV_OFFSET_UNUSED) {
      uint64_t geometry_infos_size = state->build_info->geometryCount * sizeof(struct radv_accel_struct_geometry_info);

      struct radv_accel_struct_geometry_info *geometry_infos = malloc(geometry_infos_size);
      if (!geometry_infos)
         return;

      for (uint32_t i = 0; i < state->build_info->geometryCount; i++) {
         const VkAccelerationStructureGeometryKHR *geometry =
            state->build_info->pGeometries ? &state->build_info->pGeometries[i] : state->build_info->ppGeometries[i];
         geometry_infos[i].type = geometry->geometryType;
         geometry_infos[i].flags = geometry->flags;
         geometry_infos[i].primitive_count = state->build_range_infos[i].primitiveCount;
      }

      radv_CmdUpdateBuffer(commandBuffer, vk_buffer_to_handle(dst->buffer), dst->offset + layout.geometry_info_offset,
                           geometry_infos_size, geometry_infos);

      free(geometry_infos);
   }
}

static void
radv_init_update_scratch(VkCommandBuffer commandBuffer, const struct vk_acceleration_structure_build_state *state)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   uint64_t scratch = state->build_info->scratchData.deviceAddress;

   struct update_scratch_layout layout;
   radv_get_update_scratch_layout(device, state, &layout);

   /* Prepare ready counts for internal nodes */
   radv_fill_memory(cmd_buffer, scratch + layout.internal_ready_count_offset,
                    layout.size - layout.internal_ready_count_offset, 0x0, RADV_COPY_FLAGS_DEVICE_LOCAL);

   /* geometryCount == 1 passes the data as push constant. */
   if (radv_use_bvh8(pdev) && !(state->config.update_key[0] & RADV_BUILD_FLAG_UPDATE_SINGLE_GEOMETRY)) {
      uint32_t data_size = sizeof(struct vk_bvh_geometry_data) * state->build_info->geometryCount;
      struct vk_bvh_geometry_data *data = malloc(data_size);
      if (!data) {
         vk_command_buffer_set_error(&cmd_buffer->vk, VK_ERROR_OUT_OF_HOST_MEMORY);
         return;
      }

      uint32_t first_id = 0;
      for (uint32_t i = 0; i < state->build_info->geometryCount; i++) {
         const VkAccelerationStructureGeometryKHR *geom =
            state->build_info->pGeometries ? &state->build_info->pGeometries[i] : state->build_info->ppGeometries[i];

         const VkAccelerationStructureBuildRangeInfoKHR *build_range_info = &state->build_range_infos[i];

         data[i] = vk_fill_geometry_data(state->build_info->type, first_id, i, geom, build_range_info);

         first_id += build_range_info->primitiveCount;
      }

      radv_update_memory(cmd_buffer, scratch + layout.geometry_data_offset, data_size, data,
                         RADV_COPY_FLAGS_DEVICE_LOCAL);

      free(data);
   }
}

static void
radv_update_bind_pipeline(VkCommandBuffer commandBuffer, const struct vk_acceleration_structure_build_state *state,
                          bool flushed_cp_after_init_update_scratch, bool flushed_compute_after_init_update_scratch)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   /* Wait for update scratch initialization to finish.. */
   if (!flushed_compute_after_init_update_scratch)
      vk_barrier_compute_w_to_compute_r(commandBuffer);

   if (!flushed_cp_after_init_update_scratch) {
      if (radv_device_physical(device)->info.cp_sdma_ge_use_system_memory_scope)
         cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_INV_L2;
   }

   uint32_t flags = state->config.update_key[0];

   if (radv_use_bvh8(pdev)) {
      radv_bvh_build_bind_pipeline(commandBuffer, RADV_META_OBJECT_KEY_BVH_UPDATE, update_gfx12_spv,
                                   sizeof(update_gfx12_spv), sizeof(struct update_args), flags);
   } else {
      radv_bvh_build_bind_pipeline(commandBuffer, RADV_META_OBJECT_KEY_BVH_UPDATE, update_spv, sizeof(update_spv),
                                   sizeof(struct update_args), flags);
   }
}

static uint32_t
pack_geometry_id_and_flags(uint32_t geometry_id, uint32_t flags)
{
   uint32_t geometry_id_and_flags = geometry_id;
   if (flags & VK_GEOMETRY_OPAQUE_BIT_KHR)
      geometry_id_and_flags |= RADV_GEOMETRY_OPAQUE;

   return geometry_id_and_flags;
}

static void
radv_update_as(VkCommandBuffer commandBuffer, const struct vk_acceleration_structure_build_state *state)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, src, state->build_info->srcAccelerationStructure);
   VK_FROM_HANDLE(vk_acceleration_structure, dst, state->build_info->dstAccelerationStructure);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   if (src != dst) {
      struct acceleration_structure_layout layout;
      radv_get_acceleration_structure_layout(device, state, &layout);

      /* Copy header/metadata */
      const uint64_t src_va = vk_acceleration_structure_get_va(src);
      const uint64_t dst_va = vk_acceleration_structure_get_va(dst);

      radv_copy_memory(cmd_buffer, src_va, dst_va, layout.bvh_offset, RADV_COPY_FLAGS_DEVICE_LOCAL,
                       RADV_COPY_FLAGS_DEVICE_LOCAL);
   }

   struct update_scratch_layout layout;
   radv_get_update_scratch_layout(device, state, &layout);

   struct update_args update_consts = {
      .src = vk_acceleration_structure_get_va(src),
      .dst = vk_acceleration_structure_get_va(dst),
      .leaf_bounds = state->build_info->scratchData.deviceAddress,
      .internal_ready_count = state->build_info->scratchData.deviceAddress + layout.internal_ready_count_offset,
      .leaf_node_count = state->leaf_node_count,
   };

   uint32_t first_id = 0;
   for (uint32_t i = 0; i < state->build_info->geometryCount; i++) {
      const VkAccelerationStructureGeometryKHR *geom =
         state->build_info->pGeometries ? &state->build_info->pGeometries[i] : state->build_info->ppGeometries[i];

      const VkAccelerationStructureBuildRangeInfoKHR *build_range_info = &state->build_range_infos[i];

      update_consts.geom_data = vk_fill_geometry_data(state->build_info->type, first_id, i, geom, build_range_info);

      radv_bvh_build_set_args(commandBuffer, &update_consts, sizeof(update_consts));

      radv_unaligned_dispatch(cmd_buffer, build_range_info->primitiveCount, 1, 1);

      first_id += build_range_info->primitiveCount;
   }
}

static void
radv_update_as_gfx12(VkCommandBuffer commandBuffer, const struct vk_acceleration_structure_build_state *state)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, src, state->build_info->srcAccelerationStructure);
   VK_FROM_HANDLE(vk_acceleration_structure, dst, state->build_info->dstAccelerationStructure);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   if (src != dst) {
      struct acceleration_structure_layout layout;
      radv_get_acceleration_structure_layout(device, state, &layout);

      /* Copy header/metadata */
      const uint64_t src_va = vk_acceleration_structure_get_va(src);
      const uint64_t dst_va = vk_acceleration_structure_get_va(dst);

      radv_copy_memory(cmd_buffer, src_va, dst_va, layout.bvh_offset, RADV_COPY_FLAGS_DEVICE_LOCAL,
                       RADV_COPY_FLAGS_DEVICE_LOCAL);
   }

   struct update_scratch_layout layout;
   radv_get_update_scratch_layout(device, state, &layout);

   struct update_gfx12_args update_consts = {
      .src = vk_acceleration_structure_get_va(src),
      .dst = vk_acceleration_structure_get_va(dst),
      .geom_data = state->build_info->scratchData.deviceAddress + layout.geometry_data_offset,
      .bounds = state->build_info->scratchData.deviceAddress + layout.bounds_offsets,
      .internal_ready_count = state->build_info->scratchData.deviceAddress + layout.internal_ready_count_offset,
      .leaf_node_count = state->leaf_node_count,
   };

   if (state->config.update_key[0] & RADV_BUILD_FLAG_UPDATE_SINGLE_GEOMETRY) {
      const VkAccelerationStructureGeometryKHR *geom =
         state->build_info->pGeometries ? &state->build_info->pGeometries[0] : state->build_info->ppGeometries[0];
      update_consts.geom_data0 = vk_fill_geometry_data(state->build_info->type, 0, 0, geom, state->build_range_infos);
   }

   radv_bvh_build_set_args(commandBuffer, &update_consts, sizeof(update_consts));

   struct radv_dispatch_info dispatch = {
      .ordered = true,
      .unaligned = true,
      .indirect_va =
         vk_acceleration_structure_get_va(src) + offsetof(struct radv_accel_struct_header, update_dispatch_size[0]),
   };

   radv_compute_dispatch(cmd_buffer, &dispatch);
}

static const struct radix_sort_vk_target_config radix_sort_config = {
   .keyval_dwords = 2,
   .fill.workgroup_size_log2 = 7,
   .fill.block_rows = 8,
   .histogram.workgroup_size_log2 = 8,
   .histogram.subgroup_size_log2 = 6,
   .histogram.block_rows = 14,
   .prefix.workgroup_size_log2 = 8,
   .prefix.subgroup_size_log2 = 6,
   .scatter.workgroup_size_log2 = 8,
   .scatter.subgroup_size_log2 = 6,
   .scatter.block_rows = 14,
};

static void
radv_write_buffer_cp(VkCommandBuffer commandBuffer, VkDeviceAddress addr, void *data, uint32_t size)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   radv_update_memory_cp(cmd_buffer, addr, data, size);
}

static void
radv_flush_buffer_write_cp(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   const struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   if (pdev->info.cp_sdma_ge_use_system_memory_scope)
      cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_INV_L2;
}

static void
radv_cmd_dispatch_unaligned(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y, uint32_t z)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   radv_unaligned_dispatch(cmd_buffer, x, y, z);
}

static void
radv_cmd_fill_buffer_addr(VkCommandBuffer commandBuffer, VkDeviceAddress addr, VkDeviceSize size, uint32_t data)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   radv_fill_memory(cmd_buffer, addr, size, data, RADV_COPY_FLAGS_DEVICE_LOCAL);
}

VkResult
radv_device_init_accel_struct_build_state(struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   mtx_lock(&device->meta_state.mtx);

   if (device->meta_state.accel_struct_build.radix_sort)
      goto exit;

   device->meta_state.accel_struct_build.radix_sort = vk_create_radix_sort_u64(
      radv_device_to_handle(device), &device->meta_state.alloc, device->meta_state.cache, radix_sort_config);

   device->meta_state.accel_struct_build.build_ops = (struct vk_acceleration_structure_build_ops){
      .begin_debug_marker = vk_accel_struct_cmd_begin_debug_marker,
      .end_debug_marker = vk_accel_struct_cmd_end_debug_marker,
      .get_build_config = radv_get_build_config,
      .get_as_size = radv_get_as_size,
      .get_update_scratch_size = radv_get_update_scratch_size,
      .init_update_scratch = radv_init_update_scratch,
      .update_bind_pipeline[0] = radv_update_bind_pipeline,
   };

   if (radv_use_bvh8(pdev)) {
      device->meta_state.accel_struct_build.build_ops.update_as[0] = radv_update_as_gfx12;
      device->meta_state.accel_struct_build.build_ops.get_encode_scratch_size = radv_get_encode_scratch_size;
      device->meta_state.accel_struct_build.build_ops.encode_bind_pipeline[0] = radv_encode_bind_pipeline_gfx12;
      device->meta_state.accel_struct_build.build_ops.encode_as[0] = radv_encode_as_gfx12;
      device->meta_state.accel_struct_build.build_ops.encode_bind_pipeline[1] =
         radv_encode_triangles_bind_pipeline_gfx12;
      device->meta_state.accel_struct_build.build_ops.encode_as[1] = radv_encode_triangles_gfx12;
      device->meta_state.accel_struct_build.build_ops.encode_bind_pipeline[2] =
         radv_encode_triangles_retry_bind_pipeline_gfx12;
      device->meta_state.accel_struct_build.build_ops.encode_as[2] = radv_encode_triangles_retry_gfx12;
      device->meta_state.accel_struct_build.build_ops.encode_bind_pipeline[3] = radv_init_header_bind_pipeline;
      device->meta_state.accel_struct_build.build_ops.encode_as[3] = radv_init_header;
   } else {
      device->meta_state.accel_struct_build.build_ops.update_as[0] = radv_update_as;
      device->meta_state.accel_struct_build.build_ops.encode_bind_pipeline[0] = radv_encode_bind_pipeline;
      device->meta_state.accel_struct_build.build_ops.encode_as[0] = radv_encode_as;
      device->meta_state.accel_struct_build.build_ops.encode_bind_pipeline[1] = radv_init_header_bind_pipeline;
      device->meta_state.accel_struct_build.build_ops.encode_as[1] = radv_init_header;
      device->meta_state.accel_struct_build.build_ops.leaf_spirv_override = leaf_spv;
      device->meta_state.accel_struct_build.build_ops.leaf_spirv_override_size = sizeof(leaf_spv);
   }

   device->vk.as_build_ops = &device->meta_state.accel_struct_build.build_ops;
   device->vk.write_buffer_cp = radv_write_buffer_cp;
   device->vk.flush_buffer_write_cp = radv_flush_buffer_write_cp;
   device->vk.cmd_dispatch_unaligned = radv_cmd_dispatch_unaligned;
   device->vk.cmd_fill_buffer_addr = radv_cmd_fill_buffer_addr;

   struct vk_acceleration_structure_build_args *build_args = &device->meta_state.accel_struct_build.build_args;
   build_args->subgroup_size = 64;
   build_args->bvh_bounds_offset = offsetof(struct radv_accel_struct_header, aabb);
   build_args->root_flags_offset = offsetof(struct radv_accel_struct_header, root_flags);
   build_args->propagate_cull_flags = pdev->info.gfx_level >= GFX11;
   build_args->emit_markers = device->sqtt.bo;
   build_args->radix_sort = device->meta_state.accel_struct_build.radix_sort;

exit:
   mtx_unlock(&device->meta_state.mtx);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdBuildAccelerationStructuresKHR(VkCommandBuffer commandBuffer, uint32_t infoCount,
                                       const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
                                       const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_meta_saved_state saved_state;

   VkResult result = radv_device_init_accel_struct_build_state(device);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   radv_meta_save(&saved_state, cmd_buffer,
                  RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_CONSTANTS);

   cmd_buffer->state.current_event_type = EventInternalUnknown;

   vk_cmd_build_acceleration_structures(commandBuffer, &device->vk, &device->meta_state.device, infoCount, pInfos,
                                        ppBuildRangeInfos, &device->meta_state.accel_struct_build.build_args);

   radv_meta_restore(&saved_state, cmd_buffer);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdCopyAccelerationStructureKHR(VkCommandBuffer commandBuffer, const VkCopyAccelerationStructureInfoKHR *pInfo)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, src, pInfo->src);
   VK_FROM_HANDLE(vk_acceleration_structure, dst, pInfo->dst);
   struct radv_meta_saved_state saved_state;

   radv_meta_save(&saved_state, cmd_buffer,
                  RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_CONSTANTS);

   radv_bvh_build_bind_pipeline(commandBuffer, RADV_META_OBJECT_KEY_BVH_COPY, copy_spv, sizeof(copy_spv),
                                sizeof(struct copy_args), radv_build_flags(commandBuffer, 0) & RADV_BUILD_FLAG_BVH8);

   struct copy_args consts = {
      .src_addr = vk_acceleration_structure_get_va(src),
      .dst_addr = vk_acceleration_structure_get_va(dst),
      .mode = RADV_COPY_MODE_COPY,
   };
   radv_bvh_build_set_args(commandBuffer, &consts, sizeof(consts));

   cmd_buffer->state.flush_bits |= radv_dst_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                                                         VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, 0, NULL, NULL);

   radv_CmdDispatchIndirect(commandBuffer, vk_buffer_to_handle(src->buffer),
                            src->offset + offsetof(struct radv_accel_struct_header, copy_dispatch_size));

   radv_meta_restore(&saved_state, cmd_buffer);
}

VKAPI_ATTR void VKAPI_CALL
radv_GetDeviceAccelerationStructureCompatibilityKHR(VkDevice _device,
                                                    const VkAccelerationStructureVersionInfoKHR *pVersionInfo,
                                                    VkAccelerationStructureCompatibilityKHR *pCompatibility)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   bool compat = memcmp(pVersionInfo->pVersionData, pdev->driver_uuid, VK_UUID_SIZE) == 0 &&
                 memcmp(pVersionInfo->pVersionData + VK_UUID_SIZE, pdev->cache_uuid, VK_UUID_SIZE) == 0;
   *pCompatibility = compat ? VK_ACCELERATION_STRUCTURE_COMPATIBILITY_COMPATIBLE_KHR
                            : VK_ACCELERATION_STRUCTURE_COMPATIBILITY_INCOMPATIBLE_KHR;
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdCopyMemoryToAccelerationStructureKHR(VkCommandBuffer commandBuffer,
                                             const VkCopyMemoryToAccelerationStructureInfoKHR *pInfo)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, dst, pInfo->dst);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_meta_saved_state saved_state;

   radv_meta_save(&saved_state, cmd_buffer,
                  RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_CONSTANTS);

   radv_bvh_build_bind_pipeline(commandBuffer, RADV_META_OBJECT_KEY_BVH_COPY, copy_spv, sizeof(copy_spv),
                                sizeof(struct copy_args), radv_build_flags(commandBuffer, 0) & RADV_BUILD_FLAG_BVH8);

   const struct copy_args consts = {
      .src_addr = pInfo->src.deviceAddress,
      .dst_addr = vk_acceleration_structure_get_va(dst),
      .mode = RADV_COPY_MODE_DESERIALIZE,
   };
   radv_bvh_build_set_args(commandBuffer, &consts, sizeof(consts));

   radv_CmdDispatchBase(commandBuffer, 0, 0, 0, 512, 1, 1);

   if (radv_use_bvh8(pdev)) {
      /* Wait for the main copy dispatch to finish. */
      vk_barrier_compute_w_to_compute_r(commandBuffer);

      radv_bvh_build_bind_pipeline(commandBuffer, RADV_META_OBJECT_KEY_BVH_COPY_BLAS_ADDRS_GFX12,
                                   copy_blas_addrs_gfx12_spv, sizeof(copy_blas_addrs_gfx12_spv),
                                   sizeof(struct copy_args), 0);

      radv_CmdDispatchBase(commandBuffer, 0, 0, 0, 256, 1, 1);
   }

   radv_meta_restore(&saved_state, cmd_buffer);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdCopyAccelerationStructureToMemoryKHR(VkCommandBuffer commandBuffer,
                                             const VkCopyAccelerationStructureToMemoryInfoKHR *pInfo)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, src, pInfo->src);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_meta_saved_state saved_state;

   radv_meta_save(&saved_state, cmd_buffer,
                  RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_CONSTANTS);

   radv_bvh_build_bind_pipeline(commandBuffer, RADV_META_OBJECT_KEY_BVH_COPY, copy_spv, sizeof(copy_spv),
                                sizeof(struct copy_args), radv_build_flags(commandBuffer, 0) & RADV_BUILD_FLAG_BVH8);

   const struct copy_args consts = {
      .src_addr = vk_acceleration_structure_get_va(src),
      .dst_addr = pInfo->dst.deviceAddress,
      .mode = RADV_COPY_MODE_SERIALIZE,
   };
   radv_bvh_build_set_args(commandBuffer, &consts, sizeof(consts));

   cmd_buffer->state.flush_bits |= radv_dst_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                                                         VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, 0, NULL, NULL);

   radv_CmdDispatchIndirect(commandBuffer, vk_buffer_to_handle(src->buffer),
                            src->offset + offsetof(struct radv_accel_struct_header, copy_dispatch_size));

   if (radv_use_bvh8(pdev)) {
      /* Wait for the main copy dispatch to finish. */
      vk_barrier_compute_w_to_compute_r(commandBuffer);

      radv_bvh_build_bind_pipeline(commandBuffer, RADV_META_OBJECT_KEY_BVH_COPY_BLAS_ADDRS_GFX12,
                                   copy_blas_addrs_gfx12_spv, sizeof(copy_blas_addrs_gfx12_spv),
                                   sizeof(struct copy_args), 0);

      radv_CmdDispatchBase(commandBuffer, 0, 0, 0, 256, 1, 1);
   }

   radv_meta_restore(&saved_state, cmd_buffer);

   /* Set the header of the serialized data. */
   uint8_t header_data[2 * VK_UUID_SIZE];
   memcpy(header_data, pdev->driver_uuid, VK_UUID_SIZE);
   memcpy(header_data + VK_UUID_SIZE, pdev->cache_uuid, VK_UUID_SIZE);

   radv_update_memory_cp(cmd_buffer, pInfo->dst.deviceAddress, header_data, sizeof(header_data));
}
