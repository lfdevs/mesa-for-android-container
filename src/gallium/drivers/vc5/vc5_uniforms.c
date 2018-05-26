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

#include "util/u_pack_color.h"
#include "util/format_srgb.h"

#include "vc5_context.h"
#include "compiler/v3d_compiler.h"
#include "broadcom/cle/v3d_packet_v33_pack.h"

#if 0

#define SWIZ(x,y,z,w) {          \
        PIPE_SWIZZLE_##x, \
        PIPE_SWIZZLE_##y, \
        PIPE_SWIZZLE_##z, \
        PIPE_SWIZZLE_##w  \
}

static void
write_texture_border_color(struct vc5_job *job,
                           struct vc5_cl_out **uniforms,
                           struct vc5_texture_stateobj *texstate,
                           uint32_t unit)
{
        struct pipe_sampler_state *sampler = texstate->samplers[unit];
        struct pipe_sampler_view *texture = texstate->textures[unit];
        struct vc5_resource *rsc = vc5_resource(texture->texture);
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

        /* Now, pack so that when the vc5_format-sampled texture contents are
         * replaced with our border color, the vc5_get_format_swizzle()
         * swizzling will get the right channels.
         */
        if (util_format_is_depth_or_stencil(texture->format)) {
                uc.ui[0] = util_pack_z(PIPE_FORMAT_Z24X8_UNORM,
                                       sampler->border_color.f[0]) << 8;
        } else {
                switch (rsc->vc5_format) {
                default:
                case VC5_TEXTURE_TYPE_RGBA8888:
                        util_pack_color(storage_color,
                                        PIPE_FORMAT_R8G8B8A8_UNORM, &uc);
                        break;
                case VC5_TEXTURE_TYPE_RGBA4444:
                        util_pack_color(storage_color,
                                        PIPE_FORMAT_A8B8G8R8_UNORM, &uc);
                        break;
                case VC5_TEXTURE_TYPE_RGB565:
                        util_pack_color(storage_color,
                                        PIPE_FORMAT_B8G8R8A8_UNORM, &uc);
                        break;
                case VC5_TEXTURE_TYPE_ALPHA:
                        uc.ui[0] = float_to_ubyte(storage_color[0]) << 24;
                        break;
                case VC5_TEXTURE_TYPE_LUMALPHA:
                        uc.ui[0] = ((float_to_ubyte(storage_color[1]) << 24) |
                                    (float_to_ubyte(storage_color[0]) << 0));
                        break;
                }
        }

        cl_aligned_u32(uniforms, uc.ui[0]);
}
#endif

static uint32_t
get_texrect_scale(struct vc5_texture_stateobj *texstate,
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

static uint32_t
get_texture_size(struct vc5_texture_stateobj *texstate,
                 enum quniform_contents contents,
                 uint32_t data)
{
        struct pipe_sampler_view *texture = texstate->textures[data];

        switch (contents) {
        case QUNIFORM_TEXTURE_WIDTH:
                return u_minify(texture->texture->width0,
                                texture->u.tex.first_level);
        case QUNIFORM_TEXTURE_HEIGHT:
                return u_minify(texture->texture->height0,
                                texture->u.tex.first_level);
        case QUNIFORM_TEXTURE_DEPTH:
                return u_minify(texture->texture->depth0,
                                texture->u.tex.first_level);
        case QUNIFORM_TEXTURE_ARRAY_SIZE:
                return texture->texture->array_size;
        case QUNIFORM_TEXTURE_LEVELS:
                return (texture->u.tex.last_level -
                        texture->u.tex.first_level) + 1;
        default:
                unreachable("Bad texture size field");
        }
}

static struct vc5_bo *
vc5_upload_ubo(struct vc5_context *vc5,
               struct vc5_compiled_shader *shader,
               const uint32_t *gallium_uniforms)
{
        if (!shader->prog_data.base->ubo_size)
                return NULL;

        struct vc5_bo *ubo = vc5_bo_alloc(vc5->screen,
                                          shader->prog_data.base->ubo_size,
                                          "ubo");
        void *data = vc5_bo_map(ubo);
        for (uint32_t i = 0; i < shader->prog_data.base->num_ubo_ranges; i++) {
                memcpy(data + shader->prog_data.base->ubo_ranges[i].dst_offset,
                       ((const void *)gallium_uniforms +
                        shader->prog_data.base->ubo_ranges[i].src_offset),
                       shader->prog_data.base->ubo_ranges[i].size);
        }

        return ubo;
}

/**
 *  Writes the V3D 3.x P0 (CFG_MODE=1) texture parameter.
 *
 * Some bits of this field are dependent on the type of sample being done by
 * the shader, while other bits are dependent on the sampler state.  We OR the
 * two together here.
 */
static void
write_texture_p0(struct vc5_job *job,
                 struct vc5_cl_out **uniforms,
                 struct vc5_texture_stateobj *texstate,
                 uint32_t unit,
                 uint32_t shader_data)
{
        struct pipe_sampler_state *psampler = texstate->samplers[unit];
        struct vc5_sampler_state *sampler = vc5_sampler_state(psampler);

        cl_aligned_u32(uniforms, shader_data | sampler->p0);
}

/** Writes the V3D 3.x P1 (CFG_MODE=1) texture parameter. */
static void
write_texture_p1(struct vc5_job *job,
                 struct vc5_cl_out **uniforms,
                 struct vc5_texture_stateobj *texstate,
                 uint32_t data)
{
        /* Extract the texture unit from the top bits, and the compiler's
         * packed p1 from the bottom.
         */
        uint32_t unit = data >> 5;
        uint32_t p1 = data & 0x1f;

        struct pipe_sampler_view *psview = texstate->textures[unit];
        struct vc5_sampler_view *sview = vc5_sampler_view(psview);

        struct V3D33_TEXTURE_UNIFORM_PARAMETER_1_CFG_MODE1 unpacked = {
                .texture_state_record_base_address = texstate->texture_state[unit],
        };

        uint32_t packed;
        V3D33_TEXTURE_UNIFORM_PARAMETER_1_CFG_MODE1_pack(&job->indirect,
                                                         (uint8_t *)&packed,
                                                         &unpacked);

        cl_aligned_u32(uniforms, p1 | packed | sview->p1);
}

/** Writes the V3D 4.x TMU configuration parameter 0. */
static void
write_tmu_p0(struct vc5_job *job,
             struct vc5_cl_out **uniforms,
             struct vc5_texture_stateobj *texstate,
             uint32_t data)
{
        /* Extract the texture unit from the top bits, and the compiler's
         * packed p0 from the bottom.
         */
        uint32_t unit = data >> 24;
        uint32_t p0 = data & 0x00ffffff;

        struct pipe_sampler_view *psview = texstate->textures[unit];
        struct vc5_sampler_view *sview = vc5_sampler_view(psview);
        struct vc5_resource *rsc = vc5_resource(psview->texture);

        cl_aligned_reloc(&job->indirect, uniforms, sview->bo, p0);
        vc5_job_add_bo(job, rsc->bo);
}

/** Writes the V3D 4.x TMU configuration parameter 1. */
static void
write_tmu_p1(struct vc5_job *job,
             struct vc5_cl_out **uniforms,
             struct vc5_texture_stateobj *texstate,
             uint32_t data)
{
        /* Extract the texture unit from the top bits, and the compiler's
         * packed p1 from the bottom.
         */
        uint32_t unit = data >> 24;
        uint32_t p0 = data & 0x00ffffff;

        struct pipe_sampler_state *psampler = texstate->samplers[unit];
        struct vc5_sampler_state *sampler = vc5_sampler_state(psampler);

        cl_aligned_reloc(&job->indirect, uniforms, sampler->bo, p0);
}

struct vc5_cl_reloc
vc5_write_uniforms(struct vc5_context *vc5, struct vc5_compiled_shader *shader,
                   struct vc5_constbuf_stateobj *cb,
                   struct vc5_texture_stateobj *texstate)
{
        struct v3d_uniform_list *uinfo = &shader->prog_data.base->uniforms;
        struct vc5_job *job = vc5->job;
        const uint32_t *gallium_uniforms = cb->cb[0].user_buffer;
        struct vc5_bo *ubo = vc5_upload_ubo(vc5, shader, gallium_uniforms);

        /* We always need to return some space for uniforms, because the HW
         * will be prefetching, even if we don't read any in the program.
         */
        vc5_cl_ensure_space(&job->indirect, MAX2(uinfo->count, 1) * 4, 4);

        struct vc5_cl_reloc uniform_stream = cl_get_address(&job->indirect);
        vc5_bo_reference(uniform_stream.bo);

        struct vc5_cl_out *uniforms =
                cl_start(&job->indirect);

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
                        cl_aligned_f(&uniforms, vc5->viewport.scale[0] * 256.0f);
                        break;
                case QUNIFORM_VIEWPORT_Y_SCALE:
                        cl_aligned_f(&uniforms, vc5->viewport.scale[1] * 256.0f);
                        break;

                case QUNIFORM_VIEWPORT_Z_OFFSET:
                        cl_aligned_f(&uniforms, vc5->viewport.translate[2]);
                        break;
                case QUNIFORM_VIEWPORT_Z_SCALE:
                        cl_aligned_f(&uniforms, vc5->viewport.scale[2]);
                        break;

                case QUNIFORM_USER_CLIP_PLANE:
                        cl_aligned_f(&uniforms,
                                     vc5->clip.ucp[uinfo->data[i] / 4][uinfo->data[i] % 4]);
                        break;

                case QUNIFORM_TMU_CONFIG_P0:
                        write_tmu_p0(job, &uniforms, texstate,
                                         uinfo->data[i]);
                        break;

                case QUNIFORM_TMU_CONFIG_P1:
                        write_tmu_p1(job, &uniforms, texstate,
                                         uinfo->data[i]);
                        break;

                case QUNIFORM_TEXTURE_CONFIG_P1:
                        write_texture_p1(job, &uniforms, texstate,
                                         uinfo->data[i]);
                        break;

#if 0
                case QUNIFORM_TEXTURE_FIRST_LEVEL:
                        write_texture_first_level(job, &uniforms, texstate,
                                                  uinfo->data[i]);
                        break;
#endif

                case QUNIFORM_TEXRECT_SCALE_X:
                case QUNIFORM_TEXRECT_SCALE_Y:
                        cl_aligned_u32(&uniforms,
                                       get_texrect_scale(texstate,
                                                         uinfo->contents[i],
                                                         uinfo->data[i]));
                        break;

                case QUNIFORM_TEXTURE_WIDTH:
                case QUNIFORM_TEXTURE_HEIGHT:
                case QUNIFORM_TEXTURE_DEPTH:
                case QUNIFORM_TEXTURE_ARRAY_SIZE:
                case QUNIFORM_TEXTURE_LEVELS:
                        cl_aligned_u32(&uniforms,
                                       get_texture_size(texstate,
                                                        uinfo->contents[i],
                                                        uinfo->data[i]));
                        break;

                case QUNIFORM_STENCIL:
                        cl_aligned_u32(&uniforms,
                                       vc5->zsa->stencil_uniforms[uinfo->data[i]] |
                                       (uinfo->data[i] <= 1 ?
                                        (vc5->stencil_ref.ref_value[uinfo->data[i]] << 8) :
                                        0));
                        break;

                case QUNIFORM_ALPHA_REF:
                        cl_aligned_f(&uniforms,
                                     vc5->zsa->base.alpha.ref_value);
                        break;

                case QUNIFORM_SAMPLE_MASK:
                        cl_aligned_u32(&uniforms, vc5->sample_mask);
                        break;

                case QUNIFORM_UBO_ADDR:
                        if (uinfo->data[i] == 0) {
                                cl_aligned_reloc(&job->indirect, &uniforms,
                                                 ubo, 0);
                        } else {
                                int ubo_index = uinfo->data[i];
                                struct vc5_resource *rsc =
                                        vc5_resource(cb->cb[ubo_index].buffer);

                                cl_aligned_reloc(&job->indirect, &uniforms,
                                                 rsc->bo,
                                                 cb->cb[ubo_index].buffer_offset);
                        }
                        break;

                case QUNIFORM_TEXTURE_FIRST_LEVEL:
                        cl_aligned_f(&uniforms,
                                     texstate->textures[uinfo->data[i]]->u.tex.first_level);
                        break;

                case QUNIFORM_TEXTURE_BORDER_COLOR:
                        /* XXX */
                        break;

                case QUNIFORM_SPILL_OFFSET:
                        cl_aligned_reloc(&job->indirect, &uniforms,
                                         vc5->prog.spill_bo, 0);
                        break;

                case QUNIFORM_SPILL_SIZE_PER_THREAD:
                        cl_aligned_u32(&uniforms,
                                       vc5->prog.spill_size_per_thread);
                        break;

                default:
                        assert(quniform_contents_is_texture_p0(uinfo->contents[i]));

                        write_texture_p0(job, &uniforms, texstate,
                                         uinfo->contents[i] -
                                         QUNIFORM_TEXTURE_CONFIG_P0_0,
                                         uinfo->data[i]);
                        break;

                }
#if 0
                uint32_t written_val = *((uint32_t *)uniforms - 1);
                fprintf(stderr, "shader %p[%d]: 0x%08x / 0x%08x (%f)\n",
                        shader, i, __gen_address_offset(&uniform_stream) + i * 4,
                        written_val, uif(written_val));
#endif
        }

        cl_end(&job->indirect, uniforms);

        vc5_bo_unreference(&ubo);

        return uniform_stream;
}

void
vc5_set_shader_uniform_dirty_flags(struct vc5_compiled_shader *shader)
{
        uint32_t dirty = 0;

        for (int i = 0; i < shader->prog_data.base->uniforms.count; i++) {
                switch (shader->prog_data.base->uniforms.contents[i]) {
                case QUNIFORM_CONSTANT:
                        break;
                case QUNIFORM_UNIFORM:
                case QUNIFORM_UBO_ADDR:
                        dirty |= VC5_DIRTY_CONSTBUF;
                        break;

                case QUNIFORM_VIEWPORT_X_SCALE:
                case QUNIFORM_VIEWPORT_Y_SCALE:
                case QUNIFORM_VIEWPORT_Z_OFFSET:
                case QUNIFORM_VIEWPORT_Z_SCALE:
                        dirty |= VC5_DIRTY_VIEWPORT;
                        break;

                case QUNIFORM_USER_CLIP_PLANE:
                        dirty |= VC5_DIRTY_CLIP;
                        break;

                case QUNIFORM_TMU_CONFIG_P0:
                case QUNIFORM_TMU_CONFIG_P1:
                case QUNIFORM_TEXTURE_CONFIG_P1:
                case QUNIFORM_TEXTURE_BORDER_COLOR:
                case QUNIFORM_TEXTURE_FIRST_LEVEL:
                case QUNIFORM_TEXRECT_SCALE_X:
                case QUNIFORM_TEXRECT_SCALE_Y:
                case QUNIFORM_TEXTURE_WIDTH:
                case QUNIFORM_TEXTURE_HEIGHT:
                case QUNIFORM_TEXTURE_DEPTH:
                case QUNIFORM_TEXTURE_ARRAY_SIZE:
                case QUNIFORM_TEXTURE_LEVELS:
                case QUNIFORM_SPILL_OFFSET:
                case QUNIFORM_SPILL_SIZE_PER_THREAD:
                        /* We could flag this on just the stage we're
                         * compiling for, but it's not passed in.
                         */
                        dirty |= VC5_DIRTY_FRAGTEX | VC5_DIRTY_VERTTEX;
                        break;

                case QUNIFORM_STENCIL:
                case QUNIFORM_ALPHA_REF:
                        dirty |= VC5_DIRTY_ZSA;
                        break;

                case QUNIFORM_SAMPLE_MASK:
                        dirty |= VC5_DIRTY_SAMPLE_MASK;
                        break;

                default:
                        assert(quniform_contents_is_texture_p0(shader->prog_data.base->uniforms.contents[i]));
                        dirty |= VC5_DIRTY_FRAGTEX | VC5_DIRTY_VERTTEX;
                        break;
                }
        }

        shader->uniform_dirty_bits = dirty;
}
