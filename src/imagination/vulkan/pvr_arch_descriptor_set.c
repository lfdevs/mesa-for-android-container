/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pvr_descriptor_set.h"

#include "vk_descriptor_update_template.h"
#include "vk_log.h"
#include "vk_util.h"

#include "pvr_buffer.h"
#include "pvr_csb.h"
#include "pvr_device.h"
#include "pvr_entrypoints.h"
#include "pvr_image.h"
#include "pvr_physical_device.h"
#include "pvr_sampler.h"

static void
write_buffer(const struct pvr_descriptor_set *set,
             const VkDescriptorBufferInfo *buffer_info,
             const struct pvr_descriptor_set_layout_binding *binding,
             uint32_t elem)
{
   const unsigned desc_offset = binding->offset + (elem * binding->stride);
   void *desc_mapping = (uint8_t *)set->mapping + desc_offset;

   if (buffer_info->buffer == VK_NULL_HANDLE) {
      memset(desc_mapping, 0, sizeof(struct pvr_buffer_descriptor));
      return;
   }

   VK_FROM_HANDLE(pvr_buffer, buffer, buffer_info->buffer);

   const pvr_dev_addr_t buffer_addr =
      PVR_DEV_ADDR_OFFSET(buffer->dev_addr, buffer_info->offset);

   UNUSED uint32_t range =
      vk_buffer_range(&buffer->vk, buffer_info->offset, buffer_info->range);

   const struct pvr_buffer_descriptor buffer_desc = {
      .addr = buffer_addr.addr,
      .size = range,
   };

   memcpy(desc_mapping, &buffer_desc, sizeof(buffer_desc));
}

static void
write_dynamic_buffer(struct pvr_descriptor_set *set,
                     const VkDescriptorBufferInfo *buffer_info,
                     const struct pvr_descriptor_set_layout_binding *binding,
                     uint32_t elem)
{
   assert(binding->dynamic_buffer_idx != ~0);
   const unsigned desc_offset = binding->dynamic_buffer_idx + elem;
   struct pvr_buffer_descriptor *desc_mapping =
      &set->dynamic_buffers[desc_offset];

   if (buffer_info->buffer == VK_NULL_HANDLE) {
      memset(desc_mapping, 0, sizeof(*desc_mapping));
      return;
   }

   VK_FROM_HANDLE(pvr_buffer, buffer, buffer_info->buffer);

   const pvr_dev_addr_t buffer_addr =
      PVR_DEV_ADDR_OFFSET(buffer->dev_addr, buffer_info->offset);

   UNUSED uint32_t range =
      vk_buffer_range(&buffer->vk, buffer_info->offset, buffer_info->range);

   desc_mapping->addr = buffer_addr.addr;
   desc_mapping->size = range;
}

static void
write_sampler(const struct pvr_descriptor_set *set,
              const VkDescriptorImageInfo *image_info,
              const struct pvr_descriptor_set_layout_binding *binding,
              uint32_t elem)
{
   const unsigned desc_offset = binding->offset + (elem * binding->stride);
   void *desc_mapping = (uint8_t *)set->mapping + desc_offset;
   struct pvr_sampler *sampler;

   if (binding->immutable_sampler_count) {
      sampler = binding->immutable_samplers[elem];
   } else {
      assert(image_info);
      VK_FROM_HANDLE(pvr_sampler, info_sampler, image_info->sampler);
      sampler = info_sampler;
   }

   struct pvr_sampler_descriptor sampler_desc = sampler->descriptor;
   memcpy(desc_mapping, &sampler_desc, sizeof(sampler_desc));
}

static void
write_image_sampler(const struct pvr_descriptor_set *set,
                    const VkDescriptorImageInfo *image_info,
                    const struct pvr_descriptor_set_layout_binding *binding,
                    uint32_t elem)
{
   const unsigned desc_offset = binding->offset + (elem * binding->stride);
   void *desc_mapping = (uint8_t *)set->mapping + desc_offset;

   struct pvr_combined_image_sampler_descriptor image_sampler_desc = { 0 };

   struct pvr_sampler *sampler;
   if (binding->immutable_sampler_count) {
      sampler = binding->immutable_samplers[elem];
   } else {
      VK_FROM_HANDLE(pvr_sampler, info_sampler, image_info->sampler);
      sampler = info_sampler;
   }

   image_sampler_desc.sampler = sampler->descriptor;

   if (image_info->imageView != VK_NULL_HANDLE) {
      VK_FROM_HANDLE(pvr_image_view, image_view, image_info->imageView);
      image_sampler_desc.image =
         image_view->image_state[PVR_TEXTURE_STATE_SAMPLE];
      if (image_view->sampler_words[0]) {
         for (unsigned i = 0; i < ROGUE_NUM_TEXSTATE_SAMPLER_WORDS; i++) {
            image_sampler_desc.sampler.words[i] |= image_view->sampler_words[i];
            image_sampler_desc.sampler.gather_words[i] |=
               image_view->sampler_words[i];
         }
      }
   }

   memcpy(desc_mapping, &image_sampler_desc, sizeof(image_sampler_desc));
}

static void
write_input_attachment(const struct pvr_descriptor_set *set,
                       const VkDescriptorImageInfo *image_info,
                       const struct pvr_descriptor_set_layout_binding *binding,
                       uint32_t elem)
{
   VK_FROM_HANDLE(pvr_image_view, image_view, image_info->imageView);

   const unsigned desc_offset = binding->offset + (elem * binding->stride);
   void *desc_mapping = (uint8_t *)set->mapping + desc_offset;

   struct pvr_image_descriptor image_desc =
      image_view->image_state[PVR_TEXTURE_STATE_ATTACHMENT];

   memcpy(desc_mapping, &image_desc, sizeof(image_desc));
}

static void
write_sampled_image(const struct pvr_descriptor_set *set,
                    const VkDescriptorImageInfo *image_info,
                    const struct pvr_descriptor_set_layout_binding *binding,
                    uint32_t elem,
                    const struct pvr_device_info *dev_info)
{
   VK_FROM_HANDLE(pvr_image_view, image_view, image_info->imageView);

   const unsigned desc_offset = binding->offset + (elem * binding->stride);
   void *desc_mapping = (uint8_t *)set->mapping + desc_offset;

   struct pvr_image_descriptor sampled_image_desc =
      image_view->image_state[PVR_TEXTURE_STATE_SAMPLE];

   /* YCbCr must use combined image smapler */
   assert(!image_view->sampler_words[0] && !image_view->sampler_words[1]);

   memcpy(desc_mapping, &sampled_image_desc, sizeof(sampled_image_desc));
}

static void
write_storage_image(const struct pvr_descriptor_set *set,
                    const VkDescriptorImageInfo *image_info,
                    const struct pvr_descriptor_set_layout_binding *binding,
                    uint32_t elem,
                    const struct pvr_device_info *dev_info)
{
   const unsigned desc_offset = binding->offset + (elem * binding->stride);
   void *desc_mapping = (uint8_t *)set->mapping + desc_offset;

   if (image_info->imageView == VK_NULL_HANDLE) {
      memset(desc_mapping, 0, sizeof(struct pvr_image_descriptor));
      return;
   }

   VK_FROM_HANDLE(pvr_image_view, image_view, image_info->imageView);

   bool is_cube = image_view->vk.view_type == VK_IMAGE_VIEW_TYPE_CUBE ||
                  image_view->vk.view_type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;

   struct pvr_image_descriptor storage_image_desc =
      image_view->image_state[is_cube ? PVR_TEXTURE_STATE_STORAGE
                                      : PVR_TEXTURE_STATE_SAMPLE];

   if (!PVR_HAS_FEATURE(dev_info, tpu_extended_integer_lookup)) {
      struct ROGUE_TEXSTATE_STRIDE_IMAGE_WORD1 word1;
      ROGUE_TEXSTATE_STRIDE_IMAGE_WORD1_unpack(&storage_image_desc.words[1],
                                               &word1);

      word1.index_lookup = true;
      ROGUE_TEXSTATE_STRIDE_IMAGE_WORD1_pack(&storage_image_desc.words[1],
                                             &word1);
   }

   memcpy(desc_mapping, &storage_image_desc, sizeof(storage_image_desc));
}

static void
write_buffer_view(const struct pvr_descriptor_set *set,
                  const VkBufferView _buffer_view,
                  const struct pvr_descriptor_set_layout_binding *binding,
                  uint32_t elem,
                  bool is_texel_buffer,
                  const struct pvr_device_info *dev_info)
{
   const unsigned desc_offset = binding->offset + (elem * binding->stride);
   void *desc_mapping = (uint8_t *)set->mapping + desc_offset;

   if (_buffer_view == VK_NULL_HANDLE) {
      memset(desc_mapping, 0, sizeof(struct pvr_image_descriptor));
      return;
   }

   VK_FROM_HANDLE(pvr_buffer_view, buffer_view, _buffer_view);
   struct pvr_image_descriptor buffer_view_state = buffer_view->image_state;

   if (is_texel_buffer &&
       !PVR_HAS_FEATURE(dev_info, tpu_extended_integer_lookup)) {
      struct ROGUE_TEXSTATE_STRIDE_IMAGE_WORD1 word1;
      ROGUE_TEXSTATE_STRIDE_IMAGE_WORD1_unpack(&buffer_view_state.words[1],
                                               &word1);

      word1.index_lookup = true;
      ROGUE_TEXSTATE_STRIDE_IMAGE_WORD1_pack(&buffer_view_state.words[1],
                                             &word1);
   }

   memcpy(desc_mapping, &buffer_view_state, sizeof(buffer_view_state));
}

static void write_inline_uniform_block(
   const struct pvr_descriptor_set *set,
   const VkWriteDescriptorSetInlineUniformBlock *write_iub,
   const struct pvr_descriptor_set_layout_binding *binding,
   uint32_t elem)
{
   assert(binding->stride == 1);
   const unsigned desc_offset = binding->offset + (elem * binding->stride);
   void *desc_mapping = (uint8_t *)set->mapping + desc_offset;

   memcpy(desc_mapping, write_iub->pData, write_iub->dataSize);
}

void PVR_PER_ARCH(descriptor_set_write_immutable_samplers)(
   struct pvr_descriptor_set_layout *layout,
   struct pvr_descriptor_set *set)
{
   for (unsigned u = 0; u < layout->binding_count; ++u) {
      const struct pvr_descriptor_set_layout_binding *binding =
         &layout->bindings[u];

      if (binding->type == VK_DESCRIPTOR_TYPE_SAMPLER &&
          binding->immutable_samplers) {
         for (uint32_t j = 0; j < binding->descriptor_count; j++) {
            write_sampler(set, NULL, binding, j);
         }
      }
   }
}

void PVR_PER_ARCH(UpdateDescriptorSets)(
   VkDevice _device,
   uint32_t descriptorWriteCount,
   const VkWriteDescriptorSet *pDescriptorWrites,
   uint32_t descriptorCopyCount,
   const VkCopyDescriptorSet *pDescriptorCopies)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   const struct pvr_device_info *dev_info = &device->pdevice->dev_info;

   for (uint32_t i = 0; i < descriptorWriteCount; i++) {
      const VkWriteDescriptorSet *write = &pDescriptorWrites[i];
      VK_FROM_HANDLE(pvr_descriptor_set, set, write->dstSet);
      const struct pvr_descriptor_set_layout *layout = set->layout;
      const struct pvr_descriptor_set_layout_binding *binding;

      assert(write->dstBinding < layout->binding_count);
      binding = &layout->bindings[write->dstBinding];

      const VkWriteDescriptorSetInlineUniformBlock *write_iub = NULL;
      vk_foreach_struct_const (sType, ext, write->pNext) {
         switch (sType) {
         case VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK:
            write_iub = (const VkWriteDescriptorSetInlineUniformBlock *)ext;
            continue;

         default:
            break;
         }

         vk_debug_ignored_stype(sType);
      }

      if (!binding->stage_flags)
         continue;

      switch (write->descriptorType) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            write_buffer(set,
                         &write->pBufferInfo[j],
                         binding,
                         write->dstArrayElement + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            write_dynamic_buffer(set,
                                 &write->pBufferInfo[j],
                                 binding,
                                 write->dstArrayElement + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_SAMPLER:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            write_sampler(set,
                          &write->pImageInfo[j],
                          binding,
                          write->dstArrayElement + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            write_image_sampler(set,
                                &write->pImageInfo[j],
                                binding,
                                write->dstArrayElement + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            write_sampled_image(set,
                                &write->pImageInfo[j],
                                binding,
                                write->dstArrayElement + j,
                                dev_info);
         }
         break;

      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            write_storage_image(set,
                                &write->pImageInfo[j],
                                binding,
                                write->dstArrayElement + j,
                                dev_info);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            write_buffer_view(set,
                              write->pTexelBufferView[j],
                              binding,
                              write->dstArrayElement + j,
                              write->descriptorType ==
                                 VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                              dev_info);
         }
         break;

      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            write_input_attachment(set,
                                   &write->pImageInfo[j],
                                   binding,
                                   write->dstArrayElement + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
         assert(write_iub);
         write_inline_uniform_block(set,
                                    write_iub,
                                    binding,
                                    write->dstArrayElement);
         break;

      default:
         UNREACHABLE("");
      }
   }

   for (uint32_t i = 0; i < descriptorCopyCount; i++) {
      const VkCopyDescriptorSet *copy = &pDescriptorCopies[i];
      VK_FROM_HANDLE(pvr_descriptor_set, src_set, copy->srcSet);
      VK_FROM_HANDLE(pvr_descriptor_set, dst_set, copy->dstSet);

      const struct pvr_descriptor_set_layout *src_layout = src_set->layout;
      const struct pvr_descriptor_set_layout *dst_layout = dst_set->layout;
      const struct pvr_descriptor_set_layout_binding *src_binding;
      const struct pvr_descriptor_set_layout_binding *dst_binding;

      assert(copy->srcBinding < src_layout->binding_count);
      assert(copy->dstBinding < dst_layout->binding_count);
      src_binding = &src_layout->bindings[copy->srcBinding];
      dst_binding = &dst_layout->bindings[copy->dstBinding];

      vk_foreach_struct_const (sType, ext, copy->pNext) {
         vk_debug_ignored_stype(sType);
      }

      assert(src_binding->stage_flags == dst_binding->stage_flags);
      if (!src_binding->stage_flags)
         continue;

      assert(src_binding->stride == dst_binding->stride);

      if (vk_descriptor_type_is_dynamic(src_binding->type)) {
         const unsigned src_desc_offset =
            src_binding->dynamic_buffer_idx + copy->srcArrayElement;
         const unsigned dst_desc_offset =
            dst_binding->dynamic_buffer_idx + copy->dstArrayElement;

         memcpy(&dst_set->dynamic_buffers[dst_desc_offset],
                &src_set->dynamic_buffers[src_desc_offset],
                sizeof(*src_set->dynamic_buffers) * copy->descriptorCount);

         continue;
      }

      if (src_binding->stride > 0) {
         for (uint32_t j = 0; j < copy->descriptorCount; j++) {
            const unsigned src_desc_offset =
               src_binding->offset +
               ((copy->srcArrayElement + j) * src_binding->stride);
            const void *src_desc_mapping =
               (uint8_t *)src_set->mapping + src_desc_offset;

            const unsigned dst_desc_offset =
               dst_binding->offset +
               ((copy->dstArrayElement + j) * dst_binding->stride);
            void *dst_desc_mapping =
               (uint8_t *)dst_set->mapping + dst_desc_offset;

            memcpy(dst_desc_mapping, src_desc_mapping, src_binding->stride);
         }
      }
   }
}

void PVR_PER_ARCH(UpdateDescriptorSetWithTemplate)(
   VkDevice _device,
   VkDescriptorSet descriptorSet,
   VkDescriptorUpdateTemplate descriptorUpdateTemplate,
   const void *pData)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   VK_FROM_HANDLE(vk_descriptor_update_template,
                  template,
                  descriptorUpdateTemplate);
   VK_FROM_HANDLE(pvr_descriptor_set, set, descriptorSet);

   const struct pvr_device_info *dev_info = &device->pdevice->dev_info;

   assert(template->type !=
          VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS);

   for (uint32_t i = 0; i < template->entry_count; i++) {
      const struct vk_descriptor_template_entry *entry = &template->entries[i];
      const struct pvr_descriptor_set_layout_binding *layout_binding =
         &set->layout->bindings[entry->binding];
      uint8_t *data = (uint8_t *)pData + entry->offset;

      switch (entry->type) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorBufferInfo *info =
               (const VkDescriptorBufferInfo *)(data + j * entry->stride);

            write_buffer(set, info, layout_binding, entry->array_element + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorBufferInfo *info =
               (const VkDescriptorBufferInfo *)(data + j * entry->stride);

            write_dynamic_buffer(set,
                                 info,
                                 layout_binding,
                                 entry->array_element + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_SAMPLER:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorImageInfo *info =
               (const VkDescriptorImageInfo *)(data + j * entry->stride);

            write_sampler(set, info, layout_binding, entry->array_element + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorImageInfo *info =
               (const VkDescriptorImageInfo *)(data + j * entry->stride);

            write_image_sampler(set,
                                info,
                                layout_binding,
                                entry->array_element + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorImageInfo *info =
               (const VkDescriptorImageInfo *)(data + j * entry->stride);

            write_sampled_image(set,
                                info,
                                layout_binding,
                                entry->array_element + j,
                                dev_info);
         }
         break;

      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorImageInfo *info =
               (const VkDescriptorImageInfo *)(data + j * entry->stride);

            write_storage_image(set,
                                info,
                                layout_binding,
                                entry->array_element + j,
                                dev_info);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkBufferView *bview =
               (const VkBufferView *)(data + j * entry->stride);

            write_buffer_view(set,
                              *bview,
                              layout_binding,
                              entry->array_element + j,
                              entry->type ==
                                 VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                              dev_info);
         }
         break;

      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorImageInfo *info =
               (const VkDescriptorImageInfo *)(data + j * entry->stride);

            write_input_attachment(set,
                                   info,
                                   layout_binding,
                                   entry->array_element + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
         write_inline_uniform_block(
            set,
            &(const VkWriteDescriptorSetInlineUniformBlock){
               .pData = data,
               .dataSize = entry->array_count,
            },
            layout_binding,
            entry->array_element);
         break;

      default:
         UNREACHABLE("Unknown descriptor type");
      }
   }
}
