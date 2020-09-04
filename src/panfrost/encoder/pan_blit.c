/*
 * Copyright (C) 2020 Collabora, Ltd.
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
 * Authors:
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include <math.h>
#include <stdio.h>
#include "pan_encoder.h"
#include "pan_pool.h"
#include "pan_scoreboard.h"
#include "pan_texture.h"
#include "panfrost-quirks.h"
#include "../midgard/midgard_compile.h"
#include "compiler/nir/nir_builder.h"
#include "util/u_math.h"

/* On Midgard, the native blit infrastructure (via MFBD preloads) is broken or
 * missing in many cases. We instead use software paths as fallbacks to
 * implement blits, which are done as TILER jobs. No vertex shader is
 * necessary since we can supply screen-space coordinates directly.
 *
 * This is primarily designed as a fallback for preloads but could be extended
 * for other clears/blits if needed in the future. */

static void
panfrost_build_blit_shader(panfrost_program *program, unsigned gpu_id, gl_frag_result loc, nir_alu_type T, bool ms)
{
        bool is_colour = loc >= FRAG_RESULT_DATA0;

        nir_shader *shader = nir_shader_create(NULL, MESA_SHADER_FRAGMENT, &midgard_nir_options, NULL);
        nir_function *fn = nir_function_create(shader, "main");
        nir_function_impl *impl = nir_function_impl_create(fn);

        nir_variable *c_src = nir_variable_create(shader, nir_var_shader_in, glsl_vector_type(GLSL_TYPE_FLOAT, 2), "coord");
        nir_variable *c_out = nir_variable_create(shader, nir_var_shader_out, glsl_vector_type(
                                GLSL_TYPE_FLOAT, is_colour ? 4 : 1), "out");

        c_src->data.location = VARYING_SLOT_TEX0;
        c_out->data.location = loc;

        nir_builder _b;
        nir_builder *b = &_b;
        nir_builder_init(b, impl);
        b->cursor = nir_before_block(nir_start_block(impl));

        nir_ssa_def *coord = nir_load_var(b, c_src);

        nir_tex_instr *tex = nir_tex_instr_create(shader, ms ? 3 : 1);

        tex->dest_type = T;

        if (ms) {
                tex->src[0].src_type = nir_tex_src_coord;
                tex->src[0].src = nir_src_for_ssa(nir_f2i32(b, coord));
                tex->coord_components = 2;
 
                tex->src[1].src_type = nir_tex_src_ms_index;
                tex->src[1].src = nir_src_for_ssa(nir_load_sample_id(b));

                tex->src[2].src_type = nir_tex_src_lod;
                tex->src[2].src = nir_src_for_ssa(nir_imm_int(b, 0));
                tex->sampler_dim = GLSL_SAMPLER_DIM_MS;
                tex->op = nir_texop_txf_ms;
        } else {
                tex->op = nir_texop_tex;

                tex->src[0].src_type = nir_tex_src_coord;
                tex->src[0].src = nir_src_for_ssa(coord);
                tex->coord_components = 2;

                tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
        }

        nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, NULL);
        nir_builder_instr_insert(b, &tex->instr);

        if (is_colour)
                nir_store_var(b, c_out, &tex->dest.ssa, 0xFF);
        else
                nir_store_var(b, c_out, nir_channel(b, &tex->dest.ssa, 0), 0xFF);

        midgard_compile_shader_nir(shader, program, false, 0, gpu_id, false, true);
        ralloc_free(shader);
}

/* Compile and upload all possible blit shaders ahead-of-time to reduce draw
 * time overhead. There's only ~30 of them at the moment, so this is fine */

void
panfrost_init_blit_shaders(struct panfrost_device *dev)
{
        static const struct {
                gl_frag_result loc;
                unsigned types;
        } shader_descs[] = {
                { FRAG_RESULT_DEPTH,   1 << PAN_BLIT_FLOAT },
                { FRAG_RESULT_STENCIL, 1 << PAN_BLIT_UINT },
                { FRAG_RESULT_DATA0,  ~0 },
                { FRAG_RESULT_DATA1,  ~0 },
                { FRAG_RESULT_DATA2,  ~0 },
                { FRAG_RESULT_DATA3,  ~0 },
                { FRAG_RESULT_DATA4,  ~0 },
                { FRAG_RESULT_DATA5,  ~0 },
                { FRAG_RESULT_DATA6,  ~0 },
                { FRAG_RESULT_DATA7,  ~0 }
        };

        nir_alu_type nir_types[PAN_BLIT_NUM_TYPES] = {
                nir_type_float,
                nir_type_uint,
                nir_type_int
        };

        /* Total size = # of shaders * bytes per shader. There are
         * shaders for each RT (so up to DATA7 -- overestimate is
         * okay) and up to NUM_TYPES variants of each, * 2 for multisampling
         * variants. These shaders are simple enough that they should be less
         * than 8 quadwords each (again, overestimate is fine). */

        unsigned offset = 0;
        unsigned total_size = (FRAG_RESULT_DATA7 * PAN_BLIT_NUM_TYPES)
                * (8 * 16) * 2;

        dev->blit_shaders.bo = panfrost_bo_create(dev, total_size, PAN_BO_EXECUTE);

        /* Don't bother generating multisampling variants if we don't actually
         * support multisampling */
        bool has_ms = !(dev->quirks & MIDGARD_SFBD);

        for (unsigned ms = 0; ms <= has_ms; ++ms) {
                for (unsigned i = 0; i < ARRAY_SIZE(shader_descs); ++i) {
                        unsigned loc = shader_descs[i].loc;

                        for (enum pan_blit_type T = 0; T < PAN_BLIT_NUM_TYPES; ++T) {
                                if (!(shader_descs[i].types & (1 << T)))
                                        continue;

                                panfrost_program program;
                                panfrost_build_blit_shader(&program, dev->gpu_id, loc,
                                                nir_types[T], ms);

                                assert(offset + program.compiled.size < total_size);
                                memcpy(dev->blit_shaders.bo->cpu + offset, program.compiled.data, program.compiled.size);

                                dev->blit_shaders.loads[loc][T][ms] = (dev->blit_shaders.bo->gpu + offset) | program.first_tag;
                                offset += ALIGN_POT(program.compiled.size, 64);
                                util_dynarray_fini(&program.compiled);
                        }
                }
        }
}

/* Add a shader-based load on Midgard (draw-time for GL). Shaders are
 * precached */

void
panfrost_load_midg(
                struct pan_pool *pool,
                struct pan_scoreboard *scoreboard,
                mali_ptr blend_shader,
                mali_ptr fbd,
                mali_ptr coordinates, unsigned vertex_count,
                struct pan_image *image,
                unsigned loc)
{
        unsigned width = u_minify(image->width0, image->first_level);
        unsigned height = u_minify(image->height0, image->first_level);

        struct mali_viewport viewport = {
                .clip_minx = -INFINITY,
                .clip_miny = -INFINITY,
                .clip_maxx = INFINITY,
                .clip_maxy = INFINITY,
                .clip_minz = 0.0,
                .clip_maxz = 1.0,

                .viewport0 = { 0, 0 },
                .viewport1 = { MALI_POSITIVE(width), MALI_POSITIVE(height) }
        };

        union mali_attr varying = {
		.elements = coordinates | MALI_ATTR_LINEAR,
		.stride = 4 * sizeof(float),
		.size = 4 * sizeof(float) * vertex_count,
	};

        struct mali_attr_meta varying_meta = {
                .index = 0,
                .unknown1 = 2,
                .swizzle = (MALI_CHANNEL_RED << 0) | (MALI_CHANNEL_GREEN << 3),
                .format = MALI_RGBA32F
        };

        struct mali_stencil_test stencil = {
                .mask = 0xFF,
                .func = MALI_FUNC_ALWAYS,
                .sfail = MALI_STENCIL_REPLACE,
                .dpfail = MALI_STENCIL_REPLACE,
                .dppass = MALI_STENCIL_REPLACE,
        };

        union midgard_blend replace = {
                .equation = {
                        .rgb_mode = 0x122,
                        .alpha_mode = 0x122,
                        .color_mask = MALI_MASK_R | MALI_MASK_G | MALI_MASK_B | MALI_MASK_A,
                }
        };

        if (blend_shader)
                replace.shader = blend_shader;

        /* Determine the sampler type needed. Stencil is always sampled as
         * UINT. Pure (U)INT is always (U)INT. Everything else is FLOAT. */

        enum pan_blit_type T =
                (loc == FRAG_RESULT_STENCIL) ? PAN_BLIT_UINT :
                (util_format_is_pure_uint(image->format)) ? PAN_BLIT_UINT :
                (util_format_is_pure_sint(image->format)) ? PAN_BLIT_INT :
                PAN_BLIT_FLOAT;

        bool ms = image->nr_samples > 1;

        struct mali_shader_meta shader_meta = {
                .shader = pool->dev->blit_shaders.loads[loc][T][ms],
                .sampler_count = 1,
                .texture_count = 1,
                .varying_count = 1,
                .midgard1 = {
                        .flags_lo = 0x20,
                        .work_count = 4,
                },
                .coverage_mask = 0xF,
                .unknown2_3 = MALI_DEPTH_FUNC(MALI_FUNC_ALWAYS) | 0x10,
                .unknown2_4 = 0x4e0,
                .stencil_mask_front = ~0,
                .stencil_mask_back = ~0,
                .stencil_front = stencil,
                .stencil_back = stencil,
                .blend = {
                        .shader = blend_shader
                }
        };

        if (ms)
                shader_meta.unknown2_3 |= MALI_HAS_MSAA | MALI_PER_SAMPLE;
        else
                shader_meta.unknown2_4 |= MALI_NO_MSAA;

        assert(shader_meta.shader);

        if (pool->dev->quirks & MIDGARD_SFBD) {
                shader_meta.unknown2_4 |= (0x10 | MALI_NO_DITHER);
                shader_meta.blend = replace;

                if (loc < FRAG_RESULT_DATA0)
                        shader_meta.blend.equation.color_mask = 0x0;
        }

        if (loc == FRAG_RESULT_DEPTH) {
                shader_meta.midgard1.flags_lo |= MALI_WRITES_Z;
                shader_meta.unknown2_3 |= MALI_DEPTH_WRITEMASK;
        } else if (loc == FRAG_RESULT_STENCIL) {
                shader_meta.midgard1.flags_hi |= MALI_WRITES_S;
                shader_meta.unknown2_4 |= MALI_STENCIL_TEST;
        } else {
                shader_meta.midgard1.flags_lo |= MALI_EARLY_Z;
        }

        /* Create the texture descriptor. We partially compute the base address
         * ourselves to account for layer, such that the texture descriptor
         * itself is for a 2D texture with array size 1 even for 3D/array
         * textures, removing the need to separately key the blit shaders for
         * 2D and 3D variants */

        struct panfrost_transfer texture_t = panfrost_pool_alloc(pool, sizeof(struct mali_texture_descriptor) + sizeof(mali_ptr) * 2 * MAX2(image->nr_samples, 1));

        panfrost_new_texture(texture_t.cpu,
                        image->width0, image->height0,
                        MAX2(image->nr_samples, 1), 1,
                        image->format, MALI_TEX_2D,
                        image->layout,
                        image->first_level, image->last_level,
                        0, 0,
                        image->nr_samples,
                        0,
                        (MALI_CHANNEL_RED << 0) | (MALI_CHANNEL_GREEN << 3) | (MALI_CHANNEL_BLUE << 6) | (MALI_CHANNEL_ALPHA << 9),
                        image->bo->gpu + image->first_layer *
                                panfrost_get_layer_stride(image->slices,
                                        image->type == MALI_TEX_3D,
                                        image->cubemap_stride, image->first_level),
                        image->slices);

        struct mali_sampler_descriptor sampler = {
                .filter_mode = MALI_SAMP_MAG_NEAREST | MALI_SAMP_MIN_NEAREST,
                .wrap_s = MALI_WRAP_CLAMP_TO_EDGE,
                .wrap_t = MALI_WRAP_CLAMP_TO_EDGE,
                .wrap_r = MALI_WRAP_CLAMP_TO_EDGE,
        };

        struct panfrost_transfer shader_meta_t = panfrost_pool_alloc(pool, sizeof(shader_meta) + 8 * sizeof(struct midgard_blend_rt));
        memcpy(shader_meta_t.cpu, &shader_meta, sizeof(shader_meta));

        for (unsigned i = 0; i < 8; ++i) {
                void *dest = shader_meta_t.cpu + sizeof(shader_meta) + sizeof(struct midgard_blend_rt) * i;

                if (loc == (FRAG_RESULT_DATA0 + i)) {
                        struct midgard_blend_rt blend_rt = {
                                .flags = 0x200 | MALI_BLEND_NO_DITHER,
                                .blend = replace,
                        };

                        if (util_format_is_srgb(image->format))
                                blend_rt.flags |= MALI_BLEND_SRGB;

                        if (blend_shader) {
                                blend_rt.flags |= MALI_BLEND_MRT_SHADER;
                                blend_rt.blend.shader = blend_shader;
                        }

                        memcpy(dest, &blend_rt, sizeof(struct midgard_blend_rt));
                } else {
                        memset(dest, 0x0, sizeof(struct midgard_blend_rt));
                }
        }

        struct midgard_payload_vertex_tiler payload = {
                .prefix = {
                        .draw_mode = MALI_TRIANGLES,
                        .unknown_draw = 0x3000,
                        .index_count = MALI_POSITIVE(vertex_count)
                },
                .postfix = {
                        .gl_enables = 0x7,
                        .position_varying = coordinates,
                        .textures = panfrost_pool_upload(pool, &texture_t.gpu, sizeof(texture_t.gpu)),
                        .sampler_descriptor = panfrost_pool_upload(pool, &sampler, sizeof(sampler)),
                        .shader = shader_meta_t.gpu,
                        .varyings = panfrost_pool_upload(pool, &varying, sizeof(varying)),
                        .varying_meta = panfrost_pool_upload(pool, &varying_meta, sizeof(varying_meta)),
                        .viewport = panfrost_pool_upload(pool, &viewport, sizeof(viewport)),
                        .shared_memory = fbd
                }
        };

        panfrost_pack_work_groups_compute(&payload.prefix, 1, vertex_count, 1, 1, 1, 1, true);
        payload.prefix.workgroups_x_shift_3 = 6;

        panfrost_new_job(pool, scoreboard, JOB_TYPE_TILER, false, 0, &payload, sizeof(payload), true);
}
