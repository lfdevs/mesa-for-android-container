/*
 * Copyright © 2015 Intel Corporation
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

#include <assert.h>
#include <stdbool.h>

#include "anv_private.h"
#include "vk_format_info.h"

#include "common/gen_l3_config.h"
#include "genxml/gen_macros.h"
#include "genxml/genX_pack.h"

static void
emit_lrm(struct anv_batch *batch,
         uint32_t reg, struct anv_bo *bo, uint32_t offset)
{
   anv_batch_emit(batch, GENX(MI_LOAD_REGISTER_MEM), lrm) {
      lrm.RegisterAddress  = reg;
      lrm.MemoryAddress    = (struct anv_address) { bo, offset };
   }
}

static void
emit_lri(struct anv_batch *batch, uint32_t reg, uint32_t imm)
{
   anv_batch_emit(batch, GENX(MI_LOAD_REGISTER_IMM), lri) {
      lri.RegisterOffset   = reg;
      lri.DataDWord        = imm;
   }
}

void
genX(cmd_buffer_emit_state_base_address)(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_device *device = cmd_buffer->device;

   /* Emit a render target cache flush.
    *
    * This isn't documented anywhere in the PRM.  However, it seems to be
    * necessary prior to changing the surface state base adress.  Without
    * this, we get GPU hangs when using multi-level command buffers which
    * clear depth, reset state base address, and then go render stuff.
    */
   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.DCFlushEnable = true;
      pc.RenderTargetCacheFlushEnable = true;
      pc.CommandStreamerStallEnable = true;
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(STATE_BASE_ADDRESS), sba) {
      sba.GeneralStateBaseAddress = (struct anv_address) { NULL, 0 };
      sba.GeneralStateMemoryObjectControlState = GENX(MOCS);
      sba.GeneralStateBaseAddressModifyEnable = true;

      sba.SurfaceStateBaseAddress =
         anv_cmd_buffer_surface_base_address(cmd_buffer);
      sba.SurfaceStateMemoryObjectControlState = GENX(MOCS);
      sba.SurfaceStateBaseAddressModifyEnable = true;

      sba.DynamicStateBaseAddress =
         (struct anv_address) { &device->dynamic_state_block_pool.bo, 0 };
      sba.DynamicStateMemoryObjectControlState = GENX(MOCS);
      sba.DynamicStateBaseAddressModifyEnable = true;

      sba.IndirectObjectBaseAddress = (struct anv_address) { NULL, 0 };
      sba.IndirectObjectMemoryObjectControlState = GENX(MOCS);
      sba.IndirectObjectBaseAddressModifyEnable = true;

      sba.InstructionBaseAddress =
         (struct anv_address) { &device->instruction_block_pool.bo, 0 };
      sba.InstructionMemoryObjectControlState = GENX(MOCS);
      sba.InstructionBaseAddressModifyEnable = true;

#  if (GEN_GEN >= 8)
      /* Broadwell requires that we specify a buffer size for a bunch of
       * these fields.  However, since we will be growing the BO's live, we
       * just set them all to the maximum.
       */
      sba.GeneralStateBufferSize                = 0xfffff;
      sba.GeneralStateBufferSizeModifyEnable    = true;
      sba.DynamicStateBufferSize                = 0xfffff;
      sba.DynamicStateBufferSizeModifyEnable    = true;
      sba.IndirectObjectBufferSize              = 0xfffff;
      sba.IndirectObjectBufferSizeModifyEnable  = true;
      sba.InstructionBufferSize                 = 0xfffff;
      sba.InstructionBuffersizeModifyEnable     = true;
#  endif
   }

   /* After re-setting the surface state base address, we have to do some
    * cache flusing so that the sampler engine will pick up the new
    * SURFACE_STATE objects and binding tables. From the Broadwell PRM,
    * Shared Function > 3D Sampler > State > State Caching (page 96):
    *
    *    Coherency with system memory in the state cache, like the texture
    *    cache is handled partially by software. It is expected that the
    *    command stream or shader will issue Cache Flush operation or
    *    Cache_Flush sampler message to ensure that the L1 cache remains
    *    coherent with system memory.
    *
    *    [...]
    *
    *    Whenever the value of the Dynamic_State_Base_Addr,
    *    Surface_State_Base_Addr are altered, the L1 state cache must be
    *    invalidated to ensure the new surface or sampler state is fetched
    *    from system memory.
    *
    * The PIPE_CONTROL command has a "State Cache Invalidation Enable" bit
    * which, according the PIPE_CONTROL instruction documentation in the
    * Broadwell PRM:
    *
    *    Setting this bit is independent of any other bit in this packet.
    *    This bit controls the invalidation of the L1 and L2 state caches
    *    at the top of the pipe i.e. at the parsing time.
    *
    * Unfortunately, experimentation seems to indicate that state cache
    * invalidation through a PIPE_CONTROL does nothing whatsoever in
    * regards to surface state and binding tables.  In stead, it seems that
    * invalidating the texture cache is what is actually needed.
    *
    * XXX:  As far as we have been able to determine through
    * experimentation, shows that flush the texture cache appears to be
    * sufficient.  The theory here is that all of the sampling/rendering
    * units cache the binding table in the texture cache.  However, we have
    * yet to be able to actually confirm this.
    */
   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.TextureCacheInvalidationEnable = true;
      pc.ConstantCacheInvalidationEnable = true;
      pc.StateCacheInvalidationEnable = true;
   }
}

static void
add_surface_state_reloc(struct anv_cmd_buffer *cmd_buffer,
                        struct anv_state state,
                        struct anv_bo *bo, uint32_t offset)
{
   const struct isl_device *isl_dev = &cmd_buffer->device->isl_dev;

   anv_reloc_list_add(&cmd_buffer->surface_relocs, &cmd_buffer->pool->alloc,
                      state.offset + isl_dev->ss.addr_offset, bo, offset);
}

static void
add_image_view_relocs(struct anv_cmd_buffer *cmd_buffer,
                      const struct anv_image_view *iview,
                      enum isl_aux_usage aux_usage,
                      struct anv_state state)
{
   const struct isl_device *isl_dev = &cmd_buffer->device->isl_dev;

   anv_reloc_list_add(&cmd_buffer->surface_relocs, &cmd_buffer->pool->alloc,
                      state.offset + isl_dev->ss.addr_offset,
                      iview->bo, iview->offset);

   if (aux_usage != ISL_AUX_USAGE_NONE) {
      uint32_t aux_offset = iview->offset + iview->image->aux_surface.offset;

      /* On gen7 and prior, the bottom 12 bits of the MCS base address are
       * used to store other information.  This should be ok, however, because
       * surface buffer addresses are always 4K page alinged.
       */
      assert((aux_offset & 0xfff) == 0);
      uint32_t *aux_addr_dw = state.map + isl_dev->ss.aux_addr_offset;
      aux_offset += *aux_addr_dw & 0xfff;

      anv_reloc_list_add(&cmd_buffer->surface_relocs, &cmd_buffer->pool->alloc,
                         state.offset + isl_dev->ss.aux_addr_offset,
                         iview->bo, aux_offset);
   }
}

static bool
color_is_zero_one(VkClearColorValue value, enum isl_format format)
{
   if (isl_format_has_int_channel(format)) {
      for (unsigned i = 0; i < 4; i++) {
         if (value.int32[i] != 0 && value.int32[i] != 1)
            return false;
      }
   } else {
      for (unsigned i = 0; i < 4; i++) {
         if (value.float32[i] != 0.0f && value.float32[i] != 1.0f)
            return false;
      }
   }

   return true;
}

static void
color_attachment_compute_aux_usage(struct anv_device *device,
                                   struct anv_attachment_state *att_state,
                                   struct anv_image_view *iview,
                                   VkRect2D render_area,
                                   union isl_color_value *fast_clear_color)
{
   if (iview->image->aux_surface.isl.size == 0) {
      att_state->aux_usage = ISL_AUX_USAGE_NONE;
      att_state->input_aux_usage = ISL_AUX_USAGE_NONE;
      att_state->fast_clear = false;
      return;
   }

   assert(iview->image->aux_surface.isl.usage & ISL_SURF_USAGE_CCS_BIT);

   att_state->clear_color_is_zero_one =
      color_is_zero_one(att_state->clear_value.color, iview->isl.format);

   if (att_state->pending_clear_aspects == VK_IMAGE_ASPECT_COLOR_BIT) {
      /* Start off assuming fast clears are possible */
      att_state->fast_clear = true;

      /* Potentially, we could do partial fast-clears but doing so has crazy
       * alignment restrictions.  It's easier to just restrict to full size
       * fast clears for now.
       */
      if (render_area.offset.x != 0 ||
          render_area.offset.y != 0 ||
          render_area.extent.width != iview->extent.width ||
          render_area.extent.height != iview->extent.height)
         att_state->fast_clear = false;

      if (GEN_GEN <= 7) {
         /* On gen7, we can't do multi-LOD or multi-layer fast-clears.  We
          * technically can, but it comes with crazy restrictions that we
          * don't want to deal with now.
          */
         if (iview->isl.base_level > 0 ||
             iview->isl.base_array_layer > 0 ||
             iview->isl.array_len > 1)
            att_state->fast_clear = false;
      }

      /* On Broadwell and earlier, we can only handle 0/1 clear colors */
      if (GEN_GEN <= 8 && !att_state->clear_color_is_zero_one)
         att_state->fast_clear = false;

      if (att_state->fast_clear) {
         memcpy(fast_clear_color->u32, att_state->clear_value.color.uint32,
                sizeof(fast_clear_color->u32));
      }
   } else {
      att_state->fast_clear = false;
   }

   if (isl_format_supports_lossless_compression(&device->info,
                                                iview->isl.format)) {
      att_state->aux_usage = ISL_AUX_USAGE_CCS_E;
      att_state->input_aux_usage = ISL_AUX_USAGE_CCS_E;
   } else if (att_state->fast_clear) {
      att_state->aux_usage = ISL_AUX_USAGE_CCS_D;
      if (GEN_GEN >= 9) {
         /* From the Sky Lake PRM, RENDER_SURFACE_STATE::AuxiliarySurfaceMode:
          *
          *    "If Number of Multisamples is MULTISAMPLECOUNT_1, AUX_CCS_D
          *    setting is only allowed if Surface Format supported for Fast
          *    Clear. In addition, if the surface is bound to the sampling
          *    engine, Surface Format must be supported for Render Target
          *    Compression for surfaces bound to the sampling engine."
          *
          * In other words, we can't sample from a fast-cleared image if it
          * doesn't also support color compression.
          */
         att_state->input_aux_usage = ISL_AUX_USAGE_NONE;
      } else if (GEN_GEN == 8) {
         /* Broadwell can sample from fast-cleared images */
         att_state->input_aux_usage = ISL_AUX_USAGE_CCS_D;
      } else {
         /* Ivy Bridge and Haswell cannot */
         att_state->input_aux_usage = ISL_AUX_USAGE_NONE;
      }
   } else {
      att_state->aux_usage = ISL_AUX_USAGE_NONE;
      att_state->input_aux_usage = ISL_AUX_USAGE_NONE;
   }
}

static bool
need_input_attachment_state(const struct anv_render_pass_attachment *att)
{
   if (!(att->usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT))
      return false;

   /* We only allocate input attachment states for color surfaces. Compression
    * is not yet enabled for depth textures and stencil doesn't allow
    * compression so we can just use the texture surface state from the view.
    */
   return vk_format_is_color(att->format);
}

static enum isl_aux_usage
layout_to_hiz_usage(VkImageLayout layout, uint8_t samples)
{
   switch (layout) {
   case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      return ISL_AUX_USAGE_HIZ;
   case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
   case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      if (anv_can_sample_with_hiz(GEN_GEN, samples))
         return ISL_AUX_USAGE_HIZ;
      /* Fall-through */
   case VK_IMAGE_LAYOUT_GENERAL:
      /* This buffer could be used as a source or destination in a transfer
       * operation. Transfer operations current don't perform HiZ-enabled reads
       * and writes.
       */
   default:
      return ISL_AUX_USAGE_NONE;
   }
}

/* Transitions a HiZ-enabled depth buffer from one layout to another. Unless
 * the initial layout is undefined, the HiZ buffer and depth buffer will
 * represent the same data at the end of this operation.
 */
static void
transition_depth_buffer(struct anv_cmd_buffer *cmd_buffer,
                        const struct anv_image *image,
                        VkImageLayout initial_layout,
                        VkImageLayout final_layout)
{
   assert(image);

   if (image->aux_usage != ISL_AUX_USAGE_HIZ || final_layout == initial_layout)
      return;

   const bool hiz_enabled = layout_to_hiz_usage(initial_layout, image->samples) ==
                            ISL_AUX_USAGE_HIZ;
   const bool enable_hiz = layout_to_hiz_usage(final_layout, image->samples) ==
                           ISL_AUX_USAGE_HIZ;

   enum blorp_hiz_op hiz_op;
   if (initial_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
      /* We've already initialized the aux HiZ buffer at BindImageMemory time,
       * so there's no need to perform a HIZ resolve or clear to avoid GPU hangs.
       * This initial layout indicates that the user doesn't care about the data
       * that's currently in the buffer, so resolves are not necessary except
       * for the special case noted below.
       */
      hiz_op = BLORP_HIZ_OP_NONE;
   } else if (hiz_enabled && !enable_hiz) {
      hiz_op = BLORP_HIZ_OP_DEPTH_RESOLVE;
   } else if (!hiz_enabled && enable_hiz) {
      hiz_op = BLORP_HIZ_OP_HIZ_RESOLVE;
   } else {
      assert(hiz_enabled == enable_hiz);
      /* If the same buffer will be used, no resolves are necessary except for
       * the special case noted below.
       */
      hiz_op = BLORP_HIZ_OP_NONE;
   }

   if (hiz_op != BLORP_HIZ_OP_NONE)
      anv_gen8_hiz_op_resolve(cmd_buffer, image, hiz_op);

   /* Images that have sampling with HiZ enabled cause all shader sampling to
    * load data with the HiZ buffer. Therefore, in the case of transitioning to
    * the general layout - which currently routes all writes to the depth
    * buffer - we must ensure that the HiZ buffer remains consistent with the
    * depth buffer by performing an additional HIZ resolve if the operation
    * required by this transition was not already a HiZ resolve.
    */
   if (final_layout == VK_IMAGE_LAYOUT_GENERAL &&
       anv_can_sample_with_hiz(GEN_GEN, image->samples) &&
       hiz_op != BLORP_HIZ_OP_HIZ_RESOLVE) {
      anv_gen8_hiz_op_resolve(cmd_buffer, image, BLORP_HIZ_OP_HIZ_RESOLVE);
   }
}


/**
 * Setup anv_cmd_state::attachments for vkCmdBeginRenderPass.
 */
static void
genX(cmd_buffer_setup_attachments)(struct anv_cmd_buffer *cmd_buffer,
                                   struct anv_render_pass *pass,
                                   const VkRenderPassBeginInfo *begin)
{
   const struct isl_device *isl_dev = &cmd_buffer->device->isl_dev;
   struct anv_cmd_state *state = &cmd_buffer->state;

   vk_free(&cmd_buffer->pool->alloc, state->attachments);

   if (pass->attachment_count == 0) {
      state->attachments = NULL;
      return;
   }

   state->attachments = vk_alloc(&cmd_buffer->pool->alloc,
                                 pass->attachment_count *
                                      sizeof(state->attachments[0]),
                                 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (state->attachments == NULL) {
      /* FIXME: Propagate VK_ERROR_OUT_OF_HOST_MEMORY to vkEndCommandBuffer */
      abort();
   }

   bool need_null_state = false;
   unsigned num_states = 0;
   for (uint32_t i = 0; i < pass->attachment_count; ++i) {
      if (vk_format_is_color(pass->attachments[i].format)) {
         num_states++;
      } else {
         /* We need a null state for any depth-stencil-only subpasses.
          * Importantly, this includes depth/stencil clears so we create one
          * whenever we have depth or stencil
          */
         need_null_state = true;
      }

      if (need_input_attachment_state(&pass->attachments[i]))
         num_states++;
   }
   num_states += need_null_state;

   const uint32_t ss_stride = align_u32(isl_dev->ss.size, isl_dev->ss.align);
   state->render_pass_states =
      anv_state_stream_alloc(&cmd_buffer->surface_state_stream,
                             num_states * ss_stride, isl_dev->ss.align);

   struct anv_state next_state = state->render_pass_states;
   next_state.alloc_size = isl_dev->ss.size;

   if (need_null_state) {
      state->null_surface_state = next_state;
      next_state.offset += ss_stride;
      next_state.map += ss_stride;
   }

   for (uint32_t i = 0; i < pass->attachment_count; ++i) {
      if (vk_format_is_color(pass->attachments[i].format)) {
         state->attachments[i].color_rt_state = next_state;
         next_state.offset += ss_stride;
         next_state.map += ss_stride;
      }

      if (need_input_attachment_state(&pass->attachments[i])) {
         state->attachments[i].input_att_state = next_state;
         next_state.offset += ss_stride;
         next_state.map += ss_stride;
      }
   }
   assert(next_state.offset == state->render_pass_states.offset +
                               state->render_pass_states.alloc_size);

   if (begin) {
      ANV_FROM_HANDLE(anv_framebuffer, framebuffer, begin->framebuffer);
      assert(pass->attachment_count == framebuffer->attachment_count);

      if (need_null_state) {
         struct GENX(RENDER_SURFACE_STATE) null_ss = {
            .SurfaceType = SURFTYPE_NULL,
            .SurfaceArray = framebuffer->layers > 0,
            .SurfaceFormat = ISL_FORMAT_R8G8B8A8_UNORM,
#if GEN_GEN >= 8
            .TileMode = YMAJOR,
#else
            .TiledSurface = true,
#endif
            .Width = framebuffer->width - 1,
            .Height = framebuffer->height - 1,
            .Depth = framebuffer->layers - 1,
            .RenderTargetViewExtent = framebuffer->layers - 1,
         };
         GENX(RENDER_SURFACE_STATE_pack)(NULL, state->null_surface_state.map,
                                         &null_ss);
      }

      for (uint32_t i = 0; i < pass->attachment_count; ++i) {
         struct anv_render_pass_attachment *att = &pass->attachments[i];
         VkImageAspectFlags att_aspects = vk_format_aspects(att->format);
         VkImageAspectFlags clear_aspects = 0;

         if (att_aspects == VK_IMAGE_ASPECT_COLOR_BIT) {
            /* color attachment */
            if (att->load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
               clear_aspects |= VK_IMAGE_ASPECT_COLOR_BIT;
            }
         } else {
            /* depthstencil attachment */
            if ((att_aspects & VK_IMAGE_ASPECT_DEPTH_BIT) &&
                att->load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
               clear_aspects |= VK_IMAGE_ASPECT_DEPTH_BIT;
            }
            if ((att_aspects & VK_IMAGE_ASPECT_STENCIL_BIT) &&
                att->stencil_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
               clear_aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
         }

         state->attachments[i].current_layout = att->initial_layout;
         state->attachments[i].pending_clear_aspects = clear_aspects;
         if (clear_aspects)
            state->attachments[i].clear_value = begin->pClearValues[i];

         struct anv_image_view *iview = framebuffer->attachments[i];
         assert(iview->vk_format == att->format);

         union isl_color_value clear_color = { .u32 = { 0, } };
         if (att_aspects == VK_IMAGE_ASPECT_COLOR_BIT) {
            color_attachment_compute_aux_usage(cmd_buffer->device,
                                               &state->attachments[i],
                                               iview, begin->renderArea,
                                               &clear_color);

            struct isl_view view = iview->isl;
            view.usage |= ISL_SURF_USAGE_RENDER_TARGET_BIT;
            isl_surf_fill_state(isl_dev,
                                state->attachments[i].color_rt_state.map,
                                .surf = &iview->image->color_surface.isl,
                                .view = &view,
                                .aux_surf = &iview->image->aux_surface.isl,
                                .aux_usage = state->attachments[i].aux_usage,
                                .clear_color = clear_color,
                                .mocs = cmd_buffer->device->default_mocs);

            add_image_view_relocs(cmd_buffer, iview,
                                  state->attachments[i].aux_usage,
                                  state->attachments[i].color_rt_state);
         } else {
            if (iview->image->aux_usage == ISL_AUX_USAGE_HIZ) {
               state->attachments[i].aux_usage =
                  layout_to_hiz_usage(att->initial_layout, iview->image->samples);
            } else {
               state->attachments[i].aux_usage = ISL_AUX_USAGE_NONE;
            }
            state->attachments[i].input_aux_usage = ISL_AUX_USAGE_NONE;
         }

         if (need_input_attachment_state(&pass->attachments[i])) {
            struct isl_view view = iview->isl;
            view.usage |= ISL_SURF_USAGE_TEXTURE_BIT;
            isl_surf_fill_state(isl_dev,
                                state->attachments[i].input_att_state.map,
                                .surf = &iview->image->color_surface.isl,
                                .view = &view,
                                .aux_surf = &iview->image->aux_surface.isl,
                                .aux_usage = state->attachments[i].input_aux_usage,
                                .clear_color = clear_color,
                                .mocs = cmd_buffer->device->default_mocs);

            add_image_view_relocs(cmd_buffer, iview,
                                  state->attachments[i].input_aux_usage,
                                  state->attachments[i].input_att_state);
         }
      }

      if (!cmd_buffer->device->info.has_llc)
         anv_state_clflush(state->render_pass_states);
   }
}

VkResult
genX(BeginCommandBuffer)(
    VkCommandBuffer                             commandBuffer,
    const VkCommandBufferBeginInfo*             pBeginInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   /* If this is the first vkBeginCommandBuffer, we must *initialize* the
    * command buffer's state. Otherwise, we must *reset* its state. In both
    * cases we reset it.
    *
    * From the Vulkan 1.0 spec:
    *
    *    If a command buffer is in the executable state and the command buffer
    *    was allocated from a command pool with the
    *    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT flag set, then
    *    vkBeginCommandBuffer implicitly resets the command buffer, behaving
    *    as if vkResetCommandBuffer had been called with
    *    VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT not set. It then puts
    *    the command buffer in the recording state.
    */
   anv_cmd_buffer_reset(cmd_buffer);

   cmd_buffer->usage_flags = pBeginInfo->flags;

   assert(cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY ||
          !(cmd_buffer->usage_flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT));

   genX(cmd_buffer_emit_state_base_address)(cmd_buffer);

   if (cmd_buffer->usage_flags &
       VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT) {
      cmd_buffer->state.pass =
         anv_render_pass_from_handle(pBeginInfo->pInheritanceInfo->renderPass);
      cmd_buffer->state.subpass =
         &cmd_buffer->state.pass->subpasses[pBeginInfo->pInheritanceInfo->subpass];
      cmd_buffer->state.framebuffer = NULL;

      genX(cmd_buffer_setup_attachments)(cmd_buffer, cmd_buffer->state.pass,
                                         NULL);

      cmd_buffer->state.dirty |= ANV_CMD_DIRTY_RENDER_TARGETS;
   }

   return VK_SUCCESS;
}

VkResult
genX(EndCommandBuffer)(
    VkCommandBuffer                             commandBuffer)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);

   anv_cmd_buffer_end_batch_buffer(cmd_buffer);

   return VK_SUCCESS;
}

void
genX(CmdExecuteCommands)(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    commandBufferCount,
    const VkCommandBuffer*                      pCmdBuffers)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, primary, commandBuffer);

   assert(primary->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY);

   /* The secondary command buffer doesn't know which textures etc. have been
    * flushed prior to their execution.  Apply those flushes now.
    */
   genX(cmd_buffer_apply_pipe_flushes)(primary);

   for (uint32_t i = 0; i < commandBufferCount; i++) {
      ANV_FROM_HANDLE(anv_cmd_buffer, secondary, pCmdBuffers[i]);

      assert(secondary->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY);

      if (secondary->usage_flags &
          VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT) {
         /* If we're continuing a render pass from the primary, we need to
          * copy the surface states for the current subpass into the storage
          * we allocated for them in BeginCommandBuffer.
          */
         struct anv_bo *ss_bo = &primary->device->surface_state_block_pool.bo;
         struct anv_state src_state = primary->state.render_pass_states;
         struct anv_state dst_state = secondary->state.render_pass_states;
         assert(src_state.alloc_size == dst_state.alloc_size);

         genX(cmd_buffer_gpu_memcpy)(primary, ss_bo, dst_state.offset,
                                     ss_bo, src_state.offset,
                                     src_state.alloc_size);
      }

      anv_cmd_buffer_add_secondary(primary, secondary);
   }

   /* Each of the secondary command buffers will use its own state base
    * address.  We need to re-emit state base address for the primary after
    * all of the secondaries are done.
    *
    * TODO: Maybe we want to make this a dirty bit to avoid extra state base
    * address calls?
    */
   genX(cmd_buffer_emit_state_base_address)(primary);
}

#define IVB_L3SQCREG1_SQGHPCI_DEFAULT     0x00730000
#define VLV_L3SQCREG1_SQGHPCI_DEFAULT     0x00d30000
#define HSW_L3SQCREG1_SQGHPCI_DEFAULT     0x00610000

/**
 * Program the hardware to use the specified L3 configuration.
 */
void
genX(cmd_buffer_config_l3)(struct anv_cmd_buffer *cmd_buffer,
                           const struct gen_l3_config *cfg)
{
   assert(cfg);
   if (cfg == cmd_buffer->state.current_l3_config)
      return;

   if (unlikely(INTEL_DEBUG & DEBUG_L3)) {
      fprintf(stderr, "L3 config transition: ");
      gen_dump_l3_config(cfg, stderr);
   }

   const bool has_slm = cfg->n[GEN_L3P_SLM];

   /* According to the hardware docs, the L3 partitioning can only be changed
    * while the pipeline is completely drained and the caches are flushed,
    * which involves a first PIPE_CONTROL flush which stalls the pipeline...
    */
   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.DCFlushEnable = true;
      pc.PostSyncOperation = NoWrite;
      pc.CommandStreamerStallEnable = true;
   }

   /* ...followed by a second pipelined PIPE_CONTROL that initiates
    * invalidation of the relevant caches.  Note that because RO invalidation
    * happens at the top of the pipeline (i.e. right away as the PIPE_CONTROL
    * command is processed by the CS) we cannot combine it with the previous
    * stalling flush as the hardware documentation suggests, because that
    * would cause the CS to stall on previous rendering *after* RO
    * invalidation and wouldn't prevent the RO caches from being polluted by
    * concurrent rendering before the stall completes.  This intentionally
    * doesn't implement the SKL+ hardware workaround suggesting to enable CS
    * stall on PIPE_CONTROLs with the texture cache invalidation bit set for
    * GPGPU workloads because the previous and subsequent PIPE_CONTROLs
    * already guarantee that there is no concurrent GPGPU kernel execution
    * (see SKL HSD 2132585).
    */
   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.TextureCacheInvalidationEnable = true;
      pc.ConstantCacheInvalidationEnable = true;
      pc.InstructionCacheInvalidateEnable = true;
      pc.StateCacheInvalidationEnable = true;
      pc.PostSyncOperation = NoWrite;
   }

   /* Now send a third stalling flush to make sure that invalidation is
    * complete when the L3 configuration registers are modified.
    */
   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.DCFlushEnable = true;
      pc.PostSyncOperation = NoWrite;
      pc.CommandStreamerStallEnable = true;
   }

#if GEN_GEN >= 8

   assert(!cfg->n[GEN_L3P_IS] && !cfg->n[GEN_L3P_C] && !cfg->n[GEN_L3P_T]);

   uint32_t l3cr;
   anv_pack_struct(&l3cr, GENX(L3CNTLREG),
                   .SLMEnable = has_slm,
                   .URBAllocation = cfg->n[GEN_L3P_URB],
                   .ROAllocation = cfg->n[GEN_L3P_RO],
                   .DCAllocation = cfg->n[GEN_L3P_DC],
                   .AllAllocation = cfg->n[GEN_L3P_ALL]);

   /* Set up the L3 partitioning. */
   emit_lri(&cmd_buffer->batch, GENX(L3CNTLREG_num), l3cr);

#else

   const bool has_dc = cfg->n[GEN_L3P_DC] || cfg->n[GEN_L3P_ALL];
   const bool has_is = cfg->n[GEN_L3P_IS] || cfg->n[GEN_L3P_RO] ||
                       cfg->n[GEN_L3P_ALL];
   const bool has_c = cfg->n[GEN_L3P_C] || cfg->n[GEN_L3P_RO] ||
                      cfg->n[GEN_L3P_ALL];
   const bool has_t = cfg->n[GEN_L3P_T] || cfg->n[GEN_L3P_RO] ||
                      cfg->n[GEN_L3P_ALL];

   assert(!cfg->n[GEN_L3P_ALL]);

   /* When enabled SLM only uses a portion of the L3 on half of the banks,
    * the matching space on the remaining banks has to be allocated to a
    * client (URB for all validated configurations) set to the
    * lower-bandwidth 2-bank address hashing mode.
    */
   const struct gen_device_info *devinfo = &cmd_buffer->device->info;
   const bool urb_low_bw = has_slm && !devinfo->is_baytrail;
   assert(!urb_low_bw || cfg->n[GEN_L3P_URB] == cfg->n[GEN_L3P_SLM]);

   /* Minimum number of ways that can be allocated to the URB. */
   MAYBE_UNUSED const unsigned n0_urb = devinfo->is_baytrail ? 32 : 0;
   assert(cfg->n[GEN_L3P_URB] >= n0_urb);

   uint32_t l3sqcr1, l3cr2, l3cr3;
   anv_pack_struct(&l3sqcr1, GENX(L3SQCREG1),
                   .ConvertDC_UC = !has_dc,
                   .ConvertIS_UC = !has_is,
                   .ConvertC_UC = !has_c,
                   .ConvertT_UC = !has_t);
   l3sqcr1 |=
      GEN_IS_HASWELL ? HSW_L3SQCREG1_SQGHPCI_DEFAULT :
      devinfo->is_baytrail ? VLV_L3SQCREG1_SQGHPCI_DEFAULT :
      IVB_L3SQCREG1_SQGHPCI_DEFAULT;

   anv_pack_struct(&l3cr2, GENX(L3CNTLREG2),
                   .SLMEnable = has_slm,
                   .URBLowBandwidth = urb_low_bw,
                   .URBAllocation = cfg->n[GEN_L3P_URB],
#if !GEN_IS_HASWELL
                   .ALLAllocation = cfg->n[GEN_L3P_ALL],
#endif
                   .ROAllocation = cfg->n[GEN_L3P_RO],
                   .DCAllocation = cfg->n[GEN_L3P_DC]);

   anv_pack_struct(&l3cr3, GENX(L3CNTLREG3),
                   .ISAllocation = cfg->n[GEN_L3P_IS],
                   .ISLowBandwidth = 0,
                   .CAllocation = cfg->n[GEN_L3P_C],
                   .CLowBandwidth = 0,
                   .TAllocation = cfg->n[GEN_L3P_T],
                   .TLowBandwidth = 0);

   /* Set up the L3 partitioning. */
   emit_lri(&cmd_buffer->batch, GENX(L3SQCREG1_num), l3sqcr1);
   emit_lri(&cmd_buffer->batch, GENX(L3CNTLREG2_num), l3cr2);
   emit_lri(&cmd_buffer->batch, GENX(L3CNTLREG3_num), l3cr3);

#if GEN_IS_HASWELL
   if (cmd_buffer->device->instance->physicalDevice.cmd_parser_version >= 4) {
      /* Enable L3 atomics on HSW if we have a DC partition, otherwise keep
       * them disabled to avoid crashing the system hard.
       */
      uint32_t scratch1, chicken3;
      anv_pack_struct(&scratch1, GENX(SCRATCH1),
                      .L3AtomicDisable = !has_dc);
      anv_pack_struct(&chicken3, GENX(CHICKEN3),
                      .L3AtomicDisableMask = true,
                      .L3AtomicDisable = !has_dc);
      emit_lri(&cmd_buffer->batch, GENX(SCRATCH1_num), scratch1);
      emit_lri(&cmd_buffer->batch, GENX(CHICKEN3_num), chicken3);
   }
#endif

#endif

   cmd_buffer->state.current_l3_config = cfg;
}

void
genX(cmd_buffer_apply_pipe_flushes)(struct anv_cmd_buffer *cmd_buffer)
{
   enum anv_pipe_bits bits = cmd_buffer->state.pending_pipe_bits;

   /* Flushes are pipelined while invalidations are handled immediately.
    * Therefore, if we're flushing anything then we need to schedule a stall
    * before any invalidations can happen.
    */
   if (bits & ANV_PIPE_FLUSH_BITS)
      bits |= ANV_PIPE_NEEDS_CS_STALL_BIT;

   /* If we're going to do an invalidate and we have a pending CS stall that
    * has yet to be resolved, we do the CS stall now.
    */
   if ((bits & ANV_PIPE_INVALIDATE_BITS) &&
       (bits & ANV_PIPE_NEEDS_CS_STALL_BIT)) {
      bits |= ANV_PIPE_CS_STALL_BIT;
      bits &= ~ANV_PIPE_NEEDS_CS_STALL_BIT;
   }

   if (bits & (ANV_PIPE_FLUSH_BITS | ANV_PIPE_CS_STALL_BIT)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pipe) {
         pipe.DepthCacheFlushEnable = bits & ANV_PIPE_DEPTH_CACHE_FLUSH_BIT;
         pipe.DCFlushEnable = bits & ANV_PIPE_DATA_CACHE_FLUSH_BIT;
         pipe.RenderTargetCacheFlushEnable =
            bits & ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT;

         pipe.DepthStallEnable = bits & ANV_PIPE_DEPTH_STALL_BIT;
         pipe.CommandStreamerStallEnable = bits & ANV_PIPE_CS_STALL_BIT;
         pipe.StallAtPixelScoreboard = bits & ANV_PIPE_STALL_AT_SCOREBOARD_BIT;

         /*
          * According to the Broadwell documentation, any PIPE_CONTROL with the
          * "Command Streamer Stall" bit set must also have another bit set,
          * with five different options:
          *
          *  - Render Target Cache Flush
          *  - Depth Cache Flush
          *  - Stall at Pixel Scoreboard
          *  - Post-Sync Operation
          *  - Depth Stall
          *  - DC Flush Enable
          *
          * I chose "Stall at Pixel Scoreboard" since that's what we use in
          * mesa and it seems to work fine. The choice is fairly arbitrary.
          */
         if ((bits & ANV_PIPE_CS_STALL_BIT) &&
             !(bits & (ANV_PIPE_FLUSH_BITS | ANV_PIPE_DEPTH_STALL_BIT |
                       ANV_PIPE_STALL_AT_SCOREBOARD_BIT)))
            pipe.StallAtPixelScoreboard = true;
      }

      bits &= ~(ANV_PIPE_FLUSH_BITS | ANV_PIPE_CS_STALL_BIT);
   }

   if (bits & ANV_PIPE_INVALIDATE_BITS) {
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pipe) {
         pipe.StateCacheInvalidationEnable =
            bits & ANV_PIPE_STATE_CACHE_INVALIDATE_BIT;
         pipe.ConstantCacheInvalidationEnable =
            bits & ANV_PIPE_CONSTANT_CACHE_INVALIDATE_BIT;
         pipe.VFCacheInvalidationEnable =
            bits & ANV_PIPE_VF_CACHE_INVALIDATE_BIT;
         pipe.TextureCacheInvalidationEnable =
            bits & ANV_PIPE_TEXTURE_CACHE_INVALIDATE_BIT;
         pipe.InstructionCacheInvalidateEnable =
            bits & ANV_PIPE_INSTRUCTION_CACHE_INVALIDATE_BIT;
      }

      bits &= ~ANV_PIPE_INVALIDATE_BITS;
   }

   cmd_buffer->state.pending_pipe_bits = bits;
}

void genX(CmdPipelineBarrier)(
    VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlags                        srcStageMask,
    VkPipelineStageFlags                        destStageMask,
    VkBool32                                    byRegion,
    uint32_t                                    memoryBarrierCount,
    const VkMemoryBarrier*                      pMemoryBarriers,
    uint32_t                                    bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
    uint32_t                                    imageMemoryBarrierCount,
    const VkImageMemoryBarrier*                 pImageMemoryBarriers)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   uint32_t b;

   /* XXX: Right now, we're really dumb and just flush whatever categories
    * the app asks for.  One of these days we may make this a bit better
    * but right now that's all the hardware allows for in most areas.
    */
   VkAccessFlags src_flags = 0;
   VkAccessFlags dst_flags = 0;

   for (uint32_t i = 0; i < memoryBarrierCount; i++) {
      src_flags |= pMemoryBarriers[i].srcAccessMask;
      dst_flags |= pMemoryBarriers[i].dstAccessMask;
   }

   for (uint32_t i = 0; i < bufferMemoryBarrierCount; i++) {
      src_flags |= pBufferMemoryBarriers[i].srcAccessMask;
      dst_flags |= pBufferMemoryBarriers[i].dstAccessMask;
   }

   for (uint32_t i = 0; i < imageMemoryBarrierCount; i++) {
      src_flags |= pImageMemoryBarriers[i].srcAccessMask;
      dst_flags |= pImageMemoryBarriers[i].dstAccessMask;
      ANV_FROM_HANDLE(anv_image, image, pImageMemoryBarriers[i].image);
      if (pImageMemoryBarriers[i].subresourceRange.aspectMask &
          VK_IMAGE_ASPECT_DEPTH_BIT) {
         transition_depth_buffer(cmd_buffer, image,
                                 pImageMemoryBarriers[i].oldLayout,
                                 pImageMemoryBarriers[i].newLayout);
      }
   }

   enum anv_pipe_bits pipe_bits = 0;

   for_each_bit(b, src_flags) {
      switch ((VkAccessFlagBits)(1 << b)) {
      case VK_ACCESS_SHADER_WRITE_BIT:
         pipe_bits |= ANV_PIPE_DATA_CACHE_FLUSH_BIT;
         break;
      case VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT:
         pipe_bits |= ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT;
         break;
      case VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT:
         pipe_bits |= ANV_PIPE_DEPTH_CACHE_FLUSH_BIT;
         break;
      case VK_ACCESS_TRANSFER_WRITE_BIT:
         pipe_bits |= ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT;
         pipe_bits |= ANV_PIPE_DEPTH_CACHE_FLUSH_BIT;
         break;
      default:
         break; /* Nothing to do */
      }
   }

   for_each_bit(b, dst_flags) {
      switch ((VkAccessFlagBits)(1 << b)) {
      case VK_ACCESS_INDIRECT_COMMAND_READ_BIT:
      case VK_ACCESS_INDEX_READ_BIT:
      case VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT:
         pipe_bits |= ANV_PIPE_VF_CACHE_INVALIDATE_BIT;
         break;
      case VK_ACCESS_UNIFORM_READ_BIT:
         pipe_bits |= ANV_PIPE_CONSTANT_CACHE_INVALIDATE_BIT;
         pipe_bits |= ANV_PIPE_TEXTURE_CACHE_INVALIDATE_BIT;
         break;
      case VK_ACCESS_SHADER_READ_BIT:
      case VK_ACCESS_INPUT_ATTACHMENT_READ_BIT:
      case VK_ACCESS_TRANSFER_READ_BIT:
         pipe_bits |= ANV_PIPE_TEXTURE_CACHE_INVALIDATE_BIT;
         break;
      default:
         break; /* Nothing to do */
      }
   }

   cmd_buffer->state.pending_pipe_bits |= pipe_bits;
}

static void
cmd_buffer_alloc_push_constants(struct anv_cmd_buffer *cmd_buffer)
{
   VkShaderStageFlags stages = cmd_buffer->state.pipeline->active_stages;

   /* In order to avoid thrash, we assume that vertex and fragment stages
    * always exist.  In the rare case where one is missing *and* the other
    * uses push concstants, this may be suboptimal.  However, avoiding stalls
    * seems more important.
    */
   stages |= VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;

   if (stages == cmd_buffer->state.push_constant_stages)
      return;

#if GEN_GEN >= 8
   const unsigned push_constant_kb = 32;
#elif GEN_IS_HASWELL
   const unsigned push_constant_kb = cmd_buffer->device->info.gt == 3 ? 32 : 16;
#else
   const unsigned push_constant_kb = 16;
#endif

   const unsigned num_stages =
      _mesa_bitcount(stages & VK_SHADER_STAGE_ALL_GRAPHICS);
   unsigned size_per_stage = push_constant_kb / num_stages;

   /* Broadwell+ and Haswell gt3 require that the push constant sizes be in
    * units of 2KB.  Incidentally, these are the same platforms that have
    * 32KB worth of push constant space.
    */
   if (push_constant_kb == 32)
      size_per_stage &= ~1u;

   uint32_t kb_used = 0;
   for (int i = MESA_SHADER_VERTEX; i < MESA_SHADER_FRAGMENT; i++) {
      unsigned push_size = (stages & (1 << i)) ? size_per_stage : 0;
      anv_batch_emit(&cmd_buffer->batch,
                     GENX(3DSTATE_PUSH_CONSTANT_ALLOC_VS), alloc) {
         alloc._3DCommandSubOpcode  = 18 + i;
         alloc.ConstantBufferOffset = (push_size > 0) ? kb_used : 0;
         alloc.ConstantBufferSize   = push_size;
      }
      kb_used += push_size;
   }

   anv_batch_emit(&cmd_buffer->batch,
                  GENX(3DSTATE_PUSH_CONSTANT_ALLOC_PS), alloc) {
      alloc.ConstantBufferOffset = kb_used;
      alloc.ConstantBufferSize = push_constant_kb - kb_used;
   }

   cmd_buffer->state.push_constant_stages = stages;

   /* From the BDW PRM for 3DSTATE_PUSH_CONSTANT_ALLOC_VS:
    *
    *    "The 3DSTATE_CONSTANT_VS must be reprogrammed prior to
    *    the next 3DPRIMITIVE command after programming the
    *    3DSTATE_PUSH_CONSTANT_ALLOC_VS"
    *
    * Since 3DSTATE_PUSH_CONSTANT_ALLOC_VS is programmed as part of
    * pipeline setup, we need to dirty push constants.
    */
   cmd_buffer->state.push_constants_dirty |= VK_SHADER_STAGE_ALL_GRAPHICS;
}

static VkResult
emit_binding_table(struct anv_cmd_buffer *cmd_buffer,
                   gl_shader_stage stage,
                   struct anv_state *bt_state)
{
   struct anv_subpass *subpass = cmd_buffer->state.subpass;
   struct anv_pipeline *pipeline;
   uint32_t bias, state_offset;

   switch (stage) {
   case  MESA_SHADER_COMPUTE:
      pipeline = cmd_buffer->state.compute_pipeline;
      bias = 1;
      break;
   default:
      pipeline = cmd_buffer->state.pipeline;
      bias = 0;
      break;
   }

   if (!anv_pipeline_has_stage(pipeline, stage)) {
      *bt_state = (struct anv_state) { 0, };
      return VK_SUCCESS;
   }

   struct anv_pipeline_bind_map *map = &pipeline->shaders[stage]->bind_map;
   if (bias + map->surface_count == 0) {
      *bt_state = (struct anv_state) { 0, };
      return VK_SUCCESS;
   }

   *bt_state = anv_cmd_buffer_alloc_binding_table(cmd_buffer,
                                                  bias + map->surface_count,
                                                  &state_offset);
   uint32_t *bt_map = bt_state->map;

   if (bt_state->map == NULL)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   if (stage == MESA_SHADER_COMPUTE &&
       get_cs_prog_data(cmd_buffer->state.compute_pipeline)->uses_num_work_groups) {
      struct anv_bo *bo = cmd_buffer->state.num_workgroups_bo;
      uint32_t bo_offset = cmd_buffer->state.num_workgroups_offset;

      struct anv_state surface_state;
      surface_state =
         anv_cmd_buffer_alloc_surface_state(cmd_buffer);

      const enum isl_format format =
         anv_isl_format_for_descriptor_type(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
      anv_fill_buffer_surface_state(cmd_buffer->device, surface_state,
                                    format, bo_offset, 12, 1);

      bt_map[0] = surface_state.offset + state_offset;
      add_surface_state_reloc(cmd_buffer, surface_state, bo, bo_offset);
   }

   if (map->surface_count == 0)
      goto out;

   if (map->image_count > 0) {
      VkResult result =
         anv_cmd_buffer_ensure_push_constant_field(cmd_buffer, stage, images);
      if (result != VK_SUCCESS)
         return result;

      cmd_buffer->state.push_constants_dirty |= 1 << stage;
   }

   uint32_t image = 0;
   for (uint32_t s = 0; s < map->surface_count; s++) {
      struct anv_pipeline_binding *binding = &map->surface_to_descriptor[s];

      struct anv_state surface_state;

      if (binding->set == ANV_DESCRIPTOR_SET_COLOR_ATTACHMENTS) {
         /* Color attachment binding */
         assert(stage == MESA_SHADER_FRAGMENT);
         assert(binding->binding == 0);
         if (binding->index < subpass->color_count) {
            const unsigned att = subpass->color_attachments[binding->index];
            surface_state = cmd_buffer->state.attachments[att].color_rt_state;
         } else {
            surface_state = cmd_buffer->state.null_surface_state;
         }

         bt_map[bias + s] = surface_state.offset + state_offset;
         continue;
      }

      struct anv_descriptor_set *set =
         cmd_buffer->state.descriptors[binding->set];
      uint32_t offset = set->layout->binding[binding->binding].descriptor_index;
      struct anv_descriptor *desc = &set->descriptors[offset + binding->index];

      switch (desc->type) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
         /* Nothing for us to do here */
         continue;

      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
         surface_state = desc->image_view->sampler_surface_state;
         assert(surface_state.alloc_size);
         add_image_view_relocs(cmd_buffer, desc->image_view,
                               desc->image_view->image->aux_usage,
                               surface_state);
         break;

      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         assert(stage == MESA_SHADER_FRAGMENT);
         if (desc->image_view->aspect_mask != VK_IMAGE_ASPECT_COLOR_BIT) {
            /* For depth and stencil input attachments, we treat it like any
             * old texture that a user may have bound.
             */
            surface_state = desc->image_view->sampler_surface_state;
            assert(surface_state.alloc_size);
            add_image_view_relocs(cmd_buffer, desc->image_view,
                                  desc->image_view->image->aux_usage,
                                  surface_state);
         } else {
            /* For color input attachments, we create the surface state at
             * vkBeginRenderPass time so that we can include aux and clear
             * color information.
             */
            assert(binding->input_attachment_index < subpass->input_count);
            const unsigned subpass_att = binding->input_attachment_index;
            const unsigned att = subpass->input_attachments[subpass_att];
            surface_state = cmd_buffer->state.attachments[att].input_att_state;
         }
         break;

      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: {
         surface_state = desc->image_view->storage_surface_state;
         assert(surface_state.alloc_size);
         add_image_view_relocs(cmd_buffer, desc->image_view,
                               desc->image_view->image->aux_usage,
                               surface_state);

         struct brw_image_param *image_param =
            &cmd_buffer->state.push_constants[stage]->images[image++];

         *image_param = desc->image_view->storage_image_param;
         image_param->surface_idx = bias + s;
         break;
      }

      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
         surface_state = desc->buffer_view->surface_state;
         assert(surface_state.alloc_size);
         add_surface_state_reloc(cmd_buffer, surface_state,
                                 desc->buffer_view->bo,
                                 desc->buffer_view->offset);
         break;

      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         surface_state = desc->buffer_view->storage_surface_state;
         assert(surface_state.alloc_size);
         add_surface_state_reloc(cmd_buffer, surface_state,
                                 desc->buffer_view->bo,
                                 desc->buffer_view->offset);

         struct brw_image_param *image_param =
            &cmd_buffer->state.push_constants[stage]->images[image++];

         *image_param = desc->buffer_view->storage_image_param;
         image_param->surface_idx = bias + s;
         break;

      default:
         assert(!"Invalid descriptor type");
         continue;
      }

      bt_map[bias + s] = surface_state.offset + state_offset;
   }
   assert(image == map->image_count);

 out:
   if (!cmd_buffer->device->info.has_llc)
      anv_state_clflush(*bt_state);

   return VK_SUCCESS;
}

static VkResult
emit_samplers(struct anv_cmd_buffer *cmd_buffer,
              gl_shader_stage stage,
              struct anv_state *state)
{
   struct anv_pipeline *pipeline;

   if (stage == MESA_SHADER_COMPUTE)
      pipeline = cmd_buffer->state.compute_pipeline;
   else
      pipeline = cmd_buffer->state.pipeline;

   if (!anv_pipeline_has_stage(pipeline, stage)) {
      *state = (struct anv_state) { 0, };
      return VK_SUCCESS;
   }

   struct anv_pipeline_bind_map *map = &pipeline->shaders[stage]->bind_map;
   if (map->sampler_count == 0) {
      *state = (struct anv_state) { 0, };
      return VK_SUCCESS;
   }

   uint32_t size = map->sampler_count * 16;
   *state = anv_cmd_buffer_alloc_dynamic_state(cmd_buffer, size, 32);

   if (state->map == NULL)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   for (uint32_t s = 0; s < map->sampler_count; s++) {
      struct anv_pipeline_binding *binding = &map->sampler_to_descriptor[s];
      struct anv_descriptor_set *set =
         cmd_buffer->state.descriptors[binding->set];
      uint32_t offset = set->layout->binding[binding->binding].descriptor_index;
      struct anv_descriptor *desc = &set->descriptors[offset + binding->index];

      if (desc->type != VK_DESCRIPTOR_TYPE_SAMPLER &&
          desc->type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
         continue;

      struct anv_sampler *sampler = desc->sampler;

      /* This can happen if we have an unfilled slot since TYPE_SAMPLER
       * happens to be zero.
       */
      if (sampler == NULL)
         continue;

      memcpy(state->map + (s * 16),
             sampler->state, sizeof(sampler->state));
   }

   if (!cmd_buffer->device->info.has_llc)
      anv_state_clflush(*state);

   return VK_SUCCESS;
}

static uint32_t
flush_descriptor_sets(struct anv_cmd_buffer *cmd_buffer)
{
   VkShaderStageFlags dirty = cmd_buffer->state.descriptors_dirty &
                              cmd_buffer->state.pipeline->active_stages;

   VkResult result = VK_SUCCESS;
   anv_foreach_stage(s, dirty) {
      result = emit_samplers(cmd_buffer, s, &cmd_buffer->state.samplers[s]);
      if (result != VK_SUCCESS)
         break;
      result = emit_binding_table(cmd_buffer, s,
                                  &cmd_buffer->state.binding_tables[s]);
      if (result != VK_SUCCESS)
         break;
   }

   if (result != VK_SUCCESS) {
      assert(result == VK_ERROR_OUT_OF_DEVICE_MEMORY);

      result = anv_cmd_buffer_new_binding_table_block(cmd_buffer);
      assert(result == VK_SUCCESS);

      /* Re-emit state base addresses so we get the new surface state base
       * address before we start emitting binding tables etc.
       */
      genX(cmd_buffer_emit_state_base_address)(cmd_buffer);

      /* Re-emit all active binding tables */
      dirty |= cmd_buffer->state.pipeline->active_stages;
      anv_foreach_stage(s, dirty) {
         result = emit_samplers(cmd_buffer, s, &cmd_buffer->state.samplers[s]);
         if (result != VK_SUCCESS)
            return result;
         result = emit_binding_table(cmd_buffer, s,
                                     &cmd_buffer->state.binding_tables[s]);
         if (result != VK_SUCCESS)
            return result;
      }
   }

   cmd_buffer->state.descriptors_dirty &= ~dirty;

   return dirty;
}

static void
cmd_buffer_emit_descriptor_pointers(struct anv_cmd_buffer *cmd_buffer,
                                    uint32_t stages)
{
   static const uint32_t sampler_state_opcodes[] = {
      [MESA_SHADER_VERTEX]                      = 43,
      [MESA_SHADER_TESS_CTRL]                   = 44, /* HS */
      [MESA_SHADER_TESS_EVAL]                   = 45, /* DS */
      [MESA_SHADER_GEOMETRY]                    = 46,
      [MESA_SHADER_FRAGMENT]                    = 47,
      [MESA_SHADER_COMPUTE]                     = 0,
   };

   static const uint32_t binding_table_opcodes[] = {
      [MESA_SHADER_VERTEX]                      = 38,
      [MESA_SHADER_TESS_CTRL]                   = 39,
      [MESA_SHADER_TESS_EVAL]                   = 40,
      [MESA_SHADER_GEOMETRY]                    = 41,
      [MESA_SHADER_FRAGMENT]                    = 42,
      [MESA_SHADER_COMPUTE]                     = 0,
   };

   anv_foreach_stage(s, stages) {
      if (cmd_buffer->state.samplers[s].alloc_size > 0) {
         anv_batch_emit(&cmd_buffer->batch,
                        GENX(3DSTATE_SAMPLER_STATE_POINTERS_VS), ssp) {
            ssp._3DCommandSubOpcode = sampler_state_opcodes[s];
            ssp.PointertoVSSamplerState = cmd_buffer->state.samplers[s].offset;
         }
      }

      /* Always emit binding table pointers if we're asked to, since on SKL
       * this is what flushes push constants. */
      anv_batch_emit(&cmd_buffer->batch,
                     GENX(3DSTATE_BINDING_TABLE_POINTERS_VS), btp) {
         btp._3DCommandSubOpcode = binding_table_opcodes[s];
         btp.PointertoVSBindingTable = cmd_buffer->state.binding_tables[s].offset;
      }
   }
}

static uint32_t
cmd_buffer_flush_push_constants(struct anv_cmd_buffer *cmd_buffer)
{
   static const uint32_t push_constant_opcodes[] = {
      [MESA_SHADER_VERTEX]                      = 21,
      [MESA_SHADER_TESS_CTRL]                   = 25, /* HS */
      [MESA_SHADER_TESS_EVAL]                   = 26, /* DS */
      [MESA_SHADER_GEOMETRY]                    = 22,
      [MESA_SHADER_FRAGMENT]                    = 23,
      [MESA_SHADER_COMPUTE]                     = 0,
   };

   VkShaderStageFlags flushed = 0;

   anv_foreach_stage(stage, cmd_buffer->state.push_constants_dirty) {
      if (stage == MESA_SHADER_COMPUTE)
         continue;

      struct anv_state state = anv_cmd_buffer_push_constants(cmd_buffer, stage);

      if (state.offset == 0) {
         anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_CONSTANT_VS), c)
            c._3DCommandSubOpcode = push_constant_opcodes[stage];
      } else {
         anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_CONSTANT_VS), c) {
            c._3DCommandSubOpcode = push_constant_opcodes[stage],
            c.ConstantBody = (struct GENX(3DSTATE_CONSTANT_BODY)) {
#if GEN_GEN >= 9
               .PointerToConstantBuffer2 = { &cmd_buffer->device->dynamic_state_block_pool.bo, state.offset },
               .ConstantBuffer2ReadLength = DIV_ROUND_UP(state.alloc_size, 32),
#else
               .PointerToConstantBuffer0 = { .offset = state.offset },
               .ConstantBuffer0ReadLength = DIV_ROUND_UP(state.alloc_size, 32),
#endif
            };
         }
      }

      flushed |= mesa_to_vk_shader_stage(stage);
   }

   cmd_buffer->state.push_constants_dirty &= ~VK_SHADER_STAGE_ALL_GRAPHICS;

   return flushed;
}

void
genX(cmd_buffer_flush_state)(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_pipeline *pipeline = cmd_buffer->state.pipeline;
   uint32_t *p;

   uint32_t vb_emit = cmd_buffer->state.vb_dirty & pipeline->vb_used;

   assert((pipeline->active_stages & VK_SHADER_STAGE_COMPUTE_BIT) == 0);

   genX(cmd_buffer_config_l3)(cmd_buffer, pipeline->urb.l3_config);

   genX(flush_pipeline_select_3d)(cmd_buffer);

   if (vb_emit) {
      const uint32_t num_buffers = __builtin_popcount(vb_emit);
      const uint32_t num_dwords = 1 + num_buffers * 4;

      p = anv_batch_emitn(&cmd_buffer->batch, num_dwords,
                          GENX(3DSTATE_VERTEX_BUFFERS));
      uint32_t vb, i = 0;
      for_each_bit(vb, vb_emit) {
         struct anv_buffer *buffer = cmd_buffer->state.vertex_bindings[vb].buffer;
         uint32_t offset = cmd_buffer->state.vertex_bindings[vb].offset;

         struct GENX(VERTEX_BUFFER_STATE) state = {
            .VertexBufferIndex = vb,

#if GEN_GEN >= 8
            .MemoryObjectControlState = GENX(MOCS),
#else
            .BufferAccessType = pipeline->instancing_enable[vb] ? INSTANCEDATA : VERTEXDATA,
            .InstanceDataStepRate = 1,
            .VertexBufferMemoryObjectControlState = GENX(MOCS),
#endif

            .AddressModifyEnable = true,
            .BufferPitch = pipeline->binding_stride[vb],
            .BufferStartingAddress = { buffer->bo, buffer->offset + offset },

#if GEN_GEN >= 8
            .BufferSize = buffer->size - offset
#else
            .EndAddress = { buffer->bo, buffer->offset + buffer->size - 1},
#endif
         };

         GENX(VERTEX_BUFFER_STATE_pack)(&cmd_buffer->batch, &p[1 + i * 4], &state);
         i++;
      }
   }

   cmd_buffer->state.vb_dirty &= ~vb_emit;

   if (cmd_buffer->state.dirty & ANV_CMD_DIRTY_PIPELINE) {
      anv_batch_emit_batch(&cmd_buffer->batch, &pipeline->batch);

      /* The exact descriptor layout is pulled from the pipeline, so we need
       * to re-emit binding tables on every pipeline change.
       */
      cmd_buffer->state.descriptors_dirty |=
         cmd_buffer->state.pipeline->active_stages;

      /* If the pipeline changed, we may need to re-allocate push constant
       * space in the URB.
       */
      cmd_buffer_alloc_push_constants(cmd_buffer);
   }

#if GEN_GEN <= 7
   if (cmd_buffer->state.descriptors_dirty & VK_SHADER_STAGE_VERTEX_BIT ||
       cmd_buffer->state.push_constants_dirty & VK_SHADER_STAGE_VERTEX_BIT) {
      /* From the IVB PRM Vol. 2, Part 1, Section 3.2.1:
       *
       *    "A PIPE_CONTROL with Post-Sync Operation set to 1h and a depth
       *    stall needs to be sent just prior to any 3DSTATE_VS,
       *    3DSTATE_URB_VS, 3DSTATE_CONSTANT_VS,
       *    3DSTATE_BINDING_TABLE_POINTER_VS,
       *    3DSTATE_SAMPLER_STATE_POINTER_VS command.  Only one
       *    PIPE_CONTROL needs to be sent before any combination of VS
       *    associated 3DSTATE."
       */
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
         pc.DepthStallEnable  = true;
         pc.PostSyncOperation = WriteImmediateData;
         pc.Address           =
            (struct anv_address) { &cmd_buffer->device->workaround_bo, 0 };
      }
   }
#endif

   /* Render targets live in the same binding table as fragment descriptors */
   if (cmd_buffer->state.dirty & ANV_CMD_DIRTY_RENDER_TARGETS)
      cmd_buffer->state.descriptors_dirty |= VK_SHADER_STAGE_FRAGMENT_BIT;

   /* We emit the binding tables and sampler tables first, then emit push
    * constants and then finally emit binding table and sampler table
    * pointers.  It has to happen in this order, since emitting the binding
    * tables may change the push constants (in case of storage images). After
    * emitting push constants, on SKL+ we have to emit the corresponding
    * 3DSTATE_BINDING_TABLE_POINTER_* for the push constants to take effect.
    */
   uint32_t dirty = 0;
   if (cmd_buffer->state.descriptors_dirty)
      dirty = flush_descriptor_sets(cmd_buffer);

   if (cmd_buffer->state.push_constants_dirty) {
#if GEN_GEN >= 9
      /* On Sky Lake and later, the binding table pointers commands are
       * what actually flush the changes to push constant state so we need
       * to dirty them so they get re-emitted below.
       */
      dirty |= cmd_buffer_flush_push_constants(cmd_buffer);
#else
      cmd_buffer_flush_push_constants(cmd_buffer);
#endif
   }

   if (dirty)
      cmd_buffer_emit_descriptor_pointers(cmd_buffer, dirty);

   if (cmd_buffer->state.dirty & ANV_CMD_DIRTY_DYNAMIC_VIEWPORT)
      gen8_cmd_buffer_emit_viewport(cmd_buffer);

   if (cmd_buffer->state.dirty & (ANV_CMD_DIRTY_DYNAMIC_VIEWPORT |
                                  ANV_CMD_DIRTY_PIPELINE)) {
      gen8_cmd_buffer_emit_depth_viewport(cmd_buffer,
                                          pipeline->depth_clamp_enable);
   }

   if (cmd_buffer->state.dirty & ANV_CMD_DIRTY_DYNAMIC_SCISSOR)
      gen7_cmd_buffer_emit_scissor(cmd_buffer);

   genX(cmd_buffer_flush_dynamic_state)(cmd_buffer);

   genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);
}

static void
emit_base_vertex_instance_bo(struct anv_cmd_buffer *cmd_buffer,
                             struct anv_bo *bo, uint32_t offset)
{
   uint32_t *p = anv_batch_emitn(&cmd_buffer->batch, 5,
                                 GENX(3DSTATE_VERTEX_BUFFERS));

   GENX(VERTEX_BUFFER_STATE_pack)(&cmd_buffer->batch, p + 1,
      &(struct GENX(VERTEX_BUFFER_STATE)) {
         .VertexBufferIndex = 32, /* Reserved for this */
         .AddressModifyEnable = true,
         .BufferPitch = 0,
#if (GEN_GEN >= 8)
         .MemoryObjectControlState = GENX(MOCS),
         .BufferStartingAddress = { bo, offset },
         .BufferSize = 8
#else
         .VertexBufferMemoryObjectControlState = GENX(MOCS),
         .BufferStartingAddress = { bo, offset },
         .EndAddress = { bo, offset + 8 },
#endif
      });
}

static void
emit_base_vertex_instance(struct anv_cmd_buffer *cmd_buffer,
                          uint32_t base_vertex, uint32_t base_instance)
{
   struct anv_state id_state =
      anv_cmd_buffer_alloc_dynamic_state(cmd_buffer, 8, 4);

   ((uint32_t *)id_state.map)[0] = base_vertex;
   ((uint32_t *)id_state.map)[1] = base_instance;

   if (!cmd_buffer->device->info.has_llc)
      anv_state_clflush(id_state);

   emit_base_vertex_instance_bo(cmd_buffer,
      &cmd_buffer->device->dynamic_state_block_pool.bo, id_state.offset);
}

void genX(CmdDraw)(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    vertexCount,
    uint32_t                                    instanceCount,
    uint32_t                                    firstVertex,
    uint32_t                                    firstInstance)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   struct anv_pipeline *pipeline = cmd_buffer->state.pipeline;
   const struct brw_vs_prog_data *vs_prog_data = get_vs_prog_data(pipeline);

   genX(cmd_buffer_flush_state)(cmd_buffer);

   if (vs_prog_data->uses_basevertex || vs_prog_data->uses_baseinstance)
      emit_base_vertex_instance(cmd_buffer, firstVertex, firstInstance);

   anv_batch_emit(&cmd_buffer->batch, GENX(3DPRIMITIVE), prim) {
      prim.VertexAccessType         = SEQUENTIAL;
      prim.PrimitiveTopologyType    = pipeline->topology;
      prim.VertexCountPerInstance   = vertexCount;
      prim.StartVertexLocation      = firstVertex;
      prim.InstanceCount            = instanceCount;
      prim.StartInstanceLocation    = firstInstance;
      prim.BaseVertexLocation       = 0;
   }
}

void genX(CmdDrawIndexed)(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    indexCount,
    uint32_t                                    instanceCount,
    uint32_t                                    firstIndex,
    int32_t                                     vertexOffset,
    uint32_t                                    firstInstance)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   struct anv_pipeline *pipeline = cmd_buffer->state.pipeline;
   const struct brw_vs_prog_data *vs_prog_data = get_vs_prog_data(pipeline);

   genX(cmd_buffer_flush_state)(cmd_buffer);

   if (vs_prog_data->uses_basevertex || vs_prog_data->uses_baseinstance)
      emit_base_vertex_instance(cmd_buffer, vertexOffset, firstInstance);

   anv_batch_emit(&cmd_buffer->batch, GENX(3DPRIMITIVE), prim) {
      prim.VertexAccessType         = RANDOM;
      prim.PrimitiveTopologyType    = pipeline->topology;
      prim.VertexCountPerInstance   = indexCount;
      prim.StartVertexLocation      = firstIndex;
      prim.InstanceCount            = instanceCount;
      prim.StartInstanceLocation    = firstInstance;
      prim.BaseVertexLocation       = vertexOffset;
   }
}

/* Auto-Draw / Indirect Registers */
#define GEN7_3DPRIM_END_OFFSET          0x2420
#define GEN7_3DPRIM_START_VERTEX        0x2430
#define GEN7_3DPRIM_VERTEX_COUNT        0x2434
#define GEN7_3DPRIM_INSTANCE_COUNT      0x2438
#define GEN7_3DPRIM_START_INSTANCE      0x243C
#define GEN7_3DPRIM_BASE_VERTEX         0x2440

void genX(CmdDrawIndirect)(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    _buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_buffer, buffer, _buffer);
   struct anv_pipeline *pipeline = cmd_buffer->state.pipeline;
   const struct brw_vs_prog_data *vs_prog_data = get_vs_prog_data(pipeline);
   struct anv_bo *bo = buffer->bo;
   uint32_t bo_offset = buffer->offset + offset;

   genX(cmd_buffer_flush_state)(cmd_buffer);

   if (vs_prog_data->uses_basevertex || vs_prog_data->uses_baseinstance)
      emit_base_vertex_instance_bo(cmd_buffer, bo, bo_offset + 8);

   emit_lrm(&cmd_buffer->batch, GEN7_3DPRIM_VERTEX_COUNT, bo, bo_offset);
   emit_lrm(&cmd_buffer->batch, GEN7_3DPRIM_INSTANCE_COUNT, bo, bo_offset + 4);
   emit_lrm(&cmd_buffer->batch, GEN7_3DPRIM_START_VERTEX, bo, bo_offset + 8);
   emit_lrm(&cmd_buffer->batch, GEN7_3DPRIM_START_INSTANCE, bo, bo_offset + 12);
   emit_lri(&cmd_buffer->batch, GEN7_3DPRIM_BASE_VERTEX, 0);

   anv_batch_emit(&cmd_buffer->batch, GENX(3DPRIMITIVE), prim) {
      prim.IndirectParameterEnable  = true;
      prim.VertexAccessType         = SEQUENTIAL;
      prim.PrimitiveTopologyType    = pipeline->topology;
   }
}

void genX(CmdDrawIndexedIndirect)(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    _buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_buffer, buffer, _buffer);
   struct anv_pipeline *pipeline = cmd_buffer->state.pipeline;
   const struct brw_vs_prog_data *vs_prog_data = get_vs_prog_data(pipeline);
   struct anv_bo *bo = buffer->bo;
   uint32_t bo_offset = buffer->offset + offset;

   genX(cmd_buffer_flush_state)(cmd_buffer);

   /* TODO: We need to stomp base vertex to 0 somehow */
   if (vs_prog_data->uses_basevertex || vs_prog_data->uses_baseinstance)
      emit_base_vertex_instance_bo(cmd_buffer, bo, bo_offset + 12);

   emit_lrm(&cmd_buffer->batch, GEN7_3DPRIM_VERTEX_COUNT, bo, bo_offset);
   emit_lrm(&cmd_buffer->batch, GEN7_3DPRIM_INSTANCE_COUNT, bo, bo_offset + 4);
   emit_lrm(&cmd_buffer->batch, GEN7_3DPRIM_START_VERTEX, bo, bo_offset + 8);
   emit_lrm(&cmd_buffer->batch, GEN7_3DPRIM_BASE_VERTEX, bo, bo_offset + 12);
   emit_lrm(&cmd_buffer->batch, GEN7_3DPRIM_START_INSTANCE, bo, bo_offset + 16);

   anv_batch_emit(&cmd_buffer->batch, GENX(3DPRIMITIVE), prim) {
      prim.IndirectParameterEnable  = true;
      prim.VertexAccessType         = RANDOM;
      prim.PrimitiveTopologyType    = pipeline->topology;
   }
}

static VkResult
flush_compute_descriptor_set(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_pipeline *pipeline = cmd_buffer->state.compute_pipeline;
   struct anv_state surfaces = { 0, }, samplers = { 0, };
   VkResult result;

   result = emit_binding_table(cmd_buffer, MESA_SHADER_COMPUTE, &surfaces);
   if (result != VK_SUCCESS) {
      assert(result == VK_ERROR_OUT_OF_DEVICE_MEMORY);
      result = anv_cmd_buffer_new_binding_table_block(cmd_buffer);
      assert(result == VK_SUCCESS);

      /* Re-emit state base addresses so we get the new surface state base
       * address before we start emitting binding tables etc.
       */
      genX(cmd_buffer_emit_state_base_address)(cmd_buffer);

      result = emit_binding_table(cmd_buffer, MESA_SHADER_COMPUTE, &surfaces);
      assert(result == VK_SUCCESS);
   }

   result = emit_samplers(cmd_buffer, MESA_SHADER_COMPUTE, &samplers);
   assert(result == VK_SUCCESS);

   uint32_t iface_desc_data_dw[GENX(INTERFACE_DESCRIPTOR_DATA_length)];
   struct GENX(INTERFACE_DESCRIPTOR_DATA) desc = {
      .BindingTablePointer = surfaces.offset,
      .SamplerStatePointer = samplers.offset,
   };
   GENX(INTERFACE_DESCRIPTOR_DATA_pack)(NULL, iface_desc_data_dw, &desc);

   struct anv_state state =
      anv_cmd_buffer_merge_dynamic(cmd_buffer, iface_desc_data_dw,
                                   pipeline->interface_descriptor_data,
                                   GENX(INTERFACE_DESCRIPTOR_DATA_length),
                                   64);

   uint32_t size = GENX(INTERFACE_DESCRIPTOR_DATA_length) * sizeof(uint32_t);
   anv_batch_emit(&cmd_buffer->batch,
                  GENX(MEDIA_INTERFACE_DESCRIPTOR_LOAD), mid) {
      mid.InterfaceDescriptorTotalLength        = size;
      mid.InterfaceDescriptorDataStartAddress   = state.offset;
   }

   return VK_SUCCESS;
}

void
genX(cmd_buffer_flush_compute_state)(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_pipeline *pipeline = cmd_buffer->state.compute_pipeline;
   MAYBE_UNUSED VkResult result;

   assert(pipeline->active_stages == VK_SHADER_STAGE_COMPUTE_BIT);

   genX(cmd_buffer_config_l3)(cmd_buffer, pipeline->urb.l3_config);

   genX(flush_pipeline_select_gpgpu)(cmd_buffer);

   if (cmd_buffer->state.compute_dirty & ANV_CMD_DIRTY_PIPELINE) {
      /* From the Sky Lake PRM Vol 2a, MEDIA_VFE_STATE:
       *
       *    "A stalling PIPE_CONTROL is required before MEDIA_VFE_STATE unless
       *    the only bits that are changed are scoreboard related: Scoreboard
       *    Enable, Scoreboard Type, Scoreboard Mask, Scoreboard * Delta. For
       *    these scoreboard related states, a MEDIA_STATE_FLUSH is
       *    sufficient."
       */
      cmd_buffer->state.pending_pipe_bits |= ANV_PIPE_CS_STALL_BIT;
      genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);

      anv_batch_emit_batch(&cmd_buffer->batch, &pipeline->batch);
   }

   if ((cmd_buffer->state.descriptors_dirty & VK_SHADER_STAGE_COMPUTE_BIT) ||
       (cmd_buffer->state.compute_dirty & ANV_CMD_DIRTY_PIPELINE)) {
      /* FIXME: figure out descriptors for gen7 */
      result = flush_compute_descriptor_set(cmd_buffer);
      assert(result == VK_SUCCESS);
      cmd_buffer->state.descriptors_dirty &= ~VK_SHADER_STAGE_COMPUTE_BIT;
   }

   if (cmd_buffer->state.push_constants_dirty & VK_SHADER_STAGE_COMPUTE_BIT) {
      struct anv_state push_state =
         anv_cmd_buffer_cs_push_constants(cmd_buffer);

      if (push_state.alloc_size) {
         anv_batch_emit(&cmd_buffer->batch, GENX(MEDIA_CURBE_LOAD), curbe) {
            curbe.CURBETotalDataLength    = push_state.alloc_size;
            curbe.CURBEDataStartAddress   = push_state.offset;
         }
      }
   }

   cmd_buffer->state.compute_dirty = 0;

   genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);
}

#if GEN_GEN == 7

static bool
verify_cmd_parser(const struct anv_device *device,
                  int required_version,
                  const char *function)
{
   if (device->instance->physicalDevice.cmd_parser_version < required_version) {
      vk_errorf(VK_ERROR_FEATURE_NOT_PRESENT,
                "cmd parser version %d is required for %s",
                required_version, function);
      return false;
   } else {
      return true;
   }
}

#endif

void genX(CmdDispatch)(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    x,
    uint32_t                                    y,
    uint32_t                                    z)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   struct anv_pipeline *pipeline = cmd_buffer->state.compute_pipeline;
   const struct brw_cs_prog_data *prog_data = get_cs_prog_data(pipeline);

   if (prog_data->uses_num_work_groups) {
      struct anv_state state =
         anv_cmd_buffer_alloc_dynamic_state(cmd_buffer, 12, 4);
      uint32_t *sizes = state.map;
      sizes[0] = x;
      sizes[1] = y;
      sizes[2] = z;
      if (!cmd_buffer->device->info.has_llc)
         anv_state_clflush(state);
      cmd_buffer->state.num_workgroups_offset = state.offset;
      cmd_buffer->state.num_workgroups_bo =
         &cmd_buffer->device->dynamic_state_block_pool.bo;
   }

   genX(cmd_buffer_flush_compute_state)(cmd_buffer);

   anv_batch_emit(&cmd_buffer->batch, GENX(GPGPU_WALKER), ggw) {
      ggw.SIMDSize                     = prog_data->simd_size / 16;
      ggw.ThreadDepthCounterMaximum    = 0;
      ggw.ThreadHeightCounterMaximum   = 0;
      ggw.ThreadWidthCounterMaximum    = prog_data->threads - 1;
      ggw.ThreadGroupIDXDimension      = x;
      ggw.ThreadGroupIDYDimension      = y;
      ggw.ThreadGroupIDZDimension      = z;
      ggw.RightExecutionMask           = pipeline->cs_right_mask;
      ggw.BottomExecutionMask          = 0xffffffff;
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(MEDIA_STATE_FLUSH), msf);
}

#define GPGPU_DISPATCHDIMX 0x2500
#define GPGPU_DISPATCHDIMY 0x2504
#define GPGPU_DISPATCHDIMZ 0x2508

#define MI_PREDICATE_SRC0  0x2400
#define MI_PREDICATE_SRC1  0x2408

void genX(CmdDispatchIndirect)(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    _buffer,
    VkDeviceSize                                offset)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_buffer, buffer, _buffer);
   struct anv_pipeline *pipeline = cmd_buffer->state.compute_pipeline;
   const struct brw_cs_prog_data *prog_data = get_cs_prog_data(pipeline);
   struct anv_bo *bo = buffer->bo;
   uint32_t bo_offset = buffer->offset + offset;
   struct anv_batch *batch = &cmd_buffer->batch;

#if GEN_GEN == 7
   /* Linux 4.4 added command parser version 5 which allows the GPGPU
    * indirect dispatch registers to be written.
    */
   if (!verify_cmd_parser(cmd_buffer->device, 5, "vkCmdDispatchIndirect"))
      return;
#endif

   if (prog_data->uses_num_work_groups) {
      cmd_buffer->state.num_workgroups_offset = bo_offset;
      cmd_buffer->state.num_workgroups_bo = bo;
   }

   genX(cmd_buffer_flush_compute_state)(cmd_buffer);

   emit_lrm(batch, GPGPU_DISPATCHDIMX, bo, bo_offset);
   emit_lrm(batch, GPGPU_DISPATCHDIMY, bo, bo_offset + 4);
   emit_lrm(batch, GPGPU_DISPATCHDIMZ, bo, bo_offset + 8);

#if GEN_GEN <= 7
   /* Clear upper 32-bits of SRC0 and all 64-bits of SRC1 */
   emit_lri(batch, MI_PREDICATE_SRC0 + 4, 0);
   emit_lri(batch, MI_PREDICATE_SRC1 + 0, 0);
   emit_lri(batch, MI_PREDICATE_SRC1 + 4, 0);

   /* Load compute_dispatch_indirect_x_size into SRC0 */
   emit_lrm(batch, MI_PREDICATE_SRC0, bo, bo_offset + 0);

   /* predicate = (compute_dispatch_indirect_x_size == 0); */
   anv_batch_emit(batch, GENX(MI_PREDICATE), mip) {
      mip.LoadOperation    = LOAD_LOAD;
      mip.CombineOperation = COMBINE_SET;
      mip.CompareOperation = COMPARE_SRCS_EQUAL;
   }

   /* Load compute_dispatch_indirect_y_size into SRC0 */
   emit_lrm(batch, MI_PREDICATE_SRC0, bo, bo_offset + 4);

   /* predicate |= (compute_dispatch_indirect_y_size == 0); */
   anv_batch_emit(batch, GENX(MI_PREDICATE), mip) {
      mip.LoadOperation    = LOAD_LOAD;
      mip.CombineOperation = COMBINE_OR;
      mip.CompareOperation = COMPARE_SRCS_EQUAL;
   }

   /* Load compute_dispatch_indirect_z_size into SRC0 */
   emit_lrm(batch, MI_PREDICATE_SRC0, bo, bo_offset + 8);

   /* predicate |= (compute_dispatch_indirect_z_size == 0); */
   anv_batch_emit(batch, GENX(MI_PREDICATE), mip) {
      mip.LoadOperation    = LOAD_LOAD;
      mip.CombineOperation = COMBINE_OR;
      mip.CompareOperation = COMPARE_SRCS_EQUAL;
   }

   /* predicate = !predicate; */
#define COMPARE_FALSE                           1
   anv_batch_emit(batch, GENX(MI_PREDICATE), mip) {
      mip.LoadOperation    = LOAD_LOADINV;
      mip.CombineOperation = COMBINE_OR;
      mip.CompareOperation = COMPARE_FALSE;
   }
#endif

   anv_batch_emit(batch, GENX(GPGPU_WALKER), ggw) {
      ggw.IndirectParameterEnable      = true;
      ggw.PredicateEnable              = GEN_GEN <= 7;
      ggw.SIMDSize                     = prog_data->simd_size / 16;
      ggw.ThreadDepthCounterMaximum    = 0;
      ggw.ThreadHeightCounterMaximum   = 0;
      ggw.ThreadWidthCounterMaximum    = prog_data->threads - 1;
      ggw.RightExecutionMask           = pipeline->cs_right_mask;
      ggw.BottomExecutionMask          = 0xffffffff;
   }

   anv_batch_emit(batch, GENX(MEDIA_STATE_FLUSH), msf);
}

static void
flush_pipeline_before_pipeline_select(struct anv_cmd_buffer *cmd_buffer,
                                      uint32_t pipeline)
{
#if GEN_GEN >= 8 && GEN_GEN < 10
   /* From the Broadwell PRM, Volume 2a: Instructions, PIPELINE_SELECT:
    *
    *   Software must clear the COLOR_CALC_STATE Valid field in
    *   3DSTATE_CC_STATE_POINTERS command prior to send a PIPELINE_SELECT
    *   with Pipeline Select set to GPGPU.
    *
    * The internal hardware docs recommend the same workaround for Gen9
    * hardware too.
    */
   if (pipeline == GPGPU)
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_CC_STATE_POINTERS), t);
#endif

   /* From "BXML » GT » MI » vol1a GPU Overview » [Instruction]
    * PIPELINE_SELECT [DevBWR+]":
    *
    *   Project: DEVSNB+
    *
    *   Software must ensure all the write caches are flushed through a
    *   stalling PIPE_CONTROL command followed by another PIPE_CONTROL
    *   command to invalidate read only caches prior to programming
    *   MI_PIPELINE_SELECT command to change the Pipeline Select Mode.
    */
   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.RenderTargetCacheFlushEnable  = true;
      pc.DepthCacheFlushEnable         = true;
      pc.DCFlushEnable                 = true;
      pc.PostSyncOperation             = NoWrite;
      pc.CommandStreamerStallEnable    = true;
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.TextureCacheInvalidationEnable   = true;
      pc.ConstantCacheInvalidationEnable  = true;
      pc.StateCacheInvalidationEnable     = true;
      pc.InstructionCacheInvalidateEnable = true;
      pc.PostSyncOperation                = NoWrite;
   }
}

void
genX(flush_pipeline_select_3d)(struct anv_cmd_buffer *cmd_buffer)
{
   if (cmd_buffer->state.current_pipeline != _3D) {
      flush_pipeline_before_pipeline_select(cmd_buffer, _3D);

      anv_batch_emit(&cmd_buffer->batch, GENX(PIPELINE_SELECT), ps) {
#if GEN_GEN >= 9
         ps.MaskBits = 3;
#endif
         ps.PipelineSelection = _3D;
      }

      cmd_buffer->state.current_pipeline = _3D;
   }
}

void
genX(flush_pipeline_select_gpgpu)(struct anv_cmd_buffer *cmd_buffer)
{
   if (cmd_buffer->state.current_pipeline != GPGPU) {
      flush_pipeline_before_pipeline_select(cmd_buffer, GPGPU);

      anv_batch_emit(&cmd_buffer->batch, GENX(PIPELINE_SELECT), ps) {
#if GEN_GEN >= 9
         ps.MaskBits = 3;
#endif
         ps.PipelineSelection = GPGPU;
      }

      cmd_buffer->state.current_pipeline = GPGPU;
   }
}

void
genX(cmd_buffer_emit_gen7_depth_flush)(struct anv_cmd_buffer *cmd_buffer)
{
   if (GEN_GEN >= 8)
      return;

   /* From the Haswell PRM, documentation for 3DSTATE_DEPTH_BUFFER:
    *
    *    "Restriction: Prior to changing Depth/Stencil Buffer state (i.e., any
    *    combination of 3DSTATE_DEPTH_BUFFER, 3DSTATE_CLEAR_PARAMS,
    *    3DSTATE_STENCIL_BUFFER, 3DSTATE_HIER_DEPTH_BUFFER) SW must first
    *    issue a pipelined depth stall (PIPE_CONTROL with Depth Stall bit
    *    set), followed by a pipelined depth cache flush (PIPE_CONTROL with
    *    Depth Flush Bit set, followed by another pipelined depth stall
    *    (PIPE_CONTROL with Depth Stall Bit set), unless SW can otherwise
    *    guarantee that the pipeline from WM onwards is already flushed (e.g.,
    *    via a preceding MI_FLUSH)."
    */
   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pipe) {
      pipe.DepthStallEnable = true;
   }
   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pipe) {
      pipe.DepthCacheFlushEnable = true;
   }
   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pipe) {
      pipe.DepthStallEnable = true;
   }
}

static uint32_t
depth_stencil_surface_type(enum isl_surf_dim dim)
{
   switch (dim) {
   case ISL_SURF_DIM_1D:
      if (GEN_GEN >= 9) {
         /* From the Sky Lake PRM, 3DSTATAE_DEPTH_BUFFER::SurfaceType
          *
          *    Programming Notes:
          *    The Surface Type of the depth buffer must be the same as the
          *    Surface Type of the render target(s) (defined in
          *    SURFACE_STATE), unless either the depth buffer or render
          *    targets are SURFTYPE_NULL (see exception below for SKL).  1D
          *    surface type not allowed for depth surface and stencil surface.
          *
          *    Workaround:
          *    If depth/stencil is enabled with 1D render target,
          *    depth/stencil surface type needs to be set to 2D surface type
          *    and height set to 1. Depth will use (legacy) TileY and stencil
          *    will use TileW. For this case only, the Surface Type of the
          *    depth buffer can be 2D while the Surface Type of the render
          *    target(s) are 1D, representing an exception to a programming
          *    note above.
          */
         return SURFTYPE_2D;
      } else {
         return SURFTYPE_1D;
      }
   case ISL_SURF_DIM_2D:
      return SURFTYPE_2D;
   case ISL_SURF_DIM_3D:
      if (GEN_GEN >= 9) {
         /* The Sky Lake docs list the value for 3D as "Reserved".  However,
          * they have the exact same layout as 2D arrays on gen9+, so we can
          * just use 2D here.
          */
         return SURFTYPE_2D;
      } else {
         return SURFTYPE_3D;
      }
   default:
      unreachable("Invalid surface dimension");
   }
}

static void
cmd_buffer_emit_depth_stencil(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_device *device = cmd_buffer->device;
   const struct anv_framebuffer *fb = cmd_buffer->state.framebuffer;
   const struct anv_image_view *iview =
      anv_cmd_buffer_get_depth_stencil_view(cmd_buffer);
   const struct anv_image *image = iview ? iview->image : NULL;
   const bool has_depth = image && (image->aspects & VK_IMAGE_ASPECT_DEPTH_BIT);
   const uint32_t ds = cmd_buffer->state.subpass->depth_stencil_attachment;
   const bool has_hiz = image != NULL &&
      cmd_buffer->state.attachments[ds].aux_usage == ISL_AUX_USAGE_HIZ;
   const bool has_stencil =
      image && (image->aspects & VK_IMAGE_ASPECT_STENCIL_BIT);

   /* FIXME: Implement the PMA stall W/A */
   /* FIXME: Width and Height are wrong */

   genX(cmd_buffer_emit_gen7_depth_flush)(cmd_buffer);

   /* Emit 3DSTATE_DEPTH_BUFFER */
   if (has_depth) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_DEPTH_BUFFER), db) {
         db.SurfaceType                   =
            depth_stencil_surface_type(image->depth_surface.isl.dim);
         db.DepthWriteEnable              = true;
         db.StencilWriteEnable            = has_stencil;
         db.HierarchicalDepthBufferEnable = has_hiz;

         db.SurfaceFormat = isl_surf_get_depth_format(&device->isl_dev,
                                                      &image->depth_surface.isl);

         db.SurfaceBaseAddress = (struct anv_address) {
            .bo = image->bo,
            .offset = image->offset + image->depth_surface.offset,
         };
         db.DepthBufferObjectControlState = GENX(MOCS);

         db.SurfacePitch         = image->depth_surface.isl.row_pitch - 1;
         db.Height               = image->extent.height - 1;
         db.Width                = image->extent.width - 1;
         db.LOD                  = iview->isl.base_level;
         db.MinimumArrayElement  = iview->isl.base_array_layer;

         assert(image->depth_surface.isl.dim != ISL_SURF_DIM_3D);
         db.Depth =
         db.RenderTargetViewExtent =
            iview->isl.array_len - iview->isl.base_array_layer - 1;

#if GEN_GEN >= 8
         db.SurfaceQPitch =
            isl_surf_get_array_pitch_el_rows(&image->depth_surface.isl) >> 2;
#endif
      }
   } else {
      /* Even when no depth buffer is present, the hardware requires that
       * 3DSTATE_DEPTH_BUFFER be programmed correctly. The Broadwell PRM says:
       *
       *    If a null depth buffer is bound, the driver must instead bind depth as:
       *       3DSTATE_DEPTH.SurfaceType = SURFTYPE_2D
       *       3DSTATE_DEPTH.Width = 1
       *       3DSTATE_DEPTH.Height = 1
       *       3DSTATE_DEPTH.SuraceFormat = D16_UNORM
       *       3DSTATE_DEPTH.SurfaceBaseAddress = 0
       *       3DSTATE_DEPTH.HierarchicalDepthBufferEnable = 0
       *       3DSTATE_WM_DEPTH_STENCIL.DepthTestEnable = 0
       *       3DSTATE_WM_DEPTH_STENCIL.DepthBufferWriteEnable = 0
       *
       * The PRM is wrong, though. The width and height must be programmed to
       * actual framebuffer's width and height, even when neither depth buffer
       * nor stencil buffer is present.  Also, D16_UNORM is not allowed to
       * be combined with a stencil buffer so we use D32_FLOAT instead.
       */
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_DEPTH_BUFFER), db) {
         if (has_stencil) {
            db.SurfaceType       =
               depth_stencil_surface_type(image->stencil_surface.isl.dim);
         } else {
            db.SurfaceType       = SURFTYPE_2D;
         }
         db.SurfaceFormat        = D32_FLOAT;
         db.Width                = MAX2(fb->width, 1) - 1;
         db.Height               = MAX2(fb->height, 1) - 1;
         db.StencilWriteEnable   = has_stencil;
      }
   }

   if (has_hiz) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_HIER_DEPTH_BUFFER), hdb) {
         hdb.HierarchicalDepthBufferObjectControlState = GENX(MOCS);
         hdb.SurfacePitch = image->aux_surface.isl.row_pitch - 1;
         hdb.SurfaceBaseAddress = (struct anv_address) {
            .bo = image->bo,
            .offset = image->offset + image->aux_surface.offset,
         };
#if GEN_GEN >= 8
         /* From the SKL PRM Vol2a:
          *
          *    The interpretation of this field is dependent on Surface Type
          *    as follows:
          *    - SURFTYPE_1D: distance in pixels between array slices
          *    - SURFTYPE_2D/CUBE: distance in rows between array slices
          *    - SURFTYPE_3D: distance in rows between R - slices
          *
          * Unfortunately, the docs aren't 100% accurate here.  They fail to
          * mention that the 1-D rule only applies to linear 1-D images.
          * Since depth and HiZ buffers are always tiled, they are treated as
          * 2-D images.  Prior to Sky Lake, this field is always in rows.
          */
         hdb.SurfaceQPitch =
            isl_surf_get_array_pitch_sa_rows(&image->aux_surface.isl) >> 2;
#endif
      }
   } else {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_HIER_DEPTH_BUFFER), hdb);
   }

   /* Emit 3DSTATE_STENCIL_BUFFER */
   if (has_stencil) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_STENCIL_BUFFER), sb) {
#if GEN_GEN >= 8 || GEN_IS_HASWELL
         sb.StencilBufferEnable = true;
#endif
         sb.StencilBufferObjectControlState = GENX(MOCS);

         sb.SurfacePitch = image->stencil_surface.isl.row_pitch - 1;

#if GEN_GEN >= 8
         sb.SurfaceQPitch = isl_surf_get_array_pitch_el_rows(&image->stencil_surface.isl) >> 2;
#endif
         sb.SurfaceBaseAddress = (struct anv_address) {
            .bo = image->bo,
            .offset = image->offset + image->stencil_surface.offset,
         };
      }
   } else {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_STENCIL_BUFFER), sb);
   }

   /* From the IVB PRM Vol2P1, 11.5.5.4 3DSTATE_CLEAR_PARAMS:
    *
    *    3DSTATE_CLEAR_PARAMS must always be programmed in the along with
    *    the other Depth/Stencil state commands(i.e. 3DSTATE_DEPTH_BUFFER,
    *    3DSTATE_STENCIL_BUFFER, or 3DSTATE_HIER_DEPTH_BUFFER)
    *
    * Testing also shows that some variant of this restriction may exist HSW+.
    * On BDW+, it is not possible to emit 2 of these packets consecutively when
    * both have DepthClearValueValid set. An analysis of such state programming
    * on SKL showed that the GPU doesn't register the latter packet's clear
    * value.
    */
   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_CLEAR_PARAMS), cp) {
      if (has_hiz) {
         cp.DepthClearValueValid = true;
         cp.DepthClearValue = ANV_HZ_FC_VAL;
      }
   }
}

static void
genX(cmd_buffer_set_subpass)(struct anv_cmd_buffer *cmd_buffer,
                             struct anv_subpass *subpass)
{
   cmd_buffer->state.subpass = subpass;

   cmd_buffer->state.dirty |= ANV_CMD_DIRTY_RENDER_TARGETS;

   const struct anv_image_view *iview =
      anv_cmd_buffer_get_depth_stencil_view(cmd_buffer);

   if (iview && iview->image->aux_usage == ISL_AUX_USAGE_HIZ) {
      const uint32_t ds = subpass->depth_stencil_attachment;
      transition_depth_buffer(cmd_buffer, iview->image,
                              cmd_buffer->state.attachments[ds].current_layout,
                              cmd_buffer->state.subpass->depth_stencil_layout);
      cmd_buffer->state.attachments[ds].current_layout =
         cmd_buffer->state.subpass->depth_stencil_layout;
      cmd_buffer->state.attachments[ds].aux_usage =
         layout_to_hiz_usage(cmd_buffer->state.subpass->depth_stencil_layout,
                             iview->image->samples);
   }

   cmd_buffer_emit_depth_stencil(cmd_buffer);

   anv_cmd_buffer_clear_subpass(cmd_buffer);
}

void genX(CmdBeginRenderPass)(
    VkCommandBuffer                             commandBuffer,
    const VkRenderPassBeginInfo*                pRenderPassBegin,
    VkSubpassContents                           contents)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_render_pass, pass, pRenderPassBegin->renderPass);
   ANV_FROM_HANDLE(anv_framebuffer, framebuffer, pRenderPassBegin->framebuffer);

   cmd_buffer->state.framebuffer = framebuffer;
   cmd_buffer->state.pass = pass;
   cmd_buffer->state.render_area = pRenderPassBegin->renderArea;
   genX(cmd_buffer_setup_attachments)(cmd_buffer, pass, pRenderPassBegin);

   genX(flush_pipeline_select_3d)(cmd_buffer);

   genX(cmd_buffer_set_subpass)(cmd_buffer, pass->subpasses);
}

void genX(CmdNextSubpass)(
    VkCommandBuffer                             commandBuffer,
    VkSubpassContents                           contents)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   assert(cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY);

   const struct anv_image_view *iview =
      anv_cmd_buffer_get_depth_stencil_view(cmd_buffer);

   if (iview && iview->image->aux_usage == ISL_AUX_USAGE_HIZ) {
      const uint32_t ds = cmd_buffer->state.subpass->depth_stencil_attachment;

      if (cmd_buffer->state.subpass - cmd_buffer->state.pass->subpasses ==
          cmd_buffer->state.pass->attachments[ds].last_subpass_idx) {
         transition_depth_buffer(cmd_buffer, iview->image,
                                 cmd_buffer->state.attachments[ds].current_layout,
                                 cmd_buffer->state.pass->attachments[ds].final_layout);
      }
   }

   anv_cmd_buffer_resolve_subpass(cmd_buffer);
   genX(cmd_buffer_set_subpass)(cmd_buffer, cmd_buffer->state.subpass + 1);
}

void genX(CmdEndRenderPass)(
    VkCommandBuffer                             commandBuffer)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   const struct anv_image_view *iview =
      anv_cmd_buffer_get_depth_stencil_view(cmd_buffer);

   if (iview && iview->image->aux_usage == ISL_AUX_USAGE_HIZ) {
      const uint32_t ds = cmd_buffer->state.subpass->depth_stencil_attachment;

      if (cmd_buffer->state.subpass - cmd_buffer->state.pass->subpasses ==
          cmd_buffer->state.pass->attachments[ds].last_subpass_idx) {
         transition_depth_buffer(cmd_buffer, iview->image,
                                 cmd_buffer->state.attachments[ds].current_layout,
                                 cmd_buffer->state.pass->attachments[ds].final_layout);
      }
   }

   anv_cmd_buffer_resolve_subpass(cmd_buffer);

#ifndef NDEBUG
   anv_dump_add_framebuffer(cmd_buffer, cmd_buffer->state.framebuffer);
#endif
}

static void
emit_ps_depth_count(struct anv_cmd_buffer *cmd_buffer,
                    struct anv_bo *bo, uint32_t offset)
{
   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.DestinationAddressType  = DAT_PPGTT;
      pc.PostSyncOperation       = WritePSDepthCount;
      pc.DepthStallEnable        = true;
      pc.Address                 = (struct anv_address) { bo, offset };

      if (GEN_GEN == 9 && cmd_buffer->device->info.gt == 4)
         pc.CommandStreamerStallEnable = true;
   }
}

static void
emit_query_availability(struct anv_cmd_buffer *cmd_buffer,
                        struct anv_bo *bo, uint32_t offset)
{
   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.DestinationAddressType  = DAT_PPGTT;
      pc.PostSyncOperation       = WriteImmediateData;
      pc.Address                 = (struct anv_address) { bo, offset };
      pc.ImmediateData           = 1;
   }
}

void genX(CmdResetQueryPool)(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_query_pool, pool, queryPool);

   for (uint32_t i = 0; i < queryCount; i++) {
      switch (pool->type) {
      case VK_QUERY_TYPE_OCCLUSION:
      case VK_QUERY_TYPE_TIMESTAMP: {
         anv_batch_emit(&cmd_buffer->batch, GENX(MI_STORE_DATA_IMM), sdm) {
            sdm.Address = (struct anv_address) {
               .bo = &pool->bo,
               .offset = (firstQuery + i) * sizeof(struct anv_query_pool_slot) +
                         offsetof(struct anv_query_pool_slot, available),
            };
            sdm.DataDWord0 = 0;
            sdm.DataDWord1 = 0;
         }
         break;
      }
      default:
         assert(!"Invalid query type");
      }
   }
}

void genX(CmdBeginQuery)(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query,
    VkQueryControlFlags                         flags)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_query_pool, pool, queryPool);

   /* Workaround: When meta uses the pipeline with the VS disabled, it seems
    * that the pipelining of the depth write breaks. What we see is that
    * samples from the render pass clear leaks into the first query
    * immediately after the clear. Doing a pipecontrol with a post-sync
    * operation and DepthStallEnable seems to work around the issue.
    */
   if (cmd_buffer->state.need_query_wa) {
      cmd_buffer->state.need_query_wa = false;
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
         pc.DepthCacheFlushEnable   = true;
         pc.DepthStallEnable        = true;
      }
   }

   switch (pool->type) {
   case VK_QUERY_TYPE_OCCLUSION:
      emit_ps_depth_count(cmd_buffer, &pool->bo,
                          query * sizeof(struct anv_query_pool_slot));
      break;

   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
   default:
      unreachable("");
   }
}

void genX(CmdEndQuery)(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_query_pool, pool, queryPool);

   switch (pool->type) {
   case VK_QUERY_TYPE_OCCLUSION:
      emit_ps_depth_count(cmd_buffer, &pool->bo,
                          query * sizeof(struct anv_query_pool_slot) + 8);

      emit_query_availability(cmd_buffer, &pool->bo,
                              query * sizeof(struct anv_query_pool_slot) + 16);
      break;

   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
   default:
      unreachable("");
   }
}

#define TIMESTAMP 0x2358

void genX(CmdWriteTimestamp)(
    VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlagBits                     pipelineStage,
    VkQueryPool                                 queryPool,
    uint32_t                                    query)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_query_pool, pool, queryPool);
   uint32_t offset = query * sizeof(struct anv_query_pool_slot);

   assert(pool->type == VK_QUERY_TYPE_TIMESTAMP);

   switch (pipelineStage) {
   case VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:
      anv_batch_emit(&cmd_buffer->batch, GENX(MI_STORE_REGISTER_MEM), srm) {
         srm.RegisterAddress  = TIMESTAMP;
         srm.MemoryAddress    = (struct anv_address) { &pool->bo, offset };
      }
      anv_batch_emit(&cmd_buffer->batch, GENX(MI_STORE_REGISTER_MEM), srm) {
         srm.RegisterAddress  = TIMESTAMP + 4;
         srm.MemoryAddress    = (struct anv_address) { &pool->bo, offset + 4 };
      }
      break;

   default:
      /* Everything else is bottom-of-pipe */
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
         pc.DestinationAddressType  = DAT_PPGTT;
         pc.PostSyncOperation       = WriteTimestamp;
         pc.Address = (struct anv_address) { &pool->bo, offset };

         if (GEN_GEN == 9 && cmd_buffer->device->info.gt == 4)
            pc.CommandStreamerStallEnable = true;
      }
      break;
   }

   emit_query_availability(cmd_buffer, &pool->bo, offset + 16);
}

#if GEN_GEN > 7 || GEN_IS_HASWELL

#define alu_opcode(v)   __gen_uint((v),  20, 31)
#define alu_operand1(v) __gen_uint((v),  10, 19)
#define alu_operand2(v) __gen_uint((v),   0,  9)
#define alu(opcode, operand1, operand2) \
   alu_opcode(opcode) | alu_operand1(operand1) | alu_operand2(operand2)

#define OPCODE_NOOP      0x000
#define OPCODE_LOAD      0x080
#define OPCODE_LOADINV   0x480
#define OPCODE_LOAD0     0x081
#define OPCODE_LOAD1     0x481
#define OPCODE_ADD       0x100
#define OPCODE_SUB       0x101
#define OPCODE_AND       0x102
#define OPCODE_OR        0x103
#define OPCODE_XOR       0x104
#define OPCODE_STORE     0x180
#define OPCODE_STOREINV  0x580

#define OPERAND_R0   0x00
#define OPERAND_R1   0x01
#define OPERAND_R2   0x02
#define OPERAND_R3   0x03
#define OPERAND_R4   0x04
#define OPERAND_SRCA 0x20
#define OPERAND_SRCB 0x21
#define OPERAND_ACCU 0x31
#define OPERAND_ZF   0x32
#define OPERAND_CF   0x33

#define CS_GPR(n) (0x2600 + (n) * 8)

static void
emit_load_alu_reg_u64(struct anv_batch *batch, uint32_t reg,
                      struct anv_bo *bo, uint32_t offset)
{
   anv_batch_emit(batch, GENX(MI_LOAD_REGISTER_MEM), lrm) {
      lrm.RegisterAddress  = reg,
      lrm.MemoryAddress    = (struct anv_address) { bo, offset };
   }
   anv_batch_emit(batch, GENX(MI_LOAD_REGISTER_MEM), lrm) {
      lrm.RegisterAddress  = reg + 4;
      lrm.MemoryAddress    = (struct anv_address) { bo, offset + 4 };
   }
}

static void
store_query_result(struct anv_batch *batch, uint32_t reg,
                   struct anv_bo *bo, uint32_t offset, VkQueryResultFlags flags)
{
   anv_batch_emit(batch, GENX(MI_STORE_REGISTER_MEM), srm) {
      srm.RegisterAddress  = reg;
      srm.MemoryAddress    = (struct anv_address) { bo, offset };
   }

   if (flags & VK_QUERY_RESULT_64_BIT) {
      anv_batch_emit(batch, GENX(MI_STORE_REGISTER_MEM), srm) {
         srm.RegisterAddress  = reg + 4;
         srm.MemoryAddress    = (struct anv_address) { bo, offset + 4 };
      }
   }
}

void genX(CmdCopyQueryPoolResults)(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount,
    VkBuffer                                    destBuffer,
    VkDeviceSize                                destOffset,
    VkDeviceSize                                destStride,
    VkQueryResultFlags                          flags)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_query_pool, pool, queryPool);
   ANV_FROM_HANDLE(anv_buffer, buffer, destBuffer);
   uint32_t slot_offset, dst_offset;

   if (flags & VK_QUERY_RESULT_WAIT_BIT) {
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
         pc.CommandStreamerStallEnable = true;
         pc.StallAtPixelScoreboard     = true;
      }
   }

   dst_offset = buffer->offset + destOffset;
   for (uint32_t i = 0; i < queryCount; i++) {

      slot_offset = (firstQuery + i) * sizeof(struct anv_query_pool_slot);
      switch (pool->type) {
      case VK_QUERY_TYPE_OCCLUSION:
         emit_load_alu_reg_u64(&cmd_buffer->batch,
                               CS_GPR(0), &pool->bo, slot_offset);
         emit_load_alu_reg_u64(&cmd_buffer->batch,
                               CS_GPR(1), &pool->bo, slot_offset + 8);

         /* FIXME: We need to clamp the result for 32 bit. */

         uint32_t *dw = anv_batch_emitn(&cmd_buffer->batch, 5, GENX(MI_MATH));
         dw[1] = alu(OPCODE_LOAD, OPERAND_SRCA, OPERAND_R1);
         dw[2] = alu(OPCODE_LOAD, OPERAND_SRCB, OPERAND_R0);
         dw[3] = alu(OPCODE_SUB, 0, 0);
         dw[4] = alu(OPCODE_STORE, OPERAND_R2, OPERAND_ACCU);
         break;

      case VK_QUERY_TYPE_TIMESTAMP:
         emit_load_alu_reg_u64(&cmd_buffer->batch,
                               CS_GPR(2), &pool->bo, slot_offset);
         break;

      default:
         unreachable("unhandled query type");
      }

      store_query_result(&cmd_buffer->batch,
                         CS_GPR(2), buffer->bo, dst_offset, flags);

      if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
         emit_load_alu_reg_u64(&cmd_buffer->batch, CS_GPR(0),
                               &pool->bo, slot_offset + 16);
         if (flags & VK_QUERY_RESULT_64_BIT)
            store_query_result(&cmd_buffer->batch,
                               CS_GPR(0), buffer->bo, dst_offset + 8, flags);
         else
            store_query_result(&cmd_buffer->batch,
                               CS_GPR(0), buffer->bo, dst_offset + 4, flags);
      }

      dst_offset += destStride;
   }
}

#else
void genX(CmdCopyQueryPoolResults)(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount,
    VkBuffer                                    destBuffer,
    VkDeviceSize                                destOffset,
    VkDeviceSize                                destStride,
    VkQueryResultFlags                          flags)
{
   anv_finishme("Queries not yet supported on Ivy Bridge");
}
#endif
