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

/* See mali_job for notes on how this works. But basically, for small vertex
 * counts, we have a lookup table, and for large vertex counts, we look at the
 * high bits as a heuristic. This has to match exactly how the hardware
 * calculates this (which is why the algorithm is so weird) or else instancing
 * will break. */

/* Given an odd number (of the form 2k + 1), compute k */
#define ODD(odd) ((odd - 1) >> 1)

/* Given the shift/odd pair, recover the original padded integer */

unsigned
pan_expand_shift_odd(struct pan_shift_odd o)
{
        unsigned odd = 2*o.odd + 1;
        unsigned shift = 1 << o.shift;
        return odd * shift;
}

static inline struct pan_shift_odd
pan_factored(unsigned pot, unsigned odd)
{
        struct pan_shift_odd out;

        assert(util_is_power_of_two_or_zero(pot));
        assert(odd & 1);

        /* Odd is of the form (2k + 1) = (k << 1) + 1 = (k << 1) | 1.
         *
         * So (odd >> 1) = ((k << 1) | 1) >> 1 = ((k << 1) >> 1) | (1 >> 1)
         *  = k | 0 = k */

        out.odd = (odd >> 1);

        /* POT is the form (1 << shift) */
        out.shift = __builtin_ctz(pot);

        return out;
}


/* For small vertices. Second argument is whether the primitive takes a
 * power-of-two argument, which determines how rounding works. True for POINTS
 * and LINES, false for TRIANGLES. Presumably true for QUADS but you'd be crazy
 * to try instanced quads on ES class hardware <3 */

static struct {
        unsigned pot;
        unsigned odd;
} small_lut[] = {
        {  0, 1 },
        {  1, 1 },
        {  2, 1 },
        {  1, 3 },
        {  4, 1 },
        {  1, 5 },
        {  2, 3 },
        {  1, 7 },
        {  8, 1 },
        {  1, 9 },
        {  2, 5 },
        {  4, 3 }, /* 11 */
        {  4, 3 },
        {  2, 7 }, /* 13 */
        {  2, 7 },
        { 16, 1 }, /* 15 */
        { 16, 1 },
        {  2, 9 },
        {  4, 5 }, /* 20 */
        {  4, 5 }
};

static struct pan_shift_odd
panfrost_small_padded_vertex_count(unsigned idx)
{
        return pan_factored(
                       small_lut[idx].pot,
                       small_lut[idx].odd);
}

static struct pan_shift_odd
panfrost_large_padded_vertex_count(uint32_t vertex_count)
{
        struct pan_shift_odd out = { 0 };

        /* First, we have to find the highest set one */
        unsigned highest = 32 - __builtin_clz(vertex_count);

        /* Using that, we mask out the highest 4-bits */
        unsigned n = highest - 4;
        unsigned nibble = (vertex_count >> n) & 0xF;

        /* Great, we have the nibble. Now we can just try possibilities. Note
         * that we don't care about the bottom most bit in most cases, and we
         * know the top bit must be 1 */

        unsigned middle_two = (nibble >> 1) & 0x3;

        switch (middle_two) {
        case 0b00:
                if (nibble & 1)
                        return pan_factored(1 << n, 9);
                else
                        return pan_factored(1 << (n + 1), 5);
        case 0b01:
                return pan_factored(1 << (n + 2), 3);
        case 0b10:
                return pan_factored(1 << (n + 1), 7);
        case 0b11:
                return pan_factored(1 << (n + 4), 1);
        default:
                unreachable("Invalid two bits");
        }

        return out;
}

struct pan_shift_odd
panfrost_padded_vertex_count(
        unsigned vertex_count,
        bool pot)
{
        assert(vertex_count > 0);

        if (vertex_count < 20) {
                /* Add an off-by-one if it won't align naturally (quirk of the hardware) */
                //if (!pot)
                //      vertex_count++;

                return panfrost_small_padded_vertex_count(vertex_count);
        } else
                return panfrost_large_padded_vertex_count(vertex_count);
}

/* The much, much more irritating case -- instancing is enabled. See
 * panfrost_job.h for notes on how this works */

static unsigned
panfrost_vertex_instanced(
        struct panfrost_batch *batch,
        struct panfrost_resource *rsrc,
        unsigned divisor,
        union mali_attr *attrs,
        mali_ptr addr,
        unsigned vertex_count,
        unsigned instance_count)
{
        /* First, grab the padded vertex count */

        struct pan_shift_odd o = {
                .shift = batch->ctx->payloads[PIPE_SHADER_FRAGMENT].instance_shift,
                .odd = batch->ctx->payloads[PIPE_SHADER_FRAGMENT].instance_odd,
        };

        unsigned padded_count = batch->ctx->padded_count;

        /* Depending if there is an instance divisor or not, packing varies.
         * When there is a divisor, the hardware-level divisor is actually the
         * product of the instance divisor and the padded count */

        unsigned hw_divisor = padded_count * divisor;

        if (divisor == 0) {
                /* Per-vertex attributes use the MODULO mode. First, compute
                 * the modulus */

                attrs->elements |= MALI_ATTR_MODULO;
                attrs->shift = o.shift;
                attrs->extra_flags = o.odd;

                return 1;
        } else if (util_is_power_of_two_or_zero(hw_divisor)) {
                /* If there is a divisor but the hardware divisor works out to
                 * a power of two (not terribly exceptional), we can use an
                 * easy path (just shifting) */

                attrs->elements |= MALI_ATTR_POT_DIVIDE;
                attrs->shift = __builtin_ctz(hw_divisor);

                return 1;
        } else {
                /* We have a NPOT divisor. Here's the fun one (multipling by
                 * the inverse and shifting) */

                /* floor(log2(d)) */
                unsigned shift = util_logbase2(hw_divisor);

                /* m = ceil(2^(32 + shift) / d) */
                uint64_t shift_hi = 32 + shift;
                uint64_t t = 1ll << shift_hi;
                double t_f = t;
                double hw_divisor_d = hw_divisor;
                double m_f = ceil(t_f / hw_divisor_d);
                unsigned m = m_f;

                /* Default case */
                uint32_t magic_divisor = m, extra_flags = 0;

                /* e = 2^(shift + 32) % d */
                uint64_t e = t % hw_divisor;

                /* Apply round-down algorithm? e <= 2^shift?. XXX: The blob
                 * seems to use a different condition */
                if (e <= (1ll << shift)) {
                        magic_divisor = m - 1;
                        extra_flags = 1;
                }

                /* Top flag implicitly set */
                assert(magic_divisor & (1u << 31));
                magic_divisor &= ~(1u << 31);

                /* Upload to two different slots */

                attrs[0].elements |= MALI_ATTR_NPOT_DIVIDE;
                attrs[0].shift = shift;
                attrs[0].extra_flags = extra_flags;

                attrs[1].unk = 0x20;
                attrs[1].magic_divisor = magic_divisor;
                attrs[1].zero = 0;
                attrs[1].divisor = divisor;

                return 2;
        }
}

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

        unsigned vertex_count = ctx->vertex_count;
        unsigned instanced_count = ctx->instance_count;

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

                if (divisor && instanced_count == 1) {
                        /* Silly corner case where there's a divisor(=1) but
                         * there's no legitimate instancing. So we want *every*
                         * attribute to be the same. So set stride to zero so
                         * we don't go anywhere. */

                        attrs[k].size = attrs[k].stride + chopped_addr;
                        attrs[k].stride = 0;
                        attrs[k++].elements |= MALI_ATTR_LINEAR;
                } else if (instanced_count <= 1) {
                        /* Normal, non-instanced attributes */
                        attrs[k++].elements |= MALI_ATTR_LINEAR;
                } else {
                        k += panfrost_vertex_instanced(
                                     batch, rsrc, divisor, &attrs[k], addr, vertex_count, instanced_count);
                }
        }

        /* Upload whatever we emitted and go */

        ctx->payloads[PIPE_SHADER_VERTEX].postfix.attributes =
                panfrost_upload_transient(batch, attrs, k * sizeof(union mali_attr));
}


