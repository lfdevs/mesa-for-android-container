/*
 * Copyright © 2026 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "hw_runner_priv.h"

#include <assert.h>
#include "util/macros.h"
#include "genxml/gen_macros.h"

#include "drm-uapi/panthor_drm.h"
#include "kmod/pan_kmod.h"
#include "kmod/panthor_kmod.h"
#include "cs_builder.h"

#define MAX_CMD_STREAM_LEN_B 1024

struct hw_runner_cmdstream_info {
   struct {
      uint64_t *host_ptr;

      /* GPU pointer */
      uint64_t device_ptr;

      /* Capacity in bytes */
      uint32_t capacity_B;

      /* Written CS size in bytes (output) */
      uint32_t size_B;
   } output_cs;

   uint32_t invocations;

   uint64_t shader_resource_device_ptr;
   uint8_t shader_resource_table_size;

   uint64_t fau_device_ptr;
   uint8_t fau_count;

   uint64_t shader_program_descriptor_device_ptr;

   uint64_t thread_storage_descriptor_device_ptr;
};

static void
hw_runner_fill_cmd_stream(struct pan_kmod_dev *dev,
                          struct hw_runner_cmdstream_info *info)
{
   const struct drm_panthor_csif_info *csif_info =
      panthor_kmod_get_csif_props(dev);
   struct cs_builder builder;
   struct cs_builder *b = &builder;

   struct cs_builder_conf conf = {
      .nr_registers = csif_info->cs_reg_count,
      .nr_kernel_registers = MAX2(csif_info->unpreserved_cs_reg_count, 4),
      .compute_ep_limit = dev->props.max_tasks_per_core,
      .alloc_buffer = NULL,
      .cookie = NULL,
      .ls_sb_slot = 0,
   };

   struct cs_buffer backing_buffer = {
      .cpu = info->output_cs.host_ptr,
      .gpu = info->output_cs.device_ptr,
      .capacity = info->output_cs.capacity_B / sizeof(uint64_t),
   };
   cs_builder_init(b, &conf, backing_buffer);

   /* Request resources */
   cs_req_res(b, CS_COMPUTE_RES);

   /* Flush caches */
   cs_move32_to(b, cs_reg32(b, 0), 0);
   cs_flush_caches(b, MALI_CS_FLUSH_MODE_CLEAN_AND_INVALIDATE,
                   MALI_CS_FLUSH_MODE_CLEAN_AND_INVALIDATE,
                   MALI_CS_OTHER_FLUSH_MODE_INVALIDATE, cs_reg32(b, 0),
                   cs_defer(0, 0));

   cs_wait_slot(b, 0);

   /* Setup pointer backing registers */
   assert((info->shader_resource_device_ptr & BITFIELD64_MASK(6)) == 0);
   assert((info->shader_resource_table_size & ~BITFIELD64_MASK(6)) == 0);
   uint64_t srt = info->shader_resource_device_ptr |
                  info->shader_resource_table_size;
   cs_move64_to(b, cs_sr_reg64(b, COMPUTE, SRT_0), srt);

   uint64_t fau = info->fau_device_ptr | (((uint64_t) info->fau_count) << 56);
   cs_move64_to(b, cs_sr_reg64(b, COMPUTE, FAU_0), fau);

   assert((info->shader_program_descriptor_device_ptr & BITFIELD64_MASK(5)) == 0);
   cs_move64_to(b, cs_sr_reg64(b, COMPUTE, SPD_0),
                info->shader_program_descriptor_device_ptr);

   assert((info->thread_storage_descriptor_device_ptr & BITFIELD64_MASK(6)) == 0);
   cs_move64_to(b, cs_sr_reg64(b, COMPUTE, TSD_0),
                info->thread_storage_descriptor_device_ptr);

   /* Setup attribute offset */
   cs_move32_to(b, cs_sr_reg32(b, COMPUTE, GLOBAL_ATTRIBUTE_OFFSET), 0);

   struct mali_compute_size_workgroup_packed wg_size;
   pan_pack(&wg_size, COMPUTE_SIZE_WORKGROUP, cfg) {
      cfg.workgroup_size_x = 1;
      cfg.workgroup_size_y = 1;
      cfg.workgroup_size_z = 1;
      cfg.allow_merging_workgroups = true;
   }
   cs_move32_to(b, cs_sr_reg32(b, COMPUTE, WG_SIZE),
                wg_size.opaque[0]);

   cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_OFFSET_X), 0);
   cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_OFFSET_Y), 0);
   cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_OFFSET_Z), 0);
   cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_SIZE_X), info->invocations);
   cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_SIZE_Y), 1);
   cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_SIZE_Z), 1);

   /* Set the ENDPOINT Scoreboard Entry to be 0 */
   cs_select_endpoint_sb(b, 0);
   /* Run the compute task */
   cs_run_compute(b, 1, MALI_TASK_AXIS_X, cs_shader_res_sel(0, 0, 0, 0));
   /* Wait on scoreboard entry 0 for the compute job to complete */
   cs_wait_slot(b, 0);

   /* Flush caches */
   cs_move32_to(b, cs_reg32(b, 0), 0);
   cs_flush_caches(b, MALI_CS_FLUSH_MODE_CLEAN_AND_INVALIDATE,
                   MALI_CS_FLUSH_MODE_CLEAN_AND_INVALIDATE,
                   MALI_CS_OTHER_FLUSH_MODE_NONE, cs_reg32(b, 0),
                   cs_defer(0, 0));

   cs_wait_slot(b, 0);

   assert(cs_is_valid(b));
   cs_end(b);

   info->output_cs.size_B = cs_root_chunk_size(b);

   cs_builder_fini(b);
}

void
GENX(hw_runner_new_cmd_stream)(struct pan_kmod_dev *kdev,
                               struct hw_runner_invocation_info *info,
                               struct hw_runner_layout_info *out)
{
   uint64_t size_B = 0;

   /* Lay down everything and compute all offsets.
    * Current layout:
    * - FAU
    * - code
    * - TSD: Thread Storage Descriptor
    * - SPD: Shader Program Descriptor
    * - CS: Command Stream (CSF commands)
    * Writable data must be in a separate non-executable buffer
    */
   uint64_t fau_offset_B = size_B;
   size_B = fau_offset_B + info->fau_size_B;

   uint64_t shader_offset_B = ALIGN_POT(size_B, 128);
   size_B = shader_offset_B + info->code_size_B;

   uint64_t tsd_offset = ALIGN_POT(size_B, MALI_LOCAL_STORAGE_ALIGN);
   size_B = tsd_offset + MALI_LOCAL_STORAGE_LENGTH;

   uint64_t spd_offset = ALIGN_POT(size_B, MALI_SHADER_PROGRAM_ALIGN);
   size_B = spd_offset + MALI_SHADER_PROGRAM_LENGTH;

   uint64_t cs_offset = ALIGN_POT(size_B, 128);
   size_B = cs_offset + MAX_CMD_STREAM_LEN_B;

   *out = (struct hw_runner_layout_info) {
      .descr_bo_size_B = size_B,
      .cs_offset = cs_offset,
      .cs_size_B = 0,
   };

   if (info->data_bo_host_ptr == NULL || info->descr_bo_host_ptr == NULL)
      return;

   /* Fill the data in */
   struct hw_runner_shader_args args = {
      .data_addr = info->data_bo_device_ptr,
      .data_stride = info->data_stride_B,
   };

   void *descr_ptr = info->descr_bo_host_ptr;

   /* FAU */
   memcpy(descr_ptr + fau_offset_B, info->fau_ptr, info->fau_size_B);
   /* shader_args must be copied on top of FAU */
   memcpy(descr_ptr + fau_offset_B + info->args_fau_offset, &args,
          sizeof(args));
   /* Shader code */
   memcpy(descr_ptr + shader_offset_B, info->code_ptr, info->code_size_B);

   /* TSD (TODO: local storage) */
   struct mali_local_storage_packed *tsd_packed = descr_ptr + tsd_offset;
   pan_pack(tsd_packed, LOCAL_STORAGE, tsd) {
      tsd.tls_size = 0;
      tsd.tls_base_pointer = 0;
      tsd.tls_address_mode = MALI_ADDRESS_MODE_PACKED;

      tsd.wls_instances = MALI_LOCAL_STORAGE_NO_WORKGROUP_MEM;
      tsd.wls_base_pointer = 0;
      tsd.wls_size_base = 0;
      tsd.wls_size_scale = 0;
   }

   /* SPD */
   struct mali_shader_program_packed *spd_packed = descr_ptr + spd_offset;
   pan_pack(spd_packed, SHADER_PROGRAM, spd) {
      spd.stage = MALI_SHADER_STAGE_COMPUTE;
      assert((info->register_preload & ((1ull << 48) - 1)) == 0);
      spd.preload.r48_r63 = info->register_preload >> 48;
      spd.suppress_nan = false;
      spd.flush_to_zero_mode = MALI_FLUSH_TO_ZERO_MODE_PRESERVE_SUBNORMALS;
      spd.suppress_inf = false;
      spd.shader_contains_jump_ex = false;
      assert(info->register_count <= 64);
      spd.register_allocation = info->register_count <= 32 ?
         MALI_SHADER_REGISTER_ALLOCATION_32_PER_THREAD :
         MALI_SHADER_REGISTER_ALLOCATION_64_PER_THREAD;
#if PAN_ARCH >= 12
      spd.max_warps = 1;
#endif
      spd.binary = info->descr_bo_device_ptr + shader_offset_B;
   }

   /* Write CSF command stream */
   struct hw_runner_cmdstream_info cmdstream_info = {
      .output_cs = {
         .host_ptr = info->descr_bo_host_ptr + cs_offset,
         .device_ptr = info->descr_bo_device_ptr + cs_offset,
         .capacity_B = MAX_CMD_STREAM_LEN_B,
         .size_B = 0,
      },
      .invocations = info->invocations,
      .shader_resource_device_ptr = 0,
      .shader_resource_table_size = 0,
      .fau_device_ptr = info->fau_size_B ? (info->descr_bo_device_ptr + fau_offset_B) : 0,
      .fau_count = (info->fau_size_B / 4),
      .shader_program_descriptor_device_ptr = info->descr_bo_device_ptr + spd_offset,
      .thread_storage_descriptor_device_ptr = info->descr_bo_device_ptr + tsd_offset,
   };

   hw_runner_fill_cmd_stream(kdev, &cmdstream_info);
   out->cs_size_B = cmdstream_info.output_cs.size_B;

   /* At last, copy data */
   memcpy(info->data_bo_host_ptr, info->data_ptr, info->data_size_B);
}
