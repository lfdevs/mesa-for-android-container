/*
 * Copyright (C) 2020 Collabora Ltd.
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
 * Authors (Collabora):
 *      Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include "compiler.h"
#include "nodearray.h"
#include "bi_builder.h"
#include "util/u_memory.h"

struct lcra_state {
        unsigned node_count;
        uint64_t *affinity;

        /* Linear constraints imposed. For each node there there is a
         * 'nodearray' structure, which changes between a sparse and dense
         * array depending on the number of elements.
         *
         * Each element is itself a bit field denoting whether (c_j - c_i) bias
         * is present or not, including negative biases.
         *
         * We support up to 8 components so the bias is in range
         * [-7, 7] encoded by a 16-bit field
         */
        nodearray *linear;

        /* Before solving, forced registers; after solving, solutions. */
        unsigned *solutions;

        /** Node which caused register allocation to fail */
        unsigned spill_node;
};

/* This module is an implementation of "Linearly Constrained
 * Register Allocation". The paper is available in PDF form
 * (https://people.collabora.com/~alyssa/LCRA.pdf) as well as Markdown+LaTeX
 * (https://gitlab.freedesktop.org/alyssa/lcra/blob/master/LCRA.md)
 */

static struct lcra_state *
lcra_alloc_equations(unsigned node_count)
{
        struct lcra_state *l = calloc(1, sizeof(*l));

        l->node_count = node_count;

        l->linear = calloc(sizeof(l->linear[0]), node_count);
        l->solutions = calloc(sizeof(l->solutions[0]), node_count);
        l->affinity = calloc(sizeof(l->affinity[0]), node_count);

        memset(l->solutions, ~0, sizeof(l->solutions[0]) * node_count);

        return l;
}

static void
lcra_free(struct lcra_state *l)
{
        for (unsigned i = 0; i < l->node_count; ++i)
                nodearray_reset(&l->linear[i]);

        free(l->linear);
        free(l->affinity);
        free(l->solutions);
        free(l);
}

static void
lcra_add_node_interference(struct lcra_state *l, unsigned i, unsigned cmask_i, unsigned j, unsigned cmask_j)
{
        if (i == j)
                return;

        nodearray_value constraint_fw = 0;
        nodearray_value constraint_bw = 0;

        /* The constraint bits are reversed from lcra.c so that register
         * allocation can be done in parallel for every possible solution,
         * with lower-order bits representing smaller registers. */

        for (unsigned D = 0; D < 8; ++D) {
                if (cmask_i & (cmask_j << D)) {
                        constraint_fw |= (1 << (7 + D));
                        constraint_bw |= (1 << (7 - D));
                }

                if (cmask_i & (cmask_j >> D)) {
                        constraint_bw |= (1 << (7 + D));
                        constraint_fw |= (1 << (7 - D));
                }
        }

        /* Use dense arrays after adding 256 elements */
        nodearray_orr(&l->linear[j], i, constraint_fw, 256, l->node_count);
        nodearray_orr(&l->linear[i], j, constraint_bw, 256, l->node_count);
}

static bool
lcra_test_linear(struct lcra_state *l, unsigned *solutions, unsigned i)
{
        signed constant = solutions[i];

        if (nodearray_is_sparse(&l->linear[i])) {
                nodearray_sparse_foreach(&l->linear[i], elem) {
                        unsigned j = nodearray_sparse_key(elem);
                        nodearray_value constraint = nodearray_sparse_value(elem);

                        if (solutions[j] == ~0) continue;

                        signed lhs = constant - solutions[j];

                        if (lhs < -7 || lhs > 7)
                                continue;

                        if (constraint & (1 << (lhs + 7)))
                                return false;
                }

                return true;
        }

        nodearray_value *row = l->linear[i].dense;

        for (unsigned j = 0; j < l->node_count; ++j) {
                if (solutions[j] == ~0) continue;

                signed lhs = constant - solutions[j];

                if (lhs < -7 || lhs > 7)
                        continue;

                if (row[j] & (1 << (lhs + 7)))
                        return false;
        }

        return true;
}

static bool
lcra_solve(struct lcra_state *l)
{
        for (unsigned step = 0; step < l->node_count; ++step) {
                if (l->solutions[step] != ~0) continue;
                if (l->affinity[step] == 0) continue;

                bool succ = false;

                u_foreach_bit64(r, l->affinity[step]) {
                        l->solutions[step] = r;

                        if (lcra_test_linear(l, l->solutions, step)) {
                                succ = true;
                                break;
                        }
                }

                /* Out of registers - prepare to spill */
                if (!succ) {
                        l->spill_node = step;
                        return false;
                }
        }

        return true;
}

/* Register spilling is implemented with a cost-benefit system. Costs are set
 * by the user. Benefits are calculated from the constraints. */

static unsigned
lcra_count_constraints(struct lcra_state *l, unsigned i)
{
        unsigned count = 0;
        nodearray *constraints = &l->linear[i];

        if (nodearray_is_sparse(constraints)) {
                nodearray_sparse_foreach(constraints, elem)
                        count += util_bitcount(nodearray_sparse_value(elem));
        } else {
                nodearray_dense_foreach_64(constraints, elem)
                        count += util_bitcount64(*elem);
        }

        return count;
}

/* Construct an affinity mask such that the vector with `count` elements does
 * not intersect any of the registers in the bitset `clobber`. In other words,
 * an allocated register r needs to satisfy for each i < count: a + i != b.
 * Equivalently that's a != b - i, so we need a \ne { b - i : i < n }. For the
 * entire clobber set B, we need a \ne union b \in B { b - i : i < n }, where
 * that union is the desired clobber set. That may be written equivalently as
 * the union over i < n of (B - i), where subtraction is defined elementwise
 * and corresponds to a shift of the entire bitset.
 *
 * EVEN_BITS_MASK is an affinity mask for aligned register pairs. Interpreted
 * as a bit set, it is { x : 0 <= x < 64 if x is even }
 */

#define EVEN_BITS_MASK (0x5555555555555555ull)

static uint64_t
bi_make_affinity(uint64_t clobber, unsigned count, bool split_file)
{
        uint64_t clobbered = 0;

        for (unsigned i = 0; i < count; ++i)
                clobbered |= (clobber >> i);

        /* Don't allocate past the end of the register file */
        if (count > 1) {
                unsigned excess = count - 1;
                uint64_t mask = BITFIELD_MASK(excess);
                clobbered |= mask << (64 - excess);

                if (split_file)
                        clobbered |= mask << (16 - excess);
        }

        /* Don't allocate the middle if we split out the middle */
        if (split_file)
                clobbered |= BITFIELD64_MASK(32) << 16;

        /* We can use a register iff it's not clobberred */
        return ~clobbered;
}

static void
bi_mark_interference(bi_block *block, struct lcra_state *l, uint8_t *live, uint64_t preload_live, unsigned node_count, bool is_blend, bool split_file, bool aligned_sr)
{
        bi_foreach_instr_in_block_rev(block, ins) {
                /* Mark all registers live after the instruction as
                 * interfering with the destination */

                bi_foreach_dest(ins, d) {
                        unsigned node = bi_get_node(ins->dest[d]);

                        if (node >= node_count)
                                continue;

                        /* Don't allocate to anything that's read later as a
                         * preloaded register. The affinity is the intersection
                         * of affinity masks for each write. Since writes have
                         * offsets, but the affinity is for the whole node, we
                         * need to offset the affinity opposite the write
                         * offset, so we shift right. */
                        unsigned count = bi_count_write_registers(ins, d);
                        unsigned offset = ins->dest[d].offset;
                        uint64_t affinity = bi_make_affinity(preload_live, count, split_file) >> offset;
                        /* Valhall needs >= 64-bit staging writes to be pair-aligned */
                        if (aligned_sr && (count >= 2 || offset))
                                affinity &= EVEN_BITS_MASK;

                        l->affinity[node] &= affinity;

                        for (unsigned i = 0; i < node_count; ++i) {
                                uint8_t r = live[i];

                                /* Nodes only interfere if they occupy
                                 * /different values/ at the same time
                                 * (Boissinot). In particular, sources of
                                 * moves do not interfere with their
                                 * destinations. This enables a limited form of
                                 * coalescing.
                                 */
                                if (ins->op == BI_OPCODE_MOV_I32 &&
                                    i == bi_get_node(ins->src[0])) {

                                        r &= ~BITFIELD_BIT(ins->src[0].offset);
                                }

                                if (r) {
                                        lcra_add_node_interference(l, node,
                                                        bi_writemask(ins, d), i, r);
                                }
                        }

                        unsigned node_first = bi_get_node(ins->dest[0]);
                        if (d == 1 && node_first < node_count) {
                                lcra_add_node_interference(l, node, bi_writemask(ins, 1),
                                                           node_first, bi_writemask(ins, 0));
                        }
                }

                /* Valhall needs >= 64-bit reads to be pair-aligned */
                if (aligned_sr) {
                        bi_foreach_src(ins, s) {
                                if (bi_count_read_registers(ins, s) >= 2) {
                                        unsigned node = bi_get_node(ins->src[s]);

                                        if (node < node_count)
                                                l->affinity[node] &= EVEN_BITS_MASK;
                                }
                        }
                }

                if (!is_blend && ins->op == BI_OPCODE_BLEND) {
                        /* Blend shaders might clobber r0-r15, r48. */
                        uint64_t clobber = BITFIELD64_MASK(16) | BITFIELD64_BIT(48);

                        for (unsigned i = 0; i < node_count; ++i) {
                                if (live[i])
                                        l->affinity[i] &= ~clobber;
                        }
                }

                /* Update live_in */
                preload_live = bi_postra_liveness_ins(preload_live, ins);
                bi_liveness_ins_update(live, ins, node_count);
        }

        block->reg_live_in = preload_live;
}

static void
bi_compute_interference(bi_context *ctx, struct lcra_state *l, bool full_regs)
{
        unsigned node_count = bi_max_temp(ctx);

        bi_compute_liveness(ctx);
        bi_postra_liveness(ctx);

        bi_foreach_block_rev(ctx, blk) {
                uint8_t *live = mem_dup(blk->live_out, node_count);

                bi_mark_interference(blk, l, live, blk->reg_live_out,
                                node_count, ctx->inputs->is_blend, !full_regs,
                                ctx->arch >= 9);

                free(live);
        }
}

static struct lcra_state *
bi_allocate_registers(bi_context *ctx, bool *success, bool full_regs)
{
        unsigned node_count = bi_max_temp(ctx);
        struct lcra_state *l = lcra_alloc_equations(node_count);

        /* Blend shaders are restricted to R0-R15. Other shaders at full
         * occupancy also can access R48-R63. At half occupancy they can access
         * the whole file. */

        uint64_t default_affinity =
                ctx->inputs->is_blend ? BITFIELD64_MASK(16) :
                full_regs ? BITFIELD64_MASK(64) :
                (BITFIELD64_MASK(16) | (BITFIELD64_MASK(16) << 48));

        /* To test spilling, mimic a small register file */
        if (bifrost_debug & BIFROST_DBG_SPILL && !ctx->inputs->is_blend)
                default_affinity &= BITFIELD64_MASK(48) << 8;

        bi_foreach_instr_global(ctx, ins) {
                bi_foreach_dest(ins, d) {
                        unsigned dest = bi_get_node(ins->dest[d]);

                        if (dest < node_count)
                                l->affinity[dest] = default_affinity;
                }

                /* Blend shaders expect the src colour to be in r0-r3 */
                if (ins->op == BI_OPCODE_BLEND &&
                    !ctx->inputs->is_blend) {
                        unsigned node = bi_get_node(ins->src[0]);
                        assert(node < node_count);
                        l->solutions[node] = 0;

                        /* Dual source blend input in r4-r7 */
                        node = bi_get_node(ins->src[4]);
                        if (node < node_count)
                                l->solutions[node] = 4;

                        /* Writes to R48 */
                        node = bi_get_node(ins->dest[0]);
                        if (!bi_is_null(ins->dest[0])) {
                                assert(node < node_count);
                                l->solutions[node] = 48;
                        }
                }

                /* Coverage mask writes stay in R60 */
                if ((ins->op == BI_OPCODE_ATEST ||
                     ins->op == BI_OPCODE_ZS_EMIT) &&
                    !bi_is_null(ins->dest[0])) {
                        unsigned node = bi_get_node(ins->dest[0]);
                        assert(node < node_count);
                        l->solutions[node] = 60;
                }

                /* Experimentally, it seems coverage masks inputs to ATEST must
                 * be in R60. Otherwise coverage mask writes do not work with
                 * early-ZS with pixel-frequency-shading (this combination of
                 * settings is legal if depth/stencil writes are disabled).
                 */
                if (ins->op == BI_OPCODE_ATEST) {
                        unsigned node = bi_get_node(ins->src[0]);
                        assert(node < node_count);
                        l->solutions[node] = 60;
                }
        }

        bi_compute_interference(ctx, l, full_regs);

        /* Coalesce register moves if we're allowed. We need to be careful due
         * to the restricted affinity induced by the blend shader ABI.
         */
        bi_foreach_instr_global(ctx, I) {
                if (I->op != BI_OPCODE_MOV_I32) continue;
                if (I->src[0].type != BI_INDEX_REGISTER) continue;

                unsigned reg = I->src[0].value;
                unsigned node = bi_get_node(I->dest[0]);
                assert(node < node_count);

                if (l->solutions[node] != ~0) continue;

                uint64_t affinity = l->affinity[node];

                if (ctx->inputs->is_blend) {
                        /* We're allowed to coalesce the moves to these */
                        affinity |= BITFIELD64_BIT(48);
                        affinity |= BITFIELD64_BIT(60);
                }

                /* Try to coalesce */
                if (affinity & BITFIELD64_BIT(reg)) {
                        l->solutions[node] = reg;

                        if (!lcra_test_linear(l, l->solutions, node))
                                l->solutions[node] = ~0;
                }
        }

        *success = lcra_solve(l);

        return l;
}

static bi_index
bi_reg_from_index(bi_context *ctx, struct lcra_state *l, bi_index index)
{
        /* Offsets can only be applied when we register allocated an index, or
         * alternatively for FAU's encoding */

        ASSERTED bool is_offset = (index.offset > 0) &&
                (index.type != BI_INDEX_FAU);
        unsigned node_count = bi_max_temp(ctx);

        /* Did we run RA for this index at all */
        if (bi_get_node(index) >= node_count) {
                assert(!is_offset);
                return index;
        }

        /* LCRA didn't bother solving this index (how lazy!) */
        signed solution = l->solutions[bi_get_node(index)];
        if (solution < 0) {
                assert(!is_offset);
                return index;
        }

        /* todo: do we want to compose with the subword swizzle? */
        bi_index new_index = bi_register(solution + index.offset);
        new_index.swizzle = index.swizzle;
        new_index.abs = index.abs;
        new_index.neg = index.neg;
        return new_index;
}

/* Dual texture instructions write to two sets of staging registers, modeled as
 * two destinations in the IR. The first set is communicated with the usual
 * staging register mechanism. The second set is encoded in the texture
 * operation descriptor. This is quite unusual, and requires the following late
 * fixup.
 */
static void
bi_fixup_dual_tex_register(bi_instr *I)
{
        assert(I->dest[1].type == BI_INDEX_REGISTER);
        assert(I->src[3].type == BI_INDEX_CONSTANT);

        struct bifrost_dual_texture_operation desc = {
                .secondary_register = I->dest[1].value
        };

        I->src[3].value |= bi_dual_tex_as_u32(desc);
}

static void
bi_install_registers(bi_context *ctx, struct lcra_state *l)
{
        bi_foreach_instr_global(ctx, ins) {
                bi_foreach_dest(ins, d)
                        ins->dest[d] = bi_reg_from_index(ctx, l, ins->dest[d]);

                bi_foreach_src(ins, s)
                        ins->src[s] = bi_reg_from_index(ctx, l, ins->src[s]);

                if (ins->op == BI_OPCODE_TEXC && !bi_is_null(ins->dest[1]))
                        bi_fixup_dual_tex_register(ins);
        }
}

static void
bi_rewrite_index_src_single(bi_instr *ins, bi_index old, bi_index new)
{
        bi_foreach_src(ins, i) {
                if (bi_is_equiv(ins->src[i], old)) {
                        ins->src[i].type = new.type;
                        ins->src[i].reg = new.reg;
                        ins->src[i].value = new.value;
                }
        }
}

/* If register allocation fails, find the best spill node */

static signed
bi_choose_spill_node(bi_context *ctx, struct lcra_state *l)
{
        /* Pick a node satisfying bi_spill_register's preconditions */
        BITSET_WORD *no_spill = calloc(sizeof(BITSET_WORD), BITSET_WORDS(l->node_count));

        bi_foreach_instr_global(ctx, ins) {
                bi_foreach_dest(ins, d) {
                        unsigned node = bi_get_node(ins->dest[d]);

                        if (node >= l->node_count)
                                continue;

                        /* Don't allow spilling coverage mask writes because the
                         * register preload logic assumes it will stay in R60.
                         * This could be optimized.
                         */
                        if (ins->no_spill ||
                            ins->op == BI_OPCODE_ATEST ||
                            ins->op == BI_OPCODE_ZS_EMIT ||
                            (ins->op == BI_OPCODE_MOV_I32 &&
                             ins->src[0].type == BI_INDEX_REGISTER &&
                             ins->src[0].value == 60)) {
                                BITSET_SET(no_spill, node);
                        }
                }
        }

        unsigned best_benefit = 0.0;
        signed best_node = -1;

        if (nodearray_is_sparse(&l->linear[l->spill_node])) {
                nodearray_sparse_foreach(&l->linear[l->spill_node], elem) {
                        unsigned i = nodearray_sparse_key(elem);
                        unsigned constraint = nodearray_sparse_value(elem);

                        /* Only spill nodes that interfere with the node failing
                         * register allocation. It's pointless to spill anything else */
                        if (!constraint) continue;

                        if (BITSET_TEST(no_spill, i)) continue;

                        unsigned benefit = lcra_count_constraints(l, i);

                        if (benefit > best_benefit) {
                                best_benefit = benefit;
                                best_node = i;
                        }
                }
        } else {
                nodearray_value *row = l->linear[l->spill_node].dense;

                for (unsigned i = 0; i < l->node_count; ++i) {
                        /* Only spill nodes that interfere with the node failing
                         * register allocation. It's pointless to spill anything else */
                        if (!row[i]) continue;

                        if (BITSET_TEST(no_spill, i)) continue;

                        unsigned benefit = lcra_count_constraints(l, i);

                        if (benefit > best_benefit) {
                                best_benefit = benefit;
                                best_node = i;
                        }
                }
        }

        free(no_spill);
        return best_node;
}

static unsigned
bi_count_read_index(bi_instr *I, bi_index index)
{
        unsigned max = 0;

        bi_foreach_src(I, s) {
                if (bi_is_equiv(I->src[s], index)) {
                        unsigned count = bi_count_read_registers(I, s);
                        max = MAX2(max, count + I->src[s].offset);
                }
        }

        return max;
}

/*
 * Wrappers to emit loads/stores to thread-local storage in an appropriate way
 * for the target, so the spill/fill code becomes architecture-independent.
 */

static bi_index
bi_tls_ptr(bool hi)
{
        return bi_fau(BIR_FAU_TLS_PTR, hi);
}

static bi_instr *
bi_load_tl(bi_builder *b, unsigned bits, bi_index src, unsigned offset)
{
        if (b->shader->arch >= 9) {
                return bi_load_to(b, bits, src, bi_tls_ptr(false),
                                  bi_tls_ptr(true), BI_SEG_TL, offset);
        } else {
                return bi_load_to(b, bits, src, bi_imm_u32(offset), bi_zero(),
                                  BI_SEG_TL, 0);
        }
}

static void
bi_store_tl(bi_builder *b, unsigned bits, bi_index src, unsigned offset)
{
        if (b->shader->arch >= 9) {
                bi_store(b, bits, src, bi_tls_ptr(false), bi_tls_ptr(true), BI_SEG_TL, offset);
        } else {
                bi_store(b, bits, src, bi_imm_u32(offset), bi_zero(), BI_SEG_TL, 0);
        }
}

/* Once we've chosen a spill node, spill it and returns bytes spilled */

static unsigned
bi_spill_register(bi_context *ctx, bi_index index, uint32_t offset)
{
        bi_builder b = { .shader = ctx };
        unsigned channels = 0;

        /* Spill after every store, fill before every load */
        bi_foreach_instr_global_safe(ctx, I) {
                bi_foreach_dest(I, d) {
                        if (!bi_is_equiv(I->dest[d], index)) continue;

                        unsigned extra = I->dest[d].offset;
                        bi_index tmp = bi_temp(ctx);

                        I->dest[d] = bi_replace_index(I->dest[d], tmp);
                        I->no_spill = true;

                        unsigned count = bi_count_write_registers(I, d);
                        unsigned bits = count * 32;

                        b.cursor = bi_after_instr(I);
                        bi_store_tl(&b, bits, tmp, offset + 4 * extra);

                        ctx->spills++;
                        channels = MAX2(channels, extra + count);
                }

                if (bi_has_arg(I, index)) {
                        b.cursor = bi_before_instr(I);
                        bi_index tmp = bi_temp(ctx);

                        unsigned bits = bi_count_read_index(I, index) * 32;
                        bi_rewrite_index_src_single(I, index, tmp);

                        bi_instr *ld = bi_load_tl(&b, bits, tmp, offset);
                        ld->no_spill = true;
                        ctx->fills++;
                }
        }

        return (channels * 4);
}

/*
 * For transition, lower collects and splits before RA, rather than after RA.
 * LCRA knows how to deal with offsets (broken SSA), but not how to coalesce
 * these vector moves.
 */
static void
bi_lower_vector(bi_context *ctx)
{
        bi_index *remap = calloc(ctx->ssa_alloc, sizeof(bi_index));

        bi_foreach_instr_global_safe(ctx, I) {
                bi_builder b = bi_init_builder(ctx, bi_after_instr(I));

                if (I->op == BI_OPCODE_SPLIT_I32) {
                        bi_index src = I->src[0];
                        assert(src.offset == 0);

                        for (unsigned i = 0; i < I->nr_dests; ++i) {
                                if (bi_is_null(I->dest[i]))
                                        continue;

                                src.offset = i;
                                bi_mov_i32_to(&b, I->dest[i], src);

                                if (bi_is_ssa(I->dest[i]))
                                        remap[I->dest[i].value] = src;
                        }

                        bi_remove_instruction(I);
                } else if (I->op == BI_OPCODE_COLLECT_I32) {
                        bi_index dest = I->dest[0];
                        assert(dest.offset == 0);
                        assert((bi_is_ssa(dest) || I->nr_srcs == 1) && "nir_lower_phis_to_scalar");

                        for (unsigned i = 0; i < I->nr_srcs; ++i) {
                                if (bi_is_null(I->src[i]))
                                        continue;

                                dest.offset = i;
                                bi_mov_i32_to(&b, dest, I->src[i]);
                        }

                        bi_remove_instruction(I);
                }
        }

        bi_foreach_instr_global(ctx, I) {
                bi_foreach_src(I, s) {
                        if (bi_is_ssa(I->src[s]) && !bi_is_null(remap[I->src[s].value]))
                                I->src[s] = bi_replace_index(I->src[s], remap[I->src[s].value]);
                }
        }

        free(remap);

        /* After generating a pile of moves, clean up */
        bi_opt_dead_code_eliminate(ctx);
}

/*
 * Check if the instruction requires a "tied" operand. Such instructions MUST
 * allocate their source and destination to the same register. This is a
 * constraint on RA, and may require extra moves.
 *
 * In particular, this is the case for Bifrost instructions that both read and
 * write with the staging register mechanism.
 */
static bool
bi_is_tied(const bi_instr *I)
{
        if (bi_is_null(I->src[0]))
                return false;

        return (I->op == BI_OPCODE_TEXC ||
                I->op == BI_OPCODE_ATOM_RETURN_I32 ||
                I->op == BI_OPCODE_AXCHG_I32 ||
                I->op == BI_OPCODE_ACMPXCHG_I32);
}

/*
 * For transition, coalesce tied operands together, as LCRA knows how to handle
 * non-SSA operands but doesn't know about tied operands.
 *
 * This breaks the SSA form of the program, but that doesn't matter for LCRA.
 */
static void
bi_coalesce_tied(bi_context *ctx)
{
        bi_foreach_instr_global(ctx, I) {
                if (!bi_is_tied(I)) continue;

                bi_builder b = bi_init_builder(ctx, bi_before_instr(I));
                unsigned n = bi_count_read_registers(I, 0);

                for (unsigned i = 0; i < n; ++i) {
                        bi_index dst = I->dest[0], src = I->src[0];

                        assert(dst.offset == 0 && src.offset == 0);
                        dst.offset = src.offset = i;

                        bi_mov_i32_to(&b, dst, src);
                }

                I->src[0] = bi_replace_index(I->src[0], I->dest[0]);
        }
}

static unsigned
find_or_allocate_temp(unsigned *map, unsigned value, unsigned *alloc)
{
        if (!map[value])
                map[value] = ++(*alloc);

        assert(map[value]);
        return map[value] - 1;
}

/* Reassigns numbering to get rid of gaps in the indices and to prioritize
 * smaller register classes */

static void
squeeze_index(bi_context *ctx)
{
        unsigned *map = rzalloc_array(ctx, unsigned, ctx->ssa_alloc);
        ctx->ssa_alloc = 0;

        bi_foreach_instr_global(ctx, I) {
                bi_foreach_dest(I, d) {
                        if (I->dest[d].type == BI_INDEX_NORMAL)
                                I->dest[d].value = find_or_allocate_temp(map, I->dest[d].value, &ctx->ssa_alloc);
                }

                bi_foreach_src(I, s) {
                        if (I->src[s].type == BI_INDEX_NORMAL)
                                I->src[s].value = find_or_allocate_temp(map, I->src[s].value, &ctx->ssa_alloc);
                }
        }

        ralloc_free(map);
}

void
bi_register_allocate(bi_context *ctx)
{
        struct lcra_state *l = NULL;
        bool success = false;

        unsigned iter_count = 1000; /* max iterations */

        /* Number of bytes of memory we've spilled into */
        unsigned spill_count = ctx->info.tls_size;

        if (ctx->arch >= 9)
                va_lower_split_64bit(ctx);

        bi_lower_vector(ctx);

        /* Lower tied operands. SSA is broken from here on. */
        bi_coalesce_tied(ctx);
        squeeze_index(ctx);

        /* Try with reduced register pressure to improve thread count */
        if (ctx->arch >= 7) {
                l = bi_allocate_registers(ctx, &success, false);

                if (success) {
                        ctx->info.work_reg_count = 32;
                } else {
                        lcra_free(l);
                        l = NULL;
                }
        }

        /* Otherwise, use the register file and spill until we succeed */
        while (!success && ((iter_count--) > 0)) {
                l = bi_allocate_registers(ctx, &success, true);

                if (success) {
                        ctx->info.work_reg_count = 64;
                } else {
                        signed spill_node = bi_choose_spill_node(ctx, l);
                        lcra_free(l);
                        l = NULL;

                        if (spill_node == -1)
                                unreachable("Failed to choose spill node\n");

                        if (ctx->inputs->is_blend)
                                unreachable("Blend shaders may not spill");

                        /* By default, we use packed TLS addressing on Valhall.
                         * We cannot cross 16 byte boundaries with packed TLS
                         * addressing. Align to ensure this doesn't happen. This
                         * could be optimized a bit.
                         */
                        if (ctx->arch >= 9)
                                spill_count = ALIGN_POT(spill_count, 16);

                        spill_count += bi_spill_register(ctx,
                                        bi_node_to_index(spill_node, bi_max_temp(ctx)),
                                        spill_count);

                        /* In case the spill affected an instruction with tied
                         * operands, we need to fix up.
                         */
                        bi_coalesce_tied(ctx);
                }
        }

        assert(success);
        assert(l != NULL);

        ctx->info.tls_size = spill_count;
        bi_install_registers(ctx, l);

        lcra_free(l);
}
