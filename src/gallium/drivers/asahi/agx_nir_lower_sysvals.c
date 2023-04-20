/*
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "util/bitset.h"
#include "util/u_dynarray.h"
#include "agx_state.h"

/*
 * Lower all system values to uniform loads. This pass tries to compact ranges
 * of contiguous uploaded uniforms to reduce the draw-time overhead of uploading
 * many tiny ranges. To do so, it works in 4 steps:
 *
 * 1. Lower NIR sysvals to loads from the system value buffers (as placeholder
 *    load_preambles)
 * 2. Walk the NIR, recording the placeholder load_preambles.
 * 2. Walk the ranges of uniforms needed, compacting into contiguous ranges.
 * 3. Fill in the load_preamble instructions with the real uniforms.
 */

#define MAX_TABLE_SIZE sizeof(struct agx_draw_uniforms)

struct table_state {
   /* Bitset of 16-bit uniforms pushed */
   BITSET_DECLARE(pushed, MAX_TABLE_SIZE / 2);

   /* Element size in 16-bit units, so we may split ranges of different sizes
    * to guarantee natural alignment.
    */
   uint8_t element_size[MAX_TABLE_SIZE / 2];
};

struct state {
   /* Array of load_preamble nir_intrinsic_instr's to fix up at the end */
   struct util_dynarray load_preambles;

   struct table_state tables[AGX_NUM_SYSVAL_TABLES];
};

static nir_ssa_def *
load_sysval(nir_builder *b, unsigned dim, unsigned bitsize, uint8_t table,
            uint16_t offset)
{
   /* Encode as a sideband */
   uint32_t packed = (((uint32_t)table) << 16) | ((uint32_t)offset);
   return nir_load_preamble(b, dim, bitsize, .base = packed);
}

static nir_ssa_def *
load_sysval_root(nir_builder *b, unsigned dim, unsigned bitsize, void *ptr)
{
   return load_sysval(b, dim, bitsize, AGX_SYSVAL_TABLE_ROOT, (uintptr_t)ptr);
}

static nir_ssa_def *
load_sysval_indirect(nir_builder *b, unsigned dim, unsigned bitsize,
                     uint8_t table, void *base, nir_ssa_def *offset_el)
{
   nir_ssa_scalar scalar = {offset_el, 0};
   unsigned stride = (dim * bitsize) / 8;

   if (nir_ssa_scalar_is_const(scalar)) {
      /* Load the sysval directly */
      return load_sysval(
         b, dim, bitsize, table,
         (uintptr_t)base + (nir_ssa_scalar_as_uint(scalar) * stride));
   } else {
      /* Load the base address of the table */
      struct agx_draw_uniforms *u = NULL;
      nir_ssa_def *table_base = load_sysval_root(b, 1, 64, &u->tables[table]);

      /* Load address of the array in the table */
      nir_ssa_def *array_base = nir_iadd_imm(b, table_base, (uintptr_t)base);

      /* Index into the table and load */
      nir_ssa_def *address = nir_iadd(
         b, array_base, nir_u2u64(b, nir_imul_imm(b, offset_el, stride)));
      return nir_load_global_constant(b, address, bitsize / 8, dim, bitsize);
   }
}

static nir_ssa_def *
lower_intrinsic(nir_builder *b, nir_intrinsic_instr *intr)
{
   struct agx_draw_uniforms *u = NULL;

   switch (intr->intrinsic) {
   case nir_intrinsic_load_vbo_base_agx:
      return load_sysval_indirect(b, 1, 64, AGX_SYSVAL_TABLE_ROOT,
                                  &u->vs.vbo_base, intr->src[0].ssa);
   case nir_intrinsic_load_ubo_base_agx:
      return load_sysval_indirect(b, 1, 64, AGX_SYSVAL_TABLE_ROOT, u->ubo_base,
                                  intr->src[0].ssa);
   case nir_intrinsic_load_texture_base_agx:
      return load_sysval_root(b, 1, 64, &u->texture_base);
   case nir_intrinsic_load_blend_const_color_r_float:
      return load_sysval_root(b, 1, 32, &u->fs.blend_constant[0]);
   case nir_intrinsic_load_blend_const_color_g_float:
      return load_sysval_root(b, 1, 32, &u->fs.blend_constant[1]);
   case nir_intrinsic_load_blend_const_color_b_float:
      return load_sysval_root(b, 1, 32, &u->fs.blend_constant[2]);
   case nir_intrinsic_load_blend_const_color_a_float:
      return load_sysval_root(b, 1, 32, &u->fs.blend_constant[3]);
   case nir_intrinsic_load_ssbo_address:
      return load_sysval_indirect(b, 1, 64, AGX_SYSVAL_TABLE_ROOT,
                                  &u->ssbo_base, intr->src[0].ssa);
   case nir_intrinsic_get_ssbo_size:
      return load_sysval_indirect(b, 1, 32, AGX_SYSVAL_TABLE_ROOT,
                                  &u->ssbo_size, intr->src[0].ssa);
   case nir_intrinsic_load_num_workgroups:
      return load_sysval(b, 3, 32, AGX_SYSVAL_TABLE_GRID, 0);
   default:
      return NULL;
   }
}

/* Step 1. Lower NIR sysvals */
static bool
lower_sysvals(nir_builder *b, nir_instr *instr, void *data)
{
   b->cursor = nir_before_instr(instr);
   nir_dest *dest;
   nir_ssa_def *replacement = NULL;

   if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      dest = &intr->dest;
      replacement = lower_intrinsic(b, intr);
   } else if (instr->type == nir_instr_type_tex) {
      nir_tex_instr *tex = nir_instr_as_tex(instr);
      dest = &tex->dest;

      if (tex->op != nir_texop_lod_bias_agx)
         return false;

      struct agx_draw_uniforms *u = NULL;

      int src_idx = nir_tex_instr_src_index(tex, nir_tex_src_texture_offset);
      if (src_idx >= 0) {
         replacement =
            load_sysval_indirect(b, 1, 16, AGX_SYSVAL_TABLE_ROOT, u->lod_bias,
                                 tex->src[src_idx].src.ssa);
      } else {
         replacement =
            load_sysval_root(b, 1, 16, &u->lod_bias[tex->sampler_index]);
      }
   }

   if (replacement != NULL) {
      nir_ssa_def_rewrite_uses(&dest->ssa, replacement);
      return true;
   } else {
      return false;
   }
}

/* Step 2: Record system value loads */
static bool
record_loads(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_load_preamble)
      return false;

   assert(nir_dest_bit_size(intr->dest) >= 16 && "no 8-bit sysvals");
   unsigned dim = nir_dest_num_components(intr->dest);
   unsigned element_size = nir_dest_bit_size(intr->dest) / 16;
   unsigned length = dim * element_size;

   struct state *state = data;
   unsigned base = nir_intrinsic_base(intr);
   struct table_state *table = &state->tables[base >> 16];
   unsigned offset = base & 0xFFFF;
   assert((offset % 2) == 0 && "all entries are aligned by ABI");

   BITSET_SET_RANGE(table->pushed, (offset / 2), (offset / 2) + length - 1);

   for (unsigned i = 0; i < length; ++i) {
      if (table->element_size[(offset / 2) + i])
         assert((table->element_size[(offset / 2) + i]) == element_size);
      else
         table->element_size[(offset / 2) + i] = element_size;
   }

   util_dynarray_append(&state->load_preambles, nir_intrinsic_instr *, intr);
   return false;
}

/* Step 3: Decide where to push the system values */
static struct agx_push_range *
find_push_range_containing(struct agx_compiled_shader *shader, uint8_t table,
                           uint16_t offset)
{
   for (unsigned i = 0; i < shader->push_range_count; ++i) {
      struct agx_push_range *range = &shader->push[i];

      if (range->table != table)
         continue;

      /* range->length is 16-bit words, need to convert. offset is bytes. */
      uint16_t length_B = range->length * 2;

      if (range->offset <= offset && offset < (range->offset + length_B))
         return range;
   }

   unreachable("no containing range");
}

static unsigned
lay_out_table(struct agx_compiled_shader *shader, struct table_state *state,
              unsigned table_index, unsigned uniform)
{
   unsigned start, end;
   BITSET_FOREACH_RANGE(start, end, state->pushed, sizeof(state->pushed) * 8) {
      unsigned range_start = start;

      do {
         uint8_t size = state->element_size[range_start];

         /* Find a range of constant element size. [range_start, range_end).
          * Ranges may be at most 64 halfs.
          */
         unsigned range_end;
         for (range_end = range_start + 1;
              range_end < end && state->element_size[range_end] == size &&
              range_end < range_start + 64;
              ++range_end)
            ;

         /* Now make the range with the given size (naturally aligned) */
         uniform = ALIGN_POT(uniform, size);

         assert((shader->push_range_count < ARRAY_SIZE(shader->push)) &&
                "AGX_MAX_PUSH_RANGES must be an upper bound");

         /* Offsets must be aligned to 4 bytes, this may require pushing a
          * little more than intended (otherwise we would need extra copies)
          */
         range_start = ROUND_DOWN_TO(range_start, 4 / 2);

         shader->push[shader->push_range_count++] = (struct agx_push_range){
            .uniform = uniform,
            .table = table_index,
            .offset = range_start * 2 /* bytes, not elements */,
            .length = (range_end - range_start),
         };

         uniform += (range_end - range_start);
         range_start = range_end;
      } while (range_start < end);
   }

   return uniform;
}

static unsigned
lay_out_uniforms(struct agx_compiled_shader *shader, struct state *state)
{
   unsigned uniform = 0;

   /* Lay out each system value table */
   for (uint8_t t = 0; t < AGX_NUM_SYSVAL_TABLES; ++t)
      uniform = lay_out_table(shader, &state->tables[t], t, uniform);

   /* Step 4: Fill in the loads */
   util_dynarray_foreach(&state->load_preambles, nir_intrinsic_instr *, intr) {
      uint32_t base = nir_intrinsic_base(*intr);
      uint8_t table = base >> 16;
      uint16_t offset = base & 0xFFFF;

      struct agx_push_range *range =
         find_push_range_containing(shader, table, offset);

      nir_intrinsic_set_base(*intr,
                             range->uniform + ((offset - range->offset) / 2));
   }

   return uniform;
}

bool
agx_nir_lower_sysvals(nir_shader *shader, struct agx_compiled_shader *compiled,
                      unsigned *push_size)
{
   bool progress = nir_shader_instructions_pass(
      shader, lower_sysvals, nir_metadata_block_index | nir_metadata_dominance,
      NULL);

   if (!progress) {
      *push_size = 0;
      return false;
   }

   struct state state = {0};
   nir_shader_instructions_pass(
      shader, record_loads, nir_metadata_block_index | nir_metadata_dominance,
      &state);

   *push_size = lay_out_uniforms(compiled, &state);
   return true;
}
