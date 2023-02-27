/*
 * Copyright © 2022 Friedrich Vock
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

#include "util/u_process.h"
#include "radv_acceleration_structure.h"
#include "radv_meta.h"
#include "radv_private.h"
#include "vk_common_entrypoints.h"
#include "wsi_common_entrypoints.h"

static void
radv_rra_handle_trace(VkQueue _queue)
{
   RADV_FROM_HANDLE(radv_queue, queue, _queue);

   simple_mtx_lock(&queue->device->rra_trace.data_mtx);
   /*
    * TODO: This code is shared with RGP tracing and could be merged in a common helper.
    */
   bool frame_trigger =
      queue->device->rra_trace.elapsed_frames == queue->device->rra_trace.trace_frame;
   if (queue->device->rra_trace.elapsed_frames <= queue->device->rra_trace.trace_frame)
      ++queue->device->rra_trace.elapsed_frames;

   bool file_trigger = false;
#ifndef _WIN32
   if (queue->device->rra_trace.trigger_file &&
       access(queue->device->rra_trace.trigger_file, W_OK) == 0) {
      if (unlink(queue->device->rra_trace.trigger_file) == 0) {
         file_trigger = true;
      } else {
         /* Do not enable tracing if we cannot remove the file,
          * because by then we'll trace every frame ... */
         fprintf(stderr, "radv: could not remove RRA trace trigger file, ignoring\n");
      }
   }
#endif

   if (!frame_trigger && !file_trigger) {
      simple_mtx_unlock(&queue->device->rra_trace.data_mtx);
      return;
   }

   if (_mesa_hash_table_num_entries(queue->device->rra_trace.accel_structs) == 0) {
      fprintf(stderr, "radv: No acceleration structures captured, not saving RRA trace.\n");
      simple_mtx_unlock(&queue->device->rra_trace.data_mtx);
      return;
   }

   char filename[2048];
   struct tm now;
   time_t t;

   t = time(NULL);
   now = *localtime(&t);

   snprintf(filename, sizeof(filename), "/tmp/%s_%04d.%02d.%02d_%02d.%02d.%02d.rra",
            util_get_process_name(), 1900 + now.tm_year, now.tm_mon + 1, now.tm_mday, now.tm_hour,
            now.tm_min, now.tm_sec);

   VkResult result = radv_rra_dump_trace(_queue, filename);

   if (result == VK_SUCCESS)
      fprintf(stderr, "radv: RRA capture saved to '%s'\n", filename);
   else
      fprintf(stderr, "radv: Failed to save RRA capture!\n");

   simple_mtx_unlock(&queue->device->rra_trace.data_mtx);
}

VKAPI_ATTR VkResult VKAPI_CALL
rra_QueuePresentKHR(VkQueue _queue, const VkPresentInfoKHR *pPresentInfo)
{
   RADV_FROM_HANDLE(radv_queue, queue, _queue);
   VkResult result = queue->device->layer_dispatch.rra.QueuePresentKHR(_queue, pPresentInfo);
   if (result != VK_SUCCESS)
      return result;

   radv_rra_handle_trace(_queue);

   struct hash_table *accel_structs = queue->device->rra_trace.accel_structs;

   hash_table_foreach (accel_structs, entry) {
      struct radv_rra_accel_struct_data *data = entry->data;
      if (!data->is_dead)
         continue;

      radv_destroy_rra_accel_struct_data(radv_device_to_handle(queue->device), data);
      _mesa_hash_table_remove(accel_structs, entry);
   }

   return VK_SUCCESS;
}

static uint32_t
find_memory_index(VkDevice _device, VkMemoryPropertyFlags flags)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   VkPhysicalDeviceMemoryProperties *mem_properties = &device->physical_device->memory_properties;
   for (uint32_t i = 0; i < mem_properties->memoryTypeCount; ++i) {
      if (mem_properties->memoryTypes[i].propertyFlags == flags) {
         return i;
      }
   }
   unreachable("invalid memory properties");
}

static VkResult
rra_init_accel_struct_data_buffer(VkDevice vk_device, struct radv_rra_accel_struct_data *data)
{
   RADV_FROM_HANDLE(radv_device, device, vk_device);
   VkBufferCreateInfo buffer_create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = data->size,
   };

   VkResult result = radv_create_buffer(device, &buffer_create_info, NULL, &data->buffer, true);
   if (result != VK_SUCCESS)
      return result;

   VkMemoryRequirements requirements;
   vk_common_GetBufferMemoryRequirements(vk_device, data->buffer, &requirements);

   VkMemoryAllocateFlagsInfo flags_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
      .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
   };

   VkMemoryAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &flags_info,
      .allocationSize = requirements.size,
      .memoryTypeIndex = find_memory_index(vk_device, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                                         VK_MEMORY_PROPERTY_HOST_CACHED_BIT),
   };
   result = radv_alloc_memory(device, &alloc_info, NULL, &data->memory, true);
   if (result != VK_SUCCESS)
      goto fail_buffer;

   result = vk_common_BindBufferMemory(vk_device, data->buffer, data->memory, 0);
   if (result != VK_SUCCESS)
      goto fail_memory;

   return result;
fail_memory:
   radv_FreeMemory(vk_device, data->memory, NULL);
fail_buffer:
   radv_DestroyBuffer(vk_device, data->buffer, NULL);
   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
rra_CreateAccelerationStructureKHR(VkDevice _device,
                                   const VkAccelerationStructureCreateInfoKHR *pCreateInfo,
                                   const VkAllocationCallbacks *pAllocator,
                                   VkAccelerationStructureKHR *pAccelerationStructure)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   VkResult result = device->layer_dispatch.rra.CreateAccelerationStructureKHR(
      _device, pCreateInfo, pAllocator, pAccelerationStructure);

   if (result != VK_SUCCESS)
      return result;

   RADV_FROM_HANDLE(radv_acceleration_structure, structure, *pAccelerationStructure);
   simple_mtx_lock(&device->rra_trace.data_mtx);

   struct radv_rra_accel_struct_data *data = malloc(sizeof(struct radv_rra_accel_struct_data));
   if (!data) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail_as;
   }

   data->va = structure->buffer->bo ? radv_acceleration_structure_get_va(structure) : 0;
   data->size = structure->size;
   data->type = pCreateInfo->type;
   data->is_dead = false;

   VkEventCreateInfo eventCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO,
   };

   result = radv_create_event(device, &eventCreateInfo, NULL, &data->build_event, true);
   if (result != VK_SUCCESS)
      goto fail_data;

   result = rra_init_accel_struct_data_buffer(_device, data);
   if (result != VK_SUCCESS)
      goto fail_event;

   _mesa_hash_table_insert(device->rra_trace.accel_structs, structure, data);

   if (data->va)
      _mesa_hash_table_u64_insert(device->rra_trace.accel_struct_vas, data->va, structure);

   goto exit;
fail_event:
   radv_DestroyEvent(_device, data->build_event, NULL);
fail_data:
   free(data);
fail_as:
   device->layer_dispatch.rra.DestroyAccelerationStructureKHR(_device, *pAccelerationStructure,
                                                              pAllocator);
   *pAccelerationStructure = VK_NULL_HANDLE;
exit:
   simple_mtx_unlock(&device->rra_trace.data_mtx);
   return result;
}

static void
copy_accel_struct_to_data(VkCommandBuffer commandBuffer,
                          struct radv_acceleration_structure *accel_struct,
                          struct radv_rra_accel_struct_data *data)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

   VkMemoryBarrier2 barrier = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
      .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
      .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
      .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
   };

   VkDependencyInfo dependencyInfo = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .memoryBarrierCount = 1,
      .pMemoryBarriers = &barrier,
   };

   radv_CmdPipelineBarrier2(commandBuffer, &dependencyInfo);

   vk_common_CmdSetEvent(commandBuffer, data->build_event, 0);

   VkBufferCopy2 region = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
      .srcOffset = accel_struct->offset,
      .size = accel_struct->size,
   };

   VkCopyBufferInfo2 copyInfo = {
      .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
      .srcBuffer = radv_buffer_to_handle(accel_struct->buffer),
      .dstBuffer = data->buffer,
      .regionCount = 1,
      .pRegions = &region,
   };

   radv_CmdCopyBuffer2(commandBuffer, &copyInfo);

   if (!data->va) {
      data->va = radv_acceleration_structure_get_va(accel_struct);
      _mesa_hash_table_u64_insert(cmd_buffer->device->rra_trace.accel_struct_vas, data->va,
                                  accel_struct);
   }
}

VKAPI_ATTR void VKAPI_CALL
rra_CmdBuildAccelerationStructuresKHR(
   VkCommandBuffer commandBuffer, uint32_t infoCount,
   const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
   const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   cmd_buffer->device->layer_dispatch.rra.CmdBuildAccelerationStructuresKHR(
      commandBuffer, infoCount, pInfos, ppBuildRangeInfos);

   simple_mtx_lock(&cmd_buffer->device->rra_trace.data_mtx);
   for (uint32_t i = 0; i < infoCount; ++i) {
      RADV_FROM_HANDLE(radv_acceleration_structure, structure, pInfos[i].dstAccelerationStructure);
      struct hash_entry *entry = _mesa_hash_table_search(
         cmd_buffer->device->rra_trace.accel_structs, structure);

      assert(entry);
      struct radv_rra_accel_struct_data *data = entry->data;

      copy_accel_struct_to_data(commandBuffer, structure, data);
   }
   simple_mtx_unlock(&cmd_buffer->device->rra_trace.data_mtx);
}

VKAPI_ATTR void VKAPI_CALL
rra_CmdCopyAccelerationStructureKHR(VkCommandBuffer commandBuffer,
                                    const VkCopyAccelerationStructureInfoKHR *pInfo)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   cmd_buffer->device->layer_dispatch.rra.CmdCopyAccelerationStructureKHR(commandBuffer, pInfo);

   simple_mtx_lock(&cmd_buffer->device->rra_trace.data_mtx);

   RADV_FROM_HANDLE(radv_acceleration_structure, structure, pInfo->dst);
   struct hash_entry *entry =
      _mesa_hash_table_search(cmd_buffer->device->rra_trace.accel_structs, structure);

   assert(entry);
   struct radv_rra_accel_struct_data *data = entry->data;

   copy_accel_struct_to_data(commandBuffer, structure, data);

   simple_mtx_unlock(&cmd_buffer->device->rra_trace.data_mtx);
}

VKAPI_ATTR void VKAPI_CALL
rra_CmdCopyMemoryToAccelerationStructureKHR(VkCommandBuffer commandBuffer,
                                            const VkCopyMemoryToAccelerationStructureInfoKHR *pInfo)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   cmd_buffer->device->layer_dispatch.rra.CmdCopyMemoryToAccelerationStructureKHR(commandBuffer,
                                                                                  pInfo);

   simple_mtx_lock(&cmd_buffer->device->rra_trace.data_mtx);

   RADV_FROM_HANDLE(radv_acceleration_structure, structure, pInfo->dst);
   struct hash_entry *entry =
      _mesa_hash_table_search(cmd_buffer->device->rra_trace.accel_structs, structure);

   assert(entry);
   struct radv_rra_accel_struct_data *data = entry->data;

   copy_accel_struct_to_data(commandBuffer, structure, data);

   simple_mtx_unlock(&cmd_buffer->device->rra_trace.data_mtx);
}

VKAPI_ATTR void VKAPI_CALL
rra_DestroyAccelerationStructureKHR(VkDevice _device, VkAccelerationStructureKHR _structure,
                                    const VkAllocationCallbacks *pAllocator)
{
   if (!_structure)
      return;

   RADV_FROM_HANDLE(radv_device, device, _device);
   simple_mtx_lock(&device->rra_trace.data_mtx);

   RADV_FROM_HANDLE(radv_acceleration_structure, structure, _structure);

   struct hash_entry *entry =
      _mesa_hash_table_search(device->rra_trace.accel_structs, structure);

   assert(entry);
   struct radv_rra_accel_struct_data *data = entry->data;
   data->is_dead = true;

   simple_mtx_unlock(&device->rra_trace.data_mtx);

   device->layer_dispatch.rra.DestroyAccelerationStructureKHR(_device, _structure, pAllocator);
}