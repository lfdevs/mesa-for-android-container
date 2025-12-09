/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "common/pvr_iface.h"
#include "hwdef/rogue_hw_utils.h"
#include "pco_uscgen_programs.h"
#include "pvr_bo.h"
#include "pvr_buffer.h"
#include "pvr_cmd_buffer.h"
#include "pvr_device.h"
#include "pvr_formats.h"
#include "pvr_macros.h"
#include "pvr_pds.h"
#include "pvr_physical_device.h"
#include "pvr_pipeline.h"
#include "pvr_query.h"
#include "pvr_tex_state.h"
#include "pvr_types.h"
#include "vk_alloc.h"
#include "vk_command_pool.h"
#include "vk_util.h"

/* TODO: multicore support/awareness. */

static inline void pvr_init_primary_compute_pds_program(
   struct pvr_pds_compute_shader_program *program)
{
   pvr_pds_compute_shader_program_init(program);
   program->local_input_regs[0] = 0;
   /* Workgroup id is in reg0. */
   program->work_group_input_regs[0] = 0;
   program->flattened_work_groups = true;
   program->kick_usc = true;
}

static VkResult
pvr_create_compute_secondary_prog(struct pvr_device *device,
                                  unsigned const_shared_regs,
                                  struct pvr_compute_query_shader *query_prog)
{
   const size_t size =
      pvr_pds_get_max_descriptor_upload_const_map_size_in_bytes();
   struct pvr_pds_descriptor_program_input sec_pds_program;
   struct pvr_pds_info *info = &query_prog->info;
   uint32_t staging_buffer_size;
   uint32_t *staging_buffer;
   VkResult result;

   info->entries =
      vk_zalloc(&device->vk.alloc, size, 8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!info->entries)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   info->entries_size_in_bytes = size;

   sec_pds_program = (struct pvr_pds_descriptor_program_input){
      .buffer_count = 1,
      .buffers = {
         [0] = {
            .buffer_id = 0,
            .source_offset = 0,
            .type = PVR_BUFFER_TYPE_COMPILE_TIME,
            .size_in_dwords = const_shared_regs,
            .destination = 0,
         }
      },
   };

   pvr_pds_generate_descriptor_upload_program(&sec_pds_program, NULL, info);

   staging_buffer_size = info->code_size_in_dwords;

   staging_buffer = vk_alloc(&device->vk.alloc,
                             PVR_DW_TO_BYTES(staging_buffer_size),
                             8,
                             VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!staging_buffer) {
      vk_free(&device->vk.alloc, info->entries);
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   pvr_pds_generate_descriptor_upload_program(&sec_pds_program,
                                              staging_buffer,
                                              info);

   assert(info->code_size_in_dwords <= staging_buffer_size);

   /* FIXME: Figure out the define for alignment of 16. */
   result = pvr_gpu_upload_pds(device,
                               NULL,
                               0,
                               0,
                               staging_buffer,
                               info->code_size_in_dwords,
                               16,
                               16,
                               &query_prog->pds_sec_code);
   if (result != VK_SUCCESS) {
      vk_free(&device->vk.alloc, staging_buffer);
      vk_free(&device->vk.alloc, info->entries);
      return result;
   }

   vk_free(&device->vk.alloc, staging_buffer);

   return VK_SUCCESS;
}

static void
pvr_destroy_compute_secondary_prog(struct pvr_device *device,
                                   struct pvr_compute_query_shader *program)
{
   pvr_bo_suballoc_free(program->pds_sec_code.pvr_bo);
   vk_free(&device->vk.alloc, program->info.entries);
}

static VkResult pvr_create_compute_query_precomp_program(
   struct pvr_device *device,
   enum pco_usclib_program common_program_index,
   unsigned const_shared_regs,
   struct pvr_compute_query_shader *query_prog)
{
   const uint32_t cache_line_size =
      pvr_get_slc_cache_line_size(&device->pdevice->dev_info);
   struct pvr_pds_compute_shader_program pds_primary_prog = { 0 };
   const pco_precomp_data *precomp_data;
   VkResult result;

   memset(query_prog, 0, sizeof(*query_prog));

   precomp_data = (pco_precomp_data *)pco_usclib_common[common_program_index];
   result = pvr_gpu_upload_usc(device,
                               precomp_data->binary,
                               precomp_data->size_dwords * sizeof(uint32_t),
                               cache_line_size,
                               &query_prog->usc_bo);
   if (result != VK_SUCCESS)
      return result;

   pvr_init_primary_compute_pds_program(&pds_primary_prog);

   pvr_pds_setup_doutu(&pds_primary_prog.usc_task_control,
                       query_prog->usc_bo->dev_addr.addr,
                       precomp_data->temps,
                       ROGUE_PDSINST_DOUTU_SAMPLE_RATE_INSTANCE,
                       false);

   result =
      pvr_pds_compute_shader_create_and_upload(device,
                                               &pds_primary_prog,
                                               &query_prog->pds_prim_code);
   if (result != VK_SUCCESS)
      goto err_free_usc_bo;

   query_prog->primary_data_size_dw = pds_primary_prog.data_size;
   query_prog->primary_num_temps = pds_primary_prog.temps_used;

   result =
      pvr_create_compute_secondary_prog(device, const_shared_regs, query_prog);
   if (result != VK_SUCCESS)
      goto err_free_pds_prim_code_bo;

   return VK_SUCCESS;

err_free_pds_prim_code_bo:
   pvr_bo_suballoc_free(query_prog->pds_prim_code.pvr_bo);

err_free_usc_bo:
   pvr_bo_suballoc_free(query_prog->usc_bo);

   return result;
}

/* TODO: See if we can dedup this with pvr_setup_descriptor_mappings() or
 * pvr_setup_descriptor_mappings().
 */
static VkResult pvr_write_compute_query_pds_data_section(
   struct pvr_cmd_buffer *cmd_buffer,
   const struct pvr_compute_query_shader *query_prog,
   struct pvr_private_compute_pipeline *pipeline)
{
   const struct pvr_pds_info *const info = &query_prog->info;
   struct pvr_suballoc_bo *pvr_bo;
   const uint8_t *entries;
   uint32_t *dword_buffer;
   uint64_t *qword_buffer;
   VkResult result;

   result = pvr_cmd_buffer_alloc_mem(cmd_buffer,
                                     cmd_buffer->device->heaps.pds_heap,
                                     PVR_DW_TO_BYTES(info->data_size_in_dwords),
                                     &pvr_bo);
   if (result != VK_SUCCESS)
      return result;

   dword_buffer = (uint32_t *)pvr_bo_suballoc_get_map_addr(pvr_bo);
   qword_buffer = (uint64_t *)pvr_bo_suballoc_get_map_addr(pvr_bo);

   entries = (uint8_t *)info->entries;

   /* TODO: Remove this when we can test this path and make sure that this is
    * not needed. If it's needed we should probably be using LITERAL entries for
    * this instead.
    */
#if !defined(NDEBUG)
   memset(dword_buffer, 0xFE, PVR_DW_TO_BYTES(info->data_size_in_dwords));
#endif /* !defined(NDEBUG) */

   pipeline->pds_shared_update_data_size_dw = info->data_size_in_dwords;

   for (uint32_t i = 0; i < info->entry_count; i++) {
      const struct pvr_const_map_entry *const entry_header =
         (struct pvr_const_map_entry *)entries;

      switch (entry_header->type) {
      case PVR_PDS_CONST_MAP_ENTRY_TYPE_LITERAL32: {
         const struct pvr_const_map_entry_literal32 *const literal =
            (struct pvr_const_map_entry_literal32 *)entries;

         PVR_WRITE(dword_buffer,
                   literal->literal_value,
                   literal->const_offset,
                   info->data_size_in_dwords);

         entries += sizeof(*literal);
         break;
      }
      case PVR_PDS_CONST_MAP_ENTRY_TYPE_LITERAL64: {
         const struct pvr_const_map_entry_literal64 *const literal =
            (struct pvr_const_map_entry_literal64 *)entries;

         PVR_WRITE(qword_buffer,
                   literal->literal_value,
                   literal->const_offset,
                   info->data_size_in_dwords);

         entries += sizeof(*literal);
         break;
      }
      case PVR_PDS_CONST_MAP_ENTRY_TYPE_DOUTU_ADDRESS: {
         const struct pvr_const_map_entry_doutu_address *const doutu_addr =
            (struct pvr_const_map_entry_doutu_address *)entries;
         const pvr_dev_addr_t exec_addr =
            PVR_DEV_ADDR_OFFSET(query_prog->pds_sec_code.pvr_bo->dev_addr,
                                query_prog->pds_sec_code.code_offset);
         uint64_t addr = 0ULL;

         pvr_set_usc_execution_address64(&addr, exec_addr.addr);

         PVR_WRITE(qword_buffer,
                   addr | doutu_addr->doutu_control,
                   doutu_addr->const_offset,
                   info->data_size_in_dwords);

         entries += sizeof(*doutu_addr);
         break;
      }
      case PVR_PDS_CONST_MAP_ENTRY_TYPE_SPECIAL_BUFFER: {
         const struct pvr_const_map_entry_special_buffer *special_buff_entry =
            (struct pvr_const_map_entry_special_buffer *)entries;

         switch (special_buff_entry->buffer_type) {
         case PVR_BUFFER_TYPE_COMPILE_TIME: {
            uint64_t addr = pipeline->const_buffer_addr.addr;

            PVR_WRITE(qword_buffer,
                      addr,
                      special_buff_entry->const_offset,
                      info->data_size_in_dwords);
            break;
         }

         default:
            UNREACHABLE("Unsupported special buffer type.");
         }

         entries += sizeof(*special_buff_entry);
         break;
      }
      default:
         UNREACHABLE("Unsupported data section map");
      }
   }

   pipeline->pds_shared_update_data_offset =
      pvr_bo->dev_addr.addr -
      cmd_buffer->device->heaps.pds_heap->base_addr.addr;

   return VK_SUCCESS;
}

static void pvr_write_private_compute_dispatch(
   struct pvr_cmd_buffer *cmd_buffer,
   struct pvr_private_compute_pipeline *pipeline,
   uint32_t num_query_indices)
{
   struct pvr_sub_cmd *sub_cmd = cmd_buffer->state.current_sub_cmd;
   const uint32_t workgroup_size[PVR_WORKGROUP_DIMENSIONS] = {
      DIV_ROUND_UP(num_query_indices, 32),
      1,
      1,
   };

   assert(sub_cmd->type == PVR_SUB_CMD_TYPE_QUERY);

   pvr_compute_update_shared_private(cmd_buffer, &sub_cmd->compute, pipeline);
   pvr_compute_update_kernel_private(cmd_buffer,
                                     &sub_cmd->compute,
                                     pipeline,
                                     workgroup_size);
   pvr_compute_generate_fence(cmd_buffer, &sub_cmd->compute, false);
}

static void
pvr_destroy_compute_query_program(struct pvr_device *device,
                                  struct pvr_compute_query_shader *program)
{
   pvr_destroy_compute_secondary_prog(device, program);
   pvr_bo_suballoc_free(program->pds_prim_code.pvr_bo);
   pvr_bo_suballoc_free(program->usc_bo);
}

VkResult pvr_device_create_compute_query_programs(struct pvr_device *device)
{
   VkResult result;

   result = pvr_create_compute_query_precomp_program(
      device,
      CS_QUERY_AVAILABILITY_COMMON,
      _PVR_QUERY_AVAILABILITY_DATA_COUNT,
      &device->availability_shader);

   if (result != VK_SUCCESS)
      return result;

   result =
      pvr_create_compute_query_precomp_program(device,
                                               CS_QUERY_COPY_COMMON,
                                               _PVR_QUERY_COPY_DATA_COUNT,
                                               &device->copy_results_shader);

   if (result != VK_SUCCESS)
      goto err_destroy_availability_query_program;

   result =
      pvr_create_compute_query_precomp_program(device,
                                               CS_QUERY_RESET_COMMON,
                                               _PVR_QUERY_RESET_DATA_COUNT,
                                               &device->reset_queries_shader);

   if (result != VK_SUCCESS)
      goto err_destroy_copy_results_query_program;

   return VK_SUCCESS;

err_destroy_copy_results_query_program:
   pvr_destroy_compute_query_program(device, &device->copy_results_shader);

err_destroy_availability_query_program:
   pvr_destroy_compute_query_program(device, &device->availability_shader);

   return result;
}

void pvr_device_destroy_compute_query_programs(struct pvr_device *device)
{
   pvr_destroy_compute_query_program(device, &device->availability_shader);
   pvr_destroy_compute_query_program(device, &device->copy_results_shader);
   pvr_destroy_compute_query_program(device, &device->reset_queries_shader);
}

/* TODO: Split this function into per program type functions. */
VkResult pvr_add_query_program(struct pvr_cmd_buffer *cmd_buffer,
                               const struct pvr_query_info *query_info)
{
   struct pvr_device *device = cmd_buffer->device;
   const struct pvr_compute_query_shader *query_prog;
   struct pvr_private_compute_pipeline pipeline;
   uint32_t num_query_indices;
   uint32_t *const_buffer;
   struct pvr_suballoc_bo *pvr_bo;
   VkResult result;

   result = pvr_cmd_buffer_start_sub_cmd(cmd_buffer, PVR_SUB_CMD_TYPE_QUERY);
   if (result != VK_SUCCESS)
      return result;

   switch (query_info->type) {
   case PVR_QUERY_TYPE_AVAILABILITY_WRITE:
      /* Adds a compute shader (fenced on the last 3D) that writes a non-zero
       * value in availability_bo at every index in index_bo.
       */
      query_prog = &device->availability_shader;
      num_query_indices = query_info->availability_write.num_query_indices;
      pipeline.const_shared_regs_count = _PVR_QUERY_AVAILABILITY_DATA_COUNT;
      break;

   case PVR_QUERY_TYPE_COPY_QUERY_RESULTS:
      /* Adds a compute shader to copy availability and query value data. */
      query_prog = &device->copy_results_shader;
      num_query_indices = query_info->copy_query_results.query_count;
      pipeline.const_shared_regs_count = _PVR_QUERY_COPY_DATA_COUNT;
      break;

   case PVR_QUERY_TYPE_RESET_QUERY_POOL:
      /* Adds a compute shader to reset availability and query value data. */
      query_prog = &device->reset_queries_shader;
      num_query_indices = query_info->reset_query_pool.query_count;
      pipeline.const_shared_regs_count = _PVR_QUERY_RESET_DATA_COUNT;
      break;

   default:
      UNREACHABLE("Invalid query type");
   }

   pipeline.pds_code_offset = query_prog->pds_prim_code.code_offset;
   pipeline.pds_data_offset = query_prog->pds_prim_code.data_offset;

   pipeline.pds_shared_update_code_offset =
      query_prog->pds_sec_code.code_offset;
   pipeline.pds_data_size_dw = query_prog->primary_data_size_dw;
   pipeline.pds_temps_used = query_prog->primary_num_temps;

   /* TODO: set properly. */
   pipeline.coeff_regs_count = 3;
   pipeline.unified_store_regs_count = 8;

   const_buffer = vk_alloc(&cmd_buffer->vk.pool->alloc,
                           PVR_DW_TO_BYTES(pipeline.const_shared_regs_count),
                           8,
                           VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!const_buffer) {
      return vk_command_buffer_set_error(&cmd_buffer->vk,
                                         VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   switch (query_info->type) {
   case PVR_QUERY_TYPE_AVAILABILITY_WRITE: {
      uint64_t index_addr =
         query_info->availability_write.index_bo->dev_addr.addr;

      uint64_t avail_addr =
         query_info->availability_write.availability_bo->dev_addr.addr;

      const_buffer[PVR_QUERY_AVAILABILITY_DATA_INDEX_COUNT] = num_query_indices;
      const_buffer[PVR_QUERY_AVAILABILITY_DATA_INDEX_BO_LO] = index_addr &
                                                              0xffffffff;
      const_buffer[PVR_QUERY_AVAILABILITY_DATA_INDEX_BO_HI] = index_addr >> 32;
      const_buffer[PVR_QUERY_AVAILABILITY_DATA_BO_LO] = avail_addr & 0xffffffff;
      const_buffer[PVR_QUERY_AVAILABILITY_DATA_BO_HI] = avail_addr >> 32;

      break;
   }

   case PVR_QUERY_TYPE_COPY_QUERY_RESULTS: {
      VK_FROM_HANDLE(pvr_query_pool,
                      pool,
                      query_info->copy_query_results.query_pool);
      VK_FROM_HANDLE(pvr_buffer,
                      buffer,
                      query_info->copy_query_results.dst_buffer);

      pvr_dev_addr_t dev_addr;

      /* Assert if no memory is bound to destination buffer. */
      assert(buffer->dev_addr.addr);

      uint64_t offset =
         query_info->copy_query_results.first_query * sizeof(uint32_t);

      dev_addr = PVR_DEV_ADDR_OFFSET(buffer->dev_addr,
                                     query_info->copy_query_results.dst_offset);
      uint64_t dest_addr = dev_addr.addr;

      dev_addr =
         PVR_DEV_ADDR_OFFSET(pool->availability_buffer->dev_addr, offset);
      uint64_t avail_addr = dev_addr.addr;

      dev_addr = PVR_DEV_ADDR_OFFSET(pool->result_buffer->dev_addr, offset);
      uint64_t result_addr = dev_addr.addr;

      const_buffer[PVR_QUERY_COPY_DATA_INDEX_COUNT] = num_query_indices;

      const_buffer[PVR_QUERY_COPY_DATA_DEST_BO_LO] = dest_addr & 0xffffffff;
      const_buffer[PVR_QUERY_COPY_DATA_DEST_BO_HI] = dest_addr >> 32;

      const_buffer[PVR_QUERY_COPY_DATA_AVAILABILITY_BO_LO] = avail_addr &
                                                             0xffffffff;
      const_buffer[PVR_QUERY_COPY_DATA_AVAILABILITY_BO_HI] = avail_addr >> 32;

      const_buffer[PVR_QUERY_COPY_DATA_RESULT_BO_LO] = result_addr & 0xffffffff;
      const_buffer[PVR_QUERY_COPY_DATA_RESULT_BO_HI] = result_addr >> 32;

      const_buffer[PVR_QUERY_COPY_DATA_DEST_STRIDE] =
         query_info->copy_query_results.stride;

      const_buffer[PVR_QUERY_COPY_DATA_FLAGS] =
         query_info->copy_query_results.flags;

      break;
   }

   case PVR_QUERY_TYPE_RESET_QUERY_POOL: {
      VK_FROM_HANDLE(pvr_query_pool,
                      pool,
                      query_info->reset_query_pool.query_pool);

      uint64_t offset =
         query_info->reset_query_pool.first_query * sizeof(uint32_t);

      pvr_dev_addr_t dev_addr =
         PVR_DEV_ADDR_OFFSET(pool->result_buffer->dev_addr, offset);
      uint64_t result_addr = dev_addr.addr;

      dev_addr =
         PVR_DEV_ADDR_OFFSET(pool->availability_buffer->dev_addr, offset);
      uint64_t avail_addr = dev_addr.addr;

      const_buffer[PVR_QUERY_RESET_DATA_INDEX_COUNT] = num_query_indices;
      const_buffer[PVR_QUERY_RESET_DATA_RESULT_BO_LO] = result_addr &
                                                        0xffffffff;
      const_buffer[PVR_QUERY_RESET_DATA_RESULT_BO_HI] = result_addr >> 32;
      const_buffer[PVR_QUERY_RESET_DATA_AVAILABILITY_BO_LO] = avail_addr &
                                                              0xffffffff;
      const_buffer[PVR_QUERY_RESET_DATA_AVAILABILITY_BO_HI] = avail_addr >> 32;

      break;
   }

   default:
      UNREACHABLE("Invalid query type");
   }

   result = pvr_cmd_buffer_upload_general(
      cmd_buffer,
      const_buffer,
      PVR_DW_TO_BYTES(pipeline.const_shared_regs_count),
      &pvr_bo);
   if (result != VK_SUCCESS) {
      vk_free(&cmd_buffer->vk.pool->alloc, const_buffer);

      return result;
   }

   pipeline.const_buffer_addr = pvr_bo->dev_addr;

   vk_free(&cmd_buffer->vk.pool->alloc, const_buffer);

   /* PDS data section for the secondary/constant upload. */
   result = pvr_write_compute_query_pds_data_section(cmd_buffer,
                                                     query_prog,
                                                     &pipeline);
   if (result != VK_SUCCESS)
      return result;

   pipeline.workgroup_size.width = ROGUE_MAX_INSTANCES_PER_TASK;
   pipeline.workgroup_size.height = 1;
   pipeline.workgroup_size.depth = 1;

   pvr_write_private_compute_dispatch(cmd_buffer, &pipeline, num_query_indices);

   return pvr_cmd_buffer_end_sub_cmd(cmd_buffer);
}
