/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir/radv_meta_nir.h"
#include "radv_entrypoints.h"
#include "radv_meta.h"

static VkResult
get_clear_hiz_pipeline_layout(struct radv_device *device, VkPipelineLayout *layout_out)
{
   enum radv_meta_object_key_type key = RADV_META_OBJECT_KEY_CLEAR_HIZ;

   const VkDescriptorSetLayoutBinding binding = {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
   };

   const VkDescriptorSetLayoutCreateInfo desc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
      .bindingCount = 1,
      .pBindings = &binding,
   };

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .size = 4,
   };

   return vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, &desc_info, &pc_range, &key, sizeof(key),
                                      layout_out);
}

struct radv_clear_hiz_key {
   enum radv_meta_object_key_type type;
   uint8_t samples;
};

static VkResult
get_clear_hiz_pipeline(struct radv_device *device, const struct radv_image *image, VkPipeline *pipeline_out,
                       VkPipelineLayout *layout_out)
{
   const uint32_t samples = image->vk.samples;
   struct radv_clear_hiz_key key;
   VkResult result;

   result = get_clear_hiz_pipeline_layout(device, layout_out);
   if (result != VK_SUCCESS)
      return result;

   memset(&key, 0, sizeof(key));
   key.type = RADV_META_OBJECT_KEY_CLEAR_HIZ;
   key.samples = samples;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *cs = radv_meta_nir_build_clear_hiz_compute_shader(device, samples);

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
radv_clear_hiz(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image, const VkImageSubresourceRange *range,
               uint32_t value)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radeon_surf *surf = &image->planes[0].surface;
   struct radv_meta_saved_state saved_state;
   struct radv_image_view iview;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   /* Clearing HiZ should only be needed to implement a workaround on GFX12. */
   assert(image->hiz_valid_offset);

   result = get_clear_hiz_pipeline(device, image, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   cmd_buffer->state.flush_bits |=
      radv_src_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0, image, range);

   radv_meta_save(&saved_state, cmd_buffer,
                  RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_CONSTANTS);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   const uint32_t base_width = surf->u.gfx9.zs.hiz.width_in_tiles;
   const uint32_t base_height = surf->u.gfx9.zs.hiz.height_in_tiles;

   for (uint32_t l = 0; l < vk_image_subresource_level_count(&image->vk, range); l++) {
      uint32_t width, height;

      width = u_minify(base_width, range->baseMipLevel + l);
      height = u_minify(base_height, range->baseMipLevel + l);

      for (uint32_t s = 0; s < vk_image_subresource_layer_count(&image->vk, range); s++) {
         radv_hiz_image_view_init(&iview, device,
                                  &(VkImageViewCreateInfo){
                                     .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                     .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
                                     .image = radv_image_to_handle(image),
                                     .viewType = radv_meta_get_view_type(image),
                                     .format = image->vk.format,
                                     .subresourceRange =
                                        {
                                           .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                                           .baseMipLevel = range->baseMipLevel + l,
                                           .levelCount = 1,
                                           .baseArrayLayer = range->baseArrayLayer + s,
                                           .layerCount = 1,
                                        },
                                  });

         radv_meta_bind_descriptors(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 1,
                                    (VkDescriptorGetInfoEXT[]){
                                       {
                                          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                                          .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                          .data.pStorageImage =
                                             (VkDescriptorImageInfo[]){
                                                {
                                                   .sampler = VK_NULL_HANDLE,
                                                   .imageView = radv_image_view_to_handle(&iview),
                                                   .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                                },
                                             },
                                       },
                                    });

         const VkPushConstantsInfo pc_info = {
            .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO_KHR,
            .layout = layout,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = sizeof(value),
            .pValues = &value,
         };

         radv_CmdPushConstants2(radv_cmd_buffer_to_handle(cmd_buffer), &pc_info);

         radv_unaligned_dispatch(cmd_buffer, width, height, 1);

         radv_image_view_finish(&iview);
      }
   }

   radv_meta_restore(&saved_state, cmd_buffer);

   cmd_buffer->state.flush_bits |=
      RADV_CMD_FLAG_CS_PARTIAL_FLUSH | radv_src_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                             VK_ACCESS_2_SHADER_WRITE_BIT, 0, image, range);
}
