/*
 * © Copyright 2018 Alyssa Rosenzweig
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <stdio.h>
#include "pan_blending.h"

/*
 * Implements fixed-function blending on Midgard.
 *
 * Midgard splits blending into a fixed-function fast path and a programmable
 * slow path. The fixed function blending architecture is based on "dominant"
 * blend factors. Blending is encoded separately (but identically) between RGB
 * and alpha functions.
 *
 * Essentially, for a given blending operation, there is a single dominant
 * factor. The following dominant factors are possible:
 *
 * 	- zero
 * 	- source color
 * 	- destination color
 * 	- source alpha
 * 	- destination alpha
 * 	- constant float
 *
 * Further, a dominant factor's arithmetic compliment could be used. For
 * instance, to encode GL_ONE_MINUS_SOURCE_ALPHA, the dominant factor would be
 * MALI_DOMINANT_SRC_ALPHA with the complement_dominant bit set.
 *
 * A single constant float can be passed to the fixed-function hardware,
 * allowing CONSTANT_ALPHA support. Further, if all components of the constant
 * glBlendColor are identical, CONSTANT_COLOR can be implemented with the
 * constant float mode. If the components differ, programmable blending is
 * required.
 *
 * The nondominant factor can be either:
 *
 * 	- the same as the dominant factor (MALI_BLEND_NON_MIRROR)
 * 	- zero (MALI_BLEND_NON_ZERO)
 *
 * Exactly one of the blend operation's source or destination can be used as
 * the dominant factor; this is selected by the
 * MALI_BLEND_DOM_SOURCE/DESTINATION flag.
 *
 * By default, all blending follows the standard OpenGL addition equation:
 *
 * 	out = source_value * source_factor + destination_value * destination_factor
 *
 * By setting the negate_source or negate_dest bits, other blend functions can
 * be created. For instance, for SUBTRACT mode, set the "negate destination"
 * flag, and similarly for REVERSE_SUBTRACT with "negate source".
 *
 * Finally, there is a "clip modifier" controlling the final blending
 * behaviour, allowing for the following modes:
 *
 * 	- normal
 * 	- force source factor to one (MALI_BLEND_MODE_SOURCE_ONE)
 * 	- force destination factor to one (MALI_BLEND_MODE_DEST_ONE)
 *
 * The clipping flags can be used to encode blend modes where the nondominant
 * factor is ONE.
 *
 * As an example putting it all together, to encode the following blend state:
 *
 * 	glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
 * 	glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_ONE);
 *
 * We need the following configuration:
 *
 * 	- negate source (for REVERSE_SUBTRACT)
 * 	- dominant factor "source alpha"
 * 		- compliment dominant
 * 		- source dominant
 * 	- force destination to ONE
 *
 * The following routines implement this fixed function blending encoding
 */

/* Helper to find the uncomplemented Gallium blend factor corresponding to a
 * complemented Gallium blend factor */

static int
complement_factor(int factor)
{
        switch (factor) {
        case PIPE_BLENDFACTOR_INV_SRC_COLOR:
                return PIPE_BLENDFACTOR_SRC_COLOR;

        case PIPE_BLENDFACTOR_INV_SRC_ALPHA:
                return PIPE_BLENDFACTOR_SRC_ALPHA;

        case PIPE_BLENDFACTOR_INV_DST_ALPHA:
                return PIPE_BLENDFACTOR_DST_ALPHA;

        case PIPE_BLENDFACTOR_INV_DST_COLOR:
                return PIPE_BLENDFACTOR_DST_COLOR;

        case PIPE_BLENDFACTOR_INV_CONST_COLOR:
                return PIPE_BLENDFACTOR_CONST_COLOR;

        case PIPE_BLENDFACTOR_INV_CONST_ALPHA:
                return PIPE_BLENDFACTOR_CONST_ALPHA;

        default:
                return -1;
        }
}

/* Helper to strip the complement from any Gallium blend factor */

static int
uncomplement_factor(int factor)
{
        int complement = complement_factor(factor);
        return (complement == -1) ? factor : complement;
}


/* Attempt to find the dominant factor given a particular factor, complementing
 * as necessary */

static bool
panfrost_make_dominant_factor(unsigned src_factor, enum mali_dominant_factor *factor, bool *invert)
{
        switch (src_factor) {
        case PIPE_BLENDFACTOR_SRC_COLOR:
        case PIPE_BLENDFACTOR_INV_SRC_COLOR:
                *factor = MALI_DOMINANT_SRC_COLOR;
                break;

        case PIPE_BLENDFACTOR_SRC_ALPHA:
        case PIPE_BLENDFACTOR_INV_SRC_ALPHA:
                *factor = MALI_DOMINANT_SRC_ALPHA;
                break;

        case PIPE_BLENDFACTOR_DST_COLOR:
        case PIPE_BLENDFACTOR_INV_DST_COLOR:
                *factor = MALI_DOMINANT_DST_COLOR;
                break;

        case PIPE_BLENDFACTOR_DST_ALPHA:
        case PIPE_BLENDFACTOR_INV_DST_ALPHA:
                *factor = MALI_DOMINANT_DST_ALPHA;
                break;

        case PIPE_BLENDFACTOR_ONE:
        case PIPE_BLENDFACTOR_ZERO:
                *factor = MALI_DOMINANT_ZERO;
                break;

        case PIPE_BLENDFACTOR_CONST_ALPHA:
        case PIPE_BLENDFACTOR_INV_CONST_ALPHA:
        case PIPE_BLENDFACTOR_CONST_COLOR:
        case PIPE_BLENDFACTOR_INV_CONST_COLOR:
                *factor = MALI_DOMINANT_CONSTANT;
                break;

        default:
                /* Fancy blend modes not supported */
                return false;
        }

        /* Set invert flags */

        switch (src_factor) {
        case PIPE_BLENDFACTOR_ONE:
        case PIPE_BLENDFACTOR_INV_SRC_COLOR:
        case PIPE_BLENDFACTOR_INV_SRC_ALPHA:
        case PIPE_BLENDFACTOR_INV_DST_ALPHA:
        case PIPE_BLENDFACTOR_INV_DST_COLOR:
        case PIPE_BLENDFACTOR_INV_CONST_ALPHA:
        case PIPE_BLENDFACTOR_INV_CONST_COLOR:
        case PIPE_BLENDFACTOR_INV_SRC1_COLOR:
        case PIPE_BLENDFACTOR_INV_SRC1_ALPHA:
                *invert = true;

        default:
                break;
        }

        return true;
}

/* Check if this is a special edge case blend factor, which may require the use
 * of clip modifiers */

static bool
is_edge_blendfactor(unsigned factor)
{
        return factor == PIPE_BLENDFACTOR_ONE || factor == PIPE_BLENDFACTOR_ZERO;
}

/* Perform the actual fixed function encoding. Encode the function with negate
 * bits. Check for various cases to work out the dominant/nondominant split and
 * accompanying flags. */

static bool
panfrost_make_fixed_blend_part(unsigned func, unsigned src_factor, unsigned dst_factor, unsigned *out)
{
        struct mali_blend_mode part = { 0 };

        /* Make sure that the blend function is representible with negate flags */

        if (func == PIPE_BLEND_ADD) {
                /* Default, no modifiers needed */
        } else if (func == PIPE_BLEND_SUBTRACT)
                part.negate_dest = true;
        else if (func == PIPE_BLEND_REVERSE_SUBTRACT)
                part.negate_source = true;
        else
                return false;

        part.clip_modifier = MALI_BLEND_MOD_NORMAL;

        /* Decide which is dominant, source or destination. If one is an edge
         * case, use the other as a factor. If they're the same, it doesn't
         * matter; we just mirror. If they're different non-edge-cases, you
         * need a blend shader (don't do that). */

        if (is_edge_blendfactor(dst_factor)) {
                part.dominant = MALI_BLEND_DOM_SOURCE;
                part.nondominant_mode = MALI_BLEND_NON_ZERO;

                if (dst_factor == PIPE_BLENDFACTOR_ONE)
                        part.clip_modifier = MALI_BLEND_MOD_DEST_ONE;
        } else if (is_edge_blendfactor(src_factor)) {
                part.dominant = MALI_BLEND_DOM_DESTINATION;
                part.nondominant_mode = MALI_BLEND_NON_ZERO;

                if (src_factor == PIPE_BLENDFACTOR_ONE)
                        part.clip_modifier = MALI_BLEND_MOD_SOURCE_ONE;

        } else if (src_factor == dst_factor) {
                part.dominant = MALI_BLEND_DOM_DESTINATION; /* Ought to be an arbitrary choice, but we need to set destination for some reason? Align with the blob until we understand more */
                part.nondominant_mode = MALI_BLEND_NON_MIRROR;
        } else if (src_factor == complement_factor(dst_factor)) {
                /* TODO: How does this work exactly? */
                part.dominant = MALI_BLEND_DOM_SOURCE;
                part.nondominant_mode = MALI_BLEND_NON_MIRROR;
                part.clip_modifier = MALI_BLEND_MOD_DEST_ONE;
        } else if (dst_factor == complement_factor(src_factor)) {
                part.dominant = MALI_BLEND_DOM_SOURCE;
                part.nondominant_mode = MALI_BLEND_NON_MIRROR;
                part.clip_modifier = /*MALI_BLEND_MOD_SOURCE_ONE*/MALI_BLEND_MOD_DEST_ONE; /* Which modifier should it be? */
        } else {
                printf("Failed to find dominant factor?\n");
                return false;
        }

        unsigned in_dominant_factor =
                part.dominant == MALI_BLEND_DOM_SOURCE ? src_factor : dst_factor;

        if (part.clip_modifier == MALI_BLEND_MOD_NORMAL && in_dominant_factor == PIPE_BLENDFACTOR_ONE) {
                part.clip_modifier = part.dominant == MALI_BLEND_DOM_SOURCE ? MALI_BLEND_MOD_SOURCE_ONE : MALI_BLEND_MOD_DEST_ONE;
                in_dominant_factor = PIPE_BLENDFACTOR_ZERO;
        }

        bool invert_dominant = false;
        enum mali_dominant_factor dominant_factor;

        if (!panfrost_make_dominant_factor(in_dominant_factor, &dominant_factor, &invert_dominant))
                return false;

        part.dominant_factor = dominant_factor;
        part.complement_dominant = invert_dominant;

        /* Write out mode */
        memcpy(out, &part, sizeof(part));

        return true;
}

/* We can upload a single constant for all of the factors. So, scan the factors
 * for constants used, and scan the constants for the constants used. If there
 * is a single unique constant, output that. If there are multiple,
 * fixed-function operation breaks down. */

static bool
panfrost_make_constant(unsigned *factors, unsigned num_factors, const struct pipe_blend_color *blend_color, void *out)
{
        /* Color components used */
        bool cc[4] = { false };

        for (unsigned i = 0; i < num_factors; ++i) {
                unsigned factor = uncomplement_factor(factors[i]);

                if (factor == PIPE_BLENDFACTOR_CONST_COLOR)
                        cc[0] = cc[1] = cc[2] = true;
                else if (factor == PIPE_BLENDFACTOR_CONST_ALPHA)
                        cc[3] = true;
        }

        /* Find the actual constant associated with the components used*/

        float constant = 0.0;
        bool has_constant = false;

        for (unsigned i = 0; i < 4; ++i) {
                /* If the component is unused, nothing to do */
                if (!cc[i]) continue;

                float value = blend_color->color[i];

                /* Either there's a second constant, in which case we fail, or
                 * there's no constant / a first constant, in which case we use
                 * that constant */

                if (has_constant && constant != value) {
                        return false;
                } else {
                        has_constant = true;
                        constant = value;
                }
        }

        /* We have the constant -- success! */

        memcpy(out, &constant, sizeof(float));
        return true;
}

/* Create the descriptor for a fixed blend mode given the corresponding Gallium
 * state, if possible. Return true and write out the blend descriptor into
 * blend_equation. If it is not possible with the fixed function
 * representating, return false to handle degenerate cases with a blend shader
 */

static const struct pipe_rt_blend_state default_blend = {
        .blend_enable = 1,

        .rgb_func = PIPE_BLEND_ADD,
        .rgb_src_factor = PIPE_BLENDFACTOR_ONE,
        .rgb_dst_factor = PIPE_BLENDFACTOR_ZERO,

        .alpha_func = PIPE_BLEND_ADD,
        .alpha_src_factor = PIPE_BLENDFACTOR_ONE,
        .alpha_dst_factor = PIPE_BLENDFACTOR_ZERO,

        .colormask = PIPE_MASK_RGBA
};

bool
panfrost_make_fixed_blend_mode(const struct pipe_rt_blend_state *blend, struct mali_blend_equation *out, unsigned colormask, const struct pipe_blend_color *blend_color)
{
        /* If no blending is enabled, default back on `replace` mode */

        if (!blend->blend_enable)
                return panfrost_make_fixed_blend_mode(&default_blend, out, colormask, blend_color);

        /* We have room only for a single float32 constant between the four
         * components. If we need more, spill to the programmable pipeline. */

        unsigned factors[] = {
                blend->rgb_src_factor, blend->rgb_dst_factor,
                blend->alpha_src_factor, blend->alpha_dst_factor,
        };

        if (!panfrost_make_constant(factors, ARRAY_SIZE(factors), blend_color, &out->constant))
                return false;

        unsigned rgb_mode = 0;
        unsigned alpha_mode = 0;

        if (!panfrost_make_fixed_blend_part(
                                blend->rgb_func, blend->rgb_src_factor, blend->rgb_dst_factor,
                                &rgb_mode))
                return false;

        if (!panfrost_make_fixed_blend_part(
                                blend->alpha_func, blend->alpha_src_factor, blend->alpha_dst_factor,
                                &alpha_mode))
                return false;

        out->rgb_mode = rgb_mode;
        out->alpha_mode = alpha_mode;

        /* Gallium and Mali represent colour masks identically. XXX: Static assert for future proof */
        out->color_mask = colormask;

        return true;
}
