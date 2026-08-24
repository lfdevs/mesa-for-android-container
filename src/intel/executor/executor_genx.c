/*
 * Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "executor.h"

#include "common/intel_common.h"
#include "common/intel_compute_slm.h"
#include "util/u_math.h"

#ifdef HAVE_VALGRIND
#include <valgrind.h>
#include <memcheck.h>
#define VG(x) x
#else
#define VG(x) ((void)0)
#endif

#define __gen_address_type executor_address
#define __gen_combine_address executor_combine_address
#define __gen_user_data void

#include "intel/genxml/gen_macros.h"
#include "intel/genxml/genX_pack.h"

#define __executor_cmd_length(cmd) cmd ## _length
#define __executor_cmd_header(cmd) cmd ## _header
#define __executor_cmd_pack(cmd) cmd ## _pack

#define executor_batch_emit(cmd, name)                                               \
   for (struct cmd name = { __executor_cmd_header(cmd) },                            \
        *_dst = executor_alloc_bytes(&ec->bo.batch, __executor_cmd_length(cmd) * 4); \
        __builtin_expect(_dst != NULL, 1);                                           \
        ({ __executor_cmd_pack(cmd)(0, _dst, &name);                                 \
           VG(VALGRIND_CHECK_MEM_IS_DEFINED(_dst, __executor_cmd_length(cmd) * 4));  \
           _dst = NULL;                                                              \
         }))

static void
emit_pipe_control(executor_context *ec)
{
   executor_batch_emit(GENX(PIPE_CONTROL), pc) {
#if GFX_VER >= 12
      pc.HDCPipelineFlushEnable     = true;
#endif
      pc.PipeControlFlushEnable     = true;
      pc.CommandStreamerStallEnable = true;
   }
}

static void
emit_state_invalidate(executor_context *ec)
{
   executor_batch_emit(GENX(PIPE_CONTROL), pc) {
      pc.CommandStreamerStallEnable       = true;
      pc.TextureCacheInvalidationEnable   = true;
#if GFX_VERx10 == 120
      /* Wa_14010840176: HDC flush plus state invalidation replaces constant
       * cache invalidation when invalidating constants from L1.
       */
      pc.HDCPipelineFlushEnable           = true;
#else
      pc.ConstantCacheInvalidationEnable  = true;
#endif
      pc.StateCacheInvalidationEnable     = true;
      pc.InstructionCacheInvalidateEnable = true;
   }
}

void
genX(emit_perf_stall)(executor_context *ec)
{
   executor_batch_emit(GENX(PIPE_CONTROL), pc) {
#if GFX_VER >= 12
      pc.HDCPipelineFlushEnable     = true;
#endif
      pc.PipeControlFlushEnable     = true;
      pc.CommandStreamerStallEnable = true;
      pc.StallAtPixelScoreboard     = true;
   }
}

void
genX(emit_mi_report_perf_count)(executor_context *ec, executor_bo *bo,
                                 uint32_t offset_in_bytes,
                                 uint32_t report_id)
{
   executor_batch_emit(GENX(MI_REPORT_PERF_COUNT), mi_rpc) {
      mi_rpc.MemoryAddress = (executor_address){bo->addr + offset_in_bytes};
      mi_rpc.ReportID = report_id;
   }
}

void
genX(store_register_mem)(executor_context *ec, executor_bo *bo,
                         uint32_t reg, uint32_t reg_size,
                         uint32_t offset_in_bytes)
{
   assert(reg_size == 4 || reg_size == 8);

   for (uint32_t i = 0; i < reg_size; i += 4) {
      executor_batch_emit(GENX(MI_STORE_REGISTER_MEM), srm) {
         srm.RegisterAddress = reg + i;
         srm.MemoryAddress = (executor_address){bo->addr + offset_in_bytes + i};
      }
   }
}

static void
emit_state_base_address(executor_context *ec, uint32_t mocs)
{
   /* Use the full address for stateless data and instructions.  Surface,
    * binding-table, and sampler pointers are smaller offsets, so point their
    * bases at the extra BO that owns the generated state.
    */
   const executor_address base_address = {0};
   const executor_address state_base_address = {ec->bo.extra.addr};
   const uint32_t size                 = (1 << 20) - 1;

   executor_batch_emit(GENX(STATE_BASE_ADDRESS), sba) {
      sba.GeneralStateBaseAddress               = base_address;
      sba.GeneralStateBaseAddressModifyEnable   = true;
      sba.GeneralStateBufferSize                = size;
      sba.GeneralStateBufferSizeModifyEnable    = true;
      sba.GeneralStateMOCS                      = mocs;

      sba.DynamicStateBaseAddress               = state_base_address;
      sba.DynamicStateBaseAddressModifyEnable   = true;
      sba.DynamicStateBufferSize                = size;
      sba.DynamicStateBufferSizeModifyEnable    = true;
      sba.DynamicStateMOCS                      = mocs;

      sba.InstructionBaseAddress                = base_address;
      sba.InstructionBaseAddressModifyEnable    = true;
      sba.InstructionBufferSize                 = size;
      sba.InstructionBuffersizeModifyEnable     = true;
      sba.InstructionMOCS                       = mocs;

      sba.IndirectObjectBaseAddress             = base_address;
      sba.IndirectObjectBaseAddressModifyEnable = true;
      sba.IndirectObjectBufferSize              = size;
      sba.IndirectObjectBufferSizeModifyEnable  = true;
      sba.IndirectObjectMOCS                    = mocs;

      sba.SurfaceStateBaseAddress               = state_base_address;
      sba.SurfaceStateBaseAddressModifyEnable   = true;
      sba.SurfaceStateMOCS                      = mocs;
      sba.StatelessDataPortAccessMOCS           = mocs;

#if GFX_VER >= 11
      sba.BindlessSamplerStateMOCS    = mocs;
#endif
      sba.BindlessSurfaceStateMOCS    = mocs;

#if GFX_VERx10 >= 125
      sba.L1CacheControl = L1CC_WB;
#endif
   };
}

static void
executor_perf_begin(executor_context *ec)
{
   if (!intel_perf_begin_query(ec->perf_query.ctx, ec->perf_query.obj))
      failf("failed to begin OA performance query");
}

static void
executor_perf_end(executor_context *ec)
{
   intel_perf_end_query(ec->perf_query.ctx, ec->perf_query.obj);
}

static void
emit_binding_table_pool_alloc(executor_context *ec, uint32_t mocs)
{
#if GFX_VER >= 11
   executor_batch_emit(GENX(3DSTATE_BINDING_TABLE_POOL_ALLOC), btpa) {
      btpa.BindingTablePoolBaseAddress = (executor_address){ec->bo.extra.addr};
      btpa.BindingTablePoolBufferSize = ec->bo.extra.size / 4096;
#if GFX_VERx10 < 125
      btpa.BindingTablePoolEnable = ec->num_surface_bindings != 0;
#endif
      btpa.MOCS = mocs;
   }
#endif
}

static void
emit_surface_state(executor_context *ec,
                   const executor_surface_binding *binding,
                   void *surface_state, uint32_t mocs)
{
   const uint64_t address = ec->bo.data.addr + binding->region.offset;

   if (binding->type == EXECUTOR_SURFACE_BUFFER) {
      isl_buffer_fill_state(ec->isl_dev, surface_state,
                            .address = address,
                            .mocs = mocs,
                            .size_B = binding->region.size,
                            .format = binding->format,
                            .swizzle = ISL_SWIZZLE_IDENTITY,
                            .stride_B = binding->stride,
                            .usage = ISL_SURF_USAGE_TEXTURE_BIT);
      return;
   }

   assert(binding->type == EXECUTOR_SURFACE_2D);

   struct isl_surf surf;
   bool ok = isl_surf_init(ec->isl_dev, &surf,
                           .dim = ISL_SURF_DIM_2D,
                           .format = binding->format,
                           .width = binding->width,
                           .height = binding->height,
                           .depth = 1,
                           .levels = 1,
                           .array_len = 1,
                           .samples = 1,
                           .row_pitch_B = binding->stride,
                           .usage = ISL_SURF_USAGE_TEXTURE_BIT,
                           .tiling_flags = ISL_TILING_LINEAR_BIT);
   if (!ok)
      failf("failed to create surface_2d surface");

   if (!util_is_aligned(address, surf.alignment_B))
      failf("surface_2d memory offset does not satisfy surface alignment");
   if (surf.size_B > binding->region.size)
      failf("surface_2d memory object too small for surface layout");

   const struct isl_view view = {
      .usage = ISL_SURF_USAGE_TEXTURE_BIT,
      .format = binding->format,
      .base_level = 0,
      .levels = 1,
      .base_array_layer = 0,
      .array_len = 1,
      .swizzle = ISL_SWIZZLE_IDENTITY,
   };

   isl_surf_fill_state(ec->isl_dev, surface_state,
                       .surf = &surf,
                       .view = &view,
                       .address = address,
                       .mocs = mocs);
}

static uint32_t
emit_surface_states(executor_context *ec, uint32_t mocs)
{
   if (ec->num_surface_bindings == 0)
      return 0;

   uint32_t *binding_table =
      executor_alloc_bytes_aligned(&ec->bo.extra,
                                   ec->num_surface_bindings * sizeof(uint32_t),
                                   32);

   for (uint32_t i = 0; i < ec->num_surface_bindings; i++) {
      const executor_surface_binding *binding = &ec->surface_bindings[i];
      void *surface_state =
         executor_alloc_bytes_aligned(&ec->bo.extra,
                                      GENX(RENDER_SURFACE_STATE_length) * 4,
                                      64);

      emit_surface_state(ec, binding, surface_state, mocs);

      binding_table[i] = executor_address_of_ptr(&ec->bo.extra,
                                                 surface_state).offset -
                         ec->bo.extra.addr;
   }

   return executor_address_of_ptr(&ec->bo.extra, binding_table).offset -
          ec->bo.extra.addr;
}

static uint32_t
sampler_filter_to_gen(executor_sampler_filter filter)
{
   switch (filter) {
   case EXECUTOR_SAMPLER_FILTER_NEAREST: return MAPFILTER_NEAREST;
   case EXECUTOR_SAMPLER_FILTER_LINEAR:  return MAPFILTER_LINEAR;
   default: UNREACHABLE("invalid sampler filter");
   }
}

static uint32_t
sampler_address_to_gen(executor_sampler_address address)
{
   switch (address) {
   case EXECUTOR_SAMPLER_ADDRESS_CLAMP:  return TCM_CLAMP;
   case EXECUTOR_SAMPLER_ADDRESS_REPEAT: return TCM_WRAP;
   case EXECUTOR_SAMPLER_ADDRESS_MIRROR: return TCM_MIRROR;
   default: UNREACHABLE("invalid sampler address mode");
   }
}

static uint32_t
emit_sampler_states(executor_context *ec)
{
   if (ec->num_sampler_bindings == 0)
      return 0;

   uint32_t sampler_size = GENX(SAMPLER_STATE_length) * 4;
   char *sampler_states =
      executor_alloc_bytes_aligned(&ec->bo.extra,
                                   ec->num_sampler_bindings * sampler_size,
                                   32);

   for (uint32_t i = 0; i < ec->num_sampler_bindings; i++) {
      const executor_sampler_binding *binding = &ec->sampler_bindings[i];
      const uint32_t address =
         sampler_address_to_gen(binding->address_mode);
      struct GENX(SAMPLER_STATE) sampler = {
         .TextureBorderColorMode = DX10OGL,
         .LODPreClampMode = CLAMP_MODE_OGL,
         .MipModeFilter = MIPFILTER_NONE,
         .MagModeFilter = sampler_filter_to_gen(binding->mag_filter),
         .MinModeFilter = sampler_filter_to_gen(binding->min_filter),
         .MinLOD = binding->min_lod,
         .MaxLOD = binding->max_lod,
         .TCXAddressControlMode = address,
         .TCYAddressControlMode = address,
         .TCZAddressControlMode = address,
         .NonnormalizedCoordinateEnable = binding->nonnormalized_coordinates,
      };

      GENX(SAMPLER_STATE_pack)(NULL, sampler_states + i * sampler_size, &sampler);
   }

   return executor_address_of_ptr(&ec->bo.extra, sampler_states).offset -
          ec->bo.extra.addr;
}

void
genX(emit_execute)(const executor_run *run)
{
   executor_context *ec = run->ec;
   const uint32_t simd_size = run->simd / 16;
   const uint32_t mocs = isl_mocs(ec->isl_dev, 0, false);
   const uint32_t binding_table_offset = emit_surface_states(ec, mocs);
   const uint32_t sampler_state_offset = emit_sampler_states(ec);

   uint32_t *kernel = executor_alloc_bytes_aligned(&ec->bo.extra,
                                                   run->kernel_size, 64);
   memcpy(kernel, run->kernel_bin, run->kernel_size);
   executor_address kernel_addr = executor_address_of_ptr(&ec->bo.extra, kernel);

   struct GENX(INTERFACE_DESCRIPTOR_DATA) desc = {
      .KernelStartPointer = kernel_addr.offset,
      .SamplerStatePointer = sampler_state_offset,
      .SamplerCount = DIV_ROUND_UP(ec->num_sampler_bindings, 4),
      .BindingTablePointer = binding_table_offset,
      .BindingTableEntryCount = ec->num_surface_bindings,
      .NumberofThreadsinGPGPUThreadGroup = run->hw_threads,
      .SharedLocalMemorySize =
         intel_compute_slm_encode_size(GFX_VER, run->slm_size),
#if GFX_VERx10 < 125
      .BarrierEnable = run->hw_threads > 1,
      .ConstantURBEntryReadOffset = 0,
      .ConstantURBEntryReadLength = 1,
      .CrossThreadConstantDataReadLength = 0,
#else
      .PreferredSLMAllocationSize =
         intel_compute_preferred_slm_calc_encode_size(ec->devinfo,
                                                      run->slm_size,
                                                      run->hw_threads * run->simd,
                                                      run->simd),
      .NumberOfBarriers = run->hw_threads > 1,
#endif
#if GFX_VER >= 30
      .RegistersPerThread =
         intel_register_blocks(ec->devinfo, run->hw_regs),
#endif
   };

   void *b = executor_alloc_bytes_aligned(&ec->bo.batch, 0, 256);
   ec->batch_start = executor_address_of_ptr(&ec->bo.batch, b).offset;

   emit_pipe_control(ec);

#if GFX_VERx10 < 200
   executor_batch_emit(GENX(PIPELINE_SELECT), ps) {
      ps.PipelineSelection = GPGPU;
      ps.MaskBits = 0x3;
   }
   emit_pipe_control(ec);
#endif

   emit_state_base_address(ec, mocs);
   emit_binding_table_pool_alloc(ec, mocs);
   emit_state_invalidate(ec);

   const uint32_t max_cs_threads =
      ec->devinfo->max_cs_threads * ec->devinfo->subslice_total;

#if GFX_VERx10 >= 125
   executor_batch_emit(GENX(STATE_COMPUTE_MODE), cm) {
      cm.Mask1 = 0xffff;
#if GFX_VER < 30
      cm.LargeGRFMode = run->hw_regs == 256;
#endif
#if GFX_VERx10 >= 200
      cm.Mask2 = 0xffff;
#endif
#if GFX_VER >= 30
      cm.EnableVariableRegisterSizeAllocationMask = 1;
      cm.EnableVariableRegisterSizeAllocation = 1;
#endif
   }

   executor_batch_emit(GENX(CFE_STATE), cfe) {
      cfe.MaximumNumberofThreads = max_cs_threads;
   }
#else
   executor_batch_emit(GENX(MEDIA_VFE_STATE), vfe) {
      vfe.NumberofURBEntries = 2;
      vfe.MaximumNumberofThreads = max_cs_threads - 1;
      vfe.CURBEAllocationSize = align(run->hw_threads, 2);
   }
#endif

   emit_pipe_control(ec);

#if GFX_VERx10 >= 125
   struct GENX(COMPUTE_WALKER_BODY) body = {
      .SIMDSize                = simd_size,
#if GFX_VERx10 >= 200
      .MessageSIMD             = simd_size,
#endif
      .ThreadGroupIDXDimension = run->thread_groups,
      .ThreadGroupIDYDimension = 1,
      .ThreadGroupIDZDimension = 1,
      .ExecutionMask           = 0xFFFFFFFF,
      .PostSync.MOCS           = mocs,
      .InterfaceDescriptor     = desc,
   };
#endif

#if GFX_VERx10 >= 125
   if (ec->perf_enabled)
      executor_perf_begin(ec);

   executor_batch_emit(GENX(COMPUTE_WALKER), cw) {
      cw.body = body;
   };

   if (ec->perf_enabled)
      executor_perf_end(ec);
#else
   uint32_t *idd = executor_alloc_bytes_aligned(&ec->bo.extra, 8 * 4, 256);
   GENX(INTERFACE_DESCRIPTOR_DATA_pack)(NULL, idd, &desc);

   executor_address idd_addr = executor_address_of_ptr(&ec->bo.extra, idd);

   /* DynamicStateBaseAddress points at the extra BO, so these legacy media
    * command offsets are relative to that base rather than absolute GPU
    * addresses.
    */
   executor_batch_emit(GENX(MEDIA_INTERFACE_DESCRIPTOR_LOAD), load) {
      load.InterfaceDescriptorDataStartAddress =
         idd_addr.offset - ec->bo.extra.addr;
      load.InterfaceDescriptorTotalLength = 8 * 4;
   }

   /* Pre-Gfx12.5, the hardware thread id is not part of the thread
    * payload. Pass it through per-thread CURBE data instead: one GRF per
    * hardware thread, with the id in the first dword.
    */
   const uint32_t curbe_size =
      align(ec->devinfo->grf_size * run->hw_threads, 64);
   void *curbe = executor_alloc_bytes_aligned(&ec->bo.extra, curbe_size, 64);
   memset(curbe, 0, curbe_size);

   for (uint32_t t = 0; t < run->hw_threads; t++) {
      uint32_t *record = (uint32_t *)((char *)curbe + t * ec->devinfo->grf_size);
      record[0] = t;
   }
   executor_address curbe_addr = executor_address_of_ptr(&ec->bo.extra, curbe);

   executor_batch_emit(GENX(MEDIA_CURBE_LOAD), load) {
      load.CURBEDataStartAddress = curbe_addr.offset - ec->bo.extra.addr;
      load.CURBETotalDataLength = curbe_size;
   }

   if (ec->perf_enabled)
      executor_perf_begin(ec);

   executor_batch_emit(GENX(GPGPU_WALKER), gw) {
      gw.SIMDSize = simd_size;
      gw.ThreadWidthCounterMaximum = run->hw_threads - 1;
      gw.ThreadGroupIDXDimension = run->thread_groups;
      gw.ThreadGroupIDYDimension = 1;
      gw.ThreadGroupIDZDimension = 1;
      gw.RightExecutionMask      = 0xFFFFFFFF;
      gw.BottomExecutionMask     = 0xFFFFFFFF;
   }

   executor_batch_emit(GENX(MEDIA_STATE_FLUSH), msf);

   if (ec->perf_enabled)
      executor_perf_end(ec);
#endif

   emit_pipe_control(ec);

   executor_batch_emit(GENX(MI_BATCH_BUFFER_END), end);
}
