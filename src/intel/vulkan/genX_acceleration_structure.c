/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_private.h"

#include <math.h>

#include "util/u_debug.h"
#include "util/half_float.h"
#include "util/u_atomic.h"

#include "genxml/gen_macros.h"
#include "genxml/genX_pack.h"
#include "genxml/genX_rt_pack.h"

#include "ds/intel_tracepoints.h"

#include "bvh/anv_build_interface.h"
#include "vk_acceleration_structure.h"
#include "radix_sort/radix_sort_u64.h"
#include "radix_sort/common/vk/barrier.h"

#include "vk_common_entrypoints.h"
#include "genX_mi_builder.h"

#if GFX_VERx10 >= 125

/* Id to track bvh_dump */
static uint32_t blas_id = 0;
static uint32_t tlas_id = 0;

static void
begin_debug_marker(VkCommandBuffer commandBuffer,
                   struct vk_acceleration_structure_build_marker *marker)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   switch (marker->step) {
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_TOP:
      trace_intel_begin_as_build(&cmd_buffer->trace);
      break;
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_BUILD_LEAVES:
      trace_intel_begin_as_build_leaves(&cmd_buffer->trace);
      break;
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_MORTON_GENERATE:
      trace_intel_begin_as_morton_generate(&cmd_buffer->trace);
      break;
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_MORTON_SORT:
      trace_intel_begin_as_morton_sort(&cmd_buffer->trace);
      break;
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_LBVH_BUILD_INTERNAL:
      trace_intel_begin_as_lbvh_build_internal(&cmd_buffer->trace);
      break;
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_PLOC_BUILD_INTERNAL:
      trace_intel_begin_as_ploc_build_internal(&cmd_buffer->trace);
      break;
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_ENCODE:
      trace_intel_begin_as_encode(&cmd_buffer->trace);
      break;
   default:
      UNREACHABLE("Invalid build step");
   }
}

static void
end_debug_marker(VkCommandBuffer commandBuffer,
                 struct vk_acceleration_structure_build_marker *marker)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   switch (marker->step) {
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_TOP:
      trace_intel_end_as_build(&cmd_buffer->trace,
                               marker->top.tlas_count,
                               marker->top.blas_count);
      break;
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_BUILD_LEAVES:
      trace_intel_end_as_build_leaves(&cmd_buffer->trace);
      break;
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_MORTON_GENERATE:
      trace_intel_end_as_morton_generate(&cmd_buffer->trace);
      break;
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_MORTON_SORT:
      trace_intel_end_as_morton_sort(&cmd_buffer->trace);
      break;
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_LBVH_BUILD_INTERNAL:
      trace_intel_end_as_lbvh_build_internal(&cmd_buffer->trace);
      break;
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_PLOC_BUILD_INTERNAL:
      trace_intel_end_as_ploc_build_internal(&cmd_buffer->trace);
      break;
   case VK_ACCELERATION_STRUCTURE_BUILD_STEP_ENCODE:
      trace_intel_end_as_encode(&cmd_buffer->trace,
                                marker->encode.pass,
                                marker->encode.key,
                                marker->encode.leaf_node_count,
                                marker->encode.internal_node_count);
      break;
   default:
      UNREACHABLE("Invalid build step");
   }
}

static void
add_bvh_dump(struct anv_cmd_buffer *cmd_buffer,
             VkDeviceAddress src,
             uint64_t dump_size,
             VkGeometryTypeKHR geometry_type,
             enum bvh_dump_type dump_type)
{
   assert(dump_size % 4 == 0);

   struct anv_device *device = cmd_buffer->device;
   struct anv_bo *bo = NULL;

   VkResult result = anv_device_alloc_bo(device, "bvh_dump", dump_size,
                                         ANV_BO_ALLOC_MAPPED |
                                         ANV_BO_ALLOC_HOST_CACHED_COHERENT, 0,
                                         &bo);
   if (result != VK_SUCCESS) {
      printf("Failed to allocate bvh for dump\n");
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   struct anv_bvh_dump *bvh_dump = malloc(sizeof(struct anv_bvh_dump));

   bvh_dump->bo = bo;
   bvh_dump->bvh_id = geometry_type == VK_GEOMETRY_TYPE_INSTANCES_KHR ?
                           tlas_id : blas_id;
   bvh_dump->dump_size = dump_size;
   bvh_dump->geometry_type = geometry_type;
   bvh_dump->dump_type = dump_type;

   struct anv_address dst_addr = { .bo = bvh_dump->bo, .offset = 0 };
   struct anv_address src_addr = anv_address_from_u64(src);
   anv_cmd_copy_addr(cmd_buffer, src_addr, dst_addr, bvh_dump->dump_size);

   pthread_mutex_lock(&device->mutex);
   list_addtail(&bvh_dump->link, &device->bvh_dumps);
   pthread_mutex_unlock(&device->mutex);
}

static void
debug_record_as_to_bvh_dump(struct anv_cmd_buffer *cmd_buffer,
                            VkDeviceAddress header_addr,
                            uint64_t bvh_anv_size,
                            VkDeviceAddress intermediate_header_addr,
                            VkDeviceAddress intermediate_as_addr,
                            uint32_t leaf_count,
                            VkGeometryTypeKHR geometry_type)
{
   if (INTEL_DEBUG(DEBUG_BVH_BLAS) &&
       geometry_type != VK_GEOMETRY_TYPE_INSTANCES_KHR) {
      add_bvh_dump(cmd_buffer, header_addr, bvh_anv_size, geometry_type,
                   BVH_ANV);
   }

   if (INTEL_DEBUG(DEBUG_BVH_TLAS) &&
       geometry_type == VK_GEOMETRY_TYPE_INSTANCES_KHR) {
      add_bvh_dump(cmd_buffer, header_addr, bvh_anv_size, geometry_type,
                   BVH_ANV);
   }

   if (INTEL_DEBUG(DEBUG_BVH_BLAS_IR_HDR) &&
       geometry_type != VK_GEOMETRY_TYPE_INSTANCES_KHR) {
      add_bvh_dump(cmd_buffer, intermediate_header_addr,
                   sizeof(struct vk_ir_header), geometry_type, BVH_IR_HDR);
   }

   if (INTEL_DEBUG(DEBUG_BVH_TLAS_IR_HDR) &&
       geometry_type == VK_GEOMETRY_TYPE_INSTANCES_KHR) {
      add_bvh_dump(cmd_buffer, intermediate_header_addr,
                   sizeof(struct vk_ir_header), geometry_type, BVH_IR_HDR);
   }

   uint32_t internal_node_count = MAX2(leaf_count, 2) - 1;
   uint64_t internal_node_total_size = sizeof(struct vk_ir_box_node) *
                                       internal_node_count;

   if (INTEL_DEBUG(DEBUG_BVH_BLAS_IR_AS) &&
       geometry_type != VK_GEOMETRY_TYPE_INSTANCES_KHR) {
      uint64_t leaf_total_size;

      switch (geometry_type) {
      case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
         leaf_total_size = sizeof(struct vk_ir_triangle_node) * leaf_count;
         break;
      case VK_GEOMETRY_TYPE_AABBS_KHR:
         leaf_total_size = sizeof(struct vk_ir_aabb_node) * leaf_count;
         break;
      default:
         UNREACHABLE("invalid geometry type");
      }

      add_bvh_dump(cmd_buffer, intermediate_as_addr, internal_node_total_size +
                   leaf_total_size, geometry_type, BVH_IR_AS);
   }

   if (INTEL_DEBUG(DEBUG_BVH_TLAS_IR_AS) &&
       geometry_type == VK_GEOMETRY_TYPE_INSTANCES_KHR) {
      uint64_t leaf_total_size = sizeof(struct vk_ir_instance_node) *
                                 leaf_count;
      add_bvh_dump(cmd_buffer, intermediate_as_addr, internal_node_total_size +
                   leaf_total_size, geometry_type, BVH_IR_AS);
   }


   if (geometry_type == VK_GEOMETRY_TYPE_INSTANCES_KHR) {
      tlas_id++;
   } else {
      blas_id++;
   }
}

#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)

#define ENCODE_SPV_PATH STRINGIFY(bvh/genX(encode).spv.h)
#define HEADER_SPV_PATH STRINGIFY(bvh/genX(header).spv.h)
#define COPY_SPV_PATH STRINGIFY(bvh/genX(copy).spv.h)

static const uint32_t encode_spv[] = {
#include ENCODE_SPV_PATH
};

static const uint32_t header_spv[] = {
#include HEADER_SPV_PATH
};

static const uint32_t copy_spv[] = {
#include COPY_SPV_PATH
};

static void
get_bvh_layout(VkGeometryTypeKHR geometry_type, uint32_t leaf_count,
               struct bvh_layout *layout)
{
   uint32_t internal_count = MAX2(leaf_count, 2) - 1;

   uint64_t offset = ANV_RT_BVH_HEADER_SIZE;

   /* For a TLAS, we store the address of anv_instance_leaf after header
    * This is for quick access in the copy.comp
    */
   if (geometry_type == VK_GEOMETRY_TYPE_INSTANCES_KHR) {
      offset += leaf_count * sizeof(uint64_t);
   }
   /* The BVH and hence bvh_offset needs 64 byte alignment for RT nodes. */
   offset = align64(offset, 64);

   /* This is where internal_nodes/leaves start to be encoded */
   layout->bvh_offset = offset;

   offset += internal_count * ANV_RT_INTERNAL_NODE_SIZE;

   switch (geometry_type) {
   case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
      /* Currently we encode one triangle within one quad leaf */
      offset += leaf_count * ANV_RT_QUAD_LEAF_SIZE;
      break;
   case VK_GEOMETRY_TYPE_AABBS_KHR:
      offset += leaf_count * ANV_RT_PROCEDURAL_LEAF_SIZE;
      break;
   case VK_GEOMETRY_TYPE_INSTANCES_KHR:
      offset += leaf_count * ANV_RT_INSTANCE_LEAF_SIZE;
      break;
   default:
      UNREACHABLE("Unknown VkGeometryTypeKHR");
   }

   layout->size = offset;
}

static VkDeviceSize
anv_get_as_size(VkDevice device, const struct vk_acceleration_structure_build_state *state)
{
   struct bvh_layout layout;
   get_bvh_layout(vk_get_as_geometry_type(state->build_info), state->leaf_node_count, &layout);
   return layout.size;
}

static void
anv_get_build_config(VkDevice device, struct vk_acceleration_structure_build_state *state)
{
   state->config.encode_key[1] = (state->build_info->flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR) ? 1 : 0;
}

static void
anv_bvh_build_bind_pipeline(VkCommandBuffer commandBuffer,
                            enum anv_object_key_bvh_type type,
                            const uint32_t *spirv, uint32_t spirv_size,
                            uint32_t push_constant_size, uint32_t flags)
{
   VK_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   struct anv_device *device = cmd_buffer->device;

   VkPipeline pipeline;
   VkResult result = vk_get_bvh_build_pipeline_spv(&device->vk,
                                                   &device->meta_device,
                                                   (enum anv_object_key_bvh_type)type,
                                                   spirv, spirv_size, push_constant_size,
                                                   &device->accel_struct_build.build_args,
                                                   flags, &pipeline, false);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   device->vk.dispatch_table.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                             pipeline);
}

static void
anv_bvh_build_set_args(VkCommandBuffer commandBuffer, const void *args,
                       uint32_t size)
{
   VK_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   struct anv_device *device = cmd_buffer->device;
   VkPipelineLayout layout;
   vk_get_bvh_build_pipeline_layout(&device->vk, &device->meta_device, size,
                                    &layout);

   VkPushConstantsInfoKHR push_info = {
      .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO_KHR,
      .layout = layout,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .offset = 0,
      .size = size,
      .pValues = args,
   };

   anv_CmdPushConstants2KHR(commandBuffer, &push_info);
}

static VkResult
anv_encode_bind_pipeline(VkCommandBuffer commandBuffer, const struct vk_acceleration_structure_build_state *state)
{
   anv_bvh_build_bind_pipeline(commandBuffer,
                               ANV_OBJECT_KEY_BVH_ENCODE,
                               encode_spv, sizeof(encode_spv),
                               sizeof(struct encode_args), 0);
   return VK_SUCCESS;
}

static void
anv_encode_as(VkCommandBuffer commandBuffer, const struct vk_acceleration_structure_build_state *state)
{
   if (INTEL_DEBUG(DEBUG_BVH_NO_BUILD))
      return;

   VK_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, dst, state->build_info->dstAccelerationStructure);

   uint64_t intermediate_header_addr = state->build_info->scratchData.deviceAddress + state->scratch.header_offset;
   uint64_t intermediate_bvh_addr = state->build_info->scratchData.deviceAddress + state->scratch.ir_offset;

   VkGeometryTypeKHR geometry_type = vk_get_as_geometry_type(state->build_info);

   STATIC_ASSERT(sizeof(struct anv_accel_struct_header) == ANV_RT_BVH_HEADER_SIZE);
   STATIC_ASSERT(sizeof(struct anv_instance_leaf) == ANV_RT_INSTANCE_LEAF_SIZE);
   STATIC_ASSERT(sizeof(struct anv_quad_leaf_node) == ANV_RT_QUAD_LEAF_SIZE);
   STATIC_ASSERT(sizeof(struct anv_procedural_leaf_node) == ANV_RT_PROCEDURAL_LEAF_SIZE);
   STATIC_ASSERT(sizeof(struct anv_internal_node) == ANV_RT_INTERNAL_NODE_SIZE);

   struct bvh_layout bvh_layout;
   get_bvh_layout(geometry_type, state->leaf_node_count, &bvh_layout);

   const struct encode_args args = {
      .intermediate_bvh = intermediate_bvh_addr,
      .output_bvh = vk_acceleration_structure_get_va(dst) +
                    bvh_layout.bvh_offset,
      .header = intermediate_header_addr,
      .output_bvh_offset = bvh_layout.bvh_offset,
      .leaf_node_count = state->leaf_node_count,
      .geometry_type = geometry_type,
   };
   anv_bvh_build_set_args(commandBuffer, &args, sizeof(args));

   anv_genX(cmd_buffer->device->info, cmd_dispatch_unaligned)
      (commandBuffer, MAX2(state->leaf_node_count, 1), 1, 1);
}

static VkResult
anv_init_header_bind_pipeline(VkCommandBuffer commandBuffer, const struct vk_acceleration_structure_build_state *state)
{
   if (state->config.encode_key[1] == 1) {
      anv_bvh_build_bind_pipeline(commandBuffer,
                                  ANV_OBJECT_KEY_BVH_HEADER,
                                  header_spv, sizeof(header_spv),
                                  sizeof(struct header_args), 0);
   }

   return VK_SUCCESS;
}

static void
anv_init_header(VkCommandBuffer commandBuffer, const struct vk_acceleration_structure_build_state *state)
{
   VK_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, dst, state->build_info->dstAccelerationStructure);

   uint64_t intermediate_header_addr = state->build_info->scratchData.deviceAddress + state->scratch.header_offset;
   uint64_t intermediate_bvh_addr = state->build_info->scratchData.deviceAddress + state->scratch.ir_offset;

   VkGeometryTypeKHR geometry_type = vk_get_as_geometry_type(state->build_info);

   struct bvh_layout bvh_layout;
   get_bvh_layout(geometry_type, state->leaf_node_count, &bvh_layout);

   VkDeviceAddress header_addr = vk_acceleration_structure_get_va(dst);

   uint32_t instance_count = geometry_type == VK_GEOMETRY_TYPE_INSTANCES_KHR ?
                             state->leaf_node_count : 0;

   if (state->config.encode_key[1] == 1) {
      /* Add a barrier to ensure the writes from encode.comp is ready to be
       * read by header.comp
       */
      vk_barrier_compute_w_to_compute_r(commandBuffer);

      struct header_args args = {
         .src = intermediate_header_addr,
         .dst = vk_acceleration_structure_get_va(dst),
         .bvh_offset = bvh_layout.bvh_offset,
         .instance_count = instance_count,
      };

      anv_bvh_build_set_args(commandBuffer, &args, sizeof(args));
      vk_common_CmdDispatch(commandBuffer, 1, 1, 1);
   } else {
      vk_barrier_compute_w_to_host_r(commandBuffer);

      /* L1/L2 caches flushes should have been dealt with by pipeline barriers.
       * Unfortunately some platforms require L3 flush because CS (reading the
       * dispatch size paramters) is not L3 coherent.
       */
      if (!ANV_DEVINFO_HAS_COHERENT_L3_CS(cmd_buffer->device->info)) {
         anv_add_pending_pipe_bits(cmd_buffer, ANV_PIPE_DATA_CACHE_FLUSH_BIT,
                                   "copy dispatch size for dispatch");
         genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);
      }

      size_t base = offsetof(struct anv_accel_struct_header,
                             copy_dispatch_size);

      struct anv_accel_struct_header header = {};

      header.instance_count = instance_count;
      header.self_ptr = header_addr;
      header.compacted_size = bvh_layout.size;

      /* 128 is local_size_x in copy.comp shader, 8 is the amount of data
       * copied by each iteration of that shader's loop
       */
      header.copy_dispatch_size[0] = DIV_ROUND_UP(header.compacted_size,
                                                  8 * 128);
      header.copy_dispatch_size[1] = 1;
      header.copy_dispatch_size[2] = 1;

      header.serialization_size =
         header.compacted_size +
         sizeof(struct vk_accel_struct_serialization_header) +
         sizeof(uint64_t) * header.instance_count;

      header.size = header.compacted_size;

#if GFX_VERx10 >= 300
      header.enable_64b_rt = 1;
#else
      header.enable_64b_rt = 0;
#endif

      size_t header_size = sizeof(struct anv_accel_struct_header) - base;
      assert(base % sizeof(uint32_t) == 0);
      assert(header_size % sizeof(uint32_t) == 0);
      uint32_t *header_ptr = (uint32_t *)((char *)&header + base);

      struct anv_address addr = anv_address_from_u64(header_addr + base);
      anv_cmd_buffer_update_addr(cmd_buffer, addr, header_size, header_ptr);
   }

   if (INTEL_DEBUG_BVH_ANY) {
      genx_batch_emit_pipe_control(&cmd_buffer->batch, cmd_buffer->device->info,
                                   cmd_buffer->state.current_pipeline,
                                   ANV_PIPE_END_OF_PIPE_SYNC_BIT |
                                   ANV_PIPE_DATA_CACHE_FLUSH_BIT |
                                   ANV_PIPE_HDC_PIPELINE_FLUSH_BIT |
                                   ANV_PIPE_UNTYPED_DATAPORT_CACHE_FLUSH_BIT);
      debug_record_as_to_bvh_dump(cmd_buffer, header_addr, bvh_layout.size,
                                  intermediate_header_addr, intermediate_bvh_addr,
                                  state->leaf_node_count, geometry_type);
   }
}

static const struct vk_acceleration_structure_build_ops anv_build_ops = {
   .begin_debug_marker = begin_debug_marker,
   .end_debug_marker = end_debug_marker,
   .get_as_size = anv_get_as_size,
   .get_build_config = anv_get_build_config,
   .encode_bind_pipeline = { anv_encode_bind_pipeline,
                             anv_init_header_bind_pipeline },
   .encode_as = { anv_encode_as, anv_init_header },
};

static VkResult
anv_device_init_accel_struct_build_state(struct anv_device *device)
{
   VkResult result = VK_SUCCESS;
   simple_mtx_lock(&device->accel_struct_build.mutex);

   if (device->accel_struct_build.radix_sort)
      goto exit;

   const struct radix_sort_vk_target_config radix_sort_config = {
      .keyval_dwords = 2,
      .init = { .workgroup_size_log2 = 8, },
      .fill = { .workgroup_size_log2 = 8, .block_rows = 8 },
      .histogram = {
         .workgroup_size_log2 = 8,
         .subgroup_size_log2 = device->info->ver >= 20 ? 4 : 3,
         .block_rows = 14,
      },
      .prefix = {
         .workgroup_size_log2 = 8,
         .subgroup_size_log2 = device->info->ver >= 20 ? 4 : 3,
      },
      .scatter = {
         .workgroup_size_log2 = 8,
         .subgroup_size_log2 = device->info->ver >= 20 ? 4 : 3,
         .block_rows = 14,
      },
   };

   device->accel_struct_build.radix_sort =
      vk_create_radix_sort_u64(anv_device_to_handle(device),
                               &device->vk.alloc,
                               VK_NULL_HANDLE, radix_sort_config);

   device->vk.as_build_ops = &anv_build_ops;
   device->vk.write_buffer_cp = anv_cmd_write_buffer_cp;
   device->vk.flush_buffer_write_cp = anv_cmd_flush_buffer_write_cp;
   device->vk.cmd_dispatch_unaligned = anv_cmd_dispatch_unaligned;
   device->vk.cmd_fill_buffer_addr = anv_cmd_fill_buffer_addr;

   device->accel_struct_build.build_args =
      (struct vk_acceleration_structure_build_args) {
         .emit_markers = u_trace_enabled(&device->ds.trace_context),
         .subgroup_size = device->info->ver >= 20 ? 16 : 8,
         .radix_sort = device->accel_struct_build.radix_sort,
         /* See struct anv_accel_struct_header from anv_bvh.h
          *
          * Root pointer starts at offset 0 and bound box start at offset 8.
          */
         .bvh_bounds_offset = 8,
   };

exit:
   simple_mtx_unlock(&device->accel_struct_build.mutex);
   return result;
}

void
genX(GetAccelerationStructureBuildSizesKHR)(
    VkDevice                                    _device,
    VkAccelerationStructureBuildTypeKHR         buildType,
    const VkAccelerationStructureBuildGeometryInfoKHR* pBuildInfo,
    const uint32_t*                             pMaxPrimitiveCounts,
    VkAccelerationStructureBuildSizesInfoKHR*   pSizeInfo)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   if (anv_device_init_accel_struct_build_state(device) != VK_SUCCESS)
      return;

   vk_get_as_build_sizes(_device, buildType, pBuildInfo, pMaxPrimitiveCounts,
                         pSizeInfo, &device->accel_struct_build.build_args);
}

void
genX(GetDeviceAccelerationStructureCompatibilityKHR)(
    VkDevice                                    _device,
    const VkAccelerationStructureVersionInfoKHR* pVersionInfo,
    VkAccelerationStructureCompatibilityKHR*    pCompatibility)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct vk_accel_struct_serialization_header* ser_header =
      (struct vk_accel_struct_serialization_header*)(pVersionInfo->pVersionData);

   if (memcmp(ser_header->accel_struct_compat,
              device->physical->rt_uuid,
              sizeof(device->physical->rt_uuid)) == 0) {
      *pCompatibility = VK_ACCELERATION_STRUCTURE_COMPATIBILITY_COMPATIBLE_KHR;
   } else {
      *pCompatibility =
         VK_ACCELERATION_STRUCTURE_COMPATIBILITY_INCOMPATIBLE_KHR;
   }
}

void
genX(CmdBuildAccelerationStructuresKHR)(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    infoCount,
    const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
    const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   struct anv_device *device = cmd_buffer->device;

   VkResult result = anv_device_init_accel_struct_build_state(device);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   struct anv_cmd_saved_state saved;
   anv_cmd_buffer_save_state(cmd_buffer,
                             ANV_CMD_SAVED_STATE_COMPUTE_PIPELINE |
                             ANV_CMD_SAVED_STATE_DESCRIPTOR_SET_ALL |
                             ANV_CMD_SAVED_STATE_PUSH_CONSTANTS, &saved);

   /* Apply any outstanding accumulated PC bits before we proceed on building
    * Acceleration Structure.
    *
    * 2 reasons for this :
    *    - some of the data accessed by the build might need to be flushed as a
    *    result of a previous barrier
    *    - the scratch buffer might get reused between builds
    */
   genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);

   vk_cmd_build_acceleration_structures(commandBuffer, &device->vk,
                                        &device->meta_device, infoCount,
                                        pInfos, ppBuildRangeInfos,
                                        &device->accel_struct_build.build_args);

   anv_cmd_buffer_restore_state(cmd_buffer, &saved);
}

void
genX(CmdCopyAccelerationStructureKHR)(
    VkCommandBuffer                             commandBuffer,
    const VkCopyAccelerationStructureInfoKHR*   pInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, src, pInfo->src);
   VK_FROM_HANDLE(vk_acceleration_structure, dst, pInfo->dst);

   trace_intel_begin_as_copy(&cmd_buffer->trace);

   struct anv_cmd_saved_state saved;
   anv_cmd_buffer_save_state(cmd_buffer,
                             ANV_CMD_SAVED_STATE_COMPUTE_PIPELINE |
                             ANV_CMD_SAVED_STATE_DESCRIPTOR_SET_ALL |
                             ANV_CMD_SAVED_STATE_PUSH_CONSTANTS, &saved);

   anv_bvh_build_bind_pipeline(commandBuffer, ANV_OBJECT_KEY_BVH_COPY,
                               copy_spv, sizeof(copy_spv),
                               sizeof(struct copy_args), 0);
   struct copy_args consts = {
      .src_addr = vk_acceleration_structure_get_va(src),
      .dst_addr = vk_acceleration_structure_get_va(dst),
      .mode = ANV_COPY_MODE_COPY,
   };
   anv_bvh_build_set_args(commandBuffer, &consts, sizeof(consts));

   /* L1/L2 caches flushes should have been dealt with by pipeline barriers.
    * Unfortunately some platforms require L3 flush because CS (reading the
    * dispatch paramters) is not L3 coherent.
    */
   if (!ANV_DEVINFO_HAS_COHERENT_L3_CS(cmd_buffer->device->info)) {
      anv_add_pending_pipe_bits(cmd_buffer, ANV_PIPE_DATA_CACHE_FLUSH_BIT,
                                "bvh size read for dispatch");
   }

   anv_genX(cmd_buffer->device->info, CmdDispatchIndirect)(
      commandBuffer, vk_buffer_to_handle(src->buffer),
      src->offset + offsetof(struct anv_accel_struct_header,
                             copy_dispatch_size));

   anv_cmd_buffer_restore_state(cmd_buffer, &saved);

   trace_intel_end_as_copy(&cmd_buffer->trace);
}

void
genX(CmdCopyAccelerationStructureToMemoryKHR)(
    VkCommandBuffer                             commandBuffer,
    const VkCopyAccelerationStructureToMemoryInfoKHR* pInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, src, pInfo->src);
   struct anv_device *device = cmd_buffer->device;

   trace_intel_begin_as_copy(&cmd_buffer->trace);

   struct anv_cmd_saved_state saved;
   anv_cmd_buffer_save_state(cmd_buffer,
                             ANV_CMD_SAVED_STATE_COMPUTE_PIPELINE |
                             ANV_CMD_SAVED_STATE_DESCRIPTOR_SET_ALL |
                             ANV_CMD_SAVED_STATE_PUSH_CONSTANTS, &saved);

   anv_bvh_build_bind_pipeline(commandBuffer, ANV_OBJECT_KEY_BVH_COPY,
                               copy_spv, sizeof(copy_spv),
                               sizeof(struct copy_args), 0);
   struct copy_args consts = {
      .src_addr = vk_acceleration_structure_get_va(src),
      .dst_addr = pInfo->dst.deviceAddress,
      .mode = ANV_COPY_MODE_SERIALIZE,
   };

   memcpy(consts.driver_uuid, device->physical->driver_uuid, VK_UUID_SIZE);
   memcpy(consts.accel_struct_compat, device->physical->rt_uuid, VK_UUID_SIZE);
   anv_bvh_build_set_args(commandBuffer, &consts, sizeof(consts));

   /* L1/L2 caches flushes should have been dealt with by pipeline barriers.
    * Unfortunately some platforms require L3 flush because CS (reading the
    * dispatch paramters) is not L3 coherent.
    */
   if (!ANV_DEVINFO_HAS_COHERENT_L3_CS(cmd_buffer->device->info)) {
      anv_add_pending_pipe_bits(cmd_buffer,
                                ANV_PIPE_DATA_CACHE_FLUSH_BIT,
                                "bvh size read for dispatch");
   }

   anv_genX(device->info, CmdDispatchIndirect)(
      commandBuffer, vk_buffer_to_handle(src->buffer),
      src->offset + offsetof(struct anv_accel_struct_header,
                             copy_dispatch_size));

   anv_cmd_buffer_restore_state(cmd_buffer, &saved);

   trace_intel_end_as_copy(&cmd_buffer->trace);
}

void
genX(CmdCopyMemoryToAccelerationStructureKHR)(
    VkCommandBuffer                             commandBuffer,
    const VkCopyMemoryToAccelerationStructureInfoKHR* pInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, dst, pInfo->dst);

   trace_intel_begin_as_copy(&cmd_buffer->trace);

   struct anv_cmd_saved_state saved;
   anv_cmd_buffer_save_state(cmd_buffer,
                             ANV_CMD_SAVED_STATE_COMPUTE_PIPELINE |
                             ANV_CMD_SAVED_STATE_DESCRIPTOR_SET_ALL |
                             ANV_CMD_SAVED_STATE_PUSH_CONSTANTS, &saved);

   anv_bvh_build_bind_pipeline(commandBuffer, ANV_OBJECT_KEY_BVH_COPY,
                               copy_spv, sizeof(copy_spv),
                               sizeof(struct copy_args), 0);

   const struct copy_args consts = {
      .src_addr = pInfo->src.deviceAddress,
      .dst_addr = vk_acceleration_structure_get_va(dst),
      .mode = ANV_COPY_MODE_DESERIALIZE,
   };
   anv_bvh_build_set_args(commandBuffer, &consts, sizeof(consts));

   vk_common_CmdDispatch(commandBuffer, 512, 1, 1);
   anv_cmd_buffer_restore_state(cmd_buffer, &saved);

   trace_intel_end_as_copy(&cmd_buffer->trace);
}

void
genX(DestroyAccelerationStructureKHR)(
    VkDevice                                    _device,
    VkAccelerationStructureKHR                  accelerationStructure,
    const VkAllocationCallbacks*                pAllocator)
{
   vk_common_DestroyAccelerationStructureKHR(_device, accelerationStructure,
                                             pAllocator);
}
#endif
