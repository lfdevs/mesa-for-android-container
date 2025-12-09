/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_private.h"
#include "anv_shader.h"

#include "nir/nir_serialize.h"

#include "compiler/brw/brw_disasm.h"
#include "util/shader_stats.h"

static void
anv_shader_destroy(struct vk_device *vk_device,
                   struct vk_shader *vk_shader,
                   const VkAllocationCallbacks *pAllocator)
{
   struct anv_device *device =
      container_of(vk_device, struct anv_device, vk);
   struct anv_shader *shader =
      container_of(vk_shader, struct anv_shader, vk);

   for (uint32_t i = 0; i < shader->bind_map.embedded_sampler_count; i++)
      anv_embedded_sampler_unref(device, shader->embedded_samplers[i]);

   anv_state_pool_free(&device->instruction_state_pool, shader->kernel);
   anv_reloc_list_finish(&shader->relocs);
   vk_shader_free(vk_device, pAllocator, vk_shader);
}

VkResult
anv_shader_deserialize(struct vk_device *vk_device,
                       struct blob_reader *blob,
                       uint32_t binary_version,
                       const VkAllocationCallbacks* pAllocator,
                       struct vk_shader **shader_out)
{
   struct anv_device *device = container_of(vk_device, struct anv_device, vk);

   struct anv_shader_data data = {};

   mesa_shader_stage stage = blob_read_uint32(blob);

   uint32_t code_len = blob_read_uint32(blob);
   data.code = (void *)blob_read_bytes(blob, code_len);

   blob_copy_bytes(blob, &data.prog_data, brw_prog_data_size(stage));

   data.prog_data.base.relocs =
      blob_read_bytes(blob, data.prog_data.base.num_relocs *
                      sizeof(data.prog_data.base.relocs[0]));

   data.num_stats = blob_read_uint32(blob);
   blob_copy_bytes(blob, data.stats, data.num_stats * sizeof(data.stats[0]));

   uint32_t xfb_size = blob_read_uint32(blob);
   if (xfb_size)
      data.xfb_info = blob_read_bytes(blob, xfb_size);

   data.instance_multiplier = blob_read_uint32(blob);

   data.push_desc_info.used_descriptors = blob_read_uint32(blob);
   data.push_desc_info.fully_promoted_ubo_descriptors = blob_read_uint32(blob);
   data.push_desc_info.push_set_buffer = blob_read_uint8(blob);

   blob_copy_bytes(blob, data.bind_map.surface_sha1, sizeof(data.bind_map.surface_sha1));
   blob_copy_bytes(blob, data.bind_map.sampler_sha1, sizeof(data.bind_map.sampler_sha1));
   blob_copy_bytes(blob, data.bind_map.push_sha1, sizeof(data.bind_map.push_sha1));
   data.bind_map.layout_type = blob_read_uint16(blob);
   data.bind_map.binding_mask = blob_read_uint16(blob);
   data.bind_map.surface_count = blob_read_uint32(blob);
   data.bind_map.sampler_count = blob_read_uint32(blob);
   data.bind_map.embedded_sampler_count = blob_read_uint32(blob);
   data.bind_map.surface_to_descriptor = (void *)
      blob_read_bytes(blob, data.bind_map.surface_count *
                            sizeof(*data.bind_map.surface_to_descriptor));
   data.bind_map.sampler_to_descriptor = (void *)
      blob_read_bytes(blob, data.bind_map.sampler_count *
                            sizeof(*data.bind_map.sampler_to_descriptor));
   data.bind_map.embedded_sampler_to_binding = (void *)
      blob_read_bytes(blob, data.bind_map.embedded_sampler_count *
                            sizeof(*data.bind_map.embedded_sampler_to_binding));
   blob_copy_bytes(blob, data.bind_map.input_attachments,
                   sizeof(data.bind_map.input_attachments));
   blob_copy_bytes(blob, data.bind_map.push_ranges, sizeof(data.bind_map.push_ranges));
   blob_copy_bytes(blob, data.bind_map.dynamic_descriptors,
                   sizeof(data.bind_map.dynamic_descriptors));

   if (blob->overrun)
      return vk_error(device, VK_ERROR_UNKNOWN);

   VkResult result =
      anv_shader_create(device, stage, NULL, &data, pAllocator, shader_out);

   return result;
}

static bool
anv_shader_serialize(struct vk_device *device,
                     const struct vk_shader *vk_shader,
                     struct blob *blob)
{
   struct anv_shader *shader =
      container_of(vk_shader, struct anv_shader, vk);

   blob_write_uint32(blob, vk_shader->stage);

   blob_write_uint32(blob, shader->prog_data->program_size);
   blob_write_bytes(blob, shader->kernel.map,
                    shader->prog_data->program_size);

   union brw_any_prog_data prog_data;
   memcpy(&prog_data, shader->prog_data, brw_prog_data_size(vk_shader->stage));
   prog_data.base.relocs = NULL;
   prog_data.base.param = NULL;

   blob_write_bytes(blob, &prog_data, brw_prog_data_size(vk_shader->stage));

   blob_write_bytes(blob, shader->prog_data->relocs,
                    shader->prog_data->num_relocs *
                    sizeof(shader->prog_data->relocs[0]));

   blob_write_uint32(blob, shader->num_stats);
   blob_write_bytes(blob, shader->stats,
                    shader->num_stats * sizeof(shader->stats[0]));

   if (shader->xfb_info) {
      uint32_t xfb_info_size =
         nir_xfb_info_size(shader->xfb_info->output_count);
      blob_write_uint32(blob, xfb_info_size);
      blob_write_bytes(blob, shader->xfb_info, xfb_info_size);
   } else {
      blob_write_uint32(blob, 0);
   }

   blob_write_uint32(blob, shader->instance_multiplier);

   blob_write_uint32(blob, shader->push_desc_info.used_descriptors);
   blob_write_uint32(blob, shader->push_desc_info.fully_promoted_ubo_descriptors);
   blob_write_uint8(blob, shader->push_desc_info.push_set_buffer);

   blob_write_bytes(blob, shader->bind_map.surface_sha1,
                    sizeof(shader->bind_map.surface_sha1));
   blob_write_bytes(blob, shader->bind_map.sampler_sha1,
                    sizeof(shader->bind_map.sampler_sha1));
   blob_write_bytes(blob, shader->bind_map.push_sha1,
                    sizeof(shader->bind_map.push_sha1));
   blob_write_uint16(blob, shader->bind_map.layout_type);
   blob_write_uint16(blob, shader->bind_map.binding_mask);
   blob_write_uint32(blob, shader->bind_map.surface_count);
   blob_write_uint32(blob, shader->bind_map.sampler_count);
   blob_write_uint32(blob, shader->bind_map.embedded_sampler_count);
   blob_write_bytes(blob, shader->bind_map.surface_to_descriptor,
                    shader->bind_map.surface_count *
                    sizeof(*shader->bind_map.surface_to_descriptor));
   blob_write_bytes(blob, shader->bind_map.sampler_to_descriptor,
                    shader->bind_map.sampler_count *
                    sizeof(*shader->bind_map.sampler_to_descriptor));
   blob_write_bytes(blob, shader->bind_map.embedded_sampler_to_binding,
                    shader->bind_map.embedded_sampler_count *
                    sizeof(*shader->bind_map.embedded_sampler_to_binding));
   blob_write_bytes(blob, shader->bind_map.input_attachments,
                    sizeof(shader->bind_map.input_attachments));
   blob_write_bytes(blob, shader->bind_map.push_ranges,
                    sizeof(shader->bind_map.push_ranges));
   blob_write_bytes(blob, shader->bind_map.dynamic_descriptors,
                    sizeof(shader->bind_map.dynamic_descriptors));

   return !blob->out_of_memory;
}

static VkResult
anv_shader_get_executable_properties(struct vk_device *device,
                                     const struct vk_shader *vk_shader,
                                     uint32_t *executable_count,
                                     VkPipelineExecutablePropertiesKHR *properties)
{
   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutablePropertiesKHR, out,
                          properties, executable_count);
   struct anv_shader *shader =
      container_of(vk_shader, struct anv_shader, vk);

   for (uint32_t i = 0; i < shader->num_stats; i++) {
      const struct genisa_stats *stats = &shader->stats[i];

      vk_outarray_append_typed(VkPipelineExecutablePropertiesKHR, &out, props) {
         mesa_shader_stage stage = vk_shader->stage;
         props->stages = mesa_to_vk_shader_stage(stage);

         unsigned simd_width = stats->dispatch_width;
         if (stage == MESA_SHADER_FRAGMENT) {
            if (stats->max_polygons > 1)
               VK_PRINT_STR(props->name, "SIMD%dx%d %s",
                            stats->max_polygons,
                            simd_width / stats->max_polygons,
                            _mesa_shader_stage_to_string(stage));
            else
               VK_PRINT_STR(props->name, "%s%d %s",
                            simd_width ? "SIMD" : "vec",
                            simd_width ? simd_width : 4,
                            _mesa_shader_stage_to_string(stage));
         } else {
            VK_COPY_STR(props->name, _mesa_shader_stage_to_string(stage));
         }
         VK_PRINT_STR(props->description, "%s%d %s shader",
                      simd_width ? "SIMD" : "vec",
                      simd_width ? simd_width : 4,
                      _mesa_shader_stage_to_string(stage));

         /* The compiler gives us a dispatch width of 0 for vec4 but Vulkan
          * wants a subgroup size of 1.
          */
         props->subgroupSize = MAX2(simd_width, 1);
      }
   }

   return vk_outarray_status(&out);
}

static VkResult
anv_shader_get_executable_statistics(struct vk_device *vk_device,
                                     const struct vk_shader *vk_shader,
                                     uint32_t executable_index,
                                     uint32_t *statistic_count,
                                     VkPipelineExecutableStatisticKHR *statistics)
{
   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutableStatisticKHR, out,
                          statistics, statistic_count);
   struct anv_shader *shader =
      container_of(vk_shader, struct anv_shader, vk);

   assert(executable_index < shader->num_stats);
   vk_add_genisa_stats(out, &shader->stats[executable_index]);
   return VK_SUCCESS;
}

static bool
write_ir_text(VkPipelineExecutableInternalRepresentationKHR* ir,
              const char *data)
{
   ir->isText = VK_TRUE;

   size_t data_len = strlen(data) + 1;

   if (ir->pData == NULL) {
      ir->dataSize = data_len;
      return true;
   }

   strncpy(ir->pData, data, ir->dataSize);
   if (ir->dataSize < data_len)
      return false;

   ir->dataSize = data_len;
   return true;
}

static VkResult
anv_shader_get_executable_internal_representations(
   struct vk_device *device,
   const struct vk_shader *vk_shader,
   uint32_t executable_index,
   uint32_t *internal_representation_count,
   VkPipelineExecutableInternalRepresentationKHR *internal_representations)
{
   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutableInternalRepresentationKHR, out,
                          internal_representations,
                          internal_representation_count);
   bool incomplete_text = false;
   struct anv_shader *shader =
      container_of(vk_shader, struct anv_shader, vk);
   assert(executable_index < shader->num_stats);

   if (shader->nir_str) {
      vk_outarray_append_typed(VkPipelineExecutableInternalRepresentationKHR, &out, ir) {
         VK_COPY_STR(ir->name, "Final NIR");
         VK_COPY_STR(ir->description,
                     "Final NIR before going into the back-end compiler");

         if (!write_ir_text(ir, shader->nir_str))
            incomplete_text = true;
      }
   }

   if (shader->asm_str) {
      vk_outarray_append_typed(VkPipelineExecutableInternalRepresentationKHR, &out, ir) {
         VK_COPY_STR(ir->name, "GEN Assembly");
         VK_COPY_STR(ir->description,
                     "Final GEN assembly for the generated shader binary");

         if (!write_ir_text(ir, shader->asm_str))
            incomplete_text = true;
      }
   }

   return incomplete_text ? VK_INCOMPLETE : vk_outarray_status(&out);
}

static struct vk_shader_ops anv_shader_ops = {
   .destroy                   = anv_shader_destroy,
   .serialize                 = anv_shader_serialize,
   .get_executable_properties = anv_shader_get_executable_properties,
   .get_executable_statistics = anv_shader_get_executable_statistics,
   .get_executable_internal_representations =
      anv_shader_get_executable_internal_representations,
};

static int
anv_shader_set_relocs(struct anv_device *device,
                      struct intel_shader_reloc_value *reloc_values,
                      struct anv_shader *shader)
{
   int rv_count = 0;
   const uint64_t shader_data_addr =
      device->physical->va.instruction_state_pool.addr +
      shader->kernel.offset +
      shader->prog_data->const_data_offset;

   assert((device->physical->va.instruction_state_pool.addr & 0xffffffff) == 0);
   reloc_values[rv_count++] = (struct intel_shader_reloc_value) {
      .id = BRW_SHADER_RELOC_INSTRUCTION_BASE_ADDR_HIGH,
      .value = device->physical->va.instruction_state_pool.addr >> 32,
   };
   assert((device->physical->va.dynamic_visible_pool.addr & 0xffffffff) == 0);
   reloc_values[rv_count++] = (struct intel_shader_reloc_value) {
      .id = BRW_SHADER_RELOC_DESCRIPTORS_BUFFER_ADDR_HIGH,
      .value = device->physical->va.dynamic_visible_pool.addr >> 32,
   };
   assert((device->physical->va.indirect_descriptor_pool.addr & 0xffffffff) == 0);
   assert((device->physical->va.internal_surface_state_pool.addr & 0xffffffff) == 0);
   reloc_values[rv_count++] = (struct intel_shader_reloc_value) {
      .id = BRW_SHADER_RELOC_DESCRIPTORS_ADDR_HIGH,
      .value = device->physical->indirect_descriptors ?
               (device->physical->va.indirect_descriptor_pool.addr >> 32) :
               (device->physical->va.internal_surface_state_pool.addr >> 32),
   };
   assert((device->physical->va.instruction_state_pool.addr & 0xffffffff) == 0);
   reloc_values[rv_count++] = (struct intel_shader_reloc_value) {
      .id = INTEL_SHADER_RELOC_CONST_DATA_ADDR_LOW,
      .value = shader_data_addr,
   };
   assert((device->physical->va.instruction_state_pool.addr & 0xffffffff) == 0);
   assert(shader_data_addr >> 32 == device->physical->va.instruction_state_pool.addr >> 32);
   reloc_values[rv_count++] = (struct intel_shader_reloc_value) {
      .id = INTEL_SHADER_RELOC_CONST_DATA_ADDR_HIGH,
      .value = device->physical->va.instruction_state_pool.addr >> 32,
   };
   reloc_values[rv_count++] = (struct intel_shader_reloc_value) {
      .id = INTEL_SHADER_RELOC_SHADER_START_OFFSET,
      .value = shader->kernel.offset,
   };
   if (brw_shader_stage_is_bindless(shader->vk.stage)) {
      const struct brw_bs_prog_data *bs_prog_data =
         brw_bs_prog_data_const(shader->prog_data);
      uint64_t resume_sbt_addr =
         device->physical->va.instruction_state_pool.addr +
         shader->kernel.offset +
         bs_prog_data->resume_sbt_offset;
      reloc_values[rv_count++] = (struct intel_shader_reloc_value) {
         .id = BRW_SHADER_RELOC_RESUME_SBT_ADDR_LOW,
         .value = resume_sbt_addr,
      };
      reloc_values[rv_count++] = (struct intel_shader_reloc_value) {
         .id = BRW_SHADER_RELOC_RESUME_SBT_ADDR_HIGH,
         .value = resume_sbt_addr >> 32,
      };
   }

   if (INTEL_DEBUG(DEBUG_SHADER_PRINT)) {
      struct anv_bo *bo = device->printf.bo;
      assert(bo != NULL);

      reloc_values[rv_count++] = (struct intel_shader_reloc_value) {
         .id = BRW_SHADER_RELOC_PRINTF_BUFFER_ADDR_LOW,
         .value = bo->offset & 0xffffffff,
      };
      reloc_values[rv_count++] = (struct intel_shader_reloc_value) {
         .id = BRW_SHADER_RELOC_PRINTF_BUFFER_ADDR_HIGH,
         .value = bo->offset >> 32,
      };
      reloc_values[rv_count++] = (struct intel_shader_reloc_value) {
         .id = BRW_SHADER_RELOC_PRINTF_BUFFER_SIZE,
         .value = anv_printf_buffer_size(),
      };
   }

   for (uint32_t i = 0; i < shader->bind_map.embedded_sampler_count; i++) {
      reloc_values[rv_count++] = (struct intel_shader_reloc_value) {
         .id = BRW_SHADER_RELOC_EMBEDDED_SAMPLER_HANDLE + i,
         .value = shader->embedded_samplers[i]->sampler_state.offset,
      };
   }

   return rv_count;
}

static VkResult
anv_shader_reloc(struct anv_device *device,
                 void *code,
                 struct anv_shader *shader,
                 const VkAllocationCallbacks *pAllocator)
{
   const uint32_t max_relocs =
      BRW_SHADER_RELOC_EMBEDDED_SAMPLER_HANDLE +
      shader->bind_map.embedded_sampler_count;
   uint32_t rv_count;
   struct intel_shader_reloc_value *reloc_values =
      vk_zalloc2(&device->vk.alloc, pAllocator,
                 sizeof(struct intel_shader_reloc_value) * max_relocs, 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (reloc_values == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   rv_count = anv_shader_set_relocs(device, reloc_values, shader);
   assert(rv_count <= max_relocs);

   brw_write_shader_relocs(&device->physical->compiler->isa,
                           code, shader->prog_data,
                           reloc_values, rv_count);

   vk_free2(&device->vk.alloc, pAllocator, reloc_values);

   return VK_SUCCESS;
}

struct internal_representation {
   char *nir_str;
   uint32_t nir_str_len;
   char *asm_str;
   uint32_t asm_str_len;
};

static void
get_internal_representation_data(struct internal_representation *output,
                                 struct anv_device *device,
                                 struct anv_shader_data *shader_data,
                                 void *mem_ctx)
{
   assert(mem_ctx != NULL);

   output->nir_str = nir_shader_as_str(shader_data->info->nir, mem_ctx);
   output->nir_str_len = strlen(output->nir_str) + 1;

   char *stream_data = NULL;
   size_t stream_size = 0;
   FILE *stream = open_memstream(&stream_data, &stream_size);

   const struct anv_pipeline_bind_map *bind_map = &shader_data->bind_map;
   uint32_t push_size = 0;
   for (unsigned i = 0; i < 4; i++)
      push_size += bind_map->push_ranges[i].length;
   if (push_size > 0) {
      fprintf(stream, "Push constant ranges:\n");
      for (unsigned i = 0; i < 4; i++) {
         if (bind_map->push_ranges[i].length == 0)
            continue;

         fprintf(stream, "    RANGE%d (%dB): ", i,
                 bind_map->push_ranges[i].length * 32);

         switch (bind_map->push_ranges[i].set) {
         case ANV_DESCRIPTOR_SET_NULL:
            fprintf(stream, "NULL");
            break;

         case ANV_DESCRIPTOR_SET_PUSH_CONSTANTS:
            fprintf(stream, "Vulkan push constants and API params");
            break;

         case ANV_DESCRIPTOR_SET_DESCRIPTORS_BUFFER:
            fprintf(stream, "Descriptor buffer (desc buffer) for set %d (start=%dB)",
                    bind_map->push_ranges[i].index,
                    bind_map->push_ranges[i].start * 32);
            break;

         case ANV_DESCRIPTOR_SET_DESCRIPTORS:
            fprintf(stream, "Descriptor buffer for set %d (start=%dB)",
                    bind_map->push_ranges[i].index,
                    bind_map->push_ranges[i].start * 32);
               break;

         case ANV_DESCRIPTOR_SET_COLOR_ATTACHMENTS:
            UNREACHABLE("Color attachments can't be pushed");

         case ANV_DESCRIPTOR_SET_PER_PRIM_PADDING:
            fprintf(stream, "Per primitive alignment (gfx libs & mesh)");
            break;

         default:
            fprintf(stream, "UBO (set=%d binding=%d start=%dB)",
                    bind_map->push_ranges[i].set,
                    bind_map->push_ranges[i].index,
                    bind_map->push_ranges[i].start * 32);
            break;
            }
         fprintf(stream, "\n");
      }
      fprintf(stream, "\n");

   }

   /* Creating this is far cheaper than it looks.  It's perfectly fine to
    * do it for every binary.
    */
   if (shader_data->info->stage == MESA_SHADER_FRAGMENT) {
      const struct brw_wm_prog_data *wm_prog_data = &shader_data->prog_data.wm;

      if (wm_prog_data->dispatch_8 ||
          wm_prog_data->dispatch_multi) {
         brw_disassemble_with_errors(&device->physical->compiler->isa,
                                     shader_data->code, 0, NULL, stream);
      }

      if (wm_prog_data->dispatch_16) {
         brw_disassemble_with_errors(&device->physical->compiler->isa,
                                     shader_data->code,
                                     wm_prog_data->prog_offset_16, NULL, stream);
      }

      if (wm_prog_data->dispatch_32) {
         brw_disassemble_with_errors(&device->physical->compiler->isa,
                                     shader_data->code,
                                     wm_prog_data->prog_offset_32, NULL, stream);
      }
   } else {
      brw_disassemble_with_errors(&device->physical->compiler->isa,
                                  shader_data->code, 0, NULL, stream);
   }

   fclose(stream);

   /* Copy it to a ralloc'd thing */
   output->asm_str = ralloc_size(mem_ctx, stream_size + 1);
   memcpy(output->asm_str, stream_data, stream_size);
   output->asm_str[stream_size] = 0;
   output->asm_str_len = stream_size + 1;

   free(stream_data);
}

VkResult
anv_shader_create(struct anv_device *device,
                  mesa_shader_stage stage,
                  void *mem_ctx,
                  struct anv_shader_data *shader_data,
                  const VkAllocationCallbacks *pAllocator,
                  struct vk_shader **shader_out)
{
   const bool save_internal_representations = shader_data->info &&
      (shader_data->info->flags & VK_SHADER_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_MESA);

   struct internal_representation internal_representations = {0};
   if (save_internal_representations) {
      get_internal_representation_data(&internal_representations, device,
                                       shader_data, mem_ctx);
   }

   const uint32_t cmd_data_dwords = anv_genX(device->info, shader_cmd_size)(
      device, stage);

   /* We never need this at runtime */
   shader_data->prog_data.base.param = NULL;

   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct anv_shader, shader, 1);
   VK_MULTIALLOC_DECL(&ma, uint32_t, cmd_data, cmd_data_dwords);
   VK_MULTIALLOC_DECL_SIZE(&ma, void, obj_key_data, brw_prog_key_size(stage));
   VK_MULTIALLOC_DECL_SIZE(&ma, struct brw_stage_prog_data, prog_data,
                           brw_prog_data_size(stage));
   VK_MULTIALLOC_DECL(&ma, struct intel_shader_reloc, prog_data_relocs,
                      shader_data->prog_data.base.num_relocs);

   VK_MULTIALLOC_DECL_SIZE(&ma, nir_xfb_info, xfb_info,
                           shader_data->xfb_info == NULL ? 0 :
                           nir_xfb_info_size(shader_data->xfb_info->output_count));

   VK_MULTIALLOC_DECL(&ma, struct anv_pipeline_binding, surface_to_descriptor,
                      shader_data->bind_map.surface_count);
   VK_MULTIALLOC_DECL(&ma, struct anv_pipeline_binding, sampler_to_descriptor,
                      shader_data->bind_map.sampler_count);
   VK_MULTIALLOC_DECL(&ma, struct anv_pipeline_embedded_sampler_binding,
                      embedded_sampler_to_binding,
                      shader_data->bind_map.embedded_sampler_count);
   VK_MULTIALLOC_DECL(&ma, struct anv_embedded_sampler *, embedded_samplers,
                      shader_data->bind_map.embedded_sampler_count);
   VK_MULTIALLOC_DECL(&ma, char, nir_str, internal_representations.nir_str_len);
   VK_MULTIALLOC_DECL(&ma, char, asm_str, internal_representations.asm_str_len);

   if (!vk_shader_multizalloc(&device->vk, &ma, &anv_shader_ops,
                              stage, pAllocator))
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result;
   if (shader_data->bind_map.embedded_sampler_count > 0) {
      shader->embedded_samplers = embedded_samplers;
      result = anv_device_get_embedded_samplers(
         device, embedded_samplers, &shader_data->bind_map);
      if (result != VK_SUCCESS)
         goto error_shader;
   }

   shader->kernel =
      anv_state_pool_alloc(&device->instruction_state_pool,
                           shader_data->prog_data.base.program_size, 64);
   if (shader->kernel.alloc_size == 0) {
      result = vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      goto error_embedded_samplers;
   }

   if (save_internal_representations) {
      shader->nir_str = nir_str;
      memcpy(shader->nir_str, internal_representations.nir_str,
             internal_representations.nir_str_len);
      shader->asm_str = asm_str;
      memcpy(shader->asm_str, internal_representations.asm_str,
             internal_representations.asm_str_len);
   }

   memcpy(prog_data, &shader_data->prog_data, brw_prog_data_size(stage));

   typed_memcpy(prog_data_relocs,
                shader_data->prog_data.base.relocs,
                shader_data->prog_data.base.num_relocs);
   prog_data->relocs = prog_data_relocs;

   shader->prog_data = prog_data;

   shader->num_stats = shader_data->num_stats;
   typed_memcpy(shader->stats, shader_data->stats, shader_data->num_stats);

   if (shader_data->xfb_info) {
      *xfb_info = *shader_data->xfb_info;
      typed_memcpy(xfb_info->outputs, shader_data->xfb_info->outputs,
                   shader_data->xfb_info->output_count);
      shader->xfb_info = xfb_info;
   }

   typed_memcpy(&shader->push_desc_info, &shader_data->push_desc_info, 1);

   typed_memcpy(&shader->bind_map, &shader_data->bind_map, 1);
   typed_memcpy(surface_to_descriptor,
                shader_data->bind_map.surface_to_descriptor,
                shader_data->bind_map.surface_count);
   typed_memcpy(sampler_to_descriptor,
                shader_data->bind_map.sampler_to_descriptor,
                shader_data->bind_map.sampler_count);
   typed_memcpy(embedded_sampler_to_binding,
                shader_data->bind_map.embedded_sampler_to_binding,
                shader_data->bind_map.embedded_sampler_count);
   typed_memcpy(shader->bind_map.input_attachments,
                shader_data->bind_map.input_attachments,
                ARRAY_SIZE(shader_data->bind_map.input_attachments));
   shader->bind_map.surface_to_descriptor = surface_to_descriptor;
   shader->bind_map.sampler_to_descriptor = sampler_to_descriptor;
   shader->bind_map.embedded_sampler_to_binding = embedded_sampler_to_binding;

   shader->instance_multiplier = shader_data->instance_multiplier;

   result = anv_shader_reloc(device, shader_data->code, shader, pAllocator);
   if (result != VK_SUCCESS)
      goto error_embedded_samplers;

   memcpy(shader->kernel.map, shader_data->code,
          shader_data->prog_data.base.program_size);

   if (mesa_shader_stage_is_rt(shader->vk.stage)) {
      const struct brw_bs_prog_data *bs_prog_data =
         (const struct brw_bs_prog_data *)shader->prog_data;
      shader->vk.stack_size = bs_prog_data->max_stack_size;
   }
   shader->vk.scratch_size = shader->prog_data->total_scratch;
   shader->vk.ray_queries = shader->prog_data->ray_queries;

   result =
      anv_reloc_list_init(&shader->relocs, &device->vk.alloc,
                          device->physical->uses_relocs);
   if (result != VK_SUCCESS)
      goto error_embedded_samplers;

   struct anv_batch batch = {};
   anv_batch_set_storage(&batch, ANV_NULL_ADDRESS,
                         cmd_data, 4 * cmd_data_dwords);
   batch.relocs = &shader->relocs;
   shader->cmd_data = cmd_data;
   anv_genX(device->info, shader_emit)(&batch, device, shader);

   *shader_out = &shader->vk;

   return VK_SUCCESS;

 error_embedded_samplers:
   for (uint32_t s = 0; s < shader->bind_map.embedded_sampler_count; s++)
      anv_embedded_sampler_unref(device, shader->embedded_samplers[s]);
   anv_state_pool_free(&device->instruction_state_pool, shader->kernel);
 error_shader:
   anv_state_pool_free(&device->instruction_state_pool, shader->kernel);
   vk_shader_free(&device->vk, pAllocator, &shader->vk);
   return result;
}
