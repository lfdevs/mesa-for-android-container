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

layout(location = 0) out vec4 out_color;

layout(location = 0) flat in uint32_t index;
layout(location = 1) flat in uint32_t param_index;

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
   gamma_render_params params = state.params[param_index];

   out_color = vec4(0.0, 0.0, 0.0, 1.0);

   uint32_t color_calculation = consts.flags & GAMMA_RENDERER_FLAG_COLOR_MASK;
   if (color_calculation == GAMMA_RENDERER_COLOR_PUSH_CONSTANT) {
      out_color.rgb = params.color;
   } else if (color_calculation == GAMMA_RENDERER_COLOR_GEOMETRY_INDEX ||
              color_calculation == GAMMA_RENDERER_COLOR_PRIMITIVE_INDEX) {
      out_color.r = ((index * 0xd83f0930) >> 24) / 255.0;
      out_color.g = ((index * 0x8fa9836b) >> 24) / 255.0;
      out_color.b = ((index * 0x3037f8ad) >> 24) / 255.0;
   }
}
