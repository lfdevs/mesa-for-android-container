/*
 * Copyright (C) 2021-2026 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_nir.h"

/* This optimization pass, intended to run once right before code emission,
 * analyzes direct word-aligned UBO reads and promotes a subset to moves to
 * push constant loads. It is the sole populator of the UBO push data
 * structure returned back to the command stream.
 */

/* Represents use data for a single UBO */

#define MAX_UBO_WORDS (65536 / 16)

struct pushable_ubo {
   BITSET_DECLARE(read, MAX_UBO_WORDS);
   BITSET_DECLARE(pushed, MAX_UBO_WORDS);

   /* In the first pass, this is the range in bytes accessed starting from
    * the given UBO word.  In the second pass, it's converted to an FAU index
    */
   uint8_t range_idx[MAX_UBO_WORDS];
};

static_assert(PAN_MAX_PUSH <= 256, "We assume an FAU index fits in a uint8_t");

typedef struct {
   BITSET_DECLARE(row, PAN_MAX_PUSH);
} adjacency_row;

struct opt_push_ubo_ctx {
   struct pan_fau_layout *fau;

   /* Mask of UBOs which may be pushed */
   uint32_t pushable_ubos;

   /* Mask of UBOs that are still UBOs at the end of this pass */
   uint32_t ubo_mask;

   /* Per block analysis */
   unsigned nr_ubos;
   struct pushable_ubo *ubos;

   /* Interference graph for push re-ordering */
   adjacency_row adjacency[PAN_MAX_PUSH];
};

struct ubo_range {
   int8_t ubo_idx;
   int8_t nr_words;
   uint16_t word;
};

static struct ubo_range
get_pushable_ubo_range(nir_intrinsic_instr *load,
                       const struct opt_push_ubo_ctx *ctx)
{
   /* We can't have any load_push_constant yet */
   assert(load->intrinsic != nir_intrinsic_load_push_constant);

   struct ubo_range range = {
      .ubo_idx = -1,
      .nr_words = -1,
   };

   if (load->intrinsic != nir_intrinsic_load_ubo ||
       !nir_src_is_const(load->src[0]) ||
       !nir_src_is_const(load->src[1]))
      return range;

   const uint32_t handle = nir_src_as_uint(load->src[0]);
   range.ubo_idx = pan_res_handle_get_index(handle);
   if (!(ctx->pushable_ubos & BITFIELD_BIT(range.ubo_idx)))
      return range;

   const uint32_t offset = nir_src_as_uint(load->src[1]);
   assert(load->def.bit_size >= 8);
   const uint8_t bytes = load->def.num_components * (load->def.bit_size / 8);

   /* We can't handle unaligned push constant access today */
   if ((offset % 4) != 0)
      return range;

   range.word = offset / 4;
   range.nr_words = DIV_ROUND_UP(bytes, 4);

   return range;
}

static bool
analyze_ubo_intr(nir_builder *b, nir_intrinsic_instr *load, void *data)
{
   struct opt_push_ubo_ctx *ctx = data;
   const struct ubo_range range = get_pushable_ubo_range(load, ctx);
   if (range.ubo_idx < 0 || range.nr_words < 0)
      return false;

   /* Blend constants are handled by bi_pick_blend_constants, don't
    * push them a second time.
    */
   if (b->shader->info.stage == MESA_SHADER_FRAGMENT) {
      /* PAN_UBO_SYSVALS from the gallium driver */
      unsigned sysval_ubo = 1;
      if (range.ubo_idx == sysval_ubo &&
          range.word + range.nr_words <= 4)
         return false;
   }

   struct pushable_ubo *ubo = &ctx->ubos[range.ubo_idx];
   BITSET_SET_COUNT(ubo->read, range.word, range.nr_words);
   ubo->range_idx[range.word] = MAX2(ubo->range_idx[range.word],
                                     range.nr_words);

   return false;
}

static void
add_ubo_push(struct opt_push_ubo_ctx *ctx, unsigned ubo_idx, unsigned word)
{
   struct pushable_ubo *ubo = &ctx->ubos[ubo_idx];
   assert(!BITSET_TEST(ubo->pushed, word));

   BITSET_SET(ubo->pushed, word);
   ubo->range_idx[word] = ctx->fau->count;

   pan_fau_emit_reloc(ctx->fau, (struct pan_ubo_relocation) {
      .ubo = ubo_idx,
      .offset = word * 4,
   });
}

/* We always map blend constants from the first slot in the sysval UBO to the
 * first four FAU words, so that they can be accessed from a consistent
 * location from the blend shader.
 */
static void
add_blend_constants(struct opt_push_ubo_ctx *ctx)
{
   /* PAN_UBO_SYSVALS from the gallium driver */
   unsigned sysval_ubo = 1;
   assert(ctx->pushable_ubos & BITFIELD_BIT(sysval_ubo));

   /* Blend constants are the first, non-reorderable ("fixed") relocations */
   assert(ctx->fau->count == 0 && ctx->fau->reserved == 0);

   for (unsigned channel = 0; channel < 4; channel++)
      add_ubo_push(ctx, sysval_ubo, channel);
}

/* Select UBO words to push. A sophisticated implementation would consider the
 * number of uses and perhaps the control flow to estimate benefit. This is not
 * sophisticated. Select from the last UBO first to prioritize sysvals.
 */
static void
pick_ubo_push_words(struct opt_push_ubo_ctx *ctx)
{
   for (int ubo_idx = ctx->nr_ubos - 1; ubo_idx >= 0; --ubo_idx) {
      struct pushable_ubo *ubo = &ctx->ubos[ubo_idx];

      uint32_t word;
      BITSET_FOREACH_SET(word, ubo->read, MAX_UBO_WORDS) {
         /* Don't double-push */
         if (BITSET_TEST(ubo->pushed, word))
            continue;

         /* This is still a range if the corresponding bit in pushed is not
          * yet set, which we checked above.
          */
         unsigned range = ubo->range_idx[word];
         if (pan_fau_available(ctx->fau) < range)
            return;

         for (unsigned w = 0; w < range; w++)
            add_ubo_push(ctx, ubo_idx, word + w);
      }
   }
}

/*
 * Create an undirected graph where nodes are 32-bit uniform indices and edges
 * represent that two nodes are used in the same instruction.
 *
 * The graph is constructed as an adjacency matrix stored in ctx->adjacency.
 */
static bool
analyze_alu_intr(nir_builder *b, nir_alu_instr *alu, void *data)
{
   struct opt_push_ubo_ctx *ctx = data;
   if (nir_op_is_vec_or_mov(alu->op))
      return false;

   uint8_t nodes[NIR_MAX_VEC_COMPONENTS * 2];
   uint8_t node_count = 0;

   for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
      /* We only care about the first swizzle component since this pass should
       * be run after we've already reduced ALU widths down to where we only
       * really access one word per ALU op unless it's 64-bit.
       *
       * In theory, it might be useful to chase [un]pack but this pass is
       * only ever used for OpenGL where most uniforms are 32-bit.
       */
      nir_scalar s =
         nir_scalar_resolved(alu->src[i].src.ssa, alu->src[i].swizzle[0]);

      nir_instr *s_instr = nir_def_instr(s.def);
      if (s_instr->type != nir_instr_type_intrinsic)
         continue;

      const struct ubo_range range =
         get_pushable_ubo_range(nir_instr_as_intrinsic(s_instr), ctx);
      if (range.ubo_idx < 0 || range.nr_words < 0)
         continue;

      assert(BITSET_TEST(ctx->ubos[range.ubo_idx].pushed, range.word));
      uint8_t fau_word = ctx->ubos[range.ubo_idx].range_idx[range.word];
      assert(fau_word < PAN_MAX_PUSH);
      assert(!BITSET_TEST(ctx->fau->is_const, fau_word));
      assert(ctx->fau->words[fau_word].relocation.ubo == range.ubo_idx);
      assert(ctx->fau->words[fau_word].relocation.offset == range.word * 4);

      /* Offset by the swizzle, if any.  We only care about the first swizzle
       * component since this pass should be run after we've already reduced
       * ALU widths down to where we only really access one word per ALU op
       * unless it's 64-bit.
       */
      fau_word += (s.comp * s.def->bit_size) / 32;

      nodes[node_count++] = fau_word;
      if (s.def->bit_size == 64)
         nodes[node_count++] = fau_word + 1;
   }

   /* Create clique connecting nodes[] */
   for (unsigned i = 0; i < node_count; ++i) {
      for (unsigned j = 0; j < node_count; ++j) {
         if (i == j)
            continue;

         unsigned x = nodes[i], y = nodes[j];

         /* Add undirected edge between the nodes */
         BITSET_SET(ctx->adjacency[x].row, y);
         BITSET_SET(ctx->adjacency[y].row, x);
      }
   }

   return false;
}

/* Find the connected component containing `node` with depth-first search */
static void
find_component(const adjacency_row *adjacency, BITSET_WORD *visited,
               uint8_t *component, uint8_t *size, uint8_t node)
{
   uint32_t neighbour;

   BITSET_SET(visited, node);
   component[(*size)++] = node;

   BITSET_FOREACH_SET(neighbour, adjacency[node].row, PAN_MAX_PUSH) {
      if (!BITSET_TEST(visited, neighbour)) {
         find_component(adjacency, visited, component, size, neighbour);
      }
   }
}

/*
 * Optimization pass to reorder uniforms. The goal is to reduce the number of
 * moves we emit when lowering FAU. The pass groups uniforms used by the same
 * ALU instruction.
 *
 * The pass works by creating a graph of pushed uniforms, where edges denote
 * the "both 32-bit uniforms required by the same instruction" relationship.
 * This is done by analyze_alu_intr() above.  We then perform depth-first
 * search on this graph to find the connected components, where each connected
 * component is a cluster of uniforms that are used together. We then select
 * pairs of uniforms from each connected component.  The remaining unpaired
 * uniforms (from components of odd sizes) are paired together arbitrarily.
 */
static void
reorder_ubo_push_words(struct opt_push_ubo_ctx *ctx)
{
   BITSET_DECLARE(visited, PAN_MAX_PUSH) = {0};

   uint8_t ordering[PAN_MAX_PUSH] = {0};
   uint8_t unpaired[PAN_MAX_PUSH] = {0};
   uint8_t pushed = 0, unpaired_count = 0;

   for (unsigned i = 0; i < ctx->fau->count; i++) {
      /* We're the only thing to push anything so far */
      assert(!BITSET_TEST(ctx->fau->is_const, i));
      if (BITSET_TEST(visited, i))
         continue;

      uint8_t component[PAN_MAX_PUSH] = {0};
      uint8_t size = 0;
      find_component(ctx->adjacency, visited, component, &size, i);

      /* If there is an odd number of uses, at least one use must be
       * unpaired. Arbitrarily take the last one.
       */
      if (size % 2)
         unpaired[unpaired_count++] = component[--size];

      /* The rest of uses are paired */
      assert((size % 2) == 0);

      /* Push the paired uses */
      assert(pushed + (unsigned)size < PAN_MAX_PUSH);
      typed_memcpy(ordering + pushed, component, size);
      pushed += size;
   }

   /* Push unpaired nodes at the end */
   typed_memcpy(ordering + pushed, unpaired, unpaired_count);
   pushed += unpaired_count;

   assert(pushed == ctx->fau->count);

   union pan_fau_entry fau_words[PAN_MAX_PUSH];
   typed_memcpy(fau_words, ctx->fau->words, ctx->fau->count);

   for (unsigned i = 0; i < pushed; i++) {
      assert(ordering[i] < ctx->fau->count);
      ctx->fau->words[i] = fau_words[ordering[i]];
   }
}

static bool
lower_ubo_intr(nir_builder *b, nir_intrinsic_instr *load, void *data)
{
   struct opt_push_ubo_ctx *ctx = data;
   const struct ubo_range range = get_pushable_ubo_range(load, ctx);
   if (range.ubo_idx < 0) {
      /* We don't even know the UBO index */
      ctx->ubo_mask = ~0;
      return false;
   }

   struct pushable_ubo *ubo = &ctx->ubos[range.ubo_idx];
   if (range.nr_words < 0) {
      /* We couldn't push this one */
      ctx->ubo_mask |= BITFIELD_BIT(range.ubo_idx);
      return false;
   }

   /* Check to see if we've pushed the whole range */
   for (unsigned w = 0; w < range.nr_words; w++) {
      if (!BITSET_TEST(ubo->pushed, range.word)) {
         ctx->ubo_mask |= BITFIELD_BIT(range.ubo_idx);
         return false;
      }
   }

   b->cursor = nir_before_instr(&load->instr);

   /* After re-ordering, we can't use word_idx anymore and we have to just
    * search for the result.  We could theoretically plumb the ordering
    * array through here but that would get fragile.
    *
    * We also can't assume that load_ubo are contiguous so we need to break
    * it into per-word loads.  Fortunately, Kraid should be able to clean up
    * this mess.
    */
   nir_def *words[NIR_MAX_VEC_COMPONENTS * 2];
   for (unsigned w = 0; w < range.nr_words; w++) {
      uint32_t fau_word =
         pan_lookup_pushed_ubo(ctx->fau, range.ubo_idx, (range.word + w) * 4);
      words[w] = nir_load_push_constant(b, 1, 32, nir_imm_int(b, fau_word * 4),
                                        .align_mul = 4, .align_offset = 0);
   }

   nir_def *val = nir_extract_bits(b, words, range.nr_words, 0,
                                   load->def.num_components,
                                   load->def.bit_size);
   nir_def_replace(&load->def, val);

   return true;
}

bool
pan_nir_opt_push_ubo(nir_shader *nir,
                     uint32_t pushable_ubos,
                     struct pan_fau_layout *fau,
                     uint32_t *ubo_mask_out)
{
   struct opt_push_ubo_ctx ctx = {
      .fau = fau,
      .pushable_ubos = pushable_ubos,
      .nr_ubos = nir->info.num_ubos + 1,
   };

   ctx.ubos = rzalloc_array(nir, struct pushable_ubo, ctx.nr_ubos);

   /* Analyze load_ubo intrinsics */
   nir_shader_intrinsics_pass(nir, analyze_ubo_intr, nir_metadata_all, &ctx);

   /* We first pick the blend constants, those cannot be reordered */
   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      add_blend_constants(&ctx);

   pick_ubo_push_words(&ctx);

   /* Analyze ALU instructions to build the interference graph */
   nir_shader_alu_pass(nir, analyze_alu_intr, nir_metadata_all, &ctx);

   reorder_ubo_push_words(&ctx);

   bool progress = nir_shader_intrinsics_pass(nir, lower_ubo_intr,
                                              nir_metadata_control_flow, &ctx);

   *ubo_mask_out = ctx.ubo_mask;
   ralloc_free(ctx.ubos);

   return progress;
}
