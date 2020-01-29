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

void
panfrost_emit_vertex_data(struct panfrost_batch *batch)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_vertex_state *so = ctx->vertex;

        /* Staged mali_attr, and index into them. i =/= k, depending on the
         * vertex buffer mask and instancing. Twice as much room is allocated,
         * for a worst case of NPOT_DIVIDEs which take up extra slot */
        union mali_attr attrs[PIPE_MAX_ATTRIBS * 2];
        unsigned k = 0;

        for (unsigned i = 0; i < so->num_elements; ++i) {
                /* We map a mali_attr to be 1:1 with the mali_attr_meta, which
                 * means duplicating some vertex buffers (who cares? aside from
                 * maybe some caching implications but I somehow doubt that
                 * matters) */

                struct pipe_vertex_element *elem = &so->pipe[i];
                unsigned vbi = elem->vertex_buffer_index;

                /* The exception to 1:1 mapping is that we can have multiple
                 * entries (NPOT divisors), so we fixup anyways */

                so->hw[i].index = k;

                if (!(ctx->vb_mask & (1 << vbi))) continue;

                struct pipe_vertex_buffer *buf = &ctx->vertex_buffers[vbi];
                struct panfrost_resource *rsrc = (struct panfrost_resource *) (buf->buffer.resource);

                if (!rsrc) continue;

                /* Align to 64 bytes by masking off the lower bits. This
                 * will be adjusted back when we fixup the src_offset in
                 * mali_attr_meta */

                mali_ptr raw_addr = rsrc->bo->gpu + buf->buffer_offset;
                mali_ptr addr = raw_addr & ~63;
                unsigned chopped_addr = raw_addr - addr;

                /* Add a dependency of the batch on the vertex buffer */
                panfrost_batch_add_bo(batch, rsrc->bo,
                                      PAN_BO_ACCESS_SHARED |
                                      PAN_BO_ACCESS_READ |
                                      PAN_BO_ACCESS_VERTEX_TILER);

                /* Set common fields */
                attrs[k].elements = addr;
                attrs[k].stride = buf->stride;

                /* Since we advanced the base pointer, we shrink the buffer
                 * size */
                attrs[k].size = rsrc->base.width0 - buf->buffer_offset;

                /* We need to add the extra size we masked off (for
                 * correctness) so the data doesn't get clamped away */
                attrs[k].size += chopped_addr;

                /* For non-instancing make sure we initialize */
                attrs[k].shift = attrs[k].extra_flags = 0;

                /* Instancing uses a dramatically different code path than
                 * linear, so dispatch for the actual emission now that the
                 * common code is finished */

                unsigned divisor = elem->instance_divisor;

                if (divisor && ctx->instance_count == 1) {
                        /* Silly corner case where there's a divisor(=1) but
                         * there's no legitimate instancing. So we want *every*
                         * attribute to be the same. So set stride to zero so
                         * we don't go anywhere. */

                        attrs[k].size = attrs[k].stride + chopped_addr;
                        attrs[k].stride = 0;
                        attrs[k++].elements |= MALI_ATTR_LINEAR;
                } else if (ctx->instance_count <= 1) {
                        /* Normal, non-instanced attributes */
                        attrs[k++].elements |= MALI_ATTR_LINEAR;
                } else {
                        unsigned instance_shift = batch->ctx->payloads[PIPE_SHADER_FRAGMENT].instance_shift;
                        unsigned instance_odd = batch->ctx->payloads[PIPE_SHADER_FRAGMENT].instance_odd;

                        k += panfrost_vertex_instanced(batch->ctx->padded_count,
                                        instance_shift, instance_odd, divisor, &attrs[k]);
                }
        }

        /* Add special gl_VertexID/gl_InstanceID buffers */

        panfrost_vertex_id(ctx->padded_count, &attrs[k]);
        so->hw[PAN_VERTEX_ID].index = k++;

        panfrost_instance_id(ctx->padded_count, &attrs[k]);
        so->hw[PAN_INSTANCE_ID].index = k++;

        /* Upload whatever we emitted and go */

        ctx->payloads[PIPE_SHADER_VERTEX].postfix.attributes =
                panfrost_upload_transient(batch, attrs, k * sizeof(union mali_attr));
}


