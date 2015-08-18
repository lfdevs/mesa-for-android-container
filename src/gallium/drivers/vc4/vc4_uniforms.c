/*
 * Copyright © 2014-2015 Broadcom
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

#include "util/u_pack_color.h"
#include "util/format_srgb.h"

#include "vc4_context.h"
#include "vc4_qir.h"

static void
write_texture_p0(struct vc4_context *vc4,
                 struct vc4_cl_out **uniforms,
                 struct vc4_texture_stateobj *texstate,
                 uint32_t unit)
{
        struct vc4_sampler_view *sview =
                vc4_sampler_view(texstate->textures[unit]);
        struct vc4_resource *rsc = vc4_resource(sview->base.texture);

        cl_reloc(vc4, &vc4->uniforms, uniforms, rsc->bo, sview->texture_p0);
}

static void
write_texture_p1(struct vc4_context *vc4,
                 struct vc4_cl_out **uniforms,
                 struct vc4_texture_stateobj *texstate,
                 uint32_t unit)
{
        struct vc4_sampler_view *sview =
                vc4_sampler_view(texstate->textures[unit]);
        struct vc4_sampler_state *sampler =
                vc4_sampler_state(texstate->samplers[unit]);

        cl_aligned_u32(uniforms, sview->texture_p1 | sampler->texture_p1);
}

static void
write_texture_p2(struct vc4_context *vc4,
                 struct vc4_cl_out **uniforms,
                 struct vc4_texture_stateobj *texstate,
                 uint32_t data)
{
        uint32_t unit = data & 0xffff;
        struct pipe_sampler_view *texture = texstate->textures[unit];
        struct vc4_resource *rsc = vc4_resource(texture->texture);

        cl_aligned_u32(uniforms,
               VC4_SET_FIELD(VC4_TEX_P2_PTYPE_CUBE_MAP_STRIDE,
                             VC4_TEX_P2_PTYPE) |
               VC4_SET_FIELD(rsc->cube_map_stride >> 12, VC4_TEX_P2_CMST) |
               VC4_SET_FIELD((data >> 16) & 1, VC4_TEX_P2_BSLOD));
}


#define SWIZ(x,y,z,w) {          \
        UTIL_FORMAT_SWIZZLE_##x, \
        UTIL_FORMAT_SWIZZLE_##y, \
        UTIL_FORMAT_SWIZZLE_##z, \
        UTIL_FORMAT_SWIZZLE_##w  \
}

static void
write_texture_border_color(struct vc4_context *vc4,
                           struct vc4_cl_out **uniforms,
                           struct vc4_texture_stateobj *texstate,
                           uint32_t unit)
{
        struct pipe_sampler_state *sampler = texstate->samplers[unit];
        struct pipe_sampler_view *texture = texstate->textures[unit];
        struct vc4_resource *rsc = vc4_resource(texture->texture);
        union util_color uc;

        const struct util_format_description *tex_format_desc =
                util_format_description(texture->format);

        float border_color[4];
        for (int i = 0; i < 4; i++)
                border_color[i] = sampler->border_color.f[i];
        if (util_format_is_srgb(texture->format)) {
                for (int i = 0; i < 3; i++)
                        border_color[i] =
                                util_format_linear_to_srgb_float(border_color[i]);
        }

        /* Turn the border color into the layout of channels that it would
         * have when stored as texture contents.
         */
        float storage_color[4];
        util_format_unswizzle_4f(storage_color,
                                 border_color,
                                 tex_format_desc->swizzle);

        /* Now, pack so that when the vc4_format-sampled texture contents are
         * replaced with our border color, the vc4_get_format_swizzle()
         * swizzling will get the right channels.
         */
        if (util_format_is_depth_or_stencil(texture->format)) {
                uc.ui[0] = util_pack_z(PIPE_FORMAT_Z24X8_UNORM,
                                       sampler->border_color.f[0]) << 8;
        } else {
                switch (rsc->vc4_format) {
                default:
                case VC4_TEXTURE_TYPE_RGBA8888:
                        util_pack_color(storage_color,
                                        PIPE_FORMAT_R8G8B8A8_UNORM, &uc);
                        break;
                case VC4_TEXTURE_TYPE_RGBA4444:
                        util_pack_color(storage_color,
                                        PIPE_FORMAT_A8B8G8R8_UNORM, &uc);
                        break;
                case VC4_TEXTURE_TYPE_RGB565:
                        util_pack_color(storage_color,
                                        PIPE_FORMAT_B8G8R8A8_UNORM, &uc);
                        break;
                case VC4_TEXTURE_TYPE_ALPHA:
                        uc.ui[0] = float_to_ubyte(storage_color[0]) << 24;
                        break;
                case VC4_TEXTURE_TYPE_LUMALPHA:
                        uc.ui[0] = ((float_to_ubyte(storage_color[1]) << 24) |
                                    (float_to_ubyte(storage_color[0]) << 0));
                        break;
                }
        }

        cl_aligned_u32(uniforms, uc.ui[0]);
}

static uint32_t
get_texrect_scale(struct vc4_texture_stateobj *texstate,
                  enum quniform_contents contents,
                  uint32_t data)
{
        struct pipe_sampler_view *texture = texstate->textures[data];
        uint32_t dim;

        if (contents == QUNIFORM_TEXRECT_SCALE_X)
                dim = texture->texture->width0;
        else
                dim = texture->texture->height0;

        return fui(1.0f / dim);
}

static struct vc4_bo *
vc4_upload_ubo(struct vc4_context *vc4,
               struct vc4_compiled_shader *shader,
               const uint32_t *gallium_uniforms)
{
        if (!shader->ubo_size)
                return NULL;

        struct vc4_bo *ubo = vc4_bo_alloc(vc4->screen, shader->ubo_size, "ubo");
        uint32_t *data = vc4_bo_map(ubo);
        for (uint32_t i = 0; i < shader->num_ubo_ranges; i++) {
                memcpy(data + shader->ubo_ranges[i].dst_offset,
                       gallium_uniforms + shader->ubo_ranges[i].src_offset,
                       shader->ubo_ranges[i].size);
        }

        return ubo;
}

void
vc4_write_uniforms(struct vc4_context *vc4, struct vc4_compiled_shader *shader,
                   struct vc4_constbuf_stateobj *cb,
                   struct vc4_texture_stateobj *texstate)
{
        struct vc4_shader_uniform_info *uinfo = &shader->uniforms;
        const uint32_t *gallium_uniforms = cb->cb[0].user_buffer;
        struct vc4_bo *ubo = vc4_upload_ubo(vc4, shader, gallium_uniforms);

        cl_ensure_space(&vc4->uniforms, (uinfo->count +
                                         uinfo->num_texture_samples) * 4);

        struct vc4_cl_out *uniforms =
                cl_start_shader_reloc(&vc4->uniforms,
                                      uinfo->num_texture_samples);

        for (int i = 0; i < uinfo->count; i++) {

                switch (uinfo->contents[i]) {
                case QUNIFORM_CONSTANT:
                        cl_aligned_u32(&uniforms, uinfo->data[i]);
                        break;
                case QUNIFORM_UNIFORM:
                        cl_aligned_u32(&uniforms,
                                       gallium_uniforms[uinfo->data[i]]);
                        break;
                case QUNIFORM_VIEWPORT_X_SCALE:
                        cl_aligned_f(&uniforms, vc4->viewport.scale[0] * 16.0f);
                        break;
                case QUNIFORM_VIEWPORT_Y_SCALE:
                        cl_aligned_f(&uniforms, vc4->viewport.scale[1] * 16.0f);
                        break;

                case QUNIFORM_VIEWPORT_Z_OFFSET:
                        cl_aligned_f(&uniforms, vc4->viewport.translate[2]);
                        break;
                case QUNIFORM_VIEWPORT_Z_SCALE:
                        cl_aligned_f(&uniforms, vc4->viewport.scale[2]);
                        break;

                case QUNIFORM_USER_CLIP_PLANE:
                        cl_aligned_f(&uniforms,
                                     vc4->clip.ucp[uinfo->data[i] / 4][uinfo->data[i] % 4]);
                        break;

                case QUNIFORM_TEXTURE_CONFIG_P0:
                        write_texture_p0(vc4, &uniforms, texstate,
                                         uinfo->data[i]);
                        break;

                case QUNIFORM_TEXTURE_CONFIG_P1:
                        write_texture_p1(vc4, &uniforms, texstate,
                                         uinfo->data[i]);
                        break;

                case QUNIFORM_TEXTURE_CONFIG_P2:
                        write_texture_p2(vc4, &uniforms, texstate,
                                         uinfo->data[i]);
                        break;

                case QUNIFORM_UBO_ADDR:
                        cl_aligned_reloc(vc4, &vc4->uniforms, &uniforms, ubo, 0);
                        break;

                case QUNIFORM_TEXTURE_BORDER_COLOR:
                        write_texture_border_color(vc4, &uniforms,
                                                   texstate, uinfo->data[i]);
                        break;

                case QUNIFORM_TEXRECT_SCALE_X:
                case QUNIFORM_TEXRECT_SCALE_Y:
                        cl_aligned_u32(&uniforms,
                                       get_texrect_scale(texstate,
                                                         uinfo->contents[i],
                                                         uinfo->data[i]));
                        break;

                case QUNIFORM_BLEND_CONST_COLOR_X:
                case QUNIFORM_BLEND_CONST_COLOR_Y:
                case QUNIFORM_BLEND_CONST_COLOR_Z:
                case QUNIFORM_BLEND_CONST_COLOR_W:
                        cl_aligned_f(&uniforms,
                                     CLAMP(vc4->blend_color.color[uinfo->contents[i] -
                                                                  QUNIFORM_BLEND_CONST_COLOR_X],
                                           0, 1));
                        break;

                case QUNIFORM_STENCIL:
                        cl_aligned_u32(&uniforms,
                                       vc4->zsa->stencil_uniforms[uinfo->data[i]] |
                                       (uinfo->data[i] <= 1 ?
                                        (vc4->stencil_ref.ref_value[uinfo->data[i]] << 8) :
                                        0));
                        break;

                case QUNIFORM_ALPHA_REF:
                        cl_aligned_f(&uniforms,
                                     vc4->zsa->base.alpha.ref_value);
                        break;
                }
#if 0
                uint32_t written_val = *((uint32_t *)uniforms - 1);
                fprintf(stderr, "%p: %d / 0x%08x (%f)\n",
                        shader, i, written_val, uif(written_val));
#endif
        }

        cl_end(&vc4->uniforms, uniforms);

        vc4_bo_unreference(&ubo);
}

void
vc4_set_shader_uniform_dirty_flags(struct vc4_compiled_shader *shader)
{
        uint32_t dirty = 0;

        for (int i = 0; i < shader->uniforms.count; i++) {
                switch (shader->uniforms.contents[i]) {
                case QUNIFORM_CONSTANT:
                        break;
                case QUNIFORM_UNIFORM:
                case QUNIFORM_UBO_ADDR:
                        dirty |= VC4_DIRTY_CONSTBUF;
                        break;

                case QUNIFORM_VIEWPORT_X_SCALE:
                case QUNIFORM_VIEWPORT_Y_SCALE:
                case QUNIFORM_VIEWPORT_Z_OFFSET:
                case QUNIFORM_VIEWPORT_Z_SCALE:
                        dirty |= VC4_DIRTY_VIEWPORT;
                        break;

                case QUNIFORM_USER_CLIP_PLANE:
                        dirty |= VC4_DIRTY_CLIP;
                        break;

                case QUNIFORM_TEXTURE_CONFIG_P0:
                case QUNIFORM_TEXTURE_CONFIG_P1:
                case QUNIFORM_TEXTURE_CONFIG_P2:
                case QUNIFORM_TEXTURE_BORDER_COLOR:
                case QUNIFORM_TEXRECT_SCALE_X:
                case QUNIFORM_TEXRECT_SCALE_Y:
                        dirty |= VC4_DIRTY_TEXSTATE;
                        break;

                case QUNIFORM_BLEND_CONST_COLOR_X:
                case QUNIFORM_BLEND_CONST_COLOR_Y:
                case QUNIFORM_BLEND_CONST_COLOR_Z:
                case QUNIFORM_BLEND_CONST_COLOR_W:
                        dirty |= VC4_DIRTY_BLEND_COLOR;
                        break;

                case QUNIFORM_STENCIL:
                case QUNIFORM_ALPHA_REF:
                        dirty |= VC4_DIRTY_ZSA;
                        break;
                }
        }

        shader->uniform_dirty_bits = dirty;
}
