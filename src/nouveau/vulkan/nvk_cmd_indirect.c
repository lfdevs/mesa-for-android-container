/*
 * Copyright © 2024 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_cmd_buffer.h"
#include "nvk_device.h"
#include "nvk_entrypoints.h"
#include "nvk_indirect_execution_set.h"
#include "nvk_mme.h"
#include "nvk_physical_device.h"
#include "nvk_shader.h"

#include "nir_builder.h"
#include "vk_pipeline.h"

#include "clcb97.h"
#include "nv_push.h"
#include "nv_push_cl9097.h"
#include "nv_push_cl906f.h"
#include "nv_push_cla0c0.h"
#include "nv_push_clb1c0.h"
#include "nv_push_clc6c0.h"
#include "nv_push_clc7c0.h"
#include "nv_push_clc86f.h"

struct nvk_indirect_commands_layout {
   struct vk_object_base base;

   VkShaderStageFlags stages;

   /* Stages set by the first TOKEN_TYPE_EXECUTION_SET */
   VkShaderStageFlags set_stages;

   uint32_t cmd_seq_stride_B;
   uint32_t qmd_size_per_seq_B;

   struct nvk_shader *init;
   struct nvk_shader *process;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(nvk_indirect_commands_layout, base,
                               VkIndirectCommandsLayoutEXT,
                               VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_EXT);

struct process_cmd_push {
    uint64_t in_addr;
    uint64_t out_addr;
    uint64_t qmd_pool_addr;
    uint64_t count_addr;
    uint32_t max_seq_count;
    uint32_t ies_stride;
    uint64_t ies_addr;
    uint64_t root_addr;
};

struct process_cmd_in {
    nir_def *in_addr;
    nir_def *out_addr;
    nir_def *qmd_pool_addr;
    nir_def *count_addr;
    nir_def *max_seq_count;
    nir_def *ies_stride;
    nir_def *ies_addr;
    nir_def *root_addr;
};

static nir_def *
load_struct_var(nir_builder *b, nir_variable *var, uint32_t field)
{
   nir_deref_instr *deref =
      nir_build_deref_struct(b, nir_build_deref_var(b, var), field);
   return nir_load_deref(b, deref);
}

static struct process_cmd_in
load_process_cmd_in(nir_builder *b)
{
   struct glsl_struct_field push_fields[] = {
      { .type = glsl_uint64_t_type(), .name = "in_addr",       .offset = 0x00 },
      { .type = glsl_uint64_t_type(), .name = "out_addr",      .offset = 0x08 },
      { .type = glsl_uint64_t_type(), .name = "qmd_pool_addr", .offset = 0x10 },
      { .type = glsl_uint64_t_type(), .name = "count_addr",    .offset = 0x18 },
      { .type = glsl_uint_type(),     .name = "max_seq_count", .offset = 0x20 },
      { .type = glsl_uint_type(),     .name = "ies_stride",    .offset = 0x24 },
      { .type = glsl_uint64_t_type(), .name = "ies_addr",      .offset = 0x28 },
      { .type = glsl_uint64_t_type(), .name = "root_addr",     .offset = 0x30 },
   };
   const struct glsl_type *push_iface_type =
      glsl_interface_type(push_fields, ARRAY_SIZE(push_fields),
                          GLSL_INTERFACE_PACKING_STD140,
                          false /* row_major */, "push");
   nir_variable *push = nir_variable_create(b->shader, nir_var_mem_push_const,
                                            push_iface_type, "push");

   return (struct process_cmd_in) {
      .in_addr       = load_struct_var(b, push, 0),
      .out_addr      = load_struct_var(b, push, 1),
      .qmd_pool_addr = load_struct_var(b, push, 2),
      .count_addr    = load_struct_var(b, push, 3),
      .max_seq_count = load_struct_var(b, push, 4),
      .ies_stride    = load_struct_var(b, push, 5),
      .ies_addr      = load_struct_var(b, push, 6),
      .root_addr     = load_struct_var(b, push, 7),
   };
}

static nir_def *
build_exec_set_addr(nir_builder *b, struct process_cmd_in *in, nir_def *idx)
{
   nir_def *offset = nir_imul_2x32_64(b, in->ies_stride, idx);
   return nir_iadd(b, in->ies_addr, offset);
}

static nir_def *
load_global_dw(nir_builder *b, nir_def *addr, uint32_t offset_dw)
{
   return nir_load_global(b, 1, 32, nir_iadd_imm(b, addr, offset_dw * 4));
}

static void
store_global_dw(nir_builder *b, nir_def *addr, uint32_t offset_dw,
                nir_def *data)
{
   assert(data->bit_size == 32 && data->num_components == 1);
   nir_store_global(b, data, nir_iadd_imm(b, addr, offset_dw * 4));
}

static void
nir_iadd_to_var(nir_builder *b, nir_variable *x_var, nir_def *y)
{
   nir_def *x = nir_load_var(b, x_var);
   x = nir_iadd(b, x, y);
   nir_store_var(b, x_var, x, 0x1);
}

static void
nir_iadd_to_var_imm(nir_builder *b, nir_variable *x_var, uint64_t y)
{
   nir_def *x = nir_load_var(b, x_var);
   x = nir_iadd_imm(b, x, y);
   nir_store_var(b, x_var, x, 0x1);
}

struct nvk_nir_push {
   nir_variable *addr;
   nir_variable *dw_count;
   unsigned max_dw_count;
};

static void
nvk_nir_push_start(nir_builder *b, struct nvk_nir_push *p, nir_def *addr)
{
   p->addr = nir_local_variable_create(b->impl, glsl_uint64_t_type(),
                                       "nvk_nir_push::addr");
   nir_store_var(b, p->addr, addr, 0x1);
   p->dw_count = nir_local_variable_create(b->impl, glsl_uint_type(),
                                           "nvk_nir_push::dw_count");
   nir_store_var(b, p->dw_count, nir_imm_int(b, 0), 0x1);
   p->max_dw_count = 0;
}

static inline void
nvk_nir_push_dw(nir_builder *b, struct nvk_nir_push *p, nir_def *dw)
{
   store_global_dw(b, nir_load_var(b, p->addr), 0, dw);
   nir_iadd_to_var_imm(b, p->addr, 4);
   nir_iadd_to_var_imm(b, p->dw_count, 1);
   p->max_dw_count++;
}

static inline void
nvk_nir_push_copy_dws(nir_builder *b, struct nvk_nir_push *p,
                      nir_def *src_dw_addr_in, nir_def *dw_count,
                      uint32_t max_dw_count)
{
   nir_variable *i = nir_local_variable_create(b->impl, glsl_uint_type(), "i");
   nir_store_var(b, i, nir_imm_int(b, 0), 0x1);

   nir_variable *src_dw_addr =
      nir_local_variable_create(b->impl, glsl_uint64_t_type(), "src_dw_addr");
   nir_store_var(b, src_dw_addr, src_dw_addr_in, 0x1);

   nir_push_loop(b);
   {
      nir_break_if(b, nir_uge(b, nir_load_var(b, i), dw_count));

      nir_def *dw = load_global_dw(b, nir_load_var(b, src_dw_addr), 0);
      store_global_dw(b, nir_load_var(b, p->addr), 0, dw);

      nir_iadd_to_var_imm(b, i, 1);
      nir_iadd_to_var_imm(b, p->addr, 4);
      nir_iadd_to_var_imm(b, src_dw_addr, 4);
   }
   nir_pop_loop(b, NULL);

   nir_iadd_to_var(b, p->dw_count, dw_count);
   p->max_dw_count += max_dw_count;
}

static inline void
nvk_nir_build_p_1inc(nir_builder *b, struct nvk_nir_push *p,
                     int subc, int mthd, unsigned dw_count)
{
   uint32_t hdr = NVC0_FIFO_PKHDR_1I(subc, mthd, dw_count);
   nvk_nir_push_dw(b, p, nir_imm_int(b, hdr));
}
#define nvk_nir_P_1INC(b, p, class, mthd, size_dw) \
   nvk_nir_build_p_1inc((b), (p), SUBC_##class, class##_##mthd, (size_dw))

static void
nvk_nir_build_pad_NOP(nir_builder *b, struct nvk_nir_push *p, uint32_t nop)
{
   nir_push_loop(b);
   {
      nir_break_if(b, nir_uge_imm(b, nir_load_var(b, p->dw_count),
                                  p->max_dw_count));

      store_global_dw(b, nir_load_var(b, p->addr), 0, nir_imm_int(b, nop));
      nir_iadd_to_var_imm(b, p->addr, 4);
      nir_iadd_to_var_imm(b, p->dw_count, 1);
   }
   nir_pop_loop(b, NULL);
}
#define nvk_nir_pad_NOP(b, p, class) \
   nvk_nir_build_pad_NOP((b), (p), \
      NVC0_FIFO_PKHDR_IL(SUBC_##class, class##_NO_OPERATION, 0))

static void
copy_repl_global_dw(nir_builder *b, nir_def *dst_addr, nir_def *src_addr,
                    nir_def **repl_dw, uint32_t dw_count)
{
   for (uint32_t i = 0; i < dw_count; i++) {
      nir_def *dw;
      if (repl_dw[i] == NULL)
         dw = load_global_dw(b, src_addr, i);
      else
         dw = repl_dw[i];
      store_global_dw(b, dst_addr, i, dw);
   }
}

#define QMD_ALLOC_CHUNK_SIZE_B NAK_QMD_ALIGN_B

static_assert(NVK_DGC_ALIGN >= NAK_QMD_ALIGN_B,
              "QMD alignment requirement is a lower bound of DGC alignment");

static void
build_process_cs_cmd_seq(nir_builder *b, struct nvk_nir_push *p,
                         nir_def *in_addr, nir_def *seq_idx,
                         struct process_cmd_in *in,
                         const struct nvk_physical_device *pdev,
                         const VkIndirectCommandsLayoutCreateInfoEXT *info,
                         uint32_t *qmd_size_per_seq_B_out)
{
   /* If we don't have any indirect execution set, the currently bound shader
    * will be passed in there.
    */
   nir_def *shader_qmd_addr = in->ies_addr;

   nir_def *root_repl[sizeof(struct nvk_root_descriptor_table) / 4] = {};

#define root_dw(member) ( \
   assert(nvk_root_descriptor_offset(member) % 4 == 0), \
   nvk_root_descriptor_offset(member) / 4)

   root_repl[root_dw(cs.base_group[0])] = nir_imm_int(b, 0);
   root_repl[root_dw(cs.base_group[1])] = nir_imm_int(b, 0);
   root_repl[root_dw(cs.base_group[2])] = nir_imm_int(b, 0);

   *qmd_size_per_seq_B_out = 0;
   for (uint32_t t = 0; t < info->tokenCount; t++) {
      const VkIndirectCommandsLayoutTokenEXT *token = &info->pTokens[t];

      nir_def *token_addr = nir_iadd_imm(b, in_addr, token->offset);
      switch (token->type) {
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_EXECUTION_SET_EXT: {
         assert(token->data.pExecutionSet->shaderStages ==
                VK_SHADER_STAGE_COMPUTE_BIT);
         assert(t == 0);

         nir_def *idx = load_global_dw(b, token_addr, 0);
         shader_qmd_addr = build_exec_set_addr(b, in, idx);
         break;
      }

      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_EXT: {
         const VkPushConstantRange *pc_range =
            &token->data.pPushConstant->updateRange;

         assert(pc_range->offset % 4 == 0);
         assert(pc_range->size % 4 == 0);

         const uint32_t start_dw = root_dw(push) + (pc_range->offset / 4);
         for (uint32_t i = 0; i < pc_range->size / 4; i++)
            root_repl[start_dw + i] = load_global_dw(b, token_addr, i);
         break;
      }

      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_SEQUENCE_INDEX_EXT: {
         const VkPushConstantRange *pc_range =
            &token->data.pPushConstant->updateRange;

         assert(pc_range->offset % 4 == 0);
         assert(pc_range->size == 4);

         const uint32_t dw = root_dw(push) + (pc_range->offset / 4);
         root_repl[dw] = seq_idx;
         break;
      }

      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DISPATCH_EXT: {
         nir_def *disp_size_x = load_global_dw(b, token_addr, 0);
         nir_def *disp_size_y = load_global_dw(b, token_addr, 1);
         nir_def *disp_size_z = load_global_dw(b, token_addr, 2);

         /* We need space for both a QMD and a root table */
         const uint32_t qmd_size_B = nak_qmd_size_B(&pdev->info);
         const uint32_t root_offset_B = align(qmd_size_B, NAK_QMD_ALIGN_B);
         const uint32_t qmd_root_size_B =
            root_offset_B + sizeof(struct nvk_root_descriptor_table);

         /* The root table is already aligned */
         static_assert(sizeof(struct nvk_root_descriptor_table) %
                       NAK_QMD_ALIGN_B == 0,
                       "Root descriptor table size is not aligned");

         *qmd_size_per_seq_B_out += qmd_root_size_B;

         nir_push_if(b, nir_ior(b, nir_ior(b, nir_ine_imm(b, disp_size_x, 0),
                                              nir_ine_imm(b, disp_size_y, 0)),
                                   nir_ine_imm(b, disp_size_z, 0)));
         {
            /* The first dword in qmd_addr is an allocator in units of 256
             * bytes.
             */
            assert(qmd_root_size_B % QMD_ALLOC_CHUNK_SIZE_B == 0);
            nir_def *qmd_idx =
               nir_global_atomic(b, 32, in->qmd_pool_addr,
                                 nir_imm_int(b, qmd_root_size_B /
                                                QMD_ALLOC_CHUNK_SIZE_B),
                                 .atomic_op = nir_atomic_op_iadd);
            nir_def *qmd_offset =
               nir_imul_imm(b, nir_u2u64(b, qmd_idx), QMD_ALLOC_CHUNK_SIZE_B);
            nir_def *qmd_addr = nir_iadd(b, in->qmd_pool_addr, qmd_offset);
            nir_def *root_addr = nir_iadd_imm(b, qmd_addr, root_offset_B);

            /* Upload and patch the root descriptor table */
            root_repl[root_dw(cs.group_count[0])] = disp_size_x;
            root_repl[root_dw(cs.group_count[1])] = disp_size_y;
            root_repl[root_dw(cs.group_count[2])] = disp_size_z;
            copy_repl_global_dw(b, root_addr, in->root_addr,
                                root_repl, ARRAY_SIZE(root_repl));

            /* Upload and patch the QMD */
            const struct nak_qmd_dispatch_size_layout qmd_layout =
               nak_get_qmd_dispatch_size_layout(&pdev->info);
            assert(qmd_layout.x_start % 32 == 0);
            assert(qmd_layout.x_end == qmd_layout.x_start + 32);
            assert(qmd_layout.y_start == qmd_layout.x_start + 32);

            nir_def *qmd_repl[sizeof(struct nvk_ies_cs_qmd) / 4] = {};
            uint32_t qmd_repl_count = qmd_size_B / 4;
            assert(qmd_repl_count <= ARRAY_SIZE(qmd_repl));

            qmd_repl[qmd_layout.x_start / 32] = disp_size_x;

            if (qmd_layout.z_start == qmd_layout.y_start + 32) {
               qmd_repl[qmd_layout.y_start / 32] = disp_size_y;
               qmd_repl[qmd_layout.z_start / 32] = disp_size_z;
            } else {
               assert(qmd_layout.y_end == qmd_layout.y_start + 16);
               assert(qmd_layout.z_start == qmd_layout.x_start + 48);
               assert(qmd_layout.z_end == qmd_layout.z_start + 16);
               qmd_repl[qmd_layout.y_start / 32] =
                  nir_pack_32_2x16_split(b, nir_u2u16(b, disp_size_y),
                                            nir_u2u16(b, disp_size_z));
            }

            struct nak_qmd_cbuf_desc_layout cb0_layout =
               nak_get_qmd_cbuf_desc_layout(&pdev->info, 0);
            assert(cb0_layout.addr_lo_start % 32 == 0);
            assert(cb0_layout.addr_hi_start == cb0_layout.addr_lo_start + 32);
            const uint32_t cb0_addr_lo_dw = cb0_layout.addr_lo_start / 32;
            const uint32_t cb0_addr_hi_dw = cb0_layout.addr_hi_start / 32;
            nir_def *root_addr_shifted =
               nir_ushr_imm(b, root_addr, cb0_layout.addr_shift);
            qmd_repl[cb0_addr_lo_dw] =
               nir_unpack_64_2x32_split_x(b, root_addr_shifted);
            qmd_repl[cb0_addr_hi_dw] =
               nir_ior(b, load_global_dw(b, shader_qmd_addr, cb0_addr_hi_dw),
                          nir_unpack_64_2x32_split_y(b, root_addr_shifted));

            copy_repl_global_dw(b, qmd_addr, shader_qmd_addr,
                                qmd_repl, qmd_repl_count);

            /* Now emit commands */
            nir_def *invoc = nir_imul_2x32_64(b, disp_size_x, disp_size_y);
            invoc = nir_imul(b, invoc, nir_u2u64(b, disp_size_z));
            if (pdev->info.cls_compute >= AMPERE_COMPUTE_B)
               nvk_nir_P_1INC(b, p, NVC7C0, CALL_MME_MACRO(NVK_MME_ADD_CS_INVOCATIONS), 2);
            else
               nvk_nir_P_1INC(b, p, NV9097, CALL_MME_MACRO(NVK_MME_ADD_CS_INVOCATIONS), 2);
            nvk_nir_push_dw(b, p, nir_unpack_64_2x32_split_y(b, invoc));
            nvk_nir_push_dw(b, p, nir_unpack_64_2x32_split_x(b, invoc));

            nvk_nir_P_1INC(b, p, NVA0C0, SEND_PCAS_A, 1);
            nvk_nir_push_dw(b, p, nir_u2u32(b, nir_ushr_imm(b, qmd_addr, 8)));

            if (pdev->info.cls_compute >= AMPERE_COMPUTE_A) {
               uint32_t signal;
               V_NVC6C0_SEND_SIGNALING_PCAS2_B(signal,
                  PCAS_ACTION_INVALIDATE_COPY_SCHEDULE);
               nvk_nir_P_1INC(b, p, NVC6C0, SEND_SIGNALING_PCAS2_B, 1);
               nvk_nir_push_dw(b, p, nir_imm_int(b, signal));
            } else {
               uint32_t signal;
               V_NVA0C0_SEND_SIGNALING_PCAS_B(signal, {
                  .invalidate = INVALIDATE_TRUE,
                  .schedule = SCHEDULE_TRUE
               });
               nvk_nir_P_1INC(b, p, NVA0C0, SEND_SIGNALING_PCAS_B, 1);
               nvk_nir_push_dw(b, p, nir_imm_int(b, signal));
            }
         }
         nir_pop_if(b, NULL);
         break;
      }

      default:
         UNREACHABLE("Unsupported indirect token type");
      }
   }
}

/*
 * Graphics
 */

static void
build_gfx_set_exec(nir_builder *b, struct nvk_nir_push *p, nir_def *token_addr,
                   struct process_cmd_in *in,
                   const struct nvk_physical_device *pdev,
                   const VkIndirectCommandsExecutionSetTokenEXT *token)
{
   switch (token->type) {
   case VK_INDIRECT_EXECUTION_SET_INFO_TYPE_PIPELINES_EXT: {
      nir_def *idx = load_global_dw(b, token_addr, 0);
      nir_def *push_addr = build_exec_set_addr(b, in, idx);
      nir_def *dw_count = load_global_dw(b, push_addr, 0);
      const uint16_t max_dw_count =
         nvk_ies_gfx_pipeline_max_dw_count(pdev, token->shaderStages);
      nvk_nir_push_copy_dws(b, p, nir_iadd_imm(b, push_addr, 4),
                            dw_count, max_dw_count);
      break;
   }

   case VK_INDIRECT_EXECUTION_SET_INFO_TYPE_SHADER_OBJECTS_EXT: {
      int32_t i = 0;
      mesa_shader_stage type_stage[6] = {};
      nir_def *type_shader_idx[6] = {};
      mesa_shader_stage last_vtgm = MESA_SHADER_VERTEX;
      u_foreach_bit(s, token->shaderStages) {
         mesa_shader_stage stage = vk_to_mesa_shader_stage(1 << s);

         if (stage != MESA_SHADER_FRAGMENT)
            last_vtgm = stage;

         uint32_t type = mesa_to_nv9097_shader_type(stage);
         type_stage[type] = stage;
         type_shader_idx[type] = load_global_dw(b, token_addr, i++);
      }

      for (uint32_t type = 0; type < 6; type++) {
         nir_def *shader_idx = type_shader_idx[type];
         if (shader_idx == NULL)
            continue;

         bool is_last_vtgm = type_stage[type] == last_vtgm;

         nir_def *push_addr = build_exec_set_addr(b, in, shader_idx);
         nir_def *hdr = load_global_dw(b, push_addr, 0);
         nir_def *dw_count =
            nir_extract_u16(b, hdr, nir_imm_int(b, is_last_vtgm));
         const uint16_t max_dw_count =
            nvk_ies_gfx_shader_max_dw_count(pdev, token->shaderStages,
                                            is_last_vtgm);
         nvk_nir_push_copy_dws(b, p, nir_iadd_imm(b, push_addr, 4),
                               dw_count, max_dw_count);
      }
      break;
   }

   default:
      UNREACHABLE("Unknown indirect execution set type");
   }
}

static void
build_push_gfx_const(nir_builder *b, struct nvk_nir_push *p, nir_def *token_addr,
                     const VkIndirectCommandsPushConstantTokenEXT *token)
{
   const VkPushConstantRange *pc_range = &token->updateRange;

   // TODO: Compute
   assert(!(pc_range->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT));

   assert(pc_range->offset % 4 == 0);
   assert(pc_range->size % 4 == 0);
   const uint32_t dw_count = pc_range->size / 4;

   nvk_nir_P_1INC(b, p, NV9097, LOAD_CONSTANT_BUFFER_OFFSET, 1 + dw_count);
   nvk_nir_push_dw(b, p, nir_imm_int(b,
      nvk_root_descriptor_offset(push) + pc_range->offset));
   for (uint32_t i = 0; i < dw_count; i++)
      nvk_nir_push_dw(b, p, load_global_dw(b, token_addr, i));
}

static void
build_push_gfx_seq_idx(nir_builder *b, struct nvk_nir_push *p,
                       nir_def *token_addr, nir_def *seq_idx,
                       const VkIndirectCommandsPushConstantTokenEXT *token)
{
   const VkPushConstantRange *pc_range = &token->updateRange;

   // TODO: Compute
   assert(!(pc_range->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT));

   assert(pc_range->offset % 4 == 0);
   assert(pc_range->size == 4);
   nvk_nir_P_1INC(b, p, NV9097, LOAD_CONSTANT_BUFFER_OFFSET, 2);
   nvk_nir_push_dw(b, p, nir_imm_int(b,
      nvk_root_descriptor_offset(push) + pc_range->offset));
   nvk_nir_push_dw(b, p, seq_idx);
}

static void
build_set_ib(nir_builder *b, struct nvk_nir_push *p, nir_def *token_addr,
             UNUSED const VkIndirectCommandsIndexBufferTokenEXT *token)
{
   nir_def *addr_lo = load_global_dw(b, token_addr, 0);
   nir_def *addr_hi = load_global_dw(b, token_addr, 1);
   nir_def *size_B  = load_global_dw(b, token_addr, 2);
   nir_def *idx_fmt = load_global_dw(b, token_addr, 3);

   nvk_nir_P_1INC(b, p, NV9097, CALL_MME_MACRO(NVK_MME_BIND_IB), 4);
   nvk_nir_push_dw(b, p, addr_hi);
   nvk_nir_push_dw(b, p, addr_lo);
   nvk_nir_push_dw(b, p, size_B);
   nvk_nir_push_dw(b, p, idx_fmt);
}

static nir_def *
nvk_nir_vb_stride(nir_builder *b, nir_def *vb_idx, nir_def *stride)
{
   return nir_pack_32_2x16_split(b, nir_u2u16(b, stride),
                                    nir_u2u16(b, vb_idx));
}

static void
build_set_vb(nir_builder *b, struct nvk_nir_push *p, nir_def *token_addr,
             const VkIndirectCommandsVertexBufferTokenEXT *token)
{
   nir_def *vb_idx = nir_imm_int(b, token->vertexBindingUnit);
   nir_def *addr_lo  = load_global_dw(b, token_addr, 0);
   nir_def *addr_hi  = load_global_dw(b, token_addr, 1);
   nir_def *size_B   = load_global_dw(b, token_addr, 2);
   nir_def *stride_B = load_global_dw(b, token_addr, 3);

   nvk_nir_P_1INC(b, p, NV9097, CALL_MME_MACRO(NVK_MME_BIND_VB), 4);
   nvk_nir_push_dw(b, p, vb_idx);
   nvk_nir_push_dw(b, p, addr_hi);
   nvk_nir_push_dw(b, p, addr_lo);
   nvk_nir_push_dw(b, p, size_B);

   nvk_nir_P_1INC(b, p, NV9097, CALL_MME_MACRO(NVK_MME_SET_VB_STRIDE), 1);
   nvk_nir_push_dw(b, p, nvk_nir_vb_stride(b, vb_idx, stride_B));
}

static void
build_draw(nir_builder *b, struct nvk_nir_push *p, nir_def *token_addr)
{
   nir_def *vertex_count   = load_global_dw(b, token_addr, 0);
   nir_def *instance_count = load_global_dw(b, token_addr, 1);
   nir_def *first_vertex   = load_global_dw(b, token_addr, 2);
   nir_def *first_instance = load_global_dw(b, token_addr, 3);

   nvk_nir_P_1INC(b, p, NV9097, CALL_MME_MACRO(NVK_MME_DRAW), 5);
   nvk_nir_push_dw(b, p, nir_imm_int(b, 0)); /* draw_index */
   nvk_nir_push_dw(b, p, vertex_count);
   nvk_nir_push_dw(b, p, instance_count);
   nvk_nir_push_dw(b, p, first_vertex);
   nvk_nir_push_dw(b, p, first_instance);
}

static void
build_draw_indexed(nir_builder *b, struct nvk_nir_push *p, nir_def *token_addr)
{
   nir_def *index_count    = load_global_dw(b, token_addr, 0);
   nir_def *instance_count = load_global_dw(b, token_addr, 1);
   nir_def *first_index    = load_global_dw(b, token_addr, 2);
   nir_def *vertex_offset  = load_global_dw(b, token_addr, 3);
   nir_def *first_instance = load_global_dw(b, token_addr, 4);

   nvk_nir_P_1INC(b, p, NV9097, CALL_MME_MACRO(NVK_MME_DRAW_INDEXED), 6);
   nvk_nir_push_dw(b, p, nir_imm_int(b, 0)); /* draw_index */
   nvk_nir_push_dw(b, p, index_count);
   nvk_nir_push_dw(b, p, instance_count);
   nvk_nir_push_dw(b, p, first_index);
   nvk_nir_push_dw(b, p, vertex_offset);
   nvk_nir_push_dw(b, p, first_instance);
}

static void
build_draw_count(nir_builder *b, struct nvk_nir_push *p, nir_def *token_addr)
{
   nir_def *addr_lo = load_global_dw(b, token_addr, 0);
   nir_def *addr_hi = load_global_dw(b, token_addr, 1);
   nir_def *stride  = load_global_dw(b, token_addr, 2);
   nir_def *count   = load_global_dw(b, token_addr, 3);

   nvk_nir_P_1INC(b, p, NV9097, CALL_MME_MACRO(NVK_MME_DRAW_INDIRECT), 4);
   nvk_nir_push_dw(b, p, addr_hi);
   nvk_nir_push_dw(b, p, addr_lo);
   nvk_nir_push_dw(b, p, count);
   nvk_nir_push_dw(b, p, stride);
}

static void
build_draw_indexed_count(nir_builder *b, struct nvk_nir_push *p,
                         nir_def *token_addr)
{
   nir_def *addr_lo = load_global_dw(b, token_addr, 0);
   nir_def *addr_hi = load_global_dw(b, token_addr, 1);
   nir_def *stride  = load_global_dw(b, token_addr, 2);
   nir_def *count   = load_global_dw(b, token_addr, 3);

   nvk_nir_P_1INC(b, p, NV9097, CALL_MME_MACRO(NVK_MME_DRAW_INDEXED_INDIRECT), 4);
   nvk_nir_push_dw(b, p, addr_hi);
   nvk_nir_push_dw(b, p, addr_lo);
   nvk_nir_push_dw(b, p, count);
   nvk_nir_push_dw(b, p, stride);
}

static void
build_process_gfx_cmd_seq(nir_builder *b, struct nvk_nir_push *p,
                          nir_def *in_addr, nir_def *seq_idx,
                          struct process_cmd_in *in,
                          const struct nvk_physical_device *pdev,
                          const VkIndirectCommandsLayoutCreateInfoEXT *info)
{
   for (uint32_t t = 0; t < info->tokenCount; t++) {
      const VkIndirectCommandsLayoutTokenEXT *token = &info->pTokens[t];

      nir_def *token_addr = nir_iadd_imm(b, in_addr, token->offset);
      switch (token->type) {
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_EXECUTION_SET_EXT:
         assert(t == 0);
         build_gfx_set_exec(b, p, token_addr, in, pdev,
                            token->data.pExecutionSet);
         break;

      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_EXT:
         build_push_gfx_const(b, p, token_addr, token->data.pPushConstant);
         break;

      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_SEQUENCE_INDEX_EXT:
         build_push_gfx_seq_idx(b, p, token_addr, seq_idx,
                                token->data.pPushConstant);
         break;

      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_INDEX_BUFFER_EXT:
         build_set_ib(b, p, token_addr, token->data.pIndexBuffer);
         break;

      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_EXT:
         build_set_vb(b, p, token_addr, token->data.pVertexBuffer);
         break;

      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_EXT:
         build_draw_indexed(b, p, token_addr);
         break;

      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_EXT:
         build_draw(b, p, token_addr);
         break;

      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_COUNT_EXT:
         build_draw_indexed_count(b, p, token_addr);
         break;

      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_COUNT_EXT:
         build_draw_count(b, p, token_addr);
         break;

      default:
         UNREACHABLE("Unsupported indirect token type");
      }
   }
}

static VkResult
build_init_shader(struct nvk_device *dev,
                  const VkIndirectCommandsLayoutCreateInfoEXT *info,
                  uint32_t qmd_size_per_seq_B,
                  const VkAllocationCallbacks *pAllocator,
                  struct nvk_shader **shader_out)
{
   /* There's nothing to initialize for graphics */
   if (info->shaderStages & NVK_SHADER_STAGE_GRAPHICS_BITS) {
      assert(!(info->shaderStages & ~NVK_SHADER_STAGE_GRAPHICS_BITS));
      *shader_out = NULL;
      return VK_SUCCESS;
   }

   if (qmd_size_per_seq_B == 0) {
      *shader_out = NULL;
      return VK_SUCCESS;
   }

   nir_builder build =
      nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, NULL,
                                     "nvk-init-indirect-commands");
   build.shader->info.workgroup_size[0] = 32;
   nir_builder *b = &build;

   struct process_cmd_in in = load_process_cmd_in(b);

   if (qmd_size_per_seq_B > 0) {
      /* Initialize the QMD allocator to 1 * QMD_ALLOC_CHUNK_SIZE_B so that
       * the QMDs we allocate don't stomp the allocator.
       */
      assert(info->shaderStages == VK_SHADER_STAGE_COMPUTE_BIT);
      store_global_dw(b, in.qmd_pool_addr, 0, nir_imm_int(b, 1));
   }

   return nvk_compile_nir_shader(dev, build.shader, pAllocator, shader_out);
}

static VkResult
build_process_shader(struct nvk_device *dev,
                     const VkIndirectCommandsLayoutCreateInfoEXT *info,
                     const VkAllocationCallbacks *pAllocator,
                     struct nvk_shader **shader_out,
                     uint32_t *cmd_seq_stride_B_out,
                     uint32_t *qmd_size_per_seq_B_out)
{
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   nir_builder build =
      nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, NULL,
                                     "nvk-process-indirect-commands");
   build.shader->info.workgroup_size[0] = 32;
   nir_builder *b = &build;

   struct process_cmd_in in = load_process_cmd_in(b);

   nir_def *seq_idx = nir_channel(b, nir_load_global_invocation_id(b, 32), 0);

   /* We always execute a 32-wide shader and nothing guarantees that
    * max_seq_count is a multiple of 32 so we need to bail if our index is
    * above the maximum.  If we're inside the maximum but less than the count,
    * we setill need to emit a bunch of NOP.
    */
   nir_push_if(b, nir_uge(b, seq_idx, in.max_seq_count));
   {
      nir_jump(b, nir_jump_halt);
   }
   nir_pop_if(b, NULL);

   nir_def *ind_count;
   nir_push_if(b, nir_ine_imm(b, in.count_addr, 0));
   {
      ind_count = load_global_dw(b, in.count_addr, 0);
      ind_count = nir_umin(b, ind_count, in.max_seq_count);
   }
   nir_pop_if(b, NULL);
   nir_def *count = nir_if_phi(b, ind_count, in.max_seq_count);

   nir_def *in_seq_addr = nir_iadd(b, in.in_addr,
      nir_imul_imm(b, nir_u2u64(b, seq_idx), info->indirectStride));
   /* We'll replace this later once we know what it is */
   nir_def *out_stride = nir_imm_int(b, 0xc0ffee0);
   nir_def *out_seq_addr = nir_iadd(b, in.out_addr,
      nir_imul_2x32_64(b, seq_idx, out_stride));

   struct nvk_nir_push push = {};
   nvk_nir_push_start(b, &push, out_seq_addr);

   nir_push_if(b, nir_ult(b, seq_idx, count));
   {
      if (info->shaderStages & VK_SHADER_STAGE_COMPUTE_BIT) {
         assert(info->shaderStages == VK_SHADER_STAGE_COMPUTE_BIT);
         build_process_cs_cmd_seq(b, &push, in_seq_addr, seq_idx,
                                  &in, pdev, info, qmd_size_per_seq_B_out);
      } else if (info->shaderStages & NVK_SHADER_STAGE_GRAPHICS_BITS) {
         assert(!(info->shaderStages & ~NVK_SHADER_STAGE_GRAPHICS_BITS));
         *qmd_size_per_seq_B_out = 0;
         build_process_gfx_cmd_seq(b, &push, in_seq_addr, seq_idx,
                                   &in, pdev, info);
      } else {
         UNREACHABLE("Unknown shader stage");
      }
   }
   nir_pop_if(b, NULL);

   /* Always pad the command buffer.  In the case where seq_idx >= count, the
    * entire sequence will be NO_OPERATION.
    */
   if (info->shaderStages & VK_SHADER_STAGE_COMPUTE_BIT) {
      nvk_nir_pad_NOP(b, &push, NVA0C0);
   } else if (info->shaderStages & NVK_SHADER_STAGE_GRAPHICS_BITS) {
      nvk_nir_pad_NOP(b, &push, NV9097);
   } else {
      UNREACHABLE("Unknown shader stage");
   }

   /* Replace the out stride with the actual size of a command stream */
   nir_load_const_instr *out_stride_const =
      nir_def_as_load_const(out_stride);
   out_stride_const->value[0].u32 = push.max_dw_count * 4;

   /* We also output this stride to go in the layout struct */
   *cmd_seq_stride_B_out = push.max_dw_count * 4;

   return nvk_compile_nir_shader(dev, build.shader, pAllocator, shader_out);
}

static void
nvk_indirect_commands_layout_destroy(struct nvk_device *dev,
                                     struct nvk_indirect_commands_layout *layout,
                                     const VkAllocationCallbacks *alloc)
{
   if (layout->init != NULL)
      vk_shader_destroy(&dev->vk, &layout->init->vk, alloc);
   if (layout->process != NULL)
      vk_shader_destroy(&dev->vk, &layout->process->vk, alloc);
   vk_object_free(&dev->vk, alloc, layout);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_CreateIndirectCommandsLayoutEXT(
    VkDevice _device,
    const VkIndirectCommandsLayoutCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkIndirectCommandsLayoutEXT *pIndirectCommandsLayout)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);
   VkResult result;

   struct nvk_indirect_commands_layout *layout =
      vk_object_zalloc(&dev->vk, pAllocator, sizeof(*layout),
                       VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_EXT);
   if (layout == NULL)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   layout->stages = pCreateInfo->shaderStages;

   /* From the Vulkan 1.3.XXX spec:
    *
    *    VUID-VkIndirectCommandsLayoutCreateInfoEXT-pTokens-11093
    *
    *    "The number of tokens in the pTokens array with type equal to
    *    VK_INDIRECT_COMMANDS_TOKEN_TYPE_EXECUTION_SET_EXT must be less than
    *    or equal to 1"
    *
    * and
    *
    *    VUID-VkIndirectCommandsLayoutCreateInfoEXT-pTokens-11139
    *
    *    "If the pTokens array contains a
    *    VK_INDIRECT_COMMANDS_TOKEN_TYPE_EXECUTION_SET_EXT token, then this
    *    token must be the first token in the array"
    */
   if (pCreateInfo->tokenCount > 0 &&
       pCreateInfo->pTokens[0].type ==
         VK_INDIRECT_COMMANDS_TOKEN_TYPE_EXECUTION_SET_EXT) {
      const VkIndirectCommandsExecutionSetTokenEXT *token =
         pCreateInfo->pTokens[0].data.pExecutionSet;

      /* Pipelines should never mismatch here. */
      if (token->type == VK_INDIRECT_EXECUTION_SET_INFO_TYPE_PIPELINES_EXT)
         assert(token->shaderStages == pCreateInfo->shaderStages);

      layout->set_stages = token->shaderStages;
   }

   result = build_process_shader(dev, pCreateInfo, pAllocator,
                                 &layout->process, &layout->cmd_seq_stride_B,
                                 &layout->qmd_size_per_seq_B);
   if (result != VK_SUCCESS) {
      nvk_indirect_commands_layout_destroy(dev, layout, pAllocator);
      return result;
   }

   if (layout->cmd_seq_stride_B > (NV_PUSH_MAX_COUNT * 4)) {
      nvk_indirect_commands_layout_destroy(dev, layout, pAllocator);
      return vk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                       "Too many tokens in IndirectCommandsLayout");
   }

   result = build_init_shader(dev, pCreateInfo, layout->qmd_size_per_seq_B,
                              pAllocator, &layout->init);
   if (result != VK_SUCCESS) {
      nvk_indirect_commands_layout_destroy(dev, layout, pAllocator);
      return result;
   }

   *pIndirectCommandsLayout = nvk_indirect_commands_layout_to_handle(layout);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvk_DestroyIndirectCommandsLayoutEXT(
    VkDevice _device,
    VkIndirectCommandsLayoutEXT indirectCommandsLayout,
    const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);
   VK_FROM_HANDLE(nvk_indirect_commands_layout, layout,
                  indirectCommandsLayout);

   if (layout == NULL)
      return;

   nvk_indirect_commands_layout_destroy(dev, layout, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL
nvk_GetGeneratedCommandsMemoryRequirementsEXT(
    VkDevice _device,
    const VkGeneratedCommandsMemoryRequirementsInfoEXT *pInfo,
    VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);
   VK_FROM_HANDLE(nvk_indirect_commands_layout, layout,
                  pInfo->indirectCommandsLayout);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   uint64_t size = layout->cmd_seq_stride_B * (uint64_t)pInfo->maxSequenceCount;
   if (layout->qmd_size_per_seq_B > 0) {
      size = align64(size, NAK_QMD_ALIGN_B);
      size += QMD_ALLOC_CHUNK_SIZE_B; /* One for the allocator */
      size += layout->qmd_size_per_seq_B * pInfo->maxSequenceCount;
   }

   pMemoryRequirements->memoryRequirements = (VkMemoryRequirements) {
      .size = size,
      .alignment = NAK_QMD_ALIGN_B,
      .memoryTypeBits = BITFIELD_MASK(pdev->mem_type_count),
   };
}

static void
nvk_cmd_process_cmds(struct nvk_cmd_buffer *cmd,
                     const VkGeneratedCommandsInfoEXT *info,
                     const struct nvk_cmd_state *state)
{
   VK_FROM_HANDLE(nvk_indirect_execution_set, ies, info->indirectExecutionSet);
   VK_FROM_HANDLE(nvk_indirect_commands_layout, layout,
                  info->indirectCommandsLayout);

   struct process_cmd_push push = {
      .in_addr       = info->indirectAddress,
      .out_addr      = info->preprocessAddress,
      .count_addr    = info->sequenceCountAddress,
      .max_seq_count = info->maxSequenceCount,
   };

   uint64_t qmd_addr = 0;
   if (layout->stages & VK_SHADER_STAGE_COMPUTE_BIT) {
      uint32_t global_size[3] = { 0, 0, 0 };
      VkResult result = nvk_cmd_flush_cs_qmd(cmd, state, global_size,
                                             &qmd_addr, &push.root_addr);
      if (unlikely(result != VK_SUCCESS)) {
         vk_command_buffer_set_error(&cmd->vk, result);
         return;
      }
   }

   if (layout->set_stages == 0) {
      push.ies_addr = qmd_addr;
   } else {
      assert(layout->set_stages == layout->stages);
      push.ies_addr   = ies->mem->va->addr;
      push.ies_stride = ies->stride_B;
   }

   if (layout->qmd_size_per_seq_B > 0) {
      assert(info->preprocessAddress % NAK_QMD_ALIGN_B == 0);
      uint64_t qmd_offset =
         layout->cmd_seq_stride_B * (uint64_t)info->maxSequenceCount;
      qmd_offset = align64(qmd_offset, NAK_QMD_ALIGN_B);
      push.qmd_pool_addr = info->preprocessAddress + qmd_offset;
   }

   if (layout->init != NULL) {
      nvk_cmd_dispatch_shader(cmd, layout->init, &push, sizeof(push), 1, 1, 1);

      struct nv_push *p = nvk_cmd_buffer_push(cmd, 2);
      P_IMMD(p, NVA0C0, WAIT_FOR_IDLE, 0);
   }

   nvk_cmd_dispatch_shader(cmd, layout->process, &push, sizeof(push),
                           DIV_ROUND_UP(info->maxSequenceCount, 32), 1, 1);
}

static void
nvk_cmd_flush_process_state(struct nvk_cmd_buffer *cmd,
                            const VkGeneratedCommandsInfoEXT *info)
{
   struct nvk_descriptor_state *desc =
      nvk_get_descriptor_state_for_stages(cmd, info->shaderStages);
   nvk_cmd_buffer_flush_push_descriptors(cmd, desc);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdPreprocessGeneratedCommandsEXT(VkCommandBuffer commandBuffer,
                                      const VkGeneratedCommandsInfoEXT *info,
                                      VkCommandBuffer stateCommandBuffer)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvk_cmd_buffer, state_cmd, stateCommandBuffer);

   nvk_cmd_flush_process_state(state_cmd, info);
   nvk_cmd_process_cmds(cmd, info, &state_cmd->state);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdExecuteGeneratedCommandsEXT(VkCommandBuffer commandBuffer,
                                   VkBool32 isPreprocessed,
                                   const VkGeneratedCommandsInfoEXT *info)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvk_indirect_commands_layout, layout,
                  info->indirectCommandsLayout);
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   if (!isPreprocessed) {
      nvk_cmd_flush_process_state(cmd, info);
      nvk_cmd_process_cmds(cmd, info, &cmd->state);

      struct nv_push *p = nvk_cmd_buffer_push(cmd, 6);
      P_IMMD(p, NVA0C0, INVALIDATE_SHADER_CACHES, {
         .data = DATA_TRUE,
         .constant = CONSTANT_TRUE,
         .flush_data = FLUSH_DATA_TRUE,
      });
      if (pdev->info.cls_eng3d >= MAXWELL_COMPUTE_B)
         P_IMMD(p, NVB1C0, INVALIDATE_SKED_CACHES, 0);
      if (pdev->info.cls_eng3d >= HOPPER_A)
         P_IMMD(p, NVC86F, WFI, 0);
      else
         __push_immd(p, SUBC_NV9097, NV906F_SET_REFERENCE, 0);
   }

   if (layout->stages & VK_SHADER_STAGE_COMPUTE_BIT) {
      assert(info->shaderStages == VK_SHADER_STAGE_COMPUTE_BIT);
      nvk_cmd_buffer_flush_push_descriptors(cmd, &cmd->state.cs.descriptors);
   } else if (layout->stages & NVK_SHADER_STAGE_GRAPHICS_BITS) {
      assert(!(layout->stages & ~NVK_SHADER_STAGE_GRAPHICS_BITS));

      nvk_cmd_buffer_flush_push_descriptors(cmd, &cmd->state.gfx.descriptors);
      nvk_cmd_flush_gfx_dynamic_state(cmd);

      if (layout->set_stages == 0) {
         /* In this case, we're using the CPU-bound shaders */
         nvk_cmd_flush_gfx_shaders(cmd);
         nvk_cmd_flush_gfx_cbufs(cmd);
      } else {
         /* From the Vulkan 1.3.XXX spec:
          *
          *    "If indirectCommandsLayout was created with a token sequence
          *    that contained the
          *    VK_INDIRECT_COMMANDS_TOKEN_TYPE_EXECUTION_SET_EXT token and
          *    indirectExecutionSet was created using
          *    VK_INDIRECT_EXECUTION_SET_INFO_TYPE_SHADER_OBJECTS_EXT, every
          *    executed VK_INDIRECT_COMMANDS_TOKEN_TYPE_EXECUTION_SET_EXT
          *    token must bind all the shader stages set in the
          *    VkIndirectCommandsExecutionSetTokenEXT::shaderStages used to
          *    create indirectCommandsLayout"
          *
          * So we unbind anything not explicitly bound by the layout and trust
          * the layout to bind the rest.
          */
         assert(layout->set_stages == layout->stages);

         uint8_t set_types = 0;
         u_foreach_bit(s, layout->set_stages) {
            mesa_shader_stage stage = vk_to_mesa_shader_stage(1 << s);
            uint32_t type = mesa_to_nv9097_shader_type(stage);
            set_types |= BITFIELD_BIT(type);
         }

         uint8_t unset_types = BITFIELD_MASK(6) & ~set_types;

         struct nv_push *p = nvk_cmd_buffer_push(cmd, 12);
         u_foreach_bit(type, unset_types) {
            P_IMMD(p, NV9097, SET_PIPELINE_SHADER(type), {
               .enable  = ENABLE_FALSE,
               .type    = type,
            });
         }
      }
   }

   ASSERTED const uint64_t size =
      layout->cmd_seq_stride_B * (uint64_t)info->maxSequenceCount;
   assert(size <= info->preprocessSize);

   uint64_t addr = info->preprocessAddress;
   uint64_t seq_count = info->maxSequenceCount;

   /* Break it into pices that are a multiple of cmd_seq_stride_B so that, if
    * the kernel inserts a sync point between two of our pushes, it doesn't
    * break a single command.
    */
   const uint32_t max_seq_per_push =
      (NV_PUSH_MAX_COUNT * 4) / layout->cmd_seq_stride_B;

   while (seq_count > 0) {
      uint32_t push_seq = MIN2(seq_count, max_seq_per_push);
      uint32_t push_size_B = push_seq * layout->cmd_seq_stride_B;
      nvk_cmd_buffer_push_indirect(cmd, addr, push_size_B);
      addr += push_size_B;
      seq_count -= push_seq;
   }

   if (layout->set_stages != 0) {
      if (layout->stages & NVK_SHADER_STAGE_GRAPHICS_BITS) {
         cmd->state.gfx.shaders_dirty |= NVK_SHADER_STAGE_GRAPHICS_BITS;
      }
   }
}
