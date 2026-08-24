/*
 * Copyright 2009 Corbin Simpson <MostAwesomeDude@gmail.com>
 * Copyright 2009 Marek Olšák <maraeo@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "r300_vs.h"

#include "r300_context.h"
#include "r300_screen.h"
#include "r300_reg.h"

#include "compiler/nir_to_rc.h"
#include "compiler/r300_nir.h"
#include "compiler/radeon_compiler.h"
#include "nir/nir.h"


static void set_vertex_inputs_outputs(struct r300_vertex_program_compiler * c)
{
    struct r300_vertex_shader_code * vs = c->UserData;
    struct r300_shader_semantics* outputs = &vs->outputs;
    int i, reg = 0;
    bool any_bcolor_used = outputs->bcolor[0] != ATTR_UNUSED ||
                           outputs->bcolor[1] != ATTR_UNUSED;

    /* Fill in the input mapping */
    for (i = 0; i < vs->num_inputs; i++)
        c->code->inputs[i] = i;

    /* Position. */
    if (outputs->pos != ATTR_UNUSED) {
        c->code->outputs[outputs->pos] = reg++;
    } else {
        assert(0);
    }

    /* Point size. */
    if (outputs->psize != ATTR_UNUSED) {
        c->code->outputs[outputs->psize] = reg++;
    }

    /* If we're writing back facing colors we need to send
     * four colors to make front/back face colors selection work.
     * If the vertex program doesn't write all 4 colors, lets
     * pretend it does by skipping output index reg so the colors
     * get written into appropriate output vectors.
     */

    /* Colors. */
    for (i = 0; i < ATTR_COLOR_COUNT; i++) {
        if (outputs->color[i] != ATTR_UNUSED) {
            c->code->outputs[outputs->color[i]] = reg++;
        } else if (any_bcolor_used ||
                   outputs->color[1] != ATTR_UNUSED) {
            reg++;
        }
    }

    /* Back-face colors. */
    for (i = 0; i < ATTR_COLOR_COUNT; i++) {
        if (outputs->bcolor[i] != ATTR_UNUSED) {
            c->code->outputs[outputs->bcolor[i]] = reg++;
        } else if (any_bcolor_used) {
            reg++;
        }
    }

    /* Texture coordinates. */
    for (i = 0; i < ATTR_GENERIC_COUNT; i++) {
        if (outputs->generic[i] != ATTR_UNUSED) {
            c->code->outputs[outputs->generic[i]] = reg++;
        }
    }

    /* Fog coordinates. */
    if (outputs->fog != ATTR_UNUSED) {
        c->code->outputs[outputs->fog] = reg++;
    }

    /* WPOS. */
    if (vs->key.wpos)
        c->code->outputs[outputs->wpos] = reg++;
}

static void r300_setup_vs_compiler(struct r300_context *r300,
                            struct r300_vertex_program_compiler *compiler,
                            struct r300_vertex_shader_code *vs)
{
    /* Setup the compiler */
    memset(compiler, 0, sizeof(*compiler));
    rc_init(&compiler->Base, &r300->vs_regalloc_state);

    DBG_ON(r300, DBG_VP) ? compiler->Base.Debug |= RC_DBG_LOG : 0;
    compiler->code = &vs->code;
    compiler->UserData = vs;
    compiler->Base.debug = &r300->context.debug;
    compiler->Base.is_r400 = r300->screen->caps.is_r400;
    compiler->Base.is_r500 = r300->screen->caps.is_r500;
    compiler->Base.disable_optimizations = DBG_ON(r300, DBG_NO_OPT);
    /* Only R500 has few IEEE math opcodes. */
    if (r300->screen->options.ieeemath && r300->screen->caps.is_r500) {
        compiler->Base.math_rules = RC_MATH_IEEE;
    } else if (r300->screen->options.ffmath) {
        compiler->Base.math_rules = RC_MATH_FF;
    }
    compiler->Base.has_half_swizzles = false;
    compiler->Base.has_presub = false;
    compiler->Base.has_omod = false;
    compiler->Base.max_temp_regs = 32;
    compiler->Base.max_constants = 256;
    compiler->Base.max_alu_insts = r300->screen->caps.is_r500 ? 1024 : 256;
}

void r300_translate_vertex_shader(struct r300_context *r300,
                                  struct r300_vertex_shader *shader)
{
    struct r300_vertex_program_compiler compiler;
    unsigned i;

    struct r300_vertex_shader_code *vs = shader->shader;
    r300_shader_semantics_reset(&vs->outputs);

    union r300_shader_code code;
    code.v = vs;

    r300_setup_vs_compiler(r300, &compiler, vs);

    nir_shader *clone = nir_shader_clone(NULL, shader->state.ir.nir);
    if (vs->key.frontface)
        NIR_PASS(_, clone, r300_nir_lower_frontface);

    nir_variable *wpos_var = NULL;
    if (vs->key.wpos)
        NIR_PASS(_, clone, r300_nir_add_wpos, &wpos_var);

    int wpos_output = wpos_var ? wpos_var->data.driver_location : ATTR_UNUSED;
    struct r300_fragment_program_external_state external_state = {};
    nir_to_rc(clone, (struct pipe_screen *)r300->screen,
              external_state, code, &compiler.Base);

    if (wpos_output != ATTR_UNUSED) {
        vs->outputs.wpos = wpos_output;
        for (unsigned i = 0; i < ATTR_GENERIC_COUNT; i++) {
            if (vs->outputs.generic[i] == wpos_output) {
                vs->outputs.generic[i] = ATTR_UNUSED;
                vs->outputs.num_generic--;
                break;
            }
        }
    }

    /* Nothing to do if the shader does not write gl_Position. */
    if (vs->outputs.pos == ATTR_UNUSED) {
        vs->dummy = true;
        goto cleanup;
    }

    if (compiler.Base.Error) {
        vs->error = strdup(compiler.Base.ErrorMsg ? compiler.Base.ErrorMsg
                                                  : "Cannot translate shader from NIR");
        vs->dummy = true;
        goto cleanup;
    }

    if (compiler.Base.Program.Constants.Count > 200) {
        compiler.Base.remove_unused_constants = true;
    }

    compiler.RequiredOutputs = ~(~0U << vs->outputs.num_total);
    compiler.SetHwInputOutput = &set_vertex_inputs_outputs;

    /* Invoke the compiler */
    r3xx_compile_vertex_program(&compiler);
    if (compiler.Base.Error) {
        vs->error = strdup(compiler.Base.ErrorMsg);
        vs->dummy = true;
        goto cleanup;
    }

    /* Initialize numbers of constants for each type. */
    vs->externals_count = 0;
    for (i = 0;
         i < vs->code.constants.Count &&
         vs->code.constants.Constants[i].Type == RC_CONSTANT_EXTERNAL; i++) {
        vs->externals_count = i+1;
    }
    for (; i < vs->code.constants.Count; i++) {
        assert(vs->code.constants.Constants[i].Type == RC_CONSTANT_IMMEDIATE);
    }
    vs->immediates_count = vs->code.constants.Count - vs->externals_count;

    /* And, finally... */
cleanup:
    rc_destroy(&compiler.Base);
}
