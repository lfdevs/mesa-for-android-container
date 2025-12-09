/*
 * Copyright © 2024 Collabora Ltd.
 * Copyright © 2025 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "util/mesa-sha1.h"
#include "vk_alloc.h"
#include "vk_descriptor_update_template.h"
#include "vk_descriptors.h"
#include "vk_format.h"
#include "vk_log.h"
#include "vk_util.h"

#include "util/bitset.h"

#include "genxml/gen_macros.h"

#include "panvk_buffer.h"
#include "panvk_buffer_view.h"
#include "panvk_descriptor_set.h"
#include "panvk_descriptor_set_layout.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_image.h"
#include "panvk_image_view.h"
#include "panvk_macros.h"
#include "panvk_priv_bo.h"
#include "panvk_sampler.h"

static void *
get_desc_slot_ptr(struct panvk_descriptor_set *set, uint32_t binding,
                  uint32_t elem, struct panvk_subdesc_info subdesc)
{
   const struct panvk_descriptor_set_binding_layout *binding_layout =
      &set->layout->bindings[binding];

   uint32_t offset = panvk_get_desc_index(binding_layout, elem, subdesc);

   assert(offset < set->layout->desc_count);

   return (char *)set->descs.host + offset * PANVK_DESCRIPTOR_SIZE;
}

#define write_desc(set, binding, elem, desc, subdesc)                          \
   do {                                                                        \
      static_assert(sizeof(*(desc)) == PANVK_DESCRIPTOR_SIZE,                  \
                    "wrong descriptor size");                                  \
      void *__dst = get_desc_slot_ptr(set, binding, elem, subdesc);            \
      memcpy(__dst, (desc), PANVK_DESCRIPTOR_SIZE);                            \
   } while (0)

#if PAN_ARCH >= 9
#define write_nulldesc(set, binding, elem, subdesc)                            \
   do {                                                                        \
      struct mali_null_descriptor_packed null_desc;                            \
      pan_pack(&null_desc, NULL_DESCRIPTOR, cfg)                               \
         ;                                                                     \
      write_desc(set, binding, elem, &null_desc, (subdesc));                   \
   } while (0)
#else
#define write_nulldesc(set, binding, elem, subdesc)                            \
   do {                                                                        \
   } while (0)
#endif

static void
write_sampler_desc(struct panvk_descriptor_set *set,
                   const VkDescriptorImageInfo *const pImageInfo,
                   uint32_t binding, uint32_t elem, bool write_immutable)
{
   const struct panvk_descriptor_set_binding_layout *binding_layout =
      &set->layout->bindings[binding];

   struct panvk_sampler *sampler;

   if (binding_layout->immutable_samplers) {
      if (!write_immutable)
         return;
      sampler = binding_layout->immutable_samplers[elem];
   } else {
      if (!pImageInfo)
         return;
      sampler = panvk_sampler_from_handle(pImageInfo->sampler);
   }

   if (!sampler) {
      for (uint8_t plane = 0; plane < binding_layout->samplers_per_desc;
           plane++)
         write_nulldesc(set, binding, elem,
                        get_sampler_subdesc_info(binding_layout->type, plane));
      return;
   }

   for (uint8_t plane = 0; plane < sampler->desc_count; plane++) {
      write_desc(set, binding, elem, &sampler->descs[plane],
                 get_sampler_subdesc_info(binding_layout->type, plane));
   }
}

static void
write_image_view_desc(struct panvk_descriptor_set *set,
                      const VkDescriptorImageInfo *const pImageInfo,
                      uint32_t binding, uint32_t elem, VkDescriptorType type)
{
   if (!pImageInfo)
      return;

   const struct panvk_descriptor_set_binding_layout *binding_layout =
      &set->layout->bindings[binding];

   if (pImageInfo->imageView == VK_NULL_HANDLE) {
      for (uint8_t plane = 0; plane < binding_layout->textures_per_desc;
           plane++)
         write_nulldesc(set, binding, elem,
                        get_sampler_subdesc_info(binding_layout->type, plane));
      return;
   }

   VK_FROM_HANDLE(panvk_image_view, view, pImageInfo->imageView);

   uint8_t plane_count = vk_format_get_plane_count(view->vk.format);
   for (uint8_t plane = 0; plane < plane_count; plane++) {
      struct panvk_subdesc_info subdesc = get_tex_subdesc_info(type, plane);
#if PAN_ARCH >= 9
      if (type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
         write_desc(set, binding, elem, &view->descs.storage_tex[plane],
                    subdesc);
      else
         write_desc(set, binding, elem, &view->descs.tex[plane], subdesc);
#else
      if (type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
         write_desc(set, binding, elem, &view->descs.img_attrib_buf,
                    NO_SUBDESC);
      else
         write_desc(set, binding, elem, &view->descs.tex[plane], subdesc);
#endif
   }
}

static void
write_buffer_desc(struct panvk_descriptor_set *set,
                  const VkDescriptorBufferInfo *const info, uint32_t binding,
                  uint32_t elem, VkDescriptorType type)
{
   if (info->buffer == VK_NULL_HANDLE) {
      write_nulldesc(set, binding, elem, NO_SUBDESC);
      return;
   }

   VK_FROM_HANDLE(panvk_buffer, buffer, info->buffer);
   const uint64_t range = panvk_buffer_range(buffer, info->offset, info->range);
   assert(range <= UINT32_MAX);

#if PAN_ARCH < 9
   if (type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
      struct panvk_ssbo_addr desc = {
         .base_addr = panvk_buffer_gpu_ptr(buffer, info->offset),
         .size = range,
      };

      write_desc(set, binding, elem, &desc, NO_SUBDESC);
   } else {
      struct {
         struct mali_uniform_buffer_packed ubo;
         uint32_t pad[6];
      } padded_desc = {0};

      pan_pack(&padded_desc.ubo, UNIFORM_BUFFER, cfg) {
         cfg.pointer = panvk_buffer_gpu_ptr(buffer, info->offset);
         cfg.entries = DIV_ROUND_UP(range, 16);
      }

      write_desc(set, binding, elem, &padded_desc, NO_SUBDESC);
   }
#else
   struct mali_buffer_packed desc;

   pan_pack(&desc, BUFFER, cfg) {
      cfg.address = panvk_buffer_gpu_ptr(buffer, info->offset);
      cfg.size = range;
   }
   write_desc(set, binding, elem, &desc, NO_SUBDESC);
#endif
}

static void
write_dynamic_buffer_desc(struct panvk_descriptor_set *set,
                          const VkDescriptorBufferInfo *const info,
                          uint32_t binding, uint32_t elem)
{
   /* Default to memory sink (OOB address) */
   uint64_t dev_addr = 0x8ull << 60;
   uint64_t range = 0;

   if (info->buffer != VK_NULL_HANDLE) {
      VK_FROM_HANDLE(panvk_buffer, buffer, info->buffer);

      dev_addr = panvk_buffer_gpu_ptr(buffer, info->offset);
      range = panvk_buffer_range(buffer, info->offset, info->range);
   }

   const struct panvk_descriptor_set_binding_layout *binding_layout =
      &set->layout->bindings[binding];
   uint32_t dyn_buf_idx = binding_layout->desc_idx + elem;

   assert(range <= UINT32_MAX);
   assert(dyn_buf_idx < ARRAY_SIZE(set->dyn_bufs));

   set->dyn_bufs[dyn_buf_idx].dev_addr = dev_addr;
   set->dyn_bufs[dyn_buf_idx].size = range;
}

static void
write_buffer_view_desc(struct panvk_descriptor_set *set,
                       const VkBufferView bufferView, uint32_t binding,
                       uint32_t elem, VkDescriptorType type)
{
   if (bufferView == VK_NULL_HANDLE) {
      write_nulldesc(set, binding, elem, NO_SUBDESC);
      return;
   }

   VK_FROM_HANDLE(panvk_buffer_view, view, bufferView);

#if PAN_ARCH < 9
   if (type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)
      write_desc(set, binding, elem, &view->descs.img_attrib_buf, NO_SUBDESC);
   else
      write_desc(set, binding, elem, &view->descs.tex, NO_SUBDESC);
#else
   write_desc(set, binding, elem, &view->descs.buf, NO_SUBDESC);
#endif
}

static void
write_iub(struct panvk_descriptor_set *set, uint32_t binding,
          uint32_t dst_offset, uint32_t count, const void *data)
{
   const struct panvk_descriptor_set_binding_layout *binding_layout =
      &set->layout->bindings[binding];

   /* First slot is the actual buffer descriptor. */
   uint32_t iub_data_offset =
      panvk_get_desc_index(binding_layout, 1, NO_SUBDESC) *
      PANVK_DESCRIPTOR_SIZE;

   void *iub_data_host = set->descs.host + iub_data_offset;
   memcpy(iub_data_host + dst_offset, data, count);
}

static void
panvk_desc_pool_free_set(struct panvk_descriptor_pool *pool,
                         struct panvk_descriptor_set *set)
{
   uintptr_t set_idx = set - pool->sets;
   assert(set_idx < pool->max_sets);

   if (!BITSET_TEST(pool->free_sets, set_idx)) {
      if (set->desc_count)
         util_vma_heap_free(
            &pool->desc_heap,
            pool->host_only_mem ? (uintptr_t)set->descs.host : set->descs.dev,
            set->desc_count * PANVK_DESCRIPTOR_SIZE);

      BITSET_SET(pool->free_sets, set_idx);

      /* Discard constness to call vk_descriptor_set_layout_unref(). */
      struct panvk_descriptor_set_layout *set_layout =
         (struct panvk_descriptor_set_layout *)set->layout;

      vk_descriptor_set_layout_unref(pool->base.device, &set_layout->vk);
      vk_object_base_finish(&set->base);
      memset(set, 0, sizeof(*set));
   }
}

static void
panvk_destroy_descriptor_pool(struct panvk_device *device,
                              const VkAllocationCallbacks *pAllocator,
                              struct panvk_descriptor_pool *pool)
{
   for (uint32_t i = 0; i < pool->max_sets; i++)
      panvk_desc_pool_free_set(pool, &pool->sets[i]);

   if (pool->desc_bo) {
      util_vma_heap_finish(&pool->desc_heap);
      panvk_priv_bo_unref(pool->desc_bo);
   } else if (pool->host_only_mem) {
      vk_free2(&device->vk.alloc, pAllocator, (void *)pool->host_only_mem);
      pool->host_only_mem = 0;
   }

   vk_object_free(&device->vk, pAllocator, pool);
}

static VkResult
panvk_init_pool_memory(struct panvk_device *device,
                       struct panvk_descriptor_pool *pool,
                       const VkDescriptorPoolCreateInfo *pCreateInfo,
                       uint64_t pool_size,
                       const VkAllocationCallbacks *pAllocator)
{
   if (!(pCreateInfo->flags & VK_DESCRIPTOR_POOL_CREATE_HOST_ONLY_BIT_EXT)) {
      VkResult result = panvk_priv_bo_create(device, pool_size, 0,
                                             VK_SYSTEM_ALLOCATION_SCOPE_OBJECT,
                                             &pool->desc_bo);
      if (result != VK_SUCCESS)
         return result;

      uint64_t bo_size = pool->desc_bo->bo->size;
      assert(pool_size <= bo_size);

      util_vma_heap_init(&pool->desc_heap, pool->desc_bo->addr.dev, bo_size);
   } else {
      void *pool_mem = vk_alloc2(&device->vk.alloc, pAllocator, pool_size, 8,
                                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!pool_mem)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      /* A host-only pool has no bo backing it. */
      pool->desc_bo = NULL;
      pool->host_only_mem = (uintptr_t)pool_mem;
      util_vma_heap_init(&pool->desc_heap, pool->host_only_mem, pool_size);
   }

   return VK_SUCCESS;
}

VkResult
panvk_per_arch(CreateDescriptorPool)(
   VkDevice _device, const VkDescriptorPoolCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator, VkDescriptorPool *pDescriptorPool)
{
   VK_FROM_HANDLE(panvk_device, device, _device);

   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct panvk_descriptor_pool, pool, 1);
   VK_MULTIALLOC_DECL(&ma, BITSET_WORD, free_sets,
                      BITSET_WORDS(pCreateInfo->maxSets));
   VK_MULTIALLOC_DECL(&ma, struct panvk_descriptor_set, sets,
                      pCreateInfo->maxSets);

   if (!vk_object_multizalloc(&device->vk, &ma, pAllocator,
                              VK_OBJECT_TYPE_DESCRIPTOR_POOL))
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   uint32_t desc_count = 0;
   for (unsigned i = 0; i < pCreateInfo->poolSizeCount; ++i) {
      if (!vk_descriptor_type_is_dynamic(pCreateInfo->pPoolSizes[i].type)) {
         const struct panvk_descriptor_set_binding_layout layout = {
            .type = pCreateInfo->pPoolSizes[i].type,
            .textures_per_desc = PANVK_MAX_PLANES,
            .samplers_per_desc = PANVK_MAX_DESCS_PER_SAMPLER,
         };
         desc_count += panvk_get_desc_stride(&layout) *
                       pCreateInfo->pPoolSizes[i].descriptorCount;
      }
   }

   /* initialize to all ones to indicate all sets are free */
   BITSET_SET_RANGE(free_sets, 0, pCreateInfo->maxSets - 1);
   pool->free_sets = free_sets;
   pool->sets = sets;
   pool->max_sets = pCreateInfo->maxSets;

   if (desc_count) {
      /* adjust desc_count to account for 1 dummy sampler per descriptor set */
      desc_count += pool->max_sets;

      uint64_t pool_size = desc_count * PANVK_DESCRIPTOR_SIZE;
      VkResult result = panvk_init_pool_memory(device, pool, pCreateInfo,
                                               pool_size, pAllocator);
      if (result != VK_SUCCESS) {
         panvk_destroy_descriptor_pool(device, pAllocator, pool);
         return result;
      }
   }

   *pDescriptorPool = panvk_descriptor_pool_to_handle(pool);
   return VK_SUCCESS;
}

void
panvk_per_arch(DestroyDescriptorPool)(VkDevice _device, VkDescriptorPool _pool,
                                      const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_descriptor_pool, pool, _pool);

   if (pool)
      panvk_destroy_descriptor_pool(device, pAllocator, pool);
}

static void
desc_set_write_immutable_samplers(struct panvk_descriptor_set *set,
                                  uint32_t variable_count)
{
   const struct panvk_descriptor_set_layout *layout = set->layout;

   for (uint32_t b = 0; b < layout->binding_count; b++) {
      if (layout->bindings[b].type != VK_DESCRIPTOR_TYPE_SAMPLER &&
          layout->bindings[b].type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
         continue;

      if (layout->bindings[b].immutable_samplers == NULL)
         continue;

      uint32_t array_size = layout->bindings[b].desc_count;

      if (layout->bindings[b].flags &
          VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT)
         array_size = variable_count;

      for (uint32_t j = 0; j < array_size; j++) {
         struct panvk_sampler *sampler =
            layout->bindings[b].immutable_samplers[j];
         if (!sampler) {
            for (uint8_t plane = 0;
                 plane < layout->bindings[b].samplers_per_desc; plane++)
               write_nulldesc(
                  set, b, j,
                  get_sampler_subdesc_info(layout->bindings[b].type, plane));
            continue;
         }
         for (uint8_t plane = 0; plane < sampler->desc_count; plane++) {
            write_desc(set, b, j,
                       &sampler->descs[plane],
                       get_sampler_subdesc_info(layout->bindings[b].type,
                                                plane));
         }
      }
   }
}

static void
panvk_init_iub(struct panvk_descriptor_set *set, uint32_t binding,
               uint32_t variable_count)
{
   const struct panvk_descriptor_set_binding_layout *binding_layout =
      &set->layout->bindings[binding];

   bool has_variable_count =
      binding_layout->flags &
      VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT;

   /* The first element is the buffer descriptor. */
   uint32_t iub_data_offset =
      panvk_get_desc_index(binding_layout, 1, NO_SUBDESC) *
      PANVK_DESCRIPTOR_SIZE;
   uint64_t iub_data_dev = set->descs.dev + iub_data_offset;
   uint32_t iub_desc_count = has_variable_count ?
      panvk_get_iub_desc_count(variable_count) : binding_layout->desc_count;
   uint32_t iub_size_dev = panvk_get_iub_size(iub_desc_count);

#if PAN_ARCH < 9
   struct {
      struct mali_uniform_buffer_packed ubo;
      uint32_t pad[6];
   } padded_desc = {0};

   pan_pack(&padded_desc.ubo, UNIFORM_BUFFER, cfg) {
      cfg.pointer = iub_data_dev;
      cfg.entries = iub_size_dev;
   }
   write_desc(set, binding, 0, &padded_desc, NO_SUBDESC);
#else
   struct mali_buffer_packed desc;

   pan_pack(&desc, BUFFER, cfg) {
      cfg.address = iub_data_dev;
      cfg.size = iub_size_dev;
   }
   write_desc(set, binding, 0, &desc, NO_SUBDESC);
#endif
}

static VkResult
panvk_desc_pool_allocate_set(struct panvk_descriptor_pool *pool,
                             struct panvk_descriptor_set_layout *layout,
                             uint32_t variable_count,
                             struct panvk_descriptor_set **out)
{
   uint32_t num_descs = layout->desc_count;

   if (layout->binding_count) {
      uint32_t last_binding = layout->binding_count - 1;

      if ((layout->bindings[last_binding].flags &
           VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT) &&
          !vk_descriptor_type_is_dynamic(layout->bindings[last_binding].type)) {
         if (layout->bindings[last_binding].type ==
             VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK) {
            num_descs -= layout->bindings[last_binding].desc_count;
            num_descs += panvk_get_iub_desc_count(variable_count);
         } else {
            uint32_t desc_stride =
               panvk_get_desc_stride(&layout->bindings[last_binding]);

            num_descs -= layout->bindings[last_binding].desc_count * desc_stride;
            num_descs += variable_count * desc_stride;
         }
      }
   }

   uint64_t descs_size = num_descs * PANVK_DESCRIPTOR_SIZE;
   uint32_t first_free_set =
      __bitset_ffs(pool->free_sets, BITSET_WORDS(pool->max_sets));
   if (first_free_set == 0 || pool->desc_heap.free_size < descs_size)
      return panvk_error(pool, VK_ERROR_OUT_OF_POOL_MEMORY);

   uint64_t descs_dev_addr = 0;
   if (num_descs) {
      descs_dev_addr = util_vma_heap_alloc(&pool->desc_heap, descs_size,
                                           PANVK_DESCRIPTOR_SIZE);
      if (!descs_dev_addr)
         return panvk_error(pool, VK_ERROR_FRAGMENTED_POOL);
   }
   struct panvk_descriptor_set *set = &pool->sets[first_free_set - 1];

   vk_object_base_init(pool->base.device, &set->base,
                       VK_OBJECT_TYPE_DESCRIPTOR_SET);
   vk_descriptor_set_layout_ref(&layout->vk);
   set->layout = layout;
   set->desc_count = num_descs;
   if (pool->desc_bo) {
      set->descs.dev = descs_dev_addr;
      set->descs.host =
         pool->desc_bo->addr.host + set->descs.dev - pool->desc_bo->addr.dev;
   } else {
      /* This cast is fine because the heap is initialized from a host
       * pointer in case of a host only pool. */
      set->descs.host = (void *)(uintptr_t)descs_dev_addr;
   }
   desc_set_write_immutable_samplers(set, variable_count);
   BITSET_CLEAR(pool->free_sets, first_free_set - 1);

   for (uint32_t b = 0; b < layout->binding_count; ++b) {
      if (layout->bindings[b].type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK)
         panvk_init_iub(set, b, variable_count);
   }

   *out = set;
   return VK_SUCCESS;
}

VkResult
panvk_per_arch(AllocateDescriptorSets)(
   VkDevice _device, const VkDescriptorSetAllocateInfo *pAllocateInfo,
   VkDescriptorSet *pDescriptorSets)
{
   VK_FROM_HANDLE(panvk_descriptor_pool, pool, pAllocateInfo->descriptorPool);
   VkResult result = VK_SUCCESS;
   unsigned i;

   struct panvk_descriptor_set *set = NULL;

   const VkDescriptorSetVariableDescriptorCountAllocateInfo *var_desc_count =
      vk_find_struct_const(
         pAllocateInfo->pNext,
         DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO);

   /* allocate a set of buffers for each shader to contain descriptors */
   for (i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
      VK_FROM_HANDLE(panvk_descriptor_set_layout, layout,
                     pAllocateInfo->pSetLayouts[i]);
      /* If descriptorSetCount is zero or this structure is not included in
       * the pNext chain, then the variable lengths are considered to be zero.
       */
      const uint32_t variable_count =
         var_desc_count && var_desc_count->descriptorSetCount > 0
            ? var_desc_count->pDescriptorCounts[i]
            : 0;

      result = panvk_desc_pool_allocate_set(pool, layout, variable_count, &set);
      if (result != VK_SUCCESS)
         goto err_free_sets;

      pDescriptorSets[i] = panvk_descriptor_set_to_handle(set);
   }

   return VK_SUCCESS;

err_free_sets:
   panvk_per_arch(FreeDescriptorSets)(_device, pAllocateInfo->descriptorPool, i,
                                      pDescriptorSets);
   for (i = 0; i < pAllocateInfo->descriptorSetCount; i++)
      pDescriptorSets[i] = VK_NULL_HANDLE;

   return result;
}

VkResult
panvk_per_arch(FreeDescriptorSets)(VkDevice _device,
                                   VkDescriptorPool descriptorPool,
                                   uint32_t descriptorSetCount,
                                   const VkDescriptorSet *pDescriptorSets)
{
   VK_FROM_HANDLE(panvk_descriptor_pool, pool, descriptorPool);

   for (unsigned i = 0; i < descriptorSetCount; i++) {
      VK_FROM_HANDLE(panvk_descriptor_set, set, pDescriptorSets[i]);

      if (set)
         panvk_desc_pool_free_set(pool, set);
   }
   return VK_SUCCESS;
}

VkResult
panvk_per_arch(ResetDescriptorPool)(VkDevice _device, VkDescriptorPool _pool,
                                    VkDescriptorPoolResetFlags flags)
{
   VK_FROM_HANDLE(panvk_descriptor_pool, pool, _pool);

   for (uint32_t i = 0; i < pool->max_sets; i++)
      panvk_desc_pool_free_set(pool, &pool->sets[i]);

   BITSET_SET_RANGE(pool->free_sets, 0, pool->max_sets - 1);
   return VK_SUCCESS;
}

VkResult
panvk_per_arch(descriptor_set_write)(struct panvk_descriptor_set *set,
                                     const VkWriteDescriptorSet *write,
                                     bool write_immutable_samplers)
{
   switch (write->descriptorType) {
   case VK_DESCRIPTOR_TYPE_SAMPLER:
      for (uint32_t j = 0; j < write->descriptorCount; j++) {
         write_sampler_desc(set, write->pImageInfo + j, write->dstBinding,
                            write->dstArrayElement + j,
                            write_immutable_samplers);
      }
      break;

   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      for (uint32_t j = 0; j < write->descriptorCount; j++) {
         write_image_view_desc(set, write->pImageInfo + j, write->dstBinding,
                               write->dstArrayElement + j,
                               write->descriptorType);
         write_sampler_desc(set, write->pImageInfo + j, write->dstBinding,
                            write->dstArrayElement + j,
                            write_immutable_samplers);
      }
      break;

   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
      for (uint32_t j = 0; j < write->descriptorCount; j++) {
         write_image_view_desc(set, write->pImageInfo + j, write->dstBinding,
                               write->dstArrayElement + j,
                               write->descriptorType);
      }
      break;

   case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
      for (uint32_t j = 0; j < write->descriptorCount; j++) {
         write_buffer_view_desc(set, write->pTexelBufferView[j],
                                write->dstBinding, write->dstArrayElement + j,
                                write->descriptorType);
      }
      break;

   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      for (uint32_t j = 0; j < write->descriptorCount; j++) {
         write_buffer_desc(set, write->pBufferInfo + j, write->dstBinding,
                           write->dstArrayElement + j, write->descriptorType);
      }
      break;

   case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK: {
      const VkWriteDescriptorSetInlineUniformBlock *inline_info =
         vk_find_struct_const(write->pNext,
                              WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK);
      write_iub(set, write->dstBinding, write->dstArrayElement,
                write->descriptorCount, inline_info->pData);
      break;
   }

   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      for (uint32_t j = 0; j < write->descriptorCount; j++) {
         write_dynamic_buffer_desc(set, write->pBufferInfo + j,
                                   write->dstBinding,
                                   write->dstArrayElement + j);
      }
      break;

   default:
      UNREACHABLE("Unsupported descriptor type");
   }
   return VK_SUCCESS;
}

static VkResult
panvk_descriptor_set_copy(const VkCopyDescriptorSet *copy)
{
   VK_FROM_HANDLE(panvk_descriptor_set, src_set, copy->srcSet);
   VK_FROM_HANDLE(panvk_descriptor_set, dst_set, copy->dstSet);

   const struct panvk_descriptor_set_binding_layout *dst_binding_layout =
      &dst_set->layout->bindings[copy->dstBinding];
   const struct panvk_descriptor_set_binding_layout *src_binding_layout =
      &src_set->layout->bindings[copy->srcBinding];

   const bool src_mutable = src_binding_layout->type == VK_DESCRIPTOR_TYPE_MUTABLE_EXT;
   const bool dst_mutable = dst_binding_layout->type == VK_DESCRIPTOR_TYPE_MUTABLE_EXT;
   assert(dst_binding_layout->type == src_binding_layout->type || src_mutable || dst_mutable);

   switch (src_binding_layout->type) {
   case VK_DESCRIPTOR_TYPE_SAMPLER:
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
   case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
   case VK_DESCRIPTOR_TYPE_MUTABLE_EXT:
      for (uint32_t i = 0; i < copy->descriptorCount; i++) {
         void *dst = get_desc_slot_ptr(dst_set, copy->dstBinding,
                                       copy->dstArrayElement + i,
                                       NO_SUBDESC);
         const void *src = get_desc_slot_ptr(src_set, copy->srcBinding,
                                             copy->srcArrayElement + i,
                                             NO_SUBDESC);

         memcpy(dst, src,
                PANVK_DESCRIPTOR_SIZE *
                   panvk_get_desc_stride(src_binding_layout));
      }
      break;

   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
      uint32_t dst_dyn_buf_idx =
         dst_binding_layout->desc_idx + copy->dstArrayElement;
      uint32_t src_dyn_buf_idx =
         src_binding_layout->desc_idx + copy->srcArrayElement;

      memcpy(
         &dst_set->dyn_bufs[dst_dyn_buf_idx],
         &src_set->dyn_bufs[src_dyn_buf_idx],
         copy->descriptorCount * sizeof(dst_set->dyn_bufs[dst_dyn_buf_idx]));
      break;
   }

   case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK: {
      const void *src =
         get_desc_slot_ptr(src_set, copy->srcBinding, 0, NO_SUBDESC);
      src += PANVK_DESCRIPTOR_SIZE + copy->srcArrayElement;
      write_iub(dst_set, copy->dstBinding, copy->dstArrayElement,
                copy->descriptorCount, src);
      break;
   }

   default:
      UNREACHABLE("Unsupported descriptor type");
   }

   return VK_SUCCESS;
}

void
panvk_per_arch(UpdateDescriptorSets)(
   VkDevice _device, uint32_t descriptorWriteCount,
   const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount,
   const VkCopyDescriptorSet *pDescriptorCopies)
{
   for (uint32_t i = 0; i < descriptorWriteCount; i++) {
      VK_FROM_HANDLE(panvk_descriptor_set, set, pDescriptorWrites[i].dstSet);

      panvk_per_arch(descriptor_set_write)(set, &pDescriptorWrites[i], false);
   }

   for (uint32_t i = 0; i < descriptorCopyCount; i++)
      panvk_descriptor_set_copy(&pDescriptorCopies[i]);
}

void
panvk_per_arch(descriptor_set_write_template)(
   struct panvk_descriptor_set *set,
   const struct vk_descriptor_update_template *template, const void *data,
   bool write_immutable_samplers)
{
   for (uint32_t i = 0; i < template->entry_count; i++) {
      const struct vk_descriptor_template_entry *entry = &template->entries[i];

      switch (entry->type) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorImageInfo *info =
               data + entry->offset + j * entry->stride;

            write_sampler_desc(set, info, entry->binding,
                               entry->array_element + j,
                               write_immutable_samplers);
         }
         break;

      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorImageInfo *info =
               data + entry->offset + j * entry->stride;
            write_image_view_desc(set, info, entry->binding,
                                  entry->array_element + j, entry->type);
            write_sampler_desc(set, info, entry->binding,
                               entry->array_element + j,
                               write_immutable_samplers);
         }
         break;

      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorImageInfo *info =
               data + entry->offset + j * entry->stride;

            write_image_view_desc(set, info, entry->binding,
                                  entry->array_element + j, entry->type);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkBufferView *bview =
               data + entry->offset + j * entry->stride;

            write_buffer_view_desc(set, *bview, entry->binding,
                                   entry->array_element + j, entry->type);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorBufferInfo *info =
               data + entry->offset + j * entry->stride;

            write_buffer_desc(set, info, entry->binding,
                              entry->array_element + j, entry->type);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         for (uint32_t j = 0; j < entry->array_count; j++) {
            const VkDescriptorBufferInfo *info =
               data + entry->offset + j * entry->stride;

            write_dynamic_buffer_desc(set, info, entry->binding,
                                      entry->array_element + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
         write_iub(set, entry->binding, entry->array_element,
                   entry->array_count, data + entry->offset);
         break;

      default:
         UNREACHABLE("Unsupported descriptor type");
      }
   }
}

void
panvk_per_arch(UpdateDescriptorSetWithTemplate)(
   VkDevice _device, VkDescriptorSet descriptorSet,
   VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void *pData)
{
   VK_FROM_HANDLE(panvk_descriptor_set, set, descriptorSet);
   VK_FROM_HANDLE(vk_descriptor_update_template, template,
                  descriptorUpdateTemplate);

   panvk_per_arch(descriptor_set_write_template)(set, template, pData, false);
}
