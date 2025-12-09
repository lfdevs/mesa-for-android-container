/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "mtypes.h"
#include "api_exec_decl.h"
#include "context.h"
#include "state.h"
#include "bufferobj.h"

#include "state_tracker/st_draw.h"

#include "pipe/p_context.h"
#include "pipe/p_screen.h"

typedef struct {
   GLuint x;
   GLuint y;
   GLuint z;
} DrawMeshTasksIndirectCommand;

static bool
check_mesh_shader_present(struct gl_context *ctx, const char *function)
{
   if (!_mesa_has_EXT_mesh_shader(ctx)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "unsupported function (%s) called",
                  function);
      return false;
   }

   if (ctx->_Shader->CurrentProgram[MESA_SHADER_MESH] == NULL) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(no active mesh shader)",
                  function);
      return false;
   }

   return true;
}

static void
draw_mesh_tasks(struct gl_context *ctx, const struct pipe_grid_info *info)
{
   FLUSH_FOR_DRAW(ctx);

   if (ctx->NewState)
      _mesa_update_state(ctx);

   ST_PIPELINE_MESH_STATE_MASK(mask);
   st_prepare_draw(ctx, mask);

   ctx->pipe->draw_mesh_tasks(ctx->pipe, info);

   if (MESA_DEBUG_FLAGS & DEBUG_ALWAYS_FLUSH) {
      _mesa_flush(ctx);
   }
}

static bool
validate_draw_mesh_tasks(struct gl_context *ctx, const struct pipe_grid_info *info)
{
   if (!check_mesh_shader_present(ctx, "glDrawMeshTasksEXT"))
      return false;

   const struct pipe_mesh_caps *caps = &ctx->screen->caps.mesh;

   const unsigned *max_work_group_count;
   unsigned max_work_group_total_count;
   if (ctx->_Shader->CurrentProgram[MESA_SHADER_TASK]) {
      max_work_group_count = caps->max_task_work_group_count;
      max_work_group_total_count = caps->max_task_work_group_total_count;
   } else {
      max_work_group_count = caps->max_mesh_work_group_count;
      max_work_group_total_count = caps->max_mesh_work_group_total_count;
   }

   for (int i = 0; i < 3; i++) {
      if (info->grid[i] > max_work_group_count[i]) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glDrawMeshTasksEXT(num_groups_%c)", 'x' + i);
         return false;
      }
   }

   if (info->grid[0] * info->grid[1] * info->grid[2] > max_work_group_total_count) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glDrawMeshTasksEXT exceeds total work group count");
      return false;
   }

   return true;
}

void GLAPIENTRY
_mesa_DrawMeshTasksEXT(GLuint num_groups_x,
                       GLuint num_groups_y,
                       GLuint num_groups_z)
{
   GET_CURRENT_CONTEXT(ctx);

   struct pipe_grid_info info = {
      .grid = {num_groups_x, num_groups_y, num_groups_z},
      .draw_count = 1,
   };

   if (!_mesa_is_no_error_enabled(ctx) && !validate_draw_mesh_tasks(ctx, &info))
      return;

   draw_mesh_tasks(ctx, &info);
}

static bool
validate_draw_mesh_tasks_indirect(struct gl_context *ctx, GLintptr indirect,
                                  GLsizei drawcount, GLsizei stride,
                                  const char *name)
{
   if (!check_mesh_shader_present(ctx, name))
      return false;

   if (indirect & (sizeof(GLuint) - 1)) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(indirect is not aligned)", name);
      return false;
   }

   if (indirect < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(indirect is less than zero)", name);
      return false;
   }

   if (!ctx->DrawIndirectBuffer) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s: no buffer bound to DRAW_INDIRECT_BUFFER", name);
      return false;
   }

   if (_mesa_check_disallowed_mapping(ctx->DrawIndirectBuffer)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(DRAW_INDIRECT_BUFFER is mapped)", name);
      return false;
   }

   if (stride & 3) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(stride is not aligned)", name);
      return false;
   }

   if (stride < sizeof(DrawMeshTasksIndirectCommand)) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(stride is less then DrawMeshTasksIndirectCommandEXT)", name);
      return false;
   }

   if (drawcount <= 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(drawcount is not positive)", name);
      return false;
   }

   GLsizei size = stride * (drawcount - 1) + sizeof(DrawMeshTasksIndirectCommand);
   const uint64_t end = (uint64_t) indirect + size;

   if (ctx->DrawIndirectBuffer->Size < end) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(DRAW_INDIRECT_BUFFER too small)", name);
      return false;
   }

   return true;
}

void GLAPIENTRY
_mesa_DrawMeshTasksIndirectEXT(GLintptr indirect)
{
   GET_CURRENT_CONTEXT(ctx);

   if (!_mesa_is_no_error_enabled(ctx) &&
       !validate_draw_mesh_tasks_indirect(
          ctx, indirect, 1, sizeof(DrawMeshTasksIndirectCommand),
          "glDrawMeshTasksIndirectEXT"))
      return;

   struct pipe_grid_info info = {
      .indirect = ctx->DrawIndirectBuffer->buffer,
      .indirect_offset = indirect,
      .draw_count = 1,
   };

   draw_mesh_tasks(ctx, &info);
}

void GLAPIENTRY
_mesa_MultiDrawMeshTasksIndirectEXT(GLintptr indirect,
                                    GLsizei drawcount,
                                    GLsizei stride)
{
   GET_CURRENT_CONTEXT(ctx);

   /* If <stride> is zero, the array elements are treated as tightly packed. */
   if (stride == 0)
      stride = sizeof(DrawMeshTasksIndirectCommand);

   if (!_mesa_is_no_error_enabled(ctx) &&
       !validate_draw_mesh_tasks_indirect(
          ctx, indirect, drawcount, stride, "glMultiDrawMeshTasksIndirectEXT"))
      return;

   struct pipe_grid_info info = {
      .indirect = ctx->DrawIndirectBuffer->buffer,
      .indirect_offset = indirect,
      .indirect_stride = stride,
      .draw_count = drawcount,
   };

   draw_mesh_tasks(ctx, &info);
}

static bool
validate_multi_draw_mesh_tasks_indirect_count(struct gl_context *ctx,
                                              GLintptr indirect,
                                              GLintptr drawcount,
                                              GLsizei maxdrawcount,
                                              GLsizei stride)
{
   const char *name = "glMultiDrawMeshTasksIndirectCountEXT";

   if (!validate_draw_mesh_tasks_indirect(ctx, indirect, maxdrawcount, stride, name))
      return false;

   if (drawcount & (sizeof(GLuint) - 1)) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(drawcount is not aligned)", name);
      return false;
   }

   if (drawcount < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(drawcount is less than zero)", name);
      return false;
   }

   if (!ctx->ParameterBuffer) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s: no buffer bound to PARAMETER_BUFFER", name);
      return false;
   }

   if (_mesa_check_disallowed_mapping(ctx->ParameterBuffer)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(PARAMETER_BUFFER is mapped)", name);
      return false;
   }

   const uint64_t end = (uint64_t) drawcount + sizeof(GLsizei);

   if (ctx->ParameterBuffer->Size < end) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(PARAMETER_BUFFER too small)", name);
      return false;
   }

   return true;
}

void GLAPIENTRY
_mesa_MultiDrawMeshTasksIndirectCountEXT(GLintptr indirect,
                                         GLintptr drawcount,
                                         GLsizei maxdrawcount,
                                         GLsizei stride)
{
   GET_CURRENT_CONTEXT(ctx);

   /* If <stride> is zero, the array elements are treated as tightly packed. */
   if (stride == 0)
      stride = sizeof(DrawMeshTasksIndirectCommand);

   if (!_mesa_is_no_error_enabled(ctx) &&
       !validate_multi_draw_mesh_tasks_indirect_count(ctx, indirect, drawcount,
                                                      maxdrawcount, stride))
      return;

   struct pipe_grid_info info = {
      .indirect = ctx->DrawIndirectBuffer->buffer,
      .indirect_offset = indirect,
      .indirect_stride = stride,
      .indirect_draw_count = ctx->ParameterBuffer->buffer,
      .indirect_draw_count_offset = drawcount,
      .draw_count = maxdrawcount,
   };

   draw_mesh_tasks(ctx, &info);
}
