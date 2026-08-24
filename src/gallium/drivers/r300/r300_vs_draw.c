/*
 * Copyright 2009 Marek Olšák <maraeo@gmail.com>
 * Copyright 2026 Pavel Ondračka <pavel.ondracka@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Vertex shader setup for the swtcl (draw-based) path.
 *
 * NIR transforms applied before handing to the draw module:
 * 1) Regular variants expand color outputs to satisfy r300 hardware layout rules.
 *    FACE variants skip this because Draw consumes their synthetic back color.
 * 2) Append a generic output containing a copy of gl_Position, used as WPOS
 *    by the hardware fragment shader.
 */

#include "r300_vs.h"

#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "compiler/nir_to_rc.h"
#include "compiler/r300_nir.h"

#include "draw/draw_context.h"

static nir_variable *
r300_draw_find_shader_out(nir_shader *nir, unsigned location)
{
    nir_foreach_shader_out_variable(var, nir) {
        if (var->data.location == location)
            return var;
    }
    return NULL;
}

/* Add a missing output variable and write zeros to it. */
static void
r300_draw_add_zero_output(nir_shader *nir, nir_builder *b, unsigned location,
                          const char *name)
{
    nir_variable *var = nir_variable_create(nir, nir_var_shader_out,
                                            glsl_vec4_type(), name);
    var->data.location = location;
    var->data.driver_location = nir->num_outputs++;
    var->data.interpolation = INTERP_MODE_NOPERSPECTIVE;
    nir_store_var(b, var, nir_imm_zero(b, 4, 32), 0xf);
}

/* Ensure that the color output layout satisfies the r300 hardware rules:
 *   - COL1 (secondary front color) requires COL0
 *   - Any back-face color requires all four color outputs (COL0/COL1/BFC0/BFC1) */
static bool
r300_nir_add_missing_color_outputs(nir_shader *nir)
{
    bool color_used[2] = {false, false};
    bool bcolor_used[2] = {false, false};

    nir_foreach_shader_out_variable(var, nir) {
        switch (var->data.location) {
        case VARYING_SLOT_COL0: color_used[0] = true; break;
        case VARYING_SLOT_COL1: color_used[1] = true; break;
        case VARYING_SLOT_BFC0: bcolor_used[0] = true; break;
        case VARYING_SLOT_BFC1: bcolor_used[1] = true; break;
        default: break;
        }
    }

    nir_function_impl *impl = nir_shader_get_entrypoint(nir);
    nir_builder b = nir_builder_create(impl);
    b.cursor = nir_after_impl(impl);
    bool progress = false;

    if (color_used[1] && !color_used[0]) {
        r300_draw_add_zero_output(nir, &b, VARYING_SLOT_COL0, "gl_FrontColor");
        color_used[0] = true;
        progress = true;
    }

    if (bcolor_used[0] || bcolor_used[1]) {
        if (!color_used[0]) {
            r300_draw_add_zero_output(nir, &b, VARYING_SLOT_COL0, "gl_FrontColor");
            progress = true;
        }
        if (!color_used[1]) {
            r300_draw_add_zero_output(nir, &b, VARYING_SLOT_COL1, "gl_FrontSecondaryColor");
            progress = true;
        }
        if (!bcolor_used[0]) {
            r300_draw_add_zero_output(nir, &b, VARYING_SLOT_BFC0, "gl_BackColor");
            progress = true;
        }
        if (!bcolor_used[1]) {
            r300_draw_add_zero_output(nir, &b, VARYING_SLOT_BFC1, "gl_BackSecondaryColor");
            progress = true;
        }
    }

    return nir_progress(progress, impl, nir_metadata_control_flow);
}

/* Assign driver_location to the variable at the given slot (if present)
 * and update the outputs field. */
static void
r300_draw_assign_output(nir_shader *nir, unsigned location, int *field,
                        unsigned *driver_location)
{
    nir_variable *var = r300_draw_find_shader_out(nir, location);
    if (!var)
        return;
    *field = *driver_location;
    var->data.driver_location = (*driver_location)++;
}

/* Build vs->outputs from the (transformed) NIR and assign driver_locations.
 * The WPOS output is identified by wpos_var and placed last. */
static void
r300_draw_fill_vs_outputs(nir_shader *nir, nir_variable *wpos_var,
                          struct r300_vertex_shader_code *vs)
{
    struct r300_shader_semantics *outputs = &vs->outputs;
    unsigned driver_location = 0;

    r300_shader_semantics_reset(outputs);

    r300_draw_assign_output(nir, VARYING_SLOT_POS, &outputs->pos, &driver_location);
    r300_draw_assign_output(nir, VARYING_SLOT_PSIZ, &outputs->psize, &driver_location);
    r300_draw_assign_output(nir, VARYING_SLOT_COL0, &outputs->color[0], &driver_location);
    r300_draw_assign_output(nir, VARYING_SLOT_COL1, &outputs->color[1], &driver_location);
    r300_draw_assign_output(nir, VARYING_SLOT_BFC0, &outputs->bcolor[0], &driver_location);
    r300_draw_assign_output(nir, VARYING_SLOT_BFC1, &outputs->bcolor[1], &driver_location);

    for (unsigned g = 0; g < ATTR_GENERIC_COUNT; g++) {
        unsigned loc = VARYING_SLOT_VAR0 + g;
        nir_variable *var = r300_draw_find_shader_out(nir, loc);
        if (!var || var == wpos_var)
            continue;
        outputs->generic[g] = driver_location;
        outputs->num_generic++;
        var->data.driver_location = driver_location++;
    }

    r300_draw_assign_output(nir, VARYING_SLOT_FOGC, &outputs->fog, &driver_location);

    /* Draw still needs these non-rasterized outputs in its shader info. */
    nir_variable *edge_var = r300_draw_find_shader_out(nir, VARYING_SLOT_EDGE);
    if (edge_var)
        edge_var->data.driver_location = driver_location++;
    nir_variable *clip_vertex_var =
        r300_draw_find_shader_out(nir, VARYING_SLOT_CLIP_VERTEX);
    if (clip_vertex_var)
        clip_vertex_var->data.driver_location = driver_location++;

    if (wpos_var) {
        outputs->wpos = driver_location;
        wpos_var->data.driver_location = driver_location;
    }
}

void
r300_draw_init_vertex_shader(struct r300_context *r300,
                             struct r300_vertex_shader *vs)
{
    /* Clone the NIR and apply the +9 varying shift to align VS outputs with
     * FS inputs (which get the same shift in nir_to_rc). */
    nir_shader *nir = nir_shader_clone(NULL, vs->state.ir.nir);
    ntr_fixup_varying_slots(nir, nir_var_shader_out);

    if (vs->shader->key.frontface)
        NIR_PASS(_, nir, r300_nir_lower_frontface);
    else
        NIR_PASS(_, nir, r300_nir_add_missing_color_outputs);
    nir_variable *wpos_var = NULL;
    if (vs->shader->key.wpos)
        NIR_PASS(_, nir, r300_nir_add_wpos, &wpos_var);

    /* Fill in the r300 rasterizer outputs and assign driver locations. */
    r300_draw_fill_vs_outputs(nir, wpos_var, vs->shader);

    /* Hand the transformed NIR off to the draw module. */
    struct pipe_shader_state new_vs = {
        .type = PIPE_SHADER_IR_NIR,
        .ir.nir = nir,
    };
    vs->shader->draw_vs = draw_create_vertex_shader(r300->draw, &new_vs);
}
