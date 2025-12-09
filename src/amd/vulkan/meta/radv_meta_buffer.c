/* Based on anv:
 * Copyright © 2015 Intel Corporation
 *
 * Copyright © 2016 Red Hat Inc.
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir/radv_meta_nir.h"
#include "radv_cp_dma.h"
#include "radv_debug.h"
#include "radv_meta.h"
#include "radv_sdma.h"

#include "radv_cs.h"

struct fill_constants {
   uint64_t addr;
   uint32_t max_offset;
   uint32_t data;
};

struct radv_fill_memory_key {
   enum radv_meta_object_key_type type;
   bool use_16B_copy;
};

static VkResult
get_fill_memory_pipeline(struct radv_device *device, uint64_t size, VkPipeline *pipeline_out,
                         VkPipelineLayout *layout_out)
{
   const bool use_16B_copy = size >= 16;
   struct radv_fill_memory_key key;
   VkResult result;

   memset(&key, 0, sizeof(key));
   key.type = RADV_META_OBJECT_KEY_FILL_MEMORY;
   key.use_16B_copy = use_16B_copy;

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .size = sizeof(struct fill_constants),
   };

   result = vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, NULL, &pc_range, &key, sizeof(key),
                                        layout_out);
   if (result != VK_SUCCESS)
      return result;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *cs = radv_meta_nir_build_fill_memory_shader(device, use_16B_copy ? 16 : 4);

   const VkPipelineShaderStageCreateInfo stage_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = vk_shader_module_handle_from_nir(cs),
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   const VkComputePipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = stage_info,
      .flags = 0,
      .layout = *layout_out,
   };

   result = vk_meta_create_compute_pipeline(&device->vk, &device->meta_state.device, &pipeline_info, &key, sizeof(key),
                                            pipeline_out);

   ralloc_free(cs);
   return result;
}

struct copy_constants {
   uint64_t src_addr;
   uint64_t dst_addr;
   uint32_t max_offset;
};

struct radv_copy_memory_key {
   enum radv_meta_object_key_type type;
   bool use_16B_copy;
};

static bool
radv_is_copy_memory_4B_aligned(uint64_t src_va, uint64_t dst_va, uint64_t size)
{
   return !(size & 3) && !(src_va & 3) && !(dst_va & 3);
}

static VkResult
get_copy_memory_pipeline(struct radv_device *device, uint64_t src_va, uint64_t dst_va, uint64_t size,
                         VkPipeline *pipeline_out, VkPipelineLayout *layout_out)
{
   const bool use_16B_copy = size >= 16 && radv_is_copy_memory_4B_aligned(src_va, dst_va, size);
   struct radv_copy_memory_key key;
   VkResult result;

   memset(&key, 0, sizeof(key));
   key.type = RADV_META_OBJECT_KEY_COPY_MEMORY;
   key.use_16B_copy = use_16B_copy;

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .size = sizeof(struct copy_constants),
   };

   result = vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, NULL, &pc_range, &key, sizeof(key),
                                        layout_out);
   if (result != VK_SUCCESS)
      return result;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *cs = radv_meta_nir_build_copy_memory_shader(device, use_16B_copy ? 16 : 1);

   const VkPipelineShaderStageCreateInfo stage_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = vk_shader_module_handle_from_nir(cs),
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   const VkComputePipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = stage_info,
      .flags = 0,
      .layout = *layout_out,
   };

   result = vk_meta_create_compute_pipeline(&device->vk, &device->meta_state.device, &pipeline_info, &key, sizeof(key),
                                            pipeline_out);

   ralloc_free(cs);
   return result;
}

static void
radv_compute_fill_memory(struct radv_cmd_buffer *cmd_buffer, uint64_t va, uint64_t size, uint32_t data)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_meta_saved_state saved_state;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   result = get_fill_memory_pipeline(device, size, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   radv_meta_save(&saved_state, cmd_buffer, RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_CONSTANTS);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   assert(size <= UINT32_MAX);

   struct fill_constants fill_consts = {
      .addr = va,
      .data = data,
   };
   uint32_t dim_x;

   if (size >= 16) {
      fill_consts.max_offset = size - 16;
      dim_x = DIV_ROUND_UP(size, 16);
   } else {
      fill_consts.max_offset = size - 4;
      dim_x = DIV_ROUND_UP(size, 4);
   }

   const VkPushConstantsInfoKHR pc_info = {
      .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO_KHR,
      .layout = layout,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .offset = 0,
      .size = sizeof(fill_consts),
      .pValues = &fill_consts,
   };

   radv_CmdPushConstants2(radv_cmd_buffer_to_handle(cmd_buffer), &pc_info);

   radv_unaligned_dispatch(cmd_buffer, dim_x, 1, 1);

   radv_meta_restore(&saved_state, cmd_buffer);
}

static void
radv_compute_copy_memory(struct radv_cmd_buffer *cmd_buffer, uint64_t src_va, uint64_t dst_va, uint64_t size)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const bool use_16B_copy = size >= 16 && radv_is_copy_memory_4B_aligned(src_va, dst_va, size);
   struct radv_meta_saved_state saved_state;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   result = get_copy_memory_pipeline(device, src_va, dst_va, size, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   radv_meta_save(&saved_state, cmd_buffer, RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_CONSTANTS);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   assert(size <= UINT32_MAX);

   struct copy_constants copy_consts = {
      .src_addr = src_va,
      .dst_addr = dst_va,
   };
   uint32_t dim_x;

   if (use_16B_copy) {
      copy_consts.max_offset = size - 16;
      dim_x = DIV_ROUND_UP(size, 16);
   } else {
      copy_consts.max_offset = size;
      dim_x = size;
   }

   const VkPushConstantsInfoKHR pc_info = {
      .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO_KHR,
      .layout = layout,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .offset = 0,
      .size = sizeof(copy_consts),
      .pValues = &copy_consts,
   };

   radv_CmdPushConstants2(radv_cmd_buffer_to_handle(cmd_buffer), &pc_info);

   radv_unaligned_dispatch(cmd_buffer, dim_x, 1, 1);

   radv_meta_restore(&saved_state, cmd_buffer);
}

static bool
radv_prefer_compute_or_cp_dma(const struct radv_device *device, uint64_t size, enum radv_copy_flags src_copy_flags,
                              enum radv_copy_flags dst_copy_flags)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   bool use_compute = size >= RADV_BUFFER_OPS_CS_THRESHOLD;

   if (pdev->info.gfx_level >= GFX10 && pdev->info.has_dedicated_vram) {
      if (!(src_copy_flags & RADV_COPY_FLAGS_DEVICE_LOCAL) || !(dst_copy_flags & RADV_COPY_FLAGS_DEVICE_LOCAL)) {
         /* Prefer CP DMA for GTT on dGPUS due to slow PCIe. */
         use_compute = false;
      }
   }

   return use_compute;
}

static bool
radv_is_compute_required(const struct radv_device *device, enum radv_copy_flags src_copy_flags,
                         enum radv_copy_flags dst_copy_flags)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   /* Use compute when CP DMA doesn't support sparse. */
   return !pdev->info.cp_dma_supports_sparse &&
          ((src_copy_flags & RADV_COPY_FLAGS_SPARSE) || (dst_copy_flags & RADV_COPY_FLAGS_SPARSE));
}

static uint32_t
radv_fill_memory_internal(struct radv_cmd_buffer *cmd_buffer, const struct radv_image *image, uint64_t va,
                          uint64_t size, uint32_t value, enum radv_copy_flags copy_flags)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const bool use_compute = radv_is_compute_required(device, copy_flags, copy_flags) ||
                            radv_prefer_compute_or_cp_dma(device, size, copy_flags, copy_flags);
   uint32_t flush_bits = 0;

   assert(!(va & 3));
   assert(!(size & 3));

   if (cmd_buffer->qf == RADV_QUEUE_TRANSFER) {
      radv_sdma_fill_memory(device, cmd_buffer->cs, va, size, value);
   } else if (use_compute) {
      radv_compute_fill_memory(cmd_buffer, va, size, value);

      flush_bits = RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_INV_VCACHE |
                   radv_src_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                         VK_ACCESS_2_SHADER_WRITE_BIT, 0, image, NULL);
   } else if (size)
      radv_cp_dma_fill_memory(cmd_buffer, va, size, value);

   return flush_bits;
}

uint32_t
radv_fill_memory(struct radv_cmd_buffer *cmd_buffer, uint64_t va, uint64_t size, uint32_t value,
                 enum radv_copy_flags copy_flags)
{
   return radv_fill_memory_internal(cmd_buffer, NULL, va, size, value, copy_flags);
}

uint32_t
radv_fill_image(struct radv_cmd_buffer *cmd_buffer, const struct radv_image *image, uint64_t offset, uint64_t size,
                uint32_t value)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const uint64_t va = image->bindings[0].addr + offset;
   struct radeon_winsys_bo *bo = image->bindings[0].bo;
   const enum radv_copy_flags copy_flags = radv_get_copy_flags_from_bo(bo);
   struct radv_cmd_stream *cs = cmd_buffer->cs;

   radv_cs_add_buffer(device->ws, cs->b, bo);

   return radv_fill_memory_internal(cmd_buffer, image, va, size, value, copy_flags);
}

uint32_t
radv_fill_buffer(struct radv_cmd_buffer *cmd_buffer, struct radeon_winsys_bo *bo, uint64_t va, uint64_t size,
                 uint32_t value)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const enum radv_copy_flags copy_flags = radv_get_copy_flags_from_bo(bo);
   struct radv_cmd_stream *cs = cmd_buffer->cs;

   radv_cs_add_buffer(device->ws, cs->b, bo);

   return radv_fill_memory(cmd_buffer, va, size, value, copy_flags);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize fillSize,
                   uint32_t data)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(radv_buffer, dst_buffer, dstBuffer);

   radv_suspend_conditional_rendering(cmd_buffer);

   fillSize = vk_buffer_range(&dst_buffer->vk, dstOffset, fillSize) & ~3ull;

   radv_fill_buffer(cmd_buffer, dst_buffer->bo, vk_buffer_address(&dst_buffer->vk, dstOffset), fillSize, data);

   radv_resume_conditional_rendering(cmd_buffer);
}

void
radv_copy_memory(struct radv_cmd_buffer *cmd_buffer, uint64_t src_va, uint64_t dst_va, uint64_t size,
                 enum radv_copy_flags src_copy_flags, enum radv_copy_flags dst_copy_flags)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const bool use_compute = radv_is_compute_required(device, src_copy_flags, dst_copy_flags) ||
                            (radv_is_copy_memory_4B_aligned(src_va, dst_va, size) &&
                             radv_prefer_compute_or_cp_dma(device, size, src_copy_flags, dst_copy_flags));

   if (cmd_buffer->qf == RADV_QUEUE_TRANSFER) {
      radv_sdma_copy_memory(device, cmd_buffer->cs, src_va, dst_va, size);
   } else if (use_compute) {
      radv_compute_copy_memory(cmd_buffer, src_va, dst_va, size);
   } else if (size) {
      radv_cp_dma_copy_memory(cmd_buffer, src_va, dst_va, size);
   }
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdCopyBuffer2(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2 *pCopyBufferInfo)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(radv_buffer, src_buffer, pCopyBufferInfo->srcBuffer);
   VK_FROM_HANDLE(radv_buffer, dst_buffer, pCopyBufferInfo->dstBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_cmd_stream *cs = cmd_buffer->cs;

   const enum radv_copy_flags src_copy_flags = radv_get_copy_flags_from_bo(src_buffer->bo);
   const enum radv_copy_flags dst_copy_flags = radv_get_copy_flags_from_bo(dst_buffer->bo);

   radv_suspend_conditional_rendering(cmd_buffer);

   radv_cs_add_buffer(device->ws, cs->b, src_buffer->bo);
   radv_cs_add_buffer(device->ws, cs->b, dst_buffer->bo);

   for (unsigned r = 0; r < pCopyBufferInfo->regionCount; r++) {
      const VkBufferCopy2 *region = &pCopyBufferInfo->pRegions[r];
      const uint64_t src_va = vk_buffer_address(&src_buffer->vk, region->srcOffset);
      const uint64_t dst_va = vk_buffer_address(&dst_buffer->vk, region->dstOffset);

      radv_copy_memory(cmd_buffer, src_va, dst_va, region->size, src_copy_flags, dst_copy_flags);
   }

   radv_resume_conditional_rendering(cmd_buffer);
}

void
radv_update_memory_cp(struct radv_cmd_buffer *cmd_buffer, uint64_t va, const void *data, uint64_t size)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   uint64_t words = size / 4;
   bool mec = radv_cmd_buffer_uses_mec(cmd_buffer);
   struct radv_cmd_stream *cs = cmd_buffer->cs;

   assert(size < RADV_BUFFER_UPDATE_THRESHOLD);

   radv_emit_cache_flush(cmd_buffer);
   radeon_check_space(device->ws, cs->b, words + 4);

   ac_emit_cp_write_data(cs->b, V_370_ME, mec ? V_370_MEM : V_370_MEM_GRBM, va, words, data, false);

   if (radv_device_fault_detection_enabled(device))
      radv_cmd_buffer_trace_emit(cmd_buffer);
}

void
radv_update_memory(struct radv_cmd_buffer *cmd_buffer, uint64_t va, uint64_t size, const void *data,
                   enum radv_copy_flags dst_copy_flags)
{
   assert(!(size & 3));
   assert(!(va & 3));

   if (!size)
      return;

   if (size < RADV_BUFFER_UPDATE_THRESHOLD && cmd_buffer->qf != RADV_QUEUE_TRANSFER) {
      radv_update_memory_cp(cmd_buffer, va, data, size);
   } else {
      uint32_t buf_offset;

      radv_cmd_buffer_upload_data(cmd_buffer, size, data, &buf_offset);

      const enum radv_copy_flags src_copy_flags = radv_get_copy_flags_from_bo(cmd_buffer->upload.upload_bo);
      const uint64_t src_va = radv_buffer_get_va(cmd_buffer->upload.upload_bo) + buf_offset;

      radv_copy_memory(cmd_buffer, src_va, va, size, src_copy_flags, dst_copy_flags);
   }
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize,
                     const void *pData)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(radv_buffer, dst_buffer, dstBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const uint64_t dst_va = vk_buffer_address(&dst_buffer->vk, dstOffset);
   struct radv_cmd_stream *cs = cmd_buffer->cs;

   const enum radv_copy_flags dst_copy_flags = radv_get_copy_flags_from_bo(dst_buffer->bo);

   radv_suspend_conditional_rendering(cmd_buffer);

   radv_cs_add_buffer(device->ws, cs->b, dst_buffer->bo);

   radv_update_memory(cmd_buffer, dst_va, dataSize, pData, dst_copy_flags);

   radv_resume_conditional_rendering(cmd_buffer);
}
