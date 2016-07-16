/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "anv_meta.h"
#include "nir/nir_builder.h"

enum blit2d_src_type {
   /* We can make a "normal" image view of this source and just texture
    * from it like you would in any other shader.
    */
   BLIT2D_SRC_TYPE_NORMAL,

   /* The source is W-tiled and we need to detile manually in the shader.
    * This will work on any platform but is needed for all W-tiled sources
    * prior to Broadwell.
    */
   BLIT2D_SRC_TYPE_W_DETILE,

   BLIT2D_NUM_SRC_TYPES,
};

enum blit2d_dst_type {
   /* We can bind this destination as a "normal" render target and render
    * to it just like you would anywhere else.
    */
   BLIT2D_DST_TYPE_NORMAL,

   /* The destination is W-tiled and we need to do the tiling manually in
    * the shader.  This is required for all W-tiled destinations.
    *
    * Sky Lake adds a feature for providing explicit stencil values in the
    * shader but mesa doesn't support that yet so neither do we.
    */
   BLIT2D_DST_TYPE_W_TILE,

   /* The destination has a 3-channel RGB format.  Since we can't render to
    * non-power-of-two textures, we have to bind it as a red texture and
    * select the correct component for the given red pixel in the shader.
    */
   BLIT2D_DST_TYPE_RGB,

   BLIT2D_NUM_DST_TYPES,
};

static VkFormat
vk_format_for_size(int bs)
{
   /* The choice of UNORM and UINT formats is very intentional here.  Most of
    * the time, we want to use a UINT format to avoid any rounding error in
    * the blit.  For stencil blits, R8_UINT is required by the hardware.
    * (It's the only format allowed in conjunction with W-tiling.)  Also we
    * intentionally use the 4-channel formats whenever we can.  This is so
    * that, when we do a RGB <-> RGBX copy, the two formats will line up even
    * though one of them is 3/4 the size of the other.  The choice of UNORM
    * vs. UINT is also very intentional because Haswell doesn't handle 8 or
    * 16-bit RGB UINT formats at all so we have to use UNORM there.
    * Fortunately, the only time we should ever use two different formats in
    * the table below is for RGB -> RGBA blits and so we will never have any
    * UNORM/UINT mismatch.
    */
   switch (bs) {
   case 1: return VK_FORMAT_R8_UINT;
   case 2: return VK_FORMAT_R8G8_UINT;
   case 3: return VK_FORMAT_R8G8B8_UNORM;
   case 4: return VK_FORMAT_R8G8B8A8_UNORM;
   case 6: return VK_FORMAT_R16G16B16_UNORM;
   case 8: return VK_FORMAT_R16G16B16A16_UNORM;
   case 12: return VK_FORMAT_R32G32B32_UINT;
   case 16: return VK_FORMAT_R32G32B32A32_UINT;
   default:
      unreachable("Invalid format block size");
   }
}

static void
create_iview(struct anv_cmd_buffer *cmd_buffer,
             struct anv_meta_blit2d_surf *surf,
             uint64_t offset,
             VkImageUsageFlags usage,
             uint32_t width,
             uint32_t height,
             VkImage *img,
             struct anv_image_view *iview)
{
   const VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = vk_format_for_size(surf->bs),
      .extent = {
         .width = width,
         .height = height,
         .depth = 1,
      },
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = 1,
      .tiling = surf->tiling == ISL_TILING_LINEAR ?
                VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL,
      .usage = usage,
   };

   /* Create the VkImage that is bound to the surface's memory. */
   anv_image_create(anv_device_to_handle(cmd_buffer->device),
                    &(struct anv_image_create_info) {
                       .vk_info = &image_info,
                       .isl_tiling_flags = 1 << surf->tiling,
                       .stride = surf->pitch,
                    }, &cmd_buffer->pool->alloc, img);

   /* We could use a vk call to bind memory, but that would require
    * creating a dummy memory object etc. so there's really no point.
    */
   anv_image_from_handle(*img)->bo = surf->bo;
   anv_image_from_handle(*img)->offset = surf->base_offset + offset;

   anv_image_view_init(iview, cmd_buffer->device,
                       &(VkImageViewCreateInfo) {
                          .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                          .image = *img,
                          .viewType = VK_IMAGE_VIEW_TYPE_2D,
                          .format = image_info.format,
                          .subresourceRange = {
                             .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1
                          },
                       }, cmd_buffer, usage);
}

struct blit2d_src_temps {
   VkImage image;
   struct anv_image_view iview;

   struct anv_buffer buffer;
   struct anv_buffer_view bview;

   VkDescriptorPool desc_pool;
   VkDescriptorSet set;
};

static void
blit2d_bind_src(struct anv_cmd_buffer *cmd_buffer,
                struct anv_meta_blit2d_surf *src,
                enum blit2d_src_type src_type,
                struct anv_meta_blit2d_rect *rect,
                struct blit2d_src_temps *tmp)
{
   struct anv_device *device = cmd_buffer->device;
   VkDevice vk_device = anv_device_to_handle(cmd_buffer->device);

   if (src_type == BLIT2D_SRC_TYPE_NORMAL) {
      uint32_t offset = 0;
      isl_tiling_get_intratile_offset_el(&cmd_buffer->device->isl_dev,
                                         src->tiling, src->bs, src->pitch,
                                         rect->src_x, rect->src_y,
                                         &offset, &rect->src_x, &rect->src_y);

      create_iview(cmd_buffer, src, offset, VK_IMAGE_USAGE_SAMPLED_BIT,
                   rect->src_x + rect->width, rect->src_y + rect->height,
                   &tmp->image, &tmp->iview);

      anv_CreateDescriptorPool(vk_device,
         &(const VkDescriptorPoolCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .maxSets = 1,
            .poolSizeCount = 1,
            .pPoolSizes = (VkDescriptorPoolSize[]) {
               {
                  .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                  .descriptorCount = 1
               },
            }
         }, &cmd_buffer->pool->alloc, &tmp->desc_pool);

      anv_AllocateDescriptorSets(vk_device,
         &(VkDescriptorSetAllocateInfo) {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = tmp->desc_pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &device->meta_state.blit2d.img_ds_layout
         }, &tmp->set);

      anv_UpdateDescriptorSets(vk_device,
         1, /* writeCount */
         (VkWriteDescriptorSet[]) {
            {
               .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
               .dstSet = tmp->set,
               .dstBinding = 0,
               .dstArrayElement = 0,
               .descriptorCount = 1,
               .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
               .pImageInfo = (VkDescriptorImageInfo[]) {
                  {
                     .sampler = NULL,
                     .imageView = anv_image_view_to_handle(&tmp->iview),
                     .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                  },
               }
            }
         }, 0, NULL);

      anv_CmdBindDescriptorSets(anv_cmd_buffer_to_handle(cmd_buffer),
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                device->meta_state.blit2d.img_p_layout, 0, 1,
                                &tmp->set, 0, NULL);
   } else {
      assert(src_type == BLIT2D_SRC_TYPE_W_DETILE);
      assert(src->tiling == ISL_TILING_W);
      assert(src->bs == 1);

      uint32_t tile_offset = 0;
      isl_tiling_get_intratile_offset_el(&cmd_buffer->device->isl_dev,
                                         ISL_TILING_W, 1, src->pitch,
                                         rect->src_x, rect->src_y,
                                         &tile_offset,
                                         &rect->src_x, &rect->src_y);

      tmp->buffer = (struct anv_buffer) {
         .device = device,
         .size = align_u32(rect->src_y + rect->height, 64) * src->pitch,
         .usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
         .bo = src->bo,
         .offset = src->base_offset + tile_offset,
      };

      anv_buffer_view_init(&tmp->bview, device,
         &(VkBufferViewCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
            .buffer = anv_buffer_to_handle(&tmp->buffer),
            .format = VK_FORMAT_R8_UINT,
            .offset = 0,
            .range = VK_WHOLE_SIZE,
         }, cmd_buffer);

      anv_CreateDescriptorPool(vk_device,
         &(const VkDescriptorPoolCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .maxSets = 1,
            .poolSizeCount = 1,
            .pPoolSizes = (VkDescriptorPoolSize[]) {
               {
                  .type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
                  .descriptorCount = 1
               },
            }
         }, &cmd_buffer->pool->alloc, &tmp->desc_pool);

      anv_AllocateDescriptorSets(vk_device,
         &(VkDescriptorSetAllocateInfo) {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = tmp->desc_pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &device->meta_state.blit2d.buf_ds_layout
         }, &tmp->set);

      anv_UpdateDescriptorSets(vk_device,
         1, /* writeCount */
         (VkWriteDescriptorSet[]) {
            {
               .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
               .dstSet = tmp->set,
               .dstBinding = 0,
               .dstArrayElement = 0,
               .descriptorCount = 1,
               .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
               .pTexelBufferView = (VkBufferView[]) {
                  anv_buffer_view_to_handle(&tmp->bview),
               },
            }
         }, 0, NULL);

      anv_CmdBindDescriptorSets(anv_cmd_buffer_to_handle(cmd_buffer),
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                device->meta_state.blit2d.buf_p_layout, 0, 1,
                                &tmp->set, 0, NULL);
   }
}

static void
blit2d_unbind_src(struct anv_cmd_buffer *cmd_buffer,
                  enum blit2d_src_type src_type,
                  struct blit2d_src_temps *tmp)
{
   anv_DestroyDescriptorPool(anv_device_to_handle(cmd_buffer->device),
                             tmp->desc_pool, &cmd_buffer->pool->alloc);
   if (src_type == BLIT2D_SRC_TYPE_NORMAL) {
      anv_DestroyImage(anv_device_to_handle(cmd_buffer->device),
                       tmp->image, &cmd_buffer->pool->alloc);
   }
}

struct blit2d_dst_temps {
   VkImage image;
   struct anv_image_view iview;
   VkFramebuffer fb;
};

static void
blit2d_bind_dst(struct anv_cmd_buffer *cmd_buffer,
                struct anv_meta_blit2d_surf *dst,
                uint64_t offset,
                uint32_t width,
                uint32_t height,
                struct blit2d_dst_temps *tmp)
{
   create_iview(cmd_buffer, dst, offset, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                width, height, &tmp->image, &tmp->iview);

   anv_CreateFramebuffer(anv_device_to_handle(cmd_buffer->device),
      &(VkFramebufferCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
         .attachmentCount = 1,
         .pAttachments = (VkImageView[]) {
            anv_image_view_to_handle(&tmp->iview),
         },
         .width = width,
         .height = height,
         .layers = 1
      }, &cmd_buffer->pool->alloc, &tmp->fb);
}

static void
blit2d_unbind_dst(struct anv_cmd_buffer *cmd_buffer,
                  struct blit2d_dst_temps *tmp)
{
   VkDevice vk_device = anv_device_to_handle(cmd_buffer->device);
   anv_DestroyFramebuffer(vk_device, tmp->fb, &cmd_buffer->pool->alloc);
   anv_DestroyImage(vk_device, tmp->image, &cmd_buffer->pool->alloc);
}

void
anv_meta_end_blit2d(struct anv_cmd_buffer *cmd_buffer,
                    struct anv_meta_saved_state *save)
{
   anv_meta_restore(save, cmd_buffer);
}

void
anv_meta_begin_blit2d(struct anv_cmd_buffer *cmd_buffer,
                      struct anv_meta_saved_state *save)
{
   anv_meta_save(save, cmd_buffer, 0);
}

static void
bind_pipeline(struct anv_cmd_buffer *cmd_buffer,
              enum blit2d_src_type src_type,
              enum blit2d_dst_type dst_type)
{
   VkPipeline pipeline =
      cmd_buffer->device->meta_state.blit2d.pipelines[src_type][dst_type];

   if (cmd_buffer->state.pipeline != anv_pipeline_from_handle(pipeline)) {
      anv_CmdBindPipeline(anv_cmd_buffer_to_handle(cmd_buffer),
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
   }
}

static void
anv_meta_blit2d_normal_dst(struct anv_cmd_buffer *cmd_buffer,
                           struct anv_meta_blit2d_surf *src,
                           enum blit2d_src_type src_type,
                           struct anv_meta_blit2d_surf *dst,
                           unsigned num_rects,
                           struct anv_meta_blit2d_rect *rects)
{
   struct anv_device *device = cmd_buffer->device;

   for (unsigned r = 0; r < num_rects; ++r) {
      struct blit2d_src_temps src_temps;
      blit2d_bind_src(cmd_buffer, src, src_type, &rects[r], &src_temps);

      uint32_t offset = 0;
      isl_tiling_get_intratile_offset_el(&cmd_buffer->device->isl_dev,
                                         dst->tiling, dst->bs, dst->pitch,
                                         rects[r].dst_x, rects[r].dst_y,
                                         &offset,
                                         &rects[r].dst_x, &rects[r].dst_y);

      struct blit2d_dst_temps dst_temps;
      blit2d_bind_dst(cmd_buffer, dst, offset, rects[r].dst_x + rects[r].width,
                      rects[r].dst_y + rects[r].height, &dst_temps);

      struct blit_vb_data {
         float pos[2];
         float tex_coord[3];
      } *vb_data;

      unsigned vb_size = sizeof(struct anv_vue_header) + 3 * sizeof(*vb_data);

      struct anv_state vb_state =
         anv_cmd_buffer_alloc_dynamic_state(cmd_buffer, vb_size, 16);
      memset(vb_state.map, 0, sizeof(struct anv_vue_header));
      vb_data = vb_state.map + sizeof(struct anv_vue_header);

      vb_data[0] = (struct blit_vb_data) {
         .pos = {
            rects[r].dst_x + rects[r].width,
            rects[r].dst_y + rects[r].height,
         },
         .tex_coord = {
            rects[r].src_x + rects[r].width,
            rects[r].src_y + rects[r].height,
            src->pitch,
         },
      };

      vb_data[1] = (struct blit_vb_data) {
         .pos = {
            rects[r].dst_x,
            rects[r].dst_y + rects[r].height,
         },
         .tex_coord = {
            rects[r].src_x,
            rects[r].src_y + rects[r].height,
            src->pitch,
         },
      };

      vb_data[2] = (struct blit_vb_data) {
         .pos = {
            rects[r].dst_x,
            rects[r].dst_y,
         },
         .tex_coord = {
            rects[r].src_x,
            rects[r].src_y,
            src->pitch,
         },
      };

      if (!device->info.has_llc)
         anv_state_clflush(vb_state);

      struct anv_buffer vertex_buffer = {
         .device = device,
         .size = vb_size,
         .bo = &device->dynamic_state_block_pool.bo,
         .offset = vb_state.offset,
      };

      anv_CmdBindVertexBuffers(anv_cmd_buffer_to_handle(cmd_buffer), 0, 2,
         (VkBuffer[]) {
            anv_buffer_to_handle(&vertex_buffer),
            anv_buffer_to_handle(&vertex_buffer)
         },
         (VkDeviceSize[]) {
            0,
            sizeof(struct anv_vue_header),
         });

      ANV_CALL(CmdBeginRenderPass)(anv_cmd_buffer_to_handle(cmd_buffer),
         &(VkRenderPassBeginInfo) {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = device->meta_state.blit2d.render_pass,
            .framebuffer = dst_temps.fb,
            .renderArea = {
               .offset = { rects[r].dst_x, rects[r].dst_y, },
               .extent = { rects[r].width, rects[r].height },
            },
            .clearValueCount = 0,
            .pClearValues = NULL,
         }, VK_SUBPASS_CONTENTS_INLINE);

      bind_pipeline(cmd_buffer, src_type, BLIT2D_DST_TYPE_NORMAL);

      ANV_CALL(CmdDraw)(anv_cmd_buffer_to_handle(cmd_buffer), 3, 1, 0, 0);

      ANV_CALL(CmdEndRenderPass)(anv_cmd_buffer_to_handle(cmd_buffer));

      /* At the point where we emit the draw call, all data from the
       * descriptor sets, etc. has been used.  We are free to delete it.
       */
      blit2d_unbind_src(cmd_buffer, src_type, &src_temps);
      blit2d_unbind_dst(cmd_buffer, &dst_temps);
   }
}

static void
anv_meta_blit2d_w_tiled_dst(struct anv_cmd_buffer *cmd_buffer,
                            struct anv_meta_blit2d_surf *src,
                            enum blit2d_src_type src_type,
                            struct anv_meta_blit2d_surf *dst,
                            unsigned num_rects,
                            struct anv_meta_blit2d_rect *rects)
{
   struct anv_device *device = cmd_buffer->device;

   for (unsigned r = 0; r < num_rects; ++r) {
      struct blit2d_src_temps src_temps;
      blit2d_bind_src(cmd_buffer, src, src_type, &rects[r], &src_temps);

      assert(dst->bs == 1);
      uint32_t offset;
      isl_tiling_get_intratile_offset_el(&cmd_buffer->device->isl_dev,
                                         ISL_TILING_W, 1, dst->pitch,
                                         rects[r].dst_x, rects[r].dst_y,
                                         &offset,
                                         &rects[r].dst_x, &rects[r].dst_y);

      /* The original coordinates were in terms of an actual W-tiled offset
       * but we are binding this image as Y-tiled.  We need to adjust our
       * rectangle accordingly.
       */
      uint32_t xmin_Y, xmax_Y, ymin_Y, ymax_Y;
      xmin_Y = (rects[r].dst_x / 8) * 16;
      xmax_Y = DIV_ROUND_UP(rects[r].dst_x + rects[r].width, 8) * 16;
      ymin_Y = (rects[r].dst_y / 4) * 2;
      ymax_Y = DIV_ROUND_UP(rects[r].dst_y + rects[r].height, 4) * 2;

      struct anv_meta_blit2d_surf dst_Y = {
         .bo = dst->bo,
         .tiling = ISL_TILING_Y0,
         .base_offset = dst->base_offset,
         .bs = 1,
         .pitch = dst->pitch * 2,
      };

      struct blit2d_dst_temps dst_temps;
      blit2d_bind_dst(cmd_buffer, &dst_Y, offset, xmax_Y, ymax_Y, &dst_temps);

      struct blit_vb_header {
         struct anv_vue_header vue;
         int32_t tex_offset[2];
         uint32_t tex_pitch;
         uint32_t bounds[4];
      } *vb_header;

      struct blit_vb_data {
         float pos[2];
      } *vb_data;

      unsigned vb_size = sizeof(*vb_header) + 3 * sizeof(*vb_data);

      struct anv_state vb_state =
         anv_cmd_buffer_alloc_dynamic_state(cmd_buffer, vb_size, 16);
      vb_header = vb_state.map;

      *vb_header = (struct blit_vb_header) {
         .tex_offset = {
            rects[r].src_x - rects[r].dst_x,
            rects[r].src_y - rects[r].dst_y,
         },
         .tex_pitch = src->pitch,
         .bounds = {
            rects[r].dst_x,
            rects[r].dst_y,
            rects[r].dst_x + rects[r].width,
            rects[r].dst_y + rects[r].height,
         },
      };

      vb_data = (void *)(vb_header + 1);

      vb_data[0] = (struct blit_vb_data) {
         .pos = {
            xmax_Y,
            ymax_Y,
         },
      };

      vb_data[1] = (struct blit_vb_data) {
         .pos = {
            xmin_Y,
            ymax_Y,
         },
      };

      vb_data[2] = (struct blit_vb_data) {
         .pos = {
            xmin_Y,
            ymin_Y,
         },
      };

      if (!device->info.has_llc)
         anv_state_clflush(vb_state);

      struct anv_buffer vertex_buffer = {
         .device = device,
         .size = vb_size,
         .bo = &device->dynamic_state_block_pool.bo,
         .offset = vb_state.offset,
      };

      anv_CmdBindVertexBuffers(anv_cmd_buffer_to_handle(cmd_buffer), 0, 2,
         (VkBuffer[]) {
            anv_buffer_to_handle(&vertex_buffer),
            anv_buffer_to_handle(&vertex_buffer)
         },
         (VkDeviceSize[]) {
            0,
            (void *)vb_data - vb_state.map,
         });

      ANV_CALL(CmdBeginRenderPass)(anv_cmd_buffer_to_handle(cmd_buffer),
         &(VkRenderPassBeginInfo) {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = device->meta_state.blit2d.render_pass,
            .framebuffer = dst_temps.fb,
            .renderArea = {
               .offset = { xmin_Y, ymin_Y, },
               .extent = { xmax_Y - xmin_Y, ymax_Y - ymin_Y },
            },
            .clearValueCount = 0,
            .pClearValues = NULL,
         }, VK_SUBPASS_CONTENTS_INLINE);

      bind_pipeline(cmd_buffer, src_type, BLIT2D_DST_TYPE_W_TILE);

      ANV_CALL(CmdDraw)(anv_cmd_buffer_to_handle(cmd_buffer), 3, 1, 0, 0);

      ANV_CALL(CmdEndRenderPass)(anv_cmd_buffer_to_handle(cmd_buffer));

      /* At the point where we emit the draw call, all data from the
       * descriptor sets, etc. has been used.  We are free to delete it.
       */
      blit2d_unbind_src(cmd_buffer, src_type, &src_temps);
      blit2d_unbind_dst(cmd_buffer, &dst_temps);
   }
}

void
anv_meta_blit2d(struct anv_cmd_buffer *cmd_buffer,
                struct anv_meta_blit2d_surf *src,
                struct anv_meta_blit2d_surf *dst,
                unsigned num_rects,
                struct anv_meta_blit2d_rect *rects)
{
   enum blit2d_src_type src_type;
   if (src->tiling == ISL_TILING_W && cmd_buffer->device->info.gen < 8) {
      src_type = BLIT2D_SRC_TYPE_W_DETILE;
   } else {
      src_type = BLIT2D_SRC_TYPE_NORMAL;
   }

   if (dst->tiling == ISL_TILING_W) {
      anv_meta_blit2d_w_tiled_dst(cmd_buffer, src, src_type, dst,
                                  num_rects, rects);
      return;
   } else if (dst->bs % 3 == 0) {
      anv_finishme("Blitting to RGB destinations not yet supported");
      return;
   } else {
      assert(util_is_power_of_two(dst->bs));
      anv_meta_blit2d_normal_dst(cmd_buffer, src, src_type, dst,
                                 num_rects, rects);
   }
}

static nir_shader *
build_nir_vertex_shader(void)
{
   const struct glsl_type *vec4 = glsl_vec4_type();
   nir_builder b;

   nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_VERTEX, NULL);
   b.shader->info.name = ralloc_strdup(b.shader, "meta_blit_vs");

   nir_variable *pos_in = nir_variable_create(b.shader, nir_var_shader_in,
                                              vec4, "a_pos");
   pos_in->data.location = VERT_ATTRIB_GENERIC0;
   nir_variable *pos_out = nir_variable_create(b.shader, nir_var_shader_out,
                                               vec4, "gl_Position");
   pos_out->data.location = VARYING_SLOT_POS;
   nir_copy_var(&b, pos_out, pos_in);

   nir_variable *tex_pos_in = nir_variable_create(b.shader, nir_var_shader_in,
                                                  vec4, "a_tex_pos");
   tex_pos_in->data.location = VERT_ATTRIB_GENERIC1;
   nir_variable *tex_pos_out = nir_variable_create(b.shader, nir_var_shader_out,
                                                   vec4, "v_tex_pos");
   tex_pos_out->data.location = VARYING_SLOT_VAR0;
   tex_pos_out->data.interpolation = INTERP_QUALIFIER_SMOOTH;
   nir_copy_var(&b, tex_pos_out, tex_pos_in);

   nir_variable *other_in = nir_variable_create(b.shader, nir_var_shader_in,
                                                vec4, "a_other");
   other_in->data.location = VERT_ATTRIB_GENERIC2;
   nir_variable *other_out = nir_variable_create(b.shader, nir_var_shader_out,
                                                   vec4, "v_other");
   other_out->data.location = VARYING_SLOT_VAR1;
   other_out->data.interpolation = INTERP_QUALIFIER_FLAT;
   nir_copy_var(&b, other_out, other_in);

   return b.shader;
}

typedef nir_ssa_def* (*texel_fetch_build_func)(struct nir_builder *,
                                               struct anv_device *,
                                               nir_ssa_def *, nir_ssa_def *);

static nir_ssa_def *
nir_copy_bits(struct nir_builder *b, nir_ssa_def *dst, unsigned dst_offset,
              nir_ssa_def *src, unsigned src_offset, unsigned num_bits)
{
   unsigned src_mask = (~1u >> (32 - num_bits)) << src_offset;
   nir_ssa_def *masked = nir_iand(b, src, nir_imm_int(b, src_mask));

   nir_ssa_def *shifted;
   if (dst_offset > src_offset) {
      shifted = nir_ishl(b, masked, nir_imm_int(b, dst_offset - src_offset));
   } else if (dst_offset < src_offset) {
      shifted = nir_ushr(b, masked, nir_imm_int(b, src_offset - dst_offset));
   } else {
      assert(dst_offset == src_offset);
      shifted = masked;
   }

   return nir_ior(b, dst, shifted);
}

static nir_ssa_def *
build_nir_w_tiled_fetch(struct nir_builder *b, struct anv_device *device,
                        nir_ssa_def *tex_pos, nir_ssa_def *tex_pitch)
{
   nir_ssa_def *x = nir_channel(b, tex_pos, 0);
   nir_ssa_def *y = nir_channel(b, tex_pos, 1);

   /* First, compute the block-aligned offset */
   nir_ssa_def *x_major = nir_ushr(b, x, nir_imm_int(b, 6));
   nir_ssa_def *y_major = nir_ushr(b, y, nir_imm_int(b, 6));
   nir_ssa_def *offset =
      nir_iadd(b, nir_imul(b, y_major,
                              nir_imul(b, tex_pitch, nir_imm_int(b, 64))),
                  nir_imul(b, x_major, nir_imm_int(b, 4096)));

   /* Compute the bottom 12 bits of the offset */
   offset = nir_copy_bits(b, offset, 0, x, 0, 1);
   offset = nir_copy_bits(b, offset, 1, y, 0, 1);
   offset = nir_copy_bits(b, offset, 2, x, 1, 1);
   offset = nir_copy_bits(b, offset, 3, y, 1, 1);
   offset = nir_copy_bits(b, offset, 4, x, 2, 1);
   offset = nir_copy_bits(b, offset, 5, y, 2, 4);
   offset = nir_copy_bits(b, offset, 9, x, 3, 3);

   if (device->isl_dev.has_bit6_swizzling) {
      offset = nir_ixor(b, offset,
                        nir_ushr(b, nir_iand(b, offset, nir_imm_int(b, 0x0200)),
                                 nir_imm_int(b, 3)));
   }

   const struct glsl_type *sampler_type =
      glsl_sampler_type(GLSL_SAMPLER_DIM_BUF, false, false, GLSL_TYPE_FLOAT);
   nir_variable *sampler = nir_variable_create(b->shader, nir_var_uniform,
                                               sampler_type, "s_tex");
   sampler->data.descriptor_set = 0;
   sampler->data.binding = 0;

   nir_tex_instr *tex = nir_tex_instr_create(b->shader, 1);
   tex->sampler_dim = GLSL_SAMPLER_DIM_BUF;
   tex->op = nir_texop_txf;
   tex->src[0].src_type = nir_tex_src_coord;
   tex->src[0].src = nir_src_for_ssa(offset);
   tex->dest_type = nir_type_float; /* TODO */
   tex->is_array = false;
   tex->coord_components = 1;
   tex->texture = nir_deref_var_create(tex, sampler);
   tex->sampler = NULL;

   nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
   nir_builder_instr_insert(b, &tex->instr);

   return &tex->dest.ssa;
}

static nir_ssa_def *
build_nir_texel_fetch(struct nir_builder *b, struct anv_device *device,
                      nir_ssa_def *tex_pos, nir_ssa_def *tex_pitch)
{
   const struct glsl_type *sampler_type =
      glsl_sampler_type(GLSL_SAMPLER_DIM_2D, false, false, GLSL_TYPE_FLOAT);
   nir_variable *sampler = nir_variable_create(b->shader, nir_var_uniform,
                                               sampler_type, "s_tex");
   sampler->data.descriptor_set = 0;
   sampler->data.binding = 0;

   nir_tex_instr *tex = nir_tex_instr_create(b->shader, 2);
   tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
   tex->op = nir_texop_txf;
   tex->src[0].src_type = nir_tex_src_coord;
   tex->src[0].src = nir_src_for_ssa(tex_pos);
   tex->src[1].src_type = nir_tex_src_lod;
   tex->src[1].src = nir_src_for_ssa(nir_imm_int(b, 0));
   tex->dest_type = nir_type_float; /* TODO */
   tex->is_array = false;
   tex->coord_components = 2;
   tex->texture = nir_deref_var_create(tex, sampler);
   tex->sampler = NULL;

   nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
   nir_builder_instr_insert(b, &tex->instr);

   return &tex->dest.ssa;
}

static const VkPipelineVertexInputStateCreateInfo normal_vi_create_info = {
   .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
   .vertexBindingDescriptionCount = 2,
   .pVertexBindingDescriptions = (VkVertexInputBindingDescription[]) {
      {
         .binding = 0,
         .stride = 0,
         .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
      },
      {
         .binding = 1,
         .stride = 5 * sizeof(float),
         .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
      },
   },
   .vertexAttributeDescriptionCount = 3,
   .pVertexAttributeDescriptions = (VkVertexInputAttributeDescription[]) {
      {
         /* VUE Header */
         .location = 0,
         .binding = 0,
         .format = VK_FORMAT_R32G32B32A32_UINT,
         .offset = 0
      },
      {
         /* Position */
         .location = 1,
         .binding = 1,
         .format = VK_FORMAT_R32G32_SFLOAT,
         .offset = 0
      },
      {
         /* Texture Coordinate */
         .location = 2,
         .binding = 1,
         .format = VK_FORMAT_R32G32B32_SFLOAT,
         .offset = 8
      },
   },
};

static nir_shader *
build_nir_copy_fragment_shader(struct anv_device *device,
                               texel_fetch_build_func txf_func)
{
   const struct glsl_type *vec4 = glsl_vec4_type();
   const struct glsl_type *vec3 = glsl_vector_type(GLSL_TYPE_FLOAT, 3);
   nir_builder b;

   nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_FRAGMENT, NULL);
   b.shader->info.name = ralloc_strdup(b.shader, "meta_blit2d_fs");

   nir_variable *tex_pos_in = nir_variable_create(b.shader, nir_var_shader_in,
                                                  vec3, "v_tex_pos");
   tex_pos_in->data.location = VARYING_SLOT_VAR0;

   nir_variable *color_out = nir_variable_create(b.shader, nir_var_shader_out,
                                                 vec4, "f_color");
   color_out->data.location = FRAG_RESULT_DATA0;

   nir_ssa_def *pos_int = nir_f2i(&b, nir_load_var(&b, tex_pos_in));
   unsigned swiz[4] = { 0, 1 };
   nir_ssa_def *tex_pos = nir_swizzle(&b, pos_int, swiz, 2, false);
   nir_ssa_def *tex_pitch = nir_channel(&b, pos_int, 2);

   nir_ssa_def *color = txf_func(&b, device, tex_pos, tex_pitch);
   nir_store_var(&b, color_out, color, 0xf);

   return b.shader;
}

static const VkPipelineVertexInputStateCreateInfo w_tiled_vi_create_info = {
   .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
   .vertexBindingDescriptionCount = 2,
   .pVertexBindingDescriptions = (VkVertexInputBindingDescription[]) {
      {
         .binding = 0,
         .stride = 0,
         .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
      },
      {
         .binding = 1,
         .stride = 2 * sizeof(float),
         .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
      },
   },
   .vertexAttributeDescriptionCount = 4,
   .pVertexAttributeDescriptions = (VkVertexInputAttributeDescription[]) {
      {
         /* VUE Header */
         .location = 0,
         .binding = 0,
         .format = VK_FORMAT_R32G32B32A32_UINT,
         .offset = 0
      },
      {
         /* Position */
         .location = 1,
         .binding = 1,
         .format = VK_FORMAT_R32G32_SFLOAT,
         .offset = 0
      },
      {
         /* Texture Offset */
         .location = 2,
         .binding = 0,
         .format = VK_FORMAT_R32G32B32_UINT,
         .offset = 16
      },
      {
         /* Destination bounds */
         .location = 3,
         .binding = 0,
         .format = VK_FORMAT_R32G32B32A32_UINT,
         .offset = 28
      },
   },
};

static nir_shader *
build_nir_w_tiled_fragment_shader(struct anv_device *device,
                                  texel_fetch_build_func txf_func)
{
   const struct glsl_type *vec4 = glsl_vec4_type();
   const struct glsl_type *ivec3 = glsl_vector_type(GLSL_TYPE_INT, 3);
   const struct glsl_type *uvec4 = glsl_vector_type(GLSL_TYPE_UINT, 4);
   nir_builder b;

   nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_FRAGMENT, NULL);
   b.shader->info.name = ralloc_strdup(b.shader, "meta_blit2d_fs");

   /* We need gl_FragCoord so we know our Y-tiled position */
   nir_variable *frag_coord_in = nir_variable_create(b.shader,
                                                     nir_var_shader_in,
                                                     vec4, "gl_FragCoord");
   frag_coord_in->data.location = VARYING_SLOT_POS;
   frag_coord_in->data.origin_upper_left = true;

   /* In location 0 we have an ivec3 that has the offset from dest to
    * source in the first two components and the stride in the third.
    */
   nir_variable *tex_off_in = nir_variable_create(b.shader, nir_var_shader_in,
                                                  ivec3, "v_tex_off");
   tex_off_in->data.location = VARYING_SLOT_VAR0;
   tex_off_in->data.interpolation = INTERP_QUALIFIER_FLAT;

   /* In location 1 we have a uvec4 that gives us the bounds of the
    * destination.  We need to discard if we get outside this boundary.
    */
   nir_variable *bounds_in = nir_variable_create(b.shader, nir_var_shader_in,
                                                 uvec4, "v_bounds");
   bounds_in->data.location = VARYING_SLOT_VAR1;
   bounds_in->data.interpolation = INTERP_QUALIFIER_FLAT;

   nir_variable *color_out = nir_variable_create(b.shader, nir_var_shader_out,
                                                 vec4, "f_color");
   color_out->data.location = FRAG_RESULT_DATA0;

   nir_ssa_def *frag_coord_int = nir_f2i(&b, nir_load_var(&b, frag_coord_in));
   nir_ssa_def *x_Y = nir_channel(&b, frag_coord_int, 0);
   nir_ssa_def *y_Y = nir_channel(&b, frag_coord_int, 1);

   /* Compute the W-tiled position from the Y-tiled position */
   nir_ssa_def *x_W = nir_iand(&b, x_Y, nir_imm_int(&b, 0xffffff80));
   x_W = nir_ushr(&b, x_W, nir_imm_int(&b, 1));
   x_W = nir_copy_bits(&b, x_W, 0, x_Y, 0, 1);
   x_W = nir_copy_bits(&b, x_W, 1, x_Y, 2, 1);
   x_W = nir_copy_bits(&b, x_W, 2, y_Y, 0, 1);
   x_W = nir_copy_bits(&b, x_W, 3, x_Y, 4, 3);

   nir_ssa_def *y_W = nir_iand(&b, y_Y, nir_imm_int(&b, 0xffffffe0));
   y_W = nir_ishl(&b, y_W, nir_imm_int(&b, 1));
   y_W = nir_copy_bits(&b, y_W, 0, x_Y, 1, 1);
   y_W = nir_copy_bits(&b, y_W, 1, x_Y, 3, 1);
   y_W = nir_copy_bits(&b, y_W, 2, y_Y, 1, 4);

   /* Figure out if we are out-of-bounds and discard */
   nir_ssa_def *bounds = nir_load_var(&b, bounds_in);
   nir_ssa_def *oob =
      nir_ior(&b, nir_ult(&b, x_W, nir_channel(&b, bounds, 0)),
      nir_ior(&b, nir_ult(&b, y_W, nir_channel(&b, bounds, 1)),
      nir_ior(&b, nir_uge(&b, x_W, nir_channel(&b, bounds, 2)),
                  nir_uge(&b, y_W, nir_channel(&b, bounds, 3)))));

   nir_intrinsic_instr *discard =
      nir_intrinsic_instr_create(b.shader, nir_intrinsic_discard_if);
   discard->src[0] = nir_src_for_ssa(oob);
   nir_builder_instr_insert(&b, &discard->instr);

   nir_ssa_def *tex_off = nir_channels(&b, nir_load_var(&b, tex_off_in), 0x3);
   nir_ssa_def *tex_pos = nir_iadd(&b, nir_vec2(&b, x_W, y_W), tex_off);
   nir_ssa_def *tex_pitch = nir_channel(&b, nir_load_var(&b, tex_off_in), 2);

   nir_ssa_def *color = txf_func(&b, device, tex_pos, tex_pitch);
   nir_store_var(&b, color_out, color, 0xf);

   return b.shader;
}

void
anv_device_finish_meta_blit2d_state(struct anv_device *device)
{
   if (device->meta_state.blit2d.render_pass) {
      anv_DestroyRenderPass(anv_device_to_handle(device),
                            device->meta_state.blit2d.render_pass,
                            &device->meta_state.alloc);
   }

   if (device->meta_state.blit2d.img_p_layout) {
      anv_DestroyPipelineLayout(anv_device_to_handle(device),
                                device->meta_state.blit2d.img_p_layout,
                                &device->meta_state.alloc);
   }

   if (device->meta_state.blit2d.img_ds_layout) {
      anv_DestroyDescriptorSetLayout(anv_device_to_handle(device),
                                     device->meta_state.blit2d.img_ds_layout,
                                     &device->meta_state.alloc);
   }

   if (device->meta_state.blit2d.buf_p_layout) {
      anv_DestroyPipelineLayout(anv_device_to_handle(device),
                                device->meta_state.blit2d.buf_p_layout,
                                &device->meta_state.alloc);
   }

   if (device->meta_state.blit2d.buf_ds_layout) {
      anv_DestroyDescriptorSetLayout(anv_device_to_handle(device),
                                     device->meta_state.blit2d.buf_ds_layout,
                                     &device->meta_state.alloc);
   }

   for (unsigned src = 0; src < BLIT2D_NUM_SRC_TYPES; src++) {
      for (unsigned dst = 0; dst < BLIT2D_NUM_DST_TYPES; dst++) {
         if (device->meta_state.blit2d.pipelines[src][dst]) {
            anv_DestroyPipeline(anv_device_to_handle(device),
                                device->meta_state.blit2d.pipelines[src][dst],
                                &device->meta_state.alloc);
         }
      }
   }
}

static VkResult
blit2d_init_pipeline(struct anv_device *device,
                     enum blit2d_src_type src_type,
                     enum blit2d_dst_type dst_type)
{
   VkResult result;

   texel_fetch_build_func src_func;
   switch (src_type) {
   case BLIT2D_SRC_TYPE_NORMAL:
      src_func = build_nir_texel_fetch;
      break;
   case BLIT2D_SRC_TYPE_W_DETILE:
      src_func = build_nir_w_tiled_fetch;
      break;
   default:
      unreachable("Invalid blit2d source type");
   }

   const VkPipelineVertexInputStateCreateInfo *vi_create_info;
   struct anv_shader_module fs = { .nir = NULL };
   switch (dst_type) {
   case BLIT2D_DST_TYPE_NORMAL:
      fs.nir = build_nir_copy_fragment_shader(device, src_func);
      vi_create_info = &normal_vi_create_info;
      break;
   case BLIT2D_DST_TYPE_W_TILE:
      fs.nir = build_nir_w_tiled_fragment_shader(device, src_func);
      vi_create_info = &w_tiled_vi_create_info;
      break;
   case BLIT2D_DST_TYPE_RGB:
      /* Not yet supported */
   default:
      return VK_SUCCESS;
   }

   /* We don't use a vertex shader for blitting, but instead build and pass
    * the VUEs directly to the rasterization backend.  However, we do need
    * to provide GLSL source for the vertex shader so that the compiler
    * does not dead-code our inputs.
    */
   struct anv_shader_module vs = {
      .nir = build_nir_vertex_shader(),
   };

   VkPipelineShaderStageCreateInfo pipeline_shader_stages[] = {
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = anv_shader_module_to_handle(&vs),
         .pName = "main",
         .pSpecializationInfo = NULL
      }, {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = anv_shader_module_to_handle(&fs),
         .pName = "main",
         .pSpecializationInfo = NULL
      },
   };

   const VkGraphicsPipelineCreateInfo vk_pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = ARRAY_SIZE(pipeline_shader_stages),
      .pStages = pipeline_shader_stages,
      .pVertexInputState = vi_create_info,
      .pInputAssemblyState = &(VkPipelineInputAssemblyStateCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
         .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
         .primitiveRestartEnable = false,
      },
      .pViewportState = &(VkPipelineViewportStateCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
         .viewportCount = 1,
         .scissorCount = 1,
      },
      .pRasterizationState = &(VkPipelineRasterizationStateCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
         .rasterizerDiscardEnable = false,
         .polygonMode = VK_POLYGON_MODE_FILL,
         .cullMode = VK_CULL_MODE_NONE,
         .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE
      },
      .pMultisampleState = &(VkPipelineMultisampleStateCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
         .rasterizationSamples = 1,
         .sampleShadingEnable = false,
         .pSampleMask = (VkSampleMask[]) { UINT32_MAX },
      },
      .pColorBlendState = &(VkPipelineColorBlendStateCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
         .attachmentCount = 1,
         .pAttachments = (VkPipelineColorBlendAttachmentState []) {
            { .colorWriteMask =
                 VK_COLOR_COMPONENT_A_BIT |
                 VK_COLOR_COMPONENT_R_BIT |
                 VK_COLOR_COMPONENT_G_BIT |
                 VK_COLOR_COMPONENT_B_BIT },
         }
      },
      .pDynamicState = &(VkPipelineDynamicStateCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
         .dynamicStateCount = 9,
         .pDynamicStates = (VkDynamicState[]) {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_LINE_WIDTH,
            VK_DYNAMIC_STATE_DEPTH_BIAS,
            VK_DYNAMIC_STATE_BLEND_CONSTANTS,
            VK_DYNAMIC_STATE_DEPTH_BOUNDS,
            VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
            VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
         },
      },
      .flags = 0,
      .layout = device->meta_state.blit2d.img_p_layout,
      .renderPass = device->meta_state.blit2d.render_pass,
      .subpass = 0,
   };

   const struct anv_graphics_pipeline_create_info anv_pipeline_info = {
      .color_attachment_count = -1,
      .use_repclear = false,
      .disable_vs = true,
      .use_rectlist = true
   };

   result = anv_graphics_pipeline_create(anv_device_to_handle(device),
      VK_NULL_HANDLE,
      &vk_pipeline_info, &anv_pipeline_info,
      &device->meta_state.alloc,
      &device->meta_state.blit2d.pipelines[src_type][dst_type]);

   ralloc_free(vs.nir);
   ralloc_free(fs.nir);

   return result;
}

VkResult
anv_device_init_meta_blit2d_state(struct anv_device *device)
{
   VkResult result;

   zero(device->meta_state.blit2d);

   result = anv_CreateRenderPass(anv_device_to_handle(device),
      &(VkRenderPassCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
         .attachmentCount = 1,
         .pAttachments = &(VkAttachmentDescription) {
            .format = VK_FORMAT_UNDEFINED, /* Our shaders don't care */
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .initialLayout = VK_IMAGE_LAYOUT_GENERAL,
            .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
         },
         .subpassCount = 1,
         .pSubpasses = &(VkSubpassDescription) {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .inputAttachmentCount = 0,
            .colorAttachmentCount = 1,
            .pColorAttachments = &(VkAttachmentReference) {
               .attachment = 0,
               .layout = VK_IMAGE_LAYOUT_GENERAL,
            },
            .pResolveAttachments = NULL,
            .pDepthStencilAttachment = &(VkAttachmentReference) {
               .attachment = VK_ATTACHMENT_UNUSED,
               .layout = VK_IMAGE_LAYOUT_GENERAL,
            },
            .preserveAttachmentCount = 1,
            .pPreserveAttachments = (uint32_t[]) { 0 },
         },
         .dependencyCount = 0,
      }, &device->meta_state.alloc, &device->meta_state.blit2d.render_pass);
   if (result != VK_SUCCESS)
      goto fail;

   result = anv_CreateDescriptorSetLayout(anv_device_to_handle(device),
      &(VkDescriptorSetLayoutCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
         .bindingCount = 1,
         .pBindings = (VkDescriptorSetLayoutBinding[]) {
            {
               .binding = 0,
               .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
               .descriptorCount = 1,
               .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
               .pImmutableSamplers = NULL
            },
         }
      }, &device->meta_state.alloc, &device->meta_state.blit2d.img_ds_layout);
   if (result != VK_SUCCESS)
      goto fail;

   result = anv_CreatePipelineLayout(anv_device_to_handle(device),
      &(VkPipelineLayoutCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
         .setLayoutCount = 1,
         .pSetLayouts = &device->meta_state.blit2d.img_ds_layout,
      },
      &device->meta_state.alloc, &device->meta_state.blit2d.img_p_layout);
   if (result != VK_SUCCESS)
      goto fail;

   result = anv_CreateDescriptorSetLayout(anv_device_to_handle(device),
      &(VkDescriptorSetLayoutCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
         .bindingCount = 1,
         .pBindings = (VkDescriptorSetLayoutBinding[]) {
            {
               .binding = 0,
               .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
               .descriptorCount = 1,
               .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
               .pImmutableSamplers = NULL
            },
         }
      }, &device->meta_state.alloc, &device->meta_state.blit2d.buf_ds_layout);
   if (result != VK_SUCCESS)
      goto fail;

   result = anv_CreatePipelineLayout(anv_device_to_handle(device),
      &(VkPipelineLayoutCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
         .setLayoutCount = 1,
         .pSetLayouts = &device->meta_state.blit2d.buf_ds_layout,
      },
      &device->meta_state.alloc, &device->meta_state.blit2d.buf_p_layout);
   if (result != VK_SUCCESS)
      goto fail;

   for (unsigned src = 0; src < BLIT2D_NUM_SRC_TYPES; src++) {
      for (unsigned dst = 0; dst < BLIT2D_NUM_DST_TYPES; dst++) {
         result = blit2d_init_pipeline(device, src, dst);
         if (result != VK_SUCCESS)
            goto fail;
      }
   }

   return VK_SUCCESS;

fail:
   anv_device_finish_meta_blit2d_state(device);
   return result;
}
