/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based in part on v3dv driver which is:
 * Copyright © 2019 Raspberry Pi
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

#include "pvr_pipeline.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "compiler/shader_enums.h"
#include "hwdef/rogue_hw_utils.h"
#include "nir/nir.h"
#include "nir/nir_lower_blend.h"
#include "pco/pco.h"
#include "pco/pco_data.h"
#include "pvr_bo.h"
#include "pvr_csb.h"
#include "pvr_csb_enum_helpers.h"
#include "pvr_descriptor_set.h"
#include "pvr_device.h"
#include "pvr_entrypoints.h"
#include "pvr_hw_pass.h"
#include "pvr_pass.h"
#include "pvr_pds.h"
#include "pvr_physical_device.h"
#include "pvr_robustness.h"
#include "pvr_types.h"
#include "pvr_usc.h"
#include "util/log.h"
#include "util/macros.h"
#include "util/ralloc.h"
#include "util/u_dynarray.h"
#include "util/u_math.h"
#include "vk_alloc.h"
#include "vk_blend.h"
#include "vk_format.h"
#include "vk_graphics_state.h"
#include "vk_log.h"
#include "vk_object.h"
#include "vk_pipeline_cache.h"
#include "vk_pipeline_layout.h"
#include "vk_render_pass.h"
#include "vk_util.h"
#include "vulkan/runtime/vk_pipeline.h"

/*****************************************************************************
   PDS functions
*****************************************************************************/

/* If allocator == NULL, the internal one will be used. */
static VkResult pvr_pds_coeff_program_create_and_upload(
   struct pvr_device *device,
   const VkAllocationCallbacks *allocator,
   struct pvr_fragment_shader_state *fragment_state)
{
   struct pvr_pds_coeff_loading_program *program =
      &fragment_state->pds_coeff_program;
   uint32_t staging_buffer_size;
   uint32_t *staging_buffer;

   assert(program->num_fpu_iterators < PVR_MAXIMUM_ITERATIONS);

   /* Get the size of the program and then allocate that much memory. */
   pvr_pds_coefficient_loading(program, NULL, PDS_GENERATE_SIZES);

   if (!program->code_size) {
      fragment_state->pds_coeff_program_buffer = NULL;
      fragment_state->stage_state.pds_temps_count = 0;

      return VK_SUCCESS;
   }

   staging_buffer_size =
      PVR_DW_TO_BYTES(program->code_size + program->data_size);

   staging_buffer = vk_alloc2(&device->vk.alloc,
                              allocator,
                              staging_buffer_size,
                              8,
                              VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!staging_buffer)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* Generate the program into is the staging_buffer. */
   pvr_pds_coefficient_loading(program,
                               staging_buffer,
                               PDS_GENERATE_CODEDATA_SEGMENTS);

   fragment_state->pds_coeff_program_buffer = staging_buffer;
   fragment_state->stage_state.pds_temps_count = program->temps_used;

   return VK_SUCCESS;
}

/* If allocator == NULL, the internal one will be used. */
static VkResult pvr_pds_fragment_program_create(
   struct pvr_device *device,
   const VkAllocationCallbacks *allocator,
   pco_shader *fs,
   struct pvr_fragment_shader_state *fragment_state)
{
   struct pvr_pds_kickusc_program *program =
      &fragment_state->pds_fragment_program;
   pco_data *fs_data = pco_shader_data(fs);
   uint32_t staging_buffer_size;
   uint32_t *staging_buffer;

   const pvr_dev_addr_t exec_addr =
      PVR_DEV_ADDR_OFFSET(fragment_state->shader_bo->dev_addr,
                          fs_data->common.entry_offset);

   /* Note this is not strictly required to be done before calculating the
    * staging_buffer_size in this particular case. It can also be done after
    * allocating the buffer. The size from pvr_pds_kick_usc() is constant.
    */
   pvr_pds_setup_doutu(&program->usc_task_control,
                       exec_addr.addr,
                       fs_data->common.temps,
                       ROGUE_PDSINST_DOUTU_SAMPLE_RATE_INSTANCE,
                       fs_data->fs.uses.phase_change);

   pvr_pds_kick_usc(program, NULL, 0, false, PDS_GENERATE_SIZES);

   staging_buffer_size =
      PVR_DW_TO_BYTES(program->code_size + program->data_size);

   staging_buffer = vk_alloc2(&device->vk.alloc,
                              allocator,
                              staging_buffer_size,
                              8,
                              VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!staging_buffer)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   pvr_pds_kick_usc(program,
                    staging_buffer,
                    0,
                    false,
                    PDS_GENERATE_CODEDATA_SEGMENTS);

   fragment_state->pds_fragment_program_buffer = staging_buffer;

   return VK_SUCCESS;
}

static inline size_t pvr_pds_get_max_vertex_program_const_map_size_in_bytes(
   const struct pvr_device_info *dev_info,
   bool robust_buffer_access)
{
   /* FIXME: Use more local variable to improve formatting. */

   /* Maximum memory allocation needed for const map entries in
    * pvr_pds_generate_vertex_primary_program().
    * When robustBufferAccess is disabled, it must be >= 410.
    * When robustBufferAccess is enabled, it must be >= 570.
    *
    * 1. Size of entry for base instance
    *        (pvr_const_map_entry_base_instance)
    *
    * 2. Max. number of vertex inputs (PVR_MAX_VERTEX_INPUT_BINDINGS) * (
    *     if (!robustBufferAccess)
    *         size of vertex attribute entry
    *             (pvr_const_map_entry_vertex_attribute_address) +
    *     else
    *         size of robust vertex attribute entry
    *             (pvr_const_map_entry_robust_vertex_attribute_address) +
    *         size of entry for max attribute index
    *             (pvr_const_map_entry_vertex_attribute_max_index) +
    *     fi
    *     size of Unified Store burst entry
    *         (pvr_const_map_entry_literal32) +
    *     size of entry for vertex stride
    *         (pvr_const_map_entry_literal32) +
    *     size of entries for DDMAD control word
    *         (num_ddmad_literals * pvr_const_map_entry_literal32))
    *
    * 3. Size of entry for DOUTW vertex/instance control word
    *     (pvr_const_map_entry_literal32)
    *
    * 4. Size of DOUTU entry (pvr_const_map_entry_doutu_address)
    */

   const size_t attribute_size =
      (!robust_buffer_access)
         ? sizeof(struct pvr_const_map_entry_vertex_attribute_address)
         : sizeof(struct pvr_const_map_entry_robust_vertex_attribute_address) +
              sizeof(struct pvr_const_map_entry_vertex_attribute_max_index);

   /* If has_pds_ddmadt the DDMAD control word is now a DDMADT control word
    * and is increased by one DWORD to contain the data for the DDMADT's
    * out-of-bounds check.
    */
   const size_t pvr_pds_const_map_vertex_entry_num_ddmad_literals =
      1U + (size_t)PVR_HAS_FEATURE(dev_info, pds_ddmadt);

   return (sizeof(struct pvr_const_map_entry_base_instance) +
           PVR_MAX_VERTEX_INPUT_BINDINGS *
              (attribute_size +
               (2 + pvr_pds_const_map_vertex_entry_num_ddmad_literals) *
                  sizeof(struct pvr_const_map_entry_literal32)) +
           sizeof(struct pvr_const_map_entry_literal32) +
           sizeof(struct pvr_const_map_entry_doutu_address));
}

static inline void
pvr_pds_free_pds_info_map_entries(struct pvr_device *const device,
                                  const VkAllocationCallbacks *const allocator,
                                  struct pvr_const_map_entry **const entries)
{
   if (entries && *entries) {
      vk_free2(&device->vk.alloc, allocator, *entries);
      *entries = NULL;
   }
}

static VkResult pvr_pds_vertex_attrib_program_create_and_upload(
   struct pvr_device *const device,
   const VkAllocationCallbacks *const allocator,
   struct pvr_pds_vertex_primary_program_input *const input,
   struct pvr_pds_attrib_program *const program_out)
{
   const size_t const_entries_size_in_bytes =
      pvr_pds_get_max_vertex_program_const_map_size_in_bytes(
         &device->pdevice->dev_info,
         device->vk.enabled_features.robustBufferAccess);
   struct pvr_pds_upload *const program = &program_out->program;
   struct pvr_pds_info *const info = &program_out->info;
   struct pvr_const_map_entry *new_entries;
   ASSERTED uint32_t code_size_in_dwords;
   size_t staging_buffer_size;
   uint32_t *staging_buffer;
   VkResult result;

   memset(info, 0, sizeof(*info));

   info->entries = vk_alloc2(&device->vk.alloc,
                             allocator,
                             const_entries_size_in_bytes,
                             8,
                             VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!info->entries) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_out;
   }

   info->entries_size_in_bytes = const_entries_size_in_bytes;

   pvr_pds_generate_vertex_primary_program(
      input,
      NULL,
      info,
      device->vk.enabled_features.robustBufferAccess,
      &device->pdevice->dev_info);

   code_size_in_dwords = info->code_size_in_dwords;
   staging_buffer_size = PVR_DW_TO_BYTES(info->code_size_in_dwords);

   staging_buffer = vk_alloc2(&device->vk.alloc,
                              allocator,
                              staging_buffer_size,
                              8,
                              VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!staging_buffer) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_free_entries;
   }

   /* This also fills in info->entries. */
   pvr_pds_generate_vertex_primary_program(
      input,
      staging_buffer,
      info,
      device->vk.enabled_features.robustBufferAccess,
      &device->pdevice->dev_info);

   assert(info->code_size_in_dwords <= code_size_in_dwords);

   new_entries = vk_realloc2(&device->vk.alloc,
                             allocator,
                             info->entries,
                             info->entries_written_size_in_bytes,
                             8,
                             VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!new_entries) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_free_staging_buffer;
   }

   info->entries = new_entries;
   info->entries_size_in_bytes = info->entries_written_size_in_bytes;

   /* FIXME: Figure out the define for alignment of 16. */
   result = pvr_gpu_upload_pds(device,
                               NULL,
                               0,
                               0,
                               staging_buffer,
                               info->code_size_in_dwords,
                               16,
                               16,
                               program);
   if (result != VK_SUCCESS)
      goto err_free_staging_buffer;

   vk_free2(&device->vk.alloc, allocator, staging_buffer);

   return VK_SUCCESS;

err_free_staging_buffer:
   vk_free2(&device->vk.alloc, allocator, staging_buffer);

err_free_entries:
   pvr_pds_free_pds_info_map_entries(device, allocator, &info->entries);

err_out:
   return result;
}

static inline void pvr_pds_vertex_attrib_program_destroy(
   struct pvr_device *const device,
   const struct VkAllocationCallbacks *const allocator,
   struct pvr_pds_attrib_program *const program)
{
   pvr_bo_suballoc_free(program->program.pvr_bo);
   pvr_pds_free_pds_info_map_entries(device, allocator, &program->info.entries);
}

/* This is a const pointer to an array of pvr_pds_attrib_program structs.
 * The array being pointed to is of PVR_PDS_VERTEX_ATTRIB_PROGRAM_COUNT size.
 */
typedef struct pvr_pds_attrib_program (*const pvr_pds_attrib_programs_array_ptr)
   [PVR_PDS_VERTEX_ATTRIB_PROGRAM_COUNT];

/* Generate and uploads a PDS program for DMAing vertex attribs into USC vertex
 * inputs. This will bake the code segment and create a template of the data
 * segment for the command buffer to fill in.
 */
/* If allocator == NULL, the internal one will be used.
 *
 * programs_out_ptr is a pointer to the array where the outputs will be placed.
 */
static VkResult pvr_pds_vertex_attrib_programs_create_and_upload(
   struct pvr_device *device,
   const VkAllocationCallbacks *const allocator,
   pco_data *shader_data,
   const struct pvr_pds_vertex_dma
      dma_descriptions[static const PVR_MAX_VERTEX_ATTRIB_DMAS],
   uint32_t dma_count,
   pvr_pds_attrib_programs_array_ptr programs_out_ptr)
{
   struct pvr_pds_vertex_primary_program_input input = {
      .dma_list = dma_descriptions,
      .dma_count = dma_count,
   };
   uint32_t usc_temp_count = shader_data->common.temps;
   struct pvr_pds_attrib_program *const programs_out = *programs_out_ptr;
   VkResult result;

   pco_range *sys_vals = shader_data->common.sys_vals;
   if (sys_vals[SYSTEM_VALUE_VERTEX_ID].count > 0) {
      input.flags |= PVR_PDS_VERTEX_FLAGS_VERTEX_ID_REQUIRED;
      input.vertex_id_register = sys_vals[SYSTEM_VALUE_VERTEX_ID].start;
   }

   if (sys_vals[SYSTEM_VALUE_INSTANCE_ID].count > 0) {
      input.flags |= PVR_PDS_VERTEX_FLAGS_INSTANCE_ID_REQUIRED;
      input.instance_id_register = sys_vals[SYSTEM_VALUE_INSTANCE_ID].start;
   }

   if (sys_vals[SYSTEM_VALUE_BASE_INSTANCE].count > 0) {
      input.flags |= PVR_PDS_VERTEX_FLAGS_BASE_INSTANCE_REQUIRED;
      input.base_instance_register = sys_vals[SYSTEM_VALUE_BASE_INSTANCE].start;
   }

   if (sys_vals[SYSTEM_VALUE_BASE_VERTEX].count > 0) {
      input.flags |= PVR_PDS_VERTEX_FLAGS_BASE_VERTEX_REQUIRED;
      input.base_vertex_register = sys_vals[SYSTEM_VALUE_BASE_VERTEX].start;
   }

   if (sys_vals[SYSTEM_VALUE_DRAW_ID].count > 0) {
      input.flags |= PVR_PDS_VERTEX_FLAGS_DRAW_INDEX_REQUIRED;
      input.draw_index_register = sys_vals[SYSTEM_VALUE_DRAW_ID].start;
   }

   if (sys_vals[SYSTEM_VALUE_VIEW_INDEX].count > 0) {
      input.flags |= PVR_PDS_VERTEX_FLAGS_VIEW_INDEX_REQUIRED;
      input.view_index_register = sys_vals[SYSTEM_VALUE_VIEW_INDEX].start;
   }

   pvr_pds_setup_doutu(&input.usc_task_control,
                       0,
                       usc_temp_count,
                       ROGUE_PDSINST_DOUTU_SAMPLE_RATE_INSTANCE,
                       false);

   /* Note: programs_out_ptr is a pointer to an array so this is fine. See the
    * typedef.
    */
   for (uint32_t i = 0; i < ARRAY_SIZE(*programs_out_ptr); i++) {
      uint32_t extra_flags;

      switch (i) {
      case PVR_PDS_VERTEX_ATTRIB_PROGRAM_BASIC:
         extra_flags = 0;
         break;

      case PVR_PDS_VERTEX_ATTRIB_PROGRAM_BASE_INSTANCE:
         extra_flags = PVR_PDS_VERTEX_FLAGS_BASE_INSTANCE_VARIANT;
         break;

      case PVR_PDS_VERTEX_ATTRIB_PROGRAM_DRAW_INDIRECT:
         extra_flags = PVR_PDS_VERTEX_FLAGS_DRAW_INDIRECT_VARIANT;
         break;

      default:
         UNREACHABLE("Invalid vertex attrib program type.");
      }

      input.flags |= extra_flags;

      result =
         pvr_pds_vertex_attrib_program_create_and_upload(device,
                                                         allocator,
                                                         &input,
                                                         &programs_out[i]);
      if (result != VK_SUCCESS) {
         for (uint32_t j = 0; j < i; j++) {
            pvr_pds_vertex_attrib_program_destroy(device,
                                                  allocator,
                                                  &programs_out[j]);
         }

         return result;
      }

      input.flags &= ~extra_flags;
   }

   return VK_SUCCESS;
}

size_t pvr_pds_get_max_descriptor_upload_const_map_size_in_bytes(void)
{
   /* Maximum memory allocation needed for const map entries in
    * pvr_pds_generate_descriptor_upload_program().
    * It must be >= 688 bytes. This size is calculated as the sum of:
    *
    *  1. Max. number of descriptor sets (8) * (
    *         size of descriptor entry
    *             (pvr_const_map_entry_descriptor_set) +
    *         size of Common Store burst entry
    *             (pvr_const_map_entry_literal32))
    *
    *  2. Max. number of PDS program buffers (24) * (
    *         size of the largest buffer structure
    *             (pvr_const_map_entry_constant_buffer) +
    *         size of Common Store burst entry
    *             (pvr_const_map_entry_literal32)
    *
    *  3. Size of DOUTU entry (pvr_const_map_entry_doutu_address)
    */

   /* FIXME: PVR_MAX_DESCRIPTOR_SETS is 4 and not 8. The comment above seems to
    * say that it should be 8.
    * Figure our a define for this or is the comment wrong?
    */
   return (8 * (sizeof(struct pvr_const_map_entry_descriptor_set) +
                sizeof(struct pvr_const_map_entry_literal32)) +
           PVR_PDS_MAX_BUFFERS *
              (sizeof(struct pvr_const_map_entry_constant_buffer) +
               sizeof(struct pvr_const_map_entry_literal32)) +
           sizeof(struct pvr_const_map_entry_doutu_address));
}

static VkResult pvr_pds_descriptor_program_create_and_upload(
   struct pvr_device *const device,
   const VkAllocationCallbacks *const allocator,
   const struct vk_pipeline_layout *const layout,
   mesa_shader_stage stage,
   pco_data *data,
   struct pvr_stage_allocation_descriptor_state *const descriptor_state)
{
   const size_t const_entries_size_in_bytes =
      pvr_pds_get_max_descriptor_upload_const_map_size_in_bytes();
   struct pvr_pds_info *const pds_info = &descriptor_state->pds_info;
   struct pvr_pds_descriptor_program_input program = { 0 };
   struct pvr_const_map_entry *new_entries;
   ASSERTED uint32_t code_size_in_dwords;
   uint32_t staging_buffer_size;
   uint32_t *staging_buffer;
   VkResult result;

   *pds_info = (struct pvr_pds_info){ 0 };

   for (unsigned desc_set = 0; desc_set < layout->set_count; ++desc_set) {
      const struct pvr_descriptor_set_layout *set_layout =
         vk_to_pvr_descriptor_set_layout(layout->set_layouts[desc_set]);

      const pco_descriptor_set_data *desc_set_data =
         &data->common.desc_sets[desc_set];
      const pco_range *desc_set_range = &desc_set_data->range;
      const pco_range *desc_set_dynamic_range = &desc_set_data->dynamic_range;

      /* If the descriptor set isn't for this stage or is unused, skip it. */
      if (!(BITFIELD_BIT(stage) & set_layout->stage_flags)) {
         assert(!desc_set_data->used);
         continue;
      }

      if (!desc_set_data->used)
         continue;

      if (desc_set_range->count > 0) {
         program.descriptor_sets[program.descriptor_set_count++] =
            (struct pvr_pds_descriptor_set){
               .descriptor_set = desc_set,
               .size_in_dwords = desc_set_range->count,
               .destination = desc_set_range->start,
            };
      }

      if (desc_set_dynamic_range->count > 0) {
         program.buffers[program.buffer_count++] = (struct pvr_pds_buffer){
            .type = PVR_BUFFER_TYPE_DYNAMIC,
            .size_in_dwords = desc_set_dynamic_range->count,
            .destination = desc_set_dynamic_range->start,
            .desc_set = desc_set,
         };
      }
   }

   pds_info->entries = vk_alloc2(&device->vk.alloc,
                                 allocator,
                                 const_entries_size_in_bytes,
                                 8,
                                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pds_info->entries) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_free_static_consts;
   }

   if (data->common.push_consts.range.count > 0) {
      program.buffers[program.buffer_count++] = (struct pvr_pds_buffer){
         .type = PVR_BUFFER_TYPE_PUSH_CONSTS,
         .size_in_dwords = data->common.push_consts.range.count,
         .destination = data->common.push_consts.range.start,
      };
   }

   if (stage == MESA_SHADER_FRAGMENT && data->fs.blend_consts.count > 0) {
      program.buffers[program.buffer_count++] = (struct pvr_pds_buffer){
         .type = PVR_BUFFER_TYPE_BLEND_CONSTS,
         .size_in_dwords = data->fs.blend_consts.count,
         .destination = data->fs.blend_consts.start,
      };
   }

   if (data->common.point_sampler.count > 0) {
      program.buffers[program.buffer_count++] = (struct pvr_pds_buffer){
         .type = PVR_BUFFER_TYPE_POINT_SAMPLER,
         .size_in_dwords = data->common.point_sampler.count,
         .destination = data->common.point_sampler.start,
      };
   }

   if (data->common.ia_sampler.count > 0) {
      program.buffers[program.buffer_count++] = (struct pvr_pds_buffer){
         .type = PVR_BUFFER_TYPE_IA_SAMPLER,
         .size_in_dwords = data->common.ia_sampler.count,
         .destination = data->common.ia_sampler.start,
      };
   }

   if (data->common.spill_info.count > 0) {
      program.buffers[program.buffer_count++] = (struct pvr_pds_buffer){
         .type = PVR_BUFFER_TYPE_SPILL_INFO,
         .size_in_dwords = data->common.spill_info.count,
         .destination = data->common.spill_info.start,
      };
   }

   if (data->common.scratch_info.count > 0) {
      program.buffers[program.buffer_count++] = (struct pvr_pds_buffer){
         .type = PVR_BUFFER_TYPE_SCRATCH_INFO,
         .size_in_dwords = data->common.scratch_info.count,
         .destination = data->common.scratch_info.start,
      };
   }

   if (stage == MESA_SHADER_FRAGMENT &&
       data->common.sys_vals[SYSTEM_VALUE_FRONT_FACE].count > 0) {
      program.buffers[program.buffer_count++] = (struct pvr_pds_buffer){
         .type = PVR_BUFFER_TYPE_FRONT_FACE_OP,
         .size_in_dwords = data->common.sys_vals[SYSTEM_VALUE_FRONT_FACE].count,
         .destination = data->common.sys_vals[SYSTEM_VALUE_FRONT_FACE].start,
      };
   }

   if (stage == MESA_SHADER_FRAGMENT && data->fs.meta.count > 0) {
      program.buffers[program.buffer_count++] = (struct pvr_pds_buffer){
         .type = PVR_BUFFER_TYPE_FS_META,
         .size_in_dwords = data->fs.meta.count,
         .destination = data->fs.meta.start,
      };
   }

   if (stage == MESA_SHADER_FRAGMENT && data->fs.tile_buffers.count > 0) {
      program.buffers[program.buffer_count++] = (struct pvr_pds_buffer){
         .type = PVR_BUFFER_TYPE_TILE_BUFFERS,
         .size_in_dwords = data->fs.tile_buffers.count,
         .destination = data->fs.tile_buffers.start,
      };
   }

   if (stage == MESA_SHADER_FRAGMENT && data->fs.sample_locations.count > 0) {
      program.buffers[program.buffer_count++] = (struct pvr_pds_buffer){
         .type = PVR_BUFFER_TYPE_SAMPLE_LOCATIONS,
         .size_in_dwords = data->fs.sample_locations.count,
         .destination = data->fs.sample_locations.start,
      };
   }

   pds_info->entries_size_in_bytes = const_entries_size_in_bytes;

   pvr_pds_generate_descriptor_upload_program(&program, NULL, pds_info);

   code_size_in_dwords = pds_info->code_size_in_dwords;
   staging_buffer_size = PVR_DW_TO_BYTES(pds_info->code_size_in_dwords);

   if (!staging_buffer_size) {
      vk_free2(&device->vk.alloc, allocator, pds_info->entries);

      *descriptor_state = (struct pvr_stage_allocation_descriptor_state){ 0 };

      return VK_SUCCESS;
   }

   staging_buffer = vk_alloc2(&device->vk.alloc,
                              allocator,
                              staging_buffer_size,
                              8,
                              VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!staging_buffer) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_free_entries;
   }

   pvr_pds_generate_descriptor_upload_program(&program,
                                              staging_buffer,
                                              pds_info);

   assert(pds_info->code_size_in_dwords <= code_size_in_dwords);

   new_entries = vk_realloc2(&device->vk.alloc,
                             allocator,
                             pds_info->entries,
                             pds_info->entries_written_size_in_bytes,
                             8,
                             VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!new_entries) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_free_staging_buffer;
   }

   pds_info->entries = new_entries;
   pds_info->entries_size_in_bytes = pds_info->entries_written_size_in_bytes;

   /* FIXME: Figure out the define for alignment of 16. */
   result = pvr_gpu_upload_pds(device,
                               NULL,
                               0,
                               0,
                               staging_buffer,
                               pds_info->code_size_in_dwords,
                               16,
                               16,
                               &descriptor_state->pds_code);
   if (result != VK_SUCCESS)
      goto err_free_staging_buffer;

   vk_free2(&device->vk.alloc, allocator, staging_buffer);

   return VK_SUCCESS;

err_free_staging_buffer:
   vk_free2(&device->vk.alloc, allocator, staging_buffer);

err_free_entries:
   pvr_pds_free_pds_info_map_entries(device, allocator, &pds_info->entries);

err_free_static_consts:
   pvr_bo_suballoc_free(descriptor_state->static_consts);

   return result;
}

static void pvr_pds_descriptor_program_destroy(
   struct pvr_device *const device,
   const struct VkAllocationCallbacks *const allocator,
   struct pvr_stage_allocation_descriptor_state *const descriptor_state)
{
   if (!descriptor_state)
      return;

   pvr_bo_suballoc_free(descriptor_state->pds_code.pvr_bo);
   pvr_pds_free_pds_info_map_entries(device,
                                     allocator,
                                     &descriptor_state->pds_info.entries);
   pvr_bo_suballoc_free(descriptor_state->static_consts);
}

static void pvr_pds_compute_program_setup(
   const struct pvr_device_info *dev_info,
   pco_data *cs_data,
   struct pvr_compute_shader_state *compute_state,
   struct pvr_pds_compute_shader_program *const program)
{
   pco_range *sys_vals = cs_data->common.sys_vals;

   pvr_pds_compute_shader_program_init(program);

   if (sys_vals[SYSTEM_VALUE_LOCAL_INVOCATION_INDEX].count > 0) {
      program->local_input_regs[0] =
         sys_vals[SYSTEM_VALUE_LOCAL_INVOCATION_INDEX].start;
   }

   for (unsigned u = 0; u < ARRAY_SIZE(program->work_group_input_regs); ++u) {
      if (sys_vals[SYSTEM_VALUE_WORKGROUP_ID].count > u) {
         program->work_group_input_regs[u] =
            sys_vals[SYSTEM_VALUE_WORKGROUP_ID].start + u;
      }
   }

   for (unsigned u = 0; u < ARRAY_SIZE(program->num_work_groups_regs); ++u) {
      if (sys_vals[SYSTEM_VALUE_NUM_WORKGROUPS].count > u) {
         program->num_work_groups_regs[u] =
            sys_vals[SYSTEM_VALUE_NUM_WORKGROUPS].start + u;
      }
   }

   program->flattened_work_groups = true;
   program->kick_usc = true;

   pvr_pds_setup_doutu(&program->usc_task_control,
                       compute_state->shader_bo->dev_addr.addr,
                       cs_data->common.temps,
                       ROGUE_PDSINST_DOUTU_SAMPLE_RATE_INSTANCE,
                       false);

   if (compute_state->coeff_update_shader_bo) {
      program->has_coefficient_update_task = true;
      pvr_pds_setup_doutu(&program->usc_task_control_coeff_update,
                          compute_state->coeff_update_shader_bo->dev_addr.addr,
                          compute_state->coeff_update_shader_temps,
                          ROGUE_PDSINST_DOUTU_SAMPLE_RATE_INSTANCE,
                          false);
   }

   pvr_pds_compute_shader(program, NULL, PDS_GENERATE_SIZES, dev_info);
}

/* This uploads the code segment and base data segment variant.
 * This can be patched at dispatch time.
 */
static VkResult pvr_pds_compute_program_create_and_upload(
   struct pvr_device *const device,
   const VkAllocationCallbacks *const allocator,
   struct pvr_compute_shader_state *compute_state,
   struct pvr_compute_pipeline *compute_pipeline)
{
   pco_range *sys_vals = compute_pipeline->cs_data.common.sys_vals;
   struct pvr_device_info *dev_info = &device->pdevice->dev_info;
   struct pvr_pds_compute_shader_program program;
   uint32_t *code_buffer;
   uint32_t *data_buffer;
   VkResult result;

   bool uses_wg_id = sys_vals[SYSTEM_VALUE_WORKGROUP_ID].count > 0;
   bool uses_num_wgs = sys_vals[SYSTEM_VALUE_NUM_WORKGROUPS].count > 0;

   pvr_pds_compute_program_setup(dev_info,
                                 &compute_pipeline->cs_data,
                                 compute_state,
                                 &program);

   code_buffer = vk_alloc2(&device->vk.alloc,
                           allocator,
                           PVR_DW_TO_BYTES(program.code_size),
                           8,
                           VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!code_buffer)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   data_buffer = vk_alloc2(&device->vk.alloc,
                           allocator,
                           PVR_DW_TO_BYTES(program.code_size),
                           8,
                           VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!data_buffer) {
      vk_free2(&device->vk.alloc, allocator, code_buffer);
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   pvr_pds_compute_shader(&program,
                          &code_buffer[0],
                          PDS_GENERATE_CODE_SEGMENT,
                          dev_info);

   pvr_pds_compute_shader(&program,
                          &data_buffer[0],
                          PDS_GENERATE_DATA_SEGMENT,
                          dev_info);

   /* Initialize. */
   if (uses_wg_id) {
      unsigned offset = program.base_workgroup_constant_offset_in_dwords[0];
      for (unsigned u = 0; u < PVR_WORKGROUP_DIMENSIONS; ++u) {
         data_buffer[offset + u] = 0;
      }
   }

   if (uses_num_wgs) {
      unsigned offset = program.num_workgroups_constant_offset_in_dwords[0];
      for (unsigned u = 0; u < PVR_WORKGROUP_DIMENSIONS; ++u) {
         data_buffer[offset + u] = 0;
      }

      offset = program.num_workgroups_indirect_src;
      data_buffer[offset] = 0;
      data_buffer[offset + 1] = 0;
   }

   /* FIXME: Figure out the define for alignment of 16. */
   result = pvr_gpu_upload_pds(device,
                               data_buffer,
                               program.data_size,
                               16,
                               code_buffer,
                               program.code_size,
                               16,
                               16,
                               &compute_pipeline->pds_cs_program);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, allocator, code_buffer);
      vk_free2(&device->vk.alloc, allocator, data_buffer);
      return result;
   }

   compute_pipeline->pds_cs_data_section = data_buffer;

   /* The base workgroup and num workgroups can be patched in the
    * PDS data section before dispatch so we save their offsets.
    */
   compute_pipeline->base_workgroup_data_patching_offset = ~0u;
   if (uses_wg_id) {
      compute_pipeline->base_workgroup_data_patching_offset =
         program.base_workgroup_constant_offset_in_dwords[0];
   }

   compute_pipeline->num_workgroups_data_patching_offset = ~0u;
   compute_pipeline->num_workgroups_indirect_src_patching_offset = ~0u;
   compute_pipeline->num_workgroups_indirect_src_dma_patching_offset = ~0u;
   if (uses_num_wgs) {
      compute_pipeline->num_workgroups_data_patching_offset =
         program.num_workgroups_constant_offset_in_dwords[0];

      compute_pipeline->num_workgroups_indirect_src_patching_offset =
         program.num_workgroups_indirect_src;

      compute_pipeline->num_workgroups_indirect_src_dma_patching_offset =
         program.num_workgroups_indirect_src_dma;
   }

   compute_pipeline->pds_cs_program_info = (struct pvr_pds_info){
      .temps_required = program.highest_temp,
      .code_size_in_dwords = program.code_size,
      .data_size_in_dwords = program.data_size,
   };

   vk_free2(&device->vk.alloc, allocator, code_buffer);

   return VK_SUCCESS;
}

static void
pvr_pds_compute_program_destroy(struct pvr_device *device,
                                const VkAllocationCallbacks *const allocator,
                                struct pvr_pds_upload *const pds_cs_program,
                                uint32_t *pds_cs_data_section)
{
   pvr_bo_suballoc_free(pds_cs_program->pvr_bo);
   vk_free2(&device->vk.alloc, allocator, pds_cs_data_section);
}

/******************************************************************************
   Generic pipeline functions
 ******************************************************************************/

static void pvr_pipeline_init(struct pvr_device *device,
                              enum pvr_pipeline_type type,
                              const VkPipelineLayout layout,
                              struct pvr_pipeline *const pipeline)
{
   vk_object_base_init(&device->vk, &pipeline->base, VK_OBJECT_TYPE_PIPELINE);

   pipeline->type = type;

   assert(!pipeline->layout);
   pipeline->layout = vk_pipeline_layout_from_handle(layout);
   vk_pipeline_layout_ref(pipeline->layout);
}

static void pvr_pipeline_finish(struct pvr_device *device,
                                struct pvr_pipeline *pipeline)
{
   vk_pipeline_layout_unref(&device->vk, pipeline->layout);
   vk_object_base_finish(&pipeline->base);
}

static void pvr_early_init_shader_data(pco_data *data,
                                       nir_shader *nir,
                                       const void *pCreateInfo);

static void
pvr_preprocess_shader_data(pco_data *data,
                           nir_shader *nir,
                           const void *pCreateInfo,
                           struct vk_pipeline_layout *layout,
                           const struct vk_graphics_pipeline_state *state);

static void pvr_postprocess_shader_data(pco_data *data,
                                        nir_shader *nir,
                                        const void *pCreateInfo,
                                        struct vk_pipeline_layout *layout);

/******************************************************************************
   Compute pipeline functions
 ******************************************************************************/

static void
pvr_compute_state_save(struct pvr_compute_pipeline *compute_pipeline,
                       pco_shader *cs)
{
   const pco_data *shader_data = pco_shader_data(cs);
   memcpy(&compute_pipeline->cs_data, shader_data, sizeof(*shader_data));
}

/* Compiles and uploads shaders and PDS programs. */
static VkResult pvr_compute_pipeline_compile(
   struct pvr_device *const device,
   struct vk_pipeline_cache *cache,
   const VkComputePipelineCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *const allocator,
   struct pvr_compute_pipeline *const compute_pipeline)
{
   struct vk_pipeline_layout *layout = compute_pipeline->base.layout;
   const uint32_t cache_line_size =
      pvr_get_slc_cache_line_size(&device->pdevice->dev_info);
   pco_ctx *pco_ctx = device->pdevice->pco_ctx;
   void *shader_mem_ctx = ralloc_context(NULL);
   pco_data shader_data = { 0 };
   nir_shader *nir;
   pco_shader *cs;

   struct pvr_compute_shader_state *compute_state =
      &compute_pipeline->shader_state;

   VkResult result;

   result =
      vk_pipeline_shader_stage_to_nir(&device->vk,
                                      compute_pipeline->base.pipeline_flags,
                                      &pCreateInfo->stage,
                                      pco_spirv_options(),
                                      pco_nir_options(),
                                      shader_mem_ctx,
                                      &nir);
   if (result != VK_SUCCESS)
      goto err_free_build_context;

   pvr_early_init_shader_data(&shader_data, nir, pCreateInfo);
   pco_preprocess_nir(pco_ctx, nir);
   pvr_preprocess_shader_data(&shader_data, nir, pCreateInfo, layout, NULL);
   pco_lower_nir(pco_ctx, nir, &shader_data);
   pco_postprocess_nir(pco_ctx, nir, &shader_data);
   pvr_postprocess_shader_data(&shader_data, nir, pCreateInfo, layout);

   cs = pco_trans_nir(pco_ctx, nir, &shader_data, shader_mem_ctx);
   if (!cs) {
      result = VK_ERROR_INITIALIZATION_FAILED;
      goto err_free_build_context;
   }

   pco_process_ir(pco_ctx, cs);
   pco_encode_ir(pco_ctx, cs);

   pvr_compute_state_save(compute_pipeline, cs);

   result = pvr_gpu_upload_usc(device,
                               pco_shader_binary_data(cs),
                               pco_shader_binary_size(cs),
                               cache_line_size,
                               &compute_pipeline->shader_state.shader_bo);
   if (result != VK_SUCCESS)
      goto err_free_build_context;

   if (compute_pipeline->cs_data.cs.zero_shmem) {
      uint32_t start = compute_pipeline->cs_data.cs.shmem.start;
      uint32_t count = compute_pipeline->cs_data.cs.shmem.count;
      pco_shader *zero_init_shader =
         pvr_usc_zero_init_wg_mem(pco_ctx, start, count);

      compute_state->coeff_update_shader_temps =
         pco_shader_data(zero_init_shader)->common.temps;

      result = pvr_gpu_upload_usc(device,
                                  pco_shader_binary_data(zero_init_shader),
                                  pco_shader_binary_size(zero_init_shader),
                                  cache_line_size,
                                  &compute_state->coeff_update_shader_bo);
      ralloc_free(zero_init_shader);

      if (result != VK_SUCCESS)
         goto err_free_shader;
   }

   result = pvr_pds_descriptor_program_create_and_upload(
      device,
      allocator,
      layout,
      MESA_SHADER_COMPUTE,
      &compute_pipeline->cs_data,
      &compute_pipeline->descriptor_state);
   if (result != VK_SUCCESS)
      goto err_free_coeff_update_shader;

   result = pvr_pds_compute_program_create_and_upload(device,
                                                      allocator,
                                                      compute_state,
                                                      compute_pipeline);
   if (result != VK_SUCCESS)
      goto err_free_descriptor_program;

   ralloc_free(shader_mem_ctx);

   return VK_SUCCESS;

err_free_coeff_update_shader:
   pvr_bo_suballoc_free(compute_pipeline->shader_state.coeff_update_shader_bo);

err_free_descriptor_program:
   pvr_pds_descriptor_program_destroy(device,
                                      allocator,
                                      &compute_pipeline->descriptor_state);

err_free_shader:
   pvr_bo_suballoc_free(compute_pipeline->shader_state.shader_bo);

err_free_build_context:
   ralloc_free(shader_mem_ctx);
   return result;
}

static VkResult
pvr_compute_pipeline_init(struct pvr_device *device,
                          struct vk_pipeline_cache *cache,
                          const VkComputePipelineCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *allocator,
                          struct pvr_compute_pipeline *compute_pipeline)
{
   VkResult result;

   pvr_pipeline_init(device,
                     PVR_PIPELINE_TYPE_COMPUTE,
                     pCreateInfo->layout,
                     &compute_pipeline->base);

   result = pvr_compute_pipeline_compile(device,
                                         cache,
                                         pCreateInfo,
                                         allocator,
                                         compute_pipeline);
   if (result != VK_SUCCESS) {
      pvr_pipeline_finish(device, &compute_pipeline->base);
      return result;
   }

   return VK_SUCCESS;
}

static VkResult
pvr_compute_pipeline_create(struct pvr_device *device,
                            struct vk_pipeline_cache *cache,
                            const VkComputePipelineCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *allocator,
                            VkPipeline *const pipeline_out)
{
   struct pvr_compute_pipeline *compute_pipeline;
   VkResult result;

   compute_pipeline = vk_zalloc2(&device->vk.alloc,
                                 allocator,
                                 sizeof(*compute_pipeline),
                                 8,
                                 VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!compute_pipeline)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* Compiles and uploads shaders and PDS programs. */
   result = pvr_compute_pipeline_init(device,
                                      cache,
                                      pCreateInfo,
                                      allocator,
                                      compute_pipeline);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, allocator, compute_pipeline);
      return result;
   }

   *pipeline_out = pvr_pipeline_to_handle(&compute_pipeline->base);

   return VK_SUCCESS;
}

static void pvr_pipeline_destroy_shader_data(pco_data *data);

static void pvr_compute_pipeline_destroy(
   struct pvr_device *const device,
   const VkAllocationCallbacks *const allocator,
   struct pvr_compute_pipeline *const compute_pipeline)
{
   pvr_pds_compute_program_destroy(device,
                                   allocator,
                                   &compute_pipeline->pds_cs_program,
                                   compute_pipeline->pds_cs_data_section);
   pvr_pds_descriptor_program_destroy(device,
                                      allocator,
                                      &compute_pipeline->descriptor_state);
   pvr_bo_suballoc_free(compute_pipeline->shader_state.coeff_update_shader_bo);
   pvr_bo_suballoc_free(compute_pipeline->shader_state.shader_bo);

   pvr_pipeline_destroy_shader_data(&compute_pipeline->cs_data);

   pvr_pipeline_finish(device, &compute_pipeline->base);

   vk_free2(&device->vk.alloc, allocator, compute_pipeline);
}

VkResult
pvr_CreateComputePipelines(VkDevice _device,
                           VkPipelineCache pipelineCache,
                           uint32_t createInfoCount,
                           const VkComputePipelineCreateInfo *pCreateInfos,
                           const VkAllocationCallbacks *pAllocator,
                           VkPipeline *pPipelines)
{
   VK_FROM_HANDLE(vk_pipeline_cache, cache, pipelineCache);
   VK_FROM_HANDLE(pvr_device, device, _device);
   VkResult result = VK_SUCCESS;

   for (uint32_t i = 0; i < createInfoCount; i++) {
      const VkResult local_result =
         pvr_compute_pipeline_create(device,
                                     cache,
                                     &pCreateInfos[i],
                                     pAllocator,
                                     &pPipelines[i]);
      if (local_result != VK_SUCCESS) {
         result = local_result;
         pPipelines[i] = VK_NULL_HANDLE;
      }
   }

   return result;
}

/******************************************************************************
   Graphics pipeline functions
 ******************************************************************************/

static void pvr_pipeline_destroy_shader_data(pco_data *data)
{
   for (unsigned u = 0; u < ARRAY_SIZE(data->common.desc_sets); ++u) {
      ralloc_free(data->common.desc_sets[u].bindings);
   }
}

static void
pvr_graphics_pipeline_destroy(struct pvr_device *const device,
                              const VkAllocationCallbacks *const allocator,
                              struct pvr_graphics_pipeline *const gfx_pipeline)
{
   const uint32_t num_vertex_attrib_programs =
      ARRAY_SIZE(gfx_pipeline->shader_state.vertex.pds_attrib_programs);
   struct pvr_fragment_shader_state *fragment_state =
      &gfx_pipeline->shader_state.fragment;

   pvr_pds_descriptor_program_destroy(
      device,
      allocator,
      &gfx_pipeline->shader_state.fragment.descriptor_state);

   pvr_pds_descriptor_program_destroy(
      device,
      allocator,
      &gfx_pipeline->shader_state.vertex.descriptor_state);

   for (uint32_t i = 0; i < num_vertex_attrib_programs; i++) {
      struct pvr_pds_attrib_program *const attrib_program =
         &gfx_pipeline->shader_state.vertex.pds_attrib_programs[i];

      pvr_pds_vertex_attrib_program_destroy(device, allocator, attrib_program);
   }

   vk_free2(&device->vk.alloc,
            allocator,
            fragment_state->pds_fragment_program_buffer);
   vk_free2(&device->vk.alloc,
            allocator,
            fragment_state->pds_coeff_program_buffer);

   pvr_bo_suballoc_free(gfx_pipeline->shader_state.fragment.shader_bo);
   pvr_bo_suballoc_free(gfx_pipeline->shader_state.vertex.shader_bo);

   pvr_pipeline_finish(device, &gfx_pipeline->base);

   pvr_pipeline_destroy_shader_data(&gfx_pipeline->vs_data);
   pvr_pipeline_destroy_shader_data(&gfx_pipeline->fs_data);

   vk_free2(&device->vk.alloc, allocator, gfx_pipeline);
}

static void pvr_vertex_state_save(struct pvr_graphics_pipeline *gfx_pipeline,
                                  pco_shader *vs)
{
   struct pvr_vertex_shader_state *vertex_state =
      &gfx_pipeline->shader_state.vertex;

   const pco_data *shader_data = pco_shader_data(vs);
   memcpy(&gfx_pipeline->vs_data, shader_data, sizeof(*shader_data));

   /* This ends up unused since we'll use the temp_usage for the PDS program we
    * end up selecting, and the descriptor PDS program doesn't use any temps.
    * Let's set it to ~0 in case it ever gets used.
    */
   vertex_state->stage_state.pds_temps_count = ~0;
}

static void pvr_fragment_state_save(struct pvr_graphics_pipeline *gfx_pipeline,
                                    pco_shader *fs)
{
   struct pvr_fragment_shader_state *fragment_state =
      &gfx_pipeline->shader_state.fragment;

   const pco_data *shader_data = pco_shader_data(fs);
   memcpy(&gfx_pipeline->fs_data, shader_data, sizeof(*shader_data));

   /* TODO: add selection for other values of pass type and sample rate. */

   /* TODO: do this dynamically as well */
   if (shader_data->fs.uses.depth_feedback && !shader_data->fs.uses.early_frag)
      fragment_state->pass_type = ROGUE_TA_PASSTYPE_DEPTH_FEEDBACK;
   else if (shader_data->fs.uses.discard)
      fragment_state->pass_type = ROGUE_TA_PASSTYPE_PUNCH_THROUGH;
   else if (shader_data->fs.uses.fbfetch)
      fragment_state->pass_type = ROGUE_TA_PASSTYPE_TRANSLUCENT;
   else
      fragment_state->pass_type = ROGUE_TA_PASSTYPE_OPAQUE;

   /* We can't initialize it yet since we still need to generate the PDS
    * programs so set it to `~0` to make sure that we set this up later on.
    */
   fragment_state->stage_state.pds_temps_count = ~0;
}

static void pvr_graphics_pipeline_setup_vertex_dma(
   struct pvr_graphics_pipeline *gfx_pipeline,
   const VkPipelineVertexInputStateCreateInfo *const vertex_input_state,
   const struct vk_vertex_input_state *vi,
   struct pvr_pds_vertex_dma *const dma_descriptions,
   uint32_t *const dma_count)
{
   pco_vs_data *vs_data = &gfx_pipeline->vs_data.vs;

   const VkVertexInputBindingDescription
      *sorted_bindings[PVR_MAX_VERTEX_INPUT_BINDINGS] = { 0 };

   /* Vertex attributes map to the `layout(location = x)` annotation in the
    * shader where `x` is the attribute's location.
    * Vertex bindings have NO relation to the shader. They have nothing to do
    * with the `layout(set = x, binding = y)` notation. They instead indicate
    * where the data for a collection of vertex attributes comes from. The
    * application binds a VkBuffer with vkCmdBindVertexBuffers() to a specific
    * binding number and based on that we'll know which buffer to DMA the data
    * from, to fill in the collection of vertex attributes.
    */

   for (uint32_t i = 0; i < vertex_input_state->vertexBindingDescriptionCount;
        i++) {
      const VkVertexInputBindingDescription *binding_desc =
         &vertex_input_state->pVertexBindingDescriptions[i];

      sorted_bindings[binding_desc->binding] = binding_desc;
   }

   for (uint32_t i = 0; i < vertex_input_state->vertexAttributeDescriptionCount;
        i++) {
      const VkVertexInputAttributeDescription *attribute =
         &vertex_input_state->pVertexAttributeDescriptions[i];

      gl_vert_attrib location = attribute->location + VERT_ATTRIB_GENERIC0;
      const VkVertexInputBindingDescription *binding =
         sorted_bindings[attribute->binding];
      struct pvr_pds_vertex_dma *dma_desc = &dma_descriptions[*dma_count];
      const struct util_format_description *fmt_description =
         vk_format_description(attribute->format);

      const pco_range *attrib_range = &vs_data->attribs[location];

      /* Skip unused attributes. */
      if (!attrib_range->count)
         continue;

      /* DMA setup. */

      /* The PDS program sets up DDMADs to DMA attributes into vtxin regs.
       *
       * DDMAD -> Multiply, add, and DOUTD (i.e. DMA from that address).
       *          DMA source addr = src0 * src1 + src2
       *          DMA params = src3
       *
       * In the PDS program we setup src0 with the binding's stride and src1
       * with either the instance id or vertex id (both of which get filled by
       * the hardware). We setup src2 later on once we know which VkBuffer to
       * DMA the data from so it's saved for later when we patch the data
       * section.
       */

      /* TODO: Right now we're setting up a DMA per attribute. In a case where
       * there are multiple attributes packed into a single binding with
       * adjacent locations we'd still be DMAing them separately. This is not
       * great so the DMA setup should be smarter and could do with some
       * optimization.
       */

      *dma_desc = (struct pvr_pds_vertex_dma){ 0 };

      /* In relation to the Vulkan spec. 22.4. Vertex Input Address Calculation
       * this corresponds to `attribDesc.offset`.
       * The PDS program doesn't do anything with it but just save it in the
       * PDS program entry.
       */
      dma_desc->offset = attribute->offset;

      /* In relation to the Vulkan spec. 22.4. Vertex Input Address Calculation
       * this corresponds to `bindingDesc.stride`.
       * The PDS program will calculate the `effectiveVertexOffset` with this
       * and add it to the address provided in the patched data segment.
       */
      dma_desc->stride = binding->stride;

      dma_desc->flags = 0;
      if (binding->inputRate == VK_VERTEX_INPUT_RATE_INSTANCE)
         dma_desc->flags |= PVR_PDS_VERTEX_DMA_FLAGS_INSTANCE_RATE;

      /* Size to DMA per vertex attribute. Used to setup src3 in the DDMAD. */
      dma_desc->size_in_dwords = attrib_range->count;

      /* Vtxin reg offset to start DMAing into. */
      dma_desc->destination = attrib_range->start;

      /* Will be used by the driver to figure out buffer address to patch in the
       * data section. I.e. which binding we should DMA from.
       */
      dma_desc->binding_index = attribute->binding;

      dma_desc->divisor = 1;
      if (binding->inputRate == VK_VERTEX_INPUT_RATE_INSTANCE)
         dma_desc->divisor = vi->bindings[attribute->binding].divisor;

      /* Will be used to generate PDS code that takes care of robust buffer
       * access, and later on by the driver to write the correct robustness
       * buffer address to DMA the fallback values from.
       */
      dma_desc->robustness_buffer_offset =
         pvr_get_robustness_buffer_format_offset(attribute->format);

      /* Used by later on by the driver to figure out if the buffer is being
       * accessed out of bounds, for robust buffer access.
       */
      dma_desc->component_size_in_bytes =
         fmt_description->block.bits / fmt_description->nr_channels / 8;

      ++*dma_count;
   }
}

static void pvr_graphics_pipeline_setup_fragment_coeff_program(
   struct pvr_graphics_pipeline *gfx_pipeline,
   nir_shader *fs)
{
   struct pvr_fragment_shader_state *fragment_state =
      &gfx_pipeline->shader_state.fragment;
   struct pvr_pds_coeff_loading_program *frag_coeff_program =
      &fragment_state->pds_coeff_program;

   uint64_t varyings_used = fs->info.inputs_read &
                            BITFIELD64_RANGE(VARYING_SLOT_VAR0, MAX_VARYING);
   pco_vs_data *vs_data = &gfx_pipeline->vs_data.vs;
   pco_fs_data *fs_data = &gfx_pipeline->fs_data.fs;

   unsigned fpu = 0;
   unsigned dest = 0;

   if (fs_data->uses.z) {
      pvr_csb_pack (&frag_coeff_program->FPU_iterators[fpu],
                    PDSINST_DOUT_FIELDS_DOUTI_SRC,
                    douti_src) {
         /* TODO: define instead of sizeof(uint16_t). */
         douti_src.f32_offset = fs_data->uses.w ? 1 * sizeof(uint16_t) : 0;
         douti_src.f16_offset = douti_src.f32_offset;
         douti_src.shademodel = ROGUE_PDSINST_DOUTI_SHADEMODEL_GOURUAD;
         douti_src.size = ROGUE_PDSINST_DOUTI_SIZE_1D;
      }

      frag_coeff_program->z_iterator = fpu;
      frag_coeff_program->destination[fpu++] = dest++;
   } else {
      frag_coeff_program->z_iterator = ~0u;
   }

   if (fs_data->uses.w) {
      pvr_csb_pack (&frag_coeff_program->FPU_iterators[fpu],
                    PDSINST_DOUT_FIELDS_DOUTI_SRC,
                    douti_src) {
         douti_src.f32_offset = 0;
         douti_src.f16_offset = douti_src.f32_offset;
         douti_src.shademodel = ROGUE_PDSINST_DOUTI_SHADEMODEL_GOURUAD;
         douti_src.size = ROGUE_PDSINST_DOUTI_SIZE_1D;
      }

      frag_coeff_program->destination[fpu++] = dest++;
   }

   if (fs_data->uses.pntc) {
      pvr_csb_pack (&frag_coeff_program->FPU_iterators[fpu],
                    PDSINST_DOUT_FIELDS_DOUTI_SRC,
                    douti_src) {
         douti_src.shademodel = ROGUE_PDSINST_DOUTI_SHADEMODEL_GOURUAD;
         douti_src.size = ROGUE_PDSINST_DOUTI_SIZE_2D;
         douti_src.pointsprite = true;
      }

      frag_coeff_program->destination[fpu++] = dest;
      dest += 2;
   }

   u_foreach_bit64 (varying, varyings_used) {
      nir_variable *var =
         nir_find_variable_with_location(fs, nir_var_shader_in, varying);
      assert(var);

      pco_range *cf_range = &fs_data->varyings[varying];
      assert(cf_range->count > 0);
      assert(!(cf_range->start % ROGUE_USC_COEFFICIENT_SET_SIZE));
      assert(!(cf_range->count % ROGUE_USC_COEFFICIENT_SET_SIZE));

      pco_range *vtxout_range = &vs_data->varyings[varying];
      assert(vtxout_range->count > 0);
      assert(vtxout_range->start >= 4);

      assert(vtxout_range->count ==
             cf_range->count / ROGUE_USC_COEFFICIENT_SET_SIZE);

      unsigned count = vtxout_range->count;

      unsigned vtxout = vtxout_range->start;

      /* pos.x, pos.y unused. */
      vtxout -= 2;

      /* pos.z unused. */
      if (!fs_data->uses.z)
         vtxout -= 1;

      /* pos.w unused. */
      if (!fs_data->uses.w)
         vtxout -= 1;

      pvr_csb_pack (&frag_coeff_program->FPU_iterators[fpu],
                    PDSINST_DOUT_FIELDS_DOUTI_SRC,
                    douti_src) {
         /* TODO: define instead of sizeof(uint16_t). */
         douti_src.f32_offset = vtxout * sizeof(uint16_t);
         /* TODO: f16 support. */
         douti_src.f16 = false;
         douti_src.f16_offset = douti_src.f32_offset;

         switch (var->data.interpolation) {
         case INTERP_MODE_SMOOTH:
            douti_src.perspective = true;
            FALLTHROUGH;

         case INTERP_MODE_NOPERSPECTIVE:
            douti_src.shademodel = ROGUE_PDSINST_DOUTI_SHADEMODEL_GOURUAD;
            break;

         case INTERP_MODE_FLAT:
            /* Shademodel will be set up later for flat. */
            BITSET_SET(frag_coeff_program->flat_iter_mask, fpu);
            break;

         default:
            UNREACHABLE("Unimplemented interpolation type.");
         }

         douti_src.size = ROGUE_PDSINST_DOUTI_SIZE_1D + count - 1;
      }

      frag_coeff_program->destination[fpu++] =
         cf_range->start / ROGUE_USC_COEFFICIENT_SET_SIZE;
   }

   frag_coeff_program->num_fpu_iterators = fpu;
}

static void set_var(pco_range *allocation_list,
                    unsigned to,
                    nir_variable *var,
                    unsigned dwords_each)
{
   unsigned slots = glsl_count_dword_slots(var->type, false);

   allocation_list[var->data.location] = (pco_range){
      .start = to,
      .count = slots * dwords_each,
   };
}

static void allocate_var(pco_range *allocation_list,
                         unsigned *counter,
                         nir_variable *var,
                         unsigned dwords_each)
{
   unsigned slots = glsl_count_dword_slots(var->type, false);

   allocation_list[var->data.location] = (pco_range){
      .start = *counter,
      .count = slots * dwords_each,
   };

   *counter += slots * dwords_each;
}

static void try_allocate_var(pco_range *allocation_list,
                             unsigned *counter,
                             nir_shader *nir,
                             uint64_t bitset,
                             nir_variable_mode mode,
                             int location,
                             unsigned dwords_each)
{
   if (!(bitset & BITFIELD64_BIT(location)))
      return;

   nir_variable *var = nir_find_variable_with_location(nir, mode, location);
   assert(var);

   allocate_var(allocation_list, counter, var, dwords_each);
}

static void allocate_val(pco_range *allocation_list,
                         unsigned *counter,
                         unsigned location,
                         unsigned dwords_each)
{
   allocation_list[location] = (pco_range){
      .start = *counter,
      .count = dwords_each,
   };

   *counter += dwords_each;
}

static void try_allocate_vars(pco_range *allocation_list,
                              unsigned *counter,
                              nir_shader *nir,
                              uint64_t *bitset,
                              nir_variable_mode mode,
                              bool any,
                              bool f16,
                              enum glsl_interp_mode interp_mode,
                              unsigned dwords_each)
{
   uint64_t skipped = 0;

   while (*bitset) {
      int location = u_bit_scan64(bitset);
      unsigned accum = 0;

      nir_foreach_variable_with_modes (var, nir, mode) {
         if (var->data.location != location)
            continue;

         if (!any && (glsl_type_is_16bit(
                         glsl_without_array_or_matrix(var->type)) != f16 ||
                      var->data.interpolation != interp_mode)) {
            skipped |= BITFIELD64_BIT(location);
            break;
         }

         accum += glsl_count_dword_slots(var->type, false);
      }

      if (skipped & BITFIELD64_BIT(location))
         continue;

      accum *= dwords_each;
      allocate_val(allocation_list, counter, location, accum);
   }

   *bitset |= skipped;
}

static void check_unused_sysvals(
   BITSET_WORD system_values_read[static BITSET_WORDS(SYSTEM_VALUE_MAX)])
{
#if MESA_DEBUG
   if (!__bitset_is_empty(system_values_read, BITSET_WORDS(SYSTEM_VALUE_MAX))) {
      gl_system_value sysval;
      BITSET_FOREACH_SET (sysval, system_values_read, SYSTEM_VALUE_MAX) {
         debug_printf("unhandled sysval: %s\n", gl_system_value_name(sysval));
      }
   }
#endif
}

static void pvr_alloc_vs_sysvals(pco_data *data, nir_shader *nir)
{
   BITSET_DECLARE(system_values_read, SYSTEM_VALUE_MAX);
   BITSET_COPY(system_values_read, nir->info.system_values_read);

   gl_system_value sys_vals[] = {
      SYSTEM_VALUE_VERTEX_ID,     SYSTEM_VALUE_INSTANCE_ID,
      SYSTEM_VALUE_BASE_INSTANCE, SYSTEM_VALUE_BASE_VERTEX,
      SYSTEM_VALUE_DRAW_ID,       SYSTEM_VALUE_VIEW_INDEX,
   };

   for (unsigned u = 0; u < ARRAY_SIZE(sys_vals); ++u) {
      if (BITSET_TEST(system_values_read, sys_vals[u])) {
         nir_intrinsic_op op = nir_intrinsic_from_system_value(sys_vals[u]);
         unsigned dwords = nir_intrinsic_infos[op].dest_components;
         assert(dwords > 0);

         allocate_val(data->common.sys_vals,
                      &data->common.vtxins,
                      sys_vals[u],
                      dwords);

         BITSET_CLEAR(system_values_read, sys_vals[u]);
      }
   }

   check_unused_sysvals(system_values_read);
   assert(BITSET_IS_EMPTY(system_values_read));
}

static void pvr_init_vs_attribs(
   pco_data *data,
   const VkPipelineVertexInputStateCreateInfo *const vertex_input_state)
{
   for (unsigned u = 0; u < vertex_input_state->vertexAttributeDescriptionCount;
        ++u) {
      const VkVertexInputAttributeDescription *attrib =
         &vertex_input_state->pVertexAttributeDescriptions[u];

      gl_vert_attrib location = attrib->location + VERT_ATTRIB_GENERIC0;

      data->vs.attrib_formats[location] =
         vk_format_to_pipe_format(attrib->format);
   }
}

static void pvr_alloc_vs_attribs(pco_data *data, nir_shader *nir)
{
   nir_foreach_shader_in_variable (var, nir) {
      allocate_var(data->vs.attribs, &data->common.vtxins, var, 1);
   }
}

static void pvr_alloc_vs_varyings(pco_data *data, nir_shader *nir)
{
   uint64_t vars_mask = nir->info.outputs_written &
                        BITFIELD64_RANGE(VARYING_SLOT_VAR0, MAX_VARYING);

   /* Varying ordering is specific. */
   try_allocate_var(data->vs.varyings,
                    &data->vs.vtxouts,
                    nir,
                    nir->info.outputs_written,
                    nir_var_shader_out,
                    VARYING_SLOT_POS,
                    1);

   /* Save varying counts. */
   nir_foreach_variable_with_modes (var, nir, nir_var_shader_out) {
      if (var->data.location < VARYING_SLOT_VAR0 ||
          var->data.location >= VARYING_SLOT_VAR0 + MAX_VARYING) {
         continue;
      }

      /* TODO: f16 support. */
      bool f16 = glsl_type_is_16bit(glsl_without_array_or_matrix(var->type));
      assert(!f16);
      unsigned components = glsl_get_components(var->type);

      switch (var->data.interpolation) {
      case INTERP_MODE_NONE:
         /* pco_rev_link_nir didn't run; override here. */
         var->data.interpolation = INTERP_MODE_SMOOTH;
         FALLTHROUGH;

      case INTERP_MODE_SMOOTH:
         if (f16)
            data->vs.f16_smooth += components;
         else
            data->vs.f32_smooth += components;

         break;

      case INTERP_MODE_FLAT:
         if (f16)
            data->vs.f16_flat += components;
         else
            data->vs.f32_flat += components;

         break;

      case INTERP_MODE_NOPERSPECTIVE:
         if (f16)
            data->vs.f16_npc += components;
         else
            data->vs.f32_npc += components;

         break;

      default:
         UNREACHABLE("");
      }
   }

   for (unsigned f16 = 0; f16 <= 1; ++f16) {
      for (enum glsl_interp_mode interp_mode = INTERP_MODE_SMOOTH;
           interp_mode <= INTERP_MODE_NOPERSPECTIVE;
           ++interp_mode) {
         try_allocate_vars(data->vs.varyings,
                           &data->vs.vtxouts,
                           nir,
                           &vars_mask,
                           nir_var_shader_out,
                           false,
                           f16,
                           interp_mode,
                           1);
      }
   }

   assert(!vars_mask);

   const gl_varying_slot last_slots[] = {
      VARYING_SLOT_PSIZ,       VARYING_SLOT_VIEWPORT,   VARYING_SLOT_LAYER,
      VARYING_SLOT_CLIP_DIST0, VARYING_SLOT_CLIP_DIST1,
   };

   for (unsigned u = 0; u < ARRAY_SIZE(last_slots); ++u) {
      try_allocate_var(data->vs.varyings,
                       &data->vs.vtxouts,
                       nir,
                       nir->info.outputs_written,
                       nir_var_shader_out,
                       last_slots[u],
                       1);
   }
}

static void pvr_alloc_fs_sysvals(pco_data *data, nir_shader *nir)
{
   BITSET_DECLARE(system_values_read, SYSTEM_VALUE_MAX);
   BITSET_COPY(system_values_read, nir->info.system_values_read);

   gl_system_value sys_vals[] = {
      SYSTEM_VALUE_FRONT_FACE,
   };

   for (unsigned u = 0; u < ARRAY_SIZE(sys_vals); ++u) {
      if (BITSET_TEST(system_values_read, sys_vals[u])) {
         nir_intrinsic_op op = nir_intrinsic_from_system_value(sys_vals[u]);
         unsigned dwords = nir_intrinsic_infos[op].dest_components;
         assert(dwords > 0);

         allocate_val(data->common.sys_vals,
                      &data->common.shareds,
                      sys_vals[u],
                      dwords);

         BITSET_CLEAR(system_values_read, sys_vals[u]);
      }
   }

   /* Clear built-in sysvals. */
   gl_system_value builtin_sys_vals[] = {
      SYSTEM_VALUE_SAMPLE_ID,
      SYSTEM_VALUE_LAYER_ID,
      SYSTEM_VALUE_SAMPLE_POS,
      SYSTEM_VALUE_BARYCENTRIC_LINEAR_CENTROID,
      SYSTEM_VALUE_BARYCENTRIC_LINEAR_COORD,
      SYSTEM_VALUE_BARYCENTRIC_LINEAR_PIXEL,
      SYSTEM_VALUE_BARYCENTRIC_LINEAR_SAMPLE,
      SYSTEM_VALUE_BARYCENTRIC_PERSP_CENTROID,
      SYSTEM_VALUE_BARYCENTRIC_PERSP_COORD,
      SYSTEM_VALUE_BARYCENTRIC_PERSP_PIXEL,
      SYSTEM_VALUE_BARYCENTRIC_PERSP_SAMPLE,
      SYSTEM_VALUE_BARYCENTRIC_PULL_MODEL,
   };

   for (unsigned u = 0; u < ARRAY_SIZE(builtin_sys_vals); ++u)
      BITSET_CLEAR(system_values_read, builtin_sys_vals[u]);

   check_unused_sysvals(system_values_read);
   assert(BITSET_IS_EMPTY(system_values_read));

   data->fs.meta = (pco_range){
      .start = data->common.shareds,
      .count = 1,
   };

   ++data->common.shareds;
}

static void pvr_alloc_fs_varyings(pco_data *data, nir_shader *nir)
{
   uint64_t vars_mask = nir->info.inputs_read &
                        BITFIELD64_RANGE(VARYING_SLOT_VAR0, MAX_VARYING);

   assert(!data->common.coeffs);

   /* Save the z/w locations. */
   unsigned zw_count = !!data->fs.uses.z + !!data->fs.uses.w;
   allocate_val(data->fs.varyings,
                &data->common.coeffs,
                VARYING_SLOT_POS,
                zw_count * ROGUE_USC_COEFFICIENT_SET_SIZE);

   /* If point coords are used, they come after z/w (if present). */
   nir_variable *var = nir_find_variable_with_location(nir,
                                                       nir_var_shader_in,
                                                       VARYING_SLOT_PNTC);
   if (var) {
      assert(!var->data.location_frac);
      unsigned count = glsl_get_components(var->type);
      assert(count == 2);

      allocate_var(data->fs.varyings,
                   &data->common.coeffs,
                   var,
                   ROGUE_USC_COEFFICIENT_SET_SIZE);

      data->fs.uses.pntc = true;
   }

   /* Allocate the rest of the input varyings. */
   try_allocate_vars(data->fs.varyings,
                     &data->common.coeffs,
                     nir,
                     &vars_mask,
                     nir_var_shader_in,
                     true,
                     false,
                     0,
                     ROGUE_USC_COEFFICIENT_SET_SIZE);
}

static void
pvr_init_fs_outputs(pco_data *data,
                    const struct pvr_render_pass *pass,
                    const struct pvr_render_subpass *const subpass,
                    const struct pvr_renderpass_hwsetup_subpass *hw_subpass)
{
   unsigned u;
   pco_fs_data *fs = &data->fs;

   for (u = 0; u < subpass->color_count; ++u) {
      unsigned idx = subpass->color_attachments[u];
      const struct usc_mrt_resource *mrt_resource;
      bool tile_buffer;

      if (idx == VK_ATTACHMENT_UNUSED)
         continue;

      gl_frag_result location = FRAG_RESULT_DATA0 + u;
      VkFormat vk_format = pass->attachments[idx].vk_format;
      fs->output_formats[location] = vk_format_to_pipe_format(vk_format);

      mrt_resource = &hw_subpass->setup.mrt_resources[u];
      tile_buffer = mrt_resource->type != USC_MRT_RESOURCE_TYPE_OUTPUT_REG;

      if (tile_buffer) {
         fs->num_tile_buffers =
            MAX2(fs->num_tile_buffers, mrt_resource->mem.tile_buffer + 1);
         fs->output_tile_buffers |= BITFIELD_BIT(u);
      }
   }

   fs->z_replicate = ~0u;
   if (hw_subpass->z_replicate >= 0) {
      gl_frag_result location = FRAG_RESULT_DATA0 + u;
      const struct usc_mrt_resource *mrt_resource =
         &hw_subpass->setup.mrt_resources[u];
      bool tile_buffer = mrt_resource->type != USC_MRT_RESOURCE_TYPE_OUTPUT_REG;

      fs->output_formats[location] = PIPE_FORMAT_R32_FLOAT;
      fs->z_replicate = location;

      if (tile_buffer) {
         fs->num_tile_buffers =
            MAX2(fs->num_tile_buffers, mrt_resource->mem.tile_buffer + 1);
         fs->output_tile_buffers |= BITFIELD_BIT(u);
      }
   }
}

static void
pvr_setup_fs_outputs(pco_data *data,
                     nir_shader *nir,
                     const struct pvr_render_subpass *const subpass,
                     const struct pvr_renderpass_hwsetup_subpass *hw_subpass)
{
   uint64_t outputs_written = nir->info.outputs_written;
   pco_fs_data *fs = &data->fs;

   unsigned u;
   for (u = 0; u < subpass->color_count; ++u) {
      gl_frag_result location = FRAG_RESULT_DATA0 + u;
      unsigned idx = subpass->color_attachments[u];
      const struct usc_mrt_resource *mrt_resource;
      bool tile_buffer;
      nir_variable *var;

      if (idx == VK_ATTACHMENT_UNUSED)
         continue;

      var = nir_find_variable_with_location(nir, nir_var_shader_out, location);
      if (!var)
         continue;

      mrt_resource = &hw_subpass->setup.mrt_resources[u];
      tile_buffer = fs->output_tile_buffers & BITFIELD_BIT(u);

      set_var(fs->outputs,
              tile_buffer ? mrt_resource->mem.tile_buffer
                          : mrt_resource->reg.output_reg,
              var,
              DIV_ROUND_UP(mrt_resource->intermediate_size, sizeof(uint32_t)));

      if (tile_buffer)
         fs->outputs[location].offset = mrt_resource->mem.offset_dw;

      outputs_written &= ~BITFIELD64_BIT(location);
   }

   if (hw_subpass->z_replicate >= 0) {
      const struct usc_mrt_resource *mrt_resource =
         &hw_subpass->setup.mrt_resources[hw_subpass->z_replicate];
      gl_frag_result location = FRAG_RESULT_DATA0 + u;
      nir_variable *var =
         nir_find_variable_with_location(nir, nir_var_shader_out, location);
      if (var) {
         bool tile_buffer = fs->output_tile_buffers & BITFIELD_BIT(u);

         set_var(fs->outputs,
                 tile_buffer ? mrt_resource->mem.tile_buffer
                             : mrt_resource->reg.output_reg,
                 var,
                 DIV_ROUND_UP(mrt_resource->intermediate_size,
                              sizeof(uint32_t)));

         if (tile_buffer)
            fs->outputs[location].offset = mrt_resource->mem.offset_dw;

         outputs_written &= ~BITFIELD64_BIT(location);
      }
   }

   assert(!outputs_written);
}

static void pvr_init_fs_input_attachments(
   pco_data *data,
   const struct pvr_render_pass *pass,
   const struct pvr_render_subpass *const subpass,
   const struct pvr_renderpass_hwsetup_subpass *hw_subpass)
{
   pco_fs_data *fs = &data->fs;
   for (unsigned u = 0; u < subpass->input_count; ++u) {
      unsigned idx = subpass->input_attachments[u].attachment_idx;
      if (idx == VK_ATTACHMENT_UNUSED)
         continue;

      bool onchip = hw_subpass->input_access[u].type !=
                    PVR_RENDERPASS_HWSETUP_INPUT_ACCESS_OFFCHIP;
      if (!onchip)
         continue;

      VkFormat vk_format = pass->attachments[idx].vk_format;
      bool has_stencil = vk_format_has_stencil(vk_format);
      if (hw_subpass->input_access[u].type ==
          PVR_RENDERPASS_HWSETUP_INPUT_ACCESS_ONCHIP_ZREPLICATE) {
         vk_format = VK_FORMAT_R32_SFLOAT;
      }

      fs->ia_formats[u] = vk_format_to_pipe_format(vk_format);
      assert(fs->ia_formats[u] != PIPE_FORMAT_NONE);
      if (has_stencil)
         fs->ia_has_stencil |= BITFIELD_BIT(u);

      unsigned mrt_idx = hw_subpass->input_access[u].on_chip_rt;
      const struct usc_mrt_resource *mrt_resource =
         &hw_subpass->setup.mrt_resources[mrt_idx];

      bool tile_buffer = mrt_resource->type != USC_MRT_RESOURCE_TYPE_OUTPUT_REG;

      if (tile_buffer) {
         fs->num_tile_buffers =
            MAX2(fs->num_tile_buffers, mrt_resource->mem.tile_buffer + 1);
         fs->ia_tile_buffers |= BITFIELD_BIT(u);
      }
   }
}

static void pvr_init_fs_blend(pco_data *data,
                              const struct vk_color_blend_state *cb)
{
   nir_lower_blend_options *blend_opts = &data->fs.blend_opts;
   if (!cb)
      return;

   blend_opts->logicop_enable = cb->logic_op_enable;
   blend_opts->logicop_func = vk_logic_op_to_pipe(cb->logic_op);

   unsigned count = cb->attachment_count;
   for (unsigned u = 0; u < count; ++u) {
      const struct vk_color_blend_attachment_state *rt = &cb->attachments[u];
      gl_frag_result location = FRAG_RESULT_DATA0 + u;
      blend_opts->format[u] = data->fs.output_formats[location];

      if (cb->logic_op_enable) {
         /* No blending, but we get the colour mask below */
      } else if (!rt->blend_enable) {
         const nir_lower_blend_channel replace = {
            .func = PIPE_BLEND_ADD,
            .src_factor = PIPE_BLENDFACTOR_ONE,
            .dst_factor = PIPE_BLENDFACTOR_ZERO,
         };

         blend_opts->rt[u].rgb = replace;
         blend_opts->rt[u].alpha = replace;
      } else {
         blend_opts->rt[u].rgb.func = vk_blend_op_to_pipe(rt->color_blend_op);
         blend_opts->rt[u].rgb.src_factor =
            vk_blend_factor_to_pipe(rt->src_color_blend_factor);
         blend_opts->rt[u].rgb.dst_factor =
            vk_blend_factor_to_pipe(rt->dst_color_blend_factor);

         blend_opts->rt[u].alpha.func = vk_blend_op_to_pipe(rt->alpha_blend_op);
         blend_opts->rt[u].alpha.src_factor =
            vk_blend_factor_to_pipe(rt->src_alpha_blend_factor);
         blend_opts->rt[u].alpha.dst_factor =
            vk_blend_factor_to_pipe(rt->dst_alpha_blend_factor);
      }

      blend_opts->rt[u].colormask = rt->write_mask;
   }
}

static void pvr_setup_fs_input_attachments(
   pco_data *data,
   nir_shader *nir,
   const struct pvr_render_subpass *const subpass,
   const struct pvr_renderpass_hwsetup_subpass *hw_subpass)
{
   pco_fs_data *fs = &data->fs;
   for (unsigned u = 0; u < subpass->input_count; ++u) {
      unsigned idx = subpass->input_attachments[u].attachment_idx;
      if (idx == VK_ATTACHMENT_UNUSED)
         continue;

      bool onchip = hw_subpass->input_access[u].type !=
                    PVR_RENDERPASS_HWSETUP_INPUT_ACCESS_OFFCHIP;
      if (!onchip)
         continue;

      unsigned mrt_idx = hw_subpass->input_access[u].on_chip_rt;
      const struct usc_mrt_resource *mrt_resource =
         &hw_subpass->setup.mrt_resources[mrt_idx];

      bool tile_buffer = fs->ia_tile_buffers & BITFIELD_BIT(u);

      fs->ias_onchip[u] = (pco_range){
         .start = tile_buffer ? mrt_resource->mem.tile_buffer
                              : mrt_resource->reg.output_reg,
         .count =
            DIV_ROUND_UP(mrt_resource->intermediate_size, sizeof(uint32_t)),
      };

      if (tile_buffer)
         fs->ias_onchip[u].offset = mrt_resource->mem.offset_dw;
   }
}

static void pvr_setup_fs_blend(pco_data *data)
{
   unsigned num_blend_consts = util_bitcount(data->fs.blend_consts_needed);
   if (!num_blend_consts)
      return;

   data->fs.blend_consts = (pco_range){
      .start = data->common.shareds,
      .count = num_blend_consts,
   };

   data->common.shareds += num_blend_consts;
}

static void pvr_init_fs_tile_buffers(pco_data *data)
{
   if (!data->fs.num_tile_buffers)
      return;

   unsigned tile_buffer_addr_dwords =
      data->fs.num_tile_buffers * (sizeof(uint64_t) / sizeof(uint32_t));

   data->fs.tile_buffers = (pco_range){
      .count = tile_buffer_addr_dwords,
      .stride = sizeof(uint64_t) / sizeof(uint32_t),
   };
}

static void pvr_setup_fs_tile_buffers(pco_data *data)
{
   if (!data->fs.tile_buffers.count)
      return;

   data->fs.tile_buffers.start = data->common.shareds;
   data->common.shareds += data->fs.tile_buffers.count;
}

static void pvr_setup_fs_sample_locations(pco_data *data)
{
   if (!data->fs.uses.sample_locations)
      return;

   data->fs.sample_locations.start = data->common.shareds;
   data->fs.sample_locations.count = 4; /* TODO */
   data->common.shareds += data->fs.sample_locations.count;
}

static void pvr_alloc_cs_sysvals(pco_data *data, nir_shader *nir)
{
   BITSET_DECLARE(system_values_read, SYSTEM_VALUE_MAX);
   BITSET_COPY(system_values_read, nir->info.system_values_read);

   gl_system_value vtxin_sys_vals[] = {
      SYSTEM_VALUE_LOCAL_INVOCATION_INDEX,
   };

   gl_system_value coeff_sys_vals[] = {
      SYSTEM_VALUE_WORKGROUP_ID,
      SYSTEM_VALUE_NUM_WORKGROUPS,
   };

   for (unsigned u = 0; u < ARRAY_SIZE(vtxin_sys_vals); ++u) {
      if (BITSET_TEST(system_values_read, vtxin_sys_vals[u])) {
         nir_intrinsic_op op =
            nir_intrinsic_from_system_value(vtxin_sys_vals[u]);
         unsigned dwords = nir_intrinsic_infos[op].dest_components;
         assert(dwords > 0);

         allocate_val(data->common.sys_vals,
                      &data->common.vtxins,
                      vtxin_sys_vals[u],
                      dwords);

         BITSET_CLEAR(system_values_read, vtxin_sys_vals[u]);
      }
   }

   for (unsigned u = 0; u < ARRAY_SIZE(coeff_sys_vals); ++u) {
      if (BITSET_TEST(system_values_read, coeff_sys_vals[u])) {
         nir_intrinsic_op op =
            nir_intrinsic_from_system_value(coeff_sys_vals[u]);
         unsigned dwords = nir_intrinsic_infos[op].dest_components;
         assert(dwords > 0);

         if (dwords > 1 && data->common.coeffs & 1)
            ++data->common.coeffs;

         allocate_val(data->common.sys_vals,
                      &data->common.coeffs,
                      coeff_sys_vals[u],
                      dwords);

         BITSET_CLEAR(system_values_read, coeff_sys_vals[u]);
      }
   }

   check_unused_sysvals(system_values_read);
   assert(BITSET_IS_EMPTY(system_values_read));
}

static void pvr_alloc_cs_shmem(pco_data *data, nir_shader *nir)
{
   assert(!nir->info.cs.has_variable_shared_mem);

   data->cs.shmem.start = data->common.coeffs;
   data->cs.shmem.count = nir->info.shared_size >> 2;
   data->common.coeffs += data->cs.shmem.count;
   data->cs.zero_shmem = nir->info.zero_initialize_shared_memory;
}

static void pvr_init_descriptors(pco_data *data,
                                 nir_shader *nir,
                                 struct vk_pipeline_layout *layout)
{
   const struct pvr_device *device = vk_to_pvr_device(layout->base.device);
   data->common.robust_buffer_access =
      device->vk.enabled_features.robustBufferAccess;

   data->common.null_descriptor = device->vk.enabled_features.nullDescriptor;

   data->common.image_2d_view_of_3d =
      device->vk.enabled_features.image2DViewOf3D;

   for (unsigned desc_set = 0; desc_set < layout->set_count; ++desc_set) {
      const struct pvr_descriptor_set_layout *set_layout =
         vk_to_pvr_descriptor_set_layout(layout->set_layouts[desc_set]);
      pco_descriptor_set_data *desc_set_data =
         &data->common.desc_sets[desc_set];

      /* If the descriptor set isn't for this stage, skip it. */
      if (!(BITFIELD_BIT(nir->info.stage) & set_layout->stage_flags))
         continue;

      desc_set_data->binding_count = set_layout->binding_count;
      desc_set_data->bindings =
         rzalloc_array_size(NULL,
                            sizeof(*desc_set_data->bindings),
                            set_layout->binding_count);

      for (unsigned binding = 0; binding < set_layout->binding_count;
           ++binding) {
         const struct pvr_descriptor_set_layout_binding *layout_binding =
            &set_layout->bindings[binding];
         pco_binding_data *binding_data = &desc_set_data->bindings[binding];

         binding_data->is_img_smp = layout_binding->type ==
                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      }
   }
}

static void pvr_setup_descriptors(pco_data *data,
                                  nir_shader *nir,
                                  struct vk_pipeline_layout *layout)
{
   mesa_shader_stage stage = nir->info.stage;

   data->common.shareds = ALIGN_POT(data->common.shareds, 4);

   /* Allocate shareds for the descriptors. */
   for (unsigned desc_set = 0; desc_set < layout->set_count; ++desc_set) {
      const struct pvr_descriptor_set_layout *set_layout =
         vk_to_pvr_descriptor_set_layout(layout->set_layouts[desc_set]);
      const unsigned desc_set_size_dw = set_layout->size / sizeof(uint32_t);
      pco_descriptor_set_data *desc_set_data =
         &data->common.desc_sets[desc_set];
      pco_range *desc_set_range = &desc_set_data->range;

      assert(!(set_layout->size % sizeof(uint32_t)));

      /* If the descriptor set isn't for this stage or is unused, skip it. */
      if (!(BITFIELD_BIT(stage) & set_layout->stage_flags)) {
         assert(!desc_set_data->used);
         continue;
      }

      if (!desc_set_data->used)
         continue;

      desc_set_range->start = data->common.shareds;
      desc_set_range->count = desc_set_size_dw;
      data->common.shareds += desc_set_size_dw;

      for (unsigned binding = 0; binding < set_layout->binding_count;
           ++binding) {
         const struct pvr_descriptor_set_layout_binding *layout_binding =
            &set_layout->bindings[binding];
         pco_binding_data *binding_data = &desc_set_data->bindings[binding];

         /* Skip dynamic buffer bindings. */
         if (layout_binding->offset == ~0)
            continue;

         binding_data->range = (pco_range){
            .start = desc_set_range->start +
                     (layout_binding->offset / sizeof(uint32_t)),
            .count =
               (layout_binding->stride * layout_binding->descriptor_count) /
               sizeof(uint32_t),
            .stride = layout_binding->stride / sizeof(uint32_t),
         };
      }
   }

   /* Allocate shareds for the dynamic descriptors. */
   for (unsigned desc_set = 0; desc_set < layout->set_count; ++desc_set) {
      const struct pvr_descriptor_set_layout *set_layout =
         vk_to_pvr_descriptor_set_layout(layout->set_layouts[desc_set]);
      const unsigned desc_set_dynamic_size_dw =
         (set_layout->dynamic_buffer_count *
          sizeof(struct pvr_buffer_descriptor)) /
         sizeof(uint32_t);
      pco_descriptor_set_data *desc_set_data =
         &data->common.desc_sets[desc_set];
      pco_range *desc_set_dynamic_range = &desc_set_data->dynamic_range;

      if (!desc_set_dynamic_size_dw)
         continue;

      /* If the descriptor set isn't for this stage or is unused, skip it. */
      if (!(BITFIELD_BIT(stage) & set_layout->stage_flags)) {
         assert(!desc_set_data->used);
         continue;
      }

      if (!desc_set_data->used)
         continue;

      desc_set_dynamic_range->start = data->common.shareds;
      desc_set_dynamic_range->count = desc_set_dynamic_size_dw;
      data->common.shareds += desc_set_dynamic_size_dw;

      for (unsigned binding = 0; binding < set_layout->binding_count;
           ++binding) {
         const struct pvr_descriptor_set_layout_binding *layout_binding =
            &set_layout->bindings[binding];
         pco_binding_data *binding_data = &desc_set_data->bindings[binding];

         /* Skip non-dynamic bindings. */
         if (layout_binding->dynamic_buffer_idx == ~0)
            continue;

         binding_data->range = (pco_range){
            .start = desc_set_dynamic_range->start +
                     ((layout_binding->dynamic_buffer_idx *
                       sizeof(struct pvr_buffer_descriptor)) /
                      sizeof(uint32_t)),
            .count =
               (layout_binding->stride * layout_binding->descriptor_count) /
               sizeof(uint32_t),
            .stride = layout_binding->stride / sizeof(uint32_t),
         };
      }
   }

   if (data->common.push_consts.used > 0) {
      unsigned count = data->common.push_consts.used;

      if (count == ~0U) {
         count = 0;
         for (unsigned u = 0; u < layout->push_range_count; ++u) {
            VkPushConstantRange *range = &layout->push_ranges[u];
            if (!(mesa_to_vk_shader_stage(stage) & range->stageFlags))
               continue;

            count = MAX2(count, range->offset + range->size);
         }

         assert(!(count % 4));
         count = count / 4;
      }

      data->common.push_consts.range = (pco_range){
         .start = data->common.shareds,
         .count = count,
      };

      data->common.shareds += count;
   }

   if (data->common.uses.point_sampler) {
      data->common.shareds = ALIGN_POT(data->common.shareds, 4);
      data->common.point_sampler = (pco_range){
         .start = data->common.shareds,
         .count = ROGUE_NUM_TEXSTATE_DWORDS,
      };

      data->common.shareds += ROGUE_NUM_TEXSTATE_DWORDS;
   }

   if (data->common.uses.ia_sampler) {
      data->common.shareds = ALIGN_POT(data->common.shareds, 4);
      data->common.ia_sampler = (pco_range){
         .start = data->common.shareds,
         .count = ROGUE_NUM_TEXSTATE_DWORDS,
      };

      data->common.shareds += ROGUE_NUM_TEXSTATE_DWORDS;
   }

   if (true || data->common.spilled_temps) {
      data->common.spill_info = (pco_range){
         .start = data->common.shareds,
         .count = 3,
      };

      data->common.shareds += 3;
   }

   if (data->common.scratch) {
      data->common.scratch_info = (pco_range){
         .start = data->common.shareds,
         .count = 3,
      };

      data->common.shareds += 3;
   }
}

static void
pvr_preprocess_shader_data(pco_data *data,
                           nir_shader *nir,
                           const void *pCreateInfo,
                           struct vk_pipeline_layout *layout,
                           const struct vk_graphics_pipeline_state *state)
{
   const VkGraphicsPipelineCreateInfo *pGraphicsCreateInfo = pCreateInfo;

   switch (nir->info.stage) {
   case MESA_SHADER_VERTEX: {
      const VkPipelineVertexInputStateCreateInfo *const vertex_input_state =
         pGraphicsCreateInfo->pVertexInputState;

      pvr_init_vs_attribs(data, vertex_input_state);
      break;
   }

   case MESA_SHADER_FRAGMENT: {
      VK_FROM_HANDLE(pvr_render_pass, pass, pGraphicsCreateInfo->renderPass);
      const struct pvr_render_subpass *const subpass =
         &pass->subpasses[pGraphicsCreateInfo->subpass];
      const struct pvr_renderpass_hw_map *subpass_map =
         &pass->hw_setup->subpass_map[pGraphicsCreateInfo->subpass];
      const struct pvr_renderpass_hwsetup_subpass *hw_subpass =
         &pass->hw_setup->renders[subpass_map->render]
             .subpasses[subpass_map->subpass];

      pvr_init_fs_outputs(data, pass, subpass, hw_subpass);
      pvr_init_fs_input_attachments(data, pass, subpass, hw_subpass);
      pvr_init_fs_blend(data, state->cb);
      pvr_init_fs_tile_buffers(data);

      data->fs.uses.alpha_to_coverage = state->ms->alpha_to_coverage_enable;

      if (BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_CB_COLOR_WRITE_ENABLES) ||
          (state->cb && state->cb->color_write_enables !=
                           BITFIELD_MASK(MESA_VK_MAX_COLOR_ATTACHMENTS))) {
         data->fs.meta_present.color_write_enable = true;
      }

      /* TODO: push consts, dynamic state, etc. */
      break;
   }

   case MESA_SHADER_COMPUTE: {
      break;
   }

   default:
      UNREACHABLE("");
   }

   pvr_init_descriptors(data, nir, layout);

   /* TODO: common things, like large constants being put into shareds. */
}

static void pvr_postprocess_shader_data(pco_data *data,
                                        nir_shader *nir,
                                        const void *pCreateInfo,
                                        struct vk_pipeline_layout *layout)
{
   const VkGraphicsPipelineCreateInfo *pGraphicsCreateInfo = pCreateInfo;

   switch (nir->info.stage) {
   case MESA_SHADER_VERTEX: {
      pvr_alloc_vs_sysvals(data, nir);
      pvr_alloc_vs_attribs(data, nir);
      pvr_alloc_vs_varyings(data, nir);
      break;
   }

   case MESA_SHADER_FRAGMENT: {
      VK_FROM_HANDLE(pvr_render_pass, pass, pGraphicsCreateInfo->renderPass);
      const struct pvr_render_subpass *const subpass =
         &pass->subpasses[pGraphicsCreateInfo->subpass];
      const struct pvr_renderpass_hw_map *subpass_map =
         &pass->hw_setup->subpass_map[pGraphicsCreateInfo->subpass];
      const struct pvr_renderpass_hwsetup_subpass *hw_subpass =
         &pass->hw_setup->renders[subpass_map->render]
             .subpasses[subpass_map->subpass];

      pvr_alloc_fs_sysvals(data, nir);
      pvr_alloc_fs_varyings(data, nir);
      pvr_setup_fs_outputs(data, nir, subpass, hw_subpass);
      pvr_setup_fs_input_attachments(data, nir, subpass, hw_subpass);
      pvr_setup_fs_blend(data);
      pvr_setup_fs_tile_buffers(data);
      pvr_setup_fs_sample_locations(data);

      /* TODO: push consts, blend consts, dynamic state, etc. */
      break;
   }

   case MESA_SHADER_COMPUTE: {
      pvr_alloc_cs_sysvals(data, nir);
      pvr_alloc_cs_shmem(data, nir);
      break;
   }

   default:
      UNREACHABLE("");
   }

   pvr_setup_descriptors(data, nir, layout);

   /* TODO: common things, like large constants being put into shareds. */
}

static void pvr_early_init_shader_data(pco_data *data,
                                       nir_shader *nir,
                                       const void *pCreateInfo)
{
   const VkGraphicsPipelineCreateInfo *pGraphicsCreateInfo = pCreateInfo;

   switch (nir->info.stage) {
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_FRAGMENT: {
      VK_FROM_HANDLE(pvr_render_pass, pass, pGraphicsCreateInfo->renderPass);
      data->common.multiview = pass->multiview_enabled;

      break;
   }

   case MESA_SHADER_COMPUTE:
      break;

   default:
      UNREACHABLE("Unsupported stage.");
   }
}

/* Compiles and uploads shaders and PDS programs. */
static VkResult
pvr_graphics_pipeline_compile(struct pvr_device *const device,
                              struct vk_pipeline_cache *cache,
                              const VkGraphicsPipelineCreateInfo *pCreateInfo,
                              const VkAllocationCallbacks *const allocator,
                              struct pvr_graphics_pipeline *const gfx_pipeline,
                              const struct vk_graphics_pipeline_state *state)
{
   struct vk_pipeline_layout *layout = gfx_pipeline->base.layout;
   const uint32_t cache_line_size =
      pvr_get_slc_cache_line_size(&device->pdevice->dev_info);
   VkResult result;

   struct pvr_vertex_shader_state *vertex_state =
      &gfx_pipeline->shader_state.vertex;
   struct pvr_fragment_shader_state *fragment_state =
      &gfx_pipeline->shader_state.fragment;

   pco_ctx *pco_ctx = device->pdevice->pco_ctx;

   nir_shader *producer = NULL;
   nir_shader *consumer = NULL;
   pco_data shader_data[MESA_SHADER_STAGES] = { 0 };
   pco_data *producer_data = NULL;
   nir_shader *nir_shaders[MESA_SHADER_STAGES] = { 0 };
   pco_shader *pco_shaders[MESA_SHADER_STAGES] = { 0 };
   pco_shader **vs = &pco_shaders[MESA_SHADER_VERTEX];
   pco_shader **fs = &pco_shaders[MESA_SHADER_FRAGMENT];
   void *shader_mem_ctx = ralloc_context(NULL);

   struct pvr_pds_vertex_dma vtx_dma_descriptions[PVR_MAX_VERTEX_ATTRIB_DMAS];
   uint32_t vtx_dma_count = 0;

   for (mesa_shader_stage stage = 0; stage < MESA_SHADER_STAGES; ++stage) {
      size_t stage_index = gfx_pipeline->stage_indices[stage];

      /* Skip unused/inactive stages. */
      if (stage_index == ~0)
         continue;

      result =
         vk_pipeline_shader_stage_to_nir(&device->vk,
                                         gfx_pipeline->base.pipeline_flags,
                                         &pCreateInfo->pStages[stage_index],
                                         pco_spirv_options(),
                                         pco_nir_options(),
                                         shader_mem_ctx,
                                         &nir_shaders[stage]);
      if (result != VK_SUCCESS)
         goto err_free_build_context;

      pvr_early_init_shader_data(&shader_data[stage],
                                 nir_shaders[stage],
                                 pCreateInfo);
      pco_preprocess_nir(pco_ctx, nir_shaders[stage]);
   }

   for (mesa_shader_stage stage = 0; stage < MESA_SHADER_STAGES; ++stage) {
      if (!nir_shaders[stage])
         continue;

      if (producer)
         pco_link_nir(pco_ctx,
                      producer,
                      nir_shaders[stage],
                      producer_data,
                      &shader_data[stage]);

      producer = nir_shaders[stage];
      producer_data = &shader_data[stage];
   }

   for (mesa_shader_stage stage = MESA_SHADER_STAGES; stage-- > 0;) {
      if (!nir_shaders[stage])
         continue;

      if (consumer)
         pco_rev_link_nir(pco_ctx, nir_shaders[stage], consumer);

      consumer = nir_shaders[stage];
   }

   for (mesa_shader_stage stage = 0; stage < MESA_SHADER_STAGES; ++stage) {
      if (!nir_shaders[stage])
         continue;

      pvr_preprocess_shader_data(&shader_data[stage],
                                 nir_shaders[stage],
                                 pCreateInfo,
                                 layout,
                                 state);

      pco_lower_nir(pco_ctx, nir_shaders[stage], &shader_data[stage]);

      pco_postprocess_nir(pco_ctx, nir_shaders[stage], &shader_data[stage]);

      pvr_postprocess_shader_data(&shader_data[stage],
                                  nir_shaders[stage],
                                  pCreateInfo,
                                  layout);
   }

   for (mesa_shader_stage stage = 0; stage < MESA_SHADER_STAGES; ++stage) {
      pco_shader **pco = &pco_shaders[stage];

      /* Skip unused/inactive stages. */
      if (!nir_shaders[stage])
         continue;

      *pco = pco_trans_nir(pco_ctx,
                           nir_shaders[stage],
                           &shader_data[stage],
                           shader_mem_ctx);
      if (!*pco) {
         result = VK_ERROR_INITIALIZATION_FAILED;
         goto err_free_build_context;
      }

      pco_process_ir(pco_ctx, *pco);
      pco_encode_ir(pco_ctx, *pco);
   }

   pvr_vertex_state_save(gfx_pipeline, *vs);

   pvr_graphics_pipeline_setup_vertex_dma(gfx_pipeline,
                                          pCreateInfo->pVertexInputState,
                                          state->vi,
                                          vtx_dma_descriptions,
                                          &vtx_dma_count);

   result = pvr_gpu_upload_usc(device,
                               pco_shader_binary_data(*vs),
                               pco_shader_binary_size(*vs),
                               cache_line_size,
                               &vertex_state->shader_bo);
   if (result != VK_SUCCESS)
      goto err_free_build_context;

   if (*fs) {
      pvr_fragment_state_save(gfx_pipeline, *fs);

      pvr_graphics_pipeline_setup_fragment_coeff_program(
         gfx_pipeline,
         nir_shaders[MESA_SHADER_FRAGMENT]);

      result = pvr_gpu_upload_usc(device,
                                  pco_shader_binary_data(*fs),
                                  pco_shader_binary_size(*fs),
                                  cache_line_size,
                                  &fragment_state->shader_bo);
      if (result != VK_SUCCESS)
         goto err_free_vertex_bo;

      result = pvr_pds_coeff_program_create_and_upload(device,
                                                       allocator,
                                                       fragment_state);
      if (result != VK_SUCCESS)
         goto err_free_fragment_bo;

      result = pvr_pds_fragment_program_create(device,
                                               allocator,
                                               *fs,
                                               fragment_state);
      if (result != VK_SUCCESS)
         goto err_free_coeff_program;

      result = pvr_pds_descriptor_program_create_and_upload(
         device,
         allocator,
         layout,
         MESA_SHADER_FRAGMENT,
         &gfx_pipeline->fs_data,
         &fragment_state->descriptor_state);
      if (result != VK_SUCCESS)
         goto err_free_frag_program;

      /* If not, we need to MAX2() and set
       * `fragment_state->stage_state.pds_temps_count` appropriately.
       */
      assert(fragment_state->descriptor_state.pds_info.temps_required == 0);
   }

   result = pvr_pds_vertex_attrib_programs_create_and_upload(
      device,
      allocator,
      &gfx_pipeline->vs_data,
      vtx_dma_descriptions,
      vtx_dma_count,
      &vertex_state->pds_attrib_programs);
   if (result != VK_SUCCESS)
      goto err_free_frag_descriptor_program;

   result = pvr_pds_descriptor_program_create_and_upload(
      device,
      allocator,
      layout,
      MESA_SHADER_VERTEX,
      &gfx_pipeline->vs_data,
      &vertex_state->descriptor_state);
   if (result != VK_SUCCESS)
      goto err_free_vertex_attrib_program;

   /* FIXME: When the temp_buffer_total_size is non-zero we need to allocate a
    * scratch buffer for both vertex and fragment stage.
    * Figure out the best place to do this.
    */
   /* assert(pvr_pds_descriptor_program_variables.temp_buff_total_size == 0); */
   /* TODO: Implement spilling with the above. */

   ralloc_free(shader_mem_ctx);

   return VK_SUCCESS;

err_free_vertex_attrib_program:
   for (uint32_t i = 0; i < ARRAY_SIZE(vertex_state->pds_attrib_programs);
        i++) {
      struct pvr_pds_attrib_program *const attrib_program =
         &vertex_state->pds_attrib_programs[i];

      pvr_pds_vertex_attrib_program_destroy(device, allocator, attrib_program);
   }
err_free_frag_descriptor_program:
   pvr_pds_descriptor_program_destroy(device,
                                      allocator,
                                      &fragment_state->descriptor_state);
err_free_frag_program:
   vk_free2(&device->vk.alloc,
            allocator,
            fragment_state->pds_fragment_program_buffer);
err_free_coeff_program:
   vk_free2(&device->vk.alloc,
            allocator,
            fragment_state->pds_coeff_program_buffer);
err_free_fragment_bo:
   pvr_bo_suballoc_free(fragment_state->shader_bo);
err_free_vertex_bo:
   pvr_bo_suballoc_free(vertex_state->shader_bo);
err_free_build_context:
   ralloc_free(shader_mem_ctx);
   return result;
}

static struct vk_render_pass_state
pvr_create_renderpass_state(const VkGraphicsPipelineCreateInfo *const info)
{
   VK_FROM_HANDLE(pvr_render_pass, pass, info->renderPass);
   const struct pvr_render_subpass *const subpass =
      &pass->subpasses[info->subpass];

   enum vk_rp_attachment_flags attachments = 0;

   assert(info->subpass < pass->subpass_count);

   for (uint32_t i = 0; i < subpass->color_count; i++) {
      if (subpass->color_attachments[i] == VK_ATTACHMENT_UNUSED)
         continue;

      if (pass->attachments[subpass->color_attachments[i]].aspects)
         attachments |= MESA_VK_RP_ATTACHMENT_COLOR_0_BIT << i;
   }

   if (subpass->depth_stencil_attachment != VK_ATTACHMENT_UNUSED) {
      VkImageAspectFlags ds_aspects =
         pass->attachments[subpass->depth_stencil_attachment].aspects;
      if (ds_aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
         attachments |= MESA_VK_RP_ATTACHMENT_DEPTH_BIT;
      if (ds_aspects & VK_IMAGE_ASPECT_STENCIL_BIT)
         attachments |= MESA_VK_RP_ATTACHMENT_STENCIL_BIT;
   }

   return (struct vk_render_pass_state){
      .attachments = attachments,
      .view_mask = subpass->view_mask,
   };
}

static VkResult
pvr_graphics_pipeline_init(struct pvr_device *device,
                           struct vk_pipeline_cache *cache,
                           const VkGraphicsPipelineCreateInfo *pCreateInfo,
                           const VkAllocationCallbacks *allocator,
                           struct pvr_graphics_pipeline *gfx_pipeline)
{
   struct vk_dynamic_graphics_state *const dynamic_state =
      &gfx_pipeline->dynamic_state;
   const struct vk_render_pass_state rp_state =
      pvr_create_renderpass_state(pCreateInfo);

   struct vk_graphics_pipeline_all_state all_state;
   struct vk_graphics_pipeline_state state = { 0 };

   VkResult result;

   pvr_pipeline_init(device,
                     PVR_PIPELINE_TYPE_GRAPHICS,
                     pCreateInfo->layout,
                     &gfx_pipeline->base);

   result = vk_graphics_pipeline_state_fill(&device->vk,
                                            &state,
                                            pCreateInfo,
                                            &rp_state,
                                            0,
                                            &all_state,
                                            NULL,
                                            0,
                                            NULL);
   if (result != VK_SUCCESS)
      goto err_pipeline_finish;

   vk_dynamic_graphics_state_init(dynamic_state);

   /* Load static state into base dynamic state holder. */
   vk_dynamic_graphics_state_fill(dynamic_state, &state);

   /* The value of ms.rasterization_samples is undefined when
    * rasterizer_discard_enable is set, but we need a specific value.
    * Fill that in here.
    */
   if (state.rs->rasterizer_discard_enable)
      dynamic_state->ms.rasterization_samples = VK_SAMPLE_COUNT_1_BIT;

   memset(gfx_pipeline->stage_indices, ~0, sizeof(gfx_pipeline->stage_indices));

   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
      VkShaderStageFlagBits vk_stage = pCreateInfo->pStages[i].stage;
      mesa_shader_stage gl_stage = vk_to_mesa_shader_stage(vk_stage);
      /* From the Vulkan 1.2.192 spec for VkPipelineShaderStageCreateInfo:
       *
       *    "stage must not be VK_SHADER_STAGE_ALL_GRAPHICS,
       *    or VK_SHADER_STAGE_ALL."
       *
       * So we don't handle that.
       *
       * We also don't handle VK_SHADER_STAGE_TESSELLATION_* and
       * VK_SHADER_STAGE_GEOMETRY_BIT stages as 'tessellationShader' and
       * 'geometryShader' are set to false in the VkPhysicalDeviceFeatures
       * structure returned by the driver.
       */
      switch (pCreateInfo->pStages[i].stage) {
      case VK_SHADER_STAGE_VERTEX_BIT:
      case VK_SHADER_STAGE_FRAGMENT_BIT:
         gfx_pipeline->stage_indices[gl_stage] = i;
         break;
      default:
         UNREACHABLE("Unsupported stage.");
      }
   }

   /* Compiles and uploads shaders and PDS programs. */
   result = pvr_graphics_pipeline_compile(device,
                                          cache,
                                          pCreateInfo,
                                          allocator,
                                          gfx_pipeline,
                                          &state);
   if (result != VK_SUCCESS)
      goto err_pipeline_finish;

   return VK_SUCCESS;

err_pipeline_finish:
   pvr_pipeline_finish(device, &gfx_pipeline->base);

   return result;
}

/* If allocator == NULL, the internal one will be used. */
static VkResult
pvr_graphics_pipeline_create(struct pvr_device *device,
                             struct vk_pipeline_cache *cache,
                             const VkGraphicsPipelineCreateInfo *pCreateInfo,
                             const VkAllocationCallbacks *allocator,
                             VkPipeline *const pipeline_out)
{
   struct pvr_graphics_pipeline *gfx_pipeline;
   VkResult result;

   gfx_pipeline = vk_zalloc2(&device->vk.alloc,
                             allocator,
                             sizeof(*gfx_pipeline),
                             8,
                             VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!gfx_pipeline)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* Compiles and uploads shaders and PDS programs too. */
   result = pvr_graphics_pipeline_init(device,
                                       cache,
                                       pCreateInfo,
                                       allocator,
                                       gfx_pipeline);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, allocator, gfx_pipeline);
      return result;
   }

   *pipeline_out = pvr_pipeline_to_handle(&gfx_pipeline->base);

   return VK_SUCCESS;
}

VkResult
pvr_CreateGraphicsPipelines(VkDevice _device,
                            VkPipelineCache pipelineCache,
                            uint32_t createInfoCount,
                            const VkGraphicsPipelineCreateInfo *pCreateInfos,
                            const VkAllocationCallbacks *pAllocator,
                            VkPipeline *pPipelines)
{
   VK_FROM_HANDLE(vk_pipeline_cache, cache, pipelineCache);
   VK_FROM_HANDLE(pvr_device, device, _device);
   VkResult result = VK_SUCCESS;

   for (uint32_t i = 0; i < createInfoCount; i++) {
      const VkResult local_result =
         pvr_graphics_pipeline_create(device,
                                      cache,
                                      &pCreateInfos[i],
                                      pAllocator,
                                      &pPipelines[i]);
      if (local_result != VK_SUCCESS) {
         result = local_result;
         pPipelines[i] = VK_NULL_HANDLE;
      }
   }

   return result;
}

/*****************************************************************************
   Other functions
*****************************************************************************/

void pvr_DestroyPipeline(VkDevice _device,
                         VkPipeline _pipeline,
                         const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(pvr_pipeline, pipeline, _pipeline);
   VK_FROM_HANDLE(pvr_device, device, _device);

   if (!pipeline)
      return;

   switch (pipeline->type) {
   case PVR_PIPELINE_TYPE_GRAPHICS: {
      struct pvr_graphics_pipeline *const gfx_pipeline =
         to_pvr_graphics_pipeline(pipeline);

      pvr_graphics_pipeline_destroy(device, pAllocator, gfx_pipeline);
      break;
   }

   case PVR_PIPELINE_TYPE_COMPUTE: {
      struct pvr_compute_pipeline *const compute_pipeline =
         to_pvr_compute_pipeline(pipeline);

      pvr_compute_pipeline_destroy(device, pAllocator, compute_pipeline);
      break;
   }

   default:
      UNREACHABLE("Unknown pipeline type.");
   }
}
