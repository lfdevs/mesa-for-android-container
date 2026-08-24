/*
 * Copyright 2009 Corbin Simpson <MostAwesomeDude@gmail.com>
 * Copyright 2009 Marek Olšák <maraeo@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef R300_VS_H
#define R300_VS_H

#include "pipe/p_state.h"
#include "compiler/radeon_code.h"

#include "r300_context.h"
#include "r300_shader_semantics.h"

struct r300_context;

struct r300_vertex_shader_key {
    bool wpos;
    bool frontface;
};
static_assert(sizeof(struct r300_vertex_shader_key) == 2 * sizeof(bool),
              "r300_vertex_shader_key must not contain padding");

struct r300_vertex_shader_code {
    /* Parent class */

    unsigned num_inputs;
    struct r300_shader_semantics outputs;

    /* Whether the shader was replaced by a dummy one due to a shader
     * compilation failure. */
    bool dummy;

    struct r300_vertex_shader_key key;

    /* Numbers of constants for each type. */
    unsigned externals_count;
    unsigned immediates_count;

    /* HWTCL-specific.  */
    /* Machine code (if translated) */
    struct r300_vertex_program_code code;

    /* SWTCL-specific. */
    void *draw_vs;

    struct r300_vertex_shader_code *next;

    /* Error message in case compilation failed. */
    char *error;
};

struct r300_vertex_shader {
    /* Parent class */
    struct pipe_shader_state state;

    /* Currently-bound vertex shader. */
    struct r300_vertex_shader_code *shader;

    /* List of the same shaders compiled with different states. */
    struct r300_vertex_shader_code *first;

    bool can_emulate_frontface;
};

void r300_translate_vertex_shader(struct r300_context *r300,
                                  struct r300_vertex_shader *vs);

void r300_draw_init_vertex_shader(struct r300_context *r300,
                                  struct r300_vertex_shader *vs);

#endif /* R300_VS_H */
