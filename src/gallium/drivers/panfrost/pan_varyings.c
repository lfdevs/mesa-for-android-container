/*
 * Copyright (C) 2018-2019 Alyssa Rosenzweig
 * Copyright (C) 2019 Collabora, Ltd.
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

#include "pan_bo.h"
#include "pan_context.h"
#include "util/u_prim.h"

static mali_ptr
panfrost_emit_varyings(
        struct panfrost_context *ctx,
        union mali_attr *slot,
        unsigned stride,
        unsigned count)
{
        /* Fill out the descriptor */
        slot->stride = stride;
        slot->size = stride * count;
        slot->shift = slot->extra_flags = 0;

        struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);
        struct panfrost_transfer transfer =
                panfrost_allocate_transient(batch, slot->size);

        slot->elements = transfer.gpu | MALI_ATTR_LINEAR;

        return transfer.gpu;
}

static void
panfrost_emit_streamout(
        struct panfrost_context *ctx,
        union mali_attr *slot,
        unsigned stride,
        unsigned offset,
        unsigned count,
        struct pipe_stream_output_target *target)
{
        /* Fill out the descriptor */
        slot->stride = stride * 4;
        slot->shift = slot->extra_flags = 0;

        unsigned max_size = target->buffer_size;
        unsigned expected_size = slot->stride * count;

        slot->size = MIN2(max_size, expected_size);

        /* Grab the BO and bind it to the batch */
        struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);
        struct panfrost_bo *bo = pan_resource(target->buffer)->bo;

        /* Varyings are WRITE from the perspective of the VERTEX but READ from
         * the perspective of the TILER and FRAGMENT.
         */
        panfrost_batch_add_bo(batch, bo,
                              PAN_BO_ACCESS_SHARED |
                              PAN_BO_ACCESS_RW |
                              PAN_BO_ACCESS_VERTEX_TILER |
                              PAN_BO_ACCESS_FRAGMENT);

        mali_ptr addr = bo->gpu + target->buffer_offset + (offset * slot->stride);
        slot->elements = addr;
}

/* Given a shader and buffer indices, link varying metadata together */

static bool
is_special_varying(gl_varying_slot loc)
{
        switch (loc) {
        case VARYING_SLOT_POS:
        case VARYING_SLOT_PSIZ:
        case VARYING_SLOT_PNTC:
        case VARYING_SLOT_FACE:
                return true;
        default:
                return false;
        }
}

static void
panfrost_emit_varying_meta(
                void *outptr, struct panfrost_shader_state *ss,
                signed general, signed gl_Position,
                signed gl_PointSize, signed gl_PointCoord,
                signed gl_FrontFacing)
{
        struct mali_attr_meta *out = (struct mali_attr_meta *) outptr;

        for (unsigned i = 0; i < ss->tripipe->varying_count; ++i) {
                gl_varying_slot location = ss->varyings_loc[i];
                int index = -1;

                switch (location) {
                case VARYING_SLOT_POS:
                        index = gl_Position;
                        break;
                case VARYING_SLOT_PSIZ:
                        index = gl_PointSize;
                        break;
                case VARYING_SLOT_PNTC:
                        index = gl_PointCoord;
                        break;
                case VARYING_SLOT_FACE:
                        index = gl_FrontFacing;
                        break;
                default:
                        index = general;
                        break;
                }

                assert(index >= 0);
                out[i].index = index;
        }
}

static bool
has_point_coord(unsigned mask, gl_varying_slot loc)
{
        if ((loc >= VARYING_SLOT_TEX0) && (loc <= VARYING_SLOT_TEX7))
                return (mask & (1 << (loc - VARYING_SLOT_TEX0)));
        else if (loc == VARYING_SLOT_PNTC)
                return (mask & (1 << 8));
        else
                return false;
}

/* Helpers for manipulating stream out information so we can pack varyings
 * accordingly. Compute the src_offset for a given captured varying */

static struct pipe_stream_output
pan_get_so(struct pipe_stream_output_info *info, gl_varying_slot loc)
{
        for (unsigned i = 0; i < info->num_outputs; ++i) {
                if (info->output[i].register_index == loc)
                        return  info->output[i];
        }

        unreachable("Varying not captured");
}

/* TODO: Integers */
static enum mali_format
pan_xfb_format(unsigned nr_components)
{
        switch (nr_components) {
                case 1: return MALI_R32F;
                case 2: return MALI_RG32F;
                case 3: return MALI_RGB32F;
                case 4: return MALI_RGBA32F;
                default: unreachable("Invalid format");
        }
}

void
panfrost_emit_varying_descriptor(
        struct panfrost_context *ctx,
        unsigned vertex_count)
{
        /* Load the shaders */

        struct panfrost_shader_state *vs = &ctx->shader[PIPE_SHADER_VERTEX]->variants[ctx->shader[PIPE_SHADER_VERTEX]->active_variant];
        struct panfrost_shader_state *fs = &ctx->shader[PIPE_SHADER_FRAGMENT]->variants[ctx->shader[PIPE_SHADER_FRAGMENT]->active_variant];
        unsigned int num_gen_varyings = 0;

        /* Allocate the varying descriptor */

        size_t vs_size = sizeof(struct mali_attr_meta) * vs->tripipe->varying_count;
        size_t fs_size = sizeof(struct mali_attr_meta) * fs->tripipe->varying_count;

        struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);
        struct panfrost_transfer trans = panfrost_allocate_transient(batch,
                                         vs_size + fs_size);

        struct pipe_stream_output_info so = vs->stream_output;

        /* Check if this varying is linked by us. This is the case for
         * general-purpose, non-captured varyings. If it is, link it. If it's
         * not, use the provided stream out information to determine the
         * offset, since it was already linked for us. */

        for (unsigned i = 0; i < vs->tripipe->varying_count; i++) {
                gl_varying_slot loc = vs->varyings_loc[i];

                bool special = is_special_varying(loc);
                bool captured = ((vs->so_mask & (1ll << loc)) ? true : false);

                if (captured) {
                        struct pipe_stream_output o = pan_get_so(&so, loc);

                        unsigned dst_offset = o.dst_offset * 4; /* dwords */
                        vs->varyings[i].src_offset = dst_offset;
                } else if (!special) {
                        vs->varyings[i].src_offset = 16 * (num_gen_varyings++);
                }
        }

        /* Conversely, we need to set src_offset for the captured varyings.
         * Here, the layout is defined by the stream out info, not us */

        /* Link up with fragment varyings */
        bool reads_point_coord = fs->reads_point_coord;

        for (unsigned i = 0; i < fs->tripipe->varying_count; i++) {
                gl_varying_slot loc = fs->varyings_loc[i];
                signed vs_idx = -1;

                /* Link up */
                for (unsigned j = 0; j < vs->tripipe->varying_count; ++j) {
                        if (vs->varyings_loc[j] == loc) {
                                vs_idx = j;
                                break;
                        }
                }

                /* Either assign or reuse */
                if (vs_idx >= 0)
                        fs->varyings[i].src_offset = vs->varyings[vs_idx].src_offset;
                else
                        fs->varyings[i].src_offset = 16 * (num_gen_varyings++);

                if (has_point_coord(fs->point_sprite_mask, loc))
                        reads_point_coord = true;
        }

        memcpy(trans.cpu, vs->varyings, vs_size);
        memcpy(trans.cpu + vs_size, fs->varyings, fs_size);

        union mali_attr varyings[PIPE_MAX_ATTRIBS];
        memset(varyings, 0, sizeof(varyings));

        /* Figure out how many streamout buffers could be bound */
        unsigned so_count = ctx->streamout.num_targets;
        for (unsigned i = 0; i < vs->tripipe->varying_count; i++) {
                gl_varying_slot loc = vs->varyings_loc[i];

                bool captured = ((vs->so_mask & (1ll << loc)) ? true : false);
                if (!captured) continue;

                struct pipe_stream_output o = pan_get_so(&so, loc);
                so_count = MAX2(so_count, o.output_buffer + 1);
        }

        signed idx = so_count;
        signed general = idx++;
        signed gl_Position = idx++;
        signed gl_PointSize = vs->writes_point_size ? (idx++) : -1;
        signed gl_PointCoord = reads_point_coord ? (idx++) : -1;
        signed gl_FrontFacing = fs->reads_face ? (idx++) : -1;
        signed gl_FragCoord = fs->reads_frag_coord ? (idx++) : -1;

        /* Emit the stream out buffers */

        unsigned output_count = u_stream_outputs_for_vertices(
                        ctx->active_prim, ctx->vertex_count);

        for (unsigned i = 0; i < so_count; ++i) {
                struct pipe_stream_output_target *target =
                        (i < ctx->streamout.num_targets) ? ctx->streamout.targets[i] : NULL;

                if (target) {
                        panfrost_emit_streamout(ctx, &varyings[i], so.stride[i], ctx->streamout.offsets[i], output_count, target);
                } else {
                        /* Emit a dummy buffer */
                        panfrost_emit_varyings(ctx, &varyings[i], so.stride[i] * 4, output_count);

                        /* Clear the attribute type */
                        varyings[i].elements &= ~0xF;
                }
        }

        panfrost_emit_varyings(ctx, &varyings[general], num_gen_varyings * 16,
                               vertex_count);

        /* fp32 vec4 gl_Position */
        ctx->payloads[PIPE_SHADER_FRAGMENT].postfix.position_varying =
                panfrost_emit_varyings(ctx, &varyings[gl_Position],
                                       sizeof(float) * 4, vertex_count);


        if (vs->writes_point_size)
                ctx->payloads[PIPE_SHADER_FRAGMENT].primitive_size.pointer =
                        panfrost_emit_varyings(ctx, &varyings[gl_PointSize],
                                               2, vertex_count);

        if (reads_point_coord)
                varyings[gl_PointCoord].elements = MALI_VARYING_POINT_COORD;

        if (fs->reads_face)
                varyings[gl_FrontFacing].elements = MALI_VARYING_FRONT_FACING;

        if (fs->reads_frag_coord)
                varyings[gl_FragCoord].elements = MALI_VARYING_FRAG_COORD;

        /* Let's go ahead and link varying meta to the buffer in question, now
         * that that information is available. VARYING_SLOT_POS is mapped to
         * gl_FragCoord for fragment shaders but gl_Positionf or vertex shaders
         * */

        panfrost_emit_varying_meta(trans.cpu, vs,
                general, gl_Position, gl_PointSize,
                gl_PointCoord, gl_FrontFacing);

        panfrost_emit_varying_meta(trans.cpu + vs_size, fs,
                general, gl_FragCoord, gl_PointSize,
                gl_PointCoord, gl_FrontFacing);

        /* Replace streamout */

        struct mali_attr_meta *ovs = (struct mali_attr_meta *) (trans.cpu);
        struct mali_attr_meta *ofs = (struct mali_attr_meta *) (trans.cpu + vs_size);

        for (unsigned i = 0; i < vs->tripipe->varying_count; i++) {
                gl_varying_slot loc = vs->varyings_loc[i];

                bool captured = ((vs->so_mask & (1ll << loc)) ? true : false);
                if (!captured) continue;

                struct pipe_stream_output o = pan_get_so(&so, loc);
                ovs[i].index = o.output_buffer;

                /* Set the type appropriately. TODO: Integer varyings XXX */
                assert(o.stream == 0);
                ovs[i].format = pan_xfb_format(o.num_components);
                ovs[i].swizzle = panfrost_get_default_swizzle(o.num_components);

                /* Link to the fragment */
                signed fs_idx = -1;

                /* Link up */
                for (unsigned j = 0; j < fs->tripipe->varying_count; ++j) {
                        if (fs->varyings_loc[j] == loc) {
                                fs_idx = j;
                                break;
                        }
                }

                if (fs_idx >= 0) {
                        ofs[fs_idx].index = ovs[i].index;
                        ofs[fs_idx].format = ovs[i].format;
                        ofs[fs_idx].swizzle = ovs[i].swizzle;
                }
        }

        /* Replace point sprite */
        for (unsigned i = 0; i < fs->tripipe->varying_count; i++) {
                /* If we have a point sprite replacement, handle that here. We
                 * have to translate location first.  TODO: Flip y in shader.
                 * We're already keying ... just time crunch .. */

                if (has_point_coord(fs->point_sprite_mask, fs->varyings_loc[i])) {
                        ofs[i].index = gl_PointCoord;

                        /* Swizzle out the z/w to 0/1 */
                        ofs[i].format = MALI_RG16F;
                        ofs[i].swizzle =
                                panfrost_get_default_swizzle(2);
                }
        }

        /* Fix up unaligned addresses */
        for (unsigned i = 0; i < so_count; ++i) {
                if (varyings[i].elements < MALI_VARYING_SPECIAL)
                        continue;

                unsigned align = (varyings[i].elements & 63);

                /* While we're at it, the SO buffers are linear */

                if (!align) {
                        varyings[i].elements |= MALI_ATTR_LINEAR;
                        continue;
                }

                /* We need to adjust alignment */
                varyings[i].elements &= ~63;
                varyings[i].elements |= MALI_ATTR_LINEAR;
                varyings[i].size += align;

                for (unsigned v = 0; v < vs->tripipe->varying_count; ++v) {
                        if (ovs[v].index == i)
                                ovs[v].src_offset = vs->varyings[v].src_offset + align;
                }

                for (unsigned f = 0; f < fs->tripipe->varying_count; ++f) {
                        if (ofs[f].index == i)
                                ofs[f].src_offset = fs->varyings[f].src_offset + align;
                }
        }

        mali_ptr varyings_p = panfrost_upload_transient(batch, &varyings, idx * sizeof(union mali_attr));
        ctx->payloads[PIPE_SHADER_VERTEX].postfix.varyings = varyings_p;
        ctx->payloads[PIPE_SHADER_FRAGMENT].postfix.varyings = varyings_p;

        ctx->payloads[PIPE_SHADER_VERTEX].postfix.varying_meta = trans.gpu;
        ctx->payloads[PIPE_SHADER_FRAGMENT].postfix.varying_meta = trans.gpu + vs_size;
}
