/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef ZINK_COMPILER_H
#define ZINK_COMPILER_H

#include "pipe/p_defines.h"
#include "pipe/p_state.h"

#include "compiler/shader_info.h"

#include <vulkan/vulkan.h>

struct pipe_screen;
struct zink_screen;

struct nir_shader_compiler_options;
struct nir_shader;

struct tgsi_token;

const void *
zink_get_compiler_options(struct pipe_screen *screen,
                          enum pipe_shader_ir ir,
                          enum pipe_shader_type shader);

struct nir_shader *
zink_tgsi_to_nir(struct pipe_screen *screen, const struct tgsi_token *tokens);

struct zink_shader {
   VkShaderModule shader_module;

   shader_info info;

   struct {
      int index;
      int binding;
      VkDescriptorType type;
   } bindings[PIPE_MAX_CONSTANT_BUFFERS + PIPE_MAX_SHADER_SAMPLER_VIEWS];
   size_t num_bindings;
};

struct zink_shader *
zink_compile_nir(struct zink_screen *screen, struct nir_shader *nir);

void
zink_shader_free(struct zink_screen *screen, struct zink_shader *shader);

#endif
