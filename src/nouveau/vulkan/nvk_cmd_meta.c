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

#include "vk_format.h"

#include "nv_push_cl9097.h"
#include "nv_push_cl90c0.h"
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

   for (unsigned i = 0; i < VK_META_BUFFER_CHUNK_SIZE_COUNT; ++i) {
      dev->meta.buffer_access.optimal_wg_size[i] = 128;
   }

   return VK_SUCCESS;
}

void
nvk_device_finish_meta(struct nvk_device *dev)
{
   vk_meta_device_finish(&dev->vk, &dev->meta);
}

struct nvk_meta_save_gfx {
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

struct nvk_meta_save_compute {
   struct nvk_compute_state state;
};

union nvk_meta_save_generic {
   struct nvk_meta_save_gfx gfx;
   struct nvk_meta_save_compute compute;
};

static void
nvk_meta_begin_gfx(struct nvk_cmd_buffer *cmd,
                   struct nvk_meta_save_gfx *save)
{
   const struct nvk_descriptor_state *desc = &cmd->state.gfx.descriptors;

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 10);
   P_IMMD(p, NV9097, SET_MME_SHADOW_RAM_CONTROL, MODE_METHOD_PASSTHROUGH);
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
   P_IMMD(p, NV9097, SET_ZPASS_PIXEL_COUNT, ENABLE_FALSE);
   P_IMMD(p, NV9097, SET_MME_SHADOW_RAM_CONTROL, MODE_METHOD_TRACK_WITH_FILTER);

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
nvk_meta_end_gfx(struct nvk_cmd_buffer *cmd,
                 struct nvk_meta_save_gfx *save)
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

   /* Replay the previous state from shadow RAM */
   struct nv_push *p = nvk_cmd_buffer_push(cmd, 10);
   P_IMMD(p, NV9097, SET_MME_SHADOW_RAM_CONTROL, MODE_METHOD_REPLAY);
   P_IMMD(p, NV9097, SET_ZPASS_PIXEL_COUNT, ENABLE_FALSE);
   P_IMMD(p, NV9097, SET_STATISTICS_COUNTER, {});
   P_IMMD(p, NV9097, SET_RENDER_ENABLE_OVERRIDE, MODE_USE_RENDER_ENABLE);
   P_IMMD(p, NV9097, SET_MME_SHADOW_RAM_CONTROL, MODE_METHOD_TRACK_WITH_FILTER);
}

static void
nvk_meta_begin_compute(struct nvk_cmd_buffer *cmd,
                       struct nvk_meta_save_compute *save)
{
   assert(cmd->state.cs.descriptors.flush_root == NULL);
   save->state = cmd->state.cs;
   nvk_cmd_invalidate_compute_state(cmd);

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 1);
   P_IMMD_WORD(p, NV90C0, SET_RENDER_ENABLE_OVERRIDE, MODE_ALWAYS_RENDER);
}

static void
nvk_meta_end_compute(struct nvk_cmd_buffer *cmd,
                     struct nvk_meta_save_compute *save)
{
   struct nv_push *p = nvk_cmd_buffer_push(cmd, 1);
   P_IMMD_WORD(p, NV90C0, SET_RENDER_ENABLE_OVERRIDE, MODE_USE_RENDER_ENABLE);

   cmd->state.cs = save->state;
}

static void
nvk_meta_begin_generic(struct nvk_cmd_buffer *cmd,
                       union nvk_meta_save_generic *save,
                       VkPipelineBindPoint engine)
{
   if (engine == VK_PIPELINE_BIND_POINT_GRAPHICS) {
      nvk_meta_begin_gfx(cmd, &save->gfx);
   } else {
      assert(engine == VK_PIPELINE_BIND_POINT_COMPUTE);
      nvk_meta_begin_compute(cmd, &save->compute);
   }
}

static void
nvk_meta_end_generic(struct nvk_cmd_buffer *cmd,
                     union nvk_meta_save_generic *save,
                     VkPipelineBindPoint engine)
{
   if (engine == VK_PIPELINE_BIND_POINT_GRAPHICS) {
      nvk_meta_end_gfx(cmd, &save->gfx);
   } else {
      assert(engine == VK_PIPELINE_BIND_POINT_COMPUTE);
      nvk_meta_end_compute(cmd, &save->compute);
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdBlitImage2(VkCommandBuffer commandBuffer,
                  const VkBlitImageInfo2 *pBlitImageInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);

   struct nvk_meta_save_gfx save;
   nvk_meta_begin_gfx(cmd, &save);

   vk_meta_blit_image2(&cmd->vk, &dev->meta, pBlitImageInfo);

   nvk_meta_end_gfx(cmd, &save);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdResolveImage2(VkCommandBuffer commandBuffer,
                     const VkResolveImageInfo2 *pResolveImageInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);

   struct nvk_meta_save_gfx save;
   nvk_meta_begin_gfx(cmd, &save);

   vk_meta_resolve_image2(&cmd->vk, &dev->meta, pResolveImageInfo);

   nvk_meta_end_gfx(cmd, &save);
}

void
nvk_meta_resolve_rendering(struct nvk_cmd_buffer *cmd,
                           const VkRenderingInfo *pRenderingInfo)
{
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);

   struct nvk_meta_save_gfx save;
   nvk_meta_begin_gfx(cmd, &save);

   vk_meta_resolve_rendering(&cmd->vk, &dev->meta, pRenderingInfo);

   nvk_meta_end_gfx(cmd, &save);
}

static bool
nvk_meta_image_copy_gfx_supported(struct nvk_image *img)
{
   if (vk_format_is_depth_or_stencil(img->vk.format))
      return false;
   if (vk_format_is_compressed(img->vk.format))
      return false;
   if (vk_format_get_ycbcr_info(img->vk.format))
      return false;

   assert(img->plane_count == 1);
   const struct nvk_image_plane *plane = &img->planes[0];
   const struct nil_image *nil_image = &plane->nil;

   for (int l = 0; l < nil_image->num_levels; l++) {
      const struct nil_image_level *level = &nil_image->levels[l];
      if (level->tiling.z_log2 != 0)
         return false;

      if (level->tiling.gob_type == NIL_GOB_TYPE_LINEAR &&
          !nvk_image_plane_aligned_for_linear_attachment(plane, level))
         return false;
   }
   return true;
}

static bool
nvk_meta_image_copy_compute_supported(struct nvk_image *img)
{
   if (vk_format_is_depth_or_stencil(img->vk.format))
      return false;
   if (vk_format_is_compressed(img->vk.format))
      return false;
   if (vk_format_get_ycbcr_info(img->vk.format))
      return false;

   return true;
}

static inline
uint32_t extent_size(VkExtent3D e) {
   return e.width * e.height * e.depth;
}

static struct vk_meta_copy_image_properties
nvk_meta_copy_get_image_properties(struct nvk_image *img,
                                   bool is_destination)
{
   struct vk_meta_copy_image_properties props = {};

   assert(!vk_format_is_depth_or_stencil(img->vk.format));
   assert(!vk_format_get_ycbcr_info(img->vk.format));

   unsigned blk_sz = vk_format_get_blocksize(img->vk.format);
   props.color.view_format = vk_meta_get_uint_format_for_blk_size(blk_sz);

   const struct nvk_image_plane *plane = &img->planes[0];
   const struct nil_image *nil_image = &plane->nil;

   const struct nil_image_level *level =
      &nil_image->levels[0];

   if (level->tiling.gob_type == NIL_GOB_TYPE_LINEAR) {
      props.tile_size.width = 128;
      props.tile_size.height = 1;
      props.tile_size.depth = 1;
   } else {
      struct nil_Extent4D_Bytes gob_extent =
         nil_gob_type_extent_B(level->tiling.gob_type);
      props.tile_size.width = gob_extent.width / blk_sz;
      props.tile_size.height = gob_extent.height;
      props.tile_size.depth = gob_extent.depth;

      const uint32_t MIN_SIZE = 128;

      if (extent_size(props.tile_size) < MIN_SIZE)
         props.tile_size.height *= MIN2(1 << level->tiling.y_log2,
                                        MIN_SIZE / extent_size(props.tile_size));

      if (extent_size(props.tile_size) < MIN_SIZE)
         props.tile_size.depth *= MIN2(1 << level->tiling.z_log2,
                                       MIN_SIZE / extent_size(props.tile_size));

      if (extent_size(props.tile_size) < MIN_SIZE)
         props.tile_size.width *= MIN_SIZE / extent_size(props.tile_size);
   }
   ASSERTED uint32_t wg_size = extent_size(props.tile_size);
   /* We want the workgroup size to be in this range for occupancy */
   assert(wg_size >= 128 && wg_size <= 512 &&
          util_is_power_of_two_nonzero(wg_size));

   return props;
}

static void
nvk_cmd_copy_image_to_buffer_meta(struct nvk_cmd_buffer *cmd,
                                  const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo)
{
   VK_FROM_HANDLE(nvk_image, src, pCopyImageToBufferInfo->srcImage);
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);

   struct vk_meta_copy_image_properties src_img_props =
      nvk_meta_copy_get_image_properties(src, true);

   struct nvk_meta_save_compute save;
   nvk_meta_begin_compute(cmd, &save);
   vk_meta_copy_image_to_buffer(&cmd->vk, &dev->meta, pCopyImageToBufferInfo,
                                &src_img_props);
   nvk_meta_end_compute(cmd, &save);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdCopyImageToBuffer2(VkCommandBuffer commandBuffer,
                          const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvk_image, src, pCopyImageToBufferInfo->srcImage);

   VkQueueFlags queue_flags = nvk_cmd_buffer_queue_flags(cmd);
   if ((queue_flags & VK_QUEUE_COMPUTE_BIT) &&
       nvk_meta_image_copy_compute_supported(src)) {
      nvk_cmd_copy_image_to_buffer_meta(cmd, pCopyImageToBufferInfo);
   } else {
      nvk_cmd_copy_image_to_buffer_ce(cmd, pCopyImageToBufferInfo);
   }
}

static void
nvk_cmd_copy_buffer_to_image_meta(struct nvk_cmd_buffer *cmd,
                                  const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo,
                                  VkPipelineBindPoint engine)
{
   VK_FROM_HANDLE(nvk_image, dst, pCopyBufferToImageInfo->dstImage);
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);

   struct vk_meta_copy_image_properties dst_img_props =
      nvk_meta_copy_get_image_properties(dst, true);

   union nvk_meta_save_generic save;
   nvk_meta_begin_generic(cmd, &save, engine);
   vk_meta_copy_buffer_to_image(&cmd->vk, &dev->meta, pCopyBufferToImageInfo,
                                &dst_img_props, engine);
   nvk_meta_end_generic(cmd, &save, engine);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdCopyBufferToImage2(VkCommandBuffer commandBuffer,
                          const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvk_image, dst, pCopyBufferToImageInfo->dstImage);

   VkQueueFlags queue_flags = nvk_cmd_buffer_queue_flags(cmd);
   if ((queue_flags & VK_QUEUE_COMPUTE_BIT) &&
       nvk_meta_image_copy_compute_supported(dst)) {
      nvk_cmd_copy_buffer_to_image_meta(cmd, pCopyBufferToImageInfo,
                              VK_PIPELINE_BIND_POINT_COMPUTE);
   } else {
      nvk_cmd_copy_buffer_to_image_ce(cmd, pCopyBufferToImageInfo);
   }
}

static void
nvk_cmd_copy_image_meta(struct nvk_cmd_buffer *cmd,
                        const VkCopyImageInfo2 *pCopyImageInfo,
                        VkPipelineBindPoint engine)
{
   VK_FROM_HANDLE(nvk_image, src, pCopyImageInfo->srcImage);
   VK_FROM_HANDLE(nvk_image, dst, pCopyImageInfo->dstImage);

   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);

   struct vk_meta_copy_image_properties src_img_props =
      nvk_meta_copy_get_image_properties(src, false);
   struct vk_meta_copy_image_properties dst_img_props =
      nvk_meta_copy_get_image_properties(dst, true);

   union nvk_meta_save_generic save;
   nvk_meta_begin_generic(cmd, &save, engine);
   vk_meta_copy_image(&cmd->vk, &dev->meta, pCopyImageInfo,
                      &src_img_props, &dst_img_props, engine);
   nvk_meta_end_generic(cmd, &save, engine);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdCopyImage2(VkCommandBuffer commandBuffer,
                  const VkCopyImageInfo2 *pCopyImageInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvk_image, src, pCopyImageInfo->srcImage);
   VK_FROM_HANDLE(nvk_image, dst, pCopyImageInfo->dstImage);

   VkQueueFlags queue_flags = nvk_cmd_buffer_queue_flags(cmd);
   if ((queue_flags & VK_QUEUE_GRAPHICS_BIT) &&
       nvk_meta_image_copy_gfx_supported(src) &&
       nvk_meta_image_copy_gfx_supported(dst)) {
      nvk_cmd_copy_image_meta(cmd, pCopyImageInfo,
                              VK_PIPELINE_BIND_POINT_GRAPHICS);
   } else if ((queue_flags & VK_QUEUE_COMPUTE_BIT) &&
       nvk_meta_image_copy_compute_supported(src) &&
       nvk_meta_image_copy_compute_supported(dst)) {
      nvk_cmd_copy_image_meta(cmd, pCopyImageInfo,
                              VK_PIPELINE_BIND_POINT_COMPUTE);
   } else {
      nvk_cmd_copy_image_ce(cmd, pCopyImageInfo);
   }
}

static void
nvk_cmd_copy_buffer_meta(struct nvk_cmd_buffer *cmd,
                         const VkCopyBufferInfo2 *pCopyBufferInfo)
{
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);

   struct nvk_meta_save_compute save;
   nvk_meta_begin_compute(cmd, &save);
   vk_meta_copy_buffer(&cmd->vk, &dev->meta, pCopyBufferInfo);
   nvk_meta_end_compute(cmd, &save);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdCopyBuffer2(VkCommandBuffer commandBuffer,
                   const VkCopyBufferInfo2 *pCopyBufferInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   VkQueueFlags queue_flags = nvk_cmd_buffer_queue_flags(cmd);
   if (queue_flags & VK_QUEUE_COMPUTE_BIT) {
      nvk_cmd_copy_buffer_meta(cmd, pCopyBufferInfo);
   } else {
      nvk_cmd_copy_buffer_ce(cmd, pCopyBufferInfo);
   }
}

static void
nvk_cmd_fill_buffer_meta(struct nvk_cmd_buffer *cmd,
                         VkBuffer dstBuffer,
                         VkDeviceSize dstOffset,
                         VkDeviceSize size,
                         uint32_t data)
{
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);

   struct nvk_meta_save_compute save;
   nvk_meta_begin_compute(cmd, &save);
   vk_meta_fill_buffer(&cmd->vk, &dev->meta, dstBuffer, dstOffset, size, data);
   nvk_meta_end_compute(cmd, &save);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdFillBuffer(VkCommandBuffer commandBuffer,
                  VkBuffer dstBuffer,
                  VkDeviceSize dstOffset,
                  VkDeviceSize size,
                  uint32_t data)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   VkQueueFlags queue_flags = nvk_cmd_buffer_queue_flags(cmd);
   if (queue_flags & VK_QUEUE_COMPUTE_BIT) {
      nvk_cmd_fill_buffer_meta(cmd, dstBuffer, dstOffset, size, data);
   } else {
      VK_FROM_HANDLE(nvk_buffer, dst_buffer, dstBuffer);

      uint64_t dst_addr = vk_buffer_address(&dst_buffer->vk, dstOffset);
      size = vk_buffer_range(&dst_buffer->vk, dstOffset, size);

      nvk_cmd_fill_memory_ce(cmd, dst_addr, size, data);
   }
}
