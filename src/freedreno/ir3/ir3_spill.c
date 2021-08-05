/*
 * Copyright (C) 2021 Valve Corporation
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

#include "util/rb_tree.h"
#include "ir3_ra.h"
#include "ir3_shader.h"

/*
 * This pass does one thing so far:
 *
 * 1. Calculates the maximum register pressure. To do this, we need to use the
 * exact same technique that RA uses for combining meta_split instructions
 * with their sources, so that our calculation agrees with RA.
 *
 * It will also optionally spill registers once that's implemented.
 */

struct ra_spill_interval {
   struct ir3_reg_interval interval;
};

struct ra_spill_ctx {
   struct ir3_reg_ctx reg_ctx;

   struct ra_spill_interval *intervals;

   struct ir3_pressure cur_pressure, max_pressure;

   struct ir3_liveness *live;

   const struct ir3_compiler *compiler;
};

static void
ra_spill_interval_init(struct ra_spill_interval *interval,
                       struct ir3_register *reg)
{
   ir3_reg_interval_init(&interval->interval, reg);
}

static void
ra_pressure_add(struct ir3_pressure *pressure,
                struct ra_spill_interval *interval)
{
   unsigned size = reg_size(interval->interval.reg);
   if (interval->interval.reg->flags & IR3_REG_SHARED)
      pressure->shared += size;
   else if (interval->interval.reg->flags & IR3_REG_HALF)
      pressure->half += size;
   else
      pressure->full += size;
}

static void
ra_pressure_sub(struct ir3_pressure *pressure,
                struct ra_spill_interval *interval)
{
   unsigned size = reg_size(interval->interval.reg);
   if (interval->interval.reg->flags & IR3_REG_SHARED)
      pressure->shared -= size;
   else if (interval->interval.reg->flags & IR3_REG_HALF)
      pressure->half -= size;
   else
      pressure->full -= size;
}

static struct ra_spill_interval *
ir3_reg_interval_to_interval(struct ir3_reg_interval *interval)
{
   return rb_node_data(struct ra_spill_interval, interval, interval);
}

static struct ra_spill_ctx *
ir3_reg_ctx_to_ctx(struct ir3_reg_ctx *ctx)
{
   return rb_node_data(struct ra_spill_ctx, ctx, reg_ctx);
}

static void
interval_add(struct ir3_reg_ctx *_ctx, struct ir3_reg_interval *_interval)
{
   struct ra_spill_interval *interval = ir3_reg_interval_to_interval(_interval);
   struct ra_spill_ctx *ctx = ir3_reg_ctx_to_ctx(_ctx);

   ra_pressure_add(&ctx->cur_pressure, interval);
}

static void
interval_delete(struct ir3_reg_ctx *_ctx, struct ir3_reg_interval *_interval)
{
   struct ra_spill_interval *interval = ir3_reg_interval_to_interval(_interval);
   struct ra_spill_ctx *ctx = ir3_reg_ctx_to_ctx(_ctx);

   ra_pressure_sub(&ctx->cur_pressure, interval);
}

static void
interval_readd(struct ir3_reg_ctx *_ctx, struct ir3_reg_interval *_parent,
               struct ir3_reg_interval *_child)
{
   interval_add(_ctx, _child);
}

static void
spill_ctx_init(struct ra_spill_ctx *ctx)
{
   rb_tree_init(&ctx->reg_ctx.intervals);
   ctx->reg_ctx.interval_add = interval_add;
   ctx->reg_ctx.interval_delete = interval_delete;
   ctx->reg_ctx.interval_readd = interval_readd;
}

static void
ra_spill_ctx_insert(struct ra_spill_ctx *ctx,
                    struct ra_spill_interval *interval)
{
   ir3_reg_interval_insert(&ctx->reg_ctx, &interval->interval);
}

static void
ra_spill_ctx_remove(struct ra_spill_ctx *ctx,
                    struct ra_spill_interval *interval)
{
   ir3_reg_interval_remove(&ctx->reg_ctx, &interval->interval);
}

static void
init_dst(struct ra_spill_ctx *ctx, struct ir3_register *dst)
{
   struct ra_spill_interval *interval = &ctx->intervals[dst->name];
   ra_spill_interval_init(interval, dst);
}

static void
insert_dst(struct ra_spill_ctx *ctx, struct ir3_register *dst)
{
   struct ra_spill_interval *interval = &ctx->intervals[dst->name];
   if (interval->interval.inserted)
      return;

   ra_spill_ctx_insert(ctx, interval);

   /* For precolored inputs, make sure we leave enough registers to allow for
    * holes in the inputs. It can happen that the binning shader has a lower
    * register pressure than the main shader, but the main shader decided to
    * add holes between the inputs which means that the binning shader has a
    * higher register demand.
    */
   if (dst->instr->opc == OPC_META_INPUT && dst->num != INVALID_REG) {
      physreg_t physreg = ra_reg_get_physreg(dst);
      physreg_t max = physreg + reg_size(dst);

      if (interval->interval.reg->flags & IR3_REG_SHARED)
         ctx->max_pressure.shared = MAX2(ctx->max_pressure.shared, max);
      else if (interval->interval.reg->flags & IR3_REG_HALF)
         ctx->max_pressure.half = MAX2(ctx->max_pressure.half, max);
      else
         ctx->max_pressure.full = MAX2(ctx->max_pressure.full, max);
   }
}

static void
remove_src_early(struct ra_spill_ctx *ctx, struct ir3_instruction *instr,
                 struct ir3_register *src)
{
   if (!(src->flags & IR3_REG_FIRST_KILL))
      return;

   struct ra_spill_interval *interval = &ctx->intervals[src->def->name];

   if (!interval->interval.inserted || interval->interval.parent ||
       !rb_tree_is_empty(&interval->interval.children))
      return;

   ra_spill_ctx_remove(ctx, interval);
}

static void
remove_src(struct ra_spill_ctx *ctx, struct ir3_instruction *instr,
           struct ir3_register *src)
{
   if (!(src->flags & IR3_REG_FIRST_KILL))
      return;

   struct ra_spill_interval *interval = &ctx->intervals[src->def->name];

   if (!interval->interval.inserted)
      return;

   ra_spill_ctx_remove(ctx, interval);
}

static void
remove_dst(struct ra_spill_ctx *ctx, struct ir3_register *dst)
{
   struct ra_spill_interval *interval = &ctx->intervals[dst->name];

   if (!interval->interval.inserted)
      return;

   ra_spill_ctx_remove(ctx, interval);
}

static void
update_max_pressure(struct ra_spill_ctx *ctx)
{
   d("pressure:");
   d("\tfull: %u", ctx->cur_pressure.full);
   d("\thalf: %u", ctx->cur_pressure.half);
   d("\tshared: %u", ctx->cur_pressure.shared);

   ctx->max_pressure.full =
      MAX2(ctx->max_pressure.full, ctx->cur_pressure.full);
   ctx->max_pressure.half =
      MAX2(ctx->max_pressure.half, ctx->cur_pressure.half);
   ctx->max_pressure.shared =
      MAX2(ctx->max_pressure.shared, ctx->cur_pressure.shared);
}

static void
handle_instr(struct ra_spill_ctx *ctx, struct ir3_instruction *instr)
{
   if (RA_DEBUG) {
      printf("processing: ");
      ir3_print_instr(instr);
   }

   ra_foreach_dst (dst, instr) {
      init_dst(ctx, dst);
   }

   /* Handle tied destinations. If a destination is tied to a source and that
    * source is live-through, then we need to allocate a new register for the
    * destination which is live-through itself and cannot overlap the
    * sources.
    */

   ra_foreach_dst (dst, instr) {
      struct ir3_register *tied_src = dst->tied;
      if (tied_src && !(tied_src->flags & IR3_REG_FIRST_KILL))
         insert_dst(ctx, dst);
   }

   update_max_pressure(ctx);

   ra_foreach_src (src, instr) {
      if (src->flags & IR3_REG_FIRST_KILL)
         remove_src_early(ctx, instr, src);
   }

   ra_foreach_dst (dst, instr) {
      insert_dst(ctx, dst);
   }

   update_max_pressure(ctx);

   for (unsigned i = 0; i < instr->srcs_count; i++) {
      if (ra_reg_is_src(instr->srcs[i]) &&
          (instr->srcs[i]->flags & IR3_REG_FIRST_KILL))
         remove_src(ctx, instr, instr->srcs[i]);
   }
   for (unsigned i = 0; i < instr->dsts_count; i++) {
      if (ra_reg_is_dst(instr->dsts[i]) &&
          (instr->dsts[i]->flags & IR3_REG_UNUSED))
         remove_dst(ctx, instr->dsts[i]);
   }
}

static void
handle_input_phi(struct ra_spill_ctx *ctx, struct ir3_instruction *instr)
{
   init_dst(ctx, instr->dsts[0]);
   insert_dst(ctx, instr->dsts[0]);
}

static void
remove_input_phi(struct ra_spill_ctx *ctx, struct ir3_instruction *instr)
{
   ra_foreach_src (src, instr)
      remove_src(ctx, instr, src);
   if (instr->dsts[0]->flags & IR3_REG_UNUSED)
      remove_dst(ctx, instr->dsts[0]);
}

static void
handle_live_in(struct ra_spill_ctx *ctx, struct ir3_register *def)
{
   struct ra_spill_interval *interval = &ctx->intervals[def->name];
   ra_spill_interval_init(interval, def);
   insert_dst(ctx, def);
}

static void
handle_block(struct ra_spill_ctx *ctx, struct ir3_block *block)
{
   memset(&ctx->cur_pressure, 0, sizeof(ctx->cur_pressure));
   rb_tree_init(&ctx->reg_ctx.intervals);

   unsigned name;
   BITSET_FOREACH_SET (name, ctx->live->live_in[block->index],
                       ctx->live->definitions_count) {
      struct ir3_register *reg = ctx->live->definitions[name];
      handle_live_in(ctx, reg);
   }

   foreach_instr (instr, &block->instr_list) {
      if (instr->opc != OPC_META_PHI && instr->opc != OPC_META_INPUT &&
          instr->opc != OPC_META_TEX_PREFETCH)
         break;
      handle_input_phi(ctx, instr);
   }

   update_max_pressure(ctx);

   foreach_instr (instr, &block->instr_list) {
      if (instr->opc == OPC_META_PHI || instr->opc == OPC_META_INPUT ||
          instr->opc == OPC_META_TEX_PREFETCH)
         remove_input_phi(ctx, instr);
      else
         handle_instr(ctx, instr);
   }
}

void
ir3_calc_pressure(struct ir3_shader_variant *v, struct ir3_liveness *live,
                  struct ir3_pressure *max_pressure)
{
   struct ra_spill_ctx ctx = {};
   ctx.live = live;
   ctx.intervals = calloc(live->definitions_count, sizeof(*ctx.intervals));
   ctx.compiler = v->shader->compiler;
   spill_ctx_init(&ctx);

   foreach_block (block, &v->ir->block_list) {
      handle_block(&ctx, block);
   }

   assert(ctx.cur_pressure.full == 0);
   assert(ctx.cur_pressure.half == 0);
   assert(ctx.cur_pressure.shared == 0);

   free(ctx.intervals);

   *max_pressure = ctx.max_pressure;
}
