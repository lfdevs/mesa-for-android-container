/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_descriptor_set.h"

#include "nvk_buffer.h"
#include "nvk_buffer_view.h"
#include "nvk_descriptor_set_layout.h"
#include "nvk_device.h"
#include "nvk_entrypoints.h"
#include "nvk_format.h"
#include "nvk_image_view.h"
#include "nvk_physical_device.h"
#include "nvk_sampler.h"
#include "nvkmd/nvkmd.h"

#include "util/format/u_format.h"

#include "clb097.h"

static inline uint32_t
align_u32(uint32_t v, uint32_t a)
{
   assert(a != 0 && a == (a & -a));
   return (v + a - 1) & ~(a - 1);
}

struct nvk_descriptor_writer {
   const struct nvk_physical_device *pdev;
   struct nvk_descriptor_set_layout *layout;
   struct nvk_descriptor_set *set;
   void *set_map;
   uint32_t set_size_B;
   uint32_t dirty_start;
   uint32_t dirty_end;
};

static void
nvk_descriptor_writer_init(const struct nvk_physical_device *pdev,
                           struct nvk_descriptor_writer *w)
{
   *w = (struct nvk_descriptor_writer) {
      .pdev = pdev,
      .dirty_start = UINT32_MAX,
      .dirty_end = 0,
   };
}

static void
nvk_descriptor_writer_init_push(const struct nvk_physical_device *pdev,
                                struct nvk_descriptor_writer *w,
                                struct nvk_descriptor_set_layout *layout,
                                struct nvk_push_descriptor_set *push_set)
{
   nvk_descriptor_writer_init(pdev, w);
   w->layout = layout;
   w->set_map = push_set->data;
   w->set_size_B = sizeof(push_set->data);
}

static void
nvk_descriptor_writer_init_set(const struct nvk_physical_device *pdev,
                               struct nvk_descriptor_writer *w,
                               struct nvk_descriptor_set *set)
{
   nvk_descriptor_writer_init(pdev, w);
   w->layout = set->layout;
   w->set = set;
   w->set_map = set->map;
   w->set_size_B = set->size_B;
}

static void
nvk_descriptor_writer_finish(struct nvk_descriptor_writer *w)
{
   if (w->set != NULL && w->set->pool->mem != NULL &&
       w->dirty_start < w->dirty_end) {

      /* Our flush needs to be aligned */
      uint32_t align_B = w->pdev->info.nc_atom_size_B;
      w->dirty_start = ROUND_DOWN_TO(w->dirty_start, align_B);
      w->dirty_end = align(w->dirty_end, align_B);

      nvkmd_mem_sync_map_to_gpu(w->set->pool->mem,
                                w->set->mem_offset_B + w->dirty_start,
                                w->dirty_end - w->dirty_start);
   }
}

static void
nvk_descriptor_writer_next_set(struct nvk_descriptor_writer *w,
                               struct nvk_descriptor_set *set)
{
   const struct nvk_physical_device *pdev = w->pdev;

   /* If we're writing to the same set, keep using the original writer as-is
    * so we don't do unnecessary extra flushing in the case where the client
    * has a lot of writes to the same set back-to-back.
    */
   if (w->set == set)
      return;

   if (w->set != NULL)
      nvk_descriptor_writer_finish(w);

   nvk_descriptor_writer_init_set(pdev, w, set);
}

static inline void *
desc_ubo_data(struct nvk_descriptor_writer *w, uint32_t binding,
              uint32_t elem, uint32_t elem_size_B)
{
   const struct nvk_descriptor_set_binding_layout *binding_layout =
      &w->layout->binding[binding];

   uint32_t offset = binding_layout->offset + elem * binding_layout->stride;
   assert(offset + elem_size_B <= w->set_size_B);

   w->dirty_start = MIN2(w->dirty_start, offset);
   w->dirty_end = MAX2(w->dirty_end, offset + elem_size_B);

   return (char *)w->set_map + offset;
}

static void
write_desc(struct nvk_descriptor_writer *w, uint32_t binding, uint32_t elem,
           const void *desc_data, size_t desc_size)
{
   void *dst = desc_ubo_data(w, binding, elem, desc_size);
   memcpy(dst, desc_data, desc_size);
}

static void
get_sampled_image_view_desc(VkDescriptorType descriptor_type,
                            const VkDescriptorImageInfo *const info,
                            struct nvk_sampled_image_descriptor *desc,
                            uint8_t *plane_count)
{
   STATIC_ASSERT(NVK_MAX_SAMPLER_PLANES <= NVK_MAX_IMAGE_PLANES);

   *plane_count = 1;

   if (descriptor_type != VK_DESCRIPTOR_TYPE_SAMPLER &&
       info && info->imageView != VK_NULL_HANDLE) {
      VK_FROM_HANDLE(nvk_image_view, view, info->imageView);

      *plane_count = view->plane_count;
      for (uint8_t plane = 0; plane < *plane_count; plane++) {
         assert(view->planes[plane].sampled_desc_index > 0);
         assert(view->planes[plane].sampled_desc_index < (1 << 20));
         desc[plane].image_index = view->planes[plane].sampled_desc_index;
      }
   }

   if (descriptor_type == VK_DESCRIPTOR_TYPE_SAMPLER ||
       descriptor_type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
      VK_FROM_HANDLE(nvk_sampler, sampler, info->sampler);

      *plane_count = MAX2(*plane_count, sampler->plane_count);

      for (uint8_t plane = 0; plane < *plane_count; plane++) {
         /* We need to replicate the last sampler plane out to all image
          * planes due to sampler table entry limitations. See
          * nvk_CreateSampler in nvk_sampler.c for more details.
          */
         uint8_t sampler_plane = MIN2(plane, sampler->plane_count - 1);
         assert(sampler->planes[sampler_plane].desc_index < (1 << 12));
         desc[plane].sampler_index = sampler->planes[sampler_plane].desc_index;
      }
   }
}

static void
write_sampled_image_view_desc(struct nvk_descriptor_writer *w,
                              const VkDescriptorImageInfo *const _info,
                              uint32_t binding, uint32_t elem,
                              VkDescriptorType descriptor_type)
{
   VkDescriptorImageInfo info = *_info;

   if (descriptor_type == VK_DESCRIPTOR_TYPE_SAMPLER ||
       descriptor_type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
      const struct nvk_descriptor_set_binding_layout *binding_layout =
         &w->layout->binding[binding];
      if (binding_layout->immutable_samplers != NULL) {
         info.sampler = nvk_sampler_to_handle(
            binding_layout->immutable_samplers[elem]);
      }
   }

   uint8_t plane_count;
   struct nvk_sampled_image_descriptor desc[NVK_MAX_IMAGE_PLANES] = { };
   get_sampled_image_view_desc(descriptor_type, &info, desc, &plane_count);
   write_desc(w, binding, elem, desc, plane_count * sizeof(desc[0]));
}

static struct nvk_storage_image_descriptor
get_storage_image_view_desc(const struct nvk_physical_device *pdev,
                            const VkDescriptorImageInfo *const info)
{
   struct nvk_storage_image_descriptor desc = { };
   assert(pdev->info.cls_eng3d >= MAXWELL_A);

   if (info && info->imageView != VK_NULL_HANDLE) {
      VK_FROM_HANDLE(nvk_image_view, view, info->imageView);

      /* Storage images are always single plane */
      assert(view->plane_count == 1);
      uint8_t plane = 0;

      assert(view->planes[plane].storage_desc_index > 0);
      assert(view->planes[plane].storage_desc_index < (1 << 20));

      desc.image_index = view->planes[plane].storage_desc_index;
   }

   return desc;
}

static struct nvk_kepler_storage_image_descriptor
get_kepler_storage_image_view_desc(const struct nvk_physical_device *pdev,
                                   const VkDescriptorImageInfo *const info)
{
   struct nvk_kepler_storage_image_descriptor desc = { };
   assert(pdev->info.cls_eng3d <= MAXWELL_A);

   if (info && info->imageView != VK_NULL_HANDLE) {
      VK_FROM_HANDLE(nvk_image_view, view, info->imageView);

      /* Storage images are always single plane */
      assert(view->plane_count == 1);

      desc.su_info = view->su_info;
   } else {
      desc.su_info = nil_fill_null_su_info(&pdev->info);
   }

   return desc;
}

static void
write_storage_image_view_desc(struct nvk_descriptor_writer *w,
                              const VkDescriptorImageInfo *const info,
                              uint32_t binding, uint32_t elem)
{
   if (w->pdev->info.cls_eng3d >= MAXWELL_A) {
      struct nvk_storage_image_descriptor desc =
         get_storage_image_view_desc(w->pdev, info);
      write_desc(w, binding, elem, &desc, sizeof(desc));
   } else {
      struct nvk_kepler_storage_image_descriptor desc =
         get_kepler_storage_image_view_desc(w->pdev, info);
      write_desc(w, binding, elem, &desc, sizeof(desc));
   }
}

static union nvk_buffer_descriptor
ubo_desc(const struct nvk_physical_device *pdev,
         struct nvk_addr_range addr_range)
{
   const uint32_t min_cbuf_alignment = nvk_min_cbuf_alignment(&pdev->info);

   assert(addr_range.addr % min_cbuf_alignment == 0);
   assert(addr_range.range <= NVK_MAX_CBUF_SIZE);

   addr_range.addr = ROUND_DOWN_TO(addr_range.addr, min_cbuf_alignment);
   addr_range.range = align(addr_range.range, min_cbuf_alignment);

   if (nvk_use_bindless_cbuf_2(&pdev->info)) {
      return (union nvk_buffer_descriptor) { .cbuf2 = {
         .base_addr_shift_6 = addr_range.addr >> 6,
         .size_shift_4 = addr_range.range >> 4,
      }};
   } else if (nvk_use_bindless_cbuf(&pdev->info)) {
      return (union nvk_buffer_descriptor) { .cbuf = {
         .base_addr_shift_4 = addr_range.addr >> 4,
         .size_shift_4 = addr_range.range >> 4,
      }};
   } else {
      return (union nvk_buffer_descriptor) { .addr = {
         .base_addr = addr_range.addr,
         .size = addr_range.range,
      }};
   }
}

static void
write_ubo_desc(struct nvk_descriptor_writer *w,
               const VkDescriptorBufferInfo *const info,
               uint32_t binding, uint32_t elem)
{
   VK_FROM_HANDLE(nvk_buffer, buffer, info->buffer);
   struct nvk_addr_range addr_range =
      nvk_buffer_addr_range(buffer, info->offset, info->range);

   const union nvk_buffer_descriptor desc = ubo_desc(w->pdev, addr_range);
   write_desc(w, binding, elem, &desc, sizeof(desc));
}

static void
write_dynamic_ubo_desc(struct nvk_descriptor_writer *w,
                       const VkDescriptorBufferInfo *const info,
                       uint32_t binding, uint32_t elem)
{
   VK_FROM_HANDLE(nvk_buffer, buffer, info->buffer);
   struct nvk_addr_range addr_range =
      nvk_buffer_addr_range(buffer, info->offset, info->range);

   const struct nvk_descriptor_set_binding_layout *binding_layout =
      &w->layout->binding[binding];
   w->set->dynamic_buffers[binding_layout->dynamic_buffer_index + elem] =
      ubo_desc(w->pdev, addr_range);
}

static union nvk_buffer_descriptor
ssbo_desc(struct nvk_addr_range addr_range)
{
   assert(addr_range.addr % NVK_MIN_SSBO_ALIGNMENT == 0);
   assert(addr_range.range <= UINT32_MAX);

   addr_range.addr = ROUND_DOWN_TO(addr_range.addr, NVK_MIN_SSBO_ALIGNMENT);
   addr_range.range = align(addr_range.range, NVK_SSBO_BOUNDS_CHECK_ALIGNMENT);

   return (union nvk_buffer_descriptor) { .addr = {
      .base_addr = addr_range.addr,
      .size = addr_range.range,
   }};
}

static void
write_ssbo_desc(struct nvk_descriptor_writer *w,
                const VkDescriptorBufferInfo *const info,
                uint32_t binding, uint32_t elem)
{
   VK_FROM_HANDLE(nvk_buffer, buffer, info->buffer);
   struct nvk_addr_range addr_range =
      nvk_buffer_addr_range(buffer, info->offset, info->range);

   const union nvk_buffer_descriptor desc = ssbo_desc(addr_range);
   write_desc(w, binding, elem, &desc, sizeof(desc));
}

static void
write_dynamic_ssbo_desc(struct nvk_descriptor_writer *w,
                        const VkDescriptorBufferInfo *const info,
                        uint32_t binding, uint32_t elem)
{
   VK_FROM_HANDLE(nvk_buffer, buffer, info->buffer);
   struct nvk_addr_range addr_range =
      nvk_buffer_addr_range(buffer, info->offset, info->range);

   const struct nvk_descriptor_set_binding_layout *binding_layout =
      &w->layout->binding[binding];
   w->set->dynamic_buffers[binding_layout->dynamic_buffer_index + elem] =
      ssbo_desc(addr_range);
}

static struct nvk_edb_buffer_view_descriptor
get_edb_buffer_view_desc(struct nvk_device *dev,
                         const VkDescriptorAddressInfoEXT *info)
{
   struct nvk_edb_buffer_view_descriptor desc = { };
   if (info != NULL && info->address != 0) {
      enum pipe_format format = nvk_format_to_pipe_format(info->format);
      desc = nvk_edb_bview_cache_get_descriptor(dev, &dev->edb_bview_cache,
                                                info->address, info->range,
                                                format);
   }
   return desc;
}

static void
write_buffer_view_desc(struct nvk_descriptor_writer *w,
                       const VkBufferView bufferView,
                       uint32_t binding, uint32_t elem,
                       VkDescriptorType descType)
{
   VK_FROM_HANDLE(nvk_buffer_view, view, bufferView);

   if (nvk_use_edb_buffer_views(w->pdev)) {
      struct nvk_edb_buffer_view_descriptor desc = { };
      if (view != NULL)
         desc = view->edb_desc;
      write_desc(w, binding, elem, &desc, sizeof(desc));
   } else if (w->pdev->info.cls_eng3d >= MAXWELL_A ||
              descType == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER) {
      struct nvk_buffer_view_descriptor desc = { };
      if (view != NULL)
         desc = view->desc;
      write_desc(w, binding, elem, &desc, sizeof(desc));
   } else {// Kepler
      struct nvk_kepler_storage_buffer_view_descriptor desc = { };
      assert(descType == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);

      if (view != NULL)
         desc.su_info = view->su_info;
      else
         desc.su_info = nil_fill_null_su_info(&w->pdev->info);

      write_desc(w, binding, elem, &desc, sizeof(desc));
   }
}

static void
write_inline_uniform_data(struct nvk_descriptor_writer *w,
                          const VkWriteDescriptorSetInlineUniformBlock *info,
                          uint32_t binding, uint32_t offset)
{
   assert(w->layout->binding[binding].stride == 1);
   write_desc(w, binding, offset, info->pData, info->dataSize);
}

VKAPI_ATTR void VKAPI_CALL
nvk_UpdateDescriptorSets(VkDevice device,
                         uint32_t descriptorWriteCount,
                         const VkWriteDescriptorSet *pDescriptorWrites,
                         uint32_t descriptorCopyCount,
                         const VkCopyDescriptorSet *pDescriptorCopies)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   struct nvk_descriptor_writer w;
   nvk_descriptor_writer_init(pdev, &w);

   for (uint32_t i = 0; i < descriptorWriteCount; i++) {
      const VkWriteDescriptorSet *write = &pDescriptorWrites[i];
      VK_FROM_HANDLE(nvk_descriptor_set, set, write->dstSet);

      nvk_descriptor_writer_next_set(&w, set);

      switch (write->descriptorType) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            write_sampled_image_view_desc(&w, write->pImageInfo + j,
                                          write->dstBinding,
                                          write->dstArrayElement + j,
                                          write->descriptorType);
         }
         break;

      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            write_storage_image_view_desc(&w, write->pImageInfo + j,
                                          write->dstBinding,
                                          write->dstArrayElement + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            write_buffer_view_desc(&w, write->pTexelBufferView[j],
                                   write->dstBinding, write->dstArrayElement + j,
                                   write->descriptorType);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            write_ubo_desc(&w, write->pBufferInfo + j,
                           write->dstBinding,
                           write->dstArrayElement + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            write_ssbo_desc(&w, write->pBufferInfo + j,
                            write->dstBinding,
                            write->dstArrayElement + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            write_dynamic_ubo_desc(&w, write->pBufferInfo + j,
                                   write->dstBinding,
                                   write->dstArrayElement + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            write_dynamic_ssbo_desc(&w, write->pBufferInfo + j,
                                    write->dstBinding,
                                    write->dstArrayElement + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK: {
         const VkWriteDescriptorSetInlineUniformBlock *write_inline =
            vk_find_struct_const(write->pNext,
                                 WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK);
         assert(write_inline->dataSize == write->descriptorCount);
         write_inline_uniform_data(&w, write_inline, write->dstBinding,
                                   write->dstArrayElement);
         break;
      }

      default:
         break;
      }

      nvk_descriptor_writer_finish(&w);
   }

   for (uint32_t i = 0; i < descriptorCopyCount; i++) {
      const VkCopyDescriptorSet *copy = &pDescriptorCopies[i];
      VK_FROM_HANDLE(nvk_descriptor_set, src, copy->srcSet);
      VK_FROM_HANDLE(nvk_descriptor_set, dst, copy->dstSet);

      nvk_descriptor_writer_next_set(&w, dst);

      /* This one is actually a reader */
      struct nvk_descriptor_writer r;
      nvk_descriptor_writer_init_set(pdev, &r, src);

      const struct nvk_descriptor_set_binding_layout *src_binding_layout =
         &src->layout->binding[copy->srcBinding];
      const struct nvk_descriptor_set_binding_layout *dst_binding_layout =
         &dst->layout->binding[copy->dstBinding];

      if (dst_binding_layout->stride > 0 && src_binding_layout->stride > 0) {
         for (uint32_t j = 0; j < copy->descriptorCount; j++) {
            const uint32_t copy_size = MIN2(dst_binding_layout->stride,
                                            src_binding_layout->stride);
            const void *src_map = desc_ubo_data(&r, copy->srcBinding,
                                                copy->srcArrayElement + j,
                                                copy_size);
            write_desc(&w, copy->dstBinding,
                       copy->dstArrayElement + j,
                       src_map, copy_size);
         }
      }

      if (vk_descriptor_type_is_dynamic(src_binding_layout->type)) {
         const uint32_t dst_dyn_start =
            dst_binding_layout->dynamic_buffer_index + copy->dstArrayElement;
         const uint32_t src_dyn_start =
            src_binding_layout->dynamic_buffer_index + copy->srcArrayElement;
         typed_memcpy(&dst->dynamic_buffers[dst_dyn_start],
                      &src->dynamic_buffers[src_dyn_start],
                      copy->descriptorCount);
      }
   }

   nvk_descriptor_writer_finish(&w);
}

void
nvk_push_descriptor_set_update(struct nvk_device *dev,
                               struct nvk_push_descriptor_set *push_set,
                               struct nvk_descriptor_set_layout *layout,
                               uint32_t write_count,
                               const VkWriteDescriptorSet *writes)
{
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   assert(layout->non_variable_descriptor_buffer_size < sizeof(push_set->data));
   struct nvk_descriptor_writer w;
   nvk_descriptor_writer_init_push(pdev, &w, layout, push_set);

   for (uint32_t i = 0; i < write_count; i++) {
      const VkWriteDescriptorSet *write = &writes[i];
      assert(write->dstSet == VK_NULL_HANDLE);

      switch (write->descriptorType) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            write_sampled_image_view_desc(&w, write->pImageInfo + j,
                                          write->dstBinding,
                                          write->dstArrayElement + j,
                                          write->descriptorType);
         }
         break;

      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            write_storage_image_view_desc(&w, write->pImageInfo + j,
                                          write->dstBinding,
                                          write->dstArrayElement + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            write_buffer_view_desc(&w, write->pTexelBufferView[j],
                                   write->dstBinding, write->dstArrayElement + j,
                                   write->descriptorType);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            write_ubo_desc(&w, write->pBufferInfo + j,
                           write->dstBinding,
                           write->dstArrayElement + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            write_ssbo_desc(&w, write->pBufferInfo + j,
                            write->dstBinding,
                            write->dstArrayElement + j);
         }
         break;

      default:
         break;
      }
   }

   nvk_descriptor_writer_finish(&w);
}

static void
nvk_descriptor_pool_free(struct nvk_descriptor_pool *pool,
                         uint64_t offset_B, uint64_t size_B);

static void
nvk_descriptor_set_destroy(struct nvk_device *dev,
                           struct nvk_descriptor_pool *pool,
                           struct nvk_descriptor_set *set)
{
   list_del(&set->link);
   if (set->size_B > 0)
      nvk_descriptor_pool_free(pool, set->mem_offset_B, set->size_B);
   vk_descriptor_set_layout_unref(&dev->vk, &set->layout->vk);

   vk_object_free(&dev->vk, NULL, set);
}

static void
nvk_destroy_descriptor_pool(struct nvk_device *dev,
                            const VkAllocationCallbacks *pAllocator,
                            struct nvk_descriptor_pool *pool)
{
   list_for_each_entry_safe(struct nvk_descriptor_set, set, &pool->sets, link)
      nvk_descriptor_set_destroy(dev, pool, set);

   util_vma_heap_finish(&pool->heap);

   if (pool->mem != NULL)
      nvkmd_mem_unref(pool->mem);

   if (pool->host_mem != NULL)
      vk_free2(&dev->vk.alloc, pAllocator, pool->host_mem);

   vk_object_free(&dev->vk, pAllocator, pool);
}

#define HEAP_START 0xc0ffee0000000000ull

static uint32_t
min_set_align_B(const struct nvk_physical_device *pdev)
{
   const uint32_t min_cbuf_alignment = nvk_min_cbuf_alignment(&pdev->info);
   return MAX2(min_cbuf_alignment, pdev->info.nc_atom_size_B);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_CreateDescriptorPool(VkDevice _device,
                         const VkDescriptorPoolCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator,
                         VkDescriptorPool *pDescriptorPool)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);
   struct nvk_descriptor_pool *pool;
   VkResult result;

   pool = vk_object_zalloc(&dev->vk, pAllocator, sizeof(*pool),
                           VK_OBJECT_TYPE_DESCRIPTOR_POOL);
   if (!pool)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   list_inithead(&pool->sets);

   const VkMutableDescriptorTypeCreateInfoEXT *mutable_info =
      vk_find_struct_const(pCreateInfo->pNext,
                           MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT);

   uint32_t max_align = 0;
   for (unsigned i = 0; i < pCreateInfo->poolSizeCount; ++i) {
      const VkMutableDescriptorTypeListEXT *type_list = NULL;
      if (pCreateInfo->pPoolSizes[i].type == VK_DESCRIPTOR_TYPE_MUTABLE_EXT &&
          mutable_info && i < mutable_info->mutableDescriptorTypeListCount)
            type_list = &mutable_info->pMutableDescriptorTypeLists[i];

      uint32_t stride, alignment;
      nvk_descriptor_stride_align_for_type(pdev, 0 /* not DESCRIPTOR_BUFFER */,
                                           pCreateInfo->pPoolSizes[i].type,
                                           type_list, &stride, &alignment);
      max_align = MAX2(max_align, alignment);
   }

   uint64_t mem_size = 0;
   for (unsigned i = 0; i < pCreateInfo->poolSizeCount; ++i) {
      const VkMutableDescriptorTypeListEXT *type_list = NULL;
      if (pCreateInfo->pPoolSizes[i].type == VK_DESCRIPTOR_TYPE_MUTABLE_EXT &&
          mutable_info && i < mutable_info->mutableDescriptorTypeListCount)
            type_list = &mutable_info->pMutableDescriptorTypeLists[i];

      uint32_t stride, alignment;
      nvk_descriptor_stride_align_for_type(pdev, 0 /* not DESCRIPTOR_BUFFER */,
                                           pCreateInfo->pPoolSizes[i].type,
                                           type_list, &stride, &alignment);
      mem_size += MAX2(stride, max_align) *
                 pCreateInfo->pPoolSizes[i].descriptorCount;
   }

   /* Individual descriptor sets are aligned to the min UBO alignment to
    * ensure that we don't end up with unaligned data access in any shaders.
    * This means that each descriptor buffer allocated may burn up to 16B of
    * extra space to get the right alignment.  (Technically, it's at most 28B
    * because we're always going to start at least 4B aligned but we're being
    * conservative here.)  Allocate enough extra space that we can chop it
    * into maxSets pieces and align each one of them to 32B.
    */
   mem_size += min_set_align_B(pdev) * pCreateInfo->maxSets;

   if (mem_size > 0) {
      if (pCreateInfo->flags & VK_DESCRIPTOR_POOL_CREATE_HOST_ONLY_BIT_EXT) {
         pool->host_mem = vk_zalloc2(&dev->vk.alloc, pAllocator, mem_size,
                                     16, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
         if (pool->host_mem == NULL) {
            nvk_destroy_descriptor_pool(dev, pAllocator, pool);
            return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
         }
      } else {
         result = nvkmd_dev_alloc_mapped_mem(dev->nvkmd, &dev->vk.base,
                                             mem_size, 0, NVKMD_MEM_LOCAL,
                                             NVKMD_MEM_MAP_WR, &pool->mem);
         if (result != VK_SUCCESS) {
            nvk_destroy_descriptor_pool(dev, pAllocator, pool);
            return result;
         }

         /* The BO may be larger thanks to GPU page alignment.  We may as well
          * make that extra space available to the client.
          */
         assert(pool->mem->size_B >= mem_size);
         mem_size = pool->mem->size_B;
      }

      util_vma_heap_init(&pool->heap, HEAP_START, mem_size);
   } else {
      util_vma_heap_init(&pool->heap, 0, 0);
   }

   pool->mem_size_B = mem_size;

   *pDescriptorPool = nvk_descriptor_pool_to_handle(pool);
   return VK_SUCCESS;
}

static VkResult
nvk_descriptor_pool_alloc(struct nvk_descriptor_pool *pool,
                          uint64_t size_B, uint64_t align_B,
                          uint64_t *offset_B_out)
{
   assert(size_B > 0);
   assert(size_B % align_B == 0);

   if (size_B > pool->heap.free_size)
      return VK_ERROR_OUT_OF_POOL_MEMORY;

   uint64_t addr = util_vma_heap_alloc(&pool->heap, size_B, align_B);
   if (addr == 0)
      return VK_ERROR_FRAGMENTED_POOL;

   assert(addr >= HEAP_START);
   assert(addr + size_B <= HEAP_START + pool->mem_size_B);
   *offset_B_out = addr - HEAP_START;

   return VK_SUCCESS;
}

static void
nvk_descriptor_pool_free(struct nvk_descriptor_pool *pool,
                         uint64_t offset_B, uint64_t size_B)
{
   assert(size_B > 0);
   assert(offset_B + size_B <= pool->mem_size_B);
   util_vma_heap_free(&pool->heap, HEAP_START + offset_B, size_B);
}

static VkResult
nvk_descriptor_set_create(struct nvk_device *dev,
                          struct nvk_descriptor_pool *pool,
                          struct nvk_descriptor_set_layout *layout,
                          uint32_t variable_count,
                          struct nvk_descriptor_set **out_set)
{
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);
   struct nvk_descriptor_set *set;
   VkResult result;

   uint32_t mem_size = sizeof(struct nvk_descriptor_set) +
      layout->dynamic_buffer_count * sizeof(struct nvk_buffer_address);

   set = vk_object_zalloc(&dev->vk, NULL, mem_size,
                          VK_OBJECT_TYPE_DESCRIPTOR_SET);
   if (!set)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   set->pool = pool;
   set->size_B = layout->non_variable_descriptor_buffer_size;

   if (layout->binding_count > 0 &&
       (layout->binding[layout->binding_count - 1].flags &
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT)) {
      uint32_t stride = layout->binding[layout->binding_count-1].stride;
      set->size_B += stride * variable_count;
   }

   uint32_t align_B = min_set_align_B(pdev);
   set->size_B = align64(set->size_B, align_B);

   if (set->size_B > 0) {
      result = nvk_descriptor_pool_alloc(pool, set->size_B, align_B,
                                         &set->mem_offset_B);
      if (result != VK_SUCCESS) {
         vk_object_free(&dev->vk, NULL, set);
         return result;
      }

      if (pool->host_mem != NULL) {
         set->map = pool->host_mem + set->mem_offset_B;
      } else {
         set->map = pool->mem->map + set->mem_offset_B;
      }
   }

   vk_descriptor_set_layout_ref(&layout->vk);
   set->layout = layout;

   struct nvk_descriptor_writer w;
   nvk_descriptor_writer_init_set(pdev, &w, set);
   for (uint32_t b = 0; b < layout->binding_count; b++) {
      if (layout->binding[b].type != VK_DESCRIPTOR_TYPE_SAMPLER &&
          layout->binding[b].type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
         continue;

      if (layout->binding[b].immutable_samplers == NULL)
         continue;

      uint32_t array_size = layout->binding[b].array_size;
      if (layout->binding[b].flags &
          VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT)
         array_size = variable_count;

      const VkDescriptorImageInfo empty = {};
      for (uint32_t j = 0; j < array_size; j++) {
         write_sampled_image_view_desc(&w, &empty, b, j,
                                       layout->binding[b].type);
      }
   }
   nvk_descriptor_writer_finish(&w);

   list_addtail(&set->link, &pool->sets);
   *out_set = set;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_AllocateDescriptorSets(VkDevice device,
                           const VkDescriptorSetAllocateInfo *pAllocateInfo,
                           VkDescriptorSet *pDescriptorSets)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   VK_FROM_HANDLE(nvk_descriptor_pool, pool, pAllocateInfo->descriptorPool);

   VkResult result = VK_SUCCESS;
   uint32_t i;

   struct nvk_descriptor_set *set = NULL;

   const VkDescriptorSetVariableDescriptorCountAllocateInfo *var_desc_count =
      vk_find_struct_const(pAllocateInfo->pNext,
                           DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO);

   /* allocate a set of buffers for each shader to contain descriptors */
   for (i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
      VK_FROM_HANDLE(nvk_descriptor_set_layout, layout,
                     pAllocateInfo->pSetLayouts[i]);
      /* If descriptorSetCount is zero or this structure is not included in
       * the pNext chain, then the variable lengths are considered to be zero.
       */
      const uint32_t variable_count =
         var_desc_count && var_desc_count->descriptorSetCount > 0 ?
         var_desc_count->pDescriptorCounts[i] : 0;

      result = nvk_descriptor_set_create(dev, pool, layout,
                                         variable_count, &set);
      if (result != VK_SUCCESS)
         break;

      pDescriptorSets[i] = nvk_descriptor_set_to_handle(set);
   }

   if (result != VK_SUCCESS) {
      nvk_FreeDescriptorSets(device, pAllocateInfo->descriptorPool, i, pDescriptorSets);
      for (i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
         pDescriptorSets[i] = VK_NULL_HANDLE;
      }
   }
   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_FreeDescriptorSets(VkDevice device,
                       VkDescriptorPool descriptorPool,
                       uint32_t descriptorSetCount,
                       const VkDescriptorSet *pDescriptorSets)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   VK_FROM_HANDLE(nvk_descriptor_pool, pool, descriptorPool);

   for (uint32_t i = 0; i < descriptorSetCount; i++) {
      VK_FROM_HANDLE(nvk_descriptor_set, set, pDescriptorSets[i]);

      if (set)
         nvk_descriptor_set_destroy(dev, pool, set);
   }
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvk_DestroyDescriptorPool(VkDevice device,
                          VkDescriptorPool _pool,
                          const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   VK_FROM_HANDLE(nvk_descriptor_pool, pool, _pool);

   if (!_pool)
      return;

   nvk_destroy_descriptor_pool(dev, pAllocator, pool);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_ResetDescriptorPool(VkDevice device,
                        VkDescriptorPool descriptorPool,
                        VkDescriptorPoolResetFlags flags)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   VK_FROM_HANDLE(nvk_descriptor_pool, pool, descriptorPool);

   list_for_each_entry_safe(struct nvk_descriptor_set, set, &pool->sets, link)
      nvk_descriptor_set_destroy(dev, pool, set);

   return VK_SUCCESS;
}

static void
write_from_template(struct nvk_descriptor_writer *w,
                    const struct vk_descriptor_update_template *template,
                    const void *data)
{
   for (uint32_t i = 0; i < template->entry_count; i++) {
      const struct vk_descriptor_template_entry *entry =
         &template->entries[i];

      switch (entry->type) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorImageInfo *info =
               data + entry->offset + j * entry->stride;

            write_sampled_image_view_desc(w, info,
                                          entry->binding,
                                          entry->array_element + j,
                                          entry->type);
         }
         break;

      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorImageInfo *info =
               data + entry->offset + j * entry->stride;

            write_storage_image_view_desc(w, info,
                                          entry->binding,
                                          entry->array_element + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkBufferView *bview =
               data + entry->offset + j * entry->stride;

            write_buffer_view_desc(w, *bview,
                                   entry->binding,
                                   entry->array_element + j,
                                   entry->type);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorBufferInfo *info =
               data + entry->offset + j * entry->stride;

            write_ubo_desc(w, info,
                           entry->binding,
                           entry->array_element + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorBufferInfo *info =
               data + entry->offset + j * entry->stride;

            write_ssbo_desc(w, info,
                            entry->binding,
                            entry->array_element + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorBufferInfo *info =
               data + entry->offset + j * entry->stride;

            write_dynamic_ubo_desc(w, info,
                                   entry->binding,
                                   entry->array_element + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorBufferInfo *info =
               data + entry->offset + j * entry->stride;

            write_dynamic_ssbo_desc(w, info,
                                    entry->binding,
                                    entry->array_element + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
         write_desc(w,
                    entry->binding,
                    entry->array_element,
                    data + entry->offset,
                    entry->array_count);
         break;

      default:
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_UpdateDescriptorSetWithTemplate(VkDevice device,
                                    VkDescriptorSet descriptorSet,
                                    VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                    const void *pData)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   VK_FROM_HANDLE(nvk_descriptor_set, set, descriptorSet);
   VK_FROM_HANDLE(vk_descriptor_update_template, template,
                  descriptorUpdateTemplate);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   struct nvk_descriptor_writer w;
   nvk_descriptor_writer_init_set(pdev, &w, set);
   write_from_template(&w, template, pData);
   nvk_descriptor_writer_finish(&w);
}

void
nvk_push_descriptor_set_update_template(
   struct nvk_device *dev,
   struct nvk_push_descriptor_set *push_set,
   struct nvk_descriptor_set_layout *layout,
   const struct vk_descriptor_update_template *template,
   const void *data)
{
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   struct nvk_descriptor_writer w;
   nvk_descriptor_writer_init_push(pdev, &w, layout, push_set);
   write_from_template(&w, template, data);
   nvk_descriptor_writer_finish(&w);
}

VKAPI_ATTR void VKAPI_CALL
nvk_GetDescriptorEXT(VkDevice _device,
                     const VkDescriptorGetInfoEXT *pDescriptorInfo,
                     size_t dataSize, void *pDescriptor)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   switch (pDescriptorInfo->type) {
   case VK_DESCRIPTOR_TYPE_SAMPLER: {
      const VkDescriptorImageInfo info = {
         .sampler = *pDescriptorInfo->data.pSampler,
      };
      uint8_t plane_count;
      struct nvk_sampled_image_descriptor desc = { };
      get_sampled_image_view_desc(VK_DESCRIPTOR_TYPE_SAMPLER,
                                  &info, &desc, &plane_count);
      assert(plane_count == 1);
      assert(sizeof(desc) <= dataSize);
      memcpy(pDescriptor, &desc, sizeof(desc));
      break;
   }

   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
      uint8_t plane_count;
      struct nvk_sampled_image_descriptor desc[NVK_MAX_IMAGE_PLANES] = { };
      get_sampled_image_view_desc(pDescriptorInfo->type,
                                  pDescriptorInfo->data.pCombinedImageSampler,
                                  desc, &plane_count);
      assert(plane_count <= NVK_MAX_IMAGE_PLANES);
      assert(plane_count * sizeof(desc[0]) <= dataSize);
      memcpy(pDescriptor, desc, plane_count * sizeof(desc[0]));
      break;
   }

   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: {
      uint8_t plane_count;
      struct nvk_sampled_image_descriptor desc = {};
      get_sampled_image_view_desc(pDescriptorInfo->type,
                                  pDescriptorInfo->data.pSampledImage,
                                  &desc, &plane_count);
      assert(plane_count == 1);
      assert(sizeof(desc) <= dataSize);
      memcpy(pDescriptor, &desc, sizeof(desc));
      break;
   }

   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      if (pdev->info.cls_eng3d >= MAXWELL_A) {
         struct nvk_storage_image_descriptor desc =
            get_storage_image_view_desc(pdev, pDescriptorInfo->data.pStorageImage);
         assert(sizeof(desc) <= dataSize);
         memcpy(pDescriptor, &desc, sizeof(desc));
      } else {
         struct nvk_kepler_storage_image_descriptor desc =
            get_kepler_storage_image_view_desc(pdev, pDescriptorInfo->data.pStorageImage);
         assert(sizeof(desc) <= dataSize);
         memcpy(pDescriptor, &desc, sizeof(desc));
      }
      break;

   case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: {
      struct nvk_edb_buffer_view_descriptor desc =
         get_edb_buffer_view_desc(dev, pDescriptorInfo->data.pUniformTexelBuffer);
      assert(sizeof(desc) <= dataSize);
      memcpy(pDescriptor, &desc, sizeof(desc));
      break;
   }

   case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: {
      struct nvk_edb_buffer_view_descriptor desc =
         get_edb_buffer_view_desc(dev, pDescriptorInfo->data.pStorageTexelBuffer);
      assert(sizeof(desc) <= dataSize);
      memcpy(pDescriptor, &desc, sizeof(desc));
      break;
   }

   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: {
      struct nvk_addr_range addr_range = { };
      if (pDescriptorInfo->data.pUniformBuffer != NULL &&
          pDescriptorInfo->data.pUniformBuffer->address != 0) {
         addr_range = (const struct nvk_addr_range) {
            .addr = pDescriptorInfo->data.pUniformBuffer->address,
            .range = pDescriptorInfo->data.pUniformBuffer->range,
         };
      }
      union nvk_buffer_descriptor desc = ubo_desc(pdev, addr_range);
      assert(sizeof(desc) <= dataSize);
      memcpy(pDescriptor, &desc, sizeof(desc));
      break;
   }

   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: {
      struct nvk_addr_range addr_range = { };
      if (pDescriptorInfo->data.pStorageBuffer != NULL &&
          pDescriptorInfo->data.pStorageBuffer->address != 0) {
         addr_range = (const struct nvk_addr_range) {
            .addr = pDescriptorInfo->data.pStorageBuffer->address,
            .range = pDescriptorInfo->data.pStorageBuffer->range,
         };
      }
      union nvk_buffer_descriptor desc = ssbo_desc(addr_range);
      assert(sizeof(desc) <= dataSize);
      memcpy(pDescriptor, &desc, sizeof(desc));
      break;
   }

   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: {
      uint8_t plane_count;
      struct nvk_sampled_image_descriptor desc = {};
      get_sampled_image_view_desc(pDescriptorInfo->type,
                                  pDescriptorInfo->data.pInputAttachmentImage,
                                  &desc, &plane_count);
      assert(plane_count == 1);
      assert(sizeof(desc) <= dataSize);
      memcpy(pDescriptor, &desc, sizeof(desc));
      break;
   }

   default:
      UNREACHABLE("Unknown descriptor type");
   }
}
