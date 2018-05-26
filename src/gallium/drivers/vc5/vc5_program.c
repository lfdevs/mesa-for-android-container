/*
 * Copyright © 2014-2017 Broadcom
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

#include <inttypes.h>
#include "util/u_format.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/ralloc.h"
#include "util/hash_table.h"
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_parse.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "nir/tgsi_to_nir.h"
#include "compiler/v3d_compiler.h"
#include "vc5_context.h"
#include "broadcom/cle/v3d_packet_v33_pack.h"
#include "mesa/state_tracker/st_glsl_types.h"

static gl_varying_slot
vc5_get_slot_for_driver_location(nir_shader *s, uint32_t driver_location)
{
        nir_foreach_variable(var, &s->outputs) {
                if (var->data.driver_location == driver_location) {
                        return var->data.location;
                }
        }

        return -1;
}

/**
 * Precomputes the TRANSFORM_FEEDBACK_OUTPUT_DATA_SPEC array for the shader.
 *
 * A shader can have 16 of these specs, and each one of them can write up to
 * 16 dwords.  Since we allow a total of 64 transform feedback output
 * components (not 16 vectors), we have to group the writes of multiple
 * varyings together in a single data spec.
 */
static void
vc5_set_transform_feedback_outputs(struct vc5_uncompiled_shader *so,
                                   const struct pipe_stream_output_info *stream_output)
{
        if (!stream_output->num_outputs)
                return;

        struct v3d_varying_slot slots[PIPE_MAX_SO_OUTPUTS * 4];
        int slot_count = 0;

        for (int buffer = 0; buffer < PIPE_MAX_SO_BUFFERS; buffer++) {
                uint32_t buffer_offset = 0;
                uint32_t vpm_start = slot_count;

                for (int i = 0; i < stream_output->num_outputs; i++) {
                        const struct pipe_stream_output *output =
                                &stream_output->output[i];

                        if (output->output_buffer != buffer)
                                continue;

                        /* We assume that the SO outputs appear in increasing
                         * order in the buffer.
                         */
                        assert(output->dst_offset >= buffer_offset);

                        /* Pad any undefined slots in the output */
                        for (int j = buffer_offset; j < output->dst_offset; j++) {
                                slots[slot_count] =
                                        v3d_slot_from_slot_and_component(VARYING_SLOT_POS, 0);
                                slot_count++;
                                buffer_offset++;
                        }

                        /* Set the coordinate shader up to output the
                         * components of this varying.
                         */
                        for (int j = 0; j < output->num_components; j++) {
                                gl_varying_slot slot =
                                        vc5_get_slot_for_driver_location(so->base.ir.nir, output->register_index);

                                slots[slot_count] =
                                        v3d_slot_from_slot_and_component(slot,
                                                                         output->start_component + j);
                                slot_count++;
                                buffer_offset++;
                        }
                }

                uint32_t vpm_size = slot_count - vpm_start;
                if (!vpm_size)
                        continue;

                uint32_t vpm_start_offset = vpm_start + 6;

                while (vpm_size) {
                        uint32_t write_size = MIN2(vpm_size, 1 << 4);

                        struct V3D33_TRANSFORM_FEEDBACK_OUTPUT_DATA_SPEC unpacked = {
                                /* We need the offset from the coordinate shader's VPM
                                 * output block, which has the [X, Y, Z, W, Xs, Ys]
                                 * values at the start.
                                 */
                                .first_shaded_vertex_value_to_output = vpm_start_offset,
                                .number_of_consecutive_vertex_values_to_output_as_32_bit_values_minus_1 = write_size - 1,
                                .output_buffer_to_write_to = buffer,
                        };

                        assert(so->num_tf_specs != ARRAY_SIZE(so->tf_specs));
                        V3D33_TRANSFORM_FEEDBACK_OUTPUT_DATA_SPEC_pack(NULL,
                                                                       (void *)&so->tf_specs[so->num_tf_specs],
                                                                       &unpacked);

                        /* If point size is being written by the shader, then
                         * all the VPM start offsets are shifted up by one.
                         * We won't know that until the variant is compiled,
                         * though.
                         */
                        unpacked.first_shaded_vertex_value_to_output++;
                        V3D33_TRANSFORM_FEEDBACK_OUTPUT_DATA_SPEC_pack(NULL,
                                                                       (void *)&so->tf_specs_psiz[so->num_tf_specs],
                                                                       &unpacked);
                        so->num_tf_specs++;
                        vpm_start_offset += write_size;
                        vpm_size -= write_size;
                }
        }

        so->num_tf_outputs = slot_count;
        so->tf_outputs = ralloc_array(so->base.ir.nir, struct v3d_varying_slot,
                                      slot_count);
        memcpy(so->tf_outputs, slots, sizeof(*slots) * slot_count);
}

static int
type_size(const struct glsl_type *type)
{
        return glsl_count_attribute_slots(type, false);
}

static int
uniforms_type_size(const struct glsl_type *type)
{
        return st_glsl_storage_type_size(type, false);
}

static void *
vc5_shader_state_create(struct pipe_context *pctx,
                        const struct pipe_shader_state *cso)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        struct vc5_uncompiled_shader *so = CALLOC_STRUCT(vc5_uncompiled_shader);
        if (!so)
                return NULL;

        so->program_id = vc5->next_uncompiled_program_id++;

        nir_shader *s;

        if (cso->type == PIPE_SHADER_IR_NIR) {
                /* The backend takes ownership of the NIR shader on state
                 * creation.
                 */
                s = cso->ir.nir;

                NIR_PASS_V(s, nir_lower_io, nir_var_all & ~nir_var_uniform,
                           type_size,
                           (nir_lower_io_options)0);
                NIR_PASS_V(s, nir_lower_io, nir_var_uniform,
                           uniforms_type_size,
                           (nir_lower_io_options)0);
        } else {
                assert(cso->type == PIPE_SHADER_IR_TGSI);

                if (V3D_DEBUG & V3D_DEBUG_TGSI) {
                        fprintf(stderr, "prog %d TGSI:\n",
                                so->program_id);
                        tgsi_dump(cso->tokens, 0);
                        fprintf(stderr, "\n");
                }
                s = tgsi_to_nir(cso->tokens, &v3d_nir_options);

                so->was_tgsi = true;
        }

        NIR_PASS_V(s, nir_opt_global_to_local);
        NIR_PASS_V(s, nir_lower_regs_to_ssa);
        NIR_PASS_V(s, nir_normalize_cubemap_coords);

        NIR_PASS_V(s, nir_lower_load_const_to_scalar);

        v3d_optimize_nir(s);

        NIR_PASS_V(s, nir_remove_dead_variables, nir_var_local);

        /* Garbage collect dead instructions */
        nir_sweep(s);

        so->base.type = PIPE_SHADER_IR_NIR;
        so->base.ir.nir = s;

        vc5_set_transform_feedback_outputs(so, &cso->stream_output);

        if (V3D_DEBUG & (V3D_DEBUG_NIR |
                         v3d_debug_flag_for_shader_stage(s->info.stage))) {
                fprintf(stderr, "%s prog %d NIR:\n",
                        gl_shader_stage_name(s->info.stage),
                        so->program_id);
                nir_print_shader(s, stderr);
                fprintf(stderr, "\n");
        }

        return so;
}

static struct vc5_compiled_shader *
vc5_get_compiled_shader(struct vc5_context *vc5, struct v3d_key *key)
{
        struct vc5_uncompiled_shader *shader_state = key->shader_state;
        nir_shader *s = shader_state->base.ir.nir;

        struct hash_table *ht;
        uint32_t key_size;
        if (s->info.stage == MESA_SHADER_FRAGMENT) {
                ht = vc5->fs_cache;
                key_size = sizeof(struct v3d_fs_key);
        } else {
                ht = vc5->vs_cache;
                key_size = sizeof(struct v3d_vs_key);
        }

        struct hash_entry *entry = _mesa_hash_table_search(ht, key);
        if (entry)
                return entry->data;

        struct vc5_compiled_shader *shader =
                rzalloc(NULL, struct vc5_compiled_shader);

        int program_id = shader_state->program_id;
        int variant_id =
                p_atomic_inc_return(&shader_state->compiled_variant_count);
        uint64_t *qpu_insts;
        uint32_t shader_size;

        switch (s->info.stage) {
        case MESA_SHADER_VERTEX:
                shader->prog_data.vs = rzalloc(shader, struct v3d_vs_prog_data);

                qpu_insts = v3d_compile_vs(vc5->screen->compiler,
                                           (struct v3d_vs_key *)key,
                                           shader->prog_data.vs, s,
                                           program_id, variant_id,
                                           &shader_size);
                break;
        case MESA_SHADER_FRAGMENT:
                shader->prog_data.fs = rzalloc(shader, struct v3d_fs_prog_data);

                qpu_insts = v3d_compile_fs(vc5->screen->compiler,
                                           (struct v3d_fs_key *)key,
                                           shader->prog_data.fs, s,
                                           program_id, variant_id,
                                           &shader_size);
                break;
        default:
                unreachable("bad stage");
        }

        vc5_set_shader_uniform_dirty_flags(shader);

        shader->bo = vc5_bo_alloc(vc5->screen, shader_size, "shader");
        vc5_bo_map(shader->bo);
        memcpy(shader->bo->map, qpu_insts, shader_size);

        free(qpu_insts);

        struct vc5_key *dup_key;
        dup_key = ralloc_size(shader, key_size);
        memcpy(dup_key, key, key_size);
        _mesa_hash_table_insert(ht, dup_key, shader);

        if (shader->prog_data.base->spill_size >
            vc5->prog.spill_size_per_thread) {
                /* Max 4 QPUs per slice, 3 slices per core. We only do single
                 * core so far.  This overallocates memory on smaller cores.
                 */
                int total_spill_size =
                        4 * 3 * shader->prog_data.base->spill_size;

                vc5_bo_unreference(&vc5->prog.spill_bo);
                vc5->prog.spill_bo = vc5_bo_alloc(vc5->screen,
                                                  total_spill_size, "spill");
                vc5->prog.spill_size_per_thread =
                        shader->prog_data.base->spill_size;
        }

        return shader;
}

static void
vc5_setup_shared_key(struct vc5_context *vc5, struct v3d_key *key,
                     struct vc5_texture_stateobj *texstate)
{
        const struct v3d_device_info *devinfo = &vc5->screen->devinfo;

        for (int i = 0; i < texstate->num_textures; i++) {
                struct pipe_sampler_view *sampler = texstate->textures[i];
                struct vc5_sampler_view *vc5_sampler = vc5_sampler_view(sampler);
                struct pipe_sampler_state *sampler_state =
                        texstate->samplers[i];

                if (!sampler)
                        continue;

                key->tex[i].return_size =
                        vc5_get_tex_return_size(devinfo,
                                                sampler->format,
                                                sampler_state->compare_mode);

                /* For 16-bit, we set up the sampler to always return 2
                 * channels (meaning no recompiles for most statechanges),
                 * while for 32 we actually scale the returns with channels.
                 */
                if (key->tex[i].return_size == 16) {
                        key->tex[i].return_channels = 2;
                } else if (devinfo->ver > 40) {
                        key->tex[i].return_channels = 4;
                } else {
                        key->tex[i].return_channels =
                                vc5_get_tex_return_channels(devinfo,
                                                            sampler->format);
                }

                if (key->tex[i].return_size == 32 && devinfo->ver < 40) {
                        memcpy(key->tex[i].swizzle,
                               vc5_sampler->swizzle,
                               sizeof(vc5_sampler->swizzle));
                } else {
                        /* For 16-bit returns, we let the sampler state handle
                         * the swizzle.
                         */
                        key->tex[i].swizzle[0] = PIPE_SWIZZLE_X;
                        key->tex[i].swizzle[1] = PIPE_SWIZZLE_Y;
                        key->tex[i].swizzle[2] = PIPE_SWIZZLE_Z;
                        key->tex[i].swizzle[3] = PIPE_SWIZZLE_W;
                }

                if (sampler->texture->nr_samples > 1) {
                        key->tex[i].msaa_width = sampler->texture->width0;
                        key->tex[i].msaa_height = sampler->texture->height0;
                } else if (sampler){
                        key->tex[i].compare_mode = sampler_state->compare_mode;
                        key->tex[i].compare_func = sampler_state->compare_func;
                        key->tex[i].clamp_s =
                                sampler_state->wrap_s == PIPE_TEX_WRAP_CLAMP;
                        key->tex[i].clamp_t =
                                sampler_state->wrap_t == PIPE_TEX_WRAP_CLAMP;
                        key->tex[i].clamp_r =
                                sampler_state->wrap_r == PIPE_TEX_WRAP_CLAMP;
                }
        }

        key->ucp_enables = vc5->rasterizer->base.clip_plane_enable;
}

static void
vc5_update_compiled_fs(struct vc5_context *vc5, uint8_t prim_mode)
{
        struct vc5_job *job = vc5->job;
        struct v3d_fs_key local_key;
        struct v3d_fs_key *key = &local_key;

        if (!(vc5->dirty & (VC5_DIRTY_PRIM_MODE |
                            VC5_DIRTY_BLEND |
                            VC5_DIRTY_FRAMEBUFFER |
                            VC5_DIRTY_ZSA |
                            VC5_DIRTY_RASTERIZER |
                            VC5_DIRTY_SAMPLE_MASK |
                            VC5_DIRTY_FRAGTEX |
                            VC5_DIRTY_UNCOMPILED_FS))) {
                return;
        }

        memset(key, 0, sizeof(*key));
        vc5_setup_shared_key(vc5, &key->base, &vc5->fragtex);
        key->base.shader_state = vc5->prog.bind_fs;
        key->is_points = (prim_mode == PIPE_PRIM_POINTS);
        key->is_lines = (prim_mode >= PIPE_PRIM_LINES &&
                         prim_mode <= PIPE_PRIM_LINE_STRIP);
        key->clamp_color = vc5->rasterizer->base.clamp_fragment_color;
        if (vc5->blend->logicop_enable) {
                key->logicop_func = vc5->blend->logicop_func;
        } else {
                key->logicop_func = PIPE_LOGICOP_COPY;
        }
        if (job->msaa) {
                key->msaa = vc5->rasterizer->base.multisample;
                key->sample_coverage = (vc5->rasterizer->base.multisample &&
                                        vc5->sample_mask != (1 << VC5_MAX_SAMPLES) - 1);
                key->sample_alpha_to_coverage = vc5->blend->alpha_to_coverage;
                key->sample_alpha_to_one = vc5->blend->alpha_to_one;
        }

        key->depth_enabled = (vc5->zsa->base.depth.enabled ||
                              vc5->zsa->base.stencil[0].enabled);
        if (vc5->zsa->base.alpha.enabled) {
                key->alpha_test = true;
                key->alpha_test_func = vc5->zsa->base.alpha.func;
        }

        /* gl_FragColor's propagation to however many bound color buffers
         * there are means that the buffer count needs to be in the key.
         */
        key->nr_cbufs = vc5->framebuffer.nr_cbufs;
        key->swap_color_rb = vc5->swap_color_rb;

        for (int i = 0; i < key->nr_cbufs; i++) {
                struct pipe_surface *cbuf = vc5->framebuffer.cbufs[i];
                if (!cbuf)
                        continue;

                const struct util_format_description *desc =
                        util_format_description(cbuf->format);

                if (desc->channel[0].type == UTIL_FORMAT_TYPE_FLOAT &&
                    desc->channel[0].size == 32) {
                        key->f32_color_rb |= 1 << i;
                }

                if (vc5->prog.bind_fs->was_tgsi) {
                        if (util_format_is_pure_uint(cbuf->format))
                                key->uint_color_rb |= 1 << i;
                        else if (util_format_is_pure_sint(cbuf->format))
                                key->int_color_rb |= 1 << i;
                }
        }

        if (key->is_points) {
                key->point_sprite_mask =
                        vc5->rasterizer->base.sprite_coord_enable;
                key->point_coord_upper_left =
                        (vc5->rasterizer->base.sprite_coord_mode ==
                         PIPE_SPRITE_COORD_UPPER_LEFT);
        }

        key->light_twoside = vc5->rasterizer->base.light_twoside;
        key->shade_model_flat = vc5->rasterizer->base.flatshade;

        struct vc5_compiled_shader *old_fs = vc5->prog.fs;
        vc5->prog.fs = vc5_get_compiled_shader(vc5, &key->base);
        if (vc5->prog.fs == old_fs)
                return;

        vc5->dirty |= VC5_DIRTY_COMPILED_FS;

        if (old_fs &&
            vc5->prog.fs->prog_data.fs->flat_shade_flags !=
            old_fs->prog_data.fs->flat_shade_flags) {
                vc5->dirty |= VC5_DIRTY_FLAT_SHADE_FLAGS;
        }

        if (old_fs && memcmp(vc5->prog.fs->prog_data.fs->input_slots,
                             old_fs->prog_data.fs->input_slots,
                             sizeof(vc5->prog.fs->prog_data.fs->input_slots))) {
                vc5->dirty |= VC5_DIRTY_FS_INPUTS;
        }
}

static void
vc5_update_compiled_vs(struct vc5_context *vc5, uint8_t prim_mode)
{
        struct v3d_vs_key local_key;
        struct v3d_vs_key *key = &local_key;

        if (!(vc5->dirty & (VC5_DIRTY_PRIM_MODE |
                            VC5_DIRTY_RASTERIZER |
                            VC5_DIRTY_VERTTEX |
                            VC5_DIRTY_VTXSTATE |
                            VC5_DIRTY_UNCOMPILED_VS |
                            VC5_DIRTY_FS_INPUTS))) {
                return;
        }

        memset(key, 0, sizeof(*key));
        vc5_setup_shared_key(vc5, &key->base, &vc5->verttex);
        key->base.shader_state = vc5->prog.bind_vs;
        key->num_fs_inputs = vc5->prog.fs->prog_data.fs->base.num_inputs;
        STATIC_ASSERT(sizeof(key->fs_inputs) ==
                      sizeof(vc5->prog.fs->prog_data.fs->input_slots));
        memcpy(key->fs_inputs, vc5->prog.fs->prog_data.fs->input_slots,
               sizeof(key->fs_inputs));
        key->clamp_color = vc5->rasterizer->base.clamp_vertex_color;

        key->per_vertex_point_size =
                (prim_mode == PIPE_PRIM_POINTS &&
                 vc5->rasterizer->base.point_size_per_vertex);

        struct vc5_compiled_shader *vs =
                vc5_get_compiled_shader(vc5, &key->base);
        if (vs != vc5->prog.vs) {
                vc5->prog.vs = vs;
                vc5->dirty |= VC5_DIRTY_COMPILED_VS;
        }

        key->is_coord = true;
        /* Coord shaders only output varyings used by transform feedback. */
        struct vc5_uncompiled_shader *shader_state = key->base.shader_state;
        memcpy(key->fs_inputs, shader_state->tf_outputs,
               sizeof(*key->fs_inputs) * shader_state->num_tf_outputs);
        if (shader_state->num_tf_outputs < key->num_fs_inputs) {
                memset(&key->fs_inputs[shader_state->num_tf_outputs],
                       0,
                       sizeof(*key->fs_inputs) * (key->num_fs_inputs -
                                                  shader_state->num_tf_outputs));
        }
        key->num_fs_inputs = shader_state->num_tf_outputs;

        struct vc5_compiled_shader *cs =
                vc5_get_compiled_shader(vc5, &key->base);
        if (cs != vc5->prog.cs) {
                vc5->prog.cs = cs;
                vc5->dirty |= VC5_DIRTY_COMPILED_CS;
        }
}

void
vc5_update_compiled_shaders(struct vc5_context *vc5, uint8_t prim_mode)
{
        vc5_update_compiled_fs(vc5, prim_mode);
        vc5_update_compiled_vs(vc5, prim_mode);
}

static uint32_t
fs_cache_hash(const void *key)
{
        return _mesa_hash_data(key, sizeof(struct v3d_fs_key));
}

static uint32_t
vs_cache_hash(const void *key)
{
        return _mesa_hash_data(key, sizeof(struct v3d_vs_key));
}

static bool
fs_cache_compare(const void *key1, const void *key2)
{
        return memcmp(key1, key2, sizeof(struct v3d_fs_key)) == 0;
}

static bool
vs_cache_compare(const void *key1, const void *key2)
{
        return memcmp(key1, key2, sizeof(struct v3d_vs_key)) == 0;
}

static void
delete_from_cache_if_matches(struct hash_table *ht,
                             struct vc5_compiled_shader **last_compile,
                             struct hash_entry *entry,
                             struct vc5_uncompiled_shader *so)
{
        const struct v3d_key *key = entry->key;

        if (key->shader_state == so) {
                struct vc5_compiled_shader *shader = entry->data;
                _mesa_hash_table_remove(ht, entry);
                vc5_bo_unreference(&shader->bo);

                if (shader == *last_compile)
                        *last_compile = NULL;

                ralloc_free(shader);
        }
}

static void
vc5_shader_state_delete(struct pipe_context *pctx, void *hwcso)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        struct vc5_uncompiled_shader *so = hwcso;

        struct hash_entry *entry;
        hash_table_foreach(vc5->fs_cache, entry) {
                delete_from_cache_if_matches(vc5->fs_cache, &vc5->prog.fs,
                                             entry, so);
        }
        hash_table_foreach(vc5->vs_cache, entry) {
                delete_from_cache_if_matches(vc5->vs_cache, &vc5->prog.vs,
                                             entry, so);
        }

        ralloc_free(so->base.ir.nir);
        free(so);
}

static void
vc5_fp_state_bind(struct pipe_context *pctx, void *hwcso)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        vc5->prog.bind_fs = hwcso;
        vc5->dirty |= VC5_DIRTY_UNCOMPILED_FS;
}

static void
vc5_vp_state_bind(struct pipe_context *pctx, void *hwcso)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        vc5->prog.bind_vs = hwcso;
        vc5->dirty |= VC5_DIRTY_UNCOMPILED_VS;
}

void
vc5_program_init(struct pipe_context *pctx)
{
        struct vc5_context *vc5 = vc5_context(pctx);

        pctx->create_vs_state = vc5_shader_state_create;
        pctx->delete_vs_state = vc5_shader_state_delete;

        pctx->create_fs_state = vc5_shader_state_create;
        pctx->delete_fs_state = vc5_shader_state_delete;

        pctx->bind_fs_state = vc5_fp_state_bind;
        pctx->bind_vs_state = vc5_vp_state_bind;

        vc5->fs_cache = _mesa_hash_table_create(pctx, fs_cache_hash,
                                                fs_cache_compare);
        vc5->vs_cache = _mesa_hash_table_create(pctx, vs_cache_hash,
                                                vs_cache_compare);
}

void
vc5_program_fini(struct pipe_context *pctx)
{
        struct vc5_context *vc5 = vc5_context(pctx);

        struct hash_entry *entry;
        hash_table_foreach(vc5->fs_cache, entry) {
                struct vc5_compiled_shader *shader = entry->data;
                vc5_bo_unreference(&shader->bo);
                ralloc_free(shader);
                _mesa_hash_table_remove(vc5->fs_cache, entry);
        }

        hash_table_foreach(vc5->vs_cache, entry) {
                struct vc5_compiled_shader *shader = entry->data;
                vc5_bo_unreference(&shader->bo);
                ralloc_free(shader);
                _mesa_hash_table_remove(vc5->vs_cache, entry);
        }
}
