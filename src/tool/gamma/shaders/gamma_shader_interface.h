/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef GAMMA_SHADER_INTERFACE_H
#define GAMMA_SHADER_INTERFACE_H

#ifdef VULKAN
#else
#include "gamma_util.h"
#define mat4 gamma_mat4
#define vec3 gamma_vec3
#endif

struct gamma_render_params {
   mat4 transform;
   vec3 color;
};

struct gamma_push_constants {
   mat4 transform;
   uint32_t first_param;
   uint32_t flags;
};

#define GAMMA_RENDERER_COLOR_PUSH_CONSTANT   0
#define GAMMA_RENDERER_COLOR_PRIMITIVE_INDEX 1
#define GAMMA_RENDERER_COLOR_GEOMETRY_INDEX  2
#define GAMMA_RENDERER_COLOR_BIT_COUNT       2

#define GAMMA_RENDERER_FLAG_COLOR_MASK ((1u << GAMMA_RENDERER_COLOR_BIT_COUNT) - 1)

#ifdef VULKAN
#else
#undef mat4
#undef vec3
#endif

#endif
