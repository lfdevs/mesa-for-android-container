/*
 * Copyright © 2026 Raspberry Pi Ltd
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "v3d_compiler.h"

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_builtin_builder.h"

static nir_def *
get_is_null(nir_builder *b, nir_instr *instr, nir_def **def)
{
        *def = NULL;

        if (instr->type == nir_instr_type_tex) {
                nir_tex_instr *tex = nir_instr_as_tex(instr);
                *def = &tex->def;
                nir_def *size = nir_build_texture_query(b, tex, nir_texop_txs,
                                                        1, nir_type_uint32,
                                                        false, true);
                if (size->num_components > 1)
                        size = nir_channel(b, size, 0);

                return nir_ieq_imm(b, size, 0);
        }

        if (instr->type == nir_instr_type_intrinsic) {
                nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

                if (nir_intrinsic_infos[intr->intrinsic].has_dest)
                        *def = &intr->def;

                nir_src *index_src = nir_get_io_index_src(intr);

                switch (intr->intrinsic) {
                case nir_intrinsic_load_ubo:
                        return nir_ieq_imm(b, nir_get_ubo_size(b, 32, index_src->ssa), 0);

                case nir_intrinsic_load_ssbo:
                case nir_intrinsic_ssbo_atomic:
                case nir_intrinsic_ssbo_atomic_swap:
                case nir_intrinsic_store_ssbo:
                        return nir_ieq_imm(b, nir_get_ssbo_size(b, 32, index_src->ssa), 0);

                case nir_intrinsic_image_load:
                case nir_intrinsic_image_atomic:
                case nir_intrinsic_image_atomic_swap:
                case nir_intrinsic_image_store: {
                        nir_def *size = nir_image_size(
                                b, 1, 32,
                                index_src->ssa, nir_imm_int(b, 0),
                                .image_array = nir_intrinsic_image_array(intr),
                                .image_dim = nir_intrinsic_image_dim(intr));
                        return nir_ieq_imm(b, size, 0);
                }

                default:
                        return NULL;
                }
        }

        return NULL;
}

static bool
lower_instr(nir_builder *b, nir_instr *instr, UNUSED void *data)
{
        b->cursor = nir_before_instr(instr);

        nir_def *def;
        nir_def *is_null = get_is_null(b, instr, &def);
        if (!is_null)
                return false;

        if (instr->type == nir_instr_type_tex) {
                /* We use bcsel instead of if/else+phi for texture ops because
                 * it has proven to lead to better performance than if/else
                 * blocks. When null descriptor functionality is enabled and a
                 * null texture is bound by the application, the driver will
                 * program a dummy BO to ensure valid access from the texture
                 * instruction and the bcsel below will discard the result and
                 * replace it with 0.
                 */
                b->cursor = nir_after_instr(instr);
                nir_def *zero = nir_imm_zero(b, def->num_components,
                                             def->bit_size);
                nir_def *result = nir_bcsel(b, is_null, zero, def);

                nir_def_rewrite_uses_after(def, result);

                return true;
        }

        nir_def *zero = NULL;
        nir_if *nif = nir_push_if(b, nir_inot(b, is_null));
        nir_instr_remove(instr);
        nir_builder_instr_insert(b, instr);
        if (def) {
                nir_push_else(b, nif);
                zero = nir_imm_zero(b, def->num_components, def->bit_size);
        }
        nir_pop_if(b, nif);

        if (def) {
                nir_def *phi = nir_if_phi(b, def, zero);

                /* We can't use nir_def_rewrite_uses_after on phis,
                 * so use the global version and fixup the phi manually.
                 */
                nir_def_rewrite_uses(def, phi);

                nir_instr *phi_instr = nir_def_instr(phi);
                nir_phi_instr *phi_as_phi = nir_instr_as_phi(phi_instr);
                nir_phi_src *phi_src = nir_phi_get_src_from_block(phi_as_phi, instr->block);
                nir_src_rewrite(&phi_src->src, def);
        }

        return true;
}

bool
v3d_nir_lower_null_descriptors(nir_shader *s)
{
        return nir_shader_instructions_pass(s, lower_instr,
                                            nir_metadata_none, NULL);
}
