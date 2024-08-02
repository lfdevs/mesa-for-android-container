/*
 * Copyright © 2021 Google
 *
 * SPDX-License-Identifier: MIT
 */

#include "radv_device_generated_commands.h"
#include "meta/radv_meta.h"
#include "radv_entrypoints.h"

#include "ac_rgp.h"

#include "nir_builder.h"

#include "vk_common_entrypoints.h"
#include "vk_shader_module.h"

static void
radv_get_sequence_size_compute(const struct radv_indirect_command_layout *layout,
                               const struct radv_compute_pipeline *pipeline, uint32_t *cmd_size, uint32_t *upload_size)
{
   const struct radv_device *device = container_of(layout->base.device, struct radv_device, vk);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   /* dispatch */
   *cmd_size += 5 * 4;

   if (pipeline) {
      struct radv_shader *cs = radv_get_shader(pipeline->base.shaders, MESA_SHADER_COMPUTE);
      const struct radv_userdata_info *loc = radv_get_user_sgpr_info(cs, AC_UD_CS_GRID_SIZE);
      if (loc->sgpr_idx != -1) {
         if (device->load_grid_size_from_user_sgpr) {
            /* PKT3_SET_SH_REG for immediate values */
            *cmd_size += 5 * 4;
         } else {
            /* PKT3_SET_SH_REG for pointer */
            *cmd_size += 4 * 4;
         }
      }
   } else {
      /* COMPUTE_PGM_{LO,RSRC1,RSRC2} */
      *cmd_size += 7 * 4;

      if (pdev->info.gfx_level >= GFX10) {
         /* COMPUTE_PGM_RSRC3 */
         *cmd_size += 3 * 4;
      }

      /* COMPUTE_{RESOURCE_LIMITS,NUM_THREADS_X} */
      *cmd_size += 8 * 4;

      /* Assume the compute shader needs grid size because we can't know the information for
       * indirect pipelines.
       */
      if (device->load_grid_size_from_user_sgpr) {
         /* PKT3_SET_SH_REG for immediate values */
         *cmd_size += 5 * 4;
      } else {
         /* PKT3_SET_SH_REG for pointer */
         *cmd_size += 4 * 4;
      }

      /* PKT3_SET_SH_REG for indirect descriptor sets pointer */
      *cmd_size += 3 * 4;

      /* Reserve space for indirect pipelines because they might use indirect descriptor sets. */
      *upload_size += MAX_SETS * 4;
   }

   if (device->sqtt.bo) {
      /* sqtt markers */
      *cmd_size += 8 * 3 * 4;
   }
}

static void
radv_get_sequence_size_graphics(const struct radv_indirect_command_layout *layout,
                                const struct radv_graphics_pipeline *pipeline, uint32_t *cmd_size,
                                uint32_t *ace_cmd_size, uint32_t *upload_size)
{
   const struct radv_device *device = container_of(layout->base.device, struct radv_device, vk);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_shader *vs = radv_get_shader(pipeline->base.shaders, MESA_SHADER_VERTEX);

   if (layout->bind_vbo_mask) {
      *upload_size += 16 * util_bitcount(vs->info.vs.vb_desc_usage_mask);

      /* One PKT3_SET_SH_REG for emitting VBO pointer (32-bit) */
      *cmd_size += 3 * 4;
   }

   if (layout->binds_index_buffer) {
      /* Index type write (normal reg write) + index buffer base write (64-bits, but special packet
       * so only 1 word overhead) + index buffer size (again, special packet so only 1 word
       * overhead)
       */
      *cmd_size += (3 + 3 + 2) * 4;
   }

   if (layout->indexed) {
      if (layout->binds_index_buffer) {
         /* userdata writes + instance count + indexed draw */
         *cmd_size += (5 + 2 + 5) * 4;
      } else {
         /* PKT3_SET_BASE + PKT3_DRAW_{INDEX}_INDIRECT_MULTI */
         *cmd_size += (4 + (pipeline->uses_drawid ? 10 : 5)) * 4;
      }
   } else {
      if (layout->draw_mesh_tasks) {
         const struct radv_shader *task_shader = radv_get_shader(pipeline->base.shaders, MESA_SHADER_TASK);

         if (task_shader) {
            const struct radv_userdata_info *xyz_loc = radv_get_user_sgpr_info(task_shader, AC_UD_CS_GRID_SIZE);
            const struct radv_userdata_info *draw_id_loc = radv_get_user_sgpr_info(task_shader, AC_UD_CS_TASK_DRAW_ID);

            /* PKT3_DISPATCH_TASKMESH_GFX */
            *cmd_size += 4 * 4;

            if (xyz_loc->sgpr_idx != -1)
               *ace_cmd_size += 5 * 4;
            if (draw_id_loc->sgpr_idx != -1)
               *ace_cmd_size += 3 * 4;

            /* PKT3_DISPATCH_TASKMESH_DIRECT_ACE */
            *ace_cmd_size += 6 * 4;
         } else {
            /* userdata writes + instance count + non-indexed draw */
            *cmd_size += (6 + 2 + (pdev->mesh_fast_launch_2 ? 5 : 3)) * 4;
         }
      } else {
         /* userdata writes + instance count + non-indexed draw */
         *cmd_size += (5 + 2 + 3) * 4;
      }
   }

   if (device->sqtt.bo) {
      /* sqtt markers */
      *cmd_size += 5 * 3 * 4;
   }
}

static void
radv_get_sequence_size(const struct radv_indirect_command_layout *layout, struct radv_pipeline *pipeline,
                       uint32_t *cmd_size, uint32_t *ace_cmd_size, uint32_t *upload_size)
{
   const struct radv_device *device = container_of(layout->base.device, struct radv_device, vk);

   *cmd_size = 0;
   *ace_cmd_size = 0;
   *upload_size = 0;

   if (layout->push_constant_mask) {
      bool need_copy = false;

      if (pipeline) {
         for (unsigned i = 0; i < ARRAY_SIZE(pipeline->shaders); ++i) {
            if (!pipeline->shaders[i])
               continue;

            struct radv_userdata_locations *locs = &pipeline->shaders[i]->info.user_sgprs_locs;
            if (locs->shader_data[AC_UD_PUSH_CONSTANTS].sgpr_idx >= 0) {
               /* One PKT3_SET_SH_REG for emitting push constants pointer (32-bit) */
               if (i == MESA_SHADER_TASK) {
                  *ace_cmd_size += 3 * 4;
               } else {
                  *cmd_size += 3 * 4;
               }
               need_copy = true;
            }
            if (locs->shader_data[AC_UD_INLINE_PUSH_CONSTANTS].sgpr_idx >= 0) {
               /* One PKT3_SET_SH_REG writing all inline push constants. */
               const uint32_t inline_pc_size = (3 * util_bitcount64(layout->push_constant_mask)) * 4;

               if (i == MESA_SHADER_TASK) {
                  *ace_cmd_size += inline_pc_size;
               } else {
                  *cmd_size += inline_pc_size;
               }
            }
         }
      } else {
         /* Assume the compute shader needs both user SGPRs because we can't know the information
          * for indirect pipelines.
          */
         assert(layout->pipeline_bind_point == VK_PIPELINE_BIND_POINT_COMPUTE);
         *cmd_size += 3 * 4;
         need_copy = true;

         *cmd_size += (3 * util_bitcount64(layout->push_constant_mask)) * 4;
      }

      if (need_copy) {
         *upload_size += align(layout->push_constant_size, 16);
      }
   }

   if (device->sqtt.bo) {
      /* THREAD_TRACE_MARKER */
      *cmd_size += 2 * 4;
   }

   if (layout->pipeline_bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
      struct radv_graphics_pipeline *graphics_pipeline = radv_pipeline_to_graphics(pipeline);
      radv_get_sequence_size_graphics(layout, graphics_pipeline, cmd_size, ace_cmd_size, upload_size);
   } else {
      assert(layout->pipeline_bind_point == VK_PIPELINE_BIND_POINT_COMPUTE);
      struct radv_compute_pipeline *compute_pipeline = pipeline ? radv_pipeline_to_compute(pipeline) : NULL;
      radv_get_sequence_size_compute(layout, compute_pipeline, cmd_size, upload_size);
   }
}

static uint32_t
radv_align_cmdbuf_size(const struct radv_device *device, uint32_t size, enum amd_ip_type ip_type)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const uint32_t ib_alignment = pdev->info.ip[ip_type].ib_alignment;

   return align(size, ib_alignment);
}

static unsigned
radv_dgc_preamble_cmdbuf_size(const struct radv_device *device, enum amd_ip_type ip_type)
{
   return radv_align_cmdbuf_size(device, 16, ip_type);
}

static bool
radv_dgc_use_preamble(const VkGeneratedCommandsInfoNV *cmd_info)
{
   /* Heuristic on when the overhead for the preamble (i.e. double jump) is worth it. Obviously
    * a bit of a guess as it depends on the actual count which we don't know. */
   return cmd_info->sequencesCountBuffer != VK_NULL_HANDLE && cmd_info->sequencesCount >= 64;
}

uint32_t
radv_get_indirect_cmdbuf_size(const VkGeneratedCommandsInfoNV *cmd_info)
{
   VK_FROM_HANDLE(radv_indirect_command_layout, layout, cmd_info->indirectCommandsLayout);
   VK_FROM_HANDLE(radv_pipeline, pipeline, cmd_info->pipeline);
   const struct radv_device *device = container_of(layout->base.device, struct radv_device, vk);

   if (radv_dgc_use_preamble(cmd_info))
      return radv_dgc_preamble_cmdbuf_size(device, AMD_IP_GFX);

   uint32_t cmd_size, ace_cmd_size, upload_size;
   radv_get_sequence_size(layout, pipeline, &cmd_size, &ace_cmd_size, &upload_size);
   return radv_align_cmdbuf_size(device, cmd_size * cmd_info->sequencesCount, AMD_IP_GFX);
}

uint32_t
radv_get_indirect_ace_cmdbuf_offset(const VkGeneratedCommandsInfoNV *cmd_info)
{
   VK_FROM_HANDLE(radv_indirect_command_layout, layout, cmd_info->indirectCommandsLayout);
   VK_FROM_HANDLE(radv_pipeline, pipeline, cmd_info->pipeline);
   const struct radv_device *device = container_of(layout->base.device, struct radv_device, vk);

   uint32_t cmd_size, ace_cmd_size, upload_size;
   radv_get_sequence_size(layout, pipeline, &cmd_size, &ace_cmd_size, &upload_size);

   uint32_t offset = radv_align_cmdbuf_size(device, cmd_size * cmd_info->sequencesCount, AMD_IP_GFX);

   if (radv_dgc_use_preamble(cmd_info))
      offset += radv_dgc_preamble_cmdbuf_size(device, AMD_IP_GFX);

   return offset;
}

uint32_t
radv_get_indirect_ace_cmdbuf_size(const VkGeneratedCommandsInfoNV *cmd_info)
{
   VK_FROM_HANDLE(radv_indirect_command_layout, layout, cmd_info->indirectCommandsLayout);
   VK_FROM_HANDLE(radv_pipeline, pipeline, cmd_info->pipeline);
   const struct radv_device *device = container_of(layout->base.device, struct radv_device, vk);

   if (radv_dgc_use_preamble(cmd_info))
      return radv_dgc_preamble_cmdbuf_size(device, AMD_IP_COMPUTE);

   uint32_t cmd_size, ace_cmd_size, upload_size;
   radv_get_sequence_size(layout, pipeline, &cmd_size, &ace_cmd_size, &upload_size);
   return radv_align_cmdbuf_size(device, ace_cmd_size * cmd_info->sequencesCount, AMD_IP_COMPUTE);
}

struct radv_dgc_params {
   uint32_t cmd_buf_main_offset;
   uint32_t cmd_buf_stride;
   uint32_t cmd_buf_size;
   uint32_t ace_cmd_buf_preamble_offset;
   uint32_t ace_cmd_buf_main_offset;
   uint32_t ace_cmd_buf_stride;
   uint32_t ace_cmd_buf_size;
   uint32_t upload_main_offset;
   uint32_t upload_stride;
   uint32_t upload_addr;
   uint32_t sequence_count;
   uint64_t sequence_count_addr;
   uint32_t stream_stride;
   uint64_t stream_addr;

   /* draw info */
   uint16_t draw_indexed;
   uint16_t draw_params_offset;
   uint16_t binds_index_buffer;
   uint16_t vtx_base_sgpr;
   uint32_t max_index_count;
   uint8_t draw_mesh_tasks;

   /* task/mesh info */
   uint8_t has_task_shader;
   uint16_t mesh_ring_entry_sgpr;
   uint8_t linear_dispatch_en;
   uint16_t task_ring_entry_sgpr;
   uint32_t dispatch_initiator_task;
   uint16_t task_xyz_sgpr;
   uint16_t task_draw_id_sgpr;

   /* dispatch info */
   uint32_t dispatch_initiator;
   uint16_t dispatch_params_offset;
   uint16_t grid_base_sgpr;

   /* bind index buffer info. Valid if binds_index_buffer == true && draw_indexed */
   uint16_t index_buffer_offset;

   uint8_t vbo_cnt;

   uint8_t const_copy;

   /* Which VBOs are set in this indirect layout. */
   uint32_t vbo_bind_mask;

   uint16_t vbo_reg;
   uint16_t const_copy_size;

   uint16_t push_constant_stages;
   uint64_t push_constant_mask;

   uint32_t ibo_type_32;
   uint32_t ibo_type_8;

   uint8_t is_dispatch;
   uint8_t use_preamble;

   /* For conditional rendering on ACE. */
   uint8_t predicating;
   uint8_t predication_type;
   uint64_t predication_va;

   uint8_t bind_pipeline;
   uint16_t pipeline_params_offset;

   /* For indirect descriptor sets */
   uint32_t indirect_desc_sets_va;
};

enum {
   DGC_USES_DRAWID = 1u << 14,
   DGC_USES_BASEINSTANCE = 1u << 15,
   DGC_USES_GRID_SIZE = DGC_USES_BASEINSTANCE, /* Mesh shader only */
};

enum {
   DGC_DYNAMIC_STRIDE = 1u << 15,
};

struct dgc_cmdbuf {
   const struct radv_device *dev;

   nir_builder *b;
   nir_def *va;
   nir_variable *offset;
};

static void
dgc_emit(struct dgc_cmdbuf *cs, unsigned count, nir_def **values)
{
   nir_builder *b = cs->b;

   for (unsigned i = 0; i < count; i += 4) {
      nir_def *offset = nir_load_var(b, cs->offset);
      nir_def *store_val = nir_vec(b, values + i, MIN2(count - i, 4));
      assert(store_val->bit_size >= 32);
      nir_build_store_global(b, store_val, nir_iadd(b, cs->va, nir_u2u64(b, offset)), .access = ACCESS_NON_READABLE);
      nir_store_var(b, cs->offset, nir_iadd_imm(b, offset, store_val->num_components * store_val->bit_size / 8), 0x1);
   }
}

#define load_param32(b, field)                                                                                         \
   nir_load_push_constant((b), 1, 32, nir_imm_int((b), 0), .base = offsetof(struct radv_dgc_params, field), .range = 4)

#define load_param16(b, field)                                                                                         \
   nir_ubfe_imm((b),                                                                                                   \
                nir_load_push_constant((b), 1, 32, nir_imm_int((b), 0),                                                \
                                       .base = (offsetof(struct radv_dgc_params, field) & ~3), .range = 4),            \
                (offsetof(struct radv_dgc_params, field) & 2) * 8, 16)

#define load_param8(b, field)                                                                                          \
   nir_ubfe_imm((b),                                                                                                   \
                nir_load_push_constant((b), 1, 32, nir_imm_int((b), 0),                                                \
                                       .base = (offsetof(struct radv_dgc_params, field) & ~3), .range = 4),            \
                (offsetof(struct radv_dgc_params, field) & 3) * 8, 8)

#define load_param64(b, field)                                                                                         \
   nir_pack_64_2x32((b), nir_load_push_constant((b), 2, 32, nir_imm_int((b), 0),                                       \
                                                .base = offsetof(struct radv_dgc_params, field), .range = 8))

/* Pipeline metadata */
static nir_def *
dgc_get_pipeline_va(nir_builder *b, nir_def *stream_addr)
{
   return nir_build_load_global(b, 1, 64,
                                nir_iadd(b, stream_addr, nir_u2u64(b, load_param16(b, pipeline_params_offset))),
                                .access = ACCESS_NON_WRITEABLE);
}

#define load_metadata32(b, field)                                                                                      \
   nir_load_global(                                                                                                    \
      b, nir_iadd(b, pipeline_va, nir_imm_int64(b, offsetof(struct radv_compute_pipeline_metadata, field))), 4, 1, 32)

#define load_metadata64(b, field)                                                                                      \
   nir_load_global(                                                                                                    \
      b, nir_iadd(b, pipeline_va, nir_imm_int64(b, offsetof(struct radv_compute_pipeline_metadata, field))), 4, 1, 64)

/* DGC cs emit macros */
#define dgc_cs_begin(cs)                                                                                               \
   struct dgc_cmdbuf *__cs = (cs);                                                                                     \
   nir_def *__dwords[32];                                                                                              \
   unsigned __num_dw = 0;

#define dgc_cs_emit(value)                                                                                             \
   assert(__num_dw < ARRAY_SIZE(__dwords));                                                                            \
   __dwords[__num_dw++] = value;

#define dgc_cs_emit_imm(value) dgc_cs_emit(nir_imm_int(__cs->b, value));

#define dgc_cs_set_sh_reg_seq(reg, num)                                                                                \
   dgc_cs_emit_imm(PKT3(PKT3_SET_SH_REG, num, 0));                                                                     \
   dgc_cs_emit_imm((reg - SI_SH_REG_OFFSET) >> 2);

#define dgc_cs_end() dgc_emit(__cs, __num_dw, __dwords);

static nir_def *
nir_pkt3_base(nir_builder *b, unsigned op, nir_def *len, bool predicate)
{
   len = nir_iand_imm(b, len, 0x3fff);
   return nir_ior_imm(b, nir_ishl_imm(b, len, 16), PKT_TYPE_S(3) | PKT3_IT_OPCODE_S(op) | PKT3_PREDICATE(predicate));
}

static nir_def *
nir_pkt3(nir_builder *b, unsigned op, nir_def *len)
{
   return nir_pkt3_base(b, op, len, false);
}

static nir_def *
dgc_get_nop_packet(nir_builder *b, const struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   if (pdev->info.gfx_ib_pad_with_type2) {
      return nir_imm_int(b, PKT2_NOP_PAD);
   } else {
      return nir_imm_int(b, PKT3_NOP_PAD);
   }
}

static void
dgc_emit_userdata_vertex(struct dgc_cmdbuf *cs, nir_def *first_vertex, nir_def *first_instance, nir_def *drawid)
{
   const struct radv_device *device = cs->dev;
   nir_builder *b = cs->b;

   nir_def *vtx_base_sgpr = load_param16(b, vtx_base_sgpr);
   vtx_base_sgpr = nir_u2u32(b, vtx_base_sgpr);

   nir_def *has_drawid = nir_test_mask(b, vtx_base_sgpr, DGC_USES_DRAWID);
   nir_def *has_baseinstance = nir_test_mask(b, vtx_base_sgpr, DGC_USES_BASEINSTANCE);

   nir_def *pkt_cnt = nir_imm_int(b, 1);
   pkt_cnt = nir_bcsel(b, has_drawid, nir_iadd_imm(b, pkt_cnt, 1), pkt_cnt);
   pkt_cnt = nir_bcsel(b, has_baseinstance, nir_iadd_imm(b, pkt_cnt, 1), pkt_cnt);

   dgc_cs_begin(cs);
   dgc_cs_emit(nir_pkt3(b, PKT3_SET_SH_REG, pkt_cnt));
   dgc_cs_emit(nir_iand_imm(b, vtx_base_sgpr, 0x3FFF));
   dgc_cs_emit(first_vertex);
   dgc_cs_emit(nir_bcsel(b, nir_ior(b, has_drawid, has_baseinstance), nir_bcsel(b, has_drawid, drawid, first_instance),
                         dgc_get_nop_packet(b, device)));
   dgc_cs_emit(nir_bcsel(b, nir_iand(b, has_drawid, has_baseinstance), first_instance, dgc_get_nop_packet(b, device)));
   dgc_cs_end();
}

static void
dgc_emit_userdata_mesh(struct dgc_cmdbuf *cs, nir_def *x, nir_def *y, nir_def *z, nir_def *drawid)
{
   const struct radv_device *device = cs->dev;
   nir_builder *b = cs->b;

   nir_def *vtx_base_sgpr = load_param16(b, vtx_base_sgpr);
   vtx_base_sgpr = nir_u2u32(b, vtx_base_sgpr);

   nir_def *has_grid_size = nir_test_mask(b, vtx_base_sgpr, DGC_USES_GRID_SIZE);
   nir_def *has_drawid = nir_test_mask(b, vtx_base_sgpr, DGC_USES_DRAWID);

   nir_push_if(b, nir_ior(b, has_grid_size, has_drawid));
   {
      nir_def *pkt_cnt = nir_imm_int(b, 0);
      pkt_cnt = nir_bcsel(b, has_grid_size, nir_iadd_imm(b, pkt_cnt, 3), pkt_cnt);
      pkt_cnt = nir_bcsel(b, has_drawid, nir_iadd_imm(b, pkt_cnt, 1), pkt_cnt);

      dgc_cs_begin(cs);
      dgc_cs_emit(nir_pkt3(b, PKT3_SET_SH_REG, pkt_cnt));
      dgc_cs_emit(nir_iand_imm(b, vtx_base_sgpr, 0x3FFF));
      /* DrawID needs to be first if no GridSize. */
      dgc_cs_emit(nir_bcsel(b, has_grid_size, x, drawid));
      dgc_cs_emit(nir_bcsel(b, has_grid_size, y, dgc_get_nop_packet(b, device)));
      dgc_cs_emit(nir_bcsel(b, has_grid_size, z, dgc_get_nop_packet(b, device)));
      dgc_cs_emit(nir_bcsel(b, has_drawid, drawid, dgc_get_nop_packet(b, device)));
      dgc_cs_end();
   }
   nir_pop_if(b, NULL);
}

static void
dgc_emit_sqtt_userdata(struct dgc_cmdbuf *cs, nir_def *data)
{
   const struct radv_device *device = cs->dev;
   const struct radv_physical_device *pdev = radv_device_physical(device);
   nir_builder *b = cs->b;

   if (!cs->dev->sqtt.bo)
      return;

   dgc_cs_begin(cs);
   dgc_cs_emit(nir_pkt3_base(b, PKT3_SET_UCONFIG_REG, nir_imm_int(b, 1), pdev->info.gfx_level >= GFX10));
   dgc_cs_emit_imm((R_030D08_SQ_THREAD_TRACE_USERDATA_2 - CIK_UCONFIG_REG_OFFSET) >> 2);
   dgc_cs_emit(data);
   dgc_cs_end();
}

static void
dgc_emit_sqtt_thread_trace_marker(struct dgc_cmdbuf *cs)
{
   if (!cs->dev->sqtt.bo)
      return;

   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(PKT3_EVENT_WRITE, 0, 0));
   dgc_cs_emit_imm(EVENT_TYPE(V_028A90_THREAD_TRACE_MARKER | EVENT_INDEX(0)));
   dgc_cs_end();
}

static void
dgc_emit_sqtt_marker_event(struct dgc_cmdbuf *cs, nir_def *sequence_id, enum rgp_sqtt_marker_event_type event)
{
   struct rgp_sqtt_marker_event marker = {0};
   nir_builder *b = cs->b;

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_EVENT;
   marker.api_type = event;

   dgc_emit_sqtt_userdata(cs, nir_imm_int(b, marker.dword01));
   dgc_emit_sqtt_userdata(cs, nir_imm_int(b, marker.dword02));
   dgc_emit_sqtt_userdata(cs, sequence_id);
}

static void
dgc_emit_sqtt_marker_event_with_dims(struct dgc_cmdbuf *cs, nir_def *sequence_id, nir_def *x, nir_def *y, nir_def *z,
                                     enum rgp_sqtt_marker_event_type event)
{
   struct rgp_sqtt_marker_event_with_dims marker = {0};
   nir_builder *b = cs->b;

   marker.event.identifier = RGP_SQTT_MARKER_IDENTIFIER_EVENT;
   marker.event.api_type = event;
   marker.event.has_thread_dims = 1;

   dgc_emit_sqtt_userdata(cs, nir_imm_int(b, marker.event.dword01));
   dgc_emit_sqtt_userdata(cs, nir_imm_int(b, marker.event.dword02));
   dgc_emit_sqtt_userdata(cs, sequence_id);
   dgc_emit_sqtt_userdata(cs, x);
   dgc_emit_sqtt_userdata(cs, y);
   dgc_emit_sqtt_userdata(cs, z);
}

static void
dgc_emit_sqtt_begin_api_marker(struct dgc_cmdbuf *cs, enum rgp_sqtt_marker_general_api_type api_type)
{
   struct rgp_sqtt_marker_general_api marker = {0};
   nir_builder *b = cs->b;

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_GENERAL_API;
   marker.api_type = api_type;

   dgc_emit_sqtt_userdata(cs, nir_imm_int(b, marker.dword01));
}

static void
dgc_emit_sqtt_end_api_marker(struct dgc_cmdbuf *cs, enum rgp_sqtt_marker_general_api_type api_type)
{
   struct rgp_sqtt_marker_general_api marker = {0};
   nir_builder *b = cs->b;

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_GENERAL_API;
   marker.api_type = api_type;
   marker.is_end = 1;

   dgc_emit_sqtt_userdata(cs, nir_imm_int(b, marker.dword01));
}

static void
dgc_emit_instance_count(struct dgc_cmdbuf *cs, nir_def *instance_count)
{
   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(PKT3_NUM_INSTANCES, 0, 0));
   dgc_cs_emit(instance_count);
   dgc_cs_end();
}

static void
dgc_emit_draw_index_offset_2(struct dgc_cmdbuf *cs, nir_def *index_offset, nir_def *index_count,
                             nir_def *max_index_count)
{
   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(PKT3_DRAW_INDEX_OFFSET_2, 3, 0));
   dgc_cs_emit(max_index_count);
   dgc_cs_emit(index_offset);
   dgc_cs_emit(index_count);
   dgc_cs_emit_imm(V_0287F0_DI_SRC_SEL_DMA);
   dgc_cs_end();
}

static void
dgc_emit_draw_index_auto(struct dgc_cmdbuf *cs, nir_def *vertex_count)
{
   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(PKT3_DRAW_INDEX_AUTO, 1, 0));
   dgc_cs_emit(vertex_count);
   dgc_cs_emit_imm(V_0287F0_DI_SRC_SEL_AUTO_INDEX);
   dgc_cs_end();
}

static void
dgc_emit_dispatch_direct(struct dgc_cmdbuf *cs, nir_def *wg_x, nir_def *wg_y, nir_def *wg_z,
                         nir_def *dispatch_initiator)
{
   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(PKT3_DISPATCH_DIRECT, 3, 0) | PKT3_SHADER_TYPE_S(1));
   dgc_cs_emit(wg_x);
   dgc_cs_emit(wg_y);
   dgc_cs_emit(wg_z);
   dgc_cs_emit(dispatch_initiator);
   dgc_cs_end();
}

static void
dgc_emit_dispatch_mesh_direct(struct dgc_cmdbuf *cs, nir_def *x, nir_def *y, nir_def *z)
{
   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(PKT3_DISPATCH_MESH_DIRECT, 3, 0));
   dgc_cs_emit(x);
   dgc_cs_emit(y);
   dgc_cs_emit(z);
   dgc_cs_emit_imm(S_0287F0_SOURCE_SELECT(V_0287F0_DI_SRC_SEL_AUTO_INDEX));
   dgc_cs_end();
}

static void
dgc_emit_grid_size_user_sgpr(struct dgc_cmdbuf *cs, nir_def *grid_base_sgpr, nir_def *wg_x, nir_def *wg_y,
                             nir_def *wg_z)
{
   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(PKT3_SET_SH_REG, 3, 0));
   dgc_cs_emit(grid_base_sgpr);
   dgc_cs_emit(wg_x);
   dgc_cs_emit(wg_y);
   dgc_cs_emit(wg_z);
   dgc_cs_end();
}

static void
dgc_emit_grid_size_pointer(struct dgc_cmdbuf *cs, nir_def *grid_base_sgpr, nir_def *stream_addr,
                           nir_def *dispatch_params_offset)
{
   nir_builder *b = cs->b;

   nir_def *va = nir_iadd(b, stream_addr, nir_u2u64(b, dispatch_params_offset));

   nir_def *va_lo = nir_unpack_64_2x32_split_x(b, va);
   nir_def *va_hi = nir_unpack_64_2x32_split_y(b, va);

   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(PKT3_SET_SH_REG, 2, 0));
   dgc_cs_emit(grid_base_sgpr);
   dgc_cs_emit(va_lo);
   dgc_cs_emit(va_hi);
   dgc_cs_end();
}

static void
dgc_emit_pkt3_set_base(struct dgc_cmdbuf *cs, nir_def *va)
{
   nir_builder *b = cs->b;

   nir_def *va_lo = nir_unpack_64_2x32_split_x(b, va);
   nir_def *va_hi = nir_unpack_64_2x32_split_y(b, va);

   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(PKT3_SET_BASE, 2, 0));
   dgc_cs_emit_imm(1);
   dgc_cs_emit(va_lo);
   dgc_cs_emit(va_hi);
   dgc_cs_end();
}

static void
dgc_emit_pkt3_draw_indirect(struct dgc_cmdbuf *cs, bool indexed)
{
   const unsigned di_src_sel = indexed ? V_0287F0_DI_SRC_SEL_DMA : V_0287F0_DI_SRC_SEL_AUTO_INDEX;
   nir_builder *b = cs->b;

   nir_def *vtx_base_sgpr = load_param16(b, vtx_base_sgpr);
   vtx_base_sgpr = nir_iand_imm(b, nir_u2u32(b, vtx_base_sgpr), 0x3FFF);

   nir_def *has_drawid = nir_test_mask(b, vtx_base_sgpr, DGC_USES_DRAWID);
   nir_def *has_baseinstance = nir_test_mask(b, vtx_base_sgpr, DGC_USES_BASEINSTANCE);

   /* vertex_offset_reg = (base_reg - SI_SH_REG_OFFSET) >> 2 */
   nir_def *vertex_offset_reg = vtx_base_sgpr;

   /* start_instance_reg = (base_reg + (draw_id_enable ? 8 : 4) - SI_SH_REG_OFFSET) >> 2 */
   nir_def *start_instance_offset = nir_bcsel(b, has_drawid, nir_imm_int(b, 2), nir_imm_int(b, 1));
   nir_def *start_instance_reg = nir_iadd(b, vtx_base_sgpr, start_instance_offset);

   /* draw_id_reg = (base_reg + 4 - SI_SH_REG_OFFSET) >> 2 */
   nir_def *draw_id_reg = nir_iadd(b, vtx_base_sgpr, nir_imm_int(b, 1));

   nir_if *if_drawid = nir_push_if(b, has_drawid);
   {
      const unsigned pkt3_op = indexed ? PKT3_DRAW_INDEX_INDIRECT_MULTI : PKT3_DRAW_INDIRECT_MULTI;

      dgc_cs_begin(cs);
      dgc_cs_emit_imm(PKT3(pkt3_op, 8, 0));
      dgc_cs_emit_imm(0);
      dgc_cs_emit(vertex_offset_reg);
      dgc_cs_emit(nir_bcsel(b, has_baseinstance, start_instance_reg, nir_imm_int(b, 0)));
      dgc_cs_emit(nir_ior(b, draw_id_reg, nir_imm_int(b, S_2C3_DRAW_INDEX_ENABLE(1))));
      dgc_cs_emit_imm(1); /* draw count */
      dgc_cs_emit_imm(0); /* count va low */
      dgc_cs_emit_imm(0); /* count va high */
      dgc_cs_emit_imm(0); /* stride */
      dgc_cs_emit_imm(V_0287F0_DI_SRC_SEL_AUTO_INDEX);
      dgc_cs_end();
   }
   nir_push_else(b, if_drawid);
   {
      const unsigned pkt3_op = indexed ? PKT3_DRAW_INDEX_INDIRECT : PKT3_DRAW_INDIRECT;

      dgc_cs_begin(cs);
      dgc_cs_emit_imm(PKT3(pkt3_op, 3, 0));
      dgc_cs_emit_imm(0);
      dgc_cs_emit(vertex_offset_reg);
      dgc_cs_emit(nir_bcsel(b, has_baseinstance, start_instance_reg, nir_imm_int(b, 0)));
      dgc_cs_emit_imm(di_src_sel);
      dgc_cs_end();
   }
   nir_pop_if(b, if_drawid);
}

static void
dgc_emit_draw_indirect(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_def *draw_params_offset, nir_def *sequence_id,
                       bool indexed)
{
   nir_builder *b = cs->b;

   nir_def *va = nir_iadd(b, stream_addr, nir_u2u64(b, draw_params_offset));

   dgc_emit_sqtt_begin_api_marker(cs, indexed ? ApiCmdDrawIndexedIndirect : ApiCmdDrawIndirect);
   dgc_emit_sqtt_marker_event(cs, sequence_id, indexed ? EventCmdDrawIndexedIndirect : EventCmdDrawIndirect);

   dgc_emit_pkt3_set_base(cs, va);
   dgc_emit_pkt3_draw_indirect(cs, indexed);

   dgc_emit_sqtt_thread_trace_marker(cs);
   dgc_emit_sqtt_end_api_marker(cs, indexed ? ApiCmdDrawIndexedIndirect : ApiCmdDrawIndirect);
}

static nir_def *
dgc_cmd_buf_size(nir_builder *b, nir_def *sequence_count, bool is_ace, const struct radv_device *device)
{
   nir_def *cmd_buf_size = is_ace ? load_param32(b, ace_cmd_buf_size) : load_param32(b, cmd_buf_size);
   nir_def *cmd_buf_stride = is_ace ? load_param32(b, ace_cmd_buf_stride) : load_param32(b, cmd_buf_stride);
   const enum amd_ip_type ip_type = is_ace ? AMD_IP_COMPUTE : AMD_IP_GFX;

   nir_def *use_preamble = nir_ine_imm(b, load_param8(b, use_preamble), 0);
   nir_def *size = nir_imul(b, cmd_buf_stride, sequence_count);
   unsigned align_mask = radv_align_cmdbuf_size(device, 1, ip_type) - 1;

   size = nir_iand_imm(b, nir_iadd_imm(b, size, align_mask), ~align_mask);

   /* Ensure we don't have to deal with a jump to an empty IB in the preamble. */
   size = nir_imax(b, size, nir_imm_int(b, align_mask + 1));

   return nir_bcsel(b, use_preamble, size, cmd_buf_size);
}

static void
build_dgc_buffer_tail(nir_builder *b, nir_def *cmd_buf_offset, nir_def *cmd_buf_size, nir_def *cmd_buf_stride,
                      nir_def *sequence_count, const struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   nir_def *global_id = get_global_ids(b, 1);

   nir_push_if(b, nir_ieq_imm(b, global_id, 0));
   {
      nir_def *cmd_buf_tail_start = nir_imul(b, cmd_buf_stride, sequence_count);

      nir_variable *offset = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "offset");
      nir_store_var(b, offset, cmd_buf_tail_start, 0x1);

      nir_def *va = nir_pack_64_2x32_split(b, load_param32(b, upload_addr), nir_imm_int(b, pdev->info.address32_hi));
      nir_push_loop(b);
      {
         nir_def *curr_offset = nir_load_var(b, offset);
         const unsigned MAX_PACKET_WORDS = 0x3FFC;

         nir_break_if(b, nir_ieq(b, curr_offset, cmd_buf_size));

         nir_def *packet, *packet_size;

         if (pdev->info.gfx_ib_pad_with_type2) {
            packet_size = nir_imm_int(b, 4);
            packet = nir_imm_int(b, PKT2_NOP_PAD);
         } else {
            packet_size = nir_isub(b, cmd_buf_size, curr_offset);
            packet_size = nir_umin(b, packet_size, nir_imm_int(b, MAX_PACKET_WORDS * 4));

            nir_def *len = nir_ushr_imm(b, packet_size, 2);
            len = nir_iadd_imm(b, len, -2);
            packet = nir_pkt3(b, PKT3_NOP, len);
         }

         nir_build_store_global(b, packet, nir_iadd(b, va, nir_u2u64(b, nir_iadd(b, curr_offset, cmd_buf_offset))),
                                .access = ACCESS_NON_READABLE);

         nir_store_var(b, offset, nir_iadd(b, curr_offset, packet_size), 0x1);
      }
      nir_pop_loop(b, NULL);
   }
   nir_pop_if(b, NULL);
}

static void
build_dgc_buffer_tail_gfx(nir_builder *b, nir_def *sequence_count, const struct radv_device *device)
{
   nir_def *cmd_buf_offset = load_param32(b, cmd_buf_main_offset);
   nir_def *cmd_buf_size = dgc_cmd_buf_size(b, sequence_count, false, device);
   nir_def *cmd_buf_stride = load_param32(b, cmd_buf_stride);

   build_dgc_buffer_tail(b, cmd_buf_offset, cmd_buf_size, cmd_buf_stride, sequence_count, device);
}

static void
build_dgc_buffer_tail_ace(nir_builder *b, nir_def *sequence_count, const struct radv_device *device)
{
   nir_def *cmd_buf_offset = load_param32(b, ace_cmd_buf_main_offset);
   nir_def *cmd_buf_size = dgc_cmd_buf_size(b, sequence_count, true, device);
   nir_def *cmd_buf_stride = load_param32(b, ace_cmd_buf_stride);

   build_dgc_buffer_tail(b, cmd_buf_offset, cmd_buf_size, cmd_buf_stride, sequence_count, device);
}

static void
build_dgc_buffer_preamble(nir_builder *b, nir_def *cmd_buf_preamble_offset, nir_def *cmd_buf_size,
                          nir_def *cmd_buf_main_offset, unsigned preamble_size, nir_def *sequence_count,
                          const struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   nir_def *global_id = get_global_ids(b, 1);
   nir_def *use_preamble = nir_ine_imm(b, load_param8(b, use_preamble), 0);

   nir_push_if(b, nir_iand(b, nir_ieq_imm(b, global_id, 0), use_preamble));
   {
      nir_def *va = nir_pack_64_2x32_split(b, load_param32(b, upload_addr), nir_imm_int(b, pdev->info.address32_hi));
      va = nir_iadd(b, va, nir_u2u64(b, cmd_buf_preamble_offset));

      nir_def *words = nir_ushr_imm(b, cmd_buf_size, 2);

      nir_def *nop_packet = dgc_get_nop_packet(b, device);

      nir_def *nop_packets[] = {
         nop_packet,
         nop_packet,
         nop_packet,
         nop_packet,
      };

      const unsigned jump_size = 16;
      unsigned offset;

      /* Do vectorized store if possible */
      for (offset = 0; offset + 16 <= preamble_size - jump_size; offset += 16) {
         nir_build_store_global(b, nir_vec(b, nop_packets, 4), nir_iadd(b, va, nir_imm_int64(b, offset)),
                                .access = ACCESS_NON_READABLE);
      }

      for (; offset + 4 <= preamble_size - jump_size; offset += 4) {
         nir_build_store_global(b, nop_packet, nir_iadd(b, va, nir_imm_int64(b, offset)),
                                .access = ACCESS_NON_READABLE);
      }

      nir_def *chain_packets[] = {
         nir_imm_int(b, PKT3(PKT3_INDIRECT_BUFFER, 2, 0)),
         nir_iadd(b, cmd_buf_main_offset, load_param32(b, upload_addr)),
         nir_imm_int(b, pdev->info.address32_hi),
         nir_ior_imm(b, words, S_3F2_CHAIN(1) | S_3F2_VALID(1) | S_3F2_PRE_ENA(false)),
      };

      nir_build_store_global(b, nir_vec(b, chain_packets, 4),
                             nir_iadd(b, va, nir_imm_int64(b, preamble_size - jump_size)),
                             .access = ACCESS_NON_READABLE);
   }
   nir_pop_if(b, NULL);
}

static void
build_dgc_buffer_preamble_gfx(nir_builder *b, nir_def *sequence_count, const struct radv_device *device)
{
   nir_def *cmd_buf_preamble_offset = nir_imm_int(b, 0);
   nir_def *cmd_buf_main_offset = load_param32(b, cmd_buf_main_offset);
   nir_def *cmd_buf_size = dgc_cmd_buf_size(b, sequence_count, false, device);
   unsigned preamble_size = radv_dgc_preamble_cmdbuf_size(device, AMD_IP_GFX);

   build_dgc_buffer_preamble(b, cmd_buf_preamble_offset, cmd_buf_size, cmd_buf_main_offset, preamble_size,
                             sequence_count, device);
}

static void
build_dgc_buffer_preamble_ace(nir_builder *b, nir_def *sequence_count, const struct radv_device *device)
{
   nir_def *cmd_buf_preamble_offset = load_param32(b, ace_cmd_buf_preamble_offset);
   nir_def *cmd_buf_main_offset = load_param32(b, ace_cmd_buf_main_offset);
   nir_def *cmd_buf_size = dgc_cmd_buf_size(b, sequence_count, true, device);
   unsigned preamble_size = radv_dgc_preamble_cmdbuf_size(device, AMD_IP_COMPUTE);

   build_dgc_buffer_preamble(b, cmd_buf_preamble_offset, cmd_buf_size, cmd_buf_main_offset, preamble_size,
                             sequence_count, device);
}

/**
 * Emit VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_NV.
 */
static void
dgc_emit_draw(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_def *draw_params_offset, nir_def *sequence_id)
{
   nir_builder *b = cs->b;

   nir_def *draw_data0 = nir_build_load_global(b, 4, 32, nir_iadd(b, stream_addr, nir_u2u64(b, draw_params_offset)),
                                               .access = ACCESS_NON_WRITEABLE);
   nir_def *vertex_count = nir_channel(b, draw_data0, 0);
   nir_def *instance_count = nir_channel(b, draw_data0, 1);
   nir_def *vertex_offset = nir_channel(b, draw_data0, 2);
   nir_def *first_instance = nir_channel(b, draw_data0, 3);

   nir_push_if(b, nir_iand(b, nir_ine_imm(b, vertex_count, 0), nir_ine_imm(b, instance_count, 0)));
   {
      dgc_emit_sqtt_begin_api_marker(cs, ApiCmdDraw);
      dgc_emit_sqtt_marker_event(cs, sequence_id, EventCmdDraw);

      dgc_emit_userdata_vertex(cs, vertex_offset, first_instance, sequence_id);
      dgc_emit_instance_count(cs, instance_count);
      dgc_emit_draw_index_auto(cs, vertex_count);

      dgc_emit_sqtt_thread_trace_marker(cs);
      dgc_emit_sqtt_end_api_marker(cs, ApiCmdDraw);
   }
   nir_pop_if(b, 0);
}

/**
 * Emit VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_NV.
 */
static void
dgc_emit_draw_indexed(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_def *draw_params_offset, nir_def *sequence_id,
                      nir_def *max_index_count)
{
   nir_builder *b = cs->b;

   nir_def *draw_data0 = nir_build_load_global(b, 4, 32, nir_iadd(b, stream_addr, nir_u2u64(b, draw_params_offset)),
                                               .access = ACCESS_NON_WRITEABLE);
   nir_def *draw_data1 =
      nir_build_load_global(b, 1, 32, nir_iadd_imm(b, nir_iadd(b, stream_addr, nir_u2u64(b, draw_params_offset)), 16),
                            .access = ACCESS_NON_WRITEABLE);
   nir_def *index_count = nir_channel(b, draw_data0, 0);
   nir_def *instance_count = nir_channel(b, draw_data0, 1);
   nir_def *first_index = nir_channel(b, draw_data0, 2);
   nir_def *vertex_offset = nir_channel(b, draw_data0, 3);
   nir_def *first_instance = nir_channel(b, draw_data1, 0);

   nir_push_if(b, nir_iand(b, nir_ine_imm(b, index_count, 0), nir_ine_imm(b, instance_count, 0)));
   {
      dgc_emit_sqtt_begin_api_marker(cs, ApiCmdDrawIndexed);
      dgc_emit_sqtt_marker_event(cs, sequence_id, EventCmdDrawIndexed);

      dgc_emit_userdata_vertex(cs, vertex_offset, first_instance, sequence_id);
      dgc_emit_instance_count(cs, instance_count);
      dgc_emit_draw_index_offset_2(cs, first_index, index_count, max_index_count);

      dgc_emit_sqtt_thread_trace_marker(cs);
      dgc_emit_sqtt_end_api_marker(cs, ApiCmdDrawIndexed);
   }
   nir_pop_if(b, 0);
}

/**
 * Emit VK_INDIRECT_COMMANDS_TOKEN_TYPE_INDEX_BUFFER_NV.
 */
static void
dgc_emit_index_buffer(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_def *index_buffer_offset, nir_def *ibo_type_32,
                      nir_def *ibo_type_8, nir_variable *max_index_count_var)
{
   const struct radv_device *device = cs->dev;
   const struct radv_physical_device *pdev = radv_device_physical(device);
   nir_builder *b = cs->b;

   nir_def *data = nir_build_load_global(b, 4, 32, nir_iadd(b, stream_addr, nir_u2u64(b, index_buffer_offset)),
                                         .access = ACCESS_NON_WRITEABLE);

   nir_def *vk_index_type = nir_channel(b, data, 3);
   nir_def *index_type = nir_bcsel(b, nir_ieq(b, vk_index_type, ibo_type_32), nir_imm_int(b, V_028A7C_VGT_INDEX_32),
                                   nir_imm_int(b, V_028A7C_VGT_INDEX_16));
   index_type = nir_bcsel(b, nir_ieq(b, vk_index_type, ibo_type_8), nir_imm_int(b, V_028A7C_VGT_INDEX_8), index_type);

   nir_def *index_size = nir_iand_imm(b, nir_ushr(b, nir_imm_int(b, 0x142), nir_imul_imm(b, index_type, 4)), 0xf);

   nir_def *max_index_count = nir_udiv(b, nir_channel(b, data, 2), index_size);
   nir_store_var(b, max_index_count_var, max_index_count, 0x1);

   nir_def *addr_upper = nir_channel(b, data, 1);
   addr_upper = nir_ishr_imm(b, nir_ishl_imm(b, addr_upper, 16), 16);

   dgc_cs_begin(cs);

   if (pdev->info.gfx_level >= GFX9) {
      unsigned opcode = PKT3_SET_UCONFIG_REG_INDEX;
      if (pdev->info.gfx_level < GFX9 || (pdev->info.gfx_level == GFX9 && pdev->info.me_fw_version < 26))
         opcode = PKT3_SET_UCONFIG_REG;
      dgc_cs_emit_imm(PKT3(opcode, 1, 0));
      dgc_cs_emit_imm((R_03090C_VGT_INDEX_TYPE - CIK_UCONFIG_REG_OFFSET) >> 2 | (2u << 28));
      dgc_cs_emit(index_type);
   } else {
      dgc_cs_emit_imm(PKT3(PKT3_INDEX_TYPE, 0, 0));
      dgc_cs_emit(index_type);
      dgc_cs_emit(dgc_get_nop_packet(b, device));
   }

   dgc_cs_emit_imm(PKT3(PKT3_INDEX_BASE, 1, 0));
   dgc_cs_emit(nir_channel(b, data, 0));
   dgc_cs_emit(addr_upper);

   dgc_cs_emit_imm(PKT3(PKT3_INDEX_BUFFER_SIZE, 0, 0));
   dgc_cs_emit(max_index_count);

   dgc_cs_end();
}

/**
 * Emit VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_NV.
 */
static nir_def *
dgc_get_push_constant_stages(nir_builder *b, nir_def *stream_addr)
{
   nir_def *res1, *res2;

   nir_push_if(b, nir_ieq_imm(b, load_param8(b, bind_pipeline), 1));
   {
      nir_def *pipeline_va = dgc_get_pipeline_va(b, stream_addr);

      nir_def *has_push_constant = nir_ine_imm(b, load_metadata32(b, push_const_sgpr), 0);
      res1 = nir_bcsel(b, has_push_constant, nir_imm_int(b, VK_SHADER_STAGE_COMPUTE_BIT), nir_imm_int(b, 0));
   }
   nir_push_else(b, 0);
   {
      res2 = load_param16(b, push_constant_stages);
   }
   nir_pop_if(b, 0);

   return nir_if_phi(b, res1, res2);
}

static nir_def *
dgc_get_upload_sgpr(nir_builder *b, nir_def *stream_addr, nir_def *param_buf, nir_def *param_offset,
                    gl_shader_stage stage)
{
   nir_def *res1, *res2;

   nir_push_if(b, nir_ieq_imm(b, load_param8(b, bind_pipeline), 1));
   {
      nir_def *pipeline_va = dgc_get_pipeline_va(b, stream_addr);

      res1 = load_metadata32(b, push_const_sgpr);
   }
   nir_push_else(b, 0);
   {
      res2 = nir_load_ssbo(b, 1, 32, param_buf, nir_iadd_imm(b, param_offset, stage * 12));
   }
   nir_pop_if(b, 0);

   nir_def *res = nir_if_phi(b, res1, res2);

   return nir_ubfe_imm(b, res, 0, 16);
}

static nir_def *
dgc_get_inline_sgpr(nir_builder *b, nir_def *stream_addr, nir_def *param_buf, nir_def *param_offset,
                    gl_shader_stage stage)
{
   nir_def *res1, *res2;

   nir_push_if(b, nir_ieq_imm(b, load_param8(b, bind_pipeline), 1));
   {
      nir_def *pipeline_va = dgc_get_pipeline_va(b, stream_addr);

      res1 = load_metadata32(b, push_const_sgpr);
   }
   nir_push_else(b, 0);
   {
      res2 = nir_load_ssbo(b, 1, 32, param_buf, nir_iadd_imm(b, param_offset, stage * 12));
   }
   nir_pop_if(b, 0);

   nir_def *res = nir_if_phi(b, res1, res2);

   return nir_ubfe_imm(b, res, 16, 16);
}

static nir_def *
dgc_get_inline_mask(nir_builder *b, nir_def *stream_addr, nir_def *param_buf, nir_def *param_offset,
                    gl_shader_stage stage)
{
   nir_def *res1, *res2;

   nir_push_if(b, nir_ieq_imm(b, load_param8(b, bind_pipeline), 1));
   {
      nir_def *pipeline_va = dgc_get_pipeline_va(b, stream_addr);

      res1 = load_metadata64(b, inline_push_const_mask);
   }
   nir_push_else(b, 0);
   {
      nir_def *reg_info = nir_load_ssbo(b, 2, 32, param_buf, nir_iadd_imm(b, param_offset, stage * 12 + 4));
      res2 = nir_pack_64_2x32(b, nir_channels(b, reg_info, 0x3));
   }
   nir_pop_if(b, 0);

   return nir_if_phi(b, res1, res2);
}

static nir_def *
dgc_push_constant_needs_copy(nir_builder *b, nir_def *stream_addr)
{
   nir_def *res1, *res2;

   nir_push_if(b, nir_ieq_imm(b, load_param8(b, bind_pipeline), 1));
   {
      nir_def *pipeline_va = dgc_get_pipeline_va(b, stream_addr);

      res1 = nir_ine_imm(b, nir_ubfe_imm(b, load_metadata32(b, push_const_sgpr), 0, 16), 0);
   }
   nir_push_else(b, 0);
   {
      res2 = nir_ine_imm(b, load_param8(b, const_copy), 0);
   }
   nir_pop_if(b, 0);

   return nir_if_phi(b, res1, res2);
}

struct dgc_pc_params {
   nir_def *buf;
   nir_def *offset;
   nir_def *offset_offset;
   nir_def *const_offset;
};

static struct dgc_pc_params
dgc_get_pc_params(nir_builder *b)
{
   struct dgc_pc_params params = {0};

   nir_def *vbo_cnt = load_param8(b, vbo_cnt);
   nir_def *param_offset = nir_imul_imm(b, vbo_cnt, 24);

   params.buf = radv_meta_load_descriptor(b, 0, 0);
   params.offset = nir_iadd(
      b, param_offset,
      nir_bcsel(b, nir_ieq_imm(b, load_param8(b, bind_pipeline), 1), nir_imm_int(b, MAX_SETS * 4), nir_imm_int(b, 0)));
   params.offset_offset = nir_iadd_imm(b, params.offset, MESA_VULKAN_SHADER_STAGES * 12);
   params.const_offset = nir_iadd_imm(b, params.offset, MAX_PUSH_CONSTANTS_SIZE + MESA_VULKAN_SHADER_STAGES * 12);

   return params;
}

static void
dgc_alloc_push_constant(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_def *push_const_mask,
                        const struct dgc_pc_params *params, nir_variable *upload_offset)
{
   nir_builder *b = cs->b;

   nir_def *const_copy = dgc_push_constant_needs_copy(b, stream_addr);
   nir_def *const_copy_size = load_param16(b, const_copy_size);
   nir_def *const_copy_words = nir_ushr_imm(b, const_copy_size, 2);
   const_copy_words = nir_bcsel(b, const_copy, const_copy_words, nir_imm_int(b, 0));

   nir_variable *idx = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "const_copy_idx");
   nir_store_var(b, idx, nir_imm_int(b, 0), 0x1);

   nir_push_loop(b);
   {
      nir_def *cur_idx = nir_load_var(b, idx);
      nir_break_if(b, nir_uge(b, cur_idx, const_copy_words));

      nir_variable *data = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "copy_data");

      nir_def *update = nir_iand(b, push_const_mask, nir_ishl(b, nir_imm_int64(b, 1), cur_idx));
      update = nir_bcsel(b, nir_ult_imm(b, cur_idx, 64 /* bits in push_const_mask */), update, nir_imm_int64(b, 0));

      nir_push_if(b, nir_ine_imm(b, update, 0));
      {
         nir_def *stream_offset =
            nir_load_ssbo(b, 1, 32, params->buf, nir_iadd(b, params->offset_offset, nir_ishl_imm(b, cur_idx, 2)));
         nir_def *new_data = nir_build_load_global(b, 1, 32, nir_iadd(b, stream_addr, nir_u2u64(b, stream_offset)),
                                                   .access = ACCESS_NON_WRITEABLE);
         nir_store_var(b, data, new_data, 0x1);
      }
      nir_push_else(b, NULL);
      {
         nir_store_var(
            b, data,
            nir_load_ssbo(b, 1, 32, params->buf, nir_iadd(b, params->const_offset, nir_ishl_imm(b, cur_idx, 2))), 0x1);
      }
      nir_pop_if(b, NULL);

      nir_def *offset = nir_iadd(b, nir_load_var(b, upload_offset), nir_ishl_imm(b, cur_idx, 2));

      nir_build_store_global(b, nir_load_var(b, data), nir_iadd(b, cs->va, nir_u2u64(b, offset)),
                             .access = ACCESS_NON_READABLE);

      nir_store_var(b, idx, nir_iadd_imm(b, cur_idx, 1), 0x1);
   }
   nir_pop_loop(b, NULL);
}

static void
dgc_emit_push_constant_for_stage(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_def *push_const_mask,
                                 const struct dgc_pc_params *params, gl_shader_stage stage, nir_variable *upload_offset)
{
   nir_builder *b = cs->b;

   nir_def *upload_sgpr = dgc_get_upload_sgpr(b, stream_addr, params->buf, params->offset, stage);
   nir_def *inline_sgpr = dgc_get_inline_sgpr(b, stream_addr, params->buf, params->offset, stage);
   nir_def *inline_mask = dgc_get_inline_mask(b, stream_addr, params->buf, params->offset, stage);

   nir_push_if(b, nir_ine_imm(b, upload_sgpr, 0));
   {
      dgc_cs_begin(cs);
      dgc_cs_emit_imm(PKT3(PKT3_SET_SH_REG, 1, 0));
      dgc_cs_emit(upload_sgpr);
      dgc_cs_emit(nir_iadd(b, load_param32(b, upload_addr), nir_load_var(b, upload_offset)));
      dgc_cs_end();
   }
   nir_pop_if(b, NULL);

   nir_variable *idx = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "const_copy_idx");
   nir_store_var(b, idx, nir_imm_int(b, 0), 0x1);

   nir_push_if(b, nir_ine_imm(b, inline_sgpr, 0));
   {
      nir_store_var(b, idx, nir_imm_int(b, 0), 0x1);

      nir_variable *pc_idx = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "pc_idx");
      nir_store_var(b, pc_idx, nir_imm_int(b, 0), 0x1);

      nir_push_loop(b);
      {
         nir_def *cur_idx = nir_load_var(b, idx);
         nir_push_if(b, nir_uge_imm(b, cur_idx, 64 /* bits in inline_mask */));
         {
            nir_jump(b, nir_jump_break);
         }
         nir_pop_if(b, NULL);

         nir_def *l = nir_ishl(b, nir_imm_int64(b, 1), cur_idx);
         nir_push_if(b, nir_ieq_imm(b, nir_iand(b, l, inline_mask), 0));
         {
            nir_store_var(b, idx, nir_iadd_imm(b, cur_idx, 1), 0x1);
            nir_jump(b, nir_jump_continue);
         }
         nir_pop_if(b, NULL);

         nir_variable *data = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "copy_data");

         nir_def *update = nir_iand(b, push_const_mask, nir_ishl(b, nir_imm_int64(b, 1), cur_idx));
         update = nir_bcsel(b, nir_ult_imm(b, cur_idx, 64 /* bits in push_const_mask */), update, nir_imm_int64(b, 0));

         nir_push_if(b, nir_ine_imm(b, update, 0));
         {
            nir_def *stream_offset =
               nir_load_ssbo(b, 1, 32, params->buf, nir_iadd(b, params->offset_offset, nir_ishl_imm(b, cur_idx, 2)));
            nir_def *new_data = nir_build_load_global(b, 1, 32, nir_iadd(b, stream_addr, nir_u2u64(b, stream_offset)),
                                                      .access = ACCESS_NON_WRITEABLE);

            nir_store_var(b, data, new_data, 0x1);

            dgc_cs_begin(cs);
            dgc_cs_emit_imm(PKT3(PKT3_SET_SH_REG, 1, 0));
            dgc_cs_emit(nir_iadd(b, inline_sgpr, nir_load_var(b, pc_idx)));
            dgc_cs_emit(nir_load_var(b, data));
            dgc_cs_end();
         }
         nir_push_else(b, NULL);
         {
            nir_push_if(b, nir_ieq_imm(b, load_param8(b, bind_pipeline), 1));
            {
               /* For indirect pipeline binds, partial push constant updates can't be emitted
                * when the DGC execute is called because there is no bound pipeline and they have
                * to be emitted from the DGC prepare shader.
                */
               nir_def *new_data =
                  nir_load_ssbo(b, 1, 32, params->buf, nir_iadd(b, params->const_offset, nir_ishl_imm(b, cur_idx, 2)));
               nir_store_var(b, data, new_data, 0x1);

               dgc_cs_begin(cs);
               dgc_cs_emit_imm(PKT3(PKT3_SET_SH_REG, 1, 0));
               dgc_cs_emit(nir_iadd(b, inline_sgpr, nir_load_var(b, pc_idx)));
               dgc_cs_emit(nir_load_var(b, data));
               dgc_cs_end();
            }
            nir_pop_if(b, NULL);
         }

         nir_pop_if(b, NULL);

         nir_store_var(b, idx, nir_iadd_imm(b, cur_idx, 1), 0x1);
         nir_store_var(b, pc_idx, nir_iadd_imm(b, nir_load_var(b, pc_idx), 1), 0x1);
      }
      nir_pop_loop(b, NULL);
   }
   nir_pop_if(b, NULL);
}

static void
dgc_emit_push_constant(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_def *push_const_mask,
                       nir_variable *upload_offset, VkShaderStageFlags stages)
{
   const struct dgc_pc_params params = dgc_get_pc_params(cs->b);
   nir_builder *b = cs->b;

   dgc_alloc_push_constant(cs, stream_addr, push_const_mask, &params, upload_offset);

   nir_def *push_constant_stages = dgc_get_push_constant_stages(b, stream_addr);
   radv_foreach_stage(s, stages)
   {
      nir_push_if(b, nir_test_mask(b, push_constant_stages, mesa_to_vk_shader_stage(s)));
      {
         dgc_emit_push_constant_for_stage(cs, stream_addr, push_const_mask, &params, s, upload_offset);
      }
      nir_pop_if(b, NULL);
   }
}

/**
 * For emitting VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_NV.
 */
static void
dgc_emit_vertex_buffer(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_def *vbo_bind_mask, nir_variable *upload_offset)
{
   const struct radv_device *device = cs->dev;
   const struct radv_physical_device *pdev = radv_device_physical(device);
   nir_builder *b = cs->b;

   nir_def *vbo_cnt = load_param8(b, vbo_cnt);
   nir_variable *vbo_idx = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "vbo_idx");
   nir_store_var(b, vbo_idx, nir_imm_int(b, 0), 0x1);

   nir_push_loop(b);
   {
      nir_break_if(b, nir_uge(b, nir_load_var(b, vbo_idx), vbo_cnt));

      nir_def *vbo_offset = nir_imul_imm(b, nir_load_var(b, vbo_idx), 16);
      nir_variable *vbo_data = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uvec4_type(), "vbo_data");

      nir_def *param_buf = radv_meta_load_descriptor(b, 0, 0);
      nir_store_var(b, vbo_data, nir_load_ssbo(b, 4, 32, param_buf, vbo_offset), 0xf);

      nir_def *vbo_override =
         nir_ine_imm(b, nir_iand(b, vbo_bind_mask, nir_ishl(b, nir_imm_int(b, 1), nir_load_var(b, vbo_idx))), 0);
      nir_push_if(b, vbo_override);
      {
         nir_def *vbo_offset_offset =
            nir_iadd(b, nir_imul_imm(b, vbo_cnt, 16), nir_imul_imm(b, nir_load_var(b, vbo_idx), 8));
         nir_def *vbo_over_data = nir_load_ssbo(b, 2, 32, param_buf, vbo_offset_offset);
         nir_def *stream_offset = nir_iand_imm(b, nir_channel(b, vbo_over_data, 0), 0x7FFF);
         nir_def *stream_data = nir_build_load_global(b, 4, 32, nir_iadd(b, stream_addr, nir_u2u64(b, stream_offset)),
                                                      .access = ACCESS_NON_WRITEABLE);

         nir_def *va = nir_pack_64_2x32(b, nir_trim_vector(b, stream_data, 2));
         nir_def *size = nir_channel(b, stream_data, 2);
         nir_def *stride = nir_channel(b, stream_data, 3);

         nir_def *dyn_stride = nir_test_mask(b, nir_channel(b, vbo_over_data, 0), DGC_DYNAMIC_STRIDE);
         nir_def *old_stride = nir_ubfe_imm(b, nir_channel(b, nir_load_var(b, vbo_data), 1), 16, 14);
         stride = nir_bcsel(b, dyn_stride, stride, old_stride);

         nir_def *use_per_attribute_vb_descs = nir_test_mask(b, nir_channel(b, vbo_over_data, 0), 1u << 31);
         nir_variable *num_records =
            nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "num_records");
         nir_store_var(b, num_records, size, 0x1);

         nir_push_if(b, use_per_attribute_vb_descs);
         {
            nir_def *attrib_end = nir_ubfe_imm(b, nir_channel(b, vbo_over_data, 1), 16, 16);
            nir_def *attrib_index_offset = nir_ubfe_imm(b, nir_channel(b, vbo_over_data, 1), 0, 16);

            nir_push_if(b, nir_ult(b, nir_load_var(b, num_records), attrib_end));
            {
               nir_store_var(b, num_records, nir_imm_int(b, 0), 0x1);
            }
            nir_push_else(b, NULL);
            nir_push_if(b, nir_ieq_imm(b, stride, 0));
            {
               nir_store_var(b, num_records, nir_imm_int(b, 1), 0x1);
            }
            nir_push_else(b, NULL);
            {
               nir_def *r = nir_iadd(
                  b, nir_iadd_imm(b, nir_udiv(b, nir_isub(b, nir_load_var(b, num_records), attrib_end), stride), 1),
                  attrib_index_offset);
               nir_store_var(b, num_records, r, 0x1);
            }
            nir_pop_if(b, NULL);
            nir_pop_if(b, NULL);

            nir_def *convert_cond = nir_ine_imm(b, nir_load_var(b, num_records), 0);
            if (pdev->info.gfx_level == GFX9)
               convert_cond = nir_imm_false(b);
            else if (pdev->info.gfx_level != GFX8)
               convert_cond = nir_iand(b, convert_cond, nir_ieq_imm(b, stride, 0));

            nir_def *new_records =
               nir_iadd(b, nir_imul(b, nir_iadd_imm(b, nir_load_var(b, num_records), -1), stride), attrib_end);
            new_records = nir_bcsel(b, convert_cond, new_records, nir_load_var(b, num_records));
            nir_store_var(b, num_records, new_records, 0x1);
         }
         nir_push_else(b, NULL);
         {
            if (pdev->info.gfx_level != GFX8) {
               nir_push_if(b, nir_ine_imm(b, stride, 0));
               {
                  nir_def *r = nir_iadd(b, nir_load_var(b, num_records), nir_iadd_imm(b, stride, -1));
                  nir_store_var(b, num_records, nir_udiv(b, r, stride), 0x1);
               }
               nir_pop_if(b, NULL);
            }
         }
         nir_pop_if(b, NULL);

         nir_def *rsrc_word3 = nir_channel(b, nir_load_var(b, vbo_data), 3);
         if (pdev->info.gfx_level >= GFX10) {
            nir_def *oob_select = nir_bcsel(b, nir_ieq_imm(b, stride, 0), nir_imm_int(b, V_008F0C_OOB_SELECT_RAW),
                                            nir_imm_int(b, V_008F0C_OOB_SELECT_STRUCTURED));
            rsrc_word3 = nir_iand_imm(b, rsrc_word3, C_008F0C_OOB_SELECT);
            rsrc_word3 = nir_ior(b, rsrc_word3, nir_ishl_imm(b, oob_select, 28));
         }

         nir_def *va_hi = nir_iand_imm(b, nir_unpack_64_2x32_split_y(b, va), 0xFFFF);
         stride = nir_iand_imm(b, stride, 0x3FFF);
         nir_def *new_vbo_data[4] = {nir_unpack_64_2x32_split_x(b, va), nir_ior(b, nir_ishl_imm(b, stride, 16), va_hi),
                                     nir_load_var(b, num_records), rsrc_word3};
         nir_store_var(b, vbo_data, nir_vec(b, new_vbo_data, 4), 0xf);
      }
      nir_pop_if(b, NULL);

      /* On GFX9, it seems bounds checking is disabled if both
       * num_records and stride are zero. This doesn't seem necessary on GFX8, GFX10 and
       * GFX10.3 but it doesn't hurt.
       */
      nir_def *num_records = nir_channel(b, nir_load_var(b, vbo_data), 2);
      nir_def *buf_va =
         nir_iand_imm(b, nir_pack_64_2x32(b, nir_trim_vector(b, nir_load_var(b, vbo_data), 2)), (1ull << 48) - 1ull);
      nir_push_if(b, nir_ior(b, nir_ieq_imm(b, num_records, 0), nir_ieq_imm(b, buf_va, 0)));
      {
         nir_def *new_vbo_data[4] = {nir_imm_int(b, 0), nir_imm_int(b, 0), nir_imm_int(b, 0), nir_imm_int(b, 0)};
         nir_store_var(b, vbo_data, nir_vec(b, new_vbo_data, 4), 0xf);
      }
      nir_pop_if(b, NULL);

      nir_def *upload_off = nir_iadd(b, nir_load_var(b, upload_offset), vbo_offset);
      nir_build_store_global(b, nir_load_var(b, vbo_data), nir_iadd(b, cs->va, nir_u2u64(b, upload_off)),
                             .access = ACCESS_NON_READABLE);
      nir_store_var(b, vbo_idx, nir_iadd_imm(b, nir_load_var(b, vbo_idx), 1), 0x1);
   }
   nir_pop_loop(b, NULL);

   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(PKT3_SET_SH_REG, 1, 0));
   dgc_cs_emit(load_param16(b, vbo_reg));
   dgc_cs_emit(nir_iadd(b, load_param32(b, upload_addr), nir_load_var(b, upload_offset)));
   dgc_cs_end();

   nir_store_var(b, upload_offset, nir_iadd(b, nir_load_var(b, upload_offset), nir_imul_imm(b, vbo_cnt, 16)), 0x1);
}

/**
 * For emitting VK_INDIRECT_COMMANDS_TOKEN_TYPE_DISPATCH_NV.
 */
static nir_def *
dgc_get_grid_sgpr(nir_builder *b, nir_def *stream_addr)
{
   nir_def *res1, *res2;

   nir_push_if(b, nir_ieq_imm(b, load_param8(b, bind_pipeline), 1));
   {
      nir_def *pipeline_va = dgc_get_pipeline_va(b, stream_addr);

      res1 = load_metadata32(b, grid_base_sgpr);
   }
   nir_push_else(b, 0);
   {
      res2 = load_param16(b, grid_base_sgpr);
   }
   nir_pop_if(b, 0);

   return nir_if_phi(b, res1, res2);
}

static nir_def *
dgc_get_dispatch_initiator(nir_builder *b, nir_def *stream_addr)
{
   nir_def *res1, *res2;

   nir_push_if(b, nir_ieq_imm(b, load_param8(b, bind_pipeline), 1));
   {
      nir_def *pipeline_va = dgc_get_pipeline_va(b, stream_addr);

      nir_def *dispatch_initiator = load_param32(b, dispatch_initiator);
      nir_def *wave32 = nir_ieq_imm(b, load_metadata32(b, wave32), 1);
      res1 = nir_bcsel(b, wave32, nir_ior_imm(b, dispatch_initiator, S_00B800_CS_W32_EN(1)), dispatch_initiator);
   }
   nir_push_else(b, 0);
   {
      res2 = load_param32(b, dispatch_initiator);
   }
   nir_pop_if(b, 0);

   return nir_if_phi(b, res1, res2);
}

static void
dgc_emit_dispatch(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_def *dispatch_params_offset, nir_def *sequence_id)
{
   const struct radv_device *device = cs->dev;
   nir_builder *b = cs->b;

   nir_def *dispatch_data = nir_build_load_global(
      b, 3, 32, nir_iadd(b, stream_addr, nir_u2u64(b, dispatch_params_offset)), .access = ACCESS_NON_WRITEABLE);
   nir_def *wg_x = nir_channel(b, dispatch_data, 0);
   nir_def *wg_y = nir_channel(b, dispatch_data, 1);
   nir_def *wg_z = nir_channel(b, dispatch_data, 2);

   nir_push_if(b, nir_iand(b, nir_ine_imm(b, wg_x, 0), nir_iand(b, nir_ine_imm(b, wg_y, 0), nir_ine_imm(b, wg_z, 0))));
   {
      nir_def *grid_sgpr = dgc_get_grid_sgpr(b, stream_addr);
      nir_push_if(b, nir_ine_imm(b, grid_sgpr, 0));
      {
         if (device->load_grid_size_from_user_sgpr) {
            dgc_emit_grid_size_user_sgpr(cs, grid_sgpr, wg_x, wg_y, wg_z);
         } else {
            dgc_emit_grid_size_pointer(cs, grid_sgpr, stream_addr, dispatch_params_offset);
         }
      }
      nir_pop_if(b, 0);

      dgc_emit_sqtt_begin_api_marker(cs, ApiCmdDispatch);
      dgc_emit_sqtt_marker_event_with_dims(cs, sequence_id, wg_x, wg_y, wg_z, EventCmdDispatch);

      nir_def *dispatch_initiator = dgc_get_dispatch_initiator(b, stream_addr);
      dgc_emit_dispatch_direct(cs, wg_x, wg_y, wg_z, dispatch_initiator);

      dgc_emit_sqtt_thread_trace_marker(cs);
      dgc_emit_sqtt_end_api_marker(cs, ApiCmdDispatch);
   }
   nir_pop_if(b, 0);
}

/**
 * Emit VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_NV.
 */
static void
dgc_emit_dispatch_taskmesh_gfx(struct dgc_cmdbuf *cs)
{
   const struct radv_device *device = cs->dev;
   const struct radv_physical_device *pdev = radv_device_physical(device);
   nir_builder *b = cs->b;

   nir_def *vtx_base_sgpr = load_param16(b, vtx_base_sgpr);
   nir_def *has_grid_size = nir_test_mask(b, vtx_base_sgpr, DGC_USES_GRID_SIZE);
   nir_def *has_linear_dispatch_en = nir_ieq_imm(b, load_param8(b, linear_dispatch_en), 1);

   nir_def *base_reg = nir_iand_imm(b, vtx_base_sgpr, 0x3FFF);
   nir_def *xyz_dim_reg = nir_bcsel(b, has_grid_size, base_reg, nir_imm_int(b, 0));
   nir_def *ring_entry_reg = load_param16(b, mesh_ring_entry_sgpr);

   nir_def *xyz_dim_enable = nir_bcsel(b, has_grid_size, nir_imm_int(b, S_4D1_XYZ_DIM_ENABLE(1)), nir_imm_int(b, 0));
   nir_def *mode1_enable = nir_imm_int(b, S_4D1_MODE1_ENABLE(!pdev->mesh_fast_launch_2));
   nir_def *linear_dispatch_en =
      nir_bcsel(b, has_linear_dispatch_en, nir_imm_int(b, S_4D1_LINEAR_DISPATCH_ENABLE(1)), nir_imm_int(b, 0));
   nir_def *sqtt_enable = nir_imm_int(b, device->sqtt.bo ? S_4D1_THREAD_TRACE_MARKER_ENABLE(1) : 0);

   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(PKT3_DISPATCH_TASKMESH_GFX, 2, 0) | PKT3_RESET_FILTER_CAM_S(1));
   /* S_4D0_RING_ENTRY_REG(ring_entry_reg) | S_4D0_XYZ_DIM_REG(xyz_dim_reg) */
   dgc_cs_emit(nir_ior(b, xyz_dim_reg, nir_ishl_imm(b, ring_entry_reg, 16)));
   if (pdev->info.gfx_level >= GFX11) {
      dgc_cs_emit(nir_ior(b, xyz_dim_enable, nir_ior(b, mode1_enable, nir_ior(b, linear_dispatch_en, sqtt_enable))));
   } else {
      dgc_cs_emit(sqtt_enable);
   }
   dgc_cs_emit_imm(V_0287F0_DI_SRC_SEL_AUTO_INDEX);
   dgc_cs_end();
}

static void
dgc_emit_draw_mesh_tasks_gfx(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_def *draw_params_offset,
                             nir_def *sequence_id)
{
   const struct radv_device *device = cs->dev;
   const struct radv_physical_device *pdev = radv_device_physical(device);
   nir_builder *b = cs->b;

   nir_def *draw_data = nir_build_load_global(b, 3, 32, nir_iadd(b, stream_addr, nir_u2u64(b, draw_params_offset)),
                                              .access = ACCESS_NON_WRITEABLE);
   nir_def *x = nir_channel(b, draw_data, 0);
   nir_def *y = nir_channel(b, draw_data, 1);
   nir_def *z = nir_channel(b, draw_data, 2);

   nir_push_if(b, nir_iand(b, nir_ine_imm(b, x, 0), nir_iand(b, nir_ine_imm(b, y, 0), nir_ine_imm(b, z, 0))));
   {
      dgc_emit_sqtt_begin_api_marker(cs, ApiCmdDrawMeshTasksEXT);
      dgc_emit_sqtt_marker_event(cs, sequence_id, EventCmdDrawMeshTasksEXT);

      nir_push_if(b, nir_ieq_imm(b, load_param8(b, has_task_shader), 1));
      {
         dgc_emit_dispatch_taskmesh_gfx(cs);
      }
      nir_push_else(b, NULL);
      {
         dgc_emit_userdata_mesh(cs, x, y, z, sequence_id);
         dgc_emit_instance_count(cs, nir_imm_int(b, 1));

         if (pdev->mesh_fast_launch_2) {
            dgc_emit_dispatch_mesh_direct(cs, x, y, z);
         } else {
            nir_def *vertex_count = nir_imul(b, x, nir_imul(b, y, z));
            dgc_emit_draw_index_auto(cs, vertex_count);
         }

         dgc_emit_sqtt_thread_trace_marker(cs);
         dgc_emit_sqtt_end_api_marker(cs, ApiCmdDrawMeshTasksEXT);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);
}

static void
dgc_emit_userdata_task(struct dgc_cmdbuf *ace_cs, nir_def *x, nir_def *y, nir_def *z)
{
   nir_builder *b = ace_cs->b;

   nir_def *xyz_sgpr = load_param16(b, task_xyz_sgpr);
   nir_push_if(b, nir_ine_imm(b, xyz_sgpr, 0));
   {
      dgc_cs_begin(ace_cs);
      dgc_cs_emit_imm(PKT3(PKT3_SET_SH_REG, 3, 0));
      dgc_cs_emit(xyz_sgpr);
      dgc_cs_emit(x);
      dgc_cs_emit(y);
      dgc_cs_emit(z);
      dgc_cs_end();
   }
   nir_pop_if(b, NULL);

   nir_def *draw_id_sgpr = load_param16(b, task_draw_id_sgpr);
   nir_push_if(b, nir_ine_imm(b, draw_id_sgpr, 0));
   {
      dgc_cs_begin(ace_cs);
      dgc_cs_emit_imm(PKT3(PKT3_SET_SH_REG, 1, 0));
      dgc_cs_emit(draw_id_sgpr);
      dgc_cs_emit_imm(0);
      dgc_cs_end();
   }
   nir_pop_if(b, NULL);
}

static void
dgc_emit_dispatch_taskmesh_direct_ace(struct dgc_cmdbuf *ace_cs, nir_def *x, nir_def *y, nir_def *z)
{
   nir_builder *b = ace_cs->b;

   dgc_cs_begin(ace_cs);
   dgc_cs_emit_imm(PKT3(PKT3_DISPATCH_TASKMESH_DIRECT_ACE, 4, 0) | PKT3_SHADER_TYPE_S(1));
   dgc_cs_emit(x);
   dgc_cs_emit(y);
   dgc_cs_emit(z);
   dgc_cs_emit(load_param32(b, dispatch_initiator_task));
   dgc_cs_emit(load_param16(b, task_ring_entry_sgpr));
   dgc_cs_end();
}

static void
dgc_emit_draw_mesh_tasks_ace(struct dgc_cmdbuf *ace_cs, nir_def *stream_addr, nir_def *draw_params_offset)
{
   nir_builder *b = ace_cs->b;

   nir_def *draw_data = nir_build_load_global(b, 3, 32, nir_iadd(b, stream_addr, nir_u2u64(b, draw_params_offset)),
                                              .access = ACCESS_NON_WRITEABLE);
   nir_def *x = nir_channel(b, draw_data, 0);
   nir_def *y = nir_channel(b, draw_data, 1);
   nir_def *z = nir_channel(b, draw_data, 2);

   nir_push_if(b, nir_iand(b, nir_ine_imm(b, x, 0), nir_iand(b, nir_ine_imm(b, y, 0), nir_ine_imm(b, z, 0))));
   {
      dgc_emit_userdata_task(ace_cs, x, y, z);
      dgc_emit_dispatch_taskmesh_direct_ace(ace_cs, x, y, z);
   }
   nir_pop_if(b, NULL);
}

/**
 * Emit VK_INDIRECT_COMMANDS_TOKEN_TYPE_PIPELINE_NV.
 */
static void
dgc_emit_indirect_sets(struct dgc_cmdbuf *cs, nir_def *pipeline_va)
{
   nir_builder *b = cs->b;

   nir_def *indirect_desc_sets_sgpr = load_metadata32(b, indirect_desc_sets_sgpr);
   nir_push_if(b, nir_ine_imm(b, indirect_desc_sets_sgpr, 0));
   {
      dgc_cs_begin(cs);
      dgc_cs_emit_imm(PKT3(PKT3_SET_SH_REG, 1, 0));
      dgc_cs_emit(indirect_desc_sets_sgpr);
      dgc_cs_emit(load_param32(b, indirect_desc_sets_va));
      dgc_cs_end();
   }
   nir_pop_if(b, NULL);
}

static void
dgc_emit_bind_pipeline(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_variable *upload_offset)
{
   const struct radv_device *device = cs->dev;
   const struct radv_physical_device *pdev = radv_device_physical(device);
   nir_builder *b = cs->b;

   nir_def *pipeline_va = dgc_get_pipeline_va(b, stream_addr);

   dgc_cs_begin(cs);
   dgc_cs_set_sh_reg_seq(R_00B830_COMPUTE_PGM_LO, 1);
   dgc_cs_emit(load_metadata32(b, shader_va));

   dgc_cs_set_sh_reg_seq(R_00B848_COMPUTE_PGM_RSRC1, 2);
   dgc_cs_emit(load_metadata32(b, rsrc1));
   dgc_cs_emit(load_metadata32(b, rsrc2));

   if (pdev->info.gfx_level >= GFX10) {
      dgc_cs_set_sh_reg_seq(R_00B8A0_COMPUTE_PGM_RSRC3, 1);
      dgc_cs_emit(load_metadata32(b, rsrc3));
   }

   dgc_cs_set_sh_reg_seq(R_00B854_COMPUTE_RESOURCE_LIMITS, 1);
   dgc_cs_emit(load_metadata32(b, compute_resource_limits));

   dgc_cs_set_sh_reg_seq(R_00B81C_COMPUTE_NUM_THREAD_X, 3);
   dgc_cs_emit(load_metadata32(b, block_size_x));
   dgc_cs_emit(load_metadata32(b, block_size_y));
   dgc_cs_emit(load_metadata32(b, block_size_z));
   dgc_cs_end();

   dgc_emit_indirect_sets(cs, pipeline_va);

   nir_store_var(b, upload_offset, nir_iadd_imm(b, nir_load_var(b, upload_offset), MAX_SETS * 4), 0x1);
}

static nir_def *
dgc_is_cond_render_enabled(nir_builder *b)
{
   nir_def *res1, *res2;

   nir_push_if(b, nir_ieq_imm(b, load_param8(b, predicating), 1));
   {
      nir_def *val = nir_load_global(b, load_param64(b, predication_va), 4, 1, 32);
      /* By default, all rendering commands are discarded if the 32-bit value is zero. If the
       * inverted flag is set, they are discarded if the value is non-zero.
       */
      res1 = nir_ixor(b, nir_i2b(b, load_param8(b, predication_type)), nir_ine_imm(b, val, 0));
   }
   nir_push_else(b, 0);
   {
      res2 = nir_imm_bool(b, false);
   }
   nir_pop_if(b, 0);

   return nir_if_phi(b, res1, res2);
}

static void
dgc_pad_cmdbuf(struct dgc_cmdbuf *cs, nir_def *cmd_buf_end)
{
   const struct radv_device *device = cs->dev;
   const struct radv_physical_device *pdev = radv_device_physical(device);
   nir_builder *b = cs->b;

   nir_push_if(b, nir_ine(b, nir_load_var(b, cs->offset), cmd_buf_end));
   {
      if (pdev->info.gfx_ib_pad_with_type2) {
         nir_push_loop(b);
         {
            nir_def *curr_offset = nir_load_var(b, cs->offset);

            nir_push_if(b, nir_ieq(b, curr_offset, cmd_buf_end));
            {
               nir_jump(b, nir_jump_break);
            }
            nir_pop_if(b, NULL);

            nir_def *pkt = nir_imm_int(b, PKT2_NOP_PAD);

            dgc_cs_begin(cs);
            dgc_cs_emit(pkt);
            dgc_cs_end();
         }
         nir_pop_loop(b, NULL);
      } else {
         nir_def *cnt = nir_isub(b, cmd_buf_end, nir_load_var(b, cs->offset));
         cnt = nir_ushr_imm(b, cnt, 2);
         cnt = nir_iadd_imm(b, cnt, -2);
         nir_def *pkt = nir_pkt3(b, PKT3_NOP, cnt);

         dgc_cs_begin(cs);
         dgc_cs_emit(pkt);
         dgc_cs_end();
      }
   }
   nir_pop_if(b, NULL);
}

static nir_shader *
build_dgc_prepare_shader(struct radv_device *dev)
{
   const struct radv_physical_device *pdev = radv_device_physical(dev);
   nir_builder b = radv_meta_init_shader(dev, MESA_SHADER_COMPUTE, "meta_dgc_prepare");
   b.shader->info.workgroup_size[0] = 64;

   nir_def *global_id = get_global_ids(&b, 1);

   nir_def *sequence_id = global_id;

   nir_def *cmd_buf_stride = load_param32(&b, cmd_buf_stride);
   nir_def *sequence_count = load_param32(&b, sequence_count);
   nir_def *stream_stride = load_param32(&b, stream_stride);

   nir_def *use_count = nir_iand_imm(&b, sequence_count, 1u << 31);
   sequence_count = nir_iand_imm(&b, sequence_count, UINT32_MAX >> 1);

   nir_def *cmd_buf_base_offset = load_param32(&b, cmd_buf_main_offset);

   /* The effective number of draws is
    * min(sequencesCount, sequencesCountBuffer[sequencesCountOffset]) when
    * using sequencesCountBuffer. Otherwise it is sequencesCount. */
   nir_variable *count_var = nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "sequence_count");
   nir_store_var(&b, count_var, sequence_count, 0x1);

   nir_push_if(&b, nir_ine_imm(&b, use_count, 0));
   {
      nir_def *cnt =
         nir_build_load_global(&b, 1, 32, load_param64(&b, sequence_count_addr), .access = ACCESS_NON_WRITEABLE);
      /* Must clamp count against the API count explicitly.
       * The workgroup potentially contains more threads than maxSequencesCount from API,
       * and we have to ensure these threads write NOP packets to pad out the IB. */
      cnt = nir_umin(&b, cnt, sequence_count);
      nir_store_var(&b, count_var, cnt, 0x1);
   }
   nir_pop_if(&b, NULL);

   nir_push_if(&b, dgc_is_cond_render_enabled(&b));
   {
      /* Reset the number of sequences when conditional rendering is enabled in order to skip the
       * entire shader and pad the cmdbuf with NOPs.
       */
      nir_store_var(&b, count_var, nir_imm_int(&b, 0), 0x1);
   }
   nir_pop_if(&b, NULL);

   sequence_count = nir_load_var(&b, count_var);

   nir_push_if(&b, nir_ult(&b, sequence_id, sequence_count));
   {
      struct dgc_cmdbuf cmd_buf = {
         .b = &b,
         .dev = dev,
         .va = nir_pack_64_2x32_split(&b, load_param32(&b, upload_addr), nir_imm_int(&b, pdev->info.address32_hi)),
         .offset = nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "cmd_buf_offset"),
      };
      nir_store_var(&b, cmd_buf.offset, nir_iadd(&b, nir_imul(&b, global_id, cmd_buf_stride), cmd_buf_base_offset), 1);
      nir_def *cmd_buf_end = nir_iadd(&b, nir_load_var(&b, cmd_buf.offset), cmd_buf_stride);

      nir_def *stream_addr = load_param64(&b, stream_addr);
      stream_addr = nir_iadd(&b, stream_addr, nir_u2u64(&b, nir_imul(&b, sequence_id, stream_stride)));

      nir_variable *upload_offset =
         nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "upload_offset");
      nir_def *upload_offset_init =
         nir_iadd(&b, load_param32(&b, upload_main_offset), nir_imul(&b, load_param32(&b, upload_stride), sequence_id));
      nir_store_var(&b, upload_offset, upload_offset_init, 0x1);

      nir_def *vbo_bind_mask = load_param32(&b, vbo_bind_mask);
      nir_push_if(&b, nir_ine_imm(&b, vbo_bind_mask, 0));
      {
         dgc_emit_vertex_buffer(&cmd_buf, stream_addr, vbo_bind_mask, upload_offset);
      }
      nir_pop_if(&b, NULL);

      nir_def *push_const_mask = load_param64(&b, push_constant_mask);
      nir_push_if(&b, nir_ine_imm(&b, push_const_mask, 0));
      {
         const VkShaderStageFlags stages =
            VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_MESH_BIT_EXT;

         dgc_emit_push_constant(&cmd_buf, stream_addr, push_const_mask, upload_offset, stages);
      }
      nir_pop_if(&b, 0);

      nir_push_if(&b, nir_ieq_imm(&b, load_param8(&b, bind_pipeline), 1));
      {
         dgc_emit_bind_pipeline(&cmd_buf, stream_addr, upload_offset);
      }
      nir_pop_if(&b, 0);

      nir_push_if(&b, nir_ieq_imm(&b, load_param8(&b, is_dispatch), 0));
      {
         nir_push_if(&b, nir_ieq_imm(&b, load_param16(&b, draw_indexed), 0));
         {
            nir_def *draw_mesh_tasks = load_param8(&b, draw_mesh_tasks);
            nir_push_if(&b, nir_ieq_imm(&b, draw_mesh_tasks, 0));
            {
               dgc_emit_draw(&cmd_buf, stream_addr, load_param16(&b, draw_params_offset), sequence_id);
            }
            nir_push_else(&b, NULL);
            {
               dgc_emit_draw_mesh_tasks_gfx(&cmd_buf, stream_addr, load_param16(&b, draw_params_offset), sequence_id);
            }
            nir_pop_if(&b, NULL);
         }
         nir_push_else(&b, NULL);
         {
            /* Emit direct draws when index buffers are also updated by DGC. Otherwise, emit
             * indirect draws to remove the dependency on the cmdbuf state in order to enable
             * preprocessing.
             */
            nir_def *binds_index_buffer = nir_ine_imm(&b, load_param16(&b, binds_index_buffer), 0);
            nir_push_if(&b, binds_index_buffer);
            {
               nir_variable *max_index_count_var =
                  nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "max_index_count");

               dgc_emit_index_buffer(&cmd_buf, stream_addr, load_param16(&b, index_buffer_offset),
                                     load_param32(&b, ibo_type_32), load_param32(&b, ibo_type_8), max_index_count_var);

               nir_def *max_index_count = nir_load_var(&b, max_index_count_var);

               dgc_emit_draw_indexed(&cmd_buf, stream_addr, load_param16(&b, draw_params_offset), sequence_id,
                                     max_index_count);
            }
            nir_push_else(&b, NULL);
            {
               dgc_emit_draw_indirect(&cmd_buf, stream_addr, load_param16(&b, draw_params_offset), sequence_id, true);
            }

            nir_pop_if(&b, NULL);
         }
         nir_pop_if(&b, NULL);
      }
      nir_push_else(&b, NULL);
      {
         dgc_emit_dispatch(&cmd_buf, stream_addr, load_param16(&b, dispatch_params_offset), sequence_id);
      }
      nir_pop_if(&b, NULL);

      /* Pad the cmdbuffer if we did not use the whole stride */
      dgc_pad_cmdbuf(&cmd_buf, cmd_buf_end);
   }
   nir_pop_if(&b, NULL);

   build_dgc_buffer_tail_gfx(&b, sequence_count, dev);
   build_dgc_buffer_preamble_gfx(&b, sequence_count, dev);

   /* Prepare the ACE command stream */
   nir_push_if(&b, nir_ieq_imm(&b, load_param8(&b, has_task_shader), 1));
   {
      nir_def *ace_cmd_buf_stride = load_param32(&b, ace_cmd_buf_stride);
      nir_def *ace_cmd_buf_base_offset = load_param32(&b, ace_cmd_buf_main_offset);

      nir_push_if(&b, nir_ult(&b, sequence_id, sequence_count));
      {
         struct dgc_cmdbuf cmd_buf = {
            .b = &b,
            .dev = dev,
            .va = nir_pack_64_2x32_split(&b, load_param32(&b, upload_addr), nir_imm_int(&b, pdev->info.address32_hi)),
            .offset = nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "cmd_buf_offset"),
         };
         nir_store_var(&b, cmd_buf.offset,
                       nir_iadd(&b, nir_imul(&b, global_id, ace_cmd_buf_stride), ace_cmd_buf_base_offset), 1);
         nir_def *cmd_buf_end = nir_iadd(&b, nir_load_var(&b, cmd_buf.offset), ace_cmd_buf_stride);

         nir_def *stream_addr = load_param64(&b, stream_addr);
         stream_addr = nir_iadd(&b, stream_addr, nir_u2u64(&b, nir_imul(&b, sequence_id, stream_stride)));

         nir_variable *upload_offset =
            nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "upload_offset");
         nir_def *upload_offset_init = nir_iadd(&b, load_param32(&b, upload_main_offset),
                                                nir_imul(&b, load_param32(&b, upload_stride), sequence_id));
         nir_store_var(&b, upload_offset, upload_offset_init, 0x1);

         nir_def *push_const_mask = load_param64(&b, push_constant_mask);
         nir_push_if(&b, nir_ine_imm(&b, push_const_mask, 0));
         {
            nir_def *push_constant_stages = dgc_get_push_constant_stages(&b, stream_addr);

            nir_push_if(&b, nir_test_mask(&b, push_constant_stages, VK_SHADER_STAGE_TASK_BIT_EXT));
            {
               const struct dgc_pc_params params = dgc_get_pc_params(&b);
               dgc_emit_push_constant_for_stage(&cmd_buf, stream_addr, push_const_mask, &params, MESA_SHADER_TASK,
                                                upload_offset);
            }
            nir_pop_if(&b, NULL);
         }
         nir_pop_if(&b, 0);

         dgc_emit_draw_mesh_tasks_ace(&cmd_buf, stream_addr, load_param16(&b, draw_params_offset));

         /* Pad the cmdbuffer if we did not use the whole stride */
         dgc_pad_cmdbuf(&cmd_buf, cmd_buf_end);
      }
      nir_pop_if(&b, NULL);

      build_dgc_buffer_tail_ace(&b, sequence_count, dev);
      build_dgc_buffer_preamble_ace(&b, sequence_count, dev);
   }
   nir_pop_if(&b, NULL);

   return b.shader;
}

void
radv_device_finish_dgc_prepare_state(struct radv_device *device)
{
   radv_DestroyPipeline(radv_device_to_handle(device), device->meta_state.dgc_prepare.pipeline,
                        &device->meta_state.alloc);
   radv_DestroyPipelineLayout(radv_device_to_handle(device), device->meta_state.dgc_prepare.p_layout,
                              &device->meta_state.alloc);
   device->vk.dispatch_table.DestroyDescriptorSetLayout(
      radv_device_to_handle(device), device->meta_state.dgc_prepare.ds_layout, &device->meta_state.alloc);
}

VkResult
radv_device_init_dgc_prepare_state(struct radv_device *device)
{
   VkResult result;

   const VkDescriptorSetLayoutBinding binding = {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
   };

   result = radv_meta_create_descriptor_set_layout(device, 1, &binding, &device->meta_state.dgc_prepare.ds_layout);
   if (result != VK_SUCCESS)
      return result;

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .size = sizeof(struct radv_dgc_params),
   };

   result = radv_meta_create_pipeline_layout(device, &device->meta_state.dgc_prepare.ds_layout, 1, &pc_range,
                                             &device->meta_state.dgc_prepare.p_layout);
   if (result != VK_SUCCESS)
      return result;

   nir_shader *cs = build_dgc_prepare_shader(device);

   result = radv_meta_create_compute_pipeline(device, cs, device->meta_state.dgc_prepare.p_layout,
                                              &device->meta_state.dgc_prepare.pipeline);
   ralloc_free(cs);
   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateIndirectCommandsLayoutNV(VkDevice _device, const VkIndirectCommandsLayoutCreateInfoNV *pCreateInfo,
                                    const VkAllocationCallbacks *pAllocator,
                                    VkIndirectCommandsLayoutNV *pIndirectCommandsLayout)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   struct radv_indirect_command_layout *layout;

   size_t size = sizeof(*layout) + pCreateInfo->tokenCount * sizeof(VkIndirectCommandsLayoutTokenNV);

   layout = vk_zalloc2(&device->vk.alloc, pAllocator, size, alignof(struct radv_indirect_command_layout),
                       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!layout)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &layout->base, VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_NV);

   layout->flags = pCreateInfo->flags;
   layout->pipeline_bind_point = pCreateInfo->pipelineBindPoint;
   layout->input_stride = pCreateInfo->pStreamStrides[0];
   layout->token_count = pCreateInfo->tokenCount;
   typed_memcpy(layout->tokens, pCreateInfo->pTokens, pCreateInfo->tokenCount);

   layout->ibo_type_32 = VK_INDEX_TYPE_UINT32;
   layout->ibo_type_8 = VK_INDEX_TYPE_UINT8_KHR;

   for (unsigned i = 0; i < pCreateInfo->tokenCount; ++i) {
      switch (pCreateInfo->pTokens[i].tokenType) {
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_NV:
         layout->draw_params_offset = pCreateInfo->pTokens[i].offset;
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_NV:
         layout->indexed = true;
         layout->draw_params_offset = pCreateInfo->pTokens[i].offset;
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DISPATCH_NV:
         layout->dispatch_params_offset = pCreateInfo->pTokens[i].offset;
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_INDEX_BUFFER_NV:
         layout->binds_index_buffer = true;
         layout->index_buffer_offset = pCreateInfo->pTokens[i].offset;
         /* 16-bit is implied if we find no match. */
         for (unsigned j = 0; j < pCreateInfo->pTokens[i].indexTypeCount; j++) {
            if (pCreateInfo->pTokens[i].pIndexTypes[j] == VK_INDEX_TYPE_UINT32)
               layout->ibo_type_32 = pCreateInfo->pTokens[i].pIndexTypeValues[j];
            else if (pCreateInfo->pTokens[i].pIndexTypes[j] == VK_INDEX_TYPE_UINT8_KHR)
               layout->ibo_type_8 = pCreateInfo->pTokens[i].pIndexTypeValues[j];
         }
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_NV:
         layout->bind_vbo_mask |= 1u << pCreateInfo->pTokens[i].vertexBindingUnit;
         layout->vbo_offsets[pCreateInfo->pTokens[i].vertexBindingUnit] = pCreateInfo->pTokens[i].offset;
         if (pCreateInfo->pTokens[i].vertexDynamicStride)
            layout->vbo_offsets[pCreateInfo->pTokens[i].vertexBindingUnit] |= DGC_DYNAMIC_STRIDE;
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_NV: {
         VK_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, pCreateInfo->pTokens[i].pushconstantPipelineLayout);
         for (unsigned j = pCreateInfo->pTokens[i].pushconstantOffset / 4, k = 0;
              k < pCreateInfo->pTokens[i].pushconstantSize / 4; ++j, ++k) {
            layout->push_constant_mask |= 1ull << j;
            layout->push_constant_offsets[j] = pCreateInfo->pTokens[i].offset + k * 4;
         }
         layout->push_constant_size = pipeline_layout->push_constant_size;
         assert(!pipeline_layout->dynamic_offset_count);
         break;
      }
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_NV:
         layout->draw_mesh_tasks = true;
         layout->draw_params_offset = pCreateInfo->pTokens[i].offset;
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_PIPELINE_NV:
         layout->bind_pipeline = true;
         layout->pipeline_params_offset = pCreateInfo->pTokens[i].offset;
         break;
      default:
         unreachable("Unhandled token type");
      }
   }
   if (!layout->indexed)
      layout->binds_index_buffer = false;

   *pIndirectCommandsLayout = radv_indirect_command_layout_to_handle(layout);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyIndirectCommandsLayoutNV(VkDevice _device, VkIndirectCommandsLayoutNV indirectCommandsLayout,
                                     const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(radv_indirect_command_layout, layout, indirectCommandsLayout);

   if (!layout)
      return;

   vk_object_base_finish(&layout->base);
   vk_free2(&device->vk.alloc, pAllocator, layout);
}

VKAPI_ATTR void VKAPI_CALL
radv_GetGeneratedCommandsMemoryRequirementsNV(VkDevice _device,
                                              const VkGeneratedCommandsMemoryRequirementsInfoNV *pInfo,
                                              VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   VK_FROM_HANDLE(radv_indirect_command_layout, layout, pInfo->indirectCommandsLayout);
   VK_FROM_HANDLE(radv_pipeline, pipeline, pInfo->pipeline);

   uint32_t cmd_stride, ace_cmd_stride, upload_stride;
   radv_get_sequence_size(layout, pipeline, &cmd_stride, &ace_cmd_stride, &upload_stride);

   VkDeviceSize cmd_buf_size = radv_align_cmdbuf_size(device, cmd_stride * pInfo->maxSequencesCount, AMD_IP_GFX) +
                               radv_dgc_preamble_cmdbuf_size(device, AMD_IP_GFX);

   if (ace_cmd_stride) {
      cmd_buf_size += radv_align_cmdbuf_size(device, ace_cmd_stride * pInfo->maxSequencesCount, AMD_IP_COMPUTE) +
                      radv_dgc_preamble_cmdbuf_size(device, AMD_IP_COMPUTE);
   }

   VkDeviceSize upload_buf_size = upload_stride * pInfo->maxSequencesCount;

   pMemoryRequirements->memoryRequirements.memoryTypeBits = pdev->memory_types_32bit;
   pMemoryRequirements->memoryRequirements.alignment =
      MAX2(pdev->info.ip[AMD_IP_GFX].ib_alignment, pdev->info.ip[AMD_IP_COMPUTE].ib_alignment);
   pMemoryRequirements->memoryRequirements.size =
      align(cmd_buf_size + upload_buf_size, pMemoryRequirements->memoryRequirements.alignment);
}

bool
radv_dgc_with_task_shader(const VkGeneratedCommandsInfoNV *pGeneratedCommandsInfo)
{
   VK_FROM_HANDLE(radv_indirect_command_layout, layout, pGeneratedCommandsInfo->indirectCommandsLayout);

   if (layout->pipeline_bind_point != VK_PIPELINE_BIND_POINT_GRAPHICS)
      return false;

   if (!layout->draw_mesh_tasks)
      return false;

   VK_FROM_HANDLE(radv_pipeline, pipeline, pGeneratedCommandsInfo->pipeline);
   const struct radv_shader *task_shader = radv_get_shader(pipeline->shaders, MESA_SHADER_TASK);
   if (!task_shader)
      return false;

   return true;
}

bool
radv_use_dgc_predication(struct radv_cmd_buffer *cmd_buffer, const VkGeneratedCommandsInfoNV *pGeneratedCommandsInfo)
{
   VK_FROM_HANDLE(radv_buffer, seq_count_buffer, pGeneratedCommandsInfo->sequencesCountBuffer);

   /* Enable conditional rendering (if not enabled by user) to skip prepare/execute DGC calls when
    * the indirect sequence count might be zero. This can only be enabled on GFX because on ACE it's
    * not possible to skip the execute DGC call (ie. no INDIRECT_PACKET). It should also be disabled
    * when the graphics pipelines has a task shader for the same reason (otherwise the DGC ACE IB
    * would be uninitialized).
    */
   return cmd_buffer->qf == RADV_QUEUE_GENERAL && !radv_dgc_with_task_shader(pGeneratedCommandsInfo) &&
          seq_count_buffer && !cmd_buffer->state.predicating;
}

static bool
radv_dgc_need_push_constants_copy(const struct radv_pipeline *pipeline)
{
   for (unsigned i = 0; i < ARRAY_SIZE(pipeline->shaders); ++i) {
      const struct radv_shader *shader = pipeline->shaders[i];

      if (!shader)
         continue;

      const struct radv_userdata_locations *locs = &shader->info.user_sgprs_locs;
      if (locs->shader_data[AC_UD_PUSH_CONSTANTS].sgpr_idx >= 0)
         return true;
   }

   return false;
}

bool
radv_dgc_can_preprocess(const struct radv_indirect_command_layout *layout, struct radv_pipeline *pipeline)
{
   if (!(layout->flags & VK_INDIRECT_COMMANDS_LAYOUT_USAGE_EXPLICIT_PREPROCESS_BIT_NV))
      return false;

   /* From the Vulkan spec (1.3.269, chapter 32):
    * "The bound descriptor sets and push constants that will be used with indirect command generation for the compute
    * piplines must already be specified at the time of preprocessing commands with vkCmdPreprocessGeneratedCommandsNV.
    * They must not change until the execution of indirect commands is submitted with vkCmdExecuteGeneratedCommandsNV."
    *
    * So we can always preprocess compute layouts.
    */
   if (layout->pipeline_bind_point != VK_PIPELINE_BIND_POINT_COMPUTE) {
      /* VBO binding (in particular partial VBO binding) uses some draw state which we don't generate at preprocess time
       * yet. */
      if (layout->bind_vbo_mask)
         return false;

      /* Do not preprocess when all push constants can't be inlined because they need to be copied
       * to the upload BO.
       */
      if (layout->push_constant_mask && radv_dgc_need_push_constants_copy(pipeline))
         return false;
   }

   return true;
}

/* Always need to call this directly before draw due to dependence on bound state. */
static void
radv_prepare_dgc_graphics(struct radv_cmd_buffer *cmd_buffer, const VkGeneratedCommandsInfoNV *pGeneratedCommandsInfo,
                          unsigned *upload_size, unsigned *upload_offset, void **upload_data,
                          struct radv_dgc_params *params)
{
   VK_FROM_HANDLE(radv_indirect_command_layout, layout, pGeneratedCommandsInfo->indirectCommandsLayout);
   VK_FROM_HANDLE(radv_pipeline, pipeline, pGeneratedCommandsInfo->pipeline);
   const struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_graphics_pipeline *graphics_pipeline = radv_pipeline_to_graphics(pipeline);
   struct radv_shader *vs = radv_get_shader(graphics_pipeline->base.shaders, MESA_SHADER_VERTEX);
   unsigned vb_size = layout->bind_vbo_mask ? util_bitcount(vs->info.vs.vb_desc_usage_mask) * 24 : 0;

   *upload_size = MAX2(*upload_size + vb_size, 16);

   if (!radv_cmd_buffer_upload_alloc(cmd_buffer, *upload_size, upload_offset, upload_data)) {
      vk_command_buffer_set_error(&cmd_buffer->vk, VK_ERROR_OUT_OF_HOST_MEMORY);
      return;
   }

   uint16_t vtx_base_sgpr = 0;

   if (graphics_pipeline->vtx_base_sgpr)
      vtx_base_sgpr = (graphics_pipeline->vtx_base_sgpr - SI_SH_REG_OFFSET) >> 2;

   if (graphics_pipeline->uses_drawid)
      vtx_base_sgpr |= DGC_USES_DRAWID;

   if (layout->draw_mesh_tasks) {
      struct radv_shader *mesh_shader = radv_get_shader(graphics_pipeline->base.shaders, MESA_SHADER_MESH);
      const struct radv_shader *task_shader = radv_get_shader(graphics_pipeline->base.shaders, MESA_SHADER_TASK);

      if (mesh_shader->info.cs.uses_grid_size)
         vtx_base_sgpr |= DGC_USES_GRID_SIZE;

      if (task_shader) {
         params->has_task_shader = 1;
         params->mesh_ring_entry_sgpr = radv_get_user_sgpr(mesh_shader, AC_UD_TASK_RING_ENTRY);
         params->linear_dispatch_en = task_shader->info.cs.linear_taskmesh_dispatch;
         params->task_ring_entry_sgpr = radv_get_user_sgpr(task_shader, AC_UD_TASK_RING_ENTRY);
         params->dispatch_initiator_task =
            device->dispatch_initiator_task | S_00B800_CS_W32_EN(task_shader->info.wave_size == 32);
         params->task_xyz_sgpr = radv_get_user_sgpr(task_shader, AC_UD_CS_GRID_SIZE);
         params->task_draw_id_sgpr = radv_get_user_sgpr(task_shader, AC_UD_CS_TASK_DRAW_ID);
      }
   } else {
      if (graphics_pipeline->uses_baseinstance)
         vtx_base_sgpr |= DGC_USES_BASEINSTANCE;
   }

   params->draw_indexed = layout->indexed;
   params->draw_params_offset = layout->draw_params_offset;
   params->binds_index_buffer = layout->binds_index_buffer;
   params->vtx_base_sgpr = vtx_base_sgpr;
   params->max_index_count = cmd_buffer->state.max_index_count;
   params->index_buffer_offset = layout->index_buffer_offset;
   params->ibo_type_32 = layout->ibo_type_32;
   params->ibo_type_8 = layout->ibo_type_8;
   params->draw_mesh_tasks = layout->draw_mesh_tasks;

   if (layout->bind_vbo_mask) {
      uint32_t mask = vs->info.vs.vb_desc_usage_mask;
      unsigned vb_desc_alloc_size = util_bitcount(mask) * 16;

      radv_write_vertex_descriptors(cmd_buffer, graphics_pipeline, true, *upload_data);

      uint32_t *vbo_info = (uint32_t *)((char *)*upload_data + vb_desc_alloc_size);

      unsigned idx = 0;
      while (mask) {
         unsigned i = u_bit_scan(&mask);
         unsigned binding = vs->info.vs.use_per_attribute_vb_descs ? graphics_pipeline->attrib_bindings[i] : i;
         uint32_t attrib_end = graphics_pipeline->attrib_ends[i];

         params->vbo_bind_mask |= ((layout->bind_vbo_mask >> binding) & 1u) << idx;
         vbo_info[2 * idx] = ((vs->info.vs.use_per_attribute_vb_descs ? 1u : 0u) << 31) | layout->vbo_offsets[binding];
         vbo_info[2 * idx + 1] = graphics_pipeline->attrib_index_offset[i] | (attrib_end << 16);
         ++idx;
      }
      params->vbo_cnt = idx;
      params->vbo_reg = radv_get_user_sgpr(vs, AC_UD_VS_VERTEX_BUFFERS);

      *upload_data = (char *)*upload_data + vb_size;
   }
}

static void
radv_prepare_dgc_compute(struct radv_cmd_buffer *cmd_buffer, const VkGeneratedCommandsInfoNV *pGeneratedCommandsInfo,
                         unsigned *upload_size, unsigned *upload_offset, void **upload_data,
                         struct radv_dgc_params *params, bool cond_render_enabled)
{
   VK_FROM_HANDLE(radv_indirect_command_layout, layout, pGeneratedCommandsInfo->indirectCommandsLayout);
   VK_FROM_HANDLE(radv_pipeline, pipeline, pGeneratedCommandsInfo->pipeline);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const uint32_t desc_size = pipeline ? 0 : MAX_SETS * 4;

   *upload_size = MAX2(*upload_size + desc_size, 16);

   if (!radv_cmd_buffer_upload_alloc(cmd_buffer, *upload_size, upload_offset, upload_data)) {
      vk_command_buffer_set_error(&cmd_buffer->vk, VK_ERROR_OUT_OF_HOST_MEMORY);
      return;
   }

   params->dispatch_params_offset = layout->dispatch_params_offset;
   params->dispatch_initiator = device->dispatch_initiator | S_00B800_FORCE_START_AT_000(1);
   params->is_dispatch = 1;

   if (cond_render_enabled) {
      params->predicating = true;
      params->predication_va = cmd_buffer->state.predication_va;
      params->predication_type = cmd_buffer->state.predication_type;
   }

   if (pipeline) {
      struct radv_compute_pipeline *compute_pipeline = radv_pipeline_to_compute(pipeline);
      struct radv_shader *cs = radv_get_shader(compute_pipeline->base.shaders, MESA_SHADER_COMPUTE);

      if (cs->info.wave_size == 32) {
         assert(pdev->info.gfx_level >= GFX10);
         params->dispatch_initiator |= S_00B800_CS_W32_EN(1);
      }

      params->grid_base_sgpr = radv_get_user_sgpr(cs, AC_UD_CS_GRID_SIZE);
   } else {
      struct radv_descriptor_state *descriptors_state =
         radv_get_descriptors_state(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE);

      params->bind_pipeline = 1;
      params->pipeline_params_offset = layout->pipeline_params_offset;

      for (unsigned i = 0; i < MAX_SETS; i++) {
         uint32_t *uptr = ((uint32_t *)*upload_data) + i;
         uint64_t set_va = 0;
         if (descriptors_state->valid & (1u << i))
            set_va = radv_descriptor_get_va(descriptors_state, i);

         uptr[0] = set_va & 0xffffffff;
      }

      params->indirect_desc_sets_va = radv_buffer_get_va(cmd_buffer->upload.upload_bo) + *upload_offset;

      *upload_data = (char *)*upload_data + desc_size;
   }
}

void
radv_prepare_dgc(struct radv_cmd_buffer *cmd_buffer, const VkGeneratedCommandsInfoNV *pGeneratedCommandsInfo,
                 bool cond_render_enabled)
{
   VK_FROM_HANDLE(radv_indirect_command_layout, layout, pGeneratedCommandsInfo->indirectCommandsLayout);
   VK_FROM_HANDLE(radv_pipeline, pipeline, pGeneratedCommandsInfo->pipeline);
   VK_FROM_HANDLE(radv_buffer, prep_buffer, pGeneratedCommandsInfo->preprocessBuffer);
   VK_FROM_HANDLE(radv_buffer, stream_buffer, pGeneratedCommandsInfo->pStreams[0].buffer);
   VK_FROM_HANDLE(radv_buffer, sequence_count_buffer, pGeneratedCommandsInfo->sequencesCountBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_meta_saved_state saved_state;
   unsigned upload_offset, upload_size;
   struct radv_buffer token_buffer;
   void *upload_data;

   uint32_t cmd_stride, ace_cmd_stride, upload_stride;
   radv_get_sequence_size(layout, pipeline, &cmd_stride, &ace_cmd_stride, &upload_stride);

   unsigned cmd_buf_size =
      radv_align_cmdbuf_size(device, cmd_stride * pGeneratedCommandsInfo->sequencesCount, AMD_IP_GFX);
   unsigned ace_cmd_buf_size =
      radv_align_cmdbuf_size(device, ace_cmd_stride * pGeneratedCommandsInfo->sequencesCount, AMD_IP_COMPUTE);

   uint64_t upload_addr =
      radv_buffer_get_va(prep_buffer->bo) + prep_buffer->offset + pGeneratedCommandsInfo->preprocessOffset;

   uint64_t stream_addr =
      radv_buffer_get_va(stream_buffer->bo) + stream_buffer->offset + pGeneratedCommandsInfo->pStreams[0].offset;

   uint64_t sequence_count_addr = 0;
   if (sequence_count_buffer)
      sequence_count_addr = radv_buffer_get_va(sequence_count_buffer->bo) + sequence_count_buffer->offset +
                            pGeneratedCommandsInfo->sequencesCountOffset;

   /* Determine cmdbuf offsets. */
   const bool use_preamble = radv_dgc_use_preamble(pGeneratedCommandsInfo);
   uint32_t cmd_buf_main_offset, ace_cmd_buf_preamble_offset, ace_cmd_buf_main_offset;
   uint32_t offset = 0;

   if (use_preamble)
      offset += radv_dgc_preamble_cmdbuf_size(device, AMD_IP_GFX);
   cmd_buf_main_offset = offset;

   offset += cmd_buf_size;
   ace_cmd_buf_preamble_offset = offset;

   if (use_preamble)
      offset += radv_dgc_preamble_cmdbuf_size(device, AMD_IP_COMPUTE);
   ace_cmd_buf_main_offset = offset;

   uint32_t upload_main_offset = cmd_buf_main_offset + cmd_buf_size;
   if (radv_dgc_with_task_shader(pGeneratedCommandsInfo))
      upload_main_offset = ace_cmd_buf_main_offset + ace_cmd_buf_size;

   struct radv_dgc_params params = {
      .cmd_buf_main_offset = cmd_buf_main_offset,
      .cmd_buf_stride = cmd_stride,
      .cmd_buf_size = cmd_buf_size,
      .ace_cmd_buf_preamble_offset = ace_cmd_buf_preamble_offset,
      .ace_cmd_buf_main_offset = ace_cmd_buf_main_offset,
      .ace_cmd_buf_stride = ace_cmd_stride,
      .ace_cmd_buf_size = ace_cmd_buf_size,
      .upload_main_offset = upload_main_offset,
      .upload_addr = (uint32_t)upload_addr,
      .upload_stride = upload_stride,
      .sequence_count = pGeneratedCommandsInfo->sequencesCount | (sequence_count_addr ? 1u << 31 : 0),
      .sequence_count_addr = sequence_count_addr,
      .stream_stride = layout->input_stride,
      .use_preamble = use_preamble,
      .stream_addr = stream_addr,
   };

   upload_size =
      layout->push_constant_size + sizeof(layout->push_constant_offsets) + ARRAY_SIZE(pipeline->shaders) * 12;
   if (!layout->push_constant_mask)
      upload_size = 0;

   if (layout->pipeline_bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
      radv_prepare_dgc_graphics(cmd_buffer, pGeneratedCommandsInfo, &upload_size, &upload_offset, &upload_data,
                                &params);
   } else {
      assert(layout->pipeline_bind_point == VK_PIPELINE_BIND_POINT_COMPUTE);
      radv_prepare_dgc_compute(cmd_buffer, pGeneratedCommandsInfo, &upload_size, &upload_offset, &upload_data, &params,
                               cond_render_enabled);
   }

   if (layout->push_constant_mask) {
      VkShaderStageFlags pc_stages = 0;
      uint32_t *desc = upload_data;
      upload_data = (char *)upload_data + ARRAY_SIZE(pipeline->shaders) * 12;

      if (pipeline) {
         for (unsigned i = 0; i < ARRAY_SIZE(pipeline->shaders); ++i) {
            if (!pipeline->shaders[i])
               continue;

            const struct radv_shader *shader = pipeline->shaders[i];
            const struct radv_userdata_locations *locs = &shader->info.user_sgprs_locs;
            if (locs->shader_data[AC_UD_PUSH_CONSTANTS].sgpr_idx >= 0)
               params.const_copy = 1;

            if (locs->shader_data[AC_UD_PUSH_CONSTANTS].sgpr_idx >= 0 ||
                locs->shader_data[AC_UD_INLINE_PUSH_CONSTANTS].sgpr_idx >= 0) {
               unsigned upload_sgpr = 0;
               unsigned inline_sgpr = 0;

               if (locs->shader_data[AC_UD_PUSH_CONSTANTS].sgpr_idx >= 0) {
                  upload_sgpr = radv_get_user_sgpr(shader, AC_UD_PUSH_CONSTANTS);
               }

               if (locs->shader_data[AC_UD_INLINE_PUSH_CONSTANTS].sgpr_idx >= 0) {
                  inline_sgpr = radv_get_user_sgpr(shader, AC_UD_INLINE_PUSH_CONSTANTS);
                  desc[i * 3 + 1] = pipeline->shaders[i]->info.inline_push_constant_mask;
                  desc[i * 3 + 2] = pipeline->shaders[i]->info.inline_push_constant_mask >> 32;
               }
               desc[i * 3] = upload_sgpr | (inline_sgpr << 16);

               pc_stages |= mesa_to_vk_shader_stage(i);
            }
         }
      }

      params.push_constant_stages = pc_stages;

      params.const_copy_size = layout->push_constant_size;
      params.push_constant_mask = layout->push_constant_mask;

      memcpy(upload_data, layout->push_constant_offsets, sizeof(layout->push_constant_offsets));
      upload_data = (char *)upload_data + sizeof(layout->push_constant_offsets);

      memcpy(upload_data, cmd_buffer->push_constants, layout->push_constant_size);
      upload_data = (char *)upload_data + layout->push_constant_size;
   }

   radv_buffer_init(&token_buffer, device, cmd_buffer->upload.upload_bo, upload_size, upload_offset);

   radv_meta_save(&saved_state, cmd_buffer,
                  RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_CONSTANTS);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        device->meta_state.dgc_prepare.pipeline);

   vk_common_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer), device->meta_state.dgc_prepare.p_layout,
                              VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);

   radv_meta_push_descriptor_set(
      cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, device->meta_state.dgc_prepare.p_layout, 0, 1,
      (VkWriteDescriptorSet[]){{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                .dstBinding = 0,
                                .dstArrayElement = 0,
                                .descriptorCount = 1,
                                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                .pBufferInfo = &(VkDescriptorBufferInfo){.buffer = radv_buffer_to_handle(&token_buffer),
                                                                         .offset = 0,
                                                                         .range = upload_size}}});

   unsigned block_count = MAX2(1, DIV_ROUND_UP(pGeneratedCommandsInfo->sequencesCount, 64));
   vk_common_CmdDispatch(radv_cmd_buffer_to_handle(cmd_buffer), block_count, 1, 1);

   radv_buffer_finish(&token_buffer);
   radv_meta_restore(&saved_state, cmd_buffer);
}

/* VK_NV_device_generated_commands_compute */
VKAPI_ATTR void VKAPI_CALL
radv_GetPipelineIndirectMemoryRequirementsNV(VkDevice _device, const VkComputePipelineCreateInfo *pCreateInfo,
                                             VkMemoryRequirements2 *pMemoryRequirements)
{
   VkMemoryRequirements *reqs = &pMemoryRequirements->memoryRequirements;
   const uint32_t size = sizeof(struct radv_compute_pipeline_metadata);
   VK_FROM_HANDLE(radv_device, device, _device);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   reqs->memoryTypeBits = ((1u << pdev->memory_properties.memoryTypeCount) - 1u) & ~pdev->memory_types_32bit;
   reqs->alignment = 4;
   reqs->size = align(size, reqs->alignment);
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL
radv_GetPipelineIndirectDeviceAddressNV(VkDevice device, const VkPipelineIndirectDeviceAddressInfoNV *pInfo)
{
   VK_FROM_HANDLE(radv_pipeline, pipeline, pInfo->pipeline);

   return radv_pipeline_to_compute(pipeline)->indirect.va;
}
