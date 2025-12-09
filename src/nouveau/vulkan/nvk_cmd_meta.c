/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_buffer.h"
#include "nvk_cmd_buffer.h"
#include "nvk_descriptor_set.h"
#include "nvk_device.h"
#include "nvk_entrypoints.h"
#include "nvk_image.h"
#include "nvk_physical_device.h"

#include "nv_push_cl9097.h"
#include "nv_push_clb197.h"

static VkResult
nvk_cmd_bind_map_buffer(struct vk_command_buffer *vk_cmd,
                        struct vk_meta_device *meta,
                        VkBuffer _buffer, void **map_out)
{
   struct nvk_cmd_buffer *cmd =
      container_of(vk_cmd, struct nvk_cmd_buffer, vk);
   VK_FROM_HANDLE(nvk_buffer, buffer, _buffer);
   VkResult result;

   uint64_t addr;
   assert(buffer->vk.size < UINT_MAX);
   result = nvk_cmd_buffer_upload_alloc(cmd, buffer->vk.size, 16,
                                        &addr, map_out);
   if (unlikely(result != VK_SUCCESS))
      return result;

   assert(buffer->vk.device_address == 0);
   buffer->vk.device_address = addr;

   return VK_SUCCESS;
}

VkResult
nvk_device_init_meta(struct nvk_device *dev)
{
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   VkResult result = vk_meta_device_init(&dev->vk, &dev->meta);
   if (result != VK_SUCCESS)
      return result;

   dev->meta.use_gs_for_layer = pdev->info.cls_eng3d < MAXWELL_B;
   dev->meta.use_rect_list_pipeline = true;
   dev->meta.cmd_bind_map_buffer = nvk_cmd_bind_map_buffer;
   dev->meta.max_bind_map_buffer_size_B = 64 * 1024; /* TODO */

   return VK_SUCCESS;
}

void
nvk_device_finish_meta(struct nvk_device *dev)
{
   vk_meta_device_finish(&dev->vk, &dev->meta);
}

struct nvk_meta_save {
   struct vk_vertex_input_state _dynamic_vi;
   struct vk_sample_locations_state _dynamic_sl;
   struct vk_dynamic_graphics_state dynamic;
   struct nvk_shader *shaders[MESA_SHADER_MESH + 1];
   struct nvk_addr_range vb0;
   struct nvk_descriptor_set_binding desc0;
   struct nvk_buffer_address desc0_set_addr;
   struct nvk_push_descriptor_set push_desc0;
   uint8_t set_dynamic_buffer_start[NVK_MAX_SETS];
   uint8_t push[NVK_MAX_PUSH_SIZE];
};

static void
nvk_meta_begin(struct nvk_cmd_buffer *cmd,
               struct nvk_meta_save *save)
{
   const struct nvk_descriptor_state *desc = &cmd->state.gfx.descriptors;

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 4);

   P_IMMD(p, NV9097, SET_RENDER_ENABLE_OVERRIDE, MODE_ALWAYS_RENDER);

   P_IMMD(p, NV9097, SET_STATISTICS_COUNTER, {
      .da_vertices_generated_enable = false,
      .da_primitives_generated_enable = false,
      .vs_invocations_enable = false,
      .gs_invocations_enable = false,
      .gs_primitives_generated_enable = false,
      .streaming_primitives_succeeded_enable = false,
      .streaming_primitives_needed_enable = false,
      .clipper_invocations_enable = false,
      .clipper_primitives_generated_enable = false,
      .ps_invocations_enable = false,
      .ti_invocations_enable = false,
      .ts_invocations_enable = false,
      .ts_primitives_generated_enable = false,
      .total_streaming_primitives_needed_succeeded_enable = false,
      .vtg_primitives_out_enable = false,
   });

   save->dynamic = cmd->vk.dynamic_graphics_state;
   save->_dynamic_vi = cmd->state.gfx._dynamic_vi;
   save->_dynamic_sl = cmd->state.gfx._dynamic_sl;

   STATIC_ASSERT(sizeof(cmd->state.gfx.shaders) == sizeof(save->shaders));
   memcpy(save->shaders, cmd->state.gfx.shaders, sizeof(save->shaders));

   save->vb0 = cmd->state.gfx.vb0;

   save->desc0 = desc->sets[0];
   nvk_descriptor_state_get_root(desc, sets[0], &save->desc0_set_addr);
   if (desc->sets[0].push != NULL)
      save->push_desc0 = *desc->sets[0].push;

   nvk_descriptor_state_get_root_array(desc, set_dynamic_buffer_start,
                                       0, NVK_MAX_SETS,
                                       save->set_dynamic_buffer_start);
   nvk_descriptor_state_get_root_array(desc, push, 0, NVK_MAX_PUSH_SIZE,
                                       save->push);
}

static void
nvk_meta_end(struct nvk_cmd_buffer *cmd,
             struct nvk_meta_save *save)
{
   struct nvk_descriptor_state *desc = &cmd->state.gfx.descriptors;

   switch (save->desc0.type) {
   case NVK_DESCRIPTOR_SET_TYPE_NONE:
      desc->sets[0].type = NVK_DESCRIPTOR_SET_TYPE_NONE;
      break;

   case NVK_DESCRIPTOR_SET_TYPE_SET: {
      desc->sets[0].type = NVK_DESCRIPTOR_SET_TYPE_SET;
      desc->sets[0].set = save->desc0.set;
      struct nvk_buffer_address addr = nvk_descriptor_set_addr(save->desc0.set);
      nvk_descriptor_state_set_root(cmd, desc, sets[0], addr);
      break;
   }

   case NVK_DESCRIPTOR_SET_TYPE_PUSH:
      desc->sets[0].type = NVK_DESCRIPTOR_SET_TYPE_PUSH;
      desc->sets[0].set = NULL;
      *desc->sets[0].push = save->push_desc0;
      desc->push_dirty |= BITFIELD_BIT(0);
      break;

   case NVK_DESCRIPTOR_SET_TYPE_BUFFER:
      desc->sets[0].type = NVK_DESCRIPTOR_SET_TYPE_BUFFER;
      desc->sets[0].set = NULL;
      nvk_descriptor_state_set_root(cmd, desc, sets[0], save->desc0_set_addr);
      break;

   default:
      UNREACHABLE("Unknown descriptor set type");
   }
   nvk_cmd_dirty_cbufs_for_descriptors(cmd, ~0, 0, 1);

   /* Restore set_dynaic_buffer_start because meta binding set 0 can disturb
    * all dynamic buffers starts for all sets.
    */
   nvk_descriptor_state_set_root_array(cmd, desc, set_dynamic_buffer_start,
                                       0, NVK_MAX_SETS,
                                       save->set_dynamic_buffer_start);

   /* Restore the dynamic state */
   assert(save->dynamic.vi == &cmd->state.gfx._dynamic_vi);
   assert(save->dynamic.ms.sample_locations == &cmd->state.gfx._dynamic_sl);
   cmd->vk.dynamic_graphics_state = save->dynamic;
   cmd->state.gfx._dynamic_vi = save->_dynamic_vi;
   cmd->state.gfx._dynamic_sl = save->_dynamic_sl;
   memcpy(cmd->vk.dynamic_graphics_state.dirty,
          cmd->vk.dynamic_graphics_state.set,
          sizeof(cmd->vk.dynamic_graphics_state.set));

   for (uint32_t stage = 0; stage < ARRAY_SIZE(save->shaders); stage++) {
      if (stage == MESA_SHADER_COMPUTE)
         continue;

      nvk_cmd_bind_graphics_shader(cmd, stage, save->shaders[stage]);
   }

   nvk_cmd_bind_vertex_buffer(cmd, 0, save->vb0);

   nvk_descriptor_state_set_root_array(cmd, desc, push, 0, sizeof(save->push),
                                       save->push);

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 4);

   P_IMMD(p, NV9097, SET_STATISTICS_COUNTER, {
      .da_vertices_generated_enable = true,
      .da_primitives_generated_enable = true,
      .vs_invocations_enable = true,
      .gs_invocations_enable = true,
      .gs_primitives_generated_enable = true,
      .streaming_primitives_succeeded_enable = true,
      .streaming_primitives_needed_enable = true,
      .clipper_invocations_enable = true,
      .clipper_primitives_generated_enable = true,
      .ps_invocations_enable = true,
      .ti_invocations_enable = true,
      .ts_invocations_enable = true,
      .ts_primitives_generated_enable = true,
      .total_streaming_primitives_needed_succeeded_enable = true,
      .vtg_primitives_out_enable = true,
   });

   P_IMMD(p, NV9097, SET_RENDER_ENABLE_OVERRIDE, MODE_USE_RENDER_ENABLE);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdBlitImage2(VkCommandBuffer commandBuffer,
                  const VkBlitImageInfo2 *pBlitImageInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);

   struct nvk_meta_save save;
   nvk_meta_begin(cmd, &save);

   vk_meta_blit_image2(&cmd->vk, &dev->meta, pBlitImageInfo);

   nvk_meta_end(cmd, &save);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdResolveImage2(VkCommandBuffer commandBuffer,
                     const VkResolveImageInfo2 *pResolveImageInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);

   struct nvk_meta_save save;
   nvk_meta_begin(cmd, &save);

   vk_meta_resolve_image2(&cmd->vk, &dev->meta, pResolveImageInfo);

   nvk_meta_end(cmd, &save);
}

void
nvk_meta_resolve_rendering(struct nvk_cmd_buffer *cmd,
                           const VkRenderingInfo *pRenderingInfo)
{
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);

   struct nvk_meta_save save;
   nvk_meta_begin(cmd, &save);

   vk_meta_resolve_rendering(&cmd->vk, &dev->meta, pRenderingInfo);

   nvk_meta_end(cmd, &save);
}
