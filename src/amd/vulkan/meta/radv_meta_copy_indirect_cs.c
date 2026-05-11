/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */
#include "nir/radv_meta_nir.h"
#include "radv_formats.h"
#include "radv_meta.h"
#include "vk_shader_module.h"

/* Copy memory->memory. */
static VkResult
get_compute_copy_memory_indirect_preprocess_pipeline(struct radv_device *device, VkPipeline *pipeline_out,
                                                     VkPipelineLayout *layout_out)
{
   enum radv_meta_object_key_type key = RADV_META_OBJECT_KEY_COPY_MEMORY_INDIRECT_PREPROCESS_CS;
   VkResult result;

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .size = 24,
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

   nir_shader *cs = radv_meta_nir_build_copy_memory_indirect_preprocess_cs();

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

static VkResult
radv_compute_copy_memory_indirect_preprocess(struct radv_cmd_buffer *cmd_buffer,
                                             const VkCopyMemoryIndirectInfoKHR *pCopyMemoryIndirectInfo,
                                             uint64_t upload_addr)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const uint32_t copy_count = pCopyMemoryIndirectInfo->copyCount;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   result = get_compute_copy_memory_indirect_preprocess_pipeline(device, &pipeline, &layout);
   if (result != VK_SUCCESS)
      return result;

   radv_meta_bind_compute_pipeline(cmd_buffer, pipeline);

   const uint32_t constants[6] = {
      pCopyMemoryIndirectInfo->copyAddressRange.address,
      pCopyMemoryIndirectInfo->copyAddressRange.address >> 32,
      pCopyMemoryIndirectInfo->copyAddressRange.stride,
      pCopyMemoryIndirectInfo->copyAddressRange.stride >> 32,
      upload_addr,
      upload_addr >> 32,
   };

   radv_meta_push_constants(cmd_buffer, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), constants);

   radv_unaligned_dispatch(cmd_buffer, copy_count, 1, 1);

   return VK_SUCCESS;
}

static VkResult
get_compute_copy_memory_indirect_pipeline(struct radv_device *device, VkPipeline *pipeline_out,
                                          VkPipelineLayout *layout_out)
{
   enum radv_meta_object_key_type key = RADV_META_OBJECT_KEY_COPY_MEMORY_INDIRECT_CS;
   VkResult result;

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .size = 8,
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

   nir_shader *cs = radv_meta_nir_build_copy_memory_indirect_cs();

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
radv_compute_copy_memory_indirect(struct radv_cmd_buffer *cmd_buffer,
                                  const VkCopyMemoryIndirectInfoKHR *pCopyMemoryIndirectInfo)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const uint32_t copy_count = pCopyMemoryIndirectInfo->copyCount;
   VkPipelineLayout layout;
   uint32_t alloc_offset;
   uint32_t *alloc_ptr;
   VkPipeline pipeline;
   VkResult result;

   if (!radv_cmd_buffer_upload_alloc_aligned(cmd_buffer, copy_count * sizeof(VkDispatchIndirectCommand), 4,
                                             &alloc_offset, (void *)&alloc_ptr)) {
      vk_command_buffer_set_error(&cmd_buffer->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      return;
   }

   const uint64_t upload_addr = radv_buffer_get_va(cmd_buffer->upload.upload_bo) + alloc_offset;

   result = radv_compute_copy_memory_indirect_preprocess(cmd_buffer, pCopyMemoryIndirectInfo, upload_addr);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   /* Synchronize the preprocess dispatch. */
   cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_INV_VCACHE |
                                   radv_src_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                         VK_ACCESS_2_SHADER_WRITE_BIT, 0, NULL, NULL);

   result = get_compute_copy_memory_indirect_pipeline(device, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   radv_meta_bind_compute_pipeline(cmd_buffer, pipeline);

   for (uint32_t i = 0; i < copy_count; i++) {
      const uint64_t copy_addr =
         pCopyMemoryIndirectInfo->copyAddressRange.address + i * pCopyMemoryIndirectInfo->copyAddressRange.stride;

      radv_meta_push_constants(cmd_buffer, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(copy_addr), &copy_addr);

      const struct radv_dispatch_info info = {
         .indirect_va = upload_addr + i * sizeof(VkDispatchIndirectCommand),
         .unaligned = true,
      };

      radv_compute_dispatch(cmd_buffer, &info);
   }
}

/* Copy memory->image. */
static VkResult
get_compute_copy_memory_to_image_indirect_preprocess_pipeline(struct radv_device *device, VkPipeline *pipeline_out,
                                                              VkPipelineLayout *layout_out)
{
   enum radv_meta_object_key_type key = RADV_META_OBJECT_KEY_COPY_MEMORY_TO_IMAGE_INDIRECT_PREPROCESS_CS;
   VkResult result;

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .size = 36,
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

   nir_shader *cs = radv_meta_nir_build_copy_memory_to_image_indirect_preprocess_cs();

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

static VkResult
radv_compute_copy_memory_to_image_indirect_preprocess(
   struct radv_cmd_buffer *cmd_buffer, const VkCopyMemoryToImageIndirectInfoKHR *pCopyMemoryToImageIndirectInfo,
   uint64_t upload_addr)
{
   VK_FROM_HANDLE(radv_image, dst_image, pCopyMemoryToImageIndirectInfo->dstImage);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const uint32_t copy_count = pCopyMemoryToImageIndirectInfo->copyCount;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   result = get_compute_copy_memory_to_image_indirect_preprocess_pipeline(device, &pipeline, &layout);
   if (result != VK_SUCCESS)
      return result;

   radv_meta_bind_compute_pipeline(cmd_buffer, pipeline);

   const struct util_format_description *fmt = vk_format_description(dst_image->vk.format);
   const uint32_t texel_scale = vk_format_is_96bit(dst_image->vk.format) ? 3 : 1;

   const uint32_t constants[9] = {
      pCopyMemoryToImageIndirectInfo->copyAddressRange.address,
      pCopyMemoryToImageIndirectInfo->copyAddressRange.address >> 32,
      pCopyMemoryToImageIndirectInfo->copyAddressRange.stride,
      pCopyMemoryToImageIndirectInfo->copyAddressRange.stride >> 32,
      upload_addr,
      upload_addr >> 32,
      fmt->block.width,
      fmt->block.height,
      texel_scale,
   };

   radv_meta_push_constants(cmd_buffer, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), constants);

   radv_unaligned_dispatch(cmd_buffer, copy_count, 1, 1);

   return VK_SUCCESS;
}

static VkResult
get_compute_copy_memory_to_image_indirect_pipeline_layout(struct radv_device *device, VkPipelineLayout *layout_out)
{
   enum radv_meta_object_key_type key = RADV_META_OBJECT_KEY_COPY_MEMORY_TO_IMAGE_INDIRECT_CS;

   const VkDescriptorSetLayoutBinding binding = {
      .binding = 1,
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
      .size = 36,
   };

   return vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, &desc_info, &pc_range, &key, sizeof(key),
                                      layout_out);
}

struct radv_copy_memory_to_image_indirect_key {
   enum radv_meta_object_key_type type;
   uint8_t fmt_block_width;
   uint8_t fmt_block_height;
   uint8_t fmt_block_depth;
   uint8_t fmt_element_size_B;
   bool is_3d;
};

static VkResult
get_compute_copy_memory_to_image_indirect_pipeline(struct radv_device *device, const struct radv_image *image,
                                                   VkImageAspectFlags aspect_mask, VkPipeline *pipeline_out,
                                                   VkPipelineLayout *layout_out)
{
   VkFormat aspect_format = vk_format_get_aspect_format(image->vk.format, aspect_mask);
   const struct util_format_description *fmt = vk_format_description(aspect_format);
   const bool is_3d = image->vk.image_type == VK_IMAGE_TYPE_3D;
   struct radv_copy_memory_to_image_indirect_key key;
   VkResult result;

   result = get_compute_copy_memory_to_image_indirect_pipeline_layout(device, layout_out);
   if (result != VK_SUCCESS)
      return result;

   memset(&key, 0, sizeof(key));
   key.type = RADV_META_OBJECT_KEY_COPY_MEMORY_TO_IMAGE_INDIRECT_CS;
   key.fmt_block_width = fmt->block.width;
   key.fmt_block_height = fmt->block.height;
   key.fmt_block_depth = fmt->block.depth;
   key.fmt_element_size_B = fmt->block.bits / 8;
   key.is_3d = is_3d;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *cs = radv_meta_nir_build_copy_memory_to_image_indirect_cs(
      key.fmt_block_width, key.fmt_block_height, key.fmt_block_depth, key.fmt_element_size_B, key.is_3d);

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
radv_compute_copy_memory_to_image_indirect(struct radv_cmd_buffer *cmd_buffer,
                                           const VkCopyMemoryToImageIndirectInfoKHR *pCopyMemoryToImageIndirectInfo)
{
   VK_FROM_HANDLE(radv_image, dst_image, pCopyMemoryToImageIndirectInfo->dstImage);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   VkImageLayout dst_image_layout = pCopyMemoryToImageIndirectInfo->dstImageLayout;
   const uint32_t copy_count = pCopyMemoryToImageIndirectInfo->copyCount;
   struct radv_cmd_stream *cs = cmd_buffer->cs;
   uint32_t texel_scale = 1;
   VkPipelineLayout layout;
   uint32_t alloc_offset;
   uint32_t *alloc_ptr;
   VkPipeline pipeline;
   VkResult result;

   if (!radv_cmd_buffer_upload_alloc_aligned(cmd_buffer, copy_count * sizeof(VkDispatchIndirectCommand), 4,
                                             &alloc_offset, (void *)&alloc_ptr)) {
      vk_command_buffer_set_error(&cmd_buffer->vk, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      return;
   }

   const uint64_t upload_addr = radv_buffer_get_va(cmd_buffer->upload.upload_bo) + alloc_offset;

   result =
      radv_compute_copy_memory_to_image_indirect_preprocess(cmd_buffer, pCopyMemoryToImageIndirectInfo, upload_addr);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   /* Synchronize the preprocess dispatch. */
   cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_INV_VCACHE |
                                   radv_src_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                         VK_ACCESS_2_SHADER_WRITE_BIT, 0, NULL, NULL);

   for (uint32_t i = 0; i < copy_count; i++) {
      const VkImageSubresourceLayers *imageSubresource = &pCopyMemoryToImageIndirectInfo->pImageSubresources[i];
      const VkImageAspectFlags aspect_mask = imageSubresource->aspectMask;
      const unsigned bind_idx = dst_image->disjoint ? radv_plane_from_aspect(aspect_mask) : 0;
      struct radv_image_view dst_iview;

      /* The Vulkan spec 1.4.343 says:
       *
       * "VUID-VkCopyMemoryToImageIndirectInfoKHR-commandBuffer-07674 If the queue family used to
       *  create the VkCommandPool which commandBuffer was allocated from does not support
       *  VK_QUEUE_GRAPHICS_BIT, for each region, the aspectMask member of pImageSubresources must
       *  not be VK_IMAGE_ASPECT_DEPTH_BIT or VK_IMAGE_ASPECT_STENCIL_BIT".
       *
       * So, depth/stencil images aren't allowed on compute queue. Though, there is an exception for
       * depth/stencil images that might not be usable as RT targets on <= GFX8 and need to use
       * compute copies instead.
       */
      assert(!(aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) ||
             (dst_image->planes[0].surface.flags & RADEON_SURF_NO_RENDER_TARGET));

      result = get_compute_copy_memory_to_image_indirect_pipeline(device, dst_image, imageSubresource->aspectMask,
                                                                  &pipeline, &layout);
      if (result != VK_SUCCESS) {
         vk_command_buffer_set_error(&cmd_buffer->vk, result);
         return;
      }

      radv_meta_bind_compute_pipeline(cmd_buffer, pipeline);

      radv_cs_add_buffer(device->ws, cs->b, dst_image->bindings[bind_idx].bo);

      struct radv_meta_blit2d_surf img_bsurf = radv_blit_surf_for_image_level_layer(
         dst_image, pCopyMemoryToImageIndirectInfo->dstImageLayout, imageSubresource);

      if (!radv_is_buffer_format_supported(img_bsurf.format, NULL)) {
         const uint32_t queue_mask = radv_image_queue_family_mask(dst_image, cmd_buffer->qf, cmd_buffer->qf);
         const VkFormat raw_format = vk_format_for_size(vk_format_get_blocksize(img_bsurf.format));

         if (!radv_dcc_formats_compatible(pdev->info.gfx_level, img_bsurf.format, raw_format, NULL) &&
             radv_layout_dcc_compressed(device, dst_image, imageSubresource->mipLevel, dst_image_layout, queue_mask)) {
            radv_describe_barrier_start(cmd_buffer, RGP_BARRIER_UNKNOWN_REASON);

            radv_decompress_dcc(cmd_buffer, dst_image,
                                &(VkImageSubresourceRange){
                                   .aspectMask = imageSubresource->aspectMask,
                                   .baseMipLevel = imageSubresource->mipLevel,
                                   .levelCount = 1,
                                   .baseArrayLayer = imageSubresource->baseArrayLayer,
                                   .layerCount = vk_image_subresource_layer_count(&dst_image->vk, imageSubresource),
                                });
            img_bsurf.disable_compression = true;

            radv_describe_barrier_end(cmd_buffer);
         }

         img_bsurf.format = raw_format;
      }

      radv_meta_bind_compute_pipeline(cmd_buffer, pipeline);

      if (vk_format_is_96bit(img_bsurf.format)) {
         img_bsurf.format = radv_meta_get_96bit_channel_format(img_bsurf.format);
         texel_scale = 3;
      }

      const uint32_t slice_count = vk_image_subresource_layer_count(&dst_image->vk, imageSubresource);

      for (uint32_t slice = 0; slice < slice_count; slice++) {
         const VkImageViewUsageCreateInfo iview_usage_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
            .usage = VK_IMAGE_USAGE_STORAGE_BIT,
         };

         radv_image_view_init(&dst_iview, device,
                              &(VkImageViewCreateInfo){
                                 .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                 .pNext = &iview_usage_info,
                                 .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
                                 .image = radv_image_to_handle(dst_image),
                                 .viewType = radv_meta_get_view_type(dst_image),
                                 .format = img_bsurf.format,
                                 .subresourceRange =
                                    {
                                       .aspectMask = imageSubresource->aspectMask,
                                       .baseMipLevel = imageSubresource->mipLevel,
                                       .levelCount = 1,
                                       .baseArrayLayer = imageSubresource->baseArrayLayer + slice,
                                       .layerCount = 1,
                                    },
                              },
                              &(struct radv_image_view_extra_create_info){
                                 .disable_compression = img_bsurf.disable_compression,
                              });

         radv_meta_bind_descriptors(
            cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 1,
            (VkDescriptorGetInfoEXT[]){{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                                        .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                        .data.pStorageImage = (VkDescriptorImageInfo[]){
                                           {
                                              .sampler = VK_NULL_HANDLE,
                                              .imageView = radv_image_view_to_handle(&dst_iview),
                                              .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                           },
                                        }}});

         uint32_t buffer_view_desc[4];
         radv_make_texel_buffer_descriptor(device, 0, img_bsurf.format, ~0, buffer_view_desc);

         const uint64_t copy_addr = pCopyMemoryToImageIndirectInfo->copyAddressRange.address +
                                    i * pCopyMemoryToImageIndirectInfo->copyAddressRange.stride;

         const uint32_t constants[9] = {
            copy_addr,
            copy_addr >> 32,
            slice,
            buffer_view_desc[0],
            buffer_view_desc[1],
            buffer_view_desc[2],
            buffer_view_desc[3],
            imageSubresource->baseArrayLayer,
            texel_scale,
         };

         radv_meta_push_constants(cmd_buffer, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), constants);

         const struct radv_dispatch_info info = {
            .indirect_va = upload_addr + i * sizeof(VkDispatchIndirectCommand),
            .unaligned = true,
         };

         radv_compute_dispatch(cmd_buffer, &info);

         radv_image_view_finish(&dst_iview);
      }
   }
}
