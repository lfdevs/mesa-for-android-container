/*
 * Copyright © 2022 Imagination Technologies Ltd.
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

#include "pvr_descriptor_set.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "hwdef/rogue_hw_utils.h"
#include "pvr_bo.h"
#include "pvr_buffer.h"
#include "pvr_csb.h"
#include "pvr_debug.h"
#include "pvr_device.h"
#include "pvr_entrypoints.h"
#include "pvr_image.h"
#include "pvr_physical_device.h"
#include "pvr_sampler.h"
#include "pvr_types.h"
#include "util/compiler.h"
#include "util/list.h"
#include "util/log.h"
#include "util/macros.h"
#include "util/vma.h"
#include "vk_alloc.h"
#include "vk_descriptor_update_template.h"
#include "vk_descriptors.h"
#include "vk_descriptor_set_layout.h"
#include "vk_format.h"
#include "vk_log.h"
#include "vk_object.h"
#include "vk_util.h"
#include "vulkan/util/vk_enum_to_str.h"

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

static bool
binding_has_dynamic_buffers(const VkDescriptorSetLayoutBinding *binding)
{
   return vk_descriptor_type_is_dynamic(binding->descriptorType);
}

static bool
binding_has_combined_image_samplers(const VkDescriptorSetLayoutBinding *binding)
{
   return binding->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
}

static unsigned pvr_descriptor_size(VkDescriptorType type)
{
   switch (type) {
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      return sizeof(struct pvr_buffer_descriptor);

   case VK_DESCRIPTOR_TYPE_SAMPLER:
      return sizeof(struct pvr_sampler_descriptor);

   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      return sizeof(struct pvr_combined_image_sampler_descriptor);

   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
   case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
      return sizeof(struct pvr_image_descriptor);

   default:
      mesa_loge("Unsupported descriptor type %s.\n",
                vk_DescriptorType_to_str(type));
      UNREACHABLE("");
   }
}

VkResult pvr_CreateDescriptorSetLayout(
   VkDevice _device,
   const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator,
   VkDescriptorSetLayout *pSetLayout)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   VkDescriptorSetLayoutBinding *bindings;
   uint32_t binding_count = 0;
   uint32_t immutable_sampler_count = 0;
   uint32_t dynamic_buffer_count = 0;
   uint32_t descriptor_count = 0;
   VkResult result = VK_SUCCESS;

   assert(pCreateInfo->sType ==
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);

   vk_foreach_struct_const (ext, pCreateInfo->pNext) {
      vk_debug_ignored_stype(ext->sType);
   }

   for (unsigned u = 0; u < pCreateInfo->bindingCount; ++u) {
      const VkDescriptorSetLayoutBinding *binding = &pCreateInfo->pBindings[u];

      descriptor_count += binding->descriptorCount;

      if (binding_has_immutable_samplers(binding))
         immutable_sampler_count += binding->descriptorCount;
      else if (binding_has_dynamic_buffers(binding))
         dynamic_buffer_count += binding->descriptorCount;
   }

   result = vk_create_sorted_bindings(pCreateInfo->pBindings,
                                      pCreateInfo->bindingCount,
                                      &bindings);

   if (result != VK_SUCCESS)
      return vk_error(device, result);

   if (bindings)
      binding_count = bindings[pCreateInfo->bindingCount - 1].binding + 1;

   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct pvr_descriptor_set_layout, layout, 1);
   VK_MULTIALLOC_DECL(&ma,
                      struct pvr_descriptor_set_layout_binding,
                      layout_bindings,
                      binding_count);
   VK_MULTIALLOC_DECL(&ma,
                      struct pvr_sampler *,
                      immutable_samplers,
                      immutable_sampler_count);

   if (!vk_descriptor_set_layout_multizalloc(&device->vk, &ma, pCreateInfo)) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto err_free_bindings;
   }

   layout->flags = pCreateInfo->flags;

   layout->descriptor_count = descriptor_count;
   layout->dynamic_buffer_count = dynamic_buffer_count;

   layout->binding_count = binding_count;
   layout->bindings = layout_bindings;

   layout->immutable_sampler_count = immutable_sampler_count;
   layout->immutable_samplers = immutable_samplers;

   const VkDescriptorSetLayoutBindingFlagsCreateInfo *binding_flags_create_info =
      vk_find_struct_const(pCreateInfo->pNext,
                           DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO);

   if (binding_flags_create_info && !binding_flags_create_info->bindingCount)
      binding_flags_create_info = NULL;

   assert(!binding_flags_create_info ||
          binding_flags_create_info->bindingCount == binding_count);

   unsigned dynamic_buffer_idx = 0;
   for (unsigned b = 0; b < pCreateInfo->bindingCount; ++b) {
      const VkDescriptorSetLayoutBinding *binding = &bindings[b];

      if (!binding->descriptorCount)
         continue;

      struct pvr_descriptor_set_layout_binding *layout_binding =
         &layout_bindings[binding->binding];

      layout_binding->stride = pvr_descriptor_size(binding->descriptorType);

      if (vk_descriptor_type_is_dynamic(binding->descriptorType)) {
         layout_binding->offset = ~0;
         layout_binding->dynamic_buffer_idx = dynamic_buffer_idx;

         dynamic_buffer_idx += binding->descriptorCount;
      } else {
         layout_binding->dynamic_buffer_idx = ~0;
         layout_binding->offset = layout->size;

         layout->size += binding->descriptorCount * layout_binding->stride;
      }

      layout_binding->type = binding->descriptorType;

      layout_binding->flags = binding_flags_create_info
                                 ? binding_flags_create_info->pBindingFlags[b]
                                 : 0;

      layout_binding->descriptor_count = binding->descriptorCount;
      layout_binding->stage_flags = binding->stageFlags;
      layout->stage_flags |= binding->stageFlags;

      if (binding_has_immutable_samplers(binding)) {
         layout_binding->immutable_sampler_count = binding->descriptorCount;
         layout_binding->immutable_samplers = immutable_samplers;
         immutable_samplers += binding->descriptorCount;

         for (unsigned s = 0; s < layout_binding->immutable_sampler_count;
              ++s) {
            VK_FROM_HANDLE(pvr_sampler,
                           sampler,
                           binding->pImmutableSamplers[s]);
            layout_binding->immutable_samplers[s] = sampler;
         }
      }
   }

   assert(dynamic_buffer_count == dynamic_buffer_idx);

   free(bindings);

   *pSetLayout = pvr_descriptor_set_layout_to_handle(layout);

   return VK_SUCCESS;

err_free_bindings:
   free(bindings);

   return result;
}

VkResult pvr_CreateDescriptorPool(VkDevice _device,
                                  const VkDescriptorPoolCreateInfo *pCreateInfo,
                                  const VkAllocationCallbacks *pAllocator,
                                  VkDescriptorPool *pDescriptorPool)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   const uint32_t cache_line_size =
      pvr_get_slc_cache_line_size(&device->pdevice->dev_info);
   struct pvr_descriptor_pool *pool;
   uint64_t bo_size = 0;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);

   pool = vk_object_alloc(&device->vk,
                          pAllocator,
                          sizeof(*pool),
                          VK_OBJECT_TYPE_DESCRIPTOR_POOL);
   if (!pool)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (pAllocator)
      pool->alloc = *pAllocator;
   else
      pool->alloc = device->vk.alloc;

   pool->flags = pCreateInfo->flags;

   list_inithead(&pool->desc_sets);

   if (pCreateInfo->maxSets > 0) {
      for (uint32_t i = 0; i < pCreateInfo->poolSizeCount; i++) {
         const VkDescriptorType type = pCreateInfo->pPoolSizes[i].type;
         const uint32_t descriptor_count =
            pCreateInfo->pPoolSizes[i].descriptorCount;

         if (vk_descriptor_type_is_dynamic(type))
            continue;

         bo_size += descriptor_count * pvr_descriptor_size(type);
      }
   }

   result = pvr_bo_suballoc(&device->suballoc_general,
                            bo_size,
                            cache_line_size,
                            false,
                            &pool->pvr_bo);

   if (result != VK_SUCCESS)
      goto err_free_pool;

   pool->mapping = pvr_bo_suballoc_get_map_addr(pool->pvr_bo);
   assert(pool->mapping);

   util_vma_heap_init(&pool->heap, pool->pvr_bo->dev_addr.addr, bo_size);

   *pDescriptorPool = pvr_descriptor_pool_to_handle(pool);

   return VK_SUCCESS;

err_free_pool:
   vk_object_free(&device->vk, pAllocator, pool);

   return result;
}

static VkResult pvr_pool_alloc(struct pvr_descriptor_pool *pool,
                               unsigned size,
                               pvr_dev_addr_t *dev_addr,
                               void **mapping)
{
   uint64_t _dev_addr = util_vma_heap_alloc(&pool->heap, size, 1);
   if (!_dev_addr)
      return VK_ERROR_OUT_OF_POOL_MEMORY;

   *mapping =
      (uint8_t *)pool->mapping + (_dev_addr - pool->pvr_bo->dev_addr.addr);

   *dev_addr = PVR_DEV_ADDR(_dev_addr);

   return VK_SUCCESS;
}

static void pvr_pool_free(struct pvr_descriptor_pool *pool,
                          pvr_dev_addr_t *dev_addr,
                          unsigned size)
{
   util_vma_heap_free(&pool->heap, dev_addr->addr, size);
}

static void pvr_free_descriptor_set(struct pvr_device *device,
                                    struct pvr_descriptor_pool *pool,
                                    struct pvr_descriptor_set *set)
{
   list_del(&set->link);
   vk_descriptor_set_layout_unref(&device->vk, &set->layout->vk);
   if (set->size > 0)
      pvr_pool_free(pool, &set->dev_addr, set->size);
   vk_object_free(&device->vk, &pool->alloc, set);
}

void pvr_DestroyDescriptorPool(VkDevice _device,
                               VkDescriptorPool _pool,
                               const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   VK_FROM_HANDLE(pvr_descriptor_pool, pool, _pool);

   if (!pool)
      return;

   list_for_each_entry_safe (struct pvr_descriptor_set,
                             set,
                             &pool->desc_sets,
                             link) {
      pvr_free_descriptor_set(device, pool, set);
   }

   util_vma_heap_finish(&pool->heap);
   pvr_bo_suballoc_free(pool->pvr_bo);

   vk_object_free(&device->vk, pAllocator, pool);
}

VkResult pvr_ResetDescriptorPool(VkDevice _device,
                                 VkDescriptorPool descriptorPool,
                                 VkDescriptorPoolResetFlags flags)
{
   VK_FROM_HANDLE(pvr_descriptor_pool, pool, descriptorPool);
   VK_FROM_HANDLE(pvr_device, device, _device);

   list_for_each_entry_safe (struct pvr_descriptor_set,
                             set,
                             &pool->desc_sets,
                             link) {
      pvr_free_descriptor_set(device, pool, set);
   }

   return VK_SUCCESS;
}

static void
write_sampler(const struct pvr_descriptor_set *set,
              const VkDescriptorImageInfo *image_info,
              const struct pvr_descriptor_set_layout_binding *binding,
              uint32_t elem);

static void
write_immutable_samplers(struct pvr_descriptor_set_layout *layout,
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

static VkResult
pvr_descriptor_set_create(struct pvr_device *device,
                          struct pvr_descriptor_pool *pool,
                          struct pvr_descriptor_set_layout *layout,
                          struct pvr_descriptor_set **const descriptor_set_out)
{
   struct pvr_descriptor_set *set;
   unsigned set_alloc_size;
   VkResult result;

   *descriptor_set_out = NULL;

   set_alloc_size = sizeof(*set);
   set_alloc_size +=
      layout->dynamic_buffer_count * sizeof(*set->dynamic_buffers);

   set = vk_object_zalloc(&device->vk,
                          &pool->alloc,
                          set_alloc_size,
                          VK_OBJECT_TYPE_DESCRIPTOR_SET);
   if (!set)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   set->size = layout->size;
   if (set->size > 0) {
      result = pvr_pool_alloc(pool, set->size, &set->dev_addr, &set->mapping);
      if (result != VK_SUCCESS)
         goto err_free_descriptor_set;
   }

   vk_descriptor_set_layout_ref(&layout->vk);
   set->layout = layout;
   set->pool = pool;

   list_addtail(&set->link, &pool->desc_sets);

   /* Setup immutable samplers. */
   write_immutable_samplers(layout, set);

   *descriptor_set_out = set;

   return VK_SUCCESS;

err_free_descriptor_set:
   vk_object_free(&device->vk, &pool->alloc, set);

   return result;
}

VkResult
pvr_AllocateDescriptorSets(VkDevice _device,
                           const VkDescriptorSetAllocateInfo *pAllocateInfo,
                           VkDescriptorSet *pDescriptorSets)
{
   VK_FROM_HANDLE(pvr_descriptor_pool, pool, pAllocateInfo->descriptorPool);
   VK_FROM_HANDLE(pvr_device, device, _device);
   VkResult result;
   uint32_t i;

   vk_foreach_struct_const (ext, pAllocateInfo->pNext) {
      vk_debug_ignored_stype(ext->sType);
   }

   for (i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
      VK_FROM_HANDLE(pvr_descriptor_set_layout,
                     layout,
                     pAllocateInfo->pSetLayouts[i]);
      struct pvr_descriptor_set *set;

      result = pvr_descriptor_set_create(device, pool, layout, &set);
      if (result != VK_SUCCESS)
         goto err_free_descriptor_sets;

      pDescriptorSets[i] = pvr_descriptor_set_to_handle(set);
   }

   return VK_SUCCESS;

err_free_descriptor_sets:
   pvr_FreeDescriptorSets(_device,
                          pAllocateInfo->descriptorPool,
                          i,
                          pDescriptorSets);

   for (i = 0; i < pAllocateInfo->descriptorSetCount; i++)
      pDescriptorSets[i] = VK_NULL_HANDLE;

   return result;
}

VkResult pvr_FreeDescriptorSets(VkDevice _device,
                                VkDescriptorPool descriptorPool,
                                uint32_t count,
                                const VkDescriptorSet *pDescriptorSets)
{
   VK_FROM_HANDLE(pvr_descriptor_pool, pool, descriptorPool);
   VK_FROM_HANDLE(pvr_device, device, _device);

   for (uint32_t i = 0; i < count; i++) {
      struct pvr_descriptor_set *set;

      if (!pDescriptorSets[i])
         continue;

      set = pvr_descriptor_set_from_handle(pDescriptorSets[i]);
      pvr_free_descriptor_set(device, pool, set);
   }

   return VK_SUCCESS;
}

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

   VK_FROM_HANDLE(pvr_sampler, info_sampler, image_info->sampler);
   struct pvr_sampler *sampler = binding->immutable_sampler_count
                                    ? binding->immutable_samplers[elem]
                                    : info_sampler;

   image_sampler_desc.sampler = sampler->descriptor;

   if (image_info->imageView != VK_NULL_HANDLE) {
      VK_FROM_HANDLE(pvr_image_view, image_view, image_info->imageView);
      image_sampler_desc.image =
         image_view->image_state[PVR_TEXTURE_STATE_SAMPLE];
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

void pvr_UpdateDescriptorSets(VkDevice _device,
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

      vk_foreach_struct_const (ext, write->pNext) {
         vk_debug_ignored_stype(ext->sType);
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

      vk_foreach_struct_const (ext, copy->pNext) {
         vk_debug_ignored_stype(ext->sType);
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

void pvr_GetDescriptorSetLayoutSupport(
   VkDevice _device,
   const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
   VkDescriptorSetLayoutSupport *pSupport)
{
   uint32_t descriptor_count = 0;

   for (uint32_t i = 0; i < pCreateInfo->bindingCount; i++)
      descriptor_count += pCreateInfo->pBindings[i].descriptorCount;

   pSupport->supported = descriptor_count <= PVR_MAX_DESCRIPTORS_PER_SET;
}

void pvr_UpdateDescriptorSetWithTemplate(
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

      default:
         UNREACHABLE("Unknown descriptor type");
      }
   }
}
