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

static inline int64_t
clamp_int64(int64_t x, int64_t min, int64_t max)
{
   if (x < min)
      return min;
   else if (x < max)
      return x;
   else
      return max;
}

#if GEN_GEN == 7 && !GEN_IS_HASWELL
void
gen7_cmd_buffer_emit_scissor(struct anv_cmd_buffer *cmd_buffer)
{
   uint32_t count = cmd_buffer->state.dynamic.scissor.count;
   const VkRect2D *scissors =  cmd_buffer->state.dynamic.scissor.scissors;
   struct anv_state scissor_state =
      anv_cmd_buffer_alloc_dynamic_state(cmd_buffer, count * 8, 32);

   for (uint32_t i = 0; i < count; i++) {
      const VkRect2D *s = &scissors[i];

      /* Since xmax and ymax are inclusive, we have to have xmax < xmin or
       * ymax < ymin for empty clips.  In case clip x, y, width height are all
       * 0, the clamps below produce 0 for xmin, ymin, xmax, ymax, which isn't
       * what we want. Just special case empty clips and produce a canonical
       * empty clip. */
      static const struct GEN7_SCISSOR_RECT empty_scissor = {
         .ScissorRectangleYMin = 1,
         .ScissorRectangleXMin = 1,
         .ScissorRectangleYMax = 0,
         .ScissorRectangleXMax = 0
      };

      const int max = 0xffff;
      struct GEN7_SCISSOR_RECT scissor = {
         /* Do this math using int64_t so overflow gets clamped correctly. */
         .ScissorRectangleYMin = clamp_int64(s->offset.y, 0, max),
         .ScissorRectangleXMin = clamp_int64(s->offset.x, 0, max),
         .ScissorRectangleYMax = clamp_int64((uint64_t) s->offset.y + s->extent.height - 1, 0, max),
         .ScissorRectangleXMax = clamp_int64((uint64_t) s->offset.x + s->extent.width - 1, 0, max)
      };

      if (s->extent.width <= 0 || s->extent.height <= 0) {
         GEN7_SCISSOR_RECT_pack(NULL, scissor_state.map + i * 8,
                                &empty_scissor);
      } else {
         GEN7_SCISSOR_RECT_pack(NULL, scissor_state.map + i * 8, &scissor);
      }
   }

   anv_batch_emit(&cmd_buffer->batch,
                  GEN7_3DSTATE_SCISSOR_STATE_POINTERS, ssp) {
      ssp.ScissorRectPointer = scissor_state.offset;
   }

   if (!cmd_buffer->device->info.has_llc)
      anv_state_clflush(scissor_state);
}
#endif

static const uint32_t vk_to_gen_index_type[] = {
   [VK_INDEX_TYPE_UINT16]                       = INDEX_WORD,
   [VK_INDEX_TYPE_UINT32]                       = INDEX_DWORD,
};

static const uint32_t restart_index_for_type[] = {
   [VK_INDEX_TYPE_UINT16]                    = UINT16_MAX,
   [VK_INDEX_TYPE_UINT32]                    = UINT32_MAX,
};

void genX(CmdBindIndexBuffer)(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    _buffer,
    VkDeviceSize                                offset,
    VkIndexType                                 indexType)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_buffer, buffer, _buffer);

   cmd_buffer->state.dirty |= ANV_CMD_DIRTY_INDEX_BUFFER;
   if (GEN_IS_HASWELL)
      cmd_buffer->state.restart_index = restart_index_for_type[indexType];
   cmd_buffer->state.gen7.index_buffer = buffer;
   cmd_buffer->state.gen7.index_type = vk_to_gen_index_type[indexType];
   cmd_buffer->state.gen7.index_offset = offset;
}

static VkResult
flush_compute_descriptor_set(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_device *device = cmd_buffer->device;
   struct anv_pipeline *pipeline = cmd_buffer->state.compute_pipeline;
   struct anv_state surfaces = { 0, }, samplers = { 0, };
   VkResult result;

   result = anv_cmd_buffer_emit_samplers(cmd_buffer,
                                         MESA_SHADER_COMPUTE, &samplers);
   if (result != VK_SUCCESS)
      return result;
   result = anv_cmd_buffer_emit_binding_table(cmd_buffer,
                                              MESA_SHADER_COMPUTE, &surfaces);
   if (result != VK_SUCCESS)
      return result;

   struct anv_state push_state = anv_cmd_buffer_cs_push_constants(cmd_buffer);

   const struct brw_cs_prog_data *cs_prog_data = get_cs_prog_data(pipeline);
   const struct brw_stage_prog_data *prog_data = &cs_prog_data->base;

   if (push_state.alloc_size) {
      anv_batch_emit(&cmd_buffer->batch, GENX(MEDIA_CURBE_LOAD), curbe) {
         curbe.CURBETotalDataLength    = push_state.alloc_size;
         curbe.CURBEDataStartAddress   = push_state.offset;
      }
   }

   const uint32_t slm_size = encode_slm_size(GEN_GEN, prog_data->total_shared);

   struct anv_state state =
      anv_state_pool_emit(&device->dynamic_state_pool,
                          GENX(INTERFACE_DESCRIPTOR_DATA), 64,
                          .KernelStartPointer = pipeline->cs_simd,
                          .BindingTablePointer = surfaces.offset,
                          .SamplerStatePointer = samplers.offset,
                          .ConstantURBEntryReadLength =
                             cs_prog_data->push.per_thread.regs,
#if GEN_IS_HASWELL
                          .CrossThreadConstantDataReadLength =
                             cs_prog_data->push.cross_thread.regs,
#else
                          .ConstantURBEntryReadOffset = 0,
#endif
                          .BarrierEnable = cs_prog_data->uses_barrier,
                          .SharedLocalMemorySize = slm_size,
                          .NumberofThreadsinGPGPUThreadGroup =
                             cs_prog_data->threads);

   const uint32_t size = GENX(INTERFACE_DESCRIPTOR_DATA_length) * sizeof(uint32_t);
   anv_batch_emit(&cmd_buffer->batch,
                  GENX(MEDIA_INTERFACE_DESCRIPTOR_LOAD), idl) {
      idl.InterfaceDescriptorTotalLength        = size;
      idl.InterfaceDescriptorDataStartAddress = state.offset;
   }

   return VK_SUCCESS;
}

void
genX(cmd_buffer_flush_compute_state)(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_pipeline *pipeline = cmd_buffer->state.compute_pipeline;
   MAYBE_UNUSED VkResult result;

   assert(pipeline->active_stages == VK_SHADER_STAGE_COMPUTE_BIT);

   genX(cmd_buffer_config_l3)(cmd_buffer, pipeline);

   genX(flush_pipeline_select_gpgpu)(cmd_buffer);

   if (cmd_buffer->state.compute_dirty & ANV_CMD_DIRTY_PIPELINE)
      anv_batch_emit_batch(&cmd_buffer->batch, &pipeline->batch);

   if ((cmd_buffer->state.descriptors_dirty & VK_SHADER_STAGE_COMPUTE_BIT) ||
       (cmd_buffer->state.compute_dirty & ANV_CMD_DIRTY_PIPELINE)) {
      /* FIXME: figure out descriptors for gen7 */
      result = flush_compute_descriptor_set(cmd_buffer);
      assert(result == VK_SUCCESS);
      cmd_buffer->state.descriptors_dirty &= ~VK_SHADER_STAGE_COMPUTE_BIT;
   }

   cmd_buffer->state.compute_dirty = 0;

   genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);
}

void
genX(cmd_buffer_flush_dynamic_state)(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_pipeline *pipeline = cmd_buffer->state.pipeline;

   if (cmd_buffer->state.dirty & (ANV_CMD_DIRTY_PIPELINE |
                                  ANV_CMD_DIRTY_RENDER_TARGETS |
                                  ANV_CMD_DIRTY_DYNAMIC_LINE_WIDTH |
                                  ANV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS)) {

      const struct anv_image_view *iview =
         anv_cmd_buffer_get_depth_stencil_view(cmd_buffer);
      const struct anv_image *image = iview ? iview->image : NULL;
      const bool has_depth =
         image && (image->aspects & VK_IMAGE_ASPECT_DEPTH_BIT);
      const uint32_t depth_format = has_depth ?
         isl_surf_get_depth_format(&cmd_buffer->device->isl_dev,
                                   &image->depth_surface.isl) : D16_UNORM;

      uint32_t sf_dw[GENX(3DSTATE_SF_length)];
      struct GENX(3DSTATE_SF) sf = {
         GENX(3DSTATE_SF_header),
         .DepthBufferSurfaceFormat = depth_format,
         .LineWidth = cmd_buffer->state.dynamic.line_width,
         .GlobalDepthOffsetConstant = cmd_buffer->state.dynamic.depth_bias.bias,
         .GlobalDepthOffsetScale = cmd_buffer->state.dynamic.depth_bias.slope,
         .GlobalDepthOffsetClamp = cmd_buffer->state.dynamic.depth_bias.clamp
      };
      GENX(3DSTATE_SF_pack)(NULL, sf_dw, &sf);

      anv_batch_emit_merge(&cmd_buffer->batch, sf_dw, pipeline->gen7.sf);
   }

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
         .BackfaceStencilReferenceValue = d->stencil_reference.back & 0xff,
      };
      GENX(COLOR_CALC_STATE_pack)(NULL, cc_state.map, &cc);
      if (!cmd_buffer->device->info.has_llc)
         anv_state_clflush(cc_state);

      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_CC_STATE_POINTERS), ccp) {
         ccp.ColorCalcStatePointer = cc_state.offset;
      }
   }

   if (cmd_buffer->state.dirty & (ANV_CMD_DIRTY_PIPELINE |
                                  ANV_CMD_DIRTY_RENDER_TARGETS |
                                  ANV_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK |
                                  ANV_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK)) {
      uint32_t depth_stencil_dw[GENX(DEPTH_STENCIL_STATE_length)];
      struct anv_dynamic_state *d = &cmd_buffer->state.dynamic;

      struct GENX(DEPTH_STENCIL_STATE) depth_stencil = {
         .StencilTestMask = d->stencil_compare_mask.front & 0xff,
         .StencilWriteMask = d->stencil_write_mask.front & 0xff,

         .BackfaceStencilTestMask = d->stencil_compare_mask.back & 0xff,
         .BackfaceStencilWriteMask = d->stencil_write_mask.back & 0xff,
      };
      GENX(DEPTH_STENCIL_STATE_pack)(NULL, depth_stencil_dw, &depth_stencil);

      struct anv_state ds_state =
         anv_cmd_buffer_merge_dynamic(cmd_buffer, depth_stencil_dw,
                                      pipeline->gen7.depth_stencil_state,
                                      GENX(DEPTH_STENCIL_STATE_length), 64);

      anv_batch_emit(&cmd_buffer->batch,
                     GENX(3DSTATE_DEPTH_STENCIL_STATE_POINTERS), dsp) {
         dsp.PointertoDEPTH_STENCIL_STATE = ds_state.offset;
      }
   }

   if (cmd_buffer->state.gen7.index_buffer &&
       cmd_buffer->state.dirty & (ANV_CMD_DIRTY_PIPELINE |
                                  ANV_CMD_DIRTY_INDEX_BUFFER)) {
      struct anv_buffer *buffer = cmd_buffer->state.gen7.index_buffer;
      uint32_t offset = cmd_buffer->state.gen7.index_offset;

#if GEN_IS_HASWELL
      anv_batch_emit(&cmd_buffer->batch, GEN75_3DSTATE_VF, vf) {
         vf.IndexedDrawCutIndexEnable  = pipeline->primitive_restart;
         vf.CutIndex                   = cmd_buffer->state.restart_index;
      }
#endif

      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_INDEX_BUFFER), ib) {
#if !GEN_IS_HASWELL
         ib.CutIndexEnable             = pipeline->primitive_restart;
#endif
         ib.IndexFormat                = cmd_buffer->state.gen7.index_type;
         ib.MemoryObjectControlState   = GENX(MOCS);

         ib.BufferStartingAddress =
            (struct anv_address) { buffer->bo, buffer->offset + offset };
         ib.BufferEndingAddress =
            (struct anv_address) { buffer->bo, buffer->offset + buffer->size };
      }
   }

   cmd_buffer->state.dirty = 0;
}

void genX(CmdSetEvent)(
    VkCommandBuffer                             commandBuffer,
    VkEvent                                     event,
    VkPipelineStageFlags                        stageMask)
{
   stub();
}

void genX(CmdResetEvent)(
    VkCommandBuffer                             commandBuffer,
    VkEvent                                     event,
    VkPipelineStageFlags                        stageMask)
{
   stub();
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
   stub();

   genX(CmdPipelineBarrier)(commandBuffer, srcStageMask, destStageMask,
                            false, /* byRegion */
                            memoryBarrierCount, pMemoryBarriers,
                            bufferMemoryBarrierCount, pBufferMemoryBarriers,
                            imageMemoryBarrierCount, pImageMemoryBarriers);
}
