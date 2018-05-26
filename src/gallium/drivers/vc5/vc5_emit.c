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

#include "util/u_format.h"
#include "util/u_half.h"
#include "vc5_context.h"
#include "broadcom/common/v3d_macros.h"
#include "broadcom/cle/v3dx_pack.h"
#include "broadcom/compiler/v3d_compiler.h"

static uint8_t
vc5_factor(enum pipe_blendfactor factor, bool dst_alpha_one)
{
        /* We may get a bad blendfactor when blending is disabled. */
        if (factor == 0)
                return V3D_BLEND_FACTOR_ZERO;

        switch (factor) {
        case PIPE_BLENDFACTOR_ZERO:
                return V3D_BLEND_FACTOR_ZERO;
        case PIPE_BLENDFACTOR_ONE:
                return V3D_BLEND_FACTOR_ONE;
        case PIPE_BLENDFACTOR_SRC_COLOR:
                return V3D_BLEND_FACTOR_SRC_COLOR;
        case PIPE_BLENDFACTOR_INV_SRC_COLOR:
                return V3D_BLEND_FACTOR_INV_SRC_COLOR;
        case PIPE_BLENDFACTOR_DST_COLOR:
                return V3D_BLEND_FACTOR_DST_COLOR;
        case PIPE_BLENDFACTOR_INV_DST_COLOR:
                return V3D_BLEND_FACTOR_INV_DST_COLOR;
        case PIPE_BLENDFACTOR_SRC_ALPHA:
                return V3D_BLEND_FACTOR_SRC_ALPHA;
        case PIPE_BLENDFACTOR_INV_SRC_ALPHA:
                return V3D_BLEND_FACTOR_INV_SRC_ALPHA;
        case PIPE_BLENDFACTOR_DST_ALPHA:
                return (dst_alpha_one ?
                        V3D_BLEND_FACTOR_ONE :
                        V3D_BLEND_FACTOR_DST_ALPHA);
        case PIPE_BLENDFACTOR_INV_DST_ALPHA:
                return (dst_alpha_one ?
                        V3D_BLEND_FACTOR_ZERO :
                        V3D_BLEND_FACTOR_INV_DST_ALPHA);
        case PIPE_BLENDFACTOR_CONST_COLOR:
                return V3D_BLEND_FACTOR_CONST_COLOR;
        case PIPE_BLENDFACTOR_INV_CONST_COLOR:
                return V3D_BLEND_FACTOR_INV_CONST_COLOR;
        case PIPE_BLENDFACTOR_CONST_ALPHA:
                return V3D_BLEND_FACTOR_CONST_ALPHA;
        case PIPE_BLENDFACTOR_INV_CONST_ALPHA:
                return V3D_BLEND_FACTOR_INV_CONST_ALPHA;
        case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE:
                return V3D_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        default:
                unreachable("Bad blend factor");
        }
}

static inline uint16_t
swizzled_border_color(const struct v3d_device_info *devinfo,
                      struct pipe_sampler_state *sampler,
                      struct vc5_sampler_view *sview,
                      int chan)
{
        const struct util_format_description *desc =
                util_format_description(sview->base.format);
        uint8_t swiz = chan;

        /* If we're doing swizzling in the sampler, then only rearrange the
         * border color for the mismatch between the VC5 texture format and
         * the PIPE_FORMAT, since GL_ARB_texture_swizzle will be handled by
         * the sampler's swizzle.
         *
         * For swizzling in the shader, we don't do any pre-swizzling of the
         * border color.
         */
        if (vc5_get_tex_return_size(devinfo, sview->base.format,
                                    sampler->compare_mode) != 32)
                swiz = desc->swizzle[swiz];

        switch (swiz) {
        case PIPE_SWIZZLE_0:
                return util_float_to_half(0.0);
        case PIPE_SWIZZLE_1:
                return util_float_to_half(1.0);
        default:
                return util_float_to_half(sampler->border_color.f[swiz]);
        }
}

#if V3D_VERSION < 40
static uint32_t
translate_swizzle(unsigned char pipe_swizzle)
{
        switch (pipe_swizzle) {
        case PIPE_SWIZZLE_0:
                return 0;
        case PIPE_SWIZZLE_1:
                return 1;
        case PIPE_SWIZZLE_X:
        case PIPE_SWIZZLE_Y:
        case PIPE_SWIZZLE_Z:
        case PIPE_SWIZZLE_W:
                return 2 + pipe_swizzle;
        default:
                unreachable("unknown swizzle");
        }
}

static void
emit_one_texture(struct vc5_context *vc5, struct vc5_texture_stateobj *stage_tex,
                 int i)
{
        struct vc5_job *job = vc5->job;
        struct pipe_sampler_state *psampler = stage_tex->samplers[i];
        struct vc5_sampler_state *sampler = vc5_sampler_state(psampler);
        struct pipe_sampler_view *psview = stage_tex->textures[i];
        struct vc5_sampler_view *sview = vc5_sampler_view(psview);
        struct pipe_resource *prsc = psview->texture;
        struct vc5_resource *rsc = vc5_resource(prsc);
        const struct v3d_device_info *devinfo = &vc5->screen->devinfo;

        stage_tex->texture_state[i].offset =
                vc5_cl_ensure_space(&job->indirect,
                                    cl_packet_length(TEXTURE_SHADER_STATE),
                                    32);
        vc5_bo_set_reference(&stage_tex->texture_state[i].bo,
                             job->indirect.bo);

        uint32_t return_size = vc5_get_tex_return_size(devinfo, psview->format,
                                                       psampler->compare_mode);

        struct V3D33_TEXTURE_SHADER_STATE unpacked = {
                /* XXX */
                .border_color_red = swizzled_border_color(devinfo, psampler,
                                                          sview, 0),
                .border_color_green = swizzled_border_color(devinfo, psampler,
                                                            sview, 1),
                .border_color_blue = swizzled_border_color(devinfo, psampler,
                                                           sview, 2),
                .border_color_alpha = swizzled_border_color(devinfo, psampler,
                                                            sview, 3),

                /* In the normal texturing path, the LOD gets clamped between
                 * min/max, and the base_level field (set in the sampler view
                 * from first_level) only decides where the min/mag switch
                 * happens, so we need to use the LOD clamps to keep us
                 * between min and max.
                 *
                 * For txf, the LOD clamp is still used, despite GL not
                 * wanting that.  We will need to have a separate
                 * TEXTURE_SHADER_STATE that ignores psview->min/max_lod to
                 * support txf properly.
                 */
                .min_level_of_detail = MIN2(psview->u.tex.first_level +
                                            MAX2(psampler->min_lod, 0),
                                            psview->u.tex.last_level),
                .max_level_of_detail = MIN2(psview->u.tex.first_level +
                                            psampler->max_lod,
                                            psview->u.tex.last_level),

                .texture_base_pointer = cl_address(rsc->bo,
                                                   rsc->slices[0].offset),

                .output_32_bit = return_size == 32,
        };

        /* Set up the sampler swizzle if we're doing 16-bit sampling.  For
         * 32-bit, we leave swizzling up to the shader compiler.
         *
         * Note: Contrary to the docs, the swizzle still applies even if the
         * return size is 32.  It's just that you probably want to swizzle in
         * the shader, because you need the Y/Z/W channels to be defined.
         */
        if (return_size == 32) {
                unpacked.swizzle_r = translate_swizzle(PIPE_SWIZZLE_X);
                unpacked.swizzle_g = translate_swizzle(PIPE_SWIZZLE_Y);
                unpacked.swizzle_b = translate_swizzle(PIPE_SWIZZLE_Z);
                unpacked.swizzle_a = translate_swizzle(PIPE_SWIZZLE_W);
        } else {
                unpacked.swizzle_r = translate_swizzle(sview->swizzle[0]);
                unpacked.swizzle_g = translate_swizzle(sview->swizzle[1]);
                unpacked.swizzle_b = translate_swizzle(sview->swizzle[2]);
                unpacked.swizzle_a = translate_swizzle(sview->swizzle[3]);
        }

        int min_img_filter = psampler->min_img_filter;
        int min_mip_filter = psampler->min_mip_filter;
        int mag_img_filter = psampler->mag_img_filter;

        if (return_size == 32) {
                min_mip_filter = PIPE_TEX_MIPFILTER_NEAREST;
                mag_img_filter = PIPE_TEX_FILTER_NEAREST;
                mag_img_filter = PIPE_TEX_FILTER_NEAREST;
        }

        bool min_nearest = min_img_filter == PIPE_TEX_FILTER_NEAREST;
        switch (min_mip_filter) {
        case PIPE_TEX_MIPFILTER_NONE:
                unpacked.filter += min_nearest ? 2 : 0;
                break;
        case PIPE_TEX_MIPFILTER_NEAREST:
                unpacked.filter += min_nearest ? 4 : 8;
                break;
        case PIPE_TEX_MIPFILTER_LINEAR:
                unpacked.filter += min_nearest ? 4 : 8;
                unpacked.filter += 2;
                break;
        }

        if (mag_img_filter == PIPE_TEX_FILTER_NEAREST)
                unpacked.filter++;

        if (psampler->max_anisotropy > 8)
                unpacked.filter = V3D_TMU_FILTER_ANISOTROPIC_16_1;
        else if (psampler->max_anisotropy > 4)
                unpacked.filter = V3D_TMU_FILTER_ANISOTROPIC_8_1;
        else if (psampler->max_anisotropy > 2)
                unpacked.filter = V3D_TMU_FILTER_ANISOTROPIC_4_1;
        else if (psampler->max_anisotropy)
                unpacked.filter = V3D_TMU_FILTER_ANISOTROPIC_2_1;

        uint8_t packed[cl_packet_length(TEXTURE_SHADER_STATE)];
        cl_packet_pack(TEXTURE_SHADER_STATE)(&job->indirect, packed, &unpacked);

        for (int i = 0; i < ARRAY_SIZE(packed); i++)
                packed[i] |= sview->texture_shader_state[i] | sampler->texture_shader_state[i];

        /* TMU indirect structs need to be 32b aligned. */
        vc5_cl_ensure_space(&job->indirect, ARRAY_SIZE(packed), 32);
        cl_emit_prepacked(&job->indirect, &packed);
}

static void
emit_textures(struct vc5_context *vc5, struct vc5_texture_stateobj *stage_tex)
{
        for (int i = 0; i < stage_tex->num_textures; i++) {
                if (stage_tex->textures[i])
                        emit_one_texture(vc5, stage_tex, i);
        }
}
#endif /* V3D_VERSION < 40 */

static uint32_t
translate_colormask(struct vc5_context *vc5, uint32_t colormask, int rt)
{
        if (vc5->swap_color_rb & (1 << rt)) {
                colormask = ((colormask & (2 | 8)) |
                             ((colormask & 1) << 2) |
                             ((colormask & 4) >> 2));
        }

        return (~colormask) & 0xf;
}

static void
emit_rt_blend(struct vc5_context *vc5, struct vc5_job *job,
              struct pipe_blend_state *blend, int rt)
{
        cl_emit(&job->bcl, BLEND_CONFIG, config) {
                struct pipe_rt_blend_state *rtblend = &blend->rt[rt];

#if V3D_VERSION >= 40
                config.render_target_mask = 1 << rt;
#else
                assert(rt == 0);
#endif

                config.colour_blend_mode = rtblend->rgb_func;
                config.colour_blend_dst_factor =
                        vc5_factor(rtblend->rgb_dst_factor,
                                   vc5->blend_dst_alpha_one);
                config.colour_blend_src_factor =
                        vc5_factor(rtblend->rgb_src_factor,
                                   vc5->blend_dst_alpha_one);

                config.alpha_blend_mode = rtblend->alpha_func;
                config.alpha_blend_dst_factor =
                        vc5_factor(rtblend->alpha_dst_factor,
                                   vc5->blend_dst_alpha_one);
                config.alpha_blend_src_factor =
                        vc5_factor(rtblend->alpha_src_factor,
                                   vc5->blend_dst_alpha_one);
        }
}

void
v3dX(emit_state)(struct pipe_context *pctx)
{
        struct vc5_context *vc5 = vc5_context(pctx);
        struct vc5_job *job = vc5->job;
        bool rasterizer_discard = vc5->rasterizer->base.rasterizer_discard;

        if (vc5->dirty & (VC5_DIRTY_SCISSOR | VC5_DIRTY_VIEWPORT |
                          VC5_DIRTY_RASTERIZER)) {
                float *vpscale = vc5->viewport.scale;
                float *vptranslate = vc5->viewport.translate;
                float vp_minx = -fabsf(vpscale[0]) + vptranslate[0];
                float vp_maxx = fabsf(vpscale[0]) + vptranslate[0];
                float vp_miny = -fabsf(vpscale[1]) + vptranslate[1];
                float vp_maxy = fabsf(vpscale[1]) + vptranslate[1];

                /* Clip to the scissor if it's enabled, but still clip to the
                 * drawable regardless since that controls where the binner
                 * tries to put things.
                 *
                 * Additionally, always clip the rendering to the viewport,
                 * since the hardware does guardband clipping, meaning
                 * primitives would rasterize outside of the view volume.
                 */
                uint32_t minx, miny, maxx, maxy;
                if (!vc5->rasterizer->base.scissor) {
                        minx = MAX2(vp_minx, 0);
                        miny = MAX2(vp_miny, 0);
                        maxx = MIN2(vp_maxx, job->draw_width);
                        maxy = MIN2(vp_maxy, job->draw_height);
                } else {
                        minx = MAX2(vp_minx, vc5->scissor.minx);
                        miny = MAX2(vp_miny, vc5->scissor.miny);
                        maxx = MIN2(vp_maxx, vc5->scissor.maxx);
                        maxy = MIN2(vp_maxy, vc5->scissor.maxy);
                }

                cl_emit(&job->bcl, CLIP_WINDOW, clip) {
                        clip.clip_window_left_pixel_coordinate = minx;
                        clip.clip_window_bottom_pixel_coordinate = miny;
                        clip.clip_window_width_in_pixels = maxx - minx;
                        clip.clip_window_height_in_pixels = maxy - miny;

#if V3D_VERSION < 41
                        /* The HW won't entirely clip out when scissor w/h is
                         * 0.  Just treat it the same as rasterizer discard.
                         */
                        if (clip.clip_window_width_in_pixels == 0 ||
                            clip.clip_window_height_in_pixels == 0) {
                                rasterizer_discard = true;
                                clip.clip_window_width_in_pixels = 1;
                                clip.clip_window_height_in_pixels = 1;
                        }
#endif
                }

                job->draw_min_x = MIN2(job->draw_min_x, minx);
                job->draw_min_y = MIN2(job->draw_min_y, miny);
                job->draw_max_x = MAX2(job->draw_max_x, maxx);
                job->draw_max_y = MAX2(job->draw_max_y, maxy);
        }

        if (vc5->dirty & (VC5_DIRTY_RASTERIZER |
                          VC5_DIRTY_ZSA |
                          VC5_DIRTY_BLEND |
                          VC5_DIRTY_COMPILED_FS)) {
                cl_emit(&job->bcl, CONFIGURATION_BITS, config) {
                        config.enable_forward_facing_primitive =
                                !rasterizer_discard &&
                                !(vc5->rasterizer->base.cull_face &
                                  PIPE_FACE_FRONT);
                        config.enable_reverse_facing_primitive =
                                !rasterizer_discard &&
                                !(vc5->rasterizer->base.cull_face &
                                  PIPE_FACE_BACK);
                        /* This seems backwards, but it's what gets the
                         * clipflat test to pass.
                         */
                        config.clockwise_primitives =
                                vc5->rasterizer->base.front_ccw;

                        config.enable_depth_offset =
                                vc5->rasterizer->base.offset_tri;

                        config.rasterizer_oversample_mode =
                                vc5->rasterizer->base.multisample;

                        config.direct3d_provoking_vertex =
                                vc5->rasterizer->base.flatshade_first;

                        config.blend_enable = vc5->blend->rt[0].blend_enable;

                        /* Note: EZ state may update based on the compiled FS,
                         * along with ZSA
                         */
                        config.early_z_updates_enable =
                                (job->ez_state != VC5_EZ_DISABLED);
                        if (vc5->zsa->base.depth.enabled) {
                                config.z_updates_enable =
                                        vc5->zsa->base.depth.writemask;
                                config.early_z_enable =
                                        config.early_z_updates_enable;
                                config.depth_test_function =
                                        vc5->zsa->base.depth.func;
                        } else {
                                config.depth_test_function = PIPE_FUNC_ALWAYS;
                        }

                        config.stencil_enable =
                                vc5->zsa->base.stencil[0].enabled;
                }

        }

        if (vc5->dirty & VC5_DIRTY_RASTERIZER &&
            vc5->rasterizer->base.offset_tri) {
                cl_emit(&job->bcl, DEPTH_OFFSET, depth) {
                        depth.depth_offset_factor =
                                vc5->rasterizer->offset_factor;
                        depth.depth_offset_units =
                                vc5->rasterizer->offset_units;
                }
        }

        if (vc5->dirty & VC5_DIRTY_RASTERIZER) {
                cl_emit(&job->bcl, POINT_SIZE, point_size) {
                        point_size.point_size = vc5->rasterizer->point_size;
                }

                cl_emit(&job->bcl, LINE_WIDTH, line_width) {
                        line_width.line_width = vc5->rasterizer->base.line_width;
                }
        }

        if (vc5->dirty & VC5_DIRTY_VIEWPORT) {
                cl_emit(&job->bcl, CLIPPER_XY_SCALING, clip) {
                        clip.viewport_half_width_in_1_256th_of_pixel =
                                vc5->viewport.scale[0] * 256.0f;
                        clip.viewport_half_height_in_1_256th_of_pixel =
                                vc5->viewport.scale[1] * 256.0f;
                }

                cl_emit(&job->bcl, CLIPPER_Z_SCALE_AND_OFFSET, clip) {
                        clip.viewport_z_offset_zc_to_zs =
                                vc5->viewport.translate[2];
                        clip.viewport_z_scale_zc_to_zs =
                                vc5->viewport.scale[2];
                }
                cl_emit(&job->bcl, CLIPPER_Z_MIN_MAX_CLIPPING_PLANES, clip) {
                        clip.minimum_zw = (vc5->viewport.translate[2] -
                                           vc5->viewport.scale[2]);
                        clip.maximum_zw = (vc5->viewport.translate[2] +
                                           vc5->viewport.scale[2]);
                }

                cl_emit(&job->bcl, VIEWPORT_OFFSET, vp) {
                        vp.viewport_centre_x_coordinate =
                                vc5->viewport.translate[0];
                        vp.viewport_centre_y_coordinate =
                                vc5->viewport.translate[1];
                }
        }

        if (vc5->dirty & VC5_DIRTY_BLEND && vc5->blend->rt[0].blend_enable) {
                struct pipe_blend_state *blend = vc5->blend;

                if (blend->independent_blend_enable) {
                        for (int i = 0; i < VC5_MAX_DRAW_BUFFERS; i++)
                                emit_rt_blend(vc5, job, blend, i);
                } else {
                        emit_rt_blend(vc5, job, blend, 0);
                }
        }

        if (vc5->dirty & VC5_DIRTY_BLEND) {
                struct pipe_blend_state *blend = vc5->blend;

                cl_emit(&job->bcl, COLOUR_WRITE_MASKS, mask) {
                        if (blend->independent_blend_enable) {
                                mask.render_target_0_per_colour_component_write_masks =
                                        translate_colormask(vc5, blend->rt[0].colormask, 0);
                                mask.render_target_1_per_colour_component_write_masks =
                                        translate_colormask(vc5, blend->rt[1].colormask, 1);
                                mask.render_target_2_per_colour_component_write_masks =
                                        translate_colormask(vc5, blend->rt[2].colormask, 2);
                                mask.render_target_3_per_colour_component_write_masks =
                                        translate_colormask(vc5, blend->rt[3].colormask, 3);
                        } else {
                                mask.render_target_0_per_colour_component_write_masks =
                                        translate_colormask(vc5, blend->rt[0].colormask, 0);
                                mask.render_target_1_per_colour_component_write_masks =
                                        translate_colormask(vc5, blend->rt[0].colormask, 1);
                                mask.render_target_2_per_colour_component_write_masks =
                                        translate_colormask(vc5, blend->rt[0].colormask, 2);
                                mask.render_target_3_per_colour_component_write_masks =
                                        translate_colormask(vc5, blend->rt[0].colormask, 3);
                        }
                }
        }

        /* GFXH-1431: On V3D 3.x, writing BLEND_CONFIG resets the constant
         * color.
         */
        if (vc5->dirty & VC5_DIRTY_BLEND_COLOR ||
            (V3D_VERSION < 41 && (vc5->dirty & VC5_DIRTY_BLEND))) {
                cl_emit(&job->bcl, BLEND_CONSTANT_COLOUR, colour) {
                        colour.red_f16 = (vc5->swap_color_rb ?
                                          vc5->blend_color.hf[2] :
                                          vc5->blend_color.hf[0]);
                        colour.green_f16 = vc5->blend_color.hf[1];
                        colour.blue_f16 = (vc5->swap_color_rb ?
                                           vc5->blend_color.hf[0] :
                                           vc5->blend_color.hf[2]);
                        colour.alpha_f16 = vc5->blend_color.hf[3];
                }
        }

        if (vc5->dirty & (VC5_DIRTY_ZSA | VC5_DIRTY_STENCIL_REF)) {
                struct pipe_stencil_state *front = &vc5->zsa->base.stencil[0];
                struct pipe_stencil_state *back = &vc5->zsa->base.stencil[1];

                if (front->enabled) {
                        cl_emit_with_prepacked(&job->bcl, STENCIL_CONFIG,
                                               vc5->zsa->stencil_front, config) {
                                config.stencil_ref_value =
                                        vc5->stencil_ref.ref_value[0];
                        }
                }

                if (back->enabled) {
                        cl_emit_with_prepacked(&job->bcl, STENCIL_CONFIG,
                                               vc5->zsa->stencil_back, config) {
                                config.stencil_ref_value =
                                        vc5->stencil_ref.ref_value[1];
                        }
                }
        }

#if V3D_VERSION < 40
        /* Pre-4.x, we have texture state that depends on both the sampler and
         * the view, so we merge them together at draw time.
         */
        if (vc5->dirty & VC5_DIRTY_FRAGTEX)
                emit_textures(vc5, &vc5->fragtex);

        if (vc5->dirty & VC5_DIRTY_VERTTEX)
                emit_textures(vc5, &vc5->verttex);
#endif

        if (vc5->dirty & VC5_DIRTY_FLAT_SHADE_FLAGS) {
                bool emitted_any = false;

                for (int i = 0; i < ARRAY_SIZE(vc5->prog.fs->prog_data.fs->flat_shade_flags); i++) {
                        if (!vc5->prog.fs->prog_data.fs->flat_shade_flags[i])
                                continue;

                        cl_emit(&job->bcl, FLAT_SHADE_FLAGS, flags) {
                                flags.varying_offset_v0 = i;

                                if (emitted_any) {
                                        flags.action_for_flat_shade_flags_of_lower_numbered_varyings =
                                                V3D_VARYING_FLAGS_ACTION_UNCHANGED;
                                        flags.action_for_flat_shade_flags_of_higher_numbered_varyings =
                                                V3D_VARYING_FLAGS_ACTION_UNCHANGED;
                                } else {
                                        flags.action_for_flat_shade_flags_of_lower_numbered_varyings =
                                                ((i == 0) ?
                                                 V3D_VARYING_FLAGS_ACTION_UNCHANGED :
                                                 V3D_VARYING_FLAGS_ACTION_ZEROED);

                                        flags.action_for_flat_shade_flags_of_higher_numbered_varyings =
                                                V3D_VARYING_FLAGS_ACTION_ZEROED;
                                }

                                flags.flat_shade_flags_for_varyings_v024 =
                                        vc5->prog.fs->prog_data.fs->flat_shade_flags[i];
                        }

                        emitted_any = true;
                }

                if (!emitted_any) {
                        cl_emit(&job->bcl, ZERO_ALL_FLAT_SHADE_FLAGS, flags);
                }
        }

        /* Set up the transform feedback data specs (which VPM entries to
         * output to which buffers).
         */
        if (vc5->dirty & (VC5_DIRTY_STREAMOUT |
                          VC5_DIRTY_RASTERIZER |
                          VC5_DIRTY_PRIM_MODE)) {
                struct vc5_streamout_stateobj *so = &vc5->streamout;

                if (so->num_targets) {
                        bool psiz_per_vertex = (vc5->prim_mode == PIPE_PRIM_POINTS &&
                                                vc5->rasterizer->base.point_size_per_vertex);
                        uint16_t *tf_specs = (psiz_per_vertex ?
                                              vc5->prog.bind_vs->tf_specs_psiz :
                                              vc5->prog.bind_vs->tf_specs);

#if V3D_VERSION >= 40
                        job->tf_enabled = (vc5->prog.bind_vs->num_tf_specs != 0 &&
                                           vc5->active_queries);

                        cl_emit(&job->bcl, TRANSFORM_FEEDBACK_SPECS, tfe) {
                                tfe.number_of_16_bit_output_data_specs_following =
                                        vc5->prog.bind_vs->num_tf_specs;
                                tfe.enable = job->tf_enabled;
                        };
#else /* V3D_VERSION < 40 */
                        cl_emit(&job->bcl, TRANSFORM_FEEDBACK_ENABLE, tfe) {
                                tfe.number_of_32_bit_output_buffer_address_following =
                                        so->num_targets;
                                tfe.number_of_16_bit_output_data_specs_following =
                                        vc5->prog.bind_vs->num_tf_specs;
                        };
#endif /* V3D_VERSION < 40 */
                        for (int i = 0; i < vc5->prog.bind_vs->num_tf_specs; i++) {
                                cl_emit_prepacked(&job->bcl, &tf_specs[i]);
                        }
                } else if (job->tf_enabled) {
#if V3D_VERSION >= 40
                        cl_emit(&job->bcl, TRANSFORM_FEEDBACK_SPECS, tfe) {
                                tfe.enable = false;
                        };
                        job->tf_enabled = false;
#endif /* V3D_VERSION >= 40 */
                }
        }

        /* Set up the trasnform feedback buffers. */
        if (vc5->dirty & VC5_DIRTY_STREAMOUT) {
                struct vc5_streamout_stateobj *so = &vc5->streamout;
                for (int i = 0; i < so->num_targets; i++) {
                        const struct pipe_stream_output_target *target =
                                so->targets[i];
                        struct vc5_resource *rsc = target ?
                                vc5_resource(target->buffer) : NULL;

#if V3D_VERSION >= 40
                        if (!target)
                                continue;

                        cl_emit(&job->bcl, TRANSFORM_FEEDBACK_BUFFER, output) {
                                output.buffer_address =
                                        cl_address(rsc->bo,
                                                   target->buffer_offset);
                                output.buffer_size_in_32_bit_words =
                                        target->buffer_size >> 2;
                                output.buffer_number = i;
                        }
#else /* V3D_VERSION < 40 */
                        cl_emit(&job->bcl, TRANSFORM_FEEDBACK_OUTPUT_ADDRESS, output) {
                                if (target) {
                                        output.address =
                                                cl_address(rsc->bo,
                                                           target->buffer_offset);
                                }
                        };
#endif /* V3D_VERSION < 40 */
                        if (target) {
                                vc5_job_add_write_resource(vc5->job,
                                                           target->buffer);
                        }
                        /* XXX: buffer_size? */
                }
        }

        if (vc5->dirty & VC5_DIRTY_OQ) {
                cl_emit(&job->bcl, OCCLUSION_QUERY_COUNTER, counter) {
                        job->oq_enabled = vc5->active_queries && vc5->current_oq;
                        if (job->oq_enabled) {
                                counter.address = cl_address(vc5->current_oq, 0);
                        }
                }
        }
}
