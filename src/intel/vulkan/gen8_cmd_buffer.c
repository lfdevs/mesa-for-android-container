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
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "anv_private.h"

#include "genxml/gen_macros.h"
#include "genxml/genX_pack.h"

#if GEN_GEN == 8
void
gen8_cmd_buffer_emit_viewport(struct anv_cmd_buffer *cmd_buffer)
{
   uint32_t count = cmd_buffer->state.dynamic.viewport.count;
   const VkViewport *viewports = cmd_buffer->state.dynamic.viewport.viewports;
   struct anv_state sf_clip_state =
      anv_cmd_buffer_alloc_dynamic_state(cmd_buffer, count * 64, 64);

   for (uint32_t i = 0; i < count; i++) {
      const VkViewport *vp = &viewports[i];

      /* The gen7 state struct has just the matrix and guardband fields, the
       * gen8 struct adds the min/max viewport fields. */
      struct GENX(SF_CLIP_VIEWPORT) sf_clip_viewport = {
         .ViewportMatrixElementm00 = vp->width / 2,
         .ViewportMatrixElementm11 = vp->height / 2,
         .ViewportMatrixElementm22 = 1.0,
         .ViewportMatrixElementm30 = vp->x + vp->width / 2,
         .ViewportMatrixElementm31 = vp->y + vp->height / 2,
         .ViewportMatrixElementm32 = 0.0,
         .XMinClipGuardband = -1.0f,
         .XMaxClipGuardband = 1.0f,
         .YMinClipGuardband = -1.0f,
         .YMaxClipGuardband = 1.0f,
         .XMinViewPort = vp->x,
         .XMaxViewPort = vp->x + vp->width - 1,
         .YMinViewPort = vp->y,
         .YMaxViewPort = vp->y + vp->height - 1,
      };

      GENX(SF_CLIP_VIEWPORT_pack)(NULL, sf_clip_state.map + i * 64,
                                 &sf_clip_viewport);
   }

   if (!cmd_buffer->device->info.has_llc)
      anv_state_clflush(sf_clip_state);

   anv_batch_emit(&cmd_buffer->batch,
                  GENX(3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP), clip) {
      clip.SFClipViewportPointer = sf_clip_state.offset;
   }
}

void
gen8_cmd_buffer_emit_depth_viewport(struct anv_cmd_buffer *cmd_buffer,
                                    bool depth_clamp_enable)
{
   uint32_t count = cmd_buffer->state.dynamic.viewport.count;
   const VkViewport *viewports = cmd_buffer->state.dynamic.viewport.viewports;
   struct anv_state cc_state =
      anv_cmd_buffer_alloc_dynamic_state(cmd_buffer, count * 8, 32);

   for (uint32_t i = 0; i < count; i++) {
      const VkViewport *vp = &viewports[i];

      struct GENX(CC_VIEWPORT) cc_viewport = {
         .MinimumDepth = depth_clamp_enable ? vp->minDepth : 0.0f,
         .MaximumDepth = depth_clamp_enable ? vp->maxDepth : 1.0f,
      };

      GENX(CC_VIEWPORT_pack)(NULL, cc_state.map + i * 8, &cc_viewport);
   }

   if (!cmd_buffer->device->info.has_llc)
      anv_state_clflush(cc_state);

   anv_batch_emit(&cmd_buffer->batch,
                  GENX(3DSTATE_VIEWPORT_STATE_POINTERS_CC), cc) {
      cc.CCViewportPointer = cc_state.offset;
   }
}
#endif

static void
__emit_genx_sf_state(struct anv_cmd_buffer *cmd_buffer)
{
      uint32_t sf_dw[GENX(3DSTATE_SF_length)];
      struct GENX(3DSTATE_SF) sf = {
         GENX(3DSTATE_SF_header),
         .LineWidth = cmd_buffer->state.dynamic.line_width,
      };
      GENX(3DSTATE_SF_pack)(NULL, sf_dw, &sf);
      /* FIXME: gen9.fs */
      anv_batch_emit_merge(&cmd_buffer->batch, sf_dw,
                           cmd_buffer->state.pipeline->gen8.sf);
}

#include "genxml/gen9_pack.h"
static void
__emit_gen9_sf_state(struct anv_cmd_buffer *cmd_buffer)
{
      uint32_t sf_dw[GENX(3DSTATE_SF_length)];
      struct GEN9_3DSTATE_SF sf = {
         GEN9_3DSTATE_SF_header,
         .LineWidth = cmd_buffer->state.dynamic.line_width,
      };
      GEN9_3DSTATE_SF_pack(NULL, sf_dw, &sf);
      /* FIXME: gen9.fs */
      anv_batch_emit_merge(&cmd_buffer->batch, sf_dw,
                           cmd_buffer->state.pipeline->gen8.sf);
}

static void
__emit_sf_state(struct anv_cmd_buffer *cmd_buffer)
{
   if (cmd_buffer->device->info.is_cherryview)
      __emit_gen9_sf_state(cmd_buffer);
   else
      __emit_genx_sf_state(cmd_buffer);
}

void
genX(cmd_buffer_flush_dynamic_state)(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_pipeline *pipeline = cmd_buffer->state.pipeline;

   if (cmd_buffer->state.dirty & (ANV_CMD_DIRTY_PIPELINE |
                                  ANV_CMD_DIRTY_DYNAMIC_LINE_WIDTH)) {
      __emit_sf_state(cmd_buffer);
   }

   if (cmd_buffer->state.dirty & (ANV_CMD_DIRTY_PIPELINE |
                                  ANV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS)){
      uint32_t raster_dw[GENX(3DSTATE_RASTER_length)];
      struct GENX(3DSTATE_RASTER) raster = {
         GENX(3DSTATE_RASTER_header),
         .GlobalDepthOffsetConstant = cmd_buffer->state.dynamic.depth_bias.bias,
         .GlobalDepthOffsetScale = cmd_buffer->state.dynamic.depth_bias.slope,
         .GlobalDepthOffsetClamp = cmd_buffer->state.dynamic.depth_bias.clamp
      };
      GENX(3DSTATE_RASTER_pack)(NULL, raster_dw, &raster);
      anv_batch_emit_merge(&cmd_buffer->batch, raster_dw,
                           pipeline->gen8.raster);
   }

   /* Stencil reference values moved from COLOR_CALC_STATE in gen8 to
    * 3DSTATE_WM_DEPTH_STENCIL in gen9. That means the dirty bits gets split
    * across different state packets for gen8 and gen9. We handle that by
    * using a big old #if switch here.
    */
#if GEN_GEN == 8
   if (cmd_buffer->state.dirty & (ANV_CMD_DIRTY_DYNAMIC_BLEND_CONSTANTS |
                                  ANV_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE)) {
      struct anv_dynamic_state *d = &cmd_buffer->state.dynamic;
      struct anv_state cc_state =
         anv_cmd_buffer_alloc_dynamic_state(cmd_buffer,
                                            GENX(COLOR_CALC_STATE_length) * 4,
                                            64);
      struct GENX(COLOR_CALC_STATE) cc = {
         .BlendConstantColorRed = cmd_buffer->state.dynamic.blend_constants[0],
         .BlendConstantColorGreen = cmd_buffer->state.dynamic.blend_constants[1],
         .BlendConstantColorBlue = cmd_buffer->state.dynamic.blend_constants[2],
         .BlendConstantColorAlpha = cmd_buffer->state.dynamic.blend_constants[3],
         .StencilReferenceValue = d->stencil_reference.front & 0xff,
         .BackFaceStencilReferenceValue = d->stencil_reference.back & 0xff,
      };
      GENX(COLOR_CALC_STATE_pack)(NULL, cc_state.map, &cc);

      if (!cmd_buffer->device->info.has_llc)
         anv_state_clflush(cc_state);

      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_CC_STATE_POINTERS), ccp) {
         ccp.ColorCalcStatePointer        = cc_state.offset;
         ccp.ColorCalcStatePointerValid   = true;
      }
   }

   if (cmd_buffer->state.dirty & (ANV_CMD_DIRTY_PIPELINE |
                                  ANV_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK |
                                  ANV_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK)) {
      uint32_t wm_depth_stencil_dw[GENX(3DSTATE_WM_DEPTH_STENCIL_length)];
      struct anv_dynamic_state *d = &cmd_buffer->state.dynamic;

      struct GENX(3DSTATE_WM_DEPTH_STENCIL wm_depth_stencil) = {
         GENX(3DSTATE_WM_DEPTH_STENCIL_header),

         .StencilTestMask = d->stencil_compare_mask.front & 0xff,
         .StencilWriteMask = d->stencil_write_mask.front & 0xff,

         .BackfaceStencilTestMask = d->stencil_compare_mask.back & 0xff,
         .BackfaceStencilWriteMask = d->stencil_write_mask.back & 0xff,
      };
      GENX(3DSTATE_WM_DEPTH_STENCIL_pack)(NULL, wm_depth_stencil_dw,
                                          &wm_depth_stencil);

      anv_batch_emit_merge(&cmd_buffer->batch, wm_depth_stencil_dw,
                           pipeline->gen8.wm_depth_stencil);
   }
#else
   if (cmd_buffer->state.dirty & ANV_CMD_DIRTY_DYNAMIC_BLEND_CONSTANTS) {
      struct anv_state cc_state =
         anv_cmd_buffer_alloc_dynamic_state(cmd_buffer,
                                            GEN9_COLOR_CALC_STATE_length * 4,
                                            64);
      struct GEN9_COLOR_CALC_STATE cc = {
         .BlendConstantColorRed = cmd_buffer->state.dynamic.blend_constants[0],
         .BlendConstantColorGreen = cmd_buffer->state.dynamic.blend_constants[1],
         .BlendConstantColorBlue = cmd_buffer->state.dynamic.blend_constants[2],
         .BlendConstantColorAlpha = cmd_buffer->state.dynamic.blend_constants[3],
      };
      GEN9_COLOR_CALC_STATE_pack(NULL, cc_state.map, &cc);

      if (!cmd_buffer->device->info.has_llc)
         anv_state_clflush(cc_state);

      anv_batch_emit(&cmd_buffer->batch, GEN9_3DSTATE_CC_STATE_POINTERS, ccp) {
         ccp.ColorCalcStatePointer = cc_state.offset;
         ccp.ColorCalcStatePointerValid = true;
      }
   }

   if (cmd_buffer->state.dirty & (ANV_CMD_DIRTY_PIPELINE |
                                  ANV_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK |
                                  ANV_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK |
                                  ANV_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE)) {
      uint32_t dwords[GEN9_3DSTATE_WM_DEPTH_STENCIL_length];
      struct anv_dynamic_state *d = &cmd_buffer->state.dynamic;
      struct GEN9_3DSTATE_WM_DEPTH_STENCIL wm_depth_stencil = {
         GEN9_3DSTATE_WM_DEPTH_STENCIL_header,

         .StencilTestMask = d->stencil_compare_mask.front & 0xff,
         .StencilWriteMask = d->stencil_write_mask.front & 0xff,

         .BackfaceStencilTestMask = d->stencil_compare_mask.back & 0xff,
         .BackfaceStencilWriteMask = d->stencil_write_mask.back & 0xff,

         .StencilReferenceValue = d->stencil_reference.front & 0xff,
         .BackfaceStencilReferenceValue = d->stencil_reference.back & 0xff,
      };
      GEN9_3DSTATE_WM_DEPTH_STENCIL_pack(NULL, dwords, &wm_depth_stencil);

      anv_batch_emit_merge(&cmd_buffer->batch, dwords,
                           pipeline->gen9.wm_depth_stencil);
   }
#endif

   if (cmd_buffer->state.dirty & (ANV_CMD_DIRTY_PIPELINE |
                                  ANV_CMD_DIRTY_INDEX_BUFFER)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_VF), vf) {
         vf.IndexedDrawCutIndexEnable  = pipeline->primitive_restart;
         vf.CutIndex                   = cmd_buffer->state.restart_index;
      }
   }

   cmd_buffer->state.dirty = 0;
}

void genX(CmdBindIndexBuffer)(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    _buffer,
    VkDeviceSize                                offset,
    VkIndexType                                 indexType)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_buffer, buffer, _buffer);

   static const uint32_t vk_to_gen_index_type[] = {
      [VK_INDEX_TYPE_UINT16]                    = INDEX_WORD,
      [VK_INDEX_TYPE_UINT32]                    = INDEX_DWORD,
   };

   static const uint32_t restart_index_for_type[] = {
      [VK_INDEX_TYPE_UINT16]                    = UINT16_MAX,
      [VK_INDEX_TYPE_UINT32]                    = UINT32_MAX,
   };

   cmd_buffer->state.restart_index = restart_index_for_type[indexType];

   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_INDEX_BUFFER), ib) {
      ib.IndexFormat                = vk_to_gen_index_type[indexType];
      ib.MemoryObjectControlState   = GENX(MOCS);
      ib.BufferStartingAddress      =
         (struct anv_address) { buffer->bo, buffer->offset + offset };
      ib.BufferSize                 = buffer->size - offset;
   }

   cmd_buffer->state.dirty |= ANV_CMD_DIRTY_INDEX_BUFFER;
}


/**
 * Emit the HZ_OP packet in the sequence specified by the BDW PRM section
 * entitled: "Optimized Depth Buffer Clear and/or Stencil Buffer Clear."
 *
 * \todo Enable Stencil Buffer-only clears
 */
void
genX(cmd_buffer_emit_hz_op)(struct anv_cmd_buffer *cmd_buffer,
                          enum blorp_hiz_op op)
{
   struct anv_cmd_state *cmd_state = &cmd_buffer->state;
   const struct anv_image_view *iview =
      anv_cmd_buffer_get_depth_stencil_view(cmd_buffer);

   if (iview == NULL || !anv_image_has_hiz(iview->image))
      return;

   /* FINISHME: Implement multi-subpass HiZ */
   if (cmd_buffer->state.pass->subpass_count > 1)
      return;

   const uint32_t ds = cmd_state->subpass->depth_stencil_attachment;

   /* Section 7.4. of the Vulkan 1.0.27 spec states:
    *
    *   "The render area must be contained within the framebuffer dimensions."
    *
    * Therefore, the only way the extent of the render area can match that of
    * the image view is if the render area offset equals (0, 0).
    */
   const bool full_surface_op =
             cmd_state->render_area.extent.width == iview->extent.width &&
             cmd_state->render_area.extent.height == iview->extent.height;
   if (full_surface_op)
      assert(cmd_state->render_area.offset.x == 0 &&
             cmd_state->render_area.offset.y == 0);

   /* This variable corresponds to the Pixel Dim column in the table below */
   struct isl_extent2d px_dim;

   /* Validate that we can perform the HZ operation and that it's necessary. */
   switch (op) {
   case BLORP_HIZ_OP_DEPTH_CLEAR:
      if (cmd_buffer->state.pass->attachments[ds].load_op !=
          VK_ATTACHMENT_LOAD_OP_CLEAR)
         return;

      /* Apply alignment restrictions. Despite the BDW PRM mentioning this is
       * only needed for a depth buffer surface type of D16_UNORM, testing
       * showed it to be necessary for other depth formats as well
       * (e.g., D32_FLOAT).
       */
#if GEN_GEN == 8
      /* Pre-SKL, HiZ has an 8x4 sample block. As the number of samples
       * increases, the number of pixels representable by this block
       * decreases by a factor of the sample dimensions. Sample dimensions
       * scale following the MSAA interleaved pattern.
       *
       * Sample|Sample|Pixel
       * Count |Dim   |Dim
       * ===================
       *    1  | 1x1  | 8x4
       *    2  | 2x1  | 4x4
       *    4  | 2x2  | 4x2
       *    8  | 4x2  | 2x2
       *   16  | 4x4  | 2x1
       *
       * Table: Pixel Dimensions in a HiZ Sample Block Pre-SKL
       */
      /* This variable corresponds to the Sample Dim column in the table
       * above.
       */
      const struct isl_extent2d sa_dim =
         isl_get_interleaved_msaa_px_size_sa(iview->image->samples);
      px_dim.w = 8 / sa_dim.w;
      px_dim.h = 4 / sa_dim.h;
#elif GEN_GEN >= 9
      /* SKL+, the sample block becomes a "pixel block" so the expected
       * pixel dimension is a constant 8x4 px for all sample counts.
       */
      px_dim = (struct isl_extent2d) { .w = 8, .h = 4};
#endif

      if (!full_surface_op) {
         /* Fast depth clears clear an entire sample block at a time. As a
          * result, the rectangle must be aligned to the pixel dimensions of
          * a sample block for a successful operation.
          *
          * Fast clears can still work if the offset is aligned and the render
          * area offset + extent touches the edge of a depth buffer whose extent
          * is unaligned. This is because each physical HiZ miplevel is padded
          * by the px_dim. In this case, the size of the clear rectangle will be
          * padded later on in this function.
          */
         if (cmd_state->render_area.offset.x % px_dim.w ||
             cmd_state->render_area.offset.y % px_dim.h)
            return;
         if (cmd_state->render_area.offset.x +
             cmd_state->render_area.extent.width != iview->extent.width &&
             cmd_state->render_area.extent.width % px_dim.w)
            return;
         if (cmd_state->render_area.offset.y +
             cmd_state->render_area.extent.height != iview->extent.height &&
             cmd_state->render_area.extent.height % px_dim.h)
            return;
      }
      break;
   case BLORP_HIZ_OP_DEPTH_RESOLVE:
      if (cmd_buffer->state.pass->attachments[ds].store_op !=
          VK_ATTACHMENT_STORE_OP_STORE)
         return;
      break;
   case BLORP_HIZ_OP_HIZ_RESOLVE:
      /* If the render area covers the entire surface *and* load_op is either
       * CLEAR or DONT_CARE then the previous contents of the depth buffer
       * will be entirely discarded.  In this case, we can skip the HiZ
       * resolve.
       *
       * If the render area is not the full surface, we need to do
       * the resolve because otherwise data outside the render area may get
       * garbled by the resolve at the end of the render pass.
       */
      if (full_surface_op &&
          cmd_buffer->state.pass->attachments[ds].load_op !=
          VK_ATTACHMENT_LOAD_OP_LOAD)
         return;
      break;
   case BLORP_HIZ_OP_NONE:
      unreachable("Invalid HiZ OP");
      break;
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_WM_HZ_OP), hzp) {
      switch (op) {
      case BLORP_HIZ_OP_DEPTH_CLEAR:
         hzp.StencilBufferClearEnable = VK_IMAGE_ASPECT_STENCIL_BIT &
                            cmd_state->attachments[ds].pending_clear_aspects;
         hzp.DepthBufferClearEnable = VK_IMAGE_ASPECT_DEPTH_BIT &
                            cmd_state->attachments[ds].pending_clear_aspects;
         hzp.FullSurfaceDepthandStencilClear = full_surface_op;
         hzp.StencilClearValue =
            cmd_state->attachments[ds].clear_value.depthStencil.stencil & 0xff;

         /* Mark aspects as cleared */
         cmd_state->attachments[ds].pending_clear_aspects = 0;
         break;
      case BLORP_HIZ_OP_DEPTH_RESOLVE:
         hzp.DepthBufferResolveEnable = true;
         break;
      case BLORP_HIZ_OP_HIZ_RESOLVE:
         hzp.HierarchicalDepthBufferResolveEnable = true;
         break;
      case BLORP_HIZ_OP_NONE:
         unreachable("Invalid HiZ OP");
         break;
      }

      if (op != BLORP_HIZ_OP_DEPTH_CLEAR) {
         /* The Optimized HiZ resolve rectangle must be the size of the full RT
          * and aligned to 8x4. The non-optimized Depth resolve rectangle must
          * be the size of the full RT. The same alignment is assumed to be
          * required.
          */
         hzp.ClearRectangleXMin = 0;
         hzp.ClearRectangleYMin = 0;
         hzp.ClearRectangleXMax = align_u32(iview->extent.width, 8);
         hzp.ClearRectangleYMax = align_u32(iview->extent.height, 4);
      } else {
         /* This clear rectangle is aligned */
         hzp.ClearRectangleXMin = cmd_state->render_area.offset.x;
         hzp.ClearRectangleYMin = cmd_state->render_area.offset.y;
         hzp.ClearRectangleXMax = cmd_state->render_area.offset.x +
            align_u32(cmd_state->render_area.extent.width, px_dim.width);
         hzp.ClearRectangleYMax = cmd_state->render_area.offset.y +
            align_u32(cmd_state->render_area.extent.height, px_dim.height);
      }


      /* Due to a hardware issue, this bit MBZ */
      hzp.ScissorRectangleEnable = false;
      hzp.NumberofMultisamples = ffs(iview->image->samples) - 1;
      hzp.SampleMask = 0xFFFF;
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.PostSyncOperation = WriteImmediateData;
      pc.Address =
         (struct anv_address){ &cmd_buffer->device->workaround_bo, 0 };
   }

   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_WM_HZ_OP), hzp);

   if (!full_surface_op && op == BLORP_HIZ_OP_DEPTH_CLEAR) {
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
         pc.DepthStallEnable = true;
         pc.DepthCacheFlushEnable = true;
      }
   }
}

void genX(CmdSetEvent)(
    VkCommandBuffer                             commandBuffer,
    VkEvent                                     _event,
    VkPipelineStageFlags                        stageMask)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_event, event, _event);

   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.DestinationAddressType  = DAT_PPGTT,
      pc.PostSyncOperation       = WriteImmediateData,
      pc.Address = (struct anv_address) {
         &cmd_buffer->device->dynamic_state_block_pool.bo,
         event->state.offset
      };
      pc.ImmediateData           = VK_EVENT_SET;
   }
}

void genX(CmdResetEvent)(
    VkCommandBuffer                             commandBuffer,
    VkEvent                                     _event,
    VkPipelineStageFlags                        stageMask)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_event, event, _event);

   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.DestinationAddressType  = DAT_PPGTT;
      pc.PostSyncOperation       = WriteImmediateData;
      pc.Address = (struct anv_address) {
         &cmd_buffer->device->dynamic_state_block_pool.bo,
         event->state.offset
      };
      pc.ImmediateData           = VK_EVENT_RESET;
   }
}

void genX(CmdWaitEvents)(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    eventCount,
    const VkEvent*                              pEvents,
    VkPipelineStageFlags                        srcStageMask,
    VkPipelineStageFlags                        destStageMask,
    uint32_t                                    memoryBarrierCount,
    const VkMemoryBarrier*                      pMemoryBarriers,
    uint32_t                                    bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
    uint32_t                                    imageMemoryBarrierCount,
    const VkImageMemoryBarrier*                 pImageMemoryBarriers)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   for (uint32_t i = 0; i < eventCount; i++) {
      ANV_FROM_HANDLE(anv_event, event, pEvents[i]);

      anv_batch_emit(&cmd_buffer->batch, GENX(MI_SEMAPHORE_WAIT), sem) {
         sem.WaitMode            = PollingMode,
         sem.CompareOperation    = COMPARE_SAD_EQUAL_SDD,
         sem.SemaphoreDataDword  = VK_EVENT_SET,
         sem.SemaphoreAddress = (struct anv_address) {
            &cmd_buffer->device->dynamic_state_block_pool.bo,
            event->state.offset
         };
      }
   }

   genX(CmdPipelineBarrier)(commandBuffer, srcStageMask, destStageMask,
                            false, /* byRegion */
                            memoryBarrierCount, pMemoryBarriers,
                            bufferMemoryBarrierCount, pBufferMemoryBarriers,
                            imageMemoryBarrierCount, pImageMemoryBarriers);
}
