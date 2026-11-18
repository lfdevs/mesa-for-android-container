/*
 * Copyright 2026 Abhay Byte
 * SPDX-License-Identifier: MIT
 */

#include "panvk_gpu_prerast.h"
/* Angle brackets: the per-arch include dir picks csf/ or (v9) jm/. */
#include <panvk_cmd_buffer.h>
#include "panvk_cmd_draw.h"

struct panvk_gpu_prerast_draw
panvk_per_arch(gpu_prerast_draw_info)(const struct panvk_draw_info *draw)
{
   return (struct panvk_gpu_prerast_draw){
      .index_buffer = draw->index.buffer_dev_addr,
      .indirect_buffer = draw->indirect.buffer_dev_addr,
      .indirect_count_buffer = draw->indirect.count_buffer_dev_addr,
      .index_buffer_size = draw->index.buffer_size,
      .index_offset = draw->index.offset,
      .index_size = draw->index.index_size,
      .vertex_offset = draw->vertex.base,
      .vertex_count = draw->vertex.count,
      .first_instance = draw->instance.base,
      .instance_count = draw->instance.count,
      .draw_count = draw->indirect.draw_count,
      .indirect_stride = draw->indirect.stride,
      .prim = draw->prim,
      .primitive_restart = draw->index.restart_enable,
   };
}

VkResult
panvk_per_arch(gpu_prerast_validate_execution)(
   const struct panvk_cmd_buffer *cmdbuf,
   const struct panvk_gpu_prerast_execution *exec)
{
   if (exec->abi_version != PANVK_GPU_PRERAST_ABI_VERSION ||
       exec->record_stride < 16 ||
       !exec->record_count || !exec->record_stride)
      return VK_ERROR_UNKNOWN;

   uint64_t record_bytes;
   if (!panvk_gpu_prerast_checked_size(exec->record_count,
                                        exec->record_stride, 64,
                                        &record_bytes) ||
       record_bytes > PANVK_GPU_PRERAST_MAX_EXECUTION_BYTES)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   return VK_SUCCESS;
}

enum panvk_gpu_prerast_poly_mode
panvk_per_arch(gpu_prerast_poly_mode)(const struct panvk_cmd_buffer *cmdbuf,
                                      const struct panvk_draw_info *draw)
{
   if (draw->prim != MESA_PRIM_TRIANGLES &&
       draw->prim != MESA_PRIM_TRIANGLE_STRIP &&
       draw->prim != MESA_PRIM_TRIANGLE_FAN)
      return PANVK_GPU_PRERAST_POLY_NONE;

   switch (cmdbuf->vk.dynamic_graphics_state.rs.polygon_mode) {
   case VK_POLYGON_MODE_LINE:
      return PANVK_GPU_PRERAST_POLY_LINE;
   case VK_POLYGON_MODE_POINT:
      return PANVK_GPU_PRERAST_POLY_POINT;
   default:
      return PANVK_GPU_PRERAST_POLY_NONE;
   }
}

bool
panvk_per_arch(gpu_prerast_can_lower)(
   const struct panvk_cmd_buffer *cmdbuf, const struct panvk_draw_info *draw,
   const struct panvk_gpu_prerast_shader *shader)
{
    const struct panvk_shader *gs = cmdbuf->state.gfx.gs.shader;
    const bool poly = !gs &&
                      panvk_per_arch(gpu_prerast_poly_mode)(cmdbuf, draw) !=
                         PANVK_GPU_PRERAST_POLY_NONE;
    const bool xfb = cmdbuf->state.gfx.xfb.active &&
                     panvk_gpu_prerast_has_xfb(gs ? &gs->gpu_prerast.xfb
                                                  : &shader->xfb);
    const bool side_effects = shader->has_side_effects;
    const bool vp_select = !gs && shader->vp_scissor_loc;
    if (!(PANVK_DEBUG(GPU_PRERAST) || poly || gs || xfb || side_effects ||
          vp_select) ||
        !shader->lowered_vs_compiled || !shader->passthrough_vs_compiled ||
        (poly && !shader->polygon_compiled) ||
        (gs && (!gs->gpu_prerast.gs.kernel_compiled ||
                !gs->gpu_prerast.gs.passthrough_compiled)) ||
        (draw->indirect.buffer_dev_addr && draw->indirect.draw_count > 1))
       return false;

    if (!draw->indirect.buffer_dev_addr) {
       if (!draw->vertex.count || !draw->instance.count)
          return true;
       if ((uint64_t)draw->vertex.count * draw->instance.count >
           panvk_gpu_prerast_record_capacity(shader->io.record_stride))
          return false;
       if (gs && (uint64_t)draw->vertex.count * draw->instance.count *
                       gs->gpu_prerast.gs.invocations >
                    panvk_gpu_prerast_gs_cap_slots(&gs->gpu_prerast.gs))
          return false;
    }

   if (gs) {
      switch (draw->prim) {
      case MESA_PRIM_POINTS:
      case MESA_PRIM_LINES:
      case MESA_PRIM_LINE_STRIP:
      case MESA_PRIM_TRIANGLES:
      case MESA_PRIM_TRIANGLE_STRIP:
      case MESA_PRIM_TRIANGLE_FAN:
      case MESA_PRIM_LINES_ADJACENCY:
      case MESA_PRIM_LINE_STRIP_ADJACENCY:
      case MESA_PRIM_TRIANGLES_ADJACENCY:
      case MESA_PRIM_TRIANGLE_STRIP_ADJACENCY:
         return true;
      default:
         return false;
      }
   }

   switch (draw->prim) {
   case MESA_PRIM_POINTS:
   case MESA_PRIM_LINES:
   case MESA_PRIM_LINE_STRIP:
   case MESA_PRIM_TRIANGLES:
   case MESA_PRIM_TRIANGLE_STRIP:
   case MESA_PRIM_TRIANGLE_FAN:
   case MESA_PRIM_LINES_ADJACENCY:
   case MESA_PRIM_LINE_STRIP_ADJACENCY:
   case MESA_PRIM_TRIANGLES_ADJACENCY:
   case MESA_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return true;
   default:
      return false;
   }
}

const struct panvk_gpu_prerast_contract
panvk_per_arch(gpu_prerast_contract) = {
   .side_effect_policy = PANVK_GPU_PRERAST_SIDE_EFFECT_SINGLE_EXECUTION,
   .scratch_lifetime = PANVK_GPU_PRERAST_LIFETIME_SUBMISSION,
   .render_scope = PANVK_GPU_PRERAST_INTERNAL_JOB_SPLIT,
   .output_order = PANVK_GPU_PRERAST_ORDER_ORIGINAL_PRIMITIVE,
   .state_isolation = PANVK_GPU_PRERAST_STATE_SAVED_AND_DIRTIED,
   .sync = {
      .producer_stages = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                         VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT |
                         VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT |
                         VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT,
      .producer_access = VK_ACCESS_2_SHADER_READ_BIT |
                         VK_ACCESS_2_SHADER_WRITE_BIT,
      .internal_consumer_stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                                  VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
                                  VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
      .internal_consumer_access = VK_ACCESS_2_SHADER_READ_BIT |
                                  VK_ACCESS_2_SHADER_WRITE_BIT |
                                  VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT |
                                  VK_ACCESS_2_INDEX_READ_BIT |
                                  VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
   },
};
