/*
 * Copyright © 2024 Collabora Ltd.
 *
 * Derived from tu_cmd_buffer.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "genxml/gen_macros.h"

#include "panvk_buffer.h"
#include "panvk_cmd_alloc.h"
#include "panvk_cmd_buffer.h"
#include "panvk_cmd_desc_state.h"
#include "panvk_entrypoints.h"

#include "pan_pool.h"

#include "util/rounding.h"

#include "vk_alloc.h"
#include "vk_command_buffer.h"
#include "vk_command_pool.h"

static void
cmd_desc_state_bind_sets(struct panvk_descriptor_state *desc_state,
                         const VkBindDescriptorSetsInfoKHR *info)
{
   unsigned dynoffset_idx = 0;
   for (unsigned i = 0; i < info->descriptorSetCount; ++i) {
      unsigned set_idx = i + info->firstSet;
      VK_FROM_HANDLE(panvk_descriptor_set, set, info->pDescriptorSets[i]);

      /* Invalidate the push set. */
      if (desc_state->sets[set_idx] &&
          desc_state->sets[set_idx] == desc_state->push_sets[set_idx])
         desc_state->push_sets[set_idx]->descs.dev = 0;

      desc_state->sets[set_idx] = set;

      if (set) {
         for (unsigned b = 0; b < set->layout->dyn_buf_count; b++) {
            desc_state->dyn_buf_offsets[set_idx][b] =
               info->pDynamicOffsets[dynoffset_idx++];
         }
         desc_state->dyn_ssbos[set_idx] = set->layout->dyn_ssbos;
      }
   }

   assert(dynoffset_idx == info->dynamicOffsetCount);
}

static struct panvk_descriptor_set *
cmd_get_push_desc_set(struct vk_command_buffer *vk_cmdbuf,
                      struct panvk_descriptor_state *desc_state,
                      uint32_t set_idx)
{
   struct panvk_cmd_buffer *cmdbuf =
      container_of(vk_cmdbuf, struct panvk_cmd_buffer, vk);
   struct panvk_cmd_pool *pool =
      container_of(cmdbuf->vk.pool, struct panvk_cmd_pool, vk);
   struct panvk_push_set *push_set;

   assert(set_idx < MAX_SETS);

   if (likely(desc_state->push_sets[set_idx])) {
      push_set = container_of(desc_state->push_sets[set_idx],
                              struct panvk_push_set, set);
   } else if (!list_is_empty(&pool->push_sets)) {
      push_set =
         list_first_entry(&pool->push_sets, struct panvk_push_set, base.node);
      list_del(&push_set->base.node);
      list_addtail(&push_set->base.node, &cmdbuf->push_sets);
      memset(push_set->descs, 0, sizeof(push_set->descs));
   } else {
      push_set = vk_zalloc(&pool->vk.alloc, sizeof(*push_set), 8,
                           VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (unlikely(!push_set)) {
         vk_command_buffer_set_error(&cmdbuf->vk, VK_ERROR_OUT_OF_HOST_MEMORY);
         return NULL;
      }
      list_addtail(&push_set->base.node, &cmdbuf->push_sets);
   }

   if (desc_state->push_sets[set_idx] == NULL) {
      desc_state->push_sets[set_idx] = &push_set->set;
      push_set->set.descs.host = push_set->descs;
   }

   struct panvk_descriptor_set *set = desc_state->push_sets[set_idx];

   /* Pushing descriptors replaces whatever sets are bound */
   desc_state->sets[set_idx] = set;

   BITSET_SET(desc_state->dirty_push_sets, set_idx);
   return set;
}

#if PAN_ARCH < 9
VkResult
panvk_per_arch(cmd_prepare_dyn_ssbos)(
   struct panvk_cmd_buffer *cmdbuf,
   const struct panvk_descriptor_state *desc_state,
   const struct panvk_shader_desc_info *desc_info,
   struct panvk_shader_desc_state *shader_desc_state)
{
   shader_desc_state->dyn_ssbos = 0;

   if (!desc_info->dyn_ssbos.count)
      return VK_SUCCESS;

   struct pan_ptr ptr = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, desc_info->dyn_ssbos.count * PANVK_DESCRIPTOR_SIZE,
      PANVK_DESCRIPTOR_SIZE);
   if (!ptr.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   struct panvk_ssbo_addr *ssbos = ptr.cpu;
   for (uint32_t i = 0; i < desc_info->dyn_ssbos.count; i++) {
      uint32_t src_handle = desc_info->dyn_ssbos.map[i];
      uint32_t set_idx = COPY_DESC_HANDLE_EXTRACT_TABLE(src_handle);
      uint32_t dyn_buf_idx = COPY_DESC_HANDLE_EXTRACT_INDEX(src_handle);
      const struct panvk_descriptor_set *set = desc_state->sets[set_idx];
      const uint32_t dyn_buf_offset =
         desc_state->dyn_buf_offsets[set_idx][dyn_buf_idx];

      assert(set_idx < MAX_SETS);
      assert(set);

      ssbos[i] = (struct panvk_ssbo_addr){
         .base_addr = set->dyn_bufs[dyn_buf_idx].dev_addr + dyn_buf_offset,
         .size = set->dyn_bufs[dyn_buf_idx].size,
      };
   }

   shader_desc_state->dyn_ssbos = ptr.gpu;
   return VK_SUCCESS;
}

static void
panvk_cmd_fill_dyn_ubos(const struct panvk_descriptor_state *desc_state,
                        const struct panvk_shader_desc_info *desc_info,
                        struct mali_uniform_buffer_packed *ubos,
                        uint32_t ubo_count)
{
   for (uint32_t i = 0; i < desc_info->dyn_ubos.count; i++) {
      uint32_t src_handle = desc_info->dyn_ubos.map[i];
      uint32_t set_idx = COPY_DESC_HANDLE_EXTRACT_TABLE(src_handle);
      uint32_t dyn_buf_idx = COPY_DESC_HANDLE_EXTRACT_INDEX(src_handle);
      uint32_t ubo_idx =
         i + desc_info->others.count[PANVK_BIFROST_DESC_TABLE_UBO];
      const struct panvk_descriptor_set *set = desc_state->sets[set_idx];
      const uint32_t dyn_buf_offset =
         desc_state->dyn_buf_offsets[set_idx][dyn_buf_idx];

      assert(set_idx < MAX_SETS);
      assert(set);
      assert(ubo_idx < ubo_count);

      pan_pack(&ubos[ubo_idx], UNIFORM_BUFFER, cfg) {
         cfg.pointer = set->dyn_bufs[dyn_buf_idx].dev_addr + dyn_buf_offset;
         cfg.entries = DIV_ROUND_UP(set->dyn_bufs[dyn_buf_idx].size, 16);
      }
   }
}

VkResult
panvk_per_arch(cmd_prepare_shader_desc_tables)(
   struct panvk_cmd_buffer *cmdbuf,
   const struct panvk_descriptor_state *desc_state,
   const struct panvk_shader_desc_info *desc_info,
   bool fill_image_table,
   struct panvk_shader_desc_state *shader_desc_state)
{
   for (uint32_t i = 0; i < ARRAY_SIZE(desc_info->others.count); i++) {
      uint32_t desc_count =
         desc_info->others.count[i] +
         (i == PANVK_BIFROST_DESC_TABLE_UBO ? desc_info->dyn_ubos.count : 0);
      uint32_t desc_size =
         i == PANVK_BIFROST_DESC_TABLE_UBO ? 8 : PANVK_DESCRIPTOR_SIZE;

      if (!desc_count)
         continue;

      struct pan_ptr ptr = panvk_cmd_alloc_dev_mem(
         cmdbuf, desc, desc_count * desc_size, PANVK_DESCRIPTOR_SIZE);
      if (!ptr.gpu)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;

      shader_desc_state->tables[i] = ptr.gpu;

      if (i == PANVK_BIFROST_DESC_TABLE_UBO)
         panvk_cmd_fill_dyn_ubos(desc_state, desc_info, ptr.cpu, desc_count);

      /* The image table being actually the attribute table, this is handled
       * separately for vertex shaders. */
      if (i == PANVK_BIFROST_DESC_TABLE_IMG && fill_image_table) {
         ptr = panvk_cmd_alloc_desc_array(cmdbuf, desc_count, ATTRIBUTE);
         if (!ptr.gpu)
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;

         shader_desc_state->img_attrib_table = ptr.gpu;
      }
   }

   uint32_t tex_count =
      desc_info->others.count[PANVK_BIFROST_DESC_TABLE_TEXTURE];
   uint32_t sampler_count =
      desc_info->others.count[PANVK_BIFROST_DESC_TABLE_SAMPLER];

   if (tex_count && !sampler_count) {
      struct pan_ptr sampler = panvk_cmd_alloc_desc(cmdbuf, SAMPLER);
      if (!sampler.gpu)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;

      /* Emit a dummy sampler if we have to. */
      pan_cast_and_pack(sampler.cpu, SAMPLER, cfg) {
         cfg.clamp_integer_array_indices = false;
      }

      shader_desc_state->tables[PANVK_BIFROST_DESC_TABLE_SAMPLER] = sampler.gpu;
   }

   return VK_SUCCESS;
}
#else
void
panvk_per_arch(cmd_fill_dyn_bufs)(
   const struct panvk_descriptor_state *desc_state,
   const struct panvk_shader_desc_info *desc_info,
   struct mali_buffer_packed *buffers)
{
   for (uint32_t i = 0; i < desc_info->dyn_bufs.count; i++) {
      uint32_t src_handle = desc_info->dyn_bufs.map[i];
      uint32_t set_idx = COPY_DESC_HANDLE_EXTRACT_TABLE(src_handle);
      uint32_t dyn_buf_idx = COPY_DESC_HANDLE_EXTRACT_INDEX(src_handle);
      const struct panvk_descriptor_set *set = desc_state->sets[set_idx];
      const uint32_t dyn_buf_offset =
         desc_state->dyn_buf_offsets[set_idx][dyn_buf_idx];
      const bool is_ssbo =
         desc_state->dyn_ssbos[set_idx] & BITFIELD_BIT(dyn_buf_idx);

      assert(set_idx < MAX_SETS);
      assert(set);

      pan_pack(&buffers[i], BUFFER, cfg) {
         cfg.size = align(set->dyn_bufs[dyn_buf_idx].size, is_ssbo ? 4 : 16);
         cfg.address = set->dyn_bufs[dyn_buf_idx].dev_addr + dyn_buf_offset;
      }
   }
}

VkResult
panvk_per_arch(cmd_prepare_shader_res_table)(
   struct panvk_cmd_buffer *cmdbuf,
   const struct panvk_descriptor_state *desc_state,
   const struct panvk_shader_desc_info *desc_info,
   struct panvk_shader_desc_state *shader_desc_state, uint32_t repeat_count)
{
   uint32_t first_unused_set = util_last_bit(desc_info->used_set_mask);
   uint32_t res_count =
      ALIGN_POT(1 + first_unused_set, MALI_RESOURCE_TABLE_SIZE_ALIGNMENT);
   struct pan_ptr ptr =
      panvk_cmd_alloc_desc_array(cmdbuf, res_count * repeat_count, RESOURCE);
   if (!ptr.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   struct mali_resource_packed *res_table = ptr.cpu;

   for (uint32_t r = 0; r < repeat_count; r++) {
      uint64_t drv_set_addr = shader_desc_state->driver_set.dev_addr +
                              (r * shader_desc_state->driver_set.size);
      /* First entry is the driver set table, where we store the vertex
       * attributes, the dummy sampler, the dynamic buffers and the vertex
       * buffers. */
      pan_pack(&res_table[0], RESOURCE, cfg) {
         cfg.address = drv_set_addr;
         cfg.size = shader_desc_state->driver_set.size;
         cfg.contains_descriptors = cfg.size > 0;
      }

      for (uint32_t i = 0; i < first_unused_set; i++) {
         const struct panvk_descriptor_set *set = desc_state->sets[i];

         pan_pack(&res_table[i + 1], RESOURCE, cfg) {
            if (desc_info->used_set_mask & BITFIELD_BIT(i)) {
               cfg.address = set->descs.dev;
               cfg.contains_descriptors = true;
               cfg.size = set->desc_count * PANVK_DESCRIPTOR_SIZE;
            } else {
               cfg.address = 0;
               cfg.contains_descriptors = false;
               cfg.size = 0;
            }
         }
      }
      for (uint32_t i = first_unused_set + 1; i < res_count; i++) {
         pan_pack(&res_table[i], RESOURCE, cfg) {
            cfg.address = 0;
            cfg.contains_descriptors = false;
            cfg.size = 0;
         }
      }

      res_table += res_count;
   }

   shader_desc_state->res_table = ptr.gpu | res_count;
   return VK_SUCCESS;
}
#endif

VkResult
panvk_per_arch(cmd_prepare_push_descs)(struct panvk_cmd_buffer *cmdbuf,
                                       struct panvk_descriptor_state *desc_state,
                                       uint32_t used_set_mask)
{
   for (unsigned i = 0; i < ARRAY_SIZE(desc_state->push_sets); i++) {
      struct panvk_descriptor_set *push_set = desc_state->push_sets[i];

      if (!(used_set_mask & BITFIELD_BIT(i)) || !push_set ||
          desc_state->sets[i] != push_set || push_set->descs.dev ||
          !BITSET_TEST(desc_state->dirty_push_sets, i))
         continue;

      struct pan_ptr ptr = panvk_cmd_alloc_dev_mem(
         cmdbuf, desc, push_set->desc_count * PANVK_DESCRIPTOR_SIZE,
         PANVK_DESCRIPTOR_SIZE);
      if (!ptr.gpu)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;

      memcpy(ptr.cpu, push_set->descs.host,
             push_set->desc_count * PANVK_DESCRIPTOR_SIZE);
      push_set->descs.dev = ptr.gpu;

      BITSET_CLEAR(desc_state->dirty_push_sets, i);
   }

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBindDescriptorSets2KHR)(
   VkCommandBuffer commandBuffer,
   const VkBindDescriptorSetsInfoKHR *pBindDescriptorSetsInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   /* TODO: Invalidate only if the shader tables are disturbed */
   if (pBindDescriptorSetsInfo->stageFlags & VK_SHADER_STAGE_ALL_GRAPHICS) {
      cmd_desc_state_bind_sets(&cmdbuf->state.gfx.desc_state,
                               pBindDescriptorSetsInfo);

      gfx_state_set_dirty(cmdbuf, DESC_STATE);
   }

   if (pBindDescriptorSetsInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
      cmd_desc_state_bind_sets(&cmdbuf->state.compute.desc_state,
                               pBindDescriptorSetsInfo);

      compute_state_set_dirty(cmdbuf, DESC_STATE);
   }
}

static void
push_desc_set_write(struct panvk_cmd_buffer *cmd,
                    struct panvk_descriptor_state *desc,
                    const VkPushDescriptorSetInfoKHR *info)
{
   VK_FROM_HANDLE(vk_pipeline_layout, playout, info->layout);

   const struct panvk_descriptor_set_layout *set_layout =
      to_panvk_descriptor_set_layout(playout->set_layouts[info->set]);

   struct panvk_descriptor_set *push_set =
      cmd_get_push_desc_set(&cmd->vk, desc, info->set);
   if (!push_set)
      return;

   push_set->layout = set_layout;
   push_set->desc_count = set_layout->desc_count;

   for (uint32_t i = 0; i < info->descriptorWriteCount; i++)
      panvk_per_arch(descriptor_set_write)(push_set,
                                           &info->pDescriptorWrites[i], true);

   push_set->descs.dev = 0;
   push_set->layout = NULL;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdPushDescriptorSet2KHR)(
   VkCommandBuffer commandBuffer,
   const VkPushDescriptorSetInfoKHR *pPushDescriptorSetInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   if (pPushDescriptorSetInfo->stageFlags & VK_SHADER_STAGE_ALL_GRAPHICS) {
      push_desc_set_write(cmdbuf, &cmdbuf->state.gfx.desc_state,
                          pPushDescriptorSetInfo);

      gfx_state_set_dirty(cmdbuf, DESC_STATE);
   }

   if (pPushDescriptorSetInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
      push_desc_set_write(cmdbuf, &cmdbuf->state.compute.desc_state,
                          pPushDescriptorSetInfo);

      compute_state_set_dirty(cmdbuf, DESC_STATE);
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdPushDescriptorSetWithTemplate2KHR)(
   VkCommandBuffer commandBuffer, const VkPushDescriptorSetWithTemplateInfoKHR
                                     *pPushDescriptorSetWithTemplateInfo)
{
   VK_FROM_HANDLE(vk_descriptor_update_template, template,
                  pPushDescriptorSetWithTemplateInfo->descriptorUpdateTemplate);
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(vk_pipeline_layout, playout,
                  pPushDescriptorSetWithTemplateInfo->layout);
   const uint32_t set = pPushDescriptorSetWithTemplateInfo->set;
   const struct panvk_descriptor_set_layout *set_layout =
      to_panvk_descriptor_set_layout(playout->set_layouts[set]);
   struct panvk_descriptor_state *desc_state =
      panvk_cmd_get_desc_state(cmdbuf, template->bind_point);
   struct panvk_descriptor_set *push_set =
      cmd_get_push_desc_set(&cmdbuf->vk, desc_state, set);
   if (!push_set)
      return;

   push_set->layout = set_layout;
   push_set->desc_count = set_layout->desc_count;

   panvk_per_arch(descriptor_set_write_template)(
      push_set, template, pPushDescriptorSetWithTemplateInfo->pData, true);

   push_set->descs.dev = 0;
   push_set->layout = NULL;

   if (template->bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS)
      gfx_state_set_dirty(cmdbuf, DESC_STATE);
   else
      compute_state_set_dirty(cmdbuf, DESC_STATE);
}
