/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdbool.h>

#include "genxml/genX_bits.h"
#include "shaders/libintel_shaders.h"

#include "anv_private.h"

/* The DGC preprocess command buffer layout is separated in 4 parts:
 *
 * +--------+----------+--------+------+
 * | prolog | commands | epilog | data |
 * +--------+----------+--------+------+
 *
 * The prolog consist of a few commands to deal with the command buffer
 * prefetch and editing some the return address in the epilog part.
 *
 * The commands is where the generated commands are located.
 *
 * The epilog is where the jump back to the calling command buffer happens.
 *
 * Data is where things like INTERFACE_DESCRIPTOR_DATA is located (on pre
 * Gfx12.5) and the push constant data used by the commands.
 */

static uint32_t
draw_cmd_size(const struct intel_device_info *devinfo,
              const struct vk_indirect_command_layout *vk_layout)
{
   return 4 *
      ((vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_DRAW_MESH)) ?
       _3DMESH_3D_length(devinfo) :
       devinfo->ver >= 11 ? _3DPRIMITIVE_EXTENDED_length(devinfo) :
       _3DPRIMITIVE_length(devinfo));

}

static void
layout_add_command(struct anv_indirect_command_layout *layout, uint32_t size,
                   const char *name)
{
   layout->cmd_size = align(layout->cmd_size, 4);
   layout->cmd_size += size;

   layout->items[layout->n_items++] = (struct anv_indirect_command_layout_item) {
      .name = name,
      .size = size,
   };
}

static void
layout_add_data(struct anv_indirect_command_layout *layout,
                uint32_t size, uint32_t alignment,
                uint16_t *out_data_offset)
{
   layout->data_size = align(layout->data_size, alignment);
   if (out_data_offset)
      *out_data_offset = layout->data_size;
   layout->data_size += size;
}

static void
push_layout_add_range(struct anv_dgc_push_layout *pc_layout,
                      const struct vk_indirect_command_push_constant_layout *vk_pc_layout)
{
   pc_layout->entries[pc_layout->num_entries++] = (struct anv_dgc_push_entry) {
      .seq_offset  = vk_pc_layout->src_offset_B,
      .push_offset = vk_pc_layout->dst_offset_B,
      .size        = vk_pc_layout->size_B,
   };
}

static uint32_t
push_constant_command_size(const struct intel_device_info *devinfo,
                           mesa_shader_stage stage,
                           uint32_t n_slots)
{
   uint32_t dwords = 0;
   switch (stage) {
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_TESS_CTRL:
   case MESA_SHADER_TESS_EVAL:
   case MESA_SHADER_GEOMETRY:
   case MESA_SHADER_FRAGMENT:
      if (devinfo->ver >= 12) {
         dwords += (_3DSTATE_CONSTANT_ALL_length(devinfo) +
                    n_slots * _3DSTATE_CONSTANT_ALL_DATA_length(devinfo));
      } else {
         dwords += _3DSTATE_CONSTANT_VS_length(devinfo);
      }
      break;
   case MESA_SHADER_MESH:
      dwords += _3DSTATE_MESH_SHADER_DATA_length(devinfo);
      break;
   case MESA_SHADER_TASK:
      dwords += _3DSTATE_TASK_SHADER_DATA_length(devinfo);
      break;
   default:
      UNREACHABLE("Invalid stage");
   }
   return 4 * dwords;
}

VkResult anv_CreateIndirectCommandsLayoutEXT(
    VkDevice                                    _device,
    const VkIndirectCommandsLayoutCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkIndirectCommandsLayoutEXT*                pIndirectCommandsLayout)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   const struct intel_device_info *devinfo = device->info;
   struct anv_indirect_command_layout *layout_obj;

   layout_obj = vk_indirect_command_layout_create(
      &device->vk, pCreateInfo, pAllocator,
      sizeof(struct anv_indirect_command_layout));
   if (!layout_obj)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_indirect_command_layout *vk_layout = &layout_obj->vk;

   const bool is_gfx =
      (vk_layout->dgc_info &
       (BITFIELD_BIT(MESA_VK_DGC_DRAW) |
        BITFIELD_BIT(MESA_VK_DGC_DRAW_INDEXED) |
        BITFIELD_BIT(MESA_VK_DGC_DRAW_MESH))) != 0;

   struct anv_dgc_gfx_layout *gfx_layout = &layout_obj->gfx_layout;
   struct anv_dgc_cs_layout *cs_layout = &layout_obj->cs_layout;
   struct anv_dgc_push_layout *pc_layout =
      is_gfx ? &gfx_layout->push_constants : &cs_layout->push_constants;

   /* Some checks that the OpenCL code stays in sync. */
   STATIC_ASSERT(ANV_DGC_RT_GLOBAL_DISPATCH_SIZE == BRW_RT_PUSH_CONST_OFFSET);

   /* Keep this in sync with generate_commands.cl:write_prolog_epilog() */
   layout_obj->cmd_prolog_size = 4 *
      (MI_STORE_DATA_IMM_length(devinfo) + 1 +
       MI_BATCH_BUFFER_START_length(devinfo) +
       (devinfo->ver >= 12 ? MI_ARB_CHECK_length(devinfo) : 0));
   layout_obj->cmd_epilog_size = 4 * MI_BATCH_BUFFER_START_length(devinfo);

   /* On <= Gfx12.0 the gl_NumWorkGroups is located in the push constants so
    * we need push constant data per sequence.
    */
   const bool has_per_sequence_constants = true;

   if (has_per_sequence_constants) {
      /* RT & compute need a combined push constants and also Mesh. */
      uint32_t pc_size = sizeof(struct anv_push_constants);
      /* Prior to Gfx12.5+, there is no HW mechanism in the HW thread
       * generation to provide a workgroup local id. The way the workgroup
       * local id is provided is through a per-thread push constant mechanism
       * that read a per thread 32B (one GRF) piece of data in which the
       * driver writes the thread id.
       *
       * The maximum workgroup size is 1024. With a worse case dispatch size
       * of SIMD8, that means at max 128 HW threads, each needing a 32B for
       * its subgroup_id value within the workgroup. 32B * 128 = 4096B.
       */
      if (devinfo->verx10 < 125 &&
          (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_DISPATCH)))
         pc_size += 4096;

      /* RT_DISPATCH_GLOBALS is located just before the push constant data. */
      if (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_RT))
         pc_size += ANV_DGC_RT_GLOBAL_DISPATCH_SIZE;

      layout_add_data(layout_obj, pc_size, ANV_UBO_ALIGNMENT,
                      &pc_layout->data_offset);

      for (uint32_t i = 0; i < vk_layout->n_pc_layouts; i++) {
         const struct vk_indirect_command_push_constant_layout *vk_pc_layout =
            &vk_layout->pc_layouts[i];
         push_layout_add_range(pc_layout, vk_pc_layout);
      }

      if (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_SI)) {
         pc_layout->seq_id_active = true;
         pc_layout->seq_id_offset = vk_layout->si_layout.dst_offset_B;
      }

      pc_layout->mocs =
         isl_mocs(&device->isl_dev, ISL_SURF_USAGE_CONSTANT_BUFFER_BIT, false);
   }

   /* Graphics */
   if (vk_layout->dgc_info & (BITFIELD_BIT(MESA_VK_DGC_DRAW) |
                              BITFIELD_BIT(MESA_VK_DGC_DRAW_INDEXED) |
                              BITFIELD_BIT(MESA_VK_DGC_DRAW_MESH))) {
      layout_obj->bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;

      /* 3DSTATE_INDEX_BUFFER */
      if (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_IB)) {
         layout_add_command(layout_obj,
                            _3DSTATE_INDEX_BUFFER_length(devinfo) * 4,
                            "index");
      }

      /* 3DSTATE_VERTEX_BUFFERS */
      if (devinfo->ver == 9) {
         const uint32_t n_vb_entries =
            2 + util_bitcount(vk_layout->vertex_bindings);
         layout_add_command(layout_obj,
                            (1 /* TODO: _3DSTATE_VERTEX_BUFFERS_length(devinfo) */ +
                             /* Number of vertex buffers + draw params (Gfx9 only) */
                             n_vb_entries *
                             VERTEX_BUFFER_STATE_length(devinfo)) * 4,
                            "vertex");
         if (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_VB)) {
            layout_add_command(layout_obj,
                               PIPE_CONTROL_length(devinfo) * 4,
                               "vertex cache inval");
         }
         /* Draw params data, gl_BaseInstance, gl_BaseVertex, gl_DrawID */
         layout_add_data(layout_obj, 4 * 3, 4, NULL);
      } else if (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_VB)) {
         layout_add_command(layout_obj,
                            (1 /* TODO: _3DSTATE_VERTEX_BUFFERS_length(devinfo) */ +
                             /* Number of vertex buffers */
                             util_bitcount(vk_layout->vertex_bindings) *
                             VERTEX_BUFFER_STATE_length(devinfo)) * 4,
                            "vertex");
      }

      if ((vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_DRAW_MESH)) == 0) {
         if (intel_needs_workaround(device->info, 16011107343))
            layout_add_command(layout_obj, _3DSTATE_HS_length(devinfo) * 4, "hs");
         if (intel_needs_workaround(device->info, 22018402687))
            layout_add_command(layout_obj, _3DSTATE_DS_length(devinfo) * 4, "ds");
      }

      const VkShaderStageFlags draw_stages =
         (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_DRAW_MESH)) ?
         (VK_SHADER_STAGE_TASK_BIT_EXT |
          VK_SHADER_STAGE_MESH_BIT_EXT |
          VK_SHADER_STAGE_FRAGMENT_BIT) :
         (VK_SHADER_STAGE_VERTEX_BIT |
          VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
          VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT |
          VK_SHADER_STAGE_GEOMETRY_BIT |
          VK_SHADER_STAGE_FRAGMENT_BIT);
      const bool need_push_constants =
         (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_IES)) != 0 ||
         (vk_layout->dgc_info & (BITFIELD_BIT(MESA_VK_DGC_PC) |
                                 BITFIELD_BIT(MESA_VK_DGC_SI))) != 0;

      /* 3DSTATE_CONSTANT_* */
      if (need_push_constants) {
         uint32_t cmd_size = 0;
         anv_foreach_vk_stage(vk_stage, draw_stages) {
            cmd_size += push_constant_command_size(
               devinfo, vk_to_mesa_shader_stage(vk_stage), 4);
         }
         layout_add_command(layout_obj, cmd_size, "push-constants");
      }

      /* 3DPRIMITIVE / 3DMESH_3D */
      layout_add_command(layout_obj, draw_cmd_size(devinfo, vk_layout), "draw");
      gfx_layout->draw.seq_offset = vk_layout->draw_src_offset_B;
   }

   /* Compute */
   if (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_DISPATCH)) {
      layout_obj->bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;

      if (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_IES)) {
         cs_layout->indirect_set.active = true;
         cs_layout->indirect_set.seq_offset = vk_layout->ies_src_offset_B;
      }

      cs_layout->dispatch.seq_offset = vk_layout->dispatch_src_offset_B;
      if (devinfo->verx10 >= 125) {
         /* On Gfx12.5+ everything is in a single instruction */
         uint32_t cmd_size = COMPUTE_WALKER_length(devinfo) * 4;
         layout_add_command(layout_obj, cmd_size, "compute-walker");
      } else {
         /* Prior generations  */
         uint32_t cmd_size = 4 * (MEDIA_CURBE_LOAD_length(devinfo) +
                                  GPGPU_WALKER_length(devinfo) +
                                  MEDIA_STATE_FLUSH_length(devinfo));

         if (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_IES)) {
            cmd_size += 4 * (MEDIA_VFE_STATE_length(devinfo) +
                             MEDIA_INTERFACE_DESCRIPTOR_LOAD_length(devinfo));
            layout_add_data(layout_obj,
                            4 * INTERFACE_DESCRIPTOR_DATA_length(devinfo), 64,
                            &cs_layout->indirect_set.data_offset);
         }

         layout_add_command(layout_obj, cmd_size,
                            "media-curbe,gpgpu-walker,media-state");
      }
   }

   /* Ray-tracing */
   if (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_RT)) {
      layout_obj->bind_point = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;

      uint32_t cmd_size = COMPUTE_WALKER_length(devinfo) * 4;
      layout_add_command(layout_obj, cmd_size, "compute-walker");

      cs_layout->dispatch.seq_offset = vk_layout->dispatch_src_offset_B;
   }

   layout_obj->data_prolog_size = align(layout_obj->data_prolog_size, 64);
   layout_obj->data_size = align(layout_obj->data_size, ANV_UBO_ALIGNMENT);

   layout_obj->emits_push_constants =
      (vk_layout->dgc_info & (BITFIELD_BIT(MESA_VK_DGC_PC) |
                              BITFIELD_BIT(MESA_VK_DGC_SI))) ||
      ((vk_layout->dgc_info & (BITFIELD_BIT(MESA_VK_DGC_DRAW) |
                               BITFIELD_BIT(MESA_VK_DGC_DRAW_INDEXED) |
                               BITFIELD_BIT(MESA_VK_DGC_DRAW_MESH))) &&
       (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_IES)));

   *pIndirectCommandsLayout = anv_indirect_command_layout_to_handle(layout_obj);

   return VK_SUCCESS;
}

void
anv_DestroyIndirectCommandsLayoutEXT(
    VkDevice                                    _device,
    VkIndirectCommandsLayoutEXT                 indirectCommandsLayout,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_indirect_command_layout, layout, indirectCommandsLayout);

   if (!layout)
      return;

   vk_indirect_command_layout_destroy(&device->vk, pAllocator, &layout->vk);
}

void anv_GetGeneratedCommandsMemoryRequirementsEXT(
    VkDevice                                    _device,
    const VkGeneratedCommandsMemoryRequirementsInfoEXT* pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_indirect_command_layout, layout_obj,
                   pInfo->indirectCommandsLayout);
   const struct intel_device_info *devinfo = device->info;

   pMemoryRequirements->memoryRequirements.alignment = 64;
   pMemoryRequirements->memoryRequirements.size =
      align(layout_obj->cmd_prolog_size + layout_obj->cmd_epilog_size +
            pInfo->maxSequenceCount * layout_obj->cmd_size, 64) +
      align(pInfo->maxSequenceCount * layout_obj->data_size, 64) +
      align(layout_obj->data_prolog_size, 64);
   pMemoryRequirements->memoryRequirements.memoryTypeBits =
      devinfo->verx10 <= 120 ?
      device->physical->memory.dynamic_visible_mem_types :
      device->physical->memory.default_buffer_mem_types;

   if (!device->physical->has_scratch_page) {
      pMemoryRequirements->memoryRequirements.size +=
         MAX2(devinfo->engine_class_prefetch[INTEL_ENGINE_CLASS_RENDER],
              devinfo->engine_class_prefetch[INTEL_ENGINE_CLASS_COMPUTE]);
   }
}

void
anv_dgc_fill_gfx_state(struct anv_dgc_gfx_state *state,
                       struct anv_cmd_buffer *cmd_buffer,
                       const struct anv_indirect_command_layout *layout,
                       struct anv_shader ** const shaders)
{
   struct anv_device *device = cmd_buffer->device;
   const struct vk_indirect_command_layout *vk_layout = &layout->vk;
   struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;

   if (vk_layout->dgc_info & (BITFIELD_BIT(MESA_VK_DGC_PC) |
                              BITFIELD_BIT(MESA_VK_DGC_SI))) {

      for (uint32_t i = 0; i < ANV_GRAPHICS_SHADER_STAGE_COUNT; i++) {
         if (shaders[i] == NULL)
            continue;

         const struct anv_pipeline_bind_map *bind_map = &shaders[i]->bind_map;
         for (uint32_t i = 0; i < ARRAY_SIZE(bind_map->push_ranges); i++) {
            const struct anv_push_range *range = &bind_map->push_ranges[i];
            if (range->length == 0)
               break;

            switch (range->set) {
            case ANV_DESCRIPTOR_SET_DESCRIPTORS:
               if (bind_map->layout_type == ANV_PIPELINE_DESCRIPTOR_SET_LAYOUT_TYPE_BUFFER) {
                  state->push_constants.addresses[i] =
                     anv_cmd_buffer_descriptor_buffer_address(
                        cmd_buffer,
                        gfx->base.descriptor_buffers[range->index].buffer_index) +
                     gfx->base.descriptor_buffers[range->index].buffer_offset;
               } else {
                  struct anv_descriptor_set *set = gfx->base.descriptors[range->index];
                  state->push_constants.addresses[i] = anv_address_physical(
                     anv_descriptor_set_address(set));
               }
               break;

            case ANV_DESCRIPTOR_SET_PUSH_CONSTANTS:
               break;

            case ANV_DESCRIPTOR_SET_NULL:
            case ANV_DESCRIPTOR_SET_PER_PRIM_PADDING:
               state->push_constants.addresses[i] =
                     anv_address_physical(device->workaround_address);
               break;

            default: {
               struct anv_descriptor_set *set = gfx->base.descriptors[range->set];
               const struct anv_descriptor *desc =
                  &set->descriptors[range->index];

               if (desc->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
                  if (desc->buffer) {
                     state->push_constants.addresses[i] = anv_address_physical(
                        anv_address_add(desc->buffer->address,
                                        desc->offset));
                  }
               } else {
                  assert(desc->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
                  if (desc->buffer) {
                     const struct anv_cmd_pipeline_state *pipe_state = &gfx->base;
                     uint32_t dynamic_offset =
                        pipe_state->dynamic_offsets[
                           range->set].offsets[range->dynamic_offset_index];
                     state->push_constants.addresses[i] = anv_address_physical(
                        anv_address_add(desc->buffer->address,
                                           desc->offset + dynamic_offset));
                  }
               }

               if (state->push_constants.addresses[i] == 0) {
                  /* For NULL UBOs, we just return an address in the
                   * workaround BO. We do writes to it for workarounds but
                   * always at the bottom. The higher bytes should be all
                   * zeros.
                   */
                  assert(range->length * 32 <= 2048);
                  state->push_constants.addresses[i] =
                     anv_address_physical((struct anv_address) {
                           .bo = device->workaround_bo,
                           .offset = 1024,
                        });
               }
            }
            }
         }
      }
   }

   state->draw.instance_multiplier = gfx->instance_multiplier;
}

/* This function determines the final layout of GFX generated commands. A lot
 * of things make the amount of space vary (number of stages, number of push
 * constant slots, etc...) such that we can only determine this just before
 * executing the generation.
 */
uint32_t
anv_dgc_fill_gfx_layout(struct anv_dgc_gfx_layout *layout,
                        const struct anv_device *device,
                        const struct anv_indirect_command_layout *layout_obj,
                        struct anv_shader ** const shaders)
{
   const struct vk_indirect_command_layout *vk_layout = &layout_obj->vk;
   const struct intel_device_info *devinfo = device->info;

   uint32_t cmd_offset = 0;

   layout->draw.draw_type =
      (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_DRAW_MESH)) ? ANV_DGC_DRAW_TYPE_MESH :
      (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_DRAW_INDEXED)) ? ANV_DGC_DRAW_TYPE_INDEXED :
      ANV_DGC_DRAW_TYPE_SEQUENTIAL;

   layout->index_buffer.cmd_offset = cmd_offset;
   if (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_IB)) {
      layout->index_buffer.cmd_size = _3DSTATE_INDEX_BUFFER_length(devinfo) * 4;
      layout->index_buffer.seq_offset = vk_layout->index_src_offset_B;
      layout->index_buffer.mocs =
         isl_mocs(&device->isl_dev, ISL_SURF_USAGE_INDEX_BUFFER_BIT, false);
      if (vk_layout->index_mode_is_dx) {
         /* DXGI_FORMAT values */
         layout->index_buffer.u32_value = 42;
         layout->index_buffer.u16_value = 57;
         layout->index_buffer.u8_value  = 62;
      } else {
         layout->index_buffer.u32_value = VK_INDEX_TYPE_UINT32;
         layout->index_buffer.u16_value = VK_INDEX_TYPE_UINT16;
         layout->index_buffer.u8_value  = VK_INDEX_TYPE_UINT8_EXT;
      }

      cmd_offset += layout->index_buffer.cmd_size;
   }

   layout->vertex_buffers.cmd_offset = cmd_offset;
   if (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_VB)) {
      layout->vertex_buffers.cmd_size =
         (1 /* TODO: _3DSTATE_VERTEX_BUFFERS_length(devinfo) */ +
          util_bitcount(vk_layout->vertex_bindings) *
          VERTEX_BUFFER_STATE_length(devinfo)) * 4;
      layout->vertex_buffers.mocs =
         isl_mocs(&device->isl_dev, ISL_SURF_USAGE_VERTEX_BUFFER_BIT, false);
      layout->vertex_buffers.n_buffers = vk_layout->n_vb_layouts;
      for (uint32_t i = 0; i < vk_layout->n_vb_layouts; i++) {
         layout->vertex_buffers.buffers[i].seq_offset =
            vk_layout->vb_layouts[i].src_offset_B;
         layout->vertex_buffers.buffers[i].binding =
            vk_layout->vb_layouts[i].binding;
      }
   }
   if (devinfo->ver == 9) {
      const struct brw_vs_prog_data *vs_prog_data =
         get_shader_vs_prog_data(shaders[MESA_SHADER_VERTEX]);
      if (vs_prog_data->uses_firstvertex ||
          vs_prog_data->uses_baseinstance ||
          vs_prog_data->uses_drawid) {
         layout->vertex_buffers.cmd_size = MAX2(
            layout->vertex_buffers.cmd_size,
            4 /* TODO: _3DSTATE_VERTEX_BUFFERS_length(devinfo) */);
         if (vs_prog_data->uses_firstvertex ||
             vs_prog_data->uses_baseinstance)
            layout->vertex_buffers.cmd_size += VERTEX_BUFFER_STATE_length(devinfo) * 4;
         if (vs_prog_data->uses_drawid)
            layout->vertex_buffers.cmd_size += VERTEX_BUFFER_STATE_length(devinfo) * 4;
      }
      if (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_VB))
         layout->vertex_buffers.cmd_size += PIPE_CONTROL_length(devinfo) * 4;
   }
   cmd_offset += layout->vertex_buffers.cmd_size;

   layout->indirect_set.final_cmds_offset = cmd_offset;
   if (intel_needs_workaround(devinfo, 16011107343) &&
       shaders[MESA_SHADER_TESS_CTRL] != NULL) {
      layout->indirect_set.final_cmds_size +=
         _3DSTATE_HS_length(devinfo) * 4;
   }
   if (intel_needs_workaround(devinfo, 22018402687) &&
       shaders[MESA_SHADER_TESS_EVAL] != NULL) {
      layout->indirect_set.final_cmds_size +=
         _3DSTATE_DS_length(devinfo) * 4;
   }
   cmd_offset += layout->indirect_set.final_cmds_size;

   layout->push_constants.cmd_offset = cmd_offset;
   if (vk_layout->dgc_info & (BITFIELD_BIT(MESA_VK_DGC_PC) |
                              BITFIELD_BIT(MESA_VK_DGC_SI))) {
      struct anv_dgc_push_layout *pc_layout = &layout->push_constants;

      layout->push_constants.flags = ANV_DGC_PUSH_CONSTANTS_CMD_ACTIVE;
      for (uint32_t i = 0; i < vk_layout->n_pc_layouts; i++) {
         const struct vk_indirect_command_push_constant_layout *vk_pc_layout =
            &vk_layout->pc_layouts[i];
         push_layout_add_range(&layout->push_constants, vk_pc_layout);
      }
      if (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_SI)) {
         pc_layout->seq_id_active = true;
         pc_layout->seq_id_offset = vk_layout->si_layout.dst_offset_B;
      }
      pc_layout->mocs =
         isl_mocs(&device->isl_dev, ISL_SURF_USAGE_CONSTANT_BUFFER_BIT, false);


      for (uint32_t i = 0; i < ANV_GRAPHICS_SHADER_STAGE_COUNT; i++) {
         if (shaders[i] == NULL)
            continue;

         const struct anv_pipeline_bind_map *bind_map =
            &shaders[i]->bind_map;
         uint32_t n_slots = bind_map->inline_dwords_count > 0 ? 1 : 0;
         for (uint32_t j = 0; j < ARRAY_SIZE(bind_map->push_ranges); j++) {
            if (bind_map->push_ranges[j].length == 0)
               break;
            n_slots++;
         }
         if (n_slots > 0) {
            layout->push_constants.cmd_size +=
               push_constant_command_size(devinfo, i, n_slots);
         }
      }

      cmd_offset += layout->push_constants.cmd_size;
   }

   layout->draw.cmd_offset = cmd_offset;
   layout->draw.cmd_size = draw_cmd_size(devinfo, vk_layout);
   layout->draw.seq_offset = vk_layout->draw_src_offset_B;

   cmd_offset += layout->draw.cmd_size;

   assert(cmd_offset <= layout_obj->cmd_size);

   return cmd_offset;
}

void
anv_dgc_print_gfx_state(FILE *f,
                        const struct anv_dgc_gfx_layout *layout,
                        const struct anv_indirect_command_layout *layout_obj)
{
   fprintf(f, "Generated Gfx state:\n");
#define PRINT(state_bits, cond2, ...) do {            \
      if ((state_bits) == 0 ||                        \
          (layout_obj->vk.dgc_info & (state_bits)) || \
          (cond2))                                    \
         fprintf(f, __VA_ARGS__);                \
   } while (0)
   PRINT(BITFIELD_BIT(MESA_VK_DGC_IB), false,
         "  ib:      cmd_offset=0x%04x-0x%04x (%u)\n",
         layout->index_buffer.cmd_offset,
         layout->index_buffer.cmd_offset +
         layout->index_buffer.cmd_size,
         layout->index_buffer.cmd_size);
   PRINT(BITFIELD_BIT(MESA_VK_DGC_VB), false,
         "  vb:      cmd_offset=0x%04x-0x%04x (%u)\n",
         layout->vertex_buffers.cmd_offset,
         layout->vertex_buffers.cmd_offset +
         layout->vertex_buffers.cmd_size,
         layout->vertex_buffers.cmd_size);
   PRINT(0, false,
         "  final:   cmd_offset=0x%04x-0x%04x (%u)\n",
         layout->indirect_set.final_cmds_offset,
         layout->indirect_set.final_cmds_offset +
         layout->indirect_set.final_cmds_size,
         layout->indirect_set.final_cmds_size);
   PRINT(BITFIELD_BIT(MESA_VK_DGC_PC) |
         BITFIELD_BIT(MESA_VK_DGC_SI),
         layout->push_constants.cmd_size != 0,
         "  push:    cmd_offset=0x%04x-0x%04x (%u)\n",
         layout->push_constants.cmd_offset,
         layout->push_constants.cmd_offset +
         layout->push_constants.cmd_size,
         layout->push_constants.cmd_size);
   PRINT(0, false,
         "  draw:    cmd_offset=0x%04x-0x%04x (%u)\n",
         layout->draw.cmd_offset,
         layout->draw.cmd_offset +
         layout->draw.cmd_size,
         layout->draw.cmd_size);
#undef PRINT
}

void
anv_dgc_print_layout(FILE *f,
                     const struct anv_indirect_command_layout *layout)
{
   fprintf(f, "Generated %s layout:\n",
           layout->bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS ? "Gfx" :
           layout->bind_point == VK_PIPELINE_BIND_POINT_COMPUTE  ? "CS" :
           layout->bind_point == VK_PIPELINE_BIND_POINT_COMPUTE  ? "RT" : "unknown");
#define DGC_BIT(name) ((layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_##name)) ? #name"," : "")
   fprintf(f, "  bits: %s%s%s%s%s%s%s%s%s%s\n",
           DGC_BIT(IES),
           DGC_BIT(PC),
           DGC_BIT(IB),
           DGC_BIT(VB),
           DGC_BIT(SI),
           DGC_BIT(DRAW),
           DGC_BIT(DRAW_INDEXED),
           DGC_BIT(DRAW_MESH),
           DGC_BIT(DISPATCH),
           DGC_BIT(RT));
#undef DGC_BIT
   fprintf(f, "  seq_stride:    %zu\n", layout->vk.stride);
   fprintf(f, "  cmd_prolog:    %u\n", layout->cmd_prolog_size);
   fprintf(f, "  cmd_stride:    %u\n", layout->cmd_size);
   fprintf(f, "  cmd_epilog:    %u\n", layout->cmd_epilog_size);
   fprintf(f, "  data_prolog:   %u\n", layout->data_prolog_size);
   fprintf(f, "  data_stride:   %u\n", layout->data_size);

   fprintf(f, "  sequences:\n");
   const struct vk_indirect_command_layout *vk_layout = &layout->vk;
   const struct anv_dgc_push_layout *pc_layout =
      layout->bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS ?
      &layout->gfx_layout.push_constants : &layout->cs_layout.push_constants;
   if (pc_layout->num_entries > 0 || pc_layout->seq_id_active) {
      fprintf(f, "    push_constants:\n");
      for (uint32_t i = 0; i < pc_layout->num_entries; i++) {
         fprintf(f,
                 "      pc_entry%02u seq_offset: 0x%04x (offset=%hu, size=%hu)\n",
                 i,
                 pc_layout->entries[i].seq_offset,
                 pc_layout->entries[i].push_offset,
                 pc_layout->entries[i].size);
      }
      if (pc_layout->seq_id_active) {
         fprintf(f, "      seq_id_offset: 0x%04hx\n",
                 pc_layout->seq_id_offset);
      }
   }
   switch (layout->bind_point) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS: {
      if (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_IB)) {
         fprintf(f, "    index_buffer:\n");
         fprintf(f, "      seq_offset: 0x%04x\n", vk_layout->index_src_offset_B);
      }
      if (vk_layout->n_vb_layouts) {
         fprintf(f, "    vertex_buffers:\n");
         for (uint32_t i = 0; i < vk_layout->n_vb_layouts; i++) {
            fprintf(f, "      seq_offset: 0x%04x (vb%u)\n",
                    vk_layout->vb_layouts[i].src_offset_B,
                    vk_layout->vb_layouts[i].binding);
         }
      }
      fprintf(f, "    %s:\n",
              (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_DRAW_MESH)) ? "mesh" :
              (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_DRAW_INDEXED)) ? "draw-indexed" :
              (vk_layout->dgc_info & BITFIELD_BIT(MESA_VK_DGC_DRAW)) ? "draw" :
              "unknown");
      fprintf(f, "      seq_offset: 0x%04x\n", vk_layout->draw_src_offset_B);
      break;
   }
   case VK_PIPELINE_BIND_POINT_COMPUTE:
   case VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR: {
      const struct anv_dgc_cs_layout *cs_layout = &layout->cs_layout;
      if (cs_layout->indirect_set.active) {
         fprintf(f, "    ies:\n");
         fprintf(f, "      seq_offset: 0x%04x\n", cs_layout->indirect_set.seq_offset);
      }
      fprintf(f, "    dispatch:\n");
      fprintf(f, "      seq_offset: 0x%04x\n", cs_layout->dispatch.seq_offset);
      break;
   }
   default:
      UNREACHABLE("Invalid bind point");
   }

   fprintf(f, "  commands:\n");
   for (uint32_t i = 0; i < layout->n_items; i++) {
      fprintf(f, "    %s: %u\n",
              layout->items[i].name, layout->items[i].size);
   }
}
