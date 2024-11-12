/*
 * Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "executor.h"

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
emit_state_base_address(executor_context *ec, uint32_t mocs)
{
   /* Use the full address for everything. */
   const executor_address base_address = {0};
   const uint32_t size                 = (1 << 20) - 1;

   executor_batch_emit(GENX(STATE_BASE_ADDRESS), sba) {
      sba.GeneralStateBaseAddress               = base_address;
      sba.GeneralStateBaseAddressModifyEnable   = true;
      sba.GeneralStateBufferSize                = size;
      sba.GeneralStateBufferSizeModifyEnable    = true;
      sba.GeneralStateMOCS                      = mocs;

      sba.DynamicStateBaseAddress               = base_address;
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

      sba.SurfaceStateMOCS            = mocs;
      sba.StatelessDataPortAccessMOCS = mocs;

#if GFX_VER >= 11
      sba.BindlessSamplerStateMOCS    = mocs;
#endif
      sba.BindlessSurfaceStateMOCS    = mocs;

#if GFX_VERx10 >= 125
      sba.L1CacheControl = L1CC_WB;
#endif
   };
}

void
genX(emit_execute)(executor_context *ec, const executor_params *params)
{
   uint32_t *kernel = executor_alloc_bytes(&ec->bo.extra, params->kernel_size);
   memcpy(kernel, params->kernel_bin, params->kernel_size);
   executor_address kernel_addr = executor_address_of_ptr(&ec->bo.extra, kernel);

   /* TODO: Let SIMD be a parameter. */

   struct GENX(INTERFACE_DESCRIPTOR_DATA) desc = {
      .KernelStartPointer = kernel_addr.offset,
      .NumberofThreadsinGPGPUThreadGroup = 1,
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

   const uint32_t mocs = isl_mocs(ec->isl_dev, 0, false);

   emit_state_base_address(ec, mocs);

#if GFX_VERx10 >= 125
   executor_batch_emit(GENX(STATE_COMPUTE_MODE), cm) {
      cm.Mask1 = 0xffff;
#if GFX_VERx10 >= 200
      cm.Mask2 = 0xffff;
#endif
   }

   executor_batch_emit(GENX(CFE_STATE), cfe) {
      cfe.MaximumNumberofThreads = 64;
   }
#else
   executor_batch_emit(GENX(MEDIA_VFE_STATE), vfe) {
      vfe.NumberofURBEntries = 2;
      vfe.MaximumNumberofThreads = 64;
   }
#endif

   emit_pipe_control(ec);

#if GFX_VERx10 >= 125
   struct GENX(COMPUTE_WALKER_BODY) body = {
#if GFX_VERx10 >= 200
      .SIMDSize                = 1,
      .MessageSIMD             = 1,
#endif
      .ThreadGroupIDXDimension = 1,
      .ThreadGroupIDYDimension = 1,
      .ThreadGroupIDZDimension = 1,
      .ExecutionMask           = 0xFFFFFFFF,
      .PostSync.MOCS           = mocs,
      .InterfaceDescriptor     = desc,
   };
#endif

#if GFX_VERx10 >= 125
   executor_batch_emit(GENX(COMPUTE_WALKER), cw) {
      cw.body = body;
   };
#else
   uint32_t *idd = executor_alloc_bytes_aligned(&ec->bo.extra, 8 * 4, 256);
   GENX(INTERFACE_DESCRIPTOR_DATA_pack)(NULL, idd, &desc);

   executor_address idd_addr = executor_address_of_ptr(&ec->bo.extra, idd);

   executor_batch_emit(GENX(MEDIA_INTERFACE_DESCRIPTOR_LOAD), load) {
      load.InterfaceDescriptorDataStartAddress = idd_addr.offset,
      load.InterfaceDescriptorTotalLength = 8 * 4;
   }

   executor_batch_emit(GENX(GPGPU_WALKER), gw) {
      gw.ThreadGroupIDXDimension = 1;
      gw.ThreadGroupIDYDimension = 1;
      gw.ThreadGroupIDZDimension = 1;
      gw.RightExecutionMask      = 0xFFFFFFFF;
      gw.BottomExecutionMask     = 0xFFFFFFFF;
   }

   executor_batch_emit(GENX(MEDIA_STATE_FLUSH), msf);
#endif

   emit_pipe_control(ec);

   executor_batch_emit(GENX(MI_BATCH_BUFFER_END), end);
}
