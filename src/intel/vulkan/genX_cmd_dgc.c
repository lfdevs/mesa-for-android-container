/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdbool.h>

#include "anv_internal_kernels.h"
#include "anv_private.h"
#include "anv_measure.h"
#include "vk_render_pass.h"
#include "vk_util.h"

#include "common/intel_aux_map.h"
#include "genxml/gen_macros.h"
#include "genxml/genX_pack.h"
#include "genxml/genX_rt_pack.h"
#include "common/intel_genX_state_brw.h"

#include "ds/intel_tracepoints.h"

#include "genX_mi_builder.h"

static struct anv_state
emit_push_constants(struct anv_cmd_buffer *cmd_buffer,
                    const struct anv_cmd_pipeline_state *pipe_state)
{
   const uint8_t *data = (const uint8_t *) &pipe_state->push_constants;

   struct anv_state state =
      anv_cmd_buffer_alloc_temporary_state(cmd_buffer,
                                           sizeof(struct anv_push_constants),
                                           32 /* bottom 5 bits MBZ */);
   if (state.alloc_size == 0)
      return state;

   memcpy(state.map, data, pipe_state->push_constants_client_size);
   memcpy(state.map + MAX_PUSH_CONSTANTS_SIZE,
          data + MAX_PUSH_CONSTANTS_SIZE,
          sizeof(struct anv_push_constants) - MAX_PUSH_CONSTANTS_SIZE);

   return state;
}

static struct anv_dgc_gfx_params *
preprocess_gfx_sequences(struct anv_cmd_buffer *cmd_buffer,
                         struct anv_cmd_buffer *cmd_buffer_state,
                         struct anv_indirect_command_layout *layout,
                         const VkGeneratedCommandsInfoEXT *info,
                         enum anv_internal_kernel_name kernel_name)
{
   trace_intel_begin_generate_cmds_pre(&cmd_buffer->trace);

   struct anv_device *device = cmd_buffer->device;
   struct anv_cmd_graphics_state *gfx = &cmd_buffer_state->state.gfx;

   /* Allocate push constants with the cmd_buffer_state data. */
   struct anv_state push_constants_state =
      emit_push_constants(cmd_buffer, &cmd_buffer_state->state.gfx.base);
   if (push_constants_state.alloc_size == 0)
      return NULL;

   /**/
   struct anv_dgc_gfx_state gfx_state = {};
   struct anv_state gfx_state_state =
      anv_cmd_buffer_alloc_temporary_state(cmd_buffer, sizeof(gfx_state), 8);
   if (gfx_state_state.map == NULL)
      return NULL;

   uint32_t cmd_stride = anv_dgc_fill_gfx_layout(
      &gfx_state.layout, device, layout, gfx->shaders);
   anv_dgc_fill_gfx_state(
      &gfx_state, cmd_buffer_state, layout, gfx->shaders);
   anv_write_gfx_indirect_descriptor(device, &gfx_state.descriptor,
                                     &cmd_buffer_state->state.gfx);

   memcpy(gfx_state_state.map, &gfx_state, sizeof(gfx_state));

   /**/
   struct anv_shader_internal *generate_kernel;
   VkResult ret =
      anv_device_get_internal_shader(device, kernel_name, &generate_kernel);
   if (ret != VK_SUCCESS) {
      anv_batch_set_error(&cmd_buffer->batch, ret);
      return NULL;
   }

   struct anv_simple_shader simple_state = {
      .device               = device,
      .cmd_buffer           = cmd_buffer,
      .dynamic_state_stream = &cmd_buffer->dynamic_state_stream,
      .general_state_stream = &cmd_buffer->general_state_stream,
      .batch                = &cmd_buffer->batch,
      .kernel               = generate_kernel,
   };
   genX(emit_simple_shader_init)(&simple_state);

   struct anv_dgc_gfx_params *params;
   struct anv_state push_data_state =
      genX(simple_shader_alloc_push)(&simple_state, sizeof(*params));
   if (push_data_state.map == NULL)
      return NULL;
   params = push_data_state.map;

   const bool wa_16011107343 =
      INTEL_WA_16011107343_GFX_VER &&
      intel_needs_workaround(device->info, 16011107343) &&
      gfx->shaders[MESA_SHADER_TESS_CTRL] != NULL;
   const bool wa_22018402687 =
      INTEL_WA_22018402687_GFX_VER &&
      intel_needs_workaround(device->info, 22018402687) &&
      gfx->shaders[MESA_SHADER_TESS_EVAL] != NULL;

   *params = (struct anv_dgc_gfx_params) {
      .cmd_addr   = info->preprocessAddress,
      .cmd_stride = cmd_stride,

      .data_addr   = info->preprocessAddress +
                     align(layout->cmd_prolog_size +
                           info->maxSequenceCount * layout->cmd_size +
                           layout->cmd_epilog_size, 64),
      .data_stride = layout->data_size,

      .seq_addr   = info->indirectAddress,
      .seq_stride = layout->vk.stride,

      .seq_count_addr = info->sequenceCountAddress,
      .max_seq_count  = info->maxSequenceCount,

      .cmd_prolog_size = layout->cmd_prolog_size,
      .data_prolog_size = layout->data_prolog_size,

      .state_addr = anv_address_physical(
         anv_cmd_buffer_temporary_state_address(cmd_buffer, gfx_state_state)),

      .const_addr = anv_address_physical(
         anv_cmd_buffer_temporary_state_address(
            cmd_buffer, push_constants_state)),
      .const_size =
         cmd_buffer_state->state.gfx.base.push_constants_client_size,

      .driver_const_addr = anv_address_physical(
         anv_address_add(
            anv_cmd_buffer_temporary_state_address(
               cmd_buffer, push_constants_state),
            MAX_PUSH_CONSTANTS_SIZE)),

      .flags = (cmd_buffer_state->state.conditional_render_enabled ?
                ANV_GENERATED_FLAG_PREDICATED : 0) |
               (wa_16011107343 ? ANV_GENERATED_FLAG_WA_16011107343 : 0) |
               (wa_22018402687 ? ANV_GENERATED_FLAG_WA_22018402687 : 0) |
               (intel_needs_workaround(device->info, 16014912113) ?
                ANV_GENERATED_FLAG_WA_16014912113 : 0) |
               (intel_needs_workaround(device->info, 18022330953) ||
                intel_needs_workaround(device->info, 22011440098) ?
                ANV_GENERATED_FLAG_WA_18022330953 : 0),
   };

   genX(emit_simple_shader_dispatch)(&simple_state, info->maxSequenceCount,
                                     push_data_state);

   trace_intel_end_generate_cmds_pre(&cmd_buffer->trace);

   return params;
}

#define merge_state(out, in)                            \
   do {                                                 \
      assert(ARRAY_SIZE(out) >= ARRAY_SIZE(in));        \
      for (uint32_t i = 0; i < ARRAY_SIZE(in); i++)     \
         out[i] |= in[i];                               \
   } while (0)

static uint32_t
get_cs_shader_push_offset(const struct anv_shader *shader,
                          const struct anv_indirect_command_layout *layout)
{
   /* With a device bound pipeline, we can't know this. */
   if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_IES))
      return 0;

   const struct anv_pipeline_bind_map *bind_map = &shader->bind_map;
   const struct anv_push_range *push_range = &bind_map->push_ranges[0];

   return push_range->set == ANV_DESCRIPTOR_SET_PUSH_CONSTANTS ?
          (push_range->start * 32) : 0;
}

#if GFX_VERx10 >= 125
static void
write_driver_values(struct GENX(COMPUTE_WALKER) *walker,
                    struct anv_cmd_buffer *cmd_buffer)
{
   walker->PredicateEnable = cmd_buffer->state.conditional_render_enabled;
   walker->body.InterfaceDescriptor.SamplerStatePointer =
      cmd_buffer->state.samplers[MESA_SHADER_COMPUTE].offset;
   walker->body.InterfaceDescriptor.BindingTablePointer =
      cmd_buffer->state.binding_tables[MESA_SHADER_COMPUTE].offset;
}
#else
static void
write_driver_values(struct GENX(GPGPU_WALKER) *walker,
                    struct GENX(INTERFACE_DESCRIPTOR_DATA) *idd,
                    struct anv_cmd_buffer *cmd_buffer)
{
   walker->PredicateEnable = cmd_buffer->state.conditional_render_enabled;
   idd->BindingTablePointer =
      cmd_buffer->state.binding_tables[MESA_SHADER_COMPUTE].offset;
   idd->SamplerStatePointer =
      cmd_buffer->state.samplers[MESA_SHADER_COMPUTE].offset;
}
#endif /* GFX_VERx10 >= 125 */

static struct anv_dgc_cs_params *
preprocess_cs_sequences(struct anv_cmd_buffer *cmd_buffer,
                        struct anv_cmd_buffer *cmd_buffer_state,
                        struct anv_indirect_command_layout *layout,
                        struct anv_indirect_execution_set *indirect_set,
                        const VkGeneratedCommandsInfoEXT *info,
                        enum anv_internal_kernel_name kernel_name,
                        bool emit_driver_values)
{
   trace_intel_begin_generate_cmds_pre(&cmd_buffer->trace);

   struct anv_device *device = cmd_buffer->device;
   struct anv_cmd_compute_state *comp_state = &cmd_buffer_state->state.compute;
   struct anv_cmd_pipeline_state *pipe_state = &comp_state->base;

   struct anv_state push_constants_state =
      emit_push_constants(cmd_buffer, pipe_state);
   if (push_constants_state.alloc_size == 0)
      return NULL;

   struct anv_state layout_state =
      anv_cmd_buffer_alloc_temporary_state(
         cmd_buffer, sizeof(layout->cs_layout), 8);
   if (layout_state.map == NULL)
      return NULL;
   memcpy(layout_state.map, &layout->cs_layout, sizeof(layout->cs_layout));

   /**/
   struct anv_dgc_cs_descriptor cs_desc = {};

   if ((layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_IES)) == 0)
      genX(write_cs_descriptor)(&cs_desc, device, comp_state->shader);

#if GFX_VERx10 >= 125
   struct GENX(COMPUTE_WALKER) walker = {
      GENX(COMPUTE_WALKER_header),
      .body = {
         .PostSync.MOCS = anv_mocs(device, NULL, 0),
      },
   };
   if (emit_driver_values)
      write_driver_values(&walker, cmd_buffer);

   uint32_t cs_walker_dws[GENX(COMPUTE_WALKER_length)];
   GENX(COMPUTE_WALKER_pack)(NULL, cs_walker_dws, &walker);
   merge_state(cs_desc.gfx125.compute_walker, cs_walker_dws);
#else
   struct GENX(GPGPU_WALKER) walker = {
      GENX(GPGPU_WALKER_header),
   };
   struct GENX(INTERFACE_DESCRIPTOR_DATA) idd = {};
   if (emit_driver_values)
      write_driver_values(&walker, &idd, cmd_buffer);

   GENX(GPGPU_WALKER_pack)(NULL, cs_desc.gfx9.gpgpu_walker, &walker);

   uint32_t idd_dws[GENX(INTERFACE_DESCRIPTOR_DATA_length)];
   GENX(INTERFACE_DESCRIPTOR_DATA_pack)(NULL, idd_dws, &idd);
   merge_state(cs_desc.gfx9.interface_descriptor_data, idd_dws);
#endif

   struct anv_state cs_desc_state =
      anv_cmd_buffer_alloc_temporary_state(cmd_buffer, sizeof(cs_desc),
                                           GFX_VERx10 >= 125 ? 8 : 64);
   if (cs_desc_state.map == NULL)
      return NULL;
   memcpy(cs_desc_state.map, &cs_desc, sizeof(cs_desc));

   /**/
   struct anv_shader_internal *generate_kernel;
   VkResult ret =
      anv_device_get_internal_shader(device, kernel_name, &generate_kernel);
   if (ret != VK_SUCCESS) {
      anv_batch_set_error(&cmd_buffer->batch, ret);
      return NULL;
   }

   struct anv_simple_shader state = {
      .device               = device,
      .cmd_buffer           = cmd_buffer,
      .dynamic_state_stream = &cmd_buffer->dynamic_state_stream,
      .general_state_stream = &cmd_buffer->general_state_stream,
      .batch                = &cmd_buffer->batch,
      .kernel               = generate_kernel,
   };
   genX(emit_simple_shader_init)(&state);

   struct anv_state push_data_state =
      genX(simple_shader_alloc_push)(&state, sizeof(struct anv_dgc_cs_params));
   if (push_data_state.map == NULL)
      return NULL;

   struct anv_dgc_cs_params *params = push_data_state.map;
   *params = (struct anv_dgc_cs_params) {
      .cmd_addr   = info->preprocessAddress,
      .cmd_stride = layout->cmd_size,

      .data_addr   = info->preprocessAddress +
                     align(layout->cmd_prolog_size +
                           info->maxSequenceCount * layout->cmd_size +
                           layout->cmd_epilog_size, 64),
      .data_stride = layout->data_size,

      .seq_addr   = info->indirectAddress,
      .seq_stride = layout->vk.stride,

      .seq_count_addr = info->sequenceCountAddress,
      .max_seq_count = info->maxSequenceCount,

      .cmd_prolog_size = layout->cmd_prolog_size,
      .data_prolog_size = layout->data_prolog_size,

      .layout_addr = anv_address_physical(
         anv_cmd_buffer_temporary_state_address(cmd_buffer, layout_state)),

      .indirect_set_addr = indirect_set ?
                           anv_address_physical((struct anv_address) {
                                 .bo = indirect_set->bo }) :
                           anv_address_physical(
                              anv_cmd_buffer_temporary_state_address(
                                 cmd_buffer, cs_desc_state)),

      .interface_descriptor_data_addr = anv_address_physical(
         anv_address_add(
            anv_cmd_buffer_temporary_state_address(cmd_buffer, cs_desc_state),
            offsetof(struct anv_dgc_cs_descriptor,
                     gfx9.interface_descriptor_data))),

      .const_addr = anv_address_physical(
         anv_cmd_buffer_temporary_state_address(cmd_buffer,
                                                push_constants_state)),
      .const_size = pipe_state->push_constants_client_size,

      .driver_const_addr = anv_address_physical(
         anv_address_add(
            anv_cmd_buffer_temporary_state_address(cmd_buffer,
                                                   push_constants_state),
            MAX_PUSH_CONSTANTS_SIZE)),

      .flags = cmd_buffer_state->state.conditional_render_enabled ?
               ANV_GENERATED_FLAG_PREDICATED : 0,
   };

   genX(emit_simple_shader_dispatch)(&state,
                                     info->maxSequenceCount,
                                     push_data_state);

   trace_intel_end_generate_cmds_pre(&cmd_buffer->trace);

   return params;
}

static struct anv_dgc_cs_params *
postprocess_cs_sequences(struct anv_cmd_buffer *cmd_buffer,
                         struct anv_indirect_command_layout *layout,
                         struct anv_indirect_execution_set *indirect_set,
                         const VkGeneratedCommandsInfoEXT *info)
{
   trace_intel_begin_generate_cmds_post(&cmd_buffer->trace);

   struct anv_device *device = cmd_buffer->device;

   /**/
   struct anv_dgc_cs_descriptor *cs_state;
   struct anv_state cs_state_state =
      anv_cmd_buffer_alloc_temporary_state(cmd_buffer, sizeof(*cs_state), 8);
   if (cs_state_state.map == NULL)
      return NULL;
   cs_state = cs_state_state.map;

#if GFX_VERx10 >= 125
   struct GENX(COMPUTE_WALKER) walker = {
      .body.PostSync.MOCS = anv_mocs(device, NULL, 0),
   };
   write_driver_values(&walker, cmd_buffer);

   GENX(COMPUTE_WALKER_pack)(NULL, cs_state->gfx125.compute_walker, &walker);
#else
   struct GENX(INTERFACE_DESCRIPTOR_DATA) idd = {
      .BindingTablePointer =
         cmd_buffer->state.binding_tables[MESA_SHADER_COMPUTE].offset,
      .SamplerStatePointer =
         cmd_buffer->state.samplers[MESA_SHADER_COMPUTE].offset,
   };

   GENX(INTERFACE_DESCRIPTOR_DATA_pack)(
      NULL, cs_state->gfx9.interface_descriptor_data, &idd);
#endif

   /**/
   struct anv_shader_internal *generate_kernel;
   VkResult ret =
      anv_device_get_internal_shader(
         device,
         ANV_INTERNAL_KERNEL_DGC_CS_POSTPROCESS_COMPUTE,
         &generate_kernel);
   if (ret != VK_SUCCESS) {
      anv_batch_set_error(&cmd_buffer->batch, ret);
      return NULL;
   }

   struct anv_simple_shader state = {
      .device               = device,
      .cmd_buffer           = cmd_buffer,
      .dynamic_state_stream = &cmd_buffer->dynamic_state_stream,
      .general_state_stream = &cmd_buffer->general_state_stream,
      .batch                = &cmd_buffer->batch,
      .kernel               = generate_kernel,
   };
   genX(emit_simple_shader_init)(&state);

   struct anv_state push_data_state =
      genX(simple_shader_alloc_push)(&state, sizeof(struct anv_dgc_cs_params));
   if (push_data_state.map == NULL)
      return NULL;

   struct anv_dgc_cs_params *params = push_data_state.map;
   *params = (struct anv_dgc_cs_params) {
      .cmd_addr   = info->preprocessAddress,
      .cmd_stride = layout->cmd_size,

      .data_addr   = info->preprocessAddress +
                     align(layout->cmd_prolog_size +
                           info->maxSequenceCount * layout->cmd_size +
                           layout->cmd_epilog_size, 64),
      .data_stride = layout->data_size,

      .seq_count_addr = info->sequenceCountAddress,
      .max_seq_count = info->maxSequenceCount,

      .cmd_prolog_size = layout->cmd_prolog_size,
      .data_prolog_size = layout->data_prolog_size,

      .data_stride = layout->cs_layout.indirect_set.data_offset,

      .indirect_set_addr = anv_address_physical(
         anv_cmd_buffer_temporary_state_address(
            cmd_buffer, cs_state_state)),
   };

   genX(emit_simple_shader_dispatch)(&state,
                                     info->maxSequenceCount,
                                     push_data_state);

   trace_intel_end_generate_cmds_post(&cmd_buffer->trace);

   return params;
}

#if GFX_VERx10 >= 125
static struct anv_dgc_rt_params *
preprocess_rt_sequences(struct anv_cmd_buffer *cmd_buffer,
                        struct anv_cmd_buffer *cmd_buffer_state,
                        struct anv_indirect_command_layout *layout,
                        struct anv_indirect_execution_set *indirect_set,
                        const VkGeneratedCommandsInfoEXT *info,
                        enum anv_internal_kernel_name kernel_name)
{
   trace_intel_begin_generate_cmds_pre(&cmd_buffer->trace);

   struct anv_device *device = cmd_buffer->device;
   struct anv_cmd_ray_tracing_state *rt_state = &cmd_buffer_state->state.rt;
   struct anv_cmd_pipeline_state *pipe_state = &rt_state->base;

   struct anv_state push_constants_state =
      emit_push_constants(cmd_buffer, pipe_state);
   if (push_constants_state.alloc_size == 0)
      return NULL;

   struct anv_state layout_state =
      anv_cmd_buffer_alloc_temporary_state(
         cmd_buffer, sizeof(layout->cs_layout), 8);
   if (layout_state.map == NULL)
      return NULL;
   memcpy(layout_state.map, &layout->cs_layout, sizeof(layout->cs_layout));

   struct anv_state rtdg_state =
      anv_cmd_buffer_alloc_temporary_state(cmd_buffer,
                                           4 * GENX(RT_DISPATCH_GLOBALS_length),
                                           8);
   if (rtdg_state.alloc_size == 0)
      return NULL;

   struct GENX(RT_DISPATCH_GLOBALS) rtdg = {
      .MemBaseAddress     = (struct anv_address) {
         .bo = rt_state->scratch.bo,
         .offset = rt_state->scratch.layout.ray_stack_start,
      },
#if GFX_VERx10 >= 300
      .CallStackHandler   = anv_shader_internal_get_handler(
         device->rt_trivial_return, 0),
#else
      .CallStackHandler   = anv_shader_internal_get_bsr(
         device->rt_trivial_return, 0),
#endif
      .AsyncRTStackSize   = rt_state->scratch.layout.ray_stack_stride / 64,
      .NumDSSRTStacks     = rt_state->scratch.layout.stack_ids_per_dss,
      .MaxBVHLevels       = BRW_RT_MAX_BVH_LEVELS,
      .Flags              = RT_DEPTH_TEST_LESS_EQUAL,
      .SWStackSize        = rt_state->scratch.layout.sw_stack_size / 64,
   };
   GENX(RT_DISPATCH_GLOBALS_pack)(NULL, rtdg_state.map, &rtdg);

   struct anv_state compute_walker_state =
      anv_cmd_buffer_alloc_temporary_state(cmd_buffer,
                                           4 * GENX(COMPUTE_WALKER_length),
                                           8);

   const struct brw_cs_prog_data *cs_prog_data =
      brw_cs_prog_data_const(device->rt_trampoline->prog_data);
   struct intel_cs_dispatch_info dispatch =
      brw_cs_get_dispatch_info(device->info, cs_prog_data, NULL);
   struct GENX(COMPUTE_WALKER) cw = {
      GENX(COMPUTE_WALKER_header),
      .body = {
         .SIMDSize                       = dispatch.simd_size / 16,
         .MessageSIMD                    = dispatch.simd_size / 16,
         .ExecutionMask                  = 0xff,
         .EmitInlineParameter            = true,
#if GFX_VER >= 30
         /* HSD 14016252163 */
         .DispatchWalkOrder = cs_prog_data->uses_sampler ? MortonWalk : LinearWalk,
         .ThreadGroupBatchSize = cs_prog_data->uses_sampler ? TG_BATCH_4 : TG_BATCH_1,
#endif
         .PostSync.MOCS                  = anv_mocs(device, NULL, 0),
         .InterfaceDescriptor            = (struct GENX(INTERFACE_DESCRIPTOR_DATA)) {
            .NumberofThreadsinGPGPUThreadGroup = 1,
            .BTDMode                           = true,
#if INTEL_NEEDS_WA_14017794102 || INTEL_NEEDS_WA_14023061436
            .ThreadPreemption = false,
#endif
#if GFX_VER >= 30
            .RegistersPerThread = ptl_register_blocks(cs_prog_data->base.grf_used),
#endif
         },
      },
   };
   GENX(COMPUTE_WALKER_pack)(NULL, compute_walker_state.map, &cw);

   /**/
   struct anv_shader_internal *generate_kernel;
   VkResult ret =
      anv_device_get_internal_shader(device, kernel_name, &generate_kernel);
   if (ret != VK_SUCCESS) {
      anv_batch_set_error(&cmd_buffer->batch, ret);
      return NULL;
   }

   struct anv_simple_shader state = {
      .device               = device,
      .cmd_buffer           = cmd_buffer,
      .dynamic_state_stream = &cmd_buffer->dynamic_state_stream,
      .general_state_stream = &cmd_buffer->general_state_stream,
      .batch                = &cmd_buffer->batch,
      .kernel               = generate_kernel,
   };
   genX(emit_simple_shader_init)(&state);

   struct anv_state push_data_state =
      genX(simple_shader_alloc_push)(&state,
                                     sizeof(struct anv_dgc_rt_params));
   if (push_data_state.map == NULL)
      return NULL;

   struct anv_dgc_rt_params *params = push_data_state.map;
   *params = (struct anv_dgc_rt_params) {
      .cmd_addr   = info->preprocessAddress,
      .cmd_stride = layout->cmd_size,

      .data_addr   = info->preprocessAddress +
                     align(layout->cmd_prolog_size +
                           info->maxSequenceCount * layout->cmd_size +
                           layout->cmd_epilog_size, 64),
      .data_stride = layout->data_size,

      .seq_addr   = info->indirectAddress,
      .seq_stride = layout->vk.stride,

      .seq_count_addr = info->sequenceCountAddress,
      .max_seq_count = info->maxSequenceCount,

      .cmd_prolog_size = layout->cmd_prolog_size,
      .data_prolog_size = layout->data_prolog_size,

      .layout_addr = anv_address_physical(
         anv_cmd_buffer_temporary_state_address(cmd_buffer, layout_state)),

      .compute_walker_addr = anv_address_physical(
         anv_cmd_buffer_temporary_state_address(
            cmd_buffer, compute_walker_state)),

      .rtdg_global_addr = anv_address_physical(
         anv_cmd_buffer_temporary_state_address(cmd_buffer, rtdg_state)),

      .const_addr = anv_address_physical(
         anv_cmd_buffer_temporary_state_address(cmd_buffer,
                                                push_constants_state)),
      .const_size = pipe_state->push_constants_client_size,

      .driver_const_addr = anv_address_physical(
         anv_address_add(
            anv_cmd_buffer_temporary_state_address(cmd_buffer,
                                                   push_constants_state),
            MAX_PUSH_CONSTANTS_SIZE)),

      .flags = cmd_buffer_state->state.conditional_render_enabled ?
               ANV_GENERATED_FLAG_PREDICATED : 0,
   };

   genX(emit_simple_shader_dispatch)(&state,
                                     info->maxSequenceCount,
                                     push_data_state);

   trace_intel_end_generate_cmds_pre(&cmd_buffer->trace);

   return params;
}
#endif /* GFX_VERx10 >= 125 */

void genX(CmdPreprocessGeneratedCommandsEXT)(
    VkCommandBuffer                             commandBuffer,
    const VkGeneratedCommandsInfoEXT*           pGeneratedCommandsInfo,
    VkCommandBuffer                             stateCommandBuffer)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer_state, stateCommandBuffer);
   ANV_FROM_HANDLE(anv_indirect_command_layout, layout,
                   pGeneratedCommandsInfo->indirectCommandsLayout);
   ANV_FROM_HANDLE(anv_indirect_execution_set, indirect_set,
                   pGeneratedCommandsInfo->indirectExecutionSet);

   /* Flush any pending barrier to make the indirect data available */
   genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);

   /* If the preprocessing command buffer doesn't have a pipeline mode, select
    * one.
    */
   if (cmd_buffer->state.current_pipeline == UINT32_MAX) {
      if (anv_cmd_buffer_is_compute_queue(cmd_buffer))
         genX(flush_pipeline_select_gpgpu)(cmd_buffer, false);
      else
         genX(flush_pipeline_select_3d)(cmd_buffer);
   }

   /* Add the indirect set to the relocation list. */
   if (indirect_set) {
      anv_reloc_list_add_bo(cmd_buffer->batch.relocs, indirect_set->bo);
      anv_reloc_list_append(cmd_buffer->batch.relocs, &indirect_set->relocs);
   }

   switch (layout->bind_point) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS:
      genX(cmd_buffer_flush_gfx_state)(cmd_buffer_state);

      preprocess_gfx_sequences(cmd_buffer, cmd_buffer_state, layout,
                               pGeneratedCommandsInfo,
                               anv_internal_kernel_variant(cmd_buffer, DGC_GFX));
      break;

   case VK_PIPELINE_BIND_POINT_COMPUTE:
      genX(cmd_buffer_flush_compute_state)(cmd_buffer_state, indirect_set);
#if GFX_VERx < 125
      if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_IES)) {
         assert(indirect_set != NULL);
         genX(cmd_buffer_flush_indirect_cs_descriptor_sets)(cmd_buffer_state,
                                                            indirect_set->bind_map);
      }
#endif

      preprocess_cs_sequences(cmd_buffer, cmd_buffer_state,
                              layout, indirect_set,
                              pGeneratedCommandsInfo,
                              anv_internal_kernel_variant(cmd_buffer, DGC_CS),
                              false /* emit_driver_values */);
      break;

#if GFX_VERx10 >= 125
   case VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR:
      genX(cmd_buffer_flush_rt_state)(cmd_buffer_state, cmd_buffer->state.rt.scratch_size);

      preprocess_rt_sequences(cmd_buffer, cmd_buffer_state,
                              layout, indirect_set,
                              pGeneratedCommandsInfo,
                              anv_internal_kernel_variant(cmd_buffer, DGC_RT));
      break;
#endif

   default:
      UNREACHABLE("Invalid layout bind point");
      break;
   }
}

void genX(CmdExecuteGeneratedCommandsEXT)(
   VkCommandBuffer                             commandBuffer,
   VkBool32                                    isPreprocessed,
   const VkGeneratedCommandsInfoEXT*           pGeneratedCommandsInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_indirect_command_layout, layout,
                   pGeneratedCommandsInfo->indirectCommandsLayout);
   ANV_FROM_HANDLE(anv_indirect_execution_set, indirect_set,
                   pGeneratedCommandsInfo->indirectExecutionSet);
   struct anv_device *device = cmd_buffer->device;
   const struct intel_device_info *devinfo = device->info;

   /* Flushing pending synchronization operations, we might write the return
    * address with CS below and we don't want that write to be clobbered by a
    * previous write from the preprocessing shader.
    */
    genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);

   /* Add the indirect set to the relocation list. */
   if (indirect_set) {
      anv_reloc_list_add_bo(cmd_buffer->batch.relocs, indirect_set->bo);
      anv_reloc_list_append(cmd_buffer->batch.relocs, &indirect_set->relocs);
   }

   struct mi_builder b;
   mi_builder_init(&b, devinfo, &cmd_buffer->batch);
   struct mi_goto_target t = MI_GOTO_TARGET_INIT;

   switch (layout->bind_point) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS: {
      struct anv_dgc_gfx_params *params = NULL;
      uint64_t *return_addr_loc = NULL;

      genX(flush_pipeline_select_3d)(cmd_buffer);

      /* If we're executing a preprocessing sequence here, we can have it
       * write the return address.
       */
      if (!isPreprocessed) {
         genX(cmd_buffer_flush_gfx_state)(cmd_buffer);

         params = preprocess_gfx_sequences(
            cmd_buffer, cmd_buffer, layout, pGeneratedCommandsInfo,
            ANV_INTERNAL_KERNEL_DGC_GFX_FRAGMENT);
      } else {
         return_addr_loc =
            anv_batch_emitn(&cmd_buffer->batch,
                            GENX(MI_STORE_DATA_IMM_length) + 1 /* QWord write */,
                            GENX(MI_STORE_DATA_IMM),
#if GFX_VER >= 12
                            .ForceWriteCompletionCheck = true,
#endif
                            .Address = anv_address_add(
                               anv_address_from_u64(pGeneratedCommandsInfo->preprocessAddress),
                               GENX(MI_STORE_DATA_IMM_ImmediateData_start) / 8)) +
            GENX(MI_STORE_DATA_IMM_ImmediateData_start) / 8;
      }

      if (ANV_DEBUG(DGC_DUMP)) {
         anv_cmd_buffer_dump_commands(cmd_buffer,
                                      pGeneratedCommandsInfo->preprocessAddress,
                                      pGeneratedCommandsInfo->maxSequenceCount *
                                      layout->cmd_size / 4);
      }

      genX(cmd_buffer_flush_gfx)(cmd_buffer);

      if (pGeneratedCommandsInfo->sequenceCountAddress != 0) {
         struct anv_address seq_count_addr =
            anv_address_from_u64(pGeneratedCommandsInfo->sequenceCountAddress);

         const uint32_t mocs = anv_mocs_for_address(device, &seq_count_addr);
         mi_builder_set_mocs(&b, mocs);

         mi_goto_if(&b, mi_ieq(&b, mi_mem32(seq_count_addr), mi_imm(0)), &t);
      }

      if (cmd_buffer->state.conditional_render_enabled)
         genX(cmd_emit_conditional_render_predicate)(cmd_buffer);

#if GFX_VER == 9
      /* Gfx9 has a VF cache issues (only considers the bottom 32bit of the VF
       * buffer address), since we're likely to emit those in the DGC buffer,
       * invalidate the cache here, further invalidation is emitted in the
       * generated commands if needed.
       */
      anv_add_pending_pipe_bits(cmd_buffer,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR |
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                                ANV_PIPE_VF_CACHE_INVALIDATE_BIT,
                                "Gfx9 VF cache inval pre dgc exec");
      genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);
#endif

      /* If a shader runs, flush the data to make it visible to CS. */
      if (params) {
         anv_add_pending_pipe_bits(cmd_buffer,
                                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR,
                                   VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                                   ANV_PIPE_DATA_CACHE_FLUSH_BIT |
                                   ANV_PIPE_CS_STALL_BIT,
                                   "after generated commands");
         genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);
      }

#if GFX_VER >= 12
      /* Prior to Gfx12 we cannot disable the CS prefetch but it doesn't matter
       * as the prefetch shouldn't follow the MI_BATCH_BUFFER_START.
       */
      anv_batch_emit(&cmd_buffer->batch, GENX(MI_ARB_CHECK), arb) {
         arb.PreParserDisableMask = true;
         arb.PreParserDisable = true;
      }
#endif

      /* Jump into the process buffer */
      struct anv_address cmd_addr =
         anv_address_from_u64(pGeneratedCommandsInfo->preprocessAddress);
      anv_batch_emit(&cmd_buffer->batch, GENX(MI_BATCH_BUFFER_START), bbs) {
         bbs.AddressSpaceIndicator   = ASI_PPGTT;
         bbs.SecondLevelBatchBuffer  = Firstlevelbatch;
         bbs.BatchBufferStartAddress = cmd_addr;
      }

      /* If we used a shader to generate some commands, it can generate the
       * return MI_BATCH_BUFFER_START. Otherwise we edit the
       * MI_BATCH_BUFFER_START address field from CS.
       */
      struct anv_address return_addr = anv_batch_current_address(&cmd_buffer->batch);
      if (params) {
         params->return_addr = anv_address_physical(return_addr);
      } else {
         assert(return_addr_loc);
         *return_addr_loc = anv_address_physical(return_addr);
      }

      if (pGeneratedCommandsInfo->sequenceCountAddress != 0)
         mi_goto_target(&b, &t);

      /* Dirty the bits affected by the executed commands */
      if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_IB))
         cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_INDEX_BUFFER;
      if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_VB))
          cmd_buffer->state.gfx.vb_dirty |= ~0;
      if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_PC))
         cmd_buffer->state.push_constants_dirty |= ANV_GRAPHICS_STAGE_BITS;

      cmd_buffer->state.dgc_states |= ANV_DGC_STATE_GRAPHIC;

      break;
   }

   case VK_PIPELINE_BIND_POINT_COMPUTE: {
      struct anv_cmd_compute_state *comp_state = &cmd_buffer->state.compute;

      genX(flush_pipeline_select_gpgpu)(cmd_buffer, false);

      if (pGeneratedCommandsInfo->sequenceCountAddress != 0) {
         struct anv_address seq_count_addr =
            anv_address_from_u64(pGeneratedCommandsInfo->sequenceCountAddress);

         const uint32_t mocs = anv_mocs_for_address(device, &seq_count_addr);
         mi_builder_set_mocs(&b, mocs);

         mi_goto_if(&b, mi_ieq(&b, mi_mem32(seq_count_addr), mi_imm(0)), &t);
      }

      /* Do we need to go an edit the binding table offsets? */
      const bool need_post_process =
         (GFX_VERx10 >= 125 &&
          (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_IES)) == 0 &&
          (comp_state->shader->bind_map.surface_count > 0 ||
           comp_state->shader->bind_map.sampler_count > 0)) ||
         (GFX_VERx10 <= 120 &&
          (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_IES)) &&
          (indirect_set->bind_map->surface_count > 0 ||
           indirect_set->bind_map->sampler_count > 0));

      if (!isPreprocessed || need_post_process) {
         genX(cmd_buffer_flush_compute_state)(cmd_buffer, indirect_set);
#if GFX_VERx < 125
         if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_IES)) {
            assert(indirect_set != NULL);
            genX(cmd_buffer_flush_indirect_cs_descriptor_sets)(cmd_buffer,
                                                               indirect_set->bind_map);
         }
#endif
      }

      struct anv_dgc_cs_params *params = NULL;
      uint64_t *return_addr_loc = NULL;
      /* If we're executing a preprocessing sequence here, we can have it
       * write the return address. If not we need to flush any pending
       * synchronization from the preprocessing, otherwise the return address
       * write from the preprocessing shader might clobber the CS write below.
       */
      if (!isPreprocessed) {
         params = preprocess_cs_sequences(
            cmd_buffer, cmd_buffer, layout,
            indirect_set, pGeneratedCommandsInfo,
            ANV_INTERNAL_KERNEL_DGC_CS_COMPUTE,
            true /* emit_driver_values */);
      } else if (need_post_process) {
         /* For pipelines not compiled with the
          * VK_PIPELINE_CREATE_2_INDIRECT_BINDABLE_BIT_EXT, we might be using
          * the binding table and unfortunately the binding table offset needs
          * to go in the COMPUTE_WALKER command and we only know the value
          * when we flush it here.
          */
         params = postprocess_cs_sequences(cmd_buffer, layout, indirect_set,
                                           pGeneratedCommandsInfo);
      } else {
         return_addr_loc =
            anv_batch_emitn(&cmd_buffer->batch,
                            GENX(MI_STORE_DATA_IMM_length) + 1 /* QWord write */,
                            GENX(MI_STORE_DATA_IMM),
#if GFX_VER >= 12
                            .ForceWriteCompletionCheck = true,
#endif
                            .Address = anv_address_add(
                               anv_address_from_u64(pGeneratedCommandsInfo->preprocessAddress),
                               GENX(MI_STORE_DATA_IMM_ImmediateData_start) / 8)) +
            GENX(MI_STORE_DATA_IMM_ImmediateData_start) / 8;
      }

      /* If a shader runs, flush the data to make it visible to CS. */
      if (params) {
         anv_add_pending_pipe_bits(cmd_buffer,
                                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                                   VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                                   ANV_PIPE_DATA_CACHE_FLUSH_BIT |
                                   ANV_PIPE_CS_STALL_BIT,
                                   "after generated commands");
         genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);
      }

      if (ANV_DEBUG(DGC_DUMP)) {
         anv_cmd_buffer_dump_commands(cmd_buffer,
                                      pGeneratedCommandsInfo->preprocessAddress,
                                      pGeneratedCommandsInfo->maxSequenceCount *
                                      layout->cmd_size / 4);
      }

      genX(cmd_buffer_flush_compute_state)(cmd_buffer, indirect_set);

      if (cmd_buffer->state.conditional_render_enabled)
         genX(cmd_emit_conditional_render_predicate)(cmd_buffer);

#if GFX_VER >= 12
      /* Prior to Gfx12 we cannot disable the CS prefetch but it doesn't matter
       * as the prefetch shouldn't follow the MI_BATCH_BUFFER_START.
       */
      anv_batch_emit(&cmd_buffer->batch, GENX(MI_ARB_CHECK), arb) {
         arb.PreParserDisableMask = true;
         arb.PreParserDisable = true;
      }
#endif

      /* Jump into the process buffer */
      struct anv_address cmd_addr =
         anv_address_from_u64(pGeneratedCommandsInfo->preprocessAddress);
      anv_batch_emit(&cmd_buffer->batch, GENX(MI_BATCH_BUFFER_START), bbs) {
         bbs.AddressSpaceIndicator   = ASI_PPGTT;
         bbs.SecondLevelBatchBuffer  = Firstlevelbatch;
         bbs.BatchBufferStartAddress = cmd_addr;
      }

      /* If we used a shader to generate some commands, it can generate the
       * return MI_BATCH_BUFFER_START. Otherwise we edit the
       * MI_BATCH_BUFFER_START address field from CS.
       */
      struct anv_address return_addr = anv_batch_current_address(&cmd_buffer->batch);
      if (params) {
         params->return_addr = anv_address_physical(return_addr);
      } else {
         assert(return_addr_loc);
         *return_addr_loc = anv_address_physical(return_addr);
      }

      if (pGeneratedCommandsInfo->sequenceCountAddress != 0)
         mi_goto_target(&b, &t);

      /* Dirty the bits affected by the executed commands */
      if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_IES))
         cmd_buffer->state.compute.pipeline_dirty = true;
      if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_PC))
         cmd_buffer->state.push_constants_dirty |= VK_SHADER_STAGE_COMPUTE_BIT;

      cmd_buffer->state.dgc_states |= ANV_DGC_STATE_COMPUTE;

      break;
   }

#if GFX_VERx10 >= 125
   case VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR: {
      struct anv_cmd_ray_tracing_state *rt_state = &cmd_buffer->state.rt;
      struct anv_cmd_pipeline_state *pipe_state = &rt_state->base;

      genX(flush_pipeline_select_gpgpu)(cmd_buffer, false);

      genX(flush_descriptor_buffers)(cmd_buffer, pipe_state, ANV_RT_STAGE_BITS);

      genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);

      genX(cmd_buffer_flush_push_descriptors)(cmd_buffer,
                                              &cmd_buffer->state.rt.base);

      if (pGeneratedCommandsInfo->sequenceCountAddress != 0) {
         struct anv_address seq_count_addr =
            anv_address_from_u64(pGeneratedCommandsInfo->sequenceCountAddress);

         const uint32_t mocs = anv_mocs_for_address(device, &seq_count_addr);
         mi_builder_set_mocs(&b, mocs);

         mi_goto_if(&b, mi_ieq(&b, mi_mem32(seq_count_addr), mi_imm(0)), &t);
      }

      const uint32_t scratch_size =
         (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_IES)) ?
         indirect_set->max_scratch : rt_state->scratch_size;
      genX(cmd_buffer_flush_rt_state)(cmd_buffer, scratch_size);

      struct anv_dgc_rt_params *params = NULL;
      uint64_t *return_addr_loc = NULL;
      /* If we're executing a preprocessing sequence here, we can have it
       * write the return address. If not we need to flush any pending
       * synchronization from the preprocessing, otherwise the return address
       * write from the preprocessing shader might clobber the CS write below.
       */
      if (!isPreprocessed) {
         params = preprocess_rt_sequences(
            cmd_buffer, cmd_buffer, layout,
            indirect_set, pGeneratedCommandsInfo,
            ANV_INTERNAL_KERNEL_DGC_RT_COMPUTE);
      } else {
         genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);
         return_addr_loc =
            anv_batch_emitn(&cmd_buffer->batch,
                            GENX(MI_STORE_DATA_IMM_length) + 1 /* QWord write */,
                            GENX(MI_STORE_DATA_IMM),
                            .ForceWriteCompletionCheck = true,
                            .Address = anv_address_add(
                               anv_address_from_u64(pGeneratedCommandsInfo->preprocessAddress),
                               GENX(MI_STORE_DATA_IMM_ImmediateData_start) / 8)) +
            GENX(MI_STORE_DATA_IMM_ImmediateData_start) / 8;
      }

      /* If a shader runs, flush the data to make it visible to CS. */
      if (params) {
         anv_add_pending_pipe_bits(cmd_buffer,
                                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
                                   VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                                   ANV_PIPE_DATA_CACHE_FLUSH_BIT |
                                   ANV_PIPE_CS_STALL_BIT,
                                   "after generated commands");
         genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);
      }

      if (ANV_DEBUG(DGC_DUMP)) {
         anv_cmd_buffer_dump_commands(cmd_buffer,
                                      pGeneratedCommandsInfo->preprocessAddress,
                                      pGeneratedCommandsInfo->maxSequenceCount *
                                      layout->cmd_size / 4);
      }

      if (cmd_buffer->state.conditional_render_enabled)
         genX(cmd_emit_conditional_render_predicate)(cmd_buffer);

      anv_batch_emit(&cmd_buffer->batch, GENX(MI_ARB_CHECK), arb) {
         arb.PreParserDisableMask = true;
         arb.PreParserDisable = true;
      }

      /* Jump into the process buffer */
      struct anv_address cmd_addr =
         anv_address_from_u64(pGeneratedCommandsInfo->preprocessAddress);
      anv_batch_emit(&cmd_buffer->batch, GENX(MI_BATCH_BUFFER_START), bbs) {
         bbs.AddressSpaceIndicator   = ASI_PPGTT;
         bbs.SecondLevelBatchBuffer  = Firstlevelbatch;
         bbs.BatchBufferStartAddress = cmd_addr;
      }

      /* If we used a shader to generate some commands, it can generate the
       * return MI_BATCH_BUFFER_START. Otherwise we edit the
       * MI_BATCH_BUFFER_START address field from CS.
       */
      struct anv_address return_addr = anv_batch_current_address(&cmd_buffer->batch);
      if (params) {
         params->return_addr = anv_address_physical(return_addr);
      } else {
         assert(return_addr_loc);
         *return_addr_loc = anv_address_physical(return_addr);
      }

      if (pGeneratedCommandsInfo->sequenceCountAddress != 0)
         mi_goto_target(&b, &t);

      cmd_buffer->state.compute.trace_rays_active = true;

      break;
   }
#endif

   default:
      UNREACHABLE("Invalid layout binding point");
   }
}
