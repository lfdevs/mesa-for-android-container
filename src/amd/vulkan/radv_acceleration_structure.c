/*
 * Copyright © 2021 Bas Nieuwenhuizen
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
#include "radv_acceleration_structure.h"
#include "radv_private.h"

#include "util/format/format_utils.h"
#include "util/half_float.h"
#include "nir_builder.h"
#include "radv_cs.h"
#include "radv_meta.h"

#include "radix_sort/radv_radix_sort.h"

/* Min and max bounds of the bvh used to compute morton codes */
#define SCRATCH_TOTAL_BOUNDS_SIZE (6 * sizeof(float))

enum accel_struct_build {
   accel_struct_build_unoptimized,
   accel_struct_build_lbvh,
};

static enum accel_struct_build
get_accel_struct_build(const struct radv_physical_device *pdevice,
                       VkAccelerationStructureBuildTypeKHR buildType)
{
   return buildType == VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR
             ? accel_struct_build_lbvh
             : accel_struct_build_unoptimized;
}

static uint32_t
get_node_id_stride(enum accel_struct_build build_mode)
{
   switch (build_mode) {
   case accel_struct_build_unoptimized:
      return 4;
   case accel_struct_build_lbvh:
      return 8;
   default:
      unreachable("Unhandled accel_struct_build!");
   }
}

VKAPI_ATTR void VKAPI_CALL
radv_GetAccelerationStructureBuildSizesKHR(
   VkDevice _device, VkAccelerationStructureBuildTypeKHR buildType,
   const VkAccelerationStructureBuildGeometryInfoKHR *pBuildInfo,
   const uint32_t *pMaxPrimitiveCounts, VkAccelerationStructureBuildSizesInfoKHR *pSizeInfo)
{
   RADV_FROM_HANDLE(radv_device, device, _device);

   uint64_t triangles = 0, boxes = 0, instances = 0;

   STATIC_ASSERT(sizeof(struct radv_bvh_triangle_node) == 64);
   STATIC_ASSERT(sizeof(struct radv_bvh_aabb_node) == 64);
   STATIC_ASSERT(sizeof(struct radv_bvh_instance_node) == 128);
   STATIC_ASSERT(sizeof(struct radv_bvh_box16_node) == 64);
   STATIC_ASSERT(sizeof(struct radv_bvh_box32_node) == 128);

   for (uint32_t i = 0; i < pBuildInfo->geometryCount; ++i) {
      const VkAccelerationStructureGeometryKHR *geometry;
      if (pBuildInfo->pGeometries)
         geometry = &pBuildInfo->pGeometries[i];
      else
         geometry = pBuildInfo->ppGeometries[i];

      switch (geometry->geometryType) {
      case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
         triangles += pMaxPrimitiveCounts[i];
         break;
      case VK_GEOMETRY_TYPE_AABBS_KHR:
         boxes += pMaxPrimitiveCounts[i];
         break;
      case VK_GEOMETRY_TYPE_INSTANCES_KHR:
         instances += pMaxPrimitiveCounts[i];
         break;
      case VK_GEOMETRY_TYPE_MAX_ENUM_KHR:
         unreachable("VK_GEOMETRY_TYPE_MAX_ENUM_KHR unhandled");
      }
   }

   uint64_t children = boxes + instances + triangles;
   /* Initialize to 1 to have enought space for the root node. */
   uint64_t internal_nodes = 1;
   while (children > 1) {
      children = DIV_ROUND_UP(children, 4);
      internal_nodes += children;
   }

   uint64_t size = boxes * 128 + instances * 128 + triangles * 64 + internal_nodes * 128 +
                   ALIGN(sizeof(struct radv_accel_struct_header), 64);

   pSizeInfo->accelerationStructureSize = size;

   /* 2x the max number of nodes in a BVH layer and order information for sorting when using
    * LBVH (one uint32_t each, two buffers) plus space to store the bounds.
    * LBVH is only supported for device builds and hardware that supports global atomics.
    */
   enum accel_struct_build build_mode = get_accel_struct_build(device->physical_device, buildType);
   uint32_t node_id_stride = get_node_id_stride(build_mode);

   uint32_t leaf_count = boxes + instances + triangles;
   VkDeviceSize scratchSize = 2 * leaf_count * node_id_stride;

   if (build_mode == accel_struct_build_lbvh) {
      radix_sort_vk_memory_requirements_t requirements;
      radix_sort_vk_get_memory_requirements(device->meta_state.accel_struct_build.radix_sort,
                                            leaf_count, &requirements);

      /* Make sure we have the space required by the radix sort. */
      scratchSize = MAX2(scratchSize, requirements.keyvals_size * 2);

      scratchSize += requirements.internal_size + SCRATCH_TOTAL_BOUNDS_SIZE;
   }

   scratchSize = MAX2(4096, scratchSize);
   pSizeInfo->updateScratchSize = scratchSize;
   pSizeInfo->buildScratchSize = scratchSize;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateAccelerationStructureKHR(VkDevice _device,
                                    const VkAccelerationStructureCreateInfoKHR *pCreateInfo,
                                    const VkAllocationCallbacks *pAllocator,
                                    VkAccelerationStructureKHR *pAccelerationStructure)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_buffer, buffer, pCreateInfo->buffer);
   struct radv_acceleration_structure *accel;

   accel = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*accel), 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (accel == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &accel->base, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR);

   accel->mem_offset = buffer->offset + pCreateInfo->offset;
   accel->size = pCreateInfo->size;
   accel->bo = buffer->bo;

   *pAccelerationStructure = radv_acceleration_structure_to_handle(accel);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyAccelerationStructureKHR(VkDevice _device,
                                     VkAccelerationStructureKHR accelerationStructure,
                                     const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_acceleration_structure, accel, accelerationStructure);

   if (!accel)
      return;

   vk_object_base_finish(&accel->base);
   vk_free2(&device->vk.alloc, pAllocator, accel);
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL
radv_GetAccelerationStructureDeviceAddressKHR(
   VkDevice _device, const VkAccelerationStructureDeviceAddressInfoKHR *pInfo)
{
   RADV_FROM_HANDLE(radv_acceleration_structure, accel, pInfo->accelerationStructure);
   return radv_accel_struct_get_va(accel);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_WriteAccelerationStructuresPropertiesKHR(
   VkDevice _device, uint32_t accelerationStructureCount,
   const VkAccelerationStructureKHR *pAccelerationStructures, VkQueryType queryType,
   size_t dataSize, void *pData, size_t stride)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   char *data_out = (char *)pData;

   for (uint32_t i = 0; i < accelerationStructureCount; ++i) {
      RADV_FROM_HANDLE(radv_acceleration_structure, accel, pAccelerationStructures[i]);
      const char *base_ptr = (const char *)device->ws->buffer_map(accel->bo);
      if (!base_ptr)
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

      const struct radv_accel_struct_header *header = (const void *)(base_ptr + accel->mem_offset);
      if (stride * i + sizeof(VkDeviceSize) <= dataSize) {
         uint64_t value;
         switch (queryType) {
         case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR:
            value = header->compacted_size;
            break;
         case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_SIZE_KHR:
            value = header->serialization_size;
            break;
         case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_BOTTOM_LEVEL_POINTERS_KHR:
            value = header->instance_count;
            break;
         case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SIZE_KHR:
            value = header->size;
            break;
         default:
            unreachable("Unhandled acceleration structure query");
         }
         *(VkDeviceSize *)(data_out + stride * i) = value;
      }
      device->ws->buffer_unmap(accel->bo);
   }
   return VK_SUCCESS;
}

struct radv_bvh_build_ctx {
   uint32_t *write_scratch;
   char *base;
   char *curr_ptr;
};

static void
build_triangles(struct radv_bvh_build_ctx *ctx, const VkAccelerationStructureGeometryKHR *geom,
                const VkAccelerationStructureBuildRangeInfoKHR *range, unsigned geometry_id)
{
   const VkAccelerationStructureGeometryTrianglesDataKHR *tri_data = &geom->geometry.triangles;
   VkTransformMatrixKHR matrix;
   const char *index_data = (const char *)tri_data->indexData.hostAddress;
   const char *v_data_base = (const char *)tri_data->vertexData.hostAddress;

   if (tri_data->indexType == VK_INDEX_TYPE_NONE_KHR)
      v_data_base += range->primitiveOffset;
   else
      index_data += range->primitiveOffset;

   if (tri_data->transformData.hostAddress) {
      matrix = *(const VkTransformMatrixKHR *)((const char *)tri_data->transformData.hostAddress +
                                               range->transformOffset);
   } else {
      matrix = (VkTransformMatrixKHR){
         .matrix = {{1.0, 0.0, 0.0, 0.0}, {0.0, 1.0, 0.0, 0.0}, {0.0, 0.0, 1.0, 0.0}}};
   }

   for (uint32_t p = 0; p < range->primitiveCount; ++p, ctx->curr_ptr += 64) {
      struct radv_bvh_triangle_node *node = (void *)ctx->curr_ptr;
      uint32_t node_offset = ctx->curr_ptr - ctx->base;
      uint32_t node_id = node_offset >> 3;
      *ctx->write_scratch++ = node_id;

      for (unsigned v = 0; v < 3; ++v) {
         uint32_t v_index = range->firstVertex;
         switch (tri_data->indexType) {
         case VK_INDEX_TYPE_NONE_KHR:
            v_index += p * 3 + v;
            break;
         case VK_INDEX_TYPE_UINT8_EXT:
            v_index += *(const uint8_t *)index_data;
            index_data += 1;
            break;
         case VK_INDEX_TYPE_UINT16:
            v_index += *(const uint16_t *)index_data;
            index_data += 2;
            break;
         case VK_INDEX_TYPE_UINT32:
            v_index += *(const uint32_t *)index_data;
            index_data += 4;
            break;
         case VK_INDEX_TYPE_MAX_ENUM:
            unreachable("Unhandled VK_INDEX_TYPE_MAX_ENUM");
            break;
         }

         const char *v_data = v_data_base + v_index * tri_data->vertexStride;
         float coords[4];
         switch (tri_data->vertexFormat) {
         case VK_FORMAT_R32G32_SFLOAT:
            coords[0] = *(const float *)(v_data + 0);
            coords[1] = *(const float *)(v_data + 4);
            coords[2] = 0.0f;
            coords[3] = 1.0f;
            break;
         case VK_FORMAT_R32G32B32_SFLOAT:
            coords[0] = *(const float *)(v_data + 0);
            coords[1] = *(const float *)(v_data + 4);
            coords[2] = *(const float *)(v_data + 8);
            coords[3] = 1.0f;
            break;
         case VK_FORMAT_R32G32B32A32_SFLOAT:
            coords[0] = *(const float *)(v_data + 0);
            coords[1] = *(const float *)(v_data + 4);
            coords[2] = *(const float *)(v_data + 8);
            coords[3] = *(const float *)(v_data + 12);
            break;
         case VK_FORMAT_R16G16_SFLOAT:
            coords[0] = _mesa_half_to_float(*(const uint16_t *)(v_data + 0));
            coords[1] = _mesa_half_to_float(*(const uint16_t *)(v_data + 2));
            coords[2] = 0.0f;
            coords[3] = 1.0f;
            break;
         case VK_FORMAT_R16G16B16_SFLOAT:
            coords[0] = _mesa_half_to_float(*(const uint16_t *)(v_data + 0));
            coords[1] = _mesa_half_to_float(*(const uint16_t *)(v_data + 2));
            coords[2] = _mesa_half_to_float(*(const uint16_t *)(v_data + 4));
            coords[3] = 1.0f;
            break;
         case VK_FORMAT_R16G16B16A16_SFLOAT:
            coords[0] = _mesa_half_to_float(*(const uint16_t *)(v_data + 0));
            coords[1] = _mesa_half_to_float(*(const uint16_t *)(v_data + 2));
            coords[2] = _mesa_half_to_float(*(const uint16_t *)(v_data + 4));
            coords[3] = _mesa_half_to_float(*(const uint16_t *)(v_data + 6));
            break;
         case VK_FORMAT_R16G16_SNORM:
            coords[0] = _mesa_snorm_to_float(*(const int16_t *)(v_data + 0), 16);
            coords[1] = _mesa_snorm_to_float(*(const int16_t *)(v_data + 2), 16);
            coords[2] = 0.0f;
            coords[3] = 1.0f;
            break;
         case VK_FORMAT_R16G16_UNORM:
            coords[0] = _mesa_unorm_to_float(*(const uint16_t *)(v_data + 0), 16);
            coords[1] = _mesa_unorm_to_float(*(const uint16_t *)(v_data + 2), 16);
            coords[2] = 0.0f;
            coords[3] = 1.0f;
            break;
         case VK_FORMAT_R16G16B16A16_SNORM:
            coords[0] = _mesa_snorm_to_float(*(const int16_t *)(v_data + 0), 16);
            coords[1] = _mesa_snorm_to_float(*(const int16_t *)(v_data + 2), 16);
            coords[2] = _mesa_snorm_to_float(*(const int16_t *)(v_data + 4), 16);
            coords[3] = _mesa_snorm_to_float(*(const int16_t *)(v_data + 6), 16);
            break;
         case VK_FORMAT_R16G16B16A16_UNORM:
            coords[0] = _mesa_unorm_to_float(*(const uint16_t *)(v_data + 0), 16);
            coords[1] = _mesa_unorm_to_float(*(const uint16_t *)(v_data + 2), 16);
            coords[2] = _mesa_unorm_to_float(*(const uint16_t *)(v_data + 4), 16);
            coords[3] = _mesa_unorm_to_float(*(const uint16_t *)(v_data + 6), 16);
            break;
         case VK_FORMAT_R8G8_SNORM:
            coords[0] = _mesa_snorm_to_float(*(const int8_t *)(v_data + 0), 8);
            coords[1] = _mesa_snorm_to_float(*(const int8_t *)(v_data + 1), 8);
            coords[2] = 0.0f;
            coords[3] = 1.0f;
            break;
         case VK_FORMAT_R8G8_UNORM:
            coords[0] = _mesa_unorm_to_float(*(const uint8_t *)(v_data + 0), 8);
            coords[1] = _mesa_unorm_to_float(*(const uint8_t *)(v_data + 1), 8);
            coords[2] = 0.0f;
            coords[3] = 1.0f;
            break;
         case VK_FORMAT_R8G8B8A8_SNORM:
            coords[0] = _mesa_snorm_to_float(*(const int8_t *)(v_data + 0), 8);
            coords[1] = _mesa_snorm_to_float(*(const int8_t *)(v_data + 1), 8);
            coords[2] = _mesa_snorm_to_float(*(const int8_t *)(v_data + 2), 8);
            coords[3] = _mesa_snorm_to_float(*(const int8_t *)(v_data + 3), 8);
            break;
         case VK_FORMAT_R8G8B8A8_UNORM:
            coords[0] = _mesa_unorm_to_float(*(const uint8_t *)(v_data + 0), 8);
            coords[1] = _mesa_unorm_to_float(*(const uint8_t *)(v_data + 1), 8);
            coords[2] = _mesa_unorm_to_float(*(const uint8_t *)(v_data + 2), 8);
            coords[3] = _mesa_unorm_to_float(*(const uint8_t *)(v_data + 3), 8);
            break;
         case VK_FORMAT_A2B10G10R10_UNORM_PACK32: {
            uint32_t val = *(const uint32_t *)v_data;
            coords[0] = _mesa_unorm_to_float((val >> 0) & 0x3FF, 10);
            coords[1] = _mesa_unorm_to_float((val >> 10) & 0x3FF, 10);
            coords[2] = _mesa_unorm_to_float((val >> 20) & 0x3FF, 10);
            coords[3] = _mesa_unorm_to_float((val >> 30) & 0x3, 2);
         } break;
         default:
            unreachable("Unhandled vertex format in BVH build");
         }

         for (unsigned j = 0; j < 3; ++j) {
            float r = 0;
            for (unsigned k = 0; k < 4; ++k)
               r += matrix.matrix[j][k] * coords[k];
            node->coords[v][j] = r;
         }

         node->triangle_id = p;
         node->geometry_id_and_flags = geometry_id | (geom->flags << 28);

         /* Seems to be needed for IJ, otherwise I = J = ? */
         node->id = 9;
      }
   }
}

static VkResult
build_instances(struct radv_device *device, struct radv_bvh_build_ctx *ctx,
                const VkAccelerationStructureGeometryKHR *geom,
                const VkAccelerationStructureBuildRangeInfoKHR *range)
{
   const VkAccelerationStructureGeometryInstancesDataKHR *inst_data = &geom->geometry.instances;

   for (uint32_t p = 0; p < range->primitiveCount; ++p, ctx->curr_ptr += 128) {
      const char *instance_data =
         (const char *)inst_data->data.hostAddress + range->primitiveOffset;
      const VkAccelerationStructureInstanceKHR *instance =
         inst_data->arrayOfPointers
            ? (((const VkAccelerationStructureInstanceKHR *const *)instance_data)[p])
            : &((const VkAccelerationStructureInstanceKHR *)instance_data)[p];
      if (!instance->accelerationStructureReference) {
         continue;
      }

      struct radv_bvh_instance_node *node = (void *)ctx->curr_ptr;
      uint32_t node_offset = ctx->curr_ptr - ctx->base;
      uint32_t node_id = (node_offset >> 3) | radv_bvh_node_instance;
      *ctx->write_scratch++ = node_id;

      float transform[16], inv_transform[16];
      memcpy(transform, &instance->transform.matrix, sizeof(instance->transform.matrix));
      transform[12] = transform[13] = transform[14] = 0.0f;
      transform[15] = 1.0f;

      util_invert_mat4x4(inv_transform, transform);
      memcpy(node->wto_matrix, inv_transform, sizeof(node->wto_matrix));
      node->wto_matrix[3] = transform[3];
      node->wto_matrix[7] = transform[7];
      node->wto_matrix[11] = transform[11];
      node->custom_instance_and_mask = instance->instanceCustomIndex | (instance->mask << 24);
      node->sbt_offset_and_flags =
         instance->instanceShaderBindingTableRecordOffset | (instance->flags << 24);
      node->instance_id = p;

      for (unsigned i = 0; i < 3; ++i)
         for (unsigned j = 0; j < 3; ++j)
            node->otw_matrix[i * 3 + j] = instance->transform.matrix[j][i];

      RADV_FROM_HANDLE(radv_acceleration_structure, src_accel_struct,
                       (VkAccelerationStructureKHR)instance->accelerationStructureReference);
      const void *src_base = device->ws->buffer_map(src_accel_struct->bo);
      if (!src_base)
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

      src_base = (const char *)src_base + src_accel_struct->mem_offset;
      const struct radv_accel_struct_header *src_header = src_base;
      node->base_ptr = radv_accel_struct_get_va(src_accel_struct) | src_header->root_node_offset;

      for (unsigned j = 0; j < 3; ++j) {
         node->aabb[0][j] = instance->transform.matrix[j][3];
         node->aabb[1][j] = instance->transform.matrix[j][3];
         for (unsigned k = 0; k < 3; ++k) {
            node->aabb[0][j] += MIN2(instance->transform.matrix[j][k] * src_header->aabb[0][k],
                                     instance->transform.matrix[j][k] * src_header->aabb[1][k]);
            node->aabb[1][j] += MAX2(instance->transform.matrix[j][k] * src_header->aabb[0][k],
                                     instance->transform.matrix[j][k] * src_header->aabb[1][k]);
         }
      }
      device->ws->buffer_unmap(src_accel_struct->bo);
   }
   return VK_SUCCESS;
}

static void
build_aabbs(struct radv_bvh_build_ctx *ctx, const VkAccelerationStructureGeometryKHR *geom,
            const VkAccelerationStructureBuildRangeInfoKHR *range, unsigned geometry_id)
{
   const VkAccelerationStructureGeometryAabbsDataKHR *aabb_data = &geom->geometry.aabbs;

   for (uint32_t p = 0; p < range->primitiveCount; ++p, ctx->curr_ptr += 64) {
      struct radv_bvh_aabb_node *node = (void *)ctx->curr_ptr;
      uint32_t node_offset = ctx->curr_ptr - ctx->base;
      uint32_t node_id = (node_offset >> 3) | radv_bvh_node_aabb;
      *ctx->write_scratch++ = node_id;

      const VkAabbPositionsKHR *aabb =
         (const VkAabbPositionsKHR *)((const char *)aabb_data->data.hostAddress +
                                      range->primitiveOffset + p * aabb_data->stride);

      node->aabb[0][0] = aabb->minX;
      node->aabb[0][1] = aabb->minY;
      node->aabb[0][2] = aabb->minZ;
      node->aabb[1][0] = aabb->maxX;
      node->aabb[1][1] = aabb->maxY;
      node->aabb[1][2] = aabb->maxZ;
      node->primitive_id = p;
      node->geometry_id_and_flags = geometry_id;
   }
}

static uint32_t
leaf_node_count(const VkAccelerationStructureBuildGeometryInfoKHR *info,
                const VkAccelerationStructureBuildRangeInfoKHR *ranges)
{
   uint32_t count = 0;
   for (uint32_t i = 0; i < info->geometryCount; ++i) {
      count += ranges[i].primitiveCount;
   }
   return count;
}

static void
compute_bounds(const char *base_ptr, uint32_t node_id, float *bounds)
{
   for (unsigned i = 0; i < 3; ++i)
      bounds[i] = INFINITY;
   for (unsigned i = 0; i < 3; ++i)
      bounds[3 + i] = -INFINITY;

   switch (node_id & 7) {
   case radv_bvh_node_triangle: {
      const struct radv_bvh_triangle_node *node = (const void *)(base_ptr + (node_id / 8 * 64));
      for (unsigned v = 0; v < 3; ++v) {
         for (unsigned j = 0; j < 3; ++j) {
            bounds[j] = MIN2(bounds[j], node->coords[v][j]);
            bounds[3 + j] = MAX2(bounds[3 + j], node->coords[v][j]);
         }
      }
      break;
   }
   case radv_bvh_node_internal: {
      const struct radv_bvh_box32_node *node = (const void *)(base_ptr + (node_id / 8 * 64));
      for (unsigned c2 = 0; c2 < 4; ++c2) {
         if (isnan(node->coords[c2][0][0]))
            continue;
         for (unsigned j = 0; j < 3; ++j) {
            bounds[j] = MIN2(bounds[j], node->coords[c2][0][j]);
            bounds[3 + j] = MAX2(bounds[3 + j], node->coords[c2][1][j]);
         }
      }
      break;
   }
   case radv_bvh_node_instance: {
      const struct radv_bvh_instance_node *node = (const void *)(base_ptr + (node_id / 8 * 64));
      for (unsigned j = 0; j < 3; ++j) {
         bounds[j] = MIN2(bounds[j], node->aabb[0][j]);
         bounds[3 + j] = MAX2(bounds[3 + j], node->aabb[1][j]);
      }
      break;
   }
   case radv_bvh_node_aabb: {
      const struct radv_bvh_aabb_node *node = (const void *)(base_ptr + (node_id / 8 * 64));
      for (unsigned j = 0; j < 3; ++j) {
         bounds[j] = MIN2(bounds[j], node->aabb[0][j]);
         bounds[3 + j] = MAX2(bounds[3 + j], node->aabb[1][j]);
      }
      break;
   }
   }
}

struct bvh_opt_entry {
   uint64_t key;
   uint32_t node_id;
};

static int
bvh_opt_compare(const void *_a, const void *_b)
{
   const struct bvh_opt_entry *a = _a;
   const struct bvh_opt_entry *b = _b;

   if (a->key < b->key)
      return -1;
   if (a->key > b->key)
      return 1;
   if (a->node_id < b->node_id)
      return -1;
   if (a->node_id > b->node_id)
      return 1;
   return 0;
}

static void
optimize_bvh(const char *base_ptr, uint32_t *node_ids, uint32_t node_count)
{
   if (node_count == 0)
      return;

   float bounds[6];
   for (unsigned i = 0; i < 3; ++i)
      bounds[i] = INFINITY;
   for (unsigned i = 0; i < 3; ++i)
      bounds[3 + i] = -INFINITY;

   for (uint32_t i = 0; i < node_count; ++i) {
      float node_bounds[6];
      compute_bounds(base_ptr, node_ids[i], node_bounds);
      for (unsigned j = 0; j < 3; ++j)
         bounds[j] = MIN2(bounds[j], node_bounds[j]);
      for (unsigned j = 0; j < 3; ++j)
         bounds[3 + j] = MAX2(bounds[3 + j], node_bounds[3 + j]);
   }

   struct bvh_opt_entry *entries = calloc(node_count, sizeof(struct bvh_opt_entry));
   if (!entries)
      return;

   for (uint32_t i = 0; i < node_count; ++i) {
      float node_bounds[6];
      compute_bounds(base_ptr, node_ids[i], node_bounds);
      float node_coords[3];
      for (unsigned j = 0; j < 3; ++j)
         node_coords[j] = (node_bounds[j] + node_bounds[3 + j]) * 0.5;
      int32_t coords[3];
      for (unsigned j = 0; j < 3; ++j)
         coords[j] = MAX2(
            MIN2((int32_t)((node_coords[j] - bounds[j]) / (bounds[3 + j] - bounds[j]) * (1 << 21)),
                 (1 << 21) - 1),
            0);
      uint64_t key = 0;
      for (unsigned j = 0; j < 21; ++j)
         for (unsigned k = 0; k < 3; ++k)
            key |= (uint64_t)((coords[k] >> j) & 1) << (j * 3 + k);
      entries[i].key = key;
      entries[i].node_id = node_ids[i];
   }

   qsort(entries, node_count, sizeof(entries[0]), bvh_opt_compare);
   for (unsigned i = 0; i < node_count; ++i)
      node_ids[i] = entries[i].node_id;

   free(entries);
}

static void
fill_accel_struct_header(struct radv_accel_struct_header *header)
{
   /* 16 bytes per invocation, 64 invocations per workgroup */
   header->copy_dispatch_size[0] = DIV_ROUND_UP(header->compacted_size, 16 * 64);
   header->copy_dispatch_size[1] = 1;
   header->copy_dispatch_size[2] = 1;

   header->serialization_size =
      header->compacted_size + align(sizeof(struct radv_accel_struct_serialization_header) +
                                        sizeof(uint64_t) * header->instance_count,
                                     128);

   header->size = header->serialization_size -
                  sizeof(struct radv_accel_struct_serialization_header) -
                  sizeof(uint64_t) * header->instance_count;
}

static VkResult
build_bvh(struct radv_device *device, const VkAccelerationStructureBuildGeometryInfoKHR *info,
          const VkAccelerationStructureBuildRangeInfoKHR *ranges)
{
   RADV_FROM_HANDLE(radv_acceleration_structure, accel, info->dstAccelerationStructure);
   VkResult result = VK_SUCCESS;

   uint32_t *scratch[2];
   scratch[0] = info->scratchData.hostAddress;
   scratch[1] = scratch[0] + leaf_node_count(info, ranges);

   char *base_ptr = (char *)device->ws->buffer_map(accel->bo);
   if (!base_ptr)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   base_ptr = base_ptr + accel->mem_offset;
   struct radv_accel_struct_header *header = (void *)base_ptr;
   void *first_node_ptr = (char *)base_ptr + ALIGN(sizeof(*header), 64);

   struct radv_bvh_build_ctx ctx = {.write_scratch = scratch[0],
                                    .base = base_ptr,
                                    .curr_ptr = (char *)first_node_ptr + 128};

   uint64_t instance_offset = (const char *)ctx.curr_ptr - (const char *)base_ptr;
   uint64_t instance_count = 0;

   /* This initializes the leaf nodes of the BVH all at the same level. */
   for (int inst = 1; inst >= 0; --inst) {
      for (uint32_t i = 0; i < info->geometryCount; ++i) {
         const VkAccelerationStructureGeometryKHR *geom =
            info->pGeometries ? &info->pGeometries[i] : info->ppGeometries[i];

         if ((inst && geom->geometryType != VK_GEOMETRY_TYPE_INSTANCES_KHR) ||
             (!inst && geom->geometryType == VK_GEOMETRY_TYPE_INSTANCES_KHR))
            continue;

         switch (geom->geometryType) {
         case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
            build_triangles(&ctx, geom, ranges + i, i);
            break;
         case VK_GEOMETRY_TYPE_AABBS_KHR:
            build_aabbs(&ctx, geom, ranges + i, i);
            break;
         case VK_GEOMETRY_TYPE_INSTANCES_KHR: {
            result = build_instances(device, &ctx, geom, ranges + i);
            if (result != VK_SUCCESS)
               goto fail;

            instance_count += ranges[i].primitiveCount;
            break;
         }
         case VK_GEOMETRY_TYPE_MAX_ENUM_KHR:
            unreachable("VK_GEOMETRY_TYPE_MAX_ENUM_KHR unhandled");
         }
      }
   }

   uint32_t node_counts[2] = {ctx.write_scratch - scratch[0], 0};
   optimize_bvh(base_ptr, scratch[0], node_counts[0]);
   unsigned d;

   /*
    * This is the most naive BVH building algorithm I could think of:
    * just iteratively builds each level from bottom to top with
    * the children of each node being in-order and tightly packed.
    *
    * Is probably terrible for traversal but should be easy to build an
    * equivalent GPU version.
    */
   for (d = 0; node_counts[d & 1] > 1 || d == 0; ++d) {
      uint32_t child_count = node_counts[d & 1];
      const uint32_t *children = scratch[d & 1];
      uint32_t *dst_ids = scratch[(d & 1) ^ 1];
      unsigned dst_count;
      unsigned child_idx = 0;
      for (dst_count = 0; child_idx < MAX2(1, child_count); ++dst_count, child_idx += 4) {
         unsigned local_child_count = MIN2(4, child_count - child_idx);
         uint32_t child_ids[4];
         float bounds[4][6];

         for (unsigned c = 0; c < local_child_count; ++c) {
            uint32_t id = children[child_idx + c];
            child_ids[c] = id;

            compute_bounds(base_ptr, id, bounds[c]);
         }

         struct radv_bvh_box32_node *node;

         /* Put the root node at base_ptr so the id = 0, which allows some
          * traversal optimizations. */
         if (child_idx == 0 && local_child_count == child_count) {
            node = first_node_ptr;
            header->root_node_offset = ((char *)first_node_ptr - (char *)base_ptr) / 64 * 8 + 5;
         } else {
            uint32_t dst_id = (ctx.curr_ptr - base_ptr) / 64;
            dst_ids[dst_count] = dst_id * 8 + 5;

            node = (void *)ctx.curr_ptr;
            ctx.curr_ptr += 128;
         }

         for (unsigned c = 0; c < local_child_count; ++c) {
            node->children[c] = child_ids[c];
            for (unsigned i = 0; i < 2; ++i)
               for (unsigned j = 0; j < 3; ++j)
                  node->coords[c][i][j] = bounds[c][i * 3 + j];
         }
         for (unsigned c = local_child_count; c < 4; ++c) {
            for (unsigned i = 0; i < 2; ++i)
               for (unsigned j = 0; j < 3; ++j)
                  node->coords[c][i][j] = NAN;
         }
      }

      node_counts[(d & 1) ^ 1] = dst_count;
   }

   compute_bounds(base_ptr, header->root_node_offset, &header->aabb[0][0]);

   header->instance_offset = instance_offset;
   header->instance_count = instance_count;
   header->compacted_size = (char *)ctx.curr_ptr - base_ptr;

   fill_accel_struct_header(header);

fail:
   device->ws->buffer_unmap(accel->bo);
   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_BuildAccelerationStructuresKHR(
   VkDevice _device, VkDeferredOperationKHR deferredOperation, uint32_t infoCount,
   const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
   const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   VkResult result = VK_SUCCESS;

   for (uint32_t i = 0; i < infoCount; ++i) {
      result = build_bvh(device, pInfos + i, ppBuildRangeInfos[i]);
      if (result != VK_SUCCESS)
         break;
   }
   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CopyAccelerationStructureKHR(VkDevice _device, VkDeferredOperationKHR deferredOperation,
                                  const VkCopyAccelerationStructureInfoKHR *pInfo)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_acceleration_structure, src_struct, pInfo->src);
   RADV_FROM_HANDLE(radv_acceleration_structure, dst_struct, pInfo->dst);

   char *src_ptr = (char *)device->ws->buffer_map(src_struct->bo);
   if (!src_ptr)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   char *dst_ptr = (char *)device->ws->buffer_map(dst_struct->bo);
   if (!dst_ptr) {
      device->ws->buffer_unmap(src_struct->bo);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   src_ptr += src_struct->mem_offset;
   dst_ptr += dst_struct->mem_offset;

   const struct radv_accel_struct_header *header = (const void *)src_ptr;
   memcpy(dst_ptr, src_ptr, header->compacted_size);

   device->ws->buffer_unmap(src_struct->bo);
   device->ws->buffer_unmap(dst_struct->bo);
   return VK_SUCCESS;
}

static nir_builder
create_accel_build_shader(struct radv_device *device, const char *name)
{
   nir_builder b = radv_meta_init_shader(device, MESA_SHADER_COMPUTE, "%s", name);
   b.shader->info.workgroup_size[0] = 64;

   assert(b.shader->info.workgroup_size[1] == 1);
   assert(b.shader->info.workgroup_size[2] == 1);
   assert(!b.shader->info.workgroup_size_variable);

   return b;
}

static nir_ssa_def *
get_indices(nir_builder *b, nir_ssa_def *addr, nir_ssa_def *type, nir_ssa_def *id)
{
   const struct glsl_type *uvec3_type = glsl_vector_type(GLSL_TYPE_UINT, 3);
   nir_variable *result =
      nir_variable_create(b->shader, nir_var_shader_temp, uvec3_type, "indices");

   nir_push_if(b, nir_ult(b, type, nir_imm_int(b, 2)));
   nir_push_if(b, nir_ieq_imm(b, type, VK_INDEX_TYPE_UINT16));
   {
      nir_ssa_def *index_id = nir_umul24(b, id, nir_imm_int(b, 6));
      nir_ssa_def *indices[3];
      for (unsigned i = 0; i < 3; ++i) {
         indices[i] = nir_build_load_global(
            b, 1, 16, nir_iadd(b, addr, nir_u2u64(b, nir_iadd_imm(b, index_id, 2 * i))));
      }
      nir_store_var(b, result, nir_u2u32(b, nir_vec(b, indices, 3)), 7);
   }
   nir_push_else(b, NULL);
   {
      nir_ssa_def *index_id = nir_umul24(b, id, nir_imm_int(b, 12));
      nir_ssa_def *indices =
         nir_build_load_global(b, 3, 32, nir_iadd(b, addr, nir_u2u64(b, index_id)));
      nir_store_var(b, result, indices, 7);
   }
   nir_pop_if(b, NULL);
   nir_push_else(b, NULL);
   {
      nir_ssa_def *index_id = nir_umul24(b, id, nir_imm_int(b, 3));
      nir_ssa_def *indices[] = {
         index_id,
         nir_iadd_imm(b, index_id, 1),
         nir_iadd_imm(b, index_id, 2),
      };

      nir_push_if(b, nir_ieq_imm(b, type, VK_INDEX_TYPE_NONE_KHR));
      {
         nir_store_var(b, result, nir_vec(b, indices, 3), 7);
      }
      nir_push_else(b, NULL);
      {
         for (unsigned i = 0; i < 3; ++i) {
            indices[i] =
               nir_build_load_global(b, 1, 8, nir_iadd(b, addr, nir_u2u64(b, indices[i])));
         }
         nir_store_var(b, result, nir_u2u32(b, nir_vec(b, indices, 3)), 7);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);
   return nir_load_var(b, result);
}

static void
get_vertices(nir_builder *b, nir_ssa_def *addresses, nir_ssa_def *format, nir_ssa_def *positions[3])
{
   const struct glsl_type *vec3_type = glsl_vector_type(GLSL_TYPE_FLOAT, 3);
   nir_variable *results[3] = {
      nir_variable_create(b->shader, nir_var_shader_temp, vec3_type, "vertex0"),
      nir_variable_create(b->shader, nir_var_shader_temp, vec3_type, "vertex1"),
      nir_variable_create(b->shader, nir_var_shader_temp, vec3_type, "vertex2")};

   VkFormat formats[] = {
      VK_FORMAT_R32G32B32_SFLOAT,
      VK_FORMAT_R32G32B32A32_SFLOAT,
      VK_FORMAT_R16G16B16_SFLOAT,
      VK_FORMAT_R16G16B16A16_SFLOAT,
      VK_FORMAT_R16G16_SFLOAT,
      VK_FORMAT_R32G32_SFLOAT,
      VK_FORMAT_R16G16_SNORM,
      VK_FORMAT_R16G16_UNORM,
      VK_FORMAT_R16G16B16A16_SNORM,
      VK_FORMAT_R16G16B16A16_UNORM,
      VK_FORMAT_R8G8_SNORM,
      VK_FORMAT_R8G8_UNORM,
      VK_FORMAT_R8G8B8A8_SNORM,
      VK_FORMAT_R8G8B8A8_UNORM,
      VK_FORMAT_A2B10G10R10_UNORM_PACK32,
   };

   for (unsigned f = 0; f < ARRAY_SIZE(formats); ++f) {
      if (f + 1 < ARRAY_SIZE(formats))
         nir_push_if(b, nir_ieq_imm(b, format, formats[f]));

      for (unsigned i = 0; i < 3; ++i) {
         switch (formats[f]) {
         case VK_FORMAT_R32G32B32_SFLOAT:
         case VK_FORMAT_R32G32B32A32_SFLOAT:
            nir_store_var(b, results[i],
                          nir_build_load_global(b, 3, 32, nir_channel(b, addresses, i)), 7);
            break;
         case VK_FORMAT_R32G32_SFLOAT:
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
         case VK_FORMAT_A2B10G10R10_UNORM_PACK32: {
            unsigned components = MIN2(3, vk_format_get_nr_components(formats[f]));
            unsigned comp_bits =
               vk_format_get_blocksizebits(formats[f]) / vk_format_get_nr_components(formats[f]);
            unsigned comp_bytes = comp_bits / 8;
            nir_ssa_def *values[3];
            nir_ssa_def *addr = nir_channel(b, addresses, i);

            if (formats[f] == VK_FORMAT_A2B10G10R10_UNORM_PACK32) {
               comp_bits = 10;
               nir_ssa_def *val = nir_build_load_global(b, 1, 32, addr);
               for (unsigned j = 0; j < 3; ++j)
                  values[j] = nir_ubfe(b, val, nir_imm_int(b, j * 10), nir_imm_int(b, 10));
            } else {
               for (unsigned j = 0; j < components; ++j)
                  values[j] =
                     nir_build_load_global(b, 1, comp_bits, nir_iadd_imm(b, addr, j * comp_bytes));

               for (unsigned j = components; j < 3; ++j)
                  values[j] = nir_imm_intN_t(b, 0, comp_bits);
            }

            nir_ssa_def *vec;
            if (util_format_is_snorm(vk_format_to_pipe_format(formats[f]))) {
               for (unsigned j = 0; j < 3; ++j) {
                  values[j] = nir_fdiv(b, nir_i2f32(b, values[j]),
                                       nir_imm_float(b, (1u << (comp_bits - 1)) - 1));
                  values[j] = nir_fmax(b, values[j], nir_imm_float(b, -1.0));
               }
               vec = nir_vec(b, values, 3);
            } else if (util_format_is_unorm(vk_format_to_pipe_format(formats[f]))) {
               for (unsigned j = 0; j < 3; ++j) {
                  values[j] =
                     nir_fdiv(b, nir_u2f32(b, values[j]), nir_imm_float(b, (1u << comp_bits) - 1));
                  values[j] = nir_fmin(b, values[j], nir_imm_float(b, 1.0));
               }
               vec = nir_vec(b, values, 3);
            } else if (comp_bits == 16)
               vec = nir_f2f32(b, nir_vec(b, values, 3));
            else
               vec = nir_vec(b, values, 3);
            nir_store_var(b, results[i], vec, 7);
            break;
         }
         default:
            unreachable("Unhandled format");
         }
      }
      if (f + 1 < ARRAY_SIZE(formats))
         nir_push_else(b, NULL);
   }
   for (unsigned f = 1; f < ARRAY_SIZE(formats); ++f) {
      nir_pop_if(b, NULL);
   }

   for (unsigned i = 0; i < 3; ++i)
      positions[i] = nir_load_var(b, results[i]);
}

struct build_primitive_constants {
   uint64_t node_dst_addr;
   uint64_t scratch_addr;
   uint32_t dst_offset;
   uint32_t dst_scratch_offset;
   uint32_t geometry_type;
   uint32_t geometry_id;

   union {
      struct {
         uint64_t vertex_addr;
         uint64_t index_addr;
         uint64_t transform_addr;
         uint32_t vertex_stride;
         uint32_t vertex_format;
         uint32_t index_format;
      };
      struct {
         uint64_t instance_data;
         uint32_t array_of_pointers;
      };
      struct {
         uint64_t aabb_addr;
         uint32_t aabb_stride;
      };
   };
};

struct bounds_constants {
   uint64_t node_addr;
   uint64_t scratch_addr;
};

struct morton_constants {
   uint64_t node_addr;
   uint64_t scratch_addr;
};

struct fill_constants {
   uint64_t addr;
   uint32_t value;
};

struct build_internal_constants {
   uint64_t node_dst_addr;
   uint64_t scratch_addr;
   uint32_t dst_offset;
   uint32_t dst_scratch_offset;
   uint32_t src_scratch_offset;
   uint32_t fill_header;
};

/* This inverts a 3x3 matrix using cofactors, as in e.g.
 * https://www.mathsisfun.com/algebra/matrix-inverse-minors-cofactors-adjugate.html */
static void
nir_invert_3x3(nir_builder *b, nir_ssa_def *in[3][3], nir_ssa_def *out[3][3])
{
   nir_ssa_def *cofactors[3][3];
   for (unsigned i = 0; i < 3; ++i) {
      for (unsigned j = 0; j < 3; ++j) {
         cofactors[i][j] =
            nir_fsub(b, nir_fmul(b, in[(i + 1) % 3][(j + 1) % 3], in[(i + 2) % 3][(j + 2) % 3]),
                     nir_fmul(b, in[(i + 1) % 3][(j + 2) % 3], in[(i + 2) % 3][(j + 1) % 3]));
      }
   }

   nir_ssa_def *det = NULL;
   for (unsigned i = 0; i < 3; ++i) {
      nir_ssa_def *det_part = nir_fmul(b, in[0][i], cofactors[0][i]);
      det = det ? nir_fadd(b, det, det_part) : det_part;
   }

   nir_ssa_def *det_inv = nir_frcp(b, det);
   for (unsigned i = 0; i < 3; ++i) {
      for (unsigned j = 0; j < 3; ++j) {
         out[i][j] = nir_fmul(b, cofactors[j][i], det_inv);
      }
   }
}

static nir_ssa_def *
id_to_node_id_offset(nir_builder *b, nir_ssa_def *global_id,
                     const struct radv_physical_device *pdevice)
{
   uint32_t stride = get_node_id_stride(
      get_accel_struct_build(pdevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR));

   return nir_imul_imm(b, global_id, stride);
}

static nir_ssa_def *
id_to_morton_offset(nir_builder *b, nir_ssa_def *global_id,
                    const struct radv_physical_device *pdevice)
{
   enum accel_struct_build build_mode =
      get_accel_struct_build(pdevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR);
   assert(build_mode == accel_struct_build_lbvh);

   uint32_t stride = get_node_id_stride(build_mode);

   return nir_iadd_imm(b, nir_imul_imm(b, global_id, stride), sizeof(uint32_t));
}

static void
atomic_fminmax(struct radv_device *dev, nir_builder *b, nir_ssa_def *addr, bool is_max,
               nir_ssa_def *val)
{
   if (radv_has_shader_buffer_float_minmax(dev->physical_device)) {
      if (is_max)
         nir_global_atomic_fmax(b, 32, addr, val);
      else
         nir_global_atomic_fmin(b, 32, addr, val);
      return;
   }

   /* Use an integer comparison to work correctly with negative zero. */
   val = nir_bcsel(b, nir_ilt(b, val, nir_imm_int(b, 0)),
                   nir_isub(b, nir_imm_int(b, -2147483648), val), val);

   if (is_max)
      nir_global_atomic_imax(b, 32, addr, val);
   else
      nir_global_atomic_imin(b, 32, addr, val);
}

static nir_ssa_def *
read_fminmax_atomic(struct radv_device *dev, nir_builder *b, unsigned channels, nir_ssa_def *addr)
{
   nir_ssa_def *val = nir_build_load_global(b, channels, 32, addr,
                                            .access = ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER);

   if (radv_has_shader_buffer_float_minmax(dev->physical_device))
      return val;

   return nir_bcsel(b, nir_ilt(b, val, nir_imm_int(b, 0)),
                    nir_isub(b, nir_imm_int(b, -2147483648), val), val);
}

static nir_shader *
build_leaf_shader(struct radv_device *dev)
{
   enum accel_struct_build build_mode =
      get_accel_struct_build(dev->physical_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR);

   const struct glsl_type *vec3_type = glsl_vector_type(GLSL_TYPE_FLOAT, 3);
   nir_builder b = create_accel_build_shader(dev, "accel_build_leaf_shader");

   nir_ssa_def *pconst0 =
      nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .base = 0, .range = 16);
   nir_ssa_def *pconst1 =
      nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .base = 16, .range = 16);
   nir_ssa_def *pconst2 =
      nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .base = 32, .range = 16);
   nir_ssa_def *pconst3 =
      nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .base = 48, .range = 16);
   nir_ssa_def *index_format =
      nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 0), .base = 64, .range = 4);

   nir_ssa_def *node_dst_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst0, 0b0011));
   nir_ssa_def *scratch_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst0, 0b1100));
   nir_ssa_def *node_dst_offset = nir_channel(&b, pconst1, 0);
   nir_ssa_def *scratch_offset = nir_channel(&b, pconst1, 1);
   nir_ssa_def *geom_type = nir_channel(&b, pconst1, 2);
   nir_ssa_def *geometry_id = nir_channel(&b, pconst1, 3);

   nir_ssa_def *global_id =
      nir_iadd(&b,
               nir_imul_imm(&b, nir_channels(&b, nir_load_workgroup_id(&b, 32), 1),
                            b.shader->info.workgroup_size[0]),
               nir_channels(&b, nir_load_local_invocation_id(&b), 1));
   nir_ssa_def *scratch_dst_addr =
      nir_iadd(&b, scratch_addr,
               nir_u2u64(&b, nir_iadd(&b, scratch_offset,
                                      id_to_node_id_offset(&b, global_id, dev->physical_device))));
   if (build_mode != accel_struct_build_unoptimized)
      scratch_dst_addr = nir_iadd_imm(&b, scratch_dst_addr, SCRATCH_TOTAL_BOUNDS_SIZE);

   nir_variable *bounds[2] = {
      nir_variable_create(b.shader, nir_var_shader_temp, vec3_type, "min_bound"),
      nir_variable_create(b.shader, nir_var_shader_temp, vec3_type, "max_bound"),
   };

   nir_push_if(&b, nir_ieq_imm(&b, geom_type, VK_GEOMETRY_TYPE_TRIANGLES_KHR));
   { /* Triangles */
      nir_ssa_def *vertex_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst2, 0b0011));
      nir_ssa_def *index_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst2, 0b1100));
      nir_ssa_def *transform_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst3, 3));
      nir_ssa_def *vertex_stride = nir_channel(&b, pconst3, 2);
      nir_ssa_def *vertex_format = nir_channel(&b, pconst3, 3);
      unsigned repl_swizzle[4] = {0, 0, 0, 0};

      nir_ssa_def *node_offset = nir_iadd(&b, node_dst_offset, nir_imul_imm(&b, global_id, 64));
      nir_ssa_def *triangle_node_dst_addr = nir_iadd(&b, node_dst_addr, nir_u2u64(&b, node_offset));

      nir_ssa_def *indices = get_indices(&b, index_addr, index_format, global_id);
      nir_ssa_def *vertex_addresses = nir_iadd(
         &b, nir_u2u64(&b, nir_imul(&b, indices, nir_swizzle(&b, vertex_stride, repl_swizzle, 3))),
         nir_swizzle(&b, vertex_addr, repl_swizzle, 3));
      nir_ssa_def *positions[3];
      get_vertices(&b, vertex_addresses, vertex_format, positions);

      nir_ssa_def *node_data[16];
      memset(node_data, 0, sizeof(node_data));

      nir_variable *transform[] = {
         nir_variable_create(b.shader, nir_var_shader_temp, glsl_vec4_type(), "transform0"),
         nir_variable_create(b.shader, nir_var_shader_temp, glsl_vec4_type(), "transform1"),
         nir_variable_create(b.shader, nir_var_shader_temp, glsl_vec4_type(), "transform2"),
      };
      nir_store_var(&b, transform[0], nir_imm_vec4(&b, 1.0, 0.0, 0.0, 0.0), 0xf);
      nir_store_var(&b, transform[1], nir_imm_vec4(&b, 0.0, 1.0, 0.0, 0.0), 0xf);
      nir_store_var(&b, transform[2], nir_imm_vec4(&b, 0.0, 0.0, 1.0, 0.0), 0xf);

      nir_push_if(&b, nir_ine_imm(&b, transform_addr, 0));
      nir_store_var(&b, transform[0],
                    nir_build_load_global(&b, 4, 32, nir_iadd_imm(&b, transform_addr, 0),
                                          .access = ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER),
                    0xf);
      nir_store_var(&b, transform[1],
                    nir_build_load_global(&b, 4, 32, nir_iadd_imm(&b, transform_addr, 16),
                                          .access = ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER),
                    0xf);
      nir_store_var(&b, transform[2],
                    nir_build_load_global(&b, 4, 32, nir_iadd_imm(&b, transform_addr, 32),
                                          .access = ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER),
                    0xf);
      nir_pop_if(&b, NULL);

      for (unsigned i = 0; i < 3; ++i)
         for (unsigned j = 0; j < 3; ++j)
            node_data[i * 3 + j] = nir_fdph(&b, positions[i], nir_load_var(&b, transform[j]));

      nir_ssa_def *min_bound = NULL;
      nir_ssa_def *max_bound = NULL;
      for (unsigned i = 0; i < 3; ++i) {
         nir_ssa_def *position = nir_vec(&b, node_data + i * 3, 3);
         if (min_bound) {
            min_bound = nir_fmin(&b, min_bound, position);
            max_bound = nir_fmax(&b, max_bound, position);
         } else {
            min_bound = position;
            max_bound = position;
         }
      }

      nir_store_var(&b, bounds[0], min_bound, 7);
      nir_store_var(&b, bounds[1], max_bound, 7);

      node_data[12] = global_id;
      node_data[13] = geometry_id;
      node_data[15] = nir_imm_int(&b, 9);
      for (unsigned i = 0; i < ARRAY_SIZE(node_data); ++i)
         if (!node_data[i])
            node_data[i] = nir_imm_int(&b, 0);

      for (unsigned i = 0; i < 4; ++i) {
         nir_build_store_global(&b, nir_vec(&b, node_data + i * 4, 4),
                                nir_iadd_imm(&b, triangle_node_dst_addr, i * 16), .align_mul = 16);
      }

      nir_ssa_def *node_id =
         nir_iadd_imm(&b, nir_ushr_imm(&b, node_offset, 3), radv_bvh_node_triangle);
      nir_build_store_global(&b, node_id, scratch_dst_addr);
   }
   nir_push_else(&b, NULL);
   nir_push_if(&b, nir_ieq_imm(&b, geom_type, VK_GEOMETRY_TYPE_AABBS_KHR));
   { /* AABBs */
      nir_ssa_def *aabb_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst2, 0b0011));
      nir_ssa_def *aabb_stride = nir_channel(&b, pconst2, 2);

      nir_ssa_def *node_offset = nir_iadd(&b, node_dst_offset, nir_imul_imm(&b, global_id, 64));
      nir_ssa_def *aabb_node_dst_addr = nir_iadd(&b, node_dst_addr, nir_u2u64(&b, node_offset));

      nir_ssa_def *node_id = nir_iadd_imm(&b, nir_ushr_imm(&b, node_offset, 3), radv_bvh_node_aabb);
      nir_build_store_global(&b, node_id, scratch_dst_addr);

      aabb_addr = nir_iadd(&b, aabb_addr, nir_u2u64(&b, nir_imul(&b, aabb_stride, global_id)));

      nir_ssa_def *min_bound =
         nir_build_load_global(&b, 3, 32, nir_iadd_imm(&b, aabb_addr, 0),
                               .access = ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER);
      nir_ssa_def *max_bound =
         nir_build_load_global(&b, 3, 32, nir_iadd_imm(&b, aabb_addr, 12),
                               .access = ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER);

      nir_store_var(&b, bounds[0], min_bound, 7);
      nir_store_var(&b, bounds[1], max_bound, 7);

      nir_ssa_def *values[] = {nir_channel(&b, min_bound, 0),
                               nir_channel(&b, min_bound, 1),
                               nir_channel(&b, min_bound, 2),
                               nir_channel(&b, max_bound, 0),
                               nir_channel(&b, max_bound, 1),
                               nir_channel(&b, max_bound, 2),
                               global_id,
                               geometry_id};

      nir_build_store_global(&b, nir_vec(&b, values + 0, 4),
                             nir_iadd_imm(&b, aabb_node_dst_addr, 0), .align_mul = 16);
      nir_build_store_global(&b, nir_vec(&b, values + 4, 4),
                             nir_iadd_imm(&b, aabb_node_dst_addr, 16), .align_mul = 16);
   }
   nir_push_else(&b, NULL);
   { /* Instances */

      nir_variable *instance_addr_var =
         nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint64_t_type(), "instance_addr");
      nir_push_if(&b, nir_ine_imm(&b, nir_channel(&b, pconst2, 2), 0));
      {
         nir_ssa_def *ptr = nir_iadd(&b, nir_pack_64_2x32(&b, nir_channels(&b, pconst2, 0b0011)),
                                     nir_u2u64(&b, nir_imul_imm(&b, global_id, 8)));
         nir_ssa_def *addr =
            nir_pack_64_2x32(&b, nir_build_load_global(&b, 2, 32, ptr, .align_mul = 8));
         nir_store_var(&b, instance_addr_var, addr, 1);
      }
      nir_push_else(&b, NULL);
      {
         nir_ssa_def *addr = nir_iadd(&b, nir_pack_64_2x32(&b, nir_channels(&b, pconst2, 0b0011)),
                                      nir_u2u64(&b, nir_imul_imm(&b, global_id, 64)));
         nir_store_var(&b, instance_addr_var, addr, 1);
      }
      nir_pop_if(&b, NULL);
      nir_ssa_def *instance_addr = nir_load_var(&b, instance_addr_var);

      nir_ssa_def *inst_transform[] = {
         nir_build_load_global(&b, 4, 32, nir_iadd_imm(&b, instance_addr, 0)),
         nir_build_load_global(&b, 4, 32, nir_iadd_imm(&b, instance_addr, 16)),
         nir_build_load_global(&b, 4, 32, nir_iadd_imm(&b, instance_addr, 32))};
      nir_ssa_def *inst3 = nir_build_load_global(&b, 4, 32, nir_iadd_imm(&b, instance_addr, 48));

      nir_ssa_def *node_offset = nir_iadd(&b, node_dst_offset, nir_imul_imm(&b, global_id, 128));
      node_dst_addr = nir_iadd(&b, node_dst_addr, nir_u2u64(&b, node_offset));

      nir_ssa_def *node_id =
         nir_iadd_imm(&b, nir_ushr_imm(&b, node_offset, 3), radv_bvh_node_instance);
      nir_build_store_global(&b, node_id, scratch_dst_addr);

      nir_ssa_def *header_addr = nir_pack_64_2x32(&b, nir_channels(&b, inst3, 12));
      nir_push_if(&b, nir_ine_imm(&b, header_addr, 0));
      nir_ssa_def *header_root_offset =
         nir_build_load_global(&b, 1, 32, nir_iadd_imm(&b, header_addr, 0));
      nir_ssa_def *header_min = nir_build_load_global(&b, 3, 32, nir_iadd_imm(&b, header_addr, 8));
      nir_ssa_def *header_max = nir_build_load_global(&b, 3, 32, nir_iadd_imm(&b, header_addr, 20));

      nir_ssa_def *bound_defs[2][3];
      for (unsigned i = 0; i < 3; ++i) {
         bound_defs[0][i] = bound_defs[1][i] = nir_channel(&b, inst_transform[i], 3);

         nir_ssa_def *mul_a = nir_fmul(&b, nir_channels(&b, inst_transform[i], 7), header_min);
         nir_ssa_def *mul_b = nir_fmul(&b, nir_channels(&b, inst_transform[i], 7), header_max);
         nir_ssa_def *mi = nir_fmin(&b, mul_a, mul_b);
         nir_ssa_def *ma = nir_fmax(&b, mul_a, mul_b);
         for (unsigned j = 0; j < 3; ++j) {
            bound_defs[0][i] = nir_fadd(&b, bound_defs[0][i], nir_channel(&b, mi, j));
            bound_defs[1][i] = nir_fadd(&b, bound_defs[1][i], nir_channel(&b, ma, j));
         }
      }

      nir_store_var(&b, bounds[0], nir_vec(&b, bound_defs[0], 3), 7);
      nir_store_var(&b, bounds[1], nir_vec(&b, bound_defs[1], 3), 7);

      /* Store object to world matrix */
      for (unsigned i = 0; i < 3; ++i) {
         nir_ssa_def *vals[3];
         for (unsigned j = 0; j < 3; ++j)
            vals[j] = nir_channel(&b, inst_transform[j], i);

         nir_build_store_global(&b, nir_vec(&b, vals, 3),
                                nir_iadd_imm(&b, node_dst_addr, 92 + 12 * i));
      }

      nir_ssa_def *m_in[3][3], *m_out[3][3], *m_vec[3][4];
      for (unsigned i = 0; i < 3; ++i)
         for (unsigned j = 0; j < 3; ++j)
            m_in[i][j] = nir_channel(&b, inst_transform[i], j);
      nir_invert_3x3(&b, m_in, m_out);
      for (unsigned i = 0; i < 3; ++i) {
         for (unsigned j = 0; j < 3; ++j)
            m_vec[i][j] = m_out[i][j];
         m_vec[i][3] = nir_channel(&b, inst_transform[i], 3);
      }

      for (unsigned i = 0; i < 3; ++i) {
         nir_build_store_global(&b, nir_vec(&b, m_vec[i], 4),
                                nir_iadd_imm(&b, node_dst_addr, 16 + 16 * i));
      }

      nir_ssa_def *out0[4] = {
         nir_ior(&b, nir_channel(&b, nir_unpack_64_2x32(&b, header_addr), 0), header_root_offset),
         nir_channel(&b, nir_unpack_64_2x32(&b, header_addr), 1), nir_channel(&b, inst3, 0),
         nir_channel(&b, inst3, 1)};
      nir_build_store_global(&b, nir_vec(&b, out0, 4), nir_iadd_imm(&b, node_dst_addr, 0));
      nir_build_store_global(&b, global_id, nir_iadd_imm(&b, node_dst_addr, 88));
      nir_pop_if(&b, NULL);
      nir_build_store_global(&b, nir_load_var(&b, bounds[0]), nir_iadd_imm(&b, node_dst_addr, 64));
      nir_build_store_global(&b, nir_load_var(&b, bounds[1]), nir_iadd_imm(&b, node_dst_addr, 76));
   }
   nir_pop_if(&b, NULL);
   nir_pop_if(&b, NULL);

   if (build_mode != accel_struct_build_unoptimized) {
      nir_ssa_def *min = nir_load_var(&b, bounds[0]);
      nir_ssa_def *max = nir_load_var(&b, bounds[1]);

      nir_ssa_def *min_reduced = nir_reduce(&b, min, .reduction_op = nir_op_fmin);
      nir_ssa_def *max_reduced = nir_reduce(&b, max, .reduction_op = nir_op_fmax);

      nir_push_if(&b, nir_elect(&b, 1));

      atomic_fminmax(dev, &b, scratch_addr, false, nir_channel(&b, min_reduced, 0));
      atomic_fminmax(dev, &b, nir_iadd_imm(&b, scratch_addr, 4), false,
                     nir_channel(&b, min_reduced, 1));
      atomic_fminmax(dev, &b, nir_iadd_imm(&b, scratch_addr, 8), false,
                     nir_channel(&b, min_reduced, 2));

      atomic_fminmax(dev, &b, nir_iadd_imm(&b, scratch_addr, 12), true,
                     nir_channel(&b, max_reduced, 0));
      atomic_fminmax(dev, &b, nir_iadd_imm(&b, scratch_addr, 16), true,
                     nir_channel(&b, max_reduced, 1));
      atomic_fminmax(dev, &b, nir_iadd_imm(&b, scratch_addr, 20), true,
                     nir_channel(&b, max_reduced, 2));
   }

   return b.shader;
}

static void
determine_bounds(nir_builder *b, nir_ssa_def *node_addr, nir_ssa_def *node_id,
                 nir_variable *bounds_vars[2])
{
   nir_ssa_def *node_type = nir_iand_imm(b, node_id, 7);
   node_addr =
      nir_iadd(b, node_addr, nir_u2u64(b, nir_ishl_imm(b, nir_iand_imm(b, node_id, ~7u), 3)));

   nir_push_if(b, nir_ieq_imm(b, node_type, radv_bvh_node_triangle));
   {
      nir_ssa_def *positions[3];
      for (unsigned i = 0; i < 3; ++i)
         positions[i] = nir_build_load_global(b, 3, 32, nir_iadd_imm(b, node_addr, i * 12));
      nir_ssa_def *bounds[] = {positions[0], positions[0]};
      for (unsigned i = 1; i < 3; ++i) {
         bounds[0] = nir_fmin(b, bounds[0], positions[i]);
         bounds[1] = nir_fmax(b, bounds[1], positions[i]);
      }
      nir_store_var(b, bounds_vars[0], bounds[0], 7);
      nir_store_var(b, bounds_vars[1], bounds[1], 7);
   }
   nir_push_else(b, NULL);
   nir_push_if(b, nir_ieq_imm(b, node_type, radv_bvh_node_internal));
   {
      nir_ssa_def *input_bounds[4][2];
      for (unsigned i = 0; i < 4; ++i)
         for (unsigned j = 0; j < 2; ++j)
            input_bounds[i][j] =
               nir_build_load_global(b, 3, 32, nir_iadd_imm(b, node_addr, 16 + i * 24 + j * 12));
      nir_ssa_def *bounds[] = {input_bounds[0][0], input_bounds[0][1]};
      for (unsigned i = 1; i < 4; ++i) {
         bounds[0] = nir_fmin(b, bounds[0], input_bounds[i][0]);
         bounds[1] = nir_fmax(b, bounds[1], input_bounds[i][1]);
      }

      nir_store_var(b, bounds_vars[0], bounds[0], 7);
      nir_store_var(b, bounds_vars[1], bounds[1], 7);
   }
   nir_push_else(b, NULL);
   nir_push_if(b, nir_ieq_imm(b, node_type, radv_bvh_node_instance));
   { /* Instances */
      nir_ssa_def *bounds[2];
      for (unsigned i = 0; i < 2; ++i)
         bounds[i] = nir_build_load_global(b, 3, 32, nir_iadd_imm(b, node_addr, 64 + i * 12));
      nir_store_var(b, bounds_vars[0], bounds[0], 7);
      nir_store_var(b, bounds_vars[1], bounds[1], 7);
   }
   nir_push_else(b, NULL);
   { /* AABBs */
      nir_ssa_def *bounds[2];
      for (unsigned i = 0; i < 2; ++i)
         bounds[i] = nir_build_load_global(b, 3, 32, nir_iadd_imm(b, node_addr, i * 12));
      nir_store_var(b, bounds_vars[0], bounds[0], 7);
      nir_store_var(b, bounds_vars[1], bounds[1], 7);
   }
   nir_pop_if(b, NULL);
   nir_pop_if(b, NULL);
   nir_pop_if(b, NULL);
}

/* https://developer.nvidia.com/blog/thinking-parallel-part-iii-tree-construction-gpu/ */
static nir_ssa_def *
build_morton_component(nir_builder *b, nir_ssa_def *x)
{
   x = nir_iand_imm(b, nir_imul_imm(b, x, 0x00000101u), 0x0F00F00Fu);
   x = nir_iand_imm(b, nir_imul_imm(b, x, 0x00000011u), 0xC30C30C3u);
   x = nir_iand_imm(b, nir_imul_imm(b, x, 0x00000005u), 0x49249249u);
   return x;
}

static nir_shader *
build_morton_shader(struct radv_device *dev)
{
   const struct glsl_type *vec3_type = glsl_vector_type(GLSL_TYPE_FLOAT, 3);

   nir_builder b = create_accel_build_shader(dev, "accel_build_morton_shader");

   /*
    * push constants:
    *   i32 x 2: node address
    *   i32 x 2: scratch address
    */
   nir_ssa_def *pconst0 =
      nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .base = 0, .range = 16);

   nir_ssa_def *node_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst0, 0b0011));
   nir_ssa_def *scratch_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst0, 0b1100));

   nir_ssa_def *global_id =
      nir_iadd(&b,
               nir_imul_imm(&b, nir_channel(&b, nir_load_workgroup_id(&b, 32), 0),
                            b.shader->info.workgroup_size[0]),
               nir_load_local_invocation_index(&b));

   nir_ssa_def *node_id_addr =
      nir_iadd(&b, nir_iadd_imm(&b, scratch_addr, SCRATCH_TOTAL_BOUNDS_SIZE),
               nir_u2u64(&b, id_to_node_id_offset(&b, global_id, dev->physical_device)));
   nir_ssa_def *node_id =
      nir_build_load_global(&b, 1, 32, node_id_addr, .align_mul = 4, .align_offset = 0);

   nir_variable *node_bounds[2] = {
      nir_variable_create(b.shader, nir_var_shader_temp, vec3_type, "min_bound"),
      nir_variable_create(b.shader, nir_var_shader_temp, vec3_type, "max_bound"),
   };

   determine_bounds(&b, node_addr, node_id, node_bounds);

   nir_ssa_def *node_min = nir_load_var(&b, node_bounds[0]);
   nir_ssa_def *node_max = nir_load_var(&b, node_bounds[1]);
   nir_ssa_def *node_pos =
      nir_fmul(&b, nir_fadd(&b, node_min, node_max), nir_imm_vec3(&b, 0.5, 0.5, 0.5));

   nir_ssa_def *bvh_min = read_fminmax_atomic(dev, &b, 3, scratch_addr);
   nir_ssa_def *bvh_max = read_fminmax_atomic(dev, &b, 3, nir_iadd_imm(&b, scratch_addr, 12));
   nir_ssa_def *bvh_size = nir_fsub(&b, bvh_max, bvh_min);

   nir_ssa_def *normalized_node_pos = nir_fdiv(&b, nir_fsub(&b, node_pos, bvh_min), bvh_size);

   nir_ssa_def *x_int =
      nir_f2u32(&b, nir_fmul_imm(&b, nir_channel(&b, normalized_node_pos, 0), 255.0));
   nir_ssa_def *x_morton = build_morton_component(&b, x_int);

   nir_ssa_def *y_int =
      nir_f2u32(&b, nir_fmul_imm(&b, nir_channel(&b, normalized_node_pos, 1), 255.0));
   nir_ssa_def *y_morton = build_morton_component(&b, y_int);

   nir_ssa_def *z_int =
      nir_f2u32(&b, nir_fmul_imm(&b, nir_channel(&b, normalized_node_pos, 2), 255.0));
   nir_ssa_def *z_morton = build_morton_component(&b, z_int);

   nir_ssa_def *morton_code = nir_iadd(
      &b, nir_iadd(&b, nir_ishl_imm(&b, x_morton, 2), nir_ishl_imm(&b, y_morton, 1)), z_morton);
   nir_ssa_def *key = nir_ishl_imm(&b, morton_code, 8);

   nir_ssa_def *dst_addr =
      nir_iadd(&b, nir_iadd_imm(&b, scratch_addr, SCRATCH_TOTAL_BOUNDS_SIZE),
               nir_u2u64(&b, id_to_morton_offset(&b, global_id, dev->physical_device)));
   nir_build_store_global(&b, key, dst_addr, .align_mul = 4);

   return b.shader;
}

static nir_shader *
build_internal_shader(struct radv_device *dev)
{
   const struct glsl_type *vec3_type = glsl_vector_type(GLSL_TYPE_FLOAT, 3);
   nir_builder b = create_accel_build_shader(dev, "accel_build_internal_shader");

   /*
    * push constants:
    *   i32 x 2: node dst address
    *   i32 x 2: scratch address
    *   i32: dst offset
    *   i32: dst scratch offset
    *   i32: src scratch offset
    *   i32: src_node_count | (fill_header << 31)
    */
   nir_ssa_def *pconst0 =
      nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .base = 0, .range = 16);
   nir_ssa_def *pconst1 =
      nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .base = 16, .range = 16);

   nir_ssa_def *node_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst0, 0b0011));
   nir_ssa_def *scratch_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst0, 0b1100));
   nir_ssa_def *node_dst_offset = nir_channel(&b, pconst1, 0);
   nir_ssa_def *dst_scratch_offset = nir_channel(&b, pconst1, 1);
   nir_ssa_def *src_scratch_offset = nir_channel(&b, pconst1, 2);
   nir_ssa_def *src_node_count = nir_iand_imm(&b, nir_channel(&b, pconst1, 3), 0x7FFFFFFFU);
   nir_ssa_def *fill_header =
      nir_ine_imm(&b, nir_iand_imm(&b, nir_channel(&b, pconst1, 3), 0x80000000U), 0);

   nir_ssa_def *global_id =
      nir_iadd(&b,
               nir_imul_imm(&b, nir_channels(&b, nir_load_workgroup_id(&b, 32), 1),
                            b.shader->info.workgroup_size[0]),
               nir_channels(&b, nir_load_local_invocation_id(&b), 1));
   nir_ssa_def *src_idx = nir_imul_imm(&b, global_id, 4);
   nir_ssa_def *src_count = nir_umin(&b, nir_imm_int(&b, 4), nir_isub(&b, src_node_count, src_idx));

   nir_ssa_def *node_offset = nir_iadd(&b, node_dst_offset, nir_ishl_imm(&b, global_id, 7));
   nir_ssa_def *node_dst_addr = nir_iadd(&b, node_addr, nir_u2u64(&b, node_offset));

   nir_ssa_def *src_base_addr =
      nir_iadd(&b, scratch_addr,
               nir_u2u64(&b, nir_iadd(&b, src_scratch_offset,
                                      id_to_node_id_offset(&b, src_idx, dev->physical_device))));

   enum accel_struct_build build_mode =
      get_accel_struct_build(dev->physical_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR);
   uint32_t node_id_stride = get_node_id_stride(build_mode);

   nir_ssa_def *src_nodes[4];
   for (uint32_t i = 0; i < 4; i++) {
      src_nodes[i] =
         nir_build_load_global(&b, 1, 32, nir_iadd_imm(&b, src_base_addr, i * node_id_stride));
      nir_build_store_global(&b, src_nodes[i], nir_iadd_imm(&b, node_dst_addr, i * 4));
   }

   nir_ssa_def *total_bounds[2] = {
      nir_channels(&b, nir_imm_vec4(&b, NAN, NAN, NAN, NAN), 7),
      nir_channels(&b, nir_imm_vec4(&b, NAN, NAN, NAN, NAN), 7),
   };

   for (unsigned i = 0; i < 4; ++i) {
      nir_variable *bounds[2] = {
         nir_variable_create(b.shader, nir_var_shader_temp, vec3_type, "min_bound"),
         nir_variable_create(b.shader, nir_var_shader_temp, vec3_type, "max_bound"),
      };
      nir_store_var(&b, bounds[0], nir_channels(&b, nir_imm_vec4(&b, NAN, NAN, NAN, NAN), 7), 7);
      nir_store_var(&b, bounds[1], nir_channels(&b, nir_imm_vec4(&b, NAN, NAN, NAN, NAN), 7), 7);

      nir_push_if(&b, nir_ilt(&b, nir_imm_int(&b, i), src_count));
      determine_bounds(&b, node_addr, src_nodes[i], bounds);
      nir_pop_if(&b, NULL);
      nir_build_store_global(&b, nir_load_var(&b, bounds[0]),
                             nir_iadd_imm(&b, node_dst_addr, 16 + 24 * i));
      nir_build_store_global(&b, nir_load_var(&b, bounds[1]),
                             nir_iadd_imm(&b, node_dst_addr, 28 + 24 * i));
      total_bounds[0] = nir_fmin(&b, total_bounds[0], nir_load_var(&b, bounds[0]));
      total_bounds[1] = nir_fmax(&b, total_bounds[1], nir_load_var(&b, bounds[1]));
   }

   nir_ssa_def *node_id =
      nir_iadd_imm(&b, nir_ushr_imm(&b, node_offset, 3), radv_bvh_node_internal);
   nir_ssa_def *dst_scratch_addr =
      nir_iadd(&b, scratch_addr,
               nir_u2u64(&b, nir_iadd(&b, dst_scratch_offset,
                                      id_to_node_id_offset(&b, global_id, dev->physical_device))));
   nir_build_store_global(&b, node_id, dst_scratch_addr);

   nir_push_if(&b, fill_header);
   nir_build_store_global(&b, node_id, node_addr);
   nir_build_store_global(&b, total_bounds[0], nir_iadd_imm(&b, node_addr, 8));
   nir_build_store_global(&b, total_bounds[1], nir_iadd_imm(&b, node_addr, 20));
   nir_pop_if(&b, NULL);
   return b.shader;
}

enum copy_mode {
   COPY_MODE_COPY,
   COPY_MODE_SERIALIZE,
   COPY_MODE_DESERIALIZE,
};

struct copy_constants {
   uint64_t src_addr;
   uint64_t dst_addr;
   uint32_t mode;
};

static nir_shader *
build_copy_shader(struct radv_device *dev)
{
   nir_builder b = create_accel_build_shader(dev, "accel_copy");

   nir_ssa_def *invoc_id = nir_load_local_invocation_id(&b);
   nir_ssa_def *wg_id = nir_load_workgroup_id(&b, 32);
   nir_ssa_def *block_size =
      nir_imm_ivec4(&b, b.shader->info.workgroup_size[0], b.shader->info.workgroup_size[1],
                    b.shader->info.workgroup_size[2], 0);

   nir_ssa_def *global_id =
      nir_channel(&b, nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id), 0);

   nir_variable *offset_var =
      nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "offset");
   nir_ssa_def *offset = nir_imul_imm(&b, global_id, 16);
   nir_store_var(&b, offset_var, offset, 1);

   nir_ssa_def *increment = nir_imul_imm(&b, nir_channel(&b, nir_load_num_workgroups(&b, 32), 0),
                                         b.shader->info.workgroup_size[0] * 16);

   nir_ssa_def *pconst0 =
      nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .base = 0, .range = 16);
   nir_ssa_def *pconst1 =
      nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 0), .base = 16, .range = 4);
   nir_ssa_def *src_base_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst0, 0b0011));
   nir_ssa_def *dst_base_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst0, 0b1100));
   nir_ssa_def *mode = nir_channel(&b, pconst1, 0);

   nir_variable *compacted_size_var =
      nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint64_t_type(), "compacted_size");
   nir_variable *src_offset_var =
      nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "src_offset");
   nir_variable *dst_offset_var =
      nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "dst_offset");
   nir_variable *instance_offset_var =
      nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "instance_offset");
   nir_variable *instance_count_var =
      nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "instance_count");
   nir_variable *value_var =
      nir_variable_create(b.shader, nir_var_shader_temp, glsl_vec4_type(), "value");

   nir_push_if(&b, nir_ieq_imm(&b, mode, COPY_MODE_SERIALIZE));
   {
      nir_ssa_def *instance_count = nir_build_load_global(
         &b, 1, 32,
         nir_iadd_imm(&b, src_base_addr,
                      offsetof(struct radv_accel_struct_header, instance_count)));
      nir_ssa_def *compacted_size = nir_build_load_global(
         &b, 1, 64,
         nir_iadd_imm(&b, src_base_addr,
                      offsetof(struct radv_accel_struct_header, compacted_size)));
      nir_ssa_def *serialization_size = nir_build_load_global(
         &b, 1, 64,
         nir_iadd_imm(&b, src_base_addr,
                      offsetof(struct radv_accel_struct_header, serialization_size)));

      nir_store_var(&b, compacted_size_var, compacted_size, 1);
      nir_store_var(&b, instance_offset_var,
                    nir_build_load_global(
                       &b, 1, 32,
                       nir_iadd_imm(&b, src_base_addr,
                                    offsetof(struct radv_accel_struct_header, instance_offset))),
                    1);
      nir_store_var(&b, instance_count_var, instance_count, 1);

      nir_ssa_def *dst_offset = nir_iadd_imm(&b, nir_imul_imm(&b, instance_count, sizeof(uint64_t)),
                                             sizeof(struct radv_accel_struct_serialization_header));
      nir_store_var(&b, src_offset_var, nir_imm_int(&b, 0), 1);
      nir_store_var(&b, dst_offset_var, dst_offset, 1);

      nir_push_if(&b, nir_ieq_imm(&b, global_id, 0));
      {
         nir_build_store_global(&b, serialization_size,
                                nir_iadd_imm(&b, dst_base_addr,
                                             offsetof(struct radv_accel_struct_serialization_header,
                                                      serialization_size)));
         nir_build_store_global(
            &b, compacted_size,
            nir_iadd_imm(&b, dst_base_addr,
                         offsetof(struct radv_accel_struct_serialization_header, compacted_size)));
         nir_build_store_global(
            &b, nir_u2u64(&b, instance_count),
            nir_iadd_imm(&b, dst_base_addr,
                         offsetof(struct radv_accel_struct_serialization_header, instance_count)));
      }
      nir_pop_if(&b, NULL);
   }
   nir_push_else(&b, NULL);
   nir_push_if(&b, nir_ieq_imm(&b, mode, COPY_MODE_DESERIALIZE));
   {
      nir_ssa_def *instance_count = nir_build_load_global(
         &b, 1, 32,
         nir_iadd_imm(&b, src_base_addr,
                      offsetof(struct radv_accel_struct_serialization_header, instance_count)));
      nir_ssa_def *src_offset = nir_iadd_imm(&b, nir_imul_imm(&b, instance_count, sizeof(uint64_t)),
                                             sizeof(struct radv_accel_struct_serialization_header));

      nir_ssa_def *header_addr = nir_iadd(&b, src_base_addr, nir_u2u64(&b, src_offset));
      nir_store_var(&b, compacted_size_var,
                    nir_build_load_global(
                       &b, 1, 64,
                       nir_iadd_imm(&b, header_addr,
                                    offsetof(struct radv_accel_struct_header, compacted_size))),
                    1);
      nir_store_var(&b, instance_offset_var,
                    nir_build_load_global(
                       &b, 1, 32,
                       nir_iadd_imm(&b, header_addr,
                                    offsetof(struct radv_accel_struct_header, instance_offset))),
                    1);
      nir_store_var(&b, instance_count_var, instance_count, 1);
      nir_store_var(&b, src_offset_var, src_offset, 1);
      nir_store_var(&b, dst_offset_var, nir_imm_int(&b, 0), 1);
   }
   nir_push_else(&b, NULL); /* COPY_MODE_COPY */
   {
      nir_store_var(&b, compacted_size_var,
                    nir_build_load_global(
                       &b, 1, 64,
                       nir_iadd_imm(&b, src_base_addr,
                                    offsetof(struct radv_accel_struct_header, compacted_size))),
                    1);

      nir_store_var(&b, src_offset_var, nir_imm_int(&b, 0), 1);
      nir_store_var(&b, dst_offset_var, nir_imm_int(&b, 0), 1);
      nir_store_var(&b, instance_offset_var, nir_imm_int(&b, 0), 1);
      nir_store_var(&b, instance_count_var, nir_imm_int(&b, 0), 1);
   }
   nir_pop_if(&b, NULL);
   nir_pop_if(&b, NULL);

   nir_ssa_def *instance_bound =
      nir_imul_imm(&b, nir_load_var(&b, instance_count_var), sizeof(struct radv_bvh_instance_node));
   nir_ssa_def *compacted_size = nir_build_load_global(
      &b, 1, 32,
      nir_iadd_imm(&b, src_base_addr, offsetof(struct radv_accel_struct_header, compacted_size)));

   nir_push_loop(&b);
   {
      offset = nir_load_var(&b, offset_var);
      nir_push_if(&b, nir_ilt(&b, offset, compacted_size));
      {
         nir_ssa_def *src_offset = nir_iadd(&b, offset, nir_load_var(&b, src_offset_var));
         nir_ssa_def *dst_offset = nir_iadd(&b, offset, nir_load_var(&b, dst_offset_var));
         nir_ssa_def *src_addr = nir_iadd(&b, src_base_addr, nir_u2u64(&b, src_offset));
         nir_ssa_def *dst_addr = nir_iadd(&b, dst_base_addr, nir_u2u64(&b, dst_offset));

         nir_ssa_def *value = nir_build_load_global(&b, 4, 32, src_addr, .align_mul = 16);
         nir_store_var(&b, value_var, value, 0xf);

         nir_ssa_def *instance_offset = nir_isub(&b, offset, nir_load_var(&b, instance_offset_var));
         nir_ssa_def *in_instance_bound =
            nir_iand(&b, nir_uge(&b, offset, nir_load_var(&b, instance_offset_var)),
                     nir_ult(&b, instance_offset, instance_bound));
         nir_ssa_def *instance_start = nir_ieq_imm(
            &b, nir_iand_imm(&b, instance_offset, sizeof(struct radv_bvh_instance_node) - 1), 0);

         nir_push_if(&b, nir_iand(&b, in_instance_bound, instance_start));
         {
            nir_ssa_def *instance_id = nir_ushr_imm(&b, instance_offset, 7);

            nir_push_if(&b, nir_ieq_imm(&b, mode, COPY_MODE_SERIALIZE));
            {
               nir_ssa_def *instance_addr = nir_imul_imm(&b, instance_id, sizeof(uint64_t));
               instance_addr = nir_iadd_imm(&b, instance_addr,
                                            sizeof(struct radv_accel_struct_serialization_header));
               instance_addr = nir_iadd(&b, dst_base_addr, nir_u2u64(&b, instance_addr));

               nir_build_store_global(&b, nir_channels(&b, value, 3), instance_addr,
                                      .align_mul = 8);
            }
            nir_push_else(&b, NULL);
            {
               nir_ssa_def *instance_addr = nir_imul_imm(&b, instance_id, sizeof(uint64_t));
               instance_addr = nir_iadd_imm(&b, instance_addr,
                                            sizeof(struct radv_accel_struct_serialization_header));
               instance_addr = nir_iadd(&b, src_base_addr, nir_u2u64(&b, instance_addr));

               nir_ssa_def *instance_value =
                  nir_build_load_global(&b, 2, 32, instance_addr, .align_mul = 8);

               nir_ssa_def *values[] = {
                  nir_channel(&b, instance_value, 0),
                  nir_channel(&b, instance_value, 1),
                  nir_channel(&b, value, 2),
                  nir_channel(&b, value, 3),
               };

               nir_store_var(&b, value_var, nir_vec(&b, values, 4), 0xf);
            }
            nir_pop_if(&b, NULL);
         }
         nir_pop_if(&b, NULL);

         nir_store_var(&b, offset_var, nir_iadd(&b, offset, increment), 1);

         nir_build_store_global(&b, nir_load_var(&b, value_var), dst_addr, .align_mul = 16);
      }
      nir_push_else(&b, NULL);
      {
         nir_jump(&b, nir_jump_break);
      }
      nir_pop_if(&b, NULL);
   }
   nir_pop_loop(&b, NULL);
   return b.shader;
}

void
radv_device_finish_accel_struct_build_state(struct radv_device *device)
{
   struct radv_meta_state *state = &device->meta_state;
   radv_DestroyPipeline(radv_device_to_handle(device), state->accel_struct_build.copy_pipeline,
                        &state->alloc);
   radv_DestroyPipeline(radv_device_to_handle(device), state->accel_struct_build.internal_pipeline,
                        &state->alloc);
   radv_DestroyPipeline(radv_device_to_handle(device), state->accel_struct_build.leaf_pipeline,
                        &state->alloc);
   radv_DestroyPipeline(radv_device_to_handle(device), state->accel_struct_build.morton_pipeline,
                        &state->alloc);
   radv_DestroyPipelineLayout(radv_device_to_handle(device),
                              state->accel_struct_build.copy_p_layout, &state->alloc);
   radv_DestroyPipelineLayout(radv_device_to_handle(device),
                              state->accel_struct_build.internal_p_layout, &state->alloc);
   radv_DestroyPipelineLayout(radv_device_to_handle(device),
                              state->accel_struct_build.leaf_p_layout, &state->alloc);
   radv_DestroyPipelineLayout(radv_device_to_handle(device),
                              state->accel_struct_build.morton_p_layout, &state->alloc);

   if (state->accel_struct_build.radix_sort)
      radix_sort_vk_destroy(state->accel_struct_build.radix_sort, radv_device_to_handle(device),
                            &state->alloc);
}

static VkResult
create_build_pipeline(struct radv_device *device, nir_shader *shader, unsigned push_constant_size,
                      VkPipeline *pipeline, VkPipelineLayout *layout)
{
   const VkPipelineLayoutCreateInfo pl_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 0,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges =
         &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, push_constant_size},
   };

   VkResult result = radv_CreatePipelineLayout(radv_device_to_handle(device), &pl_create_info,
                                               &device->meta_state.alloc, layout);
   if (result != VK_SUCCESS) {
      ralloc_free(shader);
      return result;
   }

   VkPipelineShaderStageCreateInfo shader_stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = vk_shader_module_handle_from_nir(shader),
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   VkComputePipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = shader_stage,
      .flags = 0,
      .layout = *layout,
   };

   result = radv_CreateComputePipelines(radv_device_to_handle(device),
                                        radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
                                        &pipeline_info, &device->meta_state.alloc, pipeline);

   if (result != VK_SUCCESS) {
      ralloc_free(shader);
      return result;
   }

   return VK_SUCCESS;
}

static void
radix_sort_fill_buffer(VkCommandBuffer commandBuffer,
                       radix_sort_vk_buffer_info_t const *buffer_info, VkDeviceSize offset,
                       VkDeviceSize size, uint32_t data)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

   assert(size != VK_WHOLE_SIZE);

   radv_fill_buffer(cmd_buffer, NULL, NULL, buffer_info->devaddr + buffer_info->offset + offset,
                    size, data);
}

VkResult
radv_device_init_accel_struct_build_state(struct radv_device *device)
{
   VkResult result;
   nir_shader *leaf_cs = build_leaf_shader(device);
   nir_shader *internal_cs = build_internal_shader(device);
   nir_shader *copy_cs = build_copy_shader(device);

   result = create_build_pipeline(device, leaf_cs, sizeof(struct build_primitive_constants),
                                  &device->meta_state.accel_struct_build.leaf_pipeline,
                                  &device->meta_state.accel_struct_build.leaf_p_layout);
   if (result != VK_SUCCESS)
      return result;

   result = create_build_pipeline(device, internal_cs, sizeof(struct build_internal_constants),
                                  &device->meta_state.accel_struct_build.internal_pipeline,
                                  &device->meta_state.accel_struct_build.internal_p_layout);
   if (result != VK_SUCCESS)
      return result;

   result = create_build_pipeline(device, copy_cs, sizeof(struct copy_constants),
                                  &device->meta_state.accel_struct_build.copy_pipeline,
                                  &device->meta_state.accel_struct_build.copy_p_layout);

   if (result != VK_SUCCESS)
      return result;

   if (get_accel_struct_build(device->physical_device,
                              VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR) ==
       accel_struct_build_lbvh) {
      nir_shader *morton_cs = build_morton_shader(device);

      result = create_build_pipeline(device, morton_cs, sizeof(struct morton_constants),
                                     &device->meta_state.accel_struct_build.morton_pipeline,
                                     &device->meta_state.accel_struct_build.morton_p_layout);
      if (result != VK_SUCCESS)
         return result;

      device->meta_state.accel_struct_build.radix_sort =
         radv_create_radix_sort_u64(radv_device_to_handle(device), &device->meta_state.alloc,
                                    radv_pipeline_cache_to_handle(&device->meta_state.cache));

      struct radix_sort_vk_sort_devaddr_info *radix_sort_info =
         &device->meta_state.accel_struct_build.radix_sort_info;
      radix_sort_info->ext = NULL;
      radix_sort_info->key_bits = 24;
      radix_sort_info->fill_buffer = radix_sort_fill_buffer;
   }

   return result;
}

struct bvh_state {
   uint32_t node_offset;
   uint32_t node_count;
   uint32_t scratch_offset;
   uint32_t buffer_1_offset;
   uint32_t buffer_2_offset;

   uint32_t instance_offset;
   uint32_t instance_count;
};

VKAPI_ATTR void VKAPI_CALL
radv_CmdBuildAccelerationStructuresKHR(
   VkCommandBuffer commandBuffer, uint32_t infoCount,
   const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
   const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_meta_saved_state saved_state;

   enum radv_cmd_flush_bits flush_bits =
      RADV_CMD_FLAG_CS_PARTIAL_FLUSH |
      radv_src_access_flush(cmd_buffer, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                            NULL) |
      radv_dst_access_flush(cmd_buffer, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                            NULL);

   enum accel_struct_build build_mode = get_accel_struct_build(
      cmd_buffer->device->physical_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR);
   uint32_t node_id_stride = get_node_id_stride(build_mode);

   radv_meta_save(
      &saved_state, cmd_buffer,
      RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_CONSTANTS);
   struct bvh_state *bvh_states = calloc(infoCount, sizeof(struct bvh_state));

   if (build_mode != accel_struct_build_unoptimized) {
      for (uint32_t i = 0; i < infoCount; ++i) {
         if (radv_has_shader_buffer_float_minmax(cmd_buffer->device->physical_device)) {
            /* Clear the bvh bounds with nan. */
            si_cp_dma_clear_buffer(cmd_buffer, pInfos[i].scratchData.deviceAddress,
                                   6 * sizeof(float), 0x7FC00000);
         } else {
            /* Clear the bvh bounds with int max/min. */
            si_cp_dma_clear_buffer(cmd_buffer, pInfos[i].scratchData.deviceAddress,
                                   3 * sizeof(float), 0x7fffffff);
            si_cp_dma_clear_buffer(cmd_buffer,
                                   pInfos[i].scratchData.deviceAddress + 3 * sizeof(float),
                                   3 * sizeof(float), 0x80000000);
         }
      }

      cmd_buffer->state.flush_bits |= flush_bits;
   }

   radv_CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                        cmd_buffer->device->meta_state.accel_struct_build.leaf_pipeline);

   for (uint32_t i = 0; i < infoCount; ++i) {
      RADV_FROM_HANDLE(radv_acceleration_structure, accel_struct,
                       pInfos[i].dstAccelerationStructure);

      struct build_primitive_constants prim_consts = {
         .node_dst_addr = radv_accel_struct_get_va(accel_struct),
         .scratch_addr = pInfos[i].scratchData.deviceAddress,
         .dst_offset = ALIGN(sizeof(struct radv_accel_struct_header), 64) + 128,
         .dst_scratch_offset = 0,
      };
      bvh_states[i].node_offset = prim_consts.dst_offset;
      bvh_states[i].instance_offset = prim_consts.dst_offset;

      for (int inst = 1; inst >= 0; --inst) {
         for (unsigned j = 0; j < pInfos[i].geometryCount; ++j) {
            const VkAccelerationStructureGeometryKHR *geom =
               pInfos[i].pGeometries ? &pInfos[i].pGeometries[j] : pInfos[i].ppGeometries[j];

            if (!inst == (geom->geometryType == VK_GEOMETRY_TYPE_INSTANCES_KHR))
               continue;

            const VkAccelerationStructureBuildRangeInfoKHR *buildRangeInfo =
               &ppBuildRangeInfos[i][j];

            prim_consts.geometry_type = geom->geometryType;
            prim_consts.geometry_id = j | (geom->flags << 28);
            unsigned prim_size;
            switch (geom->geometryType) {
            case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
               prim_consts.vertex_addr =
                  geom->geometry.triangles.vertexData.deviceAddress +
                  buildRangeInfo->firstVertex * geom->geometry.triangles.vertexStride;
               prim_consts.index_addr = geom->geometry.triangles.indexData.deviceAddress;

               if (geom->geometry.triangles.indexType == VK_INDEX_TYPE_NONE_KHR)
                  prim_consts.vertex_addr += buildRangeInfo->primitiveOffset;
               else
                  prim_consts.index_addr += buildRangeInfo->primitiveOffset;

               prim_consts.transform_addr = geom->geometry.triangles.transformData.deviceAddress;
               if (prim_consts.transform_addr)
                  prim_consts.transform_addr += buildRangeInfo->transformOffset;

               prim_consts.vertex_stride = geom->geometry.triangles.vertexStride;
               prim_consts.vertex_format = geom->geometry.triangles.vertexFormat;
               prim_consts.index_format = geom->geometry.triangles.indexType;
               prim_size = 64;
               break;
            case VK_GEOMETRY_TYPE_AABBS_KHR:
               prim_consts.aabb_addr =
                  geom->geometry.aabbs.data.deviceAddress + buildRangeInfo->primitiveOffset;
               prim_consts.aabb_stride = geom->geometry.aabbs.stride;
               prim_size = 64;
               break;
            case VK_GEOMETRY_TYPE_INSTANCES_KHR:
               prim_consts.instance_data =
                  geom->geometry.instances.data.deviceAddress + buildRangeInfo->primitiveOffset;
               prim_consts.array_of_pointers = geom->geometry.instances.arrayOfPointers ? 1 : 0;
               prim_size = 128;
               bvh_states[i].instance_count += buildRangeInfo->primitiveCount;
               break;
            default:
               unreachable("Unknown geometryType");
            }

            radv_CmdPushConstants(
               commandBuffer, cmd_buffer->device->meta_state.accel_struct_build.leaf_p_layout,
               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(prim_consts), &prim_consts);
            radv_unaligned_dispatch(cmd_buffer, buildRangeInfo->primitiveCount, 1, 1);
            prim_consts.dst_offset += prim_size * buildRangeInfo->primitiveCount;
            prim_consts.dst_scratch_offset += node_id_stride * buildRangeInfo->primitiveCount;
         }
      }
      bvh_states[i].node_offset = prim_consts.dst_offset;
      bvh_states[i].node_count = prim_consts.dst_scratch_offset / node_id_stride;
   }

   if (build_mode == accel_struct_build_lbvh) {
      cmd_buffer->state.flush_bits |= flush_bits;

      radv_CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                           cmd_buffer->device->meta_state.accel_struct_build.morton_pipeline);

      for (uint32_t i = 0; i < infoCount; ++i) {
         RADV_FROM_HANDLE(radv_acceleration_structure, accel_struct,
                          pInfos[i].dstAccelerationStructure);

         const struct morton_constants consts = {
            .node_addr = radv_accel_struct_get_va(accel_struct),
            .scratch_addr = pInfos[i].scratchData.deviceAddress,
         };

         radv_CmdPushConstants(commandBuffer,
                               cmd_buffer->device->meta_state.accel_struct_build.morton_p_layout,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(consts), &consts);
         radv_unaligned_dispatch(cmd_buffer, bvh_states[i].node_count, 1, 1);
      }

      cmd_buffer->state.flush_bits |= flush_bits;

      for (uint32_t i = 0; i < infoCount; ++i) {
         struct radix_sort_vk_memory_requirements requirements;
         radix_sort_vk_get_memory_requirements(
            cmd_buffer->device->meta_state.accel_struct_build.radix_sort, bvh_states[i].node_count,
            &requirements);

         struct radix_sort_vk_sort_devaddr_info info =
            cmd_buffer->device->meta_state.accel_struct_build.radix_sort_info;
         info.count = bvh_states[i].node_count;

         VkDeviceAddress base_addr =
            pInfos[i].scratchData.deviceAddress + SCRATCH_TOTAL_BOUNDS_SIZE;

         info.keyvals_even.buffer = VK_NULL_HANDLE;
         info.keyvals_even.offset = 0;
         info.keyvals_even.devaddr = base_addr;

         info.keyvals_odd = base_addr + requirements.keyvals_size;

         info.internal.buffer = VK_NULL_HANDLE;
         info.internal.offset = 0;
         info.internal.devaddr = base_addr + requirements.keyvals_size * 2;

         VkDeviceAddress result_addr;
         radix_sort_vk_sort_devaddr(cmd_buffer->device->meta_state.accel_struct_build.radix_sort,
                                    &info, radv_device_to_handle(cmd_buffer->device), commandBuffer,
                                    &result_addr);

         assert(result_addr == info.keyvals_even.devaddr || result_addr == info.keyvals_odd);

         if (result_addr == info.keyvals_even.devaddr) {
            bvh_states[i].buffer_1_offset = SCRATCH_TOTAL_BOUNDS_SIZE;
            bvh_states[i].buffer_2_offset = SCRATCH_TOTAL_BOUNDS_SIZE + requirements.keyvals_size;
         } else {
            bvh_states[i].buffer_1_offset = SCRATCH_TOTAL_BOUNDS_SIZE + requirements.keyvals_size;
            bvh_states[i].buffer_2_offset = SCRATCH_TOTAL_BOUNDS_SIZE;
         }
         bvh_states[i].scratch_offset = bvh_states[i].buffer_1_offset;
      }

      cmd_buffer->state.flush_bits |= flush_bits;
   } else {
      for (uint32_t i = 0; i < infoCount; ++i) {
         bvh_states[i].buffer_1_offset = 0;
         bvh_states[i].buffer_2_offset = bvh_states[i].node_count * 4;
      }
   }

   radv_CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                        cmd_buffer->device->meta_state.accel_struct_build.internal_pipeline);
   bool progress = true;
   for (unsigned iter = 0; progress; ++iter) {
      progress = false;
      for (uint32_t i = 0; i < infoCount; ++i) {
         RADV_FROM_HANDLE(radv_acceleration_structure, accel_struct,
                          pInfos[i].dstAccelerationStructure);

         if (iter && bvh_states[i].node_count == 1)
            continue;

         if (!progress)
            cmd_buffer->state.flush_bits |= flush_bits;

         progress = true;

         uint32_t dst_node_count = MAX2(1, DIV_ROUND_UP(bvh_states[i].node_count, 4));
         bool final_iter = dst_node_count == 1;

         uint32_t src_scratch_offset = bvh_states[i].scratch_offset;
         uint32_t buffer_1_offset = bvh_states[i].buffer_1_offset;
         uint32_t buffer_2_offset = bvh_states[i].buffer_2_offset;
         uint32_t dst_scratch_offset =
            (src_scratch_offset == buffer_1_offset) ? buffer_2_offset : buffer_1_offset;

         uint32_t dst_node_offset = bvh_states[i].node_offset;
         if (final_iter)
            dst_node_offset = ALIGN(sizeof(struct radv_accel_struct_header), 64);

         const struct build_internal_constants consts = {
            .node_dst_addr = radv_accel_struct_get_va(accel_struct),
            .scratch_addr = pInfos[i].scratchData.deviceAddress,
            .dst_offset = dst_node_offset,
            .dst_scratch_offset = dst_scratch_offset,
            .src_scratch_offset = src_scratch_offset,
            .fill_header = bvh_states[i].node_count | (final_iter ? 0x80000000U : 0),
         };

         radv_CmdPushConstants(commandBuffer,
                               cmd_buffer->device->meta_state.accel_struct_build.internal_p_layout,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(consts), &consts);
         radv_unaligned_dispatch(cmd_buffer, dst_node_count, 1, 1);
         if (!final_iter)
            bvh_states[i].node_offset += dst_node_count * 128;
         bvh_states[i].node_count = dst_node_count;
         bvh_states[i].scratch_offset = dst_scratch_offset;
      }
   }
   for (uint32_t i = 0; i < infoCount; ++i) {
      RADV_FROM_HANDLE(radv_acceleration_structure, accel_struct,
                       pInfos[i].dstAccelerationStructure);
      const size_t base = offsetof(struct radv_accel_struct_header, compacted_size);
      struct radv_accel_struct_header header;

      header.instance_offset = bvh_states[i].instance_offset;
      header.instance_count = bvh_states[i].instance_count;
      header.compacted_size = bvh_states[i].node_offset;

      fill_accel_struct_header(&header);

      radv_update_buffer_cp(cmd_buffer,
                            radv_buffer_get_va(accel_struct->bo) + accel_struct->mem_offset + base,
                            (const char *)&header + base, sizeof(header) - base);
   }
   free(bvh_states);
   radv_meta_restore(&saved_state, cmd_buffer);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdCopyAccelerationStructureKHR(VkCommandBuffer commandBuffer,
                                     const VkCopyAccelerationStructureInfoKHR *pInfo)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   RADV_FROM_HANDLE(radv_acceleration_structure, src, pInfo->src);
   RADV_FROM_HANDLE(radv_acceleration_structure, dst, pInfo->dst);
   struct radv_meta_saved_state saved_state;

   radv_meta_save(
      &saved_state, cmd_buffer,
      RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_CONSTANTS);

   uint64_t src_addr = radv_accel_struct_get_va(src);
   uint64_t dst_addr = radv_accel_struct_get_va(dst);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        cmd_buffer->device->meta_state.accel_struct_build.copy_pipeline);

   const struct copy_constants consts = {
      .src_addr = src_addr,
      .dst_addr = dst_addr,
      .mode = COPY_MODE_COPY,
   };

   radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                         cmd_buffer->device->meta_state.accel_struct_build.copy_p_layout,
                         VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(consts), &consts);

   cmd_buffer->state.flush_bits |=
      radv_dst_access_flush(cmd_buffer, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, NULL);

   radv_indirect_dispatch(cmd_buffer, src->bo,
                          src_addr + offsetof(struct radv_accel_struct_header, copy_dispatch_size));
   radv_meta_restore(&saved_state, cmd_buffer);
}

VKAPI_ATTR void VKAPI_CALL
radv_GetDeviceAccelerationStructureCompatibilityKHR(
   VkDevice _device, const VkAccelerationStructureVersionInfoKHR *pVersionInfo,
   VkAccelerationStructureCompatibilityKHR *pCompatibility)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   uint8_t zero[VK_UUID_SIZE] = {
      0,
   };
   bool compat =
      memcmp(pVersionInfo->pVersionData, device->physical_device->driver_uuid, VK_UUID_SIZE) == 0 &&
      memcmp(pVersionInfo->pVersionData + VK_UUID_SIZE, zero, VK_UUID_SIZE) == 0;
   *pCompatibility = compat ? VK_ACCELERATION_STRUCTURE_COMPATIBILITY_COMPATIBLE_KHR
                            : VK_ACCELERATION_STRUCTURE_COMPATIBILITY_INCOMPATIBLE_KHR;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CopyMemoryToAccelerationStructureKHR(VkDevice _device,
                                          VkDeferredOperationKHR deferredOperation,
                                          const VkCopyMemoryToAccelerationStructureInfoKHR *pInfo)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_acceleration_structure, accel_struct, pInfo->dst);

   char *base = device->ws->buffer_map(accel_struct->bo);
   if (!base)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   base += accel_struct->mem_offset;
   const struct radv_accel_struct_header *header = (const struct radv_accel_struct_header *)base;

   const char *src = pInfo->src.hostAddress;
   struct radv_accel_struct_serialization_header *src_header = (void *)src;
   src += sizeof(*src_header) + sizeof(uint64_t) * src_header->instance_count;

   memcpy(base, src, src_header->compacted_size);

   for (unsigned i = 0; i < src_header->instance_count; ++i) {
      uint64_t *p = (uint64_t *)(base + i * 128 + header->instance_offset);
      *p = (*p & 63) | src_header->instances[i];
   }

   device->ws->buffer_unmap(accel_struct->bo);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CopyAccelerationStructureToMemoryKHR(VkDevice _device,
                                          VkDeferredOperationKHR deferredOperation,
                                          const VkCopyAccelerationStructureToMemoryInfoKHR *pInfo)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_acceleration_structure, accel_struct, pInfo->src);

   const char *base = device->ws->buffer_map(accel_struct->bo);
   if (!base)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   base += accel_struct->mem_offset;
   const struct radv_accel_struct_header *header = (const struct radv_accel_struct_header *)base;

   char *dst = pInfo->dst.hostAddress;
   struct radv_accel_struct_serialization_header *dst_header = (void *)dst;
   dst += sizeof(*dst_header) + sizeof(uint64_t) * header->instance_count;

   memcpy(dst_header->driver_uuid, device->physical_device->driver_uuid, VK_UUID_SIZE);
   memset(dst_header->accel_struct_compat, 0, VK_UUID_SIZE);

   dst_header->serialization_size = header->serialization_size;
   dst_header->compacted_size = header->compacted_size;
   dst_header->instance_count = header->instance_count;

   memcpy(dst, base, header->compacted_size);

   for (unsigned i = 0; i < header->instance_count; ++i) {
      dst_header->instances[i] =
         *(const uint64_t *)(base + i * 128 + header->instance_offset) & ~63ull;
   }

   device->ws->buffer_unmap(accel_struct->bo);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdCopyMemoryToAccelerationStructureKHR(
   VkCommandBuffer commandBuffer, const VkCopyMemoryToAccelerationStructureInfoKHR *pInfo)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   RADV_FROM_HANDLE(radv_acceleration_structure, dst, pInfo->dst);
   struct radv_meta_saved_state saved_state;

   radv_meta_save(
      &saved_state, cmd_buffer,
      RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_CONSTANTS);

   uint64_t dst_addr = radv_accel_struct_get_va(dst);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        cmd_buffer->device->meta_state.accel_struct_build.copy_pipeline);

   const struct copy_constants consts = {
      .src_addr = pInfo->src.deviceAddress,
      .dst_addr = dst_addr,
      .mode = COPY_MODE_DESERIALIZE,
   };

   radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                         cmd_buffer->device->meta_state.accel_struct_build.copy_p_layout,
                         VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(consts), &consts);

   radv_CmdDispatch(commandBuffer, 512, 1, 1);
   radv_meta_restore(&saved_state, cmd_buffer);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdCopyAccelerationStructureToMemoryKHR(
   VkCommandBuffer commandBuffer, const VkCopyAccelerationStructureToMemoryInfoKHR *pInfo)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   RADV_FROM_HANDLE(radv_acceleration_structure, src, pInfo->src);
   struct radv_meta_saved_state saved_state;

   radv_meta_save(
      &saved_state, cmd_buffer,
      RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_CONSTANTS);

   uint64_t src_addr = radv_accel_struct_get_va(src);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        cmd_buffer->device->meta_state.accel_struct_build.copy_pipeline);

   const struct copy_constants consts = {
      .src_addr = src_addr,
      .dst_addr = pInfo->dst.deviceAddress,
      .mode = COPY_MODE_SERIALIZE,
   };

   radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                         cmd_buffer->device->meta_state.accel_struct_build.copy_p_layout,
                         VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(consts), &consts);

   cmd_buffer->state.flush_bits |=
      radv_dst_access_flush(cmd_buffer, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, NULL);

   radv_indirect_dispatch(cmd_buffer, src->bo,
                          src_addr + offsetof(struct radv_accel_struct_header, copy_dispatch_size));
   radv_meta_restore(&saved_state, cmd_buffer);

   /* Set the header of the serialized data. */
   uint8_t header_data[2 * VK_UUID_SIZE] = {0};
   memcpy(header_data, cmd_buffer->device->physical_device->driver_uuid, VK_UUID_SIZE);

   radv_update_buffer_cp(cmd_buffer, pInfo->dst.deviceAddress, header_data, sizeof(header_data));
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdBuildAccelerationStructuresIndirectKHR(
   VkCommandBuffer commandBuffer, uint32_t infoCount,
   const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
   const VkDeviceAddress *pIndirectDeviceAddresses, const uint32_t *pIndirectStrides,
   const uint32_t *const *ppMaxPrimitiveCounts)
{
   unreachable("Unimplemented");
}
