/* Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_nir.h"
#include "util/u_dynarray.h"

struct push_range_entry
{
   struct anv_push_range range;
   int benefit;
};

static int
set_score(uint8_t set)
{
   /* UBO bindings */
   if (set < MAX_SETS)
      return 1;

   /* Promotion of descriptor data, higher score than UBOs because of inline
    * uniforms or data from the descriptor that can be used for later resource
    * access.
    */
   switch (set) {
   case ANV_DESCRIPTOR_SET_DESCRIPTORS: return 3;
   case ANV_DESCRIPTOR_SET_PUSH_POINTER: return 3;
   default:                             UNREACHABLE("unexpected push set");
   }
}

static int
score(const struct push_range_entry *entry)
{
   return 2 * entry->benefit - entry->range.length;
}

/**
 * Compares score for two UBO range entries.
 *
 * For a descending qsort().
 */
static int
cmp_push_range_entry(const void *va, const void *vb)
{
   const struct push_range_entry *a = va;
   const struct push_range_entry *b = vb;

   /* Rank based on scores, descending order */
   int delta = score(b) - score(a);

   /* Then use promotion type, descending order */
   if (delta == 0)
      delta = set_score(b->range.set) - set_score(a->range.set);

   /* Then use the set index as a tie-breaker, descending order */
   if (delta == 0)
      delta = b->range.set - a->range.set;

   /* Then use the UBO block index as a tie-breaker, descending order */
   if (delta == 0)
      delta = b->range.index - a->range.index;

   /* Finally use the start offset as a second tie-breaker, ascending order */
   if (delta == 0)
      delta = a->range.start - b->range.start;

   return delta;
}

enum push_block_type {
   PUSH_BLOCK_TYPE_UBO = 1,
   PUSH_BLOCK_TYPE_POINTER = 2,
};

struct push_block_key
{
   enum push_block_type type;
   uint32_t             index;
};

struct push_block_info
{
   struct push_block_key key;

   /* Each bit in the offsets bitfield represents a 32-byte section of data.
    * If it's set to one, there is interesting UBO data at that offset.  If
    * not, there's a "hole" - padding between data - or just nothing at all.
    */
   uint64_t offsets;
   uint8_t uses[64];
};

struct push_analysis_state
{
   const struct intel_device_info *devinfo;
   struct hash_table *blocks;
};

static uint32_t
push_block_key_hash(const void *key)
{
   return _mesa_hash_data(key, sizeof(struct push_block_key));
}

static bool
push_block_key_compare(const void *key1, const void *key2)
{
   return memcmp(key1, key2, sizeof(struct push_block_key)) == 0;
}

static struct push_block_info *
get_block_info(struct push_analysis_state *state,
               enum push_block_type type, uint32_t index)
{
   struct push_block_key key = { .type = type, .index = index, };
   struct hash_entry *entry =
      _mesa_hash_table_search(state->blocks, &key);
   if (entry)
      return (struct push_block_info *) entry->data;

   struct push_block_info *info =
      rzalloc(state->blocks, struct push_block_info);
   info->key = key;
   _mesa_hash_table_insert(state->blocks, &info->key, info);

   return info;
}

static void
maybe_add_pushable_ubo(struct push_analysis_state *state,
                       nir_intrinsic_instr *intrin)
{
   const int block = anv_nir_get_ubo_binding_push_block(intrin->src[0]);
   const unsigned byte_offset = nir_src_as_uint(intrin->src[1]);
   const int offset = byte_offset / state->devinfo->grf_size;

   /* Avoid shifting by larger than the width of our bitfield, as this
    * is undefined in C.  Even if we require multiple bits to represent
    * the entire value, it's OK to record a partial value - the backend
    * is capable of falling back to pull loads for later components of
    * vectors, as it has to shrink ranges for other reasons anyway.
    */
   if (offset >= 64)
      return;

   /* The value might span multiple GRFs. */
   const unsigned num_components =
      nir_def_last_component_read(&intrin->def) + 1;
   const int bytes = num_components * (intrin->def.bit_size / 8);
   const int start = ROUND_DOWN_TO(byte_offset, state->devinfo->grf_size);
   const int end = align(byte_offset + bytes, state->devinfo->grf_size);
   const int chunks = (end - start) / state->devinfo->grf_size;

   /* TODO: should we count uses in loops as higher benefit? */

   struct push_block_info *info =
      get_block_info(state, PUSH_BLOCK_TYPE_UBO, block);
   info->offsets |= ((1ull << chunks) - 1) << offset;
   info->uses[offset]++;
}

/* Chase a pattern like this :
 *
 *     con 32x2    %2 = @load_push_constant (%1 (0x20)) (base=0, range=64, align_mul=256, align_offset=32)
 *     con 64      %3 = pack_64_2x32_split %2.x, %2.y
 *     con 64      %4 = load_const (0x000000000000000c = 12)
 *     con 64      %5 = iadd %4 (0xc), %3
 *     con 32      %6 = @load_global_constant (%5) (access=readonly|reorderable, align_mul=4, align_offset=0)
 */
bool
anv_nir_is_pushable_pointer(nir_intrinsic_instr *intrin,
                            uint32_t *out_push_offset,
                            uint32_t *out_load_offset)
{
   assert(intrin->intrinsic == nir_intrinsic_load_global_constant);

   if (!(nir_intrinsic_access(intrin) & ACCESS_NON_WRITEABLE))
      return false;

   if (nir_intrinsic_align_mul(intrin) < 32)
      return false;

   nir_scalar val = { intrin->src[0].ssa, 0 };

   /* Extract constant offset if any */
   *out_load_offset = 0;
   nir_alu_instr *alu;
   if (nir_scalar_is_alu(val) &&
       (alu = nir_def_as_alu(val.def))->op == nir_op_iadd) {
      for (unsigned i = 0; i < 2; ++i) {
         nir_scalar add_src = { alu->src[i].src.ssa, alu->src[i].swizzle[val.comp] };
         if (nir_scalar_is_const(add_src)) {
            *out_load_offset = nir_scalar_as_uint(add_src);
         } else if (val.def == intrin->src[0].ssa) {
            /* This is the non constant part of the iadd, if the other source
             * is constant, we'll gather the value in the previous if block,
             * otherwise we'll give up on this in the next else block.
             */
            val = add_src;
         } else {
            return false;
         }
      }
   }

   /* Unwrap packing
    *
    * TODO: consider swizzle
    */
   if (nir_scalar_is_alu(val)) {
      nir_alu_instr *pack_alu = nir_def_as_alu(val.def);
      if (pack_alu->op != nir_op_pack_64_2x32_split)
         return false;

      val = (nir_scalar){ pack_alu->src[0].src.ssa, pack_alu->src[0].swizzle[0] };
   }

   if (!nir_scalar_is_intrinsic(val))
      return false;

   nir_intrinsic_instr *push_intrin = nir_def_as_intrinsic(val.def);
   if (push_intrin->intrinsic != nir_intrinsic_load_push_constant)
      return false;

   if (!nir_src_is_const(push_intrin->src[0]))
      return false;

   *out_push_offset = nir_intrinsic_base(push_intrin) +
                      nir_src_as_uint(push_intrin->src[0]);
    return true;
}

static void
add_pushable_pointer(struct push_analysis_state *state,
                     nir_intrinsic_instr *intrin,
                     uint32_t push_byte_offset,
                     uint32_t load_byte_offset)
{
   const int offset = load_byte_offset / state->devinfo->grf_size;

   /* Avoid shifting by larger than the width of our bitfield, as this
    * is undefined in C.  Even if we require multiple bits to represent
    * the entire value, it's OK to record a partial value - the backend
    * is capable of falling back to pull loads for later components of
    * vectors, as it has to shrink ranges for other reasons anyway.
    */
   if (offset >= 64)
      return;

   const unsigned num_components =
      nir_def_last_component_read(&intrin->def) + 1;
   const int bytes = num_components * (intrin->def.bit_size / 8);
   const int start = ROUND_DOWN_TO(load_byte_offset, state->devinfo->grf_size);
   const int end = align(load_byte_offset + bytes, state->devinfo->grf_size);
   const int chunks = (end - start) / state->devinfo->grf_size;

   struct push_block_info *info =
      get_block_info(state, PUSH_BLOCK_TYPE_POINTER, push_byte_offset);
   info->offsets |= ((1ull << chunks) - 1) << offset;
   info->uses[offset]++;
}

static void
analyze_pushable_block(struct push_analysis_state *state, nir_block *block)
{
   nir_foreach_instr(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
     switch (intrin->intrinsic) {
      case nir_intrinsic_load_ubo:
         if (anv_nir_is_promotable_ubo_binding(intrin->src[0]) &&
             nir_src_is_const(intrin->src[1]))
            maybe_add_pushable_ubo(state, intrin);
         break;

     case nir_intrinsic_load_global_constant: {
        uint32_t push_offset, load_offset;
        if (anv_nir_is_pushable_pointer(intrin, &push_offset, &load_offset))
           add_pushable_pointer(state, intrin, push_offset, load_offset);
        break;
     }

     default:
         break;
      }
   }
}

static void
print_push_entry(FILE *file,
                 const struct push_block_info *info,
                 const struct push_range_entry *entry,
                 struct push_analysis_state *state)
{
   fprintf(file,
           "set %2d, index %2d, start %2d, length %2d, bits = %"PRIx64", "
           "benefit %2d, cost %2d, score = %2d\n",
           entry->range.set, entry->range.index,
           entry->range.start, entry->range.length,
           info ? info->offsets : 0ul, entry->benefit, entry->range.length, score(entry));
}

void
anv_nir_analyze_push_constants_ranges(nir_shader *nir,
                                      const struct intel_device_info *devinfo,
                                      const struct anv_pipeline_push_map *push_map,
                                      struct anv_push_range out_ranges[4])
{
   void *mem_ctx = ralloc_context(NULL);

   struct push_analysis_state state = {
      .devinfo = devinfo,
      .blocks = _mesa_hash_table_create(mem_ctx,
                                        push_block_key_hash,
                                        push_block_key_compare),
   };

   /* Walk the IR, recording how many times each UBO block/offset is used. */
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         analyze_pushable_block(&state, block);
      }
   }

   /* Find ranges: a block, starting register-size aligned byte offset, and
    * length.
    */
   struct util_dynarray ranges;
   util_dynarray_init(&ranges, mem_ctx);

   hash_table_foreach(state.blocks, entry) {
      const struct push_block_info *info = entry->data;
      uint64_t offsets = info->offsets;

      /* Walk through the offsets bitfield, finding contiguous regions of
       * set bits:
       *
       *   0000000001111111111111000000000000111111111111110000000011111100
       *            ^^^^^^^^^^^^^            ^^^^^^^^^^^^^^        ^^^^^^
       *
       * Each of these will become a UBO range.
       */
      while (offsets != 0) {
         /* Find the first 1 in the offsets bitfield.  This represents the
          * start of a range of interesting UBO data.  Make it zero-indexed.
          */
         int first_bit = ffsll(offsets) - 1;

         /* Find the first 0 bit in offsets beyond first_bit.  To find the
          * first zero bit, we find the first 1 bit in the complement.  In
          * order to ignore bits before first_bit, we mask off those bits.
          */
         int first_hole = ffsll(~offsets & ~((1ull << first_bit) - 1)) - 1;

         if (first_hole == -1) {
            /* If we didn't find a hole, then set it to the end of the
             * bitfield.  There are no more ranges to process.
             */
            first_hole = 64;
            offsets = 0;
         } else {
            /* We've processed all bits before first_hole.  Mask them off. */
            offsets &= ~((1ull << first_hole) - 1);
         }

         struct push_range_entry *entry =
            util_dynarray_grow(&ranges, struct push_range_entry, 1);

         if (info->key.type == PUSH_BLOCK_TYPE_UBO) {
            assert(info->key.index < push_map->block_count);
            const struct anv_pipeline_binding *binding =
               &push_map->block_to_descriptor[info->key.index];
            entry->range.set = binding->set;
            entry->range.index = binding->index;
            entry->range.dynamic_offset_index = binding->dynamic_offset_index;
         } else {
            entry->range.set = ANV_DESCRIPTOR_SET_PUSH_POINTER;
            entry->range.index = info->key.index;
         }
         entry->range.start = first_bit;
         /* first_hole is one beyond the end, so we don't need to add 1 */
         entry->range.length = first_hole - first_bit;
         entry->benefit = 0;

         for (int i = 0; i < entry->range.length; i++)
            entry->benefit += info->uses[first_bit + i];

         if (false)
            print_push_entry(stderr, info, entry, &state);
      }
   }

   /* TODO: Consider combining ranges.
    *
    * We can only push 4 ranges via 3DSTATE_CONSTANT_XS.  If there are
    * more ranges, and two are close by with only a small hole, it may be
    * worth combining them.  The holes will waste register space, but the
    * benefit of removing pulls may outweigh that cost.
    */

   /* Sort the list so the most beneficial ranges are at the front. */
   int nr_entries = ranges.size / sizeof(struct push_range_entry);
   if (nr_entries > 0) {
      qsort(ranges.data, nr_entries, sizeof(struct push_range_entry),
            cmp_push_range_entry);
   }

   if (false) {
      util_dynarray_foreach(&ranges, struct push_range_entry, entry) {
         print_push_entry(stderr, NULL, entry, &state);
      }
   }

   struct push_range_entry *entries = ranges.data;

   for (unsigned i = 0; i < nr_entries; i++) {
      entries[i].range.start *= devinfo->grf_size / 32;
      entries[i].range.length *= devinfo->grf_size / 32;
   }

   /* Return the top 4, limited to the maximum number of push registers.
    *
    * The Vulkan driver sets up additional non-UBO push constants, so it may
    * need to shrink these ranges further (see anv_nir_compute_push_layout.c).
    * The OpenGL driver treats legacy uniforms as a UBO, so this is enough.
    *
    * To limit further, simply drop the tail of the list, as that's the least
    * valuable portion.
    */
   const int max_ubos = 4;
   nr_entries = MIN2(nr_entries, max_ubos);

   const unsigned max_push = 64;
   unsigned total_push = 0;

   for (unsigned i = 0; i < nr_entries; i++) {
      if (total_push + entries[i].range.length > max_push)
         entries[i].range.length = max_push - total_push;
      total_push += entries[i].range.length;
   }

   for (int i = 0; i < nr_entries; i++)
      out_ranges[i] = entries[i].range;
   for (int i = nr_entries; i < 4; i++)
      out_ranges[i] = (struct anv_push_range) {};

   ralloc_free(ranges.mem_ctx);
}
