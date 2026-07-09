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
get_compute_resolve_pipeline_layout(struct radv_device *device, VkPipelineLayout *layout_out)
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
      .size = 20,
   };

   return vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, &desc_info, &pc_range, &key, sizeof(key),
                                      layout_out);
}

struct radv_compute_resolve_key {
   enum radv_meta_object_key_type type;
   enum radv_meta_resolve_compute_type resolve_type;
   VkImageAspectFlags aspects;
   uint8_t samples;
   VkResolveModeFlagBits resolve_mode;
};

static VkResult
get_compute_resolve_pipeline(struct radv_device *device, VkFormat format, uint8_t samples, VkImageAspectFlags aspects,
                             VkResolveModeFlagBits resolve_mode, VkPipeline *pipeline_out, VkPipelineLayout *layout_out)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_compute_resolve_key key;
   VkResult result;

   result = get_compute_resolve_pipeline_layout(device, layout_out);
   if (result != VK_SUCCESS)
      return result;

   memset(&key, 0, sizeof(key));
   key.type = RADV_META_OBJECT_KEY_RESOLVE_CS;
   key.aspects = aspects;
   key.samples = samples;
   key.resolve_mode = resolve_mode;

   if (aspects == VK_IMAGE_ASPECT_COLOR_BIT)
      key.resolve_type = radv_meta_get_resolve_compute_type(format);

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *cs =
      radv_meta_nir_build_resolve_cs(pdev->use_fmask, key.resolve_type, key.samples, key.aspects, key.resolve_mode);

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
radv_fixup_resolve_dst_metadata(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                                VkImageLayout image_layout, const VkImageSubresourceLayers *subresource,
                                const VkOffset3D *offset, const VkExtent3D *extent, bool before_resolve)
{
   const struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   const uint32_t queue_mask = radv_image_queue_family_mask(image, cmd_buffer->qf, cmd_buffer->qf);

   const bool is_partial_resolve = offset->x || offset->y || offset->z || extent->width != image->vk.extent.width ||
                                   extent->height != image->vk.extent.height || extent->depth != image->vk.extent.depth;

   const VkImageSubresourceRange range = {
      .aspectMask = subresource->aspectMask,
      .baseMipLevel = subresource->mipLevel,
      .levelCount = 1,
      .baseArrayLayer = subresource->baseArrayLayer,
      .layerCount = vk_image_subresource_layer_count(&image->vk, subresource),
   };

   if (vk_format_is_color(image->vk.format)) {
      if (!radv_layout_dcc_compressed(device, image, subresource->mipLevel, image_layout, queue_mask))
         return;

      /* Nothing to do when compressed DCC writes are supported. */
      if (radv_image_compress_dcc_on_image_stores(device, image))
         return;

      if (before_resolve) {
         /* For partial resolves, DCC is decompressed before because image stores don't write the
          * uncompressed DWORD to DCC. And then it's needed to re-initialize DCC to its uncompressed
          * state after the copy.
          */
         if (is_partial_resolve)
            radv_decompress_dcc(cmd_buffer, image, &range);
      } else {
         /* Fixup DCC after a copy on compute, but not for partial copies because decompressing the
          * image also means that DCC is re-initialized to its uncompressed state.
          */
         if (!is_partial_resolve)
            cmd_buffer->state.flush_bits |= radv_init_dcc(cmd_buffer, image, &range, DCC_UNCOMPRESSED);
      }
   } else {
      if (!radv_layout_is_htile_compressed(device, image, subresource->mipLevel, image_layout, queue_mask))
         return;

      if (radv_image_decompress_htile_on_image_stores(device, image))
         return;

      if (before_resolve) {
         if (is_partial_resolve) {
            /* For partial resolves, HTILE is decompressed before because image stores don't write the
             * uncompressed DWORD to HTILE. And then it's needed to re-initialize HTILE to its
             * uncompressed state after the copy.
             */
            radv_expand_depth_stencil(cmd_buffer, image, &range, NULL);
         }
      } else {
         /* Fixup HTILE after a copy on compute, but not for partial copies because decompressing
          * the image also means that HTILE is re-initialized to its uncompressed state.
          */
         if (!is_partial_resolve) {
            uint32_t htile_value = radv_get_htile_initial_value(device, image);

            cmd_buffer->state.flush_bits |= radv_clear_htile(cmd_buffer, image, &range, htile_value, false);
         }
      }
   }
}

void
radv_compute_resolve_image(struct radv_cmd_buffer *cmd_buffer, struct radv_image *src_image, VkFormat src_format,
                           VkImageLayout src_image_layout, struct radv_image *dst_image, VkFormat dst_format,
                           VkImageLayout dst_image_layout, VkResolveModeFlagBits resolve_mode,
                           const VkImageResolve2 *region)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   result = get_compute_resolve_pipeline(device, src_format, src_image->vk.samples, region->srcSubresource.aspectMask,
                                         resolve_mode, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   radv_fixup_resolve_dst_metadata(cmd_buffer, dst_image, dst_image_layout, &region->dstSubresource, &region->dstOffset,
                                   &region->extent, true);

   assert(vk_image_subresource_layer_count(&src_image->vk, &region->srcSubresource) ==
          vk_image_subresource_layer_count(&dst_image->vk, &region->dstSubresource));

   const uint32_t src_base_layer = region->srcSubresource.baseArrayLayer;
   const uint32_t dst_base_layer =
      dst_image->vk.image_type == VK_IMAGE_TYPE_3D ? region->dstOffset.z : region->dstSubresource.baseArrayLayer;

   const struct VkExtent3D extent = vk_image_sanitize_extent(&src_image->vk, region->extent);
   const struct VkOffset3D srcOffset = vk_image_sanitize_offset(&src_image->vk, region->srcOffset);
   const struct VkOffset3D dstOffset = vk_image_sanitize_offset(&dst_image->vk, region->dstOffset);
   const unsigned layer_count = vk_image_subresource_layer_count(&src_image->vk, &region->srcSubresource);

   const VkImageViewUsage2CreateInfoKHR src_iview_usage_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_2_CREATE_INFO_KHR,
      .usage = VK_IMAGE_USAGE_2_SAMPLED_BIT_KHR,
   };

   struct radv_image_view src_iview;
   radv_image_view_init(&src_iview, device,
                        &(VkImageViewCreateInfo){
                           .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                           .pNext = &src_iview_usage_info,
                           .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
                           .image = radv_image_to_handle(src_image),
                           .viewType = VK_IMAGE_VIEW_TYPE_2D,
                           .format = src_format,
                           .subresourceRange =
                              {
                                 .aspectMask = region->srcSubresource.aspectMask,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = src_base_layer,
                                 .layerCount = layer_count,
                              },
                        },
                        NULL);

   const VkImageViewUsage2CreateInfoKHR dst_iview_usage_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_2_CREATE_INFO_KHR,
      .usage = VK_IMAGE_USAGE_2_STORAGE_BIT_KHR,
   };

   struct radv_image_view dst_iview;
   radv_image_view_init(&dst_iview, device,
                        &(VkImageViewCreateInfo){
                           .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                           .pNext = &dst_iview_usage_info,
                           .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
                           .image = radv_image_to_handle(dst_image),
                           .viewType = radv_meta_get_view_type(dst_image),
                           .format = vk_format_no_srgb(dst_format),
                           .subresourceRange =
                              {
                                 .aspectMask = region->dstSubresource.aspectMask,
                                 .baseMipLevel = region->dstSubresource.mipLevel,
                                 .levelCount = 1,
                                 .baseArrayLayer = dst_base_layer,
                                 .layerCount = layer_count,
                              },
                        },
                        NULL);

   radv_meta_bind_descriptors(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 2,
                              (VkDescriptorGetInfoEXT[]){{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                                                          .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                                          .data.pSampledImage =
                                                             (VkDescriptorImageInfo[]){
                                                                {.sampler = VK_NULL_HANDLE,
                                                                 .imageView = radv_image_view_to_handle(&src_iview),
                                                                 .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
                                                             }},
                                                         {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                                                          .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                          .data.pStorageImage = (VkDescriptorImageInfo[]){
                                                             {
                                                                .sampler = VK_NULL_HANDLE,
                                                                .imageView = radv_image_view_to_handle(&dst_iview),
                                                                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                                             },
                                                          }}});

   radv_meta_bind_compute_pipeline(cmd_buffer, pipeline);

   const uint32_t push_constants[5] = {
      srcOffset.x, srcOffset.y, dstOffset.x, dstOffset.y, dstOffset.z,
   };

   radv_meta_push_constants(cmd_buffer, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants), push_constants);

   radv_unaligned_dispatch(cmd_buffer, extent.width, extent.height, layer_count);

   radv_image_view_finish(&src_iview);
   radv_image_view_finish(&dst_iview);

   radv_fixup_resolve_dst_metadata(cmd_buffer, dst_image, dst_image_layout, &region->dstSubresource, &region->dstOffset,
                                   &region->extent, false);
}
