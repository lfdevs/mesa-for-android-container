/*
 * Copyright © 2022 Konstantin Seurer
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

#ifndef VK_BVH_BUILD_HELPERS_H
#define VK_BVH_BUILD_HELPERS_H

#include "vk_bvh_defines.h"

#ifdef VULKAN
#ifndef VK_USED_BUILD_FLAGS
#error "VK_USED_BUILD_FLAGS needs to be set to use helpers"
#endif
#endif

#ifdef VULKAN
layout (constant_id = SUBGROUP_SIZE_ID) const int SUBGROUP_SIZE = 64;
layout (constant_id = BVH_BOUNDS_OFFSET_ID) const int BVH_BOUNDS_OFFSET = 0;
layout (constant_id = BUILD_FLAGS_ID) const int BUILD_FLAGS = 0;
layout (constant_id = ROOT_FLAGS_OFFSET_ID) const int ROOT_FLAGS_OFFSET = -1;
#endif

/* copied from u_math.h */
uint32_t
align(uint32_t value, uint32_t alignment)
{
   return (value + alignment - 1) & ~(alignment - 1);
}

int32_t
to_emulated_float(float f)
{
   int32_t bits = floatBitsToInt(f);
   return f < 0 ? -2147483648 - bits : bits;
}

float
from_emulated_float(int32_t bits)
{
   return intBitsToFloat(bits < 0 ? -2147483648 - bits : bits);
}

uint32_t
ir_id_to_offset(uint32_t id)
{
   return id & (~3u);
}

uint32_t
ir_id_to_type(uint32_t id)
{
   return id & 3u;
}

uint32_t
pack_ir_node_id(uint32_t offset, uint32_t type)
{
   return offset | type;
}

float
aabb_surface_area(vk_aabb aabb)
{
   vec3 diagonal = aabb.max - aabb.min;
   return 2 * diagonal.x * diagonal.y + 2 * diagonal.y * diagonal.z + 2 * diagonal.x * diagonal.z;
}

/* Just a wrapper for 3 uints. */
struct triangle_indices {
   uint32_t index[3];
};

triangle_indices
load_indices(VOID_REF indices, uint32_t index_format, uint32_t global_id)
{
   triangle_indices result;

   uint32_t index_base = global_id * 3;

   switch (index_format) {
   case VK_INDEX_TYPE_UINT16: {
      result.index[0] = DEREF(INDEX(uint16_t, indices, index_base + 0));
      result.index[1] = DEREF(INDEX(uint16_t, indices, index_base + 1));
      result.index[2] = DEREF(INDEX(uint16_t, indices, index_base + 2));
      break;
   }
   case VK_INDEX_TYPE_UINT32: {
      result.index[0] = DEREF(INDEX(uint32_t, indices, index_base + 0));
      result.index[1] = DEREF(INDEX(uint32_t, indices, index_base + 1));
      result.index[2] = DEREF(INDEX(uint32_t, indices, index_base + 2));
      break;
   }
   case VK_INDEX_TYPE_NONE_KHR: {
      result.index[0] = index_base + 0;
      result.index[1] = index_base + 1;
      result.index[2] = index_base + 2;
      break;
   }
   case VK_INDEX_TYPE_UINT8_EXT: {
      result.index[0] = DEREF(INDEX(uint8_t, indices, index_base + 0));
      result.index[1] = DEREF(INDEX(uint8_t, indices, index_base + 1));
      result.index[2] = DEREF(INDEX(uint8_t, indices, index_base + 2));
      break;
   }
   }

   return result;
}

/* Just a wrapper for 3 vec4s. */
struct triangle_vertices {
   vec4 vertex[3];
};

TYPE(float16_t, 2);

triangle_vertices
load_vertices(VOID_REF vertices, triangle_indices indices, uint32_t vertex_format, uint32_t stride)
{
   triangle_vertices result;

   for (uint32_t i = 0; i < 3; i++) {
      VOID_REF vertex_ptr = OFFSET(vertices, indices.index[i] * stride);
      vec4 vertex = vec4(0.0, 0.0, 0.0, 1.0);

      switch (vertex_format) {
      case VK_FORMAT_R32G32_SFLOAT:
         vertex.x = DEREF(INDEX(float, vertex_ptr, 0));
         vertex.y = DEREF(INDEX(float, vertex_ptr, 1));
         break;
      case VK_FORMAT_R32G32B32_SFLOAT:
      case VK_FORMAT_R32G32B32A32_SFLOAT:
         vertex.x = DEREF(INDEX(float, vertex_ptr, 0));
         vertex.y = DEREF(INDEX(float, vertex_ptr, 1));
         vertex.z = DEREF(INDEX(float, vertex_ptr, 2));
         break;
      case VK_FORMAT_R16G16_SFLOAT:
         vertex.x = DEREF(INDEX(float16_t, vertex_ptr, 0));
         vertex.y = DEREF(INDEX(float16_t, vertex_ptr, 1));
         break;
      case VK_FORMAT_R16G16B16_SFLOAT:
      case VK_FORMAT_R16G16B16A16_SFLOAT:
         vertex.x = DEREF(INDEX(float16_t, vertex_ptr, 0));
         vertex.y = DEREF(INDEX(float16_t, vertex_ptr, 1));
         vertex.z = DEREF(INDEX(float16_t, vertex_ptr, 2));
         break;
      case VK_FORMAT_R16G16_SNORM:
         vertex.x = max(-1.0, DEREF(INDEX(int16_t, vertex_ptr, 0)) / float(0x7FFF));
         vertex.y = max(-1.0, DEREF(INDEX(int16_t, vertex_ptr, 1)) / float(0x7FFF));
         break;
      case VK_FORMAT_R16G16B16A16_SNORM:
         vertex.x = max(-1.0, DEREF(INDEX(int16_t, vertex_ptr, 0)) / float(0x7FFF));
         vertex.y = max(-1.0, DEREF(INDEX(int16_t, vertex_ptr, 1)) / float(0x7FFF));
         vertex.z = max(-1.0, DEREF(INDEX(int16_t, vertex_ptr, 2)) / float(0x7FFF));
         break;
      case VK_FORMAT_R8G8_SNORM:
         vertex.x = max(-1.0, DEREF(INDEX(int8_t, vertex_ptr, 0)) / float(0x7F));
         vertex.y = max(-1.0, DEREF(INDEX(int8_t, vertex_ptr, 1)) / float(0x7F));
         break;
      case VK_FORMAT_R8G8B8A8_SNORM:
         vertex.x = max(-1.0, DEREF(INDEX(int8_t, vertex_ptr, 0)) / float(0x7F));
         vertex.y = max(-1.0, DEREF(INDEX(int8_t, vertex_ptr, 1)) / float(0x7F));
         vertex.z = max(-1.0, DEREF(INDEX(int8_t, vertex_ptr, 2)) / float(0x7F));
         break;
      case VK_FORMAT_R16G16_UNORM:
         vertex.x = DEREF(INDEX(uint16_t, vertex_ptr, 0)) / float(0xFFFF);
         vertex.y = DEREF(INDEX(uint16_t, vertex_ptr, 1)) / float(0xFFFF);
         break;
      case VK_FORMAT_R16G16B16A16_UNORM:
         vertex.x = DEREF(INDEX(uint16_t, vertex_ptr, 0)) / float(0xFFFF);
         vertex.y = DEREF(INDEX(uint16_t, vertex_ptr, 1)) / float(0xFFFF);
         vertex.z = DEREF(INDEX(uint16_t, vertex_ptr, 2)) / float(0xFFFF);
         break;
      case VK_FORMAT_R8G8_UNORM:
         vertex.x = DEREF(INDEX(uint8_t, vertex_ptr, 0)) / float(0xFF);
         vertex.y = DEREF(INDEX(uint8_t, vertex_ptr, 1)) / float(0xFF);
         break;
      case VK_FORMAT_R8G8B8A8_UNORM:
         vertex.x = DEREF(INDEX(uint8_t, vertex_ptr, 0)) / float(0xFF);
         vertex.y = DEREF(INDEX(uint8_t, vertex_ptr, 1)) / float(0xFF);
         vertex.z = DEREF(INDEX(uint8_t, vertex_ptr, 2)) / float(0xFF);
         break;
      case VK_FORMAT_A2B10G10R10_UNORM_PACK32: {
         uint32_t data = DEREF(REF(uint32_t)(vertex_ptr));
         vertex.x = float(data & 0x3FF) / 0x3FF;
         vertex.y = float((data >> 10) & 0x3FF) / 0x3FF;
         vertex.z = float((data >> 20) & 0x3FF) / 0x3FF;
         break;
      }
      }

      result.vertex[i] = vertex;
   }

   return result;
}

/* Fetch the flags of child nodes used to determine whether all/no children are opaque. */
uint32_t fetch_child_flags(VOID_REF bvh, uint32_t node_ptr)
{
   VOID_REF node = OFFSET(bvh, ir_id_to_offset(node_ptr));
   switch (ir_id_to_type(node_ptr)) {
   case vk_ir_node_triangle:
      return (DEREF(REF(vk_ir_triangle_node)(node)).geometry_id_and_flags & VK_GEOMETRY_OPAQUE) != 0
               ? VK_BVH_BOX_FLAG_ONLY_OPAQUE
               : VK_BVH_BOX_FLAG_NO_OPAQUE;
   case vk_ir_node_internal:
      return DEREF(REF(vk_ir_box_node)(node)).flags;
   case vk_ir_node_instance:
      return DEREF(REF(vk_ir_instance_node)(node)).root_flags;
   case vk_ir_node_aabb:
      return (DEREF(REF(vk_ir_aabb_node)(node)).geometry_id_and_flags & VK_GEOMETRY_OPAQUE) != 0
             ? VK_BVH_BOX_FLAG_ONLY_OPAQUE
             : VK_BVH_BOX_FLAG_NO_OPAQUE;
   default:
      return 0;
   }
}

/** Compute ceiling of integer quotient of A divided by B.
    From macros.h */
#define DIV_ROUND_UP(A, B) (((A) + (B)-1) / (B))

#ifdef USE_GLOBAL_SYNC

/* There might be more invocations available than tasks to do.
 * In that case, the fetched task index is greater than the
 * counter offset for the next phase. To avoid out-of-bounds
 * accessing, phases will be skipped until the task index is
 * is in-bounds again. */
uint32_t num_tasks_to_skip = 0;
uint32_t phase_index = 0;
bool should_skip = false;
shared uint32_t global_task_index;

shared uint32_t shared_phase_index;

uint32_t
task_count(REF(vk_ir_header) header)
{
   uint32_t phase_index = DEREF(header).sync_data.phase_index;
   return DEREF(header).sync_data.task_counts[phase_index & 1];
}

/* Sets the task count for the next phase. */
void
set_next_task_count(REF(vk_ir_header) header, uint32_t new_count)
{
   uint32_t phase_index = DEREF(header).sync_data.phase_index;
   DEREF(header).sync_data.task_counts[(phase_index + 1) & 1] = new_count;
}

/*
 * This function has two main objectives:
 * Firstly, it partitions pending work among free invocations.
 * Secondly, it guarantees global synchronization between different phases.
 *
 * After every call to fetch_task, a new task index is returned.
 * fetch_task will also set num_tasks_to_skip. Use should_execute_phase
 * to determine if the current phase should be executed or skipped.
 *
 * Since tasks are assigned per-workgroup, there is a possibility of the task index being
 * greater than the total task count.
 */
uint32_t
fetch_task(REF(vk_ir_header) header, bool did_work)
{
   /* Perform a memory + control barrier for all buffer writes for the entire workgroup.
    * This guarantees that once the workgroup leaves the PHASE loop, all invocations have finished
    * and their results are written to memory. */
   controlBarrier(gl_ScopeWorkgroup, gl_ScopeDevice, gl_StorageSemanticsBuffer,
                  gl_SemanticsAcquireRelease | gl_SemanticsMakeAvailable | gl_SemanticsMakeVisible);
   if (gl_LocalInvocationIndex == 0) {
      if (did_work)
         atomicAdd(DEREF(header).sync_data.task_done_counter, 1);
      global_task_index = atomicAdd(DEREF(header).sync_data.task_started_counter, 1);

      do {
         /* Perform a memory barrier to refresh the current phase's end counter, in case
          * another workgroup changed it. */
         memoryBarrier(gl_ScopeDevice, gl_StorageSemanticsBuffer,
                       gl_SemanticsAcquireRelease | gl_SemanticsMakeAvailable | gl_SemanticsMakeVisible);

         /* The first invocation of the first workgroup in a new phase is responsible to initiate the
          * switch to a new phase. It is only possible to switch to a new phase if all tasks of the
          * previous phase have been completed. Switching to a new phase and incrementing the phase
          * end counter in turn notifies all invocations for that phase that it is safe to execute.
          */
         if (global_task_index == DEREF(header).sync_data.current_phase_end_counter &&
             DEREF(header).sync_data.task_done_counter == DEREF(header).sync_data.current_phase_end_counter) {
            if (DEREF(header).sync_data.next_phase_exit_flag != 0) {
               DEREF(header).sync_data.phase_index = TASK_INDEX_INVALID;
               memoryBarrier(gl_ScopeDevice, gl_StorageSemanticsBuffer,
                             gl_SemanticsAcquireRelease | gl_SemanticsMakeAvailable | gl_SemanticsMakeVisible);
            } else {
               atomicAdd(DEREF(header).sync_data.phase_index, 1);
               DEREF(header).sync_data.current_phase_start_counter = DEREF(header).sync_data.current_phase_end_counter;
               /* Ensure the changes to the phase index and start/end counter are visible for other
                * workgroup waiting in the loop. */
               memoryBarrier(gl_ScopeDevice, gl_StorageSemanticsBuffer,
                             gl_SemanticsAcquireRelease | gl_SemanticsMakeAvailable | gl_SemanticsMakeVisible);
               atomicAdd(DEREF(header).sync_data.current_phase_end_counter,
                         DIV_ROUND_UP(task_count(header), gl_WorkGroupSize.x));
            }
            break;
         }

         /* If other invocations have finished all nodes, break out; there is no work to do */
         if (DEREF(header).sync_data.phase_index == TASK_INDEX_INVALID) {
            break;
         }
      } while (global_task_index >= DEREF(header).sync_data.current_phase_end_counter);

      shared_phase_index = DEREF(header).sync_data.phase_index;
   }

   barrier();
   if (DEREF(header).sync_data.phase_index == TASK_INDEX_INVALID)
      return TASK_INDEX_INVALID;

   num_tasks_to_skip = shared_phase_index - phase_index;

   uint32_t local_task_index = global_task_index - DEREF(header).sync_data.current_phase_start_counter;
   return local_task_index * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
}

bool
should_execute_phase()
{
   if (num_tasks_to_skip > 0) {
      /* Skip to next phase. */
      ++phase_index;
      --num_tasks_to_skip;
      return false;
   }
   return true;
}

#define PHASE(header)                                                                                                  \
   for (; task_index != TASK_INDEX_INVALID && should_execute_phase(); task_index = fetch_task(header, true))
#endif

#if ((VK_USED_BUILD_FLAGS & VK_BUILD_FLAG_ALWAYS_ACTIVE) != 0)
#define VK_TEST_BUILD_FLAG_ALWAYS_ACTIVE ((BUILD_FLAGS & VK_BUILD_FLAG_ALWAYS_ACTIVE) != 0)
#endif

#if ((VK_USED_BUILD_FLAGS & VK_BUILD_FLAG_PROPAGATE_CULL_FLAGS) != 0)
#define VK_TEST_BUILD_FLAG_PROPAGATE_CULL_FLAGS ((BUILD_FLAGS & VK_BUILD_FLAG_PROPAGATE_CULL_FLAGS) != 0)
#endif

#if ((VK_USED_BUILD_FLAGS & VK_BUILD_FLAG_64BIT_KEYS) != 0)
#define VK_TEST_BUILD_FLAG_64BIT_KEYS ((BUILD_FLAGS & VK_BUILD_FLAG_64BIT_KEYS) != 0)
#endif

#endif
