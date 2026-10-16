/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_CMD_PUSH_CONSTANT_H
#define PANVK_CMD_PUSH_CONSTANT_H

#include <stdint.h>

#include "genxml/gen_macros.h"

struct panvk_cmd_buffer;
struct panvk_shader_variant;

#define MAX_PUSH_CONSTANTS_SIZE 256

struct panvk_push_constant_state {
   uint64_t data[MAX_PUSH_CONSTANTS_SIZE / sizeof(uint64_t)];
};

VkResult panvk_per_arch(cmd_prepare_gfx_push_uniforms)(
   struct panvk_cmd_buffer *cmdbuf, const struct panvk_shader_variant *shader,
   struct pan_ptr *push_uniforms, uint32_t repeat_count);

VkResult panvk_per_arch(cmd_prepare_compute_push_uniforms)(
   struct panvk_cmd_buffer *cmdbuf, const struct panvk_shader_variant *shader,
   struct pan_ptr *push_uniforms);

#if PAN_ARCH < 9
struct panvk_compute_sysvals;

/* A lowered geometry shader runs as compute during a graphics draw: its
 * sysvals are built by the caller, not taken from state.compute.
 */
VkResult panvk_per_arch(cmd_prepare_gs_push_uniforms)(
   struct panvk_cmd_buffer *cmdbuf, const struct panvk_shader_variant *shader,
   const struct panvk_compute_sysvals *sysvals, struct pan_ptr *push_uniforms);
#endif

#endif
