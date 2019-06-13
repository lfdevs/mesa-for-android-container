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

#include "pan_wallpaper.h"
#include "pan_context.h"
#include "pan_screen.h"
#include "pan_util.h"
//#include "include/panfrost-job.h"
#include "midgard/midgard_compile.h"
#include "compiler/nir/nir_builder.h"

/* Creates the special-purpose fragment shader for wallpapering. A
 * pseudo-vertex shader sets us up for a fullscreen quad render, with a texture
 * coordinate varying */

static nir_shader *
panfrost_build_wallpaper_program()
{
        nir_shader *shader = nir_shader_create(NULL, MESA_SHADER_FRAGMENT, &midgard_nir_options, NULL);
        nir_function *fn = nir_function_create(shader, "main");
        nir_function_impl *impl = nir_function_impl_create(fn);

        /* Create the variables variables */

        nir_variable *c_texcoord = nir_variable_create(shader, nir_var_shader_in, glsl_vector_type(GLSL_TYPE_FLOAT, 4), "gl_TexCoord");
        nir_variable *c_out = nir_variable_create(shader, nir_var_shader_out, glsl_vector_type(GLSL_TYPE_FLOAT, 4), "gl_FragColor");

        c_texcoord->data.location = VARYING_SLOT_VAR0;
        c_out->data.location = FRAG_RESULT_COLOR;

        /* Setup nir_builder */

        nir_builder _b;
        nir_builder *b = &_b;
        nir_builder_init(b, impl);
        b->cursor = nir_before_block(nir_start_block(impl));

        /* Setup inputs */

        nir_ssa_def *s_src = nir_load_var(b, c_texcoord);

        /* Build the passthrough texture shader */

        nir_tex_instr *tx = nir_tex_instr_create(shader, 1);
        tx->op = nir_texop_tex;
        tx->texture_index = tx->sampler_index = 0;
        tx->sampler_dim = GLSL_SAMPLER_DIM_2D;
        tx->dest_type = nir_type_float;

        nir_src src = nir_src_for_ssa(s_src);
        nir_src_copy(&tx->src[0].src, &src, tx);
        tx->src[0].src_type = nir_tex_src_coord;

        nir_ssa_dest_init(&tx->instr, &tx->dest, nir_tex_instr_dest_size(tx), 32, NULL);
        nir_builder_instr_insert(b, &tx->instr);

        nir_ssa_def *texel = &tx->dest.ssa;

        nir_store_var(b, c_out, texel, 0xFF);

        return shader;
}

/* Creates the CSO corresponding to the wallpaper program */

static struct panfrost_shader_variants *
panfrost_create_wallpaper_program(struct pipe_context *pctx) 
{
        nir_shader *built_nir_shader = panfrost_build_wallpaper_program();

        struct pipe_shader_state so = {
                .type = PIPE_SHADER_IR_NIR,
                .ir = {
                        .nir = built_nir_shader
                }
        };

        return pctx->create_fs_state(pctx, &so);
}

static struct panfrost_shader_variants *wallpaper_program = NULL;
static struct panfrost_shader_variants *wallpaper_saved_program = NULL;

static void
panfrost_enable_wallpaper_program(struct pipe_context *pctx)
{
        struct panfrost_context *ctx = pan_context(pctx);

        if (!wallpaper_program) {
                wallpaper_program = panfrost_create_wallpaper_program(pctx);
        }

        /* Push the shader state */
        wallpaper_saved_program = ctx->fs;

        /* Bind the program */
        pctx->bind_fs_state(pctx, wallpaper_program);
}

static void
panfrost_disable_wallpaper_program(struct pipe_context *pctx)
{
        /* Pop off the shader state */
        pctx->bind_fs_state(pctx, wallpaper_saved_program);
}

/* Essentially, we insert a fullscreen textured quad, reading from the
 * previous frame's framebuffer */

void
panfrost_draw_wallpaper(struct pipe_context *pipe)
{
        /* Disable wallpapering for now, but still exercise the shader generation to minimise bit rot */

        panfrost_enable_wallpaper_program(pipe);
        panfrost_disable_wallpaper_program(pipe);

        return;

#if 0
        struct panfrost_context *ctx = pan_context(pipe);

        /* Setup payload for elided quad. TODO: Refactor draw_vbo so this can
         * be a little more DRY */

        ctx->payload_tiler.draw_start = 0;
        ctx->payload_tiler.prefix.draw_mode = MALI_TRIANGLE_STRIP;
        ctx->vertex_count = 4;
        ctx->payload_tiler.prefix.invocation_count = MALI_POSITIVE(4);
        ctx->payload_tiler.prefix.unknown_draw &= ~(0x3000 | 0x18000);
        ctx->payload_tiler.prefix.unknown_draw |= 0x18000;
        ctx->payload_tiler.prefix.negative_start = 0;
        ctx->payload_tiler.prefix.index_count = MALI_POSITIVE(4);
        ctx->payload_tiler.prefix.unknown_draw &= ~MALI_DRAW_INDEXED_UINT32;
        ctx->payload_tiler.prefix.indices = (uintptr_t) NULL;

        /* Setup the wallpapering program. We need to build the program via
         * NIR. */

        panfrost_enable_wallpaper_program(pipe);

        /* Setup the texture/sampler pair */

        struct pipe_sampler_view tmpl = {
                .target = PIPE_TEXTURE_2D,
                .swizzle_r = PIPE_SWIZZLE_X,
                .swizzle_g = PIPE_SWIZZLE_Y,
                .swizzle_b = PIPE_SWIZZLE_Z,
                .swizzle_a = PIPE_SWIZZLE_W
        };

        struct pipe_sampler_state state = {
                .min_mip_filter = PIPE_TEX_MIPFILTER_NONE,
                .min_img_filter = PIPE_TEX_MIPFILTER_LINEAR,
                .mag_img_filter = PIPE_TEX_MIPFILTER_LINEAR,
                .wrap_s = PIPE_TEX_WRAP_CLAMP_TO_EDGE,
                .wrap_t = PIPE_TEX_WRAP_CLAMP_TO_EDGE,
                .wrap_r = PIPE_TEX_WRAP_CLAMP_TO_EDGE,
                .normalized_coords = 1
        };

        struct pipe_resource *rsrc = pan_screen(pipe->screen)->display_target;
        struct pipe_sampler_state *sampler_state = pipe->create_sampler_state(pipe, &state);
        struct pipe_sampler_view *sampler_view = pipe->create_sampler_view(pipe, rsrc, &tmpl);

        /* Bind texture/sampler. TODO: push/pop */
        pipe->bind_sampler_states(pipe, PIPE_SHADER_FRAGMENT, 0, 1, &sampler_state);
        pipe->set_sampler_views(pipe, PIPE_SHADER_FRAGMENT, 0, 1, &sampler_view);

        panfrost_emit_for_draw(ctx, false);

        /* Elision occurs by essential precomputing the results of the
         * implied vertex shader. Insert these results for fullscreen. The
         * first two channels are ~screenspace coordinates, whereas the latter
         * two are fixed 0.0/1.0 after perspective division. See the vertex
         * shader epilogue for more context */

        float implied_position_varying[] = {
                /* The following is correct for scissored clears whose scissor deals with cutoff appropriately */

//                -1.0, -1.0,        0.0, 1.0,
//                -1.0, 65535.0,     0.0, 1.0,
//                65536.0, 1.0,      0.0, 1.0,
//                65536.0, 65536.0,  0.0, 1.0

                /* The following output is correct for a fullscreen quad with screen size 2048x1600 */
                0.0, 0.0, 0.0, 1.0,
                0.0, 1600.0, 0.0, 1.0,
                2048.0, 0.0, 0.0, 1.0,
                2048.0, 1280.0, 0.0, 1.0,
        };

        ctx->payload_tiler.postfix.position_varying = panfrost_upload_transient(ctx, implied_position_varying, sizeof(implied_position_varying));

        /* Similarly, setup the texture coordinate varying, hardcoded to match
         * the corners of the screen */

        float texture_coordinates[] = {
                0.0, 0.0, 0.0, 0.0,
                0.0, 1.0, 0.0, 0.0,
                1.0, 0.0, 0.0, 0.0,
                1.0, 1.0, 0.0, 0.0
        };

        union mali_attr varyings[1] = {
                {
                        .elements = panfrost_upload_transient(ctx, texture_coordinates, sizeof(texture_coordinates)) | 1,
                        .stride = sizeof(float) * 4,
                        .size = sizeof(texture_coordinates)
                }
        };

        ctx->payload_tiler.postfix.varyings = panfrost_upload_transient(ctx, varyings, sizeof(varyings));

        struct mali_attr_meta varying_meta[1] = {
                {
                        .type = MALI_ATYPE_FLOAT,
                        .nr_components = MALI_POSITIVE(4),
                        .not_normalised = 1,
                        .unknown1 = /*0x2c22 - nr_comp=2*/ 0x2a22,
                        .unknown2 = 0x1
                }
        };

        mali_ptr saved_varying_meta = ctx->payload_tiler.postfix.varying_meta;
        ctx->payload_tiler.postfix.varying_meta = panfrost_upload_transient(ctx, varying_meta, sizeof(varying_meta));

        /* Emit the tiler job */
        struct panfrost_transfer tiler = panfrost_vertex_tiler_job(ctx, true, true);
        struct mali_job_descriptor_header *jd = (struct mali_job_descriptor_header *) tiler.cpu;
        ctx->u_tiler_jobs[ctx->tiler_job_count] = jd;
        ctx->tiler_jobs[ctx->tiler_job_count++] = tiler.gpu;
        ctx->draw_count++;

        /* Okay, so we have the tiler job emitted. Since we set elided_tiler
         * mode, no dependencies will be set automatically. We don't actually
         * want any dependencies, since we go first and we don't need a vertex
         * first. That said, we do need the first tiler job to depend on us.
         * Its second dep slot will be free (see the panfrost_vertex_tiler_job
         * dependency setting algorithm), so fill us in with that
         */

        if (ctx->tiler_job_count > 1) {
                ctx->u_tiler_jobs[0]->job_dependency_index_2 = jd->job_index;
        }

        printf("Wallpaper boop\n");
        
        /* Cleanup */
        panfrost_disable_wallpaper_program(pipe);
        ctx->payload_tiler.postfix.varying_meta = saved_varying_meta;
#endif
}
