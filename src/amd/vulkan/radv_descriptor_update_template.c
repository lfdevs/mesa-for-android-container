/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#include "radv_descriptor_update_template.h"
#include "radv_cmd_buffer.h"
#include "radv_descriptors.h"
#include "radv_device.h"
#include "radv_entrypoints.h"

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateDescriptorUpdateTemplate(VkDevice _device, const VkDescriptorUpdateTemplateCreateInfo *pCreateInfo,
                                    const VkAllocationCallbacks *pAllocator,
                                    VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   const uint32_t entry_count = pCreateInfo->descriptorUpdateEntryCount;
   const size_t size = sizeof(struct radv_descriptor_update_template) +
                       sizeof(struct radv_descriptor_update_template_entry) * entry_count;
   struct radv_descriptor_set_layout *set_layout = NULL;
   struct radv_descriptor_update_template *templ;
   uint32_t i;

   templ = vk_alloc2(&device->vk.alloc, pAllocator, size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!templ)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &templ->base, VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE);

   templ->entry_count = entry_count;

   if (pCreateInfo->templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR) {
      VK_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, pCreateInfo->pipelineLayout);

      /* descriptorSetLayout should be ignored for push descriptors
       * and instead it refers to pipelineLayout and set.
       */
      assert(pCreateInfo->set < MAX_SETS);
      set_layout = pipeline_layout->set[pCreateInfo->set].layout;

      templ->bind_point = pCreateInfo->pipelineBindPoint;
   } else {
      assert(pCreateInfo->templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET);
      set_layout = radv_descriptor_set_layout_from_handle(pCreateInfo->descriptorSetLayout);
   }

   for (i = 0; i < entry_count; i++) {
      const VkDescriptorUpdateTemplateEntry *entry = &pCreateInfo->pDescriptorUpdateEntries[i];
      const struct radv_descriptor_set_binding_layout *binding_layout = set_layout->binding + entry->dstBinding;
      const uint32_t buffer_offset = binding_layout->buffer_offset + entry->dstArrayElement;
      const uint32_t *immutable_samplers = NULL;
      uint32_t dst_offset;
      uint32_t dst_stride;

      /* dst_offset is an offset into dynamic_descriptors when the descriptor
         is dynamic, and an offset into mapped_ptr otherwise */
      if (vk_descriptor_type_is_dynamic(entry->descriptorType)) {
         assert(pCreateInfo->templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET);
         dst_offset = binding_layout->dynamic_offset_offset + entry->dstArrayElement;
         dst_stride = 0; /* Not used */
      } else {
         switch (entry->descriptorType) {
         case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         case VK_DESCRIPTOR_TYPE_SAMPLER:
            /* Immutable samplers are copied into push descriptors when they are pushed */
            if (pCreateInfo->templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR &&
                binding_layout->immutable_samplers_offset) {
               immutable_samplers = radv_immutable_samplers(set_layout, binding_layout) + entry->dstArrayElement * 4;
            }
            break;
         default:
            break;
         }
         dst_offset = binding_layout->offset / 4;
         if (entry->descriptorType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK)
            dst_offset += entry->dstArrayElement / 4;
         else
            dst_offset += binding_layout->size * entry->dstArrayElement / 4;

         dst_stride = binding_layout->size / 4;
      }

      templ->entry[i] =
         (struct radv_descriptor_update_template_entry){.descriptor_type = entry->descriptorType,
                                                        .descriptor_count = entry->descriptorCount,
                                                        .src_offset = entry->offset,
                                                        .src_stride = entry->stride,
                                                        .dst_offset = dst_offset,
                                                        .dst_stride = dst_stride,
                                                        .buffer_offset = buffer_offset,
                                                        .has_sampler = !binding_layout->immutable_samplers_offset,
                                                        .has_ycbcr_sampler = binding_layout->has_ycbcr_sampler,
                                                        .immutable_samplers = immutable_samplers};
   }

   *pDescriptorUpdateTemplate = radv_descriptor_update_template_to_handle(templ);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyDescriptorUpdateTemplate(VkDevice _device, VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                     const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(radv_descriptor_update_template, templ, descriptorUpdateTemplate);

   if (!templ)
      return;

   vk_object_base_finish(&templ->base);
   vk_free2(&device->vk.alloc, pAllocator, templ);
}

static ALWAYS_INLINE void
radv_update_descriptor_set_with_template_impl(struct radv_device *device, struct radv_cmd_buffer *cmd_buffer,
                                              struct radv_descriptor_set *set,
                                              VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void *pData)
{
   VK_FROM_HANDLE(radv_descriptor_update_template, templ, descriptorUpdateTemplate);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   uint32_t i;

   for (i = 0; i < templ->entry_count; ++i) {
      struct radeon_winsys_bo **buffer_list = set->descriptors + templ->entry[i].buffer_offset;
      uint32_t *pDst = set->header.mapped_ptr + templ->entry[i].dst_offset;
      const uint8_t *pSrc = ((const uint8_t *)pData) + templ->entry[i].src_offset;
      uint32_t j;

      if (templ->entry[i].descriptor_type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK) {
         memcpy((uint8_t *)pDst, pSrc, templ->entry[i].descriptor_count);
         continue;
      }

      for (j = 0; j < templ->entry[i].descriptor_count; ++j) {
         switch (templ->entry[i].descriptor_type) {
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
            const unsigned idx = templ->entry[i].dst_offset + j;
            assert(!(set->header.layout->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT));
            radv_write_dynamic_buffer_descriptor(device, set->header.dynamic_descriptors + idx, buffer_list,
                                                 (struct VkDescriptorBufferInfo *)pSrc);
            break;
         }
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            radv_write_buffer_descriptor_impl(device, cmd_buffer, pDst, buffer_list,
                                              (struct VkDescriptorBufferInfo *)pSrc);
            break;
         case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            radv_write_texel_buffer_descriptor(device, cmd_buffer, pDst, buffer_list, *(VkBufferView *)pSrc);
            break;
         case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            radv_write_image_descriptor_impl(device, cmd_buffer, RADV_STORAGE_IMAGE_DESC_SIZE, pDst, buffer_list,
                                             templ->entry[i].descriptor_type, (struct VkDescriptorImageInfo *)pSrc);
            break;
         case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
         case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            radv_write_image_descriptor_impl(device, cmd_buffer, radv_get_sampled_image_desc_size(pdev), pDst,
                                             buffer_list, templ->entry[i].descriptor_type,
                                             (struct VkDescriptorImageInfo *)pSrc);
            break;
         case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
            if (templ->entry[i].has_ycbcr_sampler) {
               radv_write_image_descriptor_ycbcr_impl(device, cmd_buffer, pDst, buffer_list,
                                                      (struct VkDescriptorImageInfo *)pSrc);
            } else {
               radv_write_combined_image_sampler_descriptor(
                  device, cmd_buffer, pDst, buffer_list, templ->entry[i].descriptor_type,
                  (struct VkDescriptorImageInfo *)pSrc, templ->entry[i].has_sampler);
            }

            if (cmd_buffer && templ->entry[i].immutable_samplers) {
               const uint32_t sampler_offset = radv_get_combined_image_sampler_offset(pdev);

               memcpy((char *)pDst + sampler_offset, templ->entry[i].immutable_samplers + 4 * j,
                      RADV_SAMPLER_DESC_SIZE);
            }
            break;
         }
         case VK_DESCRIPTOR_TYPE_SAMPLER:
            if (templ->entry[i].has_sampler) {
               const VkDescriptorImageInfo *pImageInfo = (struct VkDescriptorImageInfo *)pSrc;
               radv_write_sampler_descriptor(pDst, pImageInfo->sampler);
            } else if (cmd_buffer && templ->entry[i].immutable_samplers)
               memcpy(pDst, templ->entry[i].immutable_samplers + 4 * j, RADV_SAMPLER_DESC_SIZE);
            break;
         case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: {
            VK_FROM_HANDLE(vk_acceleration_structure, accel_struct, *(const VkAccelerationStructureKHR *)pSrc);
            radv_write_accel_struct_descriptor(device, pDst,
                                               accel_struct ? vk_acceleration_structure_get_va(accel_struct) : 0);
            break;
         }
         default:
            break;
         }
         pSrc += templ->entry[i].src_stride;
         pDst += templ->entry[i].dst_stride;

         buffer_list += radv_descriptor_type_buffer_count(templ->entry[i].descriptor_type);
      }
   }
}

void
radv_cmd_update_descriptor_set_with_template(struct radv_device *device, struct radv_cmd_buffer *cmd_buffer,
                                             struct radv_descriptor_set *set,
                                             VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void *pData)
{
   /* Assume cmd_buffer != NULL to optimize out cmd_buffer checks in generic code above. */
   assume(cmd_buffer != NULL);
   radv_update_descriptor_set_with_template_impl(device, cmd_buffer, set, descriptorUpdateTemplate, pData);
}

VKAPI_ATTR void VKAPI_CALL
radv_UpdateDescriptorSetWithTemplate(VkDevice _device, VkDescriptorSet descriptorSet,
                                     VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void *pData)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(radv_descriptor_set, set, descriptorSet);

   radv_update_descriptor_set_with_template_impl(device, NULL, set, descriptorUpdateTemplate, pData);
}
