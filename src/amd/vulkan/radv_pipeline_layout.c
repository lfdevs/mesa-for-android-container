/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#include "radv_pipeline_layout.h"
#include "radv_descriptor_set.h"
#include "radv_device.h"
#include "radv_entrypoints.h"

#include "vk_alloc.h"
#include "vk_log.h"

void
radv_pipeline_layout_init(struct radv_device *device, struct radv_pipeline_layout *layout, bool independent_sets)
{
   memset(layout, 0, sizeof(*layout));

   vk_object_base_init(&device->vk, &layout->base, VK_OBJECT_TYPE_PIPELINE_LAYOUT);

   layout->independent_sets = independent_sets;
}

void
radv_pipeline_layout_add_set(struct radv_pipeline_layout *layout, uint32_t set_idx,
                             struct radv_descriptor_set_layout *set_layout)
{
   if (layout->set[set_idx].layout)
      return;

   layout->num_sets = MAX2(set_idx + 1, layout->num_sets);

   layout->set[set_idx].layout = set_layout;
   vk_descriptor_set_layout_ref(&set_layout->vk);

   layout->set[set_idx].dynamic_offset_start = layout->dynamic_offset_count;

   layout->dynamic_offset_count += set_layout->dynamic_offset_count;
   layout->dynamic_shader_stages |= set_layout->dynamic_shader_stages;
}

void
radv_pipeline_layout_hash(struct radv_pipeline_layout *layout)
{
   struct mesa_blake3 ctx;

   _mesa_blake3_init(&ctx);
   for (uint32_t i = 0; i < layout->num_sets; i++) {
      struct radv_descriptor_set_layout *set_layout = layout->set[i].layout;

      if (!set_layout)
         continue;

      _mesa_blake3_update(&ctx, set_layout->hash, sizeof(set_layout->hash));
   }
   _mesa_blake3_final(&ctx, layout->hash);
}

void
radv_pipeline_layout_finish(struct radv_device *device, struct radv_pipeline_layout *layout)
{
   for (uint32_t i = 0; i < layout->num_sets; i++) {
      if (!layout->set[i].layout)
         continue;

      vk_descriptor_set_layout_unref(&device->vk, &layout->set[i].layout->vk);
   }

   vk_object_base_finish(&layout->base);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreatePipelineLayout(VkDevice _device, const VkPipelineLayoutCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator, VkPipelineLayout *pPipelineLayout)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   struct radv_pipeline_layout *layout;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);

   layout = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*layout), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (layout == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   radv_pipeline_layout_init(device, layout, pCreateInfo->flags & VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT);

   layout->num_sets = pCreateInfo->setLayoutCount;

   for (uint32_t set = 0; set < pCreateInfo->setLayoutCount; set++) {
      VK_FROM_HANDLE(radv_descriptor_set_layout, set_layout, pCreateInfo->pSetLayouts[set]);

      if (set_layout == NULL) {
         layout->set[set].layout = NULL;
         continue;
      }

      radv_pipeline_layout_add_set(layout, set, set_layout);
   }

   radv_pipeline_layout_hash(layout);

   *pPipelineLayout = radv_pipeline_layout_to_handle(layout);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyPipelineLayout(VkDevice _device, VkPipelineLayout _pipelineLayout, const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, _pipelineLayout);

   if (!pipeline_layout)
      return;

   radv_pipeline_layout_finish(device, pipeline_layout);

   vk_free2(&device->vk.alloc, pAllocator, pipeline_layout);
}
