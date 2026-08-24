/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_buffer.h"
#include "nvk_cmd_buffer.h"
#include "nvk_device.h"
#include "nvk_entrypoints.h"
#include "nvk_mme.h"
#include "nvk_physical_device.h"
#include "nvk_shader.h"

#include "cl906f.h"
#include "cla0b5.h"
#include "cla1c0.h"
#include "clc0c0.h"
#include "clc5c0.h"
#include "clcbc0.h"
#include "clcdc0.h"
#include "nv_push_cl90c0.h"
#include "nv_push_cl9097.h"
#include "nv_push_cla0c0.h"
#include "nv_push_clb0c0.h"
#include "nv_push_clb1c0.h"
#include "nv_push_clc3c0.h"
#include "nv_push_clc597.h"
#include "nv_push_clc6c0.h"
#include "nv_push_clc7c0.h"
#include "nv_push_clc86f.h"

VkResult
nvk_push_dispatch_state_init(struct nvk_queue *queue, struct nv_push *p)
{
   struct nvk_device *dev = nvk_queue_device(queue);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   P_MTHD(p, NV90C0, SET_OBJECT);
   P_NV90C0_SET_OBJECT(p, {
      .class_id = pdev->info.cls_compute,
      .engine_id = 0,
   });

   if (pdev->info.cls_compute == MAXWELL_COMPUTE_A)
      P_IMMD(p, NVB0C0, SET_SELECT_MAXWELL_TEXTURE_HEADERS, V_TRUE);

   if (pdev->info.cls_compute < VOLTA_COMPUTE_A) {
      uint64_t shader_base_addr =
         nvk_heap_contiguous_base_address(&dev->shader_heap);

      P_MTHD(p, NVA0C0, SET_PROGRAM_REGION_A);
      P_NVA0C0_SET_PROGRAM_REGION_A(p, shader_base_addr >> 32);
      P_NVA0C0_SET_PROGRAM_REGION_B(p, shader_base_addr);
   }

   if (pdev->info.cls_compute >= VOLTA_COMPUTE_A) {
      /* From nvc0_screen.c:
       *
       *    "Reduce likelihood of collision with real buffers by placing the
       *    hole at the top of the 4G area. This will have to be dealt with
       *    for real eventually by blocking off that area from the VM."
       *
       * Really?!?  TODO: Fix this for realz.
       */
      uint64_t temp;

      /* shared memory window needs 4GB alignment on hopper+ */
      if (pdev->info.cls_compute >= HOPPER_COMPUTE_A)
         temp = 1ULL << 32;
      else
         temp = 0xfeULL << 24;

      P_MTHD(p, NVC3C0, SET_SHADER_SHARED_MEMORY_WINDOW_A);
      P_NVC3C0_SET_SHADER_SHARED_MEMORY_WINDOW_A(p, temp >> 32);
      P_NVC3C0_SET_SHADER_SHARED_MEMORY_WINDOW_B(p, temp & 0xffffffff);

      temp = 0xffULL << 24;
      P_MTHD(p, NVC3C0, SET_SHADER_LOCAL_MEMORY_WINDOW_A);
      P_NVC3C0_SET_SHADER_LOCAL_MEMORY_WINDOW_A(p, temp >> 32);
      P_NVC3C0_SET_SHADER_LOCAL_MEMORY_WINDOW_B(p, temp & 0xffffffff);
   } else {
      P_MTHD(p, NVA0C0, SET_SHADER_LOCAL_MEMORY_WINDOW);
      P_NVA0C0_SET_SHADER_LOCAL_MEMORY_WINDOW(p, 0xff << 24);

      P_MTHD(p, NVA0C0, SET_SHADER_SHARED_MEMORY_WINDOW);
      P_NVA0C0_SET_SHADER_SHARED_MEMORY_WINDOW(p, 0xfe << 24);
   }

   return VK_SUCCESS;
}

static inline uint16_t
nvk_cmd_buffer_compute_cls(struct nvk_cmd_buffer *cmd)
{
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);
   return pdev->info.cls_compute;
}

void
nvk_cmd_buffer_begin_compute(struct nvk_cmd_buffer *cmd,
                             const VkCommandBufferBeginInfo *pBeginInfo)
{
   if (cmd->vk.level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
      struct nv_push *p = nvk_cmd_buffer_push(cmd, 6);
      if (nvk_cmd_buffer_compute_cls(cmd) >= MAXWELL_COMPUTE_B) {
         P_IMMD(p, NVB1C0, INVALIDATE_SKED_CACHES, 0);
      }
      P_IMMD(p, NVA0C0, INVALIDATE_SAMPLER_CACHE_NO_WFI, {
         .lines = LINES_ALL,
      });
      P_IMMD(p, NVA0C0, INVALIDATE_TEXTURE_HEADER_CACHE_NO_WFI, {
         .lines = LINES_ALL,
      });
   }
}

void
nvk_cmd_invalidate_compute_state(struct nvk_cmd_buffer *cmd)
{
   memset(&cmd->state.cs, 0, sizeof(cmd->state.cs));
}

void
nvk_cmd_bind_compute_shader(struct nvk_cmd_buffer *cmd,
                            struct nvk_shader *shader)
{
   cmd->state.cs.shader = shader;
}

static uint32_t
nvk_compute_local_size(struct nvk_cmd_buffer *cmd)
{
   const struct nvk_shader *shader = cmd->state.cs.shader;

   return shader->info.cs.local_size[0] *
          shader->info.cs.local_size[1] *
          shader->info.cs.local_size[2];
}

static void
nvk_flush_compute_state(struct nvk_cmd_buffer *cmd,
                        uint32_t base_workgroup[3],
                        uint32_t global_size[3])
{
   struct nvk_descriptor_state *desc = &cmd->state.cs.descriptors;

   nvk_cmd_buffer_flush_push_descriptors(cmd, desc);

   nvk_descriptor_state_set_root_array(cmd, desc, cs.base_group,
                                       0, 3, base_workgroup);
   nvk_descriptor_state_set_root_array(cmd, desc, cs.group_count,
                                       0, 3, global_size);

   if (NAK_CAN_PRINTF)
      nvk_cmd_buffer_flush_printf_buffer(cmd, desc);
}

static VkResult
nvk_cmd_upload_qmd(struct nvk_cmd_buffer *cmd,
                   const struct nvk_shader *shader,
                   const struct nvk_descriptor_state *desc,
                   const void *root,
                   size_t root_size,
                   uint32_t global_size[3],
                   uint64_t *qmd_addr_out,
                   uint64_t *root_desc_addr_out)
{
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);
   const uint32_t min_cbuf_alignment = nvk_min_cbuf_alignment(&pdev->info);
   const uint32_t qmd_size_B = nak_qmd_size_B(&pdev->info);
   VkResult result;

   /* pre Pascal the constant buffer sizes need to be 0x100 aligned. As we
    * simply allocated a buffer and upload data to it, make sure its size is
    * 0x100 aligned.
    */
   assert(root_size % min_cbuf_alignment == 0);

   void *root_desc_map;
   uint64_t root_desc_addr;
   result = nvk_cmd_buffer_upload_alloc(cmd, root_size, min_cbuf_alignment,
                                        &root_desc_addr, &root_desc_map);
   if (unlikely(result != VK_SUCCESS))
      return result;

   memcpy(root_desc_map, root, root_size);

   uint64_t qmd_addr = 0;
   if (shader != NULL) {
      struct nak_qmd_info qmd_info = {
         .addr = shader->hdr_addr,
         .smem_size = shader->info.cs.smem_size,
         .global_size = {
            global_size[0],
            global_size[1],
            global_size[2],
         },
         .dependence_counter = 0,
         .hw_dependence_counter = 0,
      };

      assert(shader->cbuf_map.cbuf_count <= ARRAY_SIZE(qmd_info.cbufs));
      for (uint32_t c = 0; c < shader->cbuf_map.cbuf_count; c++) {
         const struct nvk_cbuf *cbuf = &shader->cbuf_map.cbufs[c];

         struct nvk_buffer_address ba;
         if (cbuf->type == NVK_CBUF_TYPE_ROOT_DESC) {
            ba = (struct nvk_buffer_address) {
               .base_addr = root_desc_addr,
               .size = root_size,
            };
         } else {
            ASSERTED bool direct_descriptor =
               nvk_cmd_buffer_get_cbuf_addr(cmd, desc, shader, cbuf, &ba);
            assert(direct_descriptor);
         }

         if (ba.size > 0) {
            assert(ba.base_addr % min_cbuf_alignment == 0);
            ba.size = align(ba.size, min_cbuf_alignment);
            ba.size = MIN2(ba.size, NVK_MAX_CBUF_SIZE);

            qmd_info.cbufs[qmd_info.num_cbufs++] = (struct nak_qmd_cbuf) {
               .index = c,
               .addr = ba.base_addr,
               .size = ba.size,
            };
         }
      }

      uint32_t qmd[NAK_MAX_QMD_DWORDS];
      assert(qmd_size_B <= sizeof(qmd));
      nak_fill_qmd(&pdev->info, &shader->info, &qmd_info, qmd, qmd_size_B);

      void *qmd_map;
      result = nvk_cmd_buffer_alloc_qmd(cmd, qmd_size_B, 0x100,
                                        &qmd_addr, &qmd_map);
      if (unlikely(result != VK_SUCCESS))
         return result;

      memcpy(qmd_map, qmd, qmd_size_B);
   }

   *qmd_addr_out = qmd_addr;
   if (root_desc_addr_out != NULL)
      *root_desc_addr_out = root_desc_addr;

   return VK_SUCCESS;
}

VkResult
nvk_cmd_flush_cs_qmd(struct nvk_cmd_buffer *cmd,
                     const struct nvk_cmd_state *state,
                     uint32_t global_size[3],
                     uint64_t *qmd_addr_out,
                     uint64_t *root_desc_addr_out)
{
   const struct nvk_descriptor_state *desc = &state->cs.descriptors;
   STATIC_ASSERT((sizeof(desc->root) & 0xff) == 0);

   return nvk_cmd_upload_qmd(cmd, state->cs.shader, desc,
                             desc->root, sizeof(desc->root),
                             global_size,
                             qmd_addr_out, root_desc_addr_out);
}

void
nvk_mme_add_cs_invocations(struct mme_builder *b)
{
   struct mme_value group_count_x = mme_load(b);
   struct mme_value group_count_y = mme_load(b);
   struct mme_value group_count_z = mme_load(b);

   /* Y and Z are 16b, so this cant't overflow */
   struct mme_value cs1 =
      mme_mul_32x32_32_free_srcs(b, group_count_y, group_count_z);
   struct mme_value64 cs2 =
      mme_umul_32x32_64_free_srcs(b, group_count_x, cs1);
   struct mme_value local_size = mme_load(b);
   struct mme_value64 count =
      mme_umul_32x64_64_free_srcs(b, local_size, cs2);

   struct mme_value accum_hi = nvk_mme_load_scratch(b, CS_INVOCATIONS_HI);
   struct mme_value accum_lo = nvk_mme_load_scratch(b, CS_INVOCATIONS_LO);
   struct mme_value64 accum = mme_value64(accum_lo, accum_hi);

   mme_add64_to(b, accum, accum, count);

   STATIC_ASSERT(NVK_MME_SCRATCH_CS_INVOCATIONS_HI + 1 ==
                 NVK_MME_SCRATCH_CS_INVOCATIONS_LO);

   mme_mthd(b, NVK_SET_MME_SCRATCH(CS_INVOCATIONS_HI));
   mme_emit(b, accum.hi);
   mme_emit(b, accum.lo);

   mme_free_reg64(b, accum);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdDispatchBase(VkCommandBuffer commandBuffer,
                    uint32_t baseGroupX,
                    uint32_t baseGroupY,
                    uint32_t baseGroupZ,
                    uint32_t groupCountX,
                    uint32_t groupCountY,
                    uint32_t groupCountZ)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);

   uint32_t base_workgroup[3] = { baseGroupX, baseGroupY, baseGroupZ };
   uint32_t global_size[3] = { groupCountX, groupCountY, groupCountZ };
   nvk_flush_compute_state(cmd, base_workgroup, global_size);

   uint64_t qmd_addr = 0;
   VkResult result = nvk_cmd_flush_cs_qmd(cmd, &cmd->state, global_size,
                                          &qmd_addr, NULL);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   /* Only emit this if we have a compute shader invocations query */
   if (cmd->state.cs.active_compute_invocations_query ||
       cmd->state.inherited_pipeline_statistics &
          VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT) {
      const uint32_t local_size = nvk_compute_local_size(cmd);

      struct nv_push *p = nvk_cmd_buffer_push(cmd, 5);
      if (nvk_cmd_buffer_compute_cls(cmd) >= AMPERE_COMPUTE_B)
         P_1INC(p, NVC7C0, CALL_MME_MACRO(NVK_MME_ADD_CS_INVOCATIONS));
      else
         P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_ADD_CS_INVOCATIONS));
      P_INLINE_DATA(p, groupCountX);
      P_INLINE_DATA(p, groupCountY);
      P_INLINE_DATA(p, groupCountZ);
      P_INLINE_DATA(p, local_size);
   }

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 4);
   P_MTHD(p, NVA0C0, SEND_PCAS_A);
   P_NVA0C0_SEND_PCAS_A(p, qmd_addr >> 8);

   if (nvk_cmd_buffer_compute_cls(cmd) <= TURING_COMPUTE_A) {
      P_IMMD(p, NVA0C0, SEND_SIGNALING_PCAS_B, {
            .invalidate = INVALIDATE_TRUE,
            .schedule = SCHEDULE_TRUE
      });
   } else {
      P_IMMD(p, NVC6C0, SEND_SIGNALING_PCAS2_B,
             PCAS_ACTION_INVALIDATE_COPY_SCHEDULE);
   }
}

void
nvk_cmd_dispatch_with_root(struct nvk_cmd_buffer *cmd,
                           struct nvk_shader *shader,
                           const void *root,
                           size_t root_size,
                           uint32_t groupCountX,
                           uint32_t groupCountY,
                           uint32_t groupCountZ)
{
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);

   uint32_t group_count[3] = { groupCountX, groupCountY, groupCountZ };

   uint64_t qmd_addr;
   VkResult result = nvk_cmd_upload_qmd(cmd, shader, NULL,
                                        root, root_size,
                                        group_count,
                                        &qmd_addr, NULL);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   if (shader != NULL) {
      nvk_device_ensure_slm(dev, shader->info.slm_size,
                                 shader->info.crs_size);
   }

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 8);

   /* Internal shaders don't want conditional rendering */
   P_IMMD(p, NVA0C0, SET_RENDER_ENABLE_OVERRIDE, MODE_ALWAYS_RENDER);

   P_MTHD(p, NVA0C0, SEND_PCAS_A);
   P_NVA0C0_SEND_PCAS_A(p, qmd_addr >> 8);

   if (nvk_cmd_buffer_compute_cls(cmd) <= TURING_COMPUTE_A) {
      P_IMMD(p, NVA0C0, SEND_SIGNALING_PCAS_B, {
            .invalidate = INVALIDATE_TRUE,
            .schedule = SCHEDULE_TRUE
      });
   } else {
      P_IMMD(p, NVC6C0, SEND_SIGNALING_PCAS2_B,
             PCAS_ACTION_INVALIDATE_COPY_SCHEDULE);
   }

   P_IMMD(p, NVA0C0, SET_RENDER_ENABLE_OVERRIDE, MODE_USE_RENDER_ENABLE);
}

void
nvk_cmd_dispatch_shader(struct nvk_cmd_buffer *cmd,
                        struct nvk_shader *shader,
                        const void *push_data, size_t push_size,
                        uint32_t groupCountX,
                        uint32_t groupCountY,
                        uint32_t groupCountZ)
{
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);

   struct nvk_root_descriptor_table root = {
      .cs.group_count = {
         groupCountX,
         groupCountY,
         groupCountZ,
      },
   };
   assert(push_size <= sizeof(root.push));
   memcpy(root.push, push_data, push_size);

   if (NAK_CAN_PRINTF) {
      struct nvkmd_mem *bo = (struct nvkmd_mem *)dev->printf.bo;
      root.printf_buffer_addr = bo->va->addr;
   }

   nvk_cmd_dispatch_with_root(cmd, shader, &root, sizeof(root),
                              groupCountX, groupCountY, groupCountZ);
}

static void
nvk_cmd_compute_indirect_copy(struct nvk_cmd_buffer *cmd, uint64_t dst_addr,
                              uint64_t src_addr, uint32_t size)
{
   /* The size granuality of I2M is in byte and can support unaligned addresses.
    * We allow those cases here.
    */
   struct nv_push *p = nvk_cmd_buffer_push(cmd, 7);
   P_MTHD(p, NVA0C0, LINE_LENGTH_IN);
   P_NVA0C0_LINE_LENGTH_IN(p, size);
   P_NVA0C0_LINE_COUNT(p, 1);
   P_NVA0C0_OFFSET_OUT_UPPER(p, dst_addr >> 32);
   P_NVA0C0_OFFSET_OUT(p, dst_addr);
   P_1INC(p, NVA0C0, LAUNCH_DMA);
   P_NVA0C0_LAUNCH_DMA(p, {
      .dst_memory_layout = DST_MEMORY_LAYOUT_PITCH,
      .completion_type = COMPLETION_TYPE_FLUSH_ONLY,
   });
   nv_push_update_count(p, DIV_ROUND_UP(size, 4));
   nvk_cmd_buffer_push_indirect(cmd, src_addr, ALIGN_POT(size, 4));
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdDispatchIndirect2KHR(VkCommandBuffer commandBuffer,
                            const VkDispatchIndirect2InfoKHR* pInfo)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   uint64_t dispatch_addr = pInfo->addressRange.address;

   /* We set these through the MME */
   uint32_t base_workgroup[3] = { 0, 0, 0 };
   uint32_t global_size[3] = { 0, 0, 0 };
   nvk_flush_compute_state(cmd, base_workgroup, global_size);

   uint64_t qmd_addr = 0, root_desc_addr = 0;
   VkResult result = nvk_cmd_flush_cs_qmd(cmd, &cmd->state, global_size,
                                          &qmd_addr, &root_desc_addr);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   /* First, only emit this if we have a compute shader invocations query */
   if (cmd->state.cs.active_compute_invocations_query ||
       cmd->state.inherited_pipeline_statistics &
          VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT) {

      /* We split the command buffer, first to load the indirect values and then
       * the local size is pushed on the new command buffer.
       * This is required for pre-Turing as we do not have enough registers to
       * have local_size come first.
       */
      struct nv_push *p = nvk_cmd_buffer_push(cmd, 1);
      if (nvk_cmd_buffer_compute_cls(cmd) >= AMPERE_COMPUTE_B)
         P_1INC(p, NVC7C0, CALL_MME_MACRO(NVK_MME_ADD_CS_INVOCATIONS));
      else
         P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_ADD_CS_INVOCATIONS));
      nv_push_update_count(p, 4);
      nvk_cmd_buffer_push_indirect(cmd, dispatch_addr,
                                   sizeof(VkDispatchIndirectCommand));

      const uint32_t local_size = nvk_compute_local_size(cmd);
      p = nvk_cmd_buffer_push(cmd, 1);
      nv_push_raw(p, &local_size, 1);
   }

   /* Then we update the root table with the group count */
   const uint64_t root_desc_group_count_addr =
      root_desc_addr +
      offsetof(struct nvk_root_descriptor_table, cs.group_count);
   nvk_cmd_compute_indirect_copy(cmd, root_desc_group_count_addr,
                                 dispatch_addr,
                                 sizeof(VkDispatchIndirectCommand));

   /* Now we are update the QMD */
   struct nak_qmd_dispatch_size_layout qmd_layout =
      nak_get_qmd_dispatch_size_layout(&pdev->nvkmd->dev_info);

   assert(qmd_layout.x_start % 32 == 0 &&
          qmd_layout.x_end == qmd_layout.x_start + 32);

   /* Sanity check that we have a granuality in byte at the very least */
   assert(qmd_layout.x_start % 8 == 0 &&
          qmd_layout.x_end % 8 == 0 &&
          qmd_layout.y_start % 8 == 0 &&
          qmd_layout.y_end % 8 == 0 &&
          qmd_layout.z_start % 8 == 0 &&
          qmd_layout.z_end % 8 == 0);

   if (qmd_layout.z_start == qmd_layout.y_start + 32) {
      /* In case the layout is aligned to 32-bit we can do it in one copy (Turing+) */
      nvk_cmd_compute_indirect_copy(cmd, qmd_addr + qmd_layout.x_start / 8,
                                    dispatch_addr,
                                    sizeof(VkDispatchIndirectCommand));
   } else {
      /* In case the layout is not aligned to 32-bit, do partial copies */
      nvk_cmd_compute_indirect_copy(
         cmd, qmd_addr + qmd_layout.x_start / 8,
         dispatch_addr + offsetof(struct VkDispatchIndirectCommand, x),
         sizeof(uint32_t));

      nvk_cmd_compute_indirect_copy(
         cmd, qmd_addr + qmd_layout.y_start / 8,
         dispatch_addr + offsetof(struct VkDispatchIndirectCommand, y),
         sizeof(uint16_t));

      nvk_cmd_compute_indirect_copy(
         cmd, qmd_addr + qmd_layout.z_start / 8,
         dispatch_addr + offsetof(struct VkDispatchIndirectCommand, z),
         sizeof(uint16_t));
   };

   /* Finally we schedule the QMD */
   struct nv_push *p = nvk_cmd_buffer_push(cmd, 4);
   P_MTHD(p, NVA0C0, SEND_PCAS_A);
   P_NVA0C0_SEND_PCAS_A(p, qmd_addr >> 8);
   if (nvk_cmd_buffer_compute_cls(cmd) <= TURING_COMPUTE_A) {
      P_IMMD(p, NVA0C0, SEND_SIGNALING_PCAS_B, {
            .invalidate = INVALIDATE_TRUE,
            .schedule = SCHEDULE_TRUE
      });
   } else {
      P_IMMD(p, NVC6C0, SEND_SIGNALING_PCAS2_B,
             PCAS_ACTION_INVALIDATE_COPY_SCHEDULE);
   }
}
