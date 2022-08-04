/*
 * Copyright (C) 2021 Collabora, Ltd.
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
 */

#include "compiler.h"
#include "util/u_memory.h"

/* Validatation doesn't make sense in release builds */
#ifndef NDEBUG

/* Validate that all sources are initialized in all read components. This is
 * required for correct register allocation. We check a weaker condition, that
 * all sources that are read are written at some point (equivalently, the live
 * set is empty at the start of the program). TODO: Strengthen */

bool
bi_validate_initialization(bi_context *ctx)
{
        bool success = true;

        /* Calculate the live set */
        bi_block *entry = bi_entry_block(ctx);
        unsigned temp_count = bi_max_temp(ctx);
        bi_compute_liveness(ctx);

        /* Validate that the live set is indeed empty */
        for (unsigned i = 0; i < temp_count; ++i) {
                if (entry->live_in[i] == 0) continue;

                fprintf(stderr, "%s%u\n", (i & PAN_IS_REG) ? "r" : "", i >> 1);
                success = false;
        }

        return success;
}

/*
 * Validate that there are no bi_registers accessed except at the beginning of
 * the start block, and that preloads are unique. This ensures RA can coalesce
 * preloads without interference tracking.
 */
static bool
bi_validate_preload(bi_context *ctx)
{
        bool start = true;
        uint64_t preloaded = 0;

        bi_foreach_block(ctx, block) {
                bi_foreach_instr_in_block(block, I) {
                        /* No instruction should have a register destination */
                        bi_foreach_dest(I, d) {
                                if (I->dest[d].type == BI_INDEX_REGISTER)
                                        return false;
                        }

                        /* Preloads are register moves at the start */
                        bool is_preload =
                                start && I->op == BI_OPCODE_MOV_I32 &&
                                I->src[0].type == BI_INDEX_REGISTER;

                        /* After the first nonpreload, we're done preloading */
                        start &= is_preload;

                        /* Only preloads may have a register source */
                        bi_foreach_src(I, s) {
                                if (I->src[s].type == BI_INDEX_REGISTER && !is_preload)
                                        return false;
                        }

                        /* Check uniqueness */
                        if (is_preload) {
                                unsigned r = I->src[0].value;

                                if (preloaded & BITFIELD64_BIT(r))
                                        return false;

                                preloaded |= BITFIELD64_BIT(r);
                        }
                }

                /* Only the first block may preload */
                start = false;
        }

        return true;
}

/*
 * Type check the dimensionality of sources and destinations. This occurs in two
 * passes, first to gather all destination sizes, second to validate all source
 * sizes. Depends on SSA form.
 */
static bool
bi_validate_width(bi_context *ctx)
{
        bool succ = true;
        uint8_t *width = calloc(ctx->ssa_alloc, sizeof(uint8_t));

        bi_foreach_instr_global(ctx, I) {
                bi_foreach_dest(I, d) {
                        if (bi_is_null(I->dest[d])) continue;
                        if (!bi_is_ssa(I->dest[d])) continue;

                        unsigned v = I->dest[d].value;
                        assert(width[v] == 0 && "broken SSA");

                        width[v] = bi_count_write_registers(I, d);
                }
        }

        bi_foreach_instr_global(ctx, I) {
                bi_foreach_src(I, s) {
                        if (!bi_is_ssa(I->src[s])) continue;

                        unsigned v = I->src[s].value;
                        unsigned n = bi_count_read_registers(I, s);

                        if (width[v] != n) {
                                succ = false;
                                fprintf(stderr,
                                        "source %u, expected width %u, got width %u\n",
                                        s, n, width[v]);
                                bi_print_instr(I, stderr);
                                fprintf(stderr, "\n");
                        }
                }
        }

        free(width);
        return succ;
}

void
bi_validate(bi_context *ctx, const char *after)
{
        bool fail = false;

        if (bifrost_debug & BIFROST_DBG_NOVALIDATE)
                return;

        if (!bi_validate_initialization(ctx)) {
                fprintf(stderr, "Uninitialized data read after %s\n", after);
                fail = true;
        }

        if (!bi_validate_preload(ctx)) {
                fprintf(stderr, "Unexpected preload after %s\n", after);
                fail = true;
        }

        if (!bi_validate_width(ctx)) {
                fprintf(stderr, "Unexpected vector with after %s\n", after);
                fail = true;
        }

        if (fail) {
                bi_print_shader(ctx, stderr);
                exit(1);
        }
}

#endif /* NDEBUG */
