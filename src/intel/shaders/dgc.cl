/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "libintel_shaders.h"
#include "dev/intel_wa.h"
#include "vulkan/anv_types.h"

#define HAS_STAGE(descriptor, stage) \
   (((descriptor)->active_stages & \
     BITFIELD_BIT(ANV_DGC_STAGE_##stage)) != 0)

#if GFX_VER >= 9

static void
merge_dwords(global void *dst, global void *src1, global void *src2, uint32_t n_dwords)
{
   for (uint32_t i = 0; i < n_dwords; i += 4) {
      if (n_dwords - i >= 4) {
         *(global uint4 *)(dst + i * 4) = *(global uint4 *)(src1 + i * 4) |
                                          *(global uint4 *)(src2 + i * 4) ;
      } else if (n_dwords - i >= 3) {
         *(global uint3 *)(dst + i * 4) = *(global uint3 *)(src1 + i * 4) |
                                          *(global uint3 *)(src2 + i * 4) ;
      } else if (n_dwords - i >= 2) {
         *(global uint2 *)(dst + i * 4) = *(global uint2 *)(src1 + i * 4) |
                                          *(global uint2 *)(src2 + i * 4) ;
      } else {
         *(global uint *)(dst + i * 4) = *(global uint *)(src1 + i * 4) |
                                         *(global uint *)(src2 + i * 4) ;
      }
   }
}

#if GFX_VER >= 12
static uint32_t
write_3DSTATE_CONSTANT_ALL(global void *dst_ptr,
                           global void *push_data_addr,
                           global struct anv_dgc_push_stage_state *stage_state,
                           global struct anv_dgc_gfx_state *state,
                           uint32_t stage_enabled)
{
   uint32_t n_slots = stage_state->legacy.n_slots;
   struct GENX(3DSTATE_CONSTANT_ALL) v = {
      GENX(3DSTATE_CONSTANT_ALL_header),
      .DWordLength        = GENX(3DSTATE_CONSTANT_ALL_length) -
                            GENX(3DSTATE_CONSTANT_ALL_length_bias) +
                            n_slots * GENX(3DSTATE_CONSTANT_ALL_DATA_length),
      .ShaderUpdateEnable = stage_enabled,
      .MOCS               = state->layout.push_constants.mocs,
      .PointerBufferMask  = (1u << n_slots) - 1,
   };
   GENX(3DSTATE_CONSTANT_ALL_pack)(dst_ptr, &v);

   dst_ptr += GENX(3DSTATE_CONSTANT_ALL_length) * 4;

   for (uint32_t i = 0; i < n_slots; i++) {
      struct anv_dgc_push_stage_slot slot = stage_state->legacy.slots[i];

      if (slot.type == ANV_DGC_PUSH_SLOT_TYPE_PUSH_CONSTANTS) {
         struct GENX(3DSTATE_CONSTANT_ALL_DATA) vd = {
            .ConstantBufferReadLength = slot.push_data_size / 32,
            .PointerToConstantBuffer  = (uint64_t) push_data_addr + slot.push_data_offset,
         };
         GENX(3DSTATE_CONSTANT_ALL_DATA_pack)(dst_ptr, &vd);
      } else {
         struct GENX(3DSTATE_CONSTANT_ALL_DATA) vd = {
            .ConstantBufferReadLength = slot.push_data_size / 32,
            .PointerToConstantBuffer  = state->push_constants.addresses[i],
         };
         GENX(3DSTATE_CONSTANT_ALL_DATA_pack)(dst_ptr, &vd);
      }

      dst_ptr += GENX(3DSTATE_CONSTANT_ALL_DATA_length) * 4;
   }

   return 4 * (GENX(3DSTATE_CONSTANT_ALL_length) +
               n_slots * GENX(3DSTATE_CONSTANT_ALL_DATA_length));
}
#else
static uint64_t
pc_slot_address(global struct anv_dgc_push_stage_slot *slot,
                global uint64_t *slot_address,
                global void *push_data_addr)
{
   if (slot->type == ANV_DGC_PUSH_SLOT_TYPE_PUSH_CONSTANTS) {
      return (uint64_t) push_data_addr + slot->push_data_offset;
   } else {
      return *slot_address;
   }
}

static uint32_t
write_3DSTATE_CONSTANT_XS(global void *dst_ptr,
                          global void *push_data_addr,
                          global struct anv_dgc_push_stage_state *stage_state,
                          global struct anv_dgc_gfx_state *state,
                          uint32_t stage_enabled)
{
   uint32_t opcode;
   if (stage_enabled & BITFIELD_BIT(ANV_DGC_STAGE_VERTEX))
      opcode = 21;
   else if (stage_enabled & BITFIELD_BIT(ANV_DGC_STAGE_TESS_CTRL))
      opcode = 25;
   else if (stage_enabled & BITFIELD_BIT(ANV_DGC_STAGE_TESS_EVAL))
      opcode = 26;
   else if (stage_enabled & BITFIELD_BIT(ANV_DGC_STAGE_GEOMETRY))
      opcode = 22;
   else
      opcode = 23;

   struct GENX(3DSTATE_CONSTANT_VS) v = {
      GENX(3DSTATE_CONSTANT_VS_header),
      ._3DCommandSubOpcode = opcode,
      .ConstantBody = {
         .Buffer = {
            pc_slot_address(&stage_state->legacy.slots[0],
                            &state->push_constants.addresses[0],
                            push_data_addr),
            pc_slot_address(&stage_state->legacy.slots[1],
                            &state->push_constants.addresses[1],
                            push_data_addr),
            pc_slot_address(&stage_state->legacy.slots[2],
                            &state->push_constants.addresses[2],
                            push_data_addr),
            pc_slot_address(&stage_state->legacy.slots[3],
                            &state->push_constants.addresses[3],
                            push_data_addr),
         },
         .ReadLength = {
            stage_state->legacy.slots[0].push_data_size / 32,
            stage_state->legacy.slots[1].push_data_size / 32,
            stage_state->legacy.slots[2].push_data_size / 32,
            stage_state->legacy.slots[3].push_data_size / 32,
         },
      },
   };
   GENX(3DSTATE_CONSTANT_VS_pack)(dst_ptr, &v);

   return 4 * GENX(3DSTATE_CONSTANT_VS_length);
}
#endif

static void
write_app_push_constant_data(global void *push_data_ptr,
                             global struct anv_dgc_push_layout *pc_layout,
                             global void *seq_ptr,
                             global void *template_ptr,
                             uint32_t template_size,
                             uint32_t seq_idx)
{
   uint32_t num_entries = pc_layout->num_entries;

   /* Copy the push constant data prepared on the CPU into the preprocess
    * buffer. Try to minimize the amount if the first entry partially or
    * entirely overlaps.
    */
   if (template_size > 0) {
      if (num_entries > 0) {
         struct anv_dgc_push_entry first_entry = pc_layout->entries[0];
         uint32_t entry_end = first_entry.push_offset + first_entry.size;
         if (first_entry.push_offset > 0) {
            genX(copy_data)(push_data_ptr, template_ptr,
                            first_entry.push_offset);
         }
         if (entry_end < template_size) {
            genX(copy_data)(push_data_ptr + entry_end,
                            template_ptr + entry_end,
                            template_size - entry_end);
         }
      } else {
         genX(copy_data)(push_data_ptr, template_ptr, template_size);
      }
   }

   /* Update push constant data using the indirect stream */
   for (uint32_t i = 0; i < num_entries; i++) {
      struct anv_dgc_push_entry entry = pc_layout->entries[i];
      global void *pc_ptr = seq_ptr + entry.seq_offset;
      genX(copy_data)(push_data_ptr + entry.push_offset,
                      pc_ptr, entry.size);
   }

   if (pc_layout->seq_id_active)
      *(uint32_t *)(push_data_ptr + pc_layout->seq_id_offset) = seq_idx;
}

static void
write_cs_drv_push_constant_data(global struct anv_push_constants *push_data_ptr,
                                global void *driver_template_ptr,
                                uint32_t offset, uint32_t size,
                                global VkDispatchIndirectCommand *info)
{
   genX(copy_data)(&push_data_ptr->client_data[offset],
                   driver_template_ptr, size);

#if GFX_VERx10 >= 125
   /* On Gfx12.5+ we always have the entire push constant space, so it's fine to copy */
   push_data_ptr->cs.num_workgroups[0] = info->x;
   push_data_ptr->cs.num_workgroups[1] = info->y;
   push_data_ptr->cs.num_workgroups[2] = info->z;
#else
   /* Prior to Gfx12.5, the push constant data has to be aligned to 64B and
    * the beginning is based off the first location the shader needs. So if
    * the read location is does not include the workgroup, don't write it, we
    * would be overwriting some other data in the generated commands/data.
    */
   if (offset <= offsetof(struct anv_push_constants, cs.num_workgroups[0])) {
      push_data_ptr->cs.num_workgroups[0] = info->x;
      push_data_ptr->cs.num_workgroups[1] = info->y;
      push_data_ptr->cs.num_workgroups[2] = info->z;
   }
#endif
}

static void
write_rt_drv_push_constant_data(global void *driver_data_ptr,
                                global void *driver_template_ptr,
                                uint32_t size)
{
   genX(copy_data)(driver_data_ptr, driver_template_ptr, size);
}

static void
write_gfx_drv_push_constant_data(global void *driver_data_ptr,
                                 global void *driver_template_ptr,
                                 uint32_t size)
{
   genX(copy_data)(driver_data_ptr, driver_template_ptr, size);
}

static uint32_t
write_gfx_push_constant_commands(global void *push_cmd_ptr,
                                 global void *push_data_ptr,
                                 global struct anv_dgc_gfx_state *state)
{
   uint32_t cmd_offset = 0;
   uint32_t push_stages = state->descriptor.push_constants.active_stages;
   for (uint32_t s = ANV_DGC_STAGE_VERTEX;
        s <= ANV_DGC_STAGE_FRAGMENT && push_stages != 0; s++) {
      if ((BITFIELD_BIT(s) & push_stages) == 0)
         continue;

      global struct anv_dgc_push_stage_state *stage_state =
         &state->descriptor.push_constants.stages[s];

#if GFX_VER >= 12
      cmd_offset += write_3DSTATE_CONSTANT_ALL(push_cmd_ptr + cmd_offset,
                                               push_data_ptr,
                                               stage_state,
                                               state,
                                               BITFIELD_BIT(s));
#else
      cmd_offset += write_3DSTATE_CONSTANT_XS(push_cmd_ptr + cmd_offset,
                                              push_data_ptr,
                                              stage_state,
                                               state,
                                              BITFIELD_BIT(s));
#endif

      push_stages &= ~BITFIELD_BIT(s);
   }

#if GFX_VERx10 >= 125
   /* Mesh & Task use a single combined push constants + driver constants
    * pointer
    */
   if (push_stages & BITFIELD_BIT(ANV_DGC_STAGE_TASK)) {
      struct anv_dgc_push_bindless_stage pc =
         state->descriptor.push_constants.stages[ANV_DGC_STAGE_TASK].bindless;
      uint64_t pc_addr = (uint64_t) push_data_ptr + pc.push_data_offset;
      struct GENX(3DSTATE_TASK_SHADER_DATA) data = {
         GENX(3DSTATE_TASK_SHADER_DATA_header),
         .InlineData = {
            pc.inline_dwords[0] == ANV_INLINE_DWORD_PUSH_ADDRESS_LDW ?
            pc_addr & 0xffffffff : ((global uint32_t *)push_data_ptr)[pc.inline_dwords[0]],
            pc.inline_dwords[0] == ANV_INLINE_DWORD_PUSH_ADDRESS_LDW ?
            pc_addr >> 32 : ((global uint32_t *)push_data_ptr)[pc.inline_dwords[1]],
            ((global uint32_t *)push_data_ptr)[pc.inline_dwords[2]],
            ((global uint32_t *)push_data_ptr)[pc.inline_dwords[3]],
            ((global uint32_t *)push_data_ptr)[pc.inline_dwords[4]],
            ((global uint32_t *)push_data_ptr)[pc.inline_dwords[5]],
            ((global uint32_t *)push_data_ptr)[pc.inline_dwords[6]],
            ((global uint32_t *)push_data_ptr)[pc.inline_dwords[7]],
         },
      };
      GENX(3DSTATE_TASK_SHADER_DATA_pack)(push_cmd_ptr + cmd_offset, &data);
      cmd_offset += GENX(3DSTATE_TASK_SHADER_DATA_length) * 4;
   }


   if (push_stages & BITFIELD_BIT(ANV_DGC_STAGE_MESH)) {
      struct anv_dgc_push_bindless_stage pc =
         state->descriptor.push_constants.stages[ANV_DGC_STAGE_MESH].bindless;
      uint64_t pc_addr = (uint64_t) push_data_ptr + pc.push_data_offset;
      struct GENX(3DSTATE_MESH_SHADER_DATA) data = {
         GENX(3DSTATE_MESH_SHADER_DATA_header),
         .InlineData = {
            pc.inline_dwords[0] == ANV_INLINE_DWORD_PUSH_ADDRESS_LDW ? pc_addr & 0xffffffff :
            ((global uint32_t *)push_data_ptr)[pc.inline_dwords[0]],
            pc.inline_dwords[1] == ANV_INLINE_DWORD_PUSH_ADDRESS_UDW ? pc_addr >> 32 :
            ((global uint32_t *)push_data_ptr)[pc.inline_dwords[1]],
            ((global uint32_t *)push_data_ptr)[pc.inline_dwords[2]],
            ((global uint32_t *)push_data_ptr)[pc.inline_dwords[3]],
            ((global uint32_t *)push_data_ptr)[pc.inline_dwords[4]],
            ((global uint32_t *)push_data_ptr)[pc.inline_dwords[5]],
            ((global uint32_t *)push_data_ptr)[pc.inline_dwords[6]],
            ((global uint32_t *)push_data_ptr)[pc.inline_dwords[7]],
         },
      };
      GENX(3DSTATE_MESH_SHADER_DATA_pack)(push_cmd_ptr + cmd_offset, &data);
      cmd_offset += GENX(3DSTATE_MESH_SHADER_DATA_length) * 4;
#undef PVDW_OR
   }
#endif

   return cmd_offset;
}

static global void *
get_ptr(global void *base, uint32_t stride,
        uint32_t prolog_size, uint32_t seq_idx)
{
   return base + prolog_size + seq_idx * stride;
}

static void
write_prolog_epilog(global void *cmd_base, uint32_t cmd_stride,
                    uint32_t max_count, uint32_t cmd_prolog_size,
                    uint32_t seq_idx, uint64_t return_addr)
{
   /* A write to the location of the MI_BATCH_BUFFER_START below. */
   genX(write_address)(cmd_base,
                       get_ptr(cmd_base, cmd_stride,
                               cmd_prolog_size, max_count) + 4,
                       return_addr);

   global void *next_addr = cmd_base + (GENX(MI_STORE_DATA_IMM_length) + 1 +
                                        GENX(MI_BATCH_BUFFER_START_length)) * 4;

   genX(write_MI_BATCH_BUFFER_START)(
      cmd_base + (GENX(MI_STORE_DATA_IMM_length) + 1) * 4,
      (uint64_t)next_addr);

   /* Reenable the prefetcher. */
#if GFX_VER >= 12
   struct GENX(MI_ARB_CHECK) v = {
      GENX(MI_ARB_CHECK_header),
      /* This is a trick to get the CLC->SPIRV not to use a constant variable
       * for this. Otherwise we run into issues trying to store that variable
       * in constant memory which is inefficient for a single dword and also
       * not handled in our backend.
       */
      .PreParserDisableMask = seq_idx == 0,
      .PreParserDisable = false,
   };
   GENX(MI_ARB_CHECK_pack)(next_addr, &v);
#endif

   /* This is the epilog, returning to the main batch. */
   genX(write_MI_BATCH_BUFFER_START)(
      get_ptr(cmd_base, cmd_stride, cmd_prolog_size, max_count),
      return_addr);
}

static void
write_return_addr(global void *cmd_base, uint32_t cmd_stride,
                  uint32_t max_count, uint32_t cmd_prolog_size,
                  uint64_t return_addr)
{
   /* A write to the location of the MI_BATCH_BUFFER_START below. */
   genX(write_address)(cmd_base,
                       get_ptr(cmd_base, cmd_stride,
                               cmd_prolog_size, max_count) + 4,
                       return_addr);
}

void
genX(libanv_preprocess_gfx_generate)(global void *cmd_base,
                                     uint32_t cmd_stride,
                                     global void *data_base,
                                     uint32_t data_stride,
                                     global void *seq_base,
                                     uint32_t seq_stride,
                                     global uint32_t *seq_count,
                                     uint32_t max_seq_count,
                                     uint32_t cmd_prolog_size,
                                     uint32_t data_prolog_size,
                                     global struct anv_dgc_gfx_state *state,
                                     global void *const_ptr,
                                     uint32_t const_size,
                                     global void *driver_const_ptr,
                                     uint64_t return_addr,
                                     uint32_t flags,
                                     uint32_t seq_idx)
{
   uint32_t max_count = seq_count != 0 ? min(*seq_count, max_seq_count) : max_seq_count;

   if (seq_idx == 0) {
      write_prolog_epilog(cmd_base, cmd_stride, max_count,
                          cmd_prolog_size, seq_idx, return_addr);
   }

   if (seq_idx >= max_count)
      return;

   /* Pointer to the stream data, layed out as described in stream_layout. */
   global void *seq_ptr = seq_base + seq_idx * seq_stride;

   /* Where to write the commands */
   global void *cmd_ptr =
      get_ptr(cmd_base, cmd_stride, cmd_prolog_size, seq_idx);

   /* 3DSTATE_INDEX_BUFFER */
   struct anv_dgc_index_buffer index_buffer = state->layout.index_buffer;
   if (index_buffer.cmd_size != 0) {
      VkBindIndexBufferIndirectCommandEXT idx_data =
         *(global VkBindIndexBufferIndirectCommandEXT *)(
            seq_ptr + index_buffer.seq_offset);

      uint32_t index_format =
         index_buffer.u32_value == idx_data.indexType ? INDEX_DWORD :
         index_buffer.u16_value == idx_data.indexType ? INDEX_WORD :
         index_buffer.u8_value  == idx_data.indexType ? INDEX_BYTE :
         INDEX_BYTE;

      genX(write_3DSTATE_INDEX_BUFFER)(cmd_ptr + index_buffer.cmd_offset,
                                       idx_data.bufferAddress,
                                       idx_data.size,
                                       index_format,
                                       index_buffer.mocs);
   }

   /* 3DSTATE_VERTEX_BUFFERS */
   uint32_t n_vertex_buffers = state->layout.vertex_buffers.n_buffers;
   uint32_t n_draw_param_buffers = GFX_VER == 9 ? util_bitcount(state->descriptor.draw_params) : 0;
   if (n_vertex_buffers > 0 || n_draw_param_buffers > 0) {
      global void *cmd_vb = cmd_ptr + state->layout.vertex_buffers.cmd_offset;

      genX(write_3DSTATE_VERTEX_BUFFERS)(cmd_vb, n_vertex_buffers + n_draw_param_buffers);
      cmd_vb += 4;

#if GFX_VER == 9
      global void *prev_seq_ptr = seq_base + (seq_idx == 0 ? 0 : (seq_idx - 1))  * seq_stride;
      bool needs_vf_inval = false;
#endif

      uint16_t mocs = state->layout.vertex_buffers.mocs;
      for (uint32_t i = 0; i < n_vertex_buffers; i++) {
         struct anv_dgc_vertex_buffer vb = state->layout.vertex_buffers.buffers[i];
         VkBindVertexBufferIndirectCommandEXT vtx_data =
            *(global VkBindVertexBufferIndirectCommandEXT *)(
               seq_ptr + vb.seq_offset);
#if GFX_VER == 9
         VkBindVertexBufferIndirectCommandEXT prev_vtx_data =
            *(global VkBindVertexBufferIndirectCommandEXT *)(
               prev_seq_ptr + vb.seq_offset);
         if ((vtx_data.bufferAddress >> 32) != (prev_vtx_data.bufferAddress >> 32)) {
            uint32_t offset = vtx_data.bufferAddress & 0xffffffff;
            uint32_t prev_offset = prev_vtx_data.bufferAddress & 0xffffffff;
            if (offset >= prev_offset && offset < (prev_offset + prev_vtx_data.size))
               needs_vf_inval = true;
         }
#endif

         genX(write_VERTEX_BUFFER_STATE)(cmd_vb, mocs, vb.binding,
                                         vtx_data.bufferAddress,
                                         vtx_data.size,
                                         vtx_data.stride);
         cmd_vb += GENX(VERTEX_BUFFER_STATE_length) * 4;
      }

#if GFX_VER == 9
      global uint32_t *draw_param_ptr =
         get_ptr(data_base, data_stride, data_prolog_size, seq_idx) +
         state->layout.push_constants.data_offset +
         MAX_PUSH_CONSTANTS_SIZE +
         ANV_DRIVER_PUSH_CONSTANTS_SIZE;

      if (state->descriptor.draw_params & ANV_DGC_DRAW_PARAM_BASE_INSTANCE_VERTEX) {
         genX(write_VERTEX_BUFFER_STATE)(cmd_vb, mocs, ANV_SVGS_VB_INDEX,
                                         (uint64_t)draw_param_ptr, 8, 0);
         cmd_vb += GENX(VERTEX_BUFFER_STATE_length) * 4;
         if (state->layout.draw.draw_type == ANV_DGC_DRAW_TYPE_SEQUENTIAL) {
            VkDrawIndirectCommand data =
               *((global VkDrawIndirectCommand *)(seq_ptr + state->layout.draw.seq_offset));
            draw_param_ptr[0] = data.firstVertex;
            draw_param_ptr[1] = data.firstInstance;
         } else {
            VkDrawIndexedIndirectCommand data =
               *((global VkDrawIndexedIndirectCommand *)(seq_ptr + state->layout.draw.seq_offset));
            draw_param_ptr[0] = data.vertexOffset;
            draw_param_ptr[1] = data.firstInstance;
         }
         draw_param_ptr += 2;
      }
      if (state->descriptor.draw_params & ANV_DGC_DRAW_PARAM_DRAW_ID) {
         genX(write_VERTEX_BUFFER_STATE)(cmd_vb, mocs, ANV_DRAWID_VB_INDEX,
                                         (uint64_t)draw_param_ptr, 4, 0);
         cmd_vb += GENX(VERTEX_BUFFER_STATE_length) * 4;
         /* gl_DrawID is always 0 since we don't support
          * VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_COUNT_EXT
          */
         draw_param_ptr[0] = 0;
         draw_param_ptr += 1;
      }

      if (needs_vf_inval) {
         struct GENX(PIPE_CONTROL) pc = {
            .CommandStreamerStallEnable = true,
            .VFCacheInvalidationEnable = true,
         };
         GENX(PIPE_CONTROL_pack)(cmd_vb, &pc);
      } else {
         genX(set_data)(cmd_vb, GENX(PIPE_CONTROL_length) * 4, 0);
      }
      cmd_vb += GENX(PIPE_CONTROL_length) * 4;
#endif
   }

#if INTEL_WA_16011107343_GFX_VER || INTEL_WA_22018402687_GFX_VER
   genX(copy_data)(cmd_ptr + state->layout.indirect_set.final_cmds_offset,
                   state->descriptor.final_commands,
                   state->layout.indirect_set.final_cmds_size);
#endif

   /* Push constants */
   enum anv_dgc_push_constant_flags pc_flags =
      state->layout.push_constants.flags;
   if (pc_flags & ANV_DGC_PUSH_CONSTANTS_CMD_ACTIVE) {
      global void *push_data_ptr =
         get_ptr(data_base, data_stride, data_prolog_size, seq_idx) +
         state->layout.push_constants.data_offset;

      write_app_push_constant_data(push_data_ptr,
                                   &state->layout.push_constants,
                                   seq_ptr, const_ptr,
                                   const_size, seq_idx);
      write_gfx_drv_push_constant_data(
         push_data_ptr + MAX_PUSH_CONSTANTS_SIZE,
         driver_const_ptr, ANV_DRIVER_PUSH_CONSTANTS_SIZE);

      write_gfx_push_constant_commands(cmd_ptr +
                                       state->layout.push_constants.cmd_offset,
                                       push_data_ptr,
                                       state);
   }

   /* 3DPRIMITIVE / 3DMESH_3D */
   bool is_predicated = (flags & ANV_GENERATED_FLAG_PREDICATED) != 0;
   bool tbimr_enabled = (flags & ANV_GENERATED_FLAG_TBIMR) != 0;
   switch (state->layout.draw.draw_type) {
   case ANV_DGC_DRAW_TYPE_SEQUENTIAL:
      genX(write_draw)(cmd_ptr + state->layout.draw.cmd_offset,
                       seq_ptr + state->layout.draw.seq_offset,
                       0 /* draw_id_ptr */,
                       0 /* draw_id, always 0 per spec */,
                       state->draw.instance_multiplier,
                       false /* indexed */,
                       is_predicated,
                       tbimr_enabled,
                       false /* uses_base, unused for Gfx11+ */,
                       false /* uses_draw_id, unused for Gfx11+ */,
                       0 /* mocs, unused for Gfx11+ */);
      break;

   case ANV_DGC_DRAW_TYPE_INDEXED:
      genX(write_draw)(cmd_ptr + state->layout.draw.cmd_offset,
                       seq_ptr + state->layout.draw.seq_offset,
                       0 /* draw_id_ptr */,
                       0 /* draw_id, always 0 per spec */,
                       state->draw.instance_multiplier,
                       true /* indexed */,
                       is_predicated,
                       tbimr_enabled,
                       false /* uses_base, unused for Gfx11+ */,
                       false /* uses_draw_id, unused for Gfx11+ */,
                       0 /* mocs, unused for Gfx11+ */);
      break;

#if GFX_VERx10 >= 125
   case ANV_DGC_DRAW_TYPE_MESH:
      genX(write_3DMESH_3D)(cmd_ptr + state->layout.draw.cmd_offset,
                            seq_ptr + state->layout.draw.seq_offset,
                            is_predicated,
                            tbimr_enabled);
      break;
#endif
   }
}

#if GFX_VERx10 >= 125
static void
emit_dispatch_commands(global void *cmd_base,
                       uint32_t cmd_stride,
                       uint32_t seq_idx,
                       uint32_t prolog_size,
                       global void *push_data_ptr,
                       global struct anv_dgc_cs_layout *layout,
                       global struct anv_dgc_cs_descriptor *descriptor,
                       global void *interface_descriptor_data_ptr,
                       uint32_t flags,
                       global VkDispatchIndirectCommand *info)
{
   global void *cmd_ptr = get_ptr(cmd_base, cmd_stride, prolog_size, seq_idx);

   uint64_t pc_addr = (uint64_t)push_data_ptr + descriptor->push_data_offset;

   struct GENX(COMPUTE_WALKER) v = {
      .PredicateEnable = (flags & ANV_GENERATED_FLAG_PREDICATED) != 0,
      .body = {
         .ThreadGroupIDXDimension = info->x,
         .ThreadGroupIDYDimension = info->y,
         .ThreadGroupIDZDimension = info->z,
         .ExecutionMask           = descriptor->right_mask,
         .InlineData              = {
            descriptor->gfx125.inline_dwords[0] == ANV_INLINE_DWORD_PUSH_ADDRESS_LDW ?
            pc_addr & 0xffffffff : ((global uint32_t *)push_data_ptr)[descriptor->gfx125.inline_dwords[0]],
            descriptor->gfx125.inline_dwords[0] == ANV_INLINE_DWORD_PUSH_ADDRESS_LDW ?
            pc_addr >> 32 : ((global uint32_t *)push_data_ptr)[descriptor->gfx125.inline_dwords[1]],
            ((global uint32_t *)push_data_ptr)[descriptor->gfx125.inline_dwords[2]],
            ((global uint32_t *)push_data_ptr)[descriptor->gfx125.inline_dwords[3]],
            ((global uint32_t *)push_data_ptr)[descriptor->gfx125.inline_dwords[4]],
            ((global uint32_t *)push_data_ptr)[descriptor->gfx125.inline_dwords[5]],
            ((global uint32_t *)push_data_ptr)[descriptor->gfx125.inline_dwords[6]],
            ((global uint32_t *)push_data_ptr)[descriptor->gfx125.inline_dwords[7]],
         },
      },
   };
   GENX(COMPUTE_WALKER_repack)(cmd_ptr, descriptor->gfx125.compute_walker, &v);
}
#else
static void
emit_dispatch_commands(global void *cmd_base,
                       uint32_t cmd_stride,
                       uint32_t seq_idx,
                       uint32_t cmd_prolog_size,
                       global void *data_ptr,
                       global struct anv_dgc_cs_layout *layout,
                       global struct anv_dgc_cs_descriptor *descriptor,
                       global void *interface_descriptor_data_ptr,
                       uint32_t flags,
                       global VkDispatchIndirectCommand *info)
{
   global void *cmd_ptr = get_ptr(cmd_base, cmd_stride, cmd_prolog_size, seq_idx);

   if (layout->indirect_set.active != 0) {
      /* Emit MEDIA_VFE_STATE either for each sequence */
      genX(copy_data)(cmd_ptr, descriptor->gfx9.media_vfe_state,
                      sizeof(descriptor->gfx9.media_vfe_state));
      cmd_ptr += sizeof(descriptor->gfx9.media_vfe_state);

      /* Load the shader descriptor */
      global void *idd_ptr = data_ptr + layout->indirect_set.data_offset;
      merge_dwords(idd_ptr,
                   interface_descriptor_data_ptr,
                   descriptor->gfx9.interface_descriptor_data,
                   GENX(INTERFACE_DESCRIPTOR_DATA_length));

      uint32_t idd_offset =
         ANV_DYNAMIC_VISIBLE_HEAP_OFFSET + ((uint64_t)idd_ptr) & 0xffffffff;

      struct GENX(MEDIA_INTERFACE_DESCRIPTOR_LOAD) mdd = {
         GENX(MEDIA_INTERFACE_DESCRIPTOR_LOAD_header),
         .InterfaceDescriptorTotalLength      = GENX(INTERFACE_DESCRIPTOR_DATA_length) * 4,
         .InterfaceDescriptorDataStartAddress = idd_offset,
      };
      GENX(MEDIA_INTERFACE_DESCRIPTOR_LOAD_pack)(cmd_ptr, &mdd);
      cmd_ptr += GENX(MEDIA_INTERFACE_DESCRIPTOR_LOAD_length) * 4;
   }

   /* Push constant offset relative to the dynamic state heap */
   uint32_t dyn_push_data_offset =
      ANV_DYNAMIC_VISIBLE_HEAP_OFFSET + (((uint64_t)data_ptr) & 0xffffffff);

   struct GENX(MEDIA_CURBE_LOAD) mdl = {
      GENX(MEDIA_CURBE_LOAD_header),
      .CURBETotalDataLength    = descriptor->gfx9.cross_thread_push_size +
                                 descriptor->gfx9.n_threads *
                                 descriptor->gfx9.per_thread_push_size,
      .CURBEDataStartAddress   = dyn_push_data_offset,
   };
   GENX(MEDIA_CURBE_LOAD_pack)(cmd_ptr, &mdl);
   cmd_ptr += GENX(MEDIA_CURBE_LOAD_length) * 4;

   /* Emit the walker */
   struct GENX(GPGPU_WALKER) walker = {
      .PredicateEnable           = (flags & ANV_GENERATED_FLAG_PREDICATED) != 0,
      .SIMDSize                  = descriptor->simd_size / 16,
      .ThreadWidthCounterMaximum = descriptor->threads - 1,
      .RightExecutionMask        = descriptor->right_mask,
      .BottomExecutionMask       = 0xffffffff,
      .ThreadGroupIDXDimension   = info->x,
      .ThreadGroupIDYDimension   = info->y,
      .ThreadGroupIDZDimension   = info->z,
   };
   GENX(GPGPU_WALKER_repack)(cmd_ptr, descriptor->gfx9.gpgpu_walker, &walker);
   global uint32_t *walker_ptr = cmd_ptr;
   cmd_ptr += GENX(GPGPU_WALKER_length) * 4;

   uint32_t per_thread_push_size = descriptor->gfx9.per_thread_push_size;
   if (per_thread_push_size > 0) {
      uint32_t cross_thread_push_size = descriptor->gfx9.cross_thread_push_size;
      global void *per_thread_ptr0 = data_ptr + cross_thread_push_size;
      global void *per_thread_ptr = per_thread_ptr0;
      for (uint32_t t = 0; t < descriptor->gfx9.n_threads; t++) {
         if (t > 0) {
            genX(copy_data)(per_thread_ptr, per_thread_ptr0,
                            per_thread_push_size);
         }
         *(uint32_t*)(per_thread_ptr + descriptor->gfx9.subgroup_id_offset) = t;
         per_thread_ptr += per_thread_push_size;
      }
   }

   struct GENX(MEDIA_STATE_FLUSH) flush = {
      GENX(MEDIA_STATE_FLUSH_header),
   };
   GENX(MEDIA_STATE_FLUSH_pack)(cmd_ptr, &flush);
}
#endif

void
genX(libanv_preprocess_cs_generate)(global void *cmd_base,
                                    uint32_t cmd_stride,
                                    global void *data_base,
                                    uint32_t data_stride,
                                    global void *seq_base,
                                    uint32_t seq_stride,
                                    global uint32_t *seq_count,
                                    uint32_t max_seq_count,
                                    uint32_t cmd_prolog_size,
                                    uint32_t data_prolog_size,
                                    global struct anv_dgc_cs_layout *layout,
                                    global struct anv_dgc_cs_descriptor *indirect_set,
                                    global void *interface_descriptor_data_ptr,
                                    global void *const_ptr,
                                    uint32_t const_size,
                                    global void *driver_const_ptr,
                                    uint64_t return_addr,
                                    uint32_t flags,
                                    uint32_t seq_idx)
{
   uint32_t max_count = seq_count != 0 ? min(*seq_count, max_seq_count) : max_seq_count;

   if (seq_idx == 0) {
      write_prolog_epilog(cmd_base, cmd_stride, max_count,
                          cmd_prolog_size, seq_idx, return_addr);
   }

   if (seq_idx >= max_count)
      return;

   /* Pointer to the application generated data, layed out as described in
    * stream_layout.
    */
   global void *seq_ptr = seq_base + seq_idx * seq_stride;

   /* Get the shader descriptor. */
   global struct anv_dgc_cs_descriptor *descriptor;
   if (layout->indirect_set.active != 0) {
      uint32_t set_idx = *(global uint32_t *)(seq_ptr + layout->indirect_set.seq_offset);
      descriptor = &indirect_set[set_idx];
   } else {
      descriptor = indirect_set;
   }

   /* Prepare the push constant data. */
   uint32_t push_data_offset = descriptor->push_data_offset;

   /* */
   global void *push_data_ptr =
      get_ptr(data_base, data_stride, data_prolog_size, seq_idx) +
      layout->push_constants.data_offset;
#if GFX_VERx10 >= 125
   write_app_push_constant_data(
      push_data_ptr, &layout->push_constants,
      seq_ptr, const_ptr, const_size, seq_idx);
   write_cs_drv_push_constant_data(
      push_data_ptr, driver_const_ptr,
      MAX_PUSH_CONSTANTS_SIZE,
      ANV_DRIVER_PUSH_CONSTANTS_SIZE,
      seq_ptr + layout->dispatch.seq_offset);
#else
   write_app_push_constant_data(
      push_data_ptr, &layout->push_constants,
      seq_ptr, const_ptr, const_size, seq_idx);
   write_cs_drv_push_constant_data(
      push_data_ptr - descriptor->push_data_offset, driver_const_ptr,
      MAX2(descriptor->push_data_offset, MAX_PUSH_CONSTANTS_SIZE),
      MIN2(ANV_DRIVER_PUSH_CONSTANTS_SIZE,
           (MAX_PUSH_CONSTANTS_SIZE + ANV_DRIVER_PUSH_CONSTANTS_SIZE) -
           descriptor->push_data_offset),
      seq_ptr + layout->dispatch.seq_offset);
#endif

   /* Finally write the commands */
   emit_dispatch_commands(cmd_base, cmd_stride, seq_idx, cmd_prolog_size,
                          push_data_ptr, layout, descriptor,
                          interface_descriptor_data_ptr, flags,
                          seq_ptr + layout->dispatch.seq_offset);
}

void
genX(libanv_postprocess_cs_generate)(global void *cmd_base,
                                     uint32_t cmd_stride,
                                     global void *data_base,
                                     uint32_t data_stride,
                                     global uint32_t *seq_count,
                                     uint32_t max_seq_count,
                                     uint32_t cmd_prolog_size,
                                     uint32_t data_prolog_size,
                                     uint32_t data_idd_offset,
                                     global struct anv_dgc_cs_descriptor *descriptor,
                                     uint64_t return_addr,
                                     uint32_t seq_idx)
{
   uint32_t max_count = seq_count != 0 ? min(*seq_count, max_seq_count) : max_seq_count;

   if (seq_idx == 0) {
      write_prolog_epilog(cmd_base, cmd_stride, max_count,
                          cmd_prolog_size, seq_idx, return_addr);
   }

   if (seq_idx >= max_count)
      return;

   /* Where to write the commands */
   global void *cmd_ptr =
      get_ptr(cmd_base, cmd_stride, cmd_prolog_size, seq_idx);

   /* OR the driver INTERFACE_DESCRIPTOR_DATA dwords with the device generated
    * ones.
    */
   uint32_t n_dwords = 2; /* dwords covered from
                           * INTERFACE_DESCRIPTOR_DATA::SamplerCount to
                           * INTERFACE_DESCRIPTOR_DATA::BindingTablePointer
                           */

#if GFX_VERx10 >= 125
   uint32_t idd_offset_B = 12 /* offset in INTERFACE_DESCRIPTOR_DATA */;
   uint32_t csw_body_offset_B = (GFX_VERx10 >= 200 ? 72 : 68) /* offset in COMPUTE_WALKER_BODY */;
   uint32_t csw_offset_B = 4 /* offset in COMPUTE_WALKER */;
   uint32_t inst_offset_B = csw_offset_B + csw_body_offset_B + idd_offset_B;
   merge_dwords(cmd_ptr + inst_offset_B,
                cmd_ptr + inst_offset_B,
                &descriptor->gfx125.compute_walker[inst_offset_B / 4],
                n_dwords);
#else
   global void *idd_ptr =
      get_ptr(data_base, data_stride, data_prolog_size, seq_idx) +
      data_idd_offset;
   uint32_t inst_offset_B = 12 /* offset in INTERFACE_DESCRIPTOR_DATA */;
   merge_dwords(idd_ptr + inst_offset_B,
                idd_ptr + inst_offset_B,
                &descriptor->gfx9.interface_descriptor_data[inst_offset_B / 4],
                n_dwords);
#endif
}

#if GFX_VERx10 >= 125
static uint3
calc_local_trace_size(uint3 global_size)
{
   unsigned total_shift = 0;
   uint3 local_shift = (uint3)(0, 0, 0);

   bool progress;
   do {
      progress = false;
      for (unsigned i = 0; i < 3; i++) {
         if ((1 << local_shift[i]) < global_size[i]) {
            progress = true;
            local_shift[i]++;
            total_shift++;
         }

         if (total_shift == 3)
            return local_shift;
      }
   } while (progress);

   /* Assign whatever's left to x */
   local_shift[0] += 3 - total_shift;

   return local_shift;
}

void
genX(libanv_preprocess_rt_generate)(global void *cmd_base,
                                    uint32_t cmd_stride,
                                    global void *data_base,
                                    uint32_t data_stride,
                                    global void *seq_base,
                                    uint32_t seq_stride,
                                    global uint32_t *seq_count,
                                    uint32_t max_seq_count,
                                    uint32_t cmd_prolog_size,
                                    uint32_t data_prolog_size,
                                    global struct anv_dgc_cs_layout *layout,
                                    global void *compute_walker_template,
                                    global void *rtdg_global_template,
                                    global void *const_ptr,
                                    uint32_t const_size,
                                    global void *driver_const_ptr,
                                    uint64_t return_addr,
                                    uint32_t flags,
                                    uint32_t seq_idx)
{
   uint32_t max_count = seq_count != 0 ? min(*seq_count, max_seq_count) : max_seq_count;

   if (seq_idx == 0) {
      write_prolog_epilog(cmd_base, cmd_stride, max_count,
                          cmd_prolog_size, seq_idx, return_addr);
   }

   if (seq_idx >= max_count)
      return;

   /* Where to write the commands */
   global void *cmd_ptr =
      get_ptr(cmd_base, cmd_stride, cmd_prolog_size, seq_idx);

   /* Pointer to the application generated data, layed out as described in
    * stream_layout.
    */
   global void *seq_ptr = seq_base + seq_idx * seq_stride;

   VkTraceRaysIndirectCommand2KHR *info =
      ((global VkTraceRaysIndirectCommand2KHR *)(seq_ptr + layout->dispatch.seq_offset));
   uint3 launch_size = (uint3)(info->width, info->height, info->depth);

   /* RTDG + push constants */
   global void *push_data_ptr =
      get_ptr(data_base, data_stride, data_prolog_size, seq_idx) +
      layout->push_constants.data_offset;
   global void *rtdg_ptr = push_data_ptr;
   struct GENX(RT_DISPATCH_GLOBALS) rtdg = {
      .LaunchWidth  = launch_size.x,
      .LaunchHeight = launch_size.y,
      .LaunchDepth  = launch_size.z,
#if GFX_VER >= 30
      .HitGroupStride      = info->hitShaderBindingTableStride,
      .HitGroupTable       = info->hitShaderBindingTableAddress,
      .MissGroupTable      = info->missShaderBindingTableAddress,
      .MissGroupStride     = info->missShaderBindingTableStride,
      .CallableGroupTable  = info->callableShaderBindingTableAddress,
      .CallableGroupStride = info->callableShaderBindingTableStride,
#else
      .HitGroupTable = (struct GENX(RT_SHADER_TABLE)) {
         .BaseAddress = info->hitShaderBindingTableAddress,
         .Stride      = info->hitShaderBindingTableStride,
      },
      .MissGroupTable = (struct GENX(RT_SHADER_TABLE)) {
         .BaseAddress = info->missShaderBindingTableAddress,
         .Stride      = info->missShaderBindingTableStride,
      },
      .CallableGroupTable = (struct GENX(RT_SHADER_TABLE)) {
         .BaseAddress = info->callableShaderBindingTableAddress,
         .Stride      = info->callableShaderBindingTableStride,
      },
#endif
   };
   GENX(RT_DISPATCH_GLOBALS_repack)(rtdg_ptr, rtdg_global_template, &rtdg);

   write_app_push_constant_data(
      push_data_ptr + ANV_DGC_RT_GLOBAL_DISPATCH_SIZE,
      &layout->push_constants,
      seq_ptr, const_ptr, const_size, seq_idx);
   write_rt_drv_push_constant_data(
      push_data_ptr +
      ANV_DGC_RT_GLOBAL_DISPATCH_SIZE +
      MAX_PUSH_CONSTANTS_SIZE,
      driver_const_ptr,
      ANV_DRIVER_PUSH_CONSTANTS_SIZE);

   uint3 local_size_log2 = calc_local_trace_size(launch_size);
   uint3 one = 1;
   uint3 local_size = one << local_size_log2;
   uint3 global_size = DIV_ROUND_UP(launch_size, local_size);

   /* Finally write the commands */
   global uint64_t *sbt = (global uint64_t *)info->raygenShaderRecordAddress;
   struct GENX(COMPUTE_WALKER) v = {
      .PredicateEnable = (flags & ANV_GENERATED_FLAG_PREDICATED) != 0,
      .body = {
         .LocalXMaximum           = (1u << local_size_log2.x) - 1,
         .LocalYMaximum           = (1u << local_size_log2.y) - 1,
         .LocalZMaximum           = (1u << local_size_log2.z) - 1,
         .ThreadGroupIDXDimension = global_size.x,
         .ThreadGroupIDYDimension = global_size.y,
         .ThreadGroupIDZDimension = global_size.z,
         /* See struct brw_rt_raygen_trampoline_params */
         .InlineData              = {
            ((uint64_t) rtdg_ptr) & 0xffffffff,
            ((uint64_t) rtdg_ptr) >> 32,
            info->raygenShaderRecordAddress & 0xffffffff,
            info->raygenShaderRecordAddress >> 32,
            local_size_log2.x << 8 |
            local_size_log2.y << 16 |
            local_size_log2.z << 24,
         },
      },
   };
   GENX(COMPUTE_WALKER_repack)(cmd_ptr, compute_walker_template, &v);
}
#endif /* GFX_VERx10 >= 125 */

#endif /* GFX_VER >= 9 */
