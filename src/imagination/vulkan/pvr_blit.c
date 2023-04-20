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

#include <assert.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "pvr_clear.h"
#include "pvr_csb.h"
#include "pvr_formats.h"
#include "pvr_private.h"
#include "pvr_shader_factory.h"
#include "pvr_static_shaders.h"
#include "util/list.h"
#include "vk_alloc.h"
#include "vk_command_buffer.h"
#include "vk_command_pool.h"
#include "vk_log.h"

/* TODO: Investigate where this limit comes from. */
#define PVR_MAX_TRANSFER_SIZE_IN_TEXELS 2048U

void pvr_CmdBlitImage2KHR(VkCommandBuffer commandBuffer,
                          const VkBlitImageInfo2 *pBlitImageInfo)
{
   assert(!"Unimplemented");
}

VkResult
pvr_copy_or_resolve_color_image_region(struct pvr_cmd_buffer *cmd_buffer,
                                       const struct pvr_image *src,
                                       const struct pvr_image *dst,
                                       const VkImageCopy2 *region)
{
   assert(!"Unimplemented");
   return VK_SUCCESS;
}

void pvr_CmdCopyImageToBuffer2KHR(
   VkCommandBuffer commandBuffer,
   const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo)
{
   assert(!"Unimplemented");
}

void pvr_CmdCopyImage2KHR(VkCommandBuffer commandBuffer,
                          const VkCopyImageInfo2 *pCopyImageInfo)
{
   assert(!"Unimplemented");
}

void pvr_CmdUpdateBuffer(VkCommandBuffer commandBuffer,
                         VkBuffer dstBuffer,
                         VkDeviceSize dstOffset,
                         VkDeviceSize dataSize,
                         const void *pData)
{
   assert(!"Unimplemented");
}

void pvr_CmdFillBuffer(VkCommandBuffer commandBuffer,
                       VkBuffer dstBuffer,
                       VkDeviceSize dstOffset,
                       VkDeviceSize fillSize,
                       uint32_t data)
{
   assert(!"Unimplemented");
}

void pvr_CmdCopyBufferToImage2KHR(
   VkCommandBuffer commandBuffer,
   const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo)
{
   assert(!"Unimplemented");
}

void pvr_CmdClearColorImage(VkCommandBuffer commandBuffer,
                            VkImage _image,
                            VkImageLayout imageLayout,
                            const VkClearColorValue *pColor,
                            uint32_t rangeCount,
                            const VkImageSubresourceRange *pRanges)
{
   assert(!"Unimplemented");
}

void pvr_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer,
                                   VkImage image_h,
                                   VkImageLayout imageLayout,
                                   const VkClearDepthStencilValue *pDepthStencil,
                                   uint32_t rangeCount,
                                   const VkImageSubresourceRange *pRanges)
{
   assert(!"Unimplemented");
}

void pvr_CmdCopyBuffer2KHR(VkCommandBuffer commandBuffer,
                           const VkCopyBufferInfo2 *pCopyBufferInfo)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   PVR_FROM_HANDLE(pvr_buffer, src, pCopyBufferInfo->srcBuffer);
   PVR_FROM_HANDLE(pvr_buffer, dst, pCopyBufferInfo->dstBuffer);
   const size_t regions_size =
      pCopyBufferInfo->regionCount * sizeof(*pCopyBufferInfo->pRegions);
   struct pvr_transfer_cmd *transfer_cmd;

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

   transfer_cmd = vk_alloc(&cmd_buffer->vk.pool->alloc,
                           sizeof(*transfer_cmd) + regions_size,
                           8U,
                           VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!transfer_cmd) {
      cmd_buffer->state.status =
         vk_error(cmd_buffer, VK_ERROR_OUT_OF_HOST_MEMORY);

      return;
   }

   transfer_cmd->src = src;
   transfer_cmd->dst = dst;
   transfer_cmd->region_count = pCopyBufferInfo->regionCount;
   memcpy(transfer_cmd->regions, pCopyBufferInfo->pRegions, regions_size);

   pvr_cmd_buffer_add_transfer_cmd(cmd_buffer, transfer_cmd);
}

/**
 * \brief Returns the maximum number of layers to clear starting from base_layer
 * that contain or match the target rectangle.
 *
 * \param[in] target_rect      The region which the clear should contain or
 *                             match.
 * \param[in] base_layer       The layer index to start at.
 * \param[in] clear_rect_count Amount of clear_rects
 * \param[in] clear_rects      Array of clear rects.
 *
 * \return Max number of layers that cover or match the target region.
 */
static uint32_t
pvr_get_max_layers_covering_target(VkRect2D target_rect,
                                   uint32_t base_layer,
                                   uint32_t clear_rect_count,
                                   const VkClearRect *clear_rects)
{
   const int32_t target_x0 = target_rect.offset.x;
   const int32_t target_x1 = target_x0 + (int32_t)target_rect.extent.width;
   const int32_t target_y0 = target_rect.offset.y;
   const int32_t target_y1 = target_y0 + (int32_t)target_rect.extent.height;

   uint32_t layer_count = 0;

   assert((int64_t)target_x0 + (int64_t)target_rect.extent.width <= INT32_MAX);
   assert((int64_t)target_y0 + (int64_t)target_rect.extent.height <= INT32_MAX);

   for (uint32_t i = 0; i < clear_rect_count; i++) {
      const VkClearRect *clear_rect = &clear_rects[i];
      const uint32_t max_layer =
         clear_rect->baseArrayLayer + clear_rect->layerCount;
      bool target_is_covered;
      int32_t x0, x1;
      int32_t y0, y1;

      if (clear_rect->baseArrayLayer == 0)
         continue;

      assert((uint64_t)clear_rect->baseArrayLayer + clear_rect->layerCount <=
             UINT32_MAX);

      /* Check for layer intersection. */
      if (clear_rect->baseArrayLayer > base_layer || max_layer <= base_layer)
         continue;

      x0 = clear_rect->rect.offset.x;
      x1 = x0 + (int32_t)clear_rect->rect.extent.width;
      y0 = clear_rect->rect.offset.y;
      y1 = y0 + (int32_t)clear_rect->rect.extent.height;

      assert((int64_t)x0 + (int64_t)clear_rect->rect.extent.width <= INT32_MAX);
      assert((int64_t)y0 + (int64_t)clear_rect->rect.extent.height <=
             INT32_MAX);

      target_is_covered = x0 <= target_x0 && x1 >= target_x1;
      target_is_covered &= y0 <= target_y0 && y1 >= target_y1;

      if (target_is_covered)
         layer_count = MAX2(layer_count, max_layer - base_layer);
   }

   return layer_count;
}

/* Return true if vertex shader is required to output render target id to pick
 * the texture array layer.
 */
static inline bool
pvr_clear_needs_rt_id_output(struct pvr_device_info *dev_info,
                             uint32_t rect_count,
                             const VkClearRect *rects)
{
   if (!PVR_HAS_FEATURE(dev_info, gs_rta_support))
      return false;

   for (uint32_t i = 0; i < rect_count; i++) {
      if (rects[i].baseArrayLayer != 0 || rects[i].layerCount > 1)
         return true;
   }

   return false;
}

static VkResult pvr_clear_color_attachment_static_create_consts_buffer(
   struct pvr_cmd_buffer *cmd_buffer,
   const struct pvr_shader_factory_info *shader_info,
   const uint32_t clear_color[static const PVR_CLEAR_COLOR_ARRAY_SIZE],
   ASSERTED bool uses_tile_buffer,
   uint32_t tile_buffer_idx,
   struct pvr_bo **const const_shareds_buffer_out)
{
   struct pvr_device *device = cmd_buffer->device;
   struct pvr_bo *const_shareds_buffer;
   struct pvr_bo *tile_buffer;
   uint64_t tile_dev_addr;
   uint32_t *buffer;
   VkResult result;

   /* TODO: This doesn't need to be aligned to slc size. Alignment to 4 is fine.
    * Change pvr_cmd_buffer_alloc_mem() to take in an alignment?
    */
   result = pvr_cmd_buffer_alloc_mem(cmd_buffer,
                                     device->heaps.general_heap,
                                     shader_info->const_shared_regs,
                                     PVR_BO_ALLOC_FLAG_CPU_MAPPED,
                                     &const_shareds_buffer);
   if (result != VK_SUCCESS)
      return result;

   buffer = const_shareds_buffer->bo->map;

   for (uint32_t i = 0; i < PVR_CLEAR_ATTACHMENT_CONST_COUNT; i++) {
      uint32_t dest_idx = shader_info->driver_const_location_map[i];

      if (dest_idx == PVR_CLEAR_ATTACHMENT_DEST_ID_UNUSED)
         continue;

      assert(dest_idx < shader_info->const_shared_regs);

      switch (i) {
      case PVR_CLEAR_ATTACHMENT_CONST_COMPONENT_0:
      case PVR_CLEAR_ATTACHMENT_CONST_COMPONENT_1:
      case PVR_CLEAR_ATTACHMENT_CONST_COMPONENT_2:
      case PVR_CLEAR_ATTACHMENT_CONST_COMPONENT_3:
         buffer[dest_idx] = clear_color[i];
         break;

      case PVR_CLEAR_ATTACHMENT_CONST_TILE_BUFFER_UPPER:
         assert(uses_tile_buffer);
         tile_buffer = device->tile_buffer_state.buffers[tile_buffer_idx];
         tile_dev_addr = tile_buffer->vma->dev_addr.addr;
         buffer[dest_idx] = (uint32_t)(tile_dev_addr >> 32);
         break;

      case PVR_CLEAR_ATTACHMENT_CONST_TILE_BUFFER_LOWER:
         assert(uses_tile_buffer);
         tile_buffer = device->tile_buffer_state.buffers[tile_buffer_idx];
         tile_dev_addr = tile_buffer->vma->dev_addr.addr;
         buffer[dest_idx] = (uint32_t)tile_dev_addr;
         break;

      default:
         unreachable("Unsupported clear attachment const type.");
      }
   }

   for (uint32_t i = 0; i < shader_info->num_static_const; i++) {
      const struct pvr_static_buffer *static_buff =
         &shader_info->static_const_buffer[i];

      assert(static_buff->dst_idx < shader_info->const_shared_regs);

      buffer[static_buff->dst_idx] = static_buff->value;
   }

   pvr_bo_cpu_unmap(device, const_shareds_buffer);

   *const_shareds_buffer_out = const_shareds_buffer;

   return VK_SUCCESS;
}

static VkResult pvr_clear_color_attachment_static(
   struct pvr_cmd_buffer *cmd_buffer,
   const struct usc_mrt_resource *mrt_resource,
   VkFormat format,
   uint32_t clear_color[static const PVR_CLEAR_COLOR_ARRAY_SIZE],
   uint32_t template_idx,
   uint32_t stencil,
   bool vs_has_rt_id_output)
{
   struct pvr_device *device = cmd_buffer->device;
   ASSERTED const struct pvr_device_info *dev_info = &device->pdevice->dev_info;
   ASSERTED const bool has_eight_output_registers =
      PVR_HAS_FEATURE(dev_info, eight_output_registers);
   const struct pvr_device_static_clear_state *dev_clear_state =
      &device->static_clear_state;
   const bool uses_tile_buffer = mrt_resource->type ==
                                 USC_MRT_RESOURCE_TYPE_MEMORY;
   const struct pvr_pds_clear_attachment_program_info *clear_attachment_program;
   struct pvr_pds_pixel_shader_sa_program texture_program;
   uint32_t pds_state[PVR_STATIC_CLEAR_PDS_STATE_COUNT];
   const struct pvr_shader_factory_info *shader_info;
   struct pvr_static_clear_ppp_template template;
   struct pvr_bo *pds_texture_program_bo;
   struct pvr_bo *const_shareds_buffer;
   uint64_t pds_texture_program_addr;
   uint32_t tile_buffer_idx = 0;
   uint32_t out_reg_count;
   uint32_t output_offset;
   struct pvr_bo *pvr_bo;
   uint32_t program_idx;
   uint32_t *buffer;
   VkResult result;

   out_reg_count =
      DIV_ROUND_UP(pvr_get_pbe_accum_format_size_in_bytes(format), 4U);

   if (uses_tile_buffer) {
      tile_buffer_idx = mrt_resource->mem.tile_buffer;
      output_offset = mrt_resource->mem.offset_dw;
   } else {
      output_offset = mrt_resource->reg.offset;
   }

   assert(has_eight_output_registers || out_reg_count + output_offset <= 4);

   program_idx = pvr_get_clear_attachment_program_index(out_reg_count,
                                                        output_offset,
                                                        uses_tile_buffer);

   shader_info = clear_attachment_collection[program_idx].info;

   result = pvr_clear_color_attachment_static_create_consts_buffer(
      cmd_buffer,
      shader_info,
      clear_color,
      uses_tile_buffer,
      tile_buffer_idx,
      &const_shareds_buffer);
   if (result != VK_SUCCESS)
      return result;

   /* clang-format off */
   texture_program = (struct pvr_pds_pixel_shader_sa_program){
      .num_texture_dma_kicks = 1,
      .texture_dma_address = {
         [0] = const_shareds_buffer->vma->dev_addr.addr,
      }
   };
   /* clang-format on */

   pvr_csb_pack (&texture_program.texture_dma_control[0],
                 PDSINST_DOUT_FIELDS_DOUTD_SRC1,
                 doutd_src1) {
      doutd_src1.dest = PVRX(PDSINST_DOUTD_DEST_COMMON_STORE);
      doutd_src1.bsize = shader_info->const_shared_regs;
   }

   clear_attachment_program =
      &dev_clear_state->pds_clear_attachment_program_info[program_idx];

   /* TODO: This doesn't need to be aligned to slc size. Alignment to 4 is fine.
    * Change pvr_cmd_buffer_alloc_mem() to take in an alignment?
    */
   result = pvr_cmd_buffer_alloc_mem(
      cmd_buffer,
      device->heaps.pds_heap,
      clear_attachment_program->texture_program_data_size,
      PVR_BO_ALLOC_FLAG_CPU_MAPPED,
      &pds_texture_program_bo);
   if (result != VK_SUCCESS) {
      list_del(&const_shareds_buffer->link);
      pvr_bo_free(device, const_shareds_buffer);

      return result;
   }

   buffer = pds_texture_program_bo->bo->map;
   pds_texture_program_addr = pds_texture_program_bo->vma->dev_addr.addr -
                              device->heaps.pds_heap->base_addr.addr;

   pvr_pds_generate_pixel_shader_sa_texture_state_data(
      &texture_program,
      buffer,
      &device->pdevice->dev_info);

   pvr_bo_cpu_unmap(device, pds_texture_program_bo);

   pvr_csb_pack (&pds_state[PVR_STATIC_CLEAR_PPP_PDS_TYPE_SHADERBASE],
                 TA_STATE_PDS_SHADERBASE,
                 shaderbase) {
      shaderbase.addr = clear_attachment_program->pixel_program_offset;
   }

   pvr_csb_pack (&pds_state[PVR_STATIC_CLEAR_PPP_PDS_TYPE_TEXUNICODEBASE],
                 TA_STATE_PDS_TEXUNICODEBASE,
                 texunicodebase) {
      texunicodebase.addr = clear_attachment_program->texture_program_offset;
   }

   pvr_csb_pack (&pds_state[PVR_STATIC_CLEAR_PPP_PDS_TYPE_SIZEINFO1],
                 TA_STATE_PDS_SIZEINFO1,
                 sizeinfo1) {
      sizeinfo1.pds_texturestatesize = DIV_ROUND_UP(
         clear_attachment_program->texture_program_data_size,
         PVRX(TA_STATE_PDS_SIZEINFO1_PDS_TEXTURESTATESIZE_UNIT_SIZE));

      sizeinfo1.pds_tempsize =
         DIV_ROUND_UP(clear_attachment_program->texture_program_pds_temps_count,
                      PVRX(TA_STATE_PDS_SIZEINFO1_PDS_TEMPSIZE_UNIT_SIZE));
   }

   pvr_csb_pack (&pds_state[PVR_STATIC_CLEAR_PPP_PDS_TYPE_SIZEINFO2],
                 TA_STATE_PDS_SIZEINFO2,
                 sizeinfo2) {
      sizeinfo2.usc_sharedsize =
         DIV_ROUND_UP(shader_info->const_shared_regs,
                      PVRX(TA_STATE_PDS_SIZEINFO2_USC_SHAREDSIZE_UNIT_SIZE));
   }

   /* Dummy coefficient loading program. */
   pds_state[PVR_STATIC_CLEAR_PPP_PDS_TYPE_VARYINGBASE] = 0;

   pvr_csb_pack (&pds_state[PVR_STATIC_CLEAR_PPP_PDS_TYPE_TEXTUREDATABASE],
                 TA_STATE_PDS_TEXTUREDATABASE,
                 texturedatabase) {
      texturedatabase.addr = PVR_DEV_ADDR(pds_texture_program_addr);
   }

   assert(template_idx < PVR_STATIC_CLEAR_VARIANT_COUNT);
   template =
      cmd_buffer->device->static_clear_state.ppp_templates[template_idx];

   template.config.pds_state = &pds_state;

   template.config.ispctl.upass =
      cmd_buffer->state.render_pass_info.isp_userpass;

   if (template_idx & VK_IMAGE_ASPECT_STENCIL_BIT)
      template.config.ispa.sref = stencil;

   if (vs_has_rt_id_output) {
      template.config.output_sel.rhw_pres = true;
      template.config.output_sel.render_tgt_pres = true;
      template.config.output_sel.vtxsize = 4 + 1;
   }

   result = pvr_emit_ppp_from_template(
      &cmd_buffer->state.current_sub_cmd->gfx.control_stream,
      &template,
      &pvr_bo);
   if (result != VK_SUCCESS) {
      list_del(&pds_texture_program_bo->link);
      pvr_bo_free(device, pds_texture_program_bo);

      list_del(&const_shareds_buffer->link);
      pvr_bo_free(device, const_shareds_buffer);

      cmd_buffer->state.status = result;
      return result;
   }

   list_add(&pvr_bo->link, &cmd_buffer->bo_list);

   return VK_SUCCESS;
}

static void pvr_clear_attachments(struct pvr_cmd_buffer *cmd_buffer,
                                  uint32_t attachment_count,
                                  const VkClearAttachment *attachments,
                                  uint32_t rect_count,
                                  const VkClearRect *rects)
{
   const struct pvr_render_pass *pass = cmd_buffer->state.render_pass_info.pass;
   struct pvr_render_pass_info *pass_info = &cmd_buffer->state.render_pass_info;
   const struct pvr_renderpass_hwsetup_subpass *hw_pass =
      pvr_get_hw_subpass(pass, pass_info->subpass_idx);
   struct pvr_sub_cmd_gfx *sub_cmd = &cmd_buffer->state.current_sub_cmd->gfx;
   struct pvr_device_info *dev_info = &cmd_buffer->device->pdevice->dev_info;
   struct pvr_render_subpass *sub_pass = &pass->subpasses[hw_pass->index];
   bool z_replicate = hw_pass->z_replicate != -1;
   uint32_t vs_output_size_in_bytes;
   bool vs_has_rt_id_output;

   /* TODO: This function can be optimized so that most of the device memory
    * gets allocated together in one go and then filled as needed. There might
    * also be opportunities to reuse pds code and data segments.
    */

   assert(cmd_buffer->state.current_sub_cmd->type == PVR_SUB_CMD_TYPE_GRAPHICS);

   pvr_reset_graphics_dirty_state(cmd_buffer, false);

   /* We'll be emitting to the control stream. */
   sub_cmd->empty_cmd = false;

   vs_has_rt_id_output =
      pvr_clear_needs_rt_id_output(dev_info, rect_count, rects);

   /* 4 because we're expecting the USC to output X, Y, Z, and W. */
   vs_output_size_in_bytes = 4 * sizeof(uint32_t);
   if (vs_has_rt_id_output)
      vs_output_size_in_bytes += sizeof(uint32_t);

   for (uint32_t i = 0; i < attachment_count; i++) {
      const VkClearAttachment *attachment = &attachments[i];
      struct pvr_pds_vertex_shader_program pds_program;
      struct pvr_pds_upload pds_program_upload = { 0 };
      uint64_t current_base_array_layer = ~0;
      VkResult result;
      float depth;

      if (attachment->aspectMask == VK_IMAGE_ASPECT_COLOR_BIT) {
         uint32_t packed_clear_color[PVR_CLEAR_COLOR_ARRAY_SIZE];
         const struct usc_mrt_resource *mrt_resource;
         uint32_t global_attachment_idx;
         uint32_t local_attachment_idx;
         VkFormat format;

         local_attachment_idx = attachment->colorAttachment;
         mrt_resource = &hw_pass->setup.mrt_resources[local_attachment_idx];

         assert(local_attachment_idx < sub_pass->color_count);
         global_attachment_idx =
            sub_pass->color_attachments[local_attachment_idx];

         if (global_attachment_idx == VK_ATTACHMENT_UNUSED)
            continue;

         assert(global_attachment_idx < pass->attachment_count);
         format = pass->attachments[global_attachment_idx].vk_format;

         assert(format != VK_FORMAT_UNDEFINED);

         pvr_get_hw_clear_color(format,
                                attachment->clearValue.color,
                                packed_clear_color);

         result = pvr_clear_color_attachment_static(cmd_buffer,
                                                    mrt_resource,
                                                    format,
                                                    packed_clear_color,
                                                    VK_IMAGE_ASPECT_COLOR_BIT,
                                                    0,
                                                    vs_has_rt_id_output);
         if (result != VK_SUCCESS)
            return;
      } else if (z_replicate &&
                 attachment->aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) {
         const VkClearColorValue clear_color = {
            .float32 = { [0] = attachment->clearValue.depthStencil.depth, },
         };
         const uint32_t template_idx = attachment->aspectMask |
                                       VK_IMAGE_ASPECT_COLOR_BIT;
         const uint32_t stencil = attachment->clearValue.depthStencil.stencil;
         uint32_t packed_clear_color[PVR_CLEAR_COLOR_ARRAY_SIZE];
         const struct usc_mrt_resource *mrt_resource;

         assert(hw_pass->z_replicate > 0);
         mrt_resource = &hw_pass->setup.mrt_resources[hw_pass->z_replicate];

         pvr_get_hw_clear_color(VK_FORMAT_R32_SFLOAT,
                                clear_color,
                                packed_clear_color);

         result = pvr_clear_color_attachment_static(cmd_buffer,
                                                    mrt_resource,
                                                    VK_FORMAT_R32_SFLOAT,
                                                    packed_clear_color,
                                                    template_idx,
                                                    stencil,
                                                    vs_has_rt_id_output);
         if (result != VK_SUCCESS)
            return;
      } else {
         const uint32_t template_idx = attachment->aspectMask;
         struct pvr_static_clear_ppp_template template;
         struct pvr_bo *pvr_bo;

         assert(template_idx < PVR_STATIC_CLEAR_VARIANT_COUNT);
         template =
            cmd_buffer->device->static_clear_state.ppp_templates[template_idx];

         if (attachment->aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) {
            template.config.ispa.sref =
               attachment->clearValue.depthStencil.stencil;
         }

         if (vs_has_rt_id_output) {
            template.config.output_sel.rhw_pres = true;
            template.config.output_sel.render_tgt_pres = true;
            template.config.output_sel.vtxsize = 4 + 1;
         }

         result = pvr_emit_ppp_from_template(&sub_cmd->control_stream,
                                             &template,
                                             &pvr_bo);
         if (result != VK_SUCCESS) {
            cmd_buffer->state.status = result;
            return;
         }

         list_add(&pvr_bo->link, &cmd_buffer->bo_list);
      }

      if (attachment->aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)
         depth = attachment->clearValue.depthStencil.depth;
      else
         depth = 1.0f;

      if (vs_has_rt_id_output) {
         const struct pvr_device_static_clear_state *dev_clear_state =
            &cmd_buffer->device->static_clear_state;
         const struct pvr_bo *multi_layer_vert_bo =
            dev_clear_state->usc_multi_layer_vertex_shader_bo;

         /* We can't use the device's passthrough pds program since it doesn't
          * have iterate_instance_id enabled. We'll be uploading code sections
          * per each clear rect.
          */

         /* TODO: See if we can allocate all the code section memory in one go.
          * We'd need to make sure that changing instance_id_modifier doesn't
          * change the code section size.
          * Also check if we can reuse the same code segment for each rect.
          * Seems like the instance_id_modifier is written into the data section
          * and used by the pds ADD instruction that way instead of it being
          * embedded into the code section.
          */

         pvr_pds_clear_rta_vertex_shader_program_init_base(&pds_program,
                                                           multi_layer_vert_bo);
      } else {
         /* We can reuse the device's code section but we'll need to upload data
          * sections so initialize the program.
          */
         pvr_pds_clear_vertex_shader_program_init_base(
            &pds_program,
            cmd_buffer->device->static_clear_state.usc_vertex_shader_bo);

         pds_program_upload.code_offset =
            cmd_buffer->device->static_clear_state.pds.code_offset;
         /* TODO: The code size doesn't get used by pvr_clear_vdm_state() maybe
          * let's change its interface to make that clear and not set this?
          */
         pds_program_upload.code_size =
            cmd_buffer->device->static_clear_state.pds.code_size;
      }

      for (uint32_t j = 0; j < rect_count; j++) {
         struct pvr_pds_upload pds_program_data_upload;
         const VkClearRect *clear_rect = &rects[j];
         struct pvr_bo *vertices_bo;
         uint32_t *vdm_cs_buffer;
         VkResult result;

         if (!PVR_HAS_FEATURE(dev_info, gs_rta_support) &&
             (clear_rect->baseArrayLayer != 0 || clear_rect->layerCount > 1)) {
            pvr_finishme("Add deferred RTA clear.");

            if (clear_rect->baseArrayLayer != 0)
               continue;
         }

         /* TODO: Allocate all the buffers in one go before the loop, and add
          * support to multi-alloc bo.
          */
         result = pvr_clear_vertices_upload(cmd_buffer->device,
                                            &clear_rect->rect,
                                            depth,
                                            &vertices_bo);
         if (result != VK_SUCCESS) {
            cmd_buffer->state.status = result;
            return;
         }

         list_add(&vertices_bo->link, &cmd_buffer->bo_list);

         if (vs_has_rt_id_output) {
            if (current_base_array_layer != clear_rect->baseArrayLayer) {
               const uint32_t base_array_layer = clear_rect->baseArrayLayer;
               struct pvr_pds_upload pds_program_code_upload;

               result =
                  pvr_pds_clear_rta_vertex_shader_program_create_and_upload_code(
                     &pds_program,
                     cmd_buffer,
                     base_array_layer,
                     &pds_program_code_upload);
               if (result != VK_SUCCESS) {
                  cmd_buffer->state.status = result;
                  return;
               }

               pds_program_upload.code_offset =
                  pds_program_code_upload.code_offset;
               /* TODO: The code size doesn't get used by pvr_clear_vdm_state()
                * maybe let's change its interface to make that clear and not
                * set this?
                */
               pds_program_upload.code_size = pds_program_code_upload.code_size;

               current_base_array_layer = base_array_layer;
            }

            result =
               pvr_pds_clear_rta_vertex_shader_program_create_and_upload_data(
                  &pds_program,
                  cmd_buffer,
                  vertices_bo,
                  &pds_program_data_upload);
            if (result != VK_SUCCESS)
               return;
         } else {
            result = pvr_pds_clear_vertex_shader_program_create_and_upload_data(
               &pds_program,
               cmd_buffer,
               vertices_bo,
               &pds_program_data_upload);
            if (result != VK_SUCCESS)
               return;
         }

         pds_program_upload.data_offset = pds_program_data_upload.data_offset;
         pds_program_upload.data_size = pds_program_data_upload.data_size;

         vdm_cs_buffer = pvr_csb_alloc_dwords(&sub_cmd->control_stream,
                                              PVR_CLEAR_VDM_STATE_DWORD_COUNT);
         if (!vdm_cs_buffer) {
            result = vk_error(cmd_buffer, VK_ERROR_OUT_OF_HOST_MEMORY);
            cmd_buffer->state.status = result;
            return;
         }

         pvr_pack_clear_vdm_state(dev_info,
                                  &pds_program_upload,
                                  pds_program.temps_used,
                                  4,
                                  vs_output_size_in_bytes,
                                  clear_rect->layerCount,
                                  vdm_cs_buffer);
      }
   }
}

void pvr_CmdClearAttachments(VkCommandBuffer commandBuffer,
                             uint32_t attachmentCount,
                             const VkClearAttachment *pAttachments,
                             uint32_t rectCount,
                             const VkClearRect *pRects)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_cmd_buffer_state *state = &cmd_buffer->state;
   struct pvr_sub_cmd_gfx *sub_cmd = &state->current_sub_cmd->gfx;

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);
   assert(state->current_sub_cmd->type == PVR_SUB_CMD_TYPE_GRAPHICS);

   /* TODO: There are some optimizations that can be made here:
    *  - For a full screen clear, update the clear values for the corresponding
    *    attachment index.
    *  - For a full screen color attachment clear, add its index to a load op
    *    override to add it to the background shader. This will elide any load
    *    op loads currently in the background shader as well as the usual
    *    frag kick for geometry clear.
    */

   /* If we have any depth/stencil clears, update the sub command depth/stencil
    * modification and usage flags.
    */
   if (state->depth_format != VK_FORMAT_UNDEFINED) {
      uint32_t full_screen_clear_count;
      bool has_stencil_clear = false;
      bool has_depth_clear = false;

      for (uint32_t i = 0; i < attachmentCount; i++) {
         const VkImageAspectFlags aspect_mask = pAttachments[i].aspectMask;

         if (aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT)
            has_stencil_clear = true;

         if (aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
            has_depth_clear = true;

         if (has_stencil_clear && has_depth_clear)
            break;
      }

      sub_cmd->modifies_stencil |= has_stencil_clear;
      sub_cmd->modifies_depth |= has_depth_clear;

      /* We only care about clears that have a baseArrayLayer of 0 as any
       * attachment clears we move to the background shader must apply to all of
       * the attachment's sub resources.
       */
      full_screen_clear_count =
         pvr_get_max_layers_covering_target(state->render_pass_info.render_area,
                                            0,
                                            rectCount,
                                            pRects);

      if (full_screen_clear_count > 0) {
         if (has_stencil_clear &&
             sub_cmd->stencil_usage == PVR_DEPTH_STENCIL_USAGE_UNDEFINED) {
            sub_cmd->stencil_usage = PVR_DEPTH_STENCIL_USAGE_NEVER;
         }

         if (has_depth_clear &&
             sub_cmd->depth_usage == PVR_DEPTH_STENCIL_USAGE_UNDEFINED) {
            sub_cmd->depth_usage = PVR_DEPTH_STENCIL_USAGE_NEVER;
         }
      }
   }

   pvr_clear_attachments(cmd_buffer,
                         attachmentCount,
                         pAttachments,
                         rectCount,
                         pRects);
}

void pvr_CmdResolveImage2KHR(VkCommandBuffer commandBuffer,
                             const VkResolveImageInfo2 *pResolveImageInfo)
{
   assert(!"Unimplemented");
}
