/*
 * Copyright © 2016 Dave Airlie
 *
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdbool.h>

#include "nir/radv_meta_nir.h"
#include "radv_entrypoints.h"
#include "radv_formats.h"
#include "radv_meta.h"
#include "vk_format.h"
#include "vk_shader_module.h"

static enum radv_meta_resolve_compute_type
radv_meta_get_resolve_compute_type(VkFormat format)
{
   if (vk_format_is_int(format))
      return RADV_META_RESOLVE_COMPUTE_INTEGER;

   if (vk_format_is_unorm(format) || vk_format_is_snorm(format)) {
      uint32_t max_bit_size = 0;
      for (uint32_t i = 0; i < vk_format_get_nr_components(format); i++)
         max_bit_size = MAX2(max_bit_size, vk_format_get_component_bits(format, UTIL_FORMAT_COLORSPACE_RGB, i));

      /* srgb formats are all 8-bit */
      if (vk_format_is_srgb(format)) {
         assert(max_bit_size == 8);
         return RADV_META_RESOLVE_COMPUTE_NORM_SRGB;
      }

      if (max_bit_size <= 10)
         return RADV_META_RESOLVE_COMPUTE_NORM;
   }

   return RADV_META_RESOLVE_COMPUTE_FLOAT;
}

static VkResult
create_layout(struct radv_device *device, VkPipelineLayout *layout_out)
{
   enum radv_meta_object_key_type key = RADV_META_OBJECT_KEY_RESOLVE_CS;

   const VkDescriptorSetLayoutBinding bindings[] = {
      {
         .binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
      {
         .binding = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
   };

   const VkDescriptorSetLayoutCreateInfo desc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
      .bindingCount = 2,
      .pBindings = bindings,
   };

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .size = 16,
   };

   return vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, &desc_info, &pc_range, &key, sizeof(key),
                                      layout_out);
}

struct radv_resolve_color_cs_key {
   enum radv_meta_object_key_type type;
   enum radv_meta_resolve_compute_type resolve_type;
   uint8_t samples;
};

static VkResult
get_color_resolve_pipeline(struct radv_device *device, struct radv_image_view *src_iview, VkPipeline *pipeline_out,
                           VkPipelineLayout *layout_out)
{
   const enum radv_meta_resolve_compute_type type = radv_meta_get_resolve_compute_type(src_iview->vk.format);
   uint32_t samples = src_iview->image->vk.samples;
   struct radv_resolve_color_cs_key key;
   VkResult result;

   result = create_layout(device, layout_out);
   if (result != VK_SUCCESS)
      return result;

   memset(&key, 0, sizeof(key));
   key.type = RADV_META_OBJECT_KEY_RESOLVE_COLOR_CS;
   key.resolve_type = type;
   key.samples = samples;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *cs = radv_meta_nir_build_resolve_compute_shader(device, type, samples);

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
emit_resolve(struct radv_cmd_buffer *cmd_buffer, struct radv_image_view *src_iview, struct radv_image_view *dst_iview,
             const VkOffset2D *src_offset, const VkOffset2D *dst_offset, const VkExtent2D *resolve_extent)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   result = get_color_resolve_pipeline(device, src_iview, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   radv_meta_bind_descriptors(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 2,
                              (VkDescriptorGetInfoEXT[]){{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                                                          .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                                          .data.pSampledImage =
                                                             (VkDescriptorImageInfo[]){
                                                                {.sampler = VK_NULL_HANDLE,
                                                                 .imageView = radv_image_view_to_handle(src_iview),
                                                                 .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
                                                             }},
                                                         {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                                                          .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                          .data.pStorageImage = (VkDescriptorImageInfo[]){
                                                             {
                                                                .sampler = VK_NULL_HANDLE,
                                                                .imageView = radv_image_view_to_handle(dst_iview),
                                                                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                                             },
                                                          }}});

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   unsigned push_constants[4] = {
      src_offset->x,
      src_offset->y,
      dst_offset->x,
      dst_offset->y,
   };

   const VkPushConstantsInfoKHR pc_info = {
      .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO_KHR,
      .layout = layout,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .offset = 0,
      .size = sizeof(push_constants),
      .pValues = push_constants,
   };

   radv_CmdPushConstants2(radv_cmd_buffer_to_handle(cmd_buffer), &pc_info);

   radv_unaligned_dispatch(cmd_buffer, resolve_extent->width, resolve_extent->height, 1);
}

struct radv_resolve_ds_cs_key {
   enum radv_meta_object_key_type type;
   uint8_t index;
   uint8_t samples;
   VkResolveModeFlagBits resolve_mode;
};

static VkResult
get_depth_stencil_resolve_pipeline(struct radv_device *device, int samples, VkImageAspectFlags aspects,
                                   VkResolveModeFlagBits resolve_mode, VkPipeline *pipeline_out,
                                   VkPipelineLayout *layout_out)

{
   const enum radv_meta_resolve_type index =
      aspects == VK_IMAGE_ASPECT_DEPTH_BIT ? RADV_META_DEPTH_RESOLVE : RADV_META_STENCIL_RESOLVE;
   struct radv_resolve_ds_cs_key key;
   VkResult result;

   result = create_layout(device, layout_out);
   if (result != VK_SUCCESS)
      return result;

   memset(&key, 0, sizeof(key));
   key.type = RADV_META_OBJECT_KEY_RESOLVE_DS_CS;
   key.index = index;
   key.samples = samples;
   key.resolve_mode = resolve_mode;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *cs = radv_meta_nir_build_depth_stencil_resolve_compute_shader(device, samples, index, resolve_mode);

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
radv_meta_resolve_compute_image(struct radv_cmd_buffer *cmd_buffer, struct radv_image *src_image, VkFormat src_format,
                                VkImageLayout src_image_layout, struct radv_image *dst_image, VkFormat dst_format,
                                VkImageLayout dst_image_layout, const VkImageResolve2 *region)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_meta_saved_state saved_state;

   /* For partial resolves, DCC should be decompressed before resolving
    * because the metadata is re-initialized to the uncompressed after.
    */
   uint32_t queue_mask = radv_image_queue_family_mask(dst_image, cmd_buffer->qf, cmd_buffer->qf);

   if (!radv_image_use_dcc_image_stores(device, dst_image) &&
       radv_layout_dcc_compressed(device, dst_image, region->dstSubresource.mipLevel, dst_image_layout, queue_mask) &&
       (region->dstOffset.x || region->dstOffset.y || region->dstOffset.z ||
        region->extent.width != dst_image->vk.extent.width || region->extent.height != dst_image->vk.extent.height ||
        region->extent.depth != dst_image->vk.extent.depth)) {
      radv_decompress_dcc(cmd_buffer, dst_image,
                          &(VkImageSubresourceRange){
                             .aspectMask = region->dstSubresource.aspectMask,
                             .baseMipLevel = region->dstSubresource.mipLevel,
                             .levelCount = 1,
                             .baseArrayLayer = region->dstSubresource.baseArrayLayer,
                             .layerCount = vk_image_subresource_layer_count(&dst_image->vk, &region->dstSubresource),
                          });
   }

   radv_meta_save(&saved_state, cmd_buffer,
                  RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_CONSTANTS | RADV_META_SAVE_DESCRIPTORS);

   assert(region->srcSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
   assert(region->dstSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
   assert(vk_image_subresource_layer_count(&src_image->vk, &region->srcSubresource) ==
          vk_image_subresource_layer_count(&dst_image->vk, &region->dstSubresource));

   const uint32_t dst_base_layer =
      dst_image->vk.image_type == VK_IMAGE_TYPE_3D ? region->dstOffset.z : region->dstSubresource.baseArrayLayer;

   const struct VkExtent3D extent = vk_image_sanitize_extent(&src_image->vk, region->extent);
   const struct VkOffset3D srcOffset = vk_image_sanitize_offset(&src_image->vk, region->srcOffset);
   const struct VkOffset3D dstOffset = vk_image_sanitize_offset(&dst_image->vk, region->dstOffset);
   const unsigned src_layer_count = vk_image_subresource_layer_count(&src_image->vk, &region->srcSubresource);

   for (uint32_t layer = 0; layer < src_layer_count; ++layer) {

      struct radv_image_view src_iview;
      radv_image_view_init(&src_iview, device,
                           &(VkImageViewCreateInfo){
                              .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                              .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
                              .image = radv_image_to_handle(src_image),
                              .viewType = VK_IMAGE_VIEW_TYPE_2D,
                              .format = src_format,
                              .subresourceRange =
                                 {
                                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                    .baseMipLevel = 0,
                                    .levelCount = 1,
                                    .baseArrayLayer = region->srcSubresource.baseArrayLayer + layer,
                                    .layerCount = 1,
                                 },
                           },
                           NULL);

      struct radv_image_view dst_iview;
      radv_image_view_init(&dst_iview, device,
                           &(VkImageViewCreateInfo){
                              .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                              .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
                              .image = radv_image_to_handle(dst_image),
                              .viewType = radv_meta_get_view_type(dst_image),
                              .format = vk_format_no_srgb(dst_format),
                              .subresourceRange =
                                 {
                                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                    .baseMipLevel = region->dstSubresource.mipLevel,
                                    .levelCount = 1,
                                    .baseArrayLayer = dst_base_layer + layer,
                                    .layerCount = 1,
                                 },
                           },
                           NULL);

      emit_resolve(cmd_buffer, &src_iview, &dst_iview, &(VkOffset2D){srcOffset.x, srcOffset.y},
                   &(VkOffset2D){dstOffset.x, dstOffset.y}, &(VkExtent2D){extent.width, extent.height});

      radv_image_view_finish(&src_iview);
      radv_image_view_finish(&dst_iview);
   }

   radv_meta_restore(&saved_state, cmd_buffer);

   if (!radv_image_use_dcc_image_stores(device, dst_image) &&
       radv_layout_dcc_compressed(device, dst_image, region->dstSubresource.mipLevel, dst_image_layout, queue_mask)) {

      cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_INV_VCACHE;

      VkImageSubresourceRange range = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .baseMipLevel = region->dstSubresource.mipLevel,
         .levelCount = 1,
         .baseArrayLayer = dst_base_layer,
         .layerCount = vk_image_subresource_layer_count(&dst_image->vk, &region->dstSubresource),
      };

      cmd_buffer->state.flush_bits |= radv_init_dcc(cmd_buffer, dst_image, &range, 0xffffffff);
   }
}

void
radv_meta_resolve_depth_stencil_cs(struct radv_cmd_buffer *cmd_buffer, struct radv_image *src_image,
                                   VkFormat src_format, VkImageLayout src_image_layout, struct radv_image *dst_image,
                                   VkFormat dst_format, VkImageLayout dst_image_layout,
                                   VkResolveModeFlagBits resolve_mode, const VkImageResolve2 *region)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_meta_saved_state saved_state;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   result = get_depth_stencil_resolve_pipeline(device, src_image->vk.samples, region->srcSubresource.aspectMask,
                                               resolve_mode, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   radv_decompress_resolve_src(cmd_buffer, src_image, src_image_layout, region);

   radv_meta_save(&saved_state, cmd_buffer,
                  RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_CONSTANTS);

   struct radv_image_view src_iview;
   radv_image_view_init(&src_iview, device,
                        &(VkImageViewCreateInfo){
                           .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                           .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
                           .image = radv_image_to_handle(src_image),
                           .viewType = VK_IMAGE_VIEW_TYPE_2D,
                           .format = src_format,
                           .subresourceRange =
                              {
                                 .aspectMask = region->srcSubresource.aspectMask,
                                 .baseMipLevel = region->srcSubresource.mipLevel,
                                 .levelCount = 1,
                                 .baseArrayLayer = region->srcSubresource.baseArrayLayer,
                                 .layerCount = region->srcSubresource.layerCount,
                              },
                        },
                        NULL);

   struct radv_image_view dst_iview;
   radv_image_view_init(&dst_iview, device,
                        &(VkImageViewCreateInfo){
                           .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                           .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
                           .image = radv_image_to_handle(dst_image),
                           .viewType = radv_meta_get_view_type(dst_image),
                           .format = dst_format,
                           .subresourceRange =
                              {
                                 .aspectMask = region->dstSubresource.aspectMask,
                                 .baseMipLevel = region->dstSubresource.mipLevel,
                                 .levelCount = 1,
                                 .baseArrayLayer = region->dstSubresource.baseArrayLayer,
                                 .layerCount = region->dstSubresource.layerCount,
                              },
                        },
                        NULL);

   radv_meta_bind_descriptors(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 2,
                              (VkDescriptorGetInfoEXT[]){
                                 {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                                  .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                  .data.pSampledImage =
                                     (VkDescriptorImageInfo[]){
                                        {.sampler = VK_NULL_HANDLE,
                                         .imageView = radv_image_view_to_handle(&src_iview),
                                         .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
                                     }},
                                 {
                                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                                    .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                    .data.pStorageImage =
                                       (VkDescriptorImageInfo[]){
                                          {
                                             .sampler = VK_NULL_HANDLE,
                                             .imageView = radv_image_view_to_handle(&dst_iview),
                                             .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                          },
                                       },
                                 },
                              });

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   const uint32_t push_constants[2] = {region->srcOffset.x, region->srcOffset.y};

   const VkPushConstantsInfoKHR pc_info = {
      .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO_KHR,
      .layout = layout,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .offset = 0,
      .size = sizeof(push_constants),
      .pValues = push_constants,
   };

   radv_CmdPushConstants2(radv_cmd_buffer_to_handle(cmd_buffer), &pc_info);

   radv_unaligned_dispatch(cmd_buffer, region->extent.width, region->extent.height, region->extent.depth);

   radv_image_view_finish(&src_iview);
   radv_image_view_finish(&dst_iview);

   radv_meta_restore(&saved_state, cmd_buffer);

   cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_INV_VCACHE |
                                   radv_src_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                         VK_ACCESS_2_SHADER_WRITE_BIT, 0, NULL, NULL);

   const uint32_t queue_mask = radv_image_queue_family_mask(dst_image, cmd_buffer->qf, cmd_buffer->qf);

   if (radv_layout_is_htile_compressed(device, dst_image, region->dstSubresource.mipLevel, dst_image_layout,
                                       queue_mask)) {
      VkImageSubresourceRange range = {
         .aspectMask = region->dstSubresource.aspectMask,
         .baseMipLevel = region->dstSubresource.mipLevel,
         .levelCount = 1,
         .baseArrayLayer = region->dstSubresource.baseArrayLayer,
         .layerCount = region->dstSubresource.layerCount,
      };

      uint32_t htile_value = radv_get_htile_initial_value(device, dst_image);

      cmd_buffer->state.flush_bits |= radv_clear_htile(cmd_buffer, dst_image, &range, htile_value, false);
   }
}
