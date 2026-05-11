/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir/radv_meta_nir.h"
#include "ac_surface.h"
#include "radv_meta.h"
#include "vk_shader_module.h"

static VkResult
get_pipeline(struct radv_device *device, struct radv_image *image, VkPipeline *pipeline_out,
             VkPipelineLayout *layout_out)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   enum radv_meta_object_key_type key = RADV_META_OBJECT_KEY_COPY_VRS_HTILE;
   VkResult result;

   const VkDescriptorSetLayoutBinding bindings[] = {
      {
         .binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
   };

   const VkDescriptorSetLayoutCreateInfo desc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
      .bindingCount = 1,
      .pBindings = bindings,
   };

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .size = 28,
   };

   result = vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, &desc_info, &pc_range, &key,
                                        sizeof(key), layout_out);
   if (result != VK_SUCCESS)
      return result;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *cs = radv_meta_nir_build_copy_vrs_htile_shader(pdev->info.gfx_level, pdev->info.gb_addr_config,
                                                              &image->planes[0].surface);

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

void
radv_copy_vrs_htile(struct radv_cmd_buffer *cmd_buffer, struct radv_image_view *vrs_iview, const VkRect2D *rect,
                    struct radv_image *dst_image, uint64_t htile_va, bool read_htile_value)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   result = get_pipeline(device, dst_image, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   cmd_buffer->state.flush_bits |= radv_src_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0, NULL, NULL);

   radv_meta_bind_compute_pipeline(cmd_buffer, pipeline);

   radv_meta_bind_descriptors(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 1,
                              (VkDescriptorGetInfoEXT[]){{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                                                          .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                                          .data.pSampledImage = (VkDescriptorImageInfo[]){
                                                             {
                                                                .sampler = VK_NULL_HANDLE,
                                                                .imageView = radv_image_view_to_handle(vrs_iview),
                                                                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                                             },
                                                          }}});

   const unsigned constants[7] = {
      htile_va,
      htile_va >> 32,
      rect->offset.x,
      rect->offset.y,
      dst_image->planes[0].surface.meta_pitch,
      dst_image->planes[0].surface.meta_slice_size,
      read_htile_value,
   };

   radv_meta_push_constants(cmd_buffer, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), constants);

   uint32_t width = DIV_ROUND_UP(rect->extent.width, 8);
   uint32_t height = DIV_ROUND_UP(rect->extent.height, 8);

   radv_unaligned_dispatch(cmd_buffer, width, height, 1);

   cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_INV_VCACHE |
                                   radv_src_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                         VK_ACCESS_2_SHADER_WRITE_BIT, 0, NULL, NULL);
}
