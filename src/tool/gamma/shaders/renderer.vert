/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#version 460

#extension GL_GOOGLE_include_directive : require

#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_EXT_scalar_block_layout : require

#include "gamma_shader_interface.h"

layout(location = 0) in vec3 position;
layout(location = 1) in uint32_t geometry_index;
layout(location = 2) in uint32_t primitive_index;

layout(location = 0) flat out uint32_t out_index;
layout(location = 1) flat out uint32_t out_param_index;

layout(push_constant) uniform block
{
   gamma_push_constants consts;
};

layout(scalar, set = 0, binding = 0) readonly buffer SSBO
{
   gamma_render_params params[];
}
state;

void
main()
{
   uint32_t param_index = consts.first_param + gl_DrawID;
   gamma_render_params params = state.params[param_index];

   gl_Position = consts.transform * (params.transform * vec4(position, 1.0));

   uint32_t color_calculation = consts.flags & GAMMA_RENDERER_FLAG_COLOR_MASK;
   if (color_calculation == GAMMA_RENDERER_COLOR_GEOMETRY_INDEX)
      out_index = geometry_index;
   else
      out_index = primitive_index;

   out_param_index = param_index;
}