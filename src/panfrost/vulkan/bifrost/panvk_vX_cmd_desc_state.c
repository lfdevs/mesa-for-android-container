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
#include "panvk_cmd_desc_state.h"
#include "panvk_entrypoints.h"

#include "pan_pool.h"

#include "util/rounding.h"

#include "vk_alloc.h"
#include "vk_command_buffer.h"
#include "vk_command_pool.h"

void
panvk_per_arch(cmd_desc_state_reset)(
   struct panvk_descriptor_state *gfx_desc_state,
   struct panvk_descriptor_state *compute_desc_state)
{
   memset(&gfx_desc_state->sets, 0, sizeof(gfx_desc_state->sets));
   memset(&compute_desc_state->sets, 0, sizeof(compute_desc_state->sets));
}

void
panvk_per_arch(cmd_desc_state_cleanup)(
   struct vk_command_buffer *cmdbuf,
   struct panvk_descriptor_state *gfx_desc_state,
   struct panvk_descriptor_state *compute_desc_state)
{
   for (unsigned i = 0; i < MAX_SETS; i++) {
      if (gfx_desc_state->push_sets[i])
         vk_free(&cmdbuf->pool->alloc, gfx_desc_state->push_sets[i]);
      if (compute_desc_state->push_sets[i])
         vk_free(&cmdbuf->pool->alloc, compute_desc_state->push_sets[i]);
   }
}

void
panvk_per_arch(cmd_desc_state_bind_sets)(
   struct panvk_descriptor_state *desc_state, VkPipelineLayout layout,
   uint32_t first_set, uint32_t desc_set_count,
   const VkDescriptorSet *desc_sets, uint32_t dyn_offset_count,
   const uint32_t *dyn_offsets)
{
   unsigned dynoffset_idx = 0;
   for (unsigned i = 0; i < desc_set_count; ++i) {
      unsigned set_idx = i + first_set;
      VK_FROM_HANDLE(panvk_descriptor_set, set, desc_sets[i]);

      /* Invalidate the push set. */
      if (desc_state->sets[set_idx] &&
          desc_state->sets[set_idx] == desc_state->push_sets[set_idx])
         desc_state->push_sets[set_idx]->descs.dev = 0;

      desc_state->sets[set_idx] = set;

      if (!set || !set->layout->dyn_buf_count)
         continue;

      for (unsigned b = 0; b < set->layout->binding_count; b++) {
         VkDescriptorType type = set->layout->bindings[b].type;

         if (type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC &&
             type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
            continue;

         unsigned dyn_buf_idx = set->layout->bindings[b].desc_idx;
         for (unsigned e = 0; e < set->layout->bindings[b].desc_count; e++) {
            desc_state->dyn_buf_offsets[set_idx][dyn_buf_idx++] =
               dyn_offsets[dynoffset_idx++];
         }
      }
   }

   assert(dynoffset_idx == dyn_offset_count);
}

struct panvk_descriptor_set *
panvk_per_arch(cmd_push_descriptors)(struct vk_command_buffer *cmdbuf,
                                     struct panvk_descriptor_state *desc_state,
                                     uint32_t set_idx)
{
   assert(set_idx < MAX_SETS);

   if (unlikely(desc_state->push_sets[set_idx] == NULL)) {
      VK_MULTIALLOC(ma);
      VK_MULTIALLOC_DECL(&ma, struct panvk_descriptor_set, set, 1);
      VK_MULTIALLOC_DECL(&ma, struct panvk_opaque_desc, descs, MAX_PUSH_DESCS);

      if (unlikely(!vk_multialloc_zalloc(&ma, &cmdbuf->pool->alloc,
                                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT))) {
         vk_command_buffer_set_error(cmdbuf, VK_ERROR_OUT_OF_HOST_MEMORY);
         return NULL;
      }

      desc_state->push_sets[set_idx] = set;
      set->descs.host = descs;
   }

   struct panvk_descriptor_set *set = desc_state->push_sets[set_idx];

   /* Pushing descriptors replaces whatever sets are bound */
   desc_state->sets[set_idx] = set;
   return set;
}

void
panvk_per_arch(cmd_prepare_dyn_ssbos)(
   struct pan_pool *desc_pool, const struct panvk_descriptor_state *desc_state,
   const struct panvk_shader *shader,
   struct panvk_shader_desc_state *shader_desc_state)
{
   if (!shader || !shader->desc_info.dyn_ssbos.count ||
       shader_desc_state->dyn_ssbos)
      return;

   struct panfrost_ptr ptr = pan_pool_alloc_aligned(
      desc_pool, shader->desc_info.dyn_ssbos.count * PANVK_DESCRIPTOR_SIZE,
      PANVK_DESCRIPTOR_SIZE);

   struct panvk_ssbo_addr *ssbos = ptr.cpu;
   for (uint32_t i = 0; i < shader->desc_info.dyn_ssbos.count; i++) {
      uint32_t src_handle = shader->desc_info.dyn_ssbos.map[i];
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
}

static void
panvk_cmd_fill_dyn_ubos(const struct panvk_descriptor_state *desc_state,
                        const struct panvk_shader *shader,
                        struct mali_uniform_buffer_packed *ubos,
                        uint32_t ubo_count)
{
   for (uint32_t i = 0; i < shader->desc_info.dyn_ubos.count; i++) {
      uint32_t src_handle = shader->desc_info.dyn_ubos.map[i];
      uint32_t set_idx = COPY_DESC_HANDLE_EXTRACT_TABLE(src_handle);
      uint32_t dyn_buf_idx = COPY_DESC_HANDLE_EXTRACT_INDEX(src_handle);
      uint32_t ubo_idx =
         i + shader->desc_info.others.count[PANVK_BIFROST_DESC_TABLE_UBO];
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

void
panvk_per_arch(cmd_prepare_shader_desc_tables)(
   struct pan_pool *desc_pool, const struct panvk_descriptor_state *desc_state,
   const struct panvk_shader *shader,
   struct panvk_shader_desc_state *shader_desc_state)
{
   if (!shader)
      return;

   for (uint32_t i = 0; i < ARRAY_SIZE(shader->desc_info.others.count); i++) {
      uint32_t desc_count =
         shader->desc_info.others.count[i] +
         (i == PANVK_BIFROST_DESC_TABLE_UBO ? shader->desc_info.dyn_ubos.count
                                            : 0);
      uint32_t desc_size =
         i == PANVK_BIFROST_DESC_TABLE_UBO ? 8 : PANVK_DESCRIPTOR_SIZE;

      if (!desc_count || shader_desc_state->tables[i])
         continue;

      struct panfrost_ptr ptr = pan_pool_alloc_aligned(
         desc_pool, desc_count * desc_size, PANVK_DESCRIPTOR_SIZE);

      shader_desc_state->tables[i] = ptr.gpu;

      if (i == PANVK_BIFROST_DESC_TABLE_UBO)
         panvk_cmd_fill_dyn_ubos(desc_state, shader, ptr.cpu, desc_count);

      /* The image table being actually the attribute table, this is handled
       * separately for vertex shaders. */
      if (i == PANVK_BIFROST_DESC_TABLE_IMG &&
          shader->info.stage != MESA_SHADER_VERTEX) {
         assert(!shader_desc_state->img_attrib_table);

         ptr = pan_pool_alloc_desc_array(desc_pool, desc_count, ATTRIBUTE);
         shader_desc_state->img_attrib_table = ptr.gpu;
      }
   }

   uint32_t tex_count =
      shader->desc_info.others.count[PANVK_BIFROST_DESC_TABLE_TEXTURE];
   uint32_t sampler_count =
      shader->desc_info.others.count[PANVK_BIFROST_DESC_TABLE_SAMPLER];

   if (tex_count && !sampler_count) {
      struct panfrost_ptr sampler = pan_pool_alloc_desc(desc_pool, SAMPLER);

      /* Emit a dummy sampler if we have to. */
      pan_pack(sampler.cpu, SAMPLER, _) {
      }

      shader_desc_state->tables[PANVK_BIFROST_DESC_TABLE_SAMPLER] = sampler.gpu;
   }
}

void
panvk_per_arch(cmd_prepare_push_descs)(struct pan_pool *desc_pool,
                                       struct panvk_descriptor_state *desc_state,
                                       uint32_t used_set_mask)
{
   for (unsigned i = 0; i < ARRAY_SIZE(desc_state->push_sets); i++) {
      struct panvk_descriptor_set *push_set = desc_state->push_sets[i];

      if (!(used_set_mask & BITFIELD_BIT(i)) || !push_set ||
          desc_state->sets[i] != push_set || push_set->descs.dev)
         continue;

      push_set->descs.dev = pan_pool_upload_aligned(
         desc_pool, push_set->descs.host,
         push_set->desc_count * PANVK_DESCRIPTOR_SIZE, PANVK_DESCRIPTOR_SIZE);
   }
}
