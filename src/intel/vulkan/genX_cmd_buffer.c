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

#include "genxml/gen_macros.h"
#include "genxml/genX_pack.h"

void
genX(cmd_buffer_emit_state_base_address)(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_device *device = cmd_buffer->device;

/* XXX: Do we need this on more than just BDW? */
#if (GEN_GEN >= 8)
   /* Emit a render target cache flush.
    *
    * This isn't documented anywhere in the PRM.  However, it seems to be
    * necessary prior to changing the surface state base adress.  Without
    * this, we get GPU hangs when using multi-level command buffers which
    * clear depth, reset state base address, and then go render stuff.
    */
   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.RenderTargetCacheFlushEnable = true;
   }
#endif

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
      sba.DynamicStateMemoryObjectControlState = GENX(MOCS),
      sba.DynamicStateBaseAddressModifyEnable = true,

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
   }
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
      case VK_ACCESS_COLOR_ATTACHMENT_READ_BIT:
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

   genX(cmd_buffer_config_l3)(cmd_buffer, pipeline);

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

   /* We emit the binding tables and sampler tables first, then emit push
    * constants and then finally emit binding table and sampler table
    * pointers.  It has to happen in this order, since emitting the binding
    * tables may change the push constants (in case of storage images). After
    * emitting push constants, on SKL+ we have to emit the corresponding
    * 3DSTATE_BINDING_TABLE_POINTER_* for the push constants to take effect.
    */
   uint32_t dirty = 0;
   if (cmd_buffer->state.descriptors_dirty)
      dirty = anv_cmd_buffer_flush_descriptor_sets(cmd_buffer);

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
#elif GEN_GEN <= 7
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
#endif
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

struct anv_state
genX(cmd_buffer_alloc_null_surface_state)(struct anv_cmd_buffer *cmd_buffer,
                                          struct anv_framebuffer *fb)
{
   struct anv_state state =
      anv_state_stream_alloc(&cmd_buffer->surface_state_stream, 64, 64);

   struct GENX(RENDER_SURFACE_STATE) null_ss = {
      .SurfaceType = SURFTYPE_NULL,
      .SurfaceArray = fb->layers > 0,
      .SurfaceFormat = ISL_FORMAT_R8G8B8A8_UNORM,
#if GEN_GEN >= 8
      .TileMode = YMAJOR,
#else
      .TiledSurface = true,
#endif
      .Width = fb->width - 1,
      .Height = fb->height - 1,
      .Depth = fb->layers - 1,
      .RenderTargetViewExtent = fb->layers - 1,
   };

   GENX(RENDER_SURFACE_STATE_pack)(NULL, state.map, &null_ss);

   if (!cmd_buffer->device->info.has_llc)
      anv_state_clflush(state);

   return state;
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
   const bool has_stencil =
      image && (image->aspects & VK_IMAGE_ASPECT_STENCIL_BIT);

   /* FIXME: Implement the PMA stall W/A */
   /* FIXME: Width and Height are wrong */

   /* Emit 3DSTATE_DEPTH_BUFFER */
   if (has_depth) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_DEPTH_BUFFER), db) {
         db.SurfaceType                   = SURFTYPE_2D;
         db.DepthWriteEnable              = true;
         db.StencilWriteEnable            = has_stencil;
         db.HierarchicalDepthBufferEnable = false;

         db.SurfaceFormat = isl_surf_get_depth_format(&device->isl_dev,
                                                      &image->depth_surface.isl);

         db.SurfaceBaseAddress = (struct anv_address) {
            .bo = image->bo,
            .offset = image->offset + image->depth_surface.offset,
         };
         db.DepthBufferObjectControlState = GENX(MOCS),

         db.SurfacePitch         = image->depth_surface.isl.row_pitch - 1;
         db.Height               = image->extent.height - 1;
         db.Width                = image->extent.width - 1;
         db.LOD                  = iview->base_mip;
         db.Depth                = image->array_size - 1; /* FIXME: 3-D */
         db.MinimumArrayElement  = iview->base_layer;

#if GEN_GEN >= 8
         db.SurfaceQPitch =
            isl_surf_get_array_pitch_el_rows(&image->depth_surface.isl) >> 2,
#endif
         db.RenderTargetViewExtent = 1 - 1;
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
         db.SurfaceType          = SURFTYPE_2D;
         db.SurfaceFormat        = D32_FLOAT;
         db.Width                = fb->width - 1;
         db.Height               = fb->height - 1;
         db.StencilWriteEnable   = has_stencil;
      }
   }

   /* Emit 3DSTATE_STENCIL_BUFFER */
   if (has_stencil) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_STENCIL_BUFFER), sb) {
#if GEN_GEN >= 8 || GEN_IS_HASWELL
         sb.StencilBufferEnable = true,
#endif
         sb.StencilBufferObjectControlState = GENX(MOCS),

         /* Stencil buffers have strange pitch. The PRM says:
          *
          *    The pitch must be set to 2x the value computed based on width,
          *    as the stencil buffer is stored with two rows interleaved.
          */
         sb.SurfacePitch = 2 * image->stencil_surface.isl.row_pitch - 1,

#if GEN_GEN >= 8
         sb.SurfaceQPitch = isl_surf_get_array_pitch_el_rows(&image->stencil_surface.isl) >> 2,
#endif
         sb.SurfaceBaseAddress = (struct anv_address) {
            .bo = image->bo,
            .offset = image->offset + image->stencil_surface.offset,
         };
      }
   } else {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_STENCIL_BUFFER), sb);
   }

   /* Disable hierarchial depth buffers. */
   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_HIER_DEPTH_BUFFER), hz);

   /* Clear the clear params. */
   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_CLEAR_PARAMS), cp);
}

/**
 * @see anv_cmd_buffer_set_subpass()
 */
void
genX(cmd_buffer_set_subpass)(struct anv_cmd_buffer *cmd_buffer,
                             struct anv_subpass *subpass)
{
   cmd_buffer->state.subpass = subpass;

   cmd_buffer->state.descriptors_dirty |= VK_SHADER_STAGE_FRAGMENT_BIT;

   cmd_buffer_emit_depth_stencil(cmd_buffer);
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
   anv_cmd_state_setup_attachments(cmd_buffer, pRenderPassBegin);

   genX(flush_pipeline_select_3d)(cmd_buffer);

   genX(cmd_buffer_set_subpass)(cmd_buffer, pass->subpasses);
   anv_cmd_buffer_clear_subpass(cmd_buffer);
}

void genX(CmdNextSubpass)(
    VkCommandBuffer                             commandBuffer,
    VkSubpassContents                           contents)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   assert(cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY);

   anv_cmd_buffer_resolve_subpass(cmd_buffer);
   genX(cmd_buffer_set_subpass)(cmd_buffer, cmd_buffer->state.subpass + 1);
   anv_cmd_buffer_clear_subpass(cmd_buffer);
}

void genX(CmdEndRenderPass)(
    VkCommandBuffer                             commandBuffer)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   anv_cmd_buffer_resolve_subpass(cmd_buffer);
}

static void
emit_ps_depth_count(struct anv_batch *batch,
                    struct anv_bo *bo, uint32_t offset)
{
   anv_batch_emit(batch, GENX(PIPE_CONTROL), pc) {
      pc.DestinationAddressType  = DAT_PPGTT;
      pc.PostSyncOperation       = WritePSDepthCount;
      pc.DepthStallEnable        = true;
      pc.Address                 = (struct anv_address) { bo, offset };
   }
}

static void
emit_query_availability(struct anv_batch *batch,
                        struct anv_bo *bo, uint32_t offset)
{
   anv_batch_emit(batch, GENX(PIPE_CONTROL), pc) {
      pc.DestinationAddressType  = DAT_PPGTT;
      pc.PostSyncOperation       = WriteImmediateData;
      pc.Address                 = (struct anv_address) { bo, offset };
      pc.ImmediateData           = 1;
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
      emit_ps_depth_count(&cmd_buffer->batch, &pool->bo,
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
      emit_ps_depth_count(&cmd_buffer->batch, &pool->bo,
                          query * sizeof(struct anv_query_pool_slot) + 8);

      emit_query_availability(&cmd_buffer->batch, &pool->bo,
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
         pc.DestinationAddressType  = DAT_PPGTT,
         pc.PostSyncOperation       = WriteTimestamp,
         pc.Address = (struct anv_address) { &pool->bo, offset };
      }
      break;
   }

   emit_query_availability(&cmd_buffer->batch, &pool->bo, query + 16);
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

#endif
