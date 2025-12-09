/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_descriptor_set_layout.h"

#include "nvk_descriptor_set.h"
#include "nvk_descriptor_types.h"
#include "nvk_device.h"
#include "nvk_entrypoints.h"
#include "nvk_physical_device.h"
#include "nvk_sampler.h"

#include "vk_pipeline_layout.h"

#include "clb097.h"

static bool
binding_has_immutable_samplers(const VkDescriptorSetLayoutBinding *binding)
{
   switch (binding->descriptorType) {
   case VK_DESCRIPTOR_TYPE_SAMPLER:
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      return binding->pImmutableSamplers != NULL;

   default:
      return false;
   }
}

static uint32_t
nvk_max_descriptor_size(const struct nv_device_info *info)
{
   // This should be constant-folded.
   uint32_t max_desc_size = 0;

   max_desc_size = MAX2(max_desc_size, sizeof(struct nvk_sampled_image_descriptor));
   max_desc_size = MAX2(max_desc_size, sizeof(struct nvk_edb_buffer_view_descriptor));
   max_desc_size = MAX2(max_desc_size, sizeof(union nvk_buffer_descriptor));

   if (info->cls_eng3d >= MAXWELL_A) {
      max_desc_size = MAX2(max_desc_size, sizeof(struct nvk_storage_image_descriptor));
      max_desc_size = MAX2(max_desc_size, sizeof(struct nvk_buffer_view_descriptor));
   } else {
      max_desc_size = MAX2(max_desc_size, sizeof(struct nvk_kepler_storage_image_descriptor));
      max_desc_size = MAX2(max_desc_size, sizeof(struct nvk_kepler_storage_buffer_view_descriptor));
   }

   return max_desc_size;
}

void
nvk_descriptor_stride_align_for_type(const struct nvk_physical_device *pdev,
                                     VkPipelineLayoutCreateFlags layout_flags,
                                     VkDescriptorType type,
                                     const VkMutableDescriptorTypeListEXT *type_list,
                                     uint32_t *stride, uint32_t *alignment)
{
   switch (type) {
   case VK_DESCRIPTOR_TYPE_SAMPLER:
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      /* TODO: How do samplers work? */
   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
      *stride = *alignment = sizeof(struct nvk_sampled_image_descriptor);
      break;

   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      if (pdev->info.cls_eng3d >= MAXWELL_A) {
         *stride = *alignment = sizeof(struct nvk_storage_image_descriptor);
      } else {
         *stride = sizeof(struct nvk_kepler_storage_image_descriptor);
         *alignment = 16;
      }
      break;

   case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
      if ((layout_flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT) ||
          nvk_use_edb_buffer_views(pdev)) {
         *stride = *alignment = sizeof(struct nvk_edb_buffer_view_descriptor);
      } else if (pdev->info.cls_eng3d >= MAXWELL_A ||
                 type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER) {
         *stride = *alignment = sizeof(struct nvk_buffer_view_descriptor);
      } else {
         *stride = sizeof(struct nvk_kepler_storage_buffer_view_descriptor);
         *alignment = 16;
      }
      break;

   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      *stride = *alignment = sizeof(union nvk_buffer_descriptor);
      break;

   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      *stride = *alignment = 0; /* These don't take up buffer space */
      break;

   case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
      *stride = 1; /* Array size is bytes */
      *alignment = nvk_min_cbuf_alignment(&pdev->info);
      break;

   case VK_DESCRIPTOR_TYPE_MUTABLE_EXT:
      *stride = *alignment = 0;
      if (type_list == NULL)
         *stride = *alignment = nvk_max_descriptor_size(&pdev->info);
      for (unsigned i = 0; type_list && i < type_list->descriptorTypeCount; i++) {
         /* This shouldn't recurse */
         assert(type_list->pDescriptorTypes[i] !=
                VK_DESCRIPTOR_TYPE_MUTABLE_EXT);
         uint32_t desc_stride, desc_align;
         nvk_descriptor_stride_align_for_type(pdev, layout_flags,
                                              type_list->pDescriptorTypes[i],
                                              NULL, &desc_stride, &desc_align);
         *stride = MAX2(*stride, desc_stride);
         *alignment = MAX2(*alignment, desc_align);
      }
      *stride = align(*stride, *alignment);
      break;

   default:
      UNREACHABLE("Invalid descriptor type");
   }

   assert(*stride <= NVK_MAX_DESCRIPTOR_SIZE);
}

static const VkMutableDescriptorTypeListEXT *
nvk_descriptor_get_type_list(VkDescriptorType type,
                             const VkMutableDescriptorTypeCreateInfoEXT *info,
                             const uint32_t info_idx)
{
   const VkMutableDescriptorTypeListEXT *type_list = NULL;
   if (type == VK_DESCRIPTOR_TYPE_MUTABLE_EXT) {
      assert(info != NULL);
      assert(info_idx < info->mutableDescriptorTypeListCount);
      type_list = &info->pMutableDescriptorTypeLists[info_idx];
   }
   return type_list;
}

static void
nvk_descriptor_set_layout_destroy(struct vk_device *vk_dev,
                                  struct vk_descriptor_set_layout *vk_layout)
{
   struct nvk_device *dev = container_of(vk_dev, struct nvk_device, vk);
   struct nvk_descriptor_set_layout *layout =
      vk_to_nvk_descriptor_set_layout(vk_layout);

   if (layout->embedded_samplers_addr != 0) {
      nvk_heap_free(dev, &dev->shader_heap,
                    layout->embedded_samplers_addr,
                    layout->non_variable_descriptor_buffer_size);
   }

   vk_object_free(&dev->vk, NULL, layout);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_CreateDescriptorSetLayout(VkDevice device,
                              const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                              const VkAllocationCallbacks *pAllocator,
                              VkDescriptorSetLayout *pSetLayout)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   uint32_t num_bindings = 0;
   uint32_t immutable_sampler_count = 0;
   for (uint32_t j = 0; j < pCreateInfo->bindingCount; j++) {
      const VkDescriptorSetLayoutBinding *binding = &pCreateInfo->pBindings[j];
      num_bindings = MAX2(num_bindings, binding->binding + 1);

      /* From the Vulkan 1.1.97 spec for VkDescriptorSetLayoutBinding:
     *
     *    "If descriptorType specifies a VK_DESCRIPTOR_TYPE_SAMPLER or
     *    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER type descriptor, then
     *    pImmutableSamplers can be used to initialize a set of immutable
     *    samplers. [...]  If descriptorType is not one of these descriptor
     *    types, then pImmutableSamplers is ignored.
     *
     * We need to be careful here and only parse pImmutableSamplers if we
     * have one of the right descriptor types.
     */
      if (binding_has_immutable_samplers(binding))
         immutable_sampler_count += binding->descriptorCount;
   }

   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct nvk_descriptor_set_layout, layout, 1);
   VK_MULTIALLOC_DECL(&ma, struct nvk_descriptor_set_binding_layout, bindings,
                      num_bindings);
   VK_MULTIALLOC_DECL(&ma, struct nvk_sampler *, samplers,
                      immutable_sampler_count);

   if (!vk_descriptor_set_layout_multizalloc(&dev->vk, &ma, pCreateInfo))
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   layout->vk.destroy = nvk_descriptor_set_layout_destroy;
   layout->flags = pCreateInfo->flags;
   layout->binding_count = num_bindings;

   for (uint32_t j = 0; j < pCreateInfo->bindingCount; j++) {
      const VkDescriptorSetLayoutBinding *binding = &pCreateInfo->pBindings[j];
      uint32_t b = binding->binding;
      /* We temporarily store pCreateInfo->pBindings[] index (plus one) in the
       * immutable_samplers pointer.  This provides us with a quick-and-dirty
       * way to sort the bindings by binding number.
       */
      layout->binding[b].immutable_samplers = (void *)(uintptr_t)(j + 1);
   }

   const VkDescriptorSetLayoutBindingFlagsCreateInfo *binding_flags_info =
      vk_find_struct_const(pCreateInfo->pNext,
                           DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO);
   const VkMutableDescriptorTypeCreateInfoEXT *mutable_info =
      vk_find_struct_const(pCreateInfo->pNext,
                           MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT);

   uint32_t buffer_size = 0;
   uint32_t max_variable_descriptor_size = 0;
   uint8_t dynamic_buffer_count = 0;
   for (uint32_t b = 0; b < num_bindings; b++) {
      /* We stashed the pCreateInfo->pBindings[] index (plus one) in the
       * immutable_samplers pointer.  Check for NULL (empty binding) and then
       * reset it and compute the index.
       */
      if (layout->binding[b].immutable_samplers == NULL)
         continue;
      const uint32_t info_idx =
         (uintptr_t)(void *)layout->binding[b].immutable_samplers - 1;
      layout->binding[b].immutable_samplers = NULL;

      const VkDescriptorSetLayoutBinding *binding =
         &pCreateInfo->pBindings[info_idx];

      if (binding->descriptorCount == 0)
         continue;

      layout->binding[b].type = binding->descriptorType;

      if (binding_flags_info && binding_flags_info->bindingCount > 0) {
         assert(binding_flags_info->bindingCount == pCreateInfo->bindingCount);
         layout->binding[b].flags = binding_flags_info->pBindingFlags[info_idx];
      }

      layout->binding[b].array_size = binding->descriptorCount;

      switch (binding->descriptorType) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         layout->binding[b].dynamic_buffer_index = dynamic_buffer_count;
         BITSET_SET_RANGE(layout->dynamic_ubos, dynamic_buffer_count,
                          dynamic_buffer_count + binding->descriptorCount - 1);
         dynamic_buffer_count += binding->descriptorCount;
         break;

      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         layout->binding[b].dynamic_buffer_index = dynamic_buffer_count;
         dynamic_buffer_count += binding->descriptorCount;
         break;

      default:
         break;
      }

      const VkMutableDescriptorTypeListEXT *type_list =
         nvk_descriptor_get_type_list(binding->descriptorType,
                                      mutable_info, info_idx);

      uint32_t stride, alignment;
      nvk_descriptor_stride_align_for_type(pdev, pCreateInfo->flags,
                                           binding->descriptorType,
                                           type_list, &stride, &alignment);

      uint8_t max_plane_count = 1;

      if (binding_has_immutable_samplers(binding)) {
         layout->binding[b].immutable_samplers = samplers;
         samplers += binding->descriptorCount;
         for (uint32_t i = 0; i < binding->descriptorCount; i++) {
            VK_FROM_HANDLE(nvk_sampler, sampler, binding->pImmutableSamplers[i]);
            layout->binding[b].immutable_samplers[i] = sampler;
            const uint8_t sampler_plane_count = sampler->vk.ycbcr_conversion ?
               vk_format_get_plane_count(sampler->vk.ycbcr_conversion->state.format) : 1;
            if (max_plane_count < sampler_plane_count)
               max_plane_count = sampler_plane_count;
         }
      }

      stride *= max_plane_count;

      if (stride > 0) {
         assert(stride <= UINT8_MAX);
         assert(util_is_power_of_two_nonzero(alignment));

         buffer_size = align64(buffer_size, alignment);
         layout->binding[b].offset = buffer_size;
         layout->binding[b].stride = stride;

         if (layout->binding[b].flags &
             VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT) {
            /* From the Vulkan 1.3.256 spec:
             *
             *    VUID-VkDescriptorSetLayoutBindingFlagsCreateInfo-pBindingFlags-03004
             *    "If an element of pBindingFlags includes
             *    VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT, then
             *    all other elements of
             *    VkDescriptorSetLayoutCreateInfo::pBindings must have a
             *    smaller value of binding"
             *
             * In other words, it has to be the last binding.
             */
            assert(b == num_bindings - 1);
            assert(max_variable_descriptor_size == 0);
            max_variable_descriptor_size = stride * binding->descriptorCount;
         } else {
            /* the allocation size will be computed at descriptor allocation,
             * but the buffer size will be already aligned as this binding will
             * be the last
             */
            buffer_size += stride * binding->descriptorCount;
         }
      }

   }

   layout->non_variable_descriptor_buffer_size = buffer_size;
   layout->max_buffer_size = buffer_size + max_variable_descriptor_size;
   layout->dynamic_buffer_count = dynamic_buffer_count;

   struct mesa_blake3 blake3_ctx;
   _mesa_blake3_init(&blake3_ctx);

#define BLAKE3_UPDATE_VALUE(x) _mesa_blake3_update(&blake3_ctx, &(x), sizeof(x));
   BLAKE3_UPDATE_VALUE(layout->non_variable_descriptor_buffer_size);
   BLAKE3_UPDATE_VALUE(layout->dynamic_buffer_count);
   BLAKE3_UPDATE_VALUE(layout->binding_count);

   for (uint32_t b = 0; b < num_bindings; b++) {
      BLAKE3_UPDATE_VALUE(layout->binding[b].type);
      BLAKE3_UPDATE_VALUE(layout->binding[b].flags);
      BLAKE3_UPDATE_VALUE(layout->binding[b].array_size);
      BLAKE3_UPDATE_VALUE(layout->binding[b].offset);
      BLAKE3_UPDATE_VALUE(layout->binding[b].stride);
      BLAKE3_UPDATE_VALUE(layout->binding[b].dynamic_buffer_index);

      if (layout->binding[b].immutable_samplers != NULL) {
         for (uint32_t i = 0; i < layout->binding[b].array_size; i++) {
            const struct nvk_sampler *sampler =
               layout->binding[b].immutable_samplers[i];

            /* We zalloc the object, so it's safe to hash the whole thing */
            if (sampler != NULL && sampler->vk.ycbcr_conversion != NULL)
               BLAKE3_UPDATE_VALUE(sampler->vk.ycbcr_conversion->state);
         }
      }
   }
#undef BLAKE3_UPDATE_VALUE

   _mesa_blake3_final(&blake3_ctx, layout->vk.blake3);

    if (pCreateInfo->flags &
        VK_DESCRIPTOR_SET_LAYOUT_CREATE_EMBEDDED_IMMUTABLE_SAMPLERS_BIT_EXT) {
      void *sampler_desc_data =
         vk_alloc2(&dev->vk.alloc, pAllocator, buffer_size, 4,
                   VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (sampler_desc_data == NULL) {
         nvk_descriptor_set_layout_destroy(&dev->vk, &layout->vk);
         return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
      }

      for (uint32_t b = 0; b < num_bindings; b++) {
         assert(layout->binding[b].type == VK_DESCRIPTOR_TYPE_SAMPLER);
         assert(layout->binding[b].array_size == 1);
         assert(layout->binding[b].immutable_samplers != NULL);
         assert(!(layout->binding[b].flags &
                  VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT));

         /* I'm paranoid */
         if (layout->binding[b].immutable_samplers == NULL)
            continue;

         struct nvk_sampler *sampler = layout->binding[b].immutable_samplers[0];

         /* YCbCr has to come in through a combined image/sampler */
         assert(sampler->plane_count == 1);

         assert(sampler->planes[0].desc_index < (1 << 12));
         struct nvk_sampled_image_descriptor desc = {
            .sampler_index = sampler->planes[0].desc_index,
         };
         memcpy(sampler_desc_data + layout->binding[b].offset,
                &desc, sizeof(desc));
      }

      VkResult result = nvk_heap_upload(dev, &dev->shader_heap,
                                        sampler_desc_data, buffer_size,
                                        nvk_min_cbuf_alignment(&pdev->info),
                                        &layout->embedded_samplers_addr);
      vk_free2(&dev->vk.alloc, pAllocator, sampler_desc_data);
      if (result != VK_SUCCESS) {
         nvk_descriptor_set_layout_destroy(&dev->vk, &layout->vk);
         return result;
      }
   }

   *pSetLayout = nvk_descriptor_set_layout_to_handle(layout);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvk_GetDescriptorSetLayoutSupport(VkDevice device,
                                  const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                                  VkDescriptorSetLayoutSupport *pSupport)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   const VkMutableDescriptorTypeCreateInfoEXT *mutable_info =
      vk_find_struct_const(pCreateInfo->pNext,
                           MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT);
   const VkDescriptorSetLayoutBindingFlagsCreateInfo *binding_flags =
      vk_find_struct_const(pCreateInfo->pNext,
                           DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO);

   /* Figure out the maximum alignment up-front.  Otherwise, we need to sort
    * the list of descriptors by binding number in order to get the size
    * accumulation right.
    */
   uint32_t max_align = 0;
   for (uint32_t i = 0; i < pCreateInfo->bindingCount; i++) {
      const VkDescriptorSetLayoutBinding *binding = &pCreateInfo->pBindings[i];
      const VkMutableDescriptorTypeListEXT *type_list =
         nvk_descriptor_get_type_list(binding->descriptorType,
                                      mutable_info, i);

      uint32_t stride, alignment;
      nvk_descriptor_stride_align_for_type(pdev, pCreateInfo->flags,
                                           binding->descriptorType,
                                           type_list, &stride, &alignment);
      max_align = MAX2(max_align, alignment);
   }

   uint64_t non_variable_size = 0;
   uint32_t variable_stride = 0;
   uint32_t variable_count = 0;
   bool variable_is_inline_uniform_block = false;
   uint8_t dynamic_buffer_count = 0;

   for (uint32_t i = 0; i < pCreateInfo->bindingCount; i++) {
      const VkDescriptorSetLayoutBinding *binding = &pCreateInfo->pBindings[i];

      VkDescriptorBindingFlags flags = 0;
      if (binding_flags != NULL && binding_flags->bindingCount > 0)
         flags = binding_flags->pBindingFlags[i];

      if (vk_descriptor_type_is_dynamic(binding->descriptorType))
         dynamic_buffer_count += binding->descriptorCount;

      const VkMutableDescriptorTypeListEXT *type_list =
         nvk_descriptor_get_type_list(binding->descriptorType,
                                      mutable_info, i);

      uint32_t stride, alignment;
      nvk_descriptor_stride_align_for_type(pdev, pCreateInfo->flags,
                                           binding->descriptorType,
                                           type_list, &stride, &alignment);

      if (stride > 0) {
         assert(stride <= UINT8_MAX);
         assert(util_is_power_of_two_nonzero(alignment));

         if (flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT) {
            /* From the Vulkan 1.3.256 spec:
             *
             *    "For the purposes of this command, a variable-sized
             *    descriptor binding with a descriptorCount of zero is treated
             *    as if the descriptorCount is one"
             */
            variable_count = MAX2(1, binding->descriptorCount);
            variable_stride = stride;

            variable_is_inline_uniform_block =
               binding->descriptorType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK;
         } else {
            /* Since we're aligning to the maximum and since this is just a
             * check for whether or not the max buffer size is big enough, we
             * keep non_variable_size aligned to max_align.
             */
            non_variable_size += stride * binding->descriptorCount;
            non_variable_size = align64(non_variable_size, max_align);
         }
      }
   }

   uint64_t buffer_size = non_variable_size;
   if (variable_stride > 0) {
      buffer_size += variable_stride * variable_count;
      buffer_size = align64(buffer_size, max_align);
   }

   uint32_t max_buffer_size;
   if (pCreateInfo->flags &
       VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR)
      max_buffer_size = NVK_MAX_PUSH_DESCRIPTORS * nvk_max_descriptor_size(&pdev->info);
   else
      max_buffer_size = NVK_MAX_DESCRIPTOR_SET_SIZE;

   pSupport->supported = dynamic_buffer_count <= NVK_MAX_DYNAMIC_BUFFERS &&
                         buffer_size <= max_buffer_size;

   vk_foreach_struct(ext, pSupport->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_LAYOUT_SUPPORT: {
         VkDescriptorSetVariableDescriptorCountLayoutSupport *vs = (void *)ext;
         uint32_t max_var_count;

         if (variable_stride > 0) {
            max_var_count =
               (max_buffer_size - non_variable_size) / variable_stride;
         } else {
            max_var_count = 0;
         }

         if (variable_is_inline_uniform_block) {
            max_var_count =
               MIN2(max_var_count, NVK_MAX_INLINE_UNIFORM_BLOCK_SIZE);
         }

         vs->maxVariableDescriptorCount = max_var_count;
         break;
      }

      default:
         vk_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_GetDescriptorSetLayoutSizeEXT(VkDevice device,
                                  VkDescriptorSetLayout _layout,
                                  VkDeviceSize *pLayoutSizeInBytes)
{
   VK_FROM_HANDLE(nvk_descriptor_set_layout, layout, _layout);

   *pLayoutSizeInBytes = layout->max_buffer_size;
}

VKAPI_ATTR void VKAPI_CALL
nvk_GetDescriptorSetLayoutBindingOffsetEXT(VkDevice device,
                                           VkDescriptorSetLayout _layout,
                                           uint32_t binding,
                                           VkDeviceSize *pOffset)
{
   VK_FROM_HANDLE(nvk_descriptor_set_layout, layout, _layout);

   *pOffset = layout->binding[binding].offset;
}
